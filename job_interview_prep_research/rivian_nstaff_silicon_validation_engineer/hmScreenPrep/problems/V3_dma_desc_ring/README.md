# V3 · DMA Descriptor Ring Driver (validation harness)

> **ID**: V3 · **Priority**: P0 · **Pattern**: descriptor ring + OWN-bit handoff + guard-byte self-test · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh V3` / `./run.sh V3 sol`

## 1. Problem

You are bringing up a **memcpy / AXI-DMA engine** on a new SoC. The engine walks a ring of 32-byte descriptors in DDR. Software fills a descriptor and gives it to hardware by setting the **OWN** bit; hardware copies `len` bytes from `src` to `dst`, writes `status`, clears OWN and moves to the next slot. Write the driver and a self-test that a validation run can trust.

The engine is a **fake** at the bottom of `stub.c`. On silicon it runs by itself; in the harness the test calls `dma_hw_step(budget)` to let it process up to `budget` descriptors. The fake can inject an AXI error, a 1-byte overrun, or a short copy on a chosen descriptor.

```c
typedef struct {
    uint64_t src, dst;     /* bus addresses */
    uint32_t len;          /* 1 .. DMA_MAX_LEN (256) */
    uint32_t ctrl;         /* b31 OWN (1 = HW owns) | b30 IRQ | b29 EOP (last of a transfer) */
    uint32_t status;       /* b0 DONE | b3:1 ERR (0 OK, 1 SLVERR, 2 DECERR) - written by HW */
    uint32_t rsvd;
} DmaDesc;

void dma_hw_set_ring(volatile DmaDesc *base, uint32_t n);   /* HAL: RING_BASE/RING_SIZE, HW ptr = 0 */
int  dma_hw_step(int budget);                               /* harness: let the engine run */

typedef void (*dma_cb)(void *ctx, int32_t cookie, int err, uint32_t bytes);
typedef struct {
    volatile DmaDesc *desc; uint32_t n, mask;
    uint32_t head, tail;               /* free-running; slot = idx & mask */
    int32_t  next_cookie, cookie[RING_MAX];
    uint32_t acc_bytes; int acc_err;   /* transfer currently being reaped */
} DmaRing;

int     ring_init(DmaRing *r, volatile DmaDesc *mem, uint32_t n);
int32_t dma_submit(DmaRing *r, const void *src, void *dst, uint32_t len);   /* cookie or -errno */
int     dma_reap(DmaRing *r, dma_cb cb, void *ctx);                         /* # callbacks */
int     dma_selftest(DmaRing *r, SelftestReport *rep);                      /* 0 or -EIO */
```

Requirements:

- **`ring_init`**: `n` must be a power of two in `[2, 64]`, else `-EINVAL`. Clear every descriptor (OWN = 0), reset indices, then program the engine with `dma_hw_set_ring()`.
- **`dma_submit`**: split `len` into chunks of at most 256 bytes, one descriptor each; **EOP | IRQ only on the last**. Fill `src/dst/len/status` first and **set OWN last**, with `dma_wmb()` before it. If the ring does not have enough free slots for the whole transfer → `-EBUSY` and **nothing is written**. `len == 0`, or a transfer that needs more descriptors than the ring has → `-EINVAL`. Returns a cookie `0, 1, 2, …`.
- **`dma_reap`**: walk from `tail` while the slot was submitted (`tail != head`) **and** OWN == 0. `dma_rmb()` before reading `status`. At each EOP call `cb(ctx, cookie, err, bytes)` once per transfer: `err = -EIO` if any of its descriptors had `ERR != 0` or no DONE, `bytes` = sum of `len` of its good descriptors.
- **`dma_selftest`**: sizes `{1, 3, 4, 63, 64, 255, 256, 257, 700, 1024}` × destination offsets `0..3` (40 cases), one transfer at a time. Check the destination **and 16 guard bytes before and after it**. Count each case once into `pass`, `guard_err`, `data_err`, `dma_err` or `timeout`. Return `0` only if all pass.

### Example

```
ring n = 8, all free
dma_submit(a, b, 700) -> cookie 0, 3 descriptors (head = 3):
  slot 0: len 256  ctrl = OWN
  slot 1: len 256  ctrl = OWN
  slot 2: len 188  ctrl = OWN | IRQ | EOP
