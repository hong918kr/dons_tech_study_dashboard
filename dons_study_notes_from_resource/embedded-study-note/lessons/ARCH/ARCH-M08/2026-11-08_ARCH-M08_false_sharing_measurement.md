---
id: ARCH-M08
time: 90분
updated: 2026-11-08
---
# ARCH-M08 False Sharing 측정

## 학습 목표

- 두 스레드가 **각자 자기 카운터만** 올리는데도 느려지는 현상(false sharing)을 stride 4–512 B 스윕으로 재현하고, 느려짐이 끝나는 거리로 **일관성 단위(coherence granule)** 를 추정할 수 있다.
- 이 맥(Apple M6)에서 잰 결과를 그대로 말할 수 있다: 같은 무리 코어끼리(Super/Performance 끼리, E 끼리)는 **64 B**, E 코어와 Super/P 코어 사이는 **128 B** 에서야 깨끗해진다. 그래서 패딩은 `hw.cachelinesize` = 128 B.
- 벤치마크에서 증가가 지워지지 않게 만드는 세 가지 방법(relaxed `fetch_add` → `ldadd`, volatile 저장만 → `str`, volatile 읽고 쓰기 → `ldr/add/str`)을 실제 `-S` 로 확인하고, 왜 RMW 는 8배 느려지는데 저장만 하는 루프는 2배밖에 안 느려지는지 설명한다.
- 구조체 설계 규칙(쓰는 주체별로 라인 나누기, 읽기 위주 필드는 쓰기 라인과 떼기, 코어별 배열은 원소마다 라인 하나)을 **필드 표 검사기** 로 기계적으로 검사하고, `_Alignas` + `_Static_assert(offsetof(...) % 128 == 0)` 로 못 박는다.
- 리눅스 `____cacheline_aligned_in_smp`, C++ `std::hardware_destructive_interference_size`, Cortex-M7 32 B 라인(단일 코어 → 코어 사이 false sharing 은 없고 DMA 와의 라인 공유가 문제)을 연결한다.

## 1. 문제

"스레드마다 자기 카운터를 따로 두었는데 왜 코어를 늘릴수록 느려지죠?" — 멀티코어 펌웨어·드라이버·저지연 면접에서 바로 나오는 질문이고, 답은 **캐시는 변수 단위가 아니라 라인 단위로 소유권을 옮긴다** 는 것이다. 듀얼 코어 SoC(STM32H7 M7+M4, i.MX8M A53 클러스터, RP2040)의 코어별 통계 카운터, 드라이버 큐의 head/tail, ISR 이 올리는 카운터 옆의 설정 값 — 모두 같은 함정이다. 실리콘 검증 쪽에서는 거꾸로 "이 칩의 일관성 단위가 몇 바이트인가" 를 **측정으로** 알아내는 실험 설계 문제가 된다.

이미 아는 것(다시 가르치지 않는다): `stdatomic` 과 memory_order(C-A03), 원자 연산 하나의 비용과 경합(LLAT-B06), LL/SC vs LSE `ldadd`(ARM-M04), 스핀락이 만드는 라인 핑퐁(OS-M07), 코어 간 왕복 지연과 "패딩은 각자 쓸 때만 효과" 라는 관찰(LLAT-M05, 거기서 이미 P 64 B / E 128 B 를 봤다), 스레드별 칸 + 합산 카운터(OS-B05). 여기서는 **거리(stride)에 따른 느려짐 곡선** 과 **구조체를 어떻게 배치하나** 만 다룬다. 코어 수에 따른 일관성 트래픽 곡선은 MEM-M04, MESI 상태 전이 시뮬레이터는 ARCH-A04, `perf c2c` 로 HITM 을 찾는 도구 쪽은 LPERF-A03, DMA 와 캐시 유지보수는 ARCH-M12 가 맡는다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">카운터 두 개 (A 는 스레드 0, B 는 스레드 1 만 쓴다) — 거리에 따라 같은 블록인가</text>
  <text x="10" y="44" fill="currentColor" font-size="12">128 B 라인 하나 = 64 B 반쪽 두 개 (이 맥 hw.cachelinesize = 128)</text>
  <rect x="120" y="56" width="256" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="376" y="56" width="256" height="34" class="f-mut" stroke="currentColor"/>
  <text x="200" y="78" fill="currentColor" font-size="12">0–63 B</text>
  <text x="460" y="78" fill="currentColor" font-size="12">64–127 B</text>
  <text x="10" y="78" fill="currentColor" font-size="12">라인 n</text>
  <text x="10" y="122" fill="currentColor">stride 4 B</text>
  <rect x="120" y="106" width="16" height="24" class="f-warn" stroke="currentColor"/>
  <rect x="136" y="106" width="16" height="24" class="f-warn" stroke="currentColor"/>
  <text x="124" y="123" fill="currentColor" font-size="12">A</text><text x="140" y="123" fill="currentColor" font-size="12">B</text>
  <text x="170" y="123" fill="currentColor" font-size="12">같은 64 B 반쪽 → 어느 코어 쌍이든 충돌 (8배)</text>
  <text x="10" y="166" fill="currentColor">stride 64 B</text>
  <rect x="120" y="150" width="16" height="24" class="f-acc" stroke="currentColor"/>
  <rect x="376" y="150" width="16" height="24" class="f-acc" stroke="currentColor"/>
  <text x="124" y="167" fill="currentColor" font-size="12">A</text><text x="380" y="167" fill="currentColor" font-size="12">B</text>
  <text x="150" y="167" fill="currentColor" font-size="12">반쪽은 다르고 라인은 같다</text>
  <text x="400" y="167" fill="currentColor" font-size="12">S/P 끼리 · E 끼리: 깨끗 / E↔S·P: 충돌</text>
  <text x="10" y="210" fill="currentColor">stride 128 B</text>
  <rect x="120" y="194" width="16" height="24" class="f-ok" stroke="currentColor"/>
  <text x="124" y="211" fill="currentColor" font-size="12">A</text>
  <rect x="640" y="194" width="16" height="24" class="f-ok" stroke="currentColor"/>
  <text x="644" y="211" fill="currentColor" font-size="12">B</text>
  <text x="150" y="211" fill="currentColor" font-size="12">다른 라인 (B 는 라인 n+1) → 모든 코어 쌍에서 깨끗</text>
  <rect x="10" y="236" width="720" height="84" rx="6" fill="none" stroke="currentColor"/>
  <text x="24" y="258" fill="currentColor" font-weight="bold">왜 느려지나 (MESI 한 줄 요약, 자세히는 ARCH-A04)</text>
  <text x="24" y="278" fill="currentColor">쓰려면 그 라인을 혼자 가져야 한다(M/E). 다른 코어 사본은 무효(I)가 되고, 그 코어가 자기 카운터를</text>
  <text x="24" y="296" fill="currentColor">쓰려면 다시 소유권을 빼앗아 온다 → 서로 다른 변수인데 라인이 코어 사이를 계속 오간다 (RFO 핑퐁).</text>
  <text x="24" y="314" fill="currentColor" font-size="12">"얼마나 떨어져야 안 오가나" = 하드웨어가 소유권을 추적하는 단위. 스펙 숫자가 아니라 측정으로 확인한다.</text>
