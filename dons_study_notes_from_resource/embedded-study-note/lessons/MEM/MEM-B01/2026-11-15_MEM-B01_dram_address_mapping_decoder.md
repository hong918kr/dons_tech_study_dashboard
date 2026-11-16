---
id: MEM-B01
time: 60분
updated: 2026-11-15
---
# MEM-B01 DRAM 조직: channel/rank/bank/row/column

## 학습 목표

- DRAM 계층 — 채널(channel) · 랭크(rank) · 뱅크 그룹(bank group, BG) · 뱅크(bank) · 행(row) · 열(column) — 을 칩 수, 비트 수, 바이트 수로 말할 수 있다 (x8 8 Gb 칩 → 1 KB 칩 페이지, 8 KB 랭크 행, 64 B = BL8 한 번).
- 물리 주소(physical address, PA)를 비트 필드 표 + XOR 뱅크 해싱(bank hashing)으로 분해(decode)하고 다시 합치는(encode) C 코드를 쓰고, **왜 역함수가 존재하는지**(해시 입력은 해싱되지 않는 필드만) 증명할 수 있다.
- 연속 64 B 라인과 stride 2^k 접근이 뱅크에 어떻게 퍼지는지 히스토그램으로 보여 주고, 2의 거듭제곱 stride 의 **뱅크 캠핑(bank camping)** 과 XOR 해싱이 그것을 푸는 방식(그리고 못 푸는 경우)을 숫자로 설명한다.
- 크래시 덤프 / EDAC 로그의 PA 하나에서 DIMM 위치, 같은 뱅크의 이웃 행, 그 행이 덮는 OS 페이지를 계산한다 — 매핑을 잘못 쓰면 진단이 틀린다는 것까지.

## 1. 문제

새 보드 DDR bring-up, memtest 실패 분석, ECC CE/UE 로그 해석, 성능 튜닝 — 전부 "이 물리 주소가 DRAM 의 어디냐"에서 시작한다. 면접에서도 "주소 0x… 는 어느 뱅크로 가나요, stride 가 2의 거듭제곱이면 왜 느려지나요"가 메모리 시스템 질문의 첫 단계다.

DRAM 은 상자 안의 상자다. 메모리 컨트롤러(memory controller, MC)는 **채널** 마다 독립된 버스를 갖고, 한 채널의 64-bit 데이터 버스에는 DIMM 이 꽂힌다. DIMM 안에서 칩 선택(CS_n) 하나로 함께 움직이는 칩 묶음이 **랭크** 다 — x8 칩 8개가 DQ 8줄씩 맡아 64-bit 를 채운다. 칩 하나는 **뱅크** 16개(DDR4: BG 4개 × 뱅크 4개)를 갖고, 뱅크는 행 × 열 배열 + 행 버퍼(row buffer, sense amp)다. ACT 가 행 하나를 행 버퍼로 열고, RD/WR 이 그 행의 열을 읽는다 (명령과 타이밍은 MEM-B02, hit/conflict 는 MEM-B03).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">DRAM 계층 — 이 레슨의 DDR4 구성 (2 ch × 2 rank × x8 8 Gb, 32 GiB)</text>
  <rect x="10" y="40" width="120" height="250" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="70" y="62" fill="currentColor" text-anchor="middle" font-weight="bold">메모리</text>
  <text x="70" y="80" fill="currentColor" text-anchor="middle" font-weight="bold">컨트롤러</text>
  <text x="70" y="110" fill="currentColor" text-anchor="middle" font-size="12">PA → ch, rank,</text>
  <text x="70" y="126" fill="currentColor" text-anchor="middle" font-size="12">bg, bank,</text>
  <text x="70" y="142" fill="currentColor" text-anchor="middle" font-size="12">row, col</text>
  <text x="70" y="172" fill="currentColor" text-anchor="middle" font-size="12">(이 레슨의</text>
  <text x="70" y="188" fill="currentColor" text-anchor="middle" font-size="12">디코더)</text>
  <g stroke="currentColor">
    <line x1="130" y1="95" x2="170" y2="95"/><line x1="130" y1="235" x2="170" y2="235"/>
  </g>
  <text x="135" y="88" fill="currentColor" font-size="12">ch0</text>
  <text x="135" y="228" fill="currentColor" font-size="12">ch1</text>
  <rect x="170" y="40" width="190" height="110" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="180" y="58" fill="currentColor" font-size="12">DIMM (64-bit 버스 공유)</text>
  <rect x="180" y="66" width="170" height="34" rx="4" fill="none" stroke="currentColor"/>
  <text x="188" y="88" fill="currentColor" font-size="12">rank0: x8 칩 × 8 = 64 DQ</text>
  <rect x="180" y="106" width="170" height="34" rx="4" fill="none" stroke="currentColor"/>
  <text x="188" y="128" fill="currentColor" font-size="12">rank1 (CS_n 로 선택)</text>
  <rect x="170" y="180" width="190" height="110" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="180" y="198" fill="currentColor" font-size="12">DIMM (ch1, 같은 구조)</text>
  <text x="180" y="230" fill="currentColor" font-size="12">채널끼리는 완전히 독립:</text>
  <text x="180" y="248" fill="currentColor" font-size="12">버스·명령·타이밍 따로</text>
  <line x1="350" y1="83" x2="390" y2="83" stroke="currentColor"/>
  <rect x="390" y="40" width="160" height="250" rx="6" fill="none" stroke="currentColor"/>
  <text x="400" y="58" fill="currentColor" font-size="12">칩 1개 (x8 8 Gb)</text>
  <g stroke="currentColor" fill="none">
    <rect x="400" y="68" width="66" height="50" rx="3"/><rect x="474" y="68" width="66" height="50" rx="3"/>
    <rect x="400" y="126" width="66" height="50" rx="3"/><rect x="474" y="126" width="66" height="50" rx="3"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="410" y="88">BG0</text><text x="484" y="88">BG1</text><text x="410" y="146">BG2</text><text x="484" y="146">BG3</text>
    <text x="406" y="108">ba0..ba3</text><text x="480" y="108">ba0..ba3</text><text x="406" y="166">ba0..ba3</text><text x="480" y="166">ba0..ba3</text>
    <text x="400" y="200">16 뱅크 × 64 K 행</text>
    <text x="400" y="218">× 1 K 열 × 8 bit</text>
    <text x="400" y="236">= 2^33 bit = 8 Gb</text>
    <text x="400" y="262">칩 페이지 = 1 K × 8 bit</text>
    <text x="400" y="280">= 1 KB</text>
  </g>
  <line x1="540" y1="93" x2="580" y2="93" stroke="currentColor"/>
  <rect x="580" y="40" width="150" height="160" rx="6" fill="none" stroke="currentColor"/>
  <text x="590" y="58" fill="currentColor" font-size="12">뱅크 1개</text>
  <g stroke="currentColor" opacity=".6">
    <line x1="595" y1="72" x2="715" y2="72"/><line x1="595" y1="82" x2="715" y2="82"/>
    <line x1="595" y1="92" x2="715" y2="92"/><line x1="595" y1="102" x2="715" y2="102"/>
    <line x1="595" y1="112" x2="715" y2="112"/><line x1="595" y1="122" x2="715" y2="122"/>
  </g>
  <rect x="595" y="88" width="120" height="8" class="f-warn"/>
  <text x="590" y="140" fill="currentColor" font-size="12">ACT row r ↓</text>
  <rect x="595" y="148" width="120" height="22" class="f-ok" stroke="currentColor"/>
  <text x="602" y="164" fill="currentColor" font-size="12">row buffer</text>
  <text x="590" y="190" fill="currentColor" font-size="12">RD col c → 8 bit/칩</text>
  <text x="580" y="225" fill="currentColor" font-size="12">랭크 행 = 8 칩 × 1 KB</text>
  <text x="580" y="243" fill="currentColor" font-size="12">= 8 KB, col 1개 = 8 B</text>
  <text x="580" y="261" fill="currentColor" font-size="12">BL8 = 64 B 라인</text>
  <text x="10" y="318" fill="currentColor" font-size="12">flat bank 수 = 2 ch × 2 rank × 4 BG × 4 bank = 64. 서로 다른 뱅크는 동시에 다른 행을 열어 둘 수 있다 (뱅크 병렬성).</text>
