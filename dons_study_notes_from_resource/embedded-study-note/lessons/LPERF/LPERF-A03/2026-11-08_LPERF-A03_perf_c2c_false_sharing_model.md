---
id: LPERF-A03
time: 110분
updated: 2026-11-08
---
# LPERF-A03 perf c2c로 false sharing 찾기

## 학습 목표

- 진짜 `perf c2c` 가 왜 PMU 메모리 샘플링(Intel PEBS 계열, Arm SPE, AMD IBS)을 요구하는지, 이 Lima VM 에서 어떤 오류로 실패하는지 직접 보고 말할 수 있다.
- HITM 의 정의("다른 코어가 M 으로 가진 라인을 찾은 **load**")와 왜 store 쪽에는 HITM 이 없고 L1Hit / L1Miss 만 있는지 perf 소스(`c2c_decode_stats`)로 설명한다.
- 접근 trace 를 라인별 상태 모델에 넣어 `Shared Data Cache Line Table` 과 `Shared Cache Line Distribution Pareto` 와 같은 모양의 표를 만들고, offset · tid · 코드 위치로 **true sharing / false sharing / 읽기 전용 희생자** 를 가른다.
- 패딩과 스레드 로컬 집계가 각각 무엇을 고치는지를 처리량(Mac + VM 실측)과 모델 HITM 의 변화로 나눠 설명한다. "패딩했는데 1.7배뿐" 인 이유까지.
- Graviton metal · Intel 서버에서 실제로 돌릴 때의 명령과 보고서 읽는 순서를 안다 (이 부분은 여기서 실행하지 못했다고 표시되어 있다).

## 1. 문제

> **이 레슨에서 진짜인 것 / 모델인 것 / 다른 하드웨어가 필요한 것.**
> **진짜:** `demo_stats.c` 의 처리량(맥 · VM), TSan/ASan 결과, VM 에서 `perf c2c record` 가 실패하는 오류 메시지, `perf c2c report` 의 표 머리글(메모리 이벤트가 없는 데이터로 돌린 실제 출력), `pahole` 레이아웃, perf 소스와 문서 인용.
> **모델:** 이 환경에는 PEBS/SPE 가 없어서 진짜 `perf c2c` 결과를 만들 수 없다. 대신 계측 빌드가 샘플링한 접근을 라인별 MESI 모델에 넣어 계산한 표를 쓴다. **그런 표에는 전부 "모델 출력 — perf 결과 아님" 을 붙였다.** HITM 숫자는 하드웨어 측정값이 아니다.
> **다른 하드웨어 필요:** 진짜 HITM · cycles 숫자. §5.8 의 Graviton metal / Intel 절차는 여기서 실행하지 못했고, 미확인으로 표시했다.

멀티코어 SoC 펌웨어나 드라이버에서 코어별 통계 카운터가 병목이 될 때, 코드를 읽어서는 "각자 자기 칸만 쓰는데 왜 느리지?" 에서 멈춘다. 답은 **데이터 주소와 코드 위치까지 귀속된 캐시 라인 경합 정보** 이고, 그걸 주는 도구가 `perf c2c` 다. 실리콘 검증 쪽에서는 똑같은 표를 "이 워크로드의 코어 간 전송이 어느 라인에 몰리나" 를 보는 데 쓴다.

이미 아는 것(다시 가르치지 않는다): false sharing 곡선과 패딩 규칙, M6 의 128 B 단위(ARCH-M08), 코어 수에 따른 일관성 트래픽(MEM-M04), MESI 전이 시뮬레이터(ARCH-A04), 원자 연산 비용(LLAT-B06), 스레드별 칸 + 합산 카운터(OS-B05), `perf lock contention` 쪽(LPERF-M07). 여기서는 **"어느 라인의 어느 offset 을 어느 코드가 때리는가" 를 표로 읽어 내는 일** 만 다룬다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">HITM 은 load 쪽 사건이다 — 같은 라인, 서로 다른 8 B</text>
  <rect x="20" y="40" width="150" height="46" class="f-mut" stroke="currentColor"/>
  <text x="34" y="60" fill="currentColor">코어 0 캐시</text>
  <text x="34" y="78" fill="currentColor" font-size="12">라인 X: M (slot0.pkts 씀)</text>
  <rect x="570" y="40" width="150" height="46" class="f-mut" stroke="currentColor"/>
  <text x="584" y="60" fill="currentColor">코어 1 캐시</text>
  <text x="584" y="78" fill="currentColor" font-size="12">라인 X: I</text>
  <line x1="570" y1="104" x2="170" y2="104" stroke="currentColor"/>
  <text x="220" y="98" fill="currentColor" font-size="12">① 코어 1 이 slot1.pkts 를 load → 코어 0 의 M 라인을 만남 = HITM</text>
  <line x1="170" y1="128" x2="570" y2="128" stroke="currentColor"/>
  <text x="256" y="122" fill="currentColor" font-size="12">② 코어 0 이 라인을 넘겨 준다 (둘 다 S)</text>
  <line x1="570" y1="156" x2="170" y2="156" stroke="currentColor"/>
  <text x="222" y="150" fill="currentColor" font-size="12">③ 코어 1 이 slot1.pkts 에 store → 소유권 요청 (RFO), 코어 0 은 I</text>
  <rect x="20" y="176" width="150" height="46" class="f-mut" stroke="currentColor"/>
  <text x="34" y="196" fill="currentColor">코어 0 캐시</text>
  <text x="34" y="214" fill="currentColor" font-size="12">라인 X: I</text>
  <rect x="570" y="176" width="150" height="46" class="f-acc" stroke="currentColor"/>
  <text x="584" y="196" fill="currentColor">코어 1 캐시</text>
  <text x="584" y="214" fill="currentColor" font-size="12">라인 X: M</text>
  <text x="200" y="200" fill="currentColor" font-size="12">④ 다음에 코어 0 이 자기 칸을 load 하면 다시 HITM</text>
  <rect x="10" y="238" width="720" height="52" rx="6" fill="none" stroke="currentColor"/>
  <text x="24" y="258" fill="currentColor">perf c2c 는 ① 을 Load Hitm 으로, ③ 을 Store L1Miss 로 센다.</text>
  <text x="24" y="278" fill="currentColor">그래서 쓰기끼리만 부딪히는 코드는 HITM 이 적게 보일 수 있다 — Store 칸도 같이 읽는다.</text>
</svg>
```

체크 테스트(`starter.py` → `solution.py`)는 **결정적인 것만** 본다: 손으로 계산한 golden trace 6개의 정확한 수치, 표의 정렬·필터 규칙, 커밋된 실제 trace 4개에서의 구조적 사실(HITM 이 남는 라인, 판정, 0 으로 가는지). **시간과 처리량은 `demo_stats.c` 에서만** 잰다.

### Part A — trace 파서와 라인 상태 모델

- `parse_trace(text)` — `<seq> <tid> <0xaddr> <R|W> <site>` 줄을 `Event` 로. 빈 줄·`#` 주석은 건너뛰고, seq 순으로 정렬. 형식 오류·seq 중복은 `ValueError` (메시지에 `line <n>`).
- `step(ls, tid, op)` — 라인 하나의 상태(`I/E/S/M`, holders)를 접근 하나만큼 진행하고 결과를 돌려준다. 읽기: `L1` / `HITM` / `LLC` / `DRAM`. 쓰기: `L1HIT` / `L1MISS`.
- `simulate(events, line_size)` — 라인·`(offset, site)` 단위로 합계를 낸다. 모델 규칙: 무한 캐시, 축출 없음, 스레드 하나 = 코어 하나.

