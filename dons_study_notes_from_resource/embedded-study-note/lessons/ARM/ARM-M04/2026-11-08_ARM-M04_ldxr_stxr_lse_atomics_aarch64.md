---
id: ARM-M04
time: 95분
updated: 2026-11-08
---
# ARM-M04 LDXR/STXR로 원자 연산 구현 (AArch64)

## 학습 목표

- 배타 모니터(exclusive monitor)가 `LDAXR`/`STLXR` 루프를 어떻게 원자적으로 만드는지 그림으로 설명하고, `fetch_add` · strong/weak CAS · 스핀락을 GNU extended inline asm 으로 **직접** 쓴다 (`=&r`, `+Q`, `"memory"`, `"cc"` 를 왜 쓰는지 포함).
- CAS 실패 경로의 `CLREX`, 대기 루프의 `WFE`(+`SEVL`) 가 왜 있는지, 빼면 무엇이 달라지는지 말할 수 있다.
- ARMv8.1 LSE 명령(`LDADDAL`, `CASAL`, `SWPAL`, `STSETL`, …)으로 같은 연산을 다시 쓰고, 컴파일러가 C11 atomics 를 언제 LSE / LL·SC 루프 / `__aarch64_*` 라이브러리 호출(outline atomics)로 내리는지 실제 asm 으로 확인한다.
- pthread 스트레스(정확한 최종값, 임계 구역 동시 진입 0, 티켓 FIFO 순서)로 정확성을 검증하고, 1~8 스레드 경합에서 LL/SC 와 LSE 의 비용을 **실측** 해서 "LSE 가 항상 빠르다" 가 왜 틀린지 숫자로 말한다.

## 1. 문제

"C11 atomics 없이, 어셈블리로 `fetch_add` 랑 CAS 를 써 보세요. `STXR` 가 실패하면요?" — Cortex-A 멀티코어 SoC(i.MX8M · AM62x · Zynq UltraScale+ 의 Cortex-A53, Jetson 의 A78AE)에서 커널·하이퍼바이저·부트로더·RTOS SMP 포트를 다루는 펌웨어 면접의 단골 질문이다. 특히 **Cortex-A53 은 ARMv8.0 이라 LSE 가 없다** — 그 칩의 원자 연산은 전부 이 레슨의 `LDXR/STXR` 루프다.

동시성 가이드 3층의 흐름에서 이 레슨은 "한 층 아래" 다. C-A03 에서 `atomic_fetch_add` 가 arm64 에서 `ldaddal` 한 줄로, Cortex-M4 에서 `ldrex/strex` 루프로 내려가는 것을 **읽었다**. 이번에는 그 명령들을 **손으로 써서** 같은 계약(원자성, acquire/release)을 지키는지 스트레스로 확인한다. memory_order 자체의 의미는 C-A03, 배리어 명령(DMB/LDAR/STLR 리트머스)은 ARM-M05, Cortex-M 의 LDREX/STREX 와 예외는 ARM-M11, RISC-V 의 LR/SC 는 RISCV-M04, 메모리 순서별 비용 측정은 LLAT-B06 이 맡는다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="22" fill="currentColor" font-weight="bold">LL/SC: "읽은 뒤 아무도 안 썼으면" 만 쓰기에 성공한다 — 배타 모니터가 그 사이를 지켜본다</text>
<text x="10" y="62" fill="currentColor">CPU0</text> <text x="10" y="182" fill="currentColor">CPU1</text> <rect x="60" y="45" width="140" height="30" rx="4" class="f-acc" stroke="currentColor"/>
<text x="68" y="65" fill="currentColor" font-family="monospace">ldaxr w8,[x0]</text> <rect x="210" y="45" width="110" height="30" rx="4" class="f-mut" stroke="currentColor"/>
<text x="218" y="65" fill="currentColor" font-family="monospace">add w9,w8,#1</text> <rect x="400" y="45" width="170" height="30" rx="4" class="f-warn" stroke="currentColor"/>
<text x="408" y="65" fill="currentColor" font-family="monospace">stlxr w10,w9,[x0]</text> <text x="580" y="65" fill="currentColor">→ w10 = 1 실패</text>
<rect x="60" y="95" width="510" height="22" rx="4" class="f-ok" stroke="currentColor"/> <text x="70" y="111" fill="currentColor">CPU0 모니터: Exclusive(x0 의 granule)</text>
<text x="430" y="111" fill="currentColor">→ Open</text> <rect x="250" y="165" width="160" height="30" rx="4" class="f-acc" stroke="currentColor"/>
<text x="258" y="185" fill="currentColor" font-family="monospace">ldaddal / str [x0]</text> <path d="M330 165 L420 120" stroke="currentColor" fill="none" stroke-width="2"/>
<path d="M408 120 L420 120 L413 130" stroke="currentColor" fill="none" stroke-width="2"/> <text x="430" y="150" fill="currentColor">다른 관찰자의 쓰기가 granule 을 건드리면</text>
<text x="430" y="166" fill="currentColor">CPU0 의 (global) 모니터가 풀린다</text>
<text x="60" y="140" fill="currentColor" font-family="monospace">cbnz w10, 1b → 다시 처음부터</text>
<g fill="currentColor">
<text x="10" y="232">· 성공 조건: 내 LDXR 이후 그 granule(ERG, 구현 정의 크기)에 다른 쓰기가 없었다 → STXR 의 Ws = 0.</text> <text x="10" y="254">· 실패해도 메모리는 그대로다. 그래서 "실패하면 다시" 루프만으로 원자적 RMW 가 된다.</text>
<text x="10" y="276">· 값이 아니라 "쓰기가 있었나" 를 본다 → 같은 값으로 되돌려 써도(A→B→A) 실패한다.</text> <text x="10" y="298">· 예외 반환·문맥 전환·CLREX 도 모니터를 연다 → 가짜 실패(spurious failure)가 정상 동작의 일부.</text>
<text x="10" y="320">· LSE(ARMv8.1)는 이 루프를 명령 하나로: 실패·재시도가 소프트웨어에 안 보인다.</text>
</g>
</svg>
```

| 연산 | LL/SC (ARMv8.0, 이 레슨 Part A) | LSE (ARMv8.1 FEAT_LSE, Part B) |
|---|---|---|
| fetch_add | `ldaxr` / `add` / `stlxr` / `cbnz` | `ldaddal` |
| CAS | `ldaxr` / `cmp` / `b.ne`→`clrex` / `stlxr` / `cbnz` | `casal` |
| exchange | `ldaxr` / `stlxr` / `cbnz` | `swpal` |
| or/and-not (비트) | 루프 안에서 `orr`/`bic` | `ldsetal` / `ldclral` (값 안 받으면 `stset`/`stclr`) |
| 임의 계산 (가장 낮은 1비트 찾아 지우기 등) | 루프 안 레지스터 연산이면 뭐든 | 없음 → CAS 루프나 트릭 필요 (Part C, 연습 2) |

### Part A — LL/SC 부품 3종

- `uint32_t llsc_fetch_add(uint32_t *p, uint32_t v)` — 옛 값 반환, `LDAXR/STLXR` 루프.
- `uint32_t llsc_cas(uint32_t *p, uint32_t expected, uint32_t desired)` — **strong** CAS, 본 값 반환 (== expected 면 성공). 비교 실패 경로는 `CLREX`, `STLXR` 가짜 실패는 안에서 재시도.
- `llsc_trylock` / `llsc_lock` / `llsc_unlock` — `LDAXR`+`STXR` 로 잡고, 잠겨 있으면 `WFE` 로 잠들고, `STLR WZR` 로 푼다.
- 스트레스: 4 스레드 × 20만 — 카운터 정확히 800000, 락 안의 일반 변수 `a += 1; b += 3` 가 `a == 800000`, `b == 3a`, 동시 진입(inside 카운터 ≠ 1) 0회.

### Part B — LSE 판과 경계 사례

- `lse_fetch_add`(LDADDAL) · `lse_cas`(CASAL) · `lse_swap`(SWPAL) · `lse_lock`(SWPA + LDXR/WFE 대기).
- 경계: 32비트 wrap(`0xFFFFFFFF + 1`), 64비트판 `llsc_fetch_add64`(32비트 carry), weak CAS `llsc_cas_weak`(시도 한 번, 실패 시 `*expected` 갱신), 같은 CAS 표를 LL/SC·LSE 가 똑같이 처리, LSE 로 잡은 락을 LL/SC trylock 이 존중, **같은 변수에 LL/SC 와 LSE 를 섞어도** 정확.
- 컴파일러 비교 (`demo_emit.c`): Apple clang 기본 / `-mcpu=generic` / `-moutline-atomics` / VM 의 gcc 15.
- 성능 (`demo_bench.c`): 1·2·4·6·8 스레드, 같은 캐시 라인 하나.

### Part C — Cortex-A 4코어 SoC 네트워크 드라이버: DMA 버퍼 풀 + 티켓 락

RX 처리가 여러 코어에서 돌고(RSS), 각 코어가 DMA 버퍼 풀에서 버퍼를 빌렸다 돌려준다. 커널 공유 자료구조는 **공정한** 락으로 보호한다 (먼저 온 코어가 먼저).

- `int pool_alloc(pool_t *)` — 비트마스크(1 = 빈 버퍼)에서 **가장 낮은 1비트를 찾아 지우기** 를 LL/SC 한 쌍 안에서. 비었으면 -1.
- `void pool_free(pool_t *, int)` — `STSETL` (버퍼 내용을 다 쓴 뒤 release).
- `uint32_t ticket_lock(ticket_t *)` / `ticket_unlock` — `LDADD` 로 번호표, `LDAXR` + `WFE` 로 내 차례 대기, `STLR` 로 다음 번호.
- 스트레스: 버퍼 3개를 4 스레드가 10만 번씩 — 이중 소유 0, 최종 마스크 복원. 티켓 락 4 × 5만 — 정확한 카운트, **번호표 순서대로** 입장.

## 2. Problem (English)

> **Hand-written AArch64 atomics: LL/SC vs LSE.**
>
> Implement with GNU extended inline asm (no `<stdatomic.h>` in the primitives):
>
> 1. `uint32_t llsc_fetch_add(uint32_t *p, uint32_t v)` using `LDAXR/STLXR`; `uint32_t llsc_cas(uint32_t *p, uint32_t expected, uint32_t desired)` — strong, returns the observed value; `bool llsc_trylock(lock_t *)`, `void llsc_lock(lock_t *)` (sleep with `WFE` while held), `void llsc_unlock(lock_t *)`.
> 2. The same with ARMv8.1 LSE: `lse_fetch_add` (`LDADDAL`), `lse_cas` (`CASAL`), `lse_swap` (`SWPAL`), `lse_lock` (`SWPA` + wait). Also `uint64_t llsc_fetch_add64(uint64_t *, uint64_t)` and a weak CAS `bool llsc_cas_weak(uint32_t *p, uint32_t *expected, uint32_t desired)` with C11 `compare_exchange_weak` semantics.
> 3. A lock-free buffer pool over a 32-bit free mask: `int pool_alloc(pool_t *)` (claim the lowest free index, -1 if empty) and `void pool_free(pool_t *, int)`; a FIFO ticket lock `uint32_t ticket_lock(ticket_t *)` / `void ticket_unlock(ticket_t *)`.
>
> Constraints: 4 threads × 200 000 operations must give exact totals; no two threads inside a critical section; tickets served in order; no buffer owned twice. Then benchmark LL/SC vs LSE under contention for 1..8 threads.
>
> Follow-ups: Why the loop on the status register? Is `CLREX` required on the CAS failure path? What may not appear between `LDXR` and `STXR`? Why does `WFE` wake up? What does the compiler emit for `atomic_fetch_add` on Apple Silicon vs a generic ARMv8.0 Linux build? Is LSE always faster?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, 0.1 s 안에 끝남 — 멈추지 않는다):

```text
FAIL starter.c:296  llsc_fetch_add(&x, 3u) == 5u && x == 8u
FAIL starter.c:297  llsc_cas(&x, 8u, 100u) == 8u && x == 100u
FAIL starter.c:298  llsc_cas(&x, 8u, 7u) == 100u && x == 100u
FAIL starter.c:300  llsc_trylock(&l) && l.v == 1u
FAIL starter.c:303  l.v == 0u && llsc_trylock(&l)
FAIL starter.c:306  l.v == 1u && !llsc_trylock(&l)
SKIP 4 stress checks (Part A): unit checks failed
FAIL starter.c:322  lse_fetch_add(&x, 1u) == 0xFFFFFFFFu && x == 0u
… (Part B 단위 FAIL 7줄 더: wrap, CAS 표, swap, 64비트, weak CAS 2, LSE 락)
SKIP 7 stress checks (Part B): unit checks failed
FAIL starter.c:373  a0 == 0 && a1 == 1 && a2 == 2 && a3 == -1 && p.free_mask == 0u
… (Part C 단위 FAIL 4줄 더: free/alloc 2, 티켓 2)
SKIP 4 stress checks (Part C): unit checks failed
2/36 passed
```

하네스 설계: 각 Part 의 멀티스레드 스트레스는 **단일 스레드 단위 테스트가 모두 통과해야** 돈다. 그래서 스텁(`pool_alloc` 이 늘 -1, 빈 `llsc_lock`)이 무한 재시도나 레이스를 일으키지 않는다. 스트레스 CHECK 는 올바른 구현이면 매번 성립하는 것만 본다. 버퍼 풀이 비어 재시도한 횟수처럼 타이밍에 따라 달라지는 값은 출력만 한다.

관찰용 파일 (하네스와 별개, 전부 이 디렉터리):

```bash
cc -std=c11 -O2 -pthread demo_broken.c -o db && ./db status|plain|noclobber|earlyclob   # 틀린 asm 4종
cc -std=c11 -O2 -S demo_clobber.c -o -          # (VM 에서 gcc 로도) "memory" clobber 없으면?
cc -std=c11 -g -O1 -fsanitize=thread -pthread [-DANNOTATE|-DBROKEN] demo_tsan.c -o dt && ./dt
cc -std=c11 -O2 -S [-mcpu=generic] demo_emit.c -o -   # 컴파일러가 C11 atomics 를 무엇으로 내리나
cc -std=c11 -O2 -mcpu=generic [-DEXT] -c demo_lse_probe.c -o /dev/null   # LSE asm 을 v8.0 대상으로
cc -std=c11 -O2 -pthread demo_bench.c -o bench && ./bench   # 1..8 스레드 경합 (약 12 s)
```

## 4. 힌트

<details>
<summary>힌트 1 — inline asm 뼈대: 출력은 전부 early-clobber, 메모리는 "+Q"</summary>

```c
__asm__ volatile(
    "1: ldaxr  %w[old], %[mem]\n"
    "   ...    \n"
    "   stlxr  %w[st], %w[new], %[mem]\n"
    "   cbnz   %w[st], 1b\n"
    : [old] "=&r"(old), [new] "=&r"(nv), [st] "=&r"(st), [mem] "+Q"(*p)
    : [v] "r"(v)
    : "memory");
