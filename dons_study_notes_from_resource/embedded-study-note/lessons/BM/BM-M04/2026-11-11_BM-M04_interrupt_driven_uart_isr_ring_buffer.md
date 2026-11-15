---
id: BM-M04
time: 80분
updated: 2026-11-11
---
# BM-M04 인터럽트 구동 UART: ISR + 링버퍼 TX/RX

## 학습 목표

- 폴링 UART(BM-B12)를 **RXNE / TXE 인터럽트 + TX·RX 링 버퍼** 로 바꾸고, ISR 과 main 이 각각 어느 인덱스를 쓰는지(단일 생산자/단일 소비자, SPSC)와 왜 그것만으로 락이 필요 없는지 설명한다.
- **TXEIE 를 언제 켜고 언제 끄는지** 정확히 말한다: main 은 push 한 *뒤에* 항상 켜고, ISR 은 링이 비었을 때 끈다. 안 끄면 생기는 TXE 폭풍, 순서를 뒤집으면 생기는 lost wakeup(데이터가 링에 갇힘)을 tick 단위로 재현한다.
- 오버런(ORE) — ISR 이 한 프레임 넘게 늦으면 생긴다 — 과 링 가득 참(drop-newest)을 **다른 사건** 으로 세고, 각각의 원인과 대책(ISR 지연 줄이기 vs 링 키우기/흐름 제어)을 구별한다.
- `uart_flush()`(링 비움 + TC), 한 줄 단위 all-or-nothing `uart_log()` 로 bring-up 로그를 안전하게 남기고, RX 링 크기를 "main 최대 지연 × baud" 로 계산한다.

## 1. 문제

펌웨어 면접에서 "UART 드라이버를 인터럽트로 바꿔 보세요" 는 **ISR ↔ main 동시성** 을 보는 가장 흔한 문제다. 코드는 짧은데, TXE 인터럽트를 언제 끄는지 모르면 CPU 가 ISR 에 갇히고, enable 순서를 틀리면 1주일에 한 번 로그가 멈추는 버그가 된다. SSD 컨트롤러의 디버그 콘솔, RF 모듈의 AT 명령 채널, 로봇 관절 보드의 상태 스트림이 모두 이 구조 위에서 돈다.

BM-B12 의 폴링 드라이버는 115200 baud 에서 바이트마다 86.8 µs (168 MHz 코어로 약 14 600 사이클) 동안 CPU 를 `while (!(SR & TXE))` 에 묶는다. 인터럽트 구동으로 바꾸면 main 은 바이트를 **링 버퍼에 넣고 바로 돌아오고**, USART 가 "다음 바이트를 받을 수 있다(TXE)" 고 신호할 때마다 ISR 이 하나씩 꺼내 DR 에 쓴다. 수신은 반대로 ISR 이 RXNE 마다 DR 을 읽어 RX 링에 넣고 main 이 나중에 꺼낸다.

이 레슨은 맥에서 돈다. 드라이버는 `REG_READ / REG_WRITE` 로만 레지스터를 만지고(BM-B02 mock 축약판), 호스트 빌드에서는 그 뒤에 USART2 행동 모델(BM-B12 모델 축약판)과 **NVIC-lite** 가 있다. 시간은 `sim_step()` 이 굴린다: 한 번 = 1 tick = 1 비트 시간. 그 tick 에 주변장치가 먼저 움직이고(시프트 레지스터, RX 도착), 그다음 USART2 인터럽트 선이 높고 enable 이면 ISR 을 **한 번** 동기 호출한다. 스레드도 시그널도 없어 결과가 항상 같다.

```svg
<svg viewBox="0 0 750 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">데이터 경로: main 과 ISR 은 링 하나씩을 반대 방향으로 나눠 쓴다</text>
  <rect x="10" y="40" width="120" height="200" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="22" y="60" fill="currentColor" font-weight="bold">main (스레드)</text>
  <text x="22" y="82" fill="currentColor" font-family="monospace">uart_write()</text>
  <text x="22" y="98" fill="currentColor">TX 생산자</text>
  <text x="22" y="114" fill="currentColor">→ head_tx 만 쓴다</text>
  <text x="22" y="180" fill="currentColor" font-family="monospace">uart_read()</text>
  <text x="22" y="196" fill="currentColor">RX 소비자</text>
  <text x="22" y="212" fill="currentColor">→ tail_rx 만 쓴다</text>
  <rect x="165" y="60" width="150" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="175" y="80" fill="currentColor" font-weight="bold">TX 링 (128 B)</text>
  <text x="175" y="98" fill="currentColor" font-family="monospace">head−tail = 개수</text>
  <rect x="165" y="164" width="150" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="175" y="184" fill="currentColor" font-weight="bold">RX 링 (64 B)</text>
  <text x="175" y="202" fill="currentColor">가득 → 새 바이트 버림</text>
  <rect x="350" y="40" width="150" height="200" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="362" y="60" fill="currentColor" font-weight="bold">USART2_IRQHandler</text>
  <text x="362" y="82" fill="currentColor">sr = SR (스냅숏 1번)</text>
  <text x="362" y="104" fill="currentColor">TXEIE &amp;&amp; TXE:</text>
  <text x="362" y="120" fill="currentColor">  pop → DR</text>
  <text x="362" y="136" fill="currentColor">  비었으면 TXEIE=0</text>
  <text x="362" y="164" fill="currentColor">RXNE|ORE:</text>
  <text x="362" y="180" fill="currentColor">  DR 읽기 → push</text>
  <text x="362" y="196" fill="currentColor">  ORE 면 ore++</text>
  <text x="362" y="212" fill="currentColor">  가득이면 drop++</text>
  <rect x="535" y="60" width="95" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="545" y="78" fill="currentColor">TDR</text>
  <text x="545" y="94" fill="currentColor">TXE=1: 빔</text>
  <rect x="645" y="60" width="95" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="655" y="78" fill="currentColor">시프트 TX</text>
  <text x="655" y="94" fill="currentColor">10 tick/B</text>
  <rect x="535" y="170" width="95" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="545" y="188" fill="currentColor">RDR</text>
  <text x="545" y="204" fill="currentColor">RXNE=1: 참</text>
  <rect x="645" y="170" width="95" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="655" y="188" fill="currentColor">시프트 RX</text>
  <text x="655" y="204" fill="currentColor">RDR 참→ORE</text>
  <g stroke="currentColor" fill="none">
    <path d="M130 88 L165 88 M315 88 L350 88 M500 80 L535 80 M630 80 L645 80"/>
    <path d="M165 192 L130 192 M350 192 L315 192 M535 190 L500 190 M645 190 L630 190"/>
    <path d="M158 84 L165 88 L158 92 M343 84 L350 88 L343 92 M528 76 L535 80 L528 84 M638 76 L645 80 L638 84"/>
    <path d="M137 188 L130 192 L137 196 M322 188 L315 192 L322 196 M507 186 L500 190 L507 194 M637 186 L630 190 L637 194"/>
  </g>
  <text x="10" y="266" fill="currentColor">NVIC-lite: sim_step() = 모델 1 tick → 선 = (RXNEIE·(RXNE|ORE)) | (TXEIE·TXE) | (TCIE·TC) → enable 이고 마스크 밖이면 ISR 1회</text>
  <text x="10" y="286" fill="currentColor">head 는 생산자만, tail 은 소비자만 쓴다 — 쓰는 변수가 겹치지 않으니 락 없이 안전 (단일 코어, 컴파일러 장벽 필요)</text>
</svg>
```

레지스터 (RM0090 기준 대표값 — RM 확인 권장. 테스트의 정답은 solution.c 모델의 정의다):

