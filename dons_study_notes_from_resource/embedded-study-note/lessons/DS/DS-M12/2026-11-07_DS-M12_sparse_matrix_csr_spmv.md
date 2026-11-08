---
id: DS-M12
time: 75분
updated: 2026-11-07
---
# DS-M12 희소 행렬 CSR과 SpMV

## 학습 목표

- 희소 행렬(sparse matrix)을 **COO**(좌표 목록)와 **CSR**(Compressed Sparse Row)로 표현하고, `row_ptr / col_idx / val` 세 배열의 의미와 불변식을 말로 설명한다.
- COO → CSR 변환을 **counting sort(행별 개수 → 누적합 → 흩뿌리기)** 로 O(nnz + R)에 하고, **중복 좌표 합산 정책**(합산, 합이 0이면 버림, int32 넘침은 에러)을 코드와 테스트로 고정한다.
- CSR SpMV(`y = A x`)와 **CSR 전치(= CSC)** 를 구현하고, 전치 없이 `y = Aᵀ x` 를 흩뿌리기(scatter)로 계산한다.
- 512×512 행렬에서 **희소도별 메모리/시간**을 직접 재서, 밀집(dense)보다 CSR이 이득인 지점(break-even)을 숫자로 말한다.
- 가지치기(pruning)된 NN 가중치가 왜 CSR SpMV 문제이고, AI 가속기가 왜 비정형 희소성 대신 **구조적 희소성**(2:4, 블록)을 선호하는지 연결한다.

## 1. 문제

NN의 FC 레이어 `y = W x`, 그래프의 인접 행렬, 유한요소·회로 해석의 연립방정식은 대부분 원소가 0인 **희소 행렬**이다 — 0을 저장하지도 곱하지도 않는 CSR + SpMV는 AI 가속기·GPU 면접에서 "희소성(sparsity)을 어떻게 이용하나?"의 출발점이고, 펌웨어 쪽에선 "flash/SRAM 몇 KB에 가중치를 어떻게 넣나?"의 답이다.

용어부터. 행 R개, 열 C개, 0이 아닌 원소 **nnz**(number of non-zeros)개. 희소도(sparsity) = 1 − nnz/(R·C).

- **COO**(coordinate): `(row, col, val)` 트리플릿 목록. 순서 자유, 중복 허용 → **만들기(append) 쉬움**. 센서 로그, 유한요소 조립(assembly), 학습 중 기울기 누적에서 이 모양으로 데이터가 들어온다.
- **CSR**: 행 순서로 정렬해서 행 번호를 압축한다. `row_ptr[i] .. row_ptr[i+1]` 구간이 행 i의 원소들 → **곱하기(행 단위 순회) 빠름**. `row_ptr` 은 R+1칸, 마지막 칸이 nnz.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">예제 A (4 x 5) — 밀집 그림과 CSR 세 배열. 행 1 은 비어 있다</text>
  <g stroke="currentColor" fill="none">
    <rect x="40" y="50" width="200" height="140"/> <path d="M40 85 H240 M40 120 H240 M40 155 H240 M80 50 V190 M120 50 V190 M160 50 V190 M200 50 V190"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <rect x="40" y="50" width="40" height="35"/><rect x="160" y="50" width="40" height="35"/> <rect x="80" y="120" width="40" height="35"/><rect x="200" y="120" width="40" height="35"/> <rect x="40" y="155" width="40" height="35"/><rect x="120" y="155" width="40" height="35"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="60" y="72">10</text><text x="180" y="72">20</text><text x="100" y="142">30</text> <text x="220" y="142">40</text><text x="60" y="177">50</text><text x="140" y="177">60</text>
  </g>
  <g class="f-mut" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="100" y="72">0</text><text x="140" y="72">0</text><text x="220" y="72">0</text> <text x="60" y="107">0</text><text x="100" y="107">0</text><text x="140" y="107">0</text><text x="180" y="107">0</text><text x="220" y="107">0</text> <text x="60" y="142">0</text><text x="140" y="142">0</text><text x="180" y="142">0</text> <text x="100" y="177">0</text><text x="180" y="177">0</text><text x="220" y="177">0</text>
  </g>
  <g fill="currentColor" font-size="12"><text x="14" y="72">r0</text><text x="14" y="107">r1</text><text x="14" y="142">r2</text><text x="14" y="177">r3</text>
    <text x="54" y="44">c0</text><text x="94" y="44">c1</text><text x="134" y="44">c2</text><text x="174" y="44">c3</text><text x="214" y="44">c4</text></g>
  <text x="40" y="215" fill="currentColor">밀집: 20칸 x 4B = 80B</text>
  <text x="40" y="235" fill="currentColor">CSR: 5x4 + 6x(2+4) = 56B</text>
  <text x="260" y="78" fill="currentColor">row_ptr</text>
  <g stroke="currentColor" fill="none">
    <rect x="330" y="58" width="50" height="28"/><rect x="390" y="58" width="50" height="28"/><rect x="450" y="58" width="50" height="28"/> <rect x="510" y="58" width="50" height="28"/><rect x="570" y="58" width="50" height="28"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="355" y="77">0</text><text x="415" y="77">2</text><text x="475" y="77">2</text><text x="535" y="77">4</text><text x="595" y="77">6</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="355" y="52">[0]</text><text x="415" y="52">[1]</text><text x="475" y="52">[2]</text><text x="535" y="52">[3]</text><text x="595" y="52">[4]=nnz</text></g>
  <g stroke="currentColor" fill="none">
    <path d="M355 86 L332 158"/><path d="M415 86 L430 158"/><path d="M475 86 L432 158"/><path d="M535 86 L530 158"/><path d="M595 86 L628 158" stroke-dasharray="4 3"/>
  </g>
  <text x="260" y="180" fill="currentColor">col_idx</text>
  <text x="260" y="218" fill="currentColor">val</text>
  <g stroke="currentColor" class="f-acc">
    <rect x="330" y="162" width="50" height="28"/><rect x="380" y="162" width="50" height="28"/><rect x="430" y="162" width="50" height="28"/> <rect x="480" y="162" width="50" height="28"/><rect x="530" y="162" width="50" height="28"/><rect x="580" y="162" width="50" height="28"/>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="330" y="200" width="50" height="28"/><rect x="380" y="200" width="50" height="28"/><rect x="430" y="200" width="50" height="28"/> <rect x="480" y="200" width="50" height="28"/><rect x="530" y="200" width="50" height="28"/><rect x="580" y="200" width="50" height="28"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="355" y="181">0</text><text x="405" y="181">3</text><text x="455" y="181">1</text><text x="505" y="181">4</text><text x="555" y="181">0</text><text x="605" y="181">2</text> <text x="355" y="219">10</text><text x="405" y="219">20</text><text x="455" y="219">30</text><text x="505" y="219">40</text><text x="555" y="219">50</text><text x="605" y="219">60</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M332 236 V242 H428 V236"/><path d="M432 236 V242 H528 V236"/><path d="M532 236 V242 H628 V236"/></g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="380" y="258">행 0</text><text x="480" y="258">행 2</text><text x="580" y="258">행 3</text></g>
  <text x="10" y="284" fill="currentColor">행 1: row_ptr[1] = row_ptr[2] = 2 → 빈 구간 [2,2). 빈 행은 "같은 값 두 번"으로 공짜 표현</text>
