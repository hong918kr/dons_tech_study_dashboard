---
id: PCIE-B06
time: 60분
updated: 2026-11-15
---
# PCIE-B06 lspci·setpci 실전 해석: -tv, -vvv, -xxxx

## 학습 목표

- `lspci -tv / -vvv / -xxxx`, `setpci`, `/sys/bus/pci/devices/<BDF>/` 의 각 줄이 **config space 몇 번 오프셋 / 어느 sysfs 파일**에서 왔는지 말할 수 있다.
- sysfs 만으로 BDF, VID:DID, class, BAR(주소·크기·64-bit·prefetch), capability 리스트, "Kernel driver in use" 를 뽑는 C 파서를 정적 버퍼로 짜고, 같은 장치의 `lspci`/`setpci` 출력과 **바이트 단위로 대조**한다.
- 비root 로 보면 config 가 64바이트만 보이고(`Capabilities: <access denied>`), `/proc/iomem` 주소가 0 으로 가려지는 이유를 실측으로 설명한다.
- `LnkCap` vs `LnkSta` 를 읽고 "(downgraded)" 가 어느 포트에만 찍히는지, 링크 기대값이 **양쪽 cap 의 min** 이라는 것으로 Gen4 x4 → x2 / Gen1 / DLActive- / Mem- / AER CmpltTO+ 를 진단한다.

## 1. 문제

보드 브링업과 실리콘 검증에서 **첫 번째로 치는 명령이 `lspci`** 다. Don 의 Rivian HM 스크린도 "lspci 로 뭘 보나 → LTSSM → L0 가 뭔가" 로 시작했다. 면접관은 출력의 각 줄이 어디서 오는지(config 오프셋, sysfs 파일, 커널 상태)를 아는지, 그리고 `LnkSta` 한 줄로 "이 SSD 는 반쪽 대역폭으로 돌고 있다"를 잡아내는지를 본다.

이 레슨의 재료는 세 가지다 (전부 실측 또는 실측 도구로 만든 것, 출처를 표시한다).

| 재료 | 무엇 | 파일 |
|---|---|---|
| **VM 실측** | Lima VM `elinux` (Apple Virtualization, aarch64, Linux 7.0). 호스트 브리지 `106b:1a05` 아래 버스 00 에 10개 함수, virtio 장치들은 **conventional PCI 형 endpoint** — PCIe capability·브리지 없음 | `capture_vm.txt` (`capture.sh vm`) |
| **QEMU 실측** | VM 안에서 QEMU 10.2 `virt` + `pcie-root-port` + `nvme` 를 띄우고 게스트(Linux 6.18.55)의 `/sys/bus/pci/devices/*/config` 를 hexdump → VM 의 `lspci -F <덤프>` 로 디코드. 진짜 Type 1 브리지 + PCIe cap + LnkCap/LnkSta (**QEMU 가 흉내 낸 값**) | `capture_qemu.txt` (`capture.sh qemu`) |
| **가상 덤프** | QEMU 덤프의 LnkCap/LnkSta/AER 바이트만 고친 "실HW 형식 예시" 5종 (Gen4 x4→x2, Gen1, DLActive-, overdriven, CmpltTO+) — 문자열은 **실제 `lspci -F` 가 찍은 것** | `capture_variants.txt` (`make_variants.py`) |

lspci 가 보여 주는 정보가 sysfs 의 어느 파일에서 오는지부터 그림으로 잡자 (00:06.0 virtio-blk 실측값).

```svg
<svg viewBox="0 0 740 345" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs>
    <marker id="b06a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto">
      <path d="M0,0 L8,4 L0,8 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">/sys/bus/pci/devices/0000:00:06.0/ → sudo lspci -vvv -s 00:06.0</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="300" height="40" rx="5" class="f-mut"/>
    <rect x="10" y="92" width="300" height="40" rx="5" class="f-warn"/>
    <rect x="10" y="144" width="300" height="40" rx="5" class="f-ok"/>
    <rect x="10" y="196" width="300" height="40" rx="5" class="f-acc"/>
    <rect x="10" y="248" width="300" height="40" rx="5" class="f-mut"/>
    <rect x="420" y="40" width="310" height="40" rx="5"/>
    <rect x="420" y="92" width="310" height="40" rx="5"/>
    <rect x="420" y="144" width="310" height="40" rx="5"/>
    <rect x="420" y="196" width="310" height="40" rx="5"/>
    <rect x="420" y="248" width="310" height="40" rx="5"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="57">config 0x00–0x3F (헤더 64B)</text>
    <text x="20" y="73" font-family="monospace">비root 도 읽음: 16 04 = Command 0416</text>
    <text x="20" y="109">config 0x40–0xFF (capability 영역)</text>
    <text x="20" y="125" font-family="monospace">root 만: 09 50 … 11 00 01 80</text>
    <text x="20" y="161">resource (13줄: BAR0–5, ROM, VF BAR)</text>
    <text x="20" y="177" font-family="monospace">0x380020000 0x38002ffff 0x140204</text>
    <text x="20" y="213">driver → …/bus/pci/drivers/virtio-pci</text>
    <text x="20" y="229" font-family="monospace">readlink 의 basename</text>
    <text x="20" y="265">vendor / device / class / irq (텍스트)</text>
    <text x="20" y="281" font-family="monospace">0x1af4 0x1042 0x018000 16</text>
    <text x="430" y="57" font-family="monospace">Control: I/O- Mem+ BusMaster+ …</text>
    <text x="430" y="73" font-family="monospace">Status: Cap+ …  (0x06 = 0010)</text>
    <text x="430" y="109" font-family="monospace">Capabilities: [40] … [84] MSI-X</text>
    <text x="430" y="125">비root 면 "&lt;access denied&gt;"</text>
    <text x="430" y="161" font-family="monospace">Region 0: Memory at 380020000</text>
    <text x="430" y="177" font-family="monospace">(64-bit, non-prefetchable) [size=64K]</text>
    <text x="430" y="213" font-family="monospace">Kernel driver in use: virtio-pci</text>
    <text x="430" y="229">드라이버 없으면 이 줄이 없다</text>
    <text x="430" y="265" font-family="monospace">Interrupt: pin A routed to IRQ 16</text>
    <text x="430" y="281">IRQ 는 config 0x3C(=ff)가 아니라 sysfs irq</text>
  </g>
  <g stroke="currentColor" marker-end="url(#b06a)">
    <line x1="310" y1="60" x2="418" y2="60"/>
    <line x1="310" y1="112" x2="418" y2="112"/>
    <line x1="310" y1="164" x2="418" y2="164"/>
    <line x1="310" y1="216" x2="418" y2="216"/>
    <line x1="310" y1="268" x2="418" y2="268"/>
  </g>
  <text x="10" y="314" fill="currentColor" font-size="12">Linux 의 lspci/setpci 기본 접근 방법은 linux-sysfs — 같은 파일을 읽는다.</text>
  <text x="10" y="332" fill="currentColor" font-size="12">그래서 "우리 파서 == lspci" 는 파싱의 정확성을 검증한다 (하드웨어 검증이 아님).</text>
</svg>
```

### Part A — sysfs 로 직접 읽어 디코드

1. `parse_lspci_x(text, len, cfg, cap)`: `lspci -xxx/-xxxx` 텍스트 → 바이트 배열. 장치 머리줄 허용, 오프셋 순서 검사, 줄당 정확히 16바이트, 합계 64/256/4096.
2. `parse_resource(text, len, res, max)`: sysfs `resource` 의 `start end flags` 줄들. `fmt_size(sz)`: lspci 식 크기 문자열(`64K`, `64`, `2M`).
3. `walk_caps(cfg, len, cl)`: Status.CapList → `0x34` → `[off] id → next` 순회. 하위 2비트 마스크, `< 0x40` / 버퍼 넘침 / 루프 검사.
4. `read_dev(sysroot, bdf, d)`: `config`(최대 4KB), `vendor/device/class`, `resource`, `driver` 링크 → `pci_dev` 하나. 드라이버 없음은 오류가 아니다.
5. 테스트: VM 실측 00:06.0 덤프(VID/DID/class/Command/Status/BAR raw/IntPin), resource 13줄 → BAR0 64K 64-bit, BAR2 64B 32-bit, **resource start == config BAR 주소**, `flags` 하위 4비트 == BAR 타입 비트, cap 5개, QEMU NVMe/루트 포트 덤프의 cap 순서(정렬 안 됨), 가짜 sysfs 트리에서 `read_dev`.

### Part B — 깨진 입력·권한·가짜 sysfs

| # | 주입 | 기대 결과 |
|---|---|---|
| B1 | 48바이트(3줄), 15바이트 줄, `zz`, 오프셋 `30` 이 `20` 자리에, 256B 를 64B 버퍼에, 데이터 바로 뒤 다른 장치 머리줄, 빈 텍스트, `-vvv` 텍스트, 길이로 자른(NUL 없는) 텍스트 | `E_TRUNC / E_FORMAT / E_ORDER / E_CAP` — 버퍼 밖을 읽지 않는다 |
| B2 | Status.CapList=0, 루프 40→50→40, next=0x20, 64B 버퍼, 포인터 하위 2비트 | 0개 / `E_LOOP`(2개 남김) / `E_BADPTR`(3개) / `E_TRUNC` / 마스크 |
| B3 | resource: 토큰 2개·4개, 쓰레기, end < start, 5줄, max 초과 | `E_FORMAT / E_TRUNC / E_CAP` |
| B4 | 가짜 sysfs: 64B config + driver 링크 없음, config 파일 없음, vendor 파일이 config 와 다름(stale), 전부 0xFF, resource 깨짐 | driver `""` + caps `E_TRUNC`, `E_IO`, 불일치 검출, VID ffff, `E_FORMAT` |

