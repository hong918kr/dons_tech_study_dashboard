---
id: ARCH-M12
time: 90분
updated: 2026-11-08
---
# ARCH-M12 DMA와 캐시 일관성: Clean/Invalidate 시나리오

## 학습 목표

- write-back · write-allocate D-cache(32 B 라인) + 쓰기 버퍼 + RAM 을 직접 읽고 쓰는 DMA 엔진을 C 로 모델링하고, **골든 모델**(캐시가 없었다면의 메모리)과 비교해 stale 데이터 버그를 매번 똑같이 재현한다.
- 네 가지 버그를 이름과 순서로 말할 수 있다: TX 전 clean 누락, RX 후 invalidate 누락, **DMA 도중 더티 라인 교체(eviction)** 가 DMA 데이터를 덮음(그래서 RX 는 시작 **전에도** 처리), **라인을 같이 쓰는 옆 변수** 손상(어떤 유지보수 순서로도 못 막음 → 정렬 + 크기 올림).
- 올바른 순서를 코드로 쓴다: TX = clean → DSB → 시작, RX = (clean 또는 clean+invalidate) → 시작 → 완료 → invalidate → 읽기. 디스크립터 `own` 비트와 도어벨 앞의 DSB 까지.
- 실제 API 에 연결한다: CMSIS `SCB_CleanDCache_by_Addr` 계열(소스 확인: V1.0.3 은 시작 오프셋을 더하고 옛 V5.00 은 안 더함), Cortex-M7 `DCCMVAC = 0xE000EF68` 의 실제 thumbv7em asm, Linux `dma_map_single(dir)` / `dma_sync_single_for_cpu/device` 가 arm64·arm32 에서 실제로 하는 일(소스 확인), `dma_alloc_coherent`, MPU non-cacheable 영역과의 비용 비교.

## 1. 문제

"DMA 로 받은 데이터가 가끔 이전 패킷이에요" / "옆 변수 값이 가끔 사라져요" — Cortex-M7(STM32F7/H7, i.MX RT), Cortex-A 의 비일관(non-coherent) DMA 마스터, SoC 검증의 DMA 테스트에서 가장 흔하고 가장 찾기 어려운 버그다. 면접에서는 "TX/RX 전후에 clean 과 invalidate 를 **어떤 순서로** 하나요? 왜 RX 는 시작 전에도 하나요? 버퍼는 왜 32 B 정렬에 32 B 배수여야 하나요?" 가 꼬리질문으로 이어진다.

동시성 가이드 7층 레슨이다. 3~5층의 배리어·원자 연산은 **CPU 끼리**의 이야기였다. DMA 엔진은 다른 종류의 관찰자다: CPU 의 캐시를 보지 않고(일관성 포트가 없는 경우) RAM 만 본다. 그래서 `volatile`(C-B09)도 `atomic`(C-A03)도 이 문제를 풀지 못한다 — 둘 다 "CPU 가 load/store 명령을 낸다 / 순서를 지킨다" 까지만 보장하고, 그 명령이 **캐시에 적중하는 것** 은 막지 않는다. 해결책은 캐시 유지보수(cache maintenance) 명령: **clean**(더티 라인을 RAM 에 써 내림, 라인은 남음), **invalidate**(라인을 버림, 더티면 데이터도 버려짐), **clean+invalidate**(둘 다).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">모델: CPU 는 캐시를 통해, DMA 는 RAM 을 직접 (캐시·쓰기 버퍼를 보지 않음)</text>
  <rect x="20" y="40" width="110" height="50" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="50" y="62" fill="currentColor">CPU</text>
  <text x="30" y="80" fill="currentColor" font-size="12">ld/st (volatile 도)</text>
  <rect x="180" y="35" width="200" height="110" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="190" y="55" fill="currentColor">D-cache 32 B 라인 x 16</text>
  <text x="190" y="75" fill="currentColor" font-size="12">write-back, write-allocate</text>
  <text x="190" y="95" fill="currentColor" font-size="12">라인 = valid · dirty · data</text>
  <text x="190" y="115" fill="currentColor" font-size="12">hw_evict(): 아무 때나 교체</text>
  <text x="190" y="133" fill="currentColor" font-size="12">hw_prefetch(): 추측성 채움</text>
  <rect x="180" y="165" width="200" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="190" y="190" fill="currentColor" font-size="12">쓰기 버퍼 (dsb() 가 비움)</text>
  <rect x="440" y="80" width="130" height="130" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="470" y="105" fill="currentColor">RAM</text>
  <text x="450" y="125" fill="currentColor" font-size="12">0x000–0xBFF</text>
  <text x="450" y="142" fill="currentColor" font-size="12">캐시 가능</text>
  <text x="450" y="162" fill="currentColor" font-size="12">0xC00–0xFFF</text>
  <text x="450" y="179" fill="currentColor" font-size="12">non-cacheable</text>
  <text x="450" y="196" fill="currentColor" font-size="12">(디스크립터)</text>
  <rect x="620" y="110" width="100" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="635" y="135" fill="currentColor">DMA 엔진</text>
  <text x="630" y="155" fill="currentColor" font-size="12">ETH MAC, SDMMC</text>
  <path d="M130 65 L178 65" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M380 90 L438 120" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M380 185 L438 170" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M572 145 L618 145" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="20" y="235" fill="currentColor" font-weight="bold">버그 1 TX</text>
  <text x="110" y="235" fill="currentColor" font-size="12">CPU 가 쓴 패킷이 캐시에만(더티) → DMA 는 RAM 의 옛 값을 보냄 → clean 필요</text>
  <text x="20" y="255" fill="currentColor" font-weight="bold">버그 2 RX</text>
  <text x="110" y="255" fill="currentColor" font-size="12">DMA 가 RAM 에 새 값 → CPU 는 캐시의 옛 라인 적중 → 완료 후 invalidate 필요</text>
  <text x="20" y="275" fill="currentColor" font-weight="bold">버그 3 RX</text>
  <text x="110" y="275" fill="currentColor" font-size="12">DMA 도중 더티 라인이 교체되며 RAM 에 write-back → DMA 데이터를 덮음 → 시작 전에도 처리</text>
  <text x="20" y="295" fill="currentColor" font-weight="bold">버그 4</text>
  <text x="110" y="295" fill="currentColor" font-size="12">버퍼 끝 라인을 옆 변수가 같이 씀 → invalidate 는 옆 변수를, clean 은 DMA 데이터를 잃음</text>
</svg>
```

이 레슨의 모델은 실제 칩이 아니라 **실제 칩의 규칙을 결정적으로 재현하는 장난감** 이다. 하드웨어가 "아무 때나" 하는 일(교체, 추측성 채움)은 훅 함수로 바꾸고, 테스트가 가장 나쁜 순간에 그 훅을 부른다. 그러면 실제 보드에서 "가끔" 나는 버그가 매번 난다. 검사는 전부 골든 모델 `g_gold[]` 과의 비교다: CPU 쓰기와 DMA 쓰기가 둘 다 `g_gold` 를 갱신하므로, `g_gold` 는 "캐시가 없었다면 그 주소에 있어야 할 값" 이다.

### Part A — 범위 유지보수와 TX/RX 드라이버 순서

`cache_clean_range` / `cache_invalidate_range` / `cache_clean_invalidate_range` (CMSIS `SCB_*DCache_by_Addr` 모양: 범위에 걸친 모든 라인, 부분 라인 포함, 끝에 DSB) 를 라인 한 줄짜리 명령(`dc_clean_line` = DCCMVAC 등) 위에 만든다. 그 위에 `drv_tx_submit`, `drv_rx_arm`(시작 전), `drv_rx_complete`(완료 후). 시나리오 4개: TX, 예전에 읽어 둔 RX 버퍼, memset(0) 해 둔 RX 버퍼 + DMA 도중 교체, DMA 도중 추측성 프리페치.

### Part B — 라인 정렬 규칙

`dma_round_up(len)`(라인 배수로 올림)과 `dma_buf_isolated(addr, len)`(다른 데이터와 라인을 같이 쓰지 않나). 40 B RX 버퍼의 끝 라인을 옆 카운터가 같이 쓰는 배치에서 (1) 시작 전 invalidate 가 카운터를 지우는 것, (2) DMA 도중 카운터를 쓰면 **완료 후 어떤 명령을 골라도** 둘 중 하나가 손상되는 것을 확인하고, 배치를 고쳐서 해결한다.

### Part C — STM32H7 이더넷 같은 RX 링 + TX

디스크립터 `{buf, len, own}` 는 MPU non-cacheable 영역, 버퍼는 캐시 가능 RAM(Ethernet DMA 드라이버의 흔한 구성). `eth_rx_poll`(own 확인 → invalidate → 복사 → 유지보수 → own=1 → DSB)과 `eth_tx_send`(복사 → clean → 디스크립터 → **DSB** → 도어벨). 부팅 때 C 런타임의 0 초기화가 버퍼에 더티 라인을 남기고, MAC 이 쓰기 직전 프리페치, 쓴 직후 교체가 일어나는 "나쁜 날" 에 12 프레임 에코를 돌린다. 마지막으로 캐시 가능 + 유지보수 vs non-cacheable 버퍼의 **버스 트랜잭션 수** 를 센다(결정적인 비용 표).

## 2. Problem (English)

> You are given a single-threaded model of a Cortex-M7-like system: RAM, a write-back/write-allocate D-cache with 32-byte lines, a write buffer drained by `dsb()`, and a DMA engine that reads and writes RAM directly (it never sees the cache or the write buffer). Hooks `hw_evict(addr)` and `hw_prefetch(addr)` model the cache evicting a line or speculatively filling one at an arbitrary time. A golden memory records what a coherent system would hold.
>
> 1. Implement `cache_clean_range`, `cache_invalidate_range`, `cache_clean_invalidate_range(addr, len)` on top of per-line operations: every line overlapping `[addr, addr+len)` exactly once, nothing for `len == 0`, `dsb()` at the end.
> 2. Implement the driver hooks `drv_tx_submit(buf, len)`, `drv_rx_arm(buf, len)` (before the transfer: no dirty line may remain in the range, and a neighbour's dirty data in a shared edge line must not be lost) and `drv_rx_complete(buf, len)`.
> 3. Implement `dma_round_up(len)` and `dma_buf_isolated(addr, len)`; show a buffer layout where a neighbouring variable survives a concurrent CPU update during the DMA.
> 4. Implement `eth_rx_poll(out, cap)` (returns length, `-1` if the next descriptor is still DMA-owned, `-2` if the frame does not fit — buffer still recycled) and `eth_tx_send(frame, len)` for a 4-entry RX ring and one TX descriptor in non-cacheable memory, with a doorbell that reads the descriptor immediately.
>
> Every scenario is checked against the golden memory. Follow-ups: why must RX be maintained *before* the transfer as well as after? Why does `volatile` not help? What does Linux do for `DMA_FROM_DEVICE` on arm64? When would you put buffers in a non-cacheable MPU region instead?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter       # 0.01 초, exit 1
./starter -v                                                                 # 시나리오별 이벤트 추적
```

