---
id: PCIE-B05
time: 60분
updated: 2026-11-15
---
# PCIE-B05 BAR sizing과 디코드: all-1s 쓰기 알고리즘

## 학습 목표

- BAR(Base Address Register) 하위 비트를 보고 종류를 바로 말한다: bit0=1 이면 IO, 아니면 bits 2:1 = 00 Mem32 / 10 Mem64(다음 BAR 와 한 쌍), bit3 = prefetchable.
- "저장 → 0xFFFFFFFF 쓰기 → 읽기 → 복원" sizing 루틴을 Linux 6.18 `pci_read_bases()` 와 **같은 순서**로 짜고, type 비트를 지운 값의 **가장 낮은 1 비트 = 크기** 라는 걸 증명한다.
- 64비트 BAR 는 두 dword 를 합쳐서 크기를 구한다 — 상위 절반을 따로 BAR 로 보면 4바이트짜리 유령 IO BAR 가 생기고, 4GB 넘는 BAR 는 아예 사라진다(직접 재현).
- sizing 중 Command 의 Memory/IO decode 를 꺼야 하는 이유를 숫자로 보인다: decode 를 켠 채로 돌리면 mock 이 "반쯤 쓰인 BAR" 가 남의 주소(0xFFFFF000 등)를 가로챈 3건을 기록한다.
- 반대편(SSD 컨트롤러 엔드포인트 FW)에서 BAR 크기를 정하는 방법(DWC 스타일 BAR mask = size − 1)을 구현하고, EP 설정 → 실리콘 → 호스트 sizing 738가지 왕복을 전수 검증한다.

## 1. 문제

드라이버 probe, 보드 bring-up, 실리콘 검증 면접에서 "BAR 크기는 어떻게 알아내죠?" 는 PCIe 기본기의 리트머스 시험이다. 답("all-1s 를 써서 읽는다")은 누구나 알지만, **type 비트 마스킹 · 64비트 쌍 · decode 끄기 · 복원** 네 가지를 다 말하는 사람은 드물다.

호스트는 장치가 MMIO 영역을 얼마나 원하는지 모른다. 그래서 PCI 는 아주 싼 하드웨어 약속을 만들었다. **BAR 의 주소 비트 중 "크기보다 작은 비트"는 0 으로 고정(hardwired)하고, 나머지만 쓰기 가능하게 한다.** 그러면 호스트가 0xFFFFFFFF 를 써도 쓰기 가능한 비트만 1 이 되고, 읽어 보면 "어디서부터 0 인지" 로 크기가 드러난다. 4KB BAR 는 bit 11..0 이 고정이므로 `0xFFFFF000` 이 읽힌다 → 크기 0x1000.

Don 이 SSD 펌웨어를 할 때 컨트롤러의 BAR0 (NVMe 레지스터 + doorbell, PCIE-B12) 크기는 **엔드포인트 쪽** 설정이었다. 호스트 쪽에서 그 숫자를 읽어 내는 과정이 이 레슨이고, 이걸 잘못하면 bring-up 때 `lspci` 에 `[size=4]` 짜리 유령 BAR 가 뜨거나, GPU 의 8GB BAR 가 "unassigned" 로 사라진다.

이 레슨의 mock 디바이스 (가상 장치, 이름은 "가상 SSD 컨트롤러 + 디버그 포트"):

| BAR | 오프셋 | 종류 | 크기 | 펌웨어가 써 둔 주소 | 0xFFFFFFFF 쓰고 읽은 값 |
|---|---|---|---|---|---|
| 0 | 0x10 | IO | 256 B | 0x1000 | `0xFFFFFF01` |
| 1 | 0x14 | Mem32, non-prefetchable | 4 KB | 0x1001_0000 | `0xFFFFF000` |
| 2 | 0x18 | Mem64, prefetchable (하위) | 256 MB | 0x80_0000_0000 | `0xF000000C` |
| 3 | 0x1C | (BAR2 의 상위 32비트) | — | 0x80 | `0xFFFFFFFF` |
| 4 | 0x20 | Mem32, non-prefetchable | 64 KB | 0x1000_0000 | `0xFFFF0000` |
| 5 | 0x24 | 구현 안 됨 | — | 0 | `0x00000000` |

Command 레지스터(0x04)는 처음에 `0x0007` (IO | Memory | Bus Master). 상수 이름과 값은 Linux `include/uapi/linux/pci_regs.h` 기준이다: `PCI_BASE_ADDRESS_0 = 0x10`, `PCI_BASE_ADDRESS_SPACE_IO = 0x01`, `PCI_BASE_ADDRESS_MEM_TYPE_MASK = 0x06`, `MEM_TYPE_32 = 0x00`, `MEM_TYPE_1M = 0x02`(obsolete), `MEM_TYPE_64 = 0x04`, `MEM_PREFETCH = 0x08`, `MEM_MASK = ~0x0f`, `IO_MASK = ~0x03`, `PCI_COMMAND_IO = 0x1`, `PCI_COMMAND_MEMORY = 0x2`, `PCI_COMMAND_MASTER = 0x4`.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">BAR 한 dword 의 비트 배치 (pci_regs.h 기준)</text>
  <text x="10" y="52" fill="currentColor">IO</text>
  <g stroke="currentColor">
    <rect x="90" y="34" width="470" height="30" class="f-ok"/>
    <rect x="560" y="34" width="70" height="30" class="f-mut"/>
    <rect x="630" y="34" width="70" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="200" y="54">bits 31..2 IO 주소 (크기 정렬)</text>
    <text x="568" y="54">1: 예약 0</text>
    <text x="638" y="54">0: 1 = IO</text>
  </g>
  <text x="10" y="102" fill="currentColor">Memory</text>
  <g stroke="currentColor">
    <rect x="90" y="84" width="400" height="30" class="f-ok"/>
    <rect x="490" y="84" width="70" height="30" class="f-warn"/>
    <rect x="560" y="84" width="70" height="30" class="f-acc"/>
    <rect x="630" y="84" width="70" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="170" y="104">bits 31..4 주소 (크기 정렬)</text>
    <text x="497" y="104">3: Pref</text>
    <text x="566" y="104">2:1 Type</text>
    <text x="638" y="104">0: 0 = Mem</text>
  </g>
  <text x="90" y="132" fill="currentColor" font-size="12">Type: 00 = 32비트, 01 = 1M 아래 (obsolete, Linux 는 32비트로 취급)</text>
  <text x="90" y="150" fill="currentColor" font-size="12">      10 = 64비트 (다음 BAR 가 상위 32비트), 11 = 예약</text>
  <text x="10" y="172" fill="currentColor" font-weight="bold">예: mock 의 BAR2/BAR3 (Mem64 prefetchable 256MB) 에 0xFFFFFFFF 를 쓰고 읽으면</text>
  <g stroke="currentColor">
    <rect x="90" y="186" width="120" height="34" class="f-ok"/>
    <rect x="210" y="186" width="280" height="34" class="f-mut"/>
    <rect x="490" y="186" width="70" height="34" class="f-warn"/>
    <rect x="560" y="186" width="70" height="34" class="f-acc"/>
    <rect x="630" y="186" width="70" height="34" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="96" y="201">31..28 쓰기 가능</text><text x="120" y="215" font-family="monospace">1111</text>
    <text x="250" y="201">27..4 크기보다 작은 비트 = 0 고정</text><text x="300" y="215" font-family="monospace">0000 … 0000</text>
    <text x="515" y="208" font-family="monospace">1</text><text x="585" y="208" font-family="monospace">10</text><text x="655" y="208" font-family="monospace">0</text>
  </g>
  <text x="90" y="240" fill="currentColor" font-size="12">BAR2 읽은 값 = </text><text x="190" y="240" fill="currentColor" font-family="monospace" font-size="13">0xF000000C</text>
  <text x="300" y="240" fill="currentColor" font-size="12">BAR3 읽은 값 = </text><text x="400" y="240" fill="currentColor" font-family="monospace" font-size="13">0xFFFFFFFF</text>
  <g stroke="currentColor">
    <rect x="90" y="256" width="610" height="30" class="f-mut"/>
  </g>
  <text x="100" y="276" fill="currentColor" font-family="monospace" font-size="13">합치기: 0xFFFFFFFF_F000000C  &amp; ~0xF  =  0xFFFFFFFF_F0000000</text>
  <text x="100" y="310" fill="currentColor" font-family="monospace" font-size="13">가장 낮은 1 비트 = bit 28 → 크기 0x1000_0000 = 256 MB</text>
  <text x="100" y="334" fill="currentColor" font-family="monospace" font-size="13">검사: m | (size-1) == 0xFFFFFFFF_FFFFFFFF  (위쪽이 전부 1 = 연속 마스크)</text>
  <text x="100" y="364" fill="currentColor" font-size="12">원래 값 0x0000000C / 0x00000080 → 주소 0x80_0000_0000 (type 비트 C 를 지운 값)</text>
  <text x="20" y="386" fill="currentColor" font-size="12">상위 dword 는 type 비트가 없다 — 이걸 별도 BAR 로 디코드하면 bit0=1 이라 "IO, 크기 4" 가 된다 (Part B)</text>
