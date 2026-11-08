---
id: LPERF-B10
time: 70분
updated: 2026-11-08
---
# LPERF-B10 스레드 시간 분해: on-CPU·런큐 대기·sleep

## 학습 목표

- 스레드 하나의 wall time 을 **run(CPU 위에서 실행) + wait(runnable, 런큐에서 CPU 대기) + off(sleep/block)** 세 칸으로 나누고, 각 칸이 `/proc` 의 어느 파일·필드·커널 변수에서 오는지 말할 수 있다.
- `/proc/<pid>/task/<tid>/schedstat` · `status` · `stat` 를 **함정 없이** 파싱한다 (`voluntary` ⊂ `nonvoluntary` 부분 문자열, 공백·괄호가 든 comm, 음수 필드).
- 두 샘플의 차이로 시간 분해표를 만들고 **회계 검증**(스레드마다 run+wait ≤ wall, Σrun ≤ CPU 수 × wall)을 한다. 왜 off 는 "나머지" 로 구하는지, 샘플링 오차가 어디서 오는지 설명한다.
- CPU 수보다 많은 스레드를 띄워 런큐 대기를 **실측** 하고, `stat` 의 `R` 이 "실행 중" 과 "실행 대기" 를 구분하지 못한다는 것을 숫자로 보인다.
- RTOS 의 Running / Ready / Blocked 상태와 1:1 로 대응시켜, 펌웨어에서 "태스크가 왜 마감을 놓치나" 를 같은 회계로 따진다.

## 1. 문제

"CPU 사용률은 40% 밖에 안 되는데 왜 이 스레드가 느리죠?" — 성능 면접과 임베디드 리눅스 디버깅에서 가장 먼저 나오는 질문이다. 답은 **느린 시간이 어디서 새는지 세 칸으로 나눠 보는 것** 이다: CPU 위에서 일하느라(on-CPU), CPU 를 기다리느라(런큐 대기), 무언가(락·I/O·타이머)를 기다리느라(sleep/block). 칸마다 처방이 완전히 다르다 — 첫째는 코드 최적화, 둘째는 CPU·우선순위·스레드 수, 셋째는 off-CPU 분석(LPERF-M06)·락(LPERF-M07)·I/O(LPERF-M09).

펌웨어 쪽 말로 바꾸면 RTOS 의 태스크 상태 그대로다 (RTOS-B02): **Running = run, Ready = wait, Blocked = off**. Rate Monotonic 응답 시간 분석(RTOS-M11)의 "간섭(interference)" — 더 높은 우선순위 태스크 때문에 Ready 로 서 있던 시간 — 이 바로 여기의 wait 다. FreeRTOS 의 `vTaskGetRunTimeStats()` 는 **run 만** 준다. Ready 로 기다린 시간은 `traceMOVED_TASK_TO_READY_STATE` / `traceTASK_SWITCHED_IN` 훅으로 직접 재야 한다(RTOS-A06). 리눅스 커널은 그 일을 이미 해서 `schedstat` 으로 내준다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">스레드 하나의 wall time = run + wait + off  (그리고 각 카운터가 언제 오르나)</text>
  <rect x="20" y="50" width="110" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="130" y="50" width="70" height="34" class="f-warn" stroke="currentColor"/>
  <rect x="200" y="50" width="90" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="290" y="50" width="170" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="460" y="50" width="40" height="34" class="f-warn" stroke="currentColor"/>
  <rect x="500" y="50" width="80" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="580" y="50" width="140" height="34" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="55" y="72">run</text><text x="143" y="72">wait</text><text x="230" y="72">run</text>
    <text x="320" y="72">off: S (futex)</text><text x="465" y="72">wait</text><text x="525" y="72">run</text>
    <text x="600" y="72">off: D (fsync)</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="104">stat state:   R ············ R ·········· R      S ··········· S      R ···· R       D ·········</text>
    <text x="20" y="124">(R 은 run 과 wait 를 구분하지 못한다 — schedstat 이 필요한 이유)</text>
  </g>
  <path d="M130 140 L130 88" stroke="currentColor"/><path d="M200 160 L200 88" stroke="currentColor"/>
  <path d="M290 180 L290 88" stroke="currentColor"/><path d="M500 200 L500 88" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="136" y="152">선점 → nvcsw++  (아직 돌 수 있는데 빼앗김)</text>
    <text x="206" y="172">CPU 받음 → slices++, run_delay += 70</text>
    <text x="296" y="192">스스로 잠듦 → vcsw++, sum_exec_runtime += 90</text>
    <text x="506" y="212">CPU 받음 → slices++, run_delay += 40</text>
  </g>
  <g fill="currentColor" font-size="13">
    <text x="20" y="245">schedstat 1번째 = Σ run (ns)    2번째 = Σ wait (ns)    3번째 = slices (CPU 에 올라간 횟수)</text>
    <text x="20" y="267">off 는 커널이 기본으로 주지 않는다 → off = wall − run − wait  (나머지로 구한다)</text>
    <text x="20" y="289">카운터는 "구간이 끝날 때" 더해진다 → 샘플 순간 진행 중인 구간만큼 오차 (5. 풀이)</text>
  </g>
