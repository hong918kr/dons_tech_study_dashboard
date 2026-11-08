---
id: SIV-M03
time: 90분
updated: 2026-11-08
---
# SIV-M03 인터럽트 지연·유실 검출

## 학습 목표

- 이벤트마다 **trigger 타임스탬프(HW 캡처)** 와 **ISR 진입 타임스탬프** 를 남기고, 32-bit wrap 카운터에서도 맞는 latency(지연)를 계산한다 — 음수(in-flight) 경우까지.
- latency 를 **선형 bin 히스토그램** 으로 모으고 nearest-rank 분위수(p50/p99/p99.9)의 **상한** 을 bin 하나 오차 안에서 구한다. 평균이 아니라 max·꼬리로 판정하는 이유를 말할 수 있다.
- 16-bit **sequence 번호** 를 serial 산술(RFC 1982)과 64-bit 슬라이딩 윈도로 검사해 **유실(GAP)·중복(DUP)·순서 뒤바뀜(REORDER)** 을 wrap 을 넘어서도 정확히 센다.
- ISR 이 TICK 을 **늦게 clear** 하면 인터럽트가 합쳐지고(coalesce) 레코드가 FIFO 에 갇히는(stranded) 레이스를 재현하고, "레코드가 있으면 TICK 도 있다" 불변식으로 잡는다.
- 결함 6종(유실·중복·순서·타임스탬프·오버플로·늦은 clear)을 주입해 **설명되는 유실(OVF)** 과 **조용한 유실(HW 버그)** 을 구분하는 판정과 한 줄 리포트를 만든다.

## 1. 문제

SIV-B11 이 "인터럽트가 **오는가**" 를 봤다면, 이 레슨은 "**제때, 하나도 빠짐없이, 순서대로** 오는가" 를 본다 — 실시간 센서 SoC(레이더 프레임 동기, 모터 PWM, ADC 샘플링)에서 first silicon 이후 가장 오래 끄는 버그가 간헐적 인터럽트 유실·지연이고, 검증 면접의 단골 꼬리질문("인터럽트 몇 개 왔는지 세면 되지 않나요?")이 바로 여기서 나온다.

가상의 센서 SoC 에서 프레임 동기 타이머 **TMR0** 가 40 kHz(주기 P = 25 µs)로 이벤트를 만든다. 이벤트마다 IP 는 `{seq(16-bit), trigger 시각(32-bit free-running 카운터, 1 ns/LSB)}` 레코드를 깊이 4 의 **이벤트 FIFO** 에 넣고 `IRQ_RAW.TICK` 을 세운다(W1C, sticky — SIV-B11 의 RAW 와 같은 성격). 실칩의 CAN RX FIFO 타임스탬프, PTP 타임스탬프 FIFO, 입력 캡처 FIFO 가 이런 모양이다.

| off | 이름 | access | 의미 |
|---|---|---|---|
| `0x00` | `IRQ_RAW` | W1C | bit0 `TICK`(push 마다 1) · bit1 `OVF`(FIFO 가 차서 레코드를 버림) |
| `0x04` | `IRQ_EN` | RW | 1 = enable |
| `0x08` | `FIFO_LVL` | RO | 남은 레코드 수 (0..4) |
| `0x0C` | `EVT_SEQ` | RO | head 레코드의 seq (peek) |
| `0x10` | `EVT_TS` | RC | head 레코드의 trigger 시각 — **읽으면 pop** |
| `0x14` | `OVF_CNT` | RO | 버려진 레코드 수 |

호스트에는 칩이 없으므로 `reg_read32/reg_write32` 뒤에 mock 을 두고(SIV-B02 의 mock 을 축약해 넣었다), CPU 와 ISR 은 **이산 사건 시뮬레이션(discrete-event simulation)** 으로 돌린다. 시간은 전부 시뮬레이션 ns 라서 결과가 매번 같다(호스트 시계는 demo 에서만 쓴다).

- CPU: 스레드 코드가 주기마다 한 번 0.2~9 µs 동안 인터럽트를 끈다(critical section, xorshift32 고정 seed). 라인이 서면 마스크가 끝난 뒤 **T_ENTRY = 120 ns**(Cortex-M4 의 12 cycle @100 MHz, zero-wait 메모리 기준) 후 ISR 진입. ISR 이 끝났을 때 라인이 여전히 high 면 **tail-chaining 60 ns**(6 cycle)로 바로 재진입. 레지스터 접근 1회 20 ns.
- ISR: TICK clear → `while (FIFO_LVL) { seq = EVT_SEQ; ts = EVT_TS; log(seq, ts, entry_ts) }` → 본문 18 µs. 정책 두 가지: **clear-first**(위 순서) / **clear-last**(본문까지 끝낸 뒤 clear — 흔한 드라이버 실수).
- 모델 단순화: 마스크 구간은 절대 시각으로 고정이고 ISR **진입만** 막는다(선점은 모델링 안 함).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m03a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">이벤트 1개의 일생: trigger → FIFO → (마스크) → ISR 진입 → pop → 검사기</text>
  <line x1="40" y1="70" x2="720" y2="70" stroke="currentColor"/>
  <text x="10" y="74" fill="currentColor">t</text>
  <line x1="90" y1="50" x2="90" y2="90" stroke="currentColor" stroke-width="2"/>
  <text x="60" y="44" fill="currentColor">tick k (seq, trig_ts)</text>
  <rect x="70" y="100" width="210" height="26" class="f-warn"/>
  <text x="80" y="118" fill="currentColor">CPU 마스크 (critical section)</text>
  <line x1="280" y1="50" x2="280" y2="90" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="250" y="44" fill="currentColor">마스크 끝</text>
  <line x1="320" y1="50" x2="320" y2="90" stroke="currentColor" stroke-width="2"/>
  <text x="300" y="100" fill="currentColor" font-size="12">+120 ns</text>
  <text x="330" y="44" fill="currentColor">ISR 진입 (entry_ts)</text>
  <rect x="320" y="56" width="70" height="14" class="f-acc"/>
  <text x="400" y="66" fill="currentColor" font-size="12">clear · LVL · SEQ · TS(pop)</text>
  <rect x="590" y="56" width="130" height="14" class="f-mut"/>
  <text x="600" y="94" fill="currentColor" font-size="12">본문 work 18 us</text>
  <line x1="90" y1="140" x2="320" y2="140" stroke="currentColor" marker-end="url(#m03a)"/>
  <line x1="320" y1="140" x2="90" y2="140" stroke="currentColor" marker-end="url(#m03a)"/>
  <text x="120" y="158" fill="currentColor">latency = entry_ts - trig_ts (32-bit wrap 차이)</text>
  <rect x="20" y="190" width="200" height="90" fill="none" stroke="currentColor"/>
  <text x="30" y="210" fill="currentColor" font-weight="bold">TMR0 이벤트 FIFO (깊이 4)</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <rect x="30" y="222" width="44" height="22" class="f-ok"/><text x="34" y="238">k</text>
    <rect x="76" y="222" width="44" height="22" class="f-ok"/><text x="80" y="238">k+1</text>
    <rect x="122" y="222" width="44" height="22" class="f-mut"/>
    <rect x="168" y="222" width="44" height="22" class="f-mut"/>
    <text x="30" y="268">full 에서 push → OVF_CNT++</text>
  </g>
  <line x1="220" y1="235" x2="290" y2="235" stroke="currentColor" marker-end="url(#m03a)"/>
  <rect x="292" y="190" width="200" height="90" class="f-acc"/>
  <text x="302" y="210" fill="currentColor" font-weight="bold">Part A 히스토그램</text>
  <text x="302" y="232" fill="currentColor" font-size="12">bin 500 ns, p50/p99/max</text>
  <text x="302" y="252" fill="currentColor" font-size="12">예산 10 us 초과 = DEADLINE</text>
  <rect x="504" y="190" width="216" height="90" class="f-acc"/>
  <text x="514" y="210" fill="currentColor" font-weight="bold">Part B seq 검사기</text>
  <text x="514" y="232" fill="currentColor" font-size="12">GAP · DUP · REORDER (16-bit wrap)</text>
  <text x="514" y="252" fill="currentColor" font-size="12">Part C: OVF 로 설명되나? 불변식?</text>
  <line x1="492" y1="235" x2="502" y2="235" stroke="currentColor"/>