### Part C — lspci/setpci 와 대조, 그리고 실HW 형식 해석

1. `parse_lspci_vvv(text, len, view)`: `Kernel driver in use:`, `Capabilities: [xx]` / `<access denied>`, `Region N: Memory|I/O ports at ADDR (64-bit, …) [disabled] [size=…]`.
2. VM 의 **모든** PCI 함수에 대해: sysfs 텍스트 == config, `lspci -xxx` 바이트 == sysfs config, `lspci -vvv` (driver, region 주소·크기·64-bit·prefetch, cap 오프셋) == 우리 결과, `lspci -n` 줄 == 우리 문자열.
3. `setpci -s 00:06.0 COMMAND / VENDOR_ID.w / BASE_ADDRESS_0 / 0x34.b / CAP_MSIX+2.w` == 우리 `cfg_rd`, `CAP_EXP+0x12.w` 는 **PCIe cap 이 없어서 실패**해야 한다.
4. nobody(65534) 로 config 를 읽으면 64바이트, cap 순회는 `E_TRUNC`, `lspci -vvv` 는 `<access denied>`.
5. 해석 문제 (가상 덤프를 실제 `lspci -F` 로 찍은 "실HW 형식 예시"): ① Gen4 x4 SSD 가 `Width x2 (downgraded)` ② `Speed 2.5GT/s (downgraded)` ③ 루트 포트 `DLActive-` + `PresDet+` ④ NVMe `Control: Mem- BusMaster-` + `[disabled]` ⑤ 루트 포트 AER `UESta: … CmpltTO+`. 각각 무슨 뜻이고 **무엇부터 확인**하나 (5. 풀이 Part C).

## 2. Problem (English)

> **Decode a PCI function from sysfs and cross-check it against lspci.** On a Linux box, write a C tool (no dynamic
> allocation) that, for every directory under `/sys/bus/pci/devices/`, reads `config`, `resource`, `vendor`,
> `device`, `class` and the `driver` symlink, and prints the BDF, `VID:DID`, class code, each implemented BAR
> (address, size, 32/64-bit, prefetchable), the legacy capability list and the bound driver.
>
> ```c
> int parse_lspci_x(const char *text, size_t len, uint8_t *cfg, size_t cap);   // -> 64/256/4096 or error
> int parse_resource(const char *text, size_t len, pci_res *res, int max);     // -> lines or error
> int walk_caps(const uint8_t *cfg, size_t len, cap_list *out);                // loop/bad-pointer safe
> int read_dev(const char *sysroot, const char *bdf, pci_dev *d);
> int parse_lspci_vvv(const char *text, size_t len, lspci_view *v);
> ```
>
> Then prove your decoder against the real tools: `lspci -xxx` bytes, `lspci -vvv` regions/capabilities/driver,
> `lspci -n`, and `setpci -s <bdf> COMMAND` / `CAP_MSIX+2.w`. Example for `00:06.0` on the test VM:
> `0000:00:06.0 1af4:1042 class 018000`, `Region 0: Memory at 380020000 (64-bit, non-prefetchable) [size=64K]`,
> caps `[40] 09 [50] 09 [60] 09 [74] 09 [84] 11`, `Kernel driver in use: virtio-pci`.
>
> Follow-ups: (1) Run it as a normal user — why do capabilities disappear? (2) `setpci -s 00:06.0 CAP_EXP+0x12.w`
> fails — why? (3) A Gen4 x4 NVMe shows `LnkSta: Speed 16GT/s, Width x2 (downgraded)` — what is the impact and
> what do you check first? (4) The root port above it shows `Width x2` with no "(downgraded)" — is it healthy?

## 3. 시작 코드

<!-- code: starter.c -->

검증은 VM 안에서 `check.sh` 로 한다. 레슨 폴더를 `/tmp/PCIE-B06_work/<variant>.$$` 로 복사(맥 홈은 VM 에서 읽기 전용) → `gcc -std=c11 -Wall -Wextra -Werror -O2` → `sudo` 로 실행 → PASS/FAIL, `N/M passed`, 마지막 줄 `RESULT: …` → 정리. 프로그램이 직접 `lspci`/`setpci` 를 불러 대조하므로 **읽기만** 한다 (setpci 쓰기, insmod 없음).

<!-- code: check.sh -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/PCIE/PCIE-B06/check.sh starter
```

TODO 는 여섯 개: A1 `parse_lspci_x`, A2 `parse_resource`, A3 `fmt_size`, A4 `walk_caps`, A5 `read_dev`, C1 `parse_lspci_vvv`. 스텁은 `E_FORMAT` / `E_IO` / 빈 결과를 돌려주므로 멈추거나 죽지 않는다 (0.3 초). 실제 출력 (발췌):

```text
FAIL A1 lspci -xxxx 256B parsed: r == 256 (line 564)
FAIL A1 VID:DID 1af4:1042, class 018000, rev 01, hdr 00: cfg_rd16(cfg, PCI_VENDOR_ID) == 0x1af4 && … (line 565)
…
FAIL C5 as nobody: config read = 64B (header only), same bytes, cap walk → E_TRUNC: … (line 807)
PASS C5 lspci -vvv as nobody → 'Capabilities: <access denied>' (if device has caps)
14/57 passed
RESULT: FAIL (starter, exit 1, 43 failed)
```

(남는 14개 PASS 는 스텁이 우연히 맞히는 오류 경로 — 예: "깨진 줄 → E_FORMAT" 는 스텁의 기본값과 같다.)

## 4. 힌트

<details><summary>힌트 1 — 데이터 줄과 머리줄을 가르는 한 글자</summary>

`00:06.0 Mass storage …` 와 `00: f4 1a …` 는 둘 다 `00:` 로 시작한다. 차이는 콜론 **다음 글자**: 데이터 줄은 공백, 머리줄은 숫자. 그래서 "16진 숫자 2~3개 + `:` + 공백" 이면 데이터 줄. 오프셋은 `got` (지금까지 받은 바이트 수)와 같아야 하고, 한 줄 = `j + 1 + 16×3` 글자. 끝에서 합계를 64/256/4096 중 하나로 확인하면 "중간이 잘린 덤프" 가 걸린다.

</details>

<details><summary>힌트 2 — resource 의 빈 줄과 flags</summary>

`resource` 줄 i 는 BAR i 다. **BAR0 이 64-bit 면 BAR1 줄은 0 0 0** (상위 32비트는 BAR0 의 일부). 크기 = `end − start + 1` (둘 다 0 이면 미구현). flags 는 커널의 `IORESOURCE_*` (`include/linux/ioport.h`): `0x200` MEM, `0x100` IO, `0x2000` PREFETCH, `0x100000` MEM_64, `0x40000` SIZEALIGN. 하위 4비트에는 커널이 BAR 의 타입 비트를 그대로 복사해 둔다(`0x140204` 의 `4` = BAR 의 64-bit 타입). `fmt_size` 는 "1024 로 나누어떨어지는 동안 나누고 K→M→G→T".

</details>

<details><summary>힌트 3 — cap 순회의 세 가지 정지 조건</summary>

`p = cfg[0x34] & ~3`, 그다음 `p = cfg[p+1] & ~3`. 멈출 곳: `p == 0` (정상 끝), `p < 0x40` (헤더 안을 가리킴 → `E_BADPTR`), `p + 2 > len` (**비root 의 64B 버퍼** → `E_TRUNC`), 이미 본 `p` (`E_LOOP`). 방문 표시는 `uint8_t seen[256]` 하나면 된다 — 오프셋이 8비트라서. Status.CapList(0x06 bit 4)가 0 이면 `0x34` 를 믿지 않는다.

</details>

<details><summary>힌트 4 — read_dev 의 순서와 "없음" 처리</summary>

config → vendor/device/class → resource 는 없으면 `E_IO`. `driver` 는 `readlink()` 가 실패하면 그냥 `""` — 드라이버가 안 붙은 장치(호스트 브리지 00:00.0, QEMU 의 NVMe)는 흔하다. basename 은 `strrchr(link, '/')`. 마지막에 `walk_caps(d->cfg, d->cfg_len, …)` — 길이를 **실제로 읽은 바이트 수**로 넘겨야 비root 64B 가 `E_TRUNC` 로 드러난다.

</details>

<details><summary>힌트 5 — lspci -vvv 에서 필요한 줄만</summary>

최상위 줄은 탭 **하나**로 시작한다 (`\tRegion`, `\tCapabilities:`), 그 아래 세부는 탭 둘(`\t\tBAR=0 offset=…`). 그래서 `starts(ln, "\tCapabilities: [")` 처럼 접두사로 고르면 된다. Region 줄: 번호 → `Memory at`/`I/O ports at` → 16진 주소 → `(64-bit`, `, prefetchable)`, `[disabled]`, `[size=` … `]` 를 `find_in` 으로. 탭 한 줄짜리 `Capabilities: <access denied>` 도 잊지 말 것.

</details>

## 5. 풀이

### VM 실측 먼저 — 무엇이 보이나

`capture_vm.txt` 에서 (전부 실제 출력):

```text
$ lspci -tv
-[0000:00]-+-00.0  Apple Inc. Device 1a05
           +-01.0  Red Hat, Inc. Virtio 1.0 network device
           +-05.0  Red Hat, Inc. Virtio 1.0 console
           +-06.0  Red Hat, Inc. Virtio 1.0 block device
           …
           \-0c.0  Apple Inc. Device 1a06
