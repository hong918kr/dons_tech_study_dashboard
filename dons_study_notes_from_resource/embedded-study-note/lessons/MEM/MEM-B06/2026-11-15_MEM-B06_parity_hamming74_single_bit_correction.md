---
id: MEM-B06
time: 55분
updated: 2026-11-15
---
# MEM-B06 패리티와 Hamming(7,4): 단일 비트 정정

## 학습 목표

- 짝수 패리티(even parity)를 XOR 접기로 구현하고, "홀수 개 오류만 검출, 짝수 개는 전부 놓친다"를 16 데이터 × 31 오류 패턴 전수로 확인한다.
- Hamming(7,4)를 고전 배치(위치 1..7 = p1 p2 d1 p4 d2 d3 d4)로 짜고, **신드롬(syndrome) = 틀린 비트의 위치 번호**가 되는 이유를 검사 행렬(parity-check matrix) H 로 설명한다.
- 16 데이터 × 128 오류 패턴(무게 0..7) 2048 건을 전수 주입해 단일 오류 112/112 정정, 2비트 오류 336/336 **오정정(miscorrection)** 을 숫자로 보이고, 최소 거리(minimum distance) d = 3 과 완전 부호(perfect code)로 설명한다.
- 비트 인터리빙(bit interleaving)으로 짧은 버스트(burst)를 "코드워드마다 1비트"로 쪼개 정정하는 저장 배치를 만들고, 버스트 길이별 실패율을 전수로 잰다.
- 면접에서 "SECDED 는 왜 비트가 하나 더 필요한가 / 왜 72/64 인가"로 이어지는 꼬리질문에 답할 재료를 갖춘다 (72/64 구현은 MEM-B07).

## 1. 문제

DDR bring-up 로그의 "EDAC CE count", SoC 데이터시트의 "SECDED on TCM", NAND 컨트롤러의 "ECC 정정 비트 수" — 전부 같은 뿌리인 **신드롬 디코딩**에서 나온다. 면접에서 ECC 질문은 거의 항상 "가장 작은 예로 신드롬을 설명해 보세요"로 시작하고, 그 가장 작은 예가 Hamming(7,4)다.

Don 은 SSD 펌웨어에서 BCH/LDPC 디코더의 결과(정정 비트 수, 정정 실패)를 다뤄 봤다. 그 코드들도 원리는 같다: 받은 워드에 검사 행렬을 곱해 신드롬을 얻고, 신드롬이 0 이 아니면 "어떤 오류 패턴이 이 신드롬을 만드는가"를 푼다. Hamming(7,4)는 이 풀이가 **"신드롬 = 위치 번호"** 한 줄로 끝나는 유일하게 깔끔한 경우라서, 손으로 끝까지 따라갈 수 있다.

두 가지를 같이 본다.

1. **패리티(parity)** — 비트 하나를 붙여 1 의 개수를 짝수로 맞춘다. 홀수 개 오류는 잡고 짝수 개는 놓친다. 정정은 못 한다. SRAM 바이트 패리티, DDR4 C/A 패리티 같은 곳에 쓴다 (대표 예, 부품 데이터시트 확인).
2. **Hamming(7,4)** — 데이터 4비트에 검사 비트 3개. 3개의 패리티가 서로 다른 위치 집합을 덮어서, 어느 검사가 깨졌는지의 조합(3비트 = 신드롬)이 오류 위치를 가리킨다. 1비트는 고치고, **2비트는 고친다고 착각해 다른 데이터를 내놓는다**. 이 한계가 SECDED(MEM-B07)의 출발점이다.

**이 레슨의 정의 (Hamming 1950 고전 배치 — 비트 순서는 이 레슨의 약속)**

- 위치 1..7 = `p1 p2 d1 p4 d2 d3 d4`. 검사 비트는 2의 거듭제곱 위치(1, 2, 4).
- 코드워드는 `uint8_t cw`, **bit i = 위치 i** (i = 1..7), bit 0 은 0 (예약 — 연습 1 에서 SECDED 의 전체 패리티 p0 자리가 된다).
- 데이터 니블 `d`: bit0 = d1, bit1 = d2, bit2 = d3, bit3 = d4. 예: `d = 0xB = 1011b` → d1 = 1, d2 = 1, d3 = 0, d4 = 1.
- 짝수 패리티 워드: 5비트, bit0..3 = 데이터, bit4 = p.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Hamming(7,4) 검사 행렬 H — 열 i 를 위에서 아래로 읽으면 위치 번호 i 의 2진수</text>
  <text x="10" y="56" fill="currentColor">위치 i</text>
  <g fill="currentColor" text-anchor="middle"><text x="230" y="56">1</text><text x="300" y="56">2</text><text x="370" y="56">3</text><text x="440" y="56">4</text><text x="510" y="56">5</text><text x="580" y="56">6</text><text x="650" y="56">7</text></g>
  <text x="10" y="78" fill="currentColor">2진 (b2 b1 b0)</text>
  <g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="230" y="78">001</text><text x="300" y="78">010</text><text x="370" y="78">011</text><text x="440" y="78">100</text><text x="510" y="78">101</text><text x="580" y="78">110</text><text x="650" y="78">111</text></g>
  <text x="10" y="105" fill="currentColor">역할</text>
  <g stroke="currentColor"><rect x="200" y="88" width="60" height="24" class="f-acc"/><rect x="270" y="88" width="60" height="24" class="f-acc"/><rect x="340" y="88" width="60" height="24" class="f-mut"/><rect x="410" y="88" width="60" height="24" class="f-acc"/><rect x="480" y="88" width="60" height="24" class="f-mut"/><rect x="550" y="88" width="60" height="24" class="f-mut"/><rect x="620" y="88" width="60" height="24" class="f-mut"/></g>
  <g fill="currentColor" text-anchor="middle"><text x="230" y="105">p1</text><text x="300" y="105">p2</text><text x="370" y="105">d1</text><text x="440" y="105">p4</text><text x="510" y="105">d2</text><text x="580" y="105">d3</text><text x="650" y="105">d4</text></g>
  <text x="10" y="139" fill="currentColor">s0: p1 검사 (b0 = 1)</text>
  <text x="10" y="169" fill="currentColor">s1: p2 검사 (b1 = 1)</text>
  <text x="10" y="199" fill="currentColor">s2: p4 검사 (b2 = 1)</text>
  <g stroke="currentColor">
    <rect x="200" y="122" width="60" height="24" class="f-ok"/><rect x="270" y="122" width="60" height="24" fill="none"/><rect x="340" y="122" width="60" height="24" class="f-ok"/><rect x="410" y="122" width="60" height="24" fill="none"/><rect x="480" y="122" width="60" height="24" class="f-ok"/><rect x="550" y="122" width="60" height="24" fill="none"/><rect x="620" y="122" width="60" height="24" class="f-ok"/>
    <rect x="200" y="152" width="60" height="24" fill="none"/><rect x="270" y="152" width="60" height="24" class="f-ok"/><rect x="340" y="152" width="60" height="24" class="f-ok"/><rect x="410" y="152" width="60" height="24" fill="none"/><rect x="480" y="152" width="60" height="24" fill="none"/><rect x="550" y="152" width="60" height="24" class="f-ok"/><rect x="620" y="152" width="60" height="24" class="f-ok"/>
    <rect x="200" y="182" width="60" height="24" fill="none"/><rect x="270" y="182" width="60" height="24" fill="none"/><rect x="340" y="182" width="60" height="24" fill="none"/><rect x="410" y="182" width="60" height="24" class="f-ok"/><rect x="480" y="182" width="60" height="24" class="f-ok"/><rect x="550" y="182" width="60" height="24" class="f-ok"/><rect x="620" y="182" width="60" height="24" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="230" y="139">1</text><text x="300" y="139">0</text><text x="370" y="139">1</text><text x="440" y="139">0</text><text x="510" y="139">1</text><text x="580" y="139">0</text><text x="650" y="139">1</text>
    <text x="230" y="169">0</text><text x="300" y="169">1</text><text x="370" y="169">1</text><text x="440" y="169">0</text><text x="510" y="169">0</text><text x="580" y="169">1</text><text x="650" y="169">1</text>
    <text x="230" y="199">0</text><text x="300" y="199">0</text><text x="370" y="199">0</text><text x="440" y="199">1</text><text x="510" y="199">1</text><text x="580" y="199">1</text><text x="650" y="199">1</text>
  </g>
  <g fill="currentColor" font-family="monospace"><text x="690" y="139">0xAA</text><text x="690" y="169">0xCC</text><text x="690" y="199">0xF0</text></g>
  <text x="10" y="239" fill="currentColor">예: d = 0xB → cw</text>
  <g stroke="currentColor"><rect x="200" y="222" width="60" height="24" class="f-acc"/><rect x="270" y="222" width="60" height="24" class="f-acc"/><rect x="340" y="222" width="60" height="24" class="f-mut"/><rect x="410" y="222" width="60" height="24" class="f-acc"/><rect x="480" y="222" width="60" height="24" class="f-mut"/><rect x="550" y="222" width="60" height="24" class="f-mut"/><rect x="620" y="222" width="60" height="24" class="f-mut"/></g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="230" y="239">1</text><text x="300" y="239">0</text><text x="370" y="239">1</text><text x="440" y="239">0</text><text x="510" y="239">1</text><text x="580" y="239">0</text><text x="650" y="239">1</text></g>
  <text x="690" y="239" fill="currentColor" font-family="monospace">0xAA</text>
  <text x="10" y="272" fill="currentColor" font-size="12">uint8_t 의 bit i = 위치 i 이므로 각 검사 행이 그대로 마스크(0xAA / 0xCC / 0xF0, bit 0 은 예약)가 된다.</text>
  <text x="10" y="290" fill="currentColor" font-size="12">위치 i 하나가 틀리면 깨지는 검사 = 열 i 의 1 들 → 신드롬 (s2 s1 s0) = i 의 2진수 = 오류 위치.</text>
