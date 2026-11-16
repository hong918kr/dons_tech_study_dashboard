---
id: PCIE-B10
time: 60분
updated: 2026-11-15
---
# PCIE-B10 TLP 헤더 인코딩/디코딩: MRd, MWr, CplD

## 학습 목표

- MRd·MWr(3DW/4DW)·Cpl·CplD 헤더의 모든 필드를 **바이트 번호와 비트 번호로** 말하고, 손으로 12/16바이트를 만들 수 있다 (byte0: MRd32 `00`, MRd64 `20`, MWr32 `40`, MWr64 `60`, Cpl `0A`, CplD `4A`).
- 바이트 주소·길이에서 Length(DW), First/Last DW BE(byte enable)를 계산하고, BE 규칙(1 DW 면 Last BE = 0, 연속성, QW 정렬 2 DW 예외)과 "4GB 이상일 때만 4DW" 규칙으로 잘못된 헤더를 걸러 낸다.
- 긴 DMA 전송을 **4KB 경계 + MPS/MRRS** 로 자르고, "정렬 안 된 시작 주소에서 256B = 65 DW" 함정을 피한다.
- MRd 하나가 RCB(Read Completion Boundary) 64/128B 로 여러 CplD 로 돌아올 때 각 CplD 의 **Byte Count / Lower Address** 를 계산하고, 요청자 쪽에서 그것만으로 데이터 위치와 "마지막 완료"를 알아낸다.
- Linux AER 로그의 `TLP Header` DW 를 읽어 "누가 누구에게 무엇을" 보냈는지 한 줄로 해석한다.

## 1. 문제

실리콘 검증·SSD 펌웨어 면접에서 프로토콜 애널라이저 캡처나 AER 로그의 `TLP Header: 4a000003 00000008 01000c06` 을 주고 "이게 뭐죠?" 를 묻는 이유: **링크 위에서 실제로 오가는 것은 TLP 뿐이고, 디버그는 결국 헤더 비트를 읽는 일**이기 때문이다.

PCIe 의 Transaction Layer 는 모든 요청·응답을 TLP(Transaction Layer Packet) 하나로 만든다 (계층 구조는 PCIE-B01). TLP = 헤더 3 또는 4 DW(DWord = 4바이트) + 데이터(있으면) + ECRC(TD=1 이면). Don 이 다뤘던 SSD 컨트롤러로 보면:

- 호스트 드라이버가 doorbell 을 쓴다 (`writel`) → 호스트 Root Complex 가 **MWr**(Memory Write) TLP 를 SSD 의 BAR0 주소로 보낸다. 응답 없음(posted).
- SSD 의 DMA 엔진이 SQ 엔트리·PRP 페이지를 읽는다 → SSD 가 **MRd**(Memory Read) TLP 를 호스트 메모리 주소로 보낸다. 호스트가 **CplD**(Completion with Data) 로 데이터를 돌려준다. 읽을 페이지가 4KB 경계를 넘거나 MRRS 보다 크면 MRd 를 여러 개로 나눠야 하고, 호스트는 한 MRd 를 RCB 단위의 여러 CplD 로 나눠서 돌려줄 수 있다.
- SSD 가 읽은 NAND 데이터를 호스트에 쓴다 → **MWr**, 페이로드는 MPS(Max Payload Size) 이하.
- config read 를 없는 함수에 보내면 Root Port 가 **Cpl**(데이터 없는 완료) 상태 UR 로 답하고, CPU 는 `0xFFFFFFFF` 를 본다 (PCIE-B08).

헤더는 스펙 그림 그대로 **바이트 0 이 먼저**, DW 안에서는 바이트 0 이 최상위(big-endian 표기)다. 페이로드 데이터는 반대로 바이트 주소 순서다(낮은 주소 바이트가 먼저). 아래 그림의 비트 위치가 이 레슨 코드의 기준이다 (출처: PCI Express Base Spec 2.2 절 "Transaction Layer Protocol — Packet Definition". T9/T8/LN 같은 확장 비트는 이 레슨에서 0 으로 두고 해석하지 않는다).

```svg
<svg viewBox="0 0 740 372" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-weight="bold" font-size="13">TLP 헤더 비트 지도 (한 칸 = 1비트, 바이트 0 이 왼쪽 = 먼저 전송)</text>
  <g fill="currentColor" text-anchor="middle">
    <text x="160" y="36">byte 0</text><text x="320" y="36">byte 1</text><text x="480" y="36">byte 2</text><text x="640" y="36">byte 3</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="84" y="52">7</text><text x="230" y="52">0</text><text x="244" y="52">7</text><text x="390" y="52">0</text>
    <text x="404" y="52">7</text><text x="550" y="52">0</text><text x="564" y="52">7</text><text x="710" y="52">0</text>
  </g>
  <text x="8" y="78" fill="currentColor">DW0</text>
  <g stroke="currentColor">
    <rect x="80" y="58" width="60" height="30" class="f-acc"/><rect x="140" y="58" width="100" height="30" class="f-acc"/>
    <rect x="240" y="58" width="20" height="30" class="f-mut"/><rect x="260" y="58" width="60" height="30" class="f-ok"/>
    <rect x="320" y="58" width="20" height="30" class="f-mut"/><rect x="340" y="58" width="20" height="30" class="f-ok"/>
    <rect x="360" y="58" width="20" height="30" class="f-mut"/><rect x="380" y="58" width="20" height="30" class="f-ok"/>
    <rect x="400" y="58" width="20" height="30" class="f-ok"/><rect x="420" y="58" width="20" height="30" class="f-warn"/>
    <rect x="440" y="58" width="40" height="30" class="f-ok"/><rect x="480" y="58" width="40" height="30" class="f-ok"/>
    <rect x="520" y="58" width="40" height="30" class="f-acc"/><rect x="560" y="58" width="160" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="110" y="78">Fmt</text><text x="190" y="78">Type</text><text x="250" y="78">T9</text><text x="290" y="78">TC</text>
    <text x="330" y="78">T8</text><text x="350" y="78">A2</text><text x="370" y="78">LN</text><text x="390" y="78">TH</text>
    <text x="410" y="78">TD</text><text x="430" y="78">EP</text><text x="460" y="78">Attr</text><text x="500" y="78">AT</text>
    <text x="540" y="78">Len</text><text x="640" y="78">Length[7:0] DW</text>
  </g>
  <text x="80" y="104" fill="currentColor">A2 = Attr[2] IDO · Attr[1:0] = RO(bit5), NS(bit4) · Len = Length[9:8] · T9/T8/LN: 이 레슨은 0</text>
  <text x="10" y="128" fill="currentColor" font-weight="bold">요청 MRd / MWr</text>
  <text x="8" y="154" fill="currentColor">DW1</text>
  <g stroke="currentColor">
    <rect x="80" y="134" width="320" height="30" class="f-ok"/><rect x="400" y="134" width="160" height="30" class="f-ok"/>
    <rect x="560" y="134" width="80" height="30" class="f-warn"/><rect x="640" y="134" width="80" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="240" y="154">Requester ID (bus | dev | fn)</text><text x="480" y="154">Tag</text>
    <text x="600" y="154">Last BE</text><text x="680" y="154">First BE</text>
  </g>
  <text x="8" y="188" fill="currentColor">DW2</text>
  <g stroke="currentColor">
    <rect x="80" y="168" width="600" height="30" class="f-acc"/><rect x="680" y="168" width="40" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle"><text x="380" y="188">Address[31:2]  (3DW = 주소 &lt; 4GB)</text><text x="700" y="188">PH</text></g>
  <text x="8" y="222" fill="currentColor">4DW</text>
  <g stroke="currentColor">
    <rect x="80" y="202" width="640" height="30" class="f-acc"/>
    <rect x="80" y="236" width="600" height="30" class="f-acc"/><rect x="680" y="236" width="40" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="400" y="222">DW2 = Address[63:32]  (4DW 는 주소 ≥ 4GB 일 때만)</text>
    <text x="380" y="256">DW3 = Address[31:2]</text><text x="700" y="256">PH</text>
  </g>
  <text x="10" y="292" fill="currentColor" font-weight="bold">완료 Cpl / CplD (항상 3DW)</text>
  <text x="8" y="318" fill="currentColor">DW1</text>
  <g stroke="currentColor">
    <rect x="80" y="298" width="320" height="30" class="f-ok"/><rect x="400" y="298" width="60" height="30" class="f-warn"/>
    <rect x="460" y="298" width="20" height="30" class="f-mut"/><rect x="480" y="298" width="240" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="240" y="318">Completer ID</text><text x="430" y="318">Status</text><text x="470" y="318">B</text>
    <text x="600" y="318">Byte Count[11:0] (0 = 4096)</text>
  </g>
  <text x="8" y="352" fill="currentColor">DW2</text>
  <g stroke="currentColor">
    <rect x="80" y="332" width="320" height="30" class="f-ok"/><rect x="400" y="332" width="160" height="30" class="f-ok"/>
    <rect x="560" y="332" width="20" height="30" class="f-mut"/><rect x="580" y="332" width="140" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="240" y="352">Requester ID</text><text x="480" y="352">Tag</text><text x="570" y="352">R</text>
    <text x="650" y="352">Lower Addr[6:0]</text>
  </g>
</svg>
```