</svg>
```

**정규형(canonical) CSR** 불변식 — 이 레슨의 모든 함수가 지키고, `csr_check()` 가 매 연산 후 검사한다:

1. `row_ptr[0] == 0`, `row_ptr` 단조 증가(non-decreasing), `row_ptr[R] == nnz ≤ cap`.
2. 각 행 안에서 `col_idx` 가 **엄격히** 증가 (정렬 + 중복 없음), 모든 `col_idx < C`.
3. 저장된 `val` 에 0이 없다.

정규형이면 같은 행렬의 CSR 표현이 **하나뿐**이라 배열을 `memcmp` 로 비교할 수 있다 — 테스트가 단순해진다. 타입: `row_ptr` 는 `uint32_t`, `col_idx` 는 `uint16_t`(열 ≤ 65536, 원소당 2B 절약), `val` 은 `int32_t`. 곱과 누적은 `int64_t` 라서 결과가 밀집 계산과 **비트 단위로 같아야** 하고, 테스트는 `==` 로 비교한다(부동소수점 비교는 연습 3에서 따로 다룬다). 자료구조에 malloc 없음 — 버퍼는 호출자가 준다(정적 배열 또는 flash의 const 테이블).

### Part A — COO → CSR (중복 합산), SpMV

- `coo_to_csr(in, n_in, R, C, out, work)` — counting sort로 행별 배치 → 행마다 열 기준 정렬 → 같은 좌표는 합산.
- **중복 정책(명시)**: 같은 `(row, col)` 은 **합산**한다(MATLAB `sparse(i,j,v)`, SciPy `coo.tocsr()`, PyTorch `coalesce()` 와 같은 관례 — FE 조립에서 요소 기여를 더하는 것). 합이 0이면 **저장하지 않는다**(SciPy는 explicit zero를 남긴다 — 정책 차이). 합이 int32 범위를 벗어나면 `CSR_EOVERFLOW` — 중간 합은 int64라 일시적으로 넘는 것은 괜찮고 **최종 합**만 검사한다.
- 에러(`ERANGE` 좌표 범위 밖, `ENOSPC` 용량 부족, `EOVERFLOW`) 시 `out` 은 **유효한 빈 행렬**로 남는다.
- `csr_spmv(A, x, y)` — `y[i] = Σ val[k]·x[col_idx[k]]`.
- 테스트: 예제(뒤섞임 + 중복 + 상쇄) 배열 정확 비교, 경계 4종, 랜덤 2000회를 밀집 누적 참조와 대조.

### Part B — 전치(= CSC)와 전치 곱

- `csr_transpose(A, T, work)` — **A의 CSC = Aᵀ의 CSR**. 열별 개수 → 누적합 → A를 행 순서로 돌며 흩뿌리기. 정렬 단계가 없는데도 결과가 정규형인 이유를 설명할 것.
- `csr_spmv_t(A, x, y)` — 전치를 만들지 않고 `y = Aᵀ x` (역전파의 `Wᵀ δ` 와 같은 모양).
- 테스트: 예제, nnz 0 / 1×N / N×1 / 용량 부족, 랜덤 1000회 — 밀집 전치와 일치, **두 번 전치 == 원본(배열까지)**, 두 가지 `Aᵀx` 가 밀집과 일치.

### Part C — 가지치기된 가중치 → CSR, 희소도별 메모리/시간

- `csr_from_dense_prune(W, R, C, thresh, out)` — magnitude pruning: `|w| > thresh` 만 남긴다(int64로 절댓값 — `abs(INT32_MIN)` 은 UB).
- 512×512 int32 가중치(|w|는 1..1000 균등)를 임계값 0/500/750/900/950/990/999로 가지쳐서 희소도 0~99.9%를 만들고, 각각 **메모리(바이트)** 와 **SpMV 시간 vs 밀집 GEMV 시간**을 출력한다. 결과 벡터는 매번 밀집과 `memcmp` 로 일치해야 한다.

## 2. Problem (English)

> **Sparse matrix in CSR form: build, multiply, transpose.**
>
> A CSR matrix has `row_ptr[R+1]`, `col_idx[nnz]` (`uint16_t`), `val[nnz]` (`int32_t`), with caller-provided buffers; no dynamic allocation. Canonical form: column indices strictly increasing within each row and no stored zeros.
>
> 1. `int coo_to_csr(const coo_t *in, uint32_t n, uint32_t R, uint32_t C, csr_t *out, uint32_t *work)` — input triplets are unsorted and may contain duplicates. Sum duplicates; drop entries whose sum is 0; return `CSR_EOVERFLOW` if a final sum does not fit in `int32_t`, `CSR_ERANGE` for out-of-range coordinates, `CSR_ENOSPC` if `n > out->cap`. On error, `out` must be a valid empty matrix. Target `O(n + R)` plus per-row sorting.
> 2. `void csr_spmv(const csr_t *A, const int32_t *x, int64_t *y)` — `y = A·x` in `O(R + nnz)`.
> 3. `int csr_transpose(const csr_t *A, csr_t *T, uint32_t *work)` — `T = Aᵀ` in canonical form, `O(R + C + nnz)`, no sort.
> 4. `void csr_spmv_t(const csr_t *A, const int32_t *x, int64_t *y)` — `y = Aᵀ·x` without building `Aᵀ`.
> 5. `int csr_from_dense_prune(const int32_t *W, uint32_t R, uint32_t C, int32_t t, csr_t *out)` — keep `|w| > t`.
>
> Example: triplets `(2,4,40) (0,3,15) (3,0,50) (0,0,10) (2,1,30) (0,3,5) (3,2,60) (1,2,7) (1,2,-7)` on a 4×5 matrix → `row_ptr = [0,2,2,4,6]`, `col_idx = [0,3,1,4,0,2]`, `val = [10,20,30,40,50,60]`. With `x = [1,2,3,4,5]`, `y = [90, 0, 260, 230]`.
> Follow-up: at what sparsity does CSR beat dense in memory? In time? Why are the two answers different?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "4/20 passed", exit 1 — TODO 5곳을 채워 20/20 을 만든다
```

TODO: `coo_to_csr`, `csr_spmv` (Part A) / `csr_transpose`, `csr_spmv_t` (Part B) / `csr_from_dense_prune` (Part C). `csr_check()`(정규형 불변식), `csr_equals_dense()`, `csr_same()`, 밀집 참조 `dense_gemv()`, xorshift32, 측정 루프는 주어진다. 스텁은 "빈 행렬 / y = 0" 을 돌려줘서 크래시 없이 FAIL만 난다(그래서 starter의 Part C 표는 전부 nnz 0으로 찍힌다).

## 4. 힌트

<details>
<summary>힌트 1 — row_ptr 는 "행별 개수의 누적합"</summary>

행 r의 원소 개수를 `row_ptr[r+1]` 에 세어 두고(한 칸 오른쪽!), `row_ptr[r+1] += row_ptr[r]` 를 r = 0..R−1 로 돌리면 `row_ptr[r]` = 행 r보다 앞에 있는 원소 수 = 행 r의 시작 위치가 된다. 히스토그램 → 누적합 → 위치. counting sort 그 자체다.

</details>

<details>
<summary>힌트 2 — 흩뿌리기에는 커서 사본이 필요</summary>

원소를 행 r의 다음 빈칸에 넣으려면 "행 r에 몇 개 넣었나" 커서가 있어야 한다. `row_ptr` 자체를 커서로 쓰면 끝나고 나서 시작 위치가 사라진다 → `work[r] = row_ptr[r]` 사본을 커서로 쓴다(버퍼 없이 하는 시프트 트릭은 연습 2). 입력 순서대로 돌면 같은 행 안에서는 입력 순서가 유지된다(안정, stable).

</details>

<details>
<summary>힌트 3 — 정렬·합산·0 제거를 한 번에, 제자리에서</summary>

행 r 구간 `[start, end)` 를 열 기준 삽입 정렬하면 같은 열이 붙는다. 그 다음 읽기 포인터 k, 쓰기 포인터 w로 같은 열을 int64로 합쳐 0이 아니면 `w` 에 쓴다. w ≤ k가 항상 성립(앞에서부터 압축)하므로 덮어써도 아직 안 읽은 칸을 망치지 않는다. 주의: 새 시작 `row_ptr[r] = w` 를 쓰기 **전에** 옛 `row_ptr[r+1]`(= end)을 읽어 둬야 한다.

</details>

<details>
<summary>힌트 4 — 전치는 "열 기준 counting sort"</summary>

