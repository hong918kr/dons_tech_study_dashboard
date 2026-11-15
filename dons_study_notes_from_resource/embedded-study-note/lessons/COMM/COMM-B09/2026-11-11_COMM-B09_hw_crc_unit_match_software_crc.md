---
id: COMM-B09
time: 60분
updated: 2026-11-11
---
# COMM-B09 하드웨어 CRC 유닛과 소프트웨어 CRC 맞추기

## 학습 목표

- 소프트웨어 CRC-32 두 가지 — **CRC-32/ISO-HDLC**(zlib·Ethernet, 반사형)와 **CRC-32/MPEG-2**(비반사, STM32 CRC 유닛 기본값) — 를 비트 단위로 짜고, RevEng 매개변수(width, poly, init, refin, refout, xorout)로 둘의 차이를 정확히 말한다.
- STM32 CRC 주변장치를 흉내낸 mock(32/16/8비트 DR 쓰기, CRC_INIT, CRC_POL/POLYSIZE, REV_IN, REV_OUT)을 만들고, **"하드웨어는 쓴 값을 MSB 먼저 LFSR 에 넣을 뿐"** 이라는 한 문장에서 모든 설정이 나오는 것을 이해한다.
- 바이트 순서(리틀 엔디언 LDR vs REV) × REV_IN × REV_OUT × INIT × 최종 XOR 64가지를 **테스트로 전수 탐색**해서 HW == SW 가 되는 조합을 찾고, 결과값만 보고 무엇을 빠뜨렸는지 진단한다.
- 4의 배수가 아닌 꼬리를 구형(F1/F2/F4) 유닛에서는 SW 로 이어서, 신형 유닛에서는 16/8비트 쓰기로 처리하고, 부트로더의 이미지 CRC 검증 드라이버를 짠다.

## 1. 문제

브링업(bring-up) 때 "PC 빌드 스크립트가 붙인 CRC 와 MCU 하드웨어 CRC 가 안 맞아요"는 거의 모든 펌웨어 팀이 한 번씩 겪는 일이고, 면접에서는 "STM32 CRC 유닛으로 zlib CRC-32 를 내 보세요 → 왜 값이 다르죠? → 길이가 4의 배수가 아니면?" 로 이어지는 단골 질문이다.

STM32 의 CRC 유닛은 CRC 를 **계산하는 기계일 뿐, 어떤 표준 CRC 인지는 모른다.** 우리가 32비트 워드를 데이터 레지스터(DR, data register)에 쓰면 그 워드를 MSB 부터 한 비트씩 다항식 0x04C11DB7 의 LFSR(선형 귀환 시프트 레지스터, linear feedback shift register)에 넣는다. 끝. 그런데 바이트 버퍼를 리틀 엔디언(little-endian) Cortex-M 이 `LDR` 로 읽으면 **4번째 바이트가 MSB** 가 되고, zlib CRC 는 바이트마다 **bit0 을 먼저** 넣는(반사, reflected) 알고리즘이며, 마지막에 비트를 뒤집는다(xorout). 이 세 가지 차이를 하드웨어 설정·바이트 순서·소프트웨어 후처리로 하나씩 맞추는 것이 이 레슨이다. Don 의 SSD 펌웨어(데이터 경로 보호용 CRC, 펌웨어 이미지 검증)나 RF 모듈 펌웨어 업데이트의 이미지 CRC 와 같은 이야기다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="c9a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">바이트 버퍼 → 워드 → CRC_DR → LFSR → 읽기 → 소프트웨어 후처리</text>
  <text x="10" y="44" fill="currentColor">메모리 "1234"</text>
  <g stroke="currentColor">
    <rect x="10" y="52" width="44" height="26" class="f-mut"/><rect x="56" y="52" width="44" height="26" class="f-mut"/>
    <rect x="102" y="52" width="44" height="26" class="f-mut"/><rect x="148" y="52" width="44" height="26" class="f-mut"/>
  </g>
  <g font-family="monospace">
    <text x="24" y="70" fill="currentColor">31</text><text x="70" y="70" fill="currentColor">32</text>
    <text x="116" y="70" fill="currentColor">33</text><text x="162" y="70" fill="currentColor">34</text>
    <text x="20" y="96" fill="currentColor">+0</text><text x="66" y="96" fill="currentColor">+1</text>
    <text x="112" y="96" fill="currentColor">+2</text><text x="158" y="96" fill="currentColor">+3</text>
  </g>
  <line x1="196" y1="65" x2="246" y2="65" stroke="currentColor" marker-end="url(#c9a)"/>
  <text x="200" y="56" fill="currentColor" font-family="monospace">LDR</text>
  <rect x="250" y="52" width="130" height="26" class="f-warn" stroke="currentColor"/>
  <text x="268" y="70" fill="currentColor" font-family="monospace">0x34333231</text>
  <text x="250" y="96" fill="currentColor">LE: 바이트 3 이 MSB</text>
  <line x1="384" y1="65" x2="440" y2="65" stroke="currentColor" marker-end="url(#c9a)"/>
  <text x="388" y="56" fill="currentColor">STR</text>
  <rect x="444" y="52" width="130" height="26" class="f-mut" stroke="currentColor"/>
  <text x="462" y="70" fill="currentColor" font-family="monospace">CRC-&gt;DR</text>
  <text x="444" y="96" fill="currentColor">신형: 8/16/32비트 쓰기</text>
  <line x1="509" y1="104" x2="509" y2="124" stroke="currentColor"/>
  <line x1="509" y1="124" x2="100" y2="124" stroke="currentColor"/>
  <line x1="100" y1="124" x2="100" y2="140" stroke="currentColor" marker-end="url(#c9a)"/>
  <rect x="30" y="144" width="140" height="44" class="f-acc" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="66" y="163" fill="currentColor">REV_IN</text>
  <text x="40" y="180" fill="currentColor">없음/바이트/반워드/워드</text>
  <line x1="172" y1="166" x2="206" y2="166" stroke="currentColor" marker-end="url(#c9a)"/>
  <rect x="210" y="144" width="230" height="44" class="f-ok" stroke="currentColor"/>
  <text x="222" y="163" fill="currentColor">LFSR: MSB 먼저 1비트씩</text>
  <text x="222" y="180" fill="currentColor" font-family="monospace">POL 0x04C11DB7, 32비트</text>
  <line x1="442" y1="166" x2="476" y2="166" stroke="currentColor" marker-end="url(#c9a)"/>
  <rect x="480" y="144" width="110" height="44" class="f-acc" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="508" y="163" fill="currentColor">REV_OUT</text>
  <text x="490" y="180" fill="currentColor">읽을 때 반사</text>
  <line x1="592" y1="166" x2="626" y2="166" stroke="currentColor" marker-end="url(#c9a)"/>
  <rect x="630" y="144" width="100" height="44" class="f-mut" stroke="currentColor"/>
  <text x="648" y="171" fill="currentColor">DR 읽기</text>
  <text x="210" y="208" fill="currentColor">RESET 비트 → 상태 = CRC_INIT</text>
  <line x1="680" y1="190" x2="680" y2="222" stroke="currentColor" marker-end="url(#c9a)"/>
  <rect x="560" y="226" width="170" height="28" class="f-warn" stroke="currentColor"/>
  <text x="570" y="245" fill="currentColor">SW: ^ XOROUT (HW 없음)</text>
  <text x="10" y="244" fill="currentColor">점선 상자 = 신형(F0/F3/F7/L4/G0/G4/H7 등)에만 있는 설정.</text>
  <text x="10" y="266" fill="currentColor">구형 F1/F2/F4: 다항식·INIT 고정, 반사 없음, 32비트 쓰기만 → CRC-32/MPEG-2 (워드를 BE 로 볼 때)</text>
  <text x="10" y="288" fill="currentColor">계열별 세부(좁은 쓰기와 REV_IN, POLYSIZE 별 REV_OUT)는 이 레슨 mock 의 정의 — 실제 칩은 RM 확인.</text>
