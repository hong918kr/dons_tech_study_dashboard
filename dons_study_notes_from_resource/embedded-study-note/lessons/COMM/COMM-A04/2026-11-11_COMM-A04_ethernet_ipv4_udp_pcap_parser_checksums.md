---
id: COMM-A04
time: 120분
updated: 2026-11-11
---
# COMM-A04 Ethernet/IPv4/UDP 프레임 파서와 체크섬

## 학습 목표

- classic pcap 파일(24 B 전역 헤더 + 레코드 16 B 헤더, magic 4종: LE·BE × µs·ns)을 **호스트 엔디언과 무관하게** 바이트로 읽고, `caplen`(캡처된 바이트)과 `len`(원래 길이)을 구분해 다룬다.
- Ethernet II → 802.1Q / 802.1ad(QinQ) 태그 → IPv4(IHL·옵션·total length·조각) → UDP 를 **모든 길이를 caplen 과 대조하며** 해석하고, "캡처가 잘림" 과 "패킷이 틀림" 을 다른 오류로 보고한다. 범위 밖 읽기 0 을 ASan 으로 보인다.
- RFC 1071 인터넷 체크섬(1의 보수 합)과 UDP 의사 헤더(pseudo-header) 체크섬을 구현하고, 체크섬 0(= 안 함), 계산 결과 0 → 0xFFFF, 홀수 길이 덧대기, 송신 checksum offload 의 **부분합(partial)** 이 캡처에 찍히는 현상을 실제 캡처로 확인한다.
- 내 C 도구의 출력을 **독립 파이썬 구현**(`verify_pcap.py`)과 **tcpdump**(Wireshark 대신) 출력에 대조해 검증한다.
- RFC 1624 증분 갱신(`HC' = ~(~HC + ~m + m')`)으로 라우터의 TTL 감소와 NAT 재작성을 구현하고, RFC 1141 식이 왜 틀린지(−0)를 2^32 전수로 보인다.

## 1. 문제

면접관이 "pcap 하나 주고 UDP 체크섬이 맞는지 C 로 확인해 보세요" 를 묻는 이유: 네트워크가 붙는 펌웨어(Ethernet MAC 이 있는 STM32H7·i.MX RT, 카메라·레이더 데이터 링크, 시험 장비)는 결국 **L2~L4 헤더를 손으로 파싱**하고, 버그의 절반은 **길이 검사 하나 빠진 것**(원격에서 들어오는 OOB 읽기)이거나 **체크섬 경계 조건**(홀수 길이, 0 vs 0xFFFF, offload)이다.

이 레슨은 바이트 스트림 프레이밍(COMM-B10~B12, M10)과 디스크립터 파싱(COMM-M15)에서 이어진다. 차이는 **길이 필드가 층마다 있고 서로 어긋날 수 있다**는 것:

| 길이 | 어디서 | 이 레슨의 규칙 |
|---|---|---|
| `len` (wire) | pcap 레코드 | 원래 프레임 길이. 패킷이 "틀렸는지" 는 이것과 비교 |
| `caplen` | pcap 레코드 | 실제로 파일에 있는 바이트. **이 밖은 절대 읽지 않는다.** 모자라면 "잘림" (패킷 잘못 아님) |
| IHL × 4 | IPv4 | 헤더 길이 20..60. < 20 이면 `ip_ihl` |
| total length | IPv4 | IP 헤더 + payload. `len − L2` 보다 크면 `ip_totlen`, 작으면 나머지는 Ethernet 패딩(`pad`) |
| UDP length | UDP | 헤더 + 데이터. IP payload 보다 크면 `udp_len` (단 첫 조각은 예외), 작으면 뒤는 UDP 밖 |

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">프레임 구조 (pcap 에 든 바이트, FCS 없음) — 벡터 #2 (VLAN 100)</text>
  <g stroke="currentColor">
    <rect x="10" y="30" width="96" height="34" class="f-mut"/><rect x="106" y="30" width="96" height="34" class="f-mut"/>
    <rect x="202" y="30" width="44" height="34" class="f-warn"/><rect x="246" y="30" width="44" height="34" class="f-warn"/>
    <rect x="290" y="30" width="56" height="34" class="f-mut"/><rect x="346" y="30" width="150" height="34" class="f-acc"/>
    <rect x="496" y="30" width="70" height="34" class="f-ok"/><rect x="566" y="30" width="110" height="34" class="f-ok"/>
    <rect x="676" y="30" width="54" height="34" class="f-mut" stroke-dasharray="4,3"/>
  </g>
  <g fill="currentColor">
    <text x="16" y="45">dst MAC 6</text><text x="112" y="45">src MAC 6</text><text x="206" y="45">TPID</text><text x="250" y="45">TCI</text>
    <text x="294" y="45">Type 2</text><text x="352" y="45">IPv4 헤더 20 (+옵션)</text><text x="502" y="45">UDP 8</text><text x="572" y="45">데이터</text><text x="680" y="45">pad</text>
    <text x="206" y="59" font-family="monospace">8100</text><text x="250" y="59" font-family="monospace">A064</text><text x="294" y="59" font-family="monospace">0800</text>
    <text x="16" y="80">0</text><text x="112" y="80">6</text><text x="206" y="80">12</text><text x="294" y="80">16</text><text x="352" y="80">l3_off = 18</text><text x="502" y="80">l4_off = 38</text>
    <text x="206" y="96">TCI = PCP 5 | DEI 0 | VID 100 = 0xA064 (태그는 IP·UDP 체크섬 밖)</text>
  </g>
  <text x="10" y="124" fill="currentColor" font-weight="bold" font-size="13">IPv4 헤더 (32비트 한 줄, RFC 791)</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="132" width="360" height="26" class="f-acc"/><rect x="10" y="158" width="360" height="26" class="f-acc"/>
    <rect x="10" y="184" width="360" height="26" class="f-acc"/><rect x="10" y="210" width="360" height="26" class="f-acc"/>
    <rect x="10" y="236" width="360" height="26" class="f-acc"/><rect x="10" y="262" width="360" height="26" class="f-mut" stroke-dasharray="4,3"/>
    <line x1="55" y1="132" x2="55" y2="158"/><line x1="100" y1="132" x2="100" y2="158"/><line x1="190" y1="132" x2="190" y2="210"/>
    <line x1="224" y1="158" x2="224" y2="184"/><line x1="100" y1="184" x2="100" y2="210"/>
  </g>
  <g fill="currentColor">
    <text x="16" y="149">Ver</text><text x="60" y="149">IHL</text><text x="106" y="149">TOS</text><text x="196" y="149">Total Length</text>
    <text x="16" y="175">Identification</text><text x="194" y="175">DF MF</text><text x="230" y="175">Frag Offset (×8 B)</text>
    <text x="16" y="201">TTL</text><text x="106" y="201">Protocol 17</text><text x="196" y="201">Header Checksum</text>
    <text x="16" y="227">Source Address</text><text x="16" y="253">Destination Address</text><text x="16" y="279">Options (IHL &gt; 5, 4 B 단위)</text>
    <text x="10" y="306">헤더 체크섬 범위 = 헤더 IHL×4 바이트만 (데이터 아님)</text>
  </g>
  <text x="400" y="124" fill="currentColor" font-weight="bold" font-size="13">UDP 헤더 + 의사 헤더</text>
  <g stroke="currentColor">
    <rect x="400" y="132" width="165" height="26" class="f-ok"/><rect x="565" y="132" width="165" height="26" class="f-ok"/>
    <rect x="400" y="158" width="165" height="26" class="f-ok"/><rect x="565" y="158" width="165" height="26" class="f-ok"/>
    <rect x="400" y="210" width="330" height="22" class="f-mut" stroke-dasharray="4,3"/><rect x="400" y="232" width="330" height="22" class="f-mut" stroke-dasharray="4,3"/>
    <rect x="400" y="254" width="82" height="22" class="f-mut" stroke-dasharray="4,3"/><rect x="482" y="254" width="83" height="22" class="f-mut" stroke-dasharray="4,3"/>
    <rect x="565" y="254" width="165" height="22" class="f-warn" stroke-dasharray="4,3"/>
  </g>
  <g fill="currentColor">
    <text x="406" y="149">Source Port</text><text x="571" y="149">Destination Port</text><text x="406" y="175">Length (헤더+데이터)</text><text x="571" y="175">Checksum (0 = 없음)</text>
    <text x="400" y="203">의사 헤더 (전송 안 됨, 체크섬 계산에만):</text>
    <text x="406" y="226">IP Source</text><text x="406" y="248">IP Destination</text><text x="406" y="270">0</text><text x="488" y="270">Proto 17</text><text x="571" y="270">UDP Length ← IP 길이 아님</text>
    <text x="400" y="296">체크섬 = ~(의사헤더+UDP헤더+데이터 1의 보수 합)</text>
    <text x="400" y="312">홀수 길이: 끝에 0 바이트를 덧댄 셈 (읽지는 않음)</text>
    <text x="10" y="338">MAC·VLAN·패딩은 IP/UDP 체크섬 밖 → Ethernet 은 FCS(CRC-32) 로 지키지만 pcap 에는 보통 FCS 가 없다</text>
  </g>
