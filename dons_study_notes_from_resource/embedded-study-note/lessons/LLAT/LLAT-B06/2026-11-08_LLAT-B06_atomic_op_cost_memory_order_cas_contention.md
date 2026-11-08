---
id: LLAT-B06
time: 70분
updated: 2026-11-08
---
# LLAT-B06 atomic 연산 비용: relaxed·acq_rel·seq_cst·CAS 경합

## 학습 목표

- 마이크로벤치마크를 믿을 수 있게 만드는 기본기 — 타이머 눈금(이 맥 41.7 ns), 반복 후 나누기, 워밍업, 여러 번 재서 **중앙값**, 의존 체인(지연) vs 독립 연산(처리량), "컴파일러가 지운 건 아닌가" asm 확인 — 를 실제로 해 보고 설명할 수 있다.
- Apple M6 에서 `fetch_add` 를 memory_order 별로, CAS 루프, LSE `ldadd` / `stadd`, LL/SC(`ldxr/stxr`)로 쟀을 때 **단일 스레드에서는 대부분 6 사이클 안팎으로 비슷하고**, 비싼 것은 메모리 순서가 아니라 **경합** 이라는 것을 숫자로 말한다.
- 1~8 스레드가 같은 캐시 라인을 두드릴 때 `ldadd` / `ldaddal` / LL/SC / CAS 루프가 어떻게 무너지는지(재시도 횟수 포함) 표와 그래프로 읽는다.
- hot path 에서 원자 연산을 줄이는 다섯 가지 — 스레드별 칸 + 합산, 배치, 필요한 만큼만 relaxed, CAS 루프 대신 `fetch_add`/`fetch_or`, 먼저 읽고 필요할 때만 RMW(read-before-CAS, TTAS) — 를 구현하고 효과를 잰다.
- 인라인 asm 으로 `ldadd` · `ldxr/stxr` · `stadd` · `ldumax` 를 직접 쓰고, 같은 C 코드가 Cortex-A53(LL/SC) · A55(LSE) · Cortex-M4(`LDREX/STREX`+`DMB`)에서 어떻게 달라지는지 안다.

## 1. 문제

"seq_cst 는 relaxed 보다 얼마나 비싸요? CAS 루프랑 fetch_add 는요?" — 저지연(HFT, 로봇 제어) 면접과 펌웨어 면접에서 똑같이 나오는 질문이고, **숫자로** 답해야 하는 질문이다. 펌웨어에서는 패킷·샘플마다 도는 ISR/핫 루프 안의 통계 카운터, 듀얼 코어 SoC(Cortex-A + A, 또는 A + M 메일박스)의 공유 플래그가 바로 이 비용을 낸다. 이 레슨은 **원자 연산 하나의 값** 과 **경합의 값** 을 따로 재고, 그걸 줄이는 기법을 실측으로 비교한다.

