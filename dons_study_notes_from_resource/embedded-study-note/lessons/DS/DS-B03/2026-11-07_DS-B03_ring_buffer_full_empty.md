---
id: DS-B03
time: 45분
updated: 2026-11-07
---
# DS-B03 링 버퍼 기초: Full/Empty 판별 3가지 방식

## 학습 목표

- 링 버퍼(ring buffer, 원형 큐)에서 `head == tail` 하나로는 **가득 참과 비어 있음을 구별할 수 없다**는 근본 문제를 설명한다.
- **count 변수**, **한 칸 비우기(one-slot-empty)**, **미러 비트(mirror bit)** 세 방식을 malloc 없이 구현한다.
- 세 방식의 트레이드오프(쓸 수 있는 칸 수, 추가 상태, ISR과 main 사이 경쟁)를 표로 비교하고 상황에 맞게 고른다.
- 무작위 push/pop 열을 **참조 모델(reference model)** 과 대조하는 랜덤 테스트(differential testing)를 직접 짜고, 그 테스트가 버그를 실제로 잡는지 확인한다.

## 1. 문제

UART 수신 인터럽트(ISR)가 바이트를 넣고 main 루프가 꺼내는 **링 버퍼**는 펌웨어에서 가장 많이 쓰는 자료구조이고, 면접에서 "링 버퍼 짜보세요" 다음에 반드시 나오는 꼬리질문이 **"꽉 찬 것과 빈 것을 어떻게 구별하나요?"** 다.

링 버퍼는 고정 크기 배열 `buf[N]` 과 인덱스 두 개로 만든다.

- `head` — 다음에 **쓸** 칸. 생산자(producer, UART RX ISR)가 전진시킨다.
- `tail` — 다음에 **읽을** 칸. 소비자(consumer, main)가 전진시킨다.
- 끝(N-1)을 넘으면 0으로 돌아간다(wrap). 그래서 "원형".

문제는 비어 있을 때도, 가득 찼을 때도 **head 와 tail 이 같은 칸**을 가리킨다는 것이다.

```svg
<svg viewBox="0 0 720 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">N = 5 링 버퍼: 두 상태 모두 head == tail == 2</text>
  <text x="10" y="72" fill="currentColor">비었음</text>
  <g stroke="currentColor" fill="none">
    <rect x="90" y="50" width="50" height="34"/><rect x="140" y="50" width="50" height="34"/><rect x="190" y="50" width="50" height="34"/>
    <rect x="240" y="50" width="50" height="34"/><rect x="290" y="50" width="50" height="34"/>
  </g>
  <text x="10" y="162" fill="currentColor">가득 참</text>
  <g class="f-acc" stroke="currentColor">
    <rect x="90" y="140" width="50" height="34"/><rect x="140" y="140" width="50" height="34"/><rect x="190" y="140" width="50" height="34"/>
    <rect x="240" y="140" width="50" height="34"/><rect x="290" y="140" width="50" height="34"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="115" y="161">L</text><text x="165" y="161">O</text><text x="215" y="161">H</text><text x="265" y="161">E</text><text x="315" y="161">L</text>
    <text x="115" y="102" font-size="12">0</text><text x="165" y="102" font-size="12">1</text><text x="215" y="102" font-size="12">2</text>
    <text x="265" y="102" font-size="12">3</text><text x="315" y="102" font-size="12">4</text>
    <text x="115" y="192" font-size="12">0</text><text x="165" y="192" font-size="12">1</text><text x="215" y="192" font-size="12">2</text>
    <text x="265" y="192" font-size="12">3</text><text x="315" y="192" font-size="12">4</text>
    <text x="215" y="120" font-size="12">↑ head = tail</text>
    <text x="215" y="210" font-size="12">↑ head = tail</text>
  </g>
  <rect x="380" y="45" width="330" height="160" rx="10" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="395" y="70" font-weight="bold">"HELLO" 를 칸 2부터 받으면</text>
    <text x="395" y="94">H(2) E(3) L(4) → 끝을 넘어 L(0) O(1)</text>
    <text x="395" y="118">head 는 2 → 3 → 4 → 0 → 1 → 2 로 한 바퀴.</text>
    <text x="395" y="146">head, tail 두 숫자(각 0..4)의 조합은 25가지,</text>
    <text x="395" y="168">그런데 들어 있는 개수는 0..5 의 6가지.</text>
    <text x="395" y="190">"같다" 는 0개인지 5개인지 말해주지 않는다.</text>
  </g>
  <text x="10" y="245" fill="currentColor">해결책은 "정보 1비트 더": 개수 세기(count) · 한 칸 포기 · 바퀴 홀짝 기억(mirror).</text>
</svg>
```