</svg>
```

### Part A — 종류·크기 디코드와 sizing 루틴

1. `bar_kind_of(lo, &prefetch)`: bit0 → IO, 아니면 bits 2:1 로 Mem32 / Mem64 / BAR_BAD(11b), bit3 → prefetch. type 01b(1M) 는 Linux `decode_bar()` 처럼 Mem32 로.
2. `bar_size_from_readback(rb, kind, &size)`: type 비트를 지운 `m` → 0 이면 `BS_UNIMPL`, 아니면 `s = m & (~m + 1)`(최하위 1 비트), `(m | (s-1))` 가 주소 폭과 같지 않으면 `BS_BADMASK`. IO 는 상위 16비트가 0 인 16비트 디코드 장치도 받아 준다(폭 0xFFFF).
3. `bar_read_masks(ops, orig, mask, disable_decode)`: VID 가 0xFFFF 면 아무것도 쓰지 않고 `BR_ERR_NODEV`. Command 저장 → Memory/IO 비트만 끄기(Bus Master 유지) → BAR 0..5 마다 `orig 읽기 → 0xFFFFFFFF 쓰기 → mask 읽기 → orig 쓰기` → Command 복원.
4. `bar_decode_all(orig, mask, out)`: 64비트면 다음 dword 를 상위로 합치고 그 칸은 `BAR_UPPER`. 반환 = 정상 BAR 수.
5. 테스트: 위 표의 6개 BAR 의 종류·크기·주소·prefetch, **config 256바이트 전체가 sizing 전과 같은지(memcmp)**, Command = 0x0007 복원, 쓰기 순서(Command 0x0004 → BAR 12번 → Command 0x0007), 모든 합법 크기 전수(IO 4..256, Mem32 16..2G, Mem64 16..2^63), 실측 QEMU nvme 값.

### Part B — 고전 버그와 고장 주입

| # | 상황 | 기대 결과 (테스트로 확인) |
|---|---|---|
| B1 | type 비트를 안 지우고 `~rb + 1` | IO `0xFFFFFF01` → 0xFF (정답 256), Mem64 하위 `0xF000000C` → 0x0FFFFFF4 |
| B2 | 64비트 상위 절반을 별도 BAR 로 디코드 | BAR3 = **IO, 크기 4** 유령. 8GB BAR 는 하위가 `0x0000000C` 라 "구현 안 됨" + BAR3 = 16바이트 유령 Mem32 |
| B3 | decode 를 켠 채 sizing | mock 기록: 중간 상태 BAR 가 디코드 중인 (write, BAR) 5건, 남의 주소 가로채기 3건 (write #3 BAR1@0xFFFFF000, #5 BAR2@0x80F0000000, #9 BAR4@0xFFFFF000). decode 끄면 0 / 0 |
| B4 | 8GB prefetchable Mem64 | 하위 `0x0000000C`, 상위 `0xFFFFFFFE` → 크기 0x2_0000_0000 |
| B5 | BAR5 가 64비트(짝 없음), type 11b, 비연속 마스크 `0xFFFF0F00` | `BS_64_AT_LAST`, `BS_BADTYPE`, `BS_BADMASK` — 그래도 config 는 전부 복원 |
| B6 | IO BAR 상위 16비트가 0 고정 (`0x0000FF01`) | 크기 256. `~m + 1` 공식은 0xFFFF0100 으로 틀림 |
| B7 | 없는 장치 (모든 읽기 all-1s) | `BR_ERR_NODEV`, config 쓰기 **0번** |

### Part C — 배치(assign)와 엔드포인트 FW

1. `bar_assign(ops, bars, windows)`: IO → IO 창, Mem64+prefetch → 64비트 prefetchable 창, 나머지 Memory → 32비트 창(4GB 아래). index 순서로 **자연 정렬**(addr % size == 0) 배치를 먼저 다 계산하고, 하나라도 넘치면 config 쓰기 없이 `BA_ERR_SPACE`. 들어가면 decode 끈 채 BAR 쓰기 → 필요한 decode 비트 켜기. 결과를 `lspci -v` 형식으로 찍고, mock 의 주소 디코더로 "0x1001_1234 → BAR4 offset 0x1234" 를 확인한다.
2. `ep_bar_program(bar, kind, prefetch, size, &regs)`: SSD 컨트롤러(엔드포인트) FW 쪽. DWC(Synopsys DesignWare) 스타일로 BAR 레지스터엔 type 비트, BAR mask 레지스터(dbi2)엔 `size - 1`. 2의 거듭제곱, IO 4..256, Mem ≥ 16, Mem32 ≤ 2GB, Mem64 는 짝수 번호·BAR4 이하. EP 설정 → mock 실리콘 → 호스트 sizing 왕복 738가지가 모두 같은 크기·종류로 돌아와야 한다.

## 2. Problem (English)

> **BAR sizing.** You are writing the PCI core of a small host bring-up stack. Config space is reached through
>
> ```c
> typedef struct { void *ctx;
>     uint16_t (*rd16)(void *, uint16_t); uint32_t (*rd32)(void *, uint16_t);
>     void (*wr16)(void *, uint16_t, uint16_t); void (*wr32)(void *, uint16_t, uint32_t); } cfg_ops;
> ```
>
> Implement
>
> ```c
> int bar_read_masks(const cfg_ops *ops, uint32_t orig[6], uint32_t mask[6], bool disable_decode);
> int bar_decode_all(const uint32_t orig[6], const uint32_t mask[6], bar_info out[6]); // returns #valid BARs
> int bar_assign(const cfg_ops *ops, bar_info out[6], const bar_windows *w);
> ```
>
> - Sizing must leave every config byte exactly as it was, and must not let the device decode a half-programmed BAR.
> - 64-bit memory BARs consume two slots. Report kind (IO / Mem32 / Mem64), prefetchable, bus address, size.
> - Reject: absent device (VID 0xFFFF, no writes at all), reserved type 11b, 64-bit BAR in slot 5, non-contiguous masks.
> - No dynamic allocation.
>
> Example: after writing `0xFFFFFFFF`, BAR2 reads `0xF000000C` and BAR3 reads `0xFFFFFFFF` → BAR2 is a 64-bit prefetchable BAR of 256 MB; BAR3 is its upper half.
>
> Follow-ups: (1) Why disable Memory/IO decode while sizing — what exactly can go wrong? (2) Why must a BAR be naturally aligned? (3) A BAR reads back `0x0000000C` — unimplemented? (4) On the endpoint side, how does firmware choose the size the host sees?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 개: A1 `bar_kind_of`, A2 `bar_size_from_readback`, A3 `bar_read_masks`, A4 `bar_decode_all`, C1 `bar_assign`, C2 `ep_bar_program`. mock 디바이스(바이트 배열 + 바이트별 쓰기 마스크 `wmask[256]` — QEMU `PCIDevice` 가 쓰는 방식과 같은 생각), 주소 디코더 `mock_decode`, 쓰기 기록, 일부러 틀린 `naive_*` 함수, 하네스는 주어진다. 스텁은 "BAR_NONE / UNIMPL / 0으로 채움 / 실패 코드" 를 돌려줘서 죽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
== Part A: bar_kind_of / bar_size_from_readback ==
FAIL bar_kind_of(0xFFFFFF01u, &pf) == BAR_IO && !pf  (line 347)
FAIL bar_kind_of(0xFFFFF000u, &pf) == BAR_MEM32 && !pf  (line 348)
…
  EP -> silicon -> host round trip: 0 cases, 0 bad
FAIL rt_cases == 7 * 6 + 2 * 28 * 6 + 2 * 60 * 3 && rt_bad == 0  (line 541)
18/79 passed
```

