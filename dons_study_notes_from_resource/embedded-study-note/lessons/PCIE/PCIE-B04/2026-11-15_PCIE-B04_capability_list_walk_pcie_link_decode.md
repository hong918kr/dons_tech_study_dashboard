---
id: PCIE-B04
time: 60분
updated: 2026-11-15
---
# PCIE-B04 Capability 리스트 순회와 PCIe Capability 디코드

## 학습 목표

- config 공간의 두 연결 리스트 — legacy(0x34 에서 시작, 8비트 ID + 8비트 next) 와 extended(0x100 에서 시작, 32비트 헤더 = ID 15:0 · Version 19:16 · Next 31:20) — 를 바이트 덤프에서 손으로, 그리고 코드로 따라간다.
- 깨진 리스트(루프, 0x40 / 0x100 아래를 가리키는 포인터, 비정렬, 구조체가 공간 끝을 넘음, all-1s)를 **멈추고 어디가 잘못됐는지 말하는** 워커를 malloc 없이 짠다. 왜 한계가 48 과 960 인지 숫자로 설명한다.
- PCI Express Capability 에서 LnkCap(최대 속도·폭·ASPM·포트 번호), LnkCtl, LnkSta(현재 속도·협상 폭·Training·DL Active), LnkCap2/LnkCtl2 를 비트 단위로 디코드한다.
- "LnkSta x2, LnkCap x4" 를 보고 바로 "고장" 이라고 하지 않는다 — **링크 양쪽**의 능력과 Target Link Speed 를 함께 보고 기대값을 계산하는 트리아지를 짠다 (QEMU 실제 캡처 + 가상 Gen4 SSD 덤프).
- 같은 덤프를 실제 `lspci -vvv -F` 로 디코드해 내 디코더와 맞춰 본다.

## 1. 문제

Rivian HM 스크린의 "lspci 로 링크 상태를 어떻게 보나요?" 는 결국 **LnkSta 레지스터를 어떻게 찾느냐**는 질문이다 — 그 레지스터는 고정 주소가 없고 capability 연결 리스트를 따라가야 나온다. 드라이버 probe(`pci_find_capability()`), 실리콘 검증 스크립트, SSD 컨트롤러 bring-up 이 모두 이 순회 위에 서 있다.

config 공간(PCIE-B03)의 앞 64바이트(0x00–0x3F)는 모양이 고정된 헤더다. 그 뒤는 장치마다 다르다: 전원 관리(PM), 인터럽트(MSI/MSI-X), PCIe 링크 정보 같은 **기능 블록(capability)** 을 장치 설계자가 아무 위치에나 놓고, 블록마다 "다음 블록은 어디" 포인터를 둔다. 즉 **config 공간 안의 단방향 연결 리스트(linked list)** 다.

| 영역 | 범위 | 리스트 | 시작 | 포인터 폭 | 접근 |
|---|---|---|---|---|---|
| 표준 헤더 | 0x00–0x3F | — | — | — | CF8/CFC, ECAM |
| legacy cap | 0x40–0xFF | ID 8비트 + Next 8비트 | `Cap Ptr` (0x34, CardBus 는 0x14) | 8비트 | CF8/CFC, ECAM |
| extended cap | 0x100–0xFFF | ID 16 + Ver 4 + Next 12 | 항상 0x100 | 12비트 | **ECAM 만** (PCIE-B08) |

legacy 리스트는 Status 레지스터의 bit 4 `Capabilities List`(`PCI_STATUS_CAP_LIST` = 0x10) 가 1 일 때만 의미가 있다. 이 레슨에서 쓰는 ID (Linux `include/uapi/linux/pci_regs.h` 기준):

| legacy ID | 이름 | 최소 크기 | extended ID | 이름 | 최소 크기 |
|---|---|---|---|---|---|
| 0x01 | Power Management | 8 | 0x0001 | AER (Advanced Error Reporting) | 0x2C (루트 포트 0x38) |
| 0x05 | MSI | 0x0A–0x18 (플래그) | 0x0003 | Device Serial Number | 12 |
| 0x09 | Vendor Specific | 길이 바이트 | 0x000D | ACS | 8 |
| 0x0D | Bridge Subsystem VID | 8 | 0x0010 | SR-IOV | 0x40 |
| 0x10 | **PCI Express** | v1 0x14 / v2 0x34 | 0x0018 | LTR (Latency Tolerance Reporting) | 8 |
| 0x11 | MSI-X | 12 | 0x001E | L1 PM Substates | 0x10 |
| | | | 0x0019 / 0x0026 | Secondary PCIe / Physical Layer 16 GT/s | 0x0C / 0x20 |

"최소 크기" 는 이 레슨이 RANGE 검사에 쓰는 값이다 (`PCI_PM_SIZEOF`, `PCI_CAP_MSIX_SIZEOF`, `PCI_CAP_EXP_ENDPOINT_SIZEOF_V1/V2`, `PCI_EXT_CAP_DSN_SIZEOF` 등 헤더 상수, AER 는 헤더 로그 끝 0x2C).

테스트용 주인공은 **가상 덤프**(fictional) 두 개다 — Gen4 x4 NVMe SSD 인데 링크가 **Gen4 x2** 로 올라온 상태, 그리고 그 SSD 가 꽂힌 루트 포트(Type 1, 256B 만 캡처). VID 0xF00D 는 일부러 만든 값이다. 여기에 **실제 캡처**: Lima VM 안 QEMU 10.2.1 의 `pcie-root-port` + `nvme` 4KB config 덤프(QEMU 가 흉내 낸 값, `qemu_capture_xxxx.txt`). 가상 SSD 덤프의 0 아닌 행 (`./solution -d` 실제 출력):

```text
00: 0d f0 04 54 06 04 10 00 01 02 08 01 00 00 00 00
10: 04 00 00 fe 00 00 00 00 00 00 00 00 00 00 00 00
20: 00 00 00 00 00 00 00 00 00 00 00 00 0d f0 01 00
30: 00 00 00 00 40 00 00 00 00 00 00 00 ff 01 00 00
40: 01 50 03 00 08 00 00 00 00 00 00 00 00 00 00 00
50: 05 70 80 01 00 00 00 00 00 00 00 00 00 00 00 00
70: 10 b0 02 00 22 80 00 10 3f 29 00 00 44 08 07 00
80: 42 01 24 10 00 00 00 00 00 00 00 00 00 00 00 00
90: 00 00 00 00 1f 08 00 00 00 04 00 00 1e 00 00 00
a0: 04 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
b0: 11 c0 1f 80 00 20 00 00 00 30 00 00 00 00 00 00
c0: 09 00 10 01 00 00 00 00 00 00 00 00 00 00 00 00
100: 01 00 82 14 00 00 00 00 00 00 00 00 00 00 00 00
110: 41 00 00 00 00 20 00 00 00 00 00 00 00 00 00 00
140: 00 00 00 00 00 00 00 00 18 00 01 15 03 10 03 10
150: 1e 00 01 16 1f 28 28 00 00 00 00 00 00 00 00 00
160: 03 00 01 17 04 03 02 01 0d 0c 0b 0a 00 00 00 00
170: 19 00 01 1a 00 00 00 00 00 00 00 00 00 00 00 00
1a0: 26 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">가상 SSD 덤프의 두 연결 리스트 (위 바이트 그대로)</text>
  <text x="10" y="40" fill="currentColor">legacy: 0x40–0xFF, 포인터 8비트, Status.CapList = 1 일 때만</text>
  <g stroke="currentColor">
    <rect x="10" y="50" width="88" height="56" class="f-mut"/>
    <rect x="116" y="50" width="88" height="56" class="f-acc"/>
    <rect x="222" y="50" width="88" height="56" class="f-acc"/>
    <rect x="328" y="50" width="88" height="56" class="f-ok"/>
    <rect x="434" y="50" width="88" height="56" class="f-acc"/>
    <rect x="540" y="50" width="88" height="56" class="f-acc"/>
    <rect x="646" y="50" width="84" height="56" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="68">[0x34]</text><text x="18" y="84">Cap Ptr</text><text x="18" y="100" font-family="monospace">= 40</text>
    <text x="124" y="68">[40] ID 01</text><text x="124" y="84">PM</text><text x="124" y="100" font-family="monospace">next 50</text>
    <text x="230" y="68">[50] ID 05</text><text x="230" y="84">MSI</text><text x="230" y="100" font-family="monospace">next 70</text>
    <text x="336" y="68">[70] ID 10</text><text x="336" y="84">PCI Express</text><text x="336" y="100" font-family="monospace">next b0</text>
    <text x="442" y="68">[b0] ID 11</text><text x="442" y="84">MSI-X</text><text x="442" y="100" font-family="monospace">next c0</text>
    <text x="548" y="68">[c0] ID 09</text><text x="548" y="84">Vendor</text><text x="548" y="100" font-family="monospace">next 00</text>
    <text x="656" y="84">끝 (0)</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5">
    <line x1="98" y1="78" x2="114" y2="78"/><line x1="204" y1="78" x2="220" y2="78"/><line x1="310" y1="78" x2="326" y2="78"/>
    <line x1="416" y1="78" x2="432" y2="78"/><line x1="522" y1="78" x2="538" y2="78"/><line x1="628" y1="78" x2="644" y2="78"/>
  </g>
  <text x="10" y="140" fill="currentColor">extended: 0x100–0xFFF, 헤더 32비트, 포인터 12비트, 항상 0x100 에서 시작 (ECAM 필요)</text>
  <g stroke="currentColor">
    <rect x="10" y="150" width="88" height="56" class="f-warn"/>
    <rect x="116" y="150" width="88" height="56" class="f-acc"/>
    <rect x="222" y="150" width="88" height="56" class="f-acc"/>
    <rect x="328" y="150" width="88" height="56" class="f-acc"/>
    <rect x="434" y="150" width="88" height="56" class="f-acc"/>
    <rect x="540" y="150" width="88" height="56" class="f-acc"/>
    <rect x="646" y="150" width="84" height="56" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="168">[100] v2</text><text x="18" y="184">AER 0001</text><text x="18" y="200" font-family="monospace">next 148</text>
    <text x="124" y="168">[148] v1</text><text x="124" y="184">LTR 0018</text><text x="124" y="200" font-family="monospace">next 150</text>
    <text x="230" y="168">[150] v1</text><text x="230" y="184">L1SS 001e</text><text x="230" y="200" font-family="monospace">next 160</text>
    <text x="336" y="168">[160] v1</text><text x="336" y="184">DSN 0003</text><text x="336" y="200" font-family="monospace">next 170</text>
    <text x="442" y="168">[170] v1</text><text x="442" y="184">SecPCIe 0019</text><text x="442" y="200" font-family="monospace">next 1a0</text>
    <text x="548" y="168">[1a0] v1</text><text x="548" y="184">PL16GT 0026</text><text x="548" y="200" font-family="monospace">next 000</text>
    <text x="656" y="184">끝 (0)</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5">
    <line x1="98" y1="178" x2="114" y2="178"/><line x1="204" y1="178" x2="220" y2="178"/><line x1="310" y1="178" x2="326" y2="178"/>
    <line x1="416" y1="178" x2="432" y2="178"/><line x1="522" y1="178" x2="538" y2="178"/><line x1="628" y1="178" x2="644" y2="178"/>
  </g>
  <text x="10" y="240" fill="currentColor" font-weight="bold">config 공간 지도 (비율 아님)</text>
  <g stroke="currentColor">
    <rect x="10" y="250" width="120" height="34" class="f-mut"/>
    <rect x="130" y="250" width="200" height="34" class="f-acc"/>
    <rect x="330" y="250" width="400" height="34" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="272">0x00–0x3F 헤더</text>
    <text x="138" y="272">0x40–0xFF legacy caps</text>
    <text x="338" y="272">0x100–0xFFF extended caps (4KB 중 3840B)</text>
    <text x="10" y="306">CF8/CFC 는 앞 256B 만 → 256B 덤프(lspci -xxx)에서는 extended 리스트를 아예 볼 수 없다 (CW_NO_EXT)</text>
    <text x="10" y="324">순서는 오프셋 순일 필요 없다: QEMU nvme 는 40 → 80 → 60 (아래 Part A)</text>
  </g>
</svg>
```

