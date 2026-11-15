---
id: ARCH-M11
time: 90분
updated: 2026-11-11
---
# ARCH-M11 DMA 컨트롤러 모델: 디스크립터 체인

## 학습 목표

- 디스크립터 `{src, dst, len, next, flags}` 연결 리스트를 따라가는 DMA 엔진(하드웨어 쪽)을 C 로 사이클 단위 모델링하고, 결과 버퍼·**인터럽트 순서 로그**·**사이클별 버스 사용 트레이스**를 문자열로 검증한다.
- 채널 중재(arbitration)를 설명하고 코드로 쓴다: 고정 우선순위 + 같은 레벨은 라운드로빈(round-robin), **버스트 N 비트가 중재 단위(비선점)** 라서 최고 우선순위도 기다린다는 것.
- 에러를 두 종류로 나눠 말할 수 있다: 페치 때 잡히는 **설정 에러**(len 0, 미정렬, 예약 원소 크기, 나쁜 디스크립터 주소, 루프 가드)와 전송 도중 잡히는 **버스 에러**. 채널은 멈추고 "어느 디스크립터에서 몇 바이트까지" 를 남긴다.
- 주변장치→메모리 전송을 DREQ(요청 신호) + 생산 속도 + FIFO 깊이로 모델링하고, 대량 복사와 경쟁할 때 **overrun 이 나는 두 조건**(지연: 다른 채널의 버스트, 대역폭: RR 몫)을 숫자로 보인다.

## 1. 문제

SoC·MCU 면접에서 DMA 는 "드라이버 API 쓸 줄 아나" 보다 "엔진이 안에서 뭘 하나" 를 묻는다 — "디스크립터 체인은 어떻게 걷나요?", "우선순위가 제일 높은데 왜 UART 가 overrun 났죠?", "전송 도중 버스 에러가 나면 하드웨어는 무엇을 남기나요?". 실리콘 검증(silicon validation)에서는 이걸 거꾸로, 골든 모델과 비교하는 테스트로 만든다. 이 레슨은 그 골든 모델을 직접 짠다.

범위: 엔진(하드웨어) 쪽만. 캐시 일관성(clean/invalidate)은 ARCH-M12, 드라이버 API(circular, half/complete 콜백)는 다음 단계 BM-M08 이다. 이 모델의 메모리는 캐시가 없는 SRAM 이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">모델: 4 채널 DMA 엔진 — 페치 유닛 · 중재기 · 버스 포트 하나</text>
  <rect x="15" y="35" width="150" height="150" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="25" y="55" fill="currentColor">채널 0..3 레지스터</text>
  <text x="25" y="77" fill="currentColor" font-size="12">prio 0..3 (클수록 높음)</text>
  <text x="25" y="95" fill="currentColor" font-size="12">burst N (중재 단위)</text>
  <text x="25" y="113" fill="currentColor" font-size="12">cur, src, dst, left</text>
  <text x="25" y="131" fill="currentColor" font-size="12">flags, esize</text>
  <text x="25" y="149" fill="currentColor" font-size="12">st, err, err_desc</text>
  <text x="25" y="167" fill="currentColor" font-size="12">err_done, ndesc</text>
  <rect x="210" y="35" width="130" height="60" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="225" y="58" fill="currentColor">중재기</text>
  <text x="220" y="78" fill="currentColor" font-size="12">고정 + 같은 레벨 RR</text>
  <rect x="210" y="125" width="130" height="60" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="225" y="148" fill="currentColor">페치 유닛</text>
  <text x="220" y="168" fill="currentColor" font-size="12">next 따라 20 B 읽기</text>
  <rect x="385" y="70" width="110" height="80" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="400" y="95" fill="currentColor">버스 포트</text>
  <text x="395" y="115" fill="currentColor" font-size="12">1 사이클 =</text>
  <text x="395" y="133" fill="currentColor" font-size="12">원소 1개 읽고 쓰기</text>
  <rect x="545" y="30" width="180" height="120" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="555" y="50" fill="currentColor">SRAM 0x2000_0000 64 KB</text>
  <text x="555" y="72" fill="currentColor" font-size="12">디스크립터 d0 → d1 → d2</text>
  <text x="555" y="90" fill="currentColor" font-size="12">소스 / 목적지 버퍼</text>
  <text x="555" y="110" fill="currentColor" font-size="12">끝(0x2001_0000) 넘으면</text>
  <text x="555" y="128" fill="currentColor" font-size="12">→ 버스 에러</text>
  <rect x="545" y="175" width="180" height="85" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="555" y="195" fill="currentColor">주변장치 0x4000_1000</text>
  <text x="555" y="215" fill="currentColor" font-size="12">P 사이클마다 1 B 생산</text>
  <text x="555" y="233" fill="currentColor" font-size="12">FIFO 깊이 D, 꽉 차면 overrun</text>
  <text x="555" y="251" fill="currentColor" font-size="12">DR 읽기 = FIFO 팝</text>
  <path d="M165 65 L208 65" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M165 155 L208 155" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M340 65 L383 95" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M340 155 L383 125" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M495 100 L543 90" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M495 125 L543 200" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M545 232 L362 232 L362 82 L342 82" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 4" fill="none"/>
  <text x="270" y="272" fill="currentColor" font-size="12">DREQ = FIFO 에 원소 하나 이상 (점선 → 중재기)</text>
  <text x="15" y="215" fill="currentColor" font-size="12">사이클 순서: ① 주변장치 생산</text>
  <text x="15" y="233" fill="currentColor" font-size="12">② 페치 (시간 0) ③ 중재 + 비트 1개</text>
  <text x="15" y="251" fill="currentColor" font-size="12">완료 → TC IRQ / 에러 → ERR IRQ</text>