</svg>
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">classic pcap 파일 = 전역 헤더 24 B + (레코드 헤더 16 B + caplen 바이트) × N</text>
  <g stroke="currentColor">
    <rect x="10" y="30" width="80" height="34" class="f-acc"/><rect x="90" y="30" width="45" height="34" class="f-mut"/><rect x="135" y="30" width="45" height="34" class="f-mut"/>
    <rect x="180" y="30" width="70" height="34" class="f-mut"/><rect x="250" y="30" width="70" height="34" class="f-mut"/>
    <rect x="320" y="30" width="80" height="34" class="f-mut"/><rect x="400" y="30" width="90" height="34" class="f-warn"/>
    <rect x="500" y="30" width="56" height="34" class="f-ok"/><rect x="556" y="30" width="56" height="34" class="f-ok"/>
    <rect x="612" y="30" width="56" height="34" class="f-ok"/><rect x="668" y="30" width="62" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="16" y="45">magic 4</text><text x="94" y="45">major</text><text x="139" y="45">minor</text><text x="184" y="45">thiszone</text><text x="254" y="45">sigfigs</text>
    <text x="324" y="45">snaplen 4</text><text x="404" y="45">LinkType 4</text><text x="504" y="45">ts_sec</text><text x="560" y="45">ts_frac</text><text x="616" y="45">caplen</text><text x="672" y="45">len</text>
    <text x="94" y="59">2</text><text x="139" y="59">4</text><text x="404" y="59">하위16 = 1</text><text x="504" y="59" font-size="11">레코드 16 B, 그 뒤 caplen B</text>
  </g>
  <text x="10" y="92" fill="currentColor">magic 을 리틀 엔디언 32비트로 읽은 값 → 파일 바이트 순서와 시각 단위가 동시에 정해진다:</text>
  <g stroke="currentColor"><rect x="10" y="100" width="720" height="92" class="f-mut"/></g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="118">파일 첫 4바이트   rd32le 값      뜻</text>
    <text x="20" y="136">d4 c3 b2 a1     0xA1B2C3D4     LE 파일, µs   ← lo.pcap (Linux aarch64 tcpdump)</text>
    <text x="20" y="152">4d 3c b2 a1     0xA1B23C4D     LE 파일, ns   ← lo_ns_snap60.pcap (--time-stamp-precision=nano)</text>
    <text x="20" y="168">a1 b2 c3 d4     0xD4C3B2A1     BE 파일, µs   (이후 모든 필드를 BE 로)</text>
    <text x="20" y="184">a1 b2 3c 4d     0x4D3CB2A1     BE 파일, ns</text>
  </g>
  <text x="10" y="214" fill="currentColor">LinkType 상위 비트는 FCS 길이 정보 등에 쓰일 수 있다(IETF pcap draft) → 하위 16비트만 비교하고 상위는 보고만.</text>
  <text x="10" y="232" fill="currentColor">caplen = min(len, snaplen). 검사: caplen ≤ len, caplen ≤ 262144, caplen ≤ 파일에 남은 바이트 (덧셈 넘침 없게 뺄셈으로).</text>
</svg>
```

### Part A — 체크섬, pcap 리더, Ethernet + VLAN

1. `csum_add(sum, p, n)` / `csum_fold(sum)`: RFC 1071. RFC 1071 §3 예제(`00 01 f2 03 f4 f5 f6 f7` → 합 `0xddf2`), 교과서 IPv4 헤더(`4500 0073 0000 4000 4011 .... c0a8 0001 c0a8 00c7` → `0xb861`), 홀수 길이(`12 34 AB` → `0xBD34`).
2. `pcap_open` / `pcap_next`: magic 4종, version 2, linktype 1, 레코드 경계. 같은 벡터를 4가지 변형(LE/BE × µs/ns)으로 써서 **해석 결과가 같음**을 단언.
3. `parse_l2`: Ethernet II, TPID 0x8100(C-tag)·0x88A8(S-tag) 태그 최대 2개. 3개째는 `vlan_depth` (정책: 0x9100 같은 비표준 TPID 는 이 도구는 태그로 보지 않음).
4. 실제 캡처 2개(Lima VM 의 `tcpdump -i lo`)를 해석해 tcpdump 가 보여 준 값(포트, IP id, UDP length, 올바른 체크섬)과 일치.

### Part B — 길이·체크섬 판정과 오류 주입

`parse_ipv4` 는 정해진 순서로 검사하고(5. 풀이 표), `parse_udp` 는 체크섬을 7가지로 판정한다: `ok / BAD / none(0) / partial(offload 부분합) / frag(첫 조각, 검증 불가) / trunc(캡처 잘림, 검증 불가) / na`. 주입과 단언:

| # | 주입 | 기대 |
|---|---|---|
| B1 | 시험 벡터 17개 (아래 표) | 벡터마다 오류 코드·판정·필드 |
| B2 | IP 헤더 160비트, UDP 104비트 단일 비트 뒤집기 전수 | 통과 0개. Ethernet 패딩 104비트는 104개 **전부 통과** (체크섬 밖) |
| B3 | src ↔ dst IP 교환 | IP·UDP 체크섬 **둘 다 ok** — 1의 보수 합은 순서를 못 본다 |
| B4 | 정상 벡터 14개 × 가능한 모든 caplen (2404가지), exact-size 힙 복사 | 결과는 `none` 또는 `trunc` 뿐, 검증 못 한 걸 `ok` 라 하지 않음, ASan 깨끗 |
| B5 | pcap 파일을 0..2866 모든 바이트에서 자름 | `E_SHORT / EOF / TRUNC_HDR / TRUNC_DATA` 가 정확히, 온전한 레코드 수도 정확히 |
| B6 | caplen > len, caplen = 0x7FFFFFFF, caplen > 파일 | `E_CAPLEN`, `E_CAPLEN`(넘침 없음), `E_TRUNC_DATA` |
| B7 | 고정 시드 변이 퍼징 20000 개 (헤더 쪽 1~8 바이트, 가끔 caplen·len 변경) | 크래시 0, 불변식(“ok 면 정말 맞다”, “헤더는 caplen 안”) 위반 0 |
| B8 | 랜덤 빌드→해석 왕복 3000 개 (태그 0~2, 옵션 0~40 B, payload 0~64 B, 10% 체크섬 0) | 필드 전부 일치 |

시험 벡터 (C 빌더와 `verify_pcap.py` 가 **바이트 단위로 같은** pcap 을 독립적으로 만든다 — `vectors.pcap`):

| # | 이름 | 특징 | 기대 판정 |
|---|---|---|---|
| 1 | basic_odd5 | payload "hello"(홀수), 60 B 패딩 | ip ok, udp ok, pad 13 |
| 2 | vlan100 | 802.1Q PCP 5 VID 100 | l3_off 18 |
| 3 | qinq | 0x88A8 VID 200 + 0x8100 PCP 3 DEI 1 VID 100 | l3_off 22 |
| 4 | ip_options | IHL 6, Router Alert `94 04 00 00` | l4_off 38 |
| 5 | frag1_mf | 1608 B 데이터그램의 첫 조각 (MF, IP payload 1480) | udp `frag`, ulen 1608 > 1480 허용 |
| 6 | frag2_last | 두 번째 조각 (offset 185 = 1480 B) | UDP 해석 안 함 |
| 7 | ucs_zero | UDP 체크섬 0 | `none` |
| 8 | ucs_ffff | 계산 결과가 0 이 되게 만든 payload | 저장 0xFFFF, `ok` |
| 9 / 10 | bad_ip_csum / bad_udp_csum | 체크섬 비트 하나 틀림 | `BAD want ..` |
| 11 / 12 | arp / icmp | EtherType 0x0806 / proto 1 | IP 아님 / UDP 아님 |
| 13 / 14 | ihl4 / tot_gt_frame | IHL 4 / total length 200 | `ip_ihl` / `ip_totlen` |
| 15 | snap_trunc | len 106, caplen 54 | udp `trunc` |
| 16 / 17 | ulen_gt_ip / empty_payload | UDP length 100 / 0 B 데이터 | `udp_len` / ok |

### Part C — 증분 체크섬, 라우터·NAT 재작성, bring-up 요약

- `csum_update16(hc, m, m2)`: RFC 1624 식 3. RFC 1624 §3 의 숫자 예제(다른 필드 합 0xCD7A, m 0x5555 → m' 0x3285)에서 0x0000 (RFC 1141 식은 0xFFFF). 벡터 #1 의 id 워드를 0..65535 전부로 바꿔 재계산과 비교.
- `ip_ttl_dec`: TTL 은 `(TTL << 8) | proto` 워드의 상위 바이트. 랜덤 2000 개 × TTL 끝까지(249,100 단계) 매번 재계산과 같음, TTL 1 은 RW_E_TTL 이고 프레임을 건드리지 않음.
- `nat_rewrite`: src IP(워드 2개)는 IP 체크섬 **과 UDP 체크섬(의사 헤더)** 둘 다, src port 는 UDP 체크섬만. 체크섬 0 은 0 유지, 결과 0 은 0xFFFF. 조각난 패킷은 거절(정책). 증분 갱신은 **틀린 체크섬을 틀린 채로** 둔다(재계산은 손상을 세탁한다)를 테스트로.
- 요약 카운터 한 줄: `summary frames=.. vlan=.. ipv4=.. ip_bad=.. frag=.. udp=.. ok=.. bad=.. partial=.. none=.. unverif=..` + 오류 종류별.

## 2. Problem (English)

> **pcapparse.** Write a C tool that reads a classic libpcap file (LINKTYPE_ETHERNET) and decodes every record down to
> UDP. No dynamic allocation in the parser; never read past `caplen`.
>
> ```c
> int pcap_open(pcap_reader *r, const uint8_t *buf, size_t n);   // 4 magics: a1b2c3d4 / a1b23c4d, either byte order
> int pcap_next(pcap_reader *r, pcap_rec *rec);                   // OK / EOF / TRUNC_HDR / TRUNC_DATA / E_CAPLEN
> int pkt_parse(const uint8_t *p, size_t caplen, size_t wirelen, pkt_info *pi);
> uint16_t csum_update16(uint16_t hc, uint16_t old16, uint16_t new16);   // RFC 1624
> int nat_rewrite(uint8_t *frame, size_t cap, pkt_info *pi, uint32_t new_src, uint16_t new_sport);
> ```
>
> Requirements: Ethernet II with up to two VLAN tags (0x8100, 0x88A8); IPv4 with IHL ≥ 5 and options, total length
> checked against the frame, header checksum verified (RFC 1071), fragments recognised (do not parse L4 of non-first
> fragments); UDP length rules and pseudo-header checksum (0 = not computed; a computed 0 is sent as 0xFFFF).
> Distinguish *capture truncated* (caplen < needed ≤ len) from *malformed packet* (needed > len).
>
> Example: the record `#1` of a Linux loopback capture decodes to
> `ip 127.0.0.1>127.0.0.1 ihl 5 tot 33 id 0x9375 ... udp 40001>50404 ulen 13 cs 0xfe20 partial want 0x5cd9 pay 5`
> — tcpdump prints `[bad udp cksum 0xfe20 -> 0x5cd9!]` for the same packet.
>
> Follow-ups: why is `0xfe20` not a corrupted checksum? Which errors can the Internet checksum never detect? Why does
> a router update the checksum incrementally, and why `~(~HC + ~m + m')` rather than `HC + m + ~m'`? What does a NIC
> with RX checksum offload hand you, and what about Ethernet padding?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열 개: A1 `csum_add`, A2 `csum_fold`, A3 `pcap_open`, A4 `pcap_next`, A5 `parse_l2`, B1 `parse_ipv4`, B2 `parse_udp`(조각·길이 가드 두 줄 뒤), C1 `csum_update16`, C2 `ip_ttl_dec`, C3 `nat_rewrite`. 하네스(출력 형식 `fmt_pkt`, 요약 카운터, 프레임 빌더, pcap writer, 시험 벡터, 실제 캡처 바이트 배열, 도구 모드)는 주어진다. 스텁은 "합 그대로 / 0xFFFF / E_MAGIC / EOF / TRUNC / 원래 체크섬 / E_TTL / E_CAP" 를 돌려주므로 모든 루프가 끝난다. 처음 실행(실제 출력, 앞부분):

