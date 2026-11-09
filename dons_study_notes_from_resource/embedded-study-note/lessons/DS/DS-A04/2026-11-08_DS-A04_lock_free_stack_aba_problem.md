---
id: DS-A04
time: 110분
updated: 2026-11-08
---
# DS-A04 Lock-free 스택과 ABA 문제

## 학습 목표

- 노드 풀 위에 CAS 기반 **Treiber 스택**을 만들고, push/pop의 load·CAS마다 **왜 그 memory_order인지**(push 성공 = release, pop의 load와 CAS = acquire, `next` = relaxed atomic) 한 문장으로 말할 수 있다.
- **ABA**를 확률에 맡기지 않고 **강제 스케줄**로 재현한다. T1이 `top=A, next=B`를 읽고 멈춘 사이 T2가 pop A, pop B, push A를 하면 T1의 CAS가 성공한다. 그 결과 B가 두 곳에 있게 되고, `B → B` 사이클이 생기고, C와 D를 잃는다. 이 깨진 상태를 무결성 검사기로 숫자로 보인다.
- 두 가지로 고친다. **(태그 32 | 인덱스 32) 64비트 CAS**와 **{포인터, 태그} 128비트 CAS**다. 128비트 CAS가 이 맥에서 CASP 계열(`caspa`/`caspl`), ARMv8.0에서 `ldaxp/stlxp`, x86에서 libcall 또는 `cmpxchg16b`로 내려가는 것을 실제 `-S`로 확인한다. **태그가 감기는 시간**은 32비트 ≈ 20 초, 16비트 ≈ 0.3 ms로 실측값에서 계산한다.
- ABA 해결책(태그, hazard pointer, epoch/RCU, LL/SC, 단일 소비자, `pop_all`)을 표로 비교한다. "하드웨어 LL/SC는 ABA에 면역인데 컴파일러 CAS는 왜 아닌가"를 asm과 실험으로 설명한다.
- lock-free 스택이 이 맥에서 **mutex보다 느리다**는 실측 결과를 보고, lock-free의 진짜 가치(진행 보장, ISR에서 써도 안전함)를 lock-free / wait-free / obstruction-free 정의로 설명한다.

## 1. 문제

"lock-free 스택을 짜 보세요. 그런데 이 코드에 ABA 문제가 있다는데, 무슨 뜻이고 어떻게 고치죠?" 시니어 면접에서 CAS를 쓰는 순간 거의 반드시 나오는 꼬리질문이다. 펌웨어에서는 **고정 크기 블록 풀**(패킷 버퍼, DMA 디스크립터, 메시지 블록)을 ISR과 태스크가 같이 alloc/free할 때 똑같은 구조가 나온다. 풀은 malloc을 안 쓰니 해제된 블록이 **바로, 같은 주소로** 다시 나간다. 그래서 ABA가 가장 잘 터지는 환경이다.

Treiber 스택(1986)은 단순하다. `top`이 맨 위 노드를 가리키고, push는 "새 노드의 next = top; CAS(top: 옛값 → 새 노드)", pop은 "옛 top의 next를 읽고 CAS(top: 옛 top → next)"다. 문제는 CAS가 **값만 비교**한다는 데 있다. pop이 `top == A`를 읽은 뒤 CAS를 하기 전에 A가 떼어졌다가 **다시 맨 위로 돌아오면**, CAS는 "여전히 A네" 하고 성공한다. 그런데 A의 next는 이미 B가 아니다. 이게 ABA다(값이 A → (다른 것) → A로 돌아와서 붙은 이름).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">강제 ABA 스케줄 (Part B) — 값 비교 CAS 는 "그 사이 무슨 일이 있었나" 를 모른다</text>
  <text x="10" y="58" fill="currentColor">T1</text>
  <text x="10" y="128" fill="currentColor">T2 (main)</text>
  <text x="10" y="208" fill="currentColor">스택</text>
  <rect x="90" y="40" width="190" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="98" y="60" fill="currentColor" font-family="monospace">old=A, next=B 읽음</text>
  <rect x="290" y="40" width="210" height="30" rx="4" class="f-warn" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="300" y="60" fill="currentColor">POP_WINDOW(): 멈춤</text>
  <rect x="510" y="40" width="220" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="518" y="60" fill="currentColor" font-family="monospace">CAS(top, A→B) 성공!</text>
  <rect x="290" y="110" width="210" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="298" y="130" fill="currentColor" font-family="monospace">pop→A  pop→B  push A</text>
  <g fill="currentColor" font-family="monospace">
    <text x="90" y="200">A→B→C→D</text>
    <text x="300" y="200">A→C→D</text>
    <text x="300" y="218">(B 는 T2 손에)</text>
    <text x="520" y="200">B→C→D</text>
    <text x="520" y="218">B: 스택에도, T2 손에도</text>
  </g>
  <path d="M110 185 L110 76" stroke="currentColor" fill="none" stroke-dasharray="3 3"/>
  <path d="M320 185 L320 146" stroke="currentColor" fill="none" stroke-dasharray="3 3"/>
  <path d="M540 185 L540 76" stroke="currentColor" fill="none" stroke-dasharray="3 3"/>
  <rect x="510" y="240" width="220" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="518" y="260" fill="currentColor" font-family="monospace">T2 가 B 반납 → B→B 사이클</text>
  <g fill="currentColor">
    <text x="10" y="296">T1 이 본 것: top 은 A 였고 지금도 A. 못 본 것: A 의 next 가 B → C 로 바뀌었고 B 는 남의 것.</text>
    <text x="10" y="318">고치는 법: top 에 "몇 번째 A 인가" 를 함께 넣는다 (세대 태그). 같은 A 라도 태그가 다르면 CAS 가 실패한다.</text>
  </g>