</svg>
```

### Part A — 소프트웨어 기준 두 개와 CRC 유닛 mock

- `reflect(x, nbits)` — 하위 nbits 비트를 좌우로 뒤집기. Cortex-M3/M4 에서는 `RBIT` 한 명령.
- `crc32_msb_update(crc, p, n)` (비반사, 0x04C11DB7) 과 `crc32_lsb_update(crc, p, n)` (반사, 0xEDB88320) — 초기값·최종 XOR 없이 **상태만** 이어 가는 함수. 그래서 HW 가 계산하다 만 상태에서 이어받을 수 있다(Part B). 그 위에 `crc32_iso_hdlc` = `~lsb(0xFFFFFFFF, …)`, `crc32_mpeg2` = `msb(0xFFFFFFFF, …)` 는 주어진다.
- mock `crc_unit`: `rev_in_apply()` (REV_IN: 바이트/반워드/워드 단위 반사), `hw_write(u, data, width)` (MSB 먼저 LFSR), `hw_read()` (REV_OUT). `hw_init/hw_reset` 은 주어진다.
- 테스트: 반사 8비트 전수 + 랜덤 왕복, RM 의 REV_IN 예시(0x1A2B3C4D → 0x58D43CB2 / 0xD458B23C / 0xB23CD458), 카탈로그 check 값 `"123456789"` → 0xCBF43926 / 0x0376E6E7 (+ BZIP2 0xFC891918), 두 SW 구현을 잇는 성질 1000개, 구형 유닛에 워드 0x12345678 → 0xDF8A8A2B, 구형 유닛 = MPEG-2 (BE 로드일 때만) 1000개.

### Part B — 설정 탐색, 진단, 구형 유닛의 꼬리

- `hw_run(u, cfg, p, n)` — 신형 유닛을 `{BE/LE 로드, REV_IN, REV_OUT, INIT, XOROUT}` 로 설정해 n(4의 배수) 바이트를 워드로 먹인다. 테스트가 **64가지 전부**를 랜덤 메시지 8개에 돌려 SW 와 일치하는 조합을 출력하고, 정확히 ISO-HDLC 2개·MPEG-2 1개여야 한다.
- `crc_diagnose(hw, sw)` — 결과값만 보고 "최종 XOR 빠짐 / REV_OUT 빠짐 / 둘 다 / 그 밖(바이트 순서·REV_IN·INIT)" 판정.
- `f4_crc32_mpeg2`, `f4_crc32_iso` — 구형 유닛(32비트 쓰기만)으로 임의 길이 처리: 워드는 HW, 꼬리 0~3 바이트는 **DR 값을 SW 상태로 넘겨** 이어서. 길이 0~64 전부 + 랜덤 2000개(길이 0~300, 시작 오프셋 0~3). 장애 주입: 꼬리를 0 으로 채워 넣으면 1000개 중 몇 개가 맞나(답: 0).

### Part C — 신형 유닛 드라이버와 부트로더 이미지 검증

- `crc32_begin / crc32_feed / crc32_end` — CRC-32/ISO-HDLC 설정(REV_IN=BYTE + BE 묶음, REV_OUT=1, INIT=0xFFFFFFFF), 청크마다 32비트 쓰기 → 남은 2바이트 16비트 쓰기 → 1바이트 8비트 쓰기. 랜덤 청크(0~37 바이트) 2000개, 비정렬 시작, 1027 바이트 = 워드 256 + 반워드 1 + 바이트 1 쓰기 횟수 확인. 장애 주입: 이 드라이버를 구형 유닛에서 돌리면?
- `image_ok(u, flash, size)` — `[magic "CB09"][len LE][payload][CRC32 LE]` 이미지 검증: 크기 → magic → len 범위(넘침 없이) → 64바이트씩 CRC → 저장 값 비교. 장애 주입: payload 1비트, CRC 1비트, CRC 를 BE 로 저장, len = 0xFFFFFFF8, magic 손상, 너무 작은 플래시.

## 2. Problem (English)

You are bringing up firmware on an STM32. The host build script appends `zlib.crc32(image)` (CRC-32/ISO-HDLC) to every firmware image; the bootloader must verify it **using the on-chip CRC peripheral**. Model the peripheral and find the configuration that makes hardware and software agree.

```c
uint32_t reflect(uint32_t x, unsigned nbits);
uint32_t crc32_msb_update(uint32_t crc, const uint8_t *p, size_t n);  /* poly 0x04C11DB7, MSB-first, no init/xorout */
uint32_t crc32_lsb_update(uint32_t crc, const uint8_t *p, size_t n);  /* poly 0xEDB88320, LSB-first */
void     hw_write(crc_unit *u, uint32_t data, unsigned width);       /* DR write, width = 8/16/32 */
uint32_t hw_read(const crc_unit *u);                                  /* DR read (applies REV_OUT) */
uint32_t hw_run(crc_unit *u, const hw_cfg *c, const uint8_t *p, size_t n);
int      crc_diagnose(uint32_t hw, uint32_t sw);
uint32_t f4_crc32_iso(crc_unit *u, const uint8_t *p, size_t n);      /* legacy unit, any n */
void     crc32_begin(crc_unit *u);  void crc32_feed(crc_unit *u, const uint8_t *p, size_t n);
uint32_t crc32_end(const crc_unit *u);
bool     image_ok(crc_unit *u, const uint8_t *flash, size_t flash_size);
```

Peripheral model: every DR write of `width` bits is (optionally bit-reversed per REV_IN, then) shifted into a `POLYSIZE`-bit LFSR **MSB first**; RESET loads `CRC_INIT`; a read returns the state, bit-reversed if REV_OUT. There is no final-XOR register. The legacy (F1/F2/F4) unit is fixed: poly 0x04C11DB7, init 0xFFFFFFFF, no reversal, 32-bit writes only. The CPU is little-endian; buffers may be unaligned; no dynamic allocation.

Examples:

```text
crc32_iso_hdlc("123456789") = 0xCBF43926        crc32_mpeg2("123456789") = 0x0376E6E7
legacy: RESET; DR = 0x12345678; DR -> 0xDF8A8A2B  (= CRC-32/MPEG-2 of bytes 12 34 56 78)
legacy, words loaded with LDR from "12345678": DR -> 0xFEFC54F9   (matches neither standard)
modern, REV_IN=BYTE, REV_OUT=1, BE-packed words + 8-bit tail, "123456789": ~DR = 0xCBF43926
```

Follow-ups: Why does the default STM32 CRC differ from zlib? What exactly does REV_IN reverse? How do you handle `n % 4 != 0` on an F4? Can two tasks share the unit? How would you feed it by DMA?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열네 군데: Part A 6개(A1 `reflect` ~ A6 `hw_read`), Part B 4개(B1 `hw_run` ~ B4 `f4_crc32_iso`), Part C 4개(C1~C3 드라이버, C4 `image_ok`). `mask_n`, `load_le32/load_be32`, `hw_init/hw_reset`, 하네스는 주어진다. 스텁은 입력을 그대로 또는 0 을 돌려주므로 멈추지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:174  reflect(0x01u, 8) == 0x80u && reflect(0x0Fu, 8) == 0xF0u && reflect(0x1Au, 8) == 0x58u
…
FAIL starter.c:193  crc32_iso_hdlc(chk, 9) == 0xCBF43926u
…
config search (load, REV_IN, REV_OUT, INIT, XOROUT) over 8 random 64-byte msgs:
  LE-load REV_IN=NONE REV_OUT=0 INIT=00000000 XOROUT=00000000  == CRC-32/ISO-HDLC
  …
  -> 64 configs == ISO-HDLC, 0 configs == MPEG-2 (of 64)
…
FAIL starter.c:385  image_ok(&u, flash, sizeof flash)
19/49 passed
```

(exit 1. "64 configs == ISO-HDLC" 같은 우스운 결과와 통과한 19개는 스텁끼리 우연히 같은 값(0)을 내서다 — 예: SW 스텁 `~0xFFFFFFFF` = 0 과 HW 스텁 0. 그래서 하네스는 상대 비교만이 아니라 **카탈로그 check 값 같은 절대값**도 같이 본다.)

## 4. 힌트

<details><summary>힌트 1 — 두 SW CRC 는 거울상</summary>

비반사: 바이트를 상위 8비트에 XOR(`crc ^= b << 24`), 8번 "MSB 가 1 이면 왼쪽 시프트 후 ^ 0x04C11DB7". 반사: 바이트를 하위 8비트에 XOR, 8번 "LSB 가 1 이면 오른쪽 시프트 후 ^ 0xEDB88320". 0xEDB88320 = reflect(0x04C11DB7, 32). 원리는 ALGO-M01, 테이블/slicing 가속은 ALGO-M02.

</details>

<details><summary>힌트 2 — 하드웨어는 "쓴 값의 MSB 부터"만 안다</summary>

