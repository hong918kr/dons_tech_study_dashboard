---
id: COMM-A03
time: 120분
updated: 2026-11-11
---
# COMM-A03 Ethernet PHY와 MDIO: Clause 22 레지스터와 링크 상태

## 학습 목표

- MDIO Clause 22 프레임 64비트(PRE 32 · ST 01 · OP · PHYAD · REGAD · TA · DATA 16)를 GPIO 비트뱅으로 만들고, **TA 에서 STA 가 선을 놓는 시점**과 실패한 읽기 뒤에도 클럭을 끝까지 보내야 하는 이유를 구현으로 보인다.
- BMCR / BMSR / PHYID / ANAR / ANLPAR(+ANER) 의 비트 의미, **자체 클리어**(RESET, ANRESTART) 와 **latch-low**(BMSR.2 LINK) 동작을 알고, PHYID 에서 OUI · model · revision 을 비트 순서까지 정확히 꺼낸다.
- soft reset → 광고(ANAR) → AN 재시작 → 링크 폴링 → 최고 공통 모드(HCD) 해석 순서의 드라이버를 짜고, 단계마다 타임아웃과 구별되는 에러 코드를 정한다.
- 리셋 안 끝남 · 리셋 중 무응답 · 케이블 없음 · 공통 모드 없음 · parallel detection · 링크 flap · 틀린 주소 · TA 미구동 · MDIO stuck low · 짧은 preamble 을 주입해 각각 다른 결과를 단언한다.
- Linux phylib(`genphy_update_link`, `mdio-bitbang`, `phy_print_status`)의 선택과 비교해 면접에서 설계 근거를 말한다.

## 1. 문제

보드 bring-up 에서 "이더넷 링크가 안 올라와요"를 받으면 첫 행동은 스코프가 아니라 **MDIO 로 PHY 레지스터를 읽는 것**이다 — PHY 가 대답은 하는지(ID), 리셋은 끝났는지, 상대가 뭘 광고했는지, 링크 비트가 왜 0 인지. 그래서 펌웨어·실리콘 검증 면접에서 "MDIO 프레임을 그려 보라", "BMSR 을 왜 두 번 읽나", "링크는 올라왔는데 패킷이 깨진다 — duplex mismatch 를 어떻게 찾나"가 단골이다.

MCU/SoC 안의 이더넷 MAC(STM32 ETH, i.MX ENET, Zynq GEM 등)은 데이터를 RMII/RGMII 로 외부 PHY 칩(KSZ8081, LAN8742, DP83848, RTL8201F …)에 보내고, PHY 를 **설정·감시**하는 건 별도의 2선 버스 MDC/MDIO(= SMI, Station Management Interface)다. MAC 쪽을 802.3 은 **STA**(station management entity)라고 부른다. Don 의 경험으로 치면 NVMe 컨트롤러의 레지스터를 읽어 상태를 판단하던 일과 같고, 버스는 COMM-B06 의 I2C 처럼 "주소 있는 레지스터 장치가 여럿 매달린 느린 관리 버스"다. 다만 MDIO 는 ACK 가 없고, 대신 **TA(turnaround)** 두 비트가 그 역할을 반쯤 한다.

```svg
<svg viewBox="0 0 760 240" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a3a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">이 레슨의 보드: MAC 하나, MDIO 버스 하나에 PHY 두 개</text> <rect x="20" y="34" width="190" height="176" rx="8" class="f-mut" stroke="currentColor"/> <text x="115" y="52" fill="currentColor" text-anchor="middle" font-weight="bold">MCU / SoC</text> <rect x="35" y="56" width="160" height="36" rx="4" class="f-acc" stroke="currentColor"/> <text x="115" y="79" fill="currentColor" text-anchor="middle">Ethernet MAC (DMA)</text> <rect x="35" y="104" width="160" height="44" rx="4" class="f-acc" stroke="currentColor"/> <text x="115" y="123" fill="currentColor" text-anchor="middle">STA: MDIO 컨트롤러</text> <text x="115" y="140" fill="currentColor" text-anchor="middle">또는 GPIO 비트뱅</text>
  <rect x="380" y="34" width="150" height="56" rx="6" class="f-ok" stroke="currentColor"/> <text x="455" y="56" fill="currentColor" text-anchor="middle" font-weight="bold">PHY @ addr 1</text> <text x="455" y="74" fill="currentColor" text-anchor="middle">ID 0x00221560</text> <rect x="380" y="154" width="150" height="56" rx="6" class="f-ok" stroke="currentColor"/> <text x="455" y="176" fill="currentColor" text-anchor="middle" font-weight="bold">PHY @ addr 3</text> <text x="455" y="194" fill="currentColor" text-anchor="middle">ID 0x001CC816</text> <path d="M195 66 H378" stroke="currentColor" stroke-width="3" marker-end="url(#a3a)"/> <text x="290" y="58" fill="currentColor" text-anchor="middle">RMII / RGMII (데이터)</text>
  <path d="M195 114 H430 M430 90 V154" stroke="currentColor" fill="none"/> <path d="M195 136 H490 M490 90 V154" stroke="currentColor" fill="none" stroke-dasharray="5 3"/> <text x="230" y="108" fill="currentColor">MDC → (STA 가 구동)</text> <text x="230" y="152" fill="currentColor">MDIO ↔ 양방향 3상태, 풀업</text> <rect x="590" y="34" width="150" height="56" rx="6" class="f-mut" stroke="currentColor"/> <text x="665" y="56" fill="currentColor" text-anchor="middle">마그네틱 + RJ45</text> <text x="665" y="74" fill="currentColor" text-anchor="middle">→ 링크 파트너</text> <path d="M530 62 H588" stroke="currentColor" stroke-width="3" marker-end="url(#a3a)"/> <text x="559" y="54" fill="currentColor" text-anchor="middle">MDI</text>
  <text x="545" y="186" fill="currentColor">케이블 없음 (AN 이 안 끝남)</text> <text x="20" y="232" fill="currentColor">PHYAD 5비트 → 한 버스에 최대 32개. 주소는 PHY 의 스트랩 핀이 정한다. 아무도 안 구동하면 MDIO 는 풀업으로 1 → 없는 주소는 0xFFFF.</text>
</svg>
```

레지스터는 32개(5비트 REGAD) 중 0–15 가 802.3 이 정한 표준, 16–31 은 벤더 전용이다. 이 레슨이 다루는 것:

```svg
<svg viewBox="0 0 760 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">Clause 22 기본 레지스터 (비트 15 → 0). 이름은 이 레슨의 줄임말</text> <text x="130.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">15</text> <text x="170.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">14</text> <text x="210.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">13</text> <text x="250.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">12</text> <text x="290.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">11</text> <text x="330.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">10</text> <text x="370.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">9</text> <text x="410.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">8</text>
  <text x="450.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">7</text> <text x="490.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">6</text> <text x="530.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">5</text> <text x="570.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">4</text> <text x="610.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">3</text> <text x="650.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">2</text> <text x="690.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">1</text> <text x="730.0" y="40" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="10" y="70" fill="currentColor" font-weight="bold">0 BMCR</text>
  <rect x="110" y="48" width="40" height="30" class="f-warn" stroke="currentColor"/> <text x="130.0" y="67" fill="currentColor" text-anchor="middle">RST</text> <rect x="150" y="48" width="40" height="30" fill="none" stroke="currentColor"/> <text x="170.0" y="67" fill="currentColor" text-anchor="middle">LPBK</text> <rect x="190" y="48" width="40" height="30" class="f-acc" stroke="currentColor"/> <text x="210.0" y="67" fill="currentColor" text-anchor="middle">S100</text> <rect x="230" y="48" width="40" height="30" class="f-acc" stroke="currentColor"/> <text x="250.0" y="67" fill="currentColor" text-anchor="middle">AN</text> <rect x="270" y="48" width="40" height="30" fill="none" stroke="currentColor"/>
  <text x="290.0" y="67" fill="currentColor" text-anchor="middle">PDN</text> <rect x="310" y="48" width="40" height="30" fill="none" stroke="currentColor"/> <text x="330.0" y="67" fill="currentColor" text-anchor="middle">ISO</text> <rect x="350" y="48" width="40" height="30" class="f-warn" stroke="currentColor"/> <text x="370.0" y="67" fill="currentColor" text-anchor="middle">ANR</text> <rect x="390" y="48" width="40" height="30" class="f-acc" stroke="currentColor"/> <text x="410.0" y="67" fill="currentColor" text-anchor="middle">FD</text> <rect x="430" y="48" width="40" height="30" fill="none" stroke="currentColor"/> <text x="450.0" y="67" fill="currentColor" text-anchor="middle">COL</text>
  <rect x="470" y="48" width="40" height="30" fill="none" stroke="currentColor"/> <text x="490.0" y="67" fill="currentColor" text-anchor="middle">S1G</text> <rect x="510" y="48" width="240" height="30" fill="none" stroke="currentColor"/> <text x="630" y="67" fill="currentColor" text-anchor="middle">예약</text> <text x="10" y="116" fill="currentColor" font-weight="bold">1 BMSR</text> <rect x="110" y="94" width="40" height="30" fill="none" stroke="currentColor"/> <text x="130.0" y="113" fill="currentColor" text-anchor="middle">T4</text> <rect x="150" y="94" width="40" height="30" class="f-ok" stroke="currentColor"/> <text x="170.0" y="113" fill="currentColor" text-anchor="middle">100F</text>
  <rect x="190" y="94" width="40" height="30" class="f-ok" stroke="currentColor"/> <text x="210.0" y="113" fill="currentColor" text-anchor="middle">100H</text> <rect x="230" y="94" width="40" height="30" class="f-ok" stroke="currentColor"/> <text x="250.0" y="113" fill="currentColor" text-anchor="middle">10F</text> <rect x="270" y="94" width="40" height="30" class="f-ok" stroke="currentColor"/> <text x="290.0" y="113" fill="currentColor" text-anchor="middle">10H</text> <rect x="310" y="94" width="40" height="30" fill="none" stroke="currentColor"/> <text x="330.0" y="113" fill="currentColor" text-anchor="middle">T2F</text> <rect x="350" y="94" width="40" height="30" fill="none" stroke="currentColor"/>
  <text x="370.0" y="113" fill="currentColor" text-anchor="middle">T2H</text> <rect x="390" y="94" width="40" height="30" fill="none" stroke="currentColor"/> <text x="410.0" y="113" fill="currentColor" text-anchor="middle">EST</text> <rect x="430" y="94" width="40" height="30" fill="none" stroke="currentColor"/> <text x="450.0" y="113" fill="currentColor" text-anchor="middle">UNI</text> <rect x="470" y="94" width="40" height="30" class="f-mut" stroke="currentColor"/> <text x="490.0" y="113" fill="currentColor" text-anchor="middle">MFPS</text> <rect x="510" y="94" width="40" height="30" class="f-acc" stroke="currentColor"/> <text x="530.0" y="113" fill="currentColor" text-anchor="middle">ANC</text>
  <rect x="550" y="94" width="40" height="30" fill="none" stroke="currentColor"/> <text x="570.0" y="113" fill="currentColor" text-anchor="middle">RF</text> <rect x="590" y="94" width="40" height="30" fill="none" stroke="currentColor"/> <text x="610.0" y="113" fill="currentColor" text-anchor="middle">ANA</text> <rect x="630" y="94" width="40" height="30" class="f-warn" stroke="currentColor"/> <text x="650.0" y="113" fill="currentColor" text-anchor="middle">LINK</text> <rect x="670" y="94" width="40" height="30" fill="none" stroke="currentColor"/> <text x="690.0" y="113" fill="currentColor" text-anchor="middle">JAB</text> <rect x="710" y="94" width="40" height="30" fill="none" stroke="currentColor"/>
  <text x="730.0" y="113" fill="currentColor" text-anchor="middle">EXT</text> <text x="10" y="162" fill="currentColor" font-weight="bold">4 ANAR</text> <rect x="110" y="140" width="40" height="30" fill="none" stroke="currentColor"/> <text x="130.0" y="159" fill="currentColor" text-anchor="middle">NP</text> <rect x="150" y="140" width="40" height="30" fill="none" stroke="currentColor"/> <text x="170.0" y="159" fill="currentColor" text-anchor="middle">ACK</text> <rect x="190" y="140" width="40" height="30" fill="none" stroke="currentColor"/> <text x="210.0" y="159" fill="currentColor" text-anchor="middle">RF</text> <rect x="230" y="140" width="40" height="30" fill="none" stroke="currentColor"/>
  <text x="250.0" y="159" fill="currentColor" text-anchor="middle">XNP</text> <rect x="270" y="140" width="40" height="30" class="f-acc" stroke="currentColor"/> <text x="290.0" y="159" fill="currentColor" text-anchor="middle">ASYM</text> <rect x="310" y="140" width="40" height="30" class="f-acc" stroke="currentColor"/> <text x="330.0" y="159" fill="currentColor" text-anchor="middle">PAUS</text> <rect x="350" y="140" width="40" height="30" fill="none" stroke="currentColor"/> <text x="370.0" y="159" fill="currentColor" text-anchor="middle">T4</text> <rect x="390" y="140" width="40" height="30" class="f-ok" stroke="currentColor"/> <text x="410.0" y="159" fill="currentColor" text-anchor="middle">100F</text>
  <rect x="430" y="140" width="40" height="30" class="f-ok" stroke="currentColor"/> <text x="450.0" y="159" fill="currentColor" text-anchor="middle">100H</text> <rect x="470" y="140" width="40" height="30" class="f-ok" stroke="currentColor"/> <text x="490.0" y="159" fill="currentColor" text-anchor="middle">10F</text> <rect x="510" y="140" width="40" height="30" class="f-ok" stroke="currentColor"/> <text x="530.0" y="159" fill="currentColor" text-anchor="middle">10H</text> <rect x="550" y="140" width="200" height="30" class="f-mut" stroke="currentColor"/> <text x="650.0" y="159" fill="currentColor" text-anchor="middle" font-family="monospace">selector 00001</text> <text x="10" y="208" fill="currentColor" font-weight="bold">5 ANLPAR</text>
  <rect x="110" y="186" width="40" height="30" fill="none" stroke="currentColor"/> <text x="130.0" y="205" fill="currentColor" text-anchor="middle">NP</text> <rect x="150" y="186" width="40" height="30" class="f-acc" stroke="currentColor"/> <text x="170.0" y="205" fill="currentColor" text-anchor="middle">ACK</text> <rect x="190" y="186" width="40" height="30" fill="none" stroke="currentColor"/> <text x="210.0" y="205" fill="currentColor" text-anchor="middle">RF</text> <rect x="230" y="186" width="40" height="30" fill="none" stroke="currentColor"/> <text x="250.0" y="205" fill="currentColor" text-anchor="middle">XNP</text> <rect x="270" y="186" width="40" height="30" fill="none" stroke="currentColor"/>
  <text x="290.0" y="205" fill="currentColor" text-anchor="middle">ASYM</text> <rect x="310" y="186" width="40" height="30" fill="none" stroke="currentColor"/> <text x="330.0" y="205" fill="currentColor" text-anchor="middle">PAUS</text> <rect x="350" y="186" width="40" height="30" fill="none" stroke="currentColor"/> <text x="370.0" y="205" fill="currentColor" text-anchor="middle">T4</text> <rect x="390" y="186" width="40" height="30" class="f-ok" stroke="currentColor"/> <text x="410.0" y="205" fill="currentColor" text-anchor="middle">100F</text> <rect x="430" y="186" width="40" height="30" class="f-ok" stroke="currentColor"/> <text x="450.0" y="205" fill="currentColor" text-anchor="middle">100H</text>
  <rect x="470" y="186" width="40" height="30" class="f-ok" stroke="currentColor"/> <text x="490.0" y="205" fill="currentColor" text-anchor="middle">10F</text> <rect x="510" y="186" width="40" height="30" class="f-ok" stroke="currentColor"/> <text x="530.0" y="205" fill="currentColor" text-anchor="middle">10H</text> <rect x="550" y="186" width="200" height="30" class="f-mut" stroke="currentColor"/> <text x="650.0" y="205" fill="currentColor" text-anchor="middle" font-family="monospace">selector 00001</text> <text x="10" y="254" fill="currentColor" font-weight="bold">2/3 PHYID</text> <rect x="110" y="232" width="640" height="30" class="f-mut" stroke="currentColor"/><text x="430" y="251" fill="currentColor" text-anchor="middle">PHYID1[15:0] = OUI 비트 3..18</text> <rect x="110" y="268" width="240" height="30" class="f-mut" stroke="currentColor"/><text x="230" y="287" fill="currentColor" text-anchor="middle">OUI 비트 19..24</text>
  <rect x="350" y="268" width="240" height="30" fill="none" stroke="currentColor"/><text x="470" y="287" fill="currentColor" text-anchor="middle">model [9:4]</text> <rect x="590" y="268" width="160" height="30" fill="none" stroke="currentColor"/><text x="670" y="287" fill="currentColor" text-anchor="middle">rev [3:0]</text> <rect x="10" y="314" width="14" height="12" class="f-warn"/><text x="30" y="325" fill="currentColor">자체 클리어(RST, ANR) / latch-low(LINK)</text> <rect x="300" y="314" width="14" height="12" class="f-acc"/><text x="320" y="325" fill="currentColor">드라이버가 꼭 보는 비트</text> <rect x="490" y="314" width="14" height="12" class="f-ok"/><text x="510" y="325" fill="currentColor">10/100 능력·광고 비트</text>
</svg>
```

