---
id: MEM-B12
time: 60분
updated: 2026-11-15
---
# MEM-B12 NAND 플래시 기초: 페이지/블록, program/erase 제약

## 학습 목표

- NAND 의 세 가지 규칙(페이지 단위 program, 블록 단위 erase, erase 전에는 다시 못 씀)을 **셀 물리**(전하 주입 = 1→0, 지우기 = 블록 전체)로 설명하고, 그 규칙을 지키는지 검사하는 칩 모델을 C 로 쓴다.
- 지오메트리(plane → block → page, page = data ‖ OOB)와 셀 타입 표(SLC/MLC/TLC/QLC 의 P/E 내구도, NOP, tR/tPROG/tBERS 대표값)를 하나의 구조체로 묶는다.
- program 판정의 **우선순위 표**(BAD_BLOCK → WORN_OUT → NOT_ERASED → OUT_OF_ORDER)를 만들고, 블록 상태 × 대상 페이지 전수 표로 검증한다.
- 공장 불량 블록 스캔(BBT)이 "처음 erase 하기 전에, OOB 만 읽어서" 해야 하는 이유를 말한다.
- in-place 갱신이 왜 실패하거나(ERR_NOT_ERASED) 비싼지(블록 RMW: 갱신 1번 = erase 1번 + program 8번)를 측정값으로 보이고, out-of-place 쓰기(FTL, MEM-M10)로 넘어가는 이유를 숫자로 말한다.

## 1. 문제

"NAND 는 왜 덮어쓸 수 없나요? 그래서 FTL 이 뭘 하나요?" 는 SSD · eMMC · UFS 펌웨어 면접의 첫 질문이고, SoC bring-up 에서 raw NAND 로 부팅하는 보드(U-Boot `nand` 명령, Linux MTD/UBI)를 만질 때도 같은 규칙이 바로 버그로 나타난다. 규칙을 어긴 쓰기는 실제 칩에서 **에러 없이 조용히 데이터를 망가뜨린다** — 그래서 규칙을 검사하는 모델을 먼저 손에 쥐어 두는 것이 이 레슨의 목적이다. 이 모델은 MEM-M10(L2P), M11(GC), M12(웨어 레벨링), M13(불량 블록 · RBER), M14(전원 차단 복구)가 그대로 품는다.

**셀 물리 세 줄.** NAND 셀은 전하를 가두는 층(floating gate 또는 charge trap)을 가진 트랜지스터다.

1. 지워진(erased) 셀은 전하가 없어 문턱 전압(Vt)이 낮고 **1** 로 읽힌다. 그래서 지운 페이지는 모두 `0xFF`.
2. program 은 전자를 넣어 Vt 를 올린다 → **0**. 페이지 단위로, 비트를 **1→0 으로만** 바꿀 수 있다. 0→1 은 불가능.
3. erase 는 전자를 빼낸다. 블록 안 셀들이 같은 웰(well)을 공유해 높은 전압을 한꺼번에 걸기 때문에 **블록 전체**가 단위다.

그래서 "같은 페이지에 새 값 쓰기" = 블록 전체를 지우고 다시 쓰는 일이 된다. 지우기는 수 ms 걸리고, 셀의 산화막을 조금씩 망가뜨려 블록마다 정해진 횟수(P/E 사이클, program/erase cycle)만 견딘다.

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">이 레슨의 칩: 2 plane × 4 블록 × 8 페이지 × (data 64 B ‖ OOB 8 B)</text>
  <rect x="10" y="36" width="240" height="262" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor">die (LUN)</text>
  <rect x="20" y="62" width="105" height="228" rx="6" fill="none" stroke="currentColor"/>
  <rect x="135" y="62" width="105" height="228" rx="6" fill="none" stroke="currentColor"/>
  <text x="72" y="80" fill="currentColor" text-anchor="middle">plane 0</text>
  <text x="187" y="80" fill="currentColor" text-anchor="middle">plane 1</text>
  <g stroke="currentColor">
    <rect x="30" y="90" width="85" height="40" class="f-acc"/>
    <rect x="30" y="140" width="85" height="40" fill="none"/>
    <rect x="30" y="190" width="85" height="40" fill="none"/>
    <rect x="30" y="240" width="85" height="40" class="f-warn"/>
    <rect x="145" y="90" width="85" height="40" fill="none"/>
    <rect x="145" y="140" width="85" height="40" class="f-warn"/>
    <rect x="145" y="190" width="85" height="40" fill="none"/>
    <rect x="145" y="240" width="85" height="40" fill="none"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="72" y="114">blk 0</text><text x="72" y="164">blk 2</text><text x="72" y="214">blk 4</text><text x="72" y="258">blk 6</text><text x="72" y="273">BAD</text>
    <text x="187" y="114">blk 1</text><text x="187" y="158">blk 3</text><text x="187" y="173">BAD</text><text x="187" y="214">blk 5</text><text x="187" y="264">blk 7</text>
  </g>
  <path d="M115 90 L290 62 M115 130 L290 286" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="355" y="52" fill="currentColor" text-anchor="middle">blk 0 (erase 단위)</text>
  <g stroke="currentColor">
    <rect x="290" y="62" width="130" height="28" class="f-acc"/>
    <rect x="290" y="90" width="130" height="28" class="f-acc"/>
    <rect x="290" y="118" width="130" height="28" class="f-acc"/>
    <rect x="290" y="146" width="130" height="28" class="f-mut"/>
    <rect x="290" y="174" width="130" height="28" class="f-mut"/>
    <rect x="290" y="202" width="130" height="28" class="f-mut"/>
    <rect x="290" y="230" width="130" height="28" class="f-mut"/>
    <rect x="290" y="258" width="130" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="298" y="81">p0 programmed</text><text x="298" y="109">p1 programmed</text><text x="298" y="137">p2 programmed</text>
    <text x="298" y="165">p3 0xFF</text><text x="298" y="193">p4 0xFF</text><text x="298" y="221">p5 0xFF</text>
    <text x="298" y="249">p6 0xFF</text><text x="298" y="277">p7 0xFF</text>
  </g>
  <text x="290" y="302" fill="currentColor" font-size="12">쓰기 포인터 next_page = 3</text>
  <path d="M420 146 L520 92 M420 174 L520 128" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="520" y="70" fill="currentColor">page = data ‖ OOB (spare)</text>
  <rect x="520" y="92" width="150" height="36" class="f-mut" stroke="currentColor"/>
  <rect x="670" y="92" width="58" height="36" class="f-acc" stroke="currentColor"/>
  <text x="595" y="115" fill="currentColor" text-anchor="middle">data 64 B</text>
  <text x="699" y="115" fill="currentColor" text-anchor="middle" font-size="12">OOB 8 B</text>
  <text x="520" y="160" fill="currentColor" font-size="12">OOB 바이트 배치 (이 레슨의 약속)</text>
  <g stroke="currentColor">
    <rect x="520" y="170" width="26" height="26" class="f-warn"/>
    <rect x="546" y="170" width="26" height="26" class="f-ok"/><rect x="572" y="170" width="26" height="26" class="f-ok"/>
    <rect x="598" y="170" width="26" height="26" class="f-ok"/><rect x="624" y="170" width="26" height="26" class="f-ok"/>
    <rect x="650" y="170" width="26" height="26" class="f-mut"/><rect x="676" y="170" width="26" height="26" class="f-mut"/>
    <rect x="702" y="170" width="26" height="26" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="533" y="188">0</text><text x="559" y="188">1</text><text x="585" y="188">2</text><text x="611" y="188">3</text>
    <text x="637" y="188">4</text><text x="663" y="188">5</text><text x="689" y="188">6</text><text x="715" y="188">7</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="520" y="216">byte 0 = 불량 마커 자리 (BBM)</text>
    <text x="520" y="234">byte 1..4 = LBA (P2L, Part C)</text>
    <text x="520" y="252">byte 5..7 = 여유 (실제는 ECC)</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="322">program · read 단위 = 페이지, erase 단위 = 블록. plane = 블록 번호 % planes (블록 주소 하위 비트).</text>
    <text x="10" y="342">erased = 모든 비트 1 (0xFF), program 은 1→0 만. 블록 3 · 6 = 공장 불량 (page 0 의 OOB byte 0 ≠ 0xFF).</text>
    <text x="10" y="362">크기는 이 레슨의 축소 모델. 실제 3D TLC 는 페이지 16 KiB + OOB 약 2 KB, 블록은 수백~천여 페이지 (벤더별).</text>
  </g>
