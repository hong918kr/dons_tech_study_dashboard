# N07F · Ethernet 디버그 (MAC/PHY · RGMII/SGMII · 100/1000BASE-T1)
<!-- theme: pastel -->
> **묶음**: N07 인터페이스 디버그 · **우선**: P0 ★ (면접관 Manjiang = Marvell Ethernet PHY ASIC 설계 경력 → PHY 계층 용어를 정확히) · **목록**: [N07 인덱스](2026-11-01_N07_interface_debug_scenarios.html) · **이전/다음**: [N07E DDR 메모리 디버그](2026-11-01_N07E_ddr_memory_debug.html) / [N07G MIPI CSI-2 카메라 디버그](2026-11-01_N07G_mipi_csi2_camera_debug.html) · **관련**: [LP00A §4 Ethernet 처음부터](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · [BV10 §2.5, 체인 C](2026-11-01_BV10_board_interface_tests.html) · [N06E E2 Ethernet 테스트 플랜](2026-11-01_N06E_plans_highspeed_io_sensor.html) · [최종 훑어보기 §5](2026-10-30_final_skim.html) · [deck §7 #10, #14](2026-10-25_rivian_hm_screen_prep.html) · [V10 Ethernet FCS](hmScreenPrep/problems/V10_eth_frame_fcs/index.html) · [V6 PRBS/CRC](hmScreenPrep/problems/V6_prbs_crc_checker/index.html) · [L3 RFC 2544](hmScreenPrep/problems/L3_rfc2544_throughput/index.html) · [L4 PPS discipline](hmScreenPrep/problems/L4_pps_time_discipline/index.html) · **코드**: [onsitePrep/n07/f/](onsitePrep/n07/f/run.sh) — [ethdiag.py](onsitePrep/n07/f/ethdiag.py) · [실행 출력](onsitePrep/n07/f/run_output.txt)

## 0. TL;DR

- 이 페이지로 답할 것: **"Link is up but no packets pass"**, "MDIO reads 0xFFFF", "T1 link never comes up", "FCS errors / link flaps / RX drops / PTP bias / SQI drops in the chamber" — 증상 → **계층(PHY-PCS / xMII / MAC / DMA·드라이버 / 스택)** → 첫 확인 → 확정 방법.
- 기초(프레임, C22 BMCR/BMSR, auto-neg, loopback 사다리, BER 통계, RFC 2544 숫자)는 [LP00A §4](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · [BV10 §2.5](2026-11-01_BV10_board_interface_tests.html) · [N06E E2](2026-11-01_N06E_plans_highspeed_io_sensor.html)에 있다. 여기선 **증상별 진단 + 카운터 해석 + 새 시나리오 10개**.
- **외울 것 3개**
  1. **"Link up" 은 PHY 선로 쪽(PMA/PCS)이 살았다는 뜻일 뿐** — MAC↔PHY 구간(RGMII/SGMII)은 전혀 증명 안 됨. RGMII는 수신단에서 클럭이 데이터보다 **1.0–2.6 ns 늦게**(보통 "2 ns") 와야 하고 [3], 이 지연을 **PHY · MAC · PCB 중 정확히 한 곳**이 넣어야 한다. Linux `phy-mode`는 **PHY 입장**에서 읽는다: `rgmii` = PHY는 지연 안 넣음, `rgmii-id` = PHY가 TX·RX 둘 다 [1][2].
  2. **카운터는 계층을 말해 준다**: PHY symbol error·FEC·SQI = 선로 / FCS·alignment = MAC이 본 비트 오류(선로든 xMII든) / missed·fifo·no-buffer = 호스트가 못 받아감 / rx_dropped·queue drop = 스택 / too-long·length = 크기 정책. 이름은 **드라이버마다 다르다** → 표준 그룹 `ethtool -S eth0 --groups eth-phy eth-mac eth-ctrl rmon` 과 `ip -s -s link` 로 맞춰 읽기 [4][5][6].
  3. **T1은 auto-neg가 선택(Clause 98)이고 보통 안 쓴다** → master/slave를 strap·레지스터로 고정; 둘 다 master/둘 다 slave면 링크 없음 [9]. 링크 품질은 FCS보다 **SQI(0–7, OPEN Alliance TC1)·FEC 정정 카운트**가 먼저 나빠진다 [7].
- 30초 영어 답 — **"Link is up but no packets pass."**: "Link up only proves the PHY's line side trained, so I check the MAC-to-PHY segment next. First, counters on both ends: if the PHY's line side is clean but the MAC counts FCS errors — or receives nothing — at gigabit, the prime suspect is RGMII clock-to-data delay, either added twice or not at all; a telltale is that 100 Mb/s works. I compare the Device Tree phy-mode against the PHY's delay straps and the MAC's own delay setting, then use PHY loopback: MAC loopback clean and PHY xMII loopback failing localizes it to that interface. I confirm on a scope at the receiver pins — clock edge should sit about 2 ns after the data transition — fix the one place that adds delay, and add a speed-by-delay-mode regression that includes negative cases."

## 1. 🔰 1분 설명

- **비유 (택배 — LP00A와 같은 비유를 한 층 더 쪼갬)**: MAC = 접수 창구(소포 포장·주소·파손 스티커 FCS). PHY = 트럭 운전사. 그런데 운전사 안에도 층이 있다: **PCS = 짐을 규격 상자에 다시 담는 사람(부호화·스크램블·FEC)**, **PMA = 상자를 도로 신호로 바꾸는 사람(PAM3 변조·클럭 복원·에코 제거)**, **PMD/MDI = 도로와 연결된 바퀴(아날로그 앞단·커넥터)**. 창구와 운전사 사이 **RGMII = 하역장**: 4개 레인으로 상자를 넘기는데 "지금 받아!" 신호(클럭)가 상자가 자리 잡은 **뒤**에 와야 한다 — 너무 빨리 오면 반쯤 굴러오는 상자를 집는다. **MDIO = 창구와 운전사 사이 무전기**.
- **정확한 정의**: IEEE 802.3에서 MAC 아래는 **RS(Reconciliation Sublayer: MAC 서비스를 xMII 신호로 매핑)** → **xMII**(MII/RGMII/SGMII…) → PHY = **PCS → PMA → PMD → MDI**. auto-neg가 있다면 별도 하위 기능(BASE-T는 Clause 28, T1은 선택적 Clause 98 [9]). 관리 경로는 MDIO(Clause 22 / 45) [10][11].

```text
 SoC                                              외부 PHY (또는 SoC 내장)                        선로
 ┌────────────────────────────────┐   RGMII: TXC,TX_CTL,TXD[3:0] →   ┌──────────────────────────────┐
 │ CPU ─ DMA ring(DDR) ─ MAC ─ RS ├──────────────────────────────────►│ PCS ── PMA ── PMD ──[MDI]────┼══ 1쌍 UTP ══► 상대 PHY
 │   ▲ IRQ        FCS gen/check   │   ← RXC,RX_CTL,RXD[3:0]          │ 부호화  PAM3   아날로그 앞단 │   (T1: master ↔ slave)
 │   │            PTP timestamp   │   (또는 SGMII: TX±/RX± 1.25 Gbd) │ FEC    CDR·EC  CMC·termination│
 │   └──────── MDIO master ───────┼── MDC (≤2.5 MHz 표준) / MDIO ───►│ C22 regs 0–31 · C45 MMD 1/3/7/30/31│
 └────────────────────────────────┘   (MDIO는 pull-up 필요)          └──────────────────────────────┘
          ▲ 여기 지연(MAC internal delay)      ▲ 여기 지연(PCB 뱀선)      ▲ 여기 지연(PHY RGMII-ID) ← 셋 중 한 곳만!
```

| 용어 | 풀네임 | 쉬운 설명 | 디버그에서 왜 중요 |
|---|---|---|---|
| RS | Reconciliation Sublayer | MAC 신호를 xMII 핀 신호로 바꾸는 얇은 층 | RGMII의 TX_CTL 인코딩(EN/ER) 여기 |
| PCS | Physical Coding Sublayer | 부호화·스크램블·(1000BASE-T1) 80B/81B + RS-FEC [13] | PCS link/block lock, FEC 카운터 |
| PMA | Physical Medium Attachment | 심볼 ↔ 선로 파형, 클럭 복원, 에코 제거 | loc_rcvr_status, SQI의 원천(MSE) |
| PMD / MDI | Physical Medium Dependent / Medium Dependent Interface | 아날로그 드라이버·수신기 / 커넥터 핀 | 케이블·CMC·ESD |
| RGMII | Reduced Gigabit MII | 4비트 × 125 MHz 양 edge + CTL + CLK (12핀) [1] | **2 ns 지연 누가 넣나** |
| SGMII | Serial GMII | 1.25 Gbaud 차동 직렬, 8b/10b, **in-band AN**(링크 상태 전달) | in-band 설정 불일치 → link up·트래픽 0 |
| MDIO / MDC | Management Data I/O / Clock | 관리용 2선 버스 | 0xFFFF = 아무도 응답 안 함 |
| Clause 22 / 45 | 802.3 관리 프레임 형식 | 22 = PHY당 32 레지스터, 45 = MMD별 65536 | T1 PHY 기능은 주로 C45 |
| master / slave | — | 선로 기준 클럭을 누가 내나 | T1은 고정 설정, 같은 역할끼리는 링크 없음 |
| SQI | Signal Quality Index | PHY가 MSE로 매기는 0(최악)–7(최고) 등급 [7] | FCS보다 먼저 떨어짐 |
| FEC | Forward Error Correction (RS-FEC) | 재전송 없이 수신 측이 오류 정정 | 정정 카운트 급증 = margin 소진 |
| PHC | PTP Hardware Clock | MAC/PHY 안 하드웨어 시계 | `ethtool -T`, 타임스탬프 지점 |

- 대표 PHY 계열 숫자 [13]: **1000BASE-T** = 4쌍, 4D-PAM5, 125 MBd, auto-neg 필수(master/slave도 AN으로 결정) · **100BASE-T1** = 802.3bw, Clause 96, 1쌍, 3B2T PAM3, 66.66 MBd, 15 m · **1000BASE-T1** = 802.3bp, Clause 97, 1쌍, PAM3 750 MBd, 80B/81B + RS-FEC, 40 m(type B).

## 2. 정상 기준선

### 2.1 RGMII 타이밍 — 정상과 비정상 (1 Gb/s)

- 125 MHz → 주기 8 ns, **양 edge마다 nibble** → 데이터 한 칸 = **4 ns**. TX_CTL/RX_CTL은 상승 edge에 EN(DV), 하강 edge에 EN XOR ER을 싣는다.
- RGMII 타이밍 [3] (RGMII 스펙 표를 TI SNLA243 · Intel 문서가 인용한 값 — 스펙 원문은 직접 못 봄, 상한을 2.8 ns로 적은 자료도 있음 [추정]): **TskewT** (송신단, 데이터 대비 클럭) −500…+500 ps — 송신단에서는 클럭과 데이터가 **거의 같이** 나감 · **TskewR** (수신단) **1.0 / 1.8(typ) / 2.6 ns** — 수신단에 도착할 땐 클럭이 데이터보다 1–2.6 ns 늦어야 함. 그 차이를 메우는 게 "2 ns 지연".
- 10/100에서는 클럭이 25 MHz(40 ns) / 2.5 MHz(400 ns)라 데이터 칸이 훨씬 넓어 **2 ns 틀려도 대부분 동작** → "**100M은 되는데 1G만 안 됨**"이 RGMII 지연 버그의 지문 [1].

```text
 수신단(PHY→MAC 방향 RX) 핀에서 보는 그림, 1 칸 = 4 ns
 RXD    ══X═════ D0 ═════X═════ D1 ═════X═════ D2 ═════X══
 정상 RXC  ________/‾‾‾‾‾‾‾‾‾‾‾‾\____________/‾‾‾‾      edge가 칸 가운데(전이 후 ~2 ns) → setup·hold 둘 다 ~2 ns
 지연 누락 RXC ____/‾‾‾‾‾‾‾‾‾‾‾‾\____________/‾‾‾‾          edge가 전이 바로 위(0 ns) → 비트가 반반, FCS 폭발 또는 수신 0
 지연 중복 RXC __________/‾‾‾‾‾‾‾‾‾‾‾‾\____________/        edge가 ~4 ns 뒤 = 다음 전이 위 → 역시 깨짐(또는 한 nibble 밀림)
```

- 핵심 통찰: 지연 중복(≈4 ns)은 4 ns 칸에서 **다음 전이 근처**라 누락과 똑같이 나쁘다 — "조금 더 넣으면 더 안전" 이 아님.

### 2.2 MDIO 프레임 — 정상 (Clause 22 / 45) [10]

```text
 Clause 22 read (64 MDC 클럭 ≈ 25.6 µs @ 2.5 MHz)
 MDIO: [PRE 32×'1'][ST 01][OP 10][PHYAD 5][REGAD 5][TA Z0][DATA 16]
                                                    └ PHY가 TA 두 번째 비트부터 선을 잡는다 → 아무도 안 잡으면 pull-up 때문에 DATA = 0xFFFF
 Clause 45: ST = 00 · OP 00 = address 프레임(16-bit 레지스터 주소 설정) → OP 11 read / 01 write (/ 10 post-read-increment)
            PHYAD 5 bit(=PRTAD) + DEVAD 5 bit(MMD: 1 PMA/PMD · 3 PCS · 7 AN · 30/31 vendor [11])  → 2단계 접근
 MDC: 표준은 최소 주기 400 ns (≤ 2.5 MHz); 더 빠른 MDC를 지원하는 PHY도 있음 [10]
```

### 2.3 정상 로그 · 출력 [예시 출력]

```text
[예시 출력]
$ dmesg | grep -iE 'phy|link is'
macb ff0e0000.ethernet eth0: PHY [ff0e0000.ethernet-ffffffff:01] driver [<PHY 드라이버 이름>] (irq=POLL)
macb ff0e0000.ethernet eth0: Link is Up - 1Gbps/Full - flow control off
$ cat /sys/class/net/eth0/phydev/phy_interface
rgmii-id
$ cat /sys/class/net/eth0/phydev/phy_id
0x2000a284
$ ethtool eth0 | egrep 'Speed|Duplex|master-slave|Link detected|SQI'
	Speed: 1000Mb/s
	Duplex: Full
	master-slave cfg: forced master
	master-slave status: master
	Link detected: yes
	SQI: 7/7
$ cat /sys/class/net/eth0/carrier_down_count
0
$ ethtool -S eth0 --groups eth-mac | egrep 'FramesReceivedOK|FrameCheck|Alignment|TooLong'
eth-mac-FramesReceivedOK: 4711250
eth-mac-FrameCheckSequenceErrors: 0
eth-mac-AlignmentErrors: 0
eth-mac-FrameTooLongErrors: 0
```

- `phy_interface` · `phy_id` sysfs: DT에서 실제 무엇이 적용됐는지 확인하는 가장 빠른 길 (부트로더/DT overlay가 바꿨을 수 있음). PHY ID `0x2000a284` = DP83TG720S (Linux 드라이버 값, [BV10 §2.5.2](2026-11-01_BV10_board_interface_tests.html)).
- **정상 = 오류 카운터 delta 0, carrier_down_count 증가 0, SQI max 유지**. 절대값이 아니라 **테스트 전후 delta**로 판단 (카운터는 interface down/up에도 유지되어야 한다 [4]).
- 표준 그룹 출력 형식(`eth-mac-` 접두어)은 ethtool 버전에 따라 다를 수 있음 [추정].

## 3. 관찰 지점 지도

| 무엇 | 어디서 | 도구 | 정상값 | 이상하면 의미 |
|---|---|---|---|---|
| PHY 존재·응답 | C22 reg 2/3 PHYSID, `/sys/class/net/eth0/phydev/phy_id` | phytool, sysfs, dmesg | 데이터시트 ID | 0xFFFF 무응답 · 0x0000 MDIO low 고착 |
| PHY 링크 (선로) | C22 BMSR bit2(latch-low, 2번 읽기), C45 PCS status | phytool, `ethtool eth0` | 1 · `Link detected: yes` | 첫 읽기 0 = 그사이 끊긴 적 있음 |
| T1 역할 | C45 MMD1 BASE-T1 PMA/PMD control(master bit) · strap | `ethtool eth0` master-slave cfg/status | 상대와 반대 | 같은 역할 → 링크 없음 |
| 선로 품질 | PHY SQI (vendor reg, MSE 기반) | `ethtool eth0` SQI 줄 [8] | 7/7 근처, 온도로 안 변함 | 하락 = margin 소진 |
| FEC (1000BASE-T1) | PHY vendor 카운터 | `ethtool --phy-statistics eth0` [6] (드라이버 지원 시) | 정정 낮고 일정, 미정정 0 | 정정 급증 = 곧 FCS 터짐 |
| xMII 모드 | DT `phy-mode`, `rx/tx-internal-delay-ps` [2], PHY delay strap | sysfs `phy_interface`, 회로도 | 한 곳만 지연 | 중복/누락 |
| MAC 수신 오류 | FCS/CRC, alignment, length, too-long | `ethtool -S --groups eth-mac rmon`, `ip -s -s link` | delta 0 | 표 §4 |
| 호스트 수신 한계 | rx_missed, rx_fifo/over, no-buffer | `ip -s -s link`(missed), `ethtool -S` | delta 0 | ring/IRQ/DDR |
| 스택 드롭 | rx_dropped, `/proc/net/softnet_stat` 2·3열(drop, time_squeeze), UDP RcvbufErrors | cat, `nstat -az` | 0 | 소켓 버퍼·NAPI budget |
| 링크 이력 | `/sys/class/net/eth0/carrier_down_count`, dmesg "Link is Down" | cat, dmesg -T | 증가 0 | flap 주기 분석 |
| ring·coalescing | ring 크기, rx-usecs/rx-frames | `ethtool -g`, `ethtool -c` [6] | 설계값 | 작은 ring + 긴 IRQ 지연 |
| offload | rx/tx checksum, tso, gso, rxvlan, txvlan [6] | `ethtool -k` | 설계값 | 큰 패킷·VLAN만 실패 시 bisect |
| 타임스탬프 | PHC, HW timestamp 모드 | `ethtool -T`, `ptp4l -m`, PPS 출력 | HW 모드 | SW 타임스탬프로 떨어짐 |
| 전기 | RGMII RXC vs RXD 수신단 핀, MDC/MDIO 파형, PHY 전원·25 MHz ref | scope(≥1 GHz, 짧은 접지) | §2.1 그림 | skew·링잉·droop |
| 선로 물리 | 케이블 open/short/거리 | `ethtool --cable-test eth0` [6] | OK | 결함·거리 |

### 3.1 첫 5분 증거 스냅샷 (순서대로, 양쪽 끝 모두)

```bash
dmesg -T | grep -iE 'phy|link is|mdio|watchdog|timed out'      # 1. PHY attach 성공? 링크 이벤트 시각?
cat /sys/class/net/eth0/phydev/{phy_id,phy_interface}           # 2. 실제 PHY ID, 실제 적용된 phy-mode
ethtool eth0                                                    # 3. speed/duplex/master-slave/SQI/Link detected
ip -s -s link show eth0                                         # 4. 표준 카운터 (missed, crc, frame, fifo, carrier)
ethtool -S eth0 > s0.txt; ethtool -S eth0 --all-groups >> s0.txt   # 5. 스냅샷 A
#   ... 트래픽 60 s (iperf3 -c <peer> -u -b 900M -t 60 또는 ping -f) ...
ethtool -S eth0 > s1.txt; ethtool -S eth0 --all-groups >> s1.txt   # 6. 스냅샷 B
python3 -I onsitePrep/n07/f/ethdiag.py s0.txt s1.txt            # 7. delta → 계층 → 가설 (§8)
cat /sys/class/net/eth0/carrier_down_count; cat /proc/net/softnet_stat   # 8. flap 횟수, 스택 드롭
phytool read eth0/1/1; phytool read eth0/1/1                    # 9. BMSR 두 번 (latch-low) [추정: phytool 설치]
```

- 규칙: **상대 쪽도 같이** 찍는다 — "내 TX가 깨졌는지"는 상대 RX 카운터에만 보인다.

## 4. 증상 → 원인 한 장 표

| 증상 | 계층 | 의심 원인 (순위) | 첫 확인 | 확정 방법 |
|---|---|---|---|---|
| MDIO 읽기 0xFFFF | 전기/설정 | ① PHY 리셋 유지·전원·25 MHz ref 없음 ② PHY 주소 strap 불일치 ③ 다른 MAC의 MDIO 버스에 붙은 PHY ④ C45 전용 PHY를 C22로 읽음 | 0–31 주소 스캔, RESET_N·전원 DMM | scope로 MDIO TA 이후 PHY 구동 여부 |
| MDIO 읽기 0x0000 | 전기 | MDIO가 low 고착(단락, 다른 장치가 끌어내림), pull-up 없음 | MDIO 정지 시 레벨 | 분리·저항 측정 |
| MDIO 값이 가끔 틀림/0xFFFF | 타이밍 | MDC가 PHY 최대 초과, MDC 링잉(이중 클럭), TA 타이밍 | MDC 낮추기 | 1만 회 읽기 오류율 vs MDC |
| PHY attach 실패 (-ENODEV 류) | 설정/드라이버 | DT `reg` ≠ strap, 리셋 해제 후 대기 부족, PHY 드라이버 미포함 | dmesg, sysfs mdio_bus 목록 | 주소 스캔 + 리셋 후 대기 늘리기 |
| T1 링크 안 뜸 | 설정 | ① 양쪽 같은 역할(둘 다 master) ② 100 vs 1000 T1 불일치 ③ PHY가 managed 모드로 link 시작 대기 ④ 케이블·커넥터·CMC | 양쪽 `ethtool` master-slave | 역할 반대로 강제 후 즉시 링크 |
| BASE-T 링크 안 뜸 | 설정/전기 | AN 비활성 한쪽, 4쌍 중 1쌍 불량(1G 실패·100M OK), magnetics | `ethtool` advertised/LPA | 강제 속도 bisect, cable test |
| 링크 up, 트래픽 0 / FCS 폭발 (1G만) | 타이밍(xMII) | ① RGMII 지연 중복/누락 ② MAC 속도 설정(클럭 mux) 불일치 ③ pinmux/IO 전압 | sysfs phy_interface vs strap vs MAC delay | PHY xMII loopback, RXC/RXD scope |
| SGMII 링크 up, 트래픽 0 | 설정 | in-band AN 불일치(MAC은 in-band 기대, PHY는 bypass) 또는 반대 | DT `managed`, PCS AN status | in-band on/off 맞춘 뒤 확인 |
| RX FCS 에러가 꾸준히 | 타이밍/전기 | xMII margin vs 선로 margin | PHY symbol·SQI vs MAC FCS 비교 | loopback 사다리 ([BV10](2026-11-01_BV10_board_interface_tests.html)) |
| TX 쪽만 깨짐 (상대 RX FCS) | 타이밍 | TX 지연(txid) 쪽만 틀림 | 상대 카운터 | PHY reverse loopback, TXC scope |
| 큰 프레임일수록 FCS 많음 | 전기/타이밍 | 랜덤 비트 오류(BER) — 길이에 비례 | FCS 비율 vs 프레임 크기 | 고정 크기 sweep |
| 1500 B 이상만 실패 (FCS 0) | 설정 | MTU/MAC 최대 길이/RX 버퍼 크기/VLAN +4 | `ping -M do -s 1472/1473` | too-long 카운터 증가 |
| VLAN 태그 패킷만 사라짐 | 설정/드라이버 | HW VLAN 필터에 VID 미등록, rxvlan strip 후 스택 불일치, 최대 길이 1518 고정 | `ethtool -k` rxvlan·vlan-filter | tcpdump -e 양쪽 + 필터 off |
| 링크 flap (몇 분마다) | 전기/설정 | ① SQI 경계 ② EEE/LPI(BASE-T) ③ PHY 리셋·전원 이벤트 ④ SW 주기 작업 | carrier_down_count, dmesg 시각 간격 | 주기성 분석 + SQI 로그 |
| 링크 up까지 느림 | 설정 | T1 training, PHY 리셋 시퀀스, 드라이버 폴링 주기 | dmesg 타임스탬프 | GPIO 마커 + scope |
| 64 B throughput 반토막 | 시스템 | pps 한계(프레임당 IRQ·refill) | missed vs CPU | RFC 2544 sweep ([L3](hmScreenPrep/problems/L3_rfc2544_throughput/index.html)) |
| iperf에서 RX drop (ping OK) | 시스템/드라이버 | ring 작음, IRQ coalescing·NAPI budget, DDR QoS, 소켓 버퍼 | missed vs rx_dropped vs softnet vs RcvbufErrors | 하나씩 키우며 drop 위치 이동 |
| TX timeout watchdog | 드라이버/DMA | descriptor OWN·barrier, TX clock 없음(RGMII TXC), pause storm | dmesg "NETDEV WATCHDOG" | TX ring head/tail, pause 카운터 |
| pause 프레임 폭주 | 시스템 | 상대/내 RX FIFO 포화 → 흐름 제어 | eth-ctrl / pause 카운터 | flow control off로 bisect |
| ping OK, TCP 큰 전송 실패 | 설정 | TSO/GSO·checksum offload 버그 | `ethtool -K eth0 tso off gso off` | 수신 측 checksum 에러 카운터 |
| PTP offset 고정 bias | 시스템/설정 | PHY 지연 미보정·경로 비대칭, 타임스탬프 지점 | 양쪽 PPS를 scope로 | ingress/egressLatency 보정 후 0 |
| PTP offset 링크 재수립마다 점프 | 전기/PHY | 링크마다 PHY 지연이 달라짐 [추정] | link 루프 중 PPS 차 기록 | 분포 측정 |
| 고온에서 SQI 하락 | 전기 | margin: CMC·termination·커넥터·PHY 전원·ref clock | SQI vs 온도 로그 | PRBS BER 코너 |
| EMI(BCI) 시험에서 drop | 전기 | 공통 모드 노이즈 → 선로 margin | SQI·FEC vs 주입 레벨 | CMC/배치 변경 A/B |
| 카운터가 감소/점프 | 측정 | 32-bit wrap, 드라이버 reload, read-clear 카운터 | 두 번 연속 읽기 | wrap 보정 (§8) |
| 한쪽만 링크 up 표시 | 설정/PHY | latch-low 미해제, PHY 폴링 주기, 한쪽 RX만 OK | 양쪽 BMSR 두 번 | 양방향 트래픽 |

## 5. 시나리오 드릴

### F1 · MDIO로 PHY ID를 읽으면 0xFFFF

#### 증상

- dmesg: PHY attach 실패(예: `Cannot attach to PHY (error: -19)` [예시]), `phytool read eth0/1/2` → `0xffff`, 링크 LED 꺼짐. 새 보드 첫 bring-up.

#### 먼저 물어볼 것

- "Is it 0xFFFF on every address 0–31, or only on the address we expect?"
- "Is the PHY on this MAC's MDIO bus, or does another MAC instance own the shared MDIO?"
- "Is the PHY Clause 45-only, and are we issuing Clause 22 frames?"
- "Does a known-good board read the ID with the same software?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| PHY 리셋 유지 / 전원 / ref clock 없음 | 가장 흔하고 MDIO 무응답과 정확히 일치 | RESET_N 레벨, PHY 전원 레일, 25 MHz 클럭 scope | RESET_N low 또는 클럭 없음 |
| 주소 strap ≠ DT `reg` | strap 핀이 LED/데이터 겸용이라 외부 회로가 값 변경 | 0–31 전부 스캔 | 다른 주소에서 정상 ID |
| 다른 MAC의 MDIO 버스 | SoC에서 MDIO는 MAC0 소속인 경우 많음 | 회로도 + `/sys/bus/mdio_bus/devices/` | 다른 버스에서 보임 |
| C45 전용 PHY | T1 PHY는 C45 위주 [11] | C45 read로 MMD1 reg 2/3 | C45에서만 ID |
| MDC/MDIO 전기 | pull-up 없음, MDC 과속, 레벨(1.8 vs 3.3 V) | MDC 낮추기, scope | TA 이후 아무도 안 끌어내림 |

#### 측정 · 계측

- scope 2채널: MDC + MDIO, trigger = MDIO 하강 edge(ST의 '0'). read 프레임의 **TA 두 번째 비트 이후** MDIO가 PHY에 의해 low로 끌려 내려가는 구간이 있는지 본다.

```text
 MDIO  ‾‾‾‾(PRE 32×1)‾‾‾\_/‾\_/‾‾\ PHYAD REGAD ‾‾ Z ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾  ← TA 이후 계속 high = 아무도 응답 안 함 → 0xFFFF
                     ST=01 OP=10                TA  DATA (pull-up만)
 정상  ‾‾‾‾(PRE)‾‾‾‾‾‾\_/‾\_/‾‾\ PHYAD REGAD ‾‾ Z\__/‾‾\__/‾‾‾\___ ...    ← TA 두 번째 비트에서 PHY가 0을 구동
```

- MDIO 레벨이 PHY VDDIO와 SoC IO 전압 모두에 맞는지 (1.8 V 영역 혼재).

#### root cause · fix

- 예: PHY RESET_N이 GPIO 확장기 뒤에 있어 확장기 드라이버보다 MAC probe가 먼저 → 리셋 해제 전 스캔. Fix: DT `reset-gpios` + 리셋 후 대기(데이터시트 값)를 PHY 노드에 명시하고, probe 순서를 보장(deferred probe).

#### 회귀로 굳히기

- 부팅 smoke: 모든 포트 PHYSID 읽기 == 기대 ID, MDIO 1만 회 read 오류 0, MDC 최소·최대 주파수 둘 다. 전원 사이클 100회 중 attach 실패 0.

#### 30초 영어 답

"All-ones means nobody drove the MDIO line during the data phase, so it's a presence problem, not a register problem. I first check the PHY is powered, out of reset and clocked — reset GPIO, rail, 25 MHz reference. Then I scan all 32 addresses, because the address straps often share pins with LEDs and get pulled to a different value. I also confirm the PHY is on this MAC's MDIO bus and whether it needs Clause 45 frames. A scope on MDC and MDIO tells me definitively whether the PHY drives the line after turnaround."

#### 꼬리 질문

- Q1. "Why 0xFFFF and not random data?"

<details><summary>답</summary>

"MDIO has a pull-up and the PHY only drives the line from the second turnaround bit onward. If no PHY answers, the station samples the pulled-up line for all sixteen data bits, which is 0xFFFF. All-zeros points to the opposite: something holding the line low."
- 한국어 핵심: pull-up + 무응답 = 0xFFFF, 0x0000 = low 고착.

</details>

- Q2. "The ID reads correctly at 1 MHz MDC but fails at 12.5 MHz. Is that a bug?"

<details><summary>답</summary>

"The 802.3 baseline is 2.5 MHz, so above that it's only legal if the PHY datasheet supports it. If it does, I'd look at MDC signal integrity — ringing that double-clocks — and the read turnaround timing, since the PHY's output delay plus the trace eats into the setup time at the MAC. It's a configuration limit to document, plus an MDC sweep in the regression."
- 한국어 핵심: 표준 2.5 MHz, 그 이상은 데이터시트 범위, 링잉·TA 타이밍.

</details>

### F2 · 100/1000BASE-T1 링크가 안 뜸

#### 증상

- MDIO ID 정상, `ethtool eth0` → `Link detected: no`, `master-slave status: unknown` [예시], 상대 ECU도 링크 없음. 같은 하네스로 다른 ECU는 됨.

#### 먼저 물어볼 것

- "Which side is configured master, by strap or register, and does the partner agree?"
- "Are both ends the same speed — 100BASE-T1 vs 1000BASE-T1 — and is Clause 98 autoneg enabled on either?"
- "Does the PHY need software to enable link-up (managed mode, sleep/wake)?"
- "Did it ever link on this board, or is this first power-on?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 양쪽 같은 역할 | T1은 보통 AN 없이 고정 [9], strap 겸용 핀 | 양쪽 `ethtool` master-slave cfg | 둘 다 forced master |
| 속도 불일치 (100 vs 1000 T1) | 멀티 속도 PHY 기본값 | ethtool 속도, strap | 한쪽 100, 한쪽 1000 |
| PHY가 링크 시작 대기 (managed/sleep) | 일부 T1 PHY는 호스트가 link 시작을 허가해야 함 [추정 — PHY마다] | 데이터시트 상태 레지스터 | PHY state = idle/sleep |
| 채널: 커넥터·CMC·termination·극성 | 하네스 다름 | `ethtool --cable-test`, 다른 하네스 | open/short 거리 |
| AN 설정 한쪽만 | Clause 98 AN은 선택 → 한쪽 AN, 한쪽 forced | ethtool autoneg | autoneg on vs off |

#### 측정 · 계측

- `ethtool -s eth0 master-slave forced-slave` 로 한쪽만 바꿔 즉시 링크가 뜨는지 (preferred-*는 AN 쓸 때만 의미 [6]).
- scope 차동 프로브로 MDI에 신호가 나오는지: master는 링크 전부터 송신(training) 시작 [추정 — PHY 상태 머신에 따라] → 선로가 완전히 조용하면 양쪽 다 slave 또는 송신 비활성 의심.
- strap 핀을 리셋 해제 순간에 scope로 (strap은 리셋 edge에서 래치 — 평상시 레벨은 의미 없음).

#### root cause · fix

- 예: 두 ECU 모두 strap 기본값 master(같은 레퍼런스 설계 복사). Fix: 아키텍처 표에 포트별 역할을 정의하고, 드라이버에서 DT 속성(SPE PHY의 master/slave role 속성 — 최근 커널 바인딩 논의 [9]) 또는 부팅 스크립트로 강제, strap은 문서와 일치하게 BOM 수정.

#### 회귀로 굳히기

- 포트×역할×속도 매트릭스: 반대 역할 쌍만 링크(**같은 역할 쌍은 링크 안 되는 것이 PASS — negative test**), link-up 시간 1,000회 분포(p99), 하네스 2–3종.

#### 30초 영어 답

"Single-pair automotive PHYs usually don't autonegotiate — Clause 98 autoneg is optional — so master and slave are fixed by strap or register, and two masters or two slaves never link. With the PHY ID readable, I read the master-slave config and status on both ends and the speed — 100 versus 1000BASE-T1. Then whether the PHY waits for software to enable the link or is in sleep. Only then the channel: cable test and a known-good harness. To fix, the role table becomes part of the vehicle architecture, enforced in the device tree and covered by a negative test."

#### 꼬리 질문

- Q1. "Why did automotive drop autonegotiation by default?"

<details><summary>답</summary>

"Because the topology is fixed at design time and link-up time matters — the network must come up quickly after wake-up. Negotiation adds time and a failure mode for no benefit. 802.3 still defines Clause 98 autoneg for single-pair PHYs as an option, and when it's used it also resolves master and slave."
- 한국어 핵심: 고정 토폴로지 + 빠른 link-up → AN 생략, Clause 98은 선택.

</details>

- Q2. "What does master/slave actually mean on the wire?"

<details><summary>답</summary>

"The master transmits using its local reference clock; the slave recovers the master's clock from the received signal and transmits with that recovered timing — that's what makes the echo-cancelled full-duplex single pair work. So role errors aren't a protocol nicety; with two masters both expect the other to follow their clock and the receivers never converge."
- 한국어 핵심: master = 자기 클럭, slave = 복원 클럭으로 송신 → 같은 역할은 수렴 불가.

</details>

- Q3. "How would you measure link-up time?"

<details><summary>답</summary>

"Toggle a GPIO when software releases PHY reset or issues the wake, and capture the PHY's link LED or link interrupt line on a scope, repeated a thousand times through power cycles. I report the distribution and p99 against the requirement, and separate PHY training time from driver polling delay — polling every second can dominate the number."
- 한국어 핵심: GPIO 마커 ↔ link 신호 scope, 1,000회 분포, 드라이버 폴링 지연 분리.

</details>

### F3 · 링크는 up인데 트래픽 0 (RGMII 지연) ★

#### 증상

- `Link is Up - 1Gbps/Full`, SQI 7/7, 그런데 `ping` 100% 손실. 내 쪽 `ethtool -S`: rx_crc_errors가 rx_packets의 수 % ~ 거의 전부, 또는 rx_packets가 0. 상대 쪽: 내가 보낸 프레임 FCS 에러. **`ethtool -s eth0 speed 100 duplex full autoneg off`(BASE-T) 또는 100BASE-T1 포트에서는 동작**.

#### 먼저 물어볼 것

- "Is the failure in both directions or only RX or only TX — what do the partner's counters say?"
- "What phy-mode is in the device tree, what do the PHY's delay straps say, and does the MAC have its own delay enabled by the bootloader?"
- "Does it work at 100 Mb/s, and did it work on the previous board revision?"
- "Is the PCB adding delay with serpentine traces?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| RGMII 지연 중복 (PHY `-id` + MAC delay 또는 PCB) | 1G만 실패, 선로 깨끗 | phy_interface + MAC delay 레지스터 + 회로도 | 지연이 2곳 |
| RGMII 지연 누락 (`rgmii`인데 PCB 지연 없음) | 같은 지문 | 위와 동일 | 지연 0곳 |
| 한 방향만 틀림 (`-rxid` vs `-txid` 혼동) | RX만 또는 TX만 실패 | 내 RX vs 상대 RX 카운터 | 한 방향 FCS |
| MAC 속도/클럭 mux 불일치 | adjust_link에서 MAC을 1G로 못 바꿈 | MAC 속도 레지스터, TXC 주파수 scope | TXC 25 MHz인데 링크 1G |
| pinmux·IO 전압·drive | 새 보드 | pinmux 덤프, VDDIO | 특정 비트만 깨짐 |
| SGMII in-band AN 불일치 (SGMII일 때) | 같은 "up but 0" | DT `managed`, PCS AN | PCS 링크 없음 |

#### 측정 · 계측

1. **설정 3곳을 표로**: DT `phy-mode`(실제 적용 = sysfs `phy_interface`), PHY의 RX/TX internal delay(strap 또는 레지스터 — 드라이버가 덮어씀), MAC의 delay(`rx/tx-internal-delay-ps` 또는 부트로더가 남긴 레지스터) [1][2], PCB 지연(레이아웃 길이 차). **정확히 1곳**이어야 함.
2. **loopback 사다리** ([BV10 §2.5.3](2026-11-01_BV10_board_interface_tests.html)): MAC 내부 loopback OK + PHY xMII/digital loopback(BMCR bit14) 실패 → MAC↔PHY 구간 확정.
3. **scope** (≥ 1 GHz 대역, 짧은 접지 스프링, **수신단 핀**에서): RX 방향은 MAC 핀에서 RXC vs RXD0, TX 방향은 PHY 핀에서 TXC vs TXD0. trigger = RXC 상승 edge, 데이터는 persistence로 eye처럼. 클럭 edge가 데이터 전이로부터 몇 ns인지 측정 — TskewR 1.0–2.6 ns 안이면 정상 [3].
4. bisect 손잡이: 드라이버의 `rx-internal-delay-ps`를 0 / 1000 / 2000 / 3000 ps sweep → 오류율 곡선 (중간 window가 통과 영역 = 작은 shmoo).

```text
 delay sweep (RX, 1 Gb/s, 100k 프레임) [예시]
 MAC RX delay ps :    0   500  1000  1500  2000  2500  3000  3500
 FCS error rate  :  92%   40%    0%    0%    0%    0%   15%   88%    ← PHY가 이미 2 ns 넣은 보드면 전체가 왼쪽으로 2 ns 이동
```

#### root cause · fix

- 예: DT `phy-mode = "rgmii-id"`(PHY가 TX·RX 지연) + SoC MAC의 RGMII delay 레지스터를 부트로더가 켜 둠 → RX 지연 ≈ 4 ns → 다음 전이 근처 샘플. Linux 권고대로 **PHY가 지연 담당, MAC은 phy-mode가 `rgmii`가 아니면 자기 지연을 끈다** [1]; DT 리뷰 규칙상 `rgmii` + 2000 ps MAC 지연 조합도 받아주지 않는다 [2]. Fix: MAC 드라이버가 부팅 시 delay 레지스터를 명시적으로 0으로, 부트로더와 커널 설정을 일치.

#### 회귀로 굳히기

- **지연 모드 × 속도 매트릭스**: `rgmii`/`-id`/`-rxid`/`-txid` × 10/100/1000, 보드 설계상 맞는 조합만 오류 0, 틀린 조합은 **반드시 실패**(negative control — 테스트가 지연을 실제로 구분하는지 증명). 부팅마다 MAC delay 레지스터 값 + sysfs `phy_interface`를 로그에 남겨 drift 감지. delay sweep window 폭을 보드 rev·온도별로 기록.

#### 30초 영어 답

"Link up only means the PHY's line side trained; the RGMII segment between MAC and PHY isn't tested by it. The classic cause is the 2-nanosecond clock-to-data delay applied twice or not at all — at 125 MHz double data rate each nibble is only 4 ns, so either mistake puts the sampling edge on a transition, while 100 Mb/s still works. I tabulate the four places delay can come from — PHY straps or registers, the phy-mode, the MAC's delay setting, the PCB — and require exactly one. PHY loopback confirms the segment, a scope at the receiver pins confirms the skew, and I add a delay-mode-by-speed regression with negative cases."

#### 꼬리 질문

- Q1. "In Linux, what exactly does phy-mode = rgmii-rxid mean?"

<details><summary>답</summary>

"It's described from the PHY's point of view: the PHY adds the delay on the receive path only, and the transmit delay is expected to come from elsewhere — the PCB or the MAC. rgmii means the PHY adds none, rgmii-id means the PHY adds both. A MAC driver that sees any mode other than plain rgmii should not add its own delays, otherwise you double them."
- 한국어 핵심: PHY 기준 해석, rxid = PHY가 RX만, MAC은 rgmii 외 모드에선 지연 끔.

</details>

- Q2. "Why do kernel maintainers prefer the PHY to add the delay?"

<details><summary>답</summary>

"PHYs usually offer fine-grained, calibrated delays that are stable across voltage and temperature, and putting it in the PHY keeps MAC drivers generic. PCB serpentines work too but cost area and can't be tuned. The documentation lists exactly those reasons and the failure symptoms — packet loss, FCS errors, or problems that vanish at 10/100."
- 한국어 핵심: 세밀·PVT 안정·드라이버 재사용 [1].

</details>

- Q3. "You see FCS errors only on TX, the partner reports them. How do you probe it?"

<details><summary>답</summary>

"TX timing must be measured at the PHY's input pins, because that's the receiver for TXC and TXD. I'd also use the PHY's reverse loopback if it has one, which sends MAC traffic back without the line. Then sweep only the TX delay — if a window opens, it's the txid half of the setting that's wrong."
- 한국어 핵심: TX는 PHY 핀에서 측정, reverse loopback, TX 지연만 sweep.

</details>

### F4 · RX FCS 에러가 꾸준히 증가 — loopback 사다리 (요약+링크)

- **증상**: 트래픽은 흐르는데 `rx_crc_errors`(또는 `eth-mac-FrameCheckSequenceErrors`)가 1e-5 수준으로 계속 증가.
- **3줄 답**: ① 사다리 MAC 내부 → PHY xMII → PHY analog/PCS → 외부 플러그/상대, **처음 실패하는 단이 범인** ② PHY symbol error·SQI가 깨끗하면 xMII, 나쁘면 선로 ③ 고정 크기 sweep으로 FCS 비율이 프레임 길이에 비례하는지(랜덤 BER) vs 특정 크기(정책/버퍼) 판별.
- **여기서 새로**: 프레임 오류 확률 ≈ 1 − (1 − BER)^(8·L) → BER 1e-7이면 64 B 5.1e-5, 1518 B 1.2e-3 (계산) — **큰 프레임만 깨지면 MTU 문제처럼 보이지만 실제론 BER일 수 있다** → F7과 구분은 "FCS 카운터냐 too-long 카운터냐". 그리고 FCS 카운터는 **상대 TX**의 문제일 수도 → 상대 카운터와 쌍으로.
- 링크: [BV10 §2.5.3 사다리 · 체인 C](2026-11-01_BV10_board_interface_tests.html), [deck §7 #10](2026-10-25_rivian_hm_screen_prep.html), [V10 FCS](hmScreenPrep/problems/V10_eth_frame_fcs/index.html), [V6 PRBS](hmScreenPrep/problems/V6_prbs_crc_checker/index.html).

### F5 · 링크가 몇 분마다 끊겼다 붙음 (flap)

#### 증상

- dmesg에 `Link is Down` / `Link is Up` 반복, `carrier_down_count`가 시간당 수십 증가. ping이 몇 초씩 끊김.

#### 먼저 물어볼 것

- "Is the interval regular — like every 300 seconds — or random?"
- "Does the partner see the same drops at the same times?"
- "What changes when it drops: temperature, load, a software job, power state?"
- "Is EEE enabled (BASE-T) or is a TC10 sleep request involved (T1)?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 선로 margin 경계 (SQI 낮음) | 랜덤 간격, 온도·부하 상관 | SQI·FEC 로그 vs 시각 | drop 전 SQI 하락 |
| 주기적 SW 작업 | 간격이 정확히 일정 | dmesg 간격 히스토그램, cron/데몬·runtime PM | 고정 주기 |
| EEE/LPI 상호운용 (BASE-T) | 유휴 시에만 | `ethtool --show-eee`, EEE off A/B [6] | off면 사라짐 |
| PHY 전원·리셋 글리치 | 부하 전류 순간 | PHY 레일 scope, RESET_N trigger | 레일 droop 동시 |
| 상대 쪽 원인 | 상대가 먼저 끊음 | 양쪽 시각 비교 (PTP로 맞춘 시계) | 상대 로그 선행 |
| PHY 폴링이 latch-low를 down으로 해석 | 짧은 glitch가 "down"이 됨 | BMSR 두 번 읽기, IRQ vs POLL | 실제 링크는 유지 |

#### 측정 · 계측

- `dmesg -T`의 down 시각 → 간격 분포. 일정 간격 = SW, 랜덤 + 온도 상관 = 전기.
- scope: trigger = PHY 링크 LED(또는 INT 핀) 하강 edge, 다른 채널에 PHY 전원·RESET_N·ref clock → **링크 down 순간 전에 무엇이 먼저 변했나** (pre-trigger 버퍼).
- SQI를 1초 주기로 로깅해 down 직전 추세.

#### root cause · fix

- 예: runtime PM이 유휴 300 s 후 MAC 클럭을 gate → PHY가 RGMII RX 클럭을 잃고 링크 재시작. Fix: 해당 포트 runtime PM 정책 수정 + 클럭 gate 시 PHY 상태 보존 시퀀스. 다른 흔한 결론: EEE 비활성, PHY 레일 디커플링.

#### 회귀로 굳히기

- 72 h soak에서 **carrier_down_count delta == 0**을 통과 기준에 포함, SQI 최소값 기록, 유휴 구간 포함 트래픽 패턴(유휴·버스트 교대).

#### 30초 영어 답

"First I characterize the timing: a regular interval points to software — a periodic job, power management, a watchdog — while random drops correlated with temperature or load point to margin or power. I check whether the partner drops first, log SQI every second to see if quality decays before each drop, and trigger a scope on the link indicator with the PHY rail, reset and reference clock on other channels to see what changes first. Then I add carrier-down-count equals zero to the soak pass criteria."

#### 꼬리 질문

- Q1. "BMSR link status reads 0 then 1. Did the link drop?"

<details><summary>답</summary>

"It dropped at some point since the last read — the bit is latched low, so the first read reports history and the second the current state. That's useful evidence for a brief glitch, but a driver that polls once and treats the first read as current will report spurious link-down events."
- 한국어 핵심: latch-low = 첫 읽기는 이력, 두 번째가 현재.

</details>

- Q2. "How do you tell whether your side or the partner initiated the drop?"

<details><summary>답</summary>

"Put both on a common time base — PTP or just a shared scope capturing both link indicators — and see which goes down first. The PHY that loses receiver lock first usually is the one seeing the bad signal; the other side's drop follows when it stops receiving the idles it expects."
- 한국어 핵심: 공통 시간축에서 먼저 떨어진 쪽 = 나쁜 신호를 받은 쪽.

</details>

### F6 · 64 B에서 throughput이 반토막 (요약+링크)

- **증상**: 1518 B는 line rate, 64 B는 약 50% (deck §7 #14).
- **3줄 답**: ① 바이트가 아니라 **패킷 수** 한계 — 1G·64 B = 1,488,095 fps, 프레임당 672 ns ([N06E 실제 출력](2026-11-01_N06E_plans_highspeed_io_sensor.html)) ② MAC 카운터로 drop 위치를 가른다: rx_missed/fifo 증가 = HW FIFO·DMA가 못 따라감, rx_dropped/softnet drop = SW ③ 손잡이: IRQ coalescing(`ethtool -C rx-usecs/rx-frames`), ring 크기, NAPI, RSS·IRQ affinity, DDR QoS.
- **여기서 새로**: §8 진단기의 `H_HOST_RX` 규칙이 이 판단(FCS 0 + missed 증가)을 그대로 코드로 함. 통과 기준은 "64 B ≥ [X]% zero-loss" 처럼 **크기별로** — 평균 throughput 하나로는 이 버그가 안 보인다.
- 링크: [L3 RFC 2544](hmScreenPrep/problems/L3_rfc2544_throughput/index.html), [deck §7 #14](2026-10-25_rivian_hm_screen_prep.html), [N06E E2 §5](2026-11-01_N06E_plans_highspeed_io_sensor.html).

### F7 · 1500 B 넘는 패킷만 실패

#### 증상

- `ping -s 1472 peer` OK, `ping -s 1473`(fragment 허용이면 OK, `-M do`면 실패), scp/큰 TCP 전송은 멈춤, NFS 마운트 후 큰 파일 읽기 hang. 또는 VLAN 포트에서는 1468 B부터 실패.

#### 먼저 물어볼 것

- "Which counter moves — FCS, too-long/oversize, or nothing at all?"
- "What's the MTU on both ends and on every switch in between, and is the link VLAN-tagged?"
- "Is TSO/GSO or checksum offload enabled, and does turning it off change anything?"
- "Do small packets pass at full rate without errors?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| MAC 최대 프레임 길이 1518 고정 + VLAN(1522) | VLAN 포트에서만 | too-long/oversize 카운터 | `FrameTooLongErrors` 증가 |
| MTU 불일치 (경로 중 하나가 작음) | 한쪽만 9000 등 | `ping -M do -s` sweep | 특정 크기에서 컷 |
| RX DMA 버퍼 크기 < 프레임 | 드라이버 기본 1536 등 | rx_length_errors, 버퍼 설정 | length 에러 |
| TSO/checksum offload 버그 | ping OK, TCP만 실패 | `ethtool -K eth0 tso off gso off tx off` | off하면 해결 |
| 랜덤 BER (F4) | 큰 프레임이 확률상 더 깨짐 | FCS 카운터 | FCS 증가 (too-long 아님) |

#### 측정 · 계측

- 크기 sweep: `for s in 1400 1468 1472 1473 1476 2000; do ping -c 20 -M do -s $s peer; done` — 1472 + 8(ICMP) + 20(IP) = 1500 B IP = 1518 B 프레임(헤더 14 + FCS 4), VLAN이면 1522 B.
- 매 크기마다 `ethtool -S --groups eth-mac rmon` delta → 어느 카운터가 움직이는지 (§8 c3 케이스가 이 패턴).
- tcpdump 주의: **송신 측 tcpdump에 "bad cksum" 이 보이는 건 checksum offload 시 정상** — 체크섬은 캡처 이후 HW가 채운다. 판단은 수신 측에서.

#### root cause · fix

- 예: MAC의 max frame length 레지스터가 1518로 초기화, VLAN 태그 프레임(1522)이 too-long으로 drop. Fix: VLAN 사용 시 1522(또는 MTU + 헤더 + 태그 + FCS)로 설정하고 MTU 변경 시 RX 버퍼·최대 길이를 함께 재계산.

#### 회귀로 굳히기

- 경계 테스트: 64/1518/1522/MTU 경계 ±1 B, VLAN 유무 × offload on/off, 각 경우 **기대 카운터**(정상 = good +N, 초과 = too-long +N) 정확히 일치 ([N06E E2 BND 행](2026-11-01_N06E_plans_highspeed_io_sensor.html)).

#### 30초 영어 답

"I first separate a size policy problem from a bit error problem by which counter moves: frame-too-long or length errors mean a configured limit, FCS errors mean random bit errors that just hit long frames more often. Then a ping sweep with don't-fragment around 1472 bytes, with and without VLAN, finds the exact cutoff — 1472 plus 28 is a 1500-byte packet, a 1518-byte frame, 1522 tagged. If ping is fine but TCP stalls, I bisect offloads: TSO and checksum off. The fix is usually the MAC max-length or RX buffer size not following the MTU, and the regression is a boundary test at plus and minus one byte."

#### 꼬리 질문

- Q1. "tcpdump on the sender shows bad TCP checksums. Bug?"

<details><summary>답</summary>

"Not necessarily. With transmit checksum offload, the kernel hands the frame to the NIC with the checksum field unfilled, and tcpdump captures it before the hardware computes it. The real check is on the receiver, or with offload off."
- 한국어 핵심: TX offload면 송신 측 캡처의 bad cksum은 정상.

</details>

- Q2. "Why might only large TCP transfers fail while large pings work?"

<details><summary>답</summary>

"TCP uses segmentation offload: the stack hands the NIC a large buffer and the hardware cuts it into MSS-sized frames and fixes up headers. A bug there — wrong MSS, header or checksum update — only affects TCP. Turning TSO and GSO off and seeing the problem vanish localizes it."
- 한국어 핵심: TSO는 TCP만 → off로 bisect.

</details>

### F8 · ping은 되는데 iperf에서 RX drop

#### 증상

- `iperf3 -c peer -u -b 950M` 수신 측 loss 3–5%, ping·저속 TCP는 깨끗. `ip -s -s link`의 missed가 증가, FCS 0.

#### 먼저 물어볼 것

- "Where exactly are the drops counted — NIC missed/fifo, driver queue, softnet, or the UDP socket?"
- "What are the RX ring size and interrupt coalescing settings, and how many CPUs handle the IRQ?"
- "Is anything else hammering DDR at the same time — camera, NPU?"
- "TCP or UDP, and what packet size?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| RX ring 작음 + IRQ 지연 | missed 증가, FCS 0 | `ethtool -g` → `-G rx 1024` | drop 감소 |
| coalescing/NAPI budget | 버스트에 ring 넘침 | `ethtool -c`, softnet 3열(time_squeeze) | squeeze 증가 |
| DDR 경합 (DMA 쓰기 지연) | 동시 부하에서만 | 카메라·NPU off A/B | 부하 끄면 사라짐 |
| 스택/소켓 버퍼 | NIC 카운터 0인데 앱 loss | `nstat -az UdpRcvbufErrors`, softnet 2열 | Rcvbuf 증가 |
| pause 미사용 | 버스트 흡수 불가 | flow control 설정 | pause on이면 감소 |

#### 측정 · 계측

- **drop 위치 사다리** (아래 → 위): NIC `rx_missed_errors`/fifo(HW가 버퍼 없어 버림 [4]) → 드라이버 queue drop → `/proc/net/softnet_stat`(2열 backlog drop, 3열 time_squeeze) → `UdpRcvbufErrors`(소켓). 손잡이를 하나 바꿀 때마다 drop이 **어느 층으로 이동**하는지 본다.
- IRQ 분포: `grep eth0 /proc/interrupts` 두 번 diff → 한 CPU에 몰렸나.

#### root cause · fix

- 예: RX ring 256 + rx-usecs 0 + 단일 큐 IRQ가 CPU0에 고정, 카메라 DMA 동시 → DMA 쓰기 지연이 길어지면 ring이 넘침. Fix: ring 1024, coalescing 조정, RSS/affinity 분산, 필요 시 DDR QoS 우선순위. 다시 측정해 missed 0.

#### 회귀로 굳히기

- UDP line rate 60 s × 프레임 크기 7종 × (단독 / 카메라+NPU 동시) — 통과 기준 **missed·rx_dropped·Rcvbuf delta 전부 0** (§8 diagnoser를 그대로 판정기로).

#### 30초 영어 답

"Ping proves connectivity, not capacity, so I find where the drops are counted, bottom-up: the NIC's missed or FIFO counter means the hardware had no free descriptor, a driver queue drop or softnet drop means the CPU side was too slow, and a UDP receive-buffer error means the application. Then I turn one knob at a time — ring size, interrupt coalescing, IRQ affinity — and watch the drop move or disappear. I also rerun with the camera and NPU loading DDR, because DMA write latency is what fills the ring. The regression is zero drops at line rate, under concurrent load."

#### 꼬리 질문

- Q1. "rx_missed_errors versus rx_dropped — what's the difference?"

<details><summary>답</summary>

"By the kernel's definitions, missed means the device dropped the packet because it ran out of buffers — the host didn't replenish descriptors fast enough. Dropped means the packet was received but not processed, for example no memory or an unsupported protocol, and on some hardware it includes filter drops. So missed points to DMA, ring and interrupt latency; dropped points higher."
- 한국어 핵심: missed = 버퍼 고갈(HW), dropped = 받았지만 처리 못 함 [4].

</details>

- Q2. "Bigger ring fixed it. Are you done?"

<details><summary>답</summary>

"Not quite — a bigger ring buys burst tolerance and adds latency, so I check latency percentiles didn't regress, and I want the root cause of the slow drain documented: was it interrupt placement or DDR contention? If it's DDR, the right fix may be QoS, and the ring change only hides it until the next workload."
- 한국어 핵심: ring은 버스트 흡수 + 지연 증가, 근본 원인(IRQ vs DDR) 확인.

</details>

### F9 · PTP 오프셋이 일정한 bias (예: 항상 +180 ns)

#### 증상

- `ptp4l -m` 은 `master offset` ±10 ns로 "잘 동기됨" [예시 출력]. 그런데 두 ECU의 PPS 출력을 scope로 보면 **항상 약 180 ns 차이**. 재부팅해도 같은 값, 다른 PHY 보드끼리는 차이 없음. 카메라-LiDAR 정렬 오차로 발견.

#### 먼저 물어볼 것

- "Is the bias identical after every reboot and re-link, or does it change per link-up?"
- "Where are timestamps taken — MAC, or PHY — and are PHY ingress/egress latencies compensated?"
- "Same PHY part on both ends? Same cable length both directions (asymmetric media converters)?"
- "How did we measure the truth — PPS on a scope, or only ptp4l's own number?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| PHY TX/RX 지연 미보정 (MAC 타임스탬프) | 고정 bias, PHY 종류에 따라 다름 | 데이터시트 지연값, ptp4l `ingressLatency/egressLatency` [12] | 보정 후 bias 소멸 |
| 경로 비대칭 (media converter, 스위치 포트) | 중간 장치 있을 때 | 직결 vs 경유 A/B | 경유에서만 bias |
| 타임스탬프 지점 잘못 (SFD 아님) | MAC IP 구현 차이 | 설계 문서, 크기별 bias | 프레임 크기 따라 bias 변함 |
| PPS 출력 경로 지연 | 측정 체인 자체 | PPS 핀 지연·프로브 deskew | 양쪽 같은 보드면 상쇄 |

#### 측정 · 계측

- PTP는 **경로가 대칭이라고 가정**: 오프셋 오차 = (d_ms − d_sm)/2. 서보는 이걸 볼 수 없다 → `master offset` ≈ 0 인 채로 틀린다. 진실은 **외부 기준**(양쪽 PHC의 PPS → scope, 프로브 deskew 후)으로만.
- 예 [계산]: slave PHY RX 지연 400 ns, TX 지연 40 ns, 타임스탬프는 MAC에서, 보정 없음 → 비대칭 360 ns → 오프셋 오차 180 ns.

```text
 master PPS ────┐_____________________
 slave  PPS ──────────┐_______________     Δ = 180 ns, 매 부팅 동일 → 고정 비대칭
               |<-180->|
```

- 다른 크기 프레임에서 bias가 변하면 타임스탬프 지점이 SFD가 아니라 프레임 끝 근처 [추정].

#### root cause · fix

- 예: 타임스탬프 유닛이 MAC에 있고 T1 PHY의 RX 파이프라인(FEC 포함) 지연이 커서 비대칭. Fix: PHY 데이터시트 지연을 ptp4l `ingressLatency`/`egressLatency`(또는 드라이버의 PHY 지연 보정)로 넣고, 남는 것은 `delayAsymmetry`로 [12]. 재측정해 PPS Δ < 요구치.

#### 회귀로 굳히기

- 보드 팜 고정 셋업: PPS 2개 → TIC(time interval counter) 또는 scope 자동 측정, **링크 재수립 100회 × 온도 3점**에서 Δ 분포(평균 = bias, 폭 = PHY 지연 변동) 기록 — 링크마다 PHY 지연이 다르면 [추정] 고정 보정으로 부족함을 숫자로 보여 줌. [L4 PPS](hmScreenPrep/problems/L4_pps_time_discipline/index.html), [N06E E4](2026-11-01_N06E_plans_highspeed_io_sensor.html).

#### 30초 영어 답

"A constant bias with a happy servo is the signature of path asymmetry, because PTP assumes the two directions have equal delay and folds half of any difference into the offset — the servo can't see it. So I measure against an external reference: PPS outputs from both clocks on a scope, deskewed. Then I look for the asymmetric part — usually PHY transmit and receive latencies not compensated when timestamps are taken in the MAC, or a media converter in the path. The fix is compensating ingress and egress latency, and the regression measures PPS skew across re-links and temperature."

#### 꼬리 질문

- Q1. "Why can't ptp4l detect a constant asymmetry?"

<details><summary>답</summary>

"It measures the round trip and assumes each direction is half. An asymmetry shifts the computed offset by half of the difference, and since it's constant the servo converges happily to the wrong point — reported offset near zero. Only an independent reference, like comparing PPS outputs, reveals it."
- 한국어 핵심: 왕복/2 가정 → 비대칭/2 만큼 틀린 점으로 수렴, 외부 기준 필요.

</details>

- Q2. "Where should the timestamp ideally be taken?"

<details><summary>답</summary>

"As close to the wire as possible, at a defined reference point — the start-of-frame delimiter crossing the medium interface. Timestamping in the PHY removes the PHY's variable latency from the error budget; timestamping in the MAC is common but then the PHY latencies must be calibrated and stable."
- 한국어 핵심: 선로에 가까운 SFD 기준점, MAC이면 PHY 지연 보정 필수.

</details>

- Q3. "How does this connect to multi-camera sync?"

<details><summary>답</summary>

"If cameras and LiDAR are stamped by different ECUs disciplined over gPTP, a 180-nanosecond bias is harmless, but a bias that changes per link-up or with temperature into microseconds becomes visible in fusion at speed. So time-sync validation reports both the constant bias and its spread across re-links and temperature."
- 한국어 핵심: bias 자체보다 재링크·온도에 따른 변동이 퓨전에 위험.

</details>

### F10 · 고온/EMI 시험에서 SQI 하락 후 링크 drop

#### 증상

- 챔버 25 → 105 °C 램프 중 SQI 7 → 3, FEC 정정 카운트 급증, FCS는 거의 0, 이후 링크 2회 drop. 또는 BCI(전류 주입) EMC 시험 특정 주파수에서 drop.

#### 먼저 물어볼 것

- "Does SQI track temperature smoothly, or step at a threshold?"
- "Does the partner see the same SQI drop — is it the channel or one transceiver?"
- "What about PHY supply ripple and the 25 MHz reference at temperature?"
- "Same with a short known-good cable outside the chamber, only the board heated?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 채널 손실 증가(케이블·커넥터·CMC 온도 특성) | 양쪽 SQI 같이 하락 | 보드만 가열 vs 케이블만 가열 A/B | 가열 부위에 따라 갈림 |
| PHY 전원 노이즈·ref clock | 한쪽만 하락 | 레일 ripple, ref clock 주파수·지터 | ripple 증가 |
| termination/CMC 부품 편차 | 특정 보드만 | 보드 swap | 보드 따라 감 |
| EMI 공통 모드 유입 | 특정 BCI 주파수 | 주입 레벨 sweep vs SQI | 임계 주파수 |
| PHY DSP 설정(벤더 권장 init 누락) | 전 보드 공통 | 벤더 스크립트 diff | 적용 후 개선 |

#### 측정 · 계측

- 1초 주기로 (시각, 온도, SQI, FEC 정정/미정정, FCS, carrier_down) CSV → 온도-SQI 곡선. **SQI는 MSE 기반 3비트 등급**이라 계단 모양 [7]; OPEN Alliance TC1은 잡음이 늘면 SQI가 **단조 감소**해야 한다고 요구 [7] → 이 단조성 자체도 시험 항목 (OA 100BASE-T1 IOP 시험에도 신호 품질 표시 시험 있음 [15]).
- 짧은 시간엔 SQI·FEC, 확정은 최악 코너에서 PRBS BER (1e-12 @ 1G 오류 0 = 약 50분 — [BV10 §2.5.4](2026-11-01_BV10_board_interface_tests.html)).

#### root cause · fix

- 예: CMC(공통 모드 초크) 부품의 고온 특성 + 커넥터 근처 termination 값 → 105 °C에서 margin 소진, FEC가 버티다 미정정 → 링크 재학습. Fix: 부품 변경/레이아웃, 벤더 PHY init 권장값 적용, 그리고 **SQI·FEC 기준을 통과 조건에 추가**.

#### 회귀로 굳히기

- 온도 코너(−40/25/105 °C) × 케이블 길이 × 전원 ±5%: 통과 기준 = carrier_down 0 **그리고** SQI ≥ [X] **그리고** FEC 미정정 0 — SQI 임계값은 OPEN Alliance TC1/벤더 문서로 정함 (흔히 인용되는 "SQI ≥ 5 ≈ BER < 1e-10"은 [추정 — 원문 미확인]).

#### 30초 영어 답

"With FEC on 1000BASE-T1, FCS errors are the last thing to move, so I watch SQI and FEC-corrected counts as the leading indicators. I log them every second against temperature, compare both link partners to tell channel from transceiver, and split the chamber experiment — heat only the board, then only the cable — to localize. I check PHY supply ripple and the reference clock at temperature. After the fix, SQI floor and zero uncorrectable FEC become pass criteria alongside zero link drops, at every temperature and cable corner."

#### 꼬리 질문

- Q1. "What is SQI physically?"

<details><summary>답</summary>

"It's a quantized view of the receiver's mean-square error after equalization — how far received symbols land from their ideal levels. Vendors map MSE ranges to eight levels, zero worst to seven best, and the OPEN Alliance requires it to decrease monotonically as noise increases. It's not a BER, but it moves long before frames fail."
- 한국어 핵심: 등화 후 MSE를 0–7로 양자화, BER 아님, 선행 지표.

</details>

- Q2. "SQI stays at 7 but FCS errors appear. What now?"

<details><summary>답</summary>

"Then the line side is probably healthy and the errors are being created elsewhere — between PHY and MAC, the RGMII or SGMII segment, or in the MAC's clocking. That's exactly the case where PHY xMII loopback and a scope on the parallel interface pay off."
- 한국어 핵심: SQI 정상 + FCS = 선로 아님 → xMII 구간 (F3).

</details>

## 6. 꼬리 질문 체인

### 체인 1 — "Walk me through the layers of an Ethernet PHY." (면접관 전공 체크)

Q → 꼬리1 "Where would you put loopbacks, and what does each prove?" → 꼬리2 "Clause 22 vs Clause 45 — why does it matter for T1?" → 꼬리3 "Which counters belong to which layer?"

<details>
<summary>Q. Walk me through the layers of an Ethernet PHY.</summary>

- **EN**: "Below the MAC there's the reconciliation sublayer, which maps MAC signals onto the media-independent interface — RGMII or SGMII. Inside the PHY, the PCS does coding: for 1000BASE-T1 that's 80B/81B with Reed-Solomon FEC. The PMA turns symbols into PAM3 levels and back, with clock recovery and echo cancellation, and the PMD and MDI are the analog front end and connector. For BASE-T there's an autonegotiation function; for single-pair it's optional Clause 98, otherwise master and slave are configured."
- 한국어 핵심: RS → xMII → PCS(부호화·FEC) → PMA(PAM3·CDR·EC) → PMD/MDI, T1 AN은 선택.

</details>

<details>
<summary>꼬리1. Where would you put loopbacks, and what does each prove?</summary>

- **EN**: "MAC internal proves MAC, DMA and descriptors. PHY xMII or digital loopback adds the MAC-to-PHY interface — that's where RGMII delay shows up. PCS or analog loopback inside the PHY adds its coding and front end where supported. An external plug or a partner adds connector, magnetics or choke and cable. The first stage that fails is the broken segment."
- 한국어 핵심: 안 → 밖, 처음 실패 단 = 범인.

</details>

<details>
<summary>꼬리2. Clause 22 vs Clause 45 — why does it matter for T1?</summary>

- **EN**: "Clause 22 gives 32 registers per PHY with a 5-bit register address. Clause 45 adds a device address for each MMD — PMA/PMD, PCS, autoneg, vendor — and a separate address frame for a 16-bit register number. BASE-T1 control, like the master bit in the PMA/PMD control register, lives in Clause 45 space, so a tool that only speaks Clause 22 can show a 'healthy' PHY ID while you can't see or set what actually matters."
- 한국어 핵심: C45 = MMD + 2단계 접근, T1 핵심 레지스터는 C45.

</details>

<details>
<summary>꼬리3. Which counters belong to which layer?</summary>

- **EN**: "Symbol errors, FEC corrected and uncorrected, SQI — PHY and PCS. FCS, alignment, length and too-long — MAC. Missed, FIFO overrun, no-buffer — the DMA and driver couldn't supply descriptors. Dropped and softnet drops — the stack. Names vary by driver, so I map them through the standard groups ethtool exposes — eth-phy, eth-mac, eth-ctrl, rmon — and the kernel's standard interface stats."
- 한국어 핵심: PHY/PCS · MAC · DMA · 스택 4층, 표준 그룹으로 이름 정규화.

</details>

### 체인 2 — "Link is up but no packets pass." → 깊게

Q → 꼬리1 "How do you know TX or RX?" → 꼬리2 "Why did 100M work?" → 꼬리3 "How do you stop this on the next board?"

<details>
<summary>Q. Link is up but no packets pass.</summary>

- **EN**: "Link up proves the PHY line side only. I read counters on both ends, then check the four possible sources of RGMII delay — PHY, phy-mode, MAC, PCB — expecting exactly one, and confirm with PHY loopback and a scope at the receiver pins."
- 한국어 핵심: 링크 up ≠ xMII OK, 지연 출처 4곳 중 하나만.

</details>

<details>
<summary>꼬리1. How do you know whether it's TX or RX?</summary>

- **EN**: "My RX counters show what I receive; my TX problems show up only on the partner's RX counters. With PHY xMII loopback, the frame crosses both TX and RX of the interface, so I combine it with the partner's view, or a PHY reverse loopback, to split the directions. Then I sweep only the TX or only the RX delay."
- 한국어 핵심: 내 TX 문제는 상대 RX 카운터에서, 방향별 sweep.

</details>

<details>
<summary>꼬리2. Why did 100M work?</summary>

- **EN**: "At 100 megabit the RGMII clock is 25 MHz, a 40-nanosecond period, so a 2-nanosecond error is a small fraction of the data window. At gigabit each nibble only lasts 4 nanoseconds, so missing or doubling 2 nanoseconds puts the sampling edge on a transition."
- 한국어 핵심: 40 ns 주기 vs 4 ns 칸.

</details>

<details>
<summary>꼬리3. How do you stop this on the next board?</summary>

- **EN**: "Make the delay owner an explicit line in the board bring-up checklist and the device tree review, have the MAC driver force its own delay off unless phy-mode is plain rgmii, log the applied mode and the delay registers every boot, and keep the delay-mode-by-speed matrix with negative cases in the regression, plus a small delay sweep that records the passing window per board revision."
- 한국어 핵심: 체크리스트·드라이버 기본값·부팅 로그·negative 매트릭스·window 기록.

</details>

### 체인 3 — "How do you prove an automotive link has margin?"

Q → 꼬리1 "FEC hides errors — then what?" → 꼬리2 "How long do you run?" → 꼬리3 "How do you report it?"

<details>
<summary>Q. How do you prove an automotive link has margin, not just that it works?</summary>

- **EN**: "Push it toward failure and measure the distance: temperature, supply voltage and cable length corners, with SQI and FEC counts as continuous indicators and PRBS bit error rate at the worst corner. Zero frame errors at room temperature only shows it works today."
- 한국어 핵심: 코너로 밀고 거리 측정, SQI·FEC 연속 지표 + PRBS.

</details>

<details>
<summary>꼬리1. FEC hides errors — then what?</summary>

- **EN**: "I treat FEC-corrected counts as the real error signal. A rising corrected rate at a corner means margin is shrinking even with zero FCS errors, and any uncorrectable block is a failure. So corrected-rate limits and an SQI floor are part of the pass criteria."
- 한국어 핵심: 정정 카운트가 진짜 신호, 미정정 1개 = FAIL.

</details>

<details>
<summary>꼬리2. How long do you run?</summary>

- **EN**: "It follows from the target: to claim BER below 1e-12 at 95 percent confidence with zero errors I need about three times ten to the twelfth bits — about fifty minutes at a gigabit, eight hours at 100 megabit. So I screen all corners at 1e-10 and run the full length only at the worst ones."
- 한국어 핵심: N ≈ 3e12 bit, 1G 50분 / 100M 8.3시간, 스크리닝 후 최악 코너만.

</details>

<details>
<summary>꼬리3. How do you report it?</summary>

- **EN**: "Per board and corner: SQI minimum, FEC corrected rate, uncorrectable count, link drops, and BER bound with the bit count, in a results database so we can compare silicon lots and board revisions. A margin map by temperature and voltage is more useful to design than a single pass or fail."
- 한국어 핵심: 코너별 SQI min·FEC·drop·BER 상한, DB + margin map.

</details>

### 체인 4 — "Two ECUs disagree on time by 180 ns."

Q → 꼬리1 "Why is ptp4l happy?" → 꼬리2 "What do you change?" → 꼬리3 "What if it changes each link-up?"

<details>
<summary>Q. Two ECUs disagree on time by a constant 180 ns.</summary>

- **EN**: "Constant means asymmetry. PTP assumes symmetric paths, so half of any difference ends up in the offset. I confirm with PPS outputs on a scope and then look at uncompensated PHY latencies and anything asymmetric in the path."
- 한국어 핵심: 고정 = 비대칭, PPS로 확인, PHY 지연 보정.

</details>

<details>
<summary>꼬리1. Why is ptp4l happy?</summary>

- **EN**: "Its offset is computed from timestamps that share the same wrong assumption, so it converges to the biased point and reports near zero. The servo can't observe a constant asymmetry."
- 한국어 핵심: 같은 가정 → 틀린 점 수렴.

</details>

<details>
<summary>꼬리2. What do you change?</summary>

- **EN**: "Enter the PHY's ingress and egress latencies — linuxptp has ingressLatency and egressLatency for that — or compensate in the driver, and use delayAsymmetry for residual path differences. Then re-measure the PPS skew."
- 한국어 핵심: ingress/egressLatency, delayAsymmetry, 재측정.

</details>

<details>
<summary>꼬리3. What if it changes each link-up?</summary>

- **EN**: "Then a fixed calibration isn't enough — the PHY's internal latency depends on training state, for example how the receive pipeline aligns. I'd measure the distribution over many re-links and temperatures, ask the PHY vendor whether the latency is observable in a register, and either compensate per link-up or move timestamping into the PHY."
- 한국어 핵심: 재링크 분포 측정, 레지스터로 보이면 링크마다 보정, 아니면 PHY 타임스탬프 [추정].

</details>

## 7. Don 경험 연결

- "I haven't validated an Ethernet PHY myself, but the debug pattern is the one I used on PCIe for SSD controllers: localize with loopbacks, read the layer from the error counters, and confirm the timing on a scope at the receiver. At SK hynix I ran interface reliability across chamber temperature with shmoo, which maps directly to the SQI-and-FEC-versus-temperature work here — [숫자 채우기: 챔버 코너 수 · 보드 수]. At Apple I root-caused PCIe and I2C issues with a DSO and protocol analyzer, including cases where the link reported healthy but transfers failed — [숫자 채우기: 사례 1개 증상 · 원인]. And I'd write the counter-diff checker first, because snapshots before and after every run are what turn a lab hunch into evidence."

## 8. 실습 / 코드

### 8.1 무엇을 하나

- `onsitePrep/n07/f/ethdiag.py` (Python stdlib, `python3 -I`): `ethtool -S` 스냅샷 두 개 → **delta**(32-bit wrap 보정, 감소 시 reset 표시) → 이름을 **정규화**(드라이버 이름 · `ip -s -s link` 이름 · `--groups eth-mac/rmon` 이름을 하나의 키로, 같은 사건을 세는 별칭은 **max** — 합치면 이중 계산, per-queue는 합) → **계층 분류**(PHY/PCS · MAC · DMA/driver · queue/stack · good) → 규칙으로 **가설 점수·순위** + 근거 + 다음 확인.
- 규칙(요약): FCS 비율 > 0.1% 이면서 PHY 쪽 깨끗·SQI 유지·1G → `H_RGMII` / symbol·FEC·SQI 하락·flap → `H_CHANNEL` / missed 증가 + FCS 0 → `H_HOST_RX` / too-long·length → `H_FRAMESIZE` / tx_timeout·underrun → `H_TX` / 아무 오류도 없음 → `H_ABOVE_L2`.
- mock 스냅샷 5쌍(fault 4 + 깨끗한 대조군 1), 각각 `expected.txt`와 비교해 PASS/FAIL. 카운터 이름은 흔한 패턴을 흉내 낸 **mock** — 실제 이름은 드라이버마다 다르니 `ALIASES` 표에 내 드라이버 이름을 추가해서 쓴다. 표준 그룹 이름(`FrameCheckSequenceErrors`, `FrameTooLongErrors`, `etherStatsOversizePkts`, `SymbolErrorDuringCarrier`)은 커널 소스 기준 [5].
- 실행: `sh onsitePrep/n07/f/run.sh` (→ `run_output.txt`), 단일 쌍: `python3 -I onsitePrep/n07/f/ethdiag.py s0.txt s1.txt`.

### 8.2 실제 출력 (2026-11-01, `run_output.txt`)

```text
== c0_clean_control ==
  meta(after): carrier_down=0 interval_s=60 link=up speed=1000 sqi=7 sqi_max=7
  nonzero deltas:
    good         rx_bytes                     +7145000000   rx_bytes
    good         rx_packets                   +4711250      rx_frames
    good         tx_packets                   +55000        tx_frames
  ranked hypotheses:
    1. [ 1] H_ABOVE_L2  no L1/L2 error counter moved -> look above the MAC (ARP/IP config, VLAN filter, firewall, wrong interface)
         evidence: rx_frames +4711250, tx_frames +55000
         next:     tcpdump -e on both ends; ip neigh; VLAN/filter config
  PASS c0_clean_control: top=H_ABOVE_L2 expected=H_ABOVE_L2

== c1_rgmii_delay ==
  meta(after): carrier_down=0 interval_s=60 link=up speed=1000 sqi=7 sqi_max=7
  nonzero deltas:
    MAC          rx_align_errors              +515          rx_align_err
    MAC          rx_crc_errors                +38900        rx_fcs_err
    good         rx_packets                   +460000       rx_frames
    good         tx_packets                   +496800       tx_frames
  ranked hypotheses:
    1. [ 8] H_RGMII     MAC<->PHY interface timing (RGMII clock/data delay doubled or missing)
         evidence: FCS rate 7.80%, PHY symbol/PCS errors 0, SQI drop 0, speed 1000
         next:     phy-mode vs PHY delay strap/regs vs MAC delay; PHY xMII loopback; scope RXC vs RXD at receiver
  PASS c1_rgmii_delay: top=H_RGMII expected=H_RGMII

== c2_rx_ring_overflow ==
  meta(after): carrier_down=0 interval_s=60 link=up speed=1000 sqi=7 sqi_max=7
  nonzero deltas:
    DMA/driver   rx_missed_errors             +240345       rx_missed
    queue/stack  rx_queue_0_drops             +2100         rx_sw_drop
    good         rx_packets                   +5037296      rx_frames  [32-bit wrap corrected]
    good         tx_packets                   +900          tx_frames
  ranked hypotheses:
    1. [ 7] H_HOST_RX   host RX path too slow (ring size, IRQ coalescing/NAPI budget, DDR/QoS)
         evidence: rx_missed/fifo 240345 (4.771% of rx), sw drops 2100, FCS 0
         next:     ethtool -g/-G ring, -c/-C coalesce, /proc/net/softnet_stat, IRQ affinity, DDR bandwidth
  PASS c2_rx_ring_overflow: top=H_HOST_RX expected=H_HOST_RX

== c3_oversize_vlan ==
  meta(after): carrier_down=0 interval_s=60 link=up speed=1000 sqi=7 sqi_max=7
  nonzero deltas:
    MAC          eth-mac-FrameTooLongErrors   +81274        rx_oversize
    MAC          rmon-etherStatsOversizePkts  +81274        rx_oversize
    good         eth-mac-FramesReceivedOK     +60000        rx_frames
  ranked hypotheses:
    1. [ 6] H_FRAMESIZE max frame length / MTU / VLAN +4 B mismatch (MAC max-length, RX buffer size)
         evidence: oversize 81274, length errors 0, FCS 0
         next:     ping -M do -s 1472/1473 (+VLAN); MAC max frame length register; RX buffer size; peer MTU
  PASS c3_oversize_vlan: top=H_FRAMESIZE expected=H_FRAMESIZE

== c4_phy_margin_hot ==
  meta(after): carrier_down=2 interval_s=600 link=up speed=1000 sqi=3 sqi_max=7 temp_c=105
  nonzero deltas:
    PHY/PCS      SymbolErrorDuringCarrier     +912          phy_symbol_err
    PHY/PCS      phy_fec_corrected_blocks     +250000       fec_corrected
    PHY/PCS      phy_fec_uncorrected_blocks   +12           fec_uncorr
    MAC          rx_crc_errors                +15           rx_fcs_err
    good         rx_packets                   +48000000     rx_frames
  ranked hypotheses:
    1. [12] H_CHANNEL   PHY/channel margin (temperature, EMI, cable/connector, PHY supply)
         evidence: symbol 912, FEC corr 250000 uncorr 12, SQI 7->3, carrier_down +2
         next:     log SQI/FEC vs temperature; cable test (TDR); PRBS BER at the corner; CMC/termination
  PASS c4_phy_margin_hot: top=H_CHANNEL expected=H_CHANNEL

SUMMARY: 5/5 PASS
```

### 8.3 출력 읽는 법

| 케이스 | 핵심 줄 | 읽는 법 |
|---|---|---|
| c0 clean control | `H_ABOVE_L2`, 오류 카운터 줄 없음 | **대조군**: 470만 프레임 받는 동안 L1/L2 오류 0 → 진단기가 "정상"을 정상이라 말하는지 확인 (오탐 방지). 실제로 ping이 안 되면 ARP·IP·VLAN 필터 등 MAC 위를 봐야 함 |
| c1 RGMII delay | `FCS rate 7.80%, PHY symbol/PCS errors 0, SQI drop 0, speed 1000` | 선로(PHY) 깨끗 + MAC FCS 폭발 + 1G → xMII 구간 = F3. 다음 확인 줄이 phy-mode·strap·MAC delay·xMII loopback·scope 순서 |
| c2 ring overflow | `rx_packets ... [32-bit wrap corrected]`, `rx_missed/fifo 240345 (4.771% of rx) ... FCS 0` | 카운터 wrap을 보정하지 않았으면 rx_packets delta가 음수/0이 돼 비율이 틀림 — 계측 함정. 오류 없이 missed만 = 호스트가 못 받아감 = F8/F6 |
| c3 oversize VLAN | 두 줄이 같은 81274, `evidence: oversize 81274` | `FrameTooLongErrors`(eth-mac)와 `etherStatsOversizePkts`(rmon)는 **같은 사건을 두 그룹이 센 것** → 합치면 162548로 부풀려짐, max로 정규화. FCS 0 = 비트 오류 아님 → F7 크기 정책 |
| c4 PHY margin hot | `symbol 912, FEC corr 250000 uncorr 12, SQI 7->3, carrier_down +2`, FCS +15 | FCS는 15개뿐이지만 FEC가 25만 블록을 고치고 있었음 → **FCS만 봤으면 놓쳤을 margin 소진** = F10. 점수 12로 단연 1위 |
| SUMMARY | `5/5 PASS` | 모든 mock이 기대 진단과 일치 |

- 한계 [미검증]: 실제 보드의 `ethtool -S` 출력으로는 돌려 보지 않음 — 드라이버별 이름 매핑과 임계값(FCS 0.1%, FEC 1000)은 예시값. `--groups` 출력의 `eth-mac-` 접두어 형식도 [추정].

## 9. 자주 틀리는 것

1. **"Link up이니 PHY·보드는 정상"** — link up은 PHY 선로 쪽만 증명. RGMII/SGMII 구간과 MAC 클럭은 별개 (F3).
2. **phy-mode를 MAC 입장으로 읽음** — Linux에서 `rgmii-id`는 "PHY가 지연을 넣는다". MAC은 `rgmii`가 아니면 자기 지연을 꺼야 한다 [1][2].
3. **"지연은 많을수록 안전"** — 1G에서 데이터 칸은 4 ns. 2 ns 중복(≈4 ns)은 누락만큼 나쁘다.
4. **T1에도 auto-neg가 기본이라고 가정** — Clause 98 AN은 선택, 보통 forced master/slave. `preferred-master/slave`는 AN이 있을 때만 의미 [6][9].
5. **BMSR link bit를 한 번만 읽음** — latch-low. 첫 읽기 0은 "그사이 끊긴 적 있음".
6. **ethtool -S 이름을 표준처럼 말함** — 드라이버 정의 이름은 표준이 아님 [4]. 면접에선 "the driver's CRC error counter — exact name varies — or FrameCheckSequenceErrors in the standard eth-mac group"처럼.
7. **절대값으로 판단** — 항상 테스트 전후 delta, 상대 쪽 카운터와 쌍으로, 32-bit wrap과 read-clear 주의.
8. **FEC 있는 링크에서 FCS 0 = 건강** — 1000BASE-T1은 FEC가 오류를 숨긴다. SQI·FEC 정정 카운트가 선행 지표.
9. **송신 측 tcpdump의 bad checksum을 버그로 보고** — TX checksum offload면 정상.
10. **ptp4l offset ≈ 0 = 시간 맞음** — 고정 비대칭은 서보가 못 본다. PPS + scope로 외부 검증.

## 10. 출처

1. Linux kernel docs — PHY Abstraction Layer (RGMII delay 1.5–2 ns, PHY_INTERFACE_MODE_RGMII/-ID/-RXID/-TXID 의미, PHY 쪽 지연 권장 이유, 증상) · https://docs.kernel.org/networking/phy.html
2. Linux DT binding ethernet-controller.yaml (rgmii 계열 주석, "RGMII specification requires a 2ns delay", `rx/tx-internal-delay-ps`, rgmii + 2000ps 불허) · https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/net/ethernet-controller.yaml
3. TI SNLA243 RGMII 타이밍 표 (TskewT −500…500 ps, TskewR 1/1.8/2.6 ns — 검색 요약 경유) · https://ti.com/lit/pdf/SNLA243 ; Intel 지원 문서 (RGMII v2.0 TskewT) · https://www.intel.co.uk/content/www/uk/en/support/programmable/articles/000079123.html
4. Linux kernel docs — Interface statistics (rtnl_link_stats64 vs ethtool 표준 그룹 vs 드라이버 정의, rx_missed/rx_dropped/rx_fifo/rx_crc 정의) · https://docs.kernel.org/networking/statistics.html
5. Linux net/ethtool/stats.c (eth-phy/eth-mac/eth-ctrl/rmon 통계 이름 문자열) · https://github.com/torvalds/linux/blob/master/net/ethtool/stats.c
6. ethtool(8) man page (master-slave 4값, `-S --groups`, `-g/-G`, `-c/-C`, `--show-eee`, `-T`, `-t external_lb`, `--cable-test`, `-k` 기능 이름, `--phy-statistics`) · https://man7.org/linux/man-pages/man8/ethtool.8.html
7. TI SNLA389G — DP83TC81x Open Alliance compliance (SQI 0–7 ↔ MSE 표, OA TC-1 단조 감소 요구) · https://ti.com/lit/pdf/SNLA389
8. LKML — "ethtool: provide UAPI for PHY Signal Quality Index (SQI)" · https://lkml.rescloud.iu.edu/hypermail/linux/kernel/2005.2/03032.html
9. IEEE 802.3bp task force 자료 (Clause 98 AN은 1000BASE-T1에서 선택, AN 미사용 시 master/slave는 관리·HW 설정 — draft 단계 자료, 최종 표준 원문 미확인) · https://www.ieee802.org/3/bp/public/may15/law_01a_0515.pdf · https://www.ieee802.org/3/bp/public/may15/tu_3bp_02a_0515.pdf ; SPE PHY master/slave DT 속성 논의 · https://lkml.iu.edu/hypermail/linux/kernel/2409.1/04836.html
10. MDIO 프레임 (C22 ST=01, OP 01/10, TA; C45 ST=00, address 프레임; MDC 최소 주기 400 ns) — Wikipedia MDIO · https://en.wikipedia.org/wiki/MDIO ; Intel Embedded Peripherals IP UG, MDIO Clause 45 frame · https://www.intel.com/content/www/us/en/docs/programmable/683130/21-3/mdio-frame-format-clause-45.html
11. Linux include/uapi/linux/mii.h · mdio.h (BMCR/BMSR 비트, MMD 번호 — LP00A/N06E에서 확인, 재인용) · https://github.com/torvalds/linux/blob/master/include/uapi/linux/mii.h · https://github.com/torvalds/linux/blob/master/include/uapi/linux/mdio.h
12. ptp4l(8) man page (delayAsymmetry, egressLatency, ingressLatency 정의) · https://manpages.debian.org/testing/linuxptp/ptp4l.8.en.html
13. Wikipedia — Ethernet over twisted pair (100BASE-T1 3B2T PAM3 66.66 MBd 15 m, 1000BASE-T1 PAM3 80B/81B RS-FEC 750 MBd 40 m, 1000BASE-T 4D-PAM5 125 MBd) · https://en.wikipedia.org/wiki/Ethernet_over_twisted_pair
14. RFC 2544 (프레임 크기·throughput 정의 — N06E에서 확인, 재인용) · https://www.rfc-editor.org/rfc/rfc2544
15. OPEN Alliance 100BASE-T1 Interoperability Test Suite v1.2 (신호 품질 표시 시험 항목 — 검색 요약 경유) · https://opensig.org/wp-content/uploads/2024/01/OA_100BASE-T1_Interoperability_Test_Suite_V1.2_final.pdf
