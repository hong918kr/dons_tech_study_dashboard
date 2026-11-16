---
id: TOOL-M02
time: 75분
updated: 2026-11-15
---
# TOOL-M02 맵 파일 분석기: 누가 메모리를 먹는가

## 학습 목표

- GNU ld 맵 파일(`-Wl,-Map=`)의 "Linker script and memory map" 부분을 줄 종류별로(출력 섹션 헤더 · 입력 섹션 · 두 줄로 꺾인 긴 이름 · `*fill*` · 심볼 · 대입) 구분해 파싱하고, **출력 섹션별 / 오브젝트(아카이브 멤버 포함)별 / 심볼별** 크기를 집계하는 Python 도구를 만든다.
- 파서 결과를 `arm-none-eabi-size -A`, `arm-none-eabi-nm -S --size-sort`, 각 `.o` 의 섹션 크기, `--print-gc-sections` 로 **바이트 단위까지 맞춘다** — "모든 바이트가 누구 것인지" 설명이 끝나야 도구를 믿을 수 있다.
- 맵 파서가 실제로 틀리는 네 곳(긴 이름 줄바꿈, Discarded input sections, `*fill*` 이중 계산, hex 파싱)과 맵 자체가 "거짓말"하는 두 곳(병합된 문자열 섹션 `.rodata.str1.1`, 정렬 패딩이 붙은 심볼 크기)을 실측으로 안다.
- 두 빌드(v1 → v2)의 크기 diff 를 섹션 → 오브젝트 → 심볼 순서로 좁혀 "RAM 이 5120 B 늘었다, 범인은 `g_rx_buf` +3072 과 새 `telemetry.o`" 같은 PR 코멘트를 쓴다.
- ld64(맥) `-map`, ld.lld `-Map` 이 다른 형식이라는 것을 보고, 파서를 어디까지 믿을지(링커·버전 의존) 판단한다.

## 1. 문제

"이 PR 이후 RAM 이 5 KB 늘었는데 누가 먹었죠?" — 펌웨어 팀에서 매주 나오는 질문이고, 면접에서는 "맵 파일로 RAM 사용을 어떻게 추적하나요?" 로 나온다. `size` 는 섹션 합계만 주고(TOOL-B06), 맵 파일은 답을 다 갖고 있지만 수천 줄이라 눈으로는 못 본다(눈으로 읽기는 BM-B05). 그래서 실무에서는 맵을 파싱하는 스크립트를 CI 에 걸어 PR 마다 "섹션·오브젝트·심볼 Top-N + 이전 빌드 대비 diff" 를 남긴다. SSD 컨트롤러 펌웨어에서 SRAM/TCM 을 바이트 단위로 나눠 쓰던 것과 같은 일을 Cortex-M 이미지로 한다.

실험 대상은 이 레슨의 작은 Cortex-M4 펌웨어다 (`fw/`). `gen_maps.sh` 가 맥의 `arm-none-eabi-gcc 16.2.0` (+ GNU ld 2.47) 로 두 번 빌드한다. 이 맥 툴체인에는 newlib 이 없으므로 `-nostdlib -ffreestanding` 이고, `memcpy/memset` 은 직접 쓴 `libutil.a(mem.o)`, 64비트 나눗셈은 `libgcc.a` 에서 온다.

```bash
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os -ffunction-sections -fdata-sections -c ...
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -nostdlib -T fw/fw.ld -Wl,--gc-sections \
    -Wl,--print-gc-sections -Wl,-Map=v1.map -Wl,--print-memory-usage \
    startup.o main.o comm.o sensor.o legacy_board.o libutil.a <libgcc.a> -o v1.elf
```

| 파일 | 내용 |
|---|---|
| `fw/` | `startup.c`(벡터 테이블·Reset_Handler), `main.c`, `comm.c`(`g_rx_buf[RX_BUF_SIZE]`), `sensor.c`(보정 테이블 + 안 쓰는 함수 2개), `legacy_board.c`(`-fcommon` → COMMON), `telemetry.c`(v2 전용), `util/`(→ `libutil.a`), `fw.ld` |
| `gen_maps.sh` | v1(기본) / v2(`-DRX_BUF_SIZE=4096 -DFEATURE_TELEMETRY` + `telemetry.o`) 빌드 → `/tmp/TOOL-M02_work/gen` |
| `v1.map` `v2.map` | 위 빌드의 실제 맵 (331줄 / 17 KB, 352줄 / 18 KB) — 파서의 시험 데이터 |
| `starter.py` / `solution.py` / `practice.py` | **풀 대상** — `mapstat.py` 도구 (python3 표준 라이브러리만, `python3 -I` 로 실행) |
| `check.sh` | 펌웨어를 다시 빌드해 ELF 에서 정답(size/nm/.o/gc 목록)을 뽑고, 변형 스크립트 결과와 비교 |
| `demo_ld64_map.sh` → `ld64_map_sample.txt` | 맥 Apple ld `-Wl,-map` 형식 (채점 없음) |
| `demo_vm_lld_map.sh` → `lld_map_sample.txt` | Lima VM: 같은 펌웨어를 GNU ld 와 ld.lld 로 링크, lld 맵 형식 (채점 없음) |

맵 파일은 다섯 부분이다. **파싱할 곳은 ⑤ 하나**이고, 나머지는 세면 안 되는 곳(③)이거나 보조 정보(①②④)다.

```svg
<svg viewBox="0 0 750 420" xmlns="http://www.w3.org/2000/svg" font-size="12" xml:space="preserve">
  <rect x="8" y="8" width="734" height="404" rx="8" fill="none" stroke="currentColor"/>
  <text x="20" y="30" fill="currentColor" font-size="13" font-weight="bold">v1.map 의 실제 줄 (331줄, 공백 일부 줄임) — 부분과 줄 종류</text>
  <rect x="20" y="40" width="430" height="22" class="f-mut" stroke="currentColor"/>
  <text x="28" y="56" fill="currentColor" font-family="monospace">Archive member included to satisfy …</text>
  <text x="462" y="56" fill="currentColor">① 아카이브 멤버가 왜 들어왔나 (연습 4)</text>
  <rect x="20" y="66" width="430" height="22" class="f-mut" stroke="currentColor"/>
  <text x="28" y="82" fill="currentColor" font-family="monospace">Allocating common symbols</text>
  <text x="462" y="82" fill="currentColor">② COMMON 심볼 크기 표</text>
  <rect x="20" y="92" width="430" height="22" class="f-warn" stroke="currentColor"/>
  <text x="28" y="108" fill="currentColor" font-family="monospace">Discarded input sections</text>
  <text x="462" y="108" fill="currentColor">③ --gc-sections 가 버린 것 — 세면 안 됨</text>
  <rect x="20" y="118" width="430" height="22" class="f-mut" stroke="currentColor"/>
  <text x="28" y="134" fill="currentColor" font-family="monospace">Memory Configuration</text>
  <text x="462" y="134" fill="currentColor">④ FLASH/RAM 영역 (연습 1)</text>
  <rect x="20" y="144" width="430" height="22" class="f-ok" stroke="currentColor"/>
  <text x="28" y="160" fill="currentColor" font-family="monospace">Linker script and memory map</text>
  <text x="462" y="160" fill="currentColor">⑤ 여기서부터 파싱</text>
  <text x="28" y="190" fill="currentColor" font-family="monospace">.text           0x080000d8      0x580</text>
  <text x="462" y="190" fill="currentColor">출력 섹션 헤더 (0열 시작)</text>
  <text x="28" y="210" fill="currentColor" font-family="monospace"> .text.fmt_u32  0x080005ec       0x44 libutil.a(fmt.o)</text>
  <text x="462" y="210" fill="currentColor">입력 섹션 한 줄 (1칸 들여씀)</text>
  <rect x="20" y="218" width="430" height="44" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="28" y="234" fill="currentColor" font-family="monospace"> .text.Default_Handler</text>
  <text x="28" y="254" fill="currentColor" font-family="monospace">                0x08000434        0x2 startup.o</text>
  <text x="462" y="234" fill="currentColor">이름이 14자 이상이면 두 줄로 꺾임</text>
  <text x="462" y="254" fill="currentColor">(출력 섹션은 15자 이상)</text>
  <text x="28" y="282" fill="currentColor" font-family="monospace">                0x08000434                Default_Handler</text>
  <text x="462" y="282" fill="currentColor">심볼 줄 (global 만, 크기 없음)</text>
  <text x="28" y="302" fill="currentColor" font-family="monospace"> *fill*         0x08000436        0x2</text>
  <text x="462" y="302" fill="currentColor">정렬 패딩 — 섹션 몫, 오브젝트 몫 아님</text>
  <text x="28" y="322" fill="currentColor" font-family="monospace"> *(.text*)</text>
  <text x="462" y="322" fill="currentColor">스크립트 패턴 (크기 없음)</text>
  <text x="28" y="342" fill="currentColor" font-family="monospace">                0x08000658        _etext = .</text>
  <text x="462" y="342" fill="currentColor">스크립트 대입 (크기 없음)</text>
  <text x="28" y="362" fill="currentColor" font-family="monospace">                 0x21 (size before relaxing)</text>
  <text x="462" y="362" fill="currentColor">병합 전 크기 (v2 문자열, Part C)</text>
  <text x="28" y="392" fill="currentColor" font-family="monospace">OUTPUT(v1.elf elf32-littlearm)</text>
  <text x="462" y="392" fill="currentColor">뒤로 .comment/.debug_* (non-alloc)</text>
</svg>
```

