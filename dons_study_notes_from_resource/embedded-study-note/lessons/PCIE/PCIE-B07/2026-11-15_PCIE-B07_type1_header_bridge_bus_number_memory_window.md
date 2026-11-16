---
id: PCIE-B07
time: 60분
updated: 2026-11-15
---
# PCIE-B07 Type 1 헤더와 브리지: bus number, memory window

## 학습 목표

- Type 1 헤더(PCI-to-PCI bridge header)의 0x18–0x3F 를 **오프셋과 비트로** 읽는다: Primary/Secondary/Subordinate Bus, IO Base/Limit(16/32비트), Memory Base/Limit, Prefetchable Base/Limit + Upper 32, Bridge Control.
- 윈도우(window) 공식을 손으로 계산한다: `base = (reg & 0xFFF0) << 16`, `limit = ((reg & 0xFFF0) << 16) | 0xFFFFF`, `base > limit` = 빈 윈도우, prefetchable 타입 1 이면 상위 32비트를 붙인다.
- 스위치(USP + DSP 여러 개)가 **메모리 요청**과 **config 요청**을 어느 포트로 보내는지(또는 "내 것 아님" / UR / 위로) 판정하는 라우터를 짜고, 버스 번호 전수 + 경계 주소 테스트로 검증한다.
- QEMU 로 만든 진짜 스위치(pcie-root-port → x3130 USP → xio3130 DSP ×3 + NVMe 2개)의 config 바이트를 디코드해 리눅스 `/proc/iomem`·`lspci -vv` 와 같은 값이 나오는지 확인한다.
- "엔드포인트 BAR 가 0xFFFFFFFF 로 읽힌다", "MMIO 는 되는데 DMA 가 안 된다" 같은 브링업 증상을 Command 비트·윈도우 단위로 진단한다.

## 1. 문제

PCIe 링크가 L0 까지 올라왔는데 SSD BAR 가 전부 `0xFFFFFFFF` 로 읽힐 때, 면접관(그리고 브링업 현장)이 기대하는 첫 답은 "경로상 브리지의 bus number 와 memory window, Command 비트를 읽어 보겠다" 이다 — **트래픽은 브리지 레지스터가 허락한 곳으로만 간다.**

PCIE-B03 에서 본 Type 0 헤더는 "나는 장치다, 내 BAR 는 이것" 을 말한다. Type 1 헤더는 "나는 **문(bridge)** 이다, 내 뒤에는 이 버스 번호들과 이 주소 범위가 있다" 를 말한다. 루트 포트(Root Port), 스위치의 업스트림 포트(USP, Upstream Switch Port)와 다운스트림 포트(DSP, Downstream Switch Port)는 소프트웨어에게 전부 Type 1 헤더를 가진 가상 PCI-PCI 브리지로 보인다. Don 이 SSD 펌웨어를 할 때 보던 엔드포인트(Type 0) 바로 위에는 항상 이런 브리지가 하나 이상 있다.

브리지가 들고 있는 정보는 딱 두 종류다.

| 무엇 | 레지스터 | 쓰이는 곳 |
|---|---|---|
| **버스 번호 범위** `[Secondary, Subordinate]` | 0x18 Primary, 0x19 Secondary, 0x1A Subordinate | ID 라우팅: config 요청(그리고 Completion, ID-routed Message) |
| **주소 윈도우** `[Base, Limit]` | 0x1C/0x1D IO, 0x20/0x22 Memory, 0x24/0x26 + 0x28/0x2C Prefetchable | 주소 라우팅: MRd/MWr/IO 요청 |

그리고 이 정보를 실제로 쓸지 말지를 Command 레지스터(0x04)가 정한다: **Memory/IO Space Enable = 위→아래(primary→secondary) 전달 허가**, **Bus Master Enable = 아래→위(secondary→primary) 전달 허가** (PCI-to-PCI Bridge spec / PCIe Base spec Type 1 Command 정의).

아래 그림은 Type 1 헤더 전체(64바이트)와, 이 레슨에서 QEMU 로 실제 캡처한 루트 포트 00:02.0 의 값이다 (오프셋·이름은 Linux include/uapi/linux/pci_regs.h 기준).

```svg
<svg viewBox="0 0 740 440" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="14">Type 1 헤더 (0x00–0x3F) — 왼쪽이 byte 3, 오른쪽이 byte 0 (리틀 엔디언 DW)</text>
  <text x="560" y="40" fill="currentColor" font-weight="bold">00:02.0 실값 (QEMU)</text>
  <g stroke="currentColor">
    <rect x="50" y="46" width="240" height="22" class="f-mut"/><rect x="290" y="46" width="240" height="22" class="f-mut"/>
    <rect x="50" y="68" width="240" height="22" class="f-mut"/><rect x="290" y="68" width="240" height="22" class="f-mut"/>
    <rect x="50" y="90" width="360" height="22" class="f-mut"/><rect x="410" y="90" width="120" height="22" class="f-mut"/>
    <rect x="50" y="112" width="120" height="22" class="f-mut"/><rect x="170" y="112" width="120" height="22" class="f-warn"/><rect x="290" y="112" width="120" height="22" class="f-mut"/><rect x="410" y="112" width="120" height="22" class="f-mut"/>
    <rect x="50" y="134" width="480" height="22" class="f-ok"/>
    <rect x="50" y="156" width="480" height="22" class="f-ok"/>
    <rect x="50" y="178" width="120" height="22" class="f-mut"/><rect x="170" y="178" width="120" height="22" class="f-acc"/><rect x="290" y="178" width="120" height="22" class="f-acc"/><rect x="410" y="178" width="120" height="22" class="f-acc"/>
    <rect x="50" y="200" width="240" height="22" class="f-mut"/><rect x="290" y="200" width="120" height="22" class="f-acc"/><rect x="410" y="200" width="120" height="22" class="f-acc"/>
    <rect x="50" y="222" width="240" height="22" class="f-acc"/><rect x="290" y="222" width="240" height="22" class="f-acc"/>
    <rect x="50" y="244" width="240" height="22" class="f-acc"/><rect x="290" y="244" width="240" height="22" class="f-acc"/>
    <rect x="50" y="266" width="480" height="22" class="f-acc"/>
    <rect x="50" y="288" width="480" height="22" class="f-acc"/>
    <rect x="50" y="310" width="240" height="22" class="f-acc"/><rect x="290" y="310" width="240" height="22" class="f-acc"/>
    <rect x="50" y="332" width="360" height="22" class="f-mut"/><rect x="410" y="332" width="120" height="22" class="f-mut"/>
    <rect x="50" y="354" width="480" height="22" class="f-mut"/>
    <rect x="50" y="376" width="240" height="22" class="f-acc"/><rect x="290" y="376" width="120" height="22" class="f-mut"/><rect x="410" y="376" width="120" height="22" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="12" y="62">00</text><text x="12" y="84">04</text><text x="12" y="106">08</text><text x="12" y="128">0C</text>
    <text x="12" y="150">10</text><text x="12" y="172">14</text><text x="12" y="194">18</text><text x="12" y="216">1C</text>
    <text x="12" y="238">20</text><text x="12" y="260">24</text><text x="12" y="282">28</text><text x="12" y="304">2C</text>
    <text x="12" y="326">30</text><text x="12" y="348">34</text><text x="12" y="370">38</text><text x="12" y="392">3C</text>
  </g>
  <g fill="currentColor">
    <text x="130" y="62">Device ID</text><text x="370" y="62">Vendor ID</text>
    <text x="140" y="84">Status</text><text x="370" y="84">Command</text>
    <text x="180" y="106">Class Code</text><text x="440" y="106">Rev ID</text>
    <text x="90" y="128">BIST</text><text x="178" y="128">Header Type=1</text><text x="310" y="128">Latency</text><text x="420" y="128">Cache Line</text>
    <text x="230" y="150">BAR0 (브리지 자신의 레지스터)</text>
    <text x="270" y="172">BAR1</text>
    <text x="58" y="194">Sec Latency</text><text x="182" y="194">Subordinate</text><text x="305" y="194">Secondary</text><text x="430" y="194">Primary</text>
    <text x="100" y="216">Secondary Status</text><text x="315" y="216">IO Limit</text><text x="435" y="216">IO Base</text>
    <text x="110" y="238">Memory Limit</text><text x="355" y="238">Memory Base</text>
    <text x="95" y="260">Prefetchable Limit</text><text x="340" y="260">Prefetchable Base</text>
    <text x="190" y="282">Prefetchable Base Upper 32</text>
    <text x="190" y="304">Prefetchable Limit Upper 32</text>
    <text x="95" y="326">IO Limit Upper 16</text><text x="335" y="326">IO Base Upper 16</text>
    <text x="200" y="348">Reserved</text><text x="435" y="348">Cap Ptr</text>
    <text x="200" y="370">Expansion ROM Base</text>
    <text x="110" y="392">Bridge Control</text><text x="315" y="392">Int Pin</text><text x="430" y="392">Int Line</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="560" y="62">000c 1b36</text><text x="560" y="84">0010 0407</text><text x="560" y="106">060400 00</text><text x="560" y="128">00 01 00 00</text>
    <text x="560" y="150">10781000</text><text x="560" y="172">00000000</text><text x="560" y="194">00 05 01 00</text><text x="560" y="216">0000 40 10</text>
    <text x="560" y="238">1060 1000</text><text x="560" y="260">0081 0001</text><text x="560" y="282">00000080</text><text x="560" y="304">00000080</text>
    <text x="560" y="326">0000 0000</text><text x="560" y="348">000000 54</text><text x="560" y="370">00000000</text><text x="560" y="392">0002 01 0f</text>
  </g>
  <rect x="50" y="410" width="14" height="12" class="f-acc" stroke="currentColor"/><text x="70" y="421" fill="currentColor">Type 1 전용 (이 레슨)</text>
  <rect x="230" y="410" width="14" height="12" class="f-ok" stroke="currentColor"/><text x="250" y="421" fill="currentColor">BAR 2개뿐 (0x18 부터는 버스 번호)</text>
  <rect x="470" y="410" width="14" height="12" class="f-mut" stroke="currentColor"/><text x="490" y="421" fill="currentColor">Type 0 과 공통 (PCIE-B03)</text>
</svg>
```