이미 아는 것(다시 가르치지 않는다): memory_order 의 의미와 arm64 / Cortex-M4 로 내려가는 명령(C-A03 `demo_asm.c`), 뮤텍스·atomic·sloppy 카운터의 1~8 스레드 처리량(OS-B05), false sharing(see: ARCH-M08). 여기서는 **비용** 만 본다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="22" fill="currentColor" font-weight="bold">원자 연산 비용 = ① 명령 자체 + ② 순서 보장 + ③ 캐시 라인 이동(경합)</text><rect x="10" y="40" width="340" height="180" rx="6" class="f-mut" stroke="currentColor"/><text x="22" y="62" fill="currentColor" font-weight="bold">단일 스레드: 라인이 내 L1 에 있다</text>
<rect x="40" y="80" width="120" height="40" rx="4" class="f-ok" stroke="currentColor"/><text x="54" y="105" fill="currentColor">코어 0</text><rect x="200" y="80" width="120" height="40" rx="4" class="f-acc" stroke="currentColor"/>
<text x="214" y="98" fill="currentColor">L1: counter</text><text x="214" y="114" fill="currentColor" font-size="12">(독점 상태)</text><path d="M160 100 L200 100" stroke="currentColor" fill="none"/>
<text x="22" y="150" fill="currentColor">ldadd / cas / ldxr+stxr 모두 L1 안에서 끝남</text><text x="22" y="170" fill="currentColor">→ 이 맥 실측 약 6 사이클 (plain add 와 비슷)</text><text x="22" y="190" fill="currentColor">memory_order 차이는 대부분 0 (실험 1)</text>
<text x="22" y="210" fill="currentColor" font-size="12">LL/SC 만 1.7배, 예외: ldadda (1.7배, 원인 미확인)</text><rect x="380" y="40" width="350" height="180" rx="6" class="f-mut" stroke="currentColor"/><text x="392" y="62" fill="currentColor" font-weight="bold">경합: 라인이 코어 사이를 오간다</text>
<rect x="400" y="80" width="90" height="36" rx="4" class="f-ok" stroke="currentColor"/><text x="414" y="103" fill="currentColor">코어 0</text><rect x="620" y="80" width="90" height="36" rx="4" class="f-ok" stroke="currentColor"/>
<text x="634" y="103" fill="currentColor">코어 1</text><rect x="510" y="128" width="90" height="36" rx="4" class="f-warn" stroke="currentColor"/><text x="518" y="151" fill="currentColor">counter 라인</text>
<path d="M490 110 L520 128 M600 128 L630 116" stroke="currentColor" fill="none"/><path d="M500 100 L610 100" stroke="currentColor" stroke-dasharray="4 3" fill="none"/><text x="520" y="94" fill="currentColor" font-size="12">소유권 이동</text>
<text x="392" y="188" fill="currentColor">한 번에 한 코어만 쓸 수 있다 → 총 처리량이</text><text x="392" y="208" fill="currentColor">"라인 이동 속도" 에 묶임 (실험 2: 4~29배 느림)</text><rect x="10" y="236" width="720" height="84" rx="6" fill="none" stroke="currentColor"/>
<text x="22" y="258" fill="currentColor" font-weight="bold">RMW 를 만드는 세 가지 방법 (arm64)</text><text x="22" y="280" fill="currentColor" font-family="monospace" font-size="12">LSE    : ldadd x1, x0, [x2]            ← 명령 하나, 실패 없음</text><text x="22" y="298" fill="currentColor" font-family="monospace" font-size="12">LL/SC  : 1: ldxr / add / stxr / cbnz 1b ← 다른 코어가 끼면 stxr 실패 → 재시도</text>
<text x="22" y="316" fill="currentColor" font-family="monospace" font-size="12">CAS 루프: ldr → (계산) → cas → 다르면 다시 ← 읽기와 쓰기 사이에 뺏기면 재시도</text>
</svg>
```

체크 테스트(`starter.c` → `solution.c`)는 **정확성만** 본다 — 최종 합계, RMW 를 몇 번 했는지, OR/최대값. 시간은 절대 CHECK 하지 않는다.

### Part A — 측정 도구와 원자 증가 세 가지

- `stats_of(samples, n)` — 표본 n 개(1..64)의 최소·중앙값·최대 (원본 보존, 중앙값 = 정렬 후 `[n/2]`). `ps_per_op(t0_ns, t1_ns, ops)` — 연산 1개당 피코초(정수, ops == 0 이면 0).
- `lse_fetch_add(p, v)` — 인라인 asm `ldadd` 한 명령. `llsc_fetch_add(p, v)` — `ldxr / add / stxr / cbnz` 루프. `cas_fetch_add(p, v)` — C11 `atomic_compare_exchange_weak` 루프. 셋 다 옛 값을 돌려주고 relaxed 에 해당.
- 관찰: `demo_bench.c` 실험 1(단일 스레드) · 실험 2(1~8 스레드 경합).

### Part B — hot path 에서 원자 연산 줄이기

- `batch_add(b, k)` / `batch_flush(b)` — 스레드마다 로컬에 모았다가 `limit` 개마다 공유 카운터에 `fetch_add` **한 번**. flush 는 넘길 게 없으면 RMW 를 하지 않는다. 반환값 = 이번 호출이 RMW 를 했는가.
- `flags_set(f, bits)` — sticky 플래그 OR. **먼저 읽어서** 이미 다 켜져 있으면 쓰지 않는다. 쓸 때는 `fetch_or` 한 번(CAS 루프 금지).
- 관찰: `demo_hotpath.c` (8 스레드, 기법별 처리량).

### Part C — 듀얼 코어 패킷 경로 텔레메트리

두 코어가 각자 RX 큐를 처리하는 네트워크 SoC(예: Cortex-A53 ×2 또는 A55 ×2 의 리눅스 사용자 공간 드라이버, 또는 베어메탈 패킷 엔진)를 흉내 낸다. 패킷마다 호출되는 `tel_on_packet(t, core, len, lat, err_bits)` 가 hot path 다.

- 패킷 수·바이트 = **코어별 칸**(writer 하나, RMW 없음 — OS-B05 Part C 의 `bump` 를 그대로 받는다). 오류 비트 = `flags_set` (sticky, 대부분 이미 켜져 있음).
- 최대 처리 지연 = `wm_update(m, v)` — **v 가 현재 최대보다 클 때만** CAS (read-before-CAS).
  반환값 = 이번 패킷이 한 공유 RMW 횟수. `tel_snapshot` 은 텔레메트리 태스크가 합계를 읽는다.

## 2. Problem (English)

You are optimizing the per-packet hot path of a dual-core packet engine and must justify every atomic.

**Part A.** Implement `stat3_t stats_of(const uint64_t *s, int n)` (min / median = sorted[n/2] / max of 1..64 samples, input untouched, `{0,0,0}` for invalid n) and `uint64_t ps_per_op(uint64_t t0_ns, uint64_t t1_ns, uint64_t ops)` (picoseconds per op, truncating; 0 if `ops == 0`). Then implement `fetch_add` three ways, each returning the old value with relaxed ordering:
`uint64_t lse_fetch_add(uint64_t *p, uint64_t v)` with one ARMv8.1 `LDADD` (GNU inline asm), `uint64_t llsc_fetch_add(uint64_t *p, uint64_t v)` with an `LDXR/STXR` retry loop, and `uint64_t cas_fetch_add(_Atomic uint64_t *p, uint64_t v)` with a C11 CAS loop. Four threads per implementation (12 at once) × 200 000 increments must give exact totals.

**Part B.** `bool batch_add(batch_t *b, uint64_t k)` accumulates locally and performs exactly one `fetch_add` on the shared counter every `limit` events; `bool batch_flush(batch_t *b)` pushes the remainder (no RMW when empty). `bool flags_set(_Atomic uint32_t *f, uint32_t bits)` ORs bits into a sticky flag word, performing **no write** when all bits are already set, and otherwise exactly one `fetch_or`. Return value = "did an RMW".

**Part C.** `bool wm_update(_Atomic uint32_t *m, uint32_t v)` raises a high-water mark, attempting CAS only while `v > current`. `int tel_on_packet(tel_t *t, int core, uint32_t len, uint32_t lat, uint32_t err)` updates per-core single-writer counters, sticky error flags and the latency watermark, returning the number of shared RMWs performed. `tel_snapshot` sums everything.

Constraints: arm64 only (inline asm), C11 + pthreads, no timing in tests. Example: `wm_update` over `5,3,5,9,9,1,12,12,11` writes 3 times; 1000 × `flags_set(f, 1)` writes once. Follow-up: measure each primitive single-threaded and under 1–8 threads, and explain the numbers.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, exit 1, 0.1초 안에 끝남 — TODO 스텁은 루프를 돌지 않고 틀린 값을 돌려준다):

```text
-- Part A: stats_of / ps_per_op
FAIL starter.c:172  r.min == 10 && r.median == 30 && r.max == 50
...
FAIL starter.c:204  a_lse == (uint64_t)NT * OPS
FAIL starter.c:205  a_llsc == (uint64_t)NT * OPS
FAIL starter.c:206  atomic_load(&a_cas) == (uint64_t)NT * OPS
-- Part B: batch counter
FAIL starter.c:232  flushed == 0 && atomic_load(&sh) == 0 && b.n == 15
...
-- Part C: 2 cores
FAIL starter.c:308  s.pkts == (uint64_t)NCORE * CEV
...
8/48 passed
```

관찰용 파일 (하네스와 별개, 숫자는 CHECK 하지 않는다):

```bash
cc -std=c11 -Wall -Wextra -O2 -pthread demo_bench.c -o bench && ./bench 7 1     # 실험 1: 단일 스레드 (약 4초)
./bench 5 2                                                                     # 실험 2: 1~8 스레드 경합 (약 20초)
cc -std=c11 -O2 -S demo_bench.c -o - | less                                     # 측정한 함수 lat_* / th_body 의 asm
cc -std=c11 -Wall -Wextra -O2 -pthread demo_hotpath.c -o hot && ./hot 5 8       # 실험 3: 기법별 효과 (약 6초)
cc -std=c11 -g -O1 -fsanitize=thread -pthread -DMODE=0 demo_tsan_asm.c -o t0 && ./t0   # TSan 은 asm 을 못 본다
```

## 4. 힌트

<details>
<summary>힌트 1 — 연산 1개가 1.4 ns 인데 타이머 눈금이 41 ns 면?</summary>

하나씩 재면 눈금(41.7 ns)과 타이머 호출 비용(약 7 ns)이 결과를 덮는다. 수천만 번 돌린 구간 전체를 한 번 재고 횟수로 나눈다. `ps_per_op` 는 그 나눗셈이다 — ns 로 나누면 1.39 ns 가 1 이 되니 ×1000 해서 피코초 정수로.
</details>

<details>
<summary>힌트 2 — 인라인 asm 제약(constraint)</summary>

`ldadd Xs, Xt, [Xn]` : Xs = 더할 값, Xt = 옛 값을 받을 레지스터, Xn = 주소. 출력 `"=r"(old)`, 입력 `"r"(p)`, `"r"(v)`. 64비트 레지스터 이름을 강제하려면 `%x0`. 메모리를 바꾸므로 `"memory"` clobber. LL/SC 루프는 출력이 입력보다 먼저 쓰이므로 early-clobber `"=&r"` 이 필요하다 (안 그러면 컴파일러가 같은 레지스터를 줄 수 있다). `stxr` 의 상태는 32비트 → `%w2`.
</details>

<details>
<summary>힌트 3 — CAS 루프의 old 는 누가 갱신하나?</summary>

`atomic_compare_exchange_weak(p, &old, new)` 가 실패하면 `old` 에 **현재 값** 이 들어온다. 그러니 루프 안에서 다시 load 할 필요가 없다. weak 는 값이 같아도 실패할 수 있으니(C11 7.17.7.4, LL/SC 구현 대비) 반드시 루프 안에서.
</details>

<details>
<summary>힌트 4 — batch_add 가 flush 를 부르는 조건</summary>

`local += k; n++` 한 뒤 `n == limit` 이면 flush. flush 는 `n == 0` 이면 아무것도 안 하고 false. 스레드 종료 시 남은 것을 꼭 flush — 안 하면 `BEV = 100003` 처럼 16 의 배수가 아닌 경우 합계가 모자란다.
</details>

<details>
<summary>힌트 5 — wm_update 의 루프 조건</summary>

`cur = load; while (v > cur) { if (CAS(&cur → v)) return true; }` — CAS 가 실패하면 cur 가 더 큰 값으로 바뀌어 있을 수 있고, 그러면 조건이 거짓이 되어 **쓰지 않고** 끝난다. "매번 CAS" 버전과의 차이가 실험 3 에서 85배다.
</details>

## 5. 풀이

### 0. 측정법 — 이 레슨에 필요한 만큼

마이크로벤치마크 방법론 전체는 LLAT-B01(클럭 소스) · LLAT-B02(분포) · LLAT-B03(방법론)에서 다룬다. 여기서 쓰는 것만:

| 항목 | 이 레슨에서 한 것 | 이유 |
|---|---|---|
| 타이머 | `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` | macOS 의 단조 증가 ns 시계. 실측: `mach_timebase_info` = 125/3 (눈금 41.67 ns), `clock_getres` 42 ns, 호출 비용 약 7 ns |
| 반복 후 나누기 | 실험 1: 2천만 번 / 실험 2: 스레드당 2백만 번 | 연산 하나(1~4 ns)는 눈금보다 훨씬 작다 |
| 워밍업 | 구성마다 1/4 크기로 한 번 먼저 | 코드·데이터를 캐시에 올리고 클럭을 올린다 |
| 반복 측정 | 7회(실험 1) / 5회(실험 2·3), **중앙값** + 최소/최대 | 다른 프로세스가 끼면 한두 번이 튄다 — 평균은 끌려가고 중앙값은 안 끌려간다 |
| 실행 자체도 여러 번 | 각 실험 프로그램을 **3번** 돌리고 실행별 중앙값의 중앙값 | 이 시간에 다른 벤치마크도 돌고 있었다 (load average 4.7 → 11.5) |
| 지연 vs 처리량 | 지연 = 결과가 다음 연산의 입력이 되는 **의존 체인**, 처리량 = 결과를 안 쓰는 독립 연산 | 같은 명령이라도 둘은 다를 수 있다 (실험 1 의 "4 lines") |
| asm 확인 | 측정한 함수마다 `cc -O2 -S` 로 루프 본문 확인 | 처음 버전은 clang 이 load 를 **지워 버려서** 틀린 숫자가 나왔다 (아래) |
| 코어 고정 | 못 함 (macOS 에 `sched_setaffinity` 없음). `QOS_CLASS_USER_INTERACTIVE` 로 P 코어 선호 힌트만 | 그래서 스레드 8개면 Efficiency 코어에도 올라간다 |

의존 체인은 `v = atomic_fetch_add_explicit(&g_a, dep1(v), mo)` 처럼 만들었다. `dep1(v)` 는 인라인 asm `lsr z, v, #63` (v 는 2^63 보다 작으므로 0) 에 1 을 더한 값 — 늘 1 이지만 CPU 는 v 가 나올 때까지 기다려야 한다. C 로 `v & 0` 같은 걸 쓰면 컴파일러가 접어 버리고 체인이 끊긴다. asm 안은 컴파일러가 모르므로 접지 못한다. 대가로 체인마다 `lsr + add` 2 사이클이 붙는데, 모든 줄에 똑같이 붙으므로 비교는 공정하다. 사이클 환산은 "레지스터만 쓰는 같은 체인(lsr + add + add = 3 사이클)" 이 0.68 ns 라는 데서 1 사이클 ≈ 0.226 ns(약 4.4 GHz)로 **추정** 했다 — 클럭을 직접 읽은 것이 아니다.

**실제로 당한 함정 두 개.**

1. 처음 버전에서 `static _Atomic uint64_t g_b;` 를 아무도 쓰지 않았더니, clang 이 "항상 0" 으로 보고 **seq_cst load 까지 지워 버렸다** (`stlr` 만 남은 asm 을 보고 알았다). 전역을 static 이 아니게 바꾸고 main 에서 한 번 쓰게 했다. 숫자가 이상하게 좋으면 asm 부터 본다.
2. 자기 자신을 가리키는 포인터를 `p = load(p)` 로 따라가면 relaxed `ldr` 체인이 **1 사이클(0.23 ns)** 로 나왔다. L1 load-to-use 지연(이 맥 실측 3 사이클)보다 짧다 — 늘 같은 값을 돌려주는 load 는 CPU 가 값을 **예측** 해 버리는 것으로 보인다 (Apple 코어의 load value/address prediction 은 외부 연구 기준, M6 에서 원인은 미검증). 128 B 간격 노드 256개를 무작위 순서로 엮은 링으로 바꾸자 3 사이클(0.70 ns)이 나왔다. **너무 예측하기 쉬운 벤치마크는 예측기를 잰다.**

### Part A — 측정 도구

`stats_of` 는 64칸 지역 배열에 삽입 정렬하며 복사한다 (원본 보존). 표본이 7개면 삽입 정렬로 충분하고, 중앙값 `[n/2]` 은 n 이 짝수면 위쪽 가운데다. 테스트의 `{9, 1000, 9, 9, 9, 9, 9}` 가 요점이다 — 튀는 1000 이 하나 있어도 중앙값은 9 다. 평균이었다면 150 이 된다. 실험 2 의 CAS 루프 T=5 처럼 `[28.6–74.3]` 으로 튀는 줄이 실제로 있었다.

`ps_per_op` 는 `(t1 - t0) * 1000 / ops`. 1.39 ns 같은 값을 정수로 다루려고 피코초 단위를 쓴다. `t1 < t0` 이나 `ops == 0` 은 0 (나누기 0 방지).

### Part A — 원자 증가 세 가지

`lse_fetch_add` 는 asm 한 줄, `llsc_fetch_add` 는 네 줄 루프다 (§6 정답 코드).

