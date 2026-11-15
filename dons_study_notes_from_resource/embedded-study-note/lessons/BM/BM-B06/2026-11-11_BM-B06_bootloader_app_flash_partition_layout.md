---
id: BM-B06
time: 60분
updated: 2026-11-11
---
# BM-B06 부트로더+앱 플래시 분할 레이아웃 설계

## 학습 목표

- 1 MB 플래시를 **BOOT / META / NVS / SLOT_A / SLOT_B / SCRATCH** 로 나누고, 각 경계가 왜 **섹터(sector, 지우기 단위) 경계**여야 하는지 섹터 맵으로 설명할 수 있다.
- 공통 `layout.ld` 하나를 `INCLUDE` 해서 `boot.ld` 와 `app.ld` 를 만들고, 레이아웃 심볼(`__slot_a_start` …)을 C 에서 주소로 쓰는 법을 안다 (C 에 주소 하드코딩 금지).
- 슬롯 시작에 **이미지 헤더(image header)**, +0x200 에 벡터 테이블을 고정하고, 두 이미지가 공유하는 **`.noinit` 부트 사유 구조체**를 같은 RAM 주소에 두는 법을 링커 `ASSERT()` 로 강제할 수 있다.
- `objdump -h` 와 맵 파일(map file)을 읽어 VMA/LMA·ALLOC/LOAD 를 해석하고, **ld 가 못 보는 겹침**(따로 링크된 두 이미지)을 잡는 빌드 후 검사기를 C 로 짤 수 있다.
- 실제로 일어나는 실수 — 섹션 이름 오타(orphan), `NOLOAD` 누락, `KEEP` 누락 + `--gc-sections`, 헤더 앞에 끼어든 데이터 — 가 각각 어떤 증상·어떤 메시지로 나타나는지 안다.

## 1. 문제

필드 업데이트(OTA, 시리얼 업데이트)가 있는 제품은 거의 전부 "부트로더 + 앱 슬롯" 구조이고, 면접에서는 **"플래시 맵을 그려 보세요. 경계는 어디에, 왜?"** 가 단골이다 — 틀린 레이아웃은 한 번 출하되면 바꿀 수 없다 (부트로더는 보통 필드에서 안 바꾼다).

Don 이 SSD 컨트롤러에서 본 "부트 ROM → 1 차 로더 → FW 슬롯 2 개 + 메타" 구조와 똑같다. MCU 에서는 그 경계를 **링커 스크립트(linker script)** 가 정한다. 문제는 부트로더와 앱이 **서로 다른 프로그램으로 따로 링크된다**는 것: 각 링크는 자기 자신만 보므로, 앱이 부트로더 영역을 침범해도, 둘이 공유한다고 믿는 RAM 구조체가 서로 다른 주소에 있어도 ld 는 조용하다 (아래에서 실제로 확인). 그래서 ① 공통 레이아웃 파일 하나에서 두 스크립트를 파생시키고 ② `ASSERT()` 로 불변식을 링크 시점에 막고 ③ 빌드 산출물(`objdump -h`, 맵 파일)을 **빌드 후 검사기**로 한 번 더 검증한다.

대상은 STM32F407 류(Cortex-M4, 1 MB 플래시, 128 KB SRAM). 섹터 맵은 **RM0090 기준 대표값 — RM 확인 권장**: 섹터 0–3 = 16 KB, 섹터 4 = 64 KB, 섹터 5–11 = 128 KB.

```svg
<svg viewBox="0 0 740 430" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">1 MB 플래시 분할 — 섹터 맵 위에 영역 (layout.ld, 막대 길이는 비례 아님)</text>
  <text x="20" y="36" fill="currentColor">섹터</text><text x="62" y="36" fill="currentColor">시작 주소</text><text x="160" y="36" fill="currentColor">크기</text>
  <g font-family="monospace">
    <text x="28" y="56" fill="currentColor">0</text><text x="62" y="56" fill="currentColor">0x08000000</text><text x="160" y="56" fill="currentColor">16K</text>
    <text x="28" y="80" fill="currentColor">1</text><text x="62" y="80" fill="currentColor">0x08004000</text><text x="160" y="80" fill="currentColor">16K</text>
    <text x="28" y="104" fill="currentColor">2</text><text x="62" y="104" fill="currentColor">0x08008000</text><text x="160" y="104" fill="currentColor">16K</text>
    <text x="28" y="128" fill="currentColor">3</text><text x="62" y="128" fill="currentColor">0x0800C000</text><text x="160" y="128" fill="currentColor">16K</text>
    <text x="28" y="152" fill="currentColor">4</text><text x="62" y="152" fill="currentColor">0x08010000</text><text x="160" y="152" fill="currentColor">64K</text>
    <text x="28" y="176" fill="currentColor">5</text><text x="62" y="176" fill="currentColor">0x08020000</text><text x="160" y="176" fill="currentColor">128K</text>
    <text x="28" y="200" fill="currentColor">6</text><text x="62" y="200" fill="currentColor">0x08040000</text><text x="160" y="200" fill="currentColor">128K</text>
    <text x="28" y="224" fill="currentColor">7</text><text x="62" y="224" fill="currentColor">0x08060000</text><text x="160" y="224" fill="currentColor">128K</text>
    <text x="28" y="248" fill="currentColor">8</text><text x="62" y="248" fill="currentColor">0x08080000</text><text x="160" y="248" fill="currentColor">128K</text>
    <text x="28" y="272" fill="currentColor">9</text><text x="62" y="272" fill="currentColor">0x080A0000</text><text x="160" y="272" fill="currentColor">128K</text>
    <text x="22" y="296" fill="currentColor">10</text><text x="62" y="296" fill="currentColor">0x080C0000</text><text x="160" y="296" fill="currentColor">128K</text>
    <text x="22" y="320" fill="currentColor">11</text><text x="62" y="320" fill="currentColor">0x080E0000</text><text x="160" y="320" fill="currentColor">128K</text>
    <text x="62" y="340" fill="currentColor">0x08100000 = 끝</text>
  </g>
  <rect x="215" y="42" width="230" height="46" class="f-acc" stroke="currentColor"/>
  <text x="225" y="62" fill="currentColor" font-weight="bold">BOOT 32 KB</text><text x="225" y="78" fill="currentColor">리셋 벡터 0x08000000</text>
  <rect x="215" y="90" width="230" height="46" class="f-warn" stroke="currentColor"/>
  <text x="225" y="110" fill="currentColor" font-weight="bold">META 32 KB (16 KB x 2)</text><text x="225" y="126" fill="currentColor">부트 상태 이중화 (BM-A06)</text>
  <rect x="215" y="138" width="230" height="22" class="f-mut" stroke="currentColor"/>
  <text x="225" y="154" fill="currentColor">NVS 64 KB — 앱 설정 (예약)</text>
  <rect x="215" y="162" width="230" height="70" class="f-ok" stroke="currentColor"/>
  <text x="225" y="184" fill="currentColor" font-weight="bold">SLOT_A 384 KB — 실행 슬롯</text><text x="225" y="202" fill="currentColor">+0x000 헤더, +0x200 벡터</text><text x="225" y="220" fill="currentColor">앱은 여기 주소로 링크</text>
  <rect x="215" y="234" width="230" height="70" class="f-ok" stroke="currentColor"/>
  <text x="225" y="256" fill="currentColor" font-weight="bold">SLOT_B 384 KB — 다운로드</text><text x="225" y="274" fill="currentColor">새 이미지 수신, 검증 후 교체</text>
  <rect x="215" y="306" width="230" height="22" class="f-mut" stroke="currentColor"/>
  <text x="225" y="322" fill="currentColor">SCRATCH 128 KB — 스왑용</text>
  <text x="460" y="62" fill="currentColor">작은 섹터 4 개 = 부트로더와</text>
  <text x="460" y="78" fill="currentColor">자주 지우는 메타데이터에 딱</text>
  <text x="460" y="184" fill="currentColor">A 와 B 는 같은 크기,</text>
  <text x="460" y="200" fill="currentColor">둘 다 128 KB 섹터 3 개</text>
  <text x="460" y="322" fill="currentColor">≥ 가장 큰 섹터 (스왑 1 단위)</text>
  <rect x="20" y="360" width="90" height="26" class="f-warn" stroke="currentColor"/>
  <text x="26" y="378" fill="currentColor">SHARED 256B</text>
  <rect x="110" y="360" width="335" height="26" class="f-mut" stroke="currentColor"/>
  <text x="120" y="378" fill="currentColor">RAM 0x20000100 – 0x20020000 (.data .bss 스택, 이미지마다)</text>
  <text x="20" y="404" fill="currentColor" font-family="monospace">0x20000000</text>
  <text x="460" y="372" fill="currentColor">SHARED = .noinit 부트 사유 구조체</text>
  <text x="460" y="390" fill="currentColor">boot·app 둘 다 0x20000000 에 둔다</text>
  <text x="20" y="422" fill="currentColor">모든 영역 경계 = 섹터 경계. 섹터 하나를 지우면 그 안의 모든 것이 지워진다.</text>
</svg>
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">하나의 계약에서 두 이미지 → 빌드 후 검사 (이 레슨의 흐름)</text>
  <rect x="20" y="40" width="130" height="50" class="f-acc" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor" font-weight="bold">layout.ld</text><text x="30" y="78" fill="currentColor">MEMORY + ASSERT</text>
  <rect x="20" y="110" width="130" height="50" class="f-acc" stroke="currentColor"/>
  <text x="30" y="130" fill="currentColor" font-weight="bold">shared.h</text><text x="30" y="148" fill="currentColor">헤더·공유 구조체</text>
  <rect x="200" y="40" width="130" height="50" class="f-mut" stroke="currentColor"/>
  <text x="210" y="60" fill="currentColor">boot.ld + boot.c</text><text x="210" y="78" fill="currentColor">INCLUDE layout.ld</text>
  <rect x="200" y="110" width="130" height="50" class="f-mut" stroke="currentColor"/>
  <text x="210" y="130" fill="currentColor">app.ld + app.c</text><text x="210" y="148" fill="currentColor">INCLUDE layout.ld</text>
  <path d="M150 65 L200 65 M150 65 L200 135 M150 135 L200 65 M150 135 L200 135" stroke="currentColor" fill="none"/>
  <rect x="380" y="40" width="120" height="120" class="f-warn" stroke="currentColor"/>
  <text x="390" y="62" fill="currentColor" font-weight="bold">ld (VM)</text><text x="390" y="82" fill="currentColor">링크마다 따로</text><text x="390" y="100" fill="currentColor">ASSERT 평가</text><text x="390" y="118" fill="currentColor">region overflow</text><text x="390" y="136" fill="currentColor">섹션 LMA 겹침</text>
  <path d="M330 65 L380 65 M330 135 L380 135" stroke="currentColor" fill="none"/>
  <rect x="550" y="40" width="170" height="120" class="f-mut" stroke="currentColor"/>
  <text x="560" y="62" fill="currentColor">boot.elf / app.elf</text><text x="560" y="82" fill="currentColor" font-family="monospace">objdump -h</text><text x="560" y="100" fill="currentColor" font-family="monospace">objdump -s -j .img_hdr</text><text x="560" y="118" fill="currentColor" font-family="monospace">*.map Memory Config</text><text x="560" y="136" fill="currentColor">→ cap_*.txt</text>
  <path d="M500 100 L550 100" stroke="currentColor" fill="none"/>
  <rect x="200" y="190" width="520" height="46" class="f-ok" stroke="currentColor"/>
  <text x="210" y="210" fill="currentColor" font-weight="bold">Mac 검사기: parse → layout_check → image_check → shared_consistent</text>
  <text x="210" y="228" fill="currentColor">ld 가 못 보는 것: 이미지 사이 겹침 · .noinit 주소 일치 · 헤더 내용</text>
  <path d="M635 160 L635 190" stroke="currentColor" fill="none"/>
</svg>
```

