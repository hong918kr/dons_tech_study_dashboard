---
id: MCPP-M13
time: 95분
updated: 2026-11-08
---
# MCPP-M13 메모리 모델 규칙: fence·release sequence

## 학습 목표

- happens-before 를 만드는 표준 규칙 네 가지 — op–op([atomics.order]/2), fence–fence·fence–op·op–fence([atomics.fences]/2–4) — 를 각각 코드 한 줄로 쓰고, arm64 에서 `stlr`/`ldapr` 와 `dmb ish`/`dmb ishld` 중 무엇이 나오는지 실제 asm 으로 말할 수 있다.
- **release sequence** 의 C++20 정의(머리 뒤로 **RMW 만** 이어진다, [intro.races]/5)를 설명하고, "relaxed RMW 는 사슬을 잇고 relaxed store 는 끊는다", "C++11 에서 되던 같은 스레드 store 가 C++20 에선 레이스" 를 예로 보일 수 있다.
- C 와 공유하는 plain struct 필드(DMA 디스크립터 ctrl 워드, 참조 카운트)에 `std::atomic_ref` 를 쓰고 그 규칙(정렬 `required_alignment`, 공유 중엔 atomic_ref 로만 접근, [atomics.ref.generic]/3)을 지킨다.
- 2~3 스레드 프로그램의 **모든 SC 인터리빙** 을 돌며 sb + sw → hb 를 계산해 C++ 정의의 data race([intro.races]/21)를 찾는 탐색기를 직접 만들고, 같은 프로그램을 TSan 에 돌려 **어디서 일치하고 어디서 어긋나는지**(fence 오탐, release sequence 놓침) 실측으로 정리한다.
- SC 만 보는 탐색이 **약한 메모리 실행의 레이스를 못 본다** 는 것을 반례(SB 가드)로 보이고, DRF-SC 정리가 언제 성립하는지 말할 수 있다.

## 1. 문제

"`atomic_thread_fence` 와 release store 는 뭐가 다르죠? release sequence 가 뭔가요? 참조 카운트를 왜 `fetch_sub(release)` + `fence(acquire)` 로 쓰죠?" — C++ 비중이 큰 시스템/펌웨어 면접(NVIDIA·Apple·HFT)에서 C-A03 의 acquire/release 다음에 나오는 질문이다. 정답은 "느낌" 이 아니라 **표준의 규칙 몇 줄** 이다. 그 규칙을 정확히 알면 DMA 디스크립터 폴링(여러 칸을 relaxed 로 훑고 배리어 한 번), 참조 카운트, 작업 큐 같은 펌웨어 코드를 "왜 맞는지" 증명할 수 있고, 도구(TSan)가 틀리는 지점도 알아챈다.

C-A03 은 "release store → 그 값을 읽은 acquire load" 하나로 데이터를 넘겼다. 이 레슨은 그 한 줄을 일반화한다. C++20 표준에서 **스레드 사이 happens-before 의 재료는 synchronizes-with(sw) 뿐** 이고(consume 제외), sw 를 만드는 규칙은 아래 네 가지가 전부다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">synchronizes-with 를 만드는 네 가지 모양 (원자 객체 f 를 통한 값 전달이 반드시 있어야 한다)</text>
  <g>
    <text x="14" y="42" fill="currentColor" font-weight="bold">① op–op  [atomics.order]/2</text>
    <rect x="14" y="52" width="160" height="24" rx="3" class="f-mut" stroke="currentColor"/>
    <text x="20" y="68" fill="currentColor" font-family="monospace">data = v;</text>
    <rect x="14" y="82" width="160" height="24" rx="3" class="f-acc" stroke="currentColor"/>
    <text x="20" y="98" fill="currentColor" font-family="monospace">f.store(1, release)</text>
    <rect x="200" y="82" width="160" height="24" rx="3" class="f-acc" stroke="currentColor"/>
    <text x="206" y="98" fill="currentColor" font-family="monospace">f.load(acquire)==1</text>
    <rect x="200" y="112" width="160" height="24" rx="3" class="f-mut" stroke="currentColor"/>
    <text x="206" y="128" fill="currentColor" font-family="monospace">use(data)</text>
    <path d="M174 94 L198 94" stroke="currentColor" stroke-width="2"/>
    <path d="M190 89 L198 94 L190 99" stroke="currentColor" fill="none" stroke-width="2"/>
  </g>
  <g>
    <text x="384" y="42" fill="currentColor" font-weight="bold">② fence–op  [atomics.fences]/3</text>
    <rect x="384" y="52" width="160" height="24" rx="3" class="f-mut" stroke="currentColor"/>
    <text x="390" y="68" fill="currentColor" font-family="monospace">data = v;</text>
    <rect x="384" y="82" width="160" height="24" rx="3" class="f-warn" stroke="currentColor"/>
    <text x="390" y="98" fill="currentColor" font-family="monospace">fence(release)</text>
    <rect x="384" y="112" width="160" height="24" rx="3" class="f-ok" stroke="currentColor"/>
    <text x="390" y="128" fill="currentColor" font-family="monospace">f.store(1, relaxed)</text>
    <rect x="570" y="112" width="160" height="24" rx="3" class="f-acc" stroke="currentColor"/>
    <text x="576" y="128" fill="currentColor" font-family="monospace">f.load(acquire)==1</text>
    <rect x="570" y="142" width="160" height="24" rx="3" class="f-mut" stroke="currentColor"/>
    <text x="576" y="158" fill="currentColor" font-family="monospace">use(data)</text>
    <path d="M544 94 L568 120" stroke="currentColor" stroke-width="2"/>
    <path d="M559 117 L568 120 L566 111" stroke="currentColor" fill="none" stroke-width="2"/>
  </g>
  <g>
    <text x="14" y="192" fill="currentColor" font-weight="bold">③ op–fence  [atomics.fences]/4</text>
    <rect x="14" y="202" width="160" height="24" rx="3" class="f-mut" stroke="currentColor"/>
    <text x="20" y="218" fill="currentColor" font-family="monospace">data = v;</text>
    <rect x="14" y="232" width="160" height="24" rx="3" class="f-acc" stroke="currentColor"/>
    <text x="20" y="248" fill="currentColor" font-family="monospace">f.store(1, release)</text>
    <rect x="200" y="232" width="160" height="24" rx="3" class="f-ok" stroke="currentColor"/>
    <text x="206" y="248" fill="currentColor" font-family="monospace">f.load(relaxed)==1</text>
    <rect x="200" y="262" width="160" height="24" rx="3" class="f-warn" stroke="currentColor"/>
    <text x="206" y="278" fill="currentColor" font-family="monospace">fence(acquire)</text>
    <rect x="200" y="292" width="160" height="24" rx="3" class="f-mut" stroke="currentColor"/>
    <text x="206" y="308" fill="currentColor" font-family="monospace">use(data)</text>
    <path d="M174 244 L198 270" stroke="currentColor" stroke-width="2"/>
    <path d="M189 267 L198 270 L196 261" stroke="currentColor" fill="none" stroke-width="2"/>
  </g>
  <g>
    <text x="384" y="192" fill="currentColor" font-weight="bold">④ fence–fence  [atomics.fences]/2</text>
    <rect x="384" y="202" width="160" height="24" rx="3" class="f-mut" stroke="currentColor"/>
    <text x="390" y="218" fill="currentColor" font-family="monospace">data = v;</text>
    <rect x="384" y="232" width="160" height="24" rx="3" class="f-warn" stroke="currentColor"/>
    <text x="390" y="248" fill="currentColor" font-family="monospace">fence(release)</text>
    <rect x="384" y="262" width="160" height="24" rx="3" class="f-ok" stroke="currentColor"/>
    <text x="390" y="278" fill="currentColor" font-family="monospace">f.store(1, relaxed)</text>
    <rect x="570" y="232" width="160" height="24" rx="3" class="f-ok" stroke="currentColor"/>
    <text x="576" y="248" fill="currentColor" font-family="monospace">f.load(relaxed)==1</text>
    <rect x="570" y="262" width="160" height="24" rx="3" class="f-warn" stroke="currentColor"/>
    <text x="576" y="278" fill="currentColor" font-family="monospace">fence(acquire)</text>
    <rect x="570" y="292" width="160" height="24" rx="3" class="f-mut" stroke="currentColor"/>
    <text x="576" y="308" fill="currentColor" font-family="monospace">use(data)</text>
    <path d="M544 244 L568 270" stroke="currentColor" stroke-width="2"/>
    <path d="M559 267 L568 270 L566 261" stroke="currentColor" fill="none" stroke-width="2"/>
  </g>