이 레슨은 같은 `rb_t` 구조체 위에 세 방식을 모두 구현하고, 함수 포인터 테이블 `rb_ops_t` 로 **같은 테스트**를 세 번 돌린다. `N = 5` 로 일부러 2의 거듭제곱이 아닌 크기를 써서, 인덱스 전진은 `%` 없이 `next_idx()` (비교 후 0으로) 로 한다 — 2의 거듭제곱 크기와 마스크는 다음 레슨 DS-B04 의 주제다.

### Part A — count 변수 방식

`count` 필드에 들어 있는 바이트 수를 따로 센다. `cnt_push / cnt_pop / cnt_len / cnt_empty / cnt_full` 을 구현한다.

- push: `count == N` 이면 false (UART **overrun** — 새 바이트를 버린다). 아니면 `buf[head]` 에 쓰고 head 전진, `count++`.
- pop: `count == 0` 이면 false. 아니면 `buf[tail]` 을 꺼내고 tail 전진, `count--`.
- N 칸을 **모두** 쓴다.

### Part B — 한 칸 비우기 방식

`count` 없이 head/tail 만 쓴다. **"head 를 한 칸 옮기면 tail 과 만나는 상태"를 가득 참으로 정의**해서 항상 빈 칸 하나를 남긴다. `slot_push / slot_pop / slot_len / slot_empty / slot_full` 을 구현한다.

- empty: `head == tail`, full: `next_idx(head) == tail`
- len: head 가 tail 뒤로 감겼는지에 따라 두 경우
- 쓸 수 있는 칸은 **N-1** 개 (N=5 면 4개)

### Part C — 미러 비트 방식 + 참조 모델 랜덤 테스트

1. head, tail 마다 **미러 비트**(`head_wrap`, `tail_wrap`)를 하나씩 둔다. 인덱스가 끝을 넘어 0으로 돌아갈 때마다 자기 비트를 뒤집는다(`adv_mirror`). 인덱스가 같을 때 미러 비트가 같으면 empty, 다르면 full. `mir_*` 5개 + `adv_mirror` 를 구현한다. N 칸을 모두 쓴다.
2. `fuzz_vs_model(ops, seed, stats)` 를 구현한다. xorshift32 (고정 시드) 로 100,000개의 push/pop 을 만들어 링과 **참조 모델**(감기지 않는 1차원 배열 큐 — 너무 단순해서 틀릴 수 없는 구현)에 똑같이 적용하고, 매 연산 뒤 반환값·꺼낸 바이트·len·empty·full 을 비교한다. 64 연산마다 push 확률을 70% / 30% 로 바꿔서 **가득 참과 비어 있음 경계를 자주** 밟게 한다.
3. 마지막으로 일부러 틀린 링(`OPS_NAIVE`: head/tail 만으로 N 칸을 다 쓰려는 구현)을 넣어서 랜덤 테스트가 **실패를 보고하는지** 확인한다. 버그를 못 잡는 테스트는 테스트가 아니다.

## 2. Problem (English)