이 레슨의 실습 토폴로지는 Lima VM 안에서 QEMU 로 띄운 진짜 스위치다 (`qemu_switch_capture.txt`, 게스트 리눅스 6.18 이 버스 번호와 윈도우를 직접 할당한 결과 — QEMU 에뮬레이션 값, 실칩 아님).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="14">실캡처 토폴로지: 버스 번호 [sec–sub] 와 memory window</text>
  <g stroke="currentColor">
    <rect x="250" y="30" width="240" height="40" class="f-mut"/>
    <rect x="250" y="95" width="240" height="44" class="f-acc"/>
    <rect x="250" y="160" width="240" height="44" class="f-acc"/>
    <rect x="20" y="235" width="220" height="44" class="f-acc"/>
    <rect x="260" y="235" width="220" height="44" class="f-acc"/>
    <rect x="500" y="235" width="220" height="44" class="f-acc"/>
    <rect x="40" y="295" width="180" height="30" class="f-ok"/>
    <rect x="520" y="295" width="180" height="30" class="f-ok"/>
    <line x1="370" y1="70" x2="370" y2="95"/><line x1="370" y1="139" x2="370" y2="160"/>
    <line x1="370" y1="204" x2="370" y2="220"/><line x1="130" y1="220" x2="610" y2="220"/>
    <line x1="130" y1="220" x2="130" y2="235"/><line x1="370" y1="220" x2="370" y2="235"/><line x1="610" y1="220" x2="610" y2="235"/>
    <line x1="130" y1="279" x2="130" y2="295"/><line x1="610" y1="279" x2="610" y2="295"/>
  </g>
  <g fill="currentColor">
    <text x="262" y="48">bus 00: host bridge 00:00.0 (ECAM)</text><text x="262" y="64">virtio-net 00:01.0 BAR 0x10700000</text>
    <text x="262" y="112">00:02.0 Root Port  bus [01–05]</text>
    <text x="262" y="130" font-family="monospace">mem 10000000-106fffff</text>
    <text x="262" y="177">01:00.0 USP  bus [02–05]</text>
    <text x="262" y="195" font-family="monospace">mem 10000000-106fffff</text>
    <text x="30" y="252">02:00.0 DSP dev0  bus [03]</text>
    <text x="30" y="270" font-family="monospace">mem 10000000-101fffff</text>
    <text x="270" y="252">02:01.0 DSP dev1  bus [04]</text>
    <text x="270" y="270" font-family="monospace">mem 10200000-103fffff</text>
    <text x="510" y="252">02:02.0 DSP dev2  bus [05]</text>
    <text x="510" y="270" font-family="monospace">mem 10400000-105fffff</text>
    <text x="50" y="315">NVMe 03:00.0 BAR0 10000000</text>
    <text x="300" y="300">빈 슬롯 (윈도우만 예약)</text>
    <text x="530" y="315">NVMe 05:00.0 BAR0 10400000</text>
  </g>
  <text x="500" y="112" fill="currentColor">pref64 8000000000-80008fffff</text>
  <text x="500" y="177" fill="currentColor">10600000-106fffff: DSP 없음 → UR</text>
  <text x="500" y="195" fill="currentColor">bus 02 = 스위치 내부 버스</text>