### Part A — 맵을 세 가지 장부로: 섹션 · 오브젝트 · 심볼

- `mapstat.py sections|objects|symbols MAP` 는 TSV, `report MAP -n N` 은 사람용 Top-N 보고서를 낸다.
- 출력 섹션: 헤더 크기, 그 안 입력 섹션 합, `*fill*` 합. **입력 + fill == 헤더** 가 모든 alloc 섹션에서 성립해야 한다(빠진 줄이 없다는 증거).
- 오브젝트: 입력 섹션 크기를 오브젝트별로 더한다. 경로는 짧게 (`/opt/…/libgcc.a(_udivmoddi4.o)` → `libgcc.a(_udivmoddi4.o)`). non-alloc(`.debug_*`, `.comment`, `.ARM.attributes`)은 메모리가 아니므로 뺀다.
- 심볼: 맵에는 심볼 크기가 없다. 같은 입력 섹션 안에서 **다음 심볼 주소까지의 거리**(마지막은 입력 섹션 끝까지)로 구한다. 심볼 줄이 없는 입력 섹션은 이름에서 유도(`.bss.s_rx_head` → `s_rx_head`, static 변수)하거나 `[comm.o .rodata.str1.1]` 처럼 괄호 이름을 붙인다.
- 정답 기준: 섹션 = `size -A`, 심볼 = `nm -S --size-sort`(우리 오브젝트의 심볼), 오브젝트 = 각 `.o` 의 alloc 섹션 합 − `--print-gc-sections` 로 버려진 것 + COMMON. 합계 보존: Σ오브젝트 + Σfill == Σ`size -A`.

### Part B — starter 의 함정 4개 (+ diff 는 TODO)

`starter.py` 는 돌아가지만 숫자가 틀린다. `check.sh starter` 가 어디가 틀렸는지 말해 준다. 네 버그 모두 실제 맵 파서에서 흔히 보는 것이다: 긴 이름의 두 줄 형식을 버림, "Discarded input sections" 까지 셈, `*fill*` 을 입력 섹션으로도 셈, `0x` 를 `str.strip` 으로 벗김. 그리고 `diff_maps()` 가 비어 있다.

### Part C — v1 → v2 회귀: 누가 RAM 5120 B 를 먹었나

- `mapstat.py diff v1.map v2.map` 이 섹션 → 오브젝트 → 심볼 순으로 변화량 Top-N 을 낸다 (`--tsv` 는 기계용). 변화량이 `size -A` / `nm` 의 차이와 같아야 한다.
- 출력으로 PR 코멘트를 쓰고, 맵이 오해를 부르는 두 곳 — `._user_heap_stack` −4 B (정렬), `comm.o` +3104 B (병합된 문자열이 첫 오브젝트에 몰림) — 을 설명한다.

## 2. Problem (English)

> **Map-file size analyzer.**
>
> Our Cortex-M4 firmware is linked with GNU ld using `-ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,-Map=fw.map`. Write `mapstat.py` (Python 3 standard library only) that parses the "Linker script and memory map" part of a GNU ld map file and reports:
>
> 1. per output section: size, bytes covered by input sections, bytes of `*fill*` padding;
> 2. per object file (archive members as `libfoo.a(member.o)`): allocated bytes;
> 3. per symbol: allocated bytes (the map has no symbol sizes — derive them);
> 4. a Top-N report, and `diff OLD NEW` per section / object / symbol.
>
> Constraints: handle the two-line form GNU ld uses when a section name is long; ignore "Discarded input sections"; count `*fill*` exactly once and never as an object; non-allocated sections (`.debug_*`, `.comment`, `.ARM.attributes`) are not memory. Your numbers must match `arm-none-eabi-size -A` and `arm-none-eabi-nm -S --size-sort` on the same ELF to the byte, and Σobjects + Σfill must equal the image total.
>
> Example (`report v1.map -n 3`): top object `comm.o 1241` (flash 213, ram 1044), top symbol `g_rx_buf 1024 .bss`.
>
> Follow-ups: why can map-derived symbol sizes differ from `nm -S`? RAM grew by 5120 bytes in this PR — walk me through finding the cause. How would you make this a CI gate? Why is parsing map files fragile, and what would you use instead?

## 3. 시작 코드

<!-- code: starter.py -->

시험 데이터를 만드는 스크립트 (check.sh 도 이걸로 ELF 를 다시 만든다):

<!-- code: gen_maps.sh -->

체커:

<!-- code: check.sh -->

```bash
bash check.sh starter      # 2/22, exit 1
bash check.sh solution     # 22/22, exit 0
bash check.sh practice     # 28/28, exit 0
```

check.sh 는 맥에서만 돈다 (약 2 초): `gen_maps.sh` 로 `/tmp/TOOL-M02_work/<variant>/gen` 에 v1/v2 를 새로 빌드 → 레슨 폴더의 `v1.map`/`v2.map` 이 새 빌드와 바이트까지 같은지(재현성) 확인 → ELF/오브젝트에서 정답을 뽑아 `truth/` 에 저장 → 변형 스크립트를 **커밋된 맵**에 돌려 비교한다. starter 실제 출력:

```text
PASS v1.map in the lesson dir == fresh gen_maps.sh build (reproducible)
PASS v2.map in the lesson dir == fresh gen_maps.sh build (reproducible)
FAIL wrapped output header: ._user_heap_stack = missing B (size -A: 3076)
FAIL wrapped input lines (name alone, addr/size on next line): Default_Handler=0 B libgcc.a(_dvmd_tls.o), g_banner=missing, g_adc_raw=missing
FAIL only the memory-map part parsed (Discarded input sections, Memory Configuration skipped): bist, *default*
FAIL *fill* counted once, never as an input/object: .text/.rodata fill = 4/3 B (expect 4/3), fake *fill* inputs: 4 (map has 7 *fill* lines)
FAIL hex sizes: g_rx_buf 0x400 -> 4 B, .text 0x580 -> 88 B
FAIL v1 output sections == size -A (7 alloc sections): .text,._user_heap_stack
FAIL v1 every byte accounted: inputs + *fill* == section size: .text,.rodata,.data,.bss,._user_heap_stack
FAIL v1 conservation: sum(objects) 1427 + fill 27 == size -A total 7136
FAIL v1 symbol sizes == nm -S --size-sort (28 own symbols): Default_Handler map=0 nm=2; HardFault_Handler map=0 nm=2; NMI_Handler map=0 nm=2
FAIL v1 per-object bytes == .o sections - gc'd + COMMON (8 objects), selftest.o not pulled: comm.o map=4 truth=1241; libutil.a(crc16.o) map=None truth=556; legacy_board.o map=260 truth=296
FAIL v2 output sections == size -A (7 alloc sections): .rodata,.bss,._user_heap_stack
FAIL v2 every byte accounted: inputs + *fill* == section size: .text,.rodata,.data,.bss,._user_heap_stack
FAIL v2 conservation: sum(objects) 1424 + fill 23 == size -A total 12444
FAIL v2 symbol sizes == nm -S --size-sort (32 own symbols): Default_Handler map=0 nm=2; HardFault_Handler map=0 nm=2; NMI_Handler map=0 nm=2
FAIL v2 per-object bytes == .o sections - gc'd + COMMON (7 objects; skip merged-string objs comm.o,telemetry.o), selftest.o not pulled: libutil.a(crc16.o) map=None truth=556; legacy_board.o map=260 truth=296; main.o map=None truth=176
FAIL report -n 5: top object 'libgcc.a(_udivmoddi4.o)', top symbol '__udivmoddi4'
FAIL diff sections == size -A deltas: 
FAIL diff own symbols == nm deltas (6 changed): 
FAIL diff objects == truth deltas (non-merged): 
FAIL human diff -n 5 shows telemetry.o (new)
2/22 passed
```