dma_hw_step(2); dma_reap() -> 0 callbacks (slots 0,1 freed, chain not finished)
dma_hw_step(8); dma_reap() -> 1 callback: cookie 0, err 0, bytes 700

8 x dma_submit(16 B) -> cookies 0..7, 9th -> -EBUSY
SLVERR on the middle descriptor of a 700-byte transfer -> cb(err = -EIO, bytes = 444)
self-test, clean engine -> 40/40 pass; HW copies len+1 on the first descriptor -> guard_err = 1
```

### Constraints

- C11, single core, no malloc. Descriptor memory is caller-provided. `head`/`tail` are `uint32_t` that may wrap past 2^32.
- The fake HW processes descriptors strictly in ring order and does not halt on an error.
- `dma_wmb()` / `dma_rmb()` are provided (release / acquire fences).

### Clarifying questions to ask (first 3 minutes)

1. Who owns the descriptor memory, and is it cache-coherent with the DMA master? (decides clean/invalidate)
2. Does the engine stop on an error, or report it and continue?
3. Is there a doorbell / tail register, or does HW poll OWN in DDR?
4. Should a transfer that does not fit be partially queued, or rejected as a whole?
5. Is completion per descriptor or per transfer (EOP)? Interrupt or polling?
6. Are addresses physical, IOVA (behind an IOMMU), or 32-bit only?

## 2. Your workspace

`./run.sh V3`로 바로 테스트하세요. 테스트 7묶음이 있고, 파일 아래쪽 fake DMA 엔진은 건드리지 않습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 인덱스는 free-running, 슬롯은 mask</summary>

`head`, `tail`을 계속 증가만 시키고 슬롯은 `idx & mask`로 구합니다. 사용 중인 개수는 `head - tail`(unsigned 뺄셈이라 2^32를 넘어도 맞음), 빈 칸은 `n - (head - tail)`입니다. full/empty 구분용 "한 칸 비워 두기"가 필요 없습니다.

</details>

<details>
<summary>Hint 2 — 체인은 첫 descriptor의 OWN을 맨 마지막에</summary>

뒤쪽 descriptor들은 채우고 OWN까지 켜도 됩니다. HW 포인터가 첫 슬롯에 멈춰 있기 때문입니다. 첫 슬롯의 OWN을 마지막에 켜면 HW는 **완성된 체인만** 보게 됩니다. 각 OWN 쓰기 앞에는 `dma_wmb()`.

</details>

<details>
<summary>Hint 3 — self-test가 거짓 PASS를 내지 않게</summary>

- 목적지 앞뒤에 guard(0xA5)를 깔고, 목적지 영역은 기대값의 **보수(~pattern)**로 채워 둡니다 → 안 쓴 바이트가 우연히 맞을 수 없음.
- src의 len 뒤쪽 바이트는 guard와 **다른 값**(0x5A)으로 → len+1 오버런이 guard를 반드시 바꿈.
- 패턴에 `(i >> 8)`을 섞어 256바이트 청크마다 다르게 → "청크마다 src를 안 늘림" 버그도 잡힘.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 링과 포인터 3개

링에는 포인터가 세 개 있습니다. SW가 채우는 `head`, SW가 회수하는 `tail`, HW가 처리 중인 HW 포인터. 항상 `tail ≤ HW ≤ head` 순서(모듈로 n)이고, 구간마다 소유자가 다릅니다.

<div class="chart" data-src="chart1.svg" data-caption="free-running 인덱스 예: tail 13 ~ HW 15는 완료됐지만 아직 reap 안 된 칸(OWN=0), HW 15 ~ head 18은 HW 소유(OWN=1), 나머지 3칸이 빈 칸. slot 7 → 0으로 감김"></div>

`dma_reap`의 종료 조건이 두 개인 이유가 그림에 있습니다. 빈 칸(slot 2–4)도 OWN = 0입니다. `tail != head` 검사 없이 OWN만 보면 init 직후 빈 링에서 "완료"를 8개 읽어 버립니다(테스트 1번 마지막 reap).

### 4.2 소유권 넘기기 타임라인

<div class="chart" data-src="chart2.svg" data-caption="OWN 비트가 소유권 그 자체입니다. ①이 메모리에 보이기 전에 ③이 보이면 HW가 반쯤 쓴 descriptor를 실행합니다 → dma_wmb(). ⑧에서 OWN=0을 본 뒤 dma_rmb() 없이 status를 먼저 읽으면 옛 값을 볼 수 있습니다"></div>

- **SW 쪽 순서**: 필드 → `dma_wmb()` → OWN. CPU와 인터커넥트는 서로 다른 주소의 쓰기 순서를 바꿀 수 있습니다(Arm weakly-ordered, write buffer).
- **HW 쪽 순서**: status를 쓴 다음 OWN을 지웁니다. 이건 RTL 쪽 약속이고, SW는 OWN=0을 본 다음에만 status를 믿습니다.
- **분할(split)**: 256바이트 넘는 전송은 여러 descriptor로 나누고 EOP는 마지막에만. 콜백은 전송당 1번입니다.

### 4.3 손으로 따라가기 (trace)

`dma_submit(a, b, 700)` on an empty 8-slot ring (테스트 4번):

| 단계 | i / slot | chunk | left 뒤 | 쓰는 ctrl | 비고 |
|---|---|---|---|---|---|
| 검사 | – | – | – | – | nd = 700/256 + (700%256≠0) = 2 + 1 = **3**, free = 8 − 0 = 8 ≥ 3 |
| fill | 0 / 0 | 256 | 444 | (보류) `first_ctrl = 0` | 첫 칸은 아직 OWN 안 켬 |
| fill | 1 / 1 | 256 | 188 | `dma_wmb()` → OWN | HW는 slot 0에서 멈춰 있어 안전 |
| fill | 2 / 2 | 188 | 0 | `dma_wmb()` → OWN \| IRQ \| EOP | 마지막 칸 |
| 넘김 | 0 | – | – | `dma_wmb()` → OWN | 이제 체인 전체가 HW 것, head = 3 |

이어서 HW와 reap:

| 호출 | HW ptr | reap 동작 | tail | acc_bytes | 콜백 |
|---|---|---|---|---|---|
| `dma_hw_step(2)` → 2 | 2 | – | 0 | 0 | – |
| `dma_reap` | 2 | slot 0 OWN=0, DONE → +256 / slot 1 → +256 / slot 2 OWN=1 → stop | 2 | 512 | **0개** |
| `dma_hw_step(8)` → 1 | 3 | – | 2 | 512 | – |
| `dma_reap` | 3 | slot 2 DONE → +188, EOP | 3 | 0 (리셋) | `cb(0, 0, 700)` |

오류 주입 결과 (테스트 5–7번에서 확인하는 숫자):

| 주입 | 결과 |
|---|---|
| 64 B × 3, SLVERR on 2nd descriptor | 콜백 3개: `(0, 0, 64)`, `(1, -EIO, 0)`, `(2, 0, 64)` |
| 700 B, SLVERR on 2nd (가운데) descriptor | `(0, -EIO, 444)` = 256 + 188, 다음 전송 `(1, 0, 10)`은 정상 |
| self-test, 깨끗한 엔진 | `cases 40, pass 40`, return 0 |
| 첫 descriptor가 len+1 복사 | `guard_err 1, pass 39`, return `-EIO` |
| 모든 EOP descriptor가 len+1 복사 | `guard_err 40, pass 0` |
| 3번째 descriptor가 len−1 복사 | `data_err 1, pass 39` |
| 5번째 descriptor SLVERR | `dma_err 1, pass 39, timeout 0` |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `nd = len / 256 + (len % 256 != 0)`: `(len + 255) / 256`은 len이 4 GiB 근처면 넘칩니다. 습관적으로 안전한 형태를 씁니다.
- **all-or-nothing**: 빈 칸 검사를 descriptor를 쓰기 전에 끝냅니다. 반쯤 들어간 체인은 HW가 EOP를 못 만나 영원히 끝나지 않습니다.
- `cookie[]`는 SW 전용 그림자 배열입니다. descriptor에 SW 정보를 넣으면 HW가 덮어쓸 수 있고 캐시 라인도 늘어납니다(Linux NIC 드라이버의 `tx_buffer_info`와 같은 구조).
- `acc_bytes/acc_err`를 링 구조체에 둔 이유: 체인 중간까지만 끝났을 때 reap이 앞 칸을 먼저 비워 줄 수 있고, 다음 reap 호출이 이어서 합산합니다.
- self-test의 poll 루프는 `spin < 100`으로 **상한**이 있습니다. 실리콘에서 엔진이 멈추면 테스트가 멈추면 안 되고 `timeout`으로 기록돼야 합니다.

### 4.5 복잡도

- `dma_submit`: O(nd), `dma_reap`: 회수한 descriptor 수에 비례. 둘 다 추가 메모리 O(1).
- self-test: 총 바이트 Σsize × 4 = 2,627 × 4 = 10,508 B ≈ 10.5 KB 복사 + 같은 양 비교. 버퍼는 static 약 1 KB × 2.

---

## 5. 임베디드 관점으로 말할 것

- **캐시 일관성**: 엔진이 non-coherent면 submit 전에 src와 descriptor를 **clean**(write-back), 완료 후 dst를 읽기 전에 **invalidate**. dst는 submit 전에도 invalidate(또는 clean+invalidate)해 둡니다. 안 그러면 dirty 라인이 나중에 evict되며 DMA 결과를 덮어씁니다. descriptor 링은 보통 uncached/coherent 메모리에 둡니다.
- **OWN 마지막 + 배리어**: 컴파일러 배리어만으론 부족하고 CPU 쓰기 순서까지 보장해야 합니다. Arm64 Linux에서 `dma_wmb()`는 `dmb oshst`. MMIO 도어벨 앞에는 `wmb()`(`dsb st`)가 필요합니다. 메모리 쓰기와 장치 쓰기는 경로가 다르기 때문입니다.
- **도어벨**: 실제 엔진은 DDR의 OWN을 계속 폴링하지 않고, SW가 `TAIL`/`DOORBELL` 레지스터에 새 head를 쓰면 그때 descriptor를 fetch합니다. 여러 개를 넣고 도어벨 한 번 = MMIO 쓰기 절약.
- **검증 코드로서의 의미**: 이 드라이버는 제품 드라이버가 아니라 **블록 검증용 harness**입니다. 그래서 guard byte, poison, 패턴, 타임아웃, 결과 분류(guard/data/dma/timeout)가 핵심이고, 결과를 카운터로 남겨 회귀 대시보드에 올립니다. 대역폭 측정은 같은 링 위에 타이머만 붙이면 됩니다 → [V4](../V4_perf_bench_stats/index.html).
- **메모리 테스트와의 차이**: [V1](../V1_march_memtest/index.html)의 March 테스트는 CPU가 메모리를 검사하고, V3는 DMA 경로(AXI 인터커넥트, 정렬, burst 분할)를 검사합니다. 두 개를 같이 돌려야 "메모리 vs 인터커넥트" 문제를 나눌 수 있습니다.

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"How do you know your self-test itself is correct?"** → fake 엔진에 오버런·short copy·SLVERR를 주입해서 테스트가 **정확히 그만큼** 잡는지 봅니다(mutation testing). 패턴/guard/poison 값 선택이 거짓 PASS를 막는 이유도 설명합니다. *"I validate the validator: every checker has a fault-injection case that must turn it red, and I check the exact count, not just 'it failed'."*
- **"First silicon vs FPGA/HAPS — what changes?"** → FPGA에서는 클럭이 느려 타이밍 버그가 숨고, 캐시/인터커넥트가 실제와 다를 수 있습니다. 실리콘에서는 같은 바이너리를 쓰되 타임아웃을 실측 기준으로 다시 잡고, 코히어런시 경로(ACE/CHI vs AXI)를 확인합니다. *"Same test binary on HAPS and silicon; only the platform layer — timeouts, cache ops, base addresses — changes."*
- **"What corner cases would you add for coverage?"** → 4 KB 경계 걸치기(AXI burst는 4 KB를 못 넘음), 최대 길이, 1바이트, 모든 정렬 조합, 링 full/wrap, 동시에 CPU 트래픽, DDR 랭크/뱅크 경계. *"I'd sweep length, alignment, 4 KB boundary crossing, and ring occupancy, and report coverage as a matrix of those bins."*
- **"How does this feed PVT and SLT?"** → 같은 테스트를 전압/온도/주파수 코너에서 돌려 실패 카운트를 shmoo로 그립니다 → [V7](../V7_shmoo_margin/index.html). SLT에서는 짧은 버전(수 ms)만 남겨 양산 시간을 줄입니다. *"The long version runs in PVT sweeps; a trimmed version with the highest-yield cases goes into the SLT flow."*
- **"Interrupt or polling for completion?"** → bring-up과 처리량 측정은 폴링(결정적, 지연 측정 정확), 장기 스트레스와 IRQ 경로 검증은 인터럽트 + coalescing을 따로 테스트합니다. *"Polling first for determinism, then a separate test that validates the IRQ and coalescing logic itself."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| The engine is not cache-coherent. What do you add? | submit 전: src clean, dst clean+invalidate, descriptor는 uncached 메모리(또는 clean). 완료 후: dst invalidate 후 읽기. Linux에서는 `dma_map_single(DMA_TO_DEVICE / FROM_DEVICE)`와 `dma_unmap`/`dma_sync_*`가 이 일을 합니다. |
| Why must OWN be written last, and why a barrier? | OWN이 소유권 플래그라서 HW는 OWN=1을 보면 바로 나머지 필드를 읽습니다. 쓰기 순서가 바뀌면 옛 len/addr로 복사합니다. `dma_wmb()`(Arm64 `dmb oshst`)가 "앞의 쓰기가 다 보인 다음 OWN"을 보장합니다. 읽는 쪽도 OWN=0 확인 후 `dma_rmb()`. |
| Where does a doorbell register fit? | OWN 설정 후 `wmb()`(MMIO 앞이라 `dsb`) → 도어벨에 새 head 쓰기. HW는 도어벨을 받고 head까지 fetch합니다. 여러 전송을 모아 도어벨 1번이면 MMIO 왕복이 줄어듭니다. |
| Interrupt per descriptor or coalescing? | 작은 전송이 많으면 IRQ 폭주로 CPU가 묶입니다. 그래서 EOP에만 IRQ, 또는 "N개 완료 또는 T µs 경과" coalescing을 씁니다. Linux NAPI처럼 IRQ 한 번 받고 폴링으로 몰아서 reap하는 방식도 많습니다. 지연 vs CPU 부하 트레이드오프입니다. |
| What if there is an IOMMU / SMMU? | descriptor의 주소는 물리 주소가 아니라 IOVA입니다. 매핑을 먼저 만들고, 매핑 범위를 넘는 접근은 SMMU fault로 잡힙니다. 검증에서는 오히려 좋은 도구: 일부러 매핑 안 된 주소를 넣어 DECERR/fault 경로를 테스트합니다. |
| How would you measure DMA bandwidth? | 큰 전송(예: 1 MB)을 링에 꽉 채워 연속으로 돌리고, 첫 도어벨 ~ 마지막 완료를 사이클 카운터로 잽니다. 크기별로 쓸면 descriptor 오버헤드(작은 전송)와 DDR 한계(큰 전송)가 보입니다. 여러 번 돌려 min/median/p99 → [V4](../V4_perf_bench_stats/index.html). |
| Scatter-gather? | 이 링이 이미 SG입니다. 한 전송 = 여러 descriptor, 각자 다른 src/dst 조각, 마지막에 EOP. 리스트형(next 포인터) SG는 링 대신 체인을 따라가며, 같은 OWN/EOP 규칙을 씁니다. |
| An overrun only on a middle chunk — would your test catch it? | 못 잡습니다. 가운데 청크가 1바이트 더 쓰면 다음 청크가 그 자리를 정상 값으로 덮습니다. 그래서 청크 단위 검사(청크 사이 순서를 바꾸거나, 청크마다 별도 버퍼로 보내는 케이스)를 추가해야 합니다. 좋은 "테스트의 한계" 답입니다. |

## 7. 자주 하는 실수

- `tail != head` 없이 OWN==0만 보고 reap → 빈 링에서 가짜 완료 (테스트 1번)
- full 검사를 `head >= tail + n` 같은 비교로 함 → 인덱스가 2^32를 넘으면 틀림 (테스트 3번 phase 1)
- 분할 전송이 안 들어갈 때 일부만 넣고 `-EBUSY` → HW가 EOP 없는 체인을 잡고 멈춤 (테스트 4번 `head` 불변 검사)
- EOP/IRQ를 모든 descriptor에 켬 → 콜백이 전송당 여러 번, `bytes`가 틀림
- 청크마다 `src`/`dst`를 안 늘림 → 같은 256 바이트를 반복 복사 (테스트 4번 주소 검사, self-test 패턴의 `i >> 8`)
- self-test에서 guard 값과 src 패턴이 겹칠 수 있게 둠 → 오버런을 놓치는 **거짓 PASS**
- poll 루프에 상한이 없음 → HW가 멈추면 테스트도 영원히 멈춤