</svg>
```

### Part A — Type 1 헤더 디코더

`t1_decode(cfg, len, &h)` 를 구현한다. 입력은 리틀 엔디언 config 바이트 배열(`lspci -x` 텍스트를 주어진 `cfg_parse_lspci` 로 바꾼 것, 최소 64바이트).

- Primary / Secondary / Subordinate / Secondary Latency Timer, Command, Bridge Control.
- IO 윈도우: 하위 nibble = 타입(0 = 16비트, 1 = 32비트), 32비트면 0x30/0x32 의 Upper 16 을 붙인다. 4KB 단위.
- Memory 윈도우: `[15:4]` → 주소 `[31:20]`, limit 은 `| 0xFFFFF`. 1MB 단위, 항상 32비트.
- Prefetchable 윈도우: 같은 모양, 타입 1 이면 0x28/0x2C 가 주소 `[63:32]`.
- 각 윈도우 `valid = base <= limit` (빈 윈도우 표현). `win_contains(w, a)`.
- 오류: 64바이트 미만(`T1_ERR_SHORT`), Header Type `& 0x7F != 1`(`T1_ERR_NOT_TYPE1`, multi-function 비트 0x80 은 무시), IO/Pref 타입 nibble 이 2 이상이거나 base 와 limit 의 nibble 이 다름(`T1_ERR_IO_TYPE`/`T1_ERR_PREF_TYPE`).
- 실캡처 5개 브리지의 값이 게스트 `/proc/iomem`·`/proc/ioports` 와 같아야 한다.

### Part B — 스위치 라우터: 메모리 주소와 config 버스 번호

USP 1개 + DSP N개(각각 Type 1 헤더 디코드 결과)로 된 `pcie_switch` 에 대해:

- `route_mem_down(sw, addr)` — USP 로 **위에서 내려온** 메모리 요청. USP 가 Memory Space Enable 이고 USP 의 mem 또는 pref 윈도우 안이어야 스위치가 받는다(아니면 `RT_NOT_MINE`). 받은 뒤 Memory Space Enable 인 DSP 중 윈도우가 맞는 것: 0개 → `RT_UR`, 1개 → `RT_DSP`(port), 2개 이상 → `RT_CONFLICT`.
- `route_cfg(sw, bus, dev)` — 목표 bus 의 config 요청. `[usp.sec, usp.sub]` 밖 → `RT_NOT_MINE`. `bus == usp.sec`(내부 버스) → Type 0 으로 바꿔 device 번호가 `dev` 인 DSP 에게(`RT_INTERNAL`, 없으면 UR). 아니면 `dsp.sec <= bus <= dsp.sub` 인 DSP: `bus == dsp.sec` 이면 **Type 1 → Type 0 변환**(링크 아래엔 dev 0 만 — dev ≠ 0 이면 UR), 그보다 아래 버스면 Type 1 그대로.
- 빈 윈도우(`base > limit`), 4GB 위 64비트 prefetchable, 32비트 mem 윈도우의 4GB 별칭(없음), `sec > sub` 인 망가진 버스 범위, 윈도우/버스 겹침까지 테스트. bus 0..255 × dev 0..31 전수와 경계 ±32 + 랜덤 주소 20000개를 독립 참조 모델과 비교한다.

### Part C — 실캡처 스위치: DMA 경로와 브링업 진단

- QEMU 실캡처 USP 01:00.0 + DSP 02:00.0/02:01.0/02:02.0 으로 스위치를 만들고 NVMe 두 개의 BAR 주소, 빈 슬롯 윈도우, 구멍, 스위치 밖 주소(virtio BAR, 루트 포트 자신의 BAR)를 라우팅한다.
- `route_mem_up(sw, ingress, addr)` — DSP 아래 장치(NVMe)가 보낸 메모리 요청(DMA). ingress DSP 의 Bus Master Enable 이 없으면 막힘(UR), 자기 윈도우 안 주소면 위로 안 보냄(UR), 다른 DSP 윈도우면 스위치 안 P2P(`RT_DSP`), USP 윈도우 안의 구멍이면 UR, 그 밖이면 USP Bus Master Enable 확인 후 `RT_UPSTREAM`.
- `diag_ep_bar(sw, dsp, bar, size)` — "DSP `dsp` 아래 엔드포인트의 BAR `[bar, bar+size-1]` 에 CPU 가 닿는가" 를 USP Mem 비트 → DSP Mem 비트 → USP 윈도우가 BAR **전체**를 덮는가 → DSP 윈도우가 덮는가 → 다른 DSP 와 겹치는가 순서로 판정해 진단 코드를 돌려준다.

## 2. Problem (English)

> **Decode a PCIe bridge (Type 1) header and route transactions through a switch.**
>
> Config space is a little-endian byte array. Implement:
>
> ```c
> int     t1_decode(const uint8_t *cfg, size_t len, t1_hdr *h);   // T1_OK or negative error
> bool    win_contains(win_t w, uint64_t addr);                    // empty window (base > limit) contains nothing
> route_t route_mem_down(const pcie_switch *sw, uint64_t addr);    // request entering the USP from above
> route_t route_cfg(const pcie_switch *sw, uint8_t bus, uint8_t dev);
> route_t route_mem_up(const pcie_switch *sw, int ingress_dsp, uint64_t addr);   // DMA from below a DSP
> int     diag_ep_bar(const pcie_switch *sw, int dsp, uint64_t bar, uint64_t size);
> ```
>
> - Memory window: `base = (MEMORY_BASE & 0xFFF0) << 16`, `limit = ((MEMORY_LIMIT & 0xFFF0) << 16) | 0xFFFFF`.
>   Prefetchable: same, plus `PREF_BASE_UPPER32/PREF_LIMIT_UPPER32` as bits 63:32 when the type nibble is 1.
>   IO: 4 KB granularity, optional 32-bit form with `IO_BASE_UPPER16/IO_LIMIT_UPPER16`.
> - A bridge forwards downstream only if Memory Space Enable is set; upstream only if Bus Master Enable is set.
> - Config: bus == secondary → convert to Type 0 (only device 0 exists below a PCIe downstream port);
>   secondary < bus ≤ subordinate → forward as Type 1.
> - Return `RT_NOT_MINE`, `RT_UR`, `RT_DSP`, `RT_INTERNAL`, `RT_UPSTREAM` or `RT_CONFLICT` (overlapping windows/bus ranges).
> - No dynamic allocation; reject short or malformed headers.
>
> Example (QEMU switch): USP `bus 01/02/05`, DSP0 `02/03/03 mem 10000000-101fffff`. `route_mem_down(0x10000000)` → DSP0;
> `route_mem_down(0x10600000)` → UR (inside the USP window, no DSP claims it); `route_cfg(3, 0)` → DSP0, Type 0;
> `route_cfg(6, 0)` → not mine; `route_mem_up(DSP0, 0x40000000)` → upstream.
>
> Follow-ups: (1) The NVMe BAR reads all-ones after boot — what do you read first? (2) MMIO works but the device's DMA never
> lands — why? (3) Why is the limit register "inclusive with 0xFFFFF appended"? (4) How do you program a window without a glitch?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 개: A1 `t1_decode`, A2 `win_contains`, B1 `route_mem_down`, B2 `route_cfg`, C1 `route_mem_up`, C2 `diag_ep_bar`. 텍스트 파서 `cfg_parse_lspci`(PCIE-B03 축약판), `sw_build`, `t1_print`, 가상 스위치 제조기 `t1_make`, 테스트 전용 참조 모델(`ref_*`)은 주어진다. 스텁은 "디코드 실패 / 포함 안 함 / NOT_MINE / BAD_ARG" 를 돌려줘 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:343  t1_decode(rp, 64, &h) == T1_OK
FAIL starter.c:344  h.pri == 0x00 && h.sec == 0x01 && h.sub == 0x05 && h.command == 0x0407
…
FAIL starter.c:475  diag_ep_bar(&sw, 2, 0xC0000000, 0x1000) == DIAG_NOT_IN_DSP
diag(03:00.0 BAR @0x10700000): 인자 오류 (size 0 / 주소 넘침 / DSP 번호)
15/75 passed
```

(exit 1.) 통과하는 15개는 "NOT_MINE 이 정답인" 경우들이다 — 스텁이 우연히 맞힌다. A 만 채우면 Part A 가 전부 통과하고, B 를 채우면 전수 비교 두 개가 통과한다.

## 4. 힌트

<details><summary>힌트 1 — 윈도우 레지스터는 "1MB 번호" 다</summary>

Memory Base 16비트 레지스터의 `[15:4]` 12비트는 주소 `[31:20]`, 즉 "몇 번째 1MB 인가" 다. base 는 그 1MB 의 첫 바이트(`<< 16` 하면 하위 20비트가 0), limit 는 그 1MB 의 **마지막 바이트**(`| 0xFFFFF`). 그래서 base 레지스터 == limit 레지스터 는 빈 윈도우가 아니라 **1MB 윈도우**다. 하위 nibble 은 Memory 에선 읽기전용 0, Prefetchable 에선 "64비트 지원" 표시라 주소 계산 전에 반드시 지운다.

</details>

<details><summary>힌트 2 — 빈 윈도우는 별도 비트가 없다</summary>

"이 브리지 뒤에 메모리가 없다" 를 표시하는 비트는 없다. base > limit 로 쓴다 — 리눅스는 base 0xFFF0, limit 0x0000 을 한 번의 dword 쓰기로 넣는다(drivers/pci/setup-bus.c `pci_setup_bridge_mmio`). `valid = base <= limit` 를 확장(`|0xFFFFF`, upper32) **후의** 값으로 계산하면 된다.

</details>

<details><summary>힌트 3 — config 라우팅은 세 구간</summary>

`bus` 를 USP 의 `[sec, sub]` 와 비교: 밖이면 내 것 아님. `== usp.sec` 이면 스위치 내부 버스(DSP 들이 사는 곳) → Type 0, `dev` 로 DSP 를 고른다. 그 밖(`usp.sec < bus <= usp.sub`)이면 `[dsp.sec, dsp.sub]` 가 bus 를 포함하는 DSP 를 찾고, `bus == dsp.sec` 인지로 Type 0/1 을 정한다.

</details>

<details><summary>힌트 4 — 방향마다 다른 Command 비트</summary>

위→아래(primary→secondary)는 Memory Space Enable(IO 면 IO Space Enable), 아래→위(secondary→primary)는 Bus Master Enable. 스위치 안의 P2P 는 "ingress DSP 가 내부 버스로 올려 보내고(Bus Master) 다른 DSP 가 받는(Memory Space)" 두 단계라 USP 는 관여하지 않는다.

</details>

<details><summary>힌트 5 — 진단은 "덮는가" 를 양 끝으로</summary>

BAR 시작 주소만 윈도우 안에 있으면 안 된다. `[bar, bar+size-1]` 전체가 **하나의** 윈도우 안이어야 한다. `bar + size - 1 < bar` 면 64비트 넘침 → 인자 오류.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "Type 1 은 브리지. 0x18/0x19/0x1A 가 버스 범위, 0x20–0x2F 가 주소 윈도우, Command 의 Mem/BusMaster 가 방향별 전달 스위치."
2. 윈도우 공식(1MB/4KB 단위, limit 은 포함 + 하위 비트 1로 채움, base > limit = 빈 윈도우, pref 64비트는 upper32).
3. 라우팅 규칙: 주소 라우팅(윈도우), ID 라우팅(버스 범위; sec 와 같으면 Type 0 변환).
4. 증상 → 레지스터: BAR 0xFFFFFFFF → 경로상 Mem 비트와 윈도우, DMA 실패 → 경로상 Bus Master.

### Part A — 레지스터 표 (Linux include/uapi/linux/pci_regs.h 기준)

