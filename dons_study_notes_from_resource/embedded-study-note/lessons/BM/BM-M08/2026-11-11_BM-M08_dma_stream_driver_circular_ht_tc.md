---
id: BM-M08
time: 90분
updated: 2026-11-11
---
# BM-M08 DMA 엔진 드라이버: 채널 설정, circular, HT/TC 인터럽트

## 학습 목표

- STM32F4 DMA 스트림 레지스터(SxCR / SxNDTR / SxPAR / SxM0AR)를 보고 **"이 설정이면 무엇이 몇 번 어디로 옮겨지나"** 를 말하고, `LISR/HISR` 의 불규칙한 플래그 위치(0 / 6 / 16 / 22)를 표 없이 계산할 수 있다.
- 스트림 설정 절차를 순서대로 말하고 각 단계가 빠지면 무엇이 깨지는지 재현할 수 있다: **EN 이 0 으로 "읽힐" 때까지 기다리기**, **그 스트림의 플래그 전부 지우기**, 그다음 주소·NDTR·CR.
- HT(half-transfer) / TC(transfer-complete) 가 언제 서는지(NDTR == N/2, NDTR == 0), normal 과 circular 의 차이(EN 을 HW 가 끄나, NDTR 을 재장전하나)를 모델로 확인한다.
- ISR 에서 플래그를 **"본 것만, 이 스트림 것만, 콜백 전에"** 지워야 하는 이유를 tick 단위 추적으로 설명한다.
- "DMA TC = 전송 끝" 이 아님(USART TC 와 다름), "NDTR 이 안 움직인다 = 요청이 안 온다" 같은 기동(bring-up) 진단을 할 수 있다.

## 1. 문제

DMA 는 펌웨어 면접의 단골이다. "UART 를 DMA 로 보내고 콜백에서 RS-485 방향 핀을 바꿨더니 마지막 바이트가 깨져요", "재설정했는데 첫 인터럽트가 바로 떠요", "circular 버퍼에서 가끔 반쪽이 빠져요" — 전부 **레지스터 순서 하나** 를 틀린 버그다. SSD 컨트롤러에서 NVMe 큐 · 데이터 버퍼를 DMA 디스크립터로 옮겨 본 Don 에게 MCU DMA 는 훨씬 단순하다: 디스크립터 체인 대신 스트림마다 레지스터 한 벌, 완료 통지는 플래그 + 레벨 인터럽트. 대신 그 단순한 레지스터에 함정이 몰려 있다.

이 레슨은 맥에서 돈다. RM0090(STM32F4) DMA2 레이아웃을 그대로 흉내 내는 **동작 모델(behavioral model)** 을 만들고, 그 위에서 진짜 펌웨어처럼 쓴 드라이버를 시험한다. 주소·비트는 "RM0090 기준 대표값 — RM 확인 권장" 이고, **테스트의 정답은 이 모델의 정의** 다.

| 모델 부품 | 이 레슨의 정의 |
|---|---|
| 시간 | CPU 버스 접근 1회 = 1 tick, `sim_step()` 1회 = 1 tick + IRQ 하나 실행. 콜백의 CPU 일은 `sim_burn(k)` |
| DMA2 | 스트림 8개. tick 마다 **요청이 있는 스트림 중 PL 높은 것, 같으면 번호 낮은 것** 이 항목(item) 하나를 옮긴다 |
| 항목 | PSIZE 바이트 (byte/halfword/word). PSIZE = MSIZE (direct mode, FIFO 모델 없음) |
| HT / TC | NDTR 이 N/2(내림)이 되는 순간 HTIF, 0 이 되는 순간 TCIF. normal → HW 가 EN=0, circular → NDTR·주소 재장전 |
| EN=0 쓰기 | 진행 중 전송 마무리에 3 tick (`DMA_DISABLE_LAT`). 그동안 EN 은 1 로 읽히고 설정 레지스터 쓰기는 무시. 끝나면 TCIF (abort) |
| EN=1 동안 CR 쓰기 | IE 비트(DMEIE/TEIE/HTIE/TCIE)와 EN 끄기만 먹는다 |
| 버스 에러 | SRAM(0x2000_0000, 32 KB)·USART1 밖 주소 → TEIF + HW 가 EN=0 |
| 요청 | USART1: TX = 스트림 7 채널 4 (CR3.DMAT && TXE), RX = 스트림 2 또는 5 채널 4 (CR3.DMAR && RXNE). M→M 은 항상 요청 |
| IRQ | 라인 = (그 스트림 플래그 & 켜진 IE) 의 OR — **레벨**. 지우지 않으면 다시 들어온다 (NVIC-lite) |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">모델 구조: 드라이버는 REG_READ/WRITE 로만, DMA 는 자기 버스로</text>
  <rect x="10" y="34" width="190" height="112" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor" font-weight="bold">펌웨어 (main + ISR)</text>
  <text x="20" y="72" fill="currentColor">dma_stream_config()</text>
  <text x="20" y="88" fill="currentColor">dma_stream_disable()</text>
  <text x="20" y="104" fill="currentColor">dma_irq_handler()</text>
  <text x="20" y="120" fill="currentColor">uart1_tx/rx_dma_start()</text>
  <text x="20" y="138" fill="currentColor">→ 콜백 half / full / error</text>
  <rect x="250" y="34" width="220" height="150" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="260" y="54" fill="currentColor" font-weight="bold">DMA2 모델 (0x4002_6400)</text>
  <text x="260" y="72" fill="currentColor">LISR/HISR (r) · LIFCR/HIFCR (w1)</text>
  <text x="260" y="88" fill="currentColor">S0..S7: CR NDTR PAR M0AR</text>
  <text x="260" y="104" fill="currentColor">내부: n0, pcur, mcur (안 보임)</text>
  <text x="260" y="120" fill="currentColor">중재: PL ↓, 같으면 번호 ↑</text>
  <text x="260" y="136" fill="currentColor">tick 당 항목 1개</text>
  <text x="260" y="152" fill="currentColor">NDTR==N/2 → HTIF</text>
  <text x="260" y="168" fill="currentColor">NDTR==0 → TCIF (+재장전/EN=0)</text>
  <rect x="520" y="34" width="210" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="530" y="54" fill="currentColor" font-weight="bold">SRAM 0x2000_0000 (32 KB)</text>
  <text x="530" y="72" fill="currentColor">버퍼 · 소스 · 목적지</text>
  <text x="530" y="90" fill="currentColor">그 밖 = 버스 에러 (TEIF)</text>
  <rect x="520" y="124" width="210" height="80" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="530" y="144" fill="currentColor" font-weight="bold">USART1 (0x4001_1000)</text>
  <text x="530" y="162" fill="currentColor">TDR + 시프트 (10 tick/바이트)</text>
  <text x="530" y="178" fill="currentColor">TXE·RXNE → DMA 요청</text>
  <text x="530" y="194" fill="currentColor">SR.TC = 선 위가 비었음</text>
  <path d="M200 90 L250 90" stroke="currentColor" fill="none"/>
  <text x="203" y="84" fill="currentColor">R/W</text>
  <path d="M470 70 L520 70" stroke="currentColor" fill="none"/>
  <path d="M470 160 L520 160" stroke="currentColor" fill="none"/>
  <text x="474" y="64" fill="currentColor">bus</text>
  <path d="M520 186 L470 186" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <text x="474" y="200" fill="currentColor">요청</text>
  <rect x="250" y="230" width="220" height="70" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="260" y="250" fill="currentColor" font-weight="bold">NVIC-lite</text>
  <text x="260" y="268" fill="currentColor">IRQ 56..60, 68..70 = S0..S7</text>
  <text x="260" y="286" fill="currentColor">레벨: flags &amp; IE ≠ 0</text>
  <path d="M360 184 L360 230" stroke="currentColor" fill="none"/>
  <path d="M250 265 L105 265 L105 146" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <text x="112" y="258" fill="currentColor">sim_step(): 번호 낮은 것 하나 실행</text>
  <text x="10" y="320" fill="currentColor">CPU 접근마다 시간이 흐르므로 ISR 이 레지스터를 읽고 쓰는 사이에도 DMA 는 계속 옮긴다 — 레이스가 재현되는 이유.</text>
