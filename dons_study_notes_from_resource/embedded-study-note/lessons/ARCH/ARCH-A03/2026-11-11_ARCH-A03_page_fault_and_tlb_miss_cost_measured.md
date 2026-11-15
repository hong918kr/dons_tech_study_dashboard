---
id: ARCH-A03
time: 120분
updated: 2026-11-11
---
# ARCH-A03 가상 메모리 실측: 페이지 폴트와 TLB 미스 비용

## 학습 목표

- macOS(Apple M6)의 페이지 크기가 **16 KB** 라는 것을 런타임으로 확인하고(`sysconf` · `getpagesize` · `vm_page_size`), 페이지 수학(올림·바닥·번호)을 오버플로 없이 짠다.
- 새 익명 매핑(`mmap`)을 페이지마다 1 바이트씩 만지며 **첫 접근(폴트) / 두 번째 / 세 번째 접근** 의 페이지당 비용을 `ru_minflt` · 커널 시간 · `mincore` 와 함께 재고, "읽기 먼저 → 공유 제로 페이지 → 쓰기 때 다시 폴트" 같은 리눅스 상식이 이 맥에서 맞는지 **재서** 말한다.
- 포인터 체이스(Sattolo 한-사이클 순열)로 페이지 수 P 를 4 → 65536 까지 늘리며 ns/접근 곡선을 그리고, **캐시 효과와 TLB 효과를 대조군(packed · same 배치)으로 분리** 해 무릎(knee)을 "TLB 커버리지 ≈ P × 16 KB" 로 해석한다.
- 펌웨어·OS 가 이 비용을 다루는 방법(prefault · `mlock` · 큰 페이지)을 이 맥에서 실제로 되는 것과 안 되는 것으로 나눠 말한다.

## 1. 문제

"TLB 미스 하나에 메모리 접근 몇 번?", "왜 Apple 은 16 KB 페이지를 쓰나?", "실시간 루프 첫 반복만 느린 이유는?" — 면접과 실제 칩 팀(성능·검증·드라이버)이 묻는 가상 메모리 질문은 결국 **숫자** 를 요구한다. 이 레슨은 그 숫자를 Apple M6 에서 직접 잰다. 펌웨어 쪽에서도 같다: Cortex-A 위의 리눅스 드라이버가 큰 DMA 버퍼를 처음 만질 때의 지연, 실시간 스레드가 `mlockall` 을 부르는 이유, NPU 런타임이 huge page 를 쓰는 이유가 모두 여기서 나온다.

가상 주소(VA) 하나를 쓰려면 세 가지 중 하나가 일어난다.

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">load/store 한 번의 세 갈래 — 비용은 아래 Part A/B 에서 이 맥으로 잰 값</text>
  <rect x="10" y="40" width="120" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="58" fill="currentColor">CPU: ldr x0,[VA]</text>
  <text x="20" y="76" fill="currentColor">VA → VPN + offset</text>
  <rect x="170" y="40" width="150" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="180" y="58" fill="currentColor">TLB 조회 (L1 → L2 TLB)</text>
  <text x="180" y="76" fill="currentColor">VPN → PFN + 권한</text>
  <rect x="370" y="40" width="160" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="380" y="58" fill="currentColor">① 히트: PA 로 캐시 접근</text>
  <text x="380" y="76" fill="currentColor">추가 비용 ≈ 0</text>
  <rect x="170" y="130" width="150" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="180" y="148" fill="currentColor">② 미스: 하드웨어</text>
  <text x="180" y="164" fill="currentColor">페이지 테이블 워크</text>
  <text x="180" y="180" fill="currentColor">(단계마다 PTE 1 개 load)</text>
  <rect x="370" y="130" width="160" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="380" y="148" fill="currentColor">PTE valid → TLB 채움</text>
  <text x="380" y="164" fill="currentColor">→ 다시 ① (수 ns ~ 수십 ns,</text>
  <text x="380" y="180" fill="currentColor">PTE 가 캐시에 있나에 따라)</text>
  <rect x="170" y="232" width="150" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="180" y="250" fill="currentColor">③ PTE invalid:</text>
  <text x="180" y="266" fill="currentColor">페이지 폴트 예외</text>
  <text x="180" y="282" fill="currentColor">(EL0 → EL1 커널)</text>
  <rect x="370" y="232" width="250" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="380" y="250" fill="currentColor">커널: 빈 프레임 + 16 KB 0 채우기 +</text>
  <text x="380" y="266" fill="currentColor">PTE 쓰기 → 복귀 → 명령 재실행</text>
  <text x="380" y="282" fill="currentColor">(이 맥 실측: 페이지당 약 1 µs)</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M130 62 H166"/><path d="M320 62 H366"/>
    <path d="M245 84 V126"/><path d="M320 158 H366"/>
    <path d="M245 186 V228"/><path d="M450 130 V88"/>
    <path d="M620 260 H700 V28 H245 V36" stroke-dasharray="4 3"/>
  </g>
  <text x="640" y="140" fill="currentColor">재실행</text>
  <text x="10" y="120" fill="currentColor">TLB reach</text>
  <text x="10" y="136" fill="currentColor">= 항목 수 × 페이지</text>
  <text x="10" y="152" fill="currentColor">16 KB 면 4 KB 의 4 배</text>
</svg>
```

이 레슨은 ③(첫 접근 폴트)과 ②(TLB 미스)를 **따로** 잰다. ③은 새 매핑을 만지는 첫 번째 패스에만 있고, ②는 폴트가 끝난 매핑에서도 "만지는 페이지 수가 TLB 가 기억하는 수보다 많으면" 매번 있다. 어려운 점은 ②를 **캐시 미스와 섞지 않고** 재는 것이다 — 페이지가 많아지면 보통 데이터도 많아지기 때문이다.

### Part A — 페이지 수학과 첫 접근(first touch)

1. `pages_for(bytes, pg)` — 올림 페이지 수. `(bytes + pg - 1) / pg` 는 `bytes` 가 `SIZE_MAX` 근처면 넘친다 → 넘치지 않게.
2. `page_floor(a, pg)`, `page_index(base, addr, pg)`.
3. `touch_pages(p, n, stride, off, mode)` — `p[i*stride + off]` 를 i = 0..n-1 한 번씩 읽기(합계 반환) 또는 쓰기(`i+1`).
4. `resident_pages(p, npages, pg)` — `mincore()` 로 물리 메모리에 올라온 페이지 수.

체크: 페이지 크기(세 API 가 같은 값, 2 의 거듭제곱), 경계값, `SIZE_MAX`, 새 매핑은 상주 0 → 짝수 페이지만 쓰면 정확히 절반 → 전부 쓰면 전부, 쓴 값 확인, 새 매핑을 읽으면 0. **시간은 체크하지 않는다** — `demo_fault.c` 가 같은 함수로 첫/두 번째/세 번째 패스를 7 회씩 재서 중앙값을 낸다. 쓰기 먼저 vs 읽기 먼저도 잰다.

### Part B — TLB reach: 포인터 체이스 P 스윕

1. `sattolo(next, P, seed)` — 길이 P 짜리 **사이클 하나** 인 무작위 순열 (Fisher–Yates 와 j 범위 한 글자 차이).
2. `is_single_cycle(next, P)` — 0 에서 출발해 정확히 P 걸음에 돌아오고 범위 밖 값이 없나.
3. `chase_offset(i, pg, line, layout)` — 원소 i 의 바이트 위치. `spread` = 페이지 i 의 줄 (i mod 128), `same` = 페이지 i 의 오프셋 0, `packed` = 줄 i (페이지 무시), `quad` = 한 페이지에 원소 4 개 (4 KB 간격 조각, 줄 회전).
4. `find_knees(y, n, ratio, out, maxout)` — 지연 곡선에서 "상승이 시작되는 점".

체크: 여러 P 와 시드에서 한 사이클, P = 4 에서 600 번 돌리면 (4−1)! = 6 가지 사이클이 전부 나옴, 손으로 만든 반례 5 개, 오프셋 공식, 배치별 "페이지 안 줄 번호" 가짓수(spread·packed·quad 128, same 1), 실제 매핑 위에 네 배치로 체인을 만들어 P 걸음에 원점 (spread 는 모든 페이지 정확히 한 번), 무릎 찾기 표. `demo_tlb.c` 가 P = 4 … 65536 을 네 배치로 재서 표와 그림을 만든다.

### Part C — 펌웨어/OS 가 하는 일: prefault · mlock · 큰 페이지 · 예산

1. `prefault(p, len, pg, how)` — 페이지마다 쓰기 / `memset` / `madvise(MADV_WILLNEED)` / `mlock`.
2. `vm_budget(bytes, pg, ns_per_fault, reach_bytes)` — 페이지 수, 폴트 시간 합(µs), TLB reach 안에 드나.

체크: 쓰기 터치·memset·mlock 뒤 상주 = 전부, WILLNEED 는 성공만 (상주는 보장 안 되는 힌트 — demo 가 잰다), 모르는 방법 −1, 예산 계산 표. `demo_fault.c` 가 방법별로 prefault 시간과 그 뒤 첫 사용 시간을 재고, macOS 에서 2 MB superpage 요청이 되는지 시험한다.

## 2. Problem (English)

> **Measure virtual-memory costs on a real machine (macOS, Apple Silicon).** Do not hard-code the page size: verify it at run time (`sysconf(_SC_PAGESIZE)`, `vm_page_size`).
>
> **A.** Implement overflow-safe `pages_for(bytes, pg)`, `page_floor`, `page_index`, `touch_pages(p, n, stride, off, mode)` (one byte per page; read returns the sum, write stores `i+1`) and `resident_pages` using `mincore`. In a benchmark (not in the checks), mmap a fresh anonymous region of N pages and time touching one byte per page: first pass, second pass, third pass; fresh mapping per run, median of ≥ 5 runs; report ns/page, minor faults/page and kernel time. Compare write-first with read-first. Does a read of an untouched page map a shared zero page that faults again on the first write? Measure; don't assume.
>
> **B.** Implement Sattolo's algorithm (a uniformly random *single-cycle* permutation), a single-cycle checker, the element offset for four layouts (`spread`: one line per page at line `i mod (pg/line)`; `same`: offset 0 in every page; `packed`: consecutive lines; `quad`: four elements per page, 4 KB apart), and a knee finder. Using a dependent pointer chase, sweep P = 4 … 65536 pages and plot ns/access. Explain why the line offset must vary per page, how `packed` and `quad` control for cache footprint and page count, and convert each knee to a TLB coverage estimate (P × page size). Apple does not publish TLB sizes — say what your data implies, not what a blog says.
>
> **C.** Implement `prefault(p, len, pg, how)` (touch, memset, `MADV_WILLNEED`, `mlock`) and a budget calculator. Measure each method's cost and its effect on the first real use. Is there a `MAP_POPULATE` or a huge-page knob on macOS arm64? Try it.
>
> Follow-ups: How many memory accesses can one TLB miss cost on a 4-level walk? Why 16 KB pages? Why is the second pass over freshly faulted memory not free on this machine? How would a real-time thread avoid page faults? What changes with 2 MB / 32 MB blocks?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 이 맥: 27.2 SDK 링크가 깨져 있다
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o st && ./st     # 16/61 passed, exit 1
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so     # 61/61 passed
```