### Part A — 두 리스트 순회와 PCIe Capability 디코드

1. `cap_walk_legacy(cfg, len, out, cap)`: Status.CapList 확인 → 0x34(CardBus 면 0x14) → next 를 0 까지. 각 항목 `{off, id, next}` 를 호출자 배열에 기록, 결과 `walk_result {status, n, err_ptr, err_from}`.
2. `cap_walk_ext(...)`: 0x100 부터, 헤더를 `ID = h & 0xFFFF`, `Ver = (h >> 16) & 0xF`, `Next = h >> 20` 으로 나눈다. 256B 덤프 또는 0x100 = 0xFFFFFFFF → `CW_NO_EXT`, 0x100 = 0 → 빈 리스트.
3. `pcie_decode(cfg, len, off, &pi)`: Flags(버전·장치/포트 타입·Slot), DevCap/DevCtl 의 MPS·MRRS, LnkCap(SLS, MLW, ASPMS, DLLLARC, Port #), LnkCtl(ASPMC, RCB, LD, CCC), LnkSta(CLS, NLW, LT, SLC, DLLLA), v2 면 LnkCap2·LnkCtl2(TLS).
4. 테스트: 가상 SSD(legacy 5개, extended 6개), 가상 루트 포트(Type 1 도 0x34), QEMU nvme / root port 실제 캡처의 리스트와 필드 값 전부.

### Part B — 깨진 리스트에서 멈추기

각 오류를 덤프에 주입하고 **status, 그 전까지 모은 개수 n, 문제 포인터, 그 포인터를 읽은 위치**를 단언한다.

| # | 주입 | 기대 결과 |
|---|---|---|
| B1 | MSI.next(0x51) = 0x40 / PCIe.next = 자기 자신 | `LOOP`, n = 2 / 3 |
| B2 | PM.next = 0x20, Cap Ptr = 0x3C | `PTR_LOW` (헤더 안을 가리킴) |
| B3 | PM.next = 0x51 | `ALIGN` (하위 2비트는 예약) |
| B4 | PCIe cap 을 0xD8 로 옮김 (v2 는 0x34 바이트 필요) | `RANGE`, `pcie_decode` 도 RANGE |
| B5 | cap 자리가 FF FF FF FF / 장치 전체가 FF | `ALLONES` |
| B6 | Status.CapList = 0 (0x34 는 그대로) | `NONE` — 0x34 를 믿지 않는다 |
| B7 | 0x40..0xFC 에 4바이트 cap 48개 체인 / 끝을 처음으로 | `OK` n = 48 (한계 경계) / `LOOP` n = 48 |
| B8 | extended: 루프, next 0x0F0, next 0x152, 0xFF0 의 AER, 0x100 = FFFFFFFF, 중간 FFFFFFFF, 960개 체인 | `LOOP`·`PTR_LOW`·`ALIGN`·`RANGE`·`NO_EXT`·`ALLONES`·`OK n=960` |
| B9 | 고정 시드 xorshift32 무작위 4KB 덤프 1000개 | 항상 끝남, n ≤ 48 / 960, ASan 깨끗 |

### Part C — 링크 트리아지: 양쪽을 본다

`link_triage(dsp, usp, &r)`: 링크 위쪽 포트(루트 포트·스위치 다운스트림 포트)와 아래쪽(엔드포인트) 덤프를 받아

- 기대 속도 = 양쪽 **지원 속도 벡터**의 공통 최고값, 다운스트림 포트의 LnkCtl2.TLS 가 더 낮으면 그 값 (`TARGET_CAPPED`, 설정이지 결함 아님)
- 기대 폭 = 양쪽 LnkCap.MLW 의 최소
- 실제 = 다운스트림 포트의 LnkSta

로 `SPEED_LOW / WIDTH_LOW / LINK_DOWN / TRAINING / MISMATCH / ASPM_BAD / NO_PCIE` 플래그를 세운다. 가상 RP+SSD → `WIDTH_LOW` 만, QEMU root port(16GT/s x32 가능) + QEMU nvme(2.5GT/s x1 가능) → **정상**. 이어 링크 다운, Gen1 고착, TLS 제한, Training 비트 고착, 양쪽 불일치, 지원 안 하는 ASPM 켜짐을 각각 만든다.

## 2. Problem (English)

> **Capability walker + link check.** You get a function's configuration space as `lspci -xxx` (256 B) or `lspci -xxxx` (4 KB) text, already parsed into `uint8_t cfg[len]`.
>
> ```c
> walk_result cap_walk_legacy(const uint8_t *cfg, size_t len, cap_ent *out, int cap);
> walk_result cap_walk_ext   (const uint8_t *cfg, size_t len, cap_ent *out, int cap);
> int         pcie_decode    (const uint8_t *cfg, size_t len, uint16_t off, pcie_info *pi);
> int         link_triage    (const uint8_t *dsp, size_t dlen, const uint8_t *usp, size_t ulen, link_report *r);
> ```
>
> - Legacy list: starts at the byte at 0x34 if `Status[4]` is set; each entry is `ID[7:0], Next[15:8]`. Valid pointers are ≥ 0x40 and DWORD-aligned.
> - Extended list: starts at 0x100 (4 KB space only); header = `ID[15:0], Ver[19:16], Next[31:20]`; valid next is 0 or ≥ 0x100, DWORD-aligned.
> - Never loop forever, never read outside `cfg[0..len)`, no heap. On a broken list return what you collected so far plus the offending pointer and where it was read from.
> - Decode the PCI Express capability: device/port type, max/current link speed (codes 1..6 = 2.5/5/8/16/32/64 GT/s) and width, ASPM support/control, DL Link Active.
> - `link_triage`: given both ends of a link, report whether it trained below what both sides support.
>
> Example (fictional Gen4 x4 SSD): legacy list `[40] PM → [50] MSI → [70] PCIe → [b0] MSI-X → [c0] Vendor`, extended `[100] AER v2 → [148] LTR → … → [1a0] PL 16GT/s`. `LnkCap = 0x00070844` → 16 GT/s x4, ASPM L1; `LnkSta = 0x1024` → 16 GT/s x2. With the root port also x4-capable: `flags = WIDTH_LOW`.
>
> Follow-ups: (1) Why is a hop limit of 48 enough for the legacy list? (2) `LnkSta` says x1 on a root port that can do x32 — is that a bug? (3) The ext header at 0x100 reads `0xFFFFFFFF` — what does that tell you? (4) Linux masks the low two pointer bits; should a validation tool?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 다섯 개: A1 `cap_walk_legacy`, A2 `cap_walk_ext` (둘 다 Part B 오류 경로 포함), A3 `pcie_decode`, C1 `pcie_speed_vector`, C2 `link_triage` 의 판정부. lspci 텍스트 파서(PCIE-B03 축약판), 이름 표, 최소 크기 표, 방문 비트맵 도우미, 보고서 출력, 가상/QEMU 덤프는 주어진다. 스텁은 "빈 리스트 / 디코드 실패 / 판정 없음" 을 돌려줘 멈추지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:682  r.status == CW_OK && ids_are(g_e, r.n, want, 5)
FAIL starter.c:690  pcie_decode(g_ssd, 4096, 0x70, &p) == 0
…
FAIL starter.c:849  strstr(g_rep, "LnkSta: Speed 16GT/s, Width x2 (below own cap)") != NULL
cap_report(QEMU root port 00:02.0):
FAIL starter.c:852  strstr(g_rep, "Width x32") != NULL && strstr(g_rep, "[148 v1] Access Control Services") != NULL
14/71 passed
```

(exit 1. 통과하는 14개는 주어진 텍스트 파서 8개, 이름 표 2개, 그리고 스텁이 우연히 맞히는 4개 — QEMU nvme 의 빈 확장 리스트, "MSI 위치는 PCIe cap 아님", 무작위 덤프에서 멈춤, "cap 리스트 없으면 NO_PCIE".) A1–A3 만 채우고 C1/C2 를 비워 두면 **58/71** — Part C 13개만 남는다.

## 4. 힌트

<details><summary>힌트 1 — 포인터 하나를 받을 때마다 검사 순서</summary>

`ptr == 0` 이면 끝. 아니면 (1) `< 0x40`(extended 는 `< 0x100`) (2) `ptr & 3` (3) 이미 방문했나 (4) 홉 수 > 한계 (5) 그 자리의 ID 가 0xFF (extended 는 헤더 0xFFFFFFFF) (6) `ptr + 최소 크기 > 공간 크기` (7) 출력 배열이 찼나. 전부 통과해야 기록하고 next 로 간다. 실패하면 `err_ptr = ptr`, `err_from = 그 포인터를 읽은 오프셋` 을 남기고 즉시 반환.

</details>

<details><summary>힌트 2 — 방문 비트맵은 4바이트 칸 하나에 1비트</summary>

포인터는 4바이트 정렬이므로 `ptr / 4` 가 칸 번호다. legacy 256B → 64비트(`uint32_t[2]`), extended 4KB → 1024비트(`uint32_t[32]` = 128바이트, 스택). 정렬 검사를 **먼저** 해야 `ptr / 4` 가 서로 다른 포인터를 같은 칸으로 뭉개지 않는다.

</details>

<details><summary>힌트 3 — extended 헤더 나누기와 "빈 리스트"</summary>

`h = cfg_rd32(cfg, off)` (바이트 조립, 포인터 캐스트 금지). `id = h & 0xFFFF`, `ver = (h >> 16) & 0xF`, `next = (h >> 20) & 0xFFF`. 0x100 의 헤더가 0 이면 "확장 cap 없음" 이 정상이고(QEMU nvme 가 그렇다), 0xFFFFFFFF 면 확장 공간 자체에 닿지 못한 것이다 (CF8 경로, 장치 없음).

</details>

<details><summary>힌트 4 — LnkCap / LnkSta 필드 위치</summary>

PCIe cap 기준 +0x0C LnkCap(32), +0x10 LnkCtl(16), +0x12 LnkSta(16), +0x2C LnkCap2, +0x30 LnkCtl2. 속도는 둘 다 3:0, 폭은 둘 다 9:4 (`(x & 0x3F0) >> 4`), ASPM 지원은 LnkCap 11:10, ASPM 제어는 LnkCtl 1:0 (같은 인코딩: bit0 L0s, bit1 L1). DL Active 는 LnkSta bit 13.

</details>

<details><summary>힌트 5 — 기대 속도는 "벡터의 교집합"</summary>

LnkCap2 bits 7:1 이 지원 속도 벡터(bit k = 속도 코드 k). 각자 자기 LnkCap 최대 속도 이하로 자르고(`& ((2 << max) - 1)`), 0 이면(LnkCap2 없는 옛 장치, QEMU nvme) LnkCap 최대값 1 → 0x02, 2 → 0x06 으로 합성. 두 벡터를 AND 하고 최고 비트 = 공통 최고 속도. 실제 값은 다운스트림 포트 LnkSta 에서.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "capability 는 config 공간 안의 연결 리스트 두 개다. legacy 는 0x34 에서, extended 는 0x100 에서 시작한다."
2. "순회는 하드웨어가 준 포인터를 믿지 않는다: 범위·정렬·방문·크기를 검사하고, 루프는 방문 비트맵으로 잡는다. 칸이 48개/960개뿐이라 그게 홉 한계이기도 하다."
3. "PCIe cap 에서 LnkCap 은 '할 수 있는 것', LnkSta 는 '지금 된 것'. 비교는 **링크 양쪽**의 능력과 해야 한다."
4. "x4 가능한데 x2 면 폭 저하 → 레인 하드웨어부터, 속도만 낮으면 등화·신호 품질과 AER Correctable 부터."

### Part A — legacy 리스트: 1바이트 ID + 1바이트 next

capability 하나의 첫 DWORD 는 항상 `[7:0] Cap ID`, `[15:8] Next Ptr`, `[31:16] cap 마다 다른 레지스터` 다. 가상 SSD 의 0x70 (`10 b0 02 00`) 이면 ID 0x10(PCI Express), next 0xB0, 위 16비트 0x0002 는 PCIe Capabilities 레지스터(Flags). 따라가기(가상 SSD):

| 단계 | 포인터 읽은 곳 | ptr | 검사 | cfg[ptr] = ID | cfg[ptr+1] = next |
|---|---|---|---|---|---|
| 0 | Status 0x06 = `10 00` | — | CapList = 1 → 순회 | — | — |
| 1 | 0x34 | 0x40 | ≥0x40, 정렬, 첫 방문, 0x40+8 ≤ 0x100 | 0x01 PM | 0x50 |
| 2 | 0x41 | 0x50 | MSI 플래그 0x0180 → 64bit+Mask → 0x18 바이트 | 0x05 MSI | 0x70 |
| 3 | 0x51 | 0x70 | 버전 2 → 0x34 바이트, 0xA4 ≤ 0x100 | 0x10 PCIe | 0xB0 |
| 4 | 0x71 | 0xB0 | 12 바이트 | 0x11 MSI-X | 0xC0 |
| 5 | 0xB1 | 0xC0 | 길이 바이트 0x10 | 0x09 Vendor | 0x00 → 끝 |

QEMU nvme 실제 캡처는 `34: 40` → `[40] 11 80` (MSI-X) → `[80] 10 60` (PCIe) → `[60] 01 00` (PM) → 끝. **오프셋 순서가 아니다.** "0x40 부터 0x100 까지 훑으며 ID 를 찾는" 코드는 이런 장치에서 엉뚱한 바이트(예: MSI-X 테이블 오프셋)를 ID 로 읽는다. QEMU 루트 포트도 `54 → 48 → 40` 으로 거꾸로 간다.

Type 1(브리지) 헤더도 Cap Ptr 는 0x34 다 — 가상 루트 포트와 QEMU 루트 포트가 그대로 통과한다. 0x14 를 쓰는 것은 Type 2(CardBus) 뿐 (`PCI_CB_CAPABILITY_LIST`). Linux `__pci_bus_find_cap_start()` 도 정확히 이 분기다.

### Part A — extended 리스트: 32비트 헤더

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">capability 헤더 두 종류 (DWORD, little-endian)</text>
  <text x="10" y="42" fill="currentColor">legacy (가상 SSD 0x70: 바이트 10 b0 02 00 → DW 0x0002b010)</text>
  <g stroke="currentColor">
    <rect x="10" y="50" width="360" height="40" class="f-mut"/>
    <rect x="370" y="50" width="180" height="40" class="f-warn"/>
    <rect x="550" y="50" width="180" height="40" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="16" y="66">31:16 cap 고유 (PCIe 는 Flags)</text><text x="16" y="84" font-family="monospace">0x0002 → v2, type 0</text>
    <text x="376" y="66">15:8 Next Ptr</text><text x="376" y="84" font-family="monospace">0xb0</text>
    <text x="556" y="66">7:0 Cap ID</text><text x="556" y="84" font-family="monospace">0x10 = PCI Express</text>
  </g>
  <text x="10" y="122" fill="currentColor">extended (QEMU root port 실제 0x100: 바이트 01 00 82 14 → DW 0x14820001)</text>
  <g stroke="currentColor">
    <rect x="10" y="130" width="270" height="40" class="f-warn"/>
    <rect x="280" y="130" width="90" height="40" class="f-ok"/>
    <rect x="370" y="130" width="360" height="40" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="16" y="146">31:20 Next (12비트)</text><text x="16" y="164" font-family="monospace">0x148</text>
    <text x="286" y="146">19:16 Ver</text><text x="286" y="164" font-family="monospace">2</text>
    <text x="376" y="146">15:0 Cap ID</text><text x="376" y="164" font-family="monospace">0x0001 = AER</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="196">0x14820001 = 0001 0100 1000 | 0010 | 0000 0000 0000 0001</text>
    <text x="10" y="214">              next = 0x148    ver = 2   id = 0x0001</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="242">메모리 순서: [0x100]=01 [0x101]=00 [0x102]=82 [0x103]=14 → 바이트를 거꾸로 붙여 읽는다 (cfg_rd32)</text>
    <text x="10" y="262">Next 의 하위 2비트(= DW bit 21:20)는 예약: Linux PCI_EXT_CAP_NEXT 는 &amp; 0xffc 로 버린다</text>
    <text x="10" y="282">이 레슨의 워커는 버리지 않고 ALIGN 오류로 보고한다 (검증 도구 관점, Q4 · 연습 2)</text>
  </g>
</svg>
```

헤더 0 은 "0x100 에 아무것도 없음" 이다 (스펙상 0x100 에 cap 이 없으면 ID 0, Ver 0, Next 0 — 스펙 확인 권장). QEMU nvme 캡처의 0x100 이 바로 그렇고, 워커는 `CW_OK, n = 0` 을 돌려준다. 0xFFFFFFFF 는 다르다: config 읽기가 실패했을 때 나오는 값이라 Linux `pci_cfg_space_size_ext()` 도 `PCI_POSSIBLE_ERROR(status)` 면 "이 장치는 256B" 로 결론 낸다. 이 레슨은 그것을 `CW_NO_EXT` 로 구분한다. 리스트 중간에서 FFFFFFFF 가 나오면(장치가 사라짐, 링크 다운 후 읽기) `CW_ERR_ALLONES`.

### Part A — PCI Express Capability: 어디에 무엇이 있나

PCIe cap 기준 오프셋 (pci_regs.h `PCI_EXP_*`): +0x02 Flags, +0x04 DevCap, +0x08 DevCtl, +0x0A DevSta, **+0x0C LnkCap, +0x10 LnkCtl, +0x12 LnkSta**, +0x14 SltCap, +0x18 SltCtl/SltSta, +0x1C RootCtl/RootCap, +0x20 RootSta, +0x24 DevCap2, +0x28 DevCtl2, **+0x2C LnkCap2, +0x30 LnkCtl2, +0x32 LnkSta2**. Flags[3:0] = 버전(1 이면 LnkSta 까지 0x14 바이트, 2 이면 엔드포인트 0x34 바이트), Flags[7:4] = 장치/포트 타입(0 Endpoint, 1 Legacy EP, 4 Root Port, 5 Upstream, 6 Downstream, 9 RCiEP …), bit 8 Slot Implemented.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">LnkCap (+0x0C) = 0x00070844, LnkSta (+0x12) = 0x1024 — 가상 SSD</text>
  <g stroke="currentColor">
    <rect x="10" y="30" width="80" height="44" class="f-mut"/>
    <rect x="90" y="30" width="62" height="44" class="f-mut"/>
    <rect x="152" y="30" width="72" height="44" class="f-mut"/>
    <rect x="224" y="30" width="62" height="44" class="f-mut"/>
    <rect x="286" y="30" width="62" height="44" class="f-mut"/>
    <rect x="348" y="30" width="80" height="44" class="f-mut"/>
    <rect x="428" y="30" width="72" height="44" class="f-mut"/>
    <rect x="500" y="30" width="80" height="44" class="f-ok"/>
    <rect x="580" y="30" width="86" height="44" class="f-acc"/>
    <rect x="666" y="30" width="64" height="44" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="13" y="46">31:24 Port#</text><text x="13" y="64" font-family="monospace">0</text>
    <text x="93" y="46">21 LBNC</text><text x="93" y="64" font-family="monospace">0</text>
    <text x="155" y="46">20 DLLLARC</text><text x="155" y="64" font-family="monospace">0</text>
    <text x="227" y="46">19 SDERC</text><text x="227" y="64" font-family="monospace">0</text>
    <text x="289" y="46">18 ClkPM</text><text x="289" y="64" font-family="monospace">1</text>
    <text x="351" y="46">17:15 L1ex</text><text x="351" y="64" font-family="monospace">6 (&lt;64us)</text>
    <text x="431" y="46">14:12 L0s</text><text x="431" y="64" font-family="monospace">0</text>
    <text x="503" y="46">11:10 ASPM</text><text x="503" y="64" font-family="monospace">10b = L1</text>
    <text x="583" y="46">9:4 MaxWidth</text><text x="583" y="64" font-family="monospace">000100=x4</text>
    <text x="669" y="46">3:0 Speed</text><text x="669" y="64" font-family="monospace">4=16GT/s</text>
  </g>
  <text x="10" y="96" fill="currentColor" font-family="monospace">0x00070844 = 0000 0000 0000 0111 0000 1000 0100 0100</text>
  <text x="10" y="114" fill="currentColor">(23:22 생략. 칸 너비는 비율 아님. 루트 포트 예: 0x01380C44 → Port #1, DLLLARC·SDERC·LBNC = 1, ASPM L0s L1)</text>
  <g stroke="currentColor">
    <rect x="10" y="140" width="70" height="44" class="f-mut"/>
    <rect x="80" y="140" width="70" height="44" class="f-mut"/>
    <rect x="150" y="140" width="80" height="44" class="f-warn"/>
    <rect x="230" y="140" width="80" height="44" class="f-mut"/>
    <rect x="310" y="140" width="80" height="44" class="f-warn"/>
    <rect x="390" y="140" width="60" height="44" class="f-mut"/>
    <rect x="450" y="140" width="150" height="44" class="f-acc"/>
    <rect x="600" y="140" width="130" height="44" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="14" y="156">15 LABS</text><text x="14" y="174" font-family="monospace">0</text>
    <text x="84" y="156">14 LBMS</text><text x="84" y="174" font-family="monospace">0</text>
    <text x="154" y="156">13 DLLLA</text><text x="154" y="174" font-family="monospace">0</text>
    <text x="234" y="156">12 SlotClk</text><text x="234" y="174" font-family="monospace">1</text>
    <text x="314" y="156">11 Training</text><text x="314" y="174" font-family="monospace">0</text>
    <text x="394" y="156">10 rsvd</text><text x="394" y="174" font-family="monospace">0</text>
    <text x="454" y="156">9:4 Negotiated Width</text><text x="454" y="174" font-family="monospace">000010 = x2</text>
    <text x="604" y="156">3:0 Current Speed</text><text x="604" y="174" font-family="monospace">4 = 16GT/s</text>
  </g>
  <text x="10" y="206" fill="currentColor" font-family="monospace">0x1024 = 0001 0000 0010 0100</text>
  <g fill="currentColor">
    <text x="10" y="234">속도 코드 (LnkCap 3:0 / LnkSta 3:0 / LnkCtl2 3:0 공통):</text>
    <text x="10" y="252" font-family="monospace">1=2.5  2=5  3=8  4=16  5=32  6=64 GT/s   (0, 7..15 = unknown)</text>
    <text x="10" y="274">LnkCap 3:0 은 엄밀히 "LnkCap2 지원 벡터의 몇 번째 비트가 최대인가" (pci_regs.h 주석: SLS Vector bit 0..5)</text>
    <text x="10" y="294">엔드포인트의 DLLLA = 0 은 정상일 수 있다: DLLLARC(LnkCap bit 20) 가 0 이면 보고 의무가 없다</text>
    <text x="10" y="314">Training(bit 11)도 다운스트림 포트에서 의미 있는 비트 → 트리아지는 루트 포트 쪽 LnkSta 를 "실제" 로 쓴다</text>
  </g>
</svg>
```

디코드 계산 (가상 SSD, cap = 0x70):

| 레지스터 | 위치 | 바이트 → 값 | 필드 |
|---|---|---|---|
| Flags | 0x72 | `02 00` → 0x0002 | ver 2, type 0 Endpoint, Slot- |
| DevCap | 0x74 | `22 80 00 10` → 0x10008022 | MPS 지원 code 2 → 128 << 2 = **512**, ExtTag, RBER, FLR |
| DevCtl | 0x78 | `3f 29` → 0x293F | MPS (7:5) = 1 → **256**, MRRS (14:12) = 2 → **512** |
| LnkCap | 0x7C | `44 08 07 00` → 0x00070844 | 16GT/s, x4, ASPM L1, Port 0 |
| LnkCtl | 0x80 | `42 01` → 0x0142 | ASPM L1 on, RCB 64, CommClk+, CLKREQ# en |
| LnkSta | 0x82 | `24 10` → 0x1024 | 16GT/s, **x2**, SlotClk+, DLActive- |
| LnkCap2 | 0x9C | `1e 00 00 00` | 벡터 0x1E = 2.5/5/8/16 |
| LnkCtl2 | 0xA0 | `04 00` | Target 16GT/s |

실제 `lspci 3.14.0 -vvv -F` 로 같은 바이트를 디코드한 결과 (`lspci_crosscheck.txt`, 실제 출력 발췌) — 내 디코더와 필드가 전부 일치한다:

```text
	Capabilities: [70] Express (v2) Endpoint, IntMsgNum 0
		DevCap:	MaxPayload 512 bytes, PhantFunc 0, Latency L0s <64ns, L1 <1us
		DevCtl:	CorrErr+ NonFatalErr+ FatalErr+ UnsupReq+
			MaxPayload 256 bytes, MaxReadReq 512 bytes
		LnkCap:	Port #0, Speed 16GT/s, Width x4, ASPM L1, Exit Latency L1 <64us
			ClockPM+ Surprise- LLActRep- BwNot- ASPMOptComp-
		LnkCtl:	ASPM L1 Enabled; RCB 64 bytes, LnkDisable- CommClk+
		LnkSta:	Speed 16GT/s, Width x2 (downgraded)
			TrErr- Train- SlotClk+ DLActive- BWMgmt- ABWMgmt-
		LnkCap2: Supported Link Speeds: 2.5-16GT/s, Crosslink- Retimer- 2Retimers- DRS-
	Capabilities: [b0] MSI-X: Enable+ Count=32 Masked-
	Capabilities: [100 v2] Advanced Error Reporting
		CESta:	RxErr+ BadTLP+ BadDLLP- Rollover- Timeout- AdvNonFatalErr- CorrIntErr- HeaderOF-
```

### Part B — 왜 48, 왜 960, 왜 방문 비트맵

legacy cap 은 0x40..0xFC 의 4바이트 정렬 위치에만 있을 수 있다: (256 − 64) / 4 = **48칸**. 서로 다른 cap 이 같은 칸을 쓸 수 없으니 합법적인 리스트는 최대 48개 — Linux `PCI_FIND_CAP_TTL` 도 48 이다 (drivers/pci/pci.h). extended 는 (4096 − 256) / 4 = **960칸**. Linux 는 `(4096 − 256) / 8 = 480` 을 쓰는데, 확장 cap 이 최소 8바이트라고 보는 것으로 읽힌다 (주석에는 이유가 없다 — 추정). 이 레슨의 B7 / B8 은 4바이트 cap 48개, 960개 체인으로 "한계 = 칸 수" 가 정확히 경계에서 맞음을 확인한다.

TTL 만 있으면 루프에서도 결국 멈추지만 **무엇이 잘못됐는지 모른다** — 루프 덤프에서 없는 ID 를 찾으면 Linux 방식은 헤더를 48번(extended 480번) 읽고 "없음" 이라고 한다 (연습 1·2 실측). 드라이버에는 그걸로 충분하다(못 찾으면 기능을 끈다). 검증 도구와 bring-up 로그에는 "0x51 의 next 가 이미 본 0x40 을 가리킴" 이 필요하다. 그래서 비트맵(64비트 / 1024비트, 스택)으로 **첫 재방문**에서 멈추고, 홉 한계는 비트맵이 있으면 도달할 수 없는 방어용으로 남긴다 (무작위 덤프 1000개에서 `HOPS` 0회).

| 실측 (solution 테스트) | status | n | err_ptr | err_from |
|---|---|---|---|---|
| MSI.next = 0x40 | LOOP | 2 | 0x40 | 0x51 |
| PM.next = 0x20 | PTR_LOW | 1 | 0x20 | 0x41 |
| PM.next = 0x51 | ALIGN | 1 | 0x51 | 0x41 |
| PCIe cap @0xD8 | RANGE | 4 | 0xD8 | 0xB1 |
| ext 0x170 → 0x100 | LOOP | 5 | 0x100 | 0x170 |
| ext LTR.next = 0x0F0 | PTR_LOW | 2 | 0x0F0 | 0x148 |
| ext AER @0xFF0 | RANGE | 6 | 0xFF0 | 0x1A0 |

RANGE 가 왜 필요한가: PCIe cap 이 0xD8 에 있으면 LnkCap2(+0x2C = 0x104)는 legacy 영역 밖, 즉 **extended 영역 바이트**를 PCIe 레지스터로 읽게 된다. 256B 버퍼라면 그냥 버퍼 밖 읽기다. "cap 구조체가 끝을 넘으면 디코드하지 않는다" 는 파서 안전 규칙 그 자체다 (COMM-B10 의 LEN 검사와 같은 결).

### Part C — 링크는 두 끝이 있다

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">같은 "LnkSta &lt; LnkCap" 이라도 결론이 다르다</text>
  <text x="10" y="42" fill="currentColor">(1) QEMU 실제 캡처: 루트 포트 00:02.0 — nvme 01:00.0</text>
  <g stroke="currentColor">
    <rect x="10" y="52" width="250" height="70" class="f-mut"/>
    <rect x="480" y="52" width="250" height="70" class="f-mut"/>
    <line x1="260" y1="87" x2="480" y2="87" stroke-width="3"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="70">Root Port  (DLLARC+)</text>
    <text x="18" y="88" font-family="monospace">LnkCap 16GT/s x32</text>
    <text x="18" y="106" font-family="monospace">LnkSta 2.5GT/s x1 DLActive+</text>
    <text x="488" y="70">Endpoint (NVMe)</text>
    <text x="488" y="88" font-family="monospace">LnkCap 2.5GT/s x1, LnkCap2=0</text>
    <text x="488" y="106" font-family="monospace">LnkSta 2.5GT/s x1</text>
    <text x="290" y="80">기대 = min → 2.5GT/s x1</text>
    <text x="290" y="104">실제 2.5GT/s x1 → 정상 (OK)</text>
  </g>
  <text x="10" y="150" fill="currentColor">(2) 가상 덤프: 루트 포트 — Gen4 x4 SSD</text>
  <g stroke="currentColor">
    <rect x="10" y="160" width="250" height="70" class="f-mut"/>
    <rect x="480" y="160" width="250" height="70" class="f-warn"/>
    <line x1="260" y1="195" x2="480" y2="195" stroke-width="3" stroke-dasharray="8,4"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="178">Root Port  (DLLARC+)</text>
    <text x="18" y="196" font-family="monospace">LnkCap 16GT/s x4</text>
    <text x="18" y="214" font-family="monospace">LnkSta 16GT/s x2 DLActive+</text>
    <text x="488" y="178">Endpoint (가상 SSD)</text>
    <text x="488" y="196" font-family="monospace">LnkCap 16GT/s x4</text>
    <text x="488" y="214" font-family="monospace">LnkSta 16GT/s x2</text>
    <text x="290" y="188">기대 16GT/s x4</text>
    <text x="290" y="212">실제 x2 → WIDTH_LOW</text>
  </g>
  <text x="10" y="256" fill="currentColor">lspci 3.14.0 실측: (1) 루트 포트 LnkSta 에 아무 표시 없음, (2) SSD 쪽에만 "Width x2 (downgraded)"</text>
  <text x="10" y="274" fill="currentColor">→ 루트 포트 하나만 보고 "x32 인데 x1!" 이라고 하면 틀린다. 아래쪽 장치가 x1 밖에 못 한다.</text>
</svg>
```

트리아지 규칙 (이 레슨이 정한 모델, `link_triage`):

1. 양쪽에서 PCIe cap 을 찾는다 (Part A 워커 재사용). 없으면 `NO_PCIE`.
2. 기대 속도 = highest_bit(벡터_dsp & 벡터_usp). 벡터는 LnkCap2[7:1] 을 자기 LnkCap 최대 이하로 자르고, 0 이면 LnkCap 1/2 로 합성 — Linux `pcie_get_supported_speeds()` 와 같은 규칙. QEMU nvme 는 LnkCap2 = 0 이라 0x02(2.5GT/s 만)가 된다.
3. 다운스트림 포트의 LnkCtl2.TLS 가 더 낮으면 기대 속도를 그 값으로 낮추고 `TARGET_CAPPED` (ARM 보드에서 DT 의 `max-link-speed` 를 주면 DesignWare 호스트 드라이버가 LnkCtl2.TLS 와 LnkCap.SLS 를 DBI 로 직접 고친다 — Linux `dw_pcie_link_set_max_speed()`).
4. 기대 폭 = min(MLW_dsp, MLW_usp). 실제 값 = 다운스트림 포트 LnkSta (Training·DLLLA 가 의미 있는 쪽).
5. 다운스트림 포트가 DLLLARC 인데 DLLLA = 0, 또는 협상 폭 0, 또는 Link Disable → `LINK_DOWN` (나머지 비교 생략).
6. 실제 < 기대 → `SPEED_LOW` / `WIDTH_LOW`. 두 끝 LnkSta 가 다르면 `MISMATCH` (학습 중에 읽었거나 덤프 시점이 다름). 켜진 ASPM(LnkCtl 1:0)이 양쪽 LnkCap 11:10 의 교집합 밖이면 `ASPM_BAD`.

| 시나리오 (solution 테스트) | flags |
|---|---|
| 가상 RP + 가상 SSD | `WIDTH_LOW` |
| QEMU root port + QEMU nvme (실제) | `OK` |
| RP LnkSta `0x1044` (DLActive-) | `LINK_DOWN`, 실제 0 |
| 양쪽 Gen1 x4 | `SPEED_LOW` |
| RP LnkCtl2.TLS = 8GT/s, 실제 8GT/s x4 | `TARGET_CAPPED` 만 (결함 아님) |
| RP LnkSta 에 Training 비트 | `WIDTH_LOW · TRAINING` |
| EP 는 x4, RP 는 x2 라고 보고 | `WIDTH_LOW · MISMATCH` |
| EP 가 L0s 까지 켬 (EP 는 L1 만 지원) | `ASPM_BAD` 포함 |

Linux 도 같은 생각을 한다: 열거 때 엔드포인트마다 `pcie_report_downtraining()` → `__pcie_print_link_status()` 가 "장치의 LnkCap 능력" 과 "장치에서 루트까지 경로상 LnkSta 최소값" 을 비교해 dmesg 에 `… available PCIe bandwidth, limited by … link at <BDF> (capable of …)` 를 찍는다 (drivers/pci/probe.c, pci.c). 연습 3 이 그 줄을 덤프로 재현한다.

**bring-up 체크리스트** (가상 SSD 처럼 x4 → x2 일 때, 순서대로):

1. 두 끝 LnkSta 를 다시 읽기 — Training, DLActive, 두 끝 일치 여부.
2. 두 끝 LnkCap / LnkCap2 와 다운스트림 포트 LnkCtl2.TLS — 기대값이 정말 x4·16GT/s 인가. ARM SoC 호스트면 DT `num-lanes` / `max-link-speed` 도.
3. AER Correctable Status — 가상 SSD 의 `CESta = 0x41`(RxErr + BadTLP)은 신호 품질 문제의 흔한 신호다. 레인별 정보는 Secondary PCIe cap(+0x08 Lane Error Status)과 16GT/s Physical Layer cap 의 등화 상태 (필드 세부는 스펙 확인 권장).
4. 다운스트림 포트 LnkCtl.Retrain Link(bit 5)로 재학습 → 같은 폭으로 돌아오면 하드웨어(레인 단선, AC 커플링 캡, 레인 순서/극성, 커넥터, 리타이머), 매번 다르면 신호 무결성·리셋/클럭 타이밍 (PCIE-M07, PCIE-M08, HWLAB-M09/M12, SIV-M06).

SSD 펌웨어 쪽에서 보면: LnkCap 의 MLW·SLS·ASPM 지원·Exit Latency 는 소프트웨어가 못 쓰는 HwInit 필드다. 컨트롤러가 strap/OTP/리셋 전 펌웨어 설정에서 채우고, DesignWare 계열은 "RO 레지스터 쓰기 허용" 창(Linux 의 `dw_pcie_dbi_ro_wr_en()`)으로 바꾼다. 그래서 "x2 로 붙는다" 는 버그 리포트에 SSD 쪽이 먼저 확인할 것 중 하나가 **LnkCap 을 x4 로 광고하고 있는가**다 — Don 이 SSD 펌웨어에서 봤을 법한 "PHY 설정이 레인 2개만 켜 둠" 류.

### 복잡도와 자원

| 함수 | 시간 | 메모리 |
|---|---|---|
| `cap_walk_legacy` | O(48) 홉, 홉당 O(1) | 비트맵 8B + 호출자 배열 |
| `cap_walk_ext` | O(960) 홉 | 비트맵 128B + 호출자 배열 |
| `pcie_decode` | O(1), 레지스터 10개 | `pcie_info` 하나 |
| `link_triage` | 워커 2번 + O(1) | 스택 `cap_ent[48]` × 1 (호출마다) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 71/71 passed, exit 0 (약 0.15 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음, 71/71
./solution -d     # 가상 SSD 덤프의 0 아닌 행을 lspci 형식으로 먼저 출력 (1. 문제의 덤프)
```

`solution.c` 실행 출력 중 테스트 외 줄 (실제):

```text
triage(가상 RP + 가상 SSD):
기대: 16GT/s x4 (공통 최고 16GT/s) / 실제: 16GT/s x2
- 폭 저하 → 레인 단선/AC 커플링 캡/레인 반전·커넥터, 리셋 타이밍
triage(QEMU root port + QEMU nvme):
기대: 2.5GT/s x1 (공통 최고 2.5GT/s) / 실제: 2.5GT/s x1
- 정상: 기대한 속도·폭으로 학습됨
cap_report(가상 SSD):
[40] Power Management (01)
[50] MSI (05) Enable- 64bit+ Maskable+
[70] PCI Express (10) v2 Endpoint, MaxPayload 256 (sup 512), MaxReadReq 512
        LnkCap: Port #0, Speed 16GT/s, Width x4, ASPM L1
        LnkCtl: ASPM L1; RCB 64 bytes; Disabled- CommClk+
        LnkSta: Speed 16GT/s, Width x2 (below own cap), Train- SlotClk+ DLActive-
        LnkCap2: SLS vector 0x1e  LnkCtl2: Target Link Speed 16GT/s
[b0] MSI-X (11) Enable+ Count=32 Table: BAR0+0x2000 PBA: BAR0+0x3000
[c0] Vendor Specific (09)
[100 v2] Advanced Error Reporting (0001) CESta=0x00000041
[148 v1] Latency Tolerance Reporting (0018)
[150 v1] L1 PM Substates (001e)
[160 v1] Device Serial Number (0003)
[170 v1] Secondary PCI Express (0019)
[1a0 v1] Physical Layer 16.0 GT/s (0026)
cap_report(QEMU root port 00:02.0):
[54] PCI Express (10) v2 Root Port (Slot+), MaxPayload 128 (sup 128), MaxReadReq 128
        LnkCap: Port #0, Speed 16GT/s, Width x32, ASPM L0s, DLActive reporting+
        LnkCtl: ASPM Disabled; RCB 64 bytes; Disabled- CommClk-
        LnkSta: Speed 2.5GT/s (below own cap), Width x1 (below own cap), Train- SlotClk- DLActive+
        LnkCap2: SLS vector 0x1e  LnkCtl2: Target Link Speed 16GT/s
[48] MSI-X (11) Enable+ Count=1 Table: BAR0+0x0 PBA: BAR0+0x800
[40] Bridge Subsystem VID (0d)
[100 v2] Advanced Error Reporting (0001) CESta=0x00000000
[148 v1] Access Control Services (000d)
71/71 passed
```

`cap_report` 의 "(below own cap)" 은 lspci 의 "(downgraded)" 와 달리 **포트 타입과 상관없이** 자기 LnkCap 과만 비교한 표시다 — 그래서 QEMU 루트 포트에도 붙는다. 판정은 `link_triage` 가 한다. QEMU nvme 의 Command 레지스터가 0 인 것은 캡처 때 nvme.ko 를 로드하지 않아서다 (드라이버가 Mem/BusMaster 를 켠다 — PCIE-B03).

데이터 파일: `qemu_capture_xxxx.txt` (QEMU 캡처 원본, 명령 포함), `lspci_crosscheck.txt` (가상·QEMU 덤프를 실제 `lspci -vvv -F` 로 디코드한 전체 출력).

## 7. 자주 하는 실수

**1. 0x40 부터 0xFF 까지 선형으로 훑으며 ID 를 찾는다.** QEMU nvme(40 → 80 → 60)처럼 순서가 섞인 장치, 그리고 cap 내부 레지스터 값이 우연히 0x10 인 바이트에서 오탐한다. 고친 코드: 반드시 0x34 에서 next 를 따라간다.

**2. Status.CapList 를 안 보고 0x34 를 믿는다.** CapList = 0 인 장치(오래된 PCI 장치)는 0x34 가 예약/임의 값일 수 있다. 고친 코드: `if (!(status & PCI_STATUS_CAP_LIST)) return CW_NONE;` (Linux `__pci_bus_find_cap_start()` 도 같다). Type 2 면 0x14.

**3. 종료 조건이 `next == 0` 하나뿐.** 하드웨어/펌웨어 버그(Don 이 SSD 컨트롤러 bring-up 에서 볼 수 있는 "cap 구조체를 옮기고 next 를 안 고침")나 장치가 사라져 all-1s 가 읽히면 `0xFF → 0xFF → …` 로 영원히 돈다. 0xFF 는 정렬 검사에서, 진짜 루프는 방문 비트맵에서, 나머지는 홉 한계에서 멈춘다.

**4. `*(uint32_t *)(cfg + off)` 로 읽는다.** 정렬이 안 맞으면 UB(ARM 에서 정렬 예외 가능), 그리고 빅엔디언 호스트에서 틀린 값. config 공간은 little-endian 바이트 배열 — `cfg_rd16/32` 로 조립. 실제 하드웨어에서도 Linux 는 `pci_read_config_dword()` 같은 접근자를 쓰고 바이트 순서는 접근자가 맞춘다.

**5. 폭 필드를 그대로 쓴다.** `lnksta & 0x3F0` 은 x2 를 0x20 = 32 로 읽게 만든다. `>> 4` 를 잊는 고전 버그 — x2 링크를 "x32 정상" 으로 보고하게 된다. 속도는 3:0 이라 시프트가 없어 두 필드를 섞어 쓰기 쉽다.

**6. 엔드포인트의 DLActive- 를 "링크 다운" 으로 판정한다.** DLLLARC 가 0 인 포트는 그 비트를 보고할 의무가 없다 (가상 SSD, QEMU nvme 모두 0). 고친 코드: `if (d.dllla_cap && !d.dll_active)` — 그리고 그 판정은 다운스트림 포트 쪽에서.

**7. 한쪽 LnkCap 과만 비교한다.** QEMU 루트 포트(x32 가능, x1 동작)를 결함으로 보고하거나, 반대로 x2 가능 엔드포인트가 x2 인 걸 정상으로 보면서 루트 포트 설정 문제를 놓친다. 기대값 = 양쪽 능력의 교집합 + Target Link Speed.

## 8. 면접 Q&A

<details><summary>Q1. lspci 에서 LnkCap x4, LnkSta x2 로 나옵니다. 무슨 뜻이고 무엇부터 보나요?</summary>

링크 학습(LTSSM Configuration 단계)에서 레인 2개만 합의됐다는 뜻이다. 먼저 **반대쪽** 포트의 LnkCap 폭 — 루트 포트/스위치 포트도 x4 를 지원해야 "저하" 다 (QEMU 처럼 한쪽 능력이 낮으면 정상). 둘 다 x4 면: 다운스트림 포트 LnkSta 의 Training/DLActive, AER Correctable(RxErr/BadTLP), Retrain Link 로 재현성 확인. 같은 x2 로 매번 붙으면 레인 2–3 하드웨어(단선, AC 캡, 커넥터, 레인 순서/극성 설정), 들쭉날쭉하면 신호 무결성·레퍼런스 클럭·PERST# 타이밍. 대역폭은 반이 된다: Gen4 x4 이론 약 7.88 GB/s → x2 약 3.94 GB/s (PCIE-B02). 꼬리질문 "속도만 낮으면?" → 등화(EQ) 실패 후 낮은 속도로 내려간 경우가 흔하다 — LnkSta2 의 Equalization 비트, 16GT/s Physical Layer cap (PCIE-M08).

</details>

<details><summary>Q2. capability 리스트 순회에서 무한 루프를 어떻게 막나요? Linux 는 어떻게 하나요?</summary>

Linux: TTL — legacy 48(`PCI_FIND_CAP_TTL`), extended 480. 매 홉마다 포인터를 4 로 정렬 내림하고, 0x40 미만이면 끝, ID 0xFF 면 끝(all-1s, 장치 없음). 루프여도 TTL 이 다 닳으면 "못 찾음" 으로 끝난다. 48 은 0x40..0xFC 에 놓일 수 있는 칸 수와 같아서, 정상 리스트를 자르지 않는 최소의 상한이다. 검증 도구라면 방문 비트맵을 더해 **첫 재방문 지점**(어느 레지스터의 next 가 어디를 가리켰나)을 보고한다 — 그게 RTL/펌웨어 버그 리포트에 들어갈 정보다. 비트맵은 64/1024 비트라 malloc 도 필요 없다.

</details>

<details><summary>Q3. extended capability 는 왜 0x100 부터이고, 0x100 이 0xFFFFFFFF 면 무슨 뜻인가요?</summary>

PCI 시절 config 공간이 256B 였고 legacy 포인터가 8비트라 0xFF 까지만 가리킬 수 있다. PCIe 가 4KB 로 늘리면서 늘어난 3840B 를 따로 12비트 포인터 리스트로 만들고 시작점을 0x100 으로 고정했다. 그 영역은 ECAM(메모리 맵 config)으로만 닿는다 — 옛 CF8/CFC 는 레지스터 번호가 8비트라 256B 까지만 (PCIE-B08). 0x100 이 0xFFFFFFFF: config 읽기 실패 값이다 — CF8 경로라 닿지 않거나, 확장 config 를 지원 안 하는 호스트/장치이거나, 장치가 사라졌다. Linux `pci_cfg_space_size_ext()` 는 이 경우(그리고 0x100 이 0x00 의 별칭인 경우) 장치 config 크기를 256 으로 정한다. 0 이면 "확장 cap 없음" 으로 정상 (QEMU nvme).

</details>

<details><summary>Q4. 포인터 하위 2비트가 1 이면? Linux 는 마스크하는데 검증 도구도 그래야 하나요?</summary>

스펙은 하위 2비트를 예약(하드웨어는 00b 로 구현)으로 두고, 소프트웨어는 나중 용도를 위해 마스크해서 쓰라고 한다 (스펙 확인 권장). 그래서 드라이버(Linux)는 `ALIGN_DOWN(pos, 4)` / `& 0xffc` 로 버리고 계속 간다 — 동작이 우선이다. 검증·compliance 도구는 반대로 "예약 비트가 1" 을 **하드웨어 위반**으로 기록해야 한다. 이 레슨의 워커는 `CW_ERR_ALIGN` 으로 멈추고, 연습 1·2 에서 같은 덤프를 Linux 식으로 찾으면 0x51 → 0x50 으로 MSI 를 찾아 버리는 것을 보인다. 면접 답: "드라이버는 관대하게, 검증은 엄격하게, 둘 다 로그는 남긴다."

</details>

<details><summary>Q5. LnkCap 의 Max Link Speed 4 는 16 GT/s 인데, LnkCap2 는 왜 또 있나요?</summary>

PCIe 3.0 부터 LnkCap 3:0 은 "LnkCap2 의 Supported Link Speeds Vector 에서 몇 번째 비트가 최대인가" 를 가리킨다 (pci_regs.h 주석 "LNKCAP2 SLS Vector bit N"). 벡터가 있어야 "2.5 와 16 은 되는데 8 은 안 됨" 같은 불연속 지원도 표현된다. 그래서 공통 속도는 벡터 AND 로 구하고, 벡터가 0(LnkCap2 이전 장치 — QEMU nvme 의 LnkCap2 = 0)이면 LnkCap 값으로 합성한다 (Linux `pcie_get_supported_speeds()`). LnkSta 3:0 은 같은 1..6 코드로 현재 속도를 준다. 코드 → GT/s: 1=2.5, 2=5, 3=8, 4=16, 5=32, 6=64.

</details>

<details><summary>Q6. MPS / MRRS 는 어디서 보고, 왜 처리량에 중요한가요?</summary>

PCIe cap +0x04 DevCap[2:0] = 지원 최대 payload(128 << n), +0x08 DevCtl[7:5] = 현재 MPS, DevCtl[14:12] = MRRS(최대 읽기 요청). 가상 SSD: 지원 512, 설정 256, MRRS 512. MPS 는 경로 전체가 같은 값(가장 작은 쪽)을 써야 하고 OS 가 정한다. TLP 당 헤더·프레이밍·LCRC 오버헤드가 고정이라 payload 가 클수록 효율이 오른다 (PCIE-B02 의 표: 128B 와 256B 차이가 수 %). NVMe 읽기는 SSD 가 호스트 메모리로 쓰는 MWr(MPS 제한), 쓰기는 SSD 가 MRd 를 내고 CplD 로 받는 것(MRRS·RCB 제한)이라 둘 다 영향이 있다 (PCIE-B10).

</details>

<details><summary>Q7. 드라이버 probe 에서는 이 리스트를 어떻게 쓰나요?</summary>

Linux: `pci_find_capability(dev, PCI_CAP_ID_MSIX)` 같은 함수가 이 순회를 하고 위치를 돌려준다. PCIe cap 위치는 열거 때 한 번 찾아 `dev->pcie_cap` 에 캐시하고, 이후 `pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &v)` 처럼 cap 기준 오프셋으로 읽는다. extended 는 `pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR)` (AER 서비스 드라이버), 같은 ID 가 여러 번이면(VSEC) `pci_find_next_ext_capability(dev, pos, id)` — 연습 2. MSI-X 는 cap 안의 Table BIR/offset 으로 BAR 안의 테이블을 찾는다 (연습 4, PCIE-M10). 사용자 공간에서는 `setpci -s 01:00.0 CAP_EXP+0x12.w` 처럼 이름으로 cap 을 찾아 읽을 수 있다 (PCIE-B06).

</details>

<details><summary>Q8. SSD 컨트롤러(엔드포인트) 펌웨어 입장에서 이 레지스터들은 누가 채우나요?</summary>

cap 리스트의 배치(오프셋, next)와 LnkCap 같은 HwInit 필드는 컨트롤러 IP 의 설정/strap/OTP 에서 오고, 필요하면 펌웨어가 링크를 켜기 전에 바꾼다(DesignWare 계열은 DBI 의 RO 쓰기 허용 비트 — Linux 호스트 쪽 `dw_pcie_dbi_ro_wr_en()` 이 같은 메커니즘을 쓴다). 호스트가 쓰는 쪽(DevCtl MPS/MRRS, LnkCtl ASPM, MSI-X Enable)은 펌웨어가 **읽어서 따라야** 한다 — 예를 들어 MPS 를 무시하고 512B MWr 을 보내면 호스트 쪽에서 Malformed TLP(AER Uncorrectable). 그래서 엔드포인트 펌웨어 검증 항목: "cap 리스트가 Linux/lspci 로 끝까지 읽히는가, 예약 비트 0, 루프 없음, LnkCap 이 보드 설계(레인 수)와 맞는가" — 이 레슨의 워커가 그 체크기다.

</details>

## 9. 추가 연습

### 연습 1 — Linux 식 legacy 찾기 vs 엄격한 워커

Linux `PCI_FIND_NEXT_CAP` 규칙(TTL 48, `ALIGN_DOWN(pos, 4)`, ID 0xFF 에서 중단, 0x40 미만이면 중단)으로 `linux_find_cap(cfg, id, &reads)` 를 짜라. (a) 가상 SSD 에서 MSI-X 를 몇 번 읽고 찾나, 없는 ID(SATA 0x12)는? (b) 루프 덤프(MSI.next = 0x40)에서 없는 ID 를 찾을 때 읽기 횟수 (c) PM.next = 0x51 일 때 Linux 와 엄격 워커의 결과.

<details><summary>답</summary>

(a) MSI-X 0xB0 을 4번 읽고, SATA 는 5번(끝까지) 읽고 0. (b) **48번** 읽고 0 — 엄격 워커는 2개를 모은 뒤 3번째에서 `LOOP`. 드라이버에는 "없음" 이 맞는 답이지만, 원인은 사라진다. (c) Linux 는 0x51 → 0x50 으로 내려 MSI 를 찾는다(0x50 반환), 엄격 워커는 `ALIGN`. QEMU nvme/root port 에서도 PCIe cap 이 0x80 / 0x54 로 찾아진다. 실측 출력: `practice 1: loop dump, absent ID -> Linux reads=48, strict walker stops after 2 caps (LOOP)`.

</details>

### 연습 2 — Linux 식 extended 찾기와 "다음 인스턴스"

`linux_find_ext_cap(cfg, start, id, &reads)` (PCI_FIND_NEXT_EXT_CAP: start 가 0 이면 0x100, TTL 480, 헤더 0 이면 중단, `pos != start` 일 때만 일치, next 는 `& 0xffc`). VSEC(0x000B)가 0x100 과 0x180 에 두 개, 그 사이 0x140 에 AER 인 덤프에서 VSEC 를 차례로 전부 찾아라. 루프 덤프와 비정렬 next 도 확인.

<details><summary>답</summary>

`v1 = find(0) = 0x100`, `v2 = find(0x100) = 0x180`, `find(0x180) = 0` — start 자리의 헤더는 다시 읽지만 `pos != start` 로 건너뛴다. 이것이 `pci_find_next_ext_capability()` 로 여러 DVSEC/VSEC 를 도는 방법이다 (CXL 장치는 DVSEC 가 여러 개 — PCIE-A10). 가상 SSD 에서 PL 16GT/s 는 6번째 읽기에서 찾는다. 0x170 → 0x100 루프에서 없는 ID(SR-IOV)는 **480번** 읽고 0. LTR.next = 0x152 이면 `& 0xffc` 로 0x150 의 L1SS 를 찾는다 (엄격 워커는 `ALIGN`). QEMU nvme 는 0x100 = 0 이라 1번 읽고 끝, QEMU root port 의 ACS 는 0x148.

</details>

### 연습 3 — dmesg 의 "limited by" 줄 재현

Linux `__pcie_print_link_status()` 를 덤프로 흉내 내라: capable = 장치 자신의 (지원 벡터 최고 속도, LnkCap 폭), available = 장치부터 루트 포트까지 각 장치 LnkSta 의 `폭 × PCIE_SPEED2MBS_ENC(속도)` 최소값 (같으면 위쪽이 이김: `next_bw <= bw`). 레인당 Mb/s 는 정수 나눗셈 그대로: 2.5→2000, 5→4000, 8→7876, 16→15753, 32→31507, 64→64000. (a) 가상 SSD + 가상 RP (b) Gen4 x4 SSD → 스위치 DSP(Gen4 x4) → 스위치 USP(Gen3 x4) → 루트 포트(Gen3 x4) (c) QEMU 쌍, verbose 여부.

<details><summary>답</summary>

실측 출력:

```text
practice 3a: 31.506 Gb/s available PCIe bandwidth, limited by 16.0 GT/s PCIe x2 link at 0000:00:1c.0 (capable of 63.012 Gb/s with 16.0 GT/s PCIe x4 link)
practice 3b: 31.504 Gb/s available PCIe bandwidth, limited by 8.0 GT/s PCIe x4 link at 0000:00:01.0 (capable of 63.012 Gb/s with 16.0 GT/s PCIe x4 link)
practice 3c: 2.000 Gb/s available PCIe bandwidth (2.5 GT/s PCIe x1 link)
```

(a) 4 × 15753 = 63012 → "63.012", 2 × 15753 = 31506. SSD 와 RP 의 LnkSta 가 같으므로 `<=` 때문에 **루트 포트** BDF 가 찍힌다 — 한 링크의 두 끝 중 위쪽. (b) SSD 자신의 링크는 정상인데 스위치 위 링크가 Gen3 x4(4 × 7876 = 31504)라 그 링크의 위쪽 끝(루트 포트 00:01.0)이 찍힌다. "SSD 가 느리다" 는 리포트에서 범인이 SSD 링크가 아닐 수 있다는 예. (c) 제한이 없으면 비-verbose(열거 때 경로)는 아무것도 안 찍고, verbose 는 capable 값만. 이 Gb/s 는 128b/130b 까지 뺀 값이고 TLP 오버헤드는 포함 안 한다 (PCIE-B02).

</details>

### 연습 4 — MSI / MSI-X 디코드

MSI: Message Control 3:1 Multiple Message Capable(2^n 벡터), 6:4 Multiple Message Enable, bit 7 64-bit, bit 8 per-vector mask, bit 0 Enable. MSI-X: Message Control 10:0 = 테이블 크기 − 1, bit 14 Function Mask, bit 15 Enable, +4 Table (BIR 2:0, offset 31:3), +8 PBA. 가상 SSD, QEMU nvme, QEMU root port 를 디코드하고 "MSI 와 MSI-X 동시 Enable" 을 오류로 잡아라.

<details><summary>답</summary>

가상 SSD: MSI 1벡터 가능, 64bit+, Maskable+, 꺼짐 / MSI-X 켜짐, 32벡터, Table BAR0+0x2000, PBA BAR0+0x3000. QEMU nvme: MSI 없음, MSI-X **65벡터**(0x0040 + 1, 실제 lspci 도 `Count=65`), 꺼짐(드라이버 미바인딩). QEMU root port: MSI-X 켜짐 1벡터, PBA BAR0+0x800 (pcieport 서비스 드라이버가 켬). MSI 플래그 0x018B → MMC 5 → 32벡터. 둘 다 켜면 동작이 정의되지 않으므로(스펙은 동시에 켜지 말라고 한다) `conflict`. NVMe 드라이버가 큐마다 MSI-X 벡터를 하나씩 쓰는 이유와 MSI-X 테이블을 BAR 에 매핑하는 과정은 PCIE-M10 / PCIE-M13.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 24/24 passed
```

```text
practice 1: loop dump, absent ID -> Linux reads=48, strict walker stops after 2 caps (LOOP)
practice 2: ext loop, absent ID -> Linux reads=480
practice 3a: 31.506 Gb/s available PCIe bandwidth, limited by 16.0 GT/s PCIe x2 link at 0000:00:1c.0 (capable of 63.012 Gb/s with 16.0 GT/s PCIe x4 link)
practice 3b: 31.504 Gb/s available PCIe bandwidth, limited by 8.0 GT/s PCIe x4 link at 0000:00:01.0 (capable of 63.012 Gb/s with 16.0 GT/s PCIe x4 link)
practice 3c: 2.000 Gb/s available PCIe bandwidth (2.5 GT/s PCIe x1 link)
24/24 passed
```

## 10. 핵심 정리

- legacy 리스트: Status bit 4 → 0x34(CardBus 0x14) → `[ID 7:0][Next 15:8]`, 0x40..0xFC, 최대 48개. extended: 0x100 고정 → `[ID 15:0][Ver 19:16][Next 31:20]`, ECAM 으로만, 최대 960칸.
- 순서는 오프셋 순이 아니다 (QEMU nvme 40 → 80 → 60). 선형 스캔 금지, 포인터를 따라간다.
- 포인터 검사: 0 = 끝, < 0x40/0x100, 하위 2비트, 방문 비트맵(64/1024비트), ID 0xFF·헤더 FFFFFFFF, 구조체 크기가 끝을 넘는지. Linux 는 TTL 48/480 + 마스크로 관대하게, 검증 도구는 위반 지점을 보고.
- 0x100 = 0 → 확장 cap 없음(정상), 0xFFFFFFFF → 확장 공간에 못 닿음 (256B 로 취급).
- PCIe cap: +0x0C LnkCap(3:0 최대 속도, 9:4 최대 폭, 11:10 ASPM, 20 DLLLARC, 31:24 Port#), +0x10 LnkCtl, +0x12 LnkSta(3:0 속도, 9:4 폭, 11 Training, 13 DLLLA), +0x2C LnkCap2 벡터, +0x30 LnkCtl2 TLS. 속도 코드 1..6 = 2.5/5/8/16/32/64 GT/s. 폭은 `>> 4`.
- 링크 판정은 두 끝으로: 기대 = 지원 벡터 교집합 최고(+TLS 상한), min(MLW). 실제 = 다운스트림 포트 LnkSta. 엔드포인트 DLActive- 는 DLLLARC 가 없으면 정상.
- x4 → x2 면 레인 하드웨어, 속도만 낮으면 EQ·신호 품질, AER CESta(RxErr/BadTLP)가 증거. Linux dmesg "limited by … at <BDF>" 는 경로상 최소 링크의 위쪽 끝을 가리킨다.

## 참고자료

- Linux `include/uapi/linux/pci_regs.h` (VM 의 `/usr/include/linux/pci_regs.h`, 커널 7.0 헤더) — 모든 오프셋·비트·cap ID·`*_SIZEOF` 상수의 출처.
- Linux 6.18.55 소스 (`/var/lab/src/linux-6.18.55`): `drivers/pci/pci.h` `PCI_FIND_CAP_TTL`, `PCI_FIND_NEXT_CAP`, `PCI_FIND_NEXT_EXT_CAP`, `PCIE_SPEED2MBS_ENC`; `drivers/pci/pci.c` `__pci_bus_find_cap_start`, `pci_find_capability`, `pcie_get_supported_speeds`, `pcie_bandwidth_available`, `__pcie_print_link_status`; `drivers/pci/probe.c` `pci_cfg_space_size_ext`, `pcie_report_downtraining`; `drivers/pci/controller/dwc/pcie-designware.c` `dw_pcie_link_set_max_speed`.
- pciutils `lspci` 3.14.0 — `-F <file>` 로 덤프 파일을 디코드 (이 레슨의 `lspci_crosscheck.txt`). <https://mj.ucw.cz/sw/pciutils/>
- PCI-SIG, *PCI Express Base Specification* — 7.5.3 PCI Express Capability Structure (LnkCap/LnkCtl/LnkSta/LnkCap2), 7.6 Extended Capabilities, AER/LTR/L1SS/Secondary PCIe 장. 예약 비트 마스크 규칙, Null capability, DLLLARC 의무 범위는 원문 확인 권장. <https://pcisig.com/specifications>
- PCI-SIG, *PCI Code and ID Assignment Specification* — capability ID / extended capability ID 표.
- Mike Jackson, Ravi Budruk (MindShare), *PCI Express Technology 3.0* — Configuration 장 (capability 구조), Link Initialization & Training 장.
- 관련 레슨: PCIE-B02 (대역폭 계산), PCIE-B03 (Type 0 헤더, Command/Status), PCIE-B06 (lspci/setpci 실전, `CAP_EXP+0x12.w`), PCIE-B07 (Type 1 헤더), PCIE-B08 (ECAM vs CF8, 256B 한계), PCIE-B10 (MPS/MRRS/RCB 와 TLP 분할), PCIE-M07 (LTSSM), PCIE-M08 (링크 저하 진단), PCIE-M09 (ASPM·L1SS), PCIE-M10 (MSI/MSI-X), PCIE-M12 (AER), PCIE-M13 (Linux PCI 드라이버), PCIE-A10 (CXL DVSEC), COMM-B10 (길이 검사 파서 안전 규칙), BM-B04 (W1C 레지스터 — AER Status 지우기), HWLAB-M09/M12 (eye, 등화), SIV-M06 (SerDes PRBS/BER).