빌드는 Lima VM `elinux` 의 `arm-none-eabi-gcc 14.2.1` / GNU ld 2.45.50 으로 한다 (실행은 하지 않는다 — 부트로더→앱 점프는 BM-A03). 레슨 디렉터리 파일:

| 파일 | 역할 |
|---|---|
| `layout.ld` | MEMORY 8 영역 + 레이아웃 심볼 + 레이아웃 ASSERT 10 개 (유일한 진실) |
| `boot.ld` / `app.ld` | `INCLUDE layout.ld` + SECTIONS + 이미지별 ASSERT |
| `shared.h` | `img_hdr_t`(32 B), `boot_share_t`(32 B) — `_Static_assert` 로 크기 고정 |
| `boot.c` / `app.c` | 아주 작은 부트로더·앱 (`/* ENV: qemu */`, 빌드만) |
| `run_vm.sh` | `build` / `bad`(실수 9 종 재현) / `capture`(검사기 입력 생성) |
| `cap_*.txt`, `ld_errors.txt` | VM 에서 뜬 실제 출력 (검사기 입력, 레슨 본문 인용) |

### Part A — 공통 레이아웃에서 두 링커 스크립트 파생 + 캡처 파싱

`layout.ld` 의 MEMORY·심볼·ASSERT 를 읽고 VM 에서 두 이미지를 빌드한다. 그다음 Mac 검사기에서 캡처를 파싱한다.
- `int parse_sections(const char *text, sect_t *out, int cap)` — `objdump -h` 출력 → 이름·크기·VMA·LMA·플래그(CONTENTS/ALLOC/LOAD/READONLY/CODE). cap 초과면 -1.
- `int parse_memcfg(const char *text, layout_t *L)` — 맵 파일의 `Memory Configuration` 블록 → 영역 표 (`*default*` 제외).

### Part B — 레이아웃 검사: 겹침·섹터 정렬·플래시 범위

- `int sector_index(const flash_geom_t *g, uint32_t addr, uint32_t *start)` — 비균일 섹터 맵에서 주소 → 섹터.
- `unsigned layout_check(const layout_t *L, const flash_geom_t *g)` — 오류 비트마스크: `L_OVERLAP`, `L_ALIGN`, `L_OUTSIDE`, `L_SLOT_SIZE`, `L_EMPTY`, `L_MISSING`.
- 주입: 실제로 SLOT_B 를 0x08060000 으로 옮긴 빌드의 맵(ASSERT 를 뺀 빌드), 그리고 메모리 안에서 BOOT 24 KB(섹터 중간), META 0x08009000, SCRATCH 256 KB, SLOT_B 256 KB, 길이 0, 슬롯 이름 변경, SHARED/RAM 겹침. 무작위 레이아웃 400 개는 4 KB 단위 칠하기(brute force)와 판정 비교.

### Part C — 이미지 검사: 헤더 위치, 벡터 테이블, 공유 .noinit

- `unsigned image_check(const image_t *img, const layout_t *L, const char *home)` — 섹션이 허용 영역 안인지, `.noinit` 이 NOLOAD 이고 SHARED 시작에 있는지, `.isr_vector` 가 home 시작(+0x200)·512 정렬인지, 슬롯 이미지면 헤더 섹션·magic·필드(`hdr_size`, `load_addr`, `vector_addr`)·`img_size` 까지.
- `bool shared_consistent(const image_t *a, const image_t *b)` — 두 이미지의 공유 구조체가 같은 주소·같은 크기·둘 다 NOLOAD.
- 주입: **실제 고장 빌드 2 개** (섹션 이름 오타 → orphan `.noinit_shared`, 다른 파일의 데이터가 헤더 앞에 링크됨) + 메모리 안 주입 9 종.

## 2. Problem (English)

