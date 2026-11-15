---
id: ARCH-M06
time: 90분
updated: 2026-11-11
---
# ARCH-M06 쓰기 정책: Write-back vs Write-through

## 학습 목표

- store 하나가 **4 조합** (write-back(WB) / write-through(WT) × write-allocate(WA) / no-write-allocate(NWA))에서 각각 어디로 가는지(캐시, DRAM, 둘 다)를 적중/미스별로 말하고, C 시뮬레이터로 구현한다.
- "DRAM 읽기 바이트 / 쓰기 바이트 / 트랜잭션 수" 를 **정의부터** 고정하고(채움 = B 바이트, 더티 교체 = B 바이트, store-through = store 크기, 버퍼 배출 = 유효 바이트, 끝의 flush 는 따로), memset · 로그 append · RMW 히스토그램에서 4 조합의 표를 손계산과 시뮬레이터로 맞춘다.
- **쓰기 버퍼**(K 칸, 라인 단위 합치기)가 줄이는 것(트랜잭션 수, 스톨)과 새로 만드는 위험(읽기 미스가 버퍼 속 새 값을 지나치는 RAW, 순서, imprecise fault)을 설명하고 테스트로 잡는다.
- memset 에서 WB+WA 가 **곧 덮어쓸 데이터를 읽는 이유**(RFO)를 숫자로 보이고, 실제 코어가 피하는 방법(AArch64 `DC ZVA`, 쓰기 스트리밍 감지, non-temporal store)을 연결한다 — 이 맥(M6)에서 `DCZID_EL0` 와 0 채우기 속도를 실제로 잰다.
- 펌웨어 결정: DMA 가 읽는 로그 영역을 **MPU WT 영역**으로 둘지, **WB + clean**(ARCH-M12) 으로 둘지 비용으로 고른다.

## 1. 문제

"write-back 이 write-through 보다 항상 좋은가요? memset 은 왜 쓰기만 하는데 메모리 읽기가 생기죠?" — CPU/SoC·펌웨어 면접의 단골 질문이고, Cortex-M7 MPU 속성(WT / WB / WBWA)을 고르는 일, DMA 버퍼 정책, 실리콘 검증의 버스 트래픽 카운터 해석이 모두 이 질문 위에 있다. 답은 "트래픽을 **어떻게 세느냐**" 를 정확히 정의해야만 나온다.

이미 아는 것(다시 가르치지 않는다): tag/index/offset 분해와 direct-mapped 판정(ARCH-M04), N-way 와 LRU·3C(ARCH-M05) — 이 레슨은 그 시뮬레이터를 **4-way LRU 로 축소해 넣고** 읽기 경로는 주어진 코드로 둔다. AMAT 식(ARCH-B10), 버스 wait state(ARCH-B12), DMA clean/invalidate 순서(ARCH-M12), false sharing(ARCH-M08), MESI(ARCH-A04). 여기서 새로 만드는 것은 **쓰기 경로 전부** 와 쓰기 버퍼다.

```svg
<svg viewBox="0 0 740 345" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">store 하나가 가는 길 — 적중/미스 x 4 조합</text>
  <rect x="300" y="34" width="140" height="34" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="318" y="56" fill="currentColor">store (a, size)</text>
  <path d="M370 68 L370 88" stroke="currentColor" stroke-width="2" fill="none"/>
  <rect x="300" y="88" width="140" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="322" y="110" fill="currentColor">캐시에 있나?</text>
  <path d="M300 105 L190 105 L190 140" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="200" y="98" fill="currentColor" font-size="12">적중</text>
  <path d="M440 105 L550 105 L550 140" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="500" y="98" fill="currentColor" font-size="12">미스</text>
  <rect x="80" y="140" width="220" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="92" y="160" fill="currentColor">라인 데이터 갱신 + LRU</text>
  <text x="92" y="180" fill="currentColor" font-size="12">WB: dirty = 1 (DRAM 안 감)</text>
  <text x="92" y="198" fill="currentColor" font-size="12">WT: DRAM 에도 size 바이트</text>
  <rect x="440" y="140" width="220" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="458" y="162" fill="currentColor">write-allocate 인가?</text>
  <path d="M500 174 L430 230" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="420" y="205" fill="currentColor" font-size="12">WA</text>
  <path d="M610 174 L640 230" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="632" y="205" fill="currentColor" font-size="12">NWA</text>
  <rect x="320" y="230" width="230" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="332" y="250" fill="currentColor">교체(더티면 B 바이트 쓰기)</text>
  <text x="332" y="268" fill="currentColor">+ 채움 B 바이트 읽기 (RFO)</text>
  <text x="332" y="284" fill="currentColor" font-size="12">→ 그다음은 "적중" 과 같다</text>
  <rect x="570" y="230" width="160" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="580" y="250" fill="currentColor">캐시 안 건드림</text>
  <text x="580" y="268" fill="currentColor" font-size="12">DRAM 에 size 바이트</text>
  <text x="580" y="284" fill="currentColor" font-size="12">(쓰기 버퍼 경유)</text>
  <path d="M320 260 L300 260 L300 212" stroke="currentColor" stroke-width="2" fill="none" stroke-dasharray="5 4"/>
  <text x="10" y="316" fill="currentColor" font-size="12">읽기 미스는 정책과 무관하게 항상 할당(채움).</text>
  <text x="10" y="334" fill="currentColor" font-size="12">더티 비트는 라인당 1 개 → 교체·flush 는 라인 전체(B 바이트).</text>
</svg>
```

모델의 바이트 세는 규칙 (레슨 전체에서 고정, 소스 머리 주석에도 있음):

| 사건 | DRAM 읽기 | DRAM 쓰기 | 트랜잭션 |
|---|---|---|---|
| 라인 채움 (읽기 미스, WA 쓰기 미스) | B | — | 1 |
| 더티 라인 교체 (WB) | — | B (라인 전체) | 1 |
| 버퍼 없는(K=0) store-through: WT 쓰기, NWA 쓰기 미스 | — | store 크기 | 1, 그리고 `stalls++` |
| 쓰기 버퍼 칸 1 개 배출 | — | 칸의 유효 바이트 수(마스크 popcount) | 1 |
| `clean_range` 의 write-back | — | B | 1 |
| 실행 끝 dirty flush | — | `flush_bytes` 로 **따로** | — |

끝의 flush 를 따로 두는 이유: WB 는 쓰기를 미루기 때문에 "실행 중 쓰기" 만 보면 WB 가 부당하게 이긴다. 공정한 비교는 `wr_bytes + flush_bytes`. 실행 끝 버퍼 배출은 이미 CPU 가 "보낸" 쓰기라 `wr_bytes` 에 넣는다.

### Part A — 4 조합의 쓰기 경로

캐시 C·B·W 파라미터, LRU, 읽기 경로(`cache_read`, `allocate`)는 주어진다. 구현: `evict`(A1, 더티면 write-back), `cache_write`(A2, 4 조합), `cache_flush`(A3). 모든 실행은 데이터까지 들고 다녀서 **읽기 결과와 마지막 DRAM 이 골든 메모리와 같아야** 통과한다 — 바이트 수만 맞추고 데이터를 잃는 구현을 잡는다. 테스트: 손계산 트레이스 2 개 × 4 조합, "WT 에선 더티 라인이 생기지 않는다" 성질.

### Part B — 쓰기 버퍼: 합치기, 꽉 참 스톨, RAW

`wbuf_push`(B1): 같은 라인 칸이 있으면 합치고(coalescing on), 없으면 새 칸, 꽉 차면 스톨 + 가장 오래된 칸 배출. `wbuf_drain_line`(B2): 라인을 DRAM 에서 채우기 직전에 그 라인의 버퍼 쓰기를 먼저 내보낸다. DRAM 쓰기 포트는 `drain_period` 접근마다 한 칸(주어진 코드). 테스트: 합치기 on/off 의 트랜잭션·스톨, 같은 워드 10 번 쓰기, RAW 한 줄, 1 KB memset 의 스톨 수(정확히 93), 그리고 **결정적 퍼즈** 192 회(4 정책 × K{0,1,4} × 합치기 × 배출 × 라인{16,32} × WT 영역 유무).

