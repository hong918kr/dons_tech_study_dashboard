# H2. 기기 → 서버 전송 — BLE throughput, Wi-Fi/셀룰러, USB, 배치 업로드, 재개, 무결성

> **이 노트를 다 읽으면**: BLE의 PHY · connection interval · DLE · ATT_MTU로부터 응용 throughput을 손으로 유도하고 "왜 실제로는 그보다 낮은가"를 설명할 수 있다 · Wi-Fi/셀룰러의 tail energy 때문에 업로드를 왜, 얼마나 배치해야 하는지 숫자로 말할 수 있다 · 끊겨도 · 중복돼도 · 깨져도 안전한 재개 가능 업로드(chunk + offset + idempotency key + CRC + SHA-256 + ACK 후 삭제)를 설계하고 직접 돌려 볼 수 있다 · UART/BLE/USB 바이트 스트림 위의 framing · CRC · ARQ와 기기 업로드 스케줄러를 설계할 수 있다
> **JD 연결**: "Build data collection and **ingestion pipelines** for … various sensors, **at scale**", "Experience building **sensor data collection pipelines**" — study_prep_list **H2**: BLE throughput(MTU, connection interval), Wi-Fi, USB / 배치 업로드, 재개(resume), CRC 무결성. 함께 다루는 행: **D7**(무선 에너지/바이트와 오프로드 교차점), **E8**(무선 칩 연결 · coex · COBS+CRC framing), **H1**(기기 로그 포맷 — 이 노트의 입력), **H3**(백엔드 ingestion — 이 노트의 출력), **H6**(프라이버시 · 암호화), **H8**(fleet 수집 정책)
> **Don 기준 난이도**: 무선 칩 통합(bring-up, coex, throughput · 전력 sign-off), 버스 프로토콜 디버깅, CRC, 링버퍼, NVMe 큐의 "submit → completion → 해제" 감각은 이미 있다 / BLE를 **GATT 응용 계층의 바이트/초**로 환산하는 공식, 셀룰러 RRC tail과 배치의 에너지 수학, HTTP 기반 재개 · 멱등 업로드, backoff + jitter, 기기 업로드 스케줄러 설계를 새로 배운다
> **선행 노트**: D7 (6.3절 기기 vs 클라우드 — 무선 에너지/바이트와 고정비), E8 (2.4절 COBS + CRC-16 framing, 8절 무선 칩과 coexistence), G7 (4.4절 BLE 시각 동기화 — 구현 의존 hedge), H1 (기기 로그 포맷 — 같은 시기에 작성 중), H3 (백엔드 — 같은 시기에 작성 중)

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 데이터는 기기 안에서 쓸모가 없다

H1에서 센서 데이터를 flash에 잘 쌓았다고 하자. 그 데이터로 모델을 학습하고(H4, H5), 품질을 모니터링하고(H7), 버그를 고치려면(crash dump) **서버에 도착해야** 한다. "at scale"이라는 JD 문구는 결국 **수천 대 기기가 매일, 배터리를 거의 쓰지 않고, 하나도 잃지 않고, 하나도 중복 없이** 데이터를 올리는 일이다.

이 노트는 그 경로의 가운데, 즉 "기기의 flash"에서 "서버의 업로드 API"까지를 다룬다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<rect x="20" y="110" width="150" height="110" rx="8" fill="#4a7bd0" fill-opacity="0.15" stroke="currentColor"/><text x="95" y="135" font-size="13" text-anchor="middle">웨어러블</text><text x="95" y="158" font-size="12" text-anchor="middle">H1 로그 (flash)</text><text x="95" y="178" font-size="12" text-anchor="middle">업로드 큐 · 스케줄러</text><text x="95" y="198" font-size="12" text-anchor="middle">CRC / SHA-256</text><rect x="260" y="20" width="140" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.18" stroke="currentColor"/><text x="330" y="42" font-size="12" text-anchor="middle">폰 앱 (relay)</text><text x="330" y="60" font-size="12" text-anchor="middle">폰의 Wi-Fi/셀룰러</text><rect x="260" y="100" width="140" height="40" rx="6" fill="#e08a3c" fill-opacity="0.18" stroke="currentColor"/><text x="330" y="125" font-size="12" text-anchor="middle">Wi-Fi AP</text>
<rect x="260" y="170" width="140" height="40" rx="6" fill="#d0564a" fill-opacity="0.15" stroke="currentColor"/><text x="330" y="195" font-size="12" text-anchor="middle">기지국 (LTE/5G)</text><rect x="260" y="250" width="140" height="50" rx="6" fill="#888" fill-opacity="0.18" stroke="currentColor"/><text x="330" y="272" font-size="12" text-anchor="middle">dock · factory PC</text><text x="330" y="290" font-size="12" text-anchor="middle">(USB)</text><rect x="500" y="100" width="160" height="120" rx="8" fill="#888" fill-opacity="0.12" stroke="currentColor"/><text x="580" y="128" font-size="13" text-anchor="middle">클라우드</text><text x="580" y="152" font-size="12" text-anchor="middle">업로드 API (H2)</text><text x="580" y="172" font-size="12" text-anchor="middle">object storage</text><text x="580" y="192" font-size="12" text-anchor="middle">queue · 스키마 (H3)</text>
<line x1="170" y1="125" x2="260" y2="45" stroke="#3f9a6b" stroke-width="2"/><text x="196" y="74" font-size="12">BLE</text><line x1="170" y1="150" x2="260" y2="120" stroke="#e08a3c" stroke-width="2"/><text x="205" y="125" font-size="12">Wi-Fi</text><line x1="170" y1="180" x2="260" y2="190" stroke="#d0564a" stroke-width="2"/><text x="200" y="200" font-size="12">cellular</text><line x1="170" y1="210" x2="260" y2="270" stroke="#888" stroke-width="2"/><text x="196" y="258" font-size="12">USB</text><line x1="400" y1="45" x2="500" y2="120" stroke="currentColor"/><line x1="400" y1="120" x2="500" y2="140" stroke="currentColor"/><line x1="400" y1="190" x2="500" y2="180" stroke="currentColor"/><line x1="400" y1="275" x2="500" y2="205" stroke="currentColor" stroke-dasharray="4 3"/><text x="450" y="80" font-size="12" text-anchor="middle">HTTPS</text>
<text x="450" y="268" font-size="12" text-anchor="middle">사내망</text>
</svg>
```

그림 1 — 웨어러블 데이터가 기기를 떠나는 네 가지 길. BLE로 폰 앱을 거치는 길(relay), Wi-Fi로 바로 가는 길, 셀룰러로 바로 가는 길, 그리고 dock · 공장에서 USB로 뽑는 길. 어느 길이든 마지막은 같은 업로드 API(이 노트)와 백엔드 ingestion(H3)이다.

### 0.2 펌웨어 비유 — NVMe 큐와 같은 문제다

Don이 SSD에서 다룬 NVMe를 떠올리자. host는 submission queue에 명령을 넣고 doorbell을 친다. 컨트롤러는 처리 후 completion queue에 결과를 쓴다. host는 **completion을 본 뒤에야** 버퍼를 해제한다. 전원이 나가도 FTL은 "어디까지 확정됐나"를 복구해야 한다.

업로드도 똑같은 구조다.

| NVMe / SSD | 기기 → 서버 업로드 |
|---|---|
| submission queue 항목 | 업로드 대기열의 파일 · chunk |
| command ID | 업로드 id, chunk offset, sequence number |
| completion entry | 서버의 ACK (HTTP 2xx + 확정된 offset) |
| completion 후 버퍼 해제 | ACK 후에만 flash에서 삭제 |
| power-loss 복구 (어디까지 커밋?) | 재부팅 후 서버에 "어디까지 받았나?" 질의 (HEAD) |
| queue depth > 1 로 latency 숨기기 | sliding window, 여러 chunk in-flight |
| end-to-end data protection (PI, CRC) | chunk CRC + 파일 SHA-256 |

이 비유 하나로 이 노트의 절반이 설명된다. 나머지 절반은 "무선은 비싸고 느리고 자주 끊긴다"는 물리 조건이다.

### 0.3 Don의 현재 일과 연결

Don은 Apple에서 무선 칩셋 통합을 한다. throughput 테스트에서 "PHY rate는 2 Mbit/s인데 왜 앱에서는 수백 kbit/s밖에 안 나오나?" 같은 질문을 이미 봤을 것이다. 이 노트는 그 질문에 **프로토콜 계층별로 바이트를 세서** 답하는 법을 정리한다. 또 coex(E8 8.2절)가 throughput과 지연을 깎는 변수라는 것, 링크 계층의 재전송이 응용 계층에서 지연 jitter로 보인다는 것도 같은 이야기다.

### 0.4 이웃 노트 요약 (다시 쓰지 않고 가리킨다)

- **D7 6.3절**: 무선 전송 에너지 ≈ 고정비(wake, 연결, tail) + 바이트 × 바이트당 에너지. BLE는 고정비가 작고 바이트당 에너지가 크며(typical 0.3–1 µJ/B), Wi-Fi는 고정비가 크고 바이트당 에너지가 작다(typical 0.1 µJ/B 자릿수). 작은 모델은 기기에서 돌리는 게 거의 항상 싸다. 이 노트 3절은 그 "고정비"를 tail과 배치로 더 깊게 판다.
- **E8 2.4절**: MCU ↔ SoC 링크에서 COBS framing + CRC-16(CCITT-FALSE, `"123456789"` → `0x29B1`)을 C로 구현했다. 이 노트 5절은 COBS를 SLIP · 길이 prefix와 비교하고, 그 위에 sequence number와 재전송(ARQ)을 얹는다.
- **E8 8절**: 무선 칩이 PCIe/UART/SPI로 붙는 방식과 coex 중재. 여기서는 무선을 "바이트를 나르는 파이프"로 보고 그 파이프의 용량과 비용을 계산한다.
- **G7 4.4절**: BLE connection event의 anchor로 시각을 맞추는 구현이 있지만 일반 GATT 앱에서는 노출되지 않는 경우가 많다(구현 의존). 업로드 데이터의 timestamp는 기기 쪽에서 붙여야 한다는 결론이 이 노트와 이어진다.
- **H1**: 기기 로그 포맷(헤더 + 바이너리 레코드, chunk별 CRC). 이 노트는 그 파일 · chunk를 **그대로** 실어 나른다고 가정한다.
- **H3**: 서버 쪽 object storage, queue, 스키마. 이 노트의 업로드 API가 끝나는 곳이 H3의 시작이다.

### 0.5 이 노트의 지도

```
                 ┌──────────── 1. 어떤 길로? (BLE / Wi-Fi / cellular / USB / 폰 relay)
                 │
 H1 flash 로그 ──┼── 2. 파이프 용량: BLE throughput 수학
                 │── 3. 파이프 비용: Wi-Fi/셀룰러 전력 상태, tail, 배치, TLS
                 │── 4. 업로드 프로토콜: chunk, 재개, 멱등, 무결성, backoff  ──► H3 ingestion
                 │── 5. 바이트 스트림 framing: COBS/SLIP, CRC, ARQ
                 │── 6. 보안
                 └── 7. 언제 올리나: 업로드 스케줄러 (충전 · Wi-Fi · quota · 우선순위)
```

---

## 1. 전송 수단 비교 — 웨어러블의 선택지

### 1.1 다섯 가지 길

먼저 용어 하나. **goodput**은 프로토콜 헤더 · 재전송을 다 빼고 응용이 실제로 받은 바이트/초다. PHY rate(무선 신호 속도)와 다르다. 이 노트에서 "throughput"은 따로 말이 없으면 응용 계층 goodput이다.

| 길 | 응용 throughput (typical, 조건에 따라 크게 다름) | 바이트당 에너지 (typical, D7) | 고정비 | 잘 맞는 데이터 |
|---|---|---|---|---|
| BLE → 폰 (GATT) | 수십 kbit/s – 약 1 Mbit/s대 (2절에서 유도) | 약 0.3–1 µJ/B | 연결 유지 시 작다 | 메트릭, 이벤트, 압축 오디오 조각, crash dump |
| Wi-Fi → 클라우드 직접 | 수 – 수십 Mbit/s | 약 0.1 µJ/B 자릿수 | 깨어나기 + 연결 + tail, 수십–수백 mJ | 큰 raw 센서 로그 (충전 중) |
| 셀룰러 → 클라우드 직접 | 망 · 카테고리에 따라 수백 kbit/s – 수십 Mbit/s | Wi-Fi와 비슷하거나 더 큼 | RRC 연결 + tail, J급일 수 있음 | 작고 급한 것 (crash, 사용자 요청), 배치된 메트릭 |
| USB (dock, 공장) | Full Speed 12 Mbit/s 신호 속도, High Speed 480 Mbit/s 신호 속도 — 실효는 그보다 낮다 | 배터리를 거의 안 씀 (외부 전원) | 케이블 연결 | 공장 테스트 로그, 개발 중 대용량 덤프, dogfood 회수 |
| 폰 relay 앱 | BLE 구간이 병목 | BLE 비용 + 폰 배터리 | 앱이 살아 있어야 함 | 위 BLE와 같음, 폰이 Wi-Fi/셀룰러로 다시 올림 |

표의 숫자는 모두 "자릿수 감"이다. 실제 값은 칩, 펌웨어, 폰 OS, 망 설정, coex 상황에 따라 몇 배씩 바뀐다. 면접에서는 숫자를 단정하지 말고 **유도하는 방법**을 보여 주는 것이 좋다.

### 1.2 Hark 같은 기기라면 (추정)

Hark 1세대 기기가 셀룰러를 가진다는 힌트가 있다(추정 — 확인 필요). 셀룰러가 있으면 "폰 없이도 올릴 수 있다"는 장점이 생기지만, 3절에서 보듯 셀룰러는 **한 번 깨울 때마다** 큰 고정비를 낸다. 그래서 흔한 설계는 이렇다.

- 큰 데이터(raw 센서 · 오디오 학습 데이터): **충전 중 + Wi-Fi**에서만.
- 작은 데이터(메트릭): 폰이 BLE로 붙어 있으면 BLE, 아니면 셀룰러로 **모아서** 가끔.
- 급한 데이터(crash dump, 사용자가 누른 "버그 신고"): 지금 쓸 수 있는 아무 길로나, 단 quota 안에서.

이 결정을 코드로 만든 것이 7절의 업로드 스케줄러다.

### 1.3 USB와 폰 relay — 잊기 쉬운 두 길

**USB**는 개발 · 공장 · dogfood 회수에서 가장 많이 쓰는 길이다. 흔한 방식은 두 가지다. (1) **CDC-ACM**(가상 시리얼 포트): 펌웨어가 바이트 스트림을 내보내고 PC 스크립트가 받는다. framing이 필요하다(5절). (2) **MSC**(Mass Storage Class): 기기가 USB 드라이브처럼 보이고 로그 파일을 복사한다. framing은 필요 없지만 기기 파일 시스템을 PC와 동시에 건드리면 안 된다. Don의 factory test 경험과 바로 이어진다 — 공장에서는 USB로 test 로그와 calibration 결과를 뽑아 MES로 올린다.

**폰 relay**는 웨어러블 → BLE → 폰 앱 → 폰의 인터넷 → 서버다. 장점은 웨어러블이 TLS · TCP를 몰라도 된다는 것(폰이 대신한다). 단점은 폰 앱이 백그라운드에서 얼마나 살아 있을지 OS가 정한다는 것이다. iOS는 Core Bluetooth 백그라운드 모드와 state restoration을, Android는 foreground service 같은 장치를 제공하지만 세부 정책은 OS 버전마다 바뀐다(확인 필요). 그래서 **업로드 프로토콜의 재개 지점은 폰이 아니라 기기와 서버가 들고 있는 것이 안전하다** — 폰 앱은 언제든 죽는다고 가정한다.

---

## 2. BLE throughput — 스펙에서 응용 바이트/초까지

### 2.1 직관 — 정해진 시각에만 열리는 버스

BLE 연결은 **두 기기가 약속한 시각에만 잠깐 무선을 켜는** 버스다. 그 약속 시각을 **anchor point**, 그 간격을 **connection interval**이라 한다. 각 anchor에서 central(보통 폰)이 먼저 패킷을 보내고, peripheral(웨어러블)이 150 µs 뒤에 답한다. 둘 다 보낼 것이 남아 있으면 이 주고받기를 같은 **connection event** 안에서 반복한다. event가 끝나면 다음 anchor까지 둘 다 무선을 끈다.

펌웨어 비유: I2C에서 master가 clock을 주는 것과 비슷하다 — 슬레이브(peripheral)는 마음대로 말하지 못하고, master(central)가 열어 준 슬롯에만 답한다. 그래서 throughput은 "한 슬롯에 몇 바이트 × 슬롯이 얼마나 자주"로 정해진다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="94" font-size="12">central</text><text x="10" y="154" font-size="12">periph.</text><line x1="60" y1="105" x2="670" y2="105" stroke="currentColor" stroke-width="0.5"/><line x1="60" y1="165" x2="670" y2="165" stroke="currentColor" stroke-width="0.5"/><rect x="60.0" y="85" width="5.3" height="20" fill="#888" stroke="currentColor" stroke-width="0.5"/><rect x="83.3" y="145" width="125.8" height="20" fill="#4a7bd0" fill-opacity="0.6" stroke="currentColor" stroke-width="0.5"/><text x="146.2" y="159" font-size="12" text-anchor="middle">data 251 B</text><rect x="227.0" y="85" width="5.3" height="20" fill="#888" stroke="currentColor" stroke-width="0.5"/><rect x="250.3" y="145" width="125.8" height="20" fill="#4a7bd0" fill-opacity="0.6" stroke="currentColor" stroke-width="0.5"/><text x="313.2" y="159" font-size="12" text-anchor="middle">data 251 B</text>
<rect x="394.1" y="85" width="5.3" height="20" fill="#888" stroke="currentColor" stroke-width="0.5"/><rect x="417.4" y="145" width="125.8" height="20" fill="#4a7bd0" fill-opacity="0.6" stroke="currentColor" stroke-width="0.5"/><text x="480.2" y="159" font-size="12" text-anchor="middle">data 251 B</text><text x="74.3" y="78" font-size="12" text-anchor="middle">poll</text><line x1="65.3" y1="180" x2="83.3" y2="180" stroke="#d0564a" stroke-width="2"/><text x="74.3" y="196" font-size="12" text-anchor="middle">T_IFS 150 µs</text><line x1="60.0" y1="60" x2="227.0" y2="60" stroke="currentColor"/><line x1="60.0" y1="55" x2="60.0" y2="65" stroke="currentColor"/><line x1="227.0" y1="55" x2="227.0" y2="65" stroke="currentColor"/><text x="143.5" y="52" font-size="12" text-anchor="middle">한 쌍 = 44+150+1048+150 = 1392 µs</text><text x="571.1" y="128.0" font-size="12">… radio off …</text>
<line x1="60" y1="40" x2="60" y2="210" stroke="#3f9a6b" stroke-dasharray="4 3"/><line x1="650" y1="40" x2="650" y2="210" stroke="#3f9a6b" stroke-dasharray="4 3"/><text x="60" y="228" font-size="12" text-anchor="middle">anchor n</text><text x="650" y="228" font-size="12" text-anchor="middle">anchor n+1</text><line x1="60" y1="250" x2="650" y2="250" stroke="#3f9a6b"/><text x="355.0" y="268" font-size="12" text-anchor="middle">connection interval (7.5 ms – 4 s, 1.25 ms 단위) — 그림은 축척이 아님</text><text x="355.0" y="290" font-size="12" text-anchor="middle">한 event에 몇 쌍이 들어가나 = 이 구간을 얼마나 채우나 (컨트롤러 의존)</text>
</svg>
```

그림 2 — 2M PHY에서 한 connection event 안의 주고받기. central이 빈 패킷(poll 겸 ACK)을 보내고, T_IFS 150 µs 뒤 peripheral이 251 B 데이터 패킷으로 답한다. 이 한 쌍이 1392 µs다. event가 끝나면 다음 anchor까지 무선이 꺼진다.

### 2.2 정의 — 스펙의 숫자들 (잘 확립된 것만)

| 항목 | 값 | 출처 · 비고 |
|---|---|---|
| connection interval | 7.5 ms – 4 s, 1.25 ms 단위 | Bluetooth Core Spec. central이 정하고 peripheral은 요청만 할 수 있다 |
| T_IFS | 150 µs | 같은 event 안 패킷 사이 간격 |
| LE 1M PHY | 1 Mbit/s 심볼 속도 → 1 µs/bit | BT 4.x부터 |
| LE 2M PHY | 2 Mbit/s → 0.5 µs/bit | BT 5.0에서 추가 |
| LE Coded PHY | S=2 (500 kbit/s), S=8 (125 kbit/s) | BT 5.0, 거리용 (FEC) |
| LL 데이터 패킷 on-air | preamble(1M: 1 B, 2M: 2 B) + Access Address 4 B + LL 헤더 2 B + payload + (암호화 시 MIC 4 B) + CRC 3 B | payload 0이면 "빈 패킷" |
| LL payload 상한 | DLE 없으면 27 B, **DLE(Data Length Extension)** 로 최대 251 B | DLE는 BT 4.2에서 추가 |
| L2CAP 헤더 | 4 B (길이 2 + 채널 ID 2) | |
| ATT notification / write command 헤더 | 3 B (opcode 1 + handle 2) | |
| ATT_MTU | 기본 23 B, 협상으로 증가. 흔히 쓰는 상한 517 | 517 = attribute 최대 512 B + prepare write 헤더 5 B |

말로 하면: 응용 데이터 1바이트가 공중에 나가려면 ATT 3 B, L2CAP 4 B, LL 헤더 · AA · preamble · CRC 10–11 B를 같이 짊어진다. 그리고 패킷 한 쌍마다 T_IFS 두 번(300 µs)과 상대의 빈 패킷을 낸다. 이 "세금"이 PHY rate와 goodput의 차이다.

