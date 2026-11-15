---
id: DS-A02
time: 120분
updated: 2026-11-08
---
# DS-A02 ISR↔태스크 링 버퍼와 Bip Buffer(DMA 연속 영역)

## 학습 목표

- 단일 코어 ISR 링에서 **배리어가 어느 쪽에 왜 필요한지**를 줄 단위로 말할 수 있다. 핵심 한 줄은 "순서를 지켜야 하는 쪽은 **끼어들 수 있는 문맥(태스크)** 이다"이다. ISR 쪽 순서는 뒤집어도 단일 코어에서는 깨지지 않는다는 것까지 체크로 확인한다.
- 컴파일러 배리어만으로 되는 것과 `dmb`가 필요한 것을 구분한다. Cortex-M0+/M4/M7에서 실제 `clang -S` 출력으로 확인한다. M7 스케줄링에서 `tail` 저장이 칸 읽기 **앞으로 실제로 가는 것**을 본다.
- bip buffer를 `reserve / publish / commit / read / release`로 구현하고, 꼬리에 안 들어가는 예약, 구멍(watermark), 영역 A 소진 후 r 점프를 모두 설명한다. 도달 가능한 모든 상태를 전수로 돌린다.
- **공간 보장 정리 N ≥ D + 2M**을 증명하고 전수 탐색으로 확인한다. 연속 할당의 대가가 정확히 M 바이트임을 말할 수 있다.
- UART RX DMA와 TX DMA를 bip 위에 올리고, ISR을 모든 접근 앞에서 터뜨리는 결정적 인터리버로 검증한다. "commit 후 busy 확인" 순서가 왜 lost wakeup을 막는지, 코어가 둘이면 왜 그것만으로 부족한지 설명한다.

## 1. 문제

UART, SPI, 오디오 드라이버는 거의 다 ISR/DMA와 태스크 사이의 버퍼로 만들어진다. 그래서 면접에서 "이 링에 배리어가 필요한가요?"와 "DMA는 연속 메모리만 받는데 링이 끝에서 갈라지면요?"는 짝으로 나온다. 앞 질문을 "volatile 쓰면 됩니다"로 답하면 거기서 끝난다.

DS-A01이 SMP SPSC와 acquire/release를 맡았다. DS-B04는 자유 증가 카운터, DS-M10은 패딩과 분할의 비교를 다뤘다. 여기서는 그 위에서 **단일 코어 ISR 고유의 분석**과 **연속 영역 할당**만 다룬다. Rust 판은 see:RUST-M07.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">누가 누구를 끊을 수 있나 — 끊기는 쪽만 순서가 보인다</text>
  <text x="10" y="58" fill="currentColor">태스크 (소비자)</text>
  <rect x="130" y="40" width="96" height="30" class="f-mut" stroke="currentColor"/>
  <text x="138" y="60" fill="currentColor" font-family="monospace">h = head</text>
  <rect x="236" y="40" width="26" height="30" class="f-acc" stroke="currentColor"/>
  <text x="243" y="60" fill="currentColor" font-family="monospace">B1</text>
  <rect x="272" y="40" width="126" height="30" class="f-mut" stroke="currentColor"/>
  <text x="280" y="60" fill="currentColor" font-family="monospace">v = buf[t]</text>
  <rect x="408" y="40" width="26" height="30" class="f-acc" stroke="currentColor"/>
  <text x="415" y="60" fill="currentColor" font-family="monospace">B2</text>
  <rect x="444" y="40" width="120" height="30" class="f-mut" stroke="currentColor"/>
  <text x="452" y="60" fill="currentColor" font-family="monospace">tail = t+1</text>
  <g stroke="currentColor" stroke-dasharray="3 3" fill="none">
    <path d="M231 74 L231 118"/><path d="M267 74 L267 118"/><path d="M403 74 L403 118"/><path d="M439 74 L439 118"/>
  </g>
  <text x="572" y="58" fill="currentColor">↓ 점선마다 ISR 가능</text>
  <text x="10" y="142" fill="currentColor">ISR (생산자)</text>
  <rect x="130" y="124" width="400" height="30" class="f-warn" stroke="currentColor"/>
  <text x="140" y="144" fill="currentColor" font-family="monospace">buf[h]=x → head=h+1  : 태스크는 중간 상태를 절대 못 본다</text>
  <g fill="currentColor">
    <text x="10" y="186">B1 이 없으면: 칸을 먼저 읽고(낡은 값) → ISR 이 채움 → head 를 읽어 "데이터 있음" → 낡은 값 반환</text>
    <text x="10" y="208">B2 가 없으면: tail 을 먼저 저장(칸 반납) → ISR 이 그 칸을 덮어씀 → 덮인 값을 읽음</text>
    <text x="10" y="230">ISR 안의 쓰기 순서: 태스크는 ISR 전이나 후만 보므로 순서가 보이지 않는다 → 단일 코어에서 무해</text>
    <text x="10" y="256" font-weight="bold">단, 방향이 바뀌면(TX: ISR 소비자, 태스크 생산자) 태스크 쪽 칸 쓰기 → head 사이가 위험해진다.</text>
    <text x="10" y="280">코어가 둘이면 "ISR 은 끊기지 않는다" 가 사라지므로 양쪽 다 필요하다 (DS-A01).</text>
    <text x="10" y="304">DMA 는 메모리 모델 밖의 버스 마스터다: 캐시 유지보수 + 도어벨 앞 DSB 문제 (§5 표).</text>
  </g>
</svg>
```

| 파트 | 구조 | 체크 (전부 결정적) | 관찰 데모 |
|---|---|---|---|
| A | ISR↔태스크 바이트 링, RX·TX | trace로 배리어 위치 확인, ISR 발사 위치 전수, 순서 뒤집은 변형이 깨지는 것 | `demo_asm.c` |
| B | bip buffer | 이름 붙인 랩 시나리오, 상태 공간 전수, 보장 정리, 100만 난수 연산 | `demo_waste.c`, `demo_smp.c` |
| C | UART RX DMA / TX DMA | 매 접근 ISR, 주기 발사 전수, lost wakeup 변형 | `demo_rx_chunk.c` |

### Part A — 단일 코어 ISR ↔ 태스크 링과 배리어 분석

8바이트 링, 자유 증가 `head/tail`(DS-B04). 4개 함수를 만든다.

- RX 방향: `rb_isr_put`(ISR 생산자, 가득 차면 버리고 센다) + `rb_task_get`(태스크 소비자)
- TX 방향: `rb_task_put`(태스크 생산자) + `rb_isr_get`(ISR 소비자)

공유 메모리는 하네스의 `IDX_LOAD/IDX_STORE/BYTE_RD/BYTE_WR`로만 만진다. 배리어는 `ACQ_BARRIER()/REL_BARRIER()`로 표시한다. 체크는 ① trace 문자열로 배리어 위치, ② 미리 채운 양 p, ISR 버스트, 발사 위치 k 전수 스윕, ③ 하네스의 "순서를 뒤집은 변형"이 깨지는지(ISR 쪽 뒤집기는 깨지지 않는지), ④ 각 방향 100만 연산이다.

### Part B — Bip buffer

`bip_reserve(n)`(정확히 n), `bip_reserve_max(min, max, &got)`(DMA용), `bip_publish(k)`(예약 유지), `bip_commit(k)`(k=0이면 취소), `bip_read(&n)`, `bip_release(m)`. 공유 상태는 `r`(소비자), `w`, `wm`(생산자) 3개다. 예약과 읽기 상태는 각자 비공개다. 링크드 구조, 나눗셈, 마스크, 락이 없고 cap은 2의 거듭제곱일 필요가 없다.

체크할 랩 시나리오: 꼬리에 들어감 / 딱 맞음(w==cap) / 안 들어가서 0번지로 / 앞쪽도 안 됨 / `n==r` 거절 / 구멍 안 예약 / 예약 취소 / 부분 commit / r이 wm에 닿는 release / 끝에서 빈 상태의 늦은 점프 / 예약이 열린 채로 소비자가 지나가는 경우.

### Part C — UART RX DMA와 TX DMA

- RX: `uart_rx_arm`(예약 → DMA 시작), `uart_rx_isr`(IDLE/TC 공용), `uart_rx_task`(읽기 → 반납 → 필요하면 재무장).
- TX: `uart_tx_send`(reserve → 복사 → commit → kick), `uart_tx_kick`, `uart_tx_isr`(TC: 반납 → 다음 덩어리).

하드웨어 모델은 메모리 주소, 남은 전송 수(STM32의 NDTR류), 수신 레지스터 1바이트다. 체크: 받은 바이트가 DMA가 쓴 순서 그대로, DMA가 유효 데이터를 덮지 않음, 메시지가 DMA 전송 둘로 쪼개지지 않음, 끝나면 데이터가 묶여 있지 않음.

## 2. Problem (English)

> **ISR ring barrier analysis and a bipartite (bip) buffer for DMA.**
>
> Part A. On a single-core MCU, implement an SPSC byte ring for both directions: `rb_isr_put`/`rb_task_get` and `rb_task_put`/`rb_isr_get`. Use relaxed loads/stores and compiler barriers only. State which barriers are required and why, and what would change on a Cortex-M7 with cache, or with two cores.
>
> Part B. Implement a bip buffer that always hands out **contiguous** memory:
> ```c
> uint8_t       *bip_reserve(bip_t *b, uint32_t n);                        // NULL if no contiguous n bytes
> uint8_t       *bip_reserve_max(bip_t *b, uint32_t min, uint32_t max, uint32_t *got);
> bool           bip_publish(bip_t *b, uint32_t k);                        // expose k bytes, keep the rest reserved
> bool           bip_commit (bip_t *b, uint32_t k);                        // expose k bytes, drop the rest; 0 cancels
> const uint8_t *bip_read   (bip_t *b, uint32_t *n);                       // first contiguous block
> bool           bip_release(bip_t *b, uint32_t m);
> ```
> State: `r` (consumer), `w` and `wm` (producer). Not wrapped (`w >= r`): valid data is `[r, w)`. Wrapped (`w < r`): valid data is `[r, wm)` then `[0, w)`. `w` must never reach `r` while wrapped.
>
> Example (cap 16, r = 8, w = 14): `reserve(4)` does not fit in the 2-byte tail, so it returns offset 0. After `commit(4)`, `w = 4, wm = 14`. `read` then returns 6 bytes at offset 8. Releasing them moves `r` to 0 and the next `read` returns 4 bytes at offset 0.
>
> Part C. Build a UART RX path where DMA writes straight into reservations and an IDLE/TC interrupt publishes progress, and a TX path where each message is one contiguous DMA transfer.
>
> Follow-ups: if reservations are at most M and live data at most D when reserving, how large must the buffer be so that `reserve` never fails? Does the producer ISR need a barrier on a single core? What is the single-core lost-wakeup argument for "commit, then check busy", and why does it stop being enough on two cores?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과(실제 출력, 앞부분과 끝만):

```text
== Part A: single-core ISR <-> task ring
FAIL starter.c:1118  all
FAIL starter.c:1119  !rb_isr_put(&q, 99) && q.dropped == 1u
...
  rb_task_get trace: 