</svg>
```

> **이 레슨의 정의 (JESD79-4 / 데이터시트 대표값 — 실제 부품은 데이터시트 확인)**
> DDR4 x8 8 Gb 칩: 4 BG × 4 bank, 64 K 행(row 16비트), 1 K 열(col 10비트). 64-bit 채널이므로 column 주소 1개 = 8 B (버스 1 beat), BL8 버스트 = 8 column = 64 B = 캐시 라인 1개. **PA[2:0] 은 8 B beat 안의 바이트** 이고 DRAM 주소선으로는 나가지 않는다 (버스트 순서·DM 이 처리). 구성: 2 채널 × 2 랭크 → 2 × 2 × 16 × 2^16 × 2^10 × 8 B = 2^35 B = 32 GiB, PA[34:0].

어떤 PA 비트가 어느 필드로 가는지는 **칩이 아니라 메모리 컨트롤러가 정한다** (부팅 펌웨어가 MC 의 address-map 레지스터에 쓴다). 그래서 같은 DIMM 이라도 플랫폼마다 매핑이 다르고, 성능과 진단이 거기에 달려 있다. 이 레슨은 매핑 네 개를 쓴다.

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-weight="bold" font-size="13">PA[34:0] 비트 필드 — 매핑 A (RoBaRaCoCh), AX (A + XOR), B (라인 인터리브)</text>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" opacity=".8">
    <text x="69.5" y="38">34</text><text x="354.5" y="38">19</text><text x="373.5" y="38">18</text><text x="411.5" y="38">16</text>
    <text x="449.5" y="38">14</text><text x="468.5" y="38">13</text><text x="582.5" y="38">7</text><text x="601.5" y="38">6</text>
    <text x="620.5" y="38">5</text><text x="658.5" y="38">3</text><text x="677.5" y="38">2</text><text x="715.5" y="38">0</text>
  </g>
  <text x="10" y="70" fill="currentColor" font-weight="bold" font-size="13">A</text>
  <g stroke="currentColor">
    <rect x="60" y="50" width="304" height="30" class="f-mut"/>
    <rect x="364" y="50" width="38" height="30" class="f-acc"/>
    <rect x="402" y="50" width="38" height="30" class="f-acc"/>
    <rect x="440" y="50" width="19" height="30" class="f-warn"/>
    <rect x="459" y="50" width="133" height="30" fill="none"/>
    <rect x="592" y="50" width="19" height="30" class="f-ok"/>
    <rect x="611" y="50" width="57" height="30" fill="none"/>
    <rect x="668" y="50" width="57" height="30" fill="none" stroke-dasharray="3 2"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="212" y="70">row[15:0]</text><text x="383" y="70">BA</text><text x="421" y="70">BG</text>
    <text x="449.5" y="70">rk</text><text x="525.5" y="70">col[9:3]</text><text x="601.5" y="70">ch</text>
    <text x="639.5" y="70">col[2:0]</text><text x="696.5" y="70">byte</text>
  </g>
  <g stroke="currentColor" fill="none">
    <polyline points="354.5,80 354.5,92 430.5,92 430.5,80"/>
    <polyline points="335.5,80 335.5,100 411.5,100 411.5,80"/>
    <polyline points="316.5,80 316.5,108 392.5,108 392.5,80"/>
    <polyline points="297.5,80 297.5,116 373.5,116 373.5,80"/>
  </g>
  <text x="10" y="138" fill="currentColor">AX: BG[0]=PA15⊕PA19  BG[1]=PA16⊕PA20  BA[0]=PA17⊕PA21  BA[1]=PA18⊕PA22  (뱅크 비트 ⊕ row 하위 4비트)</text>
  <text x="10" y="180" fill="currentColor" font-weight="bold" font-size="13">B</text>
  <g stroke="currentColor">
    <rect x="60" y="160" width="304" height="30" class="f-mut"/>
    <rect x="364" y="160" width="133" height="30" fill="none"/>
    <rect x="497" y="160" width="19" height="30" class="f-warn"/>
    <rect x="516" y="160" width="38" height="30" class="f-acc"/>
    <rect x="554" y="160" width="38" height="30" class="f-acc"/>
    <rect x="592" y="160" width="19" height="30" class="f-ok"/>
    <rect x="611" y="160" width="57" height="30" fill="none"/>
    <rect x="668" y="160" width="57" height="30" fill="none" stroke-dasharray="3 2"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="212" y="180">row[15:0]</text><text x="430.5" y="180">col[9:3]</text><text x="506.5" y="180">rk</text>
    <text x="535" y="180">BA</text><text x="573" y="180">BG</text><text x="601.5" y="180">ch</text>
    <text x="639.5" y="180">col[2:0]</text><text x="696.5" y="180">byte</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" opacity=".8">
    <text x="373.5" y="206">18</text><text x="487.5" y="206">12</text><text x="506.5" y="206">11</text>
    <text x="544.5" y="206">9</text><text x="582.5" y="206">7</text>
  </g>
  <text x="10" y="232" fill="currentColor">A: 연속 라인이 ch 를 번갈아 한 행(8 KB)을 채움 → 행 지역성. B: 연속 라인이 ch→BG→BA→rank 64 뱅크로.</text>
  <text x="10" y="250" fill="currentColor">C = B + Intel/AMD 풍 XOR 예시: CH = PA6⊕PA12⊕PA13⊕PA19..22, BG/BA/rank ⊕ row[8:4] (특정 CPU 아님).</text>
</svg>
```

### Part A — 매핑 표, 디코드, 인코드

매핑 = 세그먼트 목록 `{field, f_lo, pa_lo, width}` ("field[f_lo+w−1:f_lo] = PA[pa_lo+w−1:pa_lo]") + XOR 규칙 목록 `{target, tbit, src, mask}` ("target 의 tbit 비트 ^= parity(src & mask)").

- `map_validate(m, &info)` — PA 비트가 [0, pa_bits) 를 **빈틈·겹침 없이** 덮는지, 각 필드 비트가 0부터 연속인지, XOR 규칙이 가역 형태인지(target ∈ {CH, RANK, BG, BANK}, src ∈ {ROW, COL}) 검사하고 필드 폭과 pa_bits 를 계산.
- `map_decode(m, info, pa, &loc)` / `map_encode(m, info, &loc, &pa)` — 유효 범위에서 서로의 역함수. 범위 밖 PA → `MAP_ERR_RANGE`, 폭을 넘는 필드 → `MAP_ERR_FIELD`.
- 테스트: 15비트 작은 매핑(XOR 있음/없음)에서 **전수** 왕복 + 전단사 확인, 실제 DDR4 매핑 4개에서 랜덤 20만 × 2 방향 왕복, 잘못된 입력 · 잘못된 매핑 표.

### Part B — 뱅크 분포: 연속 라인과 stride 2^k

- `flat_bank(info, &loc)` — (ch, rank, bg, bank) 를 0..63 번호 하나로.
- `bank_histogram(m, info, base, stride, n, hist, cap, &st)` — PA = base + i·stride 의 뱅크 히스토그램, 사용된 뱅크 수, 가장 많이 몰린 뱅크의 개수(max load), "그 뱅크가 마지막으로 연 행과 같은가" 근사 분류 (hit / conflict — 정확한 모델은 MEM-B03).
- 64 B ~ 2 MB stride 표를 매핑 A / AX / B / C 로 출력하고, 512 KB stride 에서 A 는 뱅크 1개에 몰리고(캠핑) AX 는 16개로 퍼지는 것을 테스트로 고정.

### Part C — 크래시 덤프 / EDAC 의 PA 를 해석하기

- `row_neighbor(m, info, pa, ±1, &out)` — 같은 ch/rank/bg/bank/col 에서 행 ±1 인 PA (memtest 실패 주변 확인, Row Hammer 공격자/피해자 — MEM-A02). XOR 매핑에서는 PA ± 행 크기가 **아니다**.
- `row_lines(m, info, pa, 64, out, cap)` — 그 PA 가 속한 DRAM 행 하나가 덮는 64 B 라인의 PA 목록 → 몇 개의 4 KB OS 페이지에 걸쳐 있나 (page retire 단위 — MEM-A06).
- 같은 행에서 난 CE 3개를 올바른 매핑(AX)과 XOR 를 빠뜨린 매핑(A)으로 디코드해 BG/뱅크 판정이 달라지는 것을 보인다 (PPR 대상이 틀린다).

