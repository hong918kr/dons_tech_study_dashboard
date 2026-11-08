---
id: C-A02
time: 110분
updated: 2026-11-08
---
# C-A02 컴파일러 배리어와 메모리 순서: volatile만으론 부족한 이유

## 학습 목표

- "소스에 쓴 순서" 가 DMA 장치에게 그대로 보이려면 **컴파일러 → CPU/버스 → 캐시** 세 층을 모두 통과해야 함을 설명하고, 층마다 맞는 도구(volatile, 컴파일러 배리어, DMB/DSB, 캐시 유지보수)를 고를 수 있다.
- `asm volatile("":::"memory")`, `atomic_signal_fence`, `atomic_thread_fence` 를 넣었을 때 **실제 -O2 명령 순서가 어떻게 바뀌는지** arm64 와 Cortex-M4 asm 으로 확인하고, "명령어가 생기는가 / 무엇을 막는가" 로 구분한다.
- C11 이 보장하는 것(5.1.2.3p6 volatile, 7.17.4 fence 동기화 규칙, 5.1.2.4p25 data race)과 보장하지 않는 것(DMA 는 C 스레드가 아니다)을 나눠 말할 수 있다.
- DMA 디스크립터 링에서 **필드 → `dma_wmb` → OWN → doorbell**, **OWN 확인 → `dma_rmb` → 상태 읽기** 를 쓰고, D-cache 가 있으면 clean/invalidate 위치까지 맞게 넣는다.
- TSan, 약한 메모리 시뮬레이터, 실제 리트머스 테스트로 "순서 버그" 를 재현·검증하는 방법과 각각의 한계를 안다.

## 1. 문제

이더넷·USB·SDIO·NVMe 드라이버는 전부 "CPU 가 디스크립터를 채우고 OWN 비트로 장치에게 넘긴다" 구조이고, 시니어 펌웨어 면접은 C-B09 의 "volatile 이 뭔가요" 다음에 **"그럼 volatile 이면 DMA 디스크립터도 안전한가요?"** 를 묻는다. 정답은 "아니다, 그리고 왜·무엇으로 고치는지" 를 층별로 말하는 것이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">소스의 "필드 → OWN" 순서가 DMA 에게도 그 순서로 보이려면 세 층을 다 통과해야 한다</text>
  <rect x="10" y="40" width="170" height="152" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="60" fill="currentColor">CPU 코드 (소스 순서)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="84">buf[] = payload</text>
    <text x="20" y="104">d-&gt;addr = pa</text>
    <text x="20" y="124">d-&gt;len  = n</text>
    <text x="20" y="144">d-&gt;ctrl = OWN</text>
    <text x="20" y="164">TX_TAIL = head</text>
  </g>
  <rect x="200" y="40" width="345" height="46" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="210" y="59" fill="currentColor">① 컴파일러: 명령어 순서 바꾸기·합치기·지우기</text>
  <text x="210" y="77" fill="currentColor" font-size="12">volatile(서로끼리만), asm("":::"memory"), signal_fence</text>
  <rect x="200" y="96" width="345" height="46" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="210" y="115" fill="currentColor">② CPU·버스: store buffer, 이른(투기) load</text>
  <text x="210" y="133" fill="currentColor" font-size="12">DMB (thread_fence, dma_wmb/rmb), 완료 대기는 DSB</text>
  <rect x="200" y="152" width="345" height="46" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="210" y="171" fill="currentColor">③ 캐시: D-cache 에만 있고 RAM 엔 없는 데이터</text>
  <text x="210" y="189" fill="currentColor" font-size="12">clean / invalidate, MPU 로 non-cacheable 지정</text>
  <rect x="565" y="40" width="165" height="158" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="575" y="60" fill="currentColor">DMA 엔진</text>
  <text x="575" y="84" fill="currentColor" font-size="12">TX_TAIL 로 깨어나</text>
  <text x="575" y="104" fill="currentColor" font-size="12">ctrl 의 OWN 을 보면</text>
  <text x="575" y="124" fill="currentColor" font-size="12">addr/len 을 읽고</text>
  <text x="575" y="144" fill="currentColor" font-size="12">RAM 에서 buf 를 가져감</text>
  <text x="575" y="164" fill="currentColor" font-size="12">끝나면 stat 쓰고</text>
  <text x="575" y="184" fill="currentColor" font-size="12">OWN 을 내린다</text>
  <path d="M180 116 L200 116 M545 116 L565 116" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="10" y="226">volatile: ① 에서 volatile 접근끼리의 순서만. 일반 변수(buf, addr, len)는 그 사이를 넘나들 수 있다.</text>
    <text x="10" y="248">asm("":::"memory") = atomic_signal_fence: ① 전체를 막지만 명령어 0개 → ② 는 그대로.</text>
    <text x="10" y="270">atomic_thread_fence / dma_wmb: ① + ② (DMB 명령). ③ 은 어떤 C 구문으로도 안 된다.</text>
    <text x="10" y="292">그래서 드라이버는 "캐시 clean → 필드 → dma_wmb → OWN → (writel 의 배리어) → doorbell" 순서를 직접 쓴다.</text>
    <text x="10" y="316">회수 쪽은 거꾸로: OWN==0 확인 → dma_rmb → stat/len → (RX 면) invalidate → 데이터.</text>
  </g>
