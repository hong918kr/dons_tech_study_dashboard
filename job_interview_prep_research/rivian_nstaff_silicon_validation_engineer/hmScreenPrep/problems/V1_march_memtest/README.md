# V1 · Memory Test Engine for SRAM/DDR Bring-up

> **ID**: V1 · **Priority**: P0 · **Pattern**: fake memory + fault injection: walking-1s, power-of-two address test, March C- · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh V1` / `./run.sh V1 sol`

## 0. 문제 한국어로 먼저 보기

> 영어 원문(§1)을 읽기 전에 **무슨 상황이고 뭘 만들어야 하는지**만 빠르게 잡는 요약입니다. 풀이 힌트는 없습니다.

**한 줄 요약**: 새 SoC의 on-chip SRAM(나중엔 DDR)을 bring-up할 때 가장 먼저 돌리는 **bare-metal 메모리 테스트 엔진** 3종(data bus, address bus, March C-)을 짭니다.

**상황**
- 메모리를 아직 믿을 수 없는 bring-up 초기 단계입니다. 다른 무엇보다 먼저 이 테스트가 돌아갑니다.
- 메모리는 **word 단위 주소**(`addr` = word index)이고, `mem_wr(addr, val)` / `mem_rd(addr)` 두 함수로만 접근합니다.
- `stub.c` 아래쪽의 **fake memory**(256 × `uint32_t`)가 실제 메모리를 흉내 내며, 고장을 일부러 넣을 수 있습니다. fake는 수정 금지입니다.
  - **stuck-at-0/1**: 한 word의 어떤 비트가 항상 0(또는 1)으로 고정됨
  - **inversion coupling fault**: word A에 0→1 쓰기를 하면 word B의 한 비트가 뒤집힘 (A = aggressor, B = victim)
  - **address-line fault**: 주소 비트 k가 0 또는 1로 고정되어 서로 다른 두 주소가 같은 칸을 가리킴 (**alias**)
- 실패 정보는 `MemFail { addr, expected, actual, element }`에 담습니다. `addr`는 실패한 read의 word 주소, `element`는 테스트마다 뜻이 다릅니다 (March element 0..5 / data bus는 bit index / address bus는 address bit).
- **March C-** 표기: `⇑` 오름차순, `⇓` 내림차순, `⇕` 아무 방향(여기선 오름차순 사용). `w0` = 0 쓰기, `r1` = 1을 기대하며 읽기. "0"/"1"은 background `0x00000000` / `0xFFFFFFFF`를 뜻함.

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `memtest_databus(addr, f)` | word 하나에 **walking ones** | `bit = 0..31`마다 `1u << bit`를 쓰고 다시 읽음. 불일치 시 `element = bit` |
| `memtest_addrbus(base, nwords, bad_bit, f)` | **power-of-two 주소 테스트** | `(1u << k) < nwords`인 모든 k에 대해 `base + (1u << k)` 위치를 검사 (아래 3단계). 실패 시 `*bad_bit = k`, `f = { 읽은 addr, PATTERN, actual, k }`. 통과 시 `*bad_bit = -1`. `base`는 `nwords` 이상인 가장 가까운 2의 거듭제곱에 정렬돼 있다고 가정 |
| `memtest_march_cminus(base, nwords, f)` | `[base, base + nwords)` 구간에 **March C-** | `M0 ⇕(w0); M1 ⇑(r0,w1); M2 ⇑(r1,w0); M3 ⇓(r0,w1); M4 ⇓(r1,w0); M5 ⇕(r0)`. **첫 불일치에서 멈춤**. 깨끗하면 정확히 **10N 접근**. 구간 밖은 건드리지 않음 |
| 공통 반환값 | | `0` = 통과, `-EIO` = 고장 발견(`*f`에 **첫 번째** 실패), `-EINVAL` = 잘못된 인자 (`f == NULL`, `nwords == 0`, 범위가 `MEM_WORDS`를 넘음) |

**`memtest_addrbus`의 3단계** (`PATTERN = 0xAAAAAAAA`, `ANTI = 0x55555555`)
1. `base`와 모든 offset에 `PATTERN`을 씀.
2. `base`에만 `ANTI`를 씀 → 모든 offset은 여전히 `PATTERN`이어야 함 (아니면 bit k가 `base`와 alias).
3. `base`를 원래대로 돌려놓고, k마다 offset k에 `ANTI`를 쓴 뒤 → `base`와 **다른 모든 offset**이 여전히 `PATTERN`이어야 함 (아니면 주소 비트끼리 short). 확인 후 복구.

**예시를 말로 풀면**
- **word 10 bit 7 stuck-at-1**: M0이 전부 0을 쓴 뒤 M1(r0)에서 word 10을 읽으면 `0x00000080`이 나옵니다. 기대값 0과 다르므로 `-EIO`, `element 1`.
- **coupling** (aggressor word 20이 victim word 7보다 위): word 20의 비트를 0→1로 올리는 쓰기가 word 7의 bit 3을 뒤집습니다. 오름차순 M1에서 word 7이 먼저 1로 채워진 뒤 word 20이 1로 쓰이며 word 7의 bit 3이 0이 됩니다. 그래서 M2(r1)에서 word 7을 읽으면 `0xFFFFFFF7` → `element 2`.
- **address bit 3 stuck-at-0** (word 8 = word 0): 2단계에서 `base`(0)에 `ANTI`를 쓰면 offset 8에서도 `0x55555555`가 읽힙니다. 기대는 `PATTERN`이므로 `bb = 3`, `f = { addr 8, exp 0xAAAAAAAA, act 0x55555555, element 3 }`.
- **깨끗한 메모리, N = 64**: word마다 read 5번 + write 5번 = 10번 접근이므로 총 640회 = **320 reads + 320 writes**, 반환 0.

**지켜야 할 조건**
- `MEM_WORDS = 256`. 메모리는 실제 전원 투입 직후 SRAM처럼 쓰레기 값(`0xA5A5A5A5`)으로 시작함.
- fake가 모든 접근 횟수(`F.rd`, `F.wr`)와 범위 밖 접근(`F.oob`)을 세므로, 10N 규칙과 "범위 안에서만 접근"이 정확히 채점됨.
- `malloc` 금지. C11, single thread. cache/MMU는 모델링 안 됨 (accessor가 곧 "volatile" 경로).

## 1. Problem

You are bringing up the on-chip SRAM (and later the DDR controller) of a new SoC. Write the bare-metal memory test engine that the bring-up script runs before anything else is trusted.

Memory is word-addressed and only reachable through two accessors. At the bottom of `stub.c` they are implemented as a **fake memory** (256 × `uint32_t`) with injectable faults: stuck-at-0/1 bits in one word, an inversion coupling fault (a 0→1 write to word A flips a bit in word B), and an address-line fault (address bit k stuck at 0 or 1, so two addresses alias). Do not edit the fake.

```c
#define MEM_WORDS 256u
void     mem_wr(uint32_t addr, uint32_t val);   /* addr = word index */
uint32_t mem_rd(uint32_t addr);