### Part B — 표 두 개와 판정

- `line_table(lines, show_all)` — HITM 내림차순, 전체의 0.1 % 미만 라인은 숨김(`--show-all` 이면 모두). perf 소스의 `DISPLAY_LINE_LIMIT 0.001` 과 같은 규칙.
- `classify(line)` — offset 마다 `TRUE` / `FALSE-W` / `FALSE-R` / `-`, 라인 전체로 `TRUE` / `FALSE` / `MIXED` / `NONE`.
- `pareto(line)` — offset 순 행: `hitm_pct`, `st_l1hit_pct`, `st_l1miss_pct`, `tids`, `cpu_cnt`, `label`. 퍼센트 분모는 **그 라인의 합계**. 표 렌더러(`render_report`)는 주어져 있고, 표마다 라벨을 찍는다.

### Part C — 전/후 비교와 실제 trace

- `compare(before, after)` — HITM 합계, HITM 라인 수, 감소율, 남은 코드 위치.
- `suggest_fixes(line)` — offset 마다 처방 (`TRUE` → 스레드 로컬 집계, `FALSE-W` → 작성자별 라인 분리, `FALSE-R` → 읽기 위주 라인으로 이동).
- 실제 실행에서 얻어 커밋한 trace 4개(`trace_hidden/padded/tlagg/padded64.txt`)에 적용: `hidden` 은 2 라인(`MIXED` + `FALSE`), `padded` 는 `total_ops` 1 라인(`TRUE`), `tlagg` 는 HITM 0. `padded64` 는 64 B 관점에서는 깨끗해 보이지만 128 B 관점에서는 `FALSE` 라인이 돌아온다.

## 2. Problem (English)

You are profiling a multithreaded statistics module that "looks" contention-free: each worker increments only its own slot. Real `perf c2c` needs load-latency/SPE sampling that this environment lacks, so you build a coherence model over a sampled access trace and produce the same reports.

**Part A.** Implement `parse_trace(text) -> list[Event]` (fields `seq tid addr op site`; lines are `<seq> <tid> <0xaddr> <R|W> <site>`, `#` comments and blanks skipped, sorted by `seq`, `ValueError` mentioning `line <n>` on malformed input or duplicate `seq`). Implement `step(ls, tid, op)` for one line with states `I/E/S/M` and a holder set: a read returns `"L1"` (tid already holds a copy), `"HITM"` (another core holds it `M`; both end in `S`), `"LLC"` (other clean copies exist; end in `S`) or `"DRAM"` (no copy; end in `E`); a write returns `"L1HIT"` (tid holds it `M` or `E`) or `"L1MISS"` (otherwise); after a write the state is `M` with holders `{tid}`. Implement `simulate(events, line_size=64) -> {line_addr: LineStats}` (`ValueError` if `line_size` is not a power of two).

**Part B.** Implement `line_table(lines, show_all=False)` (sorted by HITM descending, then address; rows hidden when `hitm/total < 0.001` unless `show_all`), `classify(line) -> (verdict, {offset: label})`, and `pareto(line)`. An offset is `"TRUE"` if some thread writes it and at least two threads access it; otherwise `"FALSE-W"` (it has a writer) or `"FALSE-R"` (read-only) if a *different* offset in the line is written by a thread other than one of its accessors; otherwise `"-"`. The verdict is `"MIXED"` if both kinds appear, else `"TRUE"`, `"FALSE"` or `"NONE"`.

**Part C.** Implement `compare(before, after)` and `suggest_fixes(line)`. Using the committed traces, show that HITM goes to 0 after a thread-local-aggregate rewrite and that a 64-byte-padded layout still false-shares at a 128-byte granule.

Example: for two threads alternating `R`/`W` on `0x1000` and `0x1008`, one 64-byte line reports `records=8, loads=4, stores=4, hitm=3, ld_dram=1, st_l1hit=1, st_l1miss=3`, and the verdict is `FALSE`.

## 3. 시작 코드

<!-- code: starter.py -->

```bash
cd lessons/LPERF/LPERF-A03
python3 -I starter.py; echo "exit $?"     # TODO 는 값만 돌려준다: 멈추지 않고 FAIL, exit 1
```

실제 출력 (앞과 끝):

```text
-- Part A: 파서와 모델 --
FAIL  parse: 주석·빈 줄 건너뜀, seq 정렬  []
FAIL  parse: 16진 주소  []
FAIL  parse: tid/op/site  []
FAIL  parse: 잘못된 입력 ValueError (op)  None
…
FAIL  compare: 전 HITM 0 이면 drop 0  {}
FAIL  Part C: 전/후 비교와 실제 trace ran without exception  KeyError(24576)
1/40 passed
exit 1
```

관찰용 프로그램은 아래다. `VARIANT` 로 수정 전/후를 고르고 `-DTRACE=1` 로 접근 trace 를 뽑는다.