```

`Q` 는 "베이스 레지스터 하나로 된 메모리 주소(`[x0]`)" — 배타 명령은 오프셋을 못 받는다. `%w` 는 32비트 이름(w8), `%x` 는 64비트. 출력에 `&` 가 없으면 컴파일러가 입력(주소!)과 같은 레지스터를 줄 수 있다 — 이건 실제로 일어난다 (5. 풀이).

</details>

<details>
<summary>힌트 2 — CAS 는 출구가 둘</summary>

값이 다르면 `STLXR` 없이 빠지고(이때 `CLREX`), 같으면 `STLXR` 를 시도해 실패하면 `LDAXR` 부터 다시. 어느 출구든 `old` 에 "본 값" 이 남는다. weak 판은 `STLXR` 실패도 그냥 false 로 돌려준다.

</details>

<details>
<summary>힌트 3 — WFE 로 잠들기 전에 모니터를 걸어라</summary>

`WFE` 는 이벤트가 올 때까지 잔다. 소유자의 해제 store 가 이벤트를 만들려면, 기다리는 코어가 그 주소에 **배타 모니터를 걸어 둔 상태**(방금 `LDAXR`/`LDXR` 로 읽음)여야 한다. 순서: 배타 읽기 → 잠겨 있나 확인 → `WFE` → 다시 배타 읽기. 풀려 있는 빠른 경로에 `WFE` 를 두지 말 것.

</details>

<details>
<summary>힌트 4 — LSE 오퍼랜드 순서, CASAL 의 in/out, 버퍼 풀·티켓</summary>

`LDADDAL Ws, Wt, [Xn]`: Ws = 더할 값(입력), Wt = 옛 값(출력). `CASAL Ws, Wt, [Xn]`: Ws 는 **입력(expected)이자 출력(본 값)** → 제약은 `"+r"`, Wt = desired. `-mcpu=generic` 같은 v8.0 빌드에서도 어셈블되게 문자열 앞에 `.arch_extension lse`.

가장 낮은 1비트 인덱스 = `rbit` 후 `clz`. `bic` 으로 지우고 `stxr`. 비었으면 `clrex` 후 -1. 티켓: `ldadd` 로 `next` 를 하나 받고, `ldaxr owner` 가 내 번호가 될 때까지 `wfe`. 해제는 소유자만 하므로 `ldr owner` + 1 을 `stlr`.

</details>

## 5. 풀이

### 0. 배타 모니터 — 이 레슨에 필요한 만큼 (DDI 0487, "Synchronization and semaphores" 장)

| 용어 | 뜻 |
|---|---|
| 로컬 모니터 (local monitor) | 각 PE(코어) 안. `LDXR` 이 주소를 "Exclusive" 로 표시, `STXR`·`CLREX`·예외 반환 등으로 Open |
| 글로벌 모니터 (global monitor) | 공유(Shareable) 메모리용. **다른 관찰자의 쓰기** 가 표시된 granule 을 건드리면 그 PE 의 표시를 지운다 |
| ERG (Exclusives Reservation Granule) | 모니터가 보는 단위. 구현 정의, 최대 2 KB (문서 기준), `CTR_EL0.ERG` 에 적혀 있다 |
| 상태 레지스터 Ws | `STXR Ws, Wt, [Xn]` 의 결과: 0 = 썼다, 1 = 안 썼다(메모리 불변) |
| 가짜 실패 | 다른 쓰기가 없어도 실패할 수 있다 (예외, 문맥 전환, 같은 granule 의 다른 변수…). 루프는 이걸 전제로 한다 |

이 맥에서 `CTR_EL0` 을 읽어 ERG 를 확인해 보려 했지만, 사용자 모드에서 `mrs x0, ctr_el0` 은 **SIGILL(종료 코드 132)** 로 죽었다 — macOS 는 EL0 의 CTR_EL0 접근을 막아 둔다. 캐시 라인은 `sysctl hw.cachelinesize` = 128 B. ERG 실측값은 미확인이다. 실무 규칙: 락·카운터처럼 경합하는 원자 변수는 **캐시 라인 하나를 혼자** 쓰게 정렬한다 (같은 granule 에 다른 변수가 있으면 그 쓰기도 내 STXR 를 실패시킨다; false sharing 은 ARCH-M08).

`LDXR` 과 `STXR` 사이의 규칙 (DDI 0487 의 "Load-Exclusive and Store-Exclusive instruction usage restrictions" 요지, 문서 기준): 그 사이에 **다른 명시적 메모리 접근, 캐시 유지보수, 예외를 일으킬 일, 너무 긴 코드** 를 두지 말 것. 두면 구현에 따라 STXR 이 계속 실패(livelock)할 수 있다. 그래서 루프 몸통은 레지스터 연산 몇 개로만 쓴다 — RISC-V 는 이걸 "16 명령 이내 constrained loop" 로 명문화했다 (RISCV-M04).

### Part A — fetch_add: 한 줄씩

solution.c 의 `llsc_fetch_add` (힌트 1 의 뼈대 그대로: `1: ldaxr / add / stlxr / cbnz 1b`) 를 조각별로:

| 조각 | 왜 |
|---|---|
| `ldaxr` / `stlxr` | acquire 읽기 + release 쓰기. 이 쌍이 컴파일러의 C11 seq_cst RMW 매핑과 같다 (`-mcpu=generic` 실측, 아래) |
| `cbnz %w[st], 1b` | 실패하면 **처음부터** — 새 값을 다시 읽어야 하므로 `ldaxr` 로 돌아간다 |
| `=&r` (early-clobber) | 출력이 입력을 다 읽기 전에 써진다는 표시. 빼면 실제로 크래시 (아래) |
| `+Q`(*p) | "이 메모리를 읽고 쓴다" + `[x0]` 형식 주소. 컴파일러가 `*p` 의 값을 레지스터에 캐시하지 않는다 |
| `"memory"` | 이 asm 너머로 **다른** 메모리 접근을 옮기지 말라 (컴파일러 배리어, C-A02). 원자 연산이 순서를 주려면 필수 |
| `volatile` | 출력을 안 써도 지우지 말 것 |

**`&` 를 빼면** — `demo_broken.c` 의 `add_no_earlyclobber` 를 `cc -O2 -S` 로 본 실제 출력:

```text
_add_no_earlyclobber:
	mov	w8, #3
