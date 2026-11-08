---
id: DS-B09
time: 50분
updated: 2026-11-07
---
# DS-B09 해시 함수 기초: FNV-1a, djb2, 분포 측정

## 학습 목표

- **FNV-1a**, **djb2**, **단순 합(sum)** 해시를 외워서 쓰고, 공식 참조값으로 구현을 검증할 수 있다.
- 해시 품질을 **카이제곱(chi-square)** 과 **충돌 수(collision count)** 로 *측정* 하고, 숫자를 읽어 "좋다/나쁘다/수상하다"를 판단한다.
- **32비트 해시 충돌**(해시 값 자체가 같음)과 **버킷 충돌**(`h % m` 이 같음)을 구별한다.
- 버킷 수(2의 거듭제곱 vs 소수)와 키 모양(문자열 vs 순차 정수 vs 정렬된 LBA)이 분포에 어떻게 영향을 주는지 실험 결과로 설명한다.
- Cortex-M에서 해시를 고를 때의 비용(나눗셈, 곱셈, `char` 부호)을 이야기할 수 있다.

## 1. 문제

해시 테이블의 성능은 **해시 함수가 키를 얼마나 고르게 흩뿌리느냐**로 결정된다. 펌웨어에서도 CLI 명령 디스패치, 센서 이름 → 드라이버 레지스트리, FTL 캐시(LBA → 슬롯) 조회에 해시가 들어간다. 면접에서 "좋은 해시 함수란?"에 "충돌이 적은 거요"로 끝내면 약하다 — **어떻게 재는지**까지 말할 수 있어야 한다.

해시 테이블에서 키가 버킷에 들어가는 길은 두 단계다. ① 키 바이트 → 32비트 해시 값 `h` ② `h` → 버킷 번호 `h % m`. 충돌도 두 종류다.

```svg
<svg viewBox="0 0 720 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">키 → 32비트 해시 → 버킷 (m = 64)</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="45" width="120" height="30" rx="4"/><rect x="10" y="95" width="120" height="30" rx="4"/><rect x="10" y="145" width="120" height="30" rx="4"/>
    <rect x="180" y="80" width="110" height="60" rx="8"/>
    <rect x="340" y="45" width="140" height="30"/><rect x="340" y="95" width="140" height="30"/><rect x="340" y="145" width="140" height="30"/>
  </g>
  <rect x="180" y="80" width="110" height="60" rx="8" class="f-acc"/>
  <rect x="340" y="95" width="140" height="30" class="f-warn"/><rect x="340" y="145" width="140" height="30" class="f-warn"/>
  <g fill="currentColor" font-family="monospace">
    <text x="22" y="65">"imu0_x"</text><text x="22" y="115">"imu0_z"</text><text x="22" y="165">"imu1_y"</text>
    <text x="350" y="65">h = 594</text><text x="350" y="115">h = 596</text><text x="350" y="165">h = 596</text>
  </g>
  <text x="200" y="107" fill="currentColor">sum32(key)</text><text x="200" y="127" fill="currentColor" font-size="12">예: 단순 합 해시</text>
  <g stroke="currentColor" marker-end="url(#a9)"><line x1="130" y1="60" x2="178" y2="95"/><line x1="130" y1="110" x2="178" y2="110"/><line x1="130" y1="160" x2="178" y2="125"/>
    <line x1="290" y1="100" x2="338" y2="62"/><line x1="290" y1="110" x2="338" y2="110"/><line x1="290" y1="120" x2="338" y2="158"/>
    <line x1="480" y1="60" x2="545" y2="60"/><line x1="480" y1="110" x2="545" y2="135"/><line x1="480" y1="160" x2="545" y2="140"/></g>
  <g stroke="currentColor" fill="none"><rect x="550" y="45" width="150" height="30"/><rect x="550" y="122" width="150" height="30"/></g>
  <g fill="currentColor"><text x="560" y="65">594 % 64 = 18</text><text x="560" y="142">596 % 64 = 20</text></g>
  <text x="10" y="205" fill="currentColor">해시 충돌(h-coll): 32비트 값 자체가 같다 — 버킷을 늘려도 못 뗀다.</text>
  <text x="30" y="225" fill="currentColor">예) 단순 합: "imu0_z" = "imu1_y" = 596</text>
  <text x="10" y="245" fill="currentColor">버킷 충돌(b-coll): h는 다르고 h % m 만 같다 — 256개 → 64칸이면 최소 192.</text>
  <text x="10" y="268" fill="currentColor">좋은 해시 = h-coll ≈ 0, 버킷별 개수는 "무작위로 던진 것"과 같은 수준.</text>
</svg>
```

### Part A — 해시 함수 3개

바이트 배열을 받는 형태로 구현한다 (문자열뿐 아니라 정수·구조체 키도 해시하려고).

- `uint32_t fnv1a32(const void *data, size_t len)` — offset basis `0x811C9DC5`, prime `0x01000193`. 바이트마다 **XOR 먼저, 곱하기 나중**.
- `uint32_t djb2(const void *data, size_t len)` — 시작값 `5381`, 바이트마다 `h = h * 33 + c`.
- `uint32_t sum32(const void *data, size_t len)` — 바이트를 그냥 더한다 (나쁜 해시의 기준선).

### Part B — 품질 지표 2개