### Part C — DMA 가 읽는 로그 링: WB + clean vs WT 영역

1 KB 로그 링(16 B 레코드 = 4 B store 4 개)을 UART DMA 가 주기적으로 읽어 간다. `clean_range`(C1, 부분 라인 포함, 끝에 버퍼 배출 = DSB)와 `log_publish`(C2, 링 wrap 이면 두 조각, MPU WT 영역이면 clean 없이 배출만). publish 직후 DMA 가 읽을 범위의 DRAM 이 골든과 같아야 한다(stale 0). publish 주기 1/4/16 레코드 × {WB+WA, WT 영역} 비용 표와 3 트레이스 × 4 조합 비교 표.

## 2. Problem (English)

> You are given a single-threaded model of a set-associative LRU data cache (capacity C, line B, ways W) in front of a byte-addressed DRAM, with a K-entry FIFO write buffer whose oldest entry drains every `drain_period` accesses. Trace records are `{uint32_t addr; char rw; uint8_t size}`, never straddling a line. A golden flat memory receives every store.
>
> 1. Implement the write path for all four policies {write-back, write-through} × {write-allocate, no-write-allocate}: `evict(set, way)` (write back a dirty victim, whole line), `cache_write(addr, size, data)` and `cache_flush()` (remaining dirty lines, counted separately from run-time writes).
> 2. Implement `wbuf_push` (optional per-line coalescing with a byte mask; a full buffer stalls and drains the oldest entry) and `wbuf_drain_line(line)` called before a line fill, so that a load never observes a value older than a buffered store.
> 3. Implement `clean_range(base, len)` (every overlapping line, then drain the buffer) and `log_publish(ring_base, ring_size, from, to)` for a byte ring read by a non-coherent DMA engine; a sub-range marked write-through by an MPU-like region needs no clean.
>
> Report DRAM read bytes, write bytes, transactions and stalls for memset (64 KB, 8-byte stores), a log-append trace and a read-modify-write histogram. Follow-ups: why does memset under WB+WA read 64 KB? How do real cores avoid it? When does write-through beat write-back? What can a coalescing write buffer break?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter       # exit 1
```

처음 실행 결과 (실제 출력 일부):

```text
FAIL starter.c:305  s.rd_bytes == exp1[p][0] && s.wr_bytes == exp1[p][1] && … && s.final_mismatch == 0
…
FAIL starter.c:344  s.wr_txn == 1 && s.wr_bytes == 32 && s.coalesced == 7 && s.stalls == 0 && s.final_mismatch == 0
…
log ring 1 KB, 512 records x 16 B, DMA reads after each publish   (C=4K B=32 W=4, K=4 coalesce, drain/4)
  region      every  stale  rd_B   wr_B  wr_txn clean_B evict_B flush_B cmo_lines
  WB+WA          1   8160 99520      0       0       0       0       0         0
…
FAIL starter.c:495  ok
2/40 passed
```

TODO 는 A1–A3, B1–B2, C1–C2 (7 개 함수). 위쪽 "주어진 코드" 는 고치지 않는다. 시작 코드의 `cache_write` 는 아무것도 하지 않으므로 store 가 통째로 사라진다 — 그래서 publish 때 DMA 가 검사한 8192 B 중 8160 B 가 stale 이다(32 B 는 초기값과 우연히 같음). 통과하는 2 개는 아무것도 안 해도 맞는 것들이다: clean 전의 전제 확인(`stale_bytes(0x500, 0x40) == 12`)과 "WT 영역 publish 는 clean 0 번".

모델의 단순화 (알고 보기):

| 모델 | 실제 칩 |
|---|---|
| 레코드 1 개 = 1 사이클, DRAM 쓰기 포트 = `drain_period` 사이클에 1 칸 | 지연·대역폭은 버스(ARCH-B12)와 DRAM 상태에 따라 다름 |
| 교체 write-back 은 즉시 DRAM 으로 | 보통 victim/eviction 버퍼를 거친다 |
| 읽기 미스는 같은 라인 버퍼 칸을 **오래된 것부터** 배출한 뒤 채움 | 버퍼에서 값을 전달(forwarding)하거나 병합하는 코어도 있다 |
| 더티 비트 라인당 1 개 | 섹터/바이트 dirty 를 두는 설계도 있다 (연습 3) |
| 4-way, 32 B 라인 | Cortex-M7 L1 D-cache 도 4-way · 32 B 라인 (크기 4–64 KB 구성) |

## 4. 힌트

<details><summary>힌트 1 — evict 와 flush 는 같은 일, 세는 곳만 다르다</summary>

둘 다 "유효하고 더티면 라인 전체를 DRAM 에". `evict` 는 실행 중 트래픽이므로 `dram_write`(wr_bytes, wr_txn), flush 는 `dram_raw_write` + `flush_bytes`. evict 는 라인을 무효로, flush 는 라인을 남기고 dirty 만 끈다.
</details>

<details><summary>힌트 2 — cache_write 는 "미스 처리 → 적중 처리" 두 단계</summary>

미스 + NWA 는 `mem_store` 하고 바로 return. 미스 + WA 는 `allocate(a)` 로 웨이를 받은 뒤 **적중 경로로 합류**한다. 적중 경로: 데이터 복사, LRU, 그리고 WB 면 dirty / WT 면 `mem_store`. 정책은 `g.cfg.wb` 가 아니라 `pol_wb(a)` 로 — WT 영역(Part C)이 주소마다 정책을 바꾼다.
</details>

<details><summary>힌트 3 — 합치기는 "라인 + 바이트 마스크"</summary>

칸 = `{line, mask, data[B]}`. 같은 라인 칸이 있으면 데이터만 덮고 `mask |= 1 << (off+i)`. 배출은 마스크 비트가 선 바이트만 쓴다(주어진 `wbuf_drain_oldest`). 꽉 찼을 때 순서: `stalls++` → 가장 오래된 칸 배출 → 맨 뒤에 새 칸.
</details>

<details><summary>힌트 4 — 읽기 미스가 버퍼를 지나치면?</summary>

NWA 쓰기 미스는 캐시에 없고 버퍼에만 있다. 바로 다음에 같은 주소를 읽으면 미스 → DRAM 에서 채움 → **옛 값**. 채우기 전에 그 라인 칸을 내보내야 한다. 그 칸만 쏙 빼지 말고 오래된 것부터 순서대로 — 다른 라인 쓰기와의 순서를 지키려고.
</details>

<details><summary>힌트 5 — 링 wrap</summary>

위치 `from..to` 는 누적 바이트. 링 오프셋 = `from % rsize`, 길이 = `to - from`(rsize 보다 크면 링 전체). `start + len > rsize` 면 `[start, rsize)` 와 `[0, 나머지)` 두 조각. WT 영역이면 clean 할 더티 라인이 없으므로 `wbuf_drain_all()` 만.
</details>

## 5. 풀이

### Part A — 쓰기 경로 세 함수

```c
static void cache_write(uint32_t a, uint32_t size, const uint8_t *data) {
    int w = lookup(a);
    if (w >= 0) g.st.hits++;
    else {
        g.st.misses++;
        if (!pol_wa(a)) { mem_store(a, size, data); return; }
        w = (int)allocate(a);                      /* write-allocate: 덮어쓸 라인도 먼저 읽는다 */
    }
    cline_t *L = &g.set[set_of(a)][w];
    L->lru = ++g.clock;
    memcpy(&L->data[a - L->line], data, size);
    if (pol_wb(a)) L->dirty = true;
    else mem_store(a, size, data);
}
```

`allocate` 가 채움(B 바이트 읽기)을 하는 이유는 데이터 정확성이다: store 는 라인의 일부(여기선 4·8 B)만 덮는데 라인은 한 단위로 유효해야 하므로 나머지 바이트를 DRAM 에서 가져와야 한다. 변형 표의 "WA 인데 fill 없이 할당" 은 32/40 으로 떨어진다 — 손계산 트레이스, 퍼즈, memset 표가 모두 데이터 불일치로 잡는다.

**손계산 트레이스 t1** (C=64, B=16, W=2 → 세트 2 개, 세트 = 주소 비트 4. 0x00·0x40·0x80 은 모두 세트 0):

| # | 접근 | WB+WA | WB+NWA = WT+NWA | WT+WA |
|---|---|---|---|---|
| 1 | W 0x00 4 | 미스 → 채움 16, dirty | 미스 → DRAM 4 | 미스 → 채움 16, DRAM 4 |
| 2 | W 0x04 4 | 적중 | 미스 → DRAM 4 | 적중, DRAM 4 |
| 3 | R 0x08 4 | 적중 | 미스 → 채움 16 | 적중 |
| 4 | W 0x40 4 | 미스 → 채움 16 (웨이 1) | 미스 → DRAM 4 | 미스 → 채움 16, DRAM 4 |
| 5 | W 0x80 4 | 미스: LRU = 0x00 (더티) → **쓰기 16** + 채움 16 | 미스 → DRAM 4 | 미스: 0x00 교체(깨끗) + 채움 16, DRAM 4 |
| 6 | R 0x00 4 | 미스: LRU = 0x40 (더티) → **쓰기 16** + 채움 16 | 적중 | 미스: 0x40 교체 + 채움 16 |
| 끝 | flush | 0x80 더티 → 16 | — | — |
| 합 | rd / wr(txn) / flush / 적중:미스 | 64 / 32(2) / 16 / 2:4 | 16 / 16(4) / 0 / 1:5 | 64 / 16(4) / 0 / 2:4 |

두 가지가 보인다. (1) NWA 두 조합이 이 트레이스에서 같다 — WB+NWA 와 WT+NWA 는 **이미 캐시에 있는 라인에 쓸 때만** 갈린다. 쓰기 미스로는 라인이 들어오지 않으니까. (2) 6 번에서 WB+NWA 는 3 번의 읽기가 데려온 0x00 라인을 적중한다. 쓰기만 하고 다시 읽는(init 후 사용) 코드에선 NWA 가 읽기 미스를 한 번 더 낸다.

**카운터 RMW 10 번**(`R 0x00; W 0x00` x10): WB 두 조합 = 읽기 16, 실행 중 쓰기 0, flush 16. WT 두 조합 = 읽기 16, 쓰기 40 (트랜잭션 10). WB 의 진짜 장점은 "같은 라인에 여러 번 쓰는 것을 한 번의 write-back 으로 합친다" 는 것이다. 그리고 WT 에서는 더티 라인이 아예 생기지 않으므로 교체 write-back 0, flush 0 — 테스트 `s.evict_bytes == 0 && s.flush_bytes == 0` 가 그 성질이다.

### memset 이 64 KB 를 읽는 이유 (RFO)

비교 표의 memset 줄 (실제 출력, C=4 KB B=32 W=4, K=4 합치기, 4 사이클에 1 칸 배출):

```text
  trace         policy   rd_B    wr_B(run) flush_B  total_wr  wr_txn  stalls  hit%
  memset 64K    WB+WA    65536     61440     4096     65536    1920       0   75.0
  memset 64K    WB+NWA       0     65536        0     65536    2048       0    0.0
  memset 64K    WT+WA    65536     65536        0     65536    2048       0   75.0
  memset 64K    WT+NWA       0     65536        0     65536    2048       0    0.0