| 오프셋 | 이름 (`pci_regs.h`) | 크기 | 비트 / 디코드 |
|---|---|---|---|
| 0x18 | `PCI_PRIMARY_BUS` | 8 | 브리지 위쪽 버스 번호 |
| 0x19 | `PCI_SECONDARY_BUS` | 8 | 바로 아래 버스 번호 |
| 0x1A | `PCI_SUBORDINATE_BUS` | 8 | 아래 전체 중 가장 큰 버스 번호 |
| 0x1B | `PCI_SEC_LATENCY_TIMER` | 8 | PCIe 에선 의미 없음(0) |
| 0x1C / 0x1D | `PCI_IO_BASE` / `PCI_IO_LIMIT` | 8+8 | `[7:4]` → IO 주소 `[15:12]`, `[3:0]` 타입 `PCI_IO_RANGE_TYPE_16`=0 / `_32`=1 |
| 0x1E | `PCI_SEC_STATUS` | 16 | secondary 쪽 오류 비트 (PCIE-M12) |
| 0x20 / 0x22 | `PCI_MEMORY_BASE` / `PCI_MEMORY_LIMIT` | 16+16 | `[15:4]` → 주소 `[31:20]`, `[3:0]` RO 0 (`PCI_MEMORY_RANGE_MASK = ~0x0f`) |
| 0x24 / 0x26 | `PCI_PREF_MEMORY_BASE` / `_LIMIT` | 16+16 | 같은 모양, `[3:0]` = `PCI_PREF_RANGE_TYPE_32`=0 / `_64`=1 |
| 0x28 / 0x2C | `PCI_PREF_BASE_UPPER32` / `PCI_PREF_LIMIT_UPPER32` | 32+32 | 타입 64 일 때 주소 `[63:32]` |
| 0x30 / 0x32 | `PCI_IO_BASE_UPPER16` / `PCI_IO_LIMIT_UPPER16` | 16+16 | IO 타입 32 일 때 IO 주소 `[31:16]` |
| 0x38 | `PCI_ROM_ADDRESS1` | 32 | 브리지의 Expansion ROM (Type 0 은 0x30) |
| 0x3E | `PCI_BRIDGE_CONTROL` | 16 | 0x01 Parity, 0x02 SERR, 0x04 ISA, 0x08 VGA, 0x20 Master Abort, 0x40 Secondary Bus Reset, 0x80 FastB2B (VGA 16-bit decode 0x10 은 이 헤더 판엔 없지만 lspci 는 `VGA16` 으로 표시 — 스펙 확인 권장) |

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="14">윈도우 레지스터 → 주소 (00:02.0 실값으로)</text>
  <text x="10" y="44" fill="currentColor">Memory Base 0x20 = 0x1000</text>
  <g stroke="currentColor">
    <rect x="200" y="30" width="360" height="22" class="f-acc"/><rect x="560" y="30" width="120" height="22" class="f-mut"/>
  </g>
  <text x="260" y="46" fill="currentColor" font-family="monospace">[15:4] = 0x100 → addr[31:20]</text>
  <text x="572" y="46" fill="currentColor" font-family="monospace">[3:0]=0 RO</text>
  <text x="200" y="72" fill="currentColor" font-family="monospace">base  = 0x100 &lt;&lt; 20            = 0x1000_0000</text>
  <text x="10" y="104" fill="currentColor">Memory Limit 0x22 = 0x1060</text>
  <g stroke="currentColor">
    <rect x="200" y="90" width="360" height="22" class="f-acc"/><rect x="560" y="90" width="120" height="22" class="f-mut"/>
  </g>
  <text x="260" y="106" fill="currentColor" font-family="monospace">[15:4] = 0x106 → addr[31:20]</text>
  <text x="572" y="106" fill="currentColor" font-family="monospace">[3:0]=0 RO</text>
  <text x="200" y="132" fill="currentColor" font-family="monospace">limit = (0x106 &lt;&lt; 20) | 0xFFFFF = 0x106F_FFFF  (7MB)</text>
  <text x="10" y="168" fill="currentColor">Pref Base 0x24 = 0x0001</text>
  <g stroke="currentColor">
    <rect x="200" y="154" width="360" height="22" class="f-acc"/><rect x="560" y="154" width="120" height="22" class="f-warn"/>
  </g>
  <text x="260" y="170" fill="currentColor" font-family="monospace">[15:4] = 0x000 → addr[31:20]</text>
  <text x="565" y="170" fill="currentColor" font-family="monospace">[3:0]=1 64bit</text>
  <text x="10" y="200" fill="currentColor">Pref Base Upper32 0x28</text>
  <g stroke="currentColor"><rect x="200" y="186" width="480" height="22" class="f-ok"/></g>
  <text x="300" y="202" fill="currentColor" font-family="monospace">0x0000_0080 → addr[63:32]</text>
  <text x="200" y="228" fill="currentColor" font-family="monospace">base  = 0x80_0000_0000</text>
  <text x="200" y="246" fill="currentColor" font-family="monospace">limit = 0x0081, upper 0x80 → 0x80_008F_FFFF (9MB)</text>
  <text x="10" y="278" fill="currentColor">빈 윈도우 (Linux 가 쓰는 값): base 0xFFF0 → 0xFFF0_0000, limit 0x0000 → 0x000F_FFFF</text>
  <text x="10" y="296" fill="currentColor">→ base &gt; limit → valid = false (별도 enable 비트는 없다)</text>
  <text x="10" y="320" fill="currentColor">IO: 0x1C=0x10, 0x1D=0x40 → base 0x1000, limit 0x4FFF (4KB 단위, | 0xFFF)</text>
  <text x="10" y="336" fill="currentColor">타입 nibble 1 이면 0x30/0x32 가 IO 주소 [31:16]</text>
</svg>
```

**바이트 → 필드 추적 (루트 포트 00:02.0, `lspci -x` 0x10–0x2F 줄):**

```text
10: 00 10 78 10 00 00 00 00 00 01 05 00 10 40 00 00
20: 00 10 60 10 01 00 81 00 80 00 00 00 80 00 00 00
```

| 오프셋 | 바이트 | 계산 | 결과 |
|---|---|---|---|
| 0x18..0x1A | `00 01 05` | 그대로 | primary 00, secondary 01, subordinate 05 |
| 0x1C, 0x1D | `10`, `40` | 타입 0/0 → 16비트. `(0x10 & 0xF0) << 8`, `(0x40 & 0xF0) << 8 \| 0xFFF` | IO 0x1000–0x4FFF |
| 0x20 | `00 10` → 0x1000 | `0x1000 << 16` | mem base 0x1000_0000 |
| 0x22 | `60 10` → 0x1060 | `(0x1060 << 16) \| 0xFFFFF` | mem limit 0x106F_FFFF |
| 0x24 | `01 00` → 0x0001 | 타입 1 (64비트), `(0x0000) << 16` | pref base 하위 0 |
| 0x26 | `81 00` → 0x0081 | 타입 1, `(0x0080 << 16) \| 0xFFFFF` = 0x008F_FFFF | pref limit 하위 |
| 0x28, 0x2C | `80 00 00 00` ×2 | upper32 = 0x80 | pref 0x80_0000_0000 – 0x80_008F_FFFF |

`lspci -F` 로 같은 바이트를 디코드시킨 실제 출력 (`lspci_F_decode.txt`, pciutils 3.14.0):

```text
	Bus: primary=00, secondary=01, subordinate=05, sec-latency=0
	I/O behind bridge: 1000-4fff [size=16K] [16-bit]
	Memory behind bridge: 10000000-106fffff [size=7M] [32-bit]
	Prefetchable memory behind bridge: 0000008000000000-00000080008fffff [size=9M] [64-bit]
	BridgeCtl: Parity- SERR+ NoISA- VGA- VGA16- MAbort- >Reset- FastB2B-
```

게스트 `/proc/iomem` 도 `10000000-106fffff : PCI Bus 0000:01`, `8000000000-80008fffff : PCI Bus 0000:01` — 세 출처가 같다. `solution.c` 가 찍는 디코드 (실제 출력):

```text
00:02.0 RP  bus 00/01/05 cmd=0407 IO+ Mem+ BusMaster+
    io16 0x1000-0x4fff mem 0x10000000-0x106fffff pref64 0x8000000000-0x80008fffff bctl=0002 SERR
01:00.0 USP bus 01/02/05 cmd=0007 IO+ Mem+ BusMaster+
    io16 0x1000-0x3fff mem 0x10000000-0x106fffff pref64 0x8000000000-0x80008fffff bctl=0002 SERR
02:00.0 DSP bus 02/03/03 cmd=0407 IO+ Mem+ BusMaster+
    io16 0x1000-0x1fff mem 0x10000000-0x101fffff pref64 0x8000000000-0x80001fffff bctl=0002 SERR
02:01.0 DSP bus 02/04/04 cmd=0407 IO+ Mem+ BusMaster+
    io16 0x2000-0x2fff mem 0x10200000-0x103fffff pref64 0x8000200000-0x80003fffff bctl=0002 SERR
02:02.0 DSP bus 02/05/05 cmd=0407 IO+ Mem+ BusMaster+
    io16 0x3000-0x3fff mem 0x10400000-0x105fffff pref64 0x8000400000-0x80005fffff bctl=0002 SERR