</svg>
```

체크 테스트(`starter.c` → `solution.c`)는 **결정적인 것만** 본다: 레이아웃 산수, 패딩된 구조체의 `offsetof/sizeof/alignof` 가 규칙을 지키나, 스레드마다 자기 칸을 올린 뒤의 **정확한 합계** 와 이웃 바이트가 0 인가, 측정표 → 경계 추정 함수가 고정된 표에서 맞는 답을 내나. **시간은 `demo_bench.c` 에서만** 잰다.

### Part A — 레이아웃 산수와 한 칸 = 한 라인

- `granules_touched(addr, size, g)` — 바이트 범위 `[addr, addr+size)` 가 걸치는 g-블록 수 (g 는 2의 거듭제곱, size 0 → 0).
- `share_granule(a, sa, b, sb, g)` — 두 범위가 같은 블록을 하나라도 같이 쓰나 (겹치지 않아도 같은 블록이면 true).
- `round_up(n, g)` — g 의 배수로 올림.
- `slot_t` — 스레드 하나가 혼자 쓰는 카운터 칸. `sizeof == alignof == 128` 이 되도록 고치고 `_Static_assert` 로 못 박는다. 힙에서는 `aligned_alloc` 으로.

### Part B — 스윕 기계와 경계 추정

- `bump(slot, mode, n)` — 칸의 uint32 를 n 번 올린다. 세 모드: `MODE_ATOMIC`(relaxed `atomic_fetch_add`), `MODE_VSTORE`(지역 변수를 올려 volatile 로 **저장만**), `MODE_VRMW`(volatile 로 읽고 더해 쓰기). 컴파일러가 루프를 "한 번에 n 더하기" 로 접으면 안 된다.
- 하네스(주어짐) `run_threads` — 2 / 4 / 12 스레드가 `base + i*stride` 의 칸을 각자 올린다. stride 4…512 B 모든 조합에서 **칸 값 == 횟수** 이고 칸이 아닌 바이트는 0 이어야 한다.
- `estimate_granule(strides, ns, n, tol)` — 측정표(stride → ns/증가)에서 기준(stride ≥ 256 의 평균)의 `tol` 배 이내가 위에서부터 끊기지 않고 이어지는 가장 작은 stride. 이 맥에서 잰 두 표(S/P 끼리, E 가 낌)를 넣으면 64 와 128 이 나와야 한다.
- 관찰: `demo_bench.c` — `sweep ui|bg`(stride 스윕), `pairs`(코어 종류 쌍별), `nthr`(스레드 수), `struct`(구조체 전/후).

### Part C — 듀얼 코어 패킷 큐의 구조체 설계

`pq_t` 는 듀얼 코어 SoC 의 패킷 큐 제어 블록이다. core0(RX ISR, 생산자)이 `head / rx_packets / tail_cache` 를, core1(프로토콜 스택, 소비자)이 `tail / rx_handled / head_cache` 를 쓰고, `ring / mask / irq` 는 초기화 때만 쓴 뒤 둘 다 읽고, `name / err_overrun` 은 거의 안 쓴다.

- `layout_violations(fields, n, g)` — 필드 표(이름, offset, size, 쓰는 그룹)를 받아 **같은 g-블록 안에 서로 다른 쓰기 그룹** 이 있거나 **읽기 위주(RO) 필드와 쓰기 그룹** 이 같이 있는 쌍의 수를 센다. COLD 는 어디 있어도 된다.
- starter 의 `pq_t` 는 "의미 순서" 로 붙여 놓은 64 B 짜리다. 위반 0, 정렬 128, 크기 4 라인 이하가 되도록 다시 배치하고 `_Static_assert` 를 단다.
- 두 스레드가 각자 자기 그룹 필드만 20만 번 갱신 → 합계가 정확해야 한다 (배치를 바꿔도 동작은 같다는 확인). 속도 차이는 `demo_bench.c struct`.

## 2. Problem (English)

You are characterizing false sharing on a heterogeneous SoC (Apple M6: 2 Super + 4 Performance + 6 Efficiency cores, `hw.cachelinesize` = 128) and turning the result into layout rules for firmware/driver structs.

**Part A.** Implement `size_t granules_touched(uintptr_t addr, size_t size, size_t g)` (number of `g`-aligned blocks the byte range `[addr, addr+size)` touches; 0 for `size == 0`), `bool share_granule(uintptr_t a, size_t sa, uintptr_t b, size_t sb, size_t g)` (do the two ranges touch a common block, even without overlapping bytes?), and `size_t round_up(size_t n, size_t g)` (`g` a power of two). Fix `slot_t` so that one slot occupies exactly one 128-byte line (`sizeof == alignof == 128`), enforced at compile time.

**Part B.** Implement `void bump(void *slot, int mode, uint32_t n)` that increments the `uint32_t` at `slot` `n` times using (a) relaxed `atomic_fetch_add`, (b) a register-local counter stored through a `volatile` pointer (no load), (c) a `volatile` load-add-store. The compiler must not collapse the loop. With the provided harness, 2, 4 and 12 threads each bump their own slot at `base + i*stride` for stride ∈ {4, 8, …, 512}; every slot must equal the iteration count and every non-slot byte must stay 0. Implement `size_t estimate_granule(const size_t *strides, const double *ns, int n, double tol)`: baseline = mean of entries with stride ≥ 256; walk down from the largest stride while `ns[i] <= tol * baseline`; return the smallest stride reached (0 for invalid input: `n < 1`, `tol < 1`, strides not strictly increasing, or no entry ≥ 256).

**Part C.** Implement `int layout_violations(const field_t *f, int n, size_t g)`: count field pairs `(i < j)` that touch a common `g`-block while belonging to two different writer groups, or to a read-mostly group and a writer group (cold fields never count). Re-layout `pq_t` (producer core0 fields, consumer core1 fields, read-mostly config, cold fields) so that it has 0 violations at `g = 128`, `alignof == 128`, size a multiple of 128 and at most 512 bytes.

Example: `share_granule(0,4, 64,4, 128) == true`, `share_granule(0,4, 64,4, 64) == false`; the M6 table `{10.71, 11.50, 11.55, 11.41, 1.41, 1.37, 1.36, 1.37}` for strides 4…512 gives 64; `{17.75, 10.84, 17.95, 16.98, 14.76, 2.27, 2.07, 2.15}` gives 128.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/ARCH/ARCH-M08
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o /tmp/ARCH-M08_work/st && /tmp/ARCH-M08_work/st; echo "exit $?"
```

실제 출력 (앞부분과 끝, 0.1초 안에 끝난다 — TODO 는 값을 돌려줄 뿐 멈추지 않는다):

```text
-- Part A: layout arithmetic --
FAIL  granules_touched(0, 1, 128) == 1  (line 182)
...
FAIL  sizeof(slot_t) == GRANULE  (line 194)
FAIL  alignof(slot_t) == GRANULE  (line 195)
...
FAIL  ok2  (line 222)
FAIL  ok4  (line 223)
...
FAIL  layout_violations(naive, 4, 128) == 3  (line 250)
...
      pq_t: sizeof 64, alignof 8, head@0 tail@4, violations@128 = 0
FAIL  alignof(pq_t) == GRANULE  (line 267)
FAIL  sizeof(pq_t) % GRANULE == 0 && sizeof(pq_t) <= 4 * GRANULE  (line 268)
FAIL  offsetof(pq_t, head) % GRANULE == 0 && offsetof(pq_t, tail) % GRANULE == 0  (line 269)
14/42 passed
exit 1
```

starter 의 `pq_t` 는 64 B 에 11개 필드가 다 들어 있다 (head@0, tail@4). 위반 0 으로 찍히는 것은 `layout_violations` 가 TODO 라서다 — checker 를 먼저 채우면 이 숫자가 **27** 이 된다 (RO 3개 × 쓰기 필드 6개 = 18, core0 3개 × core1 3개 = 9).

## 4. 힌트

<details><summary>힌트 1 — 블록 번호로 생각하기</summary>

주소 `x` 의 블록 번호는 `x / g`. 범위 `[a, a+s)` 의 마지막 바이트는 `a+s-1`. 그러면 걸치는 블록 수는 `(a+s-1)/g - a/g + 1`, 두 범위가 블록을 같이 쓰는지는 "블록 번호 구간 두 개가 겹치나" 다.
</details>

<details><summary>힌트 2 — 정렬 지정자는 어디에 붙이나</summary>

C11 `_Alignas` 는 typedef 에 붙일 수 없다 (C11 6.7.5p2). 구조체의 **첫 멤버** 에 붙이면 구조체 정렬이 그만큼 올라가고, `sizeof` 는 정렬의 배수로 올림된다 (6.5.3.4 — 배열 원소 간격이 곧 sizeof). 그래서 `struct { _Alignas(128) _Atomic uint32_t v; }` 하나로 크기 128, 정렬 128 이 된다. 구조체 중간 멤버에 붙이면 그 멤버가 새 라인에서 시작한다.
</details>

<details><summary>힌트 3 — 루프가 지워지지 않게</summary>

`uint32_t *p` 로 `*p = *p + 1` 을 n 번 하면 -O2 는 `ldr; add w8, w8, w1; str` 한 번으로 접는다 (`demo_asm.c` 의 `inc_plain`). 원자 RMW 는 접지 않는다(clang 이 relaxed RMW 를 합치지 않는다 — 표준이 금지하는 건 아니지만 실제 동작). volatile 접근은 "관찰 가능한 동작" 이라 횟수 그대로 나온다(C11 5.1.2.3p6). `vstore` 는 시작값을 칸에서 한 번 읽고 지역 변수로 센 뒤 매번 저장만.
</details>

<details><summary>힌트 4 — estimate_granule 은 위에서부터</summary>