## 2. Problem (English)

> **DRAM physical-address decoder with configurable mapping.**
>
> A memory controller splits a physical address into `(channel, rank, bank_group, bank, row, column)`. The mapping is described by a table of bit-field segments plus optional XOR terms:
>
> ```c
> typedef struct { uint8_t field, f_lo, pa_lo, width; } map_seg_t;        // field[f_lo+w-1:f_lo] = PA[pa_lo+w-1:pa_lo]
> typedef struct { uint8_t target, tbit, src; uint32_t src_mask; } map_xor_t; // target.bit[tbit] ^= parity(src & mask)
> int map_validate(const addr_map_t *m, map_info_t *info);
> int map_decode(const addr_map_t *m, const map_info_t *info, uint64_t pa, dram_loc_t *out);
> int map_encode(const addr_map_t *m, const map_info_t *info, const dram_loc_t *loc, uint64_t *pa);
> int bank_histogram(const addr_map_t *m, const map_info_t *info, uint64_t base, uint64_t stride,
>                    uint32_t n, uint32_t *hist, size_t cap, bank_stats_t *st);
> int row_neighbor(const addr_map_t *m, const map_info_t *info, uint64_t pa, int delta, uint64_t *out);
> ```
>
> Requirements: segments must tile `PA[pa_bits-1:off_bits]` with no gaps or overlaps; XOR terms may only hash CH/RANK/BG/BANK using ROW/COL bits, so that `encode(decode(pa)) == pa` for every valid `pa` and `decode(encode(loc)) == loc` for every valid `loc`. Return error codes, never assert. No dynamic allocation.
>
> Config: DDR4-3200, 2 channels × 2 ranks of x8 8 Gb devices (4 BG × 4 banks, 64 K rows, 1 K columns), 64-bit bus, 32 GiB. `PA[2:0]` is the byte within an 8-byte beat.
>
> Example (mapping A: `row[34:19] ba[18:17] bg[16:15] rank[14] col[9:3]=PA[13:7] ch[6] col[2:0]=PA[5:3]`):
> `decode(0x1_2345_6740) → ch=1 rank=1 bg=2 bank=2 row=0x2468 col=0x270`.
> With XOR hashing `bg ^= row[1:0], bank ^= row[3:2]` the same address is `bank=0`.
>
> Follow-ups: Which strides make all accesses hit one bank under mapping A, and how does XOR hashing change that? Why must the hash inputs be unhashed fields? Given a correctable-error address from an EDAC log, which physical addresses are in the adjacent rows of the same bank?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 일곱 군데다: Part A `map_validate` · `map_decode` · `map_encode`, Part B `flat_bank` · `bank_histogram`, Part C `row_neighbor` · `row_lines`. 매핑 표 6개(A, AX, B, C + 전수 테스트용 작은 매핑 2개), `loc_format`, `count_pages`, 하네스는 주어진다. 스텁은 0 이나 `MAP_OK` 를 돌려줄 뿐 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:232  map_validate(&MAP_A, &ia) == MAP_OK && ia.pa_bits == 35
FAIL starter.c:234  ia.width[F_CH] == 1 && ia.width[F_RANK] == 1 && ia.width[F_BG] == 2 && ...
A RoBaRaCoCh: 0 PA bits, ch=0 rank=0 bg=0 bank=0 row=0 col=0 bits
…
FAIL starter.c:372  same_row
FAIL starter.c:373  wrong.f[F_BG] == 3 && wrong.f[F_BANK] == 2
4/53 passed
```

(exit 1. 통과한 4개는 "decode 가 MAP_OK 를 돌려준다"만 보는 체크라 스텁이 우연히 맞힌 것이다.)

## 4. 힌트

<details><summary>힌트 1 — 디코드는 "잘라 붙이기" 다음 "XOR"</summary>

세그먼트마다 `(pa >> pa_lo) & mask(width)` 를 꺼내 `<< f_lo` 해서 필드에 OR 한다. 모든 세그먼트를 끝낸 **다음에** XOR 규칙을 적용한다: `f[target] ^= parity(f[src] & mask) << tbit`. src 는 ROW/COL 이라 XOR 로 바뀌지 않으므로 순서가 상관없다.

</details>

<details><summary>힌트 2 — 인코드는 같은 XOR 를 한 번 더</summary>

`a ^ h ^ h = a`. 해시 h 는 row/col 만의 함수이고 row/col 은 loc 에 그대로 있으니, `raw[target] = loc.f[target] ^ h(loc)` 로 PA 에 실제로 적힌 비트("raw")를 되찾는다. 그 다음 세그먼트마다 `((raw >> f_lo) & mask) << pa_lo` — **uint64_t 로 바꾼 뒤 시프트** 해야 row 가 PA[34:19] 에 들어간다.

</details>

<details><summary>힌트 3 — 빈틈/겹침 검사는 비트마스크 두 개</summary>

`pa_used` (uint64) 와 필드별 `f_used` (uint32) 에 세그먼트 마스크를 OR 하면서, 이미 켜진 비트와 겹치면 오류. 끝나고 `pa_used == (1 << top) − 1` 이면 빈틈 없음. 필드 쪽은 `x & (x + 1) == 0` 이면 "하위부터 연속인 1" (예: 0b0111). 폭 = popcount.

</details>

<details><summary>힌트 4 — flat bank 와 근사 hit/conflict</summary>

모든 개수가 2의 거듭제곱이라 `((ch << wr | rank) << wbg | bg) << wb | bank` 로 이어 붙이면 된다. hit/conflict 는 뱅크별 `last_row[]` 하나로 센다: 처음 = first, 같은 행 = hit, 다른 행 = conflict.

</details>

<details><summary>힌트 5 — 행 이웃과 행 전체</summary>

둘 다 "decode → 필드 하나 바꾸기 → encode" 다. `row_neighbor` 는 row ± 1 (범위 밖이면 오류), `row_lines` 는 byte = 0, col = i · (64 / 8) 로 바꿔 가며 encode.

</details>

## 5. 풀이

### Part A — 디코드 한 번을 손으로

매핑 A 에서 PA = 0x1_2345_6740 = `0b 0010010001101000 10 10 1 1001110 1 000 000` (35비트, 필드 경계에서 끊음):

| PA 비트 | 필드 | 비트 값 | 결과 |
|---|---|---|---|
| [2:0] | byte | 000 | byte 0 |
| [5:3] | col[2:0] | 000 | |
| [6] | ch | 1 | **ch 1** |
| [13:7] | col[9:3] | 1001110 = 0x4E | col = 0x4E·8 + 0 = **0x270** |
| [14] | rank | 1 | **rank 1** |
| [16:15] | BG | 10 | **BG 2** |
| [18:17] | BA | 10 | **bank 2** |
| [34:19] | row | 0x2468 | **row 0x2468** |

매핑 AX 는 같은 raw 비트에 XOR 를 건다: row[1:0] = 00 → BG = 2 ⊕ 0 = **2**, row[3:2] = 10 → bank = 2 ⊕ 2 = **0**. 매핑 B 는 같은 PA 를 ch1 rank0 BG2 bank3 row 0x2468 col 0x2B0 으로, C 는 ch1 rank0 BG0 bank2 로 읽는다 (solution 출력 참고). **같은 PA, 같은 DIMM 인데 매핑에 따라 뱅크가 다 다르다** — 이게 레슨 전체의 요점이다.

**왜 역함수가 있나 (대수).** PA 의 비트 벡터를 p 라 하자. 세그먼트 단계 S 는 비트의 **자리 바꿈(permutation)** 이다 — validate 가 "빈틈·겹침 없음"을 보장하므로 PA 비트 하나가 정확히 필드 비트 하나로 간다. XOR 단계 X 는 GF(2) 위의 선형 사상으로, 필드 벡터를 (u, v) = (해싱되는 필드 CH/RANK/BG/BANK, 해싱 안 되는 ROW/COL) 로 나누면

  X(u, v) = (u ⊕ Hv, v),  행렬로 [[I, H], [0, I]].

이 행렬의 제곱은 [[I, 2H], [0, I]] = [[I, 0], [0, I]] (GF(2) 에서 2H = 0) 이므로 **X 는 자기 자신이 역(involution)**. 그래서 decode = X∘S, encode = S⁻¹∘X 이고 둘 다 전단사다. 만약 해시 입력에 해싱되는 필드를 쓰면(예: BG[0] ^= BANK[0], BANK[0] ^= BG[0]) 행렬이 위삼각이 아니게 되어 역이 있을 수도 없을 수도 있고, 최소한 "같은 XOR 한 번 더"로는 안 풀린다 — 그래서 validate 가 `MAP_ERR_XOR` 로 막는다. 실제 하드웨어에서는 이 단계가 XOR 게이트 몇 개라 지연이 거의 없다.

**검증 규칙 정리.**

| 검사 | 실패 시 | 왜 |
|---|---|---|
| PA 비트 겹침 / 빈틈 | `MAP_ERR_LAYOUT` | 겹치면 두 위치가 한 PA, 빈틈이면 PA 공간에 구멍 → 전단사 깨짐 |
| 필드 비트 겹침 / 0부터 연속 아님 | `MAP_ERR_LAYOUT` | row 비트 3이 없으면 row 개수가 2의 거듭제곱이 아님 |
| row/col 폭 0, 뱅크 비트 합 > 12 | `MAP_ERR_LAYOUT` | flat bank 배열 크기 상한 (정적 버퍼) |
| XOR target 이 ROW/COL, src 가 해싱 필드, tbit ≥ 폭, mask 가 src 폭 밖 | `MAP_ERR_XOR` | 위의 involution 조건 |

**테스트 설계.** 작은 매핑은 PA 15비트 = 32 768 개라 **전수**로 (1) 모든 PA 에 대해 encode(decode(pa)) = pa, (2) 모든 loc 32 768 개에 대해 decode(encode(loc)) = loc, (3) encode 결과가 각 PA 에 정확히 한 번 (`seen[]`) 을 확인한다. XOR 버전은 BG ^= row0⊕row2, BANK ^= row1, CH ^= col0⊕col2 (COL 을 입력으로), RANK ^= row3 — 규칙이 가능한 모든 모양을 섞는다. 35비트 실제 매핑은 전수가 2^35 라 xorshift64 고정 시드 20만 개 × 2 방향. 실측 결과: 전수 불일치 0 / 0, 랜덤 A=0 AX=0 B=0 C=0.

비용: decode/encode 는 O(세그먼트 수 + XOR 규칙 수) ≈ 15회 시프트·마스크. RAM 은 매핑 표 하나 약 100 B. 펌웨어에서 EDAC 핸들러나 memtest 리포트에 그대로 넣을 수 있는 크기다.

### Part B — 뱅크 캠핑과 XOR 해싱

`solution` 이 출력하는 표 (PA 0 부터 n = 1024 번 접근; 칸 = 사용된 뱅크 수(/64) / 가장 많이 몰린 뱅크의 접근 수 / 뱅크별 "마지막 행과 다름" 비율). 시뮬레이션이라 결정적이다:

```text
stride   A RoBaRaCoCh       AX RoBaRaCoCh+XOR  B line-interleave  C line-interleave+XOR
64 B      8 /  128 /   0.0   8 /  128 /   0.0  64 /   16 /   0.0  64 /   16 /   0.0
4 KB     32 /   32 /  21.9  32 /   32 /  21.9   1 / 1024 /   0.7   2 /  512 /   1.4
16 KB    32 /   32 /  96.9  32 /   32 /  96.9   1 / 1024 /   3.0   4 /  256 /   2.7
64 KB     8 /  128 /  99.2  16 /   64 /  98.4   1 / 1024 /  12.4  16 /   64 /  10.9
256 KB    2 /  512 /  99.8  16 /   64 /  98.4   1 / 1024 /  49.9  64 /   16 /  43.8
512 KB    1 / 1024 /  99.9  16 /   64 /  98.4   1 / 1024 /  99.9  64 /   16 /  93.8
1 MB      1 / 1024 /  99.9   8 /  128 /  99.2   1 / 1024 /  99.9  64 /   16 /  93.8
2 MB      1 / 1024 /  99.9   4 /  256 /  99.6   1 / 1024 /  99.9  64 /   16 /  93.8
```

(전체 16줄은 6장 출력. 여기선 대표 줄만.) 읽는 법:

1. **A 의 2^19 이상 stride = 뱅크 1개.** stride 가 2^19 (= 행 1개 × 모든 뱅크·랭크·채널 = 8 KB × 64) 의 배수면 PA[18:0] 이 그대로라 ch/rank/BG/bank 가 고정되고 row 만 바뀐다. 1024 번 접근이 전부 한 뱅크, 매번 다른 행 → **conflict 99.9 %, 직렬화 깊이 1024**. 2D 배열의 열 순회(행 길이 512 KB), 2의 거듭제곱 크기 버퍼 여러 개를 동시에 스트리밍(같은 오프셋이 같은 뱅크) 같은 흔한 패턴이 여기에 걸린다.
2. **AX 는 뱅크를 16개로 퍼뜨린다 — 하지만 conflict 는 그대로 98 %.** row 하위 4비트가 BG/bank 를 뒤섞어 같은 stride 가 16 뱅크에 4개씩 간다. 각 뱅크 안에서는 여전히 매번 다른 행이다. **XOR 해싱은 conflict 를 hit 로 바꾸지 않는다. conflict 를 한 뱅크에 줄 세우는 대신 여러 뱅크에서 겹쳐 처리하게 한다.** 그래서 지표는 conflict % 가 아니라 max load (직렬화 깊이) 다: 1024 → 64.
3. **AX 도 1 MB 이상에선 다시 몰린다.** 1 MB stride 면 row 가 2씩 늘어 row[0] 이 고정 → BG[0] 고정 → 8 뱅크. 해시에 쓰는 row 비트 위의 stride (2^23 이상) 는 A 와 똑같이 뱅크 1개다 (연습 4). 실제 CPU 해시가 row 비트를 많이 쓰는 이유다.
4. **B 는 정반대 트레이드오프.** 연속 라인이 64 뱅크에 퍼지지만(64 B 줄: 64 / 16), 4 KB stride 는 PA[11:6] 이 고정이라 **뱅크 1개** — 다만 col[9:3] 이 PA[18:12] 라 같은 행이 128번 연속 → conflict 0.7 %. 256 KB 이상에서는 1 뱅크에 conflict 50 % ~ 99.9 %.
5. **C (B + 여러 비트 XOR)** 는 256 KB 이상 stride 를 64 뱅크로 퍼뜨린다. 대신 4 KB · 16 KB stride 는 2 ~ 4 뱅크: BG/BA/rank 의 raw 비트(PA[11:7])는 4 KB 배수 stride 에서 고정이고, XOR 입력은 row[8:4] (PA[27:23]) 라서 접근 범위가 8 MB(2^23)를 넘기 전에는 ch 비트만 바뀐다 (16 KB × 1024 = 16 MB 는 PA23 하나가 더 바뀌어 4 뱅크).

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="16" fill="currentColor" font-weight="bold" font-size="13">stride 512 KB (0x80000), 64번 접근 — ch0/rank0 의 16 뱅크 (BG×4+BA) 히스토그램</text>
  <text x="40" y="38" fill="currentColor" font-size="13">매핑 A: 1 뱅크에 64개, conflict 63</text>
  <text x="400" y="38" fill="currentColor" font-size="13">매핑 AX: 16 뱅크에 4개씩</text>
  <g stroke="currentColor">
    <line x1="40" y1="210" x2="340" y2="210"/><line x1="400" y1="210" x2="700" y2="210"/>
    <line x1="40" y1="50" x2="40" y2="210"/><line x1="400" y1="50" x2="400" y2="210"/>
  </g>
  <rect x="46" y="50" width="14" height="160" class="f-warn" stroke="currentColor"/>
  <text x="64" y="62" fill="currentColor">64</text>
  <g class="f-ok" stroke="currentColor">
    <rect x="406" y="200" width="14" height="10"/><rect x="424" y="200" width="14" height="10"/>
    <rect x="442" y="200" width="14" height="10"/><rect x="460" y="200" width="14" height="10"/>
    <rect x="478" y="200" width="14" height="10"/><rect x="496" y="200" width="14" height="10"/>
    <rect x="514" y="200" width="14" height="10"/><rect x="532" y="200" width="14" height="10"/>
    <rect x="550" y="200" width="14" height="10"/><rect x="568" y="200" width="14" height="10"/>
    <rect x="586" y="200" width="14" height="10"/><rect x="604" y="200" width="14" height="10"/>
    <rect x="622" y="200" width="14" height="10"/><rect x="640" y="200" width="14" height="10"/>
    <rect x="658" y="200" width="14" height="10"/><rect x="676" y="200" width="14" height="10"/>
  </g>
  <text x="406" y="194" fill="currentColor">4 (각 뱅크)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="46" y="226">0</text><text x="316" y="226">15</text>
    <text x="406" y="226">0</text><text x="676" y="226">15</text>
  </g>
  <text x="10" y="248" fill="currentColor">max load 64 → 4: 16 뱅크가 나눠 겹쳐 처리. 뱅크마다 첫 1 + conflict 3 → conflict 48개는 남는다.</text>
  <text x="10" y="264" fill="currentColor">solution.c bank_histogram 실제 결과 (결정적 시뮬레이션). ACT 속도 상한 tRRD/tFAW 는 MEM-A01.</text>
</svg>
```

