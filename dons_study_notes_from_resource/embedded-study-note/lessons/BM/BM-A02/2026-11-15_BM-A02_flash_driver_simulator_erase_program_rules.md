---
id: BM-A02
time: 120분
updated: 2026-11-15
---
# BM-A02 플래시 드라이버 시뮬레이터: 섹터 erase/program 규칙

## 학습 목표

- 내장 NOR 플래시의 세 규칙 — **program 은 1→0 만 (AND)**, **erase 는 섹터 통째로 0xFF**, **쓰기 단위(prog_unit) 정렬** — 을 코드로 된 모델(`flash_t`)로 옮기고, 바이트 쌍 65 536 개 전수 테스트로 증명한다. 이 모델은 BM-A05(YMODEM 부트로더)·BM-A06(전원 차단 내성 업데이트)이 축약판으로 그대로 가져다 쓰는 **공용 어휘**다.
- 드라이버 `drv_write` 를 짠다: 쓰기 전 `(old & new) == new` 사전검사(전부 아니면 전무), 비정렬 head/tail 의 **read-merge**, 섹터 경계 넘기, 단위마다 read-back verify. `drv_erase_range` 의 정렬·확장 규칙(앞은 거절, 뒤는 섹터 끝까지)을 말로 방어할 수 있다.
- 전원 차단을 **모든 flash op 지점**에 주입하고(torn erase 4 단계, torn program), 부팅 때 CRC·blank check 로 **잡히는 것과 원리적으로 못 잡히는 것**(약하게 지워진 셀, weak erase)을 구분한다. "blank 이면 erase 생략" 최적화가 왜 위험한지 숫자로 보인다.
- 섹터별 erase 카운터로 **로그 구조 append vs 제자리 재작성**의 wear 를 비교한다 (10 000 회 갱신: 10 000 vs 79). SSD FTL 의 out-of-place write·wear leveling 과 같은 이야기임을 연결한다.

## 1. 문제

"플래시에 erase 없이 두 번 쓰면?", "1 바이트만 바꾸려면?", "erase 중에 전원이 나가면 부팅 때 어떻게 아나?" — 부트로더·설정 저장·로그를 맡는 펌웨어 면접의 단골이고, bring-up 에서는 "verify 는 통과했는데 한 달 뒤 설정이 깨졌다" 가 이 규칙들을 반쯤만 지킨 결과다. Don 이 SSD 펌웨어에서 다룬 NAND 의 program/erase·전원 차단 보호(PLP)·FTL 메타데이터와 뿌리가 같다 — 여기서는 MCU **내장** 플래시(STM32F4 류)를 모델로 삼는다. 외부 SPI NOR 의 명령 프로토콜(WREN/WEL/페이지 랩)은 COMM-A06 에서 했으니 다시 하지 않는다.

이 레슨의 플래시는 **레지스터 없는 동작 모델**이다. 숫자·동작은 STM32F4 (RM0090) 를 본떴지만 테스트의 정답은 모델의 정의다.

| 모델 정의 | 내용 |
|---|---|
| 지워진 상태 | 0xFF. `flash_erase_sector(f, s)` = 섹터 s 전체 0xFF (섹터 번호로 지정 — F4 의 FLASH_CR.SNB 와 같은 모양) |
| program | `mem &= data` — 1→0 만 가능. 지우지 않고 두 번 쓰면 **에러 없이 AND** (raw 모델) |
| 쓰기 단위 | `prog_unit` 1/4/8 B. 주소·길이가 단위 배수가 아니면 `FLASH_ERR_ALIGN` (F4 의 PSIZE x8/x32, L4 류 8 B double-word 를 흉내) |
| 섹터 표 | 균일(`sector_size`) 또는 F4 식 불균일 `sizes[]` = 16/16/16/16/64/128×7 KB = 1 MB (RM0090 Table 5 — 웹 사본으로 확인, 원본 확인 권장) |
| `one_shot` | ECC 식 플래시: 단위가 **전부 0xFF 일 때만** program 가능, 아니면 `FLASH_ERR_NOT_ERASED` (STM32L4/G4 의 PROGERR 류 — 확인 권장) |
| 전원 차단 | `op_index` 가 erase/program 호출마다 +1, `== cut_at` 인 op 는 **찢어지고** `FLASH_POWER_CUT`. 그 뒤 모든 op 는 아무 일 없이 `FLASH_POWER_CUT`, `flash_power_on()` 으로 "재부팅" |
| torn erase | rng 로 단계 선택: 0 초반(일부 비트만 1), 1 중반, **2 후반(전부 0xFF 로 읽히지만 일부 비트는 `weak`)**, 3 완료(CPU 만 EOP 를 못 봄) |
| torn program | 그 op 의 단위 중 하나에서 **필요한 1→0 전이의 무작위 부분집합**만 일어남. 앞 단위는 완료, 뒤 단위는 미기록 |
| `flash_age()` | 시간이 흐름: weak 비트가 0 으로 떨어진다 (retention 손실의 모델) |
| 고장 셀 | `stuck_off/mask/level`: 1 고착(program 이 못 내림) 또는 0 고착(erase 가 못 올림) |
| 에러 코드 | `FLASH_OK, ERR_ALIGN, ERR_RANGE, ERR_NOT_ERASED, ERR_VERIFY, POWER_CUT` |

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">STM32F4 1 MB 섹터 지도 (폭은 비례 아님) 와 drv_erase_range(0x0800C000, 0x5000)</text>
  <rect x="20" y="40" width="40" height="40" class="f-mut" stroke="currentColor"/><text x="40" y="58" fill="currentColor" text-anchor="middle">S0</text><text x="40" y="73" fill="currentColor" text-anchor="middle">16K</text>
  <rect x="60" y="40" width="40" height="40" class="f-mut" stroke="currentColor"/><text x="80" y="58" fill="currentColor" text-anchor="middle">S1</text><text x="80" y="73" fill="currentColor" text-anchor="middle">16K</text>
  <rect x="100" y="40" width="40" height="40" class="f-mut" stroke="currentColor"/><text x="120" y="58" fill="currentColor" text-anchor="middle">S2</text><text x="120" y="73" fill="currentColor" text-anchor="middle">16K</text>
  <rect x="140" y="40" width="40" height="40" class="f-acc" stroke="currentColor"/><text x="160" y="58" fill="currentColor" text-anchor="middle">S3</text><text x="160" y="73" fill="currentColor" text-anchor="middle">16K</text>
  <rect x="180" y="40" width="80" height="40" class="f-acc" stroke="currentColor"/><text x="220" y="58" fill="currentColor" text-anchor="middle">S4</text><text x="220" y="73" fill="currentColor" text-anchor="middle">64K</text>
  <rect x="260" y="40" width="68" height="40" class="f-mut" stroke="currentColor"/><text x="294" y="58" fill="currentColor" text-anchor="middle">S5</text><text x="294" y="73" fill="currentColor" text-anchor="middle">128K</text>
  <rect x="328" y="40" width="68" height="40" class="f-mut" stroke="currentColor"/><text x="362" y="65" fill="currentColor" text-anchor="middle">S6</text>
  <rect x="396" y="40" width="68" height="40" class="f-mut" stroke="currentColor"/><text x="430" y="65" fill="currentColor" text-anchor="middle">S7</text>
  <rect x="464" y="40" width="68" height="40" class="f-mut" stroke="currentColor"/><text x="498" y="65" fill="currentColor" text-anchor="middle">S8</text>
  <rect x="532" y="40" width="68" height="40" class="f-mut" stroke="currentColor"/><text x="566" y="65" fill="currentColor" text-anchor="middle">S9</text>
  <rect x="600" y="40" width="68" height="40" class="f-mut" stroke="currentColor"/><text x="634" y="65" fill="currentColor" text-anchor="middle">S10</text>
  <rect x="668" y="40" width="68" height="40" class="f-mut" stroke="currentColor"/><text x="702" y="65" fill="currentColor" text-anchor="middle">S11</text>
  <text x="20" y="96" fill="currentColor" font-family="monospace">0x08000000</text>
  <text x="140" y="110" fill="currentColor" font-family="monospace">0x0800C000</text>
  <text x="200" y="96" fill="currentColor" font-family="monospace">0x08010000</text>
  <text x="262" y="110" fill="currentColor" font-family="monospace">0x08020000</text>
  <text x="736" y="96" fill="currentColor" font-family="monospace" text-anchor="end">0x08100000</text>
  <path d="M140 130 H220" stroke="currentColor" stroke-width="3"/>
  <path d="M220 124 L228 130 L220 136 Z" fill="currentColor"/>
  <text x="140" y="148" fill="currentColor">요청: 0x5000 (20 KB)</text>
  <path d="M140 160 H260" stroke="currentColor" stroke-width="3" stroke-dasharray="6 3"/>
  <text x="140" y="176" fill="currentColor">실제로 지워짐: S3 + S4 = 80 KB (끝은 섹터 끝까지 확장)</text>
  <text x="430" y="130" fill="currentColor">drv_erase_range(0x0800C004, 4) → ERR_ALIGN</text>
  <text x="430" y="146" fill="currentColor">(앞으로 확장하면 S3 의 0x0800C000..C003 을</text>
  <text x="430" y="162" fill="currentColor"> 호출자가 모르게 지운다 → 거절)</text>
  <rect x="20" y="196" width="720" height="62" class="f-mut" stroke="currentColor" opacity="0.5"/>
  <text x="32" y="216" fill="currentColor" font-family="monospace">program = AND : 0xFF → 0x55 → (0xAA 쓰기) → 0x00   raw 모델은 에러 없음</text>
  <text x="32" y="234" fill="currentColor" font-family="monospace">drv_write     : 0x55 위에 0xAA → ERR_NOT_ERASED (0x55 &amp; 0xAA != 0xAA), 0 op</text>
  <text x="32" y="252" fill="currentColor" font-family="monospace">drv_write     : 0x55 위에 0x54 → OK  (1→0 만 필요: "상태 플래그 내리기")</text>