> **UART RX ring buffer — distinguishing full from empty.**
>
> A UART receive ISR pushes bytes into a fixed-size circular buffer `uint8_t buf[N]` and the main loop pops them. No dynamic allocation. `N` is not necessarily a power of two (use `N = 5`).
>
> Implement `push`, `pop`, `len`, `empty`, and `full` three times, each with a different way of telling a full buffer from an empty one:
>
> 1. **Counter**: keep an element count alongside `head`/`tail`. Capacity `N`.
> 2. **One slot open**: use only `head`/`tail`; the buffer is full when advancing `head` would make it equal to `tail`. Capacity `N - 1`.
> 3. **Mirror bit**: keep one extra wrap bit per index that flips every time the index wraps from `N-1` to `0`. Equal indices with equal bits mean empty; equal indices with different bits mean full. Capacity `N`.
>
> `push` returns `false` and drops the byte when the buffer is full (overrun). `pop` returns `false` when empty.
>
> Then write a randomized differential test: generate 100,000 push/pop operations from a seeded PRNG, apply them to both the ring buffer and a trivially correct reference queue, and compare every return value, popped byte, `len`, `empty`, and `full`. Show that the test catches a ring that uses only `head`/`tail` with capacity `N`.
>
> Follow-up: which scheme would you choose when the producer is an ISR and the consumer is the main loop, and why?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "7/33 passed" — TODO 를 채워 33/33 을 만든다
```

구조: `rb_t` (세 방식 공용) → Part A/B/C 함수 → `rb_ops_t` 테이블 → 참조 모델·xorshift32·`fuzz_vs_model` → `check_hello()` (방식마다 같은 시나리오) → `main`.

## 4. 힌트

<details>
<summary>힌트 1 — 세 방식 모두 "쓰고 → 전진" / "읽고 → 전진" 은 같다</summary>

push 는 `buf[head] = b; head = next_idx(head);`, pop 은 `*out = buf[tail]; tail = next_idx(tail);`. 방식마다 다른 건 **그 전에 하는 full/empty 검사**와 **len 계산**뿐이다.

</details>

<details>
<summary>힌트 2 — one-slot-empty 의 len 은 두 경우</summary>

`head >= tail` 이면 데이터는 `[tail, head)` 한 덩어리 → `head - tail`. `head < tail` 이면 끝을 넘어 감긴 것 → `N - tail + head`. (unsigned 에서 `head - tail` 을 그냥 쓰면 거대한 수가 나온다.)

</details>

<details>
<summary>힌트 3 — 미러 비트는 "몇 바퀴째인가의 홀짝"</summary>

push 가 tail 보다 한 바퀴 앞서 있으면(=가득 참) 두 인덱스는 같고 바퀴 수는 1 차이 → 홀짝이 다르다. head 가 tail 을 두 바퀴 앞설 수는 없다(그 전에 full 에서 막힌다). 그래서 1비트로 충분하다. len 은 "미러가 같으면 `head - tail`, 다르면 `N - tail + head`".

</details>

<details>
<summary>힌트 4 — 랜덤 테스트의 뼈대</summary>

```c
for (i = 0; i < FUZZ_OPS; i++) {
    x = xorshift32(&seed);
    if (x % 100 < push_pct) { ok = push(b); want = model_len < cap; if (want) model 에 추가; 비교 }
    else                    { ok = pop(&got); want = model_len > 0; if (want) 모델에서 꺼낸 값과 got 비교 }
    len / empty / full 을 모델과 비교 → 다르면 i 와 값 출력 후 return false
}
```

`want` 를 **링이 아니라 모델에서** 계산하는 것이 핵심이다.

</details>

## 5. 풀이

### Part A — count: 가장 직관적, 그러나 공유 변수가 생긴다

```c
static bool cnt_push(rb_t *r, uint8_t b) {
    if (r->count == RB_N) return false;   /* overrun */
    r->buf[r->head] = b;
    r->head = next_idx(r->head);
    r->count++;
    return true;
}
```

불변식(invariant): `count ∈ [0, N]`, 그리고 `tail` 에서 시작해 `count` 칸이 유효한 데이터다. empty/full/len 이 모두 `count` 한 번 읽기라 가장 읽기 쉽다.

대가: `count` 를 **ISR 도 쓰고(++) main 도 쓴다(--)**. `count--` 는 Cortex-M 에서 `LDR → SUB → STR` 세 명령이라, LDR 과 STR 사이에 RX 인터럽트가 와서 `count++` 를 하면 main 이 낡은 값으로 덮어써 **그 ++ 가 사라진다** (lost update, 추가 연습 4 에서 재현). 그래서 count 방식은 pop 쪽에서 인터럽트를 잠깐 막거나(`__disable_irq()`), LDREX/STREX 원자 연산을 써야 한다. 싱글 스레드나 RTOS 뮤텍스 안에서 쓴다면 문제없다.

### Part B — one-slot-empty: 한 칸을 버리고 단일 쓰기 주체를 얻는다

```svg
<svg viewBox="0 0 720 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">one-slot-empty: N = 5, 가득 찬 상태 (4개 저장)</text>
  <g class="f-acc" stroke="currentColor">
    <rect x="40" y="50" width="60" height="36"/><rect x="100" y="50" width="60" height="36"/>
    <rect x="220" y="50" width="60" height="36"/><rect x="280" y="50" width="60" height="36"/>
  </g>
  <rect x="160" y="50" width="60" height="36" class="f-warn" stroke="currentColor" stroke-dasharray="5 3"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="70" y="73">L</text><text x="130" y="73">L</text><text x="190" y="73">-</text><text x="250" y="73">H</text><text x="310" y="73">E</text>
    <text x="70" y="104" font-size="12">0</text><text x="130" y="104" font-size="12">1</text><text x="190" y="104" font-size="12">2</text>
    <text x="250" y="104" font-size="12">3</text><text x="310" y="104" font-size="12">4</text>
    <text x="190" y="124" font-size="12">↑ head</text><text x="250" y="124" font-size="12">↑ tail</text>
  </g>
  <g fill="currentColor">
    <text x="380" y="58">empty : head == tail</text>
    <text x="380" y="82">full  : next_idx(head) == tail</text>
    <text x="380" y="106">len   : head &gt;= tail 이면 head - tail</text>
    <text x="380" y="128">        아니면 N - tail + head</text>
    <text x="10" y="160">비어 있는 칸 하나(점선)가 "가득 참" 을 표시한다. head 는 ISR 만, tail 은 main 만 쓴다.</text>
    <text x="10" y="184">→ 공유해서 쓰는(read-modify-write) 변수가 없다. 대가는 N-1 용량.</text>
  </g>
