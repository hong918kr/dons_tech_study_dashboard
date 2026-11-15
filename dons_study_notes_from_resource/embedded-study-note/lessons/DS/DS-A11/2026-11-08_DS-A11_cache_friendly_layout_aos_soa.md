---
id: DS-A11
time: 100분
updated: 2026-11-08
---
# DS-A11 캐시 친화 레이아웃: AoS vs SoA와 정렬

## 학습 목표

- 같은 논리 레코드(64바이트)를 **AoS**(Array of Structs), **SoA**(Struct of Arrays), **AoSoA**(블록 단위 혼합)로 구현하고, 세 레이아웃이 **값·합·체크섬까지 똑같다**는 것을 레퍼런스 모델로 보인다.
- `sizeof`/`offsetof`/스트라이드(stride)/패딩을 `_Static_assert`로 못박고, C의 배치 규칙을 `layout_size()`로 직접 재현한다.
- "SoA가 빠르다"를 **원인별로 쪼갠다**. SIMD를 끈 빌드와 켠 빌드, `-Rpass=loop-vectorize` 실제 출력, 작업 집합(working set) 크기별 측정으로 캐시 효과와 SIMD 효과와 정렬 효과를 따로 본다.
- 건드리는 캐시 라인 수를 세는 **플랫폼 독립 모델**(`line_switches`)로 측정을 뒷받침한다. 연결 리스트의 비용을 할당 순서 vs 뒤섞인 순서로 비교한다.
- 측정에서 나온 것만으로 설계 지침을 쓰고, D-캐시가 없는 Cortex-M에서 무엇이 달라지는지 설명한다.

## 1. 문제

"센서 레코드 수만 개를 매 주기 처리합니다. 구조체 배열로 둘 건가요, 필드별 배열로 둘 건가요? 왜요?" 펌웨어와 시스템 면접에서 데이터 레이아웃은 답이 하나가 아니어서 자주 나온다. **근거를 숫자로 대는 사람과 "SoA가 캐시에 좋대요"라고 외운 사람이 바로 갈린다.** 파티클 시뮬레이션, 레이더 포인트 클라우드, FTL의 블록 메타데이터 테이블이 전부 같은 질문이다.

이 레슨의 레코드는 64바이트다. 위치와 속도(`x,y,z,vx,vy,vz`), 타임스탬프, 시퀀스, 온도, 플래그, 그리고 거의 안 보는 교정 계수 `cal[6]`이 들어 있다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">레코드 8개의 x 를 읽을 때 건드리는 128B 캐시 라인 (이 맥의 hw.cachelinesize)</text>
  <text x="10" y="52" fill="currentColor">AoS</text>
  <g stroke="currentColor">
    <rect x="80" y="36" width="160" height="26" class="f-warn"/><rect x="240" y="36" width="160" height="26" class="f-warn"/>
    <rect x="400" y="36" width="160" height="26" class="f-warn"/><rect x="560" y="36" width="160" height="26" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="88" y="54">x....  x....</text><text x="248" y="54">x....  x....</text>
    <text x="408" y="54">x....  x....</text><text x="568" y="54">x....  x....</text>
  </g>
  <text x="80" y="78" fill="currentColor">라인 하나에 레코드 2개, x 는 2개뿐 → 레코드 8개에 라인 4개. 나머지 120B 는 안 쓰는데 끌려온다.</text>
  <text x="10" y="122" fill="currentColor">SoA</text>
  <g stroke="currentColor">
    <rect x="80" y="106" width="160" height="26" class="f-ok"/>
    <rect x="240" y="106" width="160" height="26" class="f-mut"/><rect x="400" y="106" width="160" height="26" class="f-mut"/>
    <rect x="560" y="106" width="160" height="26" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12"><text x="88" y="124">x x x x x x x x</text><text x="248" y="124">(x 다음 칸들)</text></g>
  <text x="80" y="148" fill="currentColor">x[] 만 연속 → 라인 하나에 x 32개. 8개는 라인 1개면 끝. y, z, ... 는 아예 안 건드린다.</text>
  <text x="10" y="192" fill="currentColor">AoSoA</text>
  <g stroke="currentColor">
    <rect x="80" y="176" width="160" height="26" class="f-ok"/>
    <rect x="240" y="176" width="160" height="26" class="f-mut"/><rect x="400" y="176" width="160" height="26" class="f-mut"/>
    <rect x="560" y="176" width="160" height="26" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12"><text x="88" y="194">x y z vx [8]</text><text x="248" y="194">vy vz t_us</text><text x="408" y="194">seq temp flags..</text><text x="568" y="194">cal...</text></g>
  <text x="80" y="218" fill="currentColor">블록(512B) 안에서만 SoA → x[8] 은 32B, 라인 1개. 다음 블록의 x 는 512B 뒤.</text>
  <g fill="currentColor">
    <text x="10" y="258">x 스캔 1024 개 기준 라인 수 (solution.c 의 line_switches, 결정적 체크):</text>
    <text x="10" y="280" font-family="monospace">128B 라인: AoS 512  SoA  32  AoSoA 128   /  32B 라인(Cortex-M7): AoS 1024  SoA 128  AoSoA 128</text>
    <text x="10" y="306">일반식: x 를 W 개씩 묶은 AoSoA 는 W×4 ≥ 라인이면 SoA 와 같은 라인 수가 된다 (W=8, 32B 라인).</text>
  </g>
</svg>
```

| 파트 | 구현 | 체크 (결정적) | 측정 (`demo_bench.c`) |
|---|---|---|---|
| A | SoA 접근자, 부분 필드 커널, 전체 갱신 커널 (AoS는 기준점으로 주어짐) | 갱신 규칙 수치 고정, 레퍼런스 일치 | ns/record, 작업 집합별 |
| B | AoSoA 인덱스 계산·꼬리, swap_remove, `layout_size`, `line_switches` | 포이즌, sizeof/offsetof, 라인 수 | SIMD 켬/끔, 정렬 오프셋 |
| C | DMA 바이트열 → SoA, 윈도 통계, 노드 풀 연결 리스트 | 비정렬 디코드, 범위 감김, 라인 전환 수 | 리스트 vs 배열 |

### Part A — AoS 기준점과 SoA

- `Rec`은 64바이트이고 패딩이 없다. 필드는 큰 정렬부터 놓았다. `Aos`는 그냥 `Rec r[CAP]`이다(주어짐).
- `Soa`를 구현한다: `soa_get/set/push`, 부분 필드 읽기 `soa_sum_x`, `soa_sum_masked`(flags bit0이 켜진 레코드의 x만 합산), 절반 갱신 `soa_update_pos`, 전체 갱신 `soa_update_all`, `soa_swap_remove`.
- 전체 갱신의 계약은 `rec_update_all`과 레코드별로 똑같아야 한다. 위치는 **옛 속도로** 적분한 뒤 속도를 줄인다. 필드별 for 루프로 쪼개면 이 순서를 지키는 것이 일이 된다.

### Part B — AoSoA, 패딩, 라인 모델

- `Block`은 W=8 레인 묶음이다. 인덱스 `i`는 `(blk_of(i), lane_of(i))`로 바뀐다. `n`이 W의 배수가 아니면 마지막 블록은 일부 레인만 유효하다. **유효 레인 밖은 읽지도 쓰지도 않는다.** 하네스가 빈 슬롯을 포이즌(0xA5)으로 채워 놓고 합에 섞이거나 값이 변하면 잡는다.
- `aosoa_swap_remove`는 다른 블록의 레코드를 옮겨 온다.
- `layout_size(fields, n)`: 멤버 offset을 정렬 배수로 올리고, 끝을 최대 정렬 배수로 올리는 규칙을 그대로 구현해서 실제 `sizeof`와 일치시킨다.
- `line_switches(c, addr_fn, n, elem, line)`: 원소를 순서대로 건드릴 때 다른 라인으로 넘어가는 횟수를 센다. 128B/32B 라인에서 레이아웃별 정답을 체크한다.

### Part C — 펌웨어 현실: DMA 바이트열, 윈도, 리스트

- `wire_decode`: DMA 버퍼에는 19바이트짜리 프레임이 정렬 없이 놓인다. `(const uint32_t *)`로 캐스트하지 않고 바이트로 조립해서 SoA에 붙인다. 남는 조각은 무시하고, CAP에 차면 멈춘다.
- `soa_window_stats`: `x[first..first+count)`의 min/max/sum. `first + count` 덧셈이 감기지 않게 범위를 검사한다.
- 노드 풀 연결 리스트: `order[]` 순서대로 `next`를 잇고 합을 구한다. 뒤섞인 순서에서도 합은 같고, 라인 전환 수만 커지는 것을 체크한다.

## 2. Problem (English)

Implement a fixed-capacity container of 64-byte records in three layouts and compare them.

```c
typedef struct { uint64_t t_us; uint32_t x,y,z,vx,vy,vz,seq; uint16_t temp;
                 uint8_t flags,health; uint32_t cal[6]; } Rec;      /* sizeof == 64 */