```

WB+WA: 2048 라인 각각의 첫 8 B store 가 미스 → 32 B 채움 → **64 KB 를 읽는데 그 바이트는 전부 곧 덮인다**. 이것이 RFO(read for ownership) 비용이다(멀티코어에서는 소유권 획득까지 겹친다 — ARCH-A04). 쓰기는 1920 라인 교체(61440) + 마지막 128 라인 flush(4096) = 64 KB. 적중률 75 % 는 라인당 4 store 중 3 개가 적중이라서다 — **적중률이 높아도 트래픽은 2 배**(읽기 64 + 쓰기 64)다. NWA 두 조합은 읽기 0, 쓰기 64 KB. 합치기 버퍼가 8 B store 4 개를 32 B 한 칸으로 묶어서 2048 트랜잭션이 된다. WT+WA 는 최악: 읽기 64 KB **그리고** store 마다 store-through.

실제 코어가 RFO 를 피하는 방법:

| 방법 | 원리 | 비고 |
|---|---|---|
| AArch64 `DC ZVA` | 블록(크기 = `DCZID_EL0.BS`) 전체를 0 으로 — 읽을 필요 없이 할당 | 0 채우기만. 연습 1: memset 64 KB 읽기 65536 → **0**. Linux arm64 의 `clear_page` 가 이걸 쓰는 것으로 알려짐(소스 미확인) |
| 쓰기 스트리밍 감지 | 연속 라인 전체 쓰기가 몇 번 이어지면 write-allocate 를 잠시 끔 | Cortex-A53/A7x 계열 TRM 의 "read allocate mode", Cortex-M7 의 동적 read-allocate 모드와 그걸 끄는 ACTLR 비트가 이것으로 안다 — 임계값·비트 위치 미확인. 연습 2: 읽기 96 B (첫 3 라인) |
| non-temporal store (`STNP`, x86 `MOVNT*`) | "다시 안 읽음" 힌트 | Arm 에선 힌트일 뿐, 동작은 구현 정의 |
| x86 `rep stosb` (ERMSB) | 마이크로코드가 라인 전체 쓰기를 알아봄 | 알려진 동작, 세부 미확인 |

**이 맥에서 실측** — `demo_bench.c` (128 MiB, 7 회 중앙값, Apple clang 21.0.0, 코어 종류 고정 불가 — macOS 는 pin 이 안 됨, 측정 중 `uptime` load average 22.66–30.70 으로 **바쁜 기계**였다: 다른 빌드가 돌고 있었음):

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -O2 -Wall -Wextra demo_bench.c -o demo_bench && ./demo_bench
```

```text
DCZID_EL0 = 0x4  -> BS=4, ZVA block = 64 B, DZP(금지)=0
dc zva on one block: exactly that block zeroed
128 MiB buffer, median of 7 runs
  kernel                    ms     GB/s(written or read)   ns per 128 B line
  stp xzr (64 B/iter)       1.60       83.9                 1.525
  stnp xzr (non-temp.)      1.36       98.8                 1.295
  dc zva                    1.05      128.1                 0.999
  libc memset(0)            1.07      125.1                 1.023
  read sum (reference)      2.20       61.1                 2.095
  str 8 B per 128 B         3.04        2.8                 2.901
```

네 번 돌렸다(앞 세 번은 0 검사 표본을 넣기 전 빌드, 위는 네 번째). 네 번 모두 같았던 것: `dc zva`(0.99–1.10 ns/라인)가 `stp`/`stnp`(1.20–1.53)보다 빠르고, 둘 다 읽기(2.03–4.33)보다 빠르며, **라인마다 8 B 만 쓰기(2.88–4.55)가 읽기보다도 느리다**. libc memset 은 세 번은 `dc zva` 와 같았고(1.00–1.07) 부하가 가장 높던 첫 실행에서만 2.27 이었다. 읽는 법 (해석은 추정):