앞의 다섯 FAIL 이 버그 하나씩을 겨냥한 줄이다 (줄바꿈 2 · Discarded · fill · hex). 나머지는 그 결과 전체 숫자가 틀린 것과 diff TODO 다. 각 버그를 하나씩만 넣은 변형으로 돌려 보면 겨냥 줄이 서로 겹치지 않는다 (작성 시 확인: hex 만 → hex 줄, Discarded 만 → "only the memory-map part" 줄, fill 만 → fill 줄, 줄바꿈만 → 두 wrapped 줄).

## 4. 힌트

<details>
<summary>힌트 1 — 두 줄 형식: "이름만 있는 줄" 을 기억해 뒀다가 다음 줄과 합친다</summary>

`.text.Default_Handler` 처럼 이름이 열 15를 넘으면 ld 는 이름만 찍고 줄을 바꾼 뒤 16칸 들여서 `0x주소 0x크기 파일` 을 찍는다. 들여쓰기 0칸이면 출력 섹션(`._user_heap_stack`), 1칸이면 입력 섹션이다. "보류 중인 이름(pending)" 상태를 하나 두고, 다음 줄이 `0x.. 0x..` 로 시작하면 합치고 아니면 버린다. ` *(.text*)` 같은 패턴 줄도 "한 단어만 있는 줄" 이니 `*` 로 시작하는 것은 제외한다.

</details>

<details>
<summary>힌트 2 — "Discarded input sections" 와 "Linker script and memory map" 의 줄 모양은 똑같다</summary>

` .text.bist     0x00000000        0x6 sensor.o` 는 입력 섹션 줄과 구별이 안 된다. 주소가 0 이라는 것으로 거르면 안 된다(Flash 가 0번지에서 시작하는 MCU 도 많다). 파일 전체가 아니라 `Linker script and memory map` 줄 **이후만** 파싱하면 된다. `Memory Configuration` 의 `*default* 0x00000000 0xffffffff` 줄도 출력 섹션 헤더처럼 생겼다.

</details>

<details>
<summary>힌트 3 — `*fill*` 줄은 입력 섹션 정규식에도 걸린다</summary>

` *fill*         0x08000436        0x2 ` 는 `^ (\S+)\s+0x..\s+0x..` 에 그대로 맞는다 (이름 `*fill*`, 오브젝트 빈 문자열). fill 을 처리한 분기에서 `continue` 하지 않으면 아래 입력 섹션 분기가 한 번 더 센다.

</details>

<details>
<summary>힌트 4 — `"0x400".strip("0x")` 는 무엇인가?</summary>

`str.strip(chars)` 는 접두사가 아니라 **문자 집합**을 양 끝에서 지운다. `'0x400'.strip('0x')` → `'4'`. 0으로 끝나는 모든 크기가 틀린다 (`0x580` → `0x58` = 88). `int(tok, 16)` 은 `0x` 접두사를 그대로 받는다.

</details>

<details>
<summary>힌트 5 — diff 는 세 사전의 합집합 키로</summary>

`old.objects()` 와 `new.objects()` 의 키 합집합을 돌며 없는 쪽은 0. 섹션은 alloc 만, 심볼은 이름이 같은 것을 더해서(별칭·동명 static). 바뀐 것만 남긴다.

</details>

## 5. 풀이

### Part A — 줄 종류 → 레코드 → 세 장부

파서는 맵을 위에서 아래로 한 번 읽으며 현재 출력 섹션(`cur_out`)과 현재 입력 섹션(`cur_in`)을 들고 다닌다. 심볼 줄은 `cur_in` 에, fill 은 `cur_out` 에 붙는다.

```svg
<svg viewBox="0 0 750 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs>
    <marker id="m02a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
      <path d="M0 0 L10 5 L0 10 z" fill="currentColor"/>
    </marker>
  </defs>
  <rect x="10" y="10" width="200" height="300" rx="8" fill="none" stroke="currentColor"/>
  <text x="20" y="30" fill="currentColor" font-weight="bold" font-size="13">줄 하나 → 분기</text>
  <text x="20" y="54" fill="currentColor">0. pending 이름? → R_CONT</text>
  <text x="20" y="76" fill="currentColor">1. R_FILL → cur_out.fills</text>
  <text x="34" y="94" fill="currentColor">(continue 필수)</text>
  <text x="20" y="116" fill="currentColor">2. R_IN → 새 Input</text>
  <text x="20" y="138" fill="currentColor">3. R_OUT → 새 OutSec</text>
  <text x="20" y="160" fill="currentColor">4. R_SYM → cur_in.syms</text>
  <text x="20" y="182" fill="currentColor">5. 이름만 → pending</text>
  <text x="20" y="204" fill="currentColor">그 외 (패턴·대입·LOAD)</text>
  <text x="34" y="222" fill="currentColor">→ 무시</text>
  <text x="20" y="252" fill="currentColor">in_map = False 로 시작,</text>
  <text x="20" y="270" fill="currentColor">"Linker script and</text>
  <text x="20" y="288" fill="currentColor">memory map" 에서 True</text>
  <rect x="250" y="20" width="220" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="260" y="40" fill="currentColor" font-family="monospace">OutSec .bss</text>
  <text x="260" y="58" fill="currentColor" font-family="monospace">addr 0x20000028 size 1308</text>
  <text x="260" y="76" fill="currentColor" font-family="monospace">fills [4]</text>
  <rect x="250" y="110" width="220" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="260" y="130" fill="currentColor" font-family="monospace">Input .bss.g_rx_buf</text>
  <text x="260" y="148" fill="currentColor" font-family="monospace">1024  comm.o  syms[g_rx_buf]</text>
  <rect x="250" y="180" width="220" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="260" y="200" fill="currentColor" font-family="monospace">Input COMMON  260</text>
  <text x="260" y="218" fill="currentColor" font-family="monospace">legacy_board.o  syms[2]</text>
  <rect x="250" y="250" width="220" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="260" y="270" fill="currentColor" font-family="monospace">Input .bss.s_rx_head 4</text>
  <text x="260" y="288" fill="currentColor" font-family="monospace">comm.o  syms[] → 이름 유도</text>
  <path d="M360 90 L360 108" stroke="currentColor" marker-end="url(#m02a)"/>
  <rect x="510" y="20" width="230" height="80" rx="6" fill="none" stroke="currentColor"/>
  <text x="520" y="40" fill="currentColor" font-weight="bold">sections</text>
  <text x="520" y="60" fill="currentColor">헤더 크기 vs Σ입력 + Σfill</text>
  <text x="520" y="80" fill="currentColor">정답: size -A</text>
  <rect x="510" y="120" width="230" height="80" rx="6" fill="none" stroke="currentColor"/>
  <text x="520" y="140" fill="currentColor" font-weight="bold">objects</text>
  <text x="520" y="160" fill="currentColor">Σ입력 크기 (fill 제외)</text>
  <text x="520" y="180" fill="currentColor">정답: .o 섹션 − gc + COMMON</text>
  <rect x="510" y="220" width="230" height="80" rx="6" fill="none" stroke="currentColor"/>
  <text x="520" y="240" fill="currentColor" font-weight="bold">symbols</text>
  <text x="520" y="260" fill="currentColor">다음 심볼 주소 − 내 주소</text>
  <text x="520" y="280" fill="currentColor">정답: nm -S --size-sort</text>
  <path d="M470 55 L508 58" stroke="currentColor" marker-end="url(#m02a)"/>
  <path d="M470 140 L508 160" stroke="currentColor" marker-end="url(#m02a)"/>
  <path d="M470 210 L508 250" stroke="currentColor" marker-end="url(#m02a)"/>
</svg>
```

정규식 다섯 개가 핵심이다 (`solution.py`):