처음 실행 결과 (실제 출력 일부):

```text
FAIL starter.c:314  g_st.cmo == 2u
…
FAIL starter.c:325  g_tx_stale == 0u && memcmp(g_dev_tx, pkt, 64) == 0
FAIL starter.c:334  memcmp(got, pkt, 64) == 0 && view_mismatch(0x300, 64) == 0u
FAIL starter.c:346  memcmp(got, pkt, 64) == 0
FAIL starter.c:347  memcmp(&g_ram[0x300], pkt, 64) == 0
FAIL starter.c:366  memcmp(got, pkt, 64) == 0
FAIL starter.c:372  dma_round_up(0) == 0u && dma_round_up(1) == 32u && dma_round_up(32) == 32u
…
FAIL starter.c:432  g_mac_drops == 0u && eth_rx_poll(got, sizeof got) == 30 && memcmp(got, f, 30) == 0
cost (N=8 frames x 64 B, read twice):  line fills  NC reads  CMO lines  DSB
  cacheable + maintenance                      2         0          0    0
  MPU non-cacheable                            0       256          0    0
FAIL starter.c:459  sum_c == sum_n
FAIL starter.c:460  c.fills == 2u * N && n.fills == 0u && n.nc_rd == 32u * N && n.cmo == 0u
9/31 passed
```

TODO 는 A1–A6, B1–B2, C1–C2. 하드웨어 모델(위쪽 절반)은 고치지 않는다. 시작 코드는 "유지보수를 하나도 안 하는 드라이버" 라서, 통과하는 9개는 모델의 성질을 확인하는 테스트(예: "완료 후 invalidate 만으로는 교체 버그를 못 살린다")와 우연히 맞는 것들이다.

모델의 단순화 (알고 보기):

| 모델 | 실제 칩 |
|---|---|
| 교체 write-back 은 RAM 으로 바로 | 교체도 쓰기 버퍼·인터커넥트를 지난다 (타이밍만 다름) |
| clean 의 write-back, non-cacheable 쓰기 = 쓰기 버퍼에 머묾 → `dsb()` 가 비움 | Cortex-M7 AXI 쓰기는 posted. 유지보수 **완료** 는 DSB 로 기다린다 (ARM-M05) |
| CPU 의 미스 처리는 쓰기 버퍼를 먼저 비움 | 하드웨어가 자기 쓰기와의 위험(hazard)을 처리 — CPU 는 자기 쓰기를 본다 |
| 도어벨(장치 레지스터 쓰기)은 즉시 디스크립터를 읽음 | Device 메모리 쓰기와 앞선 Normal 쓰기의 순서는 아키텍처 규칙 + 배리어 문제 (C-A02 Q5) |
| 8 세트 x 2 웨이 LRU, 512 B | STM32H7 의 M7 D-cache 16 KB (Cortex-M7 D-cache 는 4-way 로 알려짐 — 대표값, 미확인) |

## 4. 힌트

<details><summary>힌트 1 — 범위를 라인으로</summary>

첫 라인은 `addr & ~31`, 마지막 라인은 `addr + len - 1` 이 든 라인. `for (a = lbase(addr); a < addr + len; a += 32)` 이면 부분 라인 양 끝이 다 들어간다. `(0x11F, 2)` 는 0x100, 0x120 두 줄, `(0x10C, 53)` 은 세 줄. `len == 0` 이면 아무것도 하지 말 것 — `addr + 0` 비교로는 루프가 안 돌지만, DSB 도 내지 말자.
</details>

<details><summary>힌트 2 — 왜 끝에 dsb() 인가</summary>

clean 은 더티 라인을 "RAM 으로 보내기 시작" 할 뿐이다. 모델에서 그 데이터는 쓰기 버퍼에 있고, DMA 는 쓰기 버퍼를 보지 않는다. DSB 가 앞선 유지보수와 쓰기가 끝날 때까지 기다린다. CMSIS 의 `SCB_CleanDCache_by_Addr` 도 루프 앞뒤에 `__DSB()` 가 있다.
</details>

<details><summary>힌트 3 — RX 를 시작하기 전에 왜 무엇을?</summary>

버퍼에 더티 라인이 있으면(받기 전에 memset, 이전에 CPU 가 버퍼를 썼음, 부팅 때 .bss 0 초기화) 그 라인은 DMA 도중 **언제든** 교체될 수 있고, 교체는 write-back 이다 → DMA 가 쓴 데이터 위에 옛 값이 덮인다. 완료 후 invalidate 는 이미 망가진 RAM 을 못 고친다. 그래서 시작 전에 "더티 0개" 상태를 만든다. 방법은 둘: invalidate(더티를 버림) 또는 clean(더티를 미리 RAM 에). 가장자리 라인에 옆 변수가 있다면 어느 쪽이 안전할까?
</details>

<details><summary>힌트 4 — 완료 후 invalidate 가 또 필요한 이유</summary>

시작 전 처리로 라인을 비웠어도, DMA 도중 CPU(또는 프리페처·추측 실행)가 그 주소를 건드리면 라인이 **DMA 이전의 RAM 값으로** 다시 채워진다. 완료 후 읽기 전에 한 번 더 버린다.
</details>

<details><summary>힌트 5 — 디스크립터와 도어벨</summary>

디스크립터는 non-cacheable 이라 clean 이 필요 없지만, 쓰기는 쓰기 버퍼에 머문다. 도어벨이 디스크립터를 즉시 읽는다면 그 전에 DSB. RX 재장전도 같다: 버퍼 유지보수 → own=1 → DSB. own=1 을 먼저 하면 그 순간부터 DMA 가 버퍼를 쓸 수 있다.
</details>

## 5. 풀이

### Part A — 한 줄 명령 세 개와 범위 함수

| 명령 (Cortex-M7, CMSIS 이름) | SCB 레지스터 (core_cm7.h V5.2.0) | 라인이 깨끗 | 라인이 더티 | 라인 없음 |
|---|---|---|---|---|
| clean by MVA to PoC — `DCCMVAC` | offset 0x268 → **0xE000EF68** | 그대로 | RAM 에 써 내림, 라인은 남음(깨끗) | 아무 일 없음 |
| invalidate by MVA to PoC — `DCIMVAC` | 0x25C → 0xE000EF5C | 버림 | **버림 — 더티 데이터 소실** | 아무 일 없음 |
| clean+invalidate — `DCCIMVAC` | 0x270 → 0xE000EF70 | 버림 | 써 내린 뒤 버림 | 아무 일 없음 |

(SCB_BASE = SCS_BASE 0xE000E000 + 0x0D00 = 0xE000ED00. PoC = point of coherency, DMA 를 포함한 모든 관찰자가 같은 값을 보는 지점 — Cortex-M7 에선 사실상 메인 메모리.)