</svg>
```

화살표의 출발은 **release 쪽** (release 연산 또는 그 앞의 release fence), 도착은 **acquire 쪽** (acquire 연산 또는 그 뒤의 acquire fence). 연결 고리는 언제나 "원자 객체 f 에 쓴 값을 다른 스레드가 읽었다" 다. 그리고 "쓴 값" 은 정확히는 **그 쓰기가 이끄는 release sequence 안의 아무 값** 이다 — 이게 이 레슨의 두 번째 주제.

| 규칙 | 출발(A) | 도착(B) | 연결 조건 | 표준 (N4861) |
|---|---|---|---|---|
| op–op | release 연산 A on M | acquire 연산 B on M | B 가 A 의 release sequence 안의 값을 읽음 | [atomics.order]/2 |
| fence–op | release fence A | acquire 연산 B on M | A sb X(M 수정), B 가 X 의 (가상) release sequence 값을 읽음 | [atomics.fences]/3 |
| op–fence | release 연산 A on M | acquire fence B | X(M 읽기) sb B, X 가 A 의 release sequence 값을 읽음 | [atomics.fences]/4 |
| fence–fence | release fence A | acquire fence B | A sb X(M 수정), Y sb B, Y 가 X 의 (가상) release sequence 값을 읽음 | [atomics.fences]/2 |

호스트(맥, Apple M6)에서는 `std::thread` 하나를 코어 하나로 본다.

### Part A — fence 로 만든 메일박스와 RMW 가 잇는 release sequence

- `fmb_try_send / fmb_try_recv` — 1칸 메일박스. `full` 의 load/store 는 **전부 relaxed**, 순서는 fence 로만 만든다. 게시 방향(slot 쓰기 → full=1)과 반납 방향(slot 읽기 → full=0) **둘 다** fence–fence. 20만 개 메시지가 순서대로, 안 찢어지고 도착.
- `board_publish / board_claim` — 생산자가 `job[0..n)` 을 채우고 `remaining.store(n, release)`. 작업자 4 개가 `fetch_sub(1, acquire)` 로 하나씩 가져간다. 두 번째 이후 작업자는 생산자의 store 가 아니라 **다른 작업자의 RMW 가 쓴 값** 을 읽는데도 생산자와 동기화된다 — release sequence. 10만 작업이 정확히 한 번씩, 내용 일치.

### Part B — SC 인터리빙 전수 탐색기 (이 레슨의 본체)

- 프로그램 = 스레드별 **스텝 목록** (비원자 W/R, 원자 ST/LD/AWAIT/RMW, FENCE, 레지스터 가드 `if_eq / if_gt`). 2~3 스레드, 스레드당 몇 스텝.
- `in_release_seq` (C++20 / C++11 / "RMW 규칙 끔" 세 모드), `build_sw` (네 규칙), `find_race` (hb = (sb ∪ sw)⁺, 충돌하는 비원자 쌍 중 hb 순서 없는 것), `dfs` (모든 인터리빙).
- 16 개 프로그램·규칙 조합의 **실행 수와 레이스 실행 수** 가 손 계산과 맞아야 한다 (예: `rs_rmw` 16 실행, 레이스 0 / RMW 규칙을 끄면 6).
- 같은 프로그램을 `demo_tsan.cpp` 로 진짜 스레드 + TSan 에 돌려 표로 대조한다.

### Part C — DMA 디스크립터 링: plain struct + `atomic_ref` + op–fence

C 드라이버와 공유하는 `struct dma_desc { addr, len, flags, ctrl, status }` (16 B, atomic 타입 없음). CPU 는 칸을 채우고 `atomic_ref(ctrl).store(OWN|seq, release)`, "DMA 엔진" 스레드는 `tail` 부터 OWN 인 칸들을 **relaxed 로 연속 스캔한 뒤 acquire fence 한 번**, 처리하고 `DONE|seq` 를 release-store, CPU 는 acquire 로 회수한다. 8칸 링에 10만 패킷: 전부 순서대로, status(체크섬) 일치.

## 2. Problem (English)

> **The rules that create happens-before: fences, release sequences, atomic_ref — and a tiny race checker.**
>
> 1. Implement a single-slot mailbox whose flag is accessed only with `memory_order_relaxed`; all ordering must come from `std::atomic_thread_fence` (both the publish and the return direction). Implement a job board: `board_publish(b, n)` fills plain `job[0..n)` and stores `n` with release; `board_claim(b)` returns a unique index via `fetch_sub(1, acquire)` or −1. Explain why the 2nd…Nth claimer is synchronized with the producer although it reads a value written by another claimer.
> 2. Write an exhaustive explorer for small programs (≤3 threads, step lists of `W/R` non-atomic, `ST/LD/AWAIT/RMW` atomic with a memory order, `FENCE`, register guards). For every sequentially consistent interleaving, build sequenced-before, reads-from and modification order, derive synchronizes-with from the four rules in [atomics.order]/[atomics.fences] using C++20 release sequences, close happens-before, and report a data race per [intro.races]: two conflicting non-atomic accesses from different threads not ordered by happens-before. Report `execs`, `racy` and the first racy trace. Make the release-sequence rule switchable (C++20 / C++11 / off) and fences switchable.
> 3. A TX descriptor ring shared with C code (`struct dma_desc` with a plain `uint32_t ctrl`). Use `std::atomic_ref<uint32_t>` for `ctrl`. The DMA engine scans consecutive owned descriptors with relaxed loads, issues **one** acquire fence per batch, writes `status`, and hands each descriptor back with a release store. 100 000 packets through an 8-entry ring must be reaped in order with matching status.
>
> Follow-ups: When is a fence better than an acquire load? Does a release fence followed by a `volatile` store synchronize? What changed in release sequences in C++20? Why does TSan report your fence-based code and stay silent on a C++20 race? Is "no race in any SC interleaving" a proof of race freedom?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력 52줄 중 가운데 28줄 생략):

```text
FAIL starter.cpp:345  fmb_try_send(m, make_msg(1))
FAIL starter.cpp:347  fmb_try_recv(m, out) && out.seq == 1u && msg_ok(out)
FAIL starter.cpp:348  fmb_try_send(m, make_msg(2)) && fmb_try_recv(m, out) && out.seq == 2u
FAIL starter.cpp:353  b->job[0] == job_value(0) && b->job[2] == job_value(2)
FAIL starter.cpp:355  c1 == 2 && c2 == 1 && c3 == 0 && c4 == -1
SKIP 3 stress checks (Part A): unit checks failed
FAIL starter.cpp:402  build_sw(t1, 4, Rules{}, sw) == 1 && sw[1] == (1ull << 2)
FAIL starter.cpp:405  build_sw(t1, 4, Rules{}, sw) == 0 && find_race(t1, 4, Rules{}, &ra, &rb) && ra == 0 && rb == 3
FAIL starter.cpp:411  in_release_seq(t2, 0, 1, Rules{}) && !in_release_seq(t2, 0, 2, Rules{})
FAIL starter.cpp:412  in_release_seq(t2, 0, 2, c11)
FAIL starter.cpp:413  !in_release_seq(t2, 0, 1, normm) && in_release_seq(t2, 0, 0, Rules{})
Part B explorer:
  mp_relaxed      fences=1 rmw_rs=1 cxx11=0 : execs=0 racy=0 blocked=0