</svg>
```

**이 레슨의 정의 (데이터시트 · 논문의 대표값 — 실제 부품은 데이터시트 확인).** 셀 타입 표는 코드의 `CELL[]` 과 같다. 세대(planar/3D)와 벤더에 따라 아래 값은 2배 이상 다르다.

| 셀 | bit/cell | 정격 P/E (대표, 흔한 범위) | tR (µs) | tPROG (µs) | tBERS (µs) | NOP | 블록 안 순서 (모델) |
|---|---|---|---|---|---|---|---|
| SLC | 1 | 100 000 (50 K–100 K) | 25 | 200 | 2 000 | 4 | 오름차순, 앞으로 건너뛰기 허용 |
| MLC | 2 | 3 000 (3 K–10 K) | 50 | 800 | 3 000 | 1 | 엄격한 순차 |
| TLC | 3 | 1 500 (1 K–3 K) | 70 | 1 000 | 3 500 | 1 | 엄격한 순차 |
| QLC | 4 | 500 (0.1 K–1 K) | 140 | 2 500 | 8 000 | 1 | 엄격한 순차 |

- **NOP** (number of partial programs): erase 한 번 사이에 같은 페이지를 program 할 수 있는 횟수. 2 KiB 페이지 SLC 데이터시트에는 NOP = 4 가 흔하고(512 B 조각씩 나눠 쓰기), MLC 이상은 1 이다.
- **순서 규칙.** 많은 데이터시트가 "블록 안 페이지는 낮은 번호부터 차례로(consecutively) program, 무작위 페이지 program 금지" 라고 적는다. 일부 SLC 문서는 "오름차순이면 건너뛰기 허용" 으로 읽히고, TLC/QLC 는 엄격한 순차(심지어 워드라인 단위 one-shot)를 요구한다. 모델은 이 차이를 보여 주려고 **SLC = 건너뛰기 허용, MLC 이상 = 엄격** 을 택했다 (`strict_seq`). 실제 SLC 부품도 대부분 순차가 안전하다.
- **마모 모델.** 실제 셀은 어느 날 갑자기 죽지 않는다. P/E 가 늘면 원시 비트 오류율(RBER)이 오르고, 정격 내구도는 "그 ECC 로 정해진 보존 기간을 지킬 수 있는 횟수" 다 (MEM-M13). 이 레슨은 그것을 결정적인 절벽으로 단순화한다: `pe ≥ endurance` 에서 erase 를 하면 status FAIL → 블록이 WORN(grown bad) 이 되고 이후 program/erase 도 FAIL.

### Part A — 지오메트리와 세 가지 동작

`nand_geo` (planes, blocks_per_plane, pages_per_block, page_bytes, oob_bytes, cell) 와 정적 버퍼를 가진 `nand_t` 를 쓴다. `nand_init` (지오메트리 검사 · 0xFF 출하 · 공장 불량 표시)는 주어진다. 구현할 것:

- `nand_read(n, blk, page, data, oob)` — data · OOB 를 복사 (NULL 인 쪽은 건너뜀 = OOB 만 읽기). 불량 · 마모 블록도 읽을 수 있다. tR 을 그 plane 의 바쁜 시간에 더한다.
- `nand_program(n, blk, page, data, oob)` — 저장값 `&=` 입력값 (1→0 만). `oob == NULL` 이면 OOB 는 그대로. 성공 시 tPROG.
- `nand_erase(n, blk)` — 블록 전체 0xFF, 쓰기 포인터 · program 횟수 리셋, P/E +1, tBERS.

### Part B — 규칙 위반을 에러 코드로

`program_check` 에 판정 순서를 넣는다 (5장의 표). 모든 위반은 **어서트가 아니라 반환 코드**다: `ERR_RANGE`, `ERR_PARAM`, `ERR_NOT_ERASED` (이미 데이터가 있음 · NOP 초과), `ERR_OUT_OF_ORDER` (지워져 있지만 순서상 못 씀), `ERR_BAD_BLOCK` (공장 불량), `ERR_WORN_OUT` (마모). 규칙 위반으로 거절된 명령은 칩에 안 보냈다고 보고 시간 0, 상태도 그대로. 마모 FAIL 은 칩이 시도한 것이라 시간이 든다. 테스트는 위반을 하나씩 주입하고, 블록 상태(page 0..k−1 program 됨) × 대상 페이지 p 전수 표(k = 0..8, p = 0..7, TLC 와 SLC)를 기대값과 맞춘다. 셀 타입마다 정격 P/E 만큼 erase 를 돌려 정확히 그다음 erase 에서 `ERR_WORN_OUT` 이 나는지 본다.

### Part C — 드라이버 계약: BBT 와 "덮어쓰기" 문제

- `nand_scan_bbt` — 칩 내부 상태를 보지 말고 **읽기만으로** 공장 불량 표를 만든다 (page 0 OOB byte 0 ≠ 0xFF).
- `naive_update` — LBA i = 블록의 page i 로 고정한 in-place 방식. 갱신 = 블록 전체 read → erase → 전체 program.
- `log_write` / `log_read` — out-of-place: 항상 다음 빈 페이지에 쓰고 작은 맵(LBA → 위치)만 바꾼다. OOB 에 LBA 를 적는다. 빈 페이지가 떨어지면 `ERR_RANGE` (GC 는 MEM-M11).
- 같은 워크로드(LBA 8개 채우기 + 랜덤 갱신 40번)에서 program · erase · read 횟수, plane 별 바쁜 시간, 최대 P/E 를 비교하고, in-place 만 쓰면 TLC 블록이 **갱신 몇 번에 죽는지** 센다.

## 2. Problem (English)

> **Raw NAND device model with rule checking.**
>
> Model a NAND die in C11 with static storage only. Geometry: `planes`, `blocks_per_plane`, `pages_per_block`, `page_bytes`, `oob_bytes`, and a cell type (SLC/MLC/TLC/QLC) that selects endurance, NOP, timing (tR, tPROG, tBERS) and the in-block ordering rule. Block `b` lives on plane `b % planes`. A fresh device reads `0xFF` everywhere; factory-bad blocks have a non-`0xFF` byte at OOB offset 0 of page 0.
>
> ```c
> nand_rc nand_read   (nand_t *n, uint32_t blk, uint32_t page, uint8_t *data, uint8_t *oob); /* either may be NULL */
> nand_rc nand_program(nand_t *n, uint32_t blk, uint32_t page, const uint8_t *data, const uint8_t *oob);
> nand_rc nand_erase  (nand_t *n, uint32_t blk);
> ```
>
> Rules: programming can only clear bits (`cell &= data`). A page may be programmed at most NOP times per erase, and a re-program is allowed only on the most recently programmed page. Within a block pages are programmed in increasing order; for strict-order cell types the page must be exactly the write pointer. Erase resets the block to `0xFF` and increments its P/E count; when the count has reached the rated endurance, erase fails and the block becomes worn (program and erase fail from then on). Return `ERR_RANGE`, `ERR_NOT_ERASED`, `ERR_OUT_OF_ORDER`, `ERR_BAD_BLOCK`, `ERR_WORN_OUT` — never assert. Accumulate busy time per plane.
>
> Then: (1) build the factory bad-block table using reads only; (2) show that an in-place update fails, implement the read-erase-rewrite workaround, and compare it with an append-only (out-of-place) log on the same workload.
>
> Example (TLC, block 0 holds pages 0..2): `program(0,3)` → `OK`; `program(0,1)` → `ERR_NOT_ERASED`; `program(0,5)` → `ERR_OUT_OF_ORDER`. SLC after programming pages 0 and 3: `program(0,1)` → `ERR_OUT_OF_ORDER`, `program(0,3)` → `OK` (2nd of NOP = 4), `program(0,0)` → `ERR_NOT_ERASED`. A TLC block with `pe == 1500` → `erase` → `ERR_WORN_OUT`.
>
> Follow-ups: Why is erase per block but program per page? Why must pages be programmed in order? What does erase-before-write force an FTL to do? Why must the bad-block scan run before the first erase?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여덟 군데다: Part A `nand_read` · `nand_program` · `nand_erase`, Part B `program_check` (그리고 `nand_erase` 의 마모 판정), Part C `nand_scan_bbt` · `naive_update` · `log_write` · `log_read`. 지오메트리 검사와 출하 상태(`nand_init`), 바쁜 시간 합계(`busy_total` · `makespan`), 하네스는 주어진다. 스텁은 멈추지 않도록 실패 코드를 돌려준다 (in-place 를 "죽을 때까지" 돌리는 루프도 첫 회에 끝난다). 처음 실행 결과 (실제 출력, 중간 생략):

```text
cell  bits  P/E      tR   tPROG  tBERS  NOP  order
SLC   1   100000    25    200   2000   4   skip-fwd
…
FAIL starter.c:249  rfail == 0 && not_ff == 0
FAIL starter.c:250  NA.n_read == 64 && NA.busy_us[0] == 32u * 70 && NA.busy_us[1] == 32u * 70
…
  TLC k=0 p=0 got ERR_PARAM exp OK