</svg>
```

이 레슨은 세 층을 **각각 따로, 결정적으로** 다룬다. 컴파일러 층은 실제 asm 을 본다(`demo_order.c`). CPU 층은 맥의 하드웨어 재배치를 테스트에 쓸 수 없으니(나타날지 말지가 실행마다 다르다) **약한 메모리 순서 시뮬레이터** 로 재현하고, 실제 하드웨어에서 일어나는지는 `demo_litmus.c` 로 따로 관찰한다. 캐시 층은 "캐시 값 / RAM 값" 을 따로 가진 mock 으로 다룬다.

### Part A — 언어 층: 실제 -O2 순서 비교와 C11 게시(publish)

1. `demo_order.c` 의 다섯 가지 TX 게시 버전 — 배리어 없음 / ctrl 만 volatile / + `asm volatile("":::"memory")` / + `atomic_signal_fence` / + `atomic_thread_fence` — 과 회수 쪽 네 버전을 **arm64(호스트)와 Cortex-M4** 로 `-O2 -S` 해서 명령 순서를 비교한다. 어떤 버전에서 어떤 접근이 사라지거나, 합쳐지거나, 옮겨지는가? 어떤 명령어가 새로 생기는가?
2. 그 근거로 올바른 C11 버전을 작성한다 (`starter.c`):
   - `int desc_publish(desc_t *d, const uint8_t *buf, uint32_t len)` — 아직 OWN 이거나 len 이 0/64 초과면 -1 (아무것도 안 씀). 아니면 buf, len 을 쓰고 status 를 0 으로 지운 뒤 **마지막에** `ctrl = OWN|FS|LS`.
   - `int32_t desc_reap(desc_t *d)` — OWN 이면 -1, 아니면 DMA 가 써 둔 status.
3. 주어진 pthread "DMA 엔진" 이 8칸 링으로 10만 패킷을 소비한다. 올바른 코드면 **매 실행** 손상 0 이어야 하고, TSan 도 조용해야 한다.

### Part B — CPU 층: 약한 메모리 순서 시뮬레이터

주어진 시뮬레이터에서 CPU store 는 store buffer 에 들어갔다가 **임의 순서로** 메모리에 도달하고(같은 주소끼리는 순서 유지), load 는 "지금보다 이른 시점" 의 값을 읽을 수 있다. `wm_wmb()` / `wm_rmb()` 가 각각 store 끼리 / load 끼리의 순서를 강제한다. `wm_cbar()` 는 컴파일러 배리어라 **아무 일도 안 한다**. 고정 시드 2000개로 돌린다.

- `void wm_tx_post(uint32_t n)` — 버퍼 2워드, ADDR, LEN 을 쓰고 CTRL=OWN. 장치가 OWN 을 본 순간 나머지가 전부 보여야 한다.
- `int wm_rx_reap(uint32_t *len, uint32_t *stat)` — 장치는 LEN → STAT → CTRL(OWN 해제) 순서로 쓴다. OWN==0 을 봤다면 LEN/STAT 도 새 값이어야 한다.
- 주어진 버그 버전(배리어 없음, 컴파일러 배리어만, rmb 없는 회수)이 위반을 내는 것을 테스트가 먼저 보여 준다.

### Part C — 드라이버 층: TX 링 + 캐시 + doorbell

4칸 TX 링 mock: 디스크립터는 non-cacheable SRAM(가정), 버퍼는 cacheable — CPU 가 쓴 값은 `cache_clean()` 을 해야 DMA 가 보는 RAM 에 도달한다. DMA 는 doorbell 레지스터 `TX_TAIL` 이 가리키는 곳까지만 일한다.

- `int eth_xmit(const uint8_t *p, uint32_t n)` — 잘못된 길이 -22, 링이 꽉 차면 -16 (둘 다 하드웨어를 건드리지 않는다). 아니면 복사 → clean → addr → len → 배리어 → OWN → head++ → `writel(&TX_TAIL, head)`. 기대 trace `"CPY CLEAN addr len WMB OWN WMB DB:1"`.
- `uint32_t eth_reclaim(void)` — tail 부터 OWN 이 내려간 것만 순서대로 회수, 첫 OWN 에서 멈춤, stat 으로 ok/err 집계. 기대 trace `"rC RMB rS"`.
- 랜덤 인터리빙(보내기 / DMA 진행 / 회수를 섞음) 40 프레임이 전부 바이트 단위로 맞아야 한다.

## 2. Problem (English)

> **Publishing a DMA descriptor: compiler order, CPU order, and the driver.**
>
> A TX descriptor holds `ctrl` (bit31 `OWN`, `FS`, `LS`), `len`, a buffer address and a `status` written back by the DMA engine. Software must make every descriptor field (and the payload) visible to the DMA engine *before* it can observe `OWN == 1`, and must not read `status`/`len` until after it has observed `OWN == 0`.
>
> A. Compile `demo_order.c` with `-O2 -S` for arm64 and `thumbv7em` (Cortex-M4) and compare five publish variants (no barrier, volatile `ctrl` only, `asm volatile("" ::: "memory")`, `atomic_signal_fence(memory_order_release)`, `atomic_thread_fence(memory_order_release)`). Then implement `int desc_publish(desc_t *d, const uint8_t *buf, uint32_t len)` and `int32_t desc_reap(desc_t *d)` with C11 atomics so that a DMA engine running on another thread never sees a stale field (100k packets, 8-entry ring, must pass under TSan).
>
> B. In a weak-memory simulator (stores drain from a store buffer in any order; loads may return values from an earlier time), write `wm_tx_post()` and `wm_rx_reap()` using `wm_st/wm_ld/wm_wmb/wm_rmb` so that 0 of 2000 seeded runs violate the publish/consume invariant. Explain why a compiler barrier alone fails.
>
> C. Implement `int eth_xmit(const uint8_t *p, uint32_t n)` (returns 0, `-22` bad length, `-16` ring full) and `uint32_t eth_reclaim(void)` for a 4-entry ring with a cacheable payload buffer and a `TX_TAIL` doorbell. Expected event trace: `"CPY CLEAN addr len WMB OWN WMB DB:1"` and `"rC RMB rS"`.
>
> Follow-ups: DMB vs DSB vs ISB? Why did the Cortex-M4 version "work" without a DMB? What changes on a Cortex-M7 with D-cache? Is a C11 fence a valid description of ordering against a DMA master at all?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
# 처음엔 "7/30 passed" — 버그 재현 3개, lock-free 확인 1개, 스텁의 -1/0 이 우연히 맞는 3개. TODO 6개를 채워 30/30
```

starter 의 실제 Part B 출력 (TODO 가 비어 있어 `mine` 은 2000개 전부 위반):

```text
Part B (2000 seeds): tx naive=656 cbar=656 mine=2000 | rx naive=1271 mine=2000
```

관찰용 파일 세 개 (하네스와 별개):

```bash
cc -std=c11 -O2 -S demo_order.c -o -                                                          # arm64 asm
clang -std=c11 --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_order.c -o -   # Cortex-M4 asm
clang -std=c11 --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -O2 -S demo_order.c -o - # M0+ 비교
cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_tsan.c -o demo_tsan && ./demo_tsan relaxed   # volatile|relaxed|fence|relacq
cc -std=c11 -O2 -pthread demo_litmus.c -o demo_litmus && ./demo_litmus 0 && ./demo_litmus 1   # 실제 하드웨어 재배치
```

과제 문장의 `asm volatile(...)` 는 GNU 확장 키워드라 `-std=c11` 에서는 이렇게 된다 (실제 출력):

```bash
printf 'void f(void){ asm volatile("" ::: "memory"); }\n' | cc -std=c11 -x c -c - -o /dev/null
```

```text
<stdin>:1:15: error: use of undeclared identifier 'asm'
    1 | void f(void){ asm volatile("" ::: "memory"); }
      |               ^~~
1 error generated.
```

`-std=gnu11` 이면 통과한다. 엄격 모드에서는 예약 식별자 철자 `__asm__ __volatile__("" ::: "memory")` 를 쓴다 (CMSIS 의 `__COMPILER_BARRIER()` 도 이 모양이다).

## 4. 힌트

<details>
<summary>힌트 1 — Part A: "마지막 쓰기" 하나에 순서를 싣는다</summary>

일반 필드는 평범하게 쓰고, OWN 이 든 `ctrl` 쓰기 **하나** 를 release 로 만들면 그 앞의 모든 쓰기가 같이 딸려 간다. 받는 쪽은 `ctrl` 읽기 하나를 acquire 로. `atomic_store_explicit` / `atomic_load_explicit` 의 세 번째·두 번째 인자를 보라. busy 검사(이미 OWN 인지)도 "읽은 뒤에 덮어쓴다" 는 순서가 필요하다.

</details>

<details>
<summary>힌트 2 — asm 을 읽을 때 볼 세 가지</summary>

① 소스에 있던 store 가 **없어졌는가** (dead store 제거) ② 두 접근이 **한 명령으로 합쳐졌는가** (`stp`/`ldp` — 한 명령 안의 두 워드 사이에는 순서가 없다) ③ **새 명령어** 가 생겼는가 (`dmb`, `stlr`, `ldapr`). `; InlineAsm Start` / `;MEMBARRIER` 는 주석일 뿐 명령어가 아니다.

</details>

<details>
<summary>힌트 3 — Part B: 배리어는 "앞 묶음" 과 "뒤 묶음" 사이에</summary>

`wm_wmb()` 는 그 앞의 store 전부가 뒤의 store 보다 먼저 도달하게 한다. OWN 하나만 뒤 묶음에 있으면 된다. 회수는 거꾸로 — `wm_rmb()` 는 앞선 load 가 본 시점보다 이른 값을 뒤 load 가 못 보게 한다. 그러니 CTRL 을 읽은 **뒤**, LEN 을 읽기 **전**.

</details>

<details>
<summary>힌트 4 — Part C: 캐시와 doorbell 은 배리어와 다른 문제다</summary>

`cache_clean()` 을 안 하면 DMA 는 RAM 의 옛 0 을 보낸다 — 배리어를 아무리 넣어도 소용없다. doorbell 은 Device 메모리 쓰기이고, OWN(일반 메모리) 뒤에 도달해야 한다 — `writel` 이 그 배리어를 품고 있다. 회수는 SW 인덱스 `c_tail != c_head` 로 범위를 정하고 OWN 으로 완료를 판단한다.

</details>

## 5. 풀이

### 0. 층별 책임 — 무엇이 무엇을 막나

| 도구 | ① 컴파일러 재배치 | ② CPU/버스 재배치 | ③ 캐시 | 생기는 명령 (실측) |
|---|---|---|---|---|
| `volatile` | volatile 접근 **끼리만** (5.1.2.3p6) | 아니오 | 아니오 | 없음 |
| `__asm__ __volatile__("":::"memory")` | 전부 (메모리 접근이 넘지 못함) | 아니오 | 아니오 | 없음 (`; InlineAsm Start/End`) |
| `atomic_signal_fence(order)` | 전부 (같은 스레드 + 시그널 핸들러 사이) | 아니오 | 아니오 | 없음 (`;MEMBARRIER`) |
| `atomic_thread_fence(release)` | 전부 | 예 | 아니오 | arm64 `dmb ish`, M4 `dmb sy` |
| `atomic_thread_fence(acquire)` | 전부 | 예 | 아니오 | arm64 `dmb ishld`, M4 `dmb sy` |
| `atomic_store_explicit(release)` | 그 store 앞의 접근 | 예 | 아니오 | arm64 `stlr`, M4 `dmb sy; str` |
| `atomic_load_explicit(acquire)` | 그 load 뒤의 접근 | 예 | 아니오 | arm64 `ldapr`, M4 `ldr; dmb sy` |
| `SCB_CleanDCache_by_Addr` / `Invalidate` | (함수 호출) | 내부 DSB | 예 | `DCCMVAC`/`DCIMVAC` 레지스터 쓰기 |