$ lspci -vvv -s 00:06.0            # 비root
	Region 0: Memory at 380020000 (64-bit, non-prefetchable) [size=64K]
	Region 2: Memory at 50001540 (32-bit, non-prefetchable) [size=64]
	Capabilities: <access denied>
	Kernel driver in use: virtio-pci
$ sudo lspci -vvv -s 00:06.0       # root: 같은 자리에
	Capabilities: [40] Vendor Specific Information: VirtIO: CommonCfg
	Capabilities: [50] Vendor Specific Information: VirtIO: ISR
	Capabilities: [60] Vendor Specific Information: VirtIO: Notify
	Capabilities: [74] Vendor Specific Information: VirtIO: DeviceCfg
	Capabilities: [84] MSI-X: Enable+ Count=2 Masked-
$ sudo setpci -s 00:06.0 COMMAND           → 0416
$ sudo setpci -s 00:06.0 BASE_ADDRESS_0    → 80020004
$ sudo setpci -s 00:06.0 CAP_MSIX+2.w      → 8001
$ sudo setpci -s 00:06.0 CAP_EXP+0x12.w
setpci: 0000:00:06.0: Instance #0 of Capability 0010 not found - there are no capabilities with that id.
config size: user 64 B, root 256 B
```

읽는 법: 트리가 `-[0000:00]-` 한 단계뿐 — 브리지가 없고 모든 장치가 버스 00 에 붙은 평평한 토폴로지다. `CAP_EXP+0x12.w` (= PCIe capability + 0x12 = **LnkSta**)가 실패하는 이유는 이 virtio 장치들에 PCIe capability(ID 0x10)가 아예 없기 때문이다 — cap 은 vendor-specific(0x09) 넷과 MSI-X(0x11) 하나뿐. 가상 머신의 "링크" 는 물리 링크가 아니므로 LnkCap/LnkSta 는 이 VM 에선 볼 수 없고, 그래서 아래 QEMU 캡처와 가상 덤프를 쓴다. config 가 256B 인 것도 같은 이유(PCIe extended config 4KB 없음).

### Part A — 헤더 64바이트를 손으로

`Linux include/uapi/linux/pci_regs.h` 기준 오프셋에 실측 바이트를 얹은 그림:

```svg
<svg viewBox="0 0 740 430" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs>
    <marker id="b06c" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto">
      <path d="M0,0 L8,4 L0,8 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">00:06.0 Type 0 헤더 (DW 단위, 왼쪽이 bit 31) + capability 체인</text>
  <g fill="currentColor" font-family="monospace">
    <text x="12" y="45">00</text><text x="12" y="67">04</text><text x="12" y="89">08</text><text x="12" y="111">0C</text>
    <text x="12" y="133">10</text><text x="12" y="155">14</text><text x="12" y="177">18</text><text x="12" y="199">1C</text>
    <text x="12" y="221">20</text><text x="12" y="243">24</text><text x="12" y="265">28</text><text x="12" y="287">2C</text>
    <text x="12" y="309">30</text><text x="12" y="331">34</text><text x="12" y="353">38</text><text x="12" y="375">3C</text>
  </g>
  <g stroke="currentColor">
    <rect x="40" y="30" width="150" height="22" class="f-mut"/><rect x="190" y="30" width="150" height="22" class="f-mut"/>
    <rect x="40" y="52" width="150" height="22" class="f-acc"/><rect x="190" y="52" width="150" height="22" class="f-acc"/>
    <rect x="40" y="74" width="225" height="22" class="f-mut"/><rect x="265" y="74" width="75" height="22" class="f-mut"/>
    <rect x="40" y="96" width="75" height="22"/><rect x="115" y="96" width="75" height="22"/><rect x="190" y="96" width="75" height="22"/><rect x="265" y="96" width="75" height="22"/>
    <rect x="40" y="118" width="300" height="22" class="f-ok"/><rect x="40" y="140" width="300" height="22" class="f-ok"/>
    <rect x="40" y="162" width="300" height="22" class="f-ok"/>
    <rect x="40" y="184" width="300" height="22"/><rect x="40" y="206" width="300" height="22"/><rect x="40" y="228" width="300" height="22"/>
    <rect x="40" y="250" width="300" height="22"/>
    <rect x="40" y="272" width="150" height="22" class="f-mut"/><rect x="190" y="272" width="150" height="22" class="f-mut"/>
    <rect x="40" y="294" width="300" height="22"/>
    <rect x="40" y="316" width="225" height="22"/><rect x="265" y="316" width="75" height="22" class="f-warn"/>
    <rect x="40" y="338" width="300" height="22"/>
    <rect x="40" y="360" width="75" height="22"/><rect x="115" y="360" width="75" height="22"/><rect x="190" y="360" width="75" height="22"/><rect x="265" y="360" width="75" height="22"/>
  </g>
  <g fill="currentColor">
    <text x="48" y="45">Device ID 1042</text><text x="198" y="45">Vendor ID 1af4</text>
    <text x="48" y="67">Status 0010 Cap+</text><text x="198" y="67">Command 0416</text>
    <text x="48" y="89">Class 01 80 00</text><text x="273" y="89">Rev 01</text>
    <text x="46" y="111">BIST 00</text><text x="121" y="111">Hdr 00</text><text x="196" y="111">Lat 40</text><text x="271" y="111">CLS 00</text>
    <text x="48" y="133">BAR0 80020004 (mem, 64-bit, type 10b)</text>
    <text x="48" y="155">BAR1 00000003 (BAR0 상위 32비트)</text>
    <text x="48" y="177">BAR2 50001540 (mem, 32-bit)</text>
    <text x="48" y="199">BAR3 00000000</text><text x="48" y="221">BAR4 00000000</text><text x="48" y="243">BAR5 00000000</text>
    <text x="48" y="265">CardBus CIS 00000000</text>
    <text x="48" y="287">Subsys ID 0042</text><text x="198" y="287">Subsys VID 1af4</text>
    <text x="48" y="309">Expansion ROM 00000000</text>
    <text x="48" y="331">reserved</text><text x="271" y="331">Cap 40</text>
    <text x="48" y="353">reserved</text>
    <text x="44" y="375">MaxLat 00</text><text x="119" y="375">MinGnt 00</text><text x="194" y="375">IntPin 01</text><text x="269" y="375">IntLn ff</text>
  </g>
  <line x1="10" y1="390" x2="350" y2="390" stroke="currentColor" stroke-dasharray="5,3"/>
  <text x="12" y="420" fill="currentColor">↑ 0x40: 비root 의 sysfs config 읽기는 여기서 끝 (64B)</text>
  <g stroke="currentColor">
    <rect x="450" y="40" width="200" height="30" rx="4" class="f-acc"/>
    <rect x="450" y="98" width="200" height="30" rx="4" class="f-mut"/>
    <rect x="450" y="156" width="200" height="30" rx="4" class="f-mut"/>
    <rect x="450" y="214" width="200" height="30" rx="4" class="f-mut"/>
    <rect x="450" y="272" width="200" height="30" rx="4" class="f-mut"/>
    <rect x="450" y="330" width="200" height="30" rx="4" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="460" y="60">0x34 = 40 (Status bit4=1)</text>
    <text x="460" y="118">[40] 09 → next 50</text>
    <text x="460" y="176">[50] 09 → next 60</text>
    <text x="460" y="234">[60] 09 → next 74</text>
    <text x="460" y="292">[74] 09 → next 84</text>
    <text x="460" y="350">[84] 11 → next 00 끝</text>
  </g>
  <g stroke="currentColor" marker-end="url(#b06c)">
    <line x1="550" y1="70" x2="550" y2="96"/><line x1="550" y1="128" x2="550" y2="154"/>
    <line x1="550" y1="186" x2="550" y2="212"/><line x1="550" y1="244" x2="550" y2="270"/>
    <line x1="550" y1="302" x2="550" y2="328"/>
    <line x1="340" y1="327" x2="448" y2="60"/>
  </g>
  <text x="420" y="385" fill="currentColor">09 = Vendor Specific (virtio 구조 위치)</text>
  <text x="420" y="401" fill="currentColor">11 = MSI-X: cap+2 = 8001 → Enable+, Count=2</text>