`hw_write` 는 data 의 bit(width−1) 부터 bit0 까지: `fb = 상태의 최상위 비트 ^ 데이터 비트; 상태 <<= 1 (polysize 마스크); if (fb) 상태 ^= POL`. 비반사 SW 의 바이트 루프와 똑같은 식이다 — 바이트 대신 32비트 덩어리일 뿐.

</details>

<details><summary>힌트 3 — LFSR 에 들어가는 비트 순서를 종이에 적어라</summary>

"1234" 를 LE 로 읽은 0x34333231 을 MSB 먼저 넣으면 '4' 의 bit7 이 처음이다. MPEG-2 는 '1' 의 bit7 이 처음이어야 하고, ISO-HDLC 는 '1' 의 **bit0** 이 처음이어야 한다. 어떤 변환(REV = 바이트 스왑, RBIT = 32비트 반사, REV_IN=BYTE = 바이트마다 반사)이 각 순서를 만드는가?

</details>

<details><summary>힌트 4 — 반사형 상태 = 비반사 상태를 뒤집은 것</summary>

같은 비트 열을 넣었을 때 반사형 SW 의 레지스터는 비반사 LFSR 레지스터를 `reflect(…, 32)` 한 것과 같다. 그래서 REV_OUT 이 있고, 구형 유닛에서 꼬리를 SW 로 이어 갈 때 `crc32_lsb_update(reflect(DR, 32), tail, r)` 로 넘기면 된다.

</details>

<details><summary>힌트 5 — 꼬리는 세 가지 방법</summary>

(1) 구형: HW 상태를 SW 로 넘겨 0~3 바이트 계속. (2) 신형: 8/16비트 DR 쓰기. 이때 REV_IN=BYTE + BE 묶음이면 쓰기 폭과 상관없이 같은 규칙이 성립한다. (3) 0 으로 채우기 — 표준 CRC 가 아니게 된다(양쪽이 약속할 때만).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | 상대(PC/프로토콜)가 쓰는 CRC 의 **이름과 매개변수**(width/poly/init/refin/refout/xorout), 칩 계열(F4 형인가 신형인가), 길이가 4의 배수인가, 버퍼 정렬, 저장된 CRC 의 엔디언 |
| 2. 기준부터 | 비트 단위 SW 구현 + `"123456789"` check 값으로 기준을 고정 (RevEng 카탈로그) |
| 3. HW 를 모델링 | "쓴 값을 MSB 먼저 LFSR" — 이 한 줄에서 바이트 순서·반사·XOR 차이가 전부 나온다 |
| 4. 맞추기 | 비트 순서 맞추기(REV/RBIT/REV_IN) → 출력 반사(REV_OUT) → 최종 XOR(SW) → 꼬리 |
| 5. 테스트 | check 값, 전 설정 탐색, 길이 0~64 전부, 비정렬, 랜덤 청크, 장애 주입 |
| 6. 비용 | 워드당 버스 쓰기 1번 + RBIT/REV 1명령(M3/M4), 꼬리 ≤ 3바이트 SW. RAM 0, 표 0 바이트 |

### Part A — 매개변수 6개로 CRC 를 정확히 부르기

"CRC-32" 라는 이름만으로는 값이 정해지지 않는다. RevEng 카탈로그(Greg Cook)의 Rocksoft 모델 매개변수로 부른다:

| 이름 | width | poly | init | refin | refout | xorout | check("123456789") |
|---|---|---|---|---|---|---|---|
| CRC-32/ISO-HDLC (zlib, Ethernet, PKZIP) | 32 | 0x04C11DB7 | 0xFFFFFFFF | true | true | 0xFFFFFFFF | 0xCBF43926 |
| CRC-32/MPEG-2 (STM32 유닛 기본) | 32 | 0x04C11DB7 | 0xFFFFFFFF | false | false | 0 | 0x0376E6E7 |
| CRC-32/BZIP2 | 32 | 0x04C11DB7 | 0xFFFFFFFF | false | false | 0xFFFFFFFF | 0xFC891918 |

다항식은 셋 다 같다. 다른 것은 **비트를 넣는 순서(refin)**, **결과를 읽는 방향(refout)**, **마지막 XOR** 뿐이다. 세 check 값은 이 레슨의 코드로 계산했고 ISO-HDLC 는 Python `zlib.crc32` 로도 확인했다(0xcbf43926, "a" → 0xe8b7be43, fox 문장 → 0x414fa339).

**두 구현을 서로 검사하는 성질.** 반사형 = "바이트마다 bit0 먼저" 이므로, 바이트를 미리 비트 반사해 비반사 LFSR 에 넣고 결과를 32비트 반사하면 같아야 한다:

    crc32_lsb_update(0xFFFFFFFF, data) == reflect(crc32_msb_update(0xFFFFFFFF, rev8(data)), 32)

(init 0xFFFFFFFF 는 반사해도 같아서 성립한다.) 테스트는 랜덤 1000개로 확인한다 — 한쪽만 틀리면 바로 드러난다. 이 성질이 곧 **REV_IN/REV_OUT 이 존재하는 이유**다: 비반사 하드웨어 하나로 반사형 CRC 를 내려면 입력과 출력을 뒤집으면 된다.

**mock 의 LFSR.** `hw_write` 의 루프는 SW 비반사 루프와 같은 식이다:

```c
for (int i = (int)width - 1; i >= 0; i--) {             /* MSB 먼저 */
    uint32_t fb = ((u->state & top) ? 1u : 0u) ^ ((data >> i) & 1u);
    u->state = (u->state << 1) & m;
    if (fb) u->state ^= u->pol & m;
}
```

`top = 1 << (polysize − 1)`, `m = mask_n(polysize)` 라서 POLYSIZE 16/8/7 도 같은 코드로 된다(연습 1 에서 CRC-16/MODBUS, CRC-8/SMBUS, CRC-7/MMC 까지 재현). `mask_n(32)` 를 `1u << 32` 로 짜면 UB 라 따로 처리한다(C-B01 참고).

**REV_IN 예시.** RM 에 실린 예(기억 기반 — RM 확인 권장)를 mock 이 그대로 낸다: 입력 0x1A2B3C4D 가 바이트 단위 반사 → 0x58D43CB2, 반워드 단위 → 0xD458B23C, 워드 단위 → 0xB23CD458. 바이트 단위는 **바이트 위치는 그대로, 바이트 안의 비트만** 뒤집는다(0x1A = 0001 1010 → 0101 1000 = 0x58). 워드 단위는 32비트 전체 RBIT 이라 바이트 위치도 뒤집힌다.

**구형 유닛 = MPEG-2 (워드를 BE 로 볼 때).** 리셋 후 워드 0x12345678 을 쓰면 0xDF8A8A2B — 바이트 `12 34 56 78` 의 CRC-32/MPEG-2 와 같다(테스트가 두 값을 모두 확인). 그런데 바이트 버퍼를 `LDR` 로 읽어 넣으면 랜덤 1000개 중 **992개가 틀렸다** (맞은 8개는 길이 0 인 메시지). LE 로드 때문에 워드 안 바이트 순서가 거꾸로 들어가기 때문이다.

### Part B — 64가지 설정 중 무엇이 맞나 (실측)

신형 유닛에서 `{LE/BE 로드} × REV_IN{NONE, BYTE, HALF, WORD} × REV_OUT{0,1} × INIT{0, 0xFFFFFFFF} × XOROUT{0, 0xFFFFFFFF}` = 64가지를 랜덤 64바이트 메시지 8개에 돌린 실제 출력:

```text
config search (load, REV_IN, REV_OUT, INIT, XOROUT) over 8 random 64-byte msgs:
  LE-load REV_IN=WORD REV_OUT=1 INIT=FFFFFFFF XOROUT=FFFFFFFF  == CRC-32/ISO-HDLC
  BE-load REV_IN=NONE REV_OUT=0 INIT=FFFFFFFF XOROUT=00000000  == CRC-32/MPEG-2
  BE-load REV_IN=BYTE REV_OUT=1 INIT=FFFFFFFF XOROUT=FFFFFFFF  == CRC-32/ISO-HDLC
  -> 2 configs == ISO-HDLC, 1 configs == MPEG-2 (of 64)
```

