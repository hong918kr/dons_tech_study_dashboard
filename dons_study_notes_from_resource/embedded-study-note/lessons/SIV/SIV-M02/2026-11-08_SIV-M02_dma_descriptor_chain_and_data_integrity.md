---
id: SIV-M02
time: 95분
updated: 2026-11-08
---
# SIV-M02 DMA 검증: 디스크립터 체인과 데이터 무결성

## 학습 목표

- scatter-gather(SG) DMA 의 디스크립터 체인(NEXT · SRC · DST · LEN/LAST · write-back STAT/XFER)을 그리고, 엔진이 "fetch → 검사 → 이동 → write-back → 다음" 을 도는 동안 **어디를 관측해야 하는지** 말할 수 있다.
- 디스크립터마다 **CRC-32 골든 시그니처**(생성 시점의 src CRC)로 dst 를 비교하고, CRC 가 못 보는 "써서는 안 되는 곳" 을 **dst arena 전체 stray-write 스캔**으로 잡는다. 실패하면 첫 불일치 바이트까지 좁힌다.
- 정렬/비정렬, **4 KiB 경계 교차**, 길이 **0 / 1 / max**, **체인 중간 에러 디스크립터** 를 경계 + 랜덤(고정 시드 xorshift32)으로 생성하고, 랜덤만으로는 왜 부족한지 숫자로 설명한다.
- 완료 인터럽트 · STATUS / DESC_CNT / BYTE_CNT / CUR_DESC 레지스터 · 디스크립터 write-back 이 **서로 일관된지**, 그리고 그 확인을 **어떤 순서로** 해야 하는지 설명한다.
- 8가지 결함(4K wrap, tail drop, LEN 비트 누락, len0 hang, 에러 후 계속, 에러를 DONE 으로 보고, write-back 전 IRQ, BYTE_CNT 누락)을 주입한 야간 회귀에서 각 결함을 **정확한 시그니처**로 잡고 리포트를 만든다.

## 1. 문제

DMA 는 SoC 에서 데이터가 가장 많이 지나가는 길이고, first silicon 의 "가끔 프레임이 깨져요" 는 대부분 DMA 경계 조건이다 — Rivian·NVIDIA·Apple 검증 면접에서 "DMA 엔진을 어떻게 검증하겠냐" 는 거의 항상 **경계 조건 목록 + 무결성 체크 방법 + 완료 신호의 일관성** 세 가지를 듣고 싶어 한다.

SG DMA 는 연속되지 않은 버퍼 여러 조각을 한 번에 옮긴다. 소프트웨어가 메모리에 **디스크립터** 를 연결 리스트로 써 두고 엔진에 첫 주소(head)만 주면, 엔진이 디스크립터를 하나씩 읽어(fetch) 데이터를 옮기고, 결과(완료/에러, 전송 바이트 수)를 디스크립터에 다시 쓰고(**write-back**), 끝나면 인터럽트를 올린다. Don 이 SSD 에서 본 NVMe 의 PRP/SGL, 카메라 파이프라인의 프레임 DMA, NIC 의 RX 링이 모두 이 구조다. 실제 IP 예:

| IP | 디스크립터 필드 | write-back | 비고 |
|---|---|---|---|
| Xilinx/AMD AXI DMA (PG021) SG 모드 | NXTDESC · BUFFER_ADDRESS · CONTROL(길이, SOF/EOF) | STATUS: Cmplt, DMADecErr/SlvErr/IntErr, 전송 바이트 | 디스크립터 16워드(64 B) 정렬 |
| NVMe SGL / PRP | SGL: 주소 · 길이 · 타입 / PRP: 페이지 단위 주소 목록 | 완료는 Completion Queue 엔트리 + MSI-X | 첫 PRP 외에는 페이지 정렬 |
| 이 레슨의 SGDMA0 | NEXT · SRC · DST · CTRL(LEN[15:0], LAST[16]) | STAT(DONE/ERR/ERR_CODE) · XFER | 32 B 디스크립터, 32 B 정렬 |

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m2a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">SGDMA0: DRAM 안의 디스크립터 체인과 관측 지점 ①~⑤</text>
  <rect x="10" y="40" width="120" height="40" class="f-mut"/>
  <text x="70" y="58" text-anchor="middle" fill="currentColor">DESC_HEAD</text>
  <text x="70" y="73" text-anchor="middle" fill="currentColor" font-size="12">CTRL.START</text>
  <line x1="130" y1="60" x2="168" y2="60" stroke="currentColor" marker-end="url(#m2a)"/>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <rect x="170" y="36" width="150" height="104" class="f-acc"/>
    <text x="178" y="52">desc0 @+0x000</text><text x="178" y="68">NEXT → +0x300</text><text x="178" y="84">SRC  DST</text>
    <text x="178" y="100">LEN=254</text><text x="178" y="116">STAT  XFER ③</text><text x="178" y="132">(write-back)</text>
    <rect x="370" y="36" width="150" height="104" class="f-acc"/>
    <text x="378" y="52">desc1 @+0x300</text><text x="378" y="68">NEXT → +0x600</text><text x="378" y="84">SRC  DST</text>
    <text x="378" y="100">LEN=553</text><text x="378" y="116">STAT  XFER ③</text><text x="378" y="132">(write-back)</text>
    <rect x="570" y="36" width="160" height="104" class="f-acc"/>
    <text x="578" y="52">desc2 @+0x600</text><text x="578" y="68">NEXT (무시)</text><text x="578" y="84">SRC  DST</text>
    <text x="578" y="100">LEN=563 LAST=1</text><text x="578" y="116">STAT  XFER ③</text><text x="578" y="132">(write-back)</text>
  </g>
  <line x1="320" y1="64" x2="368" y2="64" stroke="currentColor" marker-end="url(#m2a)"/>
  <line x1="520" y1="64" x2="568" y2="64" stroke="currentColor" marker-end="url(#m2a)"/>
  <rect x="170" y="180" width="560" height="34" class="f-mut"/>
  <text x="180" y="202" fill="currentColor">src arena (124 KiB): 조각 0·1·2 — 생성 시점 CRC = 골든 ①</text>
  <rect x="170" y="240" width="560" height="34" class="f-ok"/>
  <text x="180" y="262" fill="currentColor">dst arena (128 KiB, FILL=0xEE): 조각 → CRC 비교 ① + 나머지 전부 0xEE 인가 ②</text>
  <line x1="245" y1="214" x2="245" y2="238" stroke="currentColor" marker-end="url(#m2a)"/>
  <line x1="445" y1="214" x2="445" y2="238" stroke="currentColor" marker-end="url(#m2a)"/>
  <line x1="650" y1="214" x2="650" y2="238" stroke="currentColor" marker-end="url(#m2a)"/>
  <rect x="10" y="150" width="140" height="64" fill="none" stroke="currentColor"/>
  <text x="80" y="170" text-anchor="middle" fill="currentColor">레지스터 ④</text>
  <text x="80" y="187" text-anchor="middle" fill="currentColor" font-size="12">STATUS · DESC_CNT</text>
  <text x="80" y="203" text-anchor="middle" fill="currentColor" font-size="12">BYTE_CNT · CUR_DESC</text>
  <rect x="10" y="240" width="140" height="44" class="f-warn"/>
  <text x="80" y="259" text-anchor="middle" fill="currentColor">IRQ_STAT (W1C)</text>
  <text x="80" y="276" text-anchor="middle" fill="currentColor" font-size="12">→ INTC 입력 20 ⑤</text>
  <text x="10" y="308" fill="currentColor">디스크립터는 비연속 배치(+0x000→+0x300→+0x600): NEXT 를 무시하는 엔진도 드러난다</text>