**이 레슨의 mock PHY 정의** (802.3 Clause 22/28 을 단순화한 모델. 표시한 수치는 mock 값이고 실제 칩은 데이터시트 확인 권장):

| 항목 | mock 의 동작 |
|---|---|
| 프레임 수신 | MDC 상승 에지마다 MDIO 를 샘플하는 상태 기계. 1 이 32개 이상 이어진 뒤의 0 을 ST 첫 비트로 본다 (BMSR.6 MFPS = 1 이면 1개). ST = 00(Clause 45)·잘못된 OP 는 무시 |
| 읽기 응답 | REGAD 마지막 비트를 샘플한 에지 다음 에지까지 Z, 그 뒤 TA 둘째 비트 0, D15..D0 를 **상승 에지 직후** 구동, 64번째 에지에서 놓음 |
| BMCR | bit15 RESET: 쓰면 `reset_us`(기본 5 ms) 동안 1 로 읽히고 끝나면 0 + 모든 레지스터 기본값 + AN 자동 시작. bit12 AN enable, bit9 restart(즉시 0), bit13/8 속도·이중(AN 꺼질 때만 의미) |
| BMSR | 능력 비트 0x7809 (100FD/HD, 10FD/HD, AN 가능, 확장 레지스터) + bit5 AN 완료 + **bit2 LINK latch-low**: 끊기면 0 으로 잠기고, 읽으면 그 값을 돌려준 뒤 현재 상태로 다시 장전 |
| AN | `an_ms`(기본 1.5 s) 뒤 완료. 파트너가 오토네고면 ANLPAR = 파트너 광고 \| 0x4001(ACK, selector), ANER.0 = 1. 공통 모드가 없으면 페이지만 받고 완료 안 됨. 파트너가 **강제 속도**면 parallel detection: 그 속도의 **반이중**(우리가 그 HD 를 광고할 때만), ANLPAR 에 그 비트만, ANER.0 = 0. 케이블 없음 → 영원히 미완료 |
| 장애 플래그 | `reset_stuck`, `dead_in_reset`(리셋 중 MDIO 무응답), `ta_broken`(TA 를 0 으로 안 끌어내림), `anar_ro`, 링크 끊김 일정 `flaps[]`, 버스의 `stuck_low` |
| 시간 | MDC 2.5 MHz(반주기 200 ns) → 프레임 64 MDC = 25.6 µs. `sim_sleep_us()` 가 시뮬레이션 시간을 민다 |

### Part A — MDIO 프레임 비트뱅과 PHY ID

- `mdio_read(bus, phy, reg)` / `mdio_write(...)`: 주어진 GPIO 원시 함수(`mdio_bit_out`, `mdio_bit_in`, Linux `mdiobb_ops` 모양)로 프레임을 만든다. 읽기는 REGAD 뒤 **선을 놓고** TA 2비트 + DATA 16비트를 클럭하며 읽는다. TA 첫 비트(Z 구간)가 0 이면 `MDIO_EBUS`, TA 둘째 비트가 1 이면 `MDIO_ENOACK`(아무도 대답 안 함). 실패해도 16 데이터 클럭은 끝까지.
- `phyid_decode(id1, id2)`: PHYID1 = OUI 비트 3..18, PHYID2[15:10] = OUI 비트 19..24, [9:4] model, [3:0] revision. OUI 비트 번호는 **전송 순서**(옥텟마다 LSB 먼저)라서 사람이 쓰는 `00-10-A1` 로 돌리려면 옥텟마다 비트를 뒤집어야 한다.
- 테스트: 읽기·쓰기 프레임의 64비트와 "누가 구동했나"를 트레이스로 정확히, 벤더 레지스터에 16비트 값 65536개 전부 왕복, 32주소 × 레지스터 0–6, 버스 싸움 0, 실제 칩 ID 3개 + OUI 2^22개 전수.

### Part B — 리셋 · latch-low · 해석 · 탐색, 그리고 장애

- `phy_soft_reset`: BMCR.15 쓰기 → 1 ms 마다 폴링, 0.5 s(802.3 의 리셋 완료 상한으로 알려진 값 — 스펙 확인 권장) 타임아웃. 리셋 중 무응답(ENOACK)은 "아직"이지 실패가 아니다. 끝까지 무응답 = `PHY_ENODEV`, 응답은 하는데 bit15 가 안 내려감 = `PHY_ERESET_TIMEOUT`.
- `phy_read_link`: BMSR 두 번. **첫 값 = 래치**(지난 읽기 뒤 한 번이라도 끊겼나), **둘째 값 = 지금**. up 이라 믿었는데 래치가 0 이면 `dropped` + `flaps++`.
- `an_resolve(adv, lpa)`: 공통 기술 비트 중 우선순위 최고 하나 — 100BASE-TX FD > 100BASE-T4 > 100BASE-TX HD > 10BASE-T FD > 10BASE-T HD (802.3 Annex 28B 의 순서를 Linux `mii_nway_result` 대로 — 아래 Q5).
- `phy_probe`: 주소 0..31 의 ID 를 읽어 목록. 버스 고장(EBUS)은 즉시 중단, `0x?FFFFFFF`·`0` 은 "없음", 용량 초과는 `PHY_ENOBUF`.
- 장애: 위 목표의 10가지 + 랜덤 flap 1024개 × 폴링 7000여 번을 기준값(구간 교차 계산)과 대조.

### Part C — bring-up 드라이버와 상태 줄

- `phy_bringup(dev, adv_mask, &result)`: ① ID ② soft reset ③ BMSR 능력 ∩ mask 로 ANAR 만들기 ④ 쓰고 **되읽기** ⑤ BMCR = AN enable + restart ⑥ 링크 폴링(10 ms, 3 s 타임아웃 — 이 레슨의 정의) ⑦ ANLPAR·ANER 읽고 속도 / 이중 / parallel detection / pause 해석. 단계마다 시간 찍힌 로그.
- `phy_format_status`: `phy1: Link is Up - 100Mbps/Full - flow control rx/tx` (Linux `phy_print_status` 형식을 따름), 용량이 모자라면 `PHY_ENOBUF`.
- 테스트: 광고 마스크 16 × 파트너 페이지 32 = 512 bring-up 전수, parallel detection 15 × 2, 장애 표 15줄, 운영 중 flap 감시 로그.

## 2. Problem (English)

