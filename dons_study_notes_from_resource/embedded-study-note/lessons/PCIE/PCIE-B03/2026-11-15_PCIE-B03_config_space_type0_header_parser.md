---
id: PCIE-B03
time: 60분
updated: 2026-11-15
---
# PCIE-B03 Configuration Space 해부: Type 0 헤더

## 학습 목표

- `lspci -xxx` (256B) / `-xxxx` (4KB) 텍스트를 **길이를 넘지 않고** 바이트 배열로 바꾸고, 잘림·문법·offset 오류를 줄 번호와 함께 거절한다.
- Type 0 헤더 0x00–0x3F 의 모든 필드(VID/DID, Command, Status, Revision, Class Code, Header Type, BAR raw, Subsystem, Cap Pointer, Interrupt Line/Pin)를 offset 과 비트로 말하고 디코드한다.
- Command 의 Memory Space / Bus Master 가 꺼졌을 때 무슨 일이 생기는지("BAR 디코드 안 됨, MMIO 접근 시 UR/0xFFFFFFFF", "장치가 DMA/MSI 못 보냄")와 Status 에러 비트의 PCIe 의미를 출력에 넣는다.
- 64비트 BAR 의 위 절반, Header Type bit 7(multi-function), VID 0xFFFF 같은 함정을 실측 덤프로 확인한다.
- 드라이버 probe 의 Command 켜기를 16비트 쓰기로 해야 하는 이유(Status 는 RW1C)를 재현한다.

## 1. 문제

실리콘 검증·SSD 펌웨어·보드 bring-up 면접에서 "`lspci -xxx` 덤프 주면 이 장치 상태를 읽어 보세요"는 단골이다 — **config space 64바이트만 읽을 줄 알면 "드라이버가 안 붙었다 / DMA 가 안 된다 / 링크 뒤 장치가 사라졌다"의 첫 번째 원인을 30초 안에 찾는다.**

모든 PCI/PCIe 함수(function)는 config space 를 가진다. 옛 PCI 는 256바이트, PCIe 는 4KB(0x100 부터 확장 영역). 그중 앞 64바이트(0x00–0x3F)가 표준 헤더(standard header)이고, 0x0E 의 Header Type 이 그 64바이트의 모양을 정한다: **Type 0 = 엔드포인트(SSD, NIC, GPU…)**, Type 1 = 브리지/스위치 포트(PCIE-B07). 호스트는 이 공간을 ECAM(PCIE-B08)으로 읽고, Linux 는 `/sys/bus/pci/devices/<BDF>/config` 로 보여 주며, `lspci -xxx` 는 그걸 16바이트씩 hex 로 찍는다. 주의: 일반 사용자에게는 **64바이트만** 보인다(실측 — 연습 1).

Don 의 SSD 경험으로 보면: 컨트롤러 펌웨어 쪽에서는 이 헤더가 "호스트가 우리를 어떻게 보는가"의 창이고(VID/DID/Class 01:08:02 는 펌웨어나 strap 이 정한다), 호스트 드라이버 쪽에서는 probe 의 첫 단계가 바로 이 헤더 확인 → Command 켜기다.

```svg
<svg viewBox="0 0 760 476" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">Type 0 헤더 0x00–0x3F: 한 줄 = DW 하나, 바이트 3 이 왼쪽 (스펙 그림 방식)</text> <g fill="currentColor" font-size="12"> <text x="130" y="40" text-anchor="middle">byte 3 (+3)</text> <text x="250" y="40" text-anchor="middle">byte 2 (+2)</text>
  <text x="370" y="40" text-anchor="middle">byte 1 (+1)</text> <text x="490" y="40" text-anchor="middle">byte 0 (+0)</text> <text x="565" y="40">Lima VM 00:06.0 실측</text> </g> <text x="12" y="64" fill="currentColor" font-family="monospace" font-size="12">0x00</text>
  <rect x="70" y="48" width="240" height="24" class="f-acc" stroke="currentColor"/> <text x="190" y="64" fill="currentColor" text-anchor="middle" font-size="12">Device ID</text> <rect x="310" y="48" width="240" height="24" class="f-acc" stroke="currentColor"/>
  <text x="430" y="64" fill="currentColor" text-anchor="middle" font-size="12">Vendor ID</text> <text x="565" y="64" fill="currentColor" font-family="monospace" font-size="12">1042 | 1af4</text> <text x="12" y="88" fill="currentColor" font-family="monospace" font-size="12">0x04</text>
  <rect x="70" y="72" width="240" height="24" class="f-warn" stroke="currentColor"/> <text x="190" y="88" fill="currentColor" text-anchor="middle" font-size="12">Status</text> <rect x="310" y="72" width="240" height="24" class="f-acc" stroke="currentColor"/>
  <text x="430" y="88" fill="currentColor" text-anchor="middle" font-size="12">Command</text> <text x="565" y="88" fill="currentColor" font-family="monospace" font-size="12">0010 | 0416</text> <text x="12" y="112" fill="currentColor" font-family="monospace" font-size="12">0x08</text>
  <rect x="70" y="96" width="360" height="24" class="f-acc" stroke="currentColor"/> <text x="250" y="112" fill="currentColor" text-anchor="middle" font-size="12">Class Code (base · sub · prog-if)</text> <rect x="430" y="96" width="120" height="24" class="f-mut" stroke="currentColor"/>
  <text x="490" y="112" fill="currentColor" text-anchor="middle" font-size="12">Revision ID</text> <text x="565" y="112" fill="currentColor" font-family="monospace" font-size="12">01 80 00 | 01</text> <text x="12" y="136" fill="currentColor" font-family="monospace" font-size="12">0x0C</text>
  <rect x="70" y="120" width="120" height="24" class="f-mut" stroke="currentColor"/> <text x="130" y="136" fill="currentColor" text-anchor="middle" font-size="12">BIST</text> <rect x="190" y="120" width="120" height="24" class="f-acc" stroke="currentColor"/>
  <text x="250" y="136" fill="currentColor" text-anchor="middle" font-size="12">Header Type</text> <rect x="310" y="120" width="120" height="24" class="f-mut" stroke="currentColor"/> <text x="370" y="136" fill="currentColor" text-anchor="middle" font-size="12">Latency Timer</text>
  <rect x="430" y="120" width="120" height="24" class="f-mut" stroke="currentColor"/> <text x="490" y="136" fill="currentColor" text-anchor="middle" font-size="12">Cache Line</text> <text x="565" y="136" fill="currentColor" font-family="monospace" font-size="12">00 | 00 | 40 | 00</text>
  <text x="12" y="160" fill="currentColor" font-family="monospace" font-size="12">0x10</text> <rect x="70" y="144" width="480" height="24" class="f-ok" stroke="currentColor"/> <text x="310" y="160" fill="currentColor" text-anchor="middle" font-size="12">BAR0</text>
  <text x="565" y="160" fill="currentColor" font-family="monospace" font-size="12">80020004</text> <text x="12" y="184" fill="currentColor" font-family="monospace" font-size="12">0x14</text> <rect x="70" y="168" width="480" height="24" class="f-ok" stroke="currentColor"/>
  <text x="310" y="184" fill="currentColor" text-anchor="middle" font-size="12">BAR1</text> <text x="565" y="184" fill="currentColor" font-family="monospace" font-size="12">00000003</text> <text x="12" y="208" fill="currentColor" font-family="monospace" font-size="12">0x18</text>
  <rect x="70" y="192" width="480" height="24" class="f-ok" stroke="currentColor"/> <text x="310" y="208" fill="currentColor" text-anchor="middle" font-size="12">BAR2</text> <text x="565" y="208" fill="currentColor" font-family="monospace" font-size="12">50001540</text>
  <text x="12" y="232" fill="currentColor" font-family="monospace" font-size="12">0x1C</text> <rect x="70" y="216" width="480" height="24" class="f-ok" stroke="currentColor"/> <text x="310" y="232" fill="currentColor" text-anchor="middle" font-size="12">BAR3</text>
  <text x="565" y="232" fill="currentColor" font-family="monospace" font-size="12">00000000</text> <text x="12" y="256" fill="currentColor" font-family="monospace" font-size="12">0x20</text> <rect x="70" y="240" width="480" height="24" class="f-ok" stroke="currentColor"/>
  <text x="310" y="256" fill="currentColor" text-anchor="middle" font-size="12">BAR4</text> <text x="565" y="256" fill="currentColor" font-family="monospace" font-size="12">00000000</text> <text x="12" y="280" fill="currentColor" font-family="monospace" font-size="12">0x24</text>
  <rect x="70" y="264" width="480" height="24" class="f-ok" stroke="currentColor"/> <text x="310" y="280" fill="currentColor" text-anchor="middle" font-size="12">BAR5</text> <text x="565" y="280" fill="currentColor" font-family="monospace" font-size="12">00000000</text>
  <text x="12" y="304" fill="currentColor" font-family="monospace" font-size="12">0x28</text> <rect x="70" y="288" width="480" height="24" class="f-mut" stroke="currentColor"/> <text x="310" y="304" fill="currentColor" text-anchor="middle" font-size="12">CardBus CIS Pointer</text>
  <text x="565" y="304" fill="currentColor" font-family="monospace" font-size="12">00000000</text> <text x="12" y="328" fill="currentColor" font-family="monospace" font-size="12">0x2C</text> <rect x="70" y="312" width="240" height="24" class="f-mut" stroke="currentColor"/>
  <text x="190" y="328" fill="currentColor" text-anchor="middle" font-size="12">Subsystem ID</text> <rect x="310" y="312" width="240" height="24" class="f-mut" stroke="currentColor"/> <text x="430" y="328" fill="currentColor" text-anchor="middle" font-size="12">Subsystem Vendor ID</text>
  <text x="565" y="328" fill="currentColor" font-family="monospace" font-size="12">0042 | 1af4</text> <text x="12" y="352" fill="currentColor" font-family="monospace" font-size="12">0x30</text> <rect x="70" y="336" width="480" height="24" class="f-mut" stroke="currentColor"/>
  <text x="310" y="352" fill="currentColor" text-anchor="middle" font-size="12">Expansion ROM Base Address</text> <text x="565" y="352" fill="currentColor" font-family="monospace" font-size="12">00000000</text> <text x="12" y="376" fill="currentColor" font-family="monospace" font-size="12">0x34</text>
  <rect x="70" y="360" width="360" height="24" class="f-mut" stroke="currentColor"/> <text x="250" y="376" fill="currentColor" text-anchor="middle" font-size="12">reserved</text> <rect x="430" y="360" width="120" height="24" class="f-acc" stroke="currentColor"/>
  <text x="490" y="376" fill="currentColor" text-anchor="middle" font-size="12">Cap Pointer</text> <text x="565" y="376" fill="currentColor" font-family="monospace" font-size="12">40</text> <text x="12" y="400" fill="currentColor" font-family="monospace" font-size="12">0x38</text>
  <rect x="70" y="384" width="480" height="24" class="f-mut" stroke="currentColor"/> <text x="310" y="400" fill="currentColor" text-anchor="middle" font-size="12">reserved</text> <text x="565" y="400" fill="currentColor" font-family="monospace" font-size="12">00000000</text>
  <text x="12" y="424" fill="currentColor" font-family="monospace" font-size="12">0x3C</text> <rect x="70" y="408" width="120" height="24" class="f-mut" stroke="currentColor"/> <text x="130" y="424" fill="currentColor" text-anchor="middle" font-size="12">Max_Lat</text>
  <rect x="190" y="408" width="120" height="24" class="f-mut" stroke="currentColor"/> <text x="250" y="424" fill="currentColor" text-anchor="middle" font-size="12">Min_Gnt</text> <rect x="310" y="408" width="120" height="24" class="f-acc" stroke="currentColor"/>
  <text x="370" y="424" fill="currentColor" text-anchor="middle" font-size="12">Int Pin</text> <rect x="430" y="408" width="120" height="24" class="f-mut" stroke="currentColor"/> <text x="490" y="424" fill="currentColor" text-anchor="middle" font-size="12">Int Line</text>
  <text x="565" y="424" fill="currentColor" font-family="monospace" font-size="12">00 | 00 | 01 | ff</text> <text x="10" y="452" fill="currentColor" font-size="12">덤프 "00: f4 1a 42 10 16 04 10 00" = byte 0 부터 → VID 0x1af4, DID 0x1042, Command 0x0416 (LE)</text>
  <text x="10" y="470" fill="currentColor" font-size="12">0x40~0xFF = capability 영역 (PCIE-B04), 0x100~0xFFF = PCIe 확장 영역. 이 레슨은 0x3F 까지</text>
</svg>
```