| 줄 | 정규식 | 예 (v1.map) |
|---|---|---|
| 출력 헤더 | `^(\S+)\s+0x(..)\s+0x(..)(?:\s+load address 0x(..))?\s*$` | `.data           0x20000000       0x28 load address 0x08000a98` |
| 입력 섹션 | `^ (\S+)\s+0x(..)\s+0x(..)\s*(.*?)\s*$` (1칸) | ` .text.memcpy   0x08000630       0x16 libutil.a(mem.o)` |
| fill | `^ \*fill\*\s+0x(..)\s+0x(..)\s*$` | ` *fill*         0x08000436        0x2 ` |
| 꺾인 줄의 둘째 줄 | `^\s+0x(..)\s+0x(..)(?:\s+load address 0x(..)\|\s+(.*?))?\s*$` | `                0x08000434        0x2 startup.o` |
| 심볼 | `^\s{16,}0x(..)\s+([A-Za-z_.$][\w.$@]*)\s*$` | `                0x08000434                Default_Handler` |

`_etext = .` 같은 대입 줄은 `=` 와 공백이 있어 심볼 정규식에 안 걸리고, `0x21 (size before relaxing)` 은 0x 가 하나뿐이라 둘째 줄 정규식에 안 걸린다. 정답 보고서 (실제 출력):

```text
$ python3 -I solution.py report v1.map -n 5
== v1.map: 7 alloc output sections, 28 input sections, 11 objects ==
-- Top 5 output sections --
  section                  size   inputs   fill
  ._user_heap_stack        3076        0   3076
  .text                    1408     1404      4
  .bss                     1308     1304      4
  .rodata                  1080     1077      3
  .isr_vector               216      216      0
-- Top 5 objects (flash = text+rodata+data, ram = data+bss) --
  object                       total   flash     ram
  comm.o                        1241     213    1044
  libgcc.a(_udivmoddi4.o)        816     816       0
  libutil.a(crc16.o)             556     556       0
  sensor.o                       548     544       4
  legacy_board.o                 296      36     260
-- Top 5 symbols --
  symbol                        size  object                 section
  g_rx_buf                      1024  comm.o                 .bss
  __udivmoddi4                   808  libgcc.a(_udivmoddi4.o) .text
  crc16_table                    512  libutil.a(crc16.o)     .rodata
  sensor_calibration_table       512  sensor.o               .rodata
  board_scratch                  256  legacy_board.o         .bss
```

읽는 법: `._user_heap_stack` 3076 B 는 **입력이 0, 전부 fill** 이다 — 링커 스크립트의 `. = . + _Min_Heap_Size` 예약이 맵에서는 `*fill* 0x400`, `*fill* 0x800` 으로 찍힌다 (+ 정렬 4). 이것을 "오브젝트 몫" 으로 돌리면 안 되고, 섹션 표에서 보이게 둔다. `comm.o` 의 RAM 1044 = `g_rx_buf` 1024 + `g_comm_stats` 16 (.data) + `s_rx_head` 4. `.data` 는 Flash(초기값) 와 RAM 둘 다에 들어가므로 flash 213 에도 16 이 포함된다(TOOL-B06). 64비트 나눗셈 한 줄(`g_uptime_ms / 1000u`)이 `libgcc` 에서 816 + 48 + 4 B 를 끌고 왔다는 것이 오브젝트 표 2위로 바로 보인다 — 맵 분석의 전형적인 첫 발견이다.

정답과 맞추기 — 맵 유도 크기와 `nm -S` 가 **우리 오브젝트의 28개 심볼**에서는 모두 같았다. 다른 것은 libgcc 쪽이었고, 이유가 있다 (v1 실측):

| 심볼 | 맵 유도 (입력 섹션) | `nm -S --size-sort` | 왜 |
|---|---|---|---|
| `__udivmoddi4` | 808 (0x328) | 806 (0x326) | 입력 섹션 끝 2 B 패딩까지 맵은 그 멤버 몫 |
| `__aeabi_idiv0` / `ldiv0` | 4 | 2 | `bx lr` (2 B) + `nop` 패딩 (objdump 로 확인) |
| `__aeabi_uldivmod` | 48 (0x30) | `0x30` (그러나 `nm -S` 는 크기 없음) | 어셈블리라 `.size` 없음 → `--size-sort` 가 다음 심볼까지 거리로 **만들어 낸** 값 |
| `_estack` | — | `e7fe00d8` | 링커 스크립트 심볼에 `--size-sort` 가 만든 가짜 크기 (다음 심볼 주소로 wrap-around 된 것으로 보인다) |

그래서 체커는 우리 오브젝트 심볼(각 `.o` 의 `nm --defined-only` 목록)만 비교하고 `_e*`/`_s*` 스크립트 심볼은 뺀다. "누가 메모리를 먹나" 에는 맵의 입력 섹션 크기가 맞는 답이다 — 패딩 2 B 도 이미지에 실제로 있고 그 멤버 때문에 생겼다.

오브젝트 정답은 맵과 독립인 길로 만든다: 각 `.o` 의 `size -A` alloc 섹션 합 − `--print-gc-sections` 가 지운 섹션 + COMMON(`nm -S` 의 `C` 심볼). v1 실제 gc 목록:

```text
arm-none-eabi-ld: removing unused section '.text.sensor_selftest_unused' in file 'sensor.o'
arm-none-eabi-ld: removing unused section '.text.bist' in file 'sensor.o'
arm-none-eabi-ld: removing unused section '.rodata.fmt_hex32_unused.str1.1' in file 'libutil.a(fmt.o)'
arm-none-eabi-ld: removing unused section '.text.fmt_hex32_unused' in file 'libutil.a(fmt.o)'
```

(실제 줄은 `/opt/homebrew/opt/arm-none-eabi-binutils/bin/arm-none-eabi-ld:` 로 시작.) `sensor.o` = `.text.sensor_filter_update` 32 + `.rodata.sensor_calibration_table` 512 + `.bss.s_filter_acc` 4 = 548 — 맵과 같다. `libutil.a(selftest.o)` 는 아무도 참조하지 않아 아카이브에서 꺼내지지도 않았다(TOOL-B09) — 맵 어디에도 없고 체커도 "not pulled" 를 확인한다. 보존 법칙: v1 Σ오브젝트 4049 + fill 3087 = 7136 = `size -A` alloc 합 (216 + 1408 + 1080 + 8 + 40 + 1308 + 3076).

### Part B — 네 가지 함정 (starter 의 버그)

**① 두 줄 형식을 버림.** 실측 규칙(binutils 2.47): 입력 섹션 이름 13자(`.text.fmt_u32`)까지 한 줄, 14자(`.rodata.str1.1`)부터 꺾임. 출력 섹션은 14자(`.debug_aranges`)까지 한 줄, 15자(`.ARM.attributes`, `._user_heap_stack`)부터 꺾임. 둘 다 "이름이 열 15 이상까지 가면 줄바꿈" 이다 (ld 소스 `ldlang.c` 의 `SECTION_NAME_MAP_LENGTH` 16 과 맞아 보인다 — 확인 권장). `-ffunction-sections` 를 쓰면 이름이 `.text.<함수명>` 이 되므로 **대부분이 꺾인다**: v1 의 입력 섹션 28개 중 한 줄짜리는 `.text.fmt_u32`, `.text.memcpy`, `.bss.g_rx_buf` 같은 짧은 것뿐이다. starter 는 꺾인 줄을 통째로 놓쳐서 `._user_heap_stack` 이 사라지고, 그 다음 심볼 줄들은 **직전 입력 섹션에 붙는다** — `Default_Handler` 가 `libgcc.a(_dvmd_tls.o)` 소속 0 B 로 나온 이유다. 조용히 틀린 숫자가 나오는 최악의 형태.

**② Discarded input sections 를 셈.** 그 부분의 줄은 입력 섹션 줄과 모양이 같다:

```text
Discarded input sections

 .text          0x00000000        0x0 startup.o
 ...
 .text.bist     0x00000000        0x6 sensor.o
```

starter 는 `in_map = True` 로 시작해서 이 줄들을 섹션 없는(`?`) 입력으로 넣고 `bist` 를 심볼 표에 올렸다. 또 `Memory Configuration` 의 `*default* 0x00000000 0xffffffff` 가 출력 섹션 헤더로 잡혀 4294967295 B 짜리 섹션이 생겼다 (starter `report` 실제 첫 줄: `*default*            4294967295        0      0`). 주소 0 으로 거르는 것은 답이 아니다 — nRF52 처럼 Flash 가 0번지인 칩이 있다.