(exit 1.) 통과하는 18개는 "일부러 틀린 코드가 정말 틀린 값을 내는지" 와 mock 자체를 보는 테스트다.

## 4. 힌트

<details><summary>힌트 1 — 종류는 하위 4비트에 다 있다</summary>

`lo & 1` 이 1 이면 IO 이고 나머지는 볼 필요가 없다(IO 에는 prefetch 가 없다). 0 이면 `(lo >> 1) & 3` 이 type: 0 → 32, 1 → 1M(옛날 것, 32 로), 2 → 64, 3 → 예약(오류). prefetch 는 `lo & 8`. 종류는 **원래 값(orig)** 에서 읽어도 되고 mask 에서 읽어도 된다 — 하드와이어 비트라 둘이 같다 (Linux 는 orig 에서 읽는다).

</details>

<details><summary>힌트 2 — 크기 = 지운 값의 가장 낮은 1 비트</summary>

`m = rb & ~0xF`(Memory) 또는 `rb & ~0x3`(IO). 쓰기 가능한 비트는 "크기 이상" 의 주소 비트 전부이므로 m 은 `1…10…0` 꼴이다. 가장 낮은 1 비트 `m & (~m + 1)` 이 크기. 교과서 공식 `~m + 1` 은 위쪽이 **주소 폭 끝까지 전부 1** 일 때만 같은 값이다 — 16비트 IO 디코드 장치(`0x0000FF00`)에서는 틀린다. 그래서 "최하위 1 비트" 로 구하고, `(m | (s-1)) == 폭` 으로 연속인지 따로 검사한다.

</details>

<details><summary>힌트 3 — 쓰는 순서는 Linux 를 그대로</summary>

Linux 6.18.55 `drivers/pci/probe.c`: `pci_read_bases()` 가 Command 를 읽어 `PCI_COMMAND_DECODE_ENABLE`(= MEMORY | IO) 비트가 켜져 있으면 끄고, `__pci_size_bars()` 가 BAR 마다 `read orig → write ~0 → read sz → write orig`, 그 다음 Command 를 원래 값으로 되돌린다. decode 비트가 원래 꺼져 있었다면 Command 는 아예 안 쓴다 (테스트가 쓰기 횟수 12 / 14 로 확인).

</details>

<details><summary>힌트 4 — 64비트는 두 칸을 한 번에</summary>

`kind == BAR_MEM64` 면 `rb = mask[i] | (uint64_t)mask[i+1] << 32`, 주소도 같은 방식. 그리고 루프 변수 `i` 를 한 번 더 올려 상위 칸을 건너뛴다. i == 5 면 짝이 없다 → 오류. 크기 변수는 **`uint64_t`** — 8GB BAR 를 `uint32_t` 로 받으면 0 이 된다.

</details>

<details><summary>힌트 5 — 배치는 "계산 다 하고, 그다음 쓰기"</summary>

창 3개의 커서를 두고 `a = align_up(cur, size)`; `a + size - 1 > limit` 이면 실패. 32비트 창에 들어가는 64비트 non-prefetchable BAR(NVMe BAR0 이 이렇다)는 4GB 아래여야 한다. 전부 들어가는 걸 확인한 다음에만 config 에 쓴다 — 중간에 실패해서 반쯤 바뀐 장치를 남기지 않기 위해서다.

</details>

<details><summary>힌트 6 — 엔드포인트 쪽은 거울상</summary>

호스트가 읽는 값 = `~(size-1)` 의 주소 비트 | type 비트. 그러니 EP FW 는 "어느 비트를 쓰기 불가로 만들지" = `size - 1` 을 BAR mask 레지스터에 넣으면 된다 (Linux `pcie-designware-ep.c` 의 `dw_pcie_ep_set_bar_programmable()` 이 `dbi2` 에 `lower_32_bits(size - 1)`, 64비트면 `upper_32_bits(size - 1)` 를 쓴다).

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "BAR 는 크기보다 작은 주소 비트가 0 으로 고정돼 있다. all-1s 를 쓰고 읽으면 고정된 비트가 0 으로 남는다."
2. "읽은 값에서 type 비트(IO 2비트, Memory 4비트)를 지우고, 최하위 1 비트가 크기."
3. "64비트 BAR 는 두 dword 를 다 sizing 해서 합친다. 상위 dword 는 독립 BAR 가 아니다."
4. "sizing 동안 Command 의 Memory/IO decode 를 끄고, 끝나면 원래 값과 Command 를 복원한다."
5. "BAR 는 크기에 자연 정렬된 주소에만 놓을 수 있다 — 하드웨어가 상위 비트만 비교하기 때문."

### Part A — 규칙 표

| 종류 | 하드와이어 하위 비트 | 지울 마스크 | 쓰기 가능 비트 | 크기 범위 |
|---|---|---|---|---|
| IO | bit0 = 1, bit1 = 0 | `~0x3` | 31(또는 15)..log2(size) | 4 ~ 256 B (PCI 관례) |
| Mem32 | bit0 = 0, bits 2:1 = 00, bit3 = P | `~0xF` | 31..log2(size) | 16 B ~ 2 GB |
| Mem64 | bits 2:1 = 10, bit3 = P (하위 dword만) | `~0xF` (64비트) | 63..log2(size) | 16 B ~ 2^63 |
| 미구현 | 전부 0 (쓰기 불가) | — | 없음 → 0 이 읽힘 | — |

**왜 최하위 1 비트가 크기인가.** 크기 `S = 2^k` 인 BAR 는 주소 비트 `k-1..type` 이 0 고정, `폭-1..k` 가 쓰기 가능이다. 0xFFFFFFFF 를 쓰면 쓰기 가능 비트만 1 → 지운 값 `m` 은 `1…1 0…0` (위 폭-k 개가 1, 아래 k 개가 0). `m & (~m+1)` 은 2의 보수 성질로 최하위 1 비트만 남기므로 `2^k = S`. `m` 이 폭 끝까지 1 이면 `~m + 1 = 2^k` 와도 같다 — 두 식이 같은 조건이 바로 "연속 마스크" 다.

**단계별 추적 — mock 의 BAR0 (IO 256B)**

| 단계 | 동작 | BAR0 값 | 설명 |
|---|---|---|---|
| 1 | 읽기 | `0x00001001` | 주소 0x1000 + bit0 = IO |
| 2 | `0xFFFFFFFF` 쓰기 | — | wmask = `0xFFFFFF00` 인 비트만 바뀜 |
| 3 | 읽기 | `0xFFFFFF01` | bits 7..1 은 고정(7..2 = 0, bit1 예약 0), bit0 = 1 |
| 4 | `0x00001001` 쓰기 | `0x00001001` | 복원 |
| 계산 | `m = 0xFFFFFF01 & ~3 = 0xFFFFFF00` | | 최하위 1 비트 = 0x100 = **256** |

`solution.c` 의 실제 출력 (Part A 표):