> **Partition a 1 MB STM32F4 flash for a bootloader and an A/B application, and prove the layout is consistent.**
>
> Flash sectors: 4 × 16 KB, 1 × 64 KB, 7 × 128 KB starting at 0x08000000. Regions: BOOT (32 KB), META (2 × 16 KB), NVS (64 KB), SLOT_A and SLOT_B (384 KB each), SCRATCH (128 KB); RAM: SHARED (256 B at 0x20000000) + RAM.
>
> 1. Write `layout.ld` (MEMORY + symbols + `ASSERT`s) and derive `boot.ld` and `app.ld` from it with `INCLUDE`. The app image starts with a 32-byte header at the slot start (`magic, hdr_size=0x200, load_addr, vector_addr, img_size, fw_version, crc32`); its vector table is at slot + 0x200. Both images place a 32-byte `boot_share_t` in a NOLOAD `.noinit` section at exactly 0x20000000. The linker must fill `load_addr`, `vector_addr` and `img_size`.
> 2. Write a post-link checker that parses `objdump -h`, `objdump -s -j .img_hdr` and the map file's *Memory Configuration*:
>    `int sector_index(g, addr, &start)`, `unsigned layout_check(L, g)` (overlap, sector alignment, out of flash, slot size mismatch, empty, missing), `unsigned image_check(img, L, home)`, `bool shared_consistent(boot, app)`.
> 3. Inject: overlapping slots, a region ending mid-sector, a header that is not the first word of the slot, a `.noinit` section renamed by a typo, `.noinit` without NOLOAD, a vector table at +0x100.
>
> Example (real build): `app.elf` → `.img_hdr 0x20 @ 0x08020000`, `.isr_vector 0x40 @ 0x08020200`, `.data VMA 0x20000100 LMA 0x08020388`, `.noinit 0x20 @ 0x20000000 ALLOC` → `image_check == 0`; header bytes `494d4731 00020100 00000208 00020208 8c030000 …` → magic "IMG1", hdr_size 0x200, load 0x08020000, vectors 0x08020200, img_size 0x38c = last LOAD byte (0x0802038c) − slot start.
>
> Follow-ups: why can't ld detect that the app overlaps the bootloader? Why must slot boundaries be sector boundaries? Why `.noinit` + NOLOAD, and what does a missing NOLOAD do to `objcopy -O binary`? Why is the vector table at +0x200 and not +0x20? How do you run the same image from slot B?

## 3. 시작 코드

<!-- code: starter.c -->

**레슨 디렉터리에서** 실행한다 (`cap_*.txt` 를 읽는다. 다른 곳이면 첫 인자로 디렉터리를 준다):

```bash
cd <ROOT>/lessons/BM/BM-B06
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-B06_st && /tmp/BM-B06_st
```

처음 실행 결과 (실제 출력 앞부분 — FAIL 38 줄):

```text
FAIL starter.c:264  app.n == 17 && boot.n == 16
FAIL starter.c:267  s && s->size == 0x20u && s->vma == 0x08020000u && s->lma == 0x08020000u && s->flags == (SF_CONTENTS | SF_ALLOC | SF_LOAD | SF_READONLY)
FAIL starter.c:269  s && s->size == 4u && s->vma == 0x20000100u && s->lma == 0x08020388u && (s->flags & SF_LOAD)
...
9/47 passed
```

TODO 6 개: `parse_sections`, `parse_memcfg`, `sector_index`, `layout_check`, `image_check`, `shared_consistent`. `parse_hexdump` 과 작은 도구들(`next_line`, `find_sect`, `range_in`, `on_boundary` …)은 주어진다. 통과하는 9 개는 주어진 `parse_hexdump` 검사 2 개와, "정상 레이아웃 → 0", "불일치 → false" 처럼 스텁의 반환값이 우연히 맞는 것들이다.

VM 쪽 (Lima VM `elinux`. Mac 홈은 VM 에서 읽기 전용이라 스크립트가 `/tmp/BM-B06_work` 로 복사해 빌드):

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-B06
limactl shell elinux -- bash -lc "bash $L/run_vm.sh build"     # 두 이미지 빌드 + objdump -h + 헤더 hex + 맵 발췌 + size
limactl shell elinux -- bash -lc "bash $L/run_vm.sh bad"       # 실수 9 종 → 실제 ld 메시지
limactl shell elinux -- bash -lc "bash $L/run_vm.sh capture"   # 검사기 입력 재생성 후 ↓ 로 가져오기
limactl copy 'elinux:/tmp/BM-B06_work/captured/*' "$L/"
```

<!-- code: run_vm.sh -->

<!-- code: layout.ld -->

<!-- code: app.ld -->

<!-- code: boot.ld -->

<!-- code: shared.h -->

<!-- code: app.c -->

<!-- code: boot.c -->

## 4. 힌트

<details>
<summary>힌트 1 — objdump -h 는 "두 줄이 한 섹션"</summary>

`  4 .data   00000004  20000100  08020388  00020100  2**2` 다음 줄이 `CONTENTS, ALLOC, LOAD, DATA`. `sscanf(line, " %d %23s %x %x %x %x 2**%d", ...) == 7` 이면 섹션 줄이고, 그때 다음 줄을 하나 더 읽어 `strstr` 로 플래그를 찾는다. 헤더 줄(`Idx Name ...`)이나 파일 이름 줄은 7 개를 못 읽으니 저절로 걸러진다. `.noinit_shared` 처럼 긴 이름은 열이 밀리지만 `%s`/`%x` 는 공백 개수를 안 따진다.

</details>

<details>
<summary>힌트 2 — 섹터 경계 판정은 "시작이 섹터 시작, 끝이 섹터 시작 또는 플래시 끝"</summary>

섹터 맵이 비균일(16/64/128 KB)이라 `addr % size` 하나로는 안 된다. `sector_index()` 는 base 부터 `sect_size[i]` 를 누적하며 `s <= addr < s + size` 인 i 를 찾는다. 영역 `[o, o+len)` 은 `o` 가 어떤 섹터의 시작이고 `o+len` 이 다음 영역이 시작할 수 있는 점(섹터 시작 또는 0x08100000)이어야 한다 — 주어진 `on_boundary()`.

</details>

<details>
<summary>힌트 3 — 겹침은 반열린 구간 두 개, 끝 주소는 64 비트로</summary>

`[a, a+la)` 와 `[b, b+lb)` 가 겹친다 ⇔ `a < b+lb && b < a+la`. 0x20000100 + 0x1FF00 같은 값은 괜찮지만 `0xFFFFFFFF` 근처 영역(맵의 `*default*` 는 길이 0xffffffff)에서 32 비트 덧셈이 넘친다 → `(uint64_t)` 로 계산. 길이 0 영역은 겹침 계산에서 빼고 `L_EMPTY`.

</details>

<details>
<summary>힌트 4 — image_check 의 순서: 섹션 → .noinit → 벡터 → 헤더 섹션 → magic → 필드</summary>

ALLOC 이고 크기 > 0 인 섹션만 본다 (`.debug_*` 는 ALLOC 이 아니다). LOAD 섹션은 LMA 가 home 안, VMA 는 home 이거나 RAM (`.data` 는 VMA=RAM, LMA=슬롯). LOAD 가 아닌 섹션(NOBITS: `.bss`)은 VMA 가 RAM. 이때 LOAD 섹션의 `lma + size` 최댓값을 기억해 두면 그게 `img_size` 의 정답이다. 헤더는 **섹션 주소가 맞아도 내용이 틀릴 수 있다** — magic 이 틀리면 나머지 필드는 해석하지 말고 `I_HDR_MAGIC` 하나만 보고한다.

</details>

<details>
<summary>힌트 5 — orphan 캡처에서 기대값이 I_NOINIT_MISSING | I_IMG_SIZE 인 이유</summary>

`cap_bad_orphan.txt` 에는 `.noinit` 이 없고 `.noinit_shared` 가 `CONTENTS, ALLOC, LOAD` 로 RAM 0x20000104 (LMA 0x0802038c) 에 있다. LOAD 섹션이니 이미지 끝이 0x080203ac 로 늘었는데, 헤더의 `img_size` 는 링커 스크립트가 `.data` 끝으로 계산한 0x38c 그대로다.

</details>

## 5. 풀이

### Part A — 레이아웃을 "한 곳"에 쓰는 법

**`layout.ld` 의 세 부분.** ① `MEMORY` — 영역 8 개. ② 심볼 — `__slot_a_start = ORIGIN(SLOT_A);` 처럼 C 가 읽을 주소. ③ 레이아웃 `ASSERT` — 영역 순서·겹침·슬롯 크기·128 KB 정렬. `boot.ld` 와 `app.ld` 는 첫 줄이 `INCLUDE layout.ld` 이고 자기 SECTIONS 와 이미지 고유 ASSERT 만 가진다. 영역을 바꾸려면 `layout.ld` 한 곳만 고치면 두 이미지가 같이 바뀐다.

**C 에서 링커 심볼 쓰기.** 링커 심볼은 *값이 주소* 인 이름이고 변수가 아니다. `extern const uint8_t __slot_a_start[];` 로 배열로 선언하면 `__slot_a_start` 자체가 주소다. `extern uint32_t __slot_a_start;` 로 선언하고 `__slot_a_start` 를 읽으면 **그 주소에 있는 플래시 내용**(헤더 magic)을 읽는 고전적 버그가 된다 (7 장).

**링커가 헤더를 채운다.** `app.c` 의 헤더 초기화:

```c
.load_addr   = (uint32_t)(uintptr_t)__slot_a_start,     /* R_ARM_ABS32 재배치 → 0x08020000 */
.vector_addr = (uint32_t)(uintptr_t)__slot_a_vectors,   /* layout.ld: ORIGIN(SLOT_A) + __img_hdr_size */
.img_size    = (uint32_t)(uintptr_t)__app_image_size,   /* app.ld: __app_image_end - ORIGIN(SLOT_A) */
```

`__app_image_size` 는 주소가 아니라 "크기 값을 가진 절대 심볼" 이다. 32 비트 타깃에서 `(uint32_t)&sym` 은 정적 초기화에 쓸 수 있는 주소 상수라 GCC 가 재배치로 낸다. 단 `&a - &b` (두 심볼의 차)는 C 초기화식에서 못 쓴다 ("not computable at load time"). 그래서 차는 링커 스크립트에서 계산한다. `hdr_size` 는 `uint16_t` 라 심볼로 못 채우고(16 비트 절대 재배치를 GCC 가 만들지 않음) 리터럴 0x200 이다. 그 값이 레이아웃과 맞는지는 검사기가 본다.

**실제 빌드 결과** (`run_vm.sh build`, ALLOC 섹션만):

```text
boot.elf:     file format elf32-littlearm
Idx Name          Size      VMA       LMA       File off  Algn
  0 .isr_vector   00000040  08000000  08000000  00010000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  1 .text         000000e4  08000040  08000040  00010040  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  2 .rodata       00000000  08000124  08000124  00020104  2**0
                  CONTENTS, ALLOC, LOAD, DATA
  3 .data         00000004  20000100  08000124  00020100  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  4 .bss          00000000  20000104  08000128  00020104  2**0
                  ALLOC
  5 .noinit       00000020  20000000  20000000  00030000  2**2
                  ALLOC

