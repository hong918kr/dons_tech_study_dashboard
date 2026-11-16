---
id: PCIE-B02
time: 50분
updated: 2026-11-15
---
# PCIE-B02 링크 속도·폭과 실효 대역폭 계산: Gen1–Gen6

## 학습 목표

- **GT/s → raw Gb/s → 인코딩 후 Gb/s → GB/s → GiB/s** 를 단위를 섞지 않고 계산하고, Gen1–Gen6 × x1–x16 표를 머릿속에서 다시 만들 수 있다 (Gen3 x1 ≈ 985 MB/s, Gen4 x4 ≈ 7.88 GB/s).
- TLP 하나에 붙는 선로 오버헤드(프레이밍 + Sequence Number + 헤더 12/16B + LCRC (+ECRC))와 DLLP 몫을 **명시한 모델** 로 세우고, MPS 128/256/512 별 실효 처리량을 정확한 분수로 계산한다.
- "Gen4 x4 NVMe 이론 최대 순차 읽기는?" 에 **방향까지** 맞게 답한다 — 읽기 데이터는 SSD가 올리는 MWr(MPS 지배), 쓰기 데이터는 호스트가 돌려주는 CplD(MRRS·RCB·완료 크기 지배).
- 측정 처리량이 어느 (gen, width) 상한에 붙어 있는지 역산해 "링크가 x2/Gen3로 떨어졌나?" 를 의심하고, 그 다음 확인할 레지스터(LnkSta, DevCtl.MPS)를 말할 수 있다.

## 1. 문제

브링업·검증·SSD 펌웨어 면접에서 "이 링크로 몇 GB/s 나와야 정상인가?" 를 30초 안에 숫자로 못 대면, 벤치 결과가 정상인지 링크가 다운트레이닝된 건지 판단할 근거가 없다 — 이 레슨은 그 숫자를 손으로 유도하는 계산기다.

Don이 SSD 펌웨어를 할 때 본 "Gen4 x4 드라이브는 왜 다 7 GB/s 언저리에서 멈추나?" 의 답이 여기 있다. NAND 병렬도가 아니라 **PCIe 링크 자체가 천장** 이다. 천장은 세 겹으로 깎인다:

1. **물리 계층 인코딩**(encoding) — Gen1/2 의 8b/10b 는 20%를, Gen3–5 의 128b/130b 는 1.54%를 먹는다. Gen6 는 PAM4 신호에 1b/1b(인코딩 손실 없음)지만 256바이트 **FLIT** 안에 CRC·FEC 가 들어간다.
2. **TLP 오버헤드** — 데이터 256바이트를 보내려면 헤더 16바이트와 프레이밍·Seq·LCRC 8바이트가 같이 간다. 그래서 MPS(Max Payload Size)가 클수록 효율이 좋다.
3. **DLLP·SKP** — Ack/UpdateFC 같은 DLLP, 클럭 보정용 SKP ordered set 이 선로 시간을 조금 더 먹는다.

```svg
<svg viewBox="0 0 740 305" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">TLP 하나가 선로에서 차지하는 바이트 (이 레슨의 모델)</text>
  <text x="10" y="62" fill="currentColor" font-weight="bold">Gen1/2</text>
  <g stroke="currentColor">
    <rect x="100" y="44" width="34" height="28" class="f-warn"/>
    <rect x="134" y="44" width="46" height="28" class="f-warn"/>
    <rect x="180" y="44" width="96" height="28" class="f-mut"/>
    <rect x="276" y="44" width="230" height="28" class="f-acc"/>
    <rect x="506" y="44" width="50" height="28" class="f-mut" stroke-dasharray="4 3"/>
    <rect x="556" y="44" width="60" height="28" class="f-warn"/>
    <rect x="616" y="44" width="34" height="28" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="117" y="63">STP</text><text x="157" y="63">Seq</text><text x="228" y="63">헤더</text>
    <text x="391" y="63">데이터 (≤ MPS)</text><text x="531" y="63">ECRC</text><text x="586" y="63">LCRC</text><text x="633" y="63">END</text>
    <text x="117" y="88">1</text><text x="157" y="88">2</text><text x="228" y="88">12 / 16</text>
    <text x="391" y="88">0 … MPS</text><text x="531" y="88">(4)</text><text x="586" y="88">4</text><text x="633" y="88">1</text>
  </g>
  <text x="660" y="63" fill="currentColor" font-size="12">8b/10b</text>
  <text x="10" y="142" fill="currentColor" font-weight="bold">Gen3–5</text>
  <g stroke="currentColor">
    <rect x="100" y="124" width="80" height="28" class="f-warn"/>
    <rect x="180" y="124" width="96" height="28" class="f-mut"/>
    <rect x="276" y="124" width="230" height="28" class="f-acc"/>
    <rect x="506" y="124" width="50" height="28" class="f-mut" stroke-dasharray="4 3"/>
    <rect x="556" y="124" width="60" height="28" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="140" y="143">STP 토큰</text><text x="228" y="143">헤더</text><text x="391" y="143">데이터 (≤ MPS)</text>
    <text x="531" y="143">ECRC</text><text x="586" y="143">LCRC</text>
    <text x="140" y="168">4 (Len·Seq 포함)</text><text x="228" y="168">12 / 16</text><text x="391" y="168">0 … MPS</text>
    <text x="531" y="168">(4)</text><text x="586" y="168">4</text>
  </g>
  <text x="626" y="136" fill="currentColor" font-size="12">128b/130b</text>
  <text x="626" y="152" fill="currentColor" font-size="12">END 없음</text>
  <text x="10" y="222" fill="currentColor" font-weight="bold">Gen6 FLIT</text>
  <g stroke="currentColor">
    <rect x="100" y="204" width="380" height="28" class="f-acc"/>
    <rect x="480" y="204" width="60" height="28" class="f-mut"/>
    <rect x="540" y="204" width="70" height="28" class="f-warn"/>
    <rect x="610" y="204" width="60" height="28" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="290" y="223">TLP 바이트 (헤더+데이터, 여러 TLP 이어 붙임)</text>
    <text x="510" y="223">DLP</text><text x="575" y="223">CRC</text><text x="640" y="223">FEC</text>
    <text x="290" y="248">236</text><text x="510" y="248">6</text><text x="575" y="248">8</text><text x="640" y="248">6</text>
  </g>
  <text x="680" y="223" fill="currentColor" font-size="12">= 256B</text>
  <text x="10" y="274" fill="currentColor" font-size="12">Gen1–5: TLP당 고정 오버헤드 = 헤더 + 8B (ECRC 켜면 +4).</text>
  <text x="10" y="292" fill="currentColor" font-size="12">Gen6: TLP당 프레이밍·Seq·LCRC 없음, flit 손실 236/256 (바이트 분할은 스펙 확인 권장).</text>
</svg>
```

### Part A — GT/s에서 GB/s까지 (인코딩만)