- `DCZID_EL0.BS = 4` → 4·2^4 = **64 B 블록**인데 `hw.cachelinesize` 는 128 B 다. ZVA 블록과 캐시 라인은 같은 값이 아니어도 된다 — 블록 크기는 반드시 레지스터에서 읽는다.
- 라인당 8 B 만 쓰는 루프는 쓴 바이트가 1/16 인데 라인당 시간은 **읽기보다도 길다**. 부분 라인 쓰기는 나머지 120 B 를 어디선가 가져와야 한다 — 시뮬레이터의 WA 채움과 같은 모양이다.
- 전체 라인을 `stp` 로 쓰는 루프는 라인당 읽기보다 빠르다. 이 코어가 전체 라인 쓰기에서 채움을 생략하는 것과 맞는 결과지만, Apple 은 이 동작을 문서화하지 않았다 — 측정에서 나온 추정이다. libc memset 이 `dc zva` 와 같은 속도인 것도 큰 0 채우기에 ZVA 를 쓴다는 것과 맞는 결과일 뿐, libc 코드는 확인하지 않았다.
- 128 MiB 에 1 ms 대(≈ 100 GB/s 이상)라는 절대값은 이 칩의 메모리 계층(SLC 크기 등 미공개)에 달려 있어 해석하지 않는다. 상대 비교만 쓴다.

### Part B — 쓰기 버퍼

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">쓰기 버퍼 K=4, 라인 단위 합치기 — 무엇을 줄이고 무엇을 지켜야 하나</text>
  <rect x="20" y="50" width="100" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="38" y="70" fill="currentColor">CPU store</text>
  <text x="30" y="87" fill="currentColor" font-size="12">WT / NWA 미스</text>
  <path d="M120 72 L168 72" stroke="currentColor" stroke-width="2" fill="none"/>
  <rect x="170" y="40" width="400" height="128" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="180" y="58" fill="currentColor" font-size="12">wbuf[0] = 가장 오래됨 … wbuf[3]</text>
  <rect x="180" y="66" width="74" height="40" class="f-ok" stroke="currentColor"/>
  <text x="184" y="84" fill="currentColor" font-size="12" font-family="monospace">0x400</text>
  <text x="184" y="100" fill="currentColor" font-size="12" font-family="monospace">mask FF..</text>
  <rect x="258" y="66" width="74" height="40" class="f-ok" stroke="currentColor"/>
  <text x="262" y="84" fill="currentColor" font-size="12" font-family="monospace">0x420</text>
  <text x="262" y="100" fill="currentColor" font-size="12" font-family="monospace">mask 00FF</text>
  <rect x="336" y="66" width="74" height="40" class="f-warn" stroke="currentColor"/>
  <text x="340" y="84" fill="currentColor" font-size="12" font-family="monospace">0x300</text>
  <text x="340" y="100" fill="currentColor" font-size="12" font-family="monospace">mask 0F</text>
  <rect x="414" y="66" width="74" height="40" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="430" y="90" fill="currentColor" font-size="12">빈 칸</text>
  <text x="180" y="128" fill="currentColor" font-size="12">같은 라인 store → 그 칸에 합침 (트랜잭션 · 같은 바이트 재쓰기 감소)</text>
  <text x="180" y="146" fill="currentColor" font-size="12">새 라인 + 꽉 참 → 스톨, wbuf[0] 강제 배출</text>
  <text x="180" y="162" fill="currentColor" font-size="12">drain_period 마다 wbuf[0] 이 배경 배출</text>
  <path d="M570 86 L598 86" stroke="currentColor" stroke-width="2" fill="none"/>
  <rect x="600" y="60" width="130" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="645" y="84" fill="currentColor">DRAM</text>
  <text x="610" y="104" fill="currentColor" font-size="12">마스크 바이트만 씀</text>
  <rect x="20" y="200" width="200" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="34" y="220" fill="currentColor">load 0x300 미스</text>
  <text x="34" y="237" fill="currentColor" font-size="12">→ 0x300 라인 채움 필요</text>
  <path d="M220 222 L242 222" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="250" y="210" fill="currentColor" font-size="12">RAW 검사: 0x300 라인 칸(색칠한 칸)이 버퍼에 있나?</text>
  <text x="250" y="230" fill="currentColor" font-size="12">있으면 wbuf[0] 부터 0x300 칸까지 순서대로 배출 → 그다음 채움</text>
  <text x="250" y="250" fill="currentColor" font-size="12">안 하면 DRAM 의 옛 값을 채워 읽는다 (퍼즈가 잡음)</text>
  <text x="20" y="285" fill="currentColor" font-size="12">버퍼가 없는(K=0) WT 는 store 마다 CPU 가 DRAM 쓰기를 기다린다 → 모델에선 store 마다 stalls++</text>
</svg>
```

```c
static void wbuf_push(uint32_t a, uint32_t size, const uint8_t *data) {
    uint32_t l = line_of(a), off = a - l;
    wbe_t *e = NULL;
    if (g.cfg.coalesce)
        for (unsigned i = 0; i < g.wn; i++)
            if (g.wbuf[i].line == l) { e = &g.wbuf[i]; g.st.coalesced++; break; }
    if (e == NULL) {
        if (g.wn == g.cfg.K) { g.st.stalls++; wbuf_drain_oldest(); }
        e = &g.wbuf[g.wn++];
        e->line = l;
        e->mask = 0;
    }
    for (uint32_t i = 0; i < size; i++) {
        e->data[off + i] = data[i];
        e->mask |= 1ull << (off + i);
    }
}
```

Part B 테스트의 숫자 (WT+NWA, B=32, K=4):

| 상황 | 합치기 on | 합치기 off |
|---|---|---|
| 한 라인에 4 B store 8 개, 배경 배출 없음 | 트랜잭션 1, 32 B, 합침 7, 스톨 0 | 트랜잭션 8, 32 B, **스톨 4** (5~8 번째가 꽉 찬 버퍼를 만남) |
| 같은 워드에 4 B store 10 번 | 트랜잭션 1, **4 B** (합침 9) | (안 돌림 — 40 B, 10 트랜잭션이 될 것) |
| 1 KB memset 8 B store, 4 사이클에 1 칸 배출 | 트랜잭션 32, 스톨 **0** | 트랜잭션 128, 스톨 **93** |

스톨 93 의 손계산: 합치기 off 면 store 마다 새 칸, 사이클 4·8·12… 끝에 한 칸씩 나간다. 사이클 1–4 는 칸이 1→4 개로 차고(4 번째 끝에 배출 → 3), 5 번째에 다시 4 개. 6·7·8 번째는 꽉 찬 버퍼 → 스톨. 이후 4 사이클마다 "1 개 들어가고 3 개 스톨" 이 반복된다: 128 store = 첫 4 + 31 묶음 × 3 스톨 = **93**. 합치기 on 이면 8 B store 4 개가 한 라인 칸으로 합쳐져 칸 도착 속도(4 사이클에 1)가 배출 속도와 같아지므로 스톨 0. 즉 쓰기 버퍼가 줄이는 것은 바이트가 아니라 **트랜잭션 수와 CPU 대기**다 (같은 바이트를 다시 쓰는 경우만 바이트도 준다 — 히스토그램 WT 가 16384 이 아니라 16172 인 이유).

**RAW (B2).** 테스트 `W 0x300 4; W 0x308 2; R 0x300 8` (WT+NWA): 두 store 는 캐시에 없고 버퍼 한 칸에 있다. 읽기 미스가 채우기 전에 그 칸을 내보내야 `read_mismatch == 0`. B2 를 지운 변형은 35/40 — 이 테스트와 4 개 퍼즈가 전부 실패한다. 실제 코어는 load 주소를 store/write 버퍼와 비교해서 값을 전달(forwarding)하거나 배출을 기다린다.

**정직하게 적는 한계.** B2 를 "그 칸만 순서 건너뛰어 배출" 로 바꾼 변형도 **40/40** 이다. 단일 관찰자 모델은 마지막 데이터만 보기 때문에 다른 라인 쓰기와의 **순서**가 바뀐 것을 볼 수 없다. 합치기도 마찬가지로 순서를 바꾼다: `W A1; W B; W A2` 에서 A2 가 오래된 A 칸에 합쳐지면 DRAM 에는 A2 가 B 보다 먼저 닿는다. Arm Normal 메모리(약한 순서)에서는 허용되고, x86 의 TSO 에서는 일반 메모리 store 가 프로그램 순서로 보여야 하므로 이런 합치기를 못 한다(WC 메모리 타입은 예외). Arm Device 메모리 속성의 **G**(Gathering, 합치기 허용)와 **E**(Early write acknowledgement, 버퍼가 먼저 "완료" 응답)가 정확히 이 레슨의 두 기능이다 — `Device-nGnRnE` 는 둘 다 금지. 순서 검증은 ARCH-A05(litmus)의 영역이다.

### Part C — DMA 로그 링

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">로그 링 1 KB (32 라인) — 레코드 16 B, publish 마다 DMA 가 DRAM 에서 읽음</text>
  <rect x="20" y="40" width="700" height="36" class="f-mut" stroke="currentColor"/>
  <rect x="20" y="40" width="44" height="36" class="f-acc" stroke="currentColor"/>
  <text x="24" y="63" fill="currentColor" font-size="12">0..15</text>
  <rect x="654" y="40" width="66" height="36" class="f-acc" stroke="currentColor"/>
  <text x="660" y="63" fill="currentColor" font-size="12">1000..1023</text>
  <text x="200" y="63" fill="currentColor" font-size="12">publish(1000, 1040) = 링 끝 24 B + 링 처음 16 B → 두 조각, 라인 2 개</text>
  <rect x="20" y="100" width="340" height="120" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="32" y="122" fill="currentColor" font-weight="bold">WB+WA 영역 (기본 SRAM)</text>
  <text x="32" y="142" fill="currentColor" font-size="12">store 는 캐시에만 → publish = clean_range</text>
  <text x="32" y="160" fill="currentColor" font-size="12">반쯤 찬 라인을 매번 clean → 같은 라인 2 번 write-back</text>
  <text x="32" y="178" fill="currentColor" font-size="12">첫 store 마다 RFO 채움 (+ 캐시 오염)</text>
  <text x="32" y="196" fill="currentColor" font-size="12">clean 빠뜨리면 stale (ARCH-M12 의 TX 버그)</text>
  <rect x="380" y="100" width="340" height="120" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="392" y="122" fill="currentColor" font-weight="bold">MPU WT+NWA 영역</text>
  <text x="392" y="142" fill="currentColor" font-size="12">store 는 쓰기 버퍼 → DRAM, 라인은 더티가 안 됨</text>
  <text x="392" y="160" fill="currentColor" font-size="12">publish = 버퍼 배출(DSB) 만, clean 0 번</text>
  <text x="392" y="178" fill="currentColor" font-size="12">쓴 바이트만 나감, RFO 없음</text>
  <text x="392" y="196" fill="currentColor" font-size="12">대가: 로그가 아닌 데이터엔 쓰면 안 됨 (RMW 표)</text>
</svg>
```