app.elf:     file format elf32-littlearm
Idx Name          Size      VMA       LMA       File off  Algn
  0 .img_hdr      00000020  08020000  08020000  00010000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  1 .isr_vector   00000040  08020200  08020200  00010200  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  2 .text         000000c8  08020240  08020240  00010240  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  3 .rodata       00000080  08020308  08020308  00010308  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  4 .data         00000004  20000100  08020388  00020100  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  5 .bss          00000004  20000104  0802038c  00020104  2**2
                  ALLOC
  6 .noinit       00000020  20000000  20000000  00030000  2**2
                  ALLOC
Contents of section .img_hdr:
 8020000 494d4731 00020100 00000208 00020208  IMG1............
 8020010 8c030000 02000401 00000000 00000000  ................
```

읽는 법: **VMA** = 실행 중 CPU 가 보는 주소, **LMA** = 플래시에 실려 있는 주소. `.data` 는 VMA 0x20000100(RAM), LMA 0x08020388(플래시) — startup 이 복사한다 (BM-B07). **ALLOC 만** 있고 LOAD 가 없는 `.bss`·`.noinit` 은 파일에 바이트가 없다 (`.bss` 의 LMA 0x0802038c 는 의미 없는 값). `.noinit` 이 두 이미지 모두 0x20000000, 크기 0x20 — 계약 성립.

맵 파일 발췌 (`app.map`, 실제):

```text
Memory Configuration

Name             Origin             Length             Attributes
BOOT             0x08000000         0x00008000         xr
META             0x08008000         0x00008000         r
NVS              0x08010000         0x00010000         r
SLOT_A           0x08020000         0x00060000         xr
SLOT_B           0x08080000         0x00060000         r
SCRATCH          0x080e0000         0x00020000         r
SHARED           0x20000000         0x00000100         rw
RAM              0x20000100         0x0001ff00         xrw
*default*        0x00000000         0xffffffff
...
                0x08020200                        __slot_a_vectors = (ORIGIN (SLOT_A) + __img_hdr_size)
                0x00000001                        ASSERT ((((ORIGIN (SLOT_B) % 0x20000) == 0x0) && ((LENGTH (SLOT_B) % 0x20000) == 0x0)), layout: SLOT_B not on 128 KB sectors)
.img_hdr        0x08020000       0x20
 .img_hdr       0x08020000       0x20 /tmp/ccZZxbEW.o
                0x08020000                g_img_hdr
.noinit         0x20000000       0x20
 .noinit.shared
                0x20000000       0x20 /tmp/ccZZxbEW.o
                0x20000000                g_boot_share
                0x0802038c                        __app_image_end = (LOADADDR (.data) + SIZEOF (.data))
                0x0000038c                        __app_image_size = (__app_image_end - ORIGIN (SLOT_A))
```

맵은 ASSERT 도 **평가 결과(0x00000001 = 참)** 와 함께 남긴다. `run_vm.sh build` 는 이것을 세서 "ASSERT 줄 18 개 (layout 10 + app 8), 값이 0x00000001 이 아닌 것 0 개" 를 찍는다 — CI 로그에 남기기 좋다. `arm-none-eabi-size` 결과는 `boot.elf 292/4/32`, `app.elf 424/4/36` (text/data/bss) — **`size` 는 `.noinit` 32 B 를 bss 에 더해 보여 준다** (ALLOC·non-LOAD 면 다 bss 로 친다). BM-B05 의 예산표를 만들 때 주의할 점이다.

**파서.** `parse_sections` 는 7 필드가 읽히는 줄만 섹션으로 받고 다음 줄을 플래그로 소비한다. 캡처에는 `.debug_*` 까지 app 17 개, boot 16 개 섹션이 있다. ALLOC 이 아니니 검사에서 자연히 빠진다. `parse_memcfg` 는 "Memory Configuration" 줄 이전의 것은 무시하고(테스트: 헤더 없는 텍스트 → 0) `*default*` 를 버린다.

### Part B — 레이아웃 불변식

| 규칙 | 왜 | 비트 |
|---|---|---|
| 모든 영역 쌍 `[o, o+len)` 이 서로소 | 한 바이트에 두 주인 → 업데이트가 다른 영역을 덮음 | `L_OVERLAP` |
| 플래시 영역의 시작·끝 = 섹터 경계 | 지우기 단위가 섹터. 경계가 섹터 중간이면 한 영역을 지울 때 이웃 영역 일부가 같이 지워진다 | `L_ALIGN` |
| 플래시에서 시작한 영역은 플래시 안에서 끝 | 0x08100000 이후는 다른 것(또는 없음) | `L_OUTSIDE` |
| `len(SLOT_A) == len(SLOT_B)` | 스왑/복사 업데이트는 같은 크기 슬롯을 가정 | `L_SLOT_SIZE` |
| 길이 0 없음, 필수 영역 존재 | 오타·누락 방지 | `L_EMPTY`, `L_MISSING` |

`sector_index` 트레이스 (주소 0x0800FF00):

| i | 섹터 시작 s | 크기 | `s ≤ a < s+size`? |
|---|---|---|---|
| 0 | 0x08000000 | 0x4000 | 아니오 |
| 1 | 0x08004000 | 0x4000 | 아니오 |
| 2 | 0x08008000 | 0x4000 | 아니오 |
| 3 | 0x0800C000 | 0x4000 | **예** → 3, start = 0x0800C000 |

주입과 결과 (모두 테스트에서 정확한 마스크로 확인):

| 주입 | 결과 | 설명 |
|---|---|---|
| 실제 캡처: SLOT_B ORIGIN 0x08060000 (ASSERT 뺀 빌드의 맵) | `L_OVERLAP` | 0x08060000–0x08080000 이 A·B 공통. 끝 0x080C0000 은 섹터 10 시작이라 정렬은 OK |
| BOOT LENGTH 24K | `L_ALIGN` | 끝 0x08006000 = 섹터 1 중간. 섹터 1 의 나머지 8 KB 는 BOOT 를 지울 때마다 같이 지워지므로 어떤 영역에도 줄 수 없다 |
| META ORIGIN 0x08009000 | `L_ALIGN \| L_OVERLAP` | 시작이 섹터 2 중간, 끝 0x08011000 이 NVS 침범 |
| SCRATCH LENGTH 256K | `L_OUTSIDE` | 끝 0x08120000 |
| SLOT_B LENGTH 256K | `L_SLOT_SIZE` | 끝 0x080C0000 은 섹터 경계라 정렬은 OK |
| META LENGTH 0 / SLOT_B → SLOT_2 | `L_EMPTY` / `L_MISSING` | |
| SHARED ORIGIN 0x200000F0 | `L_OVERLAP` | RAM 쪽은 섹터 검사 대상이 아니지만 겹침은 본다 |

성질 테스트: 고정 시드 xorshift32 로 플래시 영역 6 개를 무작위로 만들고(2/3 은 섹터 하나에 딱 맞게, 1/3 은 4 KB 단위 임의), 256 칸(1 MB / 4 KB) 배열에 칠해 "두 번 칠해진 칸이 있나" 와 "경계가 섹터 경계인가" 를 직접 계산해 `layout_check` 와 비교 — 400 회 불일치 0.

**ld 는 어디까지 잡아 주나** (`run_vm.sh bad`, 실제 메시지 `ld_errors.txt`):

```text
### (1) SLOT_B 를 0x08060000 으로 옮김 (SLOT_A 와 128 KB 겹침)
ld: layout: SLOT_A overlaps SLOT_B
### (2) 부트로더에 40 KB const 테이블 (BOOT = 32 KB)
ld: x.elf section `.rodata' will not fit in region `BOOT'
ld: boot: image larger than BOOT region
ld: region `BOOT' overflowed by 8488 bytes
### (6) MEMORY 영역 두 개가 겹쳐도 ld 자체는 조용한가? (ASSERT 제거, SLOT_B=0x08060000)
link OK (exit 0) — 경고 없음
### (7) 한 링크 안에서 섹션끼리 겹치면? (.rodata 를 0x08020300 에 강제 — .text 와 겹침)
ld: section .rodata LMA [08020300,0802037f] overlaps section .text LMA [08020240,08020307]
```