```

DSP 윈도우가 NVMe BAR(16KB) 보다 훨씬 큰 2MB 씩인 이유: 브리지 윈도우 최소 단위가 1MB 이고, 이 DSP 들은 hot-plug 슬롯(`SltCap: HotPlug+`)이라 리눅스가 기본 예약값을 잡기 때문이다 — `drivers/pci/pci.c` 의 `DEFAULT_HOTPLUG_MMIO_SIZE (2*1024*1024)`, `DEFAULT_HOTPLUG_MMIO_PREF_SIZE (2*1024*1024)`, `DEFAULT_HOTPLUG_IO_SIZE (256)` (6.18 소스에서 확인). IO 256B 도 4KB 단위로 올림. 빈 슬롯 02:01.0 도 같은 크기를 받는다. 배정 알고리즘은 PCIE-A08.

**설계 선택.** `win_t` 에 `valid` 를 따로 두었지만 사실 `base <= a <= limit` 비교만으로도 base > limit 이면 자동으로 거짓이다(그래서 `valid` 를 빼먹어도 테스트가 다 통과한다 — 실측). `valid` 는 사람이 읽고 lint 할 때(연습 2) 쓰라고 둔 것. 반대로 **레지스터끼리** `base_reg < limit_reg` 로 비교하면 1MB 윈도우를 빈 것으로 보는 버그가 된다(7절).

### Part B — 주소 라우팅과 ID 라우팅

스위치를 소프트웨어는 이렇게 본다: USP 아래에 **내부 가상 버스**(usp.sec)가 하나 있고, 그 위에 DSP 들이 device 0, 1, 2… 로 붙어 있다. 각 DSP 의 secondary 가 링크 하나.

| 요청 | 받는 조건 | 결과 |
|---|---|---|
| 메모리, USP 로 들어옴 | USP `Mem+` 그리고 USP mem∪pref 윈도우 안 | 아니면 `NOT_MINE` (상위는 원래 이 주소를 여기로 안 보낸다) |
| (USP 가 받은 뒤) | `Mem+` 인 DSP 중 윈도우가 맞는 것 | 0개 `UR`, 1개 `DSP`, 2개+ `CONFLICT` |
| config, 목표 bus | `usp.sec <= bus <= usp.sub` | 아니면 `NOT_MINE` |
| bus == usp.sec | DSP 중 device 번호 == dev | `INTERNAL`(Type 0), 없으면 `UR` |
| usp.sec < bus | `dsp.sec <= bus <= dsp.sub` 인 DSP (sec > sub 는 빈 범위) | 0개 `UR`, 2개+ `CONFLICT` |
| bus == dsp.sec | dev == 0 | `DSP` Type 0 (Type 1→0 변환), dev ≠ 0 이면 `UR` |
| dsp.sec < bus <= dsp.sub | — | `DSP` Type 1 그대로 |

"링크 아래엔 device 0 뿐" 은 PCIe 의 점대점(point-to-point) 성질이다 — 리눅스도 `drivers/pci/probe.c only_one_child()` 에서 "A PCIe Downstream Port normally leads to a Link with only Device 0 on it (PCIe spec r3.1, sec 7.3.1)" 라며 dev 0 만 스캔한다. 예외는 ARI Forwarding(PCIE-A06).

**가상 스위치** (테스트 데이터, 레지스터 값으로 직접 만듦):

| 포트 | bus [pri/sec/sub] | mem 윈도우 | pref64 윈도우 |
|---|---|---|---|
| USP | 1 / 2 / 9 | C000_0000–C0FF_FFFF | 40_0000_0000–40_3FFF_FFFF |
| DSP0 dev0 | 2 / 3 / 3 | C000_0000–C00F_FFFF | 40_0000_0000–40_0FFF_FFFF |
| DSP1 dev1 | 2 / 4 / 6 (아래에 또 스위치) | C010_0000–C03F_FFFF | 40_1000_0000–40_1FFF_FFFF |
| DSP2 dev2 | 2 / 7 / 7 | 빈 (0xFFF0/0x0000) | 빈 |
| DSP3 dev3 | 2 / 8 / 8 | C080_0000–C08F_FFFF | 빈 |

구멍: C040_0000–C07F_FFFF, C090_0000–C0FF_FFFF, 40_2000_0000–40_3FFF_FFFF → `UR`. bus 9 는 USP 범위 안인데 아무 DSP 도 없음 → `UR`.

추적 몇 개:

- `0x40_0FFF_FFFF` → USP pref 안 → DSP0 pref limit 과 정확히 같음(포함) → `DSP 0`. `0x40_1000_0000` → `DSP 1`.
- `0x0010_0000` → USP mem(C0…) 아님, USP pref 는 상위 32비트가 0x40 이라 아님 → `NOT_MINE`. upper32 를 버리는 디코더는 pref 윈도우를 0–0x3FFF_FFFF 로 보고 이걸 DSP0 으로 보낸다(7절 실수 1).
- `0x1_C000_0000` → 하위 32비트는 C000_0000 이지만 mem 윈도우는 32비트 주소뿐 → `NOT_MINE`.
- config bus 5, dev 7 → DSP1 `[4, 6]` 안, bus ≠ 4 → `DSP 1, Type 1` (dev 는 더 아래에서 의미가 생긴다). bus 4 dev 0 → `DSP 1, Type 0`. bus 3 dev 1 → `UR`.

검증은 두 가지: bus 0..255 × dev 0..31 = 8192 조합 전수를 "버스 → 주인 표를 먼저 칠하는" 참조 모델과 비교, 그리고 메모리 주소 20000개(절반은 윈도우 경계 ±32, 절반은 40비트 랜덤)를 "주소 >> 20 의 1MB 번호를 12비트 필드와 직접 비교하는" 참조 모델과 비교. 둘 다 불일치 0.

### Part C — 위로 가는 요청과 진단

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="14">route_mem_up: DSP0 아래 NVMe 가 보낸 DMA (실캡처 스위치)</text>
  <g stroke="currentColor">
    <rect x="10" y="34" width="150" height="40" class="f-mut"/>
    <rect x="190" y="34" width="150" height="40" class="f-mut"/>
    <rect x="370" y="34" width="150" height="40" class="f-mut"/>
    <rect x="550" y="34" width="180" height="40" class="f-mut"/>
    <rect x="10" y="110" width="150" height="34" class="f-warn"/>
    <rect x="190" y="110" width="150" height="34" class="f-warn"/>
    <rect x="370" y="110" width="150" height="34" class="f-ok"/>
    <rect x="550" y="110" width="180" height="34" class="f-warn"/>
    <rect x="550" y="180" width="180" height="34" class="f-ok"/>
    <line x1="160" y1="54" x2="190" y2="54"/><line x1="340" y1="54" x2="370" y2="54"/><line x1="520" y1="54" x2="550" y2="54"/>
    <line x1="85" y1="74" x2="85" y2="110"/><line x1="265" y1="74" x2="265" y2="110"/><line x1="445" y1="74" x2="445" y2="110"/>
    <line x1="640" y1="74" x2="640" y2="110"/><line x1="640" y1="144" x2="640" y2="180"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="52">DSP0 BusMaster?</text><text x="18" y="68">아니오 ↓</text>
    <text x="198" y="52">DSP0 자기 윈도우?</text><text x="198" y="68">예 ↓</text>
    <text x="378" y="52">다른 DSP 윈도우?</text><text x="378" y="68">예 ↓</text>
    <text x="558" y="52">USP 윈도우 안?</text><text x="558" y="68">예 ↓ / 아니오 → USP BM</text>
    <text x="18" y="132">UR (DMA 막힘)</text>
    <text x="198" y="132">UR (아래 것은 아래로)</text>
    <text x="378" y="132">DSP j (스위치 안 P2P)</text>
    <text x="558" y="132">UR (구멍 10600000-)</text>
    <text x="558" y="202">UPSTREAM (→ RAM 4000_0000)</text>
  </g>
  <text x="10" y="236" fill="currentColor">예: 0x40000000 → UPSTREAM, 0x10400000 → DSP2 (옆 NVMe BAR), 0x10000000 → UR, 0x10600000 → UR</text>
</svg>
```

"자기 윈도우 안 주소가 secondary 에서 올라오면 전달하지 않는다(UR)" 는 브리지 윈도우의 반대편 디코드(inverse decode) 규칙이다 — PCI-PCI 브리지는 primary 쪽에선 윈도우 **안**을, secondary 쪽에선 윈도우 **밖**을 받아 넘긴다. PCIe 스위치에서 이 경우를 UR 로 처리하는 세부 문구는 스펙 확인 권장. P2P 요청이 실제로 옆 DSP 로 가는지는 ACS(Access Control Services) 설정에 따라 루트까지 강제로 올라갈 수도 있다(PCIE-A08) — 이 모델은 ACS 꺼짐 가정.

`diag_ep_bar` 진단 순서와 실캡처에서의 결과:

| 입력 | 결과 코드 | 의미 |
|---|---|---|
| DSP0, 0x1000_0000, 16KB | `DIAG_OK` | 03:00.0 의 진짜 BAR0 (iomem `10000000-10003fff`) |
| DSP0, 0x1070_0000 | `DIAG_NOT_IN_USP` | 스위치 밖 (virtio 의 BAR 자리) — 상위에서 이 스위치로 안 온다 |
| DSP0, 0x1040_0000 | `DIAG_NOT_IN_DSP` | USP 는 받지만 DSP2 로 간다 |
| DSP0, 0x101F_F000, 8KB | `DIAG_NOT_IN_DSP` | 시작은 안, 끝(0x1020_0FFF)은 DSP1 쪽 — 반만 닿는 BAR |
| DSP0 `Mem-` | `DIAG_DSP_MEM_OFF` | |
| USP `Mem-` | `DIAG_USP_MEM_OFF` | |
| 가상 스위치, DSP3 mem 을 C000_0000 부터로 망가뜨림 | `DIAG_SHADOWED` | 두 DSP 가 같은 주소를 주장 |

실제 브링업에서 이 순서는 그대로 체크리스트가 된다: 엔드포인트 BAR 값(PCIE-B03, B05) → 바로 위 DSP 의 `Control: Mem+`·윈도우 → USP → 루트 포트 → 호스트 브리지의 CPU 주소 범위(DT `ranges`, PCIE-B08). 어느 한 단이라도 `Mem-` 이거나 윈도우가 BAR 를 다 덮지 않으면 요청은 UR 로 끝나고 CPU 는 all-1s 를 읽는다(아키텍처에 따라 SError/machine check).

### 복잡도와 자원