<!-- code: demo_stats.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread -DVARIANT=0 demo_stats.c -o /tmp/LPERF-A03_work/hidden
/tmp/LPERF-A03_work/hidden 4 $((1<<24))
# trace 생성 (맥에서 실행해 커밋):  -DTRACE=1 빌드를 ./x 4 1048576 14 > trace_hidden.txt
python3 -I solution.py report trace_hidden.txt
python3 -I solution.py compare trace_hidden.txt trace_tlagg.txt
```

## 4. 힌트

<details><summary>힌트 1 — step() 은 쓰기를 먼저 정하고 읽기를 나눈다</summary>

읽기 네 갈래는 "내가 사본을 갖고 있나 → 다른 코어가 M 인가 → 다른 사본이 있나 → 아무것도 없나" 순서로 묻는다. `tid in holders` 를 맨 앞에 두면 M 주인이 자기 라인을 읽는 경우도 자연히 `L1` 이 된다.
</details>

<details><summary>힌트 2 — E 를 빼먹으면 모든 비공유 데이터가 L1Miss 로 뜬다</summary>

한 스레드가 자기 라인을 처음 읽고(DRAM → E) 쓸 때, E 가 없으면 쓰기가 소유권 요청처럼 세어져 `자기 라인만` golden 의 `L1Miss 0` 이 깨진다. E 주인의 쓰기는 `L1HIT` 이고 조용히 M 으로 간다.
</details>

<details><summary>힌트 3 — simulate 는 라인별 상태를 하나씩</summary>

`states = defaultdict(LineState)`, `base = addr & ~(line_size-1)`, `offset = addr - base`. 합계는 `LineStats` 와 `row(offset, site)` 양쪽에 올리고, 쓰기는 `writers[offset]` 에, 모든 접근은 `accessors[offset]` 에 넣는다.
</details>

<details><summary>힌트 4 — classify 는 offset 마다 두 질문</summary>

1. 내 offset 에 쓰는 스레드가 있고 접근 스레드가 둘 이상인가 → TRUE.
2. 아니면: 다른 offset 의 쓰기 스레드 중에, 내 offset 접근 스레드 아닌 스레드가 있나 → 내 offset 에 쓰기가 있으면 FALSE-W, 없으면 FALSE-R.

"limit 을 읽는 스레드 4개 전부가 total 도 쓰는" hidden 라인에서 `limit` 이 FALSE-R 로 나오려면, 질문 2 에서 "접근 스레드 **아무나 하나** 와 다른 쓰기 스레드 하나" 면 충분해야 한다. `w ≠ t` 인 쌍이 있는지만 보면 된다.
</details>

<details><summary>힌트 5 — 표의 분모</summary>

`line_table` 의 `hitm_pct` 분모는 **전체** HITM, `pareto` 의 퍼센트 분모는 **그 라인** 합계. 분모가 0 이면 0.0 으로.
</details>

## 5. 풀이

### 5.1 이 환경에서 진짜 perf c2c 가 안 되는 이유

VM 에서 실제로 돌린 명령과 출력이다 (`perf version 7.0.14`, 커널 `7.0.0-38-generic`, aarch64, 6 vCPU).

```text
$ perf c2c record -o c2c.data -- ./hid_dbg 4 262144
failed: no PMU supports the memory events
exit 255
$ sudo perf c2c record -o c2c.data -- ./hid_dbg 4 262144
failed: no PMU supports the memory events
exit 255
$ sudo perf stat -e cycles,instructions,cache-misses -- true
Error:
No supported events found.
The cycles event is not supported.
$ ls /sys/bus/event_source/devices/
breakpoint  kprobe  software  tracepoint  uprobe
$ man perf-c2c
No manual entry for perf-c2c
```

`/sys/bus/event_source/devices/` 에 하드웨어 PMU 가 없다 (vz 게스트에 PMU 를 노출하지 않음, 레슨 env_note 그대로). 그래서 권한 문제가 아니다 — `sudo` 도 같은 오류다. **`man perf-c2c` 는 이 VM 에 없다.** 문서는 `/var/lab/src/linux-6.18.55/tools/perf/Documentation/perf-c2c.txt` 의 소스 텍스트를 본다(바이너리는 perf 7.0.14, 문서는 6.18.55 트리: 버전 차이로 옵션이 다를 수 있다).

perf 문서가 도구의 전제를 그대로 적고 있다.

```text
On Intel, the tool is based on load latency and precise store facility events
provided by Intel CPUs. On PowerPC, the tool uses random instruction sampling
with thresholding feature. On AMD, the tool uses IBS op pmu (due to hardware
limitations, perf c2c is not supported on Zen3 cpus). On Arm64 it uses SPE to
sample load and store operations, therefore hardware and kernel support is
required.
```

`perf stat` 의 cache-miss 카운터로는 안 되는 이유가 여기 있다: 카운터는 "**몇 번**" 만 준다. c2c 에 필요한 건 샘플마다의 **데이터 주소 · 코드 주소 · 어디서 온 데이터인가(data source)** 이고, 그건 PEBS/SPE 같은 샘플링 하드웨어만 준다.

### 5.2 숨은 프로그램

`demo_stats.c` 의 hidden 은 코드만 보면 경합이 없어 보인다. 워커마다 `s = &g_hub.slot[t->id]` 를 잡고 자기 칸만 올린다. `pahole` 이 레이아웃을 드러낸다 (VM, 실제 출력):

```text
typedef struct {
	uint32_t           limit __attribute__((__aligned__(128))); /*     0     4 */
	uint32_t           flags;                /*     4     4 */
	_Atomic uint64_t   total_ops;            /*     8     8 */
	worker_slot_t      slot[8];              /*    16   192 */

	/* size: 256, cachelines: 4, members: 4 */
	/* padding: 48 */
	/* forced alignments: 1 */
} stats_hub_t __attribute__((__aligned__(128)));
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">hidden 의 첫 128 B (64 B 라인 두 개) — offset 과 모델 판정</text>
  <text x="10" y="46" fill="currentColor" font-size="12">라인 0 (HITM 표 맨 위, 모델 판정 MIXED)</text>
  <rect x="10" y="54" width="70" height="42" class="f-warn" stroke="currentColor"/>
  <text x="16" y="72" fill="currentColor" font-size="12">0: limit</text><text x="16" y="88" fill="currentColor" font-size="12">FALSE-R</text>
  <rect x="80" y="54" width="90" height="42" class="f-acc" stroke="currentColor"/>
  <text x="86" y="72" fill="currentColor" font-size="12">8: total_ops</text><text x="86" y="88" fill="currentColor" font-size="12">TRUE</text>
  <rect x="170" y="54" width="280" height="42" class="f-mut" stroke="currentColor"/>
  <text x="176" y="72" fill="currentColor" font-size="12">0x10 slot[0] pkts · bytes · drops (tid 0)</text><text x="176" y="88" fill="currentColor" font-size="12">FALSE-W</text>
  <rect x="450" y="54" width="280" height="42" class="f-mut" stroke="currentColor"/>
  <text x="456" y="72" fill="currentColor" font-size="12">0x28 slot[1] pkts · bytes · drops (tid 1)</text><text x="456" y="88" fill="currentColor" font-size="12">FALSE-W</text>
  <text x="10" y="126" fill="currentColor" font-size="12">라인 1 (모델 판정 FALSE)</text>
  <rect x="10" y="134" width="330" height="42" class="f-ok" stroke="currentColor"/>
  <text x="16" y="152" fill="currentColor" font-size="12">0x00 slot[2] pkts · bytes · drops (tid 2)</text><text x="16" y="168" fill="currentColor" font-size="12">FALSE-W</text>
  <rect x="340" y="134" width="330" height="42" class="f-ok" stroke="currentColor"/>
  <text x="346" y="152" fill="currentColor" font-size="12">0x18 slot[3] pkts · bytes · drops (tid 3)</text><text x="346" y="168" fill="currentColor" font-size="12">FALSE-W</text>
  <text x="10" y="204" fill="currentColor" font-size="12">읽는 순서: 표의 맨 위 라인 → 같은 offset 을 여러 tid 가 쓰나(TRUE) → 서로 다른 offset 을 서로 다른 tid 가 쓰나(FALSE-W)</text>
  <text x="10" y="222" fill="currentColor" font-size="12">→ 쓰지 않는 offset 이 HITM 을 맞나(FALSE-R). offset 이 어느 필드인지는 pahole 또는 offsetof 로 찾는다.</text>
  <text x="10" y="240" fill="currentColor" font-size="12">(이 그림의 판정은 mini-c2c 모델 결과다. 진짜 perf c2c 는 판정 칸이 없고 사람이 표에서 읽어 낸다.)</text>