Ltmp0:
	ldaxr	w0, [x0]        ; ← 출력 old 에 x0 배정: 주소 레지스터를 값으로 덮어씀
	add	w8, w0, w8
	stlxr	w9, w8, [x0]    ; ← 주소 = 5 (옛 값) 에 쓰기 시도
	cbnz	w9, Ltmp0
	ret

$ ./db earlyclob   (3회)
earlyclob: child killed by signal 11 (Segmentation fault: 11)
earlyclob: child killed by signal 11 (Segmentation fault: 11)
earlyclob: child killed by signal 11 (Segmentation fault: 11)
```

컴파일러는 "출력은 입력을 다 쓴 뒤에 쓴다" 고 가정하고 `old` 를 `p` 와 같은 레지스터(x0)에 줬다. 루프 안에서 출력을 먼저 쓰는 asm 은 출력 전부에 `&` 가 필요하다. 덧붙여 `STXR` 의 상태 레지스터가 데이터·주소 레지스터와 같으면 결과가 CONSTRAINED UNPREDICTABLE 이다 (DDI 0487 STXR 설명, 문서 기준). `&` 가 이 경우도 막아 준다.

**상태 레지스터를 무시하면** (`./db status`, 4 스레드 × 100만, 실제 출력):

```text
status   : counter=3037374 (expect 4000000, lost 962626)
status   : counter=3031933 (expect 4000000, lost 968067)
status   : counter=3149449 (expect 4000000, lost 850551)
```

21~24% 유실이다. STXR 이 실패하면 메모리는 그대로인데, 코드는 성공한 줄 안다. 아래 성능 절의 retries/op 를 보면, 4 스레드에서 연산 4개 중 1개 꼴로 STXR 이 실패한다. 유실 비율과 맞는 숫자다.

### Part A — CAS 와 CLREX

solution.c 의 `llsc_cas` 는 출구가 둘이다: `1: ldaxr / cmp / b.ne 2f`(값이 다르면 쓰지 않고 나감) `/ stlxr / cbnz 1b`(가짜 실패면 다시 — strong) `/ b 3f / 2: clrex / 3:`. 컴파일러도 똑같이 한다 — `demo_emit.c` 의 `atomic_compare_exchange_strong` 을 `cc -O2 -mcpu=generic -S`(ARMv8.0, LSE 없음)로 본 실제 출력:

```text
LBB2_1:
	ldaxr	w9, [x8]
	cmp	w9, w10
	b.ne	LBB2_4
	stlxr	w11, w1, [x8]
	cbnz	w11, LBB2_1
	...
LBB2_4:
	mov	w8, wzr
	clrex
```

**CLREX 는 꼭 필요한가?** 정확성만 보면 대부분 **아니다.** ① 다음 `LDXR` 이 모니터를 새로 건다. 짝 없는 `STXR` 을 쓰는 코드는 없다. ② 남겨 둔 Exclusive 표시가 문제가 되는 고전적인 경우는 따로 있다. 스레드 A 가 `LDXR X` 뒤에 선점되고, 같은 코어에서 다른 코드가 **일반 store** 로 X 를 바꾼 뒤 돌아오면, A 의 `STXR` 이 성공해 그 store 를 덮을 수 있다. 같은 PE 의 일반 store 가 로컬 모니터를 지우는지는 구현 정의다. ③ 그래서 아키텍처·OS 는 예외 진입/반환이나 문맥 전환 때 모니터를 연다. 알려진 바로는 ARMv8-A 에서 예외 반환이 로컬 모니터를 지우고, Linux arm64 도 이걸 근거로 문맥 전환 경로에 `clrex` 를 따로 두지 않는다 (문서 원문·커널 소스는 이 레슨에서 미확인).

그래서 실패 경로의 `CLREX` 는 "모니터를 열어 둔 채 오래 다른 일을 하지 않는다" 는 위생이다. LLVM 은 넣는다(위 출력). 비용은 명령 하나다. Cortex-M(ARMv7-M)에서는 CLREX 와 예외의 관계가 다르다 — ARM-M11 이 QEMU+gdb 로 확인한다.

### Part A — 스핀락: 빠른 경로 먼저, 잠겨 있으면 WFE

```c
"1: ldaxr  %w[old], %[mem]\n"      /* acquire + 모니터 걸기 */
"   cbnz   %w[old], 2f\n"          /* 잠겨 있다 → 잠들기 */
"   stxr   %w[st], %w[one], %[mem]\n"
"   cbnz   %w[st], 1b\n"
"   b      3f\n"
"2: wfe\n"
"   b      1b\n"
"3:\n"
/* unlock: */ "stlr   wzr, %[mem]\n"
```

- **잡기 = `LDAXR` + `STXR`(release 없음)** — 임계 구역 접근이 위로 못 올라오게 하는 건 acquire 쪽이다. 잡는 store 에는 release 가 필요 없다. **풀기 = `STLR WZR`** — 임계 구역의 쓰기가 해제보다 먼저 보이게 하는 release. C-A03 의 `test_and_set(acquire)` / `clear(release)` 와 같은 계약을 명령으로 쓴 것이다.
- **값을 먼저 본다.** 잠겨 있으면 `STXR` 을 시도조차 하지 않는다. 쓰기 시도는 캐시 라인을 독점 상태로 끌어오므로, 기다리는 코어들이 서로 라인을 뺏게 된다 (TAS vs TTAS, OS-M07).

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="22" fill="currentColor" font-weight="bold">WFE 가 깨는 원리: 기다리는 코어의 모니터를 소유자의 해제 store 가 지운다 → 이벤트</text>
<text x="10" y="70" fill="currentColor">대기 코어</text> <text x="10" y="170" fill="currentColor">소유 코어</text> <rect x="90" y="52" width="130" height="30" rx="4" class="f-acc" stroke="currentColor"/>
<text x="98" y="72" fill="currentColor" font-family="monospace">ldaxr → 1</text> <rect x="230" y="52" width="230" height="30" rx="4" class="f-mut" stroke="currentColor"/>
<text x="240" y="72" fill="currentColor" font-family="monospace">wfe   (저전력으로 잠듦)</text> <rect x="520" y="52" width="100" height="30" rx="4" class="f-acc" stroke="currentColor"/>
<text x="528" y="72" fill="currentColor" font-family="monospace">ldaxr → 0</text> <rect x="630" y="52" width="100" height="30" rx="4" class="f-ok" stroke="currentColor"/>
<text x="640" y="72" fill="currentColor" font-family="monospace">stxr 1</text> <rect x="90" y="152" width="300" height="30" rx="4" class="f-warn" stroke="currentColor"/>
<text x="100" y="172" fill="currentColor">임계 구역 (일반 변수 a, b)</text> <rect x="400" y="152" width="120" height="30" rx="4" class="f-acc" stroke="currentColor"/>
<text x="408" y="172" fill="currentColor" font-family="monospace">stlr wzr</text> <path d="M460 152 L470 90" stroke="currentColor" fill="none" stroke-width="2"/>
<path d="M462 99 L470 90 L473 102" stroke="currentColor" fill="none" stroke-width="2"/> <text x="480" y="125" fill="currentColor">모니터 Exclusive→Open = 이벤트</text>
<g fill="currentColor">
<text x="10" y="215">· 해제가 ldaxr 와 wfe 사이에 와도 이벤트 레지스터가 이미 세워져 있어 wfe 가 바로 통과 → 놓치지 않는다.</text> <text x="10" y="237">· 깨는 다른 원인: SEV/SEVL, 인터럽트, 이벤트 스트림 타이머 → 깬 뒤 반드시 다시 확인(루프).</text>
<text x="10" y="259">· ARMv7 시절엔 unlock 에 dsb + sev 를 넣었다. v8 은 모니터 해제가 이벤트를 만들므로 stlr 하나면 된다.</text> <text x="10" y="281">· SEVL = "내 이벤트 하나 미리 세우기" — 첫 wfe 를 그냥 통과시키는 모양에서 쓴다 (비용은 아래 실측).</text>
</g>
</svg>
```