얼마나 차이가 날까 (대략): 한 뱅크에서 conflict 가 줄을 서면 접근마다 tRC (= tRAS + tRP, MEM-B02 의 DDR4-3200 값 45.75 ns) 이상이 걸려 64개면 약 2.9 µs. 16 뱅크로 나뉘면 이론상 그 1/16 까지 줄 수 있지만, 실제로는 ACT 간격 제한(tRRD_S/L, tFAW — MEM-A01)과 명령 버스가 상한이다. 정확한 스케줄링 효과는 MEM-M01 (FR-FCFS) · MEM-M02 (매핑 정책과 처리량) 에서 시뮬레이터로 잰다.

**매핑 고르기.** 행 지역성이 높은 워크로드(CPU 의 순차 스트림, open-page 정책)는 A 처럼 "행을 먼저 채우는" 매핑이 hit 를 많이 만든다. 요청이 많고 흩어진 워크로드(GPU, 다수 코어, close-page 정책 — MEM-B03)는 B 처럼 라인 단위로 뱅크·채널에 퍼뜨려 병렬성을 얻는다. 실제 MC 는 둘 사이(몇 라인 ~ 몇 KB 단위 인터리브) + XOR 해싱을 쓴다. 이 맥(M6)의 매핑은 공개되어 있지 않다 — 여기서는 시뮬레이션만 하고, 실측 지연/대역폭은 MEM-B10 · MEM-B11 에서 잰다.