</svg>
```

주입 가능한 결함(`fault_t`): `drop_at_wrap`(seq 가 0x0000 이 되는 이벤트가 흔적 없이 사라짐), `pop_stall_every`(N 번째 pop 마다 read 포인터 정지 → 같은 레코드 두 번), `swap_backlog`(2개 이상 쌓이면 head 다음 것을 먼저 내줌), `ts_at_pop`(EVT_TS 가 trigger 시각이 아니라 읽은 순간의 시각). 그리고 시스템 조건 `long_mask`(140 µs 인터럽트 금지 — flash erase 같은)와 소프트웨어 버그 `clear-last`.

### Part A — wrap-safe latency 와 히스토그램

- `lat_of(rec, &inflight)` — `entry_ts - trig_ts` 를 wrap 에 안전하게 부호 있는 값으로. 음수면 레코드가 ISR 진입 **뒤에** 도착해 같은 drain 루프에 잡힌 것(in-flight) → `pop_ts - trig_ts` 로 재고 플래그를 세운다.
- `hist_add(h, v)` — 500 ns bin 64개 + overflow bin, n/min/max/sum.
- `hist_quantile(h, num, den)` — nearest-rank 분위수의 상한: 정확한 값 x 에 대해 `x ≤ q ≤ x + bin − 1`.
- 정상 실리콘 + clear-first ISR 10,000 이벤트에서 예산(10 µs) 초과 0, 히스토그램을 출력한다. 카운터는 시작 100 µs 만에 wrap 하게(`ts0 = 0xFFFFFFFF − 99999`) 설정돼 있다.

### Part B — seq 검사기: 유실·중복·순서 뒤바뀜

- `seq_feed(c, s, idx)` — `d = s − next` 를 16-bit serial 차이로. `d > 0` 이면 GAP(lost += d), `d < 0` 이면 64-bit 윈도에서 이미 받은 것(DUP) / 비어 있던 자리(REORDER, lost 되돌림) / 윈도 밖(STALE). 종류별 **첫 이상** 의 위치·받은 값·기대값을 남긴다.
- `seq_finish(c, end_next)` — 끝까지 안 온 꼬리 유실(TAIL)과 초과(extra).
- seq 는 `0xFFF0` 에서 시작해 16번째 이벤트에서 wrap 한다. `drop_at_wrap` 은 정확히 `rec#16 got=0x0001 exp=0x0000` 으로 잡혀야 한다.

### Part C — 결함 주입 캠페인과 '늦은 clear' 합쳐짐

- `judge(run, analysis, &v)` — 유실 중 **설명되는 것**(`OVF_CNT` + 끝까지 FIFO 에 남은 레코드)을 빼고 남는 것만 SILENT_LOSS. 원인 7종을 플래그로, 가장 우선순위 높은 것을 primary 로.
- 주어진 캠페인이 8개 시나리오를 돌린다: 정상 PASS, 결함마다 정해진 primary·flags·첫 서명. `swap_backlog` 는 정상 부하에서는 **통과**(커버리지 구멍)하고 `long_mask` 스트레스와 겹칠 때만 REORDER 로 잡힌다.
- clear-last ISR 은 합쳐짐(한 진입에 레코드 2개)과 latency ≈ P 의 꼬리를 만든다. ISR 본문 길이 sweep 으로 clear-first 는 항상 깨끗하고 clear-last 는 본문이 길수록 합쳐짐이 늘어남을 보인다.

## 2. Problem (English)

> **Interrupt latency and loss detection.**
>
> A periodic timer IP pushes a record `{seq (16-bit), trigger timestamp (32-bit free-running, 1 ns/LSB)}` into a 4-deep event FIFO on every tick and sets a sticky W1C `TICK` bit; on overflow it drops the record and increments `OVF_CNT`. An ISR drains the FIFO and logs, per record, `(seq, trig_ts, entry_ts, pop_ts)`. Both counters wrap.
>
> 1. `int32_t lat_of(const rec_t *r, bool *inflight)` — wrap-safe latency `entry_ts − trig_ts`; if negative the record arrived after ISR entry: return `pop_ts − trig_ts` and set `*inflight`. Avoid implementation-defined unsigned-to-signed conversion.
> 2. `void hist_add(hist_t *h, int32_t v)` / `int32_t hist_quantile(const hist_t *h, uint32_t num, uint32_t den)` — linear-bin histogram with an overflow bin; return an upper bound of the nearest-rank quantile that is at most one bin above the exact value (or `max` for the overflow bin); −1 if empty or `num > den`.
> 3. `void seq_feed(seqchk_t *c, uint16_t s, uint32_t idx)` / `void seq_finish(seqchk_t *c, uint16_t end_next)` — count lost, duplicate, reordered and stale sequence numbers across 16-bit wrap using a 64-entry sliding window; record the first occurrence of each anomaly. A late arrival that fills a gap is a reorder, not a loss.
> 4. `void judge(const run_t *R, const analysis_t *a, verdict_t *v)` — losses explained by `OVF_CNT` or by records still stuck in the FIFO are not silent; flag TS mismatch, silent loss, duplicate, reorder, stranded record, overflow and deadline miss; pick the highest-priority cause.
>
> Example: sequence `1, 3, 2, 4` → lost 0, reorder 1. Sequence `0xFFFF, 0x0001, 0x0000, 0x0002` → reorder 1 (wrap). An ISR that clears `TICK` after draining leaves a record in the FIFO with no pending interrupt; it is serviced one period late.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 깨짐 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/SIV-M03_st && /tmp/SIV-M03_st
```

실제로 돌린 결과 (앞부분과 끝):

```text
FAIL starter.c:441  lok == (int)(sizeof lt / sizeof lt[0])
FAIL starter.c:450  h.n == 8 && h.cnt[0] == 3 && h.cnt[1] == 1 && h.cnt[2] == 1 && h.cnt[9] == 1
FAIL starter.c:451  h.cnt[50] == 1 && h.cnt[HBINS] == 1 && h.min == 0 && h.max == 10000 && h.sum == 16498
FAIL starter.c:452  hist_quantile(&h, 1, 2) == 199
...
[drop_at_wrap] PASS
  recs=9999 entries=9999 multi=0 empty=0 lost=0(silent=0 ovf=0 left=0) dup=0 reorder=0 strand=0 late=0 p50<=-1 p99<=-1 max=-2147483648 ns
  (expected primary=1 flags=0x2, got primary=-1 flags=0x0)