</svg>
```

바이트 → 필드 → lspci 줄 (`sudo lspci -xxxx -s 00:06.0` 첫 줄 `00: f4 1a 42 10 16 04 10 00 01 00 80 01 00 40 00 00` 부터):

| 오프셋 | 바이트(리틀엔디언) | 값 | lspci 가 찍는 것 |
|---|---|---|---|
| 0x00 / 0x02 | `f4 1a` / `42 10` | VID 0x1af4, DID 0x1042 | `[1af4:1042]` (Red Hat virtio 1.0 블록) |
| 0x04 | `16 04` | Command 0x0416 = bit1 Mem, bit2 BusMaster, bit4 MemWINV, bit10 INTx disable | `Control: I/O- Mem+ BusMaster+ … MemWINV+ … DisINTx+` |
| 0x06 | `10 00` | Status 0x0010 = bit4 Cap List | `Status: Cap+ …` |
| 0x08–0x0B | `01 00 80 01` | Rev 01, class = `01 80 00` (base 01 저장장치 / sub 80 기타 / prog-if 00) | `Mass storage controller … (rev 01)`, `lspci -n` 의 `0180` |
| 0x0D / 0x0E | `40` / `00` | Latency 64, Header Type 0 (bit7=0 → 단일 함수) | `Latency: 64` |
| 0x10, 0x14 | `04 00 02 80`, `03 00 00 00` | BAR0 = 0x80020004: bit0=0 메모리, bits2:1=10b 64-bit, bit3=0 non-prefetch → 주소 = `0x00000003_80020000` | `Region 0: Memory at 380020000 (64-bit, non-prefetchable)` |
| 0x18 | `40 15 00 50` | BAR2 = 0x50001540 → 32-bit 메모리, 주소 0x50001540 | `Region 2: Memory at 50001540 (32-bit, …)` |
| 0x2C | `f4 1a 42 00` | Subsystem 1af4:0042 | `Subsystem: Red Hat, Inc. Device 0042` |
| 0x34 | `40` | 첫 capability 0x40 | (cap 순회 시작) |
| 0x3C / 0x3D | `ff` / `01` | IntLine 0xff(ARM 에선 의미 없음), IntPin 1 = INTA# | `Interrupt: pin A routed to IRQ 16` — **16 은 sysfs `irq`** 에서 |

크기는 config 에 없다. 크기를 알려면 BAR 에 all-1s 를 써 봐야 하는데(PCIE-B05), 그건 커널이 부팅 때 이미 했고 결과를 `resource` 에 남겨 두었다:

| resource 줄 | start | end | flags | 해석 |
|---|---|---|---|---|
| 0 (BAR0) | 0x380020000 | 0x38002ffff | `0x140204` | MEM 0x200 + SIZEALIGN 0x40000 + MEM_64 0x100000 + 타입 비트 0x4 → 64K, 64-bit |
| 1 (BAR1) | 0 | 0 | 0 | BAR0 의 상위 절반이라 비어 있다 |
| 2 (BAR2) | 0x50001540 | 0x5000157f | `0x040200` | MEM + SIZEALIGN → 0x40 = 64B, 32-bit |
| 3–12 | 0 | 0 | 0 | BAR3–5, 6 = ROM, 7–12 = SR-IOV VF BAR (CONFIG_PCI_IOV, 커널 `resource_show` — 줄 구성은 기억 기반, 확인 권장) |

교차 검증(테스트 A2): `(BAR1 << 32 | BAR0 & ~0xF) == resource[0].start` 와 `flags & 0xF == BAR0 & 0xF`. 크기 문자열은 pciutils 의 `show_size()` 규칙 — 1024 로 나누어떨어지는 동안 나눈다: 65536 → 64 **K**, 64 → `64`, 6144 → `6K`, 4097 → `4097`. 이 VM 의 10개 함수 BAR 들(64K, 32K, 128, 64, 4K, 1K)이 전부 같은 문자열로 나와야 C3 가 통과한다.

### Part A — cap 순회 trace

`0x34=40` → `cfg[0x40]=09, cfg[0x41]=50` → `[50] 09 next 60` → `[60] 09 next 74` → `[74] 09 next 84` → `[84] 11 next 00` → 끝, 5개. 리스트는 **정렬되어 있지 않을 수 있다**: QEMU NVMe 는 `40(MSI-X) → 80(PCIe) → 60(PM)`, QEMU 루트 포트는 `54(PCIe) → 48(MSI-X) → 40(SSVID)` (테스트 A4, 실측 덤프). 그래서 "오프셋 순으로 정렬해 두고 이진 탐색" 같은 최적화는 틀린다 — 체인을 따라가야 한다.

### Part B — 왜 비root 는 64바이트인가, 그리고 가짜 sysfs

커널 `drivers/pci/pci-sysfs.c` 의 `pci_read_config()` 는 `CAP_SYS_ADMIN` 이 없으면 읽기 크기를 64바이트(CardBus 는 128)로 자른다 — 소스 주석의 이유는 "정의되지 않은 config 영역을 읽으면 멈추는 칩이 있다". 그래서 비root `lspci -xxx` 는 4줄만, `lspci -vvv` 는 Status 에 `Cap+` 가 있는데도 cap 을 못 따라가 `Capabilities: <access denied>` 를 찍는다. 우리 파서도 같은 상황을 `E_TRUNC` 로 보고하도록 만들었다 (`p + 2 > len`) — C5 테스트가 nobody 로 실제로 읽어 64B 와 `E_TRUNC` 를 확인하고, C1 은 비root `-vvv` 텍스트와 우리 64B 결과가 "같은 말을 한다" 를 `compare_dev` 로 확인한다. 같은 원리로 `/proc/iomem` 도 비root 에게는 모든 주소가 `00000000-00000000` 이다 (연습 4).

가짜 sysfs 트리(`/tmp/PCIE-B06_fake.XXXXXX/0000:00:1e.0/…`)를 쓰는 이유: 실제 `/sys` 에서는 "config 파일이 없는 장치", "vendor 파일과 config 가 다른 장치" 를 만들 수 없다. `read_dev` 가 경로 접두사(`sysroot`)를 인자로 받게 하면 같은 코드가 진짜/가짜 트리에서 돈다 — 펌웨어 쪽에서 레지스터 접근을 함수 포인터로 빼서 목(mock)으로 테스트하는 것과 같은 설계.

전부 0xFF 인 config (B4): VID 0xFFFF = "장치 없음" (없는 함수에 config read 를 보내면 UR/Master Abort 로 끝나고 루트 컴플렉스가 all-1s 를 돌려준다, PCIE-B08). 이때 Status 도 0xFFFF 라 CapList 비트가 1, `0x34` 는 `ff → fc`, `fc` 의 next 도 `fc` → 루프. 루프 검사가 없으면 무한 루프다. 파서는 먼저 VID 를 보고 0xFFFF 면 아무것도 믿지 않는 게 정석이고, 그래도 순회 쪽에서 한 번 더 막는다.

### Part C — QEMU: 진짜 브리지와 링크 레지스터

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">QEMU virt 게스트 (capture_qemu.txt) — lspci -F … -tvn: \-02.0-[01]----00.0</text>
  <g stroke="currentColor">
    <rect x="250" y="32" width="240" height="44" rx="5" class="f-mut"/>
    <rect x="30" y="112" width="200" height="44" rx="5"/>
    <rect x="260" y="104" width="300" height="74" rx="5" class="f-acc"/>
    <rect x="260" y="214" width="300" height="74" rx="5" class="f-ok"/>
    <line x1="370" y1="76" x2="370" y2="104"/><line x1="370" y1="90" x2="130" y2="90"/><line x1="130" y1="90" x2="130" y2="112"/>
    <line x1="410" y1="178" x2="410" y2="214" stroke-width="3"/>
  </g>
  <g fill="currentColor">
    <text x="262" y="50">00:00.0 Host bridge 1b36:0008 (bus 00)</text>
    <text x="262" y="68" font-family="monospace">ECAM 0x4010000000 (게스트 iomem)</text>
    <text x="40" y="130">00:01.0 virtio-net 1af4:1000</text>
    <text x="40" y="148">Type 0, driver virtio-pci</text>
    <text x="270" y="122">00:02.0 Root Port 1b36:000c, Type 1</text>
    <text x="270" y="139" font-family="monospace">pri 00 sec 01 sub 01, driver pcieport</text>
    <text x="270" y="156" font-family="monospace">LnkCap 16GT/s x32 LLActRep+</text>
    <text x="270" y="172" font-family="monospace">LnkSta 2.5GT/s x1 DLActive+</text>
    <text x="420" y="200">링크 = min(양쪽 cap) = 2.5GT/s x1</text>
    <text x="270" y="232">01:00.0 NVMe 1b36:0010, class 010802</text>
    <text x="270" y="249" font-family="monospace">LnkCap 2.5GT/s x1, LnkSta 2.5GT/s x1</text>
    <text x="270" y="266" font-family="monospace">Control: Mem- BusMaster- (nvme.ko 없음)</text>
    <text x="270" y="282" font-family="monospace">Region 0 … [disabled]</text>
  </g>
  <text x="590" y="250" fill="currentColor">QEMU 가 흉내 낸 값:</text>
  <text x="590" y="266" fill="currentColor">물리 링크 아님</text>
</svg>
```

`capture_qemu.txt` 의 핵심 (QEMU 10.2, 게스트 커널 6.18.55 defconfig — `BLK_DEV_NVME=m` 인데 모듈을 안 올렸다):

```text
#DEV 0000:00:02.0 vendor=0x1b36 device=0x000c class=0x060400 driver=pcieport cfgsize=4096
#DEV 0000:01:00.0 vendor=0x1b36 device=0x0010 class=0x010802 driver= cfgsize=4096
$ lspci -F qemu_pcie_nvme.dump -vvv -s 00:02.0
	Bus: primary=00, secondary=01, subordinate=01, sec-latency=0
	Capabilities: [54] Express (v2) Root Port (Slot+), IntMsgNum 0
		LnkCap:	Port #0, Speed 16GT/s, Width x32, ASPM L0s, Exit Latency L0s <64ns
			ClockPM- Surprise- LLActRep+ BwNot+ ASPMOptComp-
		LnkSta:	Speed 2.5GT/s, Width x1
			TrErr- Train- SlotClk- DLActive+ BWMgmt- ABWMgmt-
$ lspci -F qemu_pcie_nvme.dump -vvv -s 01:00.0
	Control: I/O- Mem- BusMaster- SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx-
	Region 0: Memory at 10000000 (64-bit, non-prefetchable) [disabled]
	Capabilities: [80] Express (v2) Endpoint, IntMsgNum 0
		LnkCap:	Port #0, Speed 2.5GT/s, Width x1, ASPM L0s, Exit Latency L0s <64ns
			ClockPM- Surprise- LLActRep- BwNot- ASPMOptComp-
		LnkSta:	Speed 2.5GT/s, Width x1
			TrErr- Train- SlotClk- DLActive- BWMgmt- ABWMgmt-
```

