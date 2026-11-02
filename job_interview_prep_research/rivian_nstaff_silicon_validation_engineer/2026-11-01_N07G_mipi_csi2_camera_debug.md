# N07G · MIPI CSI-2 카메라 파이프라인 디버그
<!-- theme: pastel -->
> **묶음**: N07 인터페이스 디버그 · **우선**: P0 ★ (면접관 Manjiang: Baidu Apollo 다중 카메라·센서 timestamp/sync 특허) · **목록**: [N07 인덱스](2026-11-01_N07_interface_debug_scenarios.html) · **이전/다음**: [N07F Ethernet](2026-11-01_N07F_ethernet_debug.html) / [N07H 모의 드릴](2026-11-01_N07H_mock_debug_drills.html) · **관련**: [LP00A §5 MIPI 기초](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), [N06E E3 CSI-2 플랜, E4 시간 동기](2026-11-01_N06E_plans_highspeed_io_sensor.html), [BV10 §2.7, 체인 D](2026-11-01_BV10_board_interface_tests.html), [V9 패킷 파서](hmScreenPrep/problems/V9_csi2_packet_parse/index.html), [V12 timestamp 체커](hmScreenPrep/problems/V12_frame_timestamp_check/index.html), [L4 PPS](hmScreenPrep/problems/L4_pps_time_discipline/index.html), [deck §7 #8, #13](2026-10-25_rivian_hm_screen_prep.html) · **코드**: onsitePrep/n07/g/

## 0. TL;DR

- **이 페이지로 답할 것**: "The camera stream drops frames and the RX reports ECC errors — debug it." 를 **센서 → SerDes → D-PHY → CSI-2 RX → ISP → DDR** 경로를 따라 **"어느 구간에서 프레임이 사라지나"** 로 좁히는 법. 시나리오 G1–G10 (프레임 0개 · SoT · 온도 ECC · 대각선 찢어짐 · 색 · 4대 drop · VC 섞임 · FE 누락 · **카메라 간 timestamp 1 프레임** · 15 fps).
- 기초(D-PHY 레인, 패킷 구조, V4L2)는 [LP00A §5](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), 테스트 플랜·에러 종류·ECC 3-bit 오정정 실험은 [N06E E3](2026-11-01_N06E_plans_highspeed_io_sensor.html), 패킷 단위 ECC/CRC 코드는 [V9](hmScreenPrep/problems/V9_csi2_packet_parse/index.html). 여기선 **증상 → 구간 → 증거**와 **스트림 단위 진단 코드**.
- **외울 것 ①** 구간마다 "**받은 수 vs 보낸 수**" 카운터를 맞춰 본다: 센서 frame# → (deser lock·에러) → RX FS/FE 수·packet count → line count·WC → DMA DONE 수 → V4L2 `sequence` gap. **숫자가 처음 어긋나는 구간이 범인**.
- **외울 것 ②** 에러 종류가 계층을 말해 준다: **SoT/SoT sync = PHY 진입(settle·레인·클럭 모드)** · **ECC1 증가 = 신호 마진 경고(아직 동작)** · **ECC2/CRC = 실제 손상** · **frame sync error = FS/FE 구조(FE 손실·VC 충돌)** · **line buffer full = 하류(DDR/ISP) 역압** · **에러 0인데 이상 = 설정(WC·DT·Bayer·stride)**.
- **외울 것 ③** D-PHY HS 진입: **LP-11 → LP-01 → LP-00 → HS-0 → sync byte → 데이터 → HS-trail → LP-11**. 수신기 **THS-SETTLE 창 = 85 ns + 6 UI ~ 145 ns + 10 UI** [2] — 레인 속도가 바뀌면 settle 값도 다시 계산해야 한다 (G2 단골 버그).
- **★ 면접관 포인트**: 다중 카메라 동기 = "**언제의 timestamp인가(노출 시작/SoF/EoF)**" 정의 → 하나의 PTP 시계 → FSYNC → **LED 스트로브 물리 기준으로 검증**. 1 프레임 정확히 어긋남은 **이산(시퀀스·파이프라인) 문제**, 점점 커지면 **free-run drift** (G9).
- **30초 영어 답 (헤드라인)**: "First I'd find *where* frames disappear, by comparing counts stage by stage: frame numbers from the sensor, frame-start and frame-end counts and packet counts at the receiver, and buffers completed by the DMA. ECC errors tell me the layer: rising single-bit corrections mean the physical link is losing margin, two-bit and CRC errors mean real corruption, and SoT errors point at the HS entry — settle time or lane setup. If the receiver is error-free but buffers are still missing and the line buffer reports full, it's downstream bandwidth, not the link. Then I'd turn one knob at a time — lane rate, settle value, temperature, number of cameras — and add the failing condition to the regression with the counters as pass criteria."

## 1. 🔰 1분 설명

- **비유 (택배 물류 센터)**: 카메라 공장(센서)이 사진을 **한 줄씩 상자**에 담아 보낸다. 먼 거리는 **고속도로 트럭**(SerDes: GMSL/FPD-Link, 동축 케이블 몇 m)이 나르고, 물류 센터 정문(D-PHY)에서 **차단기를 올리는 시간(HS settle)** 이 틀리면 첫 상자를 놓친다. 정문 안 분류기(CSI-2 RX)는 상자 **라벨**(VC = 어느 공장, DT = 무슨 물건, WC = 크기)을 읽고 ECC로 라벨 오타를 고치고, CRC로 내용물 파손을 확인한 뒤, **"사진 시작(FS)·끝(FE)" 팻말** 사이의 상자들을 한 장으로 모아 **창고 선반(DDR)** 에 넣는다. 현상소(ISP)가 원본(RAW Bayer)을 컬러 사진으로 만든다. **선반이 꽉 차면(line buffer full)** 정문이 멀쩡해도 상자를 버린다.
- **정확한 정의**: MIPI CSI-2 = 카메라 센서 → 프로세서 영상 프로토콜, 최신 v4.2(2025-12), **최대 32 VC**, PHY는 짧은 거리 D-PHY/C-PHY, 장거리 A-PHY(15 m) [6]. 차량은 보통 센서 옆 **serializer** + SoC 보드 쪽 **deserializer**가 CSI-2를 장거리 링크로 바꿨다가 다시 CSI-2로 내준다 — 공개 부품 예: TI DS90UB960-Q1(FPD-Link III 입력 4개 × 4.16 Gb/s → CSI-2 포트 2개, 레인당 최대 1.6 Gb/s, 역방향 I2C/GPIO, frame sync 생성기, line-fault 감지) [9], ADI MAX96712(GMSL2 입력 4개, 정방향 3/6 Gb/s, 역방향 187.5 Mb/s, CSI-2 D-PHY/C-PHY 출력) [10]. **Rivian이 어느 SerDes를 쓰는지는 공개 확인 안 됨** [추정].

```text
  카메라 모듈 ×N (차 밖, 수 m)              컴퓨트 보드                         SoC
 ┌────────────────────────┐ coax(+PoC) ┌───────────────────┐ CSI-2 D-PHY ┌──────────────────────────────────────────────┐
 │ 이미지 센서 ─CSI-2─ SER ├═══════════►│ DESER (4 in)      ├────────────►│ D-PHY RX ─► CSI-2 RX ─► VC/DT 라우팅 ─► ISP ─► DMA ─► DDR
 │   ▲ I2C(CCI)    ▲ FSYNC │◄═ 역방향 ══│ lock/에러 카운터   │ 1 clk+4 data│ (settle,   (ECC, CRC,     (line buffer)  (RAW→YUV) (frame
 │   │             │       │  I2C/GPIO  │ VC 매핑, TPG      │ lane, 수 cm │  deskew,    FS/FE 짝, WC,   ↑ full?                 buffer)
 └───┼─────────────┼───────┘            └──▲────────────▲───┘             │  LP/HS)     packet/line 카운트)                       │
     └─ (터널된 I2C) ┘                        │ I2C         │ FSYNC in      │ SoF timestamp latch ◄── PHC (PTP) ──► FSYNC out  │
                                     SoC I2C master ─────┘   PHC out ────┘ └──────────────────────────────────────────────┘
  계층:  [센서 설정] [SerDes 링크]           [deser 출력 = CSI-2 TX]      [PHY]       [패킷/프레임]          [하류: 대역폭·버퍼]
```

| 용어 | 풀네임 | 쉬운 설명 | 디버그에서 왜 중요 |
|---|---|---|---|
| D-PHY | MIPI D-PHY | 클럭 레인 1 + 데이터 레인 1–4, 차동쌍 | LP/HS 전환·settle이 단골 |
| C-PHY | MIPI C-PHY | 3선 trio, 클럭 내장 | 헤더를 ECC 대신 다중 사본+CRC로 보호 [1] |
| HS / LP | High-Speed / Low-Power | 약 200 mV 차동 고속 / 1.2 V 단일 종단 저속(≤10 Mb/s) [8] | 전환 시퀀스가 SoT 에러의 원인 |
| LP-11 (stop state) | 두 선 모두 high | 정지·대기 상태 | 수신기에 따라 enable 전에 LP-11을 봐야 동기 [3] |
| THS-SETTLE | HS settle | LP→HS 직후 수신기가 신호를 무시하는 시간 | 틀리면 SoT/첫 바이트 에러 |
| SoT / sync byte | Start of Transmission | HS burst 시작 동기 패턴 | SoT error / SoT sync error |
| continuous / non-continuous clock | 클럭 레인 모드 | 클럭을 계속 켬 / 줄 사이 LP로 내림 | 양쪽 설정 불일치 = 프레임 0 또는 간헐 |
| VC / DT / WC | Virtual Channel / Data Type / Word Count | 어느 스트림 / 무슨 형식 / payload 바이트 | VC 충돌, unpack, stride |
| ECC (헤더) | 6-bit Hamming SEC-DED | 1비트 정정, 2비트 검출 | ECC1 = 마진 경고, ECC2 = 패킷 손실 |
| CRC-16 | payload 검사 | 정정 X, 재전송 X | 줄 손상 → frame level error |
| FS / FE / LS / LE | Frame/Line Start/End | short packet 팻말 (frame 번호 포함) | FE 손실 → 프레임 병합 (G8) |
| frame sync error | FS–FE 짝 불일치 | 구조 깨짐 | FE 손실·VC 충돌 |
| line buffer full | RX 출력 버퍼 넘침 | 하류가 못 따라감 | DDR/QoS 문제 (G6) |
| SER / DESER | serializer / deserializer | 장거리 링크 변환기 | lock·링크 에러 = 별도 구간 |
| PoC | Power over Coax | 같은 동축으로 카메라 전원 | 고온·긴 케이블에서 전압 강하 [추정] |
| FSYNC | frame sync trigger | 여러 카메라 동시 노출 펄스 | 다중 카메라 skew (G9) |
| TPG | Test Pattern Generator | 센서·deser·RX 안의 패턴 생성기 | 구간 자르기의 핵심 도구 |
| ISP | Image Signal Processor | RAW Bayer → YUV/RGB | 색 이상(G5)·throughput |
| V4L2 sequence | v4l2_buffer.sequence | 버퍼마다 증가하는 프레임 번호, drop 포함 [5] | 앱이 보는 drop 증거 |

## 2. 정상 기준선

### 2.1 D-PHY HS burst 하나 — 데이터 레인과 클럭 레인

```text
 데이터 레인 (Dp/Dn 단일 종단 전압으로 본 그림, 1.2 V LP / ~200 mV HS 차동) [8]
        LP-11      LP-01    LP-00          HS-0 (zero)      sync     payload (packet bytes)       HS-trail   LP-11
 Dp ─────────────┐        ┌─────────┐
                 │        │         └───┐  ┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐   ┌──────────────
 Dn ─────────────┼────────┘             │  ...  HS 차동 (수백 Mb/s – 수 Gb/s)  ...   └───┘
                 │← TLPX →│← THS-PREPARE →│← THS-ZERO →│0xB8│ DI WC ECC  payload  CRC │←THS-TRAIL→│← THS-EXIT →
                 HS 요청    bridge          수신기 종단 ON, THS-SETTLE 동안 무시  │                       │
                                            └─────────── THS-SETTLE ───────────┘ 이후 sync 탐색
 클럭 레인: LP-11 → LP-01 → LP-00 → (TCLK-PREPARE) → HS-0 (TCLK-ZERO) → 클럭 토글 시작
            → 데이터 레인 HS 진입 전 TCLK-PRE 이상 먼저 토글 → 데이터 끝난 뒤 TCLK-POST 이상 더 토글 → TCLK-TRAIL → LP-11
 continuous clock 모드: 클럭 레인은 스트리밍 내내 HS 토글 (LP로 안 내려감)
```

- 시퀀스: 수신기는 **LP-11 → LP-01 → LP-00** 을 보고 HS 수신 모드로 들어가고, **leader(sync) 시퀀스**를 찾으면 바이트를 상위로 넘기다가 **LP-11(stop)** 을 보면 burst 끝 [7]. sync byte는 벤더 문서에서 **0xB8**로 표기 [12] (전송 비트열 00011101, LSB 먼저) [추정: 1차 MIPI 문서 미확인].
- 커널 문서: 송신기는 HS로 가기 전 LP-11을 **100 µs만큼 짧게** 거칠 수 있고, 일부 수신기는 스트리밍 시작 전에 LP-11을 직접 봐야 해서 `.pre_streamon()` 으로 센서를 LP-11에 먼저 두는 콜백이 있다. 전원 직후 상태는 **LP-00** [3]. → 순서가 틀리면 G1(프레임 0).

### 2.2 타이밍 숫자 (Linux `phy-mipi-dphy.h` 의 D-PHY 스펙 값) [2]

| 파라미터 | 의미 | 범위 |
|---|---|---|
| TLPX | LP 상태 하나의 길이 | ≥ 50 ns |
| THS-PREPARE | LP-00 유지 (HS-0 전) | 40 ns + 4 UI ~ 85 ns + 6 UI |
| THS-PREPARE + THS-ZERO | sync 전 HS-0 총 시간 | ≥ 145 ns + 10 UI [11] |
| **THS-SETTLE** | 수신기가 LP→HS 직후 무시하는 시간 | **85 ns + 6 UI ~ 145 ns + 10 UI** |
| THS-TRAIL | 마지막 비트 뒤 반전 유지 | ≥ max(8 UI, 60 ns + 4 UI) |
| THS-EXIT | burst 사이 LP-11 최소 | ≥ 100 ns |
| TCLK-PREPARE | 클럭 LP-00 | 38 ~ 95 ns |
| TCLK-SETTLE | 클럭 레인 settle | 95 ~ 300 ns |
| TCLK-PRE | 데이터 HS 전 클럭 선행 | ≥ 8 UI |
| TCLK-POST | 데이터 HS 끝난 뒤 클럭 유지 | ≥ 60 ns + 52 UI |
| TCLK-TRAIL / TCLK-MISS | 클럭 종료 / 수신기가 클럭 없음 감지 | ≥ 60 ns / ≤ 60 ns |
| TINIT | 초기화 | ≥ 100 µs |

**[계산] 레인 속도별 settle 창** (UI = 1/비트율, 위 공식 그대로; "byte-clk" 열은 settle 카운터를 바이트 클럭 = 8 UI 단위로 센다고 가정한 예시 [추정: 단위는 IP마다 다름]):

```text
  80 Mb/s UI 12.500 ns | settle 160.0-270.0 ns mid 215.0 | byteclk 100.00 ns -> settle  1.6- 2.7 count
 500 Mb/s UI  2.000 ns | settle  97.0-165.0 ns mid 131.0 | byteclk  16.00 ns -> settle  6.1-10.3 count
1000 Mb/s UI  1.000 ns | settle  91.0-155.0 ns mid 123.0 | byteclk   8.00 ns -> settle 11.4-19.4 count
1500 Mb/s UI  0.667 ns | settle  89.0-151.7 ns mid 120.3 | byteclk   5.33 ns -> settle 16.7-28.4 count
2500 Mb/s UI  0.400 ns | settle  87.4-149.0 ns mid 118.2 | byteclk   3.20 ns -> settle 27.3-46.6 count
```

- 읽는 법: ns로는 창이 거의 같지만(약 90–150 ns), **카운터 단위로는 레인 속도에 비례** → 1.5 Gb/s용 값 22를 80 Mb/s 모드(binning·저속 모드)에서 그대로 쓰면 2.2 µs = 창 밖 → **SoT 에러만 증가**(G2). 80 Mb/s에선 정수 하나(2)만 창 안 — 저속 모드가 오히려 까다롭다.
- 실제 드라이버 예: NXP `imx-mipi-csis` 는 `hs_settle = (lane_rate − 5 MHz) / 45 MHz` 로 계산하고, debugfs `ths_settle`·`tclk_settle` 로 값을 덮어쓸 수 있다 [4] → settle sweep의 실전 손잡이.

### 2.3 정상 스트림과 정상 카운터

```text
 한 프레임 (VC0, RAW10 1920x1080, 30 fps) — 시간축
 FS(#n) ─ [LONG WC=2400] ─ [LONG] ─ ... 1080개 ... ─ [LONG] ─ FE(#n) ─── vertical blanking ─── FS(#n+1)
 │←──────────────────────────── 33.33 ms (= 1/30 s) ─────────────────────────────────────────────→│
 줄 사이: non-continuous clock이면 매 줄 LP-11로 돌아갔다 다시 HS 진입 (= 매 줄 SoT 한 번)
 정상: FS 수 = FE 수 = DMA DONE 수, 줄 수 = 높이, 모든 WC = 폭 × bpp / 8, frame# +1씩, 간격 = 1/fps
```

- WC 계산: RAW10 = 픽셀 4개당 5바이트 → 1920 × 10 / 8 = **2400 B**, RAW12 1920 = **2880 B**, RAW8 = 폭. 대역폭(Linux): `pixel_rate = link_freq × 2 × lanes × 16 / k / bpp`, D-PHY k = 16, C-PHY k = 7 [3]. 예: 750 MHz × 2 × 4 / 12 = 500 Mpix/s.
- PG232(AMD CSI-2 RX Subsystem)에서 정상이면 [1]: Interrupt Status의 **Frame Received**(FE 수신 시) 만 계속 서고 에러 비트 0 · Lane<n> Information의 **Stop state**는 burst 사이에만 · Core Status **packet count** 증가 · Image Information 1 (VC별) **line count** = 높이, **byte count** = WC.

**[예시 출력]** 정상 캡처 (`v4l2-ctl --stream-mmap --verbose` 류 출력 형태, 실기 없음):

```text
cap dqbuf: 0 seq:      0 bytesused: 4147200 ts: 812.033365 (ts-monotonic, ts-src-eof)
cap dqbuf: 1 seq:      1 bytesused: 4147200 ts: 812.066698 delta: 33.333 ms
cap dqbuf: 2 seq:      2 bytesused: 4147200 ts: 812.100031 delta: 33.333 ms fps: 30.00
$ v4l2-ctl -d /dev/v4l-subdev1 --log-status ; dmesg | tail
imx-mipi-csis 32e30000.mipi-csi: Frame Start 0 events: 300
imx-mipi-csis 32e30000.mipi-csi: Frame End 0 events: 300
```

- `seq` 연속 = 앱까지 drop 0 (V4L2 `sequence` 는 drop된 프레임도 세므로 **gap = drop**) [5]. `ts-src-eof` = timestamp가 **프레임 끝(EoF)** 시점 [5] — G9에서 중요. `bytesused` 4147200 = 1920 × 1080 × 2 (RAW10을 16비트로 풀어 저장).
- `--log-status` 로그의 이벤트 이름은 `imx-mipi-csis` 의 실제 표 이름("Frame Start 0", "SOT 0 Error", "ECC Error", "CRC Error", "Lost Frame End Error 0", "FIFO Overflow Error 0" …), 0인 에러는 출력 안 함 [4]. 장치 주소·숫자는 [예시].

## 3. 관찰 지점 지도

| 무엇 | 어디서 | 도구 | 정상값 | 이상하면 의미 |
|---|---|---|---|---|
| 센서 응답·chip ID | 센서 I2C(CCI) | `i2ctransfer`, 드라이버 probe 로그 | ACK, ID 일치 | 전원/MCLK/리셋/역방향 채널 ([N07B](2026-11-01_N07B_i2c_smbus_debug.html)) |
| 센서 streaming·모드 | 센서 mode/stream 레지스터(개념), PLL | 드라이버 로그, 레지스터 dump | stream on, link freq = DT 값 | 프레임 0, settle 불일치 |
| SerDes lock | deser LOCK 핀/상태, 링크 에러 카운터 | I2C dump, LED/핀 scope | lock 유지, 에러 0 | 링크(케이블·PoC·SerDes 마진) 구간 |
| deser VC 매핑 | deser 입력 포트 → VC 설정 | I2C dump | 입력마다 다른 VC | VC 충돌 (G7) |
| 레인 정지 상태 | PG232 Lane<n> Info **Stop state** [1] | 레지스터 읽기 | burst 사이에만 1 | 계속 1 = TX가 HS로 안 옴 |
| SoT / SoT sync | PG232 Lane<n> Info, ISR **ErrSoTHS / ErrSotSyncHS** [1] | 레지스터, `--log-status` (SOT n Error [4]) | 0 | settle·레인 순서·극성·클럭 |
| 헤더 ECC | ISR **ECC 1-bit / ECC 2-bit** [1] | 레지스터, ECC Error 이벤트 [4] | 0 (1-bit도 0이 정상) | 1-bit 증가 = 마진 경고, 2-bit = 패킷 손실 |
| payload CRC | ISR **CRC error (ErrCrc)** [1] | 레지스터, CRC Error 이벤트 [4] | 0 | 줄 손상 → frame level error |
| DT | ISR **Unsupported Data Type (ErrID)** [1] | 레지스터 | 0 | 센서 DT vs RX 설정 |
| WC | ISR **Word Count corruption** [1], Image Info 1 **byte count** | 레지스터 | WC = 폭 × bpp / 8 | 짧은 패킷, WC 오설정 |
| 레인 수 | ISR **Incorrect lane configuration** [1] | 레지스터 | 0 | TX 레인 > RX 설정 |
| FS/FE 구조 | ISR **Frame synchronization error VCn / Frame level error VCn** [1] | 레지스터, Lost Frame Start/End [4] | 0 | FE 손실, VC 충돌 / CRC 있던 프레임 |
| 패킷·줄 수 | Core Status **packet count**, Image Info 1 **line count** (VC별) [1] | 레지스터 2회 읽어 diff | 증가, 줄 수 = 높이 | 멈춤 = 입력 없음 |
| 버퍼 넘침 | ISR **Stream line buffer full** [1], FIFO Overflow Error [4] | 레지스터 | 0 | 하류 역압 (G6) |
| non-continuous 클럭 FIFO | ISR **Pending write in internal FIFO** (TX의 Tclk-post 늘리라는 의미) [1] | 레지스터 | 0 | 클럭을 너무 빨리 내림 |
| 미디어 그래프 | 엔티티·패드 포맷·링크 | `media-ctl -d /dev/media0 -p` | 양 끝 포맷 일치, ENABLED | `EPIPE`, 포맷 불일치 |
| 앱 프레임 | v4l2_buffer `sequence`, `V4L2_BUF_FLAG_ERROR`, timestamp flag [5] | `v4l2-ctl --stream-mmap --verbose` | seq 연속, ERROR 없음 | gap = drop, ERROR = 손상 버퍼 |
| timestamp | SoF latch(PHC), V4L2 ts (EOF/SOE) | V12형 체커, 스코프 | 주기 = 1/fps, 카메라 간 skew < 예산 | G9 |
| 물리 신호 | 클럭/데이터 레인 LP·HS 전환 | 스코프(고대역 차동 프로브), D-PHY 분석기 | 2.1 그림대로 | 시퀀스·타이밍 위반 |

**첫 5분 증거 스냅샷** (순서대로, 먼저 "어느 구간"을 정한다):

```bash
media-ctl -d /dev/media0 -p                         # 1) 그래프: 링크 ENABLED? 센서·RX·video 패드 포맷 일치?
v4l2-ctl -d /dev/v4l-subdev0 --list-ctrls           # 2) 센서: link_frequency, pixel_rate, test_pattern 메뉴
i2ctransfer -y 1 w2@0x10 0x00 0x00 r2               # 3) 센서 chip ID (16-bit 레지스터 주소 예, 주소·값은 센서마다) [예시]
#    (SerDes면) deser lock / 링크 에러 / VC 매핑 레지스터 dump — 이름·주소는 부품 데이터시트 기준
v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=300 --verbose 2>&1 | tail   # 4) seq gap, ERROR flag, fps
v4l2-ctl -d /dev/v4l-subdev1 --log-status; dmesg | tail -30   # 5) RX 드라이버의 에러/이벤트 카운터 (지원 드라이버만) [4]
#    bare-metal/FPGA면: RX ISR + Lane<n> Info + packet count + Image Info(VC별 line/byte count) 를 1초 간격 2회 읽고 diff [1]
```

- **diff가 핵심**: 카운터는 한 번 읽으면 숫자일 뿐, **1초 간격 두 번** 읽어야 "지금 증가 중"인지 안다. W1C 상태 비트는 읽고 → 기록 → 1을 써서 clear → 다시 관찰.

## 4. 증상 → 원인 한 장 표

| 증상 | 계층 | 의심 원인 (순위) | 첫 확인 | 확정 방법 |
|---|---|---|---|---|
| 프레임 0개, 에러도 0 | 설정/드라이버 | ① 센서 stream on 안 됨 ② RX enable 전에 센서가 먼저 HS 시작(LP-11 못 봄) ③ media 링크 미연결 | Stop state 계속 1? packet count 증가? | 센서 레지스터 dump, 스코프로 데이터 레인 HS 유무 |
| 프레임 0개, SoT sync 에러 다수 | 전기/타이밍 | ① 레인 순서/극성 swap ② settle 크게 틀림 ③ 레인 수 불일치 | 레인별 SoT sync | 1 lane 저속 모드로 줄여 보기, DT `data-lanes`·`lane-polarities` |
| 프레임 0개, deser lock 안 됨 | 전기 (SerDes) | ① 케이블/커넥터 ② PoC 전원 ③ 링크 속도·모드 설정 | deser lock 상태 | deser TPG → SoC: 받으면 RX 정상, 카메라 쪽 링크 문제 |
| SoT 에러만 증가, 영상은 대체로 나옴 | 타이밍 | ① settle 값이 레인 속도와 불일치 ② 레인 skew ③ non-continuous 클럭 TCLK-PRE/POST 부족 | 레인 속도 vs settle 계산값 | settle sweep으로 통과 창 측정 후 중앙 |
| ECC1 정정이 온도·시간 따라 증가 | 전기 (마진) | ① 링크 SI 마진(레인 속도 한계) ② SerDes 링크 마진·PoC ③ 수신 종단·CDR | 레인별·카메라별 분포 | 레인 속도 한 단계↓, 챔버 sweep, eye/BER |
| ECC2·CRC 에러, 줄 단위 깨짐 | 전기 | ① 마진 소진 ② 특정 레인 ③ 크로스토크·EMI | CRC가 특정 레인/카메라만? | TPG로 고정 패턴 → 비트 비교 |
| 이미지가 대각선으로 찢어짐 | 설정 | ① WC(폭·bpp) ≠ DMA stride ② RAW10 폭이 4의 배수 아님 ③ stride 정렬(64 B 등) 미반영 | 수신 WC vs 설정 | WC 로그, 줄 offset 계산 (G4) |
| 일부 줄만 이상·화면 일부만 | 설정 | ① ROI/crop 불일치 ② 높이 설정 < 실제 줄 수 ③ embedded data 줄이 영상으로 | line count vs 높이 | Image Info line count |
| 색이 녹색/분홍 | 설정 | ① Bayer 순서 불일치(flip/mirror 후 미갱신) ② RAW10 packed/unpacked 해석 ③ YUV 순서(UYVY/YUYV) | 컬러바 TPG | 패턴 원본 비트 vs 저장 버퍼 비교 (G5) |
| 전체가 어둡거나 바랜 색 | 설정 (ISP) | ① black level 미보정 ② bpp 오인(10bit를 8bit로) ③ AWB gain | RAW 덤프 히스토그램 | ISP 단계 on/off |
| 미지원 DT 에러 | 설정 | ① 센서 출력 DT ≠ RX 허용 ② embedded data DT 필터 없음 | ErrID, 센서 모드 | DT 필터/라우팅 설정 |
| 단일 카메라 OK, 4대 동시면 drop | 시스템 | ① DDR/NoC 대역폭·QoS ② line buffer 크기 ③ 링크 사용률 85–100% | line buffer full, DONE vs FS | 카메라 수·해상도 sweep, QoS 조정 (G6) |
| 부하(NPU) 걸릴 때만 drop | 시스템 | ① DDR 경합 ② ISP 클럭 DVFS ③ IRQ 지연 | drop 시점 vs NPU job 시각 | 같은 시간축 로그 |
| VC1 카메라 프레임이 VC0 버퍼로 | 설정 | ① deser VC 매핑(동일 센서 기본 VC0) ② V4L2 routing 표 ③ 3-bit 헤더 오정정(드묾) | VC별 FS 수, frame# 교차 | deser 레지스터 dump (G7) |
| FS 있는데 FE 없음 → 다음 프레임 합쳐짐 | 프로토콜/전기 | ① FE 헤더 ECC2로 손실 ② 프레임 중 stream off ③ RX가 FS 재동기 안 함 | frame sync error, ECC2 시각 | 이벤트 로그 (G8) |
| 프레임 중간 reset 후 영원히 프레임 없음 | 프로토콜 | ① RX가 FS 기다리지 않고 동기 잃음 ② line buffer full 후 reset 필요(PG232는 video_aresetn) [1] | ISR line buffer full | reset 루프 테스트 |
| 카메라 간 timestamp 1 프레임 어긋남 | 시스템 | ① 시퀀스 번호로 짝지음 + 시작 시 한 스트림 drop ② timestamp 지점 다름(SOE vs EOF) ③ trigger→노출 1 프레임 지연 ④ free-run drift | 어긋남이 상수인가 증가하나 | LED 스트로브 + PHC 로그 (G9) |
| 카메라 간 skew가 점점 커짐 | 시스템 | ① FSYNC 안 받음(free-run) ② PTP 미 lock | skew 기울기 | FSYNC 스코프, 센서 trigger 모드 |
| 30 fps 설정인데 15 fps, frame# 연속 | 설정 (센서) | ① 노출 > 프레임 시간(VTS 늘어남) ② PLL/HTS 설정 ③ 저조도 AE | RX FS 간격 | 노출 고정 후 재측정 (G10a) |
| 30 fps 설정인데 15 fps, RX는 30 | 시스템/드라이버 | ① 큐에 버퍼 1개 ② DMA re-arm 지연 ③ ISP frame skip | DONE vs FS | 버퍼 수 늘리기 (G10b) |
| 첫 프레임(들)만 깨짐 | 설정 | ① 센서 노출·AE 안정 전 ② RX가 프레임 중간부터 받음 ③ 첫 burst의 LP-11 시간 짧음 | 첫 N 프레임만? | 첫 N개 버리기 정책 vs 실제 결함 구분 |
| 고온에서만 deser lock 해제 | 전기 (SerDes) | ① 케이블·커넥터 손실 ② PoC 전압 강하 ③ SerDes 마진 | lock 해제 시 온도·PoC 전압 | 챔버 + 케이블 길이 매트릭스 [추정] |
| stream on/off 반복 중 가끔 시작 실패 | 타이밍/드라이버 | ① LP-11 창(≥100 µs 짧을 수 있음) 놓침 [3] ② 센서 → RX enable 순서 ③ deser 재lock 대기 부족 | 실패 시 Stop state | 1만 회 루프, 순서 고정 |
| 프레임 레이트가 흔들림 (jitter) | 시스템 | ① SW timestamp(IRQ 지연) ② free-run 센서 ③ AE가 VTS 변경 | HW vs SW timestamp | V12형 주기 통계 |
| line 수 맞는데 줄 끝 몇 픽셀 쓰레기 | 설정 | ① WC가 stride보다 작음 ② DMA burst 정렬 | WC vs stride | 경계 패턴 TPG |

## 5. 시나리오 드릴

### G1 · 스트림을 시작했는데 프레임이 0개

#### 증상
- `v4l2-ctl --stream-mmap --stream-count=10` 이 아무것도 출력 안 하고 멈추거나 `VIDIOC_DQBUF` timeout. dmesg에 에러 없음. RX ISR = 0, packet count = 0.

#### 먼저 물어볼 것
- "Did this ever work — on this board, this sensor mode, this kernel — or is it first bring-up?"
- "Is there a SerDes in the path, and is the deserializer reporting lock?"
- "Do the receiver's lane stop-state bits change at all, and does the packet count move?"
- "Which side is enabled first in the driver — the receiver or the sensor stream-on?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 센서가 stream on 안 됨 (I2C 쓰기 실패·모드 테이블 누락) | 가장 흔하고 싸게 확인 | 센서 stream/mode 레지스터 읽기, I2C NACK 로그 | Stop state 계속 1 (LP-11), 스코프에 HS 없음 |
| 순서 문제: 센서가 RX enable 전에 HS 시작 → RX가 LP-11을 못 봄 | 커널 문서가 경고하는 패턴 [3] | 드라이버 순서, `.pre_streamon` 지원 여부 | HS 활동은 있는데 RX packet count 0, 순서 바꾸면 해결 |
| SerDes lock 안 됨 | 차량 구성이면 별도 구간 | deser lock, deser TPG 켜기 | TPG도 안 오면 deser→SoC 구간, TPG 오면 카메라 쪽 |
| 레인 수/클럭 모드 불일치 (TX 2 lane, RX 4 lane; continuous vs non-continuous) | DT 오타 흔함 | `data-lanes`, `clock-noncontinuous` DT 속성 vs 센서 설정 | Incorrect lane configuration 또는 SoT sync |
| media 링크·포맷 불일치 | 소프트웨어 마지막 층 | `media-ctl -p` | `EPIPE` 로 stream 시작 자체 실패 |

#### 측정 · 계측
- **아래에서 위로 3단계**: (1) 스코프로 클럭 레인 — HS 토글이 있나(차동 프로브, 수신단 가까이) (2) RX 레지스터 — Stop state, packet count가 움직이나 (3) DMA/V4L2.
- deser 있으면 **deser TPG → SoC** 로 카메라·케이블을 통째로 제거해 구간을 둘로 자른다 [추정: TPG 유무는 부품마다; DS90UB960은 BIST 표기 [9]].

```text
 bad case (순서 문제): 센서가 먼저 HS 시작
 RX enable        ─────────────────────────┐ (여기서 enable)
 데이터 레인  LP-11 ─┐HS HS HS HS HS HS HS HS HS HS HS HS ...   ← RX는 LP-11→LP-01→LP-00 진입을 못 봄
                    └──────────────────────────────────────    → sync 탐색 시작 안 함 (또는 non-continuous면 다음 줄에서 잡힘)
```

#### root cause · fix
- 사례로 말할 결론 [예시]: continuous clock 모드 센서 + RX가 LP-11 관찰 필요 → 센서 stream on이 RX enable보다 먼저 → 영원히 동기 못 잡음. fix: RX 먼저 enable, 센서는 `.pre_streamon` 으로 LP-11 → RX 준비 → stream on. (non-continuous 모드였다면 다음 줄 burst에서 잡혀 "첫 프레임만 깨짐"으로 보였을 것.)

#### 회귀로 굳히기
- stream on/off **1만 회 루프** + 매 회 첫 프레임 10초 내 수신·에러 0 확인, 센서 모드(레인 속도) 전부 × 클럭 모드 × 카메라 수.

#### 30초 영어 답
"Zero frames with zero errors means nothing is reaching the packet layer, so I go bottom-up. First, is the sensor actually streaming — I read its mode and stream registers over I2C and look at the clock lane on a scope. Second, if there's HS activity but the receiver's packet count stays at zero, I check ordering: some receivers must see the lane in LP-11 before streaming starts, so if the sensor begins HS before the receiver is enabled, it never synchronizes. Third, lane count and clock mode in the device tree versus the sensor. With a SerDes, I first use the deserializer's pattern generator to split the camera side from the SoC side."

#### 꼬리 질문
- "The scope shows HS bursts on all lanes, the receiver still sees nothing. Next?"

<details><summary>답</summary>

"Then it's the receiver's PHY configuration or enable. I'd read the per-lane stop-state and SoT-sync bits: if they never change, the PHY isn't enabled or is on the wrong lanes; if SoT sync errors climb, it sees HS but can't find the sync — lane mapping, polarity or settle. I'd drop to one lane at the lowest rate to remove skew and settle from the picture."
한국어 핵심: HS는 있는데 RX 무반응 → 레인별 stop/SoT sync 비트로 PHY enable·레인 매핑·settle 구분, 1 lane 저속으로 단순화.

</details>

- "How would you make this impossible to regress?"

<details><summary>답</summary>

"A start-stop loop in the regression — ten thousand stream-on/off cycles across every sensor mode, each one required to deliver a clean first frame within a timeout, with the error counters as pass criteria. Ordering bugs are timing-dependent, so I'd also vary CPU load and add random delays between the sensor and receiver enables."
한국어 핵심: stream on/off 1만 회 + 모드 전부 + 랜덤 지연으로 순서 버그 고정.

</details>

### G2 · SoT 에러만 계속 증가

#### 증상
- 영상은 대체로 나오지만 RX의 **SoT error(ErrSoTHS)** 가 초당 수십–수백 증가, 가끔 줄 깨짐. ECC/CRC는 거의 0. 센서 모드를 1080p60(고속)에서 binning 저속 모드로 바꾼 뒤부터 시작.

#### 먼저 물어볼 것
- "Is it all lanes or one lane?"
- "Did it start with a sensor-mode change — a different link frequency?"
- "Continuous or non-continuous clock, and does it scale with lane rate?"
- "Does the receiver driver recompute settle from the link frequency, or is it a fixed value?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| settle 값이 실제 레인 속도와 불일치 | 모드 변경과 동시 시작, §2.2 계산 | 드라이버가 쓰는 settle vs 공식, DT `link-frequencies` vs 센서 PLL 실제값 | 모든 레인 SoT 에러, settle sweep에서 창이 기존 값 밖 |
| 한 레인만 skew/SI | 레인 하나만 에러 | 레인별 SoT 비트 | 특정 레인만, 보드 배선/커넥터 |
| non-continuous 클럭 TCLK-PRE/POST·LP 전환 타이밍 위반 | 매 줄 LP↔HS 전환 | 센서 클럭 모드 continuous로 바꿔 보기 | continuous면 사라짐 |
| HS-ZERO/PREPARE가 센서 쪽에서 짧음 | TX 타이밍 문제 | 스코프로 LP-00·HS-0 길이 측정 | THS-PREPARE+ZERO < 145 ns + 10 UI |

#### 측정 · 계측
- **settle sweep**: settle 값을 0부터 끝까지 한 단계씩 바꾸며 10초씩 SoT 카운트 → 통과 창(에러 0 구간) 찾기 → 창 **중앙**으로 설정. `imx-mipi-csis` 류는 debugfs `ths_settle` 로 덮어쓰기 가능 [4].
- 스코프: 데이터 레인 하나의 Dp/Dn 단일 종단 — LP-01·LP-00 폭 = TLPX·THS-PREPARE, HS-0 길이 측정. 트리거: LP-11에서 떨어지는 edge(약 1.2 V → 0).

```text
 settle sweep 결과 (모양 [예시])
 settle count : 0  2  4  6  8 10 12 14 16 18 20 22 24 26 28 30
 SoT err/10s  : ## ## ## ## ## #  .  .  .  .  .  #  ## ## ## ##     . = 0 에러
                               └── 통과 창 12–20 → 16 선택 (기존 고정값 24 = 고속 모드용)
```

#### root cause · fix
- 드라이버가 settle을 probe 때 한 번만 계산(고속 모드 기준) → 저속 모드에서 창 밖. fix: `s_stream` 마다 현재 link frequency로 재계산(`v4l2_get_link_freq()` [3]), DT `link-frequencies` 에 모드별 값 전부 나열.

#### 회귀로 굳히기
- **모든 센서 모드 × 온도 3점**에서 settle sweep 창 폭 기록 → 설정값이 창 중앙 ± margin 안인지 자동 확인. 창 폭이 줄어드는 추세도 기록(실리콘·보드 리비전 비교).

#### 30초 영어 답
"SoT errors are the PHY telling me it can't cleanly enter high-speed mode, so I look at the LP-to-HS transition. Since it started with a mode change, my first suspect is the receiver's settle time: the valid window is about 85 nanoseconds plus 6 UI to 145 plus 10 UI, and if the counter is in byte-clock units, the right count scales with lane rate — a value tuned for the fast mode is far outside the window at a slow one. I'd sweep the settle value, find the passing window and center it, and fix the driver to recompute it per mode. If only one lane errors, it's skew or the board instead."

#### 꼬리 질문
- "The settle window is only two steps wide. Is that OK?"

<details><summary>답</summary>

"It works, but it's not margin. I'd measure the window across voltage and temperature corners; if it collapses or shifts at a corner, that's a characterization finding for the PHY or the board. At very low lane rates the counter granularity itself can be the limit, which is a design feedback point."
한국어 핵심: 창 2칸 = 동작은 하나 마진 없음 → PVT 코너에서 창 측정, 저속은 카운터 해상도 자체가 한계일 수 있음.

</details>

- "How do SoT errors differ from SoT sync errors?"

<details><summary>답</summary>

"In the AMD receiver's terms, an SoT error means the start-of-transmission pattern was received with an error but sync was still possible, while SoT sync error means synchronization failed completely. Sync errors on every burst point at lane mapping, polarity or a grossly wrong settle; occasional SoT errors point at marginal timing."
한국어 핵심: SoT = 패턴 이상(동기는 됨), SoT sync = 완전 실패 → 매번이면 매핑/극성, 간헐이면 마진.

</details>

### G3 · ECC 정정 카운트가 온도 따라 증가

#### 증상
- 챔버 25 → 105 °C 램프 중 **ECC 1-bit(corrected)** 가 프레임당 0 → 5로 증가, 95 °C 이상에서 CRC 에러와 `V4L2_BUF_FLAG_ERROR` 버퍼가 나타남. 상온에선 하루 0. (코드 G3 mock = 이 상황)

#### 먼저 물어볼 것
- "Is it every camera or one, and every lane or one?"
- "Is the camera behind a SerDes — is the deserializer's own link error counter rising too?"
- "What's the lane rate versus the PHY's rated maximum, and is it the same at the voltage corners?"
- "Is the heat on the whole system or only the SoC, the camera, or the cable?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| D-PHY 링크(deser → SoC) SI 마진이 고온에서 소진 | ECC1이 먼저, CRC가 나중 = 마진 곡선 | 레인 속도 한 단계↓, 레인별 분포 | 속도 낮추면 0, 특정 레인 집중 |
| SerDes 링크 마진(케이블·커넥터·PoC 전압) | 차량 구성, 카메라만 가열해도 재현? | deser 링크 에러 카운터, PoC 전압 | deser 카운터도 같이 증가 [추정] |
| 수신 PHY 아날로그(종단·CDR·settle 창 이동) | 칩만 가열해도 재현? | 열 forcer로 SoC만 가열, settle sweep at 105 °C | settle 창이 고온에서 이동 |
| 센서 TX 출력 약화 | 센서만 가열 | 다른 센서 swap | 센서 따라감 |

#### 측정 · 계측
- **"한 번에 하나만 데운다"**: 열 forcer로 SoC만 / 카메라만 / 챔버 전체 → 어느 부품의 온도와 상관인지.
- 카운터를 프레임 단위로 기록해 **온도 대비 곡선** — 코드 출력 `ECC1 per frame [0, 0, 1, 1, 2, 3, 4, 5]` 처럼 추세로 본다.
- ECC1은 "정정됐으니 OK"가 아니다: ECC1 비율 ∝ BER, 같은 BER이면 payload(헤더보다 수백 배 김)엔 CRC 에러가 훨씬 많이 나야 정상 → **헤더만 ECC1 많고 CRC 0이면 헤더 쪽 특이 경로(첫 바이트·settle)** 를 의심.

#### root cause · fix
- [예시] 4 lane × 2.5 Gb/s(PHY 최대) 운용 + 보드 레인 길이 불일치 → 고온에서 eye 축소. fix 옵션: 레인 속도 한 단계↓(대역폭 여유 계산: [N06E E3 실행 결과 1](2026-11-01_N06E_plans_highspeed_io_sensor.html)), deskew 사용(D-PHY v1.2+) [추정], 보드 리비전에서 길이 매칭.

#### 회귀로 굳히기
- PVT 매트릭스: 온도 −40/25/105 °C × 전압 코너 × 레인 속도 → **ECC1 = 0을 통과 기준**(정정 가능이라도 0), CRC 0, 그리고 settle sweep 창 폭 ≥ N 단계. 24 h soak에서 ECC1 추세 기록.

#### 30초 영어 답
"Single-bit ECC corrections rising with temperature is the link losing margin before it actually fails — the header still decodes, but the bit error rate is climbing, and CRC errors on the much longer payload follow. I'd localize by heating one thing at a time: only the SoC with a thermal forcer, only the camera, then the whole system, and compare per-lane counts and the deserializer's link counters. Then I'd step the lane rate down and re-sweep settle at temperature. My pass criterion is zero corrected errors, not zero uncorrected ones, because corrections are the early warning."

#### 꼬리 질문
- "Why not just accept 1-bit errors since ECC fixes them?"

<details><summary>답</summary>

"Because the same bit error rate that flips header bits also hits the payload, where CRC only detects, so frames get dropped. And beyond two bits, a single-error-correcting code can miscorrect — a three-bit header error can look like a valid header with the wrong virtual channel or word count. Corrections are a margin measurement; I want zero in validation."
한국어 핵심: 같은 BER이 payload에도 → CRC drop, 3비트면 오정정(N06E 실험 93%) → ECC1은 마진 지표, 0이 기준.

</details>

- "How long do you need to run to claim a BER of 1e-12?"

<details><summary>답</summary>

"With zero errors, about three over the BER in bits for 95 percent confidence — three times ten to the twelfth bits. At four lanes times 2.5 gigabits, that's 10 gigabits per second, so roughly five minutes per corner. That's why I'd run it per lane rate and temperature point rather than as one long soak."
한국어 핵심: 에러 0 기준 3/BER 비트(95%) → 10 Gb/s면 약 300초, 코너마다.

</details>

### G4 · 이미지가 대각선으로 찢어짐

#### 증상
- 저장한 프레임이 **오른쪽 아래로 기울어진 줄무늬(shear)**, 에러 카운터는 전부 0, fps 정상. 센서 설정을 "1928×1088 (border 포함)"으로 바꾼 뒤. (코드 G4 mock: 모든 줄 WC = 2410, 설정 2400)

#### 먼저 물어볼 것
- "Are there any receiver errors, or is the link completely clean?"
- "What width, bit depth and data type does the sensor send, and what stride does the DMA write?"
- "Is the tear angle constant across frames?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| WC(실제 줄 바이트) ≠ DMA/뷰어 stride | 에러 0 + 일정한 기울기 | 수신 WC vs 설정 | 줄마다 일정 바이트 offset 누적 |
| RAW10 폭이 4의 배수 아님 → packing 경계 | 폭 변경 직후 | WC = 폭×10/8 정수인가 | 일부 RX가 hang/에러 (N06E BND 행) |
| stride 정렬(예: 64 B)을 뷰어/소비자가 모름 | DMA가 패딩 | 버퍼 `bytesperline` vs 폭×bpp | 소비자 쪽만 찢어짐 |
| 센서 bpp(RAW12)인데 RX/뷰어는 RAW10 | DT 불일치 | ErrID? DT 필드 | 줄 길이 비율 1.2배 |

#### 측정 · 계측
- 이벤트 로그/Image Info **byte count** 로 WC 확인 → 기울기 계산: 줄당 +10 B면 1088줄 동안 10,880 B = RAW10 기준 약 8,700 픽셀 밀림 → 화면을 여러 번 감는 대각선.
- 소비자가 쓰는 `bytesperline`(V4L2 포맷) vs 실제 → 셋 중 누가 다른지(센서 WC, RX/DMA 설정, 소비자 stride).

#### root cause · fix
- 센서 폭 1928 → WC 2410인데 RX/DMA·포맷은 1920(2400) 그대로 → 줄마다 10 B 밀림. fix: `media-ctl -V` 로 센서·RX·video 패드 포맷을 같은 크기로 전파, 또는 RX crop으로 1920만 받기. 포맷 검사는 링크 검증(`link_validate`)에서 걸리게.

#### 회귀로 굳히기
- 해상도 매트릭스(최소/최대/4의 배수 아닌 폭) × DT × **TPG ramp 패턴 golden 비교**(ramp는 줄 offset이 생기면 바로 보임) + 수신 WC == 기대 WC 체크.

#### 30초 영어 답
"A clean diagonal tear with no receiver errors is almost never signal integrity — it's a line-length mismatch. Each line arrives a few bytes longer or shorter than the stride the DMA or the viewer assumes, and the offset accumulates down the image. I'd compare three numbers: the word count the receiver actually sees, the line length the receiver and DMA are configured for, and the bytes-per-line the consumer uses. In my mock, every line is 2410 bytes against 2400 configured — a 1928-pixel RAW10 sensor mode. The fix is propagating the format through the media pipeline so the link validation fails loudly instead."

#### 꼬리 질문
- "Why does a ramp pattern help more than color bars here?"

<details><summary>답</summary>

"Color bars are vertical, so a small horizontal shift per line just tilts them slightly and is easy to miss. A ramp or a known per-pixel pattern lets me compute the exact byte offset per line from the captured buffer, which directly tells me the word count error."
한국어 핵심: 컬러바는 미세 offset이 잘 안 보임, ramp는 줄당 offset을 바로 계산 가능.

</details>

- "If WC matches but the tear appears only every few hundred frames?"

<details><summary>답</summary>

"Then it's not configuration, it's an occasional short or long line — a word-count corruption or a dropped packet mid-frame. I'd look at the word-count-corruption flag and the line count per frame, and correlate with ECC and CRC errors; a three-bit header error that miscorrects the word count would look exactly like this."
한국어 핵심: 간헐이면 WC corruption/줄 손실 → line count·ECC 상관, 3비트 오정정 가능성.

</details>

### G5 · 색이 녹색/분홍으로 나온다

#### 증상
- 영상 구조는 정상인데 **전체가 초록빛** 또는 **분홍/보라**. 센서를 차량 장착 방향에 맞게 **vertical flip** 켠 뒤부터.

#### 먼저 물어볼 것
- "Is the problem in the RAW capture or only after the ISP?"
- "Did anything change in flip, mirror or crop?"
- "What media-bus format does each pad report?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| Bayer 순서 불일치 (flip/mirror/홀수 crop이 RGGB → GBRG 등으로 바꿈) | flip 직후 시작 | 센서 flip 끄기, 패드 포맷 코드 | flip 끄면 정상, 순서만 바꾸면 해결 |
| RAW10 packed(5바이트/4픽셀) vs 16비트 unpacked 해석 혼동 | 색+노이즈 | RAW 덤프 바이트 패턴 | 줄무늬·잡음 동반 |
| YUV 순서(UYVY vs YUYV) | YUV 출력 모드 | 포맷 확인 | 초록/분홍 + 색 번짐 |
| black level/AWB gain | ISP 설정 | ISP bypass로 RAW 보기 | RAW는 정상 |

#### 측정 · 계측
- **센서 컬러바 TPG** → RAW 버퍼에서 2×2 블록 값을 직접 읽어 어느 위치가 R/G/B인지 확인 → 선언한 Bayer 순서와 비교. ISP를 bypass해 RAW 단계와 ISP 단계를 분리.
- 왜 초록: 순서를 한 칸 틀리면 G 픽셀(전체의 50%)이 R/B 자리에 들어가 R·B 채널이 G 값으로 커진다/작아진다 → 초록 또는 그 보색(마젠타·분홍) [추정: 직관적 설명].

#### root cause · fix
- 센서 드라이버가 flip 설정 시 media bus 코드(Bayer 순서)를 갱신 안 함 → ISP는 RGGB로 demosaic. fix: flip/mirror 변경 시 패드 포맷 코드를 갱신(많은 업스트림 센서 드라이버가 이렇게 함 [추정]), ISP 측 순서 설정 동기화.

#### 회귀로 굳히기
- flip × mirror × crop 오프셋(짝/홀) 4×2 조합 × 컬러바 TPG → ISP 출력 색 검사(바마다 기대 RGB ± 허용 오차).

#### 30초 영어 답
"Structure fine, colors wrong, no errors — that's an interpretation problem, not a link problem. A green or magenta cast is the classic sign of the wrong Bayer phase, and flip, mirror or an odd crop offset all change the Bayer order the ISP should assume. I'd turn on the sensor's color-bar pattern, read the raw buffer, check which pixel of each two-by-two block is red, and compare to the declared media-bus format. Then bypass the ISP to see whether RAW is right. The fix is updating the format when flip changes, and a regression over flip, mirror and crop combinations."

#### 꼬리 질문
- "How do you tell a Bayer-order bug from a white-balance bug?"

<details><summary>답</summary>

"White balance is a smooth gain on each channel, so edges and fine detail look normal and only the cast changes. Wrong Bayer order also produces zipper artifacts and false colors on edges, because demosaic interpolates the wrong neighbors. With a test pattern, white balance never makes a red bar green; Bayer order can."
한국어 핵심: AWB = 균일한 색조, Bayer 순서 = 경계 지퍼/가짜 색, 패턴의 빨강이 초록이 되면 순서.

</details>

- "Who owns this in validation versus camera tuning?"

<details><summary>답</summary>

"Validation owns 'bit-exact to the design intent' — the right Bayer order, the right unpacking, ISP output matching the C model for a known input. Whether the colors look good under real lighting is camera tuning. I'd write the test against the test pattern and the model, not against a subjective image."
한국어 핵심: 검증 = bit-exact·의도대로, 화질 = 튜닝 팀.

</details>

### G6 · 카메라 1대는 OK, 4대 동시면 프레임 drop ★ (deck §7 #8 확장)

#### 증상
- VC0–3으로 deser 한 포트에 4대: 각자 혼자면 drop 0, 4대 + NPU job 시작 시점에 **모든 VC에서 동시에 1–2 프레임** 사라짐. ECC/CRC 0. RX에 **stream line buffer full**. (코드 G6 mock)

#### 먼저 물어볼 것
- "Do the drops happen on all four virtual channels at the same moment, or one camera at a time?"
- "Are there any link-layer errors at all — ECC, CRC, deserializer link errors?"
- "What else is using DDR at that moment — NPU, encoder — and is there QoS on the camera path?"
- "What's the link utilization: resolution, bit depth, fps, lane rate?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| DDR/NoC 대역폭·QoS: 카메라 쓰기가 NPU 버스트에 밀림 | 동시 drop + LBFULL + PHY 깨끗 | drop 시각 vs NPU job, QoS 우선순위 | NPU 끄면 0, QoS 올리면 0 |
| line buffer 크기 부족 (지연 흡수 못 함) | LBFULL | 버퍼 깊이 vs 최악 지연 × 쓰기율 | 짧은 지연에도 넘침 |
| 링크 사용률 85–100% (4×3MP RAW12 30 fps on 4×1.5 Gb/s ≈ 85% [N06E]) | 링크 자체 한계 | 사용률 계산, ECC/SoT 동반? | PHY 에러 동반, frame# gap |
| ISP throughput (pixel/clock) 부족 | ISP가 4 스트림 시분할 | ISP bypass(RAW 직접 DDR) | bypass하면 사라짐 |
| IRQ/버퍼 반환 지연 (SW) | CPU 부하 | DONE은 있는데 앱 seq gap? | 버퍼 큐 비어 있음 |

#### 측정 · 계측
- **구간별 숫자 맞추기**: 센서 frame# 연속(gap 0) → RX FS = FE (링크는 다 받음) → **DONE < FS** (RX→DDR에서 손실) → LBFULL 시각 = NPU 시작. 코드 출력: `frame index -> VCs hit: 4:[0, 1, 2, 3], 5:[1, 3]`, `PHY-layer flags total 0, frame# gaps 0`.
- DDR 쪽: 메모리 컨트롤러 대역폭/지연 카운터(있으면), NoC QoS 설정, 카메라 경로 최대 지연 측정. 실험 손잡이: NPU 부하 단계별, 카메라 수 1→4, 해상도, QoS 우선순위.

```text
 시간축 (같은 PHC 기준으로 로그 정렬)
 NPU job        ──────────────┌██████████████┐────────
 DDR latency    ____________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\_______
 LBFULL (VC0-3) ______________|‾|‾|____|‾|____________
 DONE VC0       D  D  D  D  .  D  D  D      (. = 받았지만 DDR에 못 씀)
 RX FS VC0      F  F  F  F  F  F  F  F      ← 링크는 정상
```

#### root cause · fix
- 카메라 DMA 쓰기 QoS가 NPU와 동일 우선순위 + line buffer가 수십 µs 지연밖에 못 흡수 → NPU 버스트 동안 넘침. fix: 카메라 경로 QoS 상향/대역폭 예약, line buffer 깊이(설계 피드백), NPU 버스트 분산. 그리고 **LBFULL 후 RX 복구 동작**(PG232는 line buffer full 시 `video_aresetn` reset 필요 [1]) 확인 — 다음 프레임에 자동 복구되나.

#### 회귀로 굳히기
- STRESS: 전 카메라 최대 rate + NPU·인코더 최대 DDR 부하, 24 h, 통과 기준 = **LBFULL 0, DONE == FS, V4L2 seq gap 0**. QoS를 일부러 낮춘 negative 테스트로 "넘치면 정의된 drop + 카운터 + 다른 VC 영향 없음 + 자동 복구"도 확인.

#### 30초 영어 답
"I'd first find where frames are lost by counting at each stage. If the sensor's frame numbers are contiguous and the receiver sees every frame start and end with no ECC or CRC errors, the link is fine. If the DMA completes fewer buffers than the receiver received, and the line-buffer-full flag fires at the moment the NPU starts, the loss is downstream: DDR or interconnect bandwidth, and QoS for the camera writes. All four channels dropping together points at a shared resource, not one camera's link. I'd confirm by stepping NPU load and camera count, fix with QoS or buffering, and add a full-load soak with zero line-buffer-full as the pass criterion."

#### 꼬리 질문
- "What if drops happen on one camera only, with ECC errors?"

<details><summary>답</summary>

"Then it's that camera's link, not shared bandwidth. I'd look at that camera's SerDes link counters and the D-PHY errors on its virtual channel — although all four VCs share one D-PHY after the deserializer, so per-VC PHY errors would really point to the serializer link or the camera itself. Swap cameras and cables between ports to see whether the problem follows the camera or the port."
한국어 핵심: 한 대만 + ECC → 그 카메라 링크, deser 이후 D-PHY는 공유라 VC별 PHY 에러면 SerDes/카메라 쪽, swap으로 따라가나 확인.

</details>

- "How do you size the line buffer requirement?"

<details><summary>답</summary>

"Worst-case write latency the camera path sees from DDR under full load, times the incoming byte rate. For four 3-megapixel RAW12 cameras at 30 fps, that's around 0.7 gigabytes per second unpacked, so every microsecond of stall needs roughly 700 bytes of buffer. I'd measure the latency distribution under load and compare its p99.99 to the buffer depth."
한국어 핵심: 버퍼 = 최악 지연 × 입력 바이트율 (예 0.7 GB/s → µs당 약 700 B), 부하 중 지연 분포의 꼬리로 판단.

</details>

### G7 · VC 1 카메라의 프레임이 VC 0 버퍼로 들어간다

#### 증상
- 카메라 A(deser 입력 A)와 B(입력 B) 연결. `/dev/video0`(VC0)에 **두 카메라 화면이 줄 단위로 섞인** 프레임, `/dev/video1`(VC1)은 프레임 0. RX에 VC0 frame synchronization error 다수. (코드 G7 mock)

#### 먼저 물어볼 것
- "Are the two cameras the same sensor model with default settings?"
- "Who assigns virtual channels — the sensor, the deserializer, or both?"
- "Is it every frame, or rare single packets landing in the wrong buffer?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| deser VC 매핑 누락 → 같은 센서 둘 다 기본 VC0 | 매 프레임, VC1 완전 침묵 | deser 입력별 VC 설정 dump | VC0에 FS 2배, frame# 두 계열 교차 |
| 센서 VC 레지스터 설정 오류 | 센서 쪽에서 VC 지정 구성 | 센서 레지스터 | 동일 |
| V4L2 routing 표(stream → video 노드) 오설정 | 커널 스트림 API 사용 시 | `VIDIOC_SUBDEV_G_ROUTING` 류 확인 [13] | RX 레지스터는 VC 분리, SW 단계에서 섞임 |
| 3-bit 헤더 오정정으로 VC 비트 변조 | 드물게 한 패킷 | 빈도 | 아주 가끔 단일 줄만 엉뚱한 VC |

#### 측정 · 계측
- 이벤트 로그: VC0 FS frame#가 `[500, 40, 501, 41, 502, 42]` — **독립 카운터 두 개가 한 VC에 섞임**이 결정적 증거, 줄 수 2배(12 vs 6), VC1 FS = 0.
- RX VC별 Image Info(line count)·frame sync error VCn 비트 [1]로 HW 단계에서 이미 섞였는지 → 섞였으면 deser/센서, 아니면 SW routing.

#### root cause · fix
- deser가 입력 B의 VC 재매핑 설정을 안 씀(초기화 테이블에서 누락) → 두 카메라 모두 VC0. fix: deser 입력 포트별 VC 매핑, 드라이버가 설정 후 readback 검증. 부팅 시 "VC별 FS 수 > 0" 자가 진단 추가.

#### 회귀로 굳히기
- 카메라 수 1–4 × 포트 조합(빈 포트 포함) × 핫플러그/재연결 → 각 VC 프레임에 **카메라 고유 패턴**(deser/센서 TPG에서 카메라마다 다른 패턴)이 맞는 버퍼에 오는지 검사. 3-bit 오정정 방어선(WC 범위·FS/FE 짝)은 패턴 생성기 주입으로 별도.

#### 30초 영어 답
"If one camera's frames land in another camera's buffer every frame, it's a virtual-channel mapping problem, not corruption. Identical sensors usually default to the same VC, so the deserializer has to remap each input. The evidence in the receiver log is clear: on VC0 I see two interleaved frame-number sequences, double the lines per frame and frame-sync errors, while VC1 never sees a frame start. I'd dump the deserializer's per-port VC mapping, fix the init table, and add a test where each camera sends a distinct pattern and every buffer is checked against its source."

#### 꼬리 질문
- "And if only one line in a million lands on the wrong VC?"

<details><summary>답</summary>

"That's the header-ECC limit: a three-bit header error can be miscorrected into a valid-looking header with a different VC. The defense is a second check — word-count range, FS/FE pairing per VC, frame-number continuity — and the root fix is link margin. I'd inject three-bit header errors with a pattern generator to prove the receiver's second line of defense works."
한국어 핵심: 백만 줄에 하나 = 3비트 오정정 가능 → WC 범위·FS/FE 짝·frame# 2차 방어선, 패턴 생성기로 주입 검증.

</details>

- "How would you make VC assignment self-checking at boot?"

<details><summary>답</summary>

"After stream-on, read per-VC frame counters for a second and require every configured camera's VC to have frames at the expected rate and no frame-sync errors. Better still, turn on each camera's test pattern with a camera-specific value and check one frame per VC. It costs a few hundred milliseconds and catches mapping and cabling mistakes in the factory."
한국어 핵심: 부팅 후 VC별 FS 수·sync 에러 확인 + 카메라별 고유 TPG 1 프레임 검사.

</details>

### G8 · FE가 누락돼서 다음 프레임과 합쳐진다

#### 증상
- 가끔 버퍼 하나가 **줄 수 2배**(또는 버퍼 넘침으로 잘림) + `V4L2_BUF_FLAG_ERROR`, 앞 프레임 하나는 DONE 없음. RX에 frame synchronization error 1회, 같은 시각 **ECC 2-bit** 1회. (코드 G8 mock)

#### 먼저 물어볼 것
- "Is there an uncorrectable header error at the moment the frame end was due?"
- "Does the receiver recover on the next frame start, or does it stay out of sync?"
- "Does it correlate with temperature or stream-off?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| FE short packet 헤더가 ECC2 → 폐기 | 같은 시각 ECC2 | 이벤트 시각 정렬 | ECC2가 FE 자리, 이후 FS-while-open |
| 프레임 중간 stream off(센서가 즉시 중단) | stop/start 시점 | 로그에서 stream off 시각 | 마지막 프레임만, ECC 없음 (커널 문서: 즉시 멈추는 TX도 있음 [3]) |
| RX 상태 머신이 새 FS에서 재동기 안 함 | 병합 = RX 동작 | RX 문서/설계: FS-while-open 처리 | 병합이 계속 이어짐 |
| FE 비활성(센서 모드 설정) | 매 프레임 | 센서 설정 | 모든 프레임 병합 |

#### 측정 · 계측
- 코드 출력: `FS arrived while frame #303 was still open; that frame closed with 12 lines`, `1 uncorrectable header (ECC2) ... = where FE was due`. → **구조 오류의 원인이 PHY 오류**라는 연결이 핵심(G3과 같은 뿌리).
- RX가 FS-while-open을 어떻게 처리하나: (a) 이전 프레임을 에러로 닫고 새로 시작 — 손실 1 프레임 (b) 이어 붙임 — 병합 + 버퍼 넘침. 설계 의도와 비교.

#### root cause · fix
- 마진 부족으로 드물게 FE 헤더 2-bit 에러 → FE 폐기 → RX가 새 FS를 받고도 이전 프레임을 닫지 않음. fix 두 갈래: ① 링크 마진(G3) ② RX/드라이버: **FS-while-open이면 이전 프레임을 에러로 닫고 새 프레임 시작**(한 프레임만 잃고 다음은 정상) — 손상 전파를 막는 게 검증 관점의 요구사항.

#### 회귀로 굳히기
- 패턴 생성기로 **FE 생략 / FS 생략 / FE 헤더 2-bit 주입** 각 1,000회 → 통과 기준: 해당 프레임만 에러 표시, **다음 FS에서 자동 복구**, 병합 버퍼 0, hang 0 ([N06E E3 ERR 행](2026-11-01_N06E_plans_highspeed_io_sensor.html)).

#### 30초 영어 답
"A merged frame means the receiver missed a frame end and kept appending lines into the open frame. In the log, a frame start arrives while the previous frame is still open, and at the exact time the frame end was due there's an uncorrectable two-bit header error — so the frame end was lost on the wire. That gives two fixes: link margin, because two-bit errors shouldn't happen, and receiver behavior: on a new frame start with a frame open, it should close the old one as errored and resynchronize, so one error costs one frame, not two. I'd inject missing and corrupted frame ends to test that recovery."

#### 꼬리 질문
- "Why is recovery behavior a validation item and not just a nice-to-have?"

<details><summary>답</summary>

"Because in a car, errors will happen eventually — EMI, a connector — and what matters for safety is the blast radius. One lost frame at 30 fps is tolerable for perception; a receiver that stays out of sync or corrupts the next frame isn't. So I test that every injected error is detected, flagged, contained to one frame, and counted."
한국어 핵심: 차량에선 에러는 결국 남 → 피해 범위(1 프레임)로 제한되는지가 안전 요구.

</details>

- "How would you tell a lost FE from a sensor that stopped mid-frame?"

<details><summary>답</summary>

"A lost FE has a header error at the right moment and the next frame start arrives on schedule. A sensor stopping mid-frame has no header error, the line count stops early, and there's no following frame start until the next stream-on. My analyzer reports the second case as a different code for that reason."
한국어 핵심: FE 손실 = 그 시각 ECC2 + 다음 FS 정시, 센서 중단 = 에러 없음 + 줄 수 모자람 + 다음 FS 없음.

</details>

### G9 · 카메라 간 timestamp가 정확히 1 프레임 어긋남 ★ (면접관 특허 영역)

#### 증상
- 4대가 같은 FSYNC로 동시 노출되는데, 퓨전 로그에서 **카메라 2의 timestamp만 다른 카메라보다 정확히 33.3 ms(1 프레임) 늦게** 짝지어짐. 재부팅하면 가끔 0 또는 1 프레임으로 바뀜. 30 m/s 주행이면 1 프레임 = **약 1 m 위치 오차**.

#### 먼저 물어볼 것
- "Is the offset exactly one frame period and constant, or does it drift and grow?"
- "Where is each timestamp taken — exposure start, frame start at the receiver, or frame end at DMA completion — and is it the same for all cameras?"
- "How are frames from different cameras paired — by timestamp nearest match or by sequence number?"
- "Do all cameras run in external trigger mode, and what's each sensor's trigger-to-exposure latency?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| **sequence 번호로 짝지음** + 시작 시 한 스트림이 첫 프레임 drop(또는 늦게 stream on) | 재부팅마다 0/1로 바뀜 = 시작 조건 의존 | 짝짓기 방식, 각 스트림 첫 seq 시각 | timestamp로 짝지으면 사라짐 |
| 센서 trigger 동작 차이: 한 센서는 FSYNC 받은 **다음 프레임**에 노출 (파이프라인 1 프레임) | 정확히 1 프레임, 그 카메라만 | 센서 데이터시트 trigger 모드, LED 스트로브 | 스트로브가 그 카메라에서 1 프레임 늦게 보임 [추정: 센서마다] |
| timestamp 지점이 다름 (SOE vs EOF, SoF latch vs DMA done) | 드라이버 다르면 흔함 | V4L2 timestamp flag (`TSTAMP_SRC_SOE`/`EOF`) [5] | 차이 = 노출+readout 시간 (1 프레임 근처일 수도) |
| deser/ISP 경로 버퍼링이 한 카메라만 1 프레임 | 경로 비대칭 | SoF latch(RX) vs DONE 시각 카메라별 비교 | RX SoF는 맞고 DONE만 늦음 |
| free-run (FSYNC 미적용) | 동기 깨짐 | skew 시간 추세 | 상수 아님, 서서히 증가 후 1 프레임 slip |

#### 측정 · 계측
- **먼저 "상수냐 추세냐"**: [V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html)형 체커로 카메라 간 skew를 프레임마다 → 상수 33.3 ms면 이산 문제(짝짓기·trigger 파이프라인·버퍼), 기울기가 있으면 free-run/PTP 미 lock.
- **물리 ground truth**: PHC로 예약된 **LED 스트로브**(모든 카메라 시야, 한 프레임 동안만 켬) + 스코프(ch1 FSYNC, ch2 LED 구동, ch3 카메라 2 FSYNC 입력) → 각 카메라에서 LED가 켜진 프레임의 timestamp를 비교 → **"timestamp가 말하는 시각" vs "실제 노출 시각"** 차이를 카메라별로 측정 ([N06E E4](2026-11-01_N06E_plans_highspeed_io_sensor.html) 셋업).
- rolling shutter면 LED가 켜진 **줄 번호**로 줄 시각까지 역산 (2160줄 30 fps면 위→아래 약 32 ms [N06E E4 계산]).

```text
 FSYNC (PHC periodic out)   ┌┐            ┌┐            ┌┐
 cam0 exposure              ██▒▒▒▒        ██▒▒▒▒        ██▒▒▒▒      (██ 노출, ▒ readout)
 cam2 exposure (trigger→다음 프레임)        ██▒▒▒▒        ██▒▒▒▒   ← 1 프레임 늦게 시작
 LED strobe                               ┌──┐
 cam0 frame with LED: k        cam2 frame with LED: k (seq는 같은데 실제 노출은 다른 순간) → 퓨전이 1 프레임 어긋남
```

#### root cause · fix
- [예시 결론] 짝짓기가 V4L2 sequence 번호 기반 + 카메라 2가 stream on 직후 첫 프레임 drop → 이후 영원히 seq가 1 어긋남. fix: **같은 PHC 시간축의 HW SoF timestamp로 nearest-match**(±P/2), sequence는 drop 검출용으로만. trigger 지연이 원인이었다면 센서별 "trigger → 노출 시작" 지연을 측정해 timestamp 보정표에 넣고 정의서에 "frame timestamp = 노출 시작(중앙 줄)" 명시.

#### 회귀로 굳히기
- stream on/off·재부팅 1,000회 × 카메라 시작 순서 랜덤 → 매번 LED 스트로브로 카메라 간 실제 skew < 예산 [추정: 수십 µs급]. 24 h에서 skew 통계(평균·p99·max)와 drift 기울기, 1 프레임 slip 0회.

#### 30초 영어 답
"Exactly one frame, constant, and changing across reboots tells me it's discrete, not drift. So I suspect how frames are paired, not the clocks. If pairing uses sequence numbers and one stream dropped its first frame at start-up, every later pair is off by one. I'd pair by hardware frame-start timestamps on one PTP-disciplined clock instead, with sequence numbers only for drop detection. Then I'd prove it physically: an LED strobe that all cameras see, scheduled from the PTP clock, so I can compare what each timestamp claims against when the exposure really happened. That also catches sensors whose trigger takes effect one frame late."

#### 꼬리 질문
- "What exactly should 'the frame timestamp' mean?"

<details><summary>답</summary>

"It has to be defined in writing, because start of exposure, frame start at the receiver and frame end at DMA completion can differ by tens of milliseconds. For fusion I'd want start of exposure — ideally of a reference row, since rolling shutter spreads exposure over about one frame time — computed from a hardware latch at frame start minus a measured, per-sensor-mode offset. V4L2 even flags whether a timestamp is start of exposure or end of frame."
한국어 핵심: timestamp 정의서 필수 — 노출 시작(기준 줄) = RX SoF latch − 측정한 모드별 지연, V4L2 flag로 SOE/EOF 구분.

</details>

- "The skew is fine on the bench but bad in the vehicle. Why?"

<details><summary>답</summary>

"In the vehicle the PTP path and the trigger wiring are different. Cable lengths and SerDes latency differ per camera, the FSYNC may be tunneled through the serializer back-channel with its own latency, and the time sync crosses switches with asymmetric delays that the protocol can't see. I'd measure FSYNC arrival at each camera with a scope in the vehicle harness and compare PPS outputs of the nodes."
한국어 핵심: 차량은 케이블·SerDes 지연·FSYNC 터널링·PTP 비대칭이 다름 → 하네스 상태에서 FSYNC 도착·PPS 직접 측정.

</details>

- "How do you validate sensor sync online, while driving?"

<details><summary>답</summary>

"Continuously check per-camera frame-period jitter and inter-camera skew from the hardware timestamps against limits, and flag when a camera's skew trend changes. Cross-check against an independent source — for example, LiDAR scan phase versus camera trigger times. If something exceeds a limit, the system marks that sensor's data as degraded rather than silently fusing misaligned frames."
한국어 핵심: 주행 중 HW timestamp로 주기 jitter·skew를 계속 감시, 독립 기준(LiDAR 위상)과 교차 확인, 초과 시 degraded 표시.

</details>

### G10 · 30 fps로 설정했는데 15 fps가 나온다

#### 증상
- 앱이 측정한 fps = 15.00. 두 가지 변형 (코드 mock 둘): **G10a** RX에서도 FS 간격 66.7 ms, frame# 연속 / **G10b** RX FS 간격은 33.3 ms(30 fps), DONE이 한 프레임 걸러 하나.

#### 먼저 물어볼 것
- "What rate does the receiver see — frame-start interval at the RX, not at the application?"
- "Are the sensor's frame numbers contiguous?"
- "How many buffers are queued, and is auto-exposure on in a dark scene?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| (a) 센서가 실제로 15 fps: 노출 시간 > 프레임 시간 → VTS 늘어남 (저조도 AE) | RX도 66.7 ms, frame# 연속 | 노출 고정, 밝은 장면 | 노출 줄이면 30 fps |
| (a) 센서 PLL/HTS/VTS 설정 오류 (모드 테이블) | 항상 15 | 센서 레지스터 vs 계산 | 계산 fps = 15 |
| (b) 버퍼 큐 부족(1개) → 반환 전 다음 프레임 도착, 폐기 | RX는 30, DONE 절반, 규칙적 | `VIDIOC_REQBUFS` count, 큐 상태 | 버퍼 늘리면 30 |
| (b) ISP/DMA frame skip 설정, re-arm 지연 | 규칙적 1/2 | ISP skip 레지스터, DMA 재무장 시각 | 설정 해제 시 30 |
| (b) 소비자(앱)가 느림 | CPU 부하 | 앱 처리 시간 | 큐는 차 있음 |

#### 측정 · 계측
- 핵심은 **"rate가 반으로 줄어드는 구간"**: 센서 출력(스코프로 FS burst 간격 또는 RX FS 시각) → RX → DONE → 앱. 코드 출력이 그대로 구분: G10a `FS interval 66.67 ms = x2.00 ... frame# contiguous (gaps 0), DONE 6/FS 6` → SENSOR_RATE, G10b `RX sees x1.00 period (30 fps) ... DONE 5/FS 10; delivered pattern D.D.D.D.D.` → DOWNSTREAM_DROP.

#### root cause · fix
- G10a [예시]: 실내 저조도에서 AE가 노출을 50 ms로 → 센서가 프레임을 늘림. fix: 최대 노출을 프레임 시간 이하로 제한(차량 카메라는 고정 fps가 우선), 검증 시 노출 고정.
- G10b [예시]: 앱이 버퍼 1개만 큐 → 버퍼가 사용자 공간에 있는 동안 도착한 프레임은 버림. fix: 버퍼 3–4개, drop은 V4L2 sequence gap으로 측정하도록 테스트 수정.

#### 회귀로 굳히기
- fps 측정 테스트를 **구간별 두 숫자**(RX FS rate, DONE rate)와 **frame# 연속성**으로 바꾸고, 밝기(저조도/고조도) × 노출 모드 × 버퍼 수 매트릭스.

#### 30초 영어 답
"Half the frame rate has two very different causes, and the receiver tells them apart. If the frame-start interval at the receiver is also 66 milliseconds and the sensor's frame numbers are contiguous, the sensor really is running at 15 — usually exposure longer than the frame time stretching the frame, or a wrong timing table. If the receiver sees 30 but only every other frame completes to memory, frames are lost after the receiver — typically too few buffers queued or a DMA re-arm problem. So I measure the rate at each stage instead of only at the application."

#### 꼬리 질문
- "The app sees 29.97 instead of 30.00. Do you care?"

<details><summary>답</summary>

"It depends on the requirement, but I'd check it against the sensor's clock tolerance: the sensor's frame rate comes from its own oscillator and PLL settings, so a 0.1 percent offset could be the line-length rounding in the mode table or an oscillator error. If cameras must be synchronized, they should be triggered, and then the rate should match the trigger exactly — any difference is a free-running camera."
한국어 핵심: 0.1% 차이 = 모드 테이블 반올림/오실레이터, 동기 카메라면 trigger와 정확히 같아야 함(다르면 free-run).

</details>

- "How do you prove zero drops over 24 hours?"

<details><summary>답</summary>

"Count at every stage with hardware counters and frame numbers, not by watching the stream: sensor frame numbers contiguous, receiver frame starts equal frame ends, DMA completions equal frame starts, and the V4L2 sequence without gaps. At 30 fps that's about 2.6 million frames per camera per day, and I'd log any mismatch with its timestamp so it can be correlated with load or temperature."
한국어 핵심: 구간별 카운터·frame#·V4L2 seq로 증명, 30 fps × 24 h ≈ 260만 프레임/카메라, 불일치는 시각과 함께 기록.

</details>

## 6. 꼬리 질문 체인

### 체인 1 — "The camera stream drops frames and the RX reports ECC errors."

Q → 꼬리1 "They're all single-bit corrected errors — do you care?" → 꼬리2 "It only happens above 95 °C." → 꼬리3 "How do you turn that into a production screen?"

<details><summary>Q. Frames drop and the receiver reports ECC errors — debug it.</summary>

"First, are the drops caused by the errors or just coincident? I'd line up per-frame counters: which frames had ECC or CRC errors and which frames never completed to memory. If they match, it's the link; if drops happen on clean frames with line-buffer-full, it's downstream bandwidth. For link errors I check the type — SoT points at HS entry, rising single-bit ECC at margin — and whether it's one lane, one camera, or all."
한국어 핵심: drop과 에러가 같은 프레임인지부터 → 링크 vs 하류 분리, 에러 종류·레인·카메라로 좁히기.

</details>

<details><summary>꼬리1. All single-bit corrected — do you care?</summary>

"Yes. Corrections are a margin measurement: the same error rate hits the payload where CRC can only detect, and three-bit header errors can be miscorrected into a wrong VC or word count. In validation, the pass criterion is zero corrections at every corner."
한국어 핵심: ECC1 = 마진 지표, payload CRC drop·3비트 오정정 위험 → 기준은 0.

</details>

<details><summary>꼬리2. Only above 95 °C.</summary>

"Then I heat one component at a time to see whose temperature matters — SoC, camera module, or cable and connectors — and re-run the settle sweep and a lane-rate step-down at temperature. If the settle window shifts with SoC temperature, it's receiver PHY characterization; if the deserializer's link counters rise, it's the SerDes link or power-over-coax."
한국어 핵심: 부품별 가열로 온도 주인 찾기, 고온 settle sweep·속도↓, deser 카운터로 SerDes 구분.

</details>

<details><summary>꼬리3. Production screen?</summary>

"I'd turn the characterization into a short margin test: run the test pattern at a slightly stressed condition — one step higher lane rate or a settle value offset from nominal — for a few seconds and require zero corrections. That catches marginal parts and boards in system-level test without a chamber, and the guard band comes from the PVT data."
한국어 핵심: 약간 가혹한 조건(속도↑·settle offset)에서 짧은 TPG 테스트로 SLT 스크린, guard band는 PVT 데이터로.

</details>

### 체인 2 — "How do you know where in the pipeline a frame got lost?"

Q → 꼬리1 "The ISP has no frame counter." → 꼬리2 "It happens once a day."

<details><summary>Q. Where in the pipeline did the frame get lost?</summary>

"I count at every boundary on one time base: sensor frame numbers in the frame-start packets, deserializer link status, receiver frame start, end and packet counts, line count per VC, DMA completions, and the V4L2 sequence. The first stage where counts diverge is the culprit. My analyzer does exactly that from the receiver event log."
한국어 핵심: 경계마다 같은 시간축 카운트 → 처음 어긋나는 구간이 범인.

</details>

<details><summary>꼬리1. The ISP has no frame counter.</summary>

"Then I use what surrounds it: the receiver's output count going in, the DMA write completions coming out, and an interrupt or a GPIO toggle at each ISP frame-done if firmware can add one. Or I bypass the ISP — RAW straight to DDR — and see whether the loss disappears. And I'd file a design-for-debug request for a counter in the next revision."
한국어 핵심: 앞뒤 카운터·GPIO 토글·ISP bypass로 대체, 다음 리비전에 카운터 요청(DFD).

</details>

<details><summary>꼬리2. It happens once a day.</summary>

"Then I need a trigger, not a log dump: on a frame-number gap or a DONE mismatch, firmware freezes the counters, saves a snapshot of the receiver status and the last few hundred events from a ring buffer, and pulses a GPIO so a scope in segmented mode captures the lanes at that moment. And I'd raise the rate of the suspected condition — more cameras, more DDR load, higher temperature — to turn once a day into once an hour."
한국어 핵심: 하루 1회면 트리거 기반 캡처(카운터 freeze·링버퍼·GPIO→스코프) + 조건 강화로 빈도↑.

</details>

### 체인 3 — "How would you validate multi-camera synchronization?" (면접관 특허 영역)

Q → 꼬리1 "Where would you take the timestamp?" → 꼬리2 "Software says the cameras are within 10 µs. Do you believe it?" → 꼬리3 "Now add LiDAR."

<details><summary>Q. Validate multi-camera synchronization.</summary>

"Define the requirement first — skew between cameras, and accuracy against the vehicle time base. All timestamps come from one PTP-disciplined hardware clock; cameras are triggered by FSYNC pulses generated from that clock. Then I measure per-camera period jitter, frame-number gaps and per-frame inter-camera skew over long runs, and validate the whole chain physically with an LED strobe all cameras can see."
한국어 핵심: 요구 정의 → 하나의 PTP HW 시계 + FSYNC → jitter·gap·skew 통계 → LED 스트로브로 물리 검증.

</details>

<details><summary>꼬리1. Where would you take the timestamp?</summary>

"As close to the hardware event as possible: a latch of the PTP clock at frame start in the CSI-2 receiver, not in an interrupt handler or at DMA completion. Then I convert it to start of exposure using a measured per-mode offset, and say in the data which definition it is."
한국어 핵심: RX SoF에서 PHC HW latch, 모드별 측정 오프셋으로 노출 시작 환산, 정의 명시.

</details>

<details><summary>꼬리2. Software says within 10 µs — believe it?</summary>

"Not without a physical reference. Software only compares numbers that share the same errors — a wrong per-sensor latency, a trigger taking effect a frame later, or a path asymmetry are invisible to it. The LED strobe and a scope on the trigger lines measure what actually happened."
한국어 핵심: SW 비교는 같은 오류를 공유 → 물리 기준(LED·스코프) 없이는 못 믿음.

</details>

<details><summary>꼬리3. Now add LiDAR.</summary>

"LiDAR is a rotating sensor, so each camera should expose when the LiDAR beam crosses its field of view; the trigger is derived from the LiDAR's phase on the same PTP time base. I'd validate with a target both sensors see and compare the timestamps of the detections, and watch for the classic whole-second or 18-second offsets from PPS and GPS-versus-UTC handling."
한국어 핵심: 카메라 trigger를 LiDAR 회전 위상에 맞춤(같은 PTP), 공통 타깃으로 검증, 1 s/18 s 오프셋 함정 ([deck §7 #13](2026-10-25_rivian_hm_screen_prep.html)).

</details>

### 체인 4 — "The camera is behind a SerDes link. How do you split the problem?"

Q → 꼬리1 "The deserializer reports lock but frames are corrupt." → 꼬리2 "It only fails with the longest cable."

<details><summary>Q. How do you split SerDes problems from CSI-2 problems?</summary>

"Treat it as two links. First the deserializer: local I2C works, forward link locked, its link error counters clean, back-channel I2C to the serializer and sensor works. Then use the deserializer's own test pattern into the SoC — if that's clean, the SoC receiver and the board hop are fine and the problem is on the camera side."
한국어 핵심: 두 링크로 나눔 → deser lock·카운터·역방향 I2C, deser TPG로 SoC 구간 분리.

</details>

<details><summary>꼬리1. Lock is reported but frames are corrupt.</summary>

"Lock only says the clock recovered, not that the data is clean. I'd read the deserializer's link error or CRC counters, then compare the sensor's test pattern through the whole chain with the deserializer's pattern into the SoC. If only the full chain fails, it's the serializer link margin or the sensor's CSI-2 output into the serializer."
한국어 핵심: lock ≠ 데이터 정상 → deser 에러 카운터, 전체 체인 TPG vs deser TPG 비교.

</details>

<details><summary>꼬리2. Only with the longest cable.</summary>

"That's channel loss and possibly power: longer coax means more attenuation and more voltage drop on power-over-coax. I'd measure the camera's supply voltage at the module during streaming, check the SerDes link margin indicators if the part has them, and run a cable-length by temperature matrix. Whether Rivian's parts expose eye monitoring I'd check in the datasheet."
한국어 핵심: 긴 케이블 = 감쇠 + PoC 전압 강하 → 모듈 전압 측정, 마진 지표, 길이×온도 매트릭스 [추정].

</details>

## 7. Don 경험 연결

- "My closest experience is the MIPI family itself: at Apple I root-caused RFFE and SPMI issues with a scope and a protocol analyzer, reading the MIPI spec down to the command frames and parity bits — about [숫자 채우기] issues, including [사례 한 줄 채우기]. CSI-2 is faster, but the method is the same: decode, correlate with the analog waveform, then fix."
- "At SK hynix I ran PCIe and NAND interface reliability in thermal chambers, where we tracked corrected-error counts against temperature and margin — exactly the G3 pattern: corrections rise before failures, so we used them as the pass criterion across [숫자 채우기] corners."
- "At Solidigm I brought up Cortex-R and M cores on FPGA before silicon and verified DMA and DRAM paths; stage-by-stage counting — what went in, what came out — is how I localized drops then, and it's how I'd find where a camera frame disappears."
- "I haven't validated a CSI-2 receiver on silicon myself; I'd ramp by reading the receiver IP's error model, building the event-log analyzer I showed, and pairing with the camera team on the sensor side."

## 8. 실습 / 코드

### 8.1 무엇을 하나

- 파일: [csi2_stream_diag.py](onsitePrep/n07/g/csi2_stream_diag.py) (분석기) · [gen_mocks.py](onsitePrep/n07/g/gen_mocks.py) (mock 로그 생성) · [mocks/](onsitePrep/n07/g/mocks/G0_normal.log) (8개 로그) · [run.sh](onsitePrep/n07/g/run.sh) · [run_output.txt](onsitePrep/n07/g/run_output.txt). Python stdlib만.
- 입력 = **RX가 보고한 이벤트 열**: `t_us vc kind arg flags` (kind = FS/FE/LONG/UNK/DONE, flags = ECC1·ECC2·CRC·SOT·LBFULL·ERR). 프레임은 줄 6개로 축소했지만 프레임 주기(33.3 ms)는 실제 값.
- 하는 일: VC별 FS/FE 상태 머신(FS-while-open, FE-without-FS, orphan LONG), 프레임당 줄 수, WC 분포, frame# 연속성(16-bit, 65535 → 1 wrap [추정: 센서마다]), FS 간격/주기 비율, **RX FS 수 vs DMA DONE 수**, 플래그의 프레임별 추세·VC 간 동시성 → **규칙 순서**(구조 → PHY 마진 → 역압 → 설정 → rate)로 진단 코드 1개 + 증거 줄. 각 로그의 `#expect` 와 비교해 PASS/FAIL, 하나라도 틀리면 exit 1.
- [V9](hmScreenPrep/problems/V9_csi2_packet_parse/index.html)와의 차이: V9는 **바이트 → 패킷**(ECC 신드롬, CRC 계산). 여기는 RX가 이미 판정한 **이벤트 → 스트림 진단**(어느 구간·어떤 종류의 고장인가).

### 8.2 실행

```bash
cd onsitePrep/n07/g && ./run.sh      # mock 재생성 → 8개 분석 → test of the test → run_output.txt
```

### 8.3 실제 출력 (run_output.txt)

```text
===== generate mocks =====
wrote mocks/G0_normal.log (72 events)
wrote mocks/G3_ecc_vs_temp.log (72 events)
wrote mocks/G4_diagonal_tear.log (54 events)
wrote mocks/G6_four_cam_drop.log (282 events)
wrote mocks/G7_vc_collision.log (102 events)
wrote mocks/G8_fe_lost_merge.log (62 events)
wrote mocks/G10a_sensor_15fps.log (54 events)
wrote mocks/G10b_buffer_starved.log (85 events)

===== analyze =====
== G0_normal.log  (cfg 30 fps, 6 lines, WC 2400, VCs [0]; 72 events)
  VC0: FS=8 FE=8 DONE=8 (err 0)  lines/frame 6..6 (cfg 6)  WC {2400x48} (cfg 2400)
        frame# gaps 0 backward 0  FS interval 33.33 ms = x1.00 of 33.33 ms  sync: FS-while-open 0 FE-without-FS 0 orphan-LONG 0
        flags: ECC1 0  ECC2 0  CRC 0  SOT 0  LBFULL 0
  diagnosis: OK
    - pairing, lines, WC, frame numbers, interval, delivery and flags all match cfg
  expected OK -> PASS

== G3_ecc_vs_temp.log  (cfg 30 fps, 6 lines, WC 2400, VCs [0]; 72 events)
  VC0: FS=8 FE=8 DONE=8 (err 2)  lines/frame 6..6 (cfg 6)  WC {2400x48} (cfg 2400)
        frame# gaps 0 backward 0  FS interval 33.33 ms = x1.00 of 33.33 ms  sync: FS-while-open 0 FE-without-FS 0 orphan-LONG 0
        flags: ECC1 16  ECC2 0  CRC 2  SOT 0  LBFULL 0
  diagnosis: SI_MARGIN
    - VC0: PHY-layer flags 18 (ECC1 16, ECC2 0, CRC 2, SOT 0)
    - ECC1 per frame [0, 0, 1, 1, 2, 3, 4, 5] -> rising over the run; CRC per frame [0, 0, 0, 0, 0, 0, 1, 1]
    - structure clean: gaps 0, sync errors 0, WC {2400: 48}
    - ECC1 = header corrected (link still works) is the early warning; CRC = payload hit
    - next: per-lane SoT/err, lane rate one step down, HS-settle sweep at this temperature
  expected SI_MARGIN -> PASS

== G4_diagonal_tear.log  (cfg 30 fps, 6 lines, WC 2400, VCs [0]; 54 events)
  VC0: FS=6 FE=6 DONE=6 (err 0)  lines/frame 6..6 (cfg 6)  WC {2410x36} (cfg 2400)
        frame# gaps 0 backward 0  FS interval 33.33 ms = x1.00 of 33.33 ms  sync: FS-while-open 0 FE-without-FS 0 orphan-LONG 0
        flags: ECC1 0  ECC2 0  CRC 0  SOT 0  LBFULL 0
  diagnosis: WC_MISMATCH
    - VC0: 36/36 long packets WC=2410, cfg 2400 (+10 B/line)
    - no ECC/CRC/SOT (0), FS/FE pair 6/6, gaps 0 -> link is clean, this is configuration
    - each line lands with a constant byte offset against the stride -> diagonal shear
    - next: sensor output width/DT/bpp vs RX DT + DMA line length/stride; media-ctl pad formats
  expected WC_MISMATCH -> PASS

== G6_four_cam_drop.log  (cfg 30 fps, 6 lines, WC 2880, VCs [0, 1, 2, 3]; 282 events)
  VC0: FS=8 FE=8 DONE=7 (err 0)  lines/frame 6..6 (cfg 6)  WC {2880x48} (cfg 2880)
        frame# gaps 0 backward 0  FS interval 33.33 ms = x1.00 of 33.33 ms  sync: FS-while-open 0 FE-without-FS 0 orphan-LONG 0
        flags: ECC1 0  ECC2 0  CRC 0  SOT 0  LBFULL 2
  VC1: FS=8 FE=8 DONE=6 (err 0)  lines/frame 6..6 (cfg 6)  WC {2880x48} (cfg 2880)
        frame# gaps 0 backward 0  FS interval 33.33 ms = x1.00 of 33.33 ms  sync: FS-while-open 0 FE-without-FS 0 orphan-LONG 0
        flags: ECC1 0  ECC2 0  CRC 0  SOT 0  LBFULL 4
  VC2: FS=8 FE=8 DONE=7 (err 0)  lines/frame 6..6 (cfg 6)  WC {2880x48} (cfg 2880)
        frame# gaps 0 backward 0  FS interval 33.33 ms = x1.00 of 33.33 ms  sync: FS-while-open 0 FE-without-FS 0 orphan-LONG 0
        flags: ECC1 0  ECC2 0  CRC 0  SOT 0  LBFULL 2
  VC3: FS=8 FE=8 DONE=6 (err 0)  lines/frame 6..6 (cfg 6)  WC {2880x48} (cfg 2880)
        frame# gaps 0 backward 0  FS interval 33.33 ms = x1.00 of 33.33 ms  sync: FS-while-open 0 FE-without-FS 0 orphan-LONG 0
        flags: ECC1 0  ECC2 0  CRC 0  SOT 0  LBFULL 4
  diagnosis: BACKPRESSURE_DROP
    - LBFULL per VC {0: 2, 1: 4, 2: 2, 3: 4}; frames received-not-delivered per VC {0: 1, 1: 2, 2: 1, 3: 2}
    - frame index -> VCs hit: 4:[0, 1, 2, 3], 5:[1, 3]
    - PHY-layer flags total 0, frame# gaps 0 -> link delivered every frame
    - drops are simultaneous across VCs and in a burst -> shared downstream (DDR/NoC QoS)
    - next: DDR bandwidth/QoS counters during the burst; line-buffer watermark; RX recovery after LBFULL
  expected BACKPRESSURE_DROP -> PASS

== G7_vc_collision.log  (cfg 30 fps, 6 lines, WC 2880, VCs [0, 1]; 102 events)
  VC0: FS=12 FE=12 DONE=6 (err 6)  lines/frame 12..12 (cfg 6)  WC {2880x72} (cfg 2880)
        frame# gaps 2300 backward 6  FS interval 15.15 ms = x0.45 of 33.33 ms  sync: FS-while-open 6 FE-without-FS 6 orphan-LONG 0
        flags: ECC1 0  ECC2 0  CRC 0  SOT 0  LBFULL 0
  VC1: no frames at all (FS=0)
  diagnosis: VC_COLLISION
    - VC0: FS-while-open 6x and frame# goes backwards 6x -> two independent frame counters share VC0: [500, 40, 501, 41, 502, 42]
    - expected VC(s) [1] are silent -> their camera is mapped onto VC0
    - lines/frame [12, 12, 12, 12] = 2x cfg 6: lines of both cameras interleave
    - next: read the deserializer VC-mapping per input port; check sensor VC registers
  expected VC_COLLISION -> PASS

== G8_fe_lost_merge.log  (cfg 30 fps, 6 lines, WC 2400, VCs [0]; 62 events)
  VC0: FS=7 FE=6 DONE=6 (err 1)  lines/frame 6..12 (cfg 6)  WC {2400x42} (cfg 2400)
        frame# gaps 0 backward 0  FS interval 33.33 ms = x1.00 of 33.33 ms  sync: FS-while-open 1 FE-without-FS 0 orphan-LONG 0
        flags: ECC1 0  ECC2 0  CRC 0  SOT 0  LBFULL 0
  unattributed packets (uncorrectable header): 1
  diagnosis: FE_LOST_MERGE
    - VC0: FS arrived while frame #303 was still open; that frame closed with 12 lines (cfg 6)
    - 1 uncorrectable header (ECC2) inside it at t=100.100 ms = where FE was due -> the FE was lost on the wire
    - DONE=6 for FS=7; merged buffer flagged ERR 1x
    - next: SI margin on the link (G3 path) + RX must resync on FS (frame-sync error recovery)
  expected FE_LOST_MERGE -> PASS

== G10a_sensor_15fps.log  (cfg 30 fps, 6 lines, WC 2400, VCs [0]; 54 events)
  VC0: FS=6 FE=6 DONE=6 (err 0)  lines/frame 6..6 (cfg 6)  WC {2400x36} (cfg 2400)
        frame# gaps 0 backward 0  FS interval 66.67 ms = x2.00 of 33.33 ms  sync: FS-while-open 0 FE-without-FS 0 orphan-LONG 0
        flags: ECC1 0  ECC2 0  CRC 0  SOT 0  LBFULL 0
  diagnosis: SENSOR_RATE
    - VC0: FS interval 66.67 ms = x2.00 period at the RX, frame# contiguous (gaps 0), DONE 6/FS 6
    - the RX receives every frame the sensor sends -> the sensor itself runs slow
    - next: sensor VTS/HTS + PLL vs exposure (auto-exposure in low light stretches the frame)
  expected SENSOR_RATE -> PASS

== G10b_buffer_starved.log  (cfg 30 fps, 6 lines, WC 2400, VCs [0]; 85 events)
  VC0: FS=10 FE=10 DONE=5 (err 0)  lines/frame 6..6 (cfg 6)  WC {2400x60} (cfg 2400)
        frame# gaps 0 backward 0  FS interval 33.33 ms = x1.00 of 33.33 ms  sync: FS-while-open 0 FE-without-FS 0 orphan-LONG 0
        flags: ECC1 0  ECC2 0  CRC 0  SOT 0  LBFULL 0
  diagnosis: DOWNSTREAM_DROP
    - VC0: RX sees x1.00 period (30 fps), frame# gaps 0, but DONE 5/FS 10; delivered pattern D.D.D.D.D.
    - no LBFULL, no PHY flags (0) -> frames lost after the RX, regularly
    - next: buffers queued (VIDIOC_REQBUFS count), DMA re-arm latency, ISP frame-skip setting
  expected DOWNSTREAM_DROP -> PASS

SUMMARY 8/8 PASS
exit=0

===== test of the test: G4 log relabelled '#expect OK' must FAIL =====
  diagnosis: WC_MISMATCH
  expected OK -> FAIL
SUMMARY 0/1 PASS
analyzer exit=1 (1 = mismatch caught)
```

### 8.4 출력 읽는 법

| 케이스 | 결정적인 줄 | 해석 |
|---|---|---|
| G0 정상 | `FS=8 FE=8 DONE=8`, `gaps 0`, `x1.00`, 모든 flag 0 | 기준선 — frame# 65532 → 65535 → 1 wrap을 gap으로 세지 않음 |
| G3 (추가) | `ECC1 per frame [0, 0, 1, 1, 2, 3, 4, 5] -> rising`, CRC는 마지막 2 프레임 | 구조는 깨끗한데 PHY 플래그가 **추세**로 증가 = 마진 소진, CRC가 뒤따름 |
| G4 | `WC {2410x36} (cfg 2400)`, PHY 0 | 링크 깨끗 + 일정한 WC 차이 = 설정 → 대각선 shear |
| G6 | `frame index -> VCs hit: 4:[0, 1, 2, 3]`, `PHY-layer flags total 0, frame# gaps 0` | 링크는 모든 프레임을 받음(gap 0) → RX→DDR 구간에서 **동시에** 손실 = 공유 자원 |
| G7 | `[500, 40, 501, 41, 502, 42]`, `VC1: no frames`, lines 12 | 두 카운터 교차 = 두 소스가 한 VC. `gaps 2300`·`x0.45` 는 교차 때문에 의미 없는 값 — 구조 규칙을 먼저 보는 이유 |
| G8 | `FS arrived while frame #303 was still open`, `ECC2 ... where FE was due` | 구조 오류(병합)의 원인이 PHY 오류(FE 헤더 2-bit) — 두 층을 연결 |
| G10a | `x2.00`, `gaps 0`, `DONE 6/FS 6` | RX에서 이미 15 fps + frame# 연속 → 센서 자체 |
| G10b | `x1.00`, `DONE 5/FS 10`, `D.D.D.D.D.` | RX는 30 fps, 규칙적 1/2 전달 → RX 이후(버퍼·DMA) |
| test of the test | `expected OK -> FAIL`, `exit=1` | 라벨이 틀리면 실제로 FAIL을 내는지 확인 — 체커 자체 검증 |

- 한계 `[미검증]`: 실제 RX IP의 이벤트 로그 형식이 아님(가상의 공통 형식). 실기에선 PG232 ISR/카운터 폴링이나 드라이버 trace를 이 형식으로 변환하는 어댑터가 필요. LS/LE, embedded data DT, VCX(VC 4–31)는 다루지 않음. G1/G2/G5/G9는 이벤트 로그만으로 진단이 안 되는(스코프·이미지·물리 기준이 필요한) 시나리오라 mock 없음 — G9는 [V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html)가 timestamp 쪽을 담당.

## 9. 자주 틀리는 것

1. **"ECC 에러 = 데이터 깨짐"으로 뭉뚱그림** — ECC1(정정, 마진 경고) · ECC2(헤더 손실 → 패킷/FE 손실) · CRC(payload) · SoT(PHY 진입)는 계층이 다르다. 종류별로 말하기.
2. **앱 fps만 보고 "카메라가 drop한다"** — 센서·RX·DMA·앱 중 **어디서** 줄었는지 구간별 숫자 없이는 결론 불가 (G6, G10).
3. **settle을 상수로 생각** — ns 창은 거의 같아도 카운터 값은 레인 속도에 비례. 모드 바꾸면 재계산 (G2).
4. **ECC1을 "고쳐졌으니 OK"** — 같은 BER이 payload엔 CRC drop, 3비트면 헤더 오정정. 검증 기준은 0 (G3, [N06E E3 3-bit 실험](2026-11-01_N06E_plans_highspeed_io_sensor.html)).
5. **deser "lock" = 정상이라고 가정** — lock은 클럭 복원일 뿐, 링크 에러 카운터·TPG로 데이터 확인.
6. **에러 0인데 영상 이상 → SI부터 의심** — 에러 0 + 이상 = 거의 설정(WC/stride, Bayer 순서, DT, VC 매핑) (G4, G5, G7).
7. **동일 센서 여러 대 = 기본 VC 같음을 잊음** — deser 재매핑 누락 (G7).
8. **timestamp 정의 없이 "동기됐다"** — SOE/SoF/EOF/DMA done 중 무엇인지, rolling shutter 줄 시각까지. SW가 보고하는 skew는 물리 기준(LED·스코프)으로 확인 (G9).
9. **sequence 번호로 카메라 간 짝짓기** — 시작 시 drop 하나면 영원히 1 프레임 어긋남. 짝짓기는 HW timestamp nearest-match (G9).
10. **에러 주입 시 "검출"만 확인** — 차량에선 **복구**(다음 FS에서 정상, 피해 1 프레임, hang 0)가 더 중요 (G8).

## 10. 출처

1. AMD PG232 MIPI CSI-2 RX Subsystem — Interrupt Status Register (bit names: Frame Received, WC corruption, Incorrect lane configuration, Stream line buffer full, Stop state, ErrSoTHS, ErrSotSyncHS, ECC 1/2-bit, ErrCrc, ErrID, Frame sync/level error VCn, Pending write in internal FIFO): https://docs.amd.com/r/en-US/pg232-mipi-csi2-rx/Interrupt-Status-Register ; Lane<n> Information Registers: https://docs.amd.com/r/en-US/pg232-mipi-csi2-rx/Lane-n-Information-Registers ; Image Information 1 Registers (line count, byte count): https://docs.amd.com/r/en-US/pg232-mipi-csi2-rx/Image-Information-1-Registers-VC0-to-VC15 ; Monitoring Registers (packet count): https://docs.amd.com/r/en-US/pg232-mipi-csi2-rx/Monitoring-Registers
2. Linux include/linux/phy/phy-mipi-dphy.h — D-PHY timing min/max (lpx, hs_prepare, hs_settle, hs_trail, hs_exit, clk_prepare, clk_settle, clk_pre, clk_post, clk_trail, clk_miss, init): https://raw.githubusercontent.com/torvalds/linux/master/include/linux/phy/phy-mipi-dphy.h
3. Linux kernel docs — Pixel data transmitter and receiver drivers (LP-11 ≥ 100 µs 창, power-on LP-00, pre_streamon, link frequency, pixel_rate 공식, stream 중단 동작): https://docs.kernel.org/driver-api/media/tx-rx.html
4. Linux drivers/media/platform/nxp/imx-mipi-csis.c — 이벤트 카운터 이름(SOT n Error, Lost Frame Start/End Error, FIFO Overflow Error, ECC Error, CRC Error, Unknown ID Error), log_status 출력, debugfs ths_settle/tclk_settle, hs_settle 계산식: https://raw.githubusercontent.com/torvalds/linux/master/drivers/media/platform/nxp/imx-mipi-csis.c
5. Linux kernel docs — V4L2 Buffers (sequence에 drop 포함, V4L2_BUF_FLAG_ERROR, TSTAMP_SRC_EOF/SOE, TIMESTAMP_MONOTONIC): https://docs.kernel.org/userspace-api/media/v4l/buffer.html
6. MIPI Alliance — CSI-2 specification page (v4.2 2025-12, 32 VC, D/C/A-PHY, A-PHY 15 m): https://www.mipi.org/specifications/csi-2
7. Microchip — D-PHY RX High-Speed Data Reception Mode (LP-11 → LP-01 → LP-00 후 HS, leader sequence로 동기, LP-11에서 종료): https://onlinedocs.microchip.com/oxy/GUID-C0DEC68F-9589-43E1-B26B-4C3E38933283-en-US-1/GUID-2D508B75-62CF-416B-AFDB-FC5C995C3C79.html
8. Tektronix (China) — MIPI 系列总线介绍与测试 white paper (HS 차동 약 200 mV, LP 1.2 V 스윙·최대 10 Mb/s, LP-00/01/10/11 상태, 버스트 사이 LP-11 복귀): https://WWW.TEK.COM.CN/-/media/china-marketing-documents/campaign/automotive/tek-mipi-white-paper.pdf
9. TI DS90UB960-Q1 product page (FPD-Link III 4 × 4.16 Gb/s, CSI-2 2 포트 1–4 lane 최대 1.6 Gb/s, back channel I2C 1 Mb/s, frame sync 생성기, line-fault, BIST): https://www.ti.com/product/DS90UB960-Q1
10. Linux DT binding maxim,max96712 (GMSL2/GMSL1 quad deserializer, CSI-2 D-PHY/C-PHY out) 및 검색 요약 (GMSL2 3/6 Gb/s 정방향, 187.5 Mb/s 역방향): https://www.kernel.org/doc/Documentation/devicetree/bindings/media/i2c/maxim%2Cmax96712.yaml
11. Efinix — T20 D-PHY Timing Parameters (THS-PREPARE + THS-ZERO ≥ 145 ns + 10 UI): https://www.efinixinc.com/docs-html/dst20/topics/ref_mipi_timing.html
12. Lattice answer record 5874 — D-PHY TX sync byte 0xB8 per lane: https://www.latticesemi.com/support/answerdatabase/5/8/7/5874
13. Linux kernel docs — Sub-device interface (streams, routing, VIDIOC_SUBDEV_S_ROUTING for multiplexed buses): https://docs.kernel.org/userspace-api/media/v4l/dev-subdev.html
