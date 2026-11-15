---
id: RTOS-A06
time: 110분
updated: 2026-11-11
---
# RTOS-A06 RTOS 트레이싱: 훅으로 이벤트 기록 → 타임라인

## 학습 목표

- 커널 trace 훅(`traceTASK_SWITCHED_IN/OUT`, `traceTASK_DELAY_UNTIL`, `traceBLOCKING_ON_QUEUE_RECEIVE` …)에 붙는 **레코더**를 만든다: 고정 크기 레코드, 단일 writer 링 버퍼, 덮어쓰기(flight recorder) / 가득 차면 멈춤(streaming) 두 모드, 순번(sequence number), 잃은 이벤트 수와 DROP 마커.
- 텍스트 트레이스만 보고 태스크별 **실행 구간과 CPU 점유율** 을 다시 만드는 디코더를 짠다: 32비트 µs 타임스탬프 wrap, 첫 이벤트 없는 트레이스, 중간 손실을 처리하고 **모르는 시간은 아무에게도 주지 않는다**.
- 주기 태스크의 응답 시간(RELEASE → 블록)을 트레이스에서 재고, 손실이 있을 때 어떤 숫자를 믿어도 되는지 말할 수 있다.
- 진짜 FreeRTOS(POSIX 포트)에 trace 매크로를 꽂아 같은 포맷을 덤프하고, `timeline.py` 로 타임라인 SVG/HTML 을 그리고, 커널 자체 실행 시간 통계(`ulRunTimeCounter`)와 µs 단위로 대조한다.
- SEGGER SystemView / Percepio Tracealyzer 가 원리 수준에서 무엇을 하는지(훅 → 버퍼 → 전송 → 호스트 재구성) 설명할 수 있다.

## 1. 문제

"현장에서 가끔 모터 루프가 데드라인을 놓칩니다. 어떻게 찾겠어요?" — 시니어 펌웨어 면접의 단골 질문이고, 좋은 답은 "**트레이스를 떠서 그 순간 누가 CPU 를 쓰고 있었는지 본다**" 다. 꼬리질문은 곧바로 그 도구 자체로 간다: "그 트레이스는 ISR 에서도 기록되는데 어떻게 동기화하나요?", "버퍼가 넘치면?", "타임스탬프가 wrap 하면?", "트레이스 때문에 타이밍이 바뀌지 않나요?". 이 레슨은 SystemView/Tracealyzer 가 하는 일을 직접 만들어 그 질문들에 코드로 답한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">트레이스 파이프라인: 타깃(MCU)에서 기록, 호스트(PC)에서 재구성</text>
  <rect x="10" y="34" width="160" height="128" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor" font-weight="bold">커널 trace 훅</text>
  <text x="20" y="74" fill="currentColor" font-family="monospace">SWITCHED_IN/OUT</text>
  <text x="20" y="92" fill="currentColor" font-family="monospace">TASK_DELAY(_UNTIL)</text>
  <text x="20" y="110" fill="currentColor" font-family="monospace">BLOCKING_ON_QUEUE_*</text>
  <text x="20" y="128" fill="currentColor" font-family="monospace">QUEUE_SEND/RECEIVE</text>
  <text x="20" y="148" fill="currentColor">+ ISR 진입/종료, 앱 마커</text>
  <rect x="200" y="34" width="170" height="128" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="210" y="54" fill="currentColor" font-weight="bold">tr_record() (Part A)</text>
  <text x="210" y="76" fill="currentColor">① 인터럽트 끄기</text>
  <text x="210" y="96" fill="currentColor">② 시각 읽기 (CYCCNT/µs)</text>
  <text x="210" y="116" fill="currentColor">③ seq++, 칸 하나 쓰기</text>
  <text x="210" y="136" fill="currentColor">④ 인터럽트 복원</text>
  <text x="210" y="154" fill="currentColor" font-size="12">M4 에서 약 44 명령어</text>
  <rect x="400" y="34" width="150" height="128" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="410" y="54" fill="currentColor" font-weight="bold">RAM 링 버퍼</text>
  <text x="410" y="76" fill="currentColor">16 B × cap (2^k)</text>
  <text x="410" y="96" fill="currentColor">OVERWRITE: 마지막 N개</text>
  <text x="410" y="116" fill="currentColor">STOP: 넘치면 버리고</text>
  <text x="410" y="134" fill="currentColor">DROP 마커로 개수 기록</text>
  <rect x="580" y="34" width="150" height="128" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="590" y="54" fill="currentColor" font-weight="bold">리더 (단일)</text>
  <text x="590" y="76" fill="currentColor">RTT / UART / SWO</text>
  <text x="590" y="96" fill="currentColor">또는 정지 후 덤프</text>
  <text x="590" y="116" fill="currentColor">tx_record: seq 구멍</text>
  <text x="590" y="134" fill="currentColor">→ DROP 줄</text>
  <line x1="170" y1="98" x2="196" y2="98" stroke="currentColor" stroke-width="2"/>
  <polygon points="200,98 192,93 192,103" fill="currentColor"/>
  <line x1="370" y1="98" x2="396" y2="98" stroke="currentColor" stroke-width="2"/>
  <polygon points="400,98 392,93 392,103" fill="currentColor"/>
  <line x1="550" y1="98" x2="576" y2="98" stroke="currentColor" stroke-width="2"/>
  <polygon points="580,98 572,93 572,103" fill="currentColor"/>
  <line x1="10" y1="178" x2="730" y2="178" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="12" y="194" fill="currentColor">위: 타깃 (ISR·임계 구역 안에서도 불림)  ·  아래: 호스트</text>
  <line x1="655" y1="162" x2="655" y2="200" stroke="currentColor" stroke-width="2"/>
  <polygon points="655,206 650,198 660,198" fill="currentColor"/>
  <rect x="470" y="206" width="260" height="74" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="480" y="226" fill="currentColor" font-family="monospace"># rtos-trace v1 unit=us</text>
  <text x="480" y="246" fill="currentColor" font-family="monospace">4294709317 BLOCK Comm spi</text>
  <text x="480" y="266" fill="currentColor" font-family="monospace">4294709320 SWITCH_IN Log</text>
  <text x="470" y="296" fill="currentColor">공유 텍스트 포맷 (A09·A10 도 씀)</text>
  <rect x="200" y="202" width="240" height="38" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="210" y="226" fill="currentColor">디코더 (Part B): 구간·CPU·응답</text>
  <rect x="200" y="250" width="240" height="38" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="210" y="274" fill="currentColor">timeline.py: SVG / HTML</text>
  <line x1="470" y1="221" x2="446" y2="221" stroke="currentColor" stroke-width="2"/>
  <polygon points="440,221 448,216 448,226" fill="currentColor"/>
  <line x1="470" y1="269" x2="446" y2="269" stroke="currentColor" stroke-width="2"/>
  <polygon points="440,269 448,264 448,274" fill="currentColor"/>
  <text x="12" y="230" fill="currentColor">호스트는 상태 기계를</text>
  <text x="12" y="248" fill="currentColor">다시 돌려 재구성한다</text>
</svg>
```

**공유 트레이스 포맷 `rtos-trace v1`** (이 레슨이 정의한다. RTOS-A09 데드라인 미스 분석과 RTOS-A10 설계 레슨이 같은 포맷을 쓴다):

| 항목 | 규칙 |
|---|---|
| 헤더 | 첫 줄 `# rtos-trace v1 unit=us`. `#` 로 시작하는 줄과 빈 줄은 주석 |
| 한 줄 | `<t_us> <EVENT> <name> [arg]` — `t_us` 는 부호 없는 32비트 µs (2^32 µs = 71.6 분마다 wrap), 줄 순서 = 시간 순서 (wrap 을 법으로 단조 증가) |
| EVENT | `RELEASE` `SWITCH_IN` `SWITCH_OUT` `BLOCK` `UNBLOCK` `ISR_ENTER` `ISR_EXIT` `MUTEX_TAKE` `MUTEX_GIVE` `Q_SEND_BLOCK` `Q_RECV_BLOCK` `DEADLINE_MISS` `DROP` |
| name | `[A-Za-z0-9_]+`, 15자 이하. idle 태스크는 `IDLE`, 손실 표시는 `DROP TRACE n` |
| arg | 객체 이름(`BLOCK Comm spi`, `Q_RECV_BLOCK Comm rxq`), 블록 이유(`delay`, `delay_until`), 또는 숫자(`DROP` 의 개수) |

### Part A — 타깃 쪽 레코더와 내보내기

- `bool tr_record(trace_ring_t *r, uint32_t t, ev, id, argk, arg)` — 16바이트 레코드 하나를 링에 쓴다. **순번은 부를 때마다 하나씩** (버린 것도 번호를 먹는다).
  - `TR_OVERWRITE`: 가득 차면 가장 오래된 것을 밀어낸다 (`dropped++`). "고장 직전 N개" 를 남기는 flight recorder.
  - `TR_STOP_WHEN_FULL`: 가득 차면 **새 것** 을 버리고 `pend_drop` 에 센다. 다음에 자리가 나면 먼저 DROP 마커(seq = 첫 손실 순번, t = 첫 손실 시각, arg = 개수)를 쓰고 이벤트를 쓴다 — 마커 + 이벤트 2칸이 없으면 이번 것도 버린다.