`lspci -F <파일>` 은 다른 기계에서 뜬 덤프를 이 기계의 lspci 로 해석하는 기능이다 — 고객 사이트의 `lspci -xxxx` 를 받아 사무실에서 `-vvv` 로 보는 실전 기술. (`Region 0` 에 크기가 없는 건 덤프에는 sizing 결과가 없어서다.)

LnkSta 16비트 (pci_regs.h `PCI_EXP_LNKSTA_*`), 실측 두 값:

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">PCIe cap + 0x12 LnkSta (16비트, pci_regs.h PCI_EXP_LNKSTA_*)</text>
  <g stroke="currentColor">
    <rect x="80" y="30" width="40" height="34" class="f-mut"/><rect x="120" y="30" width="40" height="34" class="f-mut"/>
    <rect x="160" y="30" width="40" height="34" class="f-acc"/><rect x="200" y="30" width="40" height="34" class="f-mut"/>
    <rect x="240" y="30" width="40" height="34" class="f-mut"/><rect x="280" y="30" width="40" height="34" class="f-mut"/>
    <rect x="320" y="30" width="240" height="34" class="f-ok"/><rect x="560" y="30" width="160" height="34" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="84" y="44">15</text><text x="84" y="59">LABS</text><text x="124" y="44">14</text><text x="124" y="59">LBMS</text>
    <text x="164" y="44">13</text><text x="164" y="59">DLLLA</text><text x="204" y="44">12</text><text x="204" y="59">SLC</text>
    <text x="244" y="44">11</text><text x="244" y="59">LT</text><text x="284" y="44">10</text><text x="284" y="59">TrE</text>
    <text x="330" y="44">9:4</text><text x="330" y="59">NLW 협상된 폭 (x1=1, x2=2, x4=4)</text>
    <text x="570" y="44">3:0</text><text x="570" y="59">CLS 현재 속도</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="10" y="88">0x2011</text>
    <text x="100" y="88" text-anchor="middle">0</text><text x="140" y="88" text-anchor="middle">0</text><text x="180" y="88" text-anchor="middle">1</text><text x="220" y="88" text-anchor="middle">0</text><text x="260" y="88" text-anchor="middle">0</text><text x="300" y="88" text-anchor="middle">0</text><text x="340" y="88" text-anchor="middle">0</text><text x="380" y="88" text-anchor="middle">0</text>
    <text x="420" y="88" text-anchor="middle">0</text><text x="460" y="88" text-anchor="middle">0</text><text x="500" y="88" text-anchor="middle">0</text><text x="540" y="88" text-anchor="middle">1</text><text x="580" y="88" text-anchor="middle">0</text><text x="620" y="88" text-anchor="middle">0</text><text x="660" y="88" text-anchor="middle">0</text><text x="700" y="88" text-anchor="middle">1</text>
    <text x="10" y="138">0x0024</text>
    <text x="100" y="138" text-anchor="middle">0</text><text x="140" y="138" text-anchor="middle">0</text><text x="180" y="138" text-anchor="middle">0</text><text x="220" y="138" text-anchor="middle">0</text><text x="260" y="138" text-anchor="middle">0</text><text x="300" y="138" text-anchor="middle">0</text><text x="340" y="138" text-anchor="middle">0</text><text x="380" y="138" text-anchor="middle">0</text>
    <text x="420" y="138" text-anchor="middle">0</text><text x="460" y="138" text-anchor="middle">0</text><text x="500" y="138" text-anchor="middle">1</text><text x="540" y="138" text-anchor="middle">0</text><text x="580" y="138" text-anchor="middle">0</text><text x="620" y="138" text-anchor="middle">1</text><text x="660" y="138" text-anchor="middle">0</text><text x="700" y="138" text-anchor="middle">0</text>
  </g>
  <g fill="currentColor">
    <text x="80" y="108">QEMU 루트 포트 실측: DLLLA=1, NLW=1, CLS=1 → "Speed 2.5GT/s, Width x1", DLActive+</text>
    <text x="80" y="158">가상 x2 NVMe: NLW=2, CLS=4 → "Speed 16GT/s, Width x2 (downgraded)" (LnkCap x4)</text>
    <text x="10" y="190">CLS 1..6 = 2.5 / 5 / 8 / 16 / 32 / 64 GT/s. LnkCap(+0x0C) 도 3:0 최대 속도, 9:4 최대 폭,</text>
    <text x="10" y="208">11:10 ASPM, 20 DLLLARC(lspci 의 LLActRep), 31:24 포트 번호. bit 10 은 lspci 의 TrErr (헤더에 이름 없음).</text>
  </g>
</svg>
```

**"(downgraded)" 는 어디에 찍히나.** QEMU 루트 포트는 LnkCap 16GT/s x32 인데 LnkSta 2.5GT/s x1 — 그런데 "(downgraded)" 가 없다. pciutils `ls-caps.c` 의 규칙: sta == cap 이면 표시 없음, sta > cap 이면 "(overdriven)", 그 외에 포트 종류가 **Root Port / Downstream Port / PCI→PCIe bridge** 면 표시 없음, 나머지(Endpoint, Upstream Port …)만 "(downgraded)". 아래를 향한 포트는 꽂힌 상대에 따라 좁게 붙는 게 정상이라서다. 가상 덤프로 실제 lspci 를 돌려 확인했다 (`capture_variants.txt`):

```text
루트 포트 00:02.0   LnkCap: Speed 16GT/s, Width x4   LnkSta: Speed 16GT/s, Width x2            ← 표시 없음
NVMe     01:00.0   LnkCap: Speed 16GT/s, Width x4   LnkSta: Speed 16GT/s, Width x2 (downgraded)
NVMe (gen1)        LnkCap: Speed 16GT/s, Width x4   LnkSta: Speed 2.5GT/s (downgraded), Width x4
NVMe (over)        LnkCap: Speed 5GT/s,  Width x4   LnkSta: Speed 16GT/s (overdriven), Width x4
```

그래서 판정은 문자열이 아니라 **기대값 = min(포트 LnkCap, 장치 LnkCap)** 과 LnkSta 비교로 한다 (연습 2 `link_triage`): QEMU 는 min(16GT/s x32, 2.5GT/s x1) = 2.5GT/s x1 = LnkSta → **정상**. 루트 포트만 보고 "x32 인데 x1 이네, 고장" 이라고 하면 틀린다.

### Part C — 실HW 형식 예시 해석 (가상 덤프 + 실제 lspci -F 출력)

| # | 발췌 (실제 lspci 문자열) | 뜻 | 먼저 확인할 것 |
|---|---|---|---|
| ① | NVMe `LnkCap: Speed 16GT/s, Width x4` / `LnkSta: Speed 16GT/s, Width x2 (downgraded)`, 포트 LnkCap x4 | 레인 4개 중 2개만 학습 → 대역폭 절반(Gen4 x4 ≈ 7.88 GB/s → x2 ≈ 3.94 GB/s, PCIE-B02) | 포트 쪽 LnkCap 이 정말 x4 인가(슬롯/M.2 가 x2 배선, 레인 공유·bifurcation 설정) → 재장착·다른 슬롯 → AER correctable(RxErr/BadTLP) 증가 여부 → 레인별 신호 문제면 PCIE-M08 |
| ② | NVMe `LnkSta: Speed 2.5GT/s (downgraded), Width x4` | Gen1 로 떨어짐 → 대역폭 1/6 이하. Gen3+ 는 equalization 이 필요해 EQ 실패 시 낮은 속도로 남는다 | LnkCtl2 Target Link Speed(BIOS 가 Gen1 으로 묶었나), LnkSta2 `EqualizationComplete`, 부하 중에도 그런가(일부 장치는 유휴 때 속도를 낮춘다), 리타이머/케이블 |
| ③ | 루트 포트 `LnkSta: Speed 2.5GT/s, Width x0` + `DLActive-`, `SltSta: … PresDet+` | 카드는 감지(Presence Detect)됐는데 데이터 링크가 안 올라옴 — LTSSM 이 L0 에 못 감(Detect/Polling 에 갇힘, PCIE-M07) | 장치 전원, PERST# 해제 타이밍, REFCLK, 그다음 벤더 디버그 레지스터의 LTSSM 상태. 그 아래 버스의 config read 는 all-1s |
| ④ | NVMe `Control: I/O- Mem- BusMaster-`, `Region 0 … [disabled]`, `Kernel driver in use` 줄 없음 (**QEMU 실측**) | 드라이버가 `pci_enable_device()`/`pci_set_master()` 를 안 했다 → BAR 디코드 꺼짐(MMIO 읽기 = UR → 0xFFFFFFFF), DMA·MSI 불가 | `lspci -k`/`dmesg` 로 probe 실패인지 모듈 미적재인지, `/sys/.../driver` 링크, modalias |
| ⑤ | 루트 포트 AER `UESta: … CmpltTO+ …` | 루트 포트가 보낸 non-posted 요청(MMIO read, config read)이 완료(Cpl)를 시간 안에 못 받음 | 장치가 살아 있나(SSD FW 가 hang/긴 작업 중?), 링크가 내려갔다 올라왔나(DLActive, LBMS), DevCtl2 Completion Timeout 범위, `dmesg` AER 로그, NVMe 면 컨트롤러 타임아웃·리셋(PCIE-A03, PCIE-M12) |

Don 의 SSD 펌웨어 관점에서 ⑤ 는 익숙한 장면이다: 엔드포인트 FW 가 BAR 읽기에 응답하는 경로(대개 하드웨어가 처리하지만 일부 레지스터는 FW 가 채운다)가 막히면 호스트 쪽 루트 포트에 CmpltTO 가 쌓이고 드라이버는 타임아웃 → 리셋으로 간다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/PCIE/PCIE-B06/check.sh solution
# VM 안에서 직접: gcc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && sudo ./so
# ASan/UBSan: gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o so_as && sudo ./so_as
```