작은 stride 쪽은 잡음이 많다 (예: 8 B 가 우연히 빠름). 큰 stride 부터 내려오다 처음 `tol` 을 넘는 곳에서 멈추면 그런 점에 속지 않는다. 기준 = stride ≥ 256 항목 평균.
</details>

<details><summary>힌트 5 — pq_t 는 그룹별로 모으고 그룹마다 새 라인</summary>

RO + COLD 를 맨 앞(라인 0)에, core0 그룹 첫 필드에 `_Alignas(128)`(라인 1), core1 그룹 첫 필드에 `_Alignas(128)`(라인 2). 그러면 크기는 384 B. `_Static_assert(offsetof(pq_t, head) % 128 == 0, ...)` 를 달아 두면 누가 필드를 끼워 넣어도 빌드가 깨진다.
</details>

## 5. 풀이

### Part A — 레이아웃 산수

| 함수 | 식 | 함정 |
|---|---|---|
| `granules_touched` | `(a+s-1)/g - a/g + 1` | `a+s` 를 쓰면 블록 끝에 딱 맞는 범위(128, 128)가 2로 나온다 |
| `share_granule` | `a0 <= b1 && b0 <= a1` (블록 번호 구간) | 바이트가 겹치는지를 보면 안 된다 — 0..3 과 64..67 은 안 겹치지만 128 B 블록은 같다 |
| `round_up` | `(n + g - 1) & ~(g - 1)` | g 가 2의 거듭제곱일 때만. 0 → 0 |

정렬을 거는 방법과 각 방법이 실제로 보장하는 것 (이 맥 clang 21 에서 확인):

| 방법 | 크기 | 정렬 | 결과 |
|---|---|---|---|
| `struct { _Alignas(128) _Atomic uint32_t v; }` | 128 | 128 | 정답. 배열 `slot_t s[12]` 의 원소마다 라인 하나 |
| `typedef _Alignas(128) struct {...} t;` | — | — | **컴파일 에러** `'_Alignas' attribute only applies to variables and fields` |
| `union { T v; char pad[128]; }` (크기만 패딩) | 128 | 8 | 배열 시작이 0x…08 이면 원소마다 라인 두 개에 걸친다 (연습 2) |
| `__attribute__((aligned(128)))` 를 struct 타입에 | 128 | 128 | GNU 확장. 리눅스 `____cacheline_aligned_in_smp` 가 이 형태 (연습 2) |
| `malloc(n * sizeof(slot_t))` | — | 16 | macOS malloc 은 16 B 정렬. 실측 `malloc(24) % 128 = 80` → **정렬이 깨진다** |
| `aligned_alloc(128, n * 128)` | — | 128 | 정답. 단 크기가 정렬의 배수여야: `aligned_alloc(4096, 6144)` 은 이 맥에서 **NULL** 을 돌려줬다 (C11 7.22.3.1 은 배수를 요구, C17 에서 완화 — 구현은 여전히 거부할 수 있다) |

### Part B — 세 가지 증가와 실제 명령

`demo_asm.c` 를 세 타깃으로 `-S` 한 결과 (루프 부분만 발췌, 명령은 그대로):

```bash
cc -std=c11 -O2 -S demo_asm.c -o -                                        # arm64
cc -std=c11 -O2 -S -arch x86_64 demo_asm.c -o -                           # x86_64
clang -std=c11 -O2 -S --target=thumbv7em-none-eabi -mcpu=cortex-m7 demo_asm.c -o -
```

```text
arm64 (Apple clang 21)
_inc_atomic:   LBB0_2: ldadd w8, w9, [x0] ; subs w1, w1, #1 ; b.ne LBB0_2
_inc_vstore:   LBB1_2: str w8, [x0] ; add w8, w8, #1 ; subs w1, w1, #1 ; b.ne LBB1_2
_inc_vrmw:     LBB2_1: ldr w8, [x0] ; add w8, w8, #1 ; str w8, [x0] ; subs ... ; b.ne LBB2_1
_inc_plain:    ldr w8, [x0] ; add w8, w8, w1 ; str w8, [x0] ; ret        ← 루프가 사라졌다
x86_64
_inc_atomic:   lock incl (%rdi)  × 8 (언롤)
_inc_vstore:   movl %edx, (%rdi) × 8
_inc_vrmw:     incl (%rdi) × 4          ← 메모리 피연산자 inc (lock 없음 = 원자적이지 않음)
_inc_plain:    addl %esi, (%rdi)
thumbv7em (Cortex-M7)
inc_atomic:    ldrex r2, [r0] ; adds r2, #1 ; strex r1, r2, [r0] ; cmp r1, #0 ; bne   (× 4 언롤)
inc_vstore:    str r2, [r0] × 4
inc_vrmw:      ldr r3, [r0] ; add.w r3, r3, #1 ; str r3, [r0]  × 4
inc_plain:     ldr r2, [r0] ; add r1, r2 ; str r1, [r0]
```

- relaxed 이고 결과를 안 쓰는데도 clang 은 `stadd` 가 아니라 `ldadd w8, w9, [x0]` 를 냈다 (돌려받은 w9 는 버린다). 어느 쪽이든 LSE 원자 RMW 한 개 — LL/SC 루프가 아니다 (LSE 와 LL/SC 비교는 ARM-M04).
- 평범한 `*p += 1` 루프는 `inc_plain` 처럼 한 번의 덧셈이 된다. false sharing 벤치마크를 처음 짜면 이것 때문에 "차이가 없다" 는 결과가 나온다.
- Cortex-M7 은 `ldrex/strex` 루프. 그러나 M7 은 단일 코어라 다른 코어와의 false sharing 은 없다 (Part C 끝).

