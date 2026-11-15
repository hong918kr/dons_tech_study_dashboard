---
id: OS-M06
time: 100분
updated: 2026-11-11
---
# OS-M06 스레드별 캐시 할당기: arena + tcache

## 학습 목표

- 전역 락 하나짜리 malloc 이 스레드가 늘수록 **느려지는** 이유를 숫자로 보이고(실측 1 → 8 스레드: 42.8 → 13.2 Mops/s), 그것을 푸는 두 층인 **다중 arena**(스레드 → arena 라운드로빈, arena 마다 자기 락·자기 힙)와 **스레드별 캐시 tcache**(락 없는 bin, bin 당 최대 7개)를 직접 만든다.
- 블록 헤더에 **주인 arena id**(bit48..55)를 넣고, `free(p)` 가 헤더 하나로 주인을 찾고 영역·alloc 비트·푸터로 검증하게 한다. 잘못된 포인터와 이중 해제(arena 쪽·tcache 쪽 둘 다)를 거절한다.
- tcache 의 **배치(batch)** 설계 두 가지를 구현한다. 가득 차면 오래된 3개를 락 한 번에 flush 하고, 비면 락 한 번에 1+3개를 refill 한다. 그 결과 락 획득 횟수가 정확히 몇 번인지 손으로 계산하고 테스트로 맞춘다(T1 트레이스: 2466 → 303번).
- **remote free**(다른 스레드가 해제)를 주인 arena 의 lock-free 스택에 CAS 로 push 하고, 주인이 다음 slow path 에서 `atomic_exchange` 로 **통째로** 가져가게 만든다. 이 구조에 ABA 가 없는 이유, release/acquire 가 꼭 필요한 이유(relaxed 로 바꾸면 TSan 경고 26개, 실측)를 설명한다.
- 생산자/소비자 스트레스(패턴 검증 + 소유 비트맵)와 RX → 워커 패킷 파이프라인으로 "두 번 나가는 블록 0개, 종료 후 모든 바이트가 arena 로 돌아옴"을 확인하고, 스레드 종료 때 tcache 를 안 비우면 무엇이 새는지(실측 772992 B) 본다.

## 1. 문제

"멀티스레드 서버에서 malloc 이 프로파일 상위에 뜹니다. 왜 그렇고, glibc·jemalloc·mimalloc 은 어떻게 피하나요? 직접 만든다면?" 시스템 소프트웨어 면접의 단골 질문이다. 임베디드 리눅스에서는 같은 문제가 반대 얼굴로도 나온다. 4코어 ARM 보드의 카메라·센서 데몬이 스레드를 여러 개 띄우자 **RSS 가 수백 MB 로 불어나는** 현상이다. 원인은 glibc 의 arena 이고, 흔한 처방은 `MALLOC_ARENA_MAX=2` 다. 둘 다 이 레슨의 구조(arena 수, 스레드 캐시가 쥔 메모리)를 알아야 설명할 수 있다.