</svg>
```

플래그 레지스터는 스트림 4개씩 나눠 담는다. 한 스트림 = 6비트 그룹, 그런데 그룹 시작이 0, 6, 16, 22 — 하위 16비트와 상위 16비트에 두 스트림씩 넣고 12–15, 28–31 은 예약이다. 이 불규칙함이 면접 질문이 된다.

```svg
<svg viewBox="0 0 740 215" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">DMA_LISR (스트림 0..3) / DMA_HISR (스트림 4..7) — 같은 배치</text>
  <rect x="40" y="34" width="84" height="34" class="f-mut" stroke="currentColor"/>
  <text x="52" y="56" fill="currentColor">31:28 예약</text>
  <rect x="124" y="34" width="126" height="34" class="f-acc" stroke="currentColor"/>
  <text x="134" y="56" fill="currentColor">S3/S7 [27:22]</text>
  <rect x="250" y="34" width="126" height="34" class="f-acc" stroke="currentColor"/>
  <text x="260" y="56" fill="currentColor">S2/S6 [21:16]</text>
  <rect x="376" y="34" width="84" height="34" class="f-mut" stroke="currentColor"/>
  <text x="388" y="56" fill="currentColor">15:12 예약</text>
  <rect x="460" y="34" width="126" height="34" class="f-acc" stroke="currentColor"/>
  <text x="470" y="56" fill="currentColor">S1/S5 [11:6]</text>
  <rect x="586" y="34" width="126" height="34" class="f-acc" stroke="currentColor"/>
  <text x="596" y="56" fill="currentColor">S0/S4 [5:0]</text>
  <text x="40" y="84" fill="currentColor">bit 31</text>
  <text x="690" y="84" fill="currentColor">bit 0</text>
  <path d="M586 68 L220 110" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <path d="M712 68 L580 110" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <rect x="220" y="110" width="60" height="34" class="f-ok" stroke="currentColor"/>
  <text x="232" y="132" fill="currentColor">TCIF</text>
  <rect x="280" y="110" width="60" height="34" class="f-ok" stroke="currentColor"/>
  <text x="292" y="132" fill="currentColor">HTIF</text>
  <rect x="340" y="110" width="60" height="34" class="f-warn" stroke="currentColor"/>
  <text x="352" y="132" fill="currentColor">TEIF</text>
  <rect x="400" y="110" width="60" height="34" class="f-warn" stroke="currentColor"/>
  <text x="408" y="132" fill="currentColor">DMEIF</text>
  <rect x="460" y="110" width="60" height="34" class="f-mut" stroke="currentColor"/>
  <text x="482" y="132" fill="currentColor">-</text>
  <rect x="520" y="110" width="60" height="34" class="f-warn" stroke="currentColor"/>
  <text x="534" y="132" fill="currentColor">FEIF</text>
  <text x="240" y="162" fill="currentColor">+5          +4          +3          +2          +1          +0</text>
  <text x="10" y="186" fill="currentColor">그룹 마스크 0x3D. 스트림 7 TC = HISR bit 22+5 = 27, 지우기 = HIFCR 에 1 &lt;&lt; 27.</text>
  <text x="10" y="204" fill="currentColor">네 그룹 합 = 0x0F7D0F7D (유효 비트 전부).</text>
</svg>
```

### Part A — 레지스터 계산과 기본 전송

1. `dma_flag_bit(s, f)`, `dma_isr_reg(s)`, `dma_ifcr_reg(s)` — 8 스트림 × 5 플래그를 표와 **전수 비교**.
2. `dma_cr_value(cfg)` — CHSEL[27:25], PL[17:16], MSIZE[14:13], PSIZE[12:11], MINC 10, PINC 9, CIRC 8, DIR[7:6], TCIE 4, HTIE 3, TEIE 2. EN 은 켜지 않는다. 손으로 푼 값 3개 + 18432 조합 필드 왕복 검사.
3. 메모리→메모리 37 항목 복사 (byte / halfword / word, 스트림 0 / 3 / 6): 데이터, 가드 바이트, NDTR = 0, EN = 0, IE 없이도 HTIF|TCIF 가 서 있는지. N = 1..64 에서 HT 위치. PINC = 0 으로 고정 소스 채우기(memset). 무작위 3000건.

### Part B — 면접관이 찌르는 함정

1. EN = 1 인 스트림에 `dma_stream_config` → `DMA_EBUSY`, **버스 쓰기 0회** (접근 로그로 확인).
2. `dma_stream_disable` — EN 과 IE 를 끄고, EN 이 0 으로 읽힐 때까지 폴링(상한 64). abort 후 NDTR 이 남은 양, TCIF 가 서는 것, 가짜 "완료" 콜백이 없는 것.
3. 버그 재현: 기다리지 않고 재설정 → **절반만 적용** / 플래그 안 지우고 재시작 → **0 바이트에서 "완료"**.
4. TEIF: 목적지 0x3000_0000, 소스가 SRAM 끝을 넘는 경우 — NDTR 이 멈춘 곳을 알려 준다.
5. 입력 검증: 정렬 안 된 주소, NDTR 0, M→M + circular, DMA1 의 M→M, 범위 밖 값 → `DMA_EINVAL` (버스 접근 없이). 드라이버를 우회한 홀수 주소는 HW 가 정렬해 가드를 덮는다.
6. `dma_irq_handler` — 플래그 클리어 방식 세 가지 버그(콜백 뒤 전체 클리어, `LIFCR = LISR`, 안 지움)를 정답과 비교.

### Part C — 펌웨어처럼

1. `uart1_tx_dma_start(buf, n)` — 스트림 7 / 채널 4. **DMA TC 콜백 시점에 USART TC 는 0** 이고 선 위에 바이트가 남아 있음을 단언, `SR.TC` 를 기다린 뒤에야 74 바이트가 전부 나간다.
2. `uart1_rx_dma_start(buf, n)` — 스트림 2 circular, 4096 바이트를 N = 16 버퍼로: HT/TC 가 H F H F … 512번, 256 바퀴, ORE 0.
3. CHSEL 실수 · DMAT 안 켬 → NDTR 이 2000 tick 동안 그대로. `-v` 로 기동 로그.

## 2. Problem (English)

> **DMA stream driver for an STM32F4-style DMA controller.** You get a register-accurate behavioral model of DMA2 (RM0090 layout: `LISR/HISR`, `LIFCR/HIFCR`, per-stream `SxCR/SxNDTR/SxPAR/SxM0AR` at `0x10 + 0x18·s`) plus a USART1 model and a tiny NVIC whose DMA lines are level-sensitive (`line = flags & enabled-IE`).
>
> ```c
> unsigned dma_flag_bit(unsigned s, unsigned f);     // bit of flag f (FE0 DME2 TE3 HT4 TC5) of stream s in LISR/HISR
> uint32_t dma_isr_reg(unsigned s); uint32_t dma_ifcr_reg(unsigned s);
> uint32_t dma_cr_value(const dma_cfg_t *c);        // SxCR without EN
> int  dma_stream_disable(uint32_t base, unsigned s);           // clear EN (+IEs), wait until EN reads 0 (bounded)
> int  dma_stream_config(uint32_t base, const dma_cfg_t *c);    // validate; EBUSY if EN=1; clear this stream's flags; PAR, M0AR, NDTR, CR
> void dma_irq_handler(uint32_t base, unsigned s);              // clear exactly the seen+enabled flags, then error | half, full
> int  uart1_tx_dma_start(uint32_t buf, uint16_t n);            // DMA2 S7 ch4, M2P
> int  uart1_rx_dma_start(uint32_t buf, uint16_t n);            // DMA2 S2 ch4, P2M circular, HT+TC
> ```
>
> Constraints: no busy loop without a bound; reject (no bus access) stream/channel > 7, NDTR = 0, addresses not aligned to the item size, memory-to-memory with circular mode or on DMA1. Never write configuration registers while EN reads 1.
>
> Examples: `dma_flag_bit(7, TCIF) = 27` (HISR). TX config → `SxCR = 0x08010454`; RX circular config → `0x0802051C`. A 74-byte TX: the DMA TC callback runs when NDTR = 0 but only 72 bytes are on the wire and `USART_SR.TC = 0`; TC rises 17 status polls later. An ISR that clears the whole flag group *after* an 11-tick half-transfer callback loses 64 of 64 TC events in a 512-byte stream; the correct handler loses none.
>
> Follow-ups: why must flags be cleared before enabling? What does the TCIF after an abort mean? Why does NDTR count items, not bytes? HT and TC both set in one ISR entry — which came first?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter     # 25/92 passed, exit 1
./starter -v                                                                # (정답이면) 기동 로그
```