시작 코드 실제 출력 (일부):

```text
info  page size: sysconf=16384 getpagesize=16384 vm_page_size=16384
FAIL starter.c:189  pages_for(0, 16384) == 0 && pages_for(1, 16384) == 1 && pages_for(16384, 16384) == 1
...
FAIL starter.c:202  touch_pages(m, n / 2, 2 * pg, 1000, T_WRITE) == n / 2
FAIL starter.c:203  resident_pages(m, n, pg) == n / 2
...
FAIL starter.c:229  all
FAIL starter.c:237  seen == 6
...
FAIL starter.c:265  m != NULL
SKIP chain checks (spread): no mapping
...
16/61 passed
```

TODO 의 기본값은 "아무것도 안 함" (0, 항등 순열, false) 이다. 시작 코드의 `chase_offset` 은 원소를 8 바이트씩 붙여 놓고(포인터 배열처럼), `pages_for` 가 0 을 돌려주므로 체인용 매핑 크기가 0 → `mmap` 실패 → 체인 체크는 SKIP(실패로 셈)된다. 측정 프로그램(둘 다 `solution.c` 를 `main` 이름만 바꿔 포함):

```bash
cc -std=c11 -O2 demo_fault.c -o demo_fault && ./demo_fault        # Part A + C, 약 3 초
cc -std=c11 -O2 demo_tlb.c   -o demo_tlb   && ./demo_tlb          # Part B, 약 15~40 초 (부하에 따라), 최대 1 GiB 매핑
```

## 4. 힌트

<details><summary>힌트 1 — 넘치지 않는 올림</summary>

`bytes / pg` 는 절대 넘치지 않는다. 나머지가 있으면 1 을 더한다: `bytes / pg + (bytes % pg != 0)`. pg 가 2 의 거듭제곱이면 `bytes >> log2(pg)` 와 `bytes & (pg-1)` 로도 된다. `page_floor` 는 `a & ~(uintptr_t)(pg - 1)` — `pg - 1` 을 `uintptr_t` 로 바꾼 **뒤** 뒤집어야 64 비트 마스크가 된다.
</details>

<details><summary>힌트 2 — mincore</summary>

`mincore(addr, len, vec)` 는 페이지마다 `vec` 1 바이트를 채운다. macOS 헤더에서 `MINCORE_INCORE` = 0x1 (그 밖에 참조·수정 비트도 있다). `addr` 는 페이지 정렬이어야 한다. -std=c11 에서는 `_DARWIN_C_SOURCE` 를 include 앞에 정의해야 선언이 보인다.
</details>

<details><summary>힌트 3 — Sattolo</summary>

Fisher–Yates: `for i = P-1 .. 1: j = rand % (i+1); swap(a[i], a[j])` → 모든 순열이 균등. Sattolo: `j = rand % i` (자기 자신 제외) → 사이클 하나짜리 순열만, 균등하게 ((P−1)! 가지). 결과 배열을 그대로 `next[i] = a[i]` 로 읽으면 된다. 왜 한 사이클이어야 하나: 체이스가 0 에서 출발해 P 개를 **전부** 돌아야 "P 페이지를 만지는 중" 이 맞다. Fisher–Yates 는 평균 ln P 개 사이클 → 0 이 속한 사이클만 돈다 (연습 2).
</details>

<details><summary>힌트 4 — 왜 줄 위치를 페이지마다 바꾸나</summary>

모든 원소를 페이지 오프셋 0 에 두면 주소의 아래 14 비트가 전부 같다. L1 데이터 캐시의 세트 번호는 그 아래 비트에서 나오므로(VIPT, 아래 풀이) **모든 원소가 한 세트** 에 몰린다 → 세트의 way 수만큼만 캐시에 남는다. 그러면 P 가 작아도 캐시 미스가 나서 TLB 효과와 구별할 수 없다. 페이지 i 의 줄을 (i mod 128) 로 돌리면 128 세트에 고르게 퍼진다.
</details>

<details><summary>힌트 5 — 무릎</summary>

"평탄 구간의 값(base) 대비 ratio 배 이상" 이면 상승. 상승이 여러 점에 걸치면 첫 점만 기록하고, base 는 매 상승 점마다 올린다. 상승이 아니면 base 를 내리지 않는다 (노이즈로 내려간 점을 base 로 쓰면 다음 점이 가짜 무릎이 된다).
</details>

## 5. 풀이

### 0. 측정 환경 (모든 표 공통)

`sysctl`: `machdep.cpu.brand_string` = Apple M6, `hw.pagesize` = 16384, `hw.cachelinesize` = 128, 코어 3 종 (perflevel0 Super 2 개 · L1D 128 KB / perflevel1 Performance 4 개 · L1D 64 KB / perflevel2 Efficiency 6 개 · L1D 96 KB — `hw.perflevel*.l1dcachesize` 그대로), 메모리 32 GB, macOS 27.2. `cc --version` 첫 줄 = `Apple clang version 21.0.0 (clang-2100.0.123.102)`, 데모는 `-O2`.

**부하:** 이 맥은 다른 레슨 작성 세션들과 공유 중이었다. 최종 표를 잰 시각의 `uptime` load average 는 demo_tlb 세 세트 시작 37.85 / 34.61 / 34.19, demo_fault 두 세트 시작 30.18 / 29.53 (1 분 평균, 12 코어). 탐색 중에는 50~128 까지 올라갔다. 그래서 (1) 모든 값은 7 회의 **중앙값**, (2) 실행 간 [최소–최대] 를 같이 적었고, (3) 결론은 세 세트에서 반복된 것만 말한다. 스레드에 `pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0)` 을 걸었지만 **힌트일 뿐** — macOS 에는 코어 고정 API 가 없고, 부하가 이 정도면 E 코어로 밀려나거나 코어를 옮겨 다니는 구간이 섞인다고 봐야 한다.

### Part A — 페이지 수학: 왜 이렇게 짜나

| 함수 | 답 | 이유 |
|---|---|---|
| `pages_for` | `bytes / pg + (bytes % pg != 0)` | `(bytes + pg - 1)` 은 `SIZE_MAX` 근처에서 넘친다. 체크: `pages_for(SIZE_MAX, 16384) == SIZE_MAX/16384 + 1` |
| `page_floor` | `a & ~(uintptr_t)(pg - 1)` | 2 의 거듭제곱 정렬 = 아래 비트 지우기. 마스크를 주소 폭으로 만든 뒤 뒤집는다 |
| `page_index` | `(floor(addr) − floor(base)) / pg` | `addr - base` 를 바로 나눠도 base 가 정렬돼 있으면 같지만, 바닥끼리 빼면 base 가 정렬 안 된 경우도 "몇 번째 페이지 경계를 넘었나" 로 일관된다 |
| `touch_pages` | `volatile` 포인터로 `p[i*stride + off]` | volatile 이 없으면 `-O2` 가 읽기 루프의 합을 지우거나(결과 미사용 시) 쓰기를 합칠 수 있다 |
| `resident_pages` | `mincore` 의 `MINCORE_INCORE` 비트 합 | macOS `vec` 는 페이지당 1 바이트, 비트 0 = 상주 (헤더에 참조·수정 비트도 정의돼 있다) |

체크의 핵심 장면 — 새 매핑 64 페이지: 상주 0 → `touch_pages(m, 32, 2*pg, 1000, T_WRITE)` (짝수 페이지만) → 상주 **정확히 32** → 전부 쓰기 → 64. "커널은 `mmap` 때 메모리를 주지 않고, 만진 페이지만 준다" 를 결정적으로 보여 준다 (요구 페이징, demand paging).

### Part A — 첫 접근 실측 (demo_fault)

<!-- code: demo_fault.c -->

`./demo_fault` 세트 1 (load 30.18 시작). 칸 = 중앙값 ns/페이지 [7 회 최소–최대], 그 패스의 minflt/페이지, 그 패스의 커널(sys) 시간 중앙값. 매 실행 새 매핑.

| N (크기) | 순서 | 1 패스 | 2 패스 | 3 패스 |
|---|---|---|---|---|
| 256 (4 MiB) | W W W | 766 [702–1994] · 1.00 · 148 µs | 8.5 [3–26] · 0 · 1 µs | 2.6 · 0 |
| 256 | R W W | 679 [629–1326] · 1.00 · 134 µs | 6.5 [3–19] · 0 · 1 µs | 2.6 · 0 |
| 1024 (16 MiB) | W W W | 1042 [708–1301] · 1.00 · 776 µs | 127 [69–247] · 0 · 89 µs | 3.4 · 0 |
| 1024 | R W W | 1352 [700–1692] · 1.00 · 998 µs | 118 [47–201] · 0 · 90 µs | 5.0 · 0 |
| 4096 (64 MiB) | W W W | 1127 [1016–1348] · 1.00 · 3856 µs | 154 [149–220] · 0 · 446 µs | 4.3 · 0 |
| 4096 | R W W | 1231 [1086–1580] · 1.00 · 4062 µs | 149 [97–267] · 0 · 432 µs | 4.9 · 0 |
| 16384 (256 MiB) | W W W | 1251 [1147–1663] · 1.00 · 17225 µs | 286 [204–318] · 0 · 3092 µs | 7.2 · 0 |
| 16384 | R W W | 1441 [1280–2093] · 1.00 · 16340 µs | 225 [176–708] · 0 · 2593 µs | 5.9 · 0 |
| 16384 | W R R | 1206 [1084–1388] · 1.00 · 16369 µs | 205 [178–340] · 0 · 2417 µs | 5.2 · 0 |