| 레지스터 | 주소/비트 | 이 레슨에서 쓰는 뜻 |
|---|---|---|
| `USART2_SR` 0x4000 4400 | ORE3 RXNE5 TC6 TXE7 | TXE = TDR 비었음 (DR 쓰기로 0), RXNE = RDR 참 (DR 읽기로 0), ORE = RDR 이 찬 상태에서 다음 바이트 도착 (SR 읽기 → DR 읽기로 0), TC = 시프트까지 끝 |
| `USART2_DR` +0x04 | [8:0] | 쓰기 → TDR, 읽기 ← RDR |
| `USART2_CR1` +0x0C | RE2 TE3 RXNEIE5 TCIE6 TXEIE7 UE13 | **RXNEIE 는 RXNE 와 ORE 둘 다** 에 인터럽트를 건다 (RM0090, 확인 권장) |
| NVIC | USART2_IRQn = 38 | F4 벡터 표 기준 (USART1 = 37) — 데이터시트/CMSIS 헤더 확인 권장 |

### Part A — 링 버퍼와 ISR 의 기본 동작

- `ring_push / ring_pop` — 크기 2^n, 자유 증가(free-running) `uint32_t head/tail`, `count = head − tail` (DS-B04). push 는 생산자만, pop 은 소비자만 호출한다. 데이터 쓰기와 인덱스 공개 사이에 컴파일러 장벽 `atomic_signal_fence`.
- `size_t uart_write(buf, n)` — **비차단**: 들어가는 만큼만 TX 링에 넣고 넣은 개수를 돌려준다. 1개 이상 넣었으면 `CR1 |= TXEIE`.
- `size_t uart_read(buf, n)` — RX 링에서 최대 n 개.
- `void USART2_IRQHandler(void)` — SR 스냅숏 한 번. RXNE/ORE → DR 읽어 RX 링에 push (가득이면 `rx_dropped++`), ORE 면 `ore++`. TXEIE 이고 TXE 면 TX 링 pop → DR, 링이 비었으면 TXEIE 끔.
- 테스트: 무작위 2만 연산 링 vs 기준 큐 (2^32 넘김 포함), `"HELLO"` 가 ISR 6번으로 나가고 그 뒤 1000 tick 동안 ISR 0번, 비차단 write 가 128 에서 멈춤, RX `"abc"`.

### Part B — 면접관이 찌르는 고장들

- **RX 링 가득**: 100 바이트가 붙어서 오는데 main 이 안 읽는다 → 정확히 36 개 drop, 남은 것은 앞의 64 개 (정책 drop-newest).
- **ISR 지연 → ORE**: 첫 바이트 도착부터 K tick 동안 ISR 을 막는다 (더 높은 우선순위 ISR / 긴 임계구역 모델). K = 0…100 전수: 유실 = ⌊K/10⌋, 드라이버 ore 카운트 = (K ≥ 10 ? 1 : 0), 사라진 위치까지 정확히.
- **TXE 폭풍**: 링이 비어도 TXEIE 를 안 끄는 ISR 로 `"HELLO"` + 1000 tick → ISR 호출 수를 센다.
- **lost wakeup**: "TXEIE 가 이미 켜져 있으면 안 건드린다" 는 write (CR1 을 push 전에 읽음) 와 정답 write 를, 레지스터 접근 k 번째 직후에 ISR 를 끼워 넣는 훅(BM-B04 식)으로 k 전수 비교. 정답 write 의 CR1 read-modify-write 사이에 ISR 가 TXEIE 를 끄는 경우도 무해함을 보인다.

### Part C — 펌웨어처럼 쓰기

- echo 5000 바이트: 원격이 무작위 간격으로 보내고, main 루프는 RX → TX 로 되돌려 보내며 무작위로 최대 300 tick 바쁘다. 두 번째 판은 ISR 도 무작위로 5–44 tick 막힌다. 판정: 첫 판은 5000 바이트 그대로, 둘째 판은 "보낸 것 − ORE 로 사라진 것" 이 순서 그대로, 드라이버 ore 카운트 = 모델의 ORE 사건 수.
- `int uart_flush(max_spins)` — TX 링이 비고 TC=1 일 때까지 (BM-B12 의 TXE vs TC). 상한이 있어 NVIC enable 을 잊은 보드에서도 멈추지 않는다.
- `int uart_log(fmt, ...)` — 48 바이트 줄 버퍼, 넘치면 `"~\n"` 로 잘라 표시, TX 링에 줄 전체가 안 들어가면 통째로 버리고 `log_dropped++`.

## 2. Problem (English)