FAIL starter.cpp:420  expect(prog_mp_relaxed, cxx20, 3, 1)
...
FAIL starter.cpp:446  tx_submit(*r, p1, 5, 10) && tx_submit(*r, p2, 3, 11)
FAIL starter.cpp:447  r->d[0].ctrl == (OWN | 10u) && r->d[0].len == 5u
FAIL starter.cpp:449  dma_poll(*r) == 2u && dma_poll(*r) == 0u
FAIL starter.cpp:450  tx_reap(*r, &st) == 10 && st == pkt_sum(p1, 5)
FAIL starter.cpp:451  tx_reap(*r, &st) == 11 && st == pkt_sum(p2, 3) && r->d[0].ctrl == 0u
FAIL starter.cpp:454  ok == (int)kRing
SKIP 2 stress checks (Part C): unit checks failed
6/43 passed
```

하네스 설계: 멀티스레드 스트레스는 같은 Part 의 단일 스레드 단위 테스트가 다 통과해야 돈다 — 시작 코드는 멈추지 않고 exit 1 로 끝난다. 탐색기 테스트는 결정적이다 (같은 입력 → 같은 실행 수). Part B 의 `t1`/`t2` 단위 테스트는 **손으로 만든 실행 흔적** 으로 `build_sw` / `find_race` / `in_release_seq` 를 탐색(`dfs`)과 따로 검사한다.

TSan 빌드에서 쓰는 `tsan_acquire / tsan_release` 는 하네스에 있다. 일반 빌드에서는 빈 함수다 (이유는 5. 풀이 Part B).

관찰용 파일:

```bash
c++ -std=c++20 -g -O1 -fsanitize=thread -pthread demo_tsan.cpp -o dt && ./dt <mode>   # 탐색기 프로그램과 같은 모양, 진짜 스레드 + TSan
c++ -std=c++20 -O2 -pthread demo_tsan.cpp -o dt_n && ./dt_n sb_guarded                 # TSan 없이 SB 가드 실측
c++ -std=c++20 -O2 -S demo_fence_asm.cpp -o -                                           # op vs fence arm64 명령
```

## 4. 힌트

<details>
<summary>힌트 1 — fence 메일박스: 어느 쪽에 어떤 fence?</summary>

규칙은 "release 쪽은 fence 가 **원자 store 앞**, acquire 쪽은 fence 가 **원자 load 뒤**". 보내는 쪽: `full` 이 0 인지 relaxed 로 보고 → (반납과 동기화하려면) acquire fence → slot 쓰기 → release fence → `full=1` relaxed. 받는 쪽은 거울상. fence 는 "어떤 변수" 에 묶이지 않으므로 TSan 용 주석에 동기화 주소(`&m.full`)를 따로 알려 준다.

</details>

<details>
<summary>힌트 2 — 작업 보드: 두 번째 작업자는 누구와 동기화하나?</summary>

`remaining` 의 수정 순서는 `store(n)` → RMW → RMW → … 이다. 두 번째 작업자의 RMW 가 읽은 값은 첫 작업자의 RMW 가 쓴 값이고, 그 RMW 는 생산자의 store 가 이끄는 release sequence 안에 있다. 그러니 acquire RMW 하나로 충분하다 (작업자 RMW 가 release 일 필요 없음).

</details>

<details>
<summary>힌트 3 — in_release_seq / build_sw</summary>

SC 실행에서 한 원자 객체의 수정 순서 = 흔적(trace) 안에서 그 객체에 대한 쓰기(ST, RMW)의 순서. `head` 다음부터 `w` 까지 그 객체의 쓰기가 전부 RMW 면 포함 (C++11 모드에선 "head 와 같은 스레드의 쓰기" 도 허용). `build_sw` 는 읽기 b 마다, b 가 읽은 쓰기 `rf` 를 포함하는 release sequence 의 머리 a 를 모두 찾고, 출발 집합 {a(release면), a 앞의 같은 스레드 release fence} × 도착 집합 {b(acquire면), b 뒤의 같은 스레드 acquire fence} 에 간선을 넣는다.

</details>

<details>
<summary>힌트 4 — hb 닫기와 탐색</summary>

모든 sb/sw 간선은 흔적의 앞 → 뒤로만 간다. 그러니 뒤에서부터 `hb[i] = succ(i) ∪ ⋃ hb[j]` 로 한 번에 닫힌다 (64 비트 비트셋). sb 는 "같은 스레드의 바로 다음 사건" 하나만 넣어도 전이로 충분하다. DFS 는 "실행 가능한 스레드 하나 골라 한 스텝 → 재귀 → 상태 되돌리기". AWAIT 은 값이 맞을 때만 실행 가능 (스핀 루프의 실패한 반복은 아무 sw 도 만들지 않으므로 생략해도 된다).

</details>

<details>
<summary>힌트 5 — DMA 엔진의 op–fence</summary>

CPU 의 `store(OWN|seq, release)` 각각이 "release 연산 A". 엔진의 relaxed load 가 그 값을 읽고(X), 그 뒤에 acquire fence(B) 가 있으면 A sw B ([atomics.fences]/4). 한 fence 가 앞의 **모든** relaxed load 에 대해 동시에 작동하므로 n 칸에 fence 1 번이면 된다.

</details>

## 5. 풀이

### 0. 표준 용어 — 이 레슨에 필요한 만큼 (C++20, N4861)

| 용어 | 뜻 | 위치 |
|---|---|---|
| conflict | 두 평가 중 하나가 메모리 위치를 수정하고 다른 하나가 같은 위치를 읽거나 수정 | [intro.races]/2 |
| modification order (mo) | 원자 객체 **하나** 의 모든 수정의 전체 순서 | [intro.races]/4 |
| release sequence | "release 연산 A 가 이끄는, M 의 수정 순서에서 A 로 시작해 **이후가 전부 원자 RMW** 인 최대 연속 구간" | [intro.races]/5 |
| synchronizes-with | release 연산 A 와, A 의 release sequence 안의 값을 읽은 acquire 연산 B | [atomics.order]/2, fence 판은 [atomics.fences]/2–4 |
| happens-before | consume 을 안 쓰면 (sb ∪ sw) 의 전이 폐포 | [intro.races]/9–10 |
| data race | "potentially concurrent 한 conflicting 동작 두 개, 하나 이상 비원자, 어느 쪽도 다른 쪽보다 먼저 일어나지 않음" → UB | [intro.races]/21 |

C11 쪽 대응은 5.1.2.4 (C-A03 0. 절). 이 레슨의 탐색기는 위 표의 3~6 행을 **그대로 코드로** 옮긴 것이다.

### Part A — fence 3형식: 무엇이 다르고, asm 은?

fence 는 **특정 변수에 묶이지 않는다**. `atomic_thread_fence(release)` 는 "앞의 모든 읽기/쓰기" 와 "뒤의 **모든** 원자 store" 사이에 순서를 건다. 그래서 release 연산보다 강하다(뒤의 아무 원자 store 가 X 가 될 수 있다). 하지만 혼자서는 아무것도 동기화하지 않는다 — 반드시 원자 객체를 통한 store/load 쌍(X, Y)이 있어야 한다. `fence(release); volatile_flag = 1;` 은 C++ 상 동기화가 **아니다** (C-A02 에서 C11 로 같은 결론).

<!-- code: demo_fence_asm.cpp -->

`demo_fence_asm.cpp` 를 `c++ -std=c++20 -O2 -S` 로 본 실제 arm64 명령 (Apple clang 21):

| 함수 | 소스 | 실제 명령 (핵심만) |
|---|---|---|
| `pub_op` | `data=v; f.store(1, release)` | `str w0,[data]` · `stlr w9,[x8]` |
| `pub_fence` | `data=v; fence(release); f.store(1, relaxed)` | `str w0,[data]` · `dmb ish` · `str w8,[f]` |
| `get_op` | `f.load(acquire) ? data : 0` | `ldapr w8,[x8]` · `ldr w9,[data]` · `csel` |
| `get_fence` | `if (!f.load(relaxed)) return 0; fence(acquire); return data;` | `ldr w8,[f]` · `cbz` · `dmb ishld` · `ldr w0,[data]` |
| `scan_fence` | `atomic_ref` relaxed load 8개 → fence(acquire) 1번 | `ldr` ×8 · `dmb ishld` ×1 |
| `ref_release` | `atomic_ref(ctrl_word[0]).store(v, release)` | `stlr w0,[x8]` |

읽는 법:

- op 형식은 **그 접근 자체** 가 순서를 가진 명령(`stlr`/`ldapr`)이 된다. fence 형식은 접근은 평범한 `str`/`ldr` 이고 별도 배리어(`dmb ish` = 전체, `dmb ishld` = 앞의 load 대 뒤의 load/store)가 붙는다.
- `get_op` 에서 `data` 를 **무조건** 읽는다(`csel` 로 고름) — acquire 가 `ldapr` 뒤의 load 를 앞으로 못 오게 막으니 컴파일러는 분기를 없앨 수 있다. `get_fence` 는 flag 를 본 경우에만 `dmb ishld` 를 치른다 — **조건부 acquire**, fence 형식의 첫 번째 장점.
- `scan_fence`: 8 칸을 relaxed 로 읽고 배리어는 한 번. acquire load 8 개(`ldapr` ×8)보다 싸게 만들 수 있다는 게 fence 형식의 두 번째 장점 — Part C 의 DMA 엔진, Linux 의 "여러 플래그 확인 후 `smp_rmb()`" 와 같은 모양. (실제 비용 비교는 LLAT-B06 소관, 여기선 측정하지 않았다.)
- `atomic_ref` 는 `std::atomic` 과 **같은 명령** 을 낸다 (`stlr`). 차이는 타입 시스템뿐이다.
- Cortex-M4 에서는 C-A02 실측대로 release/acquire fence 모두 `dmb sy` 이고 release store 도 `dmb sy` + `str` 이라 두 형식의 명령 수가 거의 같다 (이 C++ 파일은 thumbv7em 타깃에서 `<atomic>` 헤더를 못 찾아 직접 컴파일하지 못했다 — libc++ 가용성 마크업 오류).

Part A 의 메일박스는 네 번의 fence 로 양방향을 만든다:

```cpp
// send                                         // recv
if (full.load(relaxed) != 0) return false;      if (full.load(relaxed) != 1) return false;
fence(acquire);   // ← recv 의 release fence      fence(acquire);   // ← send 의 release fence
slot = v;                                        out = slot;
fence(release);                                  fence(release);
full.store(1, relaxed);                          full.store(0, relaxed);
```

반납 방향(recv 의 `fence(release); full=0` → send 의 `full==0; fence(acquire)`)이 없으면 생산자가 소비자가 읽는 중인 slot 을 덮는다 — C-A03 Part B 에서 op 형식으로 본 것과 같은 이유.

### Part A — release sequence: C++20 은 "RMW 만"

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">원자 객체 c 의 수정 순서(mo) — 어디까지가 생산자 store 의 release sequence 인가</text>
  <text x="10" y="50" fill="currentColor" font-weight="bold">rs_rmw</text>
  <rect x="90" y="34" width="170" height="26" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="96" y="51" fill="currentColor" font-family="monospace">P: c.store(2, rel)</text>
  <rect x="290" y="34" width="190" height="26" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="296" y="51" fill="currentColor" font-family="monospace">C1: fetch_sub acq 2→1</text>
  <rect x="510" y="34" width="190" height="26" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="516" y="51" fill="currentColor" font-family="monospace">C2: fetch_sub acq 1→0</text>
  <path d="M260 47 L288 47 M480 47 L508 47" stroke="currentColor" stroke-width="2"/>
  <path d="M90 70 L90 78 L700 78 L700 70" stroke="currentColor" fill="none"/>
  <text x="250" y="96" fill="currentColor">release sequence = {P store, C1 RMW, C2 RMW} → C2 도 P 와 sw (레이스 0)</text>
  <text x="10" y="140" fill="currentColor" font-weight="bold">rs_ldst</text>
  <rect x="90" y="124" width="170" height="26" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="96" y="141" fill="currentColor" font-family="monospace">P: c.store(2, rel)</text>
  <rect x="290" y="124" width="190" height="26" rx="3" class="f-warn" stroke="currentColor"/>
  <text x="296" y="141" fill="currentColor" font-family="monospace">Relay: c.store(1, rlx)</text>
  <rect x="510" y="124" width="190" height="26" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="516" y="141" fill="currentColor" font-family="monospace">C: c.load(acq) == 1</text>
  <path d="M260 137 L288 137" stroke="currentColor" stroke-width="2"/>
  <path d="M480 137 L508 137" stroke="currentColor" stroke-width="2" stroke-dasharray="4 3"/>
  <path d="M90 160 L90 168 L260 168 L260 160" stroke="currentColor" fill="none"/>
  <text x="96" y="186" fill="currentColor">sequence = {P store} 뿐. 순수 store 가 끊는다 → C 는 아무와도 sw 없음 → P 의 data 쓰기와 레이스</text>
  <text x="10" y="230" fill="currentColor" font-weight="bold">rs_cxx11</text>
  <rect x="90" y="214" width="170" height="26" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="96" y="231" fill="currentColor" font-family="monospace">T0: f.store(1, rel)</text>
  <rect x="290" y="214" width="190" height="26" rx="3" class="f-warn" stroke="currentColor"/>
  <text x="296" y="231" fill="currentColor" font-family="monospace">T0: f.store(2, rlx)</text>
  <rect x="510" y="214" width="190" height="26" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="516" y="231" fill="currentColor" font-family="monospace">T1: f.load(acq) == 2</text>
  <path d="M260 227 L288 227" stroke="currentColor" stroke-width="2"/>
  <text x="96" y="262" fill="currentColor">C++11: "같은 스레드의 store" 도 sequence 에 포함 → sw 있음.  C++20 (P0982R1): 삭제 → 레이스.</text>
  <text x="96" y="284" fill="currentColor">고치는 법: 두 번째 store 도 release 로, 또는 RMW(fetch_add/exchange) 로.</text>
</svg>
```