`SEVL` 이 들어가는 다른 모양 — `sevl; 1: wfe; 2: ldaxr; cbnz 1b; stxr; cbnz 2b` — 도 맞다. 그러나 **매번** `SEVL`+`WFE` 를 거친다. `demo_bench.c` 에서 1 스레드(경합 없음) 획득+해제가 이 모양은 **15.5 ns**, 빠른 경로 먼저 모양은 **6.1 ns** 였다. 경합이 있을 때도 더 느렸다 (아래 표). 이 맥의 사용자 모드(EL0)에서 `WFE` 는 문제없이 돌았다. 커널이 EL0 WFE 를 트랩해 흉내 내는지는 확인하지 않았다 (`SCTLR_EL1.nTWE` 로 정해짐).

### Part B — LSE: 루프가 명령 하나로

| 명령 | 동작 (Ws = 입력, Wt = 옛 값) | 접미사 |
|---|---|---|
| `LDADD Ws, Wt, [Xn]` | `Wt = [Xn]; [Xn] += Ws` | 없음 = relaxed, `A` = acquire, `L` = release, `AL` = 둘 다 |
| `LDCLR` / `LDSET` / `LDEOR` | 비트 지우기(AND-NOT) / 세우기(OR) / 뒤집기(XOR) | 〃 |
| `LDUMAX` / `LDSMAX` / `LDUMIN` / `LDSMIN` | 부호 없는/있는 max/min | 〃 (연습 1) |
| `SWP Ws, Wt, [Xn]` | 교환 | 〃 |
| `CAS Ws, Wt, [Xn]` | `[Xn] == Ws` 면 `[Xn] = Wt`. Ws ← 본 값 | 〃 |
| `CASP Xs, Xs+1, Xt, Xt+1, [Xn]` | 128비트(짝수 레지스터 쌍) CAS | 〃 (연습 3) |
| `STADD` / `STSET` / `STCLR` … | 옛 값을 안 받는 별칭 (`Wt = WZR`) | 없음 / `L` 만 |

```c
__asm__ volatile(LSE "ldaddal %w[v], %w[old], %[mem]\n" : [old] "=r"(old), [mem] "+Q"(*p) : [v] "r"(v) : "memory");
__asm__ volatile(LSE "casal  %w[old], %w[des], %[mem]\n" : [old] "+r"(old), [mem] "+Q"(*p) : [des] "r"(desired) : "memory");
```

`LSE` 는 `".arch_extension lse\n\t"` 다. Apple clang 의 기본 대상은 `-target-cpu apple-m1` 이라(`cc -### -c` 로 확인) LSE 가 이미 켜져 있다. 그래서 이 줄이 없어도 어셈블된다. ARMv8.0 대상으로 빌드하면 이렇게 된다 (`demo_lse_probe.c`, 실제 출력):

```text
$ cc -std=c11 -O2 -mcpu=generic -c demo_lse_probe.c -o /dev/null
demo_lse_probe.c:13:22: error: instruction requires: lse
   13 |     __asm__ volatile("ldaddal %w[v], %w[o], %[m]" : [o] "=r"(old), [m] "+Q"(*p) : [v] "r"(v) : "memory");
<inline asm>:1:2: note: instantiated into assembly here
    1 |         ldaddal w1, w0, [x0]
[-mcpu=generic -DEXT] ok                     # 문자열 앞에 .arch_extension lse
[-mcpu=generic -march=armv8.1-a] ok
[-mcpu=generic -march=armv8-a+lse] ok
```

어셈블된다고 실행되는 건 아니다. FEAT_LSE 가 없는 코어(Cortex-A53/A57/A72 — Raspberry Pi 4 포함)에서 `ldaddal` 은 미정의 명령 예외(SIGILL)다. 그래서 solution.c 는 시작할 때 확인한다:

```text
$ sysctl hw.optional.arm.FEAT_LSE hw.optional.arm.FEAT_LSE2 hw.optional.arm.FEAT_LRCPC
hw.optional.arm.FEAT_LSE: 1
hw.optional.arm.FEAT_LSE2: 1
hw.optional.arm.FEAT_LRCPC: 1
```

Linux 에서는 `getauxval(AT_HWCAP) & HWCAP_ATOMICS` 로 확인한다.

**경계 사례 (단위 테스트로 확인)** — 32비트 wrap 은 두 방식 모두 정확하다 (`0xFFFFFFFF+1 → 0`). 64비트판은 `%x` 레지스터로 같은 루프를 돈다 (`0xFFFFFFFF+1 → 0x100000000`, 스트레스에서도 2^32 경계를 넘는다). CAS 표 5행에서 LL/SC 와 LSE 의 결과는 같았다. weak CAS 는 값이 다르면 false 를 주고 `*expected` 를 갱신하며, 값이 같으면 가짜 실패가 있어도 곧 성공한다.

**같은 변수에 LL/SC 와 LSE 를 섞어도** 정확했다 (`K_MIXED`: 짝수 스레드는 LL/SC, 홀수 스레드는 LSE → 800000). LSE 원자 연산도 그 granule 에 대한 쓰기라서, 다른 코어의 배타 모니터를 지우기 때문이다. LSE 로 잡은 락을 LL/SC trylock 이 존중하는 것도 같은 이유다.

### Part B — 컴파일러는 무엇을 내나 (`demo_emit.c`)

`atomic_fetch_add(&cnt, v)` (seq_cst) 하나를 빌드 조건별로 본 실제 출력:

| 빌드 | `fa_seqcst` 몸통 |
|---|---|
| `cc -O2` (Apple 기본, apple-m1) | `ldaddal w0, w0, [x8]` (relaxed 판은 `ldadd`, CAS 는 `casal`, exchange 는 `swpal`) |
| `cc -O2 -mno-outline-atomics` / `-march=armv8-a` / `-march=armv8.1-a` | 같음 (`ldaddal`) — `-march=armv8-a` 도 기본 CPU 의 LSE 를 끄지 않았다 |
| `cc -O2 -mcpu=generic` | `ldaxr / add / stlxr / cbnz` 루프 (relaxed 판은 `ldxr/stxr`) |
| 맥 clang `--target=aarch64-linux-gnu -O2` (sysroot 없음) | `ldaxr/stlxr` 루프 — 이 조건에선 outline atomics 가 기본으로 켜지지 않았다 |
| 맥 clang `--target=aarch64-linux-gnu -O2 -moutline-atomics` | `bl __aarch64_ldadd4_acq_rel` (CAS 는 `bl __aarch64_cas4_acq_rel`) |
| VM(Ubuntu aarch64) `gcc 15 -O2`, `clang 21 -O2` | `bl __aarch64_ldadd4_acq_rel` — **기본이 outline** |
| VM `gcc -O2 -mno-outline-atomics` | `ldaxr / add / stlxr / cbnz` |
| VM `gcc -O2 -march=armv8.1-a` | `ldaddal` |

**outline atomics** 는 배포판 바이너리가 ARMv8.0 (A53/A72) 에서도 돌아야 하고, LSE 코어에서는 LSE 를 쓰고 싶어서 생겼다. 원자 연산마다 작은 헬퍼를 부르고, 헬퍼가 실행 중에 고른다. VM 에서 정적으로 링크된 libgcc 헬퍼를 `objdump -d` 로 본 실제 출력:

```text
<__aarch64_ldadd4_acq_rel>:
	adrp	x16, 20000 <__data_start>
	ldrb	w16, [x16, #24]          ; __aarch64_have_lse_atomics (시작 때 HWCAP 으로 채움)
	cbz	w16, 844
	ldaddal	w0, w0, [x1]             ; LSE 가 있으면 한 명령
	ret
 844:	mov	w16, w0
	ldaxr	w0, [x1]                 ; 없으면 LL/SC 루프
	add	w17, w0, w16
	stlxr	w15, w17, [x1]
	cbnz	w15, 848
	ret
```

함수 호출과 분기 하나가 추가 비용이다. 커널·펌웨어처럼 대상 CPU 를 아는 빌드는 `-mcpu=cortex-a55` (LSE 있음) 나 `-mcpu=cortex-a53` (없음) 을 직접 준다. Apple 플랫폼은 모든 arm64 기기가 LSE 를 가지므로 처음부터 인라인 LSE 다.

### Part B — TSan 은 asm 을 못 본다 (`demo_tsan.c`)

<!-- code: demo_tsan.c -->

```text
# 올바른 asm 스핀락 + 일반 카운터, 주석(annotation) 없음 — 거짓 양성
WARNING: ThreadSanitizer: data race (pid=74550)
  Write of size 8 at 0x0001000b8008 by thread T2:
    #0 worker demo_tsan.c:57 (ARM-M04_dt:arm64+0x100000838)
  Previous write of size 8 at 0x0001000b8008 by thread T1:
    #0 worker demo_tsan.c:57 (ARM-M04_dt:arm64+0x100000838)
  Location is global 'g_plain' at 0x0001000b8008 (ARM-M04_dt+0x100008008)
  ...
plain counter=400000 (expect 400000)
ThreadSanitizer: reported 1 warnings            (exit 134)

# -DANNOTATE: lock 직후 __tsan_acquire(l), unlock 직전 __tsan_release(l)
plain counter=400000 (expect 400000)            (보고 없음)

# -DBROKEN: STLXR 상태를 무시한 asm 카운터 — 거짓 음성 (3회)
asm counter=328698 (expect 400000)
asm counter=330665 (expect 400000)
asm counter=330867 (expect 400000)              (보고 없음)
```

TSan 은 컴파일러가 계측한 load/store 와 C11 atomics·pthread 호출만 안다. inline asm 은 계측되지 않는다.

- **올바른 asm 락**: TSan 은 그 락이 만드는 happens-before 를 모른다 → 실제로는 맞는 코드를 레이스로 보고한다.
- **틀린 asm 카운터**: 접근 자체를 보지 못한다 → 약 18% 를 잃어도 조용하다.