#### 측정: 2 스레드 stride 스윕 (`demo_bench.c sweep`)

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o /tmp/ARCH-M08_work/bench
/tmp/ARCH-M08_work/bench sweep ui ; /tmp/ARCH-M08_work/bench sweep bg
```

기계: `Apple M6`, perflevel0/1/2 physicalcpu = 2 / 4 / 6 (Super / Performance / Efficiency), `hw.cachelinesize` 128, `Apple clang version 21.0.0 (clang-2100.0.123.102)`. 3회 중앙값, 스레드당 atomic 2^24 · vstore/vrmw 2^27 증가. **load average 6.7 (ui) / 6.5 (bg)** — 다른 세션 10여 개가 동시에 벤치마크 중이었다. 절대 ns 보다 **배수(×, stride ≥ 256 평균 대비)** 를 믿는다. E% = 2^16 증가마다 `pthread_cpu_number_np` 로 본 코어 번호가 E(0–5)인 비율, 두 스레드의 최소–최대.

| stride | ui atomic ns (×) | ui vstore ns (×) | ui vrmw ns (×) | bg atomic ns (×) · E% | bg vstore (×) | bg vrmw (×) |
|---|---|---|---|---|---|---|
| 4 B | 11.45 (8.4) | 0.45 (1.9) | 0.93 (3.1) | 17.16 (7.1) · 0–48 | 0.66 (1.7) | 1.09 (2.3) |
| 8 B | 11.42 (8.4) | 0.48 (2.1) | 0.86 (2.9) | 19.51 (8.0) · 0–46 | 0.64 (1.6) | 1.16 (2.5) |
| 16 B | 11.08 (8.1) | 0.50 (2.2) | 0.90 (3.0) | 15.08 (6.2) · 0–77 | 0.71 (1.8) | 1.17 (2.5) |
| 32 B | 11.07 (8.1) | 0.45 (1.9) | 0.93 (3.2) | 19.24 (7.9) · 0–45 | 0.69 (1.7) | 1.08 (2.3) |
| 64 B | **1.40 (1.0)** | 0.25 (1.1) | 0.49 (1.6) | **21.71 (8.9)** · 0–55 | 0.72 (1.8) | 1.13 (2.4) |
| 128 B | 1.40 (1.0) | 0.23 (1.0) | 0.55 (1.9) | **2.48 (1.0)** · 0–23 | 0.39 (1.0) | 0.45 (0.9) |
| 256 B | 1.37 (1.0) | 0.22 (1.0) | 0.23 (0.8) | 2.45 (1.0) · 0–24 | 0.40 (1.0) | 0.47 (1.0) |
| 512 B | 1.36 (1.0) | 0.24 (1.0) | 0.36 (1.2) | 2.42 (1.0) · 0–21 | 0.40 (1.0) | 0.47 (1.0) |
| 추정 경계 (tol 1.5) | **64 B** | 64 B | 256 B (잡음) | **128 B** | 128 B | 128 B |

(ui 의 E% 는 전부 0–9 → 사실상 S/P 코어끼리. `bench cpus ui` 로 본 코어 번호도 6·7(Super)과 8·9(P)뿐이었다.)

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">ldadd 2 스레드: stride vs ns/증가 (실측 3회 중앙값, load 6.5–6.7)</text>
  <line x1="80" y1="250" x2="730" y2="250" stroke="currentColor"/>
  <line x1="80" y1="40" x2="80" y2="250" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="60" y="254">0</text><text x="54" y="164">10</text><text x="40" y="74">20 ns</text>
  </g>
  <line x1="76" y1="160" x2="730" y2="160" stroke="currentColor" stroke-dasharray="2 4"/>
  <line x1="76" y1="70" x2="730" y2="70" stroke="currentColor" stroke-dasharray="2 4"/>
  <rect x="90" y="146.9" width="26" height="103.0" class="f-acc" stroke="currentColor"/>
  <rect x="118" y="95.6" width="26" height="154.4" class="f-warn" stroke="currentColor"/>
  <rect x="170" y="147.2" width="26" height="102.8" class="f-acc" stroke="currentColor"/>
  <rect x="198" y="74.4" width="26" height="175.6" class="f-warn" stroke="currentColor"/>
  <rect x="250" y="150.3" width="26" height="99.7" class="f-acc" stroke="currentColor"/>
  <rect x="278" y="114.3" width="26" height="135.7" class="f-warn" stroke="currentColor"/>
  <rect x="330" y="150.4" width="26" height="99.6" class="f-acc" stroke="currentColor"/>
  <rect x="358" y="76.8" width="26" height="173.2" class="f-warn" stroke="currentColor"/>
  <rect x="410" y="237.4" width="26" height="12.6" class="f-ok" stroke="currentColor"/>
  <rect x="438" y="54.6" width="26" height="195.4" class="f-warn" stroke="currentColor"/>
  <rect x="490" y="237.4" width="26" height="12.6" class="f-ok" stroke="currentColor"/>
  <rect x="518" y="227.7" width="26" height="22.3" class="f-ok" stroke="currentColor"/>
  <rect x="570" y="237.7" width="26" height="12.3" class="f-ok" stroke="currentColor"/>
  <rect x="598" y="227.9" width="26" height="22.1" class="f-ok" stroke="currentColor"/>
  <rect x="650" y="237.8" width="26" height="12.2" class="f-ok" stroke="currentColor"/>
  <rect x="678" y="228.2" width="26" height="21.8" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="102" y="268">4 B</text><text x="182" y="268">8 B</text><text x="258" y="268">16 B</text><text x="338" y="268">32 B</text>
    <text x="418" y="268">64 B</text><text x="494" y="268">128 B</text><text x="574" y="268">256 B</text><text x="654" y="268">512 B</text>
  </g>
  <rect x="90" y="280" width="14" height="12" class="f-acc" stroke="currentColor"/>
  <text x="110" y="291" fill="currentColor" font-size="12">ui (S/P 끼리): 64 B 부터 1.4 ns</text>
  <rect x="330" y="280" width="14" height="12" class="f-warn" stroke="currentColor"/>
  <text x="350" y="291" fill="currentColor" font-size="12">bg (E 코어가 낌): 64 B 도 21.7 ns, 128 B 부터 2.5 ns</text>
  <text x="90" y="306" fill="currentColor" font-size="12">초록 = 간섭 없음. bg 의 기준이 2.4 ns 인 것은 E 코어가 느려서 (단일 스레드도 E 는 약 2.7배 느렸다)</text>
</svg>
```

#### 측정: 코어 종류 쌍별 (`demo_bench.c pairs`)

스윕만으로는 "E 가 끼었다" 가 흐릿하다(E% 0–55). 그래서 2^16 증가 묶음마다 **나와 상대의 코어 번호** 를 적고, 묶음 동안 상대 칸이 실제로 2^14 이상 늘었을 때(상대가 선점당해 놀고 있지 않았을 때)만 쌍 종류별로 모았다. 코어 번호 → 종류는 단일 스레드 의존 사슬 속도로 확인: cpu 0–5 ≈ 3.4 ns/iter, 6–7 ≈ 1.25, 8–11 ≈ 1.47 → 0–5 = E, 6–7 = Super, 8–11 = P (LLAT-M05 와 같은 결론, Apple 공식 매핑은 아님). 묶음 ns/op 중앙값, `pairs` (qos ui+ui / bg+bg / ui+bg 각 3회, load 6.2):

| stride | E–E | E–S | E–P | S–S | S–P | P–P |
|---|---|---|---|---|---|---|
| 32 B | 10.87 (n 22) | 17.84 | 18.85 | 11.66 | 9.83 | 8.65 |
| 64 B | **2.61** | **19.11** | **19.58** | **1.37** | **1.37** | **1.38** |
| 96 B | 2.07 | 19.83 | 20.74 | 1.36 | 1.37 | 1.38 |
| 128 B | 2.06 | 1.56 | 1.50 | – | 1.38 | 1.38 |
| 256 B | 2.04 | 1.50 | 1.50 | 1.50 | 1.43 | 1.44 |

E–E 표본이 적어서 `pairs bg`(bg+bg 6회, load 4.3)를 따로 돌렸다: E–E 32 B 11.81, **64 B 2.04 (n 1450)**, 96 B 2.04, 128 B 2.04 / E–S 64 B 15.63, 128 B 1.38. 같은 결론이다.

- **같은 무리끼리(E–E, S–S, S–P, P–P)는 64 B 에서 이미 깨끗하다.** 96 B(같은 128 B 라인의 다른 64 B 반쪽)도 깨끗.
- **E 와 S/P 사이는 64·96 B 에서도 15–20 ns** — 같은 128 B 라인이면 충돌, 128 B 에서야 깨끗.
- 해석(추정 — Apple 은 일관성 구현을 공개하지 않는다): 무리(클러스터) 안에서는 64 B 단위로 소유권이 옮겨 다니고, 무리 사이 일관성은 128 B 단위로 추적되는 것처럼 보인다. S 와 P 는 `hw.perflevel0/1.l2cachesize` 가 둘 다 20 MB 로 보고되는데, 같은 L2 를 나눠 쓰는지는 확인하지 못했다.
- **결론은 하나다: 이 맥에서 코어 간 분리는 128 B.** 스케줄러가 스레드를 E 코어로 옮기는 순간 64 B 패딩은 깨진다 (macOS 는 코어 고정이 안 된다, QoS 는 힌트).

#### 측정: 스레드 수 (`demo_bench.c nthr ui`, atomic, load 6.3)

| 스레드 | stride 4 B | 64 B | 128 B | 256 B |
|---|---|---|---|---|
| 2 | 11.90 | 1.52 | 1.52 | 1.52 |
| 4 | 21.38 | 1.40 | 1.40 | 1.40 |
| 6 | 32.77 | 2.80 | 1.71 | 1.90 |
| 8 | 46.03 | 2.40 | 1.85 | 1.89 |
| 12 | 71.40 (E% 12–84) | 4.02 | 2.43 | 2.03 |

한 라인을 N 스레드가 나눠 쓰면 증가 하나가 N 에 거의 비례해 느려진다 (2 → 12 스레드에서 11.9 → 71.4 ns, 6배). 64 B 열은 스레드가 6개를 넘어 E 코어가 끼기 시작하면서 다시 2–3배 나빠진다 — 앞 표와 같은 이야기. 이 곡선을 코어 수 · 일관성 메시지 수로 설명하는 것은 MEM-M04 의 일이다.

#### 왜 atomic 은 8배인데 vstore 는 2배인가

| 모드 | 명령 | 같은 라인일 때 | 설명 (추정 포함) |
|---|---|---|---|
| atomic | `ldadd` | ×8 (S/P), ×7–9 (E 낌) | 원자 RMW 는 라인을 독점한 상태에서 하나씩 수행돼야 하고 옛 값을 돌려준다. 상대가 라인을 빼앗아 가면 다음 `ldadd` 는 소유권이 돌아올 때까지 못 끝난다 |
| vstore | `str` | ×1.6–2.2 | 저장은 **store buffer** 에 들어가고 명령은 바로 끝난다. 라인 소유권은 버퍼를 비울 때만 필요하고, 같은 주소에 대한 연속 저장은 라인을 잡은 동안 한꺼번에 빠질 수 있다(병합 여부는 미공개 — 추정). 그래서 소유권 이동 한 번에 저장 여러 개가 묻어 간다 |
| vrmw | `ldr; add; str` | ×2–3 | 읽기는 대부분 자기 store buffer 에서 전달(store-to-load forwarding)받는다. 기준값 자체가 0.23–0.55 ns 로 run 마다 흔들려서 경계 추정에는 쓰지 않는다 |