시작 코드 그대로면 `25/92 passed` — 버그 재현 테스트(하네스가 직접 레지스터를 쓰는 것)와 스트림 0 플래그 일부만 통과한다. TODO 는 7개: A1 플래그 위치 3함수, A2 `dma_cr_value`, B1 `dma_stream_disable`, B2 `dma_stream_config`, B3 `dma_irq_handler`, C1/C2 UART 시작 함수. 시작 코드의 핸들러는 플래그를 안 지워서 IRQ 가 매 step 다시 뜨지만, 모든 루프에 상한이 있어 멈추지 않는다.

관찰용 파일 (레슨 폴더에서 — `solution.c` 를 `#include` 한다):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_trace.c -o demo_trace && ./demo_trace   # 클리어 버그 tick 추적
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o demo_sweep && ./demo_sweep   # 콜백 비용 sweep 등 표 3개
```

## 4. 힌트

<details>
<summary>힌트 1 — A1: 오프셋은 표로, 레지스터는 s &lt; 4</summary>

`{0, 6, 16, 22}[s & 3]` + 그룹 안 비트. 스트림 4..7 은 HISR/HIFCR (+4). 분기 없는 식도 있다(연습 1): `((s & 2) << 3) + (s & 1) * 6`.

</details>

<details>
<summary>힌트 2 — A2: PSIZE 와 MSIZE 에 같은 값</summary>

필드마다 `(uint32_t)값 << Pos`. direct mode 에서는 MSIZE 가 PSIZE 를 따라가지만(RM 확인 권장) 레지스터에는 둘 다 써 두는 게 읽는 사람에게 정직하다. EN 은 따로 켠다 — 설정과 시작을 분리해야 "설정 → 플래그 클리어 → 시작" 순서를 지킬 수 있다.

</details>

<details>
<summary>힌트 3 — B1: EN=0 은 "요청" 이다</summary>

0 을 쓴 순간 꺼지는 게 아니라, 진행 중이던 전송이 끝나야 EN 이 0 으로 읽힌다. 읽기 루프에 상한을 두고, IE 비트를 같은 쓰기에서 꺼 두면 abort 가 세우는 TCIF 가 "완료" 콜백으로 둔갑하지 않는다.

</details>

<details>
<summary>힌트 4 — B2: 순서 = 검증 → EN 확인 → IFCR → PAR/M0AR/NDTR → CR</summary>

거부는 버스 접근 전에. EN=1 이면 아무것도 쓰지 말고 `DMA_EBUSY` (조용히 꺼 버리는 것은 호출자 몫). IFCR 에는 **그 스트림 그룹만**: `0x3D << dma_flag_bit(s, 0)`.

</details>

<details>
<summary>힌트 5 — B3: CR 의 IE 비트를 한 칸 올리면 플래그 비트</summary>

DMEIE/TEIE/HTIE/TCIE = CR 비트 1..4, DMEIF/TEIF/HTIF/TCIF = 그룹 비트 2..5. `ie_flags = (CR & 0x1E) << 1`. `seen = 그룹 & ie_flags` 를 **콜백 전에** IFCR 로 지우면, 콜백 도중 새로 선 플래그는 남아서 다음 진입에서 처리된다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "스트림이 켜져 있으면 끄고, **EN 이 0 으로 읽힐 때까지** 기다립니다."
2. "그 스트림의 플래그를 IFCR 로 전부 지웁니다 — 안 그러면 이전 전송의 TCIF 때문에 IE 를 켜자마자 인터럽트가 뜹니다."
3. "PAR, M0AR, NDTR(항목 수), CR(채널, 방향, 크기, 증가, circular, 우선순위, IE) 를 쓰고 마지막에 EN."
4. "ISR 은 플래그를 읽고, 본 것만 지우고, 콜백. TC 는 '마지막 항목을 옮겼다' 일 뿐 주변장치가 끝났다는 뜻이 아닙니다."

### Part A — 플래그 위치, CR 값, 한 번의 전송

| 스트림 | 상태 / 클리어 | 그룹 시작 | TCIF | HTIF | TEIF | 그룹 마스크 |
|---|---|---|---|---|---|---|
| 0 / 4 | LISR·LIFCR / HISR·HIFCR | 0 | 5 | 4 | 3 | 0x0000003D |
| 1 / 5 | 〃 | 6 | 11 | 10 | 9 | 0x00000F40 |
| 2 / 6 | 〃 | 16 | 21 | 20 | 19 | 0x003D0000 |
| 3 / 7 | 〃 | 22 | 27 | 26 | 25 | 0x0F400000 |

`dma_cr_value` 의 세 벡터를 손으로 풀면 (테스트가 단언하는 값):

| 설정 | CHSEL | PL | 크기 | MINC/PINC/CIRC | DIR | IE | SxCR |
|---|---|---|---|---|---|---|---|
| USART1 TX (S7) | 4 → 0x0800_0000 | 1 → 0x1_0000 | byte → 0 | MINC 0x400 | M→P 01 → 0x40 | TC 0x10 + TE 0x04 | **0x08010454** |
| USART1 RX (S2) | 4 → 0x0800_0000 | 2 → 0x2_0000 | byte → 0 | MINC 0x400 + CIRC 0x100 | P→M 00 | TC+HT+TE 0x1C | **0x0802051C** |
| M→M word | 0 | 3 → 0x3_0000 | word: 0x4000 + 0x1000 | MINC + PINC 0x600 | M→M 10 → 0x80 | 없음 | **0x00035680** |

18432 조합(채널 8 × PL 4 × 크기 3 × 방향 3 × 불리언 6개) 전수 검사는 하네스가 따로 쓴 디코더로 필드를 다시 꺼내 비교하고, 나머지 비트(EN, DMEIE, PFCTRL, PINCOS, DBM, CT, BURST, 예약)가 0 인지도 본다.

NDTR 은 **항목 수** 다. 37 항목 halfword 복사는 74 바이트를 옮긴다. 주소는 PINC/MINC 일 때 항목 크기만큼 증가하고, 진행 중 주소는 레지스터로 보이지 않는다(SxPAR/SxM0AR 은 프로그램한 값 그대로 — 모델 정의, RM 확인 권장). 진행 상황을 알려 주는 것은 NDTR 뿐이다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">N = 8: NDTR 과 플래그 (모델: HT 는 NDTR == 4, TC 는 0)</text>
  <text x="10" y="44" fill="currentColor">normal</text>
  <path d="M60 40 L96 40 L96 50 L132 50 L132 60 L168 60 L168 70 L204 70 L204 80 L240 80 L240 90 L276 90 L276 100 L312 100 L312 120 L640 120" stroke="currentColor" fill="none"/>
  <path d="M60 120 L700 120" stroke="currentColor" fill="none" stroke-dasharray="2 3"/>
  <text x="40" y="44" fill="currentColor">8</text>
  <text x="40" y="124" fill="currentColor">0</text>
  <path d="M204 30 L204 125" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="170" y="30" fill="currentColor">HTIF</text>
  <path d="M312 30 L312 125" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="318" y="40" fill="currentColor">TCIF + HW 가 EN=0</text>
  <text x="318" y="56" fill="currentColor">이후 요청 무시, NDTR 0 유지</text>
  <text x="10" y="164" fill="currentColor">circular</text>
  <path d="M60 160 L96 160 L96 170 L132 170 L132 180 L168 180 L168 190 L204 190 L204 200 L240 200 L240 210 L276 210 L276 220 L312 220 L312 160 L348 160 L348 170 L384 170 L384 180 L420 180 L420 190 L456 190 L456 200 L492 200 L492 210 L528 210 L528 220 L564 220 L564 160 L640 160" stroke="currentColor" fill="none"/>
  <path d="M60 240 L700 240" stroke="currentColor" fill="none" stroke-dasharray="2 3"/>
  <text x="40" y="164" fill="currentColor">8</text>
  <text x="40" y="244" fill="currentColor">0</text>
  <rect x="204" y="226" width="8" height="8" class="f-ok" stroke="currentColor"/>
  <text x="194" y="258" fill="currentColor">HT</text>
  <rect x="308" y="226" width="8" height="8" class="f-acc" stroke="currentColor"/>
  <text x="300" y="258" fill="currentColor">TC</text>
  <rect x="456" y="226" width="8" height="8" class="f-ok" stroke="currentColor"/>
  <text x="446" y="258" fill="currentColor">HT</text>
  <rect x="560" y="226" width="8" height="8" class="f-acc" stroke="currentColor"/>
  <text x="552" y="258" fill="currentColor">TC</text>
  <text x="580" y="200" fill="currentColor">마지막 항목에서 0 →</text>
  <text x="580" y="216" fill="currentColor">즉시 8 재장전, EN=1</text>
  <text x="96" y="148" fill="currentColor">항목 1개 = 계단 1칸. HT 콜백 = 앞 반쪽 [0,4) 다 씀, TC 콜백 = 뒤 반쪽 [4,8) 다 씀</text>
</svg>
```