세트 2 (바로 이어서, load 29.53): 1 패스는 같은 범위 (870~1431 ns, minflt 1.00), 그런데 **2 패스가 거의 사라졌다** — N = 16384 에서 W W W 30.0 [9–308] ns · sys 221 µs, N = 4096 에서 5.0 ns · sys 0.

읽을 점:

1. **첫 접근 = 페이지당 폴트 정확히 1 번, 약 0.7~1.4 µs.** minflt/페이지가 모든 행에서 1.00 이고, 패스의 커널 시간 ÷ N 이 1 패스 시간과 거의 같다 (16384 페이지: 17225 µs / 16384 ≈ 1.05 µs vs 1251 ns). 즉 첫 접근 비용의 대부분이 커널 안이다. 0 채우기 16 KB 만으로는 이보다 훨씬 짧아야 한다 (수십 GB/s 면 1 µs 의 몇 분의 1) → 나머지는 예외 진입·VM 객체 탐색·프레임 할당·PTE 갱신·복귀의 고정 비용. 페이지가 커질수록 이 고정 비용이 바이트당 싸진다 — 16 KB 페이지의 이득 중 하나.
2. **읽기 먼저 해도 "공유 제로 페이지 → 쓰기 때 다시 폴트" 는 없었다.** R W W 의 1 패스(읽기) minflt 1.00, 바로 다음 쓰기 패스 minflt **0** (모든 N, 두 세트). 체크 하네스의 정보 줄도 `after read-only first touch: resident 64/64 pages` — 읽기만 했는데 상주. 리눅스는 익명 매핑의 읽기 폴트에 공유 zero page 를 매핑하고 첫 쓰기에서 COW 폴트를 한 번 더 내는 것으로 알려져 있다 (이 맥에서 재지는 않았다). macOS 27.2 / M6 에서는 읽기 폴트가 곧바로 쓰기 가능한 0 페이지를 준 것으로 **관찰** 된다. 결론: 이 맥에서 "읽어서 prefault" 와 "써서 prefault" 의 폴트 수는 같다.
3. **두 번째 패스가 공짜가 아니었다 — 그리고 실행마다 달랐다.** 세트 1 의 N ≥ 1024 에서 2 패스가 120~290 ns/페이지, minflt 0 인데 **sys 시간이 있다** (16384: 3092 µs ≈ 189 ns/페이지 가 커널 안). 3 패스는 2~7 ns. 탐색 단계에서 대조 실험을 했다 (`/tmp` 의 탐색용 코드, 레슨 파일 아님): 2 패스를 다른 줄(오프셋 8 KB)로 해도 느림, 2 패스 전에 300 ms 쉬어도 느림(더 느림), 64 MiB memset 으로 캐시를 밀어낸 뒤의 패스는 빠름, 무작위 순서로 첫 접근해도·`MADV_RANDOM` 이어도 느림. 그리고 `mlock` 으로 들인 페이지는 첫 사용부터 빠르다 (Part C). → 캐시·TLB 효과가 아니라 **페이지마다 한 번의 가벼운 커널 진입** 으로, VM 폴트 카운터(`ru_minflt`, `task_info` 의 faults 둘 다 0)에 잡히지 않는다. 원인은 확정 못 했다 (Q4 의 가설). 세트 2 처럼 거의 안 나타나는 실행도 있어 시스템 전체 상태(메모리 압박 등)에 달린 것으로 보인다 — **추정**.
4. **재접근(3 패스) 2~7 ns/페이지는 "지연" 이 아니다.** 각 접근이 서로 독립이라 코어가 여러 TLB 미스·캐시 미스를 겹친다 (MLP). 페이지당 진짜 TLB 미스 지연은 Part B 의 의존 체이스로만 보인다 (mistake 6).

### Part B — 체이스 설계: 무엇을 재고 무엇을 빼나

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">네 가지 배치 — 상자 = 16 KB 페이지, 막대 = 체이스 원소가 있는 128 B 줄 (가로 위치 = 페이지 안 오프셋)</text>
  <g fill="currentColor"><text x="10" y="56">spread</text><text x="10" y="116">quad</text><text x="10" y="176">packed</text><text x="10" y="236">same</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="80" y="40" width="90" height="26"/><rect x="180" y="40" width="90" height="26"/><rect x="280" y="40" width="90" height="26"/><rect x="380" y="40" width="90" height="26"/>
    <rect x="80" y="100" width="90" height="26"/><rect x="180" y="100" width="90" height="26"/><rect x="280" y="100" width="90" height="26"/><rect x="380" y="100" width="90" height="26"/>
    <rect x="80" y="160" width="90" height="26"/><rect x="180" y="160" width="90" height="26" stroke-dasharray="3 3"/><rect x="280" y="160" width="90" height="26" stroke-dasharray="3 3"/><rect x="380" y="160" width="90" height="26" stroke-dasharray="3 3"/>
    <rect x="80" y="220" width="90" height="26"/><rect x="180" y="220" width="90" height="26"/><rect x="280" y="220" width="90" height="26"/><rect x="380" y="220" width="90" height="26"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <rect x="81" y="41" width="5" height="24"/><rect x="187" y="41" width="5" height="24"/><rect x="293" y="41" width="5" height="24"/><rect x="399" y="41" width="5" height="24"/>
    <rect x="81" y="101" width="5" height="24"/><rect x="103" y="101" width="5" height="24"/><rect x="126" y="101" width="5" height="24"/><rect x="148" y="101" width="5" height="24"/>
    <rect x="187" y="101" width="5" height="24"/><rect x="209" y="101" width="5" height="24"/><rect x="232" y="101" width="5" height="24"/><rect x="254" y="101" width="5" height="24"/>
    <rect x="81" y="161" width="5" height="24"/><rect x="87" y="161" width="5" height="24"/><rect x="93" y="161" width="5" height="24"/><rect x="99" y="161" width="5" height="24"/>
    <rect x="105" y="161" width="5" height="24"/><rect x="111" y="161" width="5" height="24"/><rect x="117" y="161" width="5" height="24"/><rect x="123" y="161" width="5" height="24"/>
  </g>
  <g class="f-warn" stroke="currentColor">
    <rect x="81" y="221" width="5" height="24"/><rect x="181" y="221" width="5" height="24"/><rect x="281" y="221" width="5" height="24"/><rect x="381" y="221" width="5" height="24"/>
  </g>
  <g fill="currentColor">
    <text x="490" y="50">P 페이지, 줄 = i mod 128</text><text x="490" y="66">→ 128 세트에 고르게</text>
    <text x="490" y="110">P/4 페이지 (4 KB 간격 4 조각)</text><text x="490" y="126">→ 무릎이 P 기준 4 배 뒤로?</text>
    <text x="490" y="170">P/128 페이지, 같은 줄 수</text><text x="490" y="186">→ 캐시 대조군 (점선 = 안 씀)</text>
    <text x="490" y="230">P 페이지, 전부 오프셋 0</text><text x="490" y="246">→ 한 세트에 몰림 (반면교사)</text>
  </g>
  <text x="10" y="276" fill="currentColor">L1D 세트 번호는 페이지 안 오프셋 비트에서 나온다고 본다 (VIPT, 128 KB ÷ 8-way = 16 KB = 페이지 — 구조상 추정).</text>
