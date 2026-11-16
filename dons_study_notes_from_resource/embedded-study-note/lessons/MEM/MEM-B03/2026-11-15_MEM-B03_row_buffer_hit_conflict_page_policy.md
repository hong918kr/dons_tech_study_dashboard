---
id: MEM-B03
time: 60분
updated: 2026-11-15
---
# MEM-B03 Row hit/miss/conflict와 페이지 정책

## 학습 목표

- 뱅크의 row buffer 상태(열림/닫힘, 어느 row)로 접근을 **hit · empty · conflict** 세 가지로 분류하고, 각각의 지연을 DDR4-3200AA 사이클과 ns 로 바로 말한다 (26 / 48 / 70 cycle = 16.25 / 30.0 / 43.75 ns).
- open-page 와 close-page 정책을 여러 뱅크 시뮬레이터로 돌려, 순차 · stride · 랜덤 · ping-pong 워크로드별 평균 지연을 비교하고 어느 쪽이 유리한지 표로 정리한다.
- 손익분기 hit 비율 **h\* = tRP / (tRP + tRCD)** 를 식으로 유도하고 시뮬레이터로 확인한다. empty 가 섞이면 "H·tRCD > C·tRP 이면 open" 이라는 부등식으로 말한다.
- tRAS · tRP · tRC 대기를 넣은 "시각 추적" 모드에서는 결론이 요청 간격(gap)에 따라 바뀐다는 것을 숫자로 보인다 (같은 뱅크 연속 접근의 tRC 바닥).
- 메모리 컨트롤러 PMU 카운터(CAS · ACT · PRE_MISS)에서 hit/empty/conflict 를 역산해 "이 SoC 에는 어떤 페이지 정책" 을 숫자로 추천한다.

## 1. 문제

"메모리 지연이 왜 40 ns 였다가 50 ns 였다가 하나요?" — DDR bring-up, SoC 성능 검증, 서버 BIOS 튜닝에서 반드시 나오는 질문이고, 답의 절반은 **row buffer** 다.

DRAM 뱅크(bank)는 한 번에 한 row(DDR4 x8 기준 칩당 1 KB, 64비트 채널 8칩 합쳐 8 KB)만 sense amplifier 에 올려 둘 수 있다. 이것이 row buffer 다. 요청이 오면 세 경우 중 하나다.

- **row hit**: 원하는 row 가 이미 열려 있다 → RD 만 (CL).
- **row empty** (closed): 뱅크가 precharge 되어 닫혀 있다 → ACT(tRCD) 후 RD.
- **row conflict** (miss): 다른 row 가 열려 있다 → PRE(tRP) → ACT(tRCD) → RD. 게다가 앞 row 의 ACT 뒤 tRAS 가 안 지났으면 PRE 도 기다려야 한다.

메모리 컨트롤러는 접근이 끝난 row 를 **열어 둘지(open-page)** **바로 닫을지(close-page, auto-precharge)** 를 정한다. 열어 두면 다음 접근이 hit 일 때 이득, conflict 일 때 손해. 닫으면 늘 empty 지연이지만 precharge 를 숨길 시간이 있어야 한다. Don 이 SSD 컨트롤러에서 본 DRAM(FTL 매핑 테이블 캐시)도, Rivian 같은 SoC 의 LPDDR 컨트롤러도 이 선택을 레지스터 하나(페이지 정책 / 타임아웃)로 노출한다. 이 레슨은 그 선택을 숫자로 하는 시뮬레이터를 만든다.

이 레슨의 정의 (JESD79-4 / 데이터시트 대표값 — 실제 부품은 데이터시트 확인): DDR4-3200AA (22-22-22), tCK = 0.625 ns, CL = 22, tRCD = 22, tRP = 22, tRAS = 52 (32 ns), tRC = 74 (45.75 ns), tRTP = 12 (7.5 ns), BL8 = 4 tCK. ns → cycle 은 정수 ps 로 ceil (MEM-B02 와 같은 규칙). 지연은 "명령 시작 → 마지막 데이터 beat" 로 잰다.

주소 매핑은 MEM-B01 모델 축약판이다: 채널 1 · 랭크 1, 뱅크 16개(4 BG × 4 bank 를 flat index 로), row 가 맨 위에 오는 RoBaCo 배치.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">이 레슨의 주소 매핑 (MEM-B01 축약판, 8 GB = 16 bank × 64 K row × 8 KB)</text>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="40" y="52">32</text><text x="352" y="52">17</text><text x="372" y="52">16</text><text x="432" y="52">13</text>
    <text x="452" y="52">12</text><text x="572" y="52">6</text><text x="592" y="52">5</text><text x="632" y="52">3</text><text x="652" y="52">2</text><text x="692" y="52">0</text>
  </g>
  <rect x="40" y="60" width="320" height="40" class="f-mut" stroke="currentColor"/>
  <rect x="360" y="60" width="80" height="40" class="f-acc" stroke="currentColor"/>
  <rect x="440" y="60" width="140" height="40" class="f-ok" stroke="currentColor"/>
  <rect x="580" y="60" width="60" height="40" class="f-ok" stroke="currentColor"/>
  <rect x="640" y="60" width="60" height="40" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="150" y="85">row (16 bit, 64 K)</text>
    <text x="368" y="85">bank (4)</text>
    <text x="450" y="85">col[9:3] 라인</text>
    <text x="590" y="85">col[2:0]</text>
    <text x="648" y="85">byte</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="40" y="125">pa[32:17] → row: 같은 뱅크에서 row 가 다르면 conflict</text>
    <text x="40" y="143">pa[16:13] → bank 0..15: 8 KB 마다 다음 뱅크 (row 하나 = 8 KB = 128 개의 64 B 라인)</text>
    <text x="40" y="161">pa[12:3] → column (8 B 단위 1024개), pa[12:6] = row 안 64 B 라인 번호 (BL8 한 번 = 64 B)</text>
    <text x="40" y="179">예: 0x1_2345_6740 → row 0x91A2, bank 0xB, col 0x0E8 (라인 29)</text>
  </g>