필드 요약 (코드의 `tlp_hdr` 와 같은 이름):

| 필드 | 위치 | 값 |
|---|---|---|
| Fmt[2:0] | byte0 [7:5] | 000 3DW 데이터 없음, 001 4DW 데이터 없음, 010 3DW 데이터, 011 4DW 데이터, 100 TLP Prefix |
| Type[4:0] | byte0 [4:0] | 0_0000 메모리 요청(MRd/MWr), 0_1010 완료(Cpl/CplD) |
| TC | byte1 [6:4] | Traffic Class 0..7 (VC 매핑, 보통 0) |
| Attr | byte2 [5:4] + byte1 [2] | Attr[0] NS(No Snoop), Attr[1] RO(Relaxed Ordering), Attr[2] IDO(ID-based Ordering) — 순서 규칙은 PCIE-B11 |
| TH / TD / EP | byte1[0] / byte2[7] / byte2[6] | TLP Processing Hint 있음 / ECRC(TLP Digest) 붙음 / Poisoned(데이터 오염) |
| AT | byte2 [3:2] | Address Type (ATS 용, 보통 00 — PCIE-M15) |
| Length | byte2[1:0] + byte3 | DW 단위 데이터 길이(MRd 는 요청 길이). 1..1024, **필드 0 = 1024** |
| Requester ID / Tag | DW1 (요청), DW2 (완료) | bus[15:8] dev[7:3] fn[2:0] / 요청자가 붙인 번호 — 완료를 짝짓는 키 |
| Last / First DW BE | byte7 [7:4] / [3:0] | 비트 i = 그 DW 의 바이트 i (주소 + i) 가 유효 |
| Status | byte6 [7:5] (완료) | 000 SC, 001 UR, 010 CRS(최신 스펙 이름 RRS), 100 CA, 나머지 reserved |
| BCM | byte6 [4] | Byte Count Modified — PCI-X 브리지만 씀, PCIe 완료자는 0 |
| Byte Count | byte6[3:0] + byte7 | 이 완료를 **포함해** 남은 바이트 수. **0 = 4096** |
| Lower Address | byte11 [6:0] | 이 완료가 돌려주는 첫 유효 바이트 주소의 하위 7비트 |

### Part A — 헤더 코덱

`tlp_encode(h, out, cap)` 와 `tlp_decode(in, n, h)` 를 짠다. 요청은 주소가 4GB 이상(`addr > 0xFFFFFFFF`)이면 4DW, 아니면 3DW 로 **인코더가 정한다**. Length 1024 와 Byte Count 4096 은 필드 0 으로 쓰고 읽는다. 오류면 `out`/`*h` 를 건드리지 않는다. 손 계산 벡터 V1~V5 와 고정 시드 무작위 헤더 4000개 왕복으로 검증한다.

### Part B — 규칙 검사, BE 계산, 요청 분할

- `tlp_check`: 필드 범위, 주소 DW 정렬, 4KB 경계 넘음, BE 규칙을 검사. 인코더·디코더가 같이 쓴다. 디코더는 추가로 잘림, 지원 안 하는 Fmt/Type(Prefix, Cfg, 4DW Cpl), "4DW 인데 상위 32비트가 0" 을 거부한다.
- `tlp_be(addr, len, &span)`: 바이트 범위 → DW 정렬 주소, Length, First/Last BE. `len == 0` 은 zero-length read(Length 1, BE 0000/0000).
- `tlp_split(addr, len, max_bytes, out, cap)`: 긴 전송을 "4KB 경계를 넘지 않고, Length×4 ≤ max_bytes(MWr 은 MPS, MRd 는 MRRS)" 인 조각으로, 앞에서부터 가장 크게 자른다.

### Part C — Completion 계산과 실전 해석

- `cpl_byte_count`, `cpl_lower_addr`: MRd 의 Length/BE → 첫 CplD 의 Byte Count / Lower Address.
- `cpl_split(rd, rcb, mps, out, cap)`: MRd 하나에 대한 CplD 목록 (각 CplD ≤ MPS, 마지막이 아니면 RCB 경계에서 끝).
- `tlp_describe`: AER 로그 DW → `CplD SC 00:00.0->01:00.0 tag=0x0c len=3 bc=8 la=0x06` 같은 한 줄.
- 시나리오: NVMe SSD(03:00.0)가 호스트 메모리 0xABCD0E70 에서 1024B 를 MRRS 512 로 읽고, 호스트가 RCB 64·MPS 256 으로 돌려준다. 요청자는 CplD 의 BC/LA 만으로 1024바이트를 정확히 한 번씩 채워야 한다.

## 2. Problem (English)

