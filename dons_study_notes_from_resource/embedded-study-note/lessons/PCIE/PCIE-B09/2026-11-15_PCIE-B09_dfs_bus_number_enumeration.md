---
id: PCIE-B09
time: 60분
updated: 2026-11-15
---
# PCIE-B09 열거(Enumeration): DFS 버스 번호 할당

## 학습 목표

- 루트 버스에서 출발해 **깊이 우선(DFS)** 으로 브리지마다 `Primary / Secondary / Subordinate` 버스 번호(Type 1 헤더 0x18/0x19/0x1A)를 쓰는 열거기를 직접 짠다. 임시 subordinate(0xFF) → 재귀 → 진짜 subordinate 순서가 **왜 꼭 그래야 하는지** config 라우팅으로 설명한다.
- 함수(function) 탐색 규칙을 정확히 쓴다: fn0 이 없으면 그 device 는 없음, fn0 의 Header Type bit7(multi-function)일 때만 fn1..7 을 물어보고, 빈 fn 사이를 건너뛴다.
- "재귀 전에 subordinate 를 안 씀 / sec 로 씀", MFD 비트 무시, 버스 번호 고갈 — 네 가지 실패를 모의 하드웨어에서 재현하고, 증상("SSD 가 lspci 에 없음")과 원인을 연결한다.
- 결과를 `lspci -tv` 와 **바이트 단위로 같은** 트리로 출력하고, 실제 Linux 6.18 이 QEMU 토폴로지에 쓴 번호와 같음을 테스트로 보인다.
- 열거 후 상태를 검사하는 검증기(bring-up 체크리스트)와 "루트에서 SSD 까지 거치는 포트" 경로를 만든다.

## 1. 문제

Rivian 같은 실리콘 검증 면접의 "lspci 에 SSD 가 안 보여요, 뭐부터 보죠?" 와 SSD 펌웨어 면접의 "호스트가 우리 장치를 어떻게 찾나요?" 는 같은 질문이다 — **열거가 버스 번호를 써 줘야 config 요청이 장치까지 라우팅된다.**

PCIe 장치는 전원이 들어와도 자기 버스 번호를 모른다. 브리지(root port, 스위치 포트)의 버스 번호 레지스터는 리셋 후 0 이라, 루트 버스(bus 0) 바로 위의 함수들 말고는 **아무것도 config 로 닿지 않는다.** 펌웨어(UEFI/U-Boot) 또는 OS(Linux `drivers/pci/probe.c`)가 트리를 내려가며 브리지마다 번호를 써 줘야 그 아래가 보이기 시작한다. 이것이 열거(enumeration)다. Config 접근 자체(ECAM 오프셋 `bus<<20 | dev<<15 | fn<<12 | reg`)는 PCIE-B08, Type 1 헤더와 메모리 창 라우팅은 PCIE-B07, BAR 크기 측정은 PCIE-B05, 브리지 메모리 창 할당은 PCIE-A08 이다. 이 레슨은 **버스 번호만** 다룬다.

이 레슨의 토폴로지 A 는 QEMU 로 실제로 만들어 Linux 6.18.55 에 열거시킨 것과 같다 (`capture_qemu_enum.sh`, 결과 `capture_qemu_enum.txt` — QEMU 에뮬레이션 장치, 열거는 진짜 커널):

```svg
<svg viewBox="0 0 760 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <rect x="200" y="8" width="360" height="34" class="f-acc" stroke="currentColor"/>
  <text x="214" y="30" fill="currentColor">Root Complex — 루트 버스 00 (ECAM bus 00-ff)</text>
  <line x1="40" y1="68" x2="730" y2="68" stroke="currentColor" stroke-width="3"/>
  <text x="680" y="62" fill="currentColor" font-size="12">bus 00</text>
  <line x1="380" y1="42" x2="380" y2="68" stroke="currentColor"/>
  <!-- bus 00 children -->
  <line x1="105" y1="68" x2="105" y2="82" stroke="currentColor"/>
  <rect x="30" y="82" width="150" height="44" class="f-mut" stroke="currentColor"/>
  <text x="40" y="100" fill="currentColor" font-family="monospace" font-size="12">00:00.0</text>
  <text x="40" y="118" fill="currentColor" font-size="12">host bridge (Type 0)</text>
  <line x1="300" y1="68" x2="300" y2="82" stroke="currentColor"/>
  <rect x="225" y="82" width="150" height="44" class="f-ok" stroke="currentColor"/>
  <text x="235" y="100" fill="currentColor" font-family="monospace" font-size="12">00:01.0 [01-05]</text>
  <text x="235" y="118" fill="currentColor" font-size="12">Root Port</text>
  <line x1="640" y1="68" x2="640" y2="82" stroke="currentColor"/>
  <rect x="565" y="82" width="150" height="44" class="f-ok" stroke="currentColor"/>
  <text x="575" y="100" fill="currentColor" font-family="monospace" font-size="12">00:02.0 [06]</text>
  <text x="575" y="118" fill="currentColor" font-size="12">Root Port</text>
  <!-- RP1 link bus 01 -->
  <line x1="300" y1="126" x2="300" y2="158" stroke="currentColor"/>
  <text x="306" y="146" fill="currentColor" font-size="12">링크 = bus 01</text>
  <rect x="225" y="158" width="150" height="44" class="f-ok" stroke="currentColor"/>
  <text x="235" y="176" fill="currentColor" font-family="monospace" font-size="12">01:00.0 [02-05]</text>
  <text x="235" y="194" fill="currentColor" font-size="12">Switch USP</text>
  <!-- switch internal bus 02 -->
  <line x1="300" y1="202" x2="300" y2="226" stroke="currentColor"/>
  <line x1="70" y1="226" x2="500" y2="226" stroke="currentColor" stroke-width="3"/>
  <text x="505" y="230" fill="currentColor" font-size="12">bus 02 (스위치 내부)</text>
  <line x1="105" y1="226" x2="105" y2="240" stroke="currentColor"/>
  <line x1="300" y1="226" x2="300" y2="240" stroke="currentColor"/>
  <line x1="470" y1="226" x2="470" y2="240" stroke="currentColor"/>
  <rect x="30" y="240" width="150" height="44" class="f-ok" stroke="currentColor"/>
  <text x="40" y="258" fill="currentColor" font-family="monospace" font-size="12">02:00.0 [03]</text>
  <text x="40" y="276" fill="currentColor" font-size="12">DSP 0</text>
  <rect x="225" y="240" width="150" height="44" class="f-ok" stroke="currentColor"/>
  <text x="235" y="258" fill="currentColor" font-family="monospace" font-size="12">02:01.0 [04]</text>
  <text x="235" y="276" fill="currentColor" font-size="12">DSP 1</text>
  <rect x="395" y="240" width="150" height="44" class="f-ok" stroke="currentColor"/>
  <text x="405" y="258" fill="currentColor" font-family="monospace" font-size="12">02:02.0 [05]</text>
  <text x="405" y="276" fill="currentColor" font-size="12">DSP 2</text>
  <!-- leaves -->
  <line x1="105" y1="284" x2="105" y2="318" stroke="currentColor"/>
  <text x="111" y="304" fill="currentColor" font-size="12">bus 03</text>
  <rect x="30" y="318" width="150" height="44" class="f-warn" stroke="currentColor"/>
  <text x="40" y="336" fill="currentColor" font-family="monospace" font-size="12">03:00.0</text>
  <text x="40" y="354" fill="currentColor" font-size="12">NVMe 01:08:02</text>
  <line x1="300" y1="284" x2="300" y2="318" stroke="currentColor"/>
  <text x="306" y="304" fill="currentColor" font-size="12">bus 04</text>
  <rect x="225" y="318" width="150" height="44" class="f-warn" stroke="currentColor"/>
  <text x="235" y="336" fill="currentColor" font-family="monospace" font-size="12">04:00.0 .1 .3</text>
  <text x="235" y="354" fill="currentColor" font-size="12">NIC, MFD (fn2 없음)</text>
  <line x1="470" y1="284" x2="470" y2="318" stroke="currentColor"/>
  <text x="476" y="304" fill="currentColor" font-size="12">bus 05</text>
  <rect x="395" y="318" width="150" height="44" fill="none" stroke="currentColor" stroke-dasharray="5,4"/>
  <text x="405" y="345" fill="currentColor" font-size="12">빈 슬롯 (번호는 받음)</text>
  <line x1="640" y1="126" x2="640" y2="158" stroke="currentColor"/>
  <text x="646" y="146" fill="currentColor" font-size="12">bus 06</text>
  <rect x="565" y="158" width="150" height="44" class="f-warn" stroke="currentColor"/>
  <text x="575" y="176" fill="currentColor" font-family="monospace" font-size="12">06:00.0</text>
  <text x="575" y="194" fill="currentColor" font-size="12">edu (00:ff:00)</text>
  <text x="565" y="320" fill="currentColor" font-size="12">[ss-uu] = secondary-</text>
  <text x="565" y="338" fill="currentColor" font-size="12">subordinate (0x19/0x1A)</text>
  <text x="30" y="390" fill="currentColor" font-size="12">root port / DSP 아래 링크는 점대점: device 0 만 있다. 스위치 내부 bus 02 에만 dev 0,1,2 가 같이 산다.</text>
</svg>
```