</svg>
```

### Part A — 분류기와 두 가지 지연 모델

- `rb_classify(bank, row)`: 닫힘 → EMPTY, 같은 row → HIT, 다른 row → CONFLICT.
- `class_latency(cls, t)`: hit = CL + 4, empty = tRCD + CL + 4, conflict = tRP + tRCD + CL + 4 (cycle).
- `access_simple(...)` — **LAT_SIMPLE 모드**: 상태만 보고 고정 지연. **tRAS 대기와 "precharge 가 숨겨졌나" 는 무시한다** (이 모드의 명시적 단순화). open 은 row 를 열어 두고, close 는 접근 뒤 바로 닫는다.
- `access_timed(...)` — **LAT_TIMED 모드**: 뱅크마다 마지막 ACT · RD 시각과 "다음 ACT 가능 시각" 을 들고, tRCD · tRAS(ACT→PRE) · tRTP(RD→PRE) · tRP(PRE→ACT) · tRC(ACT→ACT) 를 모두 지킨다. close 는 RDA(read with auto-precharge) 로 가장 이른 합법 시각에 PRE 한다.

### Part B — 워크로드와 정책 비교, 손익분기

- `gen_trace(kind, param, n, seed, out, cap)`: 순차(64 B), stride(256 B, 4 KB, 8 KB = row 크기, 128 KB = 뱅크 하나에 몰리는 bank camping), 랜덤(xorshift64, 8 GB), 한 뱅크의 두 row ping-pong(최악), hit 비율을 정확히 정하는 HITMIX(손익분기 확인용)를 결정적으로 만든다.
- 주어진 `sim_run` 이 요청을 **순서대로 하나씩** 처리한다 (다음 요청 = 앞 요청 데이터 완료 + gap cycle; 의존 load 체인 같은 closed loop). 스케줄링(FR-FCFS)과 파이프라이닝은 MEM-M01 몫이다.
- 정책별 hit/empty/conflict 수, 평균 지연(cycle, ns)을 표로 출력하고, `break_even()` 으로 h\* 를 기약분수로 구한 뒤 HITMIX 스윕으로 확인한다.

### Part C — PMU 카운터로 "이 SoC 는 어떤 정책?"

- `pmu_to_mix(cas, act, pre_miss, ...)`: open-page 로 돌던 컨트롤러의 카운터에서 hit = CAS − ACT, conflict = PRE_MISS, empty = ACT − PRE_MISS 를 역산한다. 모순(ACT > CAS, PRE_MISS > ACT) 이면 −1.
- `pmu_recommend(...)`: open 총 지연 vs close 총 지연을 비교해 REC_OPEN / REC_CLOSE / REC_TIE 와 절약 cycle 을 돌려준다. 시뮬레이터가 센 클래스를 카운터로 넣으면 같은 결론이 나와야 한다.

## 2. Problem (English)

You are tuning the page policy of a DDR4-3200 memory controller (22-22-22, tRAS 52, tRC 74, tRTP 12 cycles, BL8 = 4 cycles). One rank, 16 banks, row = 8 KB, mapping row = pa[32:17], bank = pa[16:13], col = pa[12:3]. Reads only, serviced in order one at a time.

```c
typedef enum { RB_HIT, RB_EMPTY, RB_CONFLICT } rb_class;
typedef enum { POL_OPEN, POL_CLOSE } policy_t;
rb_class rb_classify(const bank_state *b, uint32_t row);
uint32_t class_latency(rb_class c, const dram_timing *t);                 /* cycles to last data beat */
uint32_t access_simple(bank_state *b, uint32_t row, policy_t p, const dram_timing *t, rb_class *cls);
int64_t  access_timed (bank_state *b, uint32_t row, int64_t now, policy_t p, const dram_timing *t, rb_class *cls);
                                                                          /* returns cycle of last data beat */
size_t   gen_trace(workload_t w, uint32_t param, size_t n, uint64_t seed, uint64_t *out, size_t cap);
void     break_even(const dram_timing *t, uint32_t *num, uint32_t *den);  /* reduced fraction */
int      pmu_to_mix(uint64_t cas, uint64_t act, uint64_t pre_miss, uint64_t *hit, uint64_t *empty, uint64_t *conf);
rec_t    pmu_recommend(uint64_t cas, uint64_t act, uint64_t pre_miss, const dram_timing *t, int64_t *saved);
```

Constraints: no heap; deterministic (fixed-seed xorshift64); the self-test runs well under 2 s. In the timed model a PRE may not issue before ACT + tRAS or RD + tRTP, an ACT not before PRE + tRP or the previous ACT + tRC.

Examples:

```text
class_latency: hit 26, empty 48, conflict 70 cycles  (16.25 / 30.00 / 43.75 ns)
timed, open page, same bank: row 1 @0 -> done 48;  row 2 @48 -> done 122 (latency 74: waits 4 cycles for tRAS)
timed, close page, same bank: row 1 @0 -> done 48 (PRE @52, ready @74);  row 1 @48 -> latency 74
sequential 64 B lines, n = 4096: open = 4064 hit / 16 empty / 16 conflict, mean 26.26 cycles; close = 48.00
break_even(DDR4-3200AA) = 1/2
pmu_recommend(cas=1e6, act=3e5, pre_miss=2.5e5) -> OPEN, saves 9 900 000 cycles
```

Follow-ups: Derive the break-even hit rate. Why can close-page be no better than open-page for back-to-back accesses to one bank? What would you read from the memory controller's PMU to choose a policy, and what can't you see if it already runs close-page?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여덟 군데다 (Part A 4개, Part B 2개, Part C 2개). 타이밍 표, 매핑(`map_decode` / `map_encode`), 뱅크 상태, 시뮬레이터 루프 `sim_run`, 보고서 출력, 하네스는 주어진다. 스텁은 안전한 틀린 값을 돌려준다 (`access_timed` 는 `now` 를 그대로 → 지연 0, 루프는 끝난다). 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:241  rb_classify(&b, 5) == RB_HIT
FAIL starter.c:242  rb_classify(&b, 6) == RB_CONFLICT
FAIL starter.c:243  class_latency(RB_HIT, &T) == 26 && class_latency(RB_EMPTY, &T) == 48 && class_latency(RB_CONFLICT, &T) == 70
…
FAIL starter.c:392  r == REC_OPEN && saved == cl.total - o.total && saved == 16 * 22
…
7/52 passed
```

(exit 1. 통과한 7개는 주어진 타이밍 · 매핑 테스트 3개와, 스텁 값이 우연히 맞은 4개다 — 닫힌 뱅크 → EMPTY, 모든 지연이 0 이라 "close − open = H·tRCD − C·tRP" 와 "h = 1/2 에서 합 같음" 이 0 = 0 으로 성립, H = C 인 카운터 → TIE.)

## 4. 힌트

<details><summary>힌트 1 — 분류는 open-page 관점 하나면 된다</summary>

분류는 "지금 뱅크에 무엇이 열려 있나" 만 본다. close-page 에서는 접근이 끝날 때마다 `open_row = NO_ROW` 로 만들기 때문에 다음 접근은 자동으로 EMPTY 가 된다. 정책은 분류가 아니라 **접근 뒤 상태 갱신**에서 갈린다.

</details>

<details><summary>힌트 2 — TIMED conflict: PRE 의 가장 이른 시각</summary>

`pre = max(now, act_at + tRAS, rd_at + tRTP)`, `act = max(pre + tRP, act_at + tRC)`, `rd = act + tRCD`, 반환 `rd + CL + BURST`. ACT 를 하면 `act_at` 을, RD 를 하면 `rd_at` 을 갱신한다. hit 는 `rd = max(now, act_at + tRCD)`.