- `double chi_square(const uint32_t *counts, unsigned m, unsigned n)` — 버킷 `m` 개에 키 `n` 개를 넣었을 때 `Σ (O_i − E)² / E`, `E = n/m`.
- `unsigned count_hash_collisions(const uint32_t *h, unsigned n)` — 32비트 해시 값이 겹친 키 수 = `n − (서로 다른 값의 개수)`. malloc 없이 정적 버퍼에 복사해서 정렬한 뒤 이웃끼리 비교한다.

### Part C — 측정 실험과 해석

하네스가 키 집합 4개(각 256개)를 만들고, 해시 3개 × 버킷 수 3가지(64, 61, 1024)로 표를 찍는다.

| 키 집합 | 예 | 성격 |
|---|---|---|
| `sensor` | `imu0_x`, `baro3_avg` | 짧은 ASCII, 글자 몇 개만 다름 |
| `command` | `uart_get`, `flash_save` | 명사_동사 조합 |
| `seq-int` | LBA 0..255 (4바이트 LE) | 순차 정수 |
| `stride8` | LBA 0, 8, 16, … (4KiB 정렬) | SSD/FTL에서 흔한 정렬 키 |

표를 보고 다음에 답한다: ① 어떤 해시가 어떤 키 집합에서 무너지나? ② chi² 가 0 이면 좋은 건가? ③ 소수 버킷 수는 언제나 낫나?

## 2. Problem (English)

> **Hash function quality.** Implement three 32-bit hash functions over byte arrays and evaluate them empirically.
>
> 1. `uint32_t fnv1a32(const void *data, size_t len)` (FNV-1a, offset basis `2166136261`, prime `16777619`), `uint32_t djb2(const void *data, size_t len)` (`h = 5381; h = h*33 + c`), and `uint32_t sum32(const void *data, size_t len)` (byte sum).
> 2. `double chi_square(const uint32_t *counts, unsigned m, unsigned n)` returning `sum((O_i - n/m)^2 / (n/m))` over `m` buckets.
> 3. `unsigned count_hash_collisions(const uint32_t *h, unsigned n)` returning `n - (number of distinct values)`, without dynamic allocation.
> 4. For four key sets of 256 keys each (sensor names, command names, sequential integers, stride-8 integers), report chi-square, max bucket load, empty buckets, bucket collisions and full 32-bit hash collisions for `m = 64, 61, 1024`. Interpret the results.
>
> Reference values: `fnv1a32("") = 0x811C9DC5`, `fnv1a32("a") = 0xE40C292C`, `fnv1a32("foobar") = 0xBF9CF968`, `djb2("a") = 177670`, `djb2("foobar") = 0xFDE460BE`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 해시가 전부 0을 돌려줘서 FAIL 12줄, 표는 엉망 — "6/18 passed"
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

## 4. 힌트

<details>
<summary>힌트 1 — 바이트는 반드시 uint8_t 로 읽는다</summary>

`const uint8_t *p = (const uint8_t *)data;`. `char` 로 읽으면 x86(부호 있는 `char`)에서 `0x80` 이상 바이트가 음수 → 부호 확장되어 ARM(부호 없는 `char`)과 해시 값이 달라진다. 같은 펌웨어 이미지와 PC 툴이 서로 다른 해시를 계산하는 실제 버그다.

</details>

<details>
<summary>힌트 2 — 랩어라운드는 uint32_t 에 맡긴다</summary>

`h *= 16777619u;` 는 unsigned 곱이라 2^32 로 나눈 나머지가 자동으로 남는다 (UB 아님). `int h` 로 쓰면 signed overflow = UB. djb2 의 `h * 33` 은 `(h << 5) + h` 로 써도 같다.

</details>

<details>
<summary>힌트 3 — 카이제곱은 double 로</summary>

`E = n / m` 을 정수로 나누면 m = 61 일 때 4.2 가 4 가 되어 값이 틀어진다. `double e = (double)n / m;`. 버킷마다 `d = O - E; chi += d*d/e;`.

</details>

<details>
<summary>힌트 4 — 중복 세기는 "정렬 후 이웃 비교"</summary>

해시 집합(set) 없이 중복을 세는 가장 쉬운 방법: 정렬하면 같은 값이 붙어 있다. `a[i] == a[i-1]` 인 i 의 개수가 곧 `n − distinct`. n = 256 이면 삽입 정렬로 충분하다 (malloc 없는 `qsort` 도 OK).

</details>

## 5. 풀이

### Part A — 세 해시의 구조

| 해시 | 한 바이트 처리 | 비용 (Cortex-M4) | 특징 |
|---|---|---|---|
| FNV-1a | `h ^= c; h *= 0x01000193;` | XOR + MUL (1 사이클 곱셈기) | 단순, 분포 좋음, 공식 테스트 벡터 있음 |
| djb2 | `h = (h << 5) + h + c;` | 시프트 + 덧셈 2개 (곱셈기 없는 칩에도 좋음) | 짧은 ASCII엔 괜찮음, 섞임(avalanche) 약함 |
| sum | `h += c;` | 덧셈 1개 | **순서를 못 봄** → 글자 바꿔치기가 전부 충돌 |

손으로 한 번 따라가 본다 — `fnv1a32("a")`:

```text
h = 0x811C9DC5            (offset basis)
h ^= 0x61  → 0x811C9DA4   ('a')
h *= 0x01000193 → 0xE40C292C   ← 테스트 벡터와 일치
```

`0x01000193 = 2^24 + 2^8 + 0x93` 이라 곱셈은 "몇 번 시프트해서 더하기"와 같다. FNV-**1** 은 곱하기를 먼저, XOR 을 나중에 해서 *마지막 바이트*가 곱셈을 거치지 않는다 → 마지막 글자만 다른 키(`imu0_x`/`imu0_y`)가 하위 비트만 다르게 나온다. FNV-**1a** 는 순서를 바꿔 모든 바이트가 곱셈을 한 번은 거친다.

단순 합이 왜 망하는지: `"imu0_z"` 와 `"imu1_y"` 는 `'0'+'z' = 48+122 = 170`, `'1'+'y' = 49+121 = 170` 로 합이 같다. 실측으로 `imu0_z`, `imu1_y`, `imu2_x`, `gps1_z`, `gps2_y`, `gps3_x` 6개가 전부 596 이다. 센서·레지스터 이름처럼 "숫자 하나 + 접미사 하나" 가 바뀌는 키 집합은 합 해시의 최악의 상대다.

### Part B — 카이제곱을 어떻게 읽나

키를 버킷에 *무작위로* 던지면 각 버킷 개수는 평균 E 근처에서 흔들린다. 그 흔들림을 모두 모은 것이 `chi² = Σ (O−E)²/E` 이고, 무작위라면 **자유도 m−1 인 카이제곱 분포**를 따른다.

- 평균 ≈ **m − 1** (m = 64 → 63), 표준편차 ≈ **√(2(m−1))** (m = 64 → 11.2).
- 그래서 판단 기준: `m−1 ± 3σ` 안 (m = 64 → 약 30 ~ 97) 이면 "무작위처럼 고르다".
- **훨씬 크면** 특정 버킷에 몰린다 (나쁨). **훨씬 작으면 (0 근처)** 너무 규칙적이다 — 키에 구조가 있고 해시가 그 구조를 그대로 옮겼다는 뜻. 테이블엔 해가 없지만 "이 해시가 잘 섞는다"는 증거는 아니다.