</svg>
```

호스트(맥)에는 실리콘이 없으므로 256 KiB 배열을 DRAM 으로, 레지스터 블록을 `reg_read32` / `reg_write32` 뒤의 mock 으로 둔다 (SIV-B02 의 mock MMIO, SIV-B11 의 W1C IRQ 블록을 이 IP 에 필요한 만큼 축약해 넣었다). **레지스터 접근 1회 = 엔진 1 tick** 이라 poll 횟수가 곧 시간이고, 타임아웃이 결정적이다 (SIV-B09 의 가짜 시계와 같은 발상). 테스트가 DRAM 을 직접 읽는 `mem_rd32` / `mp` 는 tick 을 일으키지 않는다.

| off | 이름 | access | 의미 |
|---|---|---|---|
| `0x000` | `CTRL` | RW | `[0]` START, `[1]` SRST (둘 다 self-clear), `[8]` IE_DONE, `[9]` IE_ERR |
| `0x004` | `STATUS` | RO | `[0]` BUSY, `[7:4]` ERR_CODE (1 = SRC 디코드 에러, 2 = DST, 3 = 디스크립터) |
| `0x008` | `IRQ_STAT` | W1C | `[0]` DONE (체인 완료), `[1]` ERR |
| `0x00C` | `DESC_HEAD` | RW | 첫 디스크립터 주소 (BUSY 중에는 쓰기 무시) |
| `0x010` | `CUR_DESC` | RO | 마지막으로 fetch 한 디스크립터 — 에러면 **문제의 디스크립터** |
| `0x014` / `0x018` | `DESC_CNT` / `BYTE_CNT` | RO | 정상 완료한 디스크립터 수 / 바이트 합 |
| `0x800` | `INTC_RAW` | RO | 인터럽트 컨트롤러 입력 라인, SGDMA0 = bit 20 |

엔진 스펙(좋은 모델): 디스크립터를 fetch 하면 **데이터를 옮기기 전에** src/dst 범위를 검사한다. 범위가 디코드되지 않으면 그 디스크립터에 `STAT = ERR | code<<4, XFER = 0` 을 쓰고 체인을 **멈추며** `IRQ_STAT.ERR` 를 올린다. LEN=0 은 합법(0 바이트, DONE). 마지막 디스크립터의 write-back 이 끝난 **뒤에** `IRQ_STAT.DONE`. 주입 가능한 결함은 `dma_fault_t` 의 8개 플래그다.

### Part A — CRC-32 골든 시그니처, 실행, 데이터 검사

- `crc32_update(crc, p, n)` — CRC-32 (IEEE 802.3 / zlib) 를 zlib `crc32()` 처럼 **이어 부를 수 있게**: `crc(crc(0,a), b) == crc(a‖b)`.
- `dma_run(head, max_polls, &polls)` — 잔여 IRQ 를 W1C → `DESC_HEAD` → `CTRL = IE_DONE|IE_ERR|START` → `IRQ_STAT` 에 DONE/ERR 가 보일 때까지 최대 `max_polls` 번 poll. 마지막 IRQ_STAT 을 돌려준다 (timeout 이면 둘 다 0).
- `check_data(job, F)` — ① 모든 src 가 골든과 같은가(DMA 가 src 를 건드렸나), ② 완료돼야 할 디스크립터의 dst CRC == 골든, 다르면 첫 불일치 오프셋, ③ dst arena 에서 완료 구간 밖이 전부 `0xEE` 인가 (stray write: 첫 주소 · 값 · 개수).

### Part B — 경계 + 랜덤 job 생성기

- `gen_job(kind, seed, job)` — kind 8개: `ALIGNED`, `UNALIGNED`, `CROSS_4K`, `LEN0`, `LEN1`, `LEN_MAX`, `ERR_MID`, `RANDOM`. 같은 (kind, seed) 는 항상 같은 job (재현성). 범위 · 겹침 없음 · kind 별 경계 조건은 주어진 `job_props_ok()` 가 128개 job 에 대해 확인한다.
- 체인 중간 에러: 좋은 엔진은 **정확히 그 디스크립터에서** 멈춰야 하고, 그 뒤 디스크립터의 STAT/XFER 와 dst 는 손대지 않은 상태여야 한다 — 이것이 에러 job 의 "통과" 조건이다.

### Part C — 완료 인터럽트 · status · write-back 일관성과 야간 회귀

- `check_status(job, irq, F)` — **write-back 먼저**(DRAM 직접 읽기), 그다음 `irq` 정확히 · STATUS · DESC_CNT · BYTE_CNT · CUR_DESC · 라인, 마지막으로 W1C 후 IRQ_STAT · 라인이 0.
- `hint(job, fail)` — 실패 기록 하나를 디버그 엔지니어용 원인 후보로 (4K_CROSS, TAIL_BYTES, LEN_FIELD_BIT, WB_AFTER_IRQ, BUSY_AFTER_IRQ, ERR_AS_DONE ...).
- 주어진 `run_campaign()` 이 결함마다 8 kind × 4 seed = 32 job 을 돌린다. 좋은 엔진은 0 실패, 결함마다 **담당 kind 의 4 job 모두** 에서 기대 시그니처(check + hint)가 나와야 한다.

## 2. Problem (English)

> **Scatter-gather DMA validation: descriptor chains and data integrity.**
>
> A DMA channel (`SGDMA0`) walks a linked list of 32-byte descriptors in DRAM: `NEXT`, `SRC`, `DST`, `CTRL` (`LEN[15:0]`, `LAST[16]`), and two write-back words `STAT` (`DONE`, `ERR`, `ERR_CODE[7:4]`) and `XFER`. Registers: `CTRL` (START, SRST, IE_DONE, IE_ERR), `STATUS` (BUSY, ERR_CODE), `IRQ_STAT` (W1C DONE/ERR), `DESC_HEAD`, `CUR_DESC`, `DESC_CNT`, `BYTE_CNT`, and the interrupt-controller line `INTC_RAW` bit 20. Every register access advances the engine by one tick. A descriptor whose source or destination range does not decode must be written back with `ERR`, stop the chain, and raise `ERR`; otherwise the chain ends with `DONE` after the last write-back.
>
> 1. `uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)` — CRC-32/IEEE, chainable like zlib.
> 2. `uint32_t dma_run(uint32_t head, unsigned max_polls, unsigned *polls)` — clear stale interrupts, start the chain, poll `IRQ_STAT` with a bounded loop.
> 3. `void check_data(const job_t *J, fails_t *F)` — compare every completed segment's destination CRC with the golden CRC captured at generation time, locate the first bad byte, verify sources are intact, and scan the whole destination arena for writes outside completed segments.
> 4. `void gen_job(int kind, uint32_t seed, job_t *J)` — deterministic directed + random job generator: aligned, unaligned, 4 KiB-crossing, length 0 / 1 / 65535, mid-chain error, random.
> 5. `void check_status(const job_t *J, uint32_t irq, fails_t *F)` — check descriptor write-back, then interrupt / status / counters / current-descriptor registers and the line, then clear.
> 6. `int hint(const job_t *J, const fail_t *f)` — map a failure to a probable root cause.
>
> Example: with destination addresses wrapping inside a 4 KiB page, job `CROSS_4K#2` must report `desc=1 check=DATA_CRC addr=0x80021000 first_bad=+0x60 hint=4K_CROSS` plus a `STRAY_WRITE` at `0x80020000`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 깨짐 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

실제로 돌린 결과 (앞부분과 끝만):

```text
FAIL starter.c:490  crc32_update(0, chk, 9) == 0xCBF43926u
FAIL starter.c:491  crc32_update(0, (const uint8_t *)"a", 1) == 0xE8B7BE43u
FAIL starter.c:493  crc32_update(crc32_update(0, chk, 4), chk + 4, 5) == 0xCBF43926u
...
[good] jobs=32 failing=32 bus_accesses=128 ticks=128
...
8/44 passed
```

주어진 코드: 엔진 mock(`dma_tick`, `reg_read32`/`reg_write32`, `dma_fault_t`), DRAM 헬퍼, `job_load()`(src 랜덤 데이터 · 골든 CRC · 디스크립터 기록 · dst arena FILL), 버퍼 배치 `place()`/`place_cross()`, `run_job()`, `run_campaign()`, 리포트(`print_fail`), 생성기 속성 검사 `job_props_ok()`. 채울 곳은 `crc32_update`, `dma_run`, `check_data`, `gen_job`, `check_status`, `hint` 여섯 개. starter 에서는 job 이 비어 있고 poll 이 바로 timeout 이므로 **좋은 엔진도 32/32 job 실패** — "항상 실패하는 테스트" 는 결함 주입 행을 전부 빨갛게 칠하지만 아무것도 증명하지 않는다. 통과하는 8개는 mock sanity 와 우연히 맞는 것들이다.

## 4. 힌트

<details>
<summary>힌트 1 — CRC-32 는 파라미터 다섯 개로 정의된다</summary>

width 32, poly `0x04C11DB7`(비트 반전하면 `0xEDB88320`), init `0xFFFFFFFF`, 입력·출력 반사(reflected), 최종 XOR `0xFFFFFFFF`. 반사형은 LSB 부터 처리하므로 `crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1))`. 이어 부르기를 지원하려면 함수 **안에서** 시작할 때 `~crc`, 끝날 때 `~crc` (zlib 방식). 검증 벡터 `"123456789"` → `0xCBF43926`.