</details>

<details><summary>힌트 3 — TIMED close: RDA 와 ready_at</summary>

close-page 는 RD 와 함께 auto-precharge 를 건다. PRE 는 `max(rd + tRTP, act_at + tRAS)` 에 나가고, 뱅크는 `ready_at = PRE + tRP` 부터 다음 ACT 를 받는다. 다음 요청(EMPTY)의 ACT = `max(now, ready_at, act_at + tRC)`. 요청이 그 전에 오면 기다린다 — 이것이 "precharge 가 숨겨지지 않은" 경우다.

</details>

<details><summary>힌트 4 — HITMIX 의 정확한 hit 수</summary>

i ≥ 1 에서 `floor(i·p/100) > floor((i−1)·p/100)` 이면 hit(같은 row, 다음 라인), 아니면 row + 1. 이 Bresenham 식 판정은 i = 1..m 에서 hit 를 정확히 `floor(m·p/100)` 개 만든다. 그래서 n = 4097 이면 첫 EMPTY 1 개 + 나머지 4096 개 중 hit 가 정확히 p % 다.

</details>

<details><summary>힌트 5 — 카운터에서 클래스 역산</summary>

open-page 에서 ACT 는 empty 와 conflict 에서만 나간다 → ACT = E + C. conflict 마다 PRE 가 하나 → PRE_MISS = C. CAS 는 모든 접근 → CAS = H + E + C. 세 식을 풀면 된다. 음수가 되는 입력은 카운터가 모순이다 (다른 이벤트를 읽었거나, 리프레시 · 페이지 타이머 PRE 를 섞어 셌거나).

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **세 클래스와 비용**: "hit 는 CL, empty 는 tRCD + CL, conflict 는 tRP + tRCD + CL. DDR4-3200AA 에서 26 / 48 / 70 cycle, 마지막 beat 까지 16 / 30 / 44 ns."
2. **정책의 거래**: "open 은 hit 를 사고 conflict 위험을 진다. close 는 늘 empty 지만 precharge 를 숨길 idle 이 필요하다."
3. **손익분기**: "empty 가 없다면 h\* = tRP / (tRP + tRCD) = 1/2. 일반적으로 H·tRCD > C·tRP 이면 open."
4. **시간 모델의 단서**: "같은 뱅크에 연달아 다른 row 면 정책과 무관하게 tRC(74 cycle)마다 하나 — 해결은 정책이 아니라 주소 매핑/뱅크 병렬성."
5. **측정으로 고른다**: "PMU 의 CAS / ACT / PRE_MISS 로 hit 비율을 보고, close 로 돌던 시스템이면 shadow row 로 추정한다."

### Part A — 규칙과 지연 어휘

| 클래스 | 조건 | 명령 | 지연 (cycle) | ns |
|---|---|---|---|---|
| hit | 같은 row 열림 | RD | CL + 4 = 26 | 16.25 |
| empty | 닫힘 | ACT → RD | tRCD + CL + 4 = 48 | 30.00 |
| conflict | 다른 row 열림 | PRE → ACT → RD | tRP + tRCD + CL + 4 = 70 | 43.75 |
| conflict (tRAS 대기) | 앞 ACT 뒤 52 cycle 안 지남 | 대기 → PRE → ACT → RD | 70 + 대기 (G = 0 이면 74) | 46.25 |

ns 는 cycle × 625 ps. "+4" 는 BL8 버스트 8 beat = 4 tCK 이다 (64 비트 채널 × 8 beat = 64 B = 캐시라인 하나, MEM-B04).

**왜 tRAS 가 있나.** ACT 하면 셀의 전하가 bitline 으로 나와 sense amp 가 증폭하고, 그 값을 다시 셀에 써 넣어야(restore) 한다. 읽기는 셀을 파괴하기 때문이다. tRAS 는 restore 가 끝날 때까지 PRE 를 막는 최소 시간이다. 그래서 "row 를 열자마자 닫는" 패턴(conflict 연속, close-page 연속)은 tRAS + tRP = tRC 가 바닥이 된다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">세 클래스의 명령 타임라인 (1 cycle = 7 px, 실제 비율, DDR4-3200AA)</text>
  <g fill="currentColor" font-size="12"><text x="176" y="44">0</text><text x="326" y="44">22</text><text x="480" y="44">44</text><text x="634" y="44">66</text><text x="690" y="44">74</text></g>
  <g stroke="currentColor" stroke-dasharray="3 3"><line x1="180" y1="50" x2="180" y2="250"/><line x1="334" y1="50" x2="334" y2="250"/><line x1="488" y1="50" x2="488" y2="250"/><line x1="642" y1="50" x2="642" y2="250"/><line x1="698" y1="50" x2="698" y2="250"/></g>
  <text x="10" y="78" fill="currentColor">hit (26)</text>
  <rect x="180" y="62" width="154" height="24" class="f-ok" stroke="currentColor"/><text x="230" y="79" fill="currentColor">CL 22</text>
  <rect x="334" y="62" width="28" height="24" class="f-acc" stroke="currentColor"/><text x="368" y="79" fill="currentColor" font-size="12">data</text>
  <text x="10" y="128" fill="currentColor">empty (48)</text>
  <rect x="180" y="112" width="154" height="24" class="f-mut" stroke="currentColor"/><text x="225" y="129" fill="currentColor">tRCD 22</text>
  <rect x="334" y="112" width="154" height="24" class="f-ok" stroke="currentColor"/><text x="384" y="129" fill="currentColor">CL 22</text>
  <rect x="488" y="112" width="28" height="24" class="f-acc" stroke="currentColor"/>
  <text x="10" y="178" fill="currentColor">conflict (70)</text>
  <rect x="180" y="162" width="154" height="24" class="f-warn" stroke="currentColor"/><text x="230" y="179" fill="currentColor">tRP 22</text>
  <rect x="334" y="162" width="154" height="24" class="f-mut" stroke="currentColor"/><text x="379" y="179" fill="currentColor">tRCD 22</text>
  <rect x="488" y="162" width="154" height="24" class="f-ok" stroke="currentColor"/><text x="538" y="179" fill="currentColor">CL 22</text>
  <rect x="642" y="162" width="28" height="24" class="f-acc" stroke="currentColor"/>
  <text x="10" y="222" fill="currentColor">conflict, G=0 (74)</text>
  <rect x="180" y="206" width="28" height="24" class="f-warn" stroke="currentColor" stroke-dasharray="4 2"/>
  <rect x="208" y="206" width="154" height="24" class="f-warn" stroke="currentColor"/><text x="258" y="223" fill="currentColor">tRP 22</text>
  <rect x="362" y="206" width="154" height="24" class="f-mut" stroke="currentColor"/><text x="407" y="223" fill="currentColor">tRCD 22</text>
  <rect x="516" y="206" width="154" height="24" class="f-ok" stroke="currentColor"/><text x="566" y="223" fill="currentColor">CL 22</text>
  <rect x="670" y="206" width="28" height="24" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="10" y="268">G=0 conflict: 앞 ACT 가 48 cycle 전 → tRAS 52 까지 4 cycle 대기 후 PRE (점선 칸). 74 = tRC.</text>
    <text x="10" y="288">초록 = CL, 회색 = tRCD(ACT), 주황 = tRP(PRE), 진한 칸 = BL8 데이터 4 cycle. 지연은 마지막 beat 까지.</text>
  </g>