**③ `*fill*` 이중 계산.** fill 분기 뒤 `continue` 가 없어서 같은 줄이 입력 섹션 정규식에도 맞았다: 이름 `*fill*`, 오브젝트 `""`. 체커 메시지 `fake *fill* inputs: 4` 와, starter `report` 의 오브젝트 표에 이름 없는 줄(`27 7 20`)이 그 흔적이다. fill 은 **출력 섹션의 몫**이다: 정렬 요구 때문에 생긴 빈칸이라 어느 오브젝트 탓으로도 돌릴 수 없다. 다만 줄일 수는 있다 (Q5).

**④ hex 파싱.** `int(tok.strip("0x") or "0", 16)` — `strip` 은 문자 집합 `{'0','x'}` 를 **양쪽 끝**에서 지운다. `0x400` → `4`, `0x580` → `58` (= 88). `0x438` 처럼 0으로 안 끝나면 맞으니 몇 개만 보고는 모른다. `lstrip` 이었어도 `0x0` → `''` 로 터지고, 정답은 그냥 `int(tok, 16)` (접두사 허용) 이다.

starter 와 solution 의 차이는 이 네 곳 + diff 본문뿐이다 (6. 정답 코드의 강조 줄).

### Part C — v1 → v2 diff 와 PR 코멘트

```text
$ python3 -I solution.py diff v1.map v2.map -n 5
== diff v1.map -> v2.map ==
-- output sections (top 4 of 4 changed, by |delta|) --
  .bss                            1308 ->    6432    +5124
  .text                           1408 ->    1540     +132
  .rodata                         1080 ->    1136      +56
  ._user_heap_stack               3076 ->    3072       -4
-- objects (top 3 of 3 changed, by |delta|) --
  comm.o                          1241 ->    4345    +3104
  telemetry.o                        0 ->    2196    +2196  (new)
  main.o                           164 ->     176      +12
-- symbols (top 5 of 7 changed, by |delta|) --
  g_rx_buf                        1024 ->    4096    +3072
  telemetry_frame_buffer             0 ->    2048    +2048  (new)
  telemetry_sample_and_pack          0 ->     120     +120  (new)
  [comm.o .rodata.str1.1]           33 ->      65      +32
  telemetry_channel_names            0 ->      24      +24  (new)
```

`--print-memory-usage` (gen_maps.sh 가 같이 받는다) 로 보면 RAM 4424 → 9544 B (+5120), FLASH 2752 → 2940 B (+188). RAM +5120 = `.bss` +5124 + `._user_heap_stack` −4.

```svg
<svg viewBox="0 0 750 230" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">RAM 0x20000000 — v1 4424 B vs v2 9544 B (실제 맵 주소, 1 px = 16 B)</text>
  <text x="10" y="60" fill="currentColor">v1</text>
  <rect x="40" y="40" width="3" height="30" class="f-acc" stroke="currentColor"/>
  <rect x="43" y="40" width="82" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="125" y="40" width="192" height="30" class="f-mut" stroke="currentColor"/>
  <text x="50" y="60" fill="currentColor">.bss 1308</text>
  <text x="132" y="60" fill="currentColor">heap+stack 3072 (+4 정렬)</text>
  <text x="40" y="88" fill="currentColor" font-family="monospace">.data 40</text>
  <text x="250" y="88" fill="currentColor" font-family="monospace">0x20001148</text>
  <text x="10" y="140" fill="currentColor">v2</text>
  <rect x="40" y="120" width="3" height="30" class="f-acc" stroke="currentColor"/>
  <rect x="43" y="120" width="82" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="125" y="120" width="192" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="317" y="120" width="128" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="445" y="120" width="192" height="30" class="f-mut" stroke="currentColor"/>
  <text x="50" y="140" fill="currentColor">v1 몫 1308</text>
  <text x="132" y="140" fill="currentColor">g_rx_buf +3072</text>
  <text x="322" y="140" fill="currentColor">telemetry +2052</text>
  <text x="452" y="140" fill="currentColor">heap+stack 3072 (정렬 0)</text>
  <text x="40" y="168" fill="currentColor">.bss 6432 = 1308 + 3072 + 2052</text>
  <text x="570" y="168" fill="currentColor" font-family="monospace">0x20002548</text>
  <text x="10" y="196" fill="currentColor">v1 의 _ebss = 0x20000544 는 8 의 배수가 아니라 ALIGN(8) 이 *fill* 4 B 를 넣었다.</text>
  <text x="10" y="216" fill="currentColor">v2 의 _ebss = 0x20001948 은 8 의 배수 → fill 0 → heap_stack 이 "4 B 줄어든" 것처럼 보인다.</text>
</svg>
```

맵 diff 를 그대로 믿으면 틀리는 두 줄:

1. **`._user_heap_stack` −4 B 는 변화가 아니다.** 예약 크기(0x400 + 0x800)는 그대로이고, 앞 섹션 끝 주소가 바뀌어 `ALIGN(8)` 패딩이 4 → 0 이 됐을 뿐이다. 반대로 어떤 PR 은 아무 변수도 안 늘렸는데 정렬 때문에 수 바이트가 늘 수 있다 — 작은 diff 는 fill 열부터 본다.
2. **`comm.o` +3104 B 중 32 B 는 `telemetry.o` 의 문자열이다.** v2 맵 실제:

```text
 .rodata.str1.1
                0x080006dc       0x41 comm.o
                                 0x21 (size before relaxing)
 ...
 .rodata.str1.1
                0x08000934        0x0 telemetry.o
                                 0x20 (size before relaxing)
```

`.rodata.str1.1` 은 `SHF_MERGE|SHF_STRINGS` 섹션이라 ld 가 모든 오브젝트의 문자열을 하나로 합치고(중복 제거 가능), 합친 크기 0x41 을 **첫 입력(comm.o)** 에 몰아 적은 뒤 telemetry.o 는 0 으로 둔다. 원래 크기는 `(size before relaxing)` 줄에 남는다 (comm 0x21 = 33, telemetry 0x20 = 32 — 각 `.o` 의 `size -A` 와 같다). 그래서 체커는 v2 의 오브젝트 비교에서 이 둘을 빼고(`skip merged-string objs comm.o,telemetry.o`), 연습 3 에서 바로잡는다. 심볼 표도 `[comm.o .rodata.str1.1] +32` 로 같은 착시를 보인다.

이것을 반영한 PR 코멘트 예:

```text
RAM +5120 B (4424 -> 9544 / 131072), FLASH +188 B
  .bss +5124: g_rx_buf 1024 -> 4096 (+3072, RX_BUF_SIZE=4096), telemetry_frame_buffer +2048 (new),
              s_tm_pos +4 (new)
  ._user_heap_stack -4: alignment fill only (reservation unchanged)
  .text +132: telemetry_sample_and_pack +120 (new), main +12
  .rodata +56: telemetry strings 32 (credited to comm.o by string merging) + telemetry_channel_names 24
```

다른 링커의 맵 — 같은 파서는 못 쓴다:

| | GNU ld (bfd) `-Map` | ld.lld `-Map` (VM, `lld_map_sample.txt`) | Apple ld64 `-map` (맥, `ld64_map_sample.txt`) |
|---|---|---|---|
| 모양 | 들여쓰기 + 긴 이름 줄바꿈 | 고정 열 표 `VMA LMA Size Align Out In Symbol` | `# Object files:` 번호표 + `# Sections:` + `# Symbols:` |
| 심볼 크기 | 없음 (유도해야 함) | 있음 (`main` 80) | 있음 (`0x0000002C _sensor_filter_update`) |
| 오브젝트 표기 | `libutil.a(fmt.o)` | `main.o:(.text.startup.main)` | `[  1]` → 번호표의 `sensor.o` |
| 패딩 | `*fill*` 줄 | 줄 없음 (주소 차이로만) | 없음 |
| 버려진 것 | `Discarded input sections` | (맵에 없음) | `# Dead Stripped Symbols:` `<<dead>>` |
| Thumb 함수 주소 | 짝수 (`0x08000434`) | 홀수 (`8000415 Default_Handler`) | (arm64 라 해당 없음) |

