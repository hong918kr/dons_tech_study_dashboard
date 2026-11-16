---
id: PCIE-B01
time: 60분
updated: 2026-11-15
---
# PCIE-B01 PCIe 계층 구조: Transaction / Data Link / Physical

## 학습 목표

- PCIe 3계층(Transaction / Data Link / Physical)이 각각 **무엇을 붙이고, 무엇을 검사하고, 어떤 오류를 책임지는지** 바이트 단위로 설명할 수 있다.
- 3DW Memory Read(MRd)·Memory Write(MWr) TLP를 헤더 + 데이터 + ECRC로 직접 인코딩하고, DLL의 12비트 Sequence Number + LCRC, PHY의 Gen1/2 STP/END 프레이밍과 Gen3+ STP 토큰 + 128b/130b 블록까지 감쌀 수 있다.
- 선로 위 비트 수를 계산한다: Gen1/2 = 심볼 × 10, Gen3+ = 블록 × 130.
- 수신기를 PHY → DLL → TL 순서로 구현해서, 1비트 오류가 어느 계층에서 잡히고 어떤 AER 비트(Receiver Error / Bad TLP / Malformed / ECRC)가 켜지는지 말할 수 있다.
- "LCRC가 있는데 ECRC는 왜 필요한가", "Bad TLP 카운터가 오르면 무엇을 보나" 같은 꼬리질문에 근거를 들어 답할 수 있다.

## 1. 문제

Rivian HM 스크린의 "L0가 뭐냐"처럼, 실리콘 검증·SSD FW 면접은 결국 "AER에 Bad TLP가 쌓이는데 어느 계층 문제냐"를 묻는다. 계층마다 무엇을 붙이는지 바이트로 아는 사람만 이 질문에 바로 답한다.

PCIe 패킷 하나는 세 계층을 거치며 양파처럼 감싸진다. 위 계층이 만든 바이트는 아래 계층이 **건드리지 않고** 앞뒤에 자기 필드만 붙인다. 받는 쪽은 바깥 껍질부터 벗기며 검사한다.

| 계층 | 붙이는 것 | 누구 사이의 약속인가 | 대표 오류 → AER 비트 (pci_regs.h) |
|---|---|---|---|
| Transaction Layer (TL) | 헤더 3/4 DW, 데이터(payload), 선택적 ECRC 4B | 요청자 ↔ 완료자 **끝과 끝** (스위치 통과) | Malformed TLP `0x00040000`, ECRC Error `0x00080000` (uncorrectable) |
| Data Link Layer (DLL) | Sequence Number 2B(12비트), LCRC 4B, DLLP(Ack/Nak/FC) | 링크 하나의 **양 끝 포트** | Bad TLP `0x00000040`, Bad DLLP `0x00000080` (correctable, replay로 복구) |
| Physical Layer (PHY) | 프레이밍(STP/END 또는 STP 토큰), 인코딩(8b/10b, 128b/130b), 스크램블, 레인 분배 | 같은 링크의 송수신 회로 | Receiver Error `0x00000001` (correctable) |

```svg
<svg viewBox="0 0 760 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">MRd 16DW (seq 0x123)가 계층을 내려가며 감싸지는 모습 — solution.py --show 실제 값</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="67">Transaction (TL)</text>
    <text x="10" y="122">Data Link (DLL)</text>
    <text x="10" y="177">PHY Gen1/2 8b/10b</text>
    <text x="10" y="232">PHY Gen3+ 128b/130b</text>
  </g>
  <g stroke="currentColor">
    <rect x="250" y="45" width="180" height="34" class="f-acc"/>
    <rect x="430" y="45" width="60" height="34" class="f-mut" stroke-dasharray="4 3"/>
    <rect x="490" y="45" width="50" height="34" class="f-mut" stroke-dasharray="4 3"/>
    <rect x="190" y="100" width="60" height="34" class="f-ok"/>
    <rect x="250" y="100" width="290" height="34" class="f-acc"/>
    <rect x="540" y="100" width="60" height="34" class="f-ok"/>
    <rect x="150" y="155" width="40" height="34" class="f-warn"/>
    <rect x="190" y="155" width="60" height="34" class="f-ok"/>
    <rect x="250" y="155" width="290" height="34" class="f-acc"/>
    <rect x="540" y="155" width="60" height="34" class="f-ok"/>
    <rect x="600" y="155" width="40" height="34" class="f-warn"/>
    <rect x="150" y="210" width="100" height="34" class="f-warn"/>
    <rect x="250" y="210" width="290" height="34" class="f-acc"/>
    <rect x="540" y="210" width="60" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="340" y="67">Header 12B: 00 00 00 10 ...</text>
    <text x="460" y="67">Data</text>
    <text x="515" y="67">ECRC</text>
    <text x="220" y="122">Seq 01 23</text>
    <text x="395" y="122">TLP (12B, 그대로)</text>
    <text x="570" y="122">LCRC 4B</text>
    <text x="170" y="177">STP</text>
    <text x="220" y="177">Seq</text>
    <text x="395" y="177">TLP</text>
    <text x="570" y="177">LCRC</text>
    <text x="620" y="177">END</text>
    <text x="200" y="232">STP 토큰 4B</text>
    <text x="395" y="232">TLP</text>
    <text x="570" y="232">LCRC</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="550" y="67">(MWr / TD=1 일 때)</text>
    <text x="610" y="122">18 B</text>
    <text x="648" y="177">20심볼·200b</text>
    <text x="608" y="232">20B·블록2·260b</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="2 3" fill="none">
    <path d="M250 79 L250 100"/><path d="M430 79 L540 100"/>
    <path d="M190 134 L190 155"/><path d="M600 134 L600 155"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="270">Seq·LCRC = 링크 하나(이웃 포트끼리)에서만 유효 → 스위치마다 벗기고 새로 붙인다.</text>
    <text x="10" y="290">ECRC = 요청자 → 완료자 끝까지 그대로 간다.</text>
    <text x="10" y="310">Gen3+: Seq 12비트는 STP 토큰 안으로 (Len=5 DW, FCRC 포함).</text>
    <text x="10" y="330">Gen3+ 에는 END 가 없다 — 토큰의 길이로 끝을 안다.</text>
  </g>
</svg>
```

이 레슨의 예제 두 개는 Don의 SSD 경험에서 가져왔다.

- **MRd 16 DW**: NVMe SSD(엔드포인트 `01:00.0`, Requester ID `0x0100`)가 호스트 메모리 `0x7FFE_1000`에 있는 SQ 엔트리 64 B를 읽어 온다 (Tag `0x05`, seq `0x123`). SSD 컨트롤러가 명령을 가져오는 첫 동작이다.
- **MWr 1 DW + ECRC**: 호스트(Root Complex, Requester ID `0x0000`)가 BAR0 `0xFE00_0000` + `0x1000` = SQ0 Tail Doorbell에 `1`을 쓴다 (seq `0x124`). 드라이버의 `writel()` 한 줄이 이 posted write다 (PCIE-B11, PCIE-B12 참고).

**모델 정의 (꼭 읽기)**: 헤더 필드 위치·Fmt/Type 값·K 심볼 값·12비트 seq·Length 의미는 스펙 사실이다. 반면 CRC 결과를 선로에 싣는 **비트/바이트 순서**, Gen3 STP 토큰 안의 정확한 비트 배치, Frame CRC 식, DLLP CRC-16의 init/xorout은 이 레슨의 *약속* 이다 (스펙 확인 권장). 그래서 테스트는 "실리콘과 같은 비트"가 아니라 **왕복(round trip), 필드 위치, 오류 검출**만 확인한다. CRC-32 자체는 poly `0x04C11DB7`로 같고, 우리 구현은 zlib(`binascii.crc32`)과 같은 값을 낸다.