…
in-place on TLC block 0: 0 updates, then ERR_PARAM (pe=0)
FAIL starter.c:464  updates == 1500 && last == NAND_ERR_WORN_OUT && NA.state[0] == BLK_WORN
18/87 passed
```

(exit 1. 통과한 18개는 `nand_init` 의 지오메트리 검사 · 셀 표처럼 주어진 부분과, 스텁의 실패 코드가 우연히 맞은 경우다.)

## 4. 힌트

<details><summary>힌트 1 — 저장소 한 칸 = data ‖ OOB</summary>

`page_ptr(n, blk, page)` 가 그 페이지의 data 시작을 주고, OOB 는 바로 뒤 `+ page_bytes` 에 붙어 있다. read 는 두 번의 `memcpy`, erase 는 블록 전체(페이지 수 × (page + OOB) 바이트)에 `memset(0xFF)` 한 번이다.

</details>

<details><summary>힌트 2 — program 은 대입이 아니라 AND</summary>

`s[i] = data[i]` 가 아니라 `s[i] &= data[i]`. 지운 페이지(0xFF)에서는 둘이 같지만, partial program(NOP > 1)에서 차이가 난다. "건드리지 않을 바이트는 0xFF 를 쓴다" 가 실제 partial page program 의 동작이다.

</details>

<details><summary>힌트 3 — 판정은 "이 페이지에 데이터가 있나?" 부터</summary>

블록마다 쓰기 포인터 `next_page` (= 마지막으로 쓴 페이지 + 1), 페이지마다 `prog_cnt` 를 둔다. `prog_cnt > 0` 이면 이미 데이터가 있는 페이지 → "마지막 페이지이고 횟수 < NOP" 일 때만 OK, 아니면 NOT_ERASED. `prog_cnt == 0` 인데 `page < next_page` 면 건너뛴 페이지로 되돌아가는 것 → OUT_OF_ORDER. 엄격 모드면 `page != next_page` 도 OUT_OF_ORDER.

</details>

<details><summary>힌트 4 — 마모는 erase 에서 판정</summary>

`pe` 는 성공한 erase 횟수. `pe >= endurance` 인 블록에 erase 가 오면 state = WORN, tBERS 를 더하고 `ERR_WORN_OUT`. `program_check` 는 WORN 블록이면 `ERR_WORN_OUT` 을 돌려주고, `nand_program` 은 그 경우에만 tPROG 를 더한다 (칩이 시도했다가 FAIL).

</details>

<details><summary>힌트 5 — 로그 쓰기의 "다음 빈 페이지"</summary>

`for (;;)` 로 활성 블록이 범위 밖이면 `ERR_RANGE`, 활성 블록에 자리가 있고 BBT 에서 양품이면 멈춤, 아니면 다음 블록 · `wp = 0`. program 이 성공한 **뒤에** 맵을 바꾼다. 이전 위치가 있었다면 그 페이지는 이제 stale.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. 셀 물리: 지움 = 1, program = 1→0 만, erase 는 웰 공유라 블록 단위 → **erase-before-write**.
2. 블록 안 순서 규칙과 NOP — 이웃 셀 간섭과 페어 페이지 때문.
3. 수명: P/E 마다 산화막 손상 → RBER 증가 → 정격 P/E. SLC ≫ TLC ≫ QLC.
4. 결과: 덮어쓰기 = 다른 곳에 쓰고 매핑을 바꾼다 (out-of-place) → L2P, stale 페이지, GC, WAF, 웨어 레벨링, 불량 블록 관리 = FTL.

### Part A — 저장과 시간

모델은 `store[blk][page] = data ‖ oob` 하나의 정적 배열이다. 상태 변수는 블록마다 `next_page`(2 B) · `pe`(4 B) · `state`(1 B), 페이지마다 `prog_cnt`(1 B). 실제 FTL 펌웨어도 블록당 수 바이트의 메타데이터(쓰기 포인터, erase count, 상태)를 RAM 에 들고 있고, 이 레슨의 구조가 그 축소판이다.

program 이 `&=` 인 것이 핵심이다. 지운 페이지(0xFF)에 쓰면 결과는 입력 그대로지만, 이미 0 이 된 비트는 다시 1 로 못 올린다. solution 의 테스트는 SLC 페이지에 `0x0F` 를 쓰고 같은 페이지에 `0xF0` 을 partial program 하면 `0x00` 이 되는 것(합집합이 아니라 교집합)을 확인한다.

바쁜 시간은 plane 별로 더한다. 블록 b 는 plane `b % 2` — ONFI 계열 칩에서 plane 선택이 블록 주소의 하위 비트인 관례를 따른 것이다 (연습 1). 서로 다른 plane 의 명령은 (multi-plane 명령으로) 겹쳐 돌 수 있으므로, 워크로드가 걸리는 시간의 하한은 plane 별 합의 최댓값(`makespan`)이다. 테스트의 정확한 합:

| 명령 | plane | 더해지는 µs (TLC) |
|---|---|---|
| program (0,0), (0,1) | 0 | 1 000 + 1 000 |
| read (0,1) | 0 | 70 |
| erase 0 | 0 | 3 500 |
| program (5,0) | 1 | 1 000 |
| program (5,0) 다시 → NOT_ERASED | 1 | 0 (칩에 안 감) |
| 합 | | plane0 = 5 570, plane1 = 1 000, makespan = 5 570 |

### Part B — 판정 우선순위 표

| 순서 | 조건 | 결과 | 이유 |
|---|---|---|---|
| 0 | `blk`/`page` 범위 밖, `data == NULL` | ERR_RANGE / ERR_PARAM | 주소부터 틀림 |
| 1 | 블록이 공장 불량 | ERR_BAD_BLOCK | 마커를 지키는 것이 최우선 |
| 2 | 블록이 WORN | ERR_WORN_OUT (+tPROG) | 칩이 시도했다가 status FAIL |
| 3 | `prog_cnt > 0` 이고 (마지막 페이지 아님 또는 `prog_cnt ≥ NOP`) | ERR_NOT_ERASED | 데이터가 있는 페이지 |
| 3' | `prog_cnt > 0`, 마지막 페이지, `prog_cnt < NOP` | OK (partial) | SLC NOP = 4 |
| 4 | `prog_cnt == 0`, `page < next_page` | ERR_OUT_OF_ORDER | 건너뛴 페이지로 되돌아감 |
| 5 | 엄격 모드, `page > next_page` | ERR_OUT_OF_ORDER | 앞으로 건너뛰기 금지 |
| 6 | 그 외 | OK | |

**NOT_ERASED 와 OUT_OF_ORDER 를 가르는 기준**: "그 페이지에 데이터가 있나?" 이다. 있으면 NOT_ERASED (덮어쓰기 시도), 없는데 못 쓰면 OUT_OF_ORDER (순서 문제). 이렇게 나누면 드라이버 버그를 로그만 보고 분류할 수 있다 — "같은 LBA 를 제자리에 다시 썼다" vs "쓰기 포인터 관리가 틀렸다".

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="mb12a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">블록 한 개, 각 페이지에 program 하면 돌아오는 코드</text>
  <text x="10" y="62" fill="currentColor">TLC</text>
  <text x="10" y="80" fill="currentColor" font-size="12">p0..2 씀</text>
  <text x="10" y="162" fill="currentColor">SLC</text>
  <text x="10" y="180" fill="currentColor" font-size="12">p0, p3 씀</text>
  <g stroke="currentColor">
    <rect x="90" y="50" width="80" height="44" class="f-acc"/><rect x="170" y="50" width="80" height="44" class="f-acc"/>
    <rect x="250" y="50" width="80" height="44" class="f-acc"/><rect x="330" y="50" width="80" height="44" class="f-ok"/>
    <rect x="410" y="50" width="80" height="44" class="f-warn"/><rect x="490" y="50" width="80" height="44" class="f-warn"/>
    <rect x="570" y="50" width="80" height="44" class="f-warn"/><rect x="650" y="50" width="80" height="44" class="f-warn"/>
    <rect x="90" y="150" width="80" height="44" class="f-acc"/><rect x="170" y="150" width="80" height="44" class="f-warn"/>
    <rect x="250" y="150" width="80" height="44" class="f-warn"/><rect x="330" y="150" width="80" height="44" class="f-ok"/>
    <rect x="410" y="150" width="80" height="44" class="f-ok"/><rect x="490" y="150" width="80" height="44" class="f-ok"/>
    <rect x="570" y="150" width="80" height="44" class="f-ok"/><rect x="650" y="150" width="80" height="44" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="130" y="68">p0</text><text x="210" y="68">p1</text><text x="290" y="68">p2</text><text x="370" y="68">p3</text>
    <text x="450" y="68">p4</text><text x="530" y="68">p5</text><text x="610" y="68">p6</text><text x="690" y="68">p7</text>
    <text x="130" y="86">NE</text><text x="210" y="86">NE</text><text x="290" y="86">NE</text><text x="370" y="86">OK</text>
    <text x="450" y="86">OOO</text><text x="530" y="86">OOO</text><text x="610" y="86">OOO</text><text x="690" y="86">OOO</text>
    <text x="130" y="168">p0</text><text x="210" y="168">p1</text><text x="290" y="168">p2</text><text x="370" y="168">p3</text>
    <text x="450" y="168">p4</text><text x="530" y="168">p5</text><text x="610" y="168">p6</text><text x="690" y="168">p7</text>
    <text x="130" y="186">NE</text><text x="210" y="186">OOO</text><text x="290" y="186">OOO</text><text x="370" y="186">OK*</text>
    <text x="450" y="186">OK</text><text x="530" y="186">OK</text><text x="610" y="186">OK</text><text x="690" y="186">OK</text>
  </g>
  <path d="M370 32 L370 48" stroke="currentColor" marker-end="url(#mb12a)"/>
  <text x="378" y="40" fill="currentColor" font-size="12">next_page = 3 (엄격: 여기만)</text>
  <path d="M450 116 L450 148" stroke="currentColor" marker-end="url(#mb12a)"/>
  <text x="458" y="128" fill="currentColor" font-size="12">next_page = 4 (건너뛰기 허용: 여기부터 끝까지)</text>
  <g font-size="12" fill="currentColor">
    <rect x="10" y="222" width="16" height="14" class="f-acc" stroke="currentColor"/><text x="32" y="234">NE = ERR_NOT_ERASED: 데이터가 있는 페이지 (마지막 페이지가 아니거나 NOP 소진)</text>
    <rect x="10" y="244" width="16" height="14" class="f-warn" stroke="currentColor"/><text x="32" y="256">OOO = ERR_OUT_OF_ORDER: 지워져 있지만 쓰기 포인터 뒤(건너뛴 페이지) 또는 엄격 모드에서 앞</text>
    <rect x="10" y="266" width="16" height="14" class="f-ok" stroke="currentColor"/><text x="32" y="278">OK* = 마지막으로 쓴 페이지에 partial program (SLC NOP = 4, 지금 2/4) — 결과는 AND</text>
  </g>
</svg>
```