</svg>
```

len 계산 두 경우를 숫자로 확인하면:

| head | tail | 경우 | len | 의미 |
|---|---|---|---|---|
| 3 | 1 | head ≥ tail | 3 - 1 = 2 | 칸 1, 2 |
| 1 | 3 | head < tail (감김) | 5 - 3 + 1 = 3 | 칸 3, 4, 0 |
| 2 | 3 | head < tail, full | 5 - 3 + 2 = 4 | N-1 = 4 → full |
| 2 | 2 | empty | 0 | |

이 방식의 진짜 장점은 **각 변수에 쓰는 쪽이 하나뿐**이라는 것. ISR 은 `head` 만 쓰고 `tail` 은 읽기만, main 은 그 반대다. 32비트 정렬된 변수의 읽기/쓰기는 Cortex-M 에서 원자적이므로, 단일 생산자-단일 소비자(SPSC) 상황에서 **인터럽트를 막지 않고도** 안전하다 (멀티코어나 캐시가 끼면 메모리 배리어가 추가로 필요 — DS 트랙의 lock-free SPSC 아이템에서 다룬다). 그래서 실무에서 가장 많이 보는 방식이고, NVMe 제출 큐(Submission Queue)도 "tail + 1 == head 이면 full" 로 정확히 이 규칙을 쓴다.

용량 손해는 `1/N`. N=5 면 20% 지만 UART 버퍼(N=256)라면 0.4% 라 보통 무시한다.

### Part C — 미러 비트: 한 칸도 버리지 않고, 공유 변수도 없다

미러 비트 = "이 인덱스가 배열을 몇 바퀴 돌았는가" 의 홀짝. head 와 tail 을 각각 `(인덱스, 비트)` 쌍으로 보면, 사실상 **0..2N-1 범위를 도는 가상 인덱스**다.

```svg
<svg viewBox="0 0 720 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">미러 비트 = 가상 인덱스 0..2N-1 (N = 5)</text>
  <text x="10" y="62" fill="currentColor">가상</text>
  <text x="10" y="98" fill="currentColor">칸 번호</text>
  <text x="10" y="134" fill="currentColor">미러 비트</text>
  <g stroke="currentColor" fill="none">
    <rect x="100" y="44" width="56" height="28"/><rect x="156" y="44" width="56" height="28"/><rect x="212" y="44" width="56" height="28"/>
    <rect x="268" y="44" width="56" height="28"/><rect x="324" y="44" width="56" height="28"/>
  </g>
  <g class="f-mut" stroke="currentColor">
    <rect x="380" y="44" width="56" height="28"/><rect x="436" y="44" width="56" height="28"/><rect x="492" y="44" width="56" height="28"/>
    <rect x="548" y="44" width="56" height="28"/><rect x="604" y="44" width="56" height="28"/>
  </g>
  <rect x="212" y="44" width="56" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="492" y="44" width="56" height="28" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="128" y="63">0</text><text x="184" y="63">1</text><text x="240" y="63">2</text><text x="296" y="63">3</text><text x="352" y="63">4</text>
    <text x="408" y="63">5</text><text x="464" y="63">6</text><text x="520" y="63">7</text><text x="576" y="63">8</text><text x="632" y="63">9</text>
    <text x="128" y="98">0</text><text x="184" y="98">1</text><text x="240" y="98">2</text><text x="296" y="98">3</text><text x="352" y="98">4</text>
    <text x="408" y="98">0</text><text x="464" y="98">1</text><text x="520" y="98">2</text><text x="576" y="98">3</text><text x="632" y="98">4</text>
    <text x="128" y="134">0</text><text x="184" y="134">0</text><text x="240" y="134">0</text><text x="296" y="134">0</text><text x="352" y="134">0</text>
    <text x="408" y="134">1</text><text x="464" y="134">1</text><text x="520" y="134">1</text><text x="576" y="134">1</text><text x="632" y="134">1</text>
    <text x="240" y="160" font-size="12">↑ tail (2, 0)</text>
    <text x="520" y="160" font-size="12">↑ head (2, 1)</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="192">칸 번호는 같고(2 == 2) 미러 비트가 다르다 → 가상 거리 7 - 2 = 5 = N → 가득 참.</text>
    <text x="10" y="216">미러까지 같으면 거리 0 → 비었음. 거리는 항상 0..N 이므로 2N 가지 가상 위치면 충분하다.</text>
  </g>