- `ldadd Xs, Xt, [Xn]` (ARMv8.1 LSE, FEAT_LSE — 이 맥 `sysctl hw.optional.arm.FEAT_LSE` = 1): 메모리 값을 Xt 로 읽고 Xs 를 더해 다시 쓰는 것을 **원자적으로 한 명령에**. 접미사 없음 = 순서 보장 없음(relaxed), `a` = acquire, `l` = release, `al` = 둘 다.
- `volatile` + `"memory"` clobber: 컴파일러가 이 asm 을 지우거나 주변 메모리 접근을 asm 너머로 옮기지 못하게 한다 (C-A02 의 컴파일러 배리어와 같은 역할). CPU 순서 보장은 명령 접미사가 한다.
- `ldxr` 는 값을 읽으며 그 주소에 **exclusive monitor** 를 건다. `stxr` 은 그 사이에 다른 코어(또는 예외)가 모니터를 깨지 않았을 때만 쓰고 상태 0 을 돌려준다. 1 이면 처음부터 다시. 모니터의 세부(그래뉼, 예외 시 클리어, `clrex`)는 ARM-M04 / ARM-M11 에서 다룬다.
- `"=&r"` (early-clobber): `old` 는 `p`, `v` 를 다 읽기 전에 써진다. 이 표시가 없으면 컴파일러가 `old` 와 `p` 에 같은 레지스터를 줄 수 있다.

`cas_fetch_add` 는 C11 그대로다. 이 맥 clang 은 이것을 `ldr` + `cas` 로 낸다 (아래 asm). `cas` 도 LSE 명령이라 단일 스레드에서는 `ldadd` 와 같은 6 사이클이지만, **읽기(ldr)와 쓰기(cas)가 두 번에 나뉘어** 있어서 경합에서 무너진다 (실험 2).

#### 이 맥 clang 은 무엇을 내나 — LL/SC 를 보려면 asm 으로

Apple clang 은 C11 원자 연산을 **항상 LSE 로** 낸다. `-march=armv8-a` 를 줘도 `-###` 로 보면 `-target-cpu apple-m1` 이 그대로라 `ldadd` 가 나왔고, `-mno-outline-atomics` 도 바뀐 게 없었다. LL/SC 를 C 컴파일러에서 보려면 LSE 없는 CPU 를 지정해야 한다 (실측):

| 컴파일 | `fetch_add(relaxed)` | `fetch_add(seq_cst)` | `load(acquire)` |
|---|---|---|---|
| `cc -O2` (arm64 macOS 기본) | `ldadd` | `ldaddal` | `ldapr` |
| `cc -O2 -march=armv8-a` | `ldadd` (그대로) | `ldaddal` | `ldapr` |
| `cc -O2 -mcpu=cortex-a53` | `ldxr / add / stxr / cbnz` | `ldaxr / add / stlxr / cbnz` | `ldar` |
| `cc -O2 -mcpu=cortex-a53 -moutline-atomics` | `bl ___aarch64_ldadd8_relax` | (헬퍼 호출) | — |
| `clang --target=aarch64-none-elf -mcpu=cortex-a55` | `ldadd` | `ldaddal` | `ldapr` |

Cortex-A53 은 ARMv8.0 (LSE 없음), A55 는 ARMv8.2 (LSE 있음) — Part C 의 "같은 C 코드" 가 칩에 따라 다른 명령이 된다. outline atomics 는 런타임에 LSE 유무를 보고 고르는 헬퍼 함수 호출이다 (리눅스 배포판 GCC 10+ 은 aarch64 에서 이것이 기본 — GCC 문서 기준, 이 레슨에서 미검증). 그래서 이 레슨은 LL/SC 를 **인라인 asm** 으로 직접 써서 같은 머신에서 비교한다.

#### 실험 1 — 단일 스레드 지연 (`demo_bench.c`)

측정한 함수의 루프 본문 (실측 `cc -std=c11 -O2 -S demo_bench.c`, 체인용 `lsr/add` 포함):

```text
lat_plain        ldr x0,[x9] ; lsr x10,x0,#63 ; add x10,x0,x10 ; add x10,x10,#1 ; str x10,[x9]
lat_fa_relaxed   lsr x8,x8,#63 ; add x8,x8,#1 ; ldadd   x8, x8, [x9]
lat_fa_acquire / release / acq_rel / seq_cst   (같은 모양)  ldadda / ldaddl / ldaddal / ldaddal   ← seq_cst 는 acq_rel 과 같은 명령
lat_fa_unused_*  ldadd x9, x10, [x8]   /  ldaddal x9, x10, [x8]          ← 결과를 안 써도 stadd 가 아님
lat_stadd        stadd x9, [x8]                                          (인라인 asm)
lat_cas_relaxed  ldr x11,[x10] ; lsr ; add ; (안쪽 루프) mov ; add ; cas   x11, x13, [x10] ; cmp ; b.ne
lat_cas_seq_cst  … casal x11, x13, [x10] …
lat_llsc         lsr ; add ; ldxr x8,[x9] ; add ; stxr w12, x11, [x9] ; cbnz w12     (acqrel 판: ldaxr / stlxr)
lat_ring_*       ldr x0,[x0]  /  ldapr x0,[x0]  /  ldar x0,[x0]
lat_st_*         str x8,[x9]  /  stlr x8,[x9]  /  stlr x8,[x9]             ← release 와 seq_cst store 는 같은 명령
lat_sl_rel_acq   stlr x9,[x10] ; ldapr x12,[x11]      lat_sl_seq_cst   stlr x9,[x10] ; ldar x12,[x11]
```

측정 환경: `sysctl -n machdep.cpu.brand_string` = Apple M6, `hw.perflevel0/1/2.physicalcpu` = 2 Super + 4 Performance + 6 Efficiency, `hw.cachelinesize` = 128, `FEAT_LSE` = 1, `FEAT_LRCPC` = 1, `Apple clang version 21.0.0 (clang-2100.0.123.102)`, macOS 27.2. 실행 3번, 각 7회 중앙값 → 실행별 중앙값의 중앙값 [3회 범위]. 시작 시 load average 4.75 / 4.77 / 4.81 (다른 에이전트의 벤치마크도 돌고 있었다). 절대값보다 **배수** 를 보라.

```svg
<svg viewBox="0 0 740 402" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="22" fill="currentColor" font-size="13" font-weight="bold">실험 1 — 단일 스레드, 연산 1개당 사이클 (추정, 3회 실행 중앙값)</text><text x="262" y="59" fill="currentColor" text-anchor="end">plain add (ldr/add/str)</text><rect x="270" y="46" width="146.4" height="18" class="f-mut" stroke="currentColor"/>
<text x="422.4" y="59" fill="currentColor">6.1</text><text x="262" y="83" fill="currentColor" text-anchor="end">fetch_add relaxed — ldadd</text><rect x="270" y="70" width="144.0" height="18" class="f-ok" stroke="currentColor"/>
<text x="420.0" y="83" fill="currentColor">6.0</text><text x="262" y="107" fill="currentColor" text-anchor="end">fetch_add acquire — ldadda</text><rect x="270" y="94" width="252.0" height="18" class="f-warn" stroke="currentColor"/>
<text x="528.0" y="107" fill="currentColor">10.5</text><text x="262" y="131" fill="currentColor" text-anchor="end">fetch_add release — ldaddl</text><rect x="270" y="118" width="146.4" height="18" class="f-ok" stroke="currentColor"/>
<text x="422.4" y="131" fill="currentColor">6.1</text><text x="262" y="155" fill="currentColor" text-anchor="end">fetch_add acq_rel/seq_cst — ldaddal</text><rect x="270" y="142" width="146.4" height="18" class="f-ok" stroke="currentColor"/>
<text x="422.4" y="155" fill="currentColor">6.1</text><text x="262" y="179" fill="currentColor" text-anchor="end">결과 버림 ldadd / stadd</text><rect x="270" y="166" width="146.4" height="18" class="f-ok" stroke="currentColor"/>
<text x="422.4" y="179" fill="currentColor">6.1</text><text x="262" y="203" fill="currentColor" text-anchor="end">ldadd 4개 라인 번갈아</text><rect x="270" y="190" width="60.0" height="18" class="f-acc" stroke="currentColor"/>
<text x="336.0" y="203" fill="currentColor">2.5</text><text x="262" y="227" fill="currentColor" text-anchor="end">CAS 루프 relaxed — cas</text><rect x="270" y="214" width="146.4" height="18" class="f-ok" stroke="currentColor"/>
<text x="422.4" y="227" fill="currentColor">6.1</text><text x="262" y="251" fill="currentColor" text-anchor="end">CAS 루프 seq_cst — casal</text><rect x="270" y="238" width="163.2" height="18" class="f-ok" stroke="currentColor"/>
<text x="439.2" y="251" fill="currentColor">6.8</text><text x="262" y="275" fill="currentColor" text-anchor="end">LL/SC ldxr/stxr</text><rect x="270" y="262" width="252.0" height="18" class="f-warn" stroke="currentColor"/>
<text x="528.0" y="275" fill="currentColor">10.5</text><text x="262" y="299" fill="currentColor" text-anchor="end">LL/SC ldaxr/stlxr</text><rect x="270" y="286" width="415.2" height="18" class="f-warn" stroke="currentColor"/>
<text x="691.2" y="299" fill="currentColor">17.3</text><text x="262" y="323" fill="currentColor" text-anchor="end">load 체인 ldr / ldapr / ldar</text><rect x="270" y="310" width="74.4" height="18" class="f-mut" stroke="currentColor"/>
<text x="350.4" y="323" fill="currentColor">3.1</text><text x="262" y="347" fill="currentColor" text-anchor="end">store str / stlr</text><rect x="270" y="334" width="24.0" height="18" class="f-mut" stroke="currentColor"/>
<text x="300.0" y="347" fill="currentColor">1.0</text><path d="M270 40 L270 358" stroke="currentColor" stroke-opacity="0.2" fill="none"/><text x="270" y="372" fill="currentColor" text-anchor="middle">0</text>
<path d="M390 40 L390 358" stroke="currentColor" stroke-opacity="0.2" fill="none"/><text x="390" y="372" fill="currentColor" text-anchor="middle">5</text><path d="M510 40 L510 358" stroke="currentColor" stroke-opacity="0.2" fill="none"/>
<text x="510" y="372" fill="currentColor" text-anchor="middle">10</text><path d="M630 40 L630 358" stroke="currentColor" stroke-opacity="0.2" fill="none"/><text x="630" y="372" fill="currentColor" text-anchor="middle">15</text>
<text x="270" y="390" fill="currentColor">사이클 ≈ ns ÷ 0.226 (레지스터 체인 3 연산 = 0.68 ns 로 환산, 약 4.4 GHz 추정)</text>
</svg>
```