(숫자는 전부 실측 `capture_qemu_enum.txt` 의 Linux 가 쓴 값이며 `solution.c` 의 `LINUX_A` 표와 테스트에 그대로 들어 있다.)

모의 하드웨어는 PCIE-B08 의 ECAM 모델을 줄이고 **물리 토폴로지**를 붙인 것이다: 함수마다 256B config 바이트 배열, 브리지마다 아래쪽 "링크", 그리고 `mock_route()` 가 config 요청을 루트 버스부터 브리지의 `secondary ≤ bus ≤ subordinate` 창을 따라 내려보낸다. 아무 브리지도 안 받으면 all-1s (Unsupported Request → 루트 컴플렉스가 0xFFFFFFFF 를 돌려줌). 그래서 번호를 잘못 쓰면 장치가 **정말로 안 보인다** — 실제 칩처럼.

### Part A — DFS 열거기

1. `fn_present(m, bus, dev, fn, &vid_did)`: offset 0 을 32비트로 읽어 VID 가 0xFFFF(없음) 또는 0x0000 이면 false.
2. `scan_bus(m, r, opts, bus, parent)`: dev 0..31 을 훑고, Type 1 을 만나면 `pri = bus`, `sec = max + 1`, `sub = opts->last_bus(0xFF)` 를 쓴 뒤 `sec` 버스를 재귀로 훑고, 돌아오면 `sub = 재귀가 찾은 최대 버스` 로 다시 쓴다. 반환값 = 이 버스 아래 최대 버스 번호.
3. `enum_all()`: 루트 버스 0 부터. 결과 표(`pci_fn[64]`, malloc 없음)에 BDF, Header Type, VID/DID, class, 쓴 번호, 부모 브리지 인덱스.
4. 테스트: 토폴로지 A 의 6개 브리지 번호가 **Linux 6.18 실측과 같다** (00:01.0 [01-05], 01:00.0 [02-05], 02:00.0 [03], 02:01.0 [04], 02:02.0 [05], 00:02.0 [06]), 발견 순서가 DFS 순서.

### Part B — 함수 규칙, 버그 재현, 고갈

| # | 상황 | 기대 결과 (테스트로 단언) |
|---|---|---|
| B1 | NIC: fn0 Header Type 0x80, fn1, fn3 있음, fn2 없음 | 04:00.0/.1/.3 셋 다 — fn2 빈칸에서 멈추지 않는다 |
| B2 | 버그: 재귀 **전에** subordinate 를 안 씀 (리셋값 0) | 함수 3개만 (host, RP 두 개 [01], [02]). 스위치·SSD·NIC·edu 전부 사라짐 |
| B3 | 버그: 재귀 전 subordinate = secondary | USP 01:00.0 까지만 보이고 그 아래(DSP·SSD·NIC) 사라짐, RP2 가 [03] |
| B4 | `last_bus = 4` (DT `bus-range = <0 4>` 같은 좁은 범위) | `ENUM_ERR_BUSES`, DSP2·RP2 는 0/0/0 그대로, edu 사라짐, 11개 |
| B5 | 토폴로지 B (가상): 스위치 2단, 루트 버스 MFD 00:1f.0/.3/.4, fn0 없는 00:05.2 | 브리지 9개 번호 정확히, 00:05.2 는 **열거 안 됨**, 17개 |
| B6 | 토폴로지 B 의 FPGA 는 fn 번호를 무시하는 고장 장치 + 버그 "MFD 비트 무시하고 fn 0..7" | 같은 장치가 8번 보인다 (유령 함수 08:00.1–.7). 정답 코드는 1번 |

### Part C — lspci -tv 트리, 검증기, 경로

- `tree_print()`: pciutils `ls-tree.c` 모양 그대로. 토폴로지 A 결과가 **실측 `lspci -tv` 문자열과 바이트 단위로 같아야** 한다. 토폴로지 B 의 기대 문자열도 진짜 `lspci -F` 로 만든 것이다 (`capture_lspci_tree_b.txt`).
- `enum_verify()`: 열거가 끝난 "하드웨어" 를 다시 읽어 불변식을 검사 — `V_PRI / V_SEC / V_SUB / V_NEST / V_OVERLAP / V_UNREACH`. 한 가지씩 일부러 망가뜨리고 정확한 코드와 인덱스를 단언.
- `enum_path()`: 루트에서 SSD 03:00.0 까지 거치는 브리지 = 00:01.0 → 01:00.0 → 02:00.0. 링크가 x2 로 떨어졌을 때 LnkSta 를 볼 포트 목록이다 (PCIE-B04, PCIE-M08).

## 2. Problem (English)

> **PCI bus enumeration.** You are writing the PCI core of a boot firmware for an ARM SoC with an ECAM root complex
> (root bus 0). All bridges come out of reset with Primary/Secondary/Subordinate = 0. Config reads to a function
> that does not exist, or that no bridge routes to, return all 1s.
>
> ```c
> bool fn_present(mock *m, uint8_t bus, uint8_t dev, uint8_t fn, uint32_t *vid_did);
> int  scan_bus(mock *m, enum_result *r, const enum_opts *o, uint8_t bus, int parent); // returns highest bus below
> int  enum_all(mock *m, enum_result *r, const enum_opts *o);
> int  tree_print(const enum_result *r, name_fn nm, char *out, size_t cap);           // lspci -tv format, -1 if cap too small
> vcode enum_verify(mock *m, const enum_result *r, int *bad);
> int  enum_path(const enum_result *r, int idx, int *path, int cap);
> ```
>
> - Depth-first: give each Type 1 bridge `secondary = next free bus`, a temporary `subordinate = last_bus`, recurse, then
>   write the real subordinate. Bus numbers must not exceed `o->last_bus` (report `ENUM_ERR_BUSES`, skip that bridge).
> - Probe functions 1..7 only if function 0 exists and its Header Type bit 7 is set. Skip holes.
> - No dynamic allocation; at most 64 functions recorded.
>
> Example (topology from the figure): `enum_all` returns 6; bridge 00:01.0 ends with `[01-05]`;
> `tree_print` produces exactly
> `-[0000:00]-+-00.0  Red Hat, Inc. QEMU PCIe Host bridge` / `           +-01.0-[01-05]----00.0-[02-05]--+-00.0-[03]----00.0 ...`
> Follow-ups: what breaks if you write the subordinate only after recursing? Why DFS and not BFS?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

모의 하드웨어(`mock_route`, `ecam_rd32`, `cfg_rd8/16/32`, `cfg_wr8`), 토폴로지 빌더, 트리 줄 버퍼(`tput`, `tree_emit`), 하네스는 다 들어 있다. TODO 는 A1–A3, B1–B2(같은 `scan_bus` 안), C1a/C1b(트리), C2(검증기), C3(경로). 시작 코드 실행 결과 (경고 0, exit 1):

```text
FAIL starter.c:468  fn_present(&m, 0, 0, 0, &id) && id == 0x00081b36u
FAIL starter.c:473  last == 6
...
B2 tmp_sub=NONE : 0 functions, 0 config reads, 0 of them all-1s
...
10/48 passed
```

## 4. 힌트

<details><summary>힌트 1 — 장치가 "있다" 는 무엇으로 아나</summary>

없는 함수에 config 읽기를 보내면 아무도 Completion 을 안 주거나 UR 이 돌아오고, 루트 컴플렉스는 CPU 에 0xFFFFFFFF 를 준다. VID 0xFFFF 는 PCI-SIG 가 절대 할당하지 않는 값이라 "없음" 표시로 쓴다. Linux 는 0x00000000, 0x0000FFFF, 0xFFFF0000 도 "없음" 으로 본다 (깨진 보드 대비, `pci_bus_generic_read_dev_vendor_id`).