</details>

<details>
<summary>힌트 2 — poll 은 반드시 끝나야 하고, 시작 전 잔여 상태를 지워야 한다</summary>

`while (*polls < max_polls) { irq = reg_read32(IRQ_STAT); ++*polls; if (irq & (DONE|ERR)) break; }`. 이전 job 의 DONE 이 남아 있으면 첫 poll 에서 "끝났다" 고 착각한다 — START 전에 `IRQ_STAT` 에 `DONE|ERR` 를 W1C. 테스트에 `LEN_MAX` job 이 1 poll 이 아니라 256 poll 넘게 걸리는지 확인하는 CHECK 가 있다.

</details>

<details>
<summary>힌트 3 — CRC 는 "있어야 할 곳" 만 본다. "없어야 할 곳" 은 따로</summary>

dst 조각마다 CRC 를 비교해도, 엔진이 조각 **밖**에 쓴 바이트는 안 보인다. 완료된 조각을 `COV[]` 에 표시하고, arena 전체에서 `!COV[o] && byte != FILL` 인 곳을 센다. 첫 주소·값·개수를 하나의 `STRAY_WRITE` 로 남긴다 (수백 개 실패 줄 대신).

</details>

<details>
<summary>힌트 4 — check_status 는 순서가 정답의 일부</summary>

레지스터를 한 번 읽을 때마다 시간이 흐른다(tick). DONE 을 본 직후 레지스터부터 읽으면, 늦게 도착한 write-back 이 그 사이에 써져서 `irq_early` 결함이 **사라진다**. DONE 을 본 그 순간의 DRAM(write-back)을 먼저 확인하고, 레지스터는 그다음. 기대값: `stop = err_idx (에러 job) 또는 n`, `i < stop` 은 DONE/len, `i == stop` 은 `ERR|code<<4`/0, 그 뒤는 0/0.

</details>

<details>
<summary>힌트 5 — 4K_CROSS 와 TAIL_BYTES 의 지문</summary>

4K wrap: 첫 불일치 주소가 **페이지 정렬** 이고 오프셋 > 0 (조각이 경계 앞에서 시작해 경계에서 깨짐). tail drop: 조각 끝이 비정렬이고 `len - 오프셋 ≤ 3` (마지막 부분 워드만 빠짐). LEN 비트 누락: `XFER` 의 exp^act 가 비트 하나이고 act < exp.

</details>

## 5. 풀이

### Part A — 왜 CRC 이고, 왜 그것만으로는 부족한가

**CRC-32 파라미터 (CRC-32/ISO-HDLC, 흔히 "CRC-32").** Ethernet FCS, zlib, PNG 가 같은 것을 쓴다.

| 파라미터 | 값 | 비고 |
|---|---|---|
| poly | `0x04C11DB7` (반사형 상수 `0xEDB88320`) | x³² + x²⁶ + x²³ + x²² + x¹⁶ + x¹² + x¹¹ + x¹⁰ + x⁸ + x⁷ + x⁵ + x⁴ + x² + x + 1 |
| init / xorout | `0xFFFFFFFF` / `0xFFFFFFFF` | 앞쪽 0 바이트도 CRC 에 반영되게 |
| refin / refout | true / true | 바이트의 LSB 부터 |
| check (`"123456789"`) | `0xCBF43926` | 테스트 벡터 |

비트 단위 구현이 8 회전 × 바이트라 느리지만(테이블·slicing 은 ALGO-M02, 하드웨어 명령은 ARM-A06), 검증 코드에서는 **정확성과 이식성**이 우선이다. 성질 두 가지가 SG 검증에 딱 맞다:

1. **이어 부르기**: `crc(crc(0, a), b) == crc(a‖b)`. 조각별 CRC 를 이어 붙이면 "흩어진 dst 전체" 의 CRC 와 "모인 src 전체" 의 CRC 를 비교할 수 있다. 테스트는 10000 바이트 버퍼를 무작위 지점에서 50번 잘라 확인한다.
2. **검출 보장**: 차수 32 CRC 는 길이 32 비트 이하의 **모든 burst 오류**(단일 비트 포함)를 검출하고, 그 밖의 임의 손상은 약 2⁻³² 확률로만 놓친다. DMA 결함은 대개 "한 덩어리가 통째로 틀림" 이라 사실상 100% 다.

**memcmp 가 아니라 CRC 인 이유** — 실리콘에서는:

- dst 가 다른 칩(PCIe 엔드포인트 BAR, 다른 도메인 SRAM)에 있어 CPU 가 src 와 나란히 읽기 어렵다. 골든 CRC 는 **생성 시점에 한 번** 계산해 두면 끝.
- src 자체가 DMA 중에 망가질 수 있다(엔진이 src 에 쓰는 결함). memcmp(src, dst) 는 둘 다 같이 틀리면 통과한다. 그래서 `check_data` 는 src 도 골든과 비교한다(`SRC_INTACT`).
- 로그·대역폭: 실패 줄에 CRC 두 개만 남기고, 필요할 때만 바이트 비교로 위치를 좁힌다 (연습 1: 바이트 비교조차 못 할 때 CRC 이진 탐색).

**CRC 로 못 보는 것 — stray write.** CRC 는 조각 *안* 이 맞는지만 본다. 엔진이 조각 밖 엉뚱한 곳에 쓰면(4K wrap, 길이 디코드 초과, 에러 후 계속) 그 조각의 CRC 는 멀쩡할 수 있다. 그래서 dst arena 를 `0xEE` 로 채워 두고, 완료 구간 밖이 그대로인지 **전부** 스캔한다. 리눅스 `dmatest` 모듈도 같은 이유로 버퍼 앞뒤 영역까지 패턴으로 검사한다. 한계: 옮겨진 데이터가 우연히 `0xEE` 면 그 바이트는 안 보인다 — Part C 의 리포트에서 실제로 1바이트가 그렇게 숨었다(209 → 208). 패턴 fill 과 CRC 는 서로의 사각지대를 메운다.

checker 자체 검증 (테스트가 확인하는 사실): host `memcpy` 를 "완벽한 DMA" 로 써서 `check_data` 가 0 실패 → seg2 의 +17 한 비트, seg3 바로 뒤 1바이트, src0 첫 바이트를 망가뜨리면 정확히 3개: `SRC_INTACT desc0`, `DATA_CRC desc2 first_bad=17`, `STRAY_WRITE addr=seg3 끝 count=1`. src0 을 망가뜨려도 **dst0 은 골든과 같으므로** DATA 는 통과한다 — 골든 기준 비교라서 원인(src 손상)과 결과가 섞이지 않는다.

**poll 루프.** 좋은 엔진에서 `ALIGNED#1` (4 디스크립터) 은 23 poll, `LEN_MAX#1` (65535 + 64 B) 은 261 poll 에 DONE. `len0_hang` 에서 `max_polls=500` 이면 정확히 500 poll 후 timeout 으로 돌아온다 — 테스트 러너가 멈추지 않는 것이 SIV-B09 의 핵심이다.

### Part B — 경계 케이스는 어디서 오는가

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m2b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">CROSS_4K#1: desc1 dst=0x80020FA6 len=553 — 경계 앞 90 B, 뒤 463 B</text>
  <rect x="20" y="50" width="340" height="40" fill="none" stroke="currentColor"/>
  <rect x="380" y="50" width="340" height="40" fill="none" stroke="currentColor"/>
  <text x="24" y="44" fill="currentColor" font-family="monospace" font-size="12">0x80020000</text>
  <text x="384" y="44" fill="currentColor" font-family="monospace" font-size="12">0x80021000 (4 KiB 경계)</text>
  <rect x="24" y="55" width="70" height="30" class="f-mut"/>
  <text x="59" y="75" text-anchor="middle" fill="currentColor" font-size="12">desc0</text>
  <rect x="330" y="55" width="30" height="30" class="f-acc"/>
  <rect x="380" y="55" width="150" height="30" class="f-acc"/>
  <text x="455" y="75" text-anchor="middle" fill="currentColor" font-size="12">463 B (정상)</text>
  <text x="300" y="110" fill="currentColor" font-size="12">90 B</text>
  <text x="20" y="130" fill="currentColor" font-weight="bold">정상 엔진: 경계에서 쪼개(split) 다음 페이지로 계속 → 통과</text>
  <rect x="20" y="150" width="340" height="40" fill="none" stroke="currentColor"/>
  <rect x="380" y="150" width="340" height="40" fill="none" stroke="currentColor"/>
  <rect x="24" y="155" width="150" height="30" class="f-warn"/>
  <text x="99" y="175" text-anchor="middle" fill="currentColor" font-size="12">463 B 가 여기로 감김</text>
  <rect x="330" y="155" width="30" height="30" class="f-acc"/>
  <rect x="380" y="155" width="150" height="30" class="f-mut"/>
  <text x="455" y="175" text-anchor="middle" fill="currentColor" font-size="12">0xEE 그대로</text>
  <path d="M345,152 C345,120 99,120 99,152" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#m2b)"/>
  <text x="20" y="215" fill="currentColor">wrap_4k: 주소 상위 비트를 페이지에 묶어 둔 채 하위 12비트만 증가 → 같은 페이지 처음으로</text>
  <text x="20" y="235" fill="currentColor">결과 ① desc1 DATA_CRC first_bad=+0x5A (주소 0x80021000) ② desc0 덮어씀 ③ 경계 앞 빈 곳 STRAY 208 B</text>
  <text x="20" y="253" fill="currentColor" font-size="12">(위 그림: 정상 엔진 / 아래 그림: wrap_4k 결함)</text>