</svg>
```

모델 규칙 (테스트가 이것에 기대므로 정확히):

| 항목 | 규칙 |
|---|---|
| 시간 | 사이클 0 부터. 한 사이클 = ① 주변장치가 생산 ② BUSY 이고 안 실린 채널의 디스크립터 페치(시간 0) ③ 버스를 쓸 채널 하나가 **비트(beat) 1개** — 원소(1/2/4 B) 하나를 읽고 쓴다 |
| 디스크립터 | SRAM 안 20 B, 4 B 정렬: `+0 src, +4 dst, +8 len(바이트), +12 next(0 = 끝), +16 flags` |
| flags | bit0 `IRQ`(이 디스크립터 끝나면 TC), bit1 `SRC_INC`, bit2 `DST_INC`, bit5:4 원소 크기 코드(0:1 B, 1:2 B, 2:4 B, 3: 예약), bit8 `DREQ_SRC` |
| 중재 | 준비된 채널 중 prio 최고. 같은 레벨은 마지막 승자 다음 번호부터 RR. 이긴 채널은 **최대 burst 비트** 동안 버스를 가진다. 버스트는 디스크립터 경계를 넘지 않고, 채널이 준비 상태를 잃으면(DREQ 내려감) 일찍 끝난다 |
| 준비 | BUSY + 디스크립터 실림 + (DREQ_SRC 면 FIFO ≥ esize) |
| 로그 | TC: `@사이클 chN TC d=디스크립터 n=len`, 에러: `@사이클 chN ERR 종류 d=디스크립터 a=주소 done=바이트`. 트레이스: 사이클마다 버스 쓴 채널 번호, 빈 사이클 `.` |

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">디스크립터 체인 (Part A gather: 13 + 7 + 20 B → 연속 40 B)</text>
  <rect x="15" y="35" width="200" height="110" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="25" y="53" fill="currentColor" font-weight="bold">d0 @ 0x2000_0100</text>
  <text x="25" y="72" fill="currentColor" font-size="12">src  0x2000_1000</text>
  <text x="25" y="88" fill="currentColor" font-size="12">dst  0x2000_2000</text>
  <text x="25" y="104" fill="currentColor" font-size="12">len  13</text>
  <text x="25" y="120" fill="currentColor" font-size="12">next 0x2000_0120</text>
  <text x="25" y="136" fill="currentColor" font-size="12">flags SRC_INC|DST_INC</text>
  <rect x="270" y="35" width="200" height="110" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="280" y="53" fill="currentColor" font-weight="bold">d1 @ 0x2000_0120</text>
  <text x="280" y="72" fill="currentColor" font-size="12">src  0x2000_1100</text>
  <text x="280" y="88" fill="currentColor" font-size="12">dst  0x2000_200D</text>
  <text x="280" y="104" fill="currentColor" font-size="12">len  7</text>
  <text x="280" y="120" fill="currentColor" font-size="12">next 0x2000_0140</text>
  <text x="280" y="136" fill="currentColor" font-size="12">flags SRC_INC|DST_INC</text>
  <rect x="525" y="35" width="200" height="110" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="535" y="53" fill="currentColor" font-weight="bold">d2 @ 0x2000_0140</text>
  <text x="535" y="72" fill="currentColor" font-size="12">src  0x2000_1200</text>
  <text x="535" y="88" fill="currentColor" font-size="12">dst  0x2000_2014</text>
  <text x="535" y="104" fill="currentColor" font-size="12">len  20</text>
  <text x="535" y="120" fill="currentColor" font-size="12">next 0 (끝)</text>
  <text x="535" y="136" fill="currentColor" font-size="12">flags ...|IRQ</text>
  <path d="M215 115 L268 115" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M470 115 L523 115" stroke="currentColor" stroke-width="2" fill="none"/>
  <rect x="15" y="170" width="130" height="30" class="f-ok" stroke="currentColor"/>
  <text x="25" y="190" fill="currentColor" font-size="12">0..12 (d0)</text>
  <rect x="145" y="170" width="70" height="30" class="f-ok" stroke="currentColor"/>
  <text x="150" y="190" fill="currentColor" font-size="12">13..19</text>
  <rect x="215" y="170" width="200" height="30" class="f-ok" stroke="currentColor"/>
  <text x="225" y="190" fill="currentColor" font-size="12">20..39 (d2)</text>
  <text x="430" y="190" fill="currentColor" font-size="12">목적지 0x2000_2000 연속 40 B</text>
  <text x="15" y="222" fill="currentColor" font-size="12">로그: @39 ch0 TC d=20000140 n=20 (IRQ 는 마지막에만) — 디스크립터마다 켜면 @12, @19, @39</text>
</svg>
```

### Part A — 디스크립터 페치, 비트, 체인 따라가기

`desc_fetch`(5 워드 읽기 + 고정 순서 검증), `do_beat`(원소 하나, 주소 증가/고정), `desc_complete`(TC 인터럽트, `next` 로 이동 또는 DONE). 시나리오: 64 B 한 개(4 B 원소), gather 13+7+20 B (IRQ 마지막만 / 디스크립터마다), 고정 소스로 하프워드 패턴 채우기, 고정 목적지(FIFO 레지스터처럼 마지막 원소만 남음), 같은 64 B 를 1 B 원소로(비트 64 개).

### Part B — 중재와 에러

`arbitrate`(고정 우선순위 + 같은 레벨 RR), `grant_step`(그랜트를 버스트 동안 유지하거나 새로 중재, 비트 실행, 디스크립터 끝에서 그랜트 반납). 트레이스 문자열로 검사: 같은 레벨 둘 → `00001111…`, 높은 레벨 먼저, **사이클 3 에 도착한 prio 3 채널이 prio 0 채널의 버스트 8 을 기다림**, 고정 우선순위 기아(starvation). 에러: LEN0 / ALIGN / CFG / 체인 중간의 나쁜 next / 정렬 안 된 next / 전송 도중 SRAM 끝을 넘는 BUS / d0↔d1 루프와 가드 8 / 한 채널 에러가 다른 채널을 막지 않음.

### Part C — 주변장치 → 메모리 (DREQ, FIFO, overrun)

`periph_tick`/`periph_pop`(생산 속도, FIFO, overrun, underrun)과 `build_rx_chain`(DR → 버퍼 여러 개, 32 B 간격 디스크립터). 시나리오: UART 같은 48 B 를 버퍼 3 개로(TC 3 번), 헤더 8 B / 페이로드 24 B 분리(16 비트 원소), 그리고 4 KB 대량 mem→mem 복사와 경쟁시키는 5 가지 설정에서 overrun 수를 정확히 맞춘다.

## 2. Problem (English)