</svg>
```

**작은 트레이스 (LAT_TIMED, 한 뱅크, gap 0)** — row 1, 1, 2, 3 순서. 아래 숫자는 solution.c 의 함수로 찍은 실제 값이다.

| 요청 | open: 분류 | ACT | RD | 완료 | 지연 | close: ACT | RD | 완료 | 지연 | ready (PRE+tRP) |
|---|---|---|---|---|---|---|---|---|---|---|
| r1 row1 @0 | empty | 0 | 22 | 48 | 48 | 0 | 22 | 48 | 48 | 74 |
| r2 row1 | hit | — | 48 | 74 | 26 | 74 | 96 | 122 | 74 | 148 |
| r3 row2 | conflict | 96 | 118 | 144 | 70 | 148 | 170 | 196 | 74 | 222 |
| r4 row3 | conflict | 170 | 192 | 218 | **74** | 222 | 244 | 270 | 74 | 296 |

- open r3: 요청이 74 에 오고, ACT 0 + tRAS 52 와 RD 48 + tRTP 12 = 60 은 이미 지났다 → PRE 74, ACT 96. 지연 70.
- open r4: 요청이 144 에 오는데 ACT 96 + 52 = 148 → **4 cycle 대기**. 지연 74.
- close: 모든 접근이 empty 지만, 요청이 앞 데이터 직후(gap 0)에 오면 PRE(ACT+52) + tRP = ACT + 74 까지 기다린다 → 매번 74. 합계 open 218 vs close 270.

### Part B — 워크로드별 결과 (실제 출력)

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

```text
[Part B] n=4096 reads, mean latency in cycles (x0.625 = ns)
workload          hit empty confl | SIMopen SIMclos |  G0open  G0clos | G100opn G100cls | best(G0)
seq 64B          4064    16    16 |   26.26   48.00 |   26.26   73.80 |   26.26   48.00 | open
stride 256B      3968    16   112 |   27.29   48.00 |   27.29   73.19 |   27.29   48.00 | open
stride 4KB       2048    16  2032 |   47.91   48.00 |   47.91   61.00 |   47.91   48.00 | open
stride 8KB=row      0    16  4080 |   69.91   48.00 |   69.91   48.00 |   69.91   48.00 | close
stride 128KB        0     1  4095 |   69.99   48.00 |   73.99   73.99 |   69.99   48.00 | tie
random 8GB          0    16  4080 |   69.91   48.00 |   70.14   49.46 |   69.91   48.00 | close
pingpong 1bank      0     1  4095 |   69.99   48.00 |   73.99   73.99 |   69.99   48.00 | tie
hitmix 50%       2047     1  2048 |   48.01   48.00 |   48.01   73.99 |   48.01   48.00 | open
```

(hit/empty/confl 은 open-page 분류. SIM = LAT_SIMPLE, G0 / G100 = LAT_TIMED 에서 요청 사이 gap 0 / 100 cycle. 시뮬레이션이라 실행마다 같은 숫자.)

읽는 법:

- **순차 64 B**: row 하나에 라인 128개 → 127 hit / 1 miss. 뱅크 16개를 한 바퀴(128 KB) 돌 때마다 첫 방문은 empty, 두 번째 바퀴는 conflict. open 26.26 (= 16.4 ns) vs close 48. G = 0 close 는 73.80 — 같은 row 를 다시 쓰려 해도 닫고 다시 열어야 해서 tRC 바닥에 붙는다.
- **stride 4 KB**: row 하나에 두 번 → hit 50 %, 정확히 손익분기 근처 (47.91 vs 48.00). G = 0 close 가 61 인 이유: 한 쌍의 첫 접근은 다른 뱅크라 precharge 가 숨겨져 48, 둘째는 같은 뱅크 연속이라 74 → 평균 61.
- **stride 8 KB (= row 크기)**: 매번 다음 뱅크, 16번째마다 같은 뱅크의 다음 row → open 은 거의 전부 conflict(70), close 는 48. 뱅크를 돌기 때문에 G = 0 에서도 precharge 가 숨겨진다. **close 가 31 % 빠르다.**
- **stride 128 KB (bank camping)**: 매번 같은 뱅크 다른 row. G = 0 에서는 open · close 모두 74 = tRC 바닥. 정책으로 못 고친다 → XOR 뱅크 해싱(MEM-B01, 연습 2)으로 뱅크를 흩어야 한다.
- **랜덤 8 GB**: 64 K row 중 같은 row 를 다시 칠 확률이 없으니 hit 0. open 70 vs close 48. G = 0 에서 1/16 확률로 같은 뱅크가 연속돼 tRAS / tRP 대기가 조금 붙는다 (70.14, 49.46).
- **ping-pong**: 최악. open 은 매번 conflict.

### Part B — 손익분기 h\* 유도

**SIMPLE 모드, empty 없음.** 한 뱅크에 접근이 계속 오고 hit 비율이 h 이면 open 평균 = h·L_hit + (1 − h)·L_conf, close 평균 = L_empty. 같게 놓으면

h\* = (L_conf − L_empty) / (L_conf − L_hit) = tRP / (tRP + tRCD).

L_conf − L_empty = tRP, L_conf − L_hit = tRP + tRCD 이고 CL 과 버스트는 지워진다. DDR4-3200AA 는 tRP = tRCD = 22 → **h\* = 22/44 = 1/2**. tRP = tRCD 인 한 속도 등급과 무관하게 1/2 이다.

**empty 가 섞일 때.** 접근 수를 H, E, C 라 하면

close 합 − open 합 = (H + E + C)·L_empty − (H·L_hit + E·L_empty + C·L_conf) = H·tRCD − C·tRP.

E 는 양쪽에서 같으니 사라진다. **H·tRCD > C·tRP 이면 open.** solution.c 는 6개 워크로드 전부에서 이 등식이 정확히 성립함을 확인한다 (`rule`). stride 4 KB: H = 2048, C = 2032 → 차 = 16 × 22 = 352 cycle 로 open 이 근소하게 이긴다.

**시각 추적 모드에서는 gap G 가 끼어든다.** 한 뱅크, hit/conflict 섞인 스트림, 4 ≤ G ≤ 26 일 때:

- close: 앞 ACT 를 a 라 하면 완료 a + 48, PRE a + 52, ready a + 74. 다음 요청은 a + 48 + G 에 오므로 26 − G 만큼 기다린다 → 지연 48 + (26 − G).
- open: conflict 는 앞 ACT 뒤 48 + G ≥ 52 이므로 tRAS 대기가 없다 → 70. hit 는 26.

같게 놓으면 26h + 70(1 − h) = 74 − G → **h\*(G) = (G − 4) / 44** (4 ≤ G ≤ 26), G ≥ 26 이면 1/2. G = 20 → 16/44 = 0.364, G = 10 → 6/44 = 0.136 (연습 4). G < 4 이면 close 가 74 − G 이고 open 도 conflict 연속이면 74 − G 라 **open 이 항상 이기거나 비긴다.** "close-page 의 이득 = precharge 를 숨길 idle 시간" 이라는 말의 정량판이다.

스윕 실제 출력 (발췌, HITMIX n = 4097, 한 뱅크):

```text
 hit% | SIMopen SIMclos |  G0open  G0clos | G20open G20clos
    0 |   69.99   48.00 |   73.99   73.99 |   69.99   54.00
   25 |   59.00   48.00 |   61.00   73.99 |   59.00   54.00
   35 |   54.60   48.00 |   55.81   73.99 |   54.60   54.00
   40 |   52.40   48.00 |   53.20   73.99 |   52.40   54.00
   45 |   50.20   48.00 |   50.60   73.99 |   50.20   54.00
   50 |   48.00   48.00 |   48.00   73.99 |   48.00   54.00
   55 |   45.81   48.00 |   45.81   73.99 |   45.81   54.00
   75 |   37.00   48.00 |   37.00   73.99 |   37.00   54.00
  100 |   26.01   48.00 |   26.01   73.99 |   26.01   54.00