**한 블록에서의 추적 (SLC — 1~6 단계는 solution 의 Part B 테스트와 같은 순서):**

| 단계 | 명령 | 판정 근거 | rc | next_page | prog_cnt[p0..p3] |
|---|---|---|---|---|---|
| 1 | program p0 | cnt 0, p = next = 0 | OK | 1 | 1 0 0 0 |
| 2 | program p3 | cnt 0, p > next, SLC 는 건너뛰기 허용 | OK | 4 | 1 0 0 1 |
| 3 | program p1 | cnt 0, p < next (건너뛴 페이지) | OUT_OF_ORDER | 4 | 1 0 0 1 |
| 4 | program p2 | 같은 이유 | OUT_OF_ORDER | 4 | 1 0 0 1 |
| 5 | program p0 | cnt 1, 마지막 페이지(p3) 아님 | NOT_ERASED | 4 | 1 0 0 1 |
| 6 | program p3 | cnt 1 < NOP 4, 마지막 페이지 | OK (partial) | 4 | 1 0 0 2 |
| 7 | erase | pe 0 → 1 | OK | 0 | 0 0 0 0 |

**전수 표 검증.** 블록이 page 0..k−1 까지 차 있을 때 page p 에 program 하는 경우를 모두(k = 0..8, p = 0..7) 돌린다. TLC 의 기대값은 `p < k → NE, p == k → OK, p > k → OOO`, SLC 는 `p == k−1 → OK(partial), p < k−1 → NE, p ≥ k → OK`. 144 경우 모두 일치 (`tbl_bad == 0`). 기대값 식이 구현(우선순위 if 문)과 다른 모양이라, 둘이 같은 실수를 할 가능성이 낮다.

**왜 블록 안에서 순서대로 써야 하나.** (1) **셀 간 간섭(cell-to-cell interference)**: 이웃 워드라인을 program 하면 그 전하의 결합(coupling)으로 이미 쓴 셀의 Vt 가 조금 밀린다. 칩의 program 순서(그리고 MLC/TLC 의 다단계 program 알고리즘)는 "다음 워드라인이 이전 것을 밀어 놓는 양" 을 전제로 설계돼 있어, 순서를 바꾸면 이미 쓴 페이지의 Vt 분포가 넓어져 RBER 이 오른다. (2) **페어 페이지(paired pages)**: MLC 의 lower/upper 페이지(TLC 는 lower/middle/upper)는 같은 셀을 공유한다. upper 를 쓰는 중에 전원이 끊기면 앞서 쓴 lower 페이지까지 망가질 수 있다 — FTL 이 페어 페이지 단위로 커밋을 관리하는 이유다 (MEM-M14). (3) 3D TLC/QLC 는 워드라인의 여러 페이지를 한 번에 쓰는(one-shot) 방식이 많아, 칩 쪽 순서가 더 엄격하다. 순서와 페어링 표는 벤더 데이터시트(대개 NDA) 사항이다.