C++20 정의 ([intro.races]/5, 원문): *"A release sequence headed by a release operation A on an atomic object M is a maximal contiguous sub-sequence of side effects in the modification order of M, where the first operation is A, and every subsequent operation is an atomic read-modify-write operation."*

핵심 세 가지:

1. **RMW 의 memory_order 는 상관없다.** relaxed `fetch_sub` 도 sequence 를 잇는다 (탐색기 `rs_relay_rmw`: 레이스 0). RMW 는 항상 수정 순서의 마지막 값을 읽고 그 위에 쓰므로 "앞의 값에 이어 붙인" 쓰기이기 때문이다.
2. **순수 store 는 relaxed 든 release 든 sequence 를 끝낸다.** release store 라면 자기가 새 sequence 의 머리가 될 뿐, 앞 머리의 sequence 는 거기서 끝난다.
3. C++11 은 "A 와 같은 스레드가 한 쓰기" 도 포함했다. C++20 에서 P0982R1 (*Weaken Release Sequences*) 로 빠졌다 — 실제로 쓰이는 곳이 거의 없고 모델을 복잡하게 만든다는 이유. 그래서 `rs_cxx11` 은 **C++20 기준으로 data race(UB)**. 현실 하드웨어(arm64: `stlr` 뒤 `str`)에서 깨지는 걸 보이기는 어렵지만, 표준 기준 정답은 "레이스" 다.

`board_claim` 이 맞는 이유를 사슬로 쓰면: 생산자 `job[i]` 쓰기 —sb→ `store(n, release)` —sw→ (작업자 k 의 acquire `fetch_sub`, 그것이 읽은 값은 release sequence 안) —sb→ `job[i]` 읽기. 작업자 RMW 가 release 일 필요가 없다 (작업자끼리는 넘길 데이터가 없으니까).

### Part B — 탐색기 설계

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">탐색기 파이프라인 — 실행(인터리빙) 하나마다 hb 를 새로 계산한다</text>
  <rect x="10" y="36" width="130" height="56" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="18" y="56" fill="currentColor">스텝 목록</text>
  <text x="18" y="76" fill="currentColor" font-family="monospace">T0:{W d, ST f}</text>
  <rect x="160" y="36" width="130" height="56" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="168" y="56" fill="currentColor">dfs: 스레드 고르기</text>
  <text x="168" y="76" fill="currentColor">AWAIT·가드 반영</text>
  <rect x="310" y="36" width="130" height="56" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="318" y="56" fill="currentColor">흔적(trace)</text>
  <text x="318" y="76" fill="currentColor">사건 + rf + mo</text>
  <rect x="460" y="36" width="130" height="56" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="468" y="56" fill="currentColor">build_sw</text>
  <text x="468" y="76" fill="currentColor">4 규칙 + RS</text>
  <rect x="610" y="36" width="120" height="56" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="618" y="56" fill="currentColor">find_race</text>
  <text x="618" y="76" fill="currentColor">hb=(sb∪sw)⁺</text>
  <path d="M140 64 L158 64 M290 64 L308 64 M440 64 L458 64 M590 64 L608 64" stroke="currentColor" stroke-width="2"/>
  <path d="M152 59 L158 64 L152 69 M302 59 L308 64 L302 69 M452 59 L458 64 L452 69 M602 59 L608 64 L602 69" stroke="currentColor" fill="none" stroke-width="2"/>
  <g fill="currentColor">
    <text x="10" y="122">rf: 각 읽기는 "흔적에서 가장 최근의 같은 객체 쓰기" 를 읽는다 (SC 이므로). mo: 흔적 순서 그대로.</text>
    <text x="10" y="144">레이스: 다른 스레드, 같은 비원자 위치, 하나 이상 W, hb[a] 에 b 없음. 실행 하나라도 레이스면 전체가 UB.</text>
    <text x="10" y="166">모든 간선이 흔적 앞 → 뒤 → 뒤에서부터 비트셋 OR 한 번으로 폐포 완성 (사건 ≤ 64).</text>
    <text x="10" y="188">한계: SC 실행만 만든다 — 약한 메모리에서만 가능한 rf(둘 다 옛 값 읽기)는 탐색되지 않는다 (아래).</text>
  </g>
</svg>
```

`build_sw` 의 핵심은 네 규칙을 **하나의 루프** 로 처리하는 것이다. 읽기 b 와 그 rf 를 포함하는 sequence 의 머리 a 를 찾은 다음:

```cpp
targets = { b if acquire } ∪ { b 뒤 같은 스레드의 acquire fence }
sources = { a if release } ∪ { a 앞 같은 스레드의 release fence }
for s in sources, t in targets: sw[s] |= t      // (a,b)=op–op, (F,b)=fence–op, (a,F')=op–fence, (F,F')=fence–fence
```

fence 규칙의 "X 가 release 라면 이끌었을 가상(hypothetical) release sequence" 는 머리가 release 인지와 무관한 같은 계산이라 `in_release_seq` 하나로 된다.

실제 출력 (solution, Apple M6):

```text
Part B explorer:
  mp_relaxed      fences=1 rmw_rs=1 cxx11=0 : execs=3 racy=1 blocked=0  first: T0:W d  vs  T1:R d
      trace: T0:W d | T0:f=1 rlx | T1:f->1 rlx | T1:R d
  mp_relacq       fences=1 rmw_rs=1 cxx11=0 : execs=3 racy=0 blocked=0
  fence_fence     fences=1 rmw_rs=1 cxx11=0 : execs=4 racy=0 blocked=0
  fence_fence     fences=0 rmw_rs=1 cxx11=0 : execs=4 racy=1 blocked=0  first: T0:W d  vs  T1:R d
      trace: T0:W d | T0:fence rel | T0:f=1 rlx | T1:f->1 rlx | T1:fence acq | T1:R d
  fence_op        fences=1 rmw_rs=1 cxx11=0 : execs=4 racy=0 blocked=0
  op_fence        fences=1 rmw_rs=1 cxx11=0 : execs=3 racy=0 blocked=0
  fence_one_side  fences=1 rmw_rs=1 cxx11=0 : execs=4 racy=1 blocked=0  first: T0:W d  vs  T1:R d
      trace: T0:W d | T0:fence rel | T0:f=1 rlx | T1:f->1 rlx | T1:R d
  rs_rmw          fences=1 rmw_rs=1 cxx11=0 : execs=16 racy=0 blocked=0
  rs_rmw          fences=1 rmw_rs=0 cxx11=0 : execs=16 racy=6 blocked=0  first: T0:W d  vs  T2:R d
      trace: T0:W d | T0:c=2 rel | T1:rmw c:=1 acq | T1:R d | T2:rmw c:=0 acq | T2:R d
  rs_ldst         fences=1 rmw_rs=1 cxx11=0 : execs=1 racy=1 blocked=0  first: T0:W d  vs  T2:R d
      trace: T0:W d | T0:c=2 rel | T1:c->2 acq | T1:c=1 rlx | T2:c->1 acq | T2:R d
  rs_relay_rmw    fences=1 rmw_rs=1 cxx11=0 : execs=1 racy=0 blocked=0
  rs_cxx11        fences=1 rmw_rs=1 cxx11=0 : execs=1 racy=1 blocked=0  first: T0:W d  vs  T1:R d
      trace: T0:W d | T0:f=1 rel | T0:f=2 rlx | T1:f->2 acq | T1:R d
  rs_cxx11        fences=1 rmw_rs=1 cxx11=1 : execs=1 racy=0 blocked=0
  chain3          fences=1 rmw_rs=1 cxx11=0 : execs=1 racy=0 blocked=0
  chain3_broken   fences=1 rmw_rs=1 cxx11=0 : execs=1 racy=1 blocked=0  first: T0:W d  vs  T2:R d
      trace: T0:W d | T0:f=1 rel | T1:f->1 rlx | T1:f2=1 rel | T2:f2->1 acq | T2:R d
  sb_guarded      fences=1 rmw_rs=1 cxx11=0 : execs=10 racy=0 blocked=0