</svg>
```

| 파트 | 스택의 top | 체크 (결정적) | 관찰 데모 |
|---|---|---|---|
| A | `_Atomic uint32_t` 인덱스 | LIFO, 빈 스택, window 1회 | — |
| B | 인덱스 → **(태그, 인덱스) 64비트** | naive가 **깨지는 것** 자체를 체크 → tag64는 같은 스케줄 통과, 8스레드 스트레스 | `demo_aba_stress.c` (진짜 경합), `demo_llsc.c` |
| C | **{포인터, 태그} 128비트** | lock-free 여부, 강제 스케줄, ISR 생산자 2 + 워커 6 패킷 풀 | `demo_asm.c` (타깃별 asm), `demo_bench.c` |

### Part A — Treiber 스택 (top = 노드 인덱스)

- 노드는 `g_pool[64]`에만 있다(malloc 없음). 노드는 `{ _Atomic uint32_t next; uint32_t value; }`이고 `NIL = 0xFFFFFFFF`이다.
- `is_push(s, i)` / `is_pop(s)`를 CAS 루프로 만든다. pop은 top과 next를 읽은 뒤, **CAS 직전에 `POP_WINDOW()`를 부른다**. 테스트가 이 자리에 훅을 걸어 T1을 멈춘다.
- 체크: 빈 스택 pop → NIL, 64개 push 후 역순 pop(LIFO), 경합이 없으면 window 1번에 CAS 1번이면 끝난다.

### Part B — ABA 강제 재현과 수정 1: 64비트 (태그, 인덱스)

- 하네스의 `force_aba()`가 위 그림의 스케줄을 **mutex+cond 게이트**로 강제한다. 확률이 아니라 매번 같은 결과가 나온다. naive 스택에서는 다음 셋을 **체크로 확인**한다. ① T1의 CAS가 첫 시도에 성공(hits == 1). ② B가 스택 맨 위에도 있고 main 손에도 있다. ③ main이 B를 반납하면 `B → B` 사이클이 생기고, C와 D를 잃고, pop 두 번이 둘 다 B를 준다.
- `tag_pack / tag_idx / tag_tag`와 `ts_push / ts_pop`을 만든다. top = `(tag << 32) | idx`이고, **CAS가 성공할 때마다 태그 +1**이다. 같은 스케줄에서 T1의 CAS는 실패하고 한 번 재시도한다(hits == 2). 리스트는 `top → C → D`로 멀쩡하다.
- 태그가 `0xFFFFFFFF → 0`으로 감겨도 인덱스가 망가지지 않아야 한다(캐리가 인덱스로 새면 안 된다).
- 8스레드 × 20만 회 스트레스: 스레드마다 pop 2 → 소유 표시 → `value++`(**일반 변수**) → push 2(순서를 바꿔 가며). 끝에 동시 소유 0, `sum(value) == 성공한 pop 수`, 64노드가 스택에 정확히 한 번씩 있어야 한다.

### Part C — 수정 2: {포인터, 태그} 128비트 CAS로 만든 패킷 버퍼 풀

- `pkt_t`(48바이트 데이터 + seq + 체크섬)는 64개다. `pstack_t { _Atomic tagptr_t top; }`이고 `tagptr_t = { pkt_t *ptr; uintptr_t tag; }`로 정확히 16바이트, 패딩이 없다. `ps_push / ps_pop`을 만든다.
- `atomic_is_lock_free(&top)`가 참이어야 한다. 아니면 내부 락이 생기고, ISR에서 쓰면 교착된다.
- 펌웨어 시나리오: "UART RX ISR" 생산자 2개가 free 풀에서 버퍼를 꺼내 채워 ready 스택에 올린다. 워커 6개가 꺼내 검사하고 free로 반납한다. 20만 패킷이 **정확히 한 번씩** 소비되고(bitmap), 손상 0, 동시 소유 0, 끝나면 free에 64개가 모두 있어야 한다.

## 2. Problem (English)

> **Lock-free stack over a node pool, and the ABA problem.**
>
> Nodes come from a fixed pool (`node_t g_pool[64]`, `_Atomic uint32_t next`). Implement:
>
> 1. A Treiber stack with a 32-bit index head: `void is_push(istack_t *, uint32_t idx)`, `uint32_t is_pop(istack_t *)` (returns `NIL` when empty). `is_pop` must call `POP_WINDOW()` after reading `top` and `top->next` and before its CAS.
> 2. Using a forced schedule (T1 pauses inside the window; T2 pops A, pops B, pushes A), show that the naive stack is corrupted: T1's CAS succeeds, B is both on the stack and owned by T2, and after T2 frees B the list contains a cycle.
> 3. Fix it with a 64-bit head `(tag << 32) | idx` incremented on every successful CAS: `tag_pack/tag_idx/tag_tag`, `ts_push/ts_pop`. Survive the same schedule and an 8-thread push/pop stress with exact counts.
> 4. Fix it again with a 16-byte `{pkt_t *ptr; uintptr_t tag;}` head and a double-width CAS (`ps_push/ps_pop`), and use it as a lock-free packet pool shared by 2 "ISR" producers and 6 workers (every packet consumed exactly once).
>
> Use the weakest correct memory orders and justify each one. Follow-ups: how long until a 32-bit tag wraps? Is ABA possible if only one thread pops? Why can't you `free()` a popped node? What does a 16-byte CAS compile to on ARMv8.0 / ARMv8.1+ / x86-64? Is LL/SC immune to ABA? Lock-free vs wait-free? What would you do on a Cortex-M0?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과(실제 출력):

```text
== Part A: Treiber stack (index top)
FAIL starter.c:384  a_ok
FAIL starter.c:390  x == 0u && g_window_calls == 1
== Part B: forced ABA on the naive stack
SKIP naive ABA checks: Part A failed
== Part B: fix 1 — 64-bit (tag, index)
FAIL starter.c:422  w == 0x0000000300000007ull && tag_idx(w) == 7u && tag_tag(w) == 3u
FAIL starter.c:423  tag_idx(tag_pack(NIL, 0xFFFFFFFFu)) == NIL && tag_tag(tag_pack(NIL, 0xFFFFFFFFu)) == 0xFFFFFFFFu
FAIL starter.c:425  b_ok
FAIL starter.c:430  tag_idx(after) == 3u && tag_tag(after) == 0u && ts_pop(&ts) == 3u
SKIP tag64 schedule/stress: unit checks failed
== Part C: fix 2 — 128-bit {pointer, tag} packet pool
PASS atomic_is_lock_free(&ps.top)
FAIL starter.c:452  c_ok
SKIP dw128 schedule/stress: unit checks failed
1/20 passed
```

하네스 설계:
- **강제 스케줄은 게이트로 만든다.** T1 스레드에만 thread-local 훅(`tl_pop_window = gate_window`)을 건다. T1이 window에서 `paused = 1`을 알리고 기다리면, main이 자기 연산을 끝낸 뒤 `resume`한다. 모든 대기는 1초 timedwait × 3번으로 끝이 있다. 그래서 `POP_WINDOW()`를 빼먹은 구현은 "멈추지 않았다"로 FAIL하고, 멈추지는 않는다.
- 각 Part의 강제 스케줄과 스트레스는 **단위 테스트가 통과해야** 돈다. 그래서 스텁이 무한 루프나 레이스를 만들지 않는다.
- 새니타이저 빌드(`__has_feature(thread_sanitizer/address_sanitizer)`)에서는 스트레스 반복 수를 1/4로 줄인다(`SAN_SCALE`). 이 맥에서 일반 빌드는 0.5 초, TSan 빌드는 약 1–2 초 걸린다.

관찰용 파일(체크와 별개):

```bash
cc -std=c11 -O2 -pthread -DMODE=0|1|2 demo_aba_stress.c -o das && ./das      # 진짜 8스레드 경합: naive / tag64 / tag64+relaxed push
cc -std=c11 -g -O1 -fsanitize=thread -pthread -DMODE=2 -DROUNDS=1 -DITERS=20000u demo_aba_stress.c -o das_t && ./das_t
cc -std=c11 -O2 -pthread demo_llsc.c -o dl && ./dl                             # 손으로 쓴 LDAXR…STLXR pop 은 ABA 를 잡나
cc -std=c11 -O2 -S demo_asm.c -o -                                             # (+ 파일 머리의 타깃별 명령)
cc -std=c11 -O2 -pthread demo_bench.c -o db && ./db                            # lock-free vs mutex/os_unfair_lock
cc -std=c11 -O2 -pthread -mcpu=apple-m1+nolse demo_bench.c -o db_llsc && ./db_llsc
```

## 4. 힌트

<details><summary>힌트 1 — push의 CAS 루프 모양</summary>

`old`를 한 번 읽고, 루프 안에서 `next[i] = old` → `CAS(&top, &old, i)`를 한다. CAS가 실패하면 C11 CAS가 `old`를 **현재 값으로 고쳐 준다**. 그러니 다시 읽을 필요 없이 next만 다시 쓰면 된다. weak CAS를 쓰는 게 맞다(어차피 루프이고, LL/SC에서 가짜 실패를 허용하면 코드가 짧아진다).
</details>

<details><summary>힌트 2 — 어느 연산이 release이고 어느 연산이 acquire인가</summary>

질문을 바꿔 보자. "pop한 쪽이 노드의 `next`와 `value`를 **온전히** 보려면, push한 쪽의 어떤 쓰기가 먼저 일어나야 하나?" push가 `next`(와 `value`)를 쓴 뒤 top을 바꾸는 CAS가 **게시(publish)**다. 그러니 push의 CAS는 release다. pop이 top을 읽는 load가 그것을 **받는 쪽**이다. 그러니 pop의 load는 acquire다. 실패한 CAS가 새 `old`를 가져올 때도 같은 이유로 acquire가 필요하다.
</details>

<details><summary>힌트 3 — POP_WINDOW() 위치</summary>

`next`를 읽은 **뒤**, CAS **전**이다. ABA 창이 바로 이 구간이기 때문이다. top 읽기와 next 읽기 사이에 두면 T2의 pop A, pop B, push A 이후에 next를 다시 읽게 된다. 그러면 next = C를 보고 재현이 안 된다.
</details>

<details><summary>힌트 4 — 태그는 언제 올리나</summary>

가장 단순하고 안전한 규칙은 "top을 바꾸는 **모든 성공한 CAS**에서 +1"이다. 새 값은 `tag_pack(new_idx, tag_tag(old) + 1u)`로 만든다. `uint32_t` 덧셈이라 2³² 에서 0으로 감기는 것은 정의된 동작이다(C11 6.2.5p9). 64비트 전체에 `1ull << 32`를 더해도 결과는 같다. 캐리는 비트 64 밖으로 버려지기 때문이다.
</details>

<details><summary>힌트 5 — 16바이트 CAS를 C11으로</summary>

`_Atomic tagptr_t top;`을 두고 `atomic_compare_exchange_weak_explicit(&s->top, &old, nw, ...)`를 쓴다. 구조체 값을 그대로 넘긴다. 이 맥의 clang은 이걸 CASP 명령(pop은 `caspa`, push는 `caspl`)으로 인라인한다. `old.ptr->next`를 읽는 건 안전할까? 노드가 **풀에서만** 오고 OS에 반납되지 않으니 안전하다(낡은 노드를 읽어도 메모리는 살아 있다).
</details>

## 5. 풀이

### Part A — Treiber 스택과 memory_order

```c
static void is_push(istack_t *s, uint32_t i) {
    uint32_t old = atomic_load_explicit(&s->top, memory_order_relaxed);
    do {
        atomic_store_explicit(&g_pool[i].next, old, memory_order_relaxed);
    } while (!atomic_compare_exchange_weak_explicit(&s->top, &old, i,
                                                    memory_order_release, memory_order_relaxed));
}
static uint32_t is_pop(istack_t *s) {
    uint32_t old = atomic_load_explicit(&s->top, memory_order_acquire);
    while (old != NIL) {
        uint32_t next = atomic_load_explicit(&g_pool[old].next, memory_order_relaxed);
        POP_WINDOW();
        if (atomic_compare_exchange_weak_explicit(&s->top, &old, next,
                                                  memory_order_acquire, memory_order_acquire))
            return old;
    }
    return NIL;
}
```

| 연산 | order | 왜 |
|---|---|---|
| push: 처음 `top` 읽기 | relaxed | 읽은 값은 "비교값 후보"일 뿐이다. 틀리면 CAS가 실패하고 최신값으로 고쳐 준다. 이 값으로 다른 데이터를 읽지 않는다 |
| push: `node.next = old` | relaxed (atomic) | 순서는 다음 줄의 release CAS가 보장한다. atomic이어야 하는 이유는 낡은 top을 든 다른 pop이 **동시에** 이 칸을 읽을 수 있기 때문이다(아래). |
| push: CAS 성공 | **release** | 노드의 `next`와 `value` 쓰기를 top과 함께 **게시**한다 |
| push: CAS 실패 | relaxed | 새 `old`로 next만 다시 쓴다. 노드 내용을 읽지 않는다 |
| pop: `top` 읽기 | **acquire** | push의 release와 짝을 이룬다. 이 뒤의 `old->next` 읽기와 (pop 성공 후) `value` 접근이 push 전의 쓰기를 본다 |
| pop: `old->next` 읽기 | relaxed | acquire 뒤에 있으니 이미 순서가 잡혀 있다 |
| pop: CAS 성공 / 실패 | **acquire / acquire** | 실패하면 새 `old`를 가져오고 그 노드의 next를 읽어야 한다 → acquire가 필요하다. 성공 쪽도 acquire로 둔다. 이 맥에서 `casa`이고, release 성분(`casal`)은 필요 없다 |

**pop의 CAS에 release가 왜 필요 없나?** pop은 아무것도 게시하지 않는다. 그래도 사슬은 끊기지 않는다. C11 5.1.2.4p10의 **release sequence** 때문이다. release 쓰기 하나와 그 뒤에 같은 변수에 이어지는 **RMW 전부**가 하나의 release sequence를 이룬다. 그래서 X를 push(release)한 뒤 다른 스레드들의 pop이나 push(모두 CAS = RMW)가 끼어들어도, 나중에 acquire로 top을 읽은 스레드는 X를 push한 스레드와 synchronizes-with 관계가 된다(MCPP-M13에서 다룬 규칙이다). 실측으로도 확인했다. push의 CAS를 relaxed로 낮추는 것 하나만으로 TSan이 `value++`에서 레이스를 잡았고, 진짜 실행에서는 **리스트 구조까지** 깨졌다(Part B 마지막).

**`next`가 왜 atomic이어야 하나?** ABA와 같은 원인이다. T1이 `old = A`를 읽은 직후 T2가 A를 pop했다가 다시 push하면서 `A.next`를 쓴다. 이때 T1은 `A.next`를 읽는다. 두 접근이 동시에 일어나니 일반 변수라면 **데이터 레이스 = UB**다(C11 5.1.2.4p25). 태그 버전에서도 마찬가지다. 태그는 "낡은 값을 쓴 CAS가 실패하게" 만들 뿐이고, **낡은 값을 읽는 것 자체**는 막지 못한다. relaxed atomic이면 이 읽기는 레이스가 아니다. 값이 낡았어도 CAS가 실패하니 쓰이지도 않는다.

**노드 메모리는 살아 있어야 한다.** 이 레슨의 모든 노드는 풀에서 나오고 풀로 돌아간다. 이런 메모리를 type-stable memory라고 한다. 만약 pop한 노드를 `free()`해서 OS나 할당기에 돌려주면, 낡은 `old->next`를 읽는 일이 **use-after-free**가 된다. 태그는 이걸 고치지 못한다. 그건 메모리 회수(reclamation) 문제이고 hazard pointer(see:LLAT-M03)나 epoch/RCU(see:OS-A03)의 일이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">top 워드 두 가지와 happens-before 사슬</text>
  <rect x="20" y="36" width="160" height="30" class="f-acc" stroke="currentColor"/>
  <rect x="180" y="36" width="160" height="30" class="f-mut" stroke="currentColor"/>
  <text x="34" y="56" fill="currentColor" font-family="monospace">tag (63..32)</text>
  <text x="194" y="56" fill="currentColor" font-family="monospace">index (31..0)</text>
  <text x="350" y="56" fill="currentColor">64비트 CAS 1번 (casl / casa)</text>
  <rect x="20" y="76" width="160" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="180" y="76" width="160" height="30" class="f-acc" stroke="currentColor"/>
  <text x="34" y="96" fill="currentColor" font-family="monospace">pkt_t *ptr</text>
  <text x="194" y="96" fill="currentColor" font-family="monospace">uintptr_t tag</text>
  <text x="350" y="96" fill="currentColor">16바이트 CAS (caspa/caspl), 16 B 정렬, 패딩 없음</text>
  <text x="20" y="140" fill="currentColor">push 스레드</text>
  <text x="20" y="240" fill="currentColor">pop 스레드</text>
  <rect x="110" y="122" width="150" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="118" y="142" fill="currentColor" font-family="monospace">value = v; next=old</text>
  <rect x="270" y="122" width="180" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="278" y="142" fill="currentColor" font-family="monospace">CAS top (release)</text>
  <rect x="290" y="222" width="180" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="298" y="242" fill="currentColor" font-family="monospace">load top (acquire)</text>
  <rect x="480" y="222" width="250" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="488" y="242" fill="currentColor" font-family="monospace">next 읽기 · CAS · value 사용</text>
  <path d="M360 152 L375 222" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M367 213 L375 222 L378 210" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="390" y="180" fill="currentColor">synchronizes-with</text>
  <text x="390" y="198" fill="currentColor">(사이에 낀 CAS 들 = release sequence)</text>
  <text x="20" y="284" fill="currentColor">이 화살표가 없으면(push relaxed) pop 이 옛 next 를 top 에 넣는다 — 실측: 사이클.</text>
</svg>
```