> **TLP header codec.** Implement, with no dynamic allocation:
>
> ```c
> int tlp_encode(const tlp_hdr *h, uint8_t *out, size_t cap);   // 12 or 16, or TLP_E_* (out untouched)
> int tlp_decode(const uint8_t *in, size_t n, tlp_hdr *h);      // 12 or 16, or TLP_E_* (h untouched)
> int tlp_be(uint64_t addr, uint32_t len, tlp_span *o);         // Length + First/Last DW BE
> int tlp_split(uint64_t addr, uint64_t len, uint32_t max_bytes, tlp_chunk *out, size_t cap);
> uint16_t cpl_byte_count(uint16_t len_dw, uint8_t first_be, uint8_t last_be);
> int cpl_split(const tlp_hdr *mrd, uint32_t rcb, uint32_t mps, cpl_chunk *out, size_t cap);
> ```
>
> Support MRd/MWr in 3DW and 4DW form and Cpl/CplD. Use 4DW iff the address is ≥ 4 GB. Reject: truncated input,
> unsupported Fmt/Type, a 4DW request below 4 GB, a request crossing a 4 KB boundary, Last BE ≠ 0 for 1-DW requests,
> zero First/Last BE for multi-DW requests, non-contiguous BE (except QW-aligned 2-DW requests), out-of-range fields.
>
> Example: requester 01:00.0, tag 0x0C reads 8 bytes at 0xF7C01006 →
> `00 00 00 03 | 01 00 0C 3C | F7 C0 10 04` (Length 3, First BE 1100b, Last BE 0011b).
> The completer 00:00.0 answers `4A 00 00 03 | 00 00 00 08 | 01 00 0C 06` (Byte Count 8, Lower Address 0x06).
>
> Follow-ups: (1) Split a 1 KB DMA read at 0xABCD0E70 with MRRS = 512. (2) The root complex has RCB = 64 B and MPS = 256 B —
> list the completions with Byte Count / Lower Address. (3) How does the requester know which completion is the last?
> (4) Decode the AER header log `4a000003 00000008 01000c06`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 아홉 개: A1 `tlp_encode`, A2 `tlp_decode`, B1 `tlp_check`, B2 `tlp_be`, B3 `tlp_split`, C1 `cpl_byte_count`+`cpl_lower_addr`, C2 `cpl_split`, C3 `tlp_describe`. 하네스·`mk_req`/`mk_cpl`·브루트포스 `be_brute`·`tlp_from_dws` 는 주어진다. 스텁은 "TLP_OK / TLP_E_CAP / TLP_E_UNSUP / 0" 을 돌려줘 멈추거나 죽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
V1 MRd32 : EE EE EE EE | EE EE EE EE | EE EE EE EE
FAIL starter.c:258  rc == 12 && memcmp(b, v1, 12) == 0
…
A random round trip: 4000 headers, 4DW=0, bad=4000
…
B split properties: 16432 cases, bad=16432
…
7/80 passed
```

(exit 1.) 단계별 확인: A1·A2·B1 만 채우면 **34/80** (무작위 왕복은 아직 `tlp_be` 를 쓰므로 실패), B2·B3 까지 **60/80**, C 까지 **80/80**.

## 4. 힌트

<details><summary>힌트 1 — byte0 는 (Fmt &lt;&lt; 5) | Type 하나로 끝난다</summary>

Fmt 의 bit1 = "데이터 있음", bit0 = "4DW". MWr/CplD 면 bit1, 64-bit 주소면 bit0. Type 은 메모리 0x00, 완료 0x0A. 그래서 표의 여섯 값(00/20/40/60/0A/4A)이 바로 나온다. 디코더는 반대로 byte0 하나로 종류와 헤더 길이(12/16)를 정한 다음 길이 검사를 한다.

</details>

<details><summary>힌트 2 — 필드는 "바이트 몇, 비트 몇" 으로만 생각한다</summary>

헤더를 `uint32_t` 로 캐스트해 읽지 말 것 (호스트는 little-endian, 헤더는 바이트 0 이 최상위). `out[2] = TD<<7 | EP<<6 | (attr&3)<<4 | AT<<2 | len>>8` 처럼 바이트 단위로 조립하고, 주소만 `be32_put` 로 쓴다. Attr 는 두 군데로 갈라져 있다: Attr[1:0] 은 byte2, Attr[2](IDO) 는 byte1 bit2.

</details>

<details><summary>힌트 3 — BE 는 시작 쪽 마스크와 끝 쪽 마스크를 겹친다</summary>

시작 바이트 오프셋 s = addr & 3 → 첫 DW 마스크 `0xF << s` (& 0xF). 끝(exclusive) 오프셋 e = (addr+len) & 3 → 마지막 DW 마스크 e == 0 이면 0xF, 아니면 `(1 << e) − 1`. DW 가 하나뿐이면 두 마스크의 AND 를 First BE 에 두고 Last BE = 0.

</details>

<details><summary>힌트 4 — 분할 한계는 "바이트 수" 가 아니라 "DW 범위"</summary>

MPS 256 이면 Length ≤ 64 DW. 0x1002 에서 256바이트는 DW 0x1000..0x1100 에 걸쳐 65 DW 다. 조각 끝 = min(전체 끝, (cur & ~3) + max, 다음 4KB 경계).

</details>

<details><summary>힌트 5 — 완료의 Byte Count 는 "남은 양", Lower Address 는 "지금 위치"</summary>

첫 CplD: BC = 요청 전체 유효 바이트, LA = 첫 유효 바이트 주소 & 0x7F. 다음 CplD 들은 RCB 경계에서 시작하므로 BC = 마지막 유효 바이트 + 1 − 그 시작 주소, LA = 시작 주소 & 0x7F. CplD 끝 = min(DW 끝, (cur + MPS) 를 RCB 배수로 내림).

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "TLP 헤더는 3 또는 4 DW, byte0 = Fmt|Type 으로 종류와 길이가 정해진다" — 표 여섯 값.
2. 요청: Requester ID + Tag 가 짝짓기 키, BE 가 바이트 단위 유효 범위, 주소는 DW 정렬이고 4GB 이상만 4DW.
3. 규칙: 4KB 경계 넘지 말 것, MWr ≤ MPS, MRd ≤ MRRS (Length 기준).
4. 완료: Completer ID, Status, Byte Count(남은 양), Lower Address(지금 위치), RCB 경계로 분할.
5. 디버그: AER 헤더 로그 → 한 줄 요약 → "누가 어디에 무엇을".

### Part A — 손으로 V1 만들기 (바이트 → 필드)

요청자 01:00.0, tag 0x0C, 주소 0xF7C01006 에서 8바이트 읽기.

| 단계 | 계산 | 결과 |
|---|---|---|
| DW 범위 | 첫 DW 0xF7C01004, 끝(exclusive) 0xF7C0100E → 마지막 DW 0xF7C0100C | Length = 3 |
| First BE | 시작 오프셋 2 → `0xF << 2 & 0xF` | 1100b = 0xC |
| Last BE | 끝 오프셋 0xE & 3 = 2 → `(1<<2) − 1` | 0011b = 0x3 |
| DW0 | Fmt 000, Type 00000, TC 0, Attr 0, Length 3 | `00 00 00 03` |
| DW1 | RID = 1<<8 = 0x0100, Tag 0x0C, (Last<<4)\|First = 0x3C | `01 00 0C 3C` |
| DW2 | 주소[31:2] (하위 2비트 0) | `F7 C0 10 04` |

솔루션 실제 출력 (V1~V4):

```text
V1 MRd32 : 00 00 00 03 | 01 00 0C 3C | F7 C0 10 04
V2 MWr64 : 60 00 20 01 | 03 00 00 0F | 00 00 00 12 | 34 56 78 00
V3 CplD  : 4A 00 00 03 | 00 00 00 08 | 01 00 0C 06
V4 Cpl UR: 0A 00 00 00 | 00 08 20 04 | 00 00 05 00
```

- **V2** (MWr64, 03:00.0 → 0x12_3456_7800, 4바이트, RO): 주소가 4GB 이상이라 Fmt 011 → byte0 = 0x60. RO 는 Attr[1] → byte2 bit5 → 0x20. 1 DW 쓰기라 BE = Last 0 / First F → 0x0F. DW2 가 상위 32비트(0x00000012), DW3 가 하위.
- **V3** (V1 에 대한 CplD): Byte Count = 3×4 − 2(앞에서 꺼진 바이트) − 2(뒤에서 꺼진 바이트) = 8, Lower Address = 0xF7C01006 & 0x7F = 0x06. Requester ID 와 Tag 를 MRd 에서 그대로 복사한다 — 요청자는 (RID, Tag) 로 어떤 요청의 답인지 찾는다.
- **V4** (Cpl, UR): 없는 함수에 CfgRd → Root Port 00:01.0 이 UR 상태 완료. Status 001 → byte6 = 0x20, Byte Count 4, LA 0. Cfg/IO 요청의 완료는 BC = 4, LA = 0 으로 보내는 것이 규칙이다(스펙 확인 권장). 데이터 없는 Cpl 의 Length 필드는 reserved — 이 레슨 모델은 0 으로 보내고 읽을 때 무시한다. CPU 쪽 Root Complex 는 이 UR 을 받아 load 에 0xFFFFFFFF 를 돌려준다 (PCIE-B08 의 "0xFFFF = 장치 없음").
- **V5**: Length 1024 DW 요청은 Length 필드가 0, Byte Count 4096 도 필드 0. 10비트·12비트 필드로 1..1024 / 1..4096 을 표현하려면 0 을 최댓값으로 쓸 수밖에 없다.

모든 비트 위치 테스트 (TC 7, RO|NS|IDO, TH, TD, EP, AT 3 를 다 켠 MWr64): DW0 = `60 75 FC 01` — byte1 = 0111_0101 (TC 111, IDO, TH), byte2 = 1111_1100 (TD, EP, RO, NS, AT 11). 이 한 줄이 그림의 모든 작은 칸을 확인한다.

**디코더의 순서**가 중요하다: ① 4바이트 미만이면 잘림 ② byte0 → 종류·헤더 길이 ③ 헤더 길이 미만이면 잘림 ④ 필드 추출 ⑤ 4DW 상위 0 거부 ⑥ `tlp_check` ⑦ 성공일 때만 `*h = t`. ③ 을 ② 앞에 둘 수는 없다(4DW 인지 byte0 를 봐야 안다). 임시 `t` 에 채우고 마지막에 복사하는 이유는 "오류면 출력 불변" 약속 — 펌웨어에서 반쯤 채운 구조체가 다음 단계로 새는 버그를 막는다.

무작위 왕복: 종류·TC·Attr·TH·TD·EP·AT·ID·Tag 를 다 무작위로, 요청 주소는 절반을 4GB 이상으로, 길이는 4KB 를 넘지 않게 뽑아 `tlp_be` 로 BE 를 만든다. 실측 4000개 중 4DW 1014개, 불일치 0.

### Part B — BE 규칙과 4KB, 그리고 분할

**BE 규칙** (요청, 코드 `tlp_check`):

| Length | First DW BE | Last DW BE | 연속성 |
|---|---|---|---|
| 1 DW | 아무 값 (0000 = zero-length) | **0000 이어야 함** | 1 DW 안에서는 불연속 허용 (예: 1001) |
| 2 DW, 주소가 8바이트(QW) 정렬 | 0000 이 아님 | 0000 이 아님 | 불연속 허용 (First 0101 / Last 1010 OK) |
| 그 외 2 DW 이상 | 1111/1110/1100/1000 | 0001/0011/0111/1111 | 첫 바이트부터 끝 바이트까지 빈틈 없음 |

왜 이런 규칙인가: 2 DW 이상이면 중간 DW 는 전부 유효로 간주하므로, 양 끝 BE 에 구멍이 있으면 "연속된 바이트 범위" 라는 전제가 깨진다. QW 정렬 2 DW 예외는 64-bit 레지스터 하나에 바이트 마스크를 자유롭게 쓰는 경우(구형 PCI 호환)를 위한 것. 연습 2 에서 256개 (First, Last) 조합을 전부 돌려 유효 조합 수를 센다: Length 1 → 16, Length 3 → 16, QW 정렬 Length 2 → 225(=15×15), QW 비정렬 Length 2 → 16.

**4KB 규칙**: 메모리 요청은 4KB 경계를 넘으면 안 된다 — `(addr & 0xFFF) + 4×Length ≤ 4096`. 이유는 완료자·IOMMU 가 페이지 단위로 주소를 변환하고 라우팅하므로, 한 TLP 가 두 페이지(서로 다른 물리 장치일 수도)에 걸치면 처리할 수 없기 때문이다. 그래서 Length 1024(4096B) 요청은 4KB 정렬 주소에서만 가능하다 (테스트 `big`: 0x80000004 에서 1024 DW → `TLP_E_4K`).

**4DW iff ≥ 4GB**: 요청자는 4GB 미만 주소에 반드시 3DW 를 써야 한다. 상위 32비트가 0 인 4DW 를 받았을 때의 처리는 스펙 확인 권장 — 이 레슨 디코더는 거부(`TLP_E_ADDR`)로 정했다. 검증 관점에선 "요청자 위반" 으로 로그에 남길 일이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-weight="bold" font-size="13">BE 계산: 0xF7C01006 부터 8바이트 (V1)</text>
  <g stroke="currentColor">
    <rect x="80" y="30" width="40" height="28" class="f-mut"/><rect x="120" y="30" width="40" height="28" class="f-mut"/>
    <rect x="160" y="30" width="40" height="28" class="f-ok"/><rect x="200" y="30" width="40" height="28" class="f-ok"/>
    <rect x="250" y="30" width="40" height="28" class="f-ok"/><rect x="290" y="30" width="40" height="28" class="f-ok"/>
    <rect x="330" y="30" width="40" height="28" class="f-ok"/><rect x="370" y="30" width="40" height="28" class="f-ok"/>
    <rect x="420" y="30" width="40" height="28" class="f-ok"/><rect x="460" y="30" width="40" height="28" class="f-ok"/>
    <rect x="500" y="30" width="40" height="28" class="f-mut"/><rect x="540" y="30" width="40" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="100" y="49">..04</text><text x="140" y="49">..05</text><text x="180" y="49">..06</text><text x="220" y="49">..07</text>
    <text x="270" y="49">..08</text><text x="310" y="49">..09</text><text x="350" y="49">..0A</text><text x="390" y="49">..0B</text>
    <text x="440" y="49">..0C</text><text x="480" y="49">..0D</text><text x="520" y="49">..0E</text><text x="560" y="49">..0F</text>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="160" y="76">첫 DW 0x..04: bit3..0 = 1100</text><text x="330" y="76">가운데 DW: 전부 유효</text>
    <text x="500" y="76">끝 DW 0x..0C: 0011</text>
  </g>
  <text x="600" y="49" fill="currentColor">→ Length 3, BE 3/C</text>
  <text x="80" y="98" fill="currentColor">BE 비트 i ↔ 바이트 (DW 주소 + i): 왼쪽(낮은 주소) 칸이 bit0</text>
  <text x="10" y="128" fill="currentColor" font-weight="bold" font-size="13">SSD 가 0xABCD0E70 에서 1024B 읽기: MRRS 512 로 MRd 분할 → RCB 64 · MPS 256 으로 CplD</text>
  <line x1="312" y1="138" x2="312" y2="300" stroke="currentColor" stroke-dasharray="5,4"/>
  <text x="318" y="150" fill="currentColor">4KB 경계 0xABCD1000</text>
  <text x="6" y="178" fill="currentColor">MRd</text>
  <g stroke="currentColor">
    <rect x="60" y="160" width="252" height="28" class="f-acc"/><rect x="312" y="160" width="322.6" height="28" class="f-acc"/>
    <rect x="634.6" y="160" width="70.5" height="28" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="186" y="179">tag0: 0xE70, 400B (100 DW)</text><text x="473.3" y="179">tag1: 0x1000, 512B (128 DW)</text>
    <text x="669.8" y="179">tag2 112B</text>
  </g>
  <text x="6" y="222" fill="currentColor">CplD</text>
  <text x="6" y="236" fill="currentColor">RCB64</text>
  <g stroke="currentColor">
    <rect x="60" y="204" width="131" height="40" class="f-ok"/><rect x="191" y="204" width="121" height="40" class="f-ok"/>
    <rect x="312" y="204" width="161.3" height="40" class="f-ok"/><rect x="473.3" y="204" width="161.3" height="40" class="f-ok"/>
    <rect x="634.6" y="204" width="70.5" height="40" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="125.5" y="220">52 DW</text><text x="125.5" y="236">bc400 la70</text>
    <text x="251.5" y="220">48 DW</text><text x="251.5" y="236">bc192 la40</text>
    <text x="392.6" y="220">64 DW</text><text x="392.6" y="236">bc512 la00</text>
    <text x="553.9" y="220">64 DW</text><text x="553.9" y="236">bc256 la00</text>
    <text x="669.8" y="220">28 DW</text><text x="669.8" y="236">bc112</text>
  </g>
  <text x="6" y="270" fill="currentColor">tag0</text>
  <text x="6" y="284" fill="currentColor">RCB128</text>
  <g stroke="currentColor">
    <rect x="60" y="252" width="90.7" height="40" class="f-warn"/><rect x="150.7" y="252" width="161.3" height="40" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="105.3" y="268">36 DW</text><text x="105.3" y="284">bc400 la70</text>
    <text x="231.3" y="268">64 DW</text><text x="231.3" y="284">bc256 la00</text>
  </g>
  <text x="324" y="276" fill="currentColor">← 같은 tag0 를 RCB 128 완료자가 돌려주면</text>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="60" y="316">E70</text><text x="150.7" y="316">F00</text><text x="191" y="316">F40</text>
    <text x="312" y="316">1000</text><text x="473.3" y="316">1100</text><text x="634.6" y="316">1200</text><text x="705" y="316">1270</text>
  </g>
</svg>
```