> **Bring up an Ethernet PHY over a bit-banged MDIO bus.**
>
> You have GPIO access to MDC (output) and MDIO (bidirectional, pulled up). Up to 32 PHYs share the bus. Implement:
>
> ```c
> int  mdio_read(MdioBus *b, int phy, int reg);                 /* 0..0xFFFF or MDIO_ENOACK / MDIO_EBUS / MDIO_EINVAL */
> int  mdio_write(MdioBus *b, int phy, int reg, uint16_t val);
> void phyid_decode(uint16_t id1, uint16_t id2, PhyIdInfo *out); /* OUI as written (00-10-A1 -> 0x0010A1), model, rev */
> int  phy_soft_reset(PhyDev *d);                                /* BMCR.15, poll, timeout */
> int  phy_read_link(PhyDev *d, LinkSample *s);                  /* current link + "dropped since last poll" */
> uint16_t an_resolve(uint16_t adv, uint16_t lpa);               /* highest common mode, 0 if none */
> int  phy_probe(MdioBus *b, ProbeHit *hits, int cap, int *n);
> int  phy_bringup(PhyDev *d, uint16_t adv_mask, PhyResult *r);
> int  phy_format_status(const PhyResult *r, char *buf, size_t cap);
> ```
>
> Clause 22 frame: 32 preamble ones, ST = 01, OP = 10 read / 01 write, PHYAD[4:0], REGAD[4:0], TA (read: Z then PHY drives 0; write: STA drives 10), 16 data bits MSB first. Data is sampled on the rising edge of MDC.
>
> Requirements: a read must release MDIO before the turnaround; a failed read must still clock out the frame; the link bit in BMSR is latching-low and you must neither miss a short link drop nor report "down" while the link is up; distinguish "PHY absent", "bus stuck", "reset never completes", "no cable", "no common mode"; no dynamic allocation.
>
> Example:
>
> ```text
> mdio_read(bus, 1, 1)  bits: 1x32 01 10 00001 00001 Z0 0111100000101101  -> 0x782D
> phy_bringup(phy1, 10/100 + pause) -> "phy1: Link is Up - 100Mbps/Full - flow control rx/tx"
> ```
>
> Follow-ups: Why is BMSR read twice — and when is reading it twice wrong? What happens if the partner is forced to 100 Mb/s full duplex? How would you detect a PHY whose MDIO line is shorted to ground? How does Clause 45 differ?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 아홉 군데: Part A — `mdio_read`, `mdio_write`, `phyid_decode` / Part B — `phy_soft_reset`, `phy_read_link`, `an_resolve`, `phy_probe` / Part C — `phy_bringup`, `phy_format_status`. 레지스터 상수(Linux `mii.h` 이름), mock PHY, MDIO 선과 GPIO HAL, 비트 원시 함수, 로그, 하네스는 주어진다. 스텁은 선을 건드리지 않고 "없음/실패" 값을 돌려주므로 멈추지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
== Part A: MDIO frames ==
FAIL starter.c:522  v1 == 0x7829
FAIL starter.c:523  v2 == 0x782D
...
FAIL starter.c:533  mdio_write(&g_bus, 1, 0x10, 0xA5C3) == MDIO_OK
...
18/110 passed
```

통과하는 18개는 mock 자체 점검과, 스텁이 "없음"을 돌려줘서 우연히 맞는 단언(없는 주소 → ENOACK 등)이다.

## 4. 힌트

<details>
<summary>힌트 1 — 프레임은 비트 묶음 여섯 개</summary>

`mdio_bits_out(b, val, n)` 이 MSB 부터 n 비트를 보낸다. 읽기 = `preamble_bits` 번 `mdio_bit_out(b,1)` → `(0x1,2)` ST → `(0x2,2)` OP → `(phy,5)` → `(reg,5)`. 쓰기는 OP `0x1`, 그 뒤 TA `(0x2,2)` 와 `(val,16)` 까지 STA 가 보낸다. 프리앰블 수는 하드코딩하지 말고 `b->preamble_bits` (짧은 프리앰블 실험에 쓴다).

</details>

<details>
<summary>힌트 2 — TA: 놓는 시점과 두 비트의 의미</summary>

REGAD 마지막 비트 뒤 바로 `gpio_mdio_dir(b, 0)`. 그 다음 `mdio_bit_in` 두 번: 첫 값은 아무도 구동하지 않는 구간이라 풀업으로 **1 이어야** 하고(0 이면 선이 GND 에 붙음 → `MDIO_EBUS`), 둘째 값은 PHY 가 구동한 **0** 이어야 한다(1 이면 대답 없음 → `MDIO_ENOACK`, 단 `ignore_ta_mask` 비트가 켜진 주소는 무시). 판정은 16비트를 다 클럭한 **뒤에**.

</details>

<details>
<summary>힌트 3 — OUI 비트 순서</summary>

`s = id1 << 6 | id2 >> 10` 이 OUI 비트 3..24 를 MSB 쪽부터 담은 22비트다(비트 1·2 는 항상 0 이라 위에 0 두 개를 붙인 24비트 열로 보면 된다). 24비트 열을 8비트씩 잘라 **각 바이트의 비트 순서를 뒤집으면** 사람이 쓰는 옥텟이 나온다. 검산: KSZ8081 `0022 1560` → `00-10-A1`.

</details>

<details>
<summary>힌트 4 — 리셋 폴링의 세 갈래</summary>

`sleep(poll) → read BMCR` 루프. `v >= 0` 이면 응답한 것(seen = true), bit15 가 0 이면 OK. `MDIO_EBUS` 는 즉시 반환. `ENOACK` 은 계속. 경과 시간이 타임아웃을 넘으면 `seen ? PHY_ERESET_TIMEOUT : PHY_ENODEV`.

</details>

<details>
<summary>힌트 5 — 링크 읽기 = 두 번, 둘 다 쓴다</summary>

`a = BMSR`, `b = BMSR`. `latched_up = a & LSTATUS`, `up = b & LSTATUS`, `an_done = b & ANEGCOMPLETE`, `dropped = d->was_up && !latched_up`. 마지막에 `d->was_up = up`. 첫 값을 버리면 짧은 끊김을 놓치고, 첫 값만 쓰면 지금 up 인 링크를 down 이라 한다.

</details>

<details>
<summary>힌트 6 — bring-up 의 AN 타임아웃 진단</summary>

타임아웃이 나면 ANLPAR 를 한 번 더 읽는다. 기술 비트가 있는데(= 상대 페이지는 받았다) 우리 광고와 겹치는 게 없으면 `PHY_EAN_NOCOMMON`, 아니면 `PHY_EAN_TIMEOUT`(케이블·파트너 쪽 문제). 링크가 올라오면 `ANER & EXPANSION_NWAY == 0` 이 parallel detection 의 표시다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "MDIO 는 STA 가 클럭을 주는 반이중 직렬 관리 버스. 프레임 64비트: 프리앰블 32, ST 01, OP, PHYAD, REGAD, TA, 데이터 16. 모두 MDC 상승 에지에서 샘플."
2. "읽기에선 TA 에서 버스 방향이 바뀐다. STA 는 TA 전에 놓고, PHY 는 첫 TA 비트를 Z 로 두고 둘째를 0 으로 끌어내린다 — 그 0 이 사실상의 ACK."
3. "링크 비트는 latch-low. 첫 읽기는 '그 사이 끊긴 적 있나', 둘째는 '지금'. 폴링 주기보다 짧은 끊김은 래치로만 보인다."
4. "bring-up 은 ID → 리셋(완료 폴링) → 광고 → AN 재시작 → 링크 폴링 → ANAR∩ANLPAR 의 최고 공통 모드. 단계마다 타임아웃과 다른 에러 코드."
5. 꼬리질문 대비: parallel detection 과 duplex mismatch, Clause 45, phylib 이 실제로 하는 일.

### Part A — 프레임 한 장을 비트까지

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">MDIO 읽기 프레임 — phy1 BMSR(reg 1) = 0x782D (solution.c 의 실제 트레이스)</text> <rect x="176" y="96" width="252" height="34" class="f-acc" opacity="0.35"/> <rect x="428" y="96" width="9" height="34" class="f-mut" opacity="0.6"/> <rect x="437" y="96" width="306" height="34" class="f-ok" opacity="0.35"/> <rect x="20" y="96" width="150" height="34" class="f-acc" opacity="0.2"/> <text x="10" y="64" fill="currentColor">MDC</text><text x="10" y="118" fill="currentColor">MDIO</text> <path d="M60 70 h8 v-20 h8 v20 h8 v-20 h8 v20 h8 v-20 h8 v20" stroke="currentColor" fill="none"/> <text x="128" y="66" fill="currentColor" font-size="12">… ×32</text> <path d="M176 70 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20 h9 v-20 h9 v20" stroke="currentColor" fill="none"/>
  <line x1="185" y1="74" x2="185" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="203" y1="74" x2="203" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="221" y1="74" x2="221" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="239" y1="74" x2="239" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="257" y1="74" x2="257" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="275" y1="74" x2="275" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="293" y1="74" x2="293" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="311" y1="74" x2="311" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="329" y1="74" x2="329" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/>
  <line x1="347" y1="74" x2="347" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="365" y1="74" x2="365" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="383" y1="74" x2="383" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="401" y1="74" x2="401" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="419" y1="74" x2="419" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="437" y1="74" x2="437" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="455" y1="74" x2="455" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="473" y1="74" x2="473" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="491" y1="74" x2="491" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/>
  <line x1="509" y1="74" x2="509" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="527" y1="74" x2="527" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="545" y1="74" x2="545" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="563" y1="74" x2="563" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="581" y1="74" x2="581" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="599" y1="74" x2="599" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="617" y1="74" x2="617" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="635" y1="74" x2="635" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="653" y1="74" x2="653" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/>
  <line x1="671" y1="74" x2="671" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="689" y1="74" x2="689" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="707" y1="74" x2="707" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="725" y1="74" x2="725" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <line x1="743" y1="74" x2="743" y2="92" stroke="currentColor" stroke-dasharray="2 2" opacity="0.5"/> <path d="M20 100 H176 V126 H194 V100 H212 H230 V126 H248 H266 H284 H302 H320 V100 H338 V126 H356 H374 H392 H410 V100 H428 H437 V126 H455 H473 V100 H491 H509 H527 H545 V126 H563 H581 H599 H617 H635 V100 H653 V126 H671 V100 H689 H707 V126 H725 V100 H743 H758" stroke="currentColor" stroke-width="2" fill="none"/> <text x="95" y="148" fill="currentColor" text-anchor="middle">PRE 32 × 1</text> <text x="185" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="203" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">1</text>
  <text x="221" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">1</text> <text x="239" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="257" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="275" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="293" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="311" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="329" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">1</text> <text x="347" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="365" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <text x="383" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="401" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="419" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">1</text> <text x="432" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">Z</text> <text x="446" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="464" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="482" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">1</text> <text x="500" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">1</text> <text x="518" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">1</text>
  <text x="536" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">1</text> <text x="554" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="572" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="590" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="608" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="626" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="644" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">1</text> <text x="662" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="680" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">1</text>
  <text x="698" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">1</text> <text x="716" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">0</text> <text x="734" y="148" fill="currentColor" text-anchor="middle" font-family="monospace">1</text> <path d="M177 158 V164 H211 V158" stroke="currentColor" fill="none"/> <text x="194.0" y="180" fill="currentColor" text-anchor="middle">ST</text> <path d="M213 158 V164 H247 V158" stroke="currentColor" fill="none"/> <text x="230.0" y="180" fill="currentColor" text-anchor="middle">OP</text> <path d="M249 158 V164 H337 V158" stroke="currentColor" fill="none"/> <text x="293.0" y="180" fill="currentColor" text-anchor="middle">PHYAD=1</text>
  <path d="M339 158 V164 H427 V158" stroke="currentColor" fill="none"/> <text x="383.0" y="180" fill="currentColor" text-anchor="middle">REGAD=1</text> <path d="M429 158 V164 H463 V158" stroke="currentColor" fill="none"/> <text x="446.0" y="180" fill="currentColor" text-anchor="middle">TA</text> <path d="M465 158 V164 H751 V158" stroke="currentColor" fill="none"/> <text x="608.0" y="180" fill="currentColor" text-anchor="middle">DATA = 0x782D (MSB 먼저)</text> <rect x="20" y="200" width="16" height="12" class="f-acc" opacity="0.6"/><text x="42" y="211" fill="currentColor">STA(MAC) 가 구동 — MDC low 에서 바꾸고 상승 에지에서 PHY 가 샘플</text> <rect x="20" y="220" width="16" height="12" class="f-mut"/><text x="42" y="231" fill="currentColor">TA 첫 비트: 아무도 안 구동 → 풀업 1 (STA 는 여기서 이미 놓았어야 함)</text> <rect x="20" y="240" width="16" height="12" class="f-ok" opacity="0.6"/><text x="42" y="251" fill="currentColor">PHY 가 구동 — 상승 에지 직후 바꿈 (실제 tCO ≤ 300 ns), STA 는 다음 상승 에지에서 샘플</text>
  <text x="20" y="276" fill="currentColor">점선 = MDC 상승 에지(샘플 시점). 프레임 = 64 MDC = 25.6 µs @ 2.5 MHz. 마지막(64번째) 에지 뒤 PHY 가 선을 놓는다.</text>
</svg>
```