OS-M04 가 boundary tag 명시적 free list(분할·병합·realloc)를, OS-M05 가 그것을 크기 클래스별 segregated list 로 바꾸고 단편화를 쟀다. 이 레슨은 **OS-M05 축약판**을 arena 하나의 백엔드로 그대로 넣고(파일 하나로 완결), 그 위에 멀티스레드 층을 쌓는다. 고정 크기 객체 캐시(slab)와 매거진은 DS-M03, 버디는 DS-M04, lock-free 스택의 ABA 자체는 DS-A04 에서 다뤘다. 여기서는 "**누가 어느 힙을 잠그나**" 만 다룬다. OSTEP 17장(Free-Space Management, 끝의 Hoard 인용)과 29장(sloppy counter: 지역에서 모아 임계값마다 전역으로)이 배경이다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
<defs><marker id="m6a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" fill="currentColor" font-weight="bold">MT_TCACHE 모드: 스레드 → tcache(락 없음) → arena(자기 락) ← remote free(CAS)</text>
<rect x="20" y="40" width="150" height="70" rx="5" class="f-ok" stroke="currentColor"/>
<text x="30" y="58" fill="currentColor">스레드 T0 (RX)</text>
<text x="30" y="76" fill="currentColor" font-size="12">tcache bin 63개</text>
<text x="30" y="94" fill="currentColor" font-size="12">32..1024 B, 각 ≤ 7</text>
<rect x="20" y="130" width="150" height="70" rx="5" class="f-ok" stroke="currentColor"/>
<text x="30" y="148" fill="currentColor">스레드 T1 (워커)</text>
<text x="30" y="166" fill="currentColor" font-size="12">tcache (자기 것만)</text>
<text x="30" y="184" fill="currentColor" font-size="12">_Thread_local</text>
<rect x="290" y="40" width="260" height="110" rx="5" class="f-acc" stroke="currentColor"/>
<text x="300" y="58" fill="currentColor" font-weight="bold">arena 0</text>
<text x="300" y="78" fill="currentColor" font-size="12">mutex (128 B 정렬)</text>
<text x="300" y="96" fill="currentColor" font-size="12">자기 mmap 32 MiB + mem_sbrk</text>
<text x="300" y="114" fill="currentColor" font-size="12">seg list 17 클래스, 병합, 분할</text>
<rect x="300" y="122" width="240" height="22" rx="3" class="f-warn" stroke="currentColor"/>
<text x="308" y="138" fill="currentColor" font-size="12">remote 스택 head (atomic, 따로 한 줄)</text>
<rect x="290" y="190" width="260" height="70" rx="5" class="f-acc" stroke="currentColor"/>
<text x="300" y="208" fill="currentColor" font-weight="bold">arena 1</text>
<text x="300" y="228" fill="currentColor" font-size="12">mutex · 힙 · seg list · remote 스택</text>
<text x="300" y="246" fill="currentColor" font-size="12">(arena 2, 3 … 같은 모양)</text>
<path d="M170 60 L288 60" stroke="currentColor" stroke-width="1.5" marker-end="url(#m6a)"/>
<text x="178" y="54" fill="currentColor" font-size="12">miss: 락 1번에 1+3개</text>
<path d="M170 160 L288 210" stroke="currentColor" stroke-width="1.5" marker-end="url(#m6a)"/>
<text x="178" y="200" fill="currentColor" font-size="12">자기 블록: tcache → 넘치면 flush 3</text>
<path d="M170 140 Q 240 140 296 134" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 3" fill="none" marker-end="url(#m6a)"/>
<text x="178" y="126" fill="currentColor" font-size="12">남의 블록: CAS push</text>
<path d="M550 132 Q 640 132 640 90 Q 640 60 552 66" stroke="currentColor" stroke-width="1.5" fill="none" marker-end="url(#m6a)"/>
<text x="572" y="112" fill="currentColor" font-size="12">주인 쪽 slow path:</text>
<text x="572" y="126" fill="currentColor" font-size="12">exchange(NULL) 로</text>
<text x="572" y="140" fill="currentColor" font-size="12">통째로 회수·병합</text>
<rect x="20" y="285" width="700" height="60" rx="5" class="f-mut" stroke="currentColor"/>
<text x="30" y="305" fill="currentColor" font-family="monospace" font-size="12">header 8 B: | 63..56: 0 | 55..48: owner arena | 47..4: size (16 의 배수) | 3..1: 0 | 0: alloc |</text>
<text x="30" y="325" fill="currentColor" font-size="12">free(p) → 헤더의 owner 로 arena 를 O(1) 에 찾고, 그 arena 영역 안인지 · alloc=1 인지 · 푸터 == 헤더인지 검증</text>
<text x="30" y="340" fill="currentColor" font-size="12">tcache/remote 스택에 든 블록은 헤더를 안 건드린다(alloc=1 그대로) → 헤더 쓰기는 언제나 arena 락 아래</text>
</svg>
```

세 가지 모드를 한 코드로 만든다.

| 모드 | arena | 스레드 캐시 | malloc/free 1번에 락 | 이 레슨에서 보는 것 |
|---|---|---|---|---|
| `MT_GLOBAL` | 1개 | 없음 | 매번 같은 락 1번 | 기준선. 스레드가 늘면 줄을 선다 |
| `MT_ARENAS` | N개 (최대 8) | 없음 | 매번 **자기** arena 락 1번 | 경합은 사라지지만 락 비용은 남는다 |
| `MT_TCACHE` | N개 | bin 63개 × 최대 7 | 대부분 0번, miss·넘침 때만 묶어서 1번 | 락 횟수 자체를 줄인다 |

remote free 정책은 두 가지다. `RF_LOCK` 은 주인 arena 의 락을 잡고 바로 반납한다(glibc 가 이쪽이라고 알고 있다. 아래 Q3 참고). `RF_STACK` 은 주인의 lock-free 스택에 push 만 하고 주인이 나중에 회수한다(mimalloc 의 "thread-delayed free" 와 같은 아이디어).

### Part A — 헤더의 주인 필드와 스레드 → arena 바인딩

- 주어진 것: OS-M05 축약판 백엔드(`arena_malloc/arena_free/arena_realloc`, segregated list 17 클래스, 즉시 병합, `mem_sbrk`), `mm_check`, 공유 트레이스 T1–T4 생성기(OS-M04 의 것을 그대로 복사 → op 열이 OS-M04/M05 와 동일), 재생기(패턴 검증, 500 op 마다 `mm_check`).
- **A1** `my_arena()` — 스레드가 처음 할당할 때 원자 카운터 `G.rr` 로 라운드로빈 배정, `_Thread_local` 에 캐시. `mm_init` 이 다시 불리면 세대(`G.gen`)가 바뀌어 낡은 바인딩·tcache 를 버린다(주어짐 `tls_sync`).
- **A2** `owner_arena(p)` — 헤더의 owner 필드로 arena 를 고르고, 그 arena 의 `[lo+32, end)` 안인지, alloc 비트, 크기 상한, 푸터 == 헤더를 확인. 아니면 `NULL`(호출자가 `bad_free` 를 세고 아무것도 안 건드림).
- 검증: 6 스레드 순차 → arena `0 1 2 3 0 1`, arena 2 블록의 owner == 2, 힙 밖 포인터·이중 해제 거절, arena 1 스레드가 arena 3 블록을 해제하면 arena 3 으로 돌아감(RF_LOCK). 4 트레이스 × 3 모드 재생이 모두 깨끗하고, T1 에서 전역 락은 op 마다 1번, tcache 는 그 1/4 미만(실측 2466 vs 303).

### Part B — tcache(제한·flush·refill)와 remote free 스택

- **B1** `tc_get` / `tc_flush` / `tc_put` — bin 은 단일 연결 LIFO(next 는 payload 첫 워드). `tc_put` 은 같은 bin 을 훑어 이중 해제를 거절하고, 7개가 차 있으면 **꼬리(오래된) 3개**를 끊어 락 한 번에 arena 로 보낸 뒤 push 한다.
- **B2** `tc_refill` — (arena 락 아래) 같은 크기 블록을 3개 더 잘라 bin 에 넣는다. 정확히 그 크기가 아닌 블록(남는 16 B 를 붙여 준 블록)은 돌려주고 멈춘다.
- **B3** `remote_push` (Treiber push, release CAS) / `remote_drain` (acquire exchange 로 통째로 떼어 와 `arena_free`).
- 검증: 40 B 요청 10번 → 락 3번·bin 2개·refill 9개, 10번 해제 → bin 6개·flush 6개·락 누계 5번(손 계산과 일치해야 함). 2000 B 는 tcache 를 안 거침. tcache 이중 해제 거절. arena 0 이 만든 100개를 arena 1 스레드가 해제 → 전부 remote 스택(내 tcache 0개), 주인 쪽 다음 slow path 에서 100개 회수. 생산자 4 + 소비자 4 × 6000 블록: 패턴 오류·이중 배포·미소유 해제 0, `remote == pushed == drained`, 끝나면 live 0, 각 arena 가 free 블록 하나로 병합됨.

### Part C — 패킷 파이프라인과 종료 경로

- 임베디드 리눅스의 흔한 구조: NIC/카메라 **RX 스레드가 버퍼를 할당**하고, **워커 스레드들이 처리 후 해제**한다. 할당과 해제가 언제나 다른 스레드다. RX 는 arena 0, 워커 3개는 arena 1..3. 워커는 패킷마다 48 B 파싱 컨텍스트를 할당·해제하고, 8 패킷마다 100 B 로그 레코드를 최근 4개만 보관한다(자기 tcache 사용).
- **C1** `mm_thread_exit()` — 이 스레드 tcache 의 모든 bin 을 자기 arena 로 flush 하고 바인딩을 푼다(glibc 도 스레드 종료 때 tcache 를 비운다고 알고 있다).
- **C2** `mm_drain_all()` — 모든 arena 의 remote 스택을 그 arena 락 아래에서 비운다(종료 직전·메모리 압박 때).
- 검증: 20000 패킷 체크섬 OK, 패킷 해제 20000번이 모두 remote, 워커 종료 직후 워커 arena 1..3 의 live == 0, `drain_all` 뒤 arena 0 이 20000개 모두 회수하고 전체 live == 0.

## 2. Problem (English)

> Make a segregated-fit allocator (given: one "arena" = its own mmap region + `mem_sbrk`, boundary tags, 17 size classes, immediate coalescing) scale across threads.
>
> - Header word: bit 0 = allocated, bits 4..47 = block size, bits 48..55 = **owner arena id**. Payloads are 16-byte aligned, minimum block 32 B.
> - `mm_init_cfg(mode, narenas, policy)`, `mm_malloc`, `mm_free`, `mm_realloc`, `mm_check(verbose)`, `mm_thread_exit()`, `mm_drain_all()`. Modes: `MT_GLOBAL` (one arena, one lock), `MT_ARENAS` (threads bound round-robin to N arenas, each with its own mutex), `MT_TCACHE` (arenas + a per-thread cache of 63 bins for blocks of 32..1024 B, at most 7 blocks per bin).
> - When a bin is full, return its 3 oldest blocks to the arena under one lock acquisition; when it is empty, carve 1+3 blocks under one lock acquisition.
> - A block freed by a thread that does not own its arena must not take the owner's lock (`RF_STACK`): push it onto the owner's lock-free stack; whoever next takes that arena's lock drains the whole stack with one atomic exchange.
> - Reject frees of non-heap pointers and double frees (both in the arena and in the tcache). `malloc(0)` returns NULL; `free(NULL)` is a no-op; `realloc(p, 0)` frees and returns NULL.
>
> Example: with an empty cache, ten `malloc(40)` calls take the arena lock exactly 3 times; freeing all ten leaves 6 blocks cached and takes the lock 2 more times.
>
> Follow-ups: why bound the cache? Why is the remote stack ABA-free? Which memory orders are required? What leaks when a thread exits? How do glibc, jemalloc and mimalloc differ?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

모든 파일 첫 줄에 `/* BUILD: -pthread */` 가 있다(스레드를 쓰므로 `-pthread` 필요). 처음 실행 결과(실제 출력 일부, 0.2 초에 끝나고 exit 1):

```text
-- Part A: header owner field, thread -> arena binding, global lock vs arenas
PASS mm_malloc(0) == NULL
  bind order: 0 0 0 0 0 0
FAIL starter.c:768  g_bind_seen[0] == 0 && g_bind_seen[1] == 1 && g_bind_seen[2] == 2 && …
FAIL starter.c:775  G.arena[2].live_bytes == 0 && atomic_load(&G.bad_free) == 0
…
  T1     tcache    2466    2466  0.637
…
FAIL starter.c:806  locks_t1[0] == n_t1 && locks_t1[2] * 4 < locks_t1[0]
-- Part B: tcache bound/flush/refill, remote free stack
FAIL starter.c:815  sum_lock() - lk0 == 3 && tc_count(40) == 2 && G.arena[0].tc_refilled == 9
…
  stress tcache+stack: got=24000 remote=0 pushed=0 drained=0 errs=0 dup=0 unowned=0 live=0
FAIL starter.c:848  s1.remote > 0 && s1.remote == s1.pushed && s1.pushed == s1.drained
…
  packets=20000 bad=0 remote=20000 pushed=0 drained-during-run=0 worker-arena live after exit=1600000
…
10/31 passed
```

TODO 는 A1, A2, B1(세 함수), B2, B3(두 함수), C1, C2. 스텁은 모두 **틀리지만 안전**하다. A1 스텁은 모두를 arena 0 에 묶고(= 전역 락), A2 스텁은 arena 0 블록만 알아보고 나머지는 거절(새기만 하고 망가뜨리지 않음), tcache 스텁은 캐시하지 않고, remote 스텁은 주인 락을 잡고 바로 반납한다. 그래서 시작 코드도 패턴 검증·`mm_check` 는 통과하지만(`errs=0`), 락 횟수·bin 개수·remote 카운터를 보는 체크는 실패한다. Part C 의 `live after exit=1600000` 은 A2 스텁이 워커 arena 의 블록을 거절해서 생긴 누수다(`bad_free` 22500). 모든 대기에는 큐 close 가 있고, `main` 첫 줄의 `alarm(60)` 이 워치독이다.

## 4. 힌트

<details><summary>힌트 1 — A1: 바인딩은 한 번, 카운터는 원자적으로</summary>

`T.arena` 가 NULL 일 때만 `atomic_fetch_add_explicit(&G.rr, 1, memory_order_relaxed) % G.narenas`. relaxed 로 충분하다. 카운터 값이 겹치지만 않으면 되고, 그 값으로 다른 데이터를 공개하지 않는다. arena 구조체 자체는 `mm_init_cfg` 가 스레드를 만들기 **전에** 다 채웠다(pthread_create 가 happens-before).
</details>

<details><summary>힌트 2 — A2: 헤더를 먼저 믿지 말고, 헤더가 가리키는 곳이 맞는지 확인</summary>

`OWNER_OF(GET(HDRP(p)))` 로 id 를 얻고, `id < narenas`, `p` 가 그 arena 의 `[lo+32, end)` 안, alloc 비트 1, 크기 ≥ 32 이고 영역을 넘지 않음, 푸터 == 헤더. `brk` 는 주인이 락 아래에서 바꾸므로 락 없이 읽으면 data race 다. 불변인 `end` 로 상한을 본다.
</details>

<details><summary>힌트 3 — B1: "오래된 3개" 는 리스트 꼬리</summary>

LIFO 라서 머리가 가장 최근(캐시에 뜨거운) 블록이다. `keep = count - 3` 번째 노드까지 걸어가서 거기서 끊으면 나머지가 오래된 쪽이다. 끊은 사슬은 **락 한 번** 안에서 모두 `arena_free`. bin 은 최대 7개라 걷는 비용이 정해져 있다.
</details>

<details><summary>힌트 4 — B2: 락 횟수를 손으로 세어 보기</summary>

`mm_malloc` 이 miss 일 때 이미 arena 락을 쥐고 있다. 그 김에 3개를 더 잘라 두면 다음 3번은 락이 없다. 10번 할당 = miss 1, 5, 9번째 → 락 3번, bin 에 2개 남음. 해제 10번: 2 → 7 까지 5번, 6번째에 flush(락 4) 후 5, 8번째에 7, 9번째에 flush(락 5) 후 5, 10번째에 6.
</details>

<details><summary>힌트 5 — B3: 하나씩 pop 하지 말 것</summary>

push 는 "next 를 쓰고 → CAS(head: old → p)". 회수는 `atomic_exchange(&head, NULL)` 한 번이면 리스트 전체가 내 것이 된다. 하나씩 pop 하려면 "head 를 읽고 → head->next 를 읽고 → CAS" 인데, 그 사이에 head 가 빠졌다 같은 주소로 돌아오면 ABA 다(DS-A04). 통째로 가져가면 next 를 미리 읽는 창이 없다. 순서: push 의 CAS 성공은 release, exchange 는 acquire.
</details>

<details><summary>힌트 6 — C1: tcache 에 든 블록은 arena 입장에서 "할당됨"</summary>

헤더 alloc 비트가 1 인 채로 bin 에 있으니 스레드가 그냥 사라지면 아무도 그 블록을 해제하지 않는다. 모든 bin 에 `tc_flush(T.arena, &T.bin[i], T.bin[i].count)`.
</details>

## 5. 풀이

### Part A — 왜 전역 락은 스레드가 늘수록 느려지나

malloc/free 한 번은 수십 ns 짜리 짧은 임계 구역이다. 스레드 하나일 때 락은 uncontended(원자 연산 한두 번)라 싸다. 스레드가 여럿이면 락이 든 캐시 라인과 힙 메타데이터(free list 머리, 헤더)가 코어 사이를 오간다. 기다리던 스레드는 스핀하다가 커널에서 잠들고 깨어난다. 처리량은 "더 많은 코어" 가 아니라 "하나의 직렬 구간 + 이동 비용" 으로 정해진다. `demo_bench.c` W1(스레드마다 자기 블록만 할당·해제) 실측, 이 맥에서:

| 할당기 (Mops/s) | 1 스레드 | 2 | 4 | 8 |
|---|---|---|---|---|
| 전역 락 (arena 1) | 42.8 | 18.4 | 19.5 | **13.2** |
| arena ×8 | 38.8 | 68.7 | 131.4 | 188.1 |
| arena ×8 + tcache | **83.5** | 128.6 | 236.9 | **289.8** |
| macOS libc malloc | 70.3 | 99.3 | 144.7 | 195.5 |

기계: Apple M6 (perflevel0 2 + perflevel1 4 + perflevel2 6 = 12 코어, `hw.cachelinesize` 128), Apple clang 21.0.0, macOS 27.2. 실행 전 `uptime` load averages 16.33 15.70 12.79, 후 16.54 15.75 12.82. 다른 세션이 같이 돌고 있어서 **배수만** 믿는다. 같은 바이너리를 세 번 돌렸을 때 8 스레드 칸은 전역 13.2/8.3/9.1, arena 188.1/244.1/252.6, tcache 289.8/401.8/399.8 이었다. 순서는 늘 같았다.

읽는 법: 전역 락은 1 → 8 스레드에서 처리량이 **3배 줄었다**. arena 는 경합을 없애 4.8배 늘었고, tcache 는 1 스레드에서도 2.2배 빠르다. 경합이 없어도 **락 획득 자체**(원자 RMW + 배리어)와 seg list 탐색이 비용이기 때문이다. 스레드 1개 칸만 봐도 "tcache 의 이득 = 경합 회피 + 락 생략" 중 뒤쪽이 크다는 것이 보인다.

### Part A — 스레드 → arena: 라운드로빈 vs 다른 정책

A1 은 처음 할당할 때 한 번 정하고 끝이다. 테스트는 6 스레드를 차례로 돌려 `0 1 2 3 0 1` 을 확인한다(실측 출력 `bind order: 0 1 2 3 0 1`). 실제 할당기들은 더 영리하다(모두 기억 기준, 버전마다 다름).

| 할당기 | 스레드 → arena | 비고 |
|---|---|---|
| 이 레슨 | 첫 할당 때 라운드로빈, 고정 | 단순, 결정적 |
| glibc ptmalloc | 처음엔 main arena. 경합(락 trylock 실패) 시 새 arena 를 만들거나 기존 것을 재사용 | 상한 = CPU 수의 배수(`man 3 mallopt` M_ARENA_TEST: 64비트 기본 8, "multiple of the number of available CPUs" — VM 의 man page 에서 확인) |
| jemalloc | 스레드를 arena 에 배정(기본 arena 수 = CPU 수 × 4 로 기억) | tcache 는 arena 앞단 |
| mimalloc | 스레드마다 자기 heap(arena 를 공유하지 않음) | remote free 는 page 의 thread-free list |
| tcmalloc (신판) | 스레드가 아니라 **CPU 마다** 캐시(rseq 사용) | 스레드 수가 많아도 캐시 수가 CPU 수로 고정 |

추가 연습 3 은 glibc 식 "trylock 으로 안 바쁜 arena 찾기" 를 작게 만든다.

### Part A — 주인 arena 를 어떻게 아나: 헤더 vs 주소

A2 는 헤더 비트 8개로 주인을 O(1) 에 찾는다. 대가는 헤더가 망가지면 엉뚱한 arena 로 간다는 것이다. 그래서 찾은 arena 가 정말 그 주소를 포함하는지, 푸터가 같은지 확인한다. 다른 방법은 **주소 자체**로 찾는 것이다. 이 레슨도 arena 마다 영역이 따로라서 범위 비교로 찾을 수 있다(소유 비트맵 `own_mark` 이 그렇게 한다). 하지만 arena 수에 비례한다. 실제 할당기들은 이렇게 한다고 알고 있다(소스 줄 단위로 확인 안 함).

- glibc: 청크 헤더의 `NON_MAIN_ARENA` 비트 1개 + "main 이 아닌 arena 의 heap 은 `HEAP_MAX_SIZE` 경계에 정렬" → `p & ~(HEAP_MAX_SIZE-1)` 이 heap 헤더 → 그 안의 arena 포인터.
- mimalloc: segment 가 큰 2의 거듭제곱 경계에 정렬 → `p & ~(segment_size-1)` 로 segment → page → 주인 스레드 id.
- jemalloc: 주소 → extent 를 찾는 radix tree(rtree).
- DS-M03 슬랩: 4 KB 정렬 → `p & ~0xFFF` 로 슬랩 헤더(같은 아이디어의 가장 작은 판).

| 헤더 비트 | 이 레슨의 값 | 쓰는 곳 |
|---|---|---|
| bit 0 | alloc | 병합, 이중 해제 판정 |
| bit 1..3 | 0 (예약) | M05 의 prev_alloc 같은 용도로 쓸 수 있음. 푸터 생략은 안 함(검사기가 header == footer 를 봄) |
| bit 4..47 | 크기 | 블록 크기는 16 의 배수라 하위 4비트가 비어 있다 |
| bit 48..55 | owner arena | `mm_free` 의 첫 분기, `mm_check` 가 arena 안 모든 블록에서 확인 |

### Part A — 트레이스 재생: 공간 효율과 락 횟수

공유 트레이스 T1–T4 는 **OS-M04 solution.c 의 생성기(`xorshift32`, `gen_mix`, `gen_realloc`, `gen_binary`, `make_traces`)를 그대로 복사**했다. op 열이 OS-M04/M05 와 똑같으므로 아래 U 는 그 레슨들의 표와 바로 비교할 수 있다. 세 모드로 돌린 실제 출력이다. U = 살아 있는 요청 바이트의 최댓값 / 힙 높이(모든 arena 의 `brk - lo` 합).

| 트레이스 | ops | 락 (global / arenas / tcache) | U (global / arenas / tcache) |
|---|---|---|---|
| T1 small-uniform | 2466 | 2466 / 2466 / **303** | 0.638 / 0.637 / 0.637 |
| T2 mixed | 2438 | 2438 / 2438 / 821 | 0.861 / 0.860 / 0.809 |
| T3 realloc-grow | 19198 | 19198 / 19198 / 13398 | 0.565 / 0.565 / 0.576 |
| T4 binary-pattern | 3000 | 3000 / 3000 / 872 | 0.537 / 0.537 / **0.837** |

- global 과 arenas 의 U 차이(0.638 vs 0.637)는 쓰지 않는 arena 3개의 프롤로그 32 B × 3 이다. 단일 스레드에서는 둘이 같은 할당기다.
- tcache 는 보통 U 를 **조금 깎는다**(T2 0.861 → 0.809). bin 에 든 블록은 arena 입장에서 사용 중이라 병합되지 못한다. refill 로 미리 잘라 둔 블록도 그렇다. 이것이 tcache 의 공간 비용이다. 추가 연습 1 에서 상한을 계산한다(스레드당 최악 227 KiB).
- T3 는 realloc 이 대부분이라 tcache 를 안 거친다(`mm_realloc` 은 늘 arena 락). 그래서 락이 70% 로만 줄었다(19198 → 13398). U 는 거의 같다(0.565 vs 0.576).
- **T4 는 거꾸로 좋아졌다**(0.537 → 0.837). global 에서는 64 B(블록 80)와 448 B(블록 464)가 번갈아 놓여서, 448 을 다 해제해도 구멍 464 B 가 80 B 블록 사이에 하나씩 갇힌다. 512 B(블록 528)는 어느 구멍에도 안 들어가 힙이 자란다. tcache 의 refill 은 같은 크기 4개를 **연달아** 자르므로 배치가 `[80×4][464×4][80×4]…` 가 된다. 448 들을 해제해 flush 하면 이웃한 464 네 개가 1856 B 로 병합되어 528 이 세 개씩 들어간다. 배치 refill 이 우연히 slab 처럼 "같은 크기끼리 모으기"(DS-M03)를 한 셈이다. 이 결과는 이 트레이스의 모양 덕분이지 일반 법칙은 아니다.

### Part B — tcache: 제한(7), flush(3), refill(3)

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">bin(64 B 블록)의 개수: malloc(40) 10번 → free 10번 (테스트 B-1 과 같은 순서)</text>
<line x1="40" y1="230" x2="730" y2="230" stroke="currentColor"/>
<line x1="40" y1="230" x2="40" y2="60" stroke="currentColor"/>
<text x="10" y="234" fill="currentColor" font-size="12">0</text>
<text x="10" y="87" fill="currentColor" font-size="12">7</text>
<line x1="40" y1="83" x2="730" y2="83" stroke="currentColor" stroke-dasharray="4 4"/>
<text x="600" y="77" fill="currentColor" font-size="12">TC_COUNT = 7</text>
<g fill="currentColor" font-size="11">
<rect x="50" y="167" width="24" height="63" class="f-warn" stroke="currentColor"/><text x="56" y="248">m1</text>
<rect x="82" y="188" width="24" height="42" class="f-ok" stroke="currentColor"/><text x="86" y="248">m2</text>
<rect x="114" y="209" width="24" height="21" class="f-ok" stroke="currentColor"/><text x="118" y="248">m3</text>
<rect x="146" y="229" width="24" height="1" class="f-ok" stroke="currentColor"/><text x="150" y="248">m4</text>
<rect x="178" y="167" width="24" height="63" class="f-warn" stroke="currentColor"/><text x="182" y="248">m5</text>
<rect x="210" y="188" width="24" height="42" class="f-ok" stroke="currentColor"/><text x="214" y="248">m6</text>
<rect x="242" y="209" width="24" height="21" class="f-ok" stroke="currentColor"/><text x="246" y="248">m7</text>
<rect x="274" y="229" width="24" height="1" class="f-ok" stroke="currentColor"/><text x="278" y="248">m8</text>
<rect x="306" y="167" width="24" height="63" class="f-warn" stroke="currentColor"/><text x="310" y="248">m9</text>
<rect x="338" y="188" width="24" height="42" class="f-ok" stroke="currentColor"/><text x="339" y="248">m10</text>
<rect x="390" y="167" width="24" height="63" class="f-mut" stroke="currentColor"/><text x="396" y="248">f1</text>
<rect x="422" y="146" width="24" height="84" class="f-mut" stroke="currentColor"/><text x="428" y="248">f2</text>
<rect x="454" y="125" width="24" height="105" class="f-mut" stroke="currentColor"/><text x="460" y="248">f3</text>
<rect x="486" y="104" width="24" height="126" class="f-mut" stroke="currentColor"/><text x="492" y="248">f4</text>
<rect x="518" y="83" width="24" height="147" class="f-mut" stroke="currentColor"/><text x="524" y="248">f5</text>
<rect x="550" y="125" width="24" height="105" class="f-warn" stroke="currentColor"/><text x="556" y="248">f6</text>
<rect x="582" y="104" width="24" height="126" class="f-mut" stroke="currentColor"/><text x="588" y="248">f7</text>
<rect x="614" y="83" width="24" height="147" class="f-mut" stroke="currentColor"/><text x="620" y="248">f8</text>
<rect x="646" y="125" width="24" height="105" class="f-warn" stroke="currentColor"/><text x="652" y="248">f9</text>
<rect x="678" y="146" width="24" height="84" class="f-mut" stroke="currentColor"/><text x="680" y="248">f10</text>
</g>
<text x="50" y="160" fill="currentColor" font-size="12">락1</text><text x="178" y="160" fill="currentColor" font-size="12">락2</text><text x="306" y="160" fill="currentColor" font-size="12">락3</text>
<text x="548" y="118" fill="currentColor" font-size="12">락4</text><text x="644" y="118" fill="currentColor" font-size="12">락5</text>
<text x="40" y="275" fill="currentColor" font-size="12">miss(진한 막대) = 락 1번에 1개 돌려주고 3개 refill → 3. 넘침 = 7 에서 오래된 3개 flush(락 1번) → 4 → push 해서 5.</text>
<text x="40" y="292" fill="currentColor" font-size="12">결과: 락 5번(전역 락이면 20번), bin 6개, refill 9개, flush 6개, arena live = 6 × 64 B — 테스트가 모두 정확히 확인</text>
</svg>
```

**왜 bin 크기를 제한하나.** tcache 에 든 블록은 다른 스레드가 쓸 수 없고, 병합도 안 된다. 제한이 없으면 "한 스레드가 잔뜩 해제하고, 다른 스레드가 잔뜩 할당" 하는 패턴에서 메모리가 한쪽 캐시에 쌓여 힙이 계속 자란다(Hoard 논문이 말하는 blowup). 7 × 63 bin 이면 스레드당 최악 232848 B(연습 1). 8 스레드면 1.8 MB 다. 서버에선 작은 값이지만 64 MB RAM 의 임베디드 리눅스 보드에선 고민할 숫자다. OSTEP 29장의 sloppy counter 와 같은 거래다. 임계값 S(여기선 bin 용량)가 클수록 확장성은 좋아지고 전역 상태(여기선 arena 가 아는 free 메모리)는 부정확해진다.

**왜 묶어서(batch) 옮기나.** 넘칠 때 1개만 arena 로 보내면 "가득 찬 bin 에서 free 할 때마다 락" 이 된다. 할당·해제가 경계에서 번갈아 일어나면 매번 락을 잡는다(진동). 3개를 보내 두면 다음 3번의 free 는 락이 없다. refill 도 같다. DS-M03 의 매거진 실험(채움 8 < 용량 16)과 같은 히스테리시스다. glibc 는 넘친 청크 하나를 그냥 arena 쪽(fastbin 등)으로 보낸다고 알고 있고, jemalloc 은 tcache bin 이 차면 일부를 한꺼번에 flush 한다고 알고 있다. 둘 다 이 레슨에서는 확인하지 않았다.

**왜 꼬리(오래된 것)를 내보내나.** 머리 쪽은 방금 해제한, 캐시에 남아 있을 확률이 높은 블록이다. 다음 malloc 이 그것을 받으면 캐시 미스가 없다(테스트: `mm_malloc(40) == b[9]`, 마지막에 해제한 것). 오래된 것을 arena 로 보내야 병합 기회도 생긴다.

**tcache 이중 해제.** tcache 에 든 블록은 헤더가 여전히 alloc=1 이라 A2 검사를 통과한다. 그래서 `tc_put` 이 같은 bin 을 훑는다(최대 7개). 테스트는 `free(d); free(d);` 에서 `bad_free == 1`, bin 은 한 번만 늘었다. glibc 2.29 이후는 해제된 청크의 `key` 필드로 먼저 의심하고 그때만 bin 을 훑는다(DS-M03 의 쿠키 방식과 같다). Lima VM(Ubuntu glibc 2.43)에서 `free(p); free(p);` 실제 출력:

```text
freed once
free(): double free detected in tcache 2
exit=134
```

### Part B — remote free: 락 대신 lock-free 스택

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
<defs><marker id="m6b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" fill="currentColor" font-weight="bold">arena 0 의 remote 스택: 여러 스레드가 push, 주인은 통째로 가져간다</text>
<rect x="20" y="40" width="190" height="56" rx="4" class="f-ok" stroke="currentColor"/>
<text x="30" y="60" fill="currentColor">소비자 T5: free(p)</text>
<text x="30" y="80" fill="currentColor" font-family="monospace" font-size="12">p-&gt;next = old</text>
<rect x="20" y="110" width="190" height="56" rx="4" class="f-ok" stroke="currentColor"/>
<text x="30" y="130" fill="currentColor">소비자 T6: free(q)</text>
<text x="30" y="150" fill="currentColor" font-family="monospace" font-size="12">CAS(head, old→q) release</text>
<rect x="270" y="80" width="80" height="34" rx="4" class="f-warn" stroke="currentColor"/>
<text x="285" y="102" fill="currentColor" font-family="monospace">head</text>
<path d="M210 68 L268 92" stroke="currentColor" stroke-width="1.5" marker-end="url(#m6b)"/>
<path d="M210 138 L268 104" stroke="currentColor" stroke-width="1.5" marker-end="url(#m6b)"/>
<rect x="380" y="80" width="44" height="34" class="f-acc" stroke="currentColor"/><text x="396" y="102" fill="currentColor">q</text>
<rect x="444" y="80" width="44" height="34" class="f-acc" stroke="currentColor"/><text x="460" y="102" fill="currentColor">p</text>
<rect x="508" y="80" width="44" height="34" class="f-acc" stroke="currentColor"/><text x="522" y="102" fill="currentColor">r</text>
<path d="M350 97 L378 97" stroke="currentColor" marker-end="url(#m6b)"/>
<path d="M424 97 L442 97" stroke="currentColor" marker-end="url(#m6b)"/>
<path d="M488 97 L506 97" stroke="currentColor" marker-end="url(#m6b)"/>
<text x="560" y="102" fill="currentColor" font-size="12">next = payload 첫 워드</text>
<rect x="270" y="190" width="300" height="74" rx="4" class="f-mut" stroke="currentColor"/>
<text x="280" y="210" fill="currentColor">arena 0 을 쓰는 스레드의 slow path</text>
<text x="280" y="230" fill="currentColor" font-family="monospace" font-size="12">lock(a0); list = exchange(head, NULL)</text>
<text x="280" y="248" fill="currentColor" font-family="monospace" font-size="12">  acquire → 각 노드 arena_free(병합)</text>
<path d="M310 114 L330 188" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 3" marker-end="url(#m6b)"/>
<text x="600" y="190" fill="currentColor" font-size="12">ABA 없음:</text>
<text x="600" y="208" fill="currentColor" font-size="12">주인은 head 를 읽고</text>
<text x="600" y="226" fill="currentColor" font-size="12">next 를 미리 읽은 뒤</text>
<text x="600" y="244" fill="currentColor" font-size="12">CAS 하지 않는다</text>
<text x="20" y="282" fill="currentColor" font-size="12">push 쪽 CAS 는 head 값만 비교해도 안전: 새 노드의 next 를 "지금 head" 로 맞추는 것뿐이라 그 사이 head 가 어땠는지는 상관없다</text>
</svg>
```

remote free 를 처리하는 방법은 세 가지다.

| 방법 | 해제하는 스레드가 하는 일 | 장점 | 단점 |
|---|---|---|---|
| ① 주인 락 (`RF_LOCK`) | 주인 arena 락을 잡고 바로 `arena_free` | 단순, 메모리가 바로 돌아감 | 주인과 해제자들이 같은 락에 줄 섬 |
| ② 내 캐시에 넣기 | 남의 블록도 내 tcache 에 넣고, 넘칠 때 주인에게 | 해제가 가장 쌈 | 메모리가 소비자 쪽에 쌓이고 결국 주인 락이 필요. 캐시에 섞인 블록을 구분해야 함 |
| ③ 지연 해제 스택 (`RF_STACK`, 이 레슨) | 주인의 lock-free 스택에 CAS push | 해제자는 주인 락을 안 잡음 | 주인이 회수할 때까지 메모리가 묶임. 주인 스레드가 사라지면 누군가 대신 회수해야 함 |

glibc 의 tcache 는 ②에 가깝다고 알고 있다(해제하는 스레드의 tcache 에 넣고, 넘치면 그 청크의 arena 락을 잡음). mimalloc 은 ③이다(page 마다 `thread_free` 리스트에 원자적으로 push, 주인이 모아 감). 이 레슨은 "남의 블록은 내 tcache 에 안 넣는다" 로 정했다. 테스트 `g_b_tc_after == 0` 이 그것을 확인한다. 그래야 tcache 의 모든 블록이 자기 arena 소유라서 flush 가 락 하나로 끝난다.

**ABA 가 없는 이유.** DS-A04 의 ABA 는 pop 이 "head 를 읽고 → head->next 를 읽고 → CAS(head: A → next)" 하는 사이에 A 가 빠졌다가 돌아오는 경우다. 여기 주인은 하나씩 pop 하지 않고 `exchange(head, NULL)` 로 리스트 전체를 가져간다. 미리 읽은 next 가 없으니 창이 없다. push 의 CAS 는 "새 노드의 next = 내가 본 head" 를 맞추는 것뿐이다. head 가 A → B → A 로 바뀌었다 돌아와도, CAS 가 성공하는 순간 p->next 는 그 순간의 head 와 같다. 그래서 틀릴 수 없다. 연습 5 가 같은 강제 스케줄을 두 방식에 돌려 pop-one 만 깨지는 것을 보인다(`stack now "BC" while T2 still owns B`).

**메모리 순서.** 소비자가 `p->next = old` 를 쓰고 CAS 로 p 를 공개한다. 주인은 exchange 로 p 를 받고 `p->next` 를 읽은 뒤 헤더·푸터를 고친다. 이 둘 사이에 happens-before 가 있어야 하므로 CAS 성공은 release, exchange 는 acquire 다. 둘 다 relaxed 로 바꾼 사본(`/tmp/OS-M06_work/sol_relaxed.c`)을 TSan 으로 돌린 실제 결과다. 테스트는 31/31 통과했는데(이 맥 arm64 에서 우연히 맞게 돌았다) TSan 은 경고 26개를 냈다(exit 134):

```text
WARNING: ThreadSanitizer: data race (pid=8806)
  Read of size 8 at 0x00010ea01560 by thread T14 (mutexes: write M0):
    #0 mm_malloc sol_relaxed.c:344 (OS-M06_relax:arm64+0x100000d6c)
    #1 consumer sol_relaxed.c:728 (OS-M06_relax:arm64+0x100008738)

  Previous write of size 8 at 0x00010ea01560 by thread T15:
    #0 mm_free sol_relaxed.c:359 (OS-M06_relax:arm64+0x100001534)
    #1 consumer sol_relaxed.c:726 (OS-M06_relax:arm64+0x100008708)
…
   3 SUMMARY: ThreadSanitizer: data race sol_relaxed.c:125 in coalesce
  14 SUMMARY: ThreadSanitizer: data race sol_relaxed.c:156 in arena_malloc
   9 SUMMARY: ThreadSanitizer: data race sol_relaxed.c:344 in mm_malloc
```

344 번째 줄은 (인라인된) `remote_drain` 이 next 를 읽는 곳, 359 번째 줄은 (인라인된) `remote_push` 가 next 를 쓰는 곳이다. 이 보고에서는 두 소비자 스레드가 서로 만났다(T14 는 malloc 하다가 자기 arena 의 remote 스택을 회수, T15 가 그 블록을 push 했음). 주인이 락을 쥐고 있어도(`mutexes: write M0`) 소용없다. 소비자는 그 락을 잡은 적이 없기 때문이다. OS-M08 의 메시지 패싱 버그와 같은 모양이다.

**언제 회수하나.** 주인 arena 를 쓰는 스레드가 **어차피 락을 잡을 때**(tcache miss, 큰 블록) `remote_drain` 을 먼저 한다. 락 없는 fast path 에는 넣지 않는다. 그래서 회수가 늦을 수 있다. Part C 실측에서 실행 중 회수 19993개, 종료 후 `drain_all` 이 나머지 7개를 가져갔다(실행마다 다름). 주인 스레드가 아예 끝나 버리면 같은 arena 에 묶인 다른 스레드나 `mm_drain_all` 이 대신 회수한다. mimalloc 은 주인 스레드가 끝난 heap 을 "abandoned" 로 표시하고 다른 스레드가 가져간다고 알고 있다(미확인).

### Part B — 실측: remote free 정책 비교

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o demo_bench && ./demo_bench    # 약 9 초 (solution.c 를 MM_NO_MAIN 으로 include)
```

W1 표는 위 Part A 에 있다. W2 는 생산자 → 소비자 짝(모든 free 가 remote, arena 하나에 해제자 1명), W3 는 생산자 1 → 소비자 k(**모든 free 가 arena 하나로 몰림**, Part C 와 같은 모양)다. 같은 실행(load 16.3)의 실제 출력:

```text
W2 producer->consumer (every free is remote), Mops/s, best of 5
allocator                  1 pair  2 pairs  4 pairs
global lock (1 arena)        22.0     14.0      9.9
arenas x8, remote=lock       20.3     42.1     70.9
arenas x8, remote=stack      17.7     42.7     78.9
arenas+tcache, stack         21.8     32.6     84.7
system malloc (libc)         46.9     90.0    108.6

W3 fan-in: 1 producer -> k consumers (all frees go to ONE arena), Mops/s, best of 5
allocator                     k=1      k=3      k=7
arenas x8, remote=lock       24.2     11.7      9.2
arenas x8, remote=stack      21.2     19.3     20.2
arenas+tcache, stack         23.8     18.9     19.1
system malloc (libc)         51.6     53.3     52.4
```

- W2 에서 arena 세 줄은 **잡음 안에서 같다**(세 번 돌린 4 pairs 칸: lock 70.9/27.8/44.6, stack 78.9/74.6/47.5, tcache 84.7/56.8/89.6). arena 하나에 해제자가 한 명뿐이라 주인 락이 거의 안 붐빈다. 확실한 것은 전역 락보다 5–9배 빠르다는 것이다.
- W3 에서 차이가 난다. `remote=lock` 은 소비자가 늘수록 7명이 생산자와 같은 락에 줄을 서서 24.2 → 9.2 로 떨어진다. `remote=stack` 은 생산자 혼자 할당하는 속도(약 20 Mops/s)에서 평평하다. 세 번 실행한 k=7 칸: lock 9.2/12.1/6.6, stack 20.2/19.9/18.9. 생산자가 병목이라 tcache 는 W3 에서 더 보태지 못했다.
- macOS libc malloc 은 W1 과 W2/W3 모두 우리보다 빠르다. Apple libmalloc 은 크기별 영역(tiny/small)과 CPU 별 "magazine" 구조라고 알고 있다(man page 에는 없음, 미확인).

같은 `demo_bench.c` 를 Lima VM `elinux`(Ubuntu, 6 vCPU, gcc 15.2.0, glibc 2.43, 커널 7.0.0-38-generic, VM load 0.72)에서 돌렸다. VM 안은 한가하지만 호스트는 위와 같이 바빴다.

```text
W1 local churn, Mops/s (malloc or free = 1 op), best of 5
allocator                   1 thr    2 thr    4 thr    8 thr
global lock (1 arena)        36.8     21.5      9.1      6.7
arenas x8                    44.7     74.8    114.0    142.4
arenas x8 + tcache           65.7    123.3    210.4    253.8
system malloc (libc)        152.5    250.7    459.1    529.9

W2 producer->consumer (every free is remote), Mops/s, best of 5
global lock (1 arena)         5.8      8.7      7.6
arenas x8, remote=lock        6.4      9.6     20.4
arenas x8, remote=stack      15.8     25.2     48.5
arenas+tcache, stack         20.5     33.0     61.3
system malloc (libc)          7.6     13.0     23.8

W3 fan-in: 1 producer -> k consumers (all frees go to ONE arena), Mops/s, best of 5
arenas x8, remote=lock        6.5      4.1      2.7
arenas x8, remote=stack      14.4      8.1      8.3
arenas+tcache, stack         19.3     10.4     10.4
system malloc (libc)          6.2      5.1      3.0
```

glibc 는 자기 스레드 안의 할당·해제(W1)에서는 우리보다 2–3배 빠르다(tcache 는 bin 검사 몇 개와 포인터 두 번이면 끝난다). 하지만 생산자 → 소비자(W2/W3)에서는 우리의 `remote=lock` 과 비슷하게 느리고, `remote=stack` 이 2–3배 앞섰다. 이 결과만으로 glibc 내부 동작을 단정할 수는 없지만, "남의 arena 청크는 결국 그 arena 락을 잡아야 반납된다" 는 설명과 맞는다. 6 vCPU 에 8 스레드라 W1 8 스레드 칸은 과구독이다.

### Part C — 패킷 파이프라인: 누가 메모리를 쥐고 있나

solution 실제 출력:

```text
-- Part C: packet pipeline RX(arena 0) -> 3 workers(arenas 1..3), shutdown
  packets=20000 bad=0 remote=20000 pushed=20000 drained-during-run=19993 worker-arena live after exit=0
PASS done == NPKT && bad == 0 && remote == NPKT && pushed == NPKT
PASS g_rx_stuck_before_exit == 0 && atomic_load(&G.st_put) > 0
  after drain_all: drained=20000 live=0 bad_free=0
```

C1(종료 때 tcache flush)만 뺀 사본으로 같은 프로그램을 돌리면(`/tmp/OS-M06_work/noc1.c`, 실측):

```text
  stress tcache+stack: got=24000 remote=18094 pushed=18094 drained=18094 errs=0 dup=0 unowned=0 live=772992
  packets=20000 bad=0 remote=20000 pushed=20000 drained-during-run=19994 worker-arena live after exit=2304
  after drain_all: drained=20000 live=2656 bad_free=0
24/31 passed
```

워커 3개가 남긴 2304 B 는 작아 보인다. 하지만 스트레스(스레드 8개, 크기 다양)에서는 772992 B 가 죽은 스레드의 bin 에 갇혔다. 스레드를 요청마다 만들고 버리는 서버라면 이것이 **스레드 수만큼 계속 쌓이는** 누수다. 펌웨어 쪽 교훈 세 가지:

1. **스레드 종료 경로**에 할당기 정리가 있어야 한다. glibc 는 스레드 종료 때 tcache 를 비운다고 알고 있고, `pthread_key_create` 소멸자 같은 장치를 쓴다. 이 레슨은 결정적인 테스트를 위해 명시적 `mm_thread_exit()` 을 썼다.
2. **RX 가 할당하고 워커가 해제하는** 구조는 스레드 캐시 할당기의 최악에 가깝다. 할당 쪽 캐시는 늘 비고, 해제 쪽은 늘 남의 블록을 받는다. 커널 드라이버라면 RX 링 버퍼를 **고정 크기 풀**로 미리 할당해서(DS-M03 슬랩, RTOS-B11 의 풀) 재활용하는 것이 보통이다. 사용자 공간이라면 버퍼를 워커가 RX 에게 **돌려보내는** 반환 큐를 두기도 한다.
3. FreeRTOS 의 heap_4 는 이 레슨의 `MT_GLOBAL` 과 같다. 힙 전체를 `vTaskSuspendAll()` 하나로 보호한다(RTOS-M10). 싱글 코어 MCU 에서는 그것으로 충분하다. SMP RTOS(FreeRTOS SMP, Zephyr)에서 할당이 핫 패스에 있으면 코어별 풀을 두는 것이 이 레슨의 arena/tcache 와 같은 해법이다. ISR 에서는 어떤 경우에도 일반 힙을 쓰지 않는다(C-M14).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution     # 31/31 passed, exit 0 (5번 반복 모두 같음, 다른 cwd 에서도)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o s_asan && ./s_asan   # 31/31, 보고 0
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o s_tsan && ./s_tsan                                     # 31/31, WARNING 0
```

실행 시간: `-O2` 0.07 초, ASan+UBSan 0.28 초, TSan 0.90 초. Lima VM 에서 gcc 15.2 로 빌드해도 31/31 이었다.

## 7. 자주 하는 실수

**1. tcache 에 넣을 때 헤더의 alloc 비트를 지운다.** "캐시에 들어갔으니 free" 라고 표시하고 싶어진다. 그러면 ① 이웃이 해제될 때 병합 코드가 이 블록을 free 로 보고 리스트에서 빼려다 망가뜨린다. ② 헤더를 락 없이 쓰게 된다. 같은 arena 를 쓰는 다른 스레드가 락 아래에서 그 헤더를 읽는 순간 data race 다. → tcache·remote 스택의 블록은 헤더를 그대로 둔다(alloc=1). 링크는 payload 첫 워드에만.

**2. refill 한 블록의 실제 크기를 확인하지 않는다.** `place()` 는 남는 부분이 32 B 미만이면 블록을 통째로 준다. 그래서 asize 64 를 요청해도 80 B 블록이 나올 수 있다. 그것을 64 bin 에 넣으면 나중에 `mm_check` 가 "bin 3: bad block" 을 낸다. 이 레슨을 처음 짤 때 실제로 T1–T4 tcache 재생이 모두 이렇게 실패했다. → refill 은 정확한 크기만 받고, 아니면 돌려준다(B2). 사용자에게 주는 블록은 커도 상관없다.

**3. remote 스택을 하나씩 pop 한다.** `old = head; next = old->next; CAS(head, old, next)` 는 ABA 에 걸린다(연습 5, DS-A04). → 주인 하나만 회수하고 `exchange(head, NULL)` 로 통째로.

**4. push/drain 을 relaxed 로 한다.** 원자 변수만 원자적이면 된다고 생각하기 쉽다. 하지만 공개해야 하는 것은 `p->next`(일반 메모리)다. → CAS 성공 release, exchange acquire. relaxed 판은 테스트는 통과하고 TSan 은 경고 26개(5. 풀이).

**5. 남의 블록을 내 tcache 에 넣는다(이 설계에서).** flush 는 "자기 arena 락 하나" 를 가정한다. 남의 블록이 섞이면 그 블록을 내 arena 에 `arena_free` 해서 **다른 arena 의 힙을 내 락으로 고친다**. 힙이 깨진다. → 주인 비교 후 remote 경로로(`o != me`). 남의 블록도 캐시하려면 flush 때 블록마다 주인을 보고 나눠야 한다.

**6. 스레드 종료 때 tcache 를 안 비운다.** 테스트에서는 끝에 `live == 0` 이 안 맞는 것으로만 보이지만, 실제로는 스레드마다 최대 227 KiB 가 영원히 사라진다(실측 772992 B). → C1.

**7. ASan 이 커스텀 할당기 안의 오버플로를 잡아 줄 거라고 믿는다.** ASan 은 자기 malloc 이 준 블록의 경계만 안다. 우리 힙은 큰 mmap 하나라서, 그 안의 블록 경계를 넘는 쓰기는 정상 쓰기로 보인다. 48 B 블록에 40 B 를 써서 payload 32 B 와 푸터를 덮은 실험(`/tmp/OS-M06_work/ovf.c`, ASan+UBSan 빌드)의 실제 결과다. 쓰는 순간에는 아무 보고가 없었고, 다음 free 의 병합에서야 터졌다.

```text
solution.c:123:35: runtime error: pointer index expression with base 0x00010c408050 overflowed to 0xffff878893c807e0
AddressSanitizer:DEADLYSIGNAL
==8854==ERROR: AddressSanitizer: SEGV on unknown address 0xfffff161127b00fb (pc 0x000104cc8090 bp 0x00016b13eb00 sp 0x00016b13eac0 T0)
    #0 0x000104cc8090 in list_remove solution.c:114
    #1 0x000104cc7a10 in coalesce solution.c:123
    #2 0x000104cc3128 in mm_free solution.c:363
```

→ 할당기에서 `ASAN_POISON_MEMORY_REGION` / `ASAN_UNPOISON_MEMORY_REGION`(`<sanitizer/asan_interface.h>`, AddressSanitizer 의 manual poisoning)으로 헤더·푸터·free 블록을 독으로 표시하면 잡을 수 있다. 이 레슨에서는 하지 않았다.

## 8. 면접 Q&A

<details><summary>Q1. 멀티스레드에서 malloc 이 느려지는 이유와, 이를 푸는 구조를 단계별로 설명해 보세요.</summary>

전역 락 하나면 모든 malloc/free 가 직렬화되고, 락과 힙 메타데이터의 캐시 라인이 코어 사이를 오간다. 실측: 전역 락 1 → 8 스레드에서 42.8 → 13.2 Mops/s(더 느려짐). 1단계는 **arena 여러 개**다. 스레드를 arena 에 나눠 묶어 경합을 없앤다(188.1). 2단계는 **스레드 캐시**다. 작은 크기는 스레드 지역 bin 에서 락 없이 처리하고, arena 와는 묶음으로만 오간다(289.8, 1 스레드에서도 2.2배). 3단계는 **remote free** 처리다. 다른 스레드가 해제한 블록을 주인 락 없이 돌려보낸다(W3 k=7: 9.2 → 20.2). 꼬리: "그럼 arena 를 스레드 수만큼 만들면?" → 메모리가 arena 마다 따로 놀아서(각자 free 공간을 쥠) RSS 가 늘어난다. glibc 가 상한을 두고 `MALLOC_ARENA_MAX` 로 줄이게 하는 이유다.
</details>

<details><summary>Q2. tcache 는 왜 bin 당 개수를 제한하나요? 7 이라는 숫자는요?</summary>

캐시 안 블록은 그 스레드만 쓸 수 있고 병합도 안 된다. 제한이 없으면 한 스레드가 해제한 메모리가 거기 쌓여 다른 스레드는 힙을 늘려야 한다(blowup). 63 bin × 7 개면 스레드당 최악 232848 B 다. 7 은 glibc 의 기본값(`tcache_count`, tunable `glibc.malloc.tcache_count` 로 바꿀 수 있음. tunable 이름은 VM 의 `ld.so --list-tunables` 에서 확인, 기본값 7 은 기억)을 따랐다. 더 중요한 것은 넘침·고갈 때 **묶어서** 옮기는 것이다. 이 레슨은 flush 3, refill 3 → 테스트에서 10번 할당 + 10번 해제의 락이 20 → 5번. OSTEP 29장 sloppy counter 의 임계값 S 와 같은 거래다. 크면 확장성이 좋고, 작으면 메모리가 정확하다. 꼬리: "메모리가 부족한 임베디드 보드라면?" → bin 개수·최대 크기를 줄이거나(tunable), tcache 를 끄고 arena 수를 1–2 로.
</details>

<details><summary>Q3. 다른 스레드가 해제한 블록(remote free)은 어떻게 처리하나요?</summary>

세 가지다. ① 주인 arena 락을 잡고 바로 반납(단순하지만 주인과 줄을 섬). ② 해제한 스레드의 캐시에 넣었다가 넘칠 때 주인에게(빠르지만 메모리가 소비자 쪽에 몰림). ③ 주인의 lock-free 스택에 push, 주인이 다음 slow path 에서 통째로 회수(mimalloc 의 thread-delayed free 아이디어, 이 레슨). 실측 W3(소비자 7 → arena 하나): ① 9.2, ③ 20.2 Mops/s. VM glibc 는 같은 부하에서 3.0 이었다. 대가: ③은 주인이 회수할 때까지 메모리가 묶인다(Part C: 실행 중 19993개 회수, 7개는 종료 때). 꼬리: "주인 스레드가 죽으면?" → 같은 arena 를 쓰는 다른 스레드나 종료 경로(`mm_drain_all`)가 회수한다. mimalloc 은 버려진 heap 을 다른 스레드가 가져간다고 알고 있다.
</details>

<details><summary>Q4. remote 스택은 lock-free 스택인데 ABA 는 괜찮나요? 메모리 순서는요?</summary>

ABA 는 pop 이 "head 와 head->next 를 읽고 → CAS" 하는 사이에 head 가 같은 주소로 돌아올 때 생긴다. 여기서는 해제자는 push 만 하고, 주인은 `exchange(head, NULL)` 로 전체를 가져간다. 미리 읽는 next 가 없으니 ABA 창이 없다(연습 5: pop-one 은 강제 스케줄에서 B 가 두 번 나가고, exchange-all 은 멀쩡). push 의 CAS 는 head 값만 비교해도 된다. 성공 순간 `p->next == head` 이기만 하면 되기 때문이다. 순서: push 는 `p->next` 를 쓰고 release CAS 로 공개하고, 주인은 acquire exchange 로 받아 next 와 헤더를 읽는다. relaxed 로 바꾸면 TSan 이 data race 26개를 보고했다(테스트는 통과). 꼬리: "x86 이면 relaxed 여도 되나?" → 하드웨어(TSO)는 대체로 맞게 돌겠지만 C11 모델로는 data race = UB 이고, 컴파일러가 순서를 바꿀 수 있다.
</details>

<details><summary>Q5. free(p) 는 크기도 arena 도 안 받는데, 주인 arena 를 어떻게 찾나요?</summary>

이 레슨은 헤더 bit48..55 에 owner id 를 넣고(O(1)), 그 arena 영역 안인지·alloc 비트·푸터 == 헤더로 검증한다. 다른 방법은 주소 정렬이다. glibc 는 main 이 아닌 arena 의 heap 을 큰 경계에 정렬해 `p & ~(HEAP_MAX_SIZE-1)` 로 heap 헤더를 찾는다(청크에는 `NON_MAIN_ARENA` 비트 하나). mimalloc 은 segment 정렬 마스크, jemalloc 은 주소 → extent radix tree 다(모두 기억 기준). 헤더 방식은 헤더가 덮이면 틀린 arena 로 간다. 그래서 검증이 필요하고, 그래도 오버플로를 완전히 막지는 못한다(7. 실수 7). 꼬리: "헤더 없이 크기는 어떻게 아나?" → 크기 클래스별 page/slab 을 쓰면 page 메타데이터에 크기가 있다(DS-M03 의 주소 마스킹).
</details>

<details><summary>Q6. 임베디드 리눅스 보드에서 데몬의 RSS 가 계속 커집니다. 할당기 관점에서 무엇을 의심하나요?</summary>

① arena 수: glibc 는 스레드 경합이 보이면 arena 를 더 만들고, 상한은 CPU 수의 배수다(`man 3 mallopt`: M_ARENA_TEST 64비트 기본 8, 상한 계산은 "implementation-defined, usually a multiple of the number of available CPUs"). arena 마다 자기 free 공간과 top chunk 를 쥐므로 스레드가 많으면 RSS 가 커진다. `MALLOC_ARENA_MAX=1` 또는 `2` 로 줄여 보고 처리량과 비교한다. ② 스레드 캐시가 쥔 메모리: 이 레슨 기준 스레드당 최대 227 KiB. ③ 생산자/소비자 패턴: 해제가 늘 남의 스레드에서 일어나면 메모리가 회수되기 전까지 묶인다. ④ 단편화와 trim: free 공간이 힙 중간에 흩어져 있으면 OS 에 돌려주지 못한다(`malloc_trim`). 확인 방법은 `/proc/<pid>/smaps` 의 heap/anon 영역, `malloc_info()`, 스레드 수. 꼬리: "근본 해결은?" → 핫 패스의 고정 크기 객체를 풀로 빼고(DS-M03), 스레드 수를 줄이거나 버퍼를 재활용한다.
</details>

<details><summary>Q7. 스레드가 끝날 때 할당기는 무엇을 해야 하나요? 안 하면요?</summary>

자기 tcache 의 모든 블록을 arena 로 돌려주고(C1) arena 바인딩을 푼다. tcache 블록은 arena 입장에서 "할당됨" 이라 아무도 해제하지 않는다. C1 을 뺀 실측: 스트레스 후 772992 B, 파이프라인 후 2656 B 가 영원히 남았다. 요청마다 스레드를 만드는 서버라면 스레드 수에 비례해 새고, 스레드 풀이라면 풀 크기만큼 묶인다. glibc 는 스레드 종료 때 tcache 를 정리한다고 알고 있다. 꼬리: "`pthread_cancel` 이나 `_exit` 은?" → 소멸자 경로를 타지 않을 수 있다. 프로세스가 끝나는 `_exit` 은 어차피 주소 공간 전체가 사라지니 상관없고, 문제는 오래 사는 프로세스 안의 스레드다.
</details>

<details><summary>Q8. 이 할당기를 SMP RTOS(예: 듀얼 코어 Cortex-M33/A 계열 펌웨어)로 옮긴다면 무엇을 바꾸나요?</summary>

① `_Thread_local` 대신 **코어별** 캐시(태스크는 코어를 옮겨 다니므로 tcmalloc 의 per-CPU 와 비슷하게). 코어별 캐시에 접근하는 동안은 선점을 막거나 인터럽트를 짧게 끈다. ② arena 락은 RTOS 뮤텍스 대신 짧은 스핀락 + 인터럽트 마스크(OS-M07), 또는 heap_4 처럼 스케줄러 정지(RTOS-M10). ③ remote free 스택은 그대로 쓸 수 있다. Cortex-M 은 LDREX/STREX(LL/SC)로 CAS 를 만든다. ④ ISR 에서는 이 할당기를 부르지 않는다. ISR 은 미리 할당한 풀이나 링만 쓴다(C-M14). ⑤ 실시간성: segregated list 의 first-fit 은 O(1) 이 아니다. 최악 시간이 필요하면 TLSF(DS-A06)나 고정 크기 풀. 꼬리: "코어가 2개면 tcache 가 필요한가?" → 경합보다 락 비용과 결정성이 문제라면 코어별 고정 크기 풀만으로 충분한 경우가 많다.
</details>

## 9. 추가 연습

### 연습 1 — bin 번호와 hoarding 상한

요청 1, 16, 17, 40, 1008, 1009 B 는 각각 어느 bin 으로 가나? 모든 bin 이 7개(또는 3개)씩 찼을 때 스레드 하나가 쥐는 바이트는?

<details><summary>답</summary>

블록 = round_up(n + 16, 16), 최소 32. bin = 블록/16 − 2. 1, 16 → 블록 32 → bin 0. 17 → 48 → 1. 40 → 64 → 2. 1008 → 1024 → 62. 1009 → 1040 > 1024 → tcache 대상 아님(−1). 상한 = count × Σ(32 + 16i, i = 0..62) = count × 33264. count 7 → 232848 B, count 3 → 99792 B, 8 스레드 × 7 → 1862784 B(실측 출력 그대로). practice.c `tc_idx_req`, `hoard_bytes`.
</details>

### 연습 2 — 묶음 remote free (CAS 한 번에 사슬 하나)

해제하는 스레드가 같은 주인에게 갈 블록 16개를 지역에서 미리 엮어 두고, CAS 한 번으로 사슬 전체를 push 하라. 4 스레드 × 20000 노드, 그동안 다른 스레드가 계속 drain 한다.

<details><summary>답</summary>

`last->next = old; CAS(head, old → first)`. 사슬 안의 링크는 공개 전에 지역에서 썼으므로 release CAS 하나로 모두 공개된다. 실측: CAS 5000번으로 80000 노드, drain 이 모든 노드를 정확히 한 번씩 봤다. CAS 횟수가 1/16 이 되어 head 캐시 라인의 경합이 준다. mimalloc·jemalloc 도 remote free 를 모아서 보내는 장치가 있다고 알고 있다(세부는 미확인). practice.c `push_chain`.
</details>

### 연습 3 — trylock 으로 안 바쁜 arena 고르기

arena 4개의 뮤텍스가 있을 때, `start` 부터 돌며 `pthread_mutex_trylock` 이 성공하는 첫 arena 를 잡고, 모두 바쁘면 `start` 를 기다리는 `pick_arena` 를 써라.

<details><summary>답</summary>

아무도 안 쥐면 0, 0 을 쥔 상태에서 다시 고르면 1(내가 쥔 기본 뮤텍스에 trylock 해도 EBUSY. POSIX `pthread_mutex_trylock` 은 "현재 잠겨 있으면 (호출 스레드 자신 포함) 즉시 EBUSY"), 1·2 가 바쁘면 3. glibc 가 arena 경합을 감지하고 다른 arena 로 옮기는 생각의 축소판이다(glibc 의 실제 정책은 소스 미확인). 대가: 스레드가 arena 를 옮겨 다니면 tcache 의 "모든 블록은 내 arena 것" 가정이 깨지므로, 옮길 때 tcache 를 flush 해야 한다. practice.c `pick_arena`.
</details>

### 연습 4 — sloppy 카운터로 live 바이트 세기

스레드마다 지역 델타를 모으다가 |델타| ≥ S(4096) 일 때만 전역 락을 잡고 더하라. 4 스레드 × 50000 번 뒤 전역 값의 오차 상한은?

<details><summary>답</summary>

각 스레드에 남은 지역 값은 |local| < S 이므로 오차 < 스레드 수 × S = 16384. 실측: exact 400227, approx 402812, 오차 2585, 그리고 approx + 남은 지역 값 합 == exact. OSTEP 29장의 sloppy counter 그대로다. tcache 의 bin 용량, arena 통계(`live_bytes` 를 스레드별로 모았다가 flush 때 반영)가 같은 구조다. 할당기가 "정확한 RSS" 를 싸게 알 수 없는 이유이기도 하다.
</details>

### 연습 5 — ABA: pop-one 은 깨지고 exchange-all 은 안 깨진다

스택 A→B→C. T1 이 `top=A, next=B` 를 읽고 멈춘 사이 T2 가 pop A, pop B, push A 를 한다. T1 의 `CAS(top, A → B)` 결과는? 같은 상황에서 주인이 `exchange` 로 가져가면?

<details><summary>답</summary>

CAS 가 성공해서 스택이 `"BC"` 가 된다. 그런데 B 는 T2 가 쓰고 있다. 같은 블록이 두 번 나가는 것이다. 할당기라면 두 스레드가 같은 메모리를 받는다(소유 비트맵의 `g_dup` 이 잡는 버그). exchange 는 head 와 next 를 따로 읽지 않으므로 이런 창이 없다. 실측 출력 `owner got "XYZ", stack now "W"`. 해결책 전체 비교(태그, hazard pointer, epoch)는 DS-A04. practice.c 는 스레드 없이 단계를 손으로 끼워 넣어 결정적으로 재현한다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice    # 12/12 passed, exit 0
```

실제 출력 일부:

```text
  per-thread worst case: count 7 -> 232848 B, count 3 -> 99792 B, 8 threads x count 7 -> 1862784 B
  CAS pushes=5000 nodes drained=80000 seen-exactly-once=80000
  exact=400227 approx=402812 |error|=2585 bound=16384
  pop-one: T1 CAS succeeded, stack now "BC" while T2 still owns B
  exchange-all: owner got "XYZ", stack now "W"
12/12 passed
```

ASan+UBSan, TSan 빌드에서도 12/12, 보고 0.

## 10. 핵심 정리

- 전역 락 malloc 은 스레드가 늘면 **느려진다**(실측 42.8 → 13.2 Mops/s). 해법은 두 층이다. arena 여러 개(경합 제거)와 스레드 캐시(락 횟수 자체 제거, 1 스레드에서도 2.2배).
- 헤더에 owner arena 를 넣으면 free 가 O(1) 로 주인을 찾는다. 검증(영역·alloc·푸터)이 없으면 망가진 헤더 하나가 남의 힙을 고친다.
- tcache: bin 당 상한(7)이 메모리 blowup 을 막고, 넘침·고갈 때 **묶음**(flush 3, refill 3)으로 옮겨 락을 줄인다. tcache 블록은 헤더를 안 건드린다(alloc=1). 그래서 이중 해제는 bin 을 훑어 잡는다.
- remote free 는 주인 락(단순) / 내 캐시(빠름, 메모리 쏠림) / 주인의 lock-free 스택(이 레슨) 중 고른다. 스택은 push 만 CAS, 회수는 exchange 로 통째로 → ABA 없음. release/acquire 필수(relaxed 면 TSan 26개).
- fan-in(생산자 1 → 소비자 7)에서 stack 이 lock 보다 2배 이상(9.2 → 20.2). 1:1 에서는 잡음 안에서 같다.
- 스레드 종료 때 tcache 를 비워야 한다(안 하면 772992 B 누수). 회수가 늦은 remote 스택은 종료 경로에서 drain.
- 공간 비용: tcache 는 보통 utilization 을 깎는다(T2 0.861 → 0.809). 배치 refill 이 같은 크기를 모아 T4 처럼 좋아지는 경우도 있다(0.537 → 0.837).
- 펌웨어: 싱글 코어 RTOS 는 heap_4 의 전역 보호로 충분하다. SMP 는 코어별 풀, ISR 은 일반 힙 금지, 핫 패스는 고정 크기 풀.

## 참고자료

- R. E. Bryant, D. R. O'Hallaron, *Computer Systems: A Programmer's Perspective* 3e, §9.9 Dynamic Memory Allocation (memlib `mem_sbrk`, boundary tag, segregated fits) — 백엔드의 출처.
- OSTEP 17장 "Free-Space Management"(segregated lists, slab, buddy, Hoard 인용 [B+00]), 29장 "Lock-based Concurrent Data Structures"(approximate/sloppy counter, 임계값 S) — 로컬 `book-md/C17_*.md`, `C29_*.md` 에서 확인.
- E. D. Berger, K. S. McKinley, R. D. Blumofe, P. R. Wilson, "Hoard: A Scalable Memory Allocator for Multithreaded Applications", ASPLOS-IX, 2000 — 스레드별 힙과 blowup.
- D. Leijen, B. Zorn, L. de Moura, "Mimalloc: Free List Sharding in Action", APLAS 2019 (MSR-TR-2019-18) — thread-local heap, 지연 해제 리스트(기억 기준).
- J. Evans, "A Scalable Concurrent malloc(3) Implementation for FreeBSD" (jemalloc), BSDCan 2006 — arena, 스레드 캐시는 이후 버전(기억 기준).
- glibc: `man 3 mallopt`(M_ARENA_MAX, M_ARENA_TEST, MALLOC_ARENA_MAX — Lima VM 에서 읽음), GNU C Library manual "Memory Allocation Tunables"(`glibc.malloc.tcache_count`, `tcache_max`, `arena_max` — 이름은 VM 의 `ld.so --list-tunables` 로 확인) <https://www.gnu.org/software/libc/manual/html_node/Memory-Allocation-Tunables.html>, glibc wiki MallocInternals <https://sourceware.org/glibc/wiki/MallocInternals>.
- POSIX `pthread_mutex_trylock` <https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_trylock.html>, C11 7.17 (atomics, memory_order).
- AddressSanitizer manual poisoning <https://github.com/google/sanitizers/wiki/AddressSanitizerManualPoisoning>, ThreadSanitizer <https://clang.llvm.org/docs/ThreadSanitizer.html>.
- 관련 레슨: OS-M04/OS-M05(백엔드), DS-M03(슬랩·매거진·주소 마스킹), DS-M04(버디), DS-A04(ABA), DS-A06(TLSF), OS-M07(스핀락), OS-M08(release/acquire 메시지 패싱), RTOS-M10(heap_4), C-M14(ISR 규칙), ARCH-M08(false sharing — arena 구조체 128 B 정렬 이유).