</svg>
```

"HELLO" 를 칸 0부터 넣고 빼는 과정을 상태로 따라가면:

| 동작 | head (idx, 비트) | tail (idx, 비트) | 판정 | len |
|---|---|---|---|---|
| 초기 | (0, 0) | (0, 0) | idx 같음, 비트 같음 → empty | 0 |
| push H,E,L,L | (4, 0) | (0, 0) | 비트 같음 → 4 - 0 | 4 |
| push O | (0, **1**) | (0, 0) | idx 같음, 비트 다름 → **full** | 5 - 0 + 0 = 5 |
| push X | — | — | full → false (overrun) | 5 |
| pop → H | (0, 1) | (1, 0) | 비트 다름 → 5 - 1 + 0 | 4 |
| push ! | (1, 1) | (1, 0) | full | 5 |
| pop ×5 → ELLO! | (1, 1) | (1, **1**) | idx 같음, 비트 같음 → empty | 0 |

`adv_mirror()` 하나가 핵심이다: `if (++*i == RB_N) { *i = 0; *wrap ^= 1; }`. head 와 그 미러 비트는 ISR 만, tail 과 그 비트는 main 만 쓰므로 one-slot-empty 처럼 공유 쓰기 변수가 없다. 단, 인덱스와 비트를 **따로 두 번 쓰면** 그 사이를 상대편이 읽을 수 있다 — 실무에서는 비트를 인덱스 안에 합쳐(0..2N-1 가상 인덱스, 추가 연습 3) 한 번의 store 로 갱신한다. N 이 2의 거듭제곱이면 이 가상 인덱스를 아예 32비트 끝까지 키운 것이 DS-B04 의 **자유 증가 카운터**다.

> SSD 연결: NVMe 완료 큐(Completion Queue)의 **Phase Tag** 가 바로 미러 비트다. 컨트롤러는 큐를 한 바퀴 돌 때마다 써넣는 phase 값을 뒤집고, 호스트는 "엔트리의 phase == 내가 기대하는 phase" 이면 새 완료라고 판단한다. head 포인터를 공유하지 않고도 새 엔트리를 구별하는 같은 아이디어.

### 세 방식 비교

| | count | one-slot-empty | mirror bit |
|---|---|---|---|
| 쓸 수 있는 칸 | N | **N-1** | N |
| 추가 상태 | `count` (32비트) | 없음 | 비트 2개 (또는 인덱스 범위 2N) |
| empty | `count == 0` | `head == tail` | idx 같음 & 비트 같음 |
| full | `count == N` | `next(head) == tail` | idx 같음 & 비트 다름 |
| ISR ↔ main 공유 **쓰기** | `count` (경쟁!) | 없음 | 없음 |
| SPSC 에서 인터럽트 잠금 | pop/push 에 필요 | 불필요 | 불필요 (비트를 인덱스에 합쳤을 때) |
| 대표 사용처 | 단일 컨텍스트, RTOS 큐 내부 | UART 드라이버, NVMe SQ | NVMe CQ phase, 2의 거듭제곱 링(DS-B04) |

### 랜덤 테스트 — 참조 모델과 대조한다

직접 짠 `check_hello()` 같은 시나리오 테스트는 **내가 생각한 경우만** 확인한다. 링 버퍼 버그는 "감긴 상태에서 가득 찬 직후 pop" 처럼 경계가 겹칠 때 나오므로, 무작위로 많이 돌리고 **확실히 맞는 구현(모델)과 매 단계 비교**한다.

```svg
<svg viewBox="0 0 720 170" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <g stroke="currentColor">
    <rect x="10" y="60" width="130" height="44" rx="8" class="f-mut"/>
    <rect x="200" y="20" width="170" height="44" rx="8" class="f-acc"/>
    <rect x="200" y="100" width="170" height="44" rx="8" class="f-ok"/>
    <rect x="440" y="60" width="130" height="44" rx="8" class="f-mut"/>
    <rect x="610" y="60" width="100" height="44" rx="8" class="f-warn"/>
  </g>
  <g fill="none" stroke="currentColor">
    <path d="M140 75 L198 45"/><path d="M140 90 L198 120"/>
    <path d="M370 42 L438 75"/><path d="M370 122 L438 90"/><path d="M570 82 L608 82"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="75" y="80">xorshift32</text><text x="75" y="97" font-size="12">seed 고정</text>
    <text x="285" y="40">링 (count/slot/mirror)</text><text x="285" y="57" font-size="12">push / pop</text>
    <text x="285" y="120">모델: 1차원 배열 큐</text><text x="285" y="137" font-size="12">감기지 않아 자명함</text>
    <text x="505" y="80">비교</text><text x="505" y="97" font-size="12">ok, 바이트, len, full</text>
    <text x="660" y="80">불일치</text><text x="660" y="97" font-size="12">op 번호 출력</text>
  </g>
  <text x="10" y="164" fill="currentColor">push 확률을 64 연산마다 70% ↔ 30% 로 바꿔 full/empty 경계를 반복해서 지나가게 한다.</text>