FAIL starter.c:568  ok && trace_fenced(t, 'L', 'r') && trace_fenced(t, 'r', 'S')
...
20/102 passed
```

하네스 읽는 법:

- **인터리버.** `IDX_LOAD` 등은 접근 직전에 `ic_step()`을 부른다. 테스트가 "k번째 접근 앞에서 ISR을 끝까지 실행"으로 정해 둔다(스레드 없음). `k`를 전부 돌리면 **모든 끼어들기 위치**를 시험한 셈이다. 실행은 매번 같다.
- **단일 코어의 의미.** ISR은 태스크를 끊고 끝까지 돈 뒤 돌아온다. `ic_fire`가 정확히 그렇게 한다. ISR 도중에는 스텝이 멈춘다.
- **순서 뒤집기.** `bad_get_tail_first` 같은 함수는 "컴파일러나 CPU가 이렇게 바꿔도 되나?"를 C로 적은 것이다. 인터리버가 깨지는 k를 찾으면 그 순서는 불가능해야 한다. 배리어는 호스트에서 관찰되지 않으므로, 배리어 존재 자체는 trace로, 배리어의 효과는 `demo_asm.c`의 실제 asm으로 확인한다.
- **가드.** 틀린 구현이 말이 안 되는 길이를 주어도 크래시 대신 FAIL이 나오도록, 바이트 접근은 범위를 벗어나면 수행하지 않고 위반으로 센다.

## 4. 힌트

<details><summary>힌트 1 — 어느 쪽 함수에 배리어가 필요한가</summary>

ISR은 태스크가 도는 도중에 끼어든다. 반대로 태스크는 ISR이 도는 도중에 끼어들 수 없다. 그러니 태스크 함수의 공유 접근 **사이사이**가 위험하다. 그 함수의 어떤 두 접근의 순서를 바꾸면 ISR이 이상한 상태를 볼지 하나씩 적어 본다.
</details>

<details><summary>힌트 2 — 왜 w == r 을 금지해야 하나</summary>

bip는 r과 w 두 값으로 "감겼나 / 비었나"를 구분한다. 안 감겼을 때 `w == r`은 "비었다"이다. 감긴 상태에서 w가 r을 따라잡으면 같은 값이 "꽉 찼다"가 되어 구분할 수 없다. 그래서 감긴 쪽 예약은 `w + n < r`로 **엄격**해야 한다. 반대로 안 감겼을 때 꼬리는 `w + n == cap`까지 허용된다. w가 r을 만나지 않기 때문이다.
</details>

<details><summary>힌트 3 — wm 은 언제 쓰나</summary>

0번지로 감기는 예약을 **게시할 때** 쓴다. 옛 w 값이 데이터의 끝이다. 예약만 하고 취소하면 wm은 그대로여야 한다. 순서는 wm 먼저, w 나중이다. 소비자는 w가 감겼다고 말할 때만 wm을 믿기 때문이다.
</details>

<details><summary>힌트 4 — r 을 0 으로 언제 옮기나</summary>

감긴 상태에서 r이 wm에 닿으면 영역 A가 끝난 것이다. 그 자리에서 `r = 0`을 저장해 영역 B를 새 A로 만든다. 그런데 소비자가 끝까지 읽었을 때는 아직 안 감겨 있다가 그 뒤 생산자가 감기는 경우도 있다(`r == w == cap`). 그래서 release뿐 아니라 read에도 같은 검사가 있어야 한다.
</details>

<details><summary>힌트 5 — DMA 재무장</summary>

ISR은 TC에서 곧바로 다음 영역을 예약해야 한다. 공간이 없으면 armed를 내려 두고, 태스크가 release한 뒤 다시 건다. 이때 reserve는 생산자 작업이다. ISR과 동시에 하면 안 되므로 짧게 인터럽트를 막는다. 예약 크기에 상한을 둘지 생각해 본다(§5 Part C의 표).
</details>

<details><summary>힌트 6 — TX kick 순서</summary>

`commit → busy 확인` 순서를 쓴다. 반대 순서로 해서 ISR이 그 사이에 "비었네, busy=0"을 해 버리면 어떻게 될까?
</details>

## 5. 풀이

### Part A — 배리어 분석

두 가지를 분리해서 본다. **컴파일러**는 소스의 순서를 바꿀 수 있다. **CPU**는 자기가 낸 명령의 순서를 바꿀 수 있다. 단일 코어에서 같은 PE는 자기 메모리 접근을 프로그램 순서대로 관찰하므로(ARMv7-M ARM의 단일 PE 규칙 요지, 절 번호 미확인) CPU 순서는 ISR과 태스크 사이에 문제가 되지 않는다. 남는 것은 컴파일러뿐이다.

**태스크 소비자 `rb_task_get`을 줄 단위로:**

| 순서 | 코드 | 왜 여기에 / 없으면 어떻게 깨지나 |
|---|---|---|
| 1 | `t = tail` (relaxed) | 내 변수다. 다른 쪽이 안 쓴다. atomic인 이유는 ISR이 읽기 때문이다 (찢어짐 없음, 접근 삭제 없음) |
| 2 | `h = head` (relaxed) | ISR의 게시를 관찰한다. atomic이 아니면 컴파일러가 없애서 반복을 한 번만 읽을 수 있다 |
| 3 | `if (h == t) return` | |
| 4 | `ACQ_BARRIER()` [B1] | 칸 읽기를 2번 위로 못 올린다. 올리면 "낡은 칸 읽기 → ISR이 칸 채우고 head 증가 → head 새로 읽음 → 낡은 값 반환" (변형 `bad_get_slot_first`) |
| 5 | `v = buf[t & M]` | 이 시점 칸은 ISR이 건드리지 않는다. tail을 아직 안 옮겼다 |
| 6 | `REL_BARRIER()` [B2] | tail 저장을 칸 읽기 위로 못 올린다. 올리면 "tail 먼저 → ISR이 그 칸에 씀(가득 찼을 때) → 덮인 값 읽음" (변형 `bad_get_tail_first`) |
| 7 | `tail = t + 1` (relaxed) | 공간 반납 |

**TX 방향 `rb_task_put`:** 태스크가 게시자가 된다. `칸 쓰기 → [B3] → head 저장`이다. 뒤집으면 ISR이 아직 안 쓴 칸을 읽어 내보낸다(`bad_put_head_first`). 짝이 되는 `tail` 읽기 뒤의 `ACQ_BARRIER`는 형식 모델을 맞추려는 것이다. 컴파일러가 store를 분기 앞으로 옮기는 것은 어차피 금지다(C11의 데이터 레이스 금지, 없던 쓰기를 만들 수 없다).

**ISR 쪽은?** ISR은 태스크를 끊지만, 태스크는 ISR을 끊지 못한다. 그래서 `buf[h]=x`와 `head=h+1`의 순서를 뒤집어도 태스크는 "ISR 전"이나 "ISR 후"만 본다. 체크로 확인했다.

실제 출력(solution.c):

```text
  rb_task_get trace: LL|r|S
  rb_task_put trace: LL|w|S
  RX sweep: 4104 runs (p, burst, k 전부), 위반 0
  TX sweep: 2088 runs, 위반 0
  순서를 뒤집은 변형의 위반 run 수: tail 먼저 120, 칸 먼저 1440, TX head 먼저 120, ISR 쪽 head 먼저 0
  stress: RX 0, TX 0 violations (각 100만 연산, ISR 30%/접근, 검사 K=1024)