HT 규칙은 이 모델의 정의다: NDTR 이 `N/2`(내림)가 되는 순간. 짝수 N 이면 정확히 반, 홀수 N = 5 면 3 항목 뒤(NDTR 2), N = 1 이면 HT 와 TC 가 같은 항목에서 선다. 실제 칩의 홀수 N 동작은 RM 확인 권장 — 펌웨어는 어차피 짝수 N 만 쓴다(반쪽 처리 구조이니까). N = 1..64 테스트는 `ht_ndtr == N/2`, TC 한 번, 두 플래그가 서 있는 것을 확인한다. IE 를 하나도 안 켜도 플래그는 선다 — 폴링 드라이버는 이것만 보면 된다(테스트: IRQ 진입 0회).

`PINC = 0` 이면 소스 주소가 고정이다. word 0xA5C30F1E 하나를 64번 읽어 64 word 를 채운다 = DMA memset. 같은 원리로 `MINC = 0` + P→M 은 "마지막 값만 남는 레지스터 샘플링", `PINC = 0` + M→P 는 보통의 UART TX(PAR = DR 고정)다.

`demo_sweep` 의 2번 표 (실측, 모델: tick 당 항목 1개):

```text
2) M2M copy 1024 bytes (stream 0, model: 1 item per tick)
   byte     NDTR=1024  ticks=1023
   halfword NDTR= 512  ticks=511
   word     NDTR= 256  ticks=255
```

1024 가 아니라 1023 인 이유: 첫 항목은 `dma_stream_start` 의 EN 쓰기 tick 에 이미 옮겨지고, 측정은 그 뒤부터다. 핵심은 비율 — 항목을 키우면 버스 트랜잭션 수가 1/4 이 된다. 실제 F4 는 FIFO 와 burst(MBURST/PBURST)로 더 줄이지만 이 레슨은 direct mode 만 모델링한다(M→M 은 실제로 FIFO 모드가 필요 — RM 확인 권장).

### Part B — 설정 절차의 함정

**① EN = 1 이면 거부.** 테스트의 접근 로그는 `R 0x400264B8` 한 줄뿐이다(S7 CR = 0x10 + 0x18·7 = 0xB8). 레지스터를 쓰지 않으니 진행 중인 전송은 무사하다. 실제 칩도 EN = 1 동안 설정 비트·NDTR·주소 쓰기를 무시한다(RM0090: "can be written only when EN = 0").

**② abort — 실제 접근 로그** (`uart1_tx_dma_start(…, 100)` 후 30 바이트쯤 나간 시점):

| # | 접근 | 값 | 의미 |
|---|---|---|---|
| 1 | R SxCR | 0x08010455 | EN = 1, TCIE·TEIE 켜짐 |
| 2 | W SxCR | 0x08010440 | EN 과 IE 를 한 번에 0 (모델은 EN=1 동안 IE 쓰기 허용) |
| 3 | R SxCR | 0x08010441 | 아직 EN = 1 (진행 중 전송 마무리) |
| 4 | R SxCR | 0x08010441 | 〃 |
| 5 | R SxCR | 0x08010440 | EN = 0 → `DMA_OK` |

그 순간 NDTR = 68 (100 중 32 항목을 DR 로 넘김), HISR = 0x08000000 = 스트림 7 TCIF. abort 도 TCIF 를 세운다 — RM0090 에 "전송 중 EN 을 지우면 전송 끝을 알리려고 TCIF 가 선다" 는 취지의 설명이 있다고 알고 있다(확인 권장). 그래서 HAL 의 abort 는 IE 부터 끈다. IE 를 남겨 뒀다면 이 TCIF 는 **"전송 완료" 콜백** 으로 배달된다 — 이어 보내기 로직이 "다 보냈다" 고 믿는다. 이어 보내기는 NDTR 로 한다: 다음 주소 = M0AR + (100 − 68) × 1, 길이 68 (연습 2). 단 M→P 에서 "DR 로 넘긴 32" ≠ "선 위로 나간 바이트" — TDR·시프트 레지스터에 남은 것까지 따지려면 USART 쪽 상태도 봐야 한다.

**③ 기다리지 않은 재설정 — 절반만 적용** (실제 로그):

```text
R 0x400264B8 0x08010455     ← EN=1
W 0x400264B8 0x08010454     ← EN=0 "요청" (dis_wait 3 시작)
W 0x400264C4 0x20000600     ← M0AR: EN 이 아직 1 → 무시
W 0x400264BC 0x00000005     ← NDTR: 무시 (이 tick 에 EN 이 0 이 됨)
W 0x400264B8 0x08010454     ← CR: 이제 EN=0 이라 적용
결과: M0AR=0x20000300 (옛 버퍼), NDTR=78 (옛 잔량), CR=새 값
```

새 버퍼로 5 바이트를 보내려 했는데 옛 버퍼의 남은 78 바이트가 새 설정으로 나간다. 진짜 칩에서 이 버그는 **타이밍에 따라** 나타난다 — 진행 중 전송이 빨리 끝나면 멀쩡하다. 그래서 "기다려라" 는 규칙이 있다.

**④ 남은 TCIF.** abort 뒤 IFCR 클리어 없이 PAR/M0AR/NDTR/CR(TCIE 포함)를 쓰면, 레벨 라인은 **CR 쓰기 순간에** 이미 high 다(로그 덤프: start 전 `line = 1`). 첫 `sim_step` 에서 full 콜백, 그때 NDTR = 49 (50 중 거의 안 보냄). 같은 일을 `dma_stream_config` 로 하면 처음 20 step 동안 콜백 0, NDTR = 0 에서 정확히 한 번.

**⑤ TEIF.** 목적지 0x3000_0000 (이 모델에서 매핑 안 됨): 첫 항목의 쓰기가 실패 → TEIF, HW 가 EN = 0, NDTR = 16 그대로, error 콜백 flags = 0x08 (그룹 비트 3). 소스가 SRAM 끝 20 바이트 앞에서 word 8개: 5개 옮기고 6번째 읽기(0x2000_8000)가 실패 → NDTR = 3. **NDTR 은 사고 위치 기록** 이다: `실패 주소 = 시작 + (N − NDTR) × 크기`.