</svg>
```

| 칸 | 어디서 | 필드 | 단위 | 커널 변수 | 언제 더해지나 |
|---|---|---|---|---|---|
| run | `/proc/<pid>/task/<tid>/schedstat` | 1번째 | ns | `se.sum_exec_runtime` | tick·문맥 교환 때 `update_curr()` |
| wait | 〃 | 2번째 | ns | `sched_info.run_delay` | 런큐에서 CPU 를 **받는 순간** (그동안 기다린 만큼) |
| slices | 〃 | 3번째 | 회 | `sched_info.pcount` | CPU 를 받을 때마다 +1 |
| vcsw / nvcsw | `.../status` | `voluntary_ctxt_switches:` / `nonvoluntary_ctxt_switches:` | 회 | `nvcsw` / `nivcsw` | CPU 를 **내놓을 때** (잠들어서 / 빼앗겨서) |
| state | `.../stat` | 3번째 (`R S D T Z ...`) | 문자 | `__state` | 읽는 순간의 상태 |
| utime·stime | `.../stat` | 14·15번째 | USER_HZ 틱 | `cputime_adjust()` 결과 | 읽을 때 계산 |
| sleep·block | `.../sched` | `sum_sleep_runtime`, `sum_block_runtime` | ms(소수 6자리) | `se.statistics` | 깨어날 때. **`kernel.sched_schedstats=1` 일 때만** |

**이 VM 에서 먼저 확인한 것** (Lima `elinux`: vz, aarch64, 6 vCPU, Ubuntu 26.04.1 LTS, kernel 7.0.0-38-generic, `CONFIG_HZ=1000`, `PREEMPT_DYNAMIC`; 호스트 Apple M6):

```text
$ cat /proc/self/schedstat
261125 0 1
$ cat /proc/sys/kernel/sched_schedstats
0
$ grep -E "SCHED_INFO|SCHEDSTATS|TASK_DELAY" /boot/config-$(uname -r)
CONFIG_TASK_DELAY_ACCT=y
CONFIG_SCHED_INFO=y
CONFIG_SCHEDSTATS=y
$ getconf CLK_TCK
100
```

- `schedstat` 3필드는 `CONFIG_SCHED_INFO` 로 **항상** 쌓인다. 런타임 스위치 `kernel.sched_schedstats=0` 이어도 런큐 대기가 쌓이는 것을 확인했다 (busy loop 8개를 6 vCPU 에 띄우자 `936709328 97281085 49` 처럼 2번째 필드가 0.1 s 가까이 증가).
- `kernel.sched_schedstats=0` 이면 `/proc/<tid>/sched` 에 `sum_sleep_runtime` 등 `se.statistics` 줄이 **아예 없다**. `sudo sysctl -w kernel.sched_schedstats=1` 로 켜면 나타난다 (켠 뒤에 시작한 잠부터 센다). 교차 검증할 때만 잠깐 켜고 다시 0 으로 돌렸다. `stress-ng` 는 `sudo apt-get install -y stress-ng` (0.20.01) 로 설치.

### Part A — /proc 파서 3종

- `parse_schedstat(s, out)` — `"run wait slices\n"`. 숫자 3개 미만, 음수, `12x` 같은 꼬리는 -1. 뒤에 필드가 더 붙으면 앞 3개만 쓴다 (미래 커널 호환).
- `parse_status_ctxsw(buf, out)` — `status` 전체 텍스트에서 `voluntary_ctxt_switches:` 와 `nonvoluntary_ctxt_switches:` 를 **줄 맨 앞에서** 찾는다 (`find_line_key`). 순서에 기대지 않는다.
- `parse_stat(buf, out)` — `"tid (comm) state ppid ... utime stime ..."`. comm 은 `(` 다음부터 **마지막** `)` 앞까지 (comm 에 `) ` 가 들어갈 수 있다), 15자로 자른다. 14·15번째 필드가 utime·stime.

### Part B — 두 샘플 → 시간 분해, 판정

- `account(a, b, out)` — 같은 스레드의 앞·뒤 샘플로 wall/run/wait/off/slices/vcsw/nvcsw. `run + wait > wall` 이면 off = 0, 넘친 양은 `skew_ns`. 시간 역행·카운터 감소(=다른 스레드, tid 재사용)는 -1.
- `state_hist_add(h, c)` — 주기적으로 읽은 state 문자를 R/S/D/기타로 센다.
- `classify(b, h)` — 가장 큰 칸으로 판정 (`ON-CPU` / `RUNQ-WAIT` / off 일 때 D 표본이 S 보다 많으면 `BLOCK(D)` 아니면 `SLEEP(S)`). 동률은 run > wait > off.

### Part C — 4코어 카메라 게이트웨이: 누가 마감을 놓치게 하나

i.MX8M 급 4코어 리눅스 게이트웨이. "cam-capture 가 33 ms 프레임 마감을 놓친다" 는 버그 리포트. 1초 창에서 7개 스레드(캡처, 인코더 4, eMMC 로거, MQTT)의 `/proc` 텍스트를 만들어 파서부터 판정까지 통과시키고:

- `sample_from_text(...)` 로 세 텍스트를 샘플 하나로, `read_task_sample(pid, tid, out)` 로 **진짜** `/proc/<pid>/task/<tid>/` 세 파일을 읽어 샘플 하나로.
- `verify_wall(b, n, ncpu, tol, out)` — 스레드별 `skew > tol × wall` 개수 + `Σrun > ncpu × wall × (1+tol)` 이면 1 → 위반 수. 덤으로 `util = Σrun/(ncpu·wall)`, `avg_runnable = Σ(run+wait)/wall` (스레드판 load average), `oversub = avg_runnable > ncpu`.
- 라이브 테스트: 30 ms CPU → 30 ms `nanosleep` → 조건 변수에서 주차하는 스레드를 읽어 run ≥ 29 ms, off ≥ 30 ms, vcsw ≥ 2, 주차 중 state == `S` 를 확인.
- 그리고 `demo_workload.c`(CPU·락·I/O·주기 스레드 혼합) 를 CPU 수보다 많이 띄우고 `demo_ttime.c`(이 함수들을 `#include` 한 도구) 로 실제 분해표를 뽑는다.

## 2. Problem (English)

> You are handed a Linux box where "thread X is slow". Write a tool that splits each thread's wall-clock time over a
> window into **on-CPU time**, **run-queue wait** (runnable but not running) and **off-CPU time** (sleeping/blocked),
> using only procfs, and verify that the accounting adds up.
>
> ```c
> typedef struct { uint64_t run_ns, wait_ns, slices; } schedstat_t;
> typedef struct { uint64_t vcsw, nvcsw; } ctxsw_t;
> typedef struct { int pid; char comm[16]; char state; uint64_t utime, stime; } taskstat_t;
> typedef struct { uint64_t t_ns; schedstat_t ss; ctxsw_t cs; taskstat_t st; } sample_t;
> typedef struct { uint64_t wall_ns, run_ns, wait_ns, off_ns, skew_ns, slices, vcsw, nvcsw; } breakdown_t;
>
> int parse_schedstat(const char *s, schedstat_t *out);          // "936709328 97281085 49\n"
> int parse_status_ctxsw(const char *status, ctxsw_t *out);      // whole /proc/.../status text
> int parse_stat(const char *stat, taskstat_t *out);             // comm may contain ") " and spaces
> int account(const sample_t *a, const sample_t *b, breakdown_t *out);   // off = wall - run - wait (clamped)
> tt_class_t classify(const breakdown_t *b, const state_hist_t *h);
> int read_task_sample(int pid, int tid, sample_t *out);
> int verify_wall(const breakdown_t *b, int n, int ncpu, double tol, sys_check_t *out);
> ```
>
> Constraints: no floating point in the parsers; parsers return -1 on malformed input instead of guessing; a counter
> that goes backwards means the tid was reused (reject). Example: two samples 1 s apart with run +300 ms and
> wait +200 ms give `off = 500 ms`. Follow-ups: why can't `/proc/<tid>/stat`'s state alone tell you about run-queue
> wait? Where does the residual error come from? How would you get the same numbers on an RTOS?

## 3. 시작 코드

<!-- code: starter.c -->

이 레슨은 **리눅스 전용**(`/* ENV: linux */`, 맥 검증기는 건너뛴다)이라 Lima VM 안에서 빌드·실행한다. 맥 홈이 VM 에 **읽기 전용** 으로 마운트돼 있어서 VM 의 `/tmp/lperf_b10/` 로 복사해 쓴다.

```bash
limactl start elinux                 # 꺼져 있으면 (~1분)
limactl shell elinux                 # 이후 VM 안
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/LPERF/LPERF-B10
mkdir -p /tmp/lperf_b10 && cp $L/*.c /tmp/lperf_b10/ && cd /tmp/lperf_b10
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st
```

`gettid()` · `pthread_setname_np()` 은 리눅스 전용 API 라 `-std=c11` 에서는 `#define _GNU_SOURCE` 를 **모든 include 앞에** 둬야 선언이 보인다 (맥 레슨의 `_DARWIN_C_SOURCE` 자리). 시작 코드 실행 결과 (끝부분, 실제 출력):

```text
FAIL  parse_schedstat("936709328 97281085 49\n", &ss) == 0 && ss.run_ns == 936709328u && ss.wait_ns == 97281085u && ss.slices == 49u  (line 282)
...
FAIL  lb.vcsw >= 2u && lb.slices >= 2u  (line 412)
PASS  lb.skew_ns > 0 || lb.run_ns + lb.wait_ns + lb.off_ns == lb.wall_ns
  live: wall 0.0 ms = run 0.0 + wait 0.000 + off 0.0  (slices 0, vcsw 0, nvcsw 0)
15/43 passed
```