| 측정 | 명령 | ns/op 중앙값 [3회 범위] | 사이클(추정) | ldadd 대비 |
|---|---|---|---|---|
| 레지스터 체인 (보정용) | `lsr + add + add` | 0.68 [0.67–0.70] | 3.0 | 0.50 |
| plain add (메모리) | `ldr / add / str` | 1.38 [1.25–1.44] | 6.1 | 1.01 |
| fetch_add relaxed | `ldadd` | 1.36 [1.36–1.40] | 6.0 | **1.00** |
| fetch_add acquire | `ldadda` | 2.38 [2.35–2.41] | 10.5 | **1.74** |
| fetch_add release | `ldaddl` | 1.37 [1.36–1.39] | 6.1 | 1.01 |
| fetch_add acq_rel | `ldaddal` | 1.37 [1.36–1.37] | 6.1 | 1.00 |
| fetch_add seq_cst | `ldaddal` | 1.37 [1.36–1.39] | 6.1 | 1.00 |
| fetch_add relaxed / seq_cst, 결과 버림 | `ldadd` / `ldaddal` (체인 없음) | 1.38 [1.36–1.38] / 1.38 [1.37–1.43] | 6.1 | 1.01 |
| 결과 버림, 서로 다른 라인 4개 번갈아 | `ldadd` ×4 주소 | 0.56 [0.56–0.57] | 2.5 | 0.41 |
| asm `stadd` / 라인 4개 번갈아 | `stadd` | 1.38 [1.38–1.41] / 0.56 [0.56–0.57] | 6.1 / 2.5 | 1.01 / 0.41 |
| CAS 루프 relaxed | `ldr + cas` | 1.37 [1.36–1.40] | 6.1 | 1.00 |
| CAS 루프 seq_cst | `ldr + casal` | 1.52 [1.50–1.63] | 6.8 | 1.12 |
| LL/SC | `ldxr/add/stxr/cbnz` | 2.36 [2.31–2.39] | 10.5 | **1.73** |
| LL/SC acq/rel | `ldaxr/add/stlxr/cbnz` | 3.91 [3.89–3.92] | 17.3 | **2.86** |
| load 체인, 무작위 링 | `ldr` / `ldapr` / `ldar` | 0.70 / 0.71 / 0.69 | 3.1 | 0.51 |
| load 체인, 자기 포인터 | `ldr` / `ldapr` / `ldar` | **0.23** / 0.69 / 0.71 | 1.0 / 3.1 | — |
| store 연속 | `str` / `stlr` / `stlr` | 0.23 / 0.23 / 0.23 | 1.0 | 0.17 |
| store 후 다른 주소 load | `str;ldr` / `stlr;ldapr` / `stlr;ldar` | 0.24 / 0.30 / 0.30 | 1.0 / 1.3 / 1.3 | — |

읽는 법:

1. **경합이 없으면 원자 RMW 는 plain `ldr/add/str` 와 거의 같다** (둘 다 약 6 사이클). "atomic 은 느리다" 는 단일 스레드에서는 이 맥에서 틀린 말이다. 비싼 것은 다음 실험의 경합이다.
2. **memory_order 는 대부분 공짜였다.** `ldadd` · `ldaddl` · `ldaddal` 이 같고, release store 와 seq_cst store 는 **같은 명령** (`stlr`) 이며 연속 store 에서 `str` 과도 같다. seq_cst load(`ldar`)와 acquire load(`ldapr`)도 체인에서 같다. C-A03 에서 본 "seq_cst 가 필요한 store→load 순서" 를 `stlr ; ldar` 로 재도 `stlr ; ldapr` 과 같은 1.3 사이클 — 단일 스레드에서는 비용이 보이지 않았다 (다른 코어와 주고받는 상황의 비용은 이 실험으로 알 수 없다).
3. **예외 하나: `ldadda`(acquire 만) 가 `ldaddal`(acq_rel) 보다 1.7배 느렸다.** 3번 실행 모두 같았다. 더 약한 순서가 더 느린 이유는 확인하지 못했다 (마이크로아키텍처 구현 사정으로 보이며 다른 칩에서는 다를 수 있다). 교훈은 "약한 순서 = 빠름" 이라고 가정하지 말고 **재라** 는 것.
4. **LL/SC 는 LSE 보다 1.7배, acquire/release 를 붙인 LL/SC 는 2.9배.** 명령이 4개이고 `ldxr → stxr` 이 의존 관계로 이어진다.
5. **같은 주소에 대한 RMW 는 결과를 안 써도 6 사이클씩 줄 선다.** 결과를 버린 `ldadd`, `stadd` 도 6.1 사이클. 그런데 **서로 다른 라인 4개** 를 번갈아 두드리면 2.5 사이클로 겹쳐 실행된다. 즉 비용의 대부분은 명령 자체가 아니라 "같은 주소는 앞의 RMW 가 끝나야 다음을 할 수 있다" 는 직렬화다. 배치와 스레드별 칸이 효과적인 이유와 같은 뿌리다.
6. `stadd` 는 `ldadd` 의 목적 레지스터를 XZR 로 둔 별칭이다 (Arm ARM DDI 0487 의 LDADD/STADD 설명 — 목적 레지스터가 WZR/XZR 이면 `LDADDA/LDADDAL` 도 acquire 의미를 갖지 않는다고 적혀 있어서 `STADDA` 는 없다, 문서 기준). clang 은 결과를 버려도 `ldadd x9, x10, [x8]` 처럼 일반 레지스터를 받았다. 이 맥에서 둘의 단일 스레드 비용은 같았다.

#### 실험 2 — 경합 처리량, 1~8 스레드 (`demo_bench.c`)