VM 안에서 `sudo ./so` 로 직접 돌린 실제 출력 (발췌 — `check.sh` 는 이 중 PASS/FAIL 줄과 요약만 보여 준다):

```text
live: 10 PCI functions under /sys/bus/pci/devices, euid 0
0000:00:00.0 106b:1a05 class 060000 hdr 00 | Control 0010 Mem- BusMaster- DisINTx- | Status 0000 Cap-
    caps:
    Kernel driver in use: (none)
0000:00:06.0 1af4:1042 class 018000 hdr 00 | Control 0416 Mem+ BusMaster+ DisINTx+ | Status 0010 Cap+
    Region 0: Memory at 380020000 (64-bit, non-prefetchable) [size=64K]  flags=140204
    Region 2: Memory at 50001540 (32-bit, non-prefetchable) [size=64]  flags=040200
    caps: [40] 09 Vendor Specific [50] 09 Vendor Specific [60] 09 Vendor Specific [74] 09 Vendor Specific [84] 11 MSI-X
    Kernel driver in use: virtio-pci
0000:00:0c.0 106b:1a06 class 0c0330 hdr 00 | Control 0416 Mem+ BusMaster+ DisINTx+ | Status 0010 Cap+
    Region 0: Memory at 50000000 (32-bit, non-prefetchable) [size=4K]  flags=040200
    Region 1: Memory at 50001000 (32-bit, non-prefetchable) [size=1K]  flags=040200
    caps: [40] 11 MSI-X [4c] 05 MSI
    Kernel driver in use: xhci_hcd
PASS C2 every function: lspci -xxx bytes == our sysfs config bytes
PASS C3 every function: lspci -vvv (driver, regions addr/size/64-bit/prefetch, cap offsets) == ours
   setpci -s 0000:00:06.0 CAP_EXP+0x12.w → exit 1: setpci: 0000:00:06.0: Instance #0 of Capability 0010 not found - there are no capabilities with that id.
   nobody read /sys/bus/pci/devices/0000:00:06.0/config: 64 bytes, walk_caps → -2
57/57 passed
```

`check.sh solution` 의 마지막 두 줄은 `57/57 passed` 와 `RESULT: PASS (solution)` (exit 0).

(전체 0.3~0.6 초. ASan/UBSan 빌드도 57/57, 오류 0. 호스트 브리지 00:00.0 은 드라이버도 cap 도 BAR 도 없다 — `lspci -vvv` 에도 `Kernel driver in use` 줄이 없고, 우리도 `""` 라 대조가 통과한다.)

설계 메모: 모든 텍스트 파서는 `(text, len)` 을 받고 `next_line` 으로 길이 안에서만 움직인다 — `strtoul` 은 NUL 을 찾아 버퍼 밖으로 나갈 수 있어 쓰지 않았다(B1 의 "NUL 없는 잘린 텍스트" 테스트). `pci_dev` 는 4KB config 를 품은 고정 크기 구조체이고 `static` 으로 둔다. 명령줄에 들어가는 BDF 는 `bdf_ok()` 로 형식을 확인한 디렉터리 이름만 쓴다(셸 인젝션 방지).

## 7. 자주 하는 실수

**1. 비root 로 `lspci -vvv` 를 돌리고 "capability 가 없다/LnkSta 가 없다" 고 결론.** 비root 는 config 64B 만 본다 → `Capabilities: <access denied>`. 고친 습관: 링크·AER 를 볼 땐 항상 `sudo lspci -vvv`, 스크립트라면 `id -u` 확인. 파서에서는 "읽은 길이" 를 cap 순회에 넘겨 `E_TRUNC` 를 "없음(0개)" 과 구별한다.

**2. 루트 포트의 LnkSta 만 보고 판정.** 루트 포트 `Width x1` 에는 "(downgraded)" 가 안 붙는다(위 규칙). 반대로 QEMU 처럼 LnkCap x32 인데 x1 이어도 장치가 x1 이면 정상. 고친 코드: `expected = min(port_cap, ep_cap)` 와 비교 (연습 2 `link_triage`).

**3. `Region 1` 이 안 보이니 BAR 이 하나 빠졌다고 생각.** 64-bit BAR0 은 BAR0+BAR1 두 칸을 쓴다 — `resource` 의 1번 줄은 0, lspci 는 Region 1 을 생략. 반대 실수: BAR1 의 raw 값 `00000003` 을 "I/O BAR(bit0=1)" 로 읽는 것 (PCIE-B05 의 고전 버그).

**4. sysfs `resource` 의 flags 를 BAR raw 비트로 해석.** `0x140204` 는 커널 `IORESOURCE_*` 이다: `0x200` 은 MEM, `0x100000` 이 64-bit. BAR 비트(bit0 IO, bit2:1 타입, bit3 prefetch)와 섞으면 prefetch 를 `flags & 0x8` 로 잘못 본다 — 정답은 `IORESOURCE_PREFETCH 0x2000`.

**5. `Interrupt: … IRQ 16` 을 config 0x3C 에서 찾음.** 이 VM 의 Interrupt Line 은 `ff` 다. lspci 는 커널이 매핑한 `irq`(sysfs)를 보여 준다. ARM 에서 INTx 라우팅은 DT `interrupt-map` / ACPI `_PRT` 가 정하고, MSI-X 를 쓰는 장치는 이 숫자를 거의 안 쓴다.

**6. 드라이버가 붙은 장치에 `setpci` 로 쓰기.** `setpci -s 00:06.0 COMMAND=0` 이면 Mem 디코드가 꺼져 드라이버의 다음 MMIO 읽기가 0xFFFFFFFF, DMA 도 멈춘다 — 공유 VM·운영 서버에서 절대 금지. 이 레슨의 코드는 읽기만 한다. 쓰기 실험은 QEMU 게스트나 unbind 한 장치에서.

**7. 엔드포인트의 `DLActive-` 를 "링크 다운" 으로 읽음.** QEMU NVMe 는 정상인데 `DLActive-` 다 — LnkCap 에 `LLActRep-` (DLLLARC=0) 이면 그 비트는 의미가 없다. 링크 활성은 **아래를 향한 포트**(루트/다운스트림)의 DLActive 로 본다 (`link_triage` 첫 줄의 `DLLLARC` 검사).

## 8. 면접 Q&A

<details><summary>Q1. lspci -vvv 에서 NVMe SSD 의 링크 상태를 어떻게 확인하나요? LnkCap x4, LnkSta x2 면?</summary>

`sudo lspci -vvv -s <BDF>` 의 PCIe capability 아래 `LnkCap`(능력: 최대 속도·폭)과 `LnkSta`(지금 협상된 값)를 비교한다. 엔드포인트면 lspci 가 `(downgraded)` 를 붙여 준다. x4 → x2 는 대역폭 절반(Gen4 기준 ≈ 7.88 → 3.94 GB/s), 순차 읽기 스펙(~7 GB/s 급)이 3.5 GB/s 근처에서 막힌다. 확인 순서: 위쪽 포트의 LnkCap(슬롯이 진짜 x4 인가, M.2 어댑터·레인 공유) → 재장착/다른 슬롯 → AER correctable 카운트(RxErr, BadTLP, BadDLLP) → 레인별 마진·EQ (PCIE-M08, SIV-M06). 꼬리질문 "폭이 학습 후에 줄 수도 있나" → 하드웨어 자율 폭 변경(LnkCtl HAWD, LnkSta LABS)과 신뢰성 문제로 인한 재학습이 있다 — LABS/LBMS 비트가 힌트.

</details>

<details><summary>Q2. L0 가 뭔가요? lspci 로 L0 인지 알 수 있나요?</summary>

L0 는 LTSSM(Link Training and Status State Machine)의 정상 동작 상태 — Detect → Polling → Configuration → L0 로 들어오면 TLP 가 오간다(PCIE-M07). lspci 에는 LTSSM 상태 자체가 없다(벤더 디버그 레지스터나 프로토콜 분석기가 보여 준다). 간접 증거: 아래를 향한 포트의 `LnkSta: … DLActive+` (Data Link Layer 가 DL_Active = 링크 업), `Train-` (학습 중 아님), 협상된 속도·폭이 0 이 아님. ASPM 이 켜져 있으면 L0s/L1 로 잠깐씩 들어가지만 소프트웨어가 보는 상태는 여전히 "링크 업" 이다(PCIE-M09).

</details>