### Part B — ABA를 강제로 재현하기

solution.c의 실제 출력(검사기가 찍는 상태 줄)이다.

```text
== Part B: forced ABA on the naive stack
  [before] naive idx top -> A -> B -> C -> D -> NIL   (walked 4, both 0, lost 0, cycle no)
  [after] naive idx top -> B -> C -> D -> NIL   (walked 3, both 1, lost 0, cycle no)
PASS r.paused && r.m_a == 0u && r.m_b == 1u
PASS r.t1_got == 0u && r.hits == 1
PASS w_is_top(&is) == 1u && ir.both == 1u && ir.lost == 0u
  [B freed] naive idx top -> B -> B -> B -> B -> B -> B -> ...   (walked 1, both 0, lost 2, cycle YES)
PASS ir.cycle && ir.lost == 2u && p1 == 1u && p2 == 1u
```

| 단계 | 누가 | 동작 | top 사슬 | 소유 |
|---|---|---|---|---|
| 0 | — | 준비 | A→B→C→D | — |
| 1 | T1 | `old=A`, `next=B` 읽고 window에서 멈춤 | A→B→C→D | — |
| 2 | T2 | pop → A | B→C→D | T2: A |
| 3 | T2 | pop → B | C→D | T2: A, B |
| 4 | T2 | push A (`A.next = C`) | A→C→D | T2: B |
| 5 | T1 | CAS(top: A → B) — top이 A이니 **성공** | **B**→C→D | T1: A, T2: B |
| 6 | T2 | B 반납(push B): `B.next = top = B` | **B→B→B…** | T1: A |
| 7 | 아무나 | pop, pop | 둘 다 B를 받는다 | B를 두 명이 가진다 |

이 결과는 결정적이다. 매번 같다. 5단계에서 **B가 두 곳에 있다**(`both 1`). 6단계에서 **자기 자신을 가리키는 사이클**이 생기고, C와 D는 어디에서도 닿지 않는 **분실 노드**가 된다(`lost 2`). 펌웨어로 옮기면 같은 패킷 버퍼를 두 드라이버가 동시에 채우는 상황이고, 풀은 점점 줄어든다. 이런 버그는 보통 "가끔 패킷이 섞인다" 같은 증상으로만 나타난다.

또 하나 볼 점이 있다. T1이 window에서 **멈춰 있는 동안에도 T2는 연산 3개를 끝냈다**. mutex 스택이었다면 락을 쥔 채 선점된 T1 때문에 T2도 멈췄을 것이다. 이것이 lock-free의 뜻이다(Q&A Q4).

**진짜 경합에서도 깨지나?** 강제 스케줄 없이 `demo_aba_stress.c`로 8스레드를 돌렸다(64노드, 스레드당 20만 회, 위 스트레스와 같은 루프). load average 14.5에서 실측한 결과다.