/* Same operations for Soa and Aosoa (Aos is given): */
void     soa_get(const Soa *s, uint32_t i, Rec *out);
void     soa_set(Soa *s, uint32_t i, const Rec *r);
void     soa_push(Soa *s, const Rec *r);
uint32_t soa_sum_x(const Soa *s);                       /* wraps mod 2^32 */
uint32_t soa_sum_masked(const Soa *s);                  /* x where flags bit0 set */
void     soa_update_pos(Soa *s);                        /* x,y,z += vx,vy,vz */
void     soa_update_all(Soa *s, uint32_t dt);           /* every field; same result as Aos */
void     soa_swap_remove(Soa *s, uint32_t i);           /* vacated slot gets the poison pattern */

uint32_t layout_size(const Field *f, uint32_t n);       /* C struct layout rule */
uint32_t line_switches(const void *c, AddrFn addr, uint32_t n, uint32_t elem, uint32_t line);
uint32_t wire_decode(Soa *s, const uint8_t *buf, size_t len);
bool     soa_window_stats(const Soa *s, uint32_t first, uint32_t count, WinStats *out);
Node    *list_link(Node *pool, const uint32_t *order, uint32_t n);
```

Constraints: no heap in the containers; the AoSoA block width is 8; a kernel must never read or write a slot at index ≥ n. All layouts must produce bit-identical records, sums and checksums for the same operation sequence. Then measure throughput (ns/record) of partial-field and full-update kernels across working sets crossing L1, L2 and the system cache, with and without auto-vectorization, and say which effect explains each result.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있음
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   # 8/51 passed, exit 1
```

AoS와 하네스, `_Static_assert`는 이미 있다. TODO는 SoA 전부, AoSoA 전부, `layout_size`, `line_switches`, 그리고 Part C다.

## 4. 힌트

<details><summary>힌트 1 — SoA의 전체 갱신을 필드별 루프로 쪼갤 때</summary>

`rec_update_all`에서 필드 사이의 의존은 `x += vx` → `vx -= vx >> 4` 하나뿐이다. 필드별 루프로 나누어도 **위치 루프를 속도 루프보다 먼저** 돌리면 결과가 같다. 다른 필드는 서로 독립이라 순서가 상관없다.
</details>

<details><summary>힌트 2 — AoSoA의 꼬리</summary>

블록 포인터 `b`와 남은 개수 `rem`을 들고 돈다. 이 블록에서 처리할 레인 수는 `m = min(W, rem)`이다. 블록 전체(`l < W`)를 돌리는 코드는 합에 포이즌을 섞거나, 갱신 커널이 n 밖의 값을 바꾼다.
</details>

<details><summary>힌트 3 — layout_size의 끝 패딩</summary>

배열로 썼을 때 다음 원소가 정렬되어야 한다. 그래서 `{uint16_t; uint8_t;}`의 크기는 3이 아니라 4다. 끝에 최대 정렬 배수로 올리는 단계를 빼먹기 쉽다.
</details>

<details><summary>힌트 4 — line_switches</summary>

원소 하나가 라인 경계를 걸칠 수 있다. 원소의 첫 바이트 라인 ~ 끝 바이트 라인을 차례로 보고, 직전 라인과 다를 때만 +1 한다. "지금까지 본 라인 집합"은 필요 없다. 주소가 단조 증가하면 전환 횟수가 곧 라인 수다.
</details>

<details><summary>힌트 5 — wire_decode</summary>

`get_le32`는 주어져 있다. `t_us`는 32비트 두 번을 조합한다. `len >= WIRE_SIZE && s->n < CAP`이 루프 조건이다. `(const uint32_t *)buf`로 읽으면 이 맥에서는 값이 맞게 나와도 UBSan이 잡고, Cortex-M0에서는 HardFault가 난다.
</details>

<details><summary>힌트 6 — window_stats의 범위 검사</summary>

`first + count > n`은 `count`가 크면 감겨서 통과할 수 있다. `first >= n || count > n - first`처럼 뺄셈으로 쓴다.
</details>

## 5. 풀이

### 불변식

세 컨테이너와 레퍼런스(`Rec g_ref[CAP]`)에 대해 `invariants_ok()`가 다음을 확인한다.

1. 개수 `n`이 같다.
2. `i < n`인 모든 슬롯이 레퍼런스와 바이트까지 같다(`Rec`은 패딩이 없으니 `memcmp`가 값 비교와 같다).
3. 합과 마스크 합이 같고, FNV-1a 체크섬이 같다.
4. `i ≥ n`인 슬롯은 전부 포이즌 그대로다. **커널이 n 밖을 건드리지 않았다는 증거**다.

작은 n(≤40, 4000 연산)은 매 연산마다 전수 검사, n ≈ 1024(30만 연산)는 **K = 1024** 연산마다 + 끝에서 전수 검사한다. 이 스케일은 ASan/UBSan에서도 그대로 돌아서 1.5초쯤 걸린다.

### Part A — 레이아웃을 바꿔도 값은 안 바뀐다

SoA의 핵심은 간단하다. 접근자가 필드 배열에서 모아 주고(`soa_get`), 흩어 넣는다(`soa_set`). 전체 갱신은 필드별 루프로 쪼갠다. 문제는 **의존 순서**다.

```text
AoS:  r->x += r->vx;  ...  r->vx -= r->vx >> 4;   (레코드 안에서 순서대로)
SoA:  for x...;  for y...;  for z...;  for vx (감쇠)...;   ← 위치 루프가 먼저
```

감쇠 루프를 먼저 돌리면 `x`가 이미 줄어든 속도로 적분된다. 하네스의 고정값 체크(`x == 17`, `vx == 15`)와 레퍼런스 비교가 이 버그를 잡는다. 실제로 `damp_before_integrate` 변형을 넣어 보면 7개 체크가 FAIL한다.

정렬은 어떻게 했나? 컨테이너의 시작을 `alignas(128)`로 잡았다. 이 맥의 라인이 128바이트이고, 시작이 라인 경계여야 `line_switches`의 답이 결정적이기 때문이다. SoA의 각 필드 배열도 `alignas(128)`이고, `CAP = 1024`라 필드 배열 크기가 128의 배수여서 중간 패딩은 없다(`offsetof(y) - offsetof(x) == 4096`을 `_Static_assert`로 확인).

### Part B — AoSoA와 꼬리

AoSoA의 인덱스 계산은 비트 연산이다. W가 2의 거듭제곱이어야 `i >> 3`과 `i & 7`로 끝난다.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">n = 13, W = 8: 꼬리 블록은 레인 5개만 유효</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="20" y="50">i = 9 → blk_of = 9 &gt;&gt; 3 = 1, lane_of = 9 &amp; 7 = 1</text>
  </g>
  <g stroke="currentColor">
    <rect x="20" y="66" width="320" height="30" class="f-ok"/>
    <rect x="360" y="66" width="200" height="30" class="f-ok"/>
    <rect x="560" y="66" width="120" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="86">블록 0: 레인 0..7 전부 유효 (m = 8)</text>
    <text x="370" y="86">블록 1: 레인 0..4 (m = 5)</text>
    <text x="568" y="86">레인 5..7 = 포이즌</text>
  </g>
  <g fill="currentColor">
    <text x="20" y="122">rem = 13 → m = 8, rem = 5 → m = 5, rem = 0 → 끝.</text>
    <text x="20" y="146">레인 8 전부 돌리는 코드(l &lt; W)는 합에 0xA5A5A5A5 를 섞고, update_all 은 n 밖 값을 바꾼다.</text>
    <text x="20" y="170">swap_remove(3): 마지막(12)을 블록 0 레인 3 으로 옮기고, 블록 1 레인 4 를 포이즌으로 → 불변식 4 유지.</text>
  </g>