왜 정확히 이 셋인지는 **LFSR 에 들어가는 비트 순서**만 보면 된다:

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="c9b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">"1234" (31 32 33 34) 한 워드가 LFSR 에 들어가는 순서</text>
  <line x1="250" y1="36" x2="700" y2="36" stroke="currentColor" marker-end="url(#c9b)"/>
  <text x="250" y="30" fill="currentColor">먼저 들어감</text><text x="600" y="30" fill="currentColor">나중</text>
  <text x="10" y="62" fill="currentColor">LE 로드, REV 없음 (F4 그대로)</text>
  <text x="10" y="80" fill="currentColor" font-family="monospace">DR ← 0x34333231  ✗ 표준 아님</text>
  <g stroke="currentColor">
    <rect x="250" y="50" width="110" height="32" class="f-warn"/><rect x="365" y="50" width="110" height="32" class="f-warn"/>
    <rect x="480" y="50" width="110" height="32" class="f-warn"/><rect x="595" y="50" width="110" height="32" class="f-warn"/>
  </g>
  <g font-family="monospace">
    <text x="262" y="71" fill="currentColor">34: b7 … b0</text><text x="377" y="71" fill="currentColor">33: b7 … b0</text>
    <text x="492" y="71" fill="currentColor">32: b7 … b0</text><text x="607" y="71" fill="currentColor">31: b7 … b0</text>
  </g>
  <text x="10" y="126" fill="currentColor">BE 로드(REV), REV 없음</text>
  <text x="10" y="144" fill="currentColor" font-family="monospace">DR ← 0x31323334  ✓ MPEG-2</text>
  <g stroke="currentColor">
    <rect x="250" y="114" width="110" height="32" class="f-ok"/><rect x="365" y="114" width="110" height="32" class="f-ok"/>
    <rect x="480" y="114" width="110" height="32" class="f-ok"/><rect x="595" y="114" width="110" height="32" class="f-ok"/>
  </g>
  <g font-family="monospace">
    <text x="262" y="135" fill="currentColor">31: b7 … b0</text><text x="377" y="135" fill="currentColor">32: b7 … b0</text>
    <text x="492" y="135" fill="currentColor">33: b7 … b0</text><text x="607" y="135" fill="currentColor">34: b7 … b0</text>
  </g>
  <text x="10" y="190" fill="currentColor">LE 로드 + REV_IN=WORD</text>
  <text x="10" y="208" fill="currentColor">(F4: RBIT 후 쓰기)  ✓ ISO-HDLC</text>
  <g stroke="currentColor">
    <rect x="250" y="178" width="110" height="32" class="f-acc"/><rect x="365" y="178" width="110" height="32" class="f-acc"/>
    <rect x="480" y="178" width="110" height="32" class="f-acc"/><rect x="595" y="178" width="110" height="32" class="f-acc"/>
  </g>
  <g font-family="monospace">
    <text x="262" y="199" fill="currentColor">31: b0 … b7</text><text x="377" y="199" fill="currentColor">32: b0 … b7</text>
    <text x="492" y="199" fill="currentColor">33: b0 … b7</text><text x="607" y="199" fill="currentColor">34: b0 … b7</text>
  </g>
  <text x="10" y="254" fill="currentColor">BE 로드 + REV_IN=BYTE</text>
  <text x="10" y="272" fill="currentColor">(HAL 방식)  ✓ ISO-HDLC</text>
  <g stroke="currentColor">
    <rect x="250" y="242" width="110" height="32" class="f-acc"/><rect x="365" y="242" width="110" height="32" class="f-acc"/>
    <rect x="480" y="242" width="110" height="32" class="f-acc"/><rect x="595" y="242" width="110" height="32" class="f-acc"/>
  </g>
  <g font-family="monospace">
    <text x="262" y="263" fill="currentColor">31: b0 … b7</text><text x="377" y="263" fill="currentColor">32: b0 … b7</text>
    <text x="492" y="263" fill="currentColor">33: b0 … b7</text><text x="607" y="263" fill="currentColor">34: b0 … b7</text>
  </g>
  <text x="10" y="302" fill="currentColor">아래 두 줄은 LFSR 이 보는 32비트가 똑같다: RBIT(0x34333231) = 바이트별 반사(0x31323334) = 0x8C4CCC2C.</text>
  <text x="10" y="322" fill="currentColor">REV_IN=HALF 는 어느 로드와도 33 34 31 32 / 32 31 34 33 순서가 되어 맞는 조합이 없다.</text>
</svg>
```

- **MPEG-2** 는 "바이트 순서대로, 각 바이트 bit7 먼저". LE 코어에서는 워드를 BE 로 만들어야 한다(`__REV`, 또는 load_be32). REV_IN 은 비트만 뒤집으므로 바이트 순서를 고칠 수 없다 → BE + NONE 하나뿐.
- **ISO-HDLC** 는 "바이트 순서대로, 각 바이트 bit0 먼저". 이 순서는 LE 워드를 통째로 RBIT 한 것과 같다(LE 워드의 bit0 = 바이트 0 의 bit0). 또는 BE 워드를 바이트마다 반사해도 같다. 두 조합 모두 내부 상태가 같고, 결과는 REV_OUT 으로 반사해 읽은 뒤 SW 에서 `~`.
- INIT 0 이나 XOROUT 을 빼먹은 조합은 단 한 번도 맞지 않았다 — 이 둘은 "비트 순서"와 독립된 별개 설정이다.

**트레이스 — "123456789" 의 앞 8바이트 (실제 값).**

| 단계 | 구형 + BE 로드 (MPEG-2) | 구형 + RBIT(LE) (ISO) | 신형 REV_IN=BYTE, REV_OUT=1 (ISO) |
|---|---|---|---|
| RESET | DR = FFFFFFFF | DR = FFFFFFFF | 상태 FFFFFFFF |
| 워드 0 `31 32 33 34` | 쓰기 31323334 → DR **A695C4AA** | 쓰기 RBIT(34333231) = 8C4CCC2C → DR 3AF83826 | 쓰기 31323334 → REV_IN 후 8C4CCC2C → 상태 3AF83826, DR 641C1F5C |
| 워드 1 `35 36 37 38` | 쓰기 35363738 → DR **49E3C2FB** | 쓰기 AC6CEC1C → DR 0AA4F8A6 | 상태 0AA4F8A6, DR **651F2550** |
| SW 비교 | MPEG-2("12345678") = 49E3C2FB ✓ | RBIT(DR) = 651F2550 = 반사형 SW 상태(8바이트 후) ✓ | 같음 |

A695C4AA 는 MPEG-2("1234") 와 같다. 그리고 구형 유닛에 LE 로드 "1234" 를 넣은 값 C2091428 은 MPEG-2("4321") 과 같다 — 바이트가 뒤집혀 들어간 증거. 가운데·오른쪽 열의 내부 상태가 같은 것(3AF83826, 0AA4F8A6)이 위 그림 아래 두 줄의 뜻이다.

### Part B — 결과값만 보고 진단하기

브링업 중 HW 값이 SW 와 다를 때, 데이터를 다시 돌리기 전에 결과 두 개만으로 걸러낼 수 있는 실수가 있다. `"12345678"`, 정답(LE + WORD + REV_OUT + XOR) 0x9AE0DAAF 기준 실측:

| 빠뜨린 것 | HW 결과 | 관계 | `crc_diagnose` |
|---|---|---|---|
| 없음 | 9AE0DAAF | hw == sw | OK |
| 최종 XOR | 651F2550 | ~hw == sw | NO_XOROUT |
| REV_OUT | F55B0759 | reflect(hw) == sw | NO_REVOUT |
| REV_OUT + XOR | 0AA4F8A6 | ~reflect(hw) == sw | NO_REVOUT_NO_XOROUT |
| REV_IN=BYTE (LE 로드인데) | E3C97CBC | 관계 없음 | OTHER |
| INIT = 0 | 003DFA39 | 관계 없음 | OTHER |

`reflect(~x) = ~reflect(x)` 이므로 REV_OUT 과 XOR 의 순서는 상관없다. OTHER 는 비트 순서(로드·REV_IN)나 INIT·다항식 문제라 **같은 데이터로 설정을 바꿔 가며 다시 돌려야** 한다 — 그게 위의 64가지 탐색이다. 실전에서는 탐색 코드를 펌웨어에 넣지 않고, 위 표처럼 "check 문자열 하나 + 후보 몇 개"를 디버거에서 돌린다.

### Part B — 구형 유닛에서 4의 배수가 아닌 길이

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">n = 9 ("123456789") = 워드 2개 + 꼬리 1바이트</text>
  <g stroke="currentColor">
    <rect x="10" y="32" width="56" height="28" class="f-mut"/><rect x="68" y="32" width="56" height="28" class="f-mut"/>
    <rect x="126" y="32" width="56" height="28" class="f-mut"/><rect x="184" y="32" width="56" height="28" class="f-mut"/>
    <rect x="250" y="32" width="56" height="28" class="f-mut"/><rect x="308" y="32" width="56" height="28" class="f-mut"/>
    <rect x="366" y="32" width="56" height="28" class="f-mut"/><rect x="424" y="32" width="56" height="28" class="f-mut"/>
    <rect x="490" y="32" width="56" height="28" class="f-warn"/>
  </g>
  <g font-family="monospace">
    <text x="30" y="51" fill="currentColor">31</text><text x="88" y="51" fill="currentColor">32</text>
    <text x="146" y="51" fill="currentColor">33</text><text x="204" y="51" fill="currentColor">34</text>
    <text x="270" y="51" fill="currentColor">35</text><text x="328" y="51" fill="currentColor">36</text>
    <text x="386" y="51" fill="currentColor">37</text><text x="444" y="51" fill="currentColor">38</text>
    <text x="510" y="51" fill="currentColor">39</text>
  </g>
  <text x="90" y="78" fill="currentColor">워드 0</text><text x="330" y="78" fill="currentColor">워드 1</text>
  <text x="560" y="51" fill="currentColor">꼬리 (n % 4 = 1)</text>
  <rect x="10" y="94" width="720" height="56" class="f-ok" stroke="currentColor"/>
  <text x="20" y="114" fill="currentColor">(1) 구형 + SW 꼬리: MPEG-2 → DR 49E3C2FB 를 그대로 SW 상태로 → crc32_msb_update(.., 39) = 0376E6E7</text>
  <text x="44" y="136" fill="currentColor">ISO → RBIT(DR) = 651F2550 을 반사형 SW 상태로 → crc32_lsb_update(.., 39) = 340BC6D9 → ~ = CBF43926</text>
  <rect x="10" y="160" width="720" height="56" class="f-ok" stroke="currentColor"/>
  <text x="20" y="180" fill="currentColor">(2) 신형: 32비트 쓰기 2번 + 8비트 쓰기 1번 (REV_IN=BYTE 라 0x39 → 0x9C 로 들어감)</text>
  <text x="44" y="202" fill="currentColor">상태 9B63D02C → DR 340BC6D9 → ~ = CBF43926. 꼬리 2바이트면 16비트 쓰기 (b0 &lt;&lt; 8 | b1)</text>
  <rect x="10" y="226" width="720" height="56" class="f-warn" stroke="currentColor"/>
  <text x="20" y="246" fill="currentColor">(3) 0 으로 채움: 39 00 00 00 을 워드로 → AE24E09D ✗ (MPEG-2 는 0376E6E7)</text>
  <text x="44" y="268" fill="currentColor">"다른 CRC" 다. 양쪽이 똑같이 채우기로 약속한 장치 고유 형식일 때만 쓸 수 있다 (연습 4)</text>
</svg>
```