이 레슨이 쓰는 데이터 (전부 레슨 폴더에 원본 저장):

| 파일 | 출처 | 내용 |
|---|---|---|
| `capture_lima_lspci_xxx_all.txt` | Lima VM (Apple Virtualization.framework, Ubuntu 26.04, pciutils 3.14), `sudo lspci -xxx` | 장치 10개, 256B 씩. 00:00.0 host bridge (Mem- BusMaster-), 00:06.0 virtio-blk, 00:0c.0 xHCI |
| `capture_lima_lspci_vvv_xxx_00_06_0.txt` | 같은 VM, `sudo lspci -vvv -xxx -s 00:06.0` | 정답 비교용 `Control:` / `Status:` / `Region` 줄 + 탭 줄 섞인 덤프 |
| `capture_lima_lspci_xxx_nonroot_00_06_0.txt` | 같은 VM, 일반 사용자 `lspci -xxx` | 64B 만 나옴 |
| `capture_lima_lspci_n.txt`, `capture_lima_lspci_nn.txt` | 같은 VM, `lspci -n` / `-nn` | 장치 목록 정답 |
| `capture_qemu_guest_sysfs_config_od.txt` | Lima VM 안의 QEMU 게스트 (virt + `pcie-root-port` + `nvme`, Linux 6.18.55, nvme.ko 로드 전), sysfs `config` 를 `od -tx1` | 00:00.0 host bridge, 00:01.0 virtio-net, 00:02.0 root port(Type 1, 4KB), 01:00.0 NVMe(4KB) |
| `capture_qemu_guest_lspci_xxxx_fmt.txt` | 위 od 출력에 offset 만 붙여 lspci -xxxx 형식으로 변환 (바이트 그대로) | 파서 입력 |
| `capture_qemu_nvme_probe_dmesg.txt` | 같은 QEMU, nvme-core.ko + nvme.ko insmod 전후 | Command `00 00` → `06 04`, dmesg "enabling device (0000 -> 0002)" |

QEMU 장치 값은 **에뮬레이션 값**이고 VM 장치는 Apple 하이퍼바이저가 만든 가상 장치다. 진짜 SSD 처럼 꾸민 덤프는 손으로 만든 **가상 덤프**(VID 0xC0DE 는 지어낸 값)라고 매번 표시한다.

### Part A — 바이트 조립, 덤프 한 줄, Type 0 디코드

1. `cfg_rd16/32(cfg, off)`: little-endian 바이트 조립 (포인터 캐스트 금지 — 정렬·엔디언 둘 다 안전하게).
2. `parse_dump_line(line, &off, row[16])`: `"10: 04 00 02 80 …"` → offset 0x10 + 16바이트. 2~4자리 offset, 정확히 16개, 비 hex·15/17개·콜론 없음 → `CFG_ERR_SYNTAX`.
3. `pci_decode(cfg, n, &h)`: 0x00–0x0F 공통 필드 → VID 0xFFFF/0x0000 이면 `HDR_NO_DEVICE`, Header Type 의 layout(bit 6:0) 1 → `HDR_TYPE1` ("Type 1, PCIE-B07 참고" 하고 멈춤), 2 → `HDR_TYPE2`, 3+ → `HDR_BAD_TYPE`. Type 0 이면 BAR 6개(raw 값 + 종류 + 주소, 64비트 짝 합치기, **사이징은 안 함 — PCIE-B05**), Subsystem, ROM, Cap Pointer, Interrupt Line/Pin.
4. `pci_cmd_str` / `pci_sts_str`: lspci -vvv 의 `Control:` / `Status:` 뒤 문자열과 **글자 그대로** 같은 출력 (실측 줄로 테스트).
5. 테스트: 실측 VM 00:06.0 (64비트 BAR0 = 0x380020000, lspci `Region` 줄과 일치), 실측 QEMU virtio-net (IO BAR + 4GB 위 prefetchable), 실측 QEMU NVMe 4KB, 가상 SSD, multi-function NIC, BAR 타입 경계값.

### Part B — 깨진 입력과 진단

- `cfg_parse_text`: 빈 입력 `EMPTY`, 10줄만 `TRUNC`(n = 160), 비 hex / 15바이트 줄 / 잘못된 BDF(`00:20.0`, `.8`) `SYNTAX`(줄 번호), 줄 누락·중복 `OFFSET`, `1000:` 줄 `RANGE`, 4KB 를 256B 버퍼에 `CAP`. CRLF·대문자 hex 는 허용. 모든 접두사(prefix) 914개 전수 + 무작위 변형 3000개 + 무작위 왕복 200개. 텍스트는 버퍼 **끝**에 붙여 넣어 ASan 이 범위 밖 읽기를 잡게 한다.
- `pci_diagnose(&h)` → 비트마스크: Mem-, BusMaster-, (IO BAR 가 있는데) I/O-, DisINTx+, Status 에러 비트 6개, Cap+ 인데 포인터 < 0x40, 4KB 인데 Cap-, BAR 타입 이상, Interrupt Pin > 4. 보고서는 비트마다 정해진 문장을 출력한다.

### Part C — 여러 장치와 probe

- `lspci_scan`: `lspci -xxx` 전체 출력(장치 여러 개, 사이 빈 줄 있거나 없거나, 256B/4KB 섞임)을 차례로 읽어 요약 표 → 실측 `lspci -nn` 값과 비교, class 0x010802 로 NVMe 찾기 (Linux nvme 드라이버가 실제로 class 로 매칭한다).
- `probe_enable(cfg, n, MEM|MASTER)`: `pci_enable_device_mem()` + `pci_set_master()` 에 해당. Command 를 **16비트** RMW 로 켜고 Status 에러 비트는 보존. 32비트 RMW 버그(`probe_enable_buggy`)가 `<MAbort` 증거를 지우는 것을 재현.

## 2. Problem (English)