- `void tx_record(tr_text_t *x, const trace_rec_t *r)` / `tx_end` — 리더가 꺼낸 레코드를 텍스트로. 마커는 `DROP TRACE n` 으로, 마커 없는 **순번 구멍**(덮어쓰기로 잃은 앞부분, 전송 중 손실)도 `DROP` 줄로 바꾼다. 꼬리 손실은 `tx_end` 가 마무리한다.

### Part B — 호스트 쪽 디코더

- `dec_time` — 32비트 시각을 첫 이벤트 기준 64비트로 푼다. 직전과의 차이가 2^31 이상이면 "시간이 거꾸로" 오류.
- `dec_advance` — 흐른 시간을 주인에게: ISR 중이면 ISR 몫(깔린 태스크는 `stolen`), 아니면 태스크, 모르면 `unknown`.
- `dec_apply` — `SWITCH_IN` 이 주인을 바꾼다(같은 태스크 재선택은 전환 아님), `ISR_ENTER/EXIT` 는 스택, `DROP` 은 다음 `SWITCH_IN` 까지 "모름".
- 주어진 것: 줄 파서 `trace_decode`, 이름표, 타임라인 구간 만들기, CPU basis point 계산.

### Part C — 모터 제어기: 손실이 있는 스트리밍과 응답 시간

STM32 모터 제어기 모양의 결정적 시뮬레이터(주어진 코드): `Ctrl`(우선 3, 1 ms), `Comm`(2, 2.5 ms), `Log`(1, 10 ms), `IDLE`, ISR `ADC`(500 µs 마다), `CAN`(20 ms·33 ms 에 프레임 8개 버스트). 타임스탬프는 `0xFFFFC000` 에서 시작해 16.4 ms 에 wrap. 레코드는 UART 300 kB/s(10 µs 에 3바이트, 레코드 16바이트)로 빠져나간다.

- `dec_job` — `RELEASE X` 에서 job 을 열고 `BLOCK X delay|delay_until` 에서 닫아 응답 시간을 잰다. **DROP 을 건너는 job 은 버린다.**
- 하네스가 시뮬레이터의 정답(10 µs 마다 누가 CPU 를 썼나)과 대조: 큰 링이면 µs 단위로 완전 일치, 링 8 이면 손실이 나도 **틀린 귀속 0**, 링 32 면 같은 회선에서 손실 0, flight recorder(덮어쓰기 128)는 첫 줄이 `DROP` 이고 창 안은 정확.
- 진짜 FreeRTOS: `demo_freertos_trace.c` + 이 폴더 `FreeRTOSConfig.h` 의 trace 매크로가 **같은 레코더 코드**(solution.c 를 `#include`)로 기록 → 텍스트 → `timeline.py`. 커널의 `ulRunTimeCounter` 와 대조.

## 2. Problem (English)