```

**ISR 쪽 무해**는 다음 조건에서만 참이다. ① 소비자가 그 ISR을 끊지 못한다(더 높은 우선순위의 ISR이 소비자이면 그 ISR이 "태스크" 역할이다). ② 코어가 하나다. 그래서 ISR 쪽에도 `REL_BARRIER`를 남겼다. 비용은 0이고, 조건이 바뀌어도 맞는다.

**Asm: 진짜로 바뀌나.** `demo_asm.c`의 `get_plain`은 인덱스도 칸도 일반 변수다. 아래는 실제 `-S` 출력에서 주소 로드(`movw/movt`)와 지시어를 뺀 것이고, `;` 주석은 내가 붙였다.

<!-- code: demo_asm.c -->

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -ffreestanding -S demo_asm.c -o -
clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -mthumb -std=c11 -O2 -ffreestanding -S demo_asm.c -o -
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -std=c11 -O2 -ffreestanding -S demo_asm.c -o -
```

```text
@ Cortex-M4  get_plain                   @ Cortex-M7  get_plain
  ldr   r0, [r0]     ; head 한 번         ldr   r0, [r0]
  ldr   r2, [r1]     ; tail 한 번         ldr   r2, [r1]
  cmp   r0, r2                            cmp   r0, r2
  bne   .LBB0_2                           bne   .LBB0_2
  b     .LBB0_1      ; 자기 자신으로 점프  b     .LBB0_1
  uxtb  r0, r2                            uxtb  r0, r2
  ldrb  r0, [r3, r0] ; 칸 읽기            adds  r2, #1
  adds  r2, #1                            str   r2, [r1]      ; ← tail 저장이 먼저
  str   r2, [r1]     ; tail 저장          ldrb  r0, [r3, r0]  ; ← 칸 읽기가 나중
  bx    lr                                bx    lr
```

두 가지가 실제로 일어났다.

1. **스핀이 사라졌다.** `head`와 `tail`을 한 번만 읽고 같으면 `b .LBB0_1`로 영원히 돈다(양쪽 타깃 공통). 컴파일러 입장에서 이 루프 안에서는 변수가 바뀔 수 없기 때문이다. 이것은 `while(flag==0)` 버그의 원형이다.
2. **M7에서만 B2 위반이 나왔다.** 같은 C 코드가 `-mcpu=cortex-m7`에서는 `str`(tail 게시)이 `ldrb`(칸 읽기) **앞에** 있다. `-mcpu`별 스케줄링 모델 차이로 보인다(M4 출력은 소스 순서였다). 일반 변수니 합법이다. "M4에서 잘 돌았다"는 것이 아무 증거도 아닌 이유가 이것이다. 이 순서에서 tail 저장 직후 ISR이 들어오면 변형 `bad_get_tail_first`와 정확히 같다.

배리어를 넣은 것의 출력(M7, `get_relaxed_fence`의 스핀 뒤):

```text
  @MEMBARRIER          ; ACQ_BARRIER (명령 0개)
  uxtb  r0, r2         ; t & 255
  adds  r2, #1
  ldrb  r0, [r3, r0]   ; 칸 읽기
  @MEMBARRIER          ; REL_BARRIER (명령 0개)
  str   r2, [r1]       ; tail 게시
```

`@MEMBARRIER`는 clang이 찍는 asm 주석이다. 명령이 없다. 그런데 `ldrb`와 `str`이 다시 소스 순서로 돌아왔다. 컴파일러 배리어의 효과가 asm에서 그대로 보인다. `get_acq_rel`(코어가 둘이어도 맞는 형태)은 같은 자리에 `dmb sy`가 들어간다.

```text
@ get_acq_rel, 스핀 뒤 (M4/M7 공통)
  ldr   r3, [r0]       ; head
  dmb   sy             ; acquire = load 뒤 dmb
  ...
  ldrb  r0, [r3, r0]
  dmb   sy             ; release = store 앞 dmb
  str   r2, [r1]
```

**Cortex-M0+ 함정.** `_Atomic` 버전(`get_relaxed_fence`)은 `bl __atomic_load_4`로 내려간다(ARMv6-M에 대해 clang이 인라인 lock-free 폭을 0으로 잡는다. `-Watomic-alignment`도 그렇게 경고한다). 인터럽트 핸들러에서 라이브러리 호출을 쓰는 셈이고, 그 함수를 누가 어떻게 구현했는지(인터럽트를 끄나?)에 달린다. 반면 `get_volatile_fence`(volatile 인덱스 + 컴파일러 배리어)는 M0+에서도 `ldr/str`만 나온다. 그래서 M0+라면 volatile + signal fence 조합을 쓴다. `volatile`은 스핀 삭제만 막고, 일반 변수인 칸과의 순서는 **안 묶는다**(demo의 M7 `get_volatile`은 이 코드에서는 바뀌지 않았지만 표준이 금지하지 않는다). 그래서 배리어는 그대로 필요하다.

**`atomic_signal_fence`의 근거와 표준의 한계.** C11 7.17.4.2는 "`atomic_thread_fence`와 같되, 스레드와 그 스레드에서 실행되는 시그널 핸들러 사이에서만 순서를 만든다"이다. 다만 C11은 ISR을 모르고, 7.14.1.1p5는 비동기 시그널 핸들러가 lock-free atomic 아닌 정적 객체를 건드리는 것을 UB로 둔다. 일반 `uint8_t` 칸을 ISR에서 쓰는 것은 엄밀히 표준 밖이고, 실무의 근거는 컴파일러의 동작과 ABI다. 깐깐하게 가려면 칸도 `_Atomic uint8_t` relaxed로 만들 수 있지만(thumbv7em에서 `ldrb/strb` 그대로), 이때 memcpy와 DMA 포인터 전달은 포기한다.

**언제 무엇이 필요한가:**

| 환경 | 태스크↔ISR 인덱스 / 칸 | `dmb` | 캐시 | 메모 |
|---|---|---|---|---|
| Cortex-M0+/M3/M4 단일 코어, SRAM | 컴파일러 배리어 | 불필요 | 없음(보통) | M0+는 `_Atomic` 대신 volatile + fence |
| Cortex-M7 단일 코어, CPU끼리만 쓰는 링 | 컴파일러 배리어 | 불필요 | 캐시는 켜져 있어도 같은 PE 안에서 투명 | 쓰기 버퍼가 깊지만 자기 코어는 자기 쓰기를 본다 |
| M7 + DMA가 버퍼를 읽거나 씀 | 위와 같음 | **DMA 시작 레지스터 쓰기 전에 필요** | **TX: clean, RX: invalidate**, 줄 정렬 | 아래 설명 |
| 듀얼 코어(예: RP2040 두 코어, STM32H7 M7+M4) | acquire/release | **필요** (`dmb` 계열) | 코어 사이 일관성이 없는 부품이면 캐시 끄거나 유지보수 | ISR 안의 순서도 이제 보인다 |
| Cortex-A / Linux SMP | `smp_wmb/rmb`, acquire/release | 필요 | HW 일관성(보통) | kfifo가 `smp_wmb()`를 씀 (DS-B04) |

