---
id: DS-M14
time: 90분
updated: 2026-11-07
---
# DS-M14 블룸 필터: 확률적 집합

## 학습 목표

- m비트 비트맵(DS-B08)과 키당 k개 위치로 **"확실히 없음 / 아마 있음"** 을 답하는 블룸 필터(Bloom filter)를 malloc 없이 구현한다.
- 64비트 해시 하나로 k개 위치를 만드는 **더블 해싱** `g_i = h1 + i·h2` 를 쓰고, `m = 2^b` 일 때 **h2를 홀수로** 만들어야 하는 이유를 증명한다.
- 거짓 양성률(false positive rate) `(1 − e^{−kn/m})^k` 를 유도하고, 그 안의 **독립 근사**가 어디서 틀리는지(Jensen, 작은 m, 더블 해싱 바닥)를 실측으로 확인한다.
- 최적 `k = (m/n) ln 2` 와 크기 공식 `m = −n ln ε / (ln 2)²` 을 유도하고, 정수 k와 실측 k-스캔으로 검증한다.
- 세그먼트별 필터로 **없는 키의 플래시 읽기를 건너뛰는** KV 저장소를 만들고, RAM 예산 대비 절약된 읽기 수를 숫자로 말할 수 있다.

## 1. 문제

플래시·디스크 읽기 한 번(수십 µs~ms)은 RAM 비트 몇 개 확인(ns)보다 수천 배 비싸다. "이 키가 **없다**"를 RAM만 보고 빨리 말할 수 있으면 대부분의 헛읽기가 사라진다 — LevelDB/RocksDB의 SST 필터, 중복 제거 스토리지의 지문 인덱스, 네트워크 캐시 요약이 모두 이 구조다. 면접에서는 "공식 유도 + 파라미터 고르기 + 삭제는?"이 정해진 꼬리질문 세트다.

블룸 필터는 **한쪽만 틀리는** 집합이다. 넣은 키에 대해서는 절대 "없음"이라고 하지 않는다(거짓 음성 0). 넣지 않은 키에 대해서는 가끔 "있음"이라고 한다(거짓 양성 ε). 대가로 메모리는 **키 크기와 무관하게** 키당 약 10비트(ε ≈ 1%)면 된다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m14a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">m = 16 비트, k = 3. 위치 g_i = (h1 + i·h2) mod 16</text>
  <g stroke="currentColor" fill="none"> <rect x="20" y="36" width="170" height="26" rx="5"/><rect x="420" y="36" width="170" height="26" rx="5"/> <rect x="20" y="238" width="170" height="26" rx="5"/><rect x="420" y="238" width="170" height="26" rx="5"/> </g>
  <g fill="currentColor" font-family="monospace" font-size="12"> <text x="28" y="54">add x: h1=1 h2=5</text><text x="428" y="54">add w: h1=4 h2=3</text> <text x="28" y="256">query y: h1=2 h2=9</text><text x="428" y="256">query z: h1=1 h2=3</text> </g>
  <g stroke="currentColor"> <rect x="50" y="120" width="40" height="36" fill="none"/><rect x="90" y="120" width="40" height="36" class="f-acc"/> <rect x="130" y="120" width="40" height="36" class="f-warn" stroke-dasharray="4 3"/><rect x="170" y="120" width="40" height="36" fill="none"/> <rect x="210" y="120" width="40" height="36" class="f-acc"/><rect x="250" y="120" width="40" height="36" fill="none"/> <rect x="290" y="120" width="40" height="36" class="f-acc"/><rect x="330" y="120" width="40" height="36" class="f-acc"/> <rect x="370" y="120" width="40" height="36" fill="none"/><rect x="410" y="120" width="40" height="36" fill="none"/> <rect x="450" y="120" width="40" height="36" class="f-acc"/><rect x="490" y="120" width="40" height="36" class="f-acc"/> <rect x="530" y="120" width="40" height="36" fill="none"/><rect x="570" y="120" width="40" height="36" fill="none"/> <rect x="610" y="120" width="40" height="36" fill="none"/><rect x="650" y="120" width="40" height="36" fill="none"/> </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace"> <text x="70" y="143">0</text><text x="110" y="143">1</text><text x="150" y="143">0</text><text x="190" y="143">0</text> <text x="230" y="143">1</text><text x="270" y="143">0</text><text x="310" y="143">1</text><text x="350" y="143">1</text> <text x="390" y="143">0</text><text x="430" y="143">0</text><text x="470" y="143">1</text><text x="510" y="143">1</text> <text x="550" y="143">0</text><text x="590" y="143">0</text><text x="630" y="143">0</text><text x="670" y="143">0</text> </g>
  <g fill="currentColor" text-anchor="middle" font-size="12"> <text x="70" y="172">0</text><text x="110" y="172">1</text><text x="150" y="172">2</text><text x="190" y="172">3</text> <text x="230" y="172">4</text><text x="270" y="172">5</text><text x="310" y="172">6</text><text x="350" y="172">7</text> <text x="390" y="172">8</text><text x="430" y="172">9</text><text x="470" y="172">10</text><text x="510" y="172">11</text> <text x="550" y="172">12</text><text x="590" y="172">13</text><text x="630" y="172">14</text><text x="670" y="172">15</text> </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m14a)"> <line x1="90" y1="62" x2="110" y2="117"/><line x1="120" y1="62" x2="310" y2="117"/><line x1="150" y1="62" x2="510" y2="117"/> <line x1="470" y1="62" x2="230" y2="117"/><line x1="500" y1="62" x2="350" y2="117"/><line x1="530" y1="62" x2="470" y2="117"/> </g>
  <g stroke="currentColor" fill="none" stroke-dasharray="5 3" marker-end="url(#m14a)"> <line x1="80" y1="236" x2="150" y2="178"/><line x1="110" y1="236" x2="510" y2="178"/><line x1="140" y1="236" x2="230" y2="178"/> <line x1="460" y1="236" x2="110" y2="178"/><line x1="490" y1="236" x2="230" y2="178"/><line x1="520" y1="236" x2="350" y2="178"/> </g>
  <text x="20" y="284" fill="currentColor">y → 2, 11, 4: 비트 2 가 0 → 확실히 없음</text>
  <text x="400" y="284" fill="currentColor">z → 1, 4, 7: 셋 다 1 → 아마 있음 (거짓 양성)</text>
  <text x="200" y="104" fill="currentColor" font-size="12">z 의 비트는 x(1) 와 w(4, 7) 가 나눠서 세웠다</text>