**왜 NOP = 1 인가 (MLC 이상).** 같은 페이지를 다시 program 하면 그 워드라인 전체에 program 전압이 또 걸려(program disturb) 이웃 셀의 Vt 가 밀린다. 셀당 여러 비트를 좁은 Vt 간격으로 구분하는 MLC/TLC 에서는 감당할 여유가 없다. SLC 는 상태가 둘뿐이라 간격이 넓어 몇 번(NOP 4 정도)은 견딘다. 옛 SLC 파일시스템(YAFFS1)이 OOB 의 "삭제됨" 바이트를 다시 program 해 0 으로 만드는 트릭을 쓴 것도 이 여유 덕분이다 (연습 2).

**마모의 결정적 모델.** 셀 타입마다 정격 P/E 만큼 (program + erase) 을 반복하고 한 번 더 erase 하면 (실제 출력):

```text
wear SLC: 100000 P/E ok, next erase=ERR_WORN_OUT, program=ERR_WORN_OUT, erase=ERR_WORN_OUT, pe=100000
wear MLC: 3000 P/E ok, next erase=ERR_WORN_OUT, program=ERR_WORN_OUT, erase=ERR_WORN_OUT, pe=3000
wear TLC: 1500 P/E ok, next erase=ERR_WORN_OUT, program=ERR_WORN_OUT, erase=ERR_WORN_OUT, pe=1500
wear QLC: 500 P/E ok, next erase=ERR_WORN_OUT, program=ERR_WORN_OUT, erase=ERR_WORN_OUT, pe=500
```

FAIL 난 erase 두 번과 program 한 번도 바쁜 시간에 들어간다 (`2·tBERS + tPROG`). 실제 칩에서 erase/program 실패는 status 레지스터의 FAIL 비트(ONFI READ STATUS 의 bit 0)로 알 수 있고, 펌웨어는 그 블록을 grown bad 로 등록하고 데이터를 옮긴다 (MEM-M13). 반대로 **규칙 위반(NOT_ERASED · OUT_OF_ORDER)은 실제 칩이 알려주지 않는다** — 그냥 AND 로 덮어쓰거나 이웃을 망가뜨린 채 status PASS 를 줄 수 있다. 이 모델이 그 위반을 에러로 돌려주는 것은 칩이 아니라 **검사기(sanitizer)** 역할이고, Linux 의 `nandsim` 같은 시뮬레이터나 FTL 단위 테스트에서 쓰는 방식이다.

### Part C — 드라이버 계약

**공장 불량 블록 스캔.** 출하 시 불량인 블록은 제조사가 OOB 의 정해진 바이트를 0x00(≠ 0xFF)으로 표시해 둔다. 모델은 "page 0 의 OOB byte 0" 으로 단순화했다. 실제 위치는 부품마다 다르다 — ONFI 는 첫 페이지(또는 마지막 페이지)의 spare 첫 바이트를 쓰는 것으로 알려져 있고, 벤더에 따라 두 번째 페이지나 마지막 페이지를 쓰기도 한다 (Linux MTD 의 `NAND_BBM_FIRSTPAGE / SECONDPAGE / LASTPAGE` 옵션이 이 차이를 다룬다). 데이터시트 확인. 계약 세 가지:

1. **처음 erase 하기 전에** 스캔한다. erase 는 마커까지 0xFF 로 만들어 그 블록이 불량이었다는 정보를 영영 지운다. 모델은 공장 불량 블록의 erase 를 `ERR_BAD_BLOCK` 으로 막는다.
2. 스캔 결과(BBT)는 다른 곳에 저장한다. Linux `nand_bbt.c` 는 칩 마지막 블록들에 패턴(`Bbt0`, 미러 `1tbB`)을 붙인 플래시 BBT 를 두고, SSD 펌웨어는 자체 메타데이터 영역에 둔다. 이후 생기는 grown bad 블록도 여기에 더한다.
3. **OOB byte 0 은 예약석**이다. 사용자 메타데이터를 거기에 쓰면 다음 부팅의 스캔이 양품을 불량으로 오판한다. solution 은 양품 블록 1 의 page 0 OOB byte 0 에 0x00 을 써서 스캔 결과가 2 → 3 이 되는 것을 확인한다. 그래서 로그 쓰기는 LBA 를 byte 1..4 에 적는다.

스캔은 블록당 OOB 읽기 1번 = 블록 수 × tR. 이 모델(8 블록)에서 plane 당 4 × 70 = 280 µs.

**in-place vs out-of-place (실제 출력, 결정적 모델 · TLC 대표값).** LBA 0..7 을 채운 뒤 같은 랜덤 순서로 40번 갱신한다. in-place 칩은 블록 0 하나에 LBA i = page i 로 고정, out-of-place 칩은 공장 불량 3 · 6 을 건너뛰며 로그로 쓴다.

```text
40 random updates of 8 LBAs (TLC 대표값, 채우기 이후 증가분)
method        prog  erase  read  busy_p0_us  busy_p1_us  makespan_us  maxPE  stale
in-place RMW   320     40   320      482400           0       482400     40      -
out-of-place    40      0     0       16000       24000        24000      0     40
in-place on TLC block 0: 1500 updates, then ERR_WORN_OUT (pe=1500)
```

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">LBA 8개, 랜덤 갱신 40번 — solution.c 실제 출력 (막대는 각 항목 안에서 in-place = 470 px 기준)</text>
  <text x="10" y="62" fill="currentColor">program 횟수</text>
  <rect x="160" y="40" width="470" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="160" y="62" width="58.8" height="16" class="f-ok" stroke="currentColor"/>
  <text x="638" y="53" fill="currentColor" font-family="monospace" font-size="12">320</text>
  <text x="226" y="75" fill="currentColor" font-family="monospace" font-size="12">40 (WAF 1)</text>
  <text x="10" y="127" fill="currentColor">erase 횟수</text>
  <rect x="160" y="105" width="470" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="160" y="127" width="2" height="16" class="f-ok" stroke="currentColor"/>
  <text x="638" y="118" fill="currentColor" font-family="monospace" font-size="12">40</text>
  <text x="170" y="140" fill="currentColor" font-family="monospace" font-size="12">0 (stale 40 페이지는 GC 몫)</text>
  <text x="10" y="192" fill="currentColor">makespan</text>
  <rect x="160" y="170" width="470" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="160" y="192" width="23.4" height="16" class="f-ok" stroke="currentColor"/>
  <text x="638" y="183" fill="currentColor" font-family="monospace" font-size="12">482.4 ms</text>
  <text x="191" y="205" fill="currentColor" font-family="monospace" font-size="12">24.0 ms (2 plane 에 분산)</text>
  <rect x="160" y="228" width="16" height="14" class="f-warn" stroke="currentColor"/>
  <text x="182" y="240" fill="currentColor" font-size="12">in-place RMW (read 8 → erase → program 8, WAF 8)</text>
  <rect x="470" y="228" width="16" height="14" class="f-ok" stroke="currentColor"/>
  <text x="492" y="240" fill="currentColor" font-size="12">out-of-place 로그</text>