</svg>
```

### Part A — 모델과 드라이버 (핵심 메커니즘)

- **A1** `flash_program` (AND, 정렬·범위, `one_shot`, torn program), **A2** `flash_erase_sector` (wear 카운터, torn erase, 0 고착 셀).
- **A3** `drv_write(f, addr, buf, len)`: 감싸는 단위 [u_first, u_end) 를 구하고 (1) 사전검사 — 내 바이트마다 `(old & new) == new`, `one_shot` 이면 단위 전체 0xFF, 하나라도 어기면 **아무것도 쓰지 않고** `ERR_NOT_ERASED`; (2) 단위마다 현재 내용 읽기 → 내 바이트 덮기(read-merge) → program → 다시 읽어 비교(`ERR_VERIFY`).
- **A4** `drv_erase_range(f, addr, len)`: len 0 → OK, 범위 → `ERR_RANGE`, **시작은 섹터 경계 필수**(`ERR_ALIGN`), 끝은 `addr+len−1` 이 든 섹터 끝까지, 섹터마다 erase → blank verify.
- 테스트: F4 섹터 번호 1 M 주소 전수, AND 바이트 쌍 65 536 전수(raw), 드라이버 사전검사 65 536 전수(거절 58 975 = 65 536 − 3⁸), 비정렬 head/tail 전수(unit 1/4/8 × off 0..15 × len 0..40 = 1968 경우, 이웃 바이트 0xFF 유지 + program 횟수), 같은 단위를 나눠 쓰는 두 레코드, F4 섹터 경계 3 곳(16K→16K, 16K→64K, 64K→128K), erase 확장/거절, 무작위 4000 연산 vs 기준 배열.

### Part B — 고장 주입 (면접관이 찌르는 곳)

| 고장 | 주입 | 기대 결과 (모두 테스트) |
|---|---|---|
| erase 없이 재프로그램 | raw 0x55 → 0xAA | 읽으면 0x00, 에러 없음 |
| 드라이버로 같은 짓 | `drv_write` 0xAA | `ERR_NOT_ERASED`, 0x55 그대로. 0x54 는 OK |
| 64 B 중 마지막 1 B 만 충돌 | 0x0F 위에 0xF0 | `ERR_NOT_ERASED`, program 0 회, 앞 63 B 여전히 blank |
| ECC 식 1회 프로그램 | `one_shot`, unit 8, 5 B + 6 B 이웃 레코드 | 두 번째 `ERR_NOT_ERASED` (단위 공유), 다음 단위 경계부터는 OK, raw 모델도 거절 |
| 닳은 셀 | 1 고착 / 0 고착 | program verify `ERR_VERIFY` / erase verify `ERR_VERIFY` |
| 죽은 뒤의 op | `cut_at = 2` | 뒤의 erase/program 전부 `POWER_CUT`, 아무 변화 없음, `power_on` 뒤 정상 |
| torn program 성질 | seed 2000 | 앞 단위 완료, 뒤 단위 미기록, 찢어진 단위는 0→1 없음, 중간 상태 단위 ≤ 1 |
| **각본 시퀀스 전수 차단** | 18 op × seed 64 = 1152 | 아래 |

각본: 4 × 256 B, unit 4. 섹터 1·2 준비(erase 2 op) → 레코드 A (payload 16 + CRC-32 4 = 20 B @0x080001FA, **섹터 1→2 경계를 넘음**, 6 단위) → 레코드 B (33 + 4 = 37 B @0x0800022C, 10 단위) = **N = 18 op**. 섹터 1·2 엔 옛 데이터가 있다. 각 cut 지점 k = 1..18 × seed 64 개마다: 실행 → 재부팅 → 부팅 검사(`rec_valid` = CRC) → `boot_recover` → `flash_age` → 두 레코드가 유효한가. 복구 정책 두 개를 비교한다: **ALWAYS_ERASE**(항상 지우고 다시) vs **SKIP_IF_BLANK**(blank check 통과한 섹터는 erase 생략).

### Part C — 펌웨어 현실: wear 와 로그 구조

- 같은 24 B 값을 10 000 번 갱신. naive = 매번 섹터 0 erase + 같은 자리 쓰기. 로그 = 4 × 1 KB 원형, 32 B 레코드 `seq | data[24] | crc32` 를 다음 빈 슬롯에 append, 섹터가 차면 다음 섹터를 지우고 이동.
- **C1** `log_mount` (부팅 스캔: 유효 레코드 중 seq 최대 + 쓰기 위치 = 그 섹터의 **마지막 비-blank 슬롯 다음**), **C2** `log_append`.
- 테스트: erase 카운터 정확값(naive 10 000 / 로그 316 = format 4 + 회전 312, 섹터당 79), 재마운트, 빈/쓰레기 플래시 마운트 실패, **전원 차단 300 회** (마운트 = 마지막 ack, 이어서 새 내용 100 개 append 오류 없음).

## 2. Problem (English)

> **Internal-flash driver on a behavioural flash model.**
>
> The flash erases in sectors (uniform, or STM32F4-style 16/16/16/16/64/128×7 KiB) to 0xFF; programming can only clear bits (`mem &= data`) and must be aligned to `prog_unit` (1, 4 or 8 bytes). Implement the model and a driver on top of it:
>
> ```c
> flash_rc_t flash_erase_sector(flash_t *f, uint32_t sector);
> flash_rc_t flash_program(flash_t *f, uint32_t addr, const uint8_t *src, uint32_t len);   /* raw: AND */
> void       flash_read(const flash_t *f, uint32_t addr, uint8_t *dst, uint32_t len);
> flash_rc_t drv_write(flash_t *f, uint32_t addr, const uint8_t *buf, uint32_t len);       /* any alignment */
> flash_rc_t drv_erase_range(flash_t *f, uint32_t addr, uint32_t len);
> ```
>
> Requirements: `drv_write` accepts any address/length, rejects the whole request with `FLASH_ERR_NOT_ERASED` **before programming anything** if any byte would need a 0→1 transition, merges partial head/tail units without disturbing neighbouring bytes, and verifies every unit (`FLASH_ERR_VERIFY`). `drv_erase_range` must start on a sector boundary (`FLASH_ERR_ALIGN`) and erases up to the end of the sector holding the last byte, blank-checking each sector. Every erase/program call is a potential power-cut point (`cut_at`); after a cut every call returns `FLASH_POWER_CUT` until `flash_power_on()`.
>
> Example: on a 4-byte-unit flash, `drv_write(f, 0x080001FA, buf, 20)` issues 6 program operations (units 0x1F8, 0x1FC, 0x200, 0x204, 0x208, 0x20C); bytes 0x1F8–0x1F9 and 0x20E–0x20F keep their old values; the write crosses from sector 1 into sector 2 without any special case.
>
> Follow-ups: What can a boot-time check detect after a power cut during erase, and what can it not? Why is "skip the erase if the sector already reads blank" unsafe? How does wear change if you append records instead of rewriting in place?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "53/107 passed", exit 1 — TODO 8 곳을 채워 107/107
```