> **Interrupt-driven UART for an STM32F4-class USART (USART2, IRQ 38).**
>
> You have a working polled driver. Convert it to interrupts with two lock-free single-producer/single-consumer ring buffers (TX: 128 B, RX: 64 B, power-of-two, free-running `uint32_t` indices).
>
> 1. `size_t uart_write(const uint8_t *buf, size_t n)` — non-blocking; queue as many bytes as fit and return the count. Must never lose a wake-up: after it returns, every queued byte will eventually be transmitted.
> 2. `size_t uart_read(uint8_t *buf, size_t n)` — non-blocking.
> 3. `void USART2_IRQHandler(void)` — RXNE (and ORE, which RXNEIE also signals): read DR, count overruns, push to RX ring; if the RX ring is full drop the new byte and count it. TXE: send the next byte, or disable the TXE interrupt when the TX ring is empty.
> 4. `int uart_flush(uint32_t max_spins)` — wait until the TX ring is empty and the last stop bit is out. Bounded.
> 5. `int uart_log(const char *fmt, ...)` — formatted log line; never emit half a line.
>
> Follow-ups: What happens if the ISR never disables TXEIE? Why must `uart_write` enable TXEIE *after* pushing, unconditionally? What is the difference between an overrun and a full ring buffer, and how do you fix each? Is `CR1 |= TXEIE` in thread context racy against the ISR clearing it?
>
> Example: `uart_write("HELLO", 5)` returns 5 immediately; the ISR runs 6 times (5 bytes + 1 to disable TXEIE); `TC` rises 51 bit-times later; then zero interrupts while idle.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/BM/BM-M04
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-M04_st && /tmp/BM-M04_st
# 처음엔 "4/42 passed", exit 1 — TODO 7개를 채워 42/42
```

TODO 는 `ring_push`, `ring_pop`, `uart_write`, `uart_read`, `USART2_IRQHandler`, `uart_flush`, `uart_log` 일곱 개다. `uart_init`, 버그 데모 두 개(`uart_write_buggy`, `uart_isr_no_txeie_off`), mock core, USART 모델, NVIC-lite, 하네스는 주어진 코드다. 스텁 ISR 는 아무것도 안 하므로 RXNE 가 선을 계속 높여 매 tick 불리지만, 모든 시뮬레이션 루프에 상한이 있어 0.1 초 안에 끝난다.

## 4. 힌트

<details>
<summary>힌트 1 — 가득과 비었음을 인덱스 두 개로</summary>

`count = head − tail` 은 `uint32_t` 뺄셈이라 head 가 2^32 를 넘어 0 근처로 돌아가도 맞다 (DS-B04 의 증명). 가득은 `count == cap`, 즉 `head − tail > mask`. 칸 번호는 `head & mask`. 생산자는 tail 을 *읽기만*, 소비자는 head 를 *읽기만* 한다.

</details>

<details>
<summary>힌트 2 — 컴파일러 장벽은 어디에</summary>

push: 데이터를 `buf[]` 에 쓴 *뒤에* head 를 올려야 ISR 이 쓰레기를 읽지 않는다. `head` 는 `volatile` 이지만 `buf[]` 는 아니라서, 컴파일러는 둘의 순서를 바꿀 수 있다. 사이에 `atomic_signal_fence(memory_order_release)`. pop: head 를 읽은 뒤 데이터를 읽고(acquire), 데이터를 다 읽은 뒤 tail 을 올린다(release).

</details>

<details>
<summary>힌트 3 — TXEIE 는 "보낼 게 있다" 는 뜻</summary>

TXE 는 TDR 이 비어 있으면 **계속 1** 이다. 그래서 TXEIE 를 켜 두면 보낼 게 없어도 인터럽트 선이 계속 높다. ISR 이 링에서 꺼낼 게 없으면 그 자리에서 TXEIE 를 꺼야 한다. 반대로 main 은 push 를 끝낸 *뒤* TXEIE 를 켠다 — "이미 켜져 있나?" 를 검사하지 말고 그냥 켠다 (이미 켜져 있으면 아무 일도 없다).

</details>

<details>
<summary>힌트 4 — ISR 의 RX 쪽</summary>

SR 을 한 번 읽어 변수에 두고 그 값으로 판단한다. RXNE 나 ORE 면 DR 을 읽는다 — 이 "SR 읽기 → DR 읽기" 가 ORE 를 지운다. ORE 만 있고 RXNE 가 없는 경우는 바이트를 링에 넣지 않는다. 링이 가득이면 새 바이트를 버리고 센다 (오래된 걸 덮어쓰려면 소비자 인덱스를 생산자가 건드려야 해서 SPSC 가 깨진다).

</details>

<details>
<summary>힌트 5 — flush 와 log</summary>

flush: "링이 비었다" 만으로는 부족하다 — 마지막 바이트가 TDR 과 시프트 레지스터에 있을 수 있다. `ring_count == 0 && (SR & TC)` 를 볼 때까지 `CPU_IDLE()` (호스트에서는 `sim_step()`). log: `vsnprintf` 반환값은 *잘리기 전* 길이다. 빈 칸 검사 후 쓰는 사이에 ISR 이 끼어들어도 빈 칸은 늘기만 하므로 검사는 안전하다.

</details>

## 5. 풀이

### Part A — 링 버퍼: 누가 무엇을 쓰나

SPSC 링의 안전성은 "쓰는 변수가 겹치지 않는다" 한 줄이다. TX 링에서 main 은 `head` 와 `buf[head]` 만 쓰고, ISR 은 `tail` 만 쓴다. 각자 상대 인덱스는 읽기만 한다. Cortex-M3/M4 에서 정렬된 32비트 읽기·쓰기는 명령 하나(LDR/STR)라 찢어지지 않는다 (C-M14). 남는 위험은 **순서** 하나다.

| 위치 | 코드 | 장벽이 없으면 |
|---|---|---|
| push | `buf[h & mask] = b;` → **release fence** → `head = h + 1;` | 컴파일러가 head 저장을 먼저 내보내면, 그 사이 ISR 이 head 를 보고 아직 안 쓴 칸을 꺼낸다 |
| pop | `if (head == t) return 0;` → **acquire fence** → `*out = buf[t & mask];` | 데이터 읽기가 head 확인보다 앞당겨지면 옛 값을 읽는다 |
| pop | `*out = …;` → **release fence** → `tail = t + 1;` | tail 이 먼저 올라가면, 데이터를 읽기 전에 생산자(ISR)가 그 칸을 덮어쓸 수 있다 |

`atomic_signal_fence` 는 C11 의 "같은 스레드에서 도는 시그널 핸들러(=ISR) 와의 순서" 용 장벽이라 명령을 하나도 만들지 않고 컴파일러 재배치만 막는다. 단일 코어 Cortex-M 은 자기 ISR 이 볼 때 메모리 순서를 바꾸지 않으므로 이것으로 충분하다. 듀얼 코어(예: STM32H7 M7+M4)나 DMA 가 상대라면 `atomic_thread_fence` / `__DMB()` 와 캐시 관리가 필요하다 (ARCH-M12). `volatile` 은 "매번 메모리에서 읽고 쓰라" 이지 "다른 메모리 접근과 순서를 지키라" 가 아니다 — 이게 면접 꼬리질문의 핵심이다.

검증: head/tail 을 0xFFFF FFC0 에서 출발시켜 push/pop 을 2만 번 무작위로 섞고, `%` 로 구현한 단순 배열 큐와 매번 비교한다. head 가 실제로 0 을 넘어가는 것까지 확인 (`head < 0x8000 0000`).

### Part A — TX: TXEIE 의 생명주기

`uart_write("HELLO")` 직후부터의 실제 기록 (trace 용 임시 프로그램이 solution.c 를 그대로 포함해 찍은 값):

```text
tick   0 write  calls=0 TDR=empty TSR=idle TC=1 TXEIE=1 ring=5 cap=""
tick   1 ISR    calls=1 TDR=empty TSR=busy TC=0 TXEIE=1 ring=4 cap=""
tick   2 ISR    calls=2 TDR=full TSR=busy TC=0 TXEIE=1 ring=3 cap=""
tick  11 ISR    calls=3 TDR=full TSR=busy TC=0 TXEIE=1 ring=2 cap="H"
tick  21 ISR    calls=4 TDR=full TSR=busy TC=0 TXEIE=1 ring=1 cap="HE"
tick  31 ISR    calls=5 TDR=full TSR=busy TC=0 TXEIE=1 ring=0 cap="HEL"
tick  41 ISR    calls=6 TDR=empty TSR=busy TC=0 TXEIE=0 ring=0 cap="HELL"
tick  51 TC     calls=6 TDR=empty TSR=idle TC=1 TXEIE=0 ring=0 cap="HELLO"
```

- tick 1: ISR 이 'H' 를 DR 에 쓴다. 시프트 레지스터가 놀고 있으므로 즉시 옮겨지고 TDR 은 다시 빈다 → TXE=1 그대로 → 다음 tick 에 또 들어온다.
- tick 2: 'E' 는 TDR 에서 기다린다 (TXE=0). 이제 인터럽트 선이 내려가 ISR 는 쉰다.
- tick 11, 21, 31: 시프트가 끝날 때마다 TDR → 시프트, TXE=1 → ISR 한 번 → 다음 바이트. **바이트당 ISR 1번**.
- tick 41: 마지막 'O' 가 시프트로 넘어가 TXE=1. ISR 이 링을 보니 비었다 → TXEIE 끔. 이 6번째 호출이 "끄는 비용" 이다.
- tick 51: 'O' 의 stop 비트까지 나가 TC=1. 이후 1000 tick 동안 ISR 0번 (테스트로 확인).

```svg
<svg viewBox="0 0 750 230" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">"HELLO" 송신: tick 0–55 (1 tick = 1 비트, 13 px/tick ≈ 0..55)</text>
  <text x="10" y="52" fill="currentColor">TXEIE</text>
  <path d="M80 60 L80 40 L613 40 L613 60 L740 60" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="10" y="92" fill="currentColor">TX 핀</text>
  <rect x="93" y="72" width="130" height="24" class="f-ok" stroke="currentColor"/><text x="150" y="89" fill="currentColor">H</text>
  <rect x="223" y="72" width="130" height="24" class="f-ok" stroke="currentColor"/><text x="280" y="89" fill="currentColor">E</text>
  <rect x="353" y="72" width="130" height="24" class="f-ok" stroke="currentColor"/><text x="410" y="89" fill="currentColor">L</text>
  <rect x="483" y="72" width="130" height="24" class="f-ok" stroke="currentColor"/><text x="540" y="89" fill="currentColor">L</text>
  <rect x="613" y="72" width="130" height="24" class="f-ok" stroke="currentColor"/><text x="670" y="89" fill="currentColor">O</text>
  <text x="10" y="132" fill="currentColor">ISR</text>
  <g class="f-warn" stroke="currentColor">
    <rect x="90" y="118" width="8" height="20"/><rect x="103" y="118" width="8" height="20"/>
    <rect x="220" y="118" width="8" height="20"/><rect x="350" y="118" width="8" height="20"/>
    <rect x="480" y="118" width="8" height="20"/><rect x="610" y="118" width="8" height="20"/>
  </g>
  <text x="84" y="154" fill="currentColor">1</text><text x="104" y="154" fill="currentColor">2</text>
  <text x="214" y="154" fill="currentColor">11</text><text x="344" y="154" fill="currentColor">21</text>
  <text x="474" y="154" fill="currentColor">31</text><text x="600" y="154" fill="currentColor">41: 끔</text>
  <text x="10" y="182" fill="currentColor">TC</text>
  <path d="M80 172 L93 172 L93 190 L743 190 L743 172" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="640" y="215" fill="currentColor">tick 51: TC=1</text>
  <text x="80" y="215" fill="currentColor">TC 는 첫 DR 쓰기(SR 읽기 → DR 쓰기)로 0, 마지막 stop 비트 뒤 1</text>