</svg>
```

기호: **n** = 넣은 키 수, **m** = 비트 수, **k** = 키당 세우는 비트 수(= "해시 함수 개수"), **ε** = 거짓 양성률. 저장소는 호출자가 주는 `uint32_t` 워드 배열 — 예: m = 2^16 비트 = 8 KB.

### Part A — 해시 2개로 k 위치, add / maybe_contains

- `bloom_hash2(key, len, &h1, &h2)` — `h = fmix64(fnv1a64(key))` (64비트 해시 한 번), `h1` = 하위 32비트, `h2` = 상위 32비트 **| 1**.
- `bloom_add` — `g = h1` 에서 시작해 k번 `bit_set(g & (m−1))`, `g += h2`. `bloom_maybe_contains` — 같은 위치를 보다가 0이 하나라도 있으면 `false`.
- 테스트: 빈 필터, 키 하나 = 정확히 k비트, 같은 키 두 번 = 비트 불변, **m = 32·k = 32에서 키 하나가 32비트를 전부 켜는지**(h2 홀수 검증), 랜덤 3000번 add를 **64비트 `% m` 으로 따로 계산한 참조 비트맵**과 매번 비트 단위 대조 + 거짓 음성 0.

### Part B — 거짓 양성률: 이론 vs 실측, 최적 k

- `bloom_fp_theory(n, m, k)` = `(1 − e^{−kn/m})^k`, `bloom_k_opt(m, n)` = `(m/n) ln 2` 의 floor/ceil 중 이론 FP가 작은 쪽, `bloom_bits_for(n, ε)` = `⌈−n ln ε / (ln 2)²⌉`.
- 하네스가 (n, m, k) 8개 조합에서 짝수 키 n개를 넣고 **확실히 없는** 홀수 키 20만 개로 FP를 잰다. 채움률(fill) 실측 vs `1 − e^{−kn/m}`, FP 실측 vs 이론 vs `fill^k`.
- m/n = 10에서 k = 1..14를 스캔해 실측 최소가 k_opt 근처인지 확인.

### Part C — 플래시 읽기 건너뛰기: 세그먼트별 필터를 둔 KV 저장소

- 플래시에 append-only 세그먼트 16개(각 256 엔트리, LSM/로그 구조 KV의 축소판). 세그먼트마다 **RAM에 블룸 필터** 하나.
- `kv_put` — 지금 세그먼트에 append + 그 세그먼트 필터에 add. 가득 차면 다음 세그먼트, 전부 차면 `false`.
- `kv_get` — 새 세그먼트부터 옛 세그먼트로. 필터가 "없음"이면 **그 세그먼트는 읽지 않는다**. "아마"면 `flash_read()` (카운트) 후 검색.
- 필터 크기 0/4/8/16 비트/키에서 **없는 키 / 있는 키** 조회당 플래시 읽기 수를 재고, 결과가 필터 유무와 똑같은지(필터는 답을 바꾸면 안 된다) 확인.

## 2. Problem (English)

> **Bloom filter with double hashing, and a flash-read filter for a log-structured KV store.**
>
> 1. Implement a Bloom filter over a caller-provided bitmap of `m = 2^b` bits with `k` probes per key:
>    `void bloom_add(bloom_t *b, const void *key, size_t len)` and
>    `bool bloom_maybe_contains(const bloom_t *b, const void *key, size_t len)`.
>    Derive the k probe positions from one 64-bit hash as `g_i = (h1 + i*h2) mod m`, `i = 0..k-1`. No false negatives allowed. No dynamic allocation.
> 2. Implement `double bloom_fp_theory(n, m, k)`, `uint32_t bloom_k_opt(m, n)` and `uint32_t bloom_bits_for(n, eps)`. Measure the false-positive rate for several `(n, m, k)` and compare with `(1 - e^{-kn/m})^k`. Show empirically that the best integer `k` is near `(m/n) ln 2`.
> 3. A KV store keeps 16 append-only segments on flash (256 entries each) and one Bloom filter per segment in RAM. Implement `kv_put` and `kv_get` (newest segment wins) so that `kv_get` reads a segment from flash only when its filter says "maybe". Report flash reads per lookup for absent and present keys at 0/4/8/16 bits per key.
>
> Example: `m = 1024, k = 7`, empty filter: `maybe_contains("imu0") == false`; after `add("imu0")` exactly 7 bits are set and `maybe_contains("imu0") == true`. `bloom_bits_for(1000, 0.01) == 9586`, `bloom_k_opt(9586, 1000) == 7`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "4/19 passed", exit 1 — TODO 8곳을 채워 19/19 를 만든다
```

TODO: `bloom_hash2`, `bloom_add`, `bloom_maybe_contains` (Part A) / `bloom_fp_theory`, `bloom_k_opt`, `bloom_bits_for` (Part B) / `kv_put`, `kv_get` (Part C). 해시 재료(`fnv1a64`, `fmix64`), 비트 연산, 불변식 검사 `bloom_check()`, 참조 비트맵, 측정 함수 `measure()`, 하네스는 주어진다. 플래시는 반드시 `flash_read()` 로 읽어야 읽기 수가 잡힌다.

## 4. 힌트

<details>
<summary>힌트 1 — 해시를 k번 계산하지 않는다</summary>

64비트 해시 하나를 위/아래 32비트로 쪼개 `h1`, `h2` 로 쓰고, i번째 위치는 `h1 + i·h2`. 루프에서는 곱셈도 필요 없다 — `g = h1; ... g += h2;`. `uint32_t` 덧셈은 mod 2^32 로 랩어라운드하고(부호 없는 정수라 UB 아님), m이 2^32의 약수(2의 거듭제곱)라서 `g & (m−1)` 이 곧 `(h1 + i·h2) mod m` 이다.

</details>

<details>
<summary>힌트 2 — h2 가 짝수면 무슨 일이?</summary>

극단적인 경우 `h2 ≡ 0 (mod m)` 이면 k개 위치가 전부 같은 비트다 → 사실상 k = 1. 일반적으로 `h2 = 2^t·u` (u 홀수)면 위치는 주기 `m / 2^t` 로 반복한다. 홀수는 mod 2^b 에서 곱셈 역원이 있어서 `i·h2 ≡ j·h2 ⇒ i ≡ j` — k ≤ m 이면 k개 위치가 모두 다르다. 테스트의 "m = 32, k = 32 → 32비트 전부" 가 이걸 확인한다.

</details>

<details>
<summary>힌트 3 — 공식은 "비트 하나가 아직 0일 확률"에서 시작</summary>

비트 b 하나가 위치 하나에 안 맞을 확률은 1 − 1/m. 위치가 kn개 독립이라고 치면 b가 끝까지 0일 확률은 `(1 − 1/m)^{kn} ≈ e^{−kn/m}`. 없는 키가 거짓 양성이려면 그 키의 k 위치가 모두 1이어야 한다 → `(1 − e^{−kn/m})^k`. C로 쓸 때는 `double` 로 계산 — 정수 `k*n/m` 은 0으로 잘린다.

</details>

<details>
<summary>힌트 4 — 최적 k 는 미분보다 치환이 쉽다</summary>

`p = e^{−kn/m}` 으로 두면 `k = −(m/n) ln p` 이고 `ln FP = k ln(1−p) = −(m/n)·ln p·ln(1−p)`. p ↔ 1−p 대칭인 식이다. 정수 k는 floor와 ceil 둘 다 계산해 작은 쪽.

</details>

<details>
<summary>힌트 5 — kv_get 의 순서</summary>

같은 키가 여러 세그먼트에 있을 수 있다(갱신). 새 것이 이겨야 하므로 `cur` 에서 0 쪽으로, 세그먼트 안에서도 **뒤에서 앞으로** 찾는다. `unsigned` 역방향 루프는 `for (i = cur + 1; i-- > 0;)`. 필터 검사는 `flash_read()` **전에** — 순서가 바뀌면 절약이 0이다.

</details>

## 5. 풀이

### Part A — 비트 세우기, 더블 해싱, 불변식

```c
uint64_t h = fmix64(fnv1a64(key, len));
*h1 = (uint32_t)h;
*h2 = (uint32_t)(h >> 32) | 1u;          /* 홀수 강제 */
...
uint32_t g = h1;
for (i = 0; i < k; i++) { bit_set(w, g & mask); g += h2; }          /* add */
for (i = 0; i < k; i++) { if (!bit_test(w, g & mask)) return false; g += h2; }  /* query */
```

그림 1의 m = 16, k = 3 예를 손으로 따라가면:

| 연산 | h1, h2 | 위치 g_0, g_1, g_2 (mod 16) | 결과 |
|---|---|---|---|
| add x | 1, 5 | 1, 6, 11 | 비트 1, 6, 11 세움 |
| add w | 4, 3 | 4, 7, 10 | 비트 4, 7, 10 세움 (켜진 비트 6개) |
| query y | 2, 9 | 2, 11, 20→4 | 비트 2가 0 → **false** (첫 위치에서 바로 끝) |
| query z | 1, 3 | 1, 4, 7 | 모두 1 → **true** — 그런데 z는 넣은 적 없음 = 거짓 양성 |

**왜 64비트 해시 하나 + fmix64인가.** 해시 k개를 따로 계산하면 키 길이 L에 대해 O(kL)이고, 더블 해싱은 O(L + k). FNV-1a는 곱셈이 아래 비트를 위로만 퍼뜨려서 **하위 비트가 입력 하위 비트에만 의존**한다(DS-B09). `& mask` 는 하위 비트만 보므로 순차 LBA 같은 키에서 위치가 뭉칠 수 있다. 그래서 끝에 MurmurHash3의 `fmix64`(xor-shift와 홀수 곱의 조합, 전단사)로 모든 비트를 섞는다.