</svg>
```

꼬리를 놓치는 버그 두 가지는 실제로 잡힌다. 합 루프를 `l < W`로 돌리면(변형 `tail_full_block`) 7개 FAIL이 나고, 갱신의 한 루프만 `l < W`로 돌려도(변형 `update_tail_full`) 4개 FAIL이 난다(포이즌이 변했다는 불변식 4).

**패딩.** 같은 멤버를 어떤 순서로 놓느냐에 따라 16바이트 차이가 난다.

| 구조체 | 순서 | sizeof | 패딩 |
|---|---|---|---|
| `BadOrder` | u8, u64, u8, u32, u16 | 32 | 1+7, 1+3, 2+6 → 16바이트가 패딩 |
| `GoodOrder` | u64, u32, u16, u8, u8 | 16 | 0 |
| `Rec` (이 레슨) | u64, u32×7, u16, u8, u8, u32[6] | 64 | 0 |

`layout_size`는 `off = roundup(off, align) + size`를 반복하고 마지막에 최대 정렬로 올린다. 꼬리 패딩을 빠뜨리면(변형 `no_tail_padding`) 3개 FAIL이 난다.

**라인 모델.** `line_switches`의 체크가 그림 1의 숫자를 고정한다.

| 레이아웃 | x 1024개, 라인 128B | 라인 32B (Cortex-M7) |
|---|---|---|
| AoS (stride 64) | 512 (라인당 레코드 2개) | 1024 (레코드마다 라인 1개) |
| SoA (stride 4) | 32 (라인당 x 32개) | 128 |
| AoSoA W=8 | 128 (블록당 라인 1개) | 128 (= SoA, W×4 = 라인) |

### 측정 환경과 방법

- CPU: Apple M6. `sysctl` 실제 값: 세 등급의 코어가 있다. Super 2코어(L1d 128 KB, L2 20 MB/2코어), Performance 4코어(L1d 64 KB, L2 20 MB/4코어), Efficiency 6코어(L1d 96 KB, L2 8 MB/6코어). `hw.cachelinesize` = 128, 페이지 16 KB. **SLC(시스템 레벨 캐시) 크기는 sysctl에 나오지 않는다.**
- `demo_bench.c`: 작업 집합 S 를 16 KB ~ 256 MB로 늘리며, n = S/64 레코드에 커널을 돌린다. 한 칸 = 5회 반복의 중앙값. 시간은 `CLOCK_UPTIME_RAW`. 첫 패스는 데우기로 빼고, 메모리는 미리 건드려(`memset`) 페이지 폴트를 뺐다. `QOS_CLASS_USER_INTERACTIVE`로 P 코어를 **권유**할 뿐, 코어 고정은 안 된다. Super와 Performance 중 어디서 돌았는지는 모른다.
- 빌드 3종: 기본(`-O2`, AoSoA W=8), `-DAW=32`, 그리고 **SIMD를 끈** `-O2 -fno-vectorize -fno-slp-vectorize`.
- **이 측정은 시끄럽다.** 같은 시간에 다른 작업이 돌아서 부하 평균이 10~52였다. 전체를 6회 돌렸고(3회 × 2묶음), 표의 각 칸은 `중앙값 (최솟값)`이다. 실행 간 spread(최대-최소/중앙값)는 칸마다 대체로 30~60%, 최악 178%다. 그래서 **2배 미만의 차이는 신호로 해석하지 않는다.** 최솟값이 중앙값과 같은 방향이면 믿는다.

**자동 벡터화 실제 결과** (`cc -std=c11 -O2 -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize`, W=8, 줄 번호는 demo_bench.c):

```text
69:5: remark: vectorized loop (vectorization width: 4, interleaved count: 4)        ← aos_sum_x (!)
74:5: remark: vectorized loop (vectorization width: 4, interleaved count: 4)        ← soa_sum_x
79:5: remark: the cost-model indicates that vectorization is not beneficial         ← aosoa_sum_x 바깥 루프
86:5: remark: the cost-model indicates that vectorization is not beneficial         ← aos_sum_masked
91:5: remark: the cost-model indicates that vectorization is not beneficial         ← soa_sum_masked
104:5: remark: vectorized loop (vectorization width: 16, interleaved count: 4)      ← aos_sum_mask_bl
109:5: remark: vectorized loop (vectorization width: 16, interleaved count: 2)      ← soa_sum_mask_bl
120:5: remark: the cost-model indicates that vectorization is not beneficial        ← aos_update_pos
127:5: remark: vectorized loop (vectorization width: 4, interleaved count: 4)       ← soa_update_pos (128, 129 도)
140:5: remark: the cost-model indicates that vectorization is not beneficial        ← aos_update_all
159:5: remark: vectorized loop (vectorization width: 2, interleaved count: 4)       ← soa_update_all: t_us
160~169: vectorized (u32 폭 4, u16 폭 8, u8 폭 16)                                  ← soa_update_all 나머지
170:5: remark: the cost-model indicates that vectorization is not beneficial        ← cal[i][j] 안쪽
```

AoSoA는 루프 벡터라이저가 안 잡는 것처럼 보이는데, `-Rpass=slp-vectorizer`를 보면 다른 패스가 잡는다(고정 8회 안쪽 루프가 펼쳐진 뒤 SLP 벡터화됨).

```text
80:45: remark: Vectorized horizontal reduction with cost -10 and with tree size 1     ← aosoa_sum_x
115:45: remark: Vectorized horizontal reduction with cost -41 and with tree size 8    ← aosoa_sum_mask_bl
133:53: remark: Stores SLP vectorized with cost -12 and with tree size 4              ← aosoa_update_pos (134, 135 도)
176~188: Stores SLP vectorized ...                                                    ← aosoa_update_all
```

`-Rpass` 문구만 믿으면 안 되는 경우가 있다. **AoS의 sum_x도 "vectorized"라고 나오지만** 실제 코드는 원소 하나씩 레지스터에 끼워 넣는다(gather를 스칼라 로드로 흉내). 실제 `-S` 출력이다.

```text
; aos_sum_x (arm64, -O2) — 레인마다 따로 로드
ldr   s4, [x8, #8]
ld1.s { v4 }[1], [x12]
ld1.s { v4 }[2], [x13]
ld1.s { v4 }[3], [x14]
...
; soa_sum_x — 레인 4개를 한 번에
ldp   q4, q5, [x8, #-32]
ldp   q6, q7, [x8], #64
add.4s v0, v4, v0
...
; aosoa_sum_x (W=8) — 블록당 ldp 한 번
ldp   q1, q0, [x8], #512
add.4s v0, v1, v0
addv.4s s0, v0
```

### 결과 1 — 부분 필드 읽기: `sum_x`

표의 칸은 `ns/record 중앙값 (최솟값)`, 6회 실행. noSIMD는 `-fno-vectorize -fno-slp-vectorize` 빌드다.

| S | AoS | SoA | AoSoA-8 | AoSoA-32 | noSIMD AoS | noSIMD SoA | noSIMD AoSoA-8 |
|---|---|---|---|---|---|---|---|
| 16 KB | 0.162 (0.101) | 0.036 (0.029) | 0.049 (0.038) | 0.034 (0.023) | 0.286 (0.241) | 0.286 (0.259) | 0.112 (0.075) |
| 64 KB | 0.163 (0.103) | 0.029 (0.022) | 0.048 (0.039) | 0.036 (0.022) | 0.316 (0.249) | 0.273 (0.243) | 0.120 (0.099) |
| 256 KB | 0.562 (0.467) | 0.035 (0.032) | 0.174 (0.072) | 0.080 (0.042) | 0.514 (0.458) | 0.270 (0.229) | 0.170 (0.123) |
| 1 MB | 0.526 (0.465) | 0.032 (0.030) | 0.146 (0.074) | 0.102 (0.043) | 0.526 (0.479) | 0.275 (0.236) | 0.178 (0.118) |
| 4 MB | 0.548 (0.490) | 0.038 (0.028) | 0.169 (0.135) | 0.112 (0.055) | 0.536 (0.504) | 0.277 (0.245) | 0.167 (0.129) |
| 16 MB | 0.611 (0.517) | 0.042 (0.031) | 0.217 (0.164) | 0.138 (0.071) | 0.587 (0.516) | 0.279 (0.233) | 0.271 (0.152) |
| 64 MB | 0.724 (0.649) | 0.049 (0.038) | 0.451 (0.353) | 0.159 (0.126) | 0.777 (0.633) | 0.278 (0.242) | 0.605 (0.331) |
| 256 MB | 0.740 (0.633) | 0.051 (0.043) | 0.665 (0.610) | 0.205 (0.160) | 0.841 (0.672) | 0.285 (0.254) | 0.982 (0.625) |

**원인 분해.** 이 표에서 읽는 것이 이 레슨의 핵심이다.

| 작업 집합 | AoS vs SoA (SIMD 켬) | AoS vs SoA (SIMD 끔) | 해석 |
|---|---|---|---|
| 16 KB (L1 안) | 0.162 vs 0.036 = **4.5배** | 0.286 vs 0.286 = **1.0배** | 캐시 효과 없음. 차이는 전부 **SIMD** |
| 4 MB (L2 안) | 0.548 vs 0.038 = **14배** | 0.536 vs 0.277 = **1.9배** | 캐시 2배 + SIMD |
| 256 MB (DRAM) | 0.740 vs 0.051 = **15배** | 0.841 vs 0.285 = **3.0배** | 캐시 3배 + SIMD |

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">sum_x 중앙값: 막대 길이 = ns/record (최대 0.841). 위는 SIMD 켬, 아래는 끔</text>
  <g fill="currentColor"><text x="10" y="48">16 KB</text><text x="10" y="118">4 MB</text><text x="10" y="188">256 MB</text></g>
  <g stroke="currentColor">
    <rect x="80" y="36" width="97" height="12" class="f-warn"/><rect x="80" y="50" width="172" height="12" class="f-warn"/>
    <rect x="80" y="66" width="22" height="12" class="f-ok"/><rect x="80" y="80" width="172" height="12" class="f-ok"/>
    <rect x="80" y="106" width="329" height="12" class="f-warn"/><rect x="80" y="120" width="322" height="12" class="f-warn"/>
    <rect x="80" y="136" width="23" height="12" class="f-ok"/><rect x="80" y="150" width="166" height="12" class="f-ok"/>
    <rect x="80" y="176" width="444" height="12" class="f-warn"/><rect x="80" y="190" width="505" height="12" class="f-warn"/>
    <rect x="80" y="206" width="31" height="12" class="f-ok"/><rect x="80" y="220" width="171" height="12" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="600" y="46">AoS SIMD</text><text x="600" y="60">AoS noSIMD</text><text x="600" y="76">SoA SIMD</text><text x="600" y="90">SoA noSIMD</text>
    <text x="600" y="116">AoS SIMD</text><text x="600" y="130">AoS noSIMD</text><text x="600" y="146">SoA SIMD</text><text x="600" y="160">SoA noSIMD</text>
    <text x="600" y="186">AoS SIMD</text><text x="600" y="200">AoS noSIMD</text><text x="600" y="216">SoA SIMD</text><text x="600" y="230">SoA noSIMD</text>
  </g>
  <text x="10" y="250" fill="currentColor">SoA 는 SIMD 로 5.6~8배 빨라진다. AoS 는 L2 이상에서 이득이 없다. noSIMD 끼리의 차이가 레이아웃 효과다.</text>
</svg>
```

정리하면 **SoA가 AoS보다 빠른 배수의 대부분은 SIMD에서 나오고**, 캐시 효과는 2~3배다. 그리고 그 캐시 효과는 L1을 넘어선 뒤에야 나타난다. AoS에서 SIMD의 이득은 L1 안에서 1.8배(0.286 → 0.162)이고, L2 이상에서는 없다(4 MB에서 0.536 → 0.548). 위 asm처럼 레인마다 `ld1.s`로 끼워 넣어야 하니 로드 수가 줄지 않는다. 절약되는 것은 덧셈뿐이다.

놀라운 점 하나: **순차 AoS는 DRAM까지 가도 별로 안 느려진다.** 4 MB 0.548 → 256 MB 0.740이다. 일정한 스트라이드를 하드웨어 프리페처가 따라가는 것으로 보인다(PMU로 확인하지는 않았다). 뒤의 뒤섞인 리스트 측정과 정반대다.

AoSoA는 그 중간이다. L1에서는 SoA에 가깝고(0.049 vs 0.036), DRAM으로 가면 W=8이 AoS 쪽으로 간다(0.665 vs AoS 0.740). W=32(0.205)가 낫다. 라인 모델의 예측과 방향이 같다: W=8의 x[8]은 32바이트라 라인당 x가 8개뿐이다. W=32면 x[32]가 128바이트로 라인 하나를 가득 채운다.

### 결과 2 — 두 필드 읽기: 분기가 레이아웃을 삼킨다

| S | AoS | SoA | AoSoA-8 | AoSoA-32 | noSIMD AoS | noSIMD SoA | noSIMD AoSoA-8 |
|---|---|---|---|---|---|---|---|
| 16 KB | 0.224 (0.181) | 0.233 (0.181) | 0.227 (0.187) | 0.341 (0.306) | 0.232 (0.207) | 0.235 (0.202) | 0.236 (0.182) |
| 64 KB | 0.272 (0.230) | 0.262 (0.222) | 0.252 (0.230) | 0.474 (0.431) | 0.252 (0.228) | 0.253 (0.227) | 0.263 (0.232) |
| 256 KB | 0.534 (0.472) | 0.343 (0.301) | 0.371 (0.308) | 0.653 (0.601) | 0.506 (0.448) | 0.346 (0.307) | 0.343 (0.311) |
| 1 MB | 0.535 (0.482) | 0.448 (0.389) | 0.469 (0.387) | 0.720 (0.628) | 0.521 (0.488) | 0.457 (0.392) | 0.445 (0.383) |
| 4 MB | 1.38 (0.622) | 1.11 (0.456) | 1.01 (0.539) | 1.37 (0.756) | 0.868 (0.555) | 0.964 (0.425) | 0.798 (0.426) |
| 16 MB | 2.32 (1.93) | 2.29 (2.07) | 2.31 (1.98) | 2.93 (2.61) | 2.15 (1.92) | 2.17 (1.53) | 2.09 (1.67) |
| 64 MB | 2.63 (2.40) | 2.61 (2.40) | 2.66 (2.45) | 3.10 (2.71) | 2.51 (2.38) | 2.51 (2.36) | 2.58 (2.42) |
| 256 MB | 2.66 (2.49) | 2.66 (2.43) | 2.62 (2.45) | 3.14 (2.75) | 2.66 (2.47) | 2.67 (2.40) | 2.70 (2.44) |

`sum_masked`는 **16 MB 이상에서 AoS·SoA·AoSoA-8이 모두 2.3~2.7 ns**로 똑같아진다(AoSoA-32는 2.9~3.1 ns). 이유는 레이아웃이 아니라 **분기 예측 실패**다. 실제 코드에 `tbz/tbnz`가 있다(위 `-S`로 확인, `(f & 1) ? x : 0`이 분기로 컴파일됨). 플래그는 난수라 예측이 절반쯤 빗나간다. 작은 n(256~16384개)에서는 패턴이 짧아서 예측기가 어느 정도 외우는 것으로 보인다(0.22~0.5 ns, 추정). 이 추정은 분기 없는 대조군으로 확인한다.

| S | AoS | SoA | AoSoA-8 | AoSoA-32 | noSIMD AoS | noSIMD SoA | noSIMD AoSoA-8 |
|---|---|---|---|---|---|---|---|
| 16 KB | 1.20 (0.414) | 0.089 (0.066) | 0.100 (0.078) | 0.102 (0.075) | 0.322 (0.286) | 0.366 (0.299) | 0.214 (0.131) |
| 64 KB | 0.601 (0.272) | 0.101 (0.063) | 0.107 (0.067) | 0.136 (0.109) | 0.343 (0.254) | 0.370 (0.265) | 0.213 (0.135) |
| 256 KB | 1.12 (0.520) | 0.090 (0.063) | 0.181 (0.143) | 0.162 (0.141) | 0.558 (0.466) | 0.350 (0.293) | 0.328 (0.222) |
| 1 MB | 1.11 (0.587) | 0.090 (0.064) | 0.161 (0.119) | 0.174 (0.150) | 0.544 (0.504) | 0.345 (0.300) | 0.298 (0.231) |
| 4 MB | 0.994 (0.609) | 0.091 (0.062) | 0.195 (0.140) | 0.166 (0.141) | 0.578 (0.526) | 0.352 (0.308) | 0.339 (0.283) |
| 16 MB | 1.10 (0.623) | 0.095 (0.061) | 0.273 (0.212) | 0.180 (0.153) | 0.619 (0.561) | 0.347 (0.310) | 0.459 (0.296) |
| 64 MB | 1.43 (0.800) | 0.092 (0.068) | 0.869 (0.735) | 0.222 (0.165) | 0.758 (0.664) | 0.354 (0.293) | 1.21 (0.956) |
| 256 MB | 1.34 (0.797) | 0.096 (0.064) | 0.980 (0.877) | 0.406 (0.266) | 0.850 (0.691) | 0.358 (0.305) | 1.57 (1.33) |

`x & (0 - (f & 1))`로 분기를 없앴더니 SoA는 **0.09 ns**로 일정하다(16 MB에서 2.29 → 0.095, 약 24배). 이 중 SIMD 몫을 빼도, 즉 noSIMD 빌드끼리 비교해도 2.17 → 0.347로 약 6배다. 그러니 원인은 분기다. 레이아웃보다 **분기 제거가 먼저**다.

여기서 함정이 또 나온다. **분기 없는 AoS는 벡터화했더니 빨라지지 않았다.** 중앙값은 SIMD 빌드 0.6~1.4 ns, noSIMD 빌드 0.32~0.85 ns로 약 2배 느리다. 최솟값끼리는 1.1~1.4배 느리다(예: 4 MB에서 0.609 vs 0.526). 모든 크기에서 방향은 같지만, 배수는 이 측정의 잡음 범위와 겹치므로 "빨라지지 않았다"까지만 말한다. `-Rpass`는 "vectorized width 16"이라고 하는데 실제 asm에는 `ld1.s`류로 레인 하나씩 끼워 넣는 줄이 121개 있다.

### 결과 3 — 갱신 커널

| S | AoS | SoA | AoSoA-8 | AoSoA-32 | noSIMD AoS | noSIMD SoA | noSIMD AoSoA-8 |
|---|---|---|---|---|---|---|---|
| 16 KB | 0.593 (0.402) | 0.206 (0.164) | 0.196 (0.135) | 0.341 (0.210) | 0.483 (0.344) | 0.929 (0.840) | 0.400 (0.289) |
| 64 KB | 0.565 (0.409) | 0.207 (0.132) | 0.190 (0.131) | 0.268 (0.160) | 0.486 (0.353) | 0.944 (0.824) | 0.416 (0.305) |
| 256 KB | 1.07 (0.894) | 0.329 (0.137) | 0.451 (0.336) | 0.363 (0.280) | 0.859 (0.691) | 0.901 (0.745) | 0.565 (0.403) |
| 1 MB | 1.02 (0.768) | 0.296 (0.238) | 0.451 (0.338) | 0.357 (0.241) | 0.877 (0.731) | 0.918 (0.774) | 0.631 (0.412) |
| 4 MB | 1.03 (0.762) | 0.317 (0.241) | 0.456 (0.402) | 0.401 (0.329) | 0.873 (0.732) | 0.894 (0.754) | 0.656 (0.446) |
| 16 MB | 1.08 (0.866) | 0.326 (0.251) | 0.765 (0.484) | 0.420 (0.340) | 0.971 (0.722) | 0.938 (0.809) | 0.747 (0.502) |
| 64 MB | 1.33 (1.02) | 0.336 (0.304) | 2.28 (1.78) | 1.30 (1.20) | 1.09 (0.933) | 0.913 (0.771) | 2.05 (1.57) |
| 256 MB | 1.31 (1.18) | 0.353 (0.290) | 2.35 (1.95) | 1.51 (1.43) | 1.35 (1.12) | 1.01 (0.749) | 2.42 (2.09) |

| S | AoS | SoA | AoSoA-8 | AoSoA-32 | noSIMD AoS | noSIMD SoA | noSIMD AoSoA-8 |
|---|---|---|---|---|---|---|---|
| 16 KB | 1.81 (1.60) | 0.861 (0.742) | 0.760 (0.622) | 0.889 (0.686) | 1.98 (1.70) | 4.50 (3.92) | 2.66 (2.09) |
| 64 KB | 1.76 (1.60) | 1.02 (0.758) | 0.847 (0.599) | 0.895 (0.760) | 1.91 (1.66) | 4.17 (3.79) | 2.61 (1.98) |
| 256 KB | 2.04 (1.68) | 1.37 (1.12) | 1.24 (1.02) | 1.32 (1.12) | 1.96 (1.62) | 3.93 (3.42) | 2.59 (2.12) |
| 1 MB | 2.11 (1.71) | 1.34 (1.12) | 1.20 (1.00) | 1.41 (1.08) | 1.95 (1.64) | 3.86 (3.41) | 2.66 (1.99) |
| 4 MB | 2.12 (1.70) | 1.37 (1.06) | 1.18 (0.981) | 1.35 (1.12) | 2.00 (1.67) | 3.97 (3.53) | 2.64 (2.12) |
| 16 MB | 2.16 (1.79) | 1.45 (1.18) | 1.33 (1.06) | 1.34 (1.10) | 2.11 (1.70) | 3.85 (3.50) | 2.61 (2.17) |
| 64 MB | 2.17 (1.74) | 1.46 (1.21) | 1.36 (1.10) | 1.42 (1.19) | 2.13 (1.70) | 4.07 (3.47) | 2.75 (2.34) |
| 256 MB | 2.10 (1.86) | 1.46 (1.32) | 1.35 (1.24) | 1.44 (1.23) | 2.49 (1.68) | 4.35 (3.48) | 2.90 (2.13) |

| 커널 | SIMD 켬 | SIMD 끔 |
|---|---|---|
| `update_pos` (필드 6개 중 3개 씀) | SoA가 AoS보다 약 3~4배 빠름 | **L1에서는 SoA(0.93 ns)가 AoS(0.48 ns)보다 느림**, L2 이상에서 비슷 |
| `update_all` (필드 전부) | SoA가 AoS보다 1.5~2배 빠름 | **SoA(3.9~4.5 ns)가 AoS(1.9~2.5 ns)의 약 2배 느림** |

**전체 갱신에서 SoA의 승리는 전적으로 SIMD 덕분이다.** SIMD를 끄면 SoA는 12개의 필드 루프를 따로 돌면서 AoS보다 약 2배 느리다. 레코드 하나를 처리하는 AoS 루프는 한 번에 64바이트(라인의 절반)를 다 쓴다. 건드리는 바이트가 어차피 전부라면 AoS의 공간 지역성이 SoA와 같거나 낫다.

`update_pos`에서 AoSoA-8은 **64 MB 이상에서 AoS보다 느렸다**(2.28 vs 1.33 ns, noSIMD 빌드도 2.05 vs 1.09 ns로 같은 방향). 블록 512바이트에서 앞쪽 192바이트(라인 2개)만 쓰고 뒤쪽 라인 2개를 건너뛰는 패턴이다. 측정만으로 원인을 확정할 수는 없다(이 맥에서 PMU 없이 돌렸다). 프리페처가 "2라인 쓰고 2라인 건너뛰기" 패턴에 불리하다는 것이 가설이고, 연속 접근인 AoS(라인 전부 순차)와 SoA(필드별 순차)가 더 빠른 것과 방향이 맞는다. **W는 커널의 필드 수와 라인 크기를 같이 보고 정해야 한다.** W=32는 1.30~1.51로 AoS와 비슷했다.

### 결과 4 — 정렬

같은 코드에서 시작 주소만 바꿨다.

| S | +0B(line) | +4B(natural) | +1B(unaligned) |
|---|---|---|---|
| 64 KB | 0.031 (0.027) | 0.034 (0.028) | 0.033 (0.028) |
| 4 MB | 0.046 (0.033) | 0.049 (0.036) | 0.046 (0.036) |
| 64 MB | 0.053 (0.043) | 0.053 (0.042) | 0.051 (0.043) |

| S | +0B(record on line) | +32B(half straddle) | +1B(unaligned) |
|---|---|---|---|
| 64 KB | 0.523 (0.371) | 0.540 (0.389) | 0.579 (0.387) |
| 4 MB | 1.05 (0.719) | 1.02 (0.701) | 1.03 (0.813) |
| 64 MB | 1.29 (0.931) | 1.25 (0.959) | 1.26 (0.929) |

**이 M6에서는 시작 주소 정렬의 효과를 측정으로 찾지 못했다.** 라인 경계(+0), 자연 정렬(+4), 비정렬(+1) 모두 잡음 안에 있다. AoS의 레코드가 128B 라인을 절반 걸치게(+32B) 해도 마찬가지다. 그러니 `alignas(128)`은 **성능을 위해 넣은 것이 아니라**, 라인 수를 결정적으로 계산하려고 넣은 것이다. 다른 코어에서는 다르다(Q&A Q5, 9절 연습 4).

### 결과 5 — 연결 리스트 vs 배열

같은 n개 데이터의 합을 구한다. `arr_u32`는 4바이트 값 배열(SoA 격), `arr_node64/16`은 노드를 배열로 순회(포인터 안 따라감), `list*_inorder`는 할당 순서대로 연결, `list*_shuffled`는 고정 시드로 섞은 순서로 연결. n은 모두 S/64라 16B 노드의 실제 메모리는 S/4다.

| S | arr_u32 | arr_node64 | arr_node16 |
|---|---|---|---|
| 16 KB | 0.024 (0.022) | 0.158 (0.094) | 0.183 (0.139) |
| 64 KB | 0.034 (0.022) | 0.146 (0.097) | 0.182 (0.133) |
| 256 KB | 0.037 (0.029) | 0.504 (0.469) | 0.194 (0.133) |
| 1 MB | 0.032 (0.030) | 0.508 (0.472) | 0.201 (0.136) |
| 4 MB | 0.040 (0.030) | 0.538 (0.488) | 0.205 (0.134) |
| 16 MB | 0.038 (0.030) | 0.679 (0.488) | 0.231 (0.148) |
| 64 MB | 0.043 (0.038) | 0.686 (0.596) | 0.226 (0.193) |
| 256 MB | 0.055 (0.042) | 0.700 (0.624) | 0.282 (0.197) |

| S | list64_inorder | list16_inorder | list64_shuffled | list16_shuffled |
|---|---|---|---|---|
| 16 KB | 0.893 (0.318) | 0.873 (0.670) | 0.915 (0.809) | 0.931 (0.789) |
| 64 KB | 0.930 (0.748) | 0.852 (0.755) | 0.932 (0.808) | 0.877 (0.777) |
| 256 KB | 1.23 (1.08) | 0.861 (0.760) | 5.51 (3.81) | 0.897 (0.782) |
| 1 MB | 1.27 (1.10) | 0.928 (0.820) | 5.54 (4.33) | 4.67 (3.30) |
| 4 MB | 1.19 (1.04) | 0.908 (0.778) | 9.37 (8.04) | 5.90 (4.52) |
| 16 MB | 1.30 (1.05) | 0.991 (0.837) | 47.20 (14.90) | 8.29 (7.26) |
| 64 MB | 1.24 (1.07) | 1.00 (0.828) | 101.90 (81.34) | 38.68 (17.82) |
| 256 MB | 1.26 (1.12) | 0.992 (0.894) | 101.81 (91.74) | 96.33 (73.55) |

| 관찰 | 숫자 |
|---|---|
| 배열(SoA) vs 할당 순서 리스트 | 0.055 vs 1.26 ns (약 23배, 256 MB 기준) |
| 할당 순서 리스트의 포인터 추적 비용 | 16B 노드 0.9~1.0, 64B 노드 0.9~1.3 ns — **DRAM까지도 안 변한다** |
| 뒤섞인 리스트의 추적 비용 | 64B: 1 MB에서 5.5, 4 MB에서 9.4, 256 MB에서 **102 ns** |
| 256 MB에서 u32 배열 대비 | 0.055 vs 101.8 ns = **약 1850배** |

할당 순서로 놓인 리스트도 배열보다는 약 23배 느리다. 그래도 **DRAM 크기까지 1 ns 근처로 일정**하다. 메모리 순서가 깨진 **뒤섞인 리스트**는 작업 집합이 커지면서 100 ns까지 올라간다. 라인 모델(`list_line_switches`)은 할당 순서 512회 vs 뒤섞음 1024회로 2배 차이인데, 실제 시간은 256 MB에서 약 80배 차이가 났다(1.26 vs 101.8 ns). **라인 수는 시간의 전부가 아니다.** 나머지 차이는 프리페치 가능 여부와 직렬 의존 때문으로 보이지만 PMU로 쪼개지는 않았다. 지연 곡선과 TLB 경계는 MEM-B10이 다룬다.

### Part C — 비정렬 바이트와 경계 검사

`wire_decode`는 프레임이 홀수 주소에서 시작해도 동작한다. 바이트로 조립하기 때문이다. 포인터 캐스트로 바꾸면(변형 `wire_cast`) 이 맥에서는 체크가 **통과**한다. 그러나 UBSan이 다음을 보고한다.

```text
wire_cast.c:395:17: runtime error: load of misaligned address 0x00010026eaa1 for type 'const uint32_t' (aka 'const unsigned int'), which requires 4 byte alignment
```

Cortex-M0/M0+(ARMv6-M)는 비정렬 접근이 HardFault다. Cortex-M3/M4(ARMv7-M)는 LDR/LDRH/STR/STRH 비정렬을 허용하지만(`UNALIGN_TRP`가 꺼져 있을 때), LDM/STM/LDRD/STRD는 정렬이 필요하다. 그래서 바이트로 조립하거나 `memcpy`를 쓰는 것이 휴대 가능한 답이다.

`soa_window_stats`의 범위 검사는 `first + count > s->n`처럼 쓰면 감김 버그가 있다(변형 `window_overflow`: `count = UINT32_MAX`가 통과해 FAIL 1개). 뺄셈으로 쓴다.

### 측정에서 뽑은 설계 지침

아래 지침은 **이 맥에서 측정한 것만** 근거로 한다.

1. **일부 필드만 읽는 스캔은 SoA.** SIMD가 켜지면 AoS 대비 4.5~15배(`sum_x`). SIMD를 끄면 L1 안에서는 차이가 없고 L2/DRAM에서 2~3배뿐이다. "캐시 때문에 빠르다"라고만 말하면 틀린다.
2. **거의 모든 필드를 건드리는 갱신에서는 AoS가 나쁘지 않다.** SIMD 빌드에서 SoA가 1.5~2배 빠른 것이 전부고, SIMD가 없으면 AoS가 약 2배 빠르다.
3. **AoSoA는 공짜가 아니다.** W=8은 `update_pos`의 DRAM 영역에서 AoS보다 느렸다(2.3 vs 1.3 ns). W=32는 라인 크기(128B)에 맞아 나았다. W는 측정으로 정한다.
4. **조건 연산이 있으면 레이아웃보다 분기 제거가 먼저다.** 난수 플래그에서 분기 있는 버전은 16 MB 이상에서 2.3~3.1 ns(레이아웃 무관), 분기 없는 SoA는 0.09 ns다.
5. **자동 벡터화를 믿지 말고 재라.** 분기 없는 AoS 커널은 벡터화 후 빨라지지 않았다(중앙값 약 2배, 최솟값 1.1~1.4배 느림). `-Rpass`의 "vectorized"는 빠르다는 뜻이 아니다.
6. **연결 리스트는 노드의 메모리 순서가 문제다.** 할당 순서로 놓으면 1 ns대, 뒤섞이면 DRAM 크기에서 약 100 ns다. 이 측정에서 끌어낸 추론 하나: 할당/해제를 오래 반복해 순서가 섞인 풀은 후자에 가까워진다.
7. **시작 주소 정렬(라인 맞춤)은 이 CPU에서는 성능 차이를 못 찾았다.** 정렬 강제는 기능 이유(DMA, 원자성, SIMD 명령 제약)로 넣는다.

### D-캐시가 없는 Cortex-M에서는

Cortex-M4 같은 칩은 D-캐시가 없다(보통 SRAM에 직접 접근). 그러면 **캐시 라인 논리가 사라진다.** 레이아웃의 비용은 명령어 수로 돌아간다. `demo_asm.c`를 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S`로 본 실제 출력이다.

```text
; aos_sum_x (Cortex-M4) — 네 번 펼친 루프
ldr.w  r4, [r2, #264]
ldr.w  r5, [r2, #328]
ldr.w  r6, [r2, #392]
ldr.w  r7, [r2, #456]
; soa_sum_x (Cortex-M4)
ldr    r4, [r2, #16]!
ldr    r4, [r2, #4]
ldr    r5, [r2, #8]
ldr    r6, [r2, #12]
```

(가운데 `add`/`subs`는 줄였다.) 두 레이아웃 모두 원소당 `ldr` 하나다. **SRAM이 균일 지연이면 AoS와 SoA의 sum_x는 같은 일을 한다.** 차이는 레지스터 사용이다. AoS의 `update_pos`는 base 하나 + 즉값 오프셋으로 끝난다. SoA는 필드 포인터 6개와 n이 인자로 온다(AAPCS는 앞 4개만 레지스터, 나머지는 스택). 실제 Cortex-M4 출력에서도 차이가 보인다. SoA `update_pos`는 `push.w {r4, r5, r6, r7, r8, r9, r10, r11, lr}`에 `@ 4-byte Spill`이 붙은 스택 저장이 나왔고, AoS는 `push {r4, r5, r6, lr}`로 끝났다. 레지스터가 더 적은 Cortex-M0+에서는 더 불리할 것이다(추정). 이 레슨은 이 칩의 사이클을 재지 않았으니, 실제 판단은 **DWT->CYCCNT**로 한다.

MVE가 있는 Cortex-M55(`--target=thumbv8.1m.main-none-eabi -mcpu=cortex-m55`)에서는 다시 SIMD 이야기가 된다.

```text
; soa_sum_x (Cortex-M55, MVE)
vldrw.u32  q0, [r0], #16
vaddva.u32 r2, q0
; aos_sum_x (Cortex-M55, MVE) — 스트라이드 gather
vldrw.u32  q2, [q1]
vaddva.u32 r2, q2
```

MVE에는 진짜 gather 명령이 있어서 AoS도 벡터화되지만, 주소 벡터를 만드는 추가 명령이 든다(`vshl`, `vadd`). D-캐시를 넣은 Cortex-M7 구현(라인 32바이트)에는 이 레슨의 라인 모델 32B 열이 적용된다. 위의 M4 asm은 이 레슨의 컴파일 결과이고, 플래시 대기 상태나 버스 경합은 이 측정에 없다.

<!-- code: demo_bench.c -->

<!-- code: demo_asm.c -->

```bash
cc -std=c11 -O2 -Wall -Wextra demo_bench.c -o bench && ./bench all          # 기본, AoSoA W=8
cc -std=c11 -O2 -DAW=32 demo_bench.c -o bench32 && ./bench32 layouts        # AoSoA W=32
cc -std=c11 -O2 -fno-vectorize -fno-slp-vectorize demo_bench.c -o nv && ./nv layouts
cc -std=c11 -O2 -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize -c demo_bench.c
cc -std=c11 -O2 -Rpass=slp-vectorizer -c demo_bench.c
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S -o - demo_asm.c
```

실행 환경은 Apple M6, `-O2`, 부하 평균 10~52(같이 돌던 작업 때문)다. 절대 ns보다 비율을 보고, 2배 미만의 차이는 해석하지 않는다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution                # 51/51 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o s_san && ./s_san   # clean
```

체크 결과 끝부분(실제 출력):

```text
sizeof Rec=64 Block=512 Aos=65664 Soa=65664 Aosoa=65664
  list line switches: in-order 512, shuffled 1024
  checksum aos=7da3244ae852058e soa=7da3244ae852058e aosoa=7da3244ae852058e
51/51 passed
```

## 7. 자주 하는 실수

**1. 필드별 루프로 쪼개면서 의존 순서를 바꾼다.** `vx` 감쇠를 위치 적분보다 먼저 돌리면 값이 조금씩 달라진다. 컴파일도 되고 눈으로 봐서는 안 보인다. 레퍼런스 비교가 필요한 이유다.

```c
/* 틀림 */ for (i) s->vx[i] -= s->vx[i] >> 4;  for (i) s->x[i] += s->vx[i];
/* 맞음 */ for (i) s->x[i] += s->vx[i];        for (i) s->vx[i] -= s->vx[i] >> 4;
```

**2. AoSoA 꼬리 블록을 통째로 돈다.** `l < W`로 돌리면 n 밖 레인을 읽거나 쓴다. 블록이 다 차 있다고 가정하지 말고 `m = min(W, rem)`을 쓴다.

**3. W를 2의 거듭제곱이 아니게 잡는다.** `i & (W-1)`이 깨진다. `_Static_assert`로 막거나 나눗셈으로 쓴다(비용은 측정).

**4. `-Rpass`의 "vectorized"를 "빨라졌다"로 읽는다.** 이 레슨의 AoS `sum_x`(L2 이상에서 이득 없음)와 `sum_mask_bl`이 반례다. asm을 보고, SIMD를 끈 빌드와 비교해야 한다.

**5. SoA 포인터 사이의 앨리어싱을 잊는다.** 필드 포인터를 그냥 넘기면 컴파일러가 런타임 겹침 검사를 넣거나 벡터화를 포기한다. `restrict`를 쓰거나, 정적 배열(필드가 겹치지 않는 게 자명)로 둔다.

**6. 타이밍을 CHECK에 넣는다.** 측정값이 부하에 따라 칸마다 수십 % 흔들렸다(최악 178%). 시간 측정은 demo에만 둔다. 체크에는 라인 수처럼 결정적인 숫자만 넣는다.

**7. 패킷을 구조체로 캐스트한다.** `(const Frame *)buf`는 비정렬에서 UB이고 패딩 때문에 필드 위치도 다르다. 바이트로 조립하거나 `memcpy`를 쓴다.

**8. 범위 검사를 덧셈으로 쓴다.** `first + count > n`은 `uint32_t` 감김으로 통과한다. `count > n - first`로 쓴다.

**9. "캐시 라인에 정렬하면 빠르다"고 단정한다.** 측정해 보면 이 M6에서는 차이가 없었다. 정렬이 진짜 필요한 곳은 DMA 버퍼, 원자 변수 경계, SIMD 정렬 명령을 쓰는 코드다.

## 8. 면접 Q&A

<details><summary>Q1. AoS와 SoA 중 언제 무엇을 고르나요?</summary>

접근 패턴을 먼저 본다. **일부 필드만 많이 스캔**하면 SoA다. 이 레슨에서 x 합산은 SoA가 4.5~15배 빨랐다. **레코드 대부분을 함께 읽고 쓰거나, 인덱스로 레코드를 하나씩 다루는** 코드는 AoS가 낫다. 전체 갱신은 SIMD 없이 AoS가 약 2배 빨랐고, 레코드 복사와 교환은 AoS가 한 번이다. SoA는 필드 수만큼 필요하다. 중간 지점이 Hot/Cold 분리다(연습 2). 답하기 전에 "측정 환경에서 SIMD가 돌아가나"를 먼저 말하면 좋다.
</details>

<details><summary>Q2. "SoA가 빠른 건 캐시 때문이죠?" — 어떻게 검증하죠?</summary>

두 원인을 분리해야 한다. 나는 SIMD를 끈 빌드(`-fno-vectorize -fno-slp-vectorize`)와 켠 빌드를, 캐시 안과 밖 작업 집합에서 비교했다. 16 KB에서는 SIMD를 끄면 AoS와 SoA가 0.286 ns로 똑같았다. 즉 L1 안의 차이는 **전부 SIMD**다. 4 MB에서는 SIMD를 꺼도 1.9배 차이가 나는데, 이것이 순수한 캐시 효과다. 256 MB에서는 3배다. 꼬리질문으로 "왜 SIMD를 켜도 L2 이상에서 AoS는 안 빨라지나요?"가 나오면, 64바이트 스트라이드의 원소 네 개를 레인마다 따로 로드하는 실제 `-S`를 보여 준다.
</details>

<details><summary>Q3. AoSoA의 W는 어떻게 정하죠?</summary>

두 가지 축이다. SIMD 폭의 배수이고(NEON u32 4레인이면 W=4/8/16), 필드 하나의 W개가 **캐시 라인과 맞아야** 한다. 라인 모델에서 W×4 = 라인 크기일 때 SoA와 같은 라인 수가 나온다(32B 라인이면 W=8, 128B 라인이면 W=32). 측정도 같은 방향이었다. x만 읽을 때 W=32가 W=8보다 DRAM에서 3배 빨랐다. 다만 필드 여러 개를 쓰는 커널에서는 블록 안에서 띄엄띄엄 쓰게 되어, `update_pos`의 W=8은 DRAM에서 AoS보다 느렸다. 정답은 측정이다.
</details>

<details><summary>Q4. D-캐시 없는 Cortex-M에서는 이 얘기가 어떻게 바뀌나요?</summary>

SRAM이 균일 지연이면 "라인을 몇 개 건드리나"가 의미가 없다. 명령어 수와 레지스터 수의 문제가 된다. 실제 Cortex-M4 출력에서 AoS와 SoA의 `sum_x`는 둘 다 원소당 `ldr` 하나다. AoS는 base 하나에 즉값 오프셋으로 끝나서 레지스터가 적은 코어에서 유리할 수 있다(추정, DWT로 측정해야 함). SoA는 필드 포인터가 많아 스필이 생길 수 있다. Cortex-M7은 D-캐시가 있어 라인 32B 이야기로 돌아오고, DTCM에 두면 캐시를 우회한다. MVE가 있는 M55에서는 SoA가 `vldrw + vaddva`로 벡터화된다.
</details>

<details><summary>Q5. 캐시 라인 정렬(alignas(64))은 언제 필요하죠?</summary>

이 맥의 측정에서는 시작 주소를 +0/+4/+1로 바꿔도 차이를 못 찾았다. 그러니 "빨라진다"가 이유는 아니다. 필요한 곳은 셋이다. ① **DMA 버퍼**: 캐시 무효화는 라인 단위라서, 버퍼가 라인을 공유하면 옆 변수의 변경이 날아간다(연습 4). ② **스레드 사이의 false sharing**: 서로 다른 코어가 쓰는 변수를 같은 라인에 두지 않는다. 이 맥의 라인은 128B라 64B로 맞추면 부족할 수 있다(이 레슨에서 측정하지는 않았다). ③ **정렬 요구 명령**: Cortex-M0 비정렬 금지, LDRD/LDM 정렬 요구.
</details>

<details><summary>Q6. 연결 리스트는 왜 느린가요? 그래도 쓰는 이유는요?</summary>

포인터 추적 자체는 1 ns 근처였다. 이 레슨의 측정에서 **할당 순서로 놓인 리스트는 256 MB에서도 1.3 ns**였다. 느린 것은 **순서가 뒤섞인 경우**로 256 MB에서 102 ns였다. 다음 주소를 알 수 없어 프리페치가 안 되고, 체인이 직렬 의존이다. 그래도 쓰는 이유는 침입형 리스트의 O(1) 제거(DS-M01), 고정 풀과 맞음, ISR에서 메모리 이동 없이 쓰는 것이다. 순회가 잦다면 배열이나 풀 순서 유지를 먼저 생각한다.
</details>

<details><summary>Q7. update_pos에서 AoSoA-8이 AoS보다 느린 이유를 뭐라고 설명하죠?</summary>

측정이 보여 주는 것은 "DRAM 영역에서 2.28 vs 1.33 ns"뿐이다. 내 가설은 하드웨어 프리페처다. AoS는 모든 라인을 순서대로 건드려 스트림 프리페처에 이상적이고, AoSoA-8은 라인 2개 쓰고 2개 건너뛰는 패턴이 된다. PMU 없이 돌렸으니 이것은 **검증되지 않은 설명**이다. 확인하려면 메모리에 W=8 패턴을 흉내 낸 합성 트레이스로 프리페처 영향을 분리하거나, 코어 PMU가 있는 환경에서 프리페치 카운터를 본다.
</details>

<details><summary>Q8. 패딩을 없앤다고 __packed를 써도 되나요?</summary>

쓰면 크기는 줄지만 멤버가 비정렬이 된다. Cortex-M0는 그 멤버 접근이 폴트고, 컴파일러는 packed 멤버를 바이트/하프워드로 쪼개 읽어 코드가 커지며, 그 필드의 포인터를 함수에 넘기면 UB 위험이 있다. 먼저 **멤버를 정렬 내림차순으로 놓는다**. 이 레슨의 BadOrder는 재배치만으로 32 → 16바이트가 되었다. `packed`는 와이어 포맷처럼 레이아웃이 외부에서 정해질 때만 쓰고, 그때도 저장소로는 바이트 배열로 다룬다.
</details>

## 9. 추가 연습

### 연습 1 — 정렬 내림차순이 최소 크기인가

필드 3~7개를 무작위로 뽑아, 정렬 내림차순으로 놓은 크기가 **모든 순열의 최솟값**과 같은지 300번 확인한다. (필드 크기는 자기 정렬의 배수로 한정)

<details><summary>답</summary>

`layout_size`로 전수 탐색(재귀 순열)과 `sort_by_align_desc` 결과를 비교한다. 정렬이 2의 거듭제곱이고 크기가 정렬의 배수이면, 큰 정렬부터 놓을 때 중간 패딩이 생기지 않고 꼬리 패딩만 남는다. 전체 크기는 "멤버 합을 최대 정렬로 올린 값"보다 작을 수 없으니 이것이 최솟값이다. 300회 모두 통과한다.
</details>

### 연습 2 — Hot/Cold 분리

레코드를 `Hot` 28바이트(`x,y,z,vx,vy,vz,temp,flags,health`)와 `Cold` 40바이트로 나누고, x 합산이 AoS와 같은지와 건드리는 라인 수를 비교한다.

<details><summary>답</summary>

실제 출력은 `x 스캔 라인 수 (128B): AoS 512, Hot/Cold 224, SoA 32`다. 스트라이드가 64에서 28로 줄어 라인 수가 2.3배 줄었다. 필드를 더 잘게 쪼갤수록 SoA에 가까워진다. 구조체 접근 코드를 거의 안 바꾸고도 얻는 이득이라 기존 펌웨어를 고칠 때 제일 먼저 해 볼 만하다. FTL 블록 메타데이터에서 valid count와 상태를 hot으로, 소거 횟수와 이력을 cold로 나누는 것이 같은 발상이다.
</details>

### 연습 3 — SoA의 flags를 비트맵으로

`flags`를 1024바이트에서 128바이트 비트맵으로 줄이고, 비트가 켜진 x만 합산한다.

<details><summary>답</summary>

`bits &= bits - 1`과 `__builtin_ctz`로 켜진 비트만 건너뛴다. 마지막 워드는 `n` 밖 비트를 마스킹한다(`bits &= (1u << (n & 31)) - 1`). `n & 31 == 0`이면 마지막 워드가 통째로 유효하니 마스킹하지 않는다(`(1u << 0) - 1 == 0`이라 전부 지워진다). 플래그 메모리는 8배 준다. Cortex-M3/M4 이상은 `CLZ`가 있지만 `CTZ`는 `RBIT`+`CLZ` 조합이고, Cortex-M0는 둘 다 없다.
</details>

### 연습 4 — DMA 수신 버퍼와 라인

Cortex-M7(라인 32B)의 수신 버퍼 `[addr, addr+len)`에서 무효화해도 안전한 안쪽 구간을 계산한다. `rx_buffer_ok`는 시작과 길이가 라인 배수인지 본다.

<details><summary>답</summary>

`start`는 올림, `end`는 내림이다. 예: `0x20000010 + 64` → 안전 구간 `[0x20000020, 0x20000040)`. 양 끝의 부분 라인에는 옆 변수가 같이 있어서, 무효화하면 그 변수의 변경이 사라진다. 그래서 `alignas(32)`로 시작을 맞추고 크기를 32의 배수로 잡는다. CMSIS의 `SCB_InvalidateDCache_by_Addr`는 주소/길이를 받지만, 라인에 맞추는 책임은 호출자에게 있다. 이 레슨의 측정이 성능에서 찾지 못한 정렬의 필요가 여기에 있다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice      # 18/18 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o p_san && ./p_san
```

## 10. 핵심 정리

- 레이아웃은 **접근 패턴과 SIMD**로 고른다. 일부 필드를 많이 스캔하면 SoA, 레코드 전체를 함께 다루면 AoS, 중간은 Hot/Cold 분리.
- 이 M6에서 `sum_x`의 SoA 이득은 L1에서 **4.5배(SIMD 끄면 1.0배)**, L2에서 14배(SIMD 끄면 1.9배), DRAM에서 15배(SIMD 끄면 3배). 큰 부분은 SIMD다.
- 전체 갱신은 SIMD 없으면 AoS가 약 2배 빠르고, SIMD가 있으면 SoA가 1.5~2배 빠르다.
- AoSoA는 W×필드 크기 = 라인일 때 부분 스캔이 좋다. W가 라인과 안 맞으면(W=8, 128B 라인) DRAM 갱신에서 AoS보다 느릴 수 있다.
- 분기 있는 마스크 합산은 16 MB 이상에서 레이아웃과 무관하게 2.3~3.1 ns, 분기 없는 SoA는 0.09 ns. 분기가 먼저다.
- `-Rpass`의 "vectorized"는 빠르다는 뜻이 아니다. 분기 없는 AoS 커널은 벡터화로 빨라지지 않았다. `-S`와 no-SIMD 빌드로 확인한다.
- 리스트는 순서가 문제다. 할당 순서면 1 ns대, 뒤섞이면 DRAM에서 약 100 ns.
- 시작 주소 정렬은 이 CPU에서 측정 가능한 이득이 없었다. DMA, false sharing, 비정렬 금지 코어 때문에 쓴다. Cortex-M4는 D-캐시가 없어서 라인 논리 자체가 없고, 명령어와 레지스터 수 문제로 돌아온다.

## 참고자료

- U. Drepper, "What Every Programmer Should Know About Memory", 2007 — 캐시 계층, 프리페치, 데이터 배치
- M. Acton, "Data-Oriented Design and C++", CppCon 2014 — 레이아웃 우선 설계
- Clang 문서, "Auto-Vectorization in LLVM" — `-Rpass=loop-vectorize`, SLP 벡터라이저, `-fno-vectorize` / `-fno-slp-vectorize`
- Arm, *Armv7-M Architecture Reference Manual* (DDI 0403) — 비정렬 접근 규칙, `UNALIGN_TRP`, LDM/STM 정렬
- Arm, *Cortex-M7 Technical Reference Manual* — L1 캐시 라인 32바이트; CMSIS `SCB_InvalidateDCache_by_Addr`
- Arm, *Armv8.1-M Architecture Reference Manual* — MVE gather 로드 (see:ARM)
- 이 맥의 값: `sysctl hw.cachelinesize hw.perflevel0.l1dcachesize hw.perflevel0.l2cachesize` 등 — 본문의 수치 출처
- 관련 레슨: DS-M01 (침입형 리스트), ARCH-M07 (루프 순서·타일링), HPC-B06 (벡터화 커널), GPGPU-B07 (코얼레싱), MEM-B10 (포인터 체이싱 지연), DS-A01 (false sharing)