T의 행 = A의 열. 열별 개수 → 누적합 → 커서. A를 **행 0부터 순서대로** 돌며 `(i, val)` 을 T의 행 `col_idx[k]` 에 넣으면, T의 각 행에는 i가 증가하는 순서로 들어간다 → 정렬이 공짜.

</details>

<details>
<summary>힌트 5 — Aᵀx 는 gather 가 아니라 scatter</summary>

`y = Aᵀ x` 의 `y[c] = Σ_i A[i][c]·x[i]`. CSR은 열 c를 모아 볼 수 없으니 거꾸로: 행 i의 원소 `(c, v)` 마다 `y[c] += v·x[i]`. y를 먼저 0으로 채우는 것을 잊지 말 것. 곱은 `int64` 로 넓힌 뒤에.

</details>

## 5. 풀이

### Part A — COO → CSR: counting sort 4단계

예제 COO 9개(뒤섞임, (0,3) 중복 15+5, (1,2) 상쇄 7−7)를 따라가 보자.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">COO 9개 → CSR: 개수 → 누적합 → 흩뿌리기 → 행별 정렬 → 합산·압축</text>
  <g fill="currentColor"><text x="10" y="52">① 개수 rp[r+1]</text><text x="10" y="92">② 누적합 rp</text>
    <text x="10" y="142">③ scatter</text><text x="10" y="158" font-size="12">(work 커서)</text> <text x="10" y="212">④ 행별 정렬</text><text x="10" y="282">⑤ 합산·0 제거</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="170" y="34" width="60" height="26"/><rect x="230" y="34" width="60" height="26"/><rect x="290" y="34" width="60" height="26"/><rect x="350" y="34" width="60" height="26"/><rect x="410" y="34" width="60" height="26"/> <rect x="170" y="74" width="60" height="26"/><rect x="230" y="74" width="60" height="26"/><rect x="290" y="74" width="60" height="26"/><rect x="350" y="74" width="60" height="26"/><rect x="410" y="74" width="60" height="26"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="200" y="52">0</text><text x="260" y="52">3</text><text x="320" y="52">2</text><text x="380" y="52">2</text><text x="440" y="52">2</text> <text x="200" y="92">0</text><text x="260" y="92">3</text><text x="320" y="92">5</text><text x="380" y="92">7</text><text x="440" y="92">9</text>
  </g>
  <text x="485" y="52" fill="currentColor" font-size="12">행0: 3개, 행1~3: 2개씩</text>
  <text x="485" y="92" fill="currentColor" font-size="12">행 r 의 칸 = [rp[r], rp[r+1])</text>
  <g stroke="currentColor" fill="none">
    <rect x="170" y="126" width="60" height="30"/><rect x="230" y="126" width="60" height="30"/><rect x="290" y="126" width="60" height="30"/> <rect x="350" y="126" width="60" height="30" class="f-mut"/><rect x="410" y="126" width="60" height="30" class="f-mut"/> <rect x="470" y="126" width="60" height="30"/><rect x="530" y="126" width="60" height="30"/> <rect x="590" y="126" width="60" height="30" class="f-mut"/><rect x="650" y="126" width="60" height="30" class="f-mut"/> <rect x="170" y="196" width="60" height="30"/><rect x="230" y="196" width="60" height="30" class="f-warn"/><rect x="290" y="196" width="60" height="30" class="f-warn"/> <rect x="350" y="196" width="60" height="30" class="f-warn"/><rect x="410" y="196" width="60" height="30" class="f-warn"/> <rect x="470" y="196" width="60" height="30"/><rect x="530" y="196" width="60" height="30"/> <rect x="590" y="196" width="60" height="30"/><rect x="650" y="196" width="60" height="30"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="200" y="146">3,15</text><text x="260" y="146">0,10</text><text x="320" y="146">3,5</text><text x="380" y="146">2,7</text><text x="440" y="146">2,-7</text> <text x="500" y="146">4,40</text><text x="560" y="146">1,30</text><text x="620" y="146">0,50</text><text x="680" y="146">2,60</text> <text x="200" y="216">0,10</text><text x="260" y="216">3,15</text><text x="320" y="216">3,5</text><text x="380" y="216">2,7</text><text x="440" y="216">2,-7</text> <text x="500" y="216">1,30</text><text x="560" y="216">4,40</text><text x="620" y="216">0,50</text><text x="680" y="216">2,60</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="260" y="174">행 0</text><text x="410" y="174">행 1</text><text x="530" y="174">행 2</text><text x="650" y="174">행 3</text></g>
  <g stroke="currentColor" fill="none"><path d="M172 162 V166 H348 V162"/><path d="M352 162 V166 H468 V162"/><path d="M472 162 V166 H588 V162"/><path d="M592 162 V166 H708 V162"/></g>
  <g stroke="currentColor" class="f-acc">
    <rect x="170" y="264" width="60" height="30"/><rect x="230" y="264" width="60" height="30"/><rect x="290" y="264" width="60" height="30"/> <rect x="350" y="264" width="60" height="30"/><rect x="410" y="264" width="60" height="30"/><rect x="470" y="264" width="60" height="30"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="200" y="284">0,10</text><text x="260" y="284">3,20</text><text x="320" y="284">1,30</text><text x="380" y="284">4,40</text><text x="440" y="284">0,50</text><text x="500" y="284">2,60</text>
  </g>
  <text x="545" y="284" fill="currentColor" font-size="12">rp = [0,2,2,4,6]</text>
  <text x="10" y="246" fill="currentColor" font-size="12">강조: 같은 열끼리 붙음 → (3,15)+(3,5)=20 저장, (2,7)+(2,-7)=0 버림 → 행 1 빈 행</text>
  <text x="10" y="318" fill="currentColor" font-size="12">칸 표기 = (col,val). 9칸 → 6칸으로 앞에서부터 제자리 압축 (쓰기 w ≤ 읽기 k)</text>