**M7 + DMA에서 왜 `dmb/dsb`가 필요한가.** DMA와 CPU는 다른 observer다. 단일 코어 규칙이 적용되지 않는다. 버퍼를 다 쓰고 DMA enable 레지스터(Device)를 쓸 때, 버퍼 쓰기가 Normal 메모리로 쓰기 버퍼에 남아 있거나 AXI에서 순서가 바뀌면 DMA가 옛 데이터를 읽을 수 있다. 그래서 `SCB_CleanDCache_by_Addr`(CMSIS, 줄 크기 32 B) 뒤 `__DSB()` 후에 enable을 쓴다. ST의 STM32H7 ETH HAL도 `HAL_ETH_Transmit`에서 descriptor 준비 뒤 tail pointer 쓰기 전에 `__DSB()`를 두고("Ensure completion of descriptor preparation before transmission start"), 수신 descriptor 갱신 뒤에 `__DMB()`를 둔다(`ETH_UpdateDescriptor`, 파일 `stm32h7xx_hal_eth.c`에서 직접 확인).

Arm 측 답변(Joseph Yiu, 참고자료의 커뮤니티 스레드)에 따르면 M3/M4의 쓰기 버퍼는 1칸이고 예외 진입/복귀는 버퍼링된 쓰기가 끝나기를 기다린다. M7은 여러 칸이고 예외 진입/복귀 때 비울 의무가 없다. 그래서 ISR을 끝내기 직전의 **주변장치 레지스터 쓰기**(플래그 클리어)는 별도의 문제다. 이 쓰기가 늦게 도달하면 인터럽트가 다시 들어올 수 있어 `DSB`나 되읽기를 쓰는 관행이 있다. ISR 종료 직전 store와 관련된 Cortex-M 에라타 838869도 있다(대상 코어와 리비전은 Arm 문서로 확인할 것).

### Part B — Bip buffer

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">bip buffer 의 두 상태와 reserve 판단 (cap = 16)</text>
  <text x="10" y="50" fill="currentColor">안 감김: w ≥ r</text>
  <g stroke="currentColor">
    <rect x="130" y="34" width="120" height="30" class="f-mut"/>
    <rect x="250" y="34" width="240" height="30" class="f-acc"/>
    <rect x="490" y="34" width="120" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="134" y="54">빈 곳 (앞)</text>
    <text x="254" y="54">유효 [r, w)</text>
    <text x="494" y="54">빈 곳 (꼬리)</text>
    <text x="244" y="82">r</text><text x="484" y="82">w</text><text x="604" y="82">cap</text><text x="124" y="82">0</text>
  </g>
  <text x="10" y="128" fill="currentColor">감김: w &lt; r</text>
  <g stroke="currentColor">
    <rect x="130" y="112" width="120" height="30" class="f-acc"/>
    <rect x="250" y="112" width="130" height="30" class="f-mut"/>
    <rect x="380" y="112" width="180" height="30" class="f-acc"/>
    <rect x="560" y="112" width="50" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="134" y="132">영역 B [0,w)</text>
    <text x="254" y="132">빈 곳 [w,r)</text>
    <text x="384" y="132">영역 A [r,wm)</text>
    <text x="563" y="132">구멍</text>
    <text x="244" y="160">w</text><text x="374" y="160">r</text><text x="552" y="160">wm</text><text x="604" y="160">cap</text>
  </g>
  <rect x="10" y="182" width="720" height="130" rx="4" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="202">reserve(n):</text>
    <text x="20" y="222">  w &lt; r 이면        n &lt; r - w  → 시작 = w          (엄격: w 가 r 을 만나면 "빈 것" 과 구분 불가)</text>
    <text x="20" y="242">  아니면 cap-w ≥ n  → 시작 = w                   (w == cap 까지 허용)</text>
    <text x="20" y="262">  아니면 n &lt; r      → 시작 = 0, res_wrap         (commit 때 wm = 옛 w 게시 → 구멍 생김)</text>
    <text x="20" y="282">  아니면 NULL                                    공유 상태는 reserve 에서 전혀 안 바뀐다</text>
    <text x="20" y="302">commit: [res_wrap 이면 wm 저장] → w = start + k      read/release: 감김이고 r == wm 이면 r = 0</text>
  </g>
  <text x="10" y="340" fill="currentColor">A 를 다 읽으면 r 이 0 으로 점프해 B 가 새 A 가 된다 → 구멍 [wm, cap) 이 다시 꼬리의 일부가 된다.</text>
  <text x="10" y="362" fill="currentColor">wm 은 감긴 동안에만 뜻이 있다. 안 감긴 상태의 낡은 wm 은 아무도 안 본다 → 지우는 코드가 필요 없다.</text>
  <text x="10" y="384" fill="currentColor">쓰기 주체: r 소비자, w·wm 생산자. r 의 점프는 소비자 혼자 하는 store 한 번 (RMW 없음).</text>
</svg>
```

불변식:

- I1: `0 ≤ r, w ≤ cap`. 감겼으면 `w > 0`이고 `r ≤ wm ≤ cap`.
- I2: 유효 데이터는 안 감김이면 `[r, w)`, 감김이면 `[r, wm) + [0, w)`이고 FIFO 순서다.
- I3: 열린 예약은 유효 데이터와 겹치지 않는다. 읽고 있는 덩어리도 예약과 겹치지 않는다.
- I4: 감긴 상태에서 w는 r에 닿지 않는다(`w < r` 엄격). 안 감김에서 `w == r`은 "비었다"이다.

**reserve가 공유 상태를 바꾸지 않는 이유.** 예약은 생산자 혼자 아는 사실이다. 소비자는 commit된 것만 본다. 그래서 reserve 뒤에 취소해도(`commit(0)`) 상태는 그대로이고, DMA에 긴 시간 맡겨 놓는 것도 된다. 감긴 예약이라도 `wm`은 게시 때에야 저장한다. 시나리오 S8이 이것을 잡는다(`bs(8, 14, 99)`에서 reserve+cancel 후 wm이 여전히 99).

**commit 순서 (`publish` 내부): wm → w.** 소비자가 `w < r`을 봤을 때만 wm을 믿는다. 그러니 wm이 먼저 있어야 한다. 나쁜 변형 `bad_commit_w_first`(w 먼저)는 ISR 소비자가 그 사이에 끼면 낡은 wm으로 엉뚱한 길이를 읽는다. 이 하네스에서는 `w 먼저 게시 361` run이 깨졌다. 반대로 소비자의 `w 먼저 읽기`(wm은 그다음)도 필수다(`wm 먼저 읽기 160`). 생산자는 wm → w 순서로 쓰고 소비자는 w → wm 순서로 읽는다. 게시와 관찰의 고전적 순서다.

**r 점프가 두 군데인 이유.** release에서 r이 wm에 닿는 순간 곧장 `r = 0`을 저장하면 구멍을 일찍 돌려줄 수 있다(빠른 점프). read에서도 검사하는 것은 소비자가 끝까지 비운 뒤에 생산자가 감기는 경우(`r == w == cap`, S12)가 있기 때문이다. 그때 소비자 쪽 release는 이미 지나갔고 r에는 할 일이 없다. read에서 `r == wm`을 보고 나서야 r을 옮긴다. 빠른 점프만 두면 S12에서 소비자가 데이터를 영영 못 찾는다(read의 점프를 지운 뮤턴트는 이 하네스에서 체크 14개가 FAIL). 늦은 점프만 두면 구멍이 소비자의 다음 read까지 묶인다(S10/S13이 빠른 점프를 잡는다).

**왜 안전한가: 낡은 r.** 생산자가 읽은 r은 낡을 수 있다(그 사이 소비자가 진행). 두 가지 경우를 본다.

1. 낡은 r이 "진행 전" 값이면 빈 공간을 **더 작게** 잡아 스퓨리어스 실패뿐이다. 데이터를 덮지는 않는다.
2. 감긴 상태에서 r이 wm까지 가서 0으로 점프했는데 생산자는 큰 r을 들고 있다면, `[w, w+n) ⊂ [w, r_낡음)`에 예약한다. 실제 유효 데이터는 `[0, w)`이니 겹치지 않는다. commit 후 `w+n`까지 그대로 정상이다(S15).

r이 줄어드는 유일한 경우는 점프뿐이고, 점프는 감김에서만 일어난다. 생산자는 w의 주인이므로 감김 여부를 자기 w와 r로 정확히 판단한다. 스테일(stale) r 때문에 안 감김과 감김을 착각하는 일은 없다. 전수 탐색(아래)의 C/D 전이가 이 조합을 전부 돈다.

**랩 시나리오 추적** (cap=16, 대표):

| 시나리오 | 시작 (r, w, wm) | 연산 | 결과 |
|---|---|---|---|
| S3 꼬리 안 맞음 | (8, 14, 0) | reserve(4) → commit(4) | 포인터 0, reserve 중엔 상태 불변. commit 후 (8, 4, 14), 구멍 [14,16) |
| S4 앞쪽도 안 됨 | (3, 15, 0) | reserve(3) | n == r → NULL (`n < r` 엄격) |
| S11 구멍 안 | (8, 4, 14) | reserve(4) / reserve(3) | w+4 == r → NULL / 포인터 4 |
| S10 영역 A → B | (8, 4, 14) | read(6) → release(6) | r 이 14(=wm) → 곧장 0, (0, 4, 14). 다음 read 는 [0, 4) |
| S12 늦은 점프 | (16, 16, 0) | reserve(3) 커밋 → read | wm=16, r=16 == wm → read 가 r=0 저장 후 [0, 3) 반환 |
| S14 구멍 회수 | (0, 6, 14) | reserve(10) | 낡은 wm=14 는 무시, 포인터 6, w=16 |
| S15 예약 중 점프 | (8, 4, 14), 예약 [4,7) | 소비자 전부 소비 → commit(3) | r=0 으로 점프 후 commit, (0, 7, 14) |

**전수 탐색.** `bfs_explore(cap)`은 (0,0,0)에서 출발해 도달 가능한 모든 `(r, w, wm)`을 찾는다. 상태마다 소비자만 / 생산자만 / 예약이 열린 채 소비 / 읽기가 열린 채 생산의 모든 인자 조합을 돌리고, 다음을 확인한다: reserve 성공 여부와 시작 위치가 명세와 일치, 읽은 바이트가 진짜 FIFO 내용과 일치, 불변식, 그리고 "데이터가 있으면 read는 절대 NULL이 아님". 실제 출력:

```text
  cap=8: 도달 상태 285, 전이 13388, 위반 0
  cap=9: 도달 상태 361, 전이 22038, 위반 0