</svg>
```

**4 KiB 경계가 특별한 이유.** AXI 는 한 burst 가 4 KiB 주소 경계를 넘지 못하게 규정하고, PCIe 도 메모리 요청이 4 KiB 경계를 넘지 못한다. 그래서 DMA 엔진(또는 그 앞의 버스 브리지)은 긴 전송을 경계에서 **쪼개야** 하고, 이 split 로직이 버그의 단골이다: 경계에서 주소 상위 비트를 갱신 안 함(wrap), 경계 직후 첫 beat 의 byte-enable 오류, 경계에서 길이 재계산 off-by-one. 페이지 단위 IOMMU/SMMU 매핑이 끼면 경계 너머가 **다른 물리 페이지** 라서 결과가 더 이상해진다. 경계 교차는 "있으면 좋은" 케이스가 아니라 directed 로 반드시 만드는 케이스다 (연습 2: 올바른 split).

**kind 별 설계 의도:**

| kind | 만드는 조건 | 겨냥하는 결함 유형 |
|---|---|---|
| `ALIGNED` | 64 B 정렬, len = 64 × (1..16), 경계 안 넘음 | 기준선 — 여기서 실패하면 기본 경로 문제 |
| `UNALIGNED` | src/dst 하위 2비트 1..3, len 1..300 | 앞/뒤 부분 워드, byte-enable, 정렬 shifter |
| `CROSS_4K` | desc1 의 dst 가 k(1..len-1) 바이트 뒤 경계 교차 | split / 주소 증가 |
| `LEN0` | 가운데 디스크립터 len 0 | "남은 길이 == 0" 판정 전에 감소하는 카운터, hang |
| `LEN1` | 모든 len 1, 정렬 무작위 | 최소 전송, 시작=끝 워드 |
| `LEN_MAX` | 65535 B (필드 최대) | 길이 필드 폭, 카운터 overflow, 긴 전송의 경계 다수 교차 |
| `ERR_MID` | n=5 중 1..3번의 src 또는 dst 를 미디코드 주소로 | 에러 시 멈춤 · write-back · ERR 보고 |
| `RANDOM` | n 1..8, len 0..4096 (1/8 확률로 0/1), 정렬 0..63, 교차 허용 | 조합 · 예상 못한 상호작용 |

**길이 0 / 1 / max 가 경계인 이유.** 길이 필드가 W 비트면, `0` 은 모든 비트가 0 이라 **모든 비트의 stuck-at-1** 을, `max` 는 모든 비트가 1 이라 **모든 비트의 stuck-at-0** 을 한 번에 드러낸다(연습 4: {0, 0xFFFF} 가 32개 stuck-at 결함 전부를 덮는다). 랜덤 길이 0..4096 은 bit 13..15 를 한 번도 1 로 만들지 않으므로 `len_15bit` 같은 결함을 **영원히** 못 잡는다. 1 은 "시작 워드 = 끝 워드" 인 유일한 길이다.

**체인 중간 에러의 기대값.** 에러 job 은 "에러가 났으니 실패" 가 아니라 **스펙대로 에러가 났는가** 를 본다:

| 디스크립터 | STAT | XFER | dst |
|---|---|---|---|
| `i < e` | `DONE` | len | src 와 같음 (CRC) |
| `i == e` | `ERR \| code<<4` | 0 | 손대지 않음 (FILL) |
| `i > e` | 0 | 0 | 손대지 않음 (FILL) |
| 레지스터 | `IRQ_STAT == ERR` (DONE 없음), `STATUS == code<<4` (BUSY=0), `DESC_CNT == e`, `BYTE_CNT == Σlen(i<e)`, `CUR_DESC == desc[e]` | | |

"데이터를 옮기기 전에 검사" 는 이 IP 의 스펙 선택이다. 실제 IP 는 전송 도중 SLVERR 를 받으면 일부 바이트가 이미 써진 상태로 멈출 수 있고, 그때는 `XFER` 가 실제 쓴 양이어야 한다 — 기대값은 IP 스펙서에서 가져오고, 스펙이 모호하면 그 자체가 검증 이슈(스펙 버그)로 올린다.

좋은 엔진은 8 kind × 시드 8개 = 64 job 을 전부 통과하고, 에러 job 8개 모두에서 `e+1` 번 dst 의 첫·끝 바이트가 `0xEE` 그대로다.

**재현성.** `gen_job` 은 `(kind, seed)` 만의 함수다 — 테스트가 같은 입력으로 두 번 생성해 `memcmp` 로 비교한다(128/128). 야간 회귀에서 실패한 job 은 `job=CROSS_4K#2` 한 줄로 아침에 그대로 재현된다. 시드를 시간에서 뽑으면 이 성질이 사라진다(꼭 써야 하면 시드를 로그에 남긴다).