<details><summary>Q3. config read 가 0xFFFF(FFFF) 를 돌려주면 무슨 뜻인가요?</summary>

그 BDF 에 함수가 없다: 요청이 Unsupported Request/Master Abort 로 끝나고 루트 컴플렉스가 all-1s 를 데이터로 돌려준다. 열거(enumeration)는 이걸로 장치 유무를 판단한다(PCIE-B09). VID 0xFFFF 는 PCI-SIG 가 할당하지 않는 값이라 모호하지 않다. 같은 현상이 MMIO 에서 나면(BAR 읽기 = 0xFFFFFFFF) "Mem 디코드 꺼짐, 링크 다운, 장치 리셋 중, surprise removal" 을 의심한다 — 드라이버가 읽기 값이 전부 1 인지 검사하는 이유. 함정: 일부 레지스터는 정상 값도 0xFFFFFFFF 일 수 있어, "장치가 사라졌나" 는 VID 를 다시 읽어 확인한다.

</details>

<details><summary>Q4. Control: Mem- BusMaster- 인 장치는 어떤 상태죠? 누가 켜나요?</summary>

Command 레지스터(0x04) bit1 Memory Space 가 0 이면 장치가 BAR 주소의 MMIO 를 받지 않는다(접근 = UR, CPU 는 0xFFFFFFFF). bit2 Bus Master 가 0 이면 장치가 DMA 도, MSI/MSI-X(메모리 쓰기라서)도 못 보낸다. Linux 에서는 드라이버 probe 의 `pci_enable_device()` 가 Mem/IO 를, `pci_set_master()` 가 Bus Master 를 켠다 — 그래서 nvme.ko 를 안 올린 QEMU 게스트의 NVMe 는 `Mem- BusMaster-` + `Region 0 … [disabled]` 였다. BAR sizing 중에 Mem 을 끄는 이유는 PCIE-B05.

</details>

<details><summary>Q5. ARM 리눅스는 PCI 장치를 어떻게 찾나요? (x86 의 CF8/CFC 가 없는데)</summary>

ECAM(Enhanced Configuration Access Mechanism): 메모리 맵된 창 하나에서 `base + (bus<<20 | dev<<15 | fn<<12 | reg)` 로 config 를 읽는다(PCIE-B08). 창의 위치는 DT 의 `pci-host-ecam-generic` 노드 `reg`, ACPI 시스템이면 MCFG 테이블이 알려 준다. 이 VM 의 `/proc/iomem`(root) 에 `40000000-4fffffff : PCI ECAM` = 256 MB = 버스 256개 × 1 MB 가 보이고, QEMU 게스트는 `4010000000-401fffffff : PCI ECAM` 이다. 커널은 버스 0 부터 DFS 로 훑으며(PCIE-B09) 각 함수의 VID 를 읽는다. lspci 는 그 결과를 sysfs 로 본다.

</details>

<details><summary>Q6. Type 0 와 Type 1 헤더는 lspci 출력에서 어떻게 구별하죠?</summary>

Header Type(0x0E) 하위 7비트 0 = 일반 장치(Type 0, BAR 6개), 1 = PCI-PCI 브리지(Type 1: 루트 포트, 스위치 포트 — BAR 2개 + Primary/Secondary/Subordinate Bus, 메모리 윈도우). lspci 에서는 class `0604 PCI bridge`, `-vvv` 의 `Bus: primary=00, secondary=01, subordinate=01` 과 `Memory behind bridge:` 줄, `-tv` 의 `02.0-[01]--` (대괄호 = 그 브리지 아래 버스 범위). 이 VM 은 브리지가 없어 `-tv` 가 한 단계뿐이고, QEMU 캡처의 00:02.0 이 Type 1 이다(PCIE-B07). bit 7 은 multi-function 표시.

</details>

<details><summary>Q7. setpci 로 레지스터를 읽는 법과, LnkSta 를 바로 읽는 법은?</summary>

`setpci -s <BDF> <reg>[.b|.w|.l]` — 이름(`COMMAND`, `BASE_ADDRESS_0`)이나 오프셋(`0x34.b`), 그리고 capability 상대 주소(`CAP_EXP+0x12.w` = PCIe cap 의 LnkSta, `CAP_MSIX+2.w`, extended 는 `ECAP_AER+4.l` = UESta). setpci 가 cap 리스트를 직접 순회해 오프셋을 찾으므로 장치마다 cap 위치가 달라도 같은 명령이 된다. cap 이 없으면 "Instance #0 of Capability 0010 not found" 로 실패(이 VM). 쓰기는 `reg=value[:mask]` — 예: 다운스트림 포트에서 Retrain Link(LnkCtl bit 5)를 `setpci -s <port> CAP_EXP+0x10.w=0020:0020` 처럼 마스크로 한 비트만 (문법은 pciutils man 확인 권장, 운영 장비에선 신중히).

</details>

<details><summary>Q8. 루트 포트 AER 에 CmpltTO+ 가 찍혔습니다. 무엇을 의심하나요?</summary>

Completion Timeout = 요청자가 non-posted 요청(메모리 읽기, config, I/O)을 보내고 정해진 시간(DevCtl2 의 범위, 예: 50us~50ms) 안에 완료를 못 받았다. 루트 포트에 찍혔으면 요청자는 호스트(CPU 의 MMIO/config 읽기). 원인 후보: 장치 FW hang·리셋 중, 링크 다운/재학습 중 유실, 장치가 요청을 조용히 버림. 확인: `dmesg` 의 AER 로그와 시점, 장치 쪽 DevSta/AER, 링크 상태 변화(DLActive, LBMS), NVMe 라면 같은 시각의 I/O 타임아웃과 컨트롤러 리셋. 장치(엔드포인트) AER 에 찍혔다면 반대로 **장치의 DMA 읽기**가 호스트 메모리에서 응답을 못 받은 것 — IOMMU fault, 호스트 쪽 문제를 본다(PCIE-M12, PCIE-M15).

</details>

<details><summary>Q9. 왜 비root 사용자는 config 64바이트만 읽을 수 있죠? 우회해야 하나요?</summary>

커널 `pci_read_config()`(sysfs)가 `CAP_SYS_ADMIN` 없는 읽기를 표준 헤더 64B 로 제한한다 — 정의 안 된 확장 영역을 읽으면 멈추는 칩이 있었고, 일부 레지스터는 읽기에 부작용(read-to-clear)이 있을 수 있어서다. 우회가 아니라 root(sudo) 로 읽는 게 맞다. 같은 정책이 `/proc/iomem` 주소 가리기(비root 는 0)에도 있다 — 커널 주소 배치 노출을 막는 목적. 검증 스크립트를 만들 땐 "권한 부족 = 데이터 없음" 을 구별해 보고하도록(우리 `E_TRUNC`).

</details>

## 9. 추가 연습

### 연습 1 — lspci -tvn 트리를 직접 그리기

`tdev{bus,dev,fn,vid,did,is_bridge,sec,sub}` 목록으로 `lspci -tvn` 과 **글자 하나까지 같은** 트리를 만들라. (a) 이 VM 의 sysfs 10개 함수 → 실제 `lspci -tvn` 출력과 비교, (b) QEMU 게스트 4개 함수(64B 헤더 실측) → `lspci -F … -tvn` 의 `\-02.0-[01]----00.0  1b36:0010` 과 비교, (c) sub 를 02 로 바꾸면 `-[01-02]-`, 2차 버스가 비면 가지가 `--` 로 끝나는지.

<details><summary>답</summary>

pciutils `ls-tree.c` 의 방식 그대로: 한 줄 버퍼에 `"-[0000:00]-"` 를 쓰고, 버스의 자식이 하나면 `--`, 여럿이면 마지막 전까지 `+-`, 마지막은 `\-` 를 앞에 붙여 `dd.f` 를 쓴다. 브리지면 `-[sec]-`(또는 `-[sec-sub]-`) + `-` 를 붙이고 그 2차 버스로 재귀, 아니면 `  vvvv:dddd` 를 붙이고 줄을 내보낸다. 핵심 트릭은 `print_it`: 줄을 출력한 뒤 버퍼의 `+`·`|` 를 `|` 로, 나머지를 공백으로 바꿔 **다음 줄의 왼쪽 여백**으로 재사용한다 — 그래서 `+-01.0` 이 `-[0000:00]-` 아래 11칸 들여쓰기로 나온다. 버스 번호는 브리지의 0x19/0x1A 바이트(Type 1)에서. 실측 결과 (a)(b)(c) 모두 일치 (아래 출력).

</details>

### 연습 2 — LnkCap/LnkSta 를 lspci 문자열로, 그리고 판정

PCIe cap(ID 0x10)을 찾아 `+0x0C` LnkCap, `+0x12` LnkSta 를 lspci 와 같은 문자열(`Port #0, Speed 16GT/s, Width x32, ASPM L0s` / `Speed 2.5GT/s, Width x1` + `TrErr- Train- SlotClk- DLActive+ BWMgmt- ABWMgmt-`)로 만들고, "(downgraded)/(overdriven)" 규칙을 구현하라. 그리고 `link_triage(port_cap, port_sta, ep_cap)` → OK / DOWN / SPEED / WIDTH. QEMU 실측 두 장치와 가상 덤프 5종으로 검증.

<details><summary>답</summary>