typedef struct {
    uint32_t addr;       /* word address of the failing read */
    uint32_t expected;
    uint32_t actual;
    int      element;    /* march element 0..5 | bit index (databus) | address bit (addrbus) */
} MemFail;

/* 0 = pass, -EIO = fault found (*f = FIRST failure), -EINVAL = bad arguments */
int memtest_databus(uint32_t addr, MemFail *f);
int memtest_addrbus(uint32_t base, uint32_t nwords, int *bad_bit, MemFail *f);
int memtest_march_cminus(uint32_t base, uint32_t nwords, MemFail *f);
```

- **`memtest_databus`** — walking ones on one word: for `bit = 0..31` write `1u << bit` and read it back. On mismatch report `element = bit`.
- **`memtest_addrbus`** — power-of-two offsets `base + (1u << k)` for every `(1u << k) < nwords`:
  1. write `PATTERN = 0xAAAAAAAA` to `base` and every offset;
  2. write `ANTI = 0x55555555` to `base`; every offset must still read `PATTERN` (otherwise bit k aliases `base`);
  3. restore `base`; for each k write `ANTI` to offset k and check `base` and every other offset still read `PATTERN` (bits shorted together); restore.
  On failure `*bad_bit = k`, `f = { addr read, PATTERN, actual, k }`. On pass `*bad_bit = -1`. Assume `base` is aligned to the next power of two ≥ `nwords`.
- **`memtest_march_cminus`** over `[base, base + nwords)` with backgrounds `0x00000000` / `0xFFFFFFFF`:
  `M0 ⇕(w0); M1 ⇑(r0,w1); M2 ⇑(r1,w0); M3 ⇓(r0,w1); M4 ⇓(r1,w0); M5 ⇕(r0)` (⇑ ascending, ⇓ descending, ⇕ either — use ascending). Stop at the first mismatch. A clean run is exactly 10N accesses. Touch nothing outside the range.
- No malloc. `f == NULL`, `nwords == 0`, or a range past `MEM_WORDS` → `-EINVAL`.

### Example

```
fault: word 10 bit 7 stuck-at-1
memtest_march_cminus(0, 32, &f) -> -EIO   f = { addr 10, exp 0x00000000, act 0x00000080, element 1 }