```text
  BAR off  orig       after_all1s  kind   pf  size        addr
  0   0x10 0x00001001 0xFFFFFF01   IO     no  256         0x1000
  1   0x14 0x10010000 0xFFFFF000   Mem32  no  4K          0x10010000
  2   0x18 0x0000000C 0xF000000C   Mem64  yes 256M        0x8000000000
  3   0x1C 0x00000080 0xFFFFFFFF   upper  no  -           0x0
  4   0x20 0x10000000 0xFFFF0000   Mem32  no  64K         0x10000000
  5   0x24 0x00000000 0x00000000   none   no  -           0x0
```

**실측 1 — QEMU NVMe 에서 진짜로 해 봤다.** Lima VM 안에서 QEMU 10.2.1 (`-device pcie-root-port` + `-device nvme`, Linux 6.18.55 lab 커널, nvme.ko 는 로드 안 함)으로 부팅하고, BusyBox `dd` 로 `/sys/bus/pci/devices/0000:01:00.0/config` 에 4바이트씩 써서 같은 루틴을 돌렸다 (`capture_qemu_bar_sizing.sh` / `.txt`):

```text
resource file (start end flags):
0x0000000010000000 0x0000000010003fff 0x0000000000140204
COMMAND before = 0x0000
BAR0 @0x10: orig=0x10000004  after_all1s=0xffffc004  restored=0x10000004
BAR1 @0x14: orig=0x00000000  after_all1s=0xffffffff  restored=0x00000000
BAR2 @0x18: orig=0x00000000  after_all1s=0x00000000  restored=0x00000000
...
BAR5 @0x24: orig=0x00000000  after_all1s=0x00000000  restored=0x00000000
COMMAND restored = 0x0000
```

`0xffffc004` → type 10 (64비트), non-prefetchable. 상위 `0xffffffff` 와 합쳐 `0xFFFFFFFF_FFFFC000` → 최하위 1 비트 0x4000 = **16 KB**, 커널의 `resource` 파일(0x1000_0000–0x1000_3FFF)과 같다. BAR1 의 `0xffffffff` 는 혼자 보면 "IO" 처럼 보이지만 BAR0 의 상위 절반이다. Command 가 0x0000 인 이유: 드라이버가 안 붙어서 `pci_enable_device()` 가 불린 적이 없다 — Linux 는 BAR 에 주소만 배정해 두고 decode 는 드라이버가 켠다. 이 값은 `solution.c` 테스트에 그대로 들어 있다.

**실측 2 — 작은 BAR 도 있다.** Lima VM 자체(Apple Virtualization.framework virtio-blk, 00:06.0, 읽기만 함 — 살아 있는 디스크의 BAR 에 쓰면 안 된다)의 `lspci -vv` 는 `Region 0: Memory at 380020000 (64-bit, non-prefetchable) [size=64K]`, `Region 2: Memory at 50001540 (32-bit, non-prefetchable) [size=64]`. 64바이트 BAR 의 주소 0x5000_1540 도 64 의 배수 — 자연 정렬 (`capture_vm_0006_bars.txt`).

### Part A — 쓰기 순서와 decode

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">sizing 중 config write 순서 (mock, Command 원래 0x0007) — 위: 올바른 루틴, 아래: decode 를 켠 채</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="90" height="34" class="f-acc"/>
    <rect x="110" y="40" width="520" height="34" class="f-mut"/>
    <rect x="640" y="40" width="90" height="34" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="16" y="55">#1 Command</text><text x="16" y="69" font-family="monospace">= 0x0004</text>
    <text x="120" y="55">#2..#13: BAR0..5 마다 [0xFFFFFFFF 쓰기, 원래 값 쓰기]</text>
    <text x="120" y="69">Memory/IO decode 꺼짐 → 반쯤 쓰인 BAR 가 아무 주소도 받지 않는다</text>
    <text x="646" y="55">#14 Command</text><text x="646" y="69" font-family="monospace">= 0x0007</text>
  </g>
  <text x="10" y="96" fill="currentColor" font-size="12">mock 기록: 쓰기 14번, 중간 상태로 디코드된 (write, BAR) 0건, 가로채기 0건. Bus Master(0x4) 는 끄지 않는다.</text>
  <line x1="10" y1="112" x2="730" y2="112" stroke="currentColor" stroke-dasharray="4,4"/>
  <text x="10" y="134" fill="currentColor" font-size="12">decode 켠 채: 쓰기 12번. 다른 에이전트가 0xFFFFF000, 0x80F0000000 에 계속 접근한다고 가정</text>
  <g stroke="currentColor">
    <rect x="10" y="146" width="56" height="30" class="f-warn"/><rect x="66" y="146" width="56" height="30" class="f-mut"/>
    <rect x="122" y="146" width="56" height="30" class="f-warn"/><rect x="178" y="146" width="56" height="30" class="f-mut"/>
    <rect x="234" y="146" width="56" height="30" class="f-warn"/><rect x="290" y="146" width="56" height="30" class="f-mut"/>
    <rect x="346" y="146" width="56" height="30" class="f-warn"/><rect x="402" y="146" width="56" height="30" class="f-mut"/>
    <rect x="458" y="146" width="56" height="30" class="f-warn"/><rect x="514" y="146" width="56" height="30" class="f-mut"/>
    <rect x="570" y="146" width="56" height="30" class="f-mut"/><rect x="626" y="146" width="56" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="16" y="166">#1 B0</text><text x="72" y="166">#2</text><text x="128" y="166">#3 B1</text><text x="184" y="166">#4</text>
    <text x="240" y="166">#5 B2</text><text x="296" y="166">#6</text><text x="352" y="166">#7 B3</text><text x="408" y="166">#8</text>
    <text x="464" y="166">#9 B4</text><text x="520" y="166">#10</text><text x="576" y="166">#11 B5</text><text x="632" y="166">#12</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M150,178 V196"/><path d="M262,178 V218"/><path d="M486,178 V240"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="210">#3  BAR1 = 0xFFFFF000 → 4KB 창 0xFFFFF000..0xFFFFFFFF 가 남의 주소 0xFFFFF000 을 가로챔</text>
    <text x="20" y="232">#5  BAR2 하위만 all-1s, 상위는 아직 0x80 → 창 0x80_F000_0000 (256MB) 이 이웃 주소를 가로챔</text>
    <text x="20" y="254">#9  BAR4 = 0xFFFF0000 → 64KB 창이 다시 0xFFFFF000 을 덮음</text>
  </g>
  <text x="10" y="290" fill="currentColor" font-size="12">색칠된 칸 = 디코드 켜진 채 중간 상태인 BAR 5건: B0, B1, B2 (하위), B3 (상위 → 0xFFFFFFFF_0000_0000), B4</text>
  <text x="10" y="310" fill="currentColor" font-size="12">B5 는 미구현이라 0xFFFFFFFF 를 써도 0 그대로.</text>
  <text x="10" y="340" fill="currentColor" font-size="12">mock 실측: transient = 5, hits = 3 — 테스트가 이 숫자와 write 번호 #3, #5, #9 를 단언한다.</text>