### Part A — Transaction Layer: TLP 만들기

- `crc32_pcie(data)`: 비트 단위 CRC-32 (반사형 poly `0xEDB88320`, init/xorout `0xFFFFFFFF`). `binascii.crc32`와 랜덤 200개 비교, 체크 값 `"123456789" → 0xCBF43926`.
- `ecrc_of(tlp)`: 경로 중에 바뀔 수 있는 **variant 비트**(Type[0], EP)를 1로 놓고 CRC-32.
- `make_tlp(kind, req_id, tag, addr, length_dw, payload, ecrc)`: 3DW MRd(1–1024 DW, Length 1024 → 0) / MWr(payload 1–4 DW). First/Last BE 규칙, `ecrc=True`면 TD=1 + ECRC 4 B. 잘못된 인자(비정렬, ≥ 4 GB, 길이) → `ValueError`. 헤더 세부 필드(TC/Attr/4DW/CplD)는 PCIE-B10의 몫이다.
- `tlp_field_names(tlp)`: TLP 바이트마다 필드 이름 — 표의 "field" 열이 된다.

### Part B — Data Link + Physical: 감싸기

- `dll_wrap(tlp, seq)`: `[예약 4b | Seq[11:8]] [Seq[7:0]] + TLP + LCRC(Seq+TLP에 대한 CRC-32)`.
- `phy_gen12_tlp(dll, nullify)`: `STP(K27.7) + DLL 바이트 + END(K29.7)`. 선로 비트 = 심볼 × 10. `nullify=True`면 LCRC 반전 + END 대신 EDB(K30.7).
- `stp_token(length_dw, seq)`, `phy_gen3_tlp(dll)`: 4 B STP 토큰(Length = 토큰+TLP+LCRC의 DW 수, Frame CRC, Frame Parity, Seq) + TLP + LCRC. END 없음.
- `blocks_130(stream)`: x1에서 16 B씩 Data Block(sync `10b`), 남는 자리는 IDL(`00h`). 선로 비트 = 블록 × 130.
- `dllp_ack(seq)`: 대비용 DLLP — `Type 00h, 예약, Seq 12b, CRC-16` 6 B. Gen1/2는 `SDP + 6 + END`, Gen3는 `F0 AC + 6`.
- `annotate_gen12(symbols)`: 심볼마다 `(계층, 필드)` → 바이트 단위 표.

### Part C — 수신기: 벗기면서 검사, 오류 주입

- `unwrap_gen12` / `unwrap_gen3`: 프레이밍(STP/END, 가운데 K 심볼, sync header, STP Frame CRC/Parity, Length 범위, TLP 뒤 IDL)을 보고 → `_check_lcrc`(LCRC, nullified 판정) → `check_tlp`(Length·TD·BE 일관성 = Malformed, ECRC).
- 오류는 `LinkError(layer, code, aer)`로 올리고, 주어진 `receive()`가 계층·AER 비트·조치(Nak/replay, 버림, Recovery)를 돌려준다.
- 주입: ① 프레임의 **모든** 1비트 뒤집기(Gen1/2 180개, Gen3 260개) → 놓치는 것 0개, 계층별 개수 고정 ② 스위치 내부 비트 오류(LCRC는 새로 붙어 정상) → ECRC만 잡는다, ECRC를 끄면 **틀린 데이터가 조용히 들어간다** ③ Malformed 3종 ④ nullified TLP ⑤ 잘린 프레임.

## 2. Problem (English)

> **PCIe layer-wrapping encoder / decoder.**
>
> Using only the Python 3 standard library, model how one memory request travels down the PCIe stack and back up.
>
> 1. `make_tlp(kind, req_id, tag, addr, length_dw=1, payload=b"", ecrc=False) -> bytes` — a 3DW `MRd` (1..1024 DW, Length 0 means 1024)
>    or `MWr` (1..4 DW payload), header in transmit order (byte 0 = Fmt|Type), optional ECRC (CRC-32, variant bits Type[0] and EP
>    treated as 1). Raise `ValueError` for unaligned or ≥ 4 GB addresses and illegal sizes.
> 2. `dll_wrap(tlp, seq) -> bytes` — 12-bit sequence number in 2 bytes + TLP + LCRC (CRC-32 over seq+TLP).
> 3. Physical framing: Gen1/2 `[(is_k, byte)]` = STP + DLL bytes + END (10 wire bits per symbol);
>    Gen3 = 4-byte STP token (11-bit DW length incl. token and LCRC, frame CRC, frame parity, sequence number) + TLP + LCRC, cut into
>    16-byte data blocks with a 2-bit sync header (130 wire bits per block). Also build an Ack DLLP (6 bytes, CRC-16).
> 4. A receiver that unwraps PHY → DLL → TL and raises a layer-tagged error: framing → Receiver Error, LCRC → Bad TLP (Nak),
>    length/BE inconsistency → Malformed TLP, ECRC → ECRC Error. A nullified TLP (EDB + inverted LCRC) is dropped silently.
>
> Example: MRd, ReqID `0x0100`, Tag 5, addr `0x7FFE1000`, 16 DW → TLP `00 00 00 10 01 00 05 FF 7F FE 10 00`;
> with seq `0x123` the Gen1/2 frame is 20 symbols (200 wire bits) and the Gen3 stream is 20 bytes with STP Length = 5 DW.
> Every single-bit flip of either frame must be detected.

## 3. 시작 코드

<!-- code: starter.py -->

```bash
cd lessons/PCIE/PCIE-B01
python3 -I starter.py            # TODO 상태: 2/41 passed, exit 1
python3 -I starter.py --show     # 표도 찍어 보기 (TODO 상태라 비어 있음)
```

실제 출력 (일부):

```text
-- Part A: Transaction Layer --
FAIL  crc32_pcie(b'123456789') == 0xCBF43926 (check value)
PASS  crc32_pcie(b'') == 0
FAIL  crc32_pcie == binascii.crc32 on 200 random buffers
FAIL  MRd 16DW bytes == 00 00 00 10 | 01 00 05 FF | 7F FE 10 00
...
PASS  crc16_dllp == GF(2) polynomial reference on 300 random DLLP bodies
...
FAIL  Gen1/2 round trip MRd: ok, seq 0x123, MRd len 16 addr 0x7FFE1000 req 0x0100  (NotImplementedError: unwrap returned None)
...
2/41 passed
```

> 두 개의 공짜 PASS: `crc32_pcie(b'') == 0`(stub이 0을 돌려서), 그리고 `crc16_dllp`(주어진 코드). CRC는 빈 입력 하나로 검증하면 안 된다 — 체크 값과 독립 구현 비교가 필요하다 (ALGO-M01 참고).

## 4. 힌트

<details>
<summary>힌트 1 — 헤더 바이트는 "보내는 순서"로 쓴다</summary>

DW0의 첫 바이트가 Fmt[2:0](비트 7:5) + Type[4:0](비트 4:0)이다. MRd 3DW = `000 00000` = `0x00`, MWr 3DW = `010 00000` = `0x40`. 각 DW는 big-endian으로 쌓으면 된다: `req_id.to_bytes(2, "big")`, `addr.to_bytes(4, "big")`. 데이터는 메모리 주소 순서 그대로 (doorbell 값 1 = `01 00 00 00`, x86/ARM little-endian 메모리 표현).

</details>

<details>
<summary>힌트 2 — Length와 BE 규칙</summary>