```

**ISR 발사 위치 전수.** 위에서 찾은 cap=8의 285개 상태 **각각**을 시작점으로, 각 방향으로 `task 연산`의 **모든 접근 앞**에서 ISR을 한 번씩 발사했다.

```text
-- ISR 발사 위치 전수 (cap=8, 도달 가능한 모든 상태)
  상태 285개: RX 26312 runs 위반 0, TX 21640 runs 위반 0
  나쁜 변형 위반 run 수: w 먼저 게시 361, wm 먼저 읽기 160
-- 난수 스트레스 (cap=97)
  60만 단독: 위반 0 (랩 33876) · 20만 ISR 생산자: 위반 0 (랩 32391) · 20만 ISR 소비자: 위반 0 (랩 20218)
```

**공간 보장 정리.** "reserve 크기는 항상 M 이하이고, reserve 호출 시점의 유효 데이터는 D 이하"이면, `N ≥ D + 2M`일 때 `reserve(M)`은 절대 실패하지 않는다. 그리고 `N = D + 2M - 1`이면 실패하는 실행이 있다.

증명(충분성). 안 감긴 상태: 빈 곳은 꼬리 T = N - w와 앞쪽 r이고 T + r = N - D' (D' ≤ D). T ≥ M이면 성공. T ≤ M - 1이면 r = N - D' - T ≥ N - D - (M-1) ≥ M + 1이므로 `M < r` 성공. 감긴 상태: 구멍 길이 h = N - wm은, 그 구멍을 만든 reserve의 크기를 n ≤ M이라 할 때 꼬리가 모자랐다는 뜻이므로 h ≤ M - 1이다. 그러면 r - w = N - h - D' ≥ N - (M - 1) - D ≥ M + 1이므로 `M < r - w` 성공. ∎

필요성은 작은 경우를 전수로 확인했다(`guarantee_holds(N-1, D, M)` 이 D∈0..4, M∈1..4 범위에서 모두 실패).

```text
  보장 정리: 20 조합, N = D + 2M 충분 yes, N - 1 이면 실패 yes
```

이게 설계 답이다. **연속 할당의 비용은 공간이다.** 그냥 링(쪼개기 허용)은 `N ≥ D + M`이면 되는데 bip는 `N ≥ D + 2M`이다. 최악의 경우 구멍 크기만큼(M - 1) 더 필요하기 때문이다. 메시지 최대 크기가 512 B이고 최대 400 B를 쌓아 두면 링은 912 B, bip는 1424 B이어야 "실패 없음"을 보장한다.

**demo_waste.c — 실제로 얼마나 차이 나나** (결정적, 200k 메시지). 부하는 메시지 크기 [1, M] 균등, 예약 전 live ≤ D 로 맞춘다. 실제 출력:

```text
| N | D | M | D+M (링 보장선) | D+2M (bip 보장선) | bip 실패 | 링 실패 | 구멍 B/메시지 |
|---|---|---|---|---|---|---|---|
|  256 |  64 |  96 |  160 |  256 |   0.00% |   0.00% |   6.81 |
|  256 |  64 |  97 |  161 |  258 |   0.00% |   0.00% |   6.99 |
|  256 |  64 | 128 |  192 |  320 |   1.25% |   0.00% |  11.51 |
|  256 |  64 | 180 |  244 |  424 |  13.21% |   0.00% |  16.56 |
|  256 | 128 |  64 |  192 |  256 |   0.00% |   0.00% |   2.94 |
|  256 | 128 | 126 |  254 |  380 |  12.19% |   0.00% |   7.34 |
|  256 | 128 | 200 |  328 |  528 |  40.36% |  14.51% |  10.23 |
| 1024 | 512 |  64 |  576 |  640 |   0.00% |   0.00% |   0.68 |
| 1024 | 512 | 256 |  768 | 1024 |   0.00% |   0.00% |  11.64 |
```

읽는 법: `D+2M ≤ N`인 줄은 모두 0%다(정리대로). 둘째 줄(D+2M = 258 > 256)도 0%인데, 보장선 아래라는 것은 실패가 **있을 수 있다**는 뜻이지 난다는 뜻이 아니다. 정리는 최악의 입력 순서에 대한 것이고 균등 난수는 그 순서를 잘 못 찾는다. D+2M이 N을 크게 넘으면 bip만 실패하고(M=180에서 13.21%) 링은 D+M ≤ N인 한 0%이다. D+M > N인 줄(128+200)은 링도 실패한다. 평균 구멍 크기는 메시지당 몇 바이트로 작다.

**다른 구현과 비교.** Cooke의 원 글은 A/B 영역 descriptor를 저장하고 `Reserve/Commit/GetContiguousBlock/DecommitBlock` API를 쓰며 "A를 다 읽으면 B가 새 A가 된다"는 동작을 설명한다. Reserve는 요청 크기 이하에서 가능한 최대 블록을 돌려주는 정책이다. 이 레슨은 Lattuada·Munns(2019)의 3인덱스(read, write, watermark) 형태를 따른다. 공유 상태가 3개의 워드뿐이라 ISR 안전하게 만들기 쉽기 때문이다. Munns의 bbqueue(v0.5.1, `core/src/bbbuffer.rs`)도 같은 구조로, `last`를 wrap 때 저장하고 `write`를 그다음 저장하며 "Write must be updated AFTER last"라고 적어 둔다. 감김 쪽 `(write + sz) < read`, 앞쪽 감기 `sz < read` 비교도 엄격하다. 이 레슨은 "감길 때마다 항상 wm을 저장"하고 안 감김에서는 wm을 안 믿으므로 bbqueue의 `else if new_write > last` 해제 분기가 필요 없다.

**실제 코어 둘로 돌리면.** `demo_smp.c`는 같은 알고리즘을 진짜 두 스레드에서 돌린다. MODE=0은 단일 코어 ISR 코드 그대로(relaxed + signal fence), MODE=1은 acquire/release.

<!-- code: demo_smp.c -->

```text
$ ./ds1   (3회 모두 같음, 약 1.1 s)
MODE=1 20000000 messages: bad 0
$ ./ds0   (5회 모두 1000개 어긋남에서 조기 중단)
  (첫 1000 개 어긋남에서 중단, expect=334797)
MODE=0 messages checked until stop: bad 1000
  (첫 1000 개 어긋남에서 중단, expect=4296656)