</svg>
```

- **의존 체인** `p = *(void **)p`: 다음 주소가 이번 load 의 결과라 코어가 미스를 겹칠 수 없다 → ns/접근 = 한 접근의 지연 (L1 히트 + TLB 히트면 약 0.7 ns — 표의 작은 P 값).
- **한 사이클 (Sattolo)**: P 걸음에 모든 원소를 한 번씩. Fisher–Yates 면 0 의 사이클만 돈다 (연습 2: P = 4096 에서 사이클 5 개, 0 에서 출발하면 1171 개만).
- **무작위 순서**: 순차·고정 stride 면 프리페처가 다음 줄(그리고 다음 페이지 변환까지)을 미리 가져올 수 있다. 무작위 사이클은 그걸 막는다.
- **줄 하나/페이지 + 오프셋 회전 (spread)**: 데이터 발자국을 P × 128 B 로 작게 (P = 4096 이어도 512 KB). `same` 처럼 오프셋을 고정하면 세트 하나에 몰린다.
- **대조군 두 개**: `packed` = 같은 줄 수를 최소 페이지에 (캐시 효과만), `quad` = 페이지당 4 원소 (같은 P 에서 페이지 1/4). TLB 가 원인이면 spread 의 무릎이 quad 에서 **P 기준 4 배 뒤** 에 와야 한다.
- **폴트는 타이밍 밖**: 매핑마다 `prefault(PF_TOUCH)` + 체인 만들기(모든 원소 쓰기) + 2 바퀴 예열 뒤에 잰다.

### Part B — 실측: ns/접근 vs P (demo_tlb)

<!-- code: demo_tlb.c -->

`./demo_tlb 7` 세트 1 (load 37.85 → 34.61). 각 칸 = 2^21 (또는 4P) 걸음 시간을 7 번 잰 중앙값.

| P | P × 16 KB | P × 128 B | spread | quad | packed | same | spread − packed |
|---|---|---|---|---|---|---|---|
| 4 | 64 KiB | 0.5 KiB | 0.94 | 0.90 | 0.96 | 0.75 | −0.02 |
| 12 | 192 KiB | 1.5 KiB | 0.80 | 0.76 | 0.80 | **5.66** | 0.00 |
| 64 | 1 MiB | 8 KiB | 0.98 | 0.88 | 0.74 | 6.43 | 0.24 |
| 128 | 2 MiB | 16 KiB | 0.72 | 0.73 | 0.72 | 6.71 | 0.01 |
| 192 | 3 MiB | 24 KiB | 1.14 | 1.02 | 1.06 | 7.39 | 0.08 |
| **256** | **4 MiB** | 32 KiB | **2.41** | 0.69 | 0.91 | 8.27 | 1.50 |
| 384 | 6 MiB | 48 KiB | 2.72 | 1.06 | 0.87 | 8.85 | 1.85 |
| 512 | 8 MiB | 64 KiB | 2.23 | 1.03 | 1.03 | 9.98 | 1.20 |
| 768 | 12 MiB | 96 KiB | 2.90 | 1.06 | 0.87 | 11.30 | 2.03 |
| 1024 | 16 MiB | 128 KiB | 7.93 | **4.47** | **2.99** | 21.51 | 4.95 |
| 1536 | 24 MiB | 192 KiB | 8.53 | 7.66 | 5.71 | 29.36 | 2.82 |
| 2048 | 32 MiB | 256 KiB | 11.36 | 7.75 | 6.62 | 28.22 | 4.74 |
| **3072** | **48 MiB** | 384 KiB | **17.19** | 8.11 | 4.89 | 36.25 | 12.30 |
| 4096 | 64 MiB | 512 KiB | 17.24 | 10.27 | 5.66 | 38.81 | 11.58 |
| 8192 | 128 MiB | 1 MiB | 19.97 | 11.43 | 5.00 | 37.21 | 14.97 |
| 16384 | 256 MiB | 2 MiB | 29.89 | 30.74 | 8.07 | 42.83 | 21.82 |
| 32768 | 512 MiB | 4 MiB | 28.98 | 50.29 | 12.07 | — | 16.91 |
| **49152** | **768 MiB** | 6 MiB | **52.01** | 46.12 | 14.57 | — | 37.45 |
| 65536 | 1 GiB | 8 MiB | 108.66 | 97.24 | 15.96 | — | 92.70 |

(표는 29 개 점 중 일부. 전체는 demo 출력, 그림은 29 개 전부. `same` 은 16384 페이지까지만 잰다.)

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">포인터 체이스 ns/접근 vs 페이지 수 P — demo_tlb 실측 (세트 1, 각 점 = 7 회 중앙값, load 34~38)</text>
  <path d="M70 290.0 H720" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="62" y="294.0" fill="currentColor" text-anchor="end">0.5</text>
  <path d="M70 261.1 H720" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="62" y="265.1" fill="currentColor" text-anchor="end">1</text>
  <path d="M70 232.2 H720" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="62" y="236.2" fill="currentColor" text-anchor="end">2</text>
  <path d="M70 193.9 H720" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="62" y="197.9" fill="currentColor" text-anchor="end">5</text>
  <path d="M70 165.0 H720" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="62" y="169.0" fill="currentColor" text-anchor="end">10</text>
  <path d="M70 136.1 H720" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="62" y="140.1" fill="currentColor" text-anchor="end">20</text>
  <path d="M70 97.8 H720" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="62" y="101.8" fill="currentColor" text-anchor="end">50</text>
  <path d="M70 68.9 H720" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="62" y="72.9" fill="currentColor" text-anchor="end">100</text>
  <path d="M70 40.0 H720" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="62" y="44.0" fill="currentColor" text-anchor="end">200</text>
  <path d="M70.0 290 V40" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="70.0" y="306" fill="currentColor" text-anchor="middle">4</text>
  <path d="M162.9 290 V40" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="162.9" y="306" fill="currentColor" text-anchor="middle">16</text>
  <path d="M255.7 290 V40" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="255.7" y="306" fill="currentColor" text-anchor="middle">64</text>
  <path d="M348.6 290 V40" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="348.6" y="306" fill="currentColor" text-anchor="middle">256</text>
  <path d="M441.4 290 V40" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="441.4" y="306" fill="currentColor" text-anchor="middle">1024</text>
  <path d="M534.3 290 V40" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="534.3" y="306" fill="currentColor" text-anchor="middle">4096</text>
  <path d="M627.1 290 V40" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="627.1" y="306" fill="currentColor" text-anchor="middle">16384</text>
  <path d="M720.0 290 V40" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="720.0" y="306" fill="currentColor" text-anchor="middle">65536</text>
  <text x="395.0" y="322" fill="currentColor" text-anchor="middle">P = 만지는 페이지 수 (로그 눈금) — 16 KB 페이지면 P × 16 KB 를 덮어야 함</text>
  <text x="14" y="165.0" fill="currentColor" transform="rotate(-90 14 165.0)" text-anchor="middle">ns / 접근 (로그)</text>
  <path d="M70.0 263.7 L97.2 264.6 L116.4 272.0 L143.6 270.4 L162.9 274.2 L190.0 270.4 L209.3 271.4 L236.4 272.0 L255.7 261.9 L282.9 274.8 L302.1 274.8 L329.3 255.6 L348.6 224.4 L375.7 219.3 L395.0 227.6 L422.2 216.7 L441.4 174.7 L468.6 171.6 L487.9 159.7 L515.0 142.4 L534.3 142.3 L561.4 139.5 L580.7 136.1 L607.9 138.5 L627.1 119.3 L654.3 127.0 L673.6 120.6 L700.7 96.2 L720.0 65.5" fill="none" stroke="currentColor" stroke-width="2"/>
  <g class="f-acc" stroke="currentColor"><circle cx="70.0" cy="263.7" r="3"/><circle cx="97.2" cy="264.6" r="3"/><circle cx="116.4" cy="272.0" r="3"/><circle cx="143.6" cy="270.4" r="3"/><circle cx="162.9" cy="274.2" r="3"/><circle cx="190.0" cy="270.4" r="3"/><circle cx="209.3" cy="271.4" r="3"/><circle cx="236.4" cy="272.0" r="3"/><circle cx="255.7" cy="261.9" r="3"/><circle cx="282.9" cy="274.8" r="3"/><circle cx="302.1" cy="274.8" r="3"/><circle cx="329.3" cy="255.6" r="3"/><circle cx="348.6" cy="224.4" r="3"/><circle cx="375.7" cy="219.3" r="3"/><circle cx="395.0" cy="227.6" r="3"/><circle cx="422.2" cy="216.7" r="3"/><circle cx="441.4" cy="174.7" r="3"/><circle cx="468.6" cy="171.6" r="3"/><circle cx="487.9" cy="159.7" r="3"/><circle cx="515.0" cy="142.4" r="3"/><circle cx="534.3" cy="142.3" r="3"/><circle cx="561.4" cy="139.5" r="3"/><circle cx="580.7" cy="136.1" r="3"/><circle cx="607.9" cy="138.5" r="3"/><circle cx="627.1" cy="119.3" r="3"/><circle cx="654.3" cy="127.0" r="3"/><circle cx="673.6" cy="120.6" r="3"/><circle cx="700.7" cy="96.2" r="3"/><circle cx="720.0" cy="65.5" r="3"/></g>
  <path d="M70.0 262.8 L97.2 262.8 L116.4 264.1 L143.6 270.4 L162.9 269.9 L190.0 272.5 L209.3 272.5 L236.4 273.6 L255.7 273.6 L282.9 271.4 L302.1 274.8 L329.3 258.6 L348.6 265.0 L375.7 266.9 L395.0 259.8 L422.2 266.9 L441.4 215.4 L468.6 188.4 L487.9 182.2 L515.0 194.9 L534.3 188.7 L561.4 163.8 L580.7 193.9 L607.9 187.1 L627.1 173.9 L654.3 162.1 L673.6 157.1 L700.7 149.3 L720.0 145.5" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3"/>
  <g class="f-ok" stroke="currentColor"><circle cx="70.0" cy="262.8" r="3"/><circle cx="97.2" cy="262.8" r="3"/><circle cx="116.4" cy="264.1" r="3"/><circle cx="143.6" cy="270.4" r="3"/><circle cx="162.9" cy="269.9" r="3"/><circle cx="190.0" cy="272.5" r="3"/><circle cx="209.3" cy="272.5" r="3"/><circle cx="236.4" cy="273.6" r="3"/><circle cx="255.7" cy="273.6" r="3"/><circle cx="282.9" cy="271.4" r="3"/><circle cx="302.1" cy="274.8" r="3"/><circle cx="329.3" cy="258.6" r="3"/><circle cx="348.6" cy="265.0" r="3"/><circle cx="375.7" cy="266.9" r="3"/><circle cx="395.0" cy="259.8" r="3"/><circle cx="422.2" cy="266.9" r="3"/><circle cx="441.4" cy="215.4" r="3"/><circle cx="468.6" cy="188.4" r="3"/><circle cx="487.9" cy="182.2" r="3"/><circle cx="515.0" cy="194.9" r="3"/><circle cx="534.3" cy="188.7" r="3"/><circle cx="561.4" cy="163.8" r="3"/><circle cx="580.7" cy="193.9" r="3"/><circle cx="607.9" cy="187.1" r="3"/><circle cx="627.1" cy="173.9" r="3"/><circle cx="654.3" cy="162.1" r="3"/><circle cx="673.6" cy="157.1" r="3"/><circle cx="700.7" cy="149.3" r="3"/><circle cx="720.0" cy="145.5" r="3"/></g>
  <path d="M70.0 273.1 L97.2 274.2 L116.4 273.6 L143.6 188.7 L162.9 195.3 L190.0 199.5 L209.3 186.0 L236.4 185.5 L255.7 183.4 L282.9 190.5 L302.1 181.6 L329.3 177.6 L348.6 172.9 L375.7 170.1 L395.0 165.1 L422.2 159.9 L441.4 133.0 L468.6 120.1 L487.9 121.7 L515.0 111.3 L534.3 108.4 L561.4 106.8 L580.7 110.2 L607.9 105.3 L627.1 104.3" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="2 3"/>
  <g class="f-warn" stroke="currentColor"><circle cx="70.0" cy="273.1" r="3"/><circle cx="97.2" cy="274.2" r="3"/><circle cx="116.4" cy="273.6" r="3"/><circle cx="143.6" cy="188.7" r="3"/><circle cx="162.9" cy="195.3" r="3"/><circle cx="190.0" cy="199.5" r="3"/><circle cx="209.3" cy="186.0" r="3"/><circle cx="236.4" cy="185.5" r="3"/><circle cx="255.7" cy="183.4" r="3"/><circle cx="282.9" cy="190.5" r="3"/><circle cx="302.1" cy="181.6" r="3"/><circle cx="329.3" cy="177.6" r="3"/><circle cx="348.6" cy="172.9" r="3"/><circle cx="375.7" cy="170.1" r="3"/><circle cx="395.0" cy="165.1" r="3"/><circle cx="422.2" cy="159.9" r="3"/><circle cx="441.4" cy="133.0" r="3"/><circle cx="468.6" cy="120.1" r="3"/><circle cx="487.9" cy="121.7" r="3"/><circle cx="515.0" cy="111.3" r="3"/><circle cx="534.3" cy="108.4" r="3"/><circle cx="561.4" cy="106.8" r="3"/><circle cx="580.7" cy="110.2" r="3"/><circle cx="607.9" cy="105.3" r="3"/><circle cx="627.1" cy="104.3" r="3"/></g>
  <path d="M70.0 265.5 L97.2 272.5 L116.4 273.1 L143.6 272.5 L162.9 263.7 L190.0 269.4 L209.3 274.2 L236.4 267.9 L255.7 266.4 L282.9 274.8 L302.1 274.2 L329.3 260.3 L348.6 276.6 L375.7 258.6 L395.0 259.8 L422.2 258.6 L441.4 198.6 L468.6 176.1 L487.9 175.6 L515.0 173.7 L534.3 163.9 L561.4 123.1 L580.7 159.4 L607.9 142.4 L627.1 118.1 L654.3 111.6 L673.6 97.6 L700.7 101.2 L720.0 70.1" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="8 2 2 2"/>
  <g class="f-mut" stroke="currentColor"><circle cx="70.0" cy="265.5" r="3"/><circle cx="97.2" cy="272.5" r="3"/><circle cx="116.4" cy="273.1" r="3"/><circle cx="143.6" cy="272.5" r="3"/><circle cx="162.9" cy="263.7" r="3"/><circle cx="190.0" cy="269.4" r="3"/><circle cx="209.3" cy="274.2" r="3"/><circle cx="236.4" cy="267.9" r="3"/><circle cx="255.7" cy="266.4" r="3"/><circle cx="282.9" cy="274.8" r="3"/><circle cx="302.1" cy="274.2" r="3"/><circle cx="329.3" cy="260.3" r="3"/><circle cx="348.6" cy="276.6" r="3"/><circle cx="375.7" cy="258.6" r="3"/><circle cx="395.0" cy="259.8" r="3"/><circle cx="422.2" cy="258.6" r="3"/><circle cx="441.4" cy="198.6" r="3"/><circle cx="468.6" cy="176.1" r="3"/><circle cx="487.9" cy="175.6" r="3"/><circle cx="515.0" cy="173.7" r="3"/><circle cx="534.3" cy="163.9" r="3"/><circle cx="561.4" cy="123.1" r="3"/><circle cx="580.7" cy="159.4" r="3"/><circle cx="607.9" cy="142.4" r="3"/><circle cx="627.1" cy="118.1" r="3"/><circle cx="654.3" cy="111.6" r="3"/><circle cx="673.6" cy="97.6" r="3"/><circle cx="700.7" cy="101.2" r="3"/><circle cx="720.0" cy="70.1" r="3"/></g>
  <g stroke="currentColor" stroke-dasharray="3 4" stroke-width="1.2"><path d="M339.6 120 V290"/><path d="M502.8 120 V290"/><path d="M687.0 120 V290"/></g>
  <g fill="currentColor"><text x="300" y="132">① 192~256 p</text><text x="300" y="148">(3~4 MB)</text><text x="455" y="132">② 2048~3072 p</text><text x="455" y="148">(32~48 MB)</text><text x="640" y="166">③ ≥ 32768 p</text></g>
  <circle cx="80" cy="46" r="4" class="f-acc" stroke="currentColor"/><text x="90" y="50" fill="currentColor">spread (P 페이지, 줄 i mod 128)</text>
  <circle cx="80" cy="64" r="4" class="f-ok" stroke="currentColor"/><text x="90" y="68" fill="currentColor">packed (같은 줄 수, P/128 페이지) = 캐시 대조군</text>
  <circle cx="80" cy="82" r="4" class="f-warn" stroke="currentColor"/><text x="90" y="86" fill="currentColor">same (P 페이지, 모두 오프셋 0) = 세트 aliasing</text>
  <circle cx="80" cy="100" r="4" class="f-mut" stroke="currentColor"/><text x="90" y="104" fill="currentColor">quad (페이지당 4 원소: P/4 페이지)</text>
</svg>
```