Length는 10비트 DW 수이고 1024는 `0`으로 쓴다(`n & 0x3FF`). byte2의 하위 2비트 = Length[9:8], byte3 = Length[7:0]. First DW BE = `F`(전체 DW), 1 DW 요청이면 Last DW BE는 **반드시 0**, 2 DW 이상이면 `F`. 이 규칙을 어긴 TLP는 Malformed다 (Part C에서 그대로 검사).

</details>

<details>
<summary>힌트 3 — LCRC 범위와 Gen3의 Seq 위치</summary>

LCRC는 Seq 2 B + TLP 전체(헤더·데이터·ECRC)에 대한 CRC다. Gen3에서도 DLL이 만든 18 B를 그대로 쓰되, **앞의 Seq 2 B를 떼어 STP 토큰 B2/B3에 넣는다**. 수신기는 토큰에서 Seq를 꺼내 2 B로 다시 만들어 붙인 뒤 LCRC를 검사한다 — 그래서 Gen1/2와 Gen3의 LCRC 값이 같다.

</details>

<details>
<summary>힌트 4 — STP 토큰 Length</summary>

Length = (토큰 4 B + TLP + LCRC 4 B) / 4 = `1 + (len(dll) - 2) // 4`. MRd 3DW 무데이터 = 1 + 3 + 1 = 5, MWr 1 DW + ECRC = 1 + 3 + 1 + 1 + 1 = 7. 11비트라서 B0 상위 니블에 [3:0], B1 하위 7비트에 [10:4].

</details>

<details>
<summary>힌트 5 — 수신 검사 순서</summary>

바깥부터: PHY(프레임 경계, 심볼 종류, sync, 토큰 CRC) → DLL(LCRC, EDB면 반전 LCRC인지) → TL(Fmt/Type, 길이 = 12 + 데이터 + ECRC, BE 규칙, 마지막에 ECRC). 순서가 바뀌면 같은 비트 오류가 엉뚱한 AER 비트로 보고된다 — 예를 들어 LCRC보다 Length 검사를 먼저 하면 선로 잡음이 "Malformed(uncorrectable)"로 둔갑한다.

</details>

## 5. 풀이

### Part A — TLP 헤더: 바이트 → 필드

`Linux include/uapi/linux/pci_regs.h`는 config 공간만 정의하고 TLP 포맷은 없다. 아래 필드 위치는 PCIe Base Spec의 공통 헤더 그림(DW0)과 메모리 요청 헤더(DW1–2)를 따른다. 세부 필드(TC, Attr, TH, AT, 4DW, CplD)는 PCIE-B10에서 전부 다룬다.

```svg
<svg viewBox="0 0 760 335" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">3DW MRd 헤더 (예제 값) + Gen3 STP 토큰 — 왼쪽이 먼저 나가는 비트 쪽(byte 0 bit 7)</text>
  <g fill="currentColor" text-anchor="middle" opacity=".75">
    <text x="180" y="40">byte 0 (+0)</text><text x="340" y="40">byte 1 (+1)</text>
    <text x="500" y="40">byte 2 (+2)</text><text x="660" y="40">byte 3 (+3)</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="66" font-weight="bold">DW0</text>
    <text x="10" y="84" font-family="monospace">00 00 00 10</text>
    <text x="10" y="126" font-weight="bold">DW1</text>
    <text x="10" y="144" font-family="monospace">01 00 05 FF</text>
    <text x="10" y="186" font-weight="bold">DW2</text>
    <text x="10" y="204" font-family="monospace">7F FE 10 00</text>
    <text x="10" y="256" font-weight="bold">STP</text>
    <text x="10" y="274" font-family="monospace">5F 00 F1 23</text>
  </g>
  <g stroke="currentColor">
    <rect x="100" y="50" width="60" height="26" class="f-acc"/><rect x="160" y="50" width="100" height="26" class="f-acc"/>
    <rect x="260" y="50" width="20" height="26" class="f-mut"/><rect x="280" y="50" width="60" height="26" class="f-mut"/>
    <rect x="340" y="50" width="20" height="26" class="f-mut"/><rect x="360" y="50" width="20" height="26" class="f-mut"/>
    <rect x="380" y="50" width="20" height="26" class="f-mut"/><rect x="400" y="50" width="20" height="26" class="f-mut"/>
    <rect x="420" y="50" width="20" height="26" class="f-warn"/><rect x="440" y="50" width="20" height="26" class="f-warn"/>
    <rect x="460" y="50" width="40" height="26" class="f-mut"/><rect x="500" y="50" width="40" height="26" class="f-mut"/>
    <rect x="540" y="50" width="40" height="26" class="f-ok"/><rect x="580" y="50" width="160" height="26" class="f-ok"/>
    <rect x="100" y="110" width="320" height="26" class="f-acc"/><rect x="420" y="110" width="160" height="26" class="f-mut"/>
    <rect x="580" y="110" width="80" height="26" class="f-ok"/><rect x="660" y="110" width="80" height="26" class="f-ok"/>
    <rect x="100" y="170" width="600" height="26" class="f-acc"/><rect x="700" y="170" width="40" height="26" class="f-mut"/>
    <rect x="100" y="240" width="80" height="26" class="f-ok"/><rect x="180" y="240" width="80" height="26" class="f-warn"/>
    <rect x="260" y="240" width="20" height="26" class="f-warn"/><rect x="280" y="240" width="140" height="26" class="f-ok"/>
    <rect x="420" y="240" width="80" height="26" class="f-warn"/><rect x="500" y="240" width="80" height="26" class="f-acc"/>
    <rect x="580" y="240" width="160" height="26" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="130" y="68">Fmt</text><text x="210" y="68">Type</text>
    <text x="270" y="68">T9</text><text x="310" y="68">TC</text><text x="350" y="68">T8</text>
    <text x="370" y="68">A2</text><text x="390" y="68">LN</text><text x="410" y="68">TH</text>
    <text x="430" y="68">TD</text><text x="450" y="68">EP</text><text x="480" y="68">Attr</text>
    <text x="520" y="68">AT</text><text x="560" y="68">L9:8</text><text x="660" y="68">Length[7:0]</text>
    <text x="260" y="128">Requester ID (Bus | Dev | Fn)</text><text x="500" y="128">Tag</text>
    <text x="620" y="128">Last BE</text><text x="700" y="128">First BE</text>
    <text x="400" y="188">Address[31:2]</text><text x="720" y="188">R</text>
    <text x="140" y="258">Len[3:0]</text><text x="220" y="258">1111b</text>
    <text x="270" y="258">FP</text><text x="350" y="258">Len[10:4]</text>
    <text x="460" y="258">FCRC</text><text x="540" y="258">Seq[11:8]</text><text x="660" y="258">Seq[7:0]</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="130" y="94">000</text><text x="210" y="94">00000</text><text x="340" y="94">0000 0000</text>
    <text x="430" y="94">0</text><text x="450" y="94">0</text><text x="480" y="94">00</text>
    <text x="520" y="94">00</text><text x="560" y="94">00</text><text x="660" y="94">0001 0000 = 16</text>
    <text x="260" y="154">0x0100 = 01:00.0</text><text x="500" y="154">0x05</text>
    <text x="620" y="154">F</text><text x="700" y="154">F</text>
    <text x="400" y="214">0x7FFE1000 (하위 2비트 00)</text><text x="720" y="214">00</text>
    <text x="140" y="284">0101</text><text x="220" y="284">1111</text>
    <text x="270" y="284">0</text><text x="350" y="284">000 0000</text>
    <text x="460" y="284">1111</text><text x="540" y="284">0001</text><text x="660" y="284">0010 0011</text>
  </g>
  <text x="10" y="312" fill="currentColor">STP 토큰: Length=5 DW, FCRC=CRC4(5)=0xF, FP=0, Seq=0x123 — 토큰 안 비트 배치는 이 레슨의 모델 (스펙 확인 권장)</text>
  <text x="10" y="328" fill="currentColor">MWr 1DW+ECRC 이면 DW0 = 40 00 80 01 (Fmt=010, TD=1, Length=1), DW1 BE = 0F (Last BE 0)</text>
</svg>
```