```c
static void log_publish(uint32_t rb, uint32_t rsize, uint32_t from, uint32_t to) {
    uint32_t len = to - from, start = from % rsize;
    if (len == 0) return;
    if (len > rsize) { start = 0; len = rsize; }
    uint32_t first = len < rsize - start ? len : rsize - start;
    uint32_t piece_base[2] = { rb + start, rb }, piece_len[2] = { first, len - first };
    for (unsigned k = 0; k < 2; k++) {
        if (piece_len[k] == 0) continue;
        if (in_wt(piece_base[k])) wbuf_drain_all();
        else clean_range(piece_base[k], piece_len[k]);
    }
}
```

`clean_range` 끝의 `wbuf_drain_all()` 은 CMSIS `SCB_CleanDCache_by_Addr` 의 끝 `__DSB()` 자리다. 테스트 하나가 일부러 이것을 본다: WT 영역 주소에 store 하면 캐시가 아니라 **버퍼**에 있고(`g.wn == 1`, stale 4), `clean_range` 는 더티 라인을 하나도 못 찾지만(clean_bytes 0) 마지막 배출 덕분에 stale 0 이 된다. 이 테스트를 넣기 전에는 "끝의 배출 삭제" 변형이 40/40 으로 살아남았다 — 기본 WB 영역에서는 버퍼가 비어 있었기 때문이다.

실제 출력 (512 레코드, 앱이 레코드마다 16 KB 표를 무작위로 8 번 읽음):

```text
log ring 1 KB, 512 records x 16 B, DMA reads after each publish   (C=4K B=32 W=4, K=4 coalesce, drain/4)
  region      every  stale  rd_B   wr_B  wr_txn clean_B evict_B flush_B cmo_lines
  WB+WA          1      0 109792  16384     512   16384       0       0       512
  WB+WA          4      0 109792   8192     256    8192       0       0       256
  WB+WA         16      0 109792   8192     256    6912    1280       0       256
  WT region      1      0 99520   8192     512       0       0       0         0
  WT region      4      0 99520   8192     512       0       0       0         0
  WT region     16      0 99520   8192     512       0       0       0         0
```

- **매 레코드 publish + WB**: 16 B 레코드가 32 B 라인의 반이므로, 같은 라인을 반쯤 찼을 때 한 번, 다 찼을 때 또 한 번 clean → 쓰기 16384 = 로그 8 KB 의 **2 배**(write amplification). 4 레코드마다 publish 하면 라인이 다 찬 뒤에만 clean → 8192. 16 레코드마다면 그 사이 40 라인이 자연 교체(evict 1280)되고 나머지를 clean.
- **WT 영역**: 주기와 무관하게 8192 B, 512 트랜잭션(레코드 하나 = 4 store 가 한 칸으로 합쳐짐 → 16 B 씩). clean 명령 0 번. 읽기도 10272 B 적다.
- 읽기 차이 10272 B 의 내역: 계측 버전(임시 복사본에 채움 카운터를 단 것)으로 확인하니 **로그 라인 RFO 256 번(8192 B) + 표 읽기 미스 65 번 증가(2080 B)** 다. 로그 라인이 캐시 4 KB 중 일부를 차지해서 표의 적중이 떨어진 것(캐시 오염)이다.

### 3 트레이스 × 4 조합 — 전체 표

```text
C=4096 B=32 W=4 LRU, write buffer K=4 coalescing, drain 1 entry / 4 accesses
  trace         policy   rd_B    wr_B(run) flush_B  total_wr  wr_txn  stalls  hit%
  log 512x16B   WB+WA   109792      7936      256      8192     248       0   44.2
  log 512x16B   WB+NWA   99520      8192        0      8192     512       0   16.0
  log 512x16B   WT+WA   109792      8192        0      8192     512       0   44.2
  log 512x16B   WT+NWA   99520      8192        0      8192     512       0   16.0
  hist RMW      WB+WA    17408         0     1024      1024       0       0   95.6
  hist RMW      WB+NWA   17408         0     1024      1024       0       0   95.6
  hist RMW      WT+WA    17408     16172        0     16172    3738     663   95.6
  hist RMW      WT+NWA   17408     16172        0     16172    3738     663   95.6
```

(memset 4 줄은 위에.) 결론을 트레이스별로:

| 트레이스 | 승자 | 이유 (숫자) |
|---|---|---|
| memset (한 번 쓰고 안 읽음) | NWA (WB든 WT든) — 또는 WB+WA + ZVA/스트리밍 감지 | WA 는 64 KB RFO 읽기 |
| 로그 append (한 번 쓰고 DMA 가 읽음) | WT+NWA 영역 | 쓰기 바이트는 4 조합이 모두 8192 로 같다. 차이는 RFO + 오염(읽기 +10 KB), publish 때 clean 비용 |
| RMW 히스토그램 (같은 워드를 반복 갱신) | WB | 총 쓰기 1024 (hist 1 KB flush) vs WT 16172 — **15.8 배**. WT 는 store 3 사이클에 1 개 > 배출 4 사이클에 1 칸이라 스톨 663 |

일반 SRAM 은 WB+WA 가 기본값인 이유가 세 번째 줄이다: 실제 프로그램의 쓰기 대부분은 스택·지역 변수·자료구조처럼 **같은 라인을 여러 번** 쓴다. WT 가 이기는 것은 "한 번 쓰고 다른 관찰자(DMA, 디버거, 다른 마스터)가 읽는" 데이터다.

### 펌웨어 연결 — Cortex-M7 MPU 로 영역마다 정책 고르기