</svg>
```

이 코드에는 **data race 가 없다** (카운터는 전부 `_Atomic`, `limit` 은 스레드 시작 전에 쓴 뒤 읽기만). VM 에서 clang TSan(`setarch -R` 로 ASLR 끔)으로 hidden/padded/tlagg, 일반·TRACE 빌드 6 개를 전부 돌렸고 경고 0, 종료 코드 0 이었다. **그래서 TSan 은 이 문제를 찾지 못한다.** 성능 버그이지 정확성 버그가 아니다.

### 5.3 처리량: 패딩이 고치는 것과 못 고치는 것

4 스레드, 스레드당 2^24 반복, 5 회 중앙값. "Mops" 는 4 스레드 합계 반복/초다. 절대값이 아니라 **비율** 을 믿는다 — 측정 중 맥의 load 는 35~44 였다(다른 세션 벤치마크 동시 실행).

기계: Apple M6 (Super 2 + Performance 4 + Efficiency 6), `Apple clang version 21.0.0 (clang-2100.0.123.102)`. Mac: QoS `USER_INTERACTIVE`, 코어 고정 불가. VM: `7.0.0-38-generic`, 6 vCPU, gcc 15.2.0 (VM load 4.6).

| 변형 | 무엇이 바뀌나 | Mac Mops | VM Mops | Mac 대비 hidden |
|---|---|---:|---:|---:|
| hidden | 원래 코드 | 98.5 | 86.2 | 1.0× |
| hidden, `USE_TOTAL=0` | `total_ops` 갱신을 뺌 (false sharing 만 남음) | 850.6 | 964.9 | 8.6× |
| padded (CL=128) | 쓰는 주체별로 라인 분리, `total_ops` 는 그대로 | 164.8 | 152.2 | 1.7× |
| padded, `USE_TOTAL=0` | 패딩 + `total_ops` 제거 | 1903.5 | 1782.1 | 19× |
| padded (CL=64) | 패딩 단위 64 B | 163.8 | 131.0 | 1.7× |
| padded (CL=64), `USE_TOTAL=0` | 64 B 패딩 + `total_ops` 제거 | 1572.1 | 1805.6 | 16× |
| tlagg | 스레드 로컬 집계, 공유 쓰기 없음 | 2291.0 | 1634.5 | 23× |

읽는 법:

1. **패딩만 하면 1.7배** 다. 병목이 false sharing 만이 아니기 때문이다. 같은 `total_ops` 를 네 스레드가 RMW 하는 **true sharing** 이 비용 대부분을 먹는다. `USE_TOTAL=0` 행이 그 증거다(hidden → 8.6배 빨라짐, padded → 19배).
2. false sharing 만의 비용은 `hidden, USE_TOTAL=0`(851) 대 `padded, USE_TOTAL=0`(1904): 약 2.2 배 (VM 1.8 배). ARCH-M08 의 8 배는 카운터를 **쉬지 않고** 쓰는 스트레스였고, 여기는 한 반복에 일이 더 많아 비율이 낮다.
3. 진짜 해결은 라인 공유 자체를 없애는 것이다. `tlagg` 는 공유 라인에 쓰지 않으므로 23 배. 대가는 **읽는 쪽이 최대 `PUBLISH_EVERY` 만큼 오래된 값을 본다** 는 것이다 (읽을 때 합산, OS-B05).
4. CL=64 패딩은 이 측정에서 128 과 비슷했다(1.7×). 코어 종류가 섞이지 않게 QoS 를 고정했기 때문이다. E 코어가 끼면 달라진다는 건 ARCH-M08 의 측정이다.

주의: VM 은 vCPU 가 호스트에서 선점되므로 수치의 절대값·최솟값을 믿지 않는다. 비율만 본다.

### 5.4 Part A — 모델 만들기

```text
step(ls, tid, op) 전이 (스레드 하나 = 코어 하나, 무한 캐시):

 읽기                                             쓰기
 내가 사본 보유      → L1                          내가 M/E   → L1HIT, M
 다른 코어가 M       → HITM, 둘 다 S                그 외      → L1MISS, M(나 혼자), 다른 사본 무효
 다른 깨끗한 사본    → LLC, S
 사본 없음           → DRAM, E
```

손으로 따라가 본 golden (같은 64 B 라인, T0 는 0x1000, T1 은 0x1008):

| seq | 접근 | 결과 | 라인 상태 |
|---|---|---|---|
| 0 | T0 R 0x1000 | DRAM | E {0} |
| 1 | T0 W 0x1000 | L1HIT (E→M 조용히) | M {0} |
| 2 | T1 R 0x1008 | **HITM** | S {0,1} |
| 3 | T1 W 0x1008 | L1MISS (S 에서 쓰기) | M {1} |
| 4 | T0 R 0x1000 | **HITM** | S {0,1} |
| 5 | T0 W 0x1000 | L1MISS | M {0} |
| 6 | T1 R 0x1008 | **HITM** | S {0,1} |
| 7 | T1 W 0x1008 | L1MISS | M {1} |

→ records 8, loads 4, stores 4, HITM 3, dram 1, L1Hit 1, L1Miss 3. 두 스레드는 다른 주소만 쓰지만 **HITM 은 라인 단위로 튄다** 는 사실이 전부다. offset 별로는 0x0 에 1, 0x8 에 2 (첫 읽기가 DRAM 이라 T0 쪽이 하나 적다).

왜 trace 의 HITM 이 "진짜 HITM" 이 아닌가: `step` 은 실제 캐시 계층도, 지연도, 축출도 모른다. S → M 업그레이드를 `L1MISS` 로 세는 것도 모델 규칙이다(실제 PMU 가 그 경우를 어떻게 분류할지는 구현마다 다를 수 있다, 미확인).

### 5.5 Part B — 표 읽기

실제 실행 trace (맥에서 `demo_stats.c -DVARIANT=0 -DTRACE=1`, 4 스레드, 2^20 반복, 접근마다 1/2^14 샘플; 생성 당시 맥 load 22) 를 넣은 결과. 코드 위치 174 = `limit` 읽기, 197/198/200 = `BUMP(pkts/bytes/drops)`, 203/204 = `total_ops` R/W.

**모델 출력 — perf 결과 아님:**

```text
=================================================
           Shared Data Cache Line Table          
           [모델 출력 — perf 결과 아님]
=================================================
# trace_hidden.txt line=64
#                      Tot   --- Load Hitm ----    Total   Total   Total  - Stores -   Load   LLC  Load
# Index        Address Hitm    Total LclHit RmtHit records   Loads  Stores L1Hit L1Miss L1Hit LclHit  Dram
      0    0x102104100  77.3%     215    215      0    1202     723     479   139    340   424     84     0
      1    0x102104140  22.7%      63     63      0     558     272     286   148    138   209      0     0
# total HITM (model) = 278, lines with HITM = 2

=================================================
      Shared Cache Line Distribution Pareto      
      [모델 출력 — perf 결과 아님]
=================================================
  ---- Cacheline 0  addr 0x102104100  HITM 215  verdict MIXED ----