</svg>
```

**③ 흩뿌리기 추적** (`work` = `[0,3,5,7]` 에서 시작):

| k | 입력 (r,c,v) | 칸 p = work[r]++ | work 이후 |
|---|---|---|---|
| 0 | (2,4,40) | 5 | [0,3,6,7] |
| 1 | (0,3,15) | 0 | [1,3,6,7] |
| 2 | (3,0,50) | 7 | [1,3,6,8] |
| 3 | (0,0,10) | 1 | [2,3,6,8] |
| 4 | (2,1,30) | 6 | [2,3,7,8] |
| 5 | (0,3,5) | 2 | [3,3,7,8] |
| 6 | (3,2,60) | 8 | [3,3,7,9] |
| 7 | (1,2,7) | 3 | [3,4,7,9] |
| 8 | (1,2,−7) | 4 | [3,5,7,9] |

끝나면 `work[r] == rp[r+1]` — 각 행이 정확히 자기 칸을 다 채웠다는 확인이다.

**④⑤ 압축 루프의 순서**가 핵심 함정이다. 행 r을 처리할 때 `end = rp[r+1]`(옛 값)을 **먼저** 읽고, 그 다음 `rp[r] = w`(새 시작)를 쓴다. `rp[r+1]` 은 다음 반복에서 덮어쓰니 아직 안전하다. 모든 행이 끝나면 `rp[R] = w = nnz`.

```text
r=0: end=3, rp[0]=0, [0,3) 정렬 → (0,10)(3,15)(3,5) → 쓰기 (0,10)(3,20)      w=2
r=1: end=5, rp[1]=2, [3,5) → (2,7)(2,-7) 합 0 → 버림                        w=2
r=2: end=7, rp[2]=2, [5,7) 정렬 → (1,30)(4,40)                              w=4
r=3: end=9, rp[3]=4, [7,9) → (0,50)(2,60)                                   w=6
rp[4]=6
```

**정렬 선택**: 행 안 삽입 정렬은 행 길이 L에 대해 최악 O(L²) — 행이 짧으면(가지친 NN, FE 메쉬: 행당 수~수십 개) 가장 빠르고 코드가 작다. 한 행이 수천 개면 무너진다(연습 1에서 실측: 4000개 역순 한 행에 이동 3,972,496번). **결정적 O(nnz + R + C)** 가 필요하면 열 기준 → 행 기준 두 번의 안정 counting(LSD radix)으로 정렬한다 — 이동 정확히 2·nnz번. 메모리는 COO 사본 하나가 더 든다. 둘 다 비교 기반 정렬 하한 O(n log n)과 무관한 이유는 키(행·열 번호)가 작은 정수 범위라서다.

**용량 조건 `cap ≥ n_in`**: 흩뿌리기는 합산 **전** 원소 수만큼 칸이 필요하다. 최종 nnz가 작아도 그렇다. 미리 정확한 nnz를 알려면 정렬해서 세야 하므로, "입력 수만큼 잡는다"가 단순하고 안전하다.

**넘침 정책**: 합은 int64로 한다. 원소 하나가 |v| ≤ 2³¹이고 n_in < 2³²이므로 |합| < 2⁶³ — int64가 절대 넘치지 않는다. 그래서 `INT32_MAX + INT32_MAX − INT32_MAX` 처럼 **중간에** int32를 넘어도 최종이 맞으면 OK(테스트 있음), 최종이 넘치면 `EOVERFLOW` + 빈 행렬.

**SpMV**는 행마다 내적 하나다: `y[i] = Σ_{k∈[rp[i],rp[i+1])} val[k] · x[col_idx[k]]`. 정확히 nnz번 곱하고, 빈 행은 루프가 0번 돌아 `y[i] = 0`. 곱 전에 `(int64_t)` 로 넓혀야 한다 — `int32 * int32` 는 int32에서 계산되고 넘치면 **UB**다.

랜덤 테스트 실제 출력 (값 범위 [−3,3] 이라 중복·상쇄·0 입력이 많다):

```text
[A] random 2000 COO->CSR + SpMV vs dense: match (dup hits=140539, cancel-to-0=15261)
```

`dup hits` 는 이미 0이 아닌 칸에 다시 떨어진 입력 수, `cancel-to-0` 은 그 결과 합이 정확히 0이 된 횟수다 — 중복·상쇄 경로가 실제로 수만 번 실행됐다.

### Part B — 전치 = CSC, 정렬이 공짜인 이유

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">csr_transpose: A 의 열 c → T 의 행 c. A 를 행 순서로 돌며 흩뿌린다</text>
  <g stroke="currentColor" fill="none">
    <rect x="30" y="45" width="200" height="140"/> <path d="M30 80 H230 M30 115 H230 M30 150 H230 M70 45 V185 M110 45 V185 M150 45 V185 M190 45 V185"/>
  </g>
  <g class="f-acc" stroke="currentColor"><rect x="30" y="45" width="40" height="35"/><rect x="30" y="150" width="40" height="35"/></g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="50" y="67">10</text><text x="170" y="67">20</text><text x="90" y="137">30</text><text x="210" y="137">40</text><text x="50" y="172">50</text><text x="130" y="172">60</text>
  </g>
  <text x="30" y="205" fill="currentColor" font-size="12">강조 = A 의 열 0 → T 의 행 0 = [(0,10),(3,50)]</text>
  <text x="270" y="60" fill="currentColor">T.row_ptr</text>
  <g stroke="currentColor" fill="none">
    <rect x="350" y="42" width="50" height="26"/><rect x="400" y="42" width="50" height="26"/><rect x="450" y="42" width="50" height="26"/> <rect x="500" y="42" width="50" height="26"/><rect x="550" y="42" width="50" height="26"/><rect x="600" y="42" width="50" height="26"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="375" y="60">0</text><text x="425" y="60">2</text><text x="475" y="60">3</text><text x="525" y="60">4</text><text x="575" y="60">5</text><text x="625" y="60">6</text>
  </g>
  <text x="270" y="112" fill="currentColor">T.col_idx</text><text x="270" y="146" fill="currentColor">T.val</text><text x="270" y="180" fill="currentColor">채운 순서</text>
  <g stroke="currentColor" fill="none">
    <rect x="350" y="94" width="50" height="26" class="f-acc"/><rect x="400" y="94" width="50" height="26" class="f-acc"/><rect x="450" y="94" width="50" height="26"/> <rect x="500" y="94" width="50" height="26"/><rect x="550" y="94" width="50" height="26"/><rect x="600" y="94" width="50" height="26"/> <rect x="350" y="128" width="50" height="26" class="f-acc"/><rect x="400" y="128" width="50" height="26" class="f-acc"/><rect x="450" y="128" width="50" height="26"/> <rect x="500" y="128" width="50" height="26"/><rect x="550" y="128" width="50" height="26"/><rect x="600" y="128" width="50" height="26"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="375" y="112">0</text><text x="425" y="112">3</text><text x="475" y="112">2</text><text x="525" y="112">3</text><text x="575" y="112">0</text><text x="625" y="112">2</text> <text x="375" y="146">10</text><text x="425" y="146">50</text><text x="475" y="146">30</text><text x="525" y="146">60</text><text x="575" y="146">20</text><text x="625" y="146">40</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="375" y="180">1번째</text><text x="425" y="180">5번째</text><text x="475" y="180">3번째</text><text x="525" y="180">6번째</text><text x="575" y="180">2번째</text><text x="625" y="180">4번째</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="270" y="222">원소를 A 의 행 0 → 2 → 3 순서로 꺼내므로, T 의 한 행(= A 의 한 열) 안에는</text> <text x="270" y="240">항상 작은 행 번호 i 가 먼저 들어간다 → T.col_idx 가 행마다 자동 오름차순.</text> <text x="270" y="258">예: T 행 0 은 A(0,0)=10 을 1번째, A(3,0)=50 을 5번째에 받는다.</text>
  </g>
</svg>
```

**왜 전치 = CSC 인가**: CSC(Compressed Sparse Column)는 같은 세 배열을 "열 기준"으로 둔 것이다 — `col_ptr`, `row_idx`, `val`. Aᵀ의 행은 A의 열이므로, Aᵀ를 CSR로 저장한 배열과 A를 CSC로 저장한 배열은 **비트 단위로 같다**. 그래서 라이브러리들은 "CSR→CSC 변환"과 "전치"를 같은 함수로 구현한다(SciPy `sparsetools/csr.h` 의 `csr_tocsc`).

**정규형 증명**: 원소 (i, c)는 T의 행 c에, A를 도는 순서대로 쌓인다. 바깥 루프가 i 오름차순이므로 T 행 c의 i값은 엄격 증가(같은 (i,c)가 A에 두 번 없으니 엄격). 값은 A에서 0이 아니었으니 그대로 0이 아님. → 두 번 전치하면 A로 **배열까지** 똑같이 돌아온다. 테스트가 `csr_same(A, transpose(transpose(A)))` 를 1000번 확인한다.

**두 가지 `Aᵀx`**:

| 방법 | 접근 패턴 | 추가 메모리 | 병렬화 |
|---|---|---|---|
| ① 전치 T를 만들고 `csr_spmv(T, x)` | x는 gather(읽기 랜덤), y는 순차 쓰기 | T 전체 (nnz·6 + 4(C+1) B) | 행별로 독립 → 경쟁 없음 |
| ② `csr_spmv_t(A, x)` scatter | x는 순차 읽기, y는 랜덤 **누적 쓰기** | 0 | 두 행이 같은 `y[c]` 에 동시에 더함 → 원자 연산/분할 필요 |

한 번만 하면 ②, 여러 번(반복 해법, 학습 루프)이면 ①이 낫다. 정수라 두 결과가 정확히 같아야 하고, 실제로 같다:

```text
[B] random 1000 transpose / double-transpose / A^T x: match
```