...
10/36 passed
```

mock(TMR0 레지스터·FIFO·결함), 마스크 생성, CPU/ISR 시뮬레이터(`sim_run`), 분석 루프(`analyze`), 리포트(`print_verdict`, `hist_print`)는 주어진 코드다. 채울 곳은 `lat_of`, `hist_add`, `hist_quantile`, `seq_feed`, `seq_finish`, `judge` 여섯 개. starter 에서 `[drop_at_wrap] PASS` 가 찍히는 게 핵심 교훈이다 — **검사기가 비어 있으면 모든 결함이 PASS 로 보인다.** 이벤트 9,999개만 왔는데도 아무도 모른다.

## 4. 힌트

<details>
<summary>힌트 1 — wrap 차이는 unsigned 로 빼고, 부호 해석만 따로</summary>

`entry_ts - trig_ts` 를 `uint32_t` 로 빼면 wrap 을 넘어도 모듈러 차이가 정확하다(SIV-B09 의 타임아웃 루프와 같은 원리). 문제는 "음수" 해석이다. `(int32_t)u` 는 u > INT32_MAX 일 때 C11 6.3.1.3p3 의 **구현 정의** 변환이다. 주어진 `s32_of()` 는 `~u` 를 써서 구현 정의 없이 같은 값을 만든다.

</details>

<details>
<summary>힌트 2 — 분위수는 누적 개수가 rank 에 닿는 bin</summary>

nearest-rank: `r = ceil(n·num/den)` (정수로 `(n*num + den - 1) / den`, 최소 1). bin 을 앞에서부터 누적해 처음으로 `≥ r` 가 되는 bin b 의 위쪽 끝 `(b+1)·bin − 1` 을 돌려주되, `max` 보다 크면 `max`. overflow bin 이면 위쪽 끝이 없으니 `max`.

</details>

<details>
<summary>힌트 3 — 윈도 bit i = "top − i 를 받았나"</summary>

`top = next − 1`. 앞으로 d 칸 가면 윈도를 `d+1` 칸 왼쪽으로 밀고 bit0 를 세운다(64 이상 밀면 UB 이므로 `d >= 63` 이면 그냥 1). 뒤로 오면 `i = −d − 1`. 처음 상태를 `~0` 으로 두면 "시작보다 오래된 seq" 가 DUP 로 분류되고 lost 가 음수로 가는 일이 없다.

</details>

<details>
<summary>힌트 4 — 판정의 핵심은 "설명되는 유실"</summary>

`lost` 는 seq 검사기가 본 구멍 수다. 그중 `OVF_CNT` 만큼은 HW 가 스스로 "버렸다" 고 보고했고, `end_lvl` 만큼은 아직 FIFO 안에 있다(STRANDED 로 따로 보고). 나머지가 HW 가 **말없이** 잃은 것 = SILENT_LOSS. 우선순위는 enum 순서(TS 가 1순위 — 계측기가 틀리면 다른 숫자를 믿을 수 없다).

</details>

## 5. 풀이

### Part A — latency 를 무엇으로 재나, 어떻게 모으나

**trigger 시각은 누가 찍나.** 소프트웨어가 ISR 안에서 "지금" 을 읽으면 진입 시각만 알 뿐 이벤트가 **언제** 났는지는 모른다. 그래서 latency 측정은 언제나 "이벤트 쪽 타임스탬프 + 진입 쪽 타임스탬프" 두 개가 필요하다.

| 방법 | trigger 시각 | 진입 시각 | 장단점 |
|---|---|---|---|
| HW 캡처 타임스탬프 (이 레슨) | IP 가 이벤트 순간 free-running 카운터를 래치 | ISR 첫 줄에서 같은 카운터 읽기 | 이벤트마다 자동, 장시간 통계 가능. 두 값이 **같은 시간축** 이어야 함 |
| GPIO 토글 + 로직 분석기/오실로스코프 | 이벤트 신호(타이머 출력 핀) | ISR 첫 줄에서 GPIO set | 외부 독립 계측 — 계측기 검증용으로 최고. 채널·메모리 한계 |
| Cortex-M `DWT->CYCCNT` | 타이머 compare 값에서 역산 | ISR 첫 줄에서 CYCCNT | cycle 정밀도, 코어 내부만. `DEMCR.TRCENA` 필요 |
| 트레이스(ETM/ITM, Linux ftrace `irq_handler_entry`) | 트레이스 이벤트 | 트레이스 이벤트 | 비침투적, 대신 셋업·대역폭 |

```c
static int32_t lat_of(const rec_t *r, bool *inflight) {
    int32_t d = s32_of(r->entry_ts - r->trig_ts);      /* unsigned 뺄셈 = wrap 안전 */
    if (d >= 0) { *inflight = false; return d; }
    *inflight = true;                                   /* 진입 뒤 도착 → drain 루프가 잡음 */
    return s32_of(r->pop_ts - r->trig_ts);
}
```

- **wrap**: 테스트 표의 `{trig=0xFFFFFFF0, entry=0x00000010} → 32`. 32-bit 1 ns 카운터는 4.29 초마다 wrap 하므로 밤새 도는 soak 테스트에서는 **반드시** 넘는다. 이 레슨은 시작 100 µs 만에 넘게 해서 정상 시나리오 자체가 wrap 테스트가 되게 했다.
- **in-flight**: clear-first ISR 에서 clear 와 마지막 LVL 읽기 사이에 도착한 레코드는 같은 루프가 pop 한다. 그 레코드의 `entry_ts − trig_ts` 는 음수다. `uint32_t lat` 로 받으면 4,294,967,xxx ns 라는 괴물 값이 overflow bin 에 들어가 "max latency 4.3 초" 라는 거짓 리포트가 된다(§7).
- **unsigned → signed**: `s32_of(u) = u <= INT32_MAX ? (int32_t)u : -(int32_t)(~u) - 1`. gcc/clang 은 `(int32_t)u` 를 모듈러로 정의해 두지만 C11 에서는 구현 정의라, 검증 코드처럼 여러 컴파일러(호스트, arm-none-eabi, 벤더 컴파일러)를 오가는 코드에서는 이렇게 써 두는 게 안전하다.

**히스토그램.** latency 는 "대부분 짧고 가끔 길다" 는 꼬리 분포라 평균은 거의 정보가 없다. 실시간 요구사항은 "최악이 예산 안" 이므로 **max 와 p99/p99.9** 를 본다.

```c
static int32_t hist_quantile(const hist_t *h, uint32_t num, uint32_t den) {
    if (h->n == 0 || den == 0 || num > den) return -1;
    uint64_t r = ((uint64_t)h->n * num + den - 1) / den;   /* nearest-rank, 1-based */
    if (r == 0) r = 1;
    uint64_t c = 0;
    for (uint32_t b = 0; b <= HBINS; b++) {
        c += h->cnt[b];
        if (c < r) continue;
        if (b == HBINS) return h->max;                       /* overflow bin: 위쪽 끝 없음 */
        int64_t edge = (int64_t)(b + 1) * h->bin_ns - 1;
        return edge < h->max ? (int32_t)edge : h->max;
    }
    return h->max;
}
```

r 번째 값 x 는 bin b 안에 있으므로 `b·bin ≤ x ≤ (b+1)·bin − 1`, 그리고 `x ≤ max`. 둘 중 작은 값을 돌려주면 `x ≤ q < x + bin` — 테스트가 무작위 2,000 개를 qsort 한 정확한 값과 비교해 확인한다. 선형 bin 은 오차가 **절대값**(≤ 499 ns)으로 고정, log2 bin(추가 연습 1)은 오차가 **비율**(< 2배)로 고정이다. 예산 판정처럼 특정 구간을 정밀하게 봐야 하면 선형, 범위가 수십 배를 넘나들면 log2.

정상 시나리오 실제 출력:

```text
---- Part A: TMR0 nominal, 10000 events, P=25000 ns, ISR clear-first ----
  n=10000 min=119 mean=663 max=9014 ns  p50<=499 p90<=2499 p99<=7499 p99.9<=8999  bus_acc=50003
    0.0-  0.5 us |######################################## 8277
    0.5-  1.0 us |## 220
    1.0-  1.5 us |# 209
    ...
    8.5-  9.0 us |# 13
    9.0-  9.5 us |# 1
```

읽는 법:

1. **82.8% 가 0–0.5 µs** — tick 이 마스크 밖에 떨어지면 latency = T_ENTRY 120 ns. 나머지는 마스크 길이(0.2–9 µs)를 따라 거의 선형으로 줄어드는 꼬리. 히스토그램 모양이 **원인 분포의 지문** 이다: 실칩에서 꼬리 끝이 9 µs 에서 딱 잘리면 "가장 긴 critical section 이 9 µs" 라는 뜻이고, 그걸 찾으러 간다.
2. **min = 119 < 120** — tail-chaining 이다. tick 이 ISR 본문 중에 도착하면 ISR 이 끝나는 순간 60 ns 만에 재진입하므로, 도착 시각에 따라 120 ns 보다 짧을 수 있다. "최소 latency = 진입 cycle 수" 라는 가정이 깨지는 정상 사례.
3. **max 9,014 ns < 예산 10,000 ns** — PASS. 하지만 10,000 번에 0 번이라는 사실이 말해 주는 건 "초과 확률 < 약 3e-4 (95%)" 까지다(추가 연습 3, rule of three).
4. `bus_acc=50003` = 이벤트당 clear 1 + LVL 2 + SEQ 1 + TS 1 = 5, + EN 쓰기 1, + 끝의 OVF_CNT/LVL 2.

### Part B — seq 로 유실·중복·순서를 센다

인터럽트 **개수** 는 이벤트 개수가 아니다(합쳐짐, spurious). 이벤트에 붙은 **번호** 를 봐야 한다. 번호는 유한 비트라 wrap 하므로 비교는 serial 산술(RFC 1982)로 한다: `d = (int16)(s − next)` 가 양수면 "앞", 음수면 "뒤". 16-bit 이면 ±32,767 안에서만 의미가 있다 — 40 kHz 면 0.82 초 이상 한 번도 못 받으면 앞뒤를 판단할 수 없다는 뜻이고, 그래서 HW seq 폭은 "최대 공백 시간 × 이벤트율" 보다 넉넉하게 정한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">64-bit 윈도: bit i = seq (top − i) 를 받았나 — FIFO swap 결함으로 B, C, D, A 순서 도착</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="10" y="56">받음</text><text x="80" y="56">d</text><text x="130" y="56">판정</text><text x="250" y="56">윈도 (bit3 bit2 bit1 bit0)</text><text x="560" y="56">lost</text><text x="620" y="56">reorder</text>
  </g>
  <line x1="10" y1="64" x2="730" y2="64" stroke="currentColor" opacity=".35"/>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="10" y="90">B</text><text x="80" y="90">+1</text><text x="130" y="90">GAP (A)</text>
    <text x="10" y="128">C</text><text x="80" y="128">0</text><text x="130" y="128">순서대로</text>
    <text x="10" y="166">D</text><text x="80" y="166">0</text><text x="130" y="166">순서대로</text>
    <text x="10" y="204">A</text><text x="80" y="204">-4</text><text x="130" y="204">i=3 빈칸→REORDER</text>
    <text x="560" y="90">1</text><text x="620" y="90">0</text>
    <text x="560" y="128">1</text><text x="620" y="128">0</text>
    <text x="560" y="166">1</text><text x="620" y="166">0</text>
    <text x="560" y="204">0</text><text x="620" y="204">1</text>
  </g>
  <g font-family="monospace" font-size="12">
    <rect x="270" y="74" width="60" height="24" class="f-ok"/><rect x="340" y="74" width="60" height="24" class="f-ok"/><rect x="410" y="74" width="60" height="24" class="f-warn"/><rect x="480" y="74" width="60" height="24" class="f-ok"/>
    <rect x="270" y="112" width="60" height="24" class="f-ok"/><rect x="340" y="112" width="60" height="24" class="f-warn"/><rect x="410" y="112" width="60" height="24" class="f-ok"/><rect x="480" y="112" width="60" height="24" class="f-ok"/>
    <rect x="270" y="150" width="60" height="24" class="f-warn"/><rect x="340" y="150" width="60" height="24" class="f-ok"/><rect x="410" y="150" width="60" height="24" class="f-ok"/><rect x="480" y="150" width="60" height="24" class="f-ok"/>
    <rect x="270" y="188" width="60" height="24" class="f-ok"/><rect x="340" y="188" width="60" height="24" class="f-ok"/><rect x="410" y="188" width="60" height="24" class="f-ok"/><rect x="480" y="188" width="60" height="24" class="f-ok"/>
  </g>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="276" y="91">old</text><text x="346" y="91">old</text><text x="416" y="91">A?</text><text x="486" y="91">B</text>
    <text x="276" y="129">old</text><text x="346" y="129">A?</text><text x="416" y="129">B</text><text x="486" y="129">C</text>
    <text x="276" y="167">A?</text><text x="346" y="167">B</text><text x="416" y="167">C</text><text x="486" y="167">D</text>
    <text x="276" y="205">A</text><text x="346" y="205">B</text><text x="416" y="205">C</text><text x="486" y="205">D</text>
  </g>
  <text x="10" y="238" fill="currentColor">강조 = 비어 있는 칸(아직 못 받은 seq). 늦게 온 A 가 빈칸을 채우면 GAP 을 되돌리고 REORDER 로 센다.</text>
</svg>
```