#   HITM%  L1Hit%  L1Miss%   Offset   recs cpu  label    tids      Code address
    34.0     0.0     0.0      0x0    239   4  FALSE-R  0,1,2,3   demo_stats.c:174
    36.3     0.0     0.0      0x8    239   4  TRUE     0,1,2,3   demo_stats.c:203
     0.0    48.2    55.3      0x8    255   4  TRUE     0,1,2,3   demo_stats.c:204
     6.0     8.6    11.5     0x10    117   1  FALSE-W  0         demo_stats.c:197
     8.4    12.2    11.2     0x18    109   1  FALSE-W  0         demo_stats.c:198
     1.4     0.0     0.9     0x20     11   1  FALSE-W  0         demo_stats.c:200
     8.4    15.1    10.9     0x28    120   1  FALSE-W  1         demo_stats.c:197
     5.6    15.1     9.7     0x30    108   1  FALSE-W  1         demo_stats.c:198
     0.0     0.7     0.6     0x38      4   1  FALSE-W  1         demo_stats.c:200
  ---- Cacheline 1  addr 0x102104140  HITM 63  verdict FALSE ----
#   HITM%  L1Hit%  L1Miss%   Offset   recs cpu  label    tids      Code address
    30.2    18.2    19.6      0x0    139   1  FALSE-W  2         demo_stats.c:197
    28.6    25.7    23.2      0x8    138   1  FALSE-W  2         demo_stats.c:198
     3.2     1.4     5.1     0x10     13   1  FALSE-W  2         demo_stats.c:200
    15.9    20.9    25.4     0x18    114   1  FALSE-W  3         demo_stats.c:197
    22.2    31.1    23.9     0x20    144   1  FALSE-W  3         demo_stats.c:198
     0.0     2.7     2.9     0x28     10   1  FALSE-W  3         demo_stats.c:200
```

읽는 순서 (진짜 `perf c2c report` 도 똑같다):

| 질문 | 어디를 보나 | 이 표에서 |
|---|---|---|
| 어느 라인이 비싼가 | 첫 표 맨 위, `Tot Hitm` | 라인 0 이 77.3 % |
| 쓰기 비용은 얼마인가 | `Stores L1Miss` | 라인 0 이 340 |
| 같은 offset 을 여러 tid 가 쓰나 | Pareto 의 offset + tids | 0x8 을 tid 0–3 이 씀 → `total_ops` **true sharing** |
| 서로 다른 offset 을 서로 다른 tid 가 쓰나 | Pareto 의 다른 offset | 0x10 대 0x28, tid 0 대 tid 1 → **false sharing** |
| 쓰지 않는 offset 이 HITM 을 맞나 | 쓰기 칸이 0 인 offset | 0x0 `limit` 이 34 % → **읽기 전용 희생자** (읽기 위주 필드를 쓰기 라인에서 떼라) |
| offset 이 어느 필드인가 | `pahole` / `offsetof` | 0x8 = `total_ops`, 0x10 = `slot[0].pkts` |

`limit` 을 고치면 HITM 의 34 % 가 사라질 것처럼 보이지만, 비용은 `total_ops` 쪽 RMW 가 크다. **HITM 퍼센트는 "비용" 이 아니라 "표본 수"** 다. 진짜 보고서라면 cycles 칸(`rmt hitm` / `lcl hitm` / `load`)도 같이 본다. 이 모델에는 cycles 가 없다.

분류기(`classify`)는 사람이 위 표에서 하는 판단을 그대로 코드로 옮긴 것이다. 진짜 `perf c2c` 에는 verdict 칸이 없다.

### 5.6 Part C — 전/후 비교

**모델 출력 — perf 결과 아님** (전/후 모두 맥에서 뽑은 trace):

```text
$ python3 -I solution.py compare trace_hidden.txt trace_padded.txt
[모델 출력 — perf 결과 아님]
  hitm_before   278
  hitm_after    101
  lines_before  2
  lines_after   1
  drop_pct      63.669064748201436
  remaining     ['demo_stats.c:203']
$ python3 -I solution.py compare trace_hidden.txt trace_tlagg.txt
[모델 출력 — perf 결과 아님]
  hitm_before   278
  hitm_after    0
  lines_before  2
  lines_after   0
  drop_pct      100.0
  remaining     []
```

padded 의 HITM 101 은 전부 `total_ops` 읽기(203 행) 다. 표로 보면:

```text
  ---- Cacheline 0  addr 0x102558180  HITM 101  verdict TRUE ----
#   HITM%  L1Hit%  L1Miss%   Offset   recs cpu  label    tids      Code address
   100.0     0.0     0.0      0x0    239   4  TRUE     0,1,2,3   demo_stats.c:203
     0.0   100.0   100.0      0x0    255   4  TRUE     0,1,2,3   demo_stats.c:204
```
(위 표도 모델 출력 — perf 결과 아님.)

`tlagg` 는 HITM 0 이다. 샘플이 없어서 0 이 아니라는 점을 체크한다: `T_PUB` 는 발행 쓰기를 **전부** 남기므로(`tlagg` trace 의 store ≥ 256 건), 0 은 "공유 라인에 쓰는 일이 실제로 없다" 를 뜻한다.

처리량과 나란히 놓으면:

| 수정 | 처리량 (Mac, 위 표) | 모델 HITM | 모델이 말하는 남은 원인 |
|---|---:|---:|---|
| hidden | 98.5 | 278 (2 라인) | MIXED + FALSE |
| padded | 164.8 (1.7×) | 101 (1 라인) | `total_ops` TRUE |
| tlagg | 2291.0 (23×) | 0 | 없음 |

라인 크기 함정 — `CL=64` 패딩을 128 B 관점으로 보면 (M6 처럼 일관성 단위가 128 B 인 칩, 또는 `--double-cl`):

```text
$ python3 -I solution.py report trace_padded64.txt --line 128   (모델 출력 — perf 결과 아님)
      0    0x100a0c100  68.6%      96     96      0     731     367     364   155    209   256     15     0
      1    0x100a0c180  31.4%      44     44      0     522     274     248   170     78   229      0     1
# total HITM (model) = 140, lines with HITM = 2
  ---- Cacheline 0  addr 0x100a0c100  HITM 96  verdict MIXED ----