참고로 이 구현에서는 float이었어도 ①과 ②의 합산 순서가 같다 — 둘 다 행 i 오름차순으로 `y[c]` 에 더하기 때문. 순서가 바뀌는 건 SIMD/MAC 어레이처럼 **부분합으로 나눌 때**다(연습 3).

**복잡도**: 전치 O(R + C + nnz) 시간, `work` C칸. COO→CSR O(n_in + R + Σ L_r²)(삽입 정렬) 또는 O(n_in + R + C)(radix). SpMV O(R + nnz) — 행이 비어도 `row_ptr` 두 칸은 읽으므로 R 항이 붙는다.

### Part C — 가지치기와 희소도별 메모리/시간

`csr_from_dense_prune` 는 COO를 거치지 않는다 — 밀집을 행 순서로 훑으면 이미 행·열 정렬 순서라 바로 CSR이 된다. 미리 nnz를 몰라도 `w == cap` 이면 `ENOSPC` 로 멈춘다(한 번 세고 한 번 쓰는 2-pass도 가능).

**메모리 공식** (R×C, 희소도 s, nnz = (1−s)·R·C):

| 표현 | 바이트 | 밀집보다 작아지는 조건 |
|---|---|---|
| 밀집 int32 | 4·R·C | — |
| CSR (int32 val + u16 col) | 4(R+1) + 6·nnz | 밀도 < 2/3 (희소도 > 33%) |
| CSR (int32 val + u32 col) | 4(R+1) + 8·nnz | 밀도 < 1/2 |
| CSR (int8 val + u16 col) vs 밀집 int8 | 4(R+1) + 3·nnz vs R·C | 밀도 < 1/3 (희소도 > 67%) |

양자화(int8)할수록 인덱스 오버헤드 비중이 커져서 CSR이 손해 보는 구간이 넓어진다 — 가속기들이 **상대 인덱스 4비트**나 **비트마스크** 같은 더 작은 메타데이터를 쓰는 이유다.

**실측** — `./solution` 의 Part C 출력 그대로 (이 Mac, Apple Silicon, `-O2`. 각 시간은 200회 반복 × 5배치 중 최소 배치의 1회 평균. 실행마다 ±10~20% 흔들리니 **비율**을 볼 것):

```text
[C] 512x512 int32, sparsity | nnz    | dense B | CSR B   | CSR/dense | dense ns | CSR ns  | speedup
[C]            0.0%        | 262144 | 1048576 | 1574916 |    1.502  |    12400 |   60525 |   0.20x
[C]           50.0%        | 131178 | 1048576 |  789120 |    0.753  |    12135 |   29770 |   0.41x
[C]           74.9%        |  65807 | 1048576 |  396894 |    0.379  |    12095 |   15400 |   0.79x
[C]           90.0%        |  26246 | 1048576 |  159528 |    0.152  |    11865 |    5865 |   2.02x
[C]           95.0%        |  13048 | 1048576 |   80340 |    0.077  |    13970 |    3460 |   4.04x
[C]           99.0%        |   2609 | 1048576 |   17706 |    0.017  |    12465 |     830 |  15.02x
[C]           99.9%        |    245 | 1048576 |    3522 |    0.003  |    14080 |     290 |  48.55x
```

같은 코드를 **자동 벡터화를 끄고**(`-O2 -fno-vectorize -fno-slp-vectorize`, SIMD 없는 MCU 흉내) 돌린 실제 출력:

```text
[C] 512x512 int32, sparsity | nnz    | dense B | CSR B   | CSR/dense | dense ns | CSR ns  | speedup
[C]            0.0%        | 262144 | 1048576 | 1574916 |    1.502  |    65190 |   74360 |   0.88x
[C]           50.0%        | 131178 | 1048576 |  789120 |    0.753  |    62925 |   39340 |   1.60x
[C]           74.9%        |  65807 | 1048576 |  396894 |    0.379  |    63330 |   19715 |   3.21x
[C]           90.0%        |  26246 | 1048576 |  159528 |    0.152  |    63120 |    8175 |   7.72x
[C]           95.0%        |  13048 | 1048576 |   80340 |    0.077  |    64595 |    3555 |  18.17x
[C]           99.0%        |   2609 | 1048576 |   17706 |    0.017  |    63215 |    1030 |  61.37x
[C]           99.9%        |    245 | 1048576 |    3522 |    0.003  |    63785 |     240 | 265.77x
```

```svg
<svg viewBox="0 0 740 350" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">CSR 시간 / 밀집 시간 (짧을수록 CSR 이득). 세로선 = 1.0 (밀집과 같음)</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="52">희소도 0%</text><text x="10" y="90">50%</text><text x="10" y="128">74.9%</text><text x="10" y="166">90%</text> <text x="10" y="204">95%</text><text x="10" y="242">99%</text><text x="10" y="280">99.9%</text>
  </g>
  <g stroke="currentColor">
    <rect x="110" y="38" width="439" height="13" class="f-warn"/><rect x="110" y="53" width="103" height="13" class="f-warn"/> <rect x="110" y="76" width="221" height="13" class="f-warn"/><rect x="110" y="91" width="56" height="13" class="f-ok"/> <rect x="110" y="114" width="115" height="13" class="f-warn"/><rect x="110" y="129" width="28" height="13" class="f-ok"/> <rect x="110" y="152" width="44" height="13" class="f-ok"/><rect x="110" y="167" width="12" height="13" class="f-ok"/> <rect x="110" y="190" width="22" height="13" class="f-ok"/><rect x="110" y="205" width="5" height="13" class="f-ok"/> <rect x="110" y="228" width="6" height="13" class="f-ok"/><rect x="110" y="243" width="2" height="13" class="f-ok"/> <rect x="110" y="266" width="2" height="13" class="f-ok"/><rect x="110" y="281" width="1" height="13" class="f-ok"/>
  </g>
  <path d="M200 30 V300" stroke="currentColor" stroke-dasharray="5 4" fill="none"/>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="555" y="49">NEON 4.88</text><text x="219" y="64">scalar 1.14</text> <text x="337" y="87">2.45</text><text x="172" y="102">0.63</text> <text x="231" y="125">1.27</text><text x="144" y="140">0.31</text> <text x="160" y="163">0.49</text><text x="128" y="178">0.13</text> <text x="138" y="201">0.25</text><text x="121" y="216">0.055</text> <text x="122" y="239">0.067</text><text x="118" y="254">0.016</text> <text x="118" y="277">0.021</text><text x="117" y="292">0.004</text>
  </g>
  <text x="10" y="318" fill="currentColor" font-size="12">각 희소도에서 위 막대 = -O2 기본(NEON 벡터화), 아래 막대 = -fno-vectorize. 막대 90px = 1.0.</text>
  <text x="10" y="338" fill="currentColor" font-size="12">경고색 = CSR 이 더 느림(&gt; 1), 초록 = 더 빠름. 시간 break-even: NEON 75~90%, scalar 0~50%</text>
</svg>
```

읽는 법:

- **밀집 시간은 희소도와 무관**(~12 µs, 0을 곱해도 같은 일). CSR 시간은 nnz에 **비례**(0%→99.9%에서 60525→290 ns, nnz 비율 1070배에 시간 209배 — 작은 nnz에선 `row_ptr` 513칸 순회 같은 고정비가 보인다).
- **메모리 break-even ≠ 시간 break-even.** 메모리는 33% 희소도에서 이미 이득(공식대로: 50%에서 0.753배). 시간은 NEON에서 **75%와 90% 사이**에서야 이득. 원소당 CSR은 `col_idx` 2B + `val` 4B를 읽고 `x[col]` 을 **간접(gather) 로드**한다 — 밀집은 연속 로드라 SIMD로 한 명령에 여러 MAC을 한다. 0% 희소도에서 CSR이 4.9배 느린 게 그 비용이다.
- **SIMD를 끄면 break-even이 0~50%로 내려간다**(0%에서 0.88배, 50%에서 1.60배). 밀집 쪽이 SIMD 이득을 잃으면 CSR의 간접 접근 비용이 상대적으로 작아진다. 즉 "희소 행렬이 빨라지는 희소도"는 **하드웨어가 밀집을 얼마나 잘 하는지**로 정해진다 — SIMD 없는 Cortex-M0/M3급에서는 낮은 희소도에서도 이득일 가능성이 크고, 밀집 MAC 어레이를 가진 NPU/GPU에서는 아주 높은 희소도에서만 이득이다. (MCU 숫자 자체는 이 Mac에서 잰 게 아니므로 직접 재야 한다.)