```svg
<svg viewBox="0 0 720 350" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">센서 이름 256개 → 64 버킷: 버킷별 키 개수 (실측, 기대값 E = 4 는 점선)</text>
  <text x="10" y="42" fill="currentColor">FNV-1a: chi² = 64.5 (≈ 63) — 무작위처럼 들쭉날쭉, 치우침 없음</text>
  <line x1="40" y1="142" x2="684" y2="142" stroke="currentColor"/>
  <g class="f-ok">
    <rect x="42" y="118" width="8" height="24"/><rect x="52" y="126" width="8" height="16"/><rect x="62" y="118" width="8" height="24"/><rect x="72" y="78" width="8" height="64"/><rect x="82" y="86" width="8" height="56"/><rect x="92" y="94" width="8" height="48"/>
    <rect x="102" y="110" width="8" height="32"/><rect x="112" y="134" width="8" height="8"/><rect x="122" y="110" width="8" height="32"/><rect x="132" y="134" width="8" height="8"/><rect x="142" y="118" width="8" height="24"/><rect x="152" y="110" width="8" height="32"/>
    <rect x="162" y="94" width="8" height="48"/><rect x="172" y="110" width="8" height="32"/><rect x="182" y="110" width="8" height="32"/><rect x="192" y="126" width="8" height="16"/><rect x="202" y="110" width="8" height="32"/><rect x="212" y="118" width="8" height="24"/>
    <rect x="222" y="134" width="8" height="8"/><rect x="232" y="110" width="8" height="32"/><rect x="242" y="118" width="8" height="24"/><rect x="252" y="110" width="8" height="32"/><rect x="262" y="86" width="8" height="56"/><rect x="272" y="86" width="8" height="56"/>
    <rect x="282" y="134" width="8" height="8"/><rect x="302" y="94" width="8" height="48"/><rect x="312" y="126" width="8" height="16"/><rect x="322" y="118" width="8" height="24"/><rect x="332" y="118" width="8" height="24"/><rect x="342" y="86" width="8" height="56"/>
    <rect x="352" y="86" width="8" height="56"/><rect x="362" y="126" width="8" height="16"/><rect x="372" y="94" width="8" height="48"/><rect x="382" y="110" width="8" height="32"/><rect x="392" y="110" width="8" height="32"/><rect x="402" y="134" width="8" height="8"/>
    <rect x="412" y="118" width="8" height="24"/><rect x="422" y="118" width="8" height="24"/><rect x="432" y="110" width="8" height="32"/><rect x="442" y="78" width="8" height="64"/><rect x="452" y="94" width="8" height="48"/><rect x="462" y="102" width="8" height="40"/>
    <rect x="472" y="126" width="8" height="16"/><rect x="482" y="126" width="8" height="16"/><rect x="492" y="118" width="8" height="24"/><rect x="502" y="94" width="8" height="48"/><rect x="512" y="102" width="8" height="40"/><rect x="522" y="102" width="8" height="40"/>
    <rect x="532" y="86" width="8" height="56"/><rect x="542" y="94" width="8" height="48"/><rect x="552" y="110" width="8" height="32"/><rect x="562" y="102" width="8" height="40"/><rect x="572" y="118" width="8" height="24"/><rect x="592" y="110" width="8" height="32"/>
    <rect x="602" y="102" width="8" height="40"/><rect x="612" y="86" width="8" height="56"/><rect x="622" y="110" width="8" height="32"/><rect x="632" y="110" width="8" height="32"/><rect x="642" y="102" width="8" height="40"/><rect x="652" y="94" width="8" height="48"/>
    <rect x="662" y="134" width="8" height="8"/><rect x="672" y="126" width="8" height="16"/>
  </g>
  <line x1="40" y1="110" x2="684" y2="110" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="690" y="114" fill="currentColor" font-size="12">4</text>
  <text x="40" y="158" fill="currentColor" font-size="12">0</text><text x="664" y="158" fill="currentColor" font-size="12">63</text>
  <text x="10" y="182" fill="currentColor">단순 합: chi² = 89.5 — 양 끝(0, 62, 63)에 몰리고 가운데(28, 40~42)는 비었다</text>
  <line x1="40" y1="282" x2="684" y2="282" stroke="currentColor"/>
  <g class="f-warn">
    <rect x="42" y="194" width="8" height="88"/><rect x="52" y="218" width="8" height="64"/><rect x="62" y="242" width="8" height="40"/><rect x="72" y="234" width="8" height="48"/><rect x="82" y="234" width="8" height="48"/><rect x="92" y="242" width="8" height="40"/>
    <rect x="102" y="242" width="8" height="40"/><rect x="112" y="258" width="8" height="24"/><rect x="122" y="266" width="8" height="16"/><rect x="132" y="250" width="8" height="32"/><rect x="142" y="242" width="8" height="40"/><rect x="152" y="234" width="8" height="48"/>
    <rect x="162" y="242" width="8" height="40"/><rect x="172" y="250" width="8" height="32"/><rect x="182" y="250" width="8" height="32"/><rect x="192" y="250" width="8" height="32"/><rect x="202" y="250" width="8" height="32"/><rect x="212" y="250" width="8" height="32"/>
    <rect x="222" y="250" width="8" height="32"/><rect x="232" y="242" width="8" height="40"/><rect x="242" y="234" width="8" height="48"/><rect x="252" y="242" width="8" height="40"/><rect x="262" y="258" width="8" height="24"/><rect x="272" y="266" width="8" height="16"/>
    <rect x="282" y="266" width="8" height="16"/><rect x="292" y="266" width="8" height="16"/><rect x="302" y="266" width="8" height="16"/><rect x="312" y="274" width="8" height="8"/><rect x="332" y="274" width="8" height="8"/><rect x="342" y="266" width="8" height="16"/>
    <rect x="352" y="258" width="8" height="24"/><rect x="362" y="250" width="8" height="32"/><rect x="372" y="258" width="8" height="24"/><rect x="382" y="258" width="8" height="24"/><rect x="392" y="250" width="8" height="32"/><rect x="402" y="250" width="8" height="32"/>
    <rect x="412" y="250" width="8" height="32"/><rect x="422" y="258" width="8" height="24"/><rect x="432" y="274" width="8" height="8"/><rect x="472" y="274" width="8" height="8"/><rect x="482" y="266" width="8" height="16"/><rect x="492" y="258" width="8" height="24"/>
    <rect x="502" y="258" width="8" height="24"/><rect x="512" y="258" width="8" height="24"/><rect x="522" y="258" width="8" height="24"/><rect x="532" y="250" width="8" height="32"/><rect x="542" y="242" width="8" height="40"/><rect x="552" y="242" width="8" height="40"/>
    <rect x="562" y="242" width="8" height="40"/><rect x="572" y="250" width="8" height="32"/><rect x="582" y="258" width="8" height="24"/><rect x="592" y="258" width="8" height="24"/><rect x="602" y="250" width="8" height="32"/><rect x="612" y="242" width="8" height="40"/>
    <rect x="622" y="234" width="8" height="48"/><rect x="632" y="234" width="8" height="48"/><rect x="642" y="234" width="8" height="48"/><rect x="652" y="218" width="8" height="64"/><rect x="662" y="194" width="8" height="88"/><rect x="672" y="194" width="8" height="88"/>
  </g>
  <line x1="40" y1="250" x2="684" y2="250" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="690" y="254" fill="currentColor" font-size="12">4</text>
  <text x="40" y="298" fill="currentColor" font-size="12">0</text><text x="664" y="298" fill="currentColor" font-size="12">63</text>
  <text x="10" y="322" fill="currentColor">고른 분포 ≠ 모든 막대가 4. 무작위라면 막대가 0~8 정도로 흔들리는 게 정상이고,</text>
  <text x="10" y="342" fill="currentColor">그 흔들림의 크기가 바로 chi² ≈ m-1 이다. 합 해시는 흔들림이 아니라 "모양"(양 끝 쏠림)이 있다.</text>
</svg>
```

충돌의 기대값도 계산해 두면 표가 읽힌다.

| 지표 | 무작위 기대값 (n = 256) | 계산 |
|---|---|---|
| 32비트 h-coll | ≈ 0 (0.0000076) | 생일 문제 n²/(2·2³²) |
| b-coll, m = 64 | ≈ 193 | n − m(1 − (1 − 1/m)ⁿ) |
| b-coll, m = 1024 | ≈ 29 | 같은 식, 부하율 0.25 |