…
```

`CL=64` 패딩은 64 B 로 보면 HITM 라인 1개(`total_ops`, HITM 76)뿐인데, 128 B 로 보면 슬롯 2 개가 같은 라인에 들어가 **FALSE** 판정이 돌아온다. 패딩 값은 그 칩의 일관성 단위로 정한다 (ARCH-M08).

### 5.7 모델의 한계 (진짜처럼 믿지 말 것)

1. **HITM 수는 trace 의 인터리빙에 달렸다.** 같은 프로그램을 VM 에서 뽑은 trace 는 스레드가 오래 혼자 도는 구간이 길었고(앞 120개 이벤트의 tid 나열: `1211111111…000200002002…`), 모델 HITM 은 hidden 38 대 padded **50** 으로 **padded 가 더 많았다.** vCPU 가 호스트에서 선점되면 trace 순서가 뭉친다. 그래서 커밋한 trace 는 맥에서 뽑았고(맥 trace 는 인터리빙이 고르다: `3303030300332330…`), **숫자가 아니라 "어느 라인의 어느 offset 에 남는가" 를 읽는다.**
2. **시퀀스 번호는 하드웨어 순서가 아니다.** `g_seq` 는 샘플링된 접근마다 `fetch_add` 로 받은 번호이고, 접근 자체와 원자적으로 묶이지 않는다.
3. **샘플이 성기다.** 1/2^14 면 스레드당 2^20 × 7 / 2^14 ≈ 450 이벤트. 연습 2 에서 샘플을 더 줄여도 맨 위 라인이 유지되는지 본다(작은 라인은 빠질 수 있다).
4. 캐시 계층, 축출, 지연, 프리페처, 파이프라인 재정렬, 잠금 접두사 — 전부 없다. "이게 느리다" 는 **처리량 표(§5.3)** 가 말하고, 모델은 "어디가 원인일 수 있다" 만 말한다.

### 5.8 진짜 perf c2c 를 쓸 때 (이 환경에서 실행 불가, 미확인 표시)

**VM 에서 실제로 돌려서 볼 수 있었던 것** — 메모리 이벤트가 없는 `cpu-clock` 샘플을 넣어도 report 는 표 머리글까지는 찍는다 (`sudo perf record -e cpu-clock -d -W --phys-data --sample-cpu`, `perf c2c report --stdio -f -d tot`, 실제 출력에서 발췌. `Unable to parse` 줄은 같은 데이터를 `-d` 없이 돌린 실행의 것이고, 표의 숫자는 전부 비어 있다):

```text
  Unable to parse data source       :        354
…
=================================================
                 c2c details                     
=================================================
  Events                            : cpu-clock
  Cachelines sort on                : Total HITMs
  Cacheline data grouping           : offset,iaddr

=================================================
           Shared Data Cache Line Table          
=================================================
#
#        ----------- Cacheline ----------      Tot  ------- Load Hitm -------    Total    Total    Total  --------- Stores --------  ----- Core Load Hit -----  - LLC Load Hit --  - RMT Load Hit --  --- Load Dram ----
# Index             Address  Node  PA cnt     Hitm    Total  LclHitm  RmtHitm  records    Loads   Stores    L1Hit   L1Miss      N/A       FB       L1       L2    LclHit  LclHitm    RmtHit  RmtHitm       Lcl       Rmt
```

`-d tot` 를 빼면 이 aarch64 VM 에서는 머리글이 `Peer Snoop` 으로 바뀌고 `Cachelines sort on : Peer Snoop` 이 찍혔다. perf 소스 주석이 이유를 말한다:

```text
 * Use the 'tot' as default display type if user doesn't specify it;
 * since Arm64 platform doesn't support HITMs flag, use 'peer' as the
 * default display type.
```

문서가 말하는 Pareto 칸 (perf-c2c.txt, 6.18.55): `HITM - Rmt, Lcl`, `Store Refs - L1 Hit, L1 Miss, N/A`, `Data address - Offset`, `Pid`, `Tid`, `Code address`, `cycles - rmt hitm, lcl hitm, load`, `cpu cnt`, `Symbol`, `Shared Object`, `Source:Line`, `Node`. 기본 묶음은 `offset,iaddr`(위 실제 출력) 이고 `-c tid,iaddr` 로 스레드별로 나눈다.

HITM 이 load 에만 붙는다는 건 소스에 그대로 있다. `util/mem-events.c` 의 `c2c_decode_stats` 는 `if (op & P(OP, LOAD))` 분기 안에서만 `HITM_INC` 를 부르고, store 분기는 `st_l1hit` / `st_l1miss` / `st_na` 만 올린다.

보고서 필터: 소스의 `#define DISPLAY_LINE_LIMIT 0.001` 이고 `filter_display` 는 `val/sum < 0.001` 을 숨긴다. 문서의 `--show-all` 설명은 `HITM % 0.0005 limit` 이라고 적혀 있어 **문서와 코드가 다르다** — 코드를 믿었다.

**다른 하드웨어에서 돌리는 절차 (실행하지 못함, 미확인):**

```bash
# 공통: perf_event_paranoid 를 낮추거나 root 로. -g 는 호출 스택이 필요할 때만.
# Intel 서버 (PEBS load-latency + 정밀 store)
perf c2c record --ldlat=30 -- -a sleep 10        # 문서 예시 형식 (perf record 옵션은 -- 뒤). 다른 터미널에서 워크로드 실행
perf c2c report --stdio -c tid,iaddr --full-symbols > report.txt

# Arm: SPE 가 있는 커널·하드웨어 (Graviton metal 등, 인스턴스 지원 여부는 직접 확인)
ls /sys/bus/event_source/devices/ | grep spe     # arm_spe_0 이 있어야 한다
perf c2c record -- ./hidden 4 16777216           # 필요하면 SPE 샘플 간격 조정 (-c)
perf c2c report --stdio -d peer                  # Arm 기본은 peer
perf c2c report --stdio -d tot                   # HITM 칸은 Arm 에서 비어 있을 수 있다
```

실제 문서의 Arm SPE 요건 (perf-arm-spe.txt, 6.18.55): `The ARM_SPE_PMU config must be set`, `the kernel may need to be booted with page table isolation disabled (kpti=off)`, SPE 인터럽트가 ACPI/DT 에 없으면 `/sys/bus/event_source/devices/` 에 PMU 가 안 보일 수 있다. Graviton metal 인스턴스에서의 실제 동작, Intel `--ldlat` 값의 적정치, Zen 계열 지원 범위는 **이 레슨에서 확인하지 못했다.**