실무 함의: 진짜 카운터는 대개 `atomic_fetch_add` 이거나, 락 안의 `++` 이거나, 다른 데이터와 섞인 읽기-쓰기다. 저장만 하는 루프로 false sharing 을 재면 피해를 **과소평가** 한다. 벤치마크는 실제 코드가 내는 명령으로 짠다.

#### estimate_granule

Part B CHECK 에 넣은 표는 같은 `sweep` 의 앞선 실행(load 5.0–5.3)의 atomic 열이다: ui `{10.71, 11.50, 11.55, 11.41, 1.41, 1.37, 1.36, 1.37}`, bg `{17.75, 10.84, 17.95, 16.98, 14.76, 2.27, 2.07, 2.15}`. 위 표(load 6.5–6.7)와 숫자는 조금 달라도 모양은 같다. ui 는 512→256→128→64 가 모두 1.5배 안, 32 에서 8배 → **64**. bg 는 128 까지 안, 64 에서 7배 → **128**. 잡음 대비: 8 B 만 우연히 빠른 표는 위에서 내려오다 64 에서 멈추므로 128 이 나온다 (아래 점을 보지 않는다). 기준 자체가 망가진 표(512 가 9 ns)는 0 — "다시 재라" 는 신호.

### Part C — 구조체 설계 규칙

`layout_violations` 는 규칙을 그대로 옮긴 이중 루프다. 그룹 쌍 판정표:

| 그룹 a \ b | COLD | RO | 같은 쓰기 그룹 | 다른 쓰기 그룹 |
|---|---|---|---|---|
| COLD | 괜찮음 | 괜찮음 | 괜찮음 | 괜찮음 |
| RO | 괜찮음 | 괜찮음 | **위반** (읽는 쪽이 계속 무효화당함) | **위반** |
| 쓰기 그룹 | 괜찮음 | **위반** | 괜찮음 (true sharing 아님, 한 코어) | **위반** (false sharing) |

COLD 를 봐주는 이유: 거의 쓰지 않으니 같은 라인에 있어도 핑퐁이 생기지 않는다. 대신 COLD 를 **쓰기 그룹 라인의 남는 자리** 에 넣는 건 괜찮지만 RO 라인에 두는 편이 캐시 낭비가 적다 — 핫 경로가 건드리지 않는 바이트가 핫 라인을 차지하지 않게.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">pq_t 배치: starter (64 B, 한 라인) → solution (384 B, 그룹마다 라인)</text>
  <text x="10" y="48" fill="currentColor" font-size="12">starter</text>
  <rect x="80" y="34" width="20" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="100" y="34" width="20" height="26" class="f-warn" stroke="currentColor"/>
  <rect x="120" y="34" width="40" height="26" class="f-mut" stroke="currentColor"/>
  <rect x="160" y="34" width="40" height="26" class="f-mut" stroke="currentColor"/>
  <rect x="200" y="34" width="20" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="220" y="34" width="20" height="26" class="f-warn" stroke="currentColor"/>
  <rect x="240" y="34" width="20" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="260" y="34" width="20" height="26" class="f-warn" stroke="currentColor"/>
  <rect x="280" y="34" width="100" height="26" fill="none" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="82" y="78" fill="currentColor" font-size="12">head tail ring/mask/irq rxp rxh tc hc  name err  — 전부 라인 0</text>
  <text x="400" y="52" fill="currentColor" font-size="12">위반 27쌍, 2 스레드 17–22 ns/갱신</text>
  <line x1="10" y1="96" x2="730" y2="96" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="10" y="124" fill="currentColor" font-size="12">solution</text>
  <rect x="80" y="108" width="200" height="30" class="f-mut" stroke="currentColor"/>
  <text x="88" y="128" fill="currentColor" font-size="12">라인 0: ring mask irq (RO) name err (COLD)</text>
  <rect x="290" y="108" width="200" height="30" class="f-acc" stroke="currentColor"/>
  <text x="298" y="128" fill="currentColor" font-size="12">라인 1 @128: head rxp tail_cache</text>
  <rect x="500" y="108" width="200" height="30" class="f-warn" stroke="currentColor"/>
  <text x="508" y="128" fill="currentColor" font-size="12">라인 2 @256: tail rxh head_cache</text>
  <text x="80" y="160" fill="currentColor" font-size="12">core0 만 쓰는 라인 / core1 만 쓰는 라인 / 둘 다 읽기만 하는 라인 — 위반 0, 2 스레드 1.5 ns/갱신</text>
  <rect x="80" y="180" width="14" height="12" class="f-acc" stroke="currentColor"/>
  <text x="100" y="191" fill="currentColor" font-size="12">core0 (생산자, RX ISR)</text>
  <rect x="280" y="180" width="14" height="12" class="f-warn" stroke="currentColor"/>
  <text x="300" y="191" fill="currentColor" font-size="12">core1 (소비자)</text>
  <rect x="440" y="180" width="14" height="12" class="f-mut" stroke="currentColor"/>
  <text x="460" y="191" fill="currentColor" font-size="12">RO / COLD</text>
  <text x="80" y="218" fill="currentColor" font-size="12">tail_cache = 생산자가 마지막으로 본 tail 사본 → 생산자 라인에</text>
  <text x="80" y="236" fill="currentColor" font-size="12">(DS-A01 · LLAT-M01 의 캐시된 인덱스와 같은 이유)</text>
</svg>
```

solution 실행 줄: `pq_t: sizeof 384, alignof 128, head@128 tail@256, violations@128 = 0`. `_Static_assert` 세 줄(head 와 tail 이 라인 시작, 크기가 128 의 배수)이 배치를 못 박는다 — 누가 `head` 앞에 필드를 끼워 넣어도 `_Alignas` 덕분에 head 는 여전히 라인 시작이지만, RO 필드를 쓰기 라인에 끼워 넣는 실수는 `layout_violations` CHECK 가 잡는다. 두 장치는 서로 다른 실수를 잡는다.

`demo_bench.c struct` (2 스레드가 각자 자기 그룹만 갱신: `fetch_add` release 1개 + volatile 갱신 2개, 2^23 회, 5회):

| 배치 | sizeof | ui ns/갱신 (load 6.5) | bg ns/갱신 |
|---|---|---|---|
| starter (붙여 놓음) | 64 | 17.41 21.08 18.29 19.85 22.26 | 14.30 31.76 33.72 32.47 31.10 |
| solution (그룹별 라인) | 384 | 1.46 1.49 1.49 1.53 1.75 | 2.42 2.51 2.49 2.41 2.41 |

메모리 320 B 를 더 써서 약 12배. 이 구조체가 큐 하나에 하나뿐이니 공짜에 가깝다.

#### 구조체 설계 가이드 (체크리스트)

1. **쓰는 주체를 표로 적는다.** 필드마다 "누가 쓰나(코어/ISR/DMA) · 얼마나 자주" — 이 표가 곧 `field_t` 배열이다.
2. **쓰기 그룹마다 라인 하나.** 그룹 첫 필드에 `_Alignas(LINE)`, `_Static_assert(offsetof(T, first) % LINE == 0, ...)`.
3. **읽기 위주(설정·포인터·마스크)는 쓰기 라인과 떼어 한 라인에 모은다.** 읽는 쪽이 매번 무효화당하지 않게.
4. **코어별/스레드별 배열은 원소마다 라인 하나** (`slot_t`, 리눅스 per-CPU 영역과 같은 생각). 힙이면 `aligned_alloc(LINE, round_up(n*sizeof, LINE))`.
5. **핫/콜드 분리.** 핫 경로가 훑는 필드만 따로 빽빽한 배열로 (연습 3: 같은 훑기에서 192 라인 → 32 라인). 패딩은 "코어가 다른 쓰기" 에만 — 객체마다 128 B 를 붙이면 캐시만 낭비한다.
6. **LINE 은 타깃마다 다르다.** 이 맥 128 (측정: 무리 사이 128), Cortex-A53/A72 보드 보통 64, Cortex-M7 D-cache 32. 모르면 큰 쪽.

#### 다른 세계의 같은 장치

- **리눅스**: `include/linux/cache.h` 의 `____cacheline_aligned_in_smp` 는 `CONFIG_SMP` 일 때 `__attribute__((__aligned__(SMP_CACHE_BYTES)))`, 단일 프로세서 빌드에서는 **빈 매크로** 다 — 코어가 하나면 코어 사이 false sharing 이 없으니까. arm64 의 `L1_CACHE_BYTES` 는 64(`L1_CACHE_SHIFT 6`)로 알고 있다 (커널 버전별 확인 필요). 코어별 데이터는 `DEFINE_PER_CPU` 로 코어마다 따로 떨어진 영역에 둔다.
- **C++17**: `std::hardware_destructive_interference_size`. 이 맥 clang 이 정의한 값은 `cc -dM -E -x c /dev/null` 로 보면 `__GCC_DESTRUCTIVE_SIZE 256`, `__GCC_CONSTRUCTIVE_SIZE 64` 다 — 측정한 128 보다 보수적. 그 값으로 SPSC 큐를 실제로 재는 것은 LLAT-M01.
- **x86**: 라인은 64 B 지만 인접 라인 프리페처(spatial prefetcher) 때문에 128 B 로 떼라는 권고가 흔하다 (folly 등 — 대표 사례, 직접 측정 안 함).
- **Cortex-M7**: L1 D-cache 라인 32 B (Cortex-M7 TRM, L1 cache 장 — 문서 기준). 단일 코어라 코어 사이 false sharing 은 없다. 문제는 **DMA 와 CPU 가 한 라인을 나눠 쓸 때**: DMA 수신 버퍼 꼬리와 ISR 카운터가 같은 32 B 라인이면 invalidate 가 ISR 의 갱신을 지우거나, clean 이 DMA 데이터를 덮는다. 같은 checker 를 `g = 32`, DMA 를 쓰기 그룹 3 으로 두면 잡힌다 (Part C CHECK 의 `dma32`, 연습 4). clean/invalidate 순서는 ARCH-M12.
- **듀얼 코어 MCU**: STM32H7 듀얼 코어(M7 + M4)는 공유 SRAM 의 카운터를 두 코어가 쓰면 M7 쪽 D-cache 라인 단위로 같은 문제가 생긴다(M4 쪽은 D-cache 가 없는 것으로 알고 있음 — 확인 필요). 보통 공유 영역을 MPU 로 non-cacheable 로 두는데, 그러면 false sharing 대신 매 접근이 버스 접근이 된다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o /tmp/ARCH-M08_work/so
for i in 1 2 3 4 5; do /tmp/ARCH-M08_work/so | tail -1; done
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o /tmp/ARCH-M08_work/so_asan && /tmp/ARCH-M08_work/so_asan | tail -1
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o /tmp/ARCH-M08_work/so_tsan && /tmp/ARCH-M08_work/so_tsan | tail -1
```