**왜 h2 홀수인가 (증명).** `m = 2^b`. h2가 홀수면 `gcd(h2, 2^b) = 1` 이므로 h2는 mod 2^b 의 가역원이다. `h1 + i·h2 ≡ h1 + j·h2 (mod 2^b)` 이면 `(i − j)·h2 ≡ 0` 이고, 양변에 h2^{−1}을 곱하면 `i ≡ j (mod 2^b)`. 0 ≤ i, j < k ≤ m 이면 i = j. 즉 **k개 위치가 전부 다르다**. m이 2의 거듭제곱이 아니면 조건은 `gcd(h2, m) = 1` 이고, m이 소수면 `h2 mod m ≠ 0` 만 보장하면 된다(그래서 소수 m + "h2를 1..m−1로" 를 쓰는 구현도 있다).

**불변식과 검증.**

| 불변식 | 왜 성립 | 검사 |
|---|---|---|
| 넣은 키는 항상 `true` (거짓 음성 0) | 비트는 0→1로만 바뀐다(단조). add가 세운 k비트는 영원히 1 | 매 add 후 그 키, 256번마다 지금까지 넣은 전체 |
| `popcount ≤ min(m, k·n)` | add 한 번이 최대 k비트를 새로 켠다 | `bloom_check()` |
| popcount 단조 증가, 같은 키 재삽입은 무변화 | OR은 멱등 | 매 add 후 비교, memcmp |
| 비트맵 = 참조 비트맵 | 같은 해시, 위치는 64비트 `(h1 + i·h2) % m` 으로 별도 계산 | `ref_equal()` 매 add 후 |

참조 모델은 비트당 1바이트 배열과 64비트 나머지 연산을 쓴다 — `uint32_t` 랩어라운드 + `& mask` 와 수학적으로 같은 값이어야 하므로, 랩어라운드 처리 실수를 잡는다. 실행 결과(m = 4096, k = 5에 3000개를 넣어 일부러 과포화시킴):

```text
[A] random 3000 adds vs reference bitmap: match (fill 0.977)
```

**복잡도.** add/query 모두 해시 O(L) + 비트 연산 k번. 없는 키 query는 대부분 첫 1~2 위치에서 끝난다(fill ≈ 1/2이면 평균 검사 수 < 2). 메모리는 m비트 — **키 자체를 저장하지 않는다**(그래서 삭제·열거가 안 된다). 최악 실행 시간이 L과 k로 고정되어 결정적이다 — ISR 경로에 넣을 수 있는 몇 안 되는 집합 구조.

### Part B — 거짓 양성률 유도

**① 비트 하나가 0으로 남을 확률.** kn개 위치가 각각 독립·균등이라고 가정하면, 고정된 비트 b가 끝까지 0일 확률은

  P(b = 0) = (1 − 1/m)^{kn} ≤ e^{−kn/m}   (ln(1 − x) ≤ −x)

더블 해싱처럼 **키 하나의 k 위치가 서로 다르면** 키 하나가 b를 맞힐 확률은 정확히 k/m(사건들이 서로소)이므로 P(b = 0) = (1 − k/m)^n. 베르누이 부등식 (1 − 1/m)^k ≥ 1 − k/m 에서 (1 − k/m)^n ≤ (1 − 1/m)^{kn} — 겹침이 없으니 비트가 조금 **더** 켜진다. 세 식 모두 m이 크면 e^{−kn/m} 으로 수렴한다.

**② 거짓 양성.** 켜진 비트 비율을 ρ = X/m 이라 하자. 없는 키의 k 위치가 필터와 독립·균등이면 **주어진 필터에서** FP = ρ^k (조건부로는 정확). 교과서는 여기서 ρ를 기댓값 1 − e^{−kn/m} 로 바꿔 넣는다:

  ε ≈ (1 − e^{−kn/m})^k

이게 **독립 근사**다. 실제로는 X가 확률변수이고 x ↦ x^k 는 [0, 1]에서 볼록이므로 Jensen에 의해

  E[ρ^k] ≥ (E[ρ])^k = (1 − (1 − 1/m)^{kn})^k ≥ (1 − e^{−kn/m})^k

즉 교과서 식은 (이 모델에서) **약간 낙관적인 하한**이다(Bose et al., 2008이 고전식이 실제 FP를 과소평가함을 보였다). 다행히 X는 기댓값 주위로 강하게 모여서(표준편차 O(√m)), ρ의 흔들림은 O(1/√m) — m이 수천 비트 이상이면 차이는 무시할 만하다. 아래 실측이 정확히 이 그림을 보여 준다.

**③ 최적 k.** n, m을 고정하고 k를 실수로 본다. p = e^{−kn/m} (k ↦ p 는 (0, ∞) → (0, 1) 전단사)로 치환하면 k = −(m/n) ln p 이고

  ln ε = k·ln(1 − p) = −(m/n) · h(p),  h(p) = ln p · ln(1 − p)

ε 최소 ⇔ h 최대. h(p) = h(1 − p) 로 대칭이다. h′(p) = ln(1 − p)/p − ln p/(1 − p) = ψ(p) / (p(1 − p)), ψ(p) = (1 − p)ln(1 − p) − p ln p. ψ(0+) = ψ(1/2) = 0 이고 ψ″(p) = 1/(1 − p) − 1/p < 0 (0 < p < 1/2) 이라 ψ는 (0, 1/2]에서 순오목 → 내부에서 ψ > 0 → h는 (0, 1/2)에서 증가, 대칭이므로 (1/2, 1)에서 감소. **유일한 최대는 p = 1/2.**

  e^{−kn/m} = 1/2  ⇒  **k\* = (m/n) ln 2**,  ε\* = (1/2)^{k\*} = e^{−(m/n)(ln 2)²} ≈ 0.6185^{m/n}

해석: 최적일 때 **비트의 정확히 절반이 켜진다** — 비트맵의 엔트로피가 최대인 상태. 비트가 너무 적게 켜지면(k 작음) 키당 증거가 약하고, 너무 많이 켜지면(k 큼) 필터가 1로 포화된다.

**④ 크기 공식.** ln ε\* = −(m/n)(ln 2)² 를 m에 대해 풀면

  m = −n ln ε / (ln 2)²,  m/n = log₂(1/ε) / ln 2 ≈ **1.44 · log₂(1/ε) 비트/키**,  k\* = log₂(1/ε)

| 목표 ε | 비트/키 m/n | k\* | 1M 키에 필요한 RAM |
|---|---|---|---|
| 10% | 4.79 | 3.3 | 0.60 MB |
| 1% | 9.59 | 6.6 | 1.20 MB |
| 0.1% | 14.38 | 10.0 | 1.80 MB |
| 0.01% | 19.17 | 13.3 | 2.40 MB |

ε를 10배 줄일 때마다 키당 4.8비트씩 늘어난다. 근사 소속(approximate membership)의 정보 이론 하한은 키당 log₂(1/ε) 비트이므로 블룸 필터는 하한보다 **약 44% 더** 쓴다(cuckoo filter 등이 이 격차를 줄이는 구조).