HW 상태를 SW 로 넘길 수 있는 이유: CRC 는 "지금까지의 상태 + 다음 비트"만으로 다음 상태가 정해지는 상태 기계다. 구형 유닛의 DR 은 비반사 상태 그 자체이므로 MPEG-2 는 그대로 이어 가고, ISO 는 반사형 상태 = reflect(비반사 상태) 이므로 RBIT 한 번 뒤 이어 간다(힌트 4). 꼬리 0~3 바이트 × 8비트 = 최대 24번 시프트라 SW 비용은 무시할 만하다. 테스트: 길이 0~64 전부 + 랜덤 2000개(오프셋 0~3), 실측 출력:

```text
legacy + SW tail: 2065 msgs, mismatches mpeg2=0 iso=0, narrow writes=0
zero-padded tail: 0/1000 equal to CRC-32/MPEG-2
```

### Part C — 신형 유닛 드라이버: 왜 REV_IN=BYTE + BE 묶음인가

ISO-HDLC 를 내는 조합은 둘인데(LE+WORD, BE+BYTE), 드라이버는 **BE+BYTE** 를 골랐다. 이유:

1. **쓰기 폭과 무관하다.** REV_IN=BYTE 는 바이트 안의 비트만 뒤집으므로, 32비트 쓰기(BE 4바이트), 16비트 쓰기(`b0 << 8 | b1`), 8비트 쓰기(`b0`) 모두 "바이트 순서대로, bit0 먼저"가 된다. LE+WORD 는 32비트 쓰기에서만 확실하고, 좁은 쓰기에서 REV_IN=WORD 가 어떻게 동작하는지는 칩 문서를 확인해야 한다(mock 은 "쓰기 폭 단위로 반사"로 정의했다 — 이 정의에선 LE 꼬리도 맞지만, 그 가정에 기대지 않는 편이 안전하다).
2. ST HAL 의 바이트 입력 경로가 이 방식이다(`HAL_CRC_Calculate` 의 8비트 형식: 4바이트를 BE 로 묶어 32비트 쓰기, 남은 것은 16/8비트 쓰기 — HAL 소스 기억 기반, 확인 권장).

```c
for (; i + 4 <= n; i += 4) hw_write(u, load_be32(p + i), 32);
if (n - i >= 2) { hw_write(u, (uint32_t)p[i] << 8 | p[i + 1], 16); i += 2; }
if (n - i == 1) hw_write(u, p[i], 8);
```

상태가 HW 안에 있으므로 청크 사이에 아무것도 버퍼링하지 않는다 — 청크 경계가 4의 배수가 아니어도 그 청크의 꼬리를 좁은 쓰기로 바로 처리한다. 실측: 랜덤 청크(0~37 바이트)로 나눈 2000개 메시지 mismatches=0, 1027 바이트 = 32비트 256번 + 16비트 1번 + 8비트 1번.

**장애 주입 — 계열 혼동.** 같은 드라이버를 구형 유닛에서 돌리면 REV 설정은 무시되고(레지스터가 없음) 8비트 쓰기는 mock 정책상 무시된다(`bad_access`). 결과는 0xCBF43926 이 아니다. 실제 F4 에서 바이트 쓰기가 어떻게 되는지는 문서화돼 있지 않으니(RM 확인) "계열마다 드라이버가 다르다"가 결론이다.

**`image_ok` 의 검사 순서.** 크기 ≥ 12 → magic → `len > flash_size − 12` 이면 거부(이 형태는 빼기가 음수가 될 수 없어 넘침이 없다. `8 + len + 4 > flash_size` 로 쓰면 len = 0xFFFFFFF8 에서 32비트 덧셈이 넘쳐 통과할 수 있다) → CRC → **저장된 CRC 를 LE 로 읽어** 비교. 빌드 스크립트가 `struct.pack('<I', zlib.crc32(img))` 로 붙였다면 LE 다. BE 로 저장된 이미지는 거부되는 것을 테스트가 확인한다.

### 복잡도와 비용

| 항목 | 값 |
|---|---|
| 시간 | O(n): 워드당 DR 쓰기 1번 + (F4 ISO) RBIT 1번, 꼬리 ≤ 3바이트 SW |
| 메모리 | RAM 0 (표 없음), 스택 몇 워드 |
| HW 처리 시간 | 32비트 한 워드에 AHB 4클럭 정도 (대표값 — 계열별 RM 확인) |
| 비교: SW | 비트 단위 바이트당 ~8회 시프트, 테이블(1 KB) 바이트당 1회 조회 — ALGO-M02 |

Cortex-M 에서 반사·스왑 비용은 실제 컴파일러 출력으로 확인했다 (`demo_rbit.c`, Apple clang 21, `-O2 -S`):

| 함수 | Cortex-M4 (`thumbv7em`) | Cortex-M0 (`thumbv6m`) |
|---|---|---|
| `reflect` 루프 32비트 | `rbit r0, r0` 1개 (루프를 알아봄) | 명령 65개 (16회 펼친 루프 2바퀴 — RBIT 없음) |
| `__builtin_bitreverse32` | `rbit` 1개 | `rev` + 마스크/시프트 3단계, 20개 |
| `load_be32` (바이트 조립) | `ldr` + `rev` | `ldrb` 4개 + 시프트/덧셈 (11개, 비정렬 LDR 불가) |