PCIe cap 위치: 루트 포트 0x54, NVMe 0x80 (체인을 따라가서 찾는다 — 연습 안의 `find_cap`). 실측 값: 루트 포트 LnkCap `0x00300604` (속도 4, 폭 0x20=32, ASPM 01=L0s, DLLLARC·LBNC), LnkSta `0x2011`; NVMe LnkCap `0x00000411`, LnkSta `0x0011`. 포트 종류는 Express Capabilities(+0x02) bits 7:4 — 4 = Root Port, 0 = Endpoint. `link_compare`: sta > cap → " (overdriven)", 같으면 "", 포트 종류가 4/6/8 이면 "", 아니면 " (downgraded)". 판정: 포트 LnkCap 에 DLLLARC 가 있는데 DLActive 가 0 이거나 폭이 0 이면 DOWN, 아니면 min(양쪽 cap) 과 비교해 SPEED/WIDTH 비트. QEMU = OK (기대값 2.5GT/s x1), x2 = WIDTH, gen1 = SPEED, dldown = DOWN, Gen3 x2 = SPEED|WIDTH. 실제 SSD 에서는 LnkCap2(지원 속도 벡터)·LnkSta2(EQ 완료)까지 보는 것이 다음 단계 (PCIE-B04, PCIE-M08).

</details>

### 연습 3 — lspci -d 필터를 sysfs 로

`lspci -n -d ::0180` (class 0x0180 = 저장장치/기타)과 `lspci -n -d 1af4:` (virtio 벤더)를 sysfs config 스캔으로 다시 구현해 BDF 목록이 같은지 확인하라. NVMe 를 찾는다면 어떤 필터?

<details><summary>답</summary>

class 는 config 0x08 DW 의 상위 24비트(`cfg_rd32(0x08) >> 8`), `-d ::0180` 은 그중 base+sub 16비트(`>> 16`) 비교. 이 VM: `00:06.0 00:07.0 00:08.0` (virtio-blk 2개 + virtio-fs) — lspci 와 같다. 벤더 1af4 는 8개 (00:01.0–00:0b.0). NVMe 는 class `01 08 02`(저장장치 / NVM / prog-if 02 = NVMe) → `lspci -d ::0108` 후 prog-if 확인, 커널 드라이버는 class 매칭으로 붙는다(`PCI_CLASS_STORAGE_EXPRESS 0x010802`, nvme.ko 의 id 테이블 — PCIE-M13). QEMU NVMe 의 class 는 실측 `0x010802`.

</details>

### 연습 4 — BAR 는 /proc/iomem 에 어떻게 보이나

각 장치의 `resource` 메모리 BAR 가 root 의 `/proc/iomem` 에 `start-end : 0000:bb:dd.f` 줄로 정확히 있는지 확인하고, nobody 로 `/proc/iomem` 을 읽으면 모든 주소가 0 인지 확인하라.

<details><summary>답</summary>

`/proc/iomem` 은 커널 리소스 트리: `PCI Bus 0000:00` 창 아래에 BDF 이름으로 BAR 가 잡히고(`pci_claim_resource`), 드라이버가 `pci_request_regions()` 를 하면 그 아래 한 단계 더 드라이버 이름(`virtio-pci-modern`, `xhci-hcd`)이 붙는다. 실측: 메모리 BAR 18개 전부 일치 (예 `380020000-38002ffff : 0000:00:06.0` 와 그 아래 `virtio-pci-modern`). 32-bit BAR2(MSI-X 테이블)는 BDF 줄만 있고 드라이버 이름이 없다 — virtio 드라이버가 그 영역을 요청하지 않고 MSI 코어가 매핑하기 때문으로 보인다(추정). nobody 로 읽으면 61줄 전부 `00000000-00000000`. 디버깅 팁: "MMIO 주소가 이상하다" 할 때 `sudo cat /proc/iomem` 으로 그 주소가 누구 것인지 먼저 본다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/PCIE/PCIE-B06/check.sh practice
```

`sudo ./pr` 직접 실행 출력 (발췌, `check.sh practice` 는 PASS 줄 + `24/24 passed` + `RESULT: PASS (practice)`):

```text
-[0000:00]-+-00.0  1b36:0008
           +-01.0  1af4:1000
           \-02.0-[01]----00.0  1b36:0010
PASS P1 QEMU: root port 00:02.0 (Type 1, sec=sub=01) → tree == lspci -F -tvn
PASS P1 VM: our tree from sysfs == lspci -tvn (exact text)
   QEMU root port: type 4 LnkCap 00300604 LnkSta 2011 | NVMe: type 0 LnkCap 00000411 LnkSta 0011
PASS P2 RP LnkSta == lspci 'Speed 2.5GT/s, Width x1' (no '(downgraded)' on a root port) + DLActive+
PASS P2 triage QEMU: RP x32 Gen4 cap but EP x1 Gen1 → expected min = 2.5GT/s x1 → OK
PASS P2 x2 variant: EP 'Speed 16GT/s, Width x2 (downgraded)', RP 'Speed 16GT/s, Width x2'
PASS P3 class ::0180 (mass storage 'other'): our sysfs scan == lspci -n -d ::0180
   /proc/iomem: 18/18 memory BARs claimed as '<start>-<end> : <BDF>'
PASS P4 /proc/iomem as nobody: every address reads as 0 (kernel hides them)
   nobody saw 61 iomem lines, all 00000000-00000000
24/24 passed
```

(발췌. 실측 재현: `capture.sh vm|qemu|vars` 가 `capture_*.txt` 를 다시 만든다 — QEMU 부팅 포함 수 초, VM 의 장치는 읽기만.)

## 10. 핵심 정리

- lspci = sysfs 해석기: 헤더 64B·cap 영역·`resource`(BAR 주소·크기·IORESOURCE flags)·`driver` 링크·`irq`. Linux 에선 `lspci -xxx` 바이트 == `/sys/.../config` 바이트.
- 비root 는 config 64B 만 → `Capabilities: <access denied>`. 링크·AER 는 항상 `sudo lspci -vvv`. `/proc/iomem` 주소도 비root 엔 0.
- BAR: config 에는 주소+타입 비트만, 크기는 커널이 sizing 해 `resource` 에. 64-bit BAR 는 두 칸(BAR1 줄 0). 크기 문자열은 1024 로 나누어떨어질 때만 K/M/G.
- cap 순회: Status.CapList → 0x34 → next, `&~3`, `<0x40`/넘침/루프에서 정지. 순서는 정렬 안 됨.
- `setpci CAP_EXP+0x12.w` = LnkSta. cap 이 없으면 실패(이 VM 의 virtio = conventional PCI 형).
- LnkSta 판정은 min(포트 LnkCap, 장치 LnkCap) 과 비교. "(downgraded)" 는 엔드포인트/업스트림에만 찍히고 루트·다운스트림 포트엔 안 찍힌다. 링크 업은 아래를 향한 포트의 DLActive+ (LLActRep+ 일 때).
- 증상 → 첫 확인: x2(downgraded) → 슬롯·레인·AER correctable / 2.5GT/s(downgraded) → Target Speed·EQ / DLActive- + PresDet+ → 전원·PERST#·REFCLK·LTSSM / Mem- → 드라이버 probe / CmpltTO+ → 장치 FW·링크·DevCtl2 타임아웃.

## 참고자료

- Linux `include/uapi/linux/pci_regs.h` (VM `/usr/include/linux/pci_regs.h`, 커널 7.0 헤더) — 이 레슨의 모든 config 오프셋·비트(Command/Status/BAR/cap ID/LnkCap/LnkSta/AER UESta). `include/linux/ioport.h` — `IORESOURCE_*`.
- Linux `drivers/pci/pci-sysfs.c` — `config`(비root 64B 제한), `resource`, `driver` 속성. Documentation/PCI/sysfs-pci.rst: <https://docs.kernel.org/PCI/sysfs-pci.html>
- pciutils (lspci/setpci) 소스 — `ls-caps.c` 의 `link_compare()` (downgraded/overdriven 규칙), `ls-tree.c` (트리 그리기), `lspci.c` 의 `show_size()`: <https://github.com/pciutils/pciutils> · man `lspci(8)`, `setpci(8)` (`-F` 덤프 파일, `CAP_*`/`ECAP_*` 이름, `value:mask`)
- PCI Express Base Specification (PCI-SIG) — 7.5.3 PCI Express Capability (Link Capabilities/Status), 7.8.4 AER. 절 번호는 판에 따라 다름, 스펙 확인 권장.
- QEMU 문서 `docs/pcie.txt` — `pcie-root-port`, `x-speed`/`x-width` 속성 (이 레슨의 QEMU 10.2 는 nvme 를 2.5GT/s x1 으로 보고).
- 관련 레슨: PCIE-B02 (대역폭 계산), PCIE-B03 (Type 0 헤더), PCIE-B04 (cap/ext cap, LnkCap 디코드), PCIE-B05 (BAR sizing), PCIE-B07 (Type 1), PCIE-B08 (ECAM), PCIE-B09 (열거), PCIE-M07 (LTSSM), PCIE-M08 (링크 저하 진단), PCIE-M09 (ASPM), PCIE-M12 (AER), PCIE-M13 (Linux PCI 드라이버), PCIE-M15 (IOMMU), PCIE-A03 (NVMe 타임아웃·리셋), SIV-M06 (PRBS/BER), HWLAB-M12 (equalization), BM-B04 (레지스터 쓰기 함정), ELX-B 시리즈 (sysfs)
- 이 레슨의 실측 파일: `capture_vm.txt`, `capture_qemu.txt`, `capture_variants.txt` (생성: `capture.sh`, `make_variants.py`)