위 그림은 solution.c 가 찍은 실제 트레이스(`who` 줄: S = STA, z = 아무도, P = PHY)를 그대로 그린 것이다:

```text
read  phy1 reg1 bits: 11111111111111111111111111111111 01 10 00001 00001 10 0111100000101101
                who : SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS SS SS SSSSS SSSSS zP PPPPPPPPPPPPPPPP
write phy1 reg16 bits: 11111111111111111111111111111111 01 01 00001 10000 10 1010010111000011
one frame: 64 MDC edges, 25600 ns
```

| 필드 | 비트 | 읽기 | 쓰기 | 누가 구동 |
|---|---|---|---|---|
| PRE | 32 | 1…1 | 1…1 | STA (PHY 는 1 을 32개 세고 동기를 잡는다) |
| ST | 2 | 01 | 01 | STA (00 이면 Clause 45 프레임) |
| OP | 2 | 10 | 01 | STA |
| PHYAD | 5 | 주소 | 주소 | STA |
| REGAD | 5 | 레지스터 | 레지스터 | STA |
| TA | 2 | Z, 0 | 1, 0 | 읽기: 첫 비트 아무도, 둘째 PHY / 쓰기: STA |
| DATA | 16 | MSB 먼저 | MSB 먼저 | 읽기: PHY / 쓰기: STA |

**TA 의 이유.** MDIO 는 3상태 양방향 선이라 두 드라이버가 동시에 구동하면 버스 싸움(bus contention)이 난다. 첫 TA 비트 시간은 STA 가 놓고 PHY 는 아직 안 잡는 **완충**이고, 둘째 비트는 PHY 가 "내가 이 프레임을 받았다"를 0 으로 표시한다. 그래서 `mdio_read` 는 REGAD 직후 `gpio_mdio_dir(b, 0)` 이다. 하네스는 반주기마다 "STA 와 PHY 가 동시에 구동했나"를 세고(`contention`), 올바른 코드는 모든 테스트 뒤에도 0 이다. 연습 5 의 순진한 읽기(놓지 않음)는 프레임당 34 반주기의 싸움을 만든다.

**TA 첫 비트 검사(이 레슨이 추가한 것).** 그 구간엔 아무도 구동하지 않으므로 풀업 때문에 1 이어야 한다. 0 이면 누군가(혹은 단락)가 선을 잡고 있다 → `MDIO_EBUS`. Linux `mdio-bitbang.c` 는 이 비트를 따로 보지 않고 TA 둘째 비트만 확인한다 — 그 이유는 샘플 시점이 다르기 때문이다(아래). 덕분에 stuck-low 버스가 "모든 주소에 ID 0x00000000 인 PHY 가 있다"로 오진되지 않고 탐색 첫 프레임에서 `PHY_EBUS` 로 끝난다.

**실패해도 끝까지 클럭하는 이유.** 탐색 중 대부분의 주소는 비어 있고, 다른 주소의 PHY 들은 이 프레임을 남의 것으로 보고 "TA+DATA 18비트를 흘려보내는" 상태다. 실패를 보자마자 return 하면 그 PHY 들은 다음 프레임의 프리앰블 앞 18비트를 이전 프레임의 꼬리로 먹고, 남은 14개로는 프리앰블 조건(32개)을 못 채운다. mutant 실험에서 실제로 32주소 스캔 테스트가 깨졌다. Linux 는 TA 실패 시 32비트를 흘려보낸다(`mdiobb_read_common` 의 "flush any bits it may be trying to send").

**샘플 시점: 이 레슨 vs Linux.** 이 모델은 "PHY 는 상승 에지 직후 바꾸고, STA 는 다음 상승 에지 직전에 읽는다"(802.3 의 'MDC 상승 에지에 동기 샘플')를 따랐다. Linux `mdiobb_get_bit` 은 MDC 를 올리고 `MDIO_READ_DELAY`(350 ns, 주석: PHY 가 데이터를 내는 데 최대 300 ns) 기다린 뒤 내리고 나서 읽는다 — PHY 가 **방금 그 에지에 반응해 낸** 비트를 같은 클럭 주기 안에서 읽는 것이다. 그래서 Linux 는 TA 를 `get_bit` 한 번(=둘째 TA 비트)만 읽고 데이터 16, 마지막에 한 번 더 클럭한다. 총 MDC 수는 같다(64). `MDIO_DELAY` 250 ns × 2 = 500 ns 주기는 "최소 MDC 주기 400 ns + 여유" 라는 주석과 함께 있다 → 2.5 MHz 상한.

**시간 비용.** 프레임 64 MDC = 2.5 MHz 에서 25.6 µs (실측 `25600 ns`). 링크 폴링(BMSR 2번) = 51.2 µs. 연습 4 에서 PHY 개수·preamble suppression·GPIO 비트뱅 속도별로 계산한다. MDIO 는 데이터 경로가 아니라 관리 경로라 이 정도면 충분하지만, GPIO 비트뱅은 그 시간 동안 **CPU 를 점유**한다는 점이 다르다.

**PHY ID.** 실행 결과:

```text
KSZ8081  (Micrel) PHYID 0022 1560 -> OUI 00-10-A1 model 0x16 rev 0
RTL8201F (Realtek) PHYID 001C C816 -> OUI 00-E0-4C model 0x01 rev 6
RTL8211F (Realtek) PHYID 001C C916 -> OUI 00-E0-4C model 0x11 rev 6
phyid decode vs per-bit reference: 4194304 OUIs, bad=0
```

세 ID 는 Linux 소스의 `PHY_ID_KSZ8081 0x00221560`(include/linux/micrel_phy.h), `0x001cc816` "RTL8201F Fast Ethernet", `0x001cc916` "RTL8211F Gigabit Ethernet"(drivers/net/phy/realtek) 에서 가져왔고, OUI 00-10-A1(Kendin → Micrel 인수)과 00-E0-4C(Realtek)는 IEEE 등록부 조회 결과와 맞는다 — 즉 디코더가 실제 두 벤더의 ID 와 등록 OUI 를 독립적으로 이어 준다. KSZ8081 손계산:

| 단계 | 값 |
|---|---|
| `s = id1<<6 \| id2>>10` | `0x0022<<6 \| 0x1560>>10` = `0x880 \| 0x5` = `0x000885` |
| 24비트 열을 바이트로 | `00` `08` `85` |
| 바이트마다 비트 뒤집기 | `00` `10` `A1` |

비트 1·2 가 빠지는 이유: OUI 첫 옥텟의 LSB 두 개(전송 순서로 첫 두 비트)는 I/G·U/L 비트라 OUI 에선 항상 0 이다. 하네스는 디코더(시프트 + 바이트 뒤집기)를 **비트 하나씩 802.3 번호로 옮기는** 기준 인코더와 2^22 개 OUI 전부에서 대조한다. 오래된 일부 벤더 문서는 이 비트 순서를 다르게 적어 OUI 가 안 맞아 보이는 경우가 있다는 점도 알아 두자(데이터시트의 PHYIDR 설명과 레지스터 값을 같이 확인 권장). 실무에서 드라이버 매칭은 OUI 해석 없이 32비트 `phy_id & mask` 로 한다(Linux `PHY_ID_MATCH_EXACT/MODEL`).

### Part B — latch-low, 리셋, 해석, 탐색

**latch-low 를 그림으로.** 하네스의 실제 시나리오: 2000 ms 첫 폴링, 2003–2008 ms 끊김, 2010 · 2020 ms 폴링.

```svg
<svg viewBox="0 0 760 290" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">BMSR.2 latch-low — 폴링 사이의 5 ms 끊김 (solution.c Part B 시나리오)</text> <text x="10" y="58" fill="currentColor">실제 링크</text> <text x="10" y="108" fill="currentColor">BMSR.2 래치</text> <path d="M120 44 H180 V70 H280 V44 H740" stroke="currentColor" stroke-width="2" fill="none"/> <rect x="180" y="40" width="100" height="34" class="f-warn" opacity="0.5"/> <text x="230" y="36" fill="currentColor" text-anchor="middle">끊김 2003–2008</text> <path d="M120 120 H123 V94 H180 V120 H323 V94 H740" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="122" y="140" fill="currentColor" font-size="12">읽기→재장전</text> <text x="250" y="140" fill="currentColor" font-size="12" text-anchor="middle">읽을 때까지 0 유지</text> <line x1="120" y1="30" x2="120" y2="160" stroke="currentColor" stroke-dasharray="3 3"/> <line x1="320" y1="30" x2="320" y2="160" stroke="currentColor" stroke-dasharray="3 3"/> <line x1="520" y1="30" x2="520" y2="160" stroke="currentColor" stroke-dasharray="3 3"/> <text x="120" y="176" fill="currentColor" text-anchor="middle">2000 ms</text> <text x="320" y="176" fill="currentColor" text-anchor="middle">2010 ms</text> <text x="520" y="176" fill="currentColor" text-anchor="middle">2020 ms</text>
  <text x="120" y="194" fill="currentColor" text-anchor="middle" font-family="monospace">읽기 0, 1</text> <text x="320" y="194" fill="currentColor" text-anchor="middle" font-family="monospace">읽기 0, 1</text> <text x="520" y="194" fill="currentColor" text-anchor="middle" font-family="monospace">읽기 1, 1</text> <rect x="20" y="206" width="720" height="76" rx="6" class="f-mut" stroke="currentColor"/> <text x="30" y="226" fill="currentColor">2010 ms 폴링의 해석 — 둘째 값만(S2): up, 끊김 놓침 · 첫 값만(S1): down 이라 보고(지금은 up)</text>
  <text x="30" y="246" fill="currentColor">둘 다(S3, phy_read_link): up + dropped = 1 → flaps 1 · Linux genphy_update_link: up 이라 믿을 땐 한 번만 읽어 "down" 한 번 보고 후 회복</text> <text x="30" y="266" fill="currentColor">2000 ms 의 첫 읽기 0 은 끊김이 아니라 전원 인가 뒤 아직 한 번도 안 읽힌 래치 — was_up = false 라 dropped 로 세지 않는다</text>
</svg>
```

Linux `genphy_update_link` 의 주석이 이 선택을 정확히 말한다: "The link state is latched low so that momentary link drops can be detected. Do not double-read the status in polling mode to detect such short link drops except the link was already down." 즉 Linux 는 up 일 때 **한 번만** 읽어 짧은 끊김을 "down → (다음 폴링) up" 으로 상위에 알리고, down 일 때는 두 번 읽어 현재 값을 본다. 이 레슨의 `phy_read_link` 는 항상 두 번 읽되 첫 값을 버리지 않는 변형이다 — 상태 보고는 늘 현재 값, 끊김은 별도 카운터. 측정 (demo_flap_poll.c, 실제 출력 일부):

```text
 P(ms)   d(ms) |  S1 det  S2 det   L det  S3 det | S1 stale  L stale S3 stale | S2 theory
    10       1 |  100.0%   10.1%  100.0%  100.0% |   100.0%    89.9%     0.0% |     10.0%
    10       5 |  100.0%   48.8%  100.0%  100.0% |   100.0%    51.2%     0.0% |     50.0%
   100      20 |  100.0%   19.3%  100.0%  100.0% |   100.0%    80.7%     0.0% |     20.0%
   100     100 |  100.0%  100.0%  100.0%  100.0% |   100.0%     0.0%     0.0% |    100.0%
  1000     100 |  100.0%   10.3%  100.0%  100.0% |   100.0%    89.7%     0.0% |     10.0%
  1000     300 |  100.0%   29.9%  100.0%  100.0% |   100.0%    70.1%     0.0% |     30.0%
  1000    2000 |  100.0%  100.0%  100.0%  100.0% |   100.0%     0.0%     0.0% |    100.0%
```