**정수 k의 대가.** `bloom_bits_for(1000, 0.01) = 9586` 비트에서 k\* = 6.64. 정수로는 7이 낫고(이론 0.01004 < k=6의 0.01014) — 공식이 약속한 1%를 아주 조금 넘는다. 정확히 맞춰야 하면 m을 몇 비트 키운다. 테스트가 `fp_theory(1000, 9586, 7) < 0.0101` 로 이걸 확인한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">이론 FP = (1 − e^(−k/(m/n)))^k  (세로축 로그)</text>
  <g stroke="currentColor" stroke-dasharray="2 4" class="f-mut" fill="none"> <line x1="70" y1="100" x2="690" y2="100"/><line x1="70" y1="160" x2="690" y2="160"/><line x1="70" y1="220" x2="690" y2="220"/> </g>
  <g stroke="currentColor" fill="none"><line x1="70" y1="40" x2="70" y2="280"/><line x1="70" y1="280" x2="690" y2="280"/></g>
  <g fill="currentColor" font-size="12" text-anchor="end"> <text x="62" y="44">1</text><text x="62" y="104">0.1</text><text x="62" y="164">0.01</text><text x="62" y="224">0.001</text><text x="62" y="284">0.0001</text> </g>
  <g fill="currentColor" font-size="12" text-anchor="middle"> <text x="70" y="298">k=1</text><text x="194" y="298">4</text><text x="359" y="298">8</text><text x="525" y="298">12</text><text x="690" y="298">16</text> </g>
  <g fill="none" stroke="currentColor" stroke-width="2"> <polyline points="70,96 91,109 111,119 132,126 153,131 173,135 194,137 215,139 235,140 256,140 277,140 297,139 318,138 339,137 359,136 380,134 401,132 421,130 442,128 463,126 483,124 504,121 525,119 545,117 566,114 587,112 607,110 628,107 649,105 669,103 690,101"/> <polyline stroke-dasharray="8 4" points="70,101 91,117 111,129 132,138 153,146 173,151 194,156 215,159 235,162 256,163 277,164 297,165 318,165 339,165 359,164 380,164 401,162 421,161 442,160 463,158 483,156 504,154 525,152 545,150 566,148 587,146 607,143 628,141 649,139 669,136 690,134"/> <polyline stroke-dasharray="2 3" points="70,113 91,134 111,152 132,166 153,178 173,188 194,197 215,205 235,211 256,217 277,222 297,226 318,229 339,232 359,234 380,236 401,238 421,239 442,240 463,240 483,240 504,240 525,240 545,239 566,239 587,238 607,237 628,236 649,234 669,233 690,231"/> </g>
  <g class="f-warn" stroke="currentColor"><circle cx="258" cy="140" r="5"/><circle cx="315" cy="165" r="5"/><circle cx="487" cy="240" r="5"/></g>
  <g fill="currentColor" font-size="12"> <text x="232" y="128">k*=5.5, 0.0214</text><text x="300" y="186">k*=6.9, 0.0082</text><text x="440" y="262">k*=11.1, 0.00046</text> </g>
  <g fill="currentColor" font-size="12"> <text x="560" y="66">── m/n = 8</text><text x="560" y="82">- - m/n = 10</text><text x="560" y="98">··· m/n = 16</text> </g>
  <text x="10" y="322" fill="currentColor" font-size="12">최소점 근처는 평평하다 — k를 ±1 틀려도 손해는 몇 %. 하지만 m/n(메모리)은 FP를 지수적으로 바꾼다.</text>
</svg>
```

**⑤ 실측 vs 이론.** `measure()` 는 짝수 키 `2i` 를 n개 넣고, 확실히 없는 홀수 키 `2j+1` 20만 개로 FP를 잰다. solution 실행의 실제 출력(이 Mac, 고정 시드):

| n | m | m/n | k | fill 실측 | 1−e^{−kn/m} | FP 실측 | FP 이론 | fill^k | 실측/이론 |
|---|---|---|---|---|---|---|---|---|---|
| 8192 | 65536 | 8.0 | 2 | 0.221 | 0.221 | 0.04859 | 0.04893 | 0.04874 | 0.993 |
| 8192 | 65536 | 8.0 | 4 | 0.393 | 0.393 | 0.02378 | 0.02397 | 0.02395 | 0.992 |
| 8192 | 65536 | 8.0 | 6 | 0.528 | 0.528 | 0.02164 | 0.02158 | 0.02164 | 1.003 |
| 8192 | 65536 | 8.0 | 10 | 0.714 | 0.713 | 0.03431 | 0.03419 | 0.03442 | 1.003 |
| 6554 | 65536 | 10.0 | 7 | 0.502 | 0.503 | 0.00794 | 0.00820 | 0.00808 | 0.969 |
| 4096 | 65536 | 16.0 | 11 | 0.497 | 0.497 | 0.00046 | 0.00046 | 0.00046 | 0.992 |
| 128 | 1024 | 8.0 | 6 | 0.542 | 0.528 | 0.02527 | 0.02158 | 0.02535 | **1.171** |
| 104858 | 1048576 | 10.0 | 7 | 0.504 | 0.503 | 0.00784 | 0.00819 | 0.00820 | 0.957 |

읽는 법:

- m = 65536 이상에서는 fill이 이론과 ±0.001 이내이고, FP는 이론의 ±5% 안(최소 0.957). 질의 20만 개의 이항 표준편차가 FP = 0.008에서 상대 2.5%라 0.957~0.969는 1~2σ 잡음이다.
- **FP 실측은 이론식보다 `fill^k` 에 더 가깝다** — ②의 "주어진 필터에서 FP = ρ^k" 가 실제로 맞다는 뜻.
- m = 1024 행이 17% 벗어난 건 질의 잡음이 아니다. 이 필터 하나의 fill이 우연히 0.542(이론 0.528, 1σ ≈ 0.016)였고 FP가 그대로 0.542^6 = 0.0254를 따라갔다. 작은 필터는 **필터마다** FP가 흔들린다. 그래서 하네스의 검사는 "fill이 이론의 4σ 안" + "FP가 fill^k의 4σ 안" 두 단계로 되어 있다.

**k 스캔** (n = 6554, m = 65536, (m/n) ln 2 = 6.93, k_opt = 7):

| k | 1 | 2 | 3 | 4 | 5 | 6 | **7** | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 이론 | .09517 | .03286 | .01741 | .01182 | .00943 | .00844 | **.00820** | .00846 | .00913 | .01019 | .01165 | .01357 | .01599 | .01899 |
| 실측 | .09397 | .03235 | .01723 | .01150 | .00897 | .00851 | **.00794** | .00845 | .00916 | .00993 | .01162 | .01337 | .01582 | .01878 |

실측 최소도 k = 7. k = 6~8은 서로 3~6% 차이뿐(그림 2의 평평한 바닥)이라 최소 위치가 잡음으로 ±1 움직일 수 있어서, 테스트는 "실측 최소 ∈ [6, 8] 이고 k_opt에서의 실측 ≤ 1.1 × 실측 최소"로 잡았다.

**⑥ 독립 근사를 더 파고들기 — `demo_jensen.c`.** 같은 (n, m, k)로 키 집합만 바꾼 필터 수천 개를 만들어 평균을 낸다.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_jensen.c -o demo_jensen && ./demo_jensen
```

| 설정 | 평균 FP 실측 | E[ρ^k] | (E[ρ])^k | (1−(1−1/m)^{kn})^k | (1−e^{−kn/m})^k | 필터별 FP 범위 |
|---|---|---|---|---|---|---|
| n=128, m=1024, k=6, 더블 해싱 (4000개) | 0.02247 | 0.02196 | 0.02186 | 0.02162 | 0.02158 | 0.0127 ~ 0.0371 |
| n=16, m=128, k=6, 더블 해싱 (10000개) | 0.02815 | 0.02452 | 0.02373 | 0.02192 | 0.02158 | 0.0098 ~ 0.0610 |
| n=16, m=128, k=6, **독립 해시 k개** (10000개) | 0.02277 | 0.02283 | 0.02209 | 0.02192 | 0.02158 | 0.0056 ~ 0.0532 |
| n=8192, m=65536, k=6, 더블 해싱 (200개) | 0.02166 | 0.02161 | 0.02161 | 0.02158 | 0.02158 | 0.0189 ~ 0.0245 |

- **독립 해시** 행: 평균 FP ≈ E[ρ^k] ≥ (E[ρ])^k ≥ 이항식 ≥ 지수식 — ②의 부등식 사슬이 숫자로 그대로 나온다.
- **더블 해싱 + 작은 m**: 평균 FP가 E[ρ^k]보다도 15% 크다. 더블 해싱은 위치들이 등차수열이라 **두 키의 (h1, h2)가 mod m으로 같거나 한두 칸 어긋나면 위치가 통째로 겹친다**. Kirsch–Mitzenmacher의 "더블 해싱도 FP가 같다"는 결과는 m/n 고정, n → ∞ 의 **점근적** 결과다. m = 65536에서는 차이가 사라진다(마지막 행).