> **Decode a PCI Type 0 configuration header from `lspci` hex output.**
>
> Input is the text printed by `lspci -xxx` (256 bytes) or `lspci -xxxx` (4096 bytes) for one function, optionally
> preceded by its title line (`00:06.0 Mass storage controller: …`) and `-v` lines that start with a tab.
> Each dump line is `<hex offset>: ` followed by exactly 16 space-separated hex bytes.
>
> ```c
> int cfg_parse_text(const char *txt, size_t len, uint8_t *cfg, size_t cap, cfg_info *info);
>     // CFG_OK, or EMPTY / SYNTAX / OFFSET / TRUNC (not 256 or 4096 bytes) / RANGE / CAP; info->line = failing line
> int pci_decode(const uint8_t *cfg, size_t n, pci_t0 *h);
>     // HDR_OK (Type 0), HDR_NO_DEVICE (VID 0xFFFF/0x0000), HDR_TYPE1 (stop, bridge), HDR_TYPE2, HDR_BAD_TYPE
> uint32_t pci_diagnose(const pci_t0 *h);   // bitmask of findings
> int pci_report(const uint8_t *cfg, size_t n, const pci_bdf *bdf, char *out, size_t cap);
> ```
>
> Constraints: no dynamic allocation; never read past `len` (input is not NUL-terminated); config space is a
> little-endian byte array — assemble fields from bytes, no unaligned casts. BARs: report raw value, kind
> (IO / Mem32 / Mem64 / prefetchable) and base address; a 64-bit BAR consumes the next BAR as its upper half. Do not size BARs.
>
> Example: for `00:06.0` from a Linux VM the report must contain `Control: … I/O- Mem+ BusMaster+ … DisINTx+`
> exactly as lspci prints it and `Memory at 380020000 (64-bit, non-prefetchable)`. If Memory Space is disabled print
> "BAR decode off, MMIO reads return UR / 0xFFFFFFFF"; if Bus Master is disabled print "device cannot DMA or send MSI".
>
> Follow-ups: (1) Why does a read of VID return 0xFFFF for an empty slot? (2) Your driver sets Bus Master with a
> 32-bit read-modify-write of offset 0x04 — what did you just destroy? (3) The dump says `Header Type 0x80` — what does
> the enumerator have to do next? (4) The host sees `LnkSta x2` but the card is x4 — is that in this header? (No — PCIE-B04.)

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 10개 함수: A1 `cfg_rd16/32`, A2 `parse_dump_line`, A3 `pci_decode`, A4 `pci_cmd_str/pci_sts_str`, B1 `cfg_parse_text`, B2 `pci_diagnose`, C1 `lspci_scan`, C2 `probe_enable`. 줄 도우미(`next_line`, `parse_bdf_header`), class 이름 표, 플래그 이름 표, 진단 문장 표, `pci_report`, W1C 쓰기 모델(`cfg_wr16/32`), 버그 재현용 `probe_enable_buggy`, 실측 덤프 문자열은 주어진다. 스텁은 0 / 오류 코드를 돌려줘 멈추거나 죽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:708  cfg_rd16(b, 0) == 0x1af4 && cfg_rd16(b, 2) == 0x1042 && cfg_rd16(b, 4) == 0x0416
FAIL starter.c:709  cfg_rd32(b, 0) == 0x10421af4u && cfg_rd32(b, 4) == 0x00100416u
…
--- report: 가상 NVMe SSD (손으로 만든 덤프) ---
VID 0x0000 → 장치 없음 (0x0000 은 유효한 VID 가 아님)
prefix sweep: 914 prefixes, OK=0, mismatches=3
fuzz: 3000 mutants → OK 0, rejected 3000, bad 0
…
10/81 passed
```

(exit 1.) 추천 순서: A1 → A2 → B1 (파서가 돌아야 실측 테스트가 산다) → A3 → A4 → B2 → C1 → C2.

## 4. 힌트

<details><summary>힌트 1 — 바이트 조립과 오프셋 표</summary>

`cfg[off] | cfg[off+1] << 8` (32비트는 4개, `uint32_t` 로 캐스트한 뒤 시프트 — `uint8_t << 24` 는 int 로 승격돼 부호 비트에 닿는다). 오프셋은 `pci_regs.h` 이름 그대로: VID 0x00, DID 0x02, COMMAND 0x04, STATUS 0x06, REVISION 0x08, PROG 0x09, CLASS_DEVICE 0x0A(sub) / 0x0B(base), HEADER_TYPE 0x0E, BAR0 0x10 + 4i, SUBSYSTEM 0x2C/0x2E, ROM 0x30, CAP 0x34, INT LINE/PIN 0x3C/0x3D.

</details>

<details><summary>힌트 2 — 제목 줄과 덤프 줄은 3번째 글자로 갈린다</summary>

`00:06.0 Mass…` 와 `00: f4 1a …` 는 둘 다 `00:` 로 시작한다. 제목은 `hh:hh.d` (index 2 가 ':' 이고 5 가 '.'), 덤프는 index 2 가 ':' 이고 3 이 ' '. 3자리 offset `100:` 은 index 3 이 ':'. 도메인이 붙은 `0000:00:02.0` 도 있다. 이 판별은 `parse_bdf_header` 로 주어진다.

</details>

<details><summary>힌트 3 — 블록 파서의 상태는 "n(채운 바이트 수)" 하나면 된다</summary>

덤프 줄의 offset 은 반드시 `n` 과 같아야 한다 — 이 한 비교로 누락·중복·순서 오류가 다 잡힌다. `n + 16 > 4096` 이면 RANGE, `n + 16 > cap` 이면 CAP. 끝나면 n 이 256 이나 4096 이 아니면 TRUNC. 블록의 끝은 (시작한 뒤의) 빈 줄, 다음 제목 줄(그 줄은 먹지 않는다 — `consumed` 를 그 줄 시작으로), 또는 입력 끝.

</details>

<details><summary>힌트 4 — BAR 루프는 "앞 BAR 가 64비트였나"를 본다</summary>

raw == 0 → unused. bit0 = 1 → IO, 주소 `raw & ~3`. 아니면 메모리: 주소 `raw & ~0xF`, bit3 = prefetch, bit 2:1 = 00(32) / 10(64) / 01(1MB 아래) / 11(reserved). 64비트면 다음 BAR 를 읽어 `<< 32` 로 합치고 그 BAR 를 `BAR_MEM64_HI` 로 표시, 루프는 그 칸을 건너뛴다. BAR5 가 64비트면 위 절반이 없다 → BAD.

</details>

<details><summary>힌트 5 — 진단은 "꺼진 것"과 "켜진 에러" 두 종류</summary>

Command 는 0 인 비트가 문제(Mem-, BusMaster-, IO BAR 가 있을 때의 I/O-), Status 는 1 인 비트가 문제(0x0100, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000). Cap Pointer 는 하위 2비트를 버리고(`& ~3` — Linux 도 `ALIGN_DOWN(pos, 4)`) 0x40 미만이면 깨진 포인터. 4KB 덤프인데 Cap- 이면 PCIe 장치로서 말이 안 된다 (PCIe Capability 필수).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 |
|---|---|
| 1. 입력 계약 | 256B 인가 4KB 인가(일반 사용자면 64B), 텍스트가 NUL 종료인가, 장치 하나인가 여럿인가, 잘린 입력은 거절 |
| 2. 엔디언 | config space 는 little-endian. 바이트 조립, 포인터 캐스트 금지 |
| 3. 먼저 볼 3개 | VID(0xFFFF 면 장치 없음) → Header Type(Type 0 인가, MF 인가) → Class Code(무슨 장치인가) |
| 4. 상태 | Command(Mem/BM/IO/INTx), Status(Cap, 에러 비트 6개 — RW1C) |
| 5. 자원 | BAR raw → 종류·주소 (크기는 사이징, B05), Cap Pointer(B04), INTx pin |
| 6. 진단 문장 | Mem- → MMIO UR/all-ones, BM- → DMA/MSI 불가, 에러 비트 → 어느 쪽이 UR/CA 를 받았나 |

### Part A — 바이트에서 필드로: 00:06.0 추적

실측 첫 줄 `00: f4 1a 42 10 16 04 10 00 01 00 80 01 00 40 00 00` 을 바이트 번호대로 읽는다 (Linux `include/uapi/linux/pci_regs.h` 기준 이름):

| offset | 바이트 | 조립 | 필드 | 값의 뜻 |
|---|---|---|---|---|
| 0x00 | f4 1a | 0x1af4 | `PCI_VENDOR_ID` | Red Hat (virtio) |
| 0x02 | 42 10 | 0x1042 | `PCI_DEVICE_ID` | virtio 1.0 block (0x1040 + 장치 종류 2) |
| 0x04 | 16 04 | 0x0416 | `PCI_COMMAND` | Mem+ BusMaster+ MemWINV+ DisINTx+ |
| 0x06 | 10 00 | 0x0010 | `PCI_STATUS` | Cap+ 만 |
| 0x08 | 01 | — | `PCI_REVISION_ID` | rev 01 |
| 0x09 0x0A 0x0B | 00 80 01 | class 01:80:00 | prog-if, sub, **base** (뒤로 갈수록 큰 분류) | Mass storage, other |
| 0x0C 0x0D | 00 40 | — | Cache Line, Latency | lspci "Latency: 64" (0x40) |
| 0x0E | 00 | layout 0, bit7 0 | `PCI_HEADER_TYPE` | Type 0, single-function |

Class Code 는 0x08 의 DW 를 `>> 8` 해서 24비트로 보면 `0x018000` — 드라이버 매칭(`PCI_DEVICE_CLASS`)도 이 24비트 꼴을 쓴다. NVMe 는 **0x010802** (`pci_ids.h` 의 `PCI_CLASS_STORAGE_EXPRESS`, nvme 드라이버 테이블에 `PCI_DEVICE_CLASS(PCI_CLASS_STORAGE_EXPRESS, 0xffffff)` 로 실제로 있음 — 6.18.55 소스 확인). 그래서 **어떤 회사의 NVMe SSD 든 VID/DID 를 몰라도** 같은 드라이버가 붙는다. 우리 표에 넣은 다른 예: 02:00 Ethernet, 06:00 host bridge, 06:04 PCI bridge, 0c:03:30 xHCI(VM 00:0c.0 실측), 12:00 processing accelerator.

Header Type 은 **두 필드**다: bit 6:0 = layout, bit 7 = multi-function. `hdr == 0` 으로 비교하면 0x80 인 멀티 펑션 장치(2포트 NIC 의 fn0)를 Type 0 이 아니라고 오판한다 (7. 실수 3). bit7 이 1 이면 열거기(PCIE-B09)가 fn1..7 도 읽어 봐야 한다.

```svg
<svg viewBox="0 0 760 390" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Command (0x04, RW) 와 Status (0x06, 에러 비트 = RW1C) — Linux pci_regs.h 값</text> <text x="4" y="40" fill="currentColor" font-weight="bold" font-size="13">Command</text>
  <g fill="currentColor" font-size="12"><text x="8" y="56">bit</text><text x="48" y="56">mask</text><text x="104" y="56">이름</text><text x="304" y="56" text-anchor="middle">0416</text><text x="349" y="56" text-anchor="middle">0000</text></g>
  <rect x="4" y="62" width="370" height="19" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="8" y="76" fill="currentColor" font-family="monospace">0</text> <text x="48" y="76" fill="currentColor" font-family="monospace">0x0001</text>
  <text x="104" y="76" fill="currentColor">I/O Space</text> <text x="304" y="76" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="349" y="76" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="4" y="81" width="370" height="19" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="8" y="95" fill="currentColor" font-family="monospace">1</text> <text x="48" y="95" fill="currentColor" font-family="monospace">0x0002</text>
  <text x="104" y="95" fill="currentColor">Memory Space</text> <text x="304" y="95" fill="currentColor" font-family="monospace" text-anchor="middle">1</text> <text x="349" y="95" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="4" y="100" width="370" height="19" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="8" y="114" fill="currentColor" font-family="monospace">2</text> <text x="48" y="114" fill="currentColor" font-family="monospace">0x0004</text>
  <text x="104" y="114" fill="currentColor">Bus Master</text> <text x="304" y="114" fill="currentColor" font-family="monospace" text-anchor="middle">1</text> <text x="349" y="114" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="4" y="119" width="370" height="19" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="8" y="133" fill="currentColor" font-family="monospace">3</text> <text x="48" y="133" fill="currentColor" font-family="monospace">0x0008</text>
  <text x="104" y="133" fill="currentColor">Special Cycles</text> <text x="304" y="133" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="349" y="133" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="4" y="138" width="370" height="19" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="8" y="152" fill="currentColor" font-family="monospace">4</text> <text x="48" y="152" fill="currentColor" font-family="monospace">0x0010</text>
  <text x="104" y="152" fill="currentColor">Mem Write &amp; Invalidate</text> <text x="304" y="152" fill="currentColor" font-family="monospace" text-anchor="middle">1</text> <text x="349" y="152" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="4" y="157" width="370" height="19" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="8" y="171" fill="currentColor" font-family="monospace">5</text> <text x="48" y="171" fill="currentColor" font-family="monospace">0x0020</text>
  <text x="104" y="171" fill="currentColor">VGA Palette Snoop</text> <text x="304" y="171" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="349" y="171" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="4" y="176" width="370" height="19" fill="none" stroke="currentColor" stroke-width="0.5"/> <text x="8" y="190" fill="currentColor" font-family="monospace">6</text> <text x="48" y="190" fill="currentColor" font-family="monospace">0x0040</text>
  <text x="104" y="190" fill="currentColor">Parity Error Response</text> <text x="304" y="190" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="349" y="190" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="4" y="195" width="370" height="19" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="8" y="209" fill="currentColor" font-family="monospace">7</text> <text x="48" y="209" fill="currentColor" font-family="monospace">0x0080</text>
  <text x="104" y="209" fill="currentColor">Stepping (옛 PCI)</text> <text x="304" y="209" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="349" y="209" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="4" y="214" width="370" height="19" fill="none" stroke="currentColor" stroke-width="0.5"/> <text x="8" y="228" fill="currentColor" font-family="monospace">8</text> <text x="48" y="228" fill="currentColor" font-family="monospace">0x0100</text>
  <text x="104" y="228" fill="currentColor">SERR# Enable</text> <text x="304" y="228" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="349" y="228" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="4" y="233" width="370" height="19" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="8" y="247" fill="currentColor" font-family="monospace">9</text> <text x="48" y="247" fill="currentColor" font-family="monospace">0x0200</text>
  <text x="104" y="247" fill="currentColor">Fast Back-to-Back</text> <text x="304" y="247" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="349" y="247" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="4" y="252" width="370" height="19" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="8" y="266" fill="currentColor" font-family="monospace">10</text> <text x="48" y="266" fill="currentColor" font-family="monospace">0x0400</text>
  <text x="104" y="266" fill="currentColor">INTx Disable</text> <text x="304" y="266" fill="currentColor" font-family="monospace" text-anchor="middle">1</text> <text x="349" y="266" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="4" y="271" width="370" height="19" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="8" y="285" fill="currentColor" font-family="monospace">15:11</text> <text x="48" y="285" fill="currentColor" font-family="monospace">—</text>
  <text x="104" y="285" fill="currentColor">reserved</text> <text x="304" y="285" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="349" y="285" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="384" y="40" fill="currentColor" font-weight="bold" font-size="13">Status</text>
  <g fill="currentColor" font-size="12"><text x="388" y="56">bit</text><text x="428" y="56">mask</text><text x="484" y="56">이름</text><text x="684" y="56" text-anchor="middle">0010</text><text x="729" y="56" text-anchor="middle">2010</text></g>
  <rect x="384" y="62" width="370" height="19" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="76" fill="currentColor" font-family="monospace">0</text> <text x="428" y="76" fill="currentColor" font-family="monospace">0x0001</text>
  <text x="484" y="76" fill="currentColor">Immediate Readiness</text> <text x="684" y="76" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="729" y="76" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="384" y="81" width="370" height="19" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="95" fill="currentColor" font-family="monospace">2:1</text> <text x="428" y="95" fill="currentColor" font-family="monospace">—</text>
  <text x="484" y="95" fill="currentColor">reserved</text> <text x="684" y="95" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="729" y="95" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="384" y="100" width="370" height="19" fill="none" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="114" fill="currentColor" font-family="monospace">3</text> <text x="428" y="114" fill="currentColor" font-family="monospace">0x0008</text>
  <text x="484" y="114" fill="currentColor">Interrupt Status (RO)</text> <text x="684" y="114" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="729" y="114" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="384" y="119" width="370" height="19" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="133" fill="currentColor" font-family="monospace">4</text> <text x="428" y="133" fill="currentColor" font-family="monospace">0x0010</text>
  <text x="484" y="133" fill="currentColor">Capabilities List (RO)</text> <text x="684" y="133" fill="currentColor" font-family="monospace" text-anchor="middle">1</text> <text x="729" y="133" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <rect x="384" y="138" width="370" height="19" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="152" fill="currentColor" font-family="monospace">5</text> <text x="428" y="152" fill="currentColor" font-family="monospace">0x0020</text>
  <text x="484" y="152" fill="currentColor">66 MHz (옛 PCI)</text> <text x="684" y="152" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="729" y="152" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="384" y="157" width="370" height="19" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="171" fill="currentColor" font-family="monospace">6</text> <text x="428" y="171" fill="currentColor" font-family="monospace">0x0040</text>
  <text x="484" y="171" fill="currentColor">UDF (폐지)</text> <text x="684" y="171" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="729" y="171" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="384" y="176" width="370" height="19" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="190" fill="currentColor" font-family="monospace">7</text> <text x="428" y="190" fill="currentColor" font-family="monospace">0x0080</text>
  <text x="484" y="190" fill="currentColor">Fast B2B (옛 PCI)</text> <text x="684" y="190" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="729" y="190" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="384" y="195" width="370" height="19" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="209" fill="currentColor" font-family="monospace">8</text> <text x="428" y="209" fill="currentColor" font-family="monospace">0x0100</text>
  <text x="484" y="209" fill="currentColor">Master Data Parity Err</text> <text x="684" y="209" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="729" y="209" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="384" y="214" width="370" height="19" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="228" fill="currentColor" font-family="monospace">10:9</text> <text x="428" y="228" fill="currentColor" font-family="monospace">0x0600</text>
  <text x="484" y="228" fill="currentColor">DEVSEL timing</text> <text x="684" y="228" fill="currentColor" font-family="monospace" text-anchor="middle">00</text> <text x="729" y="228" fill="currentColor" font-family="monospace" text-anchor="middle">00</text>
  <rect x="384" y="233" width="370" height="19" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="247" fill="currentColor" font-family="monospace">11</text> <text x="428" y="247" fill="currentColor" font-family="monospace">0x0800</text>
  <text x="484" y="247" fill="currentColor">Signaled Target Abort</text> <text x="684" y="247" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="729" y="247" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="384" y="252" width="370" height="19" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="266" fill="currentColor" font-family="monospace">12</text> <text x="428" y="266" fill="currentColor" font-family="monospace">0x1000</text>
  <text x="484" y="266" fill="currentColor">Received Target Abort</text> <text x="684" y="266" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="729" y="266" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="384" y="271" width="370" height="19" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="285" fill="currentColor" font-family="monospace">13</text> <text x="428" y="285" fill="currentColor" font-family="monospace">0x2000</text>
  <text x="484" y="285" fill="currentColor">Received Master Abort</text> <text x="684" y="285" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="729" y="285" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <rect x="384" y="290" width="370" height="19" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="304" fill="currentColor" font-family="monospace">14</text> <text x="428" y="304" fill="currentColor" font-family="monospace">0x4000</text>
  <text x="484" y="304" fill="currentColor">Signaled System Error</text> <text x="684" y="304" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="729" y="304" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="384" y="309" width="370" height="19" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="323" fill="currentColor" font-family="monospace">15</text> <text x="428" y="323" fill="currentColor" font-family="monospace">0x8000</text>
  <text x="484" y="323" fill="currentColor">Detected Parity Error</text> <text x="684" y="323" fill="currentColor" font-family="monospace" text-anchor="middle">0</text> <text x="729" y="323" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="10" y="348" fill="currentColor">Command: 0416 = VM 00:06.0 실측 (드라이버 붙음), 0000 = QEMU nvme 실측 (드라이버 로드 전)</text> <text x="10" y="368" fill="currentColor">Status: 0010 = VM 실측, 2010 = 가상 SSD (&lt;MAbort+). 경고색 6비트 = RW1C (BM-B04)</text>
