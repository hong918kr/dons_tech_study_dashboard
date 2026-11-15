---
id: ELX-B05
time: 70분
updated: 2026-11-15
---
# ELX-B05 Device Tree 읽기·쓰기: DTS, DTB, dtc

## 학습 목표

- 가상 보드 DTS를 직접 써서 **노드 / 프로퍼티 / label / phandle / `#address-cells`·`#size-cells`** 규칙을 손으로 설명할 수 있다.
- `dtc` 로 DTS → DTB 컴파일, DTB → DTS 역컴파일을 하고, `fdtdump`·`fdtget` 출력으로 바이너리를 확인한다.
- **FDT(Flattened Device Tree) 바이너리 구조** — 40바이트 헤더, memory reservation 블록, structure 블록(토큰 스트림), strings 블록 — 를 바이트 단위로 읽는다.
- libfdt 없이 C로 FDT 파서를 짜서 `dtc -I dtb -O dts` 결과와 **바이트 단위로 같은** 출력을 내고, 망가진 blob 에는 절대 죽지 않고 에러를 돌려준다 (ASan 깨끗).
- 파서로 bring-up 질문에 답한다: "이 센서의 `reg` 는 무슨 뜻이고, 인터럽트는 어느 컨트롤러로 가나?"

## 1. 문제

보드 bring-up 에서 "드라이버가 probe 를 안 한다 / IRQ 가 안 온다"의 절반은 **Device Tree 문제**다. 그래서 임베디드 리눅스 면접은 "DTB 포맷을 설명해 보라", "`#address-cells` 는 누구 것을 쓰나", "phandle 이 뭔가"를 거의 항상 묻고, 펌웨어 면접에선 "이 바이너리 blob 을 파싱하는 코드를 짜라"로 변형된다.

Device Tree 는 **하드웨어 설명서**다. 커널 이미지는 여러 보드에서 같고, "이 보드엔 UART 가 0x09000000 에 있고 I2C 버스 0x48 에 온도 센서가 있다"는 정보만 DT 로 따로 넘긴다. 흐름은 이렇다 (부트 흐름 전체는 ELX-B01):

- 사람이 쓴 **DTS**(Device Tree Source, 텍스트) → `dtc`(Device Tree Compiler) → **DTB**(Device Tree Blob, 바이너리 = FDT 포맷)
- 부트로더(U-Boot)가 DTB 를 RAM 에 올리고 `/chosen` 에 `bootargs` 를 써 넣은 뒤 커널에 주소를 넘긴다. arm64 에선 **x0 = DTB 물리주소**, 8바이트 정렬, 최대 2 MB (`Documentation/arch/arm64/booting.rst` 53·165행).
- 커널은 부팅 초기에 blob 을 직접 걸어 다니며 `/chosen`·`/memory` 를 읽고(`drivers/of/fdt.c` 의 `early_init_dt_scan_chosen`, `early_init_dt_scan_memory`), 나중에 `unflatten_device_tree()` 로 포인터 트리(`struct device_node`)를 만든다. 드라이버 매칭은 ELX-M01, overlay·스키마는 ELX-A05.

이번 레슨의 가상 보드 `board.dts` (CPU 2개, RAM 512 MiB, GIC, UART, GPIO, I2C 컨트롤러 + 온도센서 @0x48 + EEPROM @0x50, 꺼진 이더넷):

<!-- code: board.dts -->

```bash
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B05 && dtc -I dts -O dtb -o /tmp/ELX-B05_board.dtb board.dts && cmp /tmp/ELX-B05_board.dtb board.dtb && dtc -I dtb -O dts /tmp/ELX-B05_board.dtb | head -12
```

```svg
<svg viewBox="0 0 760 370" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="eb05-a1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">board.dts 의 트리 — 실선 = 부모/자식, 점선 화살표 = phandle 참조</text>
  <g stroke="currentColor" fill="none">
    <line x1="380" y1="56" x2="50" y2="84"/><line x1="380" y1="56" x2="140" y2="84"/><line x1="380" y1="56" x2="225" y2="84"/>
    <line x1="380" y1="56" x2="335" y2="84"/><line x1="380" y1="56" x2="510" y2="84"/><line x1="380" y1="56" x2="680" y2="84"/>
    <line x1="225" y1="110" x2="45" y2="164"/><line x1="225" y1="110" x2="125" y2="164"/>
    <line x1="680" y1="110" x2="257" y2="164"/><line x1="680" y1="110" x2="387" y2="164"/>
    <line x1="680" y1="110" x2="512" y2="164"/><line x1="680" y1="110" x2="680" y2="164"/>
    <line x1="512" y1="200" x2="470" y2="254"/><line x1="512" y1="200" x2="620" y2="254"/>
  </g>
  <rect x="330" y="30" width="100" height="26" rx="5" class="f-acc" stroke="currentColor"/>
  <g stroke="currentColor" class="f-mut">
    <rect x="10" y="84" width="80" height="26" rx="5"/><rect x="100" y="84" width="80" height="26" rx="5"/>
    <rect x="190" y="84" width="70" height="26" rx="5"/><rect x="270" y="84" width="130" height="26" rx="5"/>
    <rect x="620" y="84" width="120" height="26" rx="5"/>
    <rect x="10" y="164" width="70" height="26" rx="5"/><rect x="90" y="164" width="70" height="26" rx="5"/>
    <rect x="200" y="164" width="115" height="36" rx="5"/><rect x="455" y="164" width="115" height="36" rx="5"/>
    <rect x="565" y="254" width="110" height="26" rx="5"/>
  </g>
  <rect x="404" y="78" width="212" height="38" rx="5" class="f-ok" stroke="currentColor"/>
  <rect x="330" y="164" width="115" height="36" rx="5" class="f-ok" stroke="currentColor"/>
  <rect x="610" y="164" width="140" height="36" rx="5" class="f-warn" stroke="currentColor"/>
  <rect x="405" y="254" width="130" height="36" rx="5" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="380" y="48" font-family="monospace">/</text>
    <text x="50" y="102">chosen</text><text x="140" y="102">aliases</text><text x="225" y="102">cpus</text>
    <text x="335" y="102" font-family="monospace">memory@40000000</text>
    <text x="510" y="94" font-family="monospace">interrupt-controller@8000000</text>
    <text x="510" y="110">gic: · phandle = 1 · 셀 3개</text>
    <text x="680" y="102" font-family="monospace">soc@0 (1/1)</text>
    <text x="45" y="182" font-family="monospace">cpu@0</text><text x="125" y="182" font-family="monospace">cpu@1</text>
    <text x="257" y="180" font-family="monospace">serial@9000000</text><text x="257" y="195">uart0: (상속)</text>
    <text x="387" y="180" font-family="monospace">gpio@9030000</text><text x="387" y="195">gpio0: · phandle = 2</text>
    <text x="512" y="180" font-family="monospace">i2c@9040000</text><text x="512" y="195">i2c0: · (1/0)</text>
    <text x="680" y="180" font-family="monospace">ethernet@9050000</text><text x="680" y="195">status = "disabled"</text>
    <text x="470" y="270" font-family="monospace">temp-sensor@48</text><text x="470" y="285">interrupts = &lt;5 2&gt;</text>
    <text x="620" y="272" font-family="monospace">eeprom@50</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-dasharray="5 4" stroke-width="1.5">
    <path d="M430 40 C 470 40, 500 50, 505 76" marker-end="url(#eb05-a1)"/>
    <path d="M415 262 C 360 262, 360 230, 375 202" marker-end="url(#eb05-a1)"/>
  </g>
  <g fill="currentColor">
    <text x="440" y="36">interrupt-parent = &lt;1&gt;</text>
    <text x="250" y="240">interrupt-parent = &lt;2&gt;</text>
    <text x="10" y="318">(1/1) = 그 노드의 #address-cells / #size-cells → 자식의 reg 를 몇 셀씩 끊어 읽을지 정한다.</text>
    <text x="10" y="338">serial·i2c·eeprom 은 interrupt-parent 가 없다 → 조상으로 올라가 루트의 &lt;1&gt;(GIC)을 상속.</text>
    <text x="10" y="358">aliases 의 &amp;uart0 은 phandle 이 아니라 경로 문자열 "/soc@0/serial@9000000" 로 컴파일된다.</text>
  </g>
</svg>
```