```c
static void seq_feed(seqchk_t *c, uint16_t s, uint32_t idx) {
    int32_t d = s16_of((uint16_t)(s - c->next));
    c->n++;
    if (d >= 0) {
        if (d > 0) { c->lost += (uint32_t)d; seq_note(c, SQ_GAP, idx, s); }
        c->win = d >= 63 ? 1ull : (c->win << (d + 1)) | 1ull;     /* 64 이상 shift 는 UB */
        c->next = (uint16_t)(s + 1u);
    } else {
        uint32_t i = (uint32_t)(-d) - 1u;
        if (i >= 64) { c->stale++; seq_note(c, SQ_STALE, idx, s); }
        else if ((c->win >> i) & 1ull) { c->dup++; seq_note(c, SQ_DUP, idx, s); }
        else { c->win |= 1ull << i; c->reorder++; c->lost--; seq_note(c, SQ_REORDER, idx, s); }
    }
}
```

| 입력 (first) | lost | dup | reorder | stale | 첫 이상 |
|---|---|---|---|---|---|
| `FFFE FFFF 0000 0001` | 0 | 0 | 0 | 0 | 없음 — wrap 은 이상이 아니다 |
| `1 2 4 5` | 1 | 0 | 0 | 0 | GAP idx 2 (exp 3, got 4) |
| `1 2 2 3` | 0 | 1 | 0 | 0 | DUP idx 2 |
| `1 3 2 4` | 0 | 0 | 1 | 0 | REORDER idx 2 |
| `FFFF 0001 0000 0002` | 0 | 0 | 1 | 0 | REORDER — wrap 을 사이에 둔 뒤바뀜 |
| `11 12 13 10 14` (first 10) | 0 | 0 | 1 | 0 | FIFO swap 의 실제 모양 |
| `1 100 2` | 98 | 0 | 0 | 1 | 2 는 윈도(64) 밖 → STALE: 중복인지 늦은 도착인지 **판단 불가** |
| `9 10` (first 10) | 0 | 1 | 0 | 0 | 시작 이전 seq → DUP (윈도 초기값 `~0`) |
| `1 2` 끝 = 5 | 2 | 0 | 0 | 0 | TAIL — 마지막 이벤트들의 유실은 끝에서만 보인다 |

이 구조는 새로 만든 게 아니다. RTP 수신기의 seq 검사(RFC 3550 부록 A.1/A.3)와 IPsec anti-replay 윈도(RFC 4303 §3.4.3)가 같은 "serial 차이 + 비트맵 윈도" 다. 윈도 폭은 **예상되는 최대 뒤바뀜 거리** 보다 커야 한다 — FIFO 깊이 4 면 4로도 되지만, 64 로 두면 비트 연산 한 번이고 여유가 있다.

퍼즈 실제 출력 (0xFF00 부터 5,000 개, wrap 포함, 유실 2%·중복 1%·인접 swap 1% 를 심음):

```text
---- Part B: fuzz planted drop=112 dup=52 swap=47 -> lost=112 dup=52 reorder=47 stale=0
```

시뮬레이션 수준에서도 `drop_at_wrap` 이 `rec#16 got=0x0001 exp=0x0000` 으로 잡힌다(seq0 = 0xFFF0 이라 16번째가 0x0000). **wrap 순간에만 사는 버그** 는 카운터 폭이 넓을수록 보기 어렵다 — 32-bit seq 라면 40 kHz 에서 30 시간에 한 번이다. 그래서 검증에서는 카운터를 wrap 직전 값으로 **미리 써 두고** 시작하는 게 정석이다(이 레슨의 `seq0`, `ts0` 처럼).

### Part C — 늦은 clear, 캠페인, 판정

**clear-first vs clear-last.** TICK 이 sticky 이벤트 비트일 때 순서가 전부다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m03b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">tick k+1 이 ISR 본문 중에 도착할 때 (lat_k 가 길어서)</text>
  <text x="10" y="56" fill="currentColor" font-weight="bold">clear-first</text>
  <rect x="120" y="40" width="30" height="24" class="f-ok"/><text x="122" y="57" fill="currentColor" font-size="11">CLR</text>
  <rect x="150" y="40" width="60" height="24" class="f-acc"/><text x="155" y="57" fill="currentColor" font-size="11">pop k</text>
  <rect x="210" y="40" width="250" height="24" class="f-mut"/><text x="300" y="57" fill="currentColor" font-size="12">work 18 us</text>
  <line x1="330" y1="30" x2="330" y2="74" stroke="currentColor" stroke-width="2"/>
  <text x="300" y="88" fill="currentColor" font-size="12">tick k+1: push, TICK=1</text>
  <rect x="470" y="40" width="80" height="24" class="f-acc"/><text x="474" y="57" fill="currentColor" font-size="11">pop k+1</text>
  <line x1="460" y1="52" x2="468" y2="52" stroke="currentColor" marker-end="url(#m03b)"/>
  <text x="560" y="57" fill="currentColor" font-size="12">tail-chain 60 ns → 정상</text>
  <text x="10" y="146" fill="currentColor" font-weight="bold">clear-last</text>
  <rect x="120" y="130" width="60" height="24" class="f-acc"/><text x="125" y="147" fill="currentColor" font-size="11">pop k</text>
  <rect x="180" y="130" width="250" height="24" class="f-mut"/><text x="270" y="147" fill="currentColor" font-size="12">work 18 us</text>
  <rect x="430" y="130" width="30" height="24" class="f-warn"/><text x="432" y="147" fill="currentColor" font-size="11">CLR</text>
  <line x1="330" y1="120" x2="330" y2="164" stroke="currentColor" stroke-width="2"/>
  <text x="250" y="178" fill="currentColor" font-size="12">tick k+1: push, TICK 이미 1 (변화 없음)</text>
  <text x="400" y="196" fill="currentColor" font-size="12">CLR 이 k+1 의 인터럽트까지 지움 → LVL=1, TICK=0</text>
  <rect x="120" y="212" width="600" height="24" class="f-warn"/>
  <text x="130" y="229" fill="currentColor" font-size="12">레코드 k+1 은 FIFO 에 갇힘(stranded) → tick k+2 의 인터럽트 때 k+1, k+2 를 함께 pop (합쳐짐)</text>
  <text x="10" y="266" fill="currentColor">결과: 진입 수 = 이벤트 수 − 합쳐진 수, k+1 의 latency ≈ P + 120 ns. 마지막 이벤트였다면 영원히 안 나온다.</text>