VM 실측(arm-none-eabi-gcc 14.2.1, GNU ld 2.45.50, LLD 21.1.8, 같은 오브젝트·같은 스크립트)에서 lld 는 고아 섹션 `.ARM.exidx` 를 `.text` **앞**에 16 B (`<internal>:(.ARM.exidx)`) 로 두었고 bfd 는 `.rodata` 뒤 8 B 였다 — 맵뿐 아니라 배치도 링커마다 다르다. ld64 맵은 심볼 크기와 `<<dead>>` 목록을 바로 주므로 오히려 파싱이 쉽다 (`demo_ld64_map.sh`, `ld PROJECT:ld-1266.8`). ld64 맵에서 `_g_rx_buf` 가 dead 로 나온 것은 데모의 호스트 `main` 이 쓰지 않아서다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```text
$ bash check.sh solution
PASS v1.map in the lesson dir == fresh gen_maps.sh build (reproducible)
PASS v2.map in the lesson dir == fresh gen_maps.sh build (reproducible)
PASS wrapped output header: ._user_heap_stack = 3076 B (size -A: 3076)
PASS wrapped input lines (name alone, addr/size on next line): Default_Handler=2 B startup.o, g_banner=24 B main.o, g_adc_raw=4 B main.o
PASS only the memory-map part parsed (Discarded input sections, Memory Configuration skipped): ok
PASS *fill* counted once, never as an input/object: .text/.rodata fill = 4/3 B (expect 4/3), fake *fill* inputs: 0 (map has 7 *fill* lines)
PASS hex sizes: g_rx_buf 0x400 -> 1024 B, .text 0x580 -> 1408 B
PASS v1 output sections == size -A (7 alloc sections)
PASS v1 every byte accounted: inputs + *fill* == section size
PASS v1 conservation: sum(objects) 4049 + fill 3087 == size -A total 7136
PASS v1 symbol sizes == nm -S --size-sort (28 own symbols)
PASS v1 per-object bytes == .o sections - gc'd + COMMON (8 objects), selftest.o not pulled
PASS v2 output sections == size -A (7 alloc sections)
PASS v2 every byte accounted: inputs + *fill* == section size
PASS v2 conservation: sum(objects) 9361 + fill 3083 == size -A total 12444
PASS v2 symbol sizes == nm -S --size-sort (32 own symbols)
PASS v2 per-object bytes == .o sections - gc'd + COMMON (7 objects; skip merged-string objs comm.o,telemetry.o), selftest.o not pulled
PASS report -n 5: top object 'comm.o', top symbol 'g_rx_buf'
PASS diff sections == size -A deltas: ._user_heap_stack -4, .bss +5124, .rodata +56, .text +132
PASS diff own symbols == nm deltas (6 changed): g_rx_buf +3072, telemetry_frame_buffer +2048, telemetry_sample_and_pack +120, telemetry_channel_names +24
PASS diff objects == truth deltas (non-merged): main.o +12
PASS human diff -n 5 shows telemetry.o (new)
22/22 passed
```

설계 메모:
- 주소는 `int(x, 16)`, 크기는 `hexnum()` — 같은 함수로 해도 되지만 starter 의 버그 위치를 한 곳으로 모으려고 나눴다.
- `pending` 은 (들여쓰기, 이름) 을 같이 기억한다: 0칸이면 출력 섹션, 1칸이면 입력 섹션으로 합친다. 둘째 줄이 안 오면(패턴·`/DISCARD/` 등) 그냥 버린다.
- 심볼 크기는 같은 주소의 별칭(`Default_Handler` 와 weak alias 4개)을 한 묶음으로 보고 "다음 **다른** 주소" 까지 잰다 — 다섯 개 모두 2 B, `nm` 과 같다. 오브젝트 합은 입력 섹션 크기로 세므로 별칭이 중복 계산되지 않는다.
- non-alloc 판정은 이름(`.debug*`, `.comment`, `.ARM.attributes` …)으로 한다. 맵에는 섹션 플래그가 없다. 확실히 하려면 `readelf -S` 의 `A` 플래그 / NOBITS 를 쓴다 (실측: `.bss NOBITS … WA`, `._user_heap_stack NOBITS`).

## 7. 자주 하는 실수

1. **한 줄 정규식만 씀** — `-ffunction-sections` 빌드에서 대부분의 입력 섹션이 두 줄이다. 놓친 섹션 뒤의 심볼은 엉뚱한 섹션에 붙어 "틀렸는데 그럴듯한" 숫자가 나온다. 방어: 섹션마다 Σ입력 + Σfill == 헤더 크기를 assert.
2. **파일 전체를 파싱** — Discarded input sections 와 Memory Configuration 이 섞여 들어온다. `Linker script and memory map` 이후만.
3. **fill 을 오브젝트에 붙이거나 두 번 셈** — fill 은 섹션 열로 따로. 보존식(Σobj + Σfill == 이미지)으로 잡힌다.
4. **`str.strip("0x")`** — 문자 집합 제거. `int(s, 16)`.
5. **맵 심볼 크기 == 함수 크기라고 믿음** — 정렬 패딩이 들어가고(`__udivmoddi4` 808 vs 806), 어셈블리 함수는 `nm` 크기가 없다. 반대로 `nm --size-sort` 는 크기 없는 심볼에 크기를 지어낸다(`_estack e7fe00d8`).
6. **병합 문자열을 첫 오브젝트 탓으로 돌림** — `.rodata.str1.*` 는 `(size before relaxing)` 로 원래 주인을 찾는다 (연습 3).
7. **non-alloc 섹션을 메모리로 셈** — `libgcc.a(_udivmoddi4.o)` 는 `.debug_*` 만 수 KB 다. 이것을 더하면 libgcc 가 1위가 된다. Flash 를 먹지 않는다.
8. **`-fdata-sections` 없이 맵 diff** — 오브젝트당 `.bss` 한 덩어리라 "누가" 를 모른다 (BM-B05). 크기 분석용 빌드는 섹션 분리 + `--gc-sections` 가 기본.

## 8. 면접 Q&A

<details>
<summary>Q1. 맵 파일에서 알 수 있는데 size/nm 로는 모르는 것은?</summary>

**누가(어느 .o / 아카이브 멤버) 어느 출력 섹션의 몇 바이트를 차지하는지**와 **왜 들어왔는지**다. `size` 는 섹션 합계, `nm -S` 는 심볼 크기만 주고 소속 오브젝트를 모른다(static 함수 이름 충돌도 구별 못 함). 맵은 입력 섹션마다 파일을 적고, 맨 앞 "Archive member included … (symbol)" 에 멤버를 끌어온 참조를 적는다 — 이 레슨에서는 `main.o (__aeabi_uldivmod)` 한 줄이 libgcc 868 B 의 이유였다. 또 `*fill*` 로 정렬 손실, `Discarded input sections` 로 gc 결과, `load address` 로 LMA 를 보여 준다. 꼬리: "그럼 맵만 보면 되나?" → 맵에는 심볼 크기와 섹션 플래그가 없어서 `nm -S`/`readelf -S` 와 맞춰 봐야 하고, 병합 문자열은 맵 자체가 첫 오브젝트에 몰아 적는다.

</details>

<details>
<summary>Q2. RAM 이 이번 PR 에서 5 KB 늘었습니다. 어떻게 원인을 찾나요?</summary>

위에서 아래로 좁힌다. ① 영역: `--print-memory-usage` 로 RAM 4424 → 9544 (+5120). ② 출력 섹션 diff: `.bss` +5124, `._user_heap_stack` −4. ③ 오브젝트 diff: `comm.o` +3104, `telemetry.o` 새로 +2196. ④ 심볼 diff: `g_rx_buf` +3072 (1024 → 4096), `telemetry_frame_buffer` +2048. ⑤ 소스: `RX_BUF_SIZE=4096` 와 `FEATURE_TELEMETRY`. 그리고 의심할 것: 작은 변화는 정렬 fill(heap_stack −4 처럼), 문자열 병합의 오귀속(comm.o 의 +32), `.data` 가 늘었다면 Flash 도 같이 는다는 것, `const` 가 빠져 `.rodata → .data` 로 옮긴 테이블(BM-B05). 마지막으로 그 버퍼가 정말 필요한 크기인지(최대 버스트 × 처리 지연)를 묻는다.

</details>

<details>
<summary>Q3. 맵에서 유도한 심볼 크기가 nm -S 와 다를 수 있나요?</summary>