</svg>
```

정답 코드 `uart_write` 의 순서가 전부다:

```c
while (i < n && ring_push(&g_tx, buf[i])) i++;                   /* 1. 먼저 넣고 */
if (i) REG_WRITE(USART_CR1, REG_READ(USART_CR1) | CR1_TXEIE);    /* 2. 그다음 무조건 깨운다 */
```

비차단이라 링이 가득이면 일부만 넣고 개수를 돌려준다 (`uart_write(big, 200) == 128`). 다 보내야 하는 호출자는 연습 1 의 `uart_write_all` 처럼 상한 있게 기다린다.

### Part A — RX: ISR 이 비우고 main 이 꺼낸다

RX 는 반대 방향이다. ISR 는 RXNE 마다 DR 을 읽어 RX 링에 넣는다. RXNEIE 는 처음부터 켜 두고 끄지 않는다 — 언제 올지 모르니까. `"abc"` 는 tick 10, 20, 30 에 도착해 ISR 3번, main 이 tick 35 에 `uart_read` 로 3 바이트를 받는다.

### Part B — 링 가득 vs 오버런: 다른 병

| | 링 가득 (`rx_dropped`) | 오버런 (`ore`) |
|---|---|---|
| 어디서 넘치나 | 소프트웨어 링 (64 B) | 하드웨어 RDR (1 B) |
| 원인 | **main** 이 오래 안 읽음 | **ISR** 가 한 프레임(86.8 µs @115200) 넘게 못 들어옴 |
| 누가 세나 | ISR 이 push 실패를 센다 (정확한 바이트 수) | SR.ORE — 몇 바이트가 사라졌는지 모른다 ("1번 이상") |
| 대책 | 링 키우기, main 루프 지연 줄이기, RTS/CTS 흐름 제어 | ISR 우선순위 올리기, 긴 임계구역 없애기, DMA (COMM-M11) |

링 가득 테스트: 100 바이트가 붙어서 오고 main 은 한 번도 안 읽는다 → `rx_bytes == 64`, `rx_dropped == 36`, `ore == 0`, 읽으면 정확히 앞의 64 개. 정책은 drop-newest — 생산자(ISR) 혼자 결정할 수 있는 유일한 정책이다. drop-oldest(덮어쓰기)는 생산자가 tail 을 옮겨야 해서 SPSC 규칙이 깨지고, 소비자가 읽는 중인 칸을 덮을 수 있다. 덮어쓰기가 꼭 필요하면(최신 센서 값만 의미 있는 스트림) 임계구역으로 감싸거나 시퀀스 번호를 붙인다.

오버런 테스트: 붙어서 오는 20 바이트, 첫 바이트 도착 tick 부터 K tick 동안 디스패치를 막는다. 모델의 한 tick 안의 순서는 "도착 먼저, ISR 나중" 이다. 그래서 막힌 구간 [a₀, a₀+K) 와 그 끝 tick a₀+K 에 도착한 바이트 중 첫 번째만 RDR 에 남고 나머지는 사라진다 → **유실 = ⌊K/10⌋**. K = 0…100 101가지 전수로 유실 개수, 위치(1번째…⌊K/10⌋번째), 드라이버 카운트(사건 1번), ORE clear 를 모두 확인했다. 실제 출력 (`demo_uart_irq.c` 표 2):

```text
   K | 유실 바이트 (ORE) | 드라이버 ore 카운트 | 받은 바이트
   0 |                 0 |                   0 | ABCDEFGHIJKLMNOPQRST
   9 |                 0 |                   0 | ABCDEFGHIJKLMNOPQRST
  10 |                 1 |                   1 | ACDEFGHIJKLMNOPQRST
  19 |                 1 |                   1 | ACDEFGHIJKLMNOPQRST
  20 |                 2 |                   1 | ADEFGHIJKLMNOPQRST
  35 |                 3 |                   1 | AEFGHIJKLMNOPQRST
 100 |                10 |                   1 | ALMNOPQRST
 150 |                15 |                   1 | AQRST
```

읽는 법: K = 9 까지는 무사하다 — RDR 1바이트 + 시프트 레지스터 1프레임이 버퍼 역할을 한다. 그래서 **ISR 최악 지연 예산 = 1 프레임** (115200 에서 86.8 µs, 921600 에서 10.9 µs). 사라진 건 'A' 가 아니라 그 *뒤* 바이트들이다: F4 에서는 ORE 때 RDR 의 옛 값이 남고 시프트 레지스터의 새 값이 버려진다 (RM0090, 확인 권장). 드라이버는 "ORE 1번" 만 알 수 있으므로, 상위 프로토콜은 CRC/길이로 프레임을 버리고 재동기화해야 한다 (COMM-B10).

### Part B — TXE 폭풍

`uart_isr_no_txeie_off` 는 링이 비어도 TXEIE 를 끄지 않는다. TXE 는 TDR 이 비어 있는 한 1 이므로 선이 내려갈 일이 없다. `demo_uart_irq.c` 표 1 의 실제 값:

```text
   L | 폴링: main 묶임 tick | IRQ: main 묶임 tick | IRQ ISR 호출 | TXEIE 안 끄는 ISR 호출
   1 |                    0 |                   0 |            2 |                   1010
  16 |                  140 |                   0 |           17 |                   1026
  64 |                  620 |                   0 |           65 |                   1074
 128 |                 1260 |                   0 |          129 |                   1138