**랜덤만으로 충분한가 — demo_soak.c 실측.** `RANDOM` job 만 결함마다 2000개 돌렸다 (Apple Silicon, Apple clang 21.0.0, `-O2`):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_soak.c -o demo_soak && ./demo_soak 2000
```

```text
RANDOM-only soak: 2000 jobs per fault (n=1..8 desc, len 0..4096, any alignment)
fault         first_seed     detected       rate
none               never       0/2000        0.0%
wrap_4k                2    1482/2000       74.1%
tail_drop              2    1899/2000       95.0%
len_15bit          never       0/2000        0.0%
len0_hang              3     432/2000       21.6%
err_continue       never       0/2000        0.0%
err_as_done        never       0/2000        0.0%
irq_early              1    2000/2000      100.0%
bytecnt_last           1    1883/2000       94.2%
18000 jobs in 4.84 s CPU (3715 jobs/s)
```

랜덤은 "흔한" 결함은 빨리 찾지만, 랜덤 분포 밖(길이 ≥ 32768, 에러 디스크립터)은 2000 job 을 돌려도 **0** 이다. directed 32 job 은 8개 결함을 전부 잡는다. 결론: 랜덤은 경계 케이스의 **대체가 아니라 보완** 이고, 랜덤 분포 자체를 커버리지로 측정해야 한다 (SIV-M13).

### Part C — 완료 신호의 일관성

**확인 순서.** DONE 은 "모든 결과가 보이는 상태" 라는 **약속** 이다. 그래서 DONE 을 본 그 순간에:

1. **write-back (DRAM)** — 레지스터를 더 읽기 전에. 레지스터 읽기는 시간을 흐르게 하고(실리콘에서도 수백 ns), 늦게 도착하는 write-back 이 그 사이에 써져 결함이 숨는다.
2. **IRQ_STAT** — poll 을 끝낸 바로 그 값이 정확히 `DONE` 또는 `ERR` (둘 다 서면 안 됨).
3. **STATUS / DESC_CNT / BYTE_CNT / CUR_DESC / 라인** — BUSY=0 이어야 한다. IRQ 를 올리고도 BUSY 면 엔진이 아직 무언가를 하고 있다는 뜻.
4. **W1C 후 IRQ_STAT == 0, 라인 == 0** (SIV-B11 의 clear 확인).
5. **데이터** (`check_data`).

| check | 지키는 약속 | 실리콘에서 실제로 보는 버그 |
|---|---|---|
| `WB_STATUS`/`WB_XFER` | DONE 시점에 write-back 이 보임 | 인터럽트가 posted write 를 앞지름 → 드라이버가 미완료 디스크립터를 처리 |
| `IRQ_STAT` | 에러는 ERR 로 | 에러를 DONE 으로 보고 → 드라이버가 깨진 데이터를 정상으로 넘김 |
| `STATUS` BUSY | IRQ = 끝 | 에러 후에도 계속 진행, IRQ 를 너무 일찍 올림 |
| `DESC_CNT`/`BYTE_CNT` | 카운터 = 실제 | 마지막 디스크립터 누락, 길이 필드 폭 |
| `CUR_DESC` | 에러 위치 보고 | 에러 디스크립터가 아니라 그 다음을 가리킴 → 드라이버가 엉뚱한 것을 재시도 |

solution 을 실제로 돌린 출력:

```text
[good] jobs=32 failing=0 bus_accesses=1909 ticks=1909
fault         ALIGNED   UNALIGNED CROSS_4K  LEN0      LEN1      LEN_MAX   ERR_MID   RANDOM    total
wrap_4k       0/4       0/4       4/4       0/4       0/4       4/4       0/4       3/4       11/32
tail_drop     0/4       4/4       4/4       0/4       3/4       4/4       0/4       3/4       18/32
len_15bit     0/4       0/4       0/4       0/4       0/4       4/4       0/4       0/4       4/32
len0_hang     0/4       0/4       0/4       4/4       0/4       0/4       0/4       2/4       6/32
err_continue  0/4       0/4       0/4       0/4       0/4       0/4       4/4       0/4       4/32
err_as_done   0/4       0/4       0/4       0/4       0/4       0/4       4/4       0/4       4/32
irq_early     4/4       4/4       4/4       4/4       4/4       4/4       0/4       4/4       28/32
bytecnt_last  4/4       4/4       4/4       4/4       4/4       4/4       0/4       4/4       28/32
[wrap_4k] first: FAIL SGDMA0 job=CROSS_4K#1 desc=0/3 check=DATA_CRC addr=0x80020040 exp=0xD2ABE8DB act=0x575C9B27 diff=0x85F773FC first_bad=+0x0/len=0xFE hint=DATA_CORRUPT
[tail_drop] first: FAIL SGDMA0 job=UNALIGNED#1 desc=0/4 check=DATA_CRC addr=0x80020108 exp=0x81247E9A act=0x3FBC6775 diff=0xBE9819EF first_bad=+0xC5/len=0xC7 hint=TAIL_BYTES
[len_15bit] first: FAIL SGDMA0 job=LEN_MAX#1 desc=0/2 check=WB_XFER addr=0x80000014 exp=0x0000FFFF act=0x00007FFF diff=0x00008000 hint=LEN_FIELD_BIT
[len0_hang] first: FAIL SGDMA0 job=LEN0#1 desc=1/3 check=TIMEOUT addr=0x80000300 exp=0x00000000 act=0x00000001 diff=0x00000001 polls=4096 hint=LEN0_HANG
[err_continue] first: FAIL SGDMA0 job=ERR_MID#1 desc=-/5 check=STATUS reg=STATUS(0x004) exp=0x00000010 act=0x00000011 diff=0x00000001 hint=BUSY_AFTER_IRQ
[err_as_done] first: FAIL SGDMA0 job=ERR_MID#1 desc=-/5 check=IRQ_STAT reg=IRQ_STAT(0x008) exp=0x00000002 act=0x00000001 diff=0x00000003 hint=ERR_AS_DONE
[irq_early] first: FAIL SGDMA0 job=ALIGNED#1 desc=3/4 check=WB_STATUS addr=0x80000110 exp=0x00000001 act=0x00000000 diff=0x00000001 hint=WB_AFTER_IRQ
[bytecnt_last] first: FAIL SGDMA0 job=ALIGNED#1 desc=-/4 check=BYTE_CNT reg=BYTE_CNT(0x018) exp=0x00000D00 act=0x000009C0 diff=0x000004C0 hint=COUNTER
---- report: SGDMA0 fault=wrap_4k job=CROSS_4K#2 ----
  desc0 @0x80000000 src=0x80001040 dst=0x80020040 len=366
  desc1 @0x80000300 src=0x80001200 dst=0x80020FA0 len=133  (dst crosses 4K)
  desc2 @0x80000600 src=0x80001300 dst=0x80021080 len=480
FAIL SGDMA0 job=CROSS_4K#2 desc=1/3 check=DATA_CRC addr=0x80021000 exp=0x9B65D236 act=0x6870C452 diff=0xF3151664 first_bad=+0x60/len=0x85 hint=4K_CROSS
FAIL SGDMA0 job=CROSS_4K#2 desc=-/3 check=STRAY_WRITE addr=0x80020000 exp=0x000000EE act=0x00000015 diff=0x000000FB stray_bytes=37 hint=STRAY_WRITE
44/44 passed
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">결함 × job kind 검출 매트릭스 (실측, 칸 = 실패 job 수 / 4, 강조 = 담당 kind)</text>
  <g fill="currentColor" font-weight="bold" font-size="12">
    <text x="10" y="48">주입 결함</text><text x="120" y="48">ALIGN</text><text x="190" y="48">UNAL</text><text x="255" y="48">X4K</text>
    <text x="320" y="48">LEN0</text><text x="385" y="48">LEN1</text><text x="450" y="48">MAX</text><text x="515" y="48">ERR</text>
    <text x="580" y="48">RAND</text><text x="636" y="48">첫 시그니처</text>
  </g>
  <line x1="10" y1="56" x2="730" y2="56" stroke="currentColor" opacity=".35"/>
  <g class="f-acc">
    <rect x="248" y="62" width="50" height="26"/><rect x="183" y="94" width="50" height="26"/>
    <rect x="443" y="126" width="50" height="26"/><rect x="313" y="158" width="50" height="26"/>
    <rect x="508" y="190" width="50" height="26"/><rect x="508" y="222" width="50" height="26"/>
    <rect x="113" y="254" width="50" height="26"/><rect x="113" y="286" width="50" height="26"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="80">wrap_4k</text><text x="10" y="112">tail_drop</text><text x="10" y="144">len_15bit</text>
    <text x="10" y="176">len0_hang</text><text x="10" y="208">err_continue</text><text x="10" y="240">err_as_done</text>
    <text x="10" y="272">irq_early</text><text x="10" y="304">bytecnt_last</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="128" y="80">0</text><text x="198" y="80">0</text><text x="263" y="80">4</text><text x="328" y="80">0</text><text x="393" y="80">0</text><text x="458" y="80">4</text><text x="523" y="80">0</text><text x="588" y="80">3</text>
    <text x="128" y="112">0</text><text x="198" y="112">4</text><text x="263" y="112">4</text><text x="328" y="112">0</text><text x="393" y="112">3</text><text x="458" y="112">4</text><text x="523" y="112">0</text><text x="588" y="112">3</text>
    <text x="128" y="144">0</text><text x="198" y="144">0</text><text x="263" y="144">0</text><text x="328" y="144">0</text><text x="393" y="144">0</text><text x="458" y="144">4</text><text x="523" y="144">0</text><text x="588" y="144">0</text>
    <text x="128" y="176">0</text><text x="198" y="176">0</text><text x="263" y="176">0</text><text x="328" y="176">4</text><text x="393" y="176">0</text><text x="458" y="176">0</text><text x="523" y="176">0</text><text x="588" y="176">2</text>
    <text x="128" y="208">0</text><text x="198" y="208">0</text><text x="263" y="208">0</text><text x="328" y="208">0</text><text x="393" y="208">0</text><text x="458" y="208">0</text><text x="523" y="208">4</text><text x="588" y="208">0</text>
    <text x="128" y="240">0</text><text x="198" y="240">0</text><text x="263" y="240">0</text><text x="328" y="240">0</text><text x="393" y="240">0</text><text x="458" y="240">0</text><text x="523" y="240">4</text><text x="588" y="240">0</text>
    <text x="128" y="272">4</text><text x="198" y="272">4</text><text x="263" y="272">4</text><text x="328" y="272">4</text><text x="393" y="272">4</text><text x="458" y="272">4</text><text x="523" y="272">0</text><text x="588" y="272">4</text>
    <text x="128" y="304">4</text><text x="198" y="304">4</text><text x="263" y="304">4</text><text x="328" y="304">4</text><text x="393" y="304">4</text><text x="458" y="304">4</text><text x="523" y="304">0</text><text x="588" y="304">4</text>
    <text x="636" y="80">4K_CROSS</text><text x="636" y="112">TAIL_BYTES</text><text x="636" y="144">LEN_FIELD_BIT</text>
    <text x="636" y="176">LEN0_HANG</text><text x="636" y="208">BUSY_AFTER</text><text x="636" y="240">ERR_AS_DONE</text>
    <text x="636" y="272">WB_AFTER_IRQ</text><text x="636" y="304">COUNTER</text>
  </g>