```text
MODE=0 (naive idx), POOL=64, 8 threads x 200000 iters, 5 rounds
round  0: pops 3200000 sum 2015836 dup 595969 empty 0 | stack walked 1/64 cycle YES <-- CORRUPT
round  1: pops 3200000 sum 2019284 dup 609671 empty 0 | stack walked 1/64 cycle YES <-- CORRUPT
...
corrupted rounds: 5 / 5
MODE=0 ... 8 threads x 2000 iters  → corrupted rounds: 5 / 5   (라운드당 동시 소유 1571–4851 회)
MODE=1 (tag64), POOL=64, 8 threads x 200000 iters → corrupted rounds: 0 / 20  (POOL=16 에서도 0 / 20)
```

naive 스택은 **매 라운드, 스레드당 2000회만 돌려도** 깨졌다. 끝 상태는 언제나 노드 하나짜리 자기 사이클(`walked 1/64 cycle YES`)이었다. 풀에서 노드를 바로 재사용하는 LIFO 구조라서, 방금 뗀 노드가 곧바로 다시 맨 위에 오는 일이 아주 흔하다. "ABA는 이론상 버그"가 아니다. 이 구조에서는 가장 먼저 터지는 버그다.

<!-- code: demo_aba_stress.c -->

### 수정 1 — 64비트 (태그, 인덱스)

```c
static uint64_t tag_pack(uint32_t idx, uint32_t tag) { return ((uint64_t)tag << 32) | idx; }
...
nw = tag_pack(next, tag_tag(old) + 1u);      /* pop. push 는 tag_pack(i, tag_tag(old) + 1u) */
```

같은 스케줄의 출력은 `[after] tag64 top -> C -> D -> NIL (both 0, lost 0)`, `hits == 2`다. 5단계에서 top은 `(A, t+3)`인데 T1은 `(A, t)`를 들고 있다. 그래서 CAS가 실패하고 `old`가 `(A, t+3)`으로 고쳐진다. T1은 `next = C`를 **다시 읽고** 재시도해서 성공한다. 고친 내용은 "비교하는 값에 이력을 넣었다"는 것 하나다.

**태그를 push에서도 올려야 하나?** 이 스케줄만 보면 pop에서만 올려도 막힌다. A가 다시 맨 위에 오려면 그 전에 A가 pop되어야 하기 때문이다. 하지만 "모든 성공 CAS에서 +1"은 따로 증명할 필요가 없는 규칙이고, 비용 차이도 없다(어차피 CAS 한 번). 그래서 이 레슨은 이 규칙을 쓴다.

**태그가 감기면?** 태그가 b비트면, T1이 멈춘 사이 성공한 CAS가 **정확히 2^b의 배수 번** 일어나고 top이 **같은 인덱스**일 때 ABA가 돌아온다. 이 맥의 실측 단일 스레드 처리량 ≈ 100–125 M쌍/s(pop+push, 성공 CAS 2번)로 계산하면 다음과 같다.

| 태그 | 감기는 데 필요한 CAS | 이 맥 최대 속도(≈2.0–2.5×10⁸ CAS/s)로 | 1 MHz ISR 속도로 | 현실성 |
|---|---|---|---|---|
| 16비트 (32비트 워드, Cortex-M3/M4) | 65 536 | **≈ 0.3 ms** | 66 ms | **현실적** — 연습 1에서 강제로 재현 |
| 32비트 (이 레슨 Part B) | 4.3×10⁹ | ≈ 17–21 s | 72 분 | 드묾. 선점된 스레드가 20 초 넘게 멈춰야 하고, 그동안 다른 스레드가 쉬지 않고 돌아야 한다 |
| 48비트 (인덱스 16 + 태그 48) | 2.8×10¹⁴ | ≈ 13 일 | 9 년 | 실질적으로 안전 — 노드가 6만 개 이하면 이 분할이 더 낫다 |
| 64비트 (Part C 128비트) | 1.8×10¹⁹ | ≈ 2 300 년 | — | 안전 |

설계 팁: 풀이 64칸인데 인덱스에 32비트를 줄 이유는 없다. **인덱스 비트는 최소로, 나머지는 태그에** 주는 것이 64비트 CAS 한 번으로 얻을 수 있는 최선이다.

**순서까지 틀리면?** `demo_aba_stress.c MODE=2`는 태그는 그대로 두고 push CAS만 relaxed로 바꾼 것이다(asm이 `casl` → `cas`로 바뀐다). 진짜 실행에서 **5/5 라운드가 깨졌다**(`dup ≈ 10만`, 사이클). 태그로 ABA를 막아도 소용이 없었다. 추정되는 원인은 `next` 저장이 CAS보다 늦게 보이는 것이다. 다른 코어가 새 top의 **옛 next**를 읽어 top에 쓰레기를 넣는다. TSan은 같은 버그를 **깨지지 않은 실행에서도** 잡는다(trim).

```text
WARNING: ThreadSanitizer: data race (pid=4017)
  Write of size 4 at 0x0001022bc1fc by thread T2:
    #0 worker demo_aba_stress.c:88 (dast2:arm64+0x100000b0c)
  Previous write of size 4 at 0x0001022bc1fc by thread T1:
    #0 worker demo_aba_stress.c:88 (dast2:arm64+0x100000b0c)
  Location is global 'g_pool' at 0x0001022bc000 (dast2+0x1000081fc)
SUMMARY: ThreadSanitizer: data race demo_aba_stress.c:88 in worker
round  0: pops 320000 sum 320000 dup 0 empty 0 | stack walked 64/64 cycle no
```

88행은 `g_pool[h[j]].value++`다. 반대로 naive(MODE=0)에서도 TSan은 같은 줄을 보고했다. 이건 ABA 자체가 아니라 그 **결과**(두 스레드가 같은 노드를 가짐)를 잡은 것이다. TSan은 원자 연산의 값 비교 논리를 모른다. ABA는 무결성 검사기나 소유 표시로 잡아야 한다.

### Part C — 128비트 {포인터, 태그} CAS: 무엇으로 컴파일되나

`demo_asm.c`의 `dw_pop`과 solution.c를 각 타깃으로 `-S`한 결과다(전부 실제 출력에서 뽑았다).

<!-- code: demo_asm.c -->

| 타깃 (명령) | 16바이트 CAS | 16바이트 acquire load | `atomic_is_lock_free` |
|---|---|---|---|
| arm64 macOS 기본 (`cc -O2 -S`) | pop(acquire) `caspa x2, x3, x4, x5, [x8]`, push(release) `caspl`, acq_rel이면 `caspal` | `ldp x2, x9, [x8]` + `dmb ishld` | 1 (실행해서 확인) |
| ARMv8.0 (`--target=aarch64-linux-gnu -mcpu=cortex-a53`) | `ldaxp` / cmp / `stlxp` 루프 | **`ldaxp` + `stxp` 루프** — 읽기인데 쓰기를 한다 | — |
| ARMv8.1 (`-march=armv8.1-a`) | `caspal` | `caspa x0, x1, x0, x1` — 역시 쓰기 | — |
| ARMv8.4 (`-march=armv8.4-a`, LSE2) | `caspal` | `ldp` + `dmb ishld` (LSE2부터 정렬된 `ldp`가 단일 복사 원자적) | — |
| x86-64 기본 | `callq __atomic_compare_exchange_16@PLT` (+ `-Watomic-alignment` 경고) | `callq __atomic_load_16@PLT` | — |
| x86-64 `-mcx16` | `lock cmpxchg16b` | `lock cmpxchg16b` (load도 CAS) | — |
| Cortex-M4 (thumbv7em) | 구조체가 8바이트 → `bl __atomic_compare_exchange` (일반 libcall) | `bl __atomic_load_8` | lock-free 아님 |

포인트 세 가지:
1. **이 맥에서는 16바이트 CAS가 진짜 하드웨어 명령이다.** 그래서 Part C의 `CHECK(atomic_is_lock_free(&ps.top))`가 통과한다. x86에서 `-mcx16` 없이 빌드하면 libatomic 호출이 된다. libatomic 구현에 따라 내부 락을 쓸 수 있으니 ISR이나 시그널 핸들러에서는 위험하다(구현 세부는 미검증).
2. **LSE2 이전에는 16바이트 "읽기"가 쓰기다**(`ldaxp/stxp`, `caspa`). 읽기 전용 페이지에 두면 폴트가 나고, 읽기만 하는 스레드도 캐시 라인을 독점으로 가져간다.
3. CAS는 **바이트 단위로 비교**한다. 구조체에 패딩이 있으면 값이 같아도 CAS가 실패할 수 있다. 그래서 `_Static_assert(sizeof(tagptr_t) == 16)`와 `_Alignof(_Atomic tagptr_t) == 16`을 둔다.