</svg>
```

lspci 의 플래그 이름은 위 표 순서 그대로다 — 테스트가 실측 `lspci -vvv` 줄 두 개(00:06.0 과 00:00.0)를 `strcmp` 로 비교한다. `DEVSEL` 은 2비트 필드라 `fast/medium/slow/??` 로 찍는다 (PCIe 에서는 의미 없는 옛 PCI 비트들 — Stepping, 66MHz, FastB2B, DEVSEL — 은 0 으로 고정된다).

### Part A — BAR: raw 값만으로 알 수 있는 것

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b03ar" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="10" y="18" fill="currentColor" font-weight="bold">BAR 의 아래 비트 = 종류, 위 비트 = 주소 (사이징 = 크기 알아내기는 PCIE-B05)</text>
  <text x="10" y="64" fill="currentColor" font-size="12">Memory BAR</text> <text x="129.4" y="44" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">31</text> <text x="635.6" y="44" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">4</text>
  <text x="654.4" y="44" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">3</text> <text x="691.9" y="44" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">2:1</text>
  <text x="710.6" y="44" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="120.0" y="48" width="525.0" height="30" class="f-ok" stroke="currentColor"/>
  <text x="382.5" y="67" fill="currentColor" text-anchor="middle" font-size="12">Base Address [31:4] (16B 이상, 크기 정렬)</text> <rect x="645.0" y="48" width="18.8" height="30" class="f-acc" stroke="currentColor"/> <text x="654.4" y="67" fill="currentColor" text-anchor="middle" font-size="12">Pf</text>
  <rect x="663.8" y="48" width="37.5" height="30" class="f-acc" stroke="currentColor"/> <text x="682.5" y="67" fill="currentColor" text-anchor="middle" font-size="12">Type</text> <rect x="701.2" y="48" width="18.8" height="30" class="f-mut" stroke="currentColor"/>
  <text x="710.6" y="67" fill="currentColor" text-anchor="middle" font-size="12">0</text> <text x="10" y="114" fill="currentColor" font-size="12">I/O BAR</text> <text x="129.4" y="94" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">31</text>
  <text x="673.1" y="94" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">2</text> <text x="691.9" y="94" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">1</text>
  <text x="710.6" y="94" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0</text> <rect x="120.0" y="98" width="562.5" height="30" class="f-ok" stroke="currentColor"/>
  <text x="401.2" y="117" fill="currentColor" text-anchor="middle" font-size="12">Base Address [31:2]</text> <rect x="682.5" y="98" width="18.8" height="30" class="f-mut" stroke="currentColor"/> <text x="691.9" y="117" fill="currentColor" text-anchor="middle" font-size="12">r</text>
  <rect x="701.2" y="98" width="18.8" height="30" class="f-warn" stroke="currentColor"/> <text x="710.6" y="117" fill="currentColor" text-anchor="middle" font-size="12">1</text>
  <text x="120" y="150" fill="currentColor" font-size="12">Type: 00 = 32비트, 10 = 64비트 (다음 BAR = 상위 32비트), 01 = 1MB 아래(폐지), 11 = reserved</text> <text x="190" y="178" fill="currentColor" font-size="12">아래 절반 (BAR n)</text><text x="330" y="178" fill="currentColor" font-size="12">위 절반 (BAR n+1)</text>
  <text x="10" y="205" fill="currentColor" font-size="12">VM 00:06.0 BAR0/1</text> <rect x="190" y="186" width="130" height="28" class="f-ok" stroke="currentColor"/> <text x="255" y="204" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0x80020004</text>
  <rect x="330" y="186" width="130" height="28" class="f-mut" stroke="currentColor"/> <text x="395" y="204" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0x00000003</text> <line x1="465" y1="200" x2="500" y2="200" stroke="currentColor" marker-end="url(#b03ar)"/>
  <text x="506" y="204" fill="currentColor" font-family="monospace" font-size="12">0x0000000380020000</text> <text x="190" y="232" fill="currentColor" font-size="12">low 4비트 0100: 메모리, 64비트, non-prefetch (lspci 실측 일치)</text> <text x="10" y="269" fill="currentColor" font-size="12">QEMU net BAR4/5</text>
  <rect x="190" y="250" width="130" height="28" class="f-ok" stroke="currentColor"/> <text x="255" y="268" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0x0020000C</text> <rect x="330" y="250" width="130" height="28" class="f-mut" stroke="currentColor"/>
  <text x="395" y="268" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0x00000080</text> <line x1="465" y1="264" x2="500" y2="264" stroke="currentColor" marker-end="url(#b03ar)"/>
  <text x="506" y="268" fill="currentColor" font-family="monospace" font-size="12">0x0000008000200000</text> <text x="190" y="296" fill="currentColor" font-size="12">low 4비트 1100: 64비트 + prefetchable, 4GB 위 (QEMU virt high MMIO)</text>
</svg>
```

