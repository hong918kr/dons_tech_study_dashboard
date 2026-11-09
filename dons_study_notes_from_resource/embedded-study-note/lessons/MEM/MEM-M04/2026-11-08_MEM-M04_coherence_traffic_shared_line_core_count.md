---
id: MEM-M04
time: 90분
updated: 2026-11-08
---
# MEM-M04 일관성 트래픽 실측: 공유 캐시라인 경합과 코어 수

## 학습 목표

- 세 가지 공유 패턴 — (a) 공유 카운터 하나에 `fetch_add`, (b) 스레드별 카운터(128 B 라인마다) + 합산, (c) 읽기 전용 공유 데이터 — 을 1~12 스레드로 재서 **확장성 곡선**을 그리고, 각 곡선 모양을 MESI 상태(M 핑퐁 / 혼자 M / 모두 S)로 설명할 수 있다.
- 시간 없이 **개수만 세는 MESI 트랜잭션 카운터**를 직접 짜서, 고정 스케줄에서 BusRd / BusRdX / BusUpgr / 무효화 / cache-to-cache 횟수를 손으로 유도한 공식과 맞춰 볼 수 있다 ("패턴 a 는 k ≥ 2 면 연산당 소유권 이동 1번").
- 모델이 예측한 "연산당 일관성 메시지" 비율과 실측한 느려짐 비율을 나란히 놓고, **어디서 맞고 어디서 안 맞는지**(버스트 소유, P/E 코어, 디렉터리 구조, far atomic 가능성) 말할 수 있다.
- CAS 루프가 `ldadd` 보다 경합에서 훨씬 나쁜 이유를 "읽기 1번 + 시도마다 독점 1번" 으로 계산하고, read-mostly 데이터에서 갱신 한 번이 reader 수만큼 무효화를 만든다는 것을 공식으로 말할 수 있다.
- 펌웨어/실리콘 검증으로 옮기기: 멀티코어 Cortex-A SoC 의 코어별 통계, 공유 설정 테이블, 그리고 "옛 값이 0..N−1 을 정확히 한 번씩" 이라는 coherence 스트레스 테스트의 판정 기준.

## 1. 문제

**문제 소개.** 코어 12개짜리 칩에서 카운터 하나를 모든 스레드가 `atomic_fetch_add` 하면, 스레드를 늘릴수록 빨라질까? 이 맥(Apple M6)에서 재 보면 1 스레드 720 Mops/s 가 2 스레드에서 **171 Mops/s 로 4배 떨어지고**, 12 스레드까지 그대로 평평하다. 반면 스레드마다 자기 라인에 따로 세면 12 스레드에서 9.3배, 모두가 같은 테이블을 읽기만 하면 10.3배로 늘어난다. 같은 "공유 메모리" 인데 왜 이렇게 다른가? 이 레슨은 그 차이를 **실측 곡선** 으로 보고, 캐시 일관성 프로토콜(cache coherence protocol)의 **메시지 개수** 로 설명한다 — 멀티코어 펌웨어·실리콘 검증 면접에서 "왜 배리어/패딩이 필요하죠?" 의 하드웨어 쪽 답이다.

왜 중요한가: 실리콘 검증·AI 칩·멀티코어 펌웨어 면접에서 "이 카운터를 코어 8개가 같이 쓰면 어떻게 되죠?" 는 MESI 상태 전이로 답해야 하는 질문이고, 리눅스 커널의 per-CPU 카운터, 멀티코어 Cortex-A SoC(i.MX8M 의 A53 ×4, 네트워크 SoC 의 패킷 엔진)의 통계 설계가 모두 이 숫자에서 나온다.