</details>

<details><summary>힌트 2 — 재귀하기 전에 subordinate 를 왜 써야 하나</summary>

브리지는 Type 1 config 요청의 버스 번호 B 가 `secondary < B ≤ subordinate` 일 때만 아래로 넘긴다 (B == secondary 면 Type 0 로 바꿔 자기 링크에). 재귀는 secondary 버스 **아래의** 브리지에 번호(secondary+1, +2 …)를 써야 하는데, 지금 브리지의 subordinate 가 0 이면 그 요청이 이 브리지에서 막힌다. 아직 아래에 몇 개가 있을지 모르니 일단 "끝까지(0xFF)" 열어 두고, 다 훑은 뒤 좁힌다.

</details>

<details><summary>힌트 3 — max 하나로 번호를 나눠 준다</summary>

`scan_bus` 안의 `max` = "지금까지 쓴 가장 큰 버스 번호". 처음엔 `bus` 자신. 브리지를 만나면 `sec = max + 1`, 재귀 결과로 `max` 를 갱신해야 다음 형제 브리지가 그 뒤 번호를 받는다. 반환값이 곧 위 브리지의 subordinate.

</details>

<details><summary>힌트 4 — 함수 루프</summary>

fn0 이 없으면 `continue` (다음 dev). 있으면 fn0 의 Header Type 을 읽어 `nfn = (bit7 ? 8 : 1)`. fn 1..7 은 없으면 `continue` — `break` 하면 fn2 가 빈 NIC 에서 fn3 을 놓친다. Header Type 의 하위 7비트(`& 0x7f`)가 1 이면 브리지.

</details>

<details><summary>힌트 5 — 트리 출력: 한 줄 버퍼를 재활용한다</summary>

`line[]` 에 접두사를 써 나가다 끝단에서 한 줄을 내보낸다(`tree_emit`). 내보낸 뒤 그 줄의 `+`,`|` 는 `|` 로, 나머지는 공백으로 바뀌어 다음 줄의 들여쓰기가 된다. 버스의 자식이 1개면 `"--"`, 여럿이면 `"+-"` … 마지막만 `"\-"`. 브리지는 `"dd.f-[ss-uu]-"` + `"-"` 후 secondary 버스로 재귀, 범위가 한 버스면 `"[ss]"`.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "브리지 버스 번호 레지스터 셋(0x18 Primary, 0x19 Secondary, 0x1A Subordinate)이 Type 1 config 라우팅을 정한다." — 그림 한 장.
2. "리셋 후 0 이라 루트 버스 말고는 안 보인다 → 루트 버스부터 DFS 로 번호를 나눠 준다." — 알고리즘 5줄.
3. "임시 subordinate 0xFF 가 핵심, 안 하면 이렇게 깨진다." — Part B 증상.
4. "MFD 비트, fn0 필수, 점대점 링크는 dev 0 만." — 함수 규칙.
5. "Linux 도 똑같이 한다: dmesg `busn_res: [bus 03-ff] end is updated to 03`." — 실측으로 마무리.

### Part A — 브리지는 버스 번호 창으로 config 를 라우팅한다

Type 1 헤더의 0x18 DW (Linux `include/uapi/linux/pci_regs.h` 기준: `PCI_PRIMARY_BUS 0x18`, `PCI_SECONDARY_BUS 0x19`, `PCI_SUBORDINATE_BUS 0x1a`, `PCI_SEC_LATENCY_TIMER 0x1b`):

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">실측 00:01.0 (root port) config 0x18..0x1B = 00 01 05 00</text>
  <g stroke="currentColor">
    <rect x="40" y="34" width="160" height="50" class="f-mut"/>
    <rect x="200" y="34" width="160" height="50" class="f-ok"/>
    <rect x="360" y="34" width="160" height="50" class="f-ok"/>
    <rect x="520" y="34" width="160" height="50" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="48" y="52">0x18 Primary</text><text x="208" y="52">0x19 Secondary</text>
    <text x="368" y="52">0x1A Subordinate</text><text x="528" y="52">0x1B Sec Latency</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="14">
    <text x="48" y="74">00 (위쪽 버스)</text><text x="208" y="74">01 (바로 아래)</text>
    <text x="368" y="74">05 (아래 최대)</text><text x="528" y="74">00</text>
  </g>
  <text x="10" y="114" fill="currentColor" font-weight="bold">이 브리지가 Type 1 config 요청 (대상 bus B) 을 받았을 때</text>
  <rect x="40" y="126" width="210" height="62" class="f-ok" stroke="currentColor"/>
  <text x="50" y="146" fill="currentColor">B == Secondary (01)</text>
  <text x="50" y="166" fill="currentColor" font-size="12">Type 0 로 바꿔 아래 링크로</text>
  <text x="50" y="181" fill="currentColor" font-size="12">(→ 01:00.0 USP 가 대답)</text>
  <rect x="270" y="126" width="230" height="62" class="f-ok" stroke="currentColor"/>
  <text x="280" y="146" fill="currentColor">Secondary &lt; B ≤ Subordinate</text>
  <text x="280" y="166" fill="currentColor" font-size="12">Type 1 그대로 아래로 (02..05)</text>
  <text x="280" y="181" fill="currentColor" font-size="12">→ 다음 브리지가 같은 판단</text>
  <rect x="520" y="126" width="210" height="62" class="f-warn" stroke="currentColor"/>
  <text x="530" y="146" fill="currentColor">그 외 (B = 06, 00 …)</text>
  <text x="530" y="166" fill="currentColor" font-size="12">내 것 아님 → 아무도 안 받으면</text>
  <text x="530" y="181" fill="currentColor" font-size="12">UR → CPU 는 0xFFFFFFFF</text>
  <text x="10" y="222" fill="currentColor" font-weight="bold">리셋 직후: Secondary = Subordinate = 0</text>
  <text x="10" y="244" fill="currentColor" font-size="12">B ≥ 1 인 어떤 요청도 "Secondary &lt; B ≤ 0" 을 만족 못 함 → 루트 버스 00 위 함수만 보인다.</text>
  <text x="10" y="266" fill="currentColor" font-size="12">그래서 열거기는 "번호를 쓴다 → 그 번호로 아래를 읽는다" 를 반복하며 위에서 아래로만 내려갈 수 있다.</text>
  <text x="10" y="288" fill="currentColor" font-size="12">Primary 는 라우팅에 안 쓰이지만 (이 레슨 모의 하드웨어도 마찬가지) 검증기와 lspci 가 트리를 재구성할 때 쓴다.</text>
</svg>
```

알고리즘 (`scan_bus`, 반환 = 이 버스 아래 최대 번호):

```text
scan_bus(bus):
  max = bus
  for dev in 0..31:
    fn0 없음 → 다음 dev
    nfn = (fn0 의 HeaderType & 0x80) ? 8 : 1
    for fn in 0..nfn-1:
      없으면 continue
      기록
      Type 1 이면:
        if max+1 > last_bus: ENUM_ERR_BUSES, 이 브리지 건너뜀
        sec = max+1
        write Primary=bus, Secondary=sec, Subordinate=last_bus(0xFF)   ← 임시로 끝까지 연다
        child_max = scan_bus(sec)
        write Subordinate=child_max                                     ← 좁힌다
        max = child_max
  return max