VM 00:06.0 의 BAR1 = `0x00000003` 을 단독 BAR 로 읽으면 bit0 = 1 이라 "IO 포트 0x0" 이 된다 — 완전히 틀린 해석이다. 앞 BAR0 의 type 비트가 10(64비트)이므로 BAR1 은 **주소의 위 32비트**일 뿐이다 (lspci 도 Region 1 을 찍지 않는다). 합친 주소 `0x3_8002_0000` 은 lspci 실측 `Memory at 380020000 (64-bit, non-prefetchable)` 과 같다. raw 만으로는 **크기**를 모른다 — all-1s 쓰기로 사이징해야 한다(PCIE-B05). 그래서 raw 0 도 "미구현" 과 "아직 주소 안 줌" 을 구분할 수 없다고 보고서에 쓴다.

NVMe 관점: BAR0/1 = 컨트롤러 레지스터(CAP/CC/CSTS/doorbell — PCIE-B12)가 사는 64비트 non-prefetchable 메모리. 가상 SSD 덤프의 BAR2 IO(`0x0000E001`)는 NVMe 의 선택적 Index/Data Pair 자리를 흉내 낸 것인데(스펙 확인 권장), Linux nvme 는 `pci_enable_device_mem()` 으로 **메모리만** 켜므로 I/O- 가 정상이다 — 진단 `IO_OFF` 는 "IO BAR 를 쓸 드라이버라면 문제" 로 읽는다.

### Part B — 파서 문법과 오류

| 줄 종류 | 판별 | 처리 |
|---|---|---|
| 빈 줄 (공백·`\r` 만) | 길이 0 (끝 공백은 `next_line` 이 잘라 줌) | 시작 전이면 건너뜀, 시작 후면 블록 끝 |
| 제목 | `[dddd:]bb:dd.f` + (끝 또는 공백) | 시작 전이면 BDF 저장, 시작 후면 블록 끝(이 줄은 다음 블록 것). dev > 0x1f, fn > 7 → SYNTAX |
| `-v` 정보 줄 | 탭으로 시작, 아직 덤프 전 | 건너뜀 (`lspci -vvv -xxx` 실측 통과) |
| 덤프 | `XX[X[X]]:` + `" hh"` × 16 | offset == n, n+16 ≤ 4096, n+16 ≤ cap 이면 복사 |
| 그 밖 | — | SYNTAX + 줄 번호 |

결과 코드: `CFG_ERR_ARG(-1) EMPTY(-2) SYNTAX(-3) OFFSET(-4) TRUNC(-5) RANGE(-6) CAP(-7)`. 테스트 값: 10줄만 → TRUNC n = 160, 0x10 줄의 `g` → SYNTAX line 3, 15바이트 줄 → SYNTAX line 3, 0x20 줄 빠짐 → OFFSET line 4, `1000:` → RANGE line 258 (제목 1 + 256줄 + 1), 4KB 를 cap 256 에 → CAP line 18.

**접두사 전수 검사**: VM 00:06.0 텍스트(913바이트)의 길이 0..913 접두사 914개를 모두 넣어 "마지막 바이트 `00` 까지 들어 있으면 OK, 아니면 오류" 를 확인 — 실측 OK 3개(끝 `00`, `\n`, 빈 줄 `\n`), 불일치 0. 각 접두사는 큰 버퍼의 **끝**에 붙여 넣어서, 파서가 len 을 1바이트라도 넘겨 읽으면 ASan 이 stack/global-buffer-overflow 로 잡는다. 무작위 변형(1~3글자를 `0-9a-fg :.\n\t\rX` 로 바꾸고 1/4 은 길이도 자름) 3000개: OK 567, 거절 2433, 이상 0 — OK 인 것은 전부 256/4096 바이트였다(hex 숫자가 다른 hex 숫자로 바뀐 경우 등).

### Part B — 진단 표 (PCIe 에서의 뜻)

| 비트 | 조건 | 출력 문장 핵심 | 먼저 확인할 것 |
|---|---|---|---|
| `MEM_OFF` | Command.Memory = 0 | BAR 디코드 안 됨, MMIO 접근 시 UR/0xFFFFFFFF | 드라이버가 붙었나(`lspci -k`), `pci_enable_device*` 호출, BAR 할당 실패 로그 |
| `BM_OFF` | Command.BusMaster = 0 | 장치가 DMA/MSI 못 보냄 | `pci_set_master` 호출, IOMMU 오류 후 커널이 끈 것인지 |
| `IO_OFF` | I/O = 0 이고 IO BAR 있음 | IO 포트 접근 응답 없음 | 그 드라이버가 IO 를 쓰는가 (NVMe 는 안 씀) |
| `INTX_OFF` | DisINTx = 1, pin 1..4 | MSI/MSI-X 를 쓰는 중일 것 | MSI-X Enable (PCIE-B04, PCIE-M10) |
| `RMA` 0x2000 | Received Master Abort | 이 장치의 요청이 UR 로 끝남 | 장치가 DMA 한 주소 (매핑 안 된 IOVA? PCIE-M15) |
| `RTA` 0x1000 | Received Target Abort | 요청이 CA 완료로 돌아옴 | 상대 completer 의 오류 |
| `STA` 0x0800 | Signaled Target Abort | 이 장치가 CA 로 거절 | 장치 펌웨어가 어떤 요청을 거절했나 |
| `MDPE` 0x0100 | Master Data Parity Error | poisoned 완료 수신 / poisoned 쓰기 송신 | Parity Error Response 켜짐 여부, AER |
| `SSE` 0x4000 | Signaled System Error | ERR_FATAL/NONFATAL 송신 | AER 레지스터 (PCIE-M12) |
| `DPE` 0x8000 | Detected Parity Error | poisoned TLP 수신 | 데이터 경로·EP 비트 (PCIE-B10) |
| `CAP_BAD` / `NO_CAPS` / `BAR_BAD` / `PIN_BAD` | 구조 이상 | 덤프·장치 확인 | 덤프가 잘렸나, 링크가 죽어 0xFF 가 섞였나 |