```

- SIMPLE: h = 50 % 에서 합계가 **정확히** 같다 (테스트 `o.total == c.total`). 45 % 는 close, 55 % 는 open.
- G = 20: 35 % 에서 close(54.60 > 54.00), 40 % 에서 open(52.40 < 54.00) → 이론 h\* = 36.4 % 와 맞는다.
- G = 0: close 는 늘 73.99 → open 이 모든 h 에서 이기거나 비긴다 (h = 0 에서 tie).
- G = 0 open 이 h ≤ 50 % 에서 SIMPLE 보다 큰 이유: conflict 뒤 바로 conflict 면 tRAS 대기 4 cycle 이 붙는다 (74). h ≥ 50 % 이면 conflict 앞에 늘 hit 가 있어 tRAS 가 이미 지났다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">평균 지연 vs hit 비율 h — solution.c HITMIX 스윕 실제 값 (cycle)</text>
  <line x1="80" y1="300" x2="690" y2="300" stroke="currentColor"/>
  <line x1="80" y1="40" x2="80" y2="300" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="74" y="318">0</text><text x="220" y="318">0.25</text><text x="372" y="318">0.5</text><text x="520" y="318">0.75</text><text x="676" y="318">1.0</text>
    <text x="380" y="340">hit 비율 h</text>
    <text x="48" y="304">20</text><text x="48" y="261">30</text><text x="48" y="217">40</text><text x="48" y="174">50</text><text x="48" y="131">60</text><text x="48" y="87">70</text><text x="48" y="44">80</text>
  </g>
  <polyline points="80,83 680,274" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <polyline points="80,66 230,122 380,179 530,226 680,274" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 3"/>
  <line x1="80" y1="179" x2="680" y2="179" stroke="currentColor" stroke-width="2.5"/>
  <line x1="80" y1="153" x2="680" y2="153" stroke="currentColor" stroke-width="1.5" stroke-dasharray="2 3"/>
  <line x1="80" y1="66" x2="680" y2="66" stroke="currentColor" stroke-width="1.5" stroke-dasharray="10 4"/>
  <circle cx="380" cy="179" r="6" class="f-acc" stroke="currentColor"/>
  <circle cx="298" cy="153" r="6" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="560" y="210">open (SIMPLE, G=20, G=100)</text>
    <text x="560" y="174">close SIMPLE = 48</text>
    <text x="560" y="148">close G=20 = 54</text>
    <text x="560" y="60">close G=0 = 74 (tRC 바닥)</text>
    <text x="236" y="112">open G=0 (점선)</text>
    <text x="180" y="215">h* = 1/2 (tRP/(tRP+tRCD))</text>
    <text x="190" y="145">h*(20) = 16/44</text>
  </g>
</svg>
```

### Part C — PMU 카운터에서 정책으로

open-page 로 도는 컨트롤러에서

- CAS(읽기) = H + E + C (모든 접근)
- ACT = E + C (hit 는 ACT 가 없다)
- PRE_MISS = C (conflict 때문에 나간 PRE)

→ **H = CAS − ACT, C = PRE_MISS, E = ACT − PRE_MISS**. page hit rate = 1 − ACT/CAS 라는 흔한 공식이 여기서 나온다. 서버 CPU 의 uncore IMC PMU 에 이런 이벤트가 있다 (예: Intel 의 CAS_COUNT · ACT_COUNT · PRE_COUNT.PAGE_MISS 계열 — 이름과 의미는 세대별로 다르니 해당 uncore 문서 확인). 임베디드 SoC 의 DDR 컨트롤러(예: DesignWare uMCTL2 류)도 성능 카운터를 두는 경우가 많지만 이벤트 구성은 IP 마다 다르다 — 레퍼런스 매뉴얼 확인.

| 예 | CAS | ACT | PRE_MISS | H / E / C | open 합 | close 합 | 추천 |
|---|---|---|---|---|---|---|---|
| CPU 단일 스레드 스트리밍 | 1,000,000 | 300,000 | 250,000 | 700k / 50k / 250k | 38.1 M | 48.0 M | **OPEN** (9.9 M cycle 절약) |
| 많은 스트림이 섞인 가속기 | 1,000,000 | 900,000 | 880,000 | 100k / 20k / 880k | 65.16 M | 48.0 M | **CLOSE** (17.16 M 절약) |

`pmu_recommend` 가 돌려주는 saved 는 정확히 H·tRCD − C·tRP 다 (9.9 M = 700k·22 − 250k·22). 마지막 테스트는 시뮬레이터가 센 stride 4 KB 의 클래스를 카운터 형태(CAS = n, ACT = E + C, PRE_MISS = C)로 넣어 REC_OPEN, saved = 352 = 시뮬레이터의 close − open 차와 같음을 확인한다.

**함정 두 가지.**