MODE=0 messages checked until stop: bad 1000
...
```

Apple M6(12코어, 측정 시 load average 8.88)에서 컴파일러 배리어만 쓰면 5/5번 깨졌다. acquire/release는 2천만 메시지 3회 모두 깨끗하다. TSan도 MODE=1(20만 메시지)은 무경고, MODE=0은 `consumer`의 칸 읽기 줄에서 data race를 보고했다(`demo_smp.c:89`, `:91`). 왜 깨지는지(store 재배치 등)는 DS-A01의 영역이고 여기서 다시 재지 않았다. **요점: 단일 코어용 코드를 그대로 다른 코어로 옮기면 실제로 깨진다.** 매크로 한 줄(`ACQ_BARRIER`/`REL_BARRIER`)이 그 차이다.

### Part C — UART DMA

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">UART RX DMA: 예약 → DMA 가 직접 씀 → IDLE/TC 에서 publish → 태스크 read/release</text>
  <rect x="30" y="40" width="680" height="36" class="f-mut" stroke="currentColor"/>
  <rect x="30" y="40" width="130" height="36" class="f-acc" stroke="currentColor"/>
  <rect x="160" y="40" width="100" height="36" class="f-acc" stroke="currentColor"/>
  <rect x="260" y="40" width="160" height="36" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="36" y="63">유효 (read 가능)</text>
    <text x="166" y="63">유효 (방금)</text>
    <text x="266" y="63">DMA 진행 (예약)</text>
    <text x="430" y="63">빈 곳</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="96">r</text><text x="256" y="96">w</text><text x="414" y="96">res 끝</text>
  </g>
  <path d="M340 140 L340 80" stroke="currentColor" fill="none"/>
  <path d="M335 90 L340 80 L345 90" stroke="currentColor" fill="none"/>
  <text x="240" y="158" fill="currentColor" font-family="monospace">DMA: *mar++ = byte; ndtr--</text>
  <g stroke="currentColor">
    <rect x="30" y="180" width="200" height="56" class="f-mut"/>
    <rect x="270" y="180" width="200" height="56" class="f-acc"/>
    <rect x="510" y="180" width="200" height="56" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="40" y="202">IDLE / TC ISR</text>
    <text x="40" y="222" font-family="monospace" font-size="12">publish(got - seen)</text>
    <text x="280" y="202">태스크</text>
    <text x="280" y="222" font-family="monospace" font-size="12">read → 복사 → release</text>
    <text x="520" y="202">TC 이고 영역 부족</text>
    <text x="520" y="222" font-family="monospace" font-size="12">armed=0 → 태스크 재무장</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="262">DMA 가 쓰는 동안 그 영역은 "예약"이다: 소비자에게 안 보인다.  IDLE 은 예약을 닫지 않고 앞부분만 공개한다.</text>
    <text x="10" y="284">TX: reserve(len) → 복사 → commit → kick. 전송 단위는 "연속 덩어리" 이고 메시지가 쪼개지지 않는다.</text>
    <text x="10" y="306">재무장 규칙: reserve 는 생산자 작업. ISR 과 태스크가 둘 다 호출하므로 태스크 쪽은 IRQ_OFF 구간에서만.</text>
    <text x="10" y="328">DMA 길이 상한(chunk)이 없으면 빈 곳을 전부 예약해 TC 때 다음 영역이 없다 → 줄에서 바이트를 잃는다.</text>
  </g>
</svg>
```

**RX 흐름.** `uart_rx_arm`이 `bip_reserve_max(b, 4, chunk, &n)`로 받은 영역에 DMA를 건다. DMA가 쓰는 동안 ISR은 IDLE/TC마다 `got = cur_len - ndtr`를 구해 **새로 들어온 바이트만** `bip_publish`한다. 여기서 핵심은 예약을 닫지 않는 것이다(`publish`). DMA를 멈추지 않으니 다음 바이트가 끊기지 않는다. 그래서 commit과 publish를 분리했다. 감긴 예약이면 첫 publish 때만 wm이 저장된다(`S18`). `ndtr` 값은 읽자마자 낡는다. 하네스의 `g_dma_race`는 일부러 ISR이 `ndtr`을 읽은 직후에 바이트를 하나 더 흘린다. 그 바이트는 다음 인터럽트에서 공개되면 그만이고, TC가 그 자리에서 나도 다음 인터럽트에 처리된다.

**유실의 원인.** `demo_rx_chunk.c`는 `solution.c`를 그대로 include해서 DMA 길이 상한만 바꿔 돌린다(시드 40개 합계, 결정적).

<!-- code: demo_rx_chunk.c -->

```text
UART_RX_CHUNK=0  (상한 없음)    6%   12%   18%   24%   30%   ← ISR 확률/접근
  유실 바이트                    96   260   601   873  1206
UART_RX_CHUNK=8                   0     0     0     0     0
UART_RX_CHUNK=16                  0     0     0     0     0
UART_RX_CHUNK=32                  0     0     0     0     0
```

상한이 없으면(`0`) 빈 곳 전체를 한 번에 예약한다. 그러면 TC가 났을 때 다음 예약을 받을 영역이 남아 있지 않다. 디버그 빌드에서 유실 순간의 상태를 찍어 보니 유실은 전부 `armed=0`일 때 일어났고 상태는 `r=0 w=63 wm=64`처럼 소비자가 아직 안 비운 자리까지 예약이 차 있었다(꼬리 1바이트 < min, 앞쪽 0). DMA가 멈춘 동안 들어온 바이트를 잃는 것이다. 상한이 있으면 TC 뒤에 다음 영역이 남는다. 기본값은 16이다. 이 표는 모델 안의 측정값이다. 실물 STM32의 double-buffer 모드나 수신 FIFO는 이 모델에 없다.

**유실의 정의.** UART 수신 레지스터 1바이트까지 모델링했다. DMA가 꺼져 있어도 한 바이트는 기다리고, 그 위에 또 오면 ORE(overrun)로 사라진다. 태스크가 놀면 정확히 그 동작이다.

**TX 흐름.** `uart_tx_send`는 reserve → 복사 → commit → `uart_tx_kick`. kick은 `busy == 0`일 때만 `bip_read`해서 DMA를 건다. TC ISR은 보낸 덩어리를 `bip_release`하고 다음 덩어리를 읽어 곧바로 건다. 없으면 `busy = 0`. `busy`는 "지금 소비자가 누구인가"의 바통이다. `busy == 0`이면 DMA가 없으니 TC가 오지 않고, 태스크가 소비자 자격으로 `bip_read`를 해도 ISR과 겹치지 않는다.

**lost wakeup 분석.** 코드가 `commit → busy 확인` 순서인 이유.

| 순서 | 스케줄 | 결과 |
|---|---|---|
| **commit → busy 확인** | (A) commit, (B) busy 읽음 / ISR: (C) read 비었음, (D) busy=0 | ISR은 C, D를 **한 번에** 실행한다. C가 A 앞이면 D도 A 앞이므로 B는 0을 본다 → kick 성공. C가 A 뒤면 ISR이 데이터를 봤으니 DMA를 건다 |
| busy 확인 → commit | (B) busy=1 읽음 → ISR (C, D) → (A) commit | commit된 데이터를 아무도 보내지 않는다 → **영구 정체** |

뒤의 순서는 변형 `bad_send_kick_first`로 확인했다.

```text
  TX 25%/접근, 큰 burst: 위반 0, 남은 바이트 0
  TX 매 접근 발사 3바이트씩: 위반 0, 남은 바이트 0, DMA 전송 300회
  TX 주기 발사 45 조합: 위반 0
  commit 전에 kick 하는 변형: 데이터가 묶인 k 개수 572 / 600
```

**코어가 둘이면?** "ISR은 C, D를 한 번에 실행"이 사라진다. commit(store) → busy load와 busy store → bip_read(load)는 Dekker 패턴이고, 둘 다 store→load 순서가 필요하다. acquire/release로는 부족하다(그 구간에 full 배리어나 seq_cst가 필요하다). 단일 코어 ISR에서만 공짜인 추론이다.

전체 출력(Part C, solution.c):

```text
== Part C: UART DMA
  RX 12%/접근, IDLE 5%: 위반 0, 유실 0
  RX 매 접근 발사 2바이트씩: 위반 0, 유실 3577
  RX 주기 발사 28 조합: 위반 0
```

"매 접근 발사 2바이트"는 태스크가 줄을 절대 못 따라가는 설정이라 유실이 나오지만, 받은 바이트는 전부 DMA가 쓴 순서와 같고(체크) 유효 데이터가 덮이지 않는다(`viol == 0`).

**복잡도와 결정성.** reserve/publish/commit/read/release 전부 루프 없이 상수 개의 인덱스 접근으로 끝난다(SPSC에서 wait-free). 상태 3워드 + 예약/읽기 비공개 상태 몇 개가 메모리 오버헤드의 전부다. 이 레슨은 타이밍을 재지 않는다. solution.c 전체가 `-O2`에서 약 0.15초, ASan/UBSan에서 약 0.43초(100만 연산을 줄이지 않았다).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

전체 실행 결과는 102/102 passed다. 위 §5의 출력이 전부 solution.c의 실제 출력이다.

## 7. 자주 하는 실수