```text
-- Part B: sweep machinery (exact totals, no neighbour corruption) --
      mode atomic
PASS  ok2
PASS  ok4
...
PASS  estimate_granule(strides, ui, 8, 1.5) == 64
PASS  estimate_granule(strides, bg, 8, 1.5) == 128
...
      pq_t: sizeof 384, alignof 128, head@128 tail@256, violations@128 = 0
PASS  layout_violations(pq_fields, NPQ, 128) == 0
...
42/42 passed
```

5회 모두 `42/42 passed`, 약 0.1초. ASan+UBSan, TSan 모두 경고 없이 `42/42 passed`. 새니타이저 빌드에서는 `__has_feature(thread_sanitizer/address_sanitizer)` 로 `SAN_SCALE = 10` → 증가 횟수 1/10. TSan 이 조용한 이유: 스레드마다 **다른 4 B 객체** 만 쓰고, main 은 `pthread_join` 뒤에 읽는다 — 같은 라인이어도 서로 다른 메모리 위치(C11 3.14, 5.1.2.4)라 데이터 레이스가 아니다. false sharing 은 **성능 버그** 이지 정확성 버그가 아니라서 TSan 도 ASan 도 찾지 못한다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 벤치마크를 `uint32_t *p; for (...) (*p)++;` 로 | -O2 가 루프를 `add w8, w8, w1` 한 번으로 접는다 (`inc_plain`). 패딩 유무 차이가 0 으로 나온다 | `atomic_fetch_add_explicit(p,1,relaxed)` 또는 volatile, `-S` 로 루프가 남았는지 확인 |
| 저장만 하는 루프로 재고 "false sharing 은 2배쯤" 이라고 결론 | store buffer 가 소유권 이동을 가린다. 실제 RMW 카운터는 8배 | 실제 코드가 내는 명령으로 잰다 |
| `_Alignas(64)` 로 충분하다 | 이 맥에서 E 코어가 끼면 64 B 거리도 15–20 ns (8.9배). macOS 는 코어 고정이 안 된다 | `_Alignas(128)` (`hw.cachelinesize`) |
| `typedef _Alignas(128) struct {...} t;` | C11 6.7.5p2 위반, clang: `'_Alignas' attribute only applies to variables and fields` | 첫 멤버에 `_Alignas(128)` |
| `union { T v; char pad[128]; }` 로 크기만 맞춤 | 정렬은 8 그대로. 배열이 0x…08 에서 시작하면 원소마다 라인 2개 → 이웃과 공유 (연습 2) | `_Alignas` 로 정렬도 |
| `malloc(n * sizeof(slot_t))` | `slot_t` 가 128 정렬이어도 malloc 은 16 B 정렬 (실측 `% 128 = 80`) | `aligned_alloc(128, round_up(n*128, 128))` |
| `aligned_alloc(4096, 6144)` | 크기가 정렬의 배수가 아니면 이 맥에서 NULL (실측). NULL 을 안 보면 바로 크래시 | 크기를 `round_up` 하고 NULL 검사 |
| 스레드별 칸 `long local[8]` 을 촘촘히 | 8칸 = 64 B = 라인 하나를 8 코어가 쓴다 (스레드 12개면 4 B stride 에서 71 ns/증가) | `slot_t local[8]` |
| 핫 카운터 옆에 읽기 위주 설정(`mask`, `ring`) | 읽는 코어가 매번 무효화당해 다시 가져온다 — 쓰는 쪽이 하나여도 느려진다 | RO 는 따로 한 라인 (`layout_violations` 규칙 2) |
| 모든 객체에 128 B 패딩 | 연결 1만 개 × 128 B = 1.2 MB, 캐시 낭비. 같은 코어만 쓰는 객체는 패딩이 필요 없다 | 패딩은 "다른 코어가 쓰는 이웃" 에만. 나머지는 핫/콜드 분리 |
| TSan 이 조용하니 괜찮다 | false sharing 은 레이스가 아니다. 도구는 `perf c2c`(HITM, LPERF-A03) 나 측정 | 벤치마크 + 레이아웃 CHECK |

코드로 보면 가장 흔한 첫 번째와 세 번째:

```c
/* 틀림: 루프가 사라진다 + 64 B 로는 E 코어와 충돌 */
struct { _Alignas(64) uint32_t v; } cnt[12];
void work(int me, uint32_t n) { for (uint32_t i = 0; i < n; i++) cnt[me].v++; }

/* 고침 */
typedef struct { _Alignas(128) _Atomic uint32_t v; } slot_t;
_Static_assert(sizeof(slot_t) == 128, "one line");
static slot_t cnt2[12];
void work2(int me, uint32_t n) { for (uint32_t i = 0; i < n; i++) atomic_fetch_add_explicit(&cnt2[me].v, 1, memory_order_relaxed); }
```

## 8. 면접 Q&A

<details><summary>Q1. False sharing 이 뭐고, 어떻게 재현하고 고치나요?</summary>

서로 다른 코어가 **서로 다른 변수** 를 쓰는데 그 변수들이 같은 캐시 라인(일관성 단위)에 있어서, 쓸 때마다 라인 소유권이 코어 사이를 오가는 현상. 재현: 스레드 두 개가 각자 자기 카운터를 `atomic_fetch_add` 로 올리고, 카운터 거리를 4 B → 512 B 로 바꾸며 ns/증가를 잰다. 이 맥에서는 같은 라인이면 11 ns, 떨어지면 1.4 ns (8배). 고침: 쓰는 주체마다 라인 하나 — `_Alignas(128)` + `_Static_assert`, 또는 스레드 로컬에 모았다가 가끔 공유 변수에 반영.

꼬리: "true sharing 과 차이는?" → true sharing 은 **같은 변수** 를 여러 코어가 쓰는 것(공유 카운터, 락 워드). 패딩으로는 안 고쳐지고 설계를 바꿔야 한다(스레드별 칸 + 합산 = OS-B05, MEM-M04 패턴 b). False sharing 은 배치만 바꾸면 사라진다.
</details>