패킷 풀 스트레스(실제 출력): `pool: produced 200000, consumed 200000, exactly-once 200000, bad 0, dup-owner 0, abort 0`. ready 스택은 LIFO라 **도착 순서는 보장하지 않는다**. 순서가 필요하면 MPMC 큐(see:DS-A03)를 쓴다. 풀(free list)은 순서가 상관없으니 스택이 맞다. 오히려 LIFO가 **캐시에 따뜻한 버퍼**를 먼저 돌려준다는 장점이 있다.

### 벤치마크: lock-free 스택 vs 락 스택

`demo_bench.c`: 스레드마다 pop+push 쌍을 200 ms 동안 돌리고, 3번 측정해 중앙값을 낸다(단위 Mpairs/s). 기계: `Apple M6`, perflevel 2/4/6 (Super 2 + Performance 4 + Efficiency 6), `Apple clang version 21.0.0`. **다른 레슨의 벤치마크가 동시에 돌았다** → 비율만 보자.

<!-- code: demo_bench.c -->

LSE 빌드 (`cc -O2`, CAS = `casa/casl`, 16바이트는 `caspa/caspl`), load average 9.10 → 9.64:

| impl | 1 thr | 2 thr | 4 thr | 8 thr |
|---|---|---|---|---|
| tag64 | 119.3 | 16.7 | 8.1 | 3.9 |
| tag64 + backoff | 118.8 | 11.8 | 7.6 | 3.6 |
| dw128 | 140.3 | 15.4 | 13.2 | 9.0 |
| pthread_mutex | 103.9 | 42.8 | 27.9 | 29.7 |
| os_unfair_lock | 166.5 | 19.5 | 16.8 | 24.1 |

LL/SC 빌드 (`-mcpu=apple-m1+nolse`, CAS = `ldaxr`·`ldxr`/`stlxr`·`stxr`, 16바이트는 `ldaxp`·`ldxp`/`stlxp`·`stxp`), load average 9.64 → 9.15:

| impl | 1 thr | 2 thr | 4 thr | 8 thr |
|---|---|---|---|---|
| tag64 | 98.5 | 13.0 | 28.5 | 7.0 |
| tag64 + backoff | 100.4 | 13.7 | 27.4 | 6.9 |
| dw128 | 145.9 | 40.7 | 46.8 | 22.4 |
| pthread_mutex | 127.9 | 71.9 | 33.8 | 22.5 |
| os_unfair_lock | 167.7 | 19.7 | 16.8 | 24.1 |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">8 스레드 처리량 (Mpairs/s, 실측 중앙값) — 길수록 빠름</text>
  <g fill="currentColor">
    <text x="10" y="58">tag64</text><text x="10" y="102">tag64+backoff</text><text x="10" y="146">dw128</text>
    <text x="10" y="190">pthread_mutex</text><text x="10" y="234">os_unfair_lock</text>
  </g>
  <rect x="130" y="42" width="58" height="14" class="f-acc" stroke="currentColor"/>
  <rect x="130" y="58" width="105" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="130" y="86" width="54" height="14" class="f-acc" stroke="currentColor"/>
  <rect x="130" y="102" width="104" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="130" y="130" width="135" height="14" class="f-acc" stroke="currentColor"/>
  <rect x="130" y="146" width="336" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="130" y="174" width="446" height="14" class="f-acc" stroke="currentColor"/>
  <rect x="130" y="190" width="338" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="130" y="218" width="362" height="14" class="f-acc" stroke="currentColor"/>
  <rect x="130" y="234" width="362" height="14" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="194" y="54">3.9</text><text x="241" y="70">7.0</text>
    <text x="190" y="98">3.6</text><text x="240" y="114">6.9</text>
    <text x="271" y="142">9.0</text><text x="472" y="158">22.4</text>
    <text x="582" y="186">29.7</text><text x="474" y="202">22.5</text>
    <text x="498" y="230">24.1</text><text x="498" y="246">24.1</text>
  </g>
  <rect x="130" y="266" width="14" height="12" class="f-acc" stroke="currentColor"/>
  <text x="150" y="277" fill="currentColor">LSE 빌드 (기본)</text>
  <rect x="290" y="266" width="14" height="12" class="f-mut" stroke="currentColor"/>
  <text x="310" y="277" fill="currentColor">LL/SC 빌드 (-mcpu=apple-m1+nolse)   1 칸 = 15 px/Mpairs</text>