### Part C — 크래시 덤프 PA 에서 출발하기

**행 이웃.** memtest 가 PA X 에서 비트 플립을 보고하면, 같은 뱅크의 이웃 행(Row Hammer 의 공격자 후보, 혹은 같은 워드라인 결함)을 다시 테스트하고 싶다. 매핑 A 에서는 row 가 PA 최상위라 이웃 = X ± 2^19 이다. AX 에서는 row 가 바뀌면 해시가 바뀌어 BG/bank 비트도 바뀐다 — 그래서 decode → row+1 → encode 해야 한다. 실측:

```text
AX row+1 neighbour of 0x123456740 = 0x1234DE740 (A would say 0x1234D6740)
```

차이 0x8000 = PA15 = BG[0] 의 raw 비트. 0x2468 → 0x2469 에서 row[0] 이 바뀌어 BG[0] = PA15 ⊕ row[0] 을 유지하려면 PA15 가 뒤집혀야 한다. 단, 이것은 **논리** 행 번호의 이웃이다. 칩 내부의 행 재배치(리페어 행, 주소 스크램블)는 데이터시트에도 잘 안 나오므로 물리적 이웃은 다를 수 있다 (MEM-A02).

**한 행이 덮는 OS 페이지.** 행 하나(8 KB) = 64 B 라인 128 개. 매핑 A 에서는 col 이 PA[5:3] + PA[13:7] 이라 16 KB 구간에 한 칸 건너 하나씩 (ch 비트 6 이 끼어 있어서) → 4 KB 페이지 **4개**. 매핑 B 에서는 col[9:3] 이 PA[18:12] 라 128 라인이 4 KB 간격으로 512 KB 에 흩어진다 → 4 KB 페이지 **128개**:

```text
one DRAM row (8 KB) = 128 lines of 64 B -> 4 KB pages touched: A=4 AX=4 B=128
```

행 하나가 망가져(행 단위 결함, 반복되는 CE) OS 가 page offline/retire 로 격리하려면, A 에서는 페이지 4개, B 에서는 128개(512 KB)를 빼야 한다. 펌웨어/OS 가 "이 행에 해당하는 페이지들"을 계산할 때 바로 이 `row_lines` 가 필요하다 (RAS 정책은 MEM-A06).

**EDAC 로그와 잘못된 매핑.** Linux EDAC 드라이버는 MC 레지스터에서 매핑을 읽어 CE 주소를 DIMM/rank/bank/row/col 로 바꿔 보고한다 (드라이버·플랫폼마다 보고 필드가 다르다). 같은 행에서 CE 3개가 났다고 하자 (AX 로 encode 해 만든 주소):

```text
CE pa=0x055E580C0  AX: ch1 rank0 bg3 bank1 row 0x0ABC col 0x008 byte 0 | A(wrong): bg3 bank2
CE pa=0x055E584C0  AX: ch1 rank0 bg3 bank1 row 0x0ABC col 0x048 byte 0 | A(wrong): bg3 bank2
CE pa=0x055E588C0  AX: ch1 rank0 bg3 bank1 row 0x0ABC col 0x088 byte 0 | A(wrong): bg3 bank2
```

XOR 를 빠뜨린 디코더(A)도 "세 개가 같은 행"이라는 결론은 맞힌다 (해시가 row 만의 함수라 같은 행 → 같은 오차). 하지만 **뱅크 번호가 틀린다** (bank 1 → 2). 이 정보로 PPR(post-package repair: 칩 안의 여분 행으로 고장 행을 대체하는 DDR4 기능 — 절차는 데이터시트 확인)을 요청하면 멀쩡한 bank 2 의 행 0x0ABC 를 수리하게 된다. 진단 도구의 매핑은 반드시 MC 를 설정한 펌웨어와 같은 소스에서 와야 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 53/53 passed, exit 0, 0.2 초 미만
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
```

실제 출력 (`solution`):

```text
A RoBaRaCoCh: 35 PA bits, ch=1 rank=1 bg=2 bank=2 row=16 col=10 bits
A : 0x1_2345_6740 -> ch1 rank1 bg2 bank2 row 0x2468 col 0x270 byte 0
AX: 0x1_2345_6740 -> ch1 rank1 bg2 bank0 row 0x2468 col 0x270 byte 0
B : 0x1_2345_6740 -> ch1 rank0 bg2 bank3 row 0x2468 col 0x2B0 byte 0
C : 0x1_2345_6740 -> ch1 rank0 bg0 bank2 row 0x2468 col 0x2B0 byte 0
exhaustive 2^15 round trip: small mismatches=0, small+xor mismatches=0
random 200000 x2 round trip: A=0 AX=0 B=0 C=0 mismatches