**⑥ 정렬.** 드라이버는 halfword 인데 홀수 주소면 거부한다. 거부하지 않으면? 하네스가 레지스터를 직접 써서 보여 준다: M0AR = 0x2000_0201, halfword 4개 → HW(모델)는 하위 비트를 버려 0x200 부터 쓴다 → 가드 바이트 0x200 이 0x11 로 덮인다. 실제 F4 도 PSIZE/MSIZE 에 맞춰 하위 주소 비트를 무시한다고 알고 있다(RM 확인 권장). 조용한 손상은 에러보다 나쁘다.

**⑦ ISR 의 플래그 클리어.** 레벨 라인이므로 ISR 이 플래그를 지워야 라인이 내려간다. 안 지우면(버그 3) 100 step 중 95번 이상 ISR — main 이 굶는다. 지우는 **방식** 이 더 미묘하다. `demo_trace` 실측 (N = 8, 1 바이트 / 3 tick, HT 콜백 11 tick):

```text
1a) clear-all-at-end, N=8, 1 byte / 3 ticks, HT callback 11 ticks, 16 bytes
  t= 22 ISR enter        LISR[21:16]=0x10
  t= 23 after R LISR     LISR[21:16]=0x10
  t= 34 callbacks done   LISR[21:16]=0x30
  t= 35 after W LIFCR    LISR[21:16]=0x00
  ...
    -> half=2 full=0
1b) correct handler (clear seen bits first), same timing
  t= 22 ISR enter        LISR[21:16]=0x10
  t= 36 ISR exit         LISR[21:16]=0x20
  t= 37 ISR enter        LISR[21:16]=0x20
  t= 40 ISR exit         LISR[21:16]=0x00
    -> half=2 full=2
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">같은 타이밍, 두 핸들러 (demo_trace 실측 tick, 스트림 2)</text>
  <path d="M60 40 L700 40" stroke="currentColor" fill="none"/>
  <text x="44" y="56" fill="currentColor">t=20</text>
  <text x="684" y="56" fill="currentColor">t=40</text>
  <path d="M124 32 L124 230" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="96" y="30" fill="currentColor">HT t=22</text>
  <path d="M508 32 L508 230" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="480" y="30" fill="currentColor">TC t=34</text>
  <text x="10" y="84" fill="currentColor">버그</text>
  <rect x="124" y="70" width="32" height="24" class="f-mut" stroke="currentColor"/>
  <text x="128" y="86" fill="currentColor">R</text>
  <rect x="156" y="70" width="352" height="24" class="f-ok" stroke="currentColor"/>
  <text x="250" y="86" fill="currentColor">HT 콜백 (11 tick)</text>
  <rect x="508" y="70" width="32" height="24" class="f-warn" stroke="currentColor"/>
  <text x="512" y="86" fill="currentColor">W</text>
  <text x="550" y="80" fill="currentColor">LIFCR ← 0x3D&lt;&lt;16</text>
  <text x="550" y="96" fill="currentColor">TC 까지 지움 → 유실</text>
  <text x="10" y="150" fill="currentColor">정답</text>
  <rect x="124" y="136" width="96" height="24" class="f-mut" stroke="currentColor"/>
  <text x="128" y="152" fill="currentColor">R CR, R, W</text>
  <rect x="220" y="136" width="352" height="24" class="f-ok" stroke="currentColor"/>
  <text x="300" y="152" fill="currentColor">HT 콜백 (11 tick)</text>
  <rect x="604" y="136" width="96" height="24" class="f-acc" stroke="currentColor"/>
  <text x="610" y="152" fill="currentColor">TC 진입</text>
  <text x="124" y="186" fill="currentColor">W 는 본 비트(0x10)만 지웠다 → t=34 의 TC(0x20)는 남아 라인 high → t=37 재진입, full 콜백</text>
  <text x="124" y="206" fill="currentColor">1 tick = 1 버스 접근. 버그 쪽 W(t=34→35)가 TC 를 세운 tick 직후에 실행됐다</text>
  <text x="124" y="224" fill="currentColor">콜백 10 tick 이면 W 가 TC 보다 먼저라 안 보인다 → "가끔" 나는 버그</text>
</svg>
```

콜백 비용을 바꿔 가며 512 바이트(콜백 128개 기대)를 돌린 `demo_sweep` 1번 표 (실측):

```text
   HT-cost | correct (seen-only) | clear-all-early | clear-all-at-end | ORE(correct)
         0 |                   0 |               0 |                0 | 0
        10 |                   0 |               0 |                0 | 0
        11 |                   0 |               0 |               64 | 0
        18 |                   0 |               0 |               64 | 0
        19 |                   0 |              21 |               64 | 0
        20 |                   0 |              32 |               64 | 0
        21 |                   5 |               0 |               64 | 0
        24 |                  18 |              14 |               64 | 0
        30 |                  37 |              34 |               64 | 0
```

(표는 12행 중 일부. 전체는 `./demo_sweep`.) 읽는 법:

- **clear-all-at-end** (`f = LISR; 콜백; LIFCR = 그룹 전체`): 콜백이 HT→TC 간격(12 tick)을 덮는 순간 TC 를 **매번** 잃는다. 10 → 0개, 11 → 64개. 개발자 책상에서 콜백이 짧으면 안 보이다가, 필터를 추가한 날 터진다.
- **clear-all-early** (`f = LISR; LIFCR = 그룹 전체; 콜백`): 창은 R 과 W 사이 1 tick 뿐인데도 위상이 맞으면 잃는다. 비용 19 의 추적: `ISR t= 69 read=0x20  flags at W=0x30  <-- set between R and W, cleared unseen` — TC 를 읽은 tick 에 다음 HT 가 섰다. 43/64 HT 만 배달.
- **correct** (`seen` 만, 콜백 전에): 비용 20 까지 0. 21 부터는 한 바퀴(24 tick)에 할 일 — HT 콜백 21 + 핸들러 레지스터 접근 3 + 디스패치 step 1 = 25 tick (밀리기 시작하면 HT·TC 를 한 진입에서 같이 처리하므로 오버헤드는 한 번) — 이 바퀴 시간을 넘어 **같은 플래그가 두 번 서는** 과부하 — 이건 핸들러가 아니라 예산 문제다(BM-M09 에서 검출·재동기화). ORE 가 0 인 이유: DMA 는 계속 제때 옮긴다. 잃는 것은 "알림" 이다.

**⑧ 같은 레지스터의 남의 플래그.** 스트림 0 과 2 는 둘 다 LISR 에 산다. 스트림 0 핸들러가 `LIFCR = LISR` 로 "편하게" 지우면 스트림 2 의 HT/TC 가 같이 사라진다. 테스트: 스트림 2 가 400 바이트 RX(콜백 100개 기대)를 받는 동안 main 이 16 step 마다 스트림 0 M→M 을 다시 걸고, 40 step 중 6 step 은 임계 구역(IRQ 보류)이다. 보류가 풀리는 순간 두 라인이 함께 high 면 번호 낮은 스트림 0(IRQ 56)이 먼저 돈다. 결과(실측): 정답 핸들러 0개 유실, `LIFCR = LISR` 5개 유실. LIFCR 은 쓰기 전용이라(0 으로 읽힘 — 이 모델의 정의, RM 확인 권장) `LIFCR |= x` 는 0 을 읽고 x 를 쓰니 우연히 맞다 — 하지만 "읽은 상태값을 그대로 클리어 레지스터에 쓰기" 는 남의 것까지 지운다. 상태와 클리어가 **다른 레지스터** 인 W1C 설계(BM-B04 의 EXTI_PR 는 같은 레지스터의 rc_w1)는 RMW 없이 원하는 비트만 지우라고 있는 것이다.