</svg>
```

### Part A — 패리티와 Hamming(7,4) 인코더 / 신드롬 / 디코더

- `parity8(x)`: 1 의 개수 mod 2 를 **XOR 접기**(x ^= x>>4; x ^= x>>2; x ^= x>>1)로 구한다. 루프·분기 없이 비트 연산만.
- 짝수 패리티 `ep_encode` / `ep_ok`(주어짐)로 16 데이터 × 31 개의 0 아닌 5비트 오류를 전수 주입해, "검출 ⇔ 오류 무게가 홀수"를 확인한다 (256 검출, 240 놓침).
- `h74_syndrome(cw)`: 위 그림의 세 마스크로 거른 비트들의 패리티 → 3비트 신드롬.
- `h74_encode(d)`: 데이터를 위치 3, 5, 6, 7 에 놓고(`h74_place`, 주어짐), 그 상태의 신드롬 s 를 구해 **s 의 bit k 를 위치 2^k 에** 넣는다. 그러면 전체 신드롬이 0 이 된다.
- `h74_decode(rx)`: 신드롬 ≠ 0 이면 그 위치 비트를 뒤집고 데이터를 꺼낸다(`h74_extract`, 주어짐). 결과로 {data, 고친 cw, syndrome} 을 돌려준다.

### Part B — 전수 오류 주입: 정정, 오정정, 조용한 오염

- `h74_outcome(d, e)`: enc(d) ⊕ e 를 디코드해 4 가지로 분류한다 — OK(오류 없음), CORRECTED(오류 있었고 데이터 맞음), MISCORRECTED(신드롬 ≠ 0 이라 "고쳤는데" 데이터가 틀림), UNDETECTED(신드롬 0 인데 데이터가 틀림 = 조용한 오염).
- `h74_classify_all(table)`: 16 데이터 × 128 오류 패턴(위치 1..7 의 모든 부분집합) = 2048 건을 오류 무게 0..7 별로 표로 만든다. 단일 오류 112/112 정정, 2비트 336/336 오정정을 assert 하고, 3비트 이상도 전부 데이터가 틀린다는 것(완전 부호)을 보인다.
- `code_min_distance()`: 120 개 코드워드 쌍의 최소 해밍 거리 = 3.
- 하네스는 2비트 오류마다 "신드롬 = i XOR j, 최종 오류 무게 = 3" 과 틀린 데이터 비트 수 분포를 찍는다.

### Part C — 32비트 워드 저장과 비트 인터리빙

현장에서 오류는 혼자 오지 않는다. SRAM 의 중성자 한 방(MBU, multi-bit upset)은 **물리적으로 이웃한** 셀 여러 개를 뒤집고, DRAM 칩 하나가 죽으면 그 칩이 내놓는 비트가 한꺼번에 틀린다. 그래서 실제 메모리는 한 코드워드의 비트를 물리적으로 흩어 놓는다(SRAM column interleaving, DRAM 의 "칩마다 코드워드 비트 몇 개" — chipkill 은 MEM-A05).

- 32비트 워드 = 니블 8개 = Hamming(7,4) 코드워드 8개 = **56 물리 비트** (`uint64_t` 의 bit 0..55).
- `phys_index(j, pos, il)`(주어짐): 순차(sequential) 배치는 `7j + (pos−1)` (코드워드가 7비트씩 이어 붙음), 인터리브 배치는 `8(pos−1) + j` (같은 위치끼리 8비트씩 모음 → 이웃 비트 = 서로 다른 코드워드).
- `blk_encode(w, il)` / `blk_decode(phys, il)`: 니블별로 인코드·디코드하고, 신드롬 ≠ 0 이었던 코드워드 수를 `ce` 로 보고한다 (정정 오류 카운터 — CE/UE 보고 정책은 SIV-M14 · MEM-A06).
- `burst_fail_count(w, L, il, &n)`: 길이 L 버스트(첫·끝 비트는 반드시, 사이 L−2 비트는 **모든 조합**)를 모든 시작 위치에 넣어 데이터가 틀린 경우를 센다. 인터리브는 L ≤ 8 전부 정정, L = 9 는 전부 실패임을 assert 한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">56 물리 비트 중 앞 24 비트 (칸 안 = 코드워드 번호 j, 진한 칸 = 버스트 4~6)</text>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="93" y="44">0</text><text x="120" y="44">1</text><text x="147" y="44">2</text><text x="174" y="44">3</text><text x="201" y="44">4</text><text x="228" y="44">5</text><text x="255" y="44">6</text><text x="282" y="44">7</text><text x="309" y="44">8</text><text x="336" y="44">9</text><text x="363" y="44">10</text><text x="390" y="44">11</text><text x="417" y="44">12</text><text x="444" y="44">13</text><text x="471" y="44">14</text><text x="498" y="44">15</text><text x="525" y="44">16</text><text x="552" y="44">17</text><text x="579" y="44">18</text><text x="606" y="44">19</text><text x="633" y="44">20</text><text x="660" y="44">21</text><text x="687" y="44">22</text><text x="714" y="44">23</text></g>
  <text x="10" y="72" fill="currentColor">순차</text>
  <g stroke="currentColor"><rect x="80" y="55" width="27" height="24" class="f-mut"/><rect x="107" y="55" width="27" height="24" class="f-mut"/><rect x="134" y="55" width="27" height="24" class="f-mut"/><rect x="161" y="55" width="27" height="24" class="f-mut"/><rect x="188" y="55" width="27" height="24" class="f-warn"/><rect x="215" y="55" width="27" height="24" class="f-warn"/><rect x="242" y="55" width="27" height="24" class="f-warn"/><rect x="269" y="55" width="27" height="24" fill="none"/><rect x="296" y="55" width="27" height="24" fill="none"/><rect x="323" y="55" width="27" height="24" fill="none"/><rect x="350" y="55" width="27" height="24" fill="none"/><rect x="377" y="55" width="27" height="24" fill="none"/><rect x="404" y="55" width="27" height="24" fill="none"/><rect x="431" y="55" width="27" height="24" fill="none"/><rect x="458" y="55" width="27" height="24" class="f-mut"/><rect x="485" y="55" width="27" height="24" class="f-mut"/><rect x="512" y="55" width="27" height="24" class="f-mut"/><rect x="539" y="55" width="27" height="24" class="f-mut"/><rect x="566" y="55" width="27" height="24" class="f-mut"/><rect x="593" y="55" width="27" height="24" class="f-mut"/><rect x="620" y="55" width="27" height="24" class="f-mut"/><rect x="647" y="55" width="27" height="24" fill="none"/><rect x="674" y="55" width="27" height="24" fill="none"/><rect x="701" y="55" width="27" height="24" fill="none"/></g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="93" y="72">0</text><text x="120" y="72">0</text><text x="147" y="72">0</text><text x="174" y="72">0</text><text x="201" y="72">0</text><text x="228" y="72">0</text><text x="255" y="72">0</text><text x="282" y="72">1</text><text x="309" y="72">1</text><text x="336" y="72">1</text><text x="363" y="72">1</text><text x="390" y="72">1</text><text x="417" y="72">1</text><text x="444" y="72">1</text><text x="471" y="72">2</text><text x="498" y="72">2</text><text x="525" y="72">2</text><text x="552" y="72">2</text><text x="579" y="72">2</text><text x="606" y="72">2</text><text x="633" y="72">2</text><text x="660" y="72">3</text><text x="687" y="72">3</text><text x="714" y="72">3</text></g>
  <text x="80" y="100" fill="currentColor" font-size="12">버스트 3비트가 전부 코드워드 0 에 들어감 → 3비트 오류 → 데이터 틀림 (오정정 또는 조용한 오염)</text>
  <text x="10" y="142" fill="currentColor">인터리브</text>
  <g stroke="currentColor"><rect x="80" y="125" width="27" height="24" class="f-mut"/><rect x="107" y="125" width="27" height="24" fill="none"/><rect x="134" y="125" width="27" height="24" fill="none"/><rect x="161" y="125" width="27" height="24" fill="none"/><rect x="188" y="125" width="27" height="24" class="f-warn"/><rect x="215" y="125" width="27" height="24" class="f-warn"/><rect x="242" y="125" width="27" height="24" class="f-warn"/><rect x="269" y="125" width="27" height="24" fill="none"/><rect x="296" y="125" width="27" height="24" class="f-mut"/><rect x="323" y="125" width="27" height="24" fill="none"/><rect x="350" y="125" width="27" height="24" fill="none"/><rect x="377" y="125" width="27" height="24" fill="none"/><rect x="404" y="125" width="27" height="24" fill="none"/><rect x="431" y="125" width="27" height="24" fill="none"/><rect x="458" y="125" width="27" height="24" fill="none"/><rect x="485" y="125" width="27" height="24" fill="none"/><rect x="512" y="125" width="27" height="24" class="f-mut"/><rect x="539" y="125" width="27" height="24" fill="none"/><rect x="566" y="125" width="27" height="24" fill="none"/><rect x="593" y="125" width="27" height="24" fill="none"/><rect x="620" y="125" width="27" height="24" fill="none"/><rect x="647" y="125" width="27" height="24" fill="none"/><rect x="674" y="125" width="27" height="24" fill="none"/><rect x="701" y="125" width="27" height="24" fill="none"/></g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="93" y="142">0</text><text x="120" y="142">1</text><text x="147" y="142">2</text><text x="174" y="142">3</text><text x="201" y="142">4</text><text x="228" y="142">5</text><text x="255" y="142">6</text><text x="282" y="142">7</text><text x="309" y="142">0</text><text x="336" y="142">1</text><text x="363" y="142">2</text><text x="390" y="142">3</text><text x="417" y="142">4</text><text x="444" y="142">5</text><text x="471" y="142">6</text><text x="498" y="142">7</text><text x="525" y="142">0</text><text x="552" y="142">1</text><text x="579" y="142">2</text><text x="606" y="142">3</text><text x="633" y="142">4</text><text x="660" y="142">5</text><text x="687" y="142">6</text><text x="714" y="142">7</text></g>
  <text x="80" y="170" fill="currentColor" font-size="12">같은 버스트가 코드워드 4, 5, 6 에 1비트씩 → 셋 다 단일 오류 → 전부 정정 (CE 3)</text>
  <text x="80" y="188" fill="currentColor" font-size="12">회색 = 코드워드 0 의 비트 (8 칸마다). 길이 ≤ 8 버스트는 코드워드마다 최대 1비트</text>
  <text x="10" y="222" fill="currentColor" font-size="12">순차: 코드워드 j = b / 7 (위치 = b mod 7 + 1)   인터리브: j = b mod 8 (위치 = b / 8 + 1)</text>
  <text x="10" y="240" fill="currentColor" font-size="12">비용은 같다 (56 비트, 데이터 32 비트). 바뀐 것은 배선(어느 셀이 어느 코드워드에 속하나)뿐이다.</text>
</svg>
```