PASS 15개는 "잘못된 입력이면 -1" 같은 테스트가 스텁의 -1 에 우연히 맞은 것이다. 라이브 테스트의 대기는 모두 타임아웃이 있어 멈추지 않는다.

## 4. 힌트

<details>
<summary>힌트 1 — status: "voluntary" 는 "nonvoluntary" 안에 들어 있다</summary>

`strstr(buf, "voluntary_ctxt_switches:")` 는 `non` 뒤의 같은 글자에도 걸린다. 파일에서 voluntary 줄이 먼저 나오니 우연히 맞을 뿐이다 (테스트는 순서를 뒤집어 놓았다). **줄 맨 앞** 에서만 비교하라: 줄 시작 포인터에서 `strncmp`, 아니면 다음 `'\n'` 뒤로.

</details>

<details>
<summary>힌트 2 — stat: sscanf("%d (%s) %c") 는 왜 안 되나</summary>

`%s` 는 공백에서 멈추고 `)` 를 먹는다. comm 은 사용자가 정하는 15바이트라 `my) prog S x` 같은 이름도 된다. 커널이 바꿀 수 없는 것은 **마지막 `)`** 의 위치뿐 — `strrchr(buf, ')')`. 그 뒤가 state, 그다음부터는 공백 구분 필드인데 5번째 `tpgid` 는 `-1` 일 수 있으니 숫자 파서 말고 "필드 건너뛰기" 로 4~13번째를 넘긴다.

</details>

<details>
<summary>힌트 3 — account: off 는 직접 잴 수 없다</summary>

schedstat 은 run 과 wait 만 준다. wall 은 두 샘플을 읽은 **단조 시계** 차이(`CLOCK_MONOTONIC`, 샘플마다 `t_ns`). off = wall − run − wait 인데, 각 카운터는 "구간이 끝날 때" 더해져서 샘플 순간에 진행 중인 구간만큼 어긋난다 → 음수가 될 수 있다. `uint64_t` 로 빼면 거대한 수가 되니 먼저 비교하고, 넘친 양은 `skew_ns` 로 남겨 검증에 쓴다.

</details>

<details>
<summary>힌트 4 — verify_wall: 무엇이 "물리적으로 불가능" 한가</summary>

① 한 스레드는 동시에 두 CPU 에서 돌 수 없다 → 스레드별 run ≤ wall, 그리고 run + wait ≤ wall (wait 중엔 안 돈다). ② 모든 스레드를 합쳐도 CPU 개수 이상 돌 수 없다 → Σrun ≤ ncpu × wall. 반대로 Σ(run+wait) 는 ncpu × wall 을 넘을 수 있다 — 그게 오버서브스크립션이다. 판정에는 허용 오차 `tol` 을 곱해서 쓴다.

</details>

<details>
<summary>힌트 5 — 라이브 테스트가 어떤 숫자를 보장하나</summary>

워커는 `CLOCK_THREAD_CPUTIME_ID` 로 자기 CPU 시간이 30 ms 가 될 때까지 돈다. 이 시계는 같은 `sum_exec_runtime` 에서 나오므로 schedstat run 증가는 **항상** 30 ms 이상이다 (벽시계로 30 ms 돌면 선점 때문에 보장 못 한다). `nanosleep(30 ms)` 는 최소 30 ms 를 off 로 보낸다. 둘 다 스스로 잠드는 것이라 vcsw 가 2 이상 오른다.

</details>

## 5. 풀이

### Part A — 파서: 형식이 "거의" 단순한 세 파일

**schedstat** 은 숫자 3개뿐이지만 "-1 을 받아 주는 `strtoull`" 이 함정이다 (`strtoull("-5")` 는 에러 없이 2⁶⁴−5 를 준다). 그래서 `next_u64` 는 첫 글자가 숫자인지 먼저 본다. 숫자 3개 뒤에는 공백·개행·끝만 허용하고, 4번째 숫자가 있으면 무시한다. 커널 문서(sched-stats)가 "필드는 3개" 라고 하지만, 파서는 **모르는 것을 더하는 쪽으로는 관대하게, 깨진 것에는 엄격하게** 쓰는 게 도구의 수명을 늘린다.

**status** 는 `키:\t값` 줄 50여 개다. 두 키가 서로의 접미사라는 게 핵심 함정이다:

| 방법 | `nonvoluntary` 줄이 먼저 오는 테스트 입력에서 |
|---|---|
| `strstr(buf, "voluntary_ctxt_switches:")` | `non` 뒤에서 걸려 vcsw = 17 (nvcsw 값) → FAIL |
| `find_line_key` (줄 맨 앞 `strncmp`) | vcsw = 4242, nvcsw = 17 → PASS |

`find_line_key("Name:\tx\nPid:\t5\n", "id:") == NULL` 테스트도 같은 이유 — `Pid:` 의 꼬리에 걸리면 안 된다.

**stat** 은 필드 번호가 `proc(5)` 기준 1부터다:

| 번호 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10–13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 이름 | pid(tid) | (comm) | state | ppid | pgrp | session | tty_nr | tpgid | flags | minflt cminflt majflt cmajflt | utime | stime |
| 주의 | | 공백·`)` 가능 | 한 글자 | | | | | **-1 가능** | | | 틱 | 틱 |

comm 을 `(` 다음부터 마지막 `)` 앞까지 자르면 `"77 (my) prog S x) D 1 ..."` 의 comm 은 `my) prog S x`, state 는 `D` 가 된다. comm 은 커널에서 `TASK_COMM_LEN = 16` (NUL 포함)이라 `pthread_setname_np` 도 15자를 넘기면 `ERANGE` 로 실패한다. 파서는 더 길게 와도 15자로 자른다.

### Part B — 회계: 왜 off 는 나머지인가

커널은 run 과 wait 를 **항상** 센다(스케줄러가 어차피 하는 일). sleep 시간은 `schedstats` 를 켜야만 센다(깨울 때마다 비용이 든다). 그래서 기본 도구는 이렇게 간다:

```text
wall = t_b − t_a                                  (샘플을 읽은 CLOCK_MONOTONIC 차)
run  = schedstat[0]_b − schedstat[0]_a
wait = schedstat[1]_b − schedstat[1]_a
off  = wall − run − wait     (음수면 0, 넘친 만큼 skew)
```

**오차는 어디서 오나.** 표의 "언제 더해지나" 열이 답이다.

| 샘플 순간 스레드가 | 아직 안 더해진 것 | 결과 |
|---|---|---|
| 달리는 중 | 마지막 tick 이후 실행분 (최대 1 tick = 1 ms, `CONFIG_HZ=1000`) | run 이 조금 작고 off 가 조금 큼 |
| 런큐에서 기다리는 중 | 이번 대기 전체 (`run_delay` 는 CPU 를 받을 때 더해짐) | wait 가 작고 off 가 큼 — **수 ms** 가능 |
| 자는 중 | (off 는 나머지라 영향 없음) | `sum_sleep_runtime` 과 비교할 때만 어긋남 |