`atomic_signal_fence(memory_order_relaxed)` 는 **아무 효과가 없다** (fence 의 relaxed 는 no-op, 7.17.4.1). 컴파일러 배리어로 쓰려면 release/acquire/seq_cst 를 줘야 한다.

### Part A — 실제 -O2 명령 순서 비교 (`demo_order.c`)

시나리오: `ctrl = 0`(회수) → `txbuf[0..1]` → `addr` → `len` → `ctrl = OWN|n`. 아래는 실제 출력에서 주소 계산 줄(`adrp/ldr ...@GOTPAGE`, `movw/movt`, `ldr rN, .LCPI`)과 상수 `mov` 줄만 뺀 것이다.

**arm64 (Apple clang 21, 호스트):**

```text
_p0_plain:                   ; ① 배리어도 volatile 도 없음
	strb	w9, [x8]            ; txbuf[0]
	strb	w0, [x8, #1]        ; txbuf[1]
	stp	w0, w8, [x9, #4]    ; len, addr 를 한 명령으로
	orr	w8, w0, #0x80000000
	str	w8, [x9]            ; ctrl = OWN|n      ← 맨 앞의 ctrl = 0 은 사라졌다
_p1_volatile:                ; ② ctrl 만 volatile
	str	wzr, [x8]           ; ctrl = 0 (살아남음)
	strb	w10, [x9]
	strb	w0, [x9, #1]
	stp	w0, w9, [x8, #4]
	orr	w9, w0, #0x80000000
	str	w9, [x8]            ; ctrl = OWN|n
_p2_asm_barrier:  ... stp w0, w9, [x8, #4] / ; InlineAsm Start / ; InlineAsm End / orr / str w9, [x8]
_p3_signal_fence: ... stp w0, w9, [x8, #4] / ;MEMBARRIER / orr / str w9, [x8]
_p4_thread_fence: ... stp w0, w9, [x8, #4] / dmb ish / orr / str w9, [x8]
```

**Cortex-M4 (`--target=thumbv7em-none-eabi`):**

```text
p1_volatile:                         p3_signal_fence:                    p4_thread_fence:
	str	r2, [r1]       ; ctrl=0         str	r2, [r1]                      str	r2, [r1]
	strb	r0, [r2, #1]   ; txbuf[1]      strb	r0, [r2, #1]                  strb	r3, [r2]
	str	r0, [r1, #4]   ; len           str	r0, [r1, #4]                  strb	r0, [r2, #1]
	orr	r0, r0, #-2147483648           orr	r0, r0, #-2147483648          str	r2, [r1, #8]
	strb	r3, [r2]       ; txbuf[0]      strb	r3, [r2]                      str	r0, [r1, #4]
	str	r2, [r1, #8]   ; addr          str	r2, [r1, #8]                  orr	r0, r0, #-2147483648
	str	r0, [r1]       ; ctrl=OWN      @MEMBARRIER                        dmb	sy
	bx	lr                               str	r0, [r1]                      str	r0, [r1]
```

**Cortex-M0+ (`thumbv6m`) — 배리어 있는 버전에서도:**

```text
p2_asm_barrier:
	strb	r0, [r1, #1]       ; txbuf[1]   ← 소스에서는 ctrl = 0 "뒤" 에 있던 store
	strb	r2, [r1]           ; txbuf[0]
	str	r3, [r2]           ; ctrl = 0   (volatile)
	str	r1, [r2, #8]
	str	r0, [r2, #4]
	@APP
	@NO_APP
	str	r1, [r2]           ; ctrl = OWN|n
```

읽는 법:

| 관찰 | 의미 |
|---|---|
| p0: `ctrl = 0` 이 사라짐 (arm64, M4 둘 다) | 일반 변수에 대한 dead store 제거. "먼저 OWN 을 내려 DMA 에서 회수" 하려던 의도가 증발한다 (C-B09 의 `data = 1` 실종과 같은 현상) |
| p0/p1 arm64: `len`, `addr` 가 `stp` 한 명령 | 한 명령 안 두 워드 사이에 아키텍처가 정한 관찰 순서는 없다 — 일반 필드끼리는 원래 순서가 없어도 되니 문제는 아니다 |
| p1/p3 M4: `txbuf[0]` 이 `len` 뒤로 감 | 일반 store 끼리는 컴파일러가 자유롭게 재배치한다 |
| M0+ p2/p3/p4: `txbuf` store 가 volatile `ctrl = 0` **앞으로** 올라감 | **일반 store 가 volatile store 를 넘었다.** 배리어는 len 과 OWN 사이에만 있었으므로 그 위쪽은 자유 구역이다 |
| p1–p3 모든 타깃: OWN store 앞에 새 명령 없음 | volatile, `asm memory`, `signal_fence` 는 **명령어 0개**. 순서는 "명령어 순서" 까지만 고정된다 |
| p4: `dmb ish` (arm64) / `dmb sy` (M4) | `thread_fence` 만 CPU 에게도 순서를 강제한다 |

이번 실측에서 clang 은 OWN 을 쓰는 volatile store **아래로** 일반 store 를 내리지는 않았다 (p1 이 우연히 맞는 이유). 하지만 그건 이 버전 clang 의 스케줄링 결과일 뿐이다. 표준은 일반 접근을 volatile 접근 너머로 옮기는 것을 금지하지 않고 (5.1.2.3p6 는 volatile 접근 **자체** 만 추상 기계대로 평가하라고 한다), M0+ 출력처럼 반대 방향으로는 실제로 넘었다. 다른 컴파일러·버전·LTO·인라이닝에서는 OWN 아래로 내려갈 수 있다.

**회수 쪽** (`if (ctrl & OWN) return -1; return len;`):

```text
arm64 r0_plain:     ldp	w9, w8, [x8]          ; ctrl 과 len 을 한 명령으로 — 순서 개념 자체가 없음
arm64 r1_volatile:  ldr	w9, [x8]              ; ctrl
                    ldr	w8, [x8, #4]          ; len — 분기 전에 "무조건" 읽음 (csinv 로 선택)
arm64 r2_asm:       ldr w9,[x8] / tbnz w9,#31 / ldr w0,[x8,#4]    ; 분기 뒤로 갔지만 명령 순서뿐
arm64 r3_fence:     ldr w9,[x8] / tbnz w9,#31 / dmb ishld / ldr w0,[x8,#4]
M4    r3_fence:     ldr r1,[r0] / cmp / itee mi / movmi / dmbpl sy / ldrpl r0,[r0,#4]
```

`r1_volatile` 이 핵심이다. 컴파일러가 if 를 `csinv` 로 바꾸면서 **len 을 OWN 확인 전에 읽어 버린다** — 소스의 제어 흐름이 사라졌다. 컴파일러 배리어(`r2`)를 넣으면 분기가 살아나지만, ARM 은 **제어 의존성(branch)만으로 load→load 순서를 보장하지 않는다** — CPU 는 분기를 예측하고 len 을 미리 읽을 수 있다. `dmb ishld` 가 있어야 한다.

**C11 원자 버전** (`c_release`, `c_acquire`):

```text
arm64:  stp w0, w9, [x8, #4] / orr / stlr w9, [x8]        |  ldapr w9, [x8] / ldr w8, [x8, #4] / cmn / csinv
M4:     str r2,[r1,#8] / str r0,[r1,#4] / orr / dmb sy / str r0,[r1]   |  ldr r1,[r0] / dmb sy / ldr r0,[r0,#4] / ...
M0+:    bl __atomic_store_4  (+ warning: ... exceeds the max lock-free size (0 bytes))
```

- arm64 의 release store 는 `stlr` (store-release), acquire load 는 **`ldapr`** — Apple 타깃(ARMv8.3 RCpc)이라 `ldar` 보다 약한 RCpc acquire 를 쓴다. C11 의 acquire 에는 이것으로 충분하다 (seq_cst load 라면 `ldar`).
- M4 의 release store 는 **`dmb sy` + `str`** — CMSIS 로 `__DMB(); d->ctrl = OWN;` 이라고 쓴 것과 같은 명령이다. 위 p4(thread_fence + volatile store)의 OWN 앞도 똑같이 `dmb sy` + `str` 이다.
- seq_cst 는 더 비싸다 (`c_seq_cst_store/load` 실측): arm64 store `stlr`, load `ldar`. M4 store `dmb sy; str; dmb sy`, load `ldr; dmb sy`. 순서가 한 방향만 필요하면 release/acquire 를 명시하는 이유다.
- M0+ 는 `_Atomic uint32_t` 를 lock-free 로 보지 않아 load/store 가 `__atomic_*_4` **라이브러리 호출** 이 된다 (C-M14 에서 본 것과 같은 이유). M0+ 펌웨어에서는 `volatile` + `__DMB()` 가 현실적인 답이다. `atomic_thread_fence` 자체는 M0+ 에서도 `dmb sy` 한 줄이었다.