fault: write that raises a bit of word 20 inverts bit 3 of word 7 (coupling, aggressor above victim)
memtest_march_cminus(0, 32, &f) -> -EIO   f = { addr 7, exp 0xFFFFFFFF, act 0xFFFFFFF7, element 2 }

fault: address bit 3 stuck-at-0 (word 8 aliases word 0)
memtest_addrbus(0, 256, &bb, &f) -> -EIO  bb = 3, f = { addr 8, exp 0xAAAAAAAA, act 0x55555555, element 3 }

clean memory, N = 64: memtest_march_cminus -> 0, 320 reads + 320 writes
```

### Constraints

- `MEM_WORDS = 256`. Memory starts with garbage (`0xA5A5A5A5`), like real power-up SRAM.
- The fake counts every access (`F.rd`, `F.wr`) and out-of-range accesses (`F.oob`), so the 10N rule and "stay in range" are tested exactly.
- C11, single thread, caches/MMU not modeled (accessors are the "volatile" path).

### Clarifying questions to ask (first 3 minutes)

1. Word width and addressing: is `addr` a word index or a byte address? 32-bit bus?
2. Should the test stop at the first failure or log all failures (and how many)?
3. Is the region I am testing also where my code, stack, or page tables live?
4. Is ECC enabled on this memory? Are caches enabled for this region?
5. Which fault classes matter for this bring-up stage — just connectivity (bus), or cell faults too?
6. Is a destructive test allowed, or must I save and restore the contents?

## 2. Your workspace

`./run.sh V1`로 바로 테스트하세요. 테스트 8묶음(32개 CHECK)이 있고, 파일 아래쪽 fake memory는 건드리지 않습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 세 테스트는 순서가 있다</summary>

data bus → address bus → cell(March) 순서입니다. data bus가 깨져 있으면 address test의 패턴 비교부터 믿을 수 없고, address bus가 깨져 있으면 March 결과가 "셀 고장"처럼 보이지만 실제로는 배선 문제입니다. 각 테스트는 앞 단계가 통과했다고 가정하고 **필요한 최소한의 접근**만 합니다(walking-1은 32번, address test는 O(log N)번).

</details>

<details>
<summary>Hint 2 — March는 "원소 함수 하나"로</summary>

`march_elem(base, n, up, do_rd, rd_val, do_wr, wr_val, elem, f)` 하나를 만들고 6번 부르면 끝입니다. 내려가는 방향은 `a = base + (n - 1 - i)`로 쓰세요. `for (uint32_t a = base + n - 1; a >= base; a--)`는 `base == 0`일 때 무한 루프입니다(unsigned는 항상 ≥ 0).

</details>

<details>
<summary>Hint 3 — 실패 보고는 한 군데에서</summary>

`static int fail(f, addr, exp, act, elem)`가 구조체를 채우고 `-EIO`를 돌려주게 하면 세 테스트 모두 `return fail(...)` 한 줄로 끝납니다. 첫 실패에서 바로 멈추므로 `f`에는 항상 **첫 번째** 실패가 남습니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — March C- 6개 원소

March 테스트는 "모든 주소를 같은 순서로 훑으면서, 각 주소에서 같은 연산 묶음을 한다"를 6번 반복합니다. 핵심은 **읽기가 항상 쓰기 앞**에 있다는 점입니다. 각 주소에서 "앞 원소가 써 놓은 값이 아직 그대로인가"를 먼저 확인하고 나서 반대 값으로 바꿉니다. 올라가는 방향(⇑)과 내려가는 방향(⇓)을 둘 다 쓰는 이유는 coupling fault의 aggressor가 victim보다 위에 있든 아래에 있든 잡기 위해서입니다.

<div class="chart" data-src="chart1.svg" data-caption="March C-: 원소별 방향·연산·끝난 뒤 배경값·접근 수. 아래 빨간 글씨는 이 문제 테스트에서 각 고장이 처음 잡힌 원소"></div>

고장별로 어디서 잡히는지:

| 고장 | 잡히는 곳 | 이유 |
|---|---|---|
| SA1 (bit가 항상 1) | M1 `r0` | M0에서 0을 썼는데 1이 읽힘 |
| SA0 (bit가 항상 0) | M2 `r1` | M1에서 1을 썼는데 0이 읽힘 |
| TF (0→1 전이 실패) | M2 `r1` | SA0와 같은 모양으로 보임 |
| CF, aggressor A < victim V | M1 (⇑) | ⇑ 순서라 A가 먼저 w1 → V가 뒤집힌 채로 `r0` |
| CF, A > V | M2 (⇑) | M1에서 V가 먼저 1이 되고 A의 w1이 V를 뒤집음 → M2의 `r1`에서 발견 |
| 주소 aliasing (A3 SA0) | M1 `r0` @8 | 주소 0의 w1이 실제로는 같은 셀 → 8에서 0을 기대했는데 1 |

### 4.2 Address-line fault — 왜 2의 거듭제곱 주소만 보면 되나

주소선 하나가 고장 나면 그 비트만 다른 주소 쌍이 같은 셀로 갑니다. 주소선이 log₂N개뿐이므로 `base + 1, 2, 4, …`만 확인하면 모든 주소선을 한 번씩 토글해 본 셈입니다. 그래서 address test는 O(N)이 아니라 O(log N) 접근으로 끝납니다(정상일 때 256워드 기준 read 72 + write 27).

<div class="chart" data-src="chart2.svg" data-caption="A3이 GND에 붙은 경우: 논리 주소 8–15가 물리 셀 0–7로 겹치고 셀 8–15는 영영 안 쓰임. 2단계에서 주소 0에 ANTI를 쓰면 주소 8에서 그 값이 보임"></div>

stuck-at-1(A2 SA1)도 같은 2단계에서 잡힙니다: 주소 0에 쓴 ANTI가 실제로는 셀 4에 가므로 offset 4를 읽을 때 ANTI가 보이고 `bad_bit = 2`, `addr = 4`가 됩니다.

### 4.3 손으로 따라가기 (trace)

작은 예: `N = 4`, coupling fault aggressor = 3, victim = 1, mask = bit 3 (A > V). 초기값 `0xA5A5A5A5`.

| 원소 | 주소 | 동작 | 읽은 값 | 셀 0 / 1 / 2 / 3 후 | 메모 |
|---|---|---|---|---|---|
| M0 ⇕ | 0,1,2,3 | w0 | – | 0 / 0 / 0 / 0 | A5→0은 0→1 전이가 없어 CF 발동 안 함 |
| M1 ⇑ | 0 | r0, w1 | 0 ✓ | F / 0 / 0 / 0 | F = `0xFFFFFFFF` |
| M1 ⇑ | 1 | r0, w1 | 0 ✓ | F / F / 0 / 0 | |
| M1 ⇑ | 2 | r0, w1 | 0 ✓ | F / F / F / 0 | |
| M1 ⇑ | 3 | r0, w1 | 0 ✓ | F / **FFFFFFF7** / F / F | A의 0→1 쓰기 → V의 bit 3 반전 |
| M2 ⇑ | 0 | r1, w0 | F ✓ | 0 / FFFFFFF7 / F / F | |
| M2 ⇑ | 1 | r1 | **FFFFFFF7 ✗** | | `f = {1, 0xFFFFFFFF, 0xFFFFFFF7, 2}` |

멈출 때까지 read 6번, write 9번입니다. 테스트 5번(A = 20, V = 7, N = 32)도 같은 모양으로 `{7, 0xFFFFFFFF, 0xFFFFFFF7, 2}`가 나옵니다.

Address test, A3 SA0, `nwords = 256`:

| 단계 | 동작 | 결과 |
|---|---|---|
| 1 | `PATTERN`을 0, 1, 2, 4, 8, 16, 32, 64, 128에 씀 (write 9) | 8은 실제로 셀 0에 씀 |
| 2 | `ANTI`를 0에 씀 (write 10) | 셀 0 = ANTI |
| 2 | offset 1, 2, 4 읽기 | `PATTERN` ✓ |
| 2 | offset 8 읽기 (read 4) | 셀 0 → `0x55555555` ✗ → `bad_bit = 3` |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `range_ok`는 `nwords <= MEM_WORDS - base`로 씁니다. `base + nwords <= MEM_WORDS`는 `base`가 크면 덧셈이 넘칠 수 있습니다(실제 64-bit 주소 공간에서는 흔한 버그).
- 내려가는 원소는 `base + (n - 1u - i)`. 인덱스 `i`는 항상 올라가므로 unsigned 언더플로가 없습니다.
- walking-1 실패 보고의 `expected`는 쓴 패턴(`1u << bit`), `actual`은 읽은 값입니다. `expected ^ actual`이 곧 고장 난 data line 마스크입니다(SA1 bit 3 → `0x1 ^ 0x9 = 0x8`).
- address test 3단계(서로 붙은 두 주소선 찾기)는 이 fake의 고장 모델로는 테스트하지 않지만 원래 알고리즘의 일부라 넣었습니다. 면접에서는 "2단계 = stuck, 3단계 = short"라고 말하면 됩니다.
- 출력은 `MemFail` 하나뿐이라 malloc이 없습니다. 실제 엔진이라면 실패 로그 링버퍼를 호출자가 넘겨줍니다.

### 4.5 복잡도

- walking-1: 64 접근(32 write + 32 read).
- address bus: O(log² N) 최악(3단계), 정상 256워드에서 99 접근. 고장이 있으면 2단계에서 바로 끝남.
- March C-: 정확히 **10N** (5N read + 5N write), 메모리 O(1). 256워드면 2560 접근.

---

## 5. 임베디드 관점으로 말할 것

- **테스트 순서 = 디버그 순서**: data bus(배선/PCB/패키지) → address bus(디코더/배선) → cell(March). 실패 리포트가 "어느 비트, 어느 주소선"까지 말해 주면 보드/패키지 팀이 바로 핀을 찾아갈 수 있습니다. `expected ^ actual`을 로그에 같이 찍는 이유입니다.
- **volatile 경로**: 실제 코드에서 `mem_wr/mem_rd`는 `*(volatile uint32_t *)addr` 입니다. volatile이 없으면 컴파일러가 "방금 쓴 값을 그대로 돌려주는" 최적화를 해서 테스트가 메모리를 전혀 안 읽습니다.
- **캐시**: 캐시가 켜진 상태에서 March를 돌리면 대부분 캐시 라인을 테스트하게 됩니다. bring-up 메모리 테스트는 캐시 끈 상태(또는 해당 영역 Device/Non-cacheable 매핑)로 돌리고, 캐시 켠 상태 테스트는 별도로 "throughput + 데이터 무결성" 목적(burst/line fill 경로)으로 돌립니다.
- **어디서 실행하나**: SRAM 테스트는 ROM/XIP 또는 테스트 대상이 아닌 다른 SRAM 뱅크에서, DDR 테스트는 SRAM(또는 잠긴 L2/TCM)에서 실행합니다. 코드·스택이 테스트 영역 안에 있으면 테스트가 자기 자신을 지웁니다.
- **ECC**: ECC가 켜져 있으면 single-bit 고장은 **자동으로 고쳐져서** March가 통과합니다. 그래서 (1) ECC를 끄고 raw 셀 테스트, 또는 (2) ECC를 켜고 corrected-error 카운터/인터럽트를 같이 확인합니다. ECC 켠 DDR은 쓰기 전 초기화(scrub)를 안 하면 첫 읽기에서 uncorrectable error가 납니다.
- **스케일**: GB급 DDR은 CPU 루프로 10N을 돌리면 수 분 걸립니다. DMA 엔진으로 패턴 fill/compare, 코어별로 주소 범위를 나눠 병렬 실행, 긴 March는 SLT/burn-in 단계로 보냅니다 → [V3](../V3_dma_desc_ring/index.html).

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"테스트 자체가 맞는지 어떻게 아나?"** — fake memory에 고장을 하나씩 주입해서 각 고장이 기대한 원소/주소에서 잡히는지 확인합니다(이 문제의 테스트가 그것). 실리콘에서는 ECC 끈 상태로 알려진 비트를 강제로 뒤집는 error-injection 레지스터를 쓰거나, FPGA 프로토에서 고장을 넣어 봅니다.
  *"I validate the test itself with fault injection: every fault model must be caught at the element and address I predict, otherwise the test has a coverage hole."*
- **"FPGA(HAPS)와 첫 실리콘에서 무엇이 다른가?"** — FPGA에서는 기능(주소 디코딩, 컨트롤러 설정)을 확인하고, 실리콘에서는 같은 바이너리를 타이밍 마진(전압·온도·주파수)을 바꿔 가며 돌립니다. 실패가 특정 corner에서만 나오면 셀이 아니라 타이밍/SI 문제입니다 → [V7](../V7_shmoo_margin/index.html).
  *"On FPGA it proves the logic; on silicon the same binary becomes a margin test when I sweep voltage, temperature and DDR frequency."*
- **"커버리지가 뭐고 런타임은 얼마인가?"** — March C-는 SAF, TF, 주소 디코더 고장, 대부분의 2-cell coupling fault를 10N으로 커버합니다. 데이터 패턴 의존 고장(인접 비트 간섭)은 못 잡으므로 checkerboard / PRBS 패턴을 추가합니다 → [V6](../V6_prbs_crc_checker/index.html).
  *"March C- gives stuck-at, transition, address-decoder and most coupling faults in 10N; I add checkerboard and PRBS data for pattern-sensitive faults."*
- **"결과가 PVT/SLT로 어떻게 이어지나?"** — 실패를 `{addr, expected, actual, element}`로 구조화해서 남기면 칩/다이/corner별로 모아 bit-map(어느 row/column에 몰리는지)을 그릴 수 있습니다. 같은 엔진을 SLT에서는 짧은 버전으로, burn-in에서는 긴 버전으로 돌립니다.
  *"Structured failure records let us build a fail bitmap per die and corner, and the same engine runs short in SLT and long in burn-in."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why does walking-1s find bus faults but not cell faults? | 한 주소의 32비트만 봅니다. data line이 붙거나 끊긴 것은 모든 주소에 똑같이 나타나므로 한 워드로 충분하지만, 셀 고장은 특정 주소에만 있으므로 전체 주소를 훑는 March가 필요합니다. |
| What does March C- cover, and why 10N? | 원소 6개: w0(N) + 4개 × (r,w)(2N) + r0(N) = 10N. SAF, TF, address decoder fault, unlinked CFin/CFid/CFst를 커버합니다. 방향 ⇑/⇓ 둘 다가 있어야 aggressor가 위/아래 어느 쪽이든 잡힙니다. |
| What faults does March C- miss? | 데이터 패턴 의존 고장(NPSF), 읽기 후에 서서히 사라지는 retention fault(지연이 없음), linked coupling 일부. retention은 쓰고 → 대기(예: refresh 끄고 수백 ms) → 읽기로 따로 봅니다. |
| How do you test the DRAM you are executing from? | 그 상태로는 못 합니다. 코드·스택을 SRAM/TCM(또는 캐시 lock)으로 옮기고 DDR 전체를 테스트하거나, 영역을 둘로 나눠 A에서 실행하며 B를 테스트하고 코드를 옮겨 A를 테스트합니다. |
| How does ECC interact with this test? | single-bit 고장이 숨겨집니다. ECC를 끄고 raw 테스트를 하거나, 켠 채 corrected/uncorrected 카운터를 읽습니다. 또 ECC 메모리는 사용 전에 전체를 한 번 써서 syndrome을 초기화해야 합니다. |
| Caches enabled or disabled? | bring-up 셀 테스트는 non-cacheable로 돌려야 실제 DRAM 접근이 됩니다. 캐시 켠 테스트는 line fill/evict 경로(burst)를 보는 별도 목적이고, 캐시 크기보다 큰 범위를 써야 실제로 DRAM까지 갑니다. |
| How do you scale this to many GB of DDR? | DMA로 fill/compare(CPU는 결과만 확인), 코어별 주소 분할 병렬 실행, 64-bit/캐시 라인 단위 접근. 대역폭이 10 GB/s면 8 GB × 10N ≈ 8 s 정도라 전체 March는 SLT용, bring-up은 샘플 범위 + bus test로 줄입니다. |
| What other data patterns would you add? | checkerboard(`0xAAAA…`/`0x5555…`를 주소 패리티로 교대) → 인접 셀 간섭, walking-0, PRBS 데이터(주소를 seed로) → 고유한 값이라 aliasing도 잡고 SI/ISI 문제까지 자극합니다. |
| Byte lanes? | DDR은 byte lane(DQS 그룹)별로 타이밍이 따로 학습됩니다. 실패 비트를 lane별로 묶어 보고하면 어떤 lane의 training이 잘못됐는지 바로 보입니다. byte write(`uint8_t*`)로 data mask 경로도 테스트합니다. |

## 7. 자주 하는 실수

- 내려가는 루프를 `for (uint32_t a = end - 1; a >= base; a--)`로 씀 → `base == 0`에서 무한 루프 / 범위 밖 접근 (테스트 1, 7번의 `F.oob`)
- M5(마지막 r0)를 빠뜨려 9N이 됨 → 접근 수 테스트 실패, M4의 w0 이후 생긴 고장을 놓침
- 읽기 전에 쓰기(w, r 순서) → 방금 쓴 값을 읽으니 coupling fault를 못 잡음 (테스트 5번)
- 실패를 찾고도 계속 진행해서 `f`가 마지막 실패로 덮어써짐 → "first failure" 테스트 실패
- address test에서 `base + off`가 범위 끝 `nwords`를 넘어감 (`off < nwords` 조건)
- `range_ok`를 `base + nwords <= MEM_WORDS`로 써서 오버플로, 또는 `nwords == 0`을 통과시킴 (테스트 8번)
- 모든 테스트에서 실패 시 `expected`/`actual`을 바꿔 적음 → 보고서를 읽는 HW 엔지니어가 반대로 해석