</svg>
```

표에서 읽을 것:

1. **열마다 맡은 결함이 있다.** `len_15bit` 는 LEN_MAX 열만, `err_*` 는 ERR_MID 열만 잡는다. 어떤 kind 하나를 빼면 결함 하나가 통째로 사라진다. 테스트가 확인하는 사실: `len_15bit` 의 total 이 정확히 4, `err_continue`/`err_as_done` 도 4.
2. **ERR_MID 열은 `irq_early`/`bytecnt_last` 를 못 잡는다.** 에러로 끝나는 체인은 "마지막 디스크립터 완료" 경로를 안 지난다. 에러 경로와 정상 완료 경로는 서로의 사각지대다.
3. **`tail_drop` 이 CROSS_4K 에서도 잡힌다.** 경계 교차 디스크립터의 dst 가 비정렬이기 때문 — 경계 케이스는 하나만 가진 게 아니라 여러 조건이 겹친다. 그래서 첫 실패의 hint 가 정확한지가 중요하다.

**피해자 vs 원인 — 첫 실패만 보면 오진한다.** `wrap_4k` 의 첫 실패는 `CROSS_4K#1 desc=0 DATA_CRC first_bad=+0x0 hint=DATA_CORRUPT` 다. desc0 은 경계를 넘지도 않는데? 그 job 을 펼쳐 보면 (같은 코드로 따로 실행한 출력):

```text
  desc0 src=0x80001040 dst=0x80020040 len=254
  desc1 src=0x80001180 dst=0x80020FA6 len=553 (crosses)
  desc2 src=0x80001400 dst=0x80021240 len=563
FAIL SGDMA0 job=CROSS_4K#1 desc=0/3 check=DATA_CRC addr=0x80020040 ... first_bad=+0x0/len=0xFE hint=DATA_CORRUPT
FAIL SGDMA0 job=CROSS_4K#1 desc=1/3 check=DATA_CRC addr=0x80021000 ... first_bad=+0x5A/len=0x229 hint=4K_CROSS
FAIL SGDMA0 job=CROSS_4K#1 desc=-/3 check=STRAY_WRITE addr=0x80020000 exp=0x000000EE act=0x000000FA ... stray_bytes=208 hint=STRAY_WRITE
```

desc1 의 경계 뒤 463 B 가 `0x80020000` 부터 감겨 써지면서 desc0(`0x80020040`, 254 B)을 **통째로 덮었다**. desc0 은 피해자, desc1 이 원인이다. 그래서 checker 는 첫 실패에서 멈추지 않고 job 의 모든 실패를 모으며, 리포트는 디스크립터 배치(주소·길이·경계 교차 표시)를 함께 출력한다. stray 는 463 − 254 = 209 바이트여야 하는데 208 로 나온 것은 감겨 쓴 데이터 중 1바이트가 우연히 `0xEE` 였기 때문 — fill 패턴의 한계를 CRC 가 메운다.

**리포트 한 줄로 디버그 엔지니어가 할 일:**

| hint | 다음 행동 |
|---|---|
| `4K_CROSS` | split 로직 / 버스 브리지 주소 증가, 경계 직후 첫 beat 파형 (에뮬레이션/ILA) |
| `TAIL_BYTES` | 마지막 beat 의 byte strobe(WSTRB) 계산, `(dst+len)&3` 별로 재현 |
| `LEN_FIELD_BIT` | 레지스터 기술서의 LEN 폭 vs RTL 의 카운터 폭 (exp^act 가 bit 15) |
| `LEN0_HANG` | "len==0" 을 감소 전에 검사하는지; 드라이버 우회책(len 0 디스크립터 금지)이 errata 후보 |
| `BUSY_AFTER_IRQ` | 에러 후 멈춤 조건 / IRQ 발생 시점 |
| `ERR_AS_DONE` | 에러 응답(DECERR/SLVERR) → 상태 머신 전달 경로 |
| `WB_AFTER_IRQ` | write-back 의 쓰기 응답을 받은 뒤 IRQ 를 올리는지 (AXI B 채널), 인터커넥트 ordering |
| `COUNTER` | exp−act = 마지막 디스크립터 길이(`0xD00−0x9C0 = 0x340`) → 마지막 갱신 누락 |

### 실제 검증 현장과의 연결

- **pre-silicon**: UVM 에서는 DMA 를 메모리 모델 + 스코어보드(예상 전송 목록 vs AXI 모니터가 본 실제 쓰기)로 검증한다. 스코어보드는 "어떤 주소에 어떤 바이트가 써졌나" 를 전부 보므로 stray 를 자연스럽게 잡는다. post-silicon 에는 버스 모니터가 없으니 **arena fill + 스캔** 이 그 대체다.
- **post-silicon**: 같은 C 테스트를 온칩 펌웨어나 JTAG 로 띄운 베어메탈 이미지에서 돌린다. 리눅스가 올라간 뒤에는 `dmatest` 모듈(dmaengine 드라이버 대상, 무작위 오프셋·길이, 버퍼 바깥 영역 검사)이 같은 역할을 한다.
- **캐시 일관성**: 실보드에서는 CPU 가 src 를 쓴 뒤 캐시를 **clean**, DMA 후 dst 를 읽기 전에 **invalidate** 해야 한다(비일관 시스템). 빠뜨리면 DMA 는 멀쩡한데 CRC 가 틀리거나(stale 캐시 라인), 반대로 진짜 결함이 캐시 때문에 안 보인다. 실패가 64 B(캐시 라인) 단위로 정렬돼 있으면 먼저 의심한다.
- **ATE vs SLT vs 시스템 검증 (SIV-M01)**: 엔진 내부 구조 결함은 scan/BIST 가, 이 레슨의 기능·경계 결함은 validation 팀의 펌웨어 테스트와 SLT 가, 동시 트래픽 경합은 SIV-M09 의 스트레스가 맡는다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 44/44 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san
# 44/44 passed, exit 0, 출력에서 "runtime error"/"AddressSanitizer" 0건 확인
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 조각별 CRC 만 비교 | 조각 밖에 쓴 바이트(4K wrap, 에러 후 계속)가 안 보인다 | dst arena 를 FILL 로 채우고 완료 구간 밖 전체 스캔 |
| `memcmp(src, dst)` 로만 비교 | DMA 가 src 도 망가뜨리면 둘 다 틀려서 통과, 원격 dst 는 비교 불가 | 생성 시점 골든 CRC, src 도 골든과 비교 |
| START 전에 IRQ_STAT 를 안 지움 | 이전 job 의 DONE 으로 첫 poll 에 "완료" — 데이터 검사가 전송 중에 돈다 | `reg_write32(IRQ_STAT, DONE\|ERR)` 후 START |
| `while (!(IRQ_STAT & DONE));` | len0 hang 에서 테스트 러너가 영원히 멈춤, ERR 로 끝나면 역시 무한 루프 | 상한 있는 poll + `DONE\|ERR` 둘 다 종료 조건 |
| DONE 후 레지스터부터 읽고 write-back 확인 | 그 사이 늦은 write-back 이 도착해 `irq_early` 가 사라짐 | DONE 을 본 즉시 DRAM write-back 먼저 |
| 에러 job 을 "실패했으니 PASS" 로 처리 | 에러를 DONE 으로 보고하거나 에러 뒤 계속 진행해도 통과 | 에러 위치·코드·멈춤·손대지 않은 뒤쪽까지 기대값으로 |
| 첫 실패만 리포트 | wrap 의 피해자(desc0)를 원인으로 오진 | job 의 모든 실패 + 디스크립터 배치 출력 |
| 랜덤 시드를 시간에서 | 야간 실패를 아침에 재현 못 함 | `(kind, seed)` 결정적 생성, 시드를 로그에 |
| 랜덤 길이 0..4096 만 | LEN 필드 상위 비트 결함, 에러 경로를 영원히 못 봄 (demo: 0/2000) | 0 / 1 / max / 에러를 directed 로 |
| 실보드에서 캐시 관리 생략 | stale 라인 때문에 가짜 실패 또는 진짜 결함 은폐 | src clean, dst invalidate (또는 non-cacheable 버퍼) |

## 8. 면접 Q&A

<details>
<summary>Q1. 새 SG DMA 엔진을 검증한다면 어떤 테스트를 하겠습니까?</summary>

