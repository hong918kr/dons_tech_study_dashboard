---
id: MEM-B07
time: 60분
updated: 2026-11-15
---
# MEM-B07 SECDED Hamming(72,64) 구현

## 학습 목표

- 64비트 데이터 + 8비트 체크(7 Hamming + 1 전체 패리티)의 **구체적인 비트 배치**를 정하고, 체크 바이트를 마스크 7개의 패리티로 계산하는 인코더를 짠다.
- 신드롬 바이트 `s | p<<7` 의 대수(H·(c ⊕ e) = H·e)로 **단일 = 정정, 2비트 = 검출** 이 왜 항상 성립하는지(최소 거리 4) 증명하고, 256 B 신드롬 → 비트 위치 표를 init 에서 만든다.
- 72 위치 단일 오류 전수, 2비트 전수(2,556) + 무작위 1,000,000회(오정정 0), 3비트 전수(59,640 중 76.0 % 오정정)로 **보장과 한계를 숫자로** 말한다.
- 메모리 컨트롤러 읽기 경로(정정 데이터 + 상태 + CE/UE 카운터 + scrub-on-CE)를 작은 API 로 짜고, scrub 이 없으면 1비트 오류가 쌓여 UE 가 되는 과정을 테스트로 보인다.
- 꼬리질문(왜 8번째 비트, 왜 72/64, Hsiao 와의 차이, 부분 쓰기 RMW, DRAM ECC vs NAND ECC)에 실측 숫자로 답한다.

## 1. 문제

서버 DIMM, HBM, 자동차 SoC 의 SRAM 은 거의 다 SECDED(Single Error Correct, Double Error Detect)로 보호되고, DDR bring-up · ECC 검증 · EDAC 로그 분석에서 "체크 비트가 어떻게 계산되고 신드롬이 무엇을 뜻하는지"를 손으로 설명할 수 있어야 한다 — 실리콘 검증과 펌웨어 면접의 단골이다.

MEM-B06 에서 Hamming(7,4)로 원리를 봤다. 이번엔 **실제 비트 폭** 이다. DDR4 ECC DIMM 은 64비트 데이터 버스 옆에 8비트 체크 버스(CB[7:0])가 있어 x8 칩 9개(또는 x4 칩 18개)로 72비트를 한 번에 읽는다. 컨트롤러는 쓸 때 체크 바이트를 만들고, 읽을 때 신드롬을 계산해 1비트면 고쳐서 넘기고(CE, Correctable Error), 2비트면 고치지 않고 알린다(UE, Uncorrectable Error). Don 이 SSD 에서 본 NAND ECC(BCH/LDPC, 1 KB 단위에 수십 비트 정정)와 달리, DRAM ECC 는 **읽기 경로에 한 사이클 남짓** 밖에 쓸 수 없어서 이렇게 작은 코드를 쓴다.

이 레슨의 레이아웃은 **고전적 위치형 Hamming** 이다 (SIV-M14 가 축약해 쓰는 것과 같다).

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="14" font-weight="bold">코드워드 위치 0..71: 체크비트는 위치 0 과 2의 거듭제곱, 데이터 d0..d63 은 나머지 위치에 차례로</text>
  <text x="4" y="52" fill="currentColor">위치</text><text x="4" y="76" fill="currentColor">내용</text>
  <g fill="currentColor" text-anchor="middle"><text x="49.5" y="52">0</text><text x="68.5" y="52">1</text><text x="87.5" y="52">2</text><text x="106.5" y="52">3</text><text x="125.5" y="52">4</text><text x="144.5" y="52">5</text><text x="163.5" y="52">6</text><text x="182.5" y="52">7</text><text x="201.5" y="52">8</text><text x="220.5" y="52">9</text><text x="239.5" y="52">10</text><text x="258.5" y="52">11</text><text x="277.5" y="52">12</text><text x="296.5" y="52">13</text><text x="315.5" y="52">14</text><text x="334.5" y="52">15</text><text x="353.5" y="52">16</text><text x="372.5" y="52">17</text><text x="391.5" y="52">18</text><text x="410.5" y="52">19</text><text x="429.5" y="52">20</text><text x="448.5" y="52">21</text><text x="467.5" y="52">22</text><text x="486.5" y="52">23</text><text x="505.5" y="52">24</text><text x="524.5" y="52">25</text><text x="543.5" y="52">26</text><text x="562.5" y="52">27</text><text x="581.5" y="52">28</text><text x="600.5" y="52">29</text><text x="619.5" y="52">30</text><text x="638.5" y="52">31</text><text x="657.5" y="52">32</text><text x="676.5" y="52">33</text><text x="695.5" y="52">34</text><text x="714.5" y="52">35</text></g>
  <g stroke="currentColor" stroke-width="0.6"><rect x="40" y="60" width="19" height="24" class="f-warn"/><rect x="59" y="60" width="19" height="24" class="f-acc"/><rect x="78" y="60" width="19" height="24" class="f-acc"/><rect x="97" y="60" width="19" height="24" class="f-mut"/><rect x="116" y="60" width="19" height="24" class="f-acc"/><rect x="135" y="60" width="19" height="24" class="f-mut"/><rect x="154" y="60" width="19" height="24" class="f-mut"/><rect x="173" y="60" width="19" height="24" class="f-mut"/><rect x="192" y="60" width="19" height="24" class="f-acc"/><rect x="211" y="60" width="19" height="24" class="f-mut"/><rect x="230" y="60" width="19" height="24" class="f-mut"/><rect x="249" y="60" width="19" height="24" class="f-mut"/><rect x="268" y="60" width="19" height="24" class="f-mut"/><rect x="287" y="60" width="19" height="24" class="f-mut"/><rect x="306" y="60" width="19" height="24" class="f-mut"/><rect x="325" y="60" width="19" height="24" class="f-mut"/><rect x="344" y="60" width="19" height="24" class="f-acc"/><rect x="363" y="60" width="19" height="24" class="f-mut"/><rect x="382" y="60" width="19" height="24" class="f-mut"/><rect x="401" y="60" width="19" height="24" class="f-mut"/><rect x="420" y="60" width="19" height="24" class="f-mut"/><rect x="439" y="60" width="19" height="24" class="f-mut"/><rect x="458" y="60" width="19" height="24" class="f-mut"/><rect x="477" y="60" width="19" height="24" class="f-mut"/><rect x="496" y="60" width="19" height="24" class="f-mut"/><rect x="515" y="60" width="19" height="24" class="f-mut"/><rect x="534" y="60" width="19" height="24" class="f-mut"/><rect x="553" y="60" width="19" height="24" class="f-mut"/><rect x="572" y="60" width="19" height="24" class="f-mut"/><rect x="591" y="60" width="19" height="24" class="f-mut"/><rect x="610" y="60" width="19" height="24" class="f-mut"/><rect x="629" y="60" width="19" height="24" class="f-mut"/><rect x="648" y="60" width="19" height="24" class="f-acc"/><rect x="667" y="60" width="19" height="24" class="f-mut"/><rect x="686" y="60" width="19" height="24" class="f-mut"/><rect x="705" y="60" width="19" height="24" class="f-mut"/></g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="49.5" y="76">c7</text><text x="68.5" y="76">c0</text><text x="87.5" y="76">c1</text><text x="106.5" y="76">0</text><text x="125.5" y="76">c2</text><text x="144.5" y="76">1</text><text x="163.5" y="76">2</text><text x="182.5" y="76">3</text><text x="201.5" y="76">c3</text><text x="220.5" y="76">4</text><text x="239.5" y="76">5</text><text x="258.5" y="76">6</text><text x="277.5" y="76">7</text><text x="296.5" y="76">8</text><text x="315.5" y="76">9</text><text x="334.5" y="76">10</text><text x="353.5" y="76">c4</text><text x="372.5" y="76">11</text><text x="391.5" y="76">12</text><text x="410.5" y="76">13</text><text x="429.5" y="76">14</text><text x="448.5" y="76">15</text><text x="467.5" y="76">16</text><text x="486.5" y="76">17</text><text x="505.5" y="76">18</text><text x="524.5" y="76">19</text><text x="543.5" y="76">20</text><text x="562.5" y="76">21</text><text x="581.5" y="76">22</text><text x="600.5" y="76">23</text><text x="619.5" y="76">24</text><text x="638.5" y="76">25</text><text x="657.5" y="76">c5</text><text x="676.5" y="76">26</text><text x="695.5" y="76">27</text><text x="714.5" y="76">28</text></g>
  <text x="4" y="144" fill="currentColor">위치</text><text x="4" y="168" fill="currentColor">내용</text>
  <g fill="currentColor" text-anchor="middle"><text x="49.5" y="144">36</text><text x="68.5" y="144">37</text><text x="87.5" y="144">38</text><text x="106.5" y="144">39</text><text x="125.5" y="144">40</text><text x="144.5" y="144">41</text><text x="163.5" y="144">42</text><text x="182.5" y="144">43</text><text x="201.5" y="144">44</text><text x="220.5" y="144">45</text><text x="239.5" y="144">46</text><text x="258.5" y="144">47</text><text x="277.5" y="144">48</text><text x="296.5" y="144">49</text><text x="315.5" y="144">50</text><text x="334.5" y="144">51</text><text x="353.5" y="144">52</text><text x="372.5" y="144">53</text><text x="391.5" y="144">54</text><text x="410.5" y="144">55</text><text x="429.5" y="144">56</text><text x="448.5" y="144">57</text><text x="467.5" y="144">58</text><text x="486.5" y="144">59</text><text x="505.5" y="144">60</text><text x="524.5" y="144">61</text><text x="543.5" y="144">62</text><text x="562.5" y="144">63</text><text x="581.5" y="144">64</text><text x="600.5" y="144">65</text><text x="619.5" y="144">66</text><text x="638.5" y="144">67</text><text x="657.5" y="144">68</text><text x="676.5" y="144">69</text><text x="695.5" y="144">70</text><text x="714.5" y="144">71</text></g>
  <g stroke="currentColor" stroke-width="0.6"><rect x="40" y="152" width="19" height="24" class="f-mut"/><rect x="59" y="152" width="19" height="24" class="f-mut"/><rect x="78" y="152" width="19" height="24" class="f-mut"/><rect x="97" y="152" width="19" height="24" class="f-mut"/><rect x="116" y="152" width="19" height="24" class="f-mut"/><rect x="135" y="152" width="19" height="24" class="f-mut"/><rect x="154" y="152" width="19" height="24" class="f-mut"/><rect x="173" y="152" width="19" height="24" class="f-mut"/><rect x="192" y="152" width="19" height="24" class="f-mut"/><rect x="211" y="152" width="19" height="24" class="f-mut"/><rect x="230" y="152" width="19" height="24" class="f-mut"/><rect x="249" y="152" width="19" height="24" class="f-mut"/><rect x="268" y="152" width="19" height="24" class="f-mut"/><rect x="287" y="152" width="19" height="24" class="f-mut"/><rect x="306" y="152" width="19" height="24" class="f-mut"/><rect x="325" y="152" width="19" height="24" class="f-mut"/><rect x="344" y="152" width="19" height="24" class="f-mut"/><rect x="363" y="152" width="19" height="24" class="f-mut"/><rect x="382" y="152" width="19" height="24" class="f-mut"/><rect x="401" y="152" width="19" height="24" class="f-mut"/><rect x="420" y="152" width="19" height="24" class="f-mut"/><rect x="439" y="152" width="19" height="24" class="f-mut"/><rect x="458" y="152" width="19" height="24" class="f-mut"/><rect x="477" y="152" width="19" height="24" class="f-mut"/><rect x="496" y="152" width="19" height="24" class="f-mut"/><rect x="515" y="152" width="19" height="24" class="f-mut"/><rect x="534" y="152" width="19" height="24" class="f-mut"/><rect x="553" y="152" width="19" height="24" class="f-mut"/><rect x="572" y="152" width="19" height="24" class="f-acc"/><rect x="591" y="152" width="19" height="24" class="f-mut"/><rect x="610" y="152" width="19" height="24" class="f-mut"/><rect x="629" y="152" width="19" height="24" class="f-mut"/><rect x="648" y="152" width="19" height="24" class="f-mut"/><rect x="667" y="152" width="19" height="24" class="f-mut"/><rect x="686" y="152" width="19" height="24" class="f-mut"/><rect x="705" y="152" width="19" height="24" class="f-mut"/></g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="49.5" y="168">29</text><text x="68.5" y="168">30</text><text x="87.5" y="168">31</text><text x="106.5" y="168">32</text><text x="125.5" y="168">33</text><text x="144.5" y="168">34</text><text x="163.5" y="168">35</text><text x="182.5" y="168">36</text><text x="201.5" y="168">37</text><text x="220.5" y="168">38</text><text x="239.5" y="168">39</text><text x="258.5" y="168">40</text><text x="277.5" y="168">41</text><text x="296.5" y="168">42</text><text x="315.5" y="168">43</text><text x="334.5" y="168">44</text><text x="353.5" y="168">45</text><text x="372.5" y="168">46</text><text x="391.5" y="168">47</text><text x="410.5" y="168">48</text><text x="429.5" y="168">49</text><text x="448.5" y="168">50</text><text x="467.5" y="168">51</text><text x="486.5" y="168">52</text><text x="505.5" y="168">53</text><text x="524.5" y="168">54</text><text x="543.5" y="168">55</text><text x="562.5" y="168">56</text><text x="581.5" y="168">c6</text><text x="600.5" y="168">57</text><text x="619.5" y="168">58</text><text x="638.5" y="168">59</text><text x="657.5" y="168">60</text><text x="676.5" y="168">61</text><text x="695.5" y="168">62</text><text x="714.5" y="168">63</text></g>
  <rect x="40" y="226" width="14" height="12" class="f-acc"/><text x="60" y="237" fill="currentColor">Hamming 체크 c0..c6 (위치 1,2,4,…,64)</text>
  <rect x="330" y="226" width="14" height="12" class="f-warn"/><text x="350" y="237" fill="currentColor">c7 = 전체 패리티 (위치 0)</text>
  <rect x="540" y="226" width="14" height="12" class="f-mut"/><text x="560" y="237" fill="currentColor">데이터 비트 번호 i</text>
  <text x="40" y="262" fill="currentColor">예: 위치 44 = d37, 위치 71 = d63. 저장은 DQ[63:0] = d63..d0 + 체크 바이트 CB[7:0] = c7..c0.</text>
  <text x="40" y="284" fill="currentColor">정상 코드워드: 1 인 비트들의 위치 XOR = 0 (s = 0), 72비트 전체 패리티 = 0 (p = 0).</text>