### Part A — 올바른 버전과 그 근거

```c
static int desc_publish(desc_t *d, const uint8_t *buf, uint32_t len) {
    if (len == 0u || len > A_MAXLEN) return -1;
    if (atomic_load_explicit(&d->ctrl, memory_order_acquire) & OWN) return -1;
    d->buf = buf; d->len = len; d->status = 0u;
    atomic_store_explicit(&d->ctrl, OWN | FS | LS, memory_order_release);
    return 0;
}
static int32_t desc_reap(desc_t *d) {
    if (atomic_load_explicit(&d->ctrl, memory_order_acquire) & OWN) return -1;
    return (int32_t)d->status;
}
```

근거를 C11 로 말하면:

1. **엔진과 CPU 가 둘 다 스레드라면** `len`/`buf`/`status` 는 일반 객체이므로, 한쪽의 쓰기와 다른 쪽의 읽기 사이에 happens-before 가 없으면 data race = UB 다 (5.1.2.4p25). volatile 은 원자 객체가 아니어서 이 관계를 만들지 못한다.
2. release store `X` 와, 그 값을 읽는 acquire load `Y` 가 **같은 원자 객체** `ctrl` 위에서 만나면 X synchronizes-with Y (5.1.2.4p11). X 앞(sequenced-before)의 필드 쓰기는 Y 뒤의 필드 읽기보다 happens-before → 엔진은 반드시 새 필드를 본다.
3. fence 버전도 같다: release fence A → relaxed store X (`ctrl`) … relaxed load Y 가 X 의 값을 읽음 → acquire fence B 이면 A synchronizes-with B (7.17.4p2). 단 **원자 객체 `ctrl` 을 통한 store/load 쌍이 반드시 있어야** 한다. `atomic_thread_fence` + `volatile` 대입은 C11 상 아무것과도 동기화하지 않는다 (asm 은 같아 보여도).
4. **busy 검사의 acquire** 는 반대 방향(WAR)이다. 엔진은 `len`·`buf` 를 다 읽은 뒤 release 로 `ctrl = 0` 을 쓴다. 우리가 그 0 을 acquire 로 읽어야 "엔진의 읽기 → 우리의 덮어쓰기" 순서가 선다. relaxed 로 읽으면 아직 엔진이 읽고 있는 `len` 을 덮어쓰는 race 가 된다. 버퍼 재사용도 같은 이유로 회수(acquire) 뒤에만 한다 (`run_threaded` 의 순서).

**그런데 DMA 는 C 스레드가 아니다.** 진짜 DMA 컨트롤러는 C 추상 기계 밖의 버스 마스터라, C11 은 장치가 무엇을 보는지에 대해 아무 말도 하지 않는다. C11 원자 연산이 DMA 에도 "동작하는" 이유는 컴파일러가 그 자리에 ① 컴파일러 배리어 효과와 ② `dmb` 를 내기 때문이고, 그 `dmb` 가 장치에게도 충분한지는 **아키텍처 문서** 가 정한다. arm64 에서 C11 fence 는 `dmb ish`(inner shareable 도메인)를 내는데, Linux 의 `dma_wmb()` 는 장치가 속한 outer shareable 도메인용 `dmb oshst` 를 쓴다 — 그래서 커널은 C11 원자를 DMA 순서에 쓰지 않고 `dma_wmb()/dma_rmb()` 를 따로 둔다. Cortex-M 은 `DMB` 옵션이 사실상 `SY` 하나라 차이가 없다.

**테스트 결과** (실측):

```text
Part A: 100000 packets through 8-entry ring, engine bad=0, cpu bad=0
```

같은 핑퐁을 네 방식으로 동기화한 `demo_tsan.c` 의 실제 TSan 결과:

| 모드 | C11 판정 | TSan (이 맥, `-fsanitize=thread`) | 결과 줄 |
|---|---|---|---|
| `volatile` | data race (ctrl, len, buf 전부) | 경고 4개 (`v_ctrl` 2, `d_len`, `d_buf`) | `dma: 20000 packets, bad=0` |
| `relaxed` | data race (len, buf) | 경고 2개 | `bad=0` |
| `fence` | **올바름** (7.17.4p2) | 경고 2개 — **false positive** | `bad=0` |
| `relacq` | 올바름 | 경고 0, exit 0 | `bad=0` |

`relaxed` 모드의 첫 보고 (실제 출력 일부):

```text
WARNING: ThreadSanitizer: data race (pid=85155)
  Read of size 4 at 0x000102600004 by thread T1:
    #0 dma demo_tsan.c:47 (C-A02_dt:arm64+0x100000978)

  Previous write of size 4 at 0x000102600004 by main thread:
    #0 main demo_tsan.c:59 (C-A02_dt:arm64+0x100000760)

  Location is global 'd_len' at 0x000102600004 (C-A02_dt+0x100008004)
```

`fence` 모드는 위와 **똑같은** 두 보고를 낸다 — TSan 은 독립(standalone) `atomic_thread_fence` 를 동기화로 모델링하지 않는다 (알려진 한계). 그래서 solution 은 TSan 이 이해하는 release store / acquire load 로 썼고, TSan 아래에서도 조용하다. 보고가 있으면 이 맥에서는 `exit=134` (SIGABRT)로 끝났다. 네 모드 모두 `bad=0` 이었다는 점도 중요하다: **race 가 있어도 값이 깨지는 걸 관찰하지 못할 수 있다.** "테스트가 통과했다" 는 순서가 맞다는 증거가 아니다.

### Part B — CPU 층: 왜 컴파일러 배리어로는 부족한가

명령어가 소스 순서대로 나와도, 약한 메모리 모델의 CPU(ARMv8-A, Cortex-M7 같은 AXI 버스 코어)는 store 를 store buffer 에 쌓았다가 **다른 순서로** 메모리에 도달시킬 수 있고, load 를 **미리** 실행할 수 있다. 시뮬레이터는 이 두 가지를 아주 단순하게 흉내 낸다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">store buffer: 같은 묶음(epoch) 안에서는 아무 순서로나 메모리에 도달한다</text>
  <text x="10" y="54" fill="currentColor">프로그램 순서</text>
  <g stroke="currentColor">
    <rect x="10" y="64" width="80" height="28" class="f-mut"/><rect x="95" y="64" width="80" height="28" class="f-mut"/>
    <rect x="180" y="64" width="80" height="28" class="f-mut"/><rect x="265" y="64" width="80" height="28" class="f-mut"/>
    <rect x="350" y="64" width="80" height="28" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="22" y="83">BUF0</text><text x="107" y="83">BUF1</text><text x="192" y="83">ADDR</text>
    <text x="277" y="83">LEN</text><text x="362" y="83">CTRL=OWN</text>
  </g>
  <text x="450" y="83" fill="currentColor">wmb 없음: 전부 epoch 0</text>
  <text x="10" y="128" fill="currentColor">가능한 도달 순서 하나</text>
  <g stroke="currentColor">
    <rect x="10" y="138" width="80" height="28" class="f-mut"/><rect x="95" y="138" width="80" height="28" class="f-warn"/>
    <rect x="180" y="138" width="80" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="22" y="157">BUF0</text><text x="107" y="157">CTRL=OWN</text><text x="192" y="157">ADDR ...</text>
  </g>
  <text x="280" y="157" fill="currentColor">→ DMA 가 OWN 을 보고 LEN=0, BUF1=0 을 읽는다</text>
  <text x="10" y="204" fill="currentColor">wm_wmb() 를 OWN 앞에</text>
  <g stroke="currentColor">
    <rect x="10" y="214" width="335" height="28" class="f-ok"/>
    <rect x="370" y="214" width="80" height="28" class="f-acc"/>
  </g>
  <text x="20" y="233" fill="currentColor">epoch 0: BUF0 BUF1 ADDR LEN (이 안에선 순서 자유)</text>
  <text x="352" y="233" fill="currentColor">→</text>
  <text x="382" y="233" fill="currentColor" font-family="monospace" font-size="12">CTRL=OWN</text>
  <text x="470" y="233" fill="currentColor">epoch 1 은 epoch 0 이 다 빠진 뒤</text>
  <text x="10" y="276" fill="currentColor">wm_cbar() (컴파일러 배리어)는 명령어가 없으므로 epoch 를 나누지 못한다 → 위반 수가 "배리어 없음" 과 똑같다.</text>