Fmt/Type 조합 (이 레슨에서 쓰는 것과 B10에서 쓸 것):

| Fmt[2:0] | 의미 | Type | TLP | byte 0 |
|---|---|---|---|---|
| `000` | 3DW, 데이터 없음 | `0_0000` | MRd (32비트 주소) | `0x00` |
| `001` | 4DW, 데이터 없음 | `0_0000` | MRd (64비트 주소) | `0x20` (PCIE-B10) |
| `010` | 3DW, 데이터 있음 | `0_0000` | MWr | `0x40` |
| `011` | 4DW, 데이터 있음 | `0_0000` | MWr 64 | `0x60` (PCIE-B10) |

MRd 예제를 한 바이트씩 따라가면:

1. byte0 `00` = Fmt 000(3DW, 무데이터) + Type 00000(메모리 요청) → MRd.
2. byte1 `00` = TC 0, Attr[2](IDO) 0, TH 0. byte2 `00` = TD 0(ECRC 없음), EP 0, RO/NS 0, AT 0, Length[9:8] 00. byte3 `10` → Length 16 DW = 64 B (NVMe SQ 엔트리 크기).
3. byte4–5 `01 00` = Requester ID → Bus 1, Dev 0, Fn 0. **완료(CplD)가 돌아올 주소**가 바로 이 ID다 — 그래서 enumeration(PCIE-B09)에서 버스 번호가 정해지기 전에는 장치가 요청을 보낼 수 없다.
4. byte6 `05` = Tag. 같은 Requester의 미완료 요청 여러 개를 구분한다 (SSD는 Tag를 많이 써서 읽기를 겹친다 — PCIE-A02 성능 모델).
5. byte7 `FF` = Last BE F, First BE F (16 DW 전부). byte8–11 `7F FE 10 00` = 주소.

**ECRC와 variant 비트**: ECRC는 요청자 TL이 만들고 완료자 TL이 검사한다. 그런데 중간 스위치가 합법적으로 바꿀 수 있는 비트가 있다: config Type1→Type0 변환 시 Type[0], 그리고 오류를 발견한 스위치가 켜는 EP(poison). 그래서 이 두 비트는 **항상 1로 놓고** CRC를 계산한다(스펙 확인 권장 — 정의 세부는 Base Spec ECRC 절). 테스트 `ECRC ignores variant bits (EP) but not data`가 이 성질을 확인한다. ECRC 4 B는 TD=1일 때만 붙고, Length에는 **들어가지 않는다** (Length는 데이터 DW만).

### Part B — DLL: Seq + LCRC, PHY: 프레이밍과 인코딩

DLL이 붙이는 6 B는 링크 하나의 신뢰성 장치다. 송신 DLL은 TLP마다 12비트 seq를 매기고, 받는 쪽이 `Ack(seq)`를 줄 때까지 replay 버퍼에 보관한다. LCRC가 틀리면 수신 DLL이 `Nak`를 보내고 송신 DLL이 그 뒤를 다시 보낸다 (연습 2, PCIE-M11). 이 재전송은 소프트웨어가 전혀 모른다 — AER의 correctable 카운터(Bad TLP, Replay Timer Timeout, REPLAY_NUM Rollover)로만 흔적이 남는다.

`python3 -I solution.py --show`의 실제 출력 (Gen1/2, MRd):

```text
== MRd 16DW (SSD 01:00.0 fetches a 64B SQ entry), seq=0x123 ==
-- Gen1/2: 20 symbols x 10 = 200 wire bits --
off  sym    layer  field
  0  K:FB   PHY    STP K27.7
  1  01     DLL    Seq rsvd|[11:8]
  2  23     DLL    Seq[7:0]
  3  00     TL     DW0 Fmt|Type
  4  00     TL     DW0 TC|Attr2|TH
  5  00     TL     DW0 TD|EP|Attr|AT|Len[9:8]
  6  10     TL     DW0 Len[7:0]
  7  01     TL     DW1 ReqID Bus
  8  00     TL     DW1 ReqID Dev|Fn
  9  05     TL     DW1 Tag
 10  FF     TL     DW1 LastBE|FirstBE
 11  7F     TL     DW2 Addr[31:24]
 12  FE     TL     DW2 Addr[23:16]
 13  10     TL     DW2 Addr[15:8]
 14  00     TL     DW2 Addr[7:2]|00
 15  1B     DLL    LCRC B0
 16  57     DLL    LCRC B1
 17  07     DLL    LCRC B2
 18  7E     DLL    LCRC B3
 19  K:FD   PHY    END K29.7
```

**Gen1/2 (8b/10b)**: 바이트 하나가 10비트 코드 하나가 된다. 데이터 바이트는 D 코드, 프레이밍은 데이터에 절대 나오지 않는 K 코드(STP `K27.7`=FBh, END `K29.7`=FDh, SDP `K28.2`=5Ch, EDB `K30.7`=FEh)라서 수신기는 "여기서 TLP가 시작/끝"을 데이터와 헷갈리지 않는다. 대가는 20 % 오버헤드(8비트 → 10비트). 8b/10b 코드 표 자체(러닝 디스패리티)는 이 레슨 범위 밖이다 — 선로 비트 수만 센다.

**Gen3+ (128b/130b)**: 블록 단위로 2비트 sync header를 붙인다 (`10b` = Data Block, `01b` = Ordered Set Block). 오버헤드는 2/130 ≈ 1.5 %. 그런데 이제 K 코드가 없으니 프레임 경계는 **토큰**으로 표시한다. STP 토큰 4 B가 TLP 길이를 들고 다니므로 END가 필요 없다. 길이가 틀리면 프레임 경계 전체가 무너지므로 길이를 Frame CRC + Parity로 따로 보호한다. 그리고 DLL의 Seq 2 B(예약 4비트 포함)를 토큰 안에 넣어 바이트를 아낀다. 실제 출력 (MRd):

```text
-- Gen3: 20 bytes in 2 block(s) x 130 = 260 wire bits (STP Length=5 DW) --
off  byte  layer    field
  0  5F    PHY      STP 1111b|Len[3:0]
  1  00    PHY      FP|Len[10:4]
  2  F1    PHY+DLL  FCRC|Seq[11:8]
  3  23    PHY+DLL  Seq[7:0]
  4  00    TL       DW0 Fmt|Type
...
 16  1B    DLL      LCRC B0
...
blocks: 10 5F 00 F1 23 00 00 00 10 01 00 05 FF 7F FE 10 00 | 10 1B 57 07 7E 00 00 00 00 00 00 00 00 00 00 00 00
```

LCRC `1B 57 07 7E`가 Gen1/2와 **똑같다** — DLL은 세대와 무관하고, PHY가 Seq를 어디에 싣느냐만 다르다. 두 번째 블록의 `00` 12개는 IDL 토큰(빈 자리)이다. 실제 링크에서는 다음 TLP/DLLP가 그 자리를 채운다.

세 프레임의 선로 비트 (모두 `--show` 실제 값, x1):