T 개 스레드를 다 만든 뒤 출발 신호(acquire/release 플래그)로 동시에 시작시키고, 전부 join 할 때까지의 벽시계 시간. 스레드당 2백만 증가(스레드별 칸은 너무 빨라서 2천만), 워밍업 1 + 5회 중앙값, 실행 3번. 매 실행 끝에 합계가 T×ops 인지 확인하고 틀리면 중단한다. CAS · LL/SC 는 실패해서 다시 한 횟수(재시도/op)도 센다. 실행 시작 load average 4.77 → 5.98 → 8.80 (실행 중 다른 벤치마크가 올라왔다).

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="70" y="24" fill="currentColor" font-size="13" font-weight="bold">실험 2 — 같은 캐시 라인 하나를 T 스레드가 증가 (Mops/s, 로그 축)</text><path d="M70 40 L70 270 L500 270" stroke="currentColor" fill="none"/><path d="M70 270.0 L500 270.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="274.0" fill="currentColor" text-anchor="end">10</text><path d="M70 233.4 L500 233.4" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="237.4" fill="currentColor" text-anchor="end">30</text><path d="M70 193.3 L500 193.3" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="197.3" fill="currentColor" text-anchor="end">100</text><path d="M70 156.8 L500 156.8" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="160.8" fill="currentColor" text-anchor="end">300</text>
<path d="M70 116.7 L500 116.7" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="120.7" fill="currentColor" text-anchor="end">1000</text><path d="M70 80.1 L500 80.1" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="84.1" fill="currentColor" text-anchor="end">3000</text><path d="M70 40.0 L500 40.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="44.0" fill="currentColor" text-anchor="end">10000</text><text x="70.0" y="286" fill="currentColor" text-anchor="middle">1</text><text x="131.4" y="286" fill="currentColor" text-anchor="middle">2</text><text x="192.9" y="286" fill="currentColor" text-anchor="middle">3</text>
<text x="254.3" y="286" fill="currentColor" text-anchor="middle">4</text><text x="315.7" y="286" fill="currentColor" text-anchor="middle">5</text><text x="377.1" y="286" fill="currentColor" text-anchor="middle">6</text><text x="438.6" y="286" fill="currentColor" text-anchor="middle">7</text><text x="500.0" y="286" fill="currentColor" text-anchor="middle">8</text><text x="285.0" y="304" fill="currentColor" text-anchor="middle">스레드 수 T</text>
<path d="M70.0 130.5 L131.4 170.3 L192.9 172.8 L254.3 174.4 L315.7 173.5 L377.1 174.4 L438.6 175.3 L500.0 175.9" stroke="currentColor" fill="none" stroke-width="1.5"/><circle cx="70.0" cy="130.5" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="131.4" cy="170.3" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="192.9" cy="172.8" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="254.3" cy="174.4" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="315.7" cy="173.5" r="3.5" class="f-acc" stroke="currentColor"/>
<circle cx="377.1" cy="174.4" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="438.6" cy="175.3" r="3.5" class="f-acc" stroke="currentColor"/><circle cx="500.0" cy="175.9" r="3.5" class="f-acc" stroke="currentColor"/><path d="M70.0 127.8 L131.4 179.9 L192.9 182.7 L254.3 182.8 L315.7 183.3 L377.1 183.1 L438.6 183.2 L500.0 183.1" stroke="currentColor" fill="none" stroke-width="1.5" stroke-dasharray="6 4"/><rect x="66.5" y="124.3" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="127.9" y="176.4" width="7" height="7" class="f-warn" stroke="currentColor"/>
<rect x="189.4" y="179.2" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="250.8" y="179.3" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="312.2" y="179.8" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="373.6" y="179.6" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="435.1" y="179.7" width="7" height="7" class="f-warn" stroke="currentColor"/><rect x="496.5" y="179.6" width="7" height="7" class="f-warn" stroke="currentColor"/>
<path d="M70.0 150.7 L131.4 170.0 L192.9 175.9 L254.3 183.9 L315.7 191.1 L377.1 194.2 L438.6 197.4 L500.0 195.3" stroke="currentColor" fill="none" stroke-width="1.5" stroke-dasharray="2 3"/><path d="M70.0 146.2 L74.5 154.2 L65.5 154.2 Z" class="f-ok" stroke="currentColor"/><path d="M131.4 165.5 L135.9 173.5 L126.9 173.5 Z" class="f-ok" stroke="currentColor"/><path d="M192.9 171.4 L197.4 179.4 L188.4 179.4 Z" class="f-ok" stroke="currentColor"/><path d="M254.3 179.4 L258.8 187.4 L249.8 187.4 Z" class="f-ok" stroke="currentColor"/><path d="M315.7 186.6 L320.2 194.6 L311.2 194.6 Z" class="f-ok" stroke="currentColor"/>
<path d="M377.1 189.7 L381.6 197.7 L372.6 197.7 Z" class="f-ok" stroke="currentColor"/><path d="M438.6 192.9 L443.1 200.9 L434.1 200.9 Z" class="f-ok" stroke="currentColor"/><path d="M500.0 190.8 L504.5 198.8 L495.5 198.8 Z" class="f-ok" stroke="currentColor"/><path d="M70.0 128.9 L131.4 204.9 L192.9 229.2 L254.3 228.1 L315.7 234.0 L377.1 237.1 L438.6 236.0 L500.0 241.0" stroke="currentColor" fill="none" stroke-width="1.5" stroke-dasharray="10 3 2 3"/><path d="M70.0 124.4 L74.5 128.9 L70.0 133.4 L65.5 128.9 Z" class="f-mut" stroke="currentColor"/><path d="M131.4 200.4 L135.9 204.9 L131.4 209.4 L126.9 204.9 Z" class="f-mut" stroke="currentColor"/>
<path d="M192.9 224.7 L197.4 229.2 L192.9 233.7 L188.4 229.2 Z" class="f-mut" stroke="currentColor"/><path d="M254.3 223.6 L258.8 228.1 L254.3 232.6 L249.8 228.1 Z" class="f-mut" stroke="currentColor"/><path d="M315.7 229.5 L320.2 234.0 L315.7 238.5 L311.2 234.0 Z" class="f-mut" stroke="currentColor"/><path d="M377.1 232.6 L381.6 237.1 L377.1 241.6 L372.6 237.1 Z" class="f-mut" stroke="currentColor"/><path d="M438.6 231.5 L443.1 236.0 L438.6 240.5 L434.1 236.0 Z" class="f-mut" stroke="currentColor"/><path d="M500.0 236.5 L504.5 241.0 L500.0 245.5 L495.5 241.0 Z" class="f-mut" stroke="currentColor"/>
<path d="M70.0 103.0 L131.4 86.8 L192.9 74.8 L254.3 66.9 L315.7 61.6 L377.1 58.1 L438.6 57.4 L500.0 52.9" stroke="currentColor" fill="none" stroke-width="2.5"/><circle cx="70.0" cy="103.0" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="131.4" cy="86.8" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="192.9" cy="74.8" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="254.3" cy="66.9" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="315.7" cy="61.6" r="3.5" class="f-ok" stroke="currentColor"/>
<circle cx="377.1" cy="58.1" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="438.6" cy="57.4" r="3.5" class="f-ok" stroke="currentColor"/><circle cx="500.0" cy="52.9" r="3.5" class="f-ok" stroke="currentColor"/><path d="M520 60 L556 60" stroke="currentColor" stroke-width="1.5"/><circle cx="538" cy="60" r="3.5" class="f-acc" stroke="currentColor"/><text x="562" y="64" fill="currentColor">fetch_add relaxed (ldadd)</text>
<path d="M520 84 L556 84" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 4"/><rect x="534.5" y="80.5" width="7" height="7" class="f-warn" stroke="currentColor"/><text x="562" y="88" fill="currentColor">fetch_add seq_cst (ldaddal)</text><path d="M520 108 L556 108" stroke="currentColor" stroke-width="1.5" stroke-dasharray="2 3"/><path d="M538 103.5 L542.5 111.5 L533.5 111.5 Z" class="f-ok" stroke="currentColor"/><text x="562" y="112" fill="currentColor">LL/SC (ldxr/stxr)</text>
<path d="M520 132 L556 132" stroke="currentColor" stroke-width="1.5" stroke-dasharray="10 3 2 3"/><path d="M538 127.5 L542.5 132 L538 136.5 L533.5 132 Z" class="f-mut" stroke="currentColor"/><text x="562" y="136" fill="currentColor">CAS 루프 (cas)</text><path d="M520 156 L556 156" stroke="currentColor" stroke-width="1.5"/><circle cx="538" cy="156" r="3.5" class="f-ok" stroke="currentColor"/><text x="562" y="160" fill="currentColor">스레드별 칸 (경합 없음)</text>
<text x="520" y="200" fill="currentColor">Apple M6, 3회 실행 × 5회 중앙값</text><text x="520" y="218" fill="currentColor">T=1 → 2 에서 공유 3줄은 급락,</text><text x="520" y="236" fill="currentColor">CAS 루프는 T≥3 에서 ldadd 의</text><text x="520" y="254" fill="currentColor">약 1/5~1/7.</text>
</svg>
```

| T | ldadd relaxed | ldaddal seq_cst | LL/SC (재시도/op) | CAS 루프 (재시도/op) | 스레드별 칸 |
|---|---|---|---|---|---|
| 1 | 661 [584–697] | 716 [511–721] | 360 [275–442] (0.00) | 692 [644–699] (0.00) | 1508 [1171–1675] |
| 2 | 200 | 150 | 202 (0.07) | 71 (0.35) | 2451 |
| 3 | 185 | 138 | 169 (0.16) | 34 (1.07) | 3518 |
| 4 | 177 | 137 | 133 (0.20) | 35 (1.19) | 4461 |
| 5 | 182 | 135 | 107 (0.24) | 30 (1.55) | 5227 |
| 6 | 177 | 136 | 97 (0.34) | 27 (1.56) | 5804 |
| 7 | 172 | 136 | 88 (0.39) | 28 (1.63) | 5928 |
| 8 | 169 [165–169] | 136 [135–144] | 94 [58–97] (0.41) | 24 [24–27] (1.89) | 6786 [4554–6914] |

(단위 Mops/s = 모든 스레드 합계 초당 백만 증가. 3번 실행의 중앙값 [3회 범위]. 재시도/op 는 실행별 중앙값의 중앙값.)

읽는 법:

1. **T=1 → 2 에서 공유 카운터는 크게 떨어지고(`ldadd` 3.3배, `ldaddal` 4.8배, CAS 루프 9.7배), 그 뒤는 거의 평평하다.** (LL/SC 는 1 스레드부터 느려서 1.8배.) 한 시점에 한 코어만 라인을 쓸 수 있으니 총 처리량은 코어 수가 아니라 "라인이 오가는 속도" 로 정해진다 (OS-B05 실험 1 과 같은 모양, 일관성 프로토콜은 MEM-M04 · ARCH-A04).
2. **경합에서는 memory_order 가 보인다.** `ldaddal`(seq_cst)은 `ldadd`(relaxed)보다 T≥2 에서 약 20% 느렸다 (136 vs 169~185). 단일 스레드에서 0 이던 차이가 경합에서는 생긴다 — 순서 보장 명령은 앞뒤 접근이 정리될 때까지 기다릴 일이 생기고, 라인이 없는 상태에서 그 기다림이 길어지는 것으로 보인다 (원인 해석은 추정).
3. **CAS 루프는 무너진다.** T≥3 에서 `ldadd` 의 1/5~1/7 (8 스레드 24 vs 169). 재시도가 연산당 1.9번 — 성공 한 번에 평균 2.9번 시도. `ldr` 로 읽은 뒤 `cas` 로 쓰기까지 사이에 다른 코어가 라인을 가져가면 실패하고, 실패한 `cas` 도 라인을 한 번 가져와야 한다. `ldadd` 는 읽기-더하기-쓰기를 라인을 한 번 잡은 채로 끝내므로 **실패라는 개념이 없다.**
4. **LL/SC 는 그 중간.** 재시도가 T 와 함께 늘어(0.07 → 0.41/op) 8 스레드에서 `ldadd` 의 약 55%. 모니터가 깨지면 `stxr` 이 실패하는 구조라 경합이 심할수록 낭비가 커진다. LSE 가 ARMv8.1 에 들어온 이유가 바로 이것이다 (대규모 시스템의 경합 원자 연산 — "Large System Extensions"). LL/SC 와 LSE 의 경합 비교, `wfe` 스핀은 ARM-M04 가 더 깊게 다룬다.
5. **스레드별 칸은 스레드 수에 따라 늘어난다** (8 스레드에서 공유 `ldadd` 의 약 40배). 공유 라인이 없으면 경합도 없다. 8배가 안 되는 이유는 OS-B05 와 같다 (E 코어, 코어 고정 불가).

### Part B — hot path 에서 원자 연산을 줄이는 기법

| 기법 | 핵심 | 대가 |
|---|---|---|
| 스레드별(코어별) 칸 + 합산 | 공유 쓰기를 없앤다. 칸은 캐시 라인마다 (`_Alignas(128)`, false sharing 은 see: ARCH-M08) | 읽을 때 칸 수만큼 load. 읽는 순간의 합은 근사값 |
| 배치 | 로컬에 N 개 모아 `fetch_add(N)` 한 번 | 공유 값이 최대 T×(N−1) 만큼 늦다. 종료 시 flush 필수 |
| 필요한 만큼만 relaxed | 개수 세기는 순서가 필요 없다 → relaxed (C-A03) | 이 값을 "다른 데이터가 준비됐다" 신호로 쓰면 안 된다 |
| CAS 루프 대신 전용 RMW | 더하기 = `fetch_add`, 비트 켜기 = `fetch_or`, 끄기 = `fetch_and`, 교환 = `exchange` | 최대/최소/포화처럼 전용 RMW 가 없는 연산만 CAS (C11 엔 `fetch_max` 가 없다 — 연습 1 의 `ldumax`) |
| 먼저 읽고 필요할 때만 RMW | 이미 켜진 비트, 최대가 아닌 값이면 **쓰지 않는다** (TTAS 와 같은 생각, 스핀락 쪽은 OS-M07) | 쓸 일이 거의 항상 있으면 load 한 번이 추가 비용 |

`batch_add` / `batch_flush` / `flags_set` 은 위 표 그대로다 (§6). `flags_set` 의 load 와 fetch_or 사이에 다른 스레드가 같은 비트를 켤 수 있다. 그래도 틀리지 않는다 — OR 는 몇 번 해도 결과가 같다(멱등). 그래서 이 함수에는 CAS 가 필요 없다. 테스트는 8 스레드가 각자 자기 비트를 1000번 켜도 RMW 는 **스레드마다 정확히 1번** 인지 본다 (자기 비트는 자기만 켜니까 결정적이다).

#### 실험 3 — 기법별 효과 (`demo_hotpath.c`, 8 스레드)

모든 변형이 이벤트마다 같은 "일"(xorshift 한 번)을 하고, 매 실행 결과(합계 = 8×2백만, 플래그 = 0xFF, 최대값 = 스레드별 최대의 최대)를 확인한다. 실행 3번 × 5회 중앙값, 시작 load average 10.95 / 11.35 / 11.08.

```svg
<svg viewBox="0 0 740 342" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="22" fill="currentColor" font-size="13" font-weight="bold">hot path 기법 — 8 스레드, 백만 이벤트/초 (로그 축, 3회 실행 중앙값)</text><text x="242" y="60" fill="currentColor" text-anchor="end">공유 fetch_add (relaxed)</text><rect x="250" y="48" width="180.4" height="17" class="f-warn" stroke="currentColor"/>
<text x="436.4" y="60" fill="currentColor">170</text><text x="242" y="83" fill="currentColor" text-anchor="end">공유 fetch_add (seq_cst)</text><rect x="250" y="71" width="168.2" height="17" class="f-warn" stroke="currentColor"/>
<text x="424.2" y="83" fill="currentColor">140</text><text x="242" y="106" fill="currentColor" text-anchor="end">배치 N=16</text><rect x="250" y="94" width="353.7" height="17" class="f-ok" stroke="currentColor"/>
<text x="609.7" y="106" fill="currentColor">2580</text><text x="242" y="129" fill="currentColor" text-anchor="end">배치 N=256</text><rect x="250" y="117" width="428.1" height="17" class="f-ok" stroke="currentColor"/>
<text x="684.1" y="129" fill="currentColor">8294</text><text x="242" y="152" fill="currentColor" text-anchor="end">스레드별 칸 + 합산</text><rect x="250" y="140" width="406.5" height="17" class="f-ok" stroke="currentColor"/>
<text x="662.5" y="152" fill="currentColor">5910</text><text x="242" y="183" fill="currentColor" text-anchor="end">OR 를 CAS 루프로</text><rect x="250" y="171" width="119.2" height="17" class="f-warn" stroke="currentColor"/>
<text x="375.2" y="183" fill="currentColor">65</text><text x="242" y="206" fill="currentColor" text-anchor="end">fetch_or (ldset)</text><rect x="250" y="194" width="180.6" height="17" class="f-mut" stroke="currentColor"/>
<text x="436.6" y="206" fill="currentColor">170</text><text x="242" y="229" fill="currentColor" text-anchor="end">먼저 읽고 없을 때만 fetch_or</text><rect x="250" y="217" width="404.1" height="17" class="f-ok" stroke="currentColor"/>
<text x="660.1" y="229" fill="currentColor">5690</text><text x="242" y="260" fill="currentColor" text-anchor="end">최대값: 매번 CAS</text><rect x="250" y="248" width="121.2" height="17" class="f-warn" stroke="currentColor"/>
<text x="377.2" y="260" fill="currentColor">67</text><text x="242" y="283" fill="currentColor" text-anchor="end">최대값: 클 때만 CAS</text><rect x="250" y="271" width="404.3" height="17" class="f-ok" stroke="currentColor"/>
<text x="660.3" y="283" fill="currentColor">5713</text><path d="M250.0 42 L250.0 294" stroke="currentColor" stroke-opacity="0.2" fill="none"/><text x="250.0" y="308" fill="currentColor" text-anchor="middle">10</text><path d="M396.7 42 L396.7 294" stroke="currentColor" stroke-opacity="0.2" fill="none"/><text x="396.7" y="308" fill="currentColor" text-anchor="middle">100</text>
<path d="M543.3 42 L543.3 294" stroke="currentColor" stroke-opacity="0.2" fill="none"/><text x="543.3" y="308" fill="currentColor" text-anchor="middle">1000</text><path d="M690.0 42 L690.0 294" stroke="currentColor" stroke-opacity="0.2" fill="none"/><text x="690.0" y="308" fill="currentColor" text-anchor="middle">10000</text><text x="250" y="328" fill="currentColor">주황 = 이벤트마다 공유 라인에 쓰기, 초록 = 공유 쓰기를 줄인 버전</text>
</svg>
```

| 변형 | Mevents/s [3회 범위] | 기준 대비 |
|---|---|---|
| H1 공유 `fetch_add` relaxed (기준) | 170 [167–175] | ×1.00 |
| H1 공유 `fetch_add` seq_cst | 140 [130–140] | ×0.83 |
| H1 배치 N=16 | 2580 [2554–2727] | ×15.2 |
| H1 배치 N=256 | 8294 [8208–9456] | ×48.8 |
| H1 스레드별 칸 + 합산 | 5910 [5330–5974] | ×34.8 |
| H2 OR 를 CAS 루프로 (기준) | 65 [65–65] | ×1.00 |
| H2 `fetch_or` (`ldset`) | 170 [170–172] | ×2.62 |
| H2 먼저 읽고 없을 때만 `fetch_or` | 5690 [5676–5714] | ×87.6 |
| H3 최대값: 매번 CAS (기준) | 67 [67–73] | ×1.00 |
| H3 최대값: 클 때만 CAS | 5713 [5266–5716] | ×85.3 |

읽는 법:

1. **배치 N=16 이 15배, N=256 이 49배.** 공유 RMW 가 1/16, 1/256 로 줄면 거의 그만큼 빨라진다. N=256 이 스레드별 칸보다도 빠른 이유: 칸 방식은 이벤트마다 자기 칸에 `ldr/add/str` 로 메모리를 쓰지만, 배치는 레지스터 안에서 더하다가 256번에 한 번만 메모리에 간다.
2. **CAS 로 OR 를 하면 `fetch_or` 보다 2.6배 느리다.** 같은 일을 하는 전용 RMW(`ldset`)가 있으면 그것을 쓴다 — 실험 2 의 CAS 붕괴와 같은 이유.
3. **"먼저 읽기" 가 가장 큰 차이(85~88배)를 냈다.** 플래그가 한번 다 켜지면 그 뒤로는 아무도 쓰지 않는다. 모두가 읽기만 하는 라인은 모든 코어의 캐시에 **공유(Shared)** 상태로 머물러 L1 히트로 끝난다. `fetch_or` 는 이미 켜진 비트를 다시 켜는 것이라도 **쓰기** 이므로 매번 라인 독점권을 가져와야 한다 (값이 안 바뀌어도 RMW 는 RMW 다). 최대값 워터마크도 같다: 값이 커질 일은 처음 잠깐뿐이라 대부분의 이벤트가 load 하나로 끝난다.
4. seq_cst 로 바꾸면 17% 느려졌다 (실험 2 의 20% 와 같은 현상). 통계 카운터에는 relaxed 로 충분하다.

### Part C — 듀얼 코어 패킷 경로 텔레메트리

```c
static int tel_on_packet(tel_t *t, int core, uint32_t len, uint32_t lat, uint32_t err_bits) {
    int rmw = 0;
    bump(&t->core[core].pkts, 1);            /* 코어별 칸: writer 하나 → ldr/add/str, RMW 아님 */
    bump(&t->core[core].bytes, len);
    if (err_bits && flags_set(&t->err, err_bits)) rmw++;    /* 대부분 이미 켜져 있음 → 읽기만 */
    if (wm_update(&t->max_lat, lat)) rmw++;                 /* 대부분 최대가 아님 → 읽기만 */
    return rmw;
}
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="22" fill="currentColor" font-weight="bold">패킷 1개당 공유 라인 접근 — 기본 설계 vs 이 레슨의 설계</text><rect x="10" y="36" width="350" height="200" rx="6" class="f-mut" stroke="currentColor"/><text x="22" y="58" fill="currentColor" font-weight="bold">기본: 전부 공유 RMW</text>
<text x="22" y="84" fill="currentColor" font-family="monospace" font-size="12">fetch_add(&amp;pkts, 1)        ← 쓰기</text><text x="22" y="104" fill="currentColor" font-family="monospace" font-size="12">fetch_add(&amp;bytes, len)     ← 쓰기</text><text x="22" y="124" fill="currentColor" font-family="monospace" font-size="12">fetch_or(&amp;err, bits)       ← 쓰기</text>
<text x="22" y="144" fill="currentColor" font-family="monospace" font-size="12">CAS 루프(&amp;max_lat)        ← 쓰기</text><rect x="22" y="162" width="326" height="58" rx="4" class="f-warn" stroke="currentColor"/><text x="32" y="184" fill="currentColor">패킷마다 공유 라인 쓰기 4번</text>
<text x="32" y="206" fill="currentColor">두 코어가 라인을 계속 뺏고 뺏긴다</text><rect x="380" y="36" width="350" height="200" rx="6" class="f-mut" stroke="currentColor"/><text x="392" y="58" fill="currentColor" font-weight="bold">이 레슨: 공유 쓰기는 "바뀔 때만"</text>
<text x="392" y="84" fill="currentColor" font-family="monospace" font-size="12">core[c].pkts / bytes  ← 내 라인</text><text x="392" y="104" fill="currentColor" font-family="monospace" font-size="12">load(err) 다 켜짐?    ← 읽기만</text><text x="392" y="124" fill="currentColor" font-family="monospace" font-size="12">load(max) 보다 작음?  ← 읽기만</text>
<text x="392" y="144" fill="currentColor" font-family="monospace" font-size="12">(드물게) ldset / cas</text><rect x="392" y="162" width="326" height="58" rx="4" class="f-ok" stroke="currentColor"/><text x="402" y="184" fill="currentColor">평소 공유 쓰기 0번 — 공유 라인은</text>
<text x="402" y="206" fill="currentColor">두 코어 캐시에 Shared 로 머문다</text>
</svg>
```

`wm_update` 는 `while (v > cur)` 안에서만 CAS 한다 — CAS 가 실패하면 `cur` 가 최신값으로 바뀌고, 그래도 v 가 크면 다시, 아니면 쓰지 않고 끝난다. 최대값은 `fetch_add` / `fetch_or` 같은 전용 C11 RMW 가 없어서 CAS 가 필요한 경우다. 그래도 "먼저 읽기" 로 대부분의 CAS 를 없앤다. arm64 에는 `ldumax` 라는 LSE 명령이 있다 (연습 1).

이 설계가 실제 칩에서 내려가는 명령: 리눅스가 도는 Cortex-A53 SoC(ARMv8.0)라면 `flags_set` 의 `fetch_or` 는 `ldxr/orr/stxr` 루프(또는 outline 헬퍼), A55(ARMv8.2)라면 `ldset` 한 명령 — 위 표의 실측과 같은 규칙. Cortex-M4 (단일 코어 MCU, 텔레메트리를 ISR 이 갱신하는 경우)라면 relaxed `fetch_add` 는 `LDREX/STREX` 루프에 `DMB` 없음, seq_cst 는 앞뒤에 `dmb sy` 가 붙는다 (C-A03 `demo_asm.c` 실측). Cortex-M 에는 다른 코어가 없으니 경합 비용은 "ISR 이 LDREX 와 STREX 사이에 끼어 STREX 가 한 번 실패" 정도이고(ARM-M11), M0/M0+ 는 원자 RMW 명령이 아예 없어 PRIMASK 임계 구역이 된다(CPP-A04). 단일 writer 칸(`bump`)은 어느 칩에서도 일반 `ldr/add/str` 이다 (OS-B05 실측).

테스트는 스크립트로 결정적인 것만 본다: 패킷 4개에서 공유 RMW 가 1, 0, 1, 1 번, 합계·OR·최대값. 두 스레드 스트레스는 코어별 칸 합계 == 2×200000, 바이트 합 == 스레드가 따로 센 합, 오류 비트 == 0b101, 최대 == 각 스레드 최대의 최대.

### TSan 과 인라인 asm

솔루션을 TSan 으로 돌리면 깨끗하다 — 그런데 그것이 asm 부분이 맞다는 증거는 **아니다**. TSan 은 컴파일러가 C 의 메모리 접근마다 넣는 계측 코드로 동작하는데, 인라인 asm 안의 `ldadd` / `ldxr` / `stxr` 는 컴파일러가 해석하지 않으므로 계측되지 않는다. `demo_tsan_asm.c` 로 확인했다 (실측):

```text
$ cc -std=c11 -g -O1 -fsanitize=thread -pthread -DMODE=0 demo_tsan_asm.c -o t0 && ./t0
MODE=0 g=100000 (reader sum 8250796332)                      ← asm ldadd 로 쓰고 C 로 그냥 읽음: 보고 없음, exit 0
$ cc -std=c11 -g -O1 -fsanitize=thread -pthread -DMODE=1 demo_tsan_asm.c -o t1 && ./t1
WARNING: ThreadSanitizer: data race (pid=74206)
  Read of size 8 at 0x000104d78000 by thread T2:
    #0 reader demo_tsan_asm.c:30 (t1:arm64+0x100000798)
  Previous write of size 8 at 0x000104d78000 by thread T1:
    #0 writer demo_tsan_asm.c:23 (t1:arm64+0x100000748)
  …