PCIe 에서의 Status 비트 뜻은 PCIe Base Spec 의 "Status Register" 표(7.5.1 계열)를 기억으로 옮긴 것이다 — 세부 조건(예: MDPE 는 Parity Error Response 가 켜진 경우만)은 **스펙 확인 권장**. 에러 비트는 전부 **RW1C**: 0 을 쓰면 그대로, 1 을 쓰면 지워진다 (BM-B04). 로그를 먼저 남기고 지운다.

가상 SSD 덤프의 보고서 (solution 실제 출력):

```text
c0de:5501 rev 01  class 01:08:02 Non-Volatile memory controller: NVM Express (NVMe)
  Header  : 0x00 = Type 0, single-function
  Control : 0x0406  I/O- Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+
  Status  : 0x2010  Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort+ >SERR- <PERR- INTx-
  CLS/Lat : 0x00 / 0, BIST 0x00
  BAR0    : 0xfe600004  Memory at fe600000 (64-bit, non-prefetchable)
  BAR1    : 0x00000000  (BAR0 의 상위 32비트)
  BAR2    : 0x0000e001  I/O ports at e000
  BAR3    : 0x00000000  unused (0: 미구현 또는 미할당 — 구분은 사이징, PCIE-B05)
  BAR4    : 0x00000000  unused (0: 미구현 또는 미할당 — 구분은 사이징, PCIE-B05)
  BAR5    : 0x00000000  unused (0: 미구현 또는 미할당 — 구분은 사이징, PCIE-B05)
  ROM     : 0x00000000 (disabled)
  Subsys  : c0de:0001
  CapPtr  : 0x40 (순회는 PCIE-B04)
  INTx    : pin A, line 0x00
  진단    :
   - I/O- : IO BAR 가 있는데 IO Space 꺼짐 → IO 포트 접근에 응답 없음
   - DisINTx+ : INTx 꺼짐 → MSI/MSI-X 를 쓰는 중일 것 (PCIE-B04, PCIE-M10)
   - <MAbort+ : Received Master Abort — 이 장치의 요청이 UR 로 끝남 (잘못된 DMA 주소?)
   - Status 에러 비트는 RW1C: 1 을 써야 지워진다 (BM-B04). 로그 남긴 뒤 지울 것
```

### Part C — 스캔과 probe

`lspci_scan` 은 `cfg_parse_text` 를 `txt + pos` 에서 반복하고 `info.consumed` 만큼 전진한다. 블록이 빈 줄로 나뉘든(실측 lspci) 제목 줄로 바로 이어지든(변환 파일) 같은 코드로 된다. 4KB 버퍼 하나를 장치마다 재사용 — malloc 없음. 테스트: VM 3장치 → `lspci -nn` 실측 값(`[0600] 106b:1a05`, `[0180] 1af4:1042 (rev 01)`, `[0c03] 106b:1a06`)과 일치, QEMU 3장치(256B + Type 1 + 4KB) → class 0x010802 는 01:00.0, 중간 장치 손상 → SYNTAX 와 그 앞 1개.

드라이버 로드 전 QEMU NVMe 의 보고서 (실측 덤프):

```text
[01:00.0] 1b36:0010 rev 02  class 01:08:02 Non-Volatile memory controller: NVM Express (NVMe)
  Header  : 0x00 = Type 0, single-function
  Control : 0x0000  I/O- Mem- BusMaster- SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx-
  Status  : 0x0010  Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
  CLS/Lat : 0x00 / 0, BIST 0x00
  BAR0    : 0x10000004  Memory at 10000000 (64-bit, non-prefetchable)
  BAR1    : 0x00000000  (BAR0 의 상위 32비트)
  BAR2    : 0x00000000  unused (0: 미구현 또는 미할당 — 구분은 사이징, PCIE-B05)
  BAR3    : 0x00000000  unused (0: 미구현 또는 미할당 — 구분은 사이징, PCIE-B05)
  BAR4    : 0x00000000  unused (0: 미구현 또는 미할당 — 구분은 사이징, PCIE-B05)
  BAR5    : 0x00000000  unused (0: 미구현 또는 미할당 — 구분은 사이징, PCIE-B05)
  ROM     : 0x00000000 (disabled)
  Subsys  : 1af4:1100
  CapPtr  : 0x40 (순회는 PCIE-B04)
  INTx    : pin A, line 0x00
  Ext     : 4KB 덤프, 0x100 = 0x00000000 (확장 cap 순회는 PCIE-B04)
  진단    :
   - Mem- : Memory Space 꺼짐 → BAR 디코드 안 됨, MMIO 접근 시 UR/0xFFFFFFFF
   - BusMaster- : 장치가 DMA/MSI 못 보냄 (MSI 도 장치가 보내는 메모리 쓰기)
```

nvme.ko 를 넣으면 어떻게 되나 — 실제로 넣어 본 결과:

```svg
<svg viewBox="0 0 760 210" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="b03ar2" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">QEMU nvme 01:00.0 의 Command 레지스터: 열거 → 드라이버 probe (Linux 6.18.55)</text> <rect x="10" y="34" width="170" height="104" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="95" y="56" fill="currentColor" text-anchor="middle" font-weight="bold">열거 (커널 PCI 코어)</text> <text x="95" y="78" fill="currentColor" text-anchor="middle">BAR0 할당 0x10000000</text>
  <text x="95" y="102" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">Command 0x0000</text> <text x="95" y="126" fill="currentColor" text-anchor="middle">실측: before 00 00</text> <line x1="182" y1="86" x2="196" y2="86" stroke="currentColor" marker-end="url(#b03ar2)"/>
  <rect x="198" y="34" width="170" height="104" rx="6" class="f-ok" stroke="currentColor"/> <text x="283" y="56" fill="currentColor" text-anchor="middle" font-weight="bold">pci_enable_device_mem()</text> <text x="283" y="78" fill="currentColor" text-anchor="middle">Mem 비트만 켬</text>
  <text x="283" y="102" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">Command 0x0002</text> <text x="283" y="126" fill="currentColor" text-anchor="middle">dmesg: enabling device</text>
  <line x1="370" y1="86" x2="384" y2="86" stroke="currentColor" marker-end="url(#b03ar2)"/> <rect x="386" y="34" width="170" height="104" rx="6" class="f-ok" stroke="currentColor"/> <text x="471" y="56" fill="currentColor" text-anchor="middle" font-weight="bold">pci_set_master()</text>
  <text x="471" y="78" fill="currentColor" text-anchor="middle">Bus Master 켬</text> <text x="471" y="102" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">Command 0x0006</text> <text x="471" y="126" fill="currentColor" text-anchor="middle">(코드 근거)</text>
  <line x1="558" y1="86" x2="572" y2="86" stroke="currentColor" marker-end="url(#b03ar2)"/> <rect x="574" y="34" width="170" height="104" rx="6" class="f-acc" stroke="currentColor"/> <text x="659" y="56" fill="currentColor" text-anchor="middle" font-weight="bold">MSI-X 켜기</text>
  <text x="659" y="78" fill="currentColor" text-anchor="middle">pci_intx_for_msi(dev, 0)</text> <text x="659" y="102" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">Command 0x0406</text>
  <text x="659" y="126" fill="currentColor" text-anchor="middle">실측: after 06 04</text> <text x="10" y="164" fill="currentColor">dmesg 실측 "enabling device (0000 -&gt; 0002)" — 커널은 Command 를 16비트(word)로만 쓴다</text>
  <text x="10" y="186" fill="currentColor">이 레슨의 진단: 맨 왼쪽 = Mem- · BusMaster- 두 문장, 맨 오른쪽 = DisINTx+ 하나</text>
</svg>
```