**분할 (`tlp_split`)** — 앞에서부터 가장 크게:

```c
uint64_t e = end;
uint64_t lim_mps = (cur & ~3ull) + max_bytes;   /* DW 정렬 시작 기준 → Length×4 ≤ max */
uint64_t lim_4k  = (cur | 0xFFFull) + 1u;       /* 다음 4KB 경계 */
if (lim_mps < e) e = lim_mps;
if (lim_4k  < e) e = lim_4k;
```

벡터 (전부 테스트에 있음):

| 입력 (addr, len, max) | 조각 | 이유 |
|---|---|---|
| 0xABCD0E70, 1024, 512 | (0E70, 400) (1000, 512) (1200, 112) | 첫 조각은 4KB 경계에서 끊김 (0xE70+512 = 0x1070 > 0x1000) |
| 0x1002, 256, 256 | (1002, 254) (1100, 2) | 256B 한 번이면 DW 0x1000..0x1100 = **65 DW** > 64 |
| 0xFFE, 4, 4096 | (FFE, 2) (1000, 2) | 4바이트도 4KB 경계를 넘으면 둘로 |
| 0x1004, 4096, 256 | 17개, 16번째 (1F04, 252), 17번째 (2000, 4) | 앞 15개는 256B 씩, 이후 4KB 경계 |