### Part A — FDT 헤더 읽고 검증하기 (`fdt_open`)

blob 앞 40바이트(v17 헤더)를 big-endian 으로 읽어 `fdt_t` 에 채운다. 그리고 **믿지 말고 검사**한다: 길이 < 40 → `SHORT`, magic ≠ `0xd00dfeed` → `MAGIC`, version < 17 또는 last_comp_version > 17 → `VERSION`, totalsize > 버퍼 → `TRUNC`, reservation 블록 8바이트·structure 블록 4바이트 정렬 위반 → `ALIGN`, 블록이 totalsize 밖 → `TRUNC`.

### Part B — 토큰을 걸어 다니며 `dtc` 와 똑같이 출력하기 (`fdt_next`, `guess_type`, `fdt_to_dts`)

- `fdt_next`: structure 블록에서 토큰 하나(BEGIN_NODE / PROP / END_NODE / NOP / END)를 읽고 다음 4바이트 정렬 위치로 간다. 프로퍼티 이름은 **strings 블록의 오프셋(nameoff)** 으로 찾는다. 어떤 입력에도 블록 밖을 읽지 않는다.
- `guess_type`: DTB 엔 타입이 없다. dtc 역컴파일러와 같은 규칙으로 문자열 / 셀 / 바이트를 추측한다.
- `fdt_to_dts`: `/dts-v1/;`, `/memreserve/` 줄, 노드 트리를 `dtc -I dtb -O dts` 와 **바이트 단위로 같게** 출력한다. BEGIN/END 짝이 안 맞으면 `NEST`.
- 테스트: 골든(진짜 dtc 출력) 비교 + 손으로 망가뜨린 blob 9종 + NOP 으로 지운 프로퍼티 + 1889가지 자르기·1889가지 바이트 뒤집기 퍼징.

### Part C — bring-up 질문에 답하기 (`fdt_reg`, `fdt_node_by_phandle`, `fdt_irq_parent`)

상황: "TMP102 온도센서 드라이버가 알람 IRQ 를 못 받는다. 손에 있는 건 보드에 올라간 DTB 하나." DTB 만으로 답을 낸다.

- `fdt_reg(path, idx)` — `reg` 의 idx 번째 (주소, 크기). 셀 개수는 **부모 노드의** `#address-cells`/`#size-cells` (없으면 2/1).
- `fdt_node_by_phandle(ph)` — `phandle` 값으로 노드 찾기.
- `fdt_irq_parent(path)` — 자기 → 루트로 올라가며 처음 만나는 `interrupt-parent` 를 따라간 인터럽트 컨트롤러의 경로.

## 2. Problem (English)

> **Parse a Flattened Device Tree blob without libfdt.**
>
> A DTB starts with a 40-byte big-endian header: `magic (0xd00dfeed), totalsize, off_dt_struct, off_dt_strings, off_mem_rsvmap, version (17), last_comp_version (16), boot_cpuid_phys, size_dt_strings, size_dt_struct`. The memory reservation block is a list of `(u64 address, u64 size)` pairs ending with `(0, 0)`. The structure block is a stream of 4-byte-aligned tokens: `FDT_BEGIN_NODE (1)` + NUL-terminated name, `FDT_PROP (3)` + `u32 len` + `u32 nameoff` + `len` bytes of value, `FDT_END_NODE (2)`, `FDT_NOP (4)`, `FDT_END (9)`. Property names live in the strings block at `nameoff`.
>
> 1. `int fdt_open(fdt_t *f, const uint8_t *blob, size_t len)` — validate the header; return a distinct negative error for short buffer, bad magic, unsupported version, truncation, and misalignment.
> 2. `int fdt_next(const fdt_t *f, uint32_t *pos, fdt_tok_t *t)` and `int fdt_to_dts(const fdt_t *f, char *out, size_t cap)` — walk the tokens and print the tree exactly like `dtc -I dtb -O dts`. Never read outside the blob, whatever the input (fuzzed, truncated, `len = 0xfffffff0`).
> 3. `int fdt_reg(f, path, idx, &addr, &size)`, `int fdt_node_by_phandle(f, ph, &node)`, `int fdt_irq_parent(f, path, out, cap)` — decode `reg` using the **parent's** cell counts, resolve phandles, and find the interrupt parent (inherited from ancestors).
>
> Example: `fdt_reg("/soc@0/i2c@9040000/temp-sensor@48", 0)` → `addr = 0x48, size = 0` (parent has `#size-cells = <0>`); `fdt_irq_parent("/soc@0/serial@9000000")` → `"/interrupt-controller@8000000"`.
> Follow-ups: Why is the header big-endian? Why are property names in a separate block? What breaks if the parser trusts `len`?

## 3. 시작 코드

<!-- code: starter.c -->

DTB 는 **C 배열로 임베드**했다: `board_dtb.h` 에 `board_dtb[1889]` (board.dtb 바이트) 와 `board_dts_golden` (진짜 `dtc -I dtb -O dts` 출력 문자열)이 들어 있고, `make_blob.sh` 가 `board.dts` 에서 다시 만든다 (손으로 고치지 말 것). 그래서 실행 위치(cwd)와 상관없이 테스트가 돈다. 파일 인자를 주면 그 DTB 를 읽어 출력하는 모드가 된다 (`dtc_compare.sh` 가 사용).

```bash
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B05 && export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk && cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/ELX-B05_starter && /tmp/ELX-B05_starter
# FAIL 줄이 40개, 마지막에 "10/50 passed", exit 1 — TODO 7개를 채워 50/50 을 만든다
```

| 파일 | 역할 |
|---|---|
| `board.dts` / `board.dtb` | 가상 보드 소스와 컴파일 결과 (1889 B) |
| `board_golden.dts` | `dtc -I dtb -O dts board.dtb` 진짜 출력 (104줄) |
| `board_dtb.h`, `make_blob.sh` | 위 둘을 C 배열/문자열로 만든 헤더와 생성 스크립트 |
| `dtc_compare.sh` (+ `dtc_compare_solution.txt`) | 내 파서 vs dtc 를 진짜 DTB 9종으로 비교 |
| `demo_bad.dts` (+ `demo_bad_dtc.txt`) | 흔한 DTS 실수와 dtc 경고 (§7) |