stride sweep, n=1024 accesses from PA 0: distinct banks(/64) / max load / conflict%
stride   A RoBaRaCoCh       AX RoBaRaCoCh+XOR  B line-interleave  C line-interleave+XOR
64 B      8 /  128 /   0.0   8 /  128 /   0.0  64 /   16 /   0.0  64 /   16 /   0.0
128 B     8 /  128 /   0.0   8 /  128 /   0.0  32 /   32 /   0.0  64 /   16 /   0.0
256 B    16 /   64 /   0.0  16 /   64 /   0.0  16 /   64 /   0.0  32 /   32 /   0.0
512 B    32 /   32 /   0.0  32 /   32 /   0.0   8 /  128 /   0.0  16 /   64 /   0.0
1 KB     32 /   32 /   3.1  32 /   32 /   3.1   4 /  256 /   0.4   8 /  128 /   0.8
2 KB     32 /   32 /   9.4  32 /   32 /   9.4   2 /  512 /   0.6   4 /  256 /   1.2
4 KB     32 /   32 /  21.9  32 /   32 /  21.9   1 / 1024 /   0.7   2 /  512 /   1.4
8 KB     32 /   32 /  46.9  32 /   32 /  46.9   1 / 1024 /   1.5   2 /  512 /   2.9
16 KB    32 /   32 /  96.9  32 /   32 /  96.9   1 / 1024 /   3.0   4 /  256 /   2.7
32 KB    16 /   64 /  98.4  16 /   64 /  98.4   1 / 1024 /   6.2   8 /  128 /   5.5
64 KB     8 /  128 /  99.2  16 /   64 /  98.4   1 / 1024 /  12.4  16 /   64 /  10.9
128 KB    4 /  256 /  99.6  16 /   64 /  98.4   1 / 1024 /  24.9  32 /   32 /  21.9
256 KB    2 /  512 /  99.8  16 /   64 /  98.4   1 / 1024 /  49.9  64 /   16 /  43.8
512 KB    1 / 1024 /  99.9  16 /   64 /  98.4   1 / 1024 /  99.9  64 /   16 /  93.8
1 MB      1 / 1024 /  99.9   8 /  128 /  99.2   1 / 1024 /  99.9  64 /   16 /  93.8
2 MB      1 / 1024 /  99.9   4 /  256 /  99.6   1 / 1024 /  99.9  64 /   16 /  93.8