또 세 파일을 차례로 읽는 몇 µs 와, 도구 자신이 런큐에서 기다리는 시간도 끼어든다. 그래서 `wall` 은 프로세스 공통이 아니라 **스레드마다 자기 샘플 시각** 으로 잰다 — 실측에서 오버서브스크립션 때 같은 프로세스 안 스레드들의 wall 이 4014.5 ms 와 4066.6 ms 로 52 ms 나 달랐다 (도구가 21개 스레드를 읽다가 선점당했다).

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">상태 전이와 카운터: 어떤 화살표가 무엇을 올리나</text>
  <rect x="290" y="40" width="160" height="50" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="318" y="62" fill="currentColor">Running (R)</text>
  <text x="312" y="80" fill="currentColor" font-size="12">run 이 쌓인다</text>
  <rect x="40" y="170" width="180" height="50" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="54" y="192" fill="currentColor">Runnable (R, 런큐)</text>
  <text x="66" y="210" fill="currentColor" font-size="12">wait 가 쌓인다</text>
  <rect x="520" y="170" width="190" height="50" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="540" y="192" fill="currentColor">Sleep S / Block D</text>
  <text x="550" y="210" fill="currentColor" font-size="12">off 가 쌓인다</text>
  <path d="M290 80 L170 170" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M180 158 L170 170 L185 166" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="90" y="118" fill="currentColor" font-size="12">선점(preempt): nvcsw++</text>
  <path d="M200 170 L310 90" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M296 94 L310 90 L304 103" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="236" y="150" fill="currentColor" font-size="12">CPU 받음: slices++, run_delay += 대기</text>
  <path d="M450 80 L580 170" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M566 166 L580 170 L574 156" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="510" y="112" fill="currentColor" font-size="12">스스로 잠듦: vcsw++</text>
  <text x="510" y="128" fill="currentColor" font-size="12">(futex·read·fsync·nanosleep)</text>
  <path d="M520 205 L220 205" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M232 199 L220 205 L232 211" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="240" y="235" fill="currentColor" font-size="12">깨움: 런큐로 (sum_sleep_runtime 은 여기서 +)</text>
  <text x="10" y="268" fill="currentColor" font-size="12">RTOS: Running / Ready / Blocked 와 같은 그림. stat 의 R 은 위 두 상자를 합친 것이다.</text>
</svg>
```

**판정 규칙** 은 일부러 단순하다: 세 칸 중 가장 큰 것. off 가 가장 크면 히스토그램으로 S(interruptible: futex, 소켓, 타이머)와 D(uninterruptible: 대부분 디스크 I/O 완료 대기)를 가른다. 동률이면 run > wait > off 순 — "CPU 를 쓰고 있다" 가 가장 확실한 증거라서.

### Part C — 고정 장면과 검증 수식

| 스레드 | run | wait | off | state 표본 | 판정 |
|---|---|---|---|---|---|
| cam-capture | 120 | **610** | 270 | R 15 / S 5 | RUNQ-WAIT |
| enc-0..3 (각) | 900 | 100 | 0 | R 20 | ON-CPU |
| logger | 15 | 5 | 980 | D 19 / S 1 | BLOCK(D) |
| mqtt | 5 | 1 | 994 | S 20 | SLEEP(S) |

`util = Σrun / (4 × 1000) = (120 + 3600 + 15 + 5) / 4000 = 0.935`, `avg_runnable = Σ(run+wait) / 1000 = (730 + 4000 + 20 + 6) / 1000 = 4.756 > 4` → 오버서브스크립션. 결론: cam-capture 는 일이 많아서가 아니라(run 12%) **CPU 를 못 받아서**(wait 61%) 마감을 놓친다. 처방은 캡처 스레드를 `SCHED_FIFO` 로 올리거나, 인코더를 `taskset`/cgroup `cpuset` 으로 3코어에 묶어 한 코어를 비워 주는 것이다 — 코드 최적화가 아니다.

검증은 두 개의 불가능을 찾는다: 스레드 하나가 wall 보다 많이 run+wait 했거나(tol 1% 초과 skew), 프로세스가 CPU 수보다 많이 돌았거나. 픽스처의 logger 를 run 1040 ms 로 망가뜨리면 둘 다 걸리고(`bad_threads == 1`, util 1.19), 같은 정상 데이터라도 "3코어" 라고 하면 Σrun 3.74 s > 3 s 라서 걸린다 — **도구가 CPU 수를 잘못 알면 회계가 깨진다** (컨테이너의 cpu quota, 오프라인 CPU 주의).

라이브 테스트 출력 (실제, 5번 이상 돌려 모두 43/43):

```text
  live: wall 82.6 ms = run 30.1 + wait 0.000 + off 52.5  (slices 2, vcsw 2, nvcsw 0)
  live: wall 83.7 ms = run 30.1 + wait 0.015 + off 53.6  (slices 3, vcsw 2, nvcsw 1)
```

부하가 없으면 wait 는 수 µs 이하다. `run_delay` 는 런큐에 **들어간 순간부터** 세기 때문에, 깨우는 CPU 가 원격 CPU 에 IPI 를 보내는 시간처럼 enqueue 이전의 지연은 들어가지 않는다. 같은 테스트를 `stress-ng --cpu 12` 아래에서 돌리면 (실제):

```text
  live: wall 136.8 ms = run 30.0 + wait 62.803 + off 44.0  (slices 14, vcsw 2, nvcsw 12)
  live: wall 102.1 ms = run 30.0 + wait 31.872 + off 40.2  (slices 15, vcsw 3, nvcsw 12)
```

run 은 그대로 30 ms 인데 wall 이 50 ms 넘게 늘었고, 그 차이는 거의 전부 wait 다. 30 ms 를 14번 나눠서 받았다 (nvcsw 12 = 12번 빼앗김). CHECK 는 이 숫자가 아니라 run ≥ 29 ms, off ≥ 30 ms, vcsw ≥ 2 만 보므로 부하가 있어도 5/5 통과했다.

### 실측: 도구로 진짜 프로세스를 분해하기

```bash
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_workload.c -o wl
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_ttime.c -o ttime    # solution.c 를 #include
./wl 4 4 2 2 6 &  sleep 0.5;  ./ttime $(pgrep -x wl) 4 20             # 4 s 창, 20 ms 마다 state 표본
```

<!-- code: demo_workload.c -->

<!-- code: demo_ttime.c -->

**실행 1 — 오버서브스크립션 없음** (cpu 4 + lock 4 + io 2 + tick 2, 6 vCPU, 실제 출력):

```text
pid 11558, 13 threads, window 4 s, 197 state samples/thread, nproc 6, schedstats off
tid     comm             wall_ms   run_ms  wait_ms   off_ms |  run% wait%  off% | verdict
11558   wl                4018.8      0.0      0.0   4018.8 |   0.0   0.0 100.0 | SLEEP(S)
11560   cpu-0             4018.8   4010.0      8.9      0.0 |  99.8   0.2   0.0 | ON-CPU
11564   lock-0            4018.8    962.5    230.3   2826.0 |  24.0   5.7  70.3 | SLEEP(S)
11568   io-0              4018.8    216.4    333.0   3469.5 |   5.4   8.3  86.3 | BLOCK(D)
11570   tick-0            4018.8    206.0    122.7   3690.1 |   5.1   3.1  91.8 | SLEEP(S)
...
sum run 20639.2 ms, sum wait 1767.6 ms | util 0.856 of 6 CPUs | avg runnable 5.58 (fits) | max skew 0.176 ms | violations 0
```

**실행 2 — 오버서브스크립션** (cpu 12 + lock 4 + io 2 + tick 2, `kernel.sched_schedstats=1` 로 켜고 실행, 실제 출력):

```text
pid 11603, 21 threads, window 4 s, 189 state samples/thread, nproc 6, schedstats on
tid     comm             wall_ms   run_ms  wait_ms   off_ms |  run% wait%  off% | verdict
11604   cpu-0             4014.5   1749.3   2265.8      0.0 |  43.6  56.4   0.0 | RUNQ-WAIT
11611   cpu-7             4064.8   3070.5    995.2      0.0 |  75.5  24.5   0.0 | ON-CPU
11616   lock-0            4064.9    453.6    896.7   2714.6 |  11.2  22.1  66.8 | SLEEP(S)
11620   io-0              4066.5    146.5    855.4   3064.5 |   3.6  21.0  75.4 | BLOCK(D)
11622   tick-0            4066.6    271.7    760.3   3034.7 |   6.7  18.7  74.6 | SLEEP(S)
...
sum run 24077.4 ms, sum wait 35122.5 ms | util 0.987 of 6 CPUs | avg runnable 14.56 (OVERSUBSCRIBED) | max skew 8.085 ms | violations 0