1. 이미 **close-page 로 도는** 시스템에서는 CAS = ACT 이고 PRE_MISS = 0 이라 hit 를 볼 수 없다. "open 이었다면" 을 알려면 뱅크마다 마지막 row 를 기억하는 shadow 레지스터가 필요하다 (연습 3 — 16 뱅크 × (row 16 비트 + valid) ≈ 34 B). 또는 잠깐 open 으로 바꿔 측정한다.
2. 위 비교는 SIMPLE 모델이다. 같은 뱅크 연속이 많으면(Part B 의 G = 0) close 의 이득은 사라진다. 결론을 낼 때는 hit 비율과 함께 **요청 간격과 뱅크 분산**도 본다.

**이 SoC 에는 어떤 정책? (정리표)**

| 워크로드 | 보이는 것 | 추천 | 이유 |
|---|---|---|---|
| 스트리밍 (DMA, 디스플레이, memcpy, 순차 버퍼) | hit 90 % 이상 | open | 26 vs 48 cycle |
| 작은 stride (≤ row/2) | hit ≥ 50 % | open (근소) | H·tRCD > C·tRP |
| row 크기 이상 stride, 여러 뱅크로 흩어짐 | hit 0, 뱅크 분산 | close | 48 vs 70, precharge 숨김 |
| 랜덤 (다수 코어 서버, 포인터 많은 코드) | hit ≈ 0 | close | 48 vs 70 (−31 %) |
| 한 뱅크 camping / ping-pong | conflict + 같은 뱅크 연속 | 정책 무의미 | tRC 바닥 → 매핑(XOR 해싱)을 고친다 |
| 단계별로 섞임 (burst 후 idle) | hit 가 시간적으로 몰림 | 타임아웃 / adaptive | 연습 1: 둘 다 이김 |

### 복잡도와 메모리

| 함수 | 시간 | 메모리 |
|---|---|---|
| `rb_classify`, `class_latency`, `access_*` | O(1) | 뱅크당 int32 + int64 × 3 = 28 B |
| `sim_run` | O(n) | 뱅크 상태 16 × 32 B (스택) |
| `gen_trace` | O(n) | 호출자 버퍼 (n × 8 B) |
| `pmu_recommend` | O(1) | — |