ARMv7-M MPU 의 TEX/C/B 인코딩으로 Normal 메모리의 캐시 정책을 영역별로 고른다(ARMv7-M ARM 의 메모리 속성 표 기준): `TEX=000 C=1 B=0` = write-through, no write-allocate / `TEX=000 C=1 B=1` = write-back, no write-allocate / `TEX=001 C=1 B=1` = write-back, write-allocate. 영역 설정 자체(크기 2^k, 겹침 우선순위)는 ARCH-M15. 코어가 각 속성을 실제로 어떻게 구현하는지(예: Cortex-M7 이 WT 를 어떻게 다루는지)는 TRM·AN4839 를 확인해야 한다 — 이번에 원문 확인 안 함. 이 레슨의 `wt_base/wt_limit` 이 그 영역 하나를 흉내 낸다.

### 변형(mutation) 표

`solution.c` 에서 한 군데씩 바꿔 돌린 결과 (임시 스크립트 `/tmp/esn_ARCH-M06/mut/mut.py`, 문자열 치환 → 빌드 → 실행):

| 변형 | 결과 | 잡은 테스트 |
|---|---|---|
| A1 교체 write-back 삭제 | 34/40 | 손계산 t1, 퍼즈(데이터 소실), memset 표, Part C stale |
| A2 WA 인데 채움 없이 할당 | 32/40 | 손계산 t1, 퍼즈, memset RFO 검사, Part C clean 테스트 |
| A2 WT 적중에서 `mem_store` 빠뜨림 | 29/40 | t1, RMW, "WT 는 더티 없음", 퍼즈, 비교 표 |
| A2 NWA 미스를 그냥 버림 | 21/40 | 거의 전부 |
| A3 flush 를 `wr_bytes` 로 셈 | 36/40 | t1/RMW 의 flush 기대값, 히스토그램 표 |
| B1 꽉 차면 배출 없이 덮어씀 | 32/40 | 스톨 4·93 테스트, 퍼즈, 비교 표 |
| B2 삭제 (RAW 검사 없음) | 35/40 | RAW 한 줄 + 퍼즈 4 개 |
| B2 순서 건너뛰고 그 칸만 배출 | **40/40** | 못 잡음 — 단일 관찰자 모델의 한계 (위) |
| C1 첫 라인만 clean | 35/40 | 부분 라인 clean, 링 전체 publish(32 라인), Part C stale |
| C1 끝의 배출 삭제 | 39/40 | WT 영역 `clean_range` 테스트 |
| C2 wrap 무시 | 39/40 | wrap publish |
| C2 WT 영역도 `clean_range` | 39/40 | `cmo_lines == 0` (정확하지만 비쌈) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 40/40 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san   # 40/40, 보고 없음
```

단일 스레드 모델이라 TSan 은 해당 없다. 실행 시간 0.04 초(퍼즈 192 회 포함).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| WB 와 WT 를 `wr_bytes` 만으로 비교 | WB 는 쓰기를 미룬다. 실행 끝 더티 라인을 빼면 WB 가 공짜처럼 보임 (RMW: 실행 중 0 B) | `wr_bytes + flush_bytes` 로 비교, flush 는 따로 보고 |
| write-allocate 미스에서 채움 생략 | store 가 덮지 않은 바이트가 쓰레기 → 데이터 오류 (변형 32/40) | `allocate()` 로 채운 뒤 적중 경로. 채움 생략은 **라인 전체**를 쓸 때만(ZVA, 연습 1) |
| "적중률이 높으니 트래픽도 적다" | memset WB+WA 적중률 75 % 인데 DRAM 트래픽은 읽기 64 KB + 쓰기 64 KB | 적중률이 아니라 바이트·트랜잭션을 센다 |
| 쓰기 버퍼가 바이트를 줄인다고 설명 | 줄이는 것은 트랜잭션과 스톨. 바이트는 같은 바이트 재쓰기만 준다 (hist 16384 → 16172) | 합치기 = 라인 + 마스크, 효과는 트랜잭션 표로 |
| 읽기 미스가 버퍼를 안 봄 | NWA/WT store 가 버퍼에 있는 동안 같은 라인을 채우면 옛 값 | 채우기 전 `wbuf_drain_line` (또는 forwarding) |
| 합치기를 아무 메모리에나 | 다른 라인 store 와의 순서가 바뀐다 — 장치 레지스터면 버그 | Device-nG(no Gathering) 속성, x86 TSO 는 WB 메모리에서 합치지 않음 |
| WT 영역이니 DMA 전에 아무것도 안 함 | store 가 아직 쓰기 버퍼에 있다 (테스트: `g.wn == 1`, stale 4) | DSB(모델: `wbuf_drain_all`) — clean 은 필요 없음 |
| 로그를 레코드마다 WB clean | 반쯤 찬 라인을 반복 write-back → 2 배 쓰기 (16384 vs 8192) | publish 를 라인 단위로 묶거나 WT 영역 / 섹터 dirty (연습 3) |
| WT 를 범용 SRAM 에 | 같은 워드를 반복 갱신하는 코드에서 15.8 배 쓰기 + 스톨 663 | 기본은 WB+WA, WT 는 공유·관찰되는 작은 영역만 |

## 8. 면접 Q&A

<details><summary>Q1. write-back 과 write-through 의 차이와, 각각 언제 쓰나요?</summary>

WB 는 store 를 캐시에만 쓰고 dirty 를 세운 뒤 교체·clean 때 라인 전체를 내린다. WT 는 store 마다 메모리에도 쓰고 라인은 언제나 깨끗하다. 같은 라인을 여러 번 쓰는 일반 코드는 WB 가 압도적이다(히스토그램 모델: 총 쓰기 1024 B vs 16172 B). WT 가 이기는 곳은 한 번 쓰고 **다른 관찰자**가 읽는 데이터 — DMA 가 가져갈 로그(모델에서 clean 0 번, RFO 없음), 디버거·다른 코어가 들여다보는 공유 상태. 꼬리: "WT 의 신뢰성 장점은?" → 캐시 라인이 늘 깨끗하므로 캐시 SRAM 에 패리티/ECC 오류가 나도 invalidate 후 메모리에서 다시 읽으면 복구된다. 더티 라인의 오류는 복구할 원본이 없다. 그리고 리셋·전원 차단 직전의 로그가 RAM 에 남는다(크래시 덤프). 다만 구체 코어의 오류 처리 방식은 TRM 을 봐야 한다.
</details>

<details><summary>Q2. memset 은 쓰기만 하는데 왜 메모리 읽기 트래픽이 생기죠? 어떻게 없애나요?</summary>

write-allocate 캐시는 쓰기 미스에서 라인을 먼저 채운다(RFO). store 가 라인 일부만 덮으니 나머지를 가져와야 라인이 유효해진다. memset 처럼 결국 라인 전체를 덮는 경우엔 그 읽기가 낭비다 — 모델: 64 KB memset 에 읽기 65536 B, 적중률 75 % 인데도. 없애는 법: (1) AArch64 `DC ZVA` 로 블록 단위 0 채우기(연습 1: 읽기 0), (2) 코어의 쓰기 스트리밍 감지(연속 전체 라인 쓰기면 할당 생략, 연습 2: 읽기 96 B), (3) non-temporal store 힌트, (4) 그 영역을 NWA 로. 꼬리: "DC ZVA 블록 크기는?" → `DCZID_EL0.BS`(log2 워드 수)에서 읽는다. 이 맥은 BS=4 → 64 B 로, `hw.cachelinesize` 128 B 와 다르다(실측). `DZP=1` 이면 사용 금지. 꼬리 2: "실제로 차이가 나요?" → M6 실측에서 라인마다 8 B 만 쓰는 루프가 라인당 2.9 ns 로, 전체 라인 0 쓰기(1.0–1.5 ns)보다 느리고 순수 읽기(2.1 ns)보다도 느렸다. 원인은 추정.
</details>

<details><summary>Q3. 쓰기 버퍼는 무엇을 해결하고 어떤 문제를 새로 만드나요?</summary>

해결: store 가 메모리 쓰기 완료를 기다리지 않게(K=0 이면 store 마다 대기 — 모델에서 store 수만큼 스톨), 같은 라인 store 를 합쳐 트랜잭션을 줄임(1 KB memset: 128 → 32, 스톨 93 → 0). 새 문제: (1) RAW — 버퍼 속 값보다 오래된 메모리를 읽지 않도록 load 가 버퍼를 검사해야 한다(B2), (2) 순서 — 합치기·배출 순서가 다른 관찰자에게 보이는 store 순서를 바꾼다, (3) 오류 보고 — 쓰기가 나중에 실패하면 이미 다음 명령이 실행된 뒤라 PC 가 정확하지 않다. 꼬리: "Cortex-M 에서 (3) 이 어떻게 보이나요?" → BusFault 가 IMPRECISERR 로 기록되고 BFAR 이 유효하지 않다(ARCH-B11). Cortex-M3/M4 는 ACTLR 의 DISDEFWBUF 로 기본 메모리 맵 접근의 쓰기 버퍼를 끄면 precise 로 바뀌어 디버깅이 쉬워진다(비트 위치는 TRM 확인 필요). 성능을 잃으니 디버깅용이다.
</details>

<details><summary>Q4. 합치는(coalescing) 쓰기 버퍼가 메모리 순서 모델과 충돌하는 지점은요?</summary>

`W A1; W B; W A2` 에서 A2 를 오래된 A 칸에 합치면 메모리에 A2 가 B 보다 먼저 닿는다. 단일 코어의 최종 값은 같아서 이 레슨의 모델은 이것을 못 본다(순서를 건너뛴 변형도 40/40). Arm 의 Normal 메모리는 약한 순서라 허용된다 — 순서가 필요하면 프로그래머가 DMB/DSB 를 넣는다(ARM-M05, C-A02). x86 TSO 는 일반(WB) 메모리 store 가 프로그램 순서로 보여야 해서 이런 합치기를 하지 않고, 합치기는 WC 메모리 타입의 write-combining 버퍼에서 한다. 꼬리: "장치 레지스터는?" → Arm Device 메모리 속성의 G(Gathering)가 합치기 허용 여부, E(Early write acknowledgement)가 버퍼의 조기 완료 응답 허용 여부다. `Device-nGnRnE` 는 둘 다 금지 — FIFO 데이터 레지스터에 두 번 쓴 것을 한 번으로 합치면 안 되니까.
</details>

<details><summary>Q5. 더티 비트를 라인당 하나 두는 것과 섹터·바이트별로 두는 것의 트레이드오프는?</summary>

라인당 하나면 태그 SRAM 이 작고 단순하지만 write-back 은 항상 라인 전체다. 로그를 레코드(16 B)마다 clean 하면 32 B 라인을 두 번 내려 2 배가 됐다(16384 B). 섹터(8 B) dirty 4 개를 두면 더티 섹터만 내려 8192 B(연습 3). 대가는 라인마다 비트 3 개 더 + write-back 경로의 부분 쓰기 지원. 꼬리: "부분 쓰기는 DRAM 에서 공짜인가요?" → 아니다. DRAM 은 버스트 단위로 움직이고, ECC 가 붙은 메모리 컨트롤러는 ECC 워드보다 작은 쓰기를 내부 read-modify-write 로 처리하는 경우가 많다. 그래서 바이트 수가 줄어도 시간은 비례해 줄지 않는다 — 이 모델은 바이트와 트랜잭션만 센다.
</details>

<details><summary>Q6. UART DMA 가 주기적으로 가져가는 로그 버퍼, 캐시 정책을 어떻게 잡겠어요?</summary>

세 선택지. (a) WB+WA + publish 때 `SCB_CleanDCache_by_Addr`(ARCH-M12 의 TX 규칙: clean → DSB → 시작). 모델 비용: 매 레코드 publish 면 쓰기 2 배(16384 B), 레코드 첫 store 마다 RFO + 캐시 오염(읽기 +10272 B). 라인 단위로 묶어 publish 하면 8192 B. (b) 로그 영역만 MPU WT(+NWA) — clean 0 번, 쓰기 8192 B, 읽기 오염 없음, publish 는 DSB 하나. (c) non-cacheable — 가장 단순하지만 CPU 가 로그를 다시 읽거나 포맷팅하면 매 접근이 버스 왕복. 나는 (b) 를 기본으로 하고, 링 크기를 라인 배수·라인 정렬로(ARCH-M12 Part B) 잡는다. 꼬리: "WT 면 DSB 도 필요 없나요?" → 필요하다. store 가 쓰기 버퍼에 남아 있을 수 있다(모델: `g.wn == 1` 인 상태에서 stale 4 B). DMA 를 켜는 레지스터 쓰기 전에 배리어. 꼬리 2: "RX 버퍼도 WT 로?" → RX 는 CPU 가 쓰지 않으니 WT 의 이득이 없고 invalidate 는 여전히 필요하다(ARCH-M12 연습 4).
</details>

<details><summary>Q7. 화이트보드: 4 KB 캐시(32 B 라인)에 64 KB memset(8 B store)을 하면 4 조합의 DRAM 읽기·쓰기는? 버퍼 없음.</summary>

N = 65536, s = 8, B = 32, 캐시에 남는 라인 = 4096/32 = 128. WB+WA: 읽기 N(라인마다 채움), 쓰기 = 교체 (2048 − 128) × 32 = 61440 + flush 128 × 32 = 4096 → 합 N, 실행 중 트랜잭션 1920. WB+NWA·WT+NWA: 읽기 0, 쓰기 N, 트랜잭션 N/s = 8192. WT+WA: 읽기 N, 쓰기 N, 트랜잭션 8192. 연습 4 의 `predict_memset` 이 이 식이고 36 가지 (N, s, 정책) 에서 시뮬레이터와 일치했다. 꼬리: "버퍼 4 칸 + 라인 합치기면?" → NWA 트랜잭션이 N/B = 2048 로 준다(비교 표에서 확인).
</details>

## 9. 추가 연습

`practice.c` 는 정답 모델을 그대로 넣고 아래 확장을 더한 것이다 (`'Z'` 레코드, `stream_T`, `sector` 설정).

### 연습 1 — `DC ZVA` 레코드

트레이스에 `'Z'`(그 주소가 든 라인 전체를 0 으로) 를 추가하라. WB 영역에서는 미스여도 **채우지 않고** 할당해 0 으로 만들고 dirty, WT 영역에서는 0 라인을 메모리로(버퍼 경유). 64 KB 를 `'Z'` 2048 개로 지울 때 WB+WA 의 읽기·쓰기는?

<details><summary>답</summary>

`allocate` 를 "교체 + 버퍼 배출 + 할당" (`allocate_nofill`)과 "채움" 으로 쪼개서 ZVA 는 앞부분만 쓴다. 실제 출력: `str x8 rd=65536 wr+flush=65536 | DC ZVA rd=0 wr+flush=65536`. 쓰기는 그대로 64 KB 다 — RFO 만 사라진다. 채움 생략이 안전한 이유는 라인 **전체**가 정의되기 때문이고, 부분 쓰기에 이 짓을 하면 Part A 변형("채움 없이 할당")처럼 데이터가 깨진다. 섞인 트레이스(부분 store 후 ZVA 후 읽기, WT 영역 ZVA 후 읽기)도 골든과 같다. 실제 `DC ZVA` 는 블록 크기가 `DCZID_EL0` 에 있고 캐시 라인과 다를 수 있다(이 맥: 64 B vs 128 B) — 모델은 블록 = 라인으로 단순화했다.
</details>

### 연습 2 — 쓰기 스트리밍 감지기

WB+WA 에서 쓰기 미스가 "직전 쓰기 미스 라인 + B" 로 이어지면 카운터 +1(같은 라인이면 유지, 아니면 1 부터, **읽기 미스면 0**). 카운터가 T 를 넘으면 그 쓰기 미스는 할당하지 않고 `mem_store`. T = 3 일 때 memset 64 KB 의 읽기는? 히스토그램 RMW 에는 영향이 있나?

<details><summary>답</summary>

실제 출력: `rd=96 wr=65440 flush=96 wr_txn=2045 noalloc_stores=8180`. 처음 3 라인(카운터 1, 2, 3)만 할당해서 96 B 를 읽고, 4 번째 라인부터는 할당하지 않는다(8192 − 12 = 8180 store). 합치기 버퍼가 나머지를 라인 단위 트랜잭션 2045 개로 묶는다. RMW 는 읽기 미스가 계속 카운터를 0 으로 되돌리므로 `noalloc == 0`, 쓰기·flush 가 감지기 없을 때와 같다. 무작위 퍼즈 6 개(순차 쓰기와 무작위 읽기·쓰기 섞음)에서도 데이터는 골든과 같다. 실제 코어의 감지 조건·임계값은 구현마다 다르고 레지스터로 끌 수 있게 해 둔 경우가 많다 — 세부 미확인.
</details>

### 연습 3 — 섹터 dirty 비트

라인(32 B)을 8 B 섹터 4 개로 나누고 섹터별 dirty 를 두어, 교체·clean·flush 는 더티 섹터만 쓰게 하라. Part C 의 "매 레코드 publish" 시나리오에서 clean 바이트는?

<details><summary>답</summary>

`mark_dirty(L, off, size)` 가 `off/sec .. (off+size-1)/sec` 섹터 비트를 세우고, `write_back` 이 비트가 선 섹터만 복사한다. 실제 출력: `clean bytes line-dirty=16384 sector-dirty=8192 (log data 8192)` — 2 배 증폭이 사라진다. 무작위 크기(1/2/4/8 B) 쓰기 퍼즈에서도 데이터 손실 없음. 트랜잭션은 그대로 라인당 1 개로 셌다(실제 버스에선 바이트 스트로브 `WSTRB` 로 부분 쓰기를 표현 — SOCX-B02).
</details>

### 연습 4 — memset 닫힌 식

버퍼 없음(K=0), 순차, N 은 B 의 배수일 때 `predict_memset(N, s, wb, wa, C, B)` 가 읽기, 실행 중 쓰기, flush, 쓰기 트랜잭션을 돌려주게 하고 N ∈ {512, 4096, 65536} × s ∈ {4, 8, 16} × 4 정책 = 36 경우를 시뮬레이터와 비교하라.

<details><summary>답</summary>

`lines = N/B`, `resident = min(N, C)/B` (순차 채움 + LRU 면 마지막 C 바이트가 남는다). WA 면 읽기 N. WB+WA: 실행 중 쓰기 `(lines − resident)·B`, 트랜잭션 `lines − resident`, flush `resident·B`. 나머지 세 조합: 쓰기 N, 트랜잭션 N/s, flush 0. 36 경우 모두 일치(N = 512 면 resident = 16 = lines 라서 실행 중 쓰기 0, 전부 flush). 면접 Q7 의 화이트보드 답이 이것이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san
```