(6) 이 핵심이다: **ld 는 MEMORY 영역끼리의 겹침을 검사하지 않는다.** 한 링크 안의 *섹션* LMA 겹침(7)과 영역 넘침(2: 8488 = 40960 + 0x128 − 32768)은 잡지만, 앱 링크는 부트로더의 섹션을 모르므로 이미지 사이 겹침은 원리적으로 볼 수 없다. 레이아웃 ASSERT(1)와 빌드 후 검사기가 그 빈칸을 메운다.

### Part C — 이미지 검사: 실제 app.elf 를 손으로 따라가기

| 섹션 | 플래그 | VMA | LMA | 규칙 | 결과 |
|---|---|---|---|---|---|
| .img_hdr | LOAD | 0x08020000 | 0x08020000 | LMA·VMA ⊂ SLOT_A | OK, load_end = 0x08020020 |
| .isr_vector | LOAD | 0x08020200 | 0x08020200 | 〃 + 위치 = 0x08020000+0x200, 512 정렬 | OK, load_end = 0x08020240 |
| .text | LOAD | 0x08020240 | 0x08020240 | 〃 | OK, 0x08020308 |
| .rodata | LOAD | 0x08020308 | 0x08020308 | 〃 | OK, 0x08020388 |
| .data | LOAD | 0x20000100 | 0x08020388 | LMA ⊂ SLOT_A, VMA ⊂ RAM | OK, **0x0802038c** |
| .bss | ALLOC | 0x20000104 | (무시) | VMA ⊂ RAM | OK |
| .noinit | ALLOC | 0x20000000 | — | NOLOAD, == ORIGIN(SHARED) | OK |
| .debug_* | (ALLOC 아님) | 0 | 0 | 건너뜀 | — |

헤더 hex 해독 (리틀 엔디언, `objdump -s` 는 바이트를 메모리 순서로 찍는다):

| 오프셋 | 바이트 | 필드 | 값 | 기대값 |
|---|---|---|---|---|
| 0 | `49 4d 47 31` | magic | 0x31474D49 "IMG1" | IMG_MAGIC |
| 4 | `00 02` | hdr_size | 0x0200 | 0x200 |
| 6 | `01 00` | hdr_version | 1 | — |
| 8 | `00 00 02 08` | load_addr | 0x08020000 | ORIGIN(SLOT_A) |
| 12 | `00 02 02 08` | vector_addr | 0x08020200 | load_addr + hdr_size |
| 16 | `8c 03 00 00` | img_size | 0x38c = 908 | load_end − 0x08020000 = 0x38c |
| 20 | `02 00 04 01` | fw_version | 0x01040002 = 1.4.2 | — |
| 24 | `00 …` | crc32 | 0 | 패키징 도구가 채움 (BM-A04) |

`img_size` 가 `.rodata` 끝(0x388)이 아니라 `.data` 의 **LMA** 끝(0x38c)까지인 이유: `.data` 초기값 4 바이트가 플래시에 실려 있고, 부트로더가 CRC 를 계산하거나 슬롯을 복사할 때 그 바이트까지 포함해야 한다.

**실제 고장 빌드 두 개:**

```text
# cap_bad_orphan.txt — app.c 의 섹션 이름 오타 ".noinit_shared" (ASSERT 뺀 빌드)
  4 .data         00000004  20000100  08020388  00020100  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  5 .noinit_shared 00000020  20000104  0802038c  00020104  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  6 .bss          00000004  20000124  080203ac  00020124  2**2
                  ALLOC
# cap_bad_hdr.txt — tag.c 의 4 바이트가 .img_hdr 맨 앞에 링크됨
Contents of section .img_hdr:
 8020000 677a0000 494d4731 00020100 00000208  gz..IMG1........
 8020010 00020208 8c030000 02000401 00000000  ................
 8020020 00000000                             ....
```

- **orphan**: 링커 스크립트의 어떤 규칙에도 안 걸린 입력 섹션은 ld 가 "비슷한 섹션 뒤"에 알아서 둔다(orphan placement). 여기서는 `.data` 바로 뒤 RAM 0x20000104 에, **LOAD** 로, LMA 는 플래시 0x0802038c. 결과: 앱은 0x20000104 를 공유 구조체로 알지만 부트로더는 0x20000000 에 쓴다 → 부트 사유가 전달되지 않는다. 더구나 startup 이 복사하는 `_sdata.._edata` 범위 밖이라 초기화도 안 되고, 플래시 32 B 를 차지해 헤더의 `img_size`(0x38c)가 실제 끝(0x3ac)보다 작아진다. 검사기: `I_NOINIT_MISSING | I_IMG_SIZE`, `shared_consistent == false`. ld ASSERT 를 켠 원래 빌드에서는 `app: g_boot_share not at ORIGIN(SHARED)` 로 막힌다. `--orphan-handling=warn` 은 `orphan section '.noinit_shared' ... being placed in section '.noinit_shared'` 를 찍는다.
- **헤더가 맨 앞이 아님**: `.img_hdr` *섹션* 은 0x08020000 에 있으니 섹션 주소 검사는 통과한다. 그런데 첫 워드가 0x00007a67 이다. 부트로더는 슬롯 시작에서 magic 을 찾으므로 이 이미지를 거부한다. 검사기는 내용을 봐서 `I_HDR_MAGIC`. ld 쪽은 `ADDR(.img_hdr)` 가 아니라 **심볼** 로 확인하는 `ASSERT(g_img_hdr == ORIGIN(SLOT_A))` 가 잡는다 (`app: g_img_hdr is not the first word of the slot`). 섹션 주소만 보는 ASSERT 였다면 통과했을 것이다.

메모리 안 주입 9 종의 결과: `.img_hdr` +4 → `I_HDR_SECT`. `load_addr` = 0x08080000(B 용 링크) → `I_HDR_FIELDS`. `hdr_size` 0x100 → `I_HDR_FIELDS`. `.isr_vector` @ +0x100 → `I_VECTOR`. `.noinit` 에 LOAD → `I_NOINIT_LOAD`. `.noinit` @ 0x20000020 → `I_NOINIT_ADDR` + 불일치. `.bss` VMA 를 플래시로 → `I_REGION`. `.text` 크기 0x60000 → `I_REGION | I_IMG_SIZE`. A 용 이미지를 `SLOT_B` 기준으로 검사 → `I_REGION | I_VECTOR | I_HDR_SECT`. boot 의 `.noinit` 크기만 0x24 → `shared_consistent == false`.