</svg>
```

### Part A — 코덱: 테이블, 인코더, 신드롬, 디코더

- `secded_init()` 이 세 테이블을 만든다: `POS[64]`(데이터 비트 → 위치), `CMASK[7]`(체크 c_j 에 들어가는 데이터 비트 마스크), `SYN_TAB[256]`(신드롬 바이트 → 비트 번호 0..71 / UE / 정상). 표 크기를 출력한다.
- `secded_encode(d)` → 체크 바이트. 정의대로 72칸 배열에 놓고 XOR 하는 느린 기준 구현 `ref_encode` 와 무작위 100,000 워드를 대조한다.
- `secded_syndrome(d, c)` → `s | p<<7`. `secded_decode(&d, &c)` → `{NO_ERR, CE_DATA k, CE_CHECK j, UE}` + 신드롬. CE 면 제자리에서 고치고, UE 면 아무것도 바꾸지 않는다.

### Part B — 오류 주입: 보장과 정직한 한계

- 72 위치 단일 오류 × 64 데이터 워드(경계값 7개 + 무작위 57개) → 전부 정정되고 보고된 비트 번호도 맞아야 한다.
- 2비트: 전수 C(72,2) = 2,556 쌍 × 데이터 4개, 그리고 고정 시드 무작위 **1,000,000회** → 전부 UE, 오정정 0, 무성 오염 0.
- 3비트: 전수 C(72,3) = 59,640 → SECDED 가 약속하지 않는 영역. UE / 오정정(데이터·체크)을 **세서** 보여 준다. 데이터 두 개로 돌려 결과가 같음(선형성)도 확인한다.

### Part C — 메모리 컨트롤러 읽기 경로

- 256 워드 `mc_t` (data[] + chk[] = DRAM 배열, ce_cnt/ue_cnt, 마지막 CE/UE 주소·신드롬, `scrub_on_ce` 플래그).
- `ecc_write(m, addr, d)` / `ecc_read(m, addr, &out, &res)` — 범위 밖 −1. CE 면 정정 데이터 + 카운터 + (켜져 있으면) 고친 코드워드를 다시 쓰기. UE 면 원시 데이터를 돌려주고 배열은 건드리지 않는다.
- 테스트 훅 `mc_inject(m, addr, bit)` 로 셀을 뒤집어: scrub 없이 두 번째 셀이 뒤집히면 UE, scrub 이 있으면 다시 CE 로 끝나는 것을 확인한다. 보고 정책(임계값, 인터럽트, page retire)은 SIV-M14 · MEM-A06 몫이다.

## 2. Problem (English)

Implement a SECDED (72,64) codec for a DDR memory controller model.

```c
typedef enum { ECC_NO_ERR, ECC_CE_DATA, ECC_CE_CHECK, ECC_UE } ecc_status_t;
typedef struct { ecc_status_t st; int bit; uint8_t syn; } ecc_result_t;  /* bit: data 0..63 / check 0..7 / -1 */

void         secded_init(void);                         /* build POS[64], CMASK[7], SYN_TAB[256] */
uint8_t      secded_encode(uint64_t d);                 /* check byte c7..c0                     */
uint8_t      secded_syndrome(uint64_t d, uint8_t c);    /* s | p<<7                              */
ecc_result_t secded_decode(uint64_t *d, uint8_t *c);    /* corrects in place on CE; untouched on UE */
int          ecc_write(mc_t *m, uint32_t addr, uint64_t d);                       /* -1 if out of range */
int          ecc_read (mc_t *m, uint32_t addr, uint64_t *out, ecc_result_t *res); /* -1 if out of range */
```

Layout: codeword positions 0..71; check bit c_j sits at position 2^j (j = 0..6), the overall parity c7 at position 0, data bits d0..d63 fill the remaining positions in increasing order. Storage is data (DQ[63:0]) plus one check byte (CB[7:0]). Bit numbers for injection and reporting: data 0..63, check c0..c7 = 64..71.

Constraints: no heap; O(1) decode (table lookup); deterministic tests (fixed-seed xorshift64); self-test < 2 s. The decoder must never modify data on UE and must never report a double-bit error as correctable.

Examples:

```text
encode(0x0000000000000000) = 0x00      encode(0x0000000000000001) = 0x83
encode(0x8000000000000000) = 0xC7      encode(0x0123456789ABCDEF) = 0x9C      encode(~0) = 0xFF
decode(0x0123456789ABCDEF ^ (1<<37), 0x9C) -> CE_DATA bit 37, syn 0xAC, data restored
decode(0x0123456789ABCDEF, 0x9C ^ 0x80)    -> CE_CHECK bit 7 (c7), syn 0x80
decode(0x0123456789ABCDEF ^ 0x3, 0x9C)     -> UE, syn 0x06 (positions 3 and 5), nothing changed
```

Follow-ups: Why does SECDED need the eighth bit? Why 72/64 and not 137/128? What happens on three flipped bits? How does a Hsiao code differ? What does ECC do to a 1-byte write?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여덟 군데다: Part A 4개(`secded_init`, `secded_encode`, `secded_syndrome`, `secded_decode`), Part B 2개(`camp_trial`, `pick_distinct`), Part C 2개(`ecc_write`, `ecc_read`). `parity64`, `xs64`, 기준 구현 `ref_encode`, `flip72`, 캠페인 루프 `run_random` / `run_exhaustive`, `mc_init` / `mc_inject`, 하네스는 주어진다. 스텁은 안전한 틀린 값을 돌려준다 (`pick_distinct` 는 0,1,2… 를 채워 루프가 끝나게 한다). 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:191  POS[0] == 3 && POS[1] == 5 && POS[4] == 9 && POS[37] == 44 && POS[63] == 71
FAIL starter.c:199  ok
SYN_TAB: 256 bytes, CE entries=256, UE entries=0, NO_ERR entries=0
FAIL starter.c:210  ce == 72 && ue == 183 && noerr == 1
…
FAIL starter.c:335  ecc_read(&m, MC_WORDS, &v, &r) == -1 && ecc_write(&m, MC_WORDS, 1) == -1
5/36 passed
```