</svg>
```

clear-last 는 "본문 처리 중 들어온 이벤트" 를 지우는 것이다. 이 레이스는 lat_k + 본문 > P 일 때만 열리므로 정상 부하에서는 몇 시간에 한 번 일어나는 간헐 버그가 된다. 고치는 방법은 셋: ① **clear 먼저, 그다음 drain**(이 레슨의 good ISR) ② clear 후 LVL 을 한 번 더 확인하고 남아 있으면 다시 루프 ③ HW 가 TICK 을 "FIFO 비어 있지 않음" 의 **레벨** 로 만들기(그러면 SW clear 자체가 필요 없다 — HW 리뷰 때 제안할 것).

검출은 **불변식** 으로 한다: "FIFO 에 레코드가 있으면 TICK 도 pending 이어야 한다". 시뮬레이터는 ISR 종료 시점마다 이 불변식을 본다(실칩이면 테스트 빌드의 ISR 끝에서 `FIFO_LVL` 과 `IRQ_RAW` 를 읽는다, 접근 2회 추가). latency 꼬리만으로는 "CPU 가 오래 막혔다(long_mask)" 와 "인터럽트를 잃었다(clear-last)" 를 **구분할 수 없다** — 둘 다 한 진입에 여러 레코드, latency ≥ P 로 보인다. 관측점을 하나 더 둬야 원인이 갈린다.

```c
static void judge(const run_t *R, const analysis_t *a, verdict_t *v) {
    uint32_t explained = R->ovf_cnt + R->end_lvl;      /* HW 가 보고한 버림 + 아직 FIFO 안 */
    memset(v, 0, sizeof *v);
    v->silent = a->sq.lost > explained ? a->sq.lost - explained : 0;
    if (a->ts_bad)                                 v->flags |= 1u << V_TS;
    if (v->silent)                                 v->flags |= 1u << V_SILENT_LOSS;
    if (a->sq.dup || a->sq.stale || a->sq.extra)   v->flags |= 1u << V_DUP;
    if (a->sq.reorder)                             v->flags |= 1u << V_REORDER;
    if (R->strand_exits || R->end_lvl)             v->flags |= 1u << V_STRANDED;
    if (R->ovf_cnt)                                v->flags |= 1u << V_OVERFLOW;
    if (a->late)                                   v->flags |= 1u << V_DEADLINE;
    v->primary = -1;
    for (int k = 0; k < V_KINDS; k++)
        if (v->flags & (1u << k)) { v->primary = k; break; }
}
```

캠페인 실제 출력:

```text
---- Part C: TMR0 fault campaign (budget 10000 ns) ----
[good] PASS
  recs=10000 entries=10000 multi=0 empty=0 lost=0(silent=0 ovf=0 left=0) dup=0 reorder=0 strand=0 late=0 p50<=499 p99<=7499 max=9014 ns
[drop_at_wrap] FAIL cause=SILENT_LOSS flags=SILENT_LOSS
  recs=9999 entries=9999 multi=0 empty=0 lost=1(silent=1 ovf=0 left=0) dup=0 reorder=0 strand=0 late=0 p50<=499 p99<=7499 max=9014 ns
  first: FAIL TMR0 kind=GAP rec#16 got=0x0001 exp=0x0000 entry#17
[pop_stall_every=3000] FAIL cause=DUPLICATE flags=DUPLICATE
  recs=10003 entries=10000 multi=3 empty=0 lost=0(silent=0 ovf=0 left=0) dup=3 reorder=0 strand=0 late=0 p50<=499 p99<=7499 max=9014 ns
  first: FAIL TMR0 kind=DUP rec#3000 got=0x0ba7 exp=0x0ba8 entry#3000
[swap_backlog (nominal)] PASS
  recs=10000 entries=10000 multi=0 empty=0 lost=0(silent=0 ovf=0 left=0) dup=0 reorder=0 strand=0 late=0 p50<=499 p99<=7499 max=9014 ns
[long_mask 140us] FAIL cause=OVERFLOW flags=OVERFLOW|DEADLINE
  recs=9998 entries=9995 multi=1 empty=0 lost=2(silent=0 ovf=2 left=0) dup=0 reorder=0 strand=0 late=5 p50<=499 p99<=7499 max=141740 ns
  first: FAIL TMR0 kind=OVERFLOW OVF_CNT=2 (accounted) first_gap rec#5004 got=0x137e exp=0x137c
[swap_backlog + long_mask] FAIL cause=REORDER flags=REORDER|OVERFLOW|DEADLINE
  recs=9998 entries=9995 multi=1 empty=0 lost=2(silent=0 ovf=2 left=0) dup=0 reorder=1 strand=0 late=5 p50<=499 p99<=7499 max=141740 ns
  first: FAIL TMR0 kind=REORDER rec#5003 got=0x1378 exp=0x137c entry#5001
[ts_at_pop] FAIL cause=TS_MISMATCH flags=TS_MISMATCH
  recs=10000 entries=10000 multi=0 empty=0 lost=0(silent=0 ovf=0 left=0) dup=0 reorder=0 strand=0 late=0 p50<=0 p99<=0 max=0 ns
  first: FAIL TMR0 kind=TS_MISMATCH rec#0 seq=0xfff0 trig_ts=0xfffea124 exp_ts=0xfffea070 diff=+180 ns
[isr clear-last] FAIL cause=STRANDED flags=STRANDED|DEADLINE
  recs=10000 entries=9895 multi=105 empty=0 lost=0(silent=0 ovf=0 left=0) dup=0 reorder=0 strand=105 late=105 p50<=499 p99<=25499 max=32683 ns
  first: FAIL TMR0 kind=STRANDED isr_exit entry#18 FIFO_LVL>0 but IRQ_RAW.TICK=0 head_seq=0x0002 policy=clear-last
```

| 시나리오 | 무엇이 잡나 | 서명 | 디버그 엔지니어의 다음 행동 |
|---|---|---|---|
| `drop_at_wrap` | seq GAP, OVF 없음 | `exp=0x0000 got=0x0001` — 정확히 wrap 지점 | seq 카운터 carry 와 push enable 로직, wrap 직전 값 preload 로 재현 |
| `pop_stall_every` | DUP | 같은 seq 2번, `multi=3`(그 진입만 pop 2회) | read 포인터 갱신 조건, RC 읽기의 클럭 게이팅 |
| `swap_backlog` 정상 부하 | **못 잡음** | — | 커버리지 구멍: backlog ≥ 2 를 만드는 스트레스가 없으면 FIFO 순서 로직은 한 번도 안 쓰인다 |
| `swap_backlog` + 140 µs 마스크 | REORDER | B,C,D,A 순서 — A(0x1378)가 맨 뒤 | FIFO 포인터 LSB, 깊이별로 backlog 를 강제하는 directed 테스트 |
| `long_mask` | OVERFLOW (accounted) + DEADLINE | `OVF_CNT=2` = lost 2, silent 0 | HW 는 정상. SW 쪽 140 µs critical section 을 찾는다(트레이스) |
| `ts_at_pop` | TS_MISMATCH | `diff=+180 ns`, latency 가 전부 0 | **계측기 고장**. 캡처 지점을 확인하기 전엔 다른 latency 숫자를 믿지 않는다 |
| clear-last | STRANDED + DEADLINE | 첫 위반 `entry#18 head_seq=0x0002`, 꼬리 ≈ 25 µs | ISR 의 clear 위치. HW 에 레벨형 인터럽트 제안 |

세 가지를 짚는다.

1. **유실의 출처를 가른다.** `long_mask` 의 유실 2 는 HW 가 `OVF_CNT` 로 **자백** 한 것이라 SILENT 가 0 이다 — 시스템 설계(FIFO 깊이 vs 최악 마스크 시간)의 문제지 실리콘 버그가 아니다. `drop_at_wrap` 의 유실 1 은 아무 카운터에도 안 남아 SILENT — 이건 RTL 버그다. 둘 다 "lost > 0" 이지만 담당 팀이 다르다.
2. **"너무 좋은" 결과를 의심한다.** `ts_at_pop` 은 p99 = 0 ns, max = 0 ns 다. 실리콘이 완벽해진 게 아니라 trigger 시각을 읽는 순간에 찍고 있어서 latency 가 사라진 것이다. 그래서 1순위가 TS 검사다: seq 로 계산한 기대 시각(스코어보드 `golden_ts`)과 다르면 계측기부터 고친다. 실칩에서는 같은 측정을 GPIO + 오실로스코프로 몇 개만 교차 확인한다.
3. **결함은 조건이 맞아야 보인다.** swap 버그는 nominal 10,000 이벤트에서 PASS 다. FIFO 에 2개 이상 쌓이는 순간이 없었기 때문이다. 검증 계획(SIV-B12)에는 "기능 × **부하 조건**" 이 같이 들어가야 한다. 동시 트래픽으로 이런 조건을 일부러 만드는 것이 SIV-M09 의 주제다.