AX row+1 neighbour of 0x123456740 = 0x1234DE740 (A would say 0x1234D6740)
one DRAM row (8 KB) = 128 lines of 64 B -> 4 KB pages touched: A=4 AX=4 B=128
CE pa=0x055E580C0  AX: ch1 rank0 bg3 bank1 row 0x0ABC col 0x008 byte 0 | A(wrong): bg3 bank2
CE pa=0x055E584C0  AX: ch1 rank0 bg3 bank1 row 0x0ABC col 0x048 byte 0 | A(wrong): bg3 bank2
CE pa=0x055E588C0  AX: ch1 rank0 bg3 bank1 row 0x0ABC col 0x088 byte 0 | A(wrong): bg3 bank2
53/53 passed
```

64 B 줄에서 A 가 "8 뱅크"인 이유: 1024 × 64 B = 64 KB 라 PA[15:0] 만 변한다 → ch(6) · rank(14) · BG[0](15) 세 비트 → 2^3 = 8.

## 7. 자주 하는 실수

**1. PA[2:0] 을 column 으로 쓴다.** 64-bit 버스에서 column 주소 1개는 8 B 다. byte 비트를 col 로 넣으면 col 이 8배가 되고 행 크기가 64 KB 로 계산된다. 데이터 버스 폭(64-bit → 3비트, DDR5 서브채널 32-bit → 2비트)부터 떼어 놓는다.

**2. uint32_t 로 시프트해서 row 를 PA 에 넣는다.** row 를 `<< 19` 하면 row[15:13] 이 32비트 밖으로 나가 사라진다. 2^32 이상 주소에서만 틀려서 작은 테스트로는 안 잡힌다.

```c
p |= (uint64_t)((raw[s->field] >> s->f_lo) & mask) << s->pa_lo;   /* 시프트 전에 64비트로 */
```

**3. 인코드에서 XOR 를 빼먹거나, 해싱된 값을 그대로 PA 에 쓴다.** decode 가 `bank = raw ^ h(row)` 였으니 encode 는 `raw = bank ^ h(row)` 를 PA 에 써야 한다. 빼먹으면 XOR 매핑에서만 왕복이 깨진다 — 그래서 XOR 매핑에도 전수 테스트를 돌린다.

**4. 해싱되는 필드를 해시 입력으로 쓴다.** `BG ^= BANK`, `BANK ^= BG` 처럼 서로 물리면 "같은 XOR 한 번 더"로 역이 안 된다 (적용 순서에 따라 결과도 달라짐). 입력은 해싱되지 않는 ROW/COL 비트로만 — validate 에서 거부한다.

**5. 세그먼트 개수(nseg)를 실제보다 크게 적는다.** 이 레슨을 쓰면서 실제로 낸 버그다: 세그먼트 7개인 표에 `nseg = 8` 을 적었더니 0으로 채워진 8번째 세그먼트(width 0)가 끼어들었다. validate 가 `width == 0` 을 `MAP_ERR_LAYOUT` 으로 거부해서 첫 실행에 잡혔다 — 검증 없이 decode 했다면 조용히 맞는 척했을 것이다. 표는 데이터이므로 **표 자체를 검사하는 코드** 가 있어야 한다.

**6. "같은 뱅크"를 bank 필드만으로 판단한다.** ch0 bank2 와 ch1 bank2 는 다른 뱅크다. 충돌·병렬성 분석은 (ch, rank, bg, bank) 전체 = flat bank 로 한다.

**7. 이웃 행을 PA ± 행 stride 로 계산한다.** XOR 매핑에서는 틀린다 (위 실측: 0x1234D6740 vs 0x1234DE740). 항상 decode → row ± 1 → encode.

## 8. 면접 Q&A

<details><summary>Q1. 메모리 컨트롤러는 왜 뱅크 비트에 row 비트를 XOR 하나요?</summary>

2의 거듭제곱 stride 가 흔하기 때문이다 (배열 크기, 페이지 크기, 여러 버퍼의 같은 오프셋). 단순 비트 자르기 매핑에서는 stride 가 "뱅크 비트보다 위"면 뱅크 비트가 고정되어 모든 요청이 한 뱅크의 서로 다른 행으로 간다 → conflict 가 한 줄로 직렬화된다. row 하위 비트를 뱅크 비트에 XOR 하면 행이 바뀔 때 뱅크도 바뀌어 그 요청들이 여러 뱅크로 퍼진다. 실측(이 레슨 시뮬레이션): 512 KB stride 1024회, max load 1024 → 64. 꼬리: conflict 비율은 그대로(98 %)다 — 해싱은 행 지역성을 만들지 않고, conflict 를 병렬로 처리할 수 있게 할 뿐이다. 그리고 행 내부 순차 접근은 해시가 row 만의 함수라 같은 뱅크에 남아 hit 가 유지된다 (Zhang 외, MICRO 2000 의 permutation-based interleaving 이 이 아이디어).

</details>

<details><summary>Q2. 왜 해시 입력은 row(또는 col)만 쓰고 bank 끼리는 섞지 않나요?</summary>

역함수 때문이다. 필드를 (해싱되는 u, 해싱 안 되는 v) 로 나누면 decode 의 XOR 단계는 [[I, H], [0, I]] 이고 GF(2) 에서 자기 역이다. 그래서 encode 가 "같은 XOR 한 번 더"로 끝나고, 하드웨어에서도 게이트 한 단이다. 입력에 u 를 섞으면 일반 가역 행렬이 필요해 검증·역계산이 복잡해지고, 잘못 고르면 두 PA 가 같은 위치로 가는(전단사 깨짐) 매핑이 된다. 실제 CPU 해시도 "여러 PA 비트의 XOR = 뱅크 비트 하나" 꼴이고 각 함수가 서로 다른 뱅크 선택 비트를 하나씩 포함하도록 짜여 있다 (DRAMA 리버스 엔지니어링 결과의 모양 — 정확한 비트는 논문 표 확인).

</details>

<details><summary>Q3. RoBaRaCoCh 같은 행 우선 매핑과 캐시 라인 인터리브 매핑, 언제 무엇을 쓰나요?</summary>

행 우선(A)은 순차 스트림이 한 행(8 KB)을 쭉 채우므로 open-page 정책에서 row hit 가 많다 — 단일 스레드 CPU 스트림에 유리. 라인 인터리브(B)는 연속 라인이 64 뱅크에 퍼져 요청이 많을 때 뱅크 병렬성이 높지만, 같은 행에 머무는 시간이 짧아 close-page 정책과 어울린다 — 많은 스트림이 섞이는 GPU · 다코어 서버에 유리. 실측: 64 B 순차 1024회에서 A 는 8 뱅크에 128씩, B 는 64 뱅크에 16씩. 반대로 4 KB stride 는 B 가 1 뱅크(대신 행 hit 99 %)다. 실제 MC 는 중간(수백 B ~ 몇 KB 인터리브) + XOR 해싱. 정책 비교는 MEM-B03, 스케줄러와 묶은 처리량은 MEM-M02.

</details>

<details><summary>Q4. 랭크와 뱅크 그룹은 왜 있나요? 많을수록 좋나요?</summary>

랭크 = 같은 데이터 버스를 공유하고 CS_n 으로 구분되는 칩 묶음. 랭크가 늘면 용량과 열 수 있는 행(뱅크 수)이 늘지만, 버스를 공유하므로 랭크를 바꿔 읽을 때 터미네이션(ODT) 전환을 위한 공백(rank-to-rank turnaround)이 생기고, 버스 부하가 커져 최고 속도가 내려갈 수 있다. 뱅크 그룹은 DDR4 에서 도입됐다: 같은 BG 안의 연속 열 명령은 tCCD_L, 다른 BG 사이는 더 짧은 tCCD_S 를 지킨다 — 그래서 연속 버스트를 BG 를 번갈아 내보내는 매핑(B 처럼 PA[8:7] 에 BG)이 버스를 꽉 채우기 좋다. 정확한 값과 tRRD_S/L · tFAW 는 MEM-A01.

</details>

<details><summary>Q5. EDAC 로그에 CE 가 PA 로만 찍혔습니다. 어떻게 DIMM 과 행을 찾나요?</summary>

MC 가 쓰는 것과 같은 매핑(Intel 서버의 SAD/TAD 같은 시스템 디코드 → 채널 디코드 → 랭크/뱅크/행/열)을 역으로 적용한다. Linux 에서는 플랫폼별 EDAC 드라이버가 MC 레지스터를 읽어 이 변환을 하고, DIMM 라벨 · 랭크 · 뱅크 · 행 · 열을 보고한다 (보고 필드는 드라이버마다 다름). 펌웨어 쪽(BIOS/BMC)은 보통 MC 설정 코드와 같은 표를 쓴다. 판단: 같은 (rank, bg, bank, row) 에 CE 가 반복되면 행 결함 → 해당 행의 페이지를 retire 하거나 PPR, 같은 col 이 여러 행에 걸치면 열(비트라인) 결함, 한 DQ 비트만 계속이면 칩/핀. 매핑을 잘못 쓰면 이 판단이 틀린다 (6장 출력: bank 1 을 2 로 오판).

</details>

<details><summary>Q6. 문서가 없는 SoC 의 매핑을 어떻게 알아내죠?</summary>

(1) 레퍼런스 매뉴얼의 MC address-map 레지스터 (가장 좋음). (2) 없으면 타이밍 사이드 채널: 두 주소를 번갈아 읽어 느리면(row conflict) "같은 뱅크 다른 행"이다. 이 "같은 뱅크?" 오라클로 한 비트씩, 두 비트씩 뒤집어 보면 XOR 함수가 나온다 — DRAMA (Pessl 외, USENIX Security 2016) 가 이 방법으로 Intel 과 일부 ARM 플랫폼의 함수를 복원했다 (대상 목록은 논문 표 확인). 연습 3 이 시뮬레이션 오라클로 같은 것을 한다 (AX → {15,19} {16,20} {17,21} {18,22}). macOS 에서는 PA 를 사용자 공간에서 볼 수 없어 이 실험이 어렵다 — 이 맥(M6)의 매핑은 확인하지 않았다.

</details>

<details><summary>Q7. DDR5 는 DIMM 하나를 왜 32-bit 서브채널 두 개로 나눴나요? 매핑에 무슨 영향이 있죠?</summary>

버스트 길이가 BL16 으로 늘었는데 64-bit 그대로면 버스트 하나가 128 B 라 64 B 캐시 라인에 낭비다. 32-bit × BL16 = 64 B 로 맞추고, 독립 서브채널 두 개로 동시에 처리할 수 있는 요청 수(채널 병렬성)를 늘렸다. 매핑에서는 byte 비트가 2개(4 B beat), col 하위 4비트가 버스트 안 위치, 서브채널이 "채널" 비트 하나로 들어간다 (연습 2). 뱅크도 x8 16 Gb 기준 8 BG × 4 = 32 개(대표값)로 늘었다. 세대 비교와 대역폭 계산은 MEM-B04.

</details>

<details><summary>Q8. 매핑이 Row Hammer · 보안과 무슨 관계가 있나요?</summary>

Row Hammer 는 같은 뱅크의 이웃 행을 빠르게 반복 ACT 해서 피해 행의 비트를 뒤집는다. 공격자는 "같은 뱅크, 행 ±1" 인 주소를 알아야 하므로 매핑을 알아내는 것이 첫 단계다 (DRAMA 가 그 도구). 방어 쪽에서도 TRR · 이웃 refresh 는 MC 가 행 단위로 하므로, 매핑과 칩 내부 행 재배치를 모르면 "이웃"을 잘못 짚는다. 또 뱅크 단위 성능 격리(OS 가 코어별로 다른 뱅크의 페이지만 주는 bank coloring, 예: PALLOC)도 매핑을 알아야 가능하다 — 연습 5: A 는 페이지 안에 ch 비트만 있어 색칠 가능, B 는 4 KB 페이지 하나가 64 뱅크를 다 건드려 불가능. 자세한 것은 MEM-A02.

</details>

## 9. 추가 연습

### 연습 1 — 용량, 랭크 행, 칩 페이지

DDR4 x8 16 Gb 칩(대표값: 4 BG × 4 bank, 128 K 행, 1 K 열)으로 1 채널 1 랭크 매핑을 만들고 용량, 랭크 행 크기, x4 / x8 / x16 칩의 페이지 크기(1 K 열 × DQ 폭)를 함수로 계산하라.

<details><summary>답 보기</summary>

`byte[2:0] col[2:0]=PA[5:3] col[9:3]=PA[12:6] BG=PA[14:13] BA=PA[16:15] row[16:0]=PA[33:17]` → 34비트 = **16 GiB** (8 칩 × 2 GiB). 랭크 행 = 2^(10+3) = **8 KB** (8 GB 칩에서와 같다 — 밀도가 올라가면 행 수가 는다, 행 길이는 그대로). 칩 페이지 x4 / x8 / x16 = **512 / 1024 / 2048 B**. 실측:

```text
ex1: ddr4-x8-16Gb 1R = 16 GiB, rank row 8192 B, chip page x4/x8/x16 = 512/1024/2048 B
```

x16 칩은 페이지가 2 KB 라 ACT 한 번이 더 많은 셀을 깨운다 → 그래서 x16 의 tFAW 가 더 길게 잡힌다 (값은 데이터시트).

</details>

### 연습 2 — DDR5 서브채널 매핑

DDR5 1 랭크 DIMM (서브채널 2 × 32-bit, x8 16 Gb 칩 = 8 BG × 4 bank, 64 K 행, 1 K 열 — 대표값)의 매핑을 만들어라. 64 B 라인이 BL16 한 번인지, 연속 64 라인이 몇 개의 (서브채널, BG, bank) 에 가는지, 한 서브채널 행의 크기는 얼마인지 확인하라.

<details><summary>답 보기</summary>

`byte[1:0] col[3:0]=PA[5:2] subch=PA[6] BG=PA[9:7] BA=PA[11:10] col[9:4]=PA[17:12] row=PA[33:18]` → 34비트 = 16 GiB. col 하위 4비트 × 4 B = **64 B = BL16 한 번**. 연속 64 라인 → **64 개 모두 다른** (subch, BG, bank) (2 × 8 × 4 = 64). 서브채널 행 = 1 K × 4 B = **4 KB** (DDR4 랭크 행 8 KB 의 절반). 랜덤 10만 왕복 불일치 0.

```text
ex2: DDR5 map 34 PA bits, 64 consecutive lines -> 64 distinct (subch,bg,bank), row = 4096 B per subchannel
```

</details>

### 연습 3 — DRAMA 식 역공학

매핑은 숨기고 "두 PA 가 같은 flat bank 인가"만 답하는 오라클이 있다. PA 비트 3..34 중 혼자 뒤집으면 뱅크가 바뀌는 비트를 찾고, 그 중 둘을 함께 뒤집으면 원래 뱅크로 돌아오는 쌍을 union-find 로 묶어 XOR 함수들을 복원하라. AX, C, A 에 적용하라.

<details><summary>답 보기</summary>

모든 단계가 GF(2) 선형이므로 bank(p ⊕ e) ⊕ bank(p) 는 e 만의 함수 → 기준 PA 0 하나로 충분하다. 비트 i, j 가 같은 함수에 있으면 둘을 같이 뒤집을 때 그 함수의 출력이 두 번 바뀌어 제자리. 실측:

```text
ex3: AX recovered     : {6} {14} {15,19} {16,20} {17,21} {18,22}
ex3: C recovered      : {6,12,13,19,20,21,22} {7,23} {8,24} {9,25} {10,26} {11,27}
```

C 의 채널 함수가 7비트 XOR 로 복원된다 (DRAMA 가 보고한 채널 함수들과 비슷한 모양). 한계: 한 PA 비트가 **두 함수에 동시에** 들어가면(예: row0 이 BG0 와 CH 둘 다에 XOR) 쌍 검사가 그 비트를 어느 쪽에도 묶지 못한다 — 그때는 "뱅크가 같은가" 대신 함수별 출력을 구분하는 실험이나 GF(2) 연립방정식 풀이가 필요하다. 실제 DRAMA 는 오라클이 타이밍이라 잡음이 있어 반복 측정·투표를 한다.

</details>

### 연습 4 — XOR 매핑에도 있는 최악 stride

stride = 2^a 또는 2^a + 2^b (6 ≤ a ≤ b ≤ 26), 256번 접근에서 max load 가 가장 큰 stride 를 A, AX, C 에 대해 찾아라.

<details><summary>답 보기</summary>

```text
ex4: worst stride (n=256) A: 0x80000 max_load 256 distinct 1 | AX: 0x800000 max_load 256 distinct 1 | C: 0x5000 max_load 129 distinct 2
```

A 는 2^19 (512 KB). AX 는 **2^23 (8 MB)** — row[3:0] 이 고정되는 첫 stride 라 해시가 아무 일도 안 한다. 해시에 row 4비트만 쓰면 캠핑 경계가 4비트 위로 옮겨질 뿐이다. C 는 256 KB 이상 stride 를 잘 퍼뜨리지만, 4 KB 배수 stride 가 8 MB 안에 머물면 BG/BA/rank 의 raw 비트(PA[11:7])도 XOR 입력(PA[27:23])도 고정이라 ch 만 바뀐다 → 2 뱅크 (0x5000 에서 129 : 127). 교훈: XOR 해시는 "흔한 stride" 를 퍼뜨리도록 설계된 것이지 모든 stride 에 대한 보장이 아니다. 성능 디버깅 때 의심할 stride 를 이 탐색으로 미리 뽑을 수 있다.

</details>

### 연습 5 — OS 페이지 하나가 건드리는 (bank, row) 수

PA 0x1_2345_0000 의 4 KB 페이지와 16 KB 페이지(이 맥 M6 의 페이지 크기)가 몇 개의 서로 다른 (flat bank, row) 를 건드리는지 A 와 B 에서 세라. OS 가 페이지 단위로 뱅크를 나눠 주는 bank coloring 이 가능한가?

<details><summary>답 보기</summary>

```text
ex5: (bank,row) per OS page  A: 4KB=2 16KB=2 | B: 4KB=64 16KB=64
```

A: 페이지 오프셋(PA[11:0] 또는 PA[13:0]) 안의 뱅크 선택 비트는 ch(PA6) 하나 → (bank,row) 2개. rank/BG/BA 는 PA[18:14] 라 **페이지 번호** 에 있으므로 OS 가 페이지를 골라 주는 것만으로 뱅크를 색칠할 수 있다 (PALLOC 같은 bank-aware allocator 의 전제). B: 페이지 하나가 64 뱅크를 다 건드린다 → 페이지 단위 뱅크 분할 불가. 같은 하드웨어라도 매핑 하나로 "실시간 시스템에서 코어별 메모리 간섭을 나눌 수 있는가"가 갈린다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 20/20 passed, exit 0
```