comm               R% (r+w)% |   S%   D%  oth | slices   vcsw  nvcsw |  tick_ms   run_ms |   off_ms   dsleep   dblock
cpu-0           100.0  100.0 |    0    0    0 |    592      0    593 |   1750.0   1749.3 |      0.0      0.0      0.0
cpu-1           100.0   99.9 |    0    0    0 |    564      0    565 |   2290.0   2289.2 |      3.6      0.0      0.0
lock-0           33.3   33.2 |   67    0    0 |    673    223    450 |    450.0    453.6 |   2714.6   2654.1      0.0
io-0             28.6   24.6 |    0   71    0 |  10905  10890     16 |    140.0    146.5 |   3064.5   3053.4   3053.4
io-1             36.0   31.4 |    0   64    0 |   7767   7749     18 |    130.0    128.7 |   2788.4   2788.0   2788.0
tick-0           24.9   25.4 |   75    0    0 |    467    407     60 |    270.0    271.7 |   3034.7   2963.8      0.0
```

```svg
<svg viewBox="0 0 740 345" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">실측 분해 (% of wall): 실행 1 (avg runnable 5.58 / 6) vs 실행 2 (14.56 / 6)</text>
  <rect x="160" y="30" width="14" height="12" class="f-acc" stroke="currentColor"/><text x="178" y="41" fill="currentColor">run</text>
  <rect x="220" y="30" width="14" height="12" class="f-warn" stroke="currentColor"/><text x="238" y="41" fill="currentColor">wait</text>
  <rect x="290" y="30" width="14" height="12" class="f-mut" stroke="currentColor"/><text x="308" y="41" fill="currentColor">off</text>
  <g fill="currentColor">
    <text x="10" y="70">cpu-0 · 실행1</text><text x="10" y="96">cpu-0 · 실행2</text>
    <text x="10" y="132">lock-0 · 실행1</text><text x="10" y="158">lock-0 · 실행2</text>
    <text x="10" y="194">io-0 · 실행1</text><text x="10" y="220">io-0 · 실행2</text>
    <text x="10" y="256">tick-0 · 실행1</text><text x="10" y="282">tick-0 · 실행2</text>
  </g>
  <rect x="140" y="58" width="499" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="639" y="58" width="1" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="140" y="84" width="218" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="358" y="84" width="282" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="140" y="120" width="120" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="260" y="120" width="28" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="288" y="120" width="352" height="18" class="f-mut" stroke="currentColor"/>
  <rect x="140" y="146" width="56" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="196" y="146" width="110" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="306" y="146" width="334" height="18" class="f-mut" stroke="currentColor"/>
  <rect x="140" y="182" width="27" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="167" y="182" width="42" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="209" y="182" width="431" height="18" class="f-mut" stroke="currentColor"/>
  <rect x="140" y="208" width="18" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="158" y="208" width="105" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="263" y="208" width="377" height="18" class="f-mut" stroke="currentColor"/>
  <rect x="140" y="244" width="26" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="166" y="244" width="15" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="181" y="244" width="459" height="18" class="f-mut" stroke="currentColor"/>
  <rect x="140" y="270" width="33" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="173" y="270" width="94" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="267" y="270" width="373" height="18" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="11">
    <text x="648" y="71">w 0.2</text><text x="648" y="97">w 56.4</text>
    <text x="648" y="133">w 5.7</text><text x="648" y="159">w 22.1</text>
    <text x="648" y="195">w 8.3</text><text x="648" y="221">w 21.0</text>
    <text x="648" y="257">w 3.1</text><text x="648" y="283">w 18.7</text>
  </g>
  <text x="10" y="315" fill="currentColor">막대 폭 = demo_ttime 표의 run%·wait%·off% (500 px = 100 %), 오른쪽 숫자 = wait %.</text>
  <text x="10" y="335" fill="currentColor">실행 2 에서는 CPU 바운드뿐 아니라 모든 종류의 스레드가 wait 를 떠안는다.</text>