TODO 8 곳: A1 `flash_program`, A2 `flash_erase_sector`, A3 `drv_write`, A4 `drv_erase_range`, B1 `rec_valid`, B2 `boot_recover`, C1 `log_mount`, C2 `log_append`. 모델의 고장 장치(`tear_erase`, `tear_program`, `flash_dead`, `flash_tick`, `stuck_bits`, `flash_age`), 지오메트리 함수, CRC-32, 각본 `store_records`/`prepare_sector`, `log_format`, `naive_update`, 하네스는 주어진다. 스텁 상태에서도 모든 루프는 유한하다 (`flash_t` 가 2 MB 넘어서 정적 하나를 재사용한다).

관찰용 파일 (레슨 폴더에서 빌드 — `#include "solution.c"` 로 같은 모델을 쓴다):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_torn.c -o demo_torn && ./demo_torn   # torn erase 단계별 hexdump, 10 000 회 통계, 512 seed 캠페인
cc -std=c11 -Wall -Wextra -Werror -O2 demo_wear.c -o demo_wear && ./demo_wear   # 100 000 회 갱신 wear 표 + F4 시간 추정
```

## 4. 힌트

<details>
<summary>힌트 1 — A1/A2: 순서가 곧 의미다</summary>

전원 검사(`flash_dead`) → 인자 검사(RANGE/ALIGN/one_shot) → `flash_tick` → 실제 동작. 인자 오류는 칩에 명령이 안 간 것이므로 op 로 세지 않는다 (테스트 A-3 이 `op_index == 0` 을 본다). program 은 단위 루프 안에서 `k == cut_unit` 이면 `tear_program` 후 바로 반환 — 그 뒤 단위는 손대지 않는다.

</details>

<details>
<summary>힌트 2 — A3: 왜 두 번 도는가</summary>

검사와 쓰기를 한 루프에서 하면, 마지막 바이트에서 충돌을 발견했을 때 앞 단위는 이미 써 버렸다 — 지울 수도 없다(erase 는 섹터 단위). 그래서 **검사 루프 전체 → 쓰기 루프 전체**. 감싸는 단위: `u_first = off − off % u`, `u_end = ⌈(off+len)/u⌉·u`.

</details>

<details>
<summary>힌트 3 — A3: merge 의 두 방법</summary>

(a) 현재 내용을 읽어 내 바이트만 덮는다(read-merge), (b) 0xFF 로 채우고 내 바이트만 넣는다. AND 플래시에선 둘 다 이웃을 바꾸지 않는다 (x & 0xFF = x, x & x = x). 단, (b) 로 쓰고 verify 를 "읽은 값 == 보낸 버퍼" 로 하면 이웃에 데이터가 있을 때 거짓 VERIFY 가 난다. 0x00 으로 채우면? 이웃을 파괴한다.

</details>

<details>
<summary>힌트 4 — B1/B2: CRC 가 판정, 정책은 인자</summary>

`rec_valid` 는 payload n B 와 CRC 4 B(LE)를 읽어 비교만 한다. `boot_recover` 는 둘 다 유효하면 손대지 않고, 하나라도 아니면 `store_records(f, pol)` 로 처음부터 — 레코드 둘이 섹터 2 를 공유하므로 "B 만 다시" 는 불가능하다(B 자리가 이미 반쯤 쓰였을 수 있다).

</details>

<details>
<summary>힌트 5 — C1: 쓰기 위치는 "최신 유효 다음" 이 아니다</summary>

찢어진 레코드는 CRC 로 무시되지만, 그 슬롯은 0xFF 가 아니다. 최신 유효 슬롯 바로 다음에 쓰면 거기가 찢어진 슬롯이라 **다른 내용**을 쓰는 순간 `NOT_ERASED`. 섹터 끝에서부터 거꾸로 훑어 처음 만나는 비-blank 슬롯 다음을 쓰기 위치로 잡는다 (COMM-A06 의 blank check 와 같은 생각).

</details>

## 5. 풀이

### Part A — 왜 1→0 만 되는가, 그리고 F4 에선 어떻게 보이나

플로팅 게이트(또는 charge-trap) 셀은 전하가 **있으면 0, 없으면 1** 로 읽힌다. program 은 셀 하나하나에 전하를 넣고(빠르다, 비트 단위로 선택 가능), erase 는 고전압 터널링으로 전하를 빼는데 웰(well)을 공유하는 **블록 전체**에 걸린다(느리다). 그래서 "비트를 1 로" 는 섹터 erase 로만 된다. 이 비대칭이 모든 규칙의 뿌리다:

| 규칙 | 결과 | 모델 |
|---|---|---|
| program 은 전하 추가만 | 1→0 만, 같은 자리 두 번 쓰면 AND | `mem &= data` |
| erase 는 블록 단위 | 1 바이트 수정 = 섹터 전체 RMW | `flash_erase_sector(s)` |
| program 은 병렬 폭 단위 | PSIZE/ECC 워드 정렬 | `prog_unit`, `ERR_ALIGN` |
| 셀은 닳는다 | erase 횟수 한도 (F4 10 kcycles 대표값 — 확인 권장) | `erase_count[]` |
| 전하는 천천히 샌다 | 약하게 지워진/쓰인 셀은 나중에 뒤집힘 | `weak[]`, `flash_age()` |

RM0090 은 "successive write operations are possible without the need of an erase operation when changing bits from '1' to '0'. Writing '1' requires a flash memory erase operation." 라고 쓴다 (웹 사본에서 확인, 원본 확인 권장). 즉 F4 자체는 재프로그램을 막지 않는다 — **막는 건 드라이버 몫**이다. 실제 F4 시퀀스(대표값, RM0090 확인 권장)와 모델의 대응:

| F4 하드웨어 | 이 모델 |
|---|---|
| FLASH_KEYR ← 0x45670123, 0xCDEF89AB (CR 잠금 해제), 끝나면 CR.LOCK(bit31) | 생략 (잠금은 BM-A05/A06 의 부트로더가 다룸) |
| CR.PSIZE[9:8] = x8/x16/x32/x64 (전압 범위에 따라 허용 폭이 다름) | `prog_unit` 1/4/8 |
| CR.SER(bit1) + SNB[6:3] = 섹터 번호 + STRT(bit16) → SR.BSY(bit16) 대기 | `flash_erase_sector(f, s)` |
| CR.PG(bit0) 후 일반 store 로 쓰기 → BSY 대기 | `flash_program` |
| SR.EOP(0) OPERR(1) WRPERR(4) PGAERR(5) PGPERR(6) PGSERR(7) | `ERR_ALIGN` (PGAERR/PGPERR 류), 나머지는 범위 밖 |
| erase/program 중 플래시 읽기 → **버스 stall** ("code or data fetches cannot be performed while a write/erase operation is ongoing") | 모델엔 시간 없음 → Q6 |

**A3 의 추적** — 레코드 A: `drv_write(f, 0x080001FA, buf, 20)`, unit 4, 섹터 1 = 0x08000100–0x080001FF, 섹터 2 = 0x08000200–.

```svg
<svg viewBox="0 0 760 230" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">drv_write(0x080001FA, 20 B), prog_unit 4 → 6 단위, 섹터 경계는 그냥 지나간다</text>
  <text x="10" y="58" fill="currentColor">단위</text>
  <rect x="60" y="40" width="100" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="160" y="40" width="100" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="260" y="40" width="100" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="360" y="40" width="100" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="460" y="40" width="100" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="560" y="40" width="100" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="110" y="40" width="50" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="560" y="40" width="50" height="28" class="f-ok" stroke="currentColor"/>
  <text x="110" y="34" fill="currentColor" text-anchor="middle" font-family="monospace">1F8</text>
  <text x="210" y="34" fill="currentColor" text-anchor="middle" font-family="monospace">1FC</text>
  <text x="310" y="34" fill="currentColor" text-anchor="middle" font-family="monospace">200</text>
  <text x="410" y="34" fill="currentColor" text-anchor="middle" font-family="monospace">204</text>
  <text x="510" y="34" fill="currentColor" text-anchor="middle" font-family="monospace">208</text>
  <text x="610" y="34" fill="currentColor" text-anchor="middle" font-family="monospace">20C</text>
  <text x="85" y="59" fill="currentColor" text-anchor="middle">old old</text>
  <text x="135" y="59" fill="currentColor" text-anchor="middle">d0 d1</text>
  <text x="585" y="59" fill="currentColor" text-anchor="middle">d18 d19</text>
  <text x="635" y="59" fill="currentColor" text-anchor="middle">old old</text>
  <path d="M260 32 V100" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3"/>
  <text x="264" y="96" fill="currentColor">섹터 1 | 섹터 2 (0x08000200)</text>
  <text x="60" y="126" fill="currentColor">① 사전검사: 내 바이트 20 개 (old &amp; new) == new (one_shot: 24 B 전부 0xFF) — 실패 시 0 op</text>
  <text x="60" y="146" fill="currentColor">② head 단위 1F8: 읽기 [o0 o1 FF FF] → [o0 o1 d0 d1] 로 덮기 → program → 읽어 비교</text>
  <text x="60" y="166" fill="currentColor">③ 가운데 4 단위: 덮을 것 없이 d2..d17 그대로 → program → 비교</text>
  <text x="60" y="186" fill="currentColor">④ tail 단위 20C: [d18 d19 o2 o3] → program → 비교.  총 6 회 = ⌈0x20E/4⌉ − ⌊0x1FA/4⌋</text>
  <text x="60" y="210" fill="currentColor">흐린 칸 = 남의 바이트(read-merge 로 보존), 초록 = 이번 쓰기. old 위 old 재기록 = 변화 없음</text>