정답 실행 (실제 출력 끝부분):

```text
region   origin     end            size  sectors
BOOT     0x08000000 0x08008000     32 KB  0-1
META     0x08008000 0x08010000     32 KB  2-3
NVS      0x08010000 0x08020000     64 KB  4-4
SLOT_A   0x08020000 0x08080000    384 KB  5-7
SLOT_B   0x08080000 0x080e0000    384 KB  8-10
SCRATCH  0x080e0000 0x08100000    128 KB  11-11
SHARED   0x20000000 0x20000100    256 B   -
RAM      0x20000100 0x20020000 130816 B   -
boot  flash  296 B / 32 KB (BOOT)   RAM 4 B   SHARED 32 B
app   flash  908 B / 384 KB (SLOT_A)   RAM 8 B   SHARED 32 B

47/47 passed
```

**설계 선택 이유.** ① 헤더를 슬롯 맨 앞에: 부트로더는 ELF 를 모른다. 고정 오프셋에서 magic·크기·버전·CRC 를 읽어야 "이 슬롯이 부팅 가능한가" 를 판단한다. MCUboot 도 같은 구조다 (헤더 32 B + 예약, `imgtool --header-size 0x200` 이 흔한 값, Zephyr 는 `CONFIG_ROM_START_OFFSET` — 기억 기반, 문서 확인 권장). ② 예약 0x200: 벡터 테이블은 VTOR 정렬을 지켜야 한다. F407 은 시스템 16 + IRQ 82 = 98 벡터 × 4 = 392 B → 2 의 거듭제곱으로 올려 512 B (ARMv7-M VTOR 규칙·IRQ 수 RM0090 확인 권장) → 헤더 예약도 512 의 배수. 0x20 바로 뒤에 두면 정렬 위반. ③ `.noinit` 을 RAM 맨 아래: 스택은 위에서 아래로 자라 `.bss` 쪽으로 넘치므로, 맨 아래 공유 영역은 스택 오버플로에 가장 늦게 맞는다. ④ RAM 의 나머지는 두 이미지가 **같이 쓴다** — 동시에 실행되지 않으니까. 부트로더의 `.data`(0x20000100)와 앱의 `.data`(0x20000100)가 같은 주소인 것은 정상이고, 검사기도 RAM 은 이미지 사이 겹침을 보지 않는다. 계약은 SHARED 하나뿐이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-B06_so && /tmp/BM-B06_so          # 47/47 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/BM-B06_as && /tmp/BM-B06_as   # 47/47, 경고 없음
```

## 7. 자주 하는 실수

**1. 섹션 이름 오타 → orphan.** `section(".noinit_shared")` 처럼 링커 규칙(`*(.noinit.shared)`)과 다른 이름은 ld 가 아무 데나(여기선 `.data` 뒤, LOAD) 둔다. 기본 설정에서는 경고도 없다. 고침: 이름을 매크로 하나(`SHARED_NOINIT`)로만 쓰고, 링커에 `ASSERT(g_boot_share == ORIGIN(SHARED))`, 가능하면 `-Wl,--orphan-handling=warn` (단 커스텀 스크립트에서는 `.glue_7`, `.iplt`, `.debug_*` 같은 스텁/디버그 섹션까지 경고가 나오니 규칙을 다 써 주거나 grep 으로 거른다).

**2. `NOLOAD` 누락.** GCC 14.2 는 이름이 `.noinit` 으로 시작하는 섹션을 알아서 NOBITS 로 낸다 (`readelf -S` 로 확인: `.noinit.shared NOBITS`). 그래서 이 레슨 이름 그대로면 NOLOAD 를 빼도 ALLOC 만 남는다. 하지만 이름이 `.bootshare` 같은 것이면 PROGBITS → LMA = VMA = 0x20000000 인 LOAD 섹션이 되고:

```text
### (8) 공유 변수 섹션을 .bootshare (GCC 가 PROGBITS 로 냄) 로 바꾸고 NOLOAD 를 빼면 → objcopy -O binary 크기
  6 .noinit       00000020  20000000  20000000  00030000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
app.bin = 402522144 bytes
```

0x20000020 − 0x08020000 = 402,522,144 B — `.bin` 이 플래시부터 RAM 까지 0 으로 채운 384 MiB 파일이 된다. 고침: 공유/보존 섹션은 항상 `(NOLOAD)`. 검사기 `I_NOINIT_LOAD`.

**3. `KEEP` 누락 + `--gc-sections`.** 헤더나 벡터 테이블은 코드에서 아무도 참조하지 않는다 (부트로더·CPU 가 주소로 읽을 뿐). `__attribute__((used))` 는 *컴파일러* 가 지우는 것만 막는다. 링커 GC 는 `KEEP()` 으로 막아야 한다. 실제: `ld: removing unused section '.img_hdr'` (`--print-gc-sections`), 이 레슨 ASSERT 가 `app: g_img_hdr is not the first word of the slot` 으로 막는다.

**4. 링커 심볼을 변수처럼 읽기.**

```c
extern uint32_t __slot_a_start;                 /* 틀림: 값 = 0x08020000 번지의 내용 (= magic 0x31474D49) */
uint32_t slot = __slot_a_start;
extern const uint8_t __slot_a_start[];          /* 맞음: 이름 자체가 주소 */
uint32_t slot = (uint32_t)(uintptr_t)__slot_a_start;
```

**5. C 에 주소를 따로 하드코딩.** `#define SLOT_A 0x08020000` 을 부트로더 C 에 두면 `layout.ld` 를 고쳐도 C 는 그대로 → 부트로더가 엉뚱한 곳에서 헤더를 찾는다. 고침: C 는 링커 심볼만 읽는다 (boot.c 의 `__slot_a_start`, `__img_hdr_size`).

**6. 섹션 주소만 ASSERT.** `ASSERT(ADDR(.img_hdr) == ORIGIN(SLOT_A))` 는 "섹션이 거기 있다" 일 뿐 "헤더가 첫 워드다" 가 아니다 (Part C 의 `tag.c` 사례). 심볼(`g_img_hdr`)로 확인하고, 내용(magic)은 빌드 후 검사기가 본다.

**7. 섹터 맵을 균일하다고 가정.** "슬롯 = 0x08010000 부터 448 KB" 처럼 64 KB 섹터 4 를 슬롯에 섞으면 A/B 를 같은 섹터 구성으로 맞출 수 없고, 스왑 단위(가장 큰 섹터)도 어긋난다. 영역 경계는 항상 섹터 표에서 고른다.

## 8. 면접 Q&A

<details>
<summary>Q1. 부트로더와 앱이 플래시에서 겹치면 링커가 에러를 내지 않나요?</summary>

안 낸다. 둘은 **따로 링크** 되는 별개 프로그램이고, 각 링크는 자기 섹션만 본다. ld 가 잡는 것은 한 링크 안의 섹션 LMA 겹침(`section .rodata LMA [08020300,0802037f] overlaps section .text LMA [...]`)과 MEMORY 영역 넘침(`region 'BOOT' overflowed by 8488 bytes`)이다. MEMORY 영역끼리 겹치는 것도 검사하지 않는다 (ASSERT 를 빼고 SLOT_B 를 SLOT_A 에 겹쳐도 exit 0, 실제 확인). 그래서 ① 레이아웃을 공통 파일 하나에 두고 ② 그 안에 `ASSERT(ORIGIN(SLOT_A)+LENGTH(SLOT_A) <= ORIGIN(SLOT_B))` 같은 불변식 ③ CI 에서 두 ELF 를 같이 보는 검사기(LOAD 범위 겹침, 공유 주소 일치)를 둔다. 꼬리: "합친 hex 를 만들 때는?" → 병합 도구(srec_cat, objcopy, 자작 스크립트)마다 겹침 처리가 다르다(에러 / 경고 / 뒤엣것이 덮어씀 — 도구 문서 확인). 그러니 합치기 **전에** 검사기를 돌린다 (추가 연습 2, 실제 병합은 BM-A03).

</details>

<details>
<summary>Q2. 슬롯 경계가 왜 섹터 경계여야 하나요? STM32F4 에서 어떻게 나누겠어요?</summary>