그래서 solution.c 의 하네스는 TSan 빌드일 때만 `__tsan_acquire/__tsan_release` 로 락의 의미를 알려 준다 (`<sanitizer/tsan_interface.h>`, 이 맥 clang 21 에 있음). asm 원자 연산의 정확성은 **스트레스 CHECK** 가 지킨다. 커널의 KCSAN 이 arch 원자 연산에 `instrument_atomic_*` 훅을 다는 것도 같은 이유다.

### Part C — 버퍼 풀: LL/SC 는 "임의의 RMW" 를 한 번에

solution.c 의 `pool_alloc` 을 `xcrun llvm-objdump -d` 로 본 실제 모습 (main 에 인라인됨, 소스의 asm 문자열과 한 줄씩 같다):

```text
1000011f0:  ldaxr   w15, [x10]
1000011f4:  cbz     w15, 0x100001214          ; 비었음 → clrex, -1
1000011f8:  rbit    w13, w15
1000011fc:  clz     w13, w13                  ; idx = ctz(mask)
100001200:  lsl     w8, w14, w13
100001204:  bic     w9, w15, w8
100001208:  stxr    w11, w9, [x10]
10000120c:  cbnz    w11, 0x1000011f0
100001210:  b       0x100001218
100001214:  clrex
```

- **찾기와 지우기가 한 배타 구간 안에** 있다. 그래서 두 코어가 같은 비트를 가져가는 일이 구조적으로 없다. 루프 몸통은 레지스터 연산 4개뿐이라 앞의 사용 제한도 지킨다.
- **LSE 에는 이 명령이 없다.** "가장 낮은 1비트 지우기" 를 LSE 로 하려면 CAS 루프로 하거나, 트릭을 쓴다: `LDR` 로 후보 비트를 고르고 `LDCLRA` 로 그 비트만 지운 뒤, 옛 값에 그 비트가 있었으면 내 것이다 (연습 2).
- **순서:** alloc 은 acquire(`ldaxr`)다 — 이전 소유자가 버퍼에 쓴 것을 본다. free 는 release(`STSETL`)다 — 내가 버퍼에 쓴 것을 다음 소유자에게 넘긴다. C-A03 의 "반납 방향도 release" 와 같은 구조다.
- **결과** (실제 출력, 6회): `allocs=400000` 은 매번 같았고, 이중 소유 0, 마스크 복원. 풀이 비어 재시도한 횟수는 1972 / 385 / 58543 / 17165 / 389 / 270 — 타이밍에 따라 크게 달라서 출력만 한다.

**티켓 락** — 스핀락은 공정하지 않다 (깬 코어 중 아무나 잡는다). 티켓 락은 번호표 순서다:

```c
"   ldadd  %w[one], %w[my], %[next]\n"   /* 번호표 (relaxed) */
"1: ldaxr  %w[cur], %[own]\n"            /* acquire + 모니터 */
"   cmp    %w[cur], %w[my]\n"
"   b.eq   2f\n"
"   wfe\n"
"   b      1b\n"
"2:\n"
/* unlock: ldr o,[owner]; add o,o,#1; stlr o,[owner] */
```

- `next` 의 `LDADD` 는 **relaxed** 로 충분하다. 번호가 겹치지 않는 건 RMW 의 원자성 덕이고, 임계 구역의 acquire 는 `owner` 를 읽는 `LDAXR` 가 준다.
- `owner` 는 소유자만 쓴다. 그래서 해제 쪽은 원자 RMW 가 필요 없고 `LDR` + `STLR` 이면 된다. 기다리는 모든 코어가 `owner` 에 모니터를 걸어 두었으므로, 이 `STLR` 하나가 전부를 깨운다. 자기 차례가 아닌 코어는 다시 잔다.
- 스트레스 결과: 20만 번의 입장이 **모두 번호표 순서**(`order_bad == 0`)였고, `next == owner == 200000`.
- 단점: 깨어나는 코어가 많을수록 owner 라인을 모두가 다시 읽는다 (thundering herd). 이것이 Linux 가 arm64 에서 ticket lock 을 qspinlock(MCS 계열 큐 락)으로 바꾼 동기 중 하나로 알려져 있다.

### 성능 — `demo_bench.c` (같은 캐시 라인 하나, 실측)

<!-- code: demo_bench.c -->

조건: Apple M6 (`sysctl -n machdep.cpu.brand_string`), 12 코어 = Super 2 + Performance 4 + Efficiency 6 (`hw.perflevel0..2`), Apple clang version 21.0.0 (clang-2100.0.123.102). ns/op 는 출발 신호부터 마지막 스레드 종료까지의 벽시계를 전체 연산 수로 나눈 값이다. 칸마다 5회 중 중앙값을 냈고, 프로그램을 3번 돌려 그 중앙값들의 중앙값을 적었다. 괄호는 3회 범위다. macOS 에는 코어 고정이 없어 6·8 스레드는 E 코어에도 올라간다.

| op \ 스레드 | 1 | 2 | 4 | 6 | 8 |
|---|---|---|---|---|---|
| LL/SC add (`ldaxr/stlxr`) | 3.6 | 6.1 (5.5–6.1) | 7.8 (7.6–7.9) | 12.1 (11.0–13.8) | 12.2 (12.1–14.1) |
| LSE `ldaddal` | **1.4** | 6.1 (6.0–6.1) | 6.5 (6.4–6.5) | **7.5** (7.1–7.5) | **7.3** (7.2–7.6) |
| LL/SC CAS 로 +1 | 3.5 | 8.6 (6.1–9.7) | 8.9 (7.6–9.5) | 9.7 (9.3–12.1) | 22.3 (13.4–22.4) |
| LSE `casal` 로 +1 | 1.5 | 21.7 (19.4–21.7) | 35.9 (33.6–36.4) | 42.1 (41.2–42.4) | 42.7 (36.5–47.4) |
| LL/SC 락 (빠른 경로 먼저 + WFE) | 6.1 | 11.7 (9.2–15.9) | 16.4 (13.6–22.5) | 18.0 (16.2–24.8) | 24.7 (22.7–35.2) |
| LL/SC 락 (매번 SEVL; WFE) | 15.5 | 32.9 (29.1–40.2) | 38.8 (34.9–41.1) | 40.2 (37.8–46.3) | 54.5 (48.1–57.0) |
| LL/SC 락 (WFE 없이 스핀) | 6.2 | 16.1 (10.7–18.2) | 18.2 (13.8–20.5) | 19.7 (19.2–20.4) | 24.1 (19.9–31.8) |
| LSE `swpa` 락 (+LDXR/WFE 대기) | 3.9 | 27.6 (19.8–29.9) | 36.0 (35.6–36.5) | 50.0 (41.2–54.5) | 57.0 (54.7–65.4) |
| LL/SC add 의 STLXR 실패 / 연산 | 0.00 | 0.08 | 0.26 | 0.48 | 0.52 |

```svg
<svg viewBox="0 0 740 372" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">ns/op (낮을수록 좋음), Apple M6 실측 중앙값 — 왼쪽: 카운터, 오른쪽: 락</text>
<g stroke="currentColor" stroke-width="0.5" stroke-dasharray="1 4"> <line x1="60" y1="242" x2="340" y2="242"/><line x1="60" y1="194" x2="340" y2="194"/>
<line x1="60" y1="146" x2="340" y2="146"/><line x1="60" y1="98" x2="340" y2="98"/><line x1="60" y1="50" x2="340" y2="50"/>
<line x1="430" y1="210" x2="710" y2="210"/><line x1="430" y1="130" x2="710" y2="130"/><line x1="430" y1="50" x2="710" y2="50"/> </g> <path d="M60 50 L60 290 L340 290" stroke="currentColor" fill="none"/>
<path d="M430 50 L430 290 L710 290" stroke="currentColor" fill="none"/> <g fill="currentColor">
<text x="40" y="294">0</text><text x="34" y="246">10</text><text x="34" y="198">20</text><text x="34" y="150">30</text><text x="34" y="102">40</text><text x="34" y="54">50</text>
<text x="410" y="294">0</text><text x="404" y="214">20</text><text x="404" y="134">40</text><text x="404" y="54">60</text>
<text x="56" y="308">1</text><text x="96" y="308">2</text><text x="176" y="308">4</text><text x="256" y="308">6</text><text x="336" y="308">8</text>
<text x="426" y="308">1</text><text x="466" y="308">2</text><text x="546" y="308">4</text><text x="626" y="308">6</text><text x="706" y="308">8</text> <text x="170" y="324">스레드 수</text><text x="540" y="324">스레드 수</text> </g>
<polyline points="60,272.7 100,260.7 180,252.6 260,231.9 340,231.4" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3"/>
<polyline points="60,283.3 100,260.7 180,258.8 260,254 340,255" fill="none" stroke="currentColor" stroke-width="2.5"/>
<polyline points="60,273.2 100,248.7 180,247.3 260,243.4 340,183" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="2 3"/>
<polyline points="60,282.8 100,185.8 180,117.7 260,87.9 340,85" fill="none" stroke="currentColor" stroke-width="2.5" stroke-dasharray="10 4"/> <g class="f-acc" stroke="currentColor">
<circle cx="340" cy="85" r="4"/><circle cx="340" cy="255" r="4"/> </g> <g fill="currentColor"> <text x="236" y="72">LSE casal +1</text> <text x="250" y="176">LL/SC CAS +1</text> <text x="270" y="225">LL/SC add</text>
<text x="270" y="272">LSE ldaddal</text> </g> <polyline points="430,265.6 470,243.2 550,224.4 630,218 710,191.2" fill="none" stroke="currentColor" stroke-width="2.5"/>
<polyline points="430,228 470,158.4 550,134.8 630,129.2 710,72" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="2 3"/>
<polyline points="430,274.4 470,179.6 550,146 630,90 710,62" fill="none" stroke="currentColor" stroke-width="2.5" stroke-dasharray="10 4"/> <g class="f-ok" stroke="currentColor"><circle cx="710" cy="191.2" r="4"/></g>
<g fill="currentColor"> <text x="600" y="76">LSE swpa 락</text> <text x="560" y="120">LL/SC 매번 SEVL</text> <text x="590" y="240">LL/SC 락 (빠른 경로)</text> </g>
<text x="10" y="344" fill="currentColor">굵은 실선 = LSE ldaddal / LL/SC 락,  긴 점선 = LSE casal / swpa 락,</text>
<text x="10" y="362" fill="currentColor">짧은 점선 = LL/SC CAS / 매번 SEVL 판,  중간 점선 = LL/SC add</text>
</svg>
```