</svg>
```

실측 (2000 시드, 위반 = 장치가 OWN 을 봤을 때 다른 필드가 기대값이 아님 / 회수 결과가 낡은 값):

```text
Part B (2000 seeds): tx naive=656 cbar=656 mine=0 | rx naive=1271 mine=0
```

`naive` 와 `cbar` 가 **정확히 같은 656** 인 것이 이 Part 의 결론이다: 같은 시드에서 같은 난수를 같은 순서로 소비했으니, 컴파일러 배리어는 CPU 층에서 문자 그대로 아무것도 바꾸지 않았다.

회수 쪽은 load 의 시점 선택으로 생긴다:

```svg
<svg viewBox="0 0 740 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">장치는 t=1,2,3 에 LEN, STAT, CTRL=0 을 쓴다. load 는 [하한, 지금] 중 한 시점을 읽는다</text>
  <line x1="60" y1="70" x2="700" y2="70" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="55" y="60">t=0</text><text x="215" y="60">t=1 LEN=n</text><text x="375" y="60">t=2 STAT=0</text><text x="535" y="60">t=3 CTRL=0</text>
  </g>
  <g stroke="currentColor"><path d="M60 64 L60 76 M220 64 L220 76 M380 64 L380 76 M540 64 L540 76"/></g>
  <rect x="520" y="90" width="200" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="530" y="109" fill="currentColor">ld CTRL → t=3 (OWN 해제 봄)</text>
  <rect x="40" y="128" width="200" height="28" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="50" y="147" fill="currentColor">ld LEN → t=0 → 0xDEAD (낡음)</text>
  <text x="260" y="147" fill="currentColor">rmb 없음: LEN 의 하한은 0 이라 과거를 읽을 수 있다</text>
  <rect x="520" y="166" width="200" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="530" y="185" fill="currentColor">ld LEN → t≥3 → n</text>
  <text x="40" y="185" fill="currentColor">wm_rmb(): 하한 = 앞선 load 가 본 가장 늦은 시점(3) →</text>
  <text x="10" y="226" fill="currentColor">CTRL=0 을 t=3 에 봤다면 LEN(t=1)·STAT(t=2) 은 그보다 앞에 쓰였으므로 하한 3 이상에서는 반드시 새 값이다.</text>
</svg>
```

이 모델은 **교육용 동작 모델** 이다. 실제 ARMv8 메모리 모델(공리 모델, herd7 의 `aarch64.cat`)에는 주소/데이터 의존성이 load 순서를 보장하는 규칙, multi-copy atomicity 등이 더 있다. 여기서는 "배리어가 없으면 허용되는 결과" 의 대표만 재현한다.

**진짜 하드웨어에서도 일어나나?** `demo_litmus.c` 는 MP(message passing) 리트머스다: 한 스레드가 `data[i]=1; flag[i]=1`, 다른 스레드가 `f=flag[i]; d=data[i]`, 모두 relaxed 원자(=UB 없음). 생성된 asm 은 소스 순서 그대로였다 (`str data` → `str flag`, `ldr flag` → `ldr data`). 이 맥(`sysctl machdep.cpu.brand_string` = `Apple M6`)에서 3회씩 실행한 실제 출력:

```text
mode=0 (no fence): flag=1 seen 50576128 times, of which data==0: 3
mode=0 (no fence): flag=1 seen 48022513 times, of which data==0: 35
mode=0 (no fence): flag=1 seen 43212871 times, of which data==0: 5911
mode=1 (rel/acq fence): flag=1 seen 27814923 times, of which data==0: 0
mode=1 (rel/acq fence): flag=1 seen 21932898 times, of which data==0: 0
mode=1 (rel/acq fence): flag=1 seen 29834336 times, of which data==0: 0
```

fence 없이 "flag 는 1 인데 data 는 0" 이 실제로 나왔다 — 컴파일러가 아니라 CPU 가 바꾼 것이다. 빈도는 3 ~ 5911 로 실행마다 수천 배 차이 난다. fence 버전의 0 은 "이번 세 번에는 안 나왔다" 일 뿐 증명이 아니다 (증명은 `dmb` 의 아키텍처 의미가 한다). 이것이 체크 테스트에 하드웨어 리트머스를 쓰지 않은 이유다.

**Cortex-M 현실:** Cortex-M0/M0+/M3/M4 는 순서대로 실행하는 단순 파이프라인이라 일반 메모리 접근 순서를 실제로 바꾸는 경우가 드물고, ARM AN321(메모리 배리어 가이드)도 이 코어들에서는 많은 경우 배리어를 빼도 문제가 드러나지 않는다고 설명한다. 그래도 **아키텍처(ARMv7-M)는 보장하지 않으므로** 이식성을 위해 넣으라는 것이 같은 문서의 권고다. Cortex-M7(AXI, 쓰기 버퍼, D-cache), Cortex-M55/M85, 그리고 Cortex-A/R 에서는 실제로 필요하다. "M4 에서 됐다" 는 M7 으로 옮기는 순간 깨지는 코드의 전형이다.

### Part C — 드라이버: 캐시, 배리어, doorbell 의 순서

```c
static int eth_xmit(const uint8_t *p, uint32_t n) {
    if (n == 0u || n > 64u) return -22;
    if (c_head - c_tail == RING) return -16;      /* free-running 인덱스 */
    uint32_t i = c_head % RING;
    buf_fill(i, p, n);            /* CPY   : 데이터는 아직 D-cache 에만        */
    cache_clean(i, n);            /* CLEAN : RAM 으로 (CMSIS 함수는 끝에 DSB)  */
    d_set_addr(&c_desc[i], BUF_PA(i));
    d_set_len(&c_desc[i], n);
    dma_wmb();                    /* WMB   : 필드 → OWN                        */
    d_set_ctrl(&c_desc[i], OWN | FS | LS);
    c_head++;
    writel(&TX_TAIL, c_head);     /* WMB DB: OWN → doorbell (writel 안의 배리어) */
    return 0;
}
```

| trace 토큰 | 빠뜨리면 | 이 mock 에서 드러나는 방식 |
|---|---|---|
| `CLEAN` | DMA 가 RAM 의 옛 데이터를 보냄 | 로그가 0 으로 채워져 `memcmp` FAIL |
| 첫 `WMB` | DMA 가 OWN 을 먼저 보고 옛 addr/len 사용 | 단일 스레드 mock 은 못 잡음 → trace 비교로 잡음 (Part B 가 행동으로 보여 줌) |
| `writel` 의 `WMB` | doorbell 이 OWN 보다 먼저 도착 → DMA 가 깨어나 OWN=0 을 보고 "descriptor unavailable" 로 다시 잠듦. 프레임이 다음 doorbell 까지 멈춘다 | trace |
| doorbell 자체 | DMA 가 일을 안 함 | `dev_run` 이 아무것도 안 함 → FAIL |

**왜 DMB 인가, DSB 인가?** `DMB` 는 "앞의 메모리 접근이 뒤의 메모리 접근보다 먼저 **관찰** 되게" 한다. 일반 메모리(디스크립터)와 Device 메모리(doorbell) 쓰기 사이의 순서도 DMB 로 충분하다 — Linux arm64 의 `writel()` 이 쓰는 배리어(`__iowmb()` = `dma_wmb()` = `dmb oshst`)가 그 예다. `DSB` 는 "앞의 접근(과 캐시 유지보수 연산)이 **완료** 될 때까지 다음 명령을 실행하지 않는다" — 캐시 clean 이 끝났음을 보장하거나, 시스템 제어 레지스터 쓰기의 효과를 기다릴 때 쓴다. CMSIS 의 `SCB_CleanDCache_by_Addr()` 는 내부에서 `__DSB()` 를 하므로 trace 의 `CLEAN` 뒤에 따로 넣지 않았다. `ISB` 는 파이프라인을 비워 이후 명령어를 다시 가져오게 한다 (VTOR/MPU/CONTROL 변경 뒤).

**캐시 정렬:** clean/invalidate 는 캐시 라인(Cortex-M7 은 32 B) 단위다. DMA 버퍼가 라인 경계에 맞지 않으면, 같은 라인에 있는 다른 변수를 CPU 가 쓴 뒤의 clean 이나 RX invalidate 가 이웃 데이터를 덮거나 날린다. DMA 버퍼는 32 B 정렬 + 크기를 32 B 배수로 (C-A05 의 정렬 할당). 디스크립터는 보통 MPU 로 non-cacheable 영역에 둔다 (STM32H7 예제들이 ETH 디스크립터를 D2 SRAM `0x30040000` 근처에 두고 MPU 속성을 바꾸는 이유 — 이 레슨의 `BUF_PA` 숫자도 거기서 왔다).

**회수:**

```c
while (c_tail != c_head) {
    hwdesc_t *d = &c_desc[c_tail % RING];
    if (d_get_ctrl(d) & OWN) break;   /* rC  */
    dma_rmb();                        /* RMB */
    if (d_get_stat(d) == 0u) c_ok++; else c_err++;   /* rS */
    c_tail++;
}
```

`c_tail != c_head` 를 먼저 보는 이유: 보낸 적 없는 칸은 OWN=0 이라 "완료" 로 오인된다 (테스트 "보낸 게 없으면 읽지도 않는다"). 링이 꽉 찼는지는 OWN 이 아니라 SW 인덱스(`head - tail == RING`)로 판단한다 — 하드웨어 읽기가 없고, free-running `uint32_t` 뺄셈은 2^32 랩어라운드에도 맞다 (부호 없는 산술, 6.2.5p9).

**Linux / CMSIS / Zephyr 대응:**

| 의도 | Linux | CMSIS (Cortex-M) | Linux arm64 실제 명령 |
|---|---|---|---|
| 컴파일러만 | `barrier()` | `__COMPILER_BARRIER()` | 없음 |
| CPU 끼리 (SMP) | `smp_wmb()` / `smp_rmb()` / `smp_store_release()` | `__DMB()` | `dmb ishst` / `dmb ishld` / `stlr` |
| CPU ↔ DMA (일관 메모리) | `dma_wmb()` / `dma_rmb()` | `__DMB()` | `dmb oshst` / `dmb oshld` |
| 완료 대기 | `wmb()` / `rmb()` / `mb()` | `__DSB()` | `dsb st` / `dsb ld` / `dsb sy` |
| MMIO 쓰기 (+ 앞 배리어) | `writel()` / `writel_relaxed()`(배리어 없음) | (직접 `__DMB()` + volatile) | `dmb oshst; str` / `str` |

Zephyr 는 `barrier_dmem_fence_full()`(DMB), `barrier_dsync_fence_full()`(DSB), `barrier_isync_fence_full()`(ISB) 을 쓴다. Linux 커널 문서 `memory-barriers.txt` 의 `dma_wmb()/dma_rmb()` 절에 이 레슨과 같은 "OWN 확인 → dma_rmb → 읽기/수정 → dma_wmb → OWN 넘김 → doorbell" 예제가 있다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution     # 30/30 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o sol_san && ./sol_san   # 30/30, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o sol_tsan && ./sol_tsan     # 30/30, 보고 없음 (ASan 과 따로)
```

