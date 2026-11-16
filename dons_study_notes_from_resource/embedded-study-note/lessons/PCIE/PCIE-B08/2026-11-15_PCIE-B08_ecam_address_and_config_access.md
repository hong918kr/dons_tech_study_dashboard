---
id: PCIE-B08
time: 60분
updated: 2026-11-15
---
# PCIE-B08 ECAM 주소 계산과 config 접근 방식

## 학습 목표

- ECAM(Enhanced Configuration Access Mechanism, 옛 이름 MMCONFIG) 오프셋 `bus<<20 | dev<<15 | fn<<12 | reg` 를 비트 단위로 설명하고, 물리 주소 ↔ (bus, dev, fn, reg) 를 머릿속으로 바꿀 수 있다 (예: `0x4003003C` = 00:06.0 의 Interrupt Line).
- `cfg_read/write` 8/16/32 API 를 짜며 **폭·정렬·범위·BDF 검사**를 하드웨어에 닿기 전에 하고, 오류 시 all-1s 를 돌려주는 Linux 의 약속을 따른다.
- legacy CF8/CFC(Configuration Mechanism #1)를 에뮬레이션하고, 왜 256B 까지만 닿는지, 바이트 레인(`0xCFC + (reg & 3)`)이 왜 필요한지, 두 단계 접근이 왜 락을 요구하는지 말한다.
- 없는 함수가 `0xFFFF` 로 읽히는 이유, VID `0x0001`(RRS) 의 의미, multi-function 비트 규칙을 버스 스캔 코드로 구현한다.
- ARM Linux 가 ECAM base 를 찾는 두 길(ACPI MCFG, DT `pci-host-ecam-generic`)을 **실캡처 바이트**로 디코드하고, 둘의 base 기준(버스 0 vs bus-range 첫 버스)이 다르다는 것을 안다.

## 1. 문제

실리콘 검증·보드 bring-up 면접에서 "lspci 가 아무것도 안 보여요 / VID 가 FFFF 예요" 다음에 나오는 질문은 늘 같다: **CPU 는 config space 를 어떤 주소로, 몇 비트 폭으로 읽고 있나?** 이 레슨은 그 한 줄짜리 주소 계산과, 그 주변에서 실제로 사고가 나는 지점(정렬, all-1s, W1C, RRS, 버스 번호 기준)을 직접 구현한다.

config space 는 함수(function)마다 4KB 짜리 레지스터 묶음이다 (앞 256B 는 PCI 시절부터의 영역, 0x100~0xFFF 는 PCIe 확장 영역 — 헤더 내용 자체는 PCIE-B03/B04). CPU 가 여기에 닿는 길은 둘이다.

| 방식 | CPU 쪽 모양 | 닿는 범위 | 접근 1번 | 쓰는 곳 |
|---|---|---|---|---|
| **ECAM** | 큰 MMIO 창 하나. 주소 비트가 곧 bus/dev/fn/reg | 함수당 4KB 전부 | load/store 1번 | ARM SoC, 최신 x86 (Linux `pci_generic_ecam_ops`) |
| **CF8/CFC** (Mechanism #1) | IO 포트 2개: `outl(0xCF8, 주소)` → `in/out(0xCFC+k)` | 함수당 256B | IO 명령 2번 (그 사이 전역 래치) | x86 레거시, 부트 초기, BIOS |

Don 의 SSD 경험으로 말하면: 호스트 BIOS/커널이 SSD 컨트롤러의 VID/DID 를 읽는 그 순간이 ECAM load 한 번(또는 CF8+CFC 두 번)이고, 컨트롤러 펌웨어가 아직 부팅 중이면 엔드포인트는 RRS(Request Retry Status) 로 "잠깐 기다려" 를 돌려준다. 호스트 쪽 드라이버를 짜든, 엔드포인트 펌웨어를 짜든 이 왕복을 바이트 단위로 알아야 bring-up 로그를 읽을 수 있다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 레지스터 00:06.0 reg 0x3C (Interrupt Line) 를 가리키는 두 주소</text>
  <text x="10" y="48" fill="currentColor">ECAM 오프셋 (28비트, 창 base 에 더한다)</text>
  <g stroke="currentColor">
    <rect x="40" y="58" width="194" height="40" class="f-acc"/>
    <rect x="234" y="58" width="121" height="40" class="f-ok"/>
    <rect x="355" y="58" width="73" height="40" class="f-warn"/>
    <rect x="428" y="58" width="291" height="40" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="44" y="74">bus [27:20]</text><text x="238" y="74">dev [19:15]</text>
    <text x="359" y="74">fn [14:12]</text><text x="432" y="74">reg [11:0] — 4KB 전부</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="13">
    <text x="44" y="92">0000 0000</text><text x="238" y="92">0 0110</text><text x="359" y="92">000</text>
    <text x="432" y="92">0000 0011 1100</text>
  </g>
  <text x="40" y="120" fill="currentColor" font-size="12">= 0x0003003C → VM 의 ECAM base 0x40000000 + 0x0003003C = 0x4003003C (devmem 으로 읽으면 0xFF)</text>
  <text x="10" y="160" fill="currentColor">CF8 에 쓰는 값 (32비트 IO 포트)</text>
  <g stroke="currentColor">
    <rect x="40" y="170" width="21" height="40" class="f-acc"/>
    <rect x="61" y="170" width="149" height="40" class="f-mut"/>
    <rect x="210" y="170" width="170" height="40" class="f-acc"/>
    <rect x="380" y="170" width="106" height="40" class="f-ok"/>
    <rect x="486" y="170" width="64" height="40" class="f-warn"/>
    <rect x="550" y="170" width="128" height="40" class="f-mut"/>
    <rect x="678" y="170" width="42" height="40" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="44" y="186">E</text><text x="65" y="186">[30:24] 예약</text><text x="214" y="186">bus [23:16]</text>
    <text x="384" y="186">dev [15:11]</text><text x="490" y="186">fn [10:8]</text><text x="554" y="186">reg [7:2]</text>
    <text x="682" y="186">00</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="13">
    <text x="46" y="204">1</text><text x="65" y="204">000 0000</text><text x="214" y="204">0000 0000</text>
    <text x="384" y="204">0 0110</text><text x="490" y="204">000</text><text x="554" y="204">00 1111</text><text x="684" y="204">00</text>
  </g>
  <text x="40" y="232" fill="currentColor" font-size="12">= 0x8000303C. reg 은 6비트(7:2)뿐 → 0x00~0xFC 의 dword 만. 바이트 선택은 데이터 포트 0xCFC + (reg &amp; 3)</text>
  <text x="40" y="252" fill="currentColor" font-size="12">비트 27:24 = reg[11:8] 로 쓰는 AMD 식 확장은 비표준 (Linux arch/x86/pci/direct.c 가 사용) — 연습 3</text>
  <line x1="40" y1="270" x2="720" y2="270" stroke="currentColor" stroke-dasharray="3,3"/>
  <text x="40" y="292" fill="currentColor" font-size="12">같은 dev 6 이 ECAM 에선 비트 15, CF8 에선 비트 11 부터 — 공식을 섞으면 0x8003003C 같은 엉뚱한 값</text>
  <text x="40" y="314" fill="currentColor" font-size="12">ECAM: 버스 1개 = 32 dev × 8 fn × 4KB = 1MB, 256 버스 = 256MB (VM 실측 창 0x40000000–0x4fffffff)</text>
</svg>
```

(그림의 숫자 `0x4003003C`, `0x8000303C` 는 `solution.c` 테스트에 그대로 있고, `0x4003003C → 0xFF` 는 Lima VM 에서 `sudo busybox devmem 0x4003003c 8` 로 실제로 읽은 값이다 — `capture_ecam_vm.txt`.)

**mock 플랫폼** (`solution.c` 의 `mock_reset()`, 주어진 코드): 루트 컴플렉스 하나가 ECAM 창(버스 0..1 = 2MB, 정적 배열)과 CF8/CFC 포트를 둘 다 제공하고, 둘은 같은 함수 저장소로 간다.

| BDF | 내용 | 출처 |
|---|---|---|
| 00:00.0 | 호스트 브리지 106b:1a05, class 06:00:00 | **VM 실캡처** `lspci -xxx` |
| 00:06.0 | virtio-blk 1af4:1042, class 01:80:00, Command 0x0416 | **VM 실캡처** `lspci -xxxx` (256B 장치) |
| 00:1c.0 | 루트 포트 1b36:000c, Header Type 1, sec=1 sub=1 | 가상 (QEMU pcie-root-port 와 같은 ID) |
| 00:1f.0, 00:1f.3 | multi-function 장치 1234:abc0 / abc3 (fn1, fn2 없음) | 가상 |
| 01:00.0 | NVMe SSD 1b36:0010, class 01:08:02, 0x100 에 AER 확장 헤더 `0x00020001`. **함수 번호를 무시하는 버그 장치**, RRS 주입 가능 | 가상 (QEMU nvme 와 같은 ID) |

쓰기 규칙(모든 함수 공통, 이 레슨의 mock 정의): Command 의 IO/Mem/BusMaster/Parity/SERR/INTx-disable 비트와 Cache Line Size·Interrupt Line·0x40 이후는 RW, Status 의 오류 비트(0xF900)는 **W1C**, 나머지(VID/DID/Class/Header Type/Int Pin …)는 RO. BAR 쓰기 규칙은 PCIE-B05, Type 1 쓰기 규칙은 PCIE-B07.

### Part A — ECAM cfg_read/write (8/16/32)

1. `ecam_offset(bus_rel, dev, fn, reg)` — 공식 그대로. 테스트: `00:06.0 → 0x30000`, `00:1f.3 reg 0x0e → 0xFB00E`, `ff:1f.7 reg 0xffc → 0x0FFFFFFC`, 256 버스 × 32 × 8 × reg 3종 전수 왕복.
2. `cfg_check(dev, fn, reg, width, limit)` — 폭 ∈ {1,2,4} → dev ≤ 31, fn ≤ 7 → reg < limit → `reg % width == 0` 순서로 `CFG_ERR_WIDTH / BDF / REG / ALIGN`.
3. `ecam_read/ecam_write(win, bus, dev, fn, reg, width, …)` — 버스가 창(`bus_start..bus_end`) 밖이면 `CFG_ERR_BDF`. 오류면 **하드웨어를 건드리지 않고** `*val = all-1s`(폭만큼). 정상이면 `mmio_read(ecam_offset(bus - bus_start, …))`.
4. 테스트: VM 에서 devmem 으로 ECAM 을 직접 읽은 값(`0x10421AF4`, `0x0416`, `0x01800001` …)과 같음, 모든 함수·모든 dword 에서 `read32 == read16 두 개 == read8 네 개`(리틀 엔디언), 없는 장치 00:02.0 / 없는 함수 00:1f.1 → `0xFFFFFFFF/0xFFFF/0xFF` 이고 반환은 `CFG_OK`.

### Part B — CF8/CFC 에뮬레이션과 오류·함정

1. `conf1_address()` + `conf1_read/conf1_write()` — limit 256, `io_out(0xCF8, 4, addr)` 다음 `io_in/io_out(0xCFC + (reg & 3), width)`.
2. **같은 레지스터를 두 길로**: 두 버스의 모든 dev/fn(없는 것 포함) × 0x00..0xFF × 폭 1/2/4 = 229376 번 비교, 불일치 0.
3. 바이트 레인: Header Type(0x0E, 1B) → 포트 0xCFE, Status(0x06, 2B) → 0xCFE, Int Pin(0x3D) → 0xCFD.
4. CF8 로 0x100 → `CFG_ERR_REG` (하드웨어 접근 0회), ECAM 으로는 `0x00020001`.
5. 오류 전부(오정렬 16/32, 폭 3/8, dev 32, fn 8, bus 256, 창 밖 버스 2, reg 0x1000, reg 0xFFE 폭 4)에서 `g_hw_ops`·`g_hw_bad` 변화 0 — 걸러진 접근은 버스에 나가지 않는다. `reg 0xFFC` 폭 4 는 합법.
6. RO 쓰기 무시(VID, Class, Int Pin), 없는 장치로의 쓰기는 조용히 버려짐.
7. **W1C 함정**: Status 에 Received Master Abort + Detected Parity 가 선 상태에서 (a) 16비트 RMW 로 Command 를 바꾸면 오류 비트 보존, (b) 32비트 RMW 로 바꾸면 `0xA0100412` 를 읽어 그대로 쓰는 바람에 **오류 비트가 지워진다**(테스트가 이 버그를 재현), (c) 의도한 지우기는 그 비트만 1 로 쓴다.
8. 창의 첫 버스가 0x40 인 플랫폼: `bus - bus_start` 를 빼먹으면 창 밖을 읽는다.

### Part C — 펌웨어/드라이버 현실: ECAM base 찾기와 버스 스캔

1. `mcfg_parse()` — VM 실캡처 ACPI MCFG 60바이트 → 창 1개(`0x40000000`, seg 0, 버스 00-ff). 체크섬·길이·시그니처 오류, 손으로 만든 2항목 MCFG(seg 1, 버스 0x10..0x1f) — **MCFG 의 base 는 버스 0 기준**이라 창 주소 = base + (0x10 << 20).
2. `dt_ecam_window()` — QEMU virt `reg = <0x40 0x10000000 0x0 0x10000000>`(부모 #address-cells=2, #size-cells=2) → `0x40_1000_0000`, 256MB. **DT 의 reg 는 bus-range 첫 버스 기준**. 창이 버스 수 × 1MB 보다 작으면 `ECAM_ERR_SIZE`.
3. `ecam_phys()` — devmem 으로 찍어 볼 주소. VM 에서 실제로 읽은 `0x40030000`, `0x40010000`(없음 → 0xFFFFFFFF), `0x400F8000`, `0x40000100` 과 같아야 한다.
4. `probe_fn()` + `scan_bus()` — Linux `pci_bus_generic_read_dev_vendor_id` 와 같은 판정(없음 패턴 4개, VID 0x0001 = RRS 재시도), Header Type bit7 일 때만 fn1..7. 출력은 `lspci -n` 형식이고 VM 실출력 `00:06.0 0180: 1af4:1042 (rev 01)` 과 글자까지 같아야 한다. 01:00.0 은 MFD 비트를 무시한 스캔에서 8개로 보여야 하고(버그 재현), RRS 5회 → 재시도 5회 후 발견, 예산 3 → `not_ready`, SV 꺼짐 → 소프트웨어는 RRS 를 못 보고 하드웨어가 멈춘다(`g_hw_stall == 5`).

## 2. Problem (English)

> **Config-space access layer.** You are writing the PCI config accessor for a bring-up kernel on an ARM SoC whose root
> complex exposes an ECAM window, and an x86-compatible CF8/CFC emulation for legacy code.
>
> ```c
> typedef struct { uint64_t phys; uint16_t seg; uint8_t bus_start, bus_end; } ecam_win;  /* phys = address of bus_start */
> uint32_t ecam_offset(unsigned bus_rel, unsigned dev, unsigned fn, unsigned reg);
> int ecam_read (const ecam_win *w, unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned width, uint32_t *val);
> int ecam_write(const ecam_win *w, unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned width, uint32_t val);
> uint32_t conf1_address(unsigned bus, unsigned dev, unsigned fn, unsigned reg);
> int conf1_read (unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned width, uint32_t *val);
> int conf1_write(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned width, uint32_t val);
> int mcfg_parse(const uint8_t *tbl, size_t len, ecam_win *out, int cap);
> int scan_bus(const ecam_win *w, unsigned bus, unsigned max_retry, pci_fn_info *out, int cap, scan_stats *st);
> ```
>
> - `width` ∈ {1, 2, 4}; the access must be naturally aligned; `dev ≤ 31`, `fn ≤ 7`; ECAM reaches `reg < 4096`,
>   CF8/CFC only `reg < 256`. On any error return a negative code, do not touch hardware, and set `*val` to all-ones.
> - A function that does not exist reads as all-ones (`0xFFFF` Vendor ID) — that is data, not an error.
> - `scan_bus` must probe functions 1–7 only if function 0 has Header Type bit 7 set, and must retry a Vendor ID of
>   `0x0001` (Request Retry Status) up to `max_retry` times.
>
> Example: ECAM base `0x40000000` (from MCFG), device `00:06.0`, register `0x3C` → physical `0x4003003C`;
> the same register via CF8 is `outl(0x8000303C, 0xCF8); inb(0xCFC)`.
>
> Follow-ups: (1) Why does a missing device read `0xFFFF`? (2) Why can't CF8/CFC reach the AER capability at 0x100?
> (3) Your controller only supports 32-bit config writes — how do you write the Command register safely?
> (4) How does Linux on ARM find the ECAM base? (5) The SSD returns Vendor ID `0x0001` for 300 ms after reset — what do you do?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 12개: A1 `ecam_offset`, A2 `cfg_check`, A3 `ecam_read`, A4 `ecam_write`, B1 `conf1_address`, B2 `conf1_read`, B3 `conf1_write`, C1 `mcfg_parse`, C2 `dt_ecam_window`, C3 `ecam_phys`, C4 `probe_fn`, C5 `scan_bus`. mock 하드웨어(`cfg_hw_read/write`, `mmio_read/write`, `io_in/io_out`), 실캡처 데이터, 하네스는 주어진다. 스텁은 "0 / all-1s / 오류 코드" 를 돌려주므로 멈추거나 죽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:394  cfg_check(6, 0, 0x3c, 4, PCI_CFG_SPACE_SIZE) == CFG_OK && cfg_check(6, 0, 0x100, 4, PCI_CFG_SPACE_SIZE) == CFG_ERR_REG
FAIL starter.c:397  ecam_offset(0, 6, 0, 0) == 0x30000u && ecam_offset(0, 2, 0, 0) == 0x10000u && ecam_offset(0, 0x1f, 0, 0) == 0xF8000u
FAIL starter.c:416  er(0, 6, 0, PCI_VENDOR_ID, 4) == 0x10421AF4u
…
FAIL starter.c:610  n == 1 && st.rrs_retries == 0 && g_hw_stall == 5
18/79 passed
```

(exit 1.) 맨 앞의 "mock 자체 점검" 4개(원시 오프셋 devmem 흉내, 실캡처 MCFG 의 base 필드)는 처음부터 통과한다. 단계별 확인: **A1–A4 만 채우면 45/79**, **B 까지 58/79**, C 까지 79/79.

## 4. 힌트

<details><summary>힌트 1 — ECAM 공식은 "1MB / 32KB / 4KB" 로 외운다</summary>

버스 하나 = 32 dev × 8 fn × 4KB = 1MB = 2^20 → bus 는 비트 20 부터. 장치 하나 = 8 fn × 4KB = 32KB = 2^15 → dev 는 비트 15 부터. 함수 하나 = 4KB = 2^12 → fn 은 비트 12 부터. reg 는 12비트. 그래서 `bus<<20 | dev<<15 | fn<<12 | reg`. Linux `PCIE_ECAM_OFFSET` 은 dev 와 fn 을 합친 `devfn`(8비트)을 `<<12` 하는데, devfn = dev<<3 | fn 이라 같은 값이다.

</details>

<details><summary>힌트 2 — 검사는 하드웨어에 닿기 전에, 그리고 순서가 있다</summary>

`dev = 32` 를 그냥 시프트하면 `32 << 15 = 0x100000` = **다음 버스의 dev 0** 이 된다(테스트에 있음). 그래서 shift 전에 범위를 본다. 폭이 이상하면 정렬 검사 `reg & (width - 1)` 자체가 의미 없으니 폭부터. `reg < limit` 이고 폭으로 정렬돼 있으면 `reg + width ≤ limit` 은 자동(limit 이 4의 배수). 오류일 때 `*val` 을 all-1s 로 채우는 이유: 호출자가 반환값을 무시해도 "없는 장치" 처럼 안전한 쪽으로 보이게 (Linux `PCI_SET_ERROR_RESPONSE`).

</details>

<details><summary>힌트 3 — CF8 의 reg 는 6비트, 나머지 2비트는 데이터 포트가 고른다</summary>

CF8 값의 비트 1:0 은 항상 00 — dword 단위로만 주소를 정한다. 그 dword 안의 바이트는 **데이터 포트 번호**로 고른다: 1바이트는 `0xCFC + (reg & 3)`, 2바이트는 `0xCFC + (reg & 2)`(정렬돼 있으면 `reg & 3` 과 같음), 4바이트는 `0xCFC`. Linux `arch/x86/pci/direct.c` 가 정확히 이렇게 한다.

</details>

<details><summary>힌트 4 — MCFG 는 표 하나, 체크섬은 "전부 더하면 0"</summary>

ACPI 테이블 공통 헤더 36바이트(시그니처 4, Length 4, Revision 1, **Checksum 1**, OEM ID 6, OEM Table ID 8, OEM Revision 4, Creator ID 4, Creator Revision 4) + MCFG 예약 8바이트 = 44, 그 뒤 16바이트 항목들. 체크섬은 테이블 전체(Length 바이트) 합이 256 으로 나눠 0. base 는 리틀 엔디언 64비트이고 **버스 0** 의 주소 — 창 주소로 쓰려면 `start_bus << 20` 을 더한다 (Linux `drivers/acpi/pci_mcfg.c`: `res.start = root->mcfg_addr + (bus_res->start << 20)`).

</details>

<details><summary>힌트 5 — 스캔: fn0 → MFD 비트 → fn1..7, RRS 는 "VID 만" 0x0001</summary>

32비트로 offset 0 을 읽으면 VID(하위 16) + DID(상위 16). `(v & 0xFFFF) == 0x0001` 이면 RRS — 같은 읽기를 다시. 없음 판정은 `0xFFFFFFFF`, `0`, `0x0000FFFF`, `0xFFFF0000` (Linux 가 "망가진 보드" 까지 고려한 목록). fn0 이 없으면 그 장치의 fn1..7 은 보지 않는다. fn0 의 Header Type(0x0E) bit7 이 0 이면 fn1..7 도 보지 않는다 — 01:00.0 이 왜 8개로 보이는지 확인해 보라.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 주소 | ECAM 비트 배치(1MB/32KB/4KB), base 는 어디서 오나(MCFG/DT), `bus - bus_start` |
| 2. 접근 규칙 | 폭 1/2/4, 자연 정렬(한 DW 안), dev ≤ 31, fn ≤ 7, ECAM 4KB / CF8 256B — 검사 실패는 버스에 안 나간다 |
| 3. 없음과 오류 | 없는 함수 = all-1s 데이터(오류 아님), 인자 오류 = 음수 코드 + all-1s |
| 4. 쓰기 | RO 무시, W1C 는 1 쓴 비트만 지움 → 쓰기 폭이 결과를 바꾼다 |
| 5. 레거시 | CF8 비트 배치, 바이트 레인, 전역 래치라 락 필요, 256B 한계 |
| 6. 현실 | 스캔: MFD 규칙, RRS, Linux 의 판정 목록과 대기 시간 |

### Part A — ECAM 공식과 접근 API

ECAM 창 안의 비트 배치 (Linux `include/linux/pci-ecam.h`: `PCIE_ECAM_BUS_SHIFT 20`, `PCIE_ECAM_DEVFN_SHIFT 12`, `PCIE_ECAM_REG_MASK 0xfff`):

| 비트 | 필드 | 크기 | 한 칸의 크기 |
|---|---|---|---|
| 27:20 | bus (창 첫 버스 기준) | 8비트, 0..255 | 1MB |
| 19:15 | device | 5비트, 0..31 | 32KB |
| 14:12 | function | 3비트, 0..7 | 4KB |
| 11:0 | register | 12비트, 0..0xFFF | 1B |

추적 — VM 의 00:06.0 Command(reg 0x04) 를 16비트로 읽기:

| 단계 | 값 |
|---|---|
| bus_rel = 0 − 0 | 0 |
| dev 6 << 15 | 0x30000 |
| fn 0 << 12 | 0 |
| reg | 0x004 |
| 오프셋 | 0x00030004 |
| 물리 주소 = 0x40000000 + 오프셋 | **0x40030004** — VM `devmem 0x40030004 16` → `0x0416` |
| 바이트 (`lspci -xxx` 의 `00:` 줄 5,6번째) | `16 04` → 리틀 엔디언 `0x0416` = INTx-disable(0x400) + MWI(0x10) + BusMaster(0x4) + Mem(0x2) |

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">ECAM 창을 확대하면 (VM 실측: 0x40000000–0x4fffffff, 버스 00–ff)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="200" height="40" class="f-acc"/>
    <rect x="20" y="80" width="200" height="40" class="f-mut"/>
    <rect x="20" y="120" width="200" height="40" class="f-mut"/>
    <rect x="20" y="160" width="200" height="40" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="28" y="64">bus 00  0x40000000 (1MB)</text>
    <text x="28" y="104">bus 01  0x40100000</text>
    <text x="28" y="144">…</text>
    <text x="28" y="184">bus ff  0x4ff00000</text>
  </g>
  <g stroke="currentColor">
    <rect x="270" y="40" width="200" height="30" class="f-mut"/>
    <rect x="270" y="70" width="200" height="30" class="f-mut"/>
    <rect x="270" y="100" width="200" height="30" class="f-ok"/>
    <rect x="270" y="130" width="200" height="30" class="f-mut"/>
    <rect x="270" y="160" width="200" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="278" y="60">dev 00  0x40000000 (32KB)</text>
    <text x="278" y="90">dev 01  0x40008000</text>
    <text x="278" y="120">dev 06  0x40030000</text>
    <text x="278" y="150">…</text>
    <text x="278" y="180">dev 1f  0x400f8000</text>
  </g>
  <g stroke="currentColor">
    <rect x="520" y="40" width="220" height="34" class="f-acc"/>
    <rect x="520" y="74" width="220" height="44" class="f-ok"/>
    <rect x="520" y="118" width="220" height="82" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="528" y="56">0x000–0x03F 표준 헤더 (B03)</text>
    <text x="528" y="69">fn0 = 0x40030000 (4KB)</text>
    <text x="528" y="94">0x040–0x0FF capability (B04)</text>
    <text x="528" y="110">virtio: 0x40, 0x50 … 0x84</text>
    <text x="528" y="140">0x100–0xFFF 확장 영역</text>
    <text x="528" y="158">AER/LTR/L1SS/SR-IOV …</text>
    <text x="528" y="176">ECAM 으로만 닿는다</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M220,40 L270,40"/><path d="M220,80 L270,190"/>
    <path d="M470,100 L520,40"/><path d="M470,130 L520,200"/>
  </g>
  <path d="M748,40 V118" stroke="currentColor" fill="none"/>
  <path d="M742,40 H748 M742,118 H748" stroke="currentColor"/>
  <text x="20" y="232" fill="currentColor" font-size="12">CF8/CFC 로 닿는 범위 = 오른쪽 위 두 칸(256B)뿐. 커널은 0x100 을 읽어 확장 영역이 진짜인지 본다 (연습 3).</text>
  <text x="20" y="254" fill="currentColor" font-size="12">VM 실측: 00:00.0 의 /sys/…/config 는 4096B, 00:06.0(PCIe cap 없는 virtio) 은 256B.</text>
  <text x="20" y="276" fill="currentColor" font-size="12">없는 장치 00:02.0 = 0x40010000 → 0xFFFFFFFF (VM devmem 실측). 루트 컴플렉스가 UR 완료를 all-1s 로 바꿔 준다.</text>
  <text x="20" y="298" fill="currentColor" font-size="12">버스 번호 하나 = 1MB 라서 창 크기 = 버스 수 × 1MB. 16MB 창이면 버스 16개 (연습 5).</text>
</svg>
```

**왜 폭과 정렬을 검사하나.** PCIe 에서 config 요청 하나(CfgRd0/CfgWr0 TLP)는 Length 가 항상 1 DW 이고, 그 DW 안에서 어느 바이트인지를 First DW Byte Enable 로 고른다(TLP 헤더는 PCIE-B10). 그러니 **한 DW 를 넘는 접근은 TLP 하나로 표현이 안 된다** — 0x02 에서 32비트, 0x01 에서 16비트는 DW 경계를 넘는다. 루트 컴플렉스가 이런 MMIO 를 받으면 쪼개거나 오류를 내거나 정의되지 않은 값을 준다(구현마다 다름). Linux 도 `drivers/pci/access.c` 에서 `PCI_word_BAD (pos & 1)`, `PCI_dword_BAD (pos & 3)` 이면 `PCIBIOS_BAD_REGISTER_NUMBER` 로 막는다. 이 레슨의 mock 은 오정렬 접근이 하드웨어까지 오면 `g_hw_bad` 를 올리고, 테스트는 그 값이 끝까지 0 인지 본다.

| 오류 | 이 레슨 코드 | Linux 에서 비슷한 것 |
|---|---|---|
| 폭 ∉ {1,2,4} | `CFG_ERR_WIDTH` | API 가 byte/word/dword 함수로 나뉘어 애초에 불가 |
| dev > 31, fn > 7, 창 밖 버스 | `CFG_ERR_BDF` | `map_bus()` 가 NULL → `PCIBIOS_DEVICE_NOT_FOUND` (0x86) |
| reg ≥ 4096 (CF8: ≥ 256) | `CFG_ERR_REG` | `PCIBIOS_BAD_REGISTER_NUMBER` (0x87) |
| 오정렬 | `CFG_ERR_ALIGN` | 같음 (0x87) |
| 오류 시 `*val` | all-1s | `PCI_SET_ERROR_RESPONSE(val)` = `~0` |

**리틀 엔디언 조립.** config space 는 바이트 배열이고 다중 바이트 필드는 리틀 엔디언이다. `lspci -xxx` 의 `00: f4 1a 42 10` 은 32비트 `0x10421AF4`(DID:VID). mock 은 바이트를 `v |= cfg[reg+i] << 8*i` 로 조립하고 테스트가 모든 함수 × 1024 dword 에서 `read32 == read16 쌍 == read8 넷` 을 확인한다. 실제 ARM 커널도 `readl()` 이 리틀 엔디언 접근을 보장한다.

**없는 함수 = all-1s 는 "오류" 가 아니라 "데이터".** 루트 컴플렉스가 보낸 CfgRd 를 받을 함수가 없으면 Unsupported Request(UR) 완료가 돌아오고(또는 PCI 시절 Master Abort), 루트 컴플렉스는 CPU 의 load 를 막을 수 없으니 **all-1s 로 채워 완료**한다. VID `0xFFFF` 는 PCI-SIG 가 절대 할당하지 않는 값이라 "없음" 표시로 안전하다. 그래서 `ecam_read` 는 이때 `CFG_OK` 를 돌려주고, 판단은 상위(`probe_fn`)가 값으로 한다. VM 에서 `devmem 0x40010000 32` → `0xFFFFFFFF` 가 바로 이것이다.

### Part B — CF8/CFC

CF8 값 = `0x80000000 | bus<<16 | dev<<11 | fn<<8 | (reg & 0xFC)` (Linux `drivers/pci/pci.h` 의 `PCI_CONF1_ADDRESS`, 출처 주석 "PCI Local Bus Spec 3.0, Section 3.2.2.3.2"). 추적:

| 접근 | CF8 에 쓰는 값 | 데이터 포트 | 결과 (mock) |
|---|---|---|---|
| 00:06.0 reg 0x3C, 1B | `0x8000303C` | `0xCFC` (0x3C & 3 = 0) | Interrupt Line |
| 00:06.0 reg 0x3D, 1B | `0x8000303C` (같은 dword!) | `0xCFD` | Int Pin = 0x01 |
| 00:06.0 reg 0x06, 2B | `0x80003004` | `0xCFE` | Status = 0x0010 |
| 00:1f.3 reg 0x0E, 1B | `0x8000FB0C` | `0xCFE` | Header Type |
| ff:1f.7 reg 0xFF | `0x80FFFFFC` | `0xCFF` | — |
| 01:00.0 reg 0x100 | 표현 불가 (reg 은 7:2 뿐) | — | `CFG_ERR_REG`, 버스에 안 나감 |

**왜 256B 뿐인가.** reg 필드가 비트 7:2 의 6비트 → 64 dword = 256B. 확장 영역(AER 0x100 등)은 PCIe 가 생긴 뒤의 것이라 Mechanism #1 에는 자리가 없다. AMD 는 예약 비트 27:24 에 reg[11:8] 을 넣는 확장을 만들었고(Linux `arch/x86/pci/direct.c` 의 `PCI_CONF1_ADDRESS` 가 `(reg & 0xF00) << 16` 을 넣는다, `drivers/pci/pci.h` 주석은 "No standardized specification" 이라고 명시), 칩셋 쪽 설정으로 켜야 한다(세부 조건은 플랫폼 문서 확인 권장). 켜지지 않은 칩셋에서 이 값을 쓰면 예약 비트가 무시되어 **0x100 이 0x000 으로 접힌다** — 연습 3 이 이것을 재현하고, 커널이 aliasing 을 어떻게 알아채는지 보인다.

**전역 래치와 락.** CF8 은 CPU 전체에 하나뿐인 상태다. `outl(CF8)` 과 `inl(CFC)` 사이에 인터럽트나 다른 CPU 가 자기 config 접근을 하면 래치가 바뀌어 **남의 레지스터**를 읽는다. 그래서 Linux 는 `raw_spin_lock_irqsave(&pci_config_lock)` 으로 두 단계를 묶는다. ECAM 은 load/store 하나라 접근 자체는 원자적이다. (Linux 는 기본적으로 `pci_lock` 으로 접근 하나하나를 직렬화하지만, read 와 write 가 따로 잠기므로 **RMW 전체를 지켜 주지는 않는다** — 그건 드라이버 몫.)

```svg
<svg viewBox="0 0 760 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">CF8/CFC 는 두 단계 — 사이에 끼어들면 엉뚱한 레지스터 (연습 2 실측 값)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="220" height="36" rx="5" class="f-acc"/>
    <rect x="270" y="40" width="220" height="36" rx="5" class="f-warn"/>
    <rect x="270" y="86" width="220" height="36" rx="5" class="f-warn"/>
    <rect x="520" y="40" width="220" height="36" rx="5" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="28" y="56">① main: outl(0x80003000, CF8)</text><text x="28" y="70">래치 = 00:06.0 reg 0</text>
    <text x="278" y="56">② ISR: outl(0x8000F800, CF8)</text><text x="278" y="70">래치 = 00:1f.0 reg 0</text>
    <text x="278" y="102">③ ISR: inl(CFC) = 0xABC01234</text><text x="278" y="116">ISR 은 맞게 읽음</text>
    <text x="528" y="56">④ main: inl(CFC) = 0xABC01234</text><text x="528" y="70">원한 값 0x10421AF4 아님!</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M240,58 H268"/><path d="M490,104 C505,104 505,58 518,58"/>
  </g>
  <g stroke="currentColor">
    <rect x="20" y="150" width="340" height="44" rx="5" class="f-ok"/>
    <rect x="400" y="150" width="340" height="44" rx="5" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="28" y="168">고침: irq_save → ①④ → irq_restore → (미룬 ISR)</text>
    <text x="28" y="184">main 0x10421AF4, ISR 0xABC01234 (둘 다 맞음)</text>
    <text x="408" y="168">ECAM: ldr w0, [0x40030000] 한 번</text>
    <text x="408" y="184">끼어들 틈이 없다 — 래치가 없다</text>
  </g>
  <text x="20" y="226" fill="currentColor" font-size="12">Linux x86: pci_config_lock (raw spinlock + irqsave). SMP 에선 다른 CPU 도 끼어들어 irq 끄기만으론 부족.</text>
  <text x="20" y="246" fill="currentColor" font-size="12">enable 비트(31)를 빼먹으면 CFC 는 config 사이클이 아니다 → 아무도 응답 안 함 → all-1s (테스트에 있음)</text>
</svg>
```

**W1C 함정 — 쓰기 폭이 결과를 바꾼다.** Command(0x04, RW) 와 Status(0x06, 오류 비트 W1C) 는 같은 dword 에 있다. 장치가 Received Master Abort(0x2000)와 Detected Parity(0x8000)를 세운 상태(Status `0xA010`)에서:

| 방법 | 읽은 값 | 쓴 값 | 결과 Status | 판정 |
|---|---|---|---|---|
| 16비트 RMW: `rd16(0x04)` → `& ~MASTER` → `wr16(0x04)` | `0x0416` | `0x0412` | `0xA010` | 올바름 — Status 를 안 건드림 |
| 32비트 RMW: `rd32(0x04)` → `| MASTER` → `wr32(0x04)` | `0xA0100412` | `0xA0100416` | **`0x0010`** | 버그 — 읽은 1 을 그대로 써서 W1C 가 지워짐 |
| 의도한 지우기: `wr16(0x06, 0x2000)` | — | `0x2000` | `0x8010` | 올바름 — 그 비트만 |

오류 비트는 bring-up 에서 "무엇이 실패했나" 의 유일한 증거다(AER 이 없는 경로면 더더욱). 32비트 RMW 한 번이 그 증거를 지운다. Linux 는 32비트 쓰기만 되는 컨트롤러용 `pci_generic_config_write32()` 에서 바로 이 이유로 "*may corrupt adjacent RW1C bits*" 경고를 찍는다 (`drivers/pci/access.c`). 그런 컨트롤러에서 안전하게 쓰는 법은 연습 4. W1C 일반론은 BM-B04.

**창의 첫 버스.** 세그먼트가 여러 개인 서버나 다중 루트 컴플렉스 SoC 는 창마다 버스 범위가 다르다(예: 0x40..0x41). `ecam_offset(bus, …)` 에 절대 버스 번호를 넣으면 0x41 << 20 = 0x4100000 으로 2MB 창 밖을 읽는다. Linux `pci_ecam_map_bus()` 도 `busn -= cfg->busr.start` 를 먼저 한다.

### Part C — ECAM base 는 어디서 오나

ARM Linux 는 펌웨어가 알려 준 base 를 쓴다. 이 VM(Apple Virtualization.framework, ACPI 부팅)의 실제 MCFG 60바이트를 디코드하면:

| 오프셋 | 바이트 | 필드 | 값 |
|---|---|---|---|
| 0–3 | `4d 43 46 47` | Signature | "MCFG" |
| 4–7 | `3c 00 00 00` | Length | 60 |
| 8 | `01` | Revision | 1 |
| 9 | `71` | Checksum | 60바이트 합 % 256 = 0 (실측) |
| 10–15 / 16–23 | `41 50 50 4c 45 20` / `41 70 70 6c 65 20 56 7a` | OEM ID / Table ID | "APPLE " / "Apple Vz" |
| 28–31 / 32–35 | `41 41 50 4c` / `27 04 18 20` | Creator ID / Rev | "AAPL" / 0x20180427 (dmesg 와 같음) |
| 36–43 | 0 × 8 | 예약 | — |
| 44–51 | `00 00 00 40 00 00 00 00` | Base Address | 0x40000000 (버스 0 의 주소) |
| 52–53 | `00 00` | PCI Segment | 0 |
| 54 / 55 | `00` / `ff` | Start / End Bus | 00 / ff |
| 56–59 | 0 × 4 | 예약 | — |

dmesg 실출력이 같은 이야기를 한다: `ACPI: MCFG table detected, 1 entries` → `acpi PNP0A08:00: ECAM at [mem 0x40000000-0x4fffffff] for [bus 00-ff]` (이 줄은 `drivers/pci/ecam.c` 의 `pci_ecam_create()` 가 찍는다), `/proc/iomem` 에 `40000000-4fffffff : PCI ECAM`.

DT 로 부팅하는 보드(QEMU virt, 대부분의 임베디드 ARM)는 `compatible = "pci-host-ecam-generic"` 노드의 `reg` 와 `bus-range` 가 그 역할을 한다. lab 의 `virt.dts`:

```text
pcie@10000000 {
        reg = <0x40 0x10000000 0x00 0x10000000>;   /* 부모(루트)의 #address-cells=2, #size-cells=2 로 읽는다 */
        bus-range = <0x00 0xff>;
        #address-cells = <0x03>;                   /* 이건 자식(PCI 주소) 용 — reg 해석에 쓰면 틀린다 */
        compatible = "pci-host-ecam-generic";
```

→ base `0x40_1000_0000`, 크기 256MB = 버스 256개. 함정 두 가지:

| | ACPI MCFG | DT `reg` |
|---|---|---|
| base 가 가리키는 버스 | **버스 0** (start_bus 가 0x10 이어도) — Linux: `mcfg_addr + (bus_res->start << 20)` | **bus-range 의 첫 버스** (binding 문서: "The base address corresponds to the first bus in the bus-range") |
| 셀/바이트 | 리틀 엔디언 u64 | 32비트 big-endian 셀, 개수는 **부모** 노드의 `#address-cells/#size-cells` |
| 창이 작을 때 | — | Linux 는 bus-range 를 창 크기에 맞춰 줄이고 경고 (연습 5) |

그래서 이 레슨의 `ecam_win.phys` 는 둘 다 **bus_start 의 주소**로 정규화한다: MCFG 는 `base + (start << 20)`, DT 는 `reg` 그대로. 테스트: 같은 숫자 (base 0x30000000 / 0x80_0000_0000, 버스 0x10..)를 MCFG 로 읽으면 창이 +16MB 에서 시작하고 DT 로 읽으면 base 에서 시작한다.

**버스 스캔과 RRS.**

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">scan_bus(): dev 0..31 마다 (Linux pci_scan_slot / read_dev_vendor_id 와 같은 판정)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="170" height="44" rx="6" class="f-acc"/>
    <rect x="230" y="40" width="190" height="44" rx="6" class="f-warn"/>
    <rect x="460" y="40" width="270" height="44" rx="6" class="f-mut"/>
    <rect x="230" y="120" width="190" height="44" rx="6" class="f-mut"/>
    <rect x="460" y="120" width="270" height="44" rx="6" class="f-ok"/>
    <rect x="460" y="200" width="270" height="44" rx="6" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="28" y="58">fn0: rd32(0x00) → v</text><text x="28" y="74">(VID 하위 16 / DID 상위 16)</text>
    <text x="238" y="58">VID == 0x0001 ? (RRS)</text><text x="238" y="74">예 → 다시 읽기 (예산까지)</text>
    <text x="468" y="58">예산 소진 → not_ready++, 이 dev 건너뜀</text><text x="468" y="74">(커널: 1 ms 부터 2배씩, 최대 60 s)</text>
    <text x="238" y="138">FFFFFFFF / 0 / 0000FFFF</text><text x="238" y="154">/ FFFF0000 ? → 없음</text>
    <text x="468" y="138">있음: class/rev, Header Type 읽기</text><text x="468" y="154">lspci -n 줄로 기록</text>
    <text x="468" y="218">HT bit7 (MFD) = 1 일 때만</text><text x="468" y="234">fn1..7 도 같은 절차</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M190,62 H228"/><path d="M420,62 H458"/><path d="M325,84 V118"/>
    <path d="M420,142 H458"/><path d="M595,164 V198"/>
  </g>
  <text x="20" y="200" fill="currentColor" font-size="12">아니오 ↓ (RRS 아님)</text>
  <text x="20" y="270" fill="currentColor" font-size="12">MFD 를 무시하면: 함수 번호를 디코드하지 않는 01:00.0 이 8개로 보인다 (naive 8 vs 올바름 1, 실측)</text>
  <text x="20" y="290" fill="currentColor" font-size="12">SV 꺼짐이면 RC 가 하드웨어로 재시도 → 소프트웨어는 RRS 를 못 보지만 CPU 가 그동안 멈춘다 (g_hw_stall)</text>
</svg>
```

RRS(Request Retry Status — PCIe 6.0 에서 CRS, Configuration Request Retry Status 를 이렇게 바꿔 불렀고 Linux 도 `rrs` 로 이름을 바꿨다): 리셋 직후 장치가 config 요청을 받을 준비가 안 됐을 때 "다시 보내" 를 뜻하는 완료 상태다. SSD 로 치면 PERST# 가 풀리고 컨트롤러 부트 ROM 이 펌웨어를 NAND/NOR 에서 올리는 동안이다. 루트 포트의 Root Control 에 **RRS Software Visibility**(`PCI_EXP_RTCTL_RRS_SVE`, pci_regs.h 0x0010)가 켜져 있으면, VID 를 읽는 config read 에 한해 루트 컴플렉스가 VID 자리에 `0x0001`(PCI-SIG 예약 ID)을 넣어 돌려주므로 소프트웨어가 잠깐 쉬었다가 다시 볼 수 있다. 꺼져 있으면 루트 컴플렉스가 하드웨어로 재시도하므로 CPU 의 load 가 그동안 멈춘다. 스펙의 시간 숫자(리셋 후 첫 config 요청까지 100 ms, RRS 를 낼 수 있는 상한 1 s 등)는 기억에 의존한 값이라 스펙 확인 권장. Linux 실제 코드(`drivers/pci/probe.c`)는 1 ms 부터 두 배씩 쉬며 최대 60 s(`pci_scan_device` 의 `60*1000`) 기다리고, 1 s 가 넘으면 `"not ready after %dms; waiting"` 을 찍는다 — 현장에서 이 줄을 보면 "엔드포인트 펌웨어가 늦게 뜬다" 로 읽는다.

mock 의 RRS 결과 (실측, `solution.c` 출력):

| 시나리오 | 결과 |
|---|---|
| RRS 5회, SV 켜짐, 예산 8 | 발견, rrs_retries 5, VID 읽기 6번 |
| RRS 5회, 예산 3 | `not_ready = 1`, 4번 읽고 포기(남은 RRS 1) → 다음 스캔에서 재시도 1번 후 발견 |
| RRS 5회, SV 꺼짐 | 소프트웨어 재시도 0, `g_hw_stall = 5` (CPU 정지) |

**multi-function 규칙.** 단일 함수 장치는 fn0 에만 응답해야 하지만, 함수 번호를 디코드하지 않는 장치가 실제로 있다. 그래서 스캔은 fn0 의 Header Type bit7(`PCI_HEADER_TYPE_MFD`)이 1일 때만 fn1..7 을 본다 (Linux `pci_scan_slot` 의 `next_fn()`). mock 의 01:00.0 이 그런 장치이고, 규칙을 무시한 스캔은 같은 SSD 를 8개로 찾는다. ARI(Alternative Routing-ID, fn 0..255)는 이 규칙을 바꾸는데 PCIE-A06 의 몫이다. 버스 번호를 매기며 브리지 아래로 내려가는 DFS 열거는 PCIE-B09.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 79/79 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
```

`solution.c` 실행 출력 중 테스트 외 줄 (실제 출력):

```text
B same-register: 229376 reads compared (ECAM vs CF8/CFC), 5814 not all-1s, mismatches=0
B W1C trap: 32-bit RMW of COMMAND read 0xA0100412 -> STATUS now 0x0010 (error bits lost)
C MCFG: 1 entry, base 0x40000000 seg 0 bus 00-ff
C scan bus 00:
   00:00.0 0600: 106b:1a05
   00:06.0 0180: 1af4:1042 (rev 01)
   00:1c.0 0604: 1b36:000c
   00:1f.0 0601: 1234:abc0
   00:1f.3 0c05: 1234:abc3 (rev 05)
C bus 01: naive (fn0..7 all) found 8, MFD-aware found 1
C RRS (SV on, 5 not-ready replies): found 1, rrs_retries 5, VID reads 6
79/79 passed
```

위 두 줄 `00:00.0 0600: 106b:1a05`, `00:06.0 0180: 1af4:1042 (rev 01)` 은 VM 의 실제 `lspci -n` 출력과 글자까지 같다(테스트가 `strcmp` 로 확인). 실캡처 전체는 같은 폴더의 `capture_ecam_vm.txt` (MCFG 바이트, dmesg, `/proc/iomem`, devmem 12회, setpci, lspci, virt.dts 발췌).

## 7. 자주 하는 실수

**1. 범위 검사 없이 시프트.** `dev = 32` → `32 << 15 = 0x100000` = 다음 버스 dev 0, `fn = 8` → `8 << 12 = 0x8000` = 다음 dev 의 fn 0. 조용히 **다른 장치**에 접근한다(쓰기면 사고). 고친 코드: `cfg_check()` 를 오프셋 계산 **전에**.

**2. CF8 공식과 ECAM 공식을 섞는다.** dev 를 `<< 15` 로 CF8 에 넣으면 00:06.0 이 `0x80030000` → 실제로는 bus 3, dev 0 을 가리킨다. 반대로 ECAM 에 `<< 11` 을 쓰면 4KB 단위가 깨진다. 외우는 법: ECAM 은 "4KB 함수 → 12, 8개 → 15, 32개 → 20", CF8 은 "256B 함수 → 8, 8개 → 11, 32개 → 16".

**3. 32비트 RMW 로 Command 를 바꾼다.** 위 표대로 Status 의 W1C 오류 비트가 지워진다. 고친 코드: `ecam_read(…, PCI_COMMAND, 2, &c); ecam_write(…, PCI_COMMAND, 2, c | PCI_COMMAND_MASTER);` — 32비트만 되는 컨트롤러라면 연습 4 처럼 W1C 자리에 0 을 써서 합친다.

**4. CF8 에 reg 하위 2비트를 넣고 CFC 에서 늘 4바이트를 읽는다.** CF8 은 하위 2비트를 버리므로 0x3D(Int Pin)를 읽으려다 0x3C 의 dword 하위 바이트(Int Line)를 얻는다. 고친 코드: `inb(0xCFC + (reg & 3))`.

**5. MCFG base 를 창 주소로 그대로 쓴다 / DT reg 에 start_bus 를 또 더한다.** start_bus = 0 인 PC·VM 에선 둘 다 우연히 맞아 버그가 숨는다. 서버(세그먼트 여러 개)나 버스 0x80 부터 쓰는 두 번째 루트 컴플렉스에서 터진다. 고친 코드: MCFG 는 `base + (start << 20)`, DT 는 그대로, 접근은 `bus - bus_start`.

**6. VID == 0xFFFF 만 본다.** RRS 의 `0x0001` 을 "벤더 0x0001" 로 등록하거나, 0이나 `0xFFFF0000` 을 돌려주는 보드에서 유령 장치를 만든다. 고친 코드: Linux 와 같은 4개 패턴 + RRS 재시도(예산·타임아웃 포함).

**7. Header Type bit7 을 안 본다.** 함수 번호를 무시하는 장치가 8개로 보이고, 드라이버가 같은 장치를 8번 probe 한다(같은 BAR 를 8번 매핑, 인터럽트 8번 등록). 고친 코드: fn0 의 MFD 비트가 1일 때만 fn1..7.

**8. CF8 enable 비트(31)를 빼먹는다.** 데이터 포트 접근이 config 사이클이 아니게 되어 전부 `0xFFFFFFFF` — "장치가 하나도 없다" 로 오진한다.

## 8. 면접 Q&A

<details><summary>Q1. ECAM 과 CF8/CFC 의 차이는? 왜 ECAM 이 생겼나요?</summary>

CF8/CFC 는 IO 포트 두 개로 주소를 래치하고 데이터를 옮기는 2단계 방식이라 (1) 함수당 256B 까지만(reg 6비트) — PCIe 확장 영역(AER, SR-IOV, L1SS …)에 못 닿고, (2) 전역 래치라 락이 필요하고(SMP 면 spinlock + irqsave), (3) IO 포트 자체가 x86 개념이다. ECAM 은 config space 전체를 MMIO 창에 펼쳐 놓아 load/store 한 번이 config 요청 하나가 되고, 4KB 전부, 아키텍처 무관(ARM·RISC-V 도 같은 방식), 동시 접근에 래치 문제가 없다. 대가는 주소 공간 256MB(256 버스)와, base 를 펌웨어가 알려 줘야 한다는 점(MCFG/DT). 꼬리: "x86 에서 둘 다 있으면?" → Linux x86 의 `raw_pci_read()` 는 도메인 0 이고 reg < 256 이면 `raw_pci_ops`(보통 CF8 방식)를, 그 밖(확장 영역, 다른 세그먼트)에만 `raw_pci_ext_ops`(MMCONFIG)를 쓴다 (`arch/x86/pci/common.c`). 어떤 ops 가 잡히는지는 부팅 시 MCFG 검증 결과에 따라 달라진다.

</details>

<details><summary>Q2. 없는 장치는 왜 0xFFFF 로 읽히나요? 0xFFFFFFFF 를 받으면 무엇을 의심하죠?</summary>

받을 함수가 없으면 config 요청은 UR(PCI 시절 Master Abort)로 끝나는데, CPU 의 load 는 값을 받아야 끝나므로 루트 컴플렉스가 all-1s 를 채워 완료한다. VID 0xFFFF 는 할당되지 않는 값이라 "없음" 신호로 쓴다. 0xFFFFFFFF 가 나오면: (1) 진짜로 없음(빈 슬롯), (2) **있었는데 사라짐** — surprise removal, 링크 다운(LnkSta DLActive-), 장치 hang (MMIO 도 0xFFFFFFFF 이면 강하게 의심, PCIE-A07), (3) 접근 경로 문제 — 창 base 가 틀림, bus_start 처리 오류, 상위 브리지의 secondary/subordinate 가 안 맞아 요청이 전달되지 않음(PCIE-B07/B09), CF8 enable 비트. 1차 확인은 상위 브리지의 Secondary Status 의 Received Master Abort, 루트 포트 LnkSta, AER(PCIE-M12).

</details>

<details><summary>Q3. ARM 에서 Linux 는 ECAM base 를 어떻게 아나요?</summary>

ACPI 부팅이면 MCFG 테이블(세그먼트별 base, start/end bus) — 또는 호스트 브리지의 `_CBA` 메서드. `drivers/acpi/pci_mcfg.c` 가 읽고 `pci_ecam_create()` 가 `ECAM at [mem …] for [bus …]` 를 찍는다. 이 VM(Apple VZ)이 그 경로다: MCFG 60바이트, base 0x40000000, 버스 00-ff. DT 부팅이면 `compatible = "pci-host-ecam-generic"`(또는 SoC 고유 compatible + 고유 접근 함수) 노드의 `reg` 와 `bus-range` — QEMU virt 는 `reg = <0x40 0x10000000 0x0 0x10000000>`. 함정: MCFG base 는 버스 0 기준, DT reg 는 bus-range 첫 버스 기준. 그리고 ECAM 비호환 컨트롤러(DesignWare 등 일부)는 config 를 별도 iATU 창으로 보내므로 `pci_ecam_ops` 대신 드라이버 고유 `map_bus` 를 쓴다(Linux `drivers/pci/controller/`).

</details>

<details><summary>Q4. 16비트 접근은 짝수, 32비트는 4의 배수여야 하는 이유는?</summary>

PCIe config 요청 TLP 는 Length 가 항상 1 DW 이고 그 DW 안의 바이트는 First DW Byte Enable 로 고른다. DW 경계를 넘는 접근(0x02 에서 4바이트)은 TLP 하나로 표현이 안 된다. ECAM 창으로 들어온 오정렬 MMIO 를 루트 컴플렉스가 어떻게 처리할지는 구현 정의라 이식성 있는 드라이버는 막는다 — Linux `PCI_word_BAD (pos & 1)`, `PCI_dword_BAD (pos & 3)`. 게다가 ARM 의 Device 메모리에 대한 오정렬 접근은 그 자체로 alignment fault 다. 꼬리: "64비트 레지스터(BAR 쌍)는?" → 32비트 두 번, 순서와 중간 상태에 주의(BAR sizing 은 PCIE-B05, NVMe 64비트 레지스터는 PCIE-B12).

</details>

<details><summary>Q5. 컨트롤러가 32비트 config 쓰기만 지원합니다. Command 레지스터를 어떻게 바꾸죠?</summary>

그냥 32비트 RMW 를 하면 같은 dword 의 Status W1C 비트(읽은 1)를 써서 지운다 — Linux `pci_generic_config_write32()` 가 "may corrupt adjacent RW1C bits" 경고를 찍는 이유. Command/Status 에 한해서는 안전한 방법이 있다: Status 쪽 16비트를 **0 으로** 채워 쓴다. Status 비트는 전부 RO 이거나 W1C 라 0 쓰기는 아무 효과가 없다. 일반화하면 "건드리지 않을 바이트의 W1C 마스크를 알고 그 자리에 0" — 마스크를 모르는 범용 코드는 불가능하므로 경고가 남는다. 연습 4 가 naive(Status 0x0010 으로 지워짐) vs safe(0xA010 보존)를 실측한다.

</details>

<details><summary>Q6. SSD 가 리셋 후 몇백 ms 동안 VID 0x0001 을 돌려줍니다. 무슨 뜻이고 호스트·디바이스 양쪽에서 무엇을 하죠?</summary>

RRS(옛 CRS) 완료를 루트 컴플렉스가 SV 로 소프트웨어에 보여 준 것 — "아직 준비 안 됨". 호스트: 다시 읽기를 반복하되 sleep 을 두고(커널: 1 ms 부터 2배, 최대 60 s), 상한을 넘으면 "not ready" 로 포기·보고. 다른 장치 스캔을 막지 않게 장치별로 처리. 디바이스(SSD 펌웨어): PERST# 해제 후 config 요청에 정해진 시간 안에 응답해야 하고(스펙 시간값 확인 권장), 그 전에는 RRS 로 답하도록 컨트롤러 IP 를 설정해 두는 것이 부트 ROM/초기 펌웨어의 일. 펌웨어 업데이트 후 부팅이 길어져 BIOS 가 SSD 를 못 찾는 사고가 바로 이 지점이다. SV 가 꺼진 시스템이면 CPU 가 config read 에서 멈춰 있게 되고, 너무 길면 플랫폼 타임아웃(완료 타임아웃 → all-1s 또는 머신 체크)으로 번진다.

</details>

<details><summary>Q7. config write 는 posted 인가요? MMIO write 와 무엇이 다르죠?</summary>

아니다. CfgWr 는 non-posted 라 완료(Cpl)가 돌아와야 끝난다. 반면 MMIO 의 MWr 는 posted(완료 없음) — 그래서 MMIO 쓰기 뒤에 읽기로 "flush" 하는 관례가 있다(PCIE-B11). Linux 는 config 창을 가능하면 non-posted 속성으로 매핑한다(`include/linux/io.h` 의 `pci_remap_cfgspace()` → `ioremap_np()`, 주석: "PCI specifications … mandate non-posted configuration transactions"). 실무 의미: Command 에 Memory Space Enable 을 쓰고 곧바로 BAR 로 MMIO 를 날려도, config write 가 끝난 뒤에 MMIO 가 나간다.

</details>

<details><summary>Q8. 장치를 스캔할 때 fn1..7 은 언제 보나요? 안 지키면?</summary>

fn0 이 있고, fn0 의 Header Type bit7(MFD)이 1일 때만. fn0 없이 fn3 만 있는 장치는 규격상 없다(그래서 fn0 이 없으면 그 dev 는 끝). 안 지키면 함수 번호를 디코드하지 않는 장치가 8번 보이고(이 레슨 01:00.0, naive 8 vs 1), 드라이버가 같은 하드웨어를 8번 probe 해 BAR·IRQ 를 중복 등록한다. MF 장치의 함수 번호는 듬성듬성해도 된다(00:1f.0, 00:1f.3). ARI 가 켜진 장치는 fn 번호가 8비트가 되고 dev 번호를 쓰지 않는다 — PCIE-A06.

</details>

<details><summary>Q9. lspci 가 00:06.0 을 보여 주는데, 같은 값을 커널을 거치지 않고 확인하려면?</summary>

ECAM base 를 알면 devmem 으로 직접 읽는다: `0x40000000 + (0<<20 | 6<<15 | 0<<12 | 0)` = `0x40030000` → `busybox devmem 0x40030000 32` = `0x10421AF4`. 이 VM 에서 실제로 해 본 값이고, `setpci -s 00:06.0 VENDOR_ID.w` (sysfs `config` 파일 경유)의 `1af4` 와 같다. 주의: `CONFIG_STRICT_DEVMEM` 이 켜져도 RAM 이 아닌 영역은 허용되지만 `IO_STRICT_DEVMEM` 이면 드라이버가 잡은 영역은 막힌다; 그리고 config **쓰기**를 devmem 으로 하는 건 커널의 `pci_lock` 과 캐시된 상태를 우회하므로 bring-up 실험에서만. lspci/setpci/sysfs 실전은 PCIE-B06.

</details>

## 9. 추가 연습

### 연습 1 — 물리 주소 → BDF

dmesg 나 버스 애널라이저가 `0x40030010` 같은 주소를 줬을 때 사람이 읽는 형태로 바꾸는 `ecam_decode(win, phys, &loc)` 를 짜라. VM 창(0x40000000, 버스 00-ff)에서 `0x40030010`, `0x4003003C`, `0x4FFFFFFF`, 창 밖 2개, 그리고 seg 1·버스 0x10 부터인 창의 주소를 디코드하고, 고정 시드 10만 개 왕복을 확인하라.

<details><summary>답</summary>

`off = phys − win.phys` 가 `[0, 버스 수 << 20)` 안인지 먼저 보고, `bus = bus_start + (off >> 20)`, `dev = (off >> 15) & 31`, `fn = (off >> 12) & 7`, `reg = off & 0xFFF`. 실측: `0x40030010 → 0000:00:06.0 reg 0x010`(BAR0), `0x400f8000 → 0000:00:1f.0 reg 0x000`, `0x4fffffff → 0000:ff:1f.7 reg 0xfff`. 0x50000000(창 다음 = VM 의 MMIO 창 시작)과 0x3FFFFFFF 는 오류. seg 1 창(phys 0x80_0100_0000, 버스 0x10..)의 +0x1FB00E 는 `0001:11:1f.3 reg 0x0e`. 이 역변환을 손으로 할 수 있으면 "0x4003xxxx 에서 SError" 같은 로그를 바로 "00:06.0 의 config" 로 읽는다.

</details>

### 연습 2 — CF8/CFC 경쟁과 락

mock 의 `io_out(0xCF8)` 직후에 대기 중인 인터럽트가 실행되도록 하고(ISR 은 00:1f.0 의 VID 를 CF8 로 읽음), 락 없는 `conf1_read(00:06.0 VID)` 가 무엇을 읽는지 보라. `irq_save/irq_restore` 로 감싼 `conf1_read_locked` 와, 같은 상황의 ECAM 읽기도 비교하라.

<details><summary>답</summary>

실측: 락 없음 → main 이 `0xABC01234`(ISR 이 래치를 00:1f.0 으로 바꿨다), 원하던 값은 `0x10421AF4`. 락 → main `0x10421AF4`, ISR 은 unlock 뒤에 실행돼 `0xABC01234`(둘 다 맞음). ECAM → 인터럽트가 끼어들 지점 자체가 없어 `0x10421AF4`, 대기 인터럽트는 그대로 남음. 실제 커널은 SMP 라 irq 끄기로는 부족하고 spinlock 까지 필요 — Linux `pci_config_lock` 이 `raw_spin_lock_irqsave`. RTOS 에서 config 를 여러 태스크가 만지면 같은 문제다(mutex + ISR 에서 config 금지).

</details>

### 연습 3 — AMD 식 CF8 확장과 aliasing 판별

`conf1_ext_address()` 로 비트 27:24 에 reg[11:8] 을 넣어 01:00.0 의 0x100(AER 헤더)을 읽어라 — 칩셋이 확장을 해석할 때와 무시할 때 각각. 그리고 Linux `pci_cfg_space_size_ext()` 처럼 "0x100 이 all-1s 이거나 0x100, 0x200, …, 0xF00 이 모두 헤더(0x00)와 같으면 256B" 로 확장 영역의 진위를 판정하라.

<details><summary>답</summary>

`conf1_ext_address(1,0,0,0x100) = 0x81010000`, `(0,6,0,0xF3C) = 0x8F00303C`. 확장을 해석하면 `0x00020001`, 무시하면 예약 비트가 버려져 reg 0x000 → `0x00101B36`(VID/DID) — **오류 없이 틀린 값**이다. 판정 실측: CF8(확장 무시) 256, CF8(확장) 4096, ECAM 4096, 호스트 브리지 00:00.0 ECAM 4096(0x100 = 0, VM 의 sysfs config 크기 4096 과 일치), 없는 장치 256. 커널이 aliasing 검사를 하는 이유가 바로 이 "접힘" 이다. 실제 커널은 호스트 브리지·PCIe·PCI-X 장치에만 이 검사를 하므로 PCIe cap 이 없는 VM 의 virtio(00:06.0)는 검사 없이 256B 다.

</details>

### 연습 4 — 32비트 쓰기 전용 컨트롤러

mock 이 16비트 config 쓰기를 거부하게(`g_only32`) 하고, Status 오류 비트가 선 00:06.0 의 Command 를 (a) Linux `pci_generic_config_write32` 식 naive RMW, (b) W1C 마스크를 아는 safe RMW 로 바꿔라. Status 만 지우는 쓰기에서 Command 가 보존되는지도 보라.

<details><summary>답</summary>

실측: 16비트 쓰기는 하드웨어 오류로 무시(`g_hw_bad` +1). naive → Status `0x0010`(Master Abort·Parity 증거 소실), safe → `0xA010` 보존, Command `0x0416`. safe 의 핵심 한 줄: `t = (old & ~mine & ~w1c_mask32) | (val << sh)` — 내가 안 쓰는 바이트의 W1C 자리는 0 으로. Status 만 지울 때(`0x2000`)는 Command 쪽이 RW 라 읽은 값 그대로 써도 무해 → Status `0x8010`, Command `0x0416`. 이런 컨트롤러는 실제로 있어서(ECAM 비호환 SoC 일부) Linux 가 경고를 남긴다.

</details>

### 연습 5 — 창 크기 ↔ 버스 수

`ecam_fit(size, lo, hi, &new_hi)`: 창이 bus-range 를 다 못 담으면 Linux `pci_ecam_create()` 처럼 `bus_range_max = size >> 20` 으로 줄이고 1 을 돌려준다. VM(256MB, 00-ff), 16MB 창 + 00-ff, 2MB 창 + 0x40-0x7f, 0.5MB 창을 확인하라.

<details><summary>답</summary>

256MB → 그대로 00-ff. 16MB → 00-0f 로 잘림(버스 16개). 2MB, 첫 버스 0x40 → 0x40-0x41. 0.5MB → 버스 0개라 오류. 버스 1개 = 32 × 8 × 4KB = 1MB 를 코드로도 확인. 현장 의미: DT 의 `reg` 크기를 줄여 쓴 보드에서 스위치 아래 버스 번호가 창을 넘으면, 그 장치들은 열거 단계에서 "보이지 않는다" — dmesg 의 bus-range 축소 경고를 먼저 본다. (QEMU virt 는 highmem ECAM 을 못 쓸 때 0x3f000000 의 16MB 창으로 내려간다고 알려져 있다 — QEMU 소스 확인 권장.)

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 23/23 passed
```

```text
practice 1: 0x40030010 -> 0000:00:06.0 reg 0x010
practice 1: 0x400f8000 -> 0000:00:1f.0 reg 0x000
practice 1: 0x4fffffff -> 0000:ff:1f.7 reg 0xfff
practice 2: no lock  -> main read 0xABC01234 (wanted 0x10421AF4), ISR read 0xABC01234
practice 2: locked   -> main read 0x10421AF4, ISR read 0xABC01234 (after unlock)
practice 3: cfg size 01:00.0 via CF8(no ext)=256, CF8(ext)=4096, ECAM=4096; host bridge 00:00.0 via ECAM=4096
practice 4: Status after naive RMW 0x0010, after safe RMW 0xA010, Command 0x0416
23/23 passed
```

## 10. 핵심 정리

- ECAM 오프셋 = `bus<<20 | dev<<15 | fn<<12 | reg` (버스 1MB, 장치 32KB, 함수 4KB). 물리 주소 = 창 주소 + 오프셋, bus 는 **창 첫 버스 기준**. VM: 00:06.0 = 0x40030000.
- CF8 = `0x80000000 | bus<<16 | dev<<11 | fn<<8 | (reg & 0xFC)`, 데이터 포트 `0xCFC + (reg & 3)`. 256B 뿐, 두 단계 + 전역 래치 → 락 필수. enable 비트 빠지면 all-1s.
- 접근 규칙: 폭 1/2/4, 자연 정렬(한 DW 안), dev ≤ 31, fn ≤ 7, ECAM reg < 4096. 검사는 하드웨어에 닿기 전에, 오류면 음수 + all-1s.
- 없는 함수 = UR → 루트 컴플렉스가 all-1s → VID 0xFFFF. 이것은 데이터다. 없음 패턴: FFFFFFFF / 0 / 0000FFFF / FFFF0000.
- VID 0x0001 = RRS(옛 CRS): 장치가 아직 준비 중(SSD 펌웨어 로딩). SV 켜짐 → 소프트웨어 재시도(Linux 1 ms→2배, 최대 60 s), 꺼짐 → CPU 정지.
- fn1..7 은 fn0 의 Header Type bit7 이 1일 때만. 안 지키면 유령 장치.
- Command 는 16비트로 쓴다. 32비트 RMW 는 Status W1C 오류 비트를 지운다(Linux 의 write32 경고).
- ECAM base: ACPI MCFG(base = 버스 0 기준, LE u64, 체크섬) 또는 DT `pci-host-ecam-generic` 의 reg(첫 버스 기준, 부모 셀 수, BE 셀) + bus-range. 창 크기 = 버스 수 × 1MB.

## 참고자료

- PCI-SIG, *PCI Express Base Specification* (5.0/6.0) — "Enhanced Configuration Access Mechanism (ECAM)" 절(7.2 근처), Configuration Request Retry Status / RRS(6.0 에서 이름 변경), Root Control 의 RRS Software Visibility. 절 번호·시간 값은 판마다 달라 원문 확인 권장: <https://pcisig.com/specifications>
- PCI-SIG, *PCI Local Bus Specification 3.0* — §3.2.2.3.2 Configuration Mechanism #1 (CF8/CFC), §3.2.5 config 트랜잭션은 non-posted (Linux 주석이 이 절을 인용).
- PCI-SIG, *PCI Firmware Specification 3.x* — MCFG 테이블 형식. ACPI Specification — 시스템 테이블 공통 헤더와 체크섬.
- Linux 6.18 소스 (lab `/var/lab/src/linux-6.18.55`): `include/uapi/linux/pci_regs.h`(상수), `include/linux/pci-ecam.h`(`PCIE_ECAM_*`), `drivers/pci/ecam.c`(`pci_ecam_create`, `pci_ecam_map_bus`), `drivers/pci/access.c`(`PCI_word_BAD`, `pci_generic_config_write32` 경고), `drivers/pci/pci.h`(`PCI_CONF1_*`), `arch/x86/pci/direct.c`(CF8/CFC, `pci_config_lock`, AMD 확장 비트), `drivers/pci/probe.c`(`pci_bus_generic_read_dev_vendor_id`, `pci_bus_wait_rrs`, `pci_cfg_space_size_ext`), `drivers/acpi/pci_mcfg.c`, `include/linux/io.h`(`pci_remap_cfgspace`), `Documentation/devicetree/bindings/pci/host-generic-pci.yaml`.
- Linux kernel docs, PCI subsystem: <https://docs.kernel.org/PCI/index.html>
- Mike Jackson, Ravi Budruk (MindShare), *PCI Express Technology 3.0* — Configuration 장 (ECAM, legacy mechanism, enumeration, CRS).
- 이 레슨의 실캡처: `capture_ecam_vm.txt` (Lima VM `elinux`, Apple Virtualization.framework 게스트, 커널 7.0.0-38; QEMU virt DT 는 lab `virt.dts`).
- 관련 레슨: PCIE-B03 (Type 0 헤더), PCIE-B04 (capability), PCIE-B05 (BAR sizing), PCIE-B06 (lspci/setpci/sysfs), PCIE-B07 (Type 1 라우팅), PCIE-B09 (DFS 열거), PCIE-B10 (TLP 헤더, Byte Enable), PCIE-B11 (posted/non-posted), PCIE-B12 (NVMe 레지스터), PCIE-A06 (ARI/SR-IOV), PCIE-A07 (surprise removal), PCIE-M12 (AER), BM-B04 (W1C·RMW 함정), ELX-B 레슨들 (sysfs).