## 4. 힌트

<details>
<summary>힌트 1 — big-endian 읽기는 바이트 시프트로</summary>

`*(uint32_t *)p` + `ntohl` 은 정렬 안 된 주소에서 UB 이고, Cortex-M0 같은 코어에선 HardFault 다. 주어진 `be32(p)` 는 `p[0]<<24 | p[1]<<16 | p[2]<<8 | p[3]` — 어떤 주소에서도 안전하다.

</details>

<details>
<summary>힌트 2 — 경계 검사는 64비트로 더해서</summary>

`p + 12 + len > end` 를 `uint32_t` 로 계산하면 `len = 0xfffffff0` 에서 덧셈이 넘쳐(wrap) 작은 수가 되고 검사를 통과해 버린다. `(uint64_t)p + 12 + len > end` 처럼 한쪽을 넓혀서 더한다.

</details>

<details>
<summary>힌트 3 — 다음 토큰 위치</summary>

BEGIN_NODE: `align4(p + 4 + strlen(name) + 1)`. PROP: `align4(p + 12 + len)` — `len` 은 패딩 **전** 길이다. 나머지 셋은 `p + 4`. 이름의 NUL 은 `memchr` 로 structure 블록 끝까지만 찾는다.

</details>

<details>
<summary>힌트 4 — dtc 출력 모양</summary>

맨 앞 `"/dts-v1/;\n\n"`, reservation 하나마다 `"/memreserve/\t0x%016llx 0x%016llx;\n"`, 루트는 `"/ {\n"`. **모든 자식 노드 앞에 빈 줄 하나**, 들여쓰기는 탭, 셀은 `0x%02x`, 바이트는 `%02x`, 빈 값은 `name;`.

</details>

<details>
<summary>힌트 5 — reg 는 부모의 셀 개수로</summary>

`fdt_find` 가 주는 `stack[d-1]` 이 그 노드, `stack[d-2]` 가 부모다. 부모의 `#address-cells`(기본 2)와 `#size-cells`(기본 1)로 한 항목 = `(ac+sc)*4` 바이트. `reg` 길이가 그 배수가 아니면 `BADCELLS`.

</details>

## 5. 풀이

### 5.0 DTS 문법 1분 정리

| DTS | 뜻 | DTB 에서는 |
|---|---|---|
| `i2c@9040000 { ... };` | 노드 = `이름@unit-address`. unit-address 는 `reg` 첫 주소(16진, 0x 없이) | BEGIN_NODE + `"i2c@9040000\0"` … END_NODE |
| `gic: interrupt-controller@8000000` | **label** — 소스 안에서만 쓰는 별명 | 사라진다 (`-@` 를 주면 `__symbols__` 노드로 남음) |
| `interrupt-parent = <&gic>;` | 셀 안의 `&label` = **phandle** 참조 | 대상 노드에 `phandle = <1>` 자동 추가, 여기엔 `<0x01>` |
| `serial0 = &uart0;` | 셀 밖의 `&label` = **경로** 참조 | 문자열 `"/soc@0/serial@9000000"` |
| `<0x0 0x40000000>` / `"a", "b"` / `[02 00 5e]` | 32비트 셀 / 문자열 목록 / 바이트열 | 전부 그냥 바이트 — 타입 정보 없음 |
| `interrupt-controller;` | 값 없는 프로퍼티 (불리언) | PROP len = 0 |
| `/memreserve/ 0x5ff00000 0x100000;` | 커널이 쓰지 말아야 할 RAM | reservation 블록 (u64, u64) |

Mac 의 `fdtget` 으로 값만 뽑아 볼 수 있다 (진짜 출력):

```text
$ fdtget -t x board.dtb /soc@0/serial@9000000 reg
9000000 1000
$ fdtget board.dtb /soc@0/serial@9000000 compatible
arm,pl011 arm,primecell
$ fdtget -t x board.dtb /soc@0/i2c@9040000/temp-sensor@48 interrupt-parent
2
$ fdtget -l board.dtb /soc@0
serial@9000000
gpio@9030000
i2c@9040000
ethernet@9050000
```

### Part A — 헤더: 10개 필드, 4개 블록

`xxd -g 4 -l 0x90 board.dtb` 의 진짜 출력과 `fdtdump` 값:

```text
00000000: d00dfeed 00000761 00000048 00000664  .......a...H...d
00000010: 00000028 00000011 00000010 00000000  ...(............
00000020: 000000fd 0000061c 00000000 5ff00000  ............_...
00000030: 00000000 00100000 00000000 00000000  ................
00000040: 00000000 00000000 00000001 00000000  ................
00000050: 00000003 00000016 00000000 45534e20  ............ESN 
00000060: 42303520 56697274 75616c20 426f6172  B05 Virtual Boar
00000070: 64000000 00000003 0000000e 00000006  d...............
00000080: 65736e2c 6230352d 626f6172 64000000  esn,b05-board...
```

| 오프셋 | 필드 | board.dtb 값 | 검사 |
|---|---|---|---|
| 0x00 | magic | `0xd00dfeed` | ≠ → `MAGIC` |
| 0x04 | totalsize | 0x761 = 1889 | > 버퍼 길이 → `TRUNC` |
| 0x08 | off_dt_struct | 0x48 | 4의 배수 아님 → `ALIGN` |
| 0x0c | off_dt_strings | 0x664 | + size 가 totalsize 밖 → `TRUNC` |
| 0x10 | off_mem_rsvmap | 0x28 | 8의 배수 아님 → `ALIGN` |
| 0x14 / 0x18 | version / last_comp_version | 17 / 16 | version < 17, last_comp > 17 → `VERSION` |
| 0x1c | boot_cpuid_phys | 0 | 부팅 CPU 의 `reg` |
| 0x20 / 0x24 | size_dt_strings / size_dt_struct | 0xfd = 253 / 0x61c = 1564 | v17 에서 생긴 필드 |

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="eb05-a2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">board.dtb (1889 B) — 블록 배치와 structure 블록의 첫 토큰들</text>
  <rect x="60" y="34" width="150" height="40" class="f-acc" stroke="currentColor"/>
  <rect x="60" y="74" width="150" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="60" y="108" width="150" height="150" class="f-ok" stroke="currentColor"/>
  <rect x="60" y="258" width="150" height="40" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="135" y="52">header 40 B</text><text x="135" y="67" font-family="monospace">d00dfeed</text>
    <text x="135" y="95">mem_rsvmap 32 B</text>
    <text x="135" y="180">structure 1564 B</text><text x="135" y="196">토큰 스트림</text>
    <text x="135" y="276">strings 253 B</text><text x="135" y="291">이름 목록</text>
  </g>
  <g fill="currentColor" text-anchor="end" font-family="monospace">
    <text x="55" y="40">0x000</text><text x="55" y="80">0x028</text><text x="55" y="114">0x048</text>
    <text x="55" y="264">0x664</text><text x="55" y="302">0x761</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="240" y="50" font-weight="bold">blob   단어(big-endian)          뜻</text>
    <text x="240" y="74">0x048  00000001 00000000         BEGIN_NODE "" (루트)</text>
    <text x="240" y="96">0x050  00000003 00000016 00000000 PROP len=22 nameoff=0</text>
    <text x="240" y="118">0x05c  "ESN B05 Virtual Board\0"   값 22 B + 패딩 2</text>
    <text x="240" y="140">0x074  00000003 0000000e 00000006 PROP len=14 nameoff=6</text>
    <text x="240" y="162">0x080  "esn,b05-board\0"           값 14 B + 패딩 2</text>
    <text x="240" y="184">…      (프로퍼티 → 자식 노드 → END_NODE)</text>
    <text x="240" y="206">0x660  00000009                  END (structure 끝)</text>
    <text x="240" y="264">0x664  model\0compatible\0#address-cells\0…</text>
    <text x="240" y="282">       ↑0     ↑6</text>
  </g>
  <path d="M590 100 C 640 140, 640 220, 300 255" stroke="currentColor" fill="none" stroke-dasharray="5 4" marker-end="url(#eb05-a2)"/>
  <text x="560" y="230" fill="currentColor">nameoff = strings 블록 안 오프셋</text>
  <text x="10" y="318" fill="currentColor">프로퍼티 이름은 한 번만 저장 — "reg"·"compatible" 이 수십 번 나와도 strings 블록엔 한 개.</text>