</svg>
```

**왜 위험한가.** 0xFFFFFFFF 를 쓴 순간 그 BAR 는 "주소 공간 꼭대기" 에 있는 것과 같다. Memory decode 가 켜져 있으면 그 사이 다른 CPU·DMA·콘솔 출력이 그 주소로 가는 순간 **이 장치가 응답한다** — 원래 주인은 요청을 못 받고, 읽기는 엉뚱한 값을, 쓰기는 이 장치 레지스터에 들어간다. 64비트 BAR 는 하위만 바뀐 동안 `0x80_F000_0000` 처럼 **완전히 엉뚱한 중간 주소**가 생긴다. Linux 소스에 `/* No printks while decoding is disabled! */` 주석이 있는 이유도 같다 — 콘솔이 PCI 장치(예: PCI UART, VGA)면, 그 장치 자신의 decode 가 꺼진 동안 printk 가 그 MMIO 로 가면 안 된다.

**Bus Master 는 왜 그대로?** sizing 은 장치가 "받는" 쪽(decode)만 위험하다. 장치가 "보내는" DMA/MSI 는 BAR 값과 무관하다. Linux 도 `PCI_COMMAND_DECODE_ENABLE` 만 끈다.

### Part A — 디코드 함수의 결정 순서

| 순서 | 검사 | 결과 |
|---|---|---|
| 1 | `mask == 0` | 미구현 (`BAR_NONE`, `BS_UNIMPL`) — 계속 |
| 2 | `mask == 0xFFFFFFFF` (하위 dword 가) | 응답 없음/고장 (`BS_BADMASK`). Linux 는 `PCI_POSSIBLE_ERROR(sz)` 면 0 취급 |
| 3 | type 11b | `BS_BADTYPE` |
| 4 | Mem64 인데 i == 5 | `BS_64_AT_LAST` |
| 5 | Mem64 | `rb`, `addr` 에 i+1 을 상위로 합치고 `out[i+1] = BAR_UPPER`, i 하나 더 건너뜀 |
| 6 | 크기 | `bar_size_from_readback` → OK / UNIMPL / BADMASK |

### Part B — 버그를 바이트로 보기

`solution.c` 실제 출력:

```text
  naive ~rb+1: IO 0xFFFFFF01 -> 0x000000FF, Mem64 lo 0xF000000C -> 0x0FFFFFF4, Mem32 pf 0xFFFFF008 -> 0x00000FF8
  naive per-dword: BAR3 (upper half 0xFFFFFFFF) -> IO size 4
  8GB BAR naive per-dword: BAR2 -> none, BAR3 -> Mem32 size 16
```

- **B1 (마스크 안 함):** `~0xFFFFFF01 + 1 = 0xFE + 1 = 0xFF`. 크기가 2의 거듭제곱이 아니면 무조건 버그다. Mem32 non-prefetchable 은 type 비트가 전부 0 이라 **우연히 맞는다** — 그래서 이 버그는 4KB 레지스터 BAR 하나뿐인 장치에서는 안 보이다가 prefetchable/64비트/IO BAR 가 있는 장치에서 터진다.
- **B2 (상위 절반을 BAR 로):** 0xFFFFFFFF 는 bit0 = 1 → "IO" 이고 `~0xFFFFFFFC + 1 = 4`. 8GB BAR 는 더 심하다: 크기 2^33 이면 하위 dword 의 쓰기 가능 주소 비트가 **하나도 없다** → 하위가 `0x0000000C` 라 "미구현", 상위 `0xFFFFFFFE` 는 bit0 = 0 이라 "Mem32 16바이트". 진짜 8GB 는 사라지고 16바이트 유령이 생긴다.
- **B6 (16비트 IO):** 일부 장치는 IO BAR 상위 16비트를 0 으로 고정한다 → `0x0000FF01`. `~0xFF00 + 1 = 0xFFFF0100` (말이 안 되는 크기). 최하위 1 비트 방식은 0x100 = 256 을 낸다. Linux 는 같은 문제를 `pci_size()` 에서 `mask & maxbase` 후 `size & ~(size-1)` 로 푼다 (IO 마스크에 `IO_SPACE_LIMIT` 도 건다).

### Part C — 배치와 확인

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">bar_assign 결과 (index 순서, 자연 정렬) — solution.c 실제 값</text>
  <text x="10" y="50" fill="currentColor" font-size="12">IO 창 0x1000..0xFFFF</text>
  <g stroke="currentColor"><rect x="200" y="36" width="90" height="24" class="f-acc"/><rect x="290" y="36" width="430" height="24" class="f-mut"/></g>
  <text x="206" y="53" fill="currentColor" font-size="12" font-family="monospace">BAR0 256B</text>
  <text x="300" y="53" fill="currentColor" font-size="12">비어 있음</text>
  <text x="10" y="96" fill="currentColor" font-size="12">Mem 창 0x1000_0000..</text>
  <g stroke="currentColor">
    <rect x="200" y="82" width="90" height="24" class="f-ok"/>
    <rect x="290" y="82" width="150" height="24" class="f-mut"/>
    <rect x="440" y="82" width="150" height="24" class="f-ok"/>
    <rect x="590" y="82" width="130" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="204" y="99">BAR1 4K</text><text x="296" y="99">빈틈 60K (정렬)</text><text x="446" y="99">BAR4 64K</text>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="200" y="122">1000_0000</text><text x="290" y="122">1000_1000</text><text x="440" y="122">1001_0000</text><text x="590" y="122">1002_0000</text>
  </g>
  <text x="10" y="160" fill="currentColor" font-size="12">PF64 창 0x80_0000_0000..</text>
  <g stroke="currentColor"><rect x="200" y="146" width="300" height="24" class="f-warn"/><rect x="500" y="146" width="220" height="24" class="f-mut"/></g>
  <text x="206" y="163" fill="currentColor" font-size="12" font-family="monospace">BAR2/3 256M @ 80_0000_0000</text>
  <text x="10" y="204" fill="currentColor" font-size="12">확인 (mock_decode, Command = 0x0007 로 decode 켠 뒤):</text>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="30" y="226">MEM 0x1001_1234      → BAR4 offset 0x1234</text>
    <text x="30" y="246">MEM 0x80_0FFF_FFFF   → BAR2 offset 0xFFFFFFF (256MB 마지막 바이트)</text>
    <text x="30" y="266">IO  0x10FF           → BAR0 offset 0xFF</text>
    <text x="30" y="286">MEM 0x1000_1000      → 아무도 안 받음 (정렬 빈틈) → 실제 버스라면 UR / all-1s</text>
  </g>
</svg>
```

`solution.c` 의 `lspci -v` 형식 출력 (실제):

```text
  Region 0: I/O ports at 1000 [size=256]
  Region 1: Memory at 10000000 (32-bit, non-prefetchable) [size=4K]
  Region 2: Memory at 8000000000 (64-bit, prefetchable) [size=256M]
  Region 4: Memory at 10010000 (32-bit, non-prefetchable) [size=64K]
```

**자연 정렬을 왜 하나.** 장치의 주소 디코더는 "들어온 주소의 상위 비트(쓰기 가능 비트 자리)가 BAR 값과 같은가" 만 비교한다. 64KB BAR 를 0x1000_1000 에 두라고 쓰면 하위 16비트는 0 고정이라 BAR 는 0x1000_0000 이 되어 버린다 — 정렬은 선택이 아니라 하드웨어가 강제한다. 그래서 BAR4 는 BAR1 바로 뒤가 아니라 0x1001_0000 으로 가고 60KB 빈틈이 생긴다 (큰 것부터 놓으면 빈틈이 줄어든다 — 연습 4, 브리지 창 전체는 PCIE-A08).

**전부 계산하고 나서 쓰기.** 창이 모자란 테스트(`small`)에서 `bar_assign` 은 config 쓰기 0번으로 실패한다. bring-up 중에 반쯤 재배치된 장치를 남기면, 다음 디버그 단계에서 "BAR 값이 왜 이렇지?" 를 또 한 번 추적해야 한다.

### Part C — 엔드포인트(SSD 컨트롤러 FW) 쪽

| 입력 | dbi (BAR 레지스터) | dbi2 (BAR mask) | 호스트가 sizing 하면 |
|---|---|---|---|
| BAR0, Mem64 NP, 16 KB (QEMU nvme 와 같은 모양) | lo `0x4`, hi `0` | lo `0x3FFF`, hi `0` | `0xFFFFC004` / `0xFFFFFFFF` → 16K |
| BAR2, Mem64 PF, 8 GB | lo `0xC` | lo `0xFFFFFFFF`, hi `0x1` | `0x0000000C` / `0xFFFFFFFE` → 8G |
| BAR1, Mem64 | — | — | `EP_ERR_INDEX` (DWC 는 홀수 시작 64비트 쌍 불가) |
| Mem32 3000 B | — | — | `EP_ERR_SIZE` (2의 거듭제곱 아님) |