| 프레임 | Gen1/2 심볼 | Gen1/2 비트 | Gen1 2.5 GT/s 시간 | Gen3 바이트 | Gen3 블록 → 비트 | Gen3 8 GT/s 시간 |
|---|---|---|---|---|---|---|
| MRd 16 DW (무데이터) | 1+2+12+4+1 = 20 | 200 | 80 ns | 4+12+4 = 20 | 2 → 260 | 32.5 ns (패딩 포함) |
| MWr 1 DW + ECRC | 1+2+12+4+4+4+1 = 28 | 280 | 112 ns | 4+12+4+4+4 = 28 | 2 → 260 | 32.5 ns |
| Ack DLLP | SDP+6+END = 8 | 80 | 32 ns | `F0 AC` + 6 = 8 | (다른 것과 블록 공유) | — |

프레이밍+DLL 오버헤드가 두 세대 모두 **8 B**(Gen1/2: STP 1 + Seq 2 + LCRC 4 + END 1, Gen3: 토큰 4 + LCRC 4)라는 점이 재밌다. 효율 차이는 거의 전부 인코딩(10/8 vs 130/128)에서 온다 (연습 5, PCIE-B02).

**DLLP와 대비**: Ack DLLP `00 00 01 23 C7 96` — Type `00h`(Ack), 예약 1 B, `예약 4b | AckNak_Seq_Num 12b`, CRC-16(모델: poly `0x100B`). DLLP는 TL로 올라가지 않는다: 링크 양 끝 DLL끼리만 주고받는 관리 패킷(Ack/Nak, 흐름 제어 크레딧 InitFC/UpdateFC, 전원 관리 PM_*)이고, seq도 LCRC도 없이 자기 CRC-16만 갖는다. 그래서 DLLP가 깨지면 재전송이 아니라 **버린다**(Bad DLLP 카운트) — 다음 Ack/UpdateFC가 어차피 최신 값을 다시 실어 오기 때문이다.

### Part C — 수신기: 어느 계층이 잡나

```svg
<svg viewBox="0 0 760 345" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="pcieb01-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">수신 순서: 바깥 껍질부터 — 실패한 곳의 계층이 AER 비트를 정한다</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="300" height="40" rx="6" class="f-warn"/>
    <rect x="20" y="100" width="300" height="40" rx="6" class="f-ok"/>
    <rect x="20" y="160" width="300" height="40" rx="6" class="f-acc"/>
    <rect x="20" y="220" width="300" height="40" rx="6" class="f-acc"/>
    <rect x="20" y="280" width="300" height="34" rx="6" class="f-mut"/>
    <rect x="400" y="40" width="340" height="40" rx="6" class="f-mut"/>
    <rect x="400" y="100" width="340" height="40" rx="6" class="f-mut"/>
    <rect x="400" y="160" width="340" height="40" rx="6" class="f-mut"/>
    <rect x="400" y="220" width="340" height="40" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="57">PHY: STP/END·가운데 K 심볼 (Gen1/2)</text>
    <text x="30" y="73">Gen3: sync·STP FCRC/FP·Length·IDL</text>
    <text x="30" y="117">DLL: LCRC (Seq+TLP 전체)</text>
    <text x="30" y="133">EDB 이면 LCRC 반전 확인 → nullified</text>
    <text x="30" y="177">TL: Fmt/Type, 길이 = 12 + 데이터 + ECRC</text>
    <text x="30" y="193">1 DW 이면 Last BE = 0</text>
    <text x="30" y="237">TL: ECRC (TD=1 일 때만)</text>
    <text x="30" y="253">요청자 → 완료자 끝과 끝</text>
    <text x="30" y="302">OK: TL 로 전달, Ack(seq) 예약</text>
    <text x="410" y="57">Receiver Error (COR 0x00000001)</text>
    <text x="410" y="73">버림 · Gen3+ 는 LTSSM Recovery</text>
    <text x="410" y="117">Bad TLP (COR 0x00000040)</text>
    <text x="410" y="133">Nak → 송신 replay (SW 모름)</text>
    <text x="410" y="177">Malformed TLP (UNC 0x00040000)</text>
    <text x="410" y="193">버림 + AER 헤더 로그</text>
    <text x="410" y="237">ECRC Error (UNC 0x00080000)</text>
    <text x="410" y="253">버림 + AER 헤더 로그</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#pcieb01-arr)">
    <path d="M170 80 L170 98"/><path d="M170 140 L170 158"/><path d="M170 200 L170 218"/><path d="M170 260 L170 278"/>
    <path d="M320 60 L398 60"/><path d="M320 120 L398 120"/><path d="M320 180 L398 180"/><path d="M320 240 L398 240"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="335" y="54">실패</text><text x="335" y="114">실패</text><text x="335" y="174">실패</text><text x="335" y="234">실패</text>
    <text x="400" y="292">1비트 주입: Gen1/2 180개 → PHY 36 · DLL 144</text>
    <text x="400" y="310">Gen3 260개 → PHY 120 · DLL 140 · 놓침 0</text>
    <text x="10" y="336">LCRC 통과 후 생긴 오류(스위치 내부 SRAM)는 ECRC 만 잡는다 — 끄면 틀린 데이터가 조용히 들어간다.</text>
  </g>
</svg>
```

**1비트 전수 주입 결과 해석** (테스트 고정 값):

- Gen1/2 MRd 프레임 = 20 심볼 × 9가지(8 데이터 비트 + "K/D 종류가 바뀌는" 오류 1개) = 180. STP/END 18개 + 가운데 18 심볼의 K 뒤집힘 18개 = **PHY 36**, 가운데 18 바이트 × 8 = **DLL 144**. 실제 8b/10b에서는 10비트 코드 하나가 깨지면 대부분 "없는 코드" 또는 disparity 오류로 PHY에서 먼저 잡힌다 — 이 모델은 그걸 단순화했다.
- Gen3 MRd = 블록 2 × 130 = 260. sync 4 + 토큰의 STP 표식/Length/FP/FCRC 20 + IDL 패딩 96 = **PHY 120**. 토큰 안의 Seq 12 + TLP·LCRC 128 = **DLL 140**. Seq는 PHY 토큰 안에 있지만 LCRC가 덮으므로 DLL이 잡는다. Length 비트 한 개 오류는 이 모델의 CRC-4 + 패리티가 반드시 잡는다(1비트 오류 다항식 xᵏ는 x⁴+x+1로 나눠떨어지지 않음 — ALGO-M01 참고). 실제 Frame CRC 식은 스펙 확인 권장.

**ECRC가 필요한 이유 (Part C 시나리오)**: 스위치는 들어온 TLP의 LCRC를 검사하고 떼어 낸 뒤, 내부 버퍼를 거쳐 다음 링크로 **새 LCRC**를 붙여 내보낸다. 내부 SRAM에서 비트가 하나 뒤집히면(doorbell 값 1 → 5) 새 LCRC는 이미 틀린 데이터에 대해 계산되므로 다음 링크의 DLL은 "정상"이라고 본다. 테스트 결과:

| 설정 | 결과 |
|---|---|
| ECRC 켬 (TD=1) | `TL ECRC`, AER `0x00080000` — 완료자가 버리고 보고 |
| ECRC 끔 | `ok=True`, payload `05 00 00 00` — **SQ tail = 5로 조용히 들어감**: SSD가 존재하지 않는 명령 4개를 가져가려 한다 |

이것이 SSD·서버 쪽에서 ECRC(+ NVMe 레벨의 End-to-End Protection, PCIE-A04)를 켜는 이유다. 단, ECRC는 AER의 `ECRC Generation/Check Enable`(`PCI_ERR_CAP_ECRC_GENE 0x40`, `PCI_ERR_CAP_ECRC_CHKE 0x100`)을 경로의 양 끝이 지원하고 켜야 의미가 있다. Linux는 기본값으로 BIOS 설정을 따른다(`pci=ecrc=on` 부트 옵션으로 강제할 수 있다).