읽는 법. 측정값은 위 표 그대로이고, 원인 설명은 **추정** 이다 — Apple 은 마이크로아키텍처를 공개하지 않는다.

1. **경합 없음(1 스레드):** LSE 가 2~3배 빠르다 (`ldaddal` 1.4 vs 루프 3.6 ns). 명령 하나와, 순서가 걸린 두 메모리 접근 + 분기의 차이다.
2. **`fetch_add` 경합:** LSE 는 2→8 스레드에서 6~7.5 ns 로 거의 평평하다. LL/SC 는 12 ns 까지 오른다. 마지막 줄을 보면 8 스레드에서 연산 하나당 STLXR 이 0.5번 실패한다 — 실패할 때마다 라인을 다시 얻어 오는 왕복이 낭비된다. LSE 는 실패라는 개념이 없다. 구현이 이 연산을 라인을 가져오지 않고 공유 캐시 쪽에서 처리할(far atomic) 수도 있다고 알려져 있다 (Apple 에서 미확인).
3. **CAS 루프는 반대다:** `casal` 로 +1 은 LL/SC CAS 보다 **2~4배 느렸다** (4 스레드 35.9 vs 8.9 ns). 추정: LL/SC 는 비교가 실패하면 `clrex` 로 조용히 나가 로컬에서 다시 돈다. `casal` 은 성공하든 실패하든 매번 완전한 원자 트랜잭션이다. 그래서 "LSE 가 항상 빠르다" 는 틀렸다. **실패가 없는 RMW(`ldadd`, `ldset`, `swp`)는 LSE, CAS 루프는 측정하고 판단** 이 이 기계의 결론이다.
4. **락:** `SWPA` 락은 경합 때 LL/SC 락보다 2배 이상 느렸다. LL/SC 락은 `ldaxr` 로 값을 보고 풀렸을 때만 쓴다. `SWPA` 는 시도할 때마다 쓴다 — 해제 순간 깨어난 모든 대기자가 동시에 `swpa` 로 쓰기 경쟁을 한다.
5. **`SEVL` 을 빠른 경로에 두지 말 것:** 경합 없이도 +9 ns 였다. WFE 유무(빠른 경로 판 vs WFE 없이 스핀)는 이 벤치에서 오차 범위 안이었다. WFE 의 이득은 속도가 아니라 **전력** 과, 기다리는 동안 버스를 두드리지 않는 것이다 — 배터리 기기·SMT 코어에서 의미가 있다.
6. 메모리 순서(relaxed vs acq_rel)별 비용과 per-thread 카운터 같은 해결책은 LLAT-B06 에서 다룬다. 여기서는 모두 acquire/release 판이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution             # 36/36, 6회 반복 모두
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o sol_asan && ./sol_asan   # 36/36, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o sol_tsan && ./sol_tsan               # 36/36, 경고 0 (하네스 주석 덕분)
```

실제 출력 (일반 빌드 1회, ASan 빌드, TSan 빌드 — 계측 빌드는 느려서 풀이 빈 상태를 훨씬 자주 본다):

```text
Part C pool: allocs=400000 empty_retries=1972
36/36 passed
Part C pool: allocs=400000 empty_retries=2169831
36/36 passed
Part C pool: allocs=400000 empty_retries=14014727
36/36 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `stxr` 의 상태를 무시 | 실패해도 메모리는 그대로 → 증가 유실 (실측 85만~97만 / 400만) | `cbnz %w[st], 1b` |
| 출력 제약에 `&` 없음 | 컴파일러가 출력과 주소에 같은 레지스터(x0)를 배정 → SIGSEGV (실측) | `"=&r"` |
| `"memory"` clobber 없음 | gcc 15 `-O2` 가 두 임계 구역의 `g_a += 1` 을 `+2` 한 번으로 합쳐 첫 unlock 뒤로 미뤘다 (`demo_clobber.c`, VM 실측). clang 21 은 이 코드에선 안 옮겼고 `./db noclobber` 도 3회 모두 정확했다 — 우연에 기대지 말 것 | 락·원자 asm 에 `: "memory"` |
| 배타 명령 대신 일반 `LDR`/`STR` 로 test-and-set (+`dmb`) | 확인과 쓰기 사이가 틈 → 둘이 동시에 "잡음". `./db plain` 실측 a = 1796159 / 2148478 / 2888133 (기대 4000000), 동시 진입 관찰 190만~290만 회 | `LDAXR`/`STXR` 쌍 또는 `SWPA` |
| 실패 시 재시도를 `ldaxr` 가 아닌 `stlxr` 로 | 모니터가 이미 풀렸고 값도 옛 값 → 다시 읽어야 한다 | `1b` 는 `ldaxr` 줄 |
| LDXR~STXR 사이에 일반 load/store, 함수 호출, 긴 계산 | 구현에 따라 계속 실패(livelock). 디버거 단일 스텝도 모니터를 지운다 | 몸통은 레지스터 연산 몇 개 |
| weak CAS 를 루프 없이 한 번 | 값이 같아도 가짜 실패 | 루프 안에서 weak, 한 번이면 strong |
| 대기 루프에서 `swp`/`stxr` 로 계속 쓰기 | 모든 대기자가 라인을 독점하려 한다 (swpa 락 실측 2배 느림) | 배타 읽기로 확인 + `WFE`, 풀렸을 때만 쓰기 |
| `sevl; wfe` 를 획득 첫머리에 | 경합 없어도 매번 비용 (15.5 vs 6.1 ns) | 빠른 경로 먼저, 잠겼을 때만 `wfe` |
| 모니터 없이 `WFE` | 해제 store 가 이벤트를 안 만들 수 있다 → 다음 인터럽트/타이머까지 잔다 | `ldxr`/`ldaxr` 로 읽은 직후 `wfe` |
| ARMv8.0 칩에 LSE 를 그대로 | 어셈블은 되지만(`.arch_extension`) A53/A72 에서 SIGILL | HWCAP/sysctl 확인, 또는 `-mcpu=` 를 칩에 맞게 |
| `_Alignas(16) uint64_t lo, hi;` | `_Alignas` 가 두 멤버 모두에 붙어 `sizeof == 32`, `hi` 가 +16 (실측) → `ldaxp` 가 패딩을 읽음 | `_Alignas(16) uint64_t lo; uint64_t hi;` + `_Static_assert(sizeof == 16)` |
| 경합 변수를 다른 데이터와 같은 라인에 | 그 데이터를 쓸 때마다 내 STXR 실패 + false sharing | 128 B (이 맥의 라인) 정렬, 혼자 쓰기 |

## 8. 면접 Q&A

<details>
<summary>Q1. LDXR/STXR 루프는 어떻게 원자성을 보장하나요? CAS 와 근본적으로 뭐가 다르죠?</summary>

`LDXR` 은 값을 읽으면서 그 주소의 granule 을 내 배타 모니터에 "Exclusive" 로 표시한다. 그 뒤 다른 관찰자가 granule 에 쓰면 표시가 지워지고, `STXR` 은 표시가 살아 있을 때만 쓰고 Ws = 0 을 준다. 실패하면 메모리는 그대로이니 처음부터 다시 돌면 된다. CAS 는 "값이 같으면" 쓰고, LL/SC 는 "그동안 아무도 안 썼으면" 쓴다. 그래서 LL/SC 는 한 위치에서 A→B→A 로 되돌아간 경우에도 실패한다 (ABA 에 강함). 다만 이 장점은 짧은 asm 몸통 안에서만 누릴 수 있다. C 의 CAS API 위에서 짠 알고리즘은 결국 값 비교라 ABA 가 남는다 — 연습 3 의 세대 태그가 C 수준의 해법이다 (DS-A04). 꼬리: "LL/SC 의 약점은?" → 가짜 실패, 경합 시 재시도 폭증(실측 8 스레드 0.52 회/연산), 몸통 제약, 디버거 단일 스텝에 약함.

</details>

<details>
<summary>Q2. CAS 실패 경로의 CLREX 는 꼭 필요한가요?</summary>

정확성으로는 대개 필요 없다. 다음 `LDXR` 이 모니터를 새로 걸고, 짝 없는 `STXR` 은 쓰지 않는다. 열린 채 남은 표시가 위험한 건 "LDXR 후 선점 → 같은 코어에서 일반 store → 복귀 후 STXR 성공" 같은 경우다. 이건 예외 반환/문맥 전환에서 모니터를 여는 것으로 막는다 (ARMv8-A 는 예외 반환이 로컬 모니터를 지운다고 알려져 있음 — 원문 미확인). 그래서 CLREX 는 위생이다. LLVM 도 실패 경로에 넣는다 (`-mcpu=generic -S` 실측). M-profile 은 예외 진입/복귀 규칙이 다르다 — ARM-M11.

</details>

<details>
<summary>Q3. LDXR 과 STXR 사이에 하면 안 되는 일은? 디버거로 그 루프를 한 줄씩 밟으면?</summary>