</svg>
```

**표에서 읽어야 할 것:**

1. **오버서브스크립션은 모든 스레드의 wait 를 키운다.** CPU 바운드만이 아니다. tick-0 은 10 ms 마다 0.5 ms 일하는 가벼운 스레드인데 wait 가 3.1% → 18.7%. 슬라이스당 평균 대기 = 760.3 ms / 467 = **1.63 ms** — 깨어나서 실제로 돌기까지 매번 1.6 ms 를 서 있었다 (연습 3). 주기 2 ms 짜리 제어 루프였다면 마감을 놓친다. 이 분포를 히스토그램으로 보는 게 runqlat(LPERF-M05)이다.
2. **`R` 표본 비율 ≈ (run+wait) 비율, run 비율이 아니다.** cpu-0 은 R 100% 인데 실제로는 43.6% 만 돌았다. `top`/`ps` 의 state 만 보면 "계속 돌고 있다" 로 오판한다. lock-0 의 R 33.3% ↔ (r+w) 33.2%, tick-0 24.9% ↔ 25.4% 로 189개 표본 수준에서 잘 맞는다.
3. **락 대기는 S, 디스크 완료 대기는 D.** 뮤텍스 경합 스레드(glibc 뮤텍스 → `futex` 대기)는 S 와 vcsw 로 보이고 `sum_block_runtime` 은 0. io-0 은 D 71% 이고 `dblock == dsleep == 3053.4 ms`. 즉 **`sum_sleep_runtime` 은 D 시간을 포함** 하고 `sum_block_runtime` 은 그중 D 부분이다 — off 와 비교할 상대는 dsleep 하나다.
4. **off(나머지) ≈ dsleep(커널이 직접 센 값)**: lock-0 2714.6 vs 2654.1, io-1 2788.4 vs 2788.0, tick-0 3034.7 vs 2963.8. 차이(최대 수십 ms)는 창 끝에 **진행 중이던 잠** 이다 (깨어날 때 더해지므로). cpu-1 의 off 3.6 ms 는 dsleep 0 이므로 진짜 잠이 아니라 샘플링 오차다 — 창 끝에 런큐에서 기다리던 중이라 그 대기분이 아직 wait 에 안 들어갔다.
5. **틱 기반 CPU 시간도 맞는다**: `stat` 의 utime+stime 변화량(× 10 ms) vs run — 1750.0 vs 1749.3, 450.0 vs 453.6 등 ±10 ms 이내 (연습 2). 리눅스는 `/proc/<pid>/stat` 을 만들 때 utime·stime 을 `sum_exec_runtime` 에 맞춰 비례 조정(`cputime_adjust`)한 뒤 USER_HZ(100) 틱으로 자르기 때문이다. 짧은 창(< 1 s)에서는 10 ms 단위가 너무 거칠다 → schedstat 을 쓴다.
6. **slices ≈ vcsw + nvcsw (±1)**: cpu-0 592 vs 0+593. 슬라이스는 "올라간 횟수", 스위치는 "내려온 횟수" 라 창 끝에서 달리는 중이면 1 차이. cpu-0 의 run/slices = 1749.3/592 ≈ 2.95 ms — 이 VM 의 EEVDF 기본 slice(`/proc/<pid>/sched` 의 `se.slice : 2100000` = 2.1 ms)와 같은 크기다.
7. **검증**: Σrun 24077 ms ≤ 6 × 4066 ms = 24400 ms (util 0.987, 6 vCPU 가 꽉 참). 스레드별 최대 skew 8.1 ms (0.2%) → violations 0.

**실행 3 — 시끄러운 이웃** (`stress-ng --cpu 8` 을 따로 돌리고 실행 1 과 같은 `wl 4 4 2 2`, 실제 출력 일부):

```text
11665   cpu-0             4003.6   1399.7   2615.3      0.0 |  35.0  65.3   0.0 | RUNQ-WAIT
11674   io-1              4003.5    100.8   1936.7   1965.9 |   2.5  48.4  49.1 | BLOCK(D)
sum run 9176.6 ms, sum wait 16833.1 ms | util 0.382 of 6 CPUs | avg runnable 6.50 (OVERSUBSCRIBED) | max skew 11.372 ms | violations 0
```

이 프로세스의 util 은 0.382 뿐인데 cpu 스레드는 65% 를 기다렸다. **CPU 를 먹은 건 다른 프로세스** 다. 프로세스 하나만 보는 도구는 "왜" 를 말할 수 없다 → 시스템 전체 `/proc/loadavg`(이때 `3.31 0.98 0.34 16/252`, 16 = runnable 수)나 PSI `cpu some`(LPERF-B02)을 같이 본다.

**VM 이라서 생기는 일**: vz VM 안의 "CPU" 는 호스트 스레드다. 호스트가 vCPU 를 잠시 빼앗으면 게스트 커널은 (paravirt steal time 이 없으면) 그 시간을 그때 달리던 스레드의 run 으로 센다. 그래서 수치는 경향 비교로 쓰고, 절대값은 실기판에서 다시 잰다.

### 새니타이저

| 대상 | ASan+UBSan (gcc) | TSan |
|---|---|---|
| solution.c | 43/43, 보고 없음 | **gcc: SIGILL(exit 132)**, clang 21: 43/43 보고 없음 |
| practice.c | 24/24, 보고 없음 | gcc·clang 모두 24/24 |
| demo_workload.c / demo_ttime.c | ttime: 실제 프로세스 1 s 샘플링, 보고 없음 | workload: gcc·clang 모두 보고 없음 |

gcc TSan 의 SIGILL 은 이 코드와 무관하다: 스레드 둘이 `pthread_cond_wait` 하나만 하는 짧은 프로그램도 gcc `-fsanitize=thread` 로는 같은 132 로 죽고, 같은 파일을 clang 으로 빌드하면 정상이다. 이 VM 에는 gcc 15.2.0 과 `libtsan2 16-20260322` (gcc 16 스냅숏 런타임)이 섞여 있어 그 불일치가 원인으로 **추정** 된다 (확인은 못 함). 그래서 TSan 은 `clang -std=c11 -g -O1 -fsanitize=thread -pthread` 로 돌렸다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so        # 43/43 passed (8번 연속)
gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_asan && ./so_asan
clang -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan
```

실제 출력 (Part C 부분):

```text
  fx cam-capture run  120 wait  610 off  270 ms -> RUNQ-WAIT
  fx enc-0       run  900 wait  100 off    0 ms -> ON-CPU
  ...  (enc-1..3 같음)
  fx logger      run   15 wait    5 off  980 ms -> BLOCK(D)
  fx mqtt        run    5 wait    1 off  994 ms -> SLEEP(S)
PASS  fx_ok
...
  live: wall 72.6 ms = run 30.0 + wait 0.000 + off 42.5  (slices 2, vcsw 2, nvcsw 0)
43/43 passed
```

`solution.c` 맨 아래 하네스는 `#ifndef TTIME_LIB_ONLY` 로 감싸 두었다. `demo_ttime.c` 는 `#define TTIME_LIB_ONLY` 후 `#include "solution.c"` 해서 **테스트한 바로 그 함수** 로 실측한다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `strstr(status, "voluntary_ctxt_switches:")` | `nonvoluntary_...` 안에도 같은 문자열이 있다. 지금 커널의 줄 순서 덕에 우연히 맞을 뿐 | 줄 맨 앞 비교 `find_line_key` |
| `sscanf(stat, "%d (%15[^)]) %c", ...)` | comm 에 `)` 나 공백이 있으면 state 를 엉뚱하게 읽는다 (`my) prog S x` → state `p`) | `strrchr(buf, ')')` 뒤에서 state |
| stat 필드를 모두 `strtoull` 로 | 8번째 `tpgid` 가 `-1` → `strtoull` 은 에러 없이 2⁶⁴−1 | 필요 없는 필드는 `skip_field` |
| `state == 'R'` 이면 "CPU 쓰는 중" | R = running **또는** runnable. 실행 2 cpu-0: R 100% 인데 run 43.6% | schedstat 1·2번째 필드로 구분 |
| off = S·D 표본 비율 × wall | 20 ms 표본은 짧은 잠(µs~ms)을 놓치고 바쁜 순간과 상관될 수 있다 | off = wall − run − wait, 표본은 S/D 구분용 |
| `uint64_t off = wall - run - wait;` | 샘플링 오차로 run+wait > wall 이면 2⁶⁴ 근처 거대한 수 | 먼저 비교, 넘친 양은 `skew_ns` |
| 모든 스레드에 같은 wall (도구 시작~끝) | 도구 자신이 선점당하면 스레드마다 읽은 시각이 수십 ms 다르다 (실측 52 ms) | 샘플마다 `t_ns`, 스레드별 wall |
| utime+stime(틱) 으로 run 계산 | 10 ms 단위 — 100 ms 창이면 ±20% 오차 | schedstat run (ns) |
| `/proc/<tid>/sched` 의 `sum_sleep_runtime` 을 믿고 씀 | `kernel.sched_schedstats=0` 이면 줄이 없다. 켜기 전부터 자던 스레드는 이번 잠이 안 들어감 | 기본은 나머지 계산, sched 는 교차 검증용 |
| `sum_sleep_runtime + sum_block_runtime` 을 off 와 비교 | sleep 에 block 이 이미 포함 (실측 io-0: 둘 다 3053.4) → 두 배로 셈 | off ↔ `sum_sleep_runtime` |
| 스레드별 %CPU 를 더해 100% 넘으면 이상하다고 판단 | 멀티스레드 프로세스는 최대 ncpu × 100% | `util = Σrun / (ncpu·wall)` |