```

실행 수는 손으로 확인할 수 있어야 테스트가 의미 있다. `mp_relaxed` 는 T0 = {W d, ST f}, T1 = {LD f, R d (f==1 일 때만)}: T1 의 LD 가 ST 앞(두 자리) → R 생략, 뒤(한 자리) → R 실행 = **3**. 그중 flag 를 본 1 개만 레이스 — "레이스가 있는 실행이 **하나라도** 있으면 프로그램 전체가 UB" 이므로 판정은 레이스다. `rs_rmw` (P = {W d, ST c=2}, C1/C2 = {RMW, R d if r>0}):

| 경우 | 조건 | 실행 수 | RMW 규칙 끔(`rmw_rs=0`)에서 레이스 |
|---|---|---|---|
| 둘 다 P 의 store 전에 RMW | 둘 다 0/−1 을 읽어 R 생략, a2 가 마지막 | 3! = 6 | 0 |
| C1 만 먼저 | {a1, b1} 순서 2 가지 → a2 → c1 → c2 | 2 | 0 (C2 는 P 를 직접 읽음) |
| C2 만 먼저 | 대칭 | 2 | 0 |
| 둘 다 나중 | a1 a2 뒤에 (b1 b2) 와 (c1 c2) 섞기 C(4,2) | 6 | **6** (두 번째 소비자는 첫 소비자의 RMW 를 읽음) |
| 합 | | **16** | **6** |

C++20 규칙을 켜면 마지막 줄의 6 개도 release sequence 덕분에 0 이 된다 — "RMW 가 release sequence 를 잇는다" 는 규칙이 정확히 이 6 개 실행을 살리는 것이다.

### Part B — TSan 과 대조 (`demo_tsan.cpp`)

<!-- code: demo_tsan.cpp -->

같은 모양의 프로그램을 진짜 스레드로 만들어 `-fsanitize=thread` 로 돌렸다 (Apple clang 21, Apple M6 — `hw.nperflevels` = 3: Super 2 + Performance 4 + Efficiency 6 = 12 코어). 보고가 있으면 이 맥에서 exit 134.

| 프로그램 | 탐색기 (C++20) | TSan 실측 | 판정 |
|---|---|---|---|
| mp_relaxed | 레이스 | 보고 (exit 134) | 일치 |
| mp_relacq | 없음 | 없음 | 일치 |
| fence_fence | 없음 | **보고** (5/5 회) | TSan **오탐** |
| fence_op | 없음 | **보고** | TSan 오탐 |
| op_fence | 없음 | **보고** | TSan 오탐 |
| rs_rmw | 없음 | 없음 (5/5) | 일치 — TSan 도 RMW sequence 를 안다 |
| rs_ldst | **레이스** | 없음 (5/5) | TSan **놓침** |
| rs_cxx11 | **레이스** | 없음 (5/5) | TSan 놓침 (C++11 식으로 너그러움) |
| chain3 | 없음 | 없음 | 일치 |
| sb_guarded | 없음 (SC 한계) | 없음 (20만 라운드 × 3 회) | **둘 다 놓침** (아래) |

`fence_fence` 의 실제 보고 (스택의 긴 템플릿 이름은 줄임):

```text
WARNING: ThreadSanitizer: data race (pid=68907)
  Read of size 4 at 0x0001000c4004 by thread T2:
    #0 void* std::__1::__thread_proxy[...]<..., main::$_3>>(void*) thread.h:168 (dt:arm64+0x10000226c)

  Previous write of size 4 at 0x0001000c4004 by thread T1:
    #0 void* std::__1::__thread_proxy[...]<..., main::$_2>>(void*) thread.h:168 (dt:arm64+0x1000020e4)

  Location is global 'data' at 0x0001000c4004 (dt+0x10000c004)
  …
mode=fence_fence rounds=1 seen=42 both_wrote=0
ThreadSanitizer: reported 1 warnings
```

해석:

- **오탐**: TSan 은 단독 `atomic_thread_fence` 를 동기화로 모델링하지 않는다 (C-A02 의 C11 실측과 같은 결과를 C++ 에서도 확인). 탐색기를 `fences=false` 로 돌린 결과(`fence_fence ... racy=1`)가 TSan 과 같다.
- **놓침**: TSan 은 relaxed store 를 만나도 그 주소의 동기화 시계를 지우지 않는 것으로 보인다 — 그래서 `rs_ldst`/`rs_cxx11` 에서 acquire 쪽이 여전히 P 의 release 를 받아 간다. 즉 TSan 의 release sequence 는 C++20 보다 넓다 (관찰과 맞는 해석, TSan 런타임 소스로 확인하지는 않았다). 추가 연습 1 이 이 "TSan 흉내" 규칙을 탐색기에 넣어 10 개 판정을 **10/10** 맞힌다.
- 그래서 solution 은 fence 를 쓰는 곳(Part A 메일박스, Part C DMA 엔진)에 `__tsan_acquire/__tsan_release(<동기화 주소>)` 주석을 붙였다 (`<sanitizer/tsan_interface.h>`, TSan 빌드에서만 활성). 주석을 뺀 사본을 TSan 으로 돌리면 `g_mb`(메일박스 slot) 3 건 + `g_ring`(디스크립터) 2 건, **5 건의 오탐** 이 나왔다 (exit 134). 주석이 있으면 0 건.

### Part B — 탐색기의 한계: SC 만 본다

`sb_guarded`: T0 = `x.store(1, release); if (y.load(acquire)==0) d = 1;`, T1 은 대칭. SC 인터리빙에서는 둘 다 0 을 읽을 수 없으니 `d` 에 두 번 쓰는 실행이 없다 → 탐색기 "레이스 0" (10 실행 = 대칭 두 순서 × 3 + 겹치는 4). 하지만 C++ 모델에서 release/acquire 는 store → (다른 변수) load 순서를 보장하지 않는다(C-A03 SB 리트머스). **둘 다 0 을 읽는 실행이 허용되고, 그 실행에는 `d` 에 대한 레이스가 있다** → 프로그램은 UB. TSan 없이 실제로 돌린 결과 (`./dt_n sb_guarded`, 라운드 20만, 3 회):

```text
mode=sb_guarded rounds=200000 seen=0 both_wrote=36
mode=sb_guarded rounds=200000 seen=0 both_wrote=513
mode=sb_guarded rounds=200000 seen=0 both_wrote=63
mode=sb_seqcst rounds=200000 seen=0 both_wrote=0      (seq_cst 판, 3 회 모두 0)
```

하드웨어가 실제로 둘 다 옛 값을 읽었다(36 ~ 513 회). TSan 빌드에서는 3 회 모두 `both_wrote=0`, 보고 0 — 계측 코드가 타이밍을 바꿔 그 실행이 안 나왔고, TSan 은 일어나지 않은 실행을 보지 않는다.

언제 SC 탐색으로 충분한가: 표준 노트 ([intro.races]/21 Note) — *"programs that correctly use mutexes and memory_order::seq_cst operations to prevent all data races and use no other synchronization operations behave as if the operations executed by their constituent threads were simply interleaved"*. 이것이 **DRF-SC** 다. 원자 연산이 전부 seq_cst(와 mutex)면 "모든 SC 인터리빙에 레이스 없음" ⇒ 레이스 없음. acquire/release/relaxed 가 섞이면 이 보장이 없다. 약한 실행까지 세는 도구로는 cppmem(Cambridge), CDSChecker, GenMC 같은 모델 검사기가 있다 (이름만 소개 — 이 레슨에서 쓰지 않음). 하드웨어 리트머스 실측은 OS-M08, ARM-M05, ARCH-A05 소관.

탐색 공간: 가드·AWAIT 없는 스레드 k 개 × 각 n 스텝이면 (kn)! / (n!)^k. 3×3 = 1680 (추가 연습 4 실측), 3×5 = 756756. 실제 도구는 DPOR(동적 부분 순서 축소)로 hb 가 같은 인터리빙을 합친다.

### Part C — `atomic_ref` 로 C 구조체를 그대로 공유

`std::atomic<uint32_t> ctrl;` 을 구조체에 넣으면 C 헤더와 공유할 수 없다(C 에서는 C++ 타입이 아니고, 레이아웃이 같다는 보장도 문서에 의존). `atomic_ref` (C++20, P0019R8)는 **plain 객체에 원자 연산을 빌려준다**. 규칙 ([atomics.ref.generic]):

| 규칙 | 이 레슨 | 근거 |
|---|---|---|
| T 는 trivially copyable | `uint32_t` | /2 |
| 대상 객체의 수명 > atomic_ref 수명 | 링은 정적 객체 | /3 |
| **atomic_ref 가 존재하는 동안 그 객체의 모든 접근은 atomic_ref 를 통해** | 공유가 시작되면 `ctrl` 은 `ctrl_of(d)` 로만. 하네스의 `r->d[0].ctrl == ...` 직접 읽기는 스레드가 없는 단위 테스트에서만 | /3 |
| 정렬 ≥ `required_alignment` | static_assert | [atomics.ref.ops] |

이 맥(libc++)에서 출력한 실제 값:

```text
u16: req_align=2 lockfree=1
u32: req_align=4 lockfree=1
u64: req_align=8 lockfree=1
S16: req_align=16 (alignof 8) lockfree=1
S12: req_align=4 (alignof 4) lockfree=0
```

16 B 구조체는 `alignof` 가 8 인데 `atomic_ref` 는 **16** 을 요구한다 (16 B 원자 접근 명령의 정렬 조건) — `alignas(16)` 없이 쓰면 UB. 12 B 구조체는 lock-free 가 아니다 → 내부 락 → ISR/DMA 경로에서 금지 (CPP-A04 에서 타깃별 lock-free 표).

엔진 쪽 `dma_poll` 은 op–fence 를 그대로 쓴다: CPU 의 `store(OWN|seq, release)` (A) → 엔진의 relaxed load (X, 값을 읽음) → `fence(acquire)` (B) ⇒ A sw B ([atomics.fences]/4). n 칸을 한 번에 처리하므로 fence 도 한 번. 스트레스 실측 출력 `Part C stress: packets=100000 max_batch=8` — 실제로 링 8 칸 전부를 fence 한 번으로 처리한 배치가 있었다 (배치 크기 분포는 타이밍 의존이라 출력만 하고 CHECK 하지 않는다).

CPU 의 `tx_submit` 이 `ctrl == 0` 확인을 relaxed 로 하는 이유: 0 은 **CPU 자신이** `tx_reap` 에서 쓴 값이다. 엔진의 DONE 은 reap 의 acquire load 가 이미 받았고, 같은 스레드 안의 순서는 sb 로 충분하다.

**주의 — 진짜 DMA 는 C++ 스레드가 아니다.** 표준은 장치가 무엇을 보는지 말하지 않는다. 진짜 하드웨어에서는 아키텍처 배리어와 캐시 유지보수가 필요하다 (Cortex-M7 D-cache clean, arm64 Linux `dma_wmb()` = `dmb oshst` — C-A02 Part C). 이 레슨의 Part C 는 "코어 간 메일박스"(Cortex-A 리눅스 ↔ Cortex-M 펌웨어 공유 SRAM 의 디스크립터, 또는 듀얼 코어 MCU)로 읽는 것이 정확하다 — 그 경우에도 두 코어가 같은 일관성 도메인에 있는지, 영역이 cacheable 인지 먼저 확인해야 한다 (C-A03 Q8).

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread solution.cpp -o solution && ./solution        # 43/43 passed (6회 반복 모두)
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.cpp -o sol_asan && ./sol_asan   # 43/43, 보고 0
c++ -std=c++20 -g -O1 -fsanitize=thread -pthread solution.cpp -o sol_tsan && ./sol_tsan          # 43/43, 보고 0 (3회)
```