SUMMARY: ThreadSanitizer: data race demo_tsan_asm.c:30 in reader      ← C 의 g++ 로 바꾸면 잡힌다, exit 134
```

그래서 asm 원자 연산의 정확성은 **스트레스 CHECK** (구현마다 4 스레드 × 200000 → 정확히 800000, 12 스레드가 동시에)로 확인한다. 같은 이유로 실제 코드에서는 asm 보다 C11 atomic 을 쓰는 것이 낫다 — 컴파일러가 같은 `ldadd` 를 내면서 TSan 도 볼 수 있다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution        # 48/48 passed (6번 실행 모두)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_asan && ./so_asan   # 48/48, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan            # 48/48, 보고 없음 (asm 부분은 TSan 이 못 봄)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 것 |
|---|---|---|
| 연산 하나를 `clock_gettime` 두 번 사이에 두고 잰다 | 눈금 41.7 ns, 호출 7 ns 가 1.4 ns 짜리 연산을 덮는다 | 수천만 번 돌린 구간을 재고 나눈다 |
| 한 번 재고 끝 / 평균을 낸다 | 다른 프로세스·주파수 변화로 한두 번이 튄다 | 여러 번 재서 중앙값(+최소/최대), 실행 자체도 여러 번 |
| asm 을 안 보고 숫자를 믿는다 | static 원자 변수의 load 를 clang 이 지웠다 (실측) | `-S` 로 루프 본문 확인 |
| 같은 값만 돌려주는 포인터로 load 지연을 잰다 | 1 사이클 — 예측기를 쟀다 | 무작위 링 (3 사이클) |
| "seq_cst 는 비싸니 다 relaxed 로" | 이 맥 단일 스레드에서 차이는 거의 0, 대신 정확성을 잃을 수 있다 (C-A03 Peterson) | 순서가 필요 없는 **통계** 만 relaxed |
| OR/더하기를 CAS 루프로 | 경합 시 2.6~7배 느림 | `fetch_or` / `fetch_add` |
| 이미 켜진 플래그에 매번 `fetch_or` | 값이 안 바뀌어도 쓰기 → 라인 독점 | 먼저 load, 필요할 때만 RMW |
| 배치 후 종료 flush 를 잊는다 | 합계가 모자란다 (100003 = 16×6250 + 3) | 스레드 끝에서 `batch_flush` |
| LL/SC asm 에 `"=&r"` 없이 `"=r"` | 컴파일러가 출력과 입력에 같은 레지스터를 줄 수 있다 | early-clobber |
| asm 원자 연산을 TSan 으로 검증했다고 생각 | asm 은 계측되지 않는다 (`demo_tsan_asm.c`) | 스트레스 CHECK 로 정확한 합계 |

## 8. 면접 Q&A

<details>
<summary>Q1. seq_cst 는 relaxed 보다 얼마나 비싼가요?</summary>

아키텍처와 경합 여부에 따라 다르다. Apple M6 실측: 경합이 없으면 `fetch_add` 는 relaxed `ldadd` 와 seq_cst `ldaddal` 이 같은 약 6 사이클, release 와 seq_cst store 는 같은 `stlr` 명령이라 차이 0. 8 스레드 경합에서는 seq_cst 가 약 20% 느렸다. ARMv7-M(Cortex-M4)에서는 seq_cst 가 `dmb sy` 를 앞뒤에 붙이고(C-A03 실측), x86 에서는 seq_cst store 가 `xchg`(또는 `mov`+`mfence`)라 store 중 가장 비싸다. 꼬리: "그럼 다 seq_cst 로 쓰면 되나?" → 정확성 면에서는 안전한 기본값이 맞다. hot path 에서는 경합 비용이 순서 비용보다 훨씬 크니, 먼저 공유 쓰기를 줄이고(칸·배치) 그 다음에 순서를 relaxed 로 내린다.
</details>

<details>
<summary>Q2. CAS 루프와 fetch_add, 언제 무엇을 쓰나요? 왜 CAS 가 경합에서 무너지죠?</summary>

전용 RMW 가 있으면(`fetch_add/or/and/xor`, `exchange`) 그것을 쓴다. CAS 는 최대/최소, 포화, 조건부 갱신, 포인터 교체처럼 전용 연산이 없을 때만. CAS 루프는 "읽기 → 계산 → CAS" 로 라인 접근이 둘로 나뉘어, 사이에 다른 코어가 쓰면 실패하고 다시 한다. 실측 8 스레드: CAS 루프 24 Mops/s, 재시도 1.9/op, `ldadd` 169 Mops/s. 단일 스레드에서는 둘 다 약 6 사이클이라 차이가 안 보인다 — **경합 테스트 없이 고르면 틀린다.** 꼬리: "weak vs strong?" → 루프 안이면 weak (LL/SC 칩에서 가짜 실패를 루프가 흡수, strong 은 안쪽에 루프를 하나 더 만든다). 이 맥은 `cas` 한 명령이라 둘이 같다.
</details>

<details>
<summary>Q3. LL/SC 와 LSE 원자 명령의 차이는?</summary>

LL/SC(`ldxr/stxr`, ARMv8.0, RISC-V `lr/sc`)는 읽기와 조건부 쓰기 두 명령이고, 그 사이에 모니터가 깨지면(다른 코어의 쓰기, 예외) 쓰기가 실패해 재시도한다. LSE(ARMv8.1 `ldadd/ldset/cas/swp`, RISC-V `amoadd` 등)는 RMW 전체가 명령 하나라 실패가 없다. 구현이 원자 연산을 캐시나 인터커넥트 쪽에서 처리할 수도 있다(이른바 far atomics — 구현 정의, 이 맥에서는 미확인). 실측: 경합 없을 때 LL/SC 가 1.7배, 8 스레드 경합에서 `ldadd` 의 약 55% + 재시도 0.41/op. 꼬리: "LL/SC 가 더 나은 점은?" → 임의의 RMW(예: 포화, 복잡한 비트 조작)를 한 번의 예약으로 만들 수 있고, 하드웨어가 단순하다. 단, 루프 안에 다른 load/store 를 넣으면 진행 보장이 깨질 수 있다 (RISC-V 의 constrained LR/SC loop 규칙 — RISCV-M04).
</details>

<details>
<summary>Q4. 원자 연산 하나의 비용을 어떻게 재나요? 믿을 수 있는 숫자를 얻는 방법은?</summary>

(1) 타이머 눈금보다 훨씬 긴 구간을 재고 횟수로 나눈다 (이 맥 눈금 41.7 ns, 연산 1.4 ns → 2천만 번). (2) 지연을 원하면 결과가 다음 입력이 되는 의존 체인, 처리량을 원하면 독립 연산. (3) 워밍업 후 여러 번, 중앙값과 범위. (4) `-S` 로 측정 대상이 실제로 남아 있는지 확인 — 나는 static 변수의 seq_cst load 를 clang 이 지운 것을 asm 으로 발견했다. (5) 예측 가능한 패턴 피하기 — 자기 포인터 체인이 1 사이클로 나왔다. (6) 머신 정보와 부하(load average)를 같이 적고, 공유 머신이면 절대값보다 배수로 말한다. 꼬리: "p99 는?" → 연산 하나 단위의 꼬리는 이 방식으로는 못 본다. 요청 단위 지연 분포는 LLAT-B02 의 히스토그램으로.
</details>

<details>
<summary>Q5. 8 코어가 패킷마다 증가시키는 통계 카운터, 어떻게 설계하나요?</summary>

코어(스레드)별 칸을 캐시 라인마다 두고 writer 하나가 relaxed load+store 로 증가, 읽는 쪽이 합산한다 (Linux `percpu_counter`, 커널 네트워크 스택의 per-CPU 통계와 같은 구조). 실측 8 스레드: 공유 `fetch_add` 170 → 칸 5910 Mevents/s (35배). 칸 대신 로컬 배치(N=256)도 49배. 대가는 읽는 순간의 합이 근사값이라는 것과, 읽기 비용이 코어 수에 비례한다는 것. 꼬리: "32비트 MCU 에서 64비트 바이트 카운터는?" → 한 번에 읽히지 않으니 seqcount(LLAT-M02) 또는 재읽기(C-M14). "리셋은?" → reader 쪽 base 값 (OS-B05).
</details>

<details>
<summary>Q6. test-and-test-and-set(TTAS), read-before-CAS 가 왜 빠른가요?</summary>

쓰기(RMW)는 값이 안 바뀌어도 라인 독점권을 가져와야 해서 다른 코어의 복사본을 무효화한다. 읽기는 여러 코어가 Shared 상태로 동시에 가질 수 있어 각자 L1 에서 끝난다. 그래서 "대부분 쓸 필요가 없는" 연산 — 이미 켜진 sticky 플래그, 최대가 아닌 값, 잡혀 있는 락 — 은 먼저 읽고 필요할 때만 RMW 한다. 실측 8 스레드: 플래그 `fetch_or` 170 → 먼저 읽기 5690 (33배), 워터마크 매번 CAS 67 → 5713 (85배). 꼬리: "언제 손해?" → 거의 매번 써야 하는 경우(카운터)에는 load 하나가 추가될 뿐이다. 스핀락에서의 TTAS·백오프는 OS-M07.
</details>

<details>
<summary>Q7. 같은 코드를 Cortex-A53 과 A55, Cortex-M4 에 올리면 원자 연산이 어떻게 달라지나요?</summary>

clang 실측: `-mcpu=cortex-a53`(ARMv8.0)은 `fetch_add` 가 `ldxr/add/stxr/cbnz` 루프, seq_cst 는 `ldaxr/stlxr`, acquire load 도 `ldar`(LRCPC 없음). `-mcpu=cortex-a55`(v8.2)는 `ldadd/ldaddal/ldapr`. `-moutline-atomics` 를 쓰면 `__aarch64_ldadd8_relax` 같은 헬퍼 호출이 되어 런타임에 LSE 유무를 고른다. Cortex-M4 는 `LDREX/STREX` 루프에 seq_cst 면 `dmb sy` 가 붙고(C-A03), 64비트 원자 연산은 lock-free 가 아니다. M0/M0+ 는 exclusive 명령이 없어 라이브러리 호출/인터럽트 끄기다 (CPP-A04). 꼬리: "그럼 A53 에서는 배치가 더 중요?" → 그렇다. LL/SC 는 경합에서 재시도가 늘어나니 공유 RMW 횟수를 줄이는 이득이 더 크다 (이 맥의 asm LL/SC 실측 기준 추론).
</details>

## 9. 추가 연습

### 연습 1 — `ldumax` 로 워터마크를 명령 하나에

C11 에는 `atomic_fetch_max` 가 없다 (C++ 는 C++26 에 `fetch_max` 추가 — 표준 문서 기준). arm64 LSE 의 `ldumax Ws, Wt, [Xn]` (부호 없는 최대)으로 `uint32_t lse_fetch_umax32(uint32_t *p, uint32_t v)` 를 써라. 옛 값을 돌려준다.

<details>
<summary>답 보기</summary>

```c
static uint32_t lse_fetch_umax32(uint32_t *p, uint32_t v) {
    uint32_t old;
    __asm__ volatile("ldumax %w2, %w0, [%1]" : "=r"(old) : "r"(p), "r"(v) : "memory");
    return old;
}
```

32비트 연산이라 `%w` 레지스터. `ldumax` 는 부호 없는 비교이고 `ldsmax` 가 부호 있는 판이다 (테스트: 11 vs 0x80000000 → 0x80000000 이 이긴다). 실패가 없는 한 명령이라 CAS 루프보다 경합에 강하지만, **항상 쓰기** 다 — 값이 최대가 아니어도 라인 독점권을 가져온다. 대부분 최대가 아닌 hot path 라면 Part C 의 "먼저 읽기 + 드물게 CAS" 가 오히려 낫고, `if (load < v) ldumax` 로 둘을 합칠 수 있다. 테스트: 4 스레드 × 10만 개 무작위 값 → 결과 == 스레드별 최대의 최대.

</details>

### 연습 2 — ID 블록 예약

패킷 시퀀스 번호/요청 ID 를 여러 스레드가 받는다. 매번 `fetch_add(1)` 하지 말고, 비면 `fetch_add(64)` 로 64개를 한 번에 예약해 로컬에서 나눠 주는 `id_next(idblk_t *b, _Atomic uint64_t *g)` 를 써라.

<details>
<summary>답 보기</summary>

```c
static uint64_t id_next(idblk_t *b, _Atomic uint64_t *g) {
    if (b->next == b->end) {
        b->next = atomic_fetch_add_explicit(g, BLK, memory_order_relaxed);
        b->end = b->next + BLK;
        b->refills++;
    }
    return b->next++;
}
```

배치의 다른 얼굴이다: 공유 RMW 가 1/64. 대가는 ID 가 **전역적으로 증가 순서가 아니고** 스레드가 끝날 때 쓰지 않은 ID 에 구멍이 생긴다는 것 — 유일성만 필요하면 문제없다 (DB 의 sequence cache, 커널의 `get_next_ino` per-CPU 배치가 같은 방식). 테스트: 4 스레드 × 10000 ID 가 전부 다르고(atomic_exchange 로 표시해 중복 0), 스레드마다 refill 이 정확히 ceil(10000/64) = 157번.

</details>

### 연습 3 — 포화 카운터 (CAS 가 꼭 필요한 경우)

TX 크레딧처럼 상한이 있는 카운터: `*p < max` 면 1 올리고 true, 꽉 찼으면 false. 꽉 찬 뒤에는 RMW 를 하지 않아야 한다.

<details>
<summary>답 보기</summary>

```c
static bool sat_inc(_Atomic uint32_t *p, uint32_t max) {
    uint32_t cur = atomic_load_explicit(p, memory_order_relaxed);
    while (cur < max) {
        if (atomic_compare_exchange_weak_explicit(p, &cur, cur + 1, memory_order_relaxed, memory_order_relaxed))
            return true;
    }
    return false;
}
```

`fetch_add` 후 넘었으면 `fetch_sub` 로 되돌리는 방법도 있지만, 그 사이에 다른 스레드가 일시적으로 max 를 넘은 값을 보게 된다 (크레딧이 음수/초과로 보이는 순간). CAS 는 "검사와 갱신" 을 한 원자 단위로 묶는다. 루프 조건이 먼저 읽은 값으로 검사하므로 꽉 찬 뒤에는 load 만 한다 (Part C 와 같은 패턴). 테스트: 8 스레드 × 10000 시도, max 30000 → 성공 합계 정확히 30000, 최종값 30000.

</details>

### 연습 4 — `stadd` 이벤트 카운터

결과가 필요 없는 원자 덧셈 `void lse_stadd(uint64_t *p, uint64_t v)` 를 `stadd` 로 써라.

<details>
<summary>답 보기</summary>

```c
static void lse_stadd(uint64_t *p, uint64_t v) {
    __asm__ volatile("stadd %x1, [%0]" : : "r"(p), "r"(v) : "memory");
}
```

출력 피연산자가 없다 (값을 돌려주지 않으니까). `volatile` 이 없으면 출력이 없는 asm 이라도 GCC/clang 은 지우지 않지만(출력 없는 asm 은 암묵적으로 volatile), 의도를 분명히 하려고 붙였다. 실험 1 에서 `stadd` 와 결과를 버린 `ldadd` 는 이 맥에서 같은 6.1 사이클이었다 — "값을 안 받으면 더 빠르다" 는 이 칩에서는 보이지 않았다. 테스트: 4 스레드가 각각 1, 2, 3, 4 를 20만 번 → 2000000.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice                         # 20/20 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_asan && ./pr_asan   # 20/20, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan                            # 20/20, 보고 없음
```