(exit 1. 통과한 5개는 스텁이 우연히 맞는 경우다: `encode(0) == 0`, "깨끗한 코드워드의 신드롬 0", 시도 0회짜리 캠페인의 "오정정 0" 같은 것.) 빈 `SYN_TAB` 가 "CE 엔트리 256개"로 세어지는 것도 눈여겨보라 — 7절 실수 4 의 정체다.

## 4. 힌트

<details><summary>힌트 1 — 위치 표와 체크 마스크</summary>

1..71 을 돌며 2의 거듭제곱이 아닌 위치만 차례로 `POS[i++]` 에 넣는다 (`p & (p−1) != 0`). 그러면 POS[0] = 3, POS[63] = 71. 체크 c_j 는 "위치의 비트 j 가 1 인 데이터 비트들의 XOR" 이므로 `CMASK[j]` 에 `(POS[k] >> j) & 1` 인 k 를 모은다. 인코드는 `c_j = parity64(d & CMASK[j])` 일곱 번.

</details>

<details><summary>힌트 2 — c7 은 72비트 전체가 짝수가 되게</summary>

c7 을 뺀 71비트(데이터 64 + c0..c6)의 패리티가 c7 이다: `c7 = parity64(d) ^ parity64(c & 0x7F)`. 신드롬 쪽 p 는 **체크 바이트 8비트 전부** 를 포함해 `parity64(d) ^ parity64(c)`.

</details>

<details><summary>힌트 3 — 신드롬은 인코더를 한 번 더 돌린 것</summary>

`s = (인코더가 d 로 다시 계산한 c0..c6) ^ (저장된 c0..c6)`. 이게 "1 인 비트들의 위치 XOR" 과 같은 이유: 정상 코드워드에선 0 이고, 위치 q 의 비트를 뒤집으면 XOR 에 q 가 더해진다.

</details>

<details><summary>힌트 4 — SYN_TAB 의 기본값은 UE</summary>

`memset(SYN_TAB, SYN_UE, …)` 로 시작해서 정상(0x00) 1개, c7(0x80) 1개, c_j(0x80 | 1<<j) 7개, 데이터(0x80 | POS[k]) 64개만 덮어쓴다. p = 0 이고 s ≠ 0 인 127개, p = 1 이고 s = 72..127 인 56개가 UE 로 남는다.

</details>

<details><summary>힌트 5 — 캠페인의 분류 기준</summary>

원본 (d0, c0) 를 기억해 두고 디코드 결과와 비교한다. CE 라고 했는데 원본과 다르면 오정정, NO_ERR 인데 다르면 무성 오염(silent data corruption), UE 면 저장값이 그대로인지 확인한다. 무작위 k비트는 **서로 다른** k 위치여야 한다 — 같은 비트를 두 번 뒤집으면 오류가 사라진다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **레이아웃을 먼저 못 박는다.** "위치형 Hamming: 체크 c_j 를 위치 2^j 에, 전체 패리티를 위치 0 에, 데이터를 나머지에. 저장은 데이터 64비트 + 체크 바이트."
2. **선형성 한 줄.** 저장값 = c ⊕ e 이면 신드롬 = H·e. 데이터와 무관하고 오류 패턴만의 함수다.
3. **판정 규칙.** p = 1 이면 홀수 개 → 1개로 보고 위치 s 를 고친다. p = 0, s ≠ 0 이면 짝수 개(≥ 2) → UE.
4. **보장의 경계.** 최소 거리 4 → 1개 정정 + 2개 검출. 3개 이상은 약속 없음 (실측 76 % 오정정).
5. **구현 비용.** 인코더 = 8개의 XOR 트리(가장 넓은 것 35입력), 디코더 = 같은 트리 + 256 엔트리 표(하드웨어에선 72개 비교기).

### Part A — 레이아웃과 인코더

이 레슨의 레이아웃은 위의 그림 그대로다. 위치형을 고른 이유는 **신드롬이 곧 오류 위치** 라 손으로 추적하기 쉽기 때문이다. 저장은 어차피 "데이터 64 + 체크 8" 로 분리돼 있으므로(DQ/CB), 위치는 H 행렬의 열 순서를 정하는 수학적 번호일 뿐이다. 실제 컨트롤러는 대개 Hsiao 같은 체계적(systematic) H 를 쓴다 (Q4, 연습 5).