</svg>
```

**read-merge 가 "합법" 인 조건.** AND 플래시에서 남의 바이트를 그 값 그대로 다시 쓰는 건 물리적으로 아무 전이도 일으키지 않는다. 그래도 "단위의 손대지 않는 바이트가 0xFF 로 남아 있을 때만" 이 원칙인 이유는 셋이다. (1) ECC 플래시(L4/G4/H7 류, 확인 권장)는 단위마다 ECC 비트를 함께 쓰므로 **단위당 program 1 회**만 된다 — 이미 쓰인 단위는 `NOT_ERASED`(테스트 B-4). (2) ECC 가 없어도 같은 워드를 여러 번 쓰는 것에 횟수 제한을 두는 칩이 있다 (nRF52 의 n_WRITE 류 — 확인 권장). (3) 전원 차단 중에 그 단위가 찢어지면 이웃 레코드와 운명을 같이 한다. 그래서 실전 레코드 포맷은 **prog_unit 배수로 패딩**해 단위를 공유하지 않게 만든다 (연습 4).

**A4 의 확장 규칙.** 끝 쪽 확장은 "이 슬롯에 len 바이트짜리 이미지를 넣을 자리를 만들어라" 라는 흔한 의도와 맞다 — 하지만 F4 에선 그게 크다: `drv_erase_range(0x0800C000, 0x5000)` 은 20 KB 를 요청했지만 S3(16K) + **S4(64K)** 를 지운다(테스트 A-8, S4 끝 근처 0x0801FFF0 의 표식이 사라지고 S5 는 남는다). 앞쪽 확장은 호출자가 언급하지도 않은 바이트(대개 부트로더나 설정)를 지우므로 거절한다. 레이아웃 자체가 섹터 정렬돼 있어야 한다는 건 BM-B06 의 링커 `ASSERT` 이야기다.

**무작위 대조.** 8 × 512 B, unit 4 에서 4000 연산 (erase 12 %, 쓰기 88 %, 쓰기 데이터의 1/3 은 0xFF) 을 "합법이면 바이트 복사, 아니면 그대로" 인 기준 배열과 비교 — `./solution -v` 실제 출력: `A-9: 4000 ops, writes ok 2088, refused 1417, erases 495`, 마지막에 4 KB 전체 일치.

### Part B — 전원 차단: 잡히는 것과 못 잡히는 것

torn erase 의 네 단계를 `demo_torn` 으로 보면 (실제 출력, 섹터 앞 16 B):

```text
  phase0 (early)  (seed 4)
    before  C5 E9 B7 3C 7F AE 33 D2 0D 0E FB FB 45 50 11 2D
    after   C5 E9 B7 7C 7F AE 77 FA 2D AE FB FF 45 54 57 FD
    blank_check(sector)=FAIL   after age: blank_check=FAIL
  phase2 (late: reads blank, weak)  (seed 2)
    before  C5 E9 B7 3C 7F AE 33 D2 0D 0E FB FB 45 50 11 2D
    after   FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
    weak    00 00 00 02 00 11 00 20 C2 81 00 00 00 24 40 40
    blank_check(sector)=PASS   after age: blank_check=FAIL
```

erase 는 비트를 **올리기만** 하므로 after ⊇ before (예: 0x3C → 0x7C). 단계 2 가 핵심이다: 지금 읽으면 완벽한 0xFF, 그러나 일부 셀은 문턱 근처에만 와 있어 시간이 지나면(또는 온도·읽기 교란) 0 으로 돌아간다. 실제 NOR 의 "partially erased / over-erase" 셀을 단순화한 모델이다 (물리 세부는 칩마다 다름 — 확인 권장). 10 000 회 통계(실제 출력):

```text
  phase  count  blank-check PASS now  PASS after age
  0       2498      0                     0
  1       2502      0                     0
  2       2509   2509                     0
  3       2491   2491                  2491
  mean weak bits per phase-2 sector: 256.0 (of 2048 bits)