## 8. 면접 Q&A

<details>
<summary>Q1. 스레드가 느리다. 시간이 어디로 가는지 어떻게 나누나?</summary>

wall = on-CPU + 런큐 대기 + off-CPU. 리눅스는 `/proc/<pid>/task/<tid>/schedstat` 에 앞의 둘(ns)과 슬라이스 수를 준다. off 는 wall 에서 빼서 구하고, S/D 구분은 `stat` 의 state 표본이나 `schedstats` 를 켠 `/proc/.../sched` 의 `sum_sleep_runtime`/`sum_block_runtime` 으로 한다. on-CPU 가 크면 perf record·flame graph(LPERF-B05/B06), 런큐 대기가 크면 CPU 수·우선순위·cgroup 제한(LPERF-M05/M12), off 가 크면 off-CPU 분석·락·I/O(LPERF-M06/M07/M09).

**꼬리질문: 그럼 `top` 의 %CPU 로는 왜 안 되나?** %CPU 는 run 만 보여 준다. run 이 낮을 때 그게 "할 일이 없어서(off)" 인지 "CPU 를 못 받아서(wait)" 인지 구분하지 못한다 — 실행 3 의 cpu 스레드는 35% CPU 였지만 일이 무한히 있었다.

</details>

<details>
<summary>Q2. `ps` 에서 state 가 R 이면 CPU 를 쓰고 있는 건가?</summary>

아니다. R(TASK_RUNNING)은 "실행 중이거나 실행 가능(런큐)" 이다. 오버서브스크립션 실측에서 cpu-0 은 R 표본 100% 인데 실제 run 은 43.6%, 나머지 56.4% 는 런큐 대기였다. 구분하려면 schedstat 2번째 필드(run_delay)나 스케줄러 트레이스포인트(`sched_wakeup`·`sched_switch`, LPERF-M05)를 본다. load average 가 R + D 스레드 수라는 것도 같은 맥락이다 (D 를 넣는 건 리눅스 특유).

</details>

<details>
<summary>Q3. voluntary 와 nonvoluntary context switch 는 각각 무엇을 말해 주나?</summary>

voluntary = 스스로 잠듦(락·I/O·sleep·조건 변수 대기) → off-CPU 원인을 찾아야 한다. nonvoluntary = 돌 수 있는데 빼앗김(타임슬라이스 소진, 더 급한 태스크 깨어남) → CPU 경쟁. 실측: cpu 스레드는 vcsw 0 / nvcsw 593, io 스레드는 vcsw 10890 / nvcsw 16. 주기 스레드 tick-0/tick-1 은 vcsw 가 둘 다 407(10 ms 주기 × 4 s)로 같지만 nvcsw 가 60 vs 426 — 같은 일을 해도 어느 CPU 에 붙었느냐에 따라 선점 횟수가 7배 달랐다.

**꼬리질문: nvcsw 가 많으면 무조건 나쁜가?** CPU 바운드 배치 작업이면 정상이다. 지연 민감 스레드(제어 루프, 오디오)라면 우선순위(`SCHED_FIFO`)나 전용 코어(`isolcpus`/cpuset)를 검토한다.

</details>

<details>
<summary>Q4. 회계가 맞는지 어떻게 검증했나? 오차는 어디서 오나?</summary>

① 스레드별 run + wait ≤ wall (넘친 양 = skew, 1% 허용), ② Σrun ≤ CPU 수 × wall. 오버서브스크립션 실측에서 Σrun 24077 ms vs 6 × 4066 = 24400 ms (util 0.987), 최대 skew 8.1 ms. 교차 검증으로 off(나머지)를 커널이 직접 센 `sum_sleep_runtime` 과 비교해 수십 ms 이내로 맞았다. 오차 원인: 카운터가 구간이 **끝날 때** 더해진다 (run_delay 는 CPU 를 받을 때, sleep 은 깨어날 때, sum_exec_runtime 은 tick 때) → 샘플 순간 진행 중인 구간만큼. 그리고 세 파일을 읽는 사이의 시차, 도구 자신의 런큐 대기.

</details>

<details>
<summary>Q5. 락 경합으로 기다리는 스레드는 어떻게 보이나? I/O 대기와 어떻게 구별하나?</summary>

glibc 뮤텍스는 경합 시 `futex(FUTEX_WAIT)` 로 interruptible sleep → state **S**, vcsw 증가, `sum_block_runtime` 0. 디스크 완료를 기다리는 `fdatasync` 는 uninterruptible → **D**, `sum_block_runtime` 증가 (실측 io-0 D 71%, dblock = dsleep). 다만 D 가 항상 디스크는 아니다 (커널 내부 mutex, NFS, 페이지 폴트 I/O). "누구를 기다리나" 는 off-CPU 스택(LPERF-M06)이나 락 holder 추적(LPERF-M07)으로 간다. 그리고 락 경합은 락을 **쥔** 스레드의 런큐 대기로 증폭된다 — 실행 2 에서 lock 스레드의 wait 가 4–6% → 22–30% 로 늘자, 같은 4초 동안 락 안에서 일한 시간(lock 스레드 run 합)이 3772 ms → 2125 ms 로 줄었다 — 락을 쥔 채 런큐에서 기다리는 동안 나머지 셋은 계속 잠들어 있다.

</details>

<details>
<summary>Q6. 같은 걸 FreeRTOS 위 펌웨어에서 재려면?</summary>

상태 대응: Running = run, Ready = wait, Blocked/Suspended = off. `configGENERATE_RUN_TIME_STATS` + 빠른 타이머(DWT CYCCNT 등)로 `vTaskGetRunTimeStats()`/`uxTaskGetSystemState()` 가 태스크별 run 을 준다. Ready 대기는 커널이 세지 않으므로 `traceMOVED_TASK_TO_READY_STATE` 에서 시각을 찍고 `traceTASK_SWITCHED_IN` 에서 차이를 더한다 (RTOS-A06, SEGGER SystemView/Tracealyzer 가 하는 일). 이 wait 가 RMA 응답 시간 식의 간섭 항 실측값이다(RTOS-M11). 회계 검증도 같다: Σ태스크 run + idle 태스크 run = 경과 시간 (단일 코어).

</details>

<details>
<summary>Q7. 런큐 대기를 줄이려면 무엇을 바꾸나? 스레드 수를 코어 수에 맞추면 끝인가?</summary>

CPU 바운드 스레드 수 ≤ 코어 수(실행 1: avg runnable 5.58/6, 대기 거의 0)가 기본이다. 하지만 실행 3 처럼 **다른 프로세스** 가 CPU 를 먹으면 소용없다 — 시스템 전체 load·PSI 를 본다. 지연 민감 스레드는 우선순위(`SCHED_FIFO`, nice), cpuset 으로 코어 분리, 인터럽트 affinity 조정. cgroup `cpu.max` 로 제한된 컨테이너는 CPU 가 놀아도 스로틀링으로 기다린다 (LPERF-M12) — 그때 schedstat 의 wait 가 아니라 `cpu.stat` 의 `nr_throttled`/`throttled_usec` 에 나타난다.