</svg>
```

검사 순서에 이유가 있다. magic 이 틀리면 나머지 필드는 의미가 없고, totalsize 를 버퍼 길이와 먼저 맞춰 둬야 이후의 "블록이 totalsize 안에 있나" 검사가 **실제 메모리 안**이라는 뜻이 된다. 커널·U-Boot 가 쓰는 libfdt 의 `fdt_check_header()` (`scripts/dtc/libfdt/fdt.c` 89행)도 같은 구조다: blob 주소 8바이트 정렬 → magic → version(지원 범위 2~17, `version < last_comp_version` 도 거부) → totalsize → rsvmap/struct/strings 경계를 `-FDT_ERR_TRUNCATED` 로. 우리 파서는 바이트 단위로 읽어서 blob 주소 정렬은 요구하지 않는다.

- **왜 version ≥ 17 만?** `size_dt_struct` 는 v17 에서 생겼다. libfdt 의 `FDT_V16_SIZE` 는 36바이트(9필드)다. 실제로 `dtc -V 16` 으로 만든 DTB 는 fdtdump 가 `size_dt_struct` 줄을 안 찍고, 우리 파서는 `parse error -3` 으로 거부한다 (진짜 실행). libfdt 는 v16 도 읽는데 structure 블록 끝을 END 토큰으로 찾는다.
- **왜 big-endian?** Device Tree 는 Open Firmware(IEEE 1275, PowerPC/SPARC 시절)에서 왔고 그 바이트 순서를 그대로 쓴다. 리틀 엔디언 ARM 에서는 매번 바이트를 뒤집어 읽는다 (커널은 `be32_to_cpu`).
- **오프셋은 항상 헤더에서.** dtc 출력은 rsvmap 0x28, struct 0x48 이지만 QEMU virt 가 만든 DTB 는 rsvmap **0x30**, struct **0x40**, totalsize **1 MiB** 다 (`dtc_compare.sh` 가 덤프한 진짜 값). "헤더 바로 뒤"를 가정하면 깨진다.

### Part B — 토큰 스트림 걷기

| 토큰 | 값 | 뒤따르는 것 | 다음 위치 |
|---|---|---|---|
| `FDT_BEGIN_NODE` | 1 | 이름 + NUL (루트는 빈 이름) | `align4(p + 4 + strlen + 1)` |
| `FDT_PROP` | 3 | `u32 len`, `u32 nameoff`, 값 len 바이트 | `align4(p + 12 + len)` |
| `FDT_END_NODE` | 2 | — | `p + 4` |
| `FDT_NOP` | 4 | — (무시) | `p + 4` |
| `FDT_END` | 9 | — (structure 끝) | 끝 |

위 hexdump 로 손으로 따라가면: 0x48 BEGIN_NODE, 이름 `""` → NUL 1바이트를 4로 패딩 → 0x50 PROP, len 0x16(22), nameoff 0 → strings[0] = `"model"`, 값 22바이트를 24로 패딩 → 0x74 PROP, len 14, nameoff 6 → `"compatible"`. 테스트의 `pos == 8`, `pos == 44` (structure 블록 기준)가 바로 이 숫자다.

`fdt_next` 의 방어선은 4개다. ① 태그 4바이트가 블록 안인가 ② 이름의 NUL 을 블록 끝까지만 `memchr` ③ `(uint64_t)p + 12 + len` 이 블록 안인가 (32비트 덧셈 overflow 차단) ④ `nameoff < size_strings` 이고 그 뒤에 NUL 이 있는가. libfdt 의 `fdt_next_tag()` (`fdt.c` 162행)도 똑같이 `len + offset` 의 wrap 을 검사하고 문제가 있으면 `FDT_END` 를 돌려 "조기 종료"로 처리한다.

**값 타입 추측** — dtc 역컴파일러(`scripts/dtc/treesource.c` 의 `guess_value_type`, 커널 트리 dtc 1.7.2 소스)의 규칙을 그대로 옮겼다:

| 조건 | 결과 | 예 |
|---|---|---|
| 마지막 바이트 NUL + 모든 바이트가 인쇄 가능(0x20~0x7e)·NUL·`\a\b\t\n\v\f\r` + NUL 수 ≤ 나머지 수 | 문자열(목록) | `"arm,pl011", "arm,primecell"` |
| 아니고 길이 % 4 == 0 | 셀 `<0x%02x …>` | `<0x00 0x01 0x04>`, `"\0\0\0"`(NUL 4개)도 `<0x00>` |
| 나머지 | 바이트 `[%02x …]` | `local-mac-address = [02 00 5e 10 00 01]` |

추측이라서 틀릴 수 있다: 값이 `0x61626300` 인 셀 하나는 `"abc"` 로 찍힌다. **DTB 에 타입이 없다**는 게 핵심이고, 진짜 의미는 바인딩 문서(스키마, ELX-A05)가 정한다.

**NOP** — 커널은 `/chosen/rng-seed` 를 읽은 뒤 `fdt_nop_property()` 로 지운다 (`drivers/of/fdt.c` 1108행). libfdt 구현(`fdt_wip.c` 59행)은 데이터를 옮기지 않고 그 자리를 NOP 토큰으로 덮어쓴다. 테스트는 ethernet 의 `status = "disabled"` 24바이트(12 + `"disabled\0"` 9→12)를 NOP 6개로 덮고, 출력이 골든에서 그 한 줄만 빠진 것과 같은지 본다.

**짝 검사** — END_NODE 가 depth 0 에서 나오면, END 가 depth ≠ 0 에서 나오면, 루트가 두 개면 `NEST`. 깊이는 32 로 제한 (재귀 대신 카운터라 스택은 안 터지지만 Part C 의 경로 스택 크기다).

실행 결과 (`/tmp/ELX-B05_solution`, Apple M6 macOS):

```text
fuzz: 1889/1889 truncations rejected, byte flips: 1090 parsed OK, 799 rejected
50/50 passed
```

바이트를 하나 뒤집어도 1090가지는 "정상 DTB" 로 읽힌다 — 문자열 값이나 셀 값 안의 바이트는 구조를 안 깨기 때문이다. 파서가 지키는 건 **메모리 안전성**이지 내용의 정확성이 아니다 (그건 서명된 FIT 이미지 같은 secure boot 의 몫, ELX-M12).

진짜 dtc 와의 비교 (`bash dtc_compare.sh solution`, 진짜 출력 `dtc_compare_solution.txt`):

```text
PASS board.dtb (1889 B, 104 lines) == dtc
PASS board -p 4096 (padding) (5985 B, 104 lines) == dtc
PASS board -@ (__symbols__) == dtc after stripping 4 labels dtc restored from __symbols__
PASS fdtput (libfdt edit) (1885 B, 104 lines) == dtc
PASS QEMU virt dumpdtb (1048576 B, 420 lines) == dtc
PASS QEMU virt,gic-version=3 dumpdtb (1048576 B, 422 lines) == dtc
PASS QEMU virt,secure=on dumpdtb (1048576 B, 470 lines) == dtc
PASS truncated 1000 B rejected: mine='…/trunc.dtb: parse error -4' dtc='FATAL ERROR: EOF before reading 1889 bytes of DT blob'
PASS bad magic rejected: mine='…/magic.dtb: parse error -2' dtc='FATAL ERROR: Blob has incorrect magic number'
9/9 passed
RESULT: PASS (solution)
```

재미있는 발견: `dtc -@` 로 만든 DTB 를 dtc 1.8.1 이 역컴파일하면 `__symbols__` 노드를 보고 `gic: interrupt-controller@8000000 {` 처럼 **label 을 되살린다** (4줄). 셀 안의 참조는 여전히 `<0x01>` 숫자다. 우리 파서는 label 을 안 만들어서 그 4줄만 다르다 — 스크립트가 label 을 지우고 비교한다.

### Part C — 셀 해석, phandle, interrupt-parent

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">reg 는 부모의 #address-cells / #size-cells 로 끊어 읽는다</text>
  <g fill="currentColor"><text x="10" y="52">/memory@40000000</text><text x="10" y="68" opacity=".75">부모 / : 2 / 2</text></g>
  <g stroke="currentColor"><rect x="200" y="38" width="100" height="28" class="f-acc"/><rect x="300" y="38" width="100" height="28" class="f-acc"/>
    <rect x="400" y="38" width="100" height="28" class="f-ok"/><rect x="500" y="38" width="100" height="28" class="f-ok"/></g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle"><text x="250" y="57">0x00</text><text x="350" y="57">0x40000000</text><text x="450" y="57">0x00</text><text x="550" y="57">0x20000000</text></g>
  <text x="610" y="57" fill="currentColor">→ 0x40000000, 512 MiB</text>
  <g fill="currentColor"><text x="10" y="112">serial@9000000</text><text x="10" y="128" opacity=".75">부모 soc@0 : 1 / 1</text></g>
  <g stroke="currentColor"><rect x="200" y="98" width="100" height="28" class="f-acc"/><rect x="300" y="98" width="100" height="28" class="f-ok"/></g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle"><text x="250" y="117">0x9000000</text><text x="350" y="117">0x1000</text></g>
  <text x="410" y="117" fill="currentColor">→ 0x09000000, 4 KiB (MMIO 레지스터 창)</text>
  <g fill="currentColor"><text x="10" y="172">temp-sensor@48</text><text x="10" y="188" opacity=".75">부모 i2c : 1 / 0</text></g>
  <g stroke="currentColor"><rect x="200" y="158" width="100" height="28" class="f-acc"/></g>
  <text x="250" y="177" fill="currentColor" font-family="monospace" text-anchor="middle">0x48</text>
  <text x="310" y="177" fill="currentColor">→ I2C 7비트 주소 0x48, 크기 없음 (CPU 주소가 아니다)</text>
  <rect x="200" y="210" width="24" height="14" class="f-acc" stroke="currentColor"/><text x="230" y="222" fill="currentColor">주소 셀</text>
  <rect x="300" y="210" width="24" height="14" class="f-ok" stroke="currentColor"/><text x="330" y="222" fill="currentColor">크기 셀</text>
  <text x="10" y="252" fill="currentColor">루트의 2/2 로 serial 의 reg(8 B)를 읽으면 16 B 단위가 안 맞아 BADCELLS — "누구의 셀인가"가 가장 흔한 실수.</text>
</svg>
```

| 호출 | 결과 | 이유 |
|---|---|---|
| `fdt_reg("/memory@40000000", 0)` | 0x40000000, 0x20000000 | 루트 2/2 → 4셀 = 한 항목 |
| `fdt_reg("/interrupt-controller@8000000", 1)` | 0x08010000, 0x2000 | 항목 2개(16 B씩) 중 두 번째 = GIC CPU interface |
| `fdt_reg("/soc@0/serial@9000000", 0)` | 0x09000000, 0x1000 | soc 1/1 |
| `fdt_reg(".../temp-sensor@48", 0)` | 0x48, 0 | i2c 1/0 |
| `fdt_reg("/cpus/cpu@1", 0)` | 1, 0 | cpus 1/0 — CPU 의 `reg` = MPIDR 친화도 값 |
| `fdt_reg("/chosen", 0)` / idx 범위 밖 / 없는 경로 | `NOTFOUND` | |
| i2c 의 `#size-cells` 를 1 로 패치 | `BADCELLS` | 센서 reg 4 B 가 8 B 단위가 아님 |

**phandle** — dtc 는 셀 안에서 `<&label>` 로 참조된 노드에만 `phandle` 프로퍼티를 붙인다. 이 보드에선 gic = 1, gpio0 = 2 뿐이고, `aliases` 에서 경로로만 참조된 uart0·i2c0 에는 phandle 이 없다 (골든 출력에서 확인). 그래서 `fdt_node_by_phandle` 은 모든 노드의 `phandle` (옛 DTB 는 `linux,phandle`) 을 비교하는 선형 탐색이다. 0 과 0xffffffff 는 유효한 phandle 이 아니다. 커널은 unflatten 때 phandle 캐시를 만들어 이 탐색을 빠르게 한다.

**interrupt-parent 상속** — 센서는 자기 노드에 `interrupt-parent = <2>` 가 있어 GPIO 컨트롤러로 간다. serial 은 없으니 soc → 루트로 올라가 루트의 `<1>` (GIC) 을 쓴다. 커널 구현 `of_irq_find_parent()` (`drivers/of/irq.c` 61행)는 조금 더 정교하다: `interrupt-parent` 가 없으면 **부모 노드**로 가고, 도착한 노드가 `#interrupt-cells` 를 가질 때(=인터럽트 컨트롤러)까지 반복한다. 이 보드에선 두 방식의 답이 같다 (GPIO 아래에 자식을 두는 경우엔 달라질 수 있다).

**bring-up 결론** — 센서의 `interrupts = <5 2>` 는 부모(gpio0)의 `#interrupt-cells = <2>` 로 읽는다: GPIO 라인 5, 타입 2 = `IRQ_TYPE_EDGE_FALLING` (`include/dt-bindings/interrupt-controller/irq.h`: RISING 1, FALLING 2, LEVEL_HIGH 4, LEVEL_LOW 8). UART 의 `<0 1 4>` 는 GIC 3셀: `GIC_SPI`(0), SPI 번호 1 (GIC INTID = 32 + 1 = 33), LEVEL_HIGH. "IRQ 가 안 온다"면 DTB 에서 ① 인터럽트 부모가 의도한 컨트롤러인가 ② 셀 개수가 그 컨트롤러의 `#interrupt-cells` 와 맞나 ③ 그 컨트롤러 노드가 `status = "okay"` 인가 를 이 파서(또는 `fdtget`)로 확인한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B05 && export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk && cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/ELX-B05_solution && /tmp/ELX-B05_solution
# fuzz: 1889/1889 truncations rejected, byte flips: 1090 parsed OK, 799 rejected
# 50/50 passed   (exit 0, 0.18 s)
```

ASan/UBSan (퍼징 3778회 포함, 경고 0 — 진짜 실행):

```bash
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B05 && export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk && cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/ELX-B05_asan && /tmp/ELX-B05_asan
```

dtc 와 바이트 비교 (QEMU 가 있으면 virt DTB 3종 포함, 약 1초):

```bash
bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B05/dtc_compare.sh solution
```

<!-- code: dtc_compare.sh -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `uint32_t v = ntohl(*(uint32_t *)p);` | 정렬 안 된 포인터 역참조 = UB, strict aliasing 위반. Cortex-M0 는 HardFault | `be32(p)` 바이트 시프트 |
| `if (p + 12 + len > end)` (32비트) | `len = 0xfffffff0` 이면 wrap 해서 통과 → 블록 밖 읽기 (테스트가 잡음) | `(uint64_t)p + 12 + len > end` |
| 다음 위치를 `p + 12 + len` | 4바이트 정렬을 빼먹음 → 다음 "태그"가 값의 꼬리 | `align4(p + 12 + len)` |
| `strings + nameoff` 를 바로 `strcmp` | nameoff 가 블록 밖이거나 NUL 이 없으면 out-of-bounds | 범위 + `memchr` 로 NUL 확인 후 사용 |
| rsvmap 은 0x28, struct 는 0x48 이라고 가정 | QEMU virt DTB 는 0x30 / 0x40 | 항상 헤더 오프셋을 따른다 |
| `reg` 를 **자기** 또는 루트의 셀 개수로 읽음 | 셀 개수는 **부모 버스**가 정한다 | `stack[d-2]` 의 `#address-cells`/`#size-cells` |
| `interrupt-parent` 를 자기 노드에서만 찾음 | 대부분의 노드는 조상(보통 루트)에서 상속 | 자기 → 루트로 올라가며 탐색 |
| `status` 가 없으면 꺼진 것으로 처리 | `status` 없음 = `"okay"` | 연습 2 의 `fdt_is_available` |

DTS 쪽 흔한 실수 3개를 `demo_bad.dts` 에 넣고 dtc 에 돌린 진짜 출력 (`demo_bad_dtc.txt`):

```text
$ dtc -I dts -O dtb -o /dev/null demo_bad.dts
demo_bad.dts:10.3-33: Warning (reg_format): /memory@40000000:reg: property has invalid length (8 bytes) (#address-cells == 2, #size-cells == 2)
demo_bad.dts:13.6-30.4: Warning (unit_address_vs_reg): /soc: node has a reg or ranges property, but no unit name
/dev/null: Warning (pci_device_reg): Failed prerequisite 'reg_format'
…
/dev/null: Warning (i2c_bus_reg): Failed prerequisite 'reg_format'
(exit 0)

$ (memory 의 reg 를 4셀로 고친 뒤)
<stdin>:13.6-30.4: Warning (unit_address_vs_reg): /soc: node has a reg or ranges property, but no unit name
<stdin>:25.19-28.6: Warning (i2c_bus_reg): /soc/i2c@9040000/temp-sensor@48: I2C bus unit address format error, expected "49"
```

볼 점: ① 셀 개수 실수는 **경고(exit 0)** 라서 빌드가 그냥 통과한다 — `dtc -E reg_format` 처럼 특정 검사를 에러로 올리거나 `make dtbs_check`(ELX-A05)를 CI 에 넣는 이유. ② `reg_format` 이 실패하면 i2c/spi/simple-bus 검사가 "Failed prerequisite" 로 **건너뛰어져** 3번째 실수(@48 vs 0x49)가 첫 실행에선 안 보인다. 반면 없는 label 참조(`<&nosuch>`)는 진짜 에러다: `ERROR (phandle_references): /x: Reference to non-existent node or label "nosuch"` 후 exit 2 (별도 한 줄 DTS 로 확인).

## 8. 면접 Q&A

<details>
<summary>Q1. DTB 파일 구조를 설명해 보세요. 왜 프로퍼티 이름을 따로 모아 두나요?</summary>

40바이트 big-endian 헤더(magic `0xd00dfeed`, totalsize, 세 블록의 오프셋, version 17, last_comp 16, boot_cpuid, 두 블록 크기) → memory reservation 블록(u64 주소·크기 쌍, (0,0) 끝) → structure 블록(BEGIN_NODE/PROP/END_NODE/NOP/END 토큰, 4바이트 정렬) → strings 블록(NUL 로 끝나는 프로퍼티 이름들). 이름을 분리하면 `compatible`·`reg` 처럼 수백 번 반복되는 이름이 한 번만 저장되고, PROP 토큰은 고정 길이 헤더(12 B)라 건너뛰기 쉽다.

꼬리질문: "왜 포인터 대신 토큰 스트림인가?" → 부트로더가 어느 주소에 올릴지 모르고 MMU 도 꺼진 상태에서 읽어야 하므로 위치 독립적인 한 덩어리여야 한다. 노드는 깊이 우선 순서로 직렬화되고, 부모-자식 관계는 BEGIN/END 의 중첩으로만 표현된다.

</details>

<details>
<summary>Q2. `#address-cells` / `#size-cells` 는 누구의 값을 쓰나요? I2C 노드는 왜 `#size-cells = <0>` 인가요?</summary>

`reg` 를 가진 노드의 **부모**의 값을 쓴다 — 주소 공간을 정의하는 건 버스다. 스펙 기본값은 2/1 이다. I2C 자식의 `reg` 는 7비트(또는 10비트) 슬레이브 주소 하나라서 "크기"가 없다 → 1/0. SPI 는 칩셀렉트 번호라 역시 1/0, CPU 노드는 MPIDR 값이라 보통 1/0 (arm64 는 2/0 도 씀).

꼬리질문: "부모에 없으면?" → 스펙은 기본 2/1. 리눅스 `of_bus_n_addr_cells()` (`drivers/of/base.c` 103행)는 조상으로 계속 올라가며 찾는데, 소스 주석이 이 동작을 deprecated 라 하고 `WARN_ONCE` 를 낸다. 즉 "부모에 명시"가 정답.

</details>

<details>
<summary>Q3. label 과 phandle 은 어떻게 다른가요? dtc 는 phandle 을 언제 만드나요?</summary>

label(`gic:`)은 소스 안에서만 쓰는 이름이고 DTB 에는 남지 않는다. 셀 안에서 `<&gic>` 로 참조하면 dtc 가 대상 노드에 `phandle = <N>` 을 붙이고 참조 자리에 N 을 넣는다. 셀 밖 `&uart0` 은 경로 문자열이 된다. 이번 보드에선 gic=1, gpio0=2 만 생겼고 uart0 엔 phandle 이 없다.

꼬리질문: "overlay 는 base DTB 의 label 을 어떻게 아나?" → base 를 `dtc -@` 로 컴파일하면 `__symbols__` 노드에 label → 경로가 남고, overlay 의 `__fixups__` 가 그걸로 phandle 을 고친다 (ELX-A05). dtc 1.8.1 은 역컴파일 때 `__symbols__` 로 label 까지 되살린다 (이번에 실제로 확인).

</details>

<details>
<summary>Q4. 어떤 노드의 인터럽트가 어느 컨트롤러로 가는지 DT 만 보고 어떻게 알아내나요?</summary>

① 자기 노드에 `interrupt-parent` 가 있으면 그 phandle ② 없으면 부모 쪽으로 올라가며 찾는다 (리눅스 `of_irq_find_parent` 는 `#interrupt-cells` 가 있는 노드에 닿을 때까지). ③ 찾은 컨트롤러의 `#interrupt-cells` 개수만큼 `interrupts` 를 끊어 읽는다: GIC 3셀 = (SPI/PPI, 번호, 플래그), GPIO 2셀 = (라인, 트리거 타입).

꼬리질문: "한 디바이스가 두 컨트롤러에 인터럽트를 가지면?" → `interrupts-extended = <&gic 0 5 4>, <&gpio0 3 2>;` 처럼 항목마다 phandle 을 붙인다. PCI 처럼 슬롯·핀을 컨트롤러로 매핑할 땐 `interrupt-map` (nexus).

</details>

<details>
<summary>Q5. 부트로더는 DTB 를 커널에 어떻게 넘기나요? QEMU virt DTB 의 totalsize 가 왜 1 MiB 인가요?</summary>

arm64: x0 = DTB 물리주소, 8바이트 정렬, 2 MB 이하 (`booting.rst`). arm32: r2 (ATAGS 자리를 대체), RISC-V: a1 (a0 = hartid). U-Boot 는 `booti ${kernel_addr} ${ramdisk}:${size} ${fdt_addr}` 전에 `/chosen` 에 `bootargs`·`linux,initrd-start/end` 를 써 넣는다 — 그러려면 blob 안에 빈 공간이 필요하다. QEMU 는 1 MiB 버퍼에서 DT 를 만들고 그 크기를 totalsize 에 기록해서 끝에 여유가 남는다. dtc 는 `-p`(padding)/`-S`(크기) 옵션으로, U-Boot 는 `fdt resize` 로 공간을 만든다. `-p 4096` 로 만든 DTB 도 우리 파서와 dtc 출력이 같았다 (빈 공간은 출력에 안 나온다).

꼬리질문: "DT 대신 ACPI 는?" → 서버급 arm64(SBSA/SystemReady SR)는 ACPI, 임베디드 SoC 는 거의 DT. 둘 다 "하드웨어를 코드 밖에서 기술"하는 같은 목적이다.

</details>

<details>
<summary>Q6. `status = "disabled"` 는 노드를 지우는 것과 뭐가 다른가요?</summary>

노드는 남지만 커널이 디바이스를 만들지 않는다 (`of_device_is_available()`; 초기 스캔은 `of_fdt_device_is_available()`, `drivers/of/fdt.c` 69행 — `/memory` 스캔도 이걸로 꺼진 노드를 건너뜀). SoC 공통 `.dtsi` 에 모든 컨트롤러를 `disabled` 로 두고, 보드 `.dts` 에서 실제 쓰는 것만 `&i2c0 { status = "okay"; };` 로 켜는 것이 표준 패턴이다. 지우면(`/delete-node/`) 다른 노드가 그 phandle 을 참조할 때 컴파일 에러가 난다. `status` 가 아예 없으면 `"okay"` 와 같다.

꼬리질문: "꺼진 노드인데 왜 커널 로그에 아무것도 없나?" → 디바이스가 안 만들어졌으니 probe 시도조차 없다. bring-up 에서 "드라이버 로그가 하나도 없다"면 1순위로 status 와 compatible 오타를 본다.

</details>

<details>
<summary>Q7. 신뢰할 수 없는 blob 을 파싱하는 코드에서 무엇을 조심해야 하나요?</summary>

모든 길이·오프셋은 공격자가 고른 값이다: ① totalsize ≤ 실제 버퍼 ② 모든 블록이 totalsize 안 ③ 덧셈은 넓은 타입으로 (wrap) ④ 문자열은 경계 안에서 NUL 확인 ⑤ 깊이 제한 (재귀 파서면 스택 고갈) ⑥ 루프가 항상 전진하는지 (pos 가 줄지 않음). 이번 테스트는 1889가지 자르기 + 1889가지 바이트 뒤집기를 ASan/UBSan 아래 돌려 경고 0 을 확인했다. 실무에선 libFuzzer 로 더 넓게 돌린다.

꼬리질문: "커널은 DTB 를 검증하나?" → 구조는 libfdt 가 검사하지만 내용(주소·IRQ)은 믿는다. 그래서 secure boot 에선 DTB 도 서명 범위(FIT 이미지 서명)에 넣는다 (ELX-M12). 또 커널은 부팅 때 blob 의 CRC 를 기억해 두고, 나중에 `/sys/firmware/fdt` 로 원본을 내보내기 전에 CRC 가 바뀌었으면 만들지 않는다 (`of_fdt_raw_init`, `drivers/of/fdt.c` 1323행).

</details>

<details>
<summary>Q8. 보드에서 커널이 실제로 받은 Device Tree 를 어떻게 확인하나요?</summary>

`/sys/firmware/devicetree/base/` (= `/proc/device-tree`) 가 노드는 디렉터리, 프로퍼티는 파일로 보여준다 (값은 big-endian 원본 바이트 → `hexdump -C`, `cat` 은 문자열만). 통째로 보려면 `dtc -I fs -O dts /sys/firmware/devicetree/base`, 원본 blob 은 `/sys/firmware/fdt` (root 전용). 빌드한 `.dtb` 와 diff 해서 "부트로더가 다른 DTB 를 올렸다 / U-Boot 가 고쳤다"를 잡는다. (이 레슨은 Mac 레인이라 게스트에서 직접 실행하진 않았다 — ELX-B04·B10 의 QEMU 게스트에서 해볼 수 있다.)

꼬리질문: "런타임에 DT 를 바꿀 수 있나?" → 보통은 부트로더 단계에서 overlay 를 적용한다 (U-Boot `fdt apply`). 메인라인 커널엔 사용자공간에서 overlay 를 올리는 표준 인터페이스가 없고, configfs 방식은 벤더/다운스트림 패치다 (ELX-A05, 기억에 의존한 내용).

</details>

## 9. 추가 연습

### 연습 1 — `fdtget` 만들기: u32 와 문자열 목록

`fdt_getprop_u32(path, name)` 와 `fdt_stringlist_get(path, name, idx)`. i2c 의 `clock-frequency` → 400000, serial `compatible[1]` → `"arm,primecell"`, `[2]` → `NOTFOUND`, `reg`(8 B) 를 u32 로 읽으면 `BADCELLS`.

<details>
<summary>답 보기</summary>

u32 는 `len == 4` 일 때만 `be32(v)`. 문자열 목록은 `memchr(v + i, '\0', len - i)` 로 다음 NUL 을 찾아 idx 만큼 건너뛴다. 마지막 문자열에 NUL 이 없으면 `TRUNC` — libfdt `fdt_stringlist_get()` 도 `strnlen` 으로 같은 검사를 하고 `-FDT_ERR_BADVALUE` 를 돌려준다. `fdtget board.dtb /soc@0/serial@9000000 compatible` 의 출력 `arm,pl011 arm,primecell` 과 같은 결과다.

</details>

### 연습 2 — compatible 로 노드 찾기 + status

`fdt_node_by_compatible("ti,tmp102")` → 센서 경로, `"arm,primecell"` → 문서 순서상 첫 노드인 serial, 없는 것 → `NOTFOUND`. `fdt_is_available` 은 status 없음/`"okay"`/`"ok"` → true, ethernet → false.

<details>
<summary>답 보기</summary>

모든 BEGIN_NODE 에서 `compatible` 문자열 목록에 정확히 같은 항목이 있는지 본다 (부분 문자열 아님 — `"arm,pl011"` 로 `"arm,pl0"` 를 찾으면 안 된다). 커널의 `of_find_compatible_node()` 와 같은 순서(트리 순서)로 첫 번째를 돌려준다. status 비교는 NUL 까지 포함한 길이(`"okay"` = 5)로 해야 `"okayx"` 를 거른다.

</details>

### 연습 3 — 콘솔은 어디? `/chosen/stdout-path` 풀기

`"serial0:115200n8"` → `:` 앞을 `/aliases` 에서 찾아 `"/soc@0/serial@9000000"`, 뒤는 옵션 `"115200n8"`. `/` 로 시작하면 이미 경로.

<details>
<summary>답 보기</summary>

`strchr(sp, ':')` 로 나누고, alias 이름이면 `/aliases` 의 같은 이름 프로퍼티(문자열)를 경로로 쓴다. 커널은 `of_find_node_opts_by_path()` 로 같은 풀이를 해서 `of_stdout` 을 정하고(`drivers/of/base.c` 1878행), `console=` 이 없으면 `of_console_check()` 로 그 UART 를 콘솔로 고른다. `earlycon` 을 인자 없이 주면 같은 노드로 초기 콘솔을 띄운다. bring-up 첫날 "부팅 로그가 안 나온다"면 stdout-path → 그 UART 노드의 status·reg·clock 을 먼저 본다.

</details>

### 연습 4 — `ranges` 로 CPU 주소 번역

`fdt_translate(path, idx)` — reg 주소를 부모 버스마다 `ranges` (자식주소, 부모주소, 크기) 로 번역해 루트까지 올린다. 빈 `ranges;` = 1:1, `ranges` 없음 = CPU 에서 안 보이는 버스(i2c) → `NOTFOUND`. soc 의 ranges 를 `<0x0  0x0 0x40000000  0x10000000>` 로 패치하면 serial 은 `0x49000000`.

<details>
<summary>답 보기</summary>

`ranges` 한 항목 = (자식 버스 `#address-cells`) + (부모 `#address-cells`) + (자식 버스 `#size-cells`) 셀. soc 는 1 + 2 + 1 = 4셀 = 16 B. 주소가 `[child, child+size)` 안이면 `parent + (addr - child)`. 라즈베리파이 4의 `0x7e…` 주소(버스 주소)가 CPU 에서 `0xfe…` 로 보이는 게 바로 이 번역이다. 커널 구현은 `of_translate_address()` (`drivers/of/address.c`).

</details>

### 연습 5 — unit-address 검사기 (dtc 경고 직접 구현)

모든 `이름@주소` 노드에서 `@` 뒤와 `reg` 첫 주소(소문자 16진, 0x 없이)가 같은지 검사해 불일치 개수와 보고서를 낸다. board.dtb → 0, 센서 reg 를 0x49 로 패치 → `"/soc@0/i2c@9040000/temp-sensor@48: reg 0x49\n"` 1건.

<details>
<summary>답 보기</summary>

`fdt_get_path` + `fdt_reg(path, 0)` 후 `snprintf("%llx")` 와 문자열 비교. `reg` 가 없는 `soc@0` 은 건너뛴다 (dtc 에선 `unit_address_without_reg` 경고 대상). dtc 의 `i2c_bus_reg` 경고("expected "49"")와 같은 검사를 DTB 만으로 할 수 있다 — 소스가 없는 벤더 DTB 를 받았을 때 유용하다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B05 && export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk && cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/ELX-B05_practice && /tmp/ELX-B05_practice
# 68/68 passed (본문 50 + 연습 18), exit 0
```

## 10. 핵심 정리

- DTS(텍스트) → `dtc` → DTB(FDT 바이너리). arm64 는 x0 로 DTB 주소를 받는다 (8 B 정렬, ≤ 2 MB).
- FDT = 40 B big-endian 헤더 + rsvmap(u64 쌍, (0,0) 끝) + structure(토큰 1/2/3/4/9, 4 B 정렬) + strings(이름). **오프셋은 항상 헤더에서**.
- PROP = tag, len(패딩 전), nameoff(strings 안), 값 → 다음은 `align4(p + 12 + len)`.
- DTB 엔 타입이 없다: dtc 는 "문자열 → 셀(4의 배수) → 바이트" 순서로 추측한다.
- 신뢰할 수 없는 blob: 64비트 덧셈 경계 검사, memchr 로 NUL 확인, 깊이 제한, 항상 전진. ASan 퍼징으로 확인.
- `reg` 는 **부모**의 `#address-cells`/`#size-cells` (기본 2/1). I2C/SPI 자식은 1/0. CPU 주소가 되려면 조상마다 `ranges`.
- `<&label>` = phandle (dtc 가 대상에 `phandle` 추가), 셀 밖 `&label` = 경로 문자열. `interrupt-parent` 는 조상에서 상속.
- `status` 없음 = okay. 셀 개수 실수는 dtc **경고**일 뿐 → 빌드는 통과하니 경고를 읽자.

## 참고자료

- [Devicetree Specification (devicetree.org, v0.4) — 5장 Flattened Devicetree (DTB) Format](https://www.devicetree.org/specifications/)
- Linux `Documentation/arch/arm64/booting.rst` — DTB 정렬·크기·x0 (6.18.55 소스 53·165행에서 확인)
- Linux `Documentation/devicetree/usage-model.rst` — DT 가 커널에서 쓰이는 방식
- Linux `drivers/of/fdt.c` (`early_init_dt_scan_chosen`, `unflatten_device_tree`, `of_fdt_raw_init`), `drivers/of/irq.c` (`of_irq_find_parent`), `drivers/of/base.c` (`of_bus_n_addr_cells`)
- dtc/libfdt 소스 (커널 트리 `scripts/dtc/`, dtc 1.7.2): `libfdt/fdt.c` (`fdt_check_header`, `fdt_next_tag`), `libfdt/fdt_wip.c` (`fdt_nop_property`), `treesource.c` (`guess_value_type`)
- [dtc 저장소 (git.kernel.org/pub/scm/utils/dtc/dtc.git)](https://git.kernel.org/pub/scm/utils/dtc/dtc.git)
- 내 노트: EmbeddedLinux/D_device_tree/2026-10-10_D2_dt_syntax.md (§10 FDT 바이트 해석, §5–6 셀·ranges)