| 함수 | 시간 | 메모리 |
|---|---|---|
| `t1_decode` | O(1), config 읽기 ~15회 | 호출자 구조체 |
| `route_mem_down` / `route_mem_up` / `route_cfg` | O(N_DSP) | 없음 |
| `diag_ep_bar` | O(N_DSP) | 없음 |
| 테스트 전체 | 8192 + 20000 라우팅, 0.01 초 미만 (실측 0.008 s) | 정적 배열만 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 75/75 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 75/75, 오류 없음
```

테스트 외 출력은 5절의 디코드 표 다섯 줄과 마지막 진단 한 줄이다:

```text
diag(03:00.0 BAR @0x10700000): BAR 가 USP 윈도우 밖 (또는 걸침) → 상위에서 이 스위치로 안 옴
75/75 passed
```

## 7. 자주 하는 실수

아래 숫자는 `solution.c` 에서 해당 줄 하나만 바꿔 돌린 실측 결과다.

**1. Prefetchable upper 32비트를 버린다 → 65/75.** 실캡처 pref 윈도우가 0x80_0000_0000 이 아니라 0x0 으로 디코드되고, 가상 스위치에서 `0x0010_0000` 이 DSP0 로 잘못 간다. 고친 코드: 타입 nibble 이 1 이면 `base |= (uint64_t)cfg_rd32(cfg, 0x28) << 32; limit |= (uint64_t)cfg_rd32(cfg, 0x2C) << 32;` — `uint64_t` 캐스트를 시프트 **전에**.

**2. limit 에 `| 0xFFFFF` 를 안 붙인다 → 66/75.** 윈도우가 마지막 1MB 의 첫 바이트에서 끝나 `0x106F_FFFF` 대신 `0x1060_0000`. 마지막 1MB 안의 BAR 가 전부 UR. 고친 코드: limit 은 "그 단위의 마지막 바이트" — mem/pref 는 `| 0xFFFFF`, IO 는 `| 0xFFF`.

**3. Prefetchable 타입 nibble 을 지우지 않는다 → 67/75.** `0x0001 << 16` = `0x1_0000` 이 base 에 섞여 `0x80_0001_0000` 이 된다. 고친 코드: `& PCI_PREF_RANGE_MASK` (`~0x0f`) 후 시프트.

**4. 레지스터끼리 `base_reg < limit_reg` 로 빈 윈도우를 판정한다 → 69/75.** 0xC000/0xC000 (1MB 윈도우) 이 빈 것으로 판정돼 DSP0·DSP3 가 통째로 사라진다. 고친 코드: 확장한 주소로 `base <= limit` (리눅스 `pci_read_bridge_mmio` 도 `if (base <= limit)`).

**5. `bus == dsp.sec` 에서도 Type 1 로 넘긴다 → 70/75.** 엔드포인트는 Type 1 config 를 무시(UR)하므로 열거에서 장치가 하나도 안 보인다. 고친 코드: 링크 바로 아래 버스면 Type 0 으로 변환하고 dev ≠ 0 은 UR.

**6. Subordinate 를 안 보고 `bus == dsp.sec` 만 비교한다 → 72/75.** DSP1 아래 둘째 스위치(bus 5, 6)로 가는 config 가 전부 UR. 열거 중 subordinate 를 늦게 쓰는 버그(PCIE-B09)와 같은 증상이다.

**7. DSP 의 Memory Space Enable 을 안 본다 → 74/75.** 시뮬레이션은 "됐다" 고 하는데 실보드에선 UR. 리셋 직후 브리지 Command 는 0 이고, 펌웨어/OS 가 켜 줘야 한다.

**8. (테스트 밖) 윈도우를 16비트씩 두 번 써서 바꾼다.** base 를 먼저 새 값으로, limit 을 나중에 쓰면 그 사이 잠깐 `[새 base, 옛 limit]` 이라는 엉뚱한 윈도우가 생긴다(진행 중인 DMA/MMIO 가 있으면 잘못 라우팅). 리눅스는 `PCI_MEMORY_BASE` 에 base+limit 을 **dword 한 번**에 쓰고, pref 는 먼저 `PCI_PREF_LIMIT_UPPER32 = 0` 으로 "잠깐 끈" 다음 갱신, IO 는 `PCI_IO_BASE_UPPER16` 에 `0x0000ffff` 를 써 끈 뒤 갱신한다 (`drivers/pci/setup-bus.c pci_setup_bridge_io/_mmio/_mmio_pref`, 6.18 소스 확인). 더 안전하게는 Command.Mem 을 끈 채 바꾼다(PCIE-B05 와 같은 이유).

## 8. 면접 Q&A

<details><summary>Q1. Type 0 과 Type 1 헤더는 뭐가 다르고, 소프트웨어는 어떻게 구분하나요?</summary>

0x0E Header Type 의 하위 7비트: 0 = 일반 장치(Type 0), 1 = PCI-PCI 브리지(Type 1), 2 = CardBus. 비트 7 은 multi-function. 0x00–0x17 과 0x34, 0x3C/0x3D 는 공통이고, Type 0 은 BAR 6개 + Subsystem ID + ROM(0x30), Type 1 은 BAR 2개 뒤 0x18 부터 버스 번호·윈도우, ROM 은 0x38, 0x3E 가 Bridge Control. 루트 포트·스위치 포트는 전부 Type 1, 엔드포인트(NVMe 등)는 Type 0. 실캡처의 NVMe 03:00.0 을 `t1_decode` 에 넣으면 `T1_ERR_NOT_TYPE1`. 추가로 PCIe Capability 의 Device/Port Type(0x4 Root Port, 0x5 Upstream, 0x6 Downstream — `PCI_EXP_TYPE_*`)으로 어떤 브리지인지까지 안다(PCIE-B04).

</details>

<details><summary>Q2. Memory Limit 은 왜 "하위 20비트를 1로 채운" 값인가요? 윈도우가 1MB 단위인 이유는?</summary>

레지스터에 12비트(주소 [31:20])만 있기 때문이다. limit 은 포함(inclusive)이라, "마지막 1MB 의 번호" 를 저장하고 그 1MB 의 마지막 바이트까지 덮는다. 1MB 단위는 비교기를 싸게 만들려는 선택(12비트 비교 두 번). 대가로 16KB BAR 하나를 위해서도 최소 1MB 를 쓴다 — 실캡처 DSP 는 hot-plug 예약까지 2MB. 32비트 주소 공간에서 브리지가 많으면 이 낭비가 리소스 할당 실패로 이어진다(PCIE-A08). IO 는 4KB 단위(`[7:4]` → [15:12]).

</details>

<details><summary>Q3. Prefetchable 윈도우와 non-prefetchable 윈도우는 왜 나뉘어 있나요? 64비트 BAR 는 어디에 들어가나요?</summary>

Prefetchable = 읽어도 부작용이 없고 합치기(write merging)/미리 읽기가 허용되는 메모리(프레임버퍼, 큰 데이터 BAR). Non-prefetchable = 읽으면 상태가 바뀌는 레지스터(인터럽트 상태 read-to-clear, FIFO). 브리지(특히 구형 PCI 브리지)가 prefetch 를 해도 되는 범위를 구분하려고 둘로 나눴다. Non-prefetchable 윈도우는 **32비트 주소만** 표현할 수 있어서, 4GB 위에 놓고 싶은 64비트 BAR 는 prefetchable 윈도우(upper32 가 있음)에만 들어간다. 그래서 NVMe 처럼 64비트지만 non-prefetchable 인 BAR0 은 보통 4GB 아래에 놓인다(실캡처 03:00.0 BAR0 = 0x1000_0000, `04 00 00 10` → 64비트 non-pref). PCIe 3.0 이후 조건부로 non-pref 64비트 BAR 를 pref 윈도우에 두는 것을 허용한 문구가 있다 — 스펙 확인 권장.

</details>

<details><summary>Q4. 부팅 후 NVMe BAR 를 읽으면 0xFFFFFFFF 입니다. 무엇부터 보나요?</summary>

1) `lspci -s <EP> -vv` 의 `Control: Mem+` 와 BAR 값(0 이거나 미할당이면 B05). 2) 위로 올라가며 각 브리지(`lspci -tv` 로 경로 파악)의 `Control: Mem+`, `Memory behind bridge` 가 BAR 범위 전체를 덮는지, `Bus: secondary/subordinate` 가 EP 버스를 포함하는지 — 이 레슨의 `diag_ep_bar` 순서. 3) 링크: DSP/루트 포트의 `LnkSta: DLActive+`, 장치가 `lspci` 에 보이는지(config 도 0xFFFF 면 링크/전원/PERST# 문제, PCIE-M08). 4) 호스트 브리지 쪽 CPU 주소 매핑(DT `ranges`). 5) surprise removal/DPC 로 링크가 내려간 경우(PCIE-A07). config 는 읽히는데 MMIO 만 all-1s 면 거의 항상 2번(윈도우·Mem 비트)이다.

</details>

<details><summary>Q5. MMIO 는 되는데 장치의 DMA 가 메모리에 안 써집니다. 왜일 수 있나요?</summary>

DMA 는 아래→위 방향이라 **경로상 모든 브리지의 Bus Master Enable** 이 필요하다(엔드포인트 자신 포함). MMIO(위→아래)는 Memory Space Enable 만 보므로 BME 가 빠져도 MMIO 는 된다 — 증상이 정확히 갈린다. 리눅스는 드라이버가 `pci_enable_device()` 를 부르면 `pci_enable_bridge()` 가 상위 브리지마다 `pci_set_master()` 를 해 준다(drivers/pci/pci.c, 6.18 확인). 엔드포인트 BME 는 드라이버가 `pci_set_master()` 로 직접. 그 다음 후보는 IOMMU(잘못된 IOVA, PCIE-M15), 그리고 주소가 다른 DSP 윈도우에 걸려 P2P 로 새는 경우(`route_mem_up`).

</details>

<details><summary>Q6. Config 요청이 스위치를 지나는 규칙을 설명해 보세요. 열거할 때 Subordinate 를 왜 먼저 0xFF 로 두나요?</summary>

Type 1 config 의 목표 bus 가 브리지의 secondary 와 같으면 Type 0 으로 바꿔 링크로 내보내고(dev 0 만), secondary < bus ≤ subordinate 면 Type 1 그대로 아래로, 범위 밖이면 무시. 열거(PCIE-B09) 중에는 아래에 버스가 몇 개 있을지 모르니 subordinate 를 임시로 0xFF 로 열어 둬야 더 깊은 버스로 가는 Type 1 이 통과한다. 다 훑고 나서 실제 최댓값으로 줄인다. 실수 6 이 바로 이게 막힌 상태.

</details>

<details><summary>Q7. Bridge Control 의 Secondary Bus Reset 은 언제 쓰나요?</summary>

브리지 아래 링크에 hot reset 을 거는 비트(0x40). 엔드포인트 펌웨어가 멈췄을 때 FLR 을 지원하지 않거나 FLR 로 안 풀리면 쓰는 리셋 수단이다(VFIO 장치 리셋, 검증에서 "리셋 후 재열거" 반복 테스트). 리눅스 `pci_reset_secondary_bus()` 는 비트를 세우고 `msleep(2)` 후 내린다 — 주석: "PCI spec v3.0 7.6.4.2 requires minimum Trst of 1ms. Double this to 2ms" (6.18 소스). 그 뒤 장치가 준비될 때까지 기다리고(config 재시도/CRS) config 공간을 복원해야 한다 — 리셋되면 엔드포인트 BAR·Command 가 0 으로 돌아가기 때문. SSD 쪽에서 보면 이 hot reset 은 PERST# 없는 링크 리셋이라, 펌웨어가 어떤 상태를 유지하고 무엇을 초기화하는지가 스펙(그리고 제품 요구사항)으로 정해져 있어야 한다.

</details>

<details><summary>Q8. 같은 스위치 아래 SSD 두 개 사이 P2P DMA 는 누가 라우팅하나요? 루트 포트까지 가나요?</summary>

스위치가 직접 한다: ingress DSP 가 내부 버스로 올리고(BME), 주소가 맞는 다른 DSP 가 받는다(Mem) — 루트 포트를 안 거친다(이 레슨 `route_mem_up(DSP0, 0x10400000)` → DSP2). 그래서 지연·대역폭이 좋지만, IOMMU 를 우회한다는 보안 문제가 있다. ACS P2P Request Redirect 를 켜면 스위치가 이런 요청을 강제로 위로 올려 루트(IOMMU)가 검사하게 한다 — 가상화에서 장치를 격리할 때 필요(IOMMU group). 자세히는 PCIE-A08, PCIE-M15.

</details>

<details><summary>Q9. lspci 의 "Memory behind bridge: 10000000-106fffff [size=7M]" 를 레지스터로 바꿔 보세요.</summary>

base 0x1000_0000 → `[31:20]` = 0x100 → `PCI_MEMORY_BASE = 0x1000`. limit 0x106F_FFFF → `[31:20]` = 0x106 → `PCI_MEMORY_LIMIT = 0x1060`. 리눅스처럼 dword 한 번으로 쓰면 `0x10601000` 을 0x20 에. 크기 = (0x106 − 0x100 + 1) MB = 7MB. 실캡처 바이트 `00 10 60 10` 과 일치. 반대 방향 인코더는 연습 1.

</details>

## 9. 추가 연습

### 연습 1 — 윈도우 인코더 (펌웨어가 쓰는 쪽)

`t1_set_mem_window(cfg, base, limit)`, `t1_set_pref_window(cfg, base, limit)`, `t1_disable_mem_window(cfg)` 를 작성하라. 1MB 정렬(`base % 1MB == 0`, `(limit+1) % 1MB == 0`), `base <= limit`, mem 은 4GB 미만, pref 는 브리지가 32비트형이면 4GB 미만. pref 의 타입 nibble 은 하드웨어가 정하는 읽기전용이므로 **보존**한다. 디코더와 왕복 검증.

<details><summary>답</summary>

`reg = (base >> 16) & 0xFFF0`, `reg_limit = (limit >> 16) & 0xFFF0`, pref 면 `| type` 하고 타입 64 면 upper32 에 `base >> 32`, `limit >> 32`. 빈 윈도우는 0xFFF0 / 0x0000. 검증: base 필드 4096개 전수 × (1MB 윈도우, 4GB 끝까지) 8192건 왕복, 64비트 pref 10000건 랜덤 왕복, 비정렬/순서/범위 오류, `limit = 2^64−1` (`limit + 1` 이 0 으로 넘쳐도 `% 1MB == 0` 이라 정렬로 통과 — 의도한 동작), 32비트 pref 브리지에 4GB 위 → `W_ERR_RANGE`. 진짜 하드웨어에선 하위 nibble 이 읽기전용이라 써도 무시되지만, mock 은 바이트 배열이므로 코드가 보존해야 한다.

</details>

### 연습 2 — 스위치 설정 lint (validation 체크리스트)

`sw_lint(sw)` 가 비트마스크를 돌려주게 하라: `LINT_PRI_MISMATCH`(DSP primary ≠ USP secondary), `LINT_BUS_OUTSIDE`(DSP 버스 범위가 `(usp.sec, usp.sub]` 밖), `LINT_BUS_OVERLAP`, `LINT_WIN_OUTSIDE`(DSP mem 이 USP mem 안, pref 가 USP pref 안이 아님), `LINT_WIN_OVERLAP`(DSP 간 mem/pref 어느 조합이든 겹침).

<details><summary>답</summary>

DSP 마다 포함 검사, DSP 쌍마다 겹침 검사(`a.base <= b.limit && b.base <= a.limit`, 빈 윈도우 제외) — O(N²), N ≤ 8 이라 충분. 실캡처 스위치(리눅스가 할당)와 가상 스위치는 0, 망가뜨린 6가지는 기대 비트. 주의: 한 번에 두 가지가 깨질 수 있다 — DSP0 mem limit 을 C10F_FFFF 로 늘리면 USP 밖으로 나가면서 DSP1 과도 겹쳐 `WIN_OUTSIDE | WIN_OVERLAP`. 검증 현장에선 이 lint 를 "열거 직후 / hot-plug 후 / 리셋 후 config 복원 후" 에 돌린다. 리눅스가 같은 문제를 만나면 dmesg 에 bridge window 할당 실패/claim 실패 메시지를 남긴다(정확한 문구는 버전마다 다름).

</details>

### 연습 3 — 여러 단을 건너는 경로

브리지 목록(`bridge_node{bdf, t1_hdr}`)에서 루트 버스 0 부터 `path_to_bus(bus)` 와 `path_to_addr(addr)` 를 구하라: 현재 버스를 primary 로 가진 브리지 중 범위/윈도우가 맞는 것을 따라 내려간다. 버스 번호가 안 늘어나는 잘못된 설정(sec == pri)에서 끝없이 돌지 않게 하라.

<details><summary>답</summary>

현재 버스 `cur` 에서 `pri == cur` 이고 bus 가 `[sec, sub]` 안인(주소면 `Mem+` 이고 윈도우 안인) 브리지를 찾아 `cur = sec`. bus 경로는 `cur == bus` 에서 끝, 못 찾으면 `PATH_ERR_NOROUTE`. 주소 경로는 더 내려갈 브리지가 없으면 그 버스에서 끝(그 버스의 장치 BAR 가 받거나, 아무도 없으면 UR). hop 이 `cap` 또는 256 을 넘으면 `PATH_ERR_LOOP`. 실캡처 결과 (실제 출력): `path to bus 05: 00:02.0 -> 01:00.0 -> 02:02.0 -> bus 05`. `0x10600000` 은 bus 02(스위치 내부 버스)에서 멈춘다 — 받을 DSP 가 없는 구멍. `0x10781000`(루트 포트 자신의 BAR0)은 0 hop, bus 00. 이것이 `lspci -tv` 의 `\-02.0-[01-05]----00.0-[02-05]--+-00.0-[03]` 를 코드로 걷는 것과 같다.

</details>

### 연습 4 — IO 라우팅과 VGA Enable

IO 주소(포트 번호)도 같은 식으로 라우팅하라(IO Space Enable + IO 윈도우). 그리고 Bridge Control VGA Enable(0x08)이 켜진 브리지는 윈도우와 상관없이 레거시 VGA 범위 — 메모리 0xA0000–0xBFFFF, IO 0x3B0–0x3BB 와 0x3C0–0x3DF — 도 받아 넘긴다. 이 모델은 IO 를 16비트 전체 디코드(VGA16 = 1)로 가정한다.

<details><summary>답</summary>

`claims_io(b, p) = IO+ && (in io window || (VGA && vga_io(p)))`, 메모리도 같은 모양. USP 가 받고 → DSP 중 하나. 가상 스위치: 0x1000 → DSP0, 0x2FFF → DSP1, 0x3000 → DSP3, 0x4000 → NOT_MINE. USP 에만 VGA 를 켜면 0x3C0 은 UR(아래에 받는 DSP 없음), DSP1 에도 켜면 DSP1. 0x3BC, 0x3E0 은 VGA 범위 밖 → NOT_MINE. DSP1 의 IO 비트를 끄면 0x3C0 도 0x2000 도 UR. VGA16 = 0 이면 하위 10비트만 디코드해 0x7C0 같은 별칭(alias)도 받는다 — 스펙 확인 권장. ISA Enable(0x04)은 첫 64KB 의 IO 윈도우 안에서 각 1KB 블록의 위쪽 768바이트를 **넘기지 않는** 비트다(레거시 ISA 카드 보호) — 스펙 확인 권장. 리눅스는 VGA 를 켤 브리지를 하나로 정하는 중재기(vgaarb)를 둔다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 39/39 passed, exit 0
```