- `encoding(gen)` → `(이름, 효율)`: Gen1/2 `"8b/10b"` 8/10, Gen3–5 `"128b/130b"` 128/130, Gen6 `"1b/1b PAM4 FLIT"` 1.
- `link_bw(gen, width)` → `gt_s`, `raw_gbps`, `enc_gbps`, `GBps`, `GiBps`, `tlp_GBps`(Gen6 는 × 236/256). 모든 값은 `fractions.Fraction` — 테스트가 `Gen4 x4 == 512/65` 처럼 **정확히** 비교한다.
- gen 1..6, width ∈ {1, 2, 4, 8, 16} 밖이면 `ValueError`. 30개 조합 전체에 대해 "폭은 정확히 선형", "Gen4/Gen3 = Gen5/Gen4 = 2, Gen3/Gen2 = 128/65" 를 검사.

### Part B — TLP·DLLP 오버헤드, MPS, 읽기 완료 경로

- `tlp_overhead(gen, hdr=16, ecrc=False)` — 그림의 모델 그대로. hdr ∉ {12, 16} → `ValueError`.
- `tlp_efficiency(gen, payload, hdr, ecrc, dllp_per_tlp)` — `payload / (payload + overhead + dllp_per_tlp × 8)`. DLLP 비율은 **가정**(기본 TLP 4개당 1개)이고 인자로 바꿀 수 있다.
- `effective_write(gen, width, mps, ...)` — MPS 꽉 찬 MWr 스트림의 GB/s. MPS ∉ {128 … 4096} → `ValueError`.
- `write_tlps(addr, nbytes, mps)` — 버퍼를 MPS 정렬 경계에서 자른 TLP 크기 목록 (정렬 안 된 버퍼는 TLP가 하나 더 생긴다).
- `effective_read(gen, width, mrrs, cpl_bytes, mps, rcb=64, ...)` — MRd 하나(≤ MRRS)에 CplD 여러 개(3DW 헤더, `cpl_bytes` 씩)가 돌아오는 경로. CplD 크기는 RCB(Read Completion Boundary) 배수, ≤ MPS, ≤ MRRS 여야 하고 아니면 `ValueError`. 반대 방향을 MRd가 얼마나 쓰는지(`req_link_share`)도 낸다.

### Part C — 면접 질문과 브링업 진단

- `nvme_seq(gen, width, op, mps, mrrs, cpl_bytes)` — `op='read'` 는 SSD→호스트 **MWr upstream**, `op='write'` 는 호스트→SSD **CplD downstream**. "Gen4 x4 NVMe 이론 최대 순차 읽기" 에 답하고 실제 드라이브 스펙 시트 숫자와 비교한다.
- `link_suspects(measured, cap_gen, cap_width)` — "Gen4 x4 SSD 가 벤치에서 3.45 GB/s 에 정체" 같은 측정값을 받아, cap 이하 (gen, width) 중 상한에 85–100%로 붙어 있는 조합을 돌려준다. 3.45 → `[Gen3 x4, Gen4 x2]`: 처리량만으로는 둘을 못 가른다 → LnkSta 를 읽어야 한다 (PCIE-B04/B06).

## 2. Problem (English)

> **PCIe effective bandwidth calculator.** Using only the Python 3 standard library and exact rational arithmetic
> (`fractions.Fraction`), all figures **per direction**:
>
> 1. `link_bw(gen, width) -> dict` for Gen1–Gen6 (2.5/5/8/16/32/64 GT/s per lane) and x1/x2/x4/x8/x16:
>    raw Gb/s, Gb/s after line encoding (8b/10b for Gen1–2, 128b/130b for Gen3–5, 1b/1b PAM4 + 256-byte FLIT for Gen6),
>    GB/s (10^9), GiB/s (2^30), and the byte rate left for TLPs (Gen6: × 236/256). Reject unknown gen/width.
> 2. `tlp_overhead(gen, hdr, ecrc)`, `tlp_efficiency(...)`, `effective_write(gen, width, mps, hdr=16, ecrc=False, dllp_per_tlp=1/4)`.
>    State your per-TLP model (framing + sequence number + header 12/16 + LCRC 4 (+ECRC 4); Gen6 FLIT has none of the
>    per-TLP framing/seq/LCRC). MPS must be a power of two in 128..4096.
> 3. `effective_read(gen, width, mrrs, cpl_bytes, mps, rcb)`: Memory Reads of MRRS bytes answered by Completions with
>    Data of `cpl_bytes` each (3DW header). Validate `cpl_bytes` against RCB, MPS and MRRS.
> 4. Answer: *"What is the theoretical maximum sequential read throughput of a Gen4 x4 NVMe SSD?"* — and explain which
>    TLP type carries the data in each direction.
>
> Example: `link_bw(4, 4)["GBps"] == Fraction(512, 65)` (≈ 7.877 GB/s); `effective_write(4, 4, 256) ≈ 7.151 GB/s`;
> `link_suspects(3.45, 4, 4) == [(3, 4), (4, 2)]`.
>
> Follow-ups: Why did Gen3 only raise the bit rate to 8 GT/s yet nearly double throughput? Why is DMA *reading* host
> memory often slower than DMA *writing* it? Does a bigger MPS always help?

## 3. 시작 코드

<!-- code: starter.py -->

```bash
cd lessons/PCIE/PCIE-B02
python3 -I starter.py            # TODO 상태: 2/41 passed, exit 1
python3 -I starter.py --tables   # 표는 "(표 생략: ...)" 로 건너뛰고 테스트만 돈다
```

실제 출력 (일부):

```text
-- Part A: GT/s → 인코딩 → GB/s --
FAIL  encoding(1) == ('8b/10b', 8/10)
FAIL  encoding(4) == ('128b/130b', 128/130)
FAIL  encoding(6) efficiency == 1 (PAM4 1b/1b)  (TypeError: 'NoneType' object is not subscriptable)
FAIL  Gen1 x1 == 0.25 GB/s exactly (250 MB/s)  (TypeError: 'NoneType' object is not subscriptable)
...
PASS  bad gen/width -> ValueError
-- Part B: TLP/DLLP 오버헤드, MPS, 읽기 완료 --
FAIL  tlp_overhead Gen1 hdr16 == 24, Gen2 hdr12 == 20
...
PASS  suspects(6.9) == [] (near full ceiling, no downgrade)
FAIL  suspects(1.7) == [Gen2 x4, Gen3 x2, Gen4 x1]
2/41 passed
```

> 공짜 PASS 두 개를 눈여겨보자: gen/width 검사는 이미 주어진 `check_gen/check_width` 가 하고, "6.9 GB/s 면 후보 없음" 은 아무것도 안 하는 stub(`[]`)도 통과한다. 진단 함수는 **후보가 나와야 하는 입력**(3.45, 1.7)으로 검증해야 의미가 있다.

## 4. 힌트