플래시는 **섹터 단위로만 지워진다** (쓰기는 바이트/워드 단위지만 1→0 만 가능, 0→1 은 erase — BM-A02). 슬롯 B 를 받으려고 섹터를 지우는데 그 섹터에 슬롯 A 꼬리나 메타데이터가 같이 있으면 같이 날아간다. F407 은 16/16/16/16/64/128×7 KB (RM0090 확인 권장). 작은 섹터 0–1 에 부트로더(32 KB), 2–3 에 메타데이터 두 벌(16 KB 를 번갈아 지우므로 전원 차단에도 한 벌은 산다, BM-A06), 64 KB 섹터 4 는 설정, 128 KB 섹터 5–7 / 8–10 이 A/B, 11 이 스왑 scratch. 꼬리: "F4 의 128 KB 섹터 erase 시간은?" → 데이터시트에 섹터 크기·병렬도(x8/x32)별 수백 ms~초 단위로 나온다 (확인 권장). 그래서 업데이트 중 워치독·전원 설계가 레이아웃과 같이 간다. 균일 2 KB 페이지인 STM32L4/G4 나 4 KB 인 nRF52 는 정렬이 훨씬 쉽다.

</details>

<details>
<summary>Q3. 부트로더가 앱에 "왜 리셋됐는지" 를 어떻게 전달하나요? .noinit 은 무엇이고 왜 NOLOAD 죠?</summary>

두 이미지가 같은 RAM 주소에 같은 구조체를 두고, 그 섹션을 startup 이 **복사도, 0 으로 지우지도 않게** 한다. 그게 `.noinit` 이다. 링커에서 `(NOLOAD)` → ELF 에 바이트가 없고(ALLOC 만), `.data` 복사 범위(`_sdata.._edata`)와 `.bss` 제로화 범위(`_sbss.._ebss`) 밖에 두므로 소프트 리셋(워치독, `NVIC_SystemReset`)을 넘어 값이 남는다. 전원이 꺼지면 SRAM 은 사라지고, 콜드 부트 직후의 값은 쓰레기다. 그래서 `magic` + `~magic` 같은 유효성 표시가 필요하다 (BM-A09 는 CRC 까지). NOLOAD 가 없고 섹션이 PROGBITS 면 LMA=0x20000000 인 LOAD 섹션이 되어 `objcopy -O binary` 가 384 MiB 파일을 만든다 (실제 402,522,144 B). 꼬리: "RAM 대신 RTC 백업 레지스터는?" → STM32 의 RTC_BKPxR 는 VBAT 로 전원 차단도 넘고 Standby 에서도 남는다 (BM-M11/M12). 단 몇 워드뿐이다.

</details>

<details>
<summary>Q4. 벡터 테이블을 왜 슬롯+0x200 에 두나요? 헤더는 32 바이트인데요.</summary>

앱으로 점프할 때 부트로더가 `SCB->VTOR = 벡터 주소` 를 쓰는데, VTOR 은 테이블 크기를 2 의 거듭제곱으로 올린 값(최소 128 B)으로 정렬돼야 한다 (ARMv7-M ARM VTOR 설명 — 확인 권장). F407 은 98 벡터 → 392 B → 512 B 정렬. 그래서 헤더 *예약* 을 512 B 로 잡고 그 뒤에 테이블을 둔다. 32 B 바로 뒤(0x08020020)는 정렬 위반이다. H7 처럼 IRQ 가 150 개 안팎이면 166 × 4 = 664 → 1024 B 정렬이 필요해 0x200 예약으로는 모자란다 (추가 연습 4). 꼬리: "Cortex-M0 는?" → VTOR 이 없다(M0+ 는 선택). STM32F0 는 앱 벡터를 RAM 0x20000000 에 복사하고 SYSCFG 로 RAM 을 0 번지에 remap 한다.

</details>

<details>
<summary>Q5. 앱은 SLOT_A 주소로 링크했는데, SLOT_B 에 받은 새 이미지는 어떻게 실행하나요?</summary>

Cortex-M 펌웨어는 보통 위치 의존(position-dependent) 코드다. 벡터 테이블·함수 포인터·리터럴 풀에 절대 주소가 박혀 있다. 그래서 선택지는 셋이다. ① **스왑/복사**: B 에 받은 이미지를 검증 후 A 로 옮긴다 (MCUboot swap-using-scratch / swap-using-move / overwrite-only). 레이아웃의 SCRATCH 가 이 용도다. ② **direct-XIP**: 같은 소스를 A 용·B 용으로 두 번 링크하고, 서버가 비어 있는 슬롯용 이미지를 보낸다. 부트로더는 헤더의 `load_addr` 이 놓인 슬롯과 같은 쪽 중 버전이 높은 것을 고른다 (추가 연습 3). ③ PIC(`-fpic -msingle-pic-base` + GOT 재배치)는 가능하지만 툴체인·startup 이 복잡해 드물다. 헤더에 `load_addr` 을 두는 이유가 이것이다: A 용 이미지가 B 에 복사돼 있으면 부트로더가 실행을 거부해야 한다.

</details>

<details>
<summary>Q6. 맵 파일에서 무엇을 보나요?</summary>

① `Memory Configuration` — 실제 적용된 영역 표 (INCLUDE 가 제대로 됐는지). ② `Linker script and memory map` — 출력 섹션별 주소·크기, 그 안에 어떤 오브젝트의 어떤 입력 섹션이 왔는지, 심볼 주소 (`0x20000000 g_boot_share`). ③ 링커 스크립트 대입문의 계산 결과 (`0x0000038c __app_image_size = ...`)와 **ASSERT 평가값**. ④ orphan 섹션이 어디 붙었는지 (`.noinit_shared 0x20000104 0x20 load address 0x0802038c`). ⑤ `Discarded input sections` (gc 로 빠진 것). 크기 예산(BM-B05)은 `size -A` 가 더 빠르고, "이 바이트가 왜 여기 있지?" 는 맵이 답한다. 꼬리: "`size` 의 bss 가 왜 32 B 더 크죠?" → NOLOAD `.noinit` 도 ALLOC·non-LOAD 라 bss 로 합산한다.

</details>

<details>
<summary>Q7. 이미지 헤더의 img_size 는 어떻게 정확히 채우나요? 손으로 적으면?</summary>

손으로 적으면 반드시 어긋난다. 링커 스크립트에서 `__app_image_end = LOADADDR(.data) + SIZEOF(.data);` (마지막 LOAD 바이트 다음), `__app_image_size = __app_image_end - ORIGIN(SLOT_A);` 를 정의하고 C 헤더 초기화에서 `(uint32_t)(uintptr_t)__app_image_size` 로 읽는다. 절대 심볼이라 ABS32 재배치로 값이 들어간다. `.rodata` 끝이 아니라 `.data` 의 **LMA** 끝까지여야 초기값이 CRC 와 복사에 포함된다. 이 정의는 `.data` 가 마지막 LOAD 섹션이라는 가정에 기대므로, 그 가정이 깨지면(orphan LOAD 섹션 — Part C) 검사기가 `I_IMG_SIZE` 로 잡는다. CRC 는 링크 후에만 알 수 있으니 패키징 도구가 바이너리에 써 넣는다 (BM-A04, MCUboot `imgtool sign`).

</details>

<details>
<summary>Q8. 부트로더와 앱이 RAM 을 같이 쓰는데 괜찮나요? 스택은요?</summary>

동시에 돌지 않으므로 RAM 의 대부분은 시간으로 나눠 쓴다. 부트로더의 `.data` 와 앱의 `.data` 가 둘 다 0x20000100 인 것은 정상이다. 점프 직전 부트로더의 RAM 상태는 의미를 잃고, 앱 startup 이 자기 `.data/.bss` 를 다시 만든다. 지켜야 할 것은 ① 공유 영역(SHARED)은 어느 쪽 `.data/.bss`/스택과도 겹치지 않게 별도 MEMORY 영역으로 뺀다 ② 점프할 때 MSP 를 앱 벡터[0] 값으로 다시 설정한다 — 부트로더 스택 위에서 앱 main 이 돌면 안 된다 (BM-A03) ③ 부트로더가 켠 주변장치·인터럽트·SysTick 을 끄고 넘긴다 (BM-A03).

</details>

## 9. 추가 연습