</svg>
```

실제 실행 결과 (seed `0xC0FFEE`, 100,000 연산):

| scheme | push_ok | overrun | pop_ok | pop_empty | at_full | at_empty |
|---|---|---|---|---|---|---|
| count | 32069 | 17906 | 32064 | 17961 | 26665 | 26783 |
| one-slot-empty | 31061 | 18914 | 31057 | 18968 | 28334 | 28459 |
| mirror-bit | 32069 | 17906 | 32064 | 17961 | 26665 | 26783 |

읽는 법:

- **at_full / at_empty 가 각각 약 27%** — 연산 뒤 상태가 가득 참/비어 있음인 경우가 그만큼 많았다. 경계를 충분히 두드렸다는 증거 (통계를 찍지 않으면 테스트가 경계를 밟았는지 알 수 없다).
- count 와 mirror-bit 는 용량이 같으므로(N=5) 모든 숫자가 **똑같다** — 같은 연산 열에 대해 같은 FIFO 로 동작한다는 뜻.
- one-slot-empty 는 용량이 4라서 overrun 이 더 많다 (17906 → 18914).

그리고 버그 주입 확인:

```text
-- 테스트가 버그를 잡는가: head/tail 만 쓰는 순진한 링 (아래 불일치 한 줄은 예상된 것)
  [naive(bug)] op 6: len/empty/full = 0/1/0, 모델 len = 5