범위 함수는 세 개가 루프 모양이 같고 한 줄 명령만 다르다:

```c
static void cache_clean_range(uint32_t addr, uint32_t len) {
    if (len == 0) return;
    for (uint32_t a = lbase(addr); a < addr + len; a += LINE) dc_clean_line(a);
    dsb();
}
```

CMSIS 의 실제 코드 (`CMSIS/Core/Include/cachel1_armv7.h` V1.0.3, 2023-03-17, GitHub `ARM-software/CMSIS_5` develop 브랜치에서 받아 확인):

```c
       int32_t op_size = dsize + (((uint32_t)addr) & (__SCB_DCACHE_LINE_SIZE - 1U));
      uint32_t op_addr = (uint32_t)addr /* & ~(__SCB_DCACHE_LINE_SIZE - 1U) */;
      __DSB();
      do {
        SCB->DCCMVAC = op_addr;             /* register accepts only 32byte aligned values, only bits 31..5 are valid */
        op_addr += __SCB_DCACHE_LINE_SIZE;
        op_size -= __SCB_DCACHE_LINE_SIZE;
      } while ( op_size > 0 );
      __DSB();
      __ISB();
```

시작 주소의 라인 안 오프셋을 크기에 더하므로 부분 라인 양 끝을 다 덮는다 — 우리 루프와 같은 라인 집합이다. **옛 버전은 다르다**: CMSIS_5 태그 5.0.0 의 `core_cm7.h` V5.00 은 `op_size = dsize` 로 시작해서, 정렬 안 된 시작 주소면 **마지막 라인을 빠뜨린다** (같은 방법으로 소스를 받아 확인). 연습 3 에서 그 차이로 16 B 가 낡은 채 남는 것을 재현한다. 문서 주석에 "addr (aligned to 32-byte boundary)" 라고 적힌 이유가 이것이다. 새 버전에서도 정렬을 지켜야 하는 이유는 API 때문이 아니라 Part B 의 옆 변수 문제 때문이다.

### 실제로 나온 명령 — `demo_m7_asm.c`

<!-- code: demo_m7_asm.c -->

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -mthumb -O2 -S demo_m7_asm.c -o -
```

`clean_dcache_by_addr` 의 실제 출력 (지시어 줄 제거):

```text
clean_dcache_by_addr:                   @ @clean_dcache_by_addr
	cmp	r1, #1
	it	lt
	bxlt	lr
.LBB0_1:
	and	r3, r0, #31
	movw	r12, #61288
	@APP
	dsb	sy
	@NO_APP
	movt	r12, #57344
	add	r1, r3
.LBB0_2:                                @ =>This Inner Loop Header: Depth=1
	cmp	r1, #33
	str.w	r0, [r12]
	blo	.LBB0_6
	subs	r1, #65
	adds	r0, #32
	…                                   (4 번 펼친 루프, 같은 str.w r?, [r12])
	str.w	r3, [r12]
	blo	.LBB0_2
.LBB0_6:
	@APP
	dsb	sy
	@NO_APP
	@APP
	isb	sy
	@NO_APP
	bx	lr
```

읽는 법: `movw r12, #61288` (0xEF68) + `movt r12, #57344` (0xE000) = **0xE000EF68 = DCCMVAC**. 루프 본체는 `str.w r0, [r12]` — 캐시 유지보수가 "특별한 명령" 이 아니라 **System Control Space 레지스터에 주소를 쓰는 store** 라는 게 M-profile 의 특징이다 (A-profile 은 `dc cvac, x0` 같은 시스템 명령). `and r3, r0, #31` + `add r1, r3` 가 CMSIS 의 "오프셋을 크기에 더하기" 다. `dsb 0xF` 는 `dsb sy` 로 출력된다. `rx_complete_invalidate` 는 같은 모양으로 `movw r2, #61276` (0xEF5C = DCIMVAC) 이 나왔다. `uart_dma_tx_start` 에서는 clean 루프와 `dsb sy; isb sy` 가 끝난 **뒤에** DMA 레지스터 store (`movt r2, #16386` → 0x40020000, `str r0, [r2]` … `str r0, [r2, #8]`) 가 나온다: asm 의 `"memory"` clobber 가 컴파일러 재배치를 막고, DSB 가 하드웨어 쪽 완료를 기다린다.

### Part A — TX: clean → (DSB) → 시작

```text
-- A-TX (시작 코드 -v)                      -- A-TX (정답 -v)
    · DMA TX: RAM 0x300..+64 읽음                · clean 0x300 (쓰기 버퍼로)
FAIL … g_tx_stale == 0u && …                     · clean 0x320 (쓰기 버퍼로)
                                                 · DMA TX: RAM 0x300..+64 읽음
```

(위는 두 번의 실제 `-v` 출력을 나란히 놓은 것이다.) 패킷은 write-allocate 때문에 캐시에만 있고 RAM 은 0 이다. clean 만 하고 DSB 를 빼면? 정답에서 `dsb()` 한 줄을 지운 변형을 돌리면 TX 테스트가 실패한다(아래 변형 표) — write-back 이 쓰기 버퍼에 있는 동안 DMA 가 RAM 을 읽었기 때문이다.

### Part A — RX: 시작 전과 완료 후, 두 번

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">RX 한 번의 시간축 — 하드웨어가 끼어들 수 있는 두 구간</text>
  <path d="M30 120 L720 120" stroke="currentColor" stroke-width="2" fill="none"/>
  <rect x="40" y="95" width="120" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="50" y="115" fill="currentColor">① 시작 전</text>
  <text x="50" y="135" fill="currentColor" font-size="12">clean(+inval)</text>
  <rect x="170" y="95" width="70" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="180" y="125" fill="currentColor" font-size="12">arm/own</text>
  <rect x="250" y="95" width="250" height="50" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="300" y="115" fill="currentColor">DMA 가 RAM 에 쓰는 중</text>
  <text x="270" y="135" fill="currentColor" font-size="12">CPU 는 이 버퍼를 건드리지 않는다</text>
  <rect x="510" y="95" width="70" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="520" y="125" fill="currentColor" font-size="12">완료 IRQ</text>
  <rect x="590" y="95" width="130" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="600" y="115" fill="currentColor">② 완료 후</text>
  <text x="600" y="135" fill="currentColor" font-size="12">invalidate → 읽기</text>
  <text x="250" y="175" fill="currentColor" font-weight="bold">위험 A: 더티 라인 교체</text>
  <text x="250" y="193" fill="currentColor" font-size="12">write-back 이 DMA 데이터를 덮는다 (RAM 손상)</text>
  <text x="250" y="211" fill="currentColor" font-size="12">→ ② 로는 못 고침. ① 에서 더티 0 개로</text>
  <text x="250" y="240" fill="currentColor" font-weight="bold">위험 B: 프리페치·추측 읽기</text>
  <text x="250" y="258" fill="currentColor" font-size="12">DMA 이전 RAM 값으로 라인 재할당 → ② 가 버린다</text>
  <path d="M300 160 L300 147" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M450 228 L450 147" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="40" y="60" fill="currentColor" font-size="12">① 이 막는 것: 위험 A (그리고 옆 변수 손실 — clean 계열일 때)</text>
  <text x="40" y="78" fill="currentColor" font-size="12">② 가 막는 것: 위험 B, 그리고 ① 이전부터 캐시에 있던 깨끗한 옛 라인</text>
</svg>
```

시나리오 A-RX2 의 실제 추적 (`./starter -v`):

```text
-- A-RX2: 버퍼를 memset(0) 해 두고 DMA 시작, DMA 도중 더티 라인이 교체됨
    · DMA RX: RAM 0x300..+32 씀
    · 교체(훅): 더티 라인 0x300 write-back → RAM  ← DMA 데이터 위에 덮어씀?
    · DMA RX: RAM 0x320..+32 씀
    · 교체(훅): 더티 라인 0x320 write-back → RAM  ← DMA 데이터 위에 덮어씀?
FAIL starter.c:346  memcmp(got, pkt, 64) == 0
FAIL starter.c:347  memcmp(&g_ram[0x300], pkt, 64) == 0
```

두 번째 FAIL 이 핵심이다: **RAM 자체** 가 0 으로 돌아갔다. 같은 상황에서 시작 전 처리를 건너뛰고 완료 후 invalidate 만 하면(A-RX2', 정답 `-v`):

```text
-- A-RX2': 같은 상황에서 '완료 후 invalidate' 만 하면? (시작 전 처리를 건너뜀)
    · DMA RX: RAM 0x300..+64 씀
    · 교체(훅): 더티 라인 0x300 write-back → RAM  ← DMA 데이터 위에 덮어씀?
    · invalidate 0x320 — 더티 데이터 버려짐!