```text
FAIL starter.c:614  csum_add(0, r1071, 8) == 0xddf2u && csum_fold(csum_add(0, r1071, 8)) == 0x220du
FAIL starter.c:617  csum_fold(csum_add(0, hdr, 20)) == 0xb861u
FAIL starter.c:619  csum_fold(csum_add(0, h2, 20)) == 0
…
9/71 passed
```

(exit 1.) 단계별 확인(실측): Part A 만 채우면 **33/71**, Part B 까지 **66/71**, Part C 까지 **71/71**.

빌더가 학생의 `csum_add/csum_fold` 를 쓰므로 A1·A2 가 틀리면 시험 벡터의 체크섬도 틀린다 — 그래서 벡터 바이트 자체는 `verify_pcap.py` 가 따로 만든 pcap 과 `cmp` 로 대조한다(5. 풀이).

## 4. 힌트

<details><summary>힌트 1 — 1의 보수 합은 "더하고, 넘친 걸 다시 아래에 더한다"</summary>

32비트 누산기에 16비트 big-endian 워드를 더하고, 끝에 `while (s >> 16) s = (s & 0xFFFF) + (s >> 16);`. 홀수 길이의 마지막 바이트는 **상위** 바이트(`p[n-1] << 8`) — 뒤에 0 을 덧댄 워드다. `p[n]` 을 읽어서 덧대면 그게 OOB 다. 넣을 값은 `~fold(sum)`, 검증은 체크섬 필드까지 포함해 `~fold(sum) == 0`.

</details>

<details><summary>힌트 2 — magic 은 리틀 엔디언으로 한 번 읽고 표로 판정</summary>

`rd32le(buf)` 가 `0xA1B2C3D4` 면 LE µs, `0xA1B23C4D` 면 LE ns, `0xD4C3B2A1` 이면 BE µs, `0x4D3CB2A1` 이면 BE ns. 그 뒤 모든 필드를 `swap ? rd32be : rd32le` 로. `*(uint32_t *)buf` 는 쓰지 말 것 — 정렬 위반 + 호스트 엔디언 의존.

</details>

<details><summary>힌트 3 — 레코드 검사는 뺄셈으로</summary>

`if (off + 16 + caplen > n)` 은 caplen 이 0xFFFFFFF0 근처면 32비트에서 넘친다(size_t 64비트라도 습관이 중요). `left = n - off; if (left < 16) …; if (caplen > left - 16) …`. caplen > len 은 쓰기 쪽 버그로 깨진 파일 → 거절.

</details>

<details><summary>힌트 4 — 검사 순서: wire 먼저, caplen 나중</summary>

각 헤더에서 (1) 원래 프레임(`wire`)이 그 헤더를 담을 만큼 긴가 → 아니면 **패킷이 틀림**(`eth_short`, `ip_short`, `ip_totlen`), (2) 캡처(`cap`)가 그만큼 있나 → 아니면 **캡처가 잘림**(`trunc`). 순서를 바꾸면 snaplen 으로 잘린 정상 패킷을 "틀린 패킷" 으로 보고한다. IPv4 는 version → IHL → total length → (caplen ≥ IHL×4) 순서.

</details>

<details><summary>힌트 5 — UDP 체크섬 판정 순서와 offload 부분합</summary>

0 → `none`. MF=1 → `frag`. `cap − l4_off < ulen` → `trunc`. 합 검증. 실패했는데 저장값이 **의사 헤더 합(보수 안 취함)** 과 같으면 `partial` — Linux 가 checksum offload 를 기대하고 부분합만 넣은 상태로 캡처된 것이다. `want` 는 체크섬 필드를 빼고 계산해 0 이면 0xFFFF.

</details>

<details><summary>힌트 6 — 증분 갱신</summary>

체크섬 HC = ~S, S = 다른 워드 합 + m. m 을 m' 으로 바꾸면 S' = S − m + m' = S + ~m + m' (1의 보수에서 빼기 = 보수 더하기). 그러니 HC' = ~(~HC + ~m + m'). 32비트 주소는 16비트 워드 두 개로 두 번. UDP 는 의사 헤더에 src IP 가 있으니 NAT 에서 UDP 체크섬도 IP 워드 두 개 + 포트 워드 하나.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 |
|---|---|
| 1. 입력 | pcap 헤더/레코드, magic 으로 엔디언·단위, caplen vs len |
| 2. 층별 해석 | L2(태그) → L3(IHL, total length, 조각) → L4(UDP length) — 각 층의 길이를 바깥 층 길이와 대조 |
| 3. 오류 분류 | 잘림(캡처 문제) / 틀림(패킷 문제) / 체크섬(값 문제) 은 서로 다른 카운터 |
| 4. 체크섬 | RFC 1071, 의사 헤더, 0 과 0xFFFF, 홀수, offload |
| 5. 검증 | 독립 구현(파이썬) + 기준 도구(tcpdump) 대조, 퍼징 + ASan |
| 6. 쓰기 경로 | 증분 갱신(RFC 1624), TTL·NAT, 하드웨어 offload |

### Part A — 인터넷 체크섬 손으로 한 번

교과서 IPv4 헤더 (체크섬 필드 0 으로):

| 워드 | 4500 | 0073 | 0000 | 4000 | 4011 | (0000) | c0a8 | 0001 | c0a8 | 00c7 |
|---|---|---|---|---|---|---|---|---|---|---|
| 누적 | 4500 | 4573 | 4573 | 8573 | c584 | c584 | 1862c | 1862d | 246d5 | 2479c |

접기: 0x479c + 0x2 = **0x479e**, 보수 → **0xb861**. 받는 쪽은 b861 까지 더해 0xffff → 보수 0 이면 정상. RFC 1071 §3 예제는 `0001 + f203 + f4f5 + f6f7 = 0x2ddf0 → 0xddf2`. 둘 다 `solution.c` 가 단언한다.

1의 보수 합의 성질 세 가지가 이 레슨 전체를 설명한다:

1. **순서 무관** (덧셈은 교환·결합): 워드 순서를 바꿔도 같다 → 검출 못 하는 오류(B3), 대신 어디서부터 더해도 되고 조각별로 더해 합칠 수 있다(의사 헤더 + UDP, 증분 갱신, 하드웨어 합).
2. **바이트 순서 독립** (RFC 1071): 바이트를 바꿔 넣은 워드로 더하면 결과도 바이트가 바뀐 것뿐 → 리틀 엔디언 MCU 는 32비트 LE 로 더하고 마지막에 한 번 교환(연습 2, demo 표 3: 바이트 쌍 루프보다 약 4.8배 빠름).
3. **0 이 두 개** (0x0000 = +0, 0xFFFF = −0): 0이 아닌 데이터의 합은 접으면 0xFFFF 가 될 수는 있어도 0x0000 은 못 된다. UDP 가 "0 = 체크섬 없음" 을 쓸 수 있는 이유이고, 계산 결과가 0(합 0xFFFF)이면 0xFFFF 로 보내는 이유다(벡터 #8).

**pcap.** 파일은 쓴 기계의 엔디언으로 쓰인다(Linux aarch64 → LE). 리더는 magic 으로 판단하고 **바이트 단위로** 읽는다 — 그러면 Mac(LE)·빅 엔디언 PowerPC 어디서나 같은 코드다. 실제 파일 `lo.pcap` 의 처음: `d4 c3 b2 a1 02 00 04 00 … 00 00 04 00 01 00 00 00` = LE µs, v2.4, snaplen 0x40000(262144), linktype 1. `lo_ns_snap60.pcap` 은 `4d 3c b2 a1 …`, snaplen 60.

**Ethernet + 태그.** EtherType 자리가 0x8100/0x88A8 이면 4바이트 태그(TCI = PCP 3 | DEI 1 | VID 12)를 벗기고 다시 EtherType 을 읽는다. 태그는 L2 라서 IP/UDP 체크섬과 무관하다 — 스위치가 태그를 붙이고 떼도 체크섬은 그대로(FCS 만 다시 계산). EtherType < 0x0600 은 802.3 길이 필드(LLC) — 이 도구는 "IPv4 아님" 으로 둔다.

### Part B — 길이 사다리와 검사 순서

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">벡터 #15 snap_trunc: len 106, caplen 54 — 어디까지 믿을 수 있나</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="680" height="24" class="f-mut"/>
    <rect x="20" y="72" width="346" height="24" class="f-ok"/>
    <rect x="20" y="104" width="90" height="24" class="f-mut"/><rect x="110" y="104" width="128" height="24" class="f-acc"/>
    <rect x="238" y="104" width="52" height="24" class="f-ok"/><rect x="290" y="104" width="410" height="24" class="f-warn"/>
  </g>
  <line x1="366" y1="30" x2="366" y2="140" stroke="currentColor" stroke-width="2" stroke-dasharray="5,3"/>
  <g fill="currentColor">
    <text x="26" y="57">len (wire) = 106 : 원래 프레임 — "패킷이 틀렸나" 의 기준</text>
    <text x="26" y="89">caplen = 54 : 파일에 있는 바이트</text>
    <text x="26" y="121">L2 14</text><text x="116" y="121">IPv4 20 (tot 92)</text><text x="242" y="121">UDP 8</text><text x="296" y="121">데이터 64 (ulen 72)</text>
    <text x="372" y="152">← 여기서 캡처가 끝남: 데이터 앞 20 B 만 있음</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="182">검사 (원래 길이 먼저 → 틀림, 그다음 caplen → 잘림)</text>
    <text x="30" y="202">① len − 14 ≥ 20 ?  106 − 14 = 92  ✓        ② caplen − 14 ≥ 20 ?  40 ✓       ③ version 4, IHL 5 ✓</text>
    <text x="30" y="222">④ 20 ≤ tot(92) ≤ len − 14 (92) ✓  → pad = 0          ⑤ caplen − 14 ≥ IHL×4 ✓ → 헤더 체크섬 검증 가능: ok</text>
    <text x="30" y="242">⑥ UDP: IP payload 72 ≥ 8 ✓, caplen − 34 = 20 ≥ 8 ✓, 8 ≤ ulen(72) ≤ 72 ✓</text>
    <text x="30" y="262">⑦ caplen − 34 = 20 &lt; ulen 72 → UDP 체크섬 검증 불가 = trunc  (BAD 라고 하면 거짓 경보)</text>
    <text x="30" y="284">같은 프레임이 len 도 54 라면 ④ 에서 tot 92 &gt; 40 → ip_totlen (패킷이 틀림)</text>
  </g>
</svg>
```

`parse_ipv4` 검사 순서와 그 이유:

| 순서 | 조건 | 결과 | 왜 이 순서 |
|---|---|---|---|
| 1 | `len − l3_off < 20` | `ip_short` | 원래 프레임이 IP 헤더도 못 담음 → 패킷 문제 |
| 2 | `cap − l3_off < 20` | `trunc` | 캡처 문제. 1 보다 뒤여야 정상 패킷을 틀렸다고 안 함 |
| 3 | version ≠ 4 | `ip_ver` | 나머지 필드 의미가 없음 |
| 4 | IHL < 5 | `ip_ihl` | tcpdump 는 `bad-hlen 16` |
| 5 | tot < IHL×4 또는 tot > len − l3_off | `ip_totlen` | 헤더가 IP 길이보다 길거나, IP 가 프레임보다 김 |
| 6 | `cap − l3_off < IHL×4` | `trunc` | 옵션이 캡처 밖이면 체크섬을 못 계산 → 읽으면 OOB (변이 M5 가 ASan 에 걸림) |
| — | (통과) | 체크섬 ok/BAD, `pad = len − l3_off − tot` | 틀린 체크섬이어도 계속 해석 = 표시 도구의 정책 |

`parse_udp`: 비첫 조각(offset ≠ 0)은 UDP 헤더가 없으니 해석하지 않는다. IP payload < 8 → `udp_short`. UDP length < 8 이거나, **조각이 아닌데** IP payload 보다 크면 `udp_len`. 첫 조각(MF=1)은 ulen(1608)이 이 조각의 payload(1480)보다 큰 게 정상이다(변이 M7: 이 예외를 빼면 벡터 #5 가 `udp_len` 으로 틀림). ulen 이 IP payload 보다 **작으면** 뒤 바이트는 UDP 밖 — 체크섬은 ulen 까지만, 의사 헤더에도 ulen (변이 M2: IP payload 길이를 넣으면 이 경우에만 BAD — 그래서 그 경우를 테스트에 넣었다).

### Part B — 실제 캡처: "bad udp cksum" 의 정체

VM(Ubuntu, Linux 7.0, tcpdump 4.99.6)에서 loopback 만 캡처했다. 보낸 것: `send.py` 가 127.0.0.1 → 50404 로 UDP 6개(`hello`, `ping`, 빈 payload, `SO_NO_CHECK` 로 체크섬 0, `IP_OPTIONS` 로 NOP NOP NOP EOL, 48 B).

```bash
sudo tcpdump -i lo -U -w /tmp/COMM-A04_work/lo.pcap udp port 50404 &
sudo tcpdump -i lo -U -s 60 --time-stamp-precision=nano -w /tmp/COMM-A04_work/lo_ns_snap60.pcap udp port 50404 &
python3 send.py ; sudo pkill -INT -x tcpdump      # 각각 "6 packets captured", 444 B / 414 B
```

Mac 에서 `tcpdump -r lo.pcap -nn -vv -e` (Apple tcpdump 4.99.1, sudo 불필요, 앞 두 패킷):

```text
19:52:53.588735 00:00:00:00:00:00 > 00:00:00:00:00:00, ethertype IPv4 (0x0800), length 47: (tos 0x0, ttl 64, id 37749, offset 0, flags [DF], proto UDP (17), length 33)
    127.0.0.1.40001 > 127.0.0.1.50404: [bad udp cksum 0xfe20 -> 0x5cd9!] UDP, length 5
19:52:53.639938 00:00:00:00:00:00 > 00:00:00:00:00:00, ethertype IPv4 (0x0800), length 46: (tos 0x0, ttl 64, id 37751, offset 0, flags [DF], proto UDP (17), length 32)
    127.0.0.1.40002 > 127.0.0.1.50404: [bad udp cksum 0xfe1f -> 0xc1db!] UDP, length 4
```

나머지(VM 의 같은 명령): #3 `0xfe1b -> 0xa0b3`, #4 `[no cksum]`, #5 `options (NOP,NOP,NOP,EOL)` + `0xfe20 -> 0x9bc3`, #6 `0xfe4b -> 0x760e`. 내 도구 `./solution dump lo.pcap`:

```text
#1 t=1794455573.588735 cap=47 len=47 eth 00:00:00:00:00:00>00:00:00:00:00:00 type 0x0800 ip 127.0.0.1>127.0.0.1 ihl 5 tot 33 id 0x9375 ttl 64 p 17 frag DF+0 hcs 0xa954 ok udp 40001>50404 ulen 13 cs 0xfe20 partial want 0x5cd9 pay 5
#2 t=1794455573.639938 cap=46 len=46 eth 00:00:00:00:00:00>00:00:00:00:00:00 type 0x0800 ip 127.0.0.1>127.0.0.1 ihl 5 tot 32 id 0x9377 ttl 64 p 17 frag DF+0 hcs 0xa953 ok udp 40002>50404 ulen 12 cs 0xfe1f partial want 0xc1db pay 4
#3 t=1794455573.690518 cap=42 len=42 eth 00:00:00:00:00:00>00:00:00:00:00:00 type 0x0800 ip 127.0.0.1>127.0.0.1 ihl 5 tot 28 id 0x937c ttl 64 p 17 frag DF+0 hcs 0xa952 ok udp 40003>50404 ulen 8 cs 0xfe1b partial want 0xa0b3 pay 0
#4 t=1794455573.745729 cap=48 len=48 eth 00:00:00:00:00:00>00:00:00:00:00:00 type 0x0800 ip 127.0.0.1>127.0.0.1 ihl 5 tot 34 id 0x939c ttl 64 p 17 frag DF+0 hcs 0xa92c ok udp 40004>50404 ulen 14 cs 0x0000 none pay 6
#5 t=1794455573.796096 cap=51 len=51 eth 00:00:00:00:00:00>00:00:00:00:00:00 type 0x0800 ip 127.0.0.1>127.0.0.1 ihl 6 tot 37 id 0x93cd ttl 64 p 17 frag DF+0 hcs 0xa5f7 ok udp 40005>50404 ulen 13 cs 0xfe20 partial want 0x9bc3 pay 5
#6 t=1794455573.846514 cap=90 len=90 eth 00:00:00:00:00:00>00:00:00:00:00:00 type 0x0800 ip 127.0.0.1>127.0.0.1 ihl 5 tot 76 id 0x93e1 ttl 64 p 17 frag DF+0 hcs 0xa8bd ok udp 40006>50404 ulen 56 cs 0xfe4b partial want 0x760e pay 48
summary frames=6 vlan=0 ipv4=6 other=0 ip_bad=0 frag=0 udp=6 ok=0 bad=0 partial=5 none=1 unverif=0
```

id 0x9375 = 37749, `want` 값 다섯 개가 tcpdump 의 `->` 값과 같다. **0xfe20 의 정체**: 의사 헤더만 더한 값이다 — `7f00 + 0001 + 7f00 + 0001 + 0011(proto) + 000d(ulen 13) = 0xfe20`. #5 도 ulen 13 이라 같은 0xfe20, #6 은 ulen 56(0x38) 이라 0xfe4b. Linux 는 lo 처럼 `tx-checksum-ip-generic: on` 인 장치로 보낼 때 체크섬 필드에 **의사 헤더 부분합(보수 안 취함)** 만 넣고 "나머지는 장치가 채움"(CHECKSUM_PARTIAL) 으로 넘기는데, 캡처 지점은 그 전이다. loopback 은 실제로 계산하지 않고 받는 쪽도 "검증됨" 으로 처리한다(ethtool 로 확인한 lo 의 기능: `rx-checksumming: on [fixed]`, `tx-checksum-ip-generic: on [fixed]`; 커널 내부 흐름은 `include/linux/skbuff.h` 주석 확인 권장). 그래서 **송신 호스트에서 뜬 캡처의 "bad udp cksum" 은 대개 offload 이지 링크 오류가 아니다** — 도구가 `partial` 로 따로 세면 bring-up 때 헛걸음을 안 한다. 실제 NIC 에서는 `ethtool -K eth0 tx off` 로 끄고 다시 캡처하면 확인된다(VM 에서는 lo 설정을 바꾸지 않았다).

`SO_NO_CHECK` 로 보낸 #4 는 진짜 0 — IPv4 UDP 에서만 허용되는 "체크섬 없음" 이다(IPv6 UDP 는 원칙적으로 필수 — RFC 8200 확인 권장). 그리고 loopback 프레임은 47 B 처럼 60 B 미만이다: 최소 길이 패딩은 실제 MAC 이 선로에 내보낼 때의 일이다.

snaplen 60 캡처의 #6 (`./solution dump lo_ns_snap60.pcap`):

```text
#6 t=1794455573.846514457 cap=60 len=90 eth 00:00:00:00:00:00>00:00:00:00:00:00 type 0x0800 ip 127.0.0.1>127.0.0.1 ihl 5 tot 76 id 0x93e1 ttl 64 p 17 frag DF+0 hcs 0xa8bd ok udp 40006>50404 ulen 56 cs 0xfe4b trunc pay 48
summary frames=6 vlan=0 ipv4=6 other=0 ip_bad=0 frag=0 udp=6 ok=0 bad=0 partial=4 none=1 unverif=1
```

tcpdump 도 이 패킷에는 체크섬 판정을 안 찍는다(`UDP, length 48`). 덤으로 발견한 도구 차이: Mac 의 Apple tcpdump 4.99.1 로 `--time-stamp-precision=nano` 를 주고 이 ns 파일을 읽으면 시각이 `19:52:53.000846514` 로 찍히고(옵션 없이 `.846514`), VM 의 4.99.6 은 `.846514457` 이다. 내 도구는 magic 대로 `.846514457`. 원인은 확인하지 않았다 — 기준 도구도 버전마다 다르니 **둘 이상과 대조**하는 이유다.

### Part B — 독립 구현 대조 (C ↔ 파이썬 ↔ tcpdump)

```bash
./solution vectors /tmp/vec_c.pcap ; python3 -I verify_pcap.py build /tmp/vec_py.pcap
cmp /tmp/vec_c.pcap /tmp/vec_py.pcap && echo "BYTES IDENTICAL"      # 실제: BYTES IDENTICAL, 2866 B, sha256 58fe93b7…
for f in vectors.pcap lo.pcap lo_ns_snap60.pcap; do
  diff <(./solution dump $f | grep -v '^summary') <(python3 -I verify_pcap.py dump $f) && echo "$f: C == python"
done                                                                 # 실제: 17 / 6 / 6 줄 모두 일치
```

파이썬 쪽은 `struct` 만 쓰고 체크섬·빌더·해석을 다시 짰다(같은 필드 값 표에서 출발, 코드 공유 없음). 같은 `vectors.pcap` 을 Mac tcpdump 로 읽은 결과와 비교하면 정책 차이가 보인다(tcpdump 출력 발췌, 실제):

| # | tcpdump | 내 도구 | 해석 |
|---|---|---|---|
| 3 | `802.1Q-QinQ (0x88a8) … vlan 200, p 0, … vlan 100, p 3, DEI` | `vlan 0x88a8:p0:d0:v200 vlan 0x8100:p3:d1:v100` | 같음 |
| 4 | `length 35, options (RA)` `[udp sum ok]` | `ihl 6 … ok … ok` | 같음 |
| 5 | `flags [+] … UDP, length 1600` (판정 없음) | `frag MF+0 … cs 0x3807 frag pay 1472` | 같음 (검증 불가) |
| 6 | `offset 1480 … ip-proto-17` | UDP 해석 안 함 | 같음 |
| 9 | `bad cksum 9a15 (->9b15)!` | `hcs 0x9a15 BAD want 0x9b15` | 같음 |
| 10 | `[bad udp cksum 0x0d9a -> 0x0d9b!]` | `cs 0x0d9a BAD want 0x0d9b` | 같음 |
| 13 | `bad-hlen 16` | `err ip_ihl` | 같음 |
| 14 | `truncated-ip - 154 bytes missing!` 후 UDP 계속 해석 `[udp sum ok]` | `err ip_totlen` (중단) | **정책 차이**: tcpdump 는 있는 만큼 보여 주고, 내 도구(펌웨어 RX 경로의 관점)는 버린다 |
| 16 | `[udp sum ok] UDP, bad length 92 > 5` | `cs 0x0d44 na err udp_len` | tcpdump 는 길이가 틀려도 체크섬을 다른 범위로 확인한 것으로 보임 (내부 규칙 미확인) |

### Part B — 1의 보수 합이 못 잡는 것 (측정)

`solution.c` B2/B3: 단일 비트 오류는 IP 160/160, UDP 104/104 전부 검출(±2^k 는 mod 65535 에서 0 이 아님), Ethernet 패딩 104비트는 **하나도** 못 잡는다(체크섬 밖 — FCS 의 몫). src↔dst 교환은 IP·UDP 둘 다 통과. `demo_csum.c` 표 1 (64 B 메시지, 줄마다 1e6 시도, 고정 시드, 실제 출력):

```text
error          inet undet      inet rate  crc32 undet     crc32 rate
1 bit                   0      0.000e+00            0      0.000e+00
2 bits              30088      3.009e-02            0      0.000e+00
3 bits               2815      2.815e-03            0      0.000e+00
4 bits               3079      3.079e-03            0      0.000e+00
burst 8                 0      0.000e+00            0      0.000e+00
burst 16                0      0.000e+00            0      0.000e+00
burst 17               29      2.900e-05            0      0.000e+00
burst 24               14      1.400e-05            0      0.000e+00
burst 32               13      1.300e-05            0      0.000e+00
burst 33               11      1.100e-05            0      0.000e+00
burst 64               16      1.600e-05            0      0.000e+00
word swap          968882      1.000e+00            0      0.000e+00
0000->FFFF        1000000      1.000e+00            0      0.000e+00
(2^-16 = 1.526e-05, 2^-32 = 2.328e-10)
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">미검출률 (로그 축, demo_csum.c 표 1 실측, 64 B, 1e6 시도/줄)</text>
  <g stroke="currentColor" stroke-width="0.6">
    <line x1="70" y1="50" x2="720" y2="50" stroke-dasharray="2,4"/><line x1="70" y1="83" x2="720" y2="83" stroke-dasharray="2,4"/>
    <line x1="70" y1="117" x2="720" y2="117" stroke-dasharray="2,4"/><line x1="70" y1="150" x2="720" y2="150" stroke-dasharray="2,4"/>
    <line x1="70" y1="183" x2="720" y2="183" stroke-dasharray="2,4"/><line x1="70" y1="217" x2="720" y2="217" stroke-dasharray="2,4"/>
    <line x1="70" y1="250" x2="720" y2="250"/>
  </g>
  <g fill="currentColor" font-size="11">
    <text x="30" y="54">1</text><text x="22" y="87">1e-1</text><text x="22" y="121">1e-2</text><text x="22" y="154">1e-3</text>
    <text x="22" y="187">1e-4</text><text x="22" y="221">1e-5</text><text x="22" y="254">1e-6</text>
  </g>
  <g stroke="currentColor">
    <rect x="90" y="101" width="50" height="149" class="f-warn"/>
    <rect x="170" y="135" width="50" height="115" class="f-warn"/>
    <rect x="250" y="134" width="50" height="116" class="f-warn"/>
    <rect x="330" y="201" width="50" height="49" class="f-warn"/>
    <rect x="410" y="213" width="50" height="37" class="f-warn"/>
    <rect x="490" y="50" width="50" height="200" class="f-warn"/>
    <rect x="570" y="50" width="50" height="200" class="f-warn"/>
  </g>
  <line x1="640" y1="200" x2="720" y2="200" stroke="currentColor" stroke-dasharray="6,3"/>
  <g fill="currentColor" font-size="11">
    <text x="92" y="96">3.0e-2</text><text x="172" y="130">2.8e-3</text><text x="252" y="129">3.1e-3</text><text x="332" y="196">2.9e-5</text>
    <text x="412" y="208">1.3e-5</text><text x="500" y="45">1.0</text><text x="580" y="45">1.0</text>
    <text x="92" y="268">2비트</text><text x="172" y="268">3비트</text><text x="252" y="268">4비트</text><text x="326" y="268">버스트17</text>
    <text x="406" y="268">버스트32</text><text x="486" y="268">워드 교환</text><text x="566" y="268">0000→FFFF</text>
    <text x="644" y="194">2^-16</text>
    <text x="70" y="290">인터넷 체크섬 (막대). CRC-32 는 모든 줄에서 0 / 1e6 (그림 아래 끝 밖). 1비트·버스트 ≤ 16 은 인터넷 체크섬도 0.</text>
  </g>
</svg>
```

읽는 법:

- **2비트 3.0%**: 두 비트가 같은 열(워드 안 같은 자리)이고 값이 반대(하나는 0→1, 하나는 1→0)면 +2^k − 2^k = 0. 64 B = 512비트에서 같은 열 쌍 16 × C(32,2) = 7936, 전체 C(512,2) = 130,816 → 6.07% × ½ = **3.03%** — 실측 3.009% 와 맞는다. 연습 1 이 IPv4 헤더에서 이 예측식 = 전수 탐색을 확인한다(랜덤 헤더 120개, 2.85%).
- **버스트 ≤ 15비트는 항상 검출**: 연속 15비트 이하는 많아야 15개 열을 건드리고, 두 워드에 걸쳐도 열이 겹치지 않아 "한 워드가 x → y (x ≠ y, {x,y} ≠ {0000, FFFF})" 와 같다 → 합이 변한다. 16비트 버스트는 0x0000 ↔ 0xFFFF 일 때만 놓친다(랜덤 데이터에서는 드물어 1e6 중 0). 17비트 이상은 ≈ 2^−16 (실측 1.1~2.9e−5).
- **구조적 오류는 100% 통과**: 워드 교환, 0x0000 → 0xFFFF(+0 → −0). 메모리 버스의 바이트 레인 꼬임이나 DMA 가 워드를 뒤바꾸는 버그가 여기에 걸린다. 그래서 Ethernet FCS(CRC-32)가 링크 오류를 막고, 인터넷 체크섬은 **라우터/호스트 메모리 안의 손상을 대충 잡는** 종단 검사다 (RFC 1071 이 속도를 위해 고른 타협; Stone & Partridge 2000 은 실제 트래픽에서 CRC 와 TCP 체크섬 사이 불일치가 생각보다 자주 난다고 보고 — 논문 확인 권장).

### Part C — 증분 갱신 (RFC 1624)

라우터는 패킷마다 TTL 을 1 줄이고 헤더 체크섬을 고쳐야 한다. 20 B 를 다시 더하는 대신, 바뀐 워드 하나로: HC = ~S 이므로 S = ~HC, S' = S − m + m' = ~HC + ~m + m', **HC' = ~(~HC + ~m + m')** (식 3). RFC 1141 의 식 2 `HC' = HC + m + ~m'` 은 대수적으로 같아 보이지만, 결과가 0x0000 이어야 할 때 0xFFFF(−0)를 낸다.

RFC 1624 §3 의 숫자 그대로(코드로 확인): 다른 필드 합 0xCD7A, m = 0x5555 → HC = ~(0xCD7A + 0x5555 → 0x22D0) = 0xDD2F. m' = 0x3285 로 바꾸면 재계산 ~(0xCD7A + 0x3285 = 0xFFFF) = **0x0000**, 식 3 = ~(0x22D0 + 0xAAAA + 0x3285) = **0x0000**, 식 2 = 0xDD2F + 0x5555 + 0xCD7A → **0xFFFF**.

`demo_csum.c` 표 2 (실제 출력):

```text
표 2: S=0xCD7A, all 2^32 (m, m') pairs: eqn3 mismatches 0; eqn2 mismatches 65536 (of which full=0x0000 & eqn2=0xFFFF: 65536; still verify: 65536)  [6.0 s]
     random S (1e8 triples, every 1024th forced to full=0): eqn3 mismatches 0, eqn2 mismatches 99185
```

즉 식 2 의 틀림은 정확히 "재계산이 0x0000 인 경우" (m 마다 m' 하나, 65536개)뿐이고, 그 결과 0xFFFF 도 **받는 쪽 검증은 통과**한다(+0 과 −0 은 같은 값). 그래서 IP 헤더에서는 해가 없어 보이지만, (1) 재계산한 장비와 바이트가 달라 "같은 패킷인가" 비교·테스트가 깨지고, (2) UDP 처럼 0x0000 과 0xFFFF 의 **의미가 다른** 필드에서는 식을 고르는 게 중요하다. `solution.c` C1 은 벡터 #1 의 id 워드 65536 값 전부에서 식 3 = 재계산(불일치 0), 식 2 불일치 1개를 확인한다.

NAT 에서 UDP 체크섬은 의사 헤더 때문에 IP 주소 변경도 반영해야 한다(워드 3개 갱신). 두 가지 UDP 규칙:

| 상황 | 처리 | 테스트 |
|---|---|---|
| 원래 체크섬 0 (없음) | 0 그대로 — 갱신하면 "있는 척하는 틀린 값" 이 된다 | 2000 개 중 186 개, 전부 0 유지 |
| 갱신 결과 0x0000 | 0xFFFF 로 | 포트를 골라 결과가 0 이 되게 만든 33 개, 전부 0xFFFF (변이 M4 가 여기서 걸림) |
| 원래 체크섬이 틀림 | 갱신 후에도 틀림 | B: payload 손상 + NAT → 여전히 BAD |

마지막 줄이 증분 갱신의 숨은 장점이다: 라우터가 체크섬을 **처음부터 다시 계산하면** 라우터에 들어오기 전에 생긴 손상이 "정상" 으로 세탁된다. 증분 갱신은 바뀐 필드만 반영하므로 종단 간(end-to-end) 검사가 살아 있다.

**하드웨어 offload** (펌웨어 쪽): 많은 Ethernet MAC 은 TX 때 IP 헤더·UDP/TCP 체크섬을 채워 주고(STM32 ETH 는 TX 디스크립터의 CIC 필드로 모드 선택 — RM 확인 권장), RX 때 "검증 결과 비트" 나 "L4 합" 을 디스크립터에 준다. 함정 두 개: (1) TX offload 를 켜면 소프트웨어가 체크섬 필드에 **무엇을 넣어 두어야 하는지**(0 인지, 의사 헤더 부분합인지)가 MAC 마다 다르다 — 위의 Linux 부분합이 그 예; (2) RX 에서 MAC 이 IP 길이를 모르고 **프레임 끝까지** 더하면 Ethernet 패딩이 섞인다 → 연습 4. 디스크립터·캐시 쪽은 COMM-A08.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 71/71 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 71/71, 오류 없음
./solution dump lo.pcap ; ./solution vectors /tmp/vec.pcap                          # 도구 모드
python3 -I verify_pcap.py dump lo.pcap                                               # 독립 구현 (같은 형식)
cc -std=c11 -Wall -Wextra -O2 demo_csum.c -o demo_csum && ./demo_csum              # 표 1~3, 약 12 초
```

`solution.c` 의 테스트 외 출력 중 위에 안 실은 줄 (실제):

```text
B  single-bit flips accepted: ip 0/160, udp 0/104, pad (not covered) 104/104
B  truncation sweep: 2404 cuts, violations 0
B  pcap file truncated at every offset 0..2866: mismatches 0
B  fuzz 20000: parsed 12302, rejected 7698, invariant violations 0
C  id word 0..65535: RFC1624 eqn3 mismatches 0, RFC1141 eqn2 mismatches 1 (still verifies: yes)
C  ttl_dec: 249100 steps, mismatches 0
C  nat_rewrite: 2000 frames, mismatches 0, ucs=0 kept 186, result 0xFFFF 33
C  vectors summary frames=17 vlan=2 ipv4=14 other=1 ip_bad=1 frag=2 udp=11 ok=7 bad=1 partial=0 none=1 unverif=2 ip_ihl=1 ip_totlen=1 udp_len=1
71/71 passed
```

하네스가 버그를 잡는지 확인한 변이(별도 복사본에서, 레슨 폴더에는 없음): M1 홀수 꼬리를 하위 바이트로 → 2 FAIL, M2 의사 헤더에 IP payload 길이 → 1 FAIL, M3 RFC 1141 식 → 3 FAIL, M4 NAT 의 0→0xFFFF 누락 → 1 FAIL, M5 "캡처 < IHL×4" 검사 누락 → 3 FAIL + ASan heap-buffer-overflow, M6 caplen > len 검사 누락 → 1 FAIL, M7 첫 조각 예외 누락 → 3 FAIL.

## 7. 자주 하는 실수

**1. 구조체를 패킷 위에 캐스트한다.** `struct iphdr *ip = (struct iphdr *)(p + 14);` — VLAN 이면 오프셋이 18/22 라 정렬이 깨지고(Cortex-M0 은 HardFault, M3+ 는 느린 비정렬 접근 또는 LDRD/LDM 에서 fault), 비트필드 순서는 컴파일러·엔디언 의존이다. 고친 코드: `rd16be(p + off)` 같은 바이트 읽기.

**2. caplen 대신 IP total length 로 읽는다.** `csum_add(0, ip, tot)` 을 caplen 확인 없이 — 캡처가 잘렸거나 공격자가 tot 을 키우면 버퍼 밖을 읽는다. 고친 코드: 바깥 길이(caplen, len)와 대조한 뒤에만, 그리고 IHL×4 도 caplen 과 대조(변이 M5 가 ASan 에 걸린 그 줄).

**3. 홀수 길이를 `p[n]` 으로 덧댄다.** 대개 다음 바이트가 0 이라 맞아 보이다가, 버퍼 끝이면 OOB, 뒤에 패딩 쓰레기가 있으면 체크섬이 틀린다. 고친 코드: `if (n & 1) sum += p[n-1] << 8;` — **상위** 바이트(하위로 넣으면 변이 M1, 2 FAIL).

**4. 의사 헤더에 IP 길이를 넣는다.** 보통은 UDP length 와 같아서 안 드러나다가, ulen < IP payload 인 패킷(트레일러가 붙은 경우)에서만 BAD. 고친 코드: UDP length 필드.

**5. 체크섬 0 을 "틀림" 으로 센다 / 계산 결과 0 을 0 으로 보낸다.** IPv4 UDP 의 0 은 "안 함". 보내는 쪽이 0 을 넣으면 받는 쪽은 검증을 건너뛴다 — 0 이 나오면 0xFFFF. NAT 증분 갱신 결과도 마찬가지.

**6. 첫 조각의 UDP length 를 IP payload 와 비교해 버린다.** 첫 조각에서 ulen > 이 조각의 payload 는 정상(벡터 #5: 1608 > 1480). 비첫 조각은 UDP 헤더가 없다 — offset ≠ 0 인데 포트를 읽으면 데이터를 포트로 읽는다.

**7. 송신 호스트 캡처의 "bad udp cksum" 을 링크 오류로 본다.** 체크섬이 의사 헤더 부분합과 같으면 offload(`partial`). 실제 판정은 받는 쪽 캡처나 offload 를 끈 캡처로.

**8. RFC 1141 식으로 증분 갱신 / 매번 재계산.** 식 2 는 −0 문제, 재계산은 손상 세탁. 고친 코드: `~(~HC + ~m + m')`.

## 8. 면접 Q&A

<details><summary>Q1. tcpdump 가 내 보드에서 보낸 UDP 를 "bad udp cksum" 이라고 합니다. 링크가 나쁜가요?</summary>

먼저 **어디서 캡처했는지**. 보내는 호스트에서 캡처했고 NIC 가 TX checksum offload 를 하면, 캡처 지점은 체크섬이 채워지기 전이라 필드에 의사 헤더 부분합이 들어 있다. 이 레슨의 Linux lo 캡처가 정확히 그렇다: `0xfe20` = `7f00+0001+7f00+0001+0011+000d`. 내 도구는 저장값이 의사 헤더 합(보수 전)과 같으면 `partial` 로 따로 센다. 확인: `ethtool -k` 로 offload 상태 보고, `-K tx off` 로 끄고 다시 캡처하거나, 받는 쪽(또는 미러 포트)에서 캡처. 받는 쪽에서도 BAD 면 그때 링크·메모리·DMA 를 의심 — 그런데 링크 오류는 대개 FCS 에서 먼저 잡혀 프레임이 버려지므로(`ethtool -S` 의 CRC 오류 카운터), FCS 는 멀쩡한데 UDP 만 BAD 면 **장비 내부**(DMA, 캐시, 스위치 메모리) 손상을 의심한다.

</details>

<details><summary>Q2. 인터넷 체크섬이 못 잡는 오류는? 그런데 왜 아직도 쓰나요?</summary>

1의 보수 덧셈은 교환법칙이 성립해 **16비트 워드 순서 바뀜**을 100% 놓친다(측정 1.000). 0x0000 ↔ 0xFFFF 도 100%. 같은 열의 반대 방향 두 비트는 항상 통과 — 64 B 에서 2비트 오류의 3.0%. 반면 단일 비트와 15비트 이하 버스트는 항상 잡고, 랜덤 오류는 ≈ 2^−16. 쓰는 이유: (1) 링크마다 CRC(FCS)가 이미 있고 이건 종단 간 보조 검사, (2) 싸다 — 덧셈뿐이라 32/64비트 워드로 더하고 바이트 순서를 무시할 수 있으며(연습 2: 약 4.8배), (3) **증분 갱신**이 된다(CRC 도 선형이라 가능은 하지만 훨씬 번거롭다). 안전 필요한 응용은 상위에 CRC-32C(iSCSI/SCTP) 같은 걸 얹는다.

</details>

<details><summary>Q3. caplen 과 len 이 다르면 어떻게 처리하죠? 둘을 왜 구분하나요?</summary>

caplen 은 파일에 있는 바이트, len 은 선로의 원래 길이. 구분 안 하면 두 종류의 거짓말을 한다: snaplen 으로 잘린 정상 패킷을 "total length 가 프레임보다 큼(malformed)" 으로 보고하거나, 반대로 진짜 malformed 를 "잘림" 이라고 넘어간다. 규칙: 각 헤더를 먼저 **len** 과 비교(모자라면 패킷 잘못), 그다음 **caplen** 과 비교(모자라면 캡처 잘림 → 검증 불가로 표시). 그리고 읽기는 항상 caplen 안에서만. 이 레슨에서 정상 벡터 14개를 가능한 모든 caplen(2404가지)으로 잘라 exact-size 힙 버퍼에서 ASan 으로 돌렸다 — 결과는 `none`/`trunc` 뿐이었고 검증 못 한 체크섬을 `ok` 라고 한 적이 없다.

</details>

<details><summary>Q4. 라우터가 TTL 을 줄일 때 체크섬은 어떻게 갱신하나요? 식을 유도해 보세요.</summary>

HC = ~S (S = 헤더 워드의 1의 보수 합). TTL 은 `TTL<<8 | proto` 워드의 상위 바이트라 그 워드 m → m'. S' = S − m + m', 1의 보수에서 −m = +~m 이므로 S' = ~HC + ~m + m', HC' = ~S'. TTL 만 1 줄면 대개 HC' = HC + 0x0100(캐리 접기) 이지만, 일반식을 쓰는 게 안전하다. RFC 1141 의 `HC + m + ~m'` 은 결과가 0x0000 이어야 할 때 0xFFFF 를 낸다 — 2^32 전수에서 정확히 65536 쌍. 받는 쪽 검증은 통과하지만 UDP 처럼 0 의 뜻이 다른 곳, 그리고 바이트 비교 테스트에서 문제. 그리고 증분 갱신은 들어오기 전 손상을 보존한다(재계산은 세탁한다).

</details>

<details><summary>Q5. NAT 가 IP 주소만 바꾸는데 왜 UDP 체크섬까지 고쳐야 하죠? 조각난 패킷은?</summary>

UDP/TCP 체크섬은 의사 헤더(src IP, dst IP, proto, 길이)를 포함한다 — IP 계층이 잘못 배달한 걸 L4 가 잡게 하려는 설계. 그래서 주소가 바뀌면 L4 체크섬도 워드 2개만큼 갱신, 포트도 바꾸면 하나 더. UDP 체크섬이 0 이면 그대로(갱신하면 틀린 값이 생김), 결과가 0 이면 0xFFFF. 조각: 포트는 첫 조각에만 있고, UDP 체크섬도 첫 조각에 있지만 데이터그램 전체를 덮는다 → 첫 조각의 체크섬 필드는 증분으로 고칠 수 있지만, 비첫 조각을 어떤 매핑으로 보낼지는 (src, dst, proto, IP id) 로 추적하거나 재조립해야 한다. 이 레슨의 `nat_rewrite` 는 조각을 거절(정책)했다.

</details>

<details><summary>Q6. NIC 의 RX checksum offload 를 쓰는 드라이버는 무엇을 조심해야 하나요?</summary>

MAC 이 주는 것이 (a) "L4 체크섬 ok" 비트인지, (b) "어떤 오프셋부터 프레임 끝까지의 1의 보수 합" 인지 구분. (a) 는 MAC 이 파싱할 수 있는 프레임(태그·옵션·조각 조합)에서만 믿을 수 있으니 "판정 불가" 비트도 확인. (b) 는 의사 헤더를 더해 검증하는데, MAC 은 IP 길이를 모르고 **Ethernet 패딩까지** 더한다 — 60 B 미만 프레임의 패딩이 0 이 아니면 틀린다. 연습 4: 쓰레기 패딩을 넣은 5000 프레임에서 패딩을 빼지 않은 검증은 3072/5000 만 소프트웨어와 일치, 패딩 합을 (홀수 위치면 바이트 교환해서) 빼면 5000/5000. 패딩이 0 이면 차이가 안 나서 실험실에서는 안 보이는 버그다. Linux 는 패딩을 잘라낼 때 그 합을 빼 주는 `pskb_trim_rcsum` 을 쓴다(커널 소스 확인 권장).

</details>

<details><summary>Q7. pcap 의 magic 이 4개인 이유와, 리더를 호스트 엔디언 독립으로 짜는 법은?</summary>

pcap 은 쓴 기계의 엔디언으로 필드를 쓴다 — magic 0xA1B2C3D4 를 그 기계의 정수로 쓰니, 다른 엔디언에서 읽으면 `d4 c3 b2 a1` 로 보인다. 나노초 판(0xA1B23C4D) 도 같은 방식이라 2 × 2 = 4. 리더는 첫 4바이트를 **고정된 순서(LE)로** 정수로 만들고 표에서 찾아 `swap` 과 `nano` 를 정한 뒤 모든 필드를 바이트로 조립한다. `fread(&hdr, sizeof hdr)` 후 `ntohl` 은 호스트가 BE 일 때 반대로 뒤집는 버그가 난다. 이 레슨은 같은 벡터를 4변형으로 써서 해석이 같음을 확인했다. pcapng(블록 구조, 인터페이스별 linktype)은 별개 형식 — 블록 길이를 앞뒤로 두 번 쓰는 것까지 같은 원칙(모든 길이 검증)으로 확장한다.

</details>

<details><summary>Q8. 파서가 안전하다는 걸 어떻게 보이나요? 리뷰어가 "OOB 없음" 을 믿게 하려면?</summary>

세 겹: (1) 구조 — 모든 헤더 접근 전에 "남은 바이트 ≥ 필요" 를 뺄셈으로 확인하고, 길이 필드는 바깥 길이와 대조한 뒤에만 쓴다(검사 순서 표). (2) 전수 — 가능한 모든 잘림 위치(패킷 2404, 파일 2867)를 exact-size 힙 복사본으로 ASan 아래 실행. (3) 퍼징 — 고정 시드 변이 20000 개 + 불변식("ok 면 독립 재계산과 같다", "헤더는 caplen 안"). 그리고 하네스가 실제로 잡는지 변이(검사 한 줄 빼기)로 확인 — M5 는 ASan heap-buffer-overflow 로 잡혔다. 다음 단계는 커버리지 기반 퍼저(libFuzzer/AFL, TEST-M02)로 같은 하네스를 돌리는 것.

</details>

## 9. 추가 연습

### 연습 1 — 2비트 미검출 예측식

IPv4 헤더 20 B(160비트)에서 동시에 뒤집어도 체크섬이 통과하는 비트 쌍의 수를 예측하는 식을 세우고, 모든 쌍(12,720)을 실제로 뒤집는 전수 탐색과 랜덤 헤더 120개에서 비교하라. 교과서 헤더(…b861)는 몇 쌍인가?

<details><summary>답</summary>

비트 k 를 0→1 로 바꾸면 합이 +2^k, 1→0 이면 −2^k. 두 개의 합이 0 (mod 65535) 이려면 같은 열 k 이고 방향이 반대 — 열마다 (그 열이 1 인 워드 수) × (0 인 워드 수). 다른 열끼리는 ±2^k ± 2^m 이 0 이 될 수 없다(크기가 65535 보다 작고 0 아님). 실측: 120 헤더 전부 예측 = 전수(불일치 0), 미검출 43,427 / 1,526,400 = 2.85%. 교과서 헤더 239 쌍. 체크섬 필드 자체의 비트도 포함된다는 점에 주의 — 체크섬 필드와 데이터 워드의 같은 열 반대 방향 쌍도 통과한다.

</details>

### 연습 2 — 32비트 LE 워드로 빠르게

리틀 엔디언 32비트 워드로 64비트 누산기에 더하고, 16비트로 접은 뒤 바이트를 한 번 교환해 `csum_add` 와 같은 값을 내는 `csum_fast32` 를 짜라. 길이 0..1500, 시작 오프셋 0..3 랜덤 20000 개로 비교.

<details><summary>답</summary>

32비트 LE 워드 `b0 b1 b2 b3` 의 값을 16비트로 접으면 `(b1 b0) + (b3 b2)` — BE 워드 `b0b1`, `b2b3` 를 바이트 교환한 것들의 합이고, 1의 보수 합은 바이트 교환과 교환 가능(RFC 1071 의 바이트 순서 독립성)이므로 마지막에 한 번 교환하면 된다. 꼬리 1~3 바이트는 0 으로 채운 워드로. 실측 불일치 0. 64비트 누산기는 1500 B(375 워드 × 2^32) 에서 넘칠 일이 없다. demo 표 3: 바이트 쌍 루프보다 4.81배 빠름(부하 있는 호스트, 비율만). Cortex-M 이면 `ADDS/ADCS` 캐리 사슬로 32비트씩 더하는 게 정석(어셈블리 최적화 예: Linux `csum_partial` 아키텍처별 구현).

</details>

### 연습 3 — 조각 재조립 후 UDP 검증

고정 2 KiB 버퍼와 8바이트 블록 비트맵으로 IPv4 조각을 모으는 `reasm_add` 를 짜라: (src, dst, id, proto) 가 다르면 거절, 마지막이 아닌 조각은 길이 8의 배수, 완전히 같은 중복은 무시, 부분 겹침은 거절(정책), 다 모이면 UDP 체크섬 검증. 벡터 #5+#6 (정순·역순), 중복, 8 B 겹침, 다른 id, 손상된 조각, 그리고 랜덤 3~5 조각 무작위 순서(+중복) 600 회.

<details><summary>답</summary>

실측: #5+#6 → total 1608, UDP 체크섬 0x3807 ok (본문에서 `frag` 로 보류했던 그 값). 역순도 완성, 중복 1회 무시, 겹침 → RA_E_OVERLAP, 다른 id → RA_E_MISMATCH, 조각 2 의 1비트 손상 → 완성되지만 체크섬 실패. 랜덤 600/600. 정책 메모: 겹치는 조각은 역사적으로 IDS 회피·Teardrop 같은 공격에 쓰였고, IPv6 는 RFC 5722 로 겹침을 금지했다(IPv4 스택의 처리는 구현마다 다름 — 확인 권장). 실제 스택은 재조립 타임아웃(Linux 기본 30 초로 알려짐 — 확인 권장)과 동시 재조립 수 상한(메모리)이 필요하다. 펌웨어 UDP 스택(lwIP 의 IP_REASSEMBLY 옵션)은 RAM 때문에 이걸 끄는 경우가 많다 → 송신 쪽이 MTU 안으로 보내야 한다.

</details>

### 연습 4 — 하드웨어 합으로 검증 + 패딩 함정

MAC 이 "L4 시작부터 프레임 끝까지" 의 1의 보수 합을 준다고 하자. 의사 헤더를 더해 UDP 를 검증하는 `hw_verify` 를 짜되, 60 B 로 패딩된 짧은 프레임에서 패딩의 합을 빼는 버전과 안 빼는 버전을 소프트웨어 검증과 비교하라(5000 프레임, 절반은 쓰레기 패딩, 20% 는 payload 손상).

<details><summary>답</summary>

패딩은 L4 시작 기준 오프셋 ulen 에서 시작한다. ulen 이 홀수면 패딩 첫 바이트가 워드의 하위 바이트 자리라 그 영역의 합은 바이트 교환된다 → `ps = swap16(ps)` 후 `s += ~ps`(1의 보수 뺄셈). 실측: 고친 버전 5000/5000 일치, 안 뺀 버전 3072/5000, **패딩이 0 인 절반만 보면 안 뺀 버전도 2500/2500** — 실험실 장비는 0 패딩이라 버그가 숨는다. 현장에서 패딩에 이전 프레임 잔재를 남기는 장비를 만나면 "가끔 UDP 체크섬 오류" 로 나타난다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 12/12 passed (약 0.1 초)
```

```text
ex1 120 random headers: undetected 2-bit pairs 43427 of 1526400 (2.85%), predictor mismatches 0
ex1 example header 4500 0073 ... (b861): undetected pairs 239
ex2 fast32 vs bytewise: 20000 random buffers (len 0..1500, offset 0..3), mismatches 0
ex3 vector #5+#6: total 1608, udp cksum 0x3807 ok
ex3 random 3..5 fragments in random order (+dups): 600/600 reassembled with udp ok
ex4 hw-sum verify vs software (5000 frames, half with garbage padding, 20% corrupted): software ok 4051, fixed agrees 5000/5000, naive agrees 3072/5000 (zero-pad only: 2500/2500)
12/12 passed
```

## 10. 핵심 정리

- pcap: magic 을 LE 로 읽어 4종(LE/BE × µs/ns) 판정 → 모든 필드를 바이트로. 레코드는 `caplen ≤ len`, `caplen ≤ 남은 바이트`(뺄셈으로).
- 길이 사다리: len ⊇ caplen, L2(+태그 4 B씩) → IHL×4 ≤ total length ≤ len − L2 → ulen ≤ IP payload(첫 조각 예외). **len 과 비교 = 패킷이 틀림, caplen 과 비교 = 캡처가 잘림.** 읽기는 caplen 안에서만.
- 인터넷 체크섬 = 16비트 BE 워드 1의 보수 합의 보수. 홀수는 상위 바이트로 덧댐(`p[n]` 금지). 검증 = 체크섬 포함 합의 보수가 0.
- UDP: 의사 헤더(src, dst, 0, 17, **UDP length**). 0 = 없음(IPv4), 계산 결과 0 → 0xFFFF. 첫 조각은 검증 불가, 비첫 조각은 UDP 헤더 없음.
- 송신 호스트 캡처의 "bad udp cksum" = 대개 offload 부분합(`0xfe20` = 의사 헤더 합). `partial` 로 따로 센다.
- 못 잡는 것: 워드 교환, 0000↔FFFF, 같은 열 반대 방향 2비트(64 B 에서 3%). 15비트 이하 버스트·단일 비트는 항상 잡음. 링크는 FCS(CRC-32)가 지킨다.
- 증분 갱신 `HC' = ~(~HC + ~m + m')` (RFC 1624). RFC 1141 식은 0x0000 대신 0xFFFF(2^32 중 65536 쌍). NAT 는 의사 헤더 때문에 UDP 체크섬도 고친다. 증분 갱신은 기존 손상을 보존한다.
- 검증 방법론: 독립 구현(파이썬)과 바이트 비교 + 기준 도구(tcpdump) 대조 + 전수 잘림 + 퍼징 + ASan + 변이.

## 참고자료

- RFC 1071, *Computing the Internet Checksum* — 1의 보수 합, 바이트 순서 독립성, §3 숫자 예제(00 01 f2 03 f4 f5 f6 f7). <https://www.rfc-editor.org/rfc/rfc1071>
- RFC 1141 (식 2), RFC 1624, *Computation of the Internet Checksum via Incremental Update* — 식 3 과 §3 의 0xCD7A/0x5555/0x3285 예제(이 레슨 코드로 같은 숫자 재현). <https://www.rfc-editor.org/rfc/rfc1624>
- RFC 791 (IPv4 헤더, 조각), RFC 768 (UDP, 의사 헤더, 체크섬 0 = 없음), RFC 2113 (Router Alert 옵션) — 필드 정의는 기억에 의존한 부분이 있어 원문 확인 권장. RFC 5722 (IPv6 겹치는 조각 금지), RFC 8200 (IPv6 UDP 체크섬).
- IETF draft-ietf-opsawg-pcap, *PCAP Capture File Format* — magic 0xA1B2C3D4 / 0xA1B23C4D, version 2.4, LinkType 하위 16비트 + FCS 정보 비트, caplen = min(len, snaplen). <https://www.ietf.org/archive/id/draft-ietf-opsawg-pcap-04.html> (WebFetch 로 확인)
- IEEE 802.1Q (C-tag 0x8100, TCI = PCP/DEI/VID), 802.1ad (S-tag 0x88A8) — 표준 원문 확인 권장.
- tcpdump / libpcap — 대조에 쓴 버전: Mac Apple tcpdump 4.99.1 (libpcap 1.10.1), Lima VM tcpdump 4.99.6 (libpcap 1.10.6). Wireshark(tshark)는 이 Mac 에 설치되어 있지 않아 tcpdump 로 대신했다. <https://www.tcpdump.org/>
- Linux 체크섬 offload: `include/linux/skbuff.h` 의 CHECKSUM_PARTIAL / CHECKSUM_COMPLETE 주석, `pskb_trim_rcsum` — 동작 설명은 기억 기반, 소스 확인 권장. `ethtool -k lo` 출력은 VM 에서 직접 확인.
- J. Stone, C. Partridge, "When the CRC and TCP Checksum Disagree", SIGCOMM 2000 — 실제 트래픽의 체크섬 실패 원인(확인 권장).
- STMicroelectronics RM0433 (STM32H7) ETH 장 — TX 디스크립터 체크섬 삽입 제어(CIC), RX 체크섬 결과 비트 (RM 확인 권장).
- 이 레슨의 파일: `vectors.pcap`(시험 벡터 17개, C·파이썬 바이트 동일), `lo.pcap`·`lo_ns_snap60.pcap`(VM loopback 실캡처), `verify_pcap.py`(독립 구현), `demo_csum.c`(표 1~3).
- 관련 레슨: COMM-B08 (CRC vs 체크섬 검출력), COMM-B10·B12·M10 (프레이밍·견고한 파서), COMM-M15 (디스크립터 파싱의 길이 검증), COMM-A08 (DMA 디스크립터 링·캐시), COMM-A10 (LA 디코더), ALGO-B14 (체크섬), ARCH-M12 (DMA 캐시 일관성), TEST-M02 (파서 퍼징, 다음 단계), NET-B01 (pcap 링크 하네스, 다음 단계).