```

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">torn erase 4 단계: blank check 가 말해 주는 것 (demo_torn 10 000 회)</text>
  <text x="20" y="52" fill="currentColor">phase 0 초반</text>
  <rect x="130" y="38" width="150" height="22" class="f-warn" stroke="currentColor"/><text x="205" y="53" fill="currentColor" text-anchor="middle">옛 데이터 + 일부 1</text>
  <text x="300" y="53" fill="currentColor">blank check FAIL → 검출</text>
  <text x="20" y="92" fill="currentColor">phase 1 중반</text>
  <rect x="130" y="78" width="150" height="22" class="f-warn" stroke="currentColor"/><text x="205" y="93" fill="currentColor" text-anchor="middle">절반쯤 1</text>
  <text x="300" y="93" fill="currentColor">blank check FAIL → 검출</text>
  <text x="20" y="132" fill="currentColor">phase 2 후반</text>
  <rect x="130" y="118" width="150" height="22" class="f-acc" stroke="currentColor"/><text x="205" y="133" fill="currentColor" text-anchor="middle">FF…FF (weak 비트)</text>
  <text x="300" y="133" fill="currentColor">blank check PASS (2509/2509) → 못 잡음, flash_age 뒤 0/2509</text>
  <text x="20" y="172" fill="currentColor">phase 3 완료</text>
  <rect x="130" y="158" width="150" height="22" class="f-ok" stroke="currentColor"/><text x="205" y="173" fill="currentColor" text-anchor="middle">FF…FF (건강)</text>
  <text x="300" y="173" fill="currentColor">blank check PASS, 나중에도 PASS (무해)</text>
  <path d="M290 118 V182" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="20" y="210" fill="currentColor">결론: 읽기만으로는 phase 2 와 phase 3 을 구별할 수 없다.</text>
  <text x="20" y="230" fill="currentColor">"erase 가 끝났다" 는 증거는 erase 가 OK 를 돌려준 뒤에 따로 기록해야 한다 (항상 다시 지우기 / 완료 마커).</text>
</svg>
```

**각본 캠페인의 결과** (`./solution` 실제 출력):

```text
Part B: N=18 ops x 64 seeds = 1152 scenarios | phase0 30 phase1 28 phase2 36 phase3 34 program 1024
Part B: false-accept 0 | silent weak (blank check passed) 36 | needed recovery 1152
Part B: after age  ALWAYS_ERASE ok 1152/1152 | SKIP_IF_BLANK corrupt 33/1152
```

| 질문 | 답 (테스트가 단언) |
|---|---|
| 모든 cut 에서 store_records 가 `POWER_CUT` 을 돌려줬나 | 1152/1152 |
| CRC 가 "유효" 라 했는데 바이트가 틀린 적 | **0** (torn program 은 CRC 로 항상 걸림) |
| torn erase 가 비트를 내린 적 (erase 는 올리기만) | 0 — 128 개 전부 ⊇ |
| phase 0/1 이 blank check 를 통과한 적 | 0 — 검출 가능 |
| phase 2 가 blank check 를 통과한 횟수 | 36/36 — **원리적으로 검출 불가** |
| ALWAYS_ERASE 복구 후 시간 경과 | 1152/1152 유효 |
| SKIP_IF_BLANK 복구 후 시간 경과 | **33** 개 손상 (모두 그 36 개 안에서; 나머지 3 개는 weak 비트가 레코드의 1 비트 자리에 안 걸림) |

512 seed 로 키우면(`demo_torn` 4 절): 9216 시나리오, phase 2 = 256, false-accept 0, ALWAYS_ERASE 9216/9216, SKIP_IF_BLANK 손상 **243**/9216 — 비율이 그대로다.

여기서 배울 것: **부팅 검사로 알 수 있는 건 "지금 읽히는 내용" 뿐**이다. CRC 는 torn program 을 완벽히 잡지만, "erase 가 물리적으로 끝났는가" 는 읽어서 알 수 없다. 그래서 (1) 준비 상태를 모르는 섹터는 **무조건 다시 지우거나**, (2) erase 가 OK 를 돌려준 **뒤에** 완료 마커를 써서 증거를 남긴다(연습 3: 이전 세대 섹터 + 21 op × 64 seed 에서 실패 0, erase 512 회 절약). ST 의 EEPROM 에뮬레이션(AN3969)이 페이지 상태 워드(ERASED/RECEIVE_DATA/VALID_PAGE)를 두는 것도, NAND FTL 이 블록을 "free" 목록에 넣기 전 erase 완료를 메타데이터로 기록하는 것도 같은 이유다. Don 이 SSD 에서 본 PLP(전원 차단 시 캡으로 버티며 flush) 는 이 창을 **물리적으로** 줄이는 쪽이고, 여기서 하는 건 **논리적으로** 견디는 쪽이다 — 둘 다 필요하다.

**torn program 쪽**은 B-7 이 성질을 고정한다: 2000 seed 에서 앞 단위 완료 / 찢어진 단위 하나 / 뒤 단위 0xFF, 찢어진 단위에서 1 이어야 할 비트가 0 이 된 적 없음. `demo_torn` 3 절: 찢어진 단위 위치는 8 곳에 고르게(1236–1255), 필요한 전이 중 25–75 % 가 일어난 경우가 9507/10 000. 실제 칩에선 "프로그램이 덜 된 셀" 도 weak 일 수 있다 — 이 모델은 단순화했다(§8 Q9).

### Part C — wear: 로그 구조는 FTL 의 축소판

`./solution -v` 의 히스토그램 (실제 출력, 10 000 회 갱신, 4 × 1 KB):

```text
  naive rewrite (10000 updates)
    sector 0  10000 |########################################
    sector 1      0 |
  log-structured append (10000 updates, 4 x 1 KB, 32 B records)
    sector 0     79 |########################################
    sector 1     79 |########################################
    sector 2     79 |########################################
    sector 3     79 |########################################
  boot: mounted seq 10000 at sector 0 slot 16, next append ok
```

```svg
<svg viewBox="0 0 760 210" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">섹터별 erase 횟수, 10 000 회 갱신 (막대 길이 비례: 680 px = 10 000)</text>
  <text x="10" y="48" fill="currentColor">naive S0</text>
  <rect x="70" y="36" width="680" height="18" class="f-warn" stroke="currentColor"/><text x="745" y="50" fill="currentColor" text-anchor="end">10000 = F4 대표 endurance 에 도달</text>
  <text x="10" y="76" fill="currentColor">naive S1-3</text><text x="76" y="76" fill="currentColor">0</text>
  <text x="10" y="112" fill="currentColor">log S0</text><rect x="70" y="100" width="5.4" height="18" class="f-ok" stroke="currentColor"/><text x="82" y="114" fill="currentColor">79</text>
  <text x="10" y="136" fill="currentColor">log S1</text><rect x="70" y="124" width="5.4" height="18" class="f-ok" stroke="currentColor"/><text x="82" y="138" fill="currentColor">79</text>
  <text x="10" y="160" fill="currentColor">log S2</text><rect x="70" y="148" width="5.4" height="18" class="f-ok" stroke="currentColor"/><text x="82" y="162" fill="currentColor">79</text>
  <text x="10" y="184" fill="currentColor">log S3</text><rect x="70" y="172" width="5.4" height="18" class="f-ok" stroke="currentColor"/><text x="82" y="186" fill="currentColor">79</text>
  <text x="200" y="138" fill="currentColor">총 316 = format 4 + 회전 312 (= ⌈10000/32⌉ − 1)</text>
  <text x="200" y="158" fill="currentColor">섹터당 32 레코드 × 4 섹터 → 비율 126 배 (10000 / 79)</text>
</svg>
```

naive 는 갱신 1 회 = erase 1 회라 **한 섹터가 수명(10 kcycles 대표값)을 혼자 다 쓴다**. 로그는 out-of-place 로 쓰고 섹터가 찼을 때만 다음 섹터를 지운다 — 갱신 32 × 4 = 128 회당 섹터마다 erase 1 회. 이게 FTL 의 "덮어쓰지 말고 새 자리에 쓰고, 매핑(여기선 seq 최대)으로 최신을 찾는다" 의 가장 작은 형태다. `demo_wear` (100 000 회, 실제 출력):