```

0x320 라인은 invalidate 가 운 좋게 먼저 버렸지만 0x300 라인은 이미 RAM 을 덮었다. "완료 후 invalidate 만 하면 된다" 는 흔한 오답이 틀린 이유를 이 세 줄이 보여 준다. 정답의 같은 시나리오:

```text
-- A-RX2: …
    · clean+invalidate 0x300 (더티 → 쓰기 버퍼)
    · clean+invalidate 0x320 (더티 → 쓰기 버퍼)
    · DMA RX: RAM 0x300..+32 씀
    · DMA RX: RAM 0x320..+32 씀
```

교체 훅이 불렸지만 라인이 없어서 아무 일도 없다(추적 줄이 없음).

**시작 전에 무엇을 하나 — 세 가지 정답.** 버퍼가 정렬돼 있으면 셋 다 이 레슨의 테스트를 통과한다 (아래 변형 표에서 확인):

| 시작 전 | 더티 라인 | 깨끗한 옛 라인 | 가장자리 옆 변수(더티) | 누가 쓰나 |
|---|---|---|---|---|
| invalidate | 버림 (빠름, write-back 없음) | 버림 | **소실** | 정렬이 보장된 버퍼 전용 최적화 |
| clean+invalidate | RAM 으로 → 버림 | 버림 | 보존 (RAM 으로) | 이 레슨 정답, CMSIS 예제에서 흔함 |
| clean | RAM 으로, 라인은 남음 | 남음 (완료 후 invalidate 가 처리) | 보존 | **Linux arm64** `arch_sync_dma_for_device` (방향 무관) |
| (경계만 c+i, 안쪽 inval) | 섞음 | 버림 | 보존 | **Linux** arm64 `dcache_inval_poc`, arm32 `v7_dma_inv_range` |

Linux 소스 (git.kernel.org torvalds/linux master, 2026-11-08 에 받아 확인):

```c
/* arch/arm64/mm/dma-mapping.c */
void arch_sync_dma_for_device(phys_addr_t paddr, size_t size, enum dma_data_direction dir)
{
	unsigned long start = (unsigned long)phys_to_virt(paddr);
	dcache_clean_poc_nosync(start, start + size);
}
void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size, enum dma_data_direction dir)
{
	unsigned long start = (unsigned long)phys_to_virt(paddr);
	if (dir == DMA_TO_DEVICE)
		return;
	dcache_inval_poc_nosync(start, start + size);
}
```

```text
/* arch/arm64/mm/cache.S — __dcache_inval_poc_nosync */
	tst	x1, x3				// end cache line aligned?
	bic	x1, x1, x3
	b.eq	1f
	dc	civac, x1			// clean & invalidate D / U line
1:	tst	x0, x3				// start cache line aligned?
	bic	x0, x0, x3
	b.eq	2f
	dc	civac, x0			// clean & invalidate D / U line
	b	3f
2:	dc	ivac, x0			// invalidate D / U line
```

즉 arm64 Linux 는 장치에 넘길 때(`dma_map_single`, `dma_sync_single_for_device`) **어느 방향이든 clean**, CPU 로 돌려받을 때(`dma_unmap_single`, `dma_sync_single_for_cpu`) `DMA_TO_DEVICE` 가 아니면 **invalidate**(부분 라인은 clean+invalidate) 한다. arm32 (`arch/arm/mm/cache-v7.S`) 는 `v7_dma_map_area` 에서 `DMA_FROM_DEVICE` 면 `v7_dma_inv_range`(경계 라인은 `mcrne p15, 0, r0, c7, c14, 1 @ clean & invalidate`, 안쪽은 `c7, c6, 1 @ invalidate`), 아니면 `v7_dma_clean_range`, unmap 에서 `DMA_TO_DEVICE` 가 아니면 다시 `v7_dma_inv_range` 다. 두 아키텍처 모두 "시작 전 + 완료 후" 두 번이다. arm64 가 시작 전에 invalidate 대신 clean 을 쓰는 이유(장치가 버퍼를 다 채우지 않을 때 옛 데이터가 사라지거나 드러나는 문제 등)는 커밋 이력을 확인하지 않았다 — 확인 필요.

### Part A — 프리페치 (A-RX3)

```text
-- A-RX3: DMA 도중 추측성 프리페치가 낡은 라인을 다시 가져옴   (./starter -v)
    · 프리페치(훅): 라인 0x300 를 RAM 에서 채움 (그 순간의 RAM 값)
    · 프리페치(훅): 라인 0x320 를 RAM 에서 채움 (그 순간의 RAM 값)
    · DMA RX: RAM 0x300..+64 씀
FAIL starter.c:366  memcmp(got, pkt, 64) == 0
```

**어느 코어가 실제로 이러나 (hedge).** Arm A-profile 아키텍처는 Normal cacheable 메모리라면 프로그램이 접근하지 않은 주소도 캐시에 할당될 수 있다고 허용한다(DDI 0487 의 캐시 동작 일반 규칙 — 절 번호 미확인). 하드웨어 프리페처와 추측 실행이 있는 Cortex-A 에서는 "완료 후 invalidate" 가 선택이 아니다. Cortex-M7 도 Normal 메모리에 추측성 읽기를 할 수 있다고 알려져 있다(Cortex-M7 TRM·ST AN4839 의 해당 설명은 이번에 원문 확인 안 함 — 확인 필요). 어느 쪽이든 아키텍처가 금지하지 않는 한 "우리 칩은 안 한다" 에 기대지 않는다.

### Part B — 라인을 같이 쓰는 옆 변수

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">40 B RX 버퍼 + 4 B 카운터 — 나쁜 배치 vs 고친 배치 (라인 = 32 B)</text>
  <text x="10" y="55" fill="currentColor">나쁜</text>
  <rect x="60" y="35" width="320" height="34" class="f-acc" stroke="currentColor"/>
  <text x="70" y="57" fill="currentColor" font-family="monospace">0x200 라인: rx[0..31]</text>
  <rect x="380" y="35" width="80" height="34" class="f-acc" stroke="currentColor"/>
  <text x="385" y="57" fill="currentColor" font-family="monospace">rx[32..39]</text>
  <rect x="460" y="35" width="40" height="34" class="f-warn" stroke="currentColor"/>
  <text x="463" y="57" fill="currentColor" font-family="monospace">cnt</text>
  <rect x="500" y="35" width="200" height="34" class="f-mut" stroke="currentColor"/>
  <text x="510" y="57" fill="currentColor" font-family="monospace">…0x220 라인 나머지</text>
  <path d="M380 30 L380 74" stroke="currentColor" stroke-width="3" fill="none"/>
  <text x="385" y="92" fill="currentColor" font-size="12">0x220 라인 하나에 DMA 영역 + CPU 변수</text>
  <text x="60" y="120" fill="currentColor" font-size="12">DMA 중 CPU 가 cnt 갱신 → 라인 더티 (rx[32..39] 는 DMA 전 값)</text>
  <text x="60" y="140" fill="currentColor" font-size="12">완료 후 invalidate → cnt 4 B 소실  |  clean+invalidate → rx 8 B 를 옛 값으로 덮음</text>
  <text x="10" y="190" fill="currentColor">고친</text>
  <rect x="60" y="170" width="320" height="34" class="f-acc" stroke="currentColor"/>
  <text x="70" y="192" fill="currentColor" font-family="monospace">0x200: rx[0..31]</text>
  <rect x="380" y="170" width="200" height="34" class="f-acc" stroke="currentColor"/>
  <text x="385" y="192" fill="currentColor" font-family="monospace">0x220: rx[32..63]</text>
  <rect x="580" y="170" width="120" height="34" class="f-ok" stroke="currentColor"/>
  <text x="590" y="192" fill="currentColor" font-family="monospace">0x240: cnt</text>
  <text x="60" y="230" fill="currentColor" font-size="12">alignas(32) 시작 + dma_round_up(40) = 64 → DMA 가 라인 두 개를 통째로 소유</text>
</svg>
```

두 실험의 실제 추적 (모델의 성질 — 시작 코드에서도 통과하는 테스트):

```text
    · DMA RX: RAM 0x200..+40 씀
    · invalidate 0x220 — 더티 데이터 버려짐!
PASS bb == 0u && bn == 4u
    · DMA RX: RAM 0x200..+40 씀
    · clean+invalidate 0x220 (더티 → 쓰기 버퍼)
PASS bb == 8u && bn == 0u
```

`bb` = 버퍼에서 골든과 다른 바이트, `bn` = 옆 변수에서 다른 바이트. invalidate 는 카운터 4 B 를 잃고, clean+invalidate 는 버퍼 끝 8 B(라인을 채울 때 들어온 DMA 이전 값)를 RAM 에 다시 써서 DMA 데이터를 덮는다. **세 번째 선택지는 없다**: 하나의 라인 안에 "CPU 가 마지막으로 쓴 바이트" 와 "DMA 가 마지막으로 쓴 바이트" 가 섞여 있고, 캐시는 라인 단위로만 RAM 과 주고받는다. 그래서 규칙이 **유지보수 순서가 아니라 배치** 다:

- 시작 주소 `alignas(32)` (A-profile 공용 코드면 64, arm64 Linux `ARCH_DMA_MINALIGN` = 128 — `arch/arm64/include/asm/cache.h` 에서 확인)
- 크기 `dma_round_up(len)` — DMA 가 마지막 라인까지 통째로 소유
- 구조체 안이면 버퍼 다음 필드도 새 라인에서 (`_Alignas(32)`, `_Static_assert(offsetof(...) % 32 == 0)`, 연습 1)

```c
static uint32_t dma_round_up(uint32_t len) { return (len + LINE - 1u) & ~(LINE - 1u); }
static bool dma_buf_isolated(uint32_t addr, uint32_t len) { return len != 0 && addr % LINE == 0 && len % LINE == 0; }
```

Linux 문서도 같은 말을 한다 (`Documentation/core-api/dma-api-howto.rst`, master): "you'd need to ensure the I/O buffers were cacheline-aligned. Without that, you'd see cacheline sharing problems (data corruption) on CPUs with DMA-incoherent caches. (The CPU could write to one word, DMA would write to a different one in the same cache line, and one of them could be overwritten.)" 같은 문서의 현재 판에는 구조체 안의 DMA_FROM_DEVICE 버퍼를 감싸는 `__dma_from_device_group_begin()/end()` 매크로가 소개돼 있다 (최근 추가된 것으로 보임 — 도입 버전 미확인). 정렬 보장 할당기 자체는 C-A05 에서 만들었다(이 레슨은 "왜" 를 재현).

B1 시나리오는 시작 전 명령의 선택이 가장자리에서 갈리는 것을 확인한다: 시작 전 invalidate 로 바꾼 변형은 `view_mismatch(0x228, 4) == 0u` 에서 바로 실패한다 — DMA 와 상관없이, **arm 하는 순간** 카운터가 RAM 의 옛 값(0)으로 돌아간다.

### 변형(mutation) 표 — 각 줄이 왜 필요한가

정답 `solution.c` 에서 한 군데씩 바꿔 돌린 결과 (임시 스크립트 `/tmp/ARCH-M12_work/mutants.py` 로 문자열 치환 → 빌드 → 실행; 직접 해 보려면 해당 줄을 손으로 바꾸면 된다):

| 변형 | 결과 | 잡은 테스트 |
|---|---|---|
| `cache_clean_range` 끝의 `dsb()` 삭제 | 29/31 | dsb 횟수, **A-TX** (write-back 이 쓰기 버퍼에 있는 동안 DMA 가 읽음) |
| A5 = invalidate | 29/31 | **B1** (arm 순간 옆 카운터 소실) |
| A5 = clean (Linux arm64 식) | 31/31 | — (정답과 동등) |
| A5 없음 | 27/31 | A-RX2 (RAM 손상), B1, **C** (.bss 더티 라인이 교체되며 프레임 덮음) |
| A6 없음 | 29/31 | **A-RX3** (프리페치), C |
| A6 = clean+invalidate | 31/31 | — (정렬된 버퍼에선 라인이 깨끗하므로 동등) |
| A4 clean 없음 | 30/31 | A-TX |
| C2 도어벨 앞 `dsb()` 삭제 | 30/31 | C (`g_mac_tx_bad_desc` — own=1 이 아직 쓰기 버퍼) |
| C2 clean 없음 | 30/31 | C (에코 프레임 stale) |
| C1 완료 invalidate 없음 | 30/31 | C (둘째 라인 프리페치) |
| C1 재장전 유지보수 없음 | **31/31** | 못 잡음 — 아래 설명 |
| B1 `return len` | 27/31 | B 표 테스트, B2 |

마지막에서 둘째 줄은 정직하게 적는다: 우리 `eth_rx_poll` 은 버퍼를 **읽기만** 하므로 재장전 때 그 버퍼의 라인은 모두 깨끗하고, 깨끗한 라인은 교체돼도 무해하며 완료 후 invalidate 가 처리한다. 그래도 넣는 이유: 나중에 누가 "제자리(in-place)에서 헤더를 고치는" 코드를 넣는 순간 더티 라인이 생기고, 그때부터는 A-RX2 버그가 된다. 드라이버의 계약을 "버퍼를 DMA 에 넘길 때마다 유지보수" 로 고정해 두는 편이 싸다(clean 계열은 깨끗한 라인에선 거의 일을 하지 않는다).

### Part C — 이더넷 RX 링과 TX

```c
static int eth_rx_poll(uint8_t *out, uint32_t cap) {
    uint32_t d = RXDESC + 16u * g_drv_rx_idx;
    if (cpu_rd32(d + D_OWN) != 0u) return -1;      /* 아직 DMA 소유 */
    uint32_t buf = cpu_rd32(d + D_BUF), len = cpu_rd32(d + D_LEN);
    drv_rx_complete(buf, RXBUF);                   /* 버퍼 전체 */
    int ret = -2;
    if (len <= cap) { cpu_read_buf(buf, out, len); ret = (int)len; }
    drv_rx_arm(buf, RXBUF);                        /* 유지보수 먼저, own 은 그다음 */
    cpu_wr32(d + D_LEN, 0);
    cpu_wr32(d + D_OWN, 1);
    dsb();
    g_drv_rx_idx = (g_drv_rx_idx + 1u) % RING;
    return ret;
}
```

설계 결정:

- **디스크립터는 non-cacheable.** 디스크립터를 캐시에 두면 `own` 과 `len` 이 한 라인이고, CPU 가 다음 디스크립터를 읽는 동안 DMA 가 같은 라인의 다른 디스크립터를 갱신하는 일이 생긴다 — Part B 의 공유 라인 문제가 디스크립터 사이에서 생긴다. 그래서 ST 의 STM32H7 이더넷 예제들은 디스크립터를 MPU 로 non-cacheable(또는 Device) 지정한 SRAM 에 두는 것으로 알려져 있다 (예제 소스 이번에 확인 안 함). C-A02 Q5 에서도 같은 권고를 했다.
- **완료 후 invalidate 는 `len` 이 아니라 버퍼 전체.** 이 테스트는 `len` 만 invalidate 해도 통과한다(복사는 `len` 까지만 하므로). 하지만 다음 재장전의 계약을 단순하게 두려면 전체가 낫고, 비용 차이는 라인 1~2 개다.
- **순서: 유지보수 → own=1 → DSB.** own=1 이 먼저 RAM 에 닿으면 그 순간부터 MAC 이 버퍼에 쓸 수 있고, 그 뒤의 clean 이 DMA 데이터를 덮을 수 있다.
- **TX: 복사 → clean(내부 DSB) → 디스크립터 3 워드 → DSB → 도어벨.** 도어벨 앞 DSB 를 지우면 모델에서 `g_mac_tx_bad_desc` 가 올라간다(own=1 이 쓰기 버퍼에 있음). 실제 칩에서 DSB 가 꼭 필요한지, DMB 로 충분한지는 도어벨 레지스터의 메모리 타입과 인터커넥트에 달려 있다(Device-nGnRE 쓰기와 앞선 Normal-NC 쓰기의 순서는 DMB 로 충분하다는 설명이 흔하다 — Linux `dma_wmb()` 가 arm64 에서 `dmb oshst` 인 이유, C-A02 Q6). 캐시 유지보수의 **완료** 를 기다리는 데는 DSB 가 필요하다 (ARM-M05 표). 이 레슨은 모델이 단순한 쪽(DSB)을 택했다.

`eth_init` 은 주어진 코드이지만 중요한 함정 하나를 일부러 넣었다: 부팅 직후 버퍼를 0 으로 쓰는 루프(C 런타임의 `.bss` 0 초기화와 같은 효과)가 캐시에 **더티 라인 8 개** 를 남긴다. `./solution -v` 의 C 시나리오 첫 줄들:

```text
-- C: 이더넷 RX 링 4 개 + 에코 TX, 나쁜 순간에 교체·프리페치
    · clean+invalidate 0x400 (더티 → 쓰기 버퍼)
    · clean+invalidate 0x420 (더티 → 쓰기 버퍼)
    …
    · clean+invalidate 0x4e0 (더티 → 쓰기 버퍼)
    · 프리페치(훅): 라인 0x420 를 RAM 에서 채움 (그 순간의 RAM 값)
    · DMA RX: RAM 0x400..+20 씀
    · clean 0x600 (쓰기 버퍼로)
    · DMA TX: RAM 0x600..+20 읽음
```

실제 펌웨어에서 `.bss` 는 보통 D-cache 를 켜기 **전** 에 지우므로 이 경우는 생기지 않을 수 있다 — 하지만 캐시를 켠 뒤 `memset(rx_bufs, 0, …)` 하는 초기화 코드는 흔하다.