> **Build a minimal RTOS trace toolchain.**
>
> 1. `bool tr_record(trace_ring_t *r, uint32_t t, uint8_t ev, uint8_t id, uint8_t argk, uint32_t arg)` — single-writer ring of 16-byte records, capacity a power of two, free-running `wr`/`rd` counters. Every call consumes one sequence number. `TR_OVERWRITE`: when full, evict the oldest (count it as dropped). `TR_STOP_WHEN_FULL`: when full, reject the new event and remember (first seq, first time, count); when space for two records is available again, first emit a `DROP` marker carrying that information, then the event.
> 2. `void tx_record(tr_text_t *x, const trace_rec_t *r)` / `void tx_end(tr_text_t *x, const trace_ring_t *r)` — convert records to the text format `<t_us> <EVENT> <name> [arg]`; turn markers and any sequence gap into `DROP TRACE n` lines (gap time = last known timestamp, or the record's own time if nothing was emitted yet); emit a final `DROP` for a lost tail.
> 3. `bool dec_time(dec_t *d, uint32_t t32, uint64_t *now)`, `void dec_advance(dec_t *d, uint64_t new_now)`, `bool dec_apply(dec_t *d, int ev, int ctx, uint32_t n)` — rebuild per-context run time from `SWITCH_IN`, account ISR time separately (and as "stolen" from the preempted task), unwrap 32-bit timestamps (a forward step ≥ 2^31 is an error), never attribute time before the first `SWITCH_IN` or after a `DROP` until the next `SWITCH_IN`.
> 4. `void dec_job(dec_t *d, int ev, int ctx, const char *arg)` — response time per periodic job: `RELEASE X` … `BLOCK X delay|delay_until`; jobs spanning a `DROP` are discarded.
>
> Example: `100 SWITCH_IN A / 400 SWITCH_IN B / 500 ISR_ENTER X / 550 ISR_EXIT X / 700 SWITCH_IN A / 1000 SWITCH_OUT A` → A 600 µs (66.67 %), B 250 µs + 50 µs stolen, X 50 µs, unknown 0, 5 intervals.
>
> Follow-ups: how is the recorder synchronized with ISRs? What does tracing cost? When would you stream vs. snapshot? Can you trust CPU% from a lossy trace?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "20/51 passed", exit 1 (스텁이라 링에 아무것도 안 들어가고, 디코더는 시간을 아무에게도 안 준다)
./starter --dump > /tmp/sim.txt          # 다 채우면: Part C 트레이스 (손실 없음) — timeline.py 입력
./starter --dump-lossy > /tmp/lossy.txt  # 링 8 + 300 kB/s, DROP 포함
```

TODO 는 7개다: A1 `tr_record`, A2 `tx_record`/`tx_end`, B1 `dec_time`, B2 `dec_advance`, B3 `dec_apply`, C1 `dec_job`. 링 초기화(`tr_init`, cap 이 2의 거듭제곱인지 검사)·`tr_pop`·텍스트 줄 쓰기·줄 파서·구간 만들기·시뮬레이터·정답 대조 함수는 주어진 코드다.

관찰용 파일 (하네스와 별개):

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_trace.c \
   $K/{tasks,list,queue}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c -o /tmp/RTOS-A06_dt
/tmp/RTOS-A06_dt                              # 8192 stop → /tmp/RTOS-A06_trace.txt (경고 0개로 빌드됨)
/tmp/RTOS-A06_dt 256 overwrite /tmp/ow.txt    # flight recorder
python3 -I timeline.py /tmp/RTOS-A06_trace.txt --svg /tmp/t.svg --html /tmp/t.html --from-us 40500 --to-us 46500
python3 -I timeline.py trace_freertos_sample.txt      # 폴더에 넣어 둔 실측 트레이스 (아래 표의 그 실행)
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4_record.c -o -   # 기록 1회의 M4 명령어
```

## 4. 힌트

<details>
<summary>힌트 1 — A1: 순번은 맨 먼저, 모드 분기는 그다음</summary>

`seq = r->next_seq++` 를 함수 첫 줄에 두면 "버린 것도 번호를 먹는다" 가 저절로 된다. 들어 있는 개수는 `r->wr - r->rd` (u32 뺄셈이라 카운터가 wrap 해도 맞다 — 그래서 cap 이 2의 거듭제곱이어야 `wr & mask` 가 같은 칸을 가리킨다). OVERWRITE 는 가득 찼으면 `rd++` 만 하면 가장 오래된 칸이 다음 쓰기 대상이 된다.

</details>

<details>
<summary>힌트 2 — A1: 마커는 "다음에 자리가 날 때" 쓴다</summary>

넘치는 순간에는 마커를 쓸 자리도 없다. 그래서 `pend_drop`, `pend_seq`, `pend_t` 에 기억만 하고, 다음 호출에서 빈칸이 **2개 이상** 일 때 `[마커][이벤트]` 를 연달아 쓴다. 1칸뿐인데 이벤트만 쓰면 순번 구멍은 생기지만 "언제부터 잃었나(pend_t)" 를 잃는다.

</details>

<details>
<summary>힌트 3 — B1: wrap 은 뺄셈 한 번으로 풀린다</summary>

`delta = t32 - last32` (u32) 가 그대로 경과 시간이다 — 4294967200 → 296 이면 delta 392. 이 식은 "연속 이벤트 간격 < 2^32 µs" 를 가정한다. 그 절반(2^31) 이상을 오류로 잡으면 순서가 뒤집힌 줄(정말로 시간이 거꾸로 간 기록)을 잡을 수 있다. wrap 횟수는 `t32 < last32` 로 센다.

</details>

<details>
<summary>힌트 4 — B2/B3: 시간은 이벤트를 적용하기 "전" 에 나눈다</summary>

줄마다 순서가 중요하다: ① 새 시각까지의 시간을 **지금 주인** 에게 준다(`dec_advance`) ② 그다음 이벤트가 주인을 바꾼다(`dec_apply`). DROP 이면 ①에서 이전 주인이 DROP 시각까지 받고(마커 시각 = 첫 손실 시각이라 그때까지는 확실), ②에서 주인이 "모름(-1)" 이 된다. `SWITCH_OUT` 은 주인을 바꾸지 않는다 — 다음 `SWITCH_IN` 까지는 커널 전환 비용이니 나가는 태스크 몫.

</details>

<details>
<summary>힌트 5 — C1: 응답 시간은 "열린 job" 하나만 추적</summary>

`RELEASE` 에서 `job_open = true, rel_t = now`, `BLOCK delay` 에서 `now - rel_t`. DROP 을 만나면 열려 있던 job 을 모두 닫고 `n_resp_lost++` — 구멍 안에서 BLOCK 이 사라졌을 수 있으니 그 뒤의 BLOCK 을 이 job 의 끝으로 쓰면 응답 시간이 엉터리로 커진다. 뮤텍스·큐 대기 BLOCK 은 job 의 끝이 아니다 (arg 로 구분).

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 도구 대응표

| 이 레슨 | FreeRTOS V11.3.2 (소스 확인) | SEGGER SystemView (문서 기준, 미검증) | Percepio Tracealyzer (문서 기준, 미검증) | Zephyr (문서 기준, 미검증) |
|---|---|---|---|---|
| 훅 지점 | `include/FreeRTOS.h` 617–1003 의 `#ifndef traceXXX` 기본값을 `FreeRTOSConfig.h` 에서 재정의 | FreeRTOS 용 설정이 trace 매크로를 `SEGGER_SYSVIEW_*` 호출로 연결 | TraceRecorder 라이브러리가 같은 trace 매크로를 정의 | `CONFIG_TRACING` + `sys_trace_*` 훅 |
| `tr_record` 잠금 | 사용자 몫 (Cortex-M 포트의 ISR 훅: `ARM_CM4F/port.c` 567–580 `traceISR_ENTER/EXIT`) | 기록 중 인터럽트 잠금 | 기록 중 임계 구역 | 백엔드마다 다름 |
| 타임스탬프 | 사용자 몫 (POSIX 포트 실행 시간 기본값 = `times()`, `port.c` 672–679) | Cortex-M 은 보통 DWT 사이클 카운터 | 하드웨어 타이머/사이클 카운터 | 커널 사이클 카운터 |
| 링 + 리더 | — | RTT 버퍼 + J-Link 로 연속 스트리밍 | snapshot(RAM 버퍼 덤프) / streaming 모드 | CTF 등 포맷, UART/USB/RAM 백엔드 |
| DROP 마커 | — | 버퍼가 넘치면 overflow 이벤트 | 손실 표시 | 백엔드마다 다름 |
| 디코더 / `timeline.py` | `vTaskGetRunTimeStats` (`ulRunTimeCounter`) 가 CPU 표만 준다 | SystemView PC 앱 | Tracealyzer PC 앱 (많은 분석 뷰) | Trace Compass 등 |

### Part A — 레코더: 순번이 손실을 말해 준다

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">cap 4 링에 이벤트 seq 0..5 (+ 6): 두 모드가 남기는 것 (테스트 A-1, A-2 그대로)</text>
  <text x="10" y="54" fill="currentColor" font-weight="bold">OVERWRITE</text>
  <rect x="140" y="36" width="66" height="26" class="f-ok" stroke="currentColor"/><text x="173" y="54" fill="currentColor" text-anchor="middle">seq 4</text>
  <rect x="206" y="36" width="66" height="26" class="f-ok" stroke="currentColor"/><text x="239" y="54" fill="currentColor" text-anchor="middle">seq 5</text>
  <rect x="272" y="36" width="66" height="26" class="f-acc" stroke="currentColor"/><text x="305" y="54" fill="currentColor" text-anchor="middle">seq 2</text>
  <rect x="338" y="36" width="66" height="26" class="f-ok" stroke="currentColor"/><text x="371" y="54" fill="currentColor" text-anchor="middle">seq 3</text>
  <text x="272" y="80" fill="currentColor">↑ rd (가장 오래된 것)</text>
  <text x="420" y="46" fill="currentColor">seq 0, 1 은 밀려남 (dropped 2)</text>
  <text x="420" y="64" fill="currentColor" font-family="monospace">→ 첫 줄 "t2 DROP TRACE 2"</text>
  <text x="10" y="124" fill="currentColor" font-weight="bold">STOP_WHEN_FULL</text>
  <rect x="140" y="106" width="66" height="26" class="f-ok" stroke="currentColor"/><text x="173" y="124" fill="currentColor" text-anchor="middle">seq 2</text>
  <rect x="206" y="106" width="66" height="26" class="f-ok" stroke="currentColor"/><text x="239" y="124" fill="currentColor" text-anchor="middle">seq 3</text>
  <rect x="272" y="106" width="66" height="26" class="f-warn" stroke="currentColor"/><text x="305" y="124" fill="currentColor" text-anchor="middle">DROP 4×2</text>
  <rect x="338" y="106" width="66" height="26" class="f-ok" stroke="currentColor"/><text x="371" y="124" fill="currentColor" text-anchor="middle">seq 6</text>
  <text x="420" y="116" fill="currentColor">4, 5 는 거절 (pend 2, pend_t = 40)</text>
  <text x="420" y="134" fill="currentColor">리더가 0, 1 을 빼 간 뒤 6 이 오면</text>
  <text x="420" y="150" fill="currentColor">마커 + 6 을 연달아 쓴다</text>
  <text x="10" y="186" fill="currentColor" font-weight="bold">전송 손실</text>
  <text x="140" y="186" fill="currentColor" font-family="monospace">seq 5 (t 260) 다음에 seq 8 (t 300) 도착</text>
  <text x="140" y="206" fill="currentColor" font-family="monospace">→ "260 DROP TRACE 2" (직전 확실한 시각부터 모름)</text>
  <text x="10" y="236" fill="currentColor">규칙: 마커의 시각 = 첫 손실 시각 (writer 가 앎) · 추정한 구멍의 시각 = 마지막으로 받은 줄 (보수적)</text>
</svg>
```

```c
uint32_t seq = r->next_seq++;
uint32_t used = r->wr - r->rd;
if (r->mode == TR_OVERWRITE) {
    if (used == r->cap) { r->rd++; r->dropped++; }          /* 가장 오래된 것을 밀어낸다 */
    tr_put(r, seq, t, ev, id, argk, arg);
    return true;
}
uint32_t need = r->pend_drop ? 2u : 1u;                     /* 밀린 마커가 있으면 마커 + 이벤트 */
if (r->cap - used < need) { ...pend_*, dropped++; return false; }
if (r->pend_drop) { tr_put(r, r->pend_seq, r->pend_t, EV_DROP, 0, ARG_NUM, r->pend_drop); r->pend_drop = 0; }
tr_put(r, seq, t, ev, id, argk, arg);
```

| 설계 결정 | 왜 |
|---|---|
| 순번 = "시도" 횟수 | 리더가 받은 순번만 보고 **어디서 몇 개** 잃었는지 안다. 저장 횟수로 매기면 구멍이 안 보인다 |
| 덮어쓰기는 `rd++` (writer 가 rd 를 움직임) | 덮어쓰기 모드에선 writer 가 rd 를 건드리므로 **동시에 읽는 리더가 있으면 안 된다** — 멈춘 뒤 덤프(post-mortem) 용. 살아 있는 스트리밍은 STOP 모드: writer 는 `wr`, 리더는 `rd` 만 쓴다 (단일 생산자·단일 소비자) |
| STOP 은 새 것을 버림 | 리더가 아직 안 읽은 칸을 writer 가 덮으면 리더가 반쯤 쓰인 레코드를 읽는다 |
| 마커에 첫 손실 **시각** | 디코더가 "그 시각까지는 이전 주인이 확실히 돌았다" 를 안다 (아래 Part B). 개수만 있으면 구멍의 시작을 모른다 |
| cap = 2의 거듭제곱 | `wr`, `rd` 를 자유 증가 u32 로 두고 `& mask` 로 칸을 고른다. `% 12` 면 2^32 에서 칸 번호가 튄다 (테스트 A-4 가 `wr = 0xFFFFFFFE` 에서 시작해 확인) |
| 단일 writer | MCU 에서 writer 는 "태스크 + 모든 ISR" 이다. **인터럽트를 끄고**(PRIMASK/BASEPRI) 시각 읽기 + 칸 쓰기를 한 덩어리로 해야 단일 writer 가 된다. 시각을 잠금 밖에서 읽으면 ISR 이 끼어들어 더 늦은 시각을 먼저 기록 → 줄 순서 ≠ 시간 순서 |

`demo_m4_record.c` 는 이 `tr_record` 를 PRIMASK 잠금 + `DWT->CYCCNT` 읽기로 감싼 타깃식 훅이다. Cortex-M4 `-O2` asm 에서 흔한 경로(STOP 모드, 자리 있음, 밀린 마커 없음)를 손으로 세면 `push` 부터 `pop` 까지 **44 명령어**, `cpsid i` 에서 `msr primask` 까지 인터럽트가 꺼진 구간이 **39 명령어** 다 (실제 출력 일부):

```text
trace_switch_in:
	push.w	{r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r1, #4100
	movw	r2, :lower16:g_ring
	mrs	r12, primask
	cpsid i
	movt	r1, #57344            @ r1 = 0xE0001004 = DWT_CYCCNT
	movt	r2, :upper16:g_ring
	ldr.w	lr, [r1]              @ 타임스탬프 = 잠금 안에서 읽는다
	...
	strh	r1, [r6, #14]
	str	r0, [r2, #12]         @ wr++
	msr	primask, r12
	pop.w	{r4, r5, r6, r7, r8, r9, r10, pc}
```

사이클은 재지 않았다 (QEMU 는 사이클 정확하지 않고, 보드에서 재는 법은 RTOS-A03). 명령어 수만 봐도 "이벤트 하나에 수십 명령어 + 16 B" 라는 감이 온다 — 10 kHz 전류 루프의 ISR 진입/종료를 다 찍으면 초당 2만 이벤트 × 16 B = 320 kB/s 다.

### Part A — 내보내기: 구멍을 줄로 바꾸기

`tx_record` 는 받은 레코드의 순번을 `exp_seq` 와 비교한다. 마커면 그대로 `DROP TRACE n` 을 쓰고 `exp_seq = seq + n`. 마커 없이 순번이 뛰면 `DROP TRACE (seq - exp_seq)` 를 **직전 줄의 시각** 에 쓴다 — 언제 잃었는지 모르므로 "마지막으로 확실한 시각" 부터 모름으로 둔다. 테스트 A-5 의 실제 기대 출력:

```text
# rtos-trace v1 unit=us
100 SWITCH_IN Ctrl
150 MUTEX_TAKE Ctrl spi
200 DROP TRACE 3          ← writer 가 쓴 마커 (순번 2,3,4)
260 SWITCH_IN Comm
260 DROP TRACE 2          ← 순번 6,7 이 전송 중 사라짐: 직전 줄 시각
300 SWITCH_IN Ctrl
300 DROP TRACE 1          ← tx_end: writer 는 10개를 냈는데 9번까지만 받음
```

### Part B — 디코더 규칙

| 줄 | 시간 귀속 (`dec_advance`, 이벤트 적용 전) | 상태 변화 (`dec_apply`) |
|---|---|---|
| `SWITCH_IN X` | 지금 주인에게 | 주인 = X. 이미 X 면 전환 아님 (`vTaskSwitchContext` 는 같은 태스크를 다시 골라도 OUT/IN 을 둘 다 찍는다 — `tasks.c` 5228, 5274 가 조건 없이 불림) |
| `SWITCH_OUT X` | 지금 주인에게 | 바꾸지 않음. 주인을 아는데 X 가 아니면 `mismatch` |
| `ISR_ENTER I` / `ISR_EXIT I` | 지금 주인에게 | 스택 push / pop. ISR 동안의 시간 = I 의 run, 깔린 태스크의 `stolen` |
| `DROP TRACE n` | 지금 주인에게 (마커 시각까지는 확실) | 주인 = 모름, ISR 스택 비움 → 다음 `SWITCH_IN` 까지 `unknown` |
| 첫 줄 이전 / 첫 `SWITCH_IN` 이전 | — | 주인 = 모름 (중간부터 시작한 트레이스) |

불변식: **Σ run + unknown = span** (테스트마다 `sum_ok` 로 검사). CPU % = `run × 10000 / span` 의 basis point (정수, 반올림). 첫 예제 `A 600 / 900` → 6667 = 66.67 %.

wrap: `delta = t32 - last32` 하나로 4294967200 → 296 이 392 µs 가 된다. 2^31 이상을 오류로 잡는 규칙은 데모를 만들다 실제로 효과를 봤다: 데모 첫 버전은 단조 증가 보정용 `g_last_t` 를 0 으로 시작했고, 첫 시각 4294667000 이 `(int32_t)(t - 0) < 0` 이라 "거꾸로" 로 판정되어 **300 ms 동안 모든 시각이 0 에 고정** 됐다 (디코드 결과 span 300708 µs, wraps 0). 시각 처리의 버그는 조용히 숫자만 틀리게 만든다 — 그래서 디코더가 이상한 점프를 오류로 내는 게 중요하다.

ISR 을 따로 세는 이유: 트레이스로 보면 `Ctrl` 이 9950 µs 돌았지만 그 사이 ISR 이 1480 µs 를 가져갔다 (아래 C-1 표). 커널의 `ulRunTimeCounter` 는 전환 시점에만 시계를 읽으므로 둘을 합친 11430 µs 를 `Ctrl` 몫으로 센다 (진짜 커널에서 확인, 아래).

### Part C — 시뮬레이터 정답과 대조 (solution 실행 결과)

```text
      events=528  (C-1 표: 디코더 결과)
      ctx          run_us   stolen     cpu%     in  resp#  max_resp
      Ctrl           9950     1480    24.88     40     40       490
      ADC            3200        0     8.00     80      0         0  (ISR)
      Comm           9500      590    23.75     27     16      1350
      Log            9500      720    23.75     24      4      7230
      IDLE           7530      730    18.83     17      0         0
      CAN             320        0     0.80     16      0         0  (ISR)
      span=40000 unknown=0 wraps=1 switches=108 drop_lines=0 dropped=0 mismatch=0 intervals=286
      응답 시간 정답 max: Ctrl=490 Comm=1350 Log=7230
      C-2 링 8: 잃음 22/528 이벤트, DROP 줄 4, 모름 660 µs (1.6 %), 응답 잰 것 Ctrl 38/40
      대조군 (DROP 무시): 틀린 귀속 1812 걸음 = 18120 µs, 모름 0 µs
      C-4 덮어쓰기 128: 앞 400개 잃음, 창 9240 µs = 귀속 9240 + 모름 0 µs
51/51 passed
```

| 경우 | 링 / 회선 | 결과 | 읽는 법 |
|---|---|---|---|
| C-1 | 4096, 정지 후 덤프 | 손실 0, 모든 태스크·ISR 의 run/stolen 이 정답과 µs 단위로 같다, 286 구간 전부 정답 | 디코더 규칙이 맞다 |
| C-2 | 8, 300 kB/s | 22 이벤트 손실 (CAN 버스트 2번), **틀린 귀속 0**, 모름 660 µs, Ctrl 응답 38/40 개만 잼 (재 것은 전부 정답 목록 안) | "모르는 건 모른다" 가 지켜짐 |
| 대조군 | C-2 텍스트에서 `DROP` 줄만 지움 | 틀린 귀속 **18120 µs** (40 ms 중 45 %) | 사라진 `SWITCH_IN` 하나 때문에 이전 주인이 다음 전환까지 계속 돈 것처럼 보인다 |
| C-3 | 32, 같은 회선 | 손실 0 | 회선 평균은 충분했다 — 문제는 **버스트를 흡수할 깊이** |
| C-4 | 덮어쓰기 128 | 첫 줄 `DROP TRACE 400`, 창 9240 µs 는 정확 | 고장 직전 창만 보는 flight recorder |

`timeline.py` 로 C-2 손실 트레이스(`./solution --dump-lossy`)의 CAN 버스트 근처를 그린 실제 출력이다. 점선 칸이 "모름" — 이 구간의 CAN ISR 일부와 Ctrl 실행은 이벤트가 사라져 아무 줄에도 그려지지 않는다:

```svg
<svg viewBox="0 0 760 218" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="8" y="18" fill="currentColor" font-size="13" font-weight="bold">C-2 손실 트레이스 (링 8, 300 kB/s) 19.5-22.5 ms</text>
  <text x="8" y="45" fill="currentColor">ADC</text>
  <line x1="78" y1="49" x2="744" y2="49" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="8" y="67" fill="currentColor">CAN</text>
  <line x1="78" y1="71" x2="744" y2="71" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="8" y="89" fill="currentColor">Ctrl</text>
  <line x1="78" y1="93" x2="744" y2="93" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="8" y="111" fill="currentColor">Comm</text>
  <line x1="78" y1="115" x2="744" y2="115" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="8" y="133" fill="currentColor">Log</text>
  <line x1="78" y1="137" x2="744" y2="137" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="8" y="155" fill="currentColor">IDLE</text>
  <line x1="78" y1="159" x2="744" y2="159" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="8" y="177" fill="currentColor">모름</text>
  <rect x="78.0" y="144" width="11.1" height="14" class="f-mut"/>
  <rect x="89.1" y="34" width="15.5" height="14" class="f-warn"/>
  <rect x="104.6" y="144" width="84.4" height="14" class="f-mut"/>
  <rect x="189.0" y="56" width="4.4" height="14" class="f-warn"/>
  <rect x="193.4" y="78" width="2.2" height="14" class="f-acc"/>
  <rect x="195.7" y="56" width="4.4" height="14" class="f-warn"/>
  <rect x="200.1" y="34" width="6.7" height="14" class="f-warn"/>
  <rect x="206.8" y="166" width="15.5" height="14" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/>
  <rect x="222.3" y="56" width="4.4" height="14" class="f-warn"/>
  <rect x="226.7" y="166" width="71.0" height="14" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/>
  <rect x="297.8" y="100" width="13.3" height="14" class="f-acc"/>
  <rect x="311.1" y="34" width="6.7" height="14" class="f-warn"/>
  <rect x="317.8" y="100" width="93.2" height="14" class="f-acc"/>
  <rect x="411.0" y="78" width="11.1" height="14" class="f-acc"/>
  <rect x="422.1" y="34" width="6.7" height="14" class="f-warn"/>
  <rect x="428.8" y="78" width="33.3" height="14" class="f-acc"/>
  <rect x="462.1" y="100" width="48.8" height="14" class="f-acc"/>
  <rect x="510.9" y="122" width="22.2" height="14" class="f-acc"/>
  <rect x="533.1" y="34" width="15.5" height="14" class="f-warn"/>
  <rect x="548.6" y="122" width="84.4" height="14" class="f-acc"/>
  <rect x="633.0" y="78" width="11.1" height="14" class="f-acc"/>
  <rect x="644.1" y="34" width="6.7" height="14" class="f-warn"/>
  <rect x="650.8" y="78" width="44.4" height="14" class="f-acc"/>
  <rect x="695.2" y="122" width="48.8" height="14" class="f-acc"/>
  <polygon points="189.0,75 185.5,81 192.5,81" fill="currentColor"/>
  <polygon points="255.6,119 252.1,125 259.1,125" fill="currentColor"/>
  <polygon points="411.0,75 407.5,81 414.5,81" fill="currentColor"/>
  <polygon points="633.0,75 629.5,81 636.5,81" fill="currentColor"/>
  <line x1="78" y1="188" x2="744" y2="188" stroke="currentColor"/>
  <line x1="78.0" y1="188" x2="78.0" y2="192" stroke="currentColor"/>
  <text x="78.0" y="205" fill="currentColor" text-anchor="middle">19.5 ms</text>
  <line x1="189.0" y1="188" x2="189.0" y2="192" stroke="currentColor"/>
  <text x="189.0" y="205" fill="currentColor" text-anchor="middle">20 ms</text>
  <line x1="300.0" y1="188" x2="300.0" y2="192" stroke="currentColor"/>
  <text x="300.0" y="205" fill="currentColor" text-anchor="middle">20.5 ms</text>
  <line x1="411.0" y1="188" x2="411.0" y2="192" stroke="currentColor"/>
  <text x="411.0" y="205" fill="currentColor" text-anchor="middle">21 ms</text>
  <line x1="522.0" y1="188" x2="522.0" y2="192" stroke="currentColor"/>
  <text x="522.0" y="205" fill="currentColor" text-anchor="middle">21.5 ms</text>
  <line x1="633.0" y1="188" x2="633.0" y2="192" stroke="currentColor"/>
  <text x="633.0" y="205" fill="currentColor" text-anchor="middle">22 ms</text>
  <line x1="744.0" y1="188" x2="744.0" y2="192" stroke="currentColor"/>
  <text x="744.0" y="205" fill="currentColor" text-anchor="end">22.5 ms</text>
</svg>
```

응답 시간: C-1 에서 Ctrl 최대 490 µs 는 20 ms 의 job 이다 — 300 µs 계산 + CAN 프레임 8개 × 20 µs + ADC 30 µs. 트레이스로 잰 최대는 "관측된 최대" 일 뿐 최악은 아니다 — 관측 ≤ RTA 상한이어야 하고 (RTOS-M11, 확장 RTA 는 RTOS-A04), 측정 WCET 의 한계는 RTOS-A03.

### Part C — 진짜 FreeRTOS 에 꽂기 (POSIX 포트, macOS)

`FreeRTOSConfig.h` 의 핵심 (전체 파일은 아래 마커):

```c
#define traceTASK_SWITCHED_OUT()                  a06_switch_out( pxCurrentTCB->pcTaskName )
#define traceTASK_SWITCHED_IN()                   a06_switch_in( pxCurrentTCB->pcTaskName )
#define traceTASK_INCREMENT_TICK( xOld )          a06_tick( ( unsigned long long ) ( xOld ) + 1ULL )
#define traceTASK_DELAY()                         a06_block( pxCurrentTCB->pcTaskName, "delay" )
#define traceTASK_DELAY_UNTIL( xTimeToWake )      a06_block( pxCurrentTCB->pcTaskName, "delay_until" )
#define traceMOVED_TASK_TO_READY_STATE( pxTCB )   a06_ready( ( pxTCB )->pcTaskName )
#define traceBLOCKING_ON_QUEUE_RECEIVE( pxQueue ) a06_queue( 0, ( void * ) ( pxQueue ), ( pxQueue )->uxQueueType == queueQUEUE_IS_MUTEX )
#define configGENERATE_RUN_TIME_STATS             1
#define portALT_GET_RUN_TIME_COUNTER_VALUE( x )   ( ( x ) = a06_runtime() )
```

| 훅 | 어디서 펼쳐지나 (V11.3.2) | 이 데모에서 |
|---|---|---|
| `traceTASK_SWITCHED_OUT/IN` | `tasks.c` 5228 / 5274 (`vTaskSwitchContext`), 첫 태스크는 3809 | `SWITCH_OUT/IN`. OUT 에서 읽은 시각을 커널 실행시간 카운터(5232)와 IN 이 **같이** 쓴다 |
| `traceTASK_INCREMENT_TICK` | `tasks.c` 4842, 증가 **전** 값. 스케줄러 정지 중이면 증가 없이 `xPendedTicks++` | sub-tick 기준점 갱신 (이벤트 아님) |
| `traceTASK_DELAY` / `_DELAY_UNTIL` | `tasks.c` 2519 (`vTaskSuspendAll` 안, 인터럽트는 켜져 있음) / 2472 | `BLOCK <task> delay(_until)` — 응답 시간의 job 끝 |
| `traceMOVED_TASK_TO_READY_STATE` | `prvAddTaskToReadyList` (`tasks.c` 289) | `UNBLOCK` — 우선순위 상속/해제 때 다시 넣는 것도 여기로 온다 (6791, 6895) |
| `traceBLOCKING_ON_QUEUE_RECEIVE/SEND` | `queue.c` 1615, 1787 / 1125 | 뮤텍스면 `BLOCK <task> spi`, 큐면 `Q_RECV_BLOCK <task> rxq` (`uxQueueType` = `pcHead`, 뮤텍스는 NULL — `queue.c` 65–66) |
| `traceQUEUE_RECEIVE` / `traceQUEUE_SEND` | `queue.c` 1698 / 979 | 뮤텍스일 때만 `MUTEX_TAKE` / `MUTEX_GIVE` |
| ISR | POSIX 포트에는 ISR 훅이 없다 (Cortex-M 포트는 `ARM_CM4F/port.c` 567–580) | 틱 훅(틱 ISR 안)에서 7 tick 마다 `ISR_ENTER UART_RX` → `xQueueSendFromISR` → `ISR_EXIT` (RTOS-B08 방식) |

**타임스탬프는 실시간이 아니다.** 시각 = `tick × 1000 µs + sub-tick` (마지막 tick 이후 `CLOCK_MONOTONIC`, 0..999 로 자름). POSIX 포트의 tick 은 타이머 스레드의 `pthread_kill(SIGALRM)` + `usleep(1000)` (`port.c` 467–468) 이라 600 tick 이 벽시계로 878 ms 걸렸고, sub-tick 이 999 에 잘린 경우가 271번이다. `Log` 는 spi 를 쥐고 `busy_us(2500)` (벽시계 2.5 ms) 를 도는데, 트레이스의 `MUTEX_TAKE` → `MUTEX_GIVE` 는 1840 µs (그 사이 Comm 317 µs 포함) 다. 순서·귀속·커널 통계와의 일치를 보는 용도이고, 지속 시간 자체를 믿으면 안 된다. tick 은 `configINITIAL_TICK_COUNT = 4294667` 에서 시작해 300.3 ms 뒤 32비트 µs 가 wrap 한다 — 진짜 데이터로 wrap 처리를 시험한 것.

실제 실행 (`/tmp/RTOS-A06_dt`, load average ≈ 19):

```text
ring cap=8192 mode=stop  events=1923 lines=1923 dropped=0  text=48556 B overflow=0  -> /tmp/RTOS-A06_trace.txt
ticks: 600 (kernel) in 878464 us wall; tick-hook calls=602; sub-tick clamps=271 back-clamps=0; UART_RX isr=85 sent=85
decode rc=0 (0=OK)
      ctx          run_us   stolen     cpu%     in  resp#  max_resp
      Report          851        0     0.14      2      0         0
      Ctrl         121348        3    20.23    120    119      1182
      Comm          39262        0     6.54     93      0         0
      Log           65814       11    10.97     48     29      2841
      IDLE         372471       88    62.08    174      0         0
      UART_RX         102        0     0.02     85      0         0  (ISR)
      span=599948 unknown=100 wraps=1 switches=437 drop_lines=0 dropped=0 mismatch=0 intervals=558

task      trace_run    +stolen       =sum   kernel_ctr   diff
Report          851          0        851          862    -11
IDLE         372471         88     372559       372559      0
Log           65814         11      65825        65825      0
Ctrl         121348          3     121351       121351      0
Comm          39262          0     39262        39262      0
kernel total runtime=600041 us, trace span=599948 us (unknown before first SWITCH_IN=100)
```

- 트레이스 디코드(run + stolen) 와 커널 `ulRunTimeCounter` 가 **µs 까지 같다** (Report 제외). 커널은 `vTaskSwitchContext` 에서 "지금 − 마지막 switch-in" 을 나가는 태스크에 더한다 (`tasks.c` 5245–5254) — 같은 시계, 같은 전환 시점이면 같은 숫자가 나와야 하고, 실제로 그랬다. 시각 버그를 고친 뒤 5번 실행 모두 Report 외 diff 0.
- Report −11: 이 태스크는 지금 실행 중이다. V11.3.2 `ulTaskGetRunTimeCounter` 는 실행 중인 태스크에 "마지막 switch-in 이후" 를 더해 준다 (`tasks.c` 8533–8546). 트레이스는 기록을 끈 그 switch-in 에서 끝났으므로 그 11 µs 가 없다.
- `tick-hook calls=602` 는 `traceTASK_INCREMENT_TICK` 호출 수다: 600 + 스케줄러 정지 중의 중복 2번 (정지 중에는 4842 의 매크로가 증가 없이 불리고, 재개할 때 밀린 tick 을 처리하며 다시 불린다 — 그래서 `a06_tick` 은 더 큰 tick 만 받는다).
- `python3 -I timeline.py` 가 같은 파일에서 같은 표를 냈다 (C 디코더와 숫자 하나까지 같음 — 두 구현이 서로를 검증).
- 작은 링: `256 stop` → 처음 256개 + 꼬리 `4294744001 DROP TRACE 1681`, span 76895 µs. `256 overwrite` → 첫 줄 `218026 DROP TRACE 1657`, 마지막 82681 µs 만 남고 그중 첫 `SWITCH_IN` 전 207 µs 는 모름.

실제 트레이스(`trace_freertos_sample.txt`)의 40.5–46.5 ms 를 `timeline.py` 로 그린 그대로다. 41.95 ms 에 UART_RX ISR 이 Comm 을 깨우고, Comm 이 42.26 ms 에 `BLOCK Comm spi` (Log 가 spi 를 쥐고 있음, 세로 막대) → Log 가 우선순위를 물려받아(트레이스에 `UNBLOCK Log` = 6791 의 재삽입) 43.79 ms `MUTEX_GIVE` 까지 돌고, Comm 이 이어받아 43.95 ms 에 큐 대기로 돌아간다. 삼각형 = `RELEASE`.

```svg
<svg viewBox="0 0 760 174" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="8" y="18" fill="currentColor" font-size="13" font-weight="bold">FreeRTOS POSIX 포트 실측 트레이스 40.5-46.5 ms (spi 뮤텍스 상속 구간)</text>
  <text x="8" y="45" fill="currentColor">UART_RX</text>
  <line x1="78" y1="49" x2="744" y2="49" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="8" y="67" fill="currentColor">Ctrl</text>
  <line x1="78" y1="71" x2="744" y2="71" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="8" y="89" fill="currentColor">Comm</text>
  <line x1="78" y1="93" x2="744" y2="93" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="8" y="111" fill="currentColor">Log</text>
  <line x1="78" y1="115" x2="744" y2="115" stroke="currentColor" stroke-opacity="0.25"/>
  <text x="8" y="133" fill="currentColor">IDLE</text>
  <line x1="78" y1="137" x2="744" y2="137" stroke="currentColor" stroke-opacity="0.25"/>
  <rect x="78.0" y="122" width="49.7" height="14" class="f-mut"/>
  <rect x="127.7" y="56" width="110.8" height="14" class="f-acc"/>
  <rect x="238.5" y="100" width="1.0" height="14" class="f-acc"/>
  <rect x="238.6" y="34" width="1.0" height="14" class="f-warn"/>
  <rect x="238.8" y="100" width="1.0" height="14" class="f-acc"/>
  <rect x="238.9" y="78" width="35.2" height="14" class="f-acc"/>
  <rect x="274.1" y="100" width="168.8" height="14" class="f-acc"/>
  <rect x="443.0" y="78" width="17.5" height="14" class="f-acc"/>
  <rect x="460.5" y="100" width="59.1" height="14" class="f-acc"/>
  <rect x="519.6" y="122" width="163.2" height="14" class="f-mut"/>
  <rect x="682.7" y="56" width="61.3" height="14" class="f-acc"/>
  <polygon points="128.6,53 125.1,59 132.1,59" fill="currentColor"/>
  <polygon points="238.5,97 235.0,103 242.0,103" fill="currentColor"/>
  <line x1="273.8" y1="76" x2="273.8" y2="94" stroke="currentColor" stroke-width="2"/>
  <line x1="460.5" y1="76" x2="460.5" y2="94" stroke="currentColor" stroke-width="2"/>
  <polygon points="684.1,53 680.6,59 687.6,59" fill="currentColor"/>
  <line x1="78" y1="144" x2="744" y2="144" stroke="currentColor"/>
  <line x1="133.5" y1="144" x2="133.5" y2="148" stroke="currentColor"/>
  <text x="133.5" y="161" fill="currentColor" text-anchor="middle">41 ms</text>
  <line x1="244.5" y1="144" x2="244.5" y2="148" stroke="currentColor"/>
  <text x="244.5" y="161" fill="currentColor" text-anchor="middle">42 ms</text>
  <line x1="355.5" y1="144" x2="355.5" y2="148" stroke="currentColor"/>
  <text x="355.5" y="161" fill="currentColor" text-anchor="middle">43 ms</text>
  <line x1="466.5" y1="144" x2="466.5" y2="148" stroke="currentColor"/>
  <text x="466.5" y="161" fill="currentColor" text-anchor="middle">44 ms</text>
  <line x1="577.5" y1="144" x2="577.5" y2="148" stroke="currentColor"/>
  <text x="577.5" y="161" fill="currentColor" text-anchor="middle">45 ms</text>
  <line x1="688.5" y1="144" x2="688.5" y2="148" stroke="currentColor"/>
  <text x="688.5" y="161" fill="currentColor" text-anchor="middle">46 ms</text>
</svg>
```

<!-- code: demo_freertos_trace.c -->

<!-- code: FreeRTOSConfig.h -->

<!-- code: timeline.py -->

<!-- code: demo_m4_record.c -->

### SystemView / Tracealyzer 와 비교 (원리 수준)

| 질문 | 이 레슨 | 상용 도구 (문서 기준, 미검증) |
|---|---|---|
| 레코드 크기 | 고정 16 B | 이벤트 ID + 델타 타임스탬프 + 가변 길이 인코딩으로 수 바이트 (추가 연습 2: 같은 아이디어로 3.45 B) |
| 전송 | 정지 후 덤프 / UART 모델 | SystemView: J-Link RTT 로 CPU 를 멈추지 않고 RAM 을 읽어 연속 스트리밍. Tracealyzer: snapshot / streaming 모드 |
| 손실 | DROP 마커 + 순번 | 넘침 이벤트와 손실 수 표시 |
| 시각 | µs (POSIX 포트에선 tick 기반) | 사이클 카운터 해상도 |
| 분석 | CPU 표, 구간, 응답 시간 | CPU 부하 그래프, 응답/실행 시간 통계, 객체 이력, 사용자 이벤트 |

원리는 같다: 커널 훅 → 잠금 아래 짧은 기록 → 버퍼 → 호스트가 상태 기계를 다시 돌려 재구성. 차이는 대부분 **대역폭(인코딩)과 전송 경로** 다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 51/51 passed, exit 0 (5번 같음)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o s_san && ./s_san   # 깨끗
```

## 7. 자주 하는 실수

**1. 시각을 잠금 밖에서 읽는다.** `t = now(); lock(); write(t); unlock();` — `now()` 와 `lock()` 사이에 ISR 이 들어와 더 늦은 시각을 먼저 쓰면 줄 순서가 뒤집힌다. 디코더는 2^32 가까운 delta 로 보고 "거꾸로" 오류를 낸다 (또는 그 규칙이 없으면 71 분이 흘렀다고 믿는다).

```c
lock(&o); rec_locked(ts_now(), ev, who, arg); unlock(&o);      /* 시각 + 칸 = 한 덩어리 */
```

**2. DROP 을 무시한다.** 대조군이 보여 주듯 사라진 `SWITCH_IN` 하나가 다음 전환까지 이전 주인의 시간을 부풀린다 (C-2 에서 18120 µs). DROP 이후 다음 `SWITCH_IN` 까지는 `unknown` 이다.

**3. 같은 태스크 재선택을 컨텍스트 스위치로 센다.** `vTaskSwitchContext` 는 고른 태스크가 같아도 OUT/IN 을 찍는다. `if (d->task != ctx) n_switches++` 로 거른다 (테스트 B-6).

**4. 2의 거듭제곱이 아닌 cap 에 자유 증가 카운터.** `wr % 12` 는 `wr` 이 2^32 에서 0 으로 돌아가는 순간 칸 번호가 튄다. cap 을 2^k 로 강제하거나 카운터를 cap 에서 감아야 한다.

**5. 트레이스 훅 안에서 `printf`.** 훅은 틱 ISR(POSIX 포트에선 SIGALRM 핸들러)과 임계 구역 안에서 불린다. 기록만 하고 출력은 나중에 태스크에서 (RTOS-M13 과 같은 규칙).

**6. 초기값 하나가 시계를 멈춘다.** 데모의 실제 버그: 단조 보정용 `g_last_t = 0` 에서 첫 시각 4294667000 이 "거꾸로" 로 판정 → 300 ms 동안 시각 0 고정.

```c
g_last_t = (uint32_t)(g_tick * 1000u);   /* 시작 시각으로 초기화 */
```

**7. 커널 CPU % 와 트레이스 CPU % 가 다르다고 놀란다.** 커널 카운터는 ISR 시간을 그때 돌던 태스크에 넣는다. 트레이스는 ISR 을 따로 센다 — 같게 만들려면 run + stolen 으로 비교한다.

## 8. 면접 Q&A

<details>
<summary>Q1. 트레이스 레코더는 태스크와 ISR 양쪽에서 불리는데 어떻게 동기화하나요?</summary>

단일 코어에서는 기록 전체(시각 읽기 + 순번 + 칸 쓰기)를 **인터럽트를 끈 짧은 구간** 으로 만든다 — 그러면 모든 호출자가 줄을 서서 "단일 writer" 가 된다. 이 레슨의 M4 asm 에서 그 구간은 39 명령어다. 꼬리: "BASEPRI 로 하면?" → `configMAX_SYSCALL_INTERRUPT_PRIORITY` 위의 ISR 은 기록 중에도 들어오므로, 그 ISR 이 기록을 한다면 PRIMASK 여야 한다 (아니면 그 ISR 은 기록하지 않는다). SMP 에서는 코어별 버퍼 + 코어 번호를 레코드에 넣고 호스트에서 시각으로 합친다 (공유 락은 다른 코어를 기다리게 해 타이밍을 바꾼다). 락 없는 방법으로 "칸 예약 = atomic fetch_add, 다 쓰면 커밋 플래그" 도 있지만 리더가 덜 쓴 칸을 구분해야 한다.

</details>

<details>
<summary>Q2. 트레이싱이 타이밍을 바꾸지 않나요? (probe effect)</summary>

바꾼다. 이벤트당 수십 명령어 + 메모리 쓰기 + 인터럽트 잠금 지연. 그래서 ① 비용을 재서 예산에 넣고(이벤트율 × 이벤트 비용, 10 kHz ISR 을 다 찍으면 초당 2만 이벤트), ② 제품에서도 **켠 상태로 검증하고 켠 상태로 출하** 하는 게 정석이다 — 끄면 타이밍이 달라져 버그가 숨는다(heisenbug). ③ 고빈도 이벤트(10 kHz 전류 루프 ISR)는 필터링하거나 카운터로만 남긴다. 트레이스 대신 GPIO 토글 + 로직 분석기는 비용이 몇 사이클이지만 정보가 적다.

</details>

<details>
<summary>Q3. 스트리밍과 스냅샷(flight recorder) 중 언제 무엇을 쓰나요?</summary>

스트리밍(STOP 모드 + 리더)은 긴 시간의 통계·드문 사건 포착에 좋지만 대역폭이 필요하고 버스트에서 잃는다 — C-2 처럼 평균이 충분해도 깊이가 모자라면 잃는다(링 8 → 22개 손실, 32 → 0). 스냅샷(OVERWRITE)은 전송 없이 RAM 만 쓰고 "고장 직전 N개" 가 남는다 — HardFault/watchdog 핸들러에서 멈추면 원인이 그 안에 있다. 리셋 후에도 남기려면 `.noinit` RAM 에 두고 부팅 시 덤프. 꼬리: "링 크기는?" → 버스트 동안 (도착 − 전송) 의 최대 적체 (추가 연습 3), 평균 이벤트율이 회선보다 크면 크기와 상관없이 넘친다.

</details>

<details>
<summary>Q4. 32비트 µs 타임스탬프가 wrap 하면요? 사이클 카운터라면?</summary>

연속 이벤트 간격이 wrap 주기의 절반보다 짧다는 가정 아래 `delta = t - prev` (u32) 로 풀린다 — µs 면 35.8 분, 168 MHz CYCCNT 면 2^31 / 168e6 ≈ 12.8 초. 그보다 오래 이벤트가 없을 수 있으면 주기적인 이벤트(틱, 하트비트)를 넣어 간격을 보장하거나, 64비트 시각(16비트 하드웨어 카운터 + 소프트웨어 overflow 카운트 — 추가 연습 4 의 hi/lo 경쟁에 주의)을 쓴다. 이 레슨은 2^31 이상 점프를 오류로 잡아 실제 버그(실수 6)를 찾았다.

</details>

<details>
<summary>Q5. FreeRTOS 만으로 태스크별 CPU 사용률을 구하려면? 트레이스와 무엇이 다른가요?</summary>

`configGENERATE_RUN_TIME_STATS = 1` + 빠른 카운터(`portGET_RUN_TIME_COUNTER_VALUE` 또는 `portALT_...`) → `vTaskGetRunTimeStats` / `uxTaskGetSystemState` 의 `ulRunTimeCounter`. 커널은 전환 때마다 "지금 − 마지막 switch-in" 을 나가는 태스크에 더한다 (`tasks.c` 5245–5254). 데모에서 같은 시계를 물리자 트레이스(run + stolen)와 µs 까지 같았다. 차이: ① ISR 시간이 태스크에 섞인다, ② **누적값만** 있고 언제·누가 누구를 막았는지는 없다, ③ 카운터 overflow 보호가 없다 (5240–5244 주석: 넘친 뒤 값은 보장하지 않음) — 32비트 µs 카운터면 71 분, 1 MHz 보다 빠르면 더 일찍.

</details>

<details>
<summary>Q6. (꼬리) 트레이스에 손실이 있었습니다. 이 분석이 틀리는 경우는?</summary>

이 레슨의 규칙(모름은 아무에게도 안 줌)을 따르면 **귀속된 시간은 틀리지 않는다** (C-2: 틀린 귀속 0) — 하지만 ① CPU % 는 하한이 된다 (분모 span 에 unknown 이 들어 있음, 알려진 시간 기준으로 다시 정규화하면 편향될 수 있다: 손실은 바쁜 버스트에서 생기므로 무작위 표본이 아니다), ② 최대 응답 시간은 **잰 것 중** 최대 — 최악의 job 이 하필 구멍에 있을 수 있다 (C-2 에서 Ctrl 40개 중 38개만 잼), ③ 훅이 없는 코드는 보이지 않는다 — 훅 없는 ISR 시간은 깔린 태스크 몫으로 보인다, ④ 시각 자체가 틀릴 수 있다 (POSIX 포트처럼 tick 기반, 또는 잠금 밖 읽기). 그래서 보고서에는 drop 수와 unknown 비율을 같이 적는다.

</details>

<details>
<summary>Q7. (꼬리) 이걸 양산 제품에 상시 켜 두려면 어떻게 바꾸겠나요?</summary>

① OVERWRITE flight recorder 를 `.noinit` RAM 에, HardFault·watchdog·assert 핸들러에서 멈추고 다음 부팅에 플래시/로그로 덤프. ② 레코드를 델타 + varint 로 줄이고(추가 연습 2, 16 B → 3.45 B) 이벤트 클래스 필터(스위치·ISR·뮤텍스만, 큐 연산 제외). ③ 시각은 사이클 카운터, 64비트 확장은 잠금 안에서 UIF 확인 (추가 연습 4). ④ 트레이스 비용을 부팅 시 자가 측정해 로그에 남김. ⑤ SMP 면 코어별 링. ⑥ 필드에서 켜고 끄는 명령 + 버전 필드(`rtos-trace v1` 헤더처럼)로 포맷 호환.

</details>

<details>
<summary>Q8. 트레이스에서 잰 응답 시간이 RTA 결과보다 크다면 / 훨씬 작다면?</summary>

크다면 **분석 모델이 틀렸다** — 빠진 블로킹(B), 릴리즈 지터(J), ISR 간섭, 잘못된 C (RTOS-A04 확장 RTA 가 다루는 항목). 트레이스는 반례 하나로 충분하다. 작다면 정상이다: 관측은 실행된 시나리오의 최대일 뿐이고 critical instant(모든 상위 태스크가 동시에 릴리즈)가 안 나왔을 수 있다. 트레이스로 "가정이 맞나" (예: 이 뮤텍스는 정말 최대 200 µs 만 잡히나 — `MUTEX_TAKE` ~ `MUTEX_GIVE` 구간 최대) 를 검증해 RTA 입력을 고친다.

</details>

<details>
<summary>Q9. 우선순위 상속이 일어났다는 걸 트레이스에서 어떻게 보나요?</summary>

이 데모의 41.95–43.95 ms: `BLOCK Comm spi` 바로 뒤 `UNBLOCK Log` 가 찍힌다. FreeRTOS 는 상속할 때 보유자를 새 우선순위의 ready 리스트로 옮기며 `prvAddTaskToReadyList` 를 부르고(`tasks.c` 6791), 그 안의 `traceMOVED_TASK_TO_READY_STATE`(289)가 불린다 — 즉 "UNBLOCK" 이 사실은 재삽입이다. `MUTEX_GIVE Log spi` 뒤의 두 번째 `UNBLOCK Log` 는 해제(6895). 더 분명히 보려면 `traceTASK_PRIORITY_INHERIT/DISINHERIT` (FreeRTOS.h 669, 679) 를 포맷에 이벤트로 추가하면 된다 — 포맷 버전을 올릴 이유다.

</details>

## 9. 추가 연습

**연습 1 — backfill: 첫 `SWITCH_OUT X` 로 앞부분 되살리기.** 덮어쓰기 덤프는 중간부터 시작한다. 첫 전환 이벤트가 `SWITCH_OUT X` 라면 그 앞의 시간도 X 몫이라고 할 수 있나?

<details>
<summary>답</summary>

"이벤트가 하나도 빠지지 않은 구간" 에서만 된다. 시작(또는 DROP 뒤) 첫 **실제** 이벤트 시각 `known_from` 이후로는 이벤트가 완전하고, 그 사이 `SWITCH_IN` 이 없었으니 X 가 계속 돌았다 → `[known_from, OUT 시각]` 을 X 에게. DROP 마커 시각 ~ `known_from` 사이는 여전히 모름 (그 안에서 전환이 사라졌을 수 있다). 첫 전환이 `SWITCH_IN` 이면 그 전 주인은 알 수 없다. `p1_decode` 테스트: 150 µs 되살림, DROP 뒤에는 140 µs 만 되살리고 60 µs 는 모름.

</details>

**연습 2 — 델타 타임스탬프 + varint.** 레코드 = `[ev][id][Δt LEB128]` 으로 바꾸면 300개 이벤트가 몇 바이트인가? wrap 을 건너도 왕복이 맞나?

<details>
<summary>답</summary>

Δt 는 u32 뺄셈이라 wrap 을 건너도 맞다. 같은 시각 묶음(Δ0)은 1바이트, 10–990 µs 는 1–2바이트. 실제 출력: `300 events: fixed 16 B/rec = 4800 B, delta+varint = 1035 B (3.45 B/rec)`. 순번은 스트림 순서로 대신하고, 손실은 DROP 레코드가 개수를 실어 나른다. 비용: 디코드가 순차적이라 중간부터 읽을 수 없다 → 주기적으로 절대 시각을 넣는다.

</details>

**연습 3 — 링 크기 정하기.** 10 µs 걸음, 배경 100 µs 마다 1 이벤트 + CAN 버스트(30 µs 마다 ENTER/EXIT 2개 × 8). 회선 3 B/걸음과 6 B/걸음에서 손실 없는 최소 cap 은?

<details>
<summary>답</summary>

도착을 먼저 넣고 전송하는 규칙으로 "도착 직후 최대 적체" 를 구하면 그게 최소 cap 이다. 실제 출력: `min cap: link 3 B/step -> 15, link 6 B/step -> 11` — 각각 cap 에선 손실 0, cap−1 에선 손실 > 0 을 시뮬레이션으로 확인. 평균 이벤트율이 회선보다 크면(10 µs 마다 1개 = 100 k/s vs 18.75 k/s) 적체가 실행 길이에 비례해 자라서 cap 으로는 해결이 안 된다.

</details>

**연습 4 — 16비트 카운터로 32비트 타임스탬프 (hi/lo 경쟁).** TIM3(16비트, 1 µs) + update ISR 의 `ovf++`. 레코더가 **인터럽트를 끈 채** 시각을 읽으면 무엇이 깨지나?

<details>
<summary>답</summary>

`hi = ovf; lo = CNT;` 사이에 카운터가 넘치면 hi 가 낡는다. 인터럽트가 켜져 있으면 표준 해법 `do { hi = ovf; lo = CNT; } while (hi != ovf);` 이 잡는다. 그러나 잠금 안에서는 ISR 이 못 돌아 `ovf` 가 안 변하므로 루프가 아무것도 못 잡는다. 대신 펜딩 플래그(UIF)를 보고 `lo` 가 작으면(새 바퀴) `hi+1`. 실제 출력 (overflow 근처 시작점 40개 중 틀린 횟수): `naive on=2 off=2 | retry on=0 off=2 | masked on=2 off=0`.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 17/17 passed
```

## 10. 핵심 정리

- 트레이스 = 커널 훅 → **인터럽트를 끈 짧은 기록**(시각 + 순번 + 칸) → 링 → 리더 → 호스트 재구성. 시각은 반드시 잠금 안에서.
- 순번은 "시도" 마다 — 받은 쪽이 어디서 몇 개 잃었는지 안다. STOP 모드는 DROP 마커(첫 손실 시각 + 개수), OVERWRITE 는 앞부분 손실 = 첫 줄 DROP.
- 디코더: `SWITCH_IN` 이 주인을 바꾸고, ISR 은 따로(태스크엔 stolen), **DROP 이후·첫 SWITCH_IN 이전은 unknown** — 이 규칙이 없으면 C-2 에서 18120 µs 를 엉뚱한 태스크에 줬다.
- 32비트 wrap: `delta = t - prev` (u32), 2^31 이상은 오류 — 실제 버그를 잡았다.
- 같은 시계를 물리면 FreeRTOS `ulRunTimeCounter` = 트레이스 run + stolen (µs 까지 일치). 커널 통계는 누적만, 트레이스는 순서와 원인.
- 손실이 있으면 CPU % 는 하한, 최대 응답 시간은 "잰 것 중 최대". 링 깊이는 버스트, 회선은 평균이 정한다.
- POSIX 포트 트레이스는 순서·귀속 검증용 (600 tick = 벽시계 878 ms). 시간 측정은 보드에서 사이클 카운터로 (RTOS-A03).

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 — `include/FreeRTOS.h` 617–1003 (trace 매크로 기본값: 631, 638, 662, 669, 679, 688, 706, 729, 789, 797, 853, 857, 877, 993–1003), 2708 (`portALT_GET_RUN_TIME_COUNTER_VALUE` 검사), 2876–2881 (`configRUN_TIME_COUNTER_TYPE`); `tasks.c` 287–293 (`prvAddTaskToReadyList`), 506, 2472, 2512–2519, 3799–3811, 4831–4846, 5213–5274 (`vTaskSwitchContext`, 실행 시간 5231–5254), 6760–6810, 6880–6895 (상속/해제 재삽입), 8521–8550 (`ulTaskGetRunTimeCounter`); `queue.c` 60–66, 130–136, 979, 1125, 1615, 1698, 1787; `portable/ThirdParty/GCC/Posix/port.c` 406–429, 452–469, 488–512, 672–679, `portmacro.h` 77, 105–117, 143–145; `portable/GCC/ARM_CM4F/port.c` 560–584 — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- FreeRTOS — Trace Hook Macros — <https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-RTOS-trace-feature>
- FreeRTOS — Run Time Statistics — <https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/08-Run-time-statistics>
- SEGGER SystemView 사용자 설명서 (UM08027) — <https://www.segger.com/products/development-tools/systemview/> (원리 비교에만 사용, 미검증)
- Percepio Tracealyzer / TraceRecorder 문서 — <https://percepio.com/tracealyzer/> (원리 비교에만 사용, 미검증)
- Zephyr Project — Tracing — <https://docs.zephyrproject.org/latest/services/tracing/index.html> (문서 기준, 미검증)
- ARM, *ARMv7-M Architecture Reference Manual* — DWT `CYCCNT` (0xE0001004), PRIMASK/`CPSID`
- 관련 레슨: RTOS-B08 (틱 훅 = ISR 대용), RTOS-M02 (FromISR), RTOS-M04 (우선순위 상속), RTOS-M11 (RTA), RTOS-M13 (FreeRTOS 내부 + trace 매크로), RTOS-A03 (WCET·CYCCNT), RTOS-A04 (확장 RTA), RTOS-A09 (이 포맷으로 데드라인 미스 분석), RTOS-A10 (설계)