세 축으로 답한다. **(1) 데이터 무결성**: 생성 시점 골든 CRC 로 디스크립터마다 dst 비교, src 손상 여부, dst 영역 밖 stray write 스캔. **(2) 경계 조건 directed 목록**: 정렬/비정렬(앞·뒤 부분 워드), 4 KiB 경계 교차(AXI·PCIe 규칙), 길이 0/1/max, 디스크립터 1개/최대 개수, 체인 중간 에러(미디코드 주소, 비정렬 디스크립터), 디스크립터가 비연속 위치. **(3) 완료 신호 일관성**: DONE 시점에 write-back 이 보이는지, IRQ_STAT 이 정확히 DONE/ERR 인지, 카운터·CUR_DESC·라인·clear. 그 위에 고정 시드 랜덤으로 조합을 돌리고, 결함 주입(또는 RTL 의 알려진 버그 재현)으로 테스트 자체가 잡는지 확인한다. 다음 단계는 동시 트래픽 스트레스(SIV-M09)와 PVT 코너.
꼬리질문 "채널이 여러 개면?" → 채널별 독립 arena 와 시그니처, 채널 간 dst 겹침이 없는데도 서로 덮는지(stray 스캔이 채널 경계를 포함하게), 우선순위·공정성은 완료 순서와 처리량으로 본다.

</details>

<details>
<summary>Q2. 왜 CRC 를 쓰나요? CRC 가 놓치는 경우는?</summary>

골든을 생성 시점에 한 번 계산해 두면 dst 가 원격(다른 칩, BAR, 다른 도메인)이어도 비교할 수 있고, 로그가 작고, src 손상과 dst 손상을 분리할 수 있다. 차수 32 CRC 는 32 비트 이하의 모든 burst 오류를 검출하고 그 밖의 임의 손상은 약 2⁻³² 확률로 놓친다. 놓치는 것은 확률보다 **구조적인** 것이다: (a) 조각 밖의 쓰기 — CRC 범위가 아니다 → stray 스캔, (b) 골든 자체가 틀린 경우 — 생성 후 src 를 CPU 캐시에만 쓰고 clean 안 함, (c) 같은 데이터를 다른 곳에 쓴 경우(두 조각이 같은 내용) — 조각마다 다른 랜덤 데이터를 쓰는 이유.
꼬리질문 "CRC-32 와 CRC-32C 차이는?" → 다항식이 다르다(`0x04C11DB7` vs Castagnoli `0x1EDC6F41`). CRC-32C 는 iSCSI·ext4 메타데이터 등에서 쓰고 x86 SSE4.2·ARMv8 CRC 명령으로 가속된다. 검증용 시그니처로는 어느 쪽이든 되지만, IP 에 하드웨어 CRC 가 있으면 그것과 맞춰야 한다(COMM-B09).

</details>

<details>
<summary>Q3. 4 KiB 경계가 왜 중요한가요?</summary>

AMBA AXI 는 한 burst 가 4 KiB 경계를 넘지 못하게 하고, PCIe 도 메모리 요청이 4 KiB 경계를 넘지 못한다. 페이지 단위 MMU/IOMMU 매핑에서는 경계 너머가 다른 물리 페이지일 수 있다. 그래서 DMA 엔진(또는 브리지)은 긴 전송을 경계에서 쪼개는데, 이 split 이 버그의 단골이다 — 상위 주소 비트 미갱신(wrap), 경계 직후 beat 의 strobe, 길이 재계산 off-by-one. 테스트는 경계를 넘는 오프셋 k 를 1..len-1 에서 골라 "경계 직전 1 바이트", "경계 직후 1 바이트" 같은 극단도 생기게 한다. wrap 결함은 경계 앞 같은 페이지를 덮으므로 **다른 디스크립터가 피해자** 로 먼저 실패할 수 있다 — 리포트에서 첫 불일치 주소가 페이지 정렬인 실패를 찾는다.

</details>

<details>
<summary>Q4. 완료 인터럽트를 받았는데 디스크립터 status 가 아직 DONE 이 아닙니다. 하드웨어 버그인가요, 드라이버 버그인가요?</summary>

둘 다 가능하다. **하드웨어**: write-back 쓰기의 응답(AXI B 채널)을 받기 전에 IRQ 를 올리면, 인터럽트(별도 와이어)가 인터커넥트 안의 posted write 를 앞지른다. PCIe 의 MSI 는 그 자체가 메모리 쓰기라 같은 경로의 앞선 쓰기 뒤에 도착하지만(ordering 규칙), 레벨 와이어 인터럽트에는 그런 보장이 없다. **드라이버**: 디스크립터를 캐시 가능한 메모리에 두고 invalidate 를 안 하면 stale 값을 본다; IRQ 후 status 읽기 전에 읽기 배리어가 없거나, 컴파일러가 읽기를 앞당김. 검증에서는 DONE 을 본 **즉시** write-back 을 확인하는 테스트(이 레슨의 `irq_early` 검출)로 하드웨어를 먼저 분리하고, non-cacheable 디스크립터로 드라이버 요인을 제거한다.
꼬리질문 "드라이버의 방어 코드는?" → IRQ 가 와도 디스크립터의 DONE 비트를 기준으로 처리하고(소유권 비트), 아직이면 다음 IRQ/poll 에서 다시 본다.

</details>

<details>
<summary>Q5. 체인 중간에 잘못된 디스크립터가 있으면 엔진은 어떻게 동작해야 하고, 무엇을 확인하나요?</summary>

스펙에 따라 다르지만 보통: 그 디스크립터에서 멈추고, 그 디스크립터의 status 에 에러 종류(디코드 에러/슬레이브 에러/디스크립터 에러)를 쓰고, 에러 인터럽트를 올리고, 현재 디스크립터 포인터가 문제의 디스크립터를 가리킨다. 확인: 앞쪽은 정상 완료(데이터·write-back), 문제 디스크립터는 에러 코드·전송 바이트, 뒤쪽은 손대지 않음(status 0, dst fill 그대로), IRQ 는 ERR 만, BUSY=0, 카운터는 앞쪽까지. 이 레슨에서 `err_continue` 는 `STATUS exp=0x10 act=0x11 (BUSY_AFTER_IRQ)` 로, `err_as_done` 은 `IRQ_STAT exp=0x2 act=0x1` 로 잡혔다. 복구 경로(SRST 후 다음 job 이 정상인지)도 테스트 항목이다 — 이 하네스는 매 job 끝에 SRST 하고 다음 job 이 통과하는 것으로 간접 확인한다.

</details>

<details>
<summary>Q6. 랜덤 테스트를 돌리고 있는데 왜 directed 경계 케이스가 따로 필요하죠?</summary>

랜덤은 그 **분포** 안에서만 탐색한다. 이 레슨의 실측: 길이 0..4096 랜덤 2000 job 은 `len_15bit`(길이 ≥ 32768 에서만 발현)와 에러 경로 결함 2개를 **한 번도** 못 잡았고, `len0_hang` 도 21.6% job 에서만 드러났다. directed 32 job 은 8개를 모두 잡는다. 반대로 랜덤은 내가 생각 못 한 조합(비정렬 + 경계 교차 + 짧은 길이)을 공짜로 준다. 그래서 둘 다 돌리고, 랜덤의 분포를 coverage 로 측정해 구멍을 directed 로 메운다(SIV-M13). 길이 필드라면 {0, max} 두 값이 모든 비트의 stuck-at 을 덮는다(연습 4).
꼬리질문 "랜덤 실패를 어떻게 재현?" → 시드를 job 이름에 넣는다(`CROSS_4K#2`). 생성기가 (kind, seed) 의 순수 함수여야 한다.

</details>

<details>
<summary>Q7. 데이터가 틀렸는데 실패가 항상 64 바이트 경계에 정렬돼 있습니다. 무엇을 의심하나요?</summary>

캐시 라인 크기 단위 → 캐시 일관성 먼저. src 를 쓴 뒤 clean 을 안 했으면 DMA 는 DRAM 의 옛 데이터를 읽고, dst 를 invalidate 안 하고 읽으면 CPU 는 옛 캐시 라인을 본다. 확인 방법: 버퍼를 non-cacheable 로 바꾸거나 캐시 유지 연산을 넣고 재현되는지. 재현이 사라지면 드라이버/테스트 문제, 남으면 하드웨어(예: 일관성 포트 경유 시 snoop 누락)다. 다른 단위의 지문도 쓸모 있다: 4 KiB 정렬이면 split/IOMMU, beat 크기(16/32 B) 단위면 데이터 경로 폭, 끝 1~3 바이트면 byte strobe.