실제 출력 꼬리:

```text
Part C stress: packets=100000 max_batch=8
43/43 passed
```

전체 실행은 0.16 초 정도 (`time`: 0.07 s user). 탐색기 16 개 판정은 그중 극히 일부다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `fence(release); flag = 1;` (flag 가 plain/volatile) | fence 규칙은 **원자 객체** M 을 통한 store/load 쌍을 요구한다. volatile 은 원자가 아니다 → 동기화 없음, 레이스 | `std::atomic<int> flag` + `flag.store(1, relaxed)` |
| acquire fence 를 relaxed load **앞** 에 | acquire fence 는 앞의 load 가 읽은 값에 대해서만 작동 ([atomics.fences]/4 "X sequenced before B") | `if (f.load(relaxed)) { fence(acquire); use(data); }` |
| 한쪽만 fence (`fence_one_side`) | release 쪽만 있으면 받는 쪽에 acquire 가 없다 → 탐색기 레이스 1 | 받는 쪽에 acquire fence 또는 acquire load |
| 중계 스레드가 `c.store(c.load()-1)` | 순수 store 는 release sequence 를 끊는다 (`rs_ldst` 레이스) | `c.fetch_sub(1, relaxed)` (relaxed RMW 로도 이어짐) |
| release 뒤 같은 스레드의 relaxed store 로 "갱신" | C++20 에서 sequence 에서 빠졌다 (`rs_cxx11`) | 그 store 도 release, 또는 RMW |
| 참조 카운트 `fetch_sub(relaxed)` + 0 일 때 acquire fence | 다른 스레드의 읽기를 "게시" 하는 release 가 없다 → 재사용 쓰기와 레이스 (연습 2 `ref_relaxed` 6/6) | `fetch_sub(release)` + `fence(acquire)` |
| `atomic_ref` 를 만든 채 다른 스레드에서 plain 접근 | [atomics.ref.generic]/3 위반 — 그 동안엔 모든 접근이 atomic_ref 로 | 공유 시작 이후 `ctrl_of(d)` 만 |
| 16 B 구조체에 `atomic_ref` (alignof 8) | `required_alignment` = 16 (실측) | `alignas(16)` + static_assert |
| "TSan 이 fence 코드를 레이스라 한다 → 코드가 틀렸다" | TSan 은 단독 fence 를 모른다 (실측 오탐) | 증명은 표준 규칙으로, TSan 엔 `__tsan_acquire/release` 주석 |
| "TSan 이 조용하다 → 레이스 없다" | 실행된 경로만, 그리고 release sequence 를 C++20 보다 넓게 본다 (실측 놓침 2 건) | 탐색기·모델 검사기·리뷰로 보완 |
| SC 인터리빙 전수 탐색으로 "증명" | acq/rel/relaxed 가 섞이면 DRF-SC 가 성립하지 않는다 (`sb_guarded`) | seq_cst 만 쓰거나, 약한 실행을 세는 도구 |

## 8. 면접 Q&A

<details>
<summary>Q1. atomic_thread_fence(acquire) 와 load(acquire) 는 무엇이 다르고, fence 는 언제 쓰나요?</summary>

load(acquire) 는 **그 load 하나** 에 순서를 붙인다(arm64 `ldapr`). acquire fence 는 "앞의 **모든** 원자 load 가 읽은 값" 과 "뒤의 모든 접근" 사이에 순서를 건다(arm64 `dmb ishld`). 그래서 ① 조건부 acquire — 플래그를 본 경우에만 배리어 비용(`get_fence`: `cbz` 뒤에 `dmb ishld`), ② 여러 플래그/디스크립터를 relaxed 로 훑고 배리어 한 번(`scan_fence`: `ldr` ×8 + `dmb ishld` ×1, Part C DMA 엔진), ③ 참조 카운트의 "마지막일 때만 acquire" 에 쓴다. 단점: 변수에 묶이지 않아 의도가 코드에 덜 드러나고, TSan 이 이해하지 못한다. 꼬리: "release fence 는?" → 앞의 모든 접근 vs 뒤의 **모든** 원자 store. 한 번의 fence 로 여러 플래그를 게시할 때 유용.

</details>

<details>
<summary>Q2. release fence 뒤에 volatile 플래그를 쓰면 동기화되나요? 하드웨어에선 dmb 가 나가잖아요.</summary>

C++ 표준상 아니다. [atomics.fences]/2–3 은 "A 가 sequenced-before 인 **원자 연산 X 가 M 을 수정**" 을 요구한다. volatile 대입은 원자 연산이 아니므로 X 가 될 수 없고, 상대가 그걸 읽어도 sw 가 생기지 않아 데이터 레이스 = UB. asm 이 같아 보여도(`dmb ish; str`) 컴파일러는 표준을 근거로 최적화할 권리가 있다. 해결: 플래그를 `std::atomic` (또는 `std::atomic_ref`) 로 두고 relaxed store. 펌웨어에서 MMIO 레지스터(DMA doorbell)처럼 진짜로 C++ 밖의 관찰자라면 표준이 아니라 아키텍처 문서(DMB/DSB 의미)가 근거다 (C-A02).

</details>

<details>
<summary>Q3. release sequence 를 설명하고, C++20 에서 무엇이 바뀌었는지 말해 보세요.</summary>