### Part C — 펌웨어처럼 쓰기

**TX: DMA TC ≠ USART TC.** `-v` 기동 로그 (실제 출력):

```text
[t=    11] S7 cfg: CR=0x08010455 NDTR=73 PAR=0x40011004 M0AR=0x20000300 (HIFCR<-0x0F400000 first)
[t=   734] S7 TC callback: NDTR=0, USART1 SR=0x00 (TC=0), on-wire 72/74 bytes
[t=   752] USART1 TC=1 after 17 more polls: 74 bytes on wire, 741 ticks total -> safe to cut power / drop RS-485 DE
```

- 설정 직후 NDTR 이 74 가 아니라 73 — `dma_stream_start` 의 EN 쓰기 tick 에 첫 바이트가 이미 DR 로 갔다(시프트 레지스터가 비어 TXE 가 바로 1 로 돌아옴).
- 접근 순서(단언됨): `R CR → W HIFCR 0x0F400000 → W PAR → W M0AR → W NDTR → W CR 0x08010454 → R CR → W CR |EN → W SR 0xFFFFFFBF (TC 만 0, rc_w0) → R CR3 → W CR3 |DMAT`. 스트림 시작 → TC 클리어 → DMAT 순서는 ST 의 UART DMA 송신 절차와 같은 꼴이다(HAL 소스 기준, 확인 권장).
- TC 콜백 순간 `SR = 0x00`: TXE = 0(TDR 에 74번째 바이트), TC = 0(시프트 중인 73번째). 이때 RS-485 DE 를 내리거나 전원을 끄면 마지막 두 바이트가 깨진다. 콜백은 "버퍼를 재사용해도 된다" 까지만 말한다 → 끝을 알려면 USART TC (폴링 또는 TCIE). BM-B12 의 "TXE vs TC" 와 같은 이야기가 한 단계 위에서 반복된다.

**RX circular:** `[t= 20693] S2 RX circular N=16: 4096 bytes, 256 HT + 256 TC callbacks, 256 laps, ORE=0, EN still 1`. HT 콜백은 `buf[0..8)`, TC 콜백은 `buf[8..16)` 를 복사하고, 이어 붙인 4096 바이트가 보낸 것과 같다. 바이트당 RXNE 인터럽트였다면 4096번, 여기서는 512번. `demo_sweep` 3번 표(실측): N = 8 → 1024, 32 → 256, 128 → 64, 512 → 16 진입. 버퍼가 클수록 IRQ 는 줄고 반쪽을 처리할 시간 여유(= N/2 문자 시간)는 늘지만, 지연(latency)과 RAM 은 커진다. IDLE 라인으로 "덜 찬 반쪽" 을 꺼내는 것은 COMM-M11, 처리 예산과 overrun 검출은 BM-M09.

**요청이 안 온다:** `[t=  2009] S7 CHSEL=5 (wrong): after 2000 ticks NDTR=40 EN=1 tx=0 -> request never arrives`. EN = 1, 에러 플래그 없음, NDTR 그대로 — DMA 는 정상이고 **요청 라인** 이 안 이어진 것이다. 기동 체크리스트:

| 증상 | 의심 순서 |
|---|---|
| NDTR 그대로, 플래그 없음 | CHSEL ↔ 요청 매핑 표, 주변장치의 DMA enable(CR3.DMAT/DMAR, ADC CR2.DMA), 주변장치 자체가 켜졌나(UE, TE), RCC 의 DMA2EN |
| TEIF | 주소(정렬, 존재하는 메모리인가, DMA 가 닿는 버스인가 — 예: F4 의 CCM RAM 은 DMA 가 못 본다고 알려짐, 확인 권장) |
| 첫 IRQ 가 바로 뜸 | 설정 전 IFCR 클리어 누락 |
| 가끔 반쪽 누락 | ISR 클리어 방식, 처리 예산 (BM-M09) |
| 데이터는 맞는데 CPU 가 옛 값을 읽음 | D-cache (Cortex-M7) — ARCH-M12 |