## 10. 핵심 정리

- 재는 법: 긴 구간 ÷ 횟수 (눈금 41.7 ns ≫ 연산 1.4 ns), 워밍업, 여러 번 중앙값, 실행도 여러 번, **asm 확인**, 예측 가능한 패턴 피하기, 부하를 적고 배수로 말하기.
- Apple M6 경합 없음: `ldadd` ≈ `ldaddal` ≈ `cas` ≈ plain `ldr/add/str` ≈ 6 사이클. release/seq_cst store 는 같은 `stlr`. 예외 `ldadda` 1.7배(원인 미확인). LL/SC 1.7~2.9배.
- 같은 주소 RMW 는 줄 선다(6 사이클씩), 다른 라인이면 겹친다(2.5 사이클).
- 경합이 진짜 비용: 공유 카운터는 2 스레드부터 1/4~1/5, seq_cst 는 경합에서 +20%, CAS 루프는 `ldadd` 의 1/5~1/7(재시도 1.9/op), LL/SC 는 중간.
- 기법(8 스레드 실측): 배치 N=256 ×49, 스레드별 칸 ×35, CAS→`fetch_or` ×2.6, 먼저 읽고 필요할 때만 RMW ×85.
- 전용 RMW 가 있으면 CAS 루프 금지. CAS 는 최대·포화·조건부 갱신에만, 그것도 read-before-CAS 로.
- 같은 C 코드: A53 → `ldxr/stxr`, A55 → `ldadd`, M4 → `LDREX/STREX`+`DMB`, M0 → 라이브러리/PRIMASK. TSan 은 인라인 asm 을 못 본다.