`count_hash_collisions` 는 정렬 → 이웃 비교. 해시 집합을 따로 만들지 않아도 되고, 정적 버퍼 1KB 로 끝난다.

### Part C — 실측 표와 해석

`./solution` 이 찍은 숫자 그대로다 (`max` = 가장 많이 찬 버킷, `empty` = 빈 버킷 수).

**m = 64 (2의 거듭제곱, E = 4, 기대 chi² = 63, σ = 11.2)**

| keyset | hash | chi² | max | empty | b-coll | h-coll |
|---|---|---|---|---|---|---|
| sensor | fnv1a | 64.5 | 8 | 2 | 194 | 0 |
| sensor | djb2 | 59.5 | 11 | 0 | 192 | 0 |
| sensor | sum | 89.5 | 11 | 4 | 196 | **127** |
| command | fnv1a | 83.0 | 9 | 6 | 198 | 0 |
| command | djb2 | 74.5 | 10 | 1 | 193 | 0 |
| command | sum | 55.0 | 8 | 3 | 195 | **63** |
| seq-int | fnv1a / djb2 / sum | **0.0** | 4 | 0 | 192 | 0 |
| stride8 | fnv1a / djb2 / sum | **0.0** | 4 | 0 | 192 | 0 |

**m = 61 (소수, E = 4.2, 기대 chi² = 60, σ = 11.0)**

| keyset | hash | chi² | max | empty | b-coll | h-coll |
|---|---|---|---|---|---|---|
| sensor | fnv1a | 57.1 | 9 | 0 | 195 | 0 |
| sensor | djb2 | 54.2 | 8 | 0 | 195 | 0 |
| sensor | sum | 93.8 | 12 | 2 | 197 | 127 |
| command | fnv1a | 61.4 | 9 | 1 | 196 | 0 |
| command | djb2 | 63.8 | 10 | 2 | 197 | 0 |
| command | sum | 61.4 | 9 | 0 | 195 | 63 |
| seq-int | fnv1a | 15.6 | 8 | 0 | 195 | 0 |
| seq-int | djb2 / sum | 2.3 | 5 | 0 | 195 | 0 |
| stride8 | fnv1a | 12.3 | 6 | 0 | 195 | 0 |
| stride8 | djb2 | **111.9** | 8 | **8** | 203 | 0 |
| stride8 | sum | 2.3 | 5 | 0 | 195 | 0 |

**m = 1024 (희소, E = 0.25, 기대 chi² = 1023, σ = 45.2)**

| keyset | hash | chi² | max | empty | b-coll | h-coll |
|---|---|---|---|---|---|---|
| sensor | fnv1a | 1016.0 | 2 | 799 | 31 | 0 |
| sensor | djb2 | 1024.0 | 3 | 799 | 31 | 0 |
| sensor | sum | **2288.0** | 6 | 895 | **127** | 127 |
| command | fnv1a | 1080.0 | 3 | 805 | 37 | 0 |
| command | djb2 | 1096.0 | 3 | 806 | 38 | 0 |
| command | sum | **1392.0** | 4 | 831 | 63 | 63 |
| seq-int / stride8 | 모두 | 768.0 | 1 | 768 | 0 | 0 |

**① 문자열 키: 합 해시만 무너진다.** sensor 에서 256개 중 127개가 다른 키와 32비트 값이 같다 (서로 다른 값이 129개뿐). m = 64 에선 chi² 89.5 로 "좀 나쁜" 정도로만 보이지만, m = 1024 로 버킷을 늘리면 정체가 드러난다: b-coll 이 무작위 기대 29 대신 **127 — 해시 충돌 수와 똑같다**. 버킷을 늘려도 해시 충돌은 절대 안 줄어든다. FNV-1a·djb2 는 b-coll 31~38 로 기대값 근처.

**② 측정 하나의 잡음.** command/fnv1a 의 chi² 83.0 은 기대 63 보다 약 1.8σ 크다. 같은 키를 m = 61 로 재면 61.4 로 정상. 1~2σ 차이는 운이다 — 해시를 하나의 숫자로 단정하지 말고 여러 m·여러 키 집합으로 본다.

**③ chi² = 0.0 은 "완벽"이 아니라 "순열"이다.** seq-int(0..255)를 m = 64 에 넣으면 세 해시 모두 모든 버킷에 정확히 4개. 이유가 해시마다 다르다:

- sum: 0..255 의 바이트 합 = 그 수 자체 → `h % 64` 는 그냥 `lba % 64`.
- djb2: `33³ ≡ 33 (mod 64)` 은 홀수라 b0 → 버킷이 일대일.
- FNV-1a: **하위 k비트 출력은 입력·상태의 하위 k비트로만 결정된다** (아래 그림). 하위 6비트에 대해 XOR 과 홀수 곱은 둘 다 일대일이라, b0 의 하위 6비트가 64가지를 4번씩 돌면 출력 하위 6비트도 64가지를 4번씩 돈다.