실제 출력 (두 빌드 같음, 새니타이저 보고 없음):

```text
ex1 memset 64K WB+WA:  str x8  rd=65536 wr+flush=65536   |  DC ZVA  rd=0 wr+flush=65536
ex2 memset 64K WB+WA + stream(T=3): rd=96 wr=65440 flush=96 wr_txn=2045 noalloc_stores=8180
ex3 log publish every record: clean bytes  line-dirty=16384  sector-dirty=8192  (log data 8192)
9/9 passed
```

## 10. 핵심 정리

- 트래픽은 정의부터: 채움 = B 읽기, 더티 교체 = B 쓰기(라인 전체), store-through = store 크기, 버퍼 배출 = 유효 바이트, 끝의 flush 는 따로. WB 비교는 `wr + flush` 로.
- 적중 = 캐시 갱신 + (WB: dirty / WT: 메모리에도). 미스 = WA: 교체 + **채움** 후 적중처럼 / NWA: 메모리로만. 읽기 미스는 항상 할당.
- WB 는 같은 라인 반복 쓰기를 한 번으로 합친다(히스토그램 1024 B vs WT 16172 B). WT 는 한 번 쓰고 남이 읽는 데이터에 이긴다(로그: clean 0, RFO 0).
- memset + WB+WA = 곧 덮을 데이터를 읽는 RFO(64 KB 읽기, 적중률 75 % 와 무관). 해법: `DC ZVA`(블록 크기는 `DCZID_EL0`, M6 = 64 B), 쓰기 스트리밍 감지, non-temporal store, NWA.
- 쓰기 버퍼는 바이트가 아니라 **트랜잭션과 대기**를 줄인다(1 KB memset 128 → 32 트랜잭션, 스톨 93 → 0). 대가: load 가 버퍼를 검사해야 하고(RAW), 순서가 바뀌고(Arm Device G/E 속성, x86 TSO), 쓰기 오류가 imprecise 가 된다.
- DMA 가 읽는 영역: WB 면 clean → DSB, WT 면 DSB 만. 라인보다 작은 단위로 자주 clean 하면 write-back 이 증폭된다(16 B 레코드 → 2 배).