```svg
<svg viewBox="0 0 760 334" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">짧은 끊김 감지율 vs 끊김 길이/폴링 주기 (demo_flap_poll.c 실측, 칸당 4000회)</text> <path d="M80 56 V266 H640" stroke="currentColor" fill="none"/> <line x1="76" y1="266.0" x2="640" y2="266.0" stroke="currentColor" opacity="0.15"/><text x="72" y="270.0" fill="currentColor" text-anchor="end">0%</text> <line x1="76" y1="213.5" x2="640" y2="213.5" stroke="currentColor" opacity="0.15"/><text x="72" y="217.5" fill="currentColor" text-anchor="end">25%</text> <line x1="76" y1="161.0" x2="640" y2="161.0" stroke="currentColor" opacity="0.15"/><text x="72" y="165.0" fill="currentColor" text-anchor="end">50%</text> <line x1="76" y1="108.5" x2="640" y2="108.5" stroke="currentColor" opacity="0.15"/><text x="72" y="112.5" fill="currentColor" text-anchor="end">75%</text> <line x1="76" y1="56.0" x2="640" y2="56.0" stroke="currentColor" opacity="0.15"/><text x="72" y="60.0" fill="currentColor" text-anchor="end">100%</text> <text x="80.0" y="282" fill="currentColor" text-anchor="middle">0</text> <text x="220.0" y="282" fill="currentColor" text-anchor="middle">0.5</text>
  <text x="360.0" y="282" fill="currentColor" text-anchor="middle">1</text> <text x="500.0" y="282" fill="currentColor" text-anchor="middle">1.5</text> <text x="640.0" y="282" fill="currentColor" text-anchor="middle">2</text> <text x="360.0" y="300" fill="currentColor" text-anchor="middle">d / P  (끊김 길이 ÷ 폴링 주기)</text> <path d="M80.0 266.0 L360.0 56.0 H640.0" stroke="currentColor" stroke-dasharray="5 4" fill="none"/> <text x="253.6" y="119.0" fill="currentColor">이론 min(1, d/P)</text> <line x1="80.0" y1="50.0" x2="640.0" y2="50.0" class="f-ok" stroke="currentColor" stroke-width="3" opacity="0.8"/> <text x="371.2" y="76.0" fill="currentColor">S1 · Linux 식 · S3 (래치를 본다) = 모든 칸 100%</text> <circle cx="108.0" cy="244.79" r="5" class="f-acc" stroke="currentColor"/>
  <circle cx="220.0" cy="163.51999999999998" r="5" class="f-acc" stroke="currentColor"/> <circle cx="640.0" cy="56.0" r="5" class="f-acc" stroke="currentColor"/> <circle cx="82.8" cy="263.9" r="5" class="f-warn" stroke="currentColor"/> <circle cx="94.0" cy="255.5" r="5" class="f-warn" stroke="currentColor"/> <circle cx="136.0" cy="225.47" r="5" class="f-warn" stroke="currentColor"/> <circle cx="360.0" cy="56.0" r="5" class="f-warn" stroke="currentColor"/> <circle cx="640.0" cy="56.0" r="5" class="f-warn" stroke="currentColor"/> <circle cx="80.28" cy="265.79" r="5" class="f-mut" stroke="currentColor"/> <circle cx="81.4" cy="264.95" r="5" class="f-mut" stroke="currentColor"/>
  <circle cx="85.6" cy="261.17" r="5" class="f-mut" stroke="currentColor"/> <circle cx="108.0" cy="244.37" r="5" class="f-mut" stroke="currentColor"/> <circle cx="164.0" cy="203.21" r="5" class="f-mut" stroke="currentColor"/> <circle cx="360.0" cy="56.0" r="5" class="f-mut" stroke="currentColor"/> <circle cx="640.0" cy="56.0" r="5" class="f-mut" stroke="currentColor"/> <circle cx="660" cy="140" r="5" class="f-acc" stroke="currentColor"/><text x="672" y="144" fill="currentColor">P = 10 ms</text> <circle cx="660" cy="162" r="5" class="f-warn" stroke="currentColor"/><text x="672" y="166" fill="currentColor">P = 100 ms</text> <circle cx="660" cy="184" r="5" class="f-mut" stroke="currentColor"/><text x="672" y="188" fill="currentColor">P = 1000 ms</text> <text x="650" y="120" fill="currentColor">S2 (둘째 값만)</text>
  <text x="10" y="322" fill="currentColor">점 = S2 실측. d/P &gt; 2 인 칸은 2 에 겹쳐 그림. 실측과 이론의 최대 차이 1.2%p (p = 0.5 에서 4000회 표본의 표준오차 ≈ 0.8%p).</text>
</svg>
```

읽는 법: S2(둘째 값만)의 감지율은 끊김이 폴링 순간을 덮을 확률 = min(1, d/P) 와 21칸 모두에서 1.2%p 안으로 맞는다. Linux 1 s 폴링(`PHY_STATE_TIME = HZ`)에서 300 ms 끊김을 S2 는 70% 놓친다. "stale" 은 링크가 실제로 up 인 순간에 down 을 보고한 비율 — S1(늘 한 번)은 회복 후 매번 한 폴링 늦고, Linux 식은 **폴링 주기보다 짧은 끊김에서만** 그렇다(끊김을 알리는 의도된 비용). S3 은 두 마리 토끼를 다 잡지만, 상위 계층이 `dropped` 를 따로 처리해야 한다(Part C 의 감시 로그). 더 잘하려면 PHY 의 인터럽트 핀(링크 변화 IRQ, 벤더 레지스터)을 쓴다 — 폴링 문제가 사라진다.

랜덤 검증 (Part B): 정렬된 끊김 1024개(1–30 ms)와 무작위 폴링 간격(1–20 ms)으로 7374번 폴링, 매 폴링의 `up`·`dropped` 를 기준값("직전 폴링 시각과 이번 폴링 시각 사이에 끊김 구간이 걸쳤나")과 대조 — `drops detected 978 (reference 978), mismatches 0`.

**리셋.** 정상 PHY(5 ms)는 5번째 폴링에 끝난다. 안 끝나는 PHY 는 정확히 488번 폴링 후 `PHY_ERESET_TIMEOUT` — 500 이 아닌 이유는 폴링마다 1 ms + MDIO 프레임 25.6 µs 이기 때문이다(500 / 1.0256 ≈ 487.5). 이런 "시간 = 대기 + 버스 시간" 계산을 놓치면 타임아웃 테스트를 잘못 쓴다. 리셋 중 MDIO 를 안 받는 PHY(20 ms)는 처음 19번 ENOACK 를 "아직"으로 넘기고 20번째에 OK — 첫 ENOACK 를 치명적으로 본 mutant 는 이 테스트에서 걸렸다. 응답이 0xFFFF 로 오는 경우(TA 무시 모드)도 bit15 = 1 이라 자연스럽게 "아직 리셋 중"으로 처리된다.

**해석(HCD).** `an_resolve` 를 기술 5비트 × 5비트 = 1024 조합 전부에서 Linux `mii_nway_result` 모양의 if-사슬 기준과 대조했다. 실제 bring-up 512개(우리 광고 16 × 파트너 페이지 32, T4 포함)도 비트 수준 MDIO 로 끝까지 돌려 기준과 같다:

```text
exhaustive bring-up: 16 masks x 32 partner pages: ok 350, no-common/timeout 130, EINVAL 32, bad 0
```

**Parallel detection.** 파트너가 오토네고를 끄고 100 Mb/s 로 강제돼 있으면 FLP 버스트가 오지 않는다. 우리 PHY 는 선 위의 100BASE-TX idle 신호로 **속도만** 알아낸다 — 이중 모드는 알 길이 없으니 **반이중**으로 정한다(802.3 Clause 28 의 parallel detection 규칙으로 알려진 내용, 스펙 확인 권장). 표시: ANER bit0(LP AN able) = 0. 파트너가 100 **전이중** 강제라면 이게 그 유명한 **duplex mismatch**: 링크는 올라오고 ping 도 되는데, 부하가 걸리면 우리 쪽은 충돌·late collision 으로, 상대는 FCS 에러·runt 로 패킷을 버린다. 그래서 상태 줄에 `[parallel detect: ...check its duplex]` 꼬리표를 단다. 연습 3 이 두 방향 모두를 재현한다. mock 은 "그 속도의 HD 를 광고할 때만" PD 하도록 정했다 — 처음엔 FD 만 광고해도 PD 되게 짰다가 결과(10HD)가 우리 광고에 없어 해석이 실패하는 경우를 테스트가 잡아서 고쳤다. 실제 칩이 어느 쪽인지는 데이터시트 확인 권장.

**장애 표** (solution.c 실제 출력. t = bring-up 시작부터의 시뮬레이션 시간, frames = MDIO 프레임 수):

```text
scenario                           return               status                            t (ms)  frames
normal                             PHY_OK               Link is Up - 100Mbps/Full - fl    1513.1     316
reset never ends                   PHY_ERESET_TIMEOUT   -                                  500.6     491
MDIO dead during reset             PHY_OK               Link is Up - 100Mbps/Full - fl    1528.5     331
dead in reset + stuck              PHY_ENODEV           -                                  500.6     491
cable unplugged                    PHY_EAN_TIMEOUT      -                                 3010.7     613
no common mode (we 10 only)        PHY_EAN_NOCOMMON     -                                 3010.7     613
partner 10HD only                  PHY_OK               Link is Up - 10Mbps/Half - flo    1513.1     316
partner forced 100 (PD)            PHY_OK               Link is Up - 100Mbps/Half - fl    1513.1     316
partner forced 10, we 100 only     PHY_EAN_TIMEOUT      -                                 3010.7     613
wrong PHYAD (5)                    PHY_ENODEV           -                                    0.0       1
PHY ignores TA (addr 2)            PHY_ENODEV           -                                    0.0       1
same + ignore_ta_mask              PHY_OK               Link is Up - 100Mbps/Full - fl    1513.1     316
MDIO stuck low                     PHY_EBUS             -                                    0.0       1
preamble 31 bits                   PHY_ENODEV           -                                    0.0       1
empty advertise mask               PHY_EINVAL           -                                    5.2       9
```

- **cable unplugged vs no common mode**: 둘 다 3 s 동안 링크가 없지만, 후자는 ANLPAR 에 상대 페이지(`0x4101` = 100FD + ACK + selector)가 들어와 있다. 현장 진단이 "케이블/상대 장비"에서 "설정(광고 마스크)"으로 바뀐다.
- **PHY ignores TA**: 실제로 TA 를 안 끌어내리는 PHY 가 있어서 Linux 에 `phy_ignore_ta_mask` 가 있다. 같은 PHY 를 마스크에 넣으면 정상 bring-up. 그 대가: 그 주소에선 "없음"도 0xFFFF 로 읽히므로, 탐색은 ID `0x?FFFFFFF` 를 "없음"으로 걸러야 한다(Linux `get_phy_c22_id` 의 "mostly Fs" 검사 — 하네스가 확인).
- **preamble 31 bits**: 첫 프레임에서 무시된다. 하지만 Part B 의 프리앰블 테스트가 보여 주듯, 직전 프레임이 "1 로 끝나면"(대답 없는 TA·데이터 = 풀업 1) PHY 가 그 꼬리의 1 까지 세어 31개로도 통과한다 — **간헐 버그**의 전형. 0x0000 쓰기 직후엔 실패, 그 다음엔 성공. 프리앰블을 줄이려면 BMSR.6(MFPS)을 확인하고 나서만.