</svg>
```

숫자 읽기:

- **in-place 의 비용은 블록 크기에 비례한다.** 갱신 1번 = read 8 + erase 1 + program 8 = 8·70 + 3 500 + 8·1 000 = 12 060 µs. 40번이면 482 400 µs (= 출력과 정확히 일치). host 가 쓴 페이지 40 개에 NAND program 320 번 → 쓰기 증폭(WAF) = 8 = 블록당 페이지 수. 실제 블록은 수백~천여 페이지라 WAF 도 그만큼 커진다.
- **수명**: in-place 는 갱신마다 같은 블록을 지운다. TLC 대표값에서 블록 0 은 **host 갱신 1 500 번**에 죽는다 (마지막 출력 줄). 16 KiB 페이지라면 고작 1 500 × 16 KiB ≈ 23 MiB 를 쓰고 블록 하나가 끝나는 셈이다.
- **전원 차단**: in-place RMW 는 erase 와 재program 사이에 전원이 끊기면 그 블록의 **LBA 8개가 모두** 사라진다 (RAM 버퍼에만 있었으니까). out-of-place 는 새 사본을 쓰는 중에 끊겨도 옛 사본이 플래시에 남는다 — OOB 의 LBA(+ 시퀀스 번호)로 맵을 다시 만드는 것이 MEM-M14 다.
- **out-of-place 의 대가**: erase 0 회, program = host 쓰기 수(WAF 1)지만 옛 사본 40 페이지가 stale 로 남았다. 8 블록 중 공장 불량 2 개를 빼면 양품 6 블록 × 8 = 48 페이지 = 채우기 8 + 갱신 40 으로 정확히 꽉 찼고, 다음 쓰기는 `ERR_RANGE` (테스트 `L.active == 7 && L.wp == 8`). 공간을 되찾으려면 stale 이 많은 블록을 골라 valid 페이지를 옮기고 지워야 한다 = GC (MEM-M11, DS-A09) — 그때 WAF 가 1 보다 커진다.
- **plane 분산**: 로그는 블록을 번호 순으로 쓰므로 plane 0/1 을 번갈아 쓴다 (블록 1, 5, 7 → plane 1 의 24 000 µs, 블록 2, 4 → plane 0 의 16 000 µs). in-place 는 블록 0 하나라 plane 0 만 일한다. 실제 FTL 은 여러 die · plane 에 동시에 쓰는 "슈퍼블록" 으로 처리량을 얻는다 (연습 4).

**처리량 감각.** TLC 대표값 tPROG 1 000 µs, 페이지 16 KiB 라면 plane 하나의 순수 program 처리량은 16 384 B / 1 ms ≈ 16.4 MB/s 다. GB/s 급 SSD 는 수십 개 die × die 당 여러 plane 을 동시에 돌려서 나온다 — 병렬성이 FTL 설계의 출발점인 이유. (실제 tPROG · 페이지 크기는 데이터시트 확인.)

### 복잡도와 RAM

| 연산 | 시간 (모델) | 칩 시간 (대표) |
|---|---|---|
| read | O(page + OOB) 복사 | tR |
| program | O(page + OOB) AND + O(1) 판정 | tPROG |
| erase | O(블록 바이트) memset | tBERS |
| scan BBT | 블록 수 × OOB 읽기 | 블록 수 × tR |

블록 메타데이터는 블록당 7 B(`next_page` 2 + `pe` 4 + `state` 1), 페이지 메타데이터는 페이지당 1 B(`prog_cnt`). 펌웨어 드라이버라면 `prog_cnt` 는 필요 없고(칩이 강제하지 않으니 디버그 빌드 전용 검사기), 블록당 쓰기 포인터 · erase count · 상태만 RAM 에 둔다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 87/87 passed, exit 0, 약 0.15 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음, 87/87
```

실제 출력 (`solution`) — 위 Part B · C 에 붙인 표 외에:

```text
cell  bits  P/E      tR   tPROG  tBERS  NOP  order
SLC   1   100000    25    200   2000   4   skip-fwd
MLC   2     3000    50    800   3000   1   strict
TLC   3     1500    70   1000   3500   1   strict
QLC   4      500   140   2500   8000   1   strict
…
87/87 passed
```

## 7. 자주 하는 실수

**1. program 을 대입으로 구현한다.** `memcpy(s, data, len)` 이면 모델이 0→1 을 허용해, partial program 이나 "실수로 두 번 쓰기" 의 실제 결과(AND)를 재현하지 못한다. FTL 테스트에서 "두 번 써도 마지막 값이 읽힌다" 는 거짓 안심을 준다.

```c
for (uint32_t i = 0; i < n->g.page_bytes; i++) s[i] &= data[i];   /* 셀은 1→0 만 */
```

**2. NOT_ERASED 와 OUT_OF_ORDER 를 섞는다.** "page < next_page 면 무조건 OUT_OF_ORDER" 로 짜면 SLC 의 partial program(마지막 페이지 재기록)이 막히고, "같은 LBA 를 제자리에 다시 썼다" 는 버그가 순서 문제로 잘못 분류된다. 먼저 `prog_cnt` (데이터가 있나) 를 본다.

**3. 거절된 명령으로 상태를 바꾼다.** 판정 전에 `prog_cnt++` 나 `busy_us +=` 를 하면, 실패한 명령이 쓰기 포인터를 밀거나 시간을 먹는다. 테스트 `NA.next_page[1] == 1` (범위 밖 명령 뒤에도 그대로) 와 `busy_before + 70` 이 이것을 잡는다.

**4. erase 전에 BBT 를 안 만든다.** 새 보드 bring-up 에서 "일단 전체 erase" 스크립트를 먼저 돌리면 공장 불량 마커가 다 지워진다. 그 뒤로는 어떤 블록이 원래 불량이었는지 알 길이 없어(제조사 테스트 결과가 사라짐) 런타임에 하나씩 grown bad 로 발견하게 된다. U-Boot 의 `nand scrub` 이 경고를 띄우는 이유다.

**5. OOB byte 0 에 메타데이터를 쓴다.** 위 Part C 처럼 다음 스캔이 양품을 불량으로 본다. OOB 레이아웃은 "BBM 자리 · ECC 패리티 · 자유 바이트" 를 먼저 정하고 (Linux 의 `mtd_ooblayout_ops`), 메타데이터는 자유 바이트에만 둔다.

**6. P/E 를 program 에서 센다.** P/E 사이클은 erase 횟수로 센다. program 마다 세면 페이지 수만큼 부풀려져 웨어 레벨링(MEM-M12) 판단이 틀어진다.

**7. 마모를 "카운터 = 정격이면 즉시 죽음" 으로만 이해한다.** 모델은 절벽이지만 실제는 RBER 이 서서히 오르고, 정격은 ECC 강도 · 보존 기간 · 온도 조건과 함께 정의된다. 정격을 넘겨도 한동안 동작하는 칩이 많고, 반대로 정격 전에도 보존 기간이 짧아진다 — 그래서 펌웨어는 카운터만이 아니라 ECC 정정 비트 수를 함께 본다 (MEM-M13).

## 8. 면접 Q&A

<details><summary>Q1. NAND 는 왜 덮어쓸 수 없고, erase 는 왜 블록 단위인가요?</summary>

program 은 셀에 전자를 넣어 Vt 를 올리는 방향(1→0)만 가능하고, 되돌리려면 전자를 빼는 erase 가 필요하다. erase 는 높은 전압을 셀들의 공통 웰(기판)에 걸어 한꺼번에 하는 방식이라, 웰을 공유하는 블록 전체가 단위가 된다. 페이지마다 웰을 나누면 면적이 커져 NAND 의 존재 이유(집적도)가 사라진다. 결과적으로 "페이지 하나 갱신" = 다른 곳에 쓰고 매핑을 바꾸는 out-of-place 쓰기가 되고, 이것이 FTL 이 존재하는 이유다.

</details>

<details><summary>Q2. 블록 안 페이지를 왜 순서대로 program 해야 하나요? 건너뛰면?</summary>

이웃 워드라인을 program 할 때의 결합(coupling)으로 이미 쓴 셀의 Vt 가 밀리는데, 칩의 program 알고리즘과 순서가 그것을 전제로 보정하도록 설계돼 있다. 순서를 어기면 이미 쓴 페이지의 오류율이 오른다. MLC/TLC 는 한 셀을 여러 페이지(lower/upper …)가 공유해서 순서가 곧 셀의 program 단계이고, 3D TLC/QLC 는 워드라인 단위 one-shot program 이라 더 엄격하다. 건너뛴 페이지는 그 erase 사이클 동안 버려진다(다시 돌아가 쓰면 안 됨). 꼬리질문 "ZNS SSD 의 zone 이 순차 쓰기만 받는 이유?" → 같은 제약을 host 까지 올려 FTL 매핑 · GC 를 줄이는 설계다 (STOR-A02).