이미 아는 것(다시 가르치지 않는다): `memory_order` 와 `ldadd` / `ldapr` 같은 명령(C-A03), 뮤텍스·atomic·sloppy 카운터 1~8 스레드 처리량(OS-B05), 원자 연산 비용과 CAS 재시도(LLAT-B06), LL/SC vs LSE(ARM-M04), 스핀락과 일관성 트래픽(OS-M07), 코어 간 왕복 지연과 128 B 경계(LLAT-M05). 여기서 새로 하는 것은 **패턴 × 코어 수 곡선** 과 **그 곡선을 세는 모델** 이다. false sharing stride 측정은 ARCH-M08, 완전한 MESI 시뮬레이터(불변식·돌연변이 테스트)는 ARCH-A04 가 맡는다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="22" fill="currentColor" font-weight="bold">세 패턴, 라인 하나의 MESI 상태 (코어 4개 예)</text>
<rect x="10" y="36" width="234" height="214" rx="6" class="f-mut" stroke="currentColor"/>
<text x="22" y="58" fill="currentColor" font-weight="bold">(a) 공유 카운터 fetch_add</text>
<rect x="30" y="72" width="44" height="30" rx="4" class="f-warn" stroke="currentColor"/><text x="44" y="92" fill="currentColor">M</text>
<rect x="80" y="72" width="44" height="30" rx="4" fill="none" stroke="currentColor"/><text x="97" y="92" fill="currentColor">I</text>
<rect x="130" y="72" width="44" height="30" rx="4" fill="none" stroke="currentColor"/><text x="147" y="92" fill="currentColor">I</text>
<rect x="180" y="72" width="44" height="30" rx="4" fill="none" stroke="currentColor"/><text x="197" y="92" fill="currentColor">I</text>
<path d="M52 108 C 52 130, 102 130, 102 108" stroke="currentColor" fill="none"/><path d="M102 108 C 102 130, 152 130, 152 108" stroke="currentColor" fill="none"/>
<text x="22" y="150" fill="currentColor">쓰려면 M (독점)이 필요 →</text>
<text x="22" y="168" fill="currentColor">연산마다 BusRdX + 라인 이동</text>
<text x="22" y="186" fill="currentColor">+ 이전 주인 무효화</text>
<text x="22" y="214" fill="currentColor" font-weight="bold">총 처리량 = 평평</text>
<text x="22" y="236" fill="currentColor" font-size="12">실측 k=2..12: 161~185 Mops/s</text>
<rect x="253" y="36" width="234" height="214" rx="6" class="f-mut" stroke="currentColor"/>
<text x="265" y="58" fill="currentColor" font-weight="bold">(b) 스레드별 라인</text>
<rect x="273" y="72" width="44" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="285" y="92" fill="currentColor">M₀</text>
<rect x="323" y="72" width="44" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="335" y="92" fill="currentColor">M₁</text>
<rect x="373" y="72" width="44" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="385" y="92" fill="currentColor">M₂</text>
<rect x="423" y="72" width="44" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="435" y="92" fill="currentColor">M₃</text>
<text x="265" y="150" fill="currentColor">라인 4개, 각자 자기 것만 M</text>
<text x="265" y="168" fill="currentColor">처음 한 번(콜드 미스) 뒤로는</text>
<text x="265" y="186" fill="currentColor">버스 트랜잭션 0</text>
<text x="265" y="214" fill="currentColor" font-weight="bold">총 처리량 ∝ 코어 수</text>
<text x="265" y="236" fill="currentColor" font-size="12">실측 k=12: 1 스레드의 9.3배</text>
<rect x="496" y="36" width="234" height="214" rx="6" class="f-mut" stroke="currentColor"/>
<text x="508" y="58" fill="currentColor" font-weight="bold">(c) 읽기 전용 공유</text>
<rect x="516" y="72" width="44" height="30" rx="4" class="f-acc" stroke="currentColor"/><text x="531" y="92" fill="currentColor">S</text>
<rect x="566" y="72" width="44" height="30" rx="4" class="f-acc" stroke="currentColor"/><text x="581" y="92" fill="currentColor">S</text>
<rect x="616" y="72" width="44" height="30" rx="4" class="f-acc" stroke="currentColor"/><text x="631" y="92" fill="currentColor">S</text>
<rect x="666" y="72" width="44" height="30" rx="4" class="f-acc" stroke="currentColor"/><text x="681" y="92" fill="currentColor">S</text>
<text x="508" y="150" fill="currentColor">같은 라인을 모두 S 로 들고</text>
<text x="508" y="168" fill="currentColor">각자 L1 에서 읽음. 아무도</text>
<text x="508" y="186" fill="currentColor">쓰지 않으니 무효화 0</text>
<text x="508" y="214" fill="currentColor" font-weight="bold">총 처리량 ∝ 코어 수</text>
<text x="508" y="236" fill="currentColor" font-size="12">실측 k=12: 10.3배</text>
<rect x="10" y="262" width="720" height="58" rx="6" fill="none" stroke="currentColor"/>
<text x="22" y="284" fill="currentColor">핵심: "공유" 가 비싼 게 아니라 <tspan font-weight="bold">공유된 라인에 쓰기</tspan> 가 비싸다. 쓰기는 다른 사본을 모두 지우고(무효화)</text>
<text x="22" y="304" fill="currentColor">라인을 가져와야(RFO, read-for-ownership) 한다. 읽기만 하는 공유는 S 상태로 복제되어 공짜에 가깝다.</text>
</svg>
```

체크 테스트(`starter.c` → `solution.c`)는 **정확성만** 본다: 합계, 옛 값 집합, 모델이 센 개수, reader 가 본 값의 단조성. 처리량 숫자는 `demo_bench.c` 의 관찰 결과이고 CHECK 하지 않는다.

### Part A — 세 패턴을 진짜 스레드로 (합계가 정확해야 한다)

- `work_shared_add(olds, n)` — (a) 공유 카운터 `g_shared` 에 `fetch_add(1, relaxed)` n 번, 돌려받은 옛 값을 `olds[i]` 에 기록. 테스트: k 스레드 × n 뒤 합계 == k·n, 그리고 **모든 스레드의 옛 값을 모으면 0..k·n−1 이 정확히 한 번씩** (RMW 원자성 = 실리콘 검증의 coherence 판정 기준).
- `work_shared_cas(n)` — (a') 같은 카운터를 `ldr` → `compare_exchange_weak` 루프로. 반환 = CAS 실패 횟수 (CHECK 하지 않음, 데모가 센다).
- `slot_t` 를 **칸 하나 = 라인 하나** 로 고치고(`_Alignas(128)`), `work_private_add(t, n)` / `sum_slots(k)` — (b) 자기 칸에만 `fetch_add`, 끝나고 합.
- `work_readonly(n)` — (c) 라인 4개짜리 테이블 `g_ro[i % 64]` 를 relaxed load 로 읽은 합. 스레드마다 같은 합이어야 한다.

### Part B — MESI 트랜잭션 카운터 (시간 없이 개수만)

- `mesi_read(s, c, l)` / `mesi_write(s, c, l)` — 코어 c 가 라인 l 을 읽기/쓰기. 상태 M/E/S/I 를 바꾸고 `rd, rdx, upgr, inval, c2c, mem, wb, hit` 를 센다 (규칙표는 `solution.c` 주석과 §5).
- `mesi_op(s, pat, t, r, every)` — 패턴 a / a' / b / c / d 를 모델 접근으로 옮긴다. 하네스가 round-robin(라운드마다 스레드 0..k−1 순서)으로 돌리며 매 접근 뒤 SWMR 불변식을 검사한다.
- 테스트: k = 1..12 에서 개수가 손으로 유도한 공식과 같아야 한다 — 예: 패턴 a, k ≥ 2, 총 N 연산이면 `rdx == N, c2c == N−1, inval == N−1`.

### Part C — 펌웨어 시나리오: 여러 코어가 읽는 설정 워드

멀티코어 SoC 에서 패킷 처리 코어들이 매 패킷 읽는 설정 워드(라우팅 테이블 버전, 보정 테이블 세대 번호)를 제어 코어 하나가 가끔 갱신한다.

- `cfg_writer(U, gap)` — `g_cfg` 에 1, 2, …, U 를 relaxed store. `cfg_reader(st)` — 하네스가 `g_cfg_done` 을 켤 때까지 relaxed load 를 반복하며 읽은 횟수·뒤로 간 횟수·최대값·마지막 값을 기록.
- 실제 칩으로 옮기면: i.MX8M 같은 Cortex-A53 ×4 클러스터는 하드웨어 일관성(SCU/인터커넥트)이 있어서 이 레슨의 패턴이 그대로 나타난다. 단일 코어 Cortex-M 은 코어 간 일관성 문제가 없고, STM32H7 듀얼 코어(M7 에 L1 D-cache, M4 는 캐시 없음)처럼 하드웨어 일관성이 없는 조합은 소프트웨어 캐시 유지보수가 필요하다 (ARCH-M12, 확인 필요: 칩별 레퍼런스 매뉴얼).
- 테스트 (진짜 스레드, 결정적): 모든 reader 의 `backwards == 0` (같은 주소의 coherence 보장), `maxv ≤ U`, done 뒤 마지막 읽기 == U.
- 테스트 (모델): writer 1 + reader R = k−1, n 라운드, every 라운드마다 갱신(U = ⌈n/every⌉) → `upgr = U−1, inval = R(U−1), rd = R·U, c2c = U`. 갱신 한 번 = 무효화 R 개 + 재읽기 R 번.

## 2. Problem (English)

You are sizing per-core statistics and shared configuration for a 12-core SoC and must justify the design with both measurements and a coherence model.

**Part A.** Implement the per-thread work functions for four sharing patterns, run by a harness with k = 1, 2, 4, 12 threads that start together:
`void work_shared_add(uint32_t *olds, uint64_t n)` (relaxed `fetch_add` on one shared counter, record each returned old value), `uint64_t work_shared_cas(uint64_t n)` (same increment with a load + `compare_exchange_weak` loop, return the number of failed CAS), `void work_private_add(int t, uint64_t n)` + `uint64_t sum_slots(int k)` (each thread increments its own slot; fix `slot_t` so that one slot occupies exactly one 128-byte line), and `uint64_t work_readonly(uint64_t n)` (sum of `g_ro[i % 64]` via relaxed loads). Totals must be exact, and the union of all returned old values must be exactly {0, …, k·n−1}.

**Part B.** Implement a timing-free MESI transaction counter: `void mesi_read(mesi_t *s, int c, int l)`, `void mesi_write(mesi_t *s, int c, int l)` (store or atomic RMW; both need M), counting BusRd / BusRdX / BusUpgr transactions, invalidated copies, cache-to-cache transfers from an M owner, memory fills and write-backs. Map each pattern onto model accesses in `void mesi_op(...)`. Under a round-robin schedule the counts must equal closed-form formulas, e.g. pattern a with k ≥ 2 and N total ops gives `rdx = N, c2c = N − 1, inval = N − 1`; pattern b gives exactly k cold misses regardless of n.

**Part C.** One writer publishes versions 1..U of a config word with relaxed stores; k − 1 readers poll it with relaxed loads until told to stop. Every reader must observe a non-decreasing sequence and finally U. In the model, derive the cost of one update with R readers.

Constraints: C11 + pthreads, no timing in tests, deterministic checks only, starter must fail fast. Example: k = 8, n = 1000 → pattern a: 1.000 transactions/op, a′ (load + CAS, no failures): 1.999, b: 0.001. Follow-up: measure Mops/s for k = 1..12 and explain where the model's ratios do and do not match the hardware.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, exit 1, 0.1초 안에 끝남 — TODO 스텁은 아무것도 하지 않거나 0 을 돌려준다):

```text
-- Part A: real threads, exact totals (OPS=200000 per thread)
FAIL starter.c:258  _Alignof(slot_t) == LINE && sizeof(slot_t) % LINE == 0
FAIL starter.c:259  (uintptr_t)&g_slot[1] / LINE != (uintptr_t)&g_slot[0] / LINE
FAIL starter.c:260  (uintptr_t)&g_slot[MAXT - 1].v % LINE == 0
FAIL starter.c:270  atomic_load(&g_shared) == total
FAIL starter.c:279  dup == 0 && bad == 0
...
-- Part B: MESI transaction counts vs hand-derived formulas
FAIL starter.c:310  s.st[0][0] == ST_E && s.m.rd == 1 && s.m.mem == 1
FAIL starter.c:312  s.st[0][0] == ST_S && s.st[1][0] == ST_S && s.m.mem == 2 && s.m.c2c == 0
...
-- Part C: read-mostly config word (real threads + model)
FAIL starter.c:373  last && maxok
FAIL starter.c:374  atomic_load(&g_cfg) == U
...
114/256 passed
```

(114개가 PASS 인 이유: 아무 상태도 바꾸지 않는 스텁은 SWMR 불변식을 깨지 않는다. 불변식만으로는 "올바른 프로토콜" 을 증명할 수 없다는 좋은 예 — 개수 공식 CHECK 가 같이 있어야 한다.)

관찰용 파일 (하네스와 별개, 숫자는 CHECK 하지 않는다):

```bash
cc -std=c11 -Wall -Wextra -O2 -pthread demo_bench.c -o bench
./bench curve 100 3        # 패턴 a/a'/b/c/d × k=1..12, 점마다 100 ms × 3회 중앙값 (약 19초)
./bench curve 100 3 bg     # 같은 실험, QoS BACKGROUND (E 코어 쪽으로)
./bench runs 100           # 패턴 a 의 핸드오프 비율 (약 1.5초)
cc -std=c11 -O2 -S solution.c -o - | awk '/^_worker:/,/cfi_endproc/'   # 작업 함수가 실제로 내는 명령
```

## 4. 힌트

<details>
<summary>힌트 1 — 옛 값 기록이 왜 "정확성 테스트" 가 되나</summary>

`fetch_add` 는 RMW 라서 항상 수정 순서(modification order)의 **바로 앞 값** 을 읽는다 (C11 7.17.3 — 문단 번호 확인 필요). 그러니 N 번의 `fetch_add(1)` 이 돌려준 옛 값은 0..N−1 의 순열이어야 한다. 중복이 하나라도 있으면 두 코어가 같은 값을 읽고 둘 다 썼다는 뜻 — lost update. 합계만 보면 놓칠 수 있는 버그도 이걸로 잡힌다. 하네스는 `g_seen[]` 바이트 배열로 센다.
</details>

<details>
<summary>힌트 2 — slot_t 를 라인 하나로</summary>

멤버에 `_Alignas(LINE)` 를 붙이면 구조체 정렬이 128 이 되고, C 는 배열 원소 크기가 정렬의 배수가 되도록 꼬리 패딩을 넣으므로 `sizeof(slot_t) == 128` 이 된다. 패딩 배열을 손으로 넣어도 되지만(`char pad[LINE - 8]`) 배열 시작 주소 정렬은 따로 보장해야 한다. 구조체 레이아웃 규칙 전체(hot/cold 분리, `_Static_assert(offsetof...)`)는 ARCH-M08.
</details>

<details>
<summary>힌트 3 — mesi_read 에서 데이터는 누가 주나</summary>

먼저 다른 코어들의 상태를 훑어서 "사본 수" 와 "M 주인이 있나" 를 구한다. M 주인이 있으면 c2c + wb (S 사본은 메모리와 같아야 하므로 M→S 때 메모리도 갱신), 없으면 mem. 다른 사본은 전부 S 가 되고(M→S, E→S), 요청자는 사본이 있었으면 S, 없었으면 E.
</details>

<details>
<summary>힌트 4 — mesi_write 의 세 갈래</summary>

M 이나 E 면 버스 없이 M (E→M 은 조용히 — E 상태가 있는 이유). S 면 BusUpgr(데이터는 이미 있으니 "지워라" 만 보냄), I 면 BusRdX(데이터 + 독점). 두 경우 모두 다른 사본을 전부 I 로 만들고 `inval += 사본 수`.
</details>

<details>
<summary>힌트 5 — 공식은 "두 번째 라운드부터" 를 먼저 세라</summary>

패턴 a 에서 라운드 안의 스레드 t 가 쓸 때, 바로 앞 스레드 t−1 이 M 을 들고 있다. 그러니 첫 연산(메모리에서 가져옴)을 빼고는 전부 "BusRdX + c2c + 무효화 1". 총 N 연산 → rdx = N, c2c = N−1, inval = N−1, mem = 1. 같은 방식으로 a' 는 "BusRd(c2c, wb) + BusUpgr(무효화 1)" 이 연산마다.
</details>

<details>
<summary>힌트 6 — cfg_reader 가 멈추는 조건</summary>

`while (!atomic_load_explicit(&g_cfg_done, memory_order_acquire))` 안에서 읽는다. 하네스가 writer 를 join 한 **뒤에** done 을 release 로 켜므로, done 을 본 reader 의 다음 읽기는 writer 의 마지막 store(U) 를 반드시 본다 (happens-before + write-read coherence). 그래서 `lastv == U` 가 매번 성립한다.
</details>

## 5. 풀이

### Part A — 세 패턴의 작업 함수와 실측 곡선

작업 함수는 짧다. 중요한 것은 **컴파일러가 측정 대상을 지우지 않게** 하는 것과, 실제로 어떤 명령이 나오는지 보는 것이다.

| 패턴 | C 코드 (루프 몸통) | 이 맥 `-O2 -S` 에서 본 명령 | 라인 상태 |
|---|---|---|---|
| (a) 공유 fetch_add | `atomic_fetch_add_explicit(&g_shared, 1, relaxed)` | `ldadd x11, x12, [x10]` + `str w12, [x9], #4` (옛 값 기록) | 하나의 라인이 코어 사이를 M 으로 이동 |
| (a') 공유 CAS | `ldr` 후 `compare_exchange_weak` 루프 | `ldr x14, [x12]` → `cas x13, x15, [x12]` → `cmp` / 실패 시 안쪽 루프 `cas x14, x15, [x12]` | 읽기(S) 후 독점(M) — 두 번 요청 |
| (b) 스레드별 | 자기 칸에 `fetch_add` relaxed | `add x9, x10, x9, lsl #7` (칸 주소 = 기준 + t·128) → `ldadd x10, x11, [x9]` | 각자 M, 이동 없음 |
| (c) 읽기 전용 | `s += atomic_load(&g_ro[i % 64], relaxed)` | `and x13, x11, #0x3f` → `ldr x13, [x12, x13, lsl #3]` | 모두 S |
| Part C reader | done 을 acquire 로 확인 | `ldapr w11, [x9]` (acquire load) | — |

(`cc -std=c11 -O2 -S solution.c` 의 `_worker` 에서 직접 본 줄들. 작업 함수들은 `worker` 안으로 인라인됐다.) 볼 점 세 가지:

1. **relaxed `fetch_add` = `ldadd` 한 명령.** Apple clang 은 기본으로 LSE 를 쓴다 (OS-M07 에서 확인). LL/SC(`ldxr/stxr`) 와의 차이는 ARM-M04 가 쟀다 — 이 레슨에선 한 줄만: LL/SC 는 경합에서 `stxr` 실패 재시도가 추가된다.
2. **`compare_exchange_weak` 이 `cas` 로 나왔다.** LSE `cas` 는 거짓 실패(spurious failure)가 없으므로 실패 횟수는 순수하게 "다른 코어가 사이에 썼다" 는 뜻이다. LL/SC 로 컴파일되는 칩이면 거짓 실패가 섞일 수 있어서, 하네스는 k = 1 에서도 실패 수를 CHECK 하지 않는다.
3. **읽기 전용인데 왜 `_Atomic` 인가.** 평범한 `const uint64_t` 였다면 컴파일러는 "64 워드 합 × 반복 횟수" 로 루프를 접거나 벡터화할 수 있다 — 그럼 메모리 접근을 재는 게 아니다. relaxed atomic load 는 `ldr` 그대로 나오고(추가 비용 0) 컴파일러가 합치지 않는다.

#### 실측 — 세 패턴 × 1~12 스레드 (`demo_bench.c curve`)

기계: `sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu` → `Apple M6 / 2 / 4 / 6` (Super 2 + Performance 4 + Efficiency 6), `cc --version` → `Apple clang version 21.0.0 (clang-2100.0.123.102)`, `hw.cachelinesize` 128. 실행 전 `uptime` load average **4.99** (다른 레슨 작성 세션 10여 개가 동시에 벤치마크 중 — 실행이 끝났을 때는 내 12 스레드까지 더해져 16.43). 점 하나 = 100 ms 동안 모든 스레드가 한 연산 합 / 경과 시간, 3회 중앙값. `[ ]` 안은 E 코어 비율(1024 연산 블록마다 `pthread_cpu_number_np` 로 본 cpu 번호가 0–5 인 비율 — 번호↔코어 매핑은 LLAT-M05 의 추정. E 코어는 느려서 블록 수가 적으므로 실제 "시간 비율" 보다 낮게 나온다).

```text
$ ./bench curve 100 3          (load average 4.99 → 16.43, QoS USER_INTERACTIVE)
 k | a  shared fetch_add | a' shared CAS loop     | b  per-thread line | c  read-only shared | d  read-mostly
 1 |    720.3 [  0] |   685.4 [ 14] f= 0.00 |    650.6 [  5] |   2859.4 [ 17] |        -
 2 |    170.9 [ 21] |    48.4 [  3] f= 0.60 |   1130.3 [ 27] |   7121.5 [  1] |   2854.7 [  6] w=15.09
 3 |    173.8 [  8] |    42.6 [ 11] f= 0.96 |   1879.9 [  8] |   8945.4 [ 17] |   5090.1 [  5] w=14.26
 4 |    185.2 [  2] |    33.6 [  0] f= 1.40 |   2531.1 [  6] |  14006.2 [  5] |   7395.9 [  1] w=15.29
 5 |    183.6 [  7] |    32.6 [  6] f= 1.35 |   3062.3 [ 14] |  15386.4 [ 16] |   8114.3 [ 20] w=13.22
 6 |    173.9 [ 16] |    31.8 [ 29] f= 1.33 |   3525.4 [ 24] |  19129.2 [ 15] |   9945.3 [ 17] w=13.82
 7 |    172.0 [ 18] |    29.6 [ 34] f= 1.47 |   3959.3 [ 31] |  21306.4 [ 18] |  11344.5 [ 21] w=13.32
 8 |    171.3 [ 31] |    29.5 [ 44] f= 1.52 |   4295.1 [ 31] |  22677.4 [ 26] |  13741.4 [ 17] w=14.02
 9 |    169.2 [ 43] |    30.7 [ 50] f= 1.46 |   3888.1 [ 39] |  21960.6 [ 28] |  13285.8 [ 26] w=12.48
10 |    160.8 [ 54] |    31.6 [ 58] f= 1.52 |   5602.7 [ 37] |  24414.1 [ 36] |  14876.3 [ 38] w=11.65
11 |    168.1 [ 44] |    29.7 [ 55] f= 1.63 |   5936.7 [ 37] |  25698.6 [ 27] |  15914.7 [ 35] w=11.27
12 |    169.2 [ 38] |    29.9 [ 55] f= 1.63 |   6052.3 [ 37] |  29527.3 [ 31] |  18419.2 [ 39] w=10.78
```

(단위 Mops/s, 모든 스레드 합. `f` = 성공한 증가 1번당 CAS 실패 수, `w` = d 의 writer 갱신 수 / µs.) 두 번째 실행(시작 load 8.31)도 같은 모양이었다: a 153–181, a' 28–38, b 12 스레드 5439 (1 스레드 720 의 7.6배), c 26035. **절대값은 실행마다 10–40% 흔들린다 — 배수만 믿는다.**

```svg
<svg viewBox="0 0 740 350" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">총 처리량 (Mops/s, 로그 눈금) vs 스레드 수 — Apple M6, QoS USER_INTERACTIVE, load 4.99</text><rect x="60.0" y="40" width="233.6" height="260" class="f-mut" opacity="0.35"/><text x="70.0" y="54" fill="currentColor">k ≤ 6: Super 2 + Performance 4 에 들어갈 수 있음</text><text x="326.4" y="54" fill="currentColor">k ≥ 7: E 코어가 반드시 낌</text><path d="M70 300.0 L540 300.0" stroke="currentColor" stroke-opacity="0.25" fill="none"/><text x="64" y="304.0" fill="currentColor" text-anchor="end">10</text><path d="M70 235.0 L540 235.0" stroke="currentColor" stroke-opacity="0.25" fill="none"/>
<text x="64" y="239.0" fill="currentColor" text-anchor="end">100</text><path d="M70 170.0 L540 170.0" stroke="currentColor" stroke-opacity="0.25" fill="none"/><text x="64" y="174.0" fill="currentColor" text-anchor="end">1 000</text><path d="M70 105.0 L540 105.0" stroke="currentColor" stroke-opacity="0.25" fill="none"/><text x="64" y="109.0" fill="currentColor" text-anchor="end">10 000</text><path d="M70 40.0 L540 40.0" stroke="currentColor" stroke-opacity="0.25" fill="none"/><text x="64" y="44.0" fill="currentColor" text-anchor="end">100 000</text>
<path d="M70 40 L70 300 L540 300" stroke="currentColor" fill="none"/><text x="70.0" y="316" fill="currentColor" text-anchor="middle">1</text><text x="112.7" y="316" fill="currentColor" text-anchor="middle">2</text><text x="155.5" y="316" fill="currentColor" text-anchor="middle">3</text><text x="198.2" y="316" fill="currentColor" text-anchor="middle">4</text><text x="240.9" y="316" fill="currentColor" text-anchor="middle">5</text><text x="283.6" y="316" fill="currentColor" text-anchor="middle">6</text>
<text x="326.4" y="316" fill="currentColor" text-anchor="middle">7</text><text x="369.1" y="316" fill="currentColor" text-anchor="middle">8</text><text x="411.8" y="316" fill="currentColor" text-anchor="middle">9</text><text x="454.5" y="316" fill="currentColor" text-anchor="middle">10</text><text x="497.3" y="316" fill="currentColor" text-anchor="middle">11</text><text x="540.0" y="316" fill="currentColor" text-anchor="middle">12</text><text x="305" y="334" fill="currentColor" text-anchor="middle">스레드 수 k</text>
<path d="M70.0 179.3 L112.7 219.9 L155.5 219.4 L198.2 217.6 L240.9 217.8 L283.6 219.4 L326.4 219.7 L369.1 219.8 L411.8 220.2 L454.5 221.6 L497.3 220.3 L540.0 220.2" stroke="currentColor" fill="none" stroke-width="1.8"/><circle cx="70.0" cy="179.3" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="112.7" cy="219.9" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="155.5" cy="219.4" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="198.2" cy="217.6" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="240.9" cy="217.8" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="283.6" cy="219.4" r="3.5" class="f-warn" stroke="currentColor"/>
<circle cx="326.4" cy="219.7" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="369.1" cy="219.8" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="411.8" cy="220.2" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="454.5" cy="221.6" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="497.3" cy="220.3" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="540.0" cy="220.2" r="3.5" class="f-warn" stroke="currentColor"/><path d="M555 70 L590 70" stroke="currentColor" stroke-width="1.8"/><circle cx="572.0" cy="70.0" r="3.5" class="f-warn" stroke="currentColor"/>
<text x="596" y="74" fill="currentColor">a 공유 fetch_add (ldadd)</text><path d="M70.0 180.7 L112.7 255.5 L155.5 259.1 L198.2 265.8 L240.9 266.6 L283.6 267.3 L326.4 269.4 L369.1 269.5 L411.8 268.3 L454.5 267.5 L497.3 269.3 L540.0 269.1" stroke="currentColor" fill="none" stroke-width="1.8" stroke-dasharray="6 4"/><rect x="66.5" y="177.2" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="109.2" y="252.0" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="152.0" y="255.6" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="194.7" y="262.3" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="237.4" y="263.1" width="7" height="7" class="f-warn" stroke="currentColor"/>
<rect x="280.1" y="263.8" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="322.9" y="265.9" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="365.6" y="266.0" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="408.3" y="264.8" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="451.0" y="264.0" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="493.8" y="265.8" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="536.5" y="265.6" width="7" height="7" class="f-warn" stroke="currentColor"/>
<path d="M555 94 L590 94" stroke="currentColor" stroke-width="1.8" stroke-dasharray="6 4"/><rect x="568.5" y="90.5" width="7" height="7" class="f-warn" stroke="currentColor"/><text x="596" y="98" fill="currentColor">a' 공유 CAS 루프</text><path d="M70.0 182.1 L112.7 166.5 L155.5 152.2 L198.2 143.8 L240.9 138.4 L283.6 134.4 L326.4 131.2 L369.1 128.9 L411.8 131.7 L454.5 121.4 L497.3 119.7 L540.0 119.2" stroke="currentColor" fill="none" stroke-width="1.8"/><path d="M70.0 177.6 L74.5 185.6 L65.5 185.6 Z" class="f-ok" stroke="currentColor"/><path d="M112.7 162.0 L117.2 170.0 L108.2 170.0 Z" class="f-ok" stroke="currentColor"/><path d="M155.5 147.7 L160.0 155.7 L151.0 155.7 Z" class="f-ok" stroke="currentColor"/><path d="M198.2 139.3 L202.7 147.3 L193.7 147.3 Z" class="f-ok" stroke="currentColor"/>
<path d="M240.9 133.9 L245.4 141.9 L236.4 141.9 Z" class="f-ok" stroke="currentColor"/><path d="M283.6 129.9 L288.1 137.9 L279.1 137.9 Z" class="f-ok" stroke="currentColor"/><path d="M326.4 126.7 L330.9 134.7 L321.9 134.7 Z" class="f-ok" stroke="currentColor"/><path d="M369.1 124.4 L373.6 132.4 L364.6 132.4 Z" class="f-ok" stroke="currentColor"/><path d="M411.8 127.2 L416.3 135.2 L407.3 135.2 Z" class="f-ok" stroke="currentColor"/><path d="M454.5 116.9 L459.0 124.9 L450.0 124.9 Z" class="f-ok" stroke="currentColor"/><path d="M497.3 115.2 L501.8 123.2 L492.8 123.2 Z" class="f-ok" stroke="currentColor"/>
<path d="M540.0 114.7 L544.5 122.7 L535.5 122.7 Z" class="f-ok" stroke="currentColor"/><path d="M555 118 L590 118" stroke="currentColor" stroke-width="1.8"/><path d="M572.0 113.5 L576.5 121.5 L567.5 121.5 Z" class="f-ok" stroke="currentColor"/><text x="596" y="122" fill="currentColor">b 스레드별 라인</text><path d="M70.0 140.3 L112.7 114.6 L155.5 108.1 L198.2 95.5 L240.9 92.8 L283.6 86.7 L326.4 83.6 L369.1 81.9 L411.8 82.8 L454.5 79.8 L497.3 78.4 L540.0 74.4" stroke="currentColor" fill="none" stroke-width="1.8" stroke-dasharray="2 3"/><circle cx="70.0" cy="140.3" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="112.7" cy="114.6" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="155.5" cy="108.1" r="3.5" class="f-ok" stroke="currentColor"/>
<circle cx="198.2" cy="95.5" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="240.9" cy="92.8" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="283.6" cy="86.7" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="326.4" cy="83.6" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="369.1" cy="81.9" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="411.8" cy="82.8" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="454.5" cy="79.8" r="3.5" class="f-ok" stroke="currentColor"/>
<circle cx="497.3" cy="78.4" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="540.0" cy="74.4" r="3.5" class="f-ok" stroke="currentColor"/><path d="M555 142 L590 142" stroke="currentColor" stroke-width="1.8" stroke-dasharray="2 3"/><circle cx="572.0" cy="142.0" r="3.5" class="f-ok" stroke="currentColor"/><text x="596" y="146" fill="currentColor">c 읽기 전용 공유</text><path d="M112.7 140.4 L155.5 124.1 L198.2 113.5 L240.9 110.9 L283.6 105.2 L326.4 101.4 L369.1 96.0 L411.8 97.0 L454.5 93.8 L497.3 91.9 L540.0 87.8" stroke="currentColor" fill="none" stroke-width="1.8" stroke-dasharray="10 3 2 3"/><rect x="109.2" y="136.9" width="7" height="7" class="f-acc" stroke="currentColor"/><rect x="152.0" y="120.6" width="7" height="7" class="f-acc" stroke="currentColor"/>
<rect x="194.7" y="110.0" width="7" height="7" class="f-acc" stroke="currentColor"/><rect x="237.4" y="107.4" width="7" height="7" class="f-acc" stroke="currentColor"/><rect x="280.1" y="101.7" width="7" height="7" class="f-acc" stroke="currentColor"/><rect x="322.9" y="97.9" width="7" height="7" class="f-acc" stroke="currentColor"/><rect x="365.6" y="92.5" width="7" height="7" class="f-acc" stroke="currentColor"/><rect x="408.3" y="93.5" width="7" height="7" class="f-acc" stroke="currentColor"/><rect x="451.0" y="90.3" width="7" height="7" class="f-acc" stroke="currentColor"/>
<rect x="493.8" y="88.4" width="7" height="7" class="f-acc" stroke="currentColor"/><rect x="536.5" y="84.3" width="7" height="7" class="f-acc" stroke="currentColor"/><path d="M555 166 L590 166" stroke="currentColor" stroke-width="1.8" stroke-dasharray="10 3 2 3"/><rect x="568.5" y="162.5" width="7" height="7" class="f-acc" stroke="currentColor"/><text x="596" y="170" fill="currentColor">d read-mostly (reader)</text><text x="555" y="210" fill="currentColor">3회 × 100 ms 중앙값.</text><text x="555" y="228" fill="currentColor">a·a' 는 k=2 부터 평평:</text><text x="555" y="246" fill="currentColor">라인 하나를 줄 서서 씀.</text>
<text x="555" y="264" fill="currentColor">b·c·d 는 k 에 따라 증가.</text><text x="555" y="282" fill="currentColor">절대값 말고 배수만 믿기.</text>
</svg>
```

```svg
<svg viewBox="0 0 740 350" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">확장성 S(k) = 처리량(k) / 처리량(1) — 같은 측정 (선형 눈금)</text><rect x="60.0" y="40" width="233.6" height="260" class="f-mut" opacity="0.35"/><path d="M70 300.0 L540 300.0" stroke="currentColor" stroke-opacity="0.25" fill="none"/><text x="64" y="304.0" fill="currentColor" text-anchor="end">0</text><path d="M70 256.7 L540 256.7" stroke="currentColor" stroke-opacity="0.25" fill="none"/><text x="64" y="260.7" fill="currentColor" text-anchor="end">2</text><path d="M70 213.3 L540 213.3" stroke="currentColor" stroke-opacity="0.25" fill="none"/>
<text x="64" y="217.3" fill="currentColor" text-anchor="end">4</text><path d="M70 170.0 L540 170.0" stroke="currentColor" stroke-opacity="0.25" fill="none"/><text x="64" y="174.0" fill="currentColor" text-anchor="end">6</text><path d="M70 126.7 L540 126.7" stroke="currentColor" stroke-opacity="0.25" fill="none"/><text x="64" y="130.7" fill="currentColor" text-anchor="end">8</text><path d="M70 83.3 L540 83.3" stroke="currentColor" stroke-opacity="0.25" fill="none"/><text x="64" y="87.3" fill="currentColor" text-anchor="end">10</text>
<path d="M70 40.0 L540 40.0" stroke="currentColor" stroke-opacity="0.25" fill="none"/><text x="64" y="44.0" fill="currentColor" text-anchor="end">12</text><path d="M70 40 L70 300 L540 300" stroke="currentColor" fill="none"/><text x="70.0" y="316" fill="currentColor" text-anchor="middle">1</text><text x="112.7" y="316" fill="currentColor" text-anchor="middle">2</text><text x="155.5" y="316" fill="currentColor" text-anchor="middle">3</text><text x="198.2" y="316" fill="currentColor" text-anchor="middle">4</text>
<text x="240.9" y="316" fill="currentColor" text-anchor="middle">5</text><text x="283.6" y="316" fill="currentColor" text-anchor="middle">6</text><text x="326.4" y="316" fill="currentColor" text-anchor="middle">7</text><text x="369.1" y="316" fill="currentColor" text-anchor="middle">8</text><text x="411.8" y="316" fill="currentColor" text-anchor="middle">9</text><text x="454.5" y="316" fill="currentColor" text-anchor="middle">10</text><text x="497.3" y="316" fill="currentColor" text-anchor="middle">11</text>
<text x="540.0" y="316" fill="currentColor" text-anchor="middle">12</text><text x="305" y="334" fill="currentColor" text-anchor="middle">스레드 수 k</text><path d="M70.0 278.3 L540.0 40.0" stroke="currentColor" stroke-dasharray="3 3" fill="none"/><text x="411.8" y="76.8" fill="currentColor">이상적 S = k</text><path d="M70.0 278.3 L112.7 294.9 L155.5 294.8 L198.2 294.4 L240.9 294.5 L283.6 294.8 L326.4 294.8 L369.1 294.8 L411.8 294.9 L454.5 295.2 L497.3 294.9 L540.0 294.9" stroke="currentColor" fill="none" stroke-width="1.8"/><circle cx="70.0" cy="278.3" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="112.7" cy="294.9" r="3.5" class="f-warn" stroke="currentColor"/>
<circle cx="155.5" cy="294.8" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="198.2" cy="294.4" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="240.9" cy="294.5" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="283.6" cy="294.8" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="326.4" cy="294.8" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="369.1" cy="294.8" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="411.8" cy="294.9" r="3.5" class="f-warn" stroke="currentColor"/>
<circle cx="454.5" cy="295.2" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="497.3" cy="294.9" r="3.5" class="f-warn" stroke="currentColor"/><circle cx="540.0" cy="294.9" r="3.5" class="f-warn" stroke="currentColor"/><path d="M555 70 L590 70" stroke="currentColor" stroke-width="1.8"/><circle cx="572.0" cy="70.0" r="3.5" class="f-warn" stroke="currentColor"/><text x="596" y="74" fill="currentColor">a 공유 fetch_add (ldadd)</text><path d="M70.0 278.3 L112.7 298.5 L155.5 298.7 L198.2 298.9 L240.9 299.0 L283.6 299.0 L326.4 299.1 L369.1 299.1 L411.8 299.0 L454.5 299.0 L497.3 299.1 L540.0 299.1" stroke="currentColor" fill="none" stroke-width="1.8" stroke-dasharray="6 4"/><rect x="66.5" y="274.8" width="7" height="7" class="f-warn" stroke="currentColor"/>
<rect x="109.2" y="295.0" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="152.0" y="295.2" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="194.7" y="295.4" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="237.4" y="295.5" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="280.1" y="295.5" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="322.9" y="295.6" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="365.6" y="295.6" width="7" height="7" class="f-warn" stroke="currentColor"/>
<rect x="408.3" y="295.5" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="451.0" y="295.5" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="493.8" y="295.6" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="536.5" y="295.6" width="7" height="7" class="f-warn" stroke="currentColor"/><path d="M555 94 L590 94" stroke="currentColor" stroke-width="1.8" stroke-dasharray="6 4"/><rect x="568.5" y="90.5" width="7" height="7" class="f-warn" stroke="currentColor"/><text x="596" y="98" fill="currentColor">a' 공유 CAS 루프</text><path d="M70.0 278.3 L112.7 262.4 L155.5 237.4 L198.2 215.7 L240.9 198.0 L283.6 182.6 L326.4 168.1 L369.1 157.0 L411.8 170.5 L454.5 113.4 L497.3 102.3 L540.0 98.4" stroke="currentColor" fill="none" stroke-width="1.8"/>
<path d="M70.0 273.8 L74.5 281.8 L65.5 281.8 Z" class="f-ok" stroke="currentColor"/><path d="M112.7 257.9 L117.2 265.9 L108.2 265.9 Z" class="f-ok" stroke="currentColor"/><path d="M155.5 232.9 L160.0 240.9 L151.0 240.9 Z" class="f-ok" stroke="currentColor"/><path d="M198.2 211.2 L202.7 219.2 L193.7 219.2 Z" class="f-ok" stroke="currentColor"/><path d="M240.9 193.5 L245.4 201.5 L236.4 201.5 Z" class="f-ok" stroke="currentColor"/><path d="M283.6 178.1 L288.1 186.1 L279.1 186.1 Z" class="f-ok" stroke="currentColor"/><path d="M326.4 163.6 L330.9 171.6 L321.9 171.6 Z" class="f-ok" stroke="currentColor"/>
<path d="M369.1 152.5 L373.6 160.5 L364.6 160.5 Z" class="f-ok" stroke="currentColor"/><path d="M411.8 166.0 L416.3 174.0 L407.3 174.0 Z" class="f-ok" stroke="currentColor"/><path d="M454.5 108.9 L459.0 116.9 L450.0 116.9 Z" class="f-ok" stroke="currentColor"/><path d="M497.3 97.8 L501.8 105.8 L492.8 105.8 Z" class="f-ok" stroke="currentColor"/><path d="M540.0 93.9 L544.5 101.9 L535.5 101.9 Z" class="f-ok" stroke="currentColor"/><path d="M555 118 L590 118" stroke="currentColor" stroke-width="1.8"/><path d="M572.0 113.5 L576.5 121.5 L567.5 121.5 Z" class="f-ok" stroke="currentColor"/><text x="596" y="122" fill="currentColor">b 스레드별 라인</text>
<path d="M70.0 278.3 L112.7 246.0 L155.5 232.2 L198.2 193.9 L240.9 183.4 L283.6 155.1 L326.4 138.6 L369.1 128.2 L411.8 133.6 L454.5 115.0 L497.3 105.3 L540.0 76.3" stroke="currentColor" fill="none" stroke-width="1.8" stroke-dasharray="2 3"/><circle cx="70.0" cy="278.3" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="112.7" cy="246.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="155.5" cy="232.2" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="198.2" cy="193.9" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="240.9" cy="183.4" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="283.6" cy="155.1" r="3.5" class="f-ok" stroke="currentColor"/>
<circle cx="326.4" cy="138.6" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="369.1" cy="128.2" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="411.8" cy="133.6" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="454.5" cy="115.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="497.3" cy="105.3" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="540.0" cy="76.3" r="3.5" class="f-ok" stroke="currentColor"/><path d="M555 142 L590 142" stroke="currentColor" stroke-width="1.8" stroke-dasharray="2 3"/><circle cx="572.0" cy="142.0" r="3.5" class="f-ok" stroke="currentColor"/>
<text x="596" y="146" fill="currentColor">c 읽기 전용 공유</text><text x="555" y="186" fill="currentColor">k=12: c 10.3배, b 9.3배,</text><text x="555" y="204" fill="currentColor">a 0.23배, a' 0.04배.</text><text x="555" y="222" fill="currentColor">k=6: b 5.4배 (거의 선형)</text><text x="555" y="240" fill="currentColor">k≥7: E 코어가 P 의 약 1/3</text><text x="555" y="258" fill="currentColor">속도라 기울기가 꺾인다.</text>
</svg>
```

읽는 법 — 각 곡선을 일관성 프로토콜로:

1. **(a) 공유 fetch_add: 1 → 2 스레드에서 4.2배 떨어지고 그 뒤로 평평 (161–185).** 쓰려면 라인을 M 으로 가져야 하고, M 은 한 번에 한 코어만 가질 수 있다(SWMR). 그러니 총 처리량은 코어 수가 아니라 "라인이 코어 사이를 오가는 속도" 로 정해진다. k 를 늘려도 줄 서는 사람만 늘 뿐 창구는 하나다. 이 맥에서는 k = 12 까지 **떨어지지도 않았다** — 무너지는(collapse) 곡선은 LL/SC 나 CAS 처럼 "실패 후 재시도" 가 있는 쪽이다. 같은 모양을 OS-B05(636 → 162), LLAT-B06(661 → 169~200)도 1~8 스레드에서 봤다.
2. **(a') CAS 루프: 1 → 2 에서 14배, 그 뒤로 계속 줄어 k = 12 에서 1 스레드의 1/23.** 실패 수 `f` 가 0.6 → 1.6 으로 늘어난다. `ldr` 로 읽고 `cas` 하기 전에 다른 코어가 라인을 가져가면 실패하고, 실패한 `cas` 도 라인을 한 번 가져와야 한다 (Part B 연산당 메시지 계산).
3. **(b) 스레드별 라인: k = 6 까지 거의 선형 (5.4배), 12 에서 9.3배.** 라인마다 주인이 하나라 처음 한 번 BusRdX 한 뒤로는 버스 트랜잭션이 없다. 12배가 안 되는 이유는 일관성이 아니라 **코어가 같지 않아서** 다: k ≥ 7 은 E 코어가 반드시 끼는데, 같은 실험을 QoS BACKGROUND 로 돌리면(E 코어만, 아래) 1 스레드가 217 Mops/s — P 코어 650 의 1/3 이다.
4. **(c) 읽기 전용: 12 스레드에서 10.3배.** 라인 4개가 모든 L1 에 S 로 복제되고 아무도 쓰지 않으니 무효화도 없다. 1 스레드 값(2859)은 실행마다 2859 / 4208 로 흔들렸다(E 코어 17% vs 0%) — 기준점이 흔들리면 배수도 흔들린다는 것까지 같이 보자.
5. **(d) read-mostly: writer 가 약 65–95 ns 마다 갱신하는데도 reader 처리량은 k 와 함께 늘어난다** (12 스레드 18419, c 의 62%). 갱신 하나가 reader 사본을 모두 지우지만, reader 는 다음 갱신까지 수십~수백 번을 L1 hit 로 읽는다. 대신 writer 쪽이 reader 가 많아질수록 느려진다 (`w` 15.1 → 10.8 /µs): 지울 사본이 많아지니까. Part C 모델이 이 비용을 센다.

#### E 코어만 쓰면 (`./bench curve 100 3 bg`, 시작 load 6.39)

```text
 k | a  shared fetch_add | a' shared CAS loop     | b  per-thread line | c  read-only shared | d  read-mostly
 1 |    219.4 [100] |   220.2 [100] f= 0.00 |    217.3 [100] |    737.7 [100] |        -
 2 |     58.0 [100] |    20.1 [100] f= 0.40 |    323.7 [100] |   1680.5 [100] |   1456.2 [100] w=5.91
 4 |     81.0 [100] |    23.1 [100] f= 1.08 |   1309.0 [100] |   5118.8 [100] |   4348.8 [100] w=6.47
 6 |    106.2 [100] |    14.9 [100] f= 1.42 |   1630.7 [100] |   6193.5 [100] |   4712.5 [100] w=3.89
 9 |    145.6 [100] |    58.8 [100] f= 0.66 |   1713.2 [100] |   6312.4 [100] |   6479.9 [100] w=3.33
12 |     82.5 [100] |    27.3 [100] f= 1.21 |   1540.2 [100] |   6472.8 [100] |   5121.9 [100] w=1.81
```

(일부 줄만. 전체 12줄은 직접 돌려 보자.) BACKGROUND 는 LLAT-M05 결과처럼 **100% E 코어** 에 갔다. 그러면 (b)(c) 는 **k = 6 근처에서 더 늘지 않는다** (b 1631 → 1540, c 6194 → 6473): E 코어가 6개뿐이라 k > 6 스레드는 같은 코어를 나눠 쓴다. P/E 경계가 곡선에 그대로 보이는 실험이다. 숫자가 훨씬 들쭉날쭉한 것(a 가 32 ~ 146)도 정보다: E 코어는 다른 세션의 백그라운드 일과 경쟁하고, DVFS 로 클럭이 바뀐다(추정).

#### 핸드오프 비율 — 라인은 정말 연산마다 움직이나? (`./bench runs 100`, load 5.44)

`fetch_add` 가 돌려준 옛 값이 **내 직전 옛 값 + 1** 이면 그 사이에 아무도 증가하지 않았다(값 순서로 보면 내가 연달아 했다). 아니면 다른 코어가 끼어든 것 = 핸드오프 1.

```text
 k |  Mops/s | handoff/op | ops per run | E%
 1 |   708.2 |     0.0000 |  74385408.0 |   2
 2 |   190.7 |     0.2860 |         3.5 |   2
 4 |   181.6 |     0.3183 |         3.1 |   1
 6 |   168.7 |     0.2459 |         4.1 |  66
 8 |   162.3 |     0.3131 |         3.2 |  50
12 |   165.5 |     0.3106 |         3.2 |  50
```

(일부 줄. BACKGROUND 로도 k ≥ 2 에서 0.30–0.33 으로 같았다.) **연산의 약 30% 만 다른 코어 뒤에 온다 — 한 코어가 라인을 잡으면 평균 3.2번 연달아 증가한다.** round-robin 모델(연산마다 이동 1번 = 1.0)과 다르다. 이 숫자로 이동 한 번의 값을 거꾸로 계산할 수 있다 (연습 4): 연산당 5.85 ns = 로컬 1.39 ns + 0.3 × t_이동 → **t_이동 ≈ 15 ns**. "값 순서의 핸드오프" 는 라인 이동 횟수의 하한일 뿐이고(이동했다가 아무 일 없이 돌아올 수도 있다), Apple 이 실제로 무엇을 하는지는 공개되어 있지 않다 — 해석은 추정이다.

### Part B — MESI 트랜잭션 카운터 모델

모델 규칙 (Illinois 계열 MESI — Papamarcos & Patel 1984, Culler & Singh 5장, Sorin·Hill·Wood Primer 7장의 스누핑 프로토콜을 단순화):

| 요청자 상태 | read (load) | write (store / RMW) |
|---|---|---|
| M | hit | hit |
| E | hit | hit, **조용히 E→M** (버스 없음) |
| S | hit | **BusUpgr**, 다른 S 를 전부 I (`inval += 사본 수`) |
| I | **BusRd**: M 주인 있으면 c2c + wb (주인 M→S), 없으면 mem. 다른 사본은 S. 요청자: 사본 있었으면 S, 없었으면 E | **BusRdX**: M 주인 있으면 c2c, 없으면 mem. 다른 사본 전부 I. 요청자 M |

모델이 일부러 뺀 것: 캐시 용량과 eviction(측정 패턴은 라인 몇 개뿐), 시간과 버스 중재(ARCH-A04 가 사이클 단위로), O/F 상태(MOESI/MESIF 는 MEM-M05), 깨끗한 라인의 cache-to-cache 공급(Illinois 원 논문은 깨끗한 사본을 가진 캐시도 데이터를 줄 수 있게 했다 — 확인 필요. 여기선 메모리가 준다고 정했다 — 그래서 `mem` 이 크게 나온다).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">패턴 a, k = 3, round-robin: 연산마다 BusRdX 1 + c2c 1 + 무효화 1</text>
<text x="20" y="50" fill="currentColor" font-weight="bold">연산</text><text x="110" y="50" fill="currentColor" font-weight="bold">C0</text><text x="170" y="50" fill="currentColor" font-weight="bold">C1</text><text x="230" y="50" fill="currentColor" font-weight="bold">C2</text><text x="300" y="50" fill="currentColor" font-weight="bold">버스 / 데이터 출처</text>
<path d="M10 58 L730 58" stroke="currentColor"/>
<text x="20" y="80" fill="currentColor">#1 C0 쓰기</text><rect x="100" y="66" width="40" height="22" rx="3" class="f-warn" stroke="currentColor"/><text x="112" y="82" fill="currentColor">M</text><text x="175" y="82" fill="currentColor">I</text><text x="235" y="82" fill="currentColor">I</text><text x="300" y="82" fill="currentColor">BusRdX, 메모리에서 (콜드 미스, mem 1)</text>
<text x="20" y="112" fill="currentColor">#2 C1 쓰기</text><text x="115" y="114" fill="currentColor">I</text><rect x="160" y="98" width="40" height="22" rx="3" class="f-warn" stroke="currentColor"/><text x="172" y="114" fill="currentColor">M</text><text x="235" y="114" fill="currentColor">I</text><text x="300" y="114" fill="currentColor">BusRdX, C0 의 M 이 데이터 줌 (c2c), C0 무효화</text>
<text x="20" y="144" fill="currentColor">#3 C2 쓰기</text><text x="115" y="146" fill="currentColor">I</text><text x="175" y="146" fill="currentColor">I</text><rect x="220" y="130" width="40" height="22" rx="3" class="f-warn" stroke="currentColor"/><text x="232" y="146" fill="currentColor">M</text><text x="300" y="146" fill="currentColor">BusRdX, C1 → C2 (c2c), C1 무효화</text>
<text x="20" y="176" fill="currentColor">#4 C0 쓰기</text><rect x="100" y="162" width="40" height="22" rx="3" class="f-warn" stroke="currentColor"/><text x="112" y="178" fill="currentColor">M</text><text x="175" y="178" fill="currentColor">I</text><text x="235" y="178" fill="currentColor">I</text><text x="300" y="178" fill="currentColor">BusRdX, C2 → C0 (c2c), C2 무효화 … 계속</text>
<path d="M10 196 L730 196" stroke="currentColor"/>
<text x="20" y="218" fill="currentColor">N 연산 합계: rdx = N, c2c = N−1, inval = N−1, mem = 1, hit = 0 → 연산당 트랜잭션 1.000</text>
<text x="20" y="240" fill="currentColor">패턴 a' (ldr + cas): 연산마다 BusRd(c2c + wb, 주인 M→S) + BusUpgr(무효화 1) → 연산당 2 (k=8: 1.999)</text>
<text x="20" y="262" fill="currentColor">패턴 b: 콜드 미스 k 번뿐 → n 이 커지면 0 으로.   패턴 c: 코어마다 라인마다 BusRd 1 번 → 0 으로</text>
<text x="20" y="284" fill="currentColor">k = 1 이면 a 는 첫 BusRdX 뒤 전부 hit, a' 는 BusRd(E) 뒤 E→M 이 조용해서 트랜잭션 1개뿐 (E 상태의 이득)</text>
</svg>
```

손으로 유도한 공식 (round-robin, 스레드 k, 라운드 n, 총 N = k·n) — `solution.c` 가 k = 1..12 전부에서 CHECK 한다:

| 패턴 | rd | rdx | upgr | inval | c2c | mem | 연산당 트랜잭션 (n→∞) |
|---|---|---|---|---|---|---|---|
| a, k = 1 | 0 | 1 | 0 | 0 | 0 | 1 | → 0 |
| a, k ≥ 2 | 0 | N | 0 | N−1 | N−1 | 1 | **1** |
| a', k ≥ 2 | N | 0 | N−1 | N−1 | N−1 (wb 도 N−1) | 1 | **2** |
| b | 0 | k | 0 | 0 | 0 | k | → 0 |
| c (라인 L=4) | k·L | 0 | 0 | 0 | 0 | k·L | → 0 |

#### 모델 vs 측정 — 어디서 맞고 어디서 안 맞나

| 질문 | 모델 예측 | 실측 (M6, load 5) | 맞나? |
|---|---|---|---|
| a 의 k = 2..12 모양 | 연산당 1 (상수) → 총 처리량 평평 | 161–185 Mops/s, 평평 | **맞음** |
| b, c 의 모양 | 연산당 → 0 → 코어 수에 비례 | b 9.3배, c 10.3배 (k=12) | 모양은 맞음. 기울기는 E 코어 때문에 꺾임 — 모델엔 코어 속도가 없다 |
| a' / a 시간 비율 | 실패 없으면 2. 실패 f 를 넣으면 연산당 `1 (ldr) + (1+f) (cas)` = 2+f → k=2: 2.6, k=12: 3.6 | k=2: 3.5, k=4: 5.5, k=12: 5.7 | **방향은 맞고 크기는 1.4–1.6배 모자람** |
| a 의 연산당 라인 이동 | 1.0 | 값 순서 핸드오프 0.29–0.32 | **안 맞음** — 진짜 하드웨어는 라인을 잡은 코어가 평균 3.2번 연달아 쓴다 |
| k=1 → 2 의 느려짐 | 0 → 1 트랜잭션 (비율 무한대) | 4.2배 | 모델은 비율을 못 낸다 — 시간이 없으니까 |
| P↔E 이동이 더 비쌈 | 구분 없음 | LLAT-M05: P–P 왕복 74–90 ns, P↔E 200–400 ns | 모델에 거리가 없다 |

안 맞는 이유 (이 맥에 대해서는 전부 추정 — Apple 은 일관성 구조를 공개하지 않는다):

- **스누핑 버스가 아니다.** 실제 멀티코어 칩은 클러스터 L2(이 맥: `hw.perflevel*.cpusperl2` = 2 / 4 / 6, 즉 Super 2 개, Performance 4 개, E 6 개가 각자 L2 공유)와 그 위의 시스템 레벨 캐시·인터커넥트에서 **디렉터리(또는 snoop filter)** 로 누가 사본을 가졌는지 추적한다고 보는 게 일반적이다. 메시지는 "버스에 한 번" 이 아니라 요청 → 홈 → 주인 → 요청자의 여러 홉이고, 같은 클러스터 안과 밖의 비용이 다르다 (디렉터리 프로토콜은 MEM-M05, Sorin·Hill·Wood 8장).
- **버스트 소유.** 라인을 받은 코어는 파이프라인에 쌓인 `ldadd` 몇 개를 처리한 뒤에 뺏긴다 (핸드오프 0.3/op). round-robin 은 최악 스케줄이다.
- **far atomic 가능성.** Arm AMBA 5 CHI 는 원자 연산을 라인을 옮기지 않고 홈 노드(L2/시스템 캐시)에서 실행하는 "atomic transaction" 을 정의한다 (IHI 0050, Atomic transactions 절). Apple 칩이 `ldadd` 를 그렇게 처리하는지는 모른다 — ARM-M04 의 "casal 이 느린 이유 = far atomics" 도 가설로 남아 있다. 그렇게 처리한다면 a 의 "라인 이동" 이라는 말 자체가 틀린다.
- **실패한 CAS 의 비용은 구현마다 다르다.** 모델은 실패한 `cas` 도 독점(M)을 얻는다고 셌다. x86 은 문서로 정해져 있다: `LOCK CMPXCHG` 는 비교가 실패해도 목적지에 쓰기 사이클을 낸다 (Intel SDM Vol.2A CMPXCHG). Arm `CAS` 가 실패 시 라인 독점을 얻는지는 구현 정의로 보이고 확인하지 못했다.

결론: **이 모델은 "곡선의 모양" 과 "어느 설계가 메시지를 몇 배 더 만드나" 를 믿을 수 있게 알려 주고, 절대 시간과 코어 배치 효과는 알려 주지 않는다.** 면접에서는 "MESI 로 세면 연산당 1번 이동이라 평평해야 하고, 실측도 평평했다. 다만 실제 칩은 디렉터리 + 클러스터 구조라 이동 비용이 위치마다 다르다" 까지 말하면 된다.

### Part C — read-mostly 설정 워드

진짜 스레드 쪽은 **coherence 의 정의** 를 테스트한다. C11 은 한 원자 객체에 대한 모든 수정이 하나의 전체 순서(modification order)를 가지며, 한 스레드가 같은 객체를 두 번 읽으면 두 번째 값이 첫 번째보다 수정 순서에서 앞설 수 없다고 정한다 (read-read coherence, C11 5.1.2.4 — N1570 기준 문단 ¶18–21 근처, 번호 확인 필요). writer 가 1, 2, …, U 를 순서대로 쓰니, reader 가 본 값은 **relaxed 여도** 줄어들 수 없다. 하드웨어가 이걸 지키는 방법이 SWMR + 데이터 값 불변식이다(Sorin·Hill·Wood 2장): 한 라인에 대해 쓰기는 한 시점에 한 코어만, 그리고 읽기는 가장 최근 쓰기 값을 본다.

주의: 이건 **한 주소** 에 대한 보장이다. 서로 다른 두 주소 사이의 순서(설정 워드와 테이블 본문)는 coherence 가 아니라 **consistency** — 메모리 모델의 일이고, release/acquire 나 seqlock(LLAT-M02)이 필요하다. 이 차이를 SB/MP 리트머스로 보는 것이 ARCH-A05, 형식 모델로 세는 것이 MEM-M07 이다.

모델 쪽 — writer 1 + reader R = k−1, every 라운드마다 갱신:

| 단계 | 일어나는 일 | 개수 |
|---|---|---|
| 첫 갱신 | writer I → M (BusRdX, mem) | rdx 1 |
| 갱신 뒤 첫 reader | writer 의 M 에서 c2c + wb, writer M→S | rd 1, c2c 1, wb 1 |
| 나머지 reader R−1 | 메모리에서 S 로 | rd R−1, mem R−1 |
| 다음 갱신 | writer 는 S → BusUpgr, reader 사본 R 개 무효화 | upgr 1, inval R |
| 갱신 없는 라운드 | 전부 hit | 0 |

합계 (U = ⌈n/every⌉): `rdx = 1, upgr = U−1, inval = R(U−1), rd = R·U, c2c = U, wb = U, mem = 1 + (R−1)U`. **갱신 한 번 = 트랜잭션 1 + R, 무효화 R.** 읽기 R·n 번 중 버스를 쓰는 비율 = U·R / (n·R) = 1/every → every = 100, k = 12 면 1% (하네스의 마지막 CHECK). 갱신이 잦아질수록 reader 수에 비례해 비싸진다 — 그래서 커널은 자주 바뀌는 값을 read-mostly 데이터와 다른 라인에 두고(`__read_mostly` 섹션), 큰 구조는 RCU(OS-A03)로 "새 사본을 만들고 포인터 하나만 바꾼다".

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
```

```text
-- Part A: real threads, exact totals (OPS=200000 per thread)
-- Part B: MESI transaction counts vs hand-derived formulas
-- Part C: read-mostly config word (real threads + model)
256/256 passed
```

6번 연속 실행 모두 256/256, 0.27초. 새니타이저 (스레드당 연산 1/10 로 자동 축소 — `__has_feature(thread_sanitizer)` / `address_sanitizer` 로 `OPS` 와 `U` 를 줄인다):

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o san && ./san   # 256/256, 보고 없음, 0.39 s
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o tsan && ./tsan                                     # 256/256, 보고 없음, 1.75 s
```

TSan 이 조용한 이유: 공유 접근이 전부 `_Atomic` (relaxed 포함)이라 C11 의 데이터 레이스 정의(5.1.2.4)에 해당하지 않는다. 독립 `atomic_thread_fence` 는 쓰지 않았다 (Apple TSan 이 모델링하지 않는다 — MCPP-M13).

## 7. 자주 하는 실수

**실수 1 — 스레드별 카운터를 패딩 없이 배열로.** `_Atomic uint64_t cnt[12];` 는 96 B — 라인 하나에 전부 들어간다. 모델로 세면 패턴 a 와 **완전히 같은 개수** 가 나온다 (연습 1: stride 8 → rdx = N, c2c = N−1). 실측 곡선도 a 처럼 평평해진다.

```c
typedef struct { _Alignas(128) _Atomic uint64_t v; } slot_t;   /* 칸 = 라인. 64 가 아니라 128 (이 맥, LLAT-M05) */
static slot_t cnt[12];
```

**실수 2 — "읽기만 하니까 공유해도 공짜" 인데 옆에 쓰기 변수가 있다.** (c) 의 테이블 라인에 통계 카운터 하나가 같이 있으면 그 라인은 더 이상 읽기 전용이 아니다 — 쓰기 한 번마다 모든 reader 사본이 무효화된다 (Part C 공식의 R). read-mostly 데이터와 write-hot 데이터는 다른 라인에 (ARCH-M08 의 hot/cold 분리, 커널 `__read_mostly`).

**실수 3 — CAS 루프로 카운터 증가.** 전용 RMW(`fetch_add` → `ldadd`)가 있는데 CAS 루프를 쓰면 경합에서 이 맥 기준 3.5–5.7배 느리다. 모델로도 연산당 메시지가 2+f 배. CAS 는 최대값·포화처럼 전용 RMW 가 없을 때만 (LLAT-B06 Part B/C).

```c
atomic_fetch_add_explicit(&c, 1, memory_order_relaxed);   /* 루프 없음, 실패 없음 */
```

**실수 4 — 측정 루프를 컴파일러가 지운다.** `uint64_t s = 0; for (...) s += table[i % 64];` 에서 `table` 이 평범한 배열이고 결과를 안 쓰면 루프가 통째로 사라지거나 접힌다. 결과를 구조체에 저장하고(`a->sink = sink`), 읽기는 relaxed atomic 으로, 그리고 **`-S` 로 확인** 한다 (§5 표의 `ldr x13, [x12, x13, lsl #3]`).

**실수 5 — 스레드 생성 순서를 측정한다.** `pthread_create` 직후부터 재면 먼저 만든 스레드가 혼자 도는 구간이 섞여 1 스레드 값처럼 부풀려진다. 모두 준비(`g_ready == k`)된 뒤 `g_go` 를 release 로 켜고, 시작 시각은 그 직전에 잰다 (`demo_bench.c` `one()`).

**실수 6 — 숫자 하나로 결론.** 같은 1 스레드 (c) 가 2859 / 4208, (a) 가 E 코어만이면 32–146 으로 흔들렸다. load average 를 같이 적고, 여러 번 재 중앙값, 그리고 **곡선 모양과 배수** 로 말한다. 코어 고정이 안 되는 맥에서는 E 코어 비율도 같이 기록한다.

**실수 7 — invariant 만 보고 프로토콜이 맞다고 결론.** 시작 코드의 빈 `mesi_write` 는 SWMR 을 한 번도 깨지 않는다 (아무것도 안 하니까). "나쁜 일이 안 일어났다" 와 "해야 할 일을 했다" 는 다른 검사다 — 개수 공식(liveness 쪽)과 불변식(safety 쪽)을 같이 둔다. ARCH-A04 의 데이터 값 불변식이 그 둘째 역할을 한다.

## 8. 면접 Q&A

<details><summary>Q1. 코어 8개가 카운터 하나를 atomic_fetch_add 하면 처리량이 어떻게 되나요? 왜죠?</summary>

1 스레드보다 **떨어지고 그 뒤로 평평** 하다. 이 맥 실측 720 → 171 Mops/s (k=2), k = 12 까지 161–185. RMW 는 라인을 M(독점)으로 가져야 하고 M 은 한 시점에 한 코어만 가질 수 있으니(SWMR), 총 처리량은 "라인 소유권이 넘어가는 속도" 가 상한이다. MESI 로 세면 round-robin 에서 연산마다 BusRdX 1 + cache-to-cache 1 + 무효화 1.

꼬리질문: "그럼 어떻게 고치죠?" → 코어별 카운터(128 B 라인마다) + 읽을 때 합. 9.3배까지 늘었다. 대가는 읽기 쪽이 k 라인을 읽어야 하고, 읽는 순간 값이 근사라는 것. 리눅스 `percpu_counter`, `this_cpu_inc` 가 이 설계 (OS-B05, LLAT-B06 의 배치도 같은 생각).
</details>

<details><summary>Q2. MESI 에서 코어 하나가 S 상태 라인에 쓰기 하나를 하면 무슨 일이 일어나나요?</summary>

요청자는 데이터를 이미 갖고 있으므로 데이터 없이 **BusUpgr**(업그레이드/무효화 요청)를 낸다. S 사본을 가진 다른 코어는 모두 I 가 되고(응답/ack), 요청자는 S → M. 이후 같은 코어의 쓰기는 hit. 만약 요청자가 I 였다면 **BusRdX (= RFO, read for ownership)** — 데이터와 독점을 같이 요청하고, M 주인이 있으면 그 캐시가 데이터를 직접 준다(intervention). 내 모델 표: S → `upgr++, inval += 사본`, I → `rdx++, c2c 또는 mem, inval += 사본`.

꼬리질문: "E 상태는 왜 있죠?" → 혼자 읽은 라인(E)에 쓰면 버스 없이 E→M. 모델에서 k = 1 패턴 a' (ldr + cas) 는 트랜잭션 1개뿐이다. MSI 였다면 BusRd 후 BusUpgr 로 2개 — private 데이터를 읽고 쓰는 흔한 경우를 공짜로 만든다 (MSI vs MESI 카운트 비교는 ARCH-A04).
</details>

<details><summary>Q3. 왜 CAS 루프가 fetch_add 보다 경합에서 훨씬 나쁘죠? 숫자로요.</summary>

`ldr` 로 읽을 때 한 번(S 로), `cas` 로 쓸 때 또 한 번(M 으로) 라인을 요청한다. 실패 없는 최선 스케줄에서도 연산당 2 트랜잭션 (모델 k=8: 1.999). 경합하면 읽기와 cas 사이에 뺏겨 실패하고, 실패한 시도도 라인을 가져온다 → 연산당 대략 2 + f. 실측 f = 0.6 (k=2) → 1.6 (k=12), 처리량은 `ldadd` 의 1/3.5 → 1/5.7. 모두가 동시에 읽고 동시에 cas 하는 최악 스케줄이면 성공 한 번에 약 2k−1 트랜잭션 (연습 2).

꼬리질문: "LL/SC 는요?" → `stxr` 이 모니터가 깨지면 실패하는 구조라 비슷하게 재시도가 생긴다. ARMv8.1 LSE 가 들어온 이유. 세부 측정은 ARM-M04.
</details>

<details><summary>Q4. 모델은 연산마다 라인이 한 번 움직인다고 하는데, 실측과 맞나요?</summary>

곡선 모양(평평)은 맞았지만 이동 횟수는 아니다. `fetch_add` 가 돌려준 옛 값이 "내 직전 값 + 1" 이 아닌 비율을 재 보니 0.29–0.32 — 한 코어가 라인을 잡으면 평균 3.2번 연달아 증가했다. 그리고 연산당 5.85 ns 인데 LLAT-M05 의 P–P 왕복이 74–90 ns 이므로 매 연산 이동은 불가능하다. 이동 한 번 ≈ (5.85 − 1.39) / 0.3 ≈ 15 ns 로 역산된다(추정). 실제 칩은 스누핑 버스가 아니라 클러스터 L2 + 디렉터리/snoop filter 구조이고, 원자 연산을 홈 노드에서 실행(far atomic, CHI)할 가능성도 있다 — Apple 은 공개하지 않았다.

꼬리질문: "그럼 모델이 쓸모 있나요?" → 설계 비교용으로는 그렇다. "패턴 b 는 n 과 무관하게 콜드 미스 k 번", "read-mostly 갱신 1번 = 무효화 R" 같은 **비율과 스케일링** 은 구조와 무관하게 성립한다.
</details>

<details><summary>Q5. 읽기 전용 공유 데이터는 왜 코어 수에 비례해서 빨라지나요? 그런데 가끔 갱신하면요?</summary>

모든 코어가 같은 라인을 **S** 로 들고 각자 L1 에서 읽는다. 쓰는 코어가 없으니 무효화가 없다 (모델: 코어 × 라인당 BusRd 1번뿐). 실측 12 스레드 10.3배.

갱신이 끼면: writer 의 쓰기 한 번이 reader 사본 R 개를 무효화하고, 각 reader 는 다음 읽기에서 다시 가져온다 (트랜잭션 1 + R). 읽기 중 버스를 쓰는 비율 = 1/every. 실측 (d) 에서는 writer 가 약 65–95 ns 마다 갱신해도 reader 처리량이 c 의 62% 였고, 대신 writer 가 reader 수에 따라 느려졌다 (15.1 → 10.8 갱신/µs). 

꼬리질문: "그럼 자주 바뀌는 큰 설정은?" → 버전 워드와 본문을 분리하고 seqlock(LLAT-M02) 또는 RCU(OS-A03) — 본문을 통째로 새로 만들고 포인터 하나만 바꾼다.
</details>

<details><summary>Q6. 12 코어인데 스레드별 카운터가 12배가 안 나와요. 일관성 때문인가요?</summary>

아니다. 모델상 일관성 트래픽은 0 에 가깝다. 원인은 **코어가 같지 않다** 는 것: Super 2 + Performance 4 = 6개까지는 거의 선형(k=6 5.4배), k ≥ 7 은 E 코어가 끼는데 E 코어는 같은 루프를 P 코어의 1/3 속도로 돈다(BACKGROUND QoS 실측 217 vs 650 Mops/s). BACKGROUND 로 E 코어만 쓰면 k = 6 에서 곡선이 멈춘다 — E 코어가 6개뿐이라서. 거기에 다른 프로세스 부하(load 5–8), 클러스터 공유 L2 와 전력/클럭 공유가 더해진다.

꼬리질문: "리눅스면 어떻게 확인하죠?" → `taskset`/`sched_setaffinity` 로 코어를 고정하고(맥은 불가, LLAT-M05), `perf stat` 으로 PMU 이벤트(Armv8 공통 이벤트 `L1D_CACHE_REFILL` 등)를 같이 본다. 어느 라인이 HITM(다른 코어의 M 에서 온 hit)을 많이 내는지는 `perf c2c` (LPERF-A03).
</details>

<details><summary>Q7. coherence 와 consistency 는 어떻게 다르죠? Part C 의 단조성 테스트는 어느 쪽인가요?</summary>

coherence = **한 주소** 에 대한 약속: 모든 쓰기가 하나의 순서를 갖고, 모든 코어가 그 순서와 모순되지 않게 읽는다 (SWMR + 데이터 값 불변식). consistency(메모리 모델) = **여러 주소** 사이의 순서: x 를 쓰고 y 를 쓴 것을 다른 코어가 같은 순서로 보나. Part C 는 한 주소라 coherence 쪽이고, relaxed 로도 성립한다 (C11 5.1.2.4 의 read-read coherence). 서로 다른 주소의 순서는 ARM 에서 relaxed 로는 보장되지 않는다 — OS-M08 에서 relaxed 플래그 메시지 패싱이 실제로 깨졌고, ARCH-A05 / MEM-M07 이 그걸 리트머스로 다룬다.

꼬리질문: "coherent 한 시스템인데 배리어가 왜 필요하죠?" → coherence 프로토콜은 "어느 라인이든 결국 한 값" 만 보장한다. 코어 안의 store buffer, 비순차 load 가 **다른 주소들 사이의 순서** 를 바꾼다 — 그걸 막는 게 `dmb`/`ldar`/`stlr` (ARM-M05).
</details>

<details><summary>Q8. 실리콘 검증에서 캐시 일관성을 어떻게 스트레스 테스트하나요?</summary>

이 레슨 Part A 의 판정이 그대로 쓰인다: 모든 코어가 같은 주소를 RMW 하고 **(1) 최종 합 == 연산 수, (2) 돌려받은 옛 값들이 0..N−1 의 순열** 인지 본다 — 중복은 lost update(일관성 위반), 빠진 값은 쓰기 유실. 여기에 false sharing 패턴(한 라인 안 서로 다른 워드), read-mostly 무효화 폭풍, eviction 을 강제하는 큰 작업 집합, DMA 등 코어가 아닌 에이전트를 섞는다. 판정은 결정적이어야 하고(타이밍 무관), 타이밍은 커버리지를 넓히는 데만 쓴다 (랜덤 지연, 코어 수 sweep). 성능 쪽은 PMU 로 refill/snoop 이벤트를 세어 모델 예측(연산당 이동 1)과 비교한다. 설계 방법은 SIV-M09 (동시성 스트레스), 프로토콜 검증 모델은 ARCH-A04 (불변식 + 돌연변이).
</details>

## 9. 추가 연습

`practice.c` 는 Part B 모델(`mesi_read` / `mesi_write`)을 그대로 복사해 두고 그 위에서 푼다.

### 연습 1 — 모델로 본 false sharing (stride 8 / 64 / 128 / 256)

스레드 t 의 카운터가 바이트 오프셋 t·stride 에 있다 (라인 128 B). round-robin 으로 n 라운드 쓸 때 rdx 와 c2c 를 stride 별로 공식으로 써라. 그리고 LLAT-M05 실측(P 코어끼리는 64 B 에서 이미 간섭 없음, E 가 끼면 128 B 부터)과 비교하라.

<details>
<summary>답 보기</summary>

```c
static int line_of(int t, int stride) { return (int)((uint64_t)t * (uint64_t)stride / LINE); }
/* round-robin write(t, line_of(t, stride)) */
```

- stride 8: 16 칸이 라인 하나 → **패턴 a 와 같다**: rdx = N, c2c = N−1, inval = N−1.
- stride 64: 두 칸씩 라인 공유 (라인 k/2 개) → 두 스레드가 번갈아 쓰니 연산마다 이동: rdx = N, c2c = N − k/2, mem = k/2.
- stride 128, 256: 칸마다 라인 → rdx = k, c2c = 0.

모델의 "라인" 은 128 B 로 정했으니 64 B stride 를 공유로 센다. 그런데 LLAT-M05 는 P 코어끼리 64 B 거리에서 이미 1.4 ns/op(간섭 없음)를 봤다 — P 쪽은 64 B 단위로 소유권을 추적하는 것처럼 보인다(가설). 모델은 "라인 크기 = 일관성 단위" 라는 가정을 하고, 그 가정이 이 칩에서는 코어 종류마다 다를 수 있다. 결론은 같다: 패딩은 `hw.cachelinesize`(128) 이상. stride 측정 자체는 ARCH-M08.

</details>

### 연습 2 — CAS 가 실제로 실패하는 스케줄 (lockstep)

라운드마다 k 스레드가 **모두 먼저 `ldr`**, 그다음 **모두 `cas`**. 첫 `cas` 만 성공하고 나머지는 실패한다. 실패한 `cas` 도 라인 독점을 얻는다고 모델링할 때(x86 `LOCK CMPXCHG` 는 문서상 그렇다), R 라운드의 개수를 공식으로 써라.

<details>
<summary>답 보기</summary>

```c
for (uint64_t r = 0; r < rounds; r++) {
    for (int t = 0; t < k; t++) mesi_read(s, t, 0);
    for (int t = 0; t < k; t++) { mesi_write(s, t, 0); if (t == 0) ok++; }
}
```

라운드 r > 0 의 시작: 직전 라운드 마지막 `cas` 를 한 스레드 k−1 만 M. 읽기 단계: t0 은 그 M 에서 c2c(+wb, 주인 M→S), t1..t(k−2) 는 메모리, **t(k−1) 은 이미 S 라 hit** → 읽기 k−1 번. cas 단계: t0 은 S → BusUpgr(사본 k−1 개 무효화), t1..t(k−1) 은 각자 BusRdX(앞사람 M 에서 c2c, 무효화 1).

합계 (k ≥ 2): rd = (k−1)R + 1, upgr = R, rdx = (k−1)R, inval = 2(k−1)R, c2c = (k−1)R + R − 1, wb = R − 1, mem = k + (k−2)(R−1), **버스 트랜잭션 = (2k−1)R + 1 → 성공 1번에 약 2k−1**. 처음 써 본 공식은 rd = kR 였는데 모델이 틀렸다고 알려 줬다 — 마지막 cas 주인의 S 사본을 잊었다. 이런 "손 공식 vs 모델" 대조가 이 레슨의 방법이다.

실측과 비교: round-robin(2) 과 lockstep(2k−1) 사이에 진짜가 있다. 실측 `f` = 1.6 이면 2 + f = 3.6 — 실제 스레드는 완전한 lockstep 이 아니다.

</details>

### 연습 3 — 코어별 카운터 + 주기적으로 합을 읽는 텔레메트리 코어

코어 0..k−1 은 자기 라인에 매 라운드 쓰고, 코어 k(텔레메트리 태스크)는 P 라운드마다 라인 k 개를 전부 읽는다. n 이 P 의 배수일 때 개수 공식과 "증가 1번당 트랜잭션" 을 구하라.

<details>
<summary>답 보기</summary>

합 한 번(S = n/P 번): 라인마다 BusRd — 주인이 M 이니 c2c + wb, 주인 M→S. 다음 라운드에 주인이 쓰면 S → BusUpgr, 텔레메트리 사본 무효화. 마지막 합(r = n−1) 뒤에는 쓰기가 없다.

rdx = k (콜드), rd = c2c = wb = k·S, upgr = inval = k(S−1). 증가 k·n 번당 트랜잭션 ≈ 2kS / kn = **2/P**. k = 8, n = 1000, P = 10 → 8 + 800 + 792 = 1600 → 증가당 0.2.

설계 의미: 코어별 카운터는 공짜가 아니다 — **읽는 빈도** 만큼 비용을 낸다. 10 ms 마다 읽는 텔레메트리는 증가가 초당 수백만이면 무시할 만하고, 매 패킷 합을 보는 코드라면 공유 카운터와 다를 바 없다. 리눅스 `percpu_counter_sum()` 이 "느리다, 자주 부르지 말라" 고 하는 이유, `percpu_counter_read()` 가 근사값(중앙 카운터만)을 주는 이유다.

</details>

### 연습 4 — 측정된 핸드오프 비율로 이동 한 번의 값 역산

`transfer_ps(contended_ps_per_op, local_ps_per_op, h_permille)` = (경합 시 연산당 시간 − 로컬 연산당 시간) × 1000 / h. 실측(171 Mops/s → 5850 ps, 1 스레드 720 → 1390 ps, h = 0.300)을 넣어라. h = 1 (round-robin 가정)이면?

<details>
<summary>답 보기</summary>

```c
static uint64_t transfer_ps(uint64_t c, uint64_t l, uint64_t h_permille) {
    if (h_permille == 0 || c <= l) return 0;
    return (c - l) * 1000 / h_permille;
}
```

h = 0.3 → 14 866 ps ≈ **15 ns**. h = 1 로 믿으면 4.5 ns — LLAT-M05 의 P–P 왕복 74–90 ns(편도 약 40 ns) 와 맞지 않는다. 15 ns 도 편도 40 ns 보다 짧다: 핑퐁은 "쓰기 → 다른 코어가 **보고** 반응" 까지 재지만, 여기서는 이동이 파이프라인에서 다른 일과 겹칠 수 있고, 같은 클러스터 L2 안의 이동일 수도 있고, far atomic 이면 이동이 아예 없을 수도 있다. 숫자 하나가 여러 해석과 맞는다 — 그래서 "추정" 이다. 이 연습의 요점은 **모델의 가정(h = 1)을 측정값으로 바꾸면 결과가 3배 달라진다** 는 것.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
```

```text
-- 1: stride false sharing in the model
-- 2: lockstep CAS (failures)
-- 3: per-thread counters + telemetry reader
-- 4: transfer time from measured handoff ratio
106/106 passed
```

## 10. 핵심 정리

- 공유가 비싼 게 아니라 **공유된 라인에 쓰기** 가 비싸다: 쓰기는 M(독점)이 필요하고, 다른 사본을 모두 무효화한다 (RFO = BusRdX, S 면 BusUpgr).
- (a) 공유 `fetch_add`: 연산당 소유권 이동 1 (모델) → 총 처리량 평평. 이 맥 720 → 161–185 Mops/s (k = 2..12).
- (b) 128 B 라인마다 코어별 카운터: 콜드 미스 k 번뿐 → 선형 (k=6 5.4배, k=12 9.3배; 꺾임은 E 코어 속도). 읽는 쪽 비용 = 2/P 트랜잭션/증가.
- (c) 읽기 전용 공유: 모두 S → 선형 (k=12 10.3배). 갱신 1번 = 트랜잭션 1 + R, 무효화 R.
- CAS 루프 = 읽기 1 + 시도마다 독점 1 → 연산당 2 + f, 실측 `ldadd` 의 1/3.5 ~ 1/5.7. 전용 RMW 가 있으면 그걸 쓴다.
- 모델은 모양과 비율을 맞히고 절대 시간·코어 배치는 못 맞힌다: 실측 핸드오프 0.3/op(버스트 소유), 클러스터 + 디렉터리, far atomic 가능성.
- coherence 는 한 주소의 약속(relaxed 로도 값이 뒤로 안 감), 여러 주소의 순서는 메모리 모델(배리어)의 일.
- 실리콘 검증 판정: RMW 옛 값이 0..N−1 의 순열 + 합계 — 타이밍과 무관한 결정적 기준.

## 참고자료

- Daniel J. Sorin, Mark D. Hill, David A. Wood, *A Primer on Memory Consistency and Cache Coherence*, 2nd ed. (Morgan & Claypool, 2020) — 2장 coherence 정의(SWMR, data-value invariant), 7장 스누핑, 8장 디렉터리 (장 번호는 2판 기준, 확인 필요)
- Mark S. Papamarcos, Janak H. Patel, "A Low-Overhead Coherence Solution for Multiprocessors with Private Cache Memories", ISCA 1984 (Illinois / MESI)
- David Culler, J. P. Singh, *Parallel Computer Architecture: A Hardware/Software Approach* (1999) — 5장 Shared Memory Multiprocessors (MESI 상태도, BusRd/BusRdX/BusUpgr/Flush), 6장 snoop-based 설계
- ISO/IEC 9899:2011 (N1570 초안) 5.1.2.4 Multi-threaded executions and data races (modification order, coherence 규칙), 7.17.3 (RMW 는 수정 순서의 마지막 값을 읽는다): <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Arm, AMBA 5 CHI Architecture Specification (IHI 0050) — Atomic transactions (홈 노드에서 실행되는 AtomicStore/AtomicLoad/AtomicSwap/AtomicCompare), 디렉터리/snoop filter 기반 일관성: <https://developer.arm.com/documentation/ihi0050/latest/>
- Arm A64 ISA — `LDADD`, `CAS`: <https://developer.arm.com/documentation/ddi0602/latest/>; Armv8 PMU 공통 이벤트 (`L1D_CACHE_REFILL` 등): Arm ARM DDI 0487, D-chapter "Performance Monitors Extension" (번호 확인 필요)
- Intel 64 and IA-32 SDM Vol. 2A, CMPXCHG — "destination operand receives a write cycle without regard to the result of the comparison": <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
- Paul E. McKenney, *Is Parallel Programming Hard, And, If So, What Can You Do About It?* — 5장 Counting (per-thread 카운터, 읽기 비용), 3장 하드웨어와 그 습관: <https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook.html>
- Linux `lib/percpu_counter.c` (`percpu_counter_sum` vs `percpu_counter_read`), `include/linux/cache.h` (`__read_mostly`, `____cacheline_aligned_in_smp`): <https://elixir.bootlin.com/linux/latest/source/lib/percpu_counter.c>
- 관련 레슨: OS-B05 (뮤텍스·atomic·sloppy 카운터 처리량), LLAT-B06 (원자 연산 비용, CAS 재시도), ARM-M04 (LL/SC vs LSE), OS-M07 (스핀락과 일관성 트래픽), LLAT-M05 (코어 간 왕복 지연, 128 B 경계, QoS), C-A03 (memory_order), OS-M08 (relaxed 메시지 패싱 실패), LLAT-M02 (seqlock), OS-A03 (RCU), MCPP-M13 (TSan 과 fence), ARCH-M08 (false sharing stride 측정 — 같은 층), ARCH-A04 (MESI 시뮬레이터 — 같은 층), ARCH-A05 / MEM-M07 (리트머스, 메모리 모델 — 같은 층), MEM-M05 (MOESI/MESIF·디렉터리), LPERF-A03 (perf c2c), SIV-M09 (동시성 스트레스 설계), RTOS-A08 (SMP RTOS)