이 lesson 의 trace 도구는 `perf c2c` 의 대체품이 아니라 **표를 읽는 연습 장치** 다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py; echo "exit $?"     # 73/73 passed, exit 0
python3 -I -W error -X dev solution.py     # 경고 0
python3 -I solution.py report trace_hidden.txt
```

## 7. 자주 하는 실수

1. **HITM 표만 보고 "HITM 이 없으니 false sharing 이 없다" 고 결론낸다.** HITM 은 load 사건이다. 스레드들이 store 만 하면(`atomic_fetch_add` 로 쓰는 부분은 load 도 있지만, 순수 저장 루프는) Store L1Miss 만 올라간다. 고치는 법: `Stores L1Miss` 와 HITM 을 함께 본다. `-d peer` 인 Arm 에서는 Peer Snoop 칸을 본다.

2. **패딩만 넣고 끝낸다.** §5.3 표에서 padded 는 1.7 배에 그쳤다. 같은 `total_ops` 를 쓰는 true sharing 이 남아 있었다. Pareto 에서 **같은 offset 을 여러 tid 가 쓰는지** 를 먼저 본다. 고치는 법: 집계를 스레드 로컬로 옮기거나 갱신 주기를 늘린다.

3. **`_Alignas(64)` 를 어디서나 쓴다.** 이 맥의 일관성 단위는 128 B (ARCH-M08). `trace_padded64.txt` 를 `--line 128` 로 보면 FALSE 가 돌아온다. 고치는 법: 패딩 상수를 플랫폼별로 두고 `_Static_assert(offsetof(...) % CL == 0, ...)` 로 못 박는다.

4. **malloc 한 칸을 패딩했다고 믿는다.** 패딩 구조체도 시작 주소가 맞지 않으면 두 라인에 걸친다. 고치는 법: `aligned_alloc` (ARCH-M08 표의 `malloc(24) % 128 = 80` 사례).

5. **`perf c2c` 실패를 권한 문제로 착각한다.** 이 VM 은 `sudo` 도 같은 오류다. 고치는 법: `/sys/bus/event_source/devices/` 를 먼저 본다. 하드웨어 PMU 항목이 없으면 권한으로 해결되지 않는다.

6. **서로 다른 실행의 HITM 절대값을 비교한다.** VM trace 에서 padded 가 hidden 보다 HITM 이 많았다(§5.7). 고치는 법: 처리량은 처리량으로 재고, 표는 "어느 라인·offset 이 남나" 로 읽는다. 진짜 perf 도 샘플링이므로 표본 수를 맞춰 본다.

7. **`classify` 에서 "접근 스레드 전부가 아닌 스레드의 쓰기" 로 조건을 잡는다.** 그러면 `limit` 처럼 모든 스레드가 읽고 모든 스레드가 다른 offset 에 쓰는 흔한 경우가 FALSE 에서 빠진다. 고치는 법: 다른 offset 의 쓰기 스레드 `w` 와 내 접근 스레드 `t` 중 `w != t` 인 쌍이 하나라도 있으면 FALSE 다.

8. **TSan 이 깨끗하니 문제없다고 말한다.** hidden 은 race 가 없다. TSan 은 정확성 도구이고 이 버그는 성능 버그다.

## 8. 면접 Q&A

<details><summary>Q1. HITM 이 정확히 뭔가요? store 는 왜 HITM 칸에 안 잡히나요?</summary>

load 가 자기 캐시에 없어서 찾으러 나갔더니 다른 코어의 캐시에 **Modified** 로 있는 라인을 만난 사건입니다. 그 코어가 데이터를 넘겨 줘야 해서 비싸고, 공유 데이터 경합을 가장 잘 드러냅니다. perf 의 `c2c_decode_stats` 를 보면 HITM 카운터는 load 분기에서만 올라가고, store 는 `L1Hit` / `L1Miss` / `N/A` 로만 분류합니다.

꼬리질문: "그럼 store 끼리만 충돌하면요?" → Load Hitm 은 적고 Store L1Miss 가 많게 나옵니다. HITM 표만 보면 놓칩니다.
</details>

<details><summary>Q2. perf c2c 보고서에서 false sharing 과 true sharing 을 어떻게 구별하나요?</summary>

Pareto 표에서 **같은 offset 을 여러 tid 가 쓰고 있으면 true sharing**, **서로 다른 offset 을 서로 다른 tid 가 쓰고 있으면 false sharing** 입니다. 쓰기가 없는 offset 이 HITM 을 많이 맞으면 읽기 전용 필드가 쓰기 라인에 끼어 있는 것입니다 (제 데모의 `limit`). offset 이 어떤 필드인지는 `pahole` 이나 `offsetof` 로 찾습니다. 다만 도구는 판정을 안 해 주고, 표를 사람이 읽습니다. -c tid,iaddr 로 스레드별로 나눠 봅니다.

꼬리질문: "같은 offset 에 다른 크기로 접근하면요?" → 표는 offset 시작 주소만 보여 주므로 8 B 필드의 중간(offset 4) 과 쓰기가 겹치는 경우 등은 수동으로 확인해야 합니다.
</details>

<details><summary>Q3. 패딩을 넣었는데 1.7배밖에 안 빨라졌습니다. 왜죠?</summary>

제 측정에서 hidden 98.5 Mops, padded 164.8 Mops 였습니다. `USE_TOTAL=0` 으로 공유 RMW 를 빼면 hidden 은 850, padded 는 1904 Mops 로, 진짜 비용은 `total_ops` 의 true sharing 이었습니다. false sharing 만의 효과는 그 비율이 2.2 배 정도입니다. 패딩은 라인을 **나눌 수 있을 때만** 해결합니다. 같은 변수를 모두가 써야 하면 집계 방식을 바꿔야 합니다.

꼬리질문: "그럼 total 을 어떻게 고치나요?" → 스레드별 로컬 카운터를 두고 읽을 때 합산하거나(OS-B05), 쓰는 빈도를 낮춥니다 (주기적 flush). 대가는 읽는 값의 staleness 입니다.
</details>

<details><summary>Q4. Arm 서버(Graviton 등)에서 perf c2c 를 쓰면 뭐가 다른가요?</summary>

SPE 로 샘플링합니다. perf 소스 주석에 "Arm64 platform doesn't support HITMs flag" 라고 되어 있어 기본 표시가 `peer` (Peer Snoop) 입니다. 그래서 표의 정렬 기준이 HITM 이 아니라 peer snoop 이 됩니다. 커널에 `ARM_SPE_PMU` 가 필요하고, 일부 CPU 는 `kpti=off`, SPE 인터럽트가 펌웨어에 기술돼 있어야 합니다(perf-arm-spe 문서). 저는 VM 에서 `cpu-clock` 데이터로 report 를 돌려 표 머리글이 `Peer Snoop` 으로 바뀌는 것까지만 확인했고, 실제 Graviton 에서의 숫자는 돌려 보지 못했습니다.

꼬리질문: "VM 에서 SPE 가 되나요?" → 이 Lima vz VM 은 `perf c2c record` 가 `failed: no PMU supports the memory events` 로 실패합니다. PMU 항목 자체가 안 보입니다.
</details>

<details><summary>Q5. perf stat cache-misses 로는 왜 안 되고 c2c 가 필요한가요?</summary>

카운터는 "몇 번" 만 줍니다. 어느 주소가, 어느 코드가, 어떤 공유 패턴으로 그랬는지는 안 나옵니다. c2c 는 PEBS/SPE/IBS 샘플의 데이터 주소와 data source 로 **라인과 offset, 코드 위치에 귀속** 합니다. 그래서 "이 카운터가 범인" 까지 말할 수 있습니다. 한계도 그 원리에서 나옵니다: 샘플링이라 표본이 적으면 라인이 안 보일 수 있고, 문서에도 SPE 는 통계적이라 모든 연산을 샘플링하지 않는다고 되어 있습니다.

꼬리질문: "그럼 처리량이 느린 건 무엇으로 증명하나요?" → 처리량(또는 사이클) 측정이 먼저입니다. c2c 는 원인을 가리키는 도구이고, 수정 전후 **벤치마크** 가 증명입니다.
</details>

<details><summary>Q6. 일관성 단위가 다른 칩에서는 패딩을 어떻게 정합니까?</summary>

측정합니다 (ARCH-M08: M6 는 코어 쌍에 따라 64 / 128 B, Cortex-M7 은 32 B 라인이지만 코어가 하나면 코어 간 false sharing 은 없고 DMA 와의 라인 공유가 문제). 코드에는 플랫폼 상수로 두고 `_Static_assert` 로 못 박습니다. `perf c2c` 에는 인접 라인 프리페치 때문에 두 라인을 하나처럼 묶어 보는 `--double-cl` 옵션도 있습니다(perf 문서).

꼬리질문: "hardware_destructive_interference_size 는요?" → C++17 표준 상수인데 플랫폼마다 값과 ABI 문제가 있어 그대로 믿기 어렵습니다 (ARCH-M08).
</details>

<details><summary>Q7. 왜 ThreadSanitizer 는 이 문제를 찾지 못합니까? 그럼 무엇으로 찾습니까?</summary>

TSan 은 **data race** 를 찾습니다. 이 코드의 카운터는 `_Atomic` 이고 각자 자기 칸만 쓰므로 race 가 없습니다. 실제로 hidden 변형을 TSan 으로 돌렸을 때 경고가 0 이었습니다. false sharing 은 정확성이 아니라 성능의 문제입니다. 찾는 도구는 `perf c2c` 같은 데이터 주소 샘플링이고, 그게 없을 때는 처리량을 `USE_TOTAL=0` 처럼 요인별로 잘라 재거나, 구조체를 `pahole` 로 보며 쓰기 주체별로 라인을 나눴을 때 달라지는지 봅니다. 드라이버에서는 per-CPU 변수와 `this_cpu_*` 쪽으로 설계합니다.

꼬리질문: "embedded 에서 PMU 샘플링이 없으면요?" → Cortex-A 코어의 일반 PMU 카운터로 L1D refill 수를 재고, 스트레스 벤치에서 레이아웃만 바꿔 비교합니다. 코어가 하나인 MCU 에서는 이 문제 자체가 없고 ISR 과 DMA 쪽 공유 문제만 남습니다.
</details>

## 9. 추가 연습

<details><summary>연습 1. HITM 의 원인 쌍 찾기: load 위치와 마지막 store 위치</summary>

`who_caused(events, line_size)` — `step` 결과가 `HITM` 일 때 (현재 load site, 그 라인을 마지막으로 쓴 site) 쌍을 센다. ping-pong golden 에서 `(site_b ← site_a)` 2, `(site_a ← site_b)` 1. 진짜 `perf c2c` 는 HITM 샘플에 "누가 쓴 건지" 를 표시하지 않으므로 Pareto 에서 서로 다른 offset 의 tid 를 보고 추론한다. 모델이니까 가능한 일이다.
</details>

<details><summary>연습 2. 샘플을 더 줄여도 맨 위 라인은 그대로인가</summary>

`downsample(events, keep_one_in, seed)` — `random.Random(seed)` 로 시드 고정. 커밋된 hidden trace 를 1/2, 1/4 로 줄여도 `line_table` 의 맨 위 라인 주소가 같다는 것을 체크한다. **작은 라인은 빠질 수 있다** 는 것을 표 크기로 확인해 본다. 이게 PEBS/SPE 표본 크기를 키워야 하는 이유다.
</details>

<details><summary>연습 3. NUMA: Local HITM 과 Remote HITM 나누기</summary>

`node_hitm(events, tid_node, line_size)` — HITM 일 때 M 주인의 노드와 요청 스레드의 노드가 같으면 `lcl`, 다르면 `rmt`. 진짜 표의 `LclHitm` / `RmtHitm` 칸의 모델판이다. 멀티 소켓 서버에서는 Rmt 가 훨씬 비싸서 `-d rmt` 로 정렬해 본다.
</details>

<details><summary>연습 4. 코드를 고치기 전에 레이아웃만 바꿔 보기 (what-if)</summary>

`remap(events, mapping)` 은 주소만 바꾼 trace 를 만든다. `hidden_to_padded(base)` 로 `limit` 은 라인 0, `total_ops` 는 자기 라인, slot 은 칸마다 라인 하나로 옮긴다. `hidden` trace 에 적용하면 HITM 라인은 `total_ops` 하나(TRUE)만 남는다. **코드를 고치기 전에 레이아웃 효과를 예측** 하는 방법이다. trace 의 첫 `#` 줄에서 base 주소와 필드 오프셋을 읽는다.
</details>