**ATT_MTU**는 ATT 계층 PDU 하나의 최대 크기다. notification 하나에 실을 수 있는 응용 데이터는 `MTU − 3`이다. **DLE**는 링크 계층(LL) 패킷 하나의 payload 크기다. 둘은 다른 계층의 숫자라서 **둘 다** 커야 한다. MTU만 크고 DLE가 없으면 ATT PDU가 27 B짜리 LL 조각 여러 개로 쪼개지고(fragmentation), 조각마다 세금을 다시 낸다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<rect x="20" y="60" width="52" height="40" fill="#888" fill-opacity="0.35" stroke="currentColor"/><text x="46.0" y="78" font-size="12" text-anchor="middle">pre</text><text x="46.0" y="94" font-size="12" text-anchor="middle">2 B</text><rect x="72" y="60" width="52" height="40" fill="#888" fill-opacity="0.35" stroke="currentColor"/><text x="98.0" y="78" font-size="12" text-anchor="middle">AA</text><text x="98.0" y="94" font-size="12" text-anchor="middle">4 B</text><rect x="124" y="60" width="52" height="40" fill="#888" fill-opacity="0.35" stroke="currentColor"/><text x="150.0" y="78" font-size="12" text-anchor="middle">hdr</text><text x="150.0" y="94" font-size="12" text-anchor="middle">2 B</text><rect x="176" y="60" width="52" height="40" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor"/>
<text x="202.0" y="78" font-size="12" text-anchor="middle">L2CAP</text><text x="202.0" y="94" font-size="12" text-anchor="middle">4 B</text><rect x="228" y="60" width="52" height="40" fill="#d0564a" fill-opacity="0.35" stroke="currentColor"/><text x="254.0" y="78" font-size="12" text-anchor="middle">ATT</text><text x="254.0" y="94" font-size="12" text-anchor="middle">3 B</text><rect x="280" y="60" width="300" height="40" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/><text x="430.0" y="78" font-size="12" text-anchor="middle">응용 데이터 244</text><text x="430.0" y="94" font-size="12" text-anchor="middle">244 B</text><rect x="580" y="60" width="52" height="40" fill="#888" fill-opacity="0.35" stroke="currentColor"/><text x="606.0" y="78" font-size="12" text-anchor="middle">CRC</text><text x="606.0" y="94" font-size="12" text-anchor="middle">3 B</text>
<line x1="176" y1="115" x2="580" y2="115" stroke="currentColor"/><line x1="176" y1="110" x2="176" y2="120" stroke="currentColor"/><line x1="580" y1="110" x2="580" y2="120" stroke="currentColor"/><text x="378.0" y="133" font-size="12" text-anchor="middle">LL payload 251 B (DLE 최대) = L2CAP PDU</text><line x1="228" y1="150" x2="580" y2="150" stroke="currentColor"/><line x1="228" y1="145" x2="228" y2="155" stroke="currentColor"/><line x1="580" y1="145" x2="580" y2="155" stroke="currentColor"/><text x="404.0" y="168" font-size="12" text-anchor="middle">ATT PDU 247 B = ATT_MTU 247을 꽉 채움</text><line x1="280" y1="185" x2="580" y2="185" stroke="currentColor"/><line x1="280" y1="180" x2="280" y2="190" stroke="currentColor"/><line x1="580" y1="180" x2="580" y2="190" stroke="currentColor"/><text x="430.0" y="203" font-size="12" text-anchor="middle">notification value = MTU − 3 = 244 B</text>
<text x="20" y="30" font-size="13">2M PHY on-air: (2+4+2+251+3) B × 4 µs/B = 1048 µs</text><text x="20" y="235" font-size="12">회색 = 링크 계층 오버헤드 (암호화 링크면 payload 뒤에 MIC 4 B 추가)</text>
</svg>
```

그림 3 — MTU 247 + DLE 251의 "딱 맞는" 조합. 응용 244 B + ATT 3 B + L2CAP 4 B = 251 B가 LL payload 하나를 정확히 채운다. 그래서 MTU 247이 BLE 펌웨어에서 자주 보이는 값이다.

### 2.3 손으로 계산 — 패킷 airtime

on-air 시간 = 바이트 수 × 8 × (bit당 시간).

```
1M PHY, payload 251 B:  (1 + 4 + 2 + 251 + 3) B × 8 µs/B = 261 × 8 = 2088 µs
1M PHY, 빈 패킷:         (1 + 4 + 2 + 0 + 3)   B × 8 µs/B =  10 × 8 =   80 µs
2M PHY, payload 251 B:  (2 + 4 + 2 + 251 + 3) B × 4 µs/B = 262 × 4 = 1048 µs
2M PHY, 빈 패킷:         (2 + 4 + 2 + 0 + 3)   B × 4 µs/B =  11 × 4 =   44 µs
```

한 쌍(data + T_IFS + 빈 패킷 + T_IFS)의 시간:

```
1M: 2088 + 150 + 80 + 150 = 2468 µs   → 244 B × 8 / 2468 µs ≈ 0.791 Mbit/s
2M: 1048 + 150 + 44 + 150 = 1392 µs   → 244 B × 8 / 1392 µs ≈ 1.402 Mbit/s
```

말로 하면: event를 빈틈없이 채울 수 있다는 **이상적 가정**에서 응용 throughput 상한은 1M에서 약 0.79 Mbit/s, 2M에서 약 1.4 Mbit/s다. 흔히 듣는 "2M PHY면 실제 1.3–1.4 Mbit/s 정도가 한계"라는 말이 여기서 나온다 — 외워서가 아니라 **헤더와 T_IFS를 세서** 나온다. 2 Mbit/s PHY의 70 %다.

Coded PHY(S=8)는 구조가 다르다: preamble 80 µs + AA 256 µs + CI 16 µs + TERM1 24 µs가 고정이고, 그 뒤 LL 헤더 · payload · CRC를 bit당 8 µs(64 µs/B)로 보내고 TERM2 24 µs를 붙인다. 251 B 패킷이 16784 µs다. 거리를 위해 속도를 1/16로 버린 PHY다.

### 2.4 손으로 계산 — 면접 단골 문제

> 2M PHY, connection interval 30 ms, DLE 251, MTU 247. 이론 상한은?

```
한 쌍 = 1392 µs  (2.3절)
30 ms 안에 들어가는 쌍 = floor(30000 / 1392) = floor(21.55) = 21
event당 응용 바이트 = 21 × 244 B = 5124 B
throughput = 5124 B × 8 / 30 ms = 1.366 Mbit/s
```

그런데 이것은 **"한 event에 패킷을 무제한으로 넣을 수 있다"** 는 가정이다. 현실에서는 한 event에 몇 개를 넣을지 **컨트롤러(BLE 칩 펌웨어)와 상대(폰)가 정한다**. 폰 쪽 칩이 Wi-Fi와 안테나 · 시간을 나눠 쓰고(coex), 여러 기기와 연결돼 있으면 event 길이를 줄인다. 이 제한은 스펙 상수가 아니라 구현 의존이므로, 측정해서 알아내야 한다. 예를 들어 event당 6개로 제한되면:

```
6 × 244 B × 8 / 30 ms = 390 kbit/s     (이상적 1366의 29 %)
```

**그래서 BLE throughput 질문의 정답 형식은 "이상적 상한은 이렇게 유도되고, 실제는 event당 패킷 수가 정한다, 그건 측정한다"이다.**

### 2.5 코드로 확인 — BLE throughput 계산기

PHY, interval, MTU, DLE, event당 패킷 제한을 넣으면 응용 throughput을 내는 계산기다. fragmentation(ATT PDU가 LL 패킷 여러 개로 쪼개지는 경우)도 센다.

```python
# BLE 응용 계층 throughput 계산기 — spec 기준 airtime, 이상적인 링크(재전송 없음)
T_IFS = 150                                             # µs, 패킷 사이 고정 간격
def airtime_us(ll_payload, phy, mic=False):
    n = ll_payload + (4 if mic and ll_payload else 0)   # MIC는 payload가 있을 때만 붙는다
    if phy == "1M": return (1 + 4 + 2 + n + 3) * 8      # preamble1 AA4 hdr2 payload CRC3, 1 µs/bit
    if phy == "2M": return (2 + 4 + 2 + n + 3) * 4      # preamble2, 0.5 µs/bit
    if phy == "S8": return 80 + 256 + 16 + 24 + (2 + n + 3) * 64 + 24   # LE Coded S=8