### Part C — bring-up 순서와 운영 감시

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a3b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">phy_bringup() 흐름과 단계별 에러 출구</text> <rect x="20" y="40" width="160" height="56" rx="8" class="f-acc" stroke="currentColor"/> <text x="100" y="62" fill="currentColor" text-anchor="middle" font-weight="bold">① ID 읽기</text> <text x="100" y="80" fill="currentColor" text-anchor="middle">PHYID1/2</text> <rect x="205" y="40" width="160" height="56" rx="8" class="f-acc" stroke="currentColor"/> <text x="285" y="62" fill="currentColor" text-anchor="middle" font-weight="bold">② soft reset</text> <text x="285" y="80" fill="currentColor" text-anchor="middle">BMCR.15 → 폴링 1 ms</text>
  <rect x="390" y="40" width="160" height="56" rx="8" class="f-acc" stroke="currentColor"/> <text x="470" y="62" fill="currentColor" text-anchor="middle" font-weight="bold">③④ 광고 + 되읽기</text> <text x="470" y="80" fill="currentColor" text-anchor="middle">능력 ∩ mask → ANAR</text> <rect x="575" y="40" width="165" height="56" rx="8" class="f-acc" stroke="currentColor"/> <text x="657" y="62" fill="currentColor" text-anchor="middle" font-weight="bold">⑤ AN 재시작</text> <text x="657" y="80" fill="currentColor" text-anchor="middle">BMCR = AN_EN | AN_RST</text>
  <rect x="575" y="170" width="165" height="56" rx="8" class="f-ok" stroke="currentColor"/> <text x="657" y="192" fill="currentColor" text-anchor="middle" font-weight="bold">⑥ 링크 폴링</text> <text x="657" y="210" fill="currentColor" text-anchor="middle">BMSR ×2, 10 ms, 3 s</text> <rect x="390" y="170" width="160" height="56" rx="8" class="f-ok" stroke="currentColor"/> <text x="470" y="192" fill="currentColor" text-anchor="middle" font-weight="bold">⑦ 해석</text> <text x="470" y="210" fill="currentColor" text-anchor="middle">ANAR∩ANLPAR, ANER</text> <rect x="205" y="170" width="160" height="56" rx="8" class="f-ok" stroke="currentColor"/> <text x="285" y="192" fill="currentColor" text-anchor="middle" font-weight="bold">보고 + 감시</text>
  <text x="285" y="210" fill="currentColor" text-anchor="middle">상태 줄, 1 s monitor</text> <path d="M180 68 H203" stroke="currentColor" marker-end="url(#a3b)"/> <path d="M365 68 H388" stroke="currentColor" marker-end="url(#a3b)"/> <path d="M550 68 H573" stroke="currentColor" marker-end="url(#a3b)"/> <path d="M657 96 V168" stroke="currentColor" marker-end="url(#a3b)"/> <path d="M575 198 H552" stroke="currentColor" marker-end="url(#a3b)"/> <path d="M390 198 H367" stroke="currentColor" marker-end="url(#a3b)"/> <path d="M740 190 C760 190 760 214 742 214" stroke="currentColor" fill="none" marker-end="url(#a3b)"/>
  <text x="100" y="114" fill="currentColor" text-anchor="middle" font-family="monospace">ENODEV / EBUS</text> <text x="285" y="114" fill="currentColor" text-anchor="middle" font-family="monospace">ERESET_TIMEOUT</text> <text x="285" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">ENODEV / EBUS</text> <text x="470" y="114" fill="currentColor" text-anchor="middle" font-family="monospace">EVERIFY / EINVAL</text> <text x="657" y="244" fill="currentColor" text-anchor="middle" font-family="monospace">EAN_TIMEOUT</text> <text x="657" y="260" fill="currentColor" text-anchor="middle" font-family="monospace">EAN_NOCOMMON</text>
  <rect x="20" y="170" width="160" height="56" rx="8" class="f-warn" stroke="currentColor"/> <text x="100" y="192" fill="currentColor" text-anchor="middle" font-weight="bold">실패 = 로그 + 코드</text> <text x="100" y="210" fill="currentColor" text-anchor="middle">반쯤 설정된 채 "OK" 없음</text> <text x="20" y="288" fill="currentColor">PHY 는 리셋 직후 스스로 AN 을 시작한다 (기본 ANAR). 우리 광고를 쓴 뒤 재시작하지 않으면 기본 광고로 협상된 결과가 남는다 — 7절 실수 4.</text>
</svg>
```

실제 bring-up 로그 (phy1 = 정상, phy3 = 케이블 없음):

```text
[    0.051 ms] phy1 probe: id 0x00221560 OUI 00-10-A1 model 0x16 rev 0
[    5.205 ms] phy1 reset: BMCR.15 cleared after 5 polls
[    5.282 ms] phy1 adv: ANAR=0x0DE1 (BMSR=0x7809)
[    5.307 ms] phy1 aneg: restart
[ 1513.090 ms] phy1 link: up after 151 polls, LPA=0x45E1 ANER=0x0001 -> 100FD
[ 1513.141 ms] phy3 probe: id 0x001CC816 OUI 00-E0-4C model 0x01 rev 6
[ 1518.294 ms] phy3 reset: BMCR.15 cleared after 5 polls
[ 1518.371 ms] phy3 adv: ANAR=0x0DE1 (BMSR=0x7809)
[ 1518.397 ms] phy3 aneg: restart
[ 4523.782 ms] phy3 aneg: no link after 300 polls (LPA=0x0000)
phy1: Link is Up - 100Mbps/Full - flow control rx/tx
phy3: Link is Down
```

값 읽기: ANAR `0x0DE1` = PAUSE_ASYM(0x800) + PAUSE(0x400) + 100FD/100HD/10FD/10HD(0x1E0) + selector 1. LPA `0x45E1` = ACK(0x4000) + PAUSE + 네 모드 + selector. 공통 최고 = 100FD, 양쪽 PAUSE → `rx/tx` (802.3 Table 28B-3, Linux `mii_resolve_flowctrl_fdx` 와 같은 논리 — 연습 1 에서 16가지 전수). pause 는 **전이중일 때만** 의미가 있어 반이중 결과면 `off`. 상태 줄 형식은 Linux `phy_print_status` 의 `"Link is Up - %s/%s %s- flow control %s"` 를 따랐다.

**되읽기 검증.** ANAR 쓰기가 안 먹는 PHY(`anar_ro`)에서 10 Mb 만 광고하려 하면 `adv: wrote 0x0061 read 0x01E1` 로그와 `PHY_EVERIFY`. 되읽기가 없으면 "10 Mb 로 제한했는데 100 으로 붙는" 현장 미스터리가 된다. 능력표에 없는 모드(예: BMSR.14 = 0 인 PHY 의 100FD)는 애초에 광고하지 않는다(하네스: ANAR `0x0CE1`, 결과 100HD).

**운영 중 감시.** bring-up 뒤 1 s 마다 `monitor_step` 이 `phy_read_link` 를 부른다. +1.5 s 에 300 ms 끊김을 넣은 실제 출력:

```text
[ 3514.051 ms] phy1 Link is Down (latched: dropped since last poll)
[ 3514.051 ms] phy1 Link is Up (again)
```

폴링 순간엔 이미 회복됐지만 래치 덕분에 끊김이 기록된다. 실제 시스템에선 여기서 MAC 쪽도 다시 맞춰야 한다 — 재협상으로 속도/이중이 바뀌었을 수 있으니 MAC 의 speed/duplex 레지스터를 새 결과로 다시 쓰는 것(Linux 는 `adjust_link` 콜백 / phylink 의 `mac_link_up`). 링크가 다시 올라오면 ANLPAR 를 다시 읽고 해석해야 하는 이유다.

**Linux 대응표** (함수 이름은 커널 소스에서 확인한 것):

| 이 레슨 | Linux |
|---|---|
| `mdio_bit_out/in`, GPIO HAL | `struct mdiobb_ops { set_mdc, set_mdio_dir, set_mdio_data, get_mdio_data }`, `drivers/net/mdio/mdio-bitbang.c` |
| `mdio_read/write` | `mdiobus_read/write()` → 버스 드라이버의 `read/write` (MAC 의 MDIO 컨트롤러 또는 bit-bang) |
| `ignore_ta_mask` | `struct mii_bus` 의 `phy_ignore_ta_mask` |
| `phy_probe`, `phyid_absent` | `mdiobus_scan` → `get_phy_c22_id` (`(id & 0x1fffffff) == 0x1fffffff` → `-ENODEV`) |
| `phy_read_link` | `genphy_update_link` (up 이면 한 번, down 이면 두 번) |
| `an_resolve`, pause | `mii_nway_result`, `mii_resolve_flowctrl_fdx` / phylib 의 `phy_resolve_aneg_linkmode` |
| `phy_format_status` | `phy_print_status` → `"Link is Up - 100Mbps/Full - flow control rx/tx"` |
| `monitor_step` 1 s | phylib 상태 기계 `PHY_STATE_TIME = HZ` (인터럽트 쓰면 폴링 안 함) |

사용자 공간에선 `ethtool eth0`(속도·이중·AN·링크), `ethtool -d`, `mii-tool -v`(BMCR/BMSR 해석), `phytool read eth0/1/1` 같은 도구가 같은 레지스터를 보여 준다(도구별 출력 형식은 버전마다 다름).

**Clause 45 한 문단.** 10G 이상 PHY 와 일부 1G PHY 는 Clause 45 를 쓴다: ST = **00**, OP 00 = 주소 / 01 = 쓰기 / 11 = 읽기 / 10 = 읽기 후 주소 증가, 5비트 PHYAD(=PRTAD) 뒤에 레지스터 대신 5비트 **DEVAD**(MMD: PMA/PMD 1, PCS 3, AN 7 …), 레지스터 주소는 16비트라 **주소 프레임 + 데이터 프레임** 두 장이 필요하다. Clause 22 PHY 는 ST = 00 을 보고 프레임을 무시해야 하므로 한 버스에 섞어 쓸 수 있다(연습 2 에서 mock 이 실제로 무시하는지 확인). Clause 22 PHY 의 MMD 레지스터(예: EEE)는 레지스터 13/14 를 통한 간접 접근으로 읽는다(802.3 Annex 22D, 연습 2). 세부 필드는 스펙 확인 권장.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 110/110 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 깨끗함
```

하네스가 실제로 잡는지 확인하려고 solution.c 의 사본에 버그 7개를 넣어 돌렸다 (전부 FAIL, exit 1): TA 전에 선을 안 놓음(50/110), BMSR 한 번만 읽기(103/110), 우선순위 100HD 를 T4 앞에(108/110), OUI 비트 뒤집기 누락(105/110), TA 실패 시 flush 없이 return(104/110), 리셋 중 ENOACK 를 치명으로(108/110), ANAR 쓴 뒤 AN 재시작 누락(107/110).

## 7. 자주 하는 실수

**1. TA 에서 선을 안 놓음.** 쓰기 코드를 복사해 OP 만 바꾸면 REGAD 뒤에도 STA 가 MDIO 를 구동한다. 마지막 REGAD 비트가 1 이면 wired 모델에선 값이 맞아 보이지만 실제 회로에선 두 push-pull 드라이버의 싸움(전류, 뭉개진 엣지)이고, 0 이면 데이터가 전부 0 이다. 연습 5 실측: reg1 은 0x7829 로 "맞게" 읽히며 싸움 34 반주기, reg2 는 이 레슨의 TA 첫 비트 검사에 걸려 `MDIO_EBUS`. 고친 코드: `mdio_bits_out(b, reg, 5); gpio_mdio_dir(b, 0);`.

**2. 실패한 읽기에서 바로 return.** 

```c
int ta = mdio_bit_in(b);
if (ta) return MDIO_ENOACK;          /* 16 데이터 클럭을 안 보냄 */
```

남의 프레임을 흘려보내던 PHY 들이 다음 프레임 프리앰블의 앞 18비트를 먹어 버려 32주소 스캔이 깨진다(mutant 실측). 고친 코드: 16비트를 다 클럭한 뒤 판정.

**3. BMSR 을 한 번만, 또는 둘째 값만.** 한 번만 읽으면 bring-up 직후 첫 폴링이 "link down"(전원 뒤 아직 안 읽힌 래치)이고, 회복 직후에도 한 폴링 늦는다. 둘째 값만 쓰면 1 s 폴링에서 300 ms 끊김을 70% 놓친다(demo). 고친 코드: 두 번 읽고 둘 다 쓴다(`phy_read_link`), 또는 Linux 처럼 믿는 상태에 따라 고른다 — 어느 쪽이든 **의도를 주석으로**.

**4. ANAR 를 쓰고 AN 을 재시작하지 않음.** PHY 는 리셋 직후 기본 광고로 이미 AN 을 시작했다. 광고만 바꾸면 새 값은 **다음** 협상부터다. 10 Mb 로 제한하려 했는데 100FD 로 붙는다. mutant 실측으로 전수 bring-up 이 깨졌다. 고친 코드: `BMCR = ANENABLE | ANRESTART` (Linux `genphy_config_aneg` 도 광고가 바뀌면 재시작).