</svg>
```

읽는 법:
1. **경합이 있으면 lock-free 스택이 락보다 느리다.** 8스레드에서 tag64(LSE)는 3.9인데 pthread_mutex는 29.7이다(약 8배 차이). Treiber 스택은 모든 연산이 **같은 캐시 라인 하나**(top)에 CAS를 한다. 스레드가 늘수록 CAS가 실패하며 라인이 코어 사이를 오가고, 실패할 때마다 다시 시도한다. 락은 기다리는 스레드를 재우므로 동시에 라인을 다투는 코어 수가 적다. 실제로는 락을 쥔 한 코어가 연속으로 여러 번 처리하게 된다(이게 macOS mutex 정책 때문인지는 미검증).
2. **LSE `cas*`보다 LL/SC가 경합에 강했다.** dw128은 9.0 → 22.4, tag64는 3.9 → 7.0이다. ARM-M04에서 측정한 "`casal` CAS 루프는 LL/SC보다 2–4배 느리다"와 같은 방향이다. 원인 추정(비교가 실패해도 `cas`는 완전한 원자 트랜잭션이다)은 ARM-M04를 참고하고, 여기서 다시 재지 않았다.
3. 단순한 지수 backoff(`yield` 1→64번)는 거의 효과가 없었다. 경합 스택의 고전적인 해법은 elimination backoff(Hendler·Shavit·Yerushalmi 2004)다. push와 pop이 top에 가지 않고 옆 배열에서 직접 짝을 맞춘다. 이 레슨에서는 측정하지 않았다.
4. 1 스레드(경합 없음)에서는 다섯 모두 비슷하다(약 100–170). 2와 4 스레드 칸은 P/E 코어 배치에 따라 크게 흔들렸다(코어 고정 불가). **결론: lock-free를 고르는 이유는 처리량이 아니라 진행 보장이다**(선점된 스레드가 남을 막지 않는다, ISR에서도 막히지 않는다).

### ABA 해결책 비교

| 방법 | 어떻게 막나 | 비용 / 한계 | 어디서 |
|---|---|---|---|
| 세대 태그 (64비트 packing) | 비교값에 이력을 넣는다 | 인덱스 기반이라야 한다. 태그 비트가 유한하니 감길 수 있다 | Part B, 연습 1 |
| 128비트 {포인터, 태그} (DWCAS) | 같은 아이디어, 포인터 그대로 사용 | CASP나 cmpxchg16b가 필요하다. 그 전 세대 CPU에서는 읽기도 쓰기다. 32비트 MCU에는 없다 | Part C |
| Hazard pointer | "내가 보고 있는 노드"를 공개한다. 그 노드는 재사용하거나 해제하지 않는다 | 읽을 때마다 store→load 순서(seq_cst)가 필요하다. 회수 지연은 제한된다 | see:LLAT-M03 |
| Epoch / RCU | 모든 reader가 지나간 뒤에야 재사용한다 | reader 하나가 멈추면 회수가 무한정 밀린다 | see:OS-A03 |
| LL/SC 직접 사용 | 값이 아니라 **쓰기 여부**로 실패한다 | asm 블록 안에서 짧게 써야 한다. 컴파일러 CAS로는 이 성질이 안 생긴다(아래) | `demo_llsc.c` |
| GC 언어 | 누가 참조하는 동안은 같은 주소가 재사용되지 않는다 | 런타임이 필요하다 | Java `ConcurrentLinkedQueue` 등 |
| 구조로 피하기 | pop하는 스레드를 하나로(MPSC), 또는 `pop_all` = exchange | API에 제약이 생긴다 | 연습 2, 3 (Linux `llist`) |

### LL/SC: 하드웨어는 ABA를 잡는데 C 코드는 왜 못 잡나

`demo_llsc.c`는 pop을 `ldaxr`(top) → `ldr`(next) → window → `stlxr`로 손으로 썼다(asm에서 `ldaxr w8,[x20]` / `ldr w23,[x19,w8,uxtw #2]` / `bl _window` / `stlxr w8,w23,[x20]` 순서를 확인했다). 3번 실행한 결과가 모두 같았다.

<!-- code: demo_llsc.c -->

```text
empty : STLXR failed    0 / 1000   (top value at STLXR time was A again: n/a)
aba   : STLXR failed 1000 / 1000   (top value at STLXR time was A again: yes)
same  : STLXR failed 1000 / 1000   (top value at STLXR time was A again: yes)
sleep : STLXR failed 1000 / 1000   (top value at STLXR time was A again: n/a)
```

`aba`에서는 STLXR 시점에 top 값이 **다시 A인데도** 1000/1000번 실패했다. `same`(다른 코어가 같은 값을 다시 쓰기만 함)도 실패했다. 배타 모니터(exclusive monitor)는 값이 아니라 **그 granule에 쓰기가 있었는지**를 보기 때문이다. `sleep`(아무도 안 쓰고 50 µs 잠듦)도 전부 실패했다. 문맥 전환이나 예외 복귀가 모니터를 지운 것으로 보인다(OS 내부는 미확인). 그런데 컴파일러가 만든 CAS는 LL/SC 기계에서도 ABA에 걸린다. `--target=aarch64-linux-gnu -mcpu=cortex-a53`으로 `naive_pop`을 컴파일한 결과다.

```text
	ldar	w11, [x8]              ; ① old = top  ← 비교값은 여기서 이미 읽었다 (모니터 밖)
	...
	ldr	w12, [x9, x0, lsl #2]  ; ② next = pool[old].next
	ldaxr	w11, [x8]              ; ③ CAS 의 LL: 지금 top 을 다시 읽는다
	cmp	x11, x0                ;    old 와 같으면
	b.ne	.LBB0_1
	stxr	w13, w12, [x8]         ; ④ SC 성공 — ③과 ④ 사이에는 아무도 안 썼다
```

①과 ③ 사이의 A→B→A는 모니터가 모른다. ③의 LL은 이미 "돌아온 A"를 다시 읽고 출발한다. CAS API는 "값이 같으면"이라는 의미를 갖고 있어서, 아래가 LL/SC여도 그 의미를 바꾸지 못한다. 하드웨어의 ABA 면역을 쓰려면 **LL로 top을 읽는 그 순간부터 SC까지**를 한 asm 블록 안에 넣어야 한다(`demo_asm.c`의 `llsc_pop_m4`, Cortex-M용 `__builtin_arm_ldrex/strex`). 단, Arm ARM은 LL과 SC 사이에 다른 메모리 접근이 있으면 진행을 보장하지 않는다(next 읽기가 그런 접근이다). 구현에 따라 계속 실패할 수 있다(Cortex-M3/M4 실리콘에서는 미검증).

### 펌웨어: Cortex-M의 lock-free 풀

`demo_asm.c`를 Cortex-M용으로 `-S`한 결과다.

| 함수 | Cortex-M4 (`thumbv7em`) | Cortex-M0+ (`thumbv6m`) |
|---|---|---|
| `naive_pop` / `tag16_pop` (32비트 CAS) | `ldrex` / `cmp` / `strex` 인라인, 앞뒤에 `dmb sy` | `bl __atomic_load_4`, `bl __atomic_compare_exchange_4` |
| `tag64_pop` (64비트) | `bl __atomic_load_8`, `bl __atomic_compare_exchange_8` | 같은 libcall |
| `llsc_pop_m4` (builtin LDREX/STREX) | `ldrex r0,[r1]` … `ldr.w r3,[r12,r0,lsl #2]` … `strex r2,r3,[r1]`, 성공 뒤 `dmb sy` | — (v6-M에는 exclusive 명령이 없다) |

- **M3/M4에는 64비트 CAS가 없다**(LDREXD/STREXD가 없음). 그래서 C11 `uint64_t` CAS는 libcall이 되고 lock-free가 아니다. 32비트 워드에 (태그 16 | 인덱스 16)을 넣으면 인라인 `ldrex/strex`가 되지만, 위 표처럼 태그가 1 MHz에서 66 ms 만에 감긴다(연습 1). 단일 코어 M4라면 `llsc_pop_m4`처럼 **LDREX/STREX를 직접** 쓰는 게 낫다. ISR이 끼어들면 예외 진입이나 복귀가 로컬 모니터를 지우므로 STREX가 실패하고 다시 시도한다. 태그가 필요 없다(문서 기준, 세부는 ARM-M11 참고).
- **단일 코어에서도 ABA는 생긴다.** 태스크가 `old=A, next=B`를 읽은 순간 UART ISR이 들어와 버퍼 A와 B를 꺼내고 A를 반납하면, 그림 1과 똑같은 일이 일어난다. C11 CAS로 짠 naive 스택이라면 ISR 하나와 태스크 하나만으로도 깨진다.
- **M0/M0+**: 원자 RMW가 전혀 없으니 `__atomic_*` libcall이 나온다. 제공되는 구현은 보통 인터럽트를 끄는 방식이다(툴체인마다 다름, see:CPP-A04). 이때는 lock-free 스택보다 **PRIMASK 임계 구역 몇 사이클**로 감싼 평범한 스택이 정직하다. FreeRTOS도 블록 풀이나 큐를 임계 구역으로 보호한다(대표 구현 기준, 미검증).
- DMA가 버퍼를 쓰는 경우, 원자 연산은 DMA와의 순서를 보장하지 않는다. 캐시가 있는 M7이면 반납 전에 D-cache clean/invalidate가 필요하다(see:C-M14).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution        # 20/20 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o s_asan && ./s_asan
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o s_tsan && ./s_tsan             # 보고 없음
```

검증한 결과: 일반 빌드 6회 연속 `20/20 passed`, ASan+UBSan과 TSan 모두 보고 없이 `20/20 passed`(exit 0)였다. 128비트 `_Atomic` 구조체 CAS를 쓰는 Part C도 TSan 빌드에서 보고 없이 통과했다(TSan이 16바이트 원자 연산을 어떻게 계측하는지는 확인하지 않았다). 바뀐 부분은 TODO 9개(`is_push/is_pop`, `tag_pack/idx/tag`, `ts_push/ts_pop`, `ps_push/ps_pop`)뿐이다.

## 7. 자주 하는 실수

1. **`POP_WINDOW()`를 top 읽기와 next 읽기 사이에 둔다.** 그러면 T2의 작업이 끝난 뒤 next를 읽게 되어 `next = C`가 나온다. ABA가 "재현되지 않는" 재현 코드가 된다. 창은 **next까지 읽은 뒤 CAS 전**이다.
2. **`next`를 일반 `uint32_t`로 둔다.** 낡은 top으로 next를 읽는 일과 그 노드의 새 push가 next를 쓰는 일이 동시에 일어나니 데이터 레이스(UB)다. 태그가 있어도 마찬가지다. `_Atomic uint32_t next` + relaxed로 고친다.
3. **push CAS를 relaxed로 둔다**("값만 바꾸니까"). 실측으로 5/5 라운드가 깨졌고 TSan은 레이스를 보고했다. 노드 내용을 게시하는 것이 push다. → `memory_order_release`.
4. **태그 증가가 인덱스로 샌다.** 예를 들어 `old + 1`처럼 워드 전체에 1을 더하면 인덱스가 바뀐다. 인덱스와 태그 경계에서 캐리를 신경 쓰지 않으면, 0xFFFFFFFF 태그에서 인덱스가 망가진다. → `tag_pack(idx, tag_tag(old) + 1u)`, 또는 `old + (1ull << 32)` (캐리가 64비트 밖으로 버려짐).
5. **pop한 노드를 `free()`한다.** 태그는 ABA를 막지만, 낡은 top을 든 다른 스레드가 `old->next`를 읽는 일은 막지 못한다. 그 메모리가 사라졌으면 use-after-free다. → 노드는 풀에 남긴다(type-stable). 아니면 hazard pointer나 EBR을 쓴다(LLAT-M03, OS-A03).
6. **16바이트 구조체에 패딩이나 정렬 문제가 있다.** `{uint32_t tag; void *p;}`는 4바이트 패딩이 생긴다. CAS는 바이트를 비교하니 같은 값이어도 실패할 수 있다. 32비트 타깃에서는 정렬이 4라서 경고와 함께 일반 libcall이 나왔다(위 M4 표). → `{ptr, uintptr_t}` 순서, `_Static_assert(sizeof == 16)`, `_Alignof == 16`.
7. **"x86에서 돌았으니 맞다."** x86은 store→store, load→load 순서를 하드웨어가 지켜 주니 relaxed push 버그(실수 3)가 숨는다. `-mcx16` 없이 빌드하면 128비트 CAS가 libcall이 된다. ARM에서, 그리고 TSan으로 검증한다.

## 8. 면접 Q&A

<details><summary>Q1. ABA 문제를 최소 시나리오로 설명하고, 어떻게 고치나요?</summary>

스택이 A→B→C다. T1이 pop을 하려고 `old=A, next=B`를 읽고 선점된다. 그 사이 T2가 pop A, pop B, push A를 한다(A→C). T1이 깨어나 `CAS(top, A, B)`를 하면 top이 여전히 A라서 성공한다. 그러면 top이 이미 T2의 것인 B를 가리키고, C는 잃는다. 고치는 법은 네 가지다. ① top에 세대 태그를 붙이고 모든 성공 CAS마다 +1 한다(64비트 packing 또는 128비트 DWCAS). ② hazard pointer로 "누가 보고 있는 노드는 재사용 금지". ③ epoch/RCU로 grace period 뒤에만 재사용. ④ 구조를 바꾼다(pop하는 스레드를 하나로, 또는 `pop_all` = exchange). 꼬리 질문 "push도 고쳐야 하나요?"의 답은 "아니요"다. push는 `new->next = old`를 쓰고 CAS하는데, 그 사이 A가 나갔다 돌아왔어도 새 노드가 지금의 A를 가리키니 올바르다(연습 2에서 강제 스케줄로 확인).
</details>

<details><summary>Q2. 32비트 태그는 충분한가요? 감기면 어떻게 되나요?</summary>

T1이 멈춘 동안 성공한 CAS가 정확히 2³²의 배수 번이고 top이 같은 인덱스로 돌아오면 ABA가 다시 생긴다. 이 맥에서 한 스레드가 쉬지 않고 돌리면 성공 CAS는 약 2–2.5억/s다. 그러면 약 17–21 초가 걸린다. 멈춘 스레드가 그렇게 오래 선점된 채로 있고, 그동안 다른 스레드가 내내 같은 스택을 두드려야 한다. 그래서 실제로는 거의 안 생긴다. 그러나 16비트 태그는 0.3 ms면 감긴다. 32비트 MCU에서 (16 | 16) packing을 쓴다면 진짜 위험이다. 개선책은 인덱스 비트를 줄이고 태그에 더 주는 것(16 + 48), 또는 128비트 DWCAS(64비트 태그 ≈ 2300년)다.
</details>

<details><summary>Q3. 왜 128비트 CAS가 필요하죠? 64비트 포인터에 태그를 넣으면 안 되나요?</summary>

넣을 수는 있다. AArch64 유저 주소는 보통 48비트 이하이고 노드가 16바이트 정렬이면 하위 4비트가 빈다. 그래서 상위 비트나 하위 비트에 태그를 숨기는 tagged pointer를 많이 쓴다. 단점이 있다. 태그가 작아서(16비트 안팎) 빨리 감긴다. 또 TBI, PAC, MTE처럼 상위 비트를 하드웨어가 쓰는 기능과 충돌할 수 있다(Apple arm64e는 PAC로 포인터 상위 비트를 쓴다). 그리고 이식성이 없다. 인덱스 기반 풀이면 이 레슨의 64비트 packing이 가장 깔끔하다. 진짜 포인터가 필요하면 `{ptr, tag}` 16바이트 + CASP나 cmpxchg16b를 쓴다. 비용도 확인해야 한다. 이 맥에서는 CASP(`caspa`/`caspl`)가 인라인으로 나오고 `atomic_is_lock_free == 1`이다. x86-64는 `-mcx16`이 있어야 `lock cmpxchg16b`, 없으면 `__atomic_compare_exchange_16` libcall이다. ARMv8.0에서는 16바이트 load조차 `ldaxp/stxp` 쓰기 루프다.
</details>

<details><summary>Q4. lock-free, wait-free, obstruction-free의 차이는? Treiber 스택은 어디에 속하나요?</summary>

- **wait-free**: **모든** 스레드가 자기 연산을 유한한(제한된) 단계 안에 끝낸다. 굶는 스레드가 없다.
- **lock-free**: 어떤 순간이든 **적어도 하나의** 스레드는 유한한 단계 안에 진행한다. 시스템 전체는 진행하지만 특정 스레드는 굶을 수 있다.
- **obstruction-free**: 다른 스레드가 모두 멈춘 상태라면 **혼자 도는** 스레드는 유한한 단계 안에 끝낸다.

포함 관계는 wait-free ⊂ lock-free ⊂ obstruction-free다. Treiber 스택은 **lock-free이지만 wait-free는 아니다**. 내 CAS가 실패했다는 것은 그 사이 다른 누군가의 CAS가 성공했다는 뜻이다. 그러니 시스템은 진행한다. 하지만 운이 나쁜 스레드는 계속 질 수 있다. Part B 스케줄이 이를 보여 준다. T1이 window에서 멈춰 있어도 T2는 연산 3개를 끝냈다. mutex라면 락을 쥔 채 선점된 스레드가 모두를 막는다. 참고로 Vyukov MPMC 큐는 엄밀히 lock-free가 아니다(see:DS-A03).
</details>

<details><summary>Q5. ARM의 LL/SC는 ABA에 면역이라던데, 그럼 ARM에서는 태그가 필요 없나요?</summary>

하드웨어 자체는 면역이다. `demo_llsc.c`에서 STLXR 시점에 top이 다시 A여도 1000/1000번 실패했다. 모니터는 "값"이 아니라 "쓰기"를 본다. 하지만 C11 CAS로 짠 코드는 면역이 아니다. 비교값 `old`를 CAS **이전의 일반 load**에서 읽었고, CAS 안의 LDAXR은 이미 돌아온 A를 다시 읽고 시작한다(a53 asm ①–④). 면역을 얻으려면 LL을 top 읽기 자체로 쓰고, next 읽기와 SC까지 한 asm 블록에 넣어야 한다. 그런데 LL과 SC 사이의 메모리 접근은 아키텍처상 진행 보장을 깨므로 이식성 있는 해법이 아니다. 게다가 이 맥의 기본 빌드는 CAS를 `casa`(LSE)로 내린다. LSE CAS는 값 비교 명령이라 처음부터 면역이 없다.
</details>

<details><summary>Q6. 이 lock-free 스택이 mutex 스택보다 빠른가요?</summary>

이 맥에서는 아니었다. 8스레드에서 tag64(LSE) 3.9 vs pthread_mutex 29.7 Mpairs/s였다(load ≈ 9, 비율만 볼 것). 하나의 top 라인에 모든 스레드가 CAS하므로 경합이 생기면 실패와 라인 이동이 폭증한다. LL/SC로 빌드하면 dw128이 22.4까지 올랐다. 즉 명령 선택도 결과를 2배 이상 바꾼다. 경합 환경에서 스택이 필요하면 elimination backoff, 스레드별 캐시 + 묶음 반납(jemalloc tcache나 커널 per-CPU 리스트 같은 방식), 샤딩 같은 방법을 쓴다. lock-free를 고르는 이유는 처리량이 아니다. 선점, 우선순위 역전, ISR 문맥에서도 **아무도 막히지 않는다는 진행 보장**이다.
</details>

<details><summary>Q7. pop한 노드를 바로 free()하면 안 되는 이유는? 태그로 해결되나요?</summary>

해결되지 않는다. T1이 `old=A`를 읽은 뒤 T2가 A를 pop해서 `free(A)`하면, T1의 `old->next` 읽기는 해제된 메모리를 읽는 것(use-after-free)이다. 할당기가 그 페이지를 OS에 돌려줬다면 세그폴트가 난다. 태그는 "읽은 값을 쓰는 CAS가 실패하게" 만들 뿐, **읽는 행위**를 막지 못한다. 해법은 두 가지다. 노드를 풀에 남기는 것(type-stable memory, 이 레슨), 또는 hazard pointer나 epoch로 "아무도 안 볼 때"까지 해제를 미루는 것이다(LLAT-M03, OS-A03).
</details>

<details><summary>Q8. Cortex-M에서 ISR과 태스크가 같이 쓰는 버퍼 풀을 만든다면?</summary>

먼저 단일 코어인지 확인한다. 단일 코어라면 ISR은 태스크를 선점할 뿐 동시에 돌지 않는다. M3/M4/M7이면 LDREX/STREX를 직접 쓴 pop이 가장 깔끔하다. 예외 진입이나 복귀가 모니터를 지우므로(문서 기준, ARM-M11) ISR이 끼면 태스크의 STREX가 실패하고, 태그가 없어도 ABA가 없다. C11 CAS를 쓴다면 32비트 (16 | 16) packing이 인라인 `ldrex/strex`로 나오지만 태그가 빨리 감긴다는 점을 문서화해야 한다. 64비트 CAS는 libcall이라 피한다. M0/M0+는 원자 RMW가 없으니 `__disable_irq()` 몇 사이클짜리 임계 구역이 정답이다. ISR 하나만 alloc하고 태스크만 free하는 식으로 구조를 제한할 수 있다면 SPSC 링(see:DS-A01)으로 바꾸는 것도 좋다. 듀얼 코어(RP2040, STM32H7 M7+M4)라면 하드웨어 세마포어나 스핀락 블록을 쓰는 방법도 있다(see:C-A03).
</details>

## 9. 추가 연습

### 연습 1 — 16비트 태그는 정확히 몇 번 만에 다시 속는가

32비트 워드에 (태그 16 | 인덱스 16)을 넣은 `t16_push/t16_pop`을 만든다(Cortex-M3/M4의 인라인 `ldrex/strex` 형태). 강제 스케줄에서 T1이 멈춘 사이 main이 "pop A, pop B, pop C, push A" 4번 + "(pop A, push A)" × k쌍을 한다. 어떤 k에서 T1의 CAS가 다시 성공하나?

<details><summary>답</summary>

성공 CAS 총수는 4 + 2k다. 이 값이 65 536의 배수가 되면 태그가 원래 값으로 돌아온다. 그러므로 k = 32 766이다. 이때 T1의 CAS가 첫 시도에 성공하고(hits == 1) B와 C가 스택에도 main 손에도 있다(both == 2). k = 32 765면 총 65 534번이라 실패하고 재시도한다. 짝수가 필요해서 main이 노드를 **2개** 들고 있게 짰다. 스택에서 노드를 1개만 순감시키면 총수가 항상 홀수(2·push + 1)라서 이 방식으로는 감기지 않는다. 실제 출력은 `wrap: hits 1, t1 got 0, both 2, walked 3`이다.
</details>

### 연습 2 — ABA가 "안" 생기는 두 경우

(a) T1이 E를 **push**하다가(next = A를 쓴 뒤, CAS 전) 멈추고, main이 pop A, pop B, push A를 한다. (b) **유일한 소비자** T1이 pop하다 멈추고, main은 push만 한다. naive 스택(태그 없음)으로 둘 다 무결성이 유지됨을 보인다.

<details><summary>답</summary>

(a) T1의 CAS(A → E)는 성공하고 결과는 E→A→C→D다. 새 노드는 **현재의** A를 가리키니 올바르다. push의 CAS는 "내가 적어 둔 next가 아직 top인가"만 확인하면 되고, A의 내용이 바뀌었어도 상관없다. (b) top이 A로 돌아오려면 누군가 A를 pop해야 하는데, pop하는 스레드는 T1뿐이다. main의 push로 top이 F가 되면 T1의 CAS는 그냥 실패하고(hits == 2), 재시도해서 F를 가져간다. 그래서 **MPSC 스택(다중 생산자, 단일 소비자)은 태그 없이도 안전하다**. Linux `llist`의 규칙("`llist_del_first`는 소비자 하나만")이 이 사실을 이용한다.
</details>

### 연습 3 — `pop_all` (atomic_exchange) + 뒤집어 FIFO

생산자 4개가 free 풀(tag64)에서 노드를 꺼내 `(pid << 24 | k)`를 쓰고 inbox에 push한다. 소비자 하나는 `atomic_exchange(&inbox, NIL)`로 사슬 전체를 가져와 뒤집은 다음 처리하고 free에 반납한다. 생산자마다 k가 0, 1, 2, … 순서로 빠짐없이 와야 한다.

<details><summary>답</summary>

exchange는 비교값이 없다. 그러니 "그 사이 A→B→A"를 신경 쓸 이유가 없고 ABA가 원천적으로 없다. 가져온 사슬은 소비자 혼자 가지므로 relaxed로 뒤집어도 된다. 한 생산자의 push k와 k+1은 같은 스레드에서 순서대로 일어나므로 inbox 수정 순서(modification order)에서도 그 순서다. 그래서 한 묶음 안에서 k+1이 top 쪽에 있고, 뒤집으면 k → k+1이 된다. 순서는 묶음 사이에서도 유지된다. exchange의 acquire는 그 사슬을 만든 모든 push(release, RMW 사슬)와 동기화한다. 실제 출력은 `got 200000 in 6769 batches, order_bad 0`이다. Linux 커널의 `llist_add` + `llist_del_all` + `llist_reverse_order`가 정확히 이 구조다(irq_work, 지연 해제 등에서 사용).
</details>

### 연습 4 — 세대 핸들 (낡은 핸들 감지)

8칸 풀에서 `handle = (gen16 << 16) | idx`를 나눠 준다. free할 때 세대를 +1 하고, `h_valid(h)`는 세대가 다르거나 슬롯이 비었으면 false를 준다. 이중 해제도 거절한다.

<details><summary>답</summary>

ABA 태그를 **사용자에게 주는 핸들**에 적용한 것이다. 슬롯 0을 받은 핸들 `0x00000000`을 free하면 세대가 1이 된다. 같은 슬롯이 다시 나가면 핸들은 `0x00010000`이다. 이제 옛 핸들로 접근하면 거절된다(use-after-free 감지). 게임 엔진의 엔티티 핸들, 드라이버의 요청 ID, NVMe 명령 ID 재사용 검사 같은 곳에서 쓰는 패턴이다. 세대는 16비트라 65 536번 재사용하면 감긴다는 점도 같다. 할당은 비트마스크 CAS(가장 낮은 1비트 지우기)로 했다. 이 비트마스크는 값 하나에 모든 상태가 들어 있어서 ABA가 문제 되지 않는다. 같은 값이면 같은 상태다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice        # 12/12 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o p_asan && ./p_asan
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o p_tsan && ./p_tsan
```

## 10. 핵심 정리

- Treiber push = `next=top; CAS(top, old → n)`이고 성공은 **release**다. pop = `old=top`(**acquire**); `next=old->next`; `CAS(top, old → next)`(acquire). 노드의 `next`는 **relaxed atomic**이다(낡은 reader가 동시에 읽으므로).
- ABA: pop이 `top=A, next=B`를 읽은 뒤 A가 나갔다 돌아오면 값 비교 CAS가 성공한다. 그 결과 노드가 두 곳에 있게 되고, 사이클이 생기고, 노드를 잃는다. 풀 재사용(LIFO) 구조에서는 **진짜 경합에서 5/5 라운드 재현**됐다.
- 수정은 비교값에 이력을 넣는 것이다. (태그 | 인덱스) 64비트, {포인터, 태그} 128비트(이 맥은 `caspa`/`caspl`, ARMv8.0은 `ldaxp/stlxp`, x86은 `cmpxchg16b` 또는 libcall).
- 태그 b비트는 2^b번이면 감긴다. 32비트 ≈ 20 s(드묾), 16비트 ≈ 0.3 ms(위험). 인덱스는 최소 비트로, 나머지를 태그에 준다.
- push의 ABA는 무해하다. 소비자가 하나(MPSC)이거나 `pop_all`(exchange)이면 태그가 필요 없다.
- 태그는 ABA만 고친다. **메모리 회수**(free)는 hazard pointer(LLAT-M03)나 epoch/RCU(OS-A03)가 맡고, 이 레슨은 type-stable 풀로 피했다.
- LL/SC 하드웨어는 쓰기를 감지해서 ABA에 면역이다. 하지만 C11 CAS는 비교값을 모니터 밖에서 읽으므로 면역이 아니다. Cortex-M 단일 코어에서는 LDREX/STREX를 직접 쓰거나, M0이면 PRIMASK를 쓴다.
- lock-free는 처리량 보장이 아니라 진행 보장이다(lock-free ⊂ obstruction-free, wait-free ⊂ lock-free). 이 맥 8스레드에서는 mutex가 약 8배 빨랐다.

## 참고자료

- R. K. Treiber, "Systems Programming: Coping with Parallelism", IBM Research Report RJ 5118, 1986 — Treiber 스택 원전
- IBM System/370 Principles of Operation, 부록 A의 CS/CDS 사용 예 — CDS(Compare Double and Swap)로 카운터를 붙여 free-pool의 ABA를 막는 예(태그 아이디어의 기원으로 자주 인용됨, 절 이름·판 미확인)
- M. M. Michael, "Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects", IEEE TPDS 15(6), 2004 — see:LLAT-M03
- D. Hendler, N. Shavit, L. Yerushalmi, "A Scalable Lock-free Stack Algorithm", SPAA 2004 — elimination backoff
- D. Dechev, P. Pirkelbauer, B. Stroustrup, "Understanding and Effectively Preventing the ABA Problem in Descriptor-based Lock-free Designs", ISORC 2010
- M. Herlihy, N. Shavit, *The Art of Multiprocessor Programming* — 3장(진행 조건), 7장(스핀락과 경합), 11장(동시 스택과 elimination) (1판 기준 장 번호)
- ISO/IEC 9899:2011 §5.1.2.4 (데이터 레이스 p25, release sequence p10), §7.17.7.4 compare_exchange, §6.2.5p9 unsigned 감김
- Arm Architecture Reference Manual for A-profile (DDI 0487) — LDAXP/STLXP, CASP (FEAT_LSE), FEAT_LSE2 단일 복사 원자성; ARMv7-M ARM (DDI 0403) — LDREX/STREX, 로컬 모니터 (see:ARM-M04, ARM-M11)
- Linux `include/linux/llist.h` — lock-less list: 다중 add + del_all, del_first는 소비자 하나 규칙
- 관련 레슨: C-A03 (CAS, release/acquire), MCPP-M13 (release sequence), ARM-M04 (LL/SC vs LSE 실측), DS-A01 (SPSC), DS-A03 (Vyukov MPMC), LLAT-M03 (hazard pointer), OS-A03 (RCU/epoch), CPP-A04 (Cortex-M lock-free 여부)