```

정답 ISR 는 L + 1 번 (마지막 1번이 TXEIE 를 끄는 호출), 폭풍 ISR 는 쉬는 1000 tick 내내 매 tick 불린다. 이 모델은 "한 tick 에 ISR 최대 1번" 이라 1000번에서 그치지만, 실제 Cortex-M 에서는 ISR 이 끝나는 순간 선이 여전히 높아 다시 pending → **tail-chaining 으로 곧바로 재진입** 해서 main 이 한 명령도 못 돈다. 증상은 "로그 몇 줄 찍고 보드가 멈춤 → 워치독 리셋". 폴링 열의 140 = (16 − 2) × 10: 처음 두 바이트는 시프트 레지스터와 TDR 에 바로 들어가고, 나머지 14 바이트는 한 프레임씩 기다린다.

### Part B — lost wakeup: "이미 켜져 있으면 안 켠다" 의 함정

```c
size_t uart_write_buggy(const uint8_t *buf, size_t n) {
    uint32_t cr1 = REG_READ(USART_CR1);          /* (1) "이미 송신 중인가?"          */
    /* ← 여기서 ISR: 마지막 바이트를 보내고 링이 비어 TXEIE 를 끈다            */
    while (i < n && ring_push(&g_tx, buf[i])) i++;   /* (2) push                  */
    if (i && !(cr1 & CR1_TXEIE)) REG_WRITE(...);     /* (3) 낡은 cr1 로 판단 → 안 켬 */
```

```svg
<svg viewBox="0 0 750 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">lost wakeup: 확인(1)과 행동(3) 사이에 ISR 가 상태를 바꾼다</text>
  <text x="70" y="44" fill="currentColor" font-weight="bold">main</text>
  <text x="300" y="44" fill="currentColor" font-weight="bold">USART2 ISR</text>
  <text x="530" y="44" fill="currentColor" font-weight="bold">상태</text>
  <rect x="20" y="56" width="200" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="28" y="76" fill="currentColor">(1) cr1 = CR1 → TXEIE=1</text>
  <text x="530" y="76" fill="currentColor">링 0, TDR='B', TXEIE=1</text>
  <rect x="250" y="96" width="220" height="44" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="258" y="114" fill="currentColor">tick 11: TXE=1, 링 비었음</text>
  <text x="258" y="130" fill="currentColor">→ CR1 &amp;= ~TXEIE</text>
  <text x="530" y="122" fill="currentColor">TXEIE=0, 'B' 시프트 중</text>
  <rect x="20" y="150" width="200" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="28" y="170" fill="currentColor">(2) push 'C','D'</text>
  <text x="530" y="170" fill="currentColor">링 2</text>
  <rect x="20" y="190" width="200" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="28" y="210" fill="currentColor">(3) cr1 에 TXEIE 있었음 → 안 켬</text>
  <text x="530" y="210" fill="currentColor">링 2, TXEIE=0 → 영원히</text>
  <path d="M120 86 L120 150 M120 180 L120 190" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <path d="M220 71 L250 104" stroke="currentColor" fill="none"/>
  <path d="M250 136 L220 160" stroke="currentColor" fill="none"/>
  <text x="10" y="242" fill="currentColor">고친 순서: push → 무조건 CR1 |= TXEIE. 검사가 없으니 "검사와 행동 사이" 도 없다.</text>
</svg>
```

재현 방법: mock 의 모든 레지스터 접근을 세고, 테스트가 "k 번째 접근 직후에 ISR 실행" 을 예약한다 (BM-B04 의 주입 훅과 같은 방식). 준비 상태는 `"AB"` 를 보내 링은 비었고 'B' 가 TDR 에서 기다리며 TXEIE 는 아직 켜진 순간. 주입 방식 2가지 (ISR 1번 / TXEIE 가 꺼질 때까지) × k = 0…3 × {정답, 버그} = 16 경우 전수:

| | k=0 (주입 없음) | k=1 | k=2 | k=3 |
|---|---|---|---|---|
| `uart_write_buggy` (접근 1개: CR1 읽기) | ABCD | **AB, 링에 2 바이트 갇힘** | 주입 안 일어남 → ABCD | 주입 안 일어남 → ABCD |
| `uart_write` (접근 2개: CR1 읽기, 쓰기) | ABCD | ABCD | ABCD | 주입 안 일어남 → ABCD |

`-v` 로 돌린 실제 출력:

```text
  [lost wakeup] inject=one_isr k=1: captured="AB" tx_ring=2 TXEIE=0
  [lost wakeup] inject=until_idle k=1: captured="AB" tx_ring=2 TXEIE=0
```

버그는 **k=1 한 곳에서만** 난다. 실기에서 이 창은 LDR 과 push 사이 몇 십 ns 라서, 115200 baud 로 하루 종일 로그를 찍어야 한 번 걸린다 — 그래서 "가끔 로그가 멈추고 다음 로그가 오면 이어서 나온다(다음 write 가 깨워 줌)" 같은 증상으로 보고된다.

정답 write 의 `CR1 |= TXEIE` 도 read-modify-write 다. 그 사이(k=1)에 ISR 이 TXEIE 를 끄면? main 은 낡은 값(TXEIE=1)을 다시 쓴다 → TXEIE 가 켜진 채로 남고, 다음 tick 에 ISR 이 빈 링을 보고 한 번 더 끈다. 테스트: `"ABCD"` 정상, `txeie_off == 2` (평소 1). **손해는 빈 인터럽트 한 번** 이고 데이터는 무사하다. 반대 방향(ISR 이 켜고 main 이 끄는) 경쟁은 이 설계에 없다: TXEIE 를 켜는 건 main, 끄는 건 ISR 뿐이고, ISR 이 끌 때는 링이 비어 있다. CR1 의 다른 비트를 ISR 가 바꾸는 설계라면 이 RMW 는 진짜 경쟁이 된다 → 임계구역(ARM-M10)이나 비트밴드 별칭 주소(Cortex-M3/M4) 로 TXEIE 한 비트만 쓴다.

### Part C — echo 5000 바이트, 그리고 flush / log

main 루프는 한 바퀴마다 `room = uart_tx_free()` 만큼만 RX 에서 꺼내 TX 로 넣는다 (TX 가 꽉 차면 RX 링이 대신 쌓인다 — 흐름 제어가 자연스럽게 생긴다). `-v` 실제 출력:

```text
[echo 기본] sent=5000 echoed=5000 ore_lost=0 ore_events=0 isr_calls=10246 (2.05/byte) ticks=67277 masks=0
[echo +ISR 지연] sent=5000 echoed=4841 ore_lost=159 ore_events=92 isr_calls=9845 (1.97/byte) ticks=68866 masks=162
```

- 기본: 바이트당 ISR 2.05번 (RX 1 + TX 1 + 버스트 끝마다 TXEIE 끄기). main 이 최대 300 tick (30 바이트) 바빠도 64 바이트 RX 링이 버틴다 → drop 0.
- ISR 지연 162번(5–44 tick): ORE 사건 92번에 159 바이트 유실. 드라이버 `ore` = 모델의 사건 수 92, echo 된 4841 = 5000 − 159 바이트는 순서 그대로. **드라이버는 "몇 번" 만 알고 "몇 바이트" 는 모른다** — 위 표의 한계가 그대로 보인다.

`uart_flush` 는 "링 비었음 **그리고** TC=1" 을 기다린다. `"OK"` 를 넣고 flush 하면 정확히 21 tick 뒤 (1 + 2 × 10) 돌아오고 그 tick 이 마지막 stop 비트가 나간 tick 과 같다. NVIC enable 을 잊은 보드(`nv_en = 0`)에서는 TXEIE 와 TXE 가 다 1 인데 ISR 이 안 불려 링이 안 빈다 — flush 는 1000 바퀴 상한에서 `UART_ETIMEOUT` 으로 돌아온다. 이 "주변장치는 인터럽트를 요청하는데 NVIC 가 닫혀 있다" 는 bring-up 1순위 실수다 (디버거로 SR, CR1, NVIC_ISER1 bit 6, NVIC_ISPR1 을 차례로 본다 — 38 = 32 + 6).

`uart_log` 실제 캡처 (`-v`):

```text
[000012] boot BM-M04 rst=PIN
[000345] rx=0 drop=0 ore=0
[000678] this line is much too long for a 48 ~
```

세 번째 줄은 60자라 47자(앞 45자 + `"~\n"`)로 잘렸고 `log_trunc == 1`. TX 링에 빈 칸이 4개뿐일 때 14바이트 줄은 통째로 거절되고(`UART_EFULL`, `log_dropped == 1`), 링에는 반쪽 줄이 남지 않는다. 빈 칸 검사(`uart_tx_free`)와 실제 push 사이에 ISR 이 끼어들 수 있지만, ISR 는 TX 링에서 꺼내기만 하므로 빈 칸은 늘기만 한다 — 검사 결과가 나빠질 수 없다. 이 성질은 "생산자는 하나" 일 때만 성립한다: **두 컨텍스트(main 과 다른 ISR)에서 `uart_log` 를 부르면 생산자가 둘이 되어 SPSC 가 깨진다.** 그런 설계면 로그용 링을 컨텍스트마다 두거나, push 를 임계구역으로 감싼다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-M04_so && /tmp/BM-M04_so      # 42/42 passed (0.01 s)
/tmp/BM-M04_so -v                                                                         # lost wakeup · echo · log · 폭풍 기록
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/BM-M04_as && /tmp/BM-M04_as   # 42/42, 보고 없음
```

관찰용 파일 (solution.c 를 `#include` 해서 같은 드라이버·모델로 표 3개를 뽑는다 — 레슨 디렉터리에서 빌드):

<!-- code: demo_uart_irq.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_uart_irq.c -o /tmp/BM-M04_demo && /tmp/BM-M04_demo   # 표 1–3 (0.1 s)
```

표 3 — RX 링 크기 × main 이 RX 를 읽는 주기 P (붙어서 오는 500 바이트) 의 drop 개수, 실제 출력:

```text
 cap \ P(tick) |    50   100   200   400   800  1600  3200
           16 |     0     0   100   292   388   436   468
           32 |     0     0     0    96   288   384   436
           64 |     0     0     0     0    96   288   372
          128 |     0     0     0     0     0    96   244
          256 |     0     0     0     0     0     0    64
```

경계가 정확히 `cap ≥ P/10` (P 동안 도착하는 바이트 수) 에 있다: cap 64 는 P = 400 (40 바이트)까지 0, P = 800 (80 바이트)에서 넘친다. 115200 baud 로 바꾸면 P = 800 tick = 6.9 ms — "main 루프가 가끔 7 ms 걸리면 64 바이트 RX 링으로는 부족하다" 가 이 표 한 칸이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| ISR 가 링이 비어도 TXEIE 를 안 끔 | TXE 는 TDR 이 비면 계속 1 → 매 tick 재진입 (모델: 쉬는 1000 tick 에 1010번), 실기에선 main 정지 | `else CR1 = cr1 & ~TXEIE;` |
| `if (!(CR1 & TXEIE))` 일 때만 켬 (검사를 push 앞에) | 검사와 push 사이에 ISR 가 TXEIE 를 끄면 데이터가 링에 갇힘 (k=1 에서 재현) | push → 무조건 `CR1 \|= TXEIE` |
| init 에서 TXEIE 를 켜 둠 | 보낼 게 없는데 TXE=1 → 부팅 직후 폭풍 | TXEIE 는 write 에서만, RXNEIE 만 init 에서 |
| `head` 만 `volatile`, 장벽 없음 | `buf[]` 쓰기와 `head` 공개 순서를 컴파일러가 바꿀 수 있다 | `atomic_signal_fence(release)` (또는 CMSIS `__COMPILER_BARRIER()`) |
| ISR 에서 RXNE 만 보고 ORE 무시 | 바이트 유실을 모름. USARTv2 계열(F0/L4/G4 등)은 ORE 가 RDR 읽기로 안 지워져 선이 계속 높다 (확인 권장) | ORE 카운트 + 계열별 clear (F4: SR→DR, v2: ICR.ORECF) |
| RX 가득일 때 ISR 가 tail 을 밀어 오래된 걸 덮음 | 소비자 인덱스를 생산자가 써서 SPSC 가 깨짐 | drop-newest + 카운터, 필요하면 임계구역 |
| `uart_flush` 가 "링 비었음" 만 확인 | 마지막 바이트가 TDR/시프트에 있다 → 전원·슬립·DE 해제 시 잘림 (BM-B12) | `ring_count == 0 && (SR & TC)` |
| ISR 안에서 `uart_write_all`/`printf` (막히는 함수) 호출 | 링이 비려면 TX ISR 가 돌아야 하는데 지금 그 ISR(또는 같은 우선순위) 안이다 → 데드락 | ISR 에선 비차단 write 만, 막히는 함수는 ISR 컨텍스트를 거부 (연습 1) |
| NVIC enable 을 잊음 | CR1 의 TXEIE/RXNEIE 만으로는 CPU 에 안 온다 → flush 가 영원히 (상한 없으면) | `NVIC_EnableIRQ(USART2_IRQn)` + 모든 대기에 상한 |
| `uart_log` 를 main 과 다른 ISR 에서 같이 부름 | 생산자가 둘 → head 경쟁, 줄이 섞이거나 칸이 덮임 | 생산자 하나로 제한, 또는 push 를 임계구역으로 |

## 8. 면접 Q&A

<details>
<summary>Q1. TXE 인터럽트는 언제 켜고 언제 끄나요? init 에서 켜 두면 안 되나요?</summary>

TXE 는 "TDR 이 비었다" 는 **상태** 라 보낼 게 없어도 1 이다. 켜 두면 인터럽트가 멈추지 않는다. 그래서 TXEIE 는 "보낼 게 있다" 는 뜻으로만 쓴다: main 이 push 한 뒤 켜고, ISR 가 링이 빈 걸 보고 끈다. RXNE 는 반대로 "읽을 게 생겼다" 는 **사건** 이라 RXNEIE 는 init 에서 켜고 계속 둔다. 꼬리질문 "그러면 TXE 인터럽트 한 번은 헛도는 거죠?" → 맞다, 메시지마다 끄는 호출 1번 (`HELLO` 에 ISR 6번). 그 1번을 아끼려고 ISR 안에서 "방금 마지막 바이트를 썼으면 바로 끈다" 로 바꿀 수 있지만, 그러면 main 이 그 사이 push 한 경우를 다시 따져야 한다 — 얻는 것에 비해 위험하다.

</details>

<details>
<summary>Q2. lost wakeup 이 뭔가요? 어떻게 재현하고 어떻게 막나요?</summary>

"상태를 확인하고 → 그 결과에 따라 행동" 하는 사이에 ISR 가 상태를 바꿔, 깨워야 할 것을 안 깨우는 버그다. UART 에서는 "TXEIE 가 이미 켜져 있으니 안 켜도 된다" 고 판단한 직후 ISR 가 마지막 바이트를 보내고 TXEIE 를 끄면, 새로 넣은 바이트는 아무도 안 꺼낸다. 재현: 레지스터 접근 횟수 훅으로 "CR1 읽기 직후 ISR" 를 강제 — 16 경우 중 정확히 그 지점에서만 걸린다. 막는 법: 검사를 없앤다 (push 후 무조건 enable — enable 은 멱등). 일반 원칙: 생산자는 "데이터 공개 → 깨우기" 순서, 깨우기는 멱등하게. RTOS 의 세마포어 give, Linux 의 `wake_up` 이 같은 원칙이다.

</details>

<details>
<summary>Q3. ISR 과 main 이 같은 링을 쓰는데 왜 락(인터럽트 금지)이 없어도 되나요?</summary>

SPSC 라서다. head 는 생산자만, tail 은 소비자만 쓰고, 상대 것은 읽기만 한다. 32비트 정렬 변수의 읽기/쓰기는 Cortex-M 에서 단일 명령이라 반쯤 바뀐 값을 볼 일이 없다. 최악은 "조금 낡은 값" 인데, 생산자가 낡은 tail 을 보면 빈 칸을 실제보다 적게 보고, 소비자가 낡은 head 를 보면 데이터를 실제보다 적게 본다 — 둘 다 안전한 쪽으로 틀린다. 필요한 건 컴파일러 장벽뿐 (Q4). 조건이 깨지면(생산자 둘, 64비트 인덱스를 32비트 CPU 에서, drop-oldest 덮어쓰기) 임계구역이 필요하다 (C-M14, ARM-M10).

</details>

<details>
<summary>Q4. head 를 volatile 로 했는데 장벽이 왜 또 필요하죠?</summary>

`volatile` 은 그 변수의 접근을 생략·합치지 말라는 뜻이고, volatile 이 아닌 `buf[]` 쓰기와의 순서는 보장하지 않는다. 컴파일러는 `buf[h] = b; head = h + 1;` 에서 buf 저장을 head 저장 뒤로 미룰 수 있고, 그 틈에 ISR 이 들어오면 아직 안 쓴 칸을 꺼낸다. 해결은 `atomic_signal_fence(memory_order_release)` 또는 `__asm volatile("" ::: "memory")` (CMSIS `__COMPILER_BARRIER()`). 이건 명령을 만들지 않는다. 하드웨어 장벽(DMB)은 다른 관찰자 — 다른 코어, DMA — 가 있을 때 필요하고, 단일 코어의 자기 ISR 에는 필요 없다 (ARMv7-M 은 자기 예외 진입에 대해 프로그램 순서를 보인다 — ARM ARM 확인 권장). 꼬리질문 "buf 도 volatile 로 하면?" → 동작은 하지만 모든 바이트 접근이 최적화에서 빠지고, 의도가 장벽만큼 분명하지 않다.

</details>

<details>
<summary>Q5. 오버런과 버퍼 가득은 어떻게 다르고, 각각 어떻게 고치나요?</summary>

오버런(ORE)은 하드웨어 RDR(1바이트)이 넘친 것 — ISR 가 한 프레임(115200 에서 86.8 µs) 안에 DR 을 못 읽었다. 원인은 ISR 지연: 더 높은 우선순위 ISR, 긴 `__disable_irq()` 구간, 플래시 지우기 중 스톨. 고치는 법은 우선순위·임계구역 정리, 아니면 DMA (COMM-M11). 버퍼 가득은 소프트웨어 링이 넘친 것 — main 이 늦다. 고치는 법은 링 키우기(표 3 의 `cap ≥ 지연 × 바이트율`), main 루프 쪼개기, RTS/CTS. 구분해서 세야 원인을 안다. 그리고 ORE 는 "1번 이상 유실" 만 알려준다 — 표 2 에서 K = 150 이면 15 바이트가 사라졌는데 카운트는 1.

</details>

<details>
<summary>Q6. ISR 하나에서 RX 와 TX 를 둘 다 처리하는데, 순서가 중요한가요?</summary>

RX 를 먼저 한다. RX 는 늦으면 데이터가 사라지지만(ORE), TX 는 늦어도 선 위에 공백이 조금 생길 뿐이다. SR 을 한 번 읽어 스냅숏으로 둘 다 판단하는 이유는 DR 읽기가 ORE/FE 등을 지우기 때문이다 — 다시 읽으면 이미 지워진 오류를 놓친다 (BM-B12). 같은 이유로 "ORE 만 있고 RXNE 없음" 이면 DR 을 읽어 지우되 링에 넣지 않는다. 꼬리질문 "SR 을 읽은 뒤 DR 을 읽기 전에 새 바이트가 오면?" → 실기에서 ORE 지우기 시퀀스의 경계 조건은 RM 의 서술 범위 밖이라 확인 권장, 이 모델은 ISR 전체가 한 tick 안에서 원자적이라 그 경우를 만들지 않는다.

</details>

<details>
<summary>Q7. "CR1 |= TXEIE" 는 main 에서 하는 read-modify-write 인데 ISR 와 경쟁 안 하나요?</summary>

한다. 다만 이 설계에서 결과가 무해하다. main 이 CR1 을 읽은 직후 ISR 이 TXEIE 를 끄면, main 은 TXEIE=1 인 낡은 값을 다시 쓴다 → TXEIE 가 다시 켜지고, 다음 ISR 이 빈 링을 보고 한 번 더 끈다 (테스트: `txeie_off == 2`). 데이터 손실은 없고 빈 인터럽트 1번이 비용이다. 무해한 이유는 CR1 에서 ISR 가 바꾸는 비트가 TXEIE 하나뿐이고 그 방향(끄기)이 main 의 방향(켜기)과 반대이기 때문이다. ISR 이 CR1 의 다른 비트(TCIE, RE 등)도 바꾸면 main 의 RMW 가 그걸 되돌릴 수 있다 → 임계구역, 또는 Cortex-M3/M4 비트밴드 별칭(0x4200 0000 + (offset·32) + bit·4)으로 한 비트만 원자적으로 쓴다. 최신 STM32 USART 에는 set/clear 전용 레지스터가 없어서 이 문제가 계속 남는다 (확인 권장).

</details>

<details>
<summary>Q8. RX 링은 몇 바이트로 잡아야 하나요?</summary>

"main 이 RX 를 못 읽는 최악 시간 × 바이트율" 보다 크게, 그리고 2^n 으로 올린다. 115200 8N1 = 11 520 B/s. main 루프 최악 5 ms → 57.6 → 58 → 64. 921600 이면 461 → 512. 표 3 이 모델에서 같은 경계를 보여준다 (cap 64 는 40 바이트 지연까지 0 drop, 80 바이트에서 넘침). 최악 시간은 측정해야 한다 — GPIO 토글 + 로직 분석기, 또는 DWT CYCCNT 로 main 루프 한 바퀴의 최대값 (HWLAB-M05). 링의 최고 수위(high-water mark)를 기록해 두면 현장에서 여유를 볼 수 있다. 메모리가 아까우면 DMA circular + IDLE (COMM-M11) 로 링 자체를 하드웨어가 채우게 한다.

</details>

<details>
<summary>Q9. printf 를 이 드라이버 위로 리타겟하면 무엇을 조심하나요?</summary>

(1) 링이 가득일 때 정책: 버릴지(비차단, 로그 손실) 기다릴지(막힘). 막히는 쪽은 ISR 안에서 부르면 데드락이다. (2) 생산자 하나 규칙 — 여러 컨텍스트에서 printf 하면 줄이 섞이거나 링이 깨진다. (3) 패닉/HardFault 경로에서는 인터럽트가 안 돌 수 있으니 폴링 출력으로 전환하는 "panic putc" 를 따로 둔다. (4) 리셋/슬립 전에 `uart_flush` (TC 까지). 이 레슨의 `uart_log` 는 줄 단위 all-or-nothing 과 잘림 표시로 (1)(2) 의 피해를 보이는 형태로 만든다. 리타겟 자체는 BM-B09.

</details>

## 9. 추가 연습

### 연습 1 — 막히는 write (상한 있음, ISR 거부)

`long uart_write_all(buf, n, max_spins)` 를 써라. 링이 차면 `CPU_IDLE()` 하며 기다리고, 상한을 넘으면 넣은 만큼만 돌려준다. ISR 컨텍스트에서 부르면 `UART_EINVAL`.

<details>
<summary>답 보기</summary>

`done += uart_write(buf + done, n − done)` 를 반복하고 모자라면 `CPU_IDLE()`. 300 바이트를 128 칸 링으로 보내면 300 을 돌려주고 flush 후 캡처가 원본과 같다. NVIC 가 닫혀 있으면 첫 바퀴에 128 개만 들어가고 50 바퀴 상한 뒤 128 을 돌려준다 (51 tick). ISR 판별은 호스트에서 NVIC-lite 의 `nv_in_isr`, 타깃에서는 `SCB->ICSR` 의 VECTACTIVE[8:0] ≠ 0 (ARMv7-M, 확인 권장). ISR 안에서 기다리면 같은 우선순위의 TX ISR 이 못 돌아 링이 영원히 안 빈다.

</details>

### 연습 2 — RS-485 DE 를 TC 인터럽트로 내리기

반이중 RS-485 트랜시버의 DE 를 PA8 (BSRR) 로 제어한다. 보내기 전에 DE=1, 마지막 stop 비트가 나간 **그 tick** 에 DE=0 이 되도록 ISR 를 확장하라. 흔한 버그(링이 빌 때 DE 를 내림)도 모델로 보여라.

<details>
<summary>답 보기</summary>

`isr_485`: 본문 ISR 를 그대로 부르고, 그 호출에서 TXEIE 가 꺼졌으면(= 링이 막 비었고 마지막 바이트는 시프트 레지스터에 있음) TCIE 를 켠다. 다음에 TC 로 들어오면 DE=0, TCIE 끔. TCIE 를 켜는 순간 TC=0 인 이유: 마지막 바이트를 DR 에 쓴 것이 "SR 읽기 → DR 쓰기" 라 TC 를 지웠다. `"RS485"` 에서 DE 하강 tick = 마지막 바이트 완료 tick, ISR 7번 (바이트 5 + TCIE 전환 1 + TC 1). 버그 버전(링이 빌 때 DE=0)은 DE 가 정확히 한 프레임(10 tick) 일찍 내려가 마지막 바이트가 버스에 안 나간다 — BM-B12 의 TXE vs TC 를 인터럽트 버전으로 다시 본 것. 많은 STM32 USART(v2)는 CR3.DEM 으로 DE 를 하드웨어가 처리한다 (확인 권장).

</details>

### 연습 3 — 줄 조립기

`int uart_readline(line_asm_t *la, char *out, size_t cap, int *trunc)` — RX 링에서 바이트를 꺼내 CR, LF, CRLF 를 모두 한 줄 끝으로 처리하고, 31자를 넘는 줄은 잘라서 표시한다 (나머지는 줄 끝까지 버림). 끝나지 않은 줄은 다음 호출까지 보관.

<details>
<summary>답 보기</summary>

CR 다음의 LF 는 건너뛰도록 `last_cr` 를 기억한다. 입력 `"ping\r\nstatus\nAT+THIS_COMMAND_IS_WAY_TOO_LONG_FOR_32\r\n\r\nx"` (바이트 사이 0–6 tick 무작위 간격) → 줄 4개: `"ping"`, `"status"`, 31자로 잘린 `"AT+THIS_COMMAND…"` (trunc=1), 빈 줄 `""`. `"x"` 는 `la->len == 1` 로 남아 있다. AT 명령 파서, CLI (COMM-B14) 의 입력단이 이것이다.

</details>

### 연습 4 — RX 링 크기 계산

`uint32_t rx_cap_needed(baud, latency_us)` — main 이 최대 `latency_us` 동안 못 읽어도 넘치지 않는 2^n 크기 (8N1). 모델 단위 판 `rx_cap_needed_ticks(P, frame)` 로 표 3 시뮬레이션과 맞춰 보라.

<details>
<summary>답 보기</summary>

`bytes = ceil(latency_us × baud / (10 × 10^6))` → 2^n 으로 올림. 115200 / 5 ms → 64, 921600 / 5 ms → 512, 115200 / 1 ms → 16. 시뮬레이션: P ∈ {50 … 1600} 10가지에서 계산한 크기는 500 바이트 버스트에 drop 0, 절반 크기는 `cap/2 × 10 < P` 일 때 반드시 drop. P = 400 → 64, 32 칸이면 drop 96 (표 3 과 같은 값).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cd lessons/BM/BM-M04     # practice.c 는 같은 디렉터리의 solution.c 를 #include 한다
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-M04_pr && /tmp/BM-M04_pr   # 24/24 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o /tmp/BM-M04_pas && /tmp/BM-M04_pas   # 24/24
```

## 10. 핵심 정리

- 인터럽트 UART = TX 링(main → ISR) + RX 링(ISR → main). 각 링은 SPSC: head 는 생산자만, tail 은 소비자만 쓴다 → 락 불필요, **컴파일러 장벽** 만 필요 (`volatile` 은 순서를 보장하지 않는다).
- TXE 는 상태(빈 TDR)라 TXEIE 는 "보낼 게 있음" 일 때만: main 은 **push 후 무조건** 켜고, ISR 는 **링이 비면** 끈다. 안 끄면 폭풍, 검사 후 켜면 lost wakeup.
- RXNEIE 는 init 에서 켜고 둔다. F4 에서 RXNEIE 는 ORE 에도 걸린다. ISR 는 SR 스냅숏 하나로 RX 먼저, TX 나중.
- ORE = ISR 가 1 프레임 넘게 늦음 (하드웨어 1바이트 넘침, 몇 바이트인지 모름). 링 가득 = main 이 늦음 (정확히 셀 수 있음). 원인이 다르니 따로 센다.
- 링 크기 ≥ main 최악 지연 × 바이트율 → 2^n. 115200 에서 5 ms → 64 B.
- flush = 링 비었음 + TC. 모든 대기에 상한 — NVIC enable 을 잊어도 멈추지 않게. 막히는 함수는 ISR 에서 금지.
- 로그는 생산자 하나, 줄 단위 all-or-nothing.

## 참고자료

- ST RM0090 *STM32F405/415, F407/417, F427/437, F429/439 Reference Manual* — USART 장: 인터럽트 요청 표(TXE/TC/RXNE/ORE ↔ TXEIE/TCIE/RXNEIE), ORE 동작과 clear 시퀀스, SR/CR1 비트 — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- ST DS8626 (STM32F405/407 datasheet) 또는 CMSIS device header `stm32f407xx.h` — `USART2_IRQn = 38` 확인용
- ARM *ARMv7-M Architecture Reference Manual* (DDI 0403) — 예외 진입/복귀, pending 동작, ICSR.VECTACTIVE, 비트밴드
- ISO/IEC 9899:2011 §7.17.4 `atomic_signal_fence`
- Linux `include/linux/circ_buf.h`, `Documentation/core-api/circular-buffers.rst` — SPSC 링과 장벽 배치 (같은 원리)
- STM32CubeF4 HAL `stm32f4xx_hal_uart.c` 의 `HAL_UART_Transmit_IT` / `UART_EndTransmit_IT` — TXEIE → TCIE 전환 방식 비교용 (원문 확인 권장)
- 관련 레슨: BM-B02 (mock), BM-B04 (접근 횟수 주입 훅, RMW 경쟁), BM-B09 (printf 리타겟), BM-B12 (폴링 UART, TXE vs TC), DS-B03/DS-B04 (링 버퍼, 자유 증가 인덱스), C-M14 (ISR ↔ main 공유 데이터), COMM-M11 (UART RX DMA + IDLE), COMM-B10/B14 (프레임 재동기화, CLI 줄 편집), ARCH-M12 (DMA 캐시 일관성). 다음 단계: BM-M08 (DMA 드라이버), ARM-M10 (PRIMASK/BASEPRI 임계구역), ARM-A08 (인터럽트 지연)