**⑦ 더블 해싱의 바닥 — `demo_dh_floor.c`.** 질의 키의 (h1 mod m, h2 mod m)이 들어 있는 키 하나와 같을 확률은 1/m × 2/m(홀수 h2는 m/2가지)이고, 그러면 나머지 비트와 무관하게 거짓 양성이다. 한두 칸 어긋나 k−1, k−2개가 겹치는 경우까지 더하면 FP에는 **2n/m²의 수 배** 규모의 바닥이 생긴다(아래 실측: 2.4~3배). 목표 FP를 아주 낮게(k 큼) 잡을 때만 드러난다. 질의 2000만 개 실측:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_dh_floor.c -o demo_dh_floor && ./demo_dh_floor
```

| n, m, k | 이론 (1−e^{−kn/m})^k | 2n/m² | double h1+i·h2 | k개 독립 | enhanced h1+i·h2+(i³−i)/6 |
|---|---|---|---|---|---|
| 500, 16384, 7 | 9.74e-06 | 3.73e-06 | 1.97e-05 (395) | 1.02e-05 (204) | 1.43e-05 (286) |
| 1000, 65536, 15 | 4.6e-11 | 4.66e-07 | **1.1e-06 (22)** | 0 (0) | 3e-07 (6) |
| 8192, 65536, 6 | 0.0216 | 3.81e-06 | 0.0212 | 0.0214 | 0.0213 |

두 번째 행이 핵심이다: 이론은 4.6·10⁻¹¹인데 더블 해싱은 10⁻⁶ — **5자리나** 나쁘다. 해법은 ① 목표 FP ≫ 2n/m² 로 쓰기(보통의 1%·0.1% 필터는 해당 없음), ② enhanced double hashing(Dillinger–Manolios), ③ 64비트 위치 산술, ④ 아주 낮은 FP가 필요하면 독립 해시. 이 레슨의 Part B/C 설정(ε ≥ 4.6·10⁻⁴)은 바닥보다 수십 배 이상 위라서 영향이 없다.

### Part C — 플래시 읽기 건너뛰기

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m14c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">kv_get(key): 새 세그먼트 → 옛 세그먼트. RAM 필터가 "없음"이면 플래시를 읽지 않는다</text>
  <text x="10" y="70" fill="currentColor">RAM</text><text x="10" y="190" fill="currentColor">Flash</text>
  <g stroke="currentColor"> <rect x="70" y="50" width="90" height="34" rx="4" class="f-mut"/><rect x="180" y="50" width="90" height="34" rx="4" class="f-warn"/> <rect x="290" y="50" width="90" height="34" rx="4" class="f-ok"/><rect x="510" y="50" width="90" height="34" rx="4" fill="none"/> <rect x="620" y="50" width="90" height="34" rx="4" fill="none"/> </g>
  <g fill="currentColor" font-size="12" text-anchor="middle"> <text x="115" y="64">filter 15</text><text x="115" y="79">없음</text> <text x="225" y="64">filter 14</text><text x="225" y="79">아마 (FP)</text> <text x="335" y="64">filter 13</text><text x="335" y="79">아마</text> <text x="555" y="72">filter 1</text><text x="665" y="72">filter 0</text> <text x="445" y="72">…</text><text x="445" y="192">…</text> </g>
  <g stroke="currentColor" fill="none"> <rect x="70" y="160" width="90" height="56" rx="4"/><rect x="180" y="160" width="90" height="56" rx="4"/> <rect x="290" y="160" width="90" height="56" rx="4"/><rect x="510" y="160" width="90" height="56" rx="4"/> <rect x="620" y="160" width="90" height="56" rx="4"/> </g>
  <rect x="180" y="160" width="90" height="56" rx="4" class="f-warn" stroke="currentColor"/>
  <rect x="290" y="160" width="90" height="56" rx="4" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="middle"> <text x="115" y="183">seg 15</text><text x="115" y="200">256 엔트리</text> <text x="225" y="183">seg 14 읽음</text><text x="225" y="200">키 없음 = 헛읽기</text> <text x="335" y="183">seg 13 읽음</text><text x="335" y="200">찾음 → 반환</text> <text x="555" y="192">seg 1</text><text x="665" y="192">seg 0 (가장 옛)</text> </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m14c)"> <line x1="225" y1="86" x2="225" y2="157"/><line x1="335" y1="86" x2="335" y2="157"/> </g>
  <g stroke="currentColor" fill="none" stroke-dasharray="4 3"><line x1="115" y1="86" x2="115" y2="157"/></g>
  <text x="122" y="125" fill="currentColor" font-size="12">✗ 안 읽음</text>
  <text x="10" y="246" fill="currentColor">없는 키: 기대 읽기 = Σ ε_i ≈ 16·ε  (필터 없으면 항상 16)</text>
  <text x="10" y="266" fill="currentColor">있는 키(seg j 에 최신 값): 기대 읽기 = 1 + (j 보다 새 세그먼트 수)·ε</text>
  <text x="10" y="286" fill="currentColor">필터는 답을 바꾸지 않는다 — 읽기 횟수만 바꾼다 (거짓 음성이 없으니까)</text>
</svg>
```

**왜 정확성이 보존되나.** 필터가 `false` 를 주는 세그먼트에는 그 키가 **확실히** 없다(거짓 음성 0) → 건너뛰어도 답이 같다. `true` 인데 실제로 없으면(거짓 양성) 읽기 한 번을 낭비할 뿐 답은 같다. 그래서 테스트는 필터 0/4/8/16 비트/키 네 설정 모두에서 2만 번 조회 결과를 참조 모델(키 번호 → 최신 값)과 대조하고, 읽기 수만 비교한다. 키는 `fmix32(i)` (전단사)로 만들어 서로 다른 i가 서로 다른 키임을 보장하고, 없는 키는 i ≥ 2^20 에서 뽑아 **확실히 없음**을 보장했다. 4096번 put 중 10%는 예전 키의 갱신이다.

실행 결과(세그먼트 16개 × 256 엔트리, 조회 2만 번 = 없는 키 1만 + 있는 키 1만):

| 필터 비트/세그먼트 | 비트/키 | k (k_opt) | RAM (16 seg) | 없는 키: 읽기/조회 | 이론 16·ε | 있는 키: 읽기/조회 |
|---|---|---|---|---|---|---|
| 0 (필터 없음) | — | — | 0 | 16.0000 | 16 | 8.113 |
| 1024 | 4 | 3 | 2048 B | 2.2985 | 2.3503 | 2.028 |
| 2048 | 8 | 6 | 4096 B | 0.3286 | 0.3452 | 1.154 |
| 4096 | 16 | 11 | 8192 B | 0.0091 | 0.0073 | 1.006 |

- RAM 4 KB(키당 8비트)로 없는 키 조회의 플래시 읽기가 16 → 0.33, **약 49배** 줄었다. 있는 키도 평균 8.1 → 1.15. NAND 페이지 읽기(tR) 하나가 수십 µs라면 조회 지연이 거의 그만큼 줄어든다.
- 16비트/키 행은 기대 73번에 실측 91번(1만 조회 기준) — 포아송 σ ≈ 8.5라 약 2σ. 필터 16개 각자의 fill도 흔들리고(⑤), m = 4096 짜리 작은 필터라 ⑦의 바닥(세그먼트당 2n/m² ≈ 3·10⁻⁵)도 조금 더해진다.
- 있는 키에 대해서도 필터가 효과가 있다: 최신 값이 seg j에 있으면 j보다 새 세그먼트들은 대부분 건너뛴다.

**펌웨어 현실과의 연결.**