</details>

<details><summary>Q3. erase-before-write 가 FTL 에 무엇을 강요하나요?</summary>

(1) 논리→물리 매핑 표(L2P)와 역방향 정보(P2L, OOB 에 LBA 기록), (2) 덮어쓴 옛 페이지가 stale 로 쌓이므로 GC — victim 블록의 valid 페이지를 옮기고 지움, 그 복사량이 쓰기 증폭(WAF), (3) 여유 공간(over-provisioning)이 클수록 GC 비용이 줄어드는 trade-off, (4) erase 가 특정 블록에 몰리지 않게 하는 웨어 레벨링, (5) 전원 차단 시 매핑 복구(체크포인트 + OOB 스캔). 이 레슨의 측정으로 말하면: in-place 는 갱신 1번에 erase 1번 · program 8번(WAF = 블록당 페이지 수), out-of-place 는 program 1번 · erase 0번이지만 stale 40 페이지를 GC 가 치워야 한다. 각각 MEM-M10/M11/M12/M14, DS-A09.

</details>

<details><summary>Q4. SLC · TLC · QLC 의 내구도 차이는 왜 생기고, SLC 캐시는 왜 쓰나요?</summary>

셀당 비트가 늘면 같은 Vt 범위를 2 → 8 → 16 개 상태로 나눠야 해서 상태 간 간격이 좁아진다. P/E 로 산화막에 갇힌 전하 · 손상이 쌓이면 Vt 분포가 넓어지고 이동하는데, 간격이 좁을수록 더 빨리 겹친다 → 대표값 SLC 100 K, MLC 3 K, TLC 1.5 K, QLC 0.5 K 수준. 같은 이유로 tPROG 도 길어진다(정밀한 다단계 program-verify). SLC 캐시는 TLC/QLC 블록 일부를 셀당 1비트로 써서 빠른 쓰기와 높은 내구도를 얻고, 나중에 TLC/QLC 로 옮긴다(folding). 용량은 1/3 · 1/4 이 된다 (MEM-A09). 수치는 세대 · 벤더별, 데이터시트 확인.

</details>

<details><summary>Q5. 1 TB TLC SSD 의 TBW 를 대충 계산해 보세요.</summary>

TBW ≈ 용량 × 정격 P/E / WAF. 1 TB × 1 500 / 3 = 500 TB. 5 년 보증이면 DWPD = 500 / (1 × 365 × 5) ≈ 0.27 (연습 3 이 정확한 정수 계산). WAF 는 워크로드(순차 ≈ 1, 랜덤 작은 쓰기 ≫ 1)와 OP 에 달려 있어 가정을 말해야 한다. 정격의 정의는 JEDEC JESD218 (client: 보존 기간 1 년 · 30 °C 수준, enterprise: 3 개월 · 40 °C 수준 — 원문 확인 권장)처럼 "정격 TBW 를 쓴 뒤에도 그 조건에서 데이터가 보존" 이다.

</details>

<details><summary>Q6. 공장 불량 블록 스캔은 왜 첫 erase 전에 해야 하고, 어떻게 하나요?</summary>

제조사가 불량 블록의 OOB 정해진 위치에 0x00 을 써 두는데, erase 하면 그 마커가 0xFF 로 지워져 정보가 사라진다. 그래서 출하 직후 첫 부팅(또는 공장 펌웨어)에서 모든 블록의 해당 OOB 바이트만 읽어 BBT 를 만들고, BBT 자체를 별도 영역(Linux 는 마지막 블록들의 `Bbt0`/`1tbB` 패턴, SSD 는 시스템 영역)에 이중으로 저장한다. 마커 위치(첫/두 번째/마지막 페이지, byte 0 또는 5)는 벤더별이다. 스캔 비용은 블록 수 × tR (수천 블록 × 수십 µs = 수백 ms 수준).

</details>

<details><summary>Q7. 실제 칩은 "이미 쓴 페이지에 또 program" 을 거부하나요?</summary>

일반적으로 아니다. 칩은 받은 대로 program 펄스를 걸고(결과는 대략 AND), 이웃 셀이 disturb 를 받아도 status 는 PASS 일 수 있다. status FAIL 은 program/erase 가 verify 를 못 통과했을 때(대개 마모 · 결함)다. 그래서 규칙 위반은 펌웨어가 스스로 막아야 하고, 이 레슨의 모델처럼 위반을 에러로 돌려주는 시뮬레이터(Linux `nandsim`, FTL 단위 테스트의 모의 NAND)로 드라이버를 검증한다. 꼬리질문 "그럼 FAIL 은 어떻게 처리?" → 그 블록을 grown bad 로 BBT 에 추가, 이미 쓴 valid 데이터를 다른 블록으로 옮김, program FAIL 이면 그 데이터를 다른 페이지에 다시 씀 (MEM-M13).

</details>

<details><summary>Q8. NAND 쓰기 처리량은 어디서 나오나요? plane 은 왜 블록 주소 하위 비트로 고르죠?</summary>

plane 하나는 tPROG 동안 페이지 하나밖에 못 쓴다 (대표값 16 KiB / 1 ms ≈ 16 MB/s). 처리량은 die 안의 plane 을 multi-plane 명령으로 동시에, 그리고 여러 die · 채널을 interleave 해서 얻는다. plane 을 블록 주소 하위 비트로 고르면 연속한 블록 번호가 plane 을 번갈아 가므로, "블록 2k 와 2k+1 을 짝지어 multi-plane program" 하기가 쉽다 — 다만 multi-plane 명령에는 같은 페이지 번호 등 짝 조건이 있다 (데이터시트 확인). 이 레슨의 로그 측정에서도 블록을 번호 순으로 쓰는 것만으로 두 plane 에 일이 나뉘어 makespan 이 24 ms 였다.

</details>

## 9. 추가 연습

### 연습 1 — row 주소 pack/unpack

ONFI 식 row 주소 = `block << page_bits | page` (`page_bits = ceil(log2(pages_per_block))`). `row_pack` · `row_unpack` 을 쓰고, 블록당 페이지가 2의 거듭제곱이 아닐 때(예: 1152) 생기는 "주소 구멍" 을 거부하라. 예: ppb 1152, 블록 0x123, page 5 → row `0x91805`.

<details><summary>답 보기</summary>

`page_bits(1152) = 11` (2048 ≥ 1152). pack 은 범위 검사 후 `blk << 11 | page`, unpack 은 하위 11비트 = page, 나머지 = block 이고 page ≥ ppb 면 −1 (row 공간에 구멍이 생긴다 — 주소를 연속 정수로 다루는 코드가 이 구멍을 밟는다). ppb = 12, 블록 16 개 전수: row 256 개 중 64 개(블록마다 page 12..15)가 구멍이고 나머지는 왕복이 정확. 4-plane 칩이면 plane = 블록 주소 하위 2비트 (`0x123 & 3 = 3`). 실제 row 주소에는 LUN 비트가 위에 더 붙고 3 사이클로 보낸다 (ONFI, 데이터시트 확인).

</details>

### 연습 2 — 지우지 않고 갈 수 있나?

`can_program_over(old, new, len)` 과 바꿔야 할 비트 수 `bits_to_clear` 를 쓰라. 0x30 → 0x31 은 왜 안 되나? OOB 의 "삭제 마커" 바이트를 0xFF → 0x00 으로 바꾸는 트릭은 언제 쓸 수 있나?

<details><summary>답 보기</summary>