**펌웨어 연결**:

- **flash 상주 가중치**: 가지친 모델은 호스트에서 CSR로 바꿔 `static const uint32_t row_ptr[] = {...}` 형태로 내보내면 Cortex-M에서 `.rodata`(flash)에 놓인다 — SRAM 0바이트, 런타임 변환 없음. 이 레슨의 `csr_t` 가 포인터 3개만 들고 있는 이유.
- **결정적 시간**: SpMV 시간은 값이 아니라 nnz와 R에만 의존한다. 모델이 고정이면 nnz도 고정 → WCET가 상수. 반면 행 길이가 불균등하면 행 단위로 나눈 병렬 작업의 최장 조각이 길어진다(연습 5).
- **SSD/FTL 연결**: 거대한 LBA 공간 중 쓰인 곳만 매핑하는 FTL 매핑 테이블, 블록별 valid page 비트맵도 "대부분 비어 있는 2차원 표"를 압축하는 같은 문제다. CSR의 `row_ptr` 는 "블록 번호 → 그 블록의 엔트리 구간" 인덱스와 같다.

**see:EML — 가지친 NN 가중치와 AI 가속기 관점.** FC 레이어와 1×1 conv는 `y = W x`(배치면 GEMM)이고, magnitude pruning 후의 W가 바로 이 레슨의 `csr_from_dense_prune` 결과다. 위 표가 보여주듯 **비정형(unstructured) 희소성은 CPU에서도 75~90% 이상이어야 빨라지고**, 밀집 MAC 어레이를 가진 가속기에서는 더 불리하다 — 간접 인덱싱(gather), 행 길이 불균형으로 인한 PE(processing element) 부하 불균형, 인덱스 디코드 로직이 밀집 데이터패스의 규칙성을 깬다. 그래서 하드웨어는 **구조적 희소성**을 택한다: NVIDIA Ampere의 2:4(4개 중 정확히 2개 0, 값마다 2비트 위치 메타데이터 → sparse Tensor Core가 최대 2배 MAC 처리량), 블록 희소성(BSR — 블록 안은 밀집). EIE(Han et al., ISCA 2016)처럼 비정형을 하드웨어로 직접 다루는 설계는 CSC 변형 + 4비트 상대 인덱스로 메타데이터를 줄이고, **열 단위(CSC)** 라서 ReLU로 0이 된 활성화 x_j의 열 전체를 건너뛴다(가중치 희소성 × 활성화 희소성). 또한 가속기에서 병목은 대개 연산이 아니라 **DRAM 대역폭**이라, 계산이 안 빨라져도 가중치 바이트를 줄이는 것만으로 이득일 수 있다 — 위 메모리 공식(int8에서 밀도 < 1/3이어야 CSR이 작다)이 그 계산이다. 정확도-희소도 곡선과 break-even 측정은 **EML-A06**, 2:4 압축 포맷과 희소 MAC 모델은 **NPU-A01**, GPU SpMV 부하 분산은 **GPGPU-A05**, CSR 기반 CG 반복 해법은 **NUM-M10** 에서 이어진다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 20/20 passed
# SIMD 없는 경우 흉내 (Part C 표 비교용)
cc -std=c11 -Wall -Wextra -Werror -O2 -fno-vectorize -fno-slp-vectorize solution.c -o solution_nv && ./solution_nv
# 메모리 오류 / UB 확인
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `s += A->val[k] * x[c];` (s는 int64) | 곱이 **int32에서** 계산된 뒤 넓혀진다. 넘치면 signed overflow = UB | `s += (int64_t)A->val[k] * x[c];` |
| 개수를 `rp[r]++` 로 센 뒤 누적합 | 누적합이 "행 r까지 포함한 개수" = 행 r의 **끝**이 되어 한 칸씩 밀린다 | `rp[r + 1]++` 로 세고 `rp[r+1] += rp[r]` |
| 압축 루프에서 행 구간을 `rp[r] .. rp[r+1]` 로 다시 읽음 | `rp[r]` 은 방금 새 시작 w(≤ 옛 시작)로 덮었다 → 이미 압축된 칸을 다시 읽어 중복 합산 | 옛 경계를 변수로: `end = rp[r+1]` 읽고 → `rp[r] = w` → 처리 → `start = end` |
| `row_ptr` 를 R칸으로 잡음 | 마지막 행의 끝(`rp[R]` = nnz)이 없어 마지막 행을 못 돈다. 빈 matrix는 `rp[0]` 만 필요 | R+1칸. `rp[R] == nnz` 를 불변식으로 |
| 용량 검사를 최종 nnz로 | 흩뿌리기 단계는 중복 합산 **전** n_in칸을 쓴다 → 버퍼 넘침 | `if (n_in > out->cap) return CSR_ENOSPC;` 를 흩뿌리기 전에 |
| COO 입력이 정렬돼 있다고 가정 | 센서 로그·FE 조립 순서는 임의. 정렬 안 하면 정규형 아님 → 이진 탐색(연습 4)·전치 비교가 깨짐 | 행 counting + 행 안 정렬 (또는 2-pass radix) |
| 중복을 "마지막 값으로 덮어쓰기" | 관례(MATLAB/SciPy/FE)는 합산. 정책이 다르면 같은 입력에서 다른 행렬 | 정책을 문서화하고 테스트로 고정 (이 레슨: 합산, 0이면 제거) |
| `abs(W[i])` 로 가지치기 | `abs(INT32_MIN)` 은 표현 불가 → UB (테스트에 INT32_MIN 넣어 둠) | `int64_t a = W[i]; if ((a < 0 ? -a : a) > t)` |
| `csr_spmv_t` 에서 y 초기화 누락 | `+=` 만 하므로 이전 호출의 쓰레기가 남는다 | `for (c) y[c] = 0;` 먼저 |
| 열 수 > 65536에 `uint16_t col_idx` | `(uint16_t)col` 이 조용히 잘려 다른 열에 곱해진다 | `CSR_MAX_DIM` 검사, 크면 `uint32_t` 인덱스 |
| float SpMV를 순서 바꾼 구현과 `==` 비교 | 부동소수점 덧셈은 결합법칙이 안 맞는다 → 비트가 다를 수 있음 | 순서가 같으면 `==`, 다르면 abs(a−b) ≤ 2γₙ·Σ abs(aᵢⱼxⱼ) (연습 3) |

## 8. 면접 Q&A

<details>
<summary>Q1. COO, CSR, CSC, ELL, BSR — 각각 언제 쓰나요?</summary>

**COO**: 추가(append)가 O(1), 순서·중복 자유 → 행렬을 **만드는** 단계(조립, 스트리밍 입력). **CSR**: 행 단위 접근, `Ax` 가 행별 내적이라 쓰기 경합이 없다 → SpMV·행 슬라이싱의 기본. **CSC**: 열 단위 접근 → `Aᵀx`, 직접 해법(sparse Cholesky/LU)의 열 연산, 입력 벡터가 희소할 때(0인 x_j의 열 건너뛰기). **ELL(ELLPACK)**: 모든 행을 최대 행 길이로 패딩한 2차원 배열 → SIMD/GPU에 규칙적이지만 행 길이가 들쭉날쭉하면 패딩 낭비. **BSR**: 작은 밀집 블록 단위 CSR → 블록 안은 SIMD/MAC 어레이로, 인덱스 오버헤드는 블록당 하나. 꼬리질문 "변환 비용은?" → COO→CSR, CSR↔CSC 모두 counting으로 O(nnz + 차원).