**5. 리셋 폴링에서 ENOACK 를 치명으로.** 리셋 중 MDIO 를 잠깐 안 받는 PHY 에서 첫 폴링에 "PHY 없음"으로 bring-up 이 죽는다. 반대로 영원히 기다리면 리셋이 안 끝나는 PHY 에서 부팅이 멈춘다. 고친 코드: 타임아웃 안에서 ENOACK = "아직", 끝까지 무응답이면 ENODEV, 응답하는데 bit15 = 1 이면 ERESET_TIMEOUT — 둘은 현장 조치가 다르다(전원·스트랩 vs PHY 클럭·리셋 핀).

**6. PHYID 를 0xFFFF 만으로 "없음" 판정.** TA 무시 모드에선 없는 주소도 TA 가 통과하고 0xFFFF 가 온다. MDIO 가 GND 에 붙으면 모든 주소가 `0x0000` 으로 "있다". 고친 코드: TA 첫 비트로 stuck-low 를 먼저 잡고, ID 는 `(id & 0x1FFFFFFF) == 0x1FFFFFFF` 또는 0 이면 없음.

**7. 링크만 보고 이중 모드를 안 봄.** parallel detection 결과(ANER.0 = 0)를 무시하면 duplex mismatch 가 "가끔 느린 네트워크"로 현장에 나간다. 고친 코드: PD 이면 로그·상태 줄에 표시하고, 가능하면 양쪽 다 오토네고 또는 양쪽 다 같은 강제 설정.

## 8. 면접 Q&A

<details>
<summary>Q1. MDIO 프레임을 그려 보세요. TA 는 왜 두 비트인가요?</summary>

PRE 32×1, ST 01, OP(10 읽기 / 01 쓰기), PHYAD 5, REGAD 5, TA 2, DATA 16, MSB 먼저, 모두 MDC 상승 에지 샘플 → 64 MDC, 2.5 MHz 면 25.6 µs. 읽기의 TA 에서 버스 소유권이 STA → PHY 로 넘어간다. 첫 비트는 둘 다 놓는 완충 구간(풀업으로 1), 둘째 비트는 PHY 가 0 으로 끌어내려 "응답 중"을 알린다. 한 비트로 끝내면 STA 가 놓는 시점과 PHY 가 잡는 시점이 겹쳐 버스 싸움이 난다. 쓰기는 방향이 안 바뀌니 STA 가 그냥 `10` 을 보낸다. 아무도 대답하지 않으면 둘째 TA 가 1 이고 데이터는 0xFFFF — "없는 PHY 는 0xFFFF".

</details>

<details>
<summary>Q2. BMSR 의 링크 비트는 왜 latch-low 이고, 드라이버는 몇 번 읽어야 하나요?</summary>

폴링은 띄엄띄엄이라 그 사이의 짧은 끊김을 놓치기 쉽다. latch-low 는 "끊긴 적 있음"을 읽을 때까지 0 으로 잡아 두는 하드웨어 기억이다. 그래서 첫 읽기 = 지난 읽기 이후 끊김 여부, 둘째 = 현재. 정답은 "몇 번"이 아니라 "무엇을 알고 싶나": 현재 상태만 원하면 두 번 읽고 둘째, 끊김을 놓치지 않으려면 첫 값을 봐야 한다. Linux `genphy_update_link` 는 up 일 땐 한 번만 읽어 끊김을 down 으로 한 번 보고하고, down 일 땐 두 번 읽는다(주석 원문 인용은 5절). 이 레슨은 두 번 읽고 첫 값을 `dropped` 카운터로 쓴다. demo: 1 s 폴링, 300 ms 끊김에서 "둘째만"은 70% 놓친다.

</details>

<details>
<summary>Q3. 링크가 안 올라옵니다. MDIO 로 무엇을 어떤 순서로 보나요?</summary>

① PHYID1/2 — 0xFFFF 면 주소·전원·리셋 핀·MDC 클럭 문제, 0x0000 이면 MDIO 단락 의심(TA 첫 비트로 확인). 주소를 모르면 0..31 스캔(주소 0 은 일부 PHY 에서 브로드캐스트로 동작 — 데이터시트 확인). ② BMCR — 리셋(bit15)이 안 풀렸나, power down(bit11)/isolate(bit10)가 켜져 있나(스트랩으로 isolate 로 부팅하는 PHY 가 있다). ③ BMSR 두 번 — AN 완료(bit5)? 링크(bit2)? ④ ANAR vs ANLPAR — 상대 페이지가 왔나(ANLPAR ≠ 0), 공통 모드가 있나, ACK 비트. ANLPAR = 0 이고 AN 미완료면 물리층(케이블, 마그네틱, 파트너 전원). ⑤ ANER — LP AN able = 0 이면 parallel detection(이중 확인). ⑥ 벤더 레지스터 — 케이블 진단(TDR), 수신 에러 카운터, 다운시프트. 이 레슨의 장애 표가 그 의사결정 트리다.

</details>

<details>
<summary>Q4. 파트너가 100 Mb/s 전이중으로 강제돼 있고 우리는 오토네고면?</summary>

FLP 가 없으니 우리 PHY 는 parallel detection 으로 속도만 알아내고 **반이중**으로 정한다(ANER.0 = 0, ANLPAR 엔 100HD 만 — 칩마다 다를 수 있음). 결과는 duplex mismatch: 우리는 반이중이라 송신 중 수신을 충돌로 보고 백오프·late collision, 상대는 전이중이라 우리가 중간에 끊은 프레임을 FCS 에러/runt 로 본다. 저부하에선 멀쩡해 보여 오래 숨어 있는다. 해결: 양쪽 다 오토네고(권장) 또는 양쪽 다 같은 강제 설정. 드라이버는 PD 를 감지해 경고 로그를 남긴다. 연습 3 이 반대 방향(우리 강제 FD, 상대 오토네고 → 상대가 HD)도 재현한다.

</details>

<details>
<summary>Q5. 최고 공통 모드(HCD) 우선순위는? T4 는 어디에?</summary>

10/100 범위에서 100BASE-TX FD > 100BASE-T4 > 100BASE-TX HD > 10BASE-T FD > 10BASE-T HD 로 구현했다 — Linux `mii_nway_result` 의 순서이고 Intel 82551 문서의 표와도 같다. 다만 `mii.h` 주석은 "802.3u 와 다른 점 하나는 100baseT4 를 100T-full 과 100T-half 사이에 둔 것"이라고 적고 있고, 1000BASE-T 나 100BASE-T2 까지 넣은 최신 표에선 T2 가 TX-FD 와 섞여 들어간다. T4·T2 PHY 는 사실상 없으니 실무 영향은 없지만, 면접에선 "Annex 28B 의 우선순위 표를 따른다, T4 위치는 판본·구현마다 차이가 있어 표를 확인한다"고 말하는 게 정확하다. 공통 모드가 없으면 AN 은 완료되지 않는다(링크 없음) — 0 을 돌려주고 NOCOMMON 으로 진단.

</details>

<details>
<summary>Q6. MDIO 컨트롤러가 있는데 왜 비트뱅을 하나요? 비트뱅의 비용은?</summary>

MAC 에 MDIO 컨트롤러가 없거나(일부 스위치 칩·FPGA), 레벨 시프터·핀 배치 때문에 다른 핀을 쓰거나, 컨트롤러가 비표준 PHY 를 지원하지 못할 때(TA 무시, Clause 45, 프리앰블 조절) 쓴다. 비용: 프레임 동안 CPU 가 잡혀 있다. 연습 4 계산: GPIO 로 약 500 kHz 면 프레임 128 µs, PHY 8개 × 링크 폴링(BMSR 2번) = 1 초마다 2.048 ms CPU 점유. 인터럽트를 막고 하면 지연이 생기고, 안 막으면 프레임 도중 선점돼 MDC 가 늘어진다(MDIO 는 정적 프로토콜이라 느려지는 건 괜찮다 — 최대 주파수만 있고 최소는 없는 것으로 알려져 있음, 스펙 확인 권장). Linux 는 `ndelay` 로 최소 반주기만 지킨다.

</details>

<details>
<summary>Q7. 리셋 후 몇 ms 기다려야 하나요? 그냥 delay 로 하면 안 되나요?</summary>

802.3 은 BMCR.15 로 시작한 리셋이 0.5 s 안에 끝나야 한다고 정해 두었고(22.2.4.1.1 로 기억 — 확인 권장), 끝나면 PHY 가 bit15 를 0 으로 되돌린다. 고정 delay 는 PHY 마다 다르고(µs 수십 ~ 수백 ms), 너무 짧으면 리셋 중 쓰기가 무시되며, 너무 길면 부팅 시간을 버린다. 폴링 + 상한 타임아웃이 정답이고, 하드웨어 리셋 핀(RESET_N) 해제 후의 대기와 첫 MDIO 접근까지의 대기는 별개다(데이터시트의 power-up / reset timing 표). 이 레슨의 폴링은 1 ms 간격 + 0.5 s 상한, 폴링마다 25.6 µs 버스 시간이 더해져 정확히 488번째에서 끝난다.

</details>

<details>
<summary>Q8. 같은 MDIO 버스에 PHY 여러 개, 그리고 스위치 칩이 있으면?</summary>

PHYAD 5비트라 32개까지. 주소는 스트랩 핀으로 정하니 보드 설계 때 겹치지 않게 하고, 드라이버는 Device Tree / 보드 설정의 주소를 믿되 bring-up 때 스캔으로 검증한다. 주의점: 주소 0 을 브로드캐스트로 받는 PHY 들이 있어서 주소 0 에 쓰면 여러 PHY 가 동시에 반응할 수 있다(데이터시트 확인). 관리형 스위치 칩은 여러 PHYAD 를 차지하거나 Clause 22 프레임으로 내부 레지스터에 간접 접근하는 방식을 쓰기도 한다. 버스 접근은 MAC 드라이버·PHY 드라이버·사용자 도구가 공유하므로 **트랜잭션 단위 락**이 필요하다(Linux `mii_bus` 의 mutex). 레지스터 13/14 간접 접근처럼 프레임 여러 개가 한 동작인 경우는 그 묶음 전체를 락으로 감싼다(COMM-M12 Q7 의 "읽기-수정-쓰기" 와 같은 이야기).

</details>

<details>
<summary>Q9. 링크는 올라왔는데 MAC 이 패킷을 못 받습니다. PHY 쪽에서 의심할 것은?</summary>

MDIO 는 정상이어도 데이터 경로(RMII/RGMII)가 틀릴 수 있다. ① 협상 결과(100/10, 이중)를 MAC 의 speed/duplex 설정에 반영했나 — 링크가 바뀔 때마다 다시. ② RMII 50 MHz 기준 클럭을 누가 내나(PHY 출력 vs 외부 오실레이터, 스트랩), RGMII 의 TX/RX 클럭 지연(PHY 내부 지연 vs PCB 지연 — Linux `phy-mode = "rgmii-id"` 류), ③ PHY 가 isolate 상태(BMCR.10)로 부팅했나, ④ 루프백(BMCR.14)으로 MAC↔PHY 구간만 따로 시험, ⑤ 벤더 레지스터의 수신 에러 카운터. 이 구분 — "관리 경로 OK, 데이터 경로 의심" — 을 말할 수 있으면 bring-up 경험이 드러난다. 데이터 경로 쪽 DMA 링은 COMM-A08.

</details>

## 9. 추가 연습

### 연습 1 — pause 해석 (802.3 Table 28B-3)