release 연산 A 가 이끄는, M 의 수정 순서에서 A 다음으로 **원자 RMW 만** 이어지는 최대 구간이다 ([intro.races]/5). acquire 가 그 구간 안의 **아무 값** 을 읽어도 A 와 동기화한다. 그래서 여러 작업자가 `fetch_sub` 로 이어 가는 카운터의 마지막 작업자도 생산자와 동기화된다 (`rs_rmw`: 규칙 끄면 16 실행 중 6 개 레이스, 켜면 0). RMW 의 memory_order 는 relaxed 여도 된다. 순수 store 는 relaxed 든 release 든 앞 sequence 를 끝낸다. C++11 은 "A 와 같은 스레드의 쓰기" 도 포함했지만 C++20 (P0982R1) 에서 삭제됐다 → `store(1, release); store(2, relaxed)` 뒤 2 를 읽은 acquire 는 C++20 에서 동기화되지 않는다 (`rs_cxx11`).

</details>

<details>
<summary>Q4. 참조 카운트 감소를 왜 fetch_sub(release) + (0 이 됐을 때) fence(acquire) 로 쓰나요? acq_rel 로 하면?</summary>

각 스레드의 release `fetch_sub` 는 "내 읽기/쓰기는 끝났다" 를 게시한다. 이 RMW 들은 서로 이어져 release sequence 를 이룬다. 마지막 스레드의 `fetch_sub` 는 바로 앞 RMW 의 값을 읽었고, 그 RMW 는 다른 모든 스레드의 release RMW 가 이끄는 sequence 안에 있다 → 뒤의 acquire fence 가 그 전부와 op–fence 로 동기화한다 → 다른 스레드의 모든 접근 hb 마지막 스레드의 `delete`/재사용. 탐색기 연습 2: 맞는 판 6 실행 레이스 0, fence 빼면 6/6, RMW 를 relaxed 로 하면 6/6 레이스. `acq_rel` 로 해도 맞지만 매 감소마다 acquire 비용을 낸다. 꼬리: "증가는?" → relaxed. 이미 참조를 가진 쪽만 증가시키므로 객체가 사라질 수 없고 넘길 데이터도 없다. (Boost.Atomic 문서의 참조 카운트 예제, Linux `refcount_dec_and_test` 의 "release + 0 일 때 acquire" 와 같은 모양 — C-A03 연습 1.)

</details>

<details>
<summary>Q5. C++ 의 data race 정의를 정확히 말해 보세요. 레이스가 어떤 실행 하나에만 있다면?</summary>

"potentially concurrent 한 conflicting 동작 두 개를 포함하고, 그중 하나 이상이 비원자이고, 어느 쪽도 다른 쪽보다 먼저 일어나지(happens before) 않으면, 그 실행은 data race 를 포함한다. 그런 data race 는 UB" ([intro.races]/21). conflict = 같은 위치, 하나 이상 수정 (/2). 원자 접근끼리는 레이스가 아니다(순서 버그는 될 수 있어도). 레이스가 가능한 실행이 **하나라도** 있으면 그 입력에 대한 프로그램 동작 전체가 정의되지 않는다 — `mp_relaxed` 는 3 실행 중 1 개만 레이스지만 판정은 "레이스". 꼬리: "signal handler 는?" → 같은 스레드의 핸들러와도 potentially concurrent 일 수 있다 (/21.2), `atomic_signal_fence` 로 순서를 건다 (C-M14).

</details>

<details>
<summary>Q6. 회사 코드가 fence 를 쓰는데 TSan 이 레이스라고 합니다. 어떻게 하죠? 반대로 TSan 이 조용하면 안심해도 되나요?</summary>

이 맥에서 실측: fence–fence / fence–op / op–fence 세 형식 모두 TSan 이 보고했다(오탐). 대응은 세 가지 — ① 가능하면 op 형식(release store / acquire load)으로 바꾼다, ② 바꿀 수 없으면 `__tsan_release(addr)` / `__tsan_acquire(addr)` 주석 (solution 이 그렇게 함: 주석 없으면 5 건, 있으면 0 건), ③ 마지막 수단으로 suppression. 반대로 TSan 이 조용해도 안심하면 안 된다: `rs_ldst`, `rs_cxx11` 은 C++20 레이스인데 5/5 회 보고 0 이었고, `sb_guarded` 는 실제 하드웨어에선 36 ~ 513 회 둘 다 썼지만 TSan 빌드에선 그 실행이 아예 안 나왔다. TSan = "이번 실행에서, TSan 의 모델로 본" 레이스다. 꼬리: "그럼 무엇으로 증명?" → 표준 규칙으로 hb 사슬을 손으로(또는 이 탐색기/모델 검사기로) 보이고, 테스트는 보조.

</details>

<details>
<summary>Q7. std::atomic_ref 는 언제 쓰고, 어떤 규칙을 지켜야 하나요?</summary>

C 와 공유하는 구조체(DMA 디스크립터, IPC 공유 메모리 헤더, 참조 카운트 필드)처럼 **타입을 바꿀 수 없는 plain 객체** 에 원자 연산이 필요할 때. 또는 대부분의 시간엔 단일 스레드라 plain 으로 빠르게 접근하다가, 특정 구간에만 공유할 때. 규칙: ① T trivially copyable, ② 객체가 atomic_ref 보다 오래 산다, ③ atomic_ref 가 존재하는 동안 그 객체의 **모든** 접근은 atomic_ref 로 ([atomics.ref.generic]/3), ④ 정렬 ≥ `required_alignment` — 이 맥에서 u32 = 4, 16 B 구조체 = 16 (alignof 8 보다 큼). ⑤ `is_always_lock_free` 를 static_assert (12 B 구조체는 0). 생성 비용은 없다 — `atomic_ref(ctrl).store(v, release)` 는 `std::atomic` 과 같은 `stlr` 한 줄 (실측). 꼬리: "C 쪽은?" → C11 `_Atomic` 은 타입 한정자라 C 에도 같은 문제가 있다. C 쪽은 `__atomic_*` 빌트인이나 C11 atomic 으로 접근하고, 두 언어의 원자 구현이 lock-free 로 같은 명령을 낸다는 걸 확인한다 (미검증 일반론 — 툴체인 문서 확인).

</details>

<details>
<summary>Q8. 모든 SC 인터리빙을 돌려 봤는데 레이스가 없으면 그 코드는 안전한가요?</summary>

원자 연산이 전부 seq_cst(와 mutex)라면 그렇다 — DRF-SC ([intro.races]/21 의 Note). acquire/release/relaxed 가 섞이면 아니다. 반례 `sb_guarded`: SC 에선 두 스레드가 모두 옛 값을 읽을 수 없어 탐색기 판정 "레이스 0" 인데, C++ 모델은 그 실행을 허용하고 실제 Apple M6 에서 20만 라운드 중 36~513 번 나왔다. seq_cst 로 바꾸면 3 회 모두 0. 탐색기는 "SC 실행 안의 hb" 만 계산하므로 **약한 rf** 를 만들어 내지 못한다. 꼬리: "그럼 이 탐색기는 쓸모없나?" → 아니다. 레이스를 찾으면 그건 **진짜** 레이스다(SC 실행도 허용된 실행이므로 거짓 양성 없음). 못 찾은 경우만 약하다. fence/release sequence 규칙을 손으로 확인하는 교재로는 충분하다.

</details>

<details>
<summary>Q9. relaxed fetch_add 가 release sequence 를 잇는다면, relaxed 연산이 동기화에 기여하는 거 아닌가요?</summary>

기여하는 것은 **값의 사슬** 이지 순서가 아니다. relaxed RMW 자체는 어떤 접근도 앞뒤로 묶지 않는다. 단지 원자 RMW 는 수정 순서에서 바로 앞의 값을 읽고 쓰므로(분할 불가능), 머리 A 가 쓴 값에서 "이어진" 값으로 취급된다. 동기화의 출발(release)과 도착(acquire)은 여전히 A 와 읽는 쪽이 낸다. 그래서 `rs_relay_rmw` 에서 중계 스레드의 RMW 는 relaxed 지만 읽는 쪽은 acquire 여야 하고(아니면 레이스), 중계 스레드 **자신** 은 P 의 데이터를 볼 권리가 없다(relaxed 라 P 와 동기화하지 않음).

</details>

## 9. 추가 연습

### 연습 1 — TSan 흉내 규칙으로 실측 판정 맞히기

`Rules` 에 `tsan_like` 를 추가하라: fence 는 무시하고, release sequence 는 "RMW 이거나 release 가 아닌 store" 면 이어진다 (release 순수 store 만 끊음). `demo_tsan.cpp` 의 10 개 모드에 대해 TSan 실측(보고 있음/없음)을 표로 넣고, 흉내 모델과 C++20 모델이 각각 몇 개를 맞히는지 CHECK 하라.

<details>
<summary>답 보기</summary>

`in_release_seq` 의 이음 조건에 `|| (r.tsan_like && (e.rmw || !is_rel(e.mo)))` 한 줄. 결과: 흉내 모델 **10/10**, C++20 모델 **5/10** — 어긋나는 5 개 = fence 3 개(TSan 오탐) + `rs_ldst`/`rs_cxx11`(TSan 놓침). `sb_guarded` 는 둘 다 "없음" 으로 맞혔지만 진짜 정답(레이스)과는 둘 다 틀리다. 이 연습의 교훈: 도구의 판정은 **도구의 모델** 의 판정이다. 흉내 규칙은 관찰에 맞춘 근사이고 TSan 런타임 소스로 확인한 것은 아니다.