있다. 맵에는 심볼 크기가 없어서 "다음 심볼 주소 − 내 주소" 로 구하는데, 그 사이에 정렬 패딩이나 이름 없는 데이터가 끼면 맵 쪽이 크다. 실측: `__udivmoddi4` 맵 808 / nm 806, `__aeabi_idiv0` 4 / 2 (`bx lr` 뒤 `nop`). `nm -S` 는 ELF `st_size`(컴파일러의 `.size` 지시어)라 정확하지만, 어셈블리 함수는 `.size` 가 없으면 0 이다 (`__aeabi_uldivmod`). 또 `nm --size-sort` 는 크기 없는 심볼에 다음 심볼까지의 거리를 크기로 만들어 넣어서 `_estack` 에 `e7fe00d8` 같은 값이 나왔다. 메모리 귀속(누가 이 바이트를 생기게 했나)에는 맵의 입력 섹션 크기, 함수 자체 크기에는 `st_size` 가 맞다.

</details>

<details>
<summary>Q4. 맵 파일 파싱은 왜 깨지기 쉽고, 대안은?</summary>

형식이 사람용이라 규격이 없다: 긴 이름 줄바꿈(열 15 기준, 실측), 들여쓰기로 줄 종류 구분, `(size before relaxing)` 같은 부가 줄, 링커마다 완전히 다른 형식(lld 는 열 표, ld64 는 `#` 섹션), 버전 차이. 대안: ① ELF 직접 읽기 — `readelf -S`/`nm -S` 로 섹션·심볼 크기 (오브젝트 소속은 모름), ② DWARF 의 컴파일 단위(CU)·라인 정보로 파일 귀속 — Bloaty McBloaty(`bloaty -d compileunits`) 같은 도구가 이 방식, ③ lld 를 쓰면 표 형식 맵이 파싱이 쉽다. 실무에서는 "링커 하나·버전 고정 + 체커로 Σ보존식 assert" 로 맵 파서를 쓰고, 이상하면 ELF 로 교차 확인한다 (이 레슨 check.sh 의 방식). Bloaty 동작 세부는 확인 권장.

</details>

<details>
<summary>Q5. *fill* 은 왜 생기고, 줄일 수 있나요?</summary>

입력 섹션마다 정렬 요구가 있고(`readelf -S` 의 Al 열; `uint64_t g_uptime_ms` 는 8), 링커가 다음 입력을 그 배수 주소에 놓으려고 빈칸을 넣는다. v1 `.bss` 의 `*fill* 0x2000002c 0x4` 는 4 B `g_adc_raw` 뒤에 8 정렬 `g_uptime_ms` 가 오면서 생겼다. Thumb 함수 사이(2 → 4 정렬)에도 2 B 씩 생긴다. 줄이는 법: 링커 스크립트에서 `SORT_BY_ALIGNMENT(.bss*)` 로 큰 정렬부터 배치, 구조체 멤버 순서 조정, 필요 이상 `aligned(…)` 금지, 함수 정렬 옵션(`-falign-functions`) 확인. 다만 이 레슨처럼 fill 이 heap/stack 예약(`. = . + size`)을 뜻하는 경우도 있으니 "fill = 낭비" 로 단정하면 안 된다. 링커 스크립트 정렬 세부는 TOOL-A07.

</details>

<details>
<summary>Q6. COMMON 은 무엇이고, 맵에서 왜 따로 보이나요?</summary>

초기값 없는 전역(`int board_rev;`)은 C 에서 "잠정 정의(tentative definition)" 이고, 옛 기본값 `-fcommon` 에서는 `.bss` 가 아니라 COMMON 심볼(`nm` 의 `C`)이 된다. 링커가 같은 이름의 COMMON 들을 하나로 합치고 크기를 정하므로, 맵 앞부분 "Allocating common symbols" 표(`board_scratch 0x100 legacy_board.o`)와 본문 ` COMMON 0x20000440 0x104 legacy_board.o` 로 보인다. 위험: 두 파일이 같은 이름 전역을 정의해도 링크 에러 없이 합쳐진다. GCC 10 / Clang 11 부터 기본이 `-fno-common` 이라(확인 권장) 새 코드는 `.bss.<name>` 로 나오고, 벤더 레거시 파일을 `-fcommon` 으로 빌드하면 이 레슨처럼 섞인다. 파서는 `COMMON` 을 입력 섹션 이름으로 똑같이 처리하면 되고, `-fdata-sections` 의 혜택(변수별 섹션, gc)을 못 받는다는 점만 기억한다.

</details>

<details>
<summary>Q7. 이걸 CI 크기 게이트로 만든다면?</summary>

① 빌드는 결정적이어야 한다(같은 소스 → 같은 맵; 이 레슨 check.sh 첫 두 줄이 그 확인, 재현 가능한 빌드는 TOOL-A06). ② main 브랜치의 맵/ELF 를 아티팩트로 저장하고 PR 빌드와 diff. ③ 규칙: 영역별 절대 예산(Flash ≤ 90 %, RAM 은 스택 여유 포함)과 PR 당 증가 한도(예: RAM +4 KB) — 넘으면 실패하고 범인 Top-N 심볼을 출력 (연습 5: `GATE FAIL RAM +5120 B (limit +4096 B)` + `g_rx_buf +3072`). ④ 의도된 증가는 승인 라벨이나 예산 파일 갱신으로 통과(ratchet). ⑤ 숫자 출처는 `--print-memory-usage`/`size` 로 교차 확인. 펌웨어에서는 부트로더/앱 슬롯 크기(BM-B06), OTA 이미지 크기, 스택 여유(TOOL-A08)까지 같은 게이트에 넣는 경우가 많다.

</details>

<details>
<summary>Q8. "Discarded input sections" 와 --gc-sections 의 관계, 그리고 위험은?</summary>

`-ffunction-sections -fdata-sections` 로 함수·변수마다 섹션을 만들고 `--gc-sections` 가 엔트리(`ENTRY(Reset_Handler)`)와 `KEEP()` 에서 시작해 참조되지 않는 섹션을 지운다. 지운 목록이 맵의 "Discarded input sections"(빈 `.text`/`.data`/`.bss` 포함)와 `--print-gc-sections` 출력이다 — v1 에서 `sensor_selftest_unused` 36 B, `bist` 6 B, `fmt_hex32_unused` 40 B + 문자열 17 B. 지운 것을 크기에 세면 안 된다. 위험: 코드에서 이름으로 참조하지 않지만 필요한 것(벡터 테이블, 커스텀 섹션 등록 테이블 — TOOL-M03)은 `KEEP` 이 없으면 사라진다. 이 레슨 `fw.ld` 는 `KEEP(*(.isr_vector))` + `used` 속성으로 막았다. 아카이브 멤버 단위 선택(TOOL-B09)은 그보다 앞 단계이고, `selftest.o` 는 아예 꺼내지지 않아 Discarded 목록에도 없다. 단계별 크기 다이어트는 TOOL-M13.

</details>

## 9. 추가 연습

`practice.py` = `solution.py` + 아래 다섯 기능. `bash check.sh practice` 가 solution 의 22줄에 6줄을 더 검사한다.

### 연습 1 — 메모리 영역 사용량 (`regions`)

"Memory Configuration" 을 읽어 FLASH/RAM 사용량을 계산하라. `--print-memory-usage` 와 바이트까지 같아야 한다.

<details>
<summary>답 보기</summary>

출력 섹션마다 VMA 가 속한 영역에 크기를 더하고, **내용이 있는** 섹션이 다른 영역에 LMA 를 가지면 그 영역에도 더한다(`.data` = RAM + FLASH). 맵은 `.bss` 와 `._user_heap_stack` 에도 `load address` 를 찍지만 내용이 없으므로(입력이 `.bss*`/`COMMON` 뿐이거나 fill 뿐) Flash 에 더하지 않는다 (BM-B05 의 함정과 같은 것). 실제: v1 `FLASH 2752 / RAM 4424`, v2 `FLASH 2940 / RAM 9544` — `--print-memory-usage` 와 같다. FLASH 2752 = 216 + 1408 + 1080 + 8 + 40.

</details>

### 연습 2 — 아카이브별 합 (`archives`)

`libutil.a(x.o)` 들을 `libutil.a` 로 묶어라. 벤더 라이브러리·libgcc·libc 가 얼마를 먹는지 한 줄로 보이게.