성질 테스트: 시작 오프셋 0..4095(13 간격) × 길이 13종 × max 4종 = 16432 경우, 모든 조각이 (a) 빈틈 없이 이어지고 (b) `tlp_be` 로 만든 Length×4 ≤ max (c) 4KB 를 안 넘고 (d) 실제로 4DW MWr 로 인코딩되며 (e) 마지막이 아닌 조각은 MPS 한계나 4KB 경계 중 하나에 정확히 닿는다(= 가장 크게). 실측 bad = 0.

"앞에서부터 최대" 는 스펙이 요구하는 최소 조건만 지킨 정책이다. 실제 DMA 엔진은 보통 첫 조각만 짧게 하고 이후를 max 정렬 주소에서 시작한다 — 개수는 같아도 이후 TLP 가 캐시 라인·RCB 에 맞는다 (연습 1: 0x1004 에서 4KB 쓰기, 쪼개진 64B 라인이 15 → 0).

### Part C — Completion 의 Byte Count 와 Lower Address

**Byte Count 표** (첫 CplD, 즉 요청 전체 유효 바이트):

- Length 1: 켜진 최고 바이트 − 최저 바이트 + 1 (사이 구멍 포함). 1001 → 4, 0110 → 2, 0000 → **1** (zero-length read 도 1).
- Length ≥ 2: `4×Length − (First BE 앞쪽 꺼진 바이트 수) − (Last BE 뒤쪽 꺼진 바이트 수)`. 예: V1 = 12 − 2 − 2 = 8, Length 2 · BE 8/1 = 8 − 3 − 3 = 2.

연습 2 가 이 표 전체(Length 1 의 16 값 + Length ≥ 2 의 16 조합)를 실제로 찍는다. 스펙의 "Byte Count 계산 표" 와 같은 모양이다.

**분할 규칙 (`cpl_split`)**. 완료자는 MRd 하나를 여러 CplD 로 나눠 보낼 수 있는데, 조건은 (1) 각 CplD 페이로드 ≤ MPS, (2) 마지막이 아닌 CplD 는 **RCB 정렬 주소에서 끝남**, (3) 같은 요청의 CplD 는 주소 순서. Root Port 의 RCB 는 64 또는 128B 이고, 소프트웨어가 엔드포인트의 Link Control RCB 비트(`PCI_EXP_LNKCTL_RCB 0x0008`, pci_regs.h)를 Root Port 에 맞춘다. 이 레슨의 완료자 정책은 "MPS 이하에서 가장 크게":

```c
uint64_t lim = (cur + mps) & ~(uint64_t)(rcb - 1u);   /* MPS 안에서 가장 먼 RCB 경계 */
if (lim < e) e = lim;
out[n].byte_count = byte_end - start;                 /* 남은 유효 바이트 (이번 포함) */
out[n].lower_addr = start & 0x7F;                     /* 이번 첫 유효 바이트 위치 */
```

MPS ≥ 128 ≥ RCB 이므로 `lim > cur` 가 항상 성립해 루프가 진행한다 (증명: cur + mps − (rcb−1) > cur).

**단계별 추적** (그림의 tag0, 0xABCD0E70 부터 400B = 100 DW, BE F/F, RCB 64, MPS 256):

| CplD | 범위 | 계산 | Length | BC | LA |
|---|---|---|---|---|---|
| 1 | 0xE70..0xF40 | (0xE70 + 0x100) = 0xF70 → 64 배수로 내림 0xF40 | 52 DW | 400 | 0x70 |
| 2 | 0xF40..0x1000 | 0xF40 + 0x100 = 0x1040 > 끝 0x1000 → 끝 | 48 DW | 1000 − F40 = 192 | 0x40 |

RCB 128 이면 첫 CplD 가 0xF00 에서 끝나 36 DW / 64 DW 가 된다 — RCB 가 클수록 첫 CplD 가 짧아질 수 있다.

**요청자 쪽 (조립)**. 요청자는 CplD 헤더만 보고:
- 이 데이터의 첫 유효 바이트 주소 = 요청 첫 바이트 + (BC₀ − BC). 하위 7비트가 LA 와 같아야 한다 (다르면 Malformed/Unexpected 로 취급할 근거).
- **이 CplD 가 마지막인가** = `BC ≤ 4×Length − (LA & 3)`. 즉 남은 양이 이번 데이터 안에 다 들어 있으면 마지막. 무작위 3000개 요청(RCB·MPS 무작위)에서 이 식이 실제 마지막과 항상 일치함을 테스트한다.
- 마지막이 아니면 이번에 받은 바이트 = `4×Length − (LA & 3)` 만큼 남은 양을 줄인다.

zero-length read(Length 1, BE 0000)는 BC = 1, LA = DW 주소, 데이터 1 DW(값은 의미 없음) 하나로 끝난다. 드라이버가 "앞의 posted write 가 다 도착했나" 를 확인하는 flush 용으로 쓰는 요청이다 (순서 규칙은 PCIE-B11).

**SSD 시나리오 실측** (솔루션 출력):

```text
C SSD MRd tag0: 00 00 00 64 | 03 00 00 FF | AB CD 0E 70
C SSD   CplD tag0 len=52 DW bc=400 la=0x70
C SSD   CplD tag0 len=48 DW bc=192 la=0x40
C SSD MRd tag1: 00 00 00 80 | 03 00 01 FF | AB CD 10 00
C SSD   CplD tag1 len=64 DW bc=512 la=0x00
C SSD   CplD tag1 len=64 DW bc=256 la=0x00
C SSD MRd tag2: 00 00 00 1C | 03 00 02 FF | AB CD 12 00
C SSD   CplD tag2 len=28 DW bc=112 la=0x00
```