```svg
<svg viewBox="0 0 760 290" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="14" font-weight="bold">H 행렬 8×72 — 열 = 위치 0..71, 칠한 칸 = 그 식에 들어가는 비트</text>
  <g fill="currentColor"><text x="78" y="42">0</text><text x="142" y="42">8</text><text x="206" y="42">16</text><text x="270" y="42">24</text><text x="334" y="42">32</text><text x="398" y="42">40</text><text x="462" y="42">48</text><text x="526" y="42">56</text><text x="590" y="42">64</text></g>
  <text x="6" y="62" fill="currentColor">s0 (c0)</text><rect x="78" y="50" width="575" height="15" fill="none" stroke="currentColor" stroke-width="0.4"/><path class="f-ok" d="M102 50h7v15h-7zM118 50h7v15h-7zM134 50h7v15h-7zM150 50h7v15h-7zM166 50h7v15h-7zM182 50h7v15h-7zM198 50h7v15h-7zM214 50h7v15h-7zM230 50h7v15h-7zM246 50h7v15h-7zM262 50h7v15h-7zM278 50h7v15h-7zM294 50h7v15h-7zM310 50h7v15h-7zM326 50h7v15h-7zM342 50h7v15h-7zM358 50h7v15h-7zM374 50h7v15h-7zM390 50h7v15h-7zM406 50h7v15h-7zM422 50h7v15h-7zM438 50h7v15h-7zM454 50h7v15h-7zM470 50h7v15h-7zM486 50h7v15h-7zM502 50h7v15h-7zM518 50h7v15h-7zM534 50h7v15h-7zM550 50h7v15h-7zM566 50h7v15h-7zM582 50h7v15h-7zM598 50h7v15h-7zM614 50h7v15h-7zM630 50h7v15h-7zM646 50h7v15h-7z"/><path class="f-acc" d="M86 50h7v15h-7z"/><text x="662" y="62" fill="currentColor">데이터 35</text>
  <text x="6" y="80" fill="currentColor">s1 (c1)</text><rect x="78" y="68" width="575" height="15" fill="none" stroke="currentColor" stroke-width="0.4"/><path class="f-ok" d="M102 68h7v15h-7zM126 68h7v15h-7zM134 68h7v15h-7zM158 68h7v15h-7zM166 68h7v15h-7zM190 68h7v15h-7zM198 68h7v15h-7zM222 68h7v15h-7zM230 68h7v15h-7zM254 68h7v15h-7zM262 68h7v15h-7zM286 68h7v15h-7zM294 68h7v15h-7zM318 68h7v15h-7zM326 68h7v15h-7zM350 68h7v15h-7zM358 68h7v15h-7zM382 68h7v15h-7zM390 68h7v15h-7zM414 68h7v15h-7zM422 68h7v15h-7zM446 68h7v15h-7zM454 68h7v15h-7zM478 68h7v15h-7zM486 68h7v15h-7zM510 68h7v15h-7zM518 68h7v15h-7zM542 68h7v15h-7zM550 68h7v15h-7zM574 68h7v15h-7zM582 68h7v15h-7zM606 68h7v15h-7zM614 68h7v15h-7zM638 68h7v15h-7zM646 68h7v15h-7z"/><path class="f-acc" d="M94 68h7v15h-7z"/><text x="662" y="80" fill="currentColor">데이터 35</text>
  <text x="6" y="98" fill="currentColor">s2 (c2)</text><rect x="78" y="86" width="575" height="15" fill="none" stroke="currentColor" stroke-width="0.4"/><path class="f-ok" d="M118 86h7v15h-7zM126 86h7v15h-7zM134 86h7v15h-7zM174 86h7v15h-7zM182 86h7v15h-7zM190 86h7v15h-7zM198 86h7v15h-7zM238 86h7v15h-7zM246 86h7v15h-7zM254 86h7v15h-7zM262 86h7v15h-7zM302 86h7v15h-7zM310 86h7v15h-7zM318 86h7v15h-7zM326 86h7v15h-7zM366 86h7v15h-7zM374 86h7v15h-7zM382 86h7v15h-7zM390 86h7v15h-7zM430 86h7v15h-7zM438 86h7v15h-7zM446 86h7v15h-7zM454 86h7v15h-7zM494 86h7v15h-7zM502 86h7v15h-7zM510 86h7v15h-7zM518 86h7v15h-7zM558 86h7v15h-7zM566 86h7v15h-7zM574 86h7v15h-7zM582 86h7v15h-7zM622 86h7v15h-7zM630 86h7v15h-7zM638 86h7v15h-7zM646 86h7v15h-7z"/><path class="f-acc" d="M110 86h7v15h-7z"/><text x="662" y="98" fill="currentColor">데이터 35</text>
  <text x="6" y="116" fill="currentColor">s3 (c3)</text><rect x="78" y="104" width="575" height="15" fill="none" stroke="currentColor" stroke-width="0.4"/><path class="f-ok" d="M150 104h7v15h-7zM158 104h7v15h-7zM166 104h7v15h-7zM174 104h7v15h-7zM182 104h7v15h-7zM190 104h7v15h-7zM198 104h7v15h-7zM270 104h7v15h-7zM278 104h7v15h-7zM286 104h7v15h-7zM294 104h7v15h-7zM302 104h7v15h-7zM310 104h7v15h-7zM318 104h7v15h-7zM326 104h7v15h-7zM398 104h7v15h-7zM406 104h7v15h-7zM414 104h7v15h-7zM422 104h7v15h-7zM430 104h7v15h-7zM438 104h7v15h-7zM446 104h7v15h-7zM454 104h7v15h-7zM526 104h7v15h-7zM534 104h7v15h-7zM542 104h7v15h-7zM550 104h7v15h-7zM558 104h7v15h-7zM566 104h7v15h-7zM574 104h7v15h-7zM582 104h7v15h-7z"/><path class="f-acc" d="M142 104h7v15h-7z"/><text x="662" y="116" fill="currentColor">데이터 31</text>
  <text x="6" y="134" fill="currentColor">s4 (c4)</text><rect x="78" y="122" width="575" height="15" fill="none" stroke="currentColor" stroke-width="0.4"/><path class="f-ok" d="M214 122h7v15h-7zM222 122h7v15h-7zM230 122h7v15h-7zM238 122h7v15h-7zM246 122h7v15h-7zM254 122h7v15h-7zM262 122h7v15h-7zM270 122h7v15h-7zM278 122h7v15h-7zM286 122h7v15h-7zM294 122h7v15h-7zM302 122h7v15h-7zM310 122h7v15h-7zM318 122h7v15h-7zM326 122h7v15h-7zM462 122h7v15h-7zM470 122h7v15h-7zM478 122h7v15h-7zM486 122h7v15h-7zM494 122h7v15h-7zM502 122h7v15h-7zM510 122h7v15h-7zM518 122h7v15h-7zM526 122h7v15h-7zM534 122h7v15h-7zM542 122h7v15h-7zM550 122h7v15h-7zM558 122h7v15h-7zM566 122h7v15h-7zM574 122h7v15h-7zM582 122h7v15h-7z"/><path class="f-acc" d="M206 122h7v15h-7z"/><text x="662" y="134" fill="currentColor">데이터 31</text>
  <text x="6" y="152" fill="currentColor">s5 (c5)</text><rect x="78" y="140" width="575" height="15" fill="none" stroke="currentColor" stroke-width="0.4"/><path class="f-ok" d="M342 140h7v15h-7zM350 140h7v15h-7zM358 140h7v15h-7zM366 140h7v15h-7zM374 140h7v15h-7zM382 140h7v15h-7zM390 140h7v15h-7zM398 140h7v15h-7zM406 140h7v15h-7zM414 140h7v15h-7zM422 140h7v15h-7zM430 140h7v15h-7zM438 140h7v15h-7zM446 140h7v15h-7zM454 140h7v15h-7zM462 140h7v15h-7zM470 140h7v15h-7zM478 140h7v15h-7zM486 140h7v15h-7zM494 140h7v15h-7zM502 140h7v15h-7zM510 140h7v15h-7zM518 140h7v15h-7zM526 140h7v15h-7zM534 140h7v15h-7zM542 140h7v15h-7zM550 140h7v15h-7zM558 140h7v15h-7zM566 140h7v15h-7zM574 140h7v15h-7zM582 140h7v15h-7z"/><path class="f-acc" d="M334 140h7v15h-7z"/><text x="662" y="152" fill="currentColor">데이터 31</text>
  <text x="6" y="170" fill="currentColor">s6 (c6)</text><rect x="78" y="158" width="575" height="15" fill="none" stroke="currentColor" stroke-width="0.4"/><path class="f-ok" d="M598 158h7v15h-7zM606 158h7v15h-7zM614 158h7v15h-7zM622 158h7v15h-7zM630 158h7v15h-7zM638 158h7v15h-7zM646 158h7v15h-7z"/><path class="f-acc" d="M590 158h7v15h-7z"/><text x="662" y="170" fill="currentColor">데이터 7</text>
  <text x="6" y="188" fill="currentColor">p (c7)</text><rect x="78" y="176" width="575" height="15" fill="none" stroke="currentColor" stroke-width="0.4"/><path class="f-ok" d="M102 176h7v15h-7zM118 176h7v15h-7zM126 176h7v15h-7zM134 176h7v15h-7zM150 176h7v15h-7zM158 176h7v15h-7zM166 176h7v15h-7zM174 176h7v15h-7zM182 176h7v15h-7zM190 176h7v15h-7zM198 176h7v15h-7zM214 176h7v15h-7zM222 176h7v15h-7zM230 176h7v15h-7zM238 176h7v15h-7zM246 176h7v15h-7zM254 176h7v15h-7zM262 176h7v15h-7zM270 176h7v15h-7zM278 176h7v15h-7zM286 176h7v15h-7zM294 176h7v15h-7zM302 176h7v15h-7zM310 176h7v15h-7zM318 176h7v15h-7zM326 176h7v15h-7zM342 176h7v15h-7zM350 176h7v15h-7zM358 176h7v15h-7zM366 176h7v15h-7zM374 176h7v15h-7zM382 176h7v15h-7zM390 176h7v15h-7zM398 176h7v15h-7zM406 176h7v15h-7zM414 176h7v15h-7zM422 176h7v15h-7zM430 176h7v15h-7zM438 176h7v15h-7zM446 176h7v15h-7zM454 176h7v15h-7zM462 176h7v15h-7zM470 176h7v15h-7zM478 176h7v15h-7zM486 176h7v15h-7zM494 176h7v15h-7zM502 176h7v15h-7zM510 176h7v15h-7zM518 176h7v15h-7zM526 176h7v15h-7zM534 176h7v15h-7zM542 176h7v15h-7zM550 176h7v15h-7zM558 176h7v15h-7zM566 176h7v15h-7zM574 176h7v15h-7zM582 176h7v15h-7zM598 176h7v15h-7zM606 176h7v15h-7zM614 176h7v15h-7zM622 176h7v15h-7zM630 176h7v15h-7zM638 176h7v15h-7zM646 176h7v15h-7z"/><path class="f-acc" d="M78 176h7v15h-7zM86 176h7v15h-7zM94 176h7v15h-7zM110 176h7v15h-7zM142 176h7v15h-7zM206 176h7v15h-7zM334 176h7v15h-7zM590 176h7v15h-7z"/><text x="662" y="188" fill="currentColor">데이터 64</text>
  <rect x="78" y="204" width="10" height="10" class="f-ok"/><text x="94" y="214" fill="currentColor">데이터 비트</text><rect x="188" y="204" width="10" height="10" class="f-acc"/><text x="204" y="214" fill="currentColor">체크 비트</text>
  <text x="10" y="238" fill="currentColor">열 p 의 위 7칸 = p 의 이진수 → 단일 오류의 s = 그 열 = 오류 위치. 맨 아래 p 행은 72열 전부 1.</text>
  <text x="10" y="260" fill="currentColor">두 열의 XOR: 위 7칸 ≠ 0 (열이 모두 다름), p 칸 = 1⊕1 = 0 → "p=0, s≠0" = 2비트 오류 → 최소 거리 4.</text>
  <text x="10" y="282" fill="currentColor">c7 을 데이터만으로 풀어 쓰면 35개 데이터 XOR (practice 연습 5). 가장 넓은 식이 XOR 트리 깊이를 정한다.</text>
</svg>
```

**체크 c_j 의 식.** 정상 코드워드의 조건은 "1 인 비트들의 위치 XOR = 0" 이다. c_j 는 위치 2^j 에 있고 그 위치는 비트 j 만 1 이므로, 위치 XOR 의 비트 j 를 0 으로 만드는 값은 c_j = ⊕{d_i : POS[i] 의 비트 j = 1} = `parity64(d & CMASK[j])`. 실제 마스크 (solution 으로 출력):

| j | CMASK[j] | 데이터 입력 수 |
|---|---|---|
| 0 | 0xAB55555556AAAD5B | 35 |
| 1 | 0xCD9999999B33366D | 35 |
| 2 | 0xF1E1E1E1E3C3C78E | 35 |
| 3 | 0x01FE01FE03FC07F0 | 31 |
| 4 | 0x01FFFE0003FFF800 | 31 |
| 5 | 0x01FFFFFFFC000000 | 31 |
| 6 | 0xFE00000000000000 | 7 |

c7 = (데이터 64 + c0..c6) 의 패리티. 데이터만으로 풀어 쓰면 c7 = ⊕{d_i : popcount(POS[i]) 가 짝수} — 35개 입력이다 (연습 5 에서 검산).

**작은 추적.** d = 0x0B (d0 = d1 = d3 = 1, 위치 3, 5, 7):

| 단계 | 계산 | 결과 |
|---|---|---|
| 위치 XOR | 3 ⊕ 5 ⊕ 7 = 011 ⊕ 101 ⊕ 111 | 001 → c0 = 1, 나머지 0 |
| c7 | parity(d) ⊕ parity(c0..c6) = 1 ⊕ 1 | 0 |
| 체크 바이트 | c7..c0 | **0x01** (코드 출력과 일치) |
| d1(위치 5) 뒤집힘 | s = 5, p = 1 | syn 0x85 → SYN_TAB = 데이터 1 → 정정 |
| d1, d3 뒤집힘 | s = 5 ⊕ 7 = 2, p = 0 | syn 0x02 → **UE** |

마지막 줄이 핵심이다. p 를 안 보면 s = 2 는 "c1 이 틀렸다" 로 읽혀 오정정된다. 8번째 비트가 이걸 막는다.

### Part A — 신드롬 대수와 최소 거리 4

H 를 8×72 행렬이라 하자: 열 q (위치 q) 의 위 7행은 q 의 이진수, 맨 아래 행은 1. 정상 코드워드 c 는 H·c = 0 이다 (위 7행 = 위치 XOR = 0, 아래 행 = 전체 패리티 = 0). 저장값이 c ⊕ e 이면

  H·(c ⊕ e) = H·c ⊕ H·e = H·e.

신드롬은 **데이터와 무관** 하다. 오류 비트 집합 E 에 대해 s = ⊕_{q∈E} q, p = |E| mod 2.

| 오류 개수 | p | s | 판정 |
|---|---|---|---|
| 0 | 0 | 0 | 정상 |
| 1 (위치 q) | 1 | q (0..71) | 위치 q 정정 — q = 0 이면 c7 |
| 2 (q ≠ r) | 0 | q ⊕ r ≠ 0 | UE — 열이 서로 달라 0 이 될 수 없다 |
| 3 | 1 | 아무 값 | s ≤ 71 이면 **오정정**, 72..127 이면 UE |
| 4 | 0 | 0 일 수 있음 | s = 0 이면 **무성 오염**, 아니면 UE |