**nullified TLP**: cut-through 스위치는 TLP 끝까지 받기 전에 다음 링크로 내보내기 시작한다. 뒤늦게 LCRC 오류를 발견하면 이미 보낸 앞부분을 무를 수 없으니, 끝을 **EDB + 반전 LCRC**로 마감해 "이 TLP는 없던 걸로"라고 알린다. 수신기는 Nak도, AER도 없이 버리고 seq도 소비하지 않는다 (테스트 `NULLIFIED, no AER bit`). 원래 링크에서 Nak/replay가 일어나므로 정상 사본이 곧 다시 온다. Gen3+의 nullify는 EDB 토큰을 쓴다 — 형식은 스펙 확인 권장.

**펌웨어 현실**: SSD 컨트롤러에서 이 세 계층은 거의 전부 PCIe 컨트롤러 IP(하드웨어)가 처리한다. FW가 보는 것은 ① LTSSM 상태와 링크 속도/폭(PCIE-B04, PCIE-M07) ② AER 상태/마스크/헤더 로그 레지스터(PCIE-M12) ③ 벤더 디버그 레지스터의 Bad TLP/Bad DLLP/Receiver Error/replay 카운터다. 보드 bring-up에서 "Gen4로는 Bad TLP가 초당 수백 개, Gen3로 내리면 0"이면 TL/FW 버그가 아니라 **채널(SI) 문제**다 — 아이 다이어그램·등화(HWLAB-M09, HWLAB-M12), PRBS/BER 측정(SIV-M06)으로 넘어간다. 반대로 Malformed/ECRC/Unsupported Request처럼 **uncorrectable TL 오류**는 대개 로직·FW 버그(잘못된 Length, BE, 주소 범위)다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py                 # 41/41 passed, exit 0 (0.1초 미만)
python3 -I solution.py --show          # 위 §5의 계층 표 출력 + 테스트
python3 -I -W error -X dev solution.py # 경고 0
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 헤더 DW를 little-endian으로 씀 (`(0x10).to_bytes(4, "little")`) | TLP 헤더는 byte 0(Fmt/Type)부터 나간다. 첫 바이트가 Length가 되어 Malformed | DW마다 `to_bytes(..., "big")` |
| Length 1024를 그대로 10비트에 넣음 (`1024 & 0x3FF` 생각 안 함) | 0x400은 11비트 → byte2에 비트가 넘쳐 TD 등을 침범 | `lf = n & 0x3FF` (0 = 1024), 디코드에서 `or 1024` |
| 1 DW 요청에 Last BE = `F` | 규칙 위반 → 완료자가 Malformed 처리 | `last_be = 0xF if n > 1 else 0` |
| ECRC를 Length에 포함 | Length는 데이터 DW만, ECRC는 TD 비트로 존재를 알린다 | 길이 검사 = `12 + 4*Length + (4 if TD)` |
| ECRC를 바이트 그대로 CRC | 스위치가 EP/Type[0]을 바꾸면 정상 TLP가 ECRC 오류 | variant 비트를 1로 놓고 계산 |
| LCRC를 TLP에만 계산 | Seq가 깨져도 못 잡음 → 순서 꼬임 | `crc32(seq2 + tlp)` |
| Gen3에서 LCRC를 토큰 바이트 포함해 계산 | Length/FCRC는 PHY 소유. 같은 DLL 프레임이 세대마다 다른 LCRC를 갖게 됨 | 토큰에서 Seq만 꺼내 2 B로 복원 후 LCRC |
| 수신에서 TL 검사를 LCRC보다 먼저 | 선로 잡음이 Malformed(uncorrectable)로 보고돼 시스템이 죽을 수 있음 | PHY → DLL → TL 순서 |
| EDB 프레임을 Bad TLP로 처리 | nullified TLP에 Nak를 보내 불필요한 replay + 잘못된 AER | EDB + 반전 LCRC면 조용히 버림 |
| Gen3 선로 비트를 `bytes × 10`으로 계산 | 8b/10b 습관. Gen3는 블록당 130비트 | `blocks × 130` (연속이면 `bytes × 8 × 130/128`) |

## 8. 면접 Q&A

<details>
<summary>Q1. PCIe 세 계층의 책임을 한 문장씩 말해 보세요.</summary>

TL: 읽기/쓰기/완료/메시지를 TLP로 만들고, 주소·ID 라우팅, 순서 규칙(PCIE-B11), 흐름 제어 크레딧 소비, 선택적 ECRC. DLL: 링크 하나 위의 신뢰 전송 — seq 번호, LCRC, Ack/Nak와 replay, 크레딧을 알리는 DLLP. PHY: 비트를 선로에 싣기 — 프레이밍, 8b/10b 또는 128b/130b 인코딩, 스크램블, 레인 분배/디스큐, 링크 훈련(LTSSM).
꼬리질문: "그럼 LTSSM은 어느 계층?" → PHY의 Logical Sub-block. DLL은 PHY가 `LinkUp`을 알려야 DL_Init을 시작하고, 끝나면 `DL_Active`(lspci `LnkSta: DLActive+`)가 된다.

</details>

<details>
<summary>Q2. LCRC가 있는데 ECRC는 왜 필요한가요? 기본으로 켜져 있나요?</summary>

LCRC는 **링크 하나**만 보호한다. 스위치·Root Complex는 LCRC를 검사하고 떼어 낸 뒤 새 LCRC를 붙이므로, 장치 **내부**(버퍼 SRAM, 데이터 경로)에서 생긴 오류는 새 LCRC가 "정상"으로 덮어 버린다. ECRC는 요청자 TL이 만들어 완료자 TL이 검사하므로 경로 전체를 덮는다 (이 레슨 Part C에서 ECRC를 끄면 doorbell 1이 5로 조용히 들어간다). 기본 활성화는 아니다: AER capability의 ECRC Generation/Check Enable을 양 끝이 켜야 하고, 지원하지 않는 장치도 많다. 그래서 스토리지는 NVMe End-to-End Protection(PI, PCIE-A04)처럼 상위 계층 보호를 더한다.
꼬리질문: "EP(poison) 비트와의 차이?" → EP는 "이 데이터가 나쁘다는 걸 **알고** 보낸다"는 표시(예: 메모리 ECC uncorrectable을 읽어 완료로 보낼 때). ECRC는 모르게 깨진 것을 잡는다. EP가 variant 비트라 ECRC 계산에서 빠지는 이유가 이것이다.

</details>

<details>
<summary>Q3. 보드 bring-up 중 AER에 Bad TLP / Receiver Error가 계속 쌓입니다. 무엇부터 보나요?</summary>

correctable이고 DLL replay로 복구되므로 기능은 되지만 **성능이 떨어지고, 언젠가 REPLAY_NUM Rollover(`0x100`)·Replay Timer Timeout(`0x1000`) → 재훈련 → 링크 다운**으로 간다. 순서: ① `lspci -vvv`로 LnkSta 속도/폭이 LnkCap과 같은지(PCIE-B04, PCIE-B06) ② 속도를 한 단계 내려(Gen4 → Gen3) 오류가 사라지는지 — 사라지면 채널/SI 문제 ③ 레인 마진(Lane Margining at Receiver, Gen4+), 아이 측정, 등화 프리셋(HWLAB-M09/M12) ④ 특정 레인만 문제인지(폭을 줄여 보기), 레퍼런스 클럭(SSC 설정 불일치), 전원 노이즈 ⑤ LTSSM Recovery 진입 횟수. Receiver Error가 먼저 늘고 Bad TLP가 따라오면 PHY 단 신호 문제일 가능성이 크다.