MRd 3개(Length 0x64 = 100, 0x80 = 128, 0x1C = 28, BE 전부 F/F), CplD 5개. 하네스는 각 CplD 를 인코딩→디코딩한 뒤 **헤더만으로** 위치를 계산해 1024바이트 각각이 정확히 한 번 채워짐을 확인한다. 실제 NVMe 컨트롤러에서는 PRP 엔트리 하나(PCIE-M04)가 이런 MRd 묶음이 된다.

**AER 로그 해석 (`tlp_describe`)**. Linux AER 은 오류를 일으킨 TLP 헤더를 DW 단위 u32 로 찍는다 (헤더 로그 레지스터 4개, `include/linux/aer.h` 의 `PCIE_STD_NUM_TLP_HEADERLOG 4`). DW 의 bit31:24 가 헤더 바이트 0 이므로 `4a000003` → 바이트 `4A 00 00 03`. 출력 형식은 커널 버전마다 조금 다르다 (예: `TLP Header: 4a000003 00000008 01000c06 00000000`). 실측:

```text
C AER L1: CplD SC 00:00.0->01:00.0 tag=0x0c len=3 bc=8 la=0x06
C AER L2: MRd64 03:00.0 tag=0xff addr=0x123456000 len=128 be=f/f
C AER L3: MWr32 03:00.0 tag=0x00 addr=0xfe001000 len=1 be=f/0 EP
C AER L4: Cpl UR 00:01.0->00:00.0 tag=0x05 len=0 bc=4 la=0x00
```

L2 는 Completion Timeout 때 흔히 보는 모양(03:00.0 이 512B 를 읽으려 했는데 답이 안 옴 → 그 주소가 IOMMU 매핑 밖인지, 호스트가 멈췄는지), L3 는 Poisoned TLP(EP=1) 로 데이터가 오염된 쓰기. 로그에서 RID 로 범인을, 주소로 대상을, Tag 로 짝을 찾는다 (AER 처리 흐름은 PCIE-M12).

### 복잡도와 자원