</details>

## 9. 추가 연습

### 연습 1 — `/proc/<tid>/sched` 를 정수로 읽기

`sum_sleep_runtime : 2963.800123` 처럼 ms 단위 소수 6자리다. `double` 로 읽지 말고 정수부 × 10⁶ + 소수부(6자리로 맞춤)로 **ns 정수** 를 만들어라. 키는 정확히 일치해야 한다 (`wait` 가 `wait_start` 에 걸리면 안 됨). schedstats 가 꺼져 있으면 줄이 없으니 -1.

<details>
<summary>답</summary>

`parse_sched_ns(buf, key, &ns)`: 줄 맨 앞이 key 이고 바로 뒤가 공백이나 `:` 일 때만 일치. `:` 다음 정수부를 `next_u64`, `.` 뒤 최대 6자리를 누적하고 모자란 자리는 ×10. `"760.3"` → 760 300 000 ns. `demo_ttime.c` 의 `sched_ms_to_ns` 가 같은 로직이다.

</details>

### 연습 2 — 틱 기반 CPU 시간과 schedstat 비교

`stat` 의 utime+stime 변화량(틱)을 ns 로 바꿔(`sysconf(_SC_CLK_TCK)`, 이 VM 100) run 과 **2틱 이내** 로 맞는지 보는 함수. 실측 쌍(229 틱 ↔ 2289.2 ms, 60 ↔ 609.6, 14 ↔ 146.5)은 통과, 10 ↔ 146.5 는 실패해야 한다.

<details>
<summary>답</summary>

`ticks_to_ns(t, hz) = t × (10⁹ / hz)`. 앞·뒤 샘플이 각각 1틱 미만 잘리므로 차이는 2틱 미만 + 세 파일을 읽는 시차. 그래서 `|run − ticks_ns| ≤ 2 틱`. `CONFIG_HZ`(1000)가 아니라 USER_HZ(100)라는 점에 주의 — 커널 tick 과 `/proc` 보고 단위는 다르다.

</details>

### 연습 3 — 깨어날 때마다 얼마나 기다렸나

`wait_per_slice_ns = wait / slices`, `preempt_permille = nvcsw × 1000 / (vcsw + nvcsw)`, 그리고 `|slices − (vcsw + nvcsw)| ≤ 1` 검사. 실행 2 의 tick-0(760.3 ms / 467 → 1 628 051 ns, 128‰)과 tick-1(606.0 ms / 833 → 727 490 ns, 511‰)로 확인.

<details>
<summary>답</summary>

정수 나눗셈으로 충분하다. 0 으로 나누는 경우(슬라이스 0, 스위치 0)는 0 을 돌려준다. tick-1 은 슬라이스당 대기가 짧지만 선점이 51% — 일을 잘게 끊겨 받았다. 평균은 꼬리를 숨기므로 최악값은 runqlat 히스토그램(LPERF-M05)으로 본다.

</details>

### 연습 4 — 평균이 숨기는 스파이크

1초 간격 누적 샘플 `{t, run, wait}` 시계열에서 wait/wall 이 가장 큰 구간의 시작 인덱스와 ‰ 을 찾는다. 4초 평균은 18.75% 인데 2→3 구간만 60% 인 시계열에서 2 와 600 이 나와야 한다. 구간 길이가 다르면 나눗셈 없이 `w₁ × wall₂ > w₂ × wall₁` 로 비교.

<details>
<summary>답</summary>

`worst_wait_interval(s, n, &permille)`: i = 0..n−2 에 대해 증분을 구하고 교차 곱으로 비교, 마지막에 한 번만 나눠 ‰. n < 2 이면 -1. 도구로 확장하면 "평균 대기 5% 인데 가끔 프레임이 빠진다" 를 잡는 첫걸음이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr      # 24/24 passed
gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_asan && ./pr_asan   # 24/24, 보고 없음
```

## 10. 핵심 정리

- **wall = run + wait + off.** run·wait 는 `/proc/<pid>/task/<tid>/schedstat` 1·2번째(ns, `CONFIG_SCHED_INFO`, sysctl 과 무관), off 는 나머지.
- `stat` 의 **R = running + runnable**. 런큐 대기는 schedstat 으로만 보인다. S = interruptible(futex 락·소켓·타이머), D = uninterruptible(주로 디스크 I/O).
- vcsw = 스스로 잠듦(off-CPU 원인 찾기), nvcsw = 빼앗김(CPU 경쟁). slices ≈ vcsw + nvcsw.
- 파싱 함정: `voluntary` ⊂ `nonvoluntary` → 줄 맨 앞 비교 / comm 은 마지막 `)` 까지 / `tpgid` -1 / 15자.
- 검증: 스레드별 run + wait ≤ wall, Σrun ≤ ncpu × wall. 오차 = 카운터가 구간 끝에 더해짐 + 읽기 시차 + 도구 자신의 런큐 대기 → 스레드별 샘플 시각.
- 오버서브스크립션(avg runnable 14.56 / 6)은 **모든** 스레드의 wait 를 키운다 — 10 ms 주기 스레드도 깨어날 때마다 평균 1.63 ms 대기. 다른 프로세스가 원인이면 프로세스 도구로는 안 보인다 → loadavg·PSI.
- `sum_sleep_runtime` 은 block(D) 을 포함. 교차 검증할 때만 `kernel.sched_schedstats=1`.
- RTOS: Running/Ready/Blocked 와 같은 회계. Ready 시간 = RMA 간섭 항, FreeRTOS 는 trace 훅으로 직접 잰다.

## 참고자료

- Linux kernel docs — Scheduler Statistics (`/proc/<pid>/schedstat` 3필드) — <https://docs.kernel.org/scheduler/sched-stats.html>
- `proc_pid_stat(5)`, `proc_pid_status(5)` man pages (stat 필드 번호, `voluntary_ctxt_switches`) — <https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html>, <https://man7.org/linux/man-pages/man5/proc_pid_status.5.html>
- Linux kernel docs — EEVDF Scheduler — <https://docs.kernel.org/scheduler/sched-eevdf.html>
- Linux kernel docs — PSI: Pressure Stall Information — <https://docs.kernel.org/accounting/psi.html>
- Brendan Gregg — Systems Performance, 2nd ed., ch.6 CPUs (run queue latency, thread state analysis), ch.5 Applications (thread state analysis: on-CPU / off-CPU / runnable)
- Brendan Gregg — Thread State Analysis (TSA) Method — <https://www.brendangregg.com/tsamethod.html>
- FreeRTOS — Run Time Statistics, Trace Hook Macros — <https://www.freertos.org/rtos-run-time-stats.html>, <https://www.freertos.org/rtos-trace-macros.html>
- `stress-ng(1)` — <https://github.com/ColinIanKing/stress-ng>
- 관련 레슨: LPERF-B01 (60초 점검), LPERF-B02 (USE·PSI), LPERF-M05 (runqlat), LPERF-M06 (off-CPU), LPERF-M07 (futex 락 경합), LPERF-M12 (cpu.max 스로틀링), OS-B04/OS-B05 (스레드·뮤텍스 경합), OS-M02 (문맥 교환 비용), RTOS-B02 (태스크 상태), RTOS-M11 (RMA 간섭), RTOS-A06 (RTOS 트레이싱)