세 세트에서 반복된 것 (세트 2 = load 34.61, 세트 3 = 34.19 시작):

| 현상 | 세트 1 | 세트 2 | 세트 3 |
|---|---|---|---|
| spread 첫 상승 (≈0.8 → 2 ns 대) | 192 → **256** 페이지 | 192 → **256** | 128 → **192** |
| 같은 구간(256~768)에서 quad | 0.69~1.06 (평탄) | 0.77~1.76 | 0.84~1.18 (평탄) |
| spread 두 번째 큰 상승 (→ 13~24 ns) | 2048 → **3072** | 2048 → **3072** | 2048 → **3072** |
| 그 위 평탄 구간 | 17~30 ns | 16~29 ns | 19~31 ns |
| 수백 MB 이상에서 다시 상승 | 49152 (52 ns), 65536 (109) | 65536 (35) | 49152 (41), 65536 (88) |
| packed 첫 상승 (L1D 넘침) | 1024 (2.99) | 1024 (2.94) | 768~1024 |

해석 (데이터가 말하는 것까지만):

1. **무릎 ①: 약 128~256 페이지 → TLB 커버리지 ≈ 2~4 MB.** spread 만 0.8 → 2.2~2.7 ns 로 오르고 packed(같은 줄 수 = 24~32 KB, 캐시에 다 들어감)는 그대로 1 ns 안쪽. quad(같은 원소·같은 줄 수, 페이지는 1/4 = 64~192 개)도 P = 256~768 내내 약 1 ns 로 평탄했다 → 원인은 줄 수가 아니라 **페이지 수**. (quad 가 "4 배 뒤" 인 P = 1024 에서 오르긴 했지만, 그 자리는 L1D 가 넘치는 자리와 겹쳐 — packed 도 오름 — 그 점만으로는 가를 수 없다. 깨끗한 증거는 "spread 는 오르는데 quad 는 안 오르는 구간" 쪽이다.) 가장 가까운(L1) 데이터 TLB 가 대략 이 정도 페이지를 덮는다고 읽는다. 그 뒤 +1.5~2 ns 는 L1 TLB 미스가 다음 단계 TLB 에서 해결되는 비용으로 본다. Apple 은 TLB 항목 수를 공개하지 않는다 — "약 200 항목 안팎" 이상으로 좁히지 않는다.
2. **P = 1024 근처의 상승은 TLB 가 아니다.** packed 도 같은 자리에서 0.9 → 3 ns 로 올랐다: 줄 1024 × 128 B = 128 KB 로 L1D(128 KB / 64 KB / 96 KB — 코어 종류마다)를 넘은 것. 연습 3 이 이 판정을 코드로 한다 (같은 자리에서 packed 도 2 배 이상 → 캐시 무릎).
3. **무릎 ②: 2048 → 3072 페이지 → 두 번째 단계 TLB 커버리지 ≈ 32~48 MB (추정).** spread 가 8~11 → 17 ns 로 뛰고 packed 는 5~7 ns 로 평탄. 그 뒤 4096~32768 페이지(64~512 MB)는 17~30 ns 평탄: **매 접근이 페이지 워크** 를 하지만 PTE 들(32768 × 8 B = 256 KB)은 아직 캐시에 있어서, spread − packed ≈ 8~22 ns 가 "캐시에 있는 PTE 로 워크 한 번" 의 비용이다. DRAM 한 번(약 100 ns 대)보다 훨씬 작다.
4. **③: 수백 MB 이상.** 49152~65536 페이지에서 spread 가 40~110 ns 로 (세트마다 크게 다름). PTE 512 KB + 데이터 8 MB 가 함께 마지막 캐시 단계를 압박하고 packed 도 14~16 ns 로 오른다 — 워크의 PTE load 자체가 캐시를 놓치기 시작한 것으로 본다. 이 구간은 부하·코어 종류에 가장 민감했다.
5. **`same` 이 보여 준 것.** P = 8 → 12 에서 0.74 → 5.66 ns: 페이지 오프셋 0 의 줄들이 한 L1 세트에 몰려 **8~12 개 사이에서 넘쳤다** (세 세트 모두 12 에서 4.9~6.2 ns). 세트당 way 가 약 8 이라는 것과 맞고, VIPT 로 128 KB L1D 를 16 KB 페이지 안에 두려면 8-way 가 필요하다는 계산(128 / 8 = 16 KB)과도 맞는다 (추정 — Apple 미공개). 이걸 TLB 로 착각하면 "TLB 8 개" 라는 엉뚱한 결론이 나온다.

TLB 커버리지 표 (16 KB 페이지 기준, 4 KB 페이지였다면 같은 항목 수가 1/4 만 덮는다):

| 무릎 | 페이지 수 구간 | 덮는 크기 (16 KB) | 같은 항목 수 × 4 KB | 판정 근거 |
|---|---|---|---|---|
| ① | 128~256 | 2~4 MB | 0.5~1 MB | spread 만 오름, quad·packed 평탄 |
| ② | 2048~3072 | 32~48 MB | 8~12 MB | spread 만 크게, packed 평탄 |
| ③ | 32768 이상 | 512 MB 이상 | — | PTE·데이터의 캐시 압박 (TLB 무릎 아님) |

### Part C — prefault · mlock · 큰 페이지 (demo_fault)

N = 16384 페이지 (256 MiB), 7 회 중앙값, ns/페이지. `prefault` = 그 방법 자체의 시간, `use1` = 그 뒤 첫 쓰기 패스, `use2` = 두 번째 쓰기 패스, `flt` = use1 동안 minflt/페이지, `resident` = prefault 직후 `mincore` 상주 페이지 (중앙값).

| 방법 | 세트 1 prefault | use1 | use2 | flt | resident | 세트 2 prefault / use1 / use2 |
|---|---|---|---|---|---|---|
| 없음 | — | 1242.5 | 207.2 | 1.00 | 0 | — / 1463.1 / 101.6 |
| 쓰기 터치 | 1187.9 | 182.1 | 6.2 | 0.00 | 16384 | 1307.6 / 382.9 / 10.2 |
| memset 전체 | 1391.8 | 239.8 | 8.1 | 0.00 | 16384 | 1382.6 / 220.5 / 7.0 |
| `madvise(MADV_WILLNEED)` | 870.0 | 205.6 | 5.3 | 0.00 | 16384 | 991.8 / 423.7 / 5.4 |
| `mlock` | 950.2 | **7.6** | 5.7 | 0.00 | 16384 | 894.7 / **8.0** / 6.3 |

읽을 점:

- **prefault 는 비용을 없애지 않고 옮긴다.** 어느 방법이든 prefault 자체가 페이지당 0.9~1.4 µs — 폴트 비용 그대로다. 실시간 루프가 아니라 **시작 단계** 에서 내는 게 요점.
- **memset 은 쓰기 터치보다 비싸다** (1392 vs 1188 ns/페이지): 폴트는 같고 페이지당 16 KB 를 실제로 쓰는 시간이 더해진다. 페이지마다 1 바이트로 충분하다.
- **`MADV_WILLNEED` 는 이 맥에서 익명 메모리를 실제로 올렸다** (상주 16384, 체크 하네스에서도 64/64). 하지만 man 페이지가 보장하는 것은 "힌트" 뿐 — 다른 버전에서 다를 수 있으니 상주를 `mincore` 로 확인하고 쓴다. 리눅스의 `MAP_POPULATE` 는 macOS 헤더에 없다 (확인).
- **`mlock` 만 첫 사용이 재접근 수준(7.6~8.0 ns)이었다.** 다른 방법은 use1 이 180~420 ns — Part A 의 "두 번째 접근 커널 진입" 이 prefault 뒤에도 남는다. mlock 은 페이지를 고정(wire)하면서 그 단계까지 끝내는 것으로 보인다 (원인은 추정). 실시간 스레드가 `mlockall` 을 쓰는 이유가 이 맥에서도 숫자로 보인다. 대가: 고정 메모리는 회수·압축이 안 된다 (`RLIMIT_MEMLOCK` 은 이 맥에서 무제한으로 확인).
- **큰 페이지: 사용자 공간에서 고를 방법이 없었다.** `mmap(..., fd = VM_FLAGS_SUPERPAGE_SIZE_2MB)` 와 `_ANY` → `Invalid argument`, `mach_vm_allocate(VM_FLAGS_SUPERPAGE_SIZE_2MB)` → `kr=4 (os/kern) invalid argument`. `MAP_HUGETLB` · `MADV_HUGEPAGE` 는 헤더에 없다. 그래서 이 맥에서 TLB reach 를 늘리는 사용자 수단은 **작업 집합을 페이지 적게 모으기** (packed 처럼)뿐이다.

예산 계산 (`vm_budget`, 체크 표): 64 MiB 버퍼 = 16 KB 페이지 4096 개 → 폴트 1 µs 로 잡으면 약 4.1 ms, 4 KB 페이지였다면 16384 개 → 약 16 ms. 측정한 무릎 ②(약 48 MB = 3072 × 16 KB) 와 비교하면 64 MiB 는 TLB reach 밖 → 무작위 접근이면 접근마다 워크 (+8~22 ns, 위 표). 1 MiB 버퍼는 64 페이지로 무릎 ① 안 → 워크 없음.

펌웨어 쪽 연결: Cortex-A 리눅스 드라이버가 큰 DMA·카메라 버퍼를 **probe 때** 할당하고 만져 두는 것, RT 스레드의 `mlockall` + 스택 prefault, NPU 런타임이 텐서 버퍼를 큰 페이지/연속 메모리로 잡는 것이 전부 이 표의 행이다. MMU 없는 Cortex-M 은 폴트 자체가 없다 (MPU 위반은 오류일 뿐 — ARCH-M15).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

바뀐 곳은 TODO 11 개의 몸체뿐이다 (`pages_for` · `page_floor` · `page_index` · `touch_pages` · `resident_pages` · `sattolo` · `is_single_cycle` · `chase_offset` · `find_knees` · `prefault` · `vm_budget`). 실제 출력:

```text
info  page size: sysconf=16384 getpagesize=16384 vm_page_size=16384
info  after read-only first touch: resident 64/64 pages
info  chain spread P=4096 -> mapping 65536 KiB (4096 pages)
info  chain same   P=4096 -> mapping 65536 KiB (4096 pages)
info  chain packed P=4096 -> mapping 512 KiB (32 pages)
info  chain quad   P=4096 -> mapping 16384 KiB (1024 pages)
info  after madv-willneed: resident 64/64 pages
61/61 passed
```

ASan/UBSan (`cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c`) 실제 출력의 차이는 한 줄이다:

```text
info  after mlock: resident 0/64 pages
61/61 passed
```

ASan 은 `mlock` 을 가로채 **아무것도 하지 않고 0 을 돌려준다** (위 출력: 성공했는데 상주 0). 그래서 하네스는 ASan 빌드에서만 mlock 상주를 체크 대신 정보 줄로 찍는다 (`UNDER_ASAN`). "sanitizer 빌드에서 mlock 이 성공했으니 고정됐다" 는 틀린 결론이다.

## 7. 자주 하는 실수

1. **페이지 크기를 4096 으로 하드코딩.** 리눅스 x86 에서 가져온 "페이지마다 1 바이트" 코드는 이 맥에서 페이지마다 **4 번** 만진다 → 폴트 수는 1/4, "ns/페이지" 는 4 배 작게 나온다 (연습 1: stride 4096 으로 1 MiB 를 256 번 만져도 상주 64 페이지). 고친 코드: `size_t pg = (size_t)sysconf(_SC_PAGESIZE);` 를 쓰고 모든 stride·정렬에 그 값을 쓴다. 펌웨어 쪽 같은 실수: Cortex-A 리눅스 커널이 4 KB / 16 KB / 64 KB 그래뉼로 빌드될 수 있는데 드라이버가 `4096` 을 박아 둔 것 — `PAGE_SIZE` 를 써야 한다.
2. **`(bytes + pg - 1) / pg` 로 올림.** `bytes` 가 사용자 입력이면 `SIZE_MAX` 근처에서 넘쳐 0 페이지 → 0 바이트 매핑 → 그 뒤 쓰기는 다른 메모리를 덮는다. 고친 코드: `bytes / pg + (bytes % pg != 0)` (체크가 `SIZE_MAX` 를 넣는다).
3. **`~(pg - 1)` 을 32 비트로 계산.** `unsigned pg` 이면 `~(pg-1)` = 0xFFFFC000 (32 비트) → `uintptr_t` 로 넓혀질 때 위 32 비트가 0 → 64 비트 주소의 위쪽이 지워진다. 고친 코드: `a & ~(uintptr_t)(pg - 1)`.
4. **Fisher–Yates 로 체이스 순서를 섞음.** 섞기는 균등하지만 사이클이 여러 개 (평균 ln P + 0.58 개) → 0 에서 출발한 체이스는 그중 하나만 돈다. P = 4096 이라 믿고 실제로는 몇백 페이지만 도는 측정이 된다 (연습 2: 이 시드에서 0 의 사이클은 4096 중 1171 개). 고친 코드: Sattolo (`j = rand % i`) + `is_single_cycle` 로 확인.
5. **모든 원소를 페이지 오프셋 0 에.** 이 레슨의 `same` 열이 그 실수다: P = 12 페이지에서 이미 6 ns — TLB 가 아니라 **L1 한 세트가 넘친 것**. 그걸 TLB 미스로 읽으면 "TLB 가 8 개뿐" 이라는 틀린 결론. 고친 코드: `spread` (줄 i mod 128) 와 `packed` 대조군.
6. **독립 load 로 지연을 잼.** `for i: sum += a[idx[i]]` 는 다음 주소가 이전 load 에 의존하지 않아 코어가 여러 미스를 겹친다 (MLP) → 지연이 아니라 처리량을 잰다. Part A 의 패스가 바로 그렇다 (재접근 2~7 ns/페이지 — 메모리 지연보다 훨씬 작다). 지연을 재려면 `p = *(void **)p` 처럼 **의존 사슬**.
7. **TLB 측정 구간에 폴트를 넣음 / Part A 에서 매핑을 재사용.** 체인을 만들면서 모든 페이지를 써서 폴트를 끝낸 뒤 시간을 잰다 (`prefault(PF_TOUCH)` + 예열 2 바퀴). 반대로 Part A 는 실행마다 **새 매핑** 이어야 첫 패스가 폴트다 — 같은 매핑을 7 번 재면 첫 번째만 폴트고 나머지 6 번은 재접근이라 중앙값이 재접근 값이 된다.
8. **리눅스 상식을 그대로 결론으로.** "읽기 먼저 하면 공유 제로 페이지 → 쓰기 때 COW 폴트 한 번 더" 는 리눅스 익명 매핑의 동작이다. 이 맥에서 재 보니 읽기 첫 접근도 폴트 1 번/페이지에 바로 상주가 되고, 그 뒤 쓰기는 `minflt` 0 이었다 (아래 표). 다른 OS 버전·다른 커널에서는 다를 수 있으니 **재고 말한다**.
9. **ASan 빌드에서 `mlock` 성공 = 고정됨.** ASan 은 `mlock` 을 아무것도 안 하는 함수로 바꾼다 (이 레슨 sanitizer 출력: 성공인데 상주 0/64). 실시간 코드의 메모리 고정은 sanitizer 없는 빌드에서 `mincore` 로 확인한다.

## 8. 면접 Q&A

<details><summary>Q1. TLB 미스 하나에 메모리 접근이 몇 번 생기나요?</summary>

하드웨어 워커가 단계마다 디스크립터(PTE) 하나를 읽는다. AArch64 4 KB 그래뉼 48 비트 VA 면 4 단계 → 최대 4 번 + 원래 데이터 1 번. 16 KB 그래뉼은 단계당 11 비트라 47 비트 VA 가 3 단계로 끝난다 (macOS 가 정확히 몇 단계를 쓰는지는 공개 문서로 확인 못 함 — "3~4 단계" 로 말한다). 실제로는 대부분의 PTE load 가 **캐시** (그리고 상위 단계를 기억하는 page-walk cache) 에서 끝나 "4 × DRAM" 이 아니다. 이 맥의 측정: 두 번째 단계 TLB 도 넘는 영역(3072 페이지 이상)에서 spread − packed 가 약 8~22 ns — DRAM 한 번(약 100 ns 대)보다 훨씬 작다 = PTE 가 캐시에 있었다. 1 GiB(65536 페이지)에서 80 ns 이상으로 뛴 건 PTE 들(65536 × 8 B = 512 KiB)과 데이터가 캐시를 함께 넘친 결과로 읽는다.

꼬리: "가상화면?" → 2 단계 변환(stage 1 × stage 2): 게스트의 각 단계 PTE 주소가 다시 stage-2 워크를 해야 해서 4 단계 × 4 단계면 최악 (4+1)(4+1) − 1 = **24 번**. 그래서 하이퍼바이저는 stage 2 를 큰 블록으로 매핑한다 (VIRT 트랙, SOCX-M12 SMMU 의 2 단계와 같은 구조).
</details>