**최소 거리(HD) = 4 증명.** 0 이 아닌 코드워드의 최소 무게를 보면 된다. 무게 1: 열이 0 이 아님 (아래 행이 1). 무게 2: 두 열이 서로 다름. 무게 3: 아래 행 합 = 1 ≠ 0. 그러므로 무게 ≤ 3 인 코드워드는 없다. 무게 4 는 있다: {c7, c0, c1, d0} = 위치 {0, 1, 2, 3}, 1 ⊕ 2 ⊕ 3 = 0, 4개 → p = 0 (코드로 확인: 디코드가 NO_ERR, syn 0x00). HD = 4 이면 반경 1 공 둘이 겹치지 않으면서(정정 1) 거리 2 오류가 다른 코드워드의 반경 1 공에 들어가지 않는다(검출 2): 1 + 2 < 4.

**SYN_TAB.** 256 엔트리 × 1 B = 256 B. 여섯 갈래로 정확히 나뉜다:

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b07a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-size="14" font-weight="bold">디코더: 신드롬 바이트 하나 → 표 하나 → 여섯 갈래 (256 = 1+1+7+64+127+56)</text>
  <rect x="10" y="40" width="200" height="56" class="f-mut" stroke="currentColor"/>
  <text x="110" y="62" text-anchor="middle" fill="currentColor">syn = s | p&lt;&lt;7</text>
  <text x="110" y="82" text-anchor="middle" fill="currentColor" font-size="12">s = 위치 XOR, p = 전체 패리티</text>
  <line x1="210" y1="68" x2="248" y2="68" stroke="currentColor" marker-end="url(#b07a)"/>
  <rect x="250" y="40" width="150" height="56" class="f-acc" stroke="currentColor"/>
  <text x="325" y="62" text-anchor="middle" fill="currentColor">SYN_TAB[syn]</text>
  <text x="325" y="82" text-anchor="middle" fill="currentColor" font-size="12">256 엔트리 × 1 B</text>
  <path d="M325 96 V131 H438" fill="none" stroke="currentColor" marker-end="url(#b07a)"/>
  <text x="200" y="136" fill="currentColor" font-size="12" text-anchor="end" font-family="monospace">p=0, s=0</text><line x1="205" y1="131" x2="320" y2="131" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="440" y="118" width="250" height="26" class="f-ok" stroke="currentColor"/><text x="448" y="136" fill="currentColor" font-size="12">NO_ERR — 그대로 돌려줌</text><text x="700" y="136" fill="currentColor" font-size="12">1</text>
  <path d="M325 131 V165 H438" fill="none" stroke="currentColor" marker-end="url(#b07a)"/>
  <text x="200" y="170" fill="currentColor" font-size="12" text-anchor="end" font-family="monospace">p=1, s=0</text><line x1="205" y1="165" x2="320" y2="165" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="440" y="152" width="250" height="26" class="f-ok" stroke="currentColor"/><text x="448" y="170" fill="currentColor" font-size="12">CE 체크 c7 — 데이터 멀쩡</text><text x="700" y="170" fill="currentColor" font-size="12">1</text>
  <path d="M325 165 V199 H438" fill="none" stroke="currentColor" marker-end="url(#b07a)"/>
  <text x="200" y="204" fill="currentColor" font-size="12" text-anchor="end" font-family="monospace">p=1, s=2^j</text><line x1="205" y1="199" x2="320" y2="199" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="440" y="186" width="250" height="26" class="f-ok" stroke="currentColor"/><text x="448" y="204" fill="currentColor" font-size="12">CE 체크 c_j (j=0..6)</text><text x="700" y="204" fill="currentColor" font-size="12">7</text>
  <path d="M325 199 V233 H438" fill="none" stroke="currentColor" marker-end="url(#b07a)"/>
  <text x="200" y="238" fill="currentColor" font-size="12" text-anchor="end" font-family="monospace">p=1, s=데이터 위치</text><line x1="205" y1="233" x2="320" y2="233" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="440" y="220" width="250" height="26" class="f-ok" stroke="currentColor"/><text x="448" y="238" fill="currentColor" font-size="12">CE 데이터 i — d ^= 1&lt;&lt;i</text><text x="700" y="238" fill="currentColor" font-size="12">64</text>
  <path d="M325 233 V267 H438" fill="none" stroke="currentColor" marker-end="url(#b07a)"/>
  <text x="200" y="272" fill="currentColor" font-size="12" text-anchor="end" font-family="monospace">p=0, s≠0</text><line x1="205" y1="267" x2="320" y2="267" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="440" y="254" width="250" height="26" class="f-warn" stroke="currentColor"/><text x="448" y="272" fill="currentColor" font-size="12">UE — 짝수 개(≥2) 오류</text><text x="700" y="272" fill="currentColor" font-size="12">127</text>
  <path d="M325 267 V301 H438" fill="none" stroke="currentColor" marker-end="url(#b07a)"/>
  <text x="200" y="306" fill="currentColor" font-size="12" text-anchor="end" font-family="monospace">p=1, s=72..127</text><line x1="205" y1="301" x2="320" y2="301" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="440" y="288" width="250" height="26" class="f-warn" stroke="currentColor"/><text x="448" y="306" fill="currentColor" font-size="12">UE — 없는 위치 (홀수 개 ≥3)</text><text x="700" y="306" fill="currentColor" font-size="12">56</text>
  <text x="10" y="322" fill="currentColor" font-size="12">예: syn 0xAC = 1 0101100b → p=1, s=44 → 위치 44 = d37 → 데이터 비트 37 정정. syn 0x06 → p=0, s=6 → UE.</text>
</svg>
```

표 없이 계산할 수도 있다: s 가 2의 거듭제곱이 아니면 데이터 번호 = s − 2 − ⌊log2 s⌋ (위치 1..s−1 중 2의 거듭제곱 ⌊log2 s⌋ + 1 개를 뺀 것; s = 44 → 44 − 2 − 5 = 37). 소프트웨어에선 256 B 표가 더 단순하고, 하드웨어는 열마다 8비트 비교기를 둔다.

### Part B — 실측: 단일 · 2비트 · 3비트

`solution` 실제 출력 (Part B 부분):

```text
single, exhaustive     n=4608     corrected=4608   ue=0        mis_data=0      mis_check=0     silent=0
double, exhaustive     n=2556     corrected=0      ue=2556     mis_data=0      mis_check=0     silent=0
double, random 1M      n=1000000  corrected=0      ue=1000000  mis_data=0      mis_check=0     silent=0
triple, exhaustive     n=59640    corrected=0      ue=14336    mis_data=40320  mis_check=4984  silent=0
triple, random 100k    n=100000   corrected=0      ue=24061    mis_data=67618  mis_check=8321  silent=0
```

- **2비트 100만 회에 오정정 0** 은 확률이 아니라 위의 증명의 결과다. 그래서 2,556 쌍 전수가 사실 "완전한" 테스트이고, 100만 회 무작위는 인코더/디코더 구현 버그(예: 데이터 의존 경로)를 잡는 용도다.
- **3비트 전수 59,640 개 중 14,336 (24.0 %) 만 UE**, 40,320 (67.6 %) 은 데이터 비트 하나를 더 뒤집고 "정정했다" 고 보고(데이터 4비트 오염), 4,984 (8.4 %) 는 체크 비트 오류로 보고(데이터 3비트 오염 그대로). p = 1 이라 디코더 눈에는 단일 오류와 구별이 안 되고, s 가 존재하지 않는 위치(72..127)일 때만 UE 가 된다. SIV-M14 연습 1 의 숫자와 같다.
- 데이터 0 과 0xDEADBEEFCAFEF00D 로 3비트 전수 결과가 **비트 단위로 같다** (`memcmp` 테스트) — 선형성의 직접 증거.

더 큰 k 는 `demo_multibit.c` 로 돌렸다 (k ≤ 4 전수, k ≥ 5 무작위 100만, 실행 0.30 s, 측정 시 load avg 31.08 — 시간 측정이 아니라 결과는 부하와 무관):

```text
k  mode           trials    corrected          UE                 miscorrected       silent(SDC)
1  exhaustive         72        72 (100.0%)         0 (  0.0%)         0 (  0.0%)         0 (  0.0%)
2  exhaustive       2556         0 (  0.0%)      2556 (100.0%)         0 (  0.0%)         0 (  0.0%)
3  exhaustive      59640         0 (  0.0%)     14336 ( 24.0%)     45304 ( 76.0%)         0 (  0.0%)
4  exhaustive    1028790         0 (  0.0%)   1017464 ( 98.9%)         0 (  0.0%)     11326 (  1.1%)
5  random 1M     1000000         0 (  0.0%)    323883 ( 32.4%)    676117 ( 67.6%)         0 (  0.0%)
6  random 1M     1000000         0 (  0.0%)    990767 ( 99.1%)         0 (  0.0%)      9233 (  0.9%)
7  random 1M     1000000         0 (  0.0%)    378009 ( 37.8%)    621991 ( 62.2%)         0 (  0.0%)
8  random 1M     1000000         0 (  0.0%)    991528 ( 99.2%)         0 (  0.0%)      8472 (  0.8%)
```

```svg
<svg viewBox="0 0 760 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="14" font-weight="bold">k비트 오류의 판정 비율 — demo_multibit.c 실측 (k≤4 전수, k≥5 무작위 100만)</text>
  <text x="10" y="57" fill="currentColor">k=1 (전수)</text>
  <rect x="100.0" y="40" width="430.0" height="24" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <text x="538" y="57" fill="currentColor">정정 100%</text>
  <text x="10" y="91" fill="currentColor">k=2 (전수)</text>
  <rect x="100.0" y="74" width="430.0" height="24" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="538" y="91" fill="currentColor">UE 100.0%</text>
  <text x="10" y="125" fill="currentColor">k=3 (전수)</text>
  <rect x="100.0" y="108" width="103.2" height="24" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="203.2" y="108" width="326.8" height="24" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <text x="538" y="125" fill="currentColor">UE 24.0% · 오정정 76.0%</text>
  <text x="10" y="159" fill="currentColor">k=4 (전수)</text>
  <rect x="100.0" y="142" width="425.3" height="24" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="525.3" y="142" width="4.7" height="24" fill="currentColor"/>
  <text x="538" y="159" fill="currentColor">UE 98.9% · 무성 1.1%</text>
  <text x="10" y="193" fill="currentColor">k=5 (1M)</text>
  <rect x="100.0" y="176" width="139.3" height="24" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="239.3" y="176" width="290.7" height="24" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <text x="538" y="193" fill="currentColor">UE 32.4% · 오정정 67.6%</text>
  <text x="10" y="227" fill="currentColor">k=6 (1M)</text>
  <rect x="100.0" y="210" width="426.1" height="24" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="526.1" y="210" width="3.9" height="24" fill="currentColor"/>
  <text x="538" y="227" fill="currentColor">UE 99.1% · 무성 0.9%</text>
  <text x="10" y="261" fill="currentColor">k=7 (1M)</text>
  <rect x="100.0" y="244" width="162.5" height="24" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="262.5" y="244" width="267.5" height="24" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <text x="538" y="261" fill="currentColor">UE 37.8% · 오정정 62.2%</text>
  <text x="10" y="295" fill="currentColor">k=8 (1M)</text>
  <rect x="100.0" y="278" width="426.6" height="24" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="526.6" y="278" width="3.4" height="24" fill="currentColor"/>
  <text x="538" y="295" fill="currentColor">UE 99.2% · 무성 0.8%</text>
  <rect x="10" y="320" width="12" height="12" class="f-ok"/><text x="28" y="331" fill="currentColor">정정 (CE, 원본 복구)</text><rect x="170" y="320" width="12" height="12" class="f-acc"/><text x="188" y="331" fill="currentColor">UE 검출</text><rect x="270" y="320" width="12" height="12" class="f-warn"/><text x="288" y="331" fill="currentColor">오정정 (CE 라고 하고 틀림)</text><rect x="470" y="320" width="12" height="12" fill="currentColor"/><text x="488" y="331" fill="currentColor">무성 오염 (NO_ERR)</text>
  <text x="10" y="354" fill="currentColor">짝수 k: p=0 → 오정정은 없고 s=0 일 때만 무성 (≈1/128). 홀수 k≥3: p=1 → s 가 0..71 이면 오정정, 72..127 이면 UE.</text>