clear-last 의 히스토그램(실제 출력 일부) — 정상 꼬리 뒤에 **P 만큼 떨어진 두 번째 봉우리** 가 생긴다:

```text
    8.5-  9.0 us |# 13
    9.0-  9.5 us |# 1
        ...
   25.0- 25.5 us |# 85
   25.5- 26.0 us |# 4
   ...
   31.5- 32.0 us |# 1
  >= 32.0 us      |# 1
```

ISR 본문 길이 sweep (같은 마스크 seed, 실제 출력):

```text
  work_us  policy       entries  multi  strand  late  max_ns
       14  clear-first    10000      0       0     0    9014
       14  clear-last     10000      0       0     0    9014
       16  clear-first    10000      0       0     0    9014
       16  clear-last      9998      2       2     2   25120
       18  clear-first    10000      0       0     0    9014
       18  clear-last      9895    105     105   105   32683
       20  clear-first    10000      0       0     0    9014
       20  clear-last      9633    367     367   367   33088
       22  clear-first    10000      0       0     0    9014
       22  clear-last      9233    767     767   767   33088
```

본문 14 µs 에서는 clear-last 도 **완전히 깨끗하다**(최대 latency 9 µs + 본문 14 µs < 25 µs 라 레이스 창이 안 열린다). 16 µs 에서 10,000 번 중 2번. 개발자 책상에서 통과하던 드라이버가 기능 추가로 ISR 이 2 µs 길어진 뒤 현장에서 "가끔 프레임 하나가 늦는다" 가 되는 전형적 경로다. 테스트가 `entries + multi == 10000` 을 확인하는 이유: 합쳐진 진입마다 레코드 하나가 더 들어 있으니 레코드 수는 보존된다 — **인터럽트 수로 세면 105개를 잃은 것처럼 보이지만 seq 로 세면 하나도 안 잃었다.**

### 실제 측정 — 호스트 wake-up latency (demo_wakeup.c)

시뮬레이션의 숫자는 모델이 정한 것이다. 같은 방법(절대 마감 → wrap-safe 차이 → 선형 bin → nearest-rank)을 진짜 시계에 적용하면 이렇다. `cyclictest`(rt-tests)의 축소판이다.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_wakeup.c -o /tmp/SIV-M03_wk && /tmp/SIV-M03_wk
```

```text
nanosleep wake-up latency, period 1 ms, n=2000, clock=CLOCK_UPTIME_RAW (res 42 ns)
min=9042 mean=335589 max=2772667 ns  p50<=339999 p90<=349999 p99<=499999 p99.9<=2772667 ns
  ...
  320-330 us |## 61
  330-340 us |######################################## 1312
  340-350 us |###### 197
  ...
  >= 640 us |# 6