실제 출력 (세 빌드 모두 같다):

```text
Part A: 100000 packets through 8-entry ring, engine bad=0, cpu bad=0
Part B (2000 seeds): tx naive=656 cbar=656 mine=0 | rx naive=1271 mine=0
30/30 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| "ctrl 을 volatile 로 했으니 끝" | volatile 은 volatile 접근끼리의 **컴파일러** 순서만. 일반 필드는 넘나들 수 있고(M0+ 실측), CPU 순서는 전혀 무관 | `dma_wmb(); d->ctrl = OWN;` 또는 release store |
| 디스크립터 전체를 volatile 로 | 컴파일러 층은 해결되지만(모든 접근이 volatile 끼리) ② CPU 층은 그대로. 게다가 모든 접근이 느려지고 `memcpy` 를 못 쓴다 | 필드는 일반 쓰기 + OWN 앞에 배리어 하나 |
| `asm volatile("":::"memory")` 를 DMB 대용으로 | 명령어 0개 — 시뮬레이터에서 위반 수가 배리어 없음과 똑같이 656 | `__DMB()` / `atomic_thread_fence` |
| `atomic_signal_fence(memory_order_relaxed)` | relaxed fence 는 no-op — 컴파일러 배리어조차 아니다 | release/acquire/seq_cst 로 |
| 배리어를 OWN **뒤에** (`ctrl = OWN; dma_wmb();`) | 막아야 할 두 쓰기 "사이" 가 아니다. 시뮬레이터 실측 643/2000 위반 | 필드 → 배리어 → OWN |
| 배리어를 버퍼 뒤·필드 앞에만 | ADDR/LEN 과 OWN 은 같은 묶음에 남음. 실측 540/2000 위반 | 배리어는 OWN 바로 앞 |
| 회수에서 `rmb` 를 CTRL 읽기 **앞에** | CTRL 과 LEN 사이를 막지 않는다. 실측 420/2000 위반 | CTRL 읽기 → rmb → LEN |
| `if (ctrl & OWN) return; len = d->len;` 의 분기만 믿음 | 컴파일러가 if 를 `csinv` 로 바꿔 len 을 먼저 읽음(r1 실측), 컴파일러가 안 바꿔도 ARM 은 제어 의존만으론 load 순서 보장 안 함 | `dma_rmb()` / acquire load |
| fence + volatile 대입으로 C11 동기화를 주장 | C11 fence 규칙(7.17.4)은 원자 객체를 통한 store/load 쌍을 요구. volatile 은 원자가 아니다 | `_Atomic` + release/acquire, 또는 "DMA 는 C 밖" 이라는 전제로 아키텍처 배리어 |
| busy 검사를 relaxed 로 하고 버퍼를 덮어씀 | 엔진의 마지막 읽기와 우리의 쓰기 사이 순서 없음 (WAR race) | acquire 로 OWN==0 확인 후 덮어쓰기 |
| cache clean 없이 OWN | 배리어는 캐시에서 RAM 으로 데이터를 밀어 주지 않는다 | `SCB_CleanDCache_by_Addr(buf, len)` 후 디스크립터 |
| RX 데이터를 invalidate 없이 읽음 | CPU 가 캐시의 낡은 줄을 읽음 (연습 3: 0xEE) | rmb → invalidate → 읽기 |
| doorbell 을 OWN 보다 먼저 / `writel_relaxed` 로 배리어 없이 | DMA 가 깨어나 OWN=0 을 보고 잠듦 → 다음 doorbell 까지 지연 (간헐적 TX 정지) | OWN → `writel()` |
| "TSan 이 fence 버전을 race 라 하니 fence 가 틀렸다" | TSan 은 독립 fence 를 모델링하지 않는다 (demo_tsan 실측) | release store/acquire load 로 쓰거나 해당 위치를 억제 |

## 8. 면접 Q&A

<details>
<summary>Q1. DMA 디스크립터의 OWN 비트를 volatile 로 쓰면 충분하지 않나요?</summary>

아니다, 두 층에서 모자란다. ① 컴파일러: volatile 은 volatile 접근끼리의 순서와 존재만 보장한다(5.1.2.3p6). 일반 필드(addr/len/버퍼)는 그 앞뒤로 옮겨질 수 있다 — Cortex-M0+ 용 clang 출력에서 버퍼 store 가 volatile store 위로 올라간 것을 실제로 봤다. ② CPU/버스: 명령 순서가 맞아도 쓰기 버퍼·버스가 다른 순서로 도달시킬 수 있다 (Apple Silicon 리트머스에서 실측). 고침: 필드 → `dma_wmb()`(=DMB) → OWN. D-cache 가 있으면 그 앞에 clean. 꼬리: "디스크립터 전체를 volatile 로?" → ① 만 고쳐진다. ② 는 여전히 배리어가 필요.

</details>

<details>
<summary>Q2. asm volatile("" ::: "memory"), atomic_signal_fence, atomic_thread_fence 는 각각 무엇을 막나요?</summary>

앞의 둘은 **컴파일러 배리어** 다. 메모리 접근을 그 지점 너머로 옮기거나, 레지스터에 캐시한 값을 재사용하지 못하게 하지만 명령어는 0개다 (실측: `; InlineAsm Start/End`, `;MEMBARRIER`). `atomic_signal_fence` 는 표준판으로 "같은 스레드와 그 스레드에서 실행되는 시그널 핸들러 사이" 의 순서만 다룬다(7.17.4.2) — 단일 코어 MCU 의 main↔ISR 이 정확히 이 경우다. `atomic_thread_fence` 는 다른 스레드/코어에게도 순서를 보장해야 하므로 CPU 배리어 명령을 낸다 (arm64 release→`dmb ish`, acquire→`dmb ishld`, M4→`dmb sy`). 꼬리: "asm memory 는 지역 변수도 막나?" → 주소가 밖으로 새지 않은 지역 변수는 레지스터에 있어도 되므로 영향받지 않는다. "memory" 클로버는 "임의의 (주소가 노출된) 메모리를 읽고 쓸 수 있다" 는 가정이다.

</details>

<details>
<summary>Q3. DMB, DSB, ISB 의 차이와 각각 언제 쓰나요?</summary>

DMB: 앞의 메모리 접근이 뒤의 메모리 접근보다 먼저 관찰되게 (순서). 디스크립터 필드 → OWN, OWN → doorbell, 플래그 → 데이터. DSB: 앞의 메모리 접근·캐시/TLB 유지보수가 **완료** 될 때까지 뒤의 명령을 실행하지 않음. 캐시 clean 완료 후 DMA 시작, `WFI` 전, ISR 끝에서 주변장치 플래그 클리어가 도착했음을 보장할 때(C-B09), 저전력 진입 전. ISB: 파이프라인 플러시 → 이후 명령을 새 설정으로 다시 가져옴. VTOR·MPU·CONTROL 변경 직후 (보통 DSB + ISB). 꼬리: "OWN → doorbell 에 DSB 를 써도 되나?" → 맞긴 하지만 더 비싸다. 순서만 필요하면 DMB 로 충분하다 (Linux arm64 `writel` 은 `dmb oshst`).

</details>

<details>
<summary>Q4. 우리 Cortex-M4 드라이버는 DMB 없이 몇 년째 잘 돌았습니다. 왜 넣어야 하죠?</summary>

M3/M4 는 단순한 순서 실행 파이프라인이라 실제 재배치가 드러나는 경우가 드물고, ARM AN321 도 그렇게 설명한다. 그래서 "돌았다". 그러나 ① 아키텍처(ARMv7-M)가 보장하지 않는다 — 같은 코드를 M7(AXI, 쓰기 버퍼, D-cache)이나 M33/M55 로 옮기면 깨질 수 있다. ② 컴파일러 층 문제는 코어와 무관하다 — 배리어 매크로(`__DMB()`)는 "memory" 클로버를 가진 asm 이라 컴파일러 재배치도 같이 막는다. 배리어가 없으면 컴파일러 버전·최적화 옵션·LTO 가 바뀔 때 깨질 수 있다. ③ 비용은 DMB 한 개(몇 사이클)이고 프레임당 한두 번이다. 그러니 넣고, 위치를 코드 리뷰로 고정한다.

</details>

<details>
<summary>Q5. D-cache 가 있는 Cortex-M7 에서 TX/RX DMA 버퍼는 어떻게 다루나요?</summary>

TX: CPU 가 버퍼를 다 쓴 뒤 `SCB_CleanDCache_by_Addr(buf, len)` (내부 DSB) → 디스크립터 → DMB → OWN → doorbell. RX: 버퍼를 DMA 에 넘기기 전에 invalidate(또는 clean+invalidate) — 더티 라인이 나중에 evict 되어 DMA 가 쓴 데이터를 덮는 것을 막는다. 완료 후 OWN==0 확인 → DMB → `SCB_InvalidateDCache_by_Addr` → 읽기 — 예측 실행이 그 사이 라인을 다시 가져왔을 수 있으므로 완료 후 invalidate 가 필수다. 버퍼는 32 B 라인 정렬·크기 32 B 배수 (C-A05). 디스크립터는 MPU 로 non-cacheable(또는 shared device) 영역에 두는 것이 보통이다 — 디스크립터를 캐시에 두면 OWN 과 필드가 같은 라인이라 clean 순서가 "라인 단위" 가 되어 추론이 어렵다.

</details>

<details>
<summary>Q6. C11 의 atomic_thread_fence 로 DMA 순서를 기술하는 게 맞나요?</summary>

엄밀히는 아니다. C11 메모리 모델은 C 스레드끼리의 happens-before 만 정의하고, DMA 컨트롤러는 C 추상 기계 밖이다. 게다가 fence 동기화(7.17.4)는 원자 객체를 통한 store/load 쌍을 요구하는데, 장치는 원자 연산을 하지 않는다. C11 원자가 DMA 에서 "동작하는" 이유는 컴파일러가 그 자리에 컴파일러 배리어 + `dmb` 를 내기 때문이고, 그 `dmb` 의 범위가 충분한지는 아키텍처가 정한다 — arm64 의 C11 fence 는 `dmb ish`(inner shareable)인데 Linux `dma_wmb()` 는 `dmb oshst`(outer shareable)다. 그래서 커널·RTOS·CMSIS 는 장치용 배리어를 별도 API 로 둔다. 실무 답: 드라이버는 플랫폼 배리어(`dma_wmb`, `__DMB`)를 쓰고, CPU 끼리의 공유는 C11 atomics 를 쓴다. 덤: M0+ 에서는 `_Atomic uint32_t` 가 libcall 이 된다 (실측).

</details>

<details>
<summary>Q7. 메모리 순서 버그는 어떻게 테스트하나요? 단위 테스트가 통과하면 된 건가요?</summary>

통과는 증거가 아니다 — demo_tsan 의 네 모드 모두 `bad=0` 이었고, 실제 리트머스는 같은 바이너리가 3번에서 5911번까지 흔들렸다. 계층별로: ① 컴파일러 층 — 생성된 asm 확인(이 레슨), 배리어를 매크로로 강제하는 코드 리뷰 규칙. ② 언어 층 — TSan (fence 는 false positive 주의), 모델 검사기(CDSChecker, GenMC). ③ 아키텍처 층 — herd7/`litmus7` 같은 리트머스 도구, 이 레슨 같은 결정적 시뮬레이터. ④ 드라이버 층 — mock HAL 로 이벤트 trace 를 기록해 "필드 → WMB → OWN → doorbell" 순서를 단정 (C-A01 의 mock 레지스터 방식). 실제 보드에서는 스트레스 + 오류 카운터(underflow, CRC) 모니터링.

</details>

<details>
<summary>Q8. arm64 에서 acquire load 가 ldar 가 아니라 ldapr 로 나왔습니다. 차이는?</summary>

`ldar` 는 RCsc acquire: 앞선 `stlr` 과도 순서를 지킨다 (seq_cst 구현에 필요). `ldapr`(ARMv8.3 FEAT_LRCPC)는 RCpc acquire: 뒤의 접근이 앞으로 못 오는 것은 같지만, 앞선 다른 주소의 `stlr` 보다 먼저 실행될 수 있다. C11 의 `memory_order_acquire` 는 이 약한 쪽으로 충분해서, Apple 기본 타깃(ARMv8.3 이상)을 아는 clang 이 `ldapr` 을 골랐다. `memory_order_seq_cst` load 는 `ldar` 가 된다. 꼬리: "store 는?" → release 와 seq_cst 모두 `stlr` (실측 `c_release`, `c_seq_cst_store`). seq_cst load 는 실측도 `ldar` 였다 (`c_seq_cst_load`).

</details>

## 9. 추가 연습

### 연습 1 — 묶음 전송: 배리어 1번, doorbell 1번

프레임 n 개를 한 번에 넣는 `xmit_burst()` 를 작성하라. 프레임마다 `WMB ... WMB DB` 를 반복하지 말고 배리어와 doorbell 을 각각 한 번만 쓴다. 링 빈칸이 모자라면 들어가는 만큼만, 0 이면 아무것도 건드리지 않는다. 기대 trace(2개): `"CPY CLEAN addr len CPY CLEAN addr len WMB OWN OWN WMB DB:2"`.

<details>
<summary>답 보기</summary>

모든 프레임의 버퍼·clean·addr·len 을 먼저 쓰고 → `dma_wmb()` 한 번 → OWN 들 → `writel` 한 번. 배리어 하나가 "앞의 모든 필드 → 뒤의 모든 OWN" 을 한 번에 보장하므로 프레임당 배리어가 필요 없다. doorbell 은 MMIO 쓰기라 버스 왕복이 비싸다 — NAPI/`xmit_more` 같은 doorbell 합치기(coalescing)가 고성능 NIC 드라이버의 기본기다. 단 OWN 을 먼저 본 DMA 가 doorbell 없이 폴링으로 출발하는 엔진이라면, 첫 프레임의 OWN 이 이미 보인 상태에서 나머지를 쓰는 건 괜찮다 (뒤 프레임의 필드는 배리어 앞에 이미 써졌다).

</details>

### 연습 2 — scatter-gather: 첫 세그먼트의 OWN 은 맨 마지막에

이미 돌고 있는 DMA(쓰기마다 링을 훑는 eager mock)에 50바이트 프레임을 32+18 두 세그먼트로 보낸다. 주어진 `sg_xmit_naive()` 는 세그먼트마다 `WMB → OWN` 을 순서대로 하는데 underflow 1회가 난다. 왜인가? underflow 0 인 `sg_xmit()` 을 작성하라.

<details>
<summary>답 보기</summary>

naive 는 첫 세그먼트(FS)의 OWN 을 세우는 순간 DMA 가 출발해, 아직 OWN 이 아닌 둘째 세그먼트를 만나 underflow 다. 배리어가 아무리 정확해도 **소유권 넘기는 순서** 가 틀렸다. 고침: 모든 세그먼트의 필드를 쓰고 둘째부터는 OWN 까지 세운 뒤 → `dma_wmb()` → 첫 세그먼트 OWN. DMA 는 첫 세그먼트에서 멈춰 있으므로 뒤 세그먼트의 OWN 이 먼저 보여도 무해하다. trace `"CPY CLEAN addr len CPY CLEAN addr len OWN WMB OWN"`. Linux stmmac 등 실제 NIC 드라이버도 "첫 디스크립터의 OWN 을 마지막에, 그 직전에 배리어" 구조다.

</details>

### 연습 3 — RX 링: OWN → RMB → invalidate → 복사 → 다시 넘기기

DMA 가 RAM 에 프레임을 쓰고 len/stat 을 쓴 뒤 OWN 을 내린다. CPU 캐시에는 옛 줄(0xEE)이 남아 있다. `rx_receive(out)` 을 작성하라: 아직 OWN 이면 -1, 에러 프레임이면 -5, 아니면 길이. 어떤 경우든 처리한 칸은 다시 DMA 에 넘긴다. 기대 trace `"rC RMB rL rS INV CPY addr len WMB OWN"`.

<details>
<summary>답 보기</summary>

`d_get_ctrl` 로 OWN 확인 → `dma_rmb()` → len, stat → `cache_inval()` → 복사 → refill(addr, len=버퍼 용량, `dma_wmb()`, OWN). invalidate 를 빼면 0xEE 가 나와 FAIL. invalidate 는 **OWN 확인 뒤** 여야 한다 — 그 전에 하면 DMA 가 아직 쓰는 중에 CPU 가 (투기적으로) 라인을 다시 가져올 수 있다. 에러 프레임도 칸을 돌려줘야 링이 마르지 않는다 (RX "descriptor unavailable" → 수신 정지는 흔한 현장 버그). 실제 M7 에서는 refill 전에도 그 버퍼 범위에 더티 라인이 없도록 해야 한다 (위 Q5).

</details>

### 연습 4 — 상황별 배리어 고르기

`pick_barrier(scen)` 이 NONE / COMPILER / ACQREL / DMB / DSB / DSB_ISB 중 하나를 돌려주게 하라: 지역 버퍼, 같은 코어 ISR 과 공유, 다른 코어와 공유, DMA 디스크립터 OWN, OWN → doorbell, 캐시 clean 완료 후 DMA 시작, VTOR/MPU 변경 후 실행.

<details>
<summary>답 보기</summary>

| 상황 | 답 | 이유 |
|---|---|---|
| 지역 버퍼 | NONE | 다른 관찰자 없음 — as-if 규칙 안 |
| 같은 코어 ISR | COMPILER | 한 코어는 자기 명령 순서대로 메모리를 본다 → 컴파일러만 막으면 됨 (`atomic_signal_fence`, 또는 ISR 공유 변수를 volatile/원자로) |
| 다른 코어 | ACQREL | 다른 관찰자 → CPU 배리어 필요. release/acquire 원자 연산 |
| DMA OWN | DMB | 장치는 C 밖 → 아키텍처 배리어 (`dma_wmb`) |
| OWN → doorbell | DMB | 순서만 필요 (`writel` 내장) |
| 캐시 clean → DMA 시작 | DSB | 유지보수 연산의 **완료** 를 기다리는 건 DSB 뿐 |
| VTOR/MPU | DSB+ISB | 쓰기 완료 + 파이프라인 재인출 |

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 21/21 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 21/21, 보고 없음
```