```

5바이트를 넣은 순간 head 가 tail 을 따라잡아 `head == tail` → 순진한 링은 "0개, 비었음" 이라고 답한다. 모델은 5개. 테스트가 1. 문제의 그림에 나온 바로 그 모호성을 op 6 (0부터 센 7번째 연산)에서 잡아냈다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 33/33 passed
cc -std=c11 -Wall -Wextra -g -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan   # 인덱스가 buf 밖으로 나가면 ASan 이 잡는다
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| head/tail 만 두고 N 칸을 다 씀 | N 개 넣으면 `head == tail` → empty 로 오판, 데이터 통째로 사라짐 | 세 방식 중 하나로 정보 1비트 추가 |
| `len = head - tail` (one-slot-empty, unsigned) | head < tail(감김)이면 거대한 수 | `head >= tail ? head - tail : N - tail + head` |
| full 검사를 쓰기 **뒤에** 함 | 이미 tail 칸(읽지 않은 데이터)을 덮어씀 | 검사 → 쓰기 → 전진 순서 |
| head 를 먼저 전진하고 나서 `buf[head] = b` | 한 칸 건너뛰어 쓰고, 소비자가 아직 안 쓴 칸을 읽음 | 쓰기 → 전진 (ISR 이 head 를 공개하는 순간 데이터가 이미 있어야 함) |
| `head = (head + 1) % N` 을 Cortex-M0 에서 | M0 에는 나눗셈 명령이 없어 라이브러리 호출 (수십 사이클), ISR 에서 느림 | `next_idx()` 비교 방식 또는 2의 거듭제곱 + 마스크 (DS-B04) |
| count 방식에서 ISR 과 main 이 둘 다 `count` 수정 | `count--` RMW 사이에 ISR `count++` → lost update | 임계 구역 또는 one-slot/mirror 로 변경 |
| `head`, `tail` 에 `volatile` 없음 (ISR 공유) | main 의 `while (empty())` 루프에서 컴파일러가 head 읽기를 루프 밖으로 빼서 영원히 빈 것으로 봄 | ISR 공유 인덱스는 `volatile` (+ 멀티코어면 배리어) |

## 8. 면접 Q&A

<details>
<summary>Q1. 링 버퍼에서 full 과 empty 를 어떻게 구별하나요? 각각의 장단점은?</summary>

세 가지: ① 개수 변수 — 직관적, N 칸 다 씀, 하지만 생산자/소비자가 같은 변수를 수정해 인터럽트 잠금 필요 ② 한 칸 비우기 — 추가 상태 없음, 각 인덱스를 한쪽만 써서 SPSC lock-free, 대가는 1칸 ③ 미러 비트 — N 칸 다 쓰고 공유 쓰기도 없음, 대신 인덱스 전진 로직이 조금 복잡. 크기가 2의 거듭제곱이면 ④ 자유 증가 카운터(`len = head - tail`)가 미러 비트의 일반화로 가장 깔끔하다 (DS-B04). 꼬리질문 "UART ISR 용으로 뭘 고르겠나?" → one-slot-empty 또는 자유 증가 카운터, 이유는 공유 쓰기 변수가 없어서 인터럽트를 막지 않아도 되기 때문.

</details>

<details>
<summary>Q2. 버퍼가 가득 찼을 때 새 바이트를 버릴까요, 가장 오래된 바이트를 덮어쓸까요?</summary>

용도에 따라 다르다. **명령/프로토콜 스트림(UART AT 명령, 패킷)** 은 중간이 빠지면 파싱이 깨지므로 새 데이터를 버리고 overrun 카운터를 올려 상위에 알린다 (하드웨어 USART 의 ORE 플래그와 같은 의미). **로그·센서 최근값** 은 최신 데이터가 중요하므로 가장 오래된 것을 덮어쓴다 (추가 연습 1). 덮어쓰기 모드는 생산자가 tail 까지 움직여야 해서 SPSC 단일 쓰기 원칙이 깨진다는 점도 말하면 좋다.

</details>

<details>
<summary>Q3. ISR 이 push 하고 main 이 pop 할 때, 어떤 변수에 volatile 이 필요하고 인터럽트를 막아야 하는 곳은 어디인가요?</summary>

양쪽이 공유하는 `head`, `tail` (count 방식이면 `count`) 은 `volatile` — 컴파일러가 main 루프에서 head 를 한 번만 읽고 캐시하는 것을 막는다. 인터럽트 잠금은 **두 컨텍스트가 같은 변수를 read-modify-write 할 때만** 필요하다: count 방식의 `count++/--`. one-slot-empty 는 head 를 ISR 만, tail 을 main 만 쓰므로 정렬된 32비트 store 가 원자적인 Cortex-M 단일 코어에선 잠금이 필요 없다. 단 쓰기 순서가 중요하다: 데이터를 `buf` 에 쓴 **뒤에** head 를 갱신해야 한다 (멀티코어/캐시가 있으면 그 사이에 release 배리어).

</details>

<details>
<summary>Q4. 링 버퍼를 어떻게 테스트하나요? 테스트가 충분한지 어떻게 아나요?</summary>

① 경계 시나리오 (빈 pop, 가득 찬 push, 감긴 상태에서 full/empty) ② 랜덤 차분 테스트: 고정 시드 PRNG 로 연산 열을 만들어 참조 모델과 매 단계 비교 — 시드 고정이라 실패를 재현할 수 있고, 첫 불일치 op 번호로 디버깅 ③ 커버리지 근거: at_full/at_empty 같은 통계를 찍어 경계를 실제로 밟았는지 확인 ④ 버그 주입(mutation): 일부러 틀린 구현을 넣어 테스트가 잡는지 확인 ⑤ ASan 으로 인덱스 범위 밖 접근 확인. 동시성은 이 방법으로 못 잡으므로 TSan/스트레스 테스트를 따로 한다.

</details>

<details>
<summary>Q5. 버퍼 크기는 어떻게 정하나요? (115200 baud UART 예)</summary>

115200 baud, 8N1 이면 바이트당 10비트 → 초당 약 11,520 바이트, 즉 **약 87 µs 마다 1바이트**. main 루프가 버퍼를 비우지 못하는 최악의 시간(예: 플래시 섹터 지우기 20 ms) 동안 들어올 양 = 20 ms × 11.5 B/ms ≈ 230 바이트 → 여유를 두고 256 (2의 거듭제곱이면 마스크 인덱싱도 가능). "최악 지연 × 유입률 + 여유" 가 공식. DMA 를 쓰면 ISR 대신 DMA 가 head 를 움직이고(circular 모드), half/full 전송 완료 인터럽트나 IDLE 라인 감지로 소비한다.

</details>

<details>
<summary>Q6. one-slot-empty 에서 len 을 head - tail 한 줄로 못 쓰는 이유는? 한 줄로 쓰는 방법은?</summary>

head < tail (감김) 이면 unsigned 뺄셈이 2^32 근처의 거대한 수가 된다. `(head - tail + N) % N` 으로 한 줄에 쓸 수는 있지만 `%` 가 비싸다. 근본 해법은 인덱스를 0..N-1 로 자르지 않고 **계속 증가시키는 것**: N 이 2의 거듭제곱이면 `head - tail` 이 unsigned 래핑 덕분에 항상 정확하다 — DS-B04.

</details>

## 9. 추가 연습

### 연습 1 — 덮어쓰기(overwrite-oldest) 모드

가득 찼을 때 가장 오래된 바이트를 버리고 새 바이트를 넣는 `bool ow_push(rb_t *r, uint8_t b)` (덮어썼으면 true). N=8 에 `"ABCDEFGHIJ"` 를 넣으면 `"CDEFGHIJ"` 가 남고 2번 덮어써야 한다.

<details>
<summary>답 보기</summary>

count 방식에서 `count == N` 이면 먼저 `tail = next_idx(tail); count--;` 로 가장 오래된 것을 버리고 평소처럼 push. 생산자가 tail 까지 건드리므로 ISR/main 에서 쓰려면 임계 구역이 필요하다 — "로그 버퍼에 lock-free 가 어려운 이유".

</details>

### 연습 2 — 완전한 한 줄만 꺼내기 (`rb_getline`)

UART 로 `"OK\r\nER"` 가 들어왔다. `'\n'` 이 버퍼에 있을 때만 그 줄을 꺼내고(끝의 `'\r'` 제거) 길이를 돌려준다. 완전한 줄이 없으면 -1 이고 **데이터는 그대로** 남아야 한다. 이후 `"R\n"` 이 오면 `"ERR"`.

<details>
<summary>답 보기</summary>

두 단계: ① 꺼내지 않고 tail 부터 count 칸을 훑어(peek) `'\n'` 위치 k 를 찾는다. 없으면 -1 ② 있으면 k+1 바이트를 pop 하면서 `'\n'` 은 빼고 복사, 마지막 `'\r'` 제거, NUL 종료. 먼저 pop 부터 하면 줄이 덜 왔을 때 앞부분을 잃는다.

</details>

### 연습 3 — 미러 비트를 인덱스에 숨기기 (0..2N-1)

head, tail 을 0..2N-1 범위로 돌리고 칸 번호는 `h < N ? h : h - N`, 미러 비트는 `h >= N` 으로 보는 `rb2_t` 를 구현하라. empty: `head == tail`, full: `len == N`. 본문과 같은 방식으로 참조 모델과 50,000 연산 랜덤 대조.

<details>
<summary>답 보기</summary>

`adv2(h) = (h + 1 == 2N) ? 0 : h + 1`, `len = head >= tail ? head - tail : 2N - tail + head`. 가득 차면 칸 번호는 같고 가상 인덱스는 N 만큼 차이 난다. 인덱스와 비트가 한 변수라 **한 번의 store** 로 갱신된다 → ISR 사이에서 찢어진 상태(인덱스는 바뀌고 비트는 아직)를 볼 일이 없다. N 이 2의 거듭제곱이면 범위를 2N 대신 2^32 로 키워 DS-B04 가 된다.

</details>

### 연습 4 — count 방식의 lost update 재현

main 의 `count--` 를 `tmp = count; /* ISR */ count = tmp - 1;` 로 쪼개고 그 사이에서 ISR push 를 호출하라. 'A','B' 가 있는 상태에서 pop 하면 실제로는 'B','Z' 두 개가 남는데 count 는 몇이 되는가?

<details>
<summary>답 보기</summary>

count = 1. main 이 tmp = 2 를 읽은 뒤 ISR 이 count 를 3 으로 올렸지만, main 이 낡은 tmp 에서 1 을 뺀 1 로 덮어썼다. head/tail 로 계산한 실제 길이는 2. 다음 push/pop 들이 이 오차를 영원히 안고 간다 (결국 데이터 유실 또는 쓰레기 읽기). 고치는 법: pop 의 `count--` 를 `__disable_irq()` / `__enable_irq()` 로 감싸거나, 공유 쓰기 변수가 없는 방식으로 바꾼다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 11/11 passed
```