```

토폴로지 A 에서의 실제 진행 (쓰기 순서 = 코드 순서). 오른쪽 열은 실측 Linux dmesg — **순서까지 같다**:

| 단계 | 어디를 훑나 | 쓰기 | 돌아오며 확정 | Linux 6.18 dmesg (실측) |
|---|---|---|---|---|
| 1 | bus 00 dev 0 | host bridge, Type 0 기록만 | | `00:00.0 ... type 00 class 0x060000` |
| 2 | bus 00 dev 1 = RP1 | 00:01.0 ← 00 / 01 / **FF** | | `00:01.0: bridge configuration invalid ([bus 00-00]), reconfiguring` |
| 3 | bus 01 dev 0 = USP | 01:00.0 ← 01 / 02 / FF | | |
| 4 | bus 02 dev 0 = DSP0 | 02:00.0 ← 02 / 03 / FF | bus 03 에 NVMe → sub = 03 | `pci_bus 0000:03: busn_res: [bus 03-ff] end is updated to 03` |
| 5 | bus 02 dev 1 = DSP1 | 02:01.0 ← 02 / 04 / FF | NIC fn0,1,3 → sub = 04 | `... [bus 04-ff] end is updated to 04` |
| 6 | bus 02 dev 2 = DSP2 | 02:02.0 ← 02 / 05 / FF | 빈 슬롯 → sub = 05 | `... [bus 05-ff] end is updated to 05` |
| 7 | bus 02 끝 | | USP sub = 05 | `... [bus 02-ff] end is updated to 05` |
| 8 | bus 01 끝 | | RP1 sub = 05 | `... [bus 01-ff] end is updated to 05` |
| 9 | bus 00 dev 2 = RP2 | 00:02.0 ← 00 / 06 / FF | edu → sub = 06 | `... [bus 06-ff] end is updated to 06` |

Linux 도 정확히 이 방식이다: `pci_scan_bridge_extend()` 가 `buses = primary | (next_busnr << 8) | (busn_res.end << 16)` 를 **한 번의 dword 쓰기**로 0x18 에 넣고(`/* We need to blast all three values with a single write */`), 자식 버스를 훑은 뒤 `pci_write_config_byte(dev, PCI_SUBORDINATE_BUS, max)` (Linux 6.18.55 `drivers/pci/probe.c`). 차이 하나: Linux 는 한 버스의 슬롯을 **먼저 전부** 훑고 나서 브리지들로 내려간다(그래서 dmesg 에 00:01.0, 00:02.0 이 먼저 찍힌다). 번호를 주는 순서는 똑같이 "브리지 순서대로, 깊이 우선" 이라 결과가 같다.

왜 단계 2–8 동안 RP1 이 [01-FF] 로 열려 있어도 괜찮은가? 그동안 RP2 는 아직 번호가 없으니(0/0) 둘이 다툴 일이 없다. RP1 이 [01-05] 로 좁혀진 **다음에야** RP2 가 06 을 받는다. DFS 가 "한 서브트리를 끝내고 다음으로" 가기 때문에 성립한다.

### Part A — 왜 DFS 인가 (BFS 로는 안 되는 이유)

브리지 하나가 표현할 수 있는 건 **연속 구간** `[secondary..subordinate]` 하나다. 그러니 한 브리지 아래의 모든 버스 번호는 연속이어야 한다. DFS 순서(전위 순회)로 번호를 매기면 모든 서브트리가 연속 구간이 된다 — 트리의 전위 순회 번호가 갖는 성질 그대로다. BFS 로 매기면 A 에서 RP1=01, RP2=02, USP=03 … 이 되어 RP1 아래(01, 03..06)가 02 를 건너뛴다. RP1 을 [01-06] 으로 잡으면 RP2 의 02 와 겹치고(`V_OVERLAP`), [01-01] 로 잡으면 USP 아래가 안 보인다.

### Part B — 함수 규칙 세 가지

| 규칙 | 근거 | 어기면 |
|---|---|---|
| fn0 이 없으면 그 device 는 없음 | 다기능 장치는 fn0 를 반드시 구현 (스펙). Linux `pci_scan_slot`: "Function 0 is required unless we are running on a hypervisor that passes through individual PCI functions" | 토폴로지 B 의 00:05.2 같은 유령을 잡거나, 반대로 fn0 없는 가상화 환경 예외를 모르면 VF passthrough 에서 놓친다 |
| fn1..7 은 fn0 Header Type bit7(`PCI_HEADER_TYPE_MFD 0x80`)일 때만 | 옛 장치 중 fn 번호를 디코드하지 않고 8개 fn 모두에 대답하는 것이 있다 | B6: FPGA 가 8개로 보임. 드라이버가 8번 probe 되어 같은 BAR 를 8번 매핑 |
| fn 사이 빈칸은 건너뛴다 | fn 은 0..7 아무 조합이나 가능 (QEMU NIC: 0, 1, 3) | `break` 하면 04:00.3 을 놓친다 |

Linux 의 `next_fn()` 이 바로 이 표다: `if (fn >= 7) return -ENODEV; if (dev && !dev->multifunction) return -ENODEV; return fn + 1;` — 직전 fn 이 없으면(`dev == NULL`) 계속 간다. 추가로 `only_one_child()`: 아래쪽 브리지가 PCIe Downstream Port(root port, DSP)면 링크에 device 0 만 있으므로 dev 1..31 을 아예 안 묻는다 (연습 1).

### Part B — subordinate 버그 두 가지를 라우팅으로 보기

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 토폴로지 A, 재귀 전에 subordinate 에 무엇을 썼나</text>
  <g stroke="currentColor">
    <rect x="10" y="34" width="240" height="30" class="f-ok"/>
    <rect x="260" y="34" width="240" height="30" class="f-warn"/>
    <rect x="510" y="34" width="240" height="30" class="f-warn"/>
  </g>
  <text x="20" y="54" fill="currentColor">정답: sub = FF → 12개</text>
  <text x="270" y="54" fill="currentColor">B2: 안 씀 (0) → 3개</text>
  <text x="520" y="54" fill="currentColor">B3: sub = sec → 5개</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="90">00:00.0 host</text>
    <text x="20" y="110">00:01.0 [01-05]</text>
    <text x="20" y="130"> 01:00.0 USP [02-05]</text>
    <text x="20" y="150">  02:00.0 [03] → NVMe</text>
    <text x="20" y="170">  02:01.0 [04] → NIC×3</text>
    <text x="20" y="190">  02:02.0 [05] (빈)</text>
    <text x="20" y="210">00:02.0 [06] → edu</text>
    <text x="270" y="90">00:00.0 host</text>
    <text x="270" y="110">00:01.0 [01]</text>
    <text x="270" y="130" opacity="0.45"> 01:00.0 … 안 보임</text>
    <text x="270" y="150">00:02.0 [02]</text>
    <text x="270" y="170" opacity="0.45"> 02:00.0 edu 안 보임</text>
    <text x="520" y="90">00:00.0 host</text>
    <text x="520" y="110">00:01.0 [01-02]</text>
    <text x="520" y="130"> 01:00.0 USP [02]</text>
    <text x="520" y="150" opacity="0.45">  DSP×3, NVMe, NIC 없음</text>
    <text x="520" y="170">00:02.0 [03]</text>
    <text x="520" y="190"> 03:00.0 edu</text>
  </g>
  <text x="270" y="244" fill="currentColor" font-size="12">bus 01 읽기: RP1 [01-00]</text>
  <text x="270" y="262" fill="currentColor" font-size="12">"01 ≤ 01 ≤ 00" 거짓 → 안 넘김</text>
  <text x="270" y="280" fill="currentColor" font-size="12">→ 바로 아래 버스조차 all-1s</text>
  <text x="520" y="244" fill="currentColor" font-size="12">bus 02 읽기: RP1 [01-01]</text>
  <text x="520" y="262" fill="currentColor" font-size="12">02 &gt; sub 01 → 안 넘김</text>
  <text x="520" y="280" fill="currentColor" font-size="12">→ 한 단계만 보이고 끝</text>
  <text x="20" y="244" fill="currentColor" font-size="12">재귀 동안 위쪽 브리지가</text>
  <text x="20" y="262" fill="currentColor" font-size="12">전부 [sec-FF] 로 열려 있어</text>
  <text x="20" y="280" fill="currentColor" font-size="12">아래 어떤 번호도 도달</text>
  <text x="10" y="316" fill="currentColor" font-size="12">B2 실측: config 읽기 111번 중 93번이 all-1s. 증상은 "root port 만 보이고 SSD 없음" — 링크 문제로 오해하기 쉽다.</text>
</svg>
```

두 버그 모두 **에러가 안 난다**. 열거는 정상 종료하고, 끝난 뒤 subordinate 도 "그럴듯한" 값으로 써진다. 그래서 `echo 1 > /sys/bus/pci/rescan` 같은 재스캔을 하면 B2 상태에서 갑자기 USP 가 나타난다(이제 RP1 이 [01-01] 이니까) — "두 번 스캔하면 보여요" 는 이 버그의 전형적인 신호다.