</svg>
```

읽는 법: **짝수 k 는 오정정이 없다** (p = 0 → 디코더는 절대 정정하지 않는다). 대신 s = 0 이면 무성 오염: 무게 4 코드워드가 정확히 A_4 = 11,326 개라 4비트 오류의 1.1 %, 큰 짝수 k 에선 s 가 대략 균등해 ≈ 1/128 = 0.78 % 로 수렴한다. **홀수 k ≥ 3 은 대부분 오정정** 이다. s 가 128개 값에 균등하다면 UE 비율은 56/128 = 43.75 % 일 텐데 실측 24–38 % 로 더 낮다 — 위치형 레이아웃에선 72..127 이 덜 나오기 때문이다 (Hsiao 는 43.7 %, 연습 5).

### Part C — 컨트롤러 읽기 경로

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b07b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-size="14" font-weight="bold">Part C — 컨트롤러 읽기 경로와 scrub-on-CE (mc_t / ecc_read)</text>
  <rect x="10" y="50" width="150" height="70" class="f-mut" stroke="currentColor"/>
  <text x="85" y="72" text-anchor="middle" fill="currentColor">DRAM 배열</text>
  <text x="85" y="92" text-anchor="middle" fill="currentColor" font-size="12">data[a] 64b (DQ)</text>
  <text x="85" y="110" text-anchor="middle" fill="currentColor" font-size="12">chk[a] 8b (CB)</text>
  <line x1="160" y1="85" x2="208" y2="85" stroke="currentColor" marker-end="url(#b07b)"/><text x="166" y="78" fill="currentColor" font-size="12">72b</text>
  <rect x="210" y="50" width="150" height="70" class="f-acc" stroke="currentColor"/>
  <text x="285" y="72" text-anchor="middle" fill="currentColor">secded_decode</text>
  <text x="285" y="92" text-anchor="middle" fill="currentColor" font-size="12">syn → SYN_TAB</text>
  <text x="285" y="110" text-anchor="middle" fill="currentColor" font-size="12">CE: 1비트 XOR 정정</text>
  <line x1="360" y1="85" x2="458" y2="85" stroke="currentColor" marker-end="url(#b07b)"/><text x="368" y="78" fill="currentColor" font-size="12">*out (정정된 64b)</text>
  <rect x="460" y="50" width="120" height="70" class="f-mut" stroke="currentColor"/>
  <text x="520" y="80" text-anchor="middle" fill="currentColor">CPU / 캐시</text>
  <text x="520" y="100" text-anchor="middle" fill="currentColor" font-size="12">+ res.st</text>
  <line x1="285" y1="120" x2="285" y2="158" stroke="currentColor" marker-end="url(#b07b)"/>
  <rect x="200" y="160" width="230" height="66" class="f-ok" stroke="currentColor"/>
  <text x="210" y="180" fill="currentColor" font-size="12">CE: ce_cnt++, last_ce_addr, last_ce_syn</text>
  <text x="210" y="198" fill="currentColor" font-size="12">UE: ue_cnt++, last_ue_addr/syn</text>
  <text x="210" y="216" fill="currentColor" font-size="12">UE 데이터 = 원시값 (poison 은 호출자)</text>
  <path d="M210 120 V140 H85 V122" fill="none" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#b07b)"/>
  <text x="20" y="160" fill="currentColor" font-size="12">scrub_on_ce: 고친 72b 를 다시 씀</text>
  <text x="600" y="62" fill="currentColor" font-size="12">정책 (보고·임계·</text><text x="600" y="78" fill="currentColor" font-size="12">page retire) 은</text><text x="600" y="94" fill="currentColor" font-size="12">SIV-M14 / MEM-A06</text>
  <text x="10" y="256" fill="currentColor" font-weight="bold">같은 워드에 셀 두 개가 시간차로 뒤집힐 때 (solution.c 테스트 그대로)</text>
  <text x="10" y="280" fill="currentColor" font-size="12" font-family="monospace">scrub 없음: flip d37 → read CE (cnt 1) → read CE (cnt 2, 아직 깨진 채) → flip d5 → read UE</text>
  <text x="10" y="302" fill="currentColor" font-size="12" font-family="monospace">scrub 있음: flip d37 → read CE (cnt 1, 고쳐 씀) → read OK → flip d5 → read CE (다시 1비트)</text>
  <text x="10" y="324" fill="currentColor" font-size="12">→ 1비트 오류가 쌓여 2비트가 되기 전에 고쳐 쓰는 것이 scrub 의 목적. 읽지 않는 영역은 patrol scrub (MEM-A06).</text>
</svg>
```

규칙 세 가지만 지키면 된다.

1. **CE 는 정정된 데이터를 돌려주고 센다.** 카운터와 "마지막 주소·신드롬"은 EDAC 같은 소프트웨어가 폴링하거나 인터럽트로 읽는 값이다 (Linux EDAC 의 `/sys/devices/system/edac/mc/mc*/ce_count` 같은 것 — 경로·필드는 커널 버전과 드라이버에 따라 다르다).
2. **UE 는 원시 데이터를 돌려주고 배열을 건드리지 않는다.** 고치려는 시도 자체가 데이터를 더 망가뜨릴 수 있다. 실제 시스템은 여기서 데이터에 poison 표시를 하거나 버스 에러(SLVERR / SError / machine check)를 낸다. 이 레슨의 API 는 상태만 돌려주고 처리는 호출자 몫으로 둔다 (SIV-M14 가 버스 응답·IRQ 까지 검증).
3. **scrub-on-CE.** 읽기에서 CE 를 고쳤으면 고친 72비트를 다시 쓴다. 안 쓰면 같은 결함을 읽을 때마다 다시 세고(테스트: ce_cnt 1 → 2), 같은 워드의 두 번째 셀이 뒤집히는 순간 UE 가 된다. 다시 쓰면 다음 1비트도 CE 로 끝난다. 단, 다시 써도 고쳐지지 않는 **하드 결함**(stuck cell)은 매번 CE 가 나므로, CE 를 주소별로 모아 같은 곳이 반복되면 page retire 하는 정책이 필요하다 — MEM-A06.

`ecc_read` 실행 순서 추적 (scrub 없음, 주소 10, want = 0x0123456789ABCDEF):

| 동작 | 배열 data[10] | 신드롬 | 상태 | ce / ue |
|---|---|---|---|---|
| ecc_write | want, chk 0x9C | — | — | 0 / 0 |
| mc_inject bit 37 | want ^ 1<<37 | — | — | 0 / 0 |
| ecc_read | 그대로 (scrub 없음) | 0xAC | CE_DATA 37, out = want | 1 / 0 |
| ecc_read | 그대로 | 0xAC | CE_DATA 37 | 2 / 0 |
| mc_inject bit 5 | want ^ bit37 ^ bit5 | — | — | 2 / 0 |
| ecc_read | 그대로 | 0x26 (s = 44 ⊕ 10 = 38, p = 0) | **UE**, out = 원시값 | 2 / 1 |

### 복잡도와 비용

| 항목 | 소프트웨어 (이 코드) | 하드웨어 |
|---|---|---|
| 인코드 | AND + parity64 × 8 | XOR 트리 8개, 최대 35입력 → 깊이 ⌈log2 35⌉ = 6 단 (2입력 XOR 기준) |
| 디코드 | 인코드 + XOR + 256 B 표 + 1비트 XOR | 같은 트리 + 72개 8비트 비교기 + 정정 XOR |
| 메모리 | POS 64 B + CMASK 56 B + SYN_TAB 256 B | 없음 (배선) |
| 저장 오버헤드 | 8/64 = 12.5 % | x8 칩 하나 더 (9/8) |