<details><summary>Q2. 이 칩(Apple M6)에서 패딩은 몇 바이트로 하나요? 근거는?</summary>

128 B. `sysctl hw.cachelinesize` 도 128 이지만, 근거는 측정이다. 코어 종류 쌍별로 재 보니 같은 무리끼리(S–S, S–P, P–P, E–E)는 64 B 에서 이미 깨끗했고, E 코어와 S/P 코어 사이는 64·96 B 에서도 15–20 ns 로 충돌하다가 128 B 에서야 1.4 ns 가 됐다. macOS 는 코어 고정이 안 되고 QoS 는 힌트라서 스레드가 언제든 E 코어로 갈 수 있다 → 128.

꼬리: "64 B 와 128 B 가 갈리는 이유는?" → Apple 이 공개하지 않아서 추정만 한다: 무리 안에서는 64 B 단위로 소유권이 옮겨지고, 무리 사이 일관성은 128 B 단위로 추적되는 것처럼 보인다. 면접에서는 "측정 결과는 이렇고, 원인은 가설" 이라고 구분해서 말한다. "C++ 의 `hardware_destructive_interference_size` 는?" → 이 clang 에서 256 (`__GCC_DESTRUCTIVE_SIZE`), 보수적.
</details>

<details><summary>Q3. 벤치마크에서 증가가 최적화로 사라지지 않게 하려면? volatile 과 atomic 으로 재면 결과가 같나요?</summary>

평범한 `(*p)++` 루프는 -O2 에서 `add w8, w8, w1` 한 번이 된다(`-S` 로 확인). relaxed `atomic_fetch_add` 는 `ldadd` 가 루프마다 남고, volatile 접근도 횟수 그대로 남는다. 그러나 결과는 다르다: `ldadd` 는 같은 라인일 때 8배, volatile **저장만**(`str`)은 2배, volatile 읽고 쓰기는 2–3배. 저장은 store buffer 에 들어가고 바로 끝나서 라인 소유권 이동이 명령 실행에서 가려지기 때문이다(병합 세부는 추정). 원자 RMW 는 라인을 가진 상태에서 하나씩 끝나야 한다.

꼬리: "그럼 어느 쪽이 현실적?" → 실제 카운터가 내는 명령. 통계 카운터는 대개 atomic RMW 거나 락 안의 RMW 라서, 저장만 하는 루프는 피해를 과소평가한다.
</details>

<details><summary>Q4. 드라이버 구조체를 설계할 때 캐시 관점의 규칙은?</summary>

(1) 필드마다 누가 쓰나를 표로 적는다. (2) 쓰기 주체(코어/ISR/DMA)마다 라인 하나 — 생산자 인덱스와 소비자 인덱스는 다른 라인. (3) 읽기 위주 설정은 쓰기 라인과 떼어 한 라인에. (4) 코어별 배열은 원소마다 라인. (5) 핫 경로가 훑는 필드는 빽빽하게 모은다(핫/콜드 분리). (6) `_Static_assert(offsetof(T, head) % LINE == 0)` 로 못 박고, 레이아웃 검사기(필드 표 → 위반 수)를 테스트로 돌린다. 이 레슨의 `pq_t` 는 64 B → 384 B 로 커졌지만 2 스레드 갱신이 18 ns → 1.5 ns.

꼬리: "패딩의 비용은?" → 메모리와 캐시 용량. 큐 하나·코어 12개짜리 배열이면 몇 KB 라 공짜지만, 객체 수만 개에 붙이면 캐시를 낭비해 오히려 느려진다. 같은 코어만 쓰는 객체엔 붙이지 않는다.
</details>

<details><summary>Q5. Cortex-M7 같은 단일 코어 MCU 에서도 false sharing 을 신경 써야 하나요?</summary>

코어 사이 false sharing 은 없다 — 쓰는 코어가 하나뿐이니 라인이 오갈 곳이 없다. 리눅스도 `CONFIG_SMP` 가 꺼지면 `____cacheline_aligned_in_smp` 가 빈 매크로가 된다. 그러나 **DMA 와 CPU 가 32 B 라인 하나를 나눠 쓰면** 같은 모양의 버그가 정확성 문제로 나온다: RX 버퍼의 마지막 라인에 ISR 카운터가 있으면, 버퍼를 invalidate 할 때 ISR 의 갱신이 날아가거나, clean 할 때 오래된 캐시 내용이 DMA 데이터를 덮는다. 그래서 DMA 버퍼는 `_Alignas(32)` + 크기를 32 의 배수로. 같은 레이아웃 검사기를 g = 32 로 돌리면 잡힌다. 유지보수 순서는 ARCH-M12.

꼬리: "듀얼 코어 MCU 는?" → STM32H7 듀얼 코어, RP2040 같은 칩에서 공유 SRAM 의 카운터를 두 코어가 쓰면 (캐시가 있는 쪽 기준으로) 같은 문제가 다시 생긴다. 흔한 답은 공유 영역을 MPU 로 non-cacheable — false sharing 대신 매 접근 버스 비용.
</details>

<details><summary>Q6. 운영 중인 시스템에서 false sharing 을 어떻게 찾나요?</summary>

TSan·ASan 은 못 찾는다 — 데이터 레이스가 아니다(서로 다른 메모리 위치). 리눅스에서는 `perf c2c` 가 다른 코어의 Modified 라인을 읽는 HITM 이벤트를 라인·오프셋별로 모아 "한 라인 안에서 서로 다른 오프셋을 다른 코어가 쓴다" 를 보여 준다(LPERF-A03). 측정 없이 할 수 있는 것: 구조체 필드 표를 만들어 레이아웃 검사기를 테스트에 넣기(Part C), `pahole` 로 구조체 배치 보기(리눅스, 이 맥에서는 안 돌려 봄). 증상으로는 "스레드를 늘렸는데 처리량이 안 늘고, 코어별 데이터 구조인데도 그렇다".

꼬리: "macOS 에서는?" → `perf c2c` 같은 도구가 없어서, 의심 구조체를 패딩한 버전과 비교 측정하는 게 가장 확실했다(이 레슨 `demo_bench.c struct`: 12배).
</details>

<details><summary>Q7. 스레드 수를 늘리면 false sharing 비용은 어떻게 변하나요?</summary>

한 라인에 N 개 스레드 카운터가 모이면 증가 하나의 시간이 거의 N 에 비례한다: 이 맥에서 2 → 4 → 6 → 8 → 12 스레드가 11.9 → 21.4 → 32.8 → 46.0 → 71.4 ns. 라인 하나를 N 명이 차례로 빼앗아 가므로 각자 기다리는 시간이 늘어난다. 64 B 간격은 스레드가 6개를 넘어 E 코어가 끼기 시작하면 다시 2–4 ns 로 나빠지고, 128 B 간격은 1.4–2.4 ns 로 거의 평평하다(남는 증가는 E 코어가 느린 탓).

꼬리: "공유 카운터 하나를 모두가 쓰면?" → 그건 true sharing 이고, 곡선과 일관성 메시지 수의 대응은 MEM-M04 가 잰다.
</details>

## 9. 추가 연습

**연습 1 — 경계를 넘는 원소.** 128 정렬된 주소에서 시작하는 24 B 원소 배열의 앞 16개 중, 64 B 블록 두 개에 걸치는 원소는 몇 개? 128 B 블록이면? 32 B 원소 64개면? 48 B 원소 8개(128 B 블록)면?

<details><summary>답</summary>

원소 i 의 범위는 `[24i, 24i+24)`. 64 의 배수를 안에 품는 것: 48–71, 120–143, 240–263, 312–335 → **4개**. 128 의 배수: 120–143, 240–263 → **2개**. 32 B 원소는 128 을 나누므로 **0개**. 48 B 원소: 96–143, 240–287 → **2개**. 경계를 넘는 원소는 쓰기 한 번이 라인 두 개를 더럽히고, 양쪽 이웃 모두와 라인을 공유한다. 원소 크기를 라인의 약수로 맞추거나(패딩) 라인 배수로 키운다. `count_straddlers` 로 확인.
</details>

**연습 2 — 리눅스식 매크로와 "크기만 패딩".** `____cacheline_aligned_in_smp` 를 128 로 정의해 `struct percpu_stats { uint64_t pkts, bytes, drops; }` 에 붙이면 sizeof/alignof 는? 같은 내용을 `union { struct {...} v; char pad[128]; }` 로 만들면? 그 union 배열이 0x1008 에서 시작하면 원소 0 과 1 은 블록을 공유하나?