```

(Apple M6, macOS, Apple clang 21.0.0, 부하 없음. 실행마다 달라지므로 CHECK 하지 않는다.) 범용 OS 의 타이머 깨우기는 **중앙값 약 340 µs** — 이 MCU 시뮬레이션의 120 ns 와 세 자릿수 이상 차이 난다. macOS 는 전력 절약을 위해 타이머에 여유(leeway)를 붙여 여러 깨우기를 묶는다(timer coalescing). 이 측정의 ~340 µs 중앙값도 그 여유로 보인다. 이 레슨의 "합쳐짐" 이 OS 수준에서 **의도적으로** 일어나는 예다. 계측기 교훈도 하나: 처음엔 `CLOCK_MONOTONIC` 으로 쟀는데 모든 값이 1000 의 배수였다. `clock_getres` 를 찍어 보니 macOS 의 `CLOCK_MONOTONIC` 해상도는 1,000 ns 였고, 그래서 ns 해상도인 `CLOCK_UPTIME_RAW` 로 바꿨다. **측정 전에 계측기의 해상도부터 확인한다.**

### 실칩에서는

- **bring-up**: CPU 인터럽트를 끈 채 폴링으로 FIFO/TICK/OVF 경로를 먼저 본다(SIV-B11). 그다음 인터럽트를 켜고 이 레슨의 로그를 RAM 링버퍼에 쌓았다가 JTAG(OpenOCD `dump_image`)으로 빼서 호스트에서 같은 분석을 돌린다 — ISR 안에서 printf 하면 그 자체가 latency 를 만든다.
- **Linux 가 올라간 뒤**: `/proc/interrupts` 카운트는 "진입 수" 일 뿐 이벤트 수가 아니다. 드라이버가 HW seq 를 읽어 gap 을 세는 debugfs 카운터를 두거나, ftrace `irq:irq_handler_entry` 이벤트로 진입 시각을 얻는다. 지연 상한은 `cyclictest` 히스토그램(`--histogram`)으로 PREEMPT_RT 커널과 비교한다.
- **PCIe MSI/MSI-X**: 인터럽트가 메모리 쓰기 메시지라서 "라인 레벨" 이 없다. 장치는 보통 이벤트를 링(완료 큐)에 쓰고 MSI 를 보내므로, 이 레슨의 seq 검사와 "큐에 남았는데 인터럽트는 없음" 불변식이 그대로 적용된다(NVMe 완료 큐의 phase tag 처럼 '새 항목인가' 를 항목 자체에 싣는 것이 seq 와 같은 발상이다).
- **pre-silicon**: 같은 불변식을 SVA 어서션(`fifo_lvl != 0 |-> tick_pending` 류)으로 RTL 에 걸고, 스코어보드가 seq 를 검사한다. 이 C 코드가 post-silicon 버전이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/SIV-M03_so && /tmp/SIV-M03_so      # 36/36 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/SIV-M03_san && /tmp/SIV-M03_san
# 36/36 passed, exit 0, "runtime error"/"AddressSanitizer" 0건, -O2 실행과 출력 동일 (확인함)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `uint32_t lat = entry - trig;` 를 그대로 히스토그램에 | in-flight 레코드에서 4.29e9 ns 가 나와 max 가 "4.3 초" 가 된다 | `s32_of()` 로 부호 해석, 음수면 in-flight 로 따로 |
| `if (entry < trig)` 로 wrap 판단 | wrap 구간에서 대소 비교는 의미가 없다 | 항상 차이(`entry - trig`)를 먼저 구하고 부호를 본다 |
| `int16_t d = s - next;` | `s - next` 는 int 로 승격(C-B02) — 16-bit 차이가 아니다 (`0x0000 - 0xFFFF = -65535`) | `(uint16_t)(s - next)` 로 자른 뒤 부호 해석 |
| ISR 진입 횟수 = 이벤트 수 | 합쳐짐(105개)·spurious 를 유실/중복으로 오진 | HW seq 또는 카운터 차이로 센다 |
| 평균 latency 로 PASS | 평균 663 ns 는 max 9 µs 를 숨긴다. 예산은 최악값에 대한 것 | max + p99/p99.9 + 예산 초과 개수 |
| 늦게 온 seq 를 "lost" 로 남겨 둠 | swap 1번이 lost 1 + 순서 이상 0 으로 보고됨 → 엉뚱한 팀에 버그 | 윈도에서 빈칸을 채우면 lost 를 되돌리고 REORDER |
| `win << (d + 1)` 를 d 검사 없이 | d ≥ 63 이면 64 이상 shift = UB (C11 6.5.7p3) | `d >= 63 ? 1 : (win << (d+1)) | 1` |
| ISR: drain → 본문 → clear | 본문 중 도착한 이벤트의 인터럽트를 지워 레코드가 갇힌다 | clear → drain (→ 끝에 LVL 재확인), 또는 HW 레벨형 인터럽트 |
| OVF 와 무관하게 "lost > 0 = HW 버그" | 부하로 인한 정당한 overflow 를 RTL 버그로 보고 | `silent = lost - OVF_CNT - 남은 레코드` |
| ISR 안에서 printf 로 latency 로그 | 계측이 지연을 만든다(UART 115200 에서 한 줄 수 ms) | RAM 링버퍼에 숫자만, 분석은 나중에 |
| seq/ts 를 0 부터 시작 | wrap 버그를 몇 시간~며칠 뒤에나 본다 | 카운터를 wrap 직전 값으로 preload |

## 8. 면접 Q&A

<details>
<summary>Q1. 새 칩에서 인터럽트 latency 를 어떻게 측정하겠습니까?</summary>

이벤트 쪽과 진입 쪽 타임스탬프가 둘 다 필요하다. 가장 좋은 건 IP 가 이벤트 순간 free-running 카운터를 캡처해 주는 것이고, ISR 첫 줄에서 같은 카운터를 읽어 뺀다(같은 시간축, wrap-safe 뺄셈). 그런 기능이 없으면 이벤트 핀과 ISR 첫 줄의 GPIO set 을 로직 분석기로 잰다. 이건 외부 독립 계측이라 HW 타임스탬프 방식을 교차 검증하는 데도 쓴다. 결과는 이벤트 수만~수백만 개의 히스토그램으로 내고 max, p99.9, 예산 초과 수를 보고한다. 부하 조건(다른 인터럽트, DMA, 캐시 miss, 저전력 상태에서 깨어나기)을 바꿔 가며 반복한다.
꼬리질문 "Cortex-M4 의 12 cycle 이면 끝 아닌가?" → 12 cycle 은 zero-wait 메모리에서 코어가 스택에 쌓는 최소 비용이다. 실제로는 flash wait state, 버스 경합, 더 높은 우선순위 ISR, critical section(PRIMASK/BASEPRI), FPU 를 쓰는 코드라면 확장 스택 프레임(lazy stacking 설정에 따라 다름) 같은 요인이 더해진다. 측정이 필요한 이유다. 반대로 tail-chaining 이면 진입이 더 짧아진다(이 레슨의 min = 119 ns).

</details>

<details>
<summary>Q2. 인터럽트가 몇 번 들어왔는지 세서 유실을 검증하면 안 되나요?</summary>

안 된다. 진입 수 ≠ 이벤트 수다. sticky 상태 비트 하나에 이벤트 두 개가 들어오면 진입은 한 번이다(합쳐짐). NIC 의 interrupt coalescing 처럼 일부러 그렇게 설계하기도 한다. 반대로 posted write 때문에 clear 가 늦게 반영되면 진입은 두 번인데 이벤트는 하나다(spurious, SIV-B11 연습 4). 그래서 이벤트에 붙은 번호(HW seq, 이벤트 카운터)로 센다. 이 레슨의 clear-last 는 진입 9,895 번, 이벤트 10,000 개, 유실 0 이었다.
꼬리질문 "seq 가 없는 IP 라면?" → 이벤트 카운터 레지스터 차이(추가 연습 4)나, 스코어보드가 아는 기대 이벤트 수(타이머 주기 × 시간)와 비교한다. 그것도 없으면 HW 에 디버그 카운터를 요청하는 게 검증 엔지니어의 일이다.

</details>

<details>
<summary>Q3. "ISR 에서 상태 비트를 언제 clear 하느냐" 가 왜 중요합니까?</summary>

sticky 이벤트 비트는 "새 이벤트가 있었다" 를 한 비트로만 기억한다. drain 을 다 하고 나서 clear 하면 drain 과 clear 사이에 도착한 이벤트의 표시까지 지운다. 그러면 데이터는 FIFO 에 있는데 인터럽트는 없는 상태(stranded)가 된다. 다음 이벤트가 와야 같이 처리되고, 마지막 이벤트였다면 영원히 처리되지 않는다. 올바른 순서는 clear 를 먼저 하고 그다음 비울 때까지 drain 하는 것이다. clear 이후에 온 이벤트는 다시 비트를 세우므로 잃지 않는다. 그 대가로 "진입했는데 이미 비어 있는" 빈 진입이 가끔 생기고, ISR 은 이걸 견뎌야 한다.
꼬리질문 "HW 쪽에서 막을 수는?" → 인터럽트를 `FIFO_LVL != 0` 의 **레벨** 로 만들면 SW clear 가 필요 없고 이 레이스 자체가 없다. 이벤트 비트가 꼭 필요하면 "clear 시점에 LVL > 0 이면 즉시 다시 set" 하는 HW 규칙을 둔다. 검증에서는 "레코드가 있으면 pending" 불변식을 어서션/테스트로 건다.

</details>

<details>
<summary>Q4. 16-bit 시퀀스 번호의 wrap 은 어떻게 처리하고, 비트 폭은 어떻게 정합니까?</summary>

serial 산술이다. `d = (int16)(uint16)(s − expected)` 의 부호로 앞/뒤를 판단하고(RFC 1982), 늦게 온 것은 비트맵 윈도로 중복과 뒤바뀜을 구분한다(RTP RFC 3550 부록 A, IPsec RFC 4303 §3.4.3 과 같은 구조). 반 바퀴(2^15)를 넘는 공백이 생기면 앞뒤 판단이 불가능하다. 그래서 폭은 "최대 공백 시간 × 이벤트율 < 2^(N−1)" 로 정한다. 40 kHz 에 16-bit 면 0.82 초다. 그보다 긴 정지(디버거 halt, 저전력)가 있을 수 있으면 32-bit 으로 하거나 재동기 규칙을 둔다.
꼬리질문 "wrap 버그는 어떻게 빨리 재현?" → 카운터를 wrap 직전으로 preload 한다(이 레슨의 seq0 = 0xFFF0, ts0 = 2^32 − 100000). 32-bit 1 ns 타임스탬프는 4.29 초마다, 16-bit seq 는 40 kHz 에서 1.6 초마다 wrap 한다.

</details>

<details>
<summary>Q5. 10,000 번 돌려 예산 초과가 0 번이면 무엇을 주장할 수 있나요?</summary>

"초과 확률이 약 3/10,000 = 3e-4 보다 작다(95% 신뢰)" 까지다(rule of three: 1 − (1−p)^n = 0.95 → p ≈ 3/n). 요구사항이 "1e-6 이하" 라면 오류 0 개로 약 300만 번(95%), 99% 신뢰면 약 460만 번(추가 연습 3: 4,605,168)을 돌려야 한다. 그래서 latency 검증은 짧은 기능 테스트가 아니라 밤새 도는 soak 테스트가 되고, 시간을 줄이려면 부하를 일부러 키운 스트레스 조건에서 돌린다. SerDes BER 신뢰구간과 같은 계산이다(SIV-M06).
꼬리질문 "p99.9 를 1,000 개로 재면?" → nearest-rank 로 1,000 개 중 999 번째 값, 즉 표본 하나가 p99.9 를 결정한다. 신뢰할 만한 꼬리 분위수에는 그 역수의 수십 배 표본이 필요하다.

</details>

<details>
<summary>Q6. seq 검사에서 유실이 나왔습니다. 실리콘 버그인지 소프트웨어/부하 문제인지 어떻게 가르나요?</summary>

HW 가 유실을 **보고했는지** 를 본다. FIFO overflow 카운터(OVF_CNT)나 drop 플래그로 설명되는 유실은 HW 가 스펙대로 동작한 것이다. 이때는 "왜 그렇게 오래 못 비웠나" 가 질문이고, 답은 보통 SW critical section, 우선순위, FIFO 깊이 사양이다. 아무 카운터에도 안 남은 유실은 RTL 버그 후보다(이 레슨의 `drop_at_wrap`, silent=1). 추가로 갇힌 레코드(종료 시 LVL > 0, TICK = 0)는 ISR 순서 버그다. 그리고 첫 유실의 **위치** 가 패턴을 말해 준다. seq 0x0000 에서만 사라지면 wrap 로직이고, backlog 가 있을 때만 사라지면 FIFO 포인터다.
꼬리질문 "OVF 카운터를 믿을 수 있나?" → 그것도 검증 대상이다. 일부러 CPU 를 오래 막아 정확히 k 개가 넘치게 만들고 OVF_CNT == k 인지 본다(이 레슨의 long_mask: 6개 도착, 4개 저장, OVF_CNT=2).

</details>

<details>
<summary>Q7. 정상 회귀에서는 안 나오고 현장에서만 나오는 순서 뒤바뀜 버그가 있었습니다. 검증 계획에서 무엇이 빠졌을까요?</summary>

FIFO 순서 로직은 FIFO 에 2개 이상 쌓여야 쓰인다. 정상 부하에서 ISR 이 매번 제때 비우면 그 로직은 한 번도 실행되지 않는다. 이 레슨의 `swap_backlog` 는 nominal 10,000 이벤트에서 PASS 였고, 140 µs 마스크를 넣었을 때만 REORDER 로 잡혔다. 그래서 계획에는 기능과 함께 **점유 상태 커버리지**(FIFO 레벨 0/1/2/…/full/overflow 각각 몇 번 거쳤나)가 들어가야 한다. 이건 functional coverage 의 coverpoint 가 된다(SIV-M13). backlog 는 ISR 을 일부러 막는 directed 테스트와 동시 트래픽 스트레스(SIV-M09)로 만든다.

</details>

<details>
<summary>Q8. latency 결과가 너무 좋게 나왔습니다(전부 0 ns). 무엇을 의심합니까?</summary>

계측기다. trigger 타임스탬프가 이벤트 순간이 아니라 읽는 순간에 캡처되거나(이 레슨의 `ts_at_pop`: diff = +180 ns, 전부 0), 두 타임스탬프가 다른 클럭 도메인이거나, 해상도가 거칠다(macOS CLOCK_MONOTONIC = 1 µs). 스코어보드가 seq 로 계산한 기대 시각과 캡처 시각을 비교하는 검사를 1순위로 두고, 몇 개 샘플은 GPIO + 오실로스코프 같은 독립 계측으로 교차 확인한다.

</details>

## 9. 추가 연습

### 연습 1 — log2 히스토그램

bpftrace `hist()` 처럼 bucket 0 = {0}, bucket b = [2^(b−1), 2^b − 1] 인 log2 히스토그램과 분위수 상한을 만들고, 본문과 같은 무작위 2,000 개에서 정확한 값 x 에 대해 `x ≤ q ≤ 2x + 1` 인지 확인하라.

<details>
<summary>답 보기</summary>

bucket 은 "최상위 1 비트의 위치 + 1" 이다(`while (v) { b++; v >>= 1; }`). 상한은 `2^b − 1` 이다(b = 32 는 UINT32_MAX — `1u << 32` 는 UB 라 따로 처리). 실제 출력: p50 정확 593 → 상한 1023, p99 정확 24235 → 상한 32767. 비율 오차는 2배 미만이지만 절대 오차는 수 µs 다. 33개 bucket 으로 0 ns ~ 4.29 초를 다 덮으니 범위를 모를 때 좋고, 예산 경계(10 µs)를 정밀하게 판정할 때는 선형 bin 이 낫다.

</details>

### 연습 2 — N-bit serial 차이

`serial_diff(a, b, bits)` 를 1..31 비트에 대해 일반화하라. 12-bit 에서 `0x000 − 0xFFF = +1`, `0x800 − 0x000` 은?

<details>
<summary>답 보기</summary>

`u = (a − b) & mask`, `u < half ? u : u − 2^bits`. `0x800 − 0x000` 은 정확히 반 바퀴라 −2048 이 된다. 앞인지 뒤인지 **정의상 모호** 하고, 이 구현은 음수(뒤)로 본다. RFC 1982 도 이 경우를 undefined 로 둔다. 4-bit 에서 `3 − 14 = +5`, `14 − 3 = −5`. 실칩의 CAN/CAN-FD 타임스탬프, USB 프레임 번호(11-bit) 같은 좁은 카운터에 그대로 쓴다.

</details>

### 연습 3 — 꼬리 사건을 보려면 몇 번?

확률 p 인 사건을 신뢰도 CL 로 한 번 이상 보려면 몇 번이 필요한가? 반대로 n 번 동안 0 번이면 p 의 상한은?

<details>
<summary>답 보기</summary>

1 − (1−p)^n ≥ CL 에서 n ≥ ln(1−CL) / ln(1−p) 다. p = 1e-4, 95% → **29,956**. p = 1e-6, 99% → 4,605,168 (실제 출력). 0 번 관측의 상한은 p = 1 − (1−CL)^(1/n) 이고, n = 10,000, 95% → **2.995e-4** ≈ 3/n(rule of three). 수치 안정성을 위해 `log1p(−p)`, `expm1()` 을 쓴다(p 가 작으면 `log(1−p)` 는 자릿수를 잃는다). SIV-M06 의 BER 계산 N ≥ −ln(1−CL)/BER 은 이 식에서 p 가 작을 때의 근사다.

</details>

### 연습 4 — FIFO 없는 카운터 전용 IP

IP 에 FIFO 가 없고 16-bit 이벤트 카운터 `EVT_CNT` 만 있다. ISR 진입마다 읽은 값 배열로 정상(차이 1), 합쳐짐(차이 ≥ 2, 합쳐진 이벤트 수), spurious(차이 0)를 집계하라. 시작 0xFFFE, 읽은 값 `FFFF 0001 0001 0002 0005`.

<details>
<summary>답 보기</summary>

차이 `1, 2, 0, 1, 3`: 정상 2, 합쳐진 진입 2(합쳐진 이벤트 1 + 2 = 3), spurious 1, 총 이벤트 7 = `(uint16)(0x0005 − 0xFFFE)`. 카운터 방식은 **몇 개** 였는지는 잃지 않지만 합쳐진 이벤트 각각의 **시각** 은 잃는다(마지막 것만 안다). 그래서 latency 히스토그램은 "진입 기준" 으로만 만들 수 있다. ISR 은 clear 를 먼저 하고 그다음 카운터를 읽어야 한다. 그래야 읽은 뒤 도착한 이벤트가 새 인터럽트를 만든다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/SIV-M03_pr && /tmp/SIV-M03_pr   # 14/14 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o /tmp/SIV-M03_prsan && /tmp/SIV-M03_prsan   # 14/14 passed, 보고 없음
# <math.h> 사용: macOS 는 -lm 불필요, Linux 에서는 끝에 -lm
```