</details>

<details>
<summary>Q2. COO → CSR 변환 복잡도는? 비교 정렬을 쓰면 안 되나요?</summary>

행 기준 counting이 O(n + R)이고, 행 안 정렬이 추가된다. 비교 정렬(qsort)을 전체에 쓰면 O(n log n)인데, 키가 [0, R) × [0, C)의 작은 정수라 **열 → 행 두 번의 안정 counting(LSD radix)** 으로 O(n + R + C)에 끝난다(연습 1, 이동 정확히 2n번). 실무에선 행이 짧으면 행 안 삽입 정렬이 메모리도 적고 빠르다. 중복 합산은 정렬 후 인접한 같은 열을 합치는 선형 패스. 꼬리질문 "메모리는?" → 커서 배열 R칸(또는 연습 2의 시프트 트릭으로 0) + radix면 COO 사본 n칸.

</details>

<details>
<summary>Q3. CSR SpMV는 왜 메모리 대역폭 한계(memory-bound)인가요? 어떻게 빠르게 하나요?</summary>

원소 하나당 MAC 1번(2 연산)에 `val` 4B + `col_idx` 2B를 스트리밍으로 읽고 `x[col]` 을 간접으로 읽는다 → 연산 강도(arithmetic intensity)가 바이트당 약 0.2~0.33 연산(x가 캐시에 있으면 2/6, 없으면 2/10). 캐시가 x를 들고 있지 못하면(큰 C, 무작위 열) gather가 캐시 미스가 된다. 개선: ① 인덱스·값 폭 줄이기(u16 인덱스, int8 값, 델타 인코딩), ② 재정렬(RCM 등, NUM-M11)로 열 지역성 높이기 → x 캐시 적중, ③ 블록 포맷(BSR)으로 인덱스 비용 분할 + SIMD, ④ 여러 벡터를 한 번에 곱하기(SpMM) — 같은 A 읽기를 여러 x에 재사용해 연산 강도를 올린다. 측정상 이 Mac에서 밀집 GEMV(NEON)보다 빨라지려면 75~90% 이상 희소해야 했다.

</details>

<details>
<summary>Q4. 멀티코어/ISR 환경에서 SpMV와 Aᵀx 를 병렬화하면?</summary>

`Ax`(CSR): 행별로 y[i]를 하나의 스레드만 쓰므로 **행 분할만으로 경쟁 없음**. 다만 행을 개수로 균등 분할하면 nnz가 불균형 → `row_ptr` 이진 탐색으로 nnz 균형 분할(연습 5). 한 행이 너무 길면 행 내부도 나눠야 한다(merge-path, GPGPU-A05). `Aᵀx`(scatter): 서로 다른 행이 같은 `y[c]` 에 더하므로 경쟁 → 원자 덧셈(느림, float이면 비결정적 순서), 스레드별 사본 y 후 합산, 또는 전치(CSC)를 미리 만들어 gather로 바꾸기. ISR이 x를 갱신하는 동안 태스크가 SpMV 중이면 반쯤 갱신된 x를 읽는다 → 이중 버퍼(ping-pong)로 x를 교체하고 포인터만 원자적으로 바꾼다.

</details>

<details>
<summary>Q5. 중복 COO 항목은 어떻게 처리해야 하나요? 합이 0이 되면?</summary>

정책 문제라 **먼저 명시**한다. 관례는 합산(MATLAB `sparse`, SciPy `tocsr()`, PyTorch `coalesce()`) — FE 조립, 기울기 누적이 그 의미를 원한다. 합이 0인 칸: 이 레슨은 버린다(정규형, 메모리 절약, 가지친 가중치에 맞음). SciPy는 explicit zero를 남기고 `eliminate_zeros()` 를 따로 둔다 — 같은 희소 **패턴**(sparsity pattern)을 유지해야 하는 경우(패턴을 재사용하는 반복 조립, 기호적 분해 재사용)에는 남기는 게 맞다. 넘침: int64 누적 후 최종이 int32를 넘으면 에러 — 펌웨어에선 포화(saturate)도 선택지지만 조용한 포화는 디버깅을 어렵게 한다.

</details>

<details>
<summary>Q6. 가지친 NN에서 희소성이 실제로 빨라지게 하려면? (AI 가속기 관점)</summary>

비정형 희소성은 메모리는 줄여도 밀집 MAC 어레이에선 거의 안 빨라진다 — 위 측정에서도 CPU NEON 기준 75~90%가 break-even. 하드웨어가 이득을 보려면 **규칙성**이 필요하다: 2:4 구조적 희소성(NVIDIA Ampere sparse Tensor Core, 값당 2비트 메타데이터로 활성화를 mux해 MAC 절반만 사용), 블록 희소성(블록 안은 밀집 GEMM), 채널 가지치기(아예 작은 밀집 레이어가 됨 — 가장 확실한 속도 이득). 비정형을 직접 다루는 EIE류 설계는 CSC + 상대 인덱스 + 활성화 0 건너뛰기, PE 간 부하 불균형은 큐로 흡수한다. 대역폭 제한 환경에서는 압축만으로도 이득이라 "연산이 아니라 바이트를 세라"가 핵심 답이다. (상세: EML-A06, NPU-A01)

</details>

<details>
<summary>Q7. 그래프 알고리즘에서 CSR은 어떻게 쓰이나요?</summary>

인접 리스트를 CSR로 둔 것이 그래프 처리의 표준 표현이다: `row_ptr[v] .. row_ptr[v+1]` 가 정점 v의 이웃 목록(`col_idx`), `val` 은 간선 가중치. 포인터 연결 리스트보다 메모리가 작고(간선당 포인터 없음) 이웃 순회가 연속 접근이다. BFS의 한 단계는 "프런티어 벡터와 인접 행렬의 곱"(불리언 semiring 위의 SpMV)으로 볼 수 있고, PageRank는 SpMV를 반복하는 것이다. 들어오는 간선(in-edges)이 필요하면 전치(= CSC)를 한 번 만들어 둔다. 단점은 정점/간선 추가가 O(nnz) — 정적 그래프용이다(ALGO 트랙의 그래프 문제에 그대로 쓸 수 있다).

</details>

## 9. 추가 연습

### 연습 1 — 정렬 없는 O(nnz + R + C) 정규화 (2-pass radix)

행 안 삽입 정렬 대신 **열 기준 → 행 기준** 두 번의 안정 counting으로 COO를 정규화하라. 한 행에 열 내림차순 4000개인 최악 입력에서 원소 이동 횟수를 본문 방식(행 counting + 삽입 정렬)과 비교하라.

<details>
<summary>답 보기</summary>

LSD radix sort: 낮은 자리(열)로 먼저 안정 정렬하고, 높은 자리(행)로 안정 정렬하면 행 안에서 열 순서가 보존된다. 그 다음 같은 (행, 열)을 선형으로 합산. 이동은 패스당 n번, 총 정확히 2n. 실제 출력:

```text
[1] one row, 4000 entries, cols descending: moves  isort=3972496  radix=8000
```

삽입 정렬은 역순 쌍(inversion) 수만큼 이동하므로 한 긴 행에서 O(L²)이 된다. 대가: COO 사본 n칸 + 개수 배열 max(R, C)+1칸. 랜덤 500회는 밀집 누적 참조와 일치한다.

</details>

### 연습 2 — work 버퍼 없는 전치 (row_ptr 시프트 트릭)

커서용 `work[C]` 없이 전치하라. 결과가 본문 방식과 배열까지 같아야 한다.

<details>
<summary>답 보기</summary>