```svg
<svg viewBox="0 0 720 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">곱셈의 자리올림은 위로만 간다 → FNV의 h &amp; 63 은 아래 6비트만 본다</text>
  <g stroke="currentColor" fill="none"><rect x="60" y="45" width="560" height="32"/><rect x="60" y="120" width="560" height="32"/></g>
  <rect x="500" y="45" width="120" height="32" class="f-acc"/><rect x="500" y="120" width="120" height="32" class="f-acc"/>
  <rect x="60" y="45" width="440" height="32" class="f-mut"/>
  <g fill="currentColor"><text x="10" y="66">h</text><text x="10" y="141">h·P</text>
    <text x="200" y="66">상위 26비트 (시드·높은 비트)</text><text x="515" y="66">하위 6비트</text>
    <text x="180" y="141">상위: 아래쪽 모든 비트의 영향을 받음</text><text x="515" y="141">하위 6비트</text></g>
  <path d="M560 78 L560 118" stroke="currentColor" marker-end="url(#a9b)"/>
  <path d="M280 78 L280 118" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#a9b)"/>
  <path d="M500 80 C470 100 470 100 440 118" stroke="currentColor" marker-end="url(#a9b)"/>
  <defs><marker id="a9b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="180" fill="currentColor">h·P 의 하위 6비트 = (h 하위 6비트 × P) mod 64 — 위에서 내려오는 길이 없다.</text>
  <text x="10" y="202" fill="currentColor">→ 2^k 버킷의 h &amp; (m−1) 은 FNV 상위 비트의 섞임을 버린다. (XOR 도 자리마다 따로)</text>
  <text x="10" y="224" fill="currentColor">고침: 상위 비트를 접어 넣기 h ^ (h &gt;&gt; 16), 또는 상위 비트를 쓴다.</text>
</svg>
```

m = 1024 에서 seq-int 의 768.0 도 같은 이야기다: 256개 버킷에 정확히 1개씩 → 기대 1023 보다 **작다** (충돌 0, 무작위보다 규칙적).

**④ 소수 버킷은 만능이 아니다.** "버킷 수는 소수로" 는 *약한 해시*의 규칙성을 깨려는 오래된 요령이다. 실제로 seq-int 의 chi² 0 이 m = 61 에선 2.3 ~ 15.6 으로 풀렸다. 그런데 stride8 + djb2 는 m = 61 에서 오히려 **111.9, 빈 버킷 8개**로 표 전체에서 가장 나쁘다. djb2 는 4바이트 키에서 `h ≡ C + 33³·b0 + 33²·b1 (mod m)` 인데 `33³ ≡ 8 (mod 61)` 이라 b0 의 8칸 간격과 맞물려 특정 나머지에 몰린다. **소수냐 아니냐보다, 해시가 잘 섞느냐가 먼저**다.

**결론 (펌웨어 선택 가이드)**

- 문자열 키(명령어, 센서 이름, 파일 이름): **FNV-1a** + 2^k 버킷 + 하위 비트가 걱정되면 `h ^ (h >> 16)` 한 번.
- 곱셈이 느린 코어(Cortex-M0/M0+ 의 32사이클 소형 곱셈기 구성)·초소형: 시프트+덧셈뿐인 djb2 도 실용적.
- 정수 키(LBA, CAN ID): 바이트 해시보다 **곱셈 해시** `(x * 2654435761u) >> (32 − k)` (추가 연습 1).
- 단순 합(체크섬)은 해시로 쓰지 않는다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 표 3개 + 18/18 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `const char *p = data; h ^= *p;` | x86 에선 `char` 가 signed → `0xC3` 이 `0xFFFFFFC3` 로 XOR. ARM 과 해시가 달라짐 | `const uint8_t *p` |
| `int h = 5381; h = h*33 + c;` | signed overflow = UB. -O2 에서 최적화가 이상해질 수 있음 | `uint32_t h` |
| FNV-1a 에서 `h *= P; h ^= c;` | 그건 FNV-1. 참조값 `0xE40C292C` 가 안 나옴 | XOR 먼저, 곱 나중 |
| `double e = n / m;` | 정수 나눗셈 → m = 61 에서 E = 4 (실제 4.2) | `(double)n / m` |
| `chi² ≈ 0` 을 "최고 해시" 로 판단 | 무작위면 m−1 근처가 정상. 0 은 키 구조를 그대로 옮긴 순열 | m−1 ± 3σ 로 판단 |
| 구조체 키를 `fnv1a32(&s, sizeof s)` | **패딩 바이트**가 쓰레기라 같은 값의 구조체가 다른 해시 | 필드별로 해시하거나 `memset` 후 채움 |
| Cortex-M0 에서 `h % 61` | M0/M0+ 는 하드웨어 나눗셈이 없어 `__aeabi_uidivmod` 호출 (수십 사이클) | 2^k 버킷 + `h & (m-1)` + 섞인 상위 비트 |

## 8. 면접 Q&A

<details>
<summary>Q1. 좋은 해시 함수의 조건은? 어떻게 검증하나요?</summary>

① 결정적(같은 키 → 같은 값) ② 빠름 ③ 출력이 고르게 퍼짐(uniformity) ④ 입력 1비트 변화가 출력 절반을 바꿈(avalanche). 검증은 실제 키 집합으로 버킷 히스토그램 → 카이제곱(≈ m−1 인지), 32비트 충돌 수(≈ 0 인지), 최대 버킷 길이. 범용 해시는 SMHasher 같은 테스트 모음으로 본다. 꼬리질문 "카이제곱이 0 이면?" → 너무 규칙적, 키 구조가 그대로 보인다는 뜻.