`probe_enable` 은 왼쪽 두 칸(0x0000 → 0x0006)을 흉내 낸다. 핵심은 **16비트 쓰기**: 0x04 를 32비트로 읽어 OR 하고 다시 쓰면 위 16비트(= Status)에 읽은 값이 그대로 써지고, 1 이었던 에러 비트는 RW1C 라 **지워진다**. 테스트에서 Status 0x2010 → 버그 버전 후 0x0010 (`<MAbort` 증거 소실), 정상 버전 후 0x2010 유지. Linux 도 `pci_write_config_word(dev, PCI_COMMAND, cmd)` 로만 쓴다 (setup-res.c, pci.c 확인).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 81/81 passed, exit 0 (약 0.03 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
```

`solution.c` 실행 출력 중 보고서·테스트 외 줄 (실측 VM 00:06.0 보고서는 위 lspci 와 같은 값이라 생략):

```text
--- report: QEMU 00:02.0 (실측 root port) ---
[00:02.0] 1b36:000c rev 00  class 06:04:00 PCI bridge
  Header  : 0x01 = Type 1 (PCI-to-PCI bridge) → Type 1, PCIE-B07 참고 (여기서 멈춤)
prefix sweep: 914 prefixes, OK=3, mismatches=0
fuzz: 3000 mutants → OK 567, rejected 2433, bad 0
81/81 passed
```

## 7. 자주 하는 실수

**1. 포인터 캐스트로 읽는다.** `*(uint32_t *)&cfg[0x10]` 은 정렬 안 된 주소에서 Cortex-M0 같은 코어는 HardFault, 빅엔디언 호스트에서는 값이 뒤집힌다. 고친 코드: `cfg_rd32` 처럼 바이트를 `uint32_t` 로 캐스트한 뒤 시프트 (`c[off+3] << 24` 를 int 그대로 하면 부호 비트 UB).

**2. Class 바이트 순서를 뒤집는다.** 0x09 = prog-if, 0x0A = sub, 0x0B = base. 바이트 순서대로 찍으면 NVMe 가 `02:08:01` 이 된다. 고친 코드: `class24 = cfg[0x0b] << 16 | cfg[0x0a] << 8 | cfg[0x09]` (= `rd32(0x08) >> 8`).

**3. Header Type 을 마스크 없이 비교한다.** `if (cfg[0x0e] == 0)` 은 0x80(멀티 펑션 Type 0)을 놓치고, `== 1` 은 0x81 브리지를 놓친다. 고친 코드: `layout = hdr & 0x7f; mf = hdr & 0x80;`.

**4. 64비트 BAR 의 위 절반을 BAR 로 해석한다.** 실측 BAR1 = `0x00000003` → "IO 포트 0x0" 이라는 엉터리. 고친 코드: 앞 BAR 의 type 이 10 이면 이 칸은 `BAR_MEM64_HI` 로 표시하고 건너뛴다. BAR5 의 64비트는 BAD.

**5. Command 를 32비트 RMW 로 쓴다.** Status 의 RW1C 에러 비트를 지워 디버깅 증거를 없앤다 (테스트로 재현: 0x2010 → 0x0010). 고친 코드: 0x04 에 16비트 쓰기. 같은 이유로 Status 를 지울 때도 "읽은 값 그대로 다시 쓰기"(= 1 인 것 전부 지움)인지 "특정 비트만 1"인지 의도를 정한다 (연습 4).

**6. 텍스트 파서가 NUL 을 믿는다.** `sscanf`/`strtol` 은 NUL 까지 읽으므로 길이로 잘린 버퍼에서 범위를 넘는다. 그리고 `00:06.0` 제목 줄을 offset 0 덤프 줄로 오해하면 VID 자리에 쓰레기가 들어간다. 고친 코드: `(ptr, len)` 만 보는 `next_line` + 글자 위치로 제목/덤프 판별, 끝에 n 이 256/4096 인지 확인.

**7. VID 0xFFFF 를 장치로 디코드한다.** 빈 슬롯·링크 다운·아직 준비 안 된 장치(CRS)의 config 읽기는 all-1s 다. 그 위에서 "Header Type 0x7f = 알 수 없는 타입", "BAR 6개가 전부 IO" 같은 헛소리를 출력하게 된다. 고친 코드: VID 를 **가장 먼저** 보고 멈춘다.

## 8. 면접 Q&A

<details><summary>Q1. 빈 슬롯의 Vendor ID 는 왜 0xFFFF 로 읽히나요?</summary>

config 읽기에 응답하는 장치가 없으면 요청은 UR(Unsupported Request)로 끝나거나(PCIe), 마스터 어보트(옛 PCI)로 끝난다. 루트 컴플렉스는 이때 요청한 CPU 에게 all-1s 를 돌려준다 — 그래서 0xFFFF. 0xFFFF 는 PCI-SIG 가 절대 배정하지 않는 VID 라 "장치 없음" 표시로 안전하다. 꼬리질문: "읽은 DW 가 0xFFFFFFFF 인데 장치는 있다?" → 링크가 내려갔거나(서프라이즈 제거 — PCIE-A07), 장치가 리셋 중이거나, Mem- 인 BAR 를 MMIO 로 읽은 것. 리셋 직후에는 CRS(Configuration Request Retry Status)로 "잠시 후 다시" 를 받을 수 있다 — 세부는 스펙 확인 권장.

</details>

<details><summary>Q2. Type 0 과 Type 1 헤더의 차이는?</summary>

0x00–0x0F 는 같다. Type 0(엔드포인트)은 0x10–0x27 에 BAR 6개, 0x2C Subsystem ID, 0x30 ROM. Type 1(브리지·스위치 포트·루트 포트)은 BAR 2개 뒤 0x18–0x1A 에 Primary/Secondary/Subordinate Bus 번호, 그 뒤 IO/Memory/Prefetchable base·limit 창 — "어떤 주소·버스를 아래로 넘길지" 를 정한다. 실측 QEMU root port 00:02.0 의 0x18 = `00 01 01`(primary 0, secondary 1, subordinate 1) 이 01:00.0 NVMe 를 아래에 둔다. 디코드는 PCIE-B07, 버스 번호 배정은 PCIE-B09.

</details>

<details><summary>Q3. Memory Space 가 꺼진 장치의 BAR 주소를 CPU 가 읽으면?</summary>

장치는 그 주소를 자기 것으로 디코드하지 않으므로 Memory Read 는 UR 로 끝나고, 루트 컴플렉스가 CPU 에 all-1s(0xFFFFFFFF)를 준다(x86 에서 일반적). ARM SoC 는 RC 설정에 따라 all-1s 대신 외부 abort(SError/동기 abort)를 낼 수 있다 — 플랫폼마다 다름, 확인 권장. 그래서 드라이버에서 "레지스터가 전부 FFFFFFFF" 면 (1) Mem- (2) 링크 다운 (3) 장치가 D3 를 먼저 의심한다. 이 레슨의 실측 QEMU NVMe 는 드라이버 로드 전 Command 0x0000 이었고, nvme.ko 가 `enabling device (0000 -> 0002)` 를 찍은 뒤에야 BAR0 가 응답한다.

</details>

<details><summary>Q4. Bus Master 를 안 켜면 왜 MSI 도 안 되나요?</summary>

MSI/MSI-X 는 "장치가 특정 주소에 특정 값을 쓰는" **메모리 쓰기 TLP** 다. Bus Master Enable 은 장치가 요청(메모리 읽기/쓰기)을 **보내는** 것 자체를 막으므로, DMA 도 MSI 도 못 나간다. NVMe 로 치면 SQ 엔트리를 가져오는 읽기, 데이터 DMA, CQ 쓰기, 인터럽트 전부. 증상: 명령을 넣었는데 완료가 안 오고 타임아웃 (PCIE-A03). INTx 는 메시지(Assert_INTx)라 별개다.

</details>

<details><summary>Q5. SSD 의 Status 에 Received Master Abort 가 켜져 있습니다. 무엇을 의심하나요?</summary>

이 장치가 **요청자(requester)** 로서 보낸 요청이 UR 완료로 돌아왔다는 뜻 — SSD 라면 거의 DMA 읽기(호스트 메모리에서 SQ/PRP/데이터 가져오기)다. 의심 순서: (1) 호스트가 준 주소가 잘못(PRP 리스트 버그, PCIE-M04), (2) IOMMU 가 그 IOVA 를 매핑하지 않음(PCIE-M15 — dmesg 의 IOMMU fault 와 시간 맞추기), (3) 장치 펌웨어의 주소 계산 버그. AER 이 있으면 Uncorrectable Status 의 UR/CmpltAbrt 비트와 Header Log 로 실제 TLP 주소를 본다(PCIE-M12). 비트는 RW1C 이니 기록하고 1 을 써서 지운 뒤 재현한다.

</details>

<details><summary>Q6. 드라이버에서 Command 를 바꿀 때 왜 16비트로 써야 하나요?</summary>

0x04 의 DW 는 아래 16비트 Command(RW) + 위 16비트 Status(대부분 RO, 에러 비트 RW1C). 32비트 RMW 는 읽은 Status 를 그대로 다시 쓰므로 1 이던 에러 비트가 지워진다 — 테스트에서 0x2010 → 0x0010 재현. 하드웨어 입장에서는 TLP 의 First DW Byte Enable 이 `0011` 이냐 `1111` 이냐의 차이다(PCIE-B10). Linux 는 `pci_write_config_word` 를 쓴다. 꼬리질문 "Status 의 Cap 비트에 1 을 쓰면?" → RO 라 무시.

</details>

<details><summary>Q7. ARM 보드에서 Linux 는 config space 를 어떻게 찾나요?</summary>

ECAM 영역의 물리 주소를 DT 의 `pci-host-ecam-generic` 노드 `reg`(QEMU virt) 또는 ACPI MCFG 테이블에서 읽고, `bus << 20 | dev << 15 | fn << 12 | reg` 로 MMIO 접근한다. x86 의 0xCF8/0xCFC 포트 방식은 256B 만 닿는다. 계산과 모의 구현은 PCIE-B08. 이 레슨의 sysfs `config` 파일은 그 위에 커널이 얹은 인터페이스이고, root 가 아니면 64바이트만 준다(실측).

</details>

<details><summary>Q8. Interrupt Line 이 0xff 인데 lspci 는 "routed to IRQ 16" 이라고 합니다. 어느 쪽이 맞나요?</summary>

둘 다 맞다. 0x3C Interrupt Line 은 옛 x86 BIOS 가 IRQ 번호를 적어 두는 칸일 뿐 하드웨어 동작에 영향이 없고, 0xff 는 관례적으로 "모름/연결 없음". Linux(특히 ARM)는 이 칸을 쓰지 않고 DT 의 `interrupt-map` 이나 ACPI 로 INTx 를 매핑해 `pci_dev->irq` 에 넣으며, lspci 는 그 값을 sysfs 에서 읽어 보여 준다. 하드웨어 의미가 있는 것은 0x3D Interrupt Pin(1..4 = INTA..INTD, 0 = 없음)이다.

</details>

<details><summary>Q9. 링크가 x4 로 설계됐는데 x2 로 떴습니다. 이 헤더에서 보이나요?</summary>

안 보인다. 속도·폭은 PCI Express Capability 안의 Link Capabilities / Link Status 에 있다 — Cap Pointer(0x34)에서 리스트를 따라가 ID 0x10 을 찾아야 한다(PCIE-B04). 이 레슨에서 할 수 있는 건 "Cap+ 이고 포인터가 정상인가" 까지. 실리콘 검증 면접에서 이 질문이 나오면 "Type 0 헤더 → Cap list → PCIe cap → LnkCap vs LnkSta" 순서를 말하면 된다.

</details>

## 9. 추가 연습

### 연습 1 — 일반 사용자 lspci (64B) 받아 주기

root 가 아니면 `lspci -xxx` 가 4줄(64B)만 찍는다 (실측 `capture_lima_lspci_xxx_nonroot_00_06_0.txt`). 코어 파서는 그대로 두고 `allow64` 옵션으로 64B 를 받아 주는 래퍼를 짜라. 128B 는 여전히 TRUNC 여야 한다.

<details><summary>답</summary>

`cfg_parse_text` 가 TRUNC 를 주고 `info.n == 64` 이며 `allow64` 이면 OK 로 바꾼다. 64바이트면 Type 0 헤더 전체(0x00–0x3F)가 있으므로 `pci_decode` 는 그대로 돈다 — 실측에서 root 256B 덤프의 앞 64B 와 바이트가 같고 BAR0 주소, Cap Pointer 0x40 도 같았다. 단 capability 본문(0x40 이후)은 없으므로 보고서에 "Cap 순회 불가(64B)" 를 붙이는 게 맞다. 코어를 고치지 않고 감싸는 이유: 256/4096 만 받는 계약을 쓰는 다른 호출자(스캔)가 영향받지 않는다.

</details>

### 연습 2 — `lspci -n` 한 줄 만들기

스캔 요약으로 `00:06.0 0180: 1af4:1042 (rev 01)` 형식을 만들고 실측 `lspci -n` 3줄과 글자 그대로 비교하라. rev 가 0 이면 `(rev 00)` 을 찍지 않는다(실측 00:00.0, 00:0c.0).

<details><summary>답</summary>

`"%02x:%02x.%x %04x: %04x:%04x"` + rev ≠ 0 이면 `" (rev %02x)"`. class 는 24비트를 `>> 8` 한 base:sub 4자리. 실측과 3줄 모두 일치. 꼬리: lspci 는 prog-if 를 `-v` 에서만 `(prog-if 30 [XHCI])` 처럼 보여 준다.

</details>

### 연습 3 — 드라이버 로드 전/후 덤프 비교

두 덤프의 0x00–0x3F 를 레지스터 단위로 비교해 바뀐 것만 이름과 함께 찍고, Command 는 비트 증감(`+Mem`)까지 찍어라. QEMU NVMe 실측 before(Command `00 00`)/after(`06 04`)로 검증.

<details><summary>답</summary>

레지스터 표 `{offset, size, name}` 를 돌며 크기만큼 LE 로 읽어 비교. 실측 결과 `Command 0x0000 -> 0x0406 (+Mem +BusMaster +DisINTx)` 한 줄 — 다른 레지스터는 그대로(BAR 는 열거 때 이미 할당). bring-up 에서 "드라이버가 무엇을 바꿨나" 를 보는 가장 빠른 방법이고, 에러 재현 전후 비교(Status 비트가 켜졌나, BAR 가 0 이 됐나 — 리셋으로 날아갔다는 신호)에도 그대로 쓴다.

</details>

### 연습 4 — Status 에러 비트 지우기 (RW1C)

`clear_status_errors(cfg)`: 읽은 Status 에서 에러 비트(마스크 0xF900)만 골라 1 로 16비트 쓰기 → 지운 비트를 반환. Cap 비트와 Command 는 그대로여야 하고, 0 을 쓰면 아무것도 안 지워지며, 0xFFFF 를 쓰면 에러만 지워지는지(RO 비트 무시) 모델로 확인하라.

<details><summary>답</summary>

0xF910 → 반환 0xF900, Status 0x0010, Command 0x0006 유지. 두 번째 호출은 0. 0 쓰기 후에도 0x2010 그대로, 0xFFFF 쓰기 후 0x0010. 펌웨어 관례: 에러를 **읽고 로그한 뒤** 그 비트만 써서 지운다. 읽은 값을 통째로 다시 쓰는 것과 결과는 같아 보이지만, 읽은 뒤 새로 켜진 비트를 실수로 지우지 않으려면 "로그한 비트만" 쓰는 것이 안전하다.

</details>

### 연습 5 — pci_device_id 식 드라이버 매칭

`{vendor, device, class, class_mask, driver}` 표와 Linux `pci_match_one_device` 의 규칙(ANY = 무조건 일치, `((id.class ^ dev.class) & id.class_mask) == 0`)으로 nvme(class 0x010802, mask 0xffffff), xhci_hcd(0x0c0330), virtio-pci(vendor 0x1af4, 아무 device)를 매칭하라. 실측 `Kernel driver in use` 와 비교.

<details><summary>답</summary>

VM 00:06.0 → virtio-pci, 00:0c.0 → xhci_hcd (실측 `lspci -k` 와 같음), 00:00.0 → 없음, QEMU 01:00.0 → nvme (dmesg `nvme 0000:01:00.0`). class 를 0x010801 로 바꾸면 mask 가 prog-if 까지 보므로 매칭 안 됨. 실제 커널 표는 subvendor/subdevice 도 있고 테이블 순서·드라이버 우선순위가 있다 — 여기서는 vendor/device/class 만.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 20/20 passed
```

```text
lspci -n : 00:00.0 0600: 106b:1a05
lspci -n : 00:06.0 0180: 1af4:1042 (rev 01)
lspci -n : 00:0c.0 0c03: 106b:1a06
diff before/after nvme probe:
Command 0x0000 -> 0x0406 (+Mem +BusMaster +DisINTx)
20/20 passed
```

## 10. 핵심 정리

- config space = little-endian 바이트 배열. 256B(PCI) / 4KB(PCIe), 앞 64B 가 표준 헤더. 일반 사용자 lspci 는 64B 만.
- 먼저 VID(0x00): 0xFFFF = 장치 없음(all-1s). 다음 Header Type(0x0E): bit 6:0 layout(0 = 엔드포인트, 1 = 브리지 → PCIE-B07), bit 7 multi-function. 다음 Class(0x0B:0x0A:0x09), NVMe = 01:08:02.
- Command(0x04): bit0 IO, bit1 Mem, bit2 BusMaster, bit10 INTx Disable. Mem- → BAR 디코드 안 됨, MMIO 는 UR/0xFFFFFFFF. BM- → DMA·MSI 불가.
- Status(0x06): bit4 Cap List, 에러 6비트 0x0100/0x0800/0x1000/0x2000/0x4000/0x8000 은 RW1C. Command 는 16비트로 써야 에러 증거가 안 지워진다.
- BAR: bit0 = IO/Mem, Mem 의 2:1 = 32/64, bit3 = prefetch. 64비트는 다음 BAR 가 위 절반. raw 만으로는 크기 모름(PCIE-B05).
- Cap Pointer(0x34, `& ~3`, ≥ 0x40) → PCIE-B04 에서 PCIe cap / LnkSta. Interrupt Pin(0x3D) 은 하드웨어, Line(0x3C) 은 소프트웨어 메모.
- 파서는 (ptr, len) 만 믿고, offset == n 한 번의 비교로 누락·중복을, 끝의 n ∈ {256, 4096} 으로 잘림을 잡는다.

## 참고자료

- Linux `include/uapi/linux/pci_regs.h` — 이 레슨의 모든 오프셋·비트 (Lima VM `/usr/include/linux/pci_regs.h` 에서 확인).
- Linux 6.18.55 소스: `include/linux/pci_ids.h` (`PCI_CLASS_STORAGE_EXPRESS 0x010802`, `PCI_CLASS_SERIAL_USB_XHCI 0x0c0330`), `drivers/nvme/host/pci.c` (`PCI_DEVICE_CLASS(…EXPRESS, 0xffffff)`, `pci_enable_device_mem`, `pci_set_master`), `drivers/pci/setup-res.c` ("enabling device (%04x -> %04x)", `pci_write_config_word`), `drivers/pci/pci.h` (`PCI_FIND_NEXT_CAP`, `ALIGN_DOWN(pos, 4)`, TTL 48), `drivers/pci/msi/msi.c` (`pci_intx_for_msi`).
- PCI-SIG, *PCI Express Base Specification* — Type 0 Configuration Space Header, Command/Status Register (PCIe 의미의 Status 에러 비트). 문구는 기억 기반, 원문 확인 권장.
- PCI-SIG, *PCI Code and ID Assignment Specification* — Class Code 표 (01:08:02 NVMe 등).
- pciutils (`lspci` 3.14) man page — `-x/-xxx/-xxxx`, `-n`, `-k`, `-vvv` 출력 형식: <https://man7.org/linux/man-pages/man8/lspci.8.html>
- NVM Express Base Specification — BAR0/1 컨트롤러 레지스터, (선택) Index/Data Pair BAR2 — PCIE-B12 에서 자세히.
- 관련 레슨: PCIE-B04 (capability 순회, LnkSta), PCIE-B05 (BAR 사이징), PCIE-B06 (lspci/setpci/sysfs 실습), PCIE-B07 (Type 1), PCIE-B08 (ECAM), PCIE-B09 (열거·multi-function), PCIE-B10 (TLP, byte enable), PCIE-B12 (NVMe BAR0 레지스터), PCIE-M10 (MSI/MSI-X), PCIE-M12 (AER), PCIE-M13 (Linux PCI 드라이버 probe), PCIE-M15 (IOMMU), PCIE-A07 (hot-plug), BM-B04 (W1C/RMW 함정), COMM-B10 (바이트 단위 파서·버퍼 끝 검사), C-B01 (비트 연산).