```text
path to bus 05: 00:02.0 -> 01:00.0 -> 02:02.0 -> bus 05
02:00.0 DSP bus 02/03/03 cmd=0407 IO+ Mem+ BusMaster+
    io16 0x1000-0x1fff mem 0x10000000-0x101fffff pref64 0x8000000000-0x80001fffff bctl=0002 SERR
39/39 passed
```

## 10. 핵심 정리

- Type 1 = 브리지. 0x18/0x19/0x1A = Primary/Secondary/Subordinate, 0x1C/0x1D(+0x30/0x32) IO, 0x20/0x22 Memory, 0x24/0x26(+0x28/0x2C) Prefetchable, 0x3E Bridge Control, BAR 는 0x10/0x14 두 개뿐, ROM 은 0x38.
- 윈도우 = "단위 번호" 레지스터: mem/pref `base = (reg & 0xFFF0) << 16`, `limit = … | 0xFFFFF` (1MB 단위, 포함), IO 는 4KB 단위 `| 0xFFF`. 하위 nibble 은 타입(지운 뒤 시프트). pref 타입 1 → upper32 를 붙인다.
- 빈 윈도우 = base > limit (리눅스: 0xFFF0/0x0000). base_reg == limit_reg 는 1MB 윈도우다.
- 주소 라우팅: 위→아래는 Mem/IO Space Enable + 윈도우 안, 아래→위는 Bus Master Enable + 윈도우 밖. 스위치 안 P2P 는 USP 를 안 거친다(ACS 가 바꿀 수 있음).
- ID 라우팅: bus == secondary → Type 0 변환(링크 아래 dev 0 만), secondary < bus ≤ subordinate → Type 1 그대로, 밖이면 무시.
- 증상 → 레지스터: BAR all-1s = 경로상 `Mem-` 또는 윈도우가 BAR 를 다 못 덮음. DMA 실패 = 경로상 `BusMaster-`. 장치가 안 보임 = 버스 범위/subordinate 또는 링크.
- 윈도우 갱신은 글리치 없이: dword 한 번, 또는 먼저 끄고(upper32=0, IO upper16=0x0000ffff) 바꾼다.
- QEMU 실캡처로 검증: 디코드 = `lspci -vv` = `/proc/iomem` (예: 루트 포트 mem 10000000-106fffff, pref64 8000000000-80008fffff).

## 참고자료

- Linux `include/uapi/linux/pci_regs.h` — Type 1 오프셋과 비트 (`PCI_PRIMARY_BUS` … `PCI_BRIDGE_CTL_*`). 이 레슨은 Lima VM 의 `/usr/include/linux/pci_regs.h` 로 대조.
- Linux 6.18 `drivers/pci/probe.c` — `pci_read_bridge_io/_mmio/_mmio_pref`(윈도우 디코드, `base <= limit`), `only_one_child()`(링크 아래 dev 0). `drivers/pci/setup-bus.c` — `pci_setup_bridge_io/_mmio/_mmio_pref`(0xFFF0 빈 윈도우, 글리치 방지 순서). `drivers/pci/pci.c` — `DEFAULT_HOTPLUG_*_SIZE`, `pci_enable_bridge()`, `pci_reset_secondary_bus()`. (`/var/lab/src/linux-6.18.55` 에서 확인)
- PCI-SIG, *PCI-to-PCI Bridge Architecture Specification* Rev 1.2 — Base/Limit 레지스터, Command 비트의 방향별 의미, Bridge Control(ISA/VGA/VGA16) 세부.
- PCI-SIG, *PCI Express Base Specification* — Type 1 Configuration Space Header, Switch 의 주소/ID 라우팅, ACS. (회원 문서 — 세부 문구는 원문 확인 권장)
- pciutils `lspci -F <file>` — 저장해 둔 config 덤프를 디코드시키는 옵션 (이 레슨 `lspci_F_decode.txt`, pciutils 3.14.0).
- 이 레슨 데이터: `qemu_switch_capture.txt` (QEMU `pcie-root-port` + `x3130-upstream` + `xio3130-downstream` ×3 + `nvme` ×2, 게스트 config 덤프·`/proc/iomem`·`/proc/ioports`), `lspci_F_decode.txt`.
- 관련 레슨: PCIE-B03 (Type 0 헤더, Command/Status), PCIE-B04 (PCIe Capability, Port Type, LnkSta), PCIE-B05 (BAR sizing, 디코드 끄기), PCIE-B06 (lspci/setpci 실전), PCIE-B08 (ECAM, DT `pci-host-ecam-generic`), PCIE-B09 (DFS 열거, subordinate 0xFF), PCIE-B11 (ordering, read-back), PCIE-M08 (링크 장애 진단), PCIE-M12 (AER, Secondary Status), PCIE-M15 (IOMMU), PCIE-A06 (ARI), PCIE-A07 (hot-plug, DPC), PCIE-A08 (윈도우 할당, P2P/ACS), BM-B04 (레지스터 RMW 함정), OS-M01 (주소 변환)