<details><summary>Q2. Apple 은 왜 16 KB 페이지를 쓰나요? 단점은?</summary>

같은 TLB 항목 수로 **4 배** 넓게 덮고 (reach), 같은 메모리에 폴트·PTE 가 1/4 이다 (256 MiB 를 처음 만지면 16384 폴트 vs 4 KB 였다면 65536). 그리고 VIPT L1 캐시: 세트 번호를 변환 전에 고르려면 (캐시 크기 / way 수) ≤ 페이지 크기여야 별칭(alias) 문제가 없다 — 16 KB 페이지면 128 KB L1D 를 8-way 로 만들 수 있고, 4 KB 페이지였다면 32-way 가 필요하다 (Apple 이 이 이유라고 발표한 적은 없다 — 구조상의 논리). 이 레슨의 `same` 열에서 한 세트가 8~12 줄 사이에서 넘친 것도 "way 약 8" 과 맞는다 (추정).

단점: 내부 단편화 (작은 할당도 16 KB 단위 매핑), 4 KB 를 가정한 소프트웨어 (mistake 1), 메모리를 적게 쓰는 프로세스가 많으면 낭비. x86 4 KB 를 가정하는 Rosetta 2 프로세스를 위해 하드웨어가 4 KB 변환도 지원한다고 알려져 있다 — 이 레슨에서 재 보지 않았다.
</details>

<details><summary>Q3. 첫 접근 한 번이 왜 1 µs 나 걸리나요? 줄이려면?</summary>

폴트 한 번 = 예외 진입(EL0→EL1) + VM 맵에서 영역 찾기 + 빈 물리 프레임 확보 + **16 KB 0 채우기** + PTE 쓰기와 배리어(DSB/ISB) + 복귀 + 명령 재실행. 이 맥에서 페이지당 약 0.7~1.4 µs (중앙값), 그 시간의 대부분이 커널 시간(sys)으로 잡혔다 (N = 16384 에서 패스 하나에 sys 약 16 ms ≈ 1 µs × 16384). 0 채우기만 따지면 16 KB 를 수십 GB/s 로 쓰는 데 0.5 µs 안쪽이다 → 나머지가 커널 경로 비용.

줄이는 법: (1) 시작할 때 미리 폴트 (Part C) — 시간이 사라지는 게 아니라 **옮겨진다**. (2) `mlock` — 이 맥에선 prefault 시간이 비슷하고 그 뒤 첫 사용이 재접근 수준(약 6 ns)까지 떨어졌다. (3) 큰 페이지 — 폴트 1 번이 2 MB 를 덮는다 (대신 그 한 번이 길다: 2 MB 0 채우기). (4) 할당을 재사용 (풀, arena) — `free` 후 `munmap` 되는 큰 블록은 다음 할당에서 다시 폴트.
</details>

<details><summary>Q4. 폴트가 끝난 메모리를 두 번째로 만졌는데 왜 공짜가 아니었나요?</summary>

이 맥에서 실제로 본 것: 새 매핑의 첫 패스 직후 두 번째 패스가 페이지당 수십~수백 ns (N 이 클수록, 실행마다 크게 흔들림), **minflt 는 0 인데 커널 시간(sys)은 늘었다**. 세 번째 패스는 2~7 ns. 다른 줄(오프셋 8 KB)을 만져도 느리고, 300 ms 쉬어도 사라지지 않았다 → 캐시나 TLB 가 아니라 **페이지마다 한 번 있는 커널 진입** 이다. `mlock` 으로 고정한 매핑에선 없었다.

해석(추정, 확인 못 함): XNU 의 pmap 이 참조/수정 상태를 소프트웨어로 관리하면서(ARM 의 Access Flag 등), 일반 폴트 경로에서 들어온 페이지의 다음 접근에 VM 폴트 카운터에 안 잡히는 가벼운 트랩을 낸다는 가설. 확인하려면 커널 트레이스(dtrace/kperf — SIP 때문에 이 맥에선 못 함)나 XNU 소스의 해당 경로를 봐야 한다. 면접 포인트는 결론이 아니라 **방법**: 시간 + 폴트 카운터 + sys 시간 + 대조 실험(다른 줄, 쉬었다가, mlock)을 같이 보면 "무엇이 아닌지" 는 확정할 수 있다.
</details>

<details><summary>Q5. 실시간 제어 루프(예: 1 kHz 모터 제어 스레드)에서 페이지 폴트를 어떻게 없애나요?</summary>

리눅스: 시작 단계에서 `mlockall(MCL_CURRENT | MCL_FUTURE)` → 스택을 최대 깊이만큼 미리 만지기(큰 지역 배열을 memset) → 힙도 필요한 만큼 할당하고 만지기 → 루프 안에서는 `malloc`/`mmap`/파일 I/O 금지 → `/proc/<pid>/stat` 의 minflt·majflt 가 루프 동안 늘지 않는지 테스트로 확인 (PREEMPT_RT 가이드의 표준 절차). 이 맥에서 본 숫자로 크기를 말하면: 폴트 1 번 ≈ 1 µs, 1 kHz 루프 예산 1000 µs 에서 새 페이지 수백 개면 예산이 사라진다.

꼬리: "TLB 는?" → 폴트를 없애도 작업 집합이 TLB reach 를 넘으면 매 접근 수 ns~수십 ns 가 붙는다. 데이터를 몰아 두고(packed 처럼), 가능하면 큰 페이지. "MCU 는?" → Cortex-M 은 MMU 가 없어 페이지 폴트가 없다 — MPU 위반은 MemManage 폴트(ARCH-M15)로 오류일 뿐, 페이징이 아니다. 비용 예측성이 MCU 를 실시간에 쓰는 이유 중 하나다.
</details>

<details><summary>Q6. TLB reach 를 쟀다고 했는데, 그게 캐시 효과가 아니라는 걸 어떻게 아나요?</summary>

세 가지 대조: (1) `packed` — 같은 줄 수(같은 캐시 발자국)를 최소 페이지에 → P = 256~768 에서 packed 는 약 1 ns 그대로인데 spread 만 2~5 ns 로 올랐다 = 페이지 수만 다른 두 실험의 차이. (2) `quad` — 페이지당 원소 4 개 → 같은 P(같은 줄 수)에서 페이지가 1/4. spread 가 2~5 ns 로 오른 P = 256~768 에서 quad 는 약 1 ns 그대로였다 = 줄 수가 아니라 페이지 수. (quad 가 4 배 뒤인 P = 1024 에서 오른 것은 L1D 넘침과 겹쳐 따로 증거로 쓰지 않았다 — 대조군도 다른 효과와 겹치는 점이 있으면 그 점은 버린다.) (3) `same` — 일부러 aliasing 을 만든 대조군으로, 그 곡선이 spread 와 전혀 다르게 생겼다는 것으로 spread 의 줄 배치가 세트 충돌을 피했음을 확인.

한계: L1 TLB 와 L2 TLB, page-walk cache 가 각각 다른 무릎을 만들고 데이터 캐시(P ≥ 1024 에서 128 KB 이상) 넘침과 겹친다. 코어가 P/E 사이를 옮겨 다니면 (macOS 는 고정 불가, QoS 는 힌트) 곡선이 섞인다. 그래서 "L1 dTLB 는 정확히 N 항목" 이라고 말하지 않고 "**약 128~256 페이지(2~4 MB) 근처에서 첫 상승**" 처럼 구간으로 말한다.
</details>

<details><summary>Q7. 2 MB / 32 MB 블록(huge page)을 쓰면 뭐가 바뀌고, 이 맥에선 되나요?</summary>

AArch64 16 KB 그래뉼: L3 페이지 16 KB, L2 블록 = 2048 × 16 KB = **32 MB**, 그리고 연속(contiguous) 힌트 비트로 L3 항목 128 개(= 2 MB)를 TLB 항목 하나로 합칠 수 있다 (Arm ARM 의 그래뉼별 표 — 숫자는 이 레슨 연습 4 에서 계산만 했다). 1 GiB 를 덮는 항목: 16 KB 페이지 65536 개 vs 32 MB 블록 32 개 → TLB reach 문제가 사실상 사라지고 워크도 한 단계 짧다.

이 맥(macOS 사용자 공간): `MAP_HUGETLB`·`MADV_HUGEPAGE`·`MAP_POPULATE` 는 헤더에 없고, `mmap` 의 `VM_FLAGS_SUPERPAGE_SIZE_2MB` / `_ANY` 와 `mach_vm_allocate(..., VM_FLAGS_SUPERPAGE_SIZE_2MB)` 는 모두 **EINVAL / KERN_INVALID_ARGUMENT** 였다 (demo_fault 출력). 즉 사용자가 고를 수 있는 큰 페이지는 없다 — 커널이 내부적으로 연속 힌트 등을 쓰는지는 밖에서 알 수 없다. 리눅스는 THP(자동) 와 hugetlbfs(예약)가 있고, 비용은 폴트 한 번에 2 MB 0 채우기(지연 스파이크), 단편화와 compaction 이다.
</details>

<details><summary>Q8. 컨텍스트 스위치 뒤 첫 몇 µs 가 느린 이유를 TLB 관점에서?</summary>

ASID(주소 공간 번호)가 없으면 스위치마다 TLB 를 비워야 하고, 있어도 다른 프로세스가 TLB 항목을 밀어냈으면 다시 워크한다. 스위치 직후 작업 집합이 수백 페이지면 이 레슨 숫자로 수백 × (수 ns ~ 20 ns) = 수 µs. 모델과 ASID 롤오버는 ARCH-A02 가 다룬다 (다음 단계). 꼬리: "커널 페이지는?" → 전역(global) 항목은 ASID 와 무관하게 남는다.
</details>

## 9. 추가 연습

### 연습 1 — 4 KB 를 가정한 코드를 16 KB 페이지에서

`pages_touched(len, stride, pg)` — 주소 0, stride, 2·stride, … (< len) 을 만질 때 서로 다른 페이지 수. 1 MiB 를 stride 100 / 4096 / 16384 / 20000 / 65536 으로 만지면 몇 페이지가 폴트되나? 공식과 `mincore` 가 같아야 한다.

<details><summary>답</summary>