DWC 규칙은 Linux 6.18.55 `drivers/pci/controller/dwc/pcie-designware-ep.c` 에서 확인했다 (`dbi2` 에 `size - 1`, `if ((flags & PCI_BASE_ADDRESS_MEM_TYPE_64) && (bar & 1)) return -EINVAL;`). PCI 스펙 자체는 64비트 BAR 를 BAR0..4 어디서든 시작할 수 있게 허용한다 — 짝수 제한은 **이 IP 의 제약**이다. 이 레슨의 mock 은 "dbi2 의 1 인 비트 = 쓰기 불가" 로 단순화했다 (실제 DWC 의 BAR mask 레지스터 bit0 은 BAR enable 의미도 겸한다 — 데이터북 확인 권장).

왕복 테스트 실제 출력: `EP -> silicon -> host round trip: 738 cases, 0 bad` (IO 7크기 × 6자리 + Mem32 28크기 × prefetch 2 × 6자리 + Mem64 60크기 × 2 × 3자리).

**SSD FW 현실:** 이 설정은 **링크가 올라오고 호스트가 열거하기 전에** 끝나야 한다. 호스트 BIOS/커널이 sizing 한 뒤에 EP FW 가 크기를 바꾸면 호스트는 이미 옛 크기로 창을 배정했다 — 레지스터 영역 일부가 안 보이거나 남의 주소와 겹친다. 그래서 컨트롤러 부트 ROM/FW 초기화 순서에서 "BAR mask 설정 → LTSSM enable(링크 트레이닝 허용)" 순서가 고정이다 (LTSSM 은 PCIE-M07).

### 복잡도와 자원