다른 명시적 load/store, 캐시 유지보수, 예외를 일으킬 수 있는 일, 긴 코드, 함수 호출 — 구현에 따라 STXR 이 영원히 실패할 수 있다 (DDI 0487 사용 제한, 문서 기준). 몸통은 레지스터 연산 몇 개로 쓴다. 단일 스텝은 매 스텝마다 디버그 예외 진입/복귀를 하며 모니터를 지울 수 있어, 루프가 끝나지 않는 고전적인 함정이 된다. gdb 의 AArch64 지원에는 LDXR…STXR 구간을 한 번에 건너뛰는 처리가 있는 것으로 알고 있다 (미검증). 실무 팁: 그 구간 뒤에 브레이크포인트를 걸고 continue.

</details>

<details>
<summary>Q4. LSE 가 있으면 LL/SC 는 쓸모없나요? LSE 가 항상 빠른가요?</summary>

아니다. 이 맥 실측으로 `fetch_add` 는 LSE 가 이긴다 — 경합 없이 1.4 vs 3.6 ns, 8 스레드 7.3 vs 12.2 ns. 그런데 **CAS 루프로 +1** 은 `casal` 이 4 스레드에서 35.9 ns, LL/SC CAS 가 8.9 ns 였다. `swpa` 스핀락도 경합 때 LL/SC 락보다 2배 넘게 느렸다. 실패 없는 RMW 는 LSE 가 유리하다. 조건부 갱신이나 "읽고 판단하고 쓰기" 는 값을 보고 안 쓰고 빠질 수 있는 LL/SC 가 유리할 수 있다. 또 LSE 에 없는 연산(가장 낮은 비트 찾아 지우기 등)은 LL/SC 가 한 번에 한다. 결론: 측정. 꼬리: "왜 그렇죠?" → 원인은 추정이다 (실패 없는 LSE 는 재시도 왕복이 없음 / CAS 는 실패해도 매번 원자 트랜잭션). Apple 은 이 부분을 공개하지 않는다고 덧붙인다.

</details>

<details>
<summary>Q5. outline atomics 가 뭔가요? Apple 에서는 왜 안 보이죠?</summary>

ARMv8.0 과 v8.1+ 모두에서 돌아야 하는 Linux 배포판 바이너리를 위한 것이다. 컴파일러가 원자 연산마다 `bl __aarch64_ldadd4_acq_rel` 같은 헬퍼를 부르고, 헬퍼(libgcc/compiler-rt)가 시작 때 HWCAP 으로 채운 `__aarch64_have_lse_atomics` 를 보고 `ldaddal` 또는 LL/SC 루프로 간다 (VM 에서 objdump 로 실제 확인). Ubuntu aarch64 의 gcc 15·clang 21 은 기본이 outline 이었다. `-mno-outline-atomics` 는 LL/SC 인라인, `-march=armv8.1-a` 는 LSE 인라인이다. Apple 은 모든 arm64 기기가 LSE 를 갖고 기본 CPU 가 apple-m1 이라 처음부터 `ldaddal` 인라인이다 — `-mno-outline-atomics` 를 줘도 같은 출력이었다. 펌웨어·커널은 대상 코어를 알므로 `-mcpu=cortex-a53` (LL/SC) / `cortex-a55` (LSE) 를 준다.

</details>

<details>
<summary>Q6. 스핀락에서 WFE 는 왜 쓰고, 무엇이 깨우나요? SEVL 은요?</summary>

`WFE` 는 이벤트 레지스터가 설 때까지 코어를 저전력으로 재운다. 깨우는 것: 다른 코어의 SEV, 인터럽트, 이벤트 스트림 타이머, 그리고 **내 글로벌 모니터가 Exclusive→Open 으로 바뀔 때**. 그래서 대기자는 `LDAXR` 로 락을 읽어 모니터를 걸고 `WFE` 로 잔다. 소유자의 `STLR WZR` 가 모니터를 지우며 이벤트를 만든다. ARMv7 은 unlock 에 `DSB; SEV` 가 필요했지만 v8 은 필요 없다. 이벤트가 `LDAXR` 과 `WFE` 사이에 와도 레지스터에 남아 있으므로 놓치지 않는다. `SEVL` 은 자기 이벤트를 미리 세워 "첫 WFE 는 통과" 시키는 용도다. 다만 획득 첫머리에 매번 두면 이 맥에서 경합 없이도 15.5 vs 6.1 ns 로 손해였다. 꼬리: "WFE 가 더 빠른가?" → 이 벤치에선 WFE 없는 스핀과 비슷했다. 이득은 전력과 버스 트래픽이다.

</details>

<details>
<summary>Q7. inline asm 으로 원자 연산을 쓸 때 컴파일러 쪽 함정은?</summary>

세 가지 모두 실제로 봤다.

1. **early-clobber `&` 누락** → clang 이 `ldaxr w0, [x0]` 로 주소 레지스터를 덮어 SIGSEGV.
2. **`"memory"` clobber 누락** → gcc 15 가 두 임계 구역의 `+1` 을 합쳐 락 밖으로 옮겼다. 메모리 오퍼랜드(`+Q`)는 "이 위치만 건드린다" 는 뜻이지 컴파일러 배리어가 아니다.
3. **`asm volatile` 누락** → 출력을 안 쓰는 store/unlock 이 지워질 수 있다.

덧붙여, 배타 명령은 `[Xn]` 형식만 받으므로 `Q` 제약을 쓴다. `%w`/`%x` 로 레지스터 폭을 맞추고, 비교가 있으면 `"cc"` 를 넣는다. TSan 은 asm 을 못 보므로 거짓 양성(올바른 락을 레이스로)과 거짓 음성(틀린 카운터를 조용히)이 둘 다 나온다 — `demo_tsan.c`.

</details>

<details>
<summary>Q8. LDAXR/STLXR 루프는 "완전한 배리어" 인가요? Linux 의 fully-ordered atomic 과 같나요?</summary>

아니다. acquire 읽기 + release 쓰기라서, RMW **앞의** 접근이 RMW 뒤로 가지 않고 **뒤의** 접근이 RMW 앞으로 오지 않는 정도다. 예를 들어 STLXR 뒤의 일반 load 가 STLXR 보다 먼저 수행될 수 있다(release 는 뒤쪽을 막지 않음). C11 seq_cst RMW 로는 이걸로 충분하다. seq_cst load 가 `ldar` 라서 앞선 `stlr`/`stlxr` 를 앞지르지 못하기 때문이다 (C-A03 의 `ldar` vs `ldapr`). 컴파일러 매핑도 실제로 `ldaxr/stlxr` 였다. Linux 의 값 반환 원자 연산(`atomic_add_return`)은 "앞뒤 모든 접근에 대한 완전한 배리어" 라는 더 강한 계약이다. 그래서 LL/SC 판은 `ldxr … stlxr` 뒤에 `dmb ish` 를 둔다고 알려져 있다 (arch/arm64/include/asm/atomic_ll_sc.h — 이 레슨에서 소스 미확인). 리트머스로 차이를 보는 건 ARM-M05.

</details>

<details>
<summary>Q9. 같은 걸 Cortex-M 과 RISC-V 에서는?</summary>

Cortex-M3/M4/M7/M33 은 `LDREX/STREX` (Thumb-2) 로 같은 루프를 쓴다. acquire/release 명령이 없어서(v8-M 의 `LDA/STL` 전까지) `DMB` 로 감싼다. 예외 진입/복귀가 로컬 모니터를 지워 ISR 이 끼면 STREX 가 실패한다 — ARM-M11 이 QEMU+gdb 로 강제한다. Cortex-M0/M0+ (ARMv6-M) 는 배타 명령이 아예 없다. 그래서 PRIMASK 임계 구역이나 RP2040 SIO 같은 하드웨어 스핀락을 쓴다 (CPP-A04). LSE 같은 명령은 없다. RISC-V A 확장은 `LR/SC` (= LDXR/STXR, 16명령 constrained loop 규칙 명문화) 와 `AMOADD/AMOSWAP` (= LSE 계열) 을 둘 다 가진다. 비교표는 RISCV-M04. 실제 칩에서 A 확장 유무는 확인할 것 (예: GD32VF103 = RV32IMAC 로 A 있음).

</details>

## 9. 추가 연습

### 연습 1 — 원자 max: LL/SC 와 `LDUMAXAL`

최대 큐 깊이 워터마크를 C-A03 은 CAS 루프로 만들었다. 이번엔 (a) `LDAXR/CMP/CSEL/STLXR` 루프 한 번, (b) LSE `LDUMAXAL` 한 명령으로 만들어라. 둘 다 옛 값을 돌려주고, 부호 없는 비교여야 한다.

<details>
<summary>답 보기</summary>

```c
"1: ldaxr  %w[old], %[mem]\n"
"   cmp    %w[old], %w[v]\n"
"   csel   %w[nv], %w[old], %w[v], hs\n"     /* old >= v (부호 없음) 이면 old */
"   stlxr  %w[st], %w[nv], %[mem]\n"
"   cbnz   %w[st], 1b\n"
/* LSE */  "ldumaxal %w[v], %w[old], %[mem]\n"
```

CAS 루프와 달리 "비교하고 고르기" 가 배타 구간 안의 레지스터 연산이라 루프가 한 겹이다. 값이 이미 크면 같은 값을 다시 쓴다. 쓰기를 아끼려면 `b.hs` 로 빠지며 `clrex` 하는 판도 된다. 부호 있는 비교면 `ge` 와 `LDSMAXAL`. 테스트: `0x80000000` 이 5 보다 크게 처리되는지(부호 없음), 4 스레드 × 10만 개 PRNG 값의 최대가 단일 스레드 계산과 같은지 (두 방식 모두).

</details>

### 연습 2 — LSE 만으로 버퍼 풀 alloc