그래서 M0/M0+ 계열(예: STM32F0/G0, 여기에도 CRC 유닛이 있다 — 계열별 확인)에서는 SW RBIT 대신 **REV_IN/REV_OUT 하드웨어 반사**를 쓰는 것이 핵심이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 49/49 passed, exit 0 (0.04 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_rbit.c -o demo_rbit && ./demo_rbit   # 맥에서 동작 확인
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S -o - demo_rbit.c       # Cortex-M4 asm
clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -O2 -S -o - demo_rbit.c       # Cortex-M0 asm
```

## 7. 자주 하는 실수

**1. 바이트 버퍼를 `*(uint32_t *)p` 로 읽어 그대로 DR 에.** 리틀 엔디언이라 워드 안 바이트가 거꾸로 들어간다. 구형 유닛 + LE 로드는 랜덤 1000개 중 992개가 MPEG-2 와 달랐다(나머지 8개는 빈 메시지). "1234" 가 MPEG-2("4321") 이 된다. 고친 코드: `load_be32()` (= `__REV(LDR)`) 또는 ISO 면 `__RBIT(LDR)`.

**2. 같은 포인터 캐스트가 M0 에서 HardFault.** `*(const uint32_t *)(p + 1)` 은 ARMv6-M 에서 비정렬 접근 폴트, M3/M4 에서도 `LDRD/LDM` 이나 UNALIGN_TRP 설정에 따라 폴트가 날 수 있고, C 로는 엄격한 별칭 규칙(strict aliasing) 위반이다. 바이트 조립(`load_le32`) 또는 `memcpy(&w, p, 4)` 를 쓰면 컴파일러가 가능한 칩에서는 `LDR` 하나로 바꿔 준다(위 asm 표). DMA 로 먹일 때는 소스 버퍼를 4바이트 정렬한다.

**3. REV_OUT 과 최종 XOR 를 섞어 생각한다.** REV_OUT 은 **비트 순서**, XOR 은 **값 반전**이다. 하나라도 빠지면 값이 다르다 — 5. 풀이 진단 표(651F2550 = XOR 빠짐, F55B0759 = REV_OUT 빠짐). STM32 에는 XOR 레지스터가 없으니 `~DR` 는 항상 SW 몫이다.

**4. REV_IN 을 "아무거나 반사"로 고른다.** LE 로드 + REV_IN=BYTE 는 "34 33 32 31 순서, 각 바이트 bit0 먼저"가 되어 틀린다(진단 표 E3C97CBC). 규칙: LE 32비트 로드면 WORD, BE 묶음이면 BYTE. HALF 는 16비트 LE 쓰기를 할 때만 의미가 있다.

**5. 꼬리를 0 으로 채운다.** 0 바이트도 CRC 에 들어가는 데이터라서 결과가 바뀐다 — 랜덤 1000개 중 **0개** 일치. "123456789" 는 0376E6E7 대신 AE24E09D. 고친 코드: 구형은 SW 로 이어 가기, 신형은 8/16비트 쓰기.

**6. 시작할 때 RESET 을 안 한다 / RCC 클럭을 안 켠다.** RESET 없이 쓰면 이전 계산의 상태에서 이어진다(이전 결과가 우연히 0xFFFFFFFF 가 아닌 한 틀림). 그리고 CRC 유닛도 주변장치라 RCC 에서 클럭(예: `__HAL_RCC_CRC_CLK_ENABLE()`)을 켜야 레지스터가 동작한다 — 안 켜면 DR 이 0 으로 읽히는 식으로 "아무 계산도 안 되는" 증상이 된다(정확한 증상은 칩마다 다름, 대표 사례).

**7. 저장된 CRC 의 엔디언을 확인하지 않는다.** 빌드 스크립트가 `'<I'`(LE) 로 붙였는데 BE 로 읽어 비교하면 모든 이미지가 실패한다. 테스트가 BE 로 저장된 이미지를 거부하는지 확인한다. 반대로 "아무 엔디언이나 통과"하게 짜면 안 된다 — 오류 검출 능력이 반으로 준다.

**8. len 범위 검사를 덧셈으로.** `if (8 + len + 4 > size)` 는 `len` 이 32비트 최대 근처일 때 넘쳐서 통과한다(`size_t` 가 32비트인 MCU). `len > size − 12` (size ≥ 12 를 먼저 확인)처럼 빼기로 쓴다. 테스트의 len = 0xFFFFFFF8 이 이 경우다.

## 8. 면접 Q&A

<details><summary>Q1. STM32 CRC 유닛 기본값이 zlib 의 crc32() 와 왜 다른가요?</summary>

다항식은 같은 0x04C11DB7 이지만 세 가지가 다르다. (1) **비트 순서**: 유닛은 비반사(MSB 먼저), zlib 은 반사형(바이트마다 bit0 먼저, 그래서 SW 에선 0xEDB88320 으로 오른쪽 시프트). (2) **출력 반사**: zlib 은 결과를 반사한 값. (3) **최종 XOR**: zlib 은 0xFFFFFFFF 를 XOR, 유닛엔 그 기능이 없다. 여기에 (4) LE 코어의 바이트 순서 문제가 겹친다. 그래서 기본 유닛 + BE 워드 = CRC-32/MPEG-2(0x0376E6E7), zlib = CRC-32/ISO-HDLC(0xCBF43926). 신형은 REV_IN=WORD(LE 로드) 또는 BYTE(BE 묶음) + REV_OUT + SW `~`, 구형은 RBIT 입력 + RBIT 출력 + `~`.

</details>

<details><summary>Q2. 반사형 CRC 는 왜 존재하나요? 하드웨어 입장에서 이상하지 않나요?</summary>

UART·HDLC·Ethernet 같은 직렬 링크는 바이트의 **LSB 를 먼저** 보낸다(COMM-B01). 직렬 하드웨어 CRC 는 선로에 나가는 순서대로 비트를 LFSR 에 넣으므로, 바이트 단위로 보면 "bit0 먼저" = 반사형이 된다. 결과 레지스터도 같은 방향으로 바이트에 담으니 refout 도 같이 켜진다(FCS 를 선로에 내보내는 비트 순서의 세부는 각 표준 확인). 소프트웨어로는 오른쪽 시프트 구현이 자연스러워 zlib 도 그 형태다. 메모리 덩어리를 다루는 칩 내부 유닛은 워드의 MSB 부터 넣는 게 자연스러워 비반사가 기본이 됐다. 같은 수학을 "어느 쪽 끝부터 읽느냐"의 차이일 뿐이다 — 5. 풀이의 성질 `lsb(data) = reflect(msb(rev8(data)))`.

</details>

<details><summary>Q3. F4 에서 길이가 4의 배수가 아니면 어떻게 하나요? 각 방법의 장단점은?</summary>

(1) 워드는 HW, 꼬리 0~3 바이트는 SW 로 이어서 — 표준 CRC 그대로, 비용은 최대 24비트 시프트. 상태 넘기기: MPEG-2 는 DR 그대로, ISO 는 RBIT(DR). (2) 0 이나 0xFF 로 채움 — 코드는 단순하지만 **표준이 아니다**. 장치 고유 프로토콜이라 상대(PC 도구)가 똑같이 채우기로 약속할 때만(연습 4). (3) 길이를 4의 배수로 강제(이미지를 빌드 때 4바이트 정렬로 패딩하고, 패딩까지 CRC 에 포함) — 플래시 이미지에서 흔한 실무 해법. 신형 칩이면 (4) 8/16비트 DR 쓰기로 끝.

</details>

<details><summary>Q4. CRC 유닛을 두 태스크(또는 태스크와 ISR)가 같이 쓰면?</summary>

유닛은 상태가 하나뿐인 공유 자원이다. 계산 도중 선점당해 다른 쪽이 RESET 하면 둘 다 틀린다. 해법: (1) 뮤텍스로 "RESET ~ 읽기" 전체를 감싼다 — ISR 에서는 못 쓰니 ISR 쪽은 SW CRC. (2) 문맥 저장/복원: 신형은 DR 을 읽어 두었다가 CRC_INIT 에 쓰고 RESET(REV_OUT 이 켜져 있으면 **반사를 되돌린 값**을 써야 한다 — 연습 5). 구형은 INIT 레지스터가 없지만, RESET 후 특정 워드 하나를 쓰면 원하는 상태로 갈 수 있다 — 32비트 워드 한 번의 변환이 가역이라서다(연습 3). (3) 짧은 계산이면 그냥 SW 로. 실무에선 (1) + "한 모듈만 CRC 유닛 소유"가 가장 단순하다.

</details>

<details><summary>Q5. DMA 로 CRC 를 먹일 수 있나요? 주의할 점은?</summary>

된다. DMA 를 메모리→메모리 모드로, 목적지 = CRC->DR (주소 고정, 증가 없음), 소스 = 버퍼(증가), 폭 32비트. CPU 는 그동안 다른 일을 하고, 끝나면 DR 을 읽는다. 주의: (1) 소스 정렬(32비트 전송이면 4바이트 정렬), (2) 바이트 순서 — DMA 는 메모리 워드를 LE 로 읽어 그대로 쓰므로 REV_IN 으로 해결해야 한다(CPU 처럼 중간에 REV 를 끼울 수 없다 → **LE + REV_IN=WORD** 가 DMA 용 조합). (3) 꼬리 바이트는 DMA 후 CPU 가 8/16비트 쓰기 또는 SW. (4) 구형 F4 는 RBIT 할 곳이 없으니 DMA 로는 MPEG-2 계열만 직접 나온다. (5) D-캐시가 있는 M7 이면 DMA 전에 캐시 clean(ARCH-M12 참고). DMA 채널/스트림이 메모리→메모리를 지원하는지는 계열별 확인.

</details>

<details><summary>Q6. HW 값이 SW 와 다르면 무엇부터 확인하나요?</summary>

순서: (1) RCC 클럭 켰나, 시작 때 RESET 했나. (2) `"123456789"` 로 SW 기준이 카탈로그 check 값을 내는지 — 기준부터 맞아야 비교가 의미 있다. (3) 결과만 보고 진단: `~hw`, `reflect(hw)`, `~reflect(hw)` 중 하나가 SW 와 같으면 XOR/REV_OUT 문제(5. 풀이 표). (4) 아니면 비트 순서: 워드 하나("1234")만 넣고 MPEG-2("1234")/MPEG-2("4321")/ISO 와 비교해 바이트 순서와 REV_IN 을 가린다. (5) 꼬리 처리, (6) INIT·POLYSIZE·POL 값(신형은 다른 드라이버가 바꿔 놓았을 수 있다). 디버거에서 레지스터 뷰로 CRC_CR 을 직접 읽어 보는 것이 빠르다.

</details>

<details><summary>Q7. 하드웨어 CRC 가 항상 소프트웨어보다 빠른가요?</summary>

긴 버퍼에서는 대개 그렇다(워드당 버스 쓰기 1번 수준). 하지만 짧은 메시지(몇 바이트)는 설정·RESET·읽기 오버헤드와 공유 자원 잠금 비용 때문에 테이블 SW 와 비슷하거나 느릴 수 있고, 캐시가 있는 M7 에서 slicing-by-8 SW 는 꽤 빠르다(ALGO-M02). 또 HW 는 다른 태스크와 공유해야 하고 계열마다 설정이 달라 이식성이 떨어진다. 그래서 실무 기준: 부팅 시 이미지 전체 검증·큰 블록 = HW(+DMA), 짧은 패킷 CRC·ISR 안 = SW 테이블. 수치는 반드시 해당 칩에서 DWT 사이클 카운터로 재 볼 것.

</details>

<details><summary>Q8. 이미지 끝에 CRC 를 붙였을 때 "전체 CRC 가 상수"라는 성질은 어디에 쓰나요?</summary>

CRC 를 LE 로 붙인 메시지 전체의 CRC-32/ISO-HDLC 는 데이터와 상관없이 **0x2144DF1C** 이고, XOR 전 레지스터(= 신형 유닛 DR, REV_OUT=1)는 **0xDEBB20E3** 이다(연습 2, 랜덤 1000개 확인). 그래서 검증 쪽은 "CRC 위치를 따로 읽어 비교"하지 않고 이미지+CRC 를 끝까지 DMA 로 흘려 넣은 뒤 상수와 한 번 비교하면 된다. Ethernet 수신기가 FCS 를 이렇게 검사한다(매직 넘버 / residue). 단, 저장 엔디언이 반사형에 맞는 LE 여야 성립한다.

</details>

## 9. 추가 연습

### 연습 1 — 카탈로그 CRC 를 신형 유닛 설정으로

신형 유닛(POLYSIZE 32/16/8/7, POL, INIT, REV_IN, REV_OUT) + SW XOR 로 아래 CRC 들의 check 값을 내는 설정 표를 만들어라: ISO-HDLC, MPEG-2, BZIP2, CRC-32/ISCSI(CRC-32C), CRC-16/MODBUS, CRC-16/IBM-3740(= "CCITT-FALSE"), CRC-16/XMODEM, CRC-8/SMBUS, CRC-7/MMC(SD 카드 명령 CRC). 바이트 쓰기만 쓴다.

<details><summary>답 보기</summary>

규칙 하나로 끝난다: **refin → REV_IN=BYTE (바이트 쓰기 기준), refout → REV_OUT=1, xorout → SW**, POL·INIT·width 는 그대로. 실제 출력:

```text
CRC-32/ISO-HDLC  POLYSIZE=32 POL=0x04C11DB7 INIT=0xFFFFFFFF REV_IN=BYTE REV_OUT=1 swXOR=0xFFFFFFFF  check=0xCBF43926
CRC-32/MPEG-2    POLYSIZE=32 POL=0x04C11DB7 INIT=0xFFFFFFFF REV_IN=NONE REV_OUT=0 swXOR=0x00000000  check=0x0376E6E7
CRC-32/BZIP2     POLYSIZE=32 POL=0x04C11DB7 INIT=0xFFFFFFFF REV_IN=NONE REV_OUT=0 swXOR=0xFFFFFFFF  check=0xFC891918
CRC-32/ISCSI     POLYSIZE=32 POL=0x1EDC6F41 INIT=0xFFFFFFFF REV_IN=BYTE REV_OUT=1 swXOR=0xFFFFFFFF  check=0xE3069283
CRC-16/MODBUS    POLYSIZE=16 POL=0x00008005 INIT=0x0000FFFF REV_IN=BYTE REV_OUT=1 swXOR=0x00000000  check=0x00004B37
CRC-16/IBM-3740  POLYSIZE=16 POL=0x00001021 INIT=0x0000FFFF REV_IN=NONE REV_OUT=0 swXOR=0x00000000  check=0x000029B1
CRC-16/XMODEM    POLYSIZE=16 POL=0x00001021 INIT=0x00000000 REV_IN=NONE REV_OUT=0 swXOR=0x00000000  check=0x000031C3
CRC-8/SMBUS      POLYSIZE= 8 POL=0x00000007 INIT=0x00000000 REV_IN=NONE REV_OUT=0 swXOR=0x00000000  check=0x000000F4
CRC-7/MMC        POLYSIZE= 7 POL=0x00000009 INIT=0x00000000 REV_IN=NONE REV_OUT=0 swXOR=0x00000000  check=0x00000075
```

아홉 개 모두 카탈로그 check 값과 같고, 일반 Rocksoft 모델 SW(`crc_generic`)와 랜덤 300개씩 대조했다. XMODEM 은 Python `binascii.crc_hqx(b'123456789', 0)` = 0x31c3 으로도 확인. MODBUS 의 바이트 순서(하위 바이트 먼저 전송)는 COMM-B13. POLYSIZE < 32 에서 INIT·REV_OUT 이 하위 비트만 쓰는 것은 mock 의 정의 — 칩 RM 확인.

</details>

### 연습 2 — 잔여값(residue)으로 한 번에 검증

메시지 뒤에 CRC-32/ISO-HDLC 를 LE 4바이트로 붙이면 전체의 CRC 가 데이터와 무관한 상수가 됨을 보이고, 신형 유닛에서 최종 XOR 없이 DR 을 바로 비교하는 상수를 구하라.

<details><summary>답 보기</summary>

데이터 뒤 반사형 레지스터를 R 이라 하면 CRC = ~R 이고, 이를 LE 4바이트로 붙이면 반사형에서는 그 4바이트가 레지스터의 하위 바이트부터 XOR 된다. 4바이트를 넣는 것은 `R' = G(R ⊕ v)` (G = 입력 0 으로 32번 시프트) 이므로 v = ~R 이면 R ⊕ ~R = 0xFFFFFFFF → `R' = G(0xFFFFFFFF)`: **데이터와 무관한 상수**다. 실측(랜덤 1000개, 길이 0~999):

```text
residue: 1000 msgs, crc(msg||crc_le) != 0x2144DF1C: 0, raw DR != 0xDEBB20E3: 0
```

CRC = 0x2144DF1C, XOR 전 레지스터(신형 DR, REV_IN=BYTE·REV_OUT=1) = 0xDEBB20E3 = ~0x2144DF1C. 부트로더는 "이미지+CRC 전체를 넣고 DR == 0xDEBB20E3" 한 줄로 검증할 수 있다.

</details>

### 연습 3 — INIT 레지스터가 없는 F4 에서 상태 복원

구형 유닛은 RESET 하면 0xFFFFFFFF 로만 간다. 계산 중 선점당해 저장한 상태 S 로 돌아가려면 RESET 후 어떤 워드 w 를 쓰면 되나? `f4_word_for_state(S)` 를 짜고, "A 계산 절반 → 저장 → ISR 이 B 계산 → 복원 → A 마저" 시나리오를 테스트하라.

<details><summary>답 보기</summary>

32비트 워드 하나를 쓰면 `state' = G(state ⊕ w)`, G = "입력 0 으로 32번 시프트". 한 번의 시프트는 되돌릴 수 있다: 시프트 후 bit0 은 "다항식을 XOR 했는가"와 같다(POL 의 bit0 = 1, 시프트한 값의 bit0 = 0). 그래서

```c
s = (s & 1u) ? ((s ^ 0x04C11DB7u) >> 1) | 0x80000000u : s >> 1;   /* 32번 */
```

로 G⁻¹ 을 구하고 `w = 0xFFFFFFFF ^ G⁻¹(S)`. 랜덤 목표 1000개 모두 복원, 문맥 전환 시나리오에서 A·B 모두 SW 와 같다. 리셋 상태(0xFFFFFFFF)로 가는 워드도 0 이 아니다 — 0 을 쓰면 상태가 G(0xFFFFFFFF) 로 바뀐다.

</details>

### 연습 4 — 장치의 "네이티브" CRC 를 PC 에서 재현

이미 출시된 장치가 구형 유닛에 `LDR` 로 읽은 워드를 그대로, 꼬리는 0 으로 채워 넣는 CRC 를 쓴다(고칠 수 없다). PC 도구가 같은 값을 내도록 `stm32_native_crc(p, n)` 을 순수 SW 로 짜라.

<details><summary>답 보기</summary>

4바이트 묶음마다 `[b3, b2, b1, b0]` 순서로(꼬리는 0 채움) MPEG-2 상태 갱신을 이어 가면 된다. "123456789" → **0xAFF19057** (MPEG-2 도 ISO 도 아님), 랜덤 1000개(길이 0~299)에서 mock 장치와 일치. 프로토콜 문서에는 "CRC-32/MPEG-2 over little-endian 32-bit words, zero-padded" 처럼 **알고리즘 이름 + 바이트 순서 + 패딩**을 같이 적어야 한다.

</details>

### 연습 5 — 신형 유닛 문맥 저장/복원과 REV_OUT 함정

REV_OUT=1 인 상태로 계산 도중 DR 을 저장했다가, 다른 계산 후 CRC_INIT 에 써서 RESET 으로 복원하라. 원래 INIT 도 되돌려야 한다.

<details><summary>답 보기</summary>

DR 은 반사되어 보이는 값이므로 INIT 에는 `reflect(DR, polysize)` 를 써야 한다. 복원 후 INIT 을 원래 값(0xFFFFFFFF)으로 되돌리지 않으면 다음 새 계산이 틀린다. 실측: 500회 랜덤(자르는 위치 랜덤, 중간에 다른 37바이트 계산) bad=0. 반사된 DR 을 그대로 INIT 에 쓰는 잘못된 복원은 "123456789" 에서 0xCBF43926 이 나오지 않는다. (mock 은 "INIT 쓰기 후 RESET 해야 상태에 반영"으로 정의 — 실제 칩에서 INIT 쓰기가 DR 에 바로 반영되는지는 RM 확인.)

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 20/20 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 오류 없음
```

## 10. 핵심 정리

- CRC 이름은 6개 매개변수로 부른다. ISO-HDLC(zlib) = 반사·XOR 0xFFFFFFFF, check 0xCBF43926. MPEG-2(STM32 기본) = 비반사·XOR 없음, check 0x0376E6E7. 다항식은 같다.
- CRC 유닛은 "쓴 값을 MSB 먼저 LFSR" 이 전부. 최종 XOR 레지스터는 없다 → `~` 는 SW.
- LE 코어 + `LDR` = 바이트 3 이 먼저 들어감 → MPEG-2 는 BE 로드(REV), ISO 는 LE+REV_IN=WORD(F4: RBIT) 또는 BE+REV_IN=BYTE. 64가지 중 맞는 건 정확히 이 3개(실측).
- 결과 진단: `~hw`, `reflect(hw)`, `~reflect(hw)` 가 SW 와 같으면 XOR/REV_OUT 문제, 아니면 비트 순서·INIT.
- 꼬리: F4 는 DR → SW 상태(ISO 는 RBIT) 로 이어 가기, 신형은 16/8비트 쓰기(REV_IN=BYTE + BE 묶음이면 폭 무관). 0 채움은 다른 CRC(실측 0/1000).
- 부트로더: len 검사는 빼기로, 저장 CRC 엔디언 확인, 또는 residue 0xDEBB20E3(DR) / 0x2144DF1C 로 한 번에.
- 공유 자원: 뮤텍스 또는 저장/복원(신형 INIT ← reflect(DR), F4 는 역산 워드). M0 에는 RBIT 가 없다(65 vs 1 명령) → HW 반사 사용.

## 참고자료

- Greg Cook, *Catalogue of parametrised CRC algorithms* (RevEng) — CRC-32/ISO-HDLC, MPEG-2, BZIP2, ISCSI, CRC-16/MODBUS, IBM-3740, XMODEM, CRC-8/SMBUS, CRC-7/MMC 매개변수·check·residue: <https://reveng.sourceforge.io/crc-catalogue/all.htm> (이 레슨의 check 값 9개는 직접 계산으로 재현)
- Ross N. Williams, *A Painless Guide to CRC Error Detection Algorithms* (1993) — Rocksoft 모델(refin/refout/xorout)의 원전.
- STMicroelectronics, AN4187 *Using the CRC peripheral in the STM32 family* — 계열별 CRC 유닛 기능 비교(고정 다항식 / 프로그래머블, REV_IN/REV_OUT). 내용은 기억 기반 — 확인 권장.
- STMicroelectronics RM0090 (STM32F405/407 등, CRC 장: 고정 0x04C11DB7, 32비트 DR, CR.RESET), RM0351 (STM32L4, CRC 장: CRC_INIT, CRC_POL, POLYSIZE, REV_IN, REV_OUT, 8/16/32비트 DR 접근) — 레지스터 비트 위치·좁은 쓰기 동작·처리 클럭은 이 문서들로 확인할 것.
- STM32Cube HAL `stm32xxxx_hal_crc.c` / `_ex.c` — `HAL_CRC_Calculate`, `CRC_Handle_8/16`, `HAL_CRCEx_Input_Data_Reverse` (바이트 입력을 BE 로 묶어 쓰고 꼬리는 좁은 쓰기 — 기억 기반, 소스 확인 권장).
- ARM, *ARMv7-M Architecture Reference Manual* — `RBIT`, `REV`, 비정렬 접근과 UNALIGN_TRP. *ARMv6-M ARM* — RBIT 없음, 비정렬 접근 폴트. CMSIS `__RBIT`, `__REV`.
- zlib `crc32.c` / Python `zlib.crc32`, `binascii.crc_hqx` — 이 레슨에서 기준값 확인에 사용.
- P. Koopman, *CRC Polynomial Zoo* / "32-Bit Cyclic Redundancy Codes for Internet Applications" (DSN 2002) — 다항식별 검출력(HD) 비교, CRC-32C 선택 배경.
- 관련 레슨: ALGO-M01 (비트 단위 CRC 원리), ALGO-M02 (테이블/slicing CRC), ALGO-B14 (덧셈 체크섬), CPP-M13 (constexpr CRC 테이블), COMM-B01 (UART LSB 먼저), COMM-B08 (CRC vs 체크섬 검출 실험), COMM-B10 (길이+CRC16 프레임), COMM-B13 (Modbus CRC-16), C-M05 (엔디언 독립 직렬화), C-B01 (`1u << 32` UB), ARCH-M12 (DMA 와 캐시 clean/invalidate), BM-A04 (부트 이미지 CRC)