## 2. Problem (English)

You are writing the reference model for a tiny on-chip ECC block (used to protect a small register file / SRAM in a test chip). Bit operations only, no tables in the core path.

```c
/* codeword layout: uint8_t bit i = Hamming position i (1..7) = p1 p2 d1 p4 d2 d3 d4; bit 0 reserved (0)
 * data nibble: bit0 = d1 ... bit3 = d4 */
unsigned   parity8(uint8_t x);                   /* popcount(x) & 1, by XOR folding */
unsigned   h74_syndrome(uint8_t cw);             /* 3-bit syndrome; for a single error at position i it equals i */
uint8_t    h74_encode(uint8_t d);                /* 4 data bits -> codeword with syndrome 0 */
h74_result h74_decode(uint8_t rx);               /* {data, corrected cw, syndrome}; flips bit 'syndrome' if != 0 */

typedef enum { OUT_OK, OUT_CORRECTED, OUT_MISCORRECTED, OUT_UNDETECTED } outcome_t;
outcome_t  h74_outcome(uint8_t d, uint8_t e);     /* decode(enc(d) ^ e) and classify */
void       h74_classify_all(unsigned table[8][4]);/* all 16 data x 128 error patterns, by error weight */
unsigned   code_min_distance(void);

uint64_t   blk_encode(uint32_t w, bool interleaved);        /* 8 nibbles -> 8 codewords -> 56 physical bits */
blk_result blk_decode(uint64_t phys, bool interleaved);     /* {data, ce = #codewords with nonzero syndrome} */
unsigned   burst_fail_count(uint32_t w, unsigned L, bool interleaved, unsigned *ntested);
```

Constraints: no heap, deterministic, exhaustive where the space is small (it always is here), whole self-test < 2 s.

Examples:

```text
h74_encode(0xB) = 0xAA        (positions 1..7 = 1 0 1 0 1 0 1)
h74_encode(0x8) = 0x96        (d4 alone touches all three checks: positions 1,2,4,7)
h74_decode(0xAA ^ 1<<5)  -> syndrome 5, data 0xB         (single error: corrected)
h74_decode(0xAA ^ 1<<2 ^ 1<<5) -> syndrome 7, cw 0x0E, data 0x1   (double error: miscorrected)
single errors 112/112 corrected; double errors 336/336 miscorrected; d_min = 3
interleaved layout: every burst of length <= 8 in the 56-bit word is corrected; every burst of length 9 fails
```

Follow-ups: Why is the syndrome equal to the error position? Why can't Hamming(7,4) even *detect* a double error? What does one extra parity bit buy (SECDED), and why do DRAM DIMMs use 72 bits for 64? Why interleave?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열 군데다 (Part A 4개: `parity8`, `h74_syndrome`, `h74_encode`, `h74_decode` / Part B 3개: `h74_outcome`, `h74_classify_all`, `code_min_distance` / Part C 3개: `blk_encode`, `blk_decode`, `burst_fail_count`). `ep_encode`/`ep_ok`, `h74_place`/`h74_extract`, `phys_index`, 하네스는 주어진다. 스텁은 안전한 틀린 값을 돌려준다 (인코더는 데이터만 놓고 검사 비트를 비워 둠, 디코더는 정정하지 않음, 개수는 0). 처음 실행 결과 (실제 출력, 앞부분):

```text
FAIL starter.c:137  parity8(0x00) == 0 && parity8(0x01) == 1 && parity8(0x80) == 1
FAIL starter.c:138  parity8(0xFF) == 0 && parity8(0x7F) == 1 && parity8(0xAA) == 0
FAIL starter.c:142  all
even parity (4+1): detected=0 missed=496 of 496 error cases
FAIL starter.c:157  rule
FAIL starter.c:158  det == 16 * 16 && miss == 16 * 15
FAIL starter.c:162  h74_encode(0xB) == 0xAA
…
codewords d->cw: 0:00 1:08 2:20 3:28 4:40 5:48 6:60 7:68 8:80 9:88 A:A0 B:A8 C:C0 D:C8 E:E0 F:E8
single errors: corrected 0/112, syndrome==position 0/112
…
FAIL starter.c:294  fs[2] == 4 * 48
11/41 passed
```

(exit 1. 통과한 11개는 스텁이 우연히 맞는 경우다 — `h74_encode(0) == 0`, 오류 없는 입력의 OK 판정, `phys_index` 표, "인터리브 실패 0" 처럼 0 이 정답인 검사.)

## 4. 힌트

<details><summary>힌트 1 — XOR 접기</summary>

패리티는 "모든 비트의 XOR" 이다. 8비트를 반으로 접어 XOR 하면 4비트 안에 같은 패리티가 남는다: `x ^= x >> 4` 후 하위 4비트의 패리티 = 원래 8비트의 패리티. 4 → 2 → 1 로 세 번 접으면 bit0 이 답이다. 위쪽 비트에는 쓰레기가 남으므로 마지막에 `& 1`.