```text
strategy                         sectors   total erases  max/sector  min/sector  updates until 10000 cycles
naive (erase + rewrite in place)  1 x 1 KB        100000      100000      100000  10000
log append                        2 x 1 KB          3126        1563        1563  ~639795
log append                        4 x 1 KB          3128         782         782  ~1278772
log append                        8 x 1 KB          3132         392         391  ~2551020
log append (F4 EEPROM-emul. size) 2 x 16 KB         197          99          98  ~10101010
naive, 16 KB sector : 400.1 ms per update (typ), worst 800.8 ms
log,  2 x 16 KB     : 0.92 ms per update on average (typ), but one update in 507 still pays a 400 ms erase
```

시간 줄은 F4 계열 데이터시트 대표값(16 KB erase typ 400 / max 800 ms, word program typ 16 / max 100 µs, x8 병렬 — F412/F427 표로 확인, F407 원본 확인 권장)으로 곱한 **추정**이다. 평균은 0.92 ms 지만 507 번에 한 번은 400 ms erase 를 맞는다 — 실시간 루프에서 중요한 건 평균이 아니라 이 꼬리다 (Q6).

이 레슨의 로그는 **키가 하나**라 회전 때 옮길 게 없다. 키가 여럿이면 지울 섹터에 남은 "살아 있는 최신 레코드" 를 먼저 새 섹터로 복사해야 하고(GC), 그 복사가 쓰기 증폭(write amplification)이다 — FTL 의 GC 와 똑같은 문제. 그리고 진짜 FTL 처럼 erase 카운터 자체도 플래시에 저장해야 전원을 껐다 켜도 wear leveling 이 이어진다.