### 비용 표 — 캐시 가능 + 유지보수 vs MPU non-cacheable

정답의 실제 출력 (결정적인 개수라 실행마다 같다. 시간 측정이 아니므로 load average 와 무관 — 참고로 작성 시 `uptime` load average 5.83–8.01):

```text
cost (N=8 frames x 64 B, read twice):  line fills  NC reads  CMO lines  DSB
  cacheable + maintenance                     16         0         32   16
  MPU non-cacheable                            0       256          0    0
```

프레임마다 64 B 를 워드 단위로 **두 번** 훑는다(체크섬 + 검증 같은 흔한 패턴). 캐시 가능: 프레임당 라인 채움 2 번(버스트 2 개) + 유지보수 라인 연산 4 번(arm 2 + 완료 2) + DSB 2 번. non-cacheable: 프레임당 단일 워드 읽기 32 번, 유지보수 0. 이것을 선택 기준으로 정리하면:

| | 캐시 가능 + clean/invalidate | MPU non-cacheable 영역 | 하드웨어 일관성 (ACP / CCI, `dma_alloc_coherent` 가 일관 장치일 때) |
|---|---|---|---|
| 정확성 부담 | 드라이버의 모든 경로에 순서 + 정렬 규칙 | 없음 (배리어만) | 없음 |
| CPU 가 데이터를 여러 번 읽을 때 | 빠름 (라인 채움 후 적중) | 접근마다 버스 왕복 — 모델에서 32 vs 2 트랜잭션 | 빠름 |
| 유지보수 비용 | 라인 수에 비례 (1.5 KB 프레임 = 48 라인 x 2) | 0 | 0 |
| 잘 맞는 데이터 | 큰 페이로드, CPU 가 가공하는 데이터 | 디스크립터, 작은 상태 워드, CPU 가 거의 안 읽는 버퍼 | 플랫폼이 제공하면 기본값 |
| MCU 제약 | 정렬 낭비(최대 31 B x 2) | MPU 영역 개수·크기(2^k, ARMv7-M; ARCH-M15) | Cortex-M7 MCU 엔 보통 없음 |

Linux 의 `dma_alloc_coherent` 는 비일관 arm64 장치에서 `arch_dma_prep_coherent` 로 그 메모리를 한 번 clean 한 뒤(소스 확인) CPU 쪽 매핑을 non-cacheable 로 만드는 것으로 알려져 있다(리매핑 세부는 미확인). 즉 "MPU non-cacheable 영역" 의 리눅스판이다. 스트리밍 매핑(`dma_map_single`)이 이 레슨의 clean/invalidate 경로다.

### `volatile` 과 atomics 가 답이 아닌 이유

모델의 `cpu_rd8()` 은 **매번 실제 load 를 낸다** — `volatile` 이 보장하는 것이 정확히 이것이다(C-B09). 그런데도 A-RX1 에서 옛 값을 읽었다: load 가 캐시에 적중했기 때문이다. `_Atomic` 도 같다. C11 원자 연산의 가시성 보장은 C 스레드(= 일관된 캐시를 공유하는 코어들) 사이의 것이고, DMA 엔진은 그 일관성 도메인 밖에 있다(C-A02 Q6). arm64 의 `ldar`/`stlr`, `dmb ish` 는 **inner shareable** 관찰자 사이의 순서를 정할 뿐 캐시 내용을 RAM 으로 보내지 않는다. 배리어는 "언제" 를, 캐시 유지보수는 "어디에 있나" 를 다룬다 — 둘 다 필요하다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 31/31 passed (6 회 반복, 같은 출력)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san   # 31/31, 보고 없음
```

단일 스레드 모델이라 TSan 은 해당 없다(n/a). 실행 시간 0.002 초.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| RX 에서 "완료 후 invalidate" 만 | DMA 도중 더티 라인이 교체되면 RAM 이 이미 덮임 (A-RX2', 모델에서 32 B 손상) | 시작 전 `clean` 또는 `clean+invalidate` + 완료 후 `invalidate` |
| RX 에서 "시작 전 invalidate" 만 | DMA 도중 프리페치·추측 읽기가 옛 라인을 다시 채움 (A-RX3) | 완료 후 한 번 더 `invalidate` |
| 완료 후 `clean+invalidate` 를 습관처럼 | 정렬된 버퍼면 무해하지만, CPU 가 그 사이 버퍼를 썼다면(버그) 그 더티 라인이 DMA 데이터를 덮는다 | 완료 후엔 `invalidate` — 더티가 있다는 것 자체가 버그 신호 |
| clean 후 DSB 없이 DMA 시작 | write-back 이 끝나기 전에 장치가 RAM 을 읽음 (변형 표 1행) | CMSIS 함수는 내부 `__DSB()`. 직접 짠 루프면 끝에 `dsb` |
| RX 버퍼 크기 40 B, 시작만 `alignas(32)` | 끝 라인을 옆 변수와 공유 → 어떤 명령으로도 못 막음 (bb 8 / bn 4) | `dma_round_up(len)`, 다음 필드 `_Alignas(32)` |
| 정렬 안 된 버퍼에 시작 전 `invalidate` | DMA 와 무관하게 옆 변수의 더티 값이 즉시 사라짐 (B1) | 경계 라인은 `clean+invalidate` (Linux 방식) — 그래도 DMA 중 옆 변수 쓰기는 못 막음 |
| 옛 CMSIS `SCB_InvalidateDCache_by_Addr(addr, size)` 에 정렬 안 된 addr | V5.00 루프는 시작 오프셋을 크기에 안 더해 마지막 라인을 빠뜨림 (연습 3: 16 B stale) | 버퍼를 정렬하거나 V1.0.3 이상(`cachel1_armv7.h`) |
| DMA 버퍼에 `volatile` | load 가 실제로 나가도 캐시 적중 → 옛 값 | 캐시 유지보수 또는 non-cacheable 영역 |
| own=1 을 쓴 뒤 버퍼 유지보수 | own=1 이 보이는 순간 DMA 가 쓰기 시작 → 뒤의 clean 이 덮음 | 유지보수 → own=1 → DSB |
| 디스크립터를 캐시 가능 RAM 에, 16 B 씩 붙여서 | 32 B 라인 하나에 디스크립터 두 개 → CPU 의 갱신과 DMA 의 갱신이 한 라인에서 충돌 | MPU non-cacheable 영역, 또는 디스크립터를 라인 크기로 패딩 |

## 8. 면접 Q&A

<details><summary>Q1. non-coherent DMA 에서 TX 와 RX 의 캐시 유지보수 순서를 말해 보세요.</summary>

TX: CPU 가 버퍼를 다 쓴 뒤 clean(더티 라인을 메모리로), DSB 로 완료 대기, 그다음 디스크립터·도어벨. RX: 버퍼를 넘기기 **전** 에 clean 또는 clean+invalidate 로 더티 라인을 없애고(DSB), 넘긴 뒤 CPU 는 버퍼를 건드리지 않는다. 완료 인터럽트 후 **invalidate** 하고 읽는다. 이 레슨 모델에서 각 단계를 하나씩 뺀 변형이 모두 다른 테스트에서 실패했다(clean 없음 → A-TX, 시작 전 처리 없음 → A-RX2·C, 완료 후 invalidate 없음 → A-RX3·C). 꼬리: "Linux 에선?" → `dma_map_single(dev, buf, len, DMA_TO_DEVICE)` 가 clean, `DMA_FROM_DEVICE` 로 map 할 때 arm64 는 clean(arm32 는 invalidate, 경계 라인은 clean+invalidate), `dma_unmap_single`/`dma_sync_single_for_cpu` 가 invalidate. 소스로 확인했다.
</details>

<details><summary>Q2. RX 인데 왜 DMA 시작 전에도 뭔가를 해야 하죠? 완료 후 invalidate 면 충분하지 않나요?</summary>

버퍼에 더티 라인이 남아 있으면 DMA 도중 캐시 교체가 그 라인을 메모리에 write-back 하고, 그게 DMA 가 막 쓴 데이터를 덮는다. 완료 후 invalidate 는 캐시 쪽만 버릴 뿐 이미 망가진 메모리를 못 고친다. 모델에서 memset(0) 해 둔 버퍼로 재현하면 RAM 자체가 0 으로 돌아갔다(`memcmp(&g_ram[0x300], pkt, 64)` 실패). 더티 라인이 생기는 흔한 원인: 받기 전에 버퍼를 지움, 이전 사용 때 CPU 가 버퍼를 제자리 수정, 캐시를 켠 뒤 0 초기화. 꼬리: "시작 전엔 invalidate 와 clean 중 뭐가 낫나요?" → 정렬이 보장되면 invalidate 가 write-back 이 없어 싸지만, 경계 라인의 옆 데이터를 지우는 위험이 있다. clean 은 안전하고 arm64 Linux 가 택한 방식이다.
</details>

<details><summary>Q3. RX 버퍼가 32 B 정렬이 아니거나 크기가 32 의 배수가 아니면 무슨 일이 생기나요?</summary>

끝(또는 시작) 라인을 다른 변수와 공유한다. DMA 도중 CPU 가 그 변수를 쓰면 그 라인은 "옆 변수의 새 값 + 버퍼 부분의 DMA 이전 값" 으로 더티해진다. 완료 후 invalidate 하면 옆 변수의 갱신이 사라지고(모델: 4 B), clean+invalidate 하면 버퍼 끝이 옛 값으로 덮인다(모델: 8 B). 캐시는 라인 단위로만 메모리와 교환하므로 **어떤 순서로도 못 막는다** — 해결은 배치: 시작 정렬 + 크기 올림 + 다음 필드도 새 라인. 꼬리: "Cortex-A 공용 코드라면?" → 64 B 가 흔하지만 arm64 Linux 는 `ARCH_DMA_MINALIGN` 을 128 로 둔다(헤더 확인). 런타임엔 CTR_EL0.DminLine 으로 최소 라인을 읽는다(연습 3).
</details>

<details><summary>Q4. DMA 버퍼를 volatile 로 선언하면 해결되나요? atomic 은요?</summary>

아니다. volatile 은 "컴파일러가 접근을 없애거나 합치지 않는다" — 즉 load 명령이 실제로 나간다는 것까지다. 그 load 가 D-cache 에 적중하면 옛 라인을 돌려준다. 모델의 `cpu_rd8` 은 매번 실제 접근인데도 A-RX1 에서 옛 값을 읽었다. atomic 도 같다: C11 의 가시성 보장은 일관된 캐시를 공유하는 스레드끼리의 것이고, DMA 엔진은 C 추상 기계 밖의 관찰자다. `dmb ish` 같은 배리어는 순서만 정하고 데이터를 메모리로 보내지 않는다. 꼬리: "그럼 volatile 은 DMA 코드에서 어디에 쓰나요?" → 장치 레지스터(도어벨, NDTR, 상태 레지스터)와 non-cacheable 디스크립터 접근이 컴파일러에게 사라지지 않게. 데이터 버퍼는 유지보수 + 배리어.
</details>

<details><summary>Q5. CMSIS SCB_CleanDCache_by_Addr 는 내부에서 무엇을 하나요? 실제 명령은?</summary>

CMSIS `cachel1_armv7.h` V1.0.3 기준: `__DSB()` → 라인마다 `SCB->DCCMVAC = op_addr` (주소를 SCB 레지스터 0xE000EF68 에 store, 하위 5 비트 무시) → `__DSB()` → `__ISB()`. 크기에 시작 주소의 라인 내 오프셋을 더해 부분 라인을 포함한다. clang `thumbv7em -mcpu=cortex-m7 -O2` 로 같은 루프를 컴파일하면 `movw r12, #61288; movt r12, #57344` 로 주소를 만들고 `str.w r0, [r12]` 를 4 번 펼친 루프 + 앞뒤 `dsb sy`, 끝에 `isb sy` 가 나왔다. A-profile 은 `dc cvac` 같은 시스템 명령을 쓴다. 꼬리: "옛 CMSIS 는요?" → `core_cm7.h` V5.00 은 오프셋을 크기에 더하지 않아, 정렬 안 된 주소면 마지막 라인을 빠뜨린다(소스 확인, 연습 3 재현). 그래서 버퍼를 정렬하는 습관이 API 버전과 무관하게 안전하다.
</details>