solution 전체(100만 + 59,640 + … 디코드)는 0.17 s, ASan/UBSan 빌드 0.34 s 에 끝난다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 36/36 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_multibit.c -o demo_multibit && ./demo_multibit   # 약 0.3 초
```

실제 출력 (`solution`):

```text
SYN_TAB: 256 bytes, CE entries=72, UE entries=183, NO_ERR entries=1
encode vs ref_encode: 100000 random words, mismatches=0, nonzero clean syndromes=0
single, exhaustive     n=4608     corrected=4608   ue=0        mis_data=0      mis_check=0     silent=0
double, exhaustive     n=2556     corrected=0      ue=2556     mis_data=0      mis_check=0     silent=0
double, random 1M      n=1000000  corrected=0      ue=1000000  mis_data=0      mis_check=0     silent=0
triple, exhaustive     n=59640    corrected=0      ue=14336    mis_data=40320  mis_check=4984  silent=0
triple, random 100k    n=100000   corrected=0      ue=24061    mis_data=67618  mis_check=8321  silent=0
36/36 passed
```

## 7. 자주 하는 실수

**1. 위치 번호와 데이터 비트 번호를 섞는다.** 위치 37 은 d37 이 아니라 d30 이다 (위치 44 가 d37). EDAC 로그의 신드롬 0xAC 를 "비트 44" 로 보고하면 엉뚱한 DQ/칩을 의심하게 된다. 변환은 항상 표로 한다.

```c
SYN_TAB[0x80 | POS[k]] = (uint8_t)k;      /* 위치 → 데이터 번호. s 를 그대로 비트 번호로 쓰지 않는다 */
```

**2. p 를 데이터만으로 계산한다.** `p = parity64(d)` 로 하면 체크 비트 하나가 뒤집혔을 때 p = 0, s = 2^j → UE 로 보고된다 (정정 가능한 걸 UE 로 → 불필요한 서버 다운). p 는 72비트 전부다.

```c
unsigned p = parity64(d) ^ parity64(c);   /* c 8비트 전부, c7 포함 */
```

**3. p = 1, s = 0 을 "정상" 으로 처리한다.** c7 자신이 뒤집힌 경우다. 데이터는 멀쩡하니 무해해 보이지만 CE 를 세지 않고 고쳐 쓰지도 않으면, 그 워드에 1비트가 더 나는 순간 UE 다. SYN_TAB[0x80] = c7.

**4. SYN_TAB 기본값을 0 으로 둔다.** 표의 값 0 은 "데이터 비트 0" 이다. memset 을 안 하면 등록 안 된 신드롬(모든 2비트 오류!)이 d0 을 뒤집고 CE 로 보고된다. starter 출력의 "CE entries=256" 이 바로 이 상태다. 기본값은 반드시 UE.

**5. 무작위 주입에서 같은 비트를 두 번 뒤집는다.** 오류가 사라져 NO_ERR 이 나오고, 캠페인은 이걸 "2비트인데 못 잡음" 으로 셀 수 있다. `pick_distinct` 로 중복 없이 뽑는다.

**6. `1 << k` (int) 로 64비트 마스크를 만든다.** k ≥ 31 이면 UB / 잘못된 값. 데이터는 `1ull << k`, 체크는 `(uint8_t)(1u << j)`.

**7. UE 에서 "가장 그럴듯한" 비트를 고친다.** 2비트 오류에서 s 를 위치로 읽고 고치면 3비트 오염이 된다 — SEC-only 코드가 하는 일이다 (연습 1: 82 % 오정정). UE 에서는 데이터를 건드리지 않는다.

## 8. 면접 Q&A

<details><summary>Q1. SECDED 는 왜 Hamming 체크 7비트에 1비트를 더 붙이나요?</summary>

Hamming(71,64) 만으로는 최소 거리 3 이라 2비트 오류의 신드롬이 어떤 단일 오류의 신드롬과 같아질 수 있다. 실측(연습 1): 2비트 2,485 쌍 중 2,037 (82.0 %) 이 존재하는 위치를 가리켜 **세 번째 비트를 뒤집는 오정정**, 448 개만 "없는 위치(72..127)" 라 검출된다. 전체 패리티 1비트를 더하면 열마다 아래 행이 1 이 되어 홀수/짝수 개 오류가 구별되고 거리가 4 가 된다: p = 1 → 1개(정정), p = 0 & s ≠ 0 → 2개(검출). 꼬리: "3비트는?" → p = 1 이라 단일과 구별 불가, 76 % 오정정.

</details>

<details><summary>Q2. 왜 72/64 인가요? 128비트에 9비트면 더 싸지 않나요?</summary>

SEC 에 필요한 r 은 2^r ≥ k + r + 1 을 만족하는 최소값: k = 64 → 7, SECDED 는 +1 = 8 → (72,64), 12.5 %. k = 128 이면 (137,128) 로 7.0 % 다 (연습 2). 64 를 고른 건 버스 폭 때문이다: DDR4 DIMM 은 64비트 데이터 + 8비트 체크 = x8 칩 9개 (x4 면 18개) 로 체크 바이트가 칩 하나에 딱 맞는다. 단어를 키우면 (1) 버스트 하나에 다 안 들어와 디코드가 늦어지고, (2) 작은 쓰기마다 더 큰 읽기-수정-쓰기가 필요하고, (3) 같은 코드워드에 비트가 많아져 다중 오류 확률이 커진다. DDR5 ECC DIMM 은 32비트 서브채널마다 체크 8비트(40비트 × 2) 구성이 흔하다고 알려져 있다 (대표값 — JESD79-5 / 모듈 데이터시트 확인 권장).

</details>

<details><summary>Q3. 3비트 이상 오류가 나면 SECDED 는 어떻게 되나요?</summary>

보장이 없다. 실측: 3비트 전수 59,640 중 UE 24.0 %, 오정정 76.0 % (그중 67.6 % 는 데이터를 하나 더 망가뜨리고 "정정했다" 고 보고). 4비트는 1.1 % 가 무성 오염. 그래서 (1) scrub 으로 1비트가 쌓이기 전에 고치고, (2) 칩 하나가 통째로 죽는 고장(x4 칩 = 같은 니블 4비트)은 SECDED 가 아니라 심볼 단위 코드(chipkill / SDDC, Reed-Solomon 계열, MEM-A05)로 막는다. 연습 4: x4 칩 고장 270 패턴 중 3비트 패턴 72개 중 69개가 오정정, 니블 전체 4개 중 4패턴은 무성 오염.

</details>

<details><summary>Q4. 실제 DRAM 컨트롤러는 Hamming 이 아니라 Hsiao 코드를 쓴다는데, 차이가 뭔가요?</summary>

Hsiao(1970)는 H 의 모든 열을 **홀수 무게** 로 고른다: 체크 8열 = 단위 열, 데이터 64열 = 무게 3 열 56개 + 무게 5 열 8개. 별도의 전체 패리티 행이 없어도 단일 오류 = 홀수 무게 신드롬(열과 일치), 2비트 = 짝수 무게 ≠ 0 이라 SECDED 가 된다. 장점은 **행 무게 균형**: 체크 하나당 데이터 입력이 전부 26 (위치형은 35, 35, 35, 31, 31, 31, 7, 그리고 c7 이 35) → XOR 트리가 얕고 고르게 되어 타이밍이 좋다. 덤으로 3비트 UE 비율이 43.7 % (위치형 24.0 %) — 연습 5 실측. 판정 규칙과 신드롬 값은 다르지만 "신드롬 = H·e" 라는 검증 방법은 같다.

</details>

<details><summary>Q5. ECC 메모리에 1바이트를 쓰면 무슨 일이 일어나나요?</summary>

체크 바이트는 64비트 전체의 함수이므로 1바이트만 바꾸려면 컨트롤러가 **읽기-수정-쓰기(RMW)** 를 해야 한다: 72비트를 읽고 → 디코드(CE 면 정정) → 바이트를 합치고 → 다시 인코드 → 72비트 쓰기. 쓰기 하나가 읽기 + 쓰기가 되고 버스 방향 전환(tWTR 등)까지 붙어 비싸다. 그래서 캐시가 있는 시스템은 라인 단위(64 B = 코드워드 8개)로 쓰게 해서 RMW 를 피하고, 컨트롤러는 부분 쓰기를 모아 합친다. DRAM 의 byte mask(DM)는 ECC 와 함께 쓰기 어렵다 — 체크 바이트를 부분 갱신할 수 없으니까. 펌웨어 쪽 함정: ECC 를 켜기 전에 메모리 전체를 한 번 써서 체크 비트를 초기화해야 한다 (안 하면 첫 부분 쓰기의 RMW 읽기에서 쓰레기 체크 비트 → 가짜 UE).

</details>

<details><summary>Q6. 신드롬이 데이터와 무관하다는 성질을 실무에서 어디에 쓰나요?</summary>

(1) **검증**: 주입 마스크만 보고 기대 신드롬·판정을 계산한다 — 데이터 패턴마다 기대값을 다시 계산할 필요가 없다 (SIV-M14 의 128 케이스 매트릭스). (2) **로그 해석**: EDAC/BMC 로그의 신드롬 하나로 어느 데이터 비트 → 어느 DQ → 어느 칩인지 바로 안다 (연습 3: 0xAC → d37 → x8 칩 4 / x4 칩 9). 같은 칩이 반복되면 DIMM 교체 근거가 된다. (3) **테스트 축소**: 2비트 2,556 쌍 전수가 데이터 하나로 완전하다. 단, 실제 DQ ↔ 칩 배선과 컨트롤러의 H 는 벤더마다 달라서 그 표를 데이터시트에서 가져와야 한다.

</details>

<details><summary>Q7. Don 씨는 SSD 에서 NAND ECC 를 다뤘죠. DRAM ECC 와 무엇이 다른가요?</summary>

오류율과 지연 예산이 다르다. DRAM 은 원시 오류가 드물고(주로 입자 충돌 같은 soft error 와 일부 hard fault) 읽기 경로가 수 ns 라, 64비트마다 1비트 정정 SECDED 를 한 사이클 남짓에 끝낸다. NAND 는 수명 말기·리텐션 후 원시 BER 이 훨씬 높아서 1–2 KB 코드워드에 수십 비트 이상 정정하는 BCH 나 LDPC(soft decoding, read retry 와 함께)를 쓰고, 디코드에 µs 단위를 쓴다. 공통점은 "CE 수를 건강 지표로 쓴다" 는 것 — SSD 의 블록 retire 와 DRAM 의 page retire 는 같은 생각이다 (MEM-M13, MEM-A06). (수치 범위는 대표값 — 부품·세대별 데이터시트 확인 권장.)

</details>

<details><summary>Q8. 새 보드 bring-up 에서 ECC 경로를 어떻게 확인하겠습니까?</summary>

(1) ECC 끈 상태에서 메모리 테스트(MEM-B08 데이터/주소 버스, MEM-B09 March)로 배선부터 확인 — ECC 가 켜져 있으면 고장 난 DQ 하나를 CE 가 가려 버린다. (2) ECC 켜고 전체 영역 초기화 쓰기. (3) 컨트롤러의 error-inject 기능으로 데이터 1비트(72 위치 전부) → CE + 올바른 신드롬, 2비트 → UE + 인터럽트/버스 에러 확인. (4) CE/UE 카운터·주소 레지스터가 OS(EDAC)까지 올라오는지. (5) 스트레스 + 온도에서 CE 발생률을 기록. (3)~(4) 의 상세 매트릭스가 SIV-M14 다.

</details>

## 9. 추가 연습

### 연습 1 — 8번째 비트를 빼면 (SEC-only)

c7 없이 위치 1..71 만 쓰는 Hamming(71,64) 에서, 신드롬 s ≠ 0 이면 위치 s 를 고치고 s > 71 이면 검출한다고 하자. 2비트 오류 C(71,2) 쌍 중 몇 개가 검출되고 몇 개가 오정정되는가? 닫힌 식으로도 구하라.

<details><summary>답 보기</summary>

실측 `pairs=2485 detected=448 miscorrected=2037 (82.0%)`. 닫힌 식: a ⊕ b ≥ 72 이려면 비트 6 이 1 이어야 하므로 정확히 하나(a)가 64..71, 다른 하나(b)가 1..63. 그때 a ⊕ b = 64 + ((a − 64) ⊕ b) 이고 a − 64 < 8 이라 (a − 64) ⊕ b ≥ 8 ⇔ b ≥ 8. 그래서 8 × 56 = 448 개만 검출. 나머지 82 % 는 세 번째 비트를 뒤집는다 — 전체 패리티 1비트의 값.

</details>

### 연습 2 — 데이터 폭별 최소 체크 비트

`secded_bits(k)` = (2^r ≥ k + r + 1 인 최소 r) + 1 을 짜고 k = 8 … 512 의 (n, k) 와 오버헤드를 표로 만들어라.

<details><summary>답 보기</summary>

실측: (13,8) 62.5 %, (22,16) 37.5 %, (39,32) 21.9 %, **(72,64) 12.5 %**, (137,128) 7.0 %, (266,256) 3.9 %, (523,512) 2.1 %. 64 → 7 이 되는 경계: k = 57 까지는 r = 6 (완전 Hamming (63,57)), 58 부터 7. 그러니 64비트에는 7비트 Hamming 중 1비트 몫(위치 72..127)이 남는다 — 이 "남는 신드롬" 이 3비트 오류 일부를 UE 로 잡는 원천이다.

</details>

### 연습 3 — EDAC 로그의 신드롬 → 칩

`syn_to_chip(syn, chip_width, &chip)` 을 짜라: SYN_TAB 으로 비트 번호(0..71)를 구하고, DQ0..63 → 데이터 칩, CB0..7 → 마지막 ECC 칩이라는 가정에서 칩 번호 = 비트 / chip_width. UE 면 −1.

<details><summary>답 보기</summary>

0xAC → 비트 37 → x8 칩 4, x4 칩 9. 0x80 → c7 → x8 칩 8 (ECC 칩), x4 칩 17. 0x84 → s = 4 → c2 → CB2 → x8 칩 8. 0x06 (p = 0) 과 0xC8 (s = 72) → UE → −1. 실제 보드에선 DQ ↔ 칩 핀 배선(스와핑)과 컨트롤러 H 행렬이 벤더마다 달라서 이 가정 대신 보드/컨트롤러 문서의 표를 쓴다.

</details>

### 연습 4 — x4 칩 하나가 죽으면

x4 칩 18개 각각에 대해 그 칩의 4비트(같은 니블) 안 0 이 아닌 오류 패턴 15개, 총 270 패턴을 이 코드로 판정하라.

<details><summary>답 보기</summary>

실측 (정정/UE/오정정/무성): 무게 1 = 72/0/0/0, 무게 2 = 0/108/0/0, 무게 3 = 0/3/**69**/0, 무게 4 = 0/14/0/**4**. 무성 4개는 칩 3, 4, 5, 15 (비트 12–15, 16–19, 20–23, 60–63) 의 니블 전체다 — 위치가 18–21, 22–25, 26–29, 68–71 처럼 **짝수에서 시작하는 연속 4위치** 라 (2m ⊕ 2m+1) ⊕ (2m+2 ⊕ 2m+3) = 1 ⊕ 1 = 0. 칩 고장은 SECDED 의 오류 모델(독립 비트)과 맞지 않는다 → chipkill (MEM-A05).

</details>

### 연습 5 — Hsiao(72,64) 만들고 비교하기

무게 3 열 56개 전부 + 0x1F 의 순환 이동 8개(무게 5)를 데이터 열로, 단위 열 8개를 체크 열로 쓰는 H 를 만들어라. 열이 모두 다르고 홀수 무게인지, 단일 72개 정정 · 2비트 2,556 개 UE 인지 전수로 확인하고, 3비트 UE 비율과 체크당 데이터 입력 수(XOR 트리 폭)를 위치형과 비교하라.

<details><summary>답 보기</summary>

실측: `single corrected=72/72, double UE=2556/2556, triple n=59640 UE=26072 (43.7%) mis=33568`, 입력 수 Hsiao `26 ×8` vs 위치형 `35 35 35 31 31 31 7 35`. 무게 3 열 56개는 각 행에 56·3/8 = 21 번, 순환 이동 8개는 각 행에 5 번 → 모두 26 으로 균형. 3비트 신드롬은 홀수 무게 128개 중 열 72개에 맞으면 오정정, 나머지 56개면 UE → 거의 균등해 56/128 = 43.75 % 에 붙는다. 위치형의 c7 을 데이터만으로 쓰면 popcount(위치) 가 짝수인 35개 데이터의 XOR 이라는 것도 무작위 10,000 워드로 검산했다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 22/22 passed
```