<details>
<summary>힌트 1 — 단위 사다리</summary>

GT/s 는 "레인 하나가 1초에 보내는 전송(transfer) 수" 다. NRZ(Gen1–5)에서는 전송 1개 = 선로 비트 1개, Gen6 PAM4 는 심볼당 2비트라 32 GBaud 로 64 GT/s 를 만든다. 사다리는 `× width` → `× 인코딩 효율` → `÷ 8` (GB/s) → `× 10^9 / 2^30` (GiB/s). 이 순서를 지키면 중간에 GB 와 GiB 가 섞이지 않는다.

</details>

<details>
<summary>힌트 2 — 왜 Fraction 인가</summary>

`16 * 4 * 128 / 130 / 8` 을 float 로 하면 7.876923076923077 이 나오고, "Gen4 x2 == Gen3 x4" 같은 등식 테스트가 반올림 오차로 흔들릴 수 있다. `Fraction(16) * 4 * Fraction(128, 130) / 8 == Fraction(512, 65)` 는 정확하다. 출력할 때만 `float()`.

</details>

<details>
<summary>힌트 3 — TLP 오버헤드 8바이트</summary>

Gen1/2: STP 1 + Seq 2 + LCRC 4 + END 1 = 8. Gen3–5: STP 토큰 4(안에 Seq 12비트가 들어간다고 모델) + LCRC 4 = 8. 그래서 Gen1–5 모두 **헤더 + 8**. Gen6 FLIT 은 TLP 별 프레이밍이 없고 flit 전체에서 236/256 만 TLP 몫.

</details>

<details>
<summary>힌트 4 — MPS 경계 자르기</summary>

다음 경계는 `(a // mps + 1) * mps`. 거기와 버퍼 끝 중 가까운 곳까지가 TLP 하나. `0x1010` 에서 512바이트, MPS 256 이면 `0x1100`(240B), `0x1200`(256B), `0x1210`(16B) — 3개.

</details>

<details>
<summary>힌트 5 — 읽기는 방향이 둘이다</summary>

MRd(헤더만, 4DW)는 요청 방향, 데이터는 CplD(3DW 헤더)로 반대 방향. MRd 하나가 MRRS 바이트를 부르면 완료자는 RCB 경계에서 잘라 `ceil(MRRS / cpl_bytes)` 개의 CplD 로 돌려준다. 데이터 방향 선로 바이트 = `MRRS + n × (12 + 8 + DLLP 몫)`.

</details>

<details>
<summary>힌트 6 — 다운트레이닝 후보</summary>

cap 이하의 모든 (g, w)(cap 자신 제외)에 대해 상한 c 를 구하고 `0.85·c ≤ 측정 ≤ c` 이면 후보. 측정값이 어떤 상한에 "붙어서" 정체했다는 뜻이다.

</details>

## 5. 풀이

### Part A — GT/s → 인코딩 → GB/s

**규칙 표** (레인당, 한 방향):

| Gen | GT/s | 인코딩 | 효율 | 인코딩 후 Gb/s | GB/s (x1) | LnkCap/LnkSta 코드 |
|---|---|---|---|---|---|---|
| 1 | 2.5 | 8b/10b | 0.8 | 2.0 | 0.250 | 1 |
| 2 | 5 | 8b/10b | 0.8 | 4.0 | 0.500 | 2 |
| 3 | 8 | 128b/130b | 0.98462 | 7.877 | 0.985 | 3 |
| 4 | 16 | 128b/130b | 0.98462 | 15.754 | 1.969 | 4 |
| 5 | 32 | 128b/130b | 0.98462 | 31.508 | 3.938 | 5 |
| 6 | 64 (PAM4, 32 GBaud) | 1b/1b + FLIT | 1 (FLIT 236/256 별도) | 64.0 | 8.000 (TLP 7.375) | 6 |

속도 코드는 Linux `include/uapi/linux/pci_regs.h` 기준: `PCI_EXP_LNKCAP_SLS 0x0000000f` 의 값 1..6 = `PCI_EXP_LNKCAP_SLS_2_5GB … _64_0GB`, 현재 속도는 `PCI_EXP_LNKSTA_CLS 0x000f`, 협상된 폭은 `PCI_EXP_LNKSTA_NLW 0x03f0` (`>> PCI_EXP_LNKSTA_NLW_SHIFT 4`). 이 레슨의 `(gen, width)` 는 바로 그 두 필드 값이다 (디코드는 PCIE-B04).

**단계별 추적 — Gen4 x4:**

| 단계 | 식 | 값 |
|---|---|---|
| raw | 16 GT/s × 4 | 64 Gb/s |
| 인코딩 후 | 64 × 128/130 | 63.015 Gb/s |
| GB/s | ÷ 8 | **512/65 = 7.877 GB/s** |
| GiB/s | × 10^9 / 2^30 | 7.336 GiB/s |

Gen3 x1: 8 × 128/130 ÷ 8 = 128/130 = 0.98462 GB/s ≈ **985 MB/s**. 브리프의 기준값 두 개(985 MB/s, 7.88 GB/s)가 그대로 나온다.

**Gen3 의 비밀:** Gen2→Gen3 은 비트율을 5 → 8 GT/s(1.6배)만 올렸는데 처리량은 128/65 ≈ 1.97배가 됐다. 8b/10b(20% 손실)를 128b/130b(1.5% 손실)로 바꿨기 때문이다: 4 Gb/s → 7.877 Gb/s. 8 GT/s 를 고른 이유는 10 GT/s 대비 채널 손실(신호 무결성) 여유 — SIV-M06 / HWLAB-M12(equalization) 참고. Gen4/Gen3, Gen5/Gen4 는 인코딩이 같아 정확히 2배 (테스트로 Fraction 등식 확인).

**Gen6:** 64 GT/s 지만 PAM4(4레벨, 심볼당 2비트)라 심볼율은 32 GBaud — Gen5 와 같은 나이퀴스트 주파수 16 GHz 를 유지하려는 선택이다. PAM4 는 눈(eye)이 1/3 높이라 BER 이 나빠져 **FEC 가 필수** 가 되고, FEC 는 고정 크기 블록이 필요해 **FLIT**(256바이트 고정 단위)가 생겼다. `pci_regs.h` 에도 `PCI_EXP_FLAGS_FLIT 0x8000 /* Flit Mode Supported */` 비트가 있다. 이 레슨의 flit 분할(TLP 236 / DLP 6 / CRC 8 / FEC 6)은 기억 기반이다 — **스펙 확인 권장**. 모델에서는 Gen6 = 항상 FLIT, Gen1–5 = 비 FLIT 으로 단순화했다 (실제로는 Gen6 장치끼리면 낮은 속도에서도 FLIT 모드를 쓸 수 있다 — 스펙 확인 권장).

`--tables` 의 실제 출력 (한 방향, GB/s):