**타깃 빌드.** 같은 드라이버를 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -ffreestanding -DTARGET_BUILD -c solution.c` 로 빌드하면(실측) 드라이버 + 핸들러 8개가 text 2026 바이트, `dma_irq_handler` 는 MMIO 접근이 정확히 세 번이다 — `ldr r1, [r1, #0x10]` (SxCR), `ldr r5, [r5]` (LISR/HISR), `str r3, [r5, r0]` (IFCR). 컴파일러는 `& 0x3D & ie_flags` 를 `ands r1, r1, #0x3c` 로 접었다(FE 는 IE 가 FCR 에 있어 CR 로는 못 켜니까).

### 호스트 모델 ↔ 실제 API (HAL 기준 대응, 확인 권장)

| 이 레슨 | STM32 HAL / LL |
|---|---|
| `dma_stream_config` + `dma_stream_start` | `HAL_DMA_Init` (EN 해제 후 대기, 레지스터 설정) + `HAL_DMA_Start_IT` (플래그 클리어, 주소·NDTR, IE, EN) |
| `dma_stream_disable` | `HAL_DMA_Abort` (IE 끄기 → EN 끄기 → EN 대기 → 플래그 클리어) |
| `dma_irq_handler` | `HAL_DMA_IRQHandler` (IE 가 켜진 플래그만 보고, 플래그별로 클리어 후 콜백) |
| `dma_flag_bit` | HAL 의 `StreamBaseAddress`/`StreamIndex` 계산 표, LL 의 `LL_DMA_ClearFlag_TC7` 같은 스트림별 함수 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 92/92 passed
./solution -v                                                                  # 기동 로그 (5. 풀이)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_san && ./sol_san   # 92/92, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `CR &= ~EN;` 바로 다음 줄에 NDTR/M0AR 쓰기 | EN 은 진행 중 전송이 끝나야 0. 그 전 쓰기는 무시 → 반쪽 설정 (Part B ③) | `while (CR & EN)` (상한 포함) 후 설정 |
| 재설정 전에 IFCR 클리어 안 함 | 지난 TCIF(abort 포함)가 남아 TCIE 쓰는 순간 IRQ → 0 바이트에서 "완료" | `IFCR = 0x3D << off(s)` 를 설정 맨 앞에 |
| ISR 에서 `LIFCR = LISR` | 같은 레지스터의 다른 3 스트림 플래그까지 지움 | `IFCR = seen << off(s)` (이 스트림, 본 비트) |
| 콜백 다 부른 뒤 그룹 전체 클리어 | 콜백 도중 선 TC 를 같이 지움 (비용 11 tick 부터 64/64 유실) | 읽고 → 본 것만 지우고 → 콜백 |
| ISR 에서 아무것도 안 지움 | 레벨 라인 → 무한 재진입, main 굶음 | 반드시 IFCR |
| DMA TC 콜백에서 RS-485 DE 해제 / 전원 차단 | 마지막 2 바이트가 TDR·시프트에 있음 (`SR = 0x00`) | USART TC 대기 (TCIE 또는 폴링) |
| NDTR 에 바이트 수 | NDTR 은 PSIZE 단위 항목 수 — word 전송에 바이트 수를 넣으면 4배 넘쳐 옆 메모리를 덮는다 | `ndtr = bytes / item_size` (나머지는 따로, 연습 3) |
| 정렬 안 된 버퍼를 halfword/word 로 | HW 가 하위 비트를 버려 앞 바이트를 덮는다 (Part B ⑥) | 검증 후 거부, 또는 byte 전송 / `__ALIGNED(4)` |
| abort 하면서 IE 를 안 끔 | abort 의 TCIF 가 "완료" 콜백으로 감 | IE 와 EN 을 같이 끄고 대기 |
| CHSEL 표를 안 봄 | 요청이 안 와서 NDTR 이 영원히 그대로 (에러 플래그도 없음) | RM 의 DMA request mapping 표, 주변장치 DMA enable 비트 |

## 8. 면접 Q&A

<details>
<summary>Q1. 스트림을 재설정하는 절차를 말해 보세요. 각 단계를 빼면?</summary>

① EN = 1 이면 EN 을 지우고 **EN 이 0 으로 읽힐 때까지** 기다림 — 빼면 그사이 쓴 설정이 무시돼 반쪽 설정(옛 M0AR + 옛 NDTR 잔량 + 새 CR). ② 그 스트림의 플래그를 IFCR 로 전부 지움 — 빼면 지난 TCIF 때문에 IE 를 켜는 순간 인터럽트. ③ PAR, M0AR(, M1AR), NDTR, CR, (FCR). ④ EN. RM0090 DMA 장의 "Stream configuration procedure" 가 이 순서를 적고 있다(확인 권장). 꼬리: "기다리는 루프에 상한은?" — 진행 중 단일 전송 하나(또는 burst)가 끝나는 시간이면 충분하니 작은 상한 + 실패 시 에러 보고. 무한 대기는 버스 고장에서 시스템을 멈춘다.

</details>

<details>
<summary>Q2. LISR 의 플래그 오프셋이 왜 0, 6, 16, 22 인가요? 코드로는 어떻게 다루나요?</summary>

스트림 하나 = 6비트(FE, 예약, DME, TE, HT, TC). 32비트 레지스터의 하위 16비트에 두 스트림(0–5, 6–11), 상위 16비트에 두 스트림(16–21, 22–27)을 넣고 12–15, 28–31 은 예약 — 그래서 불규칙하다. 표 `{0,6,16,22}[s&3]` 가 가장 읽기 쉽고, 분기 없는 식 `((s&2)<<3) + (s&1)*6` 도 된다(연습 1). 스트림 4..7 은 HISR/HIFCR. 실수 1순위는 `6*s` 같은 등간격 가정 — 스트림 2 에서 바로 틀린다(12 vs 16).

</details>

<details>
<summary>Q3. ISR 에서 플래그를 어떻게 지우나요? 콜백 뒤에 지우면 안 되나요?</summary>

"본 것만, 이 스트림 것만, 콜백 전에". 레벨 인터럽트라 지워야 라인이 내려가는데, 콜백 뒤에 그룹 전체를 지우면 콜백 도중 새로 선 TC 를 같이 지운다 — 이 레슨의 측정에서 HT 콜백이 11 tick(반 버퍼 12 tick)이면 TC 64/64 유실, 10 tick 이면 0. 즉 "필터 하나 추가한 날" 터지는 버그. 읽은 비트만 먼저 지우면 새 플래그는 남아 라인이 high 로 유지되고 바로 재진입한다. 꼬리: "읽자마자 전체를 지우면?" — 창이 1 tick 으로 줄 뿐 0 은 아니다(비용 19 에서 21/64 HT 유실 실측).

</details>

<details>
<summary>Q4. DMA TC 인터럽트가 왔습니다. UART 송신이 끝났나요?</summary>

아니다. DMA TC = "마지막 항목을 DR 에 썼다". 그 바이트는 TDR 에, 그 앞 바이트는 시프트 레지스터에 있을 수 있다 — 이 레슨에서 TC 콜백 순간 74 중 72 바이트만 선 위에 있었고 `SR = 0x00`. 버퍼 재사용은 OK, RS-485 방향 전환·전원 차단·저전력 진입·보레이트 변경은 USART TC 를 기다린 뒤(17 폴링 뒤 TC = 1). HAL 의 `HAL_UART_TxCpltCallback` 이 DMA TC 가 아니라 USART TC 에서 불리도록 되어 있는 이유다(확인 권장).

</details>

<details>
<summary>Q5. abort 후 NDTR 은 무엇을 알려 주나요? TCIF 는 왜 서나요?</summary>

NDTR = 아직 안 옮긴 항목 수. 보낸 양 = N − NDTR(× 크기), 이어 보내기 주소 = M0AR + 그만큼 (100 중 68 남음 → +32). TEIF 로 멈췄을 때도 같은 식으로 실패 주소를 찾는다(Part B ⑤: 0x2000_8000). abort 의 TCIF 는 "이 스트림이 멈췄다" 는 통지 — 드라이버가 완료와 구별하려면 IE 를 먼저 끄거나, 상태 변수(aborting)를 보고 콜백을 고른다. 이 TCIF 동작은 RM0090 기준 기억이니 확인 권장.

</details>

<details>
<summary>Q6. circular 모드에서 한 번의 ISR 진입에 HT 와 TC 가 둘 다 서 있으면 어느 쪽이 먼저인가요?</summary>

알 수 없다. 같은 바퀴의 HT→TC 일 수도, TC(k 바퀴) → HT(k+1 바퀴) 일 수도 있다. 이 레슨의 핸들러는 half 다음 full 을 부르는데, 후자라면 앞 반쪽은 이미 다음 바퀴 데이터로 덮이는 중이다. 구별하려면 NDTR 을 읽어 DMA 가 지금 어느 반쪽을 쓰는지 보거나(N − NDTR < N/2 면 앞 반쪽 쓰는 중), 둘 다 섰다는 사실 자체를 "늦었다" 신호로 세서 재동기화한다 — BM-M09(검출) 와 COMM-M11(NDTR 기반 위치) 의 주제. 근본 해결은 처리 예산을 반 버퍼 시간 안에 넣는 것.

</details>

<details>
<summary>Q7. 스트림 우선순위(PL)와 NVIC 우선순위는 무엇이 다른가요? UART RX 와 TX 중 어느 쪽을 높게?</summary>

PL 은 **버스 중재** — 두 스트림이 동시에 요청하면 누가 먼저 항목을 옮기나(같으면 번호 낮은 스트림, RM0090). NVIC 우선순위는 **어느 핸들러가 먼저/선점하나**. 서로 독립이다. RX 를 높게: RX 는 RDR 에 한 바이트 버퍼뿐이라 한 문자 시간 안에 못 가져가면 ORE 로 데이터가 사라지고, TX 는 늦으면 선 위에 틈이 생길 뿐 데이터는 안 잃는다. 이 레슨의 설정: RX PL 2, TX PL 1 (연습 4 가 중재 규칙을 전수 검증).

</details>

<details>
<summary>Q8. Cortex-M7 (STM32F7/H7) 으로 옮기면 무엇이 더 필요한가요?</summary>

D-cache. TX 전에는 버퍼를 **clean**(CPU 가 쓴 것이 RAM 에 있게), RX 후에는 **invalidate**(CPU 가 캐시의 옛 값을 안 보게), 버퍼는 캐시 라인(32 B) 정렬·크기 배수 — 아니면 non-cacheable 영역(MPU)에. ARCH-M12 에서 다뤘다. 또 H7 은 DMAMUX 로 요청 매핑이 바뀌고, DMA 가 닿는 RAM 영역(DTCM 등)에 제약이 있다(칩 RM 확인 권장). 이 레슨의 F4(Cortex-M4)는 D-cache 가 없다.

</details>

<details>
<summary>Q9. 메모리→메모리 복사에 DMA 를 쓰는 게 항상 이득인가요?</summary>

아니다. 설정 비용(레지스터 6~7번 쓰기 + 완료 대기/IRQ)이 있어 수십 바이트면 `memcpy` 가 빠르다. 이득은 큰 블록에서 CPU 가 다른 일을 할 수 있을 때. NDTR 은 16비트라 한 번에 65535 항목 — 300000 바이트 word 복사는 65535 + 9465 두 조각(연습 3). 소스·목적지 정렬이 다르면 끝까지 byte 전송이라 4배 느리다(모델: 1024 바이트에 1023 vs 255 tick). F4 에서 M→M 은 DMA2 만 가능하고 circular 불가(RM0090, 확인 권장).

</details>

## 9. 추가 연습

### 연습 1 — 플래그 위치를 분기 없이, 그리고 "TC 선 스트림 목록"

`flag_bit_alu(s, f) = ((s & 2) << 3) + (s & 1) * 6 + f` 가 8 × 6 전부에서 표와 같은지 보이고, LISR/HISR 값에서 TC 가 선 스트림의 비트마스크를 돌려주는 `tc_streams(lisr, hisr)` 를 작성하라.

<details>
<summary>답 보기</summary>

s&1 이 6, s&2 가 16 을 만든다: 0→0, 1→6, 2→16, 3→22. 스트림 마스크 `0x3D << off`: S2 = 0x003D0000, S7 = 0x0F400000, 네 개 OR = 0x0F7D0F7D. `tc_streams` 는 s 마다 `(reg >> (off+5)) & 1`. LISR bit 21 + HISR bit 27 → {2, 7}. TC 비트만 빼고 다 선 값 `0x0F7D0F7D & ~0x08200820` → 0. 32비트 각각 하나씩 세운 전수 검사로 TC 비트 네 개(5, 11, 21, 27)만 반응하는지 확인.

</details>

### 연습 2 — abort 뒤 이어 보내기

`abort_resume(m0ar, n, left, size, &new_m0ar, &new_n)`: NDTR 잔량으로 다음 주소·길이를 구하라. 남은 게 없거나 NDTR > n(불가능 = 설정 버그)이면 false.

<details>
<summary>답 보기</summary>

`new_m0ar = m0ar + (n − left) × size`, `new_n = left`. 본문 숫자: 0x2000_0300, 100, 68 → 0x2000_0320, 68. word 256 개 중 6 남음 → +1000 바이트. 주의: M→P 에서는 DMA 가 DR 에 넘긴 것까지 "보냄" 으로 센다 — 선 위로 나갔는지는 USART 쪽(TC, 남은 TDR) 이 결정한다. 정확한 재전송이 필요하면 abort 전에 USART TC 를 기다리거나 프로토콜 계층에서 재전송한다.

</details>

### 연습 3 — 임의 길이·정렬의 M→M 복사 계획

`plan_copy(src, dst, len, chunks[], cap)`: 소스·목적지가 4 정렬이면 word, 2 정렬이면 halfword, 아니면 byte. 한 조각 ≤ 65535 항목. 나머지는 더 작은 크기로.

<details>
<summary>답 보기</summary>

탐욕적으로: 매 단계 `(src|dst)` 정렬과 남은 길이로 가장 큰 크기를 고르고 `min(len/size, 65535)` 항목. 300000 바이트 word → 65535 + 9465 (2 조각). src 홀수·dst 짝수 → 정렬이 영원히 안 맞으니 byte 100개 1조각. 4 정렬 7 바이트 → word 1 + half 1 + byte 1. 검증은 src%4 × dst%4 × 길이 1..300 전수 + 1 MB 이하 무작위 500건: 조각이 이어 붙고, 각 조각이 정렬되고, 합이 len 인지. (실제 F4 는 FIFO 로 PSIZE ≠ MSIZE 패킹도 되지만 이 레슨 범위 밖.)

</details>

### 연습 4 — 스트림 중재

요청 비트마스크와 PL[8] 로 승자를 고르는 `arbitrate()` 를 "키 = PL·8 + (7 − s) 최대" 로 작성하고, 정의 그대로의 이중 루프와 2000 × 256 전수 비교하라.

<details>
<summary>답 보기</summary>

키가 PL 을 먼저, 그다음 낮은 번호를 우선하도록 만든 것. PL{S2 = 2, S7 = 1, 나머지 0} 에서 {0, 2, 7} 요청 → 2, {0, 7} → 7, {1, 3} → 1 (같은 PL 이면 번호 낮은 쪽), 요청 없음 → −1. 모델의 `dma_tick` 이 쓰는 규칙과 같다.

</details>

### 연습 5 — 바이트당 IRQ vs HT/TC IRQ, 반 버퍼 마감

8N1(10 비트/문자)에서 초당 RXNE IRQ 수, N 바이트 circular 의 초당 HT+TC 수(올림), 반 버퍼 처리 마감(µs, 내림)을 구하라.

<details>
<summary>답 보기</summary>

115200 baud → 11520 문자/s = 바이트당 IRQ 11520/s. N = 64 → 2·11520/64 = 360 IRQ/s (32배 적음), 마감 = 32 × 10 × 10⁶ / 115200 = 2777 µs. 921600 baud, N = 256 → 720 IRQ/s, 1388 µs. 3 Mbaud 에 N = 16 이면 37500 IRQ/s — 버퍼가 작으면 DMA 를 써도 다시 IRQ 폭풍. 마감 안에 처리하지 못하면 BM-M09 의 overrun.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 25/25 passed
```

## 10. 핵심 정리

- STM32F4 DMA 스트림 = CR · NDTR · PAR · M0AR(· M1AR · FCR). NDTR 은 **항목 수**, 진행 상황을 알려 주는 유일한 레지스터. M→M 은 PAR = 소스.
- 플래그 그룹 6비트(TC5 HT4 TE3 DME2 FE0), 시작 비트 0 / 6 / 16 / 22, 스트림 4..7 은 HISR. 지우기는 **다른 레지스터** IFCR 에 1.
- 설정 절차: EN 끄고 **0 으로 읽힐 때까지** → 그 스트림 플래그 전부 클리어 → 주소 · NDTR · CR → EN. EN = 1 이면 설정 쓰기는 무시된다.
- normal: NDTR 0 에서 TCIF + HW 가 EN 끔. circular: TCIF + NDTR·주소 재장전, EN 유지. HT 는 N/2.
- ISR: `seen = 그룹 & IE` → `IFCR = seen << off` → 콜백. 콜백 뒤 전체 클리어 / `LIFCR = LISR` / 안 지움은 각각 유실 · 남의 유실 · 폭풍.
- DMA TC ≠ 주변장치 완료. UART 는 USART TC 를 기다려야 선이 빈다.
- 증상 → 원인: NDTR 그대로 = 요청 없음(CHSEL, DMAT), TEIF = 주소, 첫 IRQ 즉시 = 남은 플래그, 가끔 반쪽 누락 = 클리어 방식 또는 예산.

## 참고자료

- ST RM0090 *STM32F405/415, F407/417, F427/437, F429/439 Reference manual* — DMA controller 장 (스트림 레지스터, LISR/HISR 비트 배치, Stream configuration procedure, Circular mode, Error management), DMA1/DMA2 request mapping 표, 벡터 표 — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf> (이 레슨의 주소·비트는 기억에 의존한 대표값 — 원문 확인 권장)
- ST AN4031 *Using the STM32F2, STM32F4 and STM32F7 Series DMA controller* — 설정 순서, FIFO/burst, 중재, 소프트웨어 시퀀스 권장 사항 — <https://www.st.com/resource/en/application_note/an4031-using-the-stm32f2-stm32f4-and-stm32f7-series-dma-controller-stmicroelectronics.pdf>
- STM32CubeF4 HAL `stm32f4xx_hal_dma.c` (`HAL_DMA_Start_IT`, `HAL_DMA_Abort`, `HAL_DMA_IRQHandler`, `DMA_CalcBaseAndBitshift`) 와 `stm32f4xx_hal_uart.c` (`HAL_UART_Transmit_DMA`, TC 처리) — <https://github.com/STMicroelectronics/stm32f4xx_hal_driver>
- 관련 레슨: BM-B02 (mock 프레임워크 — 여기선 블록 단위 축약판), BM-B04 (W1C / rc_w0, RMW 경쟁), BM-B12 (TXE vs TC, 폴링 UART), BM-M04 (인터럽트 구동 UART — 바이트당 IRQ 버전), BM-M09 (ADC + DMA 더블 버퍼링, 처리 예산과 overrun 검출), COMM-M11 (UART RX circular DMA + IDLE, NDTR 위치), ARCH-M12 (DMA 캐시 일관성 clean/invalidate), C-M14 (ISR↔main 공유 데이터), DS-B04 (2의 거듭제곱 링버퍼)