## 10. 핵심 정리

- 레이아웃: 위치 2^j = c_j (j = 0..6), 위치 0 = c7 전체 패리티, 나머지 64 위치 = d0..d63. 저장은 데이터 64 + 체크 바이트 8 (DQ + CB).
- 인코드: c_j = parity(d & CMASK[j]), c7 = 나머지 71비트 패리티. 신드롬 = s | p<<7, 선형이라 **H·e — 데이터와 무관**.
- 판정: p = 1 → 위치 s 정정 (s = 0 이면 c7), p = 0 & s ≠ 0 → UE, s = 72..127 → UE. SYN_TAB 256 B, 기본값은 UE.
- HD = 4: 모든 열이 다르고(2비트 ≠ 0) 아래 행이 전부 1(3비트 ≠ 0). 8번째 비트가 없으면 2비트의 82 % 를 오정정한다.
- 실측: 단일 4,608/4,608 정정, 2비트 전수 2,556 + 무작위 1,000,000 전부 UE, 3비트 59,640 중 24.0 % UE · 76.0 % 오정정, 4비트 1.1 % 무성.
- 컨트롤러: CE → 정정 데이터 + 카운트 + scrub(고쳐 쓰기), UE → 원시 데이터 + 보고, 데이터 불변. scrub 이 없으면 1비트가 쌓여 UE 가 된다.
- 72/64 = 9번째 x8 칩. 부분 쓰기는 RMW. 실제 컨트롤러는 행 무게가 고른 Hsiao 류, 칩 고장은 chipkill (MEM-A05).

## 참고자료

- R. W. Hamming, "Error Detecting and Error Correcting Codes", Bell System Technical Journal 29(2), 1950 — 위치형 Hamming 코드의 원 논문.
- M. Y. Hsiao, "A Class of Optimal Minimum Odd-weight-column SEC-DED Codes", IBM Journal of R&D 14(4), 1970 — 홀수 무게 열 SECDED (연습 5).
- C. L. Chen, M. Y. Hsiao, "Error-Correcting Codes for Semiconductor Memory Applications: A State-of-the-Art Review", IBM Journal of R&D 28(2), 1984 — 메모리 ECC 전반.
- S. Lin, D. J. Costello, *Error Control Coding* (2nd ed.) — 선형 블록 코드, 최소 거리, 확장 Hamming.
- B. Schroeder, E. Pinheiro, W.-D. Weber, "DRAM Errors in the Wild: A Large-Scale Field Study", SIGMETRICS 2009 / V. Sridharan et al., "Memory Errors in Modern Systems", ASPLOS 2015 — 현장의 CE/UE 양상 (반복되는 hard fault 와 칩 단위 고장이 중요하다는 근거).
- JEDEC JESD79-4 (DDR4) / JESD79-5 (DDR5) — ECC DIMM 구성, DDR5 서브채널·on-die ECC (유료 문서, 구성 수치는 대표값 — 원문·모듈 데이터시트 확인 권장).
- Linux kernel documentation — EDAC (driver-api/edac, admin-guide RAS): <https://docs.kernel.org/driver-api/edac.html> (sysfs 경로·필드는 커널 버전에 따라 다름).
- 관련 레슨: MEM-B06 (Hamming(7,4)), SIV-M14 (ECC 오류 주입과 CE/UE 보고 검증), COMM-B08 (선형 코드의 신드롬 표와 HD), C-B01 (비트 연산), MEM-B08 · MEM-B09 (ECC 끈 상태의 bring-up 메모리 테스트), MEM-A02 (Row Hammer 와 ECC 의 한계), MEM-A05 (chipkill · Reed-Solomon), MEM-A06 (patrol scrub · page retire), MEM-M13 (NAND 오류 처리), RF-M01 (채널 위 BER/BLER)