B4 고갈: `last_bus = 4` 면 DSP2 차례에 `max + 1 = 5 > 4`. 이 브리지는 0/0/0 그대로 두고(`ENUM_ERR_BUSES`) 계속 간다 — 이미 번호를 받은 형제를 망가뜨리지 않는 게 중요하다. RP2 도 같은 이유로 실패, edu 가 사라진다. 실제로는 DT 의 `bus-range` 나 ACPI MCFG 의 버스 범위, ECAM 창 크기(버스당 1MB: 실측 `ECAM at [mem 0x4010000000-0x401fffffff] for [bus 00-ff]` = 256MB)가 `last_bus` 를 정한다 (PCIE-B08). 그리고 `uint8_t` 로 `max + 1` 을 계산하면 255 + 1 = 0 이 되어 bus 0 으로 재귀하는 무한 루프가 된다 — 이 코드가 `int max` 를 쓰는 이유.

### Part C — lspci -tv 는 버스 번호로 트리를 다시 만든다

`lspci` 는 물리 연결을 모른다. config 공간만 읽어서 "secondary == 이 버스인 브리지 밑에 이 버스의 함수들" 로 트리를 만든다(pciutils `ls-tree.c` 의 `grow_tree`/`insert_dev`). 이 레슨의 `tree_print` 도 같은 방식이다: 버스 B 의 자식 = 결과 표에서 `bus == B` 인 함수들(발견 순서 = dev/fn 오름차순).

줄 만들기 (토폴로지 A 의 둘째·셋째 줄 추적):

```text
"-[0000:00]-"  + 루트 버스 자식 4개 → "+-" + "01.0" + "-[01-05]-" + "-"
  → bus 01 자식 1개 → "--" + "00.0" + "-[02-05]-" + "-"
  → bus 02 자식 3개 → "+-" + "00.0" + "-[03]-" + "-" → bus 03 자식 1개 → "--" + "00.0" + "  Red Hat, Inc. QEMU NVM Express Controller"  ⏎
줄을 내보낸 뒤 '+','|' → '|', 나머지 → ' '  (print_it) 그래서 다음 줄은
"           |                               " 에서 이어서 "+-01.0-[04]--+-00.0  Intel ..."
```

결과는 실측과 **바이트 단위로 같다** (테스트 `strcmp(g_tree, LSPCI_TV_A) == 0`):

```text
-[0000:00]-+-00.0  Red Hat, Inc. QEMU PCIe Host bridge
           +-01.0-[01-05]----00.0-[02-05]--+-00.0-[03]----00.0  Red Hat, Inc. QEMU NVM Express Controller
           |                               +-01.0-[04]--+-00.0  Intel Corporation 82574L Gigabit Network Connection
           |                               |            +-00.1  Intel Corporation 82574L Gigabit Network Connection
           |                               |            \-00.3  Intel Corporation 82574L Gigabit Network Connection
           |                               \-02.0-[05]--
           \-02.0-[06]----00.0  Device 1234:11e8
```

브리지 줄에는 이름이 없고, 빈 슬롯 DSP 는 `-[05]--` 로 끝난다(자식 0개 → 그 자리에서 줄을 내보냄). 이름을 모르는 장치는 `Device vvvv:dddd` (lspci 도 pci.ids 에 벤더가 없으면 이렇게 찍는다 — edu 의 1234:11e8).

**무한 재귀 방지:** secondary 가 0 이거나 자기 버스 이하인 브리지에서 secondary 버스로 내려가면 루트 버스 0 을 다시 그리게 되어 끝나지 않는다. 그래서 `tree_dev` 는 그런 브리지를 `-[--]-` 로 끝낸다(이 레슨이 정한 표기). 실제로 VM 의 pciutils 3.14.0 `lspci -F` 에 secondary/subordinate 를 0 으로 바꾼 덤프를 주자 **segfault(rc 139)** 가 났다 — 원인은 같은 종류의 재귀로 추정(소스 확인은 안 함). config 데이터를 따라가는 코드는 전부 홉/깊이 제한이 필요하다(PCIE-B04 의 capability 루프와 같은 교훈).

### Part C — 검증기: bring-up 체크리스트를 코드로

`enum_verify()` 는 결과 표를 믿지 않고 **하드웨어를 다시 읽는다** (펌웨어가 넘겨준 상태, 또는 누가 나중에 바꾼 상태를 검사하는 용도). Linux 도 펌웨어가 쓴 번호를 받아들이기 전에 같은 검사를 한다 — `primary != bus->number || secondary <= bus->number || secondary > subordinate` 면 `"bridge configuration invalid ([bus %02x-%02x]), reconfiguring"` (실측 dmesg 에 이 줄이 찍힌 이유: QEMU 는 펌웨어 없이 0/0/0 으로 넘겨줬다).

| 코드 | 조건 (브리지 b, 앉은 버스 B, 부모 브리지 p) | 테스트에서 망가뜨린 것 |
|---|---|---|
| `V_PRI` | b.pri ≠ B | 00:02.0 primary = 01 |
| `V_SEC` | b.sec ≤ B | 02:00.0 secondary = 02 |
| `V_SUB` | b.sub < b.sec | 02:00.0 = [03-02] |
| `V_NEST` | b.sub > p.sub (부모 구간 밖) | USP sub = 04 → DSP2 [05] 가 밖 |
| `V_OVERLAP` | 같은 버스 형제 브리지와 구간이 겹침 | 02:01.0 sub = 05 → 02:02.0 [05] 와 겹침 |
| `V_UNREACH` | 기록된 BDF 에서 VID/DID 가 안 읽힘 | 00:02.0 = [07] → edu 06:00.0 이 사라짐 (구간 자체는 멀쩡) |

`V_UNREACH` 가 따로 있는 이유: 번호가 모두 "형식상" 맞아도 장치가 다른 버스로 옮겨지면(재번호, 핫플러그) 드라이버가 들고 있는 BDF 는 틀린다. 마지막 단계에서 "기록대로 읽히나" 를 확인한다.

### Part C — 경로: 링크 triage 의 출발점

`enum_path(SSD)` = 00:01.0 → 01:00.0 → 02:00.0. SSD 가 Gen4 x4 인데 x2 로 떠 있으면, 이 경로의 각 링크(RP1↔USP, DSP0↔SSD)마다 아래쪽 포트의 LnkSta 와 위쪽 포트의 LnkCap 을 비교한다(PCIE-B04, B06, M08). 토폴로지 B 의 SSD-b(06:00.0)는 경로 길이 5 — 스위치를 두 번 지나므로 레이턴시·대역폭 병목 후보도 그만큼 늘어난다.

### 복잡도와 자원