</details>

<details>
<summary>Q2. FNV-1 과 FNV-1a 의 차이는? offset basis 와 prime 은 왜 그 값인가요?</summary>

FNV-1 은 `h *= P; h ^= c`, FNV-1a 는 `h ^= c; h *= P`. 1a 는 마지막 바이트도 곱셈을 거쳐 섞임이 더 좋다. prime 은 비트가 적게 켜진(2^24 + 2^8 + 0x93) 소수라 시프트-덧셈으로도 빨리 곱할 수 있고, offset basis 는 0 대신 쓰는 시작값(빈 입력에서 0 이 나오지 않게, `chongo <Landon Curt Noll> /\../\` 문자열을 FNV-0 으로 해시한 값).

</details>

<details>
<summary>Q3. 버킷 수를 2의 거듭제곱으로 할까요, 소수로 할까요?</summary>

2^k 면 `h & (m−1)` 한 번 — Cortex-M0 처럼 나눗셈기가 없는 코어에서 특히 중요하다. 대신 해시의 **하위 비트 품질**에 전적으로 의존한다(FNV 의 하위 비트는 입력 하위 비트만 봄, 항등 해시는 정렬된 키에서 망함). 소수 m 은 약한 해시의 규칙성을 일부 깨주지만 만능이 아니다(이번 실측: stride8 + djb2 + m = 61 → chi² 111.9). 실무 정답: 잘 섞는 해시 + 2^k, 또는 곱셈 해시의 상위 비트 사용.

</details>

<details>
<summary>Q4. 해시 플러딩(hash flooding) 공격이 뭔가요? 펌웨어와 관계있나요?</summary>

공격자가 같은 버킷으로 가는 키를 대량으로 보내 체이닝 테이블을 O(n) 리스트로 만들어 CPU 를 묶는 DoS. 해시가 공개(FNV, djb2)면 충돌 키를 오프라인으로 쉽게 만든다. 대책: 비밀 키를 섞는 keyed hash(SipHash — Linux·Python·Rust 기본), 버킷 길이 상한. 펌웨어도 네트워크로 키를 받는 순간(MQTT 토픽, HTTP 헤더, BLE 이름) 해당된다. 단 FNV 의 offset 에 시드를 XOR 하는 것만으론 약하다 — 상위 비트 시드는 `h & 63` 에 아무 영향이 없다(추가 연습 5).

</details>

<details>
<summary>Q5. STM32 의 하드웨어 CRC 를 해시로 써도 되나요?</summary>

분포는 꽤 괜찮고 하드웨어라 빠르니 내부 테이블용으론 실용적이다. 단 CRC 는 **선형**(`crc(a ⊕ b) = crc(a) ⊕ crc(b) ⊕ crc(0)`)이라 충돌을 일부러 만들기 매우 쉽고, 오류 검출용으로 설계된 것이지 섞임용이 아니다. 신뢰할 수 없는 입력엔 쓰지 않는다. 주변장치 공유(다른 태스크가 CRC 유닛 사용 중) 문제도 있다.

</details>

<details>
<summary>Q6. 정수 키(LBA, CAN ID)는 어떻게 해시하나요?</summary>

`lba % m` (항등 해시)는 순차 키엔 완벽하지만 정렬된 키(4KiB 단위 LBA = 8의 배수)에선 m = 64 중 8개 버킷만 쓴다. Knuth 곱셈 해시 `(x * 2654435761u) >> (32 − k)` — 2654435761 은 2³²/φ(황금비) 근처의 소수 — 는 곱셈 한 번으로 **상위 비트**를 쓰므로 모든 입력 비트가 섞인다. 더 강하게 섞으려면 murmur3 `fmix32` 같은 xorshift-multiply 피니셔.

</details>

<details>
<summary>Q7. 명령어 파서에서 strcmp 체인 대신 해시를 쓰는 방법은?</summary>

명령 집합이 고정이면 **완전 해시(perfect hash)** — 빌드 시 gperf 같은 툴로 충돌 없는 함수를 만들어 `switch (hash)` 로 분기하고, 맞은 칸에서 `strcmp` 한 번으로 확인한다. 런타임 테이블이 필요 없어 ROM 에 들어간다. 해시만 비교하고 strcmp 를 생략하면 다른 문자열이 같은 해시일 때 엉뚱한 명령이 실행되니 확인 비교는 필수.

</details>

## 9. 추가 연습

### 연습 1 — 정렬된 LBA 와 항등 해시

`lba & 63` 과 Knuth 곱셈 해시 `(lba * 2654435761u) >> 26` 로 LBA 0, 8, 16, …, 2040 (256개)을 64 버킷에 넣고 사용된 버킷 수를 세라.

<details>
<summary>답 보기</summary>

실측: 항등 **8개**, Knuth **64개** (순차 키 0..255 에선 항등도 64개). 8의 배수는 하위 3비트가 항상 0 이라 `& 63` 이 볼 수 있는 값이 8가지뿐이다. 곱셈 해시는 곱한 뒤 *상위* 6비트를 가져오므로 모든 입력 비트가 기여한다. FTL 의 매핑 캐시처럼 키가 정렬 단위로 오는 곳에서 그대로 터지는 버그.

</details>

### 연습 2 — 애벌랜치 측정

4바이트 키 256개에 대해 입력 32비트를 하나씩 뒤집고, 출력이 평균 몇 비트 바뀌는지 재라 (이상적 = 16).

<details>
<summary>답 보기</summary>

실측: FNV-1a **12.77**, djb2 **5.27**, sum **1.99**. 합 해시는 입력 1비트가 출력 1~2비트(자리올림)만 바꾼다. djb2 는 `×33` 이 비트를 5칸만 위로 퍼뜨려 약하다. FNV-1a 도 16 에 못 미친다 — 마지막 바이트가 곱셈을 한 번만 거치기 때문. 더 섞어야 하면 끝에 `fmix32` 를 붙인다.

</details>

### 연습 3 — 피니셔는 잃어버린 정보를 못 살린다

sensor 키에 `fmix32(sum32(key))` 를 써서 h-coll 과 chi²(m = 64)를 재라.

<details>
<summary>답 보기</summary>

실측: h-coll **127 → 127** (그대로), chi² 89.5 → **139.5** (오히려 나빠짐). 서로 다른 값이 129개뿐인 입력을 아무리 잘 섞어도 129개 값만 나온다 — 값 하나에 6개씩 몰린 덩어리가 그대로 버킷 하나에 떨어진다. 섞기(mixing)는 *분포*를 고칠 수 있지만 *충돌*은 앞 단계에서 이미 결정된다.

</details>

### 연습 4 — 스트리밍 FNV-1a

UART 로 NMEA 프레임 `"$GPGGA,123519,4807.038,N"` 이 7 / 10 / 나머지 바이트로 나눠 들어온다. 버퍼링 없이 조각마다 `h = fnv1a32_update(h, chunk, len)` 를 호출해 한 번에 해시한 값과 같음을 보여라.

<details>
<summary>답 보기</summary>

FNV 는 상태가 `h` 하나라 `fnv1a32_update(h, p, n)` 이 그대로 이어붙는다. 시작값만 offset basis 로 주면 된다. ISR 에서 바이트가 올 때마다 `h ^= b; h *= P;` 두 줄이면 되므로, 패킷이 다 모인 뒤 다시 훑을 필요가 없다 (CRC 를 바이트 단위로 갱신하는 것과 같은 패턴).

</details>

### 연습 5 — 시드를 섞으면 안전해질까?

offset basis 에 시드를 XOR 한 `fnv1a32_seeded(seed, key)` 로, 시드 0 과 시드 `0x2E` / `0x12340000` 에서 sensor 키가 같은 버킷(`& 63`)에 남는 수를 세라.

<details>
<summary>답 보기</summary>

실측: 하위 비트 시드 `0x2E` → **0/256** (전부 이동), 상위 비트 시드 `0x12340000` → **256/256** (하나도 안 바뀜). 그림의 성질 그대로: 출력 하위 6비트는 상태 하위 6비트로만 정해지므로, 시드의 상위 비트는 `h & 63` 에 영향이 0 이다. 그래서 공격 방어용 "시드 FNV" 는 착시다 — 진짜 방어는 SipHash 같은 keyed PRF.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
```

## 10. 핵심 정리

- FNV-1a: `h = 0x811C9DC5; for c: h ^= c; h *= 0x01000193;` — 참조값 `"a" → 0xE40C292C`. djb2: `h = 5381; h = h*33 + c`.
- 바이트는 `uint8_t`, 해시 상태는 `uint32_t` (signed char·signed overflow 함정).
- 품질 = 측정: 카이제곱 ≈ m−1 (σ = √(2(m−1))), 32비트 충돌 ≈ 0, b-coll ≈ n − m(1−(1−1/m)ⁿ).
- chi² 가 너무 크면 쏠림, 0 에 가까우면 키 구조가 그대로 (순열) — 둘 다 "섞임"의 증거가 아니다.
- 해시 충돌은 버킷을 늘려도 안 줄어든다 (sum: sensor 127, command 63). 피니셔로도 못 살린다.
- 2^k 버킷은 하위 비트에 의존 — FNV 하위 비트는 입력 하위 비트만 본다. xor-fold 또는 곱셈 해시 상위 비트.
- 정수 키: `(x * 2654435761u) >> (32 − k)`. 신뢰할 수 없는 입력: SipHash.

## 참고자료

- [FNV Hash — Landon Curt Noll (공식 페이지, 테스트 벡터)](http://www.isthe.com/chongo/tech/comp/fnv/index.html)
- [IETF draft-eastlake-fnv — The FNV Non-Cryptographic Hash Algorithm](https://datatracker.ietf.org/doc/draft-eastlake-fnv/)
- [Hash Functions (djb2, sdbm) — York University, Ozan Yigit](http://www.cse.yorku.ca/~oz/hash.html)
- [SMHasher — 해시 함수 품질 테스트 모음](https://github.com/aappleby/smhasher)
- [SipHash: a fast short-input PRF (Aumasson & Bernstein, 2012)](https://www.aumasson.jp/siphash/siphash.pdf)
- Donald Knuth, *TAOCP Vol. 3*, 6.4 Hashing — 곱셈 해시 (multiplicative hashing)
- [ARM Cortex-M0+ Technical Reference Manual — 나눗셈 명령 없음](https://developer.arm.com/documentation/ddi0484/latest/)