```text
== 표 1. 인코딩 후 한 방향 GB/s (Gen6 은 1b/1b; 마지막 줄 = FLIT 후 TLP 바이트율) ==
gen    GT/s      encoding                x1        x2        x4        x8       x16
Gen1   2.5       8b/10b               0.250     0.500     1.000     2.000     4.000
Gen2   5         8b/10b               0.500     1.000     2.000     4.000     8.000
Gen3   8         128b/130b            0.985     1.969     3.938     7.877    15.754
Gen4   16        128b/130b            1.969     3.938     7.877    15.754    31.508
Gen5   32        128b/130b            3.938     7.877    15.754    31.508    63.015
Gen6   64        1b/1b PAM4 FLIT      8.000    16.000    32.000    64.000   128.000
       Gen6 FLIT 후 TLP 바이트율:        7.375    14.750    29.500    59.000   118.000
```

대각선을 보라: Gen3 x4 = Gen4 x2 = Gen5 x1 = 3.938 GB/s. 이 등식이 Part C 진단의 핵심이다.

### Part B — TLP 오버헤드와 MPS

**모델 (이 레슨의 정의):**

| 항목 | Gen1/2 | Gen3–5 | Gen6 FLIT |
|---|---|---|---|
| 시작 프레이밍 | STP 1B | STP 토큰 4B (Length·FCRC·FP·Seq — 비트 배치 스펙 확인 권장, PCIE-B01) | 없음 |
| Sequence Number | 2B | STP 토큰 안 | 없음 (flit 단위 재전송) |
| 헤더 | 12 (3DW) / 16 (4DW) | 12 / 16 | 12 / 16 로 근사 (FLIT 모드 헤더 형식은 다름 — 스펙 확인 권장) |
| ECRC (선택) | 4 | 4 | 4 |
| LCRC | 4 | 4 | 없음 (flit CRC 8B) |
| 끝 프레이밍 | END 1B | 없음 | 없음 |
| **합 (ECRC 끔)** | **hdr + 8** | **hdr + 8** | **hdr** (+ flit 236/256) |
| DLLP 1개 | SDP + 6 + END = 8B | SDP 토큰 2 + 6 = 8B | DLP 6B 필드에 실림 → 0 |

무시한 것: SKP ordered set(추가 연습 3), 레인 정렬 패딩, 링크 재훈련, 흐름 제어 크레딧 고갈(PCIE-M11).

**DLLP 비율은 가정이다.** 단방향 스트림에서 데이터를 받는 쪽의 Ack/UpdateFC 는 **반대 방향** 선로를 먹는다. 데이터 방향에 섞이는 DLLP 는 반대 방향 트래픽(명령 fetch, doorbell 등)에 대한 Ack/UpdateFC 다. 그래서 "TLP 4개당 1개(=TLP당 2바이트)" 는 보수적인 근사이고, 0 과 1/4 를 둘 다 표에 냈다.

**효율 = payload / (payload + hdr + 8 + 8 × DLLP비율):**

```text
== 표 2. TLP 효율 (Gen1–5 공통 오버헤드 hdr+8; DLLP 0 / TLP당 1/4개) ==
MPS    hdr          DLLP 0     DLLP 1/4
128    12B          86.49%       85.33%
128    16B          84.21%       83.12%
256    12B          92.75%       92.09%
256    16B          91.43%       90.78%
512    12B          96.24%       95.88%
512    16B          95.52%       95.17%
```

추적 — MPS 256, 4DW: 선로 바이트 = 256 + 16 + 8 + 2 = 282, 효율 = 256/282 = 90.78%, Gen4 x4 실효 = 512/65 × 256/282 ≈ **7.151 GB/s**. 헤더는 왜 4DW 인가? NVMe 버퍼는 호스트 메모리 어디든(4GB 위 포함) 있을 수 있고, 주소가 4GB 이상이면 4DW 가 필수다 (Fmt/주소 규칙은 PCIE-B10).

MPS 는 Device Control 의 `PCI_EXP_DEVCTL_PAYLOAD 0x00e0` (bits 7:5, `128 << n`) 로 설정되고, 장치가 지원하는 최대는 DevCap 의 `PCI_EXP_DEVCAP_PAYLOAD 0x00000007`. MRRS 는 `PCI_EXP_DEVCTL_READRQ 0x7000` (bits 14:12, 역시 `128 << n`), RCB 는 Link Control 의 `PCI_EXP_LNKCTL_RCB 0x0008` (0 = 64B, 1 = 128B). 모두 `pci_regs.h` 기준.