solution.c 전체 0.14 초 (보고서 포함, 이 맥 M6, 실측 `time`). 실제 컨트롤러라면 분류기는 뱅크당 "열린 row 레지스터 + 비교기" 하나다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 52/52 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음, 52/52
```

## 7. 자주 하는 실수

**1. close-page 에서 hit 를 센다.** 접근 뒤 `open_row` 를 그대로 두고 "정책은 지연 계산에서만" 처리하면 close 인데 hit 가 나온다. close 는 상태를 닫는 것이다.

```c
b->open_row = (pol == POL_OPEN) ? (int32_t)row : NO_ROW;   /* 분류 다음, 반환 전에 */
```

**2. tRAS 를 잊는다.** conflict 를 늘 70 으로 두면 gap 0 ping-pong 을 70 으로 계산한다 (실제 74). 반대로 SIMPLE 모드를 쓰면서 tRAS 를 반쯤 넣으면 숫자가 섞인다. 모드를 정하고 문서에 적는다: 이 레슨은 SIMPLE = 무시, TIMED = 적용.

**3. "close-page = 항상 tRCD + CL" 이라고 믿는다.** precharge 가 끝나 있어야 한다. 같은 뱅크에 바로 다음 요청이 오면 PRE(ACT + 52) + tRP 까지 기다려 74 다. 순차 G = 0 close 가 73.80 인 이유.

**4. stride 워크로드를 바이트로 안 맞춘다.** stride 8 KB 를 "작은 stride" 로 생각하면 틀린다 — 이 매핑에서 8 KB 는 row 크기라 매번 다른 뱅크/row 다. stride 128 KB 는 뱅크 비트를 건너뛰어 한 뱅크에 몰린다. stride 는 항상 매핑의 비트 위치와 같이 본다.

**5. 손익분기를 h = H / N 으로 비교한다.** empty 가 많으면 h 가 작아도 open 이 이길 수 있다 (예: H = 10, E = 980, C = 10 → H·tRCD = C·tRP, tie, h = 1 %). 판정은 H·tRCD vs C·tRP 로 한다.

**6. ns → cycle 을 float ceil 로 한다.** 이 레슨의 값(13.75 / 0.625)은 double 로도 정확하지만, 이진 표현이 안 되는 값은 `ceil(1.1 / 0.1) = 12` 처럼 1 cycle 씩 틀린다. 정수 ps 로 `(ps + tck − 1) / tck` (MEM-B02).

**7. rand() 나 시간 시드로 워크로드를 만든다.** 표의 숫자를 재현할 수 없고 테스트가 흔들린다. 고정 시드 xorshift64 를 쓴다.

## 8. 면접 Q&A

<details><summary>Q1. row hit / empty / conflict 지연을 DDR4-3200 기준으로 말해 보세요. 실제 CPU 에서 재는 지연은 왜 훨씬 큰가요?</summary>

hit = CL = 13.75 ns, empty = tRCD + CL = 27.5 ns, conflict = tRP + tRCD + CL = 41.25 ns (버스트 2.5 ns 를 더하면 16.25 / 30 / 43.75). 그런데 포인터 체이싱으로 재면 DRAM 지연이 보통 80~120 ns 다 (MEM-B10). 차이는 DRAM 밖에 있다: 캐시 계층 miss 판정(L1 → L2 → LLC/SLC), 온칩 네트워크, 메모리 컨트롤러 큐와 스케줄링, PHY, 그리고 TLB miss. DRAM 코어 시간은 전체의 절반 이하인 경우가 많다. 그래도 conflict vs hit 의 차이(≈ 27 ns)는 그대로 보이므로 정책이 의미가 있다.

</details>

<details><summary>Q2. open-page 와 close-page 의 손익분기 hit 비율을 유도해 보세요.</summary>

open = h·(CL) + (1 − h)·(tRP + tRCD + CL), close = tRCD + CL. 같게 놓으면 h\* = tRP / (tRP + tRCD). tRP = tRCD 면 1/2. empty 가 섞이면 차이가 H·tRCD − C·tRP 이므로 "hit 가 아끼는 tRCD 총합 > conflict 가 더 쓰는 tRP 총합" 이면 open. 꼬리질문으로 "타이밍까지 넣으면?" 이 오면: close 의 empty 는 precharge 가 숨겨져야 성립하므로, 같은 뱅크 요청 간격 G 가 tRC − (tRCD + CL + 4) = 26 cycle 보다 짧으면 close 도 기다린다. 이 레슨 모델에서 h\*(G) = (G − 4)/44 (4 ≤ G ≤ 26), G < 4 면 close 가 이길 수 없다.

</details>

<details><summary>Q3. tRAS 는 왜 존재하고, 성능에서 어디에 보이나요?</summary>

DRAM 읽기는 파괴적이다. ACT 로 셀 전하를 bitline 에 나누면 셀 값이 약해지고, sense amp 가 증폭한 값을 다시 셀에 써 넣어야(restore) 한다. tRAS 는 그 restore 가 끝나기 전에 PRE 로 row 를 닫지 못하게 하는 최소 시간이다. 성능에서는 "row 를 열고 바로 닫는" 패턴에 보인다: 같은 뱅크 conflict 연속, close-page 연속 → 둘 다 ACT 간격이 tRC = tRAS + tRP = 74 cycle 이 된다. 이 레슨 트레이스의 r4 가 4 cycle 기다린 이유다.

</details>

<details><summary>Q4. row miss(conflict) 지연에서 무엇이 지배적이고, 줄이려면?</summary>

conflict 70 cycle 중 tRP + tRCD = 44 cycle 이 "row 를 바꾸는 비용" 이고 CL 22 + 버스트 4 는 hit 와 같다. 줄이는 방법: (1) 같은 row 접근을 모아 hit 로 바꾼다 — 컨트롤러의 FR-FCFS 스케줄링(MEM-M01), 소프트웨어 타일링(ARCH-M07). (2) conflict 를 다른 뱅크로 흩는다 — XOR 뱅크 해싱(MEM-B01, MEM-M02), 그러면 PRE/ACT 가 다른 뱅크의 데이터 전송과 겹친다. (3) 정책으로 precharge 를 미리 해 둔다 — close / 타임아웃. (4) 부품: 낮은 tRCD/tRP bin.

</details>

<details><summary>Q5. GPU/가속기와 CPU 에서 페이지 정책이 어떻게 다른가요?</summary>

일반론으로 답하고 단정은 피한다 (벤더 구현은 비공개가 많다). 단일 코어 · 스트리밍 위주의 시스템은 row 지역성이 커서 open 이 유리하다. 코어가 많은 서버는 여러 스레드의 접근이 섞여 row 지역성이 깨지므로 BIOS 에 close-page 나 adaptive 옵션을 두는 경우가 많다. GPU/AI 가속기는 접근이 많고 동시에 쌓이기 때문에 정책보다 **큰 큐 + FR-FCFS 로 같은 row 요청을 모으는 것**이 핵심이고, 메모리 접근을 coalescing 해서 row 를 한 번에 쓰도록 만든다 (HBM 은 MEM-A03). 모바일 LPDDR 은 전력도 본다: row 를 열어 두면 active standby 전류가 흐르고, 닫아야 precharge power-down 에 들어갈 수 있다 (MEM-M08).

</details>

<details><summary>Q6. close-page 인데도 순차 접근이 느린 경우가 있다고요? 왜죠?</summary>

요청이 한 뱅크에 연달아 오면, 앞 요청의 auto-precharge 가 ACT + tRAS 에야 나가고 tRP 가 더 걸린다. 다음 ACT 는 ACT + tRC = 74 cycle 뒤다. 그래서 같은 row 를 다시 쓰는데도 매번 74 cycle (실측: 순차 G = 0 close 평균 73.80 vs open 26.26). 이것이 "close-page 의 이득은 precharge 를 숨길 idle 이 있을 때만" 의 반례다. 실제 컨트롤러는 같은 row 요청이 큐에 있으면 auto-precharge 를 걸지 않는 식으로 완화한다 (구현마다 다름).

</details>

<details><summary>Q7. 우리 SoC 의 페이지 정책을 바꿔야 하는지 어떻게 판단하나요?</summary>

(1) 대표 워크로드에서 컨트롤러 PMU 의 CAS · ACT · PRE_MISS 를 읽어 H/E/C 를 역산하고 H·tRCD vs C·tRP 를 본다 (Part C). (2) 이미 close 로 돌고 있으면 hit 가 안 보이므로 shadow row 로 추정하거나 open 으로 잠깐 바꿔 잰다. (3) 결론은 카운터만이 아니라 실제 지표로 확인한다: 포인터 체이싱 지연(MEM-B10), STREAM 대역폭(MEM-B11), 그리고 제품 워크로드의 프레임 시간/처리량. (4) 정책 레지스터를 바꾸면 리프레시(MEM-B05), 전력(MEM-M08), row hammer 노출(MEM-A02 — close/잦은 ACT 는 같은 row 의 활성화 횟수를 늘린다)도 같이 확인한다.

</details>

<details><summary>Q8. 타임아웃(adaptive) 정책의 타임아웃 값은 어떻게 고르나요?</summary>

같은 row 에 다시 오는 요청 간격 분포를 본다. 재사용 간격보다 길고, "row 를 버리는" 간격보다 짧게. 연습 1 의 burst 워크로드(burst 안 gap 5, burst 사이 gap 300)에서 5 < T < 300 이면 burst 안은 hit(26), burst 사이는 precharge 가 숨겨져 empty(48) → 평균 31.5 cycle 로 open(36.98)과 close(63.75)를 둘 다 이긴다. 경계가 함정이다: T 가 gap 과 같으면 요청이 오는 순간 PRE 가 나가 최악이 된다 (T = 5 → 64.50, close 보다 나쁨; T = 300 → empty 로 세지만 tRP 를 통째로 기다림). 실제 컨트롤러는 hit/miss 이력으로 T 를 적응시키기도 한다 (구현마다 다름).

</details>

## 9. 추가 연습

### 연습 1 — 타임아웃 페이지 정책

TIMED 모델에 POL_TIMEOUT 을 추가하라: 마지막 데이터 뒤 T cycle 동안 요청이 없으면 p = max(done + T, ACT + tRAS, RD + tRTP) 에 PRE. burst 워크로드(한 뱅크, 같은 row 4번을 gap 5, burst 사이 gap 300, 1024 요청)에서 T ∈ {0, 4, 5, 6, 50, 300, 301, ∞} 를 재고 open/close 와 비교하라.

<details><summary>답 보기</summary>

실제 출력 (cycle/req): open 36.98, close 63.75. T = 0 · 4 → 63.75 (tRAS 가 끝나는 ACT + 52 에 닫힘 = RDA 와 같은 시각), T = 5 → 64.50 (요청 도착 순간 PRE → RDA 보다 1 cycle 늦어 768 cycle 손해), T = 6 · 50 → **31.50** (hit 768, empty 256, conflict 0), T = 300 → 36.98 (empty 로 세지만 PRE 가 도착 순간이라 tRP 22 를 다 기다림 = open 과 같은 합), T = 301 · ∞ → 36.98 = open. 교훈: 분류(empty)만 보면 속는다 — 지연은 PRE 가 **언제** 나갔는지에 달렸다.

</details>

### 연습 2 — XOR 뱅크 해싱과 bank camping

bank ^= row[3:0] 해싱을 켜고 stride 128 KB 를 G = 0 TIMED 로 다시 돌려라. 쓰이는 뱅크 수, open/close 평균, 순차 워크로드의 hit 수가 어떻게 되나?

<details><summary>답 보기</summary>

뱅크 1개 → 16개. open 73.99 → 69.91 (16 empty + 4080 conflict, tRAS 대기 없음), close 73.99 → **48.00** (같은 뱅크 재방문이 16 요청 뒤라 precharge 가 숨겨짐). 순차 hit 는 4064 그대로 (해싱은 row 안 비트를 안 건드림). row 비트를 뱅크에 XOR 해도 같은 row 는 같은 뱅크라 hit 는 보존되고, row 만 다른 접근이 뱅크로 흩어진다. 매핑 설계 일반론은 MEM-B01 / MEM-M02.

</details>

### 연습 3 — shadow row 레지스터

close-page 로 돌면서 뱅크마다 "마지막 row" 만 기억하는 shadow 를 두고, open 이었다면의 hit/conflict 와 SIMPLE 총 지연을 세라. 7개 워크로드에서 실제 open 시뮬레이션과 같은지 확인하라.

<details><summary>답 보기</summary>

7개 전부 hit · conflict · 총합이 정확히 같다 (SIMPLE 모드에서 분류는 "마지막 row" 하나로 결정되므로). 비용은 뱅크당 row 비트 16 + valid 1 → 16 뱅크 × 17 비트 ≈ 34 B. 현실 주의: TIMED 에서는 shadow 로 tRAS 대기까지 예측할 수 없으므로 shadow 는 hit 비율 추정용이다. close 로 운영하는 시스템에서 PMU 로 hit 를 볼 수 없는 문제(Part C 함정 1)의 해법이다.

</details>

### 연습 4 — 손익분기 일반화

(a) DDR4-2400 17-17-17 과 LPDDR4X 류 tRCD 39 / tRP 45 cycle (18 ns / 21 ns 를 tCK 0.469 ns 로 바꾼 대표값 — 데이터시트 확인)의 h\* 를 기약분수로. (b) 랜덤 (H, E, C) 1000 조합에서 close − open = H·tRCD − C·tRP 를 직접 합과 대조. (c) TIMED G = 10 에서 h\*(10) = 6/44 를 HITMIX 10 % / 15 % 로 확인.

<details><summary>답 보기</summary>

(a) 17/34 = **1/2**, 45/84 = **15/28 ≈ 0.536** — tRP > tRCD 면 open 이 이기려면 hit 가 더 필요하다. (b) 1000/1000 일치. (c) close 평균 64.00 (= 48 + 26 − 10, 실측). 10 % 에서 open 65.6 > 64 → close, 15 % 에서 63.4 < 64 → open. 식 h\*(G) = (G − 4)/44 와 맞다.

</details>

### 연습 5 — stride 의 닫힌 식

stride s = 2^6 … 2^18 바이트, n = 4096 에서 hit/empty/conflict 를 식으로 쓰고 시뮬레이터와 대조하라.

<details><summary>답 보기</summary>

row 한 번 방문에 쓰는 라인 k = max(1, 8192/s), row 방문 V = n/k, 쓰이는 뱅크 B = 16 (s ≤ 8 KB) 또는 max(1, 131072/s). hit = n − V, empty = min(V, B), conflict = V − empty. 실제 출력:

```text
  stride    hit  empty  confl
      64   4064     16     16
     256   3968     16    112
    4096   2048     16   2032
    8192      0     16   4080
   16384      0      8   4088
   65536      0      2   4094
  131072      0      1   4095