가능 조건은 바이트마다 `(old & new) == new` — new 의 1 이 old 에서도 1 이어야 한다. 0x30 → 0x31 은 bit 0 을 0→1 로 올려야 해서 erase 없이는 불가. 삭제 마커(0xFF → 0x00, 8비트 clear)는 조건을 만족하지만 같은 페이지를 두 번째로 program 하는 것이므로 NOP ≥ 2 인 SLC 에서만 쓸 수 있다 (YAFFS1 의 방식, MLC 로 오면서 YAFFS2 는 시퀀스 번호로 바꿨다). 되살리기(0x00 → 0xFF)는 불가.

</details>

### 연습 3 — TBW 와 DWPD

`tbw_bytes(cap, pe, waf_x100)` 와 `dwpd_x1000(tbw, cap, years)` 를 정수로. 1 TB TLC (1 500 P/E, WAF 3.0, 5 년), 2 TB QLC (500 P/E, WAF 2.5, 5 년).

<details><summary>답 보기</summary>

TBW = cap × pe × 100 / waf_x100 (곱 먼저, 나눗셈 나중 — 2e12 × 500 × 100 = 1e17 은 u64 안). 1 TB TLC → 500 TB, DWPD = 500 / 1825 = 0.273. 2 TB QLC → 400 TB, DWPD = 0.109. QLC 드라이브가 "읽기 위주" 로 팔리는 이유가 숫자로 보인다. WAF 0 은 0 을 돌려 나눗셈 0 을 막는다.

</details>

### 연습 4 — plane 스트라이핑

페이지 N 개를 plane 하나에 몰아 쓸 때와 P 개 plane 에 round-robin 할 때의 makespan 과 처리량(B/s)을 구하라. 16 KiB, tPROG 1 000 µs, 4 plane, 64 페이지 · 65 페이지.

<details><summary>답 보기</summary>

몰아 쓰기 = N · tPROG, 스트라이핑 = ceil(N / P) · tPROG. 64 페이지: 64 ms → 16 384 000 B/s, 16 ms → 65 536 000 B/s (4 배). 65 페이지는 17 ms — 마지막 한 장 때문에 한 바퀴가 더 든다 (슈퍼페이지를 꽉 채워 쓰려는 이유). 공식은 page i → plane i % P 로 바쁜 시간을 더하는 작은 시뮬레이션과 N = 0..100, P = 1..8 전수 비교로 확인한다.

</details>

### 연습 5 — OOB 예산: BCH 패리티가 들어가나

GF(2^m) 위의 BCH 는 t 비트 정정에 패리티 m · t 비트가 든다 (코드워드 길이 ≤ 2^m − 1). 섹터 크기 · t 로 패리티 바이트를 구하고, 페이지 + OOB 에 들어가는지 판정하라. 2 KiB + 64 B 페이지에 512 B 당 BCH-8 / BCH-16 (메타 2 B), 16 KiB + 2 048 B(가정) 페이지에 1 KiB 당 t = 40 / 72 (메타 64 B).

<details><summary>답 보기</summary>

512 B = 4 096 비트 → m = 13 (8 191 ≥ 4 096 + 13t). BCH-4 = 52 비트 → 7 B, BCH-8 = 104 비트 → 13 B, BCH-16 = 26 B. 2 KiB 페이지 = 섹터 4 개: BCH-8 은 52 + 2 = 54 ≤ 64 → 들어감, BCH-16 은 106 > 64 → 안 들어감 (고전 SLC 2 KiB + 64 B 칩이 BCH-4 ~ BCH-8 을 쓴 이유). 1 KiB = 8 192 비트 → m = 14: t = 40 → 70 B × 16 섹터 = 1 120 + 64 ≤ 2 048, t = 72 → 126 B × 16 = 2 016 + 64 > 2 048. 실제 TLC/QLC 는 BCH 대신 LDPC 를 많이 쓰고 OOB 크기도 벤더별이다 (MEM-B07 의 SECDED 와 같은 "패리티 비트 예산" 계산을 큰 t 로 한 것).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 26/26 passed
```

## 10. 핵심 정리

- 셀 물리 → 규칙: 지움 = 1(0xFF), program = 1→0 만(저장값 &= 입력), erase = 블록 단위(웰 공유). 덮어쓰기 = erase 가 필요.
- 블록 안 순서: 쓰기 포인터 `next_page`. MLC 이상은 엄격한 순차, NOP = 1. 이웃 간섭 · 페어 페이지 · one-shot program 때문.
- 판정 순서: RANGE → BAD_BLOCK → WORN_OUT → (데이터 있음) NOT_ERASED / partial OK → (지워짐) OUT_OF_ORDER → OK. 거절은 상태 · 시간 불변.
- 실제 칩은 규칙 위반을 알려주지 않는다 (조용한 손상). status FAIL 은 마모 · 결함일 때뿐 → 펌웨어가 지키고, 모델은 검사기.
- 공장 불량: 첫 erase 전에 OOB 마커 스캔 → BBT 별도 저장. OOB byte 0(마커 자리)은 비워 둔다.
- 측정 (TLC 대표값, 8 페이지 블록, 갱신 40번): in-place RMW = program 320 · erase 40 · 482.4 ms, out-of-place = program 40 · erase 0 · 24 ms (stale 40 → GC 몫). in-place 만 쓰면 TLC 블록은 갱신 1 500 번에 죽는다.
- 대표값: P/E SLC 100 K / MLC 3 K / TLC 1.5 K / QLC 0.5 K, tPROG 0.2 / 0.8 / 1 / 2.5 ms, tBERS 수 ms. 처리량은 plane · die 병렬성에서 나온다 (plane 당 ≈ 16 MB/s).

## 참고자료

- ONFI (Open NAND Flash Interface) Specification — 명령 세트, row/column 주소, READ STATUS 비트, 공장 불량 표시 규칙: <https://www.onfi.org/specifications> (최신 판의 절 번호는 원문 확인)
- R. Micheloni, L. Crippa, A. Marelli, *Inside NAND Flash Memories*, Springer 2010 — 셀 물리, program/erase, 셀 간 간섭, 페어 페이지.
- Y. Cai, S. Ghose, E. F. Haratsch, Y. Luo, O. Mutlu, "Error Characterization, Mitigation, and Recovery in Flash-Memory-Based Solid-State Drives", Proc. IEEE 105(9), 2017 — P/E 에 따른 RBER, program interference, read disturb, retention (MEM-M13 의 근거).
- N. Agrawal et al., "Design Tradeoffs for SSD Performance", USENIX ATC 2008 — plane/die 병렬성, 페이지 매핑, GC 비용.
- JEDEC JESD218 (SSD 내구성 요구 · 시험 방법), JESD219 (내구성 워크로드) — TBW 정의와 보존 조건 (유료 문서, 대표 조건은 원문 확인 권장).
- Linux MTD: `drivers/mtd/nand/raw/nand_bbt.c` (플래시 BBT, `Bbt0`/`1tbB` 패턴, `NAND_BBM_*` 마커 위치 옵션), `nandsim.c` (NAND 시뮬레이터), `mtd_ooblayout_ops` (OOB 레이아웃): <https://docs.kernel.org/driver-api/mtdnand.html>
- 대표 SLC NAND 데이터시트(예: Micron · Kioxia 의 2 Gb/4 Gb SLC 부품) — NOP, "페이지는 블록 안에서 순서대로 program" 문구, tR/tPROG/tBERS 표. 수치는 부품마다 다르므로 실제 부품 문서 확인.
- 관련 레슨: DS-A09 (FTL 매핑 테이블 · GC victim 자료구조), MEM-B07 (SECDED — 패리티 예산), MEM-M10 (page-level L2P), MEM-M11 (GC · WAF), MEM-M12 (웨어 레벨링), MEM-M13 (불량 블록 · RBER · read retry), MEM-M14 (전원 차단 복구), MEM-A09 (DFTL · SLC 캐시), STOR-A01 (앱 WA × FTL WA), STOR-A02 (ZNS), STOR-A03 (LittleFS 웨어 레벨링), SIV-M14 (ECC 오류 주입 · CE/UE)