인코드·디코드 O(1), 12/16바이트 고정. 분할·완료 목록은 O(조각 수), 호출자 버퍼 + `cap` 검사, malloc 없음. DMA 엔진 하나가 4KB 페이지 하나에 만드는 MRd 는 MRRS 128 일 때 최대 32개(페이지 정렬 안 된 4KB 면 33개) — `cap` 을 그만큼 잡으면 된다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 80/80 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
```

테스트 외 출력 줄 (위 풀이에 나온 V1~V4, AER, SSD 줄 외):

```text
A random round trip: 4000 headers, 4DW=1014, bad=0
B split properties: 16432 cases, bad=0
80/80 passed
```

## 7. 자주 하는 실수

**1. 헤더를 `uint32_t*` 로 캐스트해 읽는다.** little-endian CPU 에서 `*(uint32_t*)hdr` 는 `0x03000000` 이 되어 Fmt/Type 이 byte3 에서 나온다. 고친 코드: 바이트 단위 `in[0] >> 5`, 주소는 `be32_get`. AER 로그처럼 이미 u32 로 찍힌 값은 반대로 `be32_put` 으로 바이트를 복원한다 (`tlp_from_dws`).

**2. MPS 를 바이트 수로만 비교한다.** `chunk = min(remaining, mps)` 로 자르면 0x1002 에서 256B 가 Length 65 DW 가 되어 받는 쪽이 **Malformed TLP**(AER uncorrectable, `PCI_ERR_UNC_MALF_TLP 0x00040000`)로 처리한다. 정렬된 주소로만 시험하면 절대 안 보이는 버그. 고친 코드: 한계를 `(cur & ~3) + mps` 로.

**3. 4KB 경계를 잊는다.** MRRS 512 에서 0xE70 부터 512B 한 번 → 0x1070 까지, 경계 넘음 → Malformed. 실제 SSD 에선 PRP 가 보통 페이지 정렬이라 잘 안 보이다가 PRP1 오프셋이 있는 첫 페이지(PCIE-M04)나 SGL 에서 터진다. 고친 코드: `lim_4k = (cur | 0xFFF) + 1`.

**4. 4GB 미만 주소에 4DW 를 쓴다 / 4GB 이상에 3DW 를 쓴다.** 후자는 주소 상위 32비트가 사라져 **엉뚱한 하위 주소에 DMA** — 가장 위험한 형태. 64-bit DMA 마스크를 안 켠 드라이버 + 버그 있는 엔드포인트 조합에서 나온다. 고친 코드: 인코더가 주소로 Fmt bit0 를 정한다 (`is4 = addr > 0xFFFFFFFF`).

**5. 1 DW 요청에 Last BE 를 채운다.** `first = 0xF, last = 0xF, len = 1` 은 규칙 위반(Malformed). 고친 코드: DW 하나면 두 마스크를 AND 해서 First 에 두고 Last = 0.

**6. Byte Count 를 "이번 CplD 크기" 로 쓴다.** BC 는 **남은 양**이다. 이번 크기로 쓰면 요청자의 "마지막인가" 식이 첫 CplD 에서 참이 되어 나머지 CplD 가 Unexpected Completion 이 된다. 고친 코드: `byte_end − start`.

**7. Length 1024 / Byte Count 4096 을 그대로 마스크한다.** `len & 0x3FF` 는 맞지만 디코더에서 0 을 0 으로 두면 4KB 읽기가 "0 DW" 가 된다. 고친 코드: `len ? len : 1024`, `bc ? bc : 4096`.

## 8. 면접 Q&A

<details><summary>Q1. byte0 = 0x4A 이면 무엇이고, 0x60 이면 무엇인가요?</summary>

0x4A = 010_01010 → Fmt 010(3DW, 데이터 있음), Type 0_1010(완료) → **CplD**. 0x60 = 011_00000 → Fmt 011(4DW, 데이터), Type 0 → **MWr64**. 나머지: 0x00 MRd32, 0x20 MRd64, 0x40 MWr32, 0x0A Cpl. 꼬리 질문 "Cfg 는?" → Type 0_0100 CfgRd0/CfgWr0, 0_0101 Type1 (이 레슨 코덱은 `TLP_E_UNSUP`). "Fmt 100?" → TLP Prefix (PASID 등, 헤더 앞에 붙음).

</details>

<details><summary>Q2. 왜 4KB 경계를 넘는 요청이 금지되나요? 길이 1024 DW 요청은 어떤 주소에서만 가능하죠?</summary>

완료자·스위치·IOMMU 가 4KB 페이지 단위로 주소를 변환·라우팅하므로, 한 TLP 가 두 페이지에 걸치면 두 페이지가 서로 다른 장치나 다른 물리 주소일 수 있어 처리할 수 없다. 그래서 규칙은 `(addr & 0xFFF) + 4×Length ≤ 4096`, 1024 DW 는 4KB 정렬 주소에서만. DMA 엔진은 그래서 "4KB 경계 + MPS/MRRS" 두 한계로 자른다.

</details>

<details><summary>Q3. MPS 와 MRRS 는 무엇이 다르고, 처리량에 왜 중요한가요?</summary>

MPS(Max Payload Size, Device Control bit7:5 = `PCI_EXP_DEVCTL_PAYLOAD 0x00e0`)는 **데이터를 실은 TLP**(MWr, CplD)의 최대 페이로드, MRRS(Max Read Request Size, bit14:12 = `PCI_EXP_DEVCTL_READRQ 0x7000`)는 **MRd 한 개가 요청할 수 있는 최대 크기**. 둘 다 128 << n. MPS 는 경로 위 모든 장치가 지원하는 최솟값으로 맞춘다(작은 쪽이 받지 못하면 Malformed). TLP 마다 헤더+DLL+PHY 오버헤드가 약 20~30바이트(PCIE-B02 모델)라 MPS 128 vs 256 은 효율이 눈에 띄게 다르고, MRRS 가 작으면 읽기에 더 많은 태그가 필요하다 (연습 3).

</details>

<details><summary>Q4. 요청자는 여러 CplD 중 어느 것이 마지막인지 어떻게 아나요? 순서가 섞이면요?</summary>

`BC ≤ 4×Length − (LA & 3)` 이면 마지막 — 남은 양이 이번 데이터 안에 다 들어 있다는 뜻. 같은 요청(같은 Tag)의 CplD 는 주소 순서로 와야 하지만, **서로 다른 Tag 의 CplD 는 섞여도 된다**. 그래서 요청자는 Tag 별로 "남은 BC" 를 들고 있다가 BC·LA 가 기대값과 같은지 확인한다(연습 4). 낸 적 없는 Tag 의 완료는 Unexpected Completion (AER uncorrectable 비트 `PCI_ERR_UNC_UNX_COMP 0x00010000`; 상황에 따라 Advisory Non-Fatal `PCI_ERR_COR_ADV_NFAT` 로 낮춰 보고 — 세부는 스펙 확인 권장). 답이 끝내 안 오면 Completion Timeout (`PCI_ERR_UNC_COMP_TIME 0x00004000`).

</details>

<details><summary>Q5. RCB 는 무엇이고 누가 정하나요? 64 와 128 이면 무엇이 달라지나요?</summary>

Read Completion Boundary — 완료자가 한 MRd 를 여러 CplD 로 나눌 때 마지막이 아닌 CplD 는 RCB 정렬 주소에서 끝나야 한다. Root Port 는 64 또는 128B 를 하드웨어로 정하고(Link Control 의 RCB 비트를 읽기 전용으로 보고), 소프트웨어가 엔드포인트의 RCB 비트(`PCI_EXP_LNKCTL_RCB`)를 거기에 맞춘다. 이 레슨 예: tag0 첫 CplD 가 RCB 64 면 52 DW, 128 이면 36 DW. 요청자는 이걸로 "적어도 이 정도 단위로 온다" 를 가정해 버퍼를 관리한다.

</details>

<details><summary>Q6. Tag 는 몇 비트이고, 왜 Extended Tag 를 켜야 NVMe 가 빨라지나요?</summary>

기본 5비트(32개), Device Control 의 Extended Tag Field Enable(`PCI_EXP_DEVCTL_EXT_TAG 0x0100`)을 켜면 8비트(256개). Gen4+ 는 10-bit Tag 도 있다(DEVCAP2/DEVCTL2 비트, 헤더의 T9/T8 자리 — 스펙 확인 권장). 동시에 날아가 있을 수 있는 MRd 수 = 태그 수, 그래서 읽기 대역폭 ≤ 태그 × MRRS / 왕복 시간. Gen4 x4(약 7.9 GB/s), RTT 4 µs, MRRS 512 이면 62개 필요 → 32개로는 4.1 GB/s 에서 막힌다 (연습 3 실측).

</details>

<details><summary>Q7. RO 와 NS 비트는 실제로 누가 켜고, 무엇이 달라지나요?</summary>

NS(No Snoop) = "이 DMA 는 CPU 캐시를 스누프하지 않아도 된다" — 드라이버가 캐시 일관성을 직접 관리하는 버퍼(예: 그래픽)에만. 켜고 캐시를 flush 안 하면 낡은 데이터(ARCH-M12, BM-M08). RO(Relaxed Ordering) = 이 TLP 는 앞선 posted write 를 앞질러도 된다 — 데이터 MWr 에는 켜서 처리량을 얻고, "완료 표시" MWr(NVMe CQ 엔트리) 에는 끄는 식. 장치는 Device Control 의 `PCI_EXP_DEVCTL_RELAX_EN 0x0010`, `NOSNOOP_EN 0x0800` 이 켜져 있을 때만 쓸 수 있다. 순서표 자체는 PCIE-B11.

</details>

<details><summary>Q8. 검증 엔지니어로서 이 코덱으로 무엇을 자동 검사하나요?</summary>

애널라이저 트레이스를 읽어 (1) 모든 요청이 4KB·MPS·MRRS·BE·3DW/4DW 규칙을 지키는지 (2) 모든 non-posted 요청에 (RID, Tag) 가 맞는 완료가 오는지, BC/LA 가 맞는지, 완료 시간 초과는 없는지 (3) 같은 Tag 가 완료 전에 재사용되지 않는지 (4) EP/UR/CA 상태 수. 이 레슨의 `tlp_check` + 연습 4 의 조립기가 바로 그 체커의 핵심이다. 펌웨어(엔드포인트) 쪽이면 같은 검사를 DMA 엔진 설정값 단위 테스트로 돌린다.

</details>

## 9. 추가 연습

### 연습 1 — 정렬 분할: 첫 조각만 짧게

`tlp_split_aligned` 를 짜라: 조각 끝 = min(전체 끝, cur 를 max 로 내림 + max). 0x1004 에서 4096B, MPS 256 을 두 정책으로 자르고 조각 수와 "두 TLP 에 걸쳐 쪼개진 64B 캐시 라인 수" 를 비교하라. 정렬 분할도 4KB·Length 규칙을 지키는지 오프셋 전체에 대해 확인하라.

<details><summary>답</summary>

max 는 4096 을 나누는 2의 거듭제곱이라 max 정렬 경계는 4KB 경계를 포함하고, DW 정렬 시작 기준으로도 `align_down(cur, max) + max − (cur & ~3) ≤ max` 라 Length 규칙이 자동으로 지켜진다. 실측: 둘 다 17개, 쪼개진 라인 greedy 15 vs aligned 0 (greedy 는 0x1104, 0x1204 … 0x1F04 에서 끊김). 쪼개진 라인은 완료자 쪽에서 read-modify-write 나 부분 쓰기를 부를 수 있고, 이후 TLP 가 RCB·캐시 라인에 맞으면 메모리 컨트롤러 효율이 좋다 — 그래서 실제 DMA 엔진은 정렬 분할을 쓴다.

</details>

### 연습 2 — Byte Count 표와 유효 BE 조합

`cpl_byte_count` 로 Length 1 의 16 BE 와 Length ≥ 2 의 (First, Last) 16 조합에 대한 표를 찍고, 256개 (First, Last) 조합을 `tlp_check` 에 넣어 Length 1 / 3 / QW 정렬 2 / 비정렬 2 의 유효 조합 수를 세라.

<details><summary>답</summary>

실측 표:

```text
ex2 Length 1 : 0000=1 0001=1 0010=1 0011=2 0100=1 0101=3 0110=2 0111=3 1000=1 1001=4 1010=3 1011=4 1100=2 1101=4 1110=3 1111=4
ex2 Length>1 : BC = 4L - [F/F]0 [F/7]1 [F/3]2 [F/1]3 [E/F]1 [E/7]2 [E/3]3 [E/1]4 [C/F]2 [C/7]3 [C/3]4 [C/1]5 [8/F]3 [8/7]4 [8/3]5 [8/1]6
ex2 valid (First,Last) pairs: len1=16 len3=16 len2(QW aligned)=225 len2(not QW)=16
```

Length 1 은 구멍을 포함한 span(1001 → 4), 0000 → 1. Length ≥ 2 는 빼는 값 = 앞에서 꺼진 i + 뒤에서 꺼진 j (0..6). 유효 조합: Length 1 은 Last 0 만(16), 연속 규칙이면 4×4 = 16, QW 정렬 2 DW 는 0 만 빼고 전부 15×15 = 225.

</details>

### 연습 3 — DEVCTL 디코드와 태그 한계 대역폭

Device Control 값 `0x2930` 을 `pci_regs.h` 마스크로 디코드(MPS, MRRS, Extended Tag, RO, NS)하라. reserved MPS/MRRS 코드(110b, 111b)는 거부. 그리고 `tags_needed(link, rtt, mrrs) = ceil(link × rtt / mrrs)` 와 `read_bw = min(link, tags × mrrs / rtt)` 를 정수로 구현해 Gen4 x4(7877 MB/s, PCIE-B02 의 128b/130b 값) 에 적용하라.

<details><summary>답</summary>

0x2930 → PAYLOAD (0x20 >> 5 = 1) 256B, READRQ (0x2000 >> 12 = 2) 512B, EXT_TAG(0x0100) → 256 태그, RELAX_EN(0x0010), NOSNOOP_EN(0x0800). 실측:

```text
ex3 tags_needed(Gen4x4, RTT 1us/2us/4us, MRRS 512) = 16/31/62
ex3 read_bw RTT 4us: 32 tags MRRS 512 = 4096 MB/s, 32 tags MRRS 128 = 1024 MB/s, 256 tags MRRS 512 = 7877 MB/s
```

Little's Law(동시 요청 = 처리량 × 지연, PCIE-A02)의 PCIe 판. 헤더 오버헤드는 무시한 상한이다. 실제 RTT 는 플랫폼·IOMMU·메모리 부하에 따라 1~수 µs (가정). 교훈: "Gen4 SSD 가 3~4 GB/s 에서 막힌다" 면 링크 폭·속도(PCIE-B04/M08) 다음으로 MRRS 와 Extended Tag 를 본다 — `lspci -vvv` 의 `DevCtl: ... ExtTag+ ... MaxPayload 256 bytes, MaxReadReq 512 bytes` (PCIE-B06).

</details>

### 연습 4 — 요청자 쪽 완료 조립기

Tag 32개짜리 표로 `rq_issue(mrd)` / `rq_complete(cpl_bytes)` 를 짜라. 본문 SSD 시나리오의 CplD 5개를 tag 가 섞인 순서 (1,0,2,1,0 — 같은 tag 안에서는 주소 순서)로 넣어 PART/DONE 을 확인하고, 오류를 주입하라: 사용 중 tag 재발행, 끝난 tag 에 또 온 완료, 낸 적 없는 tag, 다른 Requester ID, 틀린 BC, 틀린 LA, UR 상태 (그 뒤 tag 재사용 가능).

<details><summary>답</summary>

슬롯 = {busy, 첫 바이트 주소, BC₀, 남은 BC}. 완료 처리 순서: 디코드 → 종류/RID/tag/busy 확인(아니면 Unexpected, 카운터 증가) → 상태가 SC 가 아니면 슬롯을 풀고 오류(드라이버는 이 요청을 실패로 보고) → BC == 남은 BC → LA == (첫 바이트 + BC₀ − 남은) & 0x7F → 마지막 판정식으로 DONE 또는 남은 양 감소. 실측 `ex4 results: 1 1 2 2 2` (PART PART DONE DONE DONE), 주입한 오류 7가지 전부 해당 코드로 잡힘, Unexpected 카운터 3. 실제 하드웨어에선 Unexpected Completion 과 Completion Timeout 이 AER 로 보고된다 (PCIE-M12) — 이 표가 바로 그 판단의 근거다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 25/25 passed
```