</details>

<details><summary>힌트 2 — 신드롬은 마스크 세 개</summary>

검사 k(k = 0, 1, 2)는 "위치 번호의 bit k 가 1 인 위치들"의 패리티다. bit i = 위치 i 로 저장했으니 그 위치 집합이 바로 마스크 0xAA(1,3,5,7), 0xCC(2,3,6,7), 0xF0(4,5,6,7) 이다. `s = parity8(cw & 0xAA) | parity8(cw & 0xCC) << 1 | parity8(cw & 0xF0) << 2`.

</details>

<details><summary>힌트 3 — 인코더는 "신드롬을 0 으로 만드는 검사 비트"</summary>

데이터만 놓은 c 의 신드롬 s 를 구한다. 위치 1 은 검사 s0 에만, 위치 2 는 s1 에만, 위치 4 는 s2 에만 들어간다. 그러니 s 의 bit k 를 위치 2^k 에 넣으면 그 검사만 정확히 뒤집혀 신드롬이 0 이 된다. bit i = 위치 i 라서 s 의 bit0 → bit1, bit1 → bit2, bit2 → bit4 로 옮긴다: `(s & 1) << 1 | (s & 2) << 1 | (s & 4) << 2`.

</details>

<details><summary>힌트 4 — 분류는 "데이터가 맞았나" 와 "신드롬이 0 이었나"</summary>

데이터가 맞으면 e == 0 이냐에 따라 OK / CORRECTED. 데이터가 틀리면 디코더가 본 신드롬이 0 이었냐에 따라 UNDETECTED / MISCORRECTED. 패턴 128 개는 `e = x << 1` (x = 0..127) 로 만든다 — bit 0 은 예약이라 오류를 넣지 않는다.

</details>

<details><summary>힌트 5 — 버스트 전수</summary>

시작 s 마다 `e = 1 << s | 1 << (s + L − 1) | m << (s + 1)` (m = 0 .. 2^(L−2) − 1). L = 1 이면 `e = 1 << s` 하나. 시도 수는 (57 − L) × 2^(L−2) 이다. 56 비트는 `uint64_t` 안에 들어가니 `1ull <<` 를 쓴다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **검사 행렬부터 그린다.** "열 i 가 i 의 2진수인 3 × 7 행렬 H. 코드워드는 H·c = 0 인 벡터들."
2. **신드롬 = H·(받은 워드) = H·e.** 코드워드 부분은 0 이 되므로 신드롬은 오류에만 의존한다 (데이터와 무관).
3. **단일 오류 e = e_i 면 H·e_i = 열 i = i 의 2진수** → 신드롬이 곧 위치. 0 이 아닌 3비트 값 7 개가 위치 7 개와 1:1.
4. **한계를 말한다.** 2비트 오류의 신드롬 i ⊕ j 도 0 이 아닌 어떤 위치 k 를 가리킨다 → 디코더는 k 를 뒤집어 무게 3 오류(= 다른 코드워드)를 만든다. d = 3 이라 "1 정정 또는 2 검출" 중 하나만 된다. 둘 다 원하면 비트 하나를 더 (SECDED, MEM-B07).

### Part A — 패리티, 인코더, 신드롬, 디코더

**XOR 접기 추적** (x = 0xB5 = 1011 0101, 1 이 5개 → 홀수):

| 단계 | 연산 | x (16진) | 의미 |
|---|---|---|---|
| 0 | 시작 | 0xB5 | |
| 1 | `x ^= x >> 4` | 0xBE | 하위 4비트 1110 = 상위 니블 ⊕ 하위 니블 |
| 2 | `x ^= x >> 2` | 0x91 | 하위 2비트 01 |
| 3 | `x ^= x >> 1` | 0xD9 | bit0 = 1 → 홀수 |

(실행으로 확인한 값. 위쪽 비트는 쓰레기라 `& 1` 이 필수.) 연산 수는 n 비트에 log2 n 단계 — 32비트는 5단계 (연습 5).

**짝수 패리티의 검출 규칙.** 5비트 워드에 오류 e 를 XOR 하면 패리티가 `parity(e)` 만큼 바뀐다 (XOR 은 선형). 그래서 검출 ⇔ e 의 무게가 홀수. 5비트에서 0 아닌 패턴 31 개 중 홀수 무게 16 개(C(5,1)+C(5,3)+C(5,5) = 5+10+1), 짝수 무게 15 개(10+5). 실측 `detected=256 missed=240` = 16 × 16, 16 × 15. 데이터와 무관하다는 점이 중요하다 — 패리티도 선형 부호다.

**검사 행렬과 신드롬.** H 의 열 i = i 의 2진수 (그림 1). 받은 워드 r = c ⊕ e 에서 H·r = H·c ⊕ H·e = H·e (코드워드는 H·c = 0 이 정의). 오류가 위치 i 하나면 H·e_i = 열 i = i. 그래서 **신드롬 = XOR(1 인 비트들의 위치 번호)** 라는 한 줄 공식도 나온다 (연습 3 에서 마스크 방식과 128 입력 전부 같음을 확인).

**인코딩 추적** (d = 0xB = 1011b → d1 = 1, d2 = 1, d3 = 0, d4 = 1):

| 단계 | 값 | 설명 |
|---|---|---|
| place | 0xA8 = 1010 1000 | d1 → bit3, d2 → bit5, d4 → bit7 (d3 = 0) |
| s0 = parity(0xA8 & 0xAA = 0xA8) | 1 | 위치 3, 5, 7 → 1 이 3개 |
| s1 = parity(0xA8 & 0xCC = 0x88) | 0 | 위치 3, 7 |
| s2 = parity(0xA8 & 0xF0 = 0xA0) | 0 | 위치 5, 7 |
| s = 001 → 위치 1 에 1 | 0xAA | p1 = 1, p2 = 0, p4 = 0 |

같은 결과를 공식으로: XOR(3, 5, 7) = 3 ⊕ 5 ⊕ 7 = 1 → 위치 1 에 1. 교과서식 식으로는 p1 = d1 ⊕ d2 ⊕ d4, p2 = d1 ⊕ d3 ⊕ d4, p4 = d2 ⊕ d3 ⊕ d4 (각 검사가 덮는 데이터 위치).

16 개 코드워드 전체 (실제 출력):

```text
codewords d->cw: 0:00 1:0E 2:32 3:3C 4:54 5:5A 6:66 7:68 8:96 9:98 A:A4 B:AA C:C2 D:CC E:F0 F:FE
```

무게 분포: 0 이 1개(0x00), 3 이 7개(1, 2, 4, 7, 9, A, C), 4 가 7개(3, 5, 6, 8, B, D, E), 7 이 1개(0xFE). 이것이 Hamming(7,4) 의 가중치 분포 1 + 7z³ + 7z⁴ + z⁷ 이다.

**디코딩 추적** (보낸 0xAA, 위치 5 오류 → 받음 0x8A): 1 인 위치 = 1, 3, 7 → 1 ⊕ 3 ⊕ 7 = 5 → bit5 를 뒤집어 0xAA → 데이터 0xB. 하네스 실측: 16 × 7 = 112 개 단일 오류 전부 `syndrome == position`, 전부 정정.

### Part B — 왜 2비트는 반드시 틀리는가

전수 결과표 (실제 출력, `./solution`):

```text
weight  patterns  ok  corrected  miscorrected  undetected   (16 data each)
  0       16     16      0          0             0
  1      112      0    112          0             0
  2      336      0      0        336             0
  3      560      0      0        448           112
  4      560      0      0        448           112
  5      336      0      0        336             0
  6      112      0      0        112             0
  7       16      0      0          0            16
```

세 가지 사실로 이 표 전체를 설명할 수 있다.

**(1) 최소 거리 d = 3.** 선형 부호에서 두 코드워드의 거리 = 그 XOR(역시 코드워드)의 무게이므로 d = 0 이 아닌 코드워드의 최소 무게 = 3 (`code_min_distance() == 3` 실측). 거리 d 부호는 ⌊(d−1)/2⌋ = 1 비트를 정정하거나, d − 1 = 2 비트를 검출할 수 있다 — **동시에는 안 된다.** 정정 디코더를 쓰는 순간 2비트 검출은 포기한 것이다.