1. **`volatile`만 붙이고 끝.** `volatile` 인덱스는 스핀 삭제만 막는다. 일반 변수인 칸과의 순서는 안 묶인다(M7 get_plain의 `str`→`ldrb`). → 칸 접근 양쪽에 컴파일러 배리어, 또는 멀티코어면 acquire/release.
2. **ISR에도 태스크에도 같은 배리어를 "혹시 몰라서" 넣고 이유를 모름.** 면접 답은 "태스크 쪽 [B1], [B2] (RX), [B3] (TX)가 일을 한다. ISR 쪽은 소비자가 ISR을 못 끊을 때 무해"이다. → §5 줄 단위 표로 답한다.
3. **`w + n == r`을 허용.** 감긴 상태에서 w가 r을 만나면 "비었다"로 보여서 데이터가 전부 사라진다. → `n < r - w`, 앞쪽 감기도 `n < r`. 꼬리는 `n ≤ cap - w`.
4. **wm을 w 뒤에 저장.** 감긴 w를 본 소비자가 낡은 wm을 믿는다(실측 361 run). → wm 먼저.
5. **소비자에서 wm을 w보다 먼저 읽기.** 그 사이 ISR이 감기면 낡은 wm으로 잘못 계산(실측 160 run). → w 먼저.
6. **release에서만 r 점프.** `r == w == cap` 뒤에 감긴 경우 r을 옮길 사람이 없어 데이터가 안 나온다. → read에도 점프.
7. **DMA에 빈 곳 전부 예약.** TC 때 다음 영역이 없어 줄에서 바이트를 잃는다(chunk 없음 96~1206바이트 유실, 상한 8 이상 0). → `UART_RX_CHUNK` 상한.
8. **태스크가 다시 `uart_rx_arm`을 부를 때 인터럽트 그대로.** reserve는 생산자 작업이라 ISR의 재무장과 겹치면 생산자가 둘이 된다. → 짧은 IRQ_OFF.
9. **`busy` 확인을 commit 앞에.** ISR이 사이에서 "비었네, busy=0" 하면 데이터가 묶인다. → commit 뒤에 확인, SMP면 full 배리어.
10. **M7 + DMA 버퍼를 캐시 줄에 안 맞춤.** 무효화가 같은 줄의 이웃 바이트를 버린다(연습 2c). → 버퍼 시작/크기를 32 B 단위로, 또는 비캐시 영역.

## 8. 면접 Q&A

<details><summary>Q1. 단일 코어 Cortex-M4에서 UART ISR 생산자, main 소비자 링에 배리어가 필요한가?</summary>

`dmb`는 필요 없다. 같은 PE가 자기 Normal 메모리 접근을 프로그램 순서대로 관찰하기 때문이다(문서 규칙의 요지, 절 번호 미확인). 그러나 **컴파일러 배리어는 태스크 쪽에 필요하다**. 칸 읽기가 head 읽기 앞으로, tail 저장이 칸 읽기 앞으로 가면 깨진다. 실제 `clang -mcpu=cortex-m7` 출력에서 일반 변수 코드의 `str tail`이 `ldrb buf` 앞에 있었다. `volatile`만으로는 부족하다. relaxed + `atomic_signal_fence`, 또는 volatile + `__asm__("" ::: "memory")`이 답이고 명령은 0개다(`atomic_signal_fence`의 출력 `@MEMBARRIER`로 확인). ISR 쪽 순서는 태스크가 볼 수 없으므로 뒤집어도 깨지지 않는다(체크 `ISR 쪽 head 먼저 0`). 단 소비자가 그 ISR보다 높은 우선순위이면 얘기가 달라진다.
</details>

<details><summary>Q2. 코어가 둘이거나 M7이면 무엇이 달라지나?</summary>

**듀얼 코어**: "ISR은 끊기지 않는다"가 사라진다. 양쪽 모두 acquire/release(또는 `dmb`)가 필요하고 thumbv7em에서 `dmb sy`로 나온다(실제 `-S`). 같은 코드를 relaxed만으로 Apple M6 두 코어에서 돌리면 5/5번 깨졌다(`demo_smp.c`). **단일 M7, CPU끼리만**: 쓰기 버퍼가 깊고 스케줄링이 공격적이어서 컴파일러 배리어가 더 중요하지만 `dmb`는 불필요하다. **M7 + DMA**: 다른 observer이므로 TX 버퍼 clean 뒤 DSB 후 enable, RX 버퍼 invalidate, 32 B 줄 정렬이 필요하다. 코어 간 캐시 일관성이 없는 듀얼 코어 부품이면 공유 버퍼를 비캐시 영역(MPU)에 두거나 양쪽에서 유지보수한다.
</details>

<details><summary>Q3. bip buffer의 reserve/commit 구조가 일반 링보다 나은 점과 대가는?</summary>

나은 점: 항상 **연속** 포인터를 준다. DMA 시작 주소와 길이 하나로 끝나고(scatter-gather 불필요), 레코드를 그대로 캐스팅하고, memcpy가 2번 나뉘지 않는다. 대가: 공간이다. 정확히는 "reserve ≤ M, 예약 시점 live ≤ D이면 N ≥ D + 2M이어야 reserve가 안 실패" (전수로 필요충분 확인). 링은 D + M이면 되니 최악의 경우 M 바이트를 더 쓴다. 평소의 평균 낭비는 구멍 수 바이트로 작았다(demo_waste: 메시지당 0.68~16.56 B). 소비자가 최대 2덩어리로 읽어야 한다는 점도 있다. 대신 각 덩어리는 연속이다.
</details>

<details><summary>Q4. wm이 없어도 만들 수 있나? wm을 언제 지우나?</summary>

wm 대신 Cooke식 A/B 영역 descriptor(시작과 끝 4개 + 예약)를 저장할 수 있는데, 공유 워드가 늘고 ISR-안전하게 만드는 일이 어려워진다. 3인덱스 방식에서 wm은 지우지 않는다. 감긴 동안에만 뜻이 있고, 감길 때마다 항상 새로 쓰기 때문이다. bbqueue는 `write != max`일 때만 last를 쓰기 때문에 passing 시 해제 분기가 필요한데, 이 레슨은 "항상 저장" 쪽을 골랐다. r 점프 뒤의 낡은 wm(S14)은 안 감김 상태라 아무도 안 본다.
</details>

<details><summary>Q5. r이 0으로 "돌아가는" 게 안전한가? 생산자가 r을 읽는 도중이면?</summary>

r은 소비자만 쓴다. 점프는 단일 store 한 번이고 32비트 정렬 쓰기는 찢어지지 않는다. 생산자가 점프 전 큰 r을 쓰더라도, 그 r로 잡는 예약은 `[w, r_낡음)` 안이고 실제 유효 데이터 `[0, w)`와 겹치지 않는다(S15, 전수 탐색 C 전이). 점프는 감김 상태에서만 일어나고 생산자가 wrap 예약을 잡는 것은 안 감김 상태에서뿐이므로, wrap 예약이 r 점프와 부딪힐 수 없다. 멀티코어라면 생산자의 r load가 acquire이어야 소비자가 다 읽은 뒤를 관찰한다.
</details>

<details><summary>Q6. DMA가 예약 영역에 쓰는 도중 IDLE이 나면 왜 commit이 아니라 publish인가?</summary>

commit은 예약을 끝낸다. DMA는 계속 뒤쪽 바이트를 쓰고 있으므로 예약은 살아 있어야 한다(유효 데이터로 공개되지 않고, 다른 곳에서 reserve도 못 한다). `publish(got - seen)`은 예약의 앞부분만 공개하고 나머지는 유지한다. 감긴 예약이면 첫 publish에서만 wm을 쓴다. 그래야 DMA를 멈추지 않고 "수신 중인 짧은 프레임을 지금 당장 읽기"가 된다.
</details>

<details><summary>Q7. TX에서 `commit → kick`이 lost wakeup을 막는 이유, 그리고 코어가 둘이면?</summary>

ISR이 "read 비었음 → busy=0"을 한 덩어리로 실행하기 때문이다. 태스크의 commit보다 ISR의 read가 먼저면 busy=0도 먼저 끝나서, 뒤따르는 태스크의 busy 확인이 0을 본다. 그래서 어느 쪽이든 누군가는 전송을 건다. busy 확인을 commit 앞에 두면 그 사이 ISR이 끝나 데이터가 묶인다(하네스에서 600개 k 중 572개에서 재현). 코어가 둘이면 두 쪽이 동시에 돈다. store→load 순서(commit → busy 읽기, busy 쓰기 → bip_read)가 필요한 Dekker 형태이고, acquire/release로는 보장되지 않아 full 배리어가 필요하다.
</details>

<details><summary>Q8. 이 코드를 NVIC 우선순위가 다른 ISR 두 개가 함께 쓰면?</summary>

생산자가 둘이 된다. 낮은 ISR의 `put` 도중 높은 ISR이 끼어들면 같은 head를 읽어 같은 칸에 쓰고 하나가 사라진다(연습 1: k=1,2에서 재현). 해법은 ① 우선순위별 링 분리(N개 SPSC 팬인), ② 낮은 쪽에서만 BASEPRI로 그 구간을 막기, ③ 칸별 시퀀스를 쓰는 Vyukov 큐(DS-A03)다. 펌웨어에서는 ①이 흔하다.
</details>