<details><summary>연습 5. 라인 크기 스캔 (`--double-cl` 의 직접 판)</summary>

`granule_scan(events, sizes)` — 라인 크기마다 HITM 합계. golden 의 `{32:0, 64:0, 128:3, 256:3}` 처럼 **HITM 이 튀어 오르는 크기** 가 경합 범위를 말해 준다. `padded64` 는 128 에서 HITM 이 늘어난다. ARCH-M08 은 같은 질문을 실제 시간으로 답했다.
</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py; echo "exit $?"        # 13/13 passed, exit 0
python3 -I -W error -X dev practice.py
```

## 10. 핵심 정리

- `perf c2c` 는 **PEBS/SPE/IBS 메모리 샘플** 로 데이터 주소·코드 주소·data source 를 모아 라인 경합을 귀속한다. 이 Lima VM 에는 그 PMU 가 없어 `failed: no PMU supports the memory events` 로 끝난다.
- **HITM = load 가 다른 코어의 M 라인을 찾은 사건.** store 는 L1Miss 로 보이므로 두 칸을 함께 읽는다. Arm 기본은 `peer` 표시.
- 읽는 순서: 맨 위 라인 → Pareto → 같은 offset 여러 tid = TRUE, 다른 offset 다른 tid = FALSE, 쓰기 없는 offset 의 HITM = 읽기 전용 희생자 → pahole 로 필드 이름.
- 패딩은 **false sharing 만** 고친다(1.7×). 같은 변수를 공유하는 true sharing 은 스레드 로컬 집계로(23×). 대가는 읽는 값의 staleness.
- 패딩 단위는 칩의 일관성 단위(M6 128 B). `CL=64` 는 128 관점에서 FALSE 가 돌아온다.
- 이 레슨의 표는 모델 출력이다. 원인 위치만 읽고, 효과는 처리량으로 증명한다.
- TSan 은 이 문제를 못 찾는다. race 가 없기 때문이다.

## 참고자료

- `/var/lab/src/linux-6.18.55/tools/perf/Documentation/perf-c2c.txt`, `perf-arm-spe.txt` — 이 VM 에서 읽은 perf 문서 (`man perf-c2c` 는 VM 에 없다)
- `/var/lab/src/linux-6.18.55/tools/perf/builtin-c2c.c` — 표 머리글, `DISPLAY_LINE_LIMIT`, Arm 기본 `peer`
- `/var/lab/src/linux-6.18.55/tools/perf/util/mem-events.c` — `c2c_decode_stats` (HITM 은 load 분기에서만)
- Joe Mario, "C2C - False Sharing Detection in Linux Perf" (2016) — https://joemario.github.io/blog/2016/09/01/c2c-blog/ (perf-c2c.txt 가 가리키는 글, 이 레슨에서 다시 열어 확인하지 않았다)
- 관련 레슨: ARCH-M08 (false sharing 곡선, 128 B), MEM-M04, ARCH-A04 (MESI), OS-B05 (스레드 로컬 집계), LLAT-B06, LPERF-M07