| 항목 | 값 |
|---|---|
| config 접근 | BAR 6개 × (읽기 2 + 쓰기 2) + Command 읽기 1 + 쓰기 ≤ 2 |
| 메모리 | `orig[6]`, `mask[6]`, `bar_info[6]` — 동적 할당 없음 |
| 실패 시 | 어떤 경우든 BAR·Command 원래대로 (테스트: memcmp 256바이트) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 79/79 passed, exit 0 (< 0.01 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
```

`solution.c` 실행 출력 중 테스트 외 줄 (위에 나온 표·Region 줄 제외):

```text
  decode ON: after cfg write #3 BAR1 claimed 0xFFFFF000
  decode ON: after cfg write #5 BAR2 claimed 0x80F0000000
  decode ON: after cfg write #9 BAR4 claimed 0xFFFFF000
  decode ON: transient (write,BAR) events = 5, hits = 3
  EP -> silicon -> host round trip: 738 cases, 0 bad
79/79 passed
```

실측 캡처 파일: `capture_qemu_bar_sizing.sh` (게스트에서 돌린 스크립트), `capture_qemu_bar_sizing.txt` (QEMU nvme sizing 결과), `capture_vm_0006_bars.txt` (Lima VM 00:06.0 `lspci -vv`/`-xxx`/`resource`, 읽기 전용).

## 7. 자주 하는 실수

**1. type 비트를 안 지우고 `~rb + 1`.** IO 256B 가 255, prefetchable 64비트가 0x0FFFFFF4 로 나온다. 4KB non-prefetch Mem32 에서는 우연히 맞아서 테스트를 통과해 버린다. 고친 코드: `m = rb & (kind == BAR_IO ? ~0x3u : ~0xFu)` 후 크기 계산.

**2. 64비트 BAR 의 상위 dword 를 독립 BAR 로 처리.** 결과: 4바이트 유령 IO BAR, 4GB 넘는 BAR 실종, 주소 상위 32비트 손실. 고친 코드: type 이 10b 면 `rb |= (uint64_t)mask[i+1] << 32; i++;`.

**3. sizing 중 decode 를 켜 둠.** 실험실(장치 하나, 트래픽 없음)에서는 증상이 없다가, 다른 CPU 가 MMIO 를 하는 실보드·핫플러그·VM 에서 간헐적으로 터진다. mock 에서는 3건 가로채기. 고친 코드: `if (cmd & DECODE) wr16(COMMAND, cmd & ~DECODE);` … `wr16(COMMAND, cmd);`.

**4. 복원을 "켜기" 로 대신함.** 끝에 무조건 `Command |= MEMORY | IO` 를 하면, 원래 꺼져 있던 장치(QEMU nvme 실측: 0x0000)의 decode 가 드라이버도 없는데 켜진다. 원래 값을 저장해서 그대로 쓴다. BAR 도 원래 값을 다시 써야 한다 — 0xFFFFFFFF 인 채로 두면 다음 decode enable 때 바로 실수 3 이 된다.

**5. 크기를 `uint32_t` 에 담음.** 8GB(2^33) BAR 는 0 이 되고 "미구현" 으로 분류된다. 64비트 BAR 의 크기·주소는 처음부터 `uint64_t`. 32비트 커널/부트로더에서 Linux 가 "can't handle BAR larger than 4GB" 를 찍는 경로가 바로 이것이다 (`__pci_read_base()`).

**6. 16비트 IO 디코드 장치에 `~m + 1`.** `0x0000FF00` → 0xFFFF0100. 최하위 1 비트(`m & -m`)로 구한다.

**7. ROM BAR 에 0xFFFFFFFF.** Expansion ROM BAR(0x30)의 bit0 은 enable 이다. all-1s 를 쓰면 sizing 중 ROM 디코드가 켜진다 — Linux 는 `PCI_ROM_ADDRESS_MASK`(`~0x7ff`)를 써서 enable 을 0 으로 둔다 (연습 1).

## 8. 면접 Q&A

<details><summary>Q1. BAR sizing 할 때 왜 Memory/IO decode 를 끄나요? 안 끄면 정확히 무슨 일이?</summary>

0xFFFFFFFF 를 쓴 순간 그 BAR 는 주소 공간 꼭대기(32비트면 0xFFFFF000 같은 곳, 64비트 하위만 바뀌면 `0x80_F000_0000` 같은 중간 주소)로 "이동" 한 것과 같다. decode 가 켜져 있으면 그 창에 들어오는 남의 트랜잭션을 이 장치가 받아 버린다 — 원래 주인은 요청을 못 받고, 읽기는 엉뚱한 값, 쓰기는 이 장치 레지스터에 들어간다. mock 에서는 decode 를 켠 채 sizing 하면 5번의 중간 상태와 3번의 가로채기가 기록된다. 그래서 Linux 6.18 `pci_read_bases()` 는 sizing 전에 `PCI_COMMAND_DECODE_ENABLE` 비트를 끄고, 그동안 printk 도 금지한다(콘솔이 PCI 장치일 수 있으니까). Bus Master 는 끄지 않는다 — sizing 위험은 "받는" 쪽에만 있다. 꼬리질문 "그럼 왜 Linux 는 `mmio_always_on` 예외가 있죠?" → 일부 장치(예: 호스트 브리지류)는 decode 를 끄면 시스템이 죽기 때문에 quirk 로 예외 처리한다.

</details>

<details><summary>Q2. BAR 는 왜 크기에 자연 정렬돼야 하나요?</summary>

장치의 주소 디코더가 "주소 상위 비트 == BAR 의 쓰기 가능 비트" 비교 하나로 끝나게 하려고. 크기보다 작은 비트는 0 고정이라 BAR 값 자체가 항상 size 의 배수다 — 정렬이 안 된 주소는 **쓸 수조차 없다**(쓰면 하위 비트가 잘려 다른 주소가 된다). 덕분에 덧셈·뺄셈 비교기 없이 AND/비교만으로 디코드하고, 오프셋은 그냥 하위 비트다. 대가는 배치할 때 빈틈 — mock 에서 4KB 뒤 64KB BAR 사이 60KB. 브리지 창은 1MB 단위라 또 정렬된다 (PCIE-B07, PCIE-A08).

</details>

<details><summary>Q3. 64비트 BAR 는 어떻게 sizing 하나요? 상위 dword 에서 0xFFFFFFFF 가 읽히면?</summary>

하위 dword 의 type 비트가 10b 면 다음 dword 가 상위 32비트다. 둘 다 all-1s 를 쓰고 읽어 `hi << 32 | lo` 로 합친 뒤 하위 4비트를 지우고 최하위 1 비트를 찾는다. 상위가 0xFFFFFFFF 인 건 정상(크기 ≤ 4GB 라 상위 주소 비트가 전부 쓰기 가능). 이걸 별도 BAR 로 디코드하면 bit0 = 1 이라 "4바이트 IO BAR" 가 된다. 크기가 4GB 를 넘으면 하위 dword 주소 비트가 하나도 쓰기 가능하지 않아 하위가 `0x0000000C` 처럼 읽힌다 — 하위만 보면 "미구현" 이다. 꼬리질문 "64비트 BAR 가 BAR5 에 있으면?" → 짝이 없으니 잘못된 장치(이 레슨 `BS_64_AT_LAST`). 일부 EP IP(DWC)는 짝수 번호에서만 시작할 수 있다.

</details>

<details><summary>Q4. config read 에서 0xFFFF / 0xFFFFFFFF 가 나오면 무슨 뜻인가요?</summary>

VID 0xFFFF 는 "그 BDF 에 장치 없음" — 아무도 응답하지 않은 config read 를 루트 컴플렉스가 all-1s 로 채워 돌려준다 (Unsupported Request/Master Abort 처리, 열거는 PCIE-B08/B09). 그래서 sizing 루틴은 VID 부터 보고 0xFFFF 면 **아무것도 쓰지 않는다** (이 레슨 테스트: 쓰기 0번). BAR sizing 결과가 통째로 0xFFFFFFFF 인 것도 비슷하게 "장치가 응답 안 함" 신호라 Linux 는 `PCI_POSSIBLE_ERROR(sz)` 면 sz = 0 으로 본다. 런타임에 MMIO 읽기가 0xFFFFFFFF 이면 surprise removal 이나 링크 다운을 의심한다 (PCIE-A07).

</details>

<details><summary>Q5. prefetchable 비트는 무슨 뜻이고, NVMe BAR0 은 왜 non-prefetchable 인가요?</summary>

prefetchable = "읽어도 부작용이 없고, 쓰기를 합치거나 미리 읽어도 된다". 그래서 브리지가 미리 읽기·쓰기 병합을 할 수 있고, OS 는 write-combining 매핑(`ioremap_wc`)을 쓸 수 있다 — GPU 프레임버퍼, NVMe CMB 같은 메모리성 영역. 레지스터 영역은 읽기만 해도 상태가 바뀌거나(clear-on-read), 쓰기 순서·크기가 의미를 가지므로(doorbell) non-prefetchable 이어야 한다. QEMU nvme 실측 BAR0 도 `0xffffc004` — 64비트, P = 0. 브리지 쪽에서는 prefetchable 창만 64비트를 지원하므로(PCIE-B07), 64비트 non-prefetchable BAR 는 4GB 아래 32비트 창에 들어가야 한다 (이 레슨 `bar_assign` 규칙).

</details>

<details><summary>Q6. Memory Space 비트가 꺼진 장치에 MMIO 를 하면? QEMU nvme 는 왜 Command 가 0 이었나요?</summary>

장치가 그 주소를 받지 않으므로 non-posted 읽기는 UR 로 끝나고 CPU 는 보통 0xFFFFFFFF 를 본다(플랫폼에 따라 bus error/SError — 확인 권장). posted 쓰기는 조용히 버려진다. 실측 QEMU 게스트에서 nvme.ko 를 로드하지 않았더니 Command = 0x0000 이었다: Linux PCI 코어는 열거 때 BAR 에 주소만 배정하고, Memory/Bus Master 는 드라이버의 `pci_enable_device()` / `pci_set_master()` 가 켠다. bring-up 에서 "레지스터가 전부 FFFFFFFF" 면 첫 확인은 `lspci -vv` 의 `Control: Mem+` 여부다 (PCIE-B03, PCIE-B06).

</details>

<details><summary>Q7. SSD 컨트롤러(엔드포인트) FW 입장에서 BAR 크기는 어떻게 정하나요?</summary>

EP 컨트롤러 IP 에 "BAR mask" 류 레지스터가 있다. DWC 의 경우 dbi2 공간의 같은 오프셋에 `size - 1` 을 쓰면 그 비트들이 호스트 쪽에서 쓰기 불가(0 고정)가 되고, BAR 레지스터 자체엔 type/prefetch 비트를 쓴다 (Linux `dw_pcie_ep_set_bar_programmable()`). 순서가 중요하다: 링크 트레이닝 enable 전에 끝내야 한다 — 호스트가 이미 sizing 한 뒤 바꾸면 배정된 창과 어긋난다. 크기는 레지스터 + doorbell(+ MSI-X 테이블/PBA 를 같은 BAR 에 두면 그것까지)을 덮는 2의 거듭제곱. QEMU nvme 는 16KB 다. 런타임에 크기를 바꾸고 싶으면 Resizable BAR extended capability(ReBAR) 로 호스트와 협상한다 — GPU 가 큰 VRAM 을 통째로 보이게 할 때 쓴다 (세부는 스펙 확인 권장).

</details>

<details><summary>Q8. `lspci` 의 `[size=16K]` 는 lspci 가 sizing 해서 얻은 값인가요?</summary>

아니다. 커널이 열거할 때 한 번 sizing 해서 `struct resource` 에 저장하고, sysfs `resource` 파일(`start end flags` 6줄 + ROM 등)로 내보낸다. lspci 는 그걸 읽어 `[size=...]` 를 찍는다 — 실측 QEMU nvme 의 `0x10000000 0x10003fff 0x140204` 가 16K 다 (flags = `IORESOURCE_MEM_64 | IORESOURCE_SIZEALIGN | IORESOURCE_MEM | 0x4`, include/linux/ioport.h 값). 그래서 lspci 를 아무리 돌려도 장치 BAR 에 쓰기가 생기지 않는다. 사용자 공간에서 직접 sizing 하려면 `setpci` 로 BAR 에 쓰기를 해야 하는데, 드라이버가 붙어 동작 중인 장치에선 하면 안 된다 (이 레슨 캡처는 드라이버 없는 QEMU 게스트에서만 했다).

</details>

<details><summary>Q9. 요즘도 IO BAR 를 쓰나요? ARM 에서는 IO 공간이 어떻게 보이죠?</summary>

PCIe 네이티브 엔드포인트는 IO 공간을 요구하지 않는 쪽으로 권장되고(레거시 엔드포인트만), NVMe·NIC 대부분 Memory BAR 만 있다 — QEMU nvme 도 BAR0 하나. ARM 에는 x86 의 `in/out` 명령이 없으므로 호스트 브리지가 CPU 주소의 한 창을 PCI IO 공간으로 번역한다 (QEMU virt 의 DT `ranges` 에 IO 창이 있다 — 정확한 주소는 DT 확인, PCIE-B08). Linux 는 이걸 `pci_remap_iospace()` 로 매핑해 `inb/outb` 를 MMIO 로 바꾼다.

</details>

## 9. 추가 연습

### 연습 1 — Expansion ROM BAR (0x30)

ROM BAR 는 bits 31:11 이 주소, bit0 이 enable. sizing 패턴으로 `PCI_ROM_ADDRESS_MASK`(`~0x7ff`)를 쓰는 `rom_size()` 를 짜고, 0xFFFFFFFF 를 쓰면 무엇이 잘못되는지 mock 으로 보여라. 64KB ROM, ROM 없음, 최소 2KB ROM.

<details><summary>답</summary>

`orig 읽기 → 패턴 쓰기 → 읽기 & 0xFFFFF800 → orig 쓰기`, 크기 = 최하위 1 비트. 크기 값은 두 패턴이 같다(64KB → 0x10000). 차이는 부작용: 0xFFFFFFFF 를 쓰면 enable 비트까지 1 이 되어, Memory decode 가 켜진 장치에서 ROM 이 0xFFFF0000 창에 잠깐 나타난다 (mock 의 `rom_on_during_sizing` 이 0 → 1). Linux 도 `__pci_size_rom()` 에서 `PCI_ROM_ADDRESS_MASK` 를 쓴다. ROM 이 없으면 쓰기 가능 비트가 없어 0, 최소 크기는 bit 11 = 2KB.

</details>

### 연습 2 — sysfs `resource` 파서

`/sys/bus/pci/devices/<BDF>/resource` 의 앞 6줄(`0x… 0x… 0x…`)을 파싱해 종류/크기를 내라. 입력은 이 레슨의 실측 두 개(QEMU nvme, Lima VM 00:06.0). 필드가 모자라거나 `0x` 로 시작하지 않으면 −1.

<details><summary>답</summary>

크기 = end − start + 1, flags 가 0 이면 미사용. 종류는 `IORESOURCE_IO 0x100`, `IORESOURCE_MEM 0x200`, `IORESOURCE_MEM_64 0x100000`, `IORESOURCE_PREFETCH 0x2000` (include/linux/ioport.h). 하위 8비트(`IORESOURCE_BITS`)엔 BAR 의 원래 type 비트가 남는다: QEMU nvme `0x140204` → 하위 `0x04` = Mem64. Lima VM 00:06.0: BAR0 Mem64 64K @ 0x3_8002_0000, BAR2 Mem32 **64바이트** @ 0x5000_1540 (64 의 배수 — 작은 BAR 도 자연 정렬). 파싱은 `strtoull(p, &e, 16)` 후 `e` 가 전진했는지 보고, 줄 끝이 `\n`/`\0` 인지 확인.

</details>

### 연습 3 — `[size=...]` 표기

lspci 처럼 크기를 1024 로 나눠떨어지는 동안 나누고 K/M/G 를 붙여라: 256 → `256`, 4096 → `4K`, 16384 → `16K`, 256M, 8G, 64 → `64`, 1536 → `1536`.

<details><summary>답</summary>

`while (x % 1024 == 0 && i < 4) { x /= 1024; i++; }` 후 `"%llu%s"`. 1536 은 1024 로 안 나눠지므로 그대로 — "1.5K" 같은 반올림을 하지 않는 게 포인트다 (BAR 크기는 2의 거듭제곱이라 실제로는 항상 깔끔하게 나온다). lspci 소스의 `show_size()` 도 같은 방식으로 기억한다 (pciutils 소스 확인 권장).

</details>

### 연습 4 — 배치 순서와 빈틈

한 창에 크기 {4K, 64K, 4K, 1M} BAR 를 자연 정렬로 놓을 때 index 순서와 "큰 것부터" 의 span(마지막 끝 주소)을 구하라. {16, 64, 16} 도.

<details><summary>답</summary>

index 순서: 4K@0 → 64K@0x10000 → 4K@0x20000 → 1M@0x100000, span **0x200000**. 큰 것부터: 1M@0 → 64K@0x100000 → 4K@0x110000 → 4K@0x111000, span **0x112000**. 큰 것부터 놓으면 커서가 항상 다음 크기의 배수에 있어서(큰 2의 거듭제곱의 배수는 작은 2의 거듭제곱의 배수) 빈틈이 0 이다. {16, 64, 16}: 0x90 vs 0x60. Linux 의 리소스 할당도 정렬 큰 것부터 정렬해서 놓는다 — 브리지 창 단위 계산은 PCIE-A08.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 27/27 passed
```

```text
  sizes 4K,64K,4K,1M: index order span = 0x200000, largest-first span = 0x112000
27/27 passed
```

## 10. 핵심 정리

- BAR 하위 비트: bit0 = 1 IO / 0 Memory, bits 2:1 type (00 = 32, 10 = 64, 11 = 예약), bit3 prefetchable. 64비트면 다음 BAR 가 상위 32비트.
- sizing: VID 확인 → Command 의 Memory/IO 끄기(Master 유지) → BAR 마다 [저장, 0xFFFFFFFF, 읽기, 복원] → Command 복원. Linux 6.18 `pci_read_bases()` / `__pci_size_bars()` 와 같은 순서.
- 크기 = type 비트(IO `~0x3`, Mem `~0xF`)를 지운 값의 최하위 1 비트. `~m + 1` 은 위가 폭 끝까지 1 일 때만 같다(16비트 IO 에서 틀림). `(m | (size-1)) == 폭` 으로 연속 검사.
- 64비트: 두 dword 를 합쳐 `uint64_t` 로. 상위를 따로 디코드하면 4바이트 유령 IO BAR, 4GB 넘는 BAR 는 실종.
- decode 를 켠 채 sizing 하면 반쯤 쓰인 BAR 가 남의 주소를 가로챈다 (mock: 중간 상태 5건, 가로채기 3건).
- BAR 는 크기에 자연 정렬 — 하드웨어가 상위 비트 비교만 하기 때문. 배치는 큰 것부터 하면 빈틈 0.
- EP FW 쪽 거울상: BAR mask = size − 1 (DWC dbi2), 링크 enable 전에 설정. 실측 QEMU nvme BAR0 = 64비트 NP 16KB (`0xffffc004`/`0xffffffff`).

## 참고자료

- Linux 6.18.55 `include/uapi/linux/pci_regs.h` — `PCI_BASE_ADDRESS_*`, `PCI_COMMAND_*`, `PCI_ROM_ADDRESS_MASK` (Lima VM `/usr/include/linux/pci_regs.h` 로 값 확인).
- Linux 6.18.55 `drivers/pci/probe.c` — `pci_size()`, `decode_bar()`, `__pci_size_bars()`, `__pci_read_base()`, `pci_read_bases()` (decode 끄기, "No printks while decoding is disabled!"): <https://elixir.bootlin.com/linux/v6.18/source/drivers/pci/probe.c>
- Linux 6.18.55 `drivers/pci/controller/dwc/pcie-designware-ep.c` — `dw_pcie_ep_set_bar_programmable()` (dbi2 = size − 1), 홀수 번호 64비트 BAR 거부.
- Linux `include/linux/ioport.h` — `IORESOURCE_IO/MEM/PREFETCH/MEM_64/SIZEALIGN` (sysfs `resource` flags).
- PCI-SIG, *PCI Express Base Specification* — Type 0/1 header, Base Address Registers 절 (Rev 4.0 기준 7.5.1.2.1 근처 — 절 번호는 판마다 다름, 확인 권장), Expansion ROM BAR, Resizable BAR capability.
- PCI-SIG, *PCI Local Bus Specification 3.0* 6.2.5 "Base Addresses" — all-1s sizing 절차와 decode 끄기 권고의 원전 (절 번호 기억 의존, 확인 권장).
- QEMU `hw/pci/pci.c` — `PCIDevice` 의 `config` / `wmask` / `w1cmask` 모델 (이 레슨 mock 의 바이트별 쓰기 마스크 아이디어).
- pciutils `lspci` (`ls-*.c` 의 `show_size()`), `setpci(8)` 매뉴얼.
- 실측 캡처 (이 디렉터리): `capture_qemu_bar_sizing.txt` / `.sh` — QEMU 10.2.1 nvme, `capture_vm_0006_bars.txt` — Lima VM virtio-blk.
- 관련 레슨: PCIE-B03 (Type 0 헤더, Command 비트 진단), PCIE-B04 (capability), PCIE-B06 (lspci/sysfs 실전), PCIE-B07 (Type 1 브리지 창), PCIE-B08 (ECAM, 없는 장치 0xFFFF), PCIE-B09 (열거), PCIE-B12 (NVMe BAR0 레지스터 맵), PCIE-M07 (LTSSM), PCIE-M10 (MSI-X 테이블은 BAR 안에), PCIE-M13 (드라이버 `pci_iomap`), PCIE-A01 (VFIO 로 BAR mmap), PCIE-A06 (SR-IOV VF BAR), PCIE-A07 (surprise removal), PCIE-A08 (브리지 창 리소스 할당), BM-B04 (W1C/RMW 레지스터 함정), C-B01 (비트 연산 `x & -x`).