</details>

<details>
<summary>Q4. 12비트 sequence number와 replay 버퍼는 어떻게 동작하나요? wrap은?</summary>

송신 DLL은 `NEXT_TRANSMIT_SEQ`를 TLP마다 1씩 올리고(mod 4096) TLP를 replay 버퍼에 복사한다. 수신 DLL은 `NEXT_RCV_SEQ`와 같고 LCRC가 맞으면 받아들이고 Ack를 예약한다. `Ack(n)`은 "n까지 전부 받음"이라 송신은 n 이하를 버퍼에서 지우고, `Nak(n)`은 "n까지는 받았고 그다음부터 다시"라 n 이하를 지우고 나머지를 순서대로 재전송한다. 비교는 mod 4096 반 바퀴 규칙(앞섬 < 2048)으로 한다 — 연습 2에서 4094, 4095, 0, 1이 걸친 wrap을 테스트한다. 미확인 TLP가 2048개를 넘으면 안 되므로 송신은 그 전에 멈춘다(스펙 확인 권장). 같은 TLP를 4번 replay해도 안 되면 REPLAY_NUM Rollover → PHY에 재훈련 요청.
꼬리질문: "Ack DLLP가 깨지면?" → Bad DLLP로 버린다. 다음 Ack가 더 큰 seq를 실어 오면 그걸로 충분하다. 계속 안 오면 Replay Timer가 만료돼 재전송.

</details>

<details>
<summary>Q5. Gen3에서 END 심볼이 없어진 이유와, STP 토큰의 Length가 깨지면 어떻게 되나요?</summary>

128b/130b는 블록마다 2비트 sync만 있고 K 코드가 없어서 "데이터에 절대 안 나오는 끝 표식"을 만들 수 없다. 대신 STP 토큰이 TLP 길이를 미리 알려 준다. 그래서 Length가 틀리면 다음 토큰 위치까지 전부 어긋나므로 Length를 Frame CRC(4비트) + Frame Parity로 따로 보호하고, 틀리면 TLP 하나를 버리는 게 아니라 **프레이밍 오류 → Receiver Error → LTSSM Recovery**로 스트림을 다시 맞춘다. 덤으로 Seq를 토큰에 넣어 오버헤드를 Gen1/2와 같은 8 B로 유지했다. 인코딩 효율은 8b/10b 80 % → 128b/130b 98.5 %라서 Gen3는 8 GT/s로 5 GT/s(Gen2)의 약 2배 대역폭을 낸다 (PCIE-B02).

</details>

<details>
<summary>Q6. DLLP는 TLP와 무엇이 다른가요? 왜 DLLP는 재전송하지 않나요?</summary>

DLLP는 6 B 고정(Type 1 B + 내용 3 B + CRC-16 2 B), seq 번호가 없고, TL로 올라가지 않는다. 종류: Ack/Nak, InitFC1/2·UpdateFC(Posted/Non-Posted/Completion 헤더·데이터 크레딧, PCIE-M11), PM_Enter_L1 같은 전원 관리, 벤더용. 내용이 "현재 상태 스냅숏"이라 하나를 잃어도 다음 것이 최신 값을 다시 실어 온다 — 그래서 replay 대신 버리고 Bad DLLP만 센다. 연습 4에서 Ack 프레임 72개 1비트 오류가 FRAMING 24 / BAD_DLLP 48로 전부 잡히는 것을 확인한다.

</details>

<details>
<summary>Q7. Gen6에서는 이 그림이 어떻게 바뀌나요?</summary>

Gen6(64 GT/s, PAM4)는 **FLIT 모드**가 기본이다: 256 B 고정 크기 flit 안에 TLP 바이트, DLP(DLL 정보), CRC, FEC를 담는다. TLP마다 붙던 LCRC/seq와 STP/END 프레이밍이 사라지고, 재전송 단위가 flit이 된다. PAM4는 원시 BER이 높아서 가벼운 FEC로 대부분 고치고, CRC로 검출해 flit 단위로 재전송한다. 세부 바이트 분할은 스펙 확인 권장 — PCIE-B02에서 대역폭 모델로 다룬다. 면접 포인트: "계층의 책임(신뢰 전송, 프레이밍)은 그대로이고 **단위**가 TLP에서 flit으로 바뀌었다."

</details>

<details>
<summary>Q8. (Rivian 실제 질문) "L0가 뭐죠?" — 이 레슨의 계층과 연결해서.</summary>

L0는 LTSSM의 정상 동작 상태다. Detect → Polling(비트/심볼 락, 훈련 시퀀스) → Configuration(폭·레인 번호·링크 번호 합의) → L0. L0에서야 PHY가 DLL에 LinkUp을 주고, DLL이 InitFC를 교환해 DL_Active가 된 뒤에야 TLP(이 레슨의 MRd/MWr)가 흐른다. 오류가 많으면 L0 ↔ Recovery를 오가고(속도 변경·등화도 Recovery를 거친다), 절전은 L0s/L1(ASPM, PCIE-M09). 상태 머신 전체는 PCIE-M07.
꼬리질문: "lspci로 어떻게 확인?" → `LnkSta: Speed 16GT/s, Width x4` 와 `DLActive+`, 훈련 중이면 `TrErr-/Train+` (PCIE-B04, PCIE-B06).

</details>

## 9. 추가 연습

### 연습 1 — Gen3 x4 레인 스트라이핑

`lane_blocks(stream, lanes)`: 바이트 i를 레인 `i % lanes`에 보내고, 레인마다 자기 128b/130b 블록을 만든다. `destripe`로 되돌리고, 8 GT/s에서 걸리는 시간을 구한다. MWr 4 DW(36 B 스트림)는 x1 3블록 = 48.75 ns, x4 레인당 1블록 = 16.25 ns.

<details>
<summary>답 보기</summary>

스트림을 `16 × lanes`의 배수까지 IDL로 채우고 `padded[ln::lanes]`로 레인별 바이트를 뽑아 16 B씩 블록으로 만든다. 레인은 병렬이므로 시간 = 레인당 블록 수 × 130 / GT/s. 실제 링크에서는 레인마다 sync header가 같은 시각에 나가고, 수신기가 레인 간 지연을 맞추는 **디스큐(deskew)**를 한다. 폭이 x4에서 x2로 떨어지면(링크 훈련 문제) 같은 TLP가 블록을 두 배 써서 시간이 두 배다 — `LnkSta Width x2 (downgraded)`가 성능 반토막인 이유. 실제 스펙에는 "STP 토큰은 레인 0에서 시작" 같은 정렬 규칙이 더 있다 (스펙 확인 권장).

</details>

### 연습 2 — 12비트 seq와 Ack/Nak replay 버퍼

`seq_newer(a, b)`, `on_ack(buf, s)`, `on_nak(buf, s)`를 구현하고 wrap(4094, 4095, 0, 1)에서 Ack(4095) → [0, 1] 남음, Nak(0) → [1] 재전송, 중복된 오래된 Ack(4090) → 아무것도 안 지움을 확인한다.

<details>
<summary>답 보기</summary>

`seq_newer(a, b) = 0 < (a - b) & 0xFFF < 2048`. Ack는 "s보다 뒤인 것만 남김", Nak는 Ack와 같이 지운 뒤 남은 것 전부를 순서대로 재전송한다. `a > b` 같은 일반 비교는 4095 → 0 wrap에서 틀린다 — TCP seq 비교, NVMe CQ Phase Tag(PCIE-M01)와 같은 종류의 함정이다. 실제 DLL은 범위를 벗어난 Ack seq를 DLLP 프로토콜 오류로 볼 수도 있다 (PCIE-M11에서 정확히).