## 10. 핵심 정리

- 링 버퍼의 근본 문제: `head == tail` 은 empty 와 full 둘 다 뜻한다 → **정보 1비트**가 더 필요.
- **count**: N 칸, 가장 단순. 하지만 ISR 과 main 이 같이 RMW → 인터럽트 잠금 필요.
- **one-slot-empty**: full = `next(head) == tail`, N-1 칸. head 는 생산자만, tail 은 소비자만 → SPSC 에서 잠금 불필요. UART 드라이버·NVMe SQ 의 방식.
- **mirror bit**: 인덱스마다 바퀴 홀짝 1비트. idx 같고 비트 같으면 empty, 다르면 full. N 칸 + 공유 쓰기 없음. = 0..2N-1 가상 인덱스. NVMe CQ Phase Tag.
- push 순서: **검사 → 쓰기 → 인덱스 공개**. 인덱스를 먼저 옮기면 소비자가 빈 칸을 읽는다.
- 테스트: 고정 시드 랜덤 연산 열을 **참조 모델과 매 단계 대조** + 경계 통계 + 버그 주입으로 테스트 자체를 검증.

## 참고자료

- [Wikipedia — Circular buffer (Full / Empty buffer distinction, mirroring)](https://en.wikipedia.org/wiki/Circular_buffer)
- [NVM Express Base Specification — Queue Full / Phase Tag (Completion Queue Entry)](https://nvmexpress.org/specifications/)
- [Zephyr RTOS — Ring Buffers (`ring_buf`)](https://docs.zephyrproject.org/latest/kernel/data_structures/ring_buffers.html)
- FreeRTOS — Stream Buffers / Message Buffers (커널 문서, `xStreamBufferSend` / `xStreamBufferReceive`)
- [STM32F4 Reference Manual RM0090 — USART (RXNE, ORE overrun flag)](https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [Linux kernel Documentation — Circular Buffers (`circ_buf.h`)](https://docs.kernel.org/core-api/circular-buffers.html)
