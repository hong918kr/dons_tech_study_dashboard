---
id: COMM-A06
time: 120분
updated: 2026-11-11
---
# COMM-A06 SPI NOR 플래시 드라이버: JEDEC ID, WEL, 페이지 프로그램

## 학습 목표

- SPI NOR 의 명령 프레임(0x9F RDID, 0x06 WREN, 0x04 WRDI, 0x05 RDSR, 0x01 WRSR, 0x02 PP, 0x20 SE, 0xD8 BE, 0x03 READ)을 **칩 쪽에서** 해석하는 mock 모델을 짠다. 쓰기류 명령은 CS 상승 에지에서 실행되고, WEL 이 없으면 무시되고, 실행이 끝나면 WEL 이 저절로 0 이 되고, WIP=1 동안은 RDSR 외 전부 무시된다는 규칙을 코드로 옮긴다.
- **페이지 경계 랩(wrap)** 을 재현하고, 페이지 단위로 자르는 `nor_write` 로 고친다. 단순 드라이버가 "에러 없이 성공한 뒤 데이터가 틀린" 이유를 설명할 수 있다.
- erase(4 KB/64 KB 선택, 정렬 검사), 블록 보호(BP/SRP/WP#) 해제, WIP 폴링 + 타임아웃, probe 를 짜고 장애(busy 멈춤, WREN 무시, WP# 하드웨어 보호, MCU 리셋 중 erase, 전원 차단)를 주입해 각 에러 코드를 확인한다.
- NOR 의미(프로그램 = AND, erase = 0xFF)를 가진 기준 RAM 모델과 4000 개의 랜덤 연산으로 대조한다.
- NOR 위에 작은 append-only 로그(레코드 헤더 + CRC-16, 부팅 스캔, blank check, 섹터 회전)를 올리고, 전원 차단 300 회 캠페인으로 "ack 된 레코드는 사라지지 않고, 깨진 레코드는 절대 돌려주지 않는다"를 검증한다.

## 1. 문제

외부 SPI NOR 플래시는 부트 이미지, 설정, 로그, 펌웨어 업데이트 슬롯을 담는 **가장 흔한 외부 저장장치**다. 면접에서는 "NOR 페이지 경계 넘겨 쓰면?", "WEL 은 왜 매번 세우나?", "erase 중에 MCU 가 리셋되면?", "1 바이트만 바꾸려면?" 이 단골이고, bring-up 에서는 "ID 가 FF FF FF", "쓰기가 성공했는데 읽으면 다르다", "처음 받은 보드는 쓰기가 안 된다(출하 보호)" 가 첫날 터진다. SSD 펌웨어에서 NAND 프로그램/erase 를 다뤄 봤다면 개념은 같다 — 단위가 작고(256 B 페이지, 4 KB 섹터), 컨트롤러가 없고, 명령을 직접 보낸다는 점이 다르다.

이 레슨은 COMM-B05 의 "명령 바이트 + CS 프레임" 칩 모델을 플래시로 키우고, COMM-M13 의 비동기 큐 아래에서 실제로 도는 "플래시 트랜잭션"이 무엇인지 채운다. 칩 규칙은 Winbond W25Q 계열을 본떴지만 **숫자와 세부 동작은 이 mock 의 정의**다 (실제 부품은 데이터시트 확인).

| 이 레슨의 칩 정의 | 내용 |
|---|---|
| 크기·구조 | 1 MB, 페이지 256 B, 섹터 4 KB, 블록 64 KB. 지워진 상태 = 0xFF |
| JEDEC ID (0x9F) | `EF 40 14` = 제조사 Winbond 류, 타입 0x40, 용량 코드 0x14 → 2^20 B (W25Q80 류 값 — 대표값) |
| 상태 레지스터 (0x05) | bit0 WIP(BUSY), bit1 WEL, bit4..2 BP2..BP0, bit7 SRP. BP 표(TB=0, 위쪽부터): 0 = 없음, n = 위쪽 64 KB·2^(n−1), 1 MB 이상이면 전부 |
| 쓰기류 명령 | WRSR(≥2 B), PP(≥5 B), SE/BE(정확히 4 B). **CS↑ 에서 실행**. WEL=0 → 무시, 길이 틀림 → 무시, 보호 영역 → 무시 (모두 WEL 유지) |
| PP | 1→0 만 가능 (메모리 &= 데이터). 데이터 바이트 k 는 열 `(addr + k) & 0xFF` 로 → **페이지 끝을 넘으면 같은 페이지 처음으로 랩**, 256 B 넘게 보내면 같은 열은 나중 값 |
| READ (0x03) | 연속 읽기, 페이지 랩 없음, 칩 끝(0xFFFFF) 다음은 0 |
| 바쁜 동안 | WIP=1 이면 RDSR 외 모든 프레임 무시 (MISO = 풀업 0xFF). 끝나면 WIP=0, **WEL=0 자동** |
| 시간 (1 tick = 1 µs) | SPI 1 B = 1 tick. t_PP 700, t_SE 45 000, t_BE 150 000, t_W 10 000 (W25Q 류 typical 대표값). 드라이버 타임아웃 = max 대표값 3 ms / 400 ms / 2 s / 15 ms |
| 장애 주입 | 칩 없음(MISO 풀업 또는 GND), WIP 멈춤, WREN 무시, WP# low, 지정 tick 에 전원 차단(진행 중 동작은 경과 비율만큼 비트 일부만 반영) |
| 에러 코드 | `OK 0, E_ARG −1, E_ALIGN −2, E_TIMEOUT −3, E_NODEV −4, E_ID −5, E_WEL −6, E_PROTECTED −7, E_BUSY −8` |

아래는 `nor_write` 가 한 페이지를 쓸 때 버스에 실제로 나가는 프레임 순서다 (모델의 tick 기준).

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-size="13" font-weight="bold">한 페이지 쓰기 = WREN → RDSR(WEL 확인) → PP → RDSR 폴링 (CS↑ 가 실행 시점)</text>
  <text x="8" y="54" fill="currentColor">CS#</text>
  <path d="M60 40 H70 V60 H110 V40 H130 V60 H200 V40 H220 V60 H420 V40 H450 V60 H520 V40 H690 V60 H740 V40 H750" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="8" y="104" fill="currentColor">MOSI</text>
  <rect x="76" y="90" width="28" height="20" class="f-acc"/><text x="90" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">06</text>
  <rect x="134" y="90" width="28" height="20" class="f-acc"/><text x="148" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">05</text>
  <rect x="166" y="90" width="28" height="20" class="f-mut"/><text x="180" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">FF</text>
  <rect x="224" y="90" width="26" height="20" class="f-acc"/><text x="237" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">02</text>
  <rect x="252" y="90" width="26" height="20" class="f-mut"/><text x="265" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">00</text>
  <rect x="280" y="90" width="26" height="20" class="f-mut"/><text x="293" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">01</text>
  <rect x="308" y="90" width="26" height="20" class="f-mut"/><text x="321" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">F0</text>
  <rect x="336" y="90" width="26" height="20" class="f-ok"/><text x="349" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">D0</text>
  <rect x="364" y="90" width="26" height="20" class="f-ok"/><text x="377" y="104" fill="currentColor" text-anchor="middle">…</text>
  <rect x="392" y="90" width="26" height="20" class="f-ok"/><text x="405" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">D15</text>
  <rect x="454" y="90" width="28" height="20" class="f-acc"/><text x="468" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">05</text>
  <rect x="486" y="90" width="28" height="20" class="f-mut"/><text x="500" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">FF</text>
  <text x="604" y="104" fill="currentColor" text-anchor="middle">… 50 µs 마다 RDSR …</text>
  <rect x="692" y="90" width="22" height="20" class="f-acc"/><text x="703" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">05</text>
  <rect x="716" y="90" width="22" height="20" class="f-mut"/><text x="727" y="104" fill="currentColor" text-anchor="middle" font-family="monospace">FF</text>
  <text x="8" y="144" fill="currentColor">MISO</text>
  <rect x="166" y="130" width="28" height="20" class="f-ok"/><text x="180" y="144" fill="currentColor" text-anchor="middle" font-family="monospace">02</text>
  <rect x="486" y="130" width="28" height="20" class="f-warn"/><text x="500" y="144" fill="currentColor" text-anchor="middle" font-family="monospace">03</text>
  <rect x="716" y="130" width="22" height="20" class="f-ok"/><text x="727" y="144" fill="currentColor" text-anchor="middle" font-family="monospace">00</text>
  <text x="250" y="144" fill="currentColor">주소 0x0001F0, 16 B (MSB first)</text>
  <text x="8" y="194" fill="currentColor">WEL</text>
  <path d="M60 200 H110 V180 H680 V200 H750" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="8" y="234" fill="currentColor">WIP</text>
  <path d="M60 240 H420 V220 H680 V240 H750" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M110 62 V178 M420 62 V218 M680 160 V238" stroke="currentColor" stroke-dasharray="4 3" fill="none"/>
  <text x="114" y="174" fill="currentColor">WREN 의 CS↑ → WEL=1</text>
  <text x="426" y="214" fill="currentColor">PP 의 CS↑ → 프로그램 시작 (t_PP)</text>
  <text x="676" y="174" fill="currentColor" text-anchor="end">끝: WIP=0, WEL 자동 0</text>
  <path d="M70 262 H740" stroke="currentColor" opacity=".6"/>
  <text x="70" y="278" fill="currentColor">0</text>
  <text x="420" y="278" fill="currentColor" text-anchor="middle">≈ 25 tick</text>
  <text x="680" y="278" fill="currentColor" text-anchor="middle">≈ 25 + 700</text>
  <text x="10" y="302" fill="currentColor">칸 색: 명령(강조) · 주소/더미(흐림) · 데이터/정상 응답(초록) · WIP=1 응답 0x03(경고).</text>
  <text x="10" y="320" fill="currentColor">RDSR 은 명령 바이트 뒤 매 바이트마다 최신 SR 을 내보낸다 (연습 4: CS 를 잡은 채 계속 읽기).</text>
</svg>
```

### Part A — 칩 모델과 기본 드라이버

- `chip_on_byte(c, idx, mosi)` — 프레임의 idx 번째 바이트 처리 (0 = 명령). busy 중 RDSR 이 아니면 프레임 전체 무시. RDSR/RDID/READ 는 MISO 로 값을 내보내고, PP 는 페이지 래치 `pbuf[(addr + k) & 0xFF]` 에 쌓는다.
- `chip_on_cs_rise(c)` — CS↑ 에서 WREN/WRDI/WRSR/PP/SE/BE 실행 (길이·WEL·보호 검사 → `chip_start_op`).
- `nor_probe` (ID 읽기, 빈 ID 면 기다렸다 재시도, 제조사 표, 용량 코드 0x10..0x18), `nor_wait_ready(timeout, poll)`, `nor_write_enable`(WREN + WEL 확인), `nor_write`(페이지 분할).
- 테스트: probe, CS↑ 전에는 WEL=0, WREN 없는 PP 무시, busy 중 RDID/READ/SE 무시, WEL 자동 해제, AND 의미(0xF0 위에 0x3C → 0x30), 칩의 페이지 랩(32 B·300 B @0x1F0), **단순 드라이버의 랩 버그 재현** vs `nor_write` (PP 3 번: 16 + 256 + 28), 경계(정확히 한 페이지, 마지막 바이트, 범위 밖), READ 의 칩 끝 랩.

### Part B — erase, 보호, 장애 주입 (면접관이 찌르는 곳)

- `nor_erase(addr, len)` — 4 KB 정렬 아니면 `E_ALIGN`, 64 KB 정렬이고 64 KB 이상 남으면 BE, 아니면 SE. `nor_unprotect()` — WRSR 로 BP=0, 다시 읽어 확인, 실패면 WRDI 후 `E_PROTECTED`.
- 주입하는 장애와 기대 결과:

| 장애 | 주입 | 기대 결과 |
|---|---|---|
| 지우지 않고 덮어쓰기 | 0x55 위에 0xAA | 읽으면 0x00 (에러 없음). erase 뒤에야 0xAA |
| 잘못된 길이의 SE | 주소 뒤 1 B 더 | 실행 안 됨 |
| 블록 보호 BP=1 | WRSR 0x04 | 위 64 KB 쓰기/지우기 `E_PROTECTED`, PP 명령 0 개. 드라이버를 우회한 PP 는 칩이 무시 |
| 출하 보호 BP=7 | SR = 0x1C | 모든 쓰기 `E_PROTECTED` → `nor_unprotect` → 정상 |
| WP# 하드웨어 보호 | SRP=1, WP# low | `nor_unprotect` = `E_PROTECTED`, BP 그대로, **WEL 남지 않음** |
| WIP 멈춤 | `stuck_busy` | `E_TIMEOUT` (TO_PP 경과 후), 두 번째 페이지는 안 보냄, 다음 연산 `E_BUSY`, 전원 재인가로 복구 |
| WREN 이 안 먹음 | `drop_wren` | `E_WEL`, PP/SE 0 개 |
| 칩 없음 / MISO GND | `present=false`, `miso_idle=0` | `E_NODEV` |
| 모르는 제조사 / 32 MB | ID 42 40 14 / EF 40 19 | `E_ID` |
| MCU 리셋 중 64 KB erase | BE 시작 후 드라이버만 초기화 | 그냥 읽은 ID = FF FF FF, `nor_probe` 는 기다렸다가 성공, erase 도 끝까지 |
| 전원 차단 (PP 중간 / SE 1/4) | `cut_at` | 읽으면 일부 비트만 바뀜 (1 은 새로 안 생김) |

- 마지막으로 **랜덤 4000 연산** (쓰기 45%·erase 15%·읽기 40%) 을 NOR 의미의 기준 RAM 모델과 대조하고, 끝에 1 MB 전체 비교.

### Part C — 펌웨어 현실: NOR 위의 로그 저장소

- 영역 = 섹터 4 개 (0x80000~). 섹터 헤더 8 B `'L' 'G' seq(LE32) crc16`, 레코드 `[0xA5][len][payload][crc16 LE]` (len 1..128, 섹터를 넘지 않음).
- `log_mount` — 헤더 중 seq 최대 = 현재 섹터, 없으면 포맷. 현재 섹터를 훑어 끝(첫 0xFF 매직)을 찾고, **끝부터 섹터 끝까지 0xFF 인지 확인(blank check)**, 아니면 `need_rotate`.
- `log_append` — 안 들어가거나 `need_rotate` 면 다음 섹터 erase + 헤더(seq+1) 로 회전. 쓰기 실패면 그 섹터를 더 쓰지 않음.
- 테스트: CRC-16/CCITT-FALSE 체크값 0x29B1, 출하 보호 → unprotect → 포맷, 재마운트, 회전(400 개 → 살아 있는 211 개), 찢어진 레코드, 재마운트 없이 계속 쓰기, 회전 erase 중 차단, **랜덤 전원 차단 300 회** 캠페인.

## 2. Problem (English)

> **SPI NOR flash driver + a mock chip to test it against.**
>
> You have a W25Q80-class SPI NOR (1 MiB, 256-byte pages, 4 KiB sectors, 64 KiB blocks) behind a byte-level SPI API (`spi_select`, `spi_byte`, `spi_deselect`). Write commands (WREN 0x06, WRSR 0x01, PP 0x02, SE 0x20, BE 0xD8) execute when CS goes high, require WEL = 1, and clear WEL when the internal operation completes. While WIP = 1 the chip ignores everything except RDSR (0x05). Page program only clears bits, and the address wraps inside the 256-byte page.
>
> ```c
> int nor_probe(NorDev *d);                                         /* JEDEC 0x9F -> size, vendor */
> int nor_wait_ready(NorDev *d, uint32_t timeout, uint32_t poll);   /* E_TIMEOUT */
> int nor_write_enable(NorDev *d);                                  /* WREN + verify WEL, else E_WEL */
> int nor_write(NorDev *d, uint32_t addr, const uint8_t *buf, size_t len);   /* split at pages */
> int nor_erase(NorDev *d, uint32_t addr, uint32_t len);            /* 4 KiB aligned, E_ALIGN */
> int nor_unprotect(NorDev *d);                                     /* clear BP bits, E_PROTECTED */
> int log_mount(LogStore *ls, NorDev *d, uint32_t base, uint32_t nsec);
> int log_append(LogStore *ls, const uint8_t *data, uint32_t len);
> ```
>
> Requirements: no dynamic allocation; every wait has a timeout taken from the datasheet *maximum*; a write that spans pages issues one PP per page; erase picks 64 KiB blocks when aligned; protected ranges are refused before any command is sent; the log never returns a corrupted record and never loses an acknowledged record that is still inside its retention window, under power loss at any instant.
>
> Example: `nor_write(d, 0x1F0, buf, 300)` must issue three PP commands — 16 bytes at 0x1F0, 256 at 0x200, 28 at 0x300. A single PP of 300 bytes would program page 0x100 instead (16 bytes at 0x1F0..0x1FF, then wrap to 0x100) and leave 0x200..0x31B erased — with no error.
>
> Follow-ups: What happens if the MCU resets during an erase? Why must WEL be re-armed for every page? How do you change one byte? What survives a power cut, and how does the boot scan know where to append?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열 군데: Part A `chip_on_byte` `chip_on_cs_rise` `nor_probe` `nor_wait_ready` `nor_write_enable` `nor_write`, Part B `nor_erase` `nor_unprotect`, Part C `log_mount` `log_append`. 칩의 시간 진행(`chip_advance` — 완료·전원 차단 반영), CS 프레임 시작/끝(`chip_cs`), 바이트 교환(`chip_xfer`), BP 표(`bp_protected_from`), SPI 도우미(`nor_cmd`, `nor_rdsr`, `nor_read_id`, `nor_read`), 로그의 헤더·스캔·순회(`log_read_header`, `log_start_sector`, `log_scan_sector`, `log_iterate`), CRC, 하네스는 주어진다. 스텁은 칩을 "아무것도 안 하는 칩"으로, 드라이버를 에러 코드로 만들어 모든 루프가 바로 끝난다. 처음 실행 (실제 출력, 중간 생략):

```text
-- Part A: 칩 모델, probe, WEL, 페이지 프로그램
FAIL starter.c:448  fresh() == NOR_OK
FAIL starter.c:449  g_dev.size == (1u << 20) && g_dev.vendor && strcmp(g_dev.vendor, "Winbond") == 0
FAIL starter.c:450  g_dev.id[0] == 0xEF && g_dev.id[1] == 0x40 && g_dev.id[2] == 0x14
FAIL starter.c:453  nor_rdsr(&g_dev) == 0x00
FAIL starter.c:457  (g_chip.sr & SR_WEL) != 0
…
   [log] power-cut campaign: 0 cuts, 300 appends tried, torn records seen 0, rotate-after-mount 0, lost acked 0, bad/out-of-order 300
23/169 passed
```

## 4. 힌트

<details>
<summary>힌트 1 — 칩은 "프레임 단위"로 생각한다</summary>

CS↓ 에서 프레임 상태(바이트 수, 명령, 주소, 0xFF 로 채운 페이지 래치)를 리셋하는 건 주어진 `chip_cs` 가 한다. 첫 바이트에서 명령을 기억하고 **busy 면 ignore 플래그**를 세운다 (RDSR 만 예외). 나머지 바이트는 명령별로: 1..3 번째 = 주소, 4 번째부터 데이터. 실행은 바이트를 받을 때가 아니라 CS↑ 에서.

</details>

<details>
<summary>힌트 2 — 페이지 래치를 0xFF 로 채우는 이유</summary>

프로그램은 `mem &= data` 다. 래치의 안 쓴 열이 0xFF 면 그 열은 그대로다. 그래서 "몇 번째 열이 채워졌나" 마스크가 필요 없고, 256 B 넘게 보내도 `pbuf[(addr + k) & 0xFF] = mosi` 한 줄로 "같은 열은 나중 값이 이긴다"가 된다.

</details>

<details>
<summary>힌트 3 — 페이지 분할 한 줄</summary>

`n = 256 − (addr & 0xFF); if (n > len) n = len;` — 첫 조각만 짧을 수 있고 그 뒤는 256 씩, 마지막이 짧다. 조각마다 WREN → (WEL 확인) → PP → wait. WEL 은 프로그램이 끝나면 칩이 지우므로 **매 조각마다** 다시 세운다.

</details>

<details>
<summary>힌트 4 — 폴링 루프의 종료 조건 순서</summary>

`RDSR → WIP=0 이면 OK → 경과 ≥ timeout 이면 E_TIMEOUT → sim_delay(poll)`. 경과는 `(uint32_t)(g_now − t0)` 로 (랩어라운드 안전, COMM-M13 힌트 5). 칩이 없으면 RDSR = 0xFF → WIP=1 로 보이므로 이 루프가 "칩 없음"도 타임아웃으로 끝낸다.

</details>

<details>
<summary>힌트 5 — 보호는 "보내기 전에" 거절</summary>

시작할 때 RDSR 한 번: WIP 면 `E_BUSY`, `addr + len > bp_protected_from(sr, size)` 면 `E_PROTECTED`. 칩도 보호 영역 명령을 무시하지만 그건 **에러 없이 조용히** 무시하는 것이라 드라이버가 먼저 알려야 한다.

</details>

<details>
<summary>힌트 6 — mount 의 세 단계</summary>

(1) 헤더를 전부 읽어 seq 최대 = cur (없으면 섹터 0 을 seq 1 로 포맷). (2) `log_scan_sector` 로 끝 오프셋 → `wp`. (3) `wp` 부터 섹터 끝까지 64 B 씩 읽어 0xFF 가 아니면 `need_rotate = true`. (3)을 빼먹으면 반쯤 써진 바이트 위에 다음 레코드를 AND 로 덮어쓴다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "NOR 는 프로그램이 1→0, erase 가 →1 (섹터 단위). 그래서 쓰기 전 erase, 바꾸기는 read-modify-erase-write."
2. "쓰기류 명령은 WREN 으로 WEL 을 세워야 받아들여지고, CS↑ 에서 실행, 끝나면 WEL 이 자동으로 꺼진다. 실행 중엔 WIP=1 이고 RDSR 만 받는다 → 매 연산 뒤 WIP 폴링, 타임아웃은 datasheet max."
3. "PP 는 페이지 안에서 주소가 랩한다 → 드라이버가 페이지 경계에서 자른다."
4. "보호 비트(BP/SRP/WP#), probe 는 JEDEC ID, 정밀하게는 SFDP."
5. 꼬리질문 대비: MCU 리셋 중 erase, 전원 차단과 로그 설계, 폴링 간격, QSPI/XIP.

### Part A — 칩 모델: 바이트 해석과 CS↑ 실행

칩 상태는 세 개로 요약된다. 화살표의 조건이 `chip_on_cs_rise` 의 if 문 순서 그대로다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-size="13" font-weight="bold">mock NOR 의 쓰기 상태 기계 (판단은 모두 CS↑ 에서)</text>
  <rect x="30" y="110" width="180" height="70" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="120" y="138" fill="currentColor" text-anchor="middle" font-weight="bold">대기</text>
  <text x="120" y="158" fill="currentColor" text-anchor="middle">WEL=0, WIP=0</text>
  <rect x="290" y="110" width="180" height="70" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="380" y="138" fill="currentColor" text-anchor="middle" font-weight="bold">쓰기 가능</text>
  <text x="380" y="158" fill="currentColor" text-anchor="middle">WEL=1, WIP=0</text>
  <rect x="550" y="110" width="180" height="70" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="640" y="138" fill="currentColor" text-anchor="middle" font-weight="bold">BUSY</text>
  <text x="640" y="158" fill="currentColor" text-anchor="middle">WIP=1 (WEL=1)</text>
  <path d="M210 128 H284" stroke="currentColor" fill="none" stroke-width="1.5"/><path d="M284 128 l-8 -4 v8 z" fill="currentColor"/>
  <text x="247" y="122" fill="currentColor" text-anchor="middle">WREN</text>
  <path d="M290 164 H216" stroke="currentColor" fill="none" stroke-width="1.5"/><path d="M216 164 l8 -4 v8 z" fill="currentColor"/>
  <text x="253" y="178" fill="currentColor" text-anchor="middle">WRDI</text>
  <path d="M470 145 H544" stroke="currentColor" fill="none" stroke-width="1.5"/><path d="M544 145 l-8 -4 v8 z" fill="currentColor"/>
  <text x="507" y="102" fill="currentColor" text-anchor="middle">PP / SE / BE / WRSR</text>
  <text x="507" y="88" fill="currentColor" text-anchor="middle">길이 OK · 보호 밖</text>
  <path d="M640 180 V230 H120 V186" stroke="currentColor" fill="none" stroke-width="1.5"/><path d="M120 186 l-4 8 h8 z" fill="currentColor"/>
  <text x="380" y="248" fill="currentColor" text-anchor="middle">t_PP / t_SE / t_BE / t_W 경과 → 메모리 반영, WIP=0, WEL=0 (자동)</text>
  <text x="30" y="72" fill="currentColor">PP/SE/BE/WRSR → 무시 (n_ign_wel)</text>
  <text x="290" y="60" fill="currentColor">보호 영역 · 길이 틀림 · WP#&amp;SRP → 무시, WEL 유지</text>
  <text x="560" y="206" fill="currentColor">RDSR 외 전부 무시 (MISO=FF)</text>
  <text x="10" y="276" fill="currentColor">전원 차단: BUSY 에서 끊기면 경과 비율 f 만큼 비트 일부만 반영 → 재인가 시 WIP=0, WEL=0, BP/SRP 는 유지(비휘발).</text>
  <text x="10" y="294" fill="currentColor">MCU 리셋은 이 그림의 상태를 바꾸지 않는다 — 칩은 자기 일을 계속한다 (Part B 의 probe 테스트).</text>
</svg>
```

PP 프레임 하나를 바이트 단위로 따라가 보자 (`raw_cmd_addr(CMD_PP, 0x1F0, d300, 32)`, d300[i] = 7i+3):

| idx | MOSI | 칩이 하는 일 | 상태 |
|---|---|---|---|
| 0 | 0x02 | cmd = PP, WIP=0 이라 ignore = false | |
| 1..3 | 00 01 F0 | addr = 0x0001F0 (3 번째에서 `& 0xFFFFF`) | |
| 4..19 | d300[0..15] | 열 0xF0..0xFF 에 래치 | |
| 20 | d300[16] | 열 `(0x1F0 + 16) & 0xFF = 0x00` ← **랩** | |
| 21..35 | d300[17..31] | 열 0x01..0x0F | |
| CS↑ | — | nbytes 36 ≥ 5, WEL=1, 페이지 0x100 보호 아님 → `chip_start_op(PP, 0x100, 700)` | WIP=1 |
| +700 tick | — | `mem[0x100 + c] &= pbuf[c]` 전체 256 열, WIP=0 WEL=0 | |

결과: 0x1F0..0x1FF = d300[0..15], **0x100..0x10F = d300[16..31]**, 0x200 은 0xFF 그대로 — 테스트가 이것을 확인한다. 300 B 를 한 번에 보내면 바이트 i 는 열 (0xF0 + i) & 0xFF 로 가므로 열 0x00..0x1B 는 i = 272..299 가, 나머지 열은 i = 16..271 이 마지막으로 쓴다 (`mem[0x100] == d300[272]`, `mem[0x1F0] == d300[256]`).

왜 칩이 이렇게 동작하나: 페이지 버퍼의 주소 카운터가 **하위 8 비트만** 증가하기 때문이다 (상위 비트 = 페이지 주소는 명령 시점에 고정). 프로그램 회로가 한 번에 한 페이지(한 워드라인의 일부)만 다루므로 다음 페이지로 넘어갈 방법이 없다. 데이터시트는 이것을 "256 B 넘게 보내면 페이지 처음으로 돌아가 덮어쓴다"고 쓴다 (W25Q 계열 기준 — 확인 권장).

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-size="13" font-weight="bold">32 B 를 0x1F0 에 쓰기 — 단순 드라이버(PP 1 번) vs 페이지 분할(PP 2 번)</text>
  <text x="10" y="44" fill="currentColor" font-weight="bold">단순: PP 02 00 01 F0 + 32 B</text>
  <path d="M712 66 C712 40 200 40 200 66" stroke="currentColor" fill="none" stroke-dasharray="5 3"/><path d="M200 66 l-4 -8 h8 z" fill="currentColor"/>
  <text x="456" y="48" fill="currentColor" text-anchor="middle">열 0xFF 다음 = 열 0x00 (같은 페이지)</text>
  <text x="10" y="86" fill="currentColor">page 0x100</text>
  <rect x="200" y="70" width="512" height="26" fill="none" stroke="currentColor"/>
  <rect x="200" y="70" width="32" height="26" class="f-warn"/>
  <rect x="680" y="70" width="32" height="26" class="f-acc"/>
  <text x="238" y="88" fill="currentColor">← D16..D31 이 여기로 (0x100..0x10F 를 망가뜨림)</text>
  <text x="676" y="88" fill="currentColor" text-anchor="end">D0..D15</text>
  <text x="10" y="126" fill="currentColor">page 0x200</text>
  <rect x="200" y="110" width="512" height="26" fill="none" stroke="currentColor"/>
  <text x="456" y="128" fill="currentColor" text-anchor="middle">0xFF 그대로 — 쓰려던 D16..D31 은 없다. 드라이버는 OK 를 돌려줌</text>
  <text x="200" y="152" fill="currentColor">열 0x00</text><text x="680" y="152" fill="currentColor" text-anchor="middle">0xF0</text><text x="712" y="152" fill="currentColor" text-anchor="end">0xFF</text>
  <text x="10" y="190" fill="currentColor" font-weight="bold">분할: n = 256 − (addr &amp; 0xFF) = 16 → PP #1 (16 B), 나머지 → PP #2 (16 B @0x200)</text>
  <text x="10" y="222" fill="currentColor">page 0x100</text>
  <rect x="200" y="206" width="512" height="26" fill="none" stroke="currentColor"/>
  <rect x="680" y="206" width="32" height="26" class="f-acc"/>
  <text x="676" y="224" fill="currentColor" text-anchor="end">D0..D15 (PP #1)</text>
  <text x="10" y="262" fill="currentColor">page 0x200</text>
  <rect x="200" y="246" width="512" height="26" fill="none" stroke="currentColor"/>
  <rect x="200" y="246" width="32" height="26" class="f-ok"/>
  <text x="238" y="264" fill="currentColor">D16..D31 (PP #2) — WREN 을 다시 세우고, 앞 PP 가 끝난 뒤(WIP=0)</text>
  <text x="10" y="300" fill="currentColor">300 B @0x1F0 실측: 단순 드라이버 = PP 1 번, 282/300 B 틀림 · nor_write = PP 3 번 (16+256+28), 0 B 틀림</text>
  <text x="10" y="320" fill="currentColor">(1 px = 0.5 열. 32 B 그림이 테스트 "칩 모델의 페이지 랩"이고, 300 B 는 "단순 드라이버 vs 분할" 테스트다.)</text>
</svg>
```

`nor_write` 의 조각 표 (300 B @0x1F0):

| 조각 | addr | `256 − (addr & 0xFF)` | n | 남은 len |
|---|---|---|---|---|
| 1 | 0x1F0 | 16 | 16 | 284 |
| 2 | 0x200 | 256 | 256 | 28 |
| 3 | 0x300 | 256 | 28 | 0 |

조각마다 WREN → RDSR(WEL=1, WIP=0 확인) → PP → `nor_wait_ready(TO_PP, POLL_PP)`. WEL 확인은 RDSR 한 번(2 B)이지만 "WREN 이 왜 안 먹었나"(칩 없음, 아직 busy, WP 회로, 신호 무결성)를 PP 를 보내기 **전에** 잡는다. Linux spi-nor 는 이 확인을 생략하고 쓰기 뒤 상태만 본다 (기억 — 확인 권장). 확인을 빼면 `drop_wren` 테스트가 FAIL 2 개(PP 가 나가고 조용히 무시됨).

`nor_probe` 는 ID 가 FF FF FF 또는 00 00 00 이면 바로 "없음"이라 하지 않는다. 그 두 값은 (1) 칩이 없거나 MISO 가 풀업/GND 에 붙음, (2) **MCU 만 리셋되고 플래시는 erase 중** (busy 라 RDID 를 무시 → MISO 풀업) 둘 다 만든다. 그래서 `nor_wait_ready(TO_BE)` 로 기다렸다가 한 번 더 읽는다. 칩이 없으면 RDSR 도 0xFF(WIP=1)라 타임아웃 → `E_NODEV`. 용량 코드는 Winbond/Macronix 관례인 2^code 로 해석하고 0x10..0x18 (64 KB..16 MB) 만 받는다 — 16 MB 를 넘으면 3 바이트 주소로 안 되므로 `E_ID` (연습 5). 이 관례가 모든 제조사에 맞지는 않는다 (SST26 류는 다름 — 기억, 확인 권장) → 실무는 부품 표 또는 SFDP.

### Part B — erase, 보호, 장애

erase 는 **정렬된 가장 큰 단위**를 쓴다. 블록 erase 한 번이 섹터 16 번보다 훨씬 빠르기 때문이다. demo_nor.c 의 실측 (모델 시간, t_SE 45 ms·t_BE 150 ms 대표값):

| range                  | sector-only        | driver (mixed)            | speedup |
|---|---|---|---|
| 4 KB @0x00000          |    1 SE |      45 ms |  0 BE +   1 SE |     45 ms |  1.0x |
| 64 KB @0x10000         |   16 SE |     721 ms |  1 BE +   0 SE |    150 ms |  4.8x |
| 64 KB @0x11000 (unal.) |   16 SE |     721 ms |  0 BE +  16 SE |    721 ms |  1.0x |
| 256 KB @0x40000        |   64 SE |    2886 ms |  4 BE +   0 SE |    600 ms |  4.8x |
| 1 MB @0                |  256 SE |   11545 ms | 16 BE +   0 SE |   2401 ms |  4.8x |

같은 64 KB 라도 4 KB 어긋나면 블록을 못 쓴다 → 파티션 경계를 64 KB 에 맞추는 이유 (BM-B06 의 플래시 레이아웃). 32 KB 블록(0x52)을 섞는 최소 명령 계획은 연습 2.

보호는 두 층이다. **BP 비트**(소프트웨어 보호: 위쪽 영역을 잠금, WRSR 로 바꿈)와 **SRP + WP# 핀**(하드웨어 보호: SRP=1 이고 WP# 가 low 면 WRSR 자체가 무시). `nor_unprotect` 는 WRSR 뒤 반드시 다시 읽는다 — WP# 때문에 무시됐다면 에러 없이 끝나기 때문이다. 그리고 무시된 WRSR 은 WEL 을 남겨 두므로(이 모델의 정의) **WRDI 로 지운다**. WEL=1 이 남아 있으면 그 뒤 노이즈 한 번에 PP/SE 가 받아들여질 수 있다. 출하 시 BP 가 켜져 오는 부품이 있다 (제조사·주문 옵션에 따라 — 확인 권장) → 부트 코드의 "첫 쓰기가 안 된다" 의 흔한 원인.

장애 결과 (solution.c 실제 출력):

```text
   stuck busy: nor_write -> E_TIMEOUT after 3283 ticks (TO_PP 3000), 61 RDSR polls
   power cut at 1/2 t_PP: 216/256 bytes partially programmed, 13 done, 27 untouched
   random ops: 1759 writes, 611 erases, 1630 reads, rc errors 0, read mismatches 0, full compare OK
```

- busy 멈춤: 3283 = PP 프레임 약 265 tick + 타임아웃 3000 + 마지막 폴링 오차. 두 번째 페이지는 보내지 않는다 (PP 실행 1 번). 칩은 계속 busy 이므로 다음 `nor_erase` 는 `E_BUSY`. 복구는 칩 전원 재인가(로드 스위치)나 소프트웨어 리셋 0x66/0x99 (부품 지원 시 — 확인 권장), 안 되면 FAULT 보고.
- 전원 차단: 0xAA 위에 0x00 을 쓰다 절반에서 끊으면, 바이트마다 지워야 할 4 비트가 각각 확률 0.5 로 반영된다 → 0x00 이 된 바이트 13, 하나도 안 바뀐 27, 나머지 216 은 중간값. 검사하는 성질: **결과 ⊆ old** (1 이 새로 생기지 않음), 일부는 목표와 다름. 실제 NOR 에서는 중간 상태 셀이 읽을 때마다 다른 값(약하게 프로그램된 비트)을 낼 수도 있다 — 모델은 그것까지 흉내 내지 않는다.
- 랜덤 대조: 기준 모델은 `ref[a+i] &= data[i]`(쓰기), `memset(0xFF)`(erase) 두 줄이다. 쓰기 데이터의 1/4 을 0xFF 로 섞어 겹쳐 쓰기의 AND 가 실제로 의미 있게 했다.

### Part C — 로그 저장소: 무엇을 믿고 무엇을 버리나

```svg
<svg viewBox="0 0 760 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-size="13" font-weight="bold">로그 영역 4 섹터 (round-robin) — 마운트가 보는 것</text>
  <rect x="20" y="40" width="170" height="160" fill="none" stroke="currentColor"/>
  <rect x="200" y="40" width="170" height="160" fill="none" stroke="currentColor"/>
  <rect x="380" y="40" width="170" height="160" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <rect x="560" y="40" width="170" height="160" fill="none" stroke="currentColor"/>
  <rect x="20" y="40" width="170" height="16" class="f-mut"/><text x="105" y="52" fill="currentColor" text-anchor="middle">LG seq 5</text>
  <rect x="200" y="40" width="170" height="16" class="f-mut"/><text x="285" y="52" fill="currentColor" text-anchor="middle">LG seq 6</text>
  <rect x="380" y="40" width="170" height="16" class="f-acc"/><text x="465" y="52" fill="currentColor" text-anchor="middle">LG seq 7 = 최대 → cur</text>
  <rect x="560" y="40" width="170" height="16" class="f-mut"/><text x="645" y="52" fill="currentColor" text-anchor="middle">LG seq 4 (가장 오래됨)</text>
  <rect x="20" y="60" width="170" height="130" class="f-ok"/><text x="105" y="130" fill="currentColor" text-anchor="middle">레코드 가득</text>
  <rect x="200" y="60" width="170" height="130" class="f-ok"/><text x="285" y="130" fill="currentColor" text-anchor="middle">레코드 가득</text>
  <rect x="380" y="60" width="170" height="60" class="f-ok"/><text x="465" y="94" fill="currentColor" text-anchor="middle">CRC OK 레코드들</text>
  <rect x="380" y="122" width="170" height="14" class="f-warn"/><text x="465" y="133" fill="currentColor" text-anchor="middle">찢어진 레코드 (CRC 불량)</text>
  <text x="465" y="160" fill="currentColor" text-anchor="middle">0xFF … (blank check)</text>
  <path d="M380 140 H560" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="556" y="152" fill="currentColor" text-anchor="end">wp</text>
  <rect x="560" y="60" width="170" height="130" class="f-ok"/><text x="645" y="120" fill="currentColor" text-anchor="middle">다음 회전 때</text><text x="645" y="136" fill="currentColor" text-anchor="middle">erase 될 섹터</text>
  <text x="105" y="216" fill="currentColor" text-anchor="middle">S0</text><text x="285" y="216" fill="currentColor" text-anchor="middle">S1</text>
  <text x="465" y="216" fill="currentColor" text-anchor="middle">S2 (cur)</text><text x="645" y="216" fill="currentColor" text-anchor="middle">S3 → next = (cur+1) % 4</text>
  <text x="20" y="248" fill="currentColor" font-weight="bold">섹터 헤더 8 B</text>
  <rect x="140" y="236" width="30" height="18" class="f-acc"/><text x="155" y="249" fill="currentColor" text-anchor="middle" font-family="monospace">'L'</text>
  <rect x="172" y="236" width="30" height="18" class="f-acc"/><text x="187" y="249" fill="currentColor" text-anchor="middle" font-family="monospace">'G'</text>
  <rect x="204" y="236" width="120" height="18" class="f-mut"/><text x="264" y="249" fill="currentColor" text-anchor="middle">seq (LE32)</text>
  <rect x="326" y="236" width="70" height="18" class="f-ok"/><text x="361" y="249" fill="currentColor" text-anchor="middle">crc16 LE</text>
  <text x="20" y="286" fill="currentColor" font-weight="bold">레코드 len+4 B</text>
  <rect x="140" y="274" width="30" height="18" class="f-acc"/><text x="155" y="287" fill="currentColor" text-anchor="middle" font-family="monospace">A5</text>
  <rect x="172" y="274" width="30" height="18" class="f-mut"/><text x="187" y="287" fill="currentColor" text-anchor="middle" font-family="monospace">len</text>
  <rect x="204" y="274" width="200" height="18" class="f-mut"/><text x="304" y="287" fill="currentColor" text-anchor="middle">payload (1..128 B)</text>
  <rect x="406" y="274" width="70" height="18" class="f-ok"/><text x="441" y="287" fill="currentColor" text-anchor="middle">crc16 LE</text>
  <text x="490" y="287" fill="currentColor">CRC 범위 = A5, len, payload</text>
  <text x="20" y="318" fill="currentColor">끝 = 첫 매직 0xFF. 매직이 0xFF·0xA5 가 아니거나 len 이 이상하면 그 뒤는 믿지 않는다 (end = 섹터 끝).</text>
  <text x="20" y="334" fill="currentColor">살아 있는 섹터 = seq 가 (cur_seq − 4, cur_seq] 인 것. 읽기 순서 = seq 오름차순.</text>
</svg>
```

설계 판단:

- **레코드가 섹터를 넘지 않는다** → 섹터 하나를 지울 때 레코드가 반쪽으로 남지 않는다. 대가는 섹터 끝 낭비 (≤ 최대 레코드 − 1).
- **세대 번호(seq)** 로 현재 섹터를 찾는다. 헤더는 erase 직후 쓰므로, erase 나 헤더 쓰기 중 끊기면 그 섹터는 헤더 CRC 가 틀려 "없는 섹터"가 되고, cur 은 이전 섹터로 남는다 → 다음 append 가 그 섹터를 다시 지운다.
- **CRC 로 찢어진 레코드를 버린다**. len 이 그럴듯하면 건너뛰고 다음을 본다 (n_corrupt++), 헤더 자체가 망가졌으면 그 섹터의 나머지를 포기한다.
- **blank check** — 매직이 0xFF 라도 뒤 바이트가 반쯤 프로그램됐을 수 있다 (PP 의 비트들이 서로 독립적으로 반영). 그 위에 다음 레코드를 쓰면 AND 로 섞여 **ack 된 새 레코드가 깨진다**. 끝부터 섹터 끝까지 0xFF 가 아니면 `need_rotate`.
- **쓰기 실패 = 그 섹터 포기** — 재마운트 없이 계속 쓰는 경우(플래시 전원 레일만 복구, 또는 타임아웃 뒤 재시도)에도 같은 규칙. 이걸 빼면 "재마운트 없이 계속" 테스트가 FAIL 3 개.

solution.c 의 로그 출력 (실제):

```text
   [nor] JEDEC EF 40 14 -> Winbond, 1024 KiB (page 256 B, sector 4 KiB)
   [nor] SR=0x1C (BP=7, protected from 0x000000) -> unprotect -> SR=0x00
   [log] mount @0x080000 x4: formatted=1 seq=1 wp=0x080008
   [log] remount: seq=1 records=3 corrupt=0 wp=0x080081
   [log] 400 appends: rotations=6 seq=7 alive records=211 (189..399)
   [log] torn record: remount records=10 corrupt=1 need_rotate=0 wp=0x081000
   [log] power-cut campaign: 300 cuts, 13289 appends tried, torn records seen 200, rotate-after-mount 36, lost acked 0, bad/out-of-order 0
```

- 400 개 회전: 레코드 64 B, 섹터당 ⌊(4096 − 8)/64⌋ = 63 개 → 400 = 63·6 + 22 → 7 세대, 회전 6 번. 살아 있는 4 세대 = 63·3 + 22 = 211 개, 첫 번째 = 63·3 = 189.
- 찢어진 레코드: 이번 차단은 매직 바이트까지 망가뜨려 "헤더를 믿을 수 없음" 경로 → end = 섹터 끝(wp = 0x081000), 그래서 `need_rotate` 없이도 다음 append 가 회전한다. 레코드 10 개는 그대로, 11 번째(차단된 것)는 없음.
- 캠페인의 불변식 (매 차단 후 재마운트하고 검사): 돌려준 레코드는 전부 내용 검증 통과 + 시도 순서대로, **최근 3 세대(seq ≥ cur − 2) 에 ack 된 레코드는 하나도 빠지지 않음**, 재부팅 직후 append 성공. 가장 오래된 세대는 회전 erase 가 끊길 때 일부 사라질 수 있으므로 보장 구간에서 뺀다.

demo_nor.c 의 2000 회 캠페인 (실제 출력 — 열 이름만 한국어로 풀어 씀; 원문 `torn record seen (CRC)` · `next append rotates`):

| cut during | cuts | 재마운트 때 현재 섹터에 CRC 불량 레코드 있음 | 다음 append 가 새 섹터로 |
|---|---|---|---|
| idle / SPI frame | 73 | 22 | 3 |
| page program | 610 | 510 | 448 |
| sector erase | 1317 | 655 | 739 |

```text
cuts 2000, appends tried 61384, lost acked (guaranteed window) 0, corrupt/out-of-order returned 0,
unacked (in-flight) record found complete after reboot 32, avg alive records 244.0
```

읽는 법: 시간의 대부분은 erase 다 (레코드 ~60 개마다 45 ms) → 차단의 2/3 가 erase 중. CRC 불량 칸은 지난 차단의 흔적이 회전될 때까지 남아 있어 누적으로 센다. erase 중 차단인데 "새 섹터로"가 많은 이유: 재마운트 후 첫 append 가 회전 erase 이고, 짧은 창(3 ms) 차단이 그 erase 에 자주 걸려서다. 마지막 줄의 32: **ack 를 받기 전에 끊겼는데 레코드는 완전히 써진** 경우 (프로그램은 끝났고 드라이버가 WIP 폴링 중이었음). 즉 이 로그는 "최대 한 번"이 아니라 **적어도 한 번**이다 — 앱이 재시도하면 중복이 생길 수 있으니 레코드에 시퀀스 번호를 넣어 걸러야 한다 (COMM-M14 의 중복 처리와 같은 문제).

blank check 를 빼면 (scratch 의 mutant, 실제 출력):

```text
   [log] power-cut campaign: 300 cuts, 13030 appends tried, torn records seen 228, rotate-after-mount 0, lost acked 484, bad/out-of-order 0
```

ack 된 레코드 484 개가 사라졌다. 반쯤 써진 칸 위에 새 레코드를 쓰면 CRC 가 깨져 버려지기 때문이다 (돌려준 레코드는 여전히 0 개 오류 — CRC 가 지키는 것은 무결성이지 지속성이 아니다).

### 폴링 간격과 버스 점유

64 KB 쓰기, 폴링 간격만 바꿈 (demo_nor.c 실제 출력):

| poll us | start | PPs | polls | time | throughput | bus busy | data |
|---|---|---|---|---|---|---|---|
|      5 | 0x20000 |  256 |  26112 |  247.0 ms |  259.1 KB/s |  48.2 % | OK |
|     50 | 0x20000 |  256 |   4096 |  254.2 ms |  251.8 KB/s |  29.5 % | OK |
|    200 | 0x20000 |  256 |   1536 |  274.7 ms |  233.0 KB/s |  25.4 % | OK |
|    700 | 0x20000 |  256 |    768 |  247.6 ms |  258.5 KB/s |  27.6 % | OK |
|   1000 | 0x20000 |  256 |    768 |  324.4 ms |  197.3 KB/s |  21.1 % | OK |
|     50 | 0x20010 |  257 |   4112 |  254.9 ms |  251.0 KB/s |  29.4 % | OK |

- 페이지당 시간 ≈ 프레임 ~265 µs + t_PP 700 µs + **늦게 알아챈 시간** (평균 폴링 간격의 절반 정도). 5 µs 폴링은 지연을 거의 없애지만 RDSR 을 26 112 번 보내 버스를 절반 가까이 잡는다 (같은 버스의 IMU 가 그만큼 기다린다 — COMM-M13).
- 700 µs 가 좋아 보이는 것은 **우연**이다: 폴링 주기(702 µs)가 t_PP 에 딱 맞았다. 실제 t_PP 는 부품·온도·마모에 따라 흔들리므로 (typical 과 max 가 4 배 차이) 이 정렬은 믿을 수 없다. 1000 µs 는 매 페이지 ~300 µs 를 버린다.
- 실무 패턴: 첫 확인을 typical 근처까지 미루고(sleep), 그 뒤 짧은 간격으로 폴링. RTOS 라면 폴링 사이에 태스크 양보. 시작 주소가 16 B 어긋나면 PP 가 257 번 (첫 조각 240 B).

### SFDP, QSPI, 4 바이트 주소 (한 문단)

실제 드라이버(Linux `spi-nor`, Zephyr `spi_nor`, U-Boot `sf`)는 JEDEC ID 로 부품 표를 찾고, 가능하면 **SFDP**(Serial Flash Discoverable Parameters, JESD216, 명령 0x5A + 3 바이트 주소 + 더미 1 B) 표에서 용량, 지원하는 erase 크기와 opcode, quad 읽기 명령, 4 바이트 주소 방식 등을 읽는다. 16 MB 를 넘는 칩은 3 바이트 주소로 다 못 가리키므로 4 바이트 주소 전용 opcode(0x13 READ4B, 0x12 PP4B, 0x21 SE4B, 0xDC BE4B)나 "4 바이트 모드 진입"(0xB7)을 쓴다 — 모드 방식은 MCU 리셋 후 칩이 4 바이트 모드에 남아 부트 ROM 이 못 읽는 함정이 있다. QSPI(quad I/O, 0x6B/0xEB 읽기, SR2 의 QE 비트)는 데이터선 4 개로 대역폭을 늘리고, 많은 MCU 가 메모리 매핑(XIP) 모드로 플래시를 그냥 주소로 읽게 해 준다. 하지만 **프로그램/erase 동작과 명령 순서(WREN, WIP 폴링, 페이지 랩)는 그대로다** (opcode·비트 위치는 기억에 의존 — 부품 데이터시트/JESD216 확인 권장).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution                  # 169/169 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 169/169, 경고 없음
cc -std=c11 -Wall -Wextra -O2 demo_nor.c -o demo_nor && ./demo_nor                         # 위의 표 3 개
```

하네스가 실제로 버그를 잡는지 확인하려고 solution.c 의 사본(레슨 폴더 밖)에 버그 10 개를 하나씩 넣어 돌렸다. 전부 exit 1: 페이지 분할 없음 156/169, busy 중 명령 수락 159/169, blank check 없음 168/169 (캠페인에서 lost 484), 폴링 없이 OK 74/169, WEL 확인 없음 167/169, WRDI 없음 168/169, 쓰기 실패 뒤 dirty 표시 없음 166/169, probe 가 안 기다림 167/169, 칩의 PP 랩 없음 148/169, 스캔이 CRC 무시 168/169.

## 7. 자주 하는 실수

**1. 페이지 경계에서 안 자른다.** 버퍼 전체를 PP 한 번에 보내면 칩은 에러 없이 받아들이고, 페이지 시작 쪽을 덮어쓴다. 증상: 작은 쓰기(설정 몇 바이트)는 늘 되는데 로그가 커지면 "가끔" 앞부분이 깨진다 — 페이지 경계를 넘을 때만 터지기 때문. 고친 코드:

```c
size_t n = NOR_PAGE - (addr & (NOR_PAGE - 1));
if (n > len) n = len;
```

**2. WREN 을 처음 한 번만 보낸다.** WEL 은 PP/SE/WRSR 이 끝나면 칩이 지운다. 두 번째 페이지부터는 WEL=0 이라 조용히 무시 → 첫 페이지만 써진다. 조각마다 `nor_write_enable()`.

**3. WIP 를 안 기다리고 다음 명령.** busy 중의 READ 는 0xFF 를, RDID 는 FF FF FF 를, WREN·PP 는 무시를 돌려준다. 폴링을 빼면 74/169 — 이 모델에서 가장 많이 깨지는 버그다. "쓰고 바로 읽어 검증"하는 코드가 0xFF 를 읽고 검증 실패를 내는 것도 같은 원인.

**4. 타임아웃을 typical 로 잡거나 아예 없다.** t_SE typical 45 ms 로 잡으면 마모된 칩·저온에서 멀쩡한 erase 를 실패로 처리하고, 타임아웃이 없으면 칩이 빠진 보드에서 부팅이 멈춘다 (칩 없음 = RDSR 0xFF = 영원히 busy). datasheet **max** 를 쓰고, 타임아웃 뒤 상태(E_BUSY 로 막기, 전원 재인가)를 정한다.

**5. "ID 가 FF FF FF = 칩 없음" 으로 단정.** MCU 워치독 리셋은 플래시를 리셋하지 않는다. erase 중이었다면 칩은 수백 ms 동안 RDID 를 무시한다. probe 가 기다리지 않으면 "가끔 부팅 때 플래시를 못 찾는" 보드가 된다 (mutant: FAIL 2 개).

**6. 보호 해제 실패 뒤 WEL 을 남긴다.** WP# 로 WRSR 이 무시되면 WEL=1 이 남는다. 다음 노이즈·잘못된 프레임이 쓰기로 받아들여질 수 있다 → 실패 경로에서 WRDI. 그리고 **WRSR 뒤 다시 읽어 확인** 하지 않으면 실패 자체를 모른다.

**7. 0xFF 가 아닌 곳 위에 쓴다.** NOR 쓰기는 AND 다. 지우지 않은 곳(이전 데이터, 반쯤 써진 레코드)에 쓰면 섞인 값이 되고 에러는 없다. 쓰기 전 erase 상태를 보장하는 것은 드라이버 위 계층(파일시스템·로그)의 일이다 — blank check 를 빼면 ack 된 레코드 484 개 손실.

**8. 쓰기 실패한 자리를 재사용한다.** 실패한 PP 는 일부 비트를 이미 바꿨을 수 있다. `wp` 를 그대로 두고 재시도하면 새 레코드가 AND 로 깨진다 → 실패하면 그 섹터를 포기(`need_rotate`).

## 8. 면접 Q&A

<details>
<summary>Q1. SPI NOR 에 페이지 경계를 넘겨 쓰면 어떻게 되나요?</summary>

PP 는 한 페이지(대개 256 B) 안에서만 동작한다. 칩의 페이지 버퍼 주소 카운터가 하위 8 비트만 증가하므로, 열 0xFF 다음 바이트는 **같은 페이지의 열 0x00** 으로 간다. 256 B 를 넘게 보내면 앞에 보낸 바이트를 덮어쓴다 (나중 값이 이김). 칩은 이것을 에러로 보지 않으므로 드라이버는 OK 를 받고, 다음 페이지는 0xFF 그대로, 현재 페이지 앞쪽은 원치 않는 데이터와 AND 된다. 해결: `n = 256 − (addr % 256)` 으로 첫 조각을 자르고 이후 256 씩. 조각마다 WREN, PP, WIP 대기. 300 B @0x1F0 이면 16 + 256 + 28 로 PP 3 번. 꼬리: "읽기는?" — READ(0x03) 는 페이지 랩이 없고 칩 끝까지 연속, 끝에서 0 으로 돈다 (그래서 드라이버는 읽기도 범위를 검사한다).

</details>

<details>
<summary>Q2. WEL 은 왜 있고, 왜 매 연산마다 다시 세워야 하나요?</summary>

쓰기 보호 장치다. 전원 상승·하강 중의 글리치나 잘못된 프레임 하나가 PP/SE 로 해석돼 데이터가 망가지는 것을 막으려고, 쓰기류 명령은 **바로 앞에 WREN 으로 래치를 세운 경우에만** 받아들인다. 그리고 그 래치는 연산이 끝나면 칩이 스스로 지운다 (WRDI, 전원 인가 때도 0). 그래서 한 번의 WREN 은 정확히 한 번의 쓰기만 허락한다. 드라이버는 WREN 뒤 RDSR 로 WEL=1 을 확인하면 "칩이 살아 있고 명령이 들어갔다"를 PP 전에 알 수 있다. 꼬리: "WREN 과 PP 를 같은 CS 프레임에?" — 안 된다. WREN 은 CS↑ 에서 실행되는 독립 명령이다 (CS 를 내린 채 06 02 … 를 보내면 칩은 0x06 만 명령으로 보고 나머지를 버린다).

</details>

<details>
<summary>Q3. WIP 폴링 간격은 어떻게 정하나요? 인터럽트로 받을 수는 없나요?</summary>

SPI NOR 는 완료 인터럽트 핀이 없는 게 보통이다 (일부 부품의 RY/BY# 핀이나 SO 핀을 busy 표시로 쓰는 모드 — 부품별, 확인 권장). 그래서 폴링이고, 간격은 **지연과 버스 점유의 맞바꿈**이다. demo: 64 KB 쓰기에서 5 µs 폴링은 247 ms·버스 48 % 점유, 50 µs 는 254 ms·29.5 %, 1000 µs 는 324 ms. 700 µs 가 247.6 ms 로 좋아 보인 것은 폴링 주기가 t_PP 와 우연히 맞았기 때문이라 믿으면 안 된다. 실무: typical 근처까지 잠들고(RTOS 지연, 다른 태스크에 CPU 양보) 그 뒤 짧게 폴링, 타임아웃은 max. erase 처럼 긴 연산은 수 ms 간격. CS 를 잡고 RDSR 를 계속 읽는 방식(연습 4)은 1 µs 해상도지만 그동안 버스의 다른 디바이스가 못 쓴다. QSPI 컨트롤러에는 "자동 폴링 모드"(상태 레지스터를 하드웨어가 반복 읽고 일치하면 인터럽트 — STM32 QUADSPI/OCTOSPI 의 auto-polling 등, 확인 권장)가 있어 CPU 를 쓰지 않는다.

</details>

<details>
<summary>Q4. erase 도중 MCU 가 워치독으로 리셋되면 플래시는 어떻게 되나요? 부트 코드에서 무엇을 해야 하나요?</summary>

플래시는 MCU 리셋을 모른다 (RESET# 핀이 있고 연결돼 있지 않은 한). 내부 erase 는 끝까지 진행되고 그동안 RDSR 외 명령은 무시된다. 부트 코드가 바로 RDID 를 읽으면 FF FF FF → "칩 없음"으로 오판하거나, 부트로더가 이미지를 읽다가 0xFF 를 읽는다. 그래서 부트 경로에서: (1) RDSR 로 WIP 를 보고 max 시간까지 기다린다 (테스트: 20 ms 지난 64 KB erase → probe 가 기다렸다 성공, erase 도 완료), (2) 필요하면 소프트웨어 리셋 0x66 → 0x99 (busy 중에는 무시되거나 erase 를 끊을 수 있음 — 부품별 확인), (3) 이전 부팅이 4 바이트 모드·QPI 모드로 바꿔 놓았을 수 있으니 알려진 상태로 되돌린다. 그리고 끊긴 erase 를 가정한 데이터 구조(Part C)가 필요하다 — MCU 리셋은 erase 를 끊지 않지만 **전원 차단은 끊는다**.

</details>

<details>
<summary>Q5. NOR 에서 1 바이트만 바꾸려면? 비용과 위험은?</summary>

바꾸려는 비트가 1→0 뿐이면 그냥 PP (AND 로 충분 — 연습 1 에서 600 번 중 367 번이 이 경우였다). 0→1 이 하나라도 있으면 그 섹터 4 KB 를 RAM 으로 읽고, 고치고, erase(45 ms 대표값), 다시 프로그램(16 페이지 × ~1 ms). 비용: RAM 4 KB, 수십 ms, erase 횟수(NOR 는 섹터당 약 10 만 회 대표값 — 확인 권장). 위험: erase 와 재프로그램 사이에 전원이 끊기면 **섹터 전체**가 사라진다. 그래서 실제 시스템은 제자리 갱신을 피한다 — 로그 구조(Part C), A/B 두 벌 + 시퀀스 번호, 또는 LittleFS 같은 copy-on-write 파일시스템 (STOR-A05 다음 단계). 내 SSD 경험으로 말하면 FTL 의 out-of-place write 와 같은 이유다.

</details>

<details>
<summary>Q6. 전원 차단에 강한 로그를 NOR 에 어떻게 설계하나요? 부팅 때 어디에 이어 쓸지 어떻게 아나요?</summary>

(1) 레코드 = 매직 + 길이 + 데이터 + CRC, 섹터를 넘지 않게. (2) 섹터 헤더에 세대 번호 — seq 최대가 현재 섹터. (3) 부팅 스캔: 현재 섹터를 앞에서부터 읽어 첫 0xFF 매직이 끝. CRC 가 틀린 레코드는 버리고(길이가 그럴듯하면 건너뜀), 헤더가 이상하면 그 섹터의 나머지를 포기. (4) **blank check** — 끝 뒤가 전부 0xFF 인지 확인. 반쯤 프로그램된 바이트가 있으면 다음 섹터로. 이것을 빼면 캠페인에서 ack 된 레코드 484 개 손실. (5) 쓰기 실패 = 그 섹터 포기. 보장: CRC 가 맞는 것만 돌려주고, 최근 세대에 ack 된 것은 잃지 않는다 (2000 회 차단에서 손실 0, 오류 반환 0). 꼬리: "ack 를 못 받았는데 레코드가 남아 있을 수 있나?" — 있다 (2000 회 중 32 번). 프로그램은 끝났는데 WIP 폴링 중에 끊긴 경우. 그래서 앱 레벨 시퀀스 번호로 중복을 거른다. 더 강한 방법: 데이터를 먼저 쓰고 마지막에 "커밋 바이트"(1→0 한 번 더 프로그램)로 확정하는 2 단계 쓰기.

</details>

<details>
<summary>Q7. 블록 보호(BP, SRP, WP#)는 어떻게 쓰나요? 처음 받은 보드에서 쓰기가 안 되면?</summary>

BP 비트는 칩의 위쪽(또는 TB 비트에 따라 아래쪽) 영역을 잠근다 — 부트로더·공장 보정 데이터 보호용. 잠긴 영역에 대한 PP/SE 는 **조용히 무시**된다 (에러 비트가 없는 부품이 많다). 그래서 드라이버가 SR 을 읽어 미리 `E_PROTECTED` 를 내는 게 좋다. SRP=1 + WP# low 면 SR 자체를 못 바꾼다 → 보드 레벨 쓰기 보호 (제조 라인에서 WP# 점퍼). 첫 보드에서 쓰기가 안 되면: RDSR 로 BP 를 본다. 출하 시 보호가 켜져 오는 부품/주문 옵션이 있다. 해제 후 다시 읽어 확인, 실패면 WP# 핀과 SRP 를 의심. 운영 중에는 반대로 **필요한 영역만 다시 잠근다** — BP 는 2 의 거듭제곱 크기라 원하는 경계와 안 맞으면 과보호된다 (연습 3: [0xEF000, 1M) 를 덮으려면 BP=2, 60 KB 더 잠김 → 파티션을 BP 경계에 맞춰 설계).

</details>

<details>
<summary>Q8. JEDEC ID 만으로 칩을 다 알 수 있나요?</summary>

아니다. 3 바이트(제조사, 타입, 용량 코드)는 같은 계열의 리비전 차이(전압, 지원 명령, 보호 비트 배치, 타이밍)를 구분하지 못하고, 용량 코드를 2^n 으로 읽는 것도 관례일 뿐 모든 제조사가 따르지 않는다. 제조사 코드는 JEP106 의 continuation code(0x7F) 체계라 첫 바이트만으로 모호한 경우도 있다. 그래서 드라이버는 (1) 부품 표(ID → 크기, 페이지, erase 크기, 플래그)와 (2) SFDP(JESD216, 0x5A) 를 함께 쓴다. Linux spi-nor 는 표 + SFDP 파싱으로 erase 크기·quad 설정·4 바이트 주소 방식을 정한다 (구현 세부는 커널 버전별 — 확인 권장). bring-up 첫 체크리스트: ID 가 FF FF FF 면 CS/클럭/전원/MISO 풀업, 00 00 00 이면 MISO 가 GND 나 단락, 엉뚱한 값이면 SPI 모드(CPOL/CPHA — COMM-B04)와 클럭 속도.

</details>

<details>
<summary>Q9. QSPI 메모리 매핑(XIP)으로 코드를 플래시에서 돌리는데 같은 플래시에 erase 하면?</summary>

erase/프로그램 중에는 칩이 읽기 명령을 무시하므로(WIP 동안 RDSR 만), 그 플래시에서 명령어를 가져오던 CPU 는 쓰레기를 읽거나 멈춘다. 대부분의 SPI NOR 는 RWW(Read-While-Write) 를 지원하지 않는다 (지원 부품·뱅크 구조는 예외 — 확인 권장). 해결: erase/프로그램을 부르는 코드와 그 사이에 돌 수 있는 인터럽트 핸들러를 **RAM 에서 실행**(`.ramfunc` 섹션), 메모리 매핑 모드를 끄고 간접 모드로 명령을 보낸 뒤 다시 켠다, 캐시를 invalidate (ARCH-M12). 긴 erase 동안 인터럽트 지연이 문제면 erase suspend/resume(0x75/0x7A, 부품별) 으로 쪼갠다. 이런 이유로 펌웨어 업데이트는 보통 별도 슬롯에 쓰고 부트로더가 바꾼다 (BM-B06, BM-A06 다음 단계).

</details>

## 9. 추가 연습

### 연습 1 — 1 바이트 바꾸기 (`nor_update`)

섹터 안의 임의 구간을 바꾸는 `nor_update(d, addr, buf, len, scratch, &erases)` 를 짜라. 바꿀 비트가 1→0 뿐이면 erase 없이 PP 만, 아니면 4 KB 를 `scratch` 로 읽어 고치고 erase 후 다시 쓴다 (전부 0xFF 인 페이지는 건너뜀). 섹터 경계를 넘으면 `E_ARG`.

<details>
<summary>답</summary>

판정은 `(old & new) != new` 인 바이트가 하나라도 있는가 (= 0→1 이 필요한 비트). 실제 출력: `600 random updates: 233 erases (367 skipped because only 1->0 was needed)` — 랜덤 데이터의 1/3 을 "현재 값의 부분집합"으로 만들어 섞었다. 기준 모델은 `memcpy` 다 (update 는 진짜 덮어쓰기 의미). 주의: erase 와 재쓰기 사이에 끊기면 섹터 전체를 잃는다 (Q5).

</details>

### 연습 2 — 최소 erase 계획 (4K / 32K / 64K)

W25Q 류는 32 KB 블록 erase(0x52)도 있다 (대표값). `erase_plan(addr, len, out, cap)` 이 정렬된 가장 큰 단위를 탐욕적으로 고르게 하고, 256 KB 창의 모든 섹터 구간 2080 개에서 DP 최소 명령 수와 같은지 확인하라.

<details>
<summary>답</summary>

블록들이 서로 포함 관계(64K 정렬 ⊂ 32K 정렬 ⊂ 4K 정렬)라 "지금 주소에서 정렬되고 남은 길이에 들어가는 가장 큰 것"이 최적이다 — 더 작은 것을 고르면 그 큰 블록 영역을 덮는 데 명령이 더 든다. DP: `best[i] = min(best[i−1], best[i−8] if 32K 정렬, best[i−16] if 64K 정렬) + 1`. 2080 구간 전부 일치, 덮는 범위 정확. 예: `0x7000+0x1A000 -> 4 steps: SE@07000  32K@08000  BE@10000  SE@20000` (실제 출력). 명령 수가 아니라 시간을 최소화하려면 단위별 시간(대표값 45/120/150 ms 류)을 가중치로 같은 DP.

</details>

### 연습 3 — 보호할 BP 값 고르기

`[from, size)` 를 덮는 가장 작은 BP 를 돌려주는 `bp_for_protect(from, size)` 를 짜고, 모든 섹터 정렬 `from` 에 대해 "덮는다"와 "하나 작은 BP 는 못 덮는다"를 확인하라.

<details>
<summary>답</summary>

BP 0..7 을 차례로 `bp_protected_from(bp << 2, size) <= from` 인지 본다. 실제 출력: `protect [0xEF000, 1M) -> BP=2 protects from 0xE0000 (60 KB more than asked)`. BP 영역은 64 KB × 2^k 라 원하는 경계와 다르면 과보호된다 → 보호할 파티션(부트로더, 보정 데이터)을 칩의 위쪽 2^k 블록에 맞춰 배치한다. 부품에 따라 SEC 비트(4 KB 단위)·TB 비트(아래쪽)·개별 블록 잠금(WPS)이 있다 — 확인 권장.

</details>

### 연습 4 — CS 를 잡은 채 RDSR 연속 읽기

`nor_wait_ready_cont(d, timeout, &bytes)`: CS↓, 0x05, 그 뒤 WIP=0 이 나올 때까지 바이트를 계속 읽고 CS↑. 타임아웃이면 CS 를 올리고 `E_TIMEOUT`.

<details>
<summary>답</summary>

RDSR 은 명령 뒤 매 바이트마다 최신 SR 을 내보내므로 프레임 하나로 된다 (모델의 `chip_on_byte` 가 그렇게 짜여 있다). 실제 출력: `one CS frame: 699 status bytes for t_PP=700 us (bus held the whole time)`. 해상도는 1 바이트(1 µs)지만 그동안 버스는 이 칩 전용이다. 타임아웃 경로에서 CS 를 올리는 것을 잊으면 다음 명령이 RDSR 프레임의 연속으로 해석된다 — 테스트가 `!g_chip.cs_low` 를 확인한다.

</details>

### 연습 5 — 4 바이트 주소 프레임

16 MB 를 넘는 칩은 3 바이트 주소로 다 가리킬 수 없다. `nor_addr_frame(op, addr, size, out, cap)` 가 크기에 따라 `03/02/20 + 3 B` 또는 `13/12/21 + 4 B` 를 만들게 하라 (opcode 는 W25Q256·MX25L256 류 대표값 — 확인 권장).

<details>
<summary>답</summary>

`size > 2^24` 면 4 바이트, 주소 MSB first. `addr >= size` 나 `cap` 부족은 `E_ARG`. 경계: 16 MB 칩의 0xFFFFFF 는 `03 FF FF FF`, 32 MB 칩의 0x1000000 PP 는 `12 01 00 00 00`. 대안인 "4 바이트 모드 진입(0xB7)"은 opcode 를 안 바꿔도 되지만, 그 모드가 MCU 리셋 뒤에도 남아 3 바이트로 읽는 부트 ROM 이 실패할 수 있다 (Q4).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 26/26 passed, exit 0
```

## 10. 핵심 정리

- NOR: **프로그램 = 1→0 (AND), erase = 섹터 단위 →0xFF**. 바꾸기 = read-modify-erase-write, 그래서 상위 계층은 제자리 갱신을 피한다.
- 쓰기 순서: **WREN → (WEL 확인) → 명령 → CS↑ 에서 실행 → WIP 폴링(타임아웃 = max)**. WEL 은 연산이 끝나면 저절로 0 → 매 페이지/섹터마다 다시.
- WIP=1 동안 RDSR 외 전부 무시 (READ = 0xFF, RDID = FF FF FF). MCU 리셋은 플래시 동작을 멈추지 않는다 → probe 는 기다렸다 다시 읽는다.
- PP 는 **페이지 안에서 랩**한다. `n = 256 − (addr % 256)` 으로 자르지 않으면 에러 없이 데이터가 틀린다.
- erase 는 정렬된 가장 큰 단위 (64 KB 블록 = 섹터 16 개보다 4.8 배 빠름, 대표값 기준).
- 보호: BP(영역) + SRP/WP#(하드웨어). 칩은 보호 위반을 조용히 무시 → 드라이버가 SR 로 먼저 거절하고, WRSR 뒤 다시 읽고, 실패면 WRDI.
- 전원 차단 = 비트 일부만 반영. 로그 = 레코드 CRC + 섹터 세대 번호 + **blank check** + 실패 섹터 포기. 보장은 "깨진 것은 안 돌려줌, ack 된 것은 안 잃음", 그리고 적어도 한 번(중복 가능).

## 참고자료

- Winbond, *W25Q80DV / W25Q 계열 데이터시트* — 명령표(0x06/0x04/0x05/0x01/0x02/0x20/0x52/0xD8/0x03/0x9F), 상태 레지스터(BUSY, WEL, BP, TB, SEC, SRP), 페이지 프로그램 랩 설명, t_PP/t_SE/t_BE/t_W typical·max: <https://www.winbond.com/> (이 레슨의 숫자는 기억에 의존한 대표값 — 부품·리비전별 확인 권장)
- JEDEC, *JESD216 Serial Flash Discoverable Parameters (SFDP)*, *JEP106 Standard Manufacturer's Identification Code*: <https://www.jedec.org/> (등록 필요)
- Linux kernel, *SPI NOR framework* — `drivers/mtd/spi-nor/core.c` (`spi_nor_write` 페이지 분할, `spi_nor_wait_till_ready`, SFDP 파싱 `sfdp.c`), 문서 <https://docs.kernel.org/driver-api/mtd/spi-nor.html> (세부는 커널 버전별 — 확인 권장)
- Zephyr Project, *JEDEC SPI-NOR driver* (`drivers/flash/spi_nor.c`, `jedec,spi-nor` DT 바인딩): <https://docs.zephyrproject.org/latest/hardware/peripherals/flash.html>
- U-Boot, `sf probe / sf erase / sf write` 명령 (`cmd/sf.c`) — bring-up 때 쓰는 가장 빠른 확인 경로
- STMicroelectronics, *AN4760 Quad-SPI interface on STM32 microcontrollers* — 간접/자동 폴링/메모리 매핑 모드 (문서 번호 기억에 의존 — 확인 권장)
- LittleFS 설계 문서 (전원 차단 내성, copy-on-write): <https://github.com/littlefs-project/littlefs/blob/master/DESIGN.md>
- 관련 레슨: COMM-B04 (SPI 모드 — ID 가 엉뚱할 때), COMM-B05 (명령 바이트 + CS 프레임, CS 버그), COMM-M12 (버스 독립 드라이버 계층), COMM-M13 (공유 SPI 버스 큐 — 폴링이 버스를 잡는 문제), COMM-M14 (중복 처리, 적어도 한 번), ALGO-M01 (CRC-16/CCITT-FALSE), BM-B06 (플래시 파티션 레이아웃), ARCH-M12 (캐시 invalidate — XIP 뒤 갱신), COMM-A09 (하드웨어 없이 드라이버 회귀 — record/replay), COMM-A11 (이 레슨의 NOR 축약판을 쓰는 센서 허브). 다음 단계: BM-A02 (내장 플래시 드라이버), BM-A06 (전원 차단 내성 업데이트), STOR-A05 (FatFs vs LittleFS vs raw KV)