**C1 의 함정** (전원 차단 300 회, 실제 출력): `Part C: 300 power cuts | mount = last ack (+1 when the torn write completed: 0) | torn slots skipped 290`. 290 번은 마지막 비-blank 슬롯이 찢어진 레코드였고, 마운트가 그 슬롯을 건너뛰었기 때문에 이어지는 새 내용 100 개가 `NOT_ERASED` 없이 들어갔다. 재미있는 반례: 재부팅 뒤 **같은 내용**을 그 찢어진 슬롯에 다시 쓰면 AND 플래시에선 통과한다 (찢어진 비트 ⊆ 원래 써야 할 비트) — 그래서 테스트는 재부팅 뒤 다른 내용을 쓴다. 이 "같은 데이터 재시도" 는 ECC 플래시에선 불가능하다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 107/107 passed
./solution -v                                                                     # A-9 통계 + Part C 히스토그램·부팅 로그
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_san && ./sol_san   # 107/107, 보고 없음
```

검출력 확인 (비공개 사본에 버그를 심어 돌린 실제 결과): 사전검사 삭제 → 97/107, 0x00 패딩 merge → 100/107, erase 앞쪽 확장 허용 → 105/107, 마운트 쓰기 위치 = 최신 유효 다음 → 105/107, erase 후 blank verify 삭제 → 106/107. 모두 exit 1.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 지우지 않고 덮어쓰고 "성공" 을 믿음 | F4 는 에러 없이 AND — 0x55 위 0xAA = 0x00 | 쓰기 전 `(old & new) == new` 검사, 아니면 `ERR_NOT_ERASED` |
| 검사와 쓰기를 한 루프에서 | 뒤에서 충돌을 찾으면 앞은 이미 써짐 (되돌릴 수 없음) | 검사 루프 전체 → 쓰기 루프 전체 |
| 비정렬 head 를 0x00 으로 패딩 | 이웃 바이트 파괴 | 현재 값으로 read-merge (또는 0xFF 패딩 + 내 바이트만 verify) |
| 0xFF 패딩 후 버퍼 전체를 verify | 이웃에 데이터가 있으면 거짓 `ERR_VERIFY` | 보낸 merge 버퍼와 비교, 또는 내 바이트만 비교 |
| ECC 플래시에서 레코드가 단위를 공유 | 두 번째 레코드가 `NOT_ERASED`(PROGERR 류) | 레코드를 prog_unit 배수로 패딩 |
| `erase(addr, len)` 이 주소를 섹터 시작으로 내림 | 호출자가 모르는 앞 바이트(부트로더·설정) 삭제 | 시작 비정렬은 `ERR_ALIGN` |
| F4 섹터를 16 KB 균일로 가정 | 0x08010000 부터 64 KB, 0x08020000 부터 128 KB | 섹터 표 + `flash_sector_of` |
| erase 후 blank verify 생략 | 0 고착 셀/실패한 erase 를 모름 | 섹터마다 blank check → `ERR_VERIFY` |
| "이미 0xFF 니까 erase 생략" | torn erase phase 2 는 blank 로 읽힘 → 나중에 손상 (33/1152) | 상태를 모르면 항상 erase, 또는 완료 마커 (연습 3) |
| 부팅 스캔이 "최신 유효 다음" 에 씀 | 찢어진 슬롯에 새 내용 → `NOT_ERASED` | 마지막 비-blank 슬롯 다음 |
| 같은 섹터를 매번 erase 하는 설정 저장 | 10 kcycles 대표값 → 10 000 회에 수명 끝 | 로그 구조 / EEPROM 에뮬레이션 |
| 플래시에서 실행 중인 코드로 erase | 버스 stall 동안 ISR·워치독 kick 정지 | erase 루틴·ISR 을 RAM 에서, 워치독 예산 계산 (BM-M10) |

## 8. 면접 Q&A

<details>
<summary>Q1. 플래시에 erase 없이 같은 주소에 두 번 쓰면 어떻게 되나요?</summary>

내장 NOR 계열은 program 이 1→0 만 하므로 결과는 두 값의 AND 다. STM32F4 는 이를 에러로 보지 않는다 — RM0090 이 "1→0 변경은 erase 없이 연속 쓰기 가능" 이라고 명시한다(확인 권장). 그래서 드라이버가 `(old & new) == new` 로 막아야 하고, 반대로 이 성질을 **일부러** 쓰기도 한다: 상태 플래그를 0xFFFF → 0xEEEE → 0x0000 처럼 내려 가며 기록(ST EEPROM 에뮬레이션 AN3969 의 페이지 상태 — 확인 권장). 꼬리: "모든 플래시가 그런가?" → ECC 플래시(STM32L4/G4/H7 류)는 단위(8 B / 32 B)당 한 번만 쓸 수 있어 두 번째 쓰기가 PROGERR 류로 거절된다 — 이 레슨의 `one_shot`. NAND 는 페이지 부분 프로그램 횟수(NOP) 제한이 있다.

</details>

<details>
<summary>Q2. 설정 1 바이트만 바꾸려면 어떻게 하나요? 그 도중 전원이 나가면?</summary>

순진한 방법: 섹터 전체를 RAM 에 복사 → 1 바이트 수정 → 섹터 erase → 전체 다시 쓰기. F4 128 KB 섹터면 RAM 128 KB 가 필요하고(F407 SRAM 192 KB 중), erase 와 재기록 사이에 전원이 나가면 **섹터 전체가 사라진다**. 실전: (1) 두 섹터 핑퐁 + 시퀀스 번호 + CRC — 새 사본을 다른 섹터에 완성한 뒤에야 옛 것을 무효화, (2) 로그 구조: 바뀐 값만 append, 부팅 때 최신을 찾음 (Part C), (3) 작은 설정이면 외부 EEPROM/FRAM. 이 레슨 Part C 의 로그는 갱신당 erase 가 1/128 이고, 갱신 중 차단은 "마지막 ack 된 값" 으로 돌아간다(300 회 테스트).

</details>

<details>
<summary>Q3. 쓰기 단위가 8 바이트인데 5 바이트 레코드를 쓰라는 요청이 오면?</summary>

감싸는 단위를 구해 head/tail 을 merge 한다: 현재 내용을 읽어 내 바이트만 바꾼 8 B 를 program. AND 플래시에선 남의 바이트를 그 값 그대로 다시 써도 전이가 없어 안전하다. 단 "남의 바이트가 0xFF 로 남아 있을 때만" 이 일반 원칙 — ECC 플래시는 단위 재프로그램 자체가 불가능하고(B-4: 5 B 뒤 6 B 가 `NOT_ERASED`), 같은 단위를 공유한 두 레코드는 전원 차단 때 운명을 같이 한다. 그래서 설계 답은 "레코드를 단위 배수로 패딩해 단위를 공유하지 않는다". program 횟수 = ⌈(off+len)/u⌉ − ⌊off/u⌋ (연습 1 에서 전수 확인).

</details>

<details>
<summary>Q4. erase 중 전원이 나갔습니다. 다음 부팅에서 어떻게 알아내나요?</summary>

일부는 알 수 있고 일부는 원리적으로 못 안다. 초·중반에 끊기면 섹터에 0 비트가 남아 blank check 가 잡는다 (10 000 회 중 phase 0/1 5000 회 전부 FAIL). 후반에 끊기면 전부 0xFF 로 읽히지만 일부 셀이 약하게만 지워져 나중에 0 으로 돌아간다 — blank check 통과 2509/2509, 시간 경과 후 0/2509. 그래서 "blank 이면 erase 생략" 정책은 1152 시나리오 중 33 개를 시간차로 망가뜨렸고, "상태를 모르면 항상 erase" 는 1152/1152 를 지켰다. 증거가 필요하면 erase 가 OK 를 돌려준 뒤 **완료 마커**를 쓴다 (연습 3). 쓰기 쪽은 CRC 가 잡는다 (false-accept 0). SSD 로 치면 erase 상태 플래그를 믿고 바로 free 풀에 넣지 않고, 메타데이터에 "erase 완료" 가 기록된 블록만 쓰는 것과 같다.

</details>

<details>
<summary>Q5. drv_erase_range 에 섹터 경계가 아닌 주소가 들어오면 어떻게 하나요? 왜 그렇게 정했나요?</summary>

시작이 섹터 경계가 아니면 `ERR_ALIGN` 으로 거절하고, 끝은 마지막 바이트가 든 섹터 끝까지 확장한다. 앞으로 확장하면 호출자가 언급하지도 않은 바이트를 지운다 — 흔히 바로 앞 섹터가 부트로더 꼬리나 설정이다. 끝 확장은 "이 슬롯 시작부터 len 바이트 이미지를 담을 자리" 라는 의도와 맞지만 F4 에선 비싸다: 20 KB 를 요청해도 S3 + S4 = 80 KB 가 지워진다(A-8). 더 엄격한 대안은 끝도 정확히 섹터 끝이길 요구하는 것 — 업데이트 슬롯처럼 레이아웃이 고정된 곳에선 그쪽이 낫다. 어느 쪽이든 규칙을 API 문서에 적고 테스트로 고정하는 게 핵심.

</details>

<details>
<summary>Q6. STM32F4 에서 플래시를 지우는 동안 인터럽트와 워치독은 어떻게 되나요?</summary>

erase/program 중에 같은 플래시를 읽으면 버스가 stall 된다(RM0090: "code or data fetches cannot be performed while a write/erase operation is ongoing" — 확인 권장). 벡터 테이블과 ISR 이 플래시에 있으면 인터럽트는 erase 가 끝날 때까지 사실상 막힌다 — 128 KB 섹터면 대표값 typ 2 s / max 4 s(x8). 그동안 워치독 kick 도 못 하므로, IWDG 타임아웃이 짧으면 erase 중에 리셋된다(BM-M10). 대책: erase 루틴·필요한 ISR·벡터 테이블을 RAM 으로(VTOR 재배치 — BM-A01), BSY 폴링 루프에서 kick; 또는 dual-bank 부품(F42x/F43x, 1 MB 이상 dual-bank 설정 — 확인 권장)에서 다른 뱅크를 지우며 실행(read-while-write). 최악값으로 예산을 잡아야 한다 — 평균 0.92 ms 여도 507 번에 한 번은 400 ms 다.

</details>

<details>
<summary>Q7. 같은 설정값을 하루에 수백 번 저장합니다. 몇 년 갈까요?</summary>

계산부터. F4 내장 플래시 endurance 10 kcycles(대표값, 데이터시트 확인)라면 naive(매번 erase)는 하루 300 회면 **33 일**. 로그 구조로 32 B 레코드를 2 × 16 KB 섹터에 append 하면 섹터당 512 레코드, 100 000 회 갱신에 섹터당 erase 99 회(`demo_wear`) → 10 kcycles 까지 약 1000 만 회 갱신 ≈ 하루 300 회로 약 92 년. 이게 ST EEPROM 에뮬레이션이 두 섹터를 쓰는 이유다. 꼬리: "키가 여럿이면?" → 회전 때 살아 있는 레코드를 복사(GC)해야 하고 쓰기 증폭이 생긴다. 남은 공간 비율이 낮을수록 GC 비용이 커지는 것도 SSD over-provisioning 과 같다.

</details>

<details>
<summary>Q8. (staff) 이 드라이버를 외부 QSPI NOR 나 NAND 로 옮겨야 한다면 무엇을 바꾸겠나요?</summary>

모델 경계(`flash_erase_sector / flash_program / flash_read`)는 그대로 두고 그 아래만 바꾼다. QSPI NOR: 명령마다 WREN, WIP 폴링 + 타임아웃, **256 B 페이지 랩** 때문에 program 은 페이지 경계에서 자른다(COMM-A06), 4 KB/64 KB erase 선택, 블록 보호 해제. 의미(AND, 섹터 erase)는 같아서 드라이버 상위는 재사용. NAND: 의미가 달라진다 — 페이지 단위 program 은 블록 안에서 **순서대로**, 페이지당 부분 program 횟수 제한, 같은 자리 AND 재프로그램 금지, 공장 불량 블록 + 런타임 불량 블록 관리, ECC 필수(비트 오류가 정상), read disturb·retention 에 따른 refresh. 그러면 `drv_write` 의 "AND 허용" 경로는 사라지고 Part C 의 로그 구조가 기본 설계가 된다 — 즉 FTL. 테스트 전략(모든 op 차단 주입, 기준 모델 대조)은 그대로 쓴다.

</details>

<details>
<summary>Q9. (staff) 이 모델과 설계가 틀리는 경우는 어디인가요?</summary>

(1) torn program 을 "부분집합 전이" 로만 모델했다 — 실제로는 덜 프로그램된 셀도 경계값이라 나중에 1 로 읽힐 수 있다. CRC 는 지금의 오류만 잡으므로, 중요한 메타데이터는 쓰기 후 margin read(지원하는 칩) 또는 이중 사본이 필요하다. (2) ECC 플래시에서 찢어진 단위를 **읽으면** 2 비트 오류로 NMI/BusFault 가 날 수 있다(L4/H7 류 ECCD — 확인 권장) — 부팅 스캔이 크래시한다. 스캔 코드는 ECC fault 를 잡아 "깨진 슬롯" 으로 처리해야 한다. (3) 캐시: F4 ART 가속기/데이터 캐시가 erase 이전 내용을 들고 있으면 read-back verify 가 캐시를 읽는다 — erase 후 캐시 리셋 필요(FLASH_ACR DCRST/ICRST, 끄고 리셋 — 확인 권장). (4) 시간이 없다: erase 시간, 온도에 따른 시간 변화, 타임아웃이 모델에 없다. (5) 고장 하나만 가정했다 — 복구 중 두 번째 차단은 BM-A06 이 전수로 다룬다.

</details>

## 9. 추가 연습

### 연습 1 — program 횟수의 닫힌 식

`drv_write(off, len)` 이 부르는 `flash_program` 횟수를 반복 없이 계산하는 `prog_ops(off, len, u)` 를 쓰고, unit 1/4/8 × off 0..63 × len 0..64 전수로 드라이버 실제 횟수와 맞춰라.

<details>
<summary>답 보기</summary>

`len == 0 ? 0 : ⌈(off+len)/u⌉ − ⌊off/u⌋`. 정수로는 `(off + len + u − 1) / u − off / u`. 레코드 A (0x1FA, 20, 4) = 6, 레코드 B (0x22C, 37, 4) = 10 — Part B 의 N = 2 + 6 + 10 = 18 이 여기서 나온다. 12 480 경우 전부 일치.

</details>

### 연습 2 — F4 섹터 번호를 루프 없이

`f4_sector_of(off)` (off = addr − 0x08000000, 1 MB 부품) 를 비교 두 번과 시프트로 쓰고 1 M 오프셋 전수로 표와 대조하라.

<details>
<summary>답 보기</summary>

`off < 0x10000 → off >> 14` (16 KB × 4), `off < 0x20000 → 4`, 그 외 `4 + (off >> 17)` (0x20000 >> 17 = 1 → 5, 0xE0000 >> 17 = 7 → 11). 0x100000 이상은 −1. 부트로더가 "이 주소가 어느 섹터인가" 를 매번 표를 훑지 않고 구할 때 쓰는 식이다.

</details>

### 연습 3 — erase 완료 마커

"blank 이면 erase 생략" 을 안전하게 만들어라. 각 섹터 첫 8 B 에 마커 `"ERASEDOK"` 를 **erase 가 OK 를 돌려준 뒤에만** 쓰고, 준비 단계에서 "마커 유효 + 나머지 blank" 일 때만 erase 를 건너뛴다. 이전 세대(마커 + 옛 레코드)가 있는 섹터에서 시작해 모든 op 지점 × seed 64 로 차단 → 재부팅 → 복구 → `flash_age` 후 두 레코드가 유효해야 한다.

<details>
<summary>답 보기</summary>

마커는 erase 완료의 **증거**다: torn erase 는 마커를 지우거나(phase 2/3) 비트를 올려 깨뜨리고(phase 0/1), torn 마커 쓰기는 마커를 미완성으로 남긴다 — 어느 경우든 "마커 유효" 가 아니므로 다시 지운다. 실제 출력: `ex3: 21 ops x 64 seeds = 1344 scenarios, weak-erased cases 28, erases skipped by marker 512, failures 0`. weak 섹터가 28 번 생겼지만 한 번도 쓰이지 않았고, 이미 준비된 섹터의 erase 512 회를 아꼈다. 대가: 섹터마다 8 B 와 program 2 회.

</details>

### 연습 4 — ECC 플래시에서 레코드 무효화

`one_shot`, unit 8 플래시에 레코드 `data[16] | commit[8] ('C' 'M' crc32 0 0) | tomb[8]` 을 설계하라. data 를 먼저, commit 을 마지막에 쓰고, 무효화는 비어 있던 묘비 단위를 0 으로 쓴다. "commit 에 0 을 써서 플래그 내리기" 가 거절되는 것과, data/commit 3 op 의 모든 지점 × 500 seed 차단에서 "유효인데 내용이 틀림" 이 없음을 보여라.

<details>
<summary>답 보기</summary>

AND 플래시의 "플래그 내리기" 트릭은 ECC 플래시에서 같은 단위 재프로그램이라 `NOT_ERASED` — 그래서 무효화용 단위를 처음부터 비워 둔다(묘비도 한 번뿐: 두 번째 무효화는 거절). commit 을 마지막에 쓰면 data 가 다 써지기 전에는 절대 유효가 아니다. 실제 출력: `ex4: 1500 cut scenarios, valid after cut 0` — commit 단위 자체가 찢어진 경우도 CRC/매직이 깨져 무효였다. MCUboot 의 이미지 trailer(magic, image_ok 를 각각 별도 정렬 단위에 두는 설계)도 같은 이유다 — BM-A06 에서 다시 본다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 14/14 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_san && ./pr_san   # 14/14
```