<details><summary>Q6. 그냥 DMA 버퍼를 MPU 로 non-cacheable 로 만들면 안 되나요? 언제 그게 맞나요?</summary>

된다. 정확성 부담이 사라지는 대신 CPU 의 모든 접근이 버스 왕복이 된다. 이 레슨 모델에서 64 B 프레임을 두 번 훑으면 캐시 가능 경로는 라인 채움 2 번 + 유지보수 4 라인, non-cacheable 은 단일 워드 읽기 32 번이었다(실행 시간은 모델이 재지 않는다 — 실제 비율은 메모리 지연에 따라 다름). 디스크립터·상태 워드처럼 작고 CPU 와 DMA 가 번갈아 쓰는 것, CPU 가 거의 안 읽는 버퍼(예: DMA→DMA 전달, 오디오 출력)는 non-cacheable 이 맞다. CPU 가 가공하는 큰 페이로드(패킷 파싱, 이미지 처리)는 캐시 가능 + 유지보수가 맞다. 꼬리: "MPU 제약은?" → ARMv7-M MPU 는 영역 크기 2^k·크기 정렬·영역 수 8/16 개(ARCH-M15, C-A05 Q7), 그래서 링커 스크립트로 DMA 버퍼를 한 섹션에 모아 영역 하나로 덮는다. write-through 영역은 TX 의 clean 만 없애 주고 RX 의 invalidate 는 여전히 필요하다(연습 4).
</details>

<details><summary>Q7. 디스크립터의 own 비트를 넘기고 도어벨을 칠 때 어떤 배리어가 필요하죠? DSB 인가요 DMB 인가요?</summary>

필요한 것은 두 가지다. (1) 데이터 버퍼의 clean 이 **끝났음** — 캐시 유지보수 완료는 DSB 로 기다린다(CMSIS 함수에 들어 있다). (2) 디스크립터 쓰기(Normal non-cacheable)가 도어벨 쓰기(Device)보다 장치에게 **먼저 보임** — 이건 순서 문제라 DMB 로 충분하다는 설명이 일반적이고, Linux 는 그 자리에 `dma_wmb()`(arm64 `dmb oshst`)를 쓴다(C-A02 Q6). 이 레슨 모델은 쓰기 버퍼 + "즉시 읽는 도어벨" 로 단순화해서 DSB 를 요구한다 — 빼면 `g_mac_tx_bad_desc` 가 1 이 된다. 꼬리: "DMB 를 inner shareable(`ish`)로 하면?" → DMA 장치는 보통 outer shareable 도메인이라 `ish` 범위가 충분한지 플랫폼 문서를 봐야 한다. 그래서 커널·CMSIS 의 장치용 배리어를 쓴다.
</details>

## 9. 추가 연습

### 연습 1 — 레이아웃 검사기와 `_Static_assert`

`bool shares_line(a_off, a_len, b_off, b_len, line)` — 두 바이트 구간이 같은 라인을 하나라도 쓰나. 드라이버 구조체 `struct bad_dev { uint32_t state; uint8_t rx[40]; uint32_t rx_count; }` 의 문제를 찾고, 컴파일 시간에 규칙을 강제하는 `struct good_dev` 를 만든 뒤, 두 구조체를 모델 RAM 0x200 에 놓고 "DMA 도중 ISR 이 rx_count 갱신" 을 돌려라.

<details><summary>답</summary>

라인 번호 구간 `[off/line, (off+len-1)/line]` 두 개가 겹치는지 본다 (길이 0 이면 false). bad_dev: `rx` 는 오프셋 4..43 → 라인 0·1, `state` 와 라인 0, `rx_count`(44) 와 라인 1 을 공유. 고친 구조체:

```c
struct good_dev { uint32_t state; _Alignas(32) uint8_t rx[64]; _Alignas(32) uint32_t rx_count; };
_Static_assert(_Alignof(struct good_dev) == 32, "구조체 자체가 라인 정렬");
_Static_assert(offsetof(struct good_dev, rx) % 32 == 0, "rx 시작 = 라인 경계");
_Static_assert(sizeof(((struct good_dev *)0)->rx) % 32 == 0, "rx 크기 = 라인 배수");
_Static_assert(offsetof(struct good_dev, rx_count) % 32 == 0, "다음 필드는 새 라인");
```

`sizeof(struct good_dev)` = 128, `rx` 오프셋 32 (실측). `_Alignof` 검사가 빠지면 오프셋 규칙이 주소 규칙이 되지 않는다(구조체가 4 B 경계에 놓이면 끝). 모델 실행: bad 는 손상 > 0, good 은 0. 낭비는 28 + 24 + 28 B — 그래서 실제 드라이버는 DMA 버퍼들을 한 덩어리로 모은다(Linux `__dma_from_device_group_begin/end` 의 생각).
</details>

### 연습 2 — 양방향(BIDIRECTIONAL) 버퍼: SPI 전이중 in-place

CPU 가 명령을 버퍼에 쓰고, 장치가 그 버퍼를 읽으면서 같은 자리에 응답을 쓴다(`spi_xfer_inplace`). `drv_bidir_start` / `drv_bidir_done` 을 짜라. 시작 직후 교체, 도중 프리페치가 일어나도 장치는 명령을, CPU 는 응답을 정확히 봐야 한다.

<details><summary>답</summary>