## 10. 핵심 정리

- 계층: 채널(독립 버스) > 랭크(CS_n, 칩 8개 × x8 = 64-bit) > BG > 뱅크(행 버퍼 1개) > 행(랭크 기준 8 KB) > 열(8 B). BL8 = 64 B = 라인 1개. PA[2:0] 은 beat 안 바이트.
- x8 8 Gb = 16 뱅크 × 64 K 행 × 1 K 열 × 8 bit. 2 ch × 2 rank → 64 flat bank, 32 GiB, PA 35비트.
- 매핑 = 비트 자리 바꿈 S + XOR X. X = [[I, H], [0, I]] 는 GF(2) 에서 자기 역 → encode = S⁻¹∘X. 해시 입력은 해싱 안 되는 ROW/COL 만.
- 표는 데이터다: 겹침·빈틈·필드 연속·XOR 형태를 검사하는 validate 를 두고, 작은 매핑은 전수, 큰 매핑은 랜덤 왕복으로 테스트.
- 행 우선 매핑(A)은 행 hit, 라인 인터리브(B)는 뱅크 병렬성. 2^19 이상 stride 에서 A 는 1 뱅크(직렬화 1024), AX 는 16 뱅크(64) — conflict 비율은 그대로.
- XOR 도 해시에 안 쓴 비트 위의 stride(AX: 2^23)는 못 막는다. 실제 CPU 해시는 많은 비트를 쓴다 (DRAMA).
- 크래시 덤프: 이웃 행은 decode → row ± 1 → encode (PA ± 행 stride 아님). 한 행이 덮는 페이지: A 4개, B 128개. 매핑을 틀리면 뱅크·PPR 대상이 틀린다.

## 참고자료

- JEDEC JESD79-4 DDR4 SDRAM 표준 — 뱅크 그룹, 주소 비트(BG/BA/A), 페이지 크기 표 (등록 후 열람; 이 레슨의 수치는 대표값): <https://www.jedec.org/>
- Micron DDR4 SDRAM 8 Gb / 16 Gb 데이터시트 (MT40A 계열) — x4/x8/x16 의 행·열·뱅크 주소 표 (부품별 확인).
- P. Pessl, D. Gruss, C. Maurice, M. Schwarz, S. Mangard, "DRAMA: Exploiting DRAM Addressing for Cross-CPU Attacks", USENIX Security 2016 — 타이밍으로 XOR 뱅크/채널 함수 복원 (연습 3, 함수 모양 비교).
- Z. Zhang, Z. Zhu, X. Zhang, "A Permutation-based Page Interleaving Scheme to Reduce Row-buffer Conflicts and Exploit Data Locality", MICRO 2000 — 뱅크 ⊕ row XOR 해싱 (매핑 AX 의 원형).
- B. Jacob, S. Ng, D. Wang, *Memory Systems: Cache, DRAM, Disk* — DRAM 조직과 주소 매핑 장.
- U. Drepper, "What Every Programmer Should Know About Memory" (2007) §2 — DRAM 구조 입문: <https://people.freebsd.org/~lstewart/articles/cpumemory.pdf>
- Ramulator 2.0 — 매핑 이름 규칙(RoBaRaCoCh 등, MSB → LSB)과 구현: <https://github.com/CMU-SAFARI/ramulator2>
- Linux kernel EDAC 문서 — 메모리 컨트롤러 / csrow / DIMM 보고 모델: <https://docs.kernel.org/driver-api/edac.html>
- H. Yun 외, "PALLOC: DRAM Bank-Aware Memory Allocator for Performance Isolation on Multicore Platforms", RTAS 2014 — bank coloring (연습 5).
- 관련 레슨: MEM-B02 (명령·타이밍), MEM-B03 (row hit/conflict · 페이지 정책), MEM-B04 (DDR4/DDR5/LPDDR 비교), MEM-B07 (72/64 ECC), MEM-B10 · MEM-B11 (이 맥에서 지연·대역폭 실측), MEM-M01 · MEM-M02 (스케줄링 · 매핑과 병렬성), MEM-A01 (BG · tFAW), MEM-A02 (Row Hammer), MEM-A06 (CE/UE · page retire), SIV-M14 (ECC 오류 주입), ARCH-A02 · ARCH-A03 (TLB 와 페이지), OS-B10 (페이징)