</details>

### 연습 3 — LCRC를 residue로 검사

CRC를 LSB 바이트부터 붙였다면, 데이터 + CRC 전체의 CRC-32는 데이터와 무관하게 상수 `0x2144DF1C`가 된다. `lcrc_ok_residue(dll)`를 만들고 비교 방식과 결과가 같은지 40 프레임 + 1/3 비트 뒤집기로 확인한다.

<details>
<summary>답 보기</summary>

`crc32_pcie(dll) == 0x2144DF1C`. 하드웨어 CRC 검사기가 "마지막 4 B를 따로 떼어 비교"하지 않고 **스트림 끝에서 레지스터 값이 매직 상수인지**만 보는 이유다 — 몇 바이트 뒤에 끝날지 몰라도(Gen3) 같은 회로로 검사된다. 이 성질은 우리 모델이 CRC를 LSB 바이트부터 붙이기로 정했기 때문에 성립한다. 실리콘의 정확한 매핑은 스펙 확인 권장 (CRC 레지스터 비트 순서 맞추기는 COMM-B09 참고).

</details>

### 연습 4 — Gen1/2 DLLP 수신기

`rx_dllp_gen12(symbols)` → `("ACK"|"NAK", seq)`. 프레이밍 오류 → Receiver Error, CRC-16 불일치나 모르는 Type → Bad DLLP(`0x80`). Ack 프레임의 72개 1비트 오류가 FRAMING 24 / BAD_DLLP 48로 전부 잡혀야 한다.

<details>
<summary>답 보기</summary>

길이 8, 첫 심볼 SDP, 끝 END, 가운데 K 없음 → 아니면 FRAMING. 그다음 `crc16_dllp(d[:4]) == d[4:6]`(big-endian, 모델) → 아니면 BAD_DLLP. 24 = SDP·END 각 9 + 가운데 6 심볼의 K 뒤집힘 6. 48 = 6 바이트 × 8. CRC-16(최소 해밍 거리 ≥ 3 수준)은 1비트 오류를 모두 잡는다. Bad DLLP는 버리기만 하고 Nak를 보내지 않는다 (Q6).

</details>

### 연습 5 — 프레이밍 오버헤드 표

MWr 하나의 선로 비트와 효율(payload 비트 / 선로 비트)을 payload 4–512 B에 대해 Gen1/2와 Gen3로 계산한다. Gen3는 연속 스트림 가정(블록 반올림 없음, `× 130/128`). 인코더의 실제 스트림 길이와 공식이 일치하는지도 확인한다.

<details>
<summary>답 보기</summary>

Gen1/2 = `(1 + 2 + 12 + P + 4 + 1) × 10`, Gen3 = `(4 + 12 + P + 4) × 8 × 130/128`. 실제 출력 (`python3 -I practice.py --table`):

```text
payload  Gen1/2 bits  eff     Gen3 bits  eff
    4B         240   13.3%      195.0   16.4%
   64B         840   61.0%      682.5   75.0%
  128B        1480   69.2%     1202.5   85.2%
  256B        2760   74.2%     2242.5   91.3%
  512B        5320   77.0%     4322.5   94.8%
```

4 B doorbell 쓰기는 효율이 13–16 %밖에 안 된다 — MMIO 레지스터 접근은 대역폭이 아니라 **지연과 횟수**가 문제다(드라이버가 doorbell을 묶어 쓰는 이유). 큰 payload에선 고정 20 B(헤더 12 + 프레이밍/DLL 8)가 희석되어 인코딩 효율 쪽으로 다가간다. MPS가 128 B인 시스템과 256 B인 시스템의 차이가 여기서 나온다. 여기에 DLLP(Ack/UpdateFC)와 SKP Ordered Set 오버헤드까지 더한 본격 모델은 PCIE-B02.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py            # 19/19 passed, exit 0
python3 -I practice.py --table    # 연습 5 표 + 테스트
```

## 10. 핵심 정리

- TL = TLP(헤더 3/4 DW + 데이터 + 선택 ECRC), 끝과 끝. DLL = Seq 12비트(2 B) + LCRC 4 B + DLLP, 링크 하나. PHY = 프레이밍 + 인코딩 + 레인, 선로.
- 3DW 헤더 byte 0 = Fmt|Type (MRd `0x00`, MWr `0x40`), Length는 데이터 DW(0 = 1024), 1 DW면 Last BE = 0, ECRC는 TD로 표시하고 Length에 안 들어간다.
- Gen1/2: `STP(K27.7) + Seq + TLP + LCRC + END(K29.7)`, 심볼 × 10비트. Gen3+: `STP 토큰 4 B(Length·FCRC·FP·Seq) + TLP + LCRC`, END 없음, 16 B 블록 × 130비트. 프레이밍+DLL 오버헤드는 둘 다 8 B.
- 수신은 PHY → DLL → TL 순서: 프레이밍 → Receiver Error, LCRC → Bad TLP(Nak/replay), 길이·BE → Malformed, ECRC → ECRC Error. 순서를 바꾸면 잡음이 uncorrectable로 둔갑한다.
- LCRC는 링크마다 새로 붙는다 → 장치 내부 오류는 ECRC(또는 NVMe PI)만 잡는다. ECRC variant 비트 = Type[0], EP.
- nullified TLP = EDB + 반전 LCRC → 조용히 버림(Nak·AER 없음). DLLP는 seq 없음 → 깨지면 버림(Bad DLLP).
- Bad TLP/Receiver Error 증가 = 대개 SI(속도 내려서 비교), Malformed/ECRC/UR = 대개 로직·FW.

## 참고자료

- PCI-SIG, *PCI Express Base Specification* — Transaction Layer(TLP 공통 헤더, ECRC), Data Link Layer(Seq/LCRC, Ack/Nak, DLLP), Physical Layer(8b/10b 프레이밍, 128b/130b 블록·STP/SDP/IDL/EDB 토큰): <https://pcisig.com/specifications>
- Mike Jackson, Ravi Budruk, *PCI Express Technology 3.0* (MindShare) — 2장(아키텍처 개요), 5장(TLP), 9–10장(DLLP, Ack/Nak), 11–12장(Gen1/2·Gen3 PHY 논리 블록)
- Linux `include/uapi/linux/pci_regs.h` — AER 비트 `PCI_ERR_COR_RCVR/BAD_TLP/BAD_DLLP/REP_ROLL/REP_TIMER`, `PCI_ERR_UNC_MALF_TLP/ECRC`, `PCI_ERR_CAP_ECRC_*`: <https://github.com/torvalds/linux/blob/master/include/uapi/linux/pci_regs.h>
- Linux PCI 문서 — AER 처리(`Documentation/PCI/pcieaer-howto.rst`), 부트 옵션 `pci=ecrc=`: <https://docs.kernel.org/PCI/index.html>
- Python `binascii.crc32` (CRC-32 교차 검증): <https://docs.python.org/3/library/binascii.html>
- 관련 레슨: ALGO-M01/M02(CRC 원리·가속), COMM-B08/B09(CRC 검출 능력·HW CRC 맞추기), SIV-M06(PRBS/BER), HWLAB-M09/M12(아이·등화), PCIE-B02(대역폭), PCIE-B10(TLP 헤더 전체), PCIE-B11(ordering), PCIE-M07(LTSSM), PCIE-M11(흐름 제어·replay), PCIE-M12(AER)