<details><summary>답</summary>

GNU `__attribute__((__aligned__(128)))` 를 타입에 붙이면 **sizeof 128, alignof 128** — 배열 원소가 라인마다 하나. union 은 **sizeof 128, alignof 8**: 크기만 맞았고 시작 주소는 8 의 배수일 뿐이다. 0x1008 에서 시작하면 원소 0 = 0x1008–0x1087 이 블록 0x1000 과 0x1080 두 개에 걸치고, 원소 1 = 0x1088– 도 블록 0x1080 을 쓴다 → **공유한다** (`share_granule(0x1008,128, 0x1088,128, 128) == true`). 크기 패딩만으로는 반쪽짜리 해결이다.
</details>

**연습 3 — 핫/콜드 분리.** 96 B 짜리 `conn_fat_t`(핫 16 B: seq/ack/wnd/flags + 콜드 80 B) 256 개를 훑으며 핫 필드만 읽으면 128 B 라인 몇 개를 건드리나? 핫 필드만 모은 16 B `conn_hot_t` 배열이면?

<details><summary>답</summary>

뚱뚱한 배열: 원소 i 의 핫 필드는 `96i … 96i+15`. 96i mod 128 은 0, 96, 64, 32 를 돌며 16 B 가 라인을 넘지 않고, 블록 번호 `floor(0.75 i)` 는 0…191 을 하나도 건너뛰지 않는다 → **192 라인**(배열 전체 24 KB 를 다 끌어온다). 핫 배열: 256 × 16 B = 4 KB = **32 라인**. 6배 적다. 이건 false sharing 이 아니라 **지역성** 문제지만 같은 "라인 단위로 생각하기" 다. 핫 배열이 코어별로 나뉘면 그때 코어 경계에만 패딩. `lines_for_scan` 으로 확인.
</details>

**연습 4 — Cortex-M7 (32 B 라인) DMA 버퍼.** `struct { uint8_t rx_dma[40]; volatile uint32_t isr_count; uint32_t baud; }` 를 g = 32, DMA = 쓰기 그룹 3, ISR = 쓰기 그룹 1, baud = RO 로 검사하면 위반 몇 개? 고친 배치는?

<details><summary>답</summary>

rx_dma 는 0–39 → 블록 0, 1. isr_count 40–43, baud 44–47 → 블록 1. 위반 쌍: dma–isr(다른 쓰기 그룹), dma–baud(쓰기 × RO), isr–baud(쓰기 × RO) → **3개**. 고침: `_Alignas(32) uint8_t rx_dma[64]`(라인 정렬 + 크기를 32 의 배수 64 로), `_Alignas(32) volatile uint32_t isr_count`(오프셋 64), `_Alignas(32) uint32_t baud_ro` → **0개**. 실제 펌웨어에서는 DMA 버퍼를 구조체 밖 전용 섹션(non-cacheable 또는 라인 정렬)에 두는 경우가 많다. 캐시 유지보수(clean/invalidate)와 부분 라인 문제는 ARCH-M12.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o /tmp/ARCH-M08_work/pr && /tmp/ARCH-M08_work/pr
```

```text
-- 연습 1 --
PASS  count_straddlers(24, 16, 64) == 4
PASS  count_straddlers(24, 16, 128) == 2
PASS  count_straddlers(32, 64, 128) == 0
PASS  count_straddlers(48, 8, 128) == 2
-- 연습 2 --
PASS  sizeof(struct percpu_stats) == 128 && alignof(struct percpu_stats) == 128
PASS  ok
PASS  sizeof(union padded_stats) == 128 && alignof(union padded_stats) == 8
PASS  share_granule(0x1008, 128, 0x1008 + 128, 128, 128) == true
PASS  granules_touched(0x1008, 128, 128) == 2
-- 연습 3 --
PASS  sizeof(conn_fat_t) == 96 && sizeof(conn_hot_t) == 16
      256 connections, scan seq/ack/wnd/flags: fat 192 lines, hot 32 lines
PASS  fat == 192 && hot == 32
-- 연습 4 --
PASS  layout_violations(bad, 3, M7_LINE) == 3
PASS  layout_violations(good, 3, M7_LINE) == 0
PASS  offsetof(uart_good_t, rx_dma) % M7_LINE == 0 && sizeof(((uart_good_t *)0)->rx_dma) % M7_LINE == 0
PASS  offsetof(uart_good_t, isr_count) == 64 && alignof(uart_good_t) == M7_LINE
15/15 passed
```

관찰용 파일 (CHECK 없음):

<!-- code: demo_asm.c -->

<!-- code: demo_bench.c -->

## 10. 핵심 정리

- 캐시는 **라인 단위** 로 쓰기 소유권을 옮긴다. 다른 코어가 쓰는 변수가 같은 라인이면 서로 다른 변수여도 라인이 핑퐁한다 = false sharing (정확성은 멀쩡, TSan 도 조용).
- 이 맥(M6) 실측: relaxed `ldadd` 2 스레드 같은 라인 11 ns vs 떨어지면 1.4 ns (**8배**), 12 스레드면 71 ns. 같은 무리끼리 경계 **64 B**, E↔S/P 경계 **128 B** → 패딩 **128 B** (`hw.cachelinesize`).
- 측정은 실제 명령으로: 평범한 `++` 루프는 사라지고(`-S` 확인), 저장만 하는 루프는 store buffer 덕에 2배로 과소평가된다.
- 구조체 규칙: 쓰기 주체마다 라인, RO 는 쓰기 라인과 분리, 코어별 배열은 원소마다 라인, 핫/콜드 분리. `_Alignas` 는 첫 멤버에, `_Static_assert(offsetof % LINE == 0)` 로 못 박기.
- 정렬 함정: typedef 에 `_Alignas` 불가, union 패딩은 정렬을 안 준다, macOS `malloc` 은 16 B, `aligned_alloc` 은 크기가 정렬의 배수여야.
- 타깃마다 LINE 이 다르다: M6 128, Cortex-A 보통 64, Cortex-M7 32(단일 코어 → 문제는 DMA 와의 라인 공유, ARCH-M12). 리눅스 `____cacheline_aligned_in_smp`, C++ `hardware_destructive_interference_size`(이 clang 256).

## 참고자료

- Arm, *Cortex-M7 Processor Technical Reference Manual* (DDI 0489) — L1 cache: D-cache 라인 32 B (문서 기준).
- Arm, *Arm Architecture Reference Manual for A-profile* (DDI 0487) — LSE `LDADD` / `STADD` (C6 A64 instruction set), Cache Writeback Granule (CTR_EL0.CWG — 이 맥 EL0 에서 못 읽음, ARM-M04).
- ISO/IEC 9899:2011 — 6.7.5 Alignment specifier, 6.5.3.4 sizeof/_Alignof, 7.22.3.1 aligned_alloc, 5.1.2.3 volatile 과 관찰 가능한 동작, 5.1.2.4 data race.
- Linux kernel `include/linux/cache.h` (`____cacheline_aligned_in_smp`), `arch/arm64/include/asm/cache.h` (`L1_CACHE_SHIFT`) — 버전별 값 확인 필요.
- ISO C++17 [hardware.interference] `std::hardware_destructive_interference_size`; clang `__GCC_DESTRUCTIVE_SIZE` (이 맥 256).
- Ulrich Drepper, *What Every Programmer Should Know About Memory* (2007) §6.4.1 — false sharing 과 캐시 라인 배치.
- Paul E. McKenney, *Is Parallel Programming Hard, And, If So, What Can You Do About It?* — per-CPU 변수, 캐시 라인 배치.
- Linux `perf-c2c(1)` 매뉴얼 — HITM 기반 cache-to-cache 공유 분석, false sharing 탐지 (LPERF-A03).
- 관련 레슨: LLAT-M05 (P 64 B / E 128 B 첫 관찰, 코어 번호 매핑), LLAT-B06 (원자 연산 비용), ARM-M04 (LSE vs LL/SC), OS-M07 (스핀락 라인 핑퐁), OS-B05 (스레드별 칸 + 합산), DS-A01 · LLAT-M01 (SPSC head/tail 분리, 캐시된 인덱스), MEM-M04 (코어 수 vs 일관성 트래픽), ARCH-A04 (MESI 시뮬레이터), ARCH-M12 (DMA clean/invalidate), LPERF-A03 (perf c2c), C-A03 (stdatomic).