열 c의 개수를 `rp[c]` 에(한 칸 **왼쪽**) 세고, 배타적 누적합으로 `rp[c]` = 열 c의 시작으로 만든다. 흩뿌리기에서 `rp[c]++` 를 그대로 커서로 쓰면 끝난 뒤 `rp[c]` = 열 c의 끝 = 열 c+1의 시작이 된다 → 오른쪽으로 한 칸 밀고(`for c = C..1: rp[c] = rp[c-1]`) `rp[0] = 0`. SciPy `csr_tocsc` / `coo_tocsr` 가 쓰는 방식이다. C칸(MCU에선 수 KB일 수 있다)을 아낀다. 랜덤 500회에서 `csr_same(work 방식, 시프트 방식)` 과 두 번 전치 == 원본 확인.

</details>

### 연습 3 — float SpMV: 언제 `==` 이고 언제 허용 오차인가

200×200, 밀도 10% float 행렬에서 (a) 순차 CSR SpMV와 밀집 GEMV, (b) 순차 CSR와 **부분합 4개(4-lane)** CSR SpMV를 비교하라. (c) x에 `INFINITY` 를 넣으면?

<details>
<summary>답 보기</summary>

(a) **정확히 같다(`==`)**: 같은 순서로 더하고, 밀집이 추가로 더하는 항은 `0·x_j = ±0` 이라 `s + (±0) = s` 가 정확하다(s ≠ 0일 때. FMA 축약을 끈 상태 — `#pragma STDC FP_CONTRACT OFF`, 두 루프가 다르게 축약되면 깨질 수 있다). (b) 4-lane은 합산 순서가 달라 비트가 다를 수 있다. 각 결과의 오차는 재귀 합산 오차 한계 `|ŝ − s| ≤ γₙ·Σ|aᵢⱼxⱼ|`, `γₙ = n·u/(1 − n·u)`, `u = 2⁻²⁴`(n = 그 행의 항 수, 곱의 반올림 포함)이므로 두 구현의 차이는 `≤ 2γₙ·Σ|aᵢⱼxⱼ|` 이다 — 이것을 허용 오차로 쓴다. 고정 epsilon(예: 1e-5)은 값의 크기를 무시하므로 틀린 기준이다. 실제 출력:

```text
[3] float 200x200 10%: dense==csr_seq rows all equal, lanes4 differs in 135/200 rows, worst |diff|/(u*sum|ax|) = 1.96
```

200행 중 135행이 비트가 달랐지만 최악 차이가 `1.96·u·Σ|ax|` 로 한계(행당 약 20항 → 약 40·u·Σ|ax|)보다 훨씬 작다. (c) 밀집은 `0·∞ = NaN` 이 되어 그 행이 NaN, CSR은 0인 항을 곱하지 않으니 유한값 — **희소와 밀집이 수학적으로 다른 답을 내는 경우**. 가속기 검증(silicon validation)에서 골든 모델과 비교할 때 이 차이를 알아야 한다.

</details>

### 연습 4 — 원소 조회 a(i, j) 를 O(log L)에

정규형 CSR에서 `csr_get(A, i, j)` 를 구현하라. 없으면 0.

<details>
<summary>답 보기</summary>

행 i의 `[rp[i], rp[i+1])` 구간에서 `col_idx` 를 lower_bound 이진 탐색 → O(log L). **정규형(행 안 정렬)이 전제** — 그래서 변환에서 정렬을 강제한 것이다. `mid = lo + (hi − lo)/2` 로 넘침 방지. 랜덤 100개 행렬의 모든 칸을 밀집 참조와 대조한다. 같은 조회를 반복한다면 행을 밀집 버퍼에 한 번 펼치는(scatter) 편이 낫다.

</details>

### 연습 5 — nnz 균형 행 분할 (멀티코어 / DMA 청크)

행 길이가 멱법칙(power-law)인 64행 행렬을 K=4 조각으로 나눠 SpMV를 병렬화한다. 행 개수 균등 분할과 **nnz 균등 분할**(row_ptr 이진 탐색)의 최대 조각 nnz를 비교하라.

<details>
<summary>답 보기</summary>

조각 p의 시작 행 = `row_ptr[r] ≥ p·nnz/K` 인 최소 r (row_ptr가 단조 증가라 이진 탐색). 실제 출력:

```text
[5] nnz=3290, K=4: max part nnz  even-rows=3178  nnz-balanced=2001  (ideal 822, longest row 2001)
```

행 균등 분할은 첫 조각이 3178(전체의 97%)을 떠안는다. nnz 균형 분할은 2001로 줄지만 이상값 822에는 못 미친다 — 길이 2001인 행 하나를 **행 단위로는** 나눌 수 없기 때문. 그 이상은 행 내부를 쪼개는 nnz 기반 분할(merge-path)이 필요하다 → GPGPU-A05.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 7/7 passed
```

## 10. 핵심 정리

- CSR = `row_ptr[R+1]` + `col_idx[nnz]` + `val[nnz]`. 행 i = `[row_ptr[i], row_ptr[i+1])`, 빈 행은 같은 값 두 번. 정규형 = 행 안 열 엄격 증가 + 0 없음 → 표현이 유일.
- COO → CSR = counting sort: `rp[r+1]++` → 누적합 → `work` 커서로 흩뿌리기 → 행별 정렬 → 중복 합산·0 제거·제자리 압축(w ≤ k, end를 먼저 읽기).
- 중복 정책을 명시: 합산, 합 0이면 제거, 최종 int32 넘침은 에러. 용량은 `n_in` 기준.
- 전치 = CSC = 열 기준 counting. 행 순서로 흩뿌리면 정렬이 공짜 → 두 번 전치 == 원본.
- `Ax` 는 gather(행별 독립), `Aᵀx` 는 scatter(y 초기화, 병렬 시 경쟁).
- 곱은 `(int64_t)` 로 넓힌 뒤. `abs(INT32_MIN)` 금지. float은 순서가 같으면 `==`, 다르면 `2γₙΣ|ax|`.
- 실측(512², 이 Mac): 메모리 이득은 희소도 33%부터, 시간 이득은 NEON 75~90%, SIMD 끄면 0~50% — break-even은 하드웨어의 밀집 실력이 정한다.
- 가속기는 비정형보다 구조적 희소성(2:4, 블록)을 택한다 — 규칙성, 부하 균형, 작은 메타데이터.

## 참고자료

- Yousef Saad, *Iterative Methods for Sparse Linear Systems*, 2nd ed., SIAM 2003 — Ch. 3.4 Storage Schemes (CSR/CSC/ELL 등) [저자 공개 PDF](https://www-users.cse.umn.edu/~saad/IterMethBook_2ndEd.pdf)
- Timothy A. Davis, *Direct Methods for Sparse Linear Systems*, SIAM 2006 — Ch. 2 (triplet → compressed column, `cs_compress`, `cs_transpose`, `cs_dupl`)
- [SciPy `scipy/sparse/sparsetools/csr.h` — `csr_tocsc`, `csr_sum_duplicates`](https://github.com/scipy/scipy/blob/main/scipy/sparse/sparsetools/csr.h) / [`coo.h` — `coo_tocsr`](https://github.com/scipy/scipy/blob/main/scipy/sparse/sparsetools/coo.h)
- [SciPy `coo_matrix.tocsr` 문서 — "Duplicate entries will be summed together"](https://docs.scipy.org/doc/scipy/reference/generated/scipy.sparse.coo_matrix.tocsr.html)
- Nicholas J. Higham, *Accuracy and Stability of Numerical Algorithms*, 2nd ed., SIAM 2002 — Ch. 4 Summation (γₙ 오차 한계)
- Song Han et al., "EIE: Efficient Inference Engine on Compressed Deep Neural Network", ISCA 2016 ([arXiv:1602.01528](https://arxiv.org/abs/1602.01528))
- Song Han, Huizi Mao, William J. Dally, "Deep Compression", ICLR 2016 ([arXiv:1510.00149](https://arxiv.org/abs/1510.00149))
- [NVIDIA, "Accelerating Inference with Sparsity Using the NVIDIA Ampere Architecture and NVIDIA TensorRT"](https://developer.nvidia.com/blog/accelerating-inference-with-sparsity-using-ampere-and-tensorrt/) — 2:4 구조적 희소성