로컬 PAUSE/ASYM 과 파트너 PAUSE/ASYM 16가지 조합에 대해 "우리가 PAUSE 프레임을 받아 멈추나(rx), 보내나(tx)"를 구하는 함수를 짜고, 표(don't-care 포함 8행)와 전수 대조하라. 양쪽에서 본 결과가 tx/rx 를 맞바꾼 관계인지도 확인하라.

<details>
<summary>답 보기</summary>

```c
if (adv & lpa & ADVERTISE_PAUSE_CAP) return FC_RX | FC_TX;
if (adv & lpa & ADVERTISE_PAUSE_ASYM) {
    if (adv & ADVERTISE_PAUSE_CAP) return FC_RX;     /* 1101 */
    if (lpa & ADVERTISE_PAUSE_CAP) return FC_TX;     /* 0111 */
}
return FC_OFF;
```

실행 출력 (LP LA RP RA → 로컬):

```text
    0000=off 0001=off 0010=off 0011=off
    0100=off 0101=off 0110=off 0111=tx
    1000=off 1001=off 1010=rx/tx 1011=rx/tx
    1100=off 1101=rx 1110=rx/tx 1111=rx/tx
```

표 대조 16/16, 대칭성 16/16. 비대칭 pause 의 실용 예: 버퍼가 작은 MCU 는 "보내기만"(상대를 멈추게)을 원하고 받은 PAUSE 로 멈추는 기능은 없을 수 있다. 반이중에선 pause 가 의미 없다(그래서 bring-up 은 FD 일 때만 해석).

</details>

### 연습 2 — Clause 45 프레임은 Clause 22 PHY 를 건드리지 않는가

Clause 45 주소 프레임과 읽기 프레임을 비트열로 만들고(ST 00), 같은 버스의 C22 mock PHY 에 보내라. PHY 는 프레임을 무시해야 하고(레지스터 불변, TA 응답 없음, 버스 싸움 없음) 다음 C22 프레임은 정상이어야 한다. 또 C22 PHY 의 MMD 7 레지스터 60(EEE 광고로 알려진 7.60 — 확인 권장)을 레지스터 13/14 로 읽는 순서를 만들고 MDC 수를 비교하라.

<details>
<summary>답 보기</summary>

```text
ex2 C45 address frame: 11111111111111111111111111111111 00 00 00001 00111 10 0000000000111100
ex2 MMD 7.60 via C22 regs 13/14: 4 frames = 256 MDC edges (native C45: 2 frames = 128)
```

간접 접근 순서: `W13 = 0x0007`(function 00 = 주소, DEVAD 7) → `W14 = 0x003C` → `W13 = 0x4007`(function 01 = 데이터) → `R14`. C22 mock 은 ST 의 둘째 비트가 0 인 것을 보고 IDLE 로 돌아가 다시 프리앰블 32개를 기다리므로, C45 읽기의 TA·데이터 18비트는 아무도 구동하지 않는다(트레이스 18개 전부 1). 네 프레임은 한 동작이므로 Q8 처럼 락으로 묶어야 한다.

</details>

### 연습 3 — 강제 모드와 duplex mismatch

BMCR 의 AN enable 을 끄고 속도·이중을 강제하는 `phy_force()` 를 짜고(AN 완료 비트는 보지 않고 링크만 폴링), 아래 다섯 경우에서 양쪽이 각자 결정한 속도·이중과 mismatch 여부를 출력하라.

<details>
<summary>답 보기</summary>

```c
uint16_t bmcr = (speed == 100 ? BMCR_SPEED100 : 0) | (full ? BMCR_FULLDPLX : 0);   /* ANENABLE = 0 */
mdio_write(d->bus, d->addr, MII_BMCR, bmcr);
/* phy_read_link 폴링: s.up 이면 OK, 타임아웃이면 PHY_EAN_TIMEOUT */
```

```text
   local forced 100FD, partner AN    -> PHY_OK          local 100/Full partner 100/Half DUPLEX MISMATCH
   local forced 100FD, partner 100FD -> PHY_OK          local 100/Full partner 100/Full 
   local AN, partner forced 100FD    -> PHY_OK          local 100/Half partner 100/Full DUPLEX MISMATCH
   local AN, partner AN              -> PHY_OK          local 100/Full partner 100/Full 
   local forced 10HD, partner AN 100 -> PHY_EAN_TIMEOUT local   0/-    partner   0/-    
```

규칙은 하나: **오토네고하는 쪽은 상대가 강제면 반이중이 된다.** 그래서 "한쪽만 강제 FD" 는 어느 방향이든 mismatch. 마지막 줄: 상대가 100 만 광고하면 우리 강제 10 과는 PD 도 성립 안 함(mock 규칙) → 링크 없음.

</details>

### 연습 4 — MDIO 시간 예산

프레임 시간 = (프리앰블 + 32) / f_MDC 를 정수 ns 로 계산하는 함수를 짜고, 2.5 MHz 컨트롤러 / preamble suppression / GPIO 비트뱅 500 kHz 에서 PHY 1개·8개를 1 초마다 링크 폴링(BMSR 2번)할 때 버스 점유 시간을 표로 만들어라.

<details>
<summary>답 보기</summary>

```text
   MDC                         PHYs  frame(ns) busy us / 1 s poll
   2.5 MHz controller             1      25600             51.2
   2.5 MHz controller             8      25600            409.6
   2.5 MHz + preamble supp.       1      13200             26.4
   2.5 MHz + preamble supp.       8      13200            211.2
   bit-bang GPIO ~500 kHz         1     128000            256.0
   bit-bang GPIO ~500 kHz         8     128000           2048.0
```

컨트롤러면 0.04% 수준이라 문제없고, preamble suppression 은 시간을 거의 반으로 줄이지만(33 vs 64 비트) BMSR.6 을 확인한 PHY 에만 — 그리고 Part B 의 간헐 버그를 기억하자. 비트뱅은 그 시간 내내 CPU 를 쓰므로 8개면 1 초에 2 ms — RTOS 라면 낮은 우선순위 태스크로 돌리고, MDIO 를 ISR 에서 하지 않는다. 500 kHz 는 가정값이다.

</details>

### 연습 5 — 버스 싸움 찾기

REGAD 뒤에 `gpio_mdio_dir(b, 0)` 을 빠뜨린 `mdio_read_naive()` 로 레지스터 1 과 2 를 읽고, 값과 `contention` 증가량을 보라. 왜 하나는 "맞게" 읽히나?

<details>
<summary>답 보기</summary>

```text
ex5 naive read reg1: value 0x7829, contention half-periods 34
ex5 naive read reg2: rc -2, contention half-periods 34
```

REGAD = 00001 이면 STA 가 마지막으로 구동한 값이 1 이고, 이 모델의 선은 "풀업 + 모든 드라이버의 AND" 라 PHY 의 0 이 이긴다 → 값이 맞게 나온다(래치 때문에 0x7829). 하지만 TA 2 + DATA 16 = 18비트 × 2 반주기 = 36 에서 PHY 가 아직 안 잡은 첫 반주기와 이미 놓은 마지막 반주기를 뺀 34 반주기 동안 두 출력이 맞붙었다. 실제 push-pull 출력끼리면 값은 비결정적이고 전류가 흐른다. REGAD = 00010 이면 STA 가 0 을 계속 구동 → TA 첫 비트가 0 → 이 레슨의 검사가 `MDIO_EBUS`(-2)로 잡는다. "가끔만 틀린" 버그를 잡는 건 값 비교가 아니라 **누가 구동했나**를 보는 계측(LA 의 두 채널 + 직렬 저항 양단 전압, 또는 이 모델의 contention 카운터)이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 31/31 passed, exit 0
cc -std=c11 -Wall -Wextra -O2 demo_flap_poll.c -o demo_flap_poll && ./demo_flap_poll   # 5절의 감지율 표
```

## 10. 핵심 정리

- MDIO Clause 22 프레임 = PRE 32×1 · ST 01 · OP(10 읽기/01 쓰기) · PHYAD 5 · REGAD 5 · TA 2 · DATA 16 (MSB 먼저), 모두 MDC 상승 에지 샘플. 64 MDC = 2.5 MHz 에서 25.6 µs.
- 읽기 TA: STA 는 REGAD 직후 놓고, 첫 TA 는 Z(풀업 1), 둘째는 PHY 의 0 = 응답 표시. 대답 없음 = 0xFFFF. 실패해도 프레임 끝까지 클럭.
- BMCR.15 리셋·BMCR.9 AN 재시작은 자체 클리어 → 폴링 + 상한(리셋 0.5 s). 리셋 중 무응답은 "아직".
- BMSR.2 LINK 는 latch-low: 첫 읽기 = 그 사이 끊긴 적, 둘째 = 지금. 둘째만 쓰면 짧은 끊김을 d/P 비율로만 본다.
- ANAR 을 쓰면 반드시 AN 재시작 + 되읽기. 결과 = ANAR ∩ ANLPAR 의 최고 우선순위(100FD > T4 > 100HD > 10FD > 10HD), pause 는 FD 일 때 Table 28B-3.
- 상대가 강제면 parallel detection → 반이중(ANER.0 = 0) → 한쪽만 강제 FD 는 duplex mismatch.
- 진단 순서: ID(0xFFFF/0x0000) → BMCR(리셋·isolate·power down) → BMSR×2 → ANAR/ANLPAR → ANER → 벤더 레지스터. 관리 경로 OK ≠ 데이터 경로 OK.
- Linux: mdio-bitbang(`mdiobb_ops`), `get_phy_c22_id`, `genphy_update_link`, `mii_nway_result`, `phy_print_status`, 1 s 폴링.

## 참고자료

- IEEE Std 802.3, Clause 22 (MII 관리 인터페이스: 22.2.4 레지스터, 22.2.4.5 프레임 구조, 22.3 MDC/MDIO 타이밍), Clause 28 (오토네고, parallel detection), Annex 28B (우선순위·pause 해석 Table 28B-3), Annex 22D (레지스터 13/14 를 통한 MMD 접근), Clause 45 — 이 레슨의 절 번호·수치(리셋 0.5 s 등)는 기억에 의존한 부분이 있어 원문 확인 권장
- Linux `drivers/net/mdio/mdio-bitbang.c`: <https://github.com/torvalds/linux/blob/master/drivers/net/mdio/mdio-bitbang.c> — `MDIO_DELAY 250`("Minimum MDC period is 400 ns"), `MDIO_READ_DELAY 350`("The PHY may take up to 300 ns to produce data"), TA 검사와 `phy_ignore_ta_mask`, 실패 시 flush (원문 확인함)
- Linux `drivers/net/phy/phy_device.c`: <https://github.com/torvalds/linux/blob/master/drivers/net/phy/phy_device.c> — `genphy_update_link` 의 latch-low 주석, `get_phy_c22_id` 의 `0x1fffffff` 검사 (원문 확인함)
- Linux `drivers/net/phy/phy.c` — `phy_print_status` 형식 `"Link is Up - %s/%s %s- flow control %s"`, `PHY_STATE_TIME HZ` (원문 확인함)
- Linux `include/uapi/linux/mii.h` (BMCR/BMSR/ADVERTISE/LPA/EXPANSION 비트 이름), `include/linux/mii.h` (`mii_nway_result`, `mii_resolve_flowctrl_fdx`): <https://github.com/torvalds/linux/blob/master/include/linux/mii.h> (원문 확인함)
- Linux `include/linux/micrel_phy.h` (`PHY_ID_KSZ8081 0x00221560`), `drivers/net/phy/realtek/realtek_main.c` (`0x001cc816` RTL8201F, `0x001cc916` RTL8211F) (원문 확인함)
- IEEE OUI 등록 조회: 00-10-A1 Kendin Semiconductor (Micrel 이 인수) <https://maclookup.app/macaddress/0010a1>, 00-E0-4C Realtek <https://maclookup.app/macaddress/00e04c>
- HCD 우선순위 2차 자료: Wikipedia *Autonegotiation* (NWay) <https://wikipedia.com/wiki/NWay>, Intel 82551 문서의 우선순위 표 <https://babycare.manualsonline.com/manuals/mfg/intel/82551.html?p=139> — T4 위치는 출처마다 설명이 달라 Q5 에 적었다
- PHY 데이터시트 (레지스터·리셋 타이밍·스트랩·PD 동작 확인용): Microchip KSZ8081RNA/RND, Microchip LAN8742A, TI DP83848, Realtek RTL8201F
- 관련 레슨: COMM-B06 (I2C 비트 수준 — wired-AND·ACK 와 비교), COMM-M03 (GPIO 비트뱅 버스 복구), COMM-M12 (버스 추상화·에러 계층 — 여기의 PHY_E* 설계), COMM-A04 (Ethernet/IPv4/UDP 파서 — 링크 위의 프레임), COMM-A08 (MAC DMA 디스크립터 링), COMM-A09 (트랜잭션 녹화·재생), HWLAB-M05/M06 (LA 로 MDIO 캡처·디코드)