터치 횟수 n = ⌈len / stride⌉. stride ≥ pg 면 터치마다 다른 페이지 → n. stride < pg 면 이웃 터치 사이가 한 페이지보다 짧아 **건너뛰는 페이지가 없다** → 마지막 터치 주소의 페이지 번호 + 1 = ⌊(n−1)·stride / pg⌋ + 1. 1 MiB (64 × 16 KB): stride 100 → 10486 번 만져 64 페이지, 4096 → 256 번에 **64 페이지** (4 KB 페이지였다면 256), 16384 → 64, 20000 → 53 (터치마다 다른 페이지), 65536 → 16. 실제 출력의 `resident` 가 공식과 전부 같았다. 4 KB 를 가정해 "256 폴트 예상" 으로 쓴 테스트는 이 맥에서 틀린다 (mistake 1).
</details>

### 연습 2 — Fisher–Yates 로 섞으면

같은 xorshift 로 Fisher–Yates(`j = rand % (i+1)`) 순열을 만들어 사이클 수를 세고 (`count_cycles`), 0 에서 출발한 체이스가 몇 원소를 도는지 (`cycle_len_from`) 구하라. Sattolo 와 비교.

<details><summary>답</summary>

무작위 순열의 사이클 수 기댓값은 조화수 H_P ≈ ln P + 0.577 (P = 4096 이면 약 8.9), 원소 0 이 속한 사이클 길이는 1..P 에 균등. 이 시드에서는 실제로 **사이클 5 개, 0 에서 출발하면 4096 중 1171 개** 만 돈다 → "P = 4096 페이지" 라 적은 측정이 실제로는 약 1171 페이지를 잰 것. Sattolo 는 항상 사이클 1 개 · 길이 4096. 이 실수는 결과가 그럴듯하게 나와서 잘 안 잡힌다 — `is_single_cycle` 을 측정 코드 안에서 매번 부르는 이유 (demo_tlb 의 `measure` 가 그렇게 한다).
</details>

### 연습 3 — 실측 표에서 TLB 무릎 고르기

demo_tlb 세트 1 의 spread · packed 열(29 점)을 배열로 넣고 `find_knees(…, ratio 2.0)` 로 무릎을 찾은 뒤, 각 무릎을 `classify_knee` 로 판정하라: 같은 자리에서 packed 도 2 배 이상 올랐으면 캐시('C'), 아니면 페이지 수('T'). 첫 TLB 무릎을 커버리지(바이트)로 바꾸고, 같은 항목 수가 4 KB 페이지였다면 얼마를 덮나?

<details><summary>답</summary>

무릎 4 개: P = 256 [T] (3~4 MiB), 1024 [C] (packed 0.87 → 2.99: L1D 넘침), 3072 [T] (32~48 MiB), 49152 [T] (512~768 MiB, PTE·데이터의 캐시 압박이 섞인 구간). 첫 TLB 무릎은 192 → 256 페이지 사이 → 커버리지 3~4 MiB. 같은 항목 수 × 4 KB = 0.75~1 MiB — 16 KB 페이지가 TLB reach 를 4 배로 만든다는 말의 숫자. ratio 를 1.25 로 낮추면 노이즈(작은 P 에서 0.7 → 1.0 ns 출렁임)도 무릎으로 잡힌다 (demo 출력의 ratio 1.25 줄). 노이즈가 큰 공유 기계에서는 문턱을 올리고, 세 세트에서 반복되는 무릎만 믿는다.
</details>

### 연습 4 — 큰 페이지 계산

1 GiB 를 덮는 데 필요한 항목 수: 4 KB / 16 KB 페이지, 2 MB (16 KB 그래뉼의 contiguous 128 개), 32 MB (16 KB 그래뉼 L2 블록). 그리고 마지막 단계(L3) 테이블만 센 페이지 테이블 바이트: 4 KB 그래뉼 vs 16 KB 그래뉼.

<details><summary>답</summary>

항목 수: 4 KB → 262144, 16 KB → 65536, 2 MB → 512, 32 MB → **32**. L3 테이블: 16 KB 그래뉼은 테이블 한 장에 2048 항목 → 65536 / 2048 = 32 장 × 16 KB = **512 KiB**; 4 KB 그래뉼은 한 장 512 항목 → 262144 / 512 = 512 장 × 4 KB = **2 MiB**. 이 맥에서 측정한 무릎 ③(수백 MB 부터 다시 상승)이 바로 "PTE 512 KiB + 데이터" 가 캐시를 압박하는 지점이다. 32 MB 블록이라면 PTE 는 32 개(256 B) — 이 문제가 사라진다. 다만 macOS 사용자 공간에서는 그걸 요청할 방법이 없었다 (Part C).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr     # solution.c 와 같은 폴더에서 (포함함)
```

실제 출력:

```text
info  ex1 stride   100: 10486 touches -> resident 64 pages (formula 64)
info  ex1 stride  4096:  256 touches -> resident 64 pages (formula 64)
info  ex1 stride 16384:   64 touches -> resident 64 pages (formula 64)
info  ex1 stride 20000:   53 touches -> resident 53 pages (formula 53)
info  ex1 stride 65536:   16 touches -> resident 16 pages (formula 16)
info  ex2 Fisher-Yates P=4096: 5 cycles, chase from 0 covers 1171 of 4096 elements
info  ex3 spread knees (ratio 2): P=256[T] (3-4 MiB) P=1024[C] (12-16 MiB) P=3072[T] (32-48 MiB) P=49152[T] (512-768 MiB)
16/16 passed
```

## 10. 핵심 정리

- 페이지 크기는 런타임에: 이 맥은 `sysconf` = `getpagesize` = `vm_page_size` = **16384**. 4096 하드코딩은 폴트 수를 1/4 로 잘못 센다.
- 첫 접근 = 페이지당 폴트 1 번 ≈ **0.7~1.4 µs** (대부분 커널 시간). 읽기 먼저여도 폴트 1 번이고 다음 쓰기는 폴트 0 — 이 맥에선 "제로 페이지 → COW" 가 관찰되지 않았다.
- 폴트 직후 두 번째 접근에도 카운터에 안 잡히는 커널 진입이 있을 수 있다 (세트 1: 120~290 ns/페이지, 세트 2: 거의 0). 재고, 대조 실험으로 "무엇이 아닌지" 를 확정하고, 원인은 추정으로 말한다.
- TLB reach 측정 = 의존 체인 + Sattolo 한 사이클 + 페이지마다 줄 회전 + packed/quad 대조군. 0.7 ns(L1 히트)에서 시작.
- 이 맥의 무릎: ① 128~256 페이지 (2~4 MB, +1.5~2 ns), ② 2048~3072 페이지 (32~48 MB, 이후 매 접근 워크 +8~22 ns), ③ 수백 MB (PTE 가 캐시를 놓침). 1024 근처 상승은 L1D(캐시) 무릎.
- `same` 배치는 P = 12 에서 이미 6 ns — 세트 aliasing 을 TLB 로 착각하지 말 것.
- prefault 는 비용을 시작 단계로 옮길 뿐 (≈ 폴트 비용 그대로). `mlock` 만 첫 사용이 재접근 수준(≈ 8 ns). `MADV_WILLNEED` 는 여기선 실제로 올렸지만 힌트. `MAP_POPULATE`·huge page 요청은 macOS arm64 사용자 공간에 없음/EINVAL.
- 16 KB 페이지 = 같은 TLB 항목으로 4 배 reach, 폴트·PTE 1/4, VIPT L1 을 크게 (추정). 다음 단계: ARCH-A01/A02 (워커·TLB 모델), MEM-B10, LPERF-B09, LLAT-A05.

## 참고자료

- Apple Developer, "Addressing Architectural Differences in Your macOS Code" — Apple silicon 의 16 KB 페이지, `getpagesize()`/`vm_page_size` 를 쓰라는 안내 — <https://developer.apple.com/documentation/apple-silicon/addressing-architectural-differences-in-your-macos-code>
- macOS man pages: `mmap(2)`, `mincore(2)` (`MINCORE_INCORE`), `madvise(2)`, `mlock(2)`, `getrusage(2)` (`ru_minflt`), `sysconf(3)`. 헤더: `<mach/vm_statistics.h>` 의 `VM_FLAGS_SUPERPAGE_*` (이 맥 arm64 에서는 EINVAL — demo_fault).
- Arm Architecture Reference Manual for A-profile (DDI 0487) — AArch64 VMSA 장: 변환 그래뉼 4/16/64 KB, 단계별 인덱스 비트, 블록 크기, Contiguous bit, Access flag (장 번호는 판본마다 다름) — <https://developer.arm.com/documentation/ddi0487/latest>
- U. Drepper, "What Every Programmer Should Know About Memory", 2007 — §4 Virtual Memory (TLB, 페이지 워크 비용), §3 의 포인터 체이스 측정 방법.
- A. Bhattacharjee, D. Lustig, *Architectural and Operating System Support for Virtual Memory*, Synthesis Lectures on Computer Architecture, 2017 — TLB 계층, page-walk cache, 큰 페이지, 2 단계 변환 (24 번 접근).
- S. Sattolo, "An algorithm to generate a random cyclic permutation", Information Processing Letters 22(6), 1986.
- L. McVoy, C. Staelin, "lmbench: Portable Tools for Performance Analysis", USENIX ATC 1996 — `lat_mem_rd` 의 의존 load 체인.
- XNU 소스 (apple-oss-distributions/xnu): `osfmk/vm/vm_fault.c`, `osfmk/arm64/` 의 pmap — Q4 의 가설을 확인하려면 볼 곳 (이 레슨에서는 확인하지 않음).
- Linux: `mmap(2)` 의 `MAP_POPULATE`, `Documentation/admin-guide/mm/transhuge.rst` (THP), PREEMPT_RT 의 "memory locking / prefaulting" 안내 — 비교 대상.
- 관련 레슨: OS-B09 (mmap 과 페이지당 폴트), OS-B10 (단일 레벨 페이지 테이블), ARCH-B10 (AMAT), ARCH-M04~M07 (캐시), ARCH-M15 (MPU), ARCH-A01 (페이지 테이블 워커 모델), ARCH-A02 (TLB · ASID 모델). 다음 단계: MEM-B10 (포인터 체이싱 지연), LPERF-B09 (minor/major fault), LLAT-A05 (꼬리 지연: page fault · TLB), OS-M03 (COW fork), OS-A06 (사용자 공간 폴트 처리), RISCV-M12 (Sv39 워커), SOCX-M12 (SMMU 2 단계).