| 항목 | 값 |
|---|---|
| config 읽기 | 버스마다 dev 32개 × (fn0 1번 + Header Type 1번) + 존재하는 fn 마다 몇 번. 토폴로지 A 전체 289번 중 all-1s 219번 → `only_one_child` 로 134번(연습 1, 실측) |
| 재귀 깊이 | 브리지 중첩 깊이 ≤ 버스 번호 수(255). 프레임 작음. 펌웨어 스택이 작으면 명시적 스택판(연습 4) |
| 메모리 | 결과 표 64 × 20 바이트 정도, malloc 없음 |
| 시간 | ECAM 읽기 하나가 실제 HW 에선 수백 ns~µs (non-posted, completion 대기 — PCIE-B11). 그래서 Linux 가 dev 0 만 묻는 최적화를 한다 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 48/48 passed, exit 0 (0.01 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
```

실행 출력 중 테스트 외 줄:

```text
B2 tmp_sub=NONE : 3 functions, 111 config reads, 93 of them all-1s
B3 tmp_sub=SEC  : 5 functions (USP visible, DSPs/SSD/NIC missing)
B6 probe_all_fn : FPGA appears 8 times (phantom functions 08:00.1-08:00.7)
C1 tree (topology A, same bytes as real lspci -tv):
-[0000:00]-+-00.0  Red Hat, Inc. QEMU PCIe Host bridge
           ... (위와 같음)
C2 tree (topology B):
-[0000:00]-+-00.0  Device ab00:0001
           +-01.0-[01-07]----00.0-[02-07]--+-00.0-[03-06]----00.0-[04-06]--+-00.0-[05]----00.0  Device ab30:5001
           |                               |                               \-01.0-[06]----00.0  Device ab30:5002
           |                               \-01.0-[07]----00.0  Device ab40:0e00
           +-02.0-[08]----00.0  Device ab50:f0a0
           +-03.0-[09]--
           +-1f.0  Device ab00:1f00
           +-1f.3  Device ab00:1f03
           \-1f.4  Device ab00:1f04
48/48 passed
```

토폴로지 B 트리는 같은 번호를 넣은 64바이트 덤프를 VM 의 진짜 `lspci -F dump_b.txt -tv` 에 먹여 얻은 문자열과 같다 (`capture_lspci_tree_b.txt`). 실측 재현: `limactl shell elinux -- bash lessons/PCIE/PCIE-B09/capture_qemu_enum.sh` (QEMU TCG 부팅 포함 수십 초).

## 7. 자주 하는 실수

**1. subordinate 를 재귀가 끝난 뒤에만 쓴다.** "subordinate = 아래 최대 번호" 라는 정의만 보고 짜면 자연스럽게 나오는 코드인데, 재귀 중에는 그 브리지가 아무것도 안 넘긴다(B2). 고친 코드: 재귀 **전에** `cfg_wr8(..., PCI_SUBORDINATE_BUS, o->last_bus)`, 후에 `child_max`. Linux 처럼 pri/sec/sub 세 바이트를 dword 한 번으로 쓰면 "sec 는 썼는데 sub 는 아직" 인 순간도 없앤다.

**2. 임시 subordinate 를 secondary 로 쓴다.** "깔끔하게 최소값으로" 쓴 것인데 한 단계 아래까지만 보인다(B3). 스위치가 없는 보드(root port 바로 아래 SSD)에선 멀쩡히 동작해서 **스위치가 들어간 보드에서 처음 터진다** — 테스트 토폴로지에 스위치를 꼭 넣는 이유.

**3. 재귀 결과로 max 를 갱신하지 않는다.** `scan_bus(sec)` 의 반환값을 subordinate 에만 쓰고 `max` 는 그대로 두면, 다음 형제 브리지가 `max + 1` = 이미 쓴 번호를 다시 받는다. 형제 구간이 겹쳐 `V_OVERLAP`, 라우팅은 먼저 받은 쪽이 이겨 한쪽 서브트리가 사라진다. 고친 코드: `max = child_max;`.

**4. MFD 비트를 무시하거나, 각 fn 의 자기 비트를 본다.** fn 별칭 장치가 8번 보인다(B6). 판단은 **fn0 의 Header Type bit7 하나**로 한다(QEMU NIC 도 fn0 만 0x80, fn1/fn3 은 0x00 — 실측 덤프). 반대로 fn 빈칸에서 `break` 하면 04:00.3 을 놓친다.

**5. Header Type 을 `== 1` 로 비교한다.** 다기능 브리지(헤더 0x81)를 놓친다. 고친 코드: `(hdr & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE`.

**6. 버스 번호를 8비트로 더한다.** `uint8_t sec = max + 1;` 을 범위 검사 없이 쓰면 255 다음이 0 이 되어 bus 0 을 다시 훑는 무한 재귀. 고친 코드: `int max`, `if (max + 1 > o->last_bus) { ENUM_ERR_BUSES; continue; }`.

**7. lspci 출력의 버스 번호를 10진수로 읽는다.** 연습 2 의 `[0b-0e]` 는 11..14 다. BDF 는 전부 16진수(`bb:dd.f`), 디바이스 번호도 `1f` = 31.

## 8. 면접 Q&A

<details><summary>Q1. 왜 DFS 로 번호를 매기나요? BFS 면 안 되나요?</summary>

브리지는 아래쪽 버스를 `[secondary..subordinate]` **한 구간**으로만 표현한다. 그래서 어떤 브리지 아래의 버스 번호들은 연속이어야 하고, 트리에서 "모든 서브트리가 연속 구간" 이 되는 번호 매김이 바로 전위(DFS) 순서다. BFS 는 같은 깊이를 먼저 채우므로 형제 서브트리 번호가 섞인다: 토폴로지 A 에서 RP1=01, RP2=02, USP=03 이면 RP1 아래가 {01, 03..06} 이라 구간 하나로 못 쓴다. 꼬리질문 "그럼 BFS 로 장치를 '찾는' 것도 안 되나?" → 찾기 자체도 막힌다. USP(bus 01 위)의 버스 03 에 닿으려면 RP1 창이 03 을 포함해야 하는데, 그러면 RP2 의 02 와 겹친다.

</details>

<details><summary>Q2. 재귀 전에 subordinate 를 0xFF 로 써 두는 이유와, 안 쓰면 생기는 증상은?</summary>

Type 1 config 요청은 브리지가 `secondary < B ≤ subordinate` 일 때만 아래로 넘긴다. 재귀는 secondary 아래 브리지들에 번호를 쓰고 그 번호로 읽어야 하는데, 아직 아래에 몇 개가 있을지 모르니 일단 끝(0xFF, 정확히는 그 루트가 가진 마지막 버스)까지 열어 둔다. 다 훑으면 최대값으로 좁힌다. 안 쓰면(리셋값 0) root port 아래 첫 버스조차 안 보여 "root port 만 있고 SSD 가 없음"(B2), sec 로 쓰면 스위치 USP 까지만 보인다(B3). 둘 다 에러 없이 끝나고 재스캔하면 한 단계씩 더 보이는 이상한 모습이 된다. Linux 는 `pci_bus_insert_busn_res(child, next_busnr, bus->busn_res.end)` 로 부모 끝 번호를 임시 subordinate 로 쓰고, dmesg 에 `busn_res: [bus 03-ff] end is updated to 03` 로 좁히는 순간이 찍힌다(실측).

</details>

<details><summary>Q3. 0xFFFF 는 왜 "장치 없음" 이고, 0x0001 이 읽히면 뭔가요? (SSD 펌웨어 관점)</summary>

없는 BDF 로 가는 config 읽기는 아무도 받지 않거나 UR Completion 이 돌아오고, 루트 컴플렉스는 CPU 에 all-1s 를 준다. VID 0xFFFF 는 PCI-SIG 가 할당하지 않는 값이라 안전한 표시다. 0x0001 은 다르다: 장치가 리셋 직후 아직 준비가 안 돼 config 요청에 **CRS(Configuration Request Retry Status, 최신 스펙 용어 RRS)** 로 답하고, 루트 포트의 CRS Software Visibility 가 켜져 있으면 VID 읽기가 0x0001 로 보인다 — "있지만 기다려라" (스펙 확인 권장). Linux 는 `pci_bus_rrs_vendor_id()` 로 이를 보고 `pci_bus_wait_rrs()` 로 재시도한다. SSD 컨트롤러 펌웨어 입장에선 PERST# 해제 후 링크가 올라오고 config 에 응답할 준비가 될 때까지 걸리는 부팅 시간이 이 창 안에 들어와야 한다(리셋 후 config 응답까지의 허용 시간 1 s 계열 규정 — 정확한 수치는 스펙 확인 권장). 너무 늦으면 호스트 BIOS 가 장치를 못 찾고 넘어간다 — "콜드부트에선 안 보이고 웜리셋에선 보이는 SSD" 의 고전적 원인.

</details>

<details><summary>Q4. 멀티 펑션 판정은 정확히 어떻게 하나요? fn0 이 없고 fn1 만 있으면?</summary>

fn0 의 Header Type(0x0E) bit7 이 1 일 때만 fn1..7 을 묻는다. 각 fn 의 자기 bit7 이 아니다. fn 사이 빈칸은 건너뛴다(QEMU NIC 0, 1, 3 실측). fn0 이 없으면 그 device 는 없다고 본다 — 스펙상 다기능 장치는 fn0 을 구현해야 하고, Linux `pci_scan_slot` 도 fn0 이 없으면 멈춘다(예외: 하이퍼바이저가 개별 fn 만 넘겨주는 환경, `hypervisor_isolated_pci_functions()`). 꼬리질문 "ARI 는?" → ARI(Alternative Routing-ID)가 켜진 링크에선 device 번호 5비트까지 fn 번호로 써서 fn 0..255, 다음 fn 은 ARI capability 의 Next Function Number 로 따라간다(`next_ari_fn`) — SR-IOV VF 가 많은 NIC/SSD 에서 쓴다 (PCIE-A06).

</details>

<details><summary>Q5. ARM 보드에서 Linux 는 버스 번호를 직접 매기나요, 펌웨어 값을 쓰나요?</summary>

둘 다 가능하다. DT `pci-host-ecam-generic` 노드의 `reg`(ECAM 창)와 `bus-range`, 또는 ACPI MCFG 가 ECAM 베이스와 버스 범위를 준다 (PCIE-B08). 실측 QEMU virt: `ECAM at [mem 0x4010000000-0x401fffffff] for [bus 00-ff]`. Linux 는 각 브리지의 기존 값을 읽어 유효하면 두고(pass 0), `primary != 버스 || secondary <= 버스 || secondary > subordinate` 면 `"bridge configuration invalid ([bus 00-00]), reconfiguring"` 을 찍고 다시 매긴다(pass 1). `-kernel` 로 바로 부팅한 QEMU 는 펌웨어가 아무것도 안 해 둬서 모든 브리지가 이 메시지를 냈다(실측). UEFI 가 열거한 서버에서는 대개 펌웨어 번호가 유지되고, `PCI_PROBE_ONLY`(DT `linux,pci-probe-only`)면 Linux 는 아무것도 안 바꾼다. 브링업 때 "U-Boot 의 `pci enum` 과 Linux 의 번호가 다르다" 는 흔한 일이고, 버그가 아니다 — 같은 장치를 BDF 로 하드코딩한 스크립트가 버그다.

</details>

<details><summary>Q6. 엔드포인트(SSD 컨트롤러)는 자기 버스 번호를 어떻게 아나요? 왜 알아야 하죠?</summary>

장치가 보내는 모든 요청(DMA 읽기/쓰기 TLP, MSI 쓰기)의 Requester ID = 자기 Bus/Device/Function 이다(PCIE-B10). 그런데 버스 번호는 위에서 정해진다. 그래서 엔드포인트는 자기에게 온 Type 0 Configuration Write 의 대상 Bus/Device 번호를 **잡아 두었다가(capture)** Requester ID 로 쓴다 (스펙 확인 권장: 정확한 조항과 세대별 차이). 펌웨어가 이 값을 잘못 들고 있으면 Completion 이 엉뚱한 곳으로 라우팅되거나 IOMMU 가 "알 수 없는 RID" 로 막는다(PCIE-M15). 재열거/핫플러그로 번호가 바뀌면 다음 config write 에서 다시 잡는다. 스위치·루트 포트는 자기 Type 1 레지스터가 있으니 문제없다.

</details>

<details><summary>Q7. 핫플러그 슬롯이 있는 시스템에서는 버스 번호를 어떻게 정하나요?</summary>

DFS 는 "지금 있는 것" 만큼만 번호를 준다. 빈 슬롯 DSP2 는 [05] 하나를 받고, 바로 다음 06 은 RP2 가 가져간다. 나중에 스위치가 꽂히면 그 아래 버스가 3개 필요한데 [05-05] 안에는 자리가 없다 — 연습 3 에서 `ENUM_ERR_BUSES` 재현. 그래서 hotplug 가능 포트(PCIe Capability Flags 의 Slot Implemented, `PCI_EXP_FLAGS_SLOT 0x0100`)에 버스를 미리 예약한다: Linux `pci=hpbussize=N`, 남는 버스를 hotplug 브리지에 나눠 주는 `pci_scan_child_bus_extend`. 연습 2 에서 4개씩 예약하면 같은 hot-add 가 0b..0e 안에 들어간다. 메모리 창도 같은 문제가 있다(`hpmemsize`, PCIE-A08). 대안은 재열거(전부 다시 번호 매기기)인데 이미 동작 중인 드라이버의 BDF 가 바뀌어 실서비스에선 거의 못 쓴다.

</details>

<details><summary>Q8. 브링업에서 "lspci 에 SSD 가 안 보인다" — 무엇부터 보나요?</summary>

(1) 그 위 포트는 보이나? `lspci -tv` 에서 root port/DSP 가 있고 `[ss-uu]` 가 맞나 — 이 레슨의 검증기 항목(V_PRI…V_OVERLAP). 번호가 이상하면 펌웨어 열거 버그(B2/B3 같은). (2) 포트가 정상이면 링크: 그 포트의 LnkSta `DLActive+`? `Speed/Width` 가 기대값인가 (PCIE-B04/B06, LTSSM 은 PCIE-M07). DLActive- 면 장치가 config 에 응답할 수 없다 — PERST#, REFCLK, 전원, 레인 반전/극성 확인. (3) 링크는 올라왔는데 안 보이면 장치가 CRS 중(준비 안 됨, Q3)이거나 열거 시점이 장치 준비보다 빨랐다 → 재스캔 `echo 1 > /sys/bus/pci/rescan`. (4) 보이는데 드라이버가 안 붙으면 class/VID/DID 와 BAR(Mem- 이면 PCIE-B03/B05). 순서가 "토폴로지 → 링크 → 타이밍 → 드라이버" 인 이유: 아래 단계가 깨지면 위 단계 데이터는 전부 0xFFFF 다.

</details>

## 9. 추가 연습

### 연습 1 — only_one_child: 점대점 링크는 dev 0 만

브리지의 PCIe Capability(legacy 리스트에서 ID 0x10, 홉 제한 — PCIE-B04)의 `PCI_EXP_FLAGS_TYPE` 이 Root Port(4) 또는 Downstream Port(6)면 그 아래 버스는 dev 0 만 묻는 `scan_bus_x(..., only_dev0)` 를 짜라. 토폴로지 A 에서 결과는 같고 config 읽기는 줄어야 한다.

<details><summary>답</summary>

포트 타입은 Flags 의 비트 7:4 (`(flags & 0x00f0) >> 4`, pci_regs.h 기준 ROOT_PORT 0x4, UPSTREAM 0x5, DOWNSTREAM 0x6). USP 아래(스위치 내부 버스)는 여러 dev 가 있으니 묻는다. 실측: 전체 스캔 config 읽기 **289번(all-1s 219)** → only_one_child **134번(all-1s 64)**, 결과 표는 동일. 실제 칩에서 all-1s 읽기는 UR 을 기다리는 non-posted 요청이라 가장 비싼 읽기다(타임아웃까지 가는 플랫폼도 있다). Linux `only_one_child()` 주석: "A PCIe Downstream Port normally leads to a Link with only Device 0 on it (PCIe spec r3.1, sec 7.3.1). As an optimization, scan only for Device 0". 예외 플래그 `PCI_SCAN_ALL_PCIE_DEVS` 는 특이한 토폴로지용.

</details>

### 연습 2 — hotplug 슬롯에 버스 예약

Slot Implemented 비트가 있는 브리지는 최소 N 개 버스(`sec .. sec+N-1`)를 차지하게 하라: 재귀 결과 `child_max` 가 작으면 `sec + N - 1` 로 (단 `last_bus` 를 넘지 않게). N = 4 로 토폴로지 A 의 번호를 손으로 계산하고 코드와 비교하라.

<details><summary>답</summary>

QEMU 실측에서 root port 와 DSP 는 Slot 비트 1, USP 는 0 이다(덤프의 PCIe cap flags). 손 계산: RP1 sec 01 → USP sec 02 → DSP0 sec 03, NVMe 하나뿐이라 child_max 03 → 예약으로 06; DSP1 sec 07 → 0a; DSP2 sec 0b → 0e; USP 는 slot 없음 → sub = 0e; RP1 = max(0e, 01+3) = 0e; RP2 sec 0f → 12. 코드 결과와 같고 검증기 V_OK. 트리 (실측 출력):

```text
-[0000:00]-+-00.0  Red Hat, Inc. QEMU PCIe Host bridge
           +-01.0-[01-0e]----00.0-[02-0e]--+-00.0-[03-06]----00.0  Red Hat, Inc. QEMU NVM Express Controller
           |                               +-01.0-[07-0a]--+-00.0  Intel Corporation 82574L Gigabit Network Connection
           |                               |               +-00.1  Intel Corporation 82574L Gigabit Network Connection
           |                               |               \-00.3  Intel Corporation 82574L Gigabit Network Connection
           |                               \-02.0-[0b-0e]--
           \-02.0-[0f-12]----00.0  Device 1234:11e8
```

NIC 가 bus 07 로 옮겨 간 것에 주의 — 예약 정책을 바꾸면 BDF 가 바뀐다. Linux 의 실제 정책(`hpbussize` 기본 1, 남은 버스 분배)은 이 단순 모델과 다를 수 있다.

</details>

### 연습 3 — 빈 슬롯에 스위치 hot-add 후 부분 rescan

열거가 끝난 시스템의 DSP2(02:02.0) 아래 링크에 스위치(USP + DSP 2개, 첫 DSP 아래 SSD)를 꽂고, **다른 브리지 번호는 건드리지 않고** DSP2 의 `[sec..sub]` 안에서만 다시 훑는 `rescan_bridge()` 를 짜라. (a) 예약 없이 열거한 시스템, (b) 연습 2 처럼 4개 예약한 시스템.

<details><summary>답</summary>

`rescan_bridge` = `last_bus = 그 브리지의 sub` 로 `scan_bus(sec)` 를 부르고 결과를 표 뒤에 붙인다. 그 브리지의 subordinate 는 다시 쓰지 않는다(예약 유지). 실측: (a) DSP2 [05-05] → 새 USP 는 보이지만(bus 05 위) secondary 06 을 줄 수 없어 `ENUM_ERR_BUSES`, 새 DSP 와 SSD 는 안 보이고 RP2 [06] 은 그대로. (b) DSP2 [0b-0e] → 새 USP 0b:00.0 [0c-0e], DSP 0c:00.0 [0d], 0c:01.0 [0e], 새 SSD **0d:00.0**, 경로 길이 5, 전체 검증 V_OK. 실제 Linux 에서 (a) 에 해당하는 경우 `pci_err(dev, "No bus number available for hot-added bridge\n")` 를 찍고 그 아래를 포기한다 (6.18.55 `drivers/pci/probe.c`, 소스로 확인 — 실행 재현은 안 함). 참고로 Linux `pci_hotplug_bus_size` 기본값은 `DEFAULT_HOTPLUG_BUS_SIZE 1` (`drivers/pci/pci.c`) 이라, 따로 `pci=hpbussize=` 를 주지 않으면 이 레슨의 (a) 와 비슷한 상황이 된다(남는 버스를 나눠 주는 `_extend` 로직이 있어 정확히 같지는 않다).

</details>

### 연습 4 — 재귀 없는 DFS

펌웨어 스택이 작다고 하자. 프레임 `{bus, dev, fn, nfn, max, parent, br}` 의 명시적 스택(256)으로 같은 열거를 하라. 재귀판과 결과 표, 그리고 **하드웨어에 쓴 값**이 정상 / 고갈(`last_bus = 4`) / 버그(`SUB_TMP_SEC`) 옵션 모두에서 토폴로지 A, B 둘 다 같아야 한다.

<details><summary>답</summary>

브리지를 만나면 pri/sec/임시 sub 를 쓰고 `f->br = idx` 를 기억한 채 자식 프레임 `{sec, dev 0, fn 0, max = sec, parent = idx}` 를 push. 프레임의 dev 가 32 에 닿으면(버스 끝) pop 해서 부모 프레임의 `br` 에 `sub = 자식 max` 를 쓰고 `max` 갱신 후 다음 fn 으로. fn 진행 규칙은 같다: fn0 없으면 dev++, 있으면 `nfn` 결정, `++fn >= nfn` 이면 다음 dev. 3 옵션 × 2 토폴로지 = 6 경우 모두 `same_result` + 모의 하드웨어 노드·링크 바이트 `memcmp` 가 같다. 스택 깊이 상한 = 버스 번호 수라 정적 배열 256 이면 충분하다(프레임 약 16바이트 → 4KB, 재귀판 프레임보다 작고 무엇보다 **상한이 보인다**).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 20/20 passed
```

```text
P1 topology A: full scan 289 config reads (219 all-1s), one_child 134 reads (64 all-1s)
P2 hp_reserve=4 tree:
... (연습 2 답의 트리)
P3 hot-add under DSP2 (02:02.0): no reserve -> ENUM_ERR_BUSES; reserve 4 -> new SSD at 0d:00.0, path depth 5
20/20 passed
```

## 10. 핵심 정리

- 브리지 Type 1 0x18/0x19/0x1A = Primary/Secondary/Subordinate. Type 1 config(버스 B): B == sec → Type 0 로 아래 링크, sec < B ≤ sub → 아래로, 그 외 → 내 것 아님(UR → 0xFFFFFFFF). 리셋값 0 이라 처음엔 루트 버스만 보인다.
- DFS: `sec = max+1`, pri/sec/**sub=0xFF(last_bus)** 쓰기 → 재귀 → `sub = 자식 최대`, `max` 갱신. 전위 순서라 모든 서브트리가 연속 구간 — BFS 로는 안 된다.
- 임시 sub 를 안 쓰면 root port 만, sec 로 쓰면 한 단계만 보인다. 둘 다 에러 없이 끝나 "SSD 가 없음/재스캔하면 보임" 으로 나타난다.
- 함수: VID 0xFFFF(/0x0000) = 없음, fn0 없으면 device 없음, fn1..7 은 fn0 Header Type bit7 일 때만, 빈 fn 건너뛰기, 브리지 판정은 `& 0x7f == 1`. Downstream/Root port 아래는 dev 0 만(only_one_child).
- 버스 범위(`bus-range`, MCFG, ECAM 크기 = 버스당 1MB)를 넘으면 그 브리지는 건너뛰고 오류 보고. 255+1 = 0 오버플로 주의. 핫플러그 슬롯엔 버스 예약.
- `lspci -tv` 는 버스 번호로 트리를 재구성한다 — 번호가 틀리면 트리가 틀린다. config 를 따라가는 코드는 전부 깊이/홉 제한.
- 실측: Linux 6.18.55 가 QEMU 토폴로지에 쓴 번호·순서(`busn_res ... end is updated to`)와 이 레슨 DFS 결과가 같다.

## 참고자료

- PCI-SIG, *PCI Express Base Specification* (Rev. 4.0 이상) — Type 1 Configuration Space Header(Primary/Secondary/Subordinate Bus Number), Type 0/1 configuration request 라우팅, Header Type 의 Multi-Function Device 비트, ARI, CRS/RRS, Device Number 0 on a Link. 절 번호는 판마다 다르므로 원문 확인 권장.
- PCI-SIG, *PCI-to-PCI Bridge Architecture Specification* Rev. 1.2 — 버스 번호 레지스터와 Type 1 → Type 0 변환 규칙의 원조.
- Linux 6.18.55 `drivers/pci/probe.c` — `pci_scan_bridge_extend()` (임시 subordinate, "bridge configuration invalid", 한 번의 dword 쓰기), `pci_scan_slot()` / `next_fn()` (fn0 필수, multifunction), `only_one_child()`, `pci_bus_generic_read_dev_vendor_id()` (0xFFFF/0x0000 판정, RRS). Lab 소스: `/var/lab/src/linux-6.18.55/`.
- Linux `include/uapi/linux/pci_regs.h` — `PCI_HEADER_TYPE 0x0e`, `PCI_HEADER_TYPE_MFD 0x80`, `PCI_PRIMARY_BUS 0x18`, `PCI_SECONDARY_BUS 0x19`, `PCI_SUBORDINATE_BUS 0x1a`, `PCI_EXP_FLAGS_TYPE 0x00f0`, `PCI_EXP_FLAGS_SLOT 0x0100` (이 레슨 상수는 이 헤더 값으로 확인).
- pciutils 3.14.0 `ls-tree.c` — `lspci -t` 트리 재구성(`grow_tree`, `insert_dev`, `show_tree_*`, `print_it`): <https://github.com/pciutils/pciutils>
- Linux 커널 파라미터 문서 `pci=hpbussize=`, `pci=realloc` — Documentation/admin-guide/kernel-parameters.txt.
- QEMU `docs/pcie.txt` — pcie-root-port, x3130 스위치(USP/DSP)로 PCIe 토폴로지 만들기.
- Ravi Budruk 외, *PCI Express System Architecture* (MindShare) — Configuration/Enumeration 장 (DFS 열거 예제).
- 이 레슨 실측 파일: `capture_qemu_enum.sh` (재현 스크립트), `capture_qemu_enum.txt` (lspci -tv, dmesg, 브리지 레지스터), `capture_lspci_tree_b.txt` (토폴로지 B 의 lspci -F 교차 확인).
- 관련 레슨: PCIE-B03 (Type 0 헤더), PCIE-B04 (capability·LnkSta), PCIE-B05 (BAR), PCIE-B06 (lspci/sysfs), PCIE-B07 (Type 1 창 라우팅), PCIE-B08 (ECAM), PCIE-B10 (Requester ID), PCIE-M07/M08 (LTSSM·링크 장애), PCIE-M15 (IOMMU), PCIE-A06 (SR-IOV/ARI), PCIE-A07 (hot-plug), PCIE-A08 (브리지 창 할당).