<details>
<summary>답 보기</summary>

오브젝트 이름의 `(` 앞을 키로: v1 `*.o 2519`, `libgcc.a 868`, `libutil.a 662`. 체커의 정답: libutil 은 실제로 링크된 멤버(`crc16 556 + fmt 68 + mem 38`)의 `.o` 크기, libgcc 는 `size -A` 합 − fill − 우리 오브젝트 합 (보존식) = 868. newlib 을 쓰는 빌드에서는 여기에 `libc_nano.a` 가 크게 나온다 (printf 비용 — TOOL-B06, TOOL-M13).

</details>

### 연습 3 — 병합된 문자열을 주인에게 (`objects --unmerge`)

v2 에서 `comm.o` 4345 / `telemetry.o` 2196 은 문자열 병합 때문에 32 B 어긋났다. `(size before relaxing)` 줄로 바로잡아라. 중복 제거로 줄어든 바이트는 어디에 두나?

<details>
<summary>답 보기</summary>

`R_BEFORE` 로 입력 섹션의 병합 전 크기를 저장하고, (출력 섹션, 입력 이름) 묶음마다 각 오브젝트에 자기 `before` 를 주고 맵 크기를 뺀다. 합친 크기 − Σbefore (중복 제거로 아낀 양, ≤ 0) 는 가짜 오브젝트 `*merge-saved*` 로 둬서 보존식이 깨지지 않게 한다. 실제: `comm.o 4313`, `telemetry.o 2228` — 각 `.o` 의 `size -A` 기반 정답과 같다. 이번 빌드는 겹치는 문자열이 없어 `0x41 = 0x21 + 0x20`, saved 0. 두 파일이 같은 문자열 리터럴을 쓰면 saved 가 음수로 나온다 (이 부분은 이 레슨에서 실측하지 않았다).

</details>

### 연습 4 — 이 멤버는 왜 들어왔나 (`why`)

`why v1.map _udivmoddi4.o` 가 끌어온 파일과 심볼을 보여 주게 하라. 멤버 이름이 길면 참조 정보는 다음 줄로 간다.

<details>
<summary>답 보기</summary>

"Archive member included to satisfy reference by file (symbol)" 부분: 0열에 멤버, 같은 줄 또는 다음 줄(30칸 들여씀)에 `파일 (심볼)`. 실제 출력: `libgcc.a(_udivmoddi4.o) <- libgcc.a(_aeabi_uldivmod.o) (__udivmoddi4)`, `libutil.a(mem.o) <- startup.o (memcpy)`. 체인을 따라가면 `main.o (__aeabi_uldivmod)` 가 뿌리다 — `uint64_t / 1000u` 한 줄이 868 B. 32비트 밀리초 카운터나 `* 0x10624DD3 >> 38` 같은 상수 곱셈으로 바꿀지 판단하는 근거가 된다 (정확도 조건은 따로 확인).

</details>

### 연습 5 — CI 게이트 (`gate`)

`gate OLD NEW --max-ram-growth N [--max-flash-growth N]`: 한도를 넘으면 exit 1 과 함께 그 영역에서 가장 많이 늘어난 심볼을 출력하라.

<details>
<summary>답 보기</summary>

연습 1 의 `region_usage()` 차이로 판정하고, diff 의 심볼 행 중 그 영역(심볼이 속한 출력 섹션의 VMA 영역)에서 늘어난 것을 큰 순으로. 실제:

```text
$ python3 -I practice.py gate v1.map v2.map --max-ram-growth 4096; echo rc=$?
GATE FAIL RAM +5120 B (limit +4096 B)
     g_rx_buf                     +3072 B  (comm.o)
     telemetry_frame_buffer       +2048 B  (telemetry.o)
     s_tm_pos                     +4 B  (telemetry.o)
rc=1
$ python3 -I practice.py gate v1.map v2.map --max-ram-growth 8192 --max-flash-growth 256; echo rc=$?
GATE OK   RAM +5120 B (limit +8192 B)
GATE OK   FLASH +188 B (limit +256 B)
rc=0
```

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
bash check.sh practice     # 28/28 passed
```

```text
...                                   (solution 과 같은 22줄 PASS)
PASS P3 --unmerge: SHF_MERGE strings re-attributed: comm.o 4313/4313, telemetry.o 2228/2228
PASS P1 regions v1 == --print-memory-usage: FLASH 2752/2752, RAM 4424/4424
PASS P1 regions v2 == --print-memory-usage: FLASH 2940/2940, RAM 9544/9544
PASS P2 archives: libutil.a 662 (truth 662), libgcc.a 868 (truth 868)
PASS P4 why: 'libgcc.a(_udivmoddi4.o) <- libgcc.a(_aeabi_uldivmod.o) (__udivmoddi4)' / 'libutil.a(mem.o) <- startup.o (memcpy)'
PASS P5 gate RAM +5120 B: limit 4096 -> rc 1 (names g_rx_buf: True), limit 8192 -> rc 0
28/28 passed
```

## 10. 핵심 정리

- 파싱은 `Linker script and memory map` 이후만. 줄 종류: 출력 헤더(0칸) · 입력(1칸) · 두 줄 형식(이름이 열 15 이상) · `*fill*` · 심볼(16칸+) · 대입/패턴(무시).
- 세 장부: 섹션(헤더 = Σ입력 + Σfill), 오브젝트(Σ입력, `libx.a(m.o)`), 심볼(다음 주소까지 거리). Σ오브젝트 + Σfill == `size -A` alloc 합이 성립해야 믿는다.
- 함정: 긴 이름 줄바꿈 무시, Discarded 를 셈, fill 이중 계산(`continue`), `strip("0x")`, non-alloc(.debug) 을 메모리로 셈.
- 맵의 착시: 병합 문자열(`.rodata.str1.1`)은 첫 오브젝트에 몰림 → `(size before relaxing)` 로 복원. 정렬 fill 은 아무 변화 없이도 ±수 B. 심볼 크기는 패딩 포함(808 vs nm 806).
- 회귀 추적 순서: 영역(`--print-memory-usage`) → 섹션 → 오브젝트 → 심볼 → 소스. v1→v2: RAM +5120 = `g_rx_buf` +3072 + telemetry +2052 − 정렬 4.
- 맵 형식은 링커 의존: GNU ld(들여쓰기) / lld(열 표, 심볼 크기 있음, Thumb 주소 홀수) / ld64(`# Symbols:`, `<<dead>>`). 정답은 언제나 ELF(`size -A`, `nm -S`, `readelf -S`)로 교차 확인.

## 참고자료

- GNU ld manual — `-Map`, `--print-memory-usage`, `--gc-sections`, `--print-gc-sections`, `--cref`, `SORT_BY_ALIGNMENT`: <https://sourceware.org/binutils/docs/ld/Options.html>, <https://sourceware.org/binutils/docs/ld/Input-Section-Basics.html>
- GNU binutils — `size -A`, `nm -S --size-sort`, `readelf -S`: <https://sourceware.org/binutils/docs/binutils/>
- binutils 소스 `ld/ldlang.c` (맵 출력 함수, `SECTION_NAME_MAP_LENGTH`): <https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=ld/ldlang.c>
- LLD — `-Map` 출력 형식: <https://lld.llvm.org/>
- Apple `man ld` — `-map map_file_path`, `-dead_strip` (이 맥의 로컬 man 페이지)
- ELF gABI — `SHF_MERGE`, `SHF_STRINGS` (문자열 병합): <https://refspecs.linuxfoundation.org/elf/gabi4+/ch4.sheader.html>
- Memfault Interrupt — "Best Firmware Size Tools" (map/size 분석 도구 비교), "Code Size Optimization": <https://interrupt.memfault.com/blog/best-firmware-size-tools>
- Google Bloaty McBloaty: <https://github.com/google/bloaty>
- 관련 레슨: BM-B05 (맵 눈으로 읽기·예산표), TOOL-B06 (size text/data/bss), TOOL-B09 (아카이브 멤버 선택), TOOL-M01 (링커 스크립트·VMA/LMA), TOOL-M03 (커스텀 섹션·KEEP), TOOL-M13 (크기 다이어트), TOOL-M14 (DWARF), TOOL-A06 (재현 가능한 빌드), TOOL-A07 (링커 스크립트 고급), TOOL-A08 (스택 사용량), BM-B06 (부트로더/앱 분할)