```text
ex1 greedy : 17 TLPs, first=0x1004+256, split 64B lines=15
ex1 aligned: 17 TLPs, first=0x1004+252, split 64B lines=0
ex4 results: 1 1 2 2 2 (1=PART 2=DONE)
25/25 passed
```

## 10. 핵심 정리

- byte0 = Fmt<<5 | Type: MRd32 00, MRd64 20, MWr32 40, MWr64 60, Cpl 0A, CplD 4A. Fmt bit1 = 데이터, bit0 = 4DW. 헤더는 바이트 0 이 먼저(DW 안 big-endian), 페이로드는 주소 순.
- DW0: TC byte1[6:4], IDO byte1[2], TH byte1[0], TD/EP byte2[7:6], RO/NS byte2[5:4], AT byte2[3:2], Length 10비트(0 = 1024).
- 요청 DW1 = RID | Tag | LastBE | FirstBE, 주소 DW 정렬, **4DW iff 주소 ≥ 4GB**.
- BE: 1 DW → Last 0; 2 DW 이상 → 둘 다 ≠ 0 이고 연속 (QW 정렬 2 DW 만 예외).
- 요청 분할: 4KB 경계 + Length×4 ≤ MPS(쓰기)/MRRS(읽기). 한계는 **DW 정렬 시작 기준** — 0x1002 의 256B 는 65 DW.
- 완료 DW1 = CplID | Status | BCM | Byte Count(남은 양, 0 = 4096), DW2 = RID | Tag | Lower Address(지금 위치 하위 7비트).
- CplD 분할: ≤ MPS, 마지막 아니면 RCB(64/128) 경계에서 끝. 마지막 판정 `BC ≤ 4×Length − (LA & 3)`.
- AER 헤더 로그 DW 의 bit31:24 = 바이트 0. RID 로 범인, 주소로 대상, Tag 로 짝.

## 참고자료

- PCI-SIG, *PCI Express Base Specification* (Rev 4.0 / 5.0 / 6.x) — 2.2 Transaction Layer Protocol: Packet Format, Fmt/Type 인코딩 표, 2.2.5 First/Last DW Byte Enables 규칙, 2.2.9 Completion Rules (Byte Count·Lower Address 계산 표, RCB), 2.3.1.1 데이터 반환 규칙. 이 레슨의 필드 위치·인코딩은 이 장을 기억으로 정리한 것 — Cpl 의 Length reserved, Cfg/IO 완료 BC = 4, 4DW-below-4GB 수신 처리, 10-bit Tag 비트 위치는 원문 확인 권장.
- Linux `include/uapi/linux/pci_regs.h` (Lima VM `/usr/include/linux/pci_regs.h` 로 확인): `PCI_EXP_DEVCTL_PAYLOAD 0x00e0`, `PCI_EXP_DEVCTL_READRQ 0x7000`, `PCI_EXP_DEVCTL_EXT_TAG 0x0100`, `PCI_EXP_DEVCTL_RELAX_EN 0x0010`, `PCI_EXP_DEVCTL_NOSNOOP_EN 0x0800`, `PCI_EXP_LNKCTL_RCB 0x0008`, `PCI_EXP_DEVCTL2_IDO_REQ_EN 0x0100`, `PCI_ERR_UNC_MALF_TLP 0x00040000`.
- Linux `include/linux/aer.h` (7.0 헤더로 확인): `struct pcie_tlp_log`, `PCIE_STD_NUM_TLP_HEADERLOG 4`, Flit 모드 로그 필드. 출력 함수 `pcie_print_tlp_log()` (drivers/pci/pcie/tlp.c, 형식은 커널 버전마다 다름).
- Ravi Budruk, Don Anderson, Tom Shanley, *PCI Express System Architecture* (MindShare) — 4~6장 TLP 헤더 그림과 완료 분할 예제.
- Jackson & Budruk, *PCI Express Technology 3.0* (MindShare) — Byte Enable, RCB, Tag 와 성능.
- 관련 레슨: PCIE-B01 (TLP 를 DLL/PHY 로 감싸기), PCIE-B02 (대역폭·TLP 오버헤드), PCIE-B04 (Device Capability, Link 상태), PCIE-B06 (lspci DevCtl 읽기), PCIE-B08 (UR → 0xFFFFFFFF), PCIE-B11 (RO/IDO 와 순서 규칙), PCIE-B12/B13 (NVMe BAR0 doorbell = MWr), PCIE-M04 (PRP → MRd 묶음), PCIE-M11 (크레딧), PCIE-M12 (AER), PCIE-M15 (AT, IOMMU), PCIE-A02 (Little's Law), ARCH-M12 · BM-M08 (DMA 와 캐시 일관성), C-B01 (비트 연산), BM-B04 (레지스터 비트 함정)