**(2) 2비트 오류의 대수.** 위치 i, j (i ≠ j) 오류의 신드롬은 i ⊕ j ≠ 0 이고, 이 값 k 는 i 도 j 도 아닌 세 번째 위치다 (i ⊕ j = i 이면 j = 0 이 되므로). 디코더는 k 를 뒤집는다. 최종 오류 {i, j, k} 의 위치 XOR = i ⊕ j ⊕ (i ⊕ j) = 0 → 무게 3 코드워드 → 다른 데이터. 각 신드롬 k 는 정확히 3 쌍에서 나온다 (예: 7 = 1⊕6 = 2⊕5 = 3⊕4), 21 쌍 = 7 × 3. 실측: 336 건 모두 `syndrome = i^j and final error weight 3`.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">2비트 오류의 오정정: d = 0xB, 위치 2 · 5 가 뒤집힘</text>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="272" y="40">1</text><text x="320" y="40">2</text><text x="368" y="40">3</text><text x="416" y="40">4</text><text x="464" y="40">5</text><text x="512" y="40">6</text><text x="560" y="40">7</text></g>
  <text x="10" y="64" fill="currentColor">보냄 enc(0xB) = 0xAA</text>
  <g stroke="currentColor"><rect x="250" y="48" width="44" height="24" class="f-mut"/><rect x="298" y="48" width="44" height="24" class="f-mut"/><rect x="346" y="48" width="44" height="24" class="f-mut"/><rect x="394" y="48" width="44" height="24" class="f-mut"/><rect x="442" y="48" width="44" height="24" class="f-mut"/><rect x="490" y="48" width="44" height="24" class="f-mut"/><rect x="538" y="48" width="44" height="24" class="f-mut"/></g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="272" y="65">1</text><text x="320" y="65">0</text><text x="368" y="65">1</text><text x="416" y="65">0</text><text x="464" y="65">1</text><text x="512" y="65">0</text><text x="560" y="65">1</text></g>
  <text x="596" y="65" fill="currentColor" font-size="12">s = 0</text>
  <text x="10" y="104" fill="currentColor">받음 = 0x8E</text>
  <g stroke="currentColor"><rect x="250" y="88" width="44" height="24" class="f-mut"/><rect x="298" y="88" width="44" height="24" class="f-warn"/><rect x="346" y="88" width="44" height="24" class="f-mut"/><rect x="394" y="88" width="44" height="24" class="f-mut"/><rect x="442" y="88" width="44" height="24" class="f-warn"/><rect x="490" y="88" width="44" height="24" class="f-mut"/><rect x="538" y="88" width="44" height="24" class="f-mut"/></g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="272" y="105">1</text><text x="320" y="105">1</text><text x="368" y="105">1</text><text x="416" y="105">0</text><text x="464" y="105">0</text><text x="512" y="105">0</text><text x="560" y="105">1</text></g>
  <text x="596" y="105" fill="currentColor" font-size="12">s = 2 ⊕ 5 = 7</text>
  <text x="10" y="144" fill="currentColor">위치 7 반전 → 0x0E</text>
  <g stroke="currentColor"><rect x="250" y="128" width="44" height="24" class="f-mut"/><rect x="298" y="128" width="44" height="24" class="f-warn"/><rect x="346" y="128" width="44" height="24" class="f-mut"/><rect x="394" y="128" width="44" height="24" class="f-mut"/><rect x="442" y="128" width="44" height="24" class="f-warn"/><rect x="490" y="128" width="44" height="24" class="f-mut"/><rect x="538" y="128" width="44" height="24" class="f-acc"/></g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="272" y="145">1</text><text x="320" y="145">1</text><text x="368" y="145">1</text><text x="416" y="145">0</text><text x="464" y="145">0</text><text x="512" y="145">0</text><text x="560" y="145">0</text></g>
  <text x="596" y="145" fill="currentColor" font-size="12">= enc(0x1), s = 0</text>
  <line x1="190" y1="240" x2="550" y2="240" stroke="currentColor"/>
  <ellipse cx="190" cy="240" rx="140" ry="52" fill="none" stroke="currentColor" stroke-dasharray="5 4"/>
  <ellipse cx="550" cy="240" rx="140" ry="52" fill="none" stroke="currentColor" stroke-dasharray="5 4"/>
  <circle cx="190" cy="240" r="7" class="f-ok" stroke="currentColor"/>
  <circle cx="310" cy="240" r="6" class="f-mut" stroke="currentColor"/>
  <circle cx="430" cy="240" r="6" class="f-warn" stroke="currentColor"/>
  <circle cx="550" cy="240" r="7" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="190" y="226">enc(0xB) 0xAA</text><text x="310" y="226">1비트 오류</text><text x="430" y="226">0x8E</text><text x="550" y="226">enc(0x1) 0x0E</text>
  <text x="190" y="264">거리 0</text><text x="310" y="264">→ 0xAA 로 정정</text><text x="440" y="280">0xAA 에서 2 · 0x0E 에서 1</text><text x="550" y="264">거리 3</text></g>
  <text x="10" y="300" fill="currentColor" font-size="12">점선 = 반경 1 디코딩 영역. d = 3 이라 두 영역은 겹치지 않지만 빈틈도 없다 (완전 부호).</text>
  <text x="10" y="318" fill="currentColor" font-size="12">그래서 2비트 오류는 반드시 다른 코드워드의 영역에 떨어지고, 디코더는 그쪽으로 "정정"한다.</text>
</svg>
```

**(3) 완전 부호(perfect code).** 반경 1 영역 하나에 워드 1 + 7 = 8 개, 코드워드 16 개 → 16 × 8 = 128 = 2^7. 7비트 워드 전체를 빈틈없이, 겹침 없이 덮는다 (해밍 한계 2^r ≥ n + 1 이 등호로 성립: 2^3 = 7 + 1). 하네스의 `all8` 검사가 "128 워드를 디코드하면 데이터마다 정확히 8 개씩"을 확인한다. 결과:

- 디코더는 **"고칠 수 없음"이라는 답을 낼 자리가 없다.** 어떤 워드든 가장 가까운 코드워드가 하나 있다.
- 오류 무게 ≥ 2 면 받은 워드는 원래 코드워드의 영역 밖 → 다른 코드워드의 영역 → 데이터가 틀린다. 표에서 무게 2..7 의 ok + corrected 합이 0 인 이유 (`good == 0` assert).
- 무게 3, 4 에서 "undetected" 112 = 7 패턴 × 16: 오류 자체가 무게 3·4 코드워드 7 개씩이면 신드롬 0 → 디코더는 아무것도 모른다. 무게 7(전부 뒤집힘) = 0xFE 도 코드워드라 16 건 전부 조용한 오염.

**오정정의 피해 크기.** 2비트 오류 336 건에서 틀린 데이터 비트 수 (실제 출력):

```text
double errors: 336 cases, syndrome = i^j and final error weight 3: 336; wrong data bits 0..4: 0 144 144 48 0
```

최종 오류가 무게 3 코드워드 7 개 중 하나이고, 그 데이터 무게가 1, 1, 1, 2, 2, 2, 3 (데이터 1, 2, 4 / 9, A, C / 7) → 각 코드워드가 3 쌍씩 × 16 데이터 = 144, 144, 48. 즉 **2비트 오류는 데이터 1~3비트를 망가뜨리고 "정정 1건"으로 보고된다.** CE 카운터만 보는 펌웨어는 이것을 정상 정정으로 센다 — SECDED 가 필요한 이유.

### Part C — 인터리빙: 같은 비용으로 버스트를 쪼갠다

버스트 전수 결과 (실제 출력, 데이터 워드 4개: 0x00000000, 0xFFFFFFFF, 0xDEADBEEF, 0x12345678):

```text
burst L : tested(4 words)  sequential-fail  interleaved-fail
   1    :    224                0 (  0.0%)        0 (  0.0%)
   2    :    220              192 ( 87.3%)        0 (  0.0%)
   3    :    432              376 ( 87.0%)        0 (  0.0%)
   4    :    848              764 ( 90.1%)        0 (  0.0%)
   5    :   1664             1552 ( 93.3%)        0 (  0.0%)
   6    :   3264             3124 ( 95.7%)        0 (  0.0%)
   7    :   6400             6232 ( 97.4%)        0 (  0.0%)
   8    :  12544            12348 ( 98.4%)        0 (  0.0%)
   9    :  24576            24216 ( 98.5%)    24576 (100.0%)
  10    :  48128            47604 ( 98.9%)    36096 ( 75.0%)