## 10. 핵심 정리

- latency = 이벤트 쪽 타임스탬프(HW 캡처) − 진입 쪽 타임스탬프, **같은 시간축**. unsigned 로 빼고 부호는 따로 해석한다(wrap, in-flight 음수).
- 히스토그램 + max·p99·p99.9·예산 초과 수로 판정한다. 평균은 꼬리를 숨긴다. 선형 bin 은 절대 오차, log2 는 비율 오차.
- 진입 수 ≠ 이벤트 수. seq(serial 산술 + 비트맵 윈도)로 GAP/DUP/REORDER/STALE 을 센다. 늦게 와서 빈칸을 채우면 유실이 아니라 뒤바뀜이다.
- sticky 이벤트 비트는 **clear 먼저, 그다음 drain**. 늦은 clear 는 합쳐짐 + stranded 레코드(latency ≈ P)를 만든다. "레코드가 있으면 pending" 불변식으로 잡는다.
- 유실 = OVF 로 설명되는 것(부하·설계) + 남은 레코드(ISR 순서) + 조용한 유실(RTL 버그). 담당 팀이 다르다.
- 결함은 조건이 맞아야 보인다: backlog 가 없으면 FIFO 순서 버그는 PASS. wrap 버그는 카운터 preload 로 당겨 본다.
- 계측기부터 검증한다: 기대 타임스탬프 대조, 시계 해상도(`clock_getres`), 독립 계측(GPIO + 스코프).
- 0 / n 번 관측 → p < 3/n (95%). 1e-6 을 주장하려면 수백만 이벤트가 필요하다.

## 참고자료

- [Arm Cortex-M4 Processor Technical Reference Manual (DDI 0439)](https://developer.arm.com/documentation/ddi0439/latest/) — 예외 진입 12 cycle, tail-chaining, late-arrival
- Joseph Yiu, *The Definitive Guide to Arm Cortex-M3 and Cortex-M4 Processors*, 3rd ed. — 인터럽트 latency, PRIMASK/BASEPRI, DWT CYCCNT
- [RFC 1982 — Serial Number Arithmetic](https://www.rfc-editor.org/rfc/rfc1982)
- [RFC 3550 — RTP, Appendix A.1/A.3](https://www.rfc-editor.org/rfc/rfc3550) — seq wrap 처리, 기대·유실 패킷 수 계산
- [RFC 4303 — IPsec ESP, §3.4.3 Sequence Number Verification](https://www.rfc-editor.org/rfc/rfc4303) — anti-replay 슬라이딩 윈도
- [rt-tests / cyclictest](https://wiki.linuxfoundation.org/realtime/documentation/howto/tools/cyclictest/start) — wake-up latency 히스토그램 측정
- Linux kernel `Documentation/trace/events.rst` — `irq:irq_handler_entry` / `irq_handler_exit` 트레이스 이벤트
- Arm GIC Architecture Specification (GICv3/v4) — level/edge 설정(`GICD_ICFGR`)
- 관련 레슨: SIV-B02 (mock MMIO), SIV-B09 (wrap-safe 시간 차이, fake clock), SIV-B11 (trigger-check-clear, W1C, posted write spurious), SIV-B12 (테스트 계획), SIV-M06 (BER 신뢰구간), SIV-M09 (동시성 스트레스), SIV-M13 (functional coverage), C-B02 (정수 승격)