```

(발췌, 13개 stride 전부 식과 일치.) hit 비율 = 1 − s/8192 → 4 KB 에서 1/2 = h\*. "row 크기의 절반보다 작은 stride 면 open" 이 이 매핑의 규칙이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 17/17 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 오류 없음
```

## 10. 핵심 정리

- hit / empty / conflict = CL / tRCD + CL / tRP + tRCD + CL. DDR4-3200AA 마지막 beat 까지 26 / 48 / 70 cycle = 16.25 / 30 / 43.75 ns.
- open 은 hit 를 사고 conflict 를 진다. close 는 늘 empty 지만 precharge 를 숨길 idle 이 필요하다.
- 손익분기 h\* = tRP / (tRP + tRCD) (DDR4 = 1/2). empty 포함: **H·tRCD > C·tRP 이면 open.**
- 같은 뱅크에 다른 row 가 연달아 오면 정책과 무관하게 tRC(74 cycle)마다 하나 — 고칠 곳은 매핑(XOR 해싱)과 스케줄링이다.
- 실측표: 순차 open 26.3 vs close 48, row 크기 stride · 랜덤 open 70 vs close 48, 4 KB stride ≈ 동률, bank camping G = 0 은 둘 다 74.
- PMU: H = CAS − ACT, C = PRE_MISS, E = ACT − PRE_MISS. close 로 돌면 hit 가 안 보인다 → shadow row.
- 타임아웃 정책은 재사용 간격과 idle 간격 사이에 T 를 둘 때 둘 다 이긴다. T = 간격이면 최악.

## 참고자료

- JEDEC JESD79-4 DDR4 SDRAM Standard — tRCD / tRP / tRAS / tRC / tRTP 정의와 speed bin (유료 문서; 이 레슨 값은 DDR4-3200AA 대표값, 데이터시트 확인 권장): <https://www.jedec.org/standards-documents/docs/jesd79-4a>
- Micron DDR4 SDRAM 데이터시트 (예: MT40A 계열) — speed bin 표와 명령 타이밍 다이어그램.
- Bruce Jacob, Spencer Ng, David Wang, *Memory Systems: Cache, DRAM, Disk* (2008) — row buffer, 페이지 정책, 주소 매핑 장.
- Ulrich Drepper, "What Every Programmer Should Know About Memory" (2007) — DRAM 접근 프로토콜과 tRAS/tRP: <https://people.freebsd.org/~lstewart/articles/cpumemory.pdf>
- S. Rixner et al., "Memory Access Scheduling", ISCA 2000 — FR-FCFS 와 row hit 우선 (MEM-M01 에서 다룸).
- Intel Xeon uncore performance monitoring reference (IMC 의 CAS / ACT / PRE 이벤트 — 세대별 이름 확인 필요).
- 관련 레슨: MEM-B01 (주소 매핑 · XOR 해싱), MEM-B02 (뱅크 상태머신 · 타이밍 체커), MEM-B04 (버스트와 대역폭), MEM-B05 (리프레시가 row 를 닫음), MEM-B10 (포인터 체이싱 지연 실측), MEM-B11 (STREAM), MEM-M01 (FR-FCFS), MEM-M02 (매핑과 뱅크 병렬성), MEM-M08 (저전력 상태), MEM-A01 (bank group · tFAW), MEM-A02 (Row hammer), MEM-A03 (HBM), ARCH-B10 (AMAT), ARCH-M07 (타일링)