## 9. 추가 연습

### 연습 1 — 우선순위가 다른 ISR 두 개

낮은 우선순위 ISR이 `put`하는 도중 k번째 접근 앞에서 높은 ISR이 끼어들어 같은 링에 `put`한다. 어떤 k에서 무슨 일이 생기는가? 낮은 ISR 쪽 임계 구역으로 고쳐라.

<details><summary>답</summary>

실제 출력은 `k=0: count 2, AA 1, BB 1`, `k=1: count 1, AA 1, BB 0`, `k=2: count 1, AA 0, BB 1`이다. k=0은 낮은 ISR이 시작하기 전이라 정상이다. k=1, 2는 둘 다 같은 head를 쓰므로 바이트 하나가 사라진다. `put_locked`는 `g_p.masked`로 구간을 막고 pending을 해제 시 실행한다(Cortex-M의 BASEPRI 사용과 같은 모양). "ISR은 끊기지 않는다"는 말이 항상 "우선순위가 같거나 낮은 쪽에게"에 대한 말이라는 점을 놓치면 이런 버그를 만든다.
</details>

### 연습 2 — Cortex-M7 D-cache 모델

32 B 줄의 write-back 캐시를 모델링한다(CPU는 캐시를, DMA는 RAM을 본다). (a) TX: clean 전후 DMA가 읽는 값 (b) RX: invalidate 전후 CPU가 읽는 값 (c) 정렬 안 된 RX 예약을 invalidate하면 같은 줄의 이웃 바이트는? (d) 줄 정렬로 고쳐라.

<details><summary>답</summary>

(a) clean 전 DMA는 0을, clean 후 정상 값을 읽는다. (b) invalidate 전 CPU는 낡은 캐시 줄을 본다. (c) `cache_invalidate(72, 10)`은 줄 2(64..95) 전체를 버려서 70번지에 CPU가 쓴 바이트가 사라진다(`cpu_read(70) != 0x77`). (d) 예약 시작과 길이를 줄 단위로 올린다: 시작 96, 길이 32. 그 안의 데이터만 무효화된다. 실제로는 bip의 reserve 크기와 commit 단위를 줄 크기로 맞추면 예약 경계가 항상 줄 경계가 되어 이 문제가 사라진다(대신 내부 단편화를 받아들인다). CMSIS의 `SCB_InvalidateDCache_by_Addr`도 범위를 줄 단위로 처리한다. `dsb`와 호출 순서는 Part A 표를 따른다.
</details>

### 연습 3 — 프레임 레코드

bip 위에 `[u16 total, u16 type][payload]` 레코드를 얹는다. total은 4의 배수. 30만 연산에서 페이로드가 항상 4바이트 정렬이고 레코드가 쪼개지지 않음을 확인하라.

<details><summary>답</summary>

`rec_reserve`는 `ALIGN_UP(4 + payload)`로 reserve한다. 4의 배수만 예약하고 cap도 4의 배수이므로 w와 wm이 항상 4의 배수다. 시작 위치가 언제나 4바이트 정렬이라 `(rec_hdr_t *)` 캐스팅이 안전하다. 실제 출력은 `30만 연산: 랩 16695, 위반 0, 정렬 어긋남 0`이다. DS-M10의 랩어라운드 복사가 사라진 자리에 bip의 `read` 한 번이 있다. 레코드를 `const` 포인터로 그대로 다른 코드에 빌려줄 수 있다.
</details>

### 연습 4 — 최소 용량

(D, M)마다 `reserve(M)`이 항상 성공하는 최소 cap을 전수로 찾아 D + 2M과 비교하라.

<details><summary>답</summary>

`cap_ok`가 reachable 상태를 전수로 돌며 `reserve(M)` 실패가 하나라도 나오면 false다. cap을 올려 가며 처음 true가 되는 값을 찾으면 D∈0..5, M∈1..5의 30개 조합에서 공식과 다른 것이 없었다(`공식과 다른 것 0`). 이 방법은 "수식을 믿기 전에 작은 N을 전수로 돌려 본다"는 펌웨어 테스트 습관의 예다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice        # 13/13 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o p_san && ./p_san
```

관찰용 파일(체크와 별개):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -mthumb -std=c11 -O2 -ffreestanding -S demo_asm.c -o -
for c in 0 8 16 32; do cc -std=c11 -O2 -DUART_RX_CHUNK=$c demo_rx_chunk.c -o rc && ./rc $c; done
cc -std=c11 -O2 demo_waste.c -o dw && ./dw
cc -std=c11 -O2 -pthread -DMODE=1 demo_smp.c -o ds1 && ./ds1
cc -std=c11 -O2 -pthread -DMODE=0 demo_smp.c -o ds0 && ./ds0
cc -std=c11 -g -O1 -fsanitize=thread -pthread -DMODE=1 -DMSGS=200000u demo_smp.c -o dst && ./dst
```

## 10. 핵심 정리

- 단일 코어 ISR: `dmb` 불필요, **컴파일러 배리어 필요**. 일을 하는 쪽은 끊기는 쪽(태스크)이다. RX는 소비자의 [B1][B2], TX는 생산자의 [B3]. ISR 안의 순서는 단일 코어에서 관찰되지 않는다.
- `volatile`은 스핀 삭제만 막는다. 일반 칸과의 순서는 안 묶는다. M7 스케줄링은 실제로 `tail` 저장을 칸 읽기 앞으로 옮겼다. M0+에서 `_Atomic`은 `__atomic_*` 호출이니 volatile + fence를 쓴다.
- M7 + DMA는 메모리 순서가 아니라 캐시 + 도어벨 문제다: TX clean + DSB 후 enable, RX invalidate, 32 B 정렬.
- bip 상태: 안 감김 `[r,w)`, 감김 `[r,wm)+[0,w)`. w는 감긴 동안 r을 만나지 않는다(엄격 비교). reserve는 공유 상태를 안 바꾼다.
- 게시는 wm → w, 관찰은 w → wm. r 점프는 release(빠름)와 read(`r==w==cap` 뒤) 양쪽에 있다.
- 공간 보장: 예약 ≤ M, live ≤ D이면 **N ≥ D + 2M**. 링은 D + M. 연속의 대가는 M 바이트.
- DMA: 예약에 직접 쓰고 IDLE/TC에서 publish, 예약 길이에 상한을 둔다(chunk 없으면 96~1206 바이트 유실, 상한 있으면 0).
- TX `commit → busy 확인`은 단일 코어에서 lost wakeup을 막는다. 코어가 둘이면 store→load 순서가 필요한 Dekker 패턴이라 full 배리어가 든다.

## 참고자료

- Simon Cooke, "The Bip Buffer — The Circular Buffer with a Twist" (CodeProject) — 영역 A/B, `Reserve/Commit/GetContiguousBlock/DecommitBlock`. https://www.codeproject.com/Articles/3479
- Andrea Lattuada, James Munns, "The design and implementation of a lock-free ring-buffer with contiguous reservations" (Ferrous Systems, 2019) — read/write/watermark. https://ferrous-systems.com/blog/lock-free-ring-buffer/
- James Munns, bbqueue — `core/src/bbbuffer.rs` (v0.5.1 확인): `last` 저장 순서, 엄격 비교, read의 `(read == last) && (write < read)`
- ISO/IEC 9899:2011 §7.17.4.2 `atomic_signal_fence`, §7.14.1.1p5 비동기 핸들러 제약, §5.1.2.4 데이터 레이스
- Arm 커뮤니티 "DMB/DSB/ISB on Cortex-M3/M4/M7 single core parts" — Joseph Yiu 답변: M3/M4 쓰기 버퍼 1칸과 예외 진입/복귀 대기, M7 다중 칸. https://community.arm.com/developer/ip-products/processors/f/cortex-m-forum/10389/dmb-dsb-isb-on-cortex-m3-m4-m7-single-core-parts/33102
- ST `stm32h7xx_hal_eth.c` — `HAL_ETH_Transmit`의 `__DSB()`(descriptor 준비 후 전송 시작 전), `ETH_UpdateDescriptor`의 `__DMB()`. https://github.com/STMicroelectronics/stm32h7xx_hal_driver
- Arm AN321 (Cortex-M Programming Guide to Memory Barrier Instructions), Arm erratum 838869 — 이번에 원문을 직접 확인하지 못했다. 세부는 원문으로 확인할 것
- 관련 레슨: DS-B04 (자유 증가 카운터), DS-M10 (패딩 vs 분할), DS-A01 (SMP SPSC, acquire/release), DS-A03 (Vyukov MPMC), DS-A04 (ISR 안전 풀, LL/SC), RUST-M07 (Rust SPSC split), C-M14 (ISR 공유 데이터)