</details>

### 연습 2 — 참조 카운트를 탐색기로 판정

두 스레드: `R buf; r0 = ref.fetch_sub(1, mo); if (r0 == 1) { fence(acquire); W buf; }` (`ref` 초기값 2). 세 판 — (a) `mo = release` + fence, (b) fence 없음, (c) `mo = relaxed` + fence — 의 실행 수와 레이스 수를 예측하고 확인하라.

<details>
<summary>답 보기</summary>

세 판 모두 6 실행 (두 스레드의 {R, RMW} 섞기 C(4,2)=6, 마지막 RMW 뒤에 fence/W 가 붙음). (a) 레이스 0: 먼저 감소한 스레드의 release RMW 를 마지막 RMW 가 읽음 → 그 값은 앞 RMW 가 이끄는 sequence 의 머리 → 마지막 스레드의 acquire fence 와 op–fence 동기화 → 상대의 `R buf` hb `W buf`. (b) 6/6: 마지막 RMW 는 release 지만 acquire 가 없음. (c) 6/6: acquire fence 는 있지만 출발할 release 가 없음. 실제 출력:

```text
  ref_ok          : execs=6 racy=0 blocked=0
  ref_nofence     : execs=6 racy=6 blocked=0  first: T0:R buf  vs  T1:W buf
  ref_relaxed     : execs=6 racy=6 blocked=0  first: T0:R buf  vs  T1:W buf
```

</details>

### 연습 3 — C 구조체 필드에 atomic_ref 참조 카운트 (진짜 스레드)

`struct frame_hdr { uint32_t refcnt; uint32_t seq; uint8_t payload[56]; }` (64 B, C 와 공유). `frame_get` / `frame_put` 을 `std::atomic_ref` 로 쓰고, 4 스레드 × 2만 라운드: 각자 payload 를 읽고 put, **마지막** put 만 payload 를 덮어써(재사용) 다른 스레드의 읽기를 망치지 않아야 한다.

<details>
<summary>답 보기</summary>

```cpp
static void frame_get(frame_hdr &f) { std::atomic_ref<std::uint32_t>(f.refcnt).fetch_add(1, memory_order_relaxed); }
static bool frame_put(frame_hdr &f) {
    if (std::atomic_ref<std::uint32_t>(f.refcnt).fetch_sub(1, memory_order_release) != 1u) return false;
    std::atomic_thread_fence(memory_order_acquire);
    tsan_acquire(&f.refcnt);          // TSan 빌드용 주석
    return true;
}
```

연습 2 의 (a) 를 진짜로 돌린 것이다. 실측 `연습 3: rounds=20000 lasts=20000 bad=0` — 라운드마다 정확히 한 스레드가 마지막, 읽기 오류 0. TSan 빌드에서도 보고 0 (주석 덕분 — 빼면 fence 오탐이 난다). 라운드 사이의 refcnt 재설정도 `atomic_ref` 로 한다: 다른 스레드가 아직 atomic_ref 를 쥐고 있을 수 있는 구간에서 plain 쓰기는 [atomics.ref.generic]/3 위반.

</details>

### 연습 4 — 탐색 공간의 크기

가드·AWAIT 없는 3 스레드 × 3 스텝 프로그램 두 개 — 전부 읽기 / 한 스레드가 `d` 를 쓰고 다른 스레드가 `d` 를 읽음 — 의 실행 수와 레이스 수를 공식으로 예측하라.

<details>
<summary>답 보기</summary>

9! / (3!·3!·3!) = 1680. 전부 읽기는 충돌이 없어 레이스 0. `W d` 와 `R d` 사이에 sw 를 만드는 원자 연산이 전혀 없으므로 1680 실행 **전부** 레이스. 실측 `reads_3x3 : execs=1680 racy=0`, `one_write : execs=1680 racy=1680`. 3×5 면 15!/(5!)³ = 756756 — 스텝이 조금만 늘어도 폭발한다. 실전 도구는 DPOR 로 같은 hb 를 갖는 인터리빙을 한 번만 본다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread practice.cpp -o practice && ./practice        # 10/10 passed
c++ -std=c++20 -g -O1 -fsanitize=thread -pthread practice.cpp -o practice_t && ./practice_t     # 10/10, 보고 0
```

## 10. 핵심 정리

- 스레드 사이 happens-before 의 재료는 synchronizes-with 뿐. 만드는 모양은 넷: op–op, fence–op, op–fence, fence–fence. 언제나 **원자 객체 M 을 통한 값 전달** 이 고리다 (volatile 은 고리가 못 된다).
- release 쪽 fence 는 원자 store **앞**, acquire 쪽 fence 는 원자 load **뒤**. fence 는 변수에 묶이지 않아 "조건부 acquire", "여러 칸 relaxed 스캔 + 배리어 1번" 에 쓴다 (arm64 `ldr`×8 + `dmb ishld`).
- release sequence (C++20) = 머리 뒤로 **RMW 만**. relaxed RMW 도 잇고, 순수 store 는 끊는다. C++11 의 "같은 스레드 store" 규칙은 삭제(P0982R1).
- 참조 카운트 = `fetch_sub(release)` + 마지막일 때 `fence(acquire)` — release sequence + op–fence 의 교과서 예.
- `atomic_ref` = plain 객체에 원자 연산 대여. 공유 중엔 그 객체를 atomic_ref 로만, 정렬은 `required_alignment`(16 B 구조체는 16), `is_always_lock_free` 확인. 명령은 `std::atomic` 과 같다.
- data race = 다른 스레드의 충돌하는 비원자 접근 쌍 + hb 없음. 레이스가 가능한 실행이 하나라도 있으면 UB.
- TSan: fence 는 **오탐**, C++20 release sequence 위반은 **놓침**, 일어나지 않은 실행은 못 봄. fence 코드엔 `__tsan_acquire/release` 주석.
- SC 전수 탐색: 찾은 레이스는 진짜, 못 찾은 건 보장 아님 — DRF-SC 는 seq_cst(+mutex)만 쓸 때만.

## 참고자료

- C++20 working draft N4861 — [intro.races] (¶2 conflict, ¶4 modification order, ¶5 release sequence, ¶9–11 happens-before, ¶21 data race + DRF-SC 노트): <https://timsong-cpp.github.io/cppwp/n4861/intro.races>
- N4861 [atomics.order] (¶2 synchronizes-with): <https://timsong-cpp.github.io/cppwp/n4861/atomics.order> · [atomics.fences] (¶2–4 fence 동기화, ¶5 memory_order 별 의미): <https://timsong-cpp.github.io/cppwp/n4861/atomics.fences> · [atomics.ref.generic] (¶1–4 atomic_ref 규칙): <https://timsong-cpp.github.io/cppwp/n4861/atomics.ref.generic>
- P0982R1 *Weaken Release Sequences* (C++20): <https://wg21.link/p0982r1>
- P0019R8 *Atomic Ref* (C++20): <https://wg21.link/p0019r8>
- P0668R5 *Revising the C++ memory model* (C++20, seq_cst 관련 수정 — 이 레슨에서는 다루지 않음): <https://wg21.link/p0668r5>
- cppreference — `std::memory_order` (release sequence 설명), `std::atomic_thread_fence`, `std::atomic_ref`: <https://en.cppreference.com/w/cpp/atomic/memory_order>, <https://en.cppreference.com/w/cpp/atomic/atomic_thread_fence>, <https://en.cppreference.com/w/cpp/atomic/atomic_ref>
- Clang ThreadSanitizer 문서 (지원 범위, `-fsanitize=thread`): <https://clang.llvm.org/docs/ThreadSanitizer.html> · 주석 API: `<sanitizer/tsan_interface.h>` 의 `__tsan_acquire / __tsan_release` (이 맥: `/Library/Developer/CommandLineTools/usr/lib/clang/21/include/sanitizer/tsan_interface.h`)
- Hans-J. Boehm, Sarita Adve, *Foundations of the C++ Concurrency Memory Model*, PLDI 2008 (DRF-SC)
- Mark Batty 외, *Mathematizing C++ Concurrency*, POPL 2011 — cppmem 도구: <http://svr-pes20-cppmem.cl.cam.ac.uk/cppmem/>
- Anthony Williams, *C++ Concurrency in Action*, 2nd ed., 5장 (메모리 모델, fence, release sequence)
- Linux kernel `Documentation/memory-barriers.txt` (smp_rmb 등 fence 형식 관용구): <https://www.kernel.org/doc/Documentation/memory-barriers.txt>
- 관련 레슨: C-A02 (컴파일러/CPU 재배치, C11 fence, TSan fence 오탐, DMA 배리어), C-A03 (stdatomic 기본, 참조 카운트 연습 1, SB 리트머스), C-M14 (ISR↔main), OS-M08 (메시지 패싱·DCLP 패턴 스트레스), CPP-A04 (Cortex-M lock-free 표, volatile vs atomic), ARM-M04 (LDXR/STXR·LSE), ARM-M05 (DMB/LDAR/STLR 리트머스), RISCV-M04 (LR/SC), LLAT-B06 (원자 연산 비용 측정)