## 10. 핵심 정리

- 소스 순서가 장치에게 보이려면 **컴파일러 → CPU/버스 → 캐시** 세 층을 다 통과해야 한다. 층마다 도구가 다르다.
- `volatile` = volatile 접근끼리의 컴파일러 순서만. 일반 필드는 넘나든다(M0+ 실측), CPU 순서와는 무관.
- `asm("":::"memory")` = `atomic_signal_fence` = 명령어 0개짜리 컴파일러 배리어 (같은 코어 ISR 용). `atomic_thread_fence` / release·acquire 는 `dmb`/`stlr`/`ldapr` 까지 낸다.
- TX: **clean → 필드 → `dma_wmb` → OWN → `writel`(doorbell)**. RX/회수: **OWN==0 → `dma_rmb` → stat/len → invalidate → 데이터 → refill**. 배리어는 막을 두 접근 "사이" 에.
- 컴파일러 배리어는 CPU 층에서 아무것도 안 바꾼다 (시뮬레이터 656 = 656). 분기(제어 의존)는 ARM 에서 load 순서를 보장하지 않는다.
- DMB = 순서, DSB = 완료(캐시 유지보수, WFI, 설정 반영), ISB = 파이프라인 재인출. M4 에서 "없어도 됐다" 는 M7 에서 깨진다.
- DMA 는 C 스레드가 아니다 — C11 fence 는 장치 순서의 근거가 못 된다. 플랫폼 배리어(`dma_wmb`, `__DMB`)를 쓰고, TSan 은 fence 를 모른다는 것도 기억.