**정렬 안 된 버퍼 (`write_tlps`):** DMA 엔진은 보통 MPS 정렬 경계에서 TLP 를 자른다 (스펙이 강제하는 건 4KB 경계 — PCIE-B10, 여기선 흔한 구현을 모델로). `write_tlps(0x1010, 512, 256) == [240, 256, 16]` — 2개면 될 것이 3개가 되어 오버헤드가 24B 더 붙는다. 2000개 랜덤 케이스(xorshift32 고정 시드)로 "합 = nbytes, 각 ≤ MPS, 경계 안 넘음, 개수 최소" 를 검사한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="pcieb02-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">NVMe 데이터가 실제로 흐르는 TLP — 방향이 다르다</text>
  <rect x="20" y="45" width="190" height="170" rx="8" class="f-mut" stroke="currentColor"/>
  <rect x="530" y="45" width="190" height="170" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="115" y="70" font-weight="bold">호스트</text>
    <text x="115" y="90" font-size="12">DRAM 버퍼 + Root Complex</text>
    <text x="625" y="70" font-weight="bold">NVMe SSD (엔드포인트)</text>
    <text x="625" y="90" font-size="12">DMA 엔진 = 버스 마스터</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5" marker-end="url(#pcieb02-arr)">
    <path d="M528 120 L212 120"/>
    <path d="M528 168 L212 168"/>
    <path d="M212 196 L528 196"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="370" y="112">NVMe Read: MWr (posted) payload ≤ MPS, 4DW</text>
    <text x="370" y="134" class="f-ok">→ MPS 가 지배 (표 3)</text>
    <text x="370" y="160">NVMe Write ①: MRd (헤더만) ≤ MRRS</text>
    <text x="370" y="214">② CplD 3DW, RCB 경계로 쪼개짐, 각 ≤ MPS</text>
    <text x="370" y="236" class="f-warn">→ MRRS · 완료 크기가 지배 (표 4)</text>
  </g>
</svg>
```

**읽기 완료 경로 (`effective_read`):** SSD 입장에서 NVMe **Write** 명령은 호스트 메모리를 **읽는** 일이다. MRd 512B → 루트 컴플렉스가 CplD 로 돌려준다. 완료자가 64B(RCB) 단위로 쪼개 돌려주면 CplD 8개 × (12 + 8 + 2) = 176B 오버헤드:

```text
== 표 4. Gen4 x4 순차 쓰기 상한 (CplD downstream, MRRS 512, MPS 256, DLLP 1/4) ==
CplD 크기        CplD/MRd         GB/s        MRd 몫
64B                   8        5.862        3.78%
128B                  4        6.722        4.33%
256B                  2        7.254        4.68%
```

추적 — CplD 64B: 512 / (512 + 8 × 22) = 512/688 → 7.877 × 0.7442 = **5.862 GB/s**. CplD 256B: 512/556 → 7.254 GB/s (같은 MPS 의 MWr 7.151 보다 조금 높은 건 CplD 헤더가 3DW 라서). "MRd 몫" 은 MRd(16 + 8 + 2 = 26B)가 **반대 방향** 링크를 차지하는 비율 — 작지만 0 이 아니다. 루트 컴플렉스가 실제로 몇 바이트 단위로 완료를 돌려주는지는 플랫폼마다 다르다(64B 캐시라인 단위로 돌려주는 구현이 흔하다는 보고가 많다 — 플랫폼 문서 확인 권장). 그래서 "DMA read 가 DMA write 보다 느리다" 는 말이 나온다. 그리고 읽기는 지연(RTT) 동안 outstanding 요청 수(Tag)가 충분해야 상한에 닿는다 — 추가 연습 2 (Little's Law).

### Part C — "Gen4 x4 NVMe 이론 최대 순차 읽기는?"

```text
== 표 3. x4 순차 읽기 상한 (MWr upstream, 4DW, DLLP 1/4) GB/s ==
gen          link    MPS128    MPS256    MPS512
Gen1        1.000     0.831     0.908     0.952
Gen2        2.000     1.662     1.816     1.903
Gen3        3.938     3.274     3.575     3.748
Gen4        7.877     6.547     7.151     7.496
Gen5       15.754    13.094    14.301    14.993
Gen6       32.000    26.222    27.765    28.606
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Gen4 x4 한 방향: 8 GB/s 가 어디서 깎이나 (solution.py 실제 값, GB/s)</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="61">raw (64 Gb/s ÷ 8)</text>
    <text x="10" y="93">128b/130b 후</text>
    <text x="10" y="125">MWr MPS256 4DW</text>
    <text x="10" y="157">+ DLLP 1/4 = 답</text>
    <text x="10" y="189">MWr MPS128</text>
    <text x="10" y="221">쓰기: CplD 64B</text>
    <text x="10" y="253">Gen3 x4 = Gen4 x2</text>
  </g>
  <g stroke="currentColor">
    <rect x="190" y="44" width="496" height="24" class="f-mut"/>
    <rect x="190" y="76" width="488.4" height="24" class="f-mut"/>
    <rect x="190" y="108" width="446.5" height="24" class="f-acc"/>
    <rect x="190" y="140" width="443.4" height="24" class="f-ok"/>
    <rect x="190" y="172" width="405.9" height="24" class="f-acc"/>
    <rect x="190" y="204" width="363.4" height="24" class="f-warn"/>
    <rect x="190" y="236" width="221.7" height="24" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="end">
    <text x="680" y="61">8.000</text>
    <text x="672" y="93">7.877</text>
    <text x="630" y="125">7.202</text>
    <text x="627" y="157">7.151</text>
    <text x="590" y="189">6.547</text>
    <text x="547" y="221">5.862</text>
    <text x="405" y="253">3.575</text>
  </g>
  <line x1="651.9" y1="36" x2="651.9" y2="266" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="655" y="286" fill="currentColor" font-size="12" text-anchor="end">점선 7.45 = Gen4 x4 최상위 드라이브 스펙 수치 (기억 기반, 확인 권장)</text>
</svg>
```

**2분 답변 스크립트:**

1. 링크: 16 GT/s × 4 레인 = 64 Gb/s, 128b/130b → 63.0 Gb/s = **7.88 GB/s 한 방향** (= 7.34 GiB/s).
2. 호스트 Read 데이터는 SSD 가 호스트 메모리에 **MWr 로 올린다**(upstream, posted). TLP 하나 = MPS 데이터 + 4DW 헤더 16 + 프레이밍/Seq/LCRC 8.
3. MPS 256 이면 256/280 ≈ 91.4%, DLLP 조금 빼서 **≈ 7.15 GB/s**; MPS 512 면 ≈ 7.50 GB/s. SKP 까지 빼면 0.3% 더 (추가 연습 3).
4. 그래서 Gen4 x4 드라이브는 ~7 GB/s 에서 정체 → 링크가 병목. 더 빠르려면 Gen5 (≈ 14.3–15.0 GB/s).
5. 반대로 호스트 Write 는 SSD 가 MRd → CplD 로 받아 오므로 MRRS·완료 크기에 좌우되고, 64B 완료면 ≈ 5.9 GB/s 까지 떨어질 수 있다.

**스펙 시트와 비교 (제조사 수치는 기억 기반 — 제조사 페이지 확인 권장, 모두 "up to" 값):**

| 드라이브 | 링크 | 제조사 순차 읽기 | ÷ 모델 MPS256 (7.151 / 14.301) | ÷ 모델 MPS512 (7.496 / 14.993) |
|---|---|---|---|---|
| Samsung 990 PRO | Gen4 x4 | 7,450 MB/s | 104.2% | 99.4% |
| WD_BLACK SN850X | Gen4 x4 | 7,300 MB/s | 102.1% | 97.4% |
| Samsung 9100 PRO | Gen5 x4 | 14,700 MB/s | 102.8% | 98.0% |

해석: 숫자가 맞다면 이 드라이브들은 **인코딩 후 링크 상한(7.877 / 15.754)의 93–95%** 를 쓴다 — 거의 링크 한계다. 그리고 MPS 256·4DW 모델 상한을 넘으므로, 측정 플랫폼이 MPS 512 이상이었거나(또는 헤더·DLLP 가정이 보수적이거나) 하는 가설이 선다. 면접에서는 "제조사 숫자가 7.45 면 MPS 256 로는 이론상 불가능에 가깝고, 그 숫자는 MPS 512 플랫폼에서 나왔을 것" 정도로 **가설로** 말하면 된다. 단위도 조심: 7.45 GB/s = 6.94 GiB/s 이고, fio 는 기본으로 MiB/s 를 먼저 찍는다.

**브링업 진단 (`link_suspects`):**

```text
== 표 5. 측정 순차 읽기 → 다운트레이닝 후보 (cap Gen4 x4, MPS 256) ==
 6.90 GB/s → (없음)
 3.45 GB/s → Gen3 x4, Gen4 x2
 1.70 GB/s → Gen2 x4, Gen3 x2, Gen4 x1
 0.80 GB/s → Gen1 x4, Gen2 x2, Gen3 x1
```

새 보드에서 Gen4 x4 SSD 가 3.45 GB/s 에서 딱 멈췄다면 링크가 반토막 났을 가능성이 높다. 그런데 **Gen3 x4 와 Gen4 x2 는 상한이 정확히 같다** (테스트: `nvme_seq(4,2) == nvme_seq(3,4)`). 원인이 전혀 다르다:

| 후보 | LnkSta 에 보이는 것 | 의심할 것 |
|---|---|---|
| Gen3 x4 | Speed 8GT/s, Width x4 | equalization 실패로 Gen4 못 올림, 채널 손실, 리타이머, 루트 포트 LnkCtl2 Target Speed, BIOS 설정 |
| Gen4 x2 | Speed 16GT/s, Width x2 | 레인 2개 불량 — 커넥터/AC 커플링 캡/레인 반전·스와핑 배선 실수 (PCIE-M07 의 x4→x2 협상) |

그래서 처리량은 **의심** 까지만 주고, 확정은 `lspci -vvv` 의 LnkCap/LnkSta(PCIE-B06) 또는 config 덤프 디코드(PCIE-B04)로 한다. 6.9 GB/s 는 후보 없음 — 링크는 정상이고 NAND/FTL 쪽을 봐야 한다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py                    # 41/41 passed, exit 0
python3 -I solution.py --tables           # 위 표 1–5 + 테스트
python3 -I -W error -X dev solution.py    # 경고·dev 모드에서도 깨끗
```

## 7. 자주 하는 실수

**① 인코딩을 빼먹는다.** `16 × 4 / 8 = 8 GB/s` 는 raw 다. 인코딩 후 7.877, TLP 후 ≈ 7.15. 면접관이 "8 GB/s 다 나오나요?" 라고 물으면 함정이다.

```python
gbps = GT_S[gen] * width / 8                        # 틀림: raw
gbps = GT_S[gen] * width * encoding(gen)[1] / 8     # 맞음
```

**② Gen3 에 8b/10b 를 적용한다.** 8 × 0.8 = 6.4 Gb/s/레인 → Gen4 x4 를 6.4 GB/s 로 계산하게 된다. 8b/10b 는 Gen1/2 뿐이다.

**③ GB 와 GiB 를 섞는다.** 7.877 GB/s = 7.336 GiB/s 로 7% 차이 — Gen4 와 "다운트레이닝 의심" 을 가를 만큼 크다. fio·dd·CrystalDiskMark 가 어느 단위로 찍는지 먼저 확인.

**④ NVMe 쓰기 처리량에 MPS 공식을 쓴다.** 호스트 Write 의 데이터는 CplD 로 온다. MRRS·완료 크기·Tag 수가 변수다.

```python
nvme_seq(4, 4, "write")                      # CplD 경로 (effective_read) — 맞음
effective_write(4, 4, 256)                   # 호스트 Write 에 쓰면 틀림 (MWr 은 호스트 Read 방향)
```

**⑤ 양방향 숫자를 한 방향처럼 쓴다.** "Gen5 x16 = 128 GB/s" 는 raw 64 GB/s × 2 방향이다. 한 방향 인코딩 후는 63.015 GB/s.

**⑥ MPS 를 DevCap 에서 읽는다.** DevCap(`PCI_EXP_DEVCAP_PAYLOAD`)은 장치가 **지원** 하는 최대, 실제로 쓰는 값은 DevCtl(`PCI_EXP_DEVCTL_PAYLOAD`, bits 7:5). 경로상 모든 장치의 DevCtl MPS 는 같아야 하고(더 큰 TLP 를 받으면 Malformed TLP), OS 는 보통 경로 최소값으로 맞춘다.

**⑦ 비율을 float 로 비교한다.** `link_bw(4,2)["GBps"] == link_bw(3,4)["GBps"]` 는 Fraction 이면 항상 참, float 면 연산 순서에 따라 깨질 수 있다.

## 8. 면접 Q&A

<details>
<summary>Q1. GT/s 와 Gb/s 는 어떻게 다른가? Gen3 는 왜 10 GT/s 가 아니라 8 GT/s 인가?</summary>

GT/s 는 선로의 전송(심볼) 속도로, 인코딩 오버헤드가 포함된 값이다. Gb/s 데이터율 = GT/s × 인코딩 효율. Gen2 5 GT/s × 8/10 = 4 Gb/s. Gen3 는 2배를 원했는데 8b/10b 를 유지하면 10 GT/s 가 필요했다 — 채널 손실과 전력이 너무 커진다. 그래서 128b/130b 로 인코딩 손실을 20% → 1.5% 로 줄이고 8 GT/s 에서 7.877 Gb/s 를 얻었다 (1.97배). 대신 DC 균형·전이 밀도를 보장하던 8b/10b 의 성질을 스크램블링으로 대신해야 했고, 2비트 sync header 로 블록 경계를 잡는다.

</details>

<details>
<summary>Q2. "Gen5 x16 = 128 GB/s" 라는 광고 숫자는 무슨 뜻인가?</summary>

32 GT/s × 16 레인 ÷ 8 = 64 GB/s 를 **양방향 합** 으로 2배 한 raw 숫자다. 한 방향, 인코딩 후는 63.015 GB/s, MPS 256 TLP 후 ≈ 57 GB/s. PCIe 는 full duplex 라 각 방향이 독립적으로 이 대역폭을 가진다. 면접 답은 항상 "한 방향, 인코딩 후, 프로토콜 후" 순서로 말한다.

</details>

<details>
<summary>Q3. NVMe Read 와 Write 에서 데이터는 어떤 TLP 로 흐르나? 왜 중요한가?</summary>

NVMe 컨트롤러가 버스 마스터라 데이터 이동은 SSD 가 주도한다. 호스트 Read = SSD 가 호스트 메모리에 **MWr** (posted, upstream) → MPS 와 TLP 헤더가 효율을 정한다. 호스트 Write = SSD 가 호스트 메모리를 **MRd** 로 읽고 루트 컴플렉스가 **CplD** 로 돌려준다 → MRRS, 완료 크기(RCB 64/128 경계로 쪼갬), outstanding Tag 수, 메모리 지연이 효율을 정한다. 그래서 같은 링크에서도 쓰기 처리량이 읽기보다 낮게 나오는 경우가 흔하다. SSD 펌웨어 쪽에서는 쓰기 경로의 MRd 를 충분히 많이 미리 내는(prefetch) 것이 성능 포인트다.

</details>

<details>
<summary>Q4. MPS 와 MRRS 는 누가 정하나? 크게 하면 무조건 좋은가?</summary>

DevCap 이 지원 최대를 알려주고, OS/펌웨어가 DevCtl 에 쓴다. MPS 는 경로 전체가 같은 값을 써야 해서(받는 쪽 MPS 보다 큰 TLP 는 Malformed TLP 에러) 보통 경로 최소값이 된다 (Linux 는 `pci=pcie_bus_safe/perf/peer2peer` 같은 정책이 있다 — 커널 문서 확인 권장). 크면 효율은 오르지만(표 2: 128 → 512 에서 83% → 95%) 단점도 있다: 큰 TLP 하나가 링크를 오래 점유해 다른 트래픽(작은 doorbell, 인터럽트 MSI)의 지연이 늘고, 수신 버퍼·replay buffer 가 커져야 하며, LCRC 에러 시 재전송 비용도 크다. MRRS 를 너무 크게 하면 한 장치가 루트 컴플렉스 완료 대역을 독점해 공정성이 떨어질 수 있다.

</details>

<details>
<summary>Q5. lspci 에서 LnkCap x4 인데 LnkSta x2 다. 처리량에 무슨 일이 생기고 무엇부터 보나?</summary>

인코딩 후 상한이 정확히 절반(Gen4: 7.877 → 3.938 GB/s)이 되고, 순차 읽기가 ~3.5 GB/s 에서 정체한다. "(downgraded)" 표시와 함께 Speed 도 같이 본다 — Gen3 x4 도 같은 숫자라 처리량만으로는 구분 불가. 폭 저하면 레인 결함 쪽: 커넥터 접촉, AC 커플링 캡, 레인 스와핑/반전 배선, 한쪽 레인의 수신 감지(Detect) 실패. 다음 단계는 LTSSM 로그·레인별 에러 카운터·AER Correctable(Receiver Error, Bad TLP) 확인 (PCIE-M07/M08, PCIE-B06).

</details>

<details>
<summary>Q6. Gen6 는 왜 FLIT 을 도입했고, 그 결과 TLP 별 Sequence Number 와 LCRC 는 어떻게 됐나?</summary>

64 GT/s 를 같은 16 GHz 나이퀴스트로 내려고 PAM4 를 썼고, PAM4 는 BER 이 높아 FEC(전방 오류 정정)가 필수가 됐다. FEC 는 고정 길이 코드워드가 필요해서 링크 단위를 256바이트 flit 으로 고정했다. 그러면 오류 검출(CRC)과 재전송(Ack/Nak, 순번)도 flit 단위로 하는 편이 자연스럽다 — 그래서 TLP 마다 붙던 STP/Seq/LCRC 가 없어지고, DLLP 정보도 flit 의 DLP 바이트로 들어간다. 낮은 지연의 가벼운 FEC + 강한 CRC + 재전송 조합으로 지연 증가를 몇 ns 로 억제한다는 것이 설계 목표다. flit 바이트 분할(236/6/8/6)은 이 레슨에서 기억 기반 — 스펙 확인 권장.

</details>

<details>
<summary>Q7. 측정 처리량이 링크 상한의 50% 다. 링크 말고 의심할 것은?</summary>

먼저 LnkSta 로 링크를 배제한다(정상이면 표 5 의 "없음"). 그 다음: ① MPS 가 128 로 잡혔나 (DevCtl) ② 쓰기라면 MRRS 가 작거나 Tag 수가 부족해 RTT 동안 파이프가 비는가 (추가 연습 2) ③ 큐 깊이(QD)·I/O 크기가 작아 SSD 내부 병렬도를 못 채우나 (PCIE-A02) ④ IOMMU 변환 미스(PCIE-M15), 인터럽트 폭주 ⑤ 스위치 업링크가 공유되나 (추가 연습 1: 병목 링크) ⑥ ASPM L1 진입/복귀 지연 (PCIE-M09). 처리량 숫자는 가설을 좁히는 도구이고, 확정은 레지스터와 카운터로 한다.

</details>

<details>
<summary>Q8. 4KB 랜덤 읽기 IOPS 는 PCIe 가 병목인가?</summary>

Gen4 x4 에서 4KB 읽기 한 건은 upstream 으로 데이터 16 × 280B + SQE fetch MRd 24B + CQE 40B + MSI-X 28B ≈ 4572B 를 쓴다 → PCIe 쪽 상한 ≈ 1.72M IOPS (추가 연습 4). 실제 Gen4 드라이브의 랜덤 읽기 스펙은 그 아래(1M–1.4M 급, 제조사 수치 확인 권장)라 보통 NAND 읽기 지연·FTL·컨트롤러 코어가 병목이다. 반대로 512B I/O 면 PCIe 상한이 ~12M IOPS 로 올라가 더더욱 PCIe 병목이 아니다. 순차는 링크 병목, 랜덤 소블록은 디바이스 병목 — 이 구분을 말할 수 있으면 좋다.

</details>

## 9. 추가 연습

### 연습 1 — dmesg 의 "available PCIe bandwidth" 를 정수로 재현

Linux 는 링크가 느리게 잡히면 `... Gb/s available PCIe bandwidth, limited by 8.0 GT/s PCIe x4 link at ...` 를 찍는다. 커널은 레인당 Mb/s 를 정수 산술로 계산한다(`drivers/pci/pci.h` 의 `PCIE_SPEED2MBS_ENC` — 기억 기반, 소스 확인 권장): `8000*128/130 = 7876` 처럼 **내림**. `kernel_mbps(gen, width)`, 경로 `[(gen, width), ...]`(장치 쪽부터) 의 최소값과 병목 index 를 주는 `available_bw(path)`, 메시지 포맷 `kernel_msg(path)` 를 작성하라. Gen4 x4 SSD 가 Gen3 x8 스위치 업링크 뒤에 있으면?

<details>
<summary>답 보기</summary>

레인당 2000 / 4000 / 7876 / 15753 / 31507 / 64000 Mb/s. Gen3 x4 = 31504 → `31.504 Gb/s` (실제 dmesg 에서 자주 보는 숫자). Gen4 x4 = 63012, Gen3 x8 = 63008 — 업링크가 4 Mb/s 더 작아서 **index 1(업링크)이 병목** 으로 보고된다. 정수 내림 때문에 생기는 차이로, 실제로는 같은 대역이다. 같은 값이면 장치 쪽을 보고하도록 `<` 로 비교한다. Gen6 은 커널이 FLIT 손실을 빼지 않고 `64000*1/1` 로 둔다(기억 기반 — 확인 권장) — 이 레슨의 `tlp_GBps`(236/256)와 정의가 다르다는 점도 기억.

</details>

### 연습 2 — Tag 개수와 Little's Law

읽기(MRd→CplD)는 요청을 내고 RTT 후에 데이터가 온다. outstanding 요청이 `tags` 개면 처리량 ≤ `tags × MRRS / RTT` (bytes/ns = GB/s). `read_bw_tag_limited(tags, mrrs, rtt_ns, ceiling)` 와 상한을 채우는 최소 Tag 수 `min_tags(ceiling, mrrs, rtt_ns)` 를 작성하라. Gen4 x4, MRRS 512, CplD 256 상한(≈ 7.254 GB/s)에서 RTT 1 µs / 4 µs 이면?

<details>
<summary>답 보기</summary>

필요한 바이트-인-플라이트 = 7.254 GB/s × 1000 ns = 7254 B → ⌈7254/512⌉ = **15 Tag**. RTT 4 µs 면 29015 B → **57 Tag** — 5비트 Tag(32개)로는 모자라 32 × 512 / 4000 = 4.096 GB/s 에서 막힌다. 그래서 Extended Tag(8비트, `PCI_EXP_DEVCTL_EXT_TAG 0x0100`)나 10비트 Tag 가 중요하고, 스위치·리타이머가 RTT 를 늘리면 쓰기 성능이 먼저 떨어진다. 쓰기 성능이 이상하면 "Tag 수 × MRRS ÷ RTT" 부터 계산해 보라.

</details>

### 연습 3 — SKP ordered set 까지 빼기

송신기는 클럭 보정(수신측 elastic buffer)을 위해 주기적으로 SKP ordered set 을 넣는다. 모델(간격 값은 기억 기반 — 스펙 확인 권장): Gen1/2 는 4심볼 SKP OS 를 1180 심볼 시간마다, Gen3–5 는 SKP 블록 1개를 데이터 블록 370개마다. `skp_factor(gen, interval=None)` 을 작성하고 Gen4 x4 MPS 256 읽기 상한에 곱하라.

<details>
<summary>답 보기</summary>

Gen1/2: 1176/1180 (0.34%), Gen3–5: 370/371 (0.27%), Gen6 은 이 모델에서 1. Gen4 x4: 7.151 × 370/371 ≈ **7.131 GB/s**. 0.3% 는 MPS 선택(표 2 의 몇 %)보다 한 자릿수 작다 — 면접에서는 "SKP·DLLP 는 1% 미만, 큰 항목은 인코딩과 TLP 헤더" 라고 크기 순서를 말하면 된다. 실제 간격은 SRIS(독립 refclk + SSC) 같은 클럭 구성에서 더 짧아진다(스펙 확인 권장).

</details>

### 연습 4 — 4KB 랜덤 읽기의 PCIe 바이트 회계

랜덤 읽기 한 건이 양방향에 쓰는 바이트를 세라 (헤더 4DW, CplD 3DW, DLLP 무시). upstream: SQE fetch MRd + 데이터 MWr(MPS 로 쪼갬) + CQE MWr 16B + MSI-X MWr 4B × `irq_per_io`. downstream: SQ doorbell MWr 4B + SQE CplD 64B + CQ doorbell MWr 4B. `nvme_4k_io(gen, width, io_bytes, mps, irq_per_io)` 가 바이트와 IOPS 상한, 병목 방향을 돌려주게 하라.

<details>
<summary>답 보기</summary>

Gen4 x4, 4KB, MPS 256: up = 24 + 16 × 280 + 40 + 28 = **4572 B**, down = 28 + 84 + 28 = **140 B** → upstream 병목, IOPS ≈ **1.7229 M**. 인터럽트를 8건당 1번으로 합치면(coalescing) up = 4547.5 B — 고작 0.5% 개선이라 PCIe 대역 때문이 아니라 **CPU 인터럽트 비용** 때문에 coalescing 을 한다는 걸 알 수 있다. 512B I/O 는 데이터 TLP 2개(560 B), up 652 B → ≈ 12.08 M IOPS. Gen3 x4 는 정확히 절반.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py      # 20/20 passed, exit 0
```

## 10. 핵심 정리

- 사다리: **GT/s × 레인 × 인코딩 효율 ÷ 8 = GB/s (한 방향)**, GiB 는 × 10^9/2^30. Gen1/2 8b/10b(0.8), Gen3–5 128b/130b(0.985), Gen6 PAM4 1b/1b + FLIT 236/256(스펙 확인 권장).
- 외울 값: Gen3 x1 ≈ 985 MB/s, Gen4 x4 = 512/65 ≈ 7.877 GB/s, Gen5 x16 ≈ 63.0 GB/s, Gen3 x4 = Gen4 x2 = Gen5 x1 ≈ 3.94 GB/s.
- Gen1–5 TLP 오버헤드 모델 = 헤더(12/16) + 8 (STP/Seq/LCRC/END 또는 STP 토큰/LCRC) (+ECRC 4). DLLP 8B/개, 비율은 가정.
- MPS 128/256/512 → 4DW 효율 84/91/96% → Gen4 x4 순차 읽기 ≈ 6.55 / 7.15 / 7.50 GB/s.
- NVMe Read = SSD 의 **MWr upstream**(MPS), NVMe Write = SSD 의 MRd → **CplD downstream**(MRRS, RCB 완료 크기, Tag × MRRS / RTT).
- Gen4 x4 드라이브가 ~7 GB/s 에서 멈추는 건 링크 한계. 측정이 절반이면 Gen3 x4 또는 Gen4 x2 — 처리량으로는 못 가르니 LnkSta 를 읽는다.
- 레지스터: LnkCap SLS/MLW, LnkSta CLS/NLW, DevCtl PAYLOAD(7:5)/READRQ(14:12), LnkCtl RCB — `pci_regs.h` 기준.

## 참고자료

- PCI-SIG, *PCI Express Base Specification* Rev 3.0 (4.2 128b/130b, framing tokens), Rev 6.0 (FLIT mode, PAM4, FEC) — 이 레슨의 FLIT 바이트 분할·STP 토큰 배치·SKP 간격은 스펙으로 확인할 것
- Linux `include/uapi/linux/pci_regs.h` — `PCI_EXP_LNKCAP_SLS*`, `PCI_EXP_LNKSTA_CLS/NLW`, `PCI_EXP_DEVCTL_PAYLOAD/READRQ`, `PCI_EXP_LNKCTL_RCB`, `PCI_EXP_FLAGS_FLIT` (Lima VM `/usr/include/linux/pci_regs.h` 에서 확인)
- Linux `drivers/pci/pci.h` (`PCIE_SPEED2MBS_ENC`) · `drivers/pci/pci.c` (`pcie_bandwidth_available`, `pcie_print_link_status`)
- Xilinx/AMD WP350, *Understanding Performance of PCI Express Systems* — TLP/DLLP 오버헤드와 MPS/MRRS 효율 계산의 고전
- NVM Express Base Specification 2.x — 명령 처리 흐름(SQ/CQ, doorbell, PRP)과 데이터 전송 방향 (PCIE-M01, PCIE-M04)
- R. Budruk, D. Anderson, T. Shanley, *PCI Express System Architecture* (MindShare) — 8b/10b, 프레이밍, 흐름 제어
- 관련 레슨: PCIE-B01(계층 감싸기), PCIE-B04(LnkCap/LnkSta 디코드), PCIE-B06(lspci 실전), PCIE-B10(TLP 헤더·4KB 분할), PCIE-M07/M08(LTSSM·다운트레이닝 진단), PCIE-M11(크레딧), PCIE-A02(QD·IOPS 모델), SIV-M06(SerDes BER), HWLAB-M12(equalization)
- Python [`fractions`](https://docs.python.org/3/library/fractions.html) — 정확한 유리수 연산