- **언제 비트맵이 낫나.** 배드 블록 테이블(BBT)처럼 키 공간이 작고 촘촘하면(블록 4096개 → 비트맵 512 B, 정확) DS-B08 비트맵이 정답이다. 블룸 필터 메모리는 **넣은 개수 n**에 비례하고(1.44·n·log₂(1/ε)) 비트맵은 **키 공간 크기 U**에 비례한다. 32비트 키(U = 2^32 → 512 MB 비트맵)나 문자열·64비트 지문처럼 U가 거대할 때 블룸 필터가 이긴다. "배드 블록 후보가 드물고 블록 번호가 64비트 (die, plane, block) 조합" 같은 경우라면 필터 + 플래시의 정확한 표 조합이 될 수 있다.
- **필터 수명.** 세그먼트가 불변(append-only)이면 필터도 불변 — 세그먼트를 닫을 때 필터를 플래시에 같이 쓰고(LevelDB가 SST 안에 filter block을 둔다), 마운트 때 RAM으로 올린다. 컴팩션(DS-A10)이 세그먼트를 합치면 필터는 **다시 만든다**(블룸 필터에서 키를 뺄 수 없으므로).
- **RAM 예산.** Cortex-M4 192 KB SRAM에서 필터에 8 KB를 주면 16비트/키로 4096 키, 8비트/키면 8192 키. 예산이 고정이면 m/n이 정해지고 k = (m/n) ln 2가 따라온다.
- **중복 제거 스토리지.** Data Domain 파일 시스템(FAST'08)은 "Summary Vector"라는 블룸 필터로 새 세그먼트의 지문이 디스크 인덱스에 **없음**을 RAM에서 판정해 디스크 인덱스 조회를 피한다 — Part C와 같은 아이디어다.

**복잡도.** `kv_get` 은 필터 검사 S번(각 O(k)) + 플래시 읽기 기대 S·ε(없는 키) — 최악은 여전히 S번(모든 필터가 거짓 양성). 실시간 보장이 필요하면 "최악 S번"을 기준으로 설계하고, 평균만 필터로 줄인다고 말해야 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 19/19 passed (0.2초 이내)
# 메모리 오류 확인
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan
```

관찰용 프로그램 두 개(테스트 아님, 위 ⑥⑦의 표를 만든다): `demo_jensen.c`, `demo_dh_floor.c` — 빌드 명령은 5장 ⑥⑦에 있다. 둘 다 ASan/UBSan으로도 깨끗하다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `h2 = (uint32_t)(h >> 32)` (홀수 강제 없음) | m = 2^b에서 h2가 짝수면 위치가 주기 m/2^t로 반복, h2 ≡ 0이면 k위치가 전부 같음 → 사실상 k = 1 | `h2 = (uint32_t)(h >> 32) \| 1u` (소수 m이면 `h2 % m != 0`) |
| `g_i = h1 + i` (연속 k비트) | 켜진 비트가 덩어리로 뭉쳐 "옆 비트도 1"일 확률이 높다 → 실측 FP 11.7배(k=6), 427배(k=11) — 연습 2 | 등차 h2를 홀수 큰 수로: `h1 + i*h2` |
| FNV-1a 결과의 하위 비트를 바로 `& mask` | FNV 하위 비트는 입력 하위 비트만 반영 — 순차 키가 위치를 공유 | `fmix64()` 등 최종 섞기(finalizer) |
| "해시 많을수록 정확" → k를 크게 | k > k\*면 필터가 1로 포화 — m/n=8에서 k=10은 0.0343, k=6은 0.0216 | `k = round((m/n) ln 2)` 근처 |
| 지우려고 비트를 0으로 clear | 그 비트를 공유한 **다른 키**가 거짓 음성이 된다 — 블룸 필터의 유일한 약속이 깨짐 | 지우지 않는다 / 카운팅 블룸 필터(연습 1) / 주기적 재구축 |
| n을 예상의 2배 넣음 | m, k 고정이면 m/n=10 → 5: FP 0.0082 → 0.1378 (17배) | 크기를 최대 n으로 잡고, fill > 0.5 감시 또는 n 추정(연습 3) |
| `pow(1 - exp(-k*n/m), k)` 를 정수 인자로 계산 | `k*n/m` 이 정수 나눗셈 → 0 → FP = 0 | `double` 로 받거나 `(double)k * n / m` |
| m이 2의 거듭제곱이 아닌데 `g & (m-1)` | m−1이 마스크가 아님 → 일부 비트는 절대 안 쓰임, 범위 밖 인덱스 가능 | `bloom_init` 에서 2의 거듭제곱 검사, 아니면 `% m` |
| `kv_get` 에서 flash_read 후 필터 검사 | 정확하지만 절약 0 — 필터의 존재 이유가 사라짐 | 필터 검사를 **먼저** |

## 8. 면접 Q&A

<details>
<summary>Q1. 거짓 양성률 공식을 유도해 보세요. 어떤 가정이 들어가나요?</summary>

비트 하나가 위치 하나에 안 맞을 확률 1 − 1/m, kn개 위치가 독립·균등이면 끝까지 0일 확률 (1 − 1/m)^{kn} ≈ e^{−kn/m}. 없는 키의 k 위치가 모두 1일 확률을 "1의 비율^k"로 두면 (1 − e^{−kn/m})^k. 가정은 ① 해시가 균등·독립, ② **각 비트가 1일 사건들이 서로 독립**(실제로는 음의 상관), ③ 켜진 비율을 기댓값으로 대체. 꼬리질문 "그럼 정확한가?" → 주어진 필터에서는 FP = ρ^k가 맞고, ρ가 확률변수라 Jensen으로 E[ρ^k] ≥ (Eρ)^k — 고전식은 약간 낙관적(Bose et al. 2008). m이 수천 비트 이상이면 ρ가 집중돼 차이 무시. 실측: m ≥ 65536에서 ±5% 안, m=1024 필터 하나는 +17%(그 필터의 fill이 높았음).

</details>

<details>
<summary>Q2. 최적 k는? 왜 그때 비트의 절반이 1인가요?</summary>

p = e^{−kn/m} 로 치환하면 ln ε = −(m/n) ln p ln(1−p), p ↔ 1−p 대칭이고 유일한 최대가 p = 1/2 → k = (m/n) ln 2, ε = 2^{−k} ≈ 0.6185^{m/n}. 직관: k가 작으면 키당 증거가 부족, 크면 필터가 포화. 절반이 1일 때 비트맵의 정보량(엔트로피)이 최대다. 실무 공식은 거꾸로: ε을 정하면 비트/키 = 1.44 log₂(1/ε), k = log₂(1/ε). "1%면 키당 약 10비트, 해시 7개"를 외워 두면 면접에서 바로 쓴다. 정수 k는 floor/ceil 중 작은 쪽 — 최소점 근처가 평평해서 ±1은 몇 % 차이.

</details>

<details>
<summary>Q3. 블룸 필터에서 삭제는 어떻게 하나요?</summary>

기본 블룸 필터는 못 한다 — 비트를 끄면 그 비트를 공유한 다른 키가 거짓 음성이 된다. ① **카운팅 블룸 필터**(Fan et al., Summary Cache 2000): 비트 대신 4비트 카운터, add는 +1, delete는 −1. 메모리 4배. 카운터가 15에 닿으면 그 칸은 고정(포화) — 거짓 음성을 막는 대신 그 칸은 영원히 1. Fan et al.은 최적 k에서 4비트 카운터가 넘칠 확률이 실용적으로 무시할 수준임을 보였다. **함정**: 넣은 적 없는 키를 지우면 안 된다. "아마 있음"이면 지운다는 검사는 거짓 양성 키를 통과시킨다 — 연습 1에서 실제로 그렇게 지웠더니 진짜 키 600개 중 3개가 거짓 음성이 됐다. ② **cuckoo filter**(Fan et al. 2014): 지문을 저장해 삭제 가능, 낮은 ε에서 블룸보다 공간도 작다. ③ 세그먼트 단위 재구축(Part C처럼 불변 세그먼트 + 컴팩션 때 새로 만들기) — 스토리지에서 가장 흔하다.

</details>

<details>
<summary>Q4. 해시 함수가 정말 k개 필요한가요? 더블 해싱의 함정은?</summary>

Kirsch & Mitzenmacher(2006): g_i = h1 + i·h2 두 개로 만든 위치도 점근적으로 같은 FP. LevelDB `util/bloom.cc` 도 32비트 해시 하나와 그 회전값 `delta = (h >> 17) | (h << 15)` 을 더해 가며 k 위치를 만든다(`k = bits_per_key × 0.69`, 1..30으로 제한). 함정 ① m = 2^b 에서 h2가 짝수면 위치가 겹친다 → `| 1`. ② "점근적"이라는 단서 — 두 키의 (h1, h2)가 mod m으로 같거나 한두 칸 어긋나면 위치가 통째로(또는 거의) 겹쳐 FP에 2n/m²의 수 배 규모의 바닥이 생긴다. 실측: n=1000, m=65536, k=15에서 이론 4.6·10⁻¹¹인데 더블 해싱 1.1·10⁻⁶, 독립 해시 0(2000만 질의). 아주 낮은 ε이 필요하면 enhanced double hashing(h1 + i·h2 + (i³−i)/6, Dillinger–Manolios)나 64비트 위치 산술, 독립 해시.

</details>

<details>
<summary>Q5. ISR과 태스크가(또는 두 코어가) 같은 필터를 쓰면?</summary>

add는 비트 OR만 하고 비트는 절대 0으로 돌아가지 않는다(단조) — 동시성 면에서 매우 다루기 쉽다. 문제는 **읽기-수정-쓰기**: `w[i] |= bit` 는 load-or-store라 두 문맥이 같은 워드를 동시에 고치면 한쪽 비트가 사라진다(→ 거짓 음성!). 해법: C11 `atomic_fetch_or_explicit(&w[i], bit, memory_order_relaxed)` (ARMv7-M에서는 LDREX/STREX 루프), Cortex-M0처럼 LDREX가 없으면 짧은 임계 구역, Cortex-M3/M4(STM32F1/F4 등)는 SRAM bit-band 별칭 주소에 1을 쓰면 단일 비트 쓰기가 원자적이다. 읽는 쪽은 add가 **진행 중인** 키를 "없음"으로 볼 수 있지만, 그 add는 아직 일어나지 않은 것이니 선형화 관점에서 문제없다. add 완료 후 다른 코어가 반드시 보게 하려면 add 끝에 release, 읽기에 acquire. 카운팅 필터는 감소가 있어 이런 단순함이 사라진다.

</details>

<details>
<summary>Q6. 블룸 필터 vs 해시 셋 vs 비트맵 — 언제 무엇을?</summary>

비트맵: 키 공간 U가 작고 촘촘할 때(블록 번호, 우선순위, PID). 메모리 U비트, 정확. 해시 셋: 정확한 답과 삭제·열거가 필요할 때. 메모리 n × (키 크기 + 오버헤드) — 32비트 키에 부하율 0.5면 키당 64비트 이상. 블룸 필터: U가 거대하고, "없음"을 빨리 거르는 게 목적이고, 가끔의 거짓 양성을 뒤의 정확한 검사(플래시 읽기, 디스크 인덱스)가 처리할 때. 키당 ~10비트(1%)로 해시 셋보다 6배 이상 작고 키 길이와 무관(URL 100바이트든 LBA 4바이트든 같음). 정보 이론 하한(log₂(1/ε) 비트/키)보다 44% 더 쓴다.

</details>

<details>
<summary>Q7. 넣을 개수 n을 미리 모르면? 필터가 꽉 찼는지 어떻게 아나요?</summary>

켜진 비트 수 X로 n을 추정한다: E[X/m] ≈ 1 − e^{−kn/m} 을 뒤집으면 n̂ = −(m/k) ln(1 − X/m) (Swamidass & Baldi 2007). 연습 3 실측 오차 ±0.3% 이내. X/m > 1/2이면 이미 최적점을 넘었다는 신호 — 그때 ① 더 큰 필터로 재구축(원본 키가 있어야 함), ② **scalable Bloom filter**(Almeida et al. 2007): 필터를 층으로 쌓고 새 층은 더 엄격한 ε_i로 만들어 전체 ε를 등비급수로 묶는다. 필터 크기를 반으로 접는 것(위 절반을 아래 절반에 OR)도 가능 — m/2 필터가 되고 FP는 커진다.

</details>

<details>
<summary>Q8. 실제로 어디에 쓰이나요? 근거를 말할 수 있는 예를.</summary>

LevelDB/RocksDB — SST 파일마다 filter block, LevelDB 문서는 `NewBloomFilterPolicy(10)`(키당 10비트, 약 1%)을 권한다. Get 시 그 SST를 디스크에서 읽을지 결정 — Part C와 같은 구조. Linux eBPF `BPF_MAP_TYPE_BLOOM_FILTER`(커널 5.16, `kernel/bpf/bloom_filter.c`): 시드를 바꾼 jhash로 k번 해시, push/peek만 있고 삭제 없음. 중복 제거 스토리지 Data Domain의 Summary Vector(FAST'08), 웹 캐시 간 요약 교환(Summary Cache, 카운팅 블룸 필터의 출처). 꼬리질문 "SSD 펌웨어에서는?" → LSM 기반 KV-SSD, 그리고 "이 LBA/지문이 존재하는가"를 RAM에서 거르고 NAND의 정확한 인덱스로 확정하는 모든 구조.

</details>

## 9. 추가 연습

### 연습 1 — 카운팅 블룸 필터: 삭제와 그 함정

4096개 4비트 카운터(바이트당 2개 = 2 KB), k = 5로 add/remove/maybe를 구현하라. 랜덤 add/remove 2만 번 동안 "포화가 없으면 카운터 합 == k × (넣은 개수, 중복 포함)"을 매번 검사하고, 다 지운 뒤 모든 카운터가 0인지 확인하라. 그다음 키 600개를 넣고, 넣은 적 없지만 "아마 있음"인 키를 하나 찾아 `cbf_remove` 해 보라.

<details>
<summary>답 보기</summary>

카운터 합 불변식: add 한 번은 +1을 k번 하므로(포화가 없다면) 합이 정확히 k 늘고, remove는 k 줄어든다 — 위치가 겹쳐도 성립하지만, h2 홀수라 실제로는 k칸이 모두 다르다. 포화(15)된 카운터는 증가도 감소도 하지 않는다 — 감소하면 그 칸을 공유한 다른 키가 거짓 음성이 될 수 있다. 실행 결과:

```text
[1] counting BF random add/remove: ok, all counters back to 0
[1] removed never-inserted key 1000047 (a false positive): 3 of 600 real keys now false negative
```

`cbf_remove` 는 "아마 있음"일 때만 줄이지만 거짓 양성 키는 이 검사를 통과한다. 그 키의 5개 카운터를 1씩 줄이면 어떤 칸이 0이 되고, 그 칸을 쓰는 진짜 키 3개가 "없음"이 됐다. 교훈: **넣었다고 확실히 아는 키만 지운다**(호출자가 정확한 집합을 따로 갖고 있어야 한다).

</details>

### 연습 2 — 위치 생성 방식 비교

같은 키 집합으로 ① 더블 해싱 h1 + i·h2, ② h1 + i (연속 k비트), ③ k개 독립 해시의 FP를 재라 (n=8192/k=6, n=4096/k=11, m=65536).

<details>
<summary>답 보기</summary>

```text
[2] n=8192 m=65536 k=6  theory 0.02158
[2]   double h1+i*h2 FP 0.02164  (x1.00)
[2]   linear h1+i    FP 0.25210  (x11.68)
[2]   k independent  FP 0.02199  (x1.02)
[2] n=4096 m=65536 k=11  theory 0.00046
[2]   double h1+i*h2 FP 0.00046  (x0.99)
[2]   linear h1+i    FP 0.19580  (x426.85)
[2]   k independent  FP 0.00049  (x1.07)
```

더블 해싱 = 독립 해시(잡음 범위). h1 + i는 키마다 길이 k짜리 연속 구간을 칠하므로 켜진 비트가 긴 덩어리를 이루고, 질의의 연속 구간이 덩어리 안에 통째로 들어갈 확률이 ρ^k보다 훨씬 크다 — "비트들이 독립"이라는 가정이 정면으로 깨지는 예다. k가 클수록 덩어리가 길어져 더 나빠진다.

</details>

### 연습 3 — 켜진 비트 수로 원소 수 추정

`bf_estimate_n()` = −(m/k) ln(1 − X/m) 을 구현하고 n = 100, 1000, 4000, 8000 (m = 65536, k = 6)에서 오차를 재라. 모든 비트가 1이면?

<details>
<summary>답 보기</summary>

```text
[3] n=  100  set bits=  598  estimate=   100.1  (err +0.12%)
[3] n= 1000  set bits= 5731  estimate=   999.5  (err -0.05%)
[3] n= 4000  set bits=20094  estimate=  3999.5  (err -0.01%)
[3] n= 8000  set bits=33968  estimate=  7978.5  (err -0.27%)
```

X = m이면 ln(0) → 추정 불가이므로 `INFINITY` 를 반환한다(테스트: m = 64에 5000개). 필터가 "꽉 찼는지"를 X/m > 1/2(최적점 초과)로 감시하는 데도 같은 X를 쓴다. 두 필터의 합집합 크기, 교집합 크기 추정(포함-배제)에도 응용된다.

</details>

### 연습 4 — 합집합(OR)과 교집합(AND)

같은 m, k, 해시로 A = {0..999}, B = {500..1499} 필터를 만들고 ① A|B 가 {0..1499}로 직접 만든 필터와 비트 단위로 같은지, ② A&B 와 {500..999}로 직접 만든 필터를 비교하라.

<details>
<summary>답 보기</summary>

```text
[4] OR == filter(A u B): bit-exact | AND bits 3659 vs filter(A n B) bits 3136 | FP AND 0.00005 vs 0.00003
```

OR은 정확하다 — 각 비트는 "어떤 키가 이 비트를 세웠나"의 OR이므로. 여러 코어/태스크가 각자 필터를 만들고 나중에 합칠 수 있다(병렬 구축, 분산 캐시 요약 교환). AND는 근사다 — A에만 있는 키 x와 B에만 있는 키 y가 같은 비트를 세웠으면 교집합에 없는 비트가 남는다. 그래서 AND 필터는 진짜 교집합 필터의 비트를 모두 포함하는 상위 집합(거짓 음성 없음)이지만 비트가 더 많고(3659 vs 3136) FP가 더 높다. (참고: 직접 만든 교집합 필터의 FP 3·10⁻⁵ 도 이론 ρ^7 ≈ 10⁻⁵ 보다 높다. 5장 ⑦ 표 첫 행이 바로 같은 n=500, m=16384, k=7 설정인데 거기서도 더블 해싱 실측이 2.0·10⁻⁵ 로 이론의 2배였다. 100만 질의에서 기대 20개 안팎이라 이 정도 차이는 잡음 범위다.)

</details>

### 연습 5 — 블록 블룸 필터 (캐시 라인 하나에 k비트)

m = 65536을 512비트(64 B = 캐시 라인) 블록 128개로 나누고, 키의 k비트를 **한 블록 안에** 몰아 세워라. 블록 안 위치는 ① 블록 안 더블 해싱, ② 64비트 해시를 9비트씩 잘라 쓰기 두 가지로. 일반 필터와 FP를 비교하고, 블록 부하가 Poisson(n/128)이라는 이론 Σ P(L)(1 − e^{−kL/512})^k 과도 비교하라.

<details>
<summary>답 보기</summary>

```text
[5] n=4096 m/n=16 k=6 | standard 0.00101 (theory 0.00094) | blocked: 9-bit fields 0.00128, in-block double hash 0.00208 (theory 0.00122)
[5] n=8192 m/n=8 k=6 | standard 0.02164 (theory 0.02158) | blocked: 9-bit fields 0.02350, in-block double hash 0.02441 (theory 0.02333)
```

블록 필터는 질의당 메모리 접근이 **캐시 라인 1개**(일반은 k개의 무작위 접근)라 빠르고, 필터가 플래시에 있으면 **페이지 하나만 읽으면** 된다. 대가는 FP: 블록마다 부하가 Poisson으로 흔들리고 FP는 부하에 대해 볼록이라(Jensen 또 등장) 평균이 올라간다 — 9비트 필드 버전이 Poisson 이론(0.00122, 0.02333)과 맞는다. 블록 안 더블 해싱은 m = 512짜리 작은 필터 안에서 등차수열을 쓰는 셈이라 ⑦의 바닥이 커져서 더 나쁘다(0.00208). 블록 안에서는 해시 비트를 잘라 쓰는 게 맞다. (Putze, Sanders, Singler 2007)

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 7/7 passed
```

## 10. 핵심 정리

- 블룸 필터 = m비트 + 키당 k비트. "없음"은 확실, "있음"은 아마. 거짓 음성 0은 **비트가 1→0으로 안 바뀐다**는 단조성에서 나온다 → 삭제 불가.
- 위치는 64비트 해시 하나로: `g_i = h1 + i·h2`, m = 2^b면 **h2 | 1** (홀수 = mod 2^b 가역 → k위치 서로 다름). FNV 뒤에는 fmix64.
- FP ≈ (1 − e^{−kn/m})^k. 주어진 필터에선 정확히 ρ^k, 교과서 식은 Jensen 때문에 약간 낙관적. 작은 m에선 필터마다 흔들린다.
- 최적: 비트 절반이 1일 때, **k = (m/n) ln 2, ε = 0.6185^{m/n}, m/n = 1.44 log₂(1/ε)**. 1% ↔ 키당 9.6비트, k = 7.
- 더블 해싱은 점근적으로만 같다: FP 바닥 ~ 2n/m²의 수 배. 아주 낮은 ε엔 enhanced/독립 해시.
- 실측(m ≥ 65536): 이론 ±5%, k 스캔 최소 = k_opt = 7. KV 세그먼트 필터 4 KB → 없는 키 플래시 읽기 16 → 0.33.
- 삭제: 카운팅(4비트, 포화 고정, 넣은 키만 지우기) / cuckoo filter / 재구축. 합집합 = OR(정확), 교집합 = AND(근사).
- 동시성: add는 단조 OR → `atomic_fetch_or` 또는 bit-band면 충분.

## 참고자료

- Burton H. Bloom, "Space/Time Trade-offs in Hash Coding with Allowable Errors", *Communications of the ACM* 13(7), 1970
- Andrei Broder & Michael Mitzenmacher, "Network Applications of Bloom Filters: A Survey", *Internet Mathematics* 1(4), 2004
- Adam Kirsch & Michael Mitzenmacher, "Less Hashing, Same Performance: Building a Better Bloom Filter", ESA 2006 (*Random Structures & Algorithms* 33(2), 2008)
- Peter C. Dillinger & Panagiotis Manolios, "Bloom Filters in Probabilistic Verification", FMCAD 2004 — enhanced double hashing
- Prosenjit Bose et al., "On the false-positive rate of Bloom filters", *Information Processing Letters* 108(4), 2008
- Li Fan, Pei Cao, Jussara Almeida, Andrei Broder, "Summary Cache: A Scalable Wide-Area Web Cache Sharing Protocol", *IEEE/ACM ToN* 8(3), 2000 — counting Bloom filter
- Bin Fan, David Andersen, Michael Kaminsky, Michael Mitzenmacher, "Cuckoo Filter: Practically Better Than Bloom", CoNEXT 2014
- Felix Putze, Peter Sanders, Johannes Singler, "Cache-, Hash- and Space-Efficient Bloom Filters", WEA 2007 — blocked Bloom filter
- Paulo Sérgio Almeida et al., "Scalable Bloom Filters", *Information Processing Letters* 101(6), 2007
- Benjamin Zhu, Kai Li, Hugo Patterson, "Avoiding the Disk Bottleneck in the Data Domain Deduplication File System", FAST 2008
- Michael Mitzenmacher & Eli Upfal, *Probability and Computing*, Ch. 5 (Balls, Bins, and Random Graphs — Bloom filters)
- [LevelDB `util/bloom.cc`](https://github.com/google/leveldb/blob/main/util/bloom.cc) · [LevelDB 문서 — Filters](https://github.com/google/leveldb/blob/main/doc/index.md)
- [Linux `kernel/bpf/bloom_filter.c` — BPF_MAP_TYPE_BLOOM_FILTER](https://github.com/torvalds/linux/blob/master/kernel/bpf/bloom_filter.c)
- 관련 레슨: DS-B08(비트맵), DS-B09(해시 함수·FNV-1a), DS-M08(해시 리사이즈), DS-A07(cuckoo 해싱), DS-A10(플래시 KV와 컴팩션)