## 참고자료

- C11 (N1570): 5.1.2.3p2·p6 (volatile 접근 = 부작용, 관찰 가능한 동작), 5.1.2.4p11·p25 (synchronizes-with, data race = UB), 6.7.3p7 (volatile), 7.17.4 Fences (p2–p4 fence 동기화 규칙), 7.17.4.1 `atomic_thread_fence`, 7.17.4.2 `atomic_signal_fence` — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- ARM Application Note 321 — ARM Cortex-M Programming Guide to Memory Barrier Instructions — <https://developer.arm.com/documentation/dai0321/latest/>
- ARMv7-M Architecture Reference Manual (DDI0403) — Memory access order 절, DMB/DSB/ISB 명령 설명 — <https://developer.arm.com/documentation/ddi0403/latest/>
- Arm Architecture Reference Manual for A-profile (DDI0487) — 메모리 모델 장, LDAR/LDAPR/STLR 명령 설명 — <https://developer.arm.com/documentation/ddi0487/latest/>
- Linux kernel `Documentation/memory-barriers.txt` — "dma_wmb() / dma_rmb()" 와 MMIO 순서 — <https://www.kernel.org/doc/Documentation/memory-barriers.txt>
- Linux `arch/arm64/include/asm/barrier.h`, `io.h` (`dma_wmb` = `dmb oshst`, `__iowmb`) — <https://github.com/torvalds/linux/blob/master/arch/arm64/include/asm/barrier.h>
- CMSIS-Core: `__DMB`, `__DSB`, `__ISB`, `__COMPILER_BARRIER`, `SCB_CleanDCache_by_Addr` — <https://arm-software.github.io/CMSIS_5/Core/html/group__intrinsic__CPU__gr.html>
- ThreadSanitizer 문서 (지원 범위) — <https://clang.llvm.org/docs/ThreadSanitizer.html>
- herd7 / litmus7 (diy 도구 모음) — 리트머스 테스트로 메모리 모델 검증 — <https://diy.inria.fr/>
- 관련 레슨: C-B09 (volatile 의 한계), C-M01 (MMIO volatile/레지스터 구조체), C-M14 (ISR 공유 데이터, PRIMASK), C-A01 (mock 레지스터 HAL, trace 테스트), C-A03 (C11 atomics, acquire/release, TSan), C-A05 (DMA 정렬 버퍼), C-A11 (ISR·드라이버 코드 리뷰)