> **DMA controller model with descriptor chains (hardware side).**
>
> Memory: 64 KB SRAM at `0x2000_0000`; a peripheral at `0x4000_1000` with `SR` (+0, FIFO level, read-only) and `DR` (+4; read pops the RX FIFO, write goes to a TX sink). Any other address is a bus error. A descriptor is 20 bytes in SRAM, 4-byte aligned: `{src, dst, len (bytes), next (0 = end), flags}`; flags: `IRQ`, `SRC_INC`, `DST_INC`, element size code (1/2/4 bytes, code 3 reserved), `DREQ_SRC`.
>
> The engine has 4 channels, each with a priority (0..3, higher wins), a burst length N (beats per grant) and an optional loop guard (max descriptors per start). One cycle: (1) the peripheral may produce a byte, (2) every busy channel without a loaded descriptor fetches one (zero time), (3) one channel performs one beat (read one element, write it).
>
> 1. `desc_fetch`: validate in this order — loop guard → descriptor address → reserved size → `len == 0` → alignment of src/dst/len to the element size; on failure stop the channel with an error IRQ. `do_beat`: one element; a bus error stops the channel and records the faulting address and bytes done in this descriptor. `desc_complete`: TC IRQ if requested; follow `next` or finish.
> 2. `arbitrate`: highest priority among ready channels, round-robin within a level. `grant_step`: a grant lasts up to N beats (non-preemptive), never crosses a descriptor boundary, and ends early if the channel stops being ready.
> 3. Peripheral: a byte every P cycles into a FIFO of depth D (full → the new byte is lost, `overrun++`); `DREQ` = FIFO holds at least one element. `build_rx_chain` builds a periph→memory scatter chain.
>
> Checks compare result buffers, the IRQ log and a per-cycle bus trace as exact strings. Follow-ups: why does the highest-priority channel still overrun? What FIFO depth do you need? What status must hardware leave after a mid-transfer bus error?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/ARCH/ARCH-M11
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있다
mkdir -p /tmp/esn_ARCH-M11
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/esn_ARCH-M11/st && /tmp/esn_ARCH-M11/st; echo "exit $?"
```

실제 출력 (앞뒤만):

```text
FAIL starter.c:289  memcmp(P(DST), P(SRC), 64) == 0 && E.ch[0].st == CH_DONE && E.ch[0].total == 64
FAIL starter.c:290  strcmp(irq_log(&E), "@15 ch0 TC d=20000100 n=64") == 0
FAIL starter.c:291  E.cycle == 16 && strcmp(E.trace, "0000000000000000") == 0
...
FAIL starter.c:497  memcmp(P(0x20008000u), P(SRC), 4096) == 0
FAIL starter.c:500  E.ch[1].st == CH_DONE && memcmp(P(DST), g_stream, 48) == 0
5/49 passed
exit 1
```

TODO 는 8 개: `desc_fetch`(A1), `do_beat`(A2), `desc_complete`(A3), `arbitrate`(B1), `grant_step`(B2), `periph_tick`·`periph_pop`(C1), `build_rx_chain`(C2). 메모리 맵, `bus_rd/bus_wr`, `chan_error`(상태 기록 + 에러 IRQ), `chan_ready`, `engine_step` 은 주어진다. `./so -v` 는 시나리오마다 트레이스와 IRQ 로그를 찍는다. 스텁에서 지나가는 5 개는 모두 C3 의 "overrun 이 0 이어야 한다"(아무것도 안 하면 0) · "바이트를 잃은 UART 채널은 BUSY 로 남는다"(스텁에선 늘 BUSY) 처럼 우연히 맞는 검사다.

모델이 단순화한 것 (실제 칩과 비교해서 읽을 것):

| 모델 | 실제 컨트롤러 |
|---|---|
| 비트 1개 = 1 사이클에 읽기 + 쓰기 | 읽기와 쓰기는 별도 버스 전송이다. 내부 FIFO 로 읽기 몇 개를 모았다가 쓰는 설계가 많다(STM32F4 DMA FIFO 모드). 사이클 수는 버스·대기 상태(ARCH-B12)를 따른다 |
| 디스크립터 페치 = 시간 0 | LLI(linked-list item) 페치도 같은 버스에서 읽는다 — 연습 4 에서 비용을 넣는다 |
| 버스트 = 중재 단위(원소 N 개) | 버스 프로토콜의 버스트(AHB INCR4, AXI AxLEN)와 같을 수도 다를 수도 있다(SOCX-B02) |
| TC = 마지막 쓰기 사이클 | 실제 설계는 보통 마지막 쓰기의 응답(AXI B 채널 등)까지 본 뒤 TC 를 올린다고 알려져 있다 — posted write 를 기다리는 문제 (설계마다 다름, 확인 필요) |
| 주변장치 DREQ = FIFO ≥ 원소 1개 | 실제 주변장치는 single 요청과 burst 요청(FIFO 문턱값)을 따로 낸다(예: PL080 의 DMACSREQ / DMACBREQ 신호) |

## 4. 힌트

<details><summary>힌트 1 — 페치 검증 순서와 "어디서 실패했나"</summary>

순서가 정해진 이유: 루프 가드는 "디스크립터를 또 읽을까" 를 묻는 것이라 읽기 전에, 주소 검사도 읽기 전에(못 읽으니까). 그다음 5 워드를 읽고 `ndesc++`, 그리고 내용 검사(크기 코드 → len 0 → 정렬). 정렬은 `(src | dst | len) & (esize - 1)` 한 번으로 셋 다 본다. 실패하면 `chan_error(e, c, 코드, c->cur)` — `loaded` 가 아직 false 이므로 `done = 0` 이 자동으로 기록된다.
</details>

<details><summary>힌트 2 — 버스 에러 때 원소는 끝났나?</summary>

`bus_rd` 가 실패하면 아무것도 안 옮겼다. `bus_wr` 가 실패하면 읽기는 이미 됐다 — 주변장치 DR 이었다면 그 바이트는 FIFO 에서 빠져서 사라진다. 어느 쪽이든 그 원소는 **끝나지 않은 것**: `left`, `total`, 주소를 바꾸지 말고 바로 `chan_error(E_BUS, 실패한 주소)`. 그래야 `err_done = len - left` 가 "확실히 쓰인 바이트" 가 된다.
</details>

<details><summary>힌트 3 — 라운드로빈 한 줄</summary>

레벨마다 `rr_last[lvl]` 을 두고 `i = (rr_last + k) % NCH` 를 k = 1..NCH 로 돈다. k = NCH 가 자기 자신이라 "혼자 준비된 채널은 계속 이긴다" 가 자연히 된다. 레벨은 3 → 0 으로 내려가며 첫 승자에서 끝.
</details>

<details><summary>힌트 4 — 그랜트를 언제 놓나</summary>

세 경우: (1) `gleft == 0` (버스트 다 씀), (2) 그 채널이 준비 상태가 아님(DREQ 내려감, 에러, DONE), (3) 디스크립터를 막 끝냄. (1)(2)는 다음 비트 전에 검사, (3)은 비트 직후에 `gch = -1`. 높은 우선순위 채널이 새로 와도 (1)~(3) 이 아니면 놓지 않는다 — 이게 "비선점 버스트" 다.
</details>

<details><summary>힌트 5 — 주변장치 FIFO</summary>

원형 버퍼(head, count). 생산: 꽉 찼으면 새 바이트를 버리고 overrun++ — FIFO 에 이미 있는 것은 지킨다. 팝: `count < n` 이면 아무것도 꺼내지 말고 underrun++. 리틀 엔디언: i 번째로 꺼낸 바이트가 `v` 의 비트 8i 부터.
</details>

## 5. 풀이

### Part A — 페치 → 비트 → 완료

엔진 하나가 하는 일은 세 단계의 반복이다. 디스크립터를 읽어 채널 레지스터에 싣고(load), 원소 단위로 옮기고, 다 옮기면 다음 디스크립터로.

```c
static bool desc_fetch(dma_t *e, chan_t *c) {
    if (c->max_desc && c->ndesc >= c->max_desc) { chan_error(e, c, E_LOOP, c->cur); return false; }
    if ((c->cur & 3u) || !sram_ok(c->cur, DESC_BYTES)) { chan_error(e, c, E_DESC, c->cur); return false; }
    /* ... 5 워드 읽기, ndesc++ ... */
    if (code == 3u) { chan_error(e, c, E_CFG, c->cur); return false; }
    if (len == 0) { chan_error(e, c, E_LEN0, c->cur); return false; }
    if ((src | dst | len) & (es - 1u)) { chan_error(e, c, E_ALIGN, c->cur); return false; }
```

`sram_ok(a, n)` 는 `a - SRAM_BASE <= SRAM_SIZE - n` 으로 쓴다 — `a + n` 을 계산하면 주소 공간 끝에서 감긴다(ARCH-B11 의 경계 산수와 같은 이유).

실제 실행 (`./so -v`, Part A 부분):

```text
-- A1 mem->mem 64 B, esize 4  (cycles=16 beats=16 idle=0)
   trace 0000000000000000
   irq   @15 ch0 TC d=20000100 n=64
-- A2 gather 13+7+20, IRQ on last  (cycles=40 beats=40 idle=0)
   trace 0000000000000000000000000000000000000000
   irq   @39 ch0 TC d=20000140 n=20
```

| 시나리오 | 비트 수 = 사이클 | 핵심 |
|---|---|---|
| 64 B, 4 B 원소 | 16 | TC 는 마지막 비트의 사이클 15 |
| 64 B, 1 B 원소 | 64 | 같은 데이터, 4 배 버스 사용 — 원소 크기를 버스 폭에 맞추는 이유 |
| gather 13+7+20 (1 B) | 40 | 디스크립터 경계에서 빈 사이클 없음(페치 시간 0). IRQ 를 디스크립터마다 켜면 `@12; @19; @39` |
| 고정 소스, 2 B 원소 64 B | 32 | memset 패턴 채우기 — 소스 하프워드 0xA55A 를 반복 |
| 고정 목적지, 4 B × 4 | 4 | 목적지에는 마지막 원소만 남는다 — 목적지가 TX FIFO 레지스터일 때 쓰는 모드 |

주소 고정(fixed)은 "주변장치 데이터 레지스터는 주소 하나에 FIFO 가 숨어 있다" 를 표현한다. 증가(increment)는 메모리 버퍼. 둘의 조합 4 가지가 mem→mem / periph→mem / mem→periph / periph→periph 이다.

### Part B — 중재: 고정 우선순위 + RR, 그리고 비선점 버스트

```c
static int arbitrate(dma_t *e) {
    for (int lvl = 3; lvl >= 0; lvl--)
        for (int k = 1; k <= NCH; k++) {
            int i = (e->rr_last[lvl] + k) % NCH;
            if (e->ch[i].prio == lvl && chan_ready(&e->ch[i])) { e->rr_last[lvl] = i; return i; }
        }
    return -1;
}
```

실제 트레이스 (`./so -v`):

```text
-- B1 RR, burst 4  (cycles=32 beats=32 idle=0)
   trace 00001111000011110000111100001111
-- B2 prio 2 > prio 1 (RR)  (cycles=24 beats=24 idle=0)
   trace 222222220000111100001111
-- B3 ch1 (prio 3) arrives at cycle 3, ch0 burst 8  (cycles=20 beats=20 idle=0)
   trace 00000000111100000000
   irq   @11 ch1 TC d=20000120 n=4
-- B3 same, ch0 burst 1  (cycles=20 beats=20 idle=0)
   trace 00011110000000000000
   irq   @6 ch1 TC d=20000120 n=4
```

B2 에서 ch2 는 버스트 4 를 두 번 연달아 가진다 — 그 레벨에 혼자라서 RR 이 자기 자신으로 돌아온다. B3 이 이 레슨의 첫 번째 핵심이다:

```svg
<svg viewBox="0 0 740 190" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">B3: prio 3 요청이 사이클 3 에 도착 — prio 0 채널의 버스트가 끝나야 버스를 받는다</text>
  <text x="10" y="50" fill="currentColor" font-size="12">사이클</text>
  <g font-size="12" fill="currentColor">
    <text x="128" y="50">0</text><text x="158" y="50">1</text><text x="188" y="50">2</text><text x="218" y="50">3</text>
    <text x="248" y="50">4</text><text x="278" y="50">5</text><text x="308" y="50">6</text><text x="338" y="50">7</text>
    <text x="368" y="50">8</text><text x="398" y="50">9</text><text x="424" y="50">10</text><text x="454" y="50">11</text>
    <text x="484" y="50">12</text><text x="514" y="50">13</text>
  </g>
  <text x="10" y="82" fill="currentColor" font-size="12">burst 8</text>
  <rect x="120" y="65" width="240" height="26" class="f-mut" stroke="currentColor"/>
  <text x="200" y="83" fill="currentColor" font-size="12">ch0 (prio 0) 8 비트</text>
  <rect x="360" y="65" width="120" height="26" class="f-acc" stroke="currentColor"/>
  <text x="380" y="83" fill="currentColor" font-size="12">ch1 4 비트</text>
  <rect x="480" y="65" width="60" height="26" class="f-mut" stroke="currentColor"/>
  <text x="490" y="83" fill="currentColor" font-size="12">ch0…</text>
  <path d="M225 58 L225 100" stroke="currentColor" stroke-width="2" stroke-dasharray="4 3"/>
  <text x="560" y="83" fill="currentColor" font-size="12">대기 5 사이클</text>
  <text x="10" y="132" fill="currentColor" font-size="12">burst 1</text>
  <rect x="120" y="115" width="90" height="26" class="f-mut" stroke="currentColor"/>
  <text x="130" y="133" fill="currentColor" font-size="12">ch0 ×3</text>
  <rect x="210" y="115" width="120" height="26" class="f-acc" stroke="currentColor"/>
  <text x="230" y="133" fill="currentColor" font-size="12">ch1 4 비트</text>
  <rect x="330" y="115" width="210" height="26" class="f-mut" stroke="currentColor"/>
  <text x="380" y="133" fill="currentColor" font-size="12">ch0 나머지 13 비트…</text>
  <text x="560" y="133" fill="currentColor" font-size="12">대기 0 사이클</text>
  <text x="10" y="172" fill="currentColor" font-size="12">점선 = ch1 요청 도착. 최고 우선순위의 최악 대기 = 다른 채널 버스트 길이 − 1 (+ 버스 대기 상태)</text>
</svg>
```

우선순위는 **누가 다음 그랜트를 받나** 를 정할 뿐, 진행 중인 버스트를 끊지 않는다. 버스트를 길게 잡으면 버스 효율(주소 단계 재사용, DRAM 행 적중)이 오르지만 다른 채널의 지연이 늘어난다. 이 trade-off 가 Part C 의 overrun 으로 이어진다.

고정 우선순위의 그림자: prio 3 의 64 비트 채널과 prio 0 채널을 같이 시작하면 prio 0 의 첫 비트는 사이클 64 (`first_beat == 64` 검사). 높은 채널이 계속 준비돼 있으면 낮은 채널은 **굶는다**. 그래서 실제 컨트롤러는 "레벨은 고정, 레벨 안에서 RR" 이나 "전부 RR" 선택지를 준다 (NXP eDMA 는 고정/RR 을 고르는 비트가 있다고 알려져 있다 — 이름은 문서 확인).

### Part B — 에러: 언제 잡히고 무엇을 남기나

```text
-- B4 bus error mid-transfer  (cycles=5 beats=5 idle=0)
   trace 00000
   irq   @4 ch0 ERR BUS d=20000100 a=20010000 done=16
-- B5 bad next pointer (ch0) / misaligned next (ch1)  (cycles=17 beats=16 idle=1)
   trace 0000000011111111.
   irq   @7 ch0 TC d=20000100 n=8; @8 ch0 ERR DESC d=30000000 a=30000000 done=0; @15 ch1 TC d=20000120 n=8; @16 ch1 ERR DESC d=20000142 a=20000142 done=0
-- B6 ch0 bus error, ch1 continues  (cycles=9 beats=9 idle=0)
   trace 011111111
   irq   @0 ch0 ERR BUS d=20000100 a=40001008 done=0; @8 ch1 TC d=20000120 n=32
```

| 에러 | 잡히는 때 | 옮긴 바이트 | `err_desc` / `err_addr` | 실제 칩에서 비슷한 것 |
|---|---|---|---|---|
| LOOP | 페치 전 (가드 초과) | 0 (이 디스크립터) | 9 번째로 읽으려던 디스크립터 | 하드웨어 가드는 흔하지 않다 — 대개 소프트웨어 책임 (연습 1) |
| DESC | 페치 전 (주소 정렬/범위) | 0 | next 값 그대로 | eDMA 의 scatter-gather 설정 에러(SGE) 계열 |
| CFG / LEN0 / ALIGN | 페치 직후 내용 검사 | 0 | 그 디스크립터 | eDMA NBYTES/CITER 설정 에러, 주소·오프셋 정렬 에러 계열 (이름은 문서 확인) |
| BUS | **비트 도중** | 그 전까지 (`done=16`) | 실패한 버스 주소 | STM32 TEIF, eDMA 소스/목적지 버스 에러 |

B4: dst `0x2000_FFF0`, 4 B 원소 16 개. 4 비트(16 B)는 SRAM 마지막 16 B 에 정상으로 쓰이고, 5 번째 비트의 목적지 `0x2001_0000` 이 미매핑 → 사이클 4 에 에러, `done=16`. 펌웨어는 이 세 값(어느 디스크립터, 어느 주소, 몇 바이트)으로 "어디까지 믿을 수 있나" 를 안다. 이어서 다시 시작하려면 그 디스크립터의 src/dst 를 `done` 만큼 밀고 len 을 줄인 새 디스크립터로 시작하면 된다(재시작 정책은 BM-M08).

B5 는 체인이 반쯤 진행된 뒤의 에러다: d0 의 TC(@7)는 이미 나갔고, 다음 사이클 페치에서 `next = 0x3000_0000` 이 걸렸다. 즉 TC 를 받았다고 체인 전체가 성공한 건 아니다 — 체인 끝은 채널 상태(DONE)나 마지막 디스크립터의 TC 로 판단한다.

B6: ch0 의 소스가 주변장치 블록 안의 매핑 안 된 오프셋 `0x4000_1008` 이라 첫 비트에서 멈췄고, ch1 은 사이클 1 부터 8 비트로 끝났다. **에러는 채널 단위**로 격리된다(엔진 전체를 멈추지 않는다). 루프 시나리오(d0 ↔ d1, 가드 8)는 비트 8 개(32 B)를 정상으로 옮긴 뒤 `@8 ch0 ERR LOOP d=20000100` 으로 멈췄다.

### Part C — 주변장치 → 메모리: DREQ 와 FIFO

`DREQ_SRC` 채널은 FIFO 에 원소 하나 이상 있을 때만 준비 상태다. 그래서 빠른 엔진이 느린 주변장치를 기다리며 빈 사이클을 낸다:

```text
-- C1 UART RX 48 B, period 4, 3 buffers  (cycles=189 beats=48 idle=141)
   trace 0...0...0...0...0...0...0...0...0...0...0...0...0...0...0...0...0...0...0...0...0...0...0...0...…
   irq   @60 ch0 TC d=20000100 n=16; @124 ch0 TC d=20000120 n=16; @188 ch0 TC d=20000140 n=16
-- C2 header/payload split, esize 2  (cycles=64 beats=16 idle=48)
   trace ...0...0...0...0...0...0...0...0...0...0...0...0...0...0...0...0
   irq   @63 ch0 TC d=20000120 n=24
```

C1: 바이트가 사이클 0, 4, …, 188 에 도착하고 같은 사이클에 옮겨진다(생산이 먼저). 16 번째 바이트 = 사이클 60 → 첫 TC. FIFO 최대 레벨 1. C2: 2 B 원소라서 바이트 두 개가 모여야 DREQ 가 선다 — 비트는 4 사이클마다(바이트 간격 2 × 2), 최대 레벨 2. 헤더 8 B 는 첫 버퍼, 나머지 24 B 는 두 번째 버퍼로, IRQ 는 마지막(`d=20000120`)에만. 이게 scatter-gather 를 쓰는 진짜 이유다: CPU 복사 없이 헤더와 페이로드를 다른 곳에 놓는다.

이제 4 KB(1024 비트) 대량 mem→mem 복사(ch0)와 UART RX(ch1, P = 3 사이클/B, FIFO 깊이 4, 48 B)를 같이 돌린다. `./so -v` 실제 출력:

```text
-- C3 bulk prio 2 burst 16 vs uart prio 1: overrun=44 max_level=4 uart_state=1 cycles=20000
-- C3 bulk prio 1 burst 16 vs uart prio 2: overrun=12 max_level=4 uart_state=1 cycles=20000
-- C3 bulk prio 1 burst 16 vs uart prio 1: overrun=12 max_level=4 uart_state=1 cycles=20000
-- C3 bulk prio 1 burst  4 vs uart prio 1: overrun=0 max_level=2 uart_state=2 cycles=1072
-- C3 bulk prio 1 burst  4 vs uart prio 2: overrun=0 max_level=1 uart_state=2 cycles=1072
```

(`uart_state` 1 = BUSY, 2 = DONE. 바이트를 잃은 채널은 len 48 을 못 채워 영원히 기다린다 — 실제 드라이버가 UART RX 에 idle-line 타임아웃을 같이 쓰는 이유. 대량 복사 결과는 다섯 경우 모두 정확했다.)

1. 대량이 더 높으면 UART 는 1024 사이클 동안 굶는다: FIFO 4 개만 남고 44 B 손실.
2. **UART 를 더 높여도 12 B 를 잃는다.** UART 가 FIFO 를 비우고 준비 상태를 잃는 순간 대량 채널이 버스트 16 을 잡고, 그 16 사이클 동안 바이트가 5~6 개 온다 — 깊이 4 를 넘는다. 우선순위로는 못 고친다.
3. 같은 레벨 RR 도 같은 이유로 12.
4. 대량 버스트를 4 로 줄이면 0. (RR 에서 최대 레벨 2 인 것은 시작 사이클 0 에 RR 이 ch0 = 대량 차례였기 때문이다.)

필요한 깊이를 일반화하려고 `demo_fifo_sweep.c` 로 쟀다 (FIFO 깊이를 64 로 넉넉히 두고, 생산 위상 0..P−1 중 최악의 최대 레벨):

<!-- code: demo_fifo_sweep.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_fifo_sweep.c -o /tmp/esn_ARCH-M11/demo_fifo_sweep && /tmp/esn_ARCH-M11/demo_fifo_sweep
```

```text
needed FIFO depth (worst max_level over all phases), UART burst 8
 N\P |   P=2        P=3        P=4        P=8      (cell = hi-prio / equal-RR / ceil(N/P), ! = overrun at depth 64)
  1  |   1 / 1 / 1   1 / 1 / 1   1 / 1 / 1   1 / 1 / 1
  2  |   1 / 2 / 1   1 / 1 / 1   1 / 1 / 1   1 / 1 / 1
  4  |   2 / 3 / 2   2 / 2 / 2   1 / 2 / 1   1 / 1 / 1
  8  |   4 / 5 / 4   3 / 3 / 3   2 / 3 / 2   1 / 2 / 1
 16  |   8 /64!/ 8   6 / 6 / 6   4 / 5 / 4   2 / 3 / 2
 32  |  16 /64!/16  11 /64!/11   8 /47 / 8   4 / 5 / 4
```

(demo 는 `solution.c` 를 `#define main …` 으로 main 만 숨기고 include 한다. 0.02 초.)

두 가지 법칙이 보인다.

**(1) 지연 조건 — UART 가 더 높을 때 필요한 깊이 = ⌈N/P⌉ (24 칸 전부 정확히 일치).** 대량 채널은 UART 가 준비 안 된 사이클 t 에만 버스트를 시작할 수 있다(그 사이클에 바이트가 왔다면 UART 가 이겼다). 버스트가 t..t+N−1 을 차지하고, 사이클 t+N 에는 생산이 팝보다 먼저다. 그래서 쌓이는 바이트 = 연속 N 사이클(t+1..t+N) 동안의 도착 ≤ ⌈N/P⌉. 실제 버스에서는 N 대신 "다른 채널의 버스트가 버스를 잡는 시간"(대기 상태 포함, ARCH-B12)을 넣는다.

**(2) 대역폭 조건 — 같은 레벨 RR 은 먼저 N ≤ (P−1)·Bu 이어야 한다** (Bu = UART 채널 버스트 8). 밀린 데이터가 있으면 한 바퀴는 UART Bu 비트 + 대량 N 비트 = Bu+N 사이클, 그 동안 (Bu+N)/P 바이트가 오므로 Bu 이상이어야 한다. N=16, P=2: 16 > 8 → 매 바퀴 쌓여 64 도 넘침(`!`). N=32, P=4: 32 > 24 → 바퀴당 2 B 씩 쌓여 스트림 200 B 가 끝날 때 47. N=16, P=3 은 16 = 16 경계라 버틴다(6). 조건을 만족하는 칸은 ⌈N/P⌉ 또는 +1 이다(+1 = RR 이 바이트가 이미 기다리는 사이클에 대량 차례를 줄 때). 연습 3 이 Bu ∈ {4, 8} 전체 격자에서 이 두 법칙을 검사한다.

펌웨어 결론: UART·SPI RX·ADC 같은 실시간 스트림 채널은 **우선순위를 올리고, 같은 버스의 대량 채널 버스트를 제한하고, 주변장치 FIFO 문턱값을 그 지연에 맞춘다**. 셋 중 하나만으로는 부족할 수 있다.

### 실제 컨트롤러와 대응

| 개념 | 이 모델 | ARM PL080 (AHB DMAC) | STM32F4 DMA | NXP eDMA |
|---|---|---|---|---|
| 체인 | 20 B 디스크립터, `next` | LLI 4 워드 {src, dst, next LLI, control} | 없음 (더블 버퍼 모드만 — BM-M08) | TCD 32 B, scatter-gather 비트 + 다음 TCD 주소 |
| 길이 단위 | 바이트 | 전송 수 (소스 폭 단위) | NDTR = 데이터 항목 수 | 바이트 수 × 반복 수 |
| 우선순위 | 0..3 + 레벨 안 RR | 채널 번호 고정 우선순위로 알려짐 | PL 4 단계, 같으면 낮은 스트림 번호 | 고정 또는 RR 선택 |
| 완료 IRQ | 디스크립터마다 IRQ 비트 | control 워드의 TC 인터럽트 비트 | TCIF (+ HTIF) | 주 루프 완료 IRQ 비트 |
| 버스 에러 | 채널 정지 + ERR | 에러 인터럽트 | TEIF, 하드웨어가 EN 을 끔 | 에러 상태 레지스터, 채널 에러 IRQ |

표의 레지스터·필드 이름은 각 TRM/레퍼런스 매뉴얼 기억에 기댄 것이라, 비트 위치는 일부러 적지 않았다. 최신 STM32(U5/H5 등)의 GPDMA 는 연결 리스트 모드가 있어 노드가 채널 레지스터 일부를 골라 다시 싣는 방식이라고 알려져 있다. 리눅스에서는 이 모든 엔진이 `dmaengine_prep_slave_sg()` → `dmaengine_submit()` → `dma_async_issue_pending()` 아래로 숨는다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_ARCH-M11/so && /tmp/esn_ARCH-M11/so      # 49/49 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_ARCH-M11/san && /tmp/esn_ARCH-M11/san   # 49/49, 보고 없음
```

환경: `Apple clang version 21.0.0 (clang-2100.0.123.102)`, Apple M6. 사이클은 모델의 사이클이라 호스트 속도와 무관하고 결과가 매번 같다(결정적). 타이밍 측정은 없다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 버스 에러 난 원소도 `left -= esize` | `done` 이 실제보다 커져 펌웨어가 쓰이지 않은 바이트를 믿는다 | 에러면 주소·left·total 을 건드리지 않고 바로 `chan_error` |
| `sram_ok` 를 `a + n <= END` 로 | `a` 가 0xFFFF_FFFx 면 감겨서 통과 | `a - BASE <= SIZE - n` |
| 페치에서 `len == 0` 을 안 봄 | `left == 0` 인 채로 비트를 내면 `left -= esize` 가 감겨 40 억 비트 전송 | 페치 때 E_LEN0 |
| 높은 우선순위가 오면 버스트를 끊음 | 이 모델(그리고 대부분의 버스)의 규칙과 다름 — B3 트레이스가 `000111…` 이 된다 | 그랜트는 `gleft`, 준비 상태, 디스크립터 끝에서만 반납 |
| RR 포인터를 레벨 공용 하나로 | 다른 레벨의 승자가 포인터를 옮겨 같은 레벨 순서가 흔들린다 | `rr_last[lvl]` 레벨마다 |
| 버스트가 디스크립터 경계를 넘게 | 다음 디스크립터가 아직 페치(검증) 안 됐는데 비트를 냄 | 디스크립터 끝에서 그랜트 종료 |
| 주변장치 FIFO 가 꽉 찼을 때 오래된 바이트를 덮음 | 스트림 중간이 빠져 위치가 어긋난다 — UART ORE 는 새 데이터를 버린다 | `count == depth` 면 overrun++ 하고 새 바이트 버림 |
| overrun 을 우선순위로만 고치려 함 | 다른 채널의 버스트 동안은 최고 우선순위도 기다린다 (C3: 12 B 손실) | 버스트 제한 + FIFO 깊이/문턱값 ≥ ⌈N/P⌉ |
| "TC 받았으니 체인 성공" | B5: d0 TC 다음 사이클에 DESC 에러 | 체인 끝 = 채널 DONE 또는 마지막 디스크립터 TC |

## 8. 면접 Q&A

<details><summary>Q1. 디스크립터 기반(scatter-gather) DMA 는 레지스터 기반 DMA 와 무엇이 다르고, 왜 쓰나요?</summary>

레지스터 기반은 CPU 가 채널 레지스터에 src/dst/len 을 한 번 쓰고 끝나면 인터럽트를 받아 다음 것을 다시 쓴다 — 조각마다 CPU 개입과 인터럽트 지연이 끼어든다. 디스크립터 기반은 메모리에 `{src, dst, len, next, flags}` 리스트를 미리 만들어 두고, 엔진이 `next` 를 따라 스스로 다음 조각을 읽어 레지스터에 싣는다. 흩어진 버퍼 모으기(gather: 헤더 + 페이로드 송신), 나눠 받기(scatter: Part C2 헤더 분리), 링 버퍼, 이더넷 디스크립터 링이 모두 이 방식이다. 꼬리: "공짜인가요?" → 아니다. 디스크립터마다 페치가 버스를 쓴다(PL080 LLI 는 4 워드). 연습 4 에서 5 워드 페치를 넣으면 64 B 조각 효율 76.1 %, 1500 B 프레임 98.6 %, 8 B 조각(1 B 원소) 61.5 % 였다. 작은 조각이 많으면 CPU memcpy 가 나을 수도 있다.
</details>

<details><summary>Q2. UART RX 채널을 최고 우선순위로 올렸는데 overrun 이 납니다. 왜죠?</summary>

우선순위는 다음 그랜트를 누가 받느냐만 정하고, 이미 시작된 다른 채널의 버스트를 끊지 않는다. UART 가 FIFO 를 비운 순간(준비 상태가 아님) 대량 채널이 버스트 N 을 잡고, 그 동안 들어오는 바이트가 FIFO 깊이를 넘으면 잃는다. 모델(P = 3, 깊이 4)에서 대량 버스트 16 이면 UART 가 더 높아도 12 B 손실, 버스트 4 면 0. 필요한 깊이는 ⌈N/P⌉ 였다(24 칸 측정 모두 일치). 꼬리: "실제 시스템에서 N 에 무엇을 넣나요?" → 다른 마스터(다른 DMA, CPU, GPU)가 버스를 잡을 수 있는 최장 시간. 대기 상태(ARCH-B12)와 인터커넥트 중재까지 포함한 사이클 수에 바이트 간격을 나눈다. 115200 baud UART 처럼 느린 것은 여유가 크고, 수십 Mbps SPI·병렬 카메라 인터페이스는 여유가 거의 없다.
</details>

<details><summary>Q3. 고정 우선순위와 라운드로빈의 trade-off 는요?</summary>

고정 우선순위는 지연이 예측 가능하지만(최고 채널의 최악 대기 = 다른 채널 버스트 하나) 낮은 채널이 굶을 수 있다 — 모델에서 prio 3 이 64 비트 동안 준비돼 있자 prio 0 의 첫 비트가 사이클 64 였다. RR 은 굶지 않지만 각 채널의 대역폭 몫이 Bu/(ΣB) 로 나뉜다. 그래서 RR 에선 "몫 ≥ 도착률" 이라는 대역폭 조건 N ≤ (P−1)·Bu 가 먼저 성립해야 하고, 깨지면 FIFO 깊이를 아무리 늘려도 결국 넘친다(데모의 `64!`). 꼬리: "섞으면?" → 레벨은 고정, 레벨 안은 RR (이 모델). 실시간 스트림은 높은 레벨, 대량 복사는 낮은 레벨에 두고 버스트를 제한한다.
</details>

<details><summary>Q4. 전송 도중 버스 에러가 나면 하드웨어는 무엇을 남겨야 하고, 펌웨어는 어떻게 복구하나요?</summary>

최소한: 에러 종류, 실패한 디스크립터 주소(체인이면 필수), 실패한 버스 주소, 그 디스크립터에서 확실히 끝난 양(남은 카운트로 표현해도 된다), 그리고 채널 정지. 모델 B4: `ERR BUS d=20000100 a=20010000 done=16`. 펌웨어는 (1) 앞 디스크립터들과 이 디스크립터의 16 B 까지는 유효하다고 보고, (2) 원인(잘못된 버퍼 주소, 꺼진 주변장치 클럭)을 고친 뒤, (3) src/dst 를 16 만큼 민 디스크립터로 이어서 시작하거나 전체를 다시 한다. 꼬리: "읽기는 됐는데 쓰기가 실패하면?" → 그 원소는 끝나지 않은 것으로 센다. 소스가 주변장치 FIFO 면 그 바이트는 이미 팝돼서 **잃었다** — 그래서 주변장치→메모리 경로의 버스 에러는 재시도로 복구되지 않는 데이터 손실로 보고해야 한다.
</details>

<details><summary>Q5. 디스크립터 체인이 루프를 돌면 어떻게 되나요? 일부러 만든 링과는 어떻게 구분하죠?</summary>

하드웨어는 `next` 를 의심하지 않으므로 순환 체인은 영원히 돈다(연습 1 에서 가드 없이 5000 사이클 BUSY). 그런데 의도적인 순환(링 버퍼, 순환 수신)도 같은 모양이라 하드웨어가 "루프 = 에러" 로 볼 수는 없다. 그래서 이 모델은 채널별 선택 가드 `max_desc`(0 = 무제한)를 뒀고, 실제로는 드라이버가 시작 전에 체인을 검사한다: 플로이드(Floyd) 거북이·토끼로 O(1) 메모리, O(n) 시간에 순환과 나쁜 포인터를 찾는다(연습 1). 꼬리: "링이 정상인 경우 완료는 어떻게 알리죠?" → 디스크립터마다 IRQ 를 켜거나 반/전체 지점 IRQ 를 쓴다 — 그 드라이버 쪽 설계는 BM-M08.
</details>

<details><summary>Q6. 주변장치→메모리에서 소스 주소는 왜 고정이고, 원소 크기는 무엇으로 정하나요?</summary>

주변장치 데이터 레지스터는 주소 하나 뒤에 FIFO 가 있다 — 읽을 때마다 다음 데이터가 나온다. 주소를 증가시키면 옆 레지스터(상태, 설정)를 읽게 된다. 원소 크기는 주변장치 데이터 폭에 맞춘다: 8 비트 UART 는 1 B, 12/16 비트 ADC 는 2 B. 모델 C2 에서 2 B 원소면 DREQ 가 바이트 두 개가 모일 때 서고 비트 수는 절반이 된다(16 비트). 꼬리: "정렬 에러는 왜 페치 때?" → 원소 크기, src, dst, len 이 디스크립터에 다 있으니 비트를 내기 전에 확정할 수 있다. 버스 에러는 주소가 실제로 디코드될 때만 안다(ARCH-B11).
</details>

<details><summary>Q7. 완료 인터럽트를 디스크립터마다 켤까요, 마지막에만 켤까요?</summary>

마지막에만 켜면 인터럽트 수가 최소지만 중간 진행을 모른다. 디스크립터마다 켜면 각 버퍼를 일찍 처리할 수 있지만(오디오·스트림의 핑퐁) ISR 비용이 조각 수만큼 든다. 모델 A2: 마지막만 `@39` 한 번, 모두 켜면 `@12; @19; @39`. 꼬리: "TC 가 왔을 때 데이터가 정말 메모리에 있나요?" → 엔진 쪽에서는 마지막 쓰기의 응답을 받은 뒤 TC 를 내야 한다(posted write 라면 아직 인터커넥트 안에 있을 수 있다). CPU 쪽에서는 캐시가 있으면 invalidate 가 필요하다 — ARCH-M12.
</details>

<details><summary>Q8. 실리콘 검증에서 DMA 엔진을 어떻게 테스트하나요?</summary>

이 레슨의 모델이 그대로 골든 모델(reference model)이 된다. (1) 결정적 시나리오: 결과 버퍼 비교 + IRQ 순서 로그 문자열 비교(이 레슨 49 개 검사의 방식). (2) 무작위: 시드 고정 PRNG 로 디스크립터 개수·길이·원소 크기·정렬·우선순위·버스트를 뽑아 RTL/실리콘과 모델을 비교. (3) 에러 주입: 미매핑 주소, len 0, 루프, 체인 중간의 나쁜 next — 에러 종류·디스크립터·done 이 기대값과 정확히 같은지. (4) 성능·지연: 경쟁 채널을 걸고 최고 우선순위 채널의 최대 대기와 FIFO 최대 레벨을 측정해 ⌈N/P⌉ 같은 분석값과 비교. 꼬리: "사이클이 모델과 안 맞으면?" → 이 모델은 사이클 근사다. 데이터·순서·에러는 정확히 맞춰야 하고, 사이클은 범위(상한)로 검사한다 — 정밀한 사이클 모델은 SIM-M01 의 주제.
</details>

## 9. 추가 연습

### 연습 1 — 시작 전 체인 검사 (플로이드 순환 검출)

`int chain_check(uint32_t first, uint32_t *count)`: 0 = 정상(디스크립터 수), −1 = 순환, −2 = 나쁜 포인터(4 B 정렬 아님 / 20 B 가 SRAM 밖). 추가 메모리 O(1). 5 개 선형, 꼬리 → 몸통 순환, 자기 자신, 정렬 안 된 next, 미매핑 next 로 검사하고, 순환 체인을 가드 없이 엔진에 넣으면 어떻게 되는지 확인하라.

<details><summary>답</summary>

fast 가 두 칸씩 가며 지나가는 노드를 모두 검사하고(`next == 0` 이면 선형 확정), slow 는 한 칸 — slow 는 fast 가 이미 검사한 노드만 밟으므로 따로 검사할 필요가 없다. 둘이 만나면 순환. 선형이면 한 번 더 걸으며 센다. 결과: 5 개 → 0/5, d4→d1 → −1, d0→d0 → −1, next `0x2000_0F02` → −2, `0x5000_0000` → −2, `0x2000_FFF0`(20 B 가 끝을 넘음) → −2. 가드 0 인 엔진에 3 노드 순환을 넣으면 5000 사이클 뒤에도 BUSY — 하드웨어는 순환을 모른다. 하드웨어 홉 카운터(Part B 의 `max_desc`)는 의도적 링과 구분 못 해서 끌 수 있어야 하고, 소프트웨어 검사는 링을 만드는 드라이버가 "의도한 순환" 을 알고 건너뛸 수 있다.
</details>

### 연습 2 — 메모리 → 주변장치 TX: 목적지 쪽 DREQ

TX FIFO(깊이 4, 선로가 4 사이클마다 1 B 를 뺌)를 주변장치에 추가하고, 플래그 `DREQ_DST`(bit9) = "TX FIFO 빈자리가 원소 하나 이상일 때만 준비" 를 엔진에 넣어라. 32 B 를 보낼 때 트레이스 앞부분, TC 사이클, 그리고 `DREQ_DST` 없이 보냈을 때 잃는 바이트 수를 구하라.

<details><summary>답</summary>

`chan_ready` 에 `&& (!(flags & DREQ_DST) || tx_depth - txcount >= esize)` 한 줄, DR 쓰기는 TX FIFO 에 넣고(꽉 차면 `tx_overflow++`), `periph_tick` 이 사이클 시작에 선로 쪽으로 1 B 를 뺀다. 결과: 트레이스 `00000...0...0...` — 처음 4 비트로 FIFO 를 채우고, 사이클 4 에 선로가 하나 빼자 5 번째, 그 뒤로는 선로 속도(4 사이클에 1 비트)로 맞춰진다. 마지막 비트 = 4 + 4·27 = 사이클 112 → `@112 ch0 TC d=20000100 n=32`. **TC 시점에 선로로 나간 건 28 B 뿐** 이고 4 B 는 아직 FIFO 안이다 — "DMA 완료 = 전송 완료" 가 아니다(UART 는 TC 플래그로 시프트 레지스터까지 비었는지 따로 본다). DREQ 없이 쓰면 32 사이클 동안 32 번 쓰고, FIFO 가 받아 준 건 처음 4 + 선로가 뺀 7 = 11 → **21 B 손실**.
</details>

### 연습 3 — FIFO 깊이 공식을 격자 전체에서 검사

`need_depth_hi(N, P) = ⌈N/P⌉` 와 `rr_bandwidth_ok(N, P, Bu) = N ≤ (P−1)·Bu` 를 쓰고, N ∈ {1,2,4,8,16,32} × P ∈ {2,3,4,8} 에서 시뮬레이션과 비교하라: UART 가 높을 때 최악 최대 레벨 == ⌈N/P⌉, 같은 레벨 RR(Bu ∈ {4, 8})에서는 대역폭 조건이 참이면 최대 레벨 ≤ ⌈N/P⌉ + 1, 거짓이면 그보다 크다(스트림 200 B).

<details><summary>답</summary>

지연 조건의 근거는 5. 풀이 Part C: 대량 채널은 UART 가 준비 안 된 사이클에만 버스트를 시작하므로 쌓이는 건 연속 N 사이클의 도착 수. 대역폭 조건은 밀린 상태에서 한 바퀴(Bu+N 사이클) 동안 도착 (Bu+N)/P ≤ 처리 Bu. 실행해 보면 72 개 비교(hi 24 칸 + RR 24 칸 × Bu 2 가지)가 전부 맞는다 — 경계 칸 N=16, P=3, Bu=8 (16 = 16)은 최대 6 으로 버티고, Bu=4 이면 (16 > 8) 계속 쌓인다. 실무 공식으로 옮기면: 필요 깊이 ≥ ⌈(다른 마스터의 최장 버스 점유 사이클) / (바이트 간격 사이클)⌉ + 여유 1, 그리고 RR 이면 대역폭 몫부터 확인.
</details>

### 연습 4 — 페치 비용을 넣은 scatter-gather 효율

디스크립터 하나를 읽는 데 버스 비트 F = 5 개가 든다고 하자. `sg_eff_permille(lens, n, esize, F)` = 1000 · 데이터 비트 / (데이터 비트 + F·n) 과, 조각 하나의 효율이 목표 이상이 되는 최소 조각 크기 `min_frag_bytes(esize, F, permille)` 를 구하라.

<details><summary>답</summary>

b/(b+F) ≥ x ⇔ b ≥ x·F/(1−x) → 비트 수 b = ⌈permille·F / (1000 − permille)⌉, 바이트 = b·esize. 값: 64 B/4 B 원소 → 16/21 = 761‰, 1500 B/4 B → 375/380 = 986‰, 8 B/1 B → 8/13 = 615‰, Part A 의 gather(13+7+20, 1 B) → 40/55 = 727‰. 4 B 원소로 80 % 를 넘기려면 조각 ≥ 80 B, 90 % 면 ≥ 180 B. 정수 나눗셈 함정: 천분율을 내림하므로 비교는 무차별 탐색(b 를 1 부터 올림)과 맞춰 검사했다. 실제 엔진은 페치를 앞 디스크립터 전송과 겹치기도(prefetch) 해서 이보다 낫다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_ARCH-M11/pr && /tmp/esn_ARCH-M11/pr
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o /tmp/esn_ARCH-M11/prsan && /tmp/esn_ARCH-M11/prsan
```

실제 출력: `18/18 passed` (두 빌드 모두, 새니타이저 보고 없음). practice.c 는 본문 모델 전체 + TX FIFO/`DREQ_DST` 를 담은 독립 파일이다.

## 10. 핵심 정리

- DMA 엔진 = **페치(next 따라 디스크립터 싣기) → 비트(원소 읽고 쓰기, 주소 증가/고정) → 완료(TC, next 또는 DONE)** 의 반복. 디스크립터 = `{src, dst, len, next, flags}`.
- 검증은 두 시점: 설정 에러(LOOP·DESC·CFG·LEN0·ALIGN)는 **페치 때, 0 바이트**. 버스 에러는 **비트 때, 부분 전송** — 채널 정지 + (디스크립터, 주소, done) 기록. 에러는 채널 단위로 격리.
- 중재 = 레벨 고정 우선순위 + 레벨 안 RR. **버스트는 비선점** → 최고 우선순위의 최악 대기 = 다른 채널 버스트 길이 − 1 사이클 (B3: 버스트 8, 사이클 3 도착 → 5 사이클 대기). 고정 우선순위는 굶주림을 만든다.
- 주변장치 경로: DREQ = FIFO 에 원소가 있음. overrun 조건 두 개: 지연(필요 깊이 ⌈N/P⌉, 모델 24 칸 일치)과 RR 대역폭(N ≤ (P−1)·Bu). 우선순위만 올려서는 못 고친다 (C3: 12 B 손실).
- TC ≠ "체인 성공"(B5), TC ≠ "선로 전송 끝"(연습 2), 그리고 캐시가 있으면 TC ≠ "CPU 가 볼 수 있음"(ARCH-M12).
- 순환 체인은 하드웨어가 모른다: 선택 가드 또는 시작 전 플로이드 검사. 작은 조각은 페치 비용으로 효율이 떨어진다(5 워드 페치: 64 B 76 %, 1500 B 99 %).

## 참고자료

- Arm, *PrimeCell DMA Controller (PL080) Technical Reference Manual* (DDI 0196) — LLI 구조, 채널 우선순위, 주변장치 요청 신호 (이번에 원문 확인 안 함: 필드·신호 이름은 기억 기준)
- Arm, *CoreLink DMA-330 (PL330) TRM* (DDI 0424) — 마이크로코드 방식 DMAC, 이 모델과 다른 설계의 예 (원문 확인 안 함)
- STMicroelectronics, RM0090 *STM32F405/415, 407/417, 427/437, 429/439 Reference Manual* — DMA 스트림 우선순위, FIFO, 버스트, TEIF/FEIF/DMEIF (원문 확인 안 함)
- STMicroelectronics, STM32U5 / STM32H5 레퍼런스 매뉴얼의 GPDMA 장 — linked-list 모드 (원문 확인 안 함)
- NXP, i.MX RT / Kinetis 레퍼런스 매뉴얼의 eDMA 장 — TCD, scatter-gather, 채널 중재, 에러 상태 (원문 확인 안 함)
- Linux `Documentation/driver-api/dmaengine/` (provider.rst, client.rst) — `dmaengine_prep_slave_sg`, 디스크립터와 콜백: <https://docs.kernel.org/driver-api/dmaengine/index.html>
- R. W. Floyd 의 순환 검출(거북이와 토끼) — Knuth, *The Art of Computer Programming* Vol. 2 의 난수 생성 장(3.1 절)이 Floyd 에게 돌리는 것으로 널리 인용됨 (문항 번호 미확인)
- 관련 레슨: ARCH-B11 (메모리 맵·버스 폴트 — 이 모델의 BUS 에러 디코드), ARCH-B12 (대기 상태 — 버스트 점유 시간), ARCH-M12 (DMA 와 캐시 일관성 — TC 뒤의 invalidate), ARCH-M13 (AHB/APB), ARCH-M14 (AXI 채널·응답), SOCX-B02 (AXI 버스트 주소), BM-M08 (DMA 드라이버: circular, HT/TC — 다음 단계), SIM-M01 (사이클 근사 모델), DS-A01 (DMA 생산자 링), SIV-M02 (DMA 검증)