본문 검사기의 파서·섹터 맵을 그대로 복사해 쓰는 연습 4 개. 모두 같은 캡처 파일을 읽는다.

### 연습 1 — erase 계획

`int erase_plan(g, addr, len, inside, &first, &last)`: `[addr, addr+len)` 에 쓰려면 지워야 하는 섹터 범위. `inside` 영역 밖까지 지우게 되면 -2. 슬롯 B 통째, 100 KB 이미지, 128 KB + 1 B 이미지, META 두 번째 사본, 섹터 3·4 에 걸친 쓰기, 40 KB 부트로더 쓰기를 확인하라.

<details>
<summary>답</summary>

`sector_index` 로 첫 바이트·마지막 바이트의 섹터를 구하고, 지울 구간 `[섹터 first 시작, 섹터 last 끝)` 이 `inside` 안인지 `range_in` 으로 본다. 결과: SLOT_B 전체 → 8..10 (3 개), 100 KB → 8 하나, 128 KB + 1 → 8..9 (1 바이트 때문에 128 KB 섹터 하나를 더 지운다), META + 16 KB 의 64 B → 섹터 3, 0x0800FF00 부터 0x200 B → 섹터 3..4 인데 META 기준으로는 -2 (NVS 섹터 4 를 지우게 됨), 40 KB 부트로더 → 섹터 0..2 라서 BOOT 밖 → -2. 교훈: 쓰기 범위가 아니라 **지우기 범위** 로 영역을 검사해야 한다.

</details>

### 연습 2 — 두 이미지를 하나의 플래시 이미지로 합칠 때

`images_overlap(boot, app)` (LOAD 섹션 LMA 끼리 비교)와 `merged_bin_size()` (`objcopy -O binary` 로 합쳤을 때 파일 크기 = 가장 낮은 LMA ~ 가장 높은 LMA 끝).

<details>
<summary>답</summary>

실제 캡처로 boot LOAD = [0x08000000, 0x08000128), app = [0x08020000, 0x0802038c) → 겹침 없음, 합친 .bin = 0x2038c = 131,980 B (대부분 BOOT 끝~SLOT_A 사이 빈틈을 채운 0). 섹션 단위로 비교해야 하는 이유: boot 의 `.text` 를 0x08020040 에 놓으면 app 의 [lo, hi) 범위 안이지만 헤더(0x20 B)와 벡터(0x08020200) 사이 빈틈에 들어가 실제 겹침은 없다. 0x08020210 이면 벡터 테이블과 겹친다. 범위끼리 비교하면 앞의 경우를 헛경보한다. 실제 합치기(hex 병합 + QEMU 부팅)는 BM-A03.

</details>

### 연습 3 — 어느 슬롯으로 부팅할까 (direct-XIP)

`runnable_at(hdr, slot_origin, slot_len)` 과 `pick_boot_slot(a, b, ...)`: 헤더가 그 자리에서 실행 가능한가(magic, hdr_size, `load_addr == 놓인 주소`, `vector_addr`, 크기) + 둘 다 가능하면 버전이 높은 쪽, 하나도 없으면 -1.

<details>
<summary>답</summary>

실제 캡처 헤더(1.4.2, A 용)는 A 에서 실행 가능, B 에서는 불가 (`load_addr` 이 다름 — A 용 이미지를 B 에 그대로 복사하면 절대 주소가 전부 A 를 가리킨다). B 용으로 링크한 1.5.0 을 B 에 두면 1 을 고르고, 1.3.0 이면 0. B 에 A 용 이미지가 있으면 0. 둘 다 지워진 플래시(0xFF...)면 -1 → 복구 모드 (BM-A05). `img_size` 가 슬롯보다 크면 거부. 롤백 방지(보안 카운터)는 BM-A04.

</details>

### 연습 4 — VTOR 정렬과 헤더 예약

`vtor_align(n_irq)`: (16 + n_irq) × 4 B 를 2 의 거듭제곱으로 올리고 최소 128 B. `hdr_reservation_ok(hdr_size, n_irq)`: 헤더 예약이 정렬의 배수인가.

<details>
<summary>답</summary>

0 IRQ → 128, 16 → 128 (32 × 4 = 128), 17 → 256, F407 82 → 512 (0x200 예약 OK), 150 (H7 급) → 1024 (0x200 부족, 0x400 필요), 240 → 1024, 241 → 2048. IRQ 수는 기억 기반 (F405/407 82 개 — RM0090 의 벡터 표, H7 은 RM0433 확인 권장). 그래서 헤더 예약을 정할 때 "칩 패밀리 중 IRQ 가 가장 많은 것" 을 기준으로 잡거나, 부트로더가 헤더의 `vector_addr` 을 쓰고 링커 ASSERT 로 정렬을 강제한다 (`app.ld` 의 `ADDR(.isr_vector) % 512 == 0`).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cd <ROOT>/lessons/BM/BM-B06
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-B06_pr && /tmp/BM-B06_pr   # 29/29 passed
```

## 10. 핵심 정리

- 플래시 맵의 모든 경계 = **섹터 경계** (지우기 단위). F407: 16×4 / 64 / 128×7 KB → BOOT 0–1, META 2–3, NVS 4, SLOT_A 5–7, SLOT_B 8–10, SCRATCH 11.
- 레이아웃은 `layout.ld` 한 곳. `boot.ld`/`app.ld` 는 `INCLUDE` 로 파생하고, C 는 링커 심볼(`extern const uint8_t sym[]`)로 주소를 읽는다. 주소 하드코딩 금지.
- **ld 는 따로 링크된 이미지 사이 겹침도, MEMORY 영역끼리의 겹침도 보지 않는다.** → 레이아웃 `ASSERT` + 빌드 후 검사기(objdump -h / 맵 파싱).
- 헤더 = 슬롯 offset 0 (심볼로 ASSERT, 내용은 magic 검사), 벡터 = +0x200 (VTOR 정렬, F407 98 벡터 → 512 B). `img_size` 는 링커가 `.data` LMA 끝까지 계산.
- 공유 부트 사유 = 두 이미지에서 같은 주소의 `.noinit (NOLOAD)` + magic/~magic. NOLOAD 를 빼면 LOAD 가 되어 `.bin` 이 384 MiB (실측 402,522,144 B).
- 실수 Top 4: 섹션 이름 오타(orphan, 경고 없음), `KEEP` 누락 + `--gc-sections`, 링커 심볼을 변수처럼 읽기, 섹션 주소만 ASSERT.
- VMA = 실행 주소, LMA = 실린 주소. ALLOC 만 = 파일에 없음(.bss/.noinit). `size` 는 `.noinit` 도 bss 로 센다.

## 참고자료

- GNU ld manual — MEMORY, SECTIONS, `INCLUDE`, `ASSERT`, `NOLOAD`, `KEEP`, `REGION_ALIAS`, Orphan Sections, `--orphan-handling`, `--gc-sections` — <https://sourceware.org/binutils/docs/ld/>
- ST RM0090 (STM32F405/407/415/417/42x/43x Reference Manual) — Embedded Flash memory 장의 섹터 구성 표, 벡터 표 (섹터 크기·IRQ 수는 이 레슨에서 기억 기반 대표값) — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armcortexm4-based-32bit-mcus-stmicroelectronics.pdf>
- ARMv7-M Architecture Reference Manual (DDI 0403) — B3.2.5 VTOR (TBLOFF 정렬 규칙) — <https://developer.arm.com/documentation/ddi0403/latest/>
- MCUboot 설계 문서 — 이미지 헤더, 슬롯·scratch, swap/overwrite/direct-XIP 모드 — <https://docs.mcuboot.com/design.html>
- GCC 문서 — `section`, `used`, `noinit` 변수 속성 — <https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html>
- 관련 레슨: BM-B05 (맵 파일·예산표), BM-B07 (.data 복사·.bss 제로화), BM-B08 (C 벡터 테이블로 첫 부팅), BM-A02 (플래시 erase/program 모델), BM-A03 (부트로더 → 앱 점프, 이미지 합치기), BM-A04 (헤더 CRC/SHA-256), BM-A05 (시리얼 부트로더), BM-A06 (전원 차단 내성 스왑), BM-A09 (.noinit 크래시 덤프), TOOL-M01 (링커 스크립트 기초), TOOL-A07 (고급 링커 스크립트), ARM-M08 (asm 벡터 테이블 + Thumb 비트)