## 10. 핵심 정리

- 플래시 3 규칙: program = AND (1→0 만), erase = 섹터 통째 0xFF, 쓰기는 prog_unit 정렬. F4 는 재프로그램을 **에러 없이** AND 한다 — 막는 건 드라이버.
- `drv_write`: 검사 루프 전체(`(old & new) == new`, ECC 면 단위 전체 0xFF) → 쓰기 루프(read-merge → program → verify). 전부 아니면 전무. program 횟수 = ⌈(off+len)/u⌉ − ⌊off/u⌋.
- read-merge 는 남의 바이트가 0xFF 로 남을 때가 원칙 — ECC 플래시는 단위당 1 회. 레코드는 단위 배수로 패딩.
- `drv_erase_range`: 시작 비정렬은 거절, 끝은 섹터 끝까지 확장, 섹터마다 blank verify. F4 섹터 16/16/16/16/64/128×7 KB.
- 전원 차단: CRC 는 torn program 을 잡는다(false-accept 0/1152). torn erase 후반은 **blank 로 읽혀도 약하다** — 상태를 모르면 항상 erase, 또는 erase 성공 뒤 완료 마커.
- 로그 구조 append: 10 000 갱신에 섹터당 erase 79 vs naive 10 000 (126 배). 부팅 스캔의 쓰기 위치는 "마지막 비-blank 슬롯 다음".
- F4 erase 중엔 플래시 fetch 가 stall — ISR·워치독 예산은 최악 erase 시간(128 KB max 4 s 대표값)으로.

## 참고자료

- ST RM0090 Reference manual (STM32F405/415, 407/417, 427/437, 429/439) — 3장 Embedded flash memory interface: Table 5 섹터 구성, Table 13 PSIZE 병렬도, FLASH_KEYR 키, FLASH_CR(PG/SER/MER/SNB/PSIZE/STRT/LOCK), FLASH_SR(EOP/OPERR/WRPERR/PGAERR/PGPERR/PGSERR/BSY), "Successive write operations are possible without the need of an erase operation when changing bits from '1' to '0'", 읽기 stall 문장 — 확인한 웹 사본: <https://docs.dirba.io/rm0090-stm32f405-415-407-417-427-437-429-439/03-embedded-flash-memory-interface/>, 원본 <https://www.st.com/resource/en/reference_manual/rm0090.pdf>
- STM32F4 계열 데이터시트 "Flash memory characteristics" — 16 KB erase typ 400 / max 800 ms, 128 KB typ 2 / max 4 s (x8), word program typ 16 / max 100 µs: F412/F427 표 사본으로 확인 (<https://www.alldatasheet.com/html-pdf/2179000/STMICROELECTRONICS/STM32F412RGH7P/266870/117/STM32F412RGH7P.html>), F407 원본과 endurance 10 kcycles 는 확인 권장
- ST AN3969 "EEPROM emulation in STM32F40x/STM32F41x microcontrollers" — 두 섹터 + 페이지 상태 (확인 권장)
- STM32L4 Reference manual RM0351 — 64 비트 double-word program, ECC, PROGERR (확인 권장); Nordic nRF52 Product Specification NVMC — n_WRITE 제한 (확인 권장)
- MCUboot design 문서 — 이미지 trailer 와 정렬 단위 (<https://docs.mcuboot.com/design.html>, BM-A06 에서 사용)
- 관련 레슨: COMM-A06 (SPI NOR 프로토콜·페이지 랩·NOR 위 로그), ALGO-M01 / COMM-B08 (CRC-32), BM-B06 (섹터 정렬 레이아웃 링커 ASSERT), BM-M10 (워치독 타임아웃), BM-A01 (VTOR 를 RAM 으로), 다음 단계: BM-A04 (이미지 헤더 + CRC/SHA-256), BM-A05 (YMODEM 부트로더 — 이 모델 축약판 사용), BM-A06 (전원 차단 내성 업데이트 — 이 모델의 cut_at 훅으로 이중 차단까지 전수), PLAT-M07 (A/B 슬롯 정책)