Part C 의 `pool_alloc` 을 LL/SC 없이 써라. 힌트: `LDCLR` 은 옛 값을 돌려준다.

<details>
<summary>답 보기</summary>

```c
for (;;) {
    uint32_t m = ld32(mask);                    /* 정렬된 LDR = single-copy atomic 읽기 (힌트일 뿐) */
    if (m == 0u) return -1;
    uint32_t bit = m & (0u - m);                /* 가장 낮은 1비트 */
    uint32_t old;  /* ldclra bit, old, [mask] */
    if (old & bit) return __builtin_ctz(bit);   /* 그 비트를 1→0 으로 만든 건 나 */
}                                               /* 아니면 이미 남이 가져감: 내 LDCLR 은 0 을 0 으로 — 무해, 재시도 */
```

핵심은 **"지웠다" 가 아니라 "내가 1 을 0 으로 바꿨다"** 를 옛 값으로 판정하는 것이다. 여러 코어가 같은 후보를 골라도 `LDCLRA` 는 직렬화되고, 옛 값에 비트가 있는 건 정확히 한 명이다. 그래서 CAS 처럼 "전체 마스크가 그대로인가" 를 보지 않아도 되고, 다른 비트의 변화에는 실패하지 않는다. acquire(A)는 이전 소유자의 버퍼 쓰기를 보기 위함이다. 테스트: 단위(`0x5` → 0, 2, -1), 4 스레드 × 10만 alloc/free, 이중 소유 0, 마스크 복원.

</details>

### 연습 3 — 128비트 CAS: 값 + 세대 태그 (`LDAXP/STLXP`, `CASPAL`)

lock-free 스택의 ABA 를 막는 {포인터, 태그} 쌍을 위해 16바이트 CAS 를 두 방식으로 써라. 4 스레드가 CAS 루프로 `{lo+1, hi+3}` 를 10만 번씩 하면 `{400000, 1200000}` 이어야 한다.

<details>
<summary>답 보기</summary>

LL/SC: `ldaxp lo, hi, [p]` → `cmp lo, elo; ccmp hi, ehi, #0, eq; b.ne 2f` → `stlxp st, dlo, dhi, [p]`. **실패 경로에서도 본 값을 `stlxp` 로 다시 써 본다** (`2: stlxp st, lo, hi, [p]; cbnz st, 1b`). LDXP 가 읽은 두 반쪽이 같은 순간의 값이라는 보장은 짝 STXP 가 성공했을 때만 생긴다 (FEAT_LSE2 이전 규칙, 문서 기준). clang 도 `__atomic_compare_exchange_n` 128비트를 `-mcpu=generic` 에서 똑같이 낸다 — 실측: 불일치 블록에 `stlxp w12, x9, x8, [x0]`.

LSE: `caspal x4, x5, x6, x7, [x0]` — 비교/결과 쌍과 새 값 쌍은 **짝수 번호에서 시작하는 연속 레지스터** 여야 해서 `register uint64_t s0 __asm__("x4")` 로 고정한다. Apple 기본 빌드의 clang 출력도 `caspal x4, x5, x2, x3, [x0]` 이었다.

함정: `_Alignas(16) uint64_t lo, hi;` 는 두 멤버 모두를 16 정렬해 32바이트 구조체가 된다 (실측 `sizeof=32 offsetof(hi)=16`). 첫 멤버에만 붙이고 `_Static_assert(sizeof == 16)`. 테스트: 태그만 다른 불일치, 성공, LSE 판 동일 사례, 두 방식 스트레스 각각.

</details>

### 연습 4 — 이벤트 플래그: `STSETL` 로 올리고 `SWPA WZR` 로 몽땅 가져가기

8개 생산자 코어가 각자 슬롯을 채운 뒤 자기 비트를 올리고(FreeRTOS EventGroup / Zephyr `k_event` 의 핵심), 소비자는 쌓인 비트를 한 번에 가져가며 비운다. C-A03 연습 2 를 명령으로 내려 써라.

<details>
<summary>답 보기</summary>

post = `stsetl bits, [ev]` (옛 값 불필요 → ST 별칭, release: 슬롯 쓰기를 먼저 보이게). take = `swpa wzr, old, [ev]` (0 과 교환, acquire: 받은 비트의 슬롯을 본다). 가져가기와 비우기가 한 명령이라 그 사이에 온 비트를 잃지 않는다. 여러 생산자의 `stsetl` 은 원자 RMW 라 서로의 비트를 지우지 않는다 (`ldr/orr/str` 이면 lost update). 테스트: 단위(`0x5` 와 `0x4` post → take 는 `0x5`, 다시 take 는 0), 300 라운드 × 8 생산자에서 받은 비트의 슬롯 값이 그 라운드 값과 같고, 같은 비트를 두 번 받지 않는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice        # 16/16 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_asan && ./pr_asan   # 16/16, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan          # 16/16, 경고 0
```

## 10. 핵심 정리

- LL/SC = `LDAXR`(읽고 모니터 걸기) → 레지스터 연산 → `STLXR`(아무도 안 썼으면 쓰기, Ws=0) → 실패면 **`LDAXR` 부터** 다시. 값이 아니라 "쓰기가 있었나" 를 본다. 가짜 실패는 정상이다.
- inline asm 3종 세트: 출력 전부 `=&r`, 메모리는 `+Q`(*p), 순서가 필요하면 `"memory"` (+ 비교 시 `"cc"`). 빠뜨린 결과를 실제로 봤다: SIGSEGV, gcc 가 임계 구역 쓰기를 합침, 24% 유실.
- CAS 실패 경로는 `CLREX` 로 정리한다 (위생, LLVM 도 넣음). 락은 `LDAXR`+`STXR` 로 잡고 `STLR WZR` 로 푼다. 잠겨 있을 때만 `WFE` 한다 — 모니터 해제가 이벤트이므로 놓치지 않는다. `SEVL`+`WFE` 를 빠른 경로에 두지 않는다.
- LSE(ARMv8.1): `LDADD/LDSET/LDCLR/SWP/CAS/CASP` + `A/L/AL`. ARMv8.0 코어(A53/A72)에선 SIGILL — HWCAP/sysctl 로 확인한다. 배포판은 outline atomics(`__aarch64_*` 헬퍼가 실행 중에 선택), Apple 은 기본 인라인 LSE.
- 이 맥 실측: `ldaddal` 은 LL/SC 보다 빠르고 경합에도 평평하다 (8T 7.3 vs 12.2 ns). 그러나 `casal` CAS 루프와 `swpa` 락은 LL/SC 판보다 2~4배 느렸다. "LSE 가 항상 빠르다" 는 틀렸다 — 실패 없는 RMW 는 LSE, 나머지는 측정.
- LL/SC 는 "찾아서 지우기" 같은 **임의의 RMW** 를 한 번에 한다 (버퍼 풀). LSE 만으로는 옛 값 판정 트릭(`LDCLRA`)이나 CAS 루프를 쓴다.
- TSan 은 asm 을 못 본다 — 올바른 asm 락은 거짓 양성, 틀린 asm 카운터는 거짓 음성. `__tsan_acquire/__tsan_release` 주석과 정확한 스트레스 CHECK 로 검증한다.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — "Synchronization and semaphores" 장: exclusive monitors (local/global), Exclusives Reservation Granule, Load-Exclusive/Store-Exclusive usage restrictions; LDXR/STXR/LDAXR/STLXR/LDXP/STXP/CLREX, LSE 원자 명령(LDADD, LDCLR, LDSET, LDUMAX, SWP, CAS, CASP) 설명; "Wait for Event and Send Event" (WFE/SEV/SEVL, 이벤트 레지스터); CTR_EL0.ERG — <https://developer.arm.com/documentation/ddi0487/latest/>
- ARM Synchronization Primitives (DHT0008A) — LDREX/STREX, CLREX, 문맥 전환: <https://developer.arm.com/documentation/dht0008/a/>
- GCC 문서 — Extended Asm (clobber `"memory"`, early-clobber `&`), Machine Constraints (AArch64 `Q`), `-moutline-atomics`: <https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html>, <https://gcc.gnu.org/onlinedocs/gcc/AArch64-Options.html>
- Clang ThreadSanitizer 문서 / `sanitizer/tsan_interface.h` (`__tsan_acquire`, `__tsan_release`): <https://clang.llvm.org/docs/ThreadSanitizer.html>
- Apple — `sysctlbyname` (`hw.optional.arm.FEAT_LSE` 등 기능 확인): <https://developer.apple.com/documentation/kernel/1387446-sysctlbyname>; Writing ARM64 code for Apple platforms: <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
- C11 (N1570) 7.17.7.4 compare_exchange (weak 의 가짜 실패), 5.1.2.4 (데이터 레이스) — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- C/C++11 mappings to processors (Cambridge): <https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html>
- Linux kernel — `arch/arm64/include/asm/atomic_ll_sc.h`, `atomic_lse.h`, `cmpxchg.h` (`__cmpwait`), `Documentation/atomic_t.txt` (fully ordered 정의): <https://elixir.bootlin.com/linux/latest/source/arch/arm64/include/asm>
- 관련 레슨: C-A02 (컴파일러 배리어), C-A03 (stdatomic, arm64/M4 asm 읽기), C-M14 (ISR ↔ main), OS-B04/B05 (카운터 레이스·처리량), OS-M07 (TAS/TTAS/ticket), OS-M08 (패턴), CPP-A04 (Cortex-M lock-free 여부, M0), MCPP-M13 (fence 규칙), ARM-M05 (DMB/LDAR/STLR 리트머스), ARM-M11 (Cortex-M LDREX/STREX + 예외), RISCV-M04 (LR/SC·AMO), LLAT-B06 (memory_order 비용), ARCH-M08 (false sharing), DS-A04 (ABA)