## 참고자료

- John L. Hennessy, David A. Patterson, *Computer Architecture: A Quantitative Approach* — 메모리 계층 장(부록 B 포함): write-through/write-back, write-allocate/no-write-allocate, write buffer
- Norman P. Jouppi, "Cache Write Policies and Performance", ISCA 1993 — write-allocate/fetch-on-write 조합과 write buffer 의 고전 분석
- Arm, *Arm Architecture Reference Manual for A-profile* (DDI 0487) — `DC ZVA`, `DCZID_EL0`(BS, DZP), Device 메모리 속성 nGnRnE/nGnRE/nGRE/GRE, Normal 메모리 cacheability (절 번호 미확인): <https://developer.arm.com/documentation/ddi0487/latest/>
- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403) — MPU TEX/C/B 메모리 속성 인코딩, BusFault IMPRECISERR
- Arm, *Cortex-M7 Technical Reference Manual* (DDI 0489) — L1 D-cache(4-way, 32 B 라인), store buffer, ACTLR 의 할당 관련 비트 (이번에 원문 확인 안 함)
- Arm, *Cortex-M4 Technical Reference Manual* — ACTLR.DISDEFWBUF (쓰기 버퍼 끄기 → precise BusFault) (비트 위치 원문 확인 안 함)
- STMicroelectronics, AN4839 *Level 1 cache on STM32F7 Series and STM32H7 Series* — MPU 속성별 캐시 동작, DMA 와의 일관성 (원문 확인 안 함)
- Linux kernel `arch/arm64/lib/clear_page.S` — 페이지 0 채우기에 `dc zva` 를 쓰는 것으로 알려짐 (소스 미확인)
- 이 맥에서 직접 확인: `sysctl hw.cachelinesize` = 128, `hw.perflevel0.l1dcachesize` = 131072, `DCZID_EL0` = 0x4 (demo_bench.c)
- 관련 레슨: ARCH-M04 (direct-mapped 시뮬레이터, tag/index/offset), ARCH-M05 (N-way, 교체 정책, 3C), ARCH-M07 (루프 순서·타일링 실측), ARCH-M12 (DMA clean/invalidate — WB 영역 publish 의 근거), ARCH-M08 (라인 공유), ARCH-A04 (MESI — 멀티코어 RFO), ARCH-A05 (메모리 순서 litmus), ARCH-B10 (AMAT), ARCH-B11 (BusFault, IMPRECISERR), ARCH-B12 (버스 wait state), ARCH-M15 (MPU 영역), ARM-M05 (DMB/DSB), C-A02 (배리어와 DMA), SOCX-B02 (WSTRB 부분 쓰기); 다음 단계: MEM-M06 (RFO 대역폭 실측), MEM-M03 (다단 캐시), MEM-A08 (roofline)