```

손으로 맞춰 보기:

- **인터리브, L ≤ 8 → 0.** 물리 비트 b 의 코드워드는 b mod 8. 연속 L ≤ 8 비트는 mod 8 이 모두 달라 코드워드마다 최대 1비트 → 전부 단일 오류.
- **인터리브, L = 9 → 100 %.** 첫 비트 s 와 끝 비트 s + 8 은 반드시 뒤집히고 둘 다 코드워드 s mod 8 (위치는 이웃한 두 개) → 그 코드워드에 2비트 → 오정정.
- **인터리브, L = 10 → 75 %.** 끝 비트 s + 9 는 코드워드 (s+1) mod 8 의 두 번째 비트. 실패 ⇔ (s + 8 이 뒤집힘) 또는 (s + 1 이 뒤집힘). 사이 비트는 동전 던지기라 1 − (1/2)(1/2) = 3/4.
- **순차, L = 2 → 48/55 per word.** b 와 b + 1 이 같은 코드워드 ⇔ b mod 7 ≠ 6. 시작 55 곳 중 경계(b = 6, 13, …, 48) 7 곳만 산다 → 4 워드 × 48 = 192 (assert).

| 배치 | 저장 비용 | 단일 오류 | 길이 ≤ 8 버스트 1개 | 메모: 실제 예 (대표) |
|---|---|---|---|---|
| 순차 | 56 비트 / 32 데이터 (75 % 추가) | 전부 정정 | 대부분 실패 (87~98 %) | — |
| 인터리브 | 같음 | 전부 정정 | **전부 정정** | SRAM column interleaving, DRAM 칩별 비트 분산 |
| SECDED(39,32) 참고 | 39 / 32 (22 %) | 정정 | 2비트 검출, 그 이상 보장 없음 | MCU TCM/SRAM ECC (MEM-B07 의 축소판) |

**펌웨어 관점.**

- 하드웨어 ECC 블록은 이 일을 배선으로 한다. 펌웨어가 할 일은 (1) bring-up 때 오류 주입으로 정정/보고가 스펙대로인지 확인(SIV-M14), (2) CE 를 세어 임계값을 넘으면 그 영역을 은퇴(retire)시키거나 스크럽(scrub)하는 것(MEM-A06), (3) 소프트웨어 ECC 가 필요한 곳(작은 설정 블록, OTP 에뮬레이션)에선 이 레슨처럼 **배치를 먼저 정하는 것**이다.
- Don 의 SSD 경험과 연결: 예전 SLC NAND 는 256/512 바이트마다 Hamming 계열 1비트 정정 ECC(256 B 당 22 비트를 3 바이트 OOB 에 — SmartMedia 시절 대표 예, 데이터시트 확인)를 썼고, MLC 이후 오류율이 올라가며 BCH → LDPC 로 갔다. 원리(검사 행렬 → 신드롬 → 오류 위치)는 같고, 오류 위치를 푸는 방법만 복잡해진다.
- 코드 크기·RAM: 비트 연산 디코더는 상태 없음, 마스크 3 개. 표 디코더(연습 2)는 128 B. 32비트 워드 블록 디코드는 코드워드 8 개 × (7 비트 모으기 + 디코드) — O(56) 비트 연산.

### 복잡도와 실행 시간

| 함수 | 시간 | 메모리 |
|---|---|---|
| `parity8` / `h74_syndrome` / `h74_encode` / `h74_decode` | O(1) (접기 3단계 × 마스크 3개) | 없음 |
| `h74_classify_all` | 16 × 128 = 2048 디코드 | 8 × 4 카운터 |
| `burst_fail_count(L)` | (57 − L) × 2^(L−2) 블록 디코드 | 없음 |

`solution` 전체 실행 약 0.14 초 (실측 `time`, 컴파일된 -O2 바이너리, 공유 머신 load avg 약 29).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 41/41 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 41/41, 오류 없음
```

실제 출력 (`solution`):

```text
even parity (4+1): detected=256 missed=240 of 496 error cases
codewords d->cw: 0:00 1:0E 2:32 3:3C 4:54 5:5A 6:66 7:68 8:96 9:98 A:A4 B:AA C:C2 D:CC E:F0 F:FE
single errors: corrected 112/112, syndrome==position 112/112
weight  patterns  ok  corrected  miscorrected  undetected   (16 data each)
  0       16     16      0          0             0
  1      112      0    112          0             0
  2      336      0      0        336             0
  3      560      0      0        448           112
  4      560      0      0        448           112
  5      336      0      0        336             0
  6      112      0      0        112             0
  7       16      0      0          0            16
double errors: 336 cases, syndrome = i^j and final error weight 3: 336; wrong data bits 0..4: 0 144 144 48 0
trace: rx=0x8E syndrome=7 -> cw=0x0E data=0x1 (sent 0xB)
burst L : tested(4 words)  sequential-fail  interleaved-fail
   1    :    224                0 (  0.0%)        0 (  0.0%)
   2    :    220              192 ( 87.3%)        0 (  0.0%)
   3    :    432              376 ( 87.0%)        0 (  0.0%)
   4    :    848              764 ( 90.1%)        0 (  0.0%)
   5    :   1664             1552 ( 93.3%)        0 (  0.0%)
   6    :   3264             3124 ( 95.7%)        0 (  0.0%)
   7    :   6400             6232 ( 97.4%)        0 (  0.0%)
   8    :  12544            12348 ( 98.4%)        0 (  0.0%)
   9    :  24576            24216 ( 98.5%)    24576 (100.0%)
  10    :  48128            47604 ( 98.9%)    36096 ( 75.0%)
41/41 passed
```

## 7. 자주 하는 실수

**1. 신드롬이 위치가 되지 않는 배치.** 검사 비트를 끝에 몰아(`d1 d2 d3 d4 p1 p2 p4`, 체계적(systematic) 배치) 놓으면 부호는 똑같이 좋지만 신드롬 값 ≠ 위치다. 그 경우엔 "신드롬 → 비트" 표가 필요하다 (MEM-B07 이 72/64 에서 만드는 표). 고전 배치를 쓸 거면 H 의 열 i 가 정확히 i 가 되도록 마스크를 맞춘다.

```c
/* 틀림: 마스크를 "bit i-1 = 위치 i" 로 만들고 저장은 "bit i = 위치 i" 로 함 → 신드롬이 한 칸 어긋남 */
#define H74_MASK_P1 0x55u
/* 맞음: 저장 규칙과 같은 규칙으로 — bit i = 위치 i → 위치 1,3,5,7 = 0xAA */
#define H74_MASK_P1 0xAAu
```

**2. 2비트 오류도 "검출은 된다"고 생각한다.** 정정 디코더를 쓰면 2비트 오류는 신드롬 ≠ 0 → "1비트 정정"으로 보고되고 데이터는 틀린다 (336/336). Hamming(7,4)로 2비트를 잡고 싶으면 **정정을 끄고** 검출 전용(신드롬 ≠ 0 → 오류)으로 써야 한다. 둘 다 원하면 SECDED.

**3. 오류를 예약 비트 0 에 넣는다.** e 를 0..255 로 돌리면 bit 0 오류는 디코더가 버리는 비트라 "정정됨"으로 세어지고 표가 2배로 부풀어 맞지 않는다. 패턴은 `x << 1` (x = 0..127). 디코더 쪽도 `rx & 0xFE` 로 bit 0 을 무시한다 (연습 1 에서는 bit 0 이 p0 라 다르게 다룬다).

**4. 시프트 우선순위.** `cw >> 4 & 2` 는 `(cw >> 4) & 2` 로 묶이지만(시프트가 & 보다 우선), `1u << s | x` 같은 식에 `+` 를 섞으면 바로 틀린다 (`+` 가 `<<` 보다 우선). 헷갈리면 괄호. `uint8_t` 에 `~` 나 `<<` 를 하면 int 로 승격된다 — 결과를 `(uint8_t)` 로 잘라야 `-Wconversion` 계열 경고와 상위 비트 쓰레기를 피한다.

**5. 버스트를 "L 비트 전부 랜덤"으로 만든다.** 양 끝이 0 이면 실제 길이가 L 보다 짧다. "인터리브 L = 9 는 100 % 실패" 같은 깔끔한 결론은 첫·끝 비트를 반드시 뒤집을 때만 나온다 (COMM-B08 과 같은 정의).

**6. 56 비트에 `1u << b`.** b ≥ 32 에서 UB. `uint64_t` 는 `1ull << b`. UBSan 이 바로 잡는다 (`-fsanitize=undefined`).

**7. 정정 카운터를 "오류 없음의 증거"로 쓴다.** CE 가 조금씩 오르는 건 정상일 수 있지만, Part B 에서 본 대로 2비트 오류도 CE 1건으로 보고된다. CE 가 같은 주소에서 반복되면 하드 오류(고장 셀)일 가능성이 높고, 그 주소에 두 번째 오류가 오면 Hamming(7,4)은 조용히 틀린다. 스크럽·은퇴 정책은 MEM-A06.

## 8. 면접 Q&A