## 참고자료

- C11 (N1570) 5.1.2.4 Multi-threaded executions and data races, 7.17.3 Order and consistency, 7.17.7.4 The atomic_compare_exchange generic functions (weak 의 가짜 실패), 7.17.7.5 The atomic_fetch and modify generic functions: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Arm Architecture Reference Manual for A-profile (DDI 0487) — A64 명령 설명 LDADD/LDADDA/LDADDAL/LDADDL, STADD/STADDL(별칭), LDSET, LDUMAX, CAS/CASAL, LDXR/STXR, LDAXR/STLXR, LDAR/STLR, LDAPR; 메모리 모델 장의 Load-Acquire/Store-Release 와 "Synchronization and semaphores"(exclusive monitor): <https://developer.arm.com/documentation/ddi0487/latest/>
- Arm, "Learn the architecture — Armv8-A memory model" (acquire/release, LSE 개요): <https://developer.arm.com/documentation/102336/latest/>
- GCC AArch64 Options — `-moutline-atomics` (`__aarch64_*` 헬퍼, 기본값 설명): <https://gcc.gnu.org/onlinedocs/gcc/AArch64-Options.html>
- Apple `clock_gettime_nsec_np`, `mach_timebase_info` (`man clock_gettime_nsec_np`), `pthread/qos.h`
- Paul E. McKenney, *Is Parallel Programming Hard...* — 5장 Counting (per-thread / statistical counters): <https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook.html>
- Linux `include/linux/percpu_counter.h`, `lib/percpu_counter.c` (배치 + per-CPU 칸): <https://elixir.bootlin.com/linux/latest/source/lib/percpu_counter.c>
- Clang ThreadSanitizer 문서 (계측 기반 — asm 은 보지 못함): <https://clang.llvm.org/docs/ThreadSanitizer.html>
- 관련 레슨: C-A02 (컴파일러 배리어), C-A03 (memory_order 의미, arm64/M4 asm), C-M14 (ISR 공유 데이터), OS-B05 (뮤텍스·atomic·sloppy 처리량, 단일 writer 칸), OS-M07 (TAS/TTAS 스핀락), OS-M08 (메시지 패싱·DCLP 패턴), CPP-A04 (Cortex-M lock-free 여부), MCPP-M13 (fence·release sequence), ARM-M04 (LDXR/STXR·LSE 손으로 구현, 경합 비교), ARM-M05 (DMB/DSB/ISB, LDAR/STLR), ARM-M11 (Cortex-M exclusive monitor), RISCV-M04 (LR/SC·AMO), ARCH-M08 (false sharing), MEM-M04 (일관성 트래픽), LLAT-B01~B03 (측정 방법론), LLAT-B02 (지연 분포), LLAT-M01 (SPSC 캐시된 인덱스), LLAT-M02 (seqlock)