</details>

## 9. 추가 연습

### 연습 1 — CRC 이진 탐색으로 첫 불일치 바이트 찾기

타깃이 "주소, 길이 → CRC" 명령만 주고 바이트 덤프는 느린 상황(JTAG, 원격 dst)에서 첫 불일치 위치를 prefix CRC 비교만으로 찾는 `crc_first_bad(a, b, len, &calls)` 를 작성하라. 4096 B 에서 비교 횟수의 상한은?

<details>
<summary>답 보기</summary>

prefix `[0, mid]` 의 CRC 가 다르면 답은 `≤ mid`, 같으면 `> mid` 인 이진 탐색. 불변식 "답 ∈ [lo, hi]". 전체 비교 1회 + ⌈log₂ 4096⌉ = 12회 → 최대 13 쌍 (실측 13). 정확성의 근거: 첫 불일치 바이트 하나만 포함한 prefix 는 단일 burst(≤ 8 비트) 오류라 CRC 가 **반드시** 다르다. 두 번째 오류까지 포함한 prefix 는 2⁻³² 확률로만 같아질 수 있다.

</details>

### 연습 2 — 4 KiB 분할

`split_4k(addr, len, out, max)` — 전송을 4 KiB 경계를 넘지 않는 조각들로 나눠라. `(0x80020FA0, 133)` 은? `(0x80020040, 0xFFFF)` 는 몇 조각?

<details>
<summary>답 보기</summary>

이 페이지에 남은 `room = 0x1000 - (addr & 0xFFF)` 만큼 떼어 내고 반복. `(0x80020FA0, 133)` → `0x60` + `0x25` (두 번째 조각이 `0x80021000` 에서 시작). `(0x80020040, 0xFFFF)` → 17 조각 (첫 조각 `0xFC0` B 뒤로 15개의 4 KiB 조각, 끝에 나머지 `0x3F` B). 테스트는 무작위 1000개에 대해 조각 합 == len, 각 조각이 경계를 안 넘음, 조각이 연속임을 확인한다. 이것이 `wrap_4k` 결함이 하지 않은 일이다.

</details>

### 연습 3 — START 전 체인 lint

디스크립터 체인을 START 전에 host 에서 걸어 보며 비정렬 · 디스크립터 영역 밖 · 버퍼 범위 밖 · 루프(LAST 없이 되돌아옴) · 너무 김을 판정하는 `chain_lint()` 를 작성하라.

<details>
<summary>답 보기</summary>

디스크립터 영역이 4 KiB / 32 B = 128 슬롯이므로 방문 bitmap 으로 루프를 O(n) 에 잡는다(영역이 크면 Floyd 의 토끼와 거북이). 실패 시 체인 순서상의 인덱스를 돌려준다: `d0→d1→d2→d1` 은 `LOOP bad=3`, dst 를 DRAM 끝 1 바이트 넘기면 `BUF_RANGE bad=2`. 검증 코드가 자기 입력을 먼저 검사하는 이유: 잘못 만든 체인으로 엔진이 hang 하면 "하드웨어 버그" 로 오진하고 반나절을 잃는다. 반대로 **엔진의 에러 처리** 를 검증할 때는 lint 를 끄고 일부러 잘못된 체인을 넣는다(ERR_MID).

</details>

### 연습 4 — 길이 필드 stuck-at 을 덮는 길이 집합

W=16 비트 LEN 필드의 각 비트가 0 또는 1 로 고정된 32개 결함 중, 주어진 길이 집합이 몇 개를 잡는지 세는 `len_faults_covered()` 를 작성하라. {64}, {0xFFFF}, {0, 0xFFFF}, 랜덤 0..4096 2000개는?

<details>
<summary>답 보기</summary>

길이 L 이 "bit b stuck-at-v" 를 잡는다 ⇔ L 의 bit b 가 v 가 아님. {64} → 16, {0xFFFF} → 16 (모든 stuck-at-0), {0, 0xFFFF} → **32/32**, {0, 1} → 17. 랜덤 0..4096 2000개 → 28/32: bit 13..15 의 stuck-at-0 3개 + bit 12 의 stuck-at-0 (bit 12 가 1 인 값은 4096 하나뿐인데 2000번 중 한 번도 안 뽑힘). `len_15bit` 결함이 랜덤 soak 에서 0/2000 이었던 이유를 비트 단위로 설명해 준다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 22/22 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 22/22 passed, 보고 없음
```

실제 출력:

```text
ex1: first-bad by CRC bisect, max 13 prefix-CRC pairs for 4096 B
ex3: lint codes OK/MISALIGNED/DESC_RANGE/BUF_RANGE/LOOP/TOO_LONG
ex4: 2000 random lengths in 0..4096 cover 28/32 LEN stuck-at faults; {0,0xFFFF} covers 32/32
22/22 passed
```

## 10. 핵심 정리

- SG DMA = DRAM 의 디스크립터 연결 리스트(NEXT·SRC·DST·LEN/LAST) + write-back(STAT·XFER) + 완료/에러 IRQ. 관측 지점: 데이터 · arena · write-back · 레지스터 · 라인.
- 무결성 = **생성 시점 골든 CRC-32** (src 와 dst 둘 다 비교) + **arena 전체 stray 스캔**. CRC 는 조각 안, fill 스캔은 조각 밖. 실패하면 첫 불일치 바이트까지.
- CRC-32: poly `0x04C11DB7`/반사 `0xEDB88320`, init·xorout `0xFFFFFFFF`, `"123456789"` → `0xCBF43926`. zlib 식으로 이어 부를 수 있게.
- directed 경계: 비정렬(앞/뒤 부분 워드), 4 KiB 교차(AXI·PCIe), 길이 0/1/max({0,max} = 길이 필드 stuck-at 전부), 체인 중간 에러, 비연속 디스크립터. 랜덤은 보완 — 분포 밖은 영원히 0.
- 에러 job 의 통과 조건: 정확히 그 디스크립터에서 멈춤, ERR+코드 write-back, 뒤쪽 손대지 않음, IRQ=ERR 만, BUSY=0, 카운터·CUR_DESC 일치.
- DONE 은 "모든 결과가 보인다" 는 약속: **DONE 직후 write-back 먼저**, 레지스터는 그다음, 마지막에 W1C 와 라인. 순서를 바꾸면 `irq_early` 가 숨는다.
- poll 은 상한이 있고 DONE|ERR 둘 다 종료 조건, START 전 잔여 IRQ clear. 생성기는 (kind, seed) 의 순수 함수.
- 리포트: job 이름(kind#seed)·디스크립터 배치·check·주소·exp/act/diff·first_bad·hint, 그리고 job 의 **모든** 실패 — 첫 실패는 피해자일 수 있다.

## 참고자료

- AMD/Xilinx PG021 *AXI DMA LogiCORE IP Product Guide* — Scatter Gather 디스크립터(NXTDESC, BUFFER_ADDRESS, CONTROL, STATUS), 에러 비트, 디스크립터 정렬
- Arm *AMBA AXI and ACE Protocol Specification* (IHI 0022) — burst 의 4 KiB 경계 규칙, write response 와 ordering
- PCI Express Base Specification — 메모리 요청의 4 KiB 경계 규칙, MSI/MSI-X 가 posted write 라는 점과 ordering
- NVM Express Base Specification — PRP / SGL 디스크립터, Completion Queue
- Linux kernel `Documentation/driver-api/dmaengine/dmatest.rst`, `drivers/dma/dmatest.c` — 무작위 오프셋/길이 memcpy 테스트, 버퍼 바깥 영역 검사
- Linux kernel `Documentation/core-api/dma-api-howto.rst` — 스트리밍 DMA 매핑과 캐시 유지(clean/invalidate)
- Ross N. Williams, *A Painless Guide to CRC Error Detection Algorithms* — CRC 파라미터 모델(poly, init, refin, refout, xorout, check)
- 관련 레슨: SIV-B02 (mock MMIO), SIV-B09 (poll-with-timeout), SIV-B11 (W1C IRQ trigger-check-clear), SIV-B14 (리포트), SIV-M09 (동시성 스트레스), SIV-M13 (커버리지), ARCH-M11 (DMA 컨트롤러 모델), ALGO-M02 (CRC 테이블/slicing), COMM-B09 (하드웨어 CRC 유닛 맞추기), COMM-A08 (디스크립터 링과 캐시 일관성)