시작 전 **clean** (명령을 메모리로 + 더티 0 개 — 교체돼도 write-back 없음), 완료 후 **invalidate**. Linux arm32 `v7_dma_map_area` 가 `DMA_FROM_DEVICE` 가 아니면(=TO_DEVICE·BIDIRECTIONAL) clean, unmap 에서 `DMA_TO_DEVICE` 가 아니면 invalidate 하는 것과 같다. 시작 전 invalidate 는 틀린다 — 명령이 메모리에 가기 전에 버려진다. 테스트: 장치가 받은 64 B == 명령(`g_tx_stale == 0`), CPU 가 읽은 64 B == 명령 ^ 0x5A, 골든 불일치 0.
</details>

### 연습 3 — CMSIS 옛 루프 vs 새 루프, 라인 크기 레지스터

(a) CMSIS `core_cm7.h` V5.00 과 `cachel1_armv7.h` V1.0.3 의 invalidate 루프를 그대로 옮겨 `(0x210, 32)` 에서 각각 몇 라인을 건드리는지, 같은 RX 시나리오에서 몇 바이트가 낡은 채 남는지 구하라. (b) Cortex-M7 `CCSIDR.LineSize`(비트 2:0) 와 A-profile `CTR_EL0.DminLine`(비트 19:16) 에서 라인 바이트 수를 구하는 함수를 짜라.

<details><summary>답</summary>

(a) V5.00: `op_size = 32` → 0x210 한 번(라인 0x200)으로 끝 → **1 라인**, 0x220..0x22F 의 16 B 가 옛 값으로 남는다(모델 `view_mismatch == 16`). V1.0.3: `op_size = 32 + 16 = 48` → 0x210, 0x230 → **2 라인**, 0 B. 정렬된 `(0x200, 64)` 는 둘 다 2 라인. (b) `CCSIDR.LineSize` = log2(라인 워드 수) − 2 → 바이트 = `1 << (LineSize + 4)` (M7 은 1 → 32 B). `CTR_EL0.DminLine` = log2(워드 수) → 바이트 = `4 << DminLine` (4 → 64 B, 5 → 128 B). Linux arm64 의 `dcache_line_size` 매크로가 CTR_EL0 에서 이렇게 구한다. 레지스터 필드 정의는 ARMv7-M ARM 의 CCSIDR, DDI 0487 의 CTR_EL0 설명 기준(절 번호 미확인). 테스트 값은 합성 값이다(특정 칩의 실제 레지스터 값 아님).
</details>

### 연습 4 — MPU write-through 영역

[0x700, 0x800) 을 write-through, no-write-allocate 로 지정했다고 하자(모델에 `g_wt_base/limit` 추가). TX 와 RX 에 무엇이 여전히 필요한가?

<details><summary>답</summary>

TX: 쓰기가 메모리로 바로 가므로 **clean 이 필요 없다** — 하지만 쓰기 버퍼는 비워야 한다. 모델에서 `dsb()` 없이 시작하면 `g_tx_stale > 0`, `dsb()` 하나면 0 (유지보수 라인 연산 0 번, write-back 영역의 같은 TX 는 2 번). RX: 라인은 더티가 될 수 없으므로 **시작 전 처리는 생략 가능**(교체돼도 write-back 0 번 — 모델 확인), 하지만 CPU 가 읽어 둔 라인이나 DMA 도중 다시 읽은 라인은 옛 값이다 → **완료 후 invalidate 는 그대로 필요** (안 하면 64 B 전부 불일치). 즉 write-through 는 "TX 쪽 반" 만 풀어 준다. 쓰기가 많은 데이터에는 버스 트래픽이 늘어난다(ARCH-M06).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san
```

실제 출력: `26/26 passed` (두 빌드 모두, 새니타이저 보고 없음).

## 10. 핵심 정리

- DMA 엔진(일관성 포트 없음)은 RAM 만 본다. CPU 는 캐시를 본다. `volatile`·atomic·`dmb` 는 이 차이를 메우지 못한다 — 캐시 유지보수(clean / invalidate / clean+invalidate)가 메운다.
- TX = **clean → DSB → 시작**. RX = **(clean 또는 clean+invalidate) → DSB → 시작 … 완료 → invalidate → 읽기**. 두 번이다.
- 시작 전 처리가 막는 것 = DMA 도중 **더티 라인 교체의 write-back**(RAM 손상, 완료 후엔 못 고침). 완료 후 invalidate 가 막는 것 = DMA 도중 **프리페치·추측 읽기로 다시 들어온 옛 라인**.
- 버퍼는 **시작 정렬 + 크기를 라인 배수로**(M7 32 B, Cortex-A 64 B, arm64 Linux `ARCH_DMA_MINALIGN` 128). 라인을 같이 쓰는 옆 변수는 어떤 명령 순서로도 못 지킨다(invalidate → 옆 변수 소실, clean+invalidate → DMA 데이터 덮음).
- Cortex-M7: `SCB_CleanDCache_by_Addr` = DSB + `DCCMVAC`(0xE000EF68)에 주소 store 루프 + DSB + ISB. 옛 CMSIS V5.00 은 정렬 안 된 주소에서 마지막 라인을 빠뜨린다.
- Linux arm64: device 쪽으로 넘길 때 방향 무관 clean, CPU 쪽으로 받을 때 `DMA_TO_DEVICE` 가 아니면 invalidate(경계는 `dc civac`). arm32: FROM_DEVICE map 은 invalidate(경계 c+i), 그 외 clean.
- 디스크립터는 non-cacheable, 순서는 **버퍼 유지보수 → own=1 → 배리어 → 도어벨**. 큰 페이로드는 캐시 가능 + 유지보수, 작은 공유 상태는 non-cacheable (모델: 2 채움 + 4 CMO vs 32 단일 읽기).

## 참고자료

- Arm CMSIS-Core: `CMSIS/Core/Include/cachel1_armv7.h` V1.0.3 (2023-03-17) — `SCB_CleanDCache_by_Addr`, `SCB_InvalidateDCache_by_Addr`, `SCB_CleanInvalidateDCache_by_Addr`; `core_cm7.h` V5.2.0 — SCB 레지스터 오프셋(DCIMVAC 0x25C, DCCMVAC 0x268, DCCIMVAC 0x270), CCSIDR 필드; 태그 5.0.0 의 `core_cm7.h` V5.00 (옛 루프): <https://github.com/ARM-software/CMSIS_5>
- CMSIS 문서 — D-Cache functions: <https://arm-software.github.io/CMSIS_5/Core/html/group__Dcache__functions__m7.html>
- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403) — 캐시 유지보수 연산(DCCMVAC 등), CCSIDR, DSB/ISB (절 번호 미확인)
- Arm, *Arm Architecture Reference Manual for A-profile* (DDI 0487) — `DC CVAC/IVAC/CIVAC`, CTR_EL0, PoC, 추측성 캐시 할당 규칙: <https://developer.arm.com/documentation/ddi0487/latest/>
- Arm, *Cortex-M7 Technical Reference Manual* (DDI 0489) — L1 D-cache(32 B 라인), 추측성 읽기 관련 설명 (이번에 원문 확인 안 함)
- STMicroelectronics, AN4839 *Level 1 cache on STM32F7 Series and STM32H7 Series* — 캐시 유지보수와 MPU 속성으로 DMA 일관성 다루기 (원문 확인 안 함)
- Linux kernel (master, git.kernel.org): `arch/arm64/mm/dma-mapping.c` (`arch_sync_dma_for_device/cpu`, `arch_dma_prep_coherent`), `arch/arm64/mm/cache.S` (`__dcache_inval_poc_nosync`), `arch/arm/mm/cache-v7.S` (`v7_dma_inv_range`, `v7_dma_map_area`, `v7_dma_unmap_area`), `arch/arm64/include/asm/cache.h` (`ARCH_DMA_MINALIGN` 128): <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/>
- Linux `Documentation/core-api/dma-api-howto.rst` — 스트리밍/일관 매핑, 캐시 라인 공유 경고, `__dma_from_device_group_begin/end`, ARCH_DMA_MINALIGN: <https://docs.kernel.org/core-api/dma-api-howto.html>
- 관련 레슨: C-B09 (volatile 의 한계), C-A02 (배리어, DMA OWN/doorbell 순서, Q5·Q6), C-A05 (정렬 보장 할당·DMA 버퍼 조건), ARM-M05 (DMB/DSB/ISB), ARCH-M06 (write-back vs write-through), ARCH-M11 (DMA 컨트롤러 모델), ARCH-M15 (MPU 영역), ARCH-M08 (false sharing — 같은 "라인 공유" 가 코어끼리면 성능 문제, DMA 와면 정확성 문제), ARCH-A04 (MESI — 일관된 관찰자끼리는 하드웨어가 이 일을 한다), SIV-M02 (DMA 검증: 실패가 라인 단위면 캐시 먼저 의심), SIV-M09 (동시성 스트레스), CAM-M11 (캡처 버퍼 소유권 — 같은 dqbuf 후 invalidate 규칙), DS-A01 (DMA 생산자 링)