<details><summary>Q1. 신드롬이 왜 오류 위치가 되나요? 증명해 보세요.</summary>

H 를 "열 i = i 의 2진수"인 3 × 7 행렬로 정한다. 코드워드 c 는 H·c = 0 (GF(2)). 받은 r = c ⊕ e 이면 H·r = H·c ⊕ H·e = H·e. 단일 오류 e = e_i 면 H·e_i = H 의 열 i = i. 0 이 아닌 3비트 값이 7 개, 위치도 7 개라 1:1 이다. 꼬리: "데이터와 무관한가?" → 그렇다, H·c = 0 이라 신드롬은 오류만의 함수다. 이 선형성 덕에 하드웨어 오류 주입 검증에서 기대 신드롬을 오류 패턴만으로 계산할 수 있다 (SIV-M14).

</details>

<details><summary>Q2. Hamming(7,4)는 2비트 오류를 왜 못 잡죠? 검출이라도 안 되나요?</summary>

최소 거리 d = 3. 거리 3 부호는 "1비트 정정" 또는 "2비트 검출" 중 하나를 고른다. 정정 디코더에서 2비트 오류 (i, j) 의 신드롬 i ⊕ j 는 세 번째 위치 k 를 가리키고, k 를 뒤집으면 {i, j, k} 의 XOR 이 0 → 무게 3 코드워드 = 다른 데이터. 전수로 336/336 오정정, 데이터 1~3비트 틀림. 검출 전용으로 쓰면(신드롬 ≠ 0 → 오류 보고, 정정 안 함) 2비트도 전부 잡는다 (i ⊕ j ≠ 0). 꼬리: "Hamming(7,4)는 완전 부호라서" — 모든 7비트 워드가 어떤 코드워드의 반경 1 안에 있어 "모르겠다"가 나올 자리가 없다.

</details>

<details><summary>Q3. SECDED 는 비트 하나를 더해서 뭘 얻나요? 디코딩 규칙은?</summary>

전체 패리티 p0 를 더해 d = 4 로 만든다 (모든 코드워드 무게가 짝수 → 무게 3 이 사라짐). 디코드: 신드롬 s 와 전체 패리티 P 를 본다. (s = 0, P = 0) 오류 없음 / (P = 1) 홀수 개 → 단일 오류로 보고 s 위치 정정 (s = 0 이면 p0 자체) / (s ≠ 0, P = 0) 짝수 개 → 2비트, 정정 불가(UE). 연습 1 전수: 단일 128/128 정정, 2비트 448/448 검출, 오정정 0. 꼬리 "3비트는?" → P = 1 이라 단일로 착각, 896/896 오정정. SECDED 는 3비트에 아무것도 약속하지 않는다.

</details>

<details><summary>Q4. DRAM ECC 는 왜 72/64 인가요?</summary>

단일 정정에 필요한 검사 비트 r 은 해밍 한계 2^r ≥ k + r + 1. k = 64 → r = 7 (128 ≥ 72; r = 6 이면 64 < 71). Hamming(127,120) 을 줄인(shortened) (71,64) 에 전체 패리티 1 비트 → (72,64) SECDED. 72 = 64 + 8 이라 x8 칩 하나를 더 붙인 9 칩(또는 x4 18 칩) DIMM 이 된다. 오버헤드 12.5 %. 실제 컨트롤러는 Hsiao 코드(모든 열이 홀수 무게, 별도 p0 없이 SECDED, 게이트 깊이 균형)를 많이 쓴다 (대표, 벤더 문서 확인). 구현은 MEM-B07. 꼬리 "DDR5 는?" → 서브채널 32 비트에 ECC 8 비트(40 비트 채널, 대표)와 칩 내부 on-die ECC 가 따로 있다 — MEM-B04 · MEM-A05.

</details>

<details><summary>Q5. 그럼 패리티만 쓰는 곳은 왜 있나요?</summary>

정정할 필요가 없고 "다시 가져오면 되는" 데이터에서는 검출만으로 충분하다. 예: 깨끗한(clean) 캐시 라인은 패리티 오류 시 무효화하고 아래 계층에서 다시 읽으면 된다 — 일부 코어의 L1 I-cache 가 패리티만 두는 이유 (코어마다 다름, TRM 확인). 버스 패리티(DDR4 C/A 패리티 등, 대표)는 전송 오류를 잡아 재시도시키는 용도. 대신 짝수 개 오류는 전부 놓친다 — 이웃한 데이터 라인 두 개가 같이 뒤집히는 경우(연습 5: DQ3+DQ4 256/256 놓침). 더티(dirty) 데이터처럼 다시 가져올 곳이 없는 데이터는 정정 코드가 필요하다.

</details>

<details><summary>Q6. 인터리빙은 왜 하나요? 비용은?</summary>

ECC 의 보장은 "코드워드당 몇 비트"다. 물리적 오류는 이웃 셀에 몰린다(SRAM MBU, 칩 하나 고장, NAND 의 워드라인 단위 오류). 한 코드워드의 비트를 물리적으로 흩어 놓으면 버스트 하나가 여러 코드워드에 1비트씩 나뉘어 각자 정정된다. 실측: 같은 56 비트에서 순차 배치는 길이 2 버스트의 87 % 를 망치지만, 인터리브는 길이 ≤ 8 버스트를 전부 정정. 비용은 저장 비트가 아니라 배선/레이아웃(그리고 소프트웨어면 비트 모으기 연산). 한계: 길이 9 이상(인터리브 깊이 초과)이면 다시 실패 — 깊이는 예상 버스트 길이로 정한다. DRAM 에서 "칩 하나 전체"를 견디는 건 심볼 단위 부호(chipkill, MEM-A05)의 일이다.

</details>

<details><summary>Q7. 비트 오류율 p 에서 Hamming(7,4) 블록이 틀릴 확률은? 부호가 손해인 경우도 있나요?</summary>

완전 부호라 실패 ⇔ 오류 ≥ 2 비트: P_fail = 1 − (1−p)^7 − 7p(1−p)^6 ≈ 21p². p = 1e-3 이면 2.093e-5, 부호 없는 니블(1 − (1−p)^4 = 3.994e-3)보다 191 배 낮다 (연습 4 실측). q = 1 − p 로 두면 "부호가 나음" ⇔ 6q³ − 7q² + 1 < 0 = (q−1)(3q+1)(2q−1) < 0 ⇔ 1/2 < q < 1, 즉 0 < p < 1/2 전 구간에서 블록 단위로는 항상 낫고 p = 1/2 에서 같다 (둘 다 0.9375). 단 이 비교는 같은 p 를 가정했다 — 실제 링크에서는 7 비트를 보내느라 비트당 에너지가 줄어 p 가 올라간다 (BER/BLER 곡선은 RF-M01). 꼬리: 닫힌 식을 double 로 그대로 계산하면 p = 1e-6 에서 상대오차 8.9e-6 — 1 − (거의 1) 의 자릿수 손실. 무게별 합(21p²(1−p)^5 + …)으로 계산하라.

</details>

<details><summary>Q8. 메모리 ECC 를 bring-up 할 때 이 레슨의 무엇을 쓰나요?</summary>

(1) 참조 모델 — 인코더/신드롬을 C 로 두고 하드웨어 출력과 대조. (2) 오류 주입 매트릭스 — 데이터 × 단일 위치 전수 → 전부 CE + 정정, 2비트 → (SECDED 면) UE, 3비트는 PASS/FAIL 이 아니라 분포만 기록 (SIV-M14). (3) 체크 비트 자체의 오류도 주입한다 — 위치 1, 2, 4 오류도 신드롬이 위치를 가리켜야 하고 데이터는 그대로여야 한다. (4) 물리 배치 확인 — 인접 셀 2 비트 주입이 서로 다른 코드워드로 가는지(인터리브가 설계대로인지). 그리고 CE 카운터와 주소 캡처 레지스터가 실제 주입 위치와 맞는지.

</details>

## 9. 추가 연습

### 연습 1 — 확장 Hamming(8,4) SECDED

bit 0 에 p0 = (위치 1..7 의 패리티)를 넣어 8 비트 전체를 짝수로 만든다. 디코더는 `S_OK / S_CE / S_DED` 를 돌려준다. 16 데이터 × 255 개 오류 패턴을 전수로 돌려 무게별 결과를 세라.

<details><summary>답 보기</summary>

규칙: (s = 0, P = 0) → OK; P = 1 → 단일 오류, s 위치를 뒤집음 (s = 0 이면 bit 0 = p0); (s ≠ 0, P = 0) → DED. 실측:

```text
ex1 H(8,4): single 128/128 corrected, double 448/448 DED (miscorrect 0), triple 896/896 miscorrected, quad 224 undetected + 896 DED of 1120
```

d = 4: 무게 4 코드워드 14 개 → 무게 4 오류 중 14 × 16 = 224 는 조용한 오염, 나머지 56 × 16 은 DED. 3비트는 P = 1 이라 단일로 착각 → 전부 오정정. 이것이 72/64 (MEM-B07) 의 축소판이다.

</details>

### 연습 2 — 128 바이트 표 디코더

7비트 수신 워드(cw >> 1) → 데이터 표 `g_lut[128]` 를 init 에 만들어라. 표를 어떻게 채우면 빈칸·중복이 없나? 비트 연산 디코더와 256 입력 전부 같은지 확인하라.

<details><summary>답 보기</summary>

데이터 d 마다 코드워드 자신 + 이웃 7 개(위치 1..7 하나씩 뒤집음)에 d 를 쓴다. 완전 부호라 16 × 8 = 128 칸이 정확히 한 번씩 채워진다. 실측 `ex2 LUT: 128 bytes, matches bitwise decoder on all 256 inputs: yes`. 펌웨어 트레이드오프: 표 128 B(플래시) + 메모리 접근 1 번 vs 마스크 3 개 · 접기 9 번. 작은 MCU 에선 둘 다 싸고, 하드웨어에선 XOR 트리가 표보다 작다.

</details>

### 연습 3 — 일반 위치형 Hamming(15,11)

위치 1..15, 검사 비트는 1, 2, 4, 8. 신드롬을 "1 인 비트들의 위치 번호 XOR" 로 정의하고 인코더/디코더를 짜라. 2048 데이터 × 15 단일 오류 전수, 최소 무게, 그리고 (7,4) 의 마스크 신드롬과 XOR 공식이 같은지.

<details><summary>답 보기</summary>

```text
ex3 H(15,11): 30720 single errors corrected=all, d_min=3, (7,4) mask syndrome == XOR of positions: yes
```

r 비트 검사로 2^r − 1 위치를 가리킬 수 있으니 Hamming(2^r − 1, 2^r − 1 − r) 가 일반형이다: (7,4), (15,11), (31,26), (63,57), (127,120). 64 데이터는 (127,120) 을 줄여 (71,64) + p0 = (72,64). 효율 4/7 = 57 % → 11/15 = 73 % → 64/72 = 89 %: 길어질수록 싸지만 한 코드워드에 2 오류가 들어갈 확률도 커진다.

</details>

### 연습 4 — 디코드 실패 확률과 교차점

비트 오류가 독립이고 확률 p 일 때 P_fail(7,4) 를 (a) 닫힌 식 1 − (1−p)^7 − 7p(1−p)^6, (b) 오류 패턴 128 개를 실제 디코드해서 무게별 확률 합으로 구하고, 부호 없는 니블(1 − (1−p)^4)과 비교하라. 부호가 손해가 되는 p 가 있나?

<details><summary>답 보기</summary>

실측:

```text
ex4 p        raw nibble    H(7,4) fail   ratio      closed-form rel.err
    1e-06    4.0000e-06    2.1000e-11    190476.5 x 8.9e-06
    1e-04    3.9994e-04    2.0993e-07    1905.1   x 3.4e-10
    1e-03    3.9940e-03    2.0930e-05    190.8    x 2.2e-12
    1e-02    3.9404e-02    2.0310e-03    19.4     x 3.9e-14
    1e-01    3.4390e-01    1.4969e-01    2.3      x 1.1e-15
    coded < raw for all p = 0.001..0.499: yes; at p = 0.5 both = 0.9375 (diff 0.0e+00)
```

작은 p 에서 P_fail ≈ C(7,2)p² = 21p², 비율 ≈ 4p / 21p² = 4/(21p). 대수: (1−p)^4 로 나누면 (q³ + 7pq² ≥ 1) ⇔ 6q³ − 7q² + 1 ≤ 0 = (q−1)(3q+1)(2q−1) ≤ 0 → p ≤ 1/2 에서 항상 부호가 낫거나 같다. 닫힌 식의 상대 오차가 p = 1e-6 에서 8.9e-6 까지 커지는 것은 1 − (거의 1) 의 상쇄(cancellation) 때문 — 그래서 assert 는 무게별 합을 기준으로 했다.

</details>

### 연습 5 — 32비트 패리티 세 가지와 버스 패리티의 맹점

(a) XOR 접기 5단계, (b) 16 → 4비트로 접은 뒤 상수 0x6996 을 4비트 표로 쓰기, (c) `__builtin_parity` 가 100,007 워드에서 일치하는지. 그리고 바이트 패리티가 데이터 라인 DQ3 · DQ4 동시 반전을 몇 번 놓치는지.

<details><summary>답 보기</summary>

`0x6996 = 0110 1001 1001 0110b` 의 bit n = parity(n) (n = 0..15) — 니블 패리티 16 개를 상수 하나에 담은 표. 실측 `ex5 parity32 3 ways agree on 100007 words: yes; byte parity misses DQ3+DQ4 double flip 256/256`. 2비트 동시 반전은 무게 2 → 패리티 불변 → 256 값 전부 놓친다. 이웃 라인 short · 크로스토크는 짝수 개로 오기 쉬워 패리티만으로는 부족하다 (데이터 라인 short 진단은 MEM-B08 의 walking-1).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 16/16 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 16/16, 오류 없음
```

## 10. 핵심 정리

- 패리티 = 모든 비트의 XOR (접기 log2 n 단계). 검출 ⇔ 오류 무게가 홀수. 정정 불가. 실측 5비트: 홀수 16 패턴 검출, 짝수 15 패턴 놓침.
- Hamming(7,4): 위치 1..7 = p1 p2 d1 p4 d2 d3 d4, H 의 열 i = i 의 2진수 → **신드롬 = H·e = 오류 위치**. 마스크 0xAA / 0xCC / 0xF0 (bit i = 위치 i). 인코딩 예 0xB → 0xAA.
- 신드롬은 데이터와 무관(선형). 공식 하나로: 신드롬 = XOR(1 인 비트들의 위치 번호).
- d = 3, 가중치 분포 1 + 7z³ + 7z⁴ + z⁷, 완전 부호(16 × 8 = 128). 단일 112/112 정정, **2비트 336/336 오정정**(신드롬 i ⊕ j 가 세 번째 위치를 가리킴), 무게 ≥ 2 는 하나도 못 살린다.
- 정정 디코더의 CE 1건은 실제로 2비트 오류일 수 있다 → SECDED(p0 하나 추가, d = 4, 72/64 = MEM-B07).
- 인터리빙: 같은 56 비트, 배선만 바꿔 길이 ≤ 8 버스트 전부 정정 (순차는 L = 2 에서 87 % 실패). 길이 9 = 깊이 초과 → 100 % 실패.
- P_fail ≈ 21p² (p = 1e-3 → 2.09e-5, 니블 대비 191 배). 작은 확률은 닫힌 식 대신 무게별 합으로 계산.

## 참고자료

- R. W. Hamming, "Error Detecting and Error Correcting Codes", Bell System Technical Journal 29(2), 1950 — 위치 배치(검사 비트를 2의 거듭제곱 위치에)와 SEC/SECDED 의 원 논문.
- M. Y. Hsiao, "A Class of Optimal Minimum Odd-weight-column SEC-DED Codes", IBM Journal of R&D 14(4), 1970 — 실제 메모리 컨트롤러가 쓰는 SECDED 형태 (MEM-B07 에서 다룸).
- S. Lin, D. J. Costello, *Error Control Coding* (2nd ed.) — 선형 블록 부호, 해밍 한계, 완전 부호, 가중치 분포.
- F. J. MacWilliams, N. J. A. Sloane, *The Theory of Error-Correcting Codes* — Hamming 부호의 가중치 분포와 완전 부호의 분류.
- Linux kernel EDAC 문서 (Documentation/driver-api/edac.rst, admin-guide/ras) — CE/UE 카운터가 펌웨어/OS 에 보이는 방식: <https://docs.kernel.org/driver-api/edac.html>
- 관련 레슨: C-B01 (비트 연산), COMM-B08 (CRC/체크섬 검출과 HD, 같은 버스트 정의), SIV-M14 (ECC 오류 주입과 CE/UE 보고), MEM-B07 (SECDED 72/64), MEM-B08 (데이터 라인 short 진단), MEM-A05 (chipkill · Reed-Solomon), MEM-A06 (patrol scrub · page retire), MEM-B12 · MEM-M13 (NAND 와 오류 처리), RF-M01 (BER/BLER)