def throughput_kbps(phy, ci_ms, mtu, dle=True, max_pkts=None, mic=False):
    ll_max = 251 if dle else 27                         # LL payload 상한 (DLE 없으면 27)
    value = min(mtu - 3, 512)                           # notification 1개의 응용 바이트
    l2cap = value + 3 + 4                               # + ATT 헤더 3 + L2CAP 헤더 4
    frags = [ll_max] * (l2cap // ll_max) + ([l2cap % ll_max] if l2cap % ll_max else [])
    t_sdu = sum(airtime_us(f, phy, mic) + T_IFS + airtime_us(0, phy) + T_IFS for f in frags)
    t_pkt = t_sdu / len(frags)                          # LL 패킷 1개 + 빈 ACK의 평균 시간
    n_pkt = int(ci_ms * 1000 // t_pkt)                  # 한 connection event에 들어가는 LL 패킷
    if max_pkts is not None: n_pkt = min(n_pkt, max_pkts)   # 컨트롤러/폰 제한 (구현 의존)
    bytes_per_event = n_pkt / len(frags) * value
    return bytes_per_event * 8 / ci_ms, n_pkt, len(frags)   # bit/ms = kbit/s
if __name__ == "__main__":
    for phy in ["1M", "2M", "S8"]:
        print(f"{phy}: 251 B 패킷 {airtime_us(251, phy):5d} µs, 빈 패킷 {airtime_us(0, phy):3d} µs")
    cases = [("1M", 30, 23, False, None), ("1M", 30, 247, True, None), ("2M", 30, 247, True, None),
             ("2M", 30, 247, True, 6), ("2M", 7.5, 247, True, None), ("2M", 30, 185, True, None),
             ("2M", 30, 517, True, None), ("S8", 30, 247, True, None), ("2M", 30, 23, False, 4)]
    print("PHY CI(ms)  MTU DLE limit  LLpkt/event frag  app kbps")
    for phy, ci, mtu, dle, lim in cases:
        kbps, n, nf = throughput_kbps(phy, ci, mtu, dle, lim)
        print(f"{phy:3s} {ci:5.1f} {mtu:5d}  {'Y' if dle else 'N'}  {str(lim):>5s} {n:8d} {nf:7d} {kbps:9.0f}")
```

```text
1M: 251 B 패킷  2088 µs, 빈 패킷  80 µs
2M: 251 B 패킷  1048 µs, 빈 패킷  44 µs
S8: 251 B 패킷 16784 µs, 빈 패킷 720 µs
PHY CI(ms)  MTU DLE limit  LLpkt/event frag  app kbps
1M   30.0    23  N   None       44       1       235
1M   30.0   247  Y   None       12       1       781
2M   30.0   247  Y   None       21       1      1366
2M   30.0   247  Y      6        6       1       390
2M    7.5   247  Y   None        5       1      1301
2M   30.0   185  Y   None       26       1      1262
2M   30.0   517  Y   None       27       3      1229
S8   30.0   247  Y   None        1       1        65
2M   30.0    23  N      4        4       1        21
```

출력에서 볼 것:

- 2M · 30 ms · MTU 247 · 제한 없음이 1366 kbit/s로 손계산과 같다. 1M은 781, DLE 없는 1M · MTU 23은 235다.
- **event당 6개 제한** 하나로 1366 → 390. 실제 기기에서 보는 숫자가 이쪽에 더 가깝다는 보고가 많다(폰 · 칩에 따라 다르다).
- **7.5 ms interval이 30 ms보다 느리다**(1301 < 1366). 7.5 ms 안에는 쌍이 5개만 들어가고(5 × 1392 = 6960 µs) 남은 540 µs는 버려진다. 짧은 interval은 **지연**에 좋지 throughput에 꼭 좋은 게 아니다 — 대신 event당 패킷 제한이 있을 때는 짧은 interval이 throughput을 올린다(다음 그림).
- MTU 517은 3조각(251 + 251 + 17)으로 쪼개져 오히려 247보다 느리다(1229).
- Coded S=8은 30 ms에 251 B 패킷 하나만 들어가 65 kbit/s다.
- 마지막 줄 2M · DLE 없음 · MTU 23 · event당 4개 = 21 kbit/s. "BLE는 느리다"는 인상은 대부분 이 기본 설정에서 온다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="300" x2="640" y2="300" stroke="currentColor"/><line x1="70" y1="300" x2="70" y2="30" stroke="currentColor"/><line x1="70.0" y1="300" x2="70.0" y2="304" stroke="currentColor"/><text x="70.0" y="318" font-size="12" text-anchor="middle">0</text><line x1="184.0" y1="300" x2="184.0" y2="304" stroke="currentColor"/><text x="184.0" y="318" font-size="12" text-anchor="middle">20</text><line x1="298.0" y1="300" x2="298.0" y2="304" stroke="currentColor"/><text x="298.0" y="318" font-size="12" text-anchor="middle">40</text><line x1="412.0" y1="300" x2="412.0" y2="304" stroke="currentColor"/><text x="412.0" y="318" font-size="12" text-anchor="middle">60</text><line x1="526.0" y1="300" x2="526.0" y2="304" stroke="currentColor"/><text x="526.0" y="318" font-size="12" text-anchor="middle">80</text>
<line x1="640.0" y1="300" x2="640.0" y2="304" stroke="currentColor"/><text x="640.0" y="318" font-size="12" text-anchor="middle">100</text><line x1="66" y1="300.0" x2="70" y2="300.0" stroke="currentColor"/><text x="62" y="304.0" font-size="12" text-anchor="end">0</text><line x1="66" y1="255.0" x2="70" y2="255.0" stroke="currentColor"/><text x="62" y="259.0" font-size="12" text-anchor="end">250</text><line x1="66" y1="210.0" x2="70" y2="210.0" stroke="currentColor"/><text x="62" y="214.0" font-size="12" text-anchor="end">500</text><line x1="66" y1="165.0" x2="70" y2="165.0" stroke="currentColor"/><text x="62" y="169.0" font-size="12" text-anchor="end">750</text><line x1="66" y1="120.0" x2="70" y2="120.0" stroke="currentColor"/><text x="62" y="124.0" font-size="12" text-anchor="end">1000</text>
<line x1="66" y1="75.0" x2="70" y2="75.0" stroke="currentColor"/><text x="62" y="79.0" font-size="12" text-anchor="end">1250</text><line x1="66" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/><text x="62" y="34.0" font-size="12" text-anchor="end">1500</text>
<polyline points="112.8,65.8 119.9,59.1 127.0,54.0 134.1,50.1 141.2,75.1 148.4,70.0 155.5,65.8 162.6,62.2 169.8,59.1 176.9,56.4 184.0,54.0 191.1,52.0 198.2,50.1 205.4,48.5 212.5,61.1 219.6,59.1 226.8,57.2 233.9,55.6 241.0,54.0 248.1,52.6 255.2,51.3 262.4,50.1 269.5,49.0 276.6,48.0 283.8,56.4 290.9,55.2 298.0,54.0 305.1,53.0 312.2,52.0 319.4,51.0 326.5,50.1 333.6,49.3 340.8,48.5 347.9,47.7 355.0,54.0 362.1,53.2 369.2,52.4 376.4,51.6 383.5,50.9 390.6,50.1 397.8,49.5 404.9,48.8 412.0,48.2 419.1,47.6 426.2,52.6 433.4,52.0 440.5,51.3 447.6,50.7 454.8,50.1 461.9,49.6 469.0,49.0 476.1,48.5 483.2,48.0 490.4,52.3 497.5,51.7 504.6,51.2 511.8,50.6 518.9,50.1 526.0,49.7 533.1,49.2 540.2,48.7 547.4,48.3 554.5,47.8 561.6,51.5 568.8,51.0 575.9,50.6 583.0,50.1 590.1,49.7 597.2,49.3 604.4,48.9 611.5,48.5 618.6,48.1 625.8,47.7 632.9,50.9 640.0,50.5" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<polyline points="112.8,159.5 119.9,179.5 127.0,159.5 134.1,175.1 141.2,159.5 148.4,172.2 155.5,159.5 162.6,170.3 169.8,159.5 176.9,168.8 184.0,159.5 191.1,167.7 198.2,159.5 205.4,166.9 212.5,159.5 219.6,166.1 226.8,159.5 233.9,165.6 241.0,159.5 248.1,165.1 255.2,159.5 262.4,164.7 269.5,159.5 276.6,164.3 283.8,159.5 290.9,164.0 298.0,159.5 305.1,163.7 312.2,159.5 319.4,163.5 326.5,159.5 333.6,163.3 340.8,159.5 347.9,163.1 355.0,159.5 362.1,162.9 369.2,159.5 376.4,162.7 383.5,159.5 390.6,162.6 397.8,159.5 404.9,162.4 412.0,159.5 419.1,162.3 426.2,159.5 433.4,162.2 440.5,159.5 447.6,162.1 454.8,159.5 461.9,162.0 469.0,159.5 476.1,161.9 483.2,159.5 490.4,161.8 497.5,159.5 504.6,161.8 511.8,159.5 518.9,161.7 526.0,159.5 533.1,161.6 540.2,159.5 547.4,161.6 554.5,159.5 561.6,161.5 568.8,159.5 575.9,161.4 583.0,159.5 590.1,161.4 597.2,159.5 604.4,161.3 611.5,159.5 618.6,161.3 625.8,159.5 632.9,157.7 640.0,159.5" fill="none" stroke="#e08a3c" stroke-width="2"/>
<polyline points="112.8,65.8 119.9,59.1 127.0,89.2 134.1,112.6 141.2,131.3 148.4,146.7 155.5,159.5 162.6,170.3 169.8,179.5 176.9,187.6 184.0,194.6 191.1,200.8 198.2,206.3 205.4,211.2 212.5,215.7 219.6,219.7 226.8,223.3 233.9,226.7 241.0,229.7 248.1,232.5 255.2,235.1 262.4,237.5 269.5,239.8 276.6,241.8 283.8,243.8 290.9,245.6 298.0,247.3 305.1,248.9 312.2,250.4 319.4,251.8 326.5,253.2 333.6,254.4 340.8,255.6 347.9,256.8 355.0,257.8 362.1,258.9 369.2,259.8 376.4,260.8 383.5,261.7 390.6,262.5 397.8,263.3 404.9,264.1 412.0,264.9 419.1,265.6 426.2,266.3 433.4,266.9 440.5,267.6 447.6,268.2 454.8,268.8 461.9,269.3 469.0,269.9 476.1,270.4 483.2,270.9 490.4,271.4 497.5,271.9 504.6,272.4 511.8,272.8 518.9,273.2 526.0,273.6 533.1,274.1 540.2,274.4 547.4,274.8 554.5,275.2 561.6,275.6 568.8,275.9 575.9,276.2 583.0,276.6 590.1,276.9 597.2,277.2 604.4,277.5 611.5,277.8 618.6,278.1 625.8,278.4 632.9,278.7 640.0,278.9" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<polyline points="112.8,284.6 119.9,286.8 127.0,288.5 134.1,289.8 141.2,290.8 148.4,291.6 155.5,292.3 162.6,292.9 169.8,293.4 176.9,293.9 184.0,294.2 191.1,294.6 198.2,294.9 205.4,295.1 212.5,295.4 219.6,295.6 226.8,295.8 233.9,296.0 241.0,296.2 248.1,296.3 255.2,296.5 262.4,296.6 269.5,296.7 276.6,296.8 283.8,296.9 290.9,297.0 298.0,297.1 305.1,297.2 312.2,297.3 319.4,297.4 326.5,297.4 333.6,297.5 340.8,297.6 347.9,297.6 355.0,297.7 362.1,297.8 369.2,297.8 376.4,297.9 383.5,297.9 390.6,298.0 397.8,298.0 404.9,298.0 412.0,298.1 419.1,298.1 426.2,298.2 433.4,298.2 440.5,298.2 447.6,298.3 454.8,298.3 461.9,298.3 469.0,298.4 476.1,298.4 483.2,298.4 490.4,298.4 497.5,298.5 504.6,298.5 511.8,298.5 518.9,298.5 526.0,298.6 533.1,298.6 540.2,298.6 547.4,298.6 554.5,298.6 561.6,298.7 568.8,298.7 575.9,298.7 583.0,298.7 590.1,298.7 597.2,298.8 604.4,298.8 611.5,298.8 618.6,298.8 625.8,298.8 632.9,298.8 640.0,298.8" fill="none" stroke="#d0564a" stroke-width="2"/>
<text x="355.0" y="338" font-size="13" text-anchor="middle">connection interval (ms)</text><text x="20" y="165.0" font-size="13" text-anchor="middle" transform="rotate(-90 20 165.0)">응용 throughput (kbit/s)</text><line x1="90" y1="362" x2="115" y2="362" stroke="#4a7bd0" stroke-width="3"/><text x="122" y="366" font-size="12">2M · DLE · MTU 247 · 제한 없음</text><line x1="390" y1="362" x2="415" y2="362" stroke="#e08a3c" stroke-width="3"/><text x="422" y="366" font-size="12">1M · DLE · MTU 247 · 제한 없음</text><line x1="90" y1="382" x2="115" y2="382" stroke="#3f9a6b" stroke-width="3"/><text x="122" y="386" font-size="12">2M · DLE · MTU 247 · 6 패킷/event</text><line x1="390" y1="382" x2="415" y2="382" stroke="#d0564a" stroke-width="3"/><text x="422" y="386" font-size="12">1M · DLE 없음 · MTU 23 · 4 패킷/event</text>
</svg>
```

그림 4 — connection interval에 따른 응용 throughput (계산기 출력). 제한이 없으면(파랑, 주황) interval이 길수록 event 끝의 낭비가 줄어 상한에 가까워지고, 톱니 모양은 "몇 쌍이 들어가나"의 floor 효과다. event당 패킷 수가 제한되면(초록, 빨강) throughput ≈ 제한 × 244 B / interval이라 **interval이 짧을수록** 빠르다. 실제 기기는 대개 초록 · 빨강 쪽 세계에 산다.

### 2.6 MTU 고르기 — 정렬이 중요하다

2M · 30 ms에서 MTU와 DLE 조합을 바꿔 본다. 오른쪽 열은 event당 LL 패킷 6개 제한이다.

```python
# MTU와 DLE 조합이 throughput을 어떻게 바꾸나 — 2M PHY, 30 ms, 컨트롤러 제한 6 LL 패킷/event
from ble import throughput_kbps
print(" MTU  DLE   frag/SDU   제한없음 kbps   6pkt/event kbps")
for mtu in [23, 185, 247, 251, 498, 517]:
    for dle in [False, True]:
        a, _, nf = throughput_kbps("2M", 30, mtu, dle)
        b, _, _ = throughput_kbps("2M", 30, mtu, dle, max_pkts=6)
        print(f"{mtu:4d}   {'Y' if dle else 'N'}   {nf:6d}   {a:12.0f}   {b:14.0f}")
```

```text
 MTU  DLE   frag/SDU   제한없음 kbps   6pkt/event kbps
  23   N        1            320               32
  23   Y        1            320               32
 185   N        7            416               42
 185   Y        1           1262              291
 247   N       10            397               39
 247   Y        1           1366              390
 251   N       10            403               40
 251   Y        2           1091              198
 498   N       19            417               42
 498   Y        2           1386              396
 517   N       20            416               41
 517   Y        3           1229              273
```

출력에서 볼 것:

- **DLE가 없으면 MTU를 키워도 거의 소용없다** (397–417 kbit/s). ATT PDU가 27 B 조각 10–20개로 쪼개질 뿐이다.
- MTU 247과 498은 L2CAP PDU가 251의 배수(251, 502)라 조각이 꽉 찬다. **MTU 251은 247보다 느리다** — 255 B가 251 + 4 B로 쪼개져 4 B짜리 조각 하나가 쌍 하나(60 + 150 + 44 + 150 = 404 µs)를 통째로 쓴다. 6패킷 제한에서는 거의 절반(198 vs 390)이다.
- 규칙: **MTU − 3 + 7 = 251 × k** 가 되게 고른다 (k = 1이면 MTU 247, k = 2이면 498).

### 2.7 notification vs indication, write command vs write request

| 방향 | 확인 없음 (빠름) | 확인 있음 (느림) |
|---|---|---|
| peripheral → central | **notification** | indication (central이 confirmation을 보냄) |
| central → peripheral | **write without response** (write command) | write request (peripheral이 write response) |

확인 있는 방식은 ATT 계층에서 **한 번에 하나만 in-flight**다. 응답이 다음 쌍이나 다음 event에 와야 다음을 보낼 수 있어서, 대략 "event당 1개 이하"로 묶인다. 대량 전송은 notification(웨어러블 → 폰)과 write without response(폰 → 웨어러블)로 한다.

"확인이 없으면 데이터를 잃지 않나?" — 연결이 살아 있는 동안은 아니다. **BLE 링크 계층 자체가 ACK와 재전송을 한다**(LL 헤더의 SN/NESN 비트). LL은 CRC가 틀리면 다시 보내고 순서를 지킨다. 데이터를 잃는 것은 **연결이 끊길 때**(supervision timeout, 사용자가 멀어짐)이고, 그때 이미 컨트롤러 버퍼에 들어갔지만 공중에 못 나간 데이터가 사라진다. 그래서 응용 계층에는 "링크 재전송"이 아니라 **"재연결 후 어디서부터 다시"를 정하는 재개 프로토콜**이 필요하다(4절). 펌웨어 쪽에서는 notification 송신 큐가 꽉 찼다는 신호(스택마다 "buffer full" 에러나 "tx complete" 이벤트)로 흐름 제어를 한다.

### 2.8 폰(central)의 제약 — iOS/Android (hedge)

웨어러블이 peripheral, 폰이 central이면 **interval과 event 길이를 폰이 정한다.** 웨어러블은 L2CAP connection parameter update로 원하는 범위를 **요청**만 할 수 있다.

- **Android**: 앱이 `BluetoothGatt.requestMtu()`로 MTU를(흔히 517 요청), `requestConnectionPriority(CONNECTION_PRIORITY_HIGH)`로 짧은 interval을 요청할 수 있다. 실제로 받는 값과 event당 패킷 수는 제조사 · 칩 · OS 버전마다 다르다(확인 필요).
- **iOS**: 앱이 interval을 직접 정하는 API가 없다. MTU는 시스템이 협상하고, 앱은 `maximumWriteValueLength(for:)`로 결과를 읽는다. Apple의 Accessory Design Guidelines에 허용되는 connection parameter 범위가 적혀 있으니 액세서리 펌웨어는 그 범위 안에서 요청해야 한다(정확한 값은 문서 버전별로 확인 필요). write without response 흐름 제어는 `canSendWriteWithoutResponse`와 `peripheralIsReady(toSendWriteWithoutResponse:)` 콜백으로 한다.
- 공통: 폰이 동시에 BT 오디오(이어폰), Wi-Fi 다운로드를 하면 coex 때문에 웨어러블 링크의 event가 짧아진다(E8 8.2절). **throughput 사양은 "어떤 폰, 어떤 동시 부하에서"를 같이 적어야 한다.**

### 2.9 Don의 경험과 연결, 그리고 함정

Don은 RF 칩 통합에서 PHY rate, retry rate, coex 중재를 직접 본다. BLE throughput 디버깅은 그 감각을 응용 계층까지 끌어올리는 일이다. 실제로 측정할 때의 순서:

1. **sniffer(또는 HCI 로그)로 실제 협상된 값부터 확인한다**: PHY, interval, DLE(LL_LENGTH_REQ/RSP), MTU(ATT Exchange MTU). 기대한 값이 협상되지 않은 경우가 제일 흔하다.
2. **event당 패킷 수를 센다.** 계산기에 넣으면 이론값이 나오고, 측정과의 차이가 재전송 · coex 몫이다.
3. **암호화 MIC 4 B**를 잊지 않는다(계산기의 `mic=True`). 2M에서 쌍당 16 µs 늘어난다.
4. 응용 쪽 병목도 본다: 펌웨어가 notification을 충분히 빨리 큐에 넣는지, flash 읽기가 따라오는지(H1), 폰 앱이 콜백에서 무거운 일을 하지 않는지.

함정: "DLE를 켰다"고 생각했는데 상대가 27 B로 답해서 실제로는 꺼져 있다 · MTU 251을 "251 B 패킷에 맞췄다"고 착각한다 · 짧은 interval이 항상 빠르다고 믿는다 · 폰 하나에서 잰 숫자를 사양으로 쓴다.

---

## 3. Wi-Fi와 셀룰러 — 전력 상태, tail energy, 배치

### 3.1 직관 — 택시 기본요금

무선 업로드 에너지는 택시 요금과 같다. **기본요금**(깨어나기, 연결, 핸드셰이크, 그리고 내린 뒤에도 미터기가 한참 도는 tail)이 있고, **거리 요금**(바이트당 에너지)이 있다. 짧은 거리를 자주 타면 기본요금이 전부다. 그래서 **모아서 한 번에** 타야 한다 — 이것이 배치(batching)다.

### 3.2 Wi-Fi의 전력 상태

Wi-Fi station(STA)은 연결된 채로 대부분 잔다. 802.11 **power save mode(PSM)** 에서 AP는 자는 STA에게 온 프레임을 버퍼링하고, 주기적인 beacon의 TIM(Traffic Indication Map)에 "너한테 온 것 있음"을 표시한다. STA는 beacon(또는 DTIM beacon) 때만 깨어 확인한다. Wi-Fi 6(802.11ax)에는 **TWT(Target Wake Time)** 가 있어 AP와 "언제 깨어날지"를 협상할 수 있다.

업로드 한 번의 비용은 대략 이렇다:

- 연결이 끊겨 있었다면: scan + association + 인증(WPA2/WPA3 4-way handshake) + DHCP — 이것이 비싸다(수백 ms – 초 단위가 될 수 있다).
- 연결된 PSM 상태라면: 깨어나서 송신하고, 응답을 기다리고, 잠시 깨어 있다가(다시 올 트래픽 대비) 다시 잔다.

### 3.3 셀룰러 — RRC 상태와 tail

LTE 모뎀은 RRC(Radio Resource Control) 상태 기계를 가진다. 크게 **RRC_IDLE**(망에 등록만, 주기적으로 paging 확인)과 **RRC_CONNECTED**(데이터 채널 할당)이다. 보낼 것이 생기면 IDLE → CONNECTED로 **promotion**하는 데 시간과 에너지가 든다. 전송이 끝나도 망은 바로 IDLE로 보내지 않는다 — **inactivity timer**가 끝날 때까지 CONNECTED(그 안에서 DRX로 조금씩 줄어드는 전력)에 머문다. 이 구간이 **tail**이다. 5G NR에는 그 중간인 RRC_INACTIVE 상태가 추가됐다.

Huang et al.(MobiSys 2012)의 LTE 측정은 tail이 수 초 – 10초 이상이고 그동안 수백 mW – 1 W 자릿수 전력을 쓸 수 있음을 보였다. 다만 inactivity timer는 **망 운영자가 설정**하고 모뎀 · 칩 · 세대(LTE Cat-1, LTE-M, NB-IoT, 5G)마다 크게 달라서, 숫자는 "가정"으로만 쓴다. IoT용 LTE-M/NB-IoT에는 PSM과 eDRX처럼 잠을 길게 자는 기능이 있다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250" x2="640" y2="250" stroke="currentColor"/><line x1="70" y1="250" x2="70" y2="40" stroke="currentColor"/><line x1="70.0" y1="250" x2="70.0" y2="254" stroke="currentColor"/><text x="70.0" y="268" font-size="12" text-anchor="middle">0</text><line x1="141.2" y1="250" x2="141.2" y2="254" stroke="currentColor"/><text x="141.2" y="268" font-size="12" text-anchor="middle">1</text><line x1="212.5" y1="250" x2="212.5" y2="254" stroke="currentColor"/><text x="212.5" y="268" font-size="12" text-anchor="middle">2</text><line x1="283.8" y1="250" x2="283.8" y2="254" stroke="currentColor"/><text x="283.8" y="268" font-size="12" text-anchor="middle">3</text><line x1="355.0" y1="250" x2="355.0" y2="254" stroke="currentColor"/><text x="355.0" y="268" font-size="12" text-anchor="middle">4</text>
<line x1="426.2" y1="250" x2="426.2" y2="254" stroke="currentColor"/><text x="426.2" y="268" font-size="12" text-anchor="middle">5</text><line x1="497.5" y1="250" x2="497.5" y2="254" stroke="currentColor"/><text x="497.5" y="268" font-size="12" text-anchor="middle">6</text><line x1="568.8" y1="250" x2="568.8" y2="254" stroke="currentColor"/><text x="568.8" y="268" font-size="12" text-anchor="middle">7</text><line x1="640.0" y1="250" x2="640.0" y2="254" stroke="currentColor"/><text x="640.0" y="268" font-size="12" text-anchor="middle">8</text><line x1="66" y1="250.0" x2="70" y2="250.0" stroke="currentColor"/><text x="62" y="254.0" font-size="12" text-anchor="end">0.0</text><line x1="66" y1="190.0" x2="70" y2="190.0" stroke="currentColor"/><text x="62" y="194.0" font-size="12" text-anchor="end">0.4</text>
<line x1="66" y1="130.0" x2="70" y2="130.0" stroke="currentColor"/><text x="62" y="134.0" font-size="12" text-anchor="end">0.8</text><line x1="66" y1="70.0" x2="70" y2="70.0" stroke="currentColor"/><text x="62" y="74.0" font-size="12" text-anchor="end">1.2</text><rect x="70.0" y="248.5" width="35.6" height="1.5" fill="#888" fill-opacity="0.35" stroke="currentColor" stroke-width="0.6"/><rect x="105.6" y="100.0" width="21.4" height="150.0" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor" stroke-width="0.6"/><rect x="127.0" y="70.0" width="17.1" height="180.0" fill="#d0564a" fill-opacity="0.35" stroke="currentColor" stroke-width="0.6"/><rect x="144.1" y="70.0" width="11.4" height="180.0" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.6"/>
<rect x="155.5" y="160.0" width="356.2" height="90.0" fill="#888" fill-opacity="0.35" stroke="currentColor" stroke-width="0.6"/><rect x="511.8" y="248.5" width="128.2" height="1.5" fill="#888" fill-opacity="0.35" stroke="currentColor" stroke-width="0.6"/><text x="333.6" y="190.0" font-size="12" text-anchor="middle">tail: 5 s × 0.6 W = 3.0 J</text><text x="333.6" y="208.0" font-size="12" text-anchor="middle">(보낼 것이 없는데도 고전력 유지)</text><line x1="116.3" y1="96.0" x2="134.1" y2="52.0" stroke="currentColor"/><text x="137.7" y="50.0" font-size="12">promotion 0.3 J · 핸드셰이크 0.29 J · 100 KB 전송 0.19 J</text><line x1="149.8" y1="68.0" x2="162.6" y2="56.0" stroke="currentColor"/><text x="355.0" y="288" font-size="13" text-anchor="middle">시간 (s)</text><text x="22" y="145.0" font-size="13" text-anchor="middle" transform="rotate(-90 22 145.0)">모뎀 전력 (W, 가정)</text>
<text x="575.9" y="240.5" font-size="12" text-anchor="middle">idle</text><text x="355.0" y="315" font-size="12" text-anchor="middle">실제 값은 망 설정(inactivity timer, DRX)과 모뎀마다 다르다 — 모양만 기억</text>
</svg>
```

그림 5 — 셀룰러 업로드 한 번의 전력 모양 (모든 수치는 설명용 가정). 100 KB를 보내는 데는 0.19 J이지만, promotion · 핸드셰이크 · tail까지 합치면 약 3.8 J이다. **에너지의 80 %가 데이터를 보내지 않는 tail에서 나간다.**

### 3.4 코드로 확인 — 배치 크기와 바이트당 에너지

업로드 1회 = promotion + 핸드셰이크(RTT 3번) + 전송 + tail. 배치 크기를 바꾸며 1회 에너지와 바이트당 에너지를, 그리고 "하루 3 MB를 몇 분마다 올리나"에 따른 배터리 비율을 계산한다.

```python
# 배치 크기에 따른 업로드 1회 에너지와 바이트당 에너지 — 모든 무선 수치는 설명용 가정
radios = {  # promo(s, W), 핸드셰이크 RTT 수와 RTT(s), 전송 전력(W)·속도(B/s), tail(s, W)
    "cellular": dict(promo=(0.3, 1.0), rtts=3, rtt=0.08, p_tx=1.2, rate=5e6/8, tail=(5.0, 0.6)),
    "Wi-Fi":    dict(promo=(0.05, 0.5), rtts=3, rtt=0.03, p_tx=0.3, rate=20e6/8, tail=(0.2, 0.3)),
}
def upload_energy_J(r, nbytes):
    p = radios[r]
    e_fixed = p["promo"][0]*p["promo"][1] + p["rtts"]*p["rtt"]*p["p_tx"] + p["tail"][0]*p["tail"][1]
    return e_fixed, nbytes / p["rate"] * p["p_tx"]
print("업로드 1회 고정비:", {r: round(sum(upload_energy_J(r, 0)), 3) for r in radios}, "J")
print(f"{'batch':>8s} {'cell J':>8s} {'cell µJ/B':>10s} {'WiFi J':>8s} {'WiFi µJ/B':>10s}")
for nb in [1e3, 1e4, 1e5, 1e6, 1e7]:
    row = []
    for r in radios:
        f, v = upload_energy_J(r, nb); row += [f + v, (f + v) / nb * 1e6]
    print(f"{int(nb):8d} {row[0]:8.2f} {row[1]:10.2f} {row[2]:8.3f} {row[3]:10.3f}")
# 하루 3 MB(분당 ~2 KB 메트릭)를 몇 분마다 올리나 — 배터리 300 mAh·3.85 V ≈ 1.16 Wh ≈ 4.2 kJ 기준
daily, batt_J = 3e6, 300e-3 * 3.85 * 3600
for every_min in [1, 15, 60, 240]:
    n = 24 * 60 // every_min
    for r in radios:
        f, v = upload_energy_J(r, daily / n)
        e = n * (f + v)
        print(f"{every_min:4d}분마다 {r:8s}: 하루 {n:5d}회, {e:8.1f} J = 배터리의 {100*e/batt_J:5.2f} %")
```

```text
업로드 1회 고정비: {'cellular': 3.588, 'Wi-Fi': 0.112} J
   batch   cell J  cell µJ/B   WiFi J  WiFi µJ/B
    1000     3.59    3589.92    0.112    112.120
   10000     3.61     360.72    0.113     11.320
  100000     3.78      37.80    0.124      1.240
 1000000     5.51       5.51    0.232      0.232
10000000    22.79       2.28    1.312      0.131
   1분마다 cellular: 하루  1440회,   5172.5 J = 배터리의 124.40 %
   1분마다 Wi-Fi   : 하루  1440회,    161.6 J = 배터리의  3.89 %
  15분마다 cellular: 하루    96회,    350.2 J = 배터리의  8.42 %
  15분마다 Wi-Fi   : 하루    96회,     11.1 J = 배터리의  0.27 %
  60분마다 cellular: 하루    24회,     91.9 J = 배터리의  2.21 %
  60분마다 Wi-Fi   : 하루    24회,      3.0 J = 배터리의  0.07 %
 240분마다 cellular: 하루     6회,     27.3 J = 배터리의  0.66 %
 240분마다 Wi-Fi   : 하루     6회,      1.0 J = 배터리의  0.02 %
```

출력에서 볼 것:

- 셀룰러 1 KB 업로드는 바이트당 3590 µJ, 10 MB 배치는 2.3 µJ — **1500배 차이**다. 고정비 3.6 J이 작은 배치를 지배한다.
- **1분마다 셀룰러로 올리면 하루에 배터리의 124 %** — 즉 배터리가 하루를 못 간다. 1시간마다면 2.2 %, 4시간마다면 0.66 %. 같은 3 MB인데 업로드 주기만으로 190배 차이가 난다.
- Wi-Fi는 고정비가 0.11 J로 작아서 1분 주기도 3.9 %지만, 역시 배치가 유리하다.
- 결론: **"데이터가 생기면 바로 보낸다"는 설계는 셀룰러 웨어러블에서 불가능하다.** 지연 허용치(메트릭은 몇 시간 늦어도 된다)를 정하고 그만큼 모은다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<line x1="80" y1="290" x2="640" y2="290" stroke="currentColor"/><line x1="80" y1="290" x2="80" y2="30" stroke="currentColor"/><line x1="80.0" y1="290" x2="80.0" y2="294" stroke="currentColor"/><text x="80.0" y="308" font-size="12" text-anchor="middle">100 B</text><line x1="173.3" y1="290" x2="173.3" y2="294" stroke="currentColor"/><text x="173.3" y="308" font-size="12" text-anchor="middle">1 KB</text><line x1="266.7" y1="290" x2="266.7" y2="294" stroke="currentColor"/><text x="266.7" y="308" font-size="12" text-anchor="middle">10 KB</text><line x1="360.0" y1="290" x2="360.0" y2="294" stroke="currentColor"/><text x="360.0" y="308" font-size="12" text-anchor="middle">100 KB</text><line x1="453.3" y1="290" x2="453.3" y2="294" stroke="currentColor"/><text x="453.3" y="308" font-size="12" text-anchor="middle">1 MB</text>
<line x1="546.7" y1="290" x2="546.7" y2="294" stroke="currentColor"/><text x="546.7" y="308" font-size="12" text-anchor="middle">10 MB</text><line x1="640.0" y1="290" x2="640.0" y2="294" stroke="currentColor"/><text x="640.0" y="308" font-size="12" text-anchor="middle">100 MB</text><line x1="76" y1="290.0" x2="80" y2="290.0" stroke="currentColor"/><text x="72" y="294.0" font-size="12" text-anchor="end">0.1</text><line x1="76" y1="246.7" x2="80" y2="246.7" stroke="currentColor"/><text x="72" y="250.7" font-size="12" text-anchor="end">1</text><line x1="76" y1="203.3" x2="80" y2="203.3" stroke="currentColor"/><text x="72" y="207.3" font-size="12" text-anchor="end">10</text><line x1="76" y1="160.0" x2="80" y2="160.0" stroke="currentColor"/><text x="72" y="164.0" font-size="12" text-anchor="end">100</text>
<line x1="76" y1="116.7" x2="80" y2="116.7" stroke="currentColor"/><text x="72" y="120.7" font-size="12" text-anchor="end">1000</text><line x1="76" y1="73.3" x2="80" y2="73.3" stroke="currentColor"/><text x="72" y="77.3" font-size="12" text-anchor="end">10000</text><line x1="76" y1="30.0" x2="80" y2="30.0" stroke="currentColor"/><text x="72" y="34.0" font-size="12" text-anchor="end">100000</text>
<polyline points="80.0,49.3 89.3,53.6 98.7,58.0 108.0,62.3 117.3,66.6 126.7,71.0 136.0,75.3 145.3,79.6 154.7,84.0 164.0,88.3 173.3,92.6 182.7,96.9 192.0,101.3 201.3,105.6 210.7,109.9 220.0,114.3 229.3,118.6 238.7,122.9 248.0,127.2 257.3,131.5 266.7,135.9 276.0,140.2 285.3,144.5 294.7,148.8 304.0,153.0 313.3,157.3 322.7,161.6 332.0,165.8 341.3,170.0 350.7,174.2 360.0,178.3 369.3,182.4 378.7,186.4 388.0,190.4 397.3,194.2 406.7,198.0 416.0,201.7 425.3,205.2 434.7,208.5 444.0,211.6 453.3,214.6 462.7,217.3 472.0,219.7 481.3,222.0 490.7,223.9 500.0,225.7 509.3,227.1 518.7,228.4 528.0,229.5 537.3,230.4 546.7,231.2 556.0,231.8 565.3,232.3 574.7,232.7 584.0,233.0 593.3,233.3 602.7,233.5 612.0,233.7 621.3,233.8 630.7,234.0 640.0,234.0" fill="none" stroke="#d0564a" stroke-width="2.2"/><line x1="80" y1="234.4" x2="640" y2="234.4" stroke="#d0564a" stroke-dasharray="4 4"/>
<polyline points="80.0,114.5 89.3,118.9 98.7,123.2 108.0,127.5 117.3,131.9 126.7,136.2 136.0,140.5 145.3,144.9 154.7,149.2 164.0,153.5 173.3,157.8 182.7,162.2 192.0,166.5 201.3,170.8 210.7,175.1 220.0,179.5 229.3,183.8 238.7,188.1 248.0,192.4 257.3,196.7 266.7,201.0 276.0,205.3 285.3,209.6 294.7,213.8 304.0,218.0 313.3,222.2 322.7,226.4 332.0,230.5 341.3,234.6 350.7,238.7 360.0,242.6 369.3,246.5 378.7,250.2 388.0,253.9 397.3,257.4 406.7,260.7 416.0,263.8 425.3,266.8 434.7,269.5 444.0,271.9 453.3,274.2 462.7,276.1 472.0,277.9 481.3,279.3 490.7,280.6 500.0,281.7 509.3,282.6 518.7,283.4 528.0,284.0 537.3,284.5 546.7,284.9 556.0,285.2 565.3,285.5 574.7,285.7 584.0,285.9 593.3,286.0 602.7,286.1 612.0,286.2 621.3,286.3 630.7,286.3 640.0,286.4" fill="none" stroke="#4a7bd0" stroke-width="2.2"/><line x1="80" y1="286.6" x2="640" y2="286.6" stroke="#4a7bd0" stroke-dasharray="4 4"/>
<text x="640" y="228.4" font-size="12" text-anchor="end">cellular 바닥 ≈ 1.9 µJ/B (전송만)</text><text x="640" y="280.6" font-size="12" text-anchor="end">Wi-Fi 바닥 ≈ 0.12 µJ/B</text><text x="360.0" y="328" font-size="13" text-anchor="middle">업로드 1회 배치 크기 (로그)</text><text x="22" y="160.0" font-size="13" text-anchor="middle" transform="rotate(-90 22 160.0)">바이트당 에너지 (µJ/B, 로그)</text><line x1="100" y1="360" x2="125" y2="360" stroke="#d0564a" stroke-width="3"/><text x="132" y="364" font-size="12">cellular (고정비 ≈ 3.6 J, 가정)</text><line x1="380" y1="360" x2="405" y2="360" stroke="#4a7bd0" stroke-width="3"/><text x="412" y="364" font-size="12">Wi-Fi (고정비 ≈ 0.11 J, 가정)</text>
</svg>
```

그림 6 — 배치 크기에 따른 바이트당 에너지 (로그-로그, 가정 수치). 작은 배치에서는 기울기 −1(고정비 ÷ 바이트), 큰 배치에서는 바닥(전송 자체의 바이트당 에너지)에 붙는다. 꺾이는 지점 ≈ 고정비 ÷ 바이트당 에너지 = 셀룰러 약 1.9 MB, Wi-Fi 약 0.9 MB. 그보다 작게 올리면 고정비를 내는 셈이다.

배치의 대가도 있다:

- **지연**: 1시간 배치면 데이터가 최대 1시간 늦게 도착한다. crash dump처럼 급한 것은 배치에서 뺀다(7절 우선순위).
- **저장 공간**: 모으는 동안 flash에 있어야 한다(H1의 용량 예산).
- **손실 위험**: 기기를 잃어버리면 안 올린 데이터도 잃는다.

또 하나의 기법은 **piggyback**이다: 다른 이유로 이미 radio가 깨어 있을 때(사용자의 음성 질의로 셀룰러가 CONNECTED 상태일 때) tail 구간에 대기열을 같이 올리면 고정비가 거의 0이다. 업로드 스케줄러가 "radio 상태" 이벤트를 구독해야 하는 이유다.

### 3.5 TCP, TLS, QUIC — 핸드셰이크도 고정비다

셀룰러 RTT는 수십 – 수백 ms다. 연결마다 왕복이 몇 번 필요한지가 곧 "활성 상태로 기다리는 시간"이다.

- **TCP**: 3-way handshake로 1 RTT.
- **TLS 1.2**: full handshake 2 RTT (session ticket · session ID로 재개하면 1 RTT).
- **TLS 1.3** (RFC 8446): full handshake 1 RTT. **session resumption(PSK)** 은 RTT 수는 같지만 인증서 체인 전송 · 검증과 서명 연산을 생략해 바이트와 CPU를 아낀다. **0-RTT(early data)** 는 첫 비행에 요청을 실어 1 RTT를 더 아끼지만, 공격자가 그 데이터를 **재전송(replay)** 할 수 있어서 멱등한 요청에만 써야 한다.
- **QUIC** (RFC 9000, HTTP/3은 RFC 9114): UDP 위에서 전송 + TLS 1.3 핸드셰이크를 합쳐 1 RTT, 재개 시 0-RTT. 연결 ID 덕분에 IP가 바뀌어도(Wi-Fi ↔ 셀룰러) 연결을 이어갈 수 있다(connection migration). 대신 UDP를 막는 망이 있고, 기기 쪽 구현이 무겁다.

```python
# 첫 요청의 응답을 받기까지 RTT 수 — 연결 방식별 (DNS는 캐시됐다고 가정)
setups = {  # 이름: (요청 전 RTT 수, 비고)
    "TCP + TLS 1.2 (full)":        (3, "TCP 1 + TLS 2"),
    "TCP + TLS 1.3 (full)":        (2, "TCP 1 + TLS 1"),
    "TCP + TLS 1.3 resumption":    (2, "RTT는 같고 인증서 검증·서명 생략"),
    "TCP + TLS 1.3 0-RTT":         (1, "early data는 replay 가능"),
    "QUIC (HTTP/3) 1-RTT":         (1, "전송+암호 핸드셰이크 통합"),
    "QUIC 0-RTT":                  (0, "early data는 replay 가능"),
    "keep-alive 연결 재사용":       (0, "이미 열린 연결"),
}
P_radio = 1.0                                    # W, 셀룰러 활성 전력 (가정)
print(f"{'방식':28s} RTT수  RTT 80ms   RTT 300ms  에너지@300ms")
for name, (n, note) in setups.items():
    t80, t300 = (n + 1) * 80, (n + 1) * 300          # +1 = 요청/응답 자체
    print(f"{name:28s} {n:3d} {t80:7d} ms {t300:8d} ms {P_radio * t300:8.0f} mJ   {note}")
```

```text
방식                           RTT수  RTT 80ms   RTT 300ms  에너지@300ms
TCP + TLS 1.2 (full)           3     320 ms     1200 ms     1200 mJ   TCP 1 + TLS 2
TCP + TLS 1.3 (full)           2     240 ms      900 ms      900 mJ   TCP 1 + TLS 1
TCP + TLS 1.3 resumption       2     240 ms      900 ms      900 mJ   RTT는 같고 인증서 검증·서명 생략
TCP + TLS 1.3 0-RTT            1     160 ms      600 ms      600 mJ   early data는 replay 가능
QUIC (HTTP/3) 1-RTT            1     160 ms      600 ms      600 mJ   전송+암호 핸드셰이크 통합
QUIC 0-RTT                     0      80 ms      300 ms      300 mJ   early data는 replay 가능
keep-alive 연결 재사용              0      80 ms      300 ms      300 mJ   이미 열린 연결
```

출력에서 볼 것: RTT 300 ms 셀룰러에서 TCP + TLS 1.2 full은 첫 응답까지 1.2 s, 이미 열린 연결 재사용은 0.3 s다. 1 W 모뎀이면 0.9 J 차이 — 그림 5의 tail과 비슷한 크기다. **배치하면 핸드셰이크도 한 번만 낸다.** 한 업로드 세션 안에서 chunk 여러 개를 HTTP keep-alive(또는 HTTP/2 · HTTP/3 multiplexing)로 같은 연결에 실어 보내는 것이 기본이다.

MCU급 기기에서의 추가 고려 (임베디드 관점):

- TLS 핸드셰이크의 비대칭 연산(ECDHE, ECDSA 검증)은 Cortex-M에서 수십 – 수백 ms가 걸릴 수 있다(코어 · 클럭 · 라이브러리 · 하드웨어 가속기에 따라 다름). session resumption은 이것을 줄인다.
- TLS 라이브러리(mbedTLS 등)의 RAM: 레코드 버퍼만 수 KB – 16 KB 이상. TLS 레코드 최대 크기 협상(max_fragment_length 확장, RFC 6066)으로 줄일 수 있지만 서버가 지원해야 한다.
- **시계**: 인증서 유효기간 검사에는 현재 시각이 필요하다. 배터리가 완전히 방전된 뒤 RTC가 1970년이면 TLS가 실패한다 — 펌웨어가 "마지막으로 알던 시각"을 flash에 저장해 두는 이유다.

### 3.6 흔한 함정

- 메트릭 하나 생길 때마다 셀룰러 연결 → 배터리가 하루를 못 간다(3.4절).
- 업로드 직후 바로 다음 업로드를 예약해서 tail을 두 번 낸다 — tail 안에 몰아 넣어라.
- 연결마다 새 TLS 세션 → 핸드셰이크 비용 반복. 세션 티켓을 보관하라.
- 0-RTT로 "업로드 확정" 같은 비멱등 요청을 보낸다 → replay 위험.
- 실험실 Wi-Fi(RTT 수 ms)에서만 테스트해서 셀룰러 RTT · 손실에서 timeout이 너무 짧게 잡힌다.


---

## 4. 업로드 프로토콜 설계 — 끊겨도, 중복돼도, 깨져도

### 4.1 요구 사항부터

무선 업로드에서 일어나는 일을 먼저 나열하자. 설계는 이 목록에 하나씩 답하는 것이다.

| 일어나는 일 | 결과 (대책이 없으면) | 대책 |
|---|---|---|
| 전송 중 연결 끊김 | 처음부터 다시 → 에너지 낭비, 큰 파일은 영원히 못 올림 | chunk + **재개(resume)**: 서버가 받은 offset을 알려 줌 |
| 서버는 받았는데 응답(ACK)이 유실 | 기기가 다시 보냄 → 서버에 **중복** | **멱등성**: 같은 요청을 여러 번 해도 결과가 한 번과 같게 (offset 검사, idempotency key) |
| 비트 오류, 버그, 잘린 파일 | 조용히 망가진 데이터가 학습셋에 들어감 | chunk별 **CRC** + 파일 전체 **SHA-256**, 서버가 검증 |
| 기기 재부팅 | 업로드 상태(RAM) 소실 | 업로드 id · 진행 상태를 flash에 기록, 재부팅 후 서버에 질의 |
| 서버 장애 후 복구 | 수천 대가 동시에 재시도 → 서버가 다시 죽음 | **exponential backoff + jitter**, 서버의 `Retry-After` 존중 |
| 업로드 성공 전 삭제 | 데이터 영구 손실 | **ACK 후 삭제** 정책 |
| 같은 파일을 두 경로(폰 relay와 Wi-Fi)로 올림 | 중복 | 파일의 content hash로 dedup |

### 4.2 chunk와 content hash

파일을 고정 크기 **chunk**로 나눈다. chunk 크기는 trade-off다.

- 작으면: 끊겼을 때 다시 보낼 양이 적다, 기기 RAM 버퍼가 작다. 대신 요청마다 헤더 · 왕복 비용.
- 크면: 요청 오버헤드가 적다. 대신 끊기면 많이 잃고, 버퍼가 크다.

웨어러블에서는 RAM과 링크 상태에 맞춰 수 KB – 수백 KB가 흔하다(BLE relay라면 더 작게). 참고로 Amazon S3 multipart upload는 마지막을 뺀 part가 최소 5 MiB이고 part 수 상한이 10,000이다 — 서버 · 데스크톱용 설계라서 웨어러블 chunk와는 크기 감이 다르다. 기기 chunk를 폰이나 게이트웨이가 모아 큰 part로 올리는 구조도 가능하다.

각 파일에는 **SHA-256** 같은 암호학적 해시를 붙인다. 이 해시는 세 가지로 쓰인다: (1) 서버가 다 받은 뒤 무결성 검증, (2) **content-addressed 이름**(같은 내용이면 같은 이름 → 두 경로로 올라와도 자동 dedup), (3) 업로드 세션의 idempotency key 재료.

### 4.3 재개 — 서버가 "어디까지 받았나"를 알려 준다

재개의 핵심은 단순하다: **진실은 서버의 offset**이다. 기기는 "내가 몇 바이트 보냈나"가 아니라 "서버가 몇 바이트를 확정했나"를 기준으로 다음을 보낸다. 끊기면 물어보고(HEAD), 답한 지점부터 이어 보낸다.

실제로 쓰이는 방식들 (이름과 세부는 각 문서에서 확인):

| 방식 | 재개 지점 확인 | chunk 전송 | 끝내기 |
|---|---|---|---|
| tus 프로토콜 (tus.io, 오픈 프로토콜) | `HEAD` → `Upload-Offset` 헤더 | `PATCH` + `Upload-Offset` (불일치면 409 Conflict) | offset == `Upload-Length`면 완료 |
| S3 multipart upload | `ListParts` | `UploadPart` (part 번호, 순서 무관 · 병렬 가능) | `CompleteMultipartUpload` (part 목록 + ETag) |
| Google Cloud Storage resumable upload | 빈 `PUT`으로 상태 질의 → `Range` 헤더 | `PUT` + `Content-Range` | 마지막 범위 도착 |
| 이 노트의 데모 (tus를 단순화) | `HEAD` → `Upload-Offset` | `PATCH` + `Upload-Offset` + `X-Chunk-Crc32` | 서버가 SHA-256 검증 후 `X-Complete: ok` |

두 계열이 있다: tus · GCS처럼 **순차 offset**(단순, 기기에 맞다)과 S3처럼 **번호 붙은 part**(병렬 가능, 순서 무관). 기기는 링크 하나라서 순차 offset이 보통 충분하다.

### 4.4 멱등성 — 같은 요청을 두 번 해도 안전하게

**멱등(idempotent)** 은 "한 번 하든 여러 번 하든 결과가 같다"는 뜻이다. 네트워크에서 응답이 사라지면 클라이언트는 "처리됐는지"를 알 수 없으므로 **다시 보낼 수밖에 없다.** 그래서 모든 요청은 재전송돼도 안전해야 한다.

- **chunk 전송**: 요청에 `Upload-Offset`을 같이 보낸다. 서버는 "지금 내 offset과 같을 때만" 붙인다. 이미 붙인 chunk가 다시 오면 offset이 달라서 **409 + 현재 offset**을 돌려준다. 클라이언트는 그 offset으로 점프한다. 중복이 원천적으로 안 생긴다.
- **업로드 생성**: "새 업로드 시작" 요청이 두 번 가면 업로드가 두 개 생긴다. 그래서 `Idempotency-Key` 헤더(예: 기기 ID + 파일 SHA-256)를 붙이고, 서버는 같은 key면 **같은 업로드 id를 돌려준다.** 이 헤더 이름은 Stripe 같은 결제 API에서 널리 쓰이고 IETF에서 표준 초안으로 다뤄지고 있다(상태 확인 필요). 이름보다 개념이 중요하다.
- **기록(ingestion)**: 서버 쪽 H3 파이프라인도 같은 파일이 두 번 들어오면 content hash로 한 번만 처리해야 한다(at-least-once 전송 + 멱등 처리 = 사실상 exactly-once).

펌웨어 비유: NVMe 명령을 재시도할 때 같은 LBA에 같은 데이터를 쓰는 것은 멱등이지만 "append" 같은 명령은 그렇지 않다. 업로드의 "chunk append"를 **offset 지정 쓰기**로 바꿔 멱등하게 만드는 것이 4.3 – 4.4절의 요점이다.

### 4.5 ACK 후 삭제 — 기기 쪽 상태 기계

기기의 파일 하나는 이런 상태를 거친다. 상태는 **flash에 기록**해서 재부팅에도 살아남게 한다(H1의 메타데이터 영역).

```
 CLOSED ──(파일 닫힘, SHA-256 계산)──► QUEUED
 QUEUED ──(스케줄러가 선택, POST + Idempotency-Key)──► UPLOADING(id, 서버 offset)
 UPLOADING ──(끊김/재부팅)──► UPLOADING   ← HEAD로 offset 다시 받기
 UPLOADING ──(마지막 chunk, 서버가 SHA-256 OK)──► ACKED
 ACKED ──(flash 공간 회수)──► DELETED
 UPLOADING ──(서버가 SHA 불일치 응답)──► QUEUED (처음부터, 원인 로그)
 QUEUED/UPLOADING ──(저장 공간 압박 + 낮은 우선순위)──► DROPPED (카운터 증가, 메트릭으로 보고)
```

핵심 규칙 세 가지:

1. **ACK는 "서버가 durable하게 저장했다"는 뜻이어야 한다.** 서버가 메모리에만 받고 ACK를 주면 서버 장애 때 사라진다. SSD에서 "write cache에만 있는데 완료 응답 → 전원 손실"과 같은 버그다. 서버는 object storage에 커밋한 뒤 ACK한다(H3).
2. **삭제는 ACK 뒤에만.** 그 전에는 flash가 부족해도 지우지 않는다 — 버려야 한다면 "DROPPED"로 **세어서** 보고한다(H7 데이터 품질 지표). 조용히 사라지는 데이터가 제일 나쁘다.
3. **폰 relay의 ACK는 폰의 ACK가 아니라 서버의 ACK여야 한다.** 폰이 받았다고 지우면, 폰 앱이 서버에 올리기 전에 죽을 때 잃는다. 폰이 서버 ACK를 기기에 전달하거나, 폰이 durable 저장소에 쓴 뒤의 ACK만 인정한다.

### 4.6 무결성 — chunk CRC + 파일 SHA-256 (end-to-end)

"BLE도 CRC가 있고 TCP도 체크섬이 있고 TLS는 MAC이 있는데 왜 또?" — 그 검사들은 **각 구간(hop)** 만 지킨다. 데이터는 flash → RAM → BLE → 폰 앱 메모리 → HTTP → 서버 디스크 → object storage를 거치고, **버그는 구간 사이(버퍼 복사, 파일 시스템, 앱 코드)** 에서 생긴다. Saltzer · Reed · Clark의 "end-to-end argument"(1984)가 바로 이 이야기다: 정말 필요한 검사는 끝과 끝이 해야 한다.

| 검사 | 범위 | 비용 | 무엇을 잡나 |
|---|---|---|---|
| chunk CRC-32 (H1 로그에도 있음) | chunk 하나 | MCU에서 싸다 (테이블 · 하드웨어 CRC 유닛) | 전송 · 복사 오류, 잘림 — 어느 chunk인지 바로 앎 |
| 파일 SHA-256 | 파일 전체, 기기 → object storage | 더 비싸지만 파일당 한 번 | 순서 바뀜, 빠진 chunk, 서버 쪽 조립 버그, 고의 변조 탐지의 기초 |
| TLS 레코드 MAC (AEAD) | 기기(또는 폰) ↔ 서버 연결 구간 | TLS가 이미 함 | 전송 중 변조 |

CRC는 **우연한 오류** 검출용이고, 고의 변조는 막지 못한다(누구나 CRC를 다시 계산할 수 있다). 변조 방지는 TLS와 서명(6절)이 한다.

C로 기기 쪽 chunk 헤더와 CRC-32를 만든다. CRC-32는 zlib · Ethernet과 같은 반사형 다항식 `0xEDB88320`이고, check 값은 `"123456789"` → `0xCBF43926`이다. chunk별 CRC와 함께 **파일 전체 CRC를 chunk를 이어가며 갱신**할 수 있다는 것도 보인다(CRC는 스트리밍 계산이 된다).

```c
/* 업로드 chunk 헤더 + CRC-32 (zlib/IEEE 802.3과 같은 다항식) — 기기 쪽 코드 스케치 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;                                       /* init 0xFFFFFFFF */
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;                                      /* final XOR 0xFFFFFFFF */
}
typedef struct {            /* 와이어 포맷: little-endian으로 직접 직렬화 (struct padding 무시) */
    uint32_t magic, file_id, seq, offset; uint16_t len; uint32_t crc;
} chunk_hdr;
static size_t put_hdr(uint8_t *w, const chunk_hdr *h) {
    uint32_t v[4] = {h->magic, h->file_id, h->seq, h->offset}; size_t n = 0;
    for (int i = 0; i < 4; i++) for (int b = 0; b < 4; b++) w[n++] = (uint8_t)(v[i] >> (8 * b));
    w[n++] = (uint8_t)h->len; w[n++] = (uint8_t)(h->len >> 8);
    for (int b = 0; b < 4; b++) w[n++] = (uint8_t)(h->crc >> (8 * b));
    return n;                                         /* 22 바이트 */
}
int main(void) {
    printf("crc32(\"123456789\") = 0x%08X (기대 0xCBF43926)\n",
           crc32_update(0, (const uint8_t *)"123456789", 9));
    uint8_t file[1000]; for (int i = 0; i < 1000; i++) file[i] = (uint8_t)((i * 2654435761u) >> 13);
    uint32_t whole = 0; uint8_t wire[64];
    for (uint32_t seq = 0, off = 0; off < sizeof file; seq++, off += 256) {
        uint16_t len = (uint16_t)((sizeof file - off) < 256 ? sizeof file - off : 256);
        chunk_hdr h = {0x48324331u, 42, seq, off, len, crc32_update(0, file + off, len)};
        size_t n = put_hdr(wire, &h);
        whole = crc32_update(whole, file + off, len);   /* 이어서 계산하면 파일 전체 CRC */
        printf("seq=%u off=%4u len=%3u crc=0x%08X hdr=%zu B\n", seq, off, len, h.crc, n);
    }
    printf("파일 전체 crc32 (chunk 이어붙여 계산) = 0x%08X\n", whole);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 chunk.c -o chunk && ./chunk
python3 -c "import zlib; f=bytes(((i*2654435761)&0xffffffff)>>13 & 255 for i in range(1000)); print(hex(zlib.crc32(f)), [hex(zlib.crc32(f[o:o+256])) for o in range(0,1000,256)])"
```

```text
crc32("123456789") = 0xCBF43926 (기대 0xCBF43926)
seq=0 off=   0 len=256 crc=0xCDADAE3B hdr=22 B
seq=1 off= 256 len=256 crc=0x3A5A41D7 hdr=22 B
seq=2 off= 512 len=256 crc=0xBD1CA117 hdr=22 B
seq=3 off= 768 len=232 crc=0xBA4912FE hdr=22 B
파일 전체 crc32 (chunk 이어붙여 계산) = 0x5B8F28CE
```

```text
0x5b8f28ce ['0xcdadae3b', '0x3a5a41d7', '0xbd1ca117', '0xba4912fe']
```

출력에서 볼 것: C의 chunk CRC 네 개와 파일 전체 CRC가 Python `zlib.crc32`와 정확히 같다. 즉 기기와 서버가 **같은 함수**를 쓰고 있다는 것을 check 값과 실제 데이터로 확인했다 — CRC 버그의 대부분은 다항식 · 초기값 · 반사 · 최종 XOR 중 하나가 어긋난 것이다. 헤더를 struct 그대로 `memcpy`하지 않고 바이트 단위로 직렬화한 것도 중요하다(padding · endianness가 컴파일러마다 다르다).

### 4.7 순서와 중복 — sequence number

chunk에 offset이 있으면 순서는 offset이 정한다. 파일 단위로는 **파일마다 단조 증가하는 sequence number**(부팅 횟수 + 파일 번호 같은)를 붙여 두면 서버가 "기기 42의 파일 1033이 빠졌다"를 알 수 있다. 업로드는 우선순위 때문에 순서가 바뀌어 도착하므로(crash dump가 먼저), 서버는 **도착 순서가 아니라 기기 timestamp와 sequence로 정렬**한다. 빠진 번호는 H7의 "데이터 누락" 지표가 된다 — 기기가 DROPPED로 보고한 것인지, 아직 안 온 것인지 구분할 수 있게 기기가 DROPPED 목록도 메트릭으로 보낸다.

### 4.8 동작하는 데모 — 재개 가능한 chunk 업로드 (로컬 loopback)

표준 라이브러리만으로 서버와 클라이언트를 만든다. 서버는 `127.0.0.1:18765`에서만 듣는 스레드 HTTP 서버다(외부 노출 없음). 클라이언트는 100,000 B 파일을 16 KB chunk로 올리면서 일부러 세 가지 사고를 낸다: (1) 서버가 chunk를 받았는데 **응답이 사라짐**, (2) chunk에 **비트 오류**, (3) **기기 재부팅**(클라이언트가 진행 상태를 잃음).

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
<text x="130" y="24" font-size="13" text-anchor="middle">기기 (client)</text><text x="550" y="24" font-size="13" text-anchor="middle">서버</text><line x1="130" y1="34" x2="130" y2="460" stroke="currentColor"/><line x1="550" y1="34" x2="550" y2="460" stroke="currentColor"/><line x1="130" y1="55" x2="550" y2="55" stroke="#4a7bd0" stroke-width="1.8"/><polygon points="550,55 542,51 542,59" fill="#4a7bd0"/><text x="340.0" y="50" font-size="12" text-anchor="middle">POST /uploads  Idempotency-Key, Upload-Length, X-Sha256</text><line x1="550" y1="80" x2="130" y2="80" stroke="#4a7bd0" stroke-width="1.8" stroke-dasharray="5 3"/><polygon points="130,80 138,76 138,84" fill="#4a7bd0"/><text x="340.0" y="75" font-size="12" text-anchor="middle">201 Location: id</text><line x1="130" y1="110" x2="550" y2="110" stroke="#3f9a6b" stroke-width="1.8"/><polygon points="550,110 542,106 542,114" fill="#3f9a6b"/>
<text x="340.0" y="105" font-size="12" text-anchor="middle">PATCH off=0  [16 KB]  X-Chunk-Crc32</text><line x1="550" y1="135" x2="130" y2="135" stroke="#3f9a6b" stroke-width="1.8" stroke-dasharray="5 3"/><polygon points="130,135 138,131 138,139" fill="#3f9a6b"/><text x="340.0" y="130" font-size="12" text-anchor="middle">204 Upload-Offset: 16384</text><line x1="130" y1="165" x2="550" y2="165" stroke="#3f9a6b" stroke-width="1.8"/><polygon points="550,165 542,161 542,169" fill="#3f9a6b"/><text x="340.0" y="160" font-size="12" text-anchor="middle">PATCH off=16384  [16 KB]</text><line x1="550" y1="190" x2="340.0" y2="190" stroke="#d0564a" stroke-width="1.8" stroke-dasharray="5 3"/><line x1="334.0" y1="184" x2="346.0" y2="196" stroke="#d0564a" stroke-width="2"/><line x1="334.0" y1="196" x2="346.0" y2="184" stroke="#d0564a" stroke-width="2"/>
<text x="325" y="194" font-size="12" text-anchor="end">204 (32768) — 응답 유실</text><line x1="130" y1="220" x2="550" y2="220" stroke="#e08a3c" stroke-width="1.8"/><polygon points="550,220 542,216 542,224" fill="#e08a3c"/><text x="340.0" y="215" font-size="12" text-anchor="middle">PATCH off=16384 (재전송)</text><line x1="550" y1="245" x2="130" y2="245" stroke="#e08a3c" stroke-width="1.8" stroke-dasharray="5 3"/><polygon points="130,245 138,241 138,249" fill="#e08a3c"/><text x="340.0" y="240" font-size="12" text-anchor="middle">409 Upload-Offset: 32768 → 그 offset으로 점프</text><line x1="130" y1="275" x2="550" y2="275" stroke="#3f9a6b" stroke-width="1.8"/><polygon points="550,275 542,271 542,279" fill="#3f9a6b"/><text x="340.0" y="270" font-size="12" text-anchor="middle">PATCH off=32768 …</text><line x1="130" y1="330" x2="550" y2="330" stroke="#4a7bd0" stroke-width="1.8"/>
<polygon points="550,330 542,326 542,334" fill="#4a7bd0"/><text x="340.0" y="325" font-size="12" text-anchor="middle">(재부팅 후) HEAD /uploads/id</text><line x1="550" y1="355" x2="130" y2="355" stroke="#4a7bd0" stroke-width="1.8" stroke-dasharray="5 3"/><polygon points="130,355 138,351 138,359" fill="#4a7bd0"/><text x="340.0" y="350" font-size="12" text-anchor="middle">200 Upload-Offset: 65536</text><line x1="130" y1="385" x2="550" y2="385" stroke="#3f9a6b" stroke-width="1.8"/><polygon points="550,385 542,381 542,389" fill="#3f9a6b"/><text x="340.0" y="380" font-size="12" text-anchor="middle">PATCH off=65536 … 마지막 chunk</text><line x1="550" y1="410" x2="130" y2="410" stroke="#3f9a6b" stroke-width="1.8" stroke-dasharray="5 3"/><polygon points="130,410 138,406 138,414" fill="#3f9a6b"/><text x="340.0" y="405" font-size="12" text-anchor="middle">204 X-Complete: ok (서버가 SHA-256 검증)</text>
<text x="140" y="440" font-size="12">ACK 확인 후에만 기기에서 파일 삭제</text><line x1="70" y1="302" x2="610" y2="302" stroke="#d0564a" stroke-dasharray="2 3"/><text x="340.0" y="318" font-size="12" text-anchor="middle">기기 재부팅 — RAM 상태 소실, 업로드 id는 flash에 저장해 둠</text>
</svg>
```

그림 7 — 데모의 메시지 흐름. 응답이 사라지면 같은 offset으로 재전송하고, 서버는 409와 함께 진짜 offset을 알려 준다. 재부팅 뒤에는 HEAD로 offset을 묻고 이어 보낸다. 서버가 SHA-256까지 확인한 ACK를 받은 뒤에만 기기가 파일을 지운다.

서버 (`up_server.py`, 36줄):

```python
# 재개 가능한 chunk 업로드 서버 — 127.0.0.1:18765, 표준 라이브러리만 (tus 스타일 헤더를 단순화)
import hashlib, threading, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
UPLOADS, KEYS, LOCK = {}, {}, threading.Lock()      # id -> 상태, Idempotency-Key -> id
class H(BaseHTTPRequestHandler):
    def reply(self, code, **hdr):
        self.send_response(code)
        for k, v in hdr.items(): self.send_header(k.replace("_", "-"), str(v))
        self.send_header("Content-Length", "0"); self.end_headers()
    def do_POST(self):                               # 업로드 생성 (같은 key면 같은 id)
        key = self.headers["Idempotency-Key"]
        with LOCK:
            if key in KEYS: return self.reply(200, Location=KEYS[key])
            uid = hashlib.sha1(key.encode()).hexdigest()[:8]; KEYS[key] = uid
            UPLOADS[uid] = dict(size=int(self.headers["Upload-Length"]),
                                sha=self.headers["X-Sha256"], data=bytearray())
        self.reply(201, Location=uid)
    def do_HEAD(self):                               # "어디까지 받았나?"
        self.reply(200, Upload_Offset=len(UPLOADS[self.path.split("/")[-1]]["data"]))
    def do_PATCH(self):                              # chunk 추가: offset이 맞고 CRC가 맞을 때만
        u = UPLOADS[self.path.split("/")[-1]]
        body = self.rfile.read(int(self.headers["Content-Length"]))
        with LOCK:
            if int(self.headers["Upload-Offset"]) != len(u["data"]):
                return self.reply(409, Upload_Offset=len(u["data"]))
            if zlib.crc32(body) != int(self.headers["X-Chunk-Crc32"]):
                return self.reply(400, Upload_Offset=len(u["data"]))
            u["data"] += body
            done = len(u["data"]) == u["size"]
            ok = done and hashlib.sha256(u["data"]).hexdigest() == u["sha"]
        self.reply(204, Upload_Offset=len(u["data"]), X_Complete=("ok" if ok else "BAD") if done else "no")
    def log_message(self, *a): pass                  # 접근 로그 끄기
def start(port=18765):
    srv = ThreadingHTTPServer(("127.0.0.1", port), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv
```

클라이언트 (`up_client.py`, 33줄 — 같은 프로세스에서 서버 스레드를 띄운다):

```python
# 클라이언트: 16 KB chunk 업로드, 응답 유실·비트 오류·재부팅을 일부러 일으키고 재개한다
import hashlib, http.client, zlib, random
import up_server
srv = up_server.start()                              # 서버: 127.0.0.1:18765 (같은 프로세스의 스레드)
random.seed(0); data = bytes(random.getrandbits(8) for _ in range(100_000))
sha, CH = hashlib.sha256(data).hexdigest(), 16_384
def req(method, path, body=b"", **hdr):
    c = http.client.HTTPConnection("127.0.0.1", 18765, timeout=5)
    c.request(method, path, body=body, headers={k.replace("_", "-"): str(v) for k, v in hdr.items()})
    r = c.getresponse(); r.read(); c.close(); return r
def create():
    key = "dev42:" + sha[:16]                         # 같은 파일 → 같은 key → 중복 생성 안 됨
    r = req("POST", "/uploads", Upload_Length=len(data), X_Sha256=sha, Idempotency_Key=key)
    return r.status, r.getheader("Location")
print("create #1:", create()); st, uid = create(); print("create #2 (재시도):", (st, uid))
def upload(uid, off, faults):
    hit = lambda k, at: faults.get(k) == at and faults.pop(k) is not None   # 한 번만 발생
    while off < len(data):
        chunk = data[off:off + CH]; crc = zlib.crc32(chunk)
        if hit("flip", off): chunk = bytes([chunk[0] ^ 1]) + chunk[1:]   # 비트 오류
        r = req("PATCH", f"/uploads/{uid}", chunk, Upload_Offset=off, X_Chunk_Crc32=crc)
        new = int(r.getheader("Upload-Offset"))
        if hit("drop", off):           # 서버는 받았지만 응답이 사라짐
            print(f"  off={off:6d}: 응답 유실 → 같은 offset으로 재전송"); continue
        print(f"  off={off:6d}: HTTP {r.status} → 서버 offset {new}  complete={r.getheader('X-Complete')}")
        if hit("reboot", new): return new   # 기기 재부팅: 메모리 상태 소실
        off = new
    return off
print("세션 1"); upload(uid, 0, {"drop": 16_384, "flip": 49_152, "reboot": 65_536})
r = req("HEAD", f"/uploads/{uid}"); off = int(r.getheader("Upload-Offset"))
print(f"재부팅 후 HEAD → 서버 offset {off}에서 재개"); print("세션 2"); upload(uid, off, {})
print("서버 sha256 == 로컬:", hashlib.sha256(up_server.UPLOADS[uid]["data"]).hexdigest() == sha)
srv.shutdown()
```

```sh
.venv/bin/python up_client.py      # 같은 폴더에 up_server.py
```

```text
create #1: (201, 'd7fe5ff5')
create #2 (재시도): (200, 'd7fe5ff5')
세션 1
  off=     0: HTTP 204 → 서버 offset 16384  complete=no
  off= 16384: 응답 유실 → 같은 offset으로 재전송
  off= 16384: HTTP 409 → 서버 offset 32768  complete=None
  off= 32768: HTTP 204 → 서버 offset 49152  complete=no
  off= 49152: HTTP 400 → 서버 offset 49152  complete=None
  off= 49152: HTTP 204 → 서버 offset 65536  complete=no
재부팅 후 HEAD → 서버 offset 65536에서 재개
세션 2
  off= 65536: HTTP 204 → 서버 offset 81920  complete=no
  off= 81920: HTTP 204 → 서버 offset 98304  complete=no
  off= 98304: HTTP 204 → 서버 offset 100000  complete=ok
서버 sha256 == 로컬: True
```

출력에서 볼 것:

- **create 두 번 → 같은 id**(`201` 다음 `200`). 업로드 생성 요청이 재전송돼도 업로드가 두 개 생기지 않는다(idempotency key).
- **응답 유실 → 409**: 서버는 이미 16384–32767을 붙였다. 같은 offset으로 다시 온 chunk를 붙이지 않고 "나는 32768에 있다"고 답한다. 중복 없음.
- **비트 오류 → 400**: chunk CRC가 안 맞아 서버가 거부하고 offset이 그대로(49152)다. 다시 보내 성공.
- **재부팅 → HEAD**: 클라이언트는 아무것도 기억하지 않고(업로드 id만 flash에 있다고 가정) 서버에 물어 65536부터 이어 보냈다.
- 마지막 chunk에서 서버가 SHA-256을 확인해 `complete=ok`. 이 ACK를 받은 뒤에만 기기가 파일을 지운다.

데모가 일부러 생략한 것: TLS와 인증(6절), 서버 쪽 durable 저장(메모리 dict), 동시에 같은 업로드에 두 클라이언트가 붙는 경우의 잠금(여기서는 전역 lock), 업로드 만료(오래 방치된 미완성 업로드 정리), 409 응답의 offset이 내 데이터의 prefix라는 가정(최종 SHA-256이 이를 검증한다).

### 4.9 재시도 — exponential backoff와 jitter

실패하면 다시 보내야 한다. 문제는 **언제** 다시 보내느냐다.

- **고정 간격 재시도**: 1초마다. 서버가 죽어 있으면 기기 수 × 초당 1회의 부하가 계속된다.
- **exponential backoff**: 실패할 때마다 대기를 2배로(1, 2, 4, 8 … 초, 상한 있음). 부하는 줄지만 **모두가 같은 순간에 실패했다면 모두가 같은 순간에 다시 온다** — 동기화된 파도(thundering herd)가 2배 간격으로 계속 친다.
- **exponential backoff + full jitter**: 대기 = `uniform(0, min(cap, base × 2^n))`. 재시도 시각이 흩어져 파도가 평평해진다. AWS Architecture Blog의 "Exponential Backoff And Jitter"(Marc Brooker, 2015)가 이 비교로 유명하다.

웨어러블 fleet에서 이것이 실제로 일어나는 순간: 서버 배포 후 장애, 그리고 **모든 기기가 "자정에 업로드" 같은 같은 스케줄을 가질 때.** 후자는 재시도 이전에 스케줄 자체에 jitter를 넣어야 한다(7절).

시뮬레이션: 1000대가 t = 0에 동시에 실패, 서버는 10초 동안 죽어 있다가 초당 100건을 처리한다. 두 가지 서버 모델을 본다 — 과부하여도 100건은 처리하는 서버, 그리고 과부하면 처리량이 무너지는(요청을 거절하는 데도 자원이 들어서) 서버.

```python
# 1000대가 동시에 실패했을 때 — 재시도 전략별 서버 부하 (1초 bin, 서버 용량 100 req/s, 10초 장애)
import numpy as np
def simulate(strategy, collapse, n=1000, cap=100, outage=10, max_delay=64, seed=0):
    rng = np.random.default_rng(seed)
    t_next = np.zeros(n); attempt = np.zeros(n, int); done = np.zeros(n, bool)
    load = np.zeros(600, int); t_done = np.full(n, np.nan)
    for sec in range(600):
        idx = np.where(~done & (t_next >= sec) & (t_next < sec + 1))[0]
        load[sec] = len(idx)
        served = cap if len(idx) <= cap or not collapse else int(cap * cap / len(idx))
        ok = np.zeros(len(idx), bool)
        if sec >= outage: ok[rng.permutation(len(idx))[:served]] = True   # 과부하면 일부만 성공
        done[idx[ok]] = True; t_done[idx[ok]] = sec
        fail = idx[~ok]; attempt[fail] += 1
        d = np.minimum(max_delay, 2.0 ** attempt[fail])          # 2, 4, 8 … 초 (상한 64)
        if strategy == "fixed 1s":    d = np.ones(len(fail))
        if strategy == "full jitter": d = rng.uniform(0, d)       # [0, d) 균등 — "full jitter"
        t_next[fail] += d
        if done.all(): break
    return load, t_done
for collapse in [False, True]:
    print("과부하 시 서버 goodput:", "유지 (100/s)" if not collapse else "붕괴 (100·100/도착수)")
    for s in ["fixed 1s", "exp no jitter", "full jitter"]:
        load, t_done = simulate(s, collapse)
        print(f"  {s:14s} 총 요청 {load.sum():6d}  복구 후 최대 {load[10:].max():4d} req/s  "
              f"전원 완료 {np.nanmax(t_done):4.0f} s  중앙값 {np.nanmedian(t_done):4.0f} s")
```

```text
과부하 시 서버 goodput: 유지 (100/s)
  fixed 1s       총 요청  15500  복구 후 최대 1000 req/s  전원 완료   19 s  중앙값   14 s
  exp no jitter  총 요청   8500  복구 후 최대 1000 req/s  전원 완료  510 s  중앙값  222 s
  full jitter    총 요청   2784  복구 후 최대   68 req/s  전원 완료   54 s  중앙값   14 s
과부하 시 서버 goodput: 붕괴 (100·100/도착수)
  fixed 1s       총 요청  45563  복구 후 최대 1000 req/s  전원 완료   63 s  중앙값   49 s
  exp no jitter  총 요청  13450  복구 후 최대 1000 req/s  전원 완료  574 s  중앙값  254 s
  full jitter    총 요청   2784  복구 후 최대   68 req/s  전원 완료   54 s  중앙값   14 s
```

출력에서 볼 것:

- **full jitter는 총 요청 2784건, 복구 후 최대 68 req/s** — 서버 용량(100) 아래에 머문다. 중앙값 14초에 끝난다.
- 고정 1초 재시도는 서버가 버텨 주면 빨리 끝나지만(19 s) 요청이 15,500건이다. 서버가 과부하에서 무너지는 현실적 모델에서는 45,563건, 63초로 나빠진다.
- **jitter 없는 exponential은 최악이다**: 1000대가 계속 같은 순간에 몰려와 매번 100대만 성공하고 나머지는 더 긴 대기로 밀려 전원 완료까지 510 – 574초가 걸린다. backoff만으로는 동기화가 풀리지 않는다.
- full jitter 결과는 두 서버 모델에서 같다 — 애초에 과부하를 만들지 않기 때문이다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="280" x2="640" y2="280" stroke="currentColor"/><line x1="70" y1="280" x2="70" y2="30" stroke="currentColor"/><line x1="70.0" y1="280" x2="70.0" y2="284" stroke="currentColor"/><text x="70.0" y="298" font-size="12" text-anchor="middle">0</text><line x1="165.0" y1="280" x2="165.0" y2="284" stroke="currentColor"/><text x="165.0" y="298" font-size="12" text-anchor="middle">20</text><line x1="260.0" y1="280" x2="260.0" y2="284" stroke="currentColor"/><text x="260.0" y="298" font-size="12" text-anchor="middle">40</text><line x1="355.0" y1="280" x2="355.0" y2="284" stroke="currentColor"/><text x="355.0" y="298" font-size="12" text-anchor="middle">60</text><line x1="450.0" y1="280" x2="450.0" y2="284" stroke="currentColor"/><text x="450.0" y="298" font-size="12" text-anchor="middle">80</text>
<line x1="545.0" y1="280" x2="545.0" y2="284" stroke="currentColor"/><text x="545.0" y="298" font-size="12" text-anchor="middle">100</text><line x1="640.0" y1="280" x2="640.0" y2="284" stroke="currentColor"/><text x="640.0" y="298" font-size="12" text-anchor="middle">120</text><line x1="66" y1="280.0" x2="70" y2="280.0" stroke="currentColor"/><text x="62" y="284.0" font-size="12" text-anchor="end">0</text><line x1="66" y1="217.5" x2="70" y2="217.5" stroke="currentColor"/><text x="62" y="221.5" font-size="12" text-anchor="end">250</text><line x1="66" y1="155.0" x2="70" y2="155.0" stroke="currentColor"/><text x="62" y="159.0" font-size="12" text-anchor="end">500</text><line x1="66" y1="92.5" x2="70" y2="92.5" stroke="currentColor"/><text x="62" y="96.5" font-size="12" text-anchor="end">750</text>
<line x1="66" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/><text x="62" y="34.0" font-size="12" text-anchor="end">1000</text><rect x="70" y="30" width="47.5" height="250" fill="#888" fill-opacity="0.15"/><text x="93.8" y="46" font-size="12" text-anchor="middle">장애</text><line x1="70" y1="255.0" x2="640" y2="255.0" stroke="currentColor" stroke-dasharray="5 4"/><text x="640" y="249.0" font-size="12" text-anchor="end">서버 용량 100 req/s</text>
<polyline points="70.0,30.0 74.8,30.0 74.8,30.0 79.5,30.0 79.5,30.0 84.2,30.0 84.2,30.0 89.0,30.0 89.0,30.0 93.8,30.0 93.8,30.0 98.5,30.0 98.5,30.0 103.2,30.0 103.2,30.0 108.0,30.0 108.0,30.0 112.8,30.0 112.8,30.0 117.5,30.0 117.5,30.0 122.2,30.0 122.2,55.0 127.0,55.0 127.0,80.0 131.8,80.0 131.8,105.0 136.5,105.0 136.5,130.0 141.2,130.0 141.2,155.0 146.0,155.0 146.0,180.0 150.8,180.0 150.8,205.0 155.5,205.0 155.5,230.0 160.2,230.0 160.2,255.0 165.0,255.0 165.0,280.0 169.8,280.0 169.8,280.0 174.5,280.0 174.5,280.0 179.2,280.0 179.2,280.0 184.0,280.0 184.0,280.0 188.8,280.0 188.8,280.0 193.5,280.0 193.5,280.0 198.2,280.0 198.2,280.0 203.0,280.0 203.0,280.0 207.8,280.0 207.8,280.0 212.5,280.0 212.5,280.0 217.3,280.0 217.3,280.0 222.0,280.0 222.0,280.0 226.8,280.0 226.8,280.0 231.5,280.0 231.5,280.0 236.2,280.0 236.2,280.0 241.0,280.0 241.0,280.0 245.8,280.0 245.8,280.0 250.5,280.0 250.5,280.0 255.2,280.0 255.2,280.0 260.0,280.0 260.0,280.0 264.8,280.0 264.8,280.0 269.5,280.0 269.5,280.0 274.2,280.0 274.2,280.0 279.0,280.0 279.0,280.0 283.8,280.0 283.8,280.0 288.5,280.0 288.5,280.0 293.2,280.0 293.2,280.0 298.0,280.0 298.0,280.0 302.8,280.0 302.8,280.0 307.5,280.0 307.5,280.0 312.2,280.0 312.2,280.0 317.0,280.0 317.0,280.0 321.8,280.0 321.8,280.0 326.5,280.0 326.5,280.0 331.2,280.0 331.2,280.0 336.0,280.0 336.0,280.0 340.8,280.0 340.8,280.0 345.5,280.0 345.5,280.0 350.2,280.0 350.2,280.0 355.0,280.0 355.0,280.0 359.8,280.0 359.8,280.0 364.5,280.0 364.5,280.0 369.2,280.0 369.2,280.0 374.0,280.0 374.0,280.0 378.8,280.0 378.8,280.0 383.5,280.0 383.5,280.0 388.2,280.0 388.2,280.0 393.0,280.0 393.0,280.0 397.8,280.0 397.8,280.0 402.5,280.0 402.5,280.0 407.2,280.0 407.2,280.0 412.0,280.0 412.0,280.0 416.7,280.0 416.7,280.0 421.5,280.0 421.5,280.0 426.2,280.0 426.2,280.0 431.0,280.0 431.0,280.0 435.8,280.0 435.8,280.0 440.5,280.0 440.5,280.0 445.2,280.0 445.2,280.0 450.0,280.0 450.0,280.0 454.8,280.0 454.8,280.0 459.5,280.0 459.5,280.0 464.2,280.0 464.2,280.0 469.0,280.0 469.0,280.0 473.8,280.0 473.8,280.0 478.5,280.0 478.5,280.0 483.2,280.0 483.2,280.0 488.0,280.0 488.0,280.0 492.8,280.0 492.8,280.0 497.5,280.0 497.5,280.0 502.2,280.0 502.2,280.0 507.0,280.0 507.0,280.0 511.8,280.0 511.8,280.0 516.5,280.0 516.5,280.0 521.2,280.0 521.2,280.0 526.0,280.0 526.0,280.0 530.8,280.0 530.8,280.0 535.5,280.0 535.5,280.0 540.2,280.0 540.2,280.0 545.0,280.0 545.0,280.0 549.8,280.0 549.8,280.0 554.5,280.0 554.5,280.0 559.2,280.0 559.2,280.0 564.0,280.0 564.0,280.0 568.8,280.0 568.8,280.0 573.5,280.0 573.5,280.0 578.2,280.0 578.2,280.0 583.0,280.0 583.0,280.0 587.8,280.0 587.8,280.0 592.5,280.0 592.5,280.0 597.2,280.0 597.2,280.0 602.0,280.0 602.0,280.0 606.8,280.0 606.8,280.0 611.5,280.0 611.5,280.0 616.2,280.0 616.2,280.0 621.0,280.0 621.0,280.0 625.8,280.0 625.8,280.0 630.5,280.0 630.5,280.0 635.2,280.0 635.2,280.0 640.0,280.0" fill="none" stroke="#d0564a" stroke-width="1.8"/>
<polyline points="70.0,30.0 74.8,30.0 74.8,280.0 79.5,280.0 79.5,30.0 84.2,30.0 84.2,280.0 89.0,280.0 89.0,280.0 93.8,280.0 93.8,280.0 98.5,280.0 98.5,30.0 103.2,30.0 103.2,280.0 108.0,280.0 108.0,280.0 112.8,280.0 112.8,280.0 117.5,280.0 117.5,280.0 122.2,280.0 122.2,280.0 127.0,280.0 127.0,280.0 131.8,280.0 131.8,280.0 136.5,280.0 136.5,30.0 141.2,30.0 141.2,280.0 146.0,280.0 146.0,280.0 150.8,280.0 150.8,280.0 155.5,280.0 155.5,280.0 160.2,280.0 160.2,280.0 165.0,280.0 165.0,280.0 169.8,280.0 169.8,280.0 174.5,280.0 174.5,280.0 179.2,280.0 179.2,280.0 184.0,280.0 184.0,280.0 188.8,280.0 188.8,280.0 193.5,280.0 193.5,280.0 198.2,280.0 198.2,280.0 203.0,280.0 203.0,280.0 207.8,280.0 207.8,280.0 212.5,280.0 212.5,55.0 217.3,55.0 217.3,280.0 222.0,280.0 222.0,280.0 226.8,280.0 226.8,280.0 231.5,280.0 231.5,280.0 236.2,280.0 236.2,280.0 241.0,280.0 241.0,280.0 245.8,280.0 245.8,280.0 250.5,280.0 250.5,280.0 255.2,280.0 255.2,280.0 260.0,280.0 260.0,280.0 264.8,280.0 264.8,280.0 269.5,280.0 269.5,280.0 274.2,280.0 274.2,280.0 279.0,280.0 279.0,280.0 283.8,280.0 283.8,280.0 288.5,280.0 288.5,280.0 293.2,280.0 293.2,280.0 298.0,280.0 298.0,280.0 302.8,280.0 302.8,280.0 307.5,280.0 307.5,280.0 312.2,280.0 312.2,280.0 317.0,280.0 317.0,280.0 321.8,280.0 321.8,280.0 326.5,280.0 326.5,280.0 331.2,280.0 331.2,280.0 336.0,280.0 336.0,280.0 340.8,280.0 340.8,280.0 345.5,280.0 345.5,280.0 350.2,280.0 350.2,280.0 355.0,280.0 355.0,280.0 359.8,280.0 359.8,280.0 364.5,280.0 364.5,80.0 369.2,80.0 369.2,280.0 374.0,280.0 374.0,280.0 378.8,280.0 378.8,280.0 383.5,280.0 383.5,280.0 388.2,280.0 388.2,280.0 393.0,280.0 393.0,280.0 397.8,280.0 397.8,280.0 402.5,280.0 402.5,280.0 407.2,280.0 407.2,280.0 412.0,280.0 412.0,280.0 416.7,280.0 416.7,280.0 421.5,280.0 421.5,280.0 426.2,280.0 426.2,280.0 431.0,280.0 431.0,280.0 435.8,280.0 435.8,280.0 440.5,280.0 440.5,280.0 445.2,280.0 445.2,280.0 450.0,280.0 450.0,280.0 454.8,280.0 454.8,280.0 459.5,280.0 459.5,280.0 464.2,280.0 464.2,280.0 469.0,280.0 469.0,280.0 473.8,280.0 473.8,280.0 478.5,280.0 478.5,280.0 483.2,280.0 483.2,280.0 488.0,280.0 488.0,280.0 492.8,280.0 492.8,280.0 497.5,280.0 497.5,280.0 502.2,280.0 502.2,280.0 507.0,280.0 507.0,280.0 511.8,280.0 511.8,280.0 516.5,280.0 516.5,280.0 521.2,280.0 521.2,280.0 526.0,280.0 526.0,280.0 530.8,280.0 530.8,280.0 535.5,280.0 535.5,280.0 540.2,280.0 540.2,280.0 545.0,280.0 545.0,280.0 549.8,280.0 549.8,280.0 554.5,280.0 554.5,280.0 559.2,280.0 559.2,280.0 564.0,280.0 564.0,280.0 568.8,280.0 568.8,280.0 573.5,280.0 573.5,280.0 578.2,280.0 578.2,280.0 583.0,280.0 583.0,280.0 587.8,280.0 587.8,280.0 592.5,280.0 592.5,280.0 597.2,280.0 597.2,280.0 602.0,280.0 602.0,280.0 606.8,280.0 606.8,280.0 611.5,280.0 611.5,280.0 616.2,280.0 616.2,280.0 621.0,280.0 621.0,280.0 625.8,280.0 625.8,280.0 630.5,280.0 630.5,280.0 635.2,280.0 635.2,280.0 640.0,280.0" fill="none" stroke="#e08a3c" stroke-width="1.8"/>
<polyline points="70.0,30.0 74.8,30.0 74.8,148.2 79.5,148.2 79.5,250.2 84.2,250.2 84.2,240.5 89.0,240.5 89.0,240.5 93.8,240.5 93.8,248.5 98.5,248.5 98.5,259.2 103.2,259.2 103.2,265.5 108.0,265.5 108.0,260.0 112.8,260.0 112.8,263.2 117.5,263.2 117.5,263.0 122.2,263.0 122.2,268.5 127.0,268.5 127.0,271.0 131.8,271.0 131.8,272.5 136.5,272.5 136.5,274.0 141.2,274.0 141.2,274.2 146.0,274.2 146.0,273.2 150.8,273.2 150.8,274.0 155.5,274.0 155.5,276.5 160.2,276.5 160.2,276.5 165.0,276.5 165.0,276.2 169.8,276.2 169.8,276.0 174.5,276.0 174.5,277.0 179.2,277.0 179.2,276.8 184.0,276.8 184.0,278.0 188.8,278.0 188.8,279.5 193.5,279.5 193.5,279.8 198.2,279.8 198.2,279.0 203.0,279.0 203.0,279.8 207.8,279.8 207.8,278.8 212.5,278.8 212.5,279.8 217.3,279.8 217.3,278.8 222.0,278.8 222.0,279.5 226.8,279.5 226.8,279.5 231.5,279.5 231.5,280.0 236.2,280.0 236.2,279.0 241.0,279.0 241.0,279.5 245.8,279.5 245.8,279.0 250.5,279.0 250.5,280.0 255.2,280.0 255.2,279.8 260.0,279.8 260.0,279.5 264.8,279.5 264.8,280.0 269.5,280.0 269.5,280.0 274.2,280.0 274.2,280.0 279.0,280.0 279.0,280.0 283.8,280.0 283.8,280.0 288.5,280.0 288.5,279.8 293.2,279.8 293.2,280.0 298.0,280.0 298.0,280.0 302.8,280.0 302.8,280.0 307.5,280.0 307.5,280.0 312.2,280.0 312.2,280.0 317.0,280.0 317.0,280.0 321.8,280.0 321.8,280.0 326.5,280.0 326.5,279.8 331.2,279.8 331.2,280.0 336.0,280.0 336.0,280.0 340.8,280.0 340.8,280.0 345.5,280.0 345.5,280.0 350.2,280.0 350.2,280.0 355.0,280.0 355.0,280.0 359.8,280.0 359.8,280.0 364.5,280.0 364.5,280.0 369.2,280.0 369.2,280.0 374.0,280.0 374.0,280.0 378.8,280.0 378.8,280.0 383.5,280.0 383.5,280.0 388.2,280.0 388.2,280.0 393.0,280.0 393.0,280.0 397.8,280.0 397.8,280.0 402.5,280.0 402.5,280.0 407.2,280.0 407.2,280.0 412.0,280.0 412.0,280.0 416.7,280.0 416.7,280.0 421.5,280.0 421.5,280.0 426.2,280.0 426.2,280.0 431.0,280.0 431.0,280.0 435.8,280.0 435.8,280.0 440.5,280.0 440.5,280.0 445.2,280.0 445.2,280.0 450.0,280.0 450.0,280.0 454.8,280.0 454.8,280.0 459.5,280.0 459.5,280.0 464.2,280.0 464.2,280.0 469.0,280.0 469.0,280.0 473.8,280.0 473.8,280.0 478.5,280.0 478.5,280.0 483.2,280.0 483.2,280.0 488.0,280.0 488.0,280.0 492.8,280.0 492.8,280.0 497.5,280.0 497.5,280.0 502.2,280.0 502.2,280.0 507.0,280.0 507.0,280.0 511.8,280.0 511.8,280.0 516.5,280.0 516.5,280.0 521.2,280.0 521.2,280.0 526.0,280.0 526.0,280.0 530.8,280.0 530.8,280.0 535.5,280.0 535.5,280.0 540.2,280.0 540.2,280.0 545.0,280.0 545.0,280.0 549.8,280.0 549.8,280.0 554.5,280.0 554.5,280.0 559.2,280.0 559.2,280.0 564.0,280.0 564.0,280.0 568.8,280.0 568.8,280.0 573.5,280.0 573.5,280.0 578.2,280.0 578.2,280.0 583.0,280.0 583.0,280.0 587.8,280.0 587.8,280.0 592.5,280.0 592.5,280.0 597.2,280.0 597.2,280.0 602.0,280.0 602.0,280.0 606.8,280.0 606.8,280.0 611.5,280.0 611.5,280.0 616.2,280.0 616.2,280.0 621.0,280.0 621.0,280.0 625.8,280.0 625.8,280.0 630.5,280.0 630.5,280.0 635.2,280.0 635.2,280.0 640.0,280.0" fill="none" stroke="#4a7bd0" stroke-width="1.8"/>
<text x="355.0" y="318" font-size="13" text-anchor="middle">시간 (s)</text><text x="20" y="155.0" font-size="13" text-anchor="middle" transform="rotate(-90 20 155.0)">초당 요청 수</text><line x1="80" y1="355" x2="105" y2="355" stroke="#d0564a" stroke-width="3"/><text x="112" y="359" font-size="12">고정 1초 재시도</text><line x1="280" y1="355" x2="305" y2="355" stroke="#e08a3c" stroke-width="3"/><text x="312" y="359" font-size="12">지수 backoff, jitter 없음</text><line x1="480" y1="355" x2="505" y2="355" stroke="#4a7bd0" stroke-width="3"/><text x="512" y="359" font-size="12">지수 backoff + full jitter</text>
</svg>
```

그림 8 — 시간별 초당 요청 수 (과부하에도 용량 유지 모델, 처음 120초). 빨강(고정 1초)은 장애 동안 초당 1000건을 계속 쏜다. 주황(jitter 없는 지수)은 2, 4, 8, 16 … 초마다 1000건짜리 파도가 친다. 파랑(full jitter)은 첫 실패 뒤 요청이 흩어져 점선(서버 용량) 아래에 머문다.

실무 규칙:

- backoff에 **상한**(cap)과 **최대 시도 횟수 또는 기한**을 둔다. 기한을 넘으면 포기하고 대기열에 남겨 다음 스케줄 창에서 다시.
- 서버가 `429 Too Many Requests`나 `503 Service Unavailable`에 `Retry-After`를 주면 그 값을 따른다(서버 주도 throttling, 7절 · H8).
- 재시도해도 소용없는 오류는 재시도하지 않는다: 400(요청이 잘못됨 — 단, 이 데모처럼 CRC 불일치는 재전송이 맞다), 401/403(인증 — 토큰 갱신이 먼저), 413(너무 큼).
- 재시도는 **멱등한 요청에만** 자동으로 한다(4.4절이 그래서 먼저다).

---

## 5. 바이트 스트림 위 framing — UART, BLE serial, USB CDC

### 5.1 왜 framing이 필요한가

UART, USB CDC-ACM, 그리고 BLE 위에 시리얼처럼 쓰는 커스텀 서비스(Nordic UART Service 같은)는 **바이트 스트림**이다. "메시지"라는 경계가 없다. 받는 쪽이 중간부터 듣기 시작하거나, 바이트 하나가 빠지면 어디가 메시지 시작인지 모른다. **framing**은 스트림에 경계를 표시하는 규칙이다. E8 2.4절에서 COBS + CRC-16으로 한 번 만들었다. 여기서는 세 가지 방식을 비교하고 그 위에 신뢰성(ARQ)을 얹는다.

| 방식 | 규칙 | 장점 | 단점 |
|---|---|---|---|
| 길이 prefix | `[len][payload][crc]` | 오버헤드 고정 (2 B), 구현 단순 | 길이 바이트가 깨지거나 중간부터 들으면 **동기를 잃는다** → 별도 sync 바이트 · 타임아웃 필요 |
| SLIP (RFC 1055) | 끝에 `0xC0`, payload 안의 `0xC0`/`0xDB`는 2바이트 escape | 구분자로 바로 재동기 | 오버헤드가 데이터 의존 — 최악 2배 |
| COBS (Cheshire & Baker) | payload의 `0x00`을 없애고 프레임 끝에 `0x00` 하나 | 구분자로 바로 재동기, **오버헤드 상한이 작다** (254 B마다 1 B + 2) | 인코딩에 조금 더 복잡한 로직, 인코딩 전 프레임 길이만큼 버퍼 |

### 5.2 코드로 확인 — 오버헤드 비교

같은 payload를 세 방식으로 인코딩해 늘어난 바이트를 센다. 최악의 경우도 일부러 만든다.

```python
# 바이트 스트림 framing 3종의 오버헤드 — 길이 prefix, SLIP(RFC 1055), COBS
import random
def slip(p):                                   # END=0xC0, ESC=0xDB → 두 바이트로 escape
    out = bytearray([0xC0])
    for b in p: out += {0xC0: b"\xdb\xdc", 0xDB: b"\xdb\xdd"}.get(b, bytes([b]))
    return bytes(out + b"\xc0")
def cobs(p):                                   # 0x00을 없애고, 프레임 끝에 0x00 하나
    out, code_i, code = bytearray([0]), 0, 1
    for b in p:
        if b == 0:
            out[code_i] = code; code_i = len(out); out.append(0); code = 1
        else:
            out.append(b); code += 1
            if code == 0xFF: out[code_i] = code; code_i = len(out); out.append(0); code = 1
    out[code_i] = code
    return bytes(out + b"\x00")
def lenpfx(p): return len(p).to_bytes(2, "little") + p        # 길이 2바이트 + payload
random.seed(1)
cases = {"random 240 B": bytes(random.getrandbits(8) for _ in range(240)),
         "zeros 240 B (IMU 정지)": bytes(240),
         "all 0xC0 240 B (SLIP 최악)": bytes([0xC0]) * 240,
         "no-zero 1000 B (COBS 최악)": bytes(random.randint(1, 255) for _ in range(1000))}
print(f"{'payload':28s} {'len-prefix':>10s} {'SLIP':>6s} {'COBS':>6s}")
for name, p in cases.items():
    print(f"{name:28s} " + " ".join(f"{len(f(p)) - len(p):+6d}".rjust(w) for f, w in
                                    [(lenpfx, 10), (slip, 6), (cobs, 6)]))
print("COBS 예:", cobs(bytes([0x11, 0x00, 0x22, 0x33])).hex(" "))
```

```text
payload                      len-prefix   SLIP   COBS
random 240 B                         +2     +3     +2
zeros 240 B (IMU 정지)                 +2     +2     +2
all 0xC0 240 B (SLIP 최악)             +2   +242     +2
no-zero 1000 B (COBS 최악)             +2     +8     +5
COBS 예: 02 11 03 22 33 00
```

출력에서 볼 것:

- 랜덤 240 B는 셋 다 +2 – 3 B. 평범한 데이터에서는 차이가 없다.
- **`0xC0`만 있는 payload에서 SLIP은 +242 B(2배)**. 데이터 분포가 escape 대상 바이트에 몰리면(센서 값 · 압축 데이터는 분포를 예측하기 어렵다) 대역폭이 데이터 의존적으로 바뀐다. 실시간 링크 예산을 잡을 때 나쁘다.
- **COBS는 최악에도 1000 B에 +5 B**: `0x00`이 하나도 없는 1000 B는 254 B마다 코드 바이트 1개 + 시작 1 + 구분자 1이다. 상한이 정해져 있어서 버퍼 크기를 정적으로 잡을 수 있다.
- COBS 예: `11 00 22 33` → `02 11 03 22 33 00`. 코드 바이트 `02`는 "다음 1바이트 뒤에 0이 있었다", `03`은 "다음 2바이트는 0이 아니고 끝"이라는 뜻이다.

길이 prefix도 실무에서 많이 쓴다 — 그때는 `[sync 0xA5 0x5A][len][hdr crc][payload][crc]`처럼 **sync 바이트 + 헤더 CRC**를 넣어서, 동기를 잃으면 sync를 찾아 다시 맞추고 헤더 CRC로 가짜 sync를 걸러 낸다. USB나 SPI처럼 하위 계층이 이미 패킷 경계를 주는 경우는 framing보다 헤더 설계가 중요하다.

### 5.3 CRC 고르기 — 몇 비트가 필요한가

CRC 선택은 "어떤 오류를 얼마나 놓쳐도 되나"의 문제다. 256 B 프레임에 세 종류의 오류를 10만 번씩 넣고 못 잡은 횟수를 센다. 비교 대상은 8-bit 합(checksum), CRC-16/CCITT-FALSE(E8에서 쓴 것), CRC-32.

```python
# 256 B 프레임에 오류를 주입했을 때 "못 잡은" 횟수 — 8-bit 합, CRC-16/CCITT-FALSE, CRC-32
import binascii, zlib, random
random.seed(0)
checks = {"sum8": lambda b: sum(b) & 0xFF,
          "crc16": lambda b: binascii.crc_hqx(b, 0xFFFF),     # CCITT-FALSE (E8의 CRC-16)
          "crc32": zlib.crc32}
def corrupt(b, kind):
    b = bytearray(b)
    if kind == "2-bit 랜덤":
        for pos in random.sample(range(len(b) * 8), 2): b[pos // 8] ^= 1 << (pos % 8)
    elif kind == "burst ≤16 bit":
        L = random.randint(2, 16); s = random.randrange(len(b) * 8 - L)
        flips = [s, s + L - 1] + [s + i for i in range(1, L - 1) if random.random() < 0.5]
        for pos in set(flips): b[pos // 8] ^= 1 << (pos % 8)
    else:                                                     # 프레임 전체가 쓰레기 (동기 상실)
        b = bytearray(random.getrandbits(8) for _ in range(len(b)))
    return bytes(b)
N = 100_000
for kind in ["2-bit 랜덤", "burst ≤16 bit", "전체 랜덤"]:
    miss = dict.fromkeys(checks, 0)
    for _ in range(N):
        good = random.randbytes(256); bad = corrupt(good, kind)
        if bad == good: continue
        for k, f in checks.items(): miss[k] += f(bad) == f(good)
    print(f"{kind:14s} " + "  ".join(f"{k} 놓침 {v:5d}/{N}" for k, v in miss.items()))
```

```text
2-bit 랜덤       sum8 놓침  7082/100000  crc16 놓침     0/100000  crc32 놓침     0/100000
burst ≤16 bit  sum8 놓침   231/100000  crc16 놓침     0/100000  crc32 놓침     0/100000
전체 랜덤          sum8 놓침   364/100000  crc16 놓침     2/100000  crc32 놓침     0/100000
```

출력에서 볼 것:

- **8-bit 합은 2-bit 오류를 7 % 놓친다** — 같은 비트 위치의 0→1, 1→0이 서로 상쇄되기 때문이다. 단순 합은 쓰지 말자.
- CRC-16과 CRC-32는 2-bit 오류와 16비트 이하 burst를 **하나도 놓치지 않았다**. 이것은 운이 아니라 수학이다: n-bit CRC는 길이 n 이하의 burst 오류를 전부 잡고, 좋은 다항식은 프레임 길이가 충분히 짧으면 적은 수의 비트 오류(Hamming distance 이하)를 전부 잡는다.
- 프레임 전체가 쓰레기일 때(동기를 잃어 엉뚱한 바이트를 프레임으로 받았을 때)는 확률 게임이다: CRC-16은 약 1/65536(10만 번 중 2번), CRC-32는 약 1/43억. **초당 수천 프레임이 오가는 링크에서 CRC-16은 하루에 여러 번 쓰레기를 통과시킬 수 있다.**

그래서 흔한 선택은:

- 짧은 링크 프레임(수십 – 수백 B, 하위 계층이 이미 CRC를 가짐, 예: BLE 위): CRC-16 또는 생략하고 chunk 단위 CRC-32에 맡김.
- UART · USB CDC처럼 노이즈와 동기 상실이 있을 수 있는 원시 바이트 스트림: 프레임마다 CRC-16 이상, 파일 · chunk 단위로 CRC-32.
- 파일 · 업로드 단위: CRC-32(빠른 오류 위치 확인) + SHA-256(끝과 끝).
- 다항식은 직접 만들지 말고 표준 것을 쓴다(Koopman의 CRC 표가 다항식 · 길이별 Hamming distance를 정리해 둔다). 많은 MCU에 CRC 하드웨어 유닛이 있으니 다항식 설정이 표준과 맞는지 check 값으로 확인한다.

### 5.4 sequence number와 ARQ — stop-and-wait vs sliding window

framing과 CRC는 "깨진 프레임을 버린다"까지만 한다. 버린 프레임을 다시 받아야 하면 **ARQ(Automatic Repeat reQuest)** 가 필요하다: 프레임마다 sequence number를 붙이고, 받는 쪽이 ACK하고, 보내는 쪽은 ACK가 안 오면 다시 보낸다.

- **stop-and-wait**: 하나 보내고 ACK를 기다린다. 단순하지만 RTT 동안 링크가 논다.
- **Go-Back-N (GBN)**: ACK 없이 W개까지 보낸다(window). 받는 쪽은 순서대로만 받고, 하나가 빠지면 그 뒤를 전부 버린다. 보내는 쪽은 timeout 때 빠진 것부터 **전부** 다시 보낸다.
- **Selective Repeat (SR)**: 받는 쪽이 순서가 틀린 것도 버퍼에 받아 두고, 보내는 쪽은 **빠진 것만** 다시 보낸다. 수신 버퍼가 필요하다.

효율 공식 (프레임 시간 T_f, 왕복 지연 RTT를 프레임 단위로 d = RTT / T_f, 손실률 p):

```
stop-and-wait:  U = (1 − p) · 1 / (1 + d)
sliding window (이상적):  U = (1 − p) · min(1, W / (1 + d))
```

말로 하면: stop-and-wait는 프레임 하나를 보낼 때마다 RTT만큼 쉬고, window는 "RTT 동안 보낼 수 있는 양(대역폭 × 지연)"만큼 window가 크면 쉬지 않는다. 필요한 window ≈ 1 + d 프레임이다 — **bandwidth-delay product**.

```python
# stop-and-wait vs Go-Back-N vs Selective Repeat — 슬롯 단위 시뮬레이션 (ACK는 잃지 않는다고 가정)
import numpy as np
def sim(proto, p_loss, d, W=16, n=5000, seed=0):
    """d = 단방향 지연+ACK 지연을 프레임 시간 단위로 (RTT/T_frame). 반환: 링크 효율"""
    rng = np.random.default_rng(seed); W = 1 if proto == "SW" else W
    t, base, nxt, expected = 0, 0, 0, 0
    sent = {}                                # seq -> (보낸 슬롯, 손실 여부)
    acked, retx, acks = set(), [], []        # acks: (도착 슬롯, 누적 ack 값) — GBN용
    while base < n:
        for s, (ts, lost) in list(sent.items()):        # 1+d 슬롯 뒤에 ACK 또는 timeout
            if ts + 1 + d == t:
                del sent[s]
                if proto != "GBN": (retx.append(s) if lost else acked.add(s))
        if proto == "GBN":
            while acks and acks[0][0] <= t: base = max(base, acks.pop(0)[1])
            if nxt > base and base not in sent: nxt = base          # timeout → base부터 다시
        else:
            while base in acked: base += 1
        if retx: s = retx.pop(0)                                    # SR/SW: 잃은 것만 재전송
        elif nxt < min(base + W, n): s = nxt; nxt += 1
        else: t += 1; continue
        lost = rng.random() < p_loss; sent[s] = (t, lost)
        if proto == "GBN":
            if not lost and s == expected: expected += 1            # 순서대로만 받는다
            acks.append((t + 1 + d, expected))
        t += 1
    return n / t
T_f = 256 * 8 / 1e6                          # 256 B 프레임 @ 1 Mbit/s = 2.048 ms
for rtt_ms in [1, 20, 100]:
    d = round(rtt_ms / 1000 / T_f)
    for p in [0.0, 0.01, 0.05]:
        r = {pr: sim(pr, p, d) for pr in ["SW", "GBN", "SR"]}
        print(f"RTT {rtt_ms:3d} ms (d={d:2d})  loss {p:4.0%}  " +
              "  ".join(f"{k}={v:5.3f}" for k, v in r.items()) +
              f"   SW 이론={(1-p)/(1+d):5.3f}  SR 이론={(1-p)*min(1, 16/(1+d)):5.3f}")
```

```text
RTT   1 ms (d= 0)  loss   0%  SW=1.000  GBN=1.000  SR=1.000   SW 이론=1.000  SR 이론=1.000
RTT   1 ms (d= 0)  loss   1%  SW=0.990  GBN=0.990  SR=0.990   SW 이론=0.990  SR 이론=0.990
RTT   1 ms (d= 0)  loss   5%  SW=0.949  GBN=0.949  SR=0.949   SW 이론=0.950  SR 이론=0.950
RTT  20 ms (d=10)  loss   0%  SW=0.091  GBN=0.998  SR=0.998   SW 이론=0.091  SR 이론=1.000
RTT  20 ms (d=10)  loss   1%  SW=0.090  GBN=0.899  SR=0.945   SW 이론=0.090  SR 이론=0.990
RTT  20 ms (d=10)  loss   5%  SW=0.086  GBN=0.632  SR=0.831   SW 이론=0.086  SR 이론=0.950
RTT 100 ms (d=49)  loss   0%  SW=0.020  GBN=0.319  SR=0.319   SW 이론=0.020  SR 이론=0.320
RTT 100 ms (d=49)  loss   1%  SW=0.020  GBN=0.286  SR=0.289   SW 이론=0.020  SR 이론=0.317
RTT 100 ms (d=49)  loss   5%  SW=0.019  GBN=0.201  SR=0.231   SW 이론=0.019  SR 이론=0.304
```

출력에서 볼 것:

- RTT가 프레임 시간보다 훨씬 짧으면(d = 0, 예: 보드 위 UART 루프백) 세 방식이 같다. stop-and-wait로 충분하다.
- **RTT 20 ms(d = 10)에서 stop-and-wait는 9 %** 다. 1 Mbit/s 링크가 91 kbit/s 링크가 된다. window 16이면 손실 없을 때 거의 100 %.
- 손실 5 %에서 GBN(0.632)이 SR(0.831)보다 많이 떨어진다. GBN은 하나를 잃을 때 window 전체를 다시 보낸다.
- **RTT 100 ms(d = 49)에서 window 16은 부족하다**(0.32 ≈ 16/50). window를 bandwidth-delay product 이상으로 키워야 한다.
- 시뮬레이션의 SR이 "이론"보다 낮은 이유: 이론식은 window가 막히는 것(가장 오래된 프레임이 재전송을 기다리는 동안 window가 앞으로 못 감)을 무시한다. 손실과 RTT가 둘 다 클수록 이 차이가 커진다 — 공식보다 시뮬레이션을 믿어야 하는 예다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="280" x2="640" y2="280" stroke="currentColor"/><line x1="70" y1="280" x2="70" y2="30" stroke="currentColor"/><line x1="70.0" y1="280" x2="70.0" y2="284" stroke="currentColor"/><text x="70.0" y="298" font-size="12" text-anchor="middle">0</text><line x1="184.0" y1="280" x2="184.0" y2="284" stroke="currentColor"/><text x="184.0" y="298" font-size="12" text-anchor="middle">25</text><line x1="298.0" y1="280" x2="298.0" y2="284" stroke="currentColor"/><text x="298.0" y="298" font-size="12" text-anchor="middle">50</text><line x1="412.0" y1="280" x2="412.0" y2="284" stroke="currentColor"/><text x="412.0" y="298" font-size="12" text-anchor="middle">75</text><line x1="526.0" y1="280" x2="526.0" y2="284" stroke="currentColor"/><text x="526.0" y="298" font-size="12" text-anchor="middle">100</text>
<line x1="640.0" y1="280" x2="640.0" y2="284" stroke="currentColor"/><text x="640.0" y="298" font-size="12" text-anchor="middle">125</text><line x1="66" y1="280.0" x2="70" y2="280.0" stroke="currentColor"/><text x="62" y="284.0" font-size="12" text-anchor="end">0.00</text><line x1="66" y1="217.5" x2="70" y2="217.5" stroke="currentColor"/><text x="62" y="221.5" font-size="12" text-anchor="end">0.25</text><line x1="66" y1="155.0" x2="70" y2="155.0" stroke="currentColor"/><text x="62" y="159.0" font-size="12" text-anchor="end">0.50</text><line x1="66" y1="92.5" x2="70" y2="92.5" stroke="currentColor"/><text x="62" y="96.5" font-size="12" text-anchor="end">0.75</text><line x1="66" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/><text x="62" y="34.0" font-size="12" text-anchor="end">1.00</text>
<polyline points="70.0,32.9 98.0,218.2 126.0,244.7 154.0,255.3 182.1,261.0 210.1,264.6 238.1,267.0 266.1,268.8 294.1,270.1 322.1,271.2 350.2,272.0 378.2,272.7 406.2,273.3 434.2,273.8 462.2,274.3 490.2,274.6 518.3,275.0 546.3,275.2 574.3,275.5 602.3,275.7 630.3,275.9" fill="none" stroke="#d0564a" stroke-width="2.2"/><polyline points="70.0,32.9 98.0,41.1 126.0,48.4 154.0,56.1 182.1,63.7 210.1,70.3 238.1,101.4 266.1,124.5 294.1,142.3 322.1,156.5 350.2,168.0 378.2,177.5 406.2,185.6 434.2,192.5 462.2,198.4 490.2,203.6 518.3,208.2 546.3,212.2 574.3,215.8 602.3,219.1 630.3,222.0" fill="none" stroke="#e08a3c" stroke-width="2.2"/>
<polyline points="70.0,32.9 98.0,33.1 126.0,33.4 154.0,40.7 182.1,54.3 210.1,66.4 238.1,98.3 266.1,121.8 294.1,140.0 322.1,154.4 350.2,166.1 378.2,175.9 406.2,184.1 434.2,191.1 462.2,197.1 490.2,202.4 518.3,207.0 546.3,211.1 574.3,214.8 602.3,218.1 630.3,221.1" fill="none" stroke="#4a7bd0" stroke-width="2.2"/><polyline points="70.0,32.9 98.0,33.1 126.0,33.4 154.0,33.6 182.1,33.9 210.1,34.1 238.1,34.3 266.1,34.6 294.1,34.8 322.1,36.5 350.2,37.4 378.2,40.0 406.2,54.4 434.2,58.2 462.2,66.4 490.2,76.5 518.3,83.4 546.3,87.5 574.3,93.6 602.3,99.3 630.3,104.8" fill="none" stroke="#3f9a6b" stroke-width="2.2"/><text x="355.0" y="318" font-size="13" text-anchor="middle">RTT (ms) — 프레임 256 B @ 1 Mbit/s (2.048 ms), 손실 1 %</text><text x="20" y="155.0" font-size="13" text-anchor="middle" transform="rotate(-90 20 155.0)">링크 효율</text>
<line x1="80" y1="342" x2="105" y2="342" stroke="#d0564a" stroke-width="3"/><text x="112" y="346" font-size="12">stop-and-wait</text><line x1="380" y1="342" x2="405" y2="342" stroke="#e08a3c" stroke-width="3"/><text x="412" y="346" font-size="12">Go-Back-N W=16</text><line x1="80" y1="362" x2="105" y2="362" stroke="#4a7bd0" stroke-width="3"/><text x="112" y="366" font-size="12">Selective Repeat W=16</text><line x1="380" y1="362" x2="405" y2="362" stroke="#3f9a6b" stroke-width="3"/><text x="412" y="366" font-size="12">Selective Repeat W=64</text>
</svg>
```

그림 9 — RTT에 따른 링크 효율 (시뮬레이션, 256 B 프레임 @ 1 Mbit/s, 손실 1 %). stop-and-wait(빨강)는 RTT가 프레임 몇 개 분량만 돼도 급락한다. window 16(주황 GBN, 파랑 SR)은 RTT 약 25 ms부터, window 64(초록)는 약 60 ms부터 내려간다. 이론식(W ≥ 1 + d)대로라면 각각 약 31 ms, 129 ms까지 버텨야 하지만, 잃은 프레임이 재전송을 기다리는 동안 window가 막혀 더 일찍 떨어진다 — 손실이 있는 링크에서는 window를 bandwidth-delay product보다 넉넉히 잡는다.

### 5.5 그럼 BLE 위에서 ARQ를 또 만들어야 하나?

연결이 살아 있는 BLE는 링크 계층이 이미 ACK · 재전송 · 순서를 보장한다(2.7절). TCP도 그렇다. 그래서 **같은 연결 안에서 또 ARQ를 만들 필요는 없다** — 만들면 이중 재전송 · 이중 timeout이 서로 싸운다. 응용 계층이 해야 하는 일은:

- **연결이 끊겼다 다시 붙었을 때** 어디서부터 보낼지 정하는 것 → 4절의 offset 재개를 BLE 위에도 그대로 쓴다(폰이 "몇 바이트까지 받았다"를 알려 줌).
- 응용 수준 흐름 제어 → 폰 앱이 느려도 기기 버퍼가 넘치지 않게 credit(“N개 더 보내도 돼”)을 주는 방식.
- **원시 UART · USB CDC처럼 하위 계층이 신뢰성을 주지 않는 링크**에서만 sequence + ACK + 재전송을 직접 만든다. 이때도 window는 bandwidth-delay product에 맞춘다.

Don의 경험으로 말하면: PCIe는 Data Link Layer가 LCRC와 ACK/NAK 재전송(replay buffer)을 이미 하고, 그 위의 NVMe는 명령 단위 timeout · 재시도만 한다. 같은 원리 — **각 계층은 아래 계층이 보장하지 않는 것만** 한다.

---

## 6. 보안 기초 — 누가 보냈고, 누가 읽을 수 있나

이 노트는 보안 전문서가 아니므로 업로드 설계에 바로 영향을 주는 것만 짚는다.

- **기기 identity**: 기기마다 고유 키 쌍을 공장에서 만들고(가능하면 secure element나 TrustZone 같은 보호 영역 안에서, 개인 키는 밖으로 안 나가게) 서버 CA가 서명한 기기 인증서를 넣는다. 업로드는 이 인증서로 **mutual TLS(mTLS)** 를 하거나, 인증서로 짧은 수명의 토큰을 받아 쓴다. 공유 비밀(모든 기기에 같은 API 키)은 하나만 털려도 fleet 전체가 털린다.
- **TLS**: 서버 인증서 검증을 끄지 않는다(디버그 빌드의 "verify off"가 양산에 남는 사고). CA 번들 또는 pinning을 쓰되, pinning은 인증서 교체 계획과 함께(핀이 바뀌면 OTA 전에 기기가 서버에 못 붙는다).
- **signed / pre-signed 업로드**: 기기가 object storage에 직접 올리게 할 때는 서버가 발급한 **짧은 수명의 pre-signed URL**(S3 pre-signed URL 같은)을 쓴다. 기기에 저장소 자격 증명을 두지 않는다. 파일 SHA-256을 서명 대상에 포함하면 서버가 "기기가 의도한 그 내용"인지 확인할 수 있다.
- **기기에서의 암호화(at rest)**: 업로드 대기 중인 센서 · 오디오 데이터는 기기를 잃어버리면 노출된다. flash 암호화 또는 파일 단위 암호화, 키는 하드웨어 보호 — 정책과 보관 기간은 **H6**.
- **폰 relay**: 폰 앱은 기기 데이터를 그대로 전달만 하고, 가능하면 기기 → 서버 end-to-end 암호화로 폰이 내용을 못 보게 한다(설계 선택). BLE 링크 자체는 페어링 · 본딩으로 암호화한다(LE Secure Connections).
- **로그에 비밀을 올리지 않는다**: Wi-Fi 비밀번호, 토큰, 키, 사용자 음성 원문이 디버그 로그나 crash dump에 섞여 올라가는 사고가 흔하다. crash dump의 메모리 영역 중 키 버퍼는 덤프 전에 지우고, 로그 문자열은 빌드 단계에서 검사한다. PII 처리는 **H6**.
- **replay와 0-RTT**: 3.5절. 업로드 생성 · 완료 같은 요청은 0-RTT로 보내지 않는다.

---

## 7. 업로드 스케줄링 — 언제, 무엇을, 어떤 길로

### 7.1 정책의 재료

기기는 "지금 올릴까?"를 계속 판단한다. 재료는 이렇다.

- **링크 상태**: Wi-Fi 연결됨? 폰이 BLE로 붙어 있음? 셀룰러만? 아무것도 없음?
- **전원 상태**: 충전 중? 배터리 잔량? 온도(E9 — 충전 중 + 대량 업로드 = 발열)?
- **데이터 클래스와 우선순위**: crash dump > 메트릭 · 이벤트 > raw 센서 데이터(학습용). 사용자가 요청한 업로드(버그 신고)는 최우선.
- **quota**: 셀룰러 하루 바이트 상한(요금 · 배터리), 클래스별 저장 공간 상한.
- **저장 공간 압박**: flash가 차면 낮은 우선순위부터, 오래된 것부터 버린다(그리고 센다).
- **서버 지시**: 서버가 "raw는 당분간 올리지 마", "이 기기 그룹만 오디오 샘플링 5 %"처럼 원격으로 정책을 바꾼다 — fleet 단위 원격 설정은 **H8**. `429`/`503` + `Retry-After`도 서버 지시다.
- **jitter**: "새벽 3시에 업로드"를 모든 기기가 같은 초에 하지 않도록 창 안에서 무작위로 흩는다(4.9절).

폰 relay라면 폰 OS의 스케줄러를 쓴다: Android WorkManager의 constraint(`setRequiresCharging`, unmetered 네트워크 요구), iOS의 `BGTaskScheduler`와 `BGProcessingTaskRequest`(`requiresExternalPower`, `requiresNetworkConnectivity`). 이들은 OS가 실행 시각을 정하므로 "언젠가 실행됨"으로 설계해야 한다.

### 7.2 코드로 확인 — 하루 시뮬레이션

하루를 1분 단위로 돌린다. 가정: 0–6시와 22–24시는 충전 중 + Wi-Fi, 8–18시는 셀룰러만, 18–22시는 폰 BLE, 나머지(출퇴근)는 연결 없음. 메트릭 분당 2 KB, 깨어 있는 7–23시에 raw 센서 클립 시간당 6 MB, 10:17에 crash dump 200 KB. flash 대기열 상한 64 MB, 셀룰러 하루 1 MB quota, 셀룰러 메트릭은 1시간 배치.

```python
# 하루 동안의 업로드 스케줄러 — 우선순위 · 링크별 허용 클래스 · 셀룰러 quota · 저장 공간 압박
PRIO = {"crash": 0, "metrics": 1, "raw": 2}                 # 숫자가 작을수록 먼저
ALLOW = {"wifi+charging": {"crash", "metrics", "raw"}, "ble-phone": {"crash", "metrics"},
         "cellular": {"crash", "metrics"}, "none": set()}
RATE = {"wifi+charging": 2_000_000, "ble-phone": 60_000, "cellular": 500_000, "none": 0}  # B/s (실효, 가정)
def link_at(h):
    if h < 6 or h >= 22: return "wifi+charging"
    if 8 <= h < 18: return "cellular"
    if 18 <= h < 22: return "ble-phone"
    return "none"                                           # 출퇴근: 폰 연결 끊김 가정
queue, sent, dropped, CAP, cell_quota = [], {}, 0, 64_000_000, 1_000_000   # 셀룰러 하루 1 MB
wakeups = set()                                             # 셀룰러 radio를 깨운 분(minute)
for minute in range(24 * 60):
    h = minute / 60
    queue.append(["metrics", 2_000, minute])                # 분당 2 KB
    if 7 <= h < 23 and minute % 60 == 0: queue.append(["raw", 6_000_000, minute])   # 시간당 6 MB 클립
    if minute == 10 * 60 + 17: queue.append(["crash", 200_000, minute])
    while sum(q[1] for q in queue) > CAP:                   # 저장 공간 압박: raw부터, 오래된 것부터 버림
        victim = min((q for q in queue if q[0] == "raw"), key=lambda q: q[2], default=None)
        if victim is None: break
        queue.remove(victim); dropped += victim[1]
    link = link_at(h); budget = RATE[link] * 60             # 이번 1분 동안 보낼 수 있는 바이트
    for q in sorted(queue, key=lambda q: (PRIO[q[0]], q[2])):
        if q[0] not in ALLOW[link] or budget <= 0: continue
        if link == "cellular" and q[0] == "metrics" and minute % 60: continue   # 셀룰러는 1시간 배치
        n = min(q[1], budget, cell_quota if link == "cellular" else budget)
        if n <= 0: continue
        q[1] -= n; budget -= n
        if link == "cellular": cell_quota -= n; wakeups.add(minute)
        sent[(q[0], link)] = sent.get((q[0], link), 0) + n
        if q[1] == 0:
            queue.remove(q)
            if q[0] == "crash": print(f"crash dump: {q[2]//60:02d}:{q[2]%60:02d} 생성 → {minute//60:02d}:{minute%60:02d} 업로드 완료 ({link})")
for (cls, link), n in sorted(sent.items()): print(f"  {cls:8s} via {link:14s} {n/1e6:7.2f} MB")
print(f"버려진 raw {dropped/1e6:.0f} MB, 하루 끝 대기열 {sum(q[1] for q in queue)/1e6:.2f} MB, 셀룰러 quota 남음 {cell_quota/1e6:.2f} MB, 셀룰러 wake {len(wakeups)}회")
```

```text
crash dump: 10:17 생성 → 10:17 업로드 완료 (cellular)
  crash    via cellular          0.20 MB
  metrics  via ble-phone         1.12 MB
  metrics  via cellular          0.80 MB
  metrics  via wifi+charging     0.96 MB
  raw      via wifi+charging    60.00 MB
버려진 raw 36 MB, 하루 끝 대기열 0.00 MB, 셀룰러 quota 남음 0.00 MB, 셀룰러 wake 7회
```

출력에서 볼 것:

- **crash dump는 생성된 그 분에 셀룰러로 올라갔다** — 우선순위 0이고 셀룰러 허용 클래스다.
- 셀룰러 메트릭은 1시간 배치라 radio wake가 하루 7회뿐이다(분마다 보냈다면 수백 회 — 3.4절의 124 %를 떠올리자). 1 MB quota가 13시쯤 바닥나고, 나머지 메트릭은 저녁에 폰 BLE로 갔다.
- raw 96 MB 중 **36 MB를 버렸다.** 낮 동안 Wi-Fi가 없어 64 MB 대기열이 찼기 때문이다. 정책이 의도대로 동작한 것이지만, 이 숫자는 **"학습 데이터가 매일 37 % 사라진다"** 는 제품 결정 사항이다 — flash를 늘릴지, raw를 압축할지(H1), 샘플링할지(H8), 폰 relay로 일부를 보낼지 팀이 정해야 한다. 그래서 dropped 바이트는 반드시 메트릭으로 보고한다.

### 7.3 설계 체크리스트

```
[ ] 클래스별 우선순위와 링크별 허용 표가 설정(서버 원격 변경 가능)으로 존재
[ ] 셀룰러: 배치 주기 + 하루 quota + piggyback(이미 깨어 있으면 같이 보냄)
[ ] 대량 데이터: 충전 + Wi-Fi(unmetered) + 온도 OK일 때만
[ ] 스케줄 창 안에서 jitter, 실패 시 exponential backoff + full jitter, Retry-After 존중
[ ] 저장 공간 압박 시 버리는 순서 정의 + dropped 카운터를 메트릭으로 보고
[ ] 업로드 상태(id, offset, 상태 기계)를 flash에 저장, 재부팅 후 재개
[ ] ACK(서버 durable 저장) 전에는 삭제 금지
[ ] 사용자 요청 업로드(버그 신고)는 quota 예외 경로
```

---

## 8. 임베디드 관점에서 다시 보기

### 8.1 RAM과 flash 예산

| 항목 | 크기 감 (예시) | 메모 |
|---|---|---|
| BLE 송신 버퍼 | MTU 247 × 큐 깊이 4–8 ≈ 1–2 KB | event당 패킷 수를 채울 만큼은 있어야 throughput이 나온다 |
| 업로드 chunk 버퍼 | chunk 크기 × 2 (ping-pong) | flash 읽기와 전송을 겹치기 — DMA ping-pong과 같은 구조 |
| COBS 인코딩 버퍼 | 프레임 + 프레임/254 + 2 | 상한이 정해져 정적 할당 가능 (5.2절) |
| TLS (기기가 직접 HTTPS를 할 때) | 레코드 버퍼 수 KB – 16 KB 이상 + 핸드셰이크 힙 | max_fragment_length 협상으로 줄일 수 있음 (서버 지원 필요) |
| 업로드 상태 (flash) | 파일당 수십 B: id, offset, SHA-256 32 B, 상태 | 상태 변경마다 flash 쓰기 — wear를 고려해 로그 구조로 append (H1) |
| SHA-256 상태 | 약 100 B 문맥 | 파일을 쓰는 동안 스트리밍으로 갱신 → 닫을 때 해시가 바로 나온다 |

### 8.2 전력 관점의 펌웨어 규칙

- 무선은 "켜는 것"이 비싸다. 작은 메시지를 여러 번보다 **큰 버스트 한 번**(BLE도 마찬가지 — event를 꽉 채우고 길게 자기).
- 업로드 중에도 SoC · flash가 깨어 있다. 무선 대기(ACK 기다림) 동안 CPU는 WFI로 재운다.
- 해시 · CRC는 데이터를 **쓸 때 이미 계산**해 두면 업로드 때 flash를 다시 읽지 않아도 된다(H1과 계약).
- 전송 직전에 압축(H1)하면 바이트당 에너지가 큰 BLE · 셀룰러에서 이득이 크다. 압축 CPU 에너지 vs 무선 바이트 에너지를 D7 방식으로 비교한다.

### 8.3 C로 쓰는 업로드 상태 레코드 (스케치)

업로드 상태를 flash에 남기는 레코드다. 아래를 `rec.h`로 저장하고, 레이아웃이 72 B로 고정되는지 컴파일해서 확인한다.

```c
/* flash에 append하는 업로드 상태 레코드 — 마지막 유효 레코드가 현재 상태 */
#include <stdint.h>
enum up_state { UP_QUEUED = 1, UP_UPLOADING = 2, UP_ACKED = 3, UP_DROPPED = 4 };
typedef struct {
    uint32_t magic;          /* 레코드 시작 표시 */
    uint32_t file_seq;       /* 기기 안에서 단조 증가하는 파일 번호 */
    uint8_t  state;          /* enum up_state */
    uint8_t  prio;           /* 0 = crash, 1 = metrics, 2 = raw */
    uint16_t reserved;
    uint32_t file_len;
    uint32_t server_offset;  /* 마지막으로 서버가 확정한 offset (진실은 서버, 이것은 힌트) */
    uint8_t  upload_id[16];  /* 서버가 준 업로드 id */
    uint8_t  sha256[32];
    uint32_t crc32;          /* 이 레코드 자신의 CRC — 쓰다가 전원이 나가면 버린다 */
} up_record;
_Static_assert(sizeof(up_record) == 72, "flash 레이아웃 고정");
```

```sh
printf '#include "rec.h"\nint main(void){return (int)sizeof(up_record);}\n' > m.c && cc -std=c11 -Wall -Wextra -O2 m.c -o m && ./m; echo "sizeof = $?"
```

```text
sizeof = 72
```

말로 하면: 상태가 바뀔 때마다 덮어쓰지 않고 새 레코드를 append하고, 부팅 때 CRC가 맞는 마지막 레코드를 현재 상태로 읽는다. 쓰는 도중 전원이 나가면 그 레코드의 CRC가 틀려서 직전 상태로 돌아간다 — SSD FTL의 로그 구조 메타데이터와 같은 원리다. `server_offset`은 **힌트**일 뿐이고, 재개할 때는 항상 서버에 묻는다(4.3절).

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| MTU만 키우고 DLE 미협상 | MTU 247인데 throughput이 수십 kbit/s | ATT PDU가 27 B LL 조각 10개로 쪼개짐 | sniffer로 LL_LENGTH 교환 확인, DLE 활성화, MTU 247 정렬 |
| 폰 하나에서 잰 BLE 속도를 사양으로 씀 | 다른 폰 · BT 이어폰 사용 중 업로드가 몇 배 느림 | event당 패킷 수 · interval이 central과 coex에 달림 | 폰 매트릭스 측정, 최악 조건 기준 설계, 스케줄러가 느린 링크 감안 |
| 데이터가 생길 때마다 셀룰러 업로드 | 배터리가 하루를 못 감 | 업로드마다 promotion + tail 고정비 | 배치 + piggyback + quota (3.4절) |
| 응답 유실 시 append 재전송 | 서버 파일에 chunk 중복, SHA 불일치 | 비멱등 요청 | offset 지정 PATCH, 409로 offset 동기화 |
| 업로드 생성 재시도 | 같은 파일의 업로드 세션이 여러 개 | 생성 요청에 idempotency key 없음 | 기기 ID + SHA-256을 key로 |
| 폰이 받으면 기기에서 삭제 | 가끔 데이터가 영영 안 옴 | 폰 앱이 서버 업로드 전에 종료 | 서버 ACK(또는 폰 durable 저장) 후 삭제 |
| jitter 없는 backoff, 고정 업로드 시각 | 서버 장애 후 복구 직후 다시 장애, 매일 자정 부하 스파이크 | fleet 동기화 | full jitter, 스케줄 창 jitter, Retry-After |
| CRC 다항식 · 초기값 불일치 | 기기는 OK인데 서버가 모든 chunk 거부 | CRC 변형이 수십 가지 | `"123456789"` check 값을 양쪽 단위 테스트에 |
| struct를 그대로 전송 | 컴파일러 · 아키텍처가 바뀌자 서버 파싱 실패 | padding · endianness | 명시적 직렬화 (4.6절 C 예제) |
| 저장 공간 부족 시 조용히 덮어씀 | 학습 데이터가 특정 시간대에만 비어 있음 | dropped를 세지 않음 | DROPPED 상태 + 카운터를 메트릭으로 (7.2절) |
| RTC 리셋 후 TLS 실패 | 배터리 방전 뒤 기기가 서버에 못 붙음 | 인증서 유효기간 검사에 1970년 시각 | 마지막 알던 시각을 flash에 저장, 시간 동기 후 재시도 |

---

## 10. 면접에서 이렇게 말한다

**Q.** "Compute the BLE throughput for 2M PHY, 30 ms connection interval, DLE enabled, MTU 247."

**A.** MTU 247이면 notification 하나에 244 B가 실리고, ATT 3 B + L2CAP 4 B를 더하면 251 B로 DLE 최대 LL payload를 꽉 채운다. 2M에서 251 B 패킷은 (2 + 4 + 2 + 251 + 3) B × 4 µs = 1048 µs, 상대의 빈 패킷 44 µs와 T_IFS 두 번 300 µs를 더하면 쌍 하나가 1392 µs다. 30 ms에 21쌍이 들어가서 21 × 244 B × 8 / 30 ms ≈ 1.37 Mbit/s가 이론 상한이다. 실제로는 central과 컨트롤러가 event당 패킷 수를 제한한다. 예를 들어 6개면 약 390 kbit/s다. 그래서 sniffer로 협상된 값과 event당 패킷 수를 먼저 재고, 이 계산과 비교한다.

> "With MTU 247, each notification carries 244 bytes of payload; add the 3-byte ATT header and 4-byte L2CAP header and you get exactly 251 bytes, the maximum LL payload with data length extension. On the 2M PHY that packet is 262 bytes on air at 4 microseconds per byte, 1048 microseconds, plus a 44-microsecond empty packet from the central and two 150-microsecond inter-frame spaces — 1392 microseconds per exchange. Twenty-one of those fit in 30 milliseconds, so the ideal ceiling is about 1.37 megabits per second. In practice the central and the controllers limit packets per connection event; at six packets per event you get around 390 kilobits. So I'd sniff the link to confirm the negotiated PHY, DLE and MTU, count packets per event, and compare against this model."

**Q.** "How do you make uploads resumable and idempotent?"

**A.** 파일을 chunk로 나누고, 진실은 서버의 offset으로 둔다. 생성 요청에는 기기 ID + 파일 SHA-256으로 만든 idempotency key를 붙여서 재시도해도 같은 업로드 id를 받는다. chunk는 `Upload-Offset`을 같이 보내고, 서버는 자기 offset과 같을 때만 붙인다. 응답이 사라져서 같은 chunk가 다시 오면 409와 현재 offset을 돌려주고 클라이언트는 거기로 점프한다. 재부팅 후에는 HEAD로 offset을 묻는다. chunk마다 CRC-32, 마지막에 서버가 SHA-256을 검증하고 durable하게 저장한 뒤 ACK하고, 기기는 그 ACK 후에만 지운다. tus 프로토콜이나 S3 multipart와 같은 개념이다.

> "I make the server's committed offset the source of truth. Creating an upload carries an idempotency key — device ID plus the file's SHA-256 — so a retried create returns the same upload ID. Each chunk is sent with its expected offset, and the server only appends if it matches; if an acknowledgment was lost and the same chunk comes back, the server answers 409 with its current offset and the client jumps ahead. After a reboot the client just asks the server for the offset. Every chunk has a CRC-32, the server verifies the whole-file SHA-256 and persists it before acknowledging, and the device deletes the file only after that acknowledgment. It's the same idea as the tus protocol or S3 multipart uploads."

**Q.** "Why batch uploads on cellular?"

**A.** 셀룰러 업로드의 에너지는 대부분 데이터가 아니라 고정비다: IDLE에서 CONNECTED로의 promotion, TLS 핸드셰이크 왕복, 그리고 전송 후 inactivity timer 동안 고전력에 머무는 tail. 측정 연구들은 LTE tail이 수 초에 걸쳐 수백 mW – 1 W 자릿수라고 보고한다. 내 가정 모델로 업로드 1회 고정비가 약 3.6 J인데 1 KB를 보내면 바이트당 3.6 mJ, 10 MB를 모아 보내면 2.3 µJ다. 하루 3 MB를 1분마다 보내면 300 mAh 배터리의 100 % 이상, 1시간마다면 2 % 정도다. 그래서 지연 허용치만큼 모으고, radio가 이미 깨어 있을 때 piggyback하고, 급한 것(crash)만 바로 보낸다.

> "Because the energy is dominated by fixed costs, not bytes: promoting the radio from idle to connected, the TCP and TLS round trips, and the tail — the modem stays in a high-power state for seconds after the last packet until the network's inactivity timer expires. With a rough model of about 3.6 joules of fixed cost per upload, sending one kilobyte costs milli-joules per byte, while a 10-megabyte batch is a couple of micro-joules per byte. For 3 megabytes a day, uploading every minute would exceed a small wearable battery; hourly is around two percent. So I batch up to the latency the data can tolerate, piggyback on moments when the radio is already awake, and only send urgent things like crash dumps immediately."

**Q.** "What framing and CRC would you use over UART or a BLE serial link?"

**A.** 원시 UART에는 COBS framing을 쓴다. `0x00`을 구분자로 써서 중간부터 들어도 다음 0에서 재동기하고, 오버헤드가 254 B당 1 B로 상한이 있어서 버퍼를 정적으로 잡을 수 있다. SLIP은 escape 때문에 최악에 2배가 된다. 프레임에는 type, sequence number, 길이와 CRC-16 이상을 넣고, 노이즈가 있거나 프레임이 크면 CRC-32. 파일 단위로는 CRC-32와 SHA-256. 하위 계층이 신뢰성을 주지 않으면 sequence + ACK에 window를 bandwidth-delay product만큼 둔다. BLE는 링크 계층이 이미 CRC와 재전송을 하므로 같은 연결 안에서 ARQ를 또 만들지 않고, 재연결 후 offset 재개만 응용에서 한다.

> "On a raw UART I'd use COBS: zero is the delimiter, so a receiver that starts mid-stream resynchronizes at the next zero, and the overhead is bounded — one byte per 254 — so buffers can be statically sized, unlike SLIP which can double in the worst case. Each frame gets a type, a sequence number, a length and at least a CRC-16, or CRC-32 for larger frames or noisy links, and files additionally get a CRC-32 and a SHA-256 end to end. If the lower layer isn't reliable I add acknowledgments with a sliding window sized to the bandwidth-delay product. Over BLE the link layer already does CRC and retransmission, so I don't duplicate ARQ inside a connection — I only handle resuming from an offset after a reconnect."

**Q.** "Design the upload scheduler for a wearable."

**A.** 데이터를 클래스로 나눈다 — crash dump, 메트릭, raw 센서. 클래스마다 우선순위, 허용 링크, 지연 허용치를 정한다. raw는 충전 중 + unmetered Wi-Fi + 온도 OK일 때만. 메트릭은 폰 BLE가 있으면 거기로, 셀룰러면 시간 단위로 모아서, 하루 quota 안에서. crash는 즉시. 셀룰러는 이미 깨어 있으면 piggyback. 업로드 창에 jitter, 실패에 full-jitter backoff, 서버의 Retry-After와 원격 정책을 따른다. 상태는 flash에 저장해서 재부팅 후 재개하고, ACK 후에만 지운다. flash가 차면 raw부터 오래된 것부터 버리되 dropped 바이트를 메트릭으로 보고한다 — 그 숫자가 flash 크기나 샘플링 정책을 정하는 근거가 된다.

> "I'd classify data — crash dumps, metrics, raw sensor captures — and give each class a priority, allowed links and a latency budget. Raw data only goes up while charging on unmetered Wi-Fi and below a thermal limit. Metrics go over the phone's BLE link when it's there, otherwise batched hourly over cellular within a daily quota. Crash dumps go immediately over whatever is available, and if the modem is already awake we piggyback the queue. Upload windows are jittered across the fleet, failures use exponential backoff with full jitter, and the server can throttle with Retry-After or change the policy remotely. Upload state lives in flash so it resumes after a reboot, nothing is deleted before the server acknowledges it, and when storage fills we drop the lowest-priority oldest data but count and report it — that number drives decisions about flash size or sampling."

**Q.** "Your BLE uploads are 5× slower than expected on some phones. How do you debug it?"

**A.** 먼저 계층별로 실제 협상 값을 본다: sniffer나 HCI 로그로 PHY, connection interval, DLE, MTU. 다음으로 event당 패킷 수와 재전송률을 센다. 계산기로 이론값을 내고, 차이가 협상(낮은 MTU · DLE 없음 · 긴 interval) 때문인지, central의 event 길이 제한 때문인지, 재전송(간섭 · coex) 때문인지, 우리 펌웨어가 큐를 못 채우는 것인지 가른다. 마지막 것은 tx complete 이벤트 사이의 공백으로 보인다. 폰 쪽 동시 부하(BT 오디오, Wi-Fi)도 재현 조건에 넣는다.

> "I'd go layer by layer. First, sniff or pull HCI logs to see what was actually negotiated — PHY, interval, data length and MTU. Then count packets per connection event and the retransmission rate. With a throughput model I can tell whether the gap comes from negotiation, from the central limiting event length, from retransmissions due to interference or coexistence, or from our own firmware not keeping the transmit queue full — which shows up as idle gaps between transmit-complete events. And I'd reproduce with the phone also streaming Bluetooth audio or downloading over Wi-Fi, since that's when the event length usually shrinks."

---

## 11. 직접 해보기

1. **손계산**: 1M PHY, interval 15 ms, DLE 251, MTU 247, 제한 없음. 한 쌍의 시간, event당 쌍 수, 응용 throughput은? — 정답: 쌍 2468 µs, floor(15000 / 2468) = 6쌍, 6 × 244 × 8 / 15 ms ≈ 781 kbit/s.
2. **손계산**: 같은 링크를 암호화하면(MIC 4 B) 2M · 30 ms · MTU 247 · 제한 없음에서 몇 kbit/s인가? — 정답: data 패킷 1048 + 16 = 1064 µs, 쌍 1408 µs, 21쌍 그대로 → 1366 kbit/s (이 interval에서는 floor가 안 바뀜). 계산기에 `mic=True`로 확인해 보라.
3. **코드**: `throughput_kbps`에 Coded S=2 PHY를 추가하라 (FEC block 1은 S=8과 같은 376 µs, 그 뒤는 bit당 2 µs, TERM2는 6 µs). — 힌트: `376 + (2 + n + 3) × 16 + 6`.
4. **손계산**: 셀룰러 고정비 3.6 J, 바이트당 1.9 µJ일 때 바이트당 에너지가 바닥의 2배가 되는 배치 크기는? — 정답: 고정비 / 배치 = 1.9 µJ/B → 배치 ≈ 1.9 MB.
5. **코드**: 4.9절 시뮬레이션에 "equal jitter"(`d/2 + uniform(0, d/2)`)와 "decorrelated jitter"를 추가해 총 요청과 완료 시간을 비교하라. — 힌트: Brooker의 글에서 정의를 확인. full jitter와 비슷하거나 약간 다른 trade-off가 나온다.
6. **코드**: 데모 서버에 업로드 만료(마지막 PATCH 후 N초 지나면 삭제)와 `Retry-After`를 주는 503 모드를 넣고, 클라이언트가 그것을 따르게 고쳐라. — 힌트: 클라이언트의 `req()`에서 503이면 헤더 값만큼 sleep 후 HEAD로 offset부터 다시.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| goodput | 응용 계층 실효 throughput | 헤더 · 재전송을 뺀, 응용이 실제로 받은 바이트/초 |
| connection interval | BLE 연결의 anchor 간격 | 7.5 ms – 4 s, 1.25 ms 단위, central이 정함 |
| connection event | anchor에서 시작하는 주고받기 묶음 | 몇 쌍을 넣을지는 컨트롤러 의존 |
| T_IFS | inter frame space | 같은 event 안 패킷 사이 150 µs |
| PHY (1M / 2M / Coded) | BLE 물리 계층 모드 | 1 µs/bit, 0.5 µs/bit, 거리용 FEC |
| DLE | Data Length Extension | LL payload 상한을 27 B에서 251 B로 |
| ATT_MTU | ATT PDU 최대 크기 | notification 응용 데이터 = MTU − 3 |
| notification / write without response | 확인 없는 GATT 전송 | 대량 전송용. LL이 연결 중 신뢰성 보장 |
| fragmentation | 상위 PDU를 여러 LL 패킷으로 나눔 | 조각마다 오버헤드를 다시 냄 |
| RRC | Radio Resource Control | LTE/5G 모뎀의 IDLE · CONNECTED 상태 기계 |
| tail energy | 전송 후 고전력 유지 에너지 | inactivity timer 동안. 망 설정 의존 |
| PSM / TWT | Wi-Fi 절전 모드 / Target Wake Time | AP가 버퍼링, 정해진 때만 깨어남 |
| session resumption | 이전 TLS 세션 재사용 | 인증서 검증 · 서명 생략, 0-RTT는 replay 주의 |
| QUIC | UDP 기반 전송 + TLS 1.3 | 1-RTT 연결, 0-RTT 재개, connection migration |
| chunk | 업로드 단위 조각 | 재개 · CRC 단위 |
| resumable upload | 끊긴 지점부터 이어 올리기 | 서버 offset이 진실 (tus, S3 multipart, GCS) |
| idempotency key | 요청 중복 판별용 키 | 같은 key면 같은 결과를 돌려줌 |
| at-least-once + dedup | 최소 한 번 전달 + 중복 제거 | 사실상 exactly-once 효과 |
| end-to-end argument | 끝과 끝에서 검사해야 한다는 설계 원칙 | 구간별 CRC만으로는 부족 |
| exponential backoff | 실패마다 대기 2배 | 상한 필요 |
| full jitter | 대기를 0 – 상한 사이 균등 무작위로 | thundering herd 해소 |
| thundering herd | 동기화된 대량 재시도 | 복구 직후 서버를 다시 쓰러뜨림 |
| COBS | Consistent Overhead Byte Stuffing | 0x00 제거, 오버헤드 상한 작음 |
| SLIP | Serial Line IP framing (RFC 1055) | 0xC0 구분자 + escape, 최악 2배 |
| ARQ | Automatic Repeat reQuest | seq + ACK + 재전송 |
| Go-Back-N / Selective Repeat | sliding window ARQ 두 방식 | 전부 재전송 / 빠진 것만 재전송 |
| bandwidth-delay product | 대역폭 × RTT | in-flight로 둬야 링크가 안 쉬는 양 |
| pre-signed URL | 서버가 서명한 짧은 수명 업로드 URL | 기기에 저장소 자격 증명을 두지 않음 |
| mTLS | mutual TLS | 서버도 기기 인증서를 검증 |
| piggyback | 이미 깨어 있는 radio에 얹어 보내기 | 고정비 회피 |

---

## 13. 요약 & 체크리스트

기기 → 서버 전송은 "파이프 용량 × 파이프 비용 × 프로토콜의 견고함 × 언제 보낼지"의 문제다. BLE 응용 throughput은 PHY airtime, T_IFS, 헤더(ATT 3 + L2CAP 4 + LL), DLE · MTU 정렬, 그리고 무엇보다 **event당 패킷 수**로 정해진다 — 2M에서 이상적 상한은 약 1.4 Mbit/s이지만 실제는 central과 컨트롤러가 정하므로 측정한다. Wi-Fi와 셀룰러는 promotion · 핸드셰이크 · tail이라는 고정비가 커서 **배치**가 배터리를 몇 배에서 수백 배 바꾼다. 업로드 프로토콜은 서버 offset을 진실로 두는 chunk 재개, idempotency key, chunk CRC + 파일 SHA-256, ACK 후 삭제, full-jitter backoff로 "끊겨도 · 중복돼도 · 깨져도" 안전하게 만든다. 원시 바이트 스트림에는 COBS + CRC + (필요하면) sliding window ARQ를, 신뢰성 있는 링크(BLE · TCP) 위에는 재연결 후 재개만 얹는다. 마지막으로 스케줄러가 클래스별 우선순위 · 링크 · 충전 · quota · 서버 지시로 "언제 무엇을"을 정하고, 버린 데이터는 반드시 센다.

- [ ] BLE 2M · 30 ms · DLE · MTU 247의 이론 throughput을 손으로 유도할 수 있다 (1392 µs, 21쌍, 1.37 Mbit/s)
- [ ] MTU 247이 왜 "딱 맞는" 값이고 MTU 251이 왜 느린지 설명할 수 있다
- [ ] event당 패킷 수 제한, central(iOS/Android) 제약, coex가 실제 throughput을 정한다는 것을 hedge와 함께 말할 수 있다
- [ ] 셀룰러 tail energy와 배치 크기 → 바이트당 에너지 곡선을 그리고 꺾이는 지점을 계산할 수 있다
- [ ] TCP · TLS 1.2 · TLS 1.3 · resumption · 0-RTT · QUIC의 핸드셰이크 RTT 수와 0-RTT replay 위험을 말할 수 있다
- [ ] offset 기반 재개 + idempotency key + chunk CRC + SHA-256 + ACK 후 삭제 업로드를 설계하고 로컬에서 돌려 볼 수 있다
- [ ] exponential backoff에 jitter가 왜 필요한지 시뮬레이션 결과로 설명할 수 있다
- [ ] 길이 prefix · SLIP · COBS의 오버헤드와 재동기 특성을 비교하고 CRC 비트 수를 고를 수 있다
- [ ] stop-and-wait와 sliding window의 효율을 bandwidth-delay product로 설명할 수 있다
- [ ] 웨어러블 업로드 스케줄러(우선순위, 링크, 충전, quota, jitter, dropped 보고)를 설계할 수 있다

## 참고 자료

- Bluetooth SIG, "Bluetooth Core Specification" (v5.x) — Vol 6 Part B(Link Layer: 패킷 포맷, connection interval, DLE), Vol 3 Part A(L2CAP), Part F(ATT) — [bluetooth.com/specifications](https://www.bluetooth.com/specifications/specs/)
- Apple, "Accessory Design Guidelines for Apple Devices" — BLE connection parameter 권장 범위 (developer.apple.com, 최신 버전 확인)
- Android Developers — `BluetoothGatt.requestMtu`, `requestConnectionPriority`, WorkManager — [developer.android.com](https://developer.android.com)
- J. Huang et al., "A Close Examination of Performance and Power Characteristics of 4G LTE Networks", MobiSys 2012 — LTE RRC tail energy
- RFC 8446 (TLS 1.3), RFC 9000 (QUIC), RFC 9114 (HTTP/3), RFC 6066 (TLS extensions: max_fragment_length), RFC 1055 (SLIP) — [rfc-editor.org](https://www.rfc-editor.org)
- tus resumable upload protocol — [tus.io](https://tus.io)
- Amazon S3 User Guide, "Uploading and copying objects using multipart upload" — [docs.aws.amazon.com](https://docs.aws.amazon.com/AmazonS3/latest/userguide/mpuoverview.html)
- Google Cloud Storage, "Resumable uploads" — [cloud.google.com/storage/docs/resumable-uploads](https://cloud.google.com/storage/docs/resumable-uploads)
- M. Brooker, "Exponential Backoff And Jitter", AWS Architecture Blog, 2015
- S. Cheshire, M. Baker, "Consistent Overhead Byte Stuffing", IEEE/ACM Transactions on Networking, 1999 (SIGCOMM '97)
- P. Koopman, CRC polynomial zoo — [users.ece.cmu.edu/~koopman/crc](https://users.ece.cmu.edu/~koopman/crc/)
- J. H. Saltzer, D. P. Reed, D. D. Clark, "End-to-End Arguments in System Design", ACM TOCS, 1984
- J. Kurose, K. Ross, "Computer Networking: A Top-Down Approach" — 3장 (stop-and-wait, Go-Back-N, Selective Repeat)
- 이웃 노트: D7 (무선 에너지/바이트, 오프로드 교차점), E8 (COBS + CRC-16, 무선 칩 · coex), G7 (BLE 시각 동기화 hedge), H1 (로그 포맷), H3 (백엔드 ingestion), H6 (프라이버시 · 암호화), H8 (fleet 수집 정책)
