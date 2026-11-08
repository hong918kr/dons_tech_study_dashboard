---
id: OS-M02
time: 95분
updated: 2026-11-08
---
# OS-M02 컨텍스트 스위치 비용 측정: 파이프 핑퐁

## 학습 목표

- 두 스레드와 fork 한 두 프로세스가 **파이프 두 개로 1바이트를 주고받는 핑퐁** 을 짜고, EOF·EINTR·SIGPIPE·안 쓰는 파이프 끝 닫기까지 처리해 수십만 번 왕복해도 멈추지 않게 만든다.
- 왕복 하나가 **write+read 쌍 2개 + "블록 → 상대 깨움 → 스위치" 2번** 으로 이루어진다는 것을 그림으로 설명하고, 블록하지 않는 기준선(selfpipe)을 빼서 한 방향 스위치 비용을 추정한다 (이 맥: 약 0.5~1 µs).
- 측정값을 **min / median / p99** 로 요약하고(평균이 왜 거짓말하는지 포함), 타이머 해상도에 맞춰 배치 크기를 정한다.
- macOS 에서 생기는 오차 요인 — **코어 고정 불가**(`sched_setaffinity` 없음, affinity tag 미지원), P/E 코어, 1 µs 짜리 `CLOCK_MONOTONIC`, 워밍업, 같은 코어 vs 다른 코어 깨움, 백그라운드 부하 — 를 각각 실측으로 보여 주고 통제 방법을 말할 수 있다.
- 측정한 깨움 비용으로 "센서 스레드 → 로거 스레드" 전달의 **배치 크기** 를 CPU 예산과 마감 시간에서 계산하고, 부분 읽기(partial read)를 재조립하는 수신기를 쓴다.

## 1. 문제

"컨텍스트 스위치 한 번에 얼마나 걸리죠? 그걸 어떻게 재죠?" — 임베디드 리눅스·로보틱스·플랫폼 면접에서 "IPC 왕복 지연 중 몇 %가 스위치인가", "스레드를 몇 개로 나눌까", "1 kHz 제어 루프를 스레드 둘로 쪼개도 되나" 를 판단하는 기초 숫자다. MCU 에서도 같은 질문이 "ISR → 태스크 알림 → 태스크 실행까지 몇 µs?" 로 나온다 — Cortex-M 은 PendSV 예외로 스위치하고(ARM-A01), FreeRTOS 의 태스크 알림이 가장 싼 깨움이다(RTOS-M01, RTOS-M02). 거기서는 `DWT->CYCCNT` 로 사이클을 직접 세지만(RTOS-A03), 범용 OS 에서는 스위치를 따로 떼어 잴 수 없다. 그래서 OSTEP 6장(Limited Direct Execution) 측정 과제가 제안하는 방법을 쓴다: **두 실행 흐름이 파이프로 1바이트를 주고받게 해서, 한쪽이 기다리면 반드시 다른 쪽으로 넘어가게 만든다.**

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">왕복(RT) 하나 = write+read 쌍 2개 + 블록→깨움→스위치 2번 (한 코어에서 일어난 경우)</text>
  <text x="10" y="62" fill="currentColor">핑</text>
  <text x="10" y="162" fill="currentColor">커널</text>
  <text x="10" y="262" fill="currentColor">퐁</text>
  <line x1="60" y1="57" x2="730" y2="57" stroke="currentColor" stroke-dasharray="3 4"/>
  <line x1="60" y1="257" x2="730" y2="257" stroke="currentColor" stroke-dasharray="3 4"/>
  <rect x="60" y="42" width="70" height="30" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="66" y="62" fill="currentColor" font-family="monospace">t0 write</text>
  <rect x="130" y="142" width="80" height="30" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="136" y="162" fill="currentColor">퐁 깨움 표시</text>
  <rect x="210" y="42" width="60" height="30" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="216" y="62" fill="currentColor" font-family="monospace">read</text>
  <rect x="270" y="142" width="90" height="30" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="276" y="162" fill="currentColor">블록→스위치 ①</text>
  <rect x="360" y="242" width="60" height="30" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="366" y="262" fill="currentColor" font-family="monospace">read↩</text>
  <rect x="420" y="242" width="60" height="30" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="426" y="262" fill="currentColor" font-family="monospace">write</text>
  <rect x="480" y="142" width="80" height="30" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="486" y="162" fill="currentColor">핑 깨움 표시</text>
  <rect x="560" y="242" width="50" height="30" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="564" y="262" fill="currentColor" font-family="monospace">read</text>
  <rect x="610" y="142" width="90" height="30" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="616" y="162" fill="currentColor">블록→스위치 ②</text>
  <rect x="660" y="42" width="70" height="30" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="664" y="62" fill="currentColor" font-family="monospace">read↩ t1</text>
  <path d="M100 72 L160 142 M240 72 L300 142 M330 172 L380 242 M450 272 L510 172 M585 272 L640 172 M680 142 L690 72" stroke="currentColor" fill="none"/>
  <g fill="currentColor">
    <text x="10" y="300">초록 = selfpipe 기준선으로 잴 수 있는 부분 (블록 안 하는 write+read 쌍 ≈ 0.43 µs, 2쌍).</text>
    <text x="10" y="320">주황 = 그 나머지: 깨울 스레드 찾기 · 레지스터/스택 교체 · (프로세스면) 주소 공간 교체 · 캐시 콜드 → 우리가 원하는 값.</text>
  </g>
</svg>
```

그런데 이 그림은 **두 흐름이 같은 코어에 있을 때** 의 이야기다. 코어가 12개인 Apple M6 에서는 퐁이 다른 코어에서 깨어날 수도 있고, 그러면 "스위치" 가 아니라 "다른 코어를 깨우는 비용(IPI, idle 탈출)" 을 재게 된다. 리눅스는 `taskset`/`sched_setaffinity` 로 둘을 한 코어에 묶지만 **macOS 에는 그 수단이 없다.** 이 레슨은 그 한계를 숨기지 않고 측정에 기록해서 나눠 본다.

측정 환경 (모든 숫자의 출처):

```text
$ sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu
Apple M6
2          ← perflevel0 "Super"
4          ← perflevel1 "Performance"
6          ← perflevel2 "Efficiency"   (hw.nperflevels: 3, hw.ncpu: 12)
$ cc --version | head -1
Apple clang version 21.0.0 (clang-2100.0.123.102)
macOS 27.2, 측정 중 다른 에이전트의 벤치마크·Lima VM 이 함께 돌았다 (load average 4~11, 각 표에 표시)
```

### Part A — 1바이트 핑퐁 (스레드 2개, 프로세스 2개)

- `xwrite1(fd, b)` / `xread1(fd, &b)` — 1바이트 쓰기/읽기. `EINTR` 면 재시도, 성공 1, EOF 0(읽기), 오류 -1.
- `pp_pong(rfd, wfd, rounds)` — 받은 바이트에 +1 해서 돌려준다. `rounds` 번 또는 EOF/오류까지. 돌려준 횟수 반환.
- `pp_ping(rfd, wfd, rounds, rt_ns)` — i 번째 왕복에 `(uint8_t)i` 를 보내고 `(uint8_t)(i+1)` 을 받아야 한다. 왕복마다 시간을 `rt_ns[i]` 에 (NULL 이면 생략). 검증된 왕복 수 반환.
- 같은 함수를 하네스 `run_thread_pp`(pthread) 와 `run_proc_pp`(fork) 가 쓴다. 2만 번 왕복이 전부 검증돼야 하고, **핑이 끝나고 쓰기 끝을 닫으면 퐁은 EOF 로 끝나야** 한다 (자식 프로세스의 exit 코드로 확인).

### Part B — 측정 수학: 변환 · 요약 · 빼기 · 배치

- `ticks_to_ns(t, numer, denom)` — `mach_absolute_time` 의 tick 을 ns 로 (Apple Silicon 125/3). `t*numer` 가 64비트를 넘쳐도 정확하게.
- `summarize(s, n, warm, &st)` — 앞 `warm` 개를 버리고 min / median / p99(nearest-rank) / max / mean. 입력은 건드리지 않는다.
- `switch_cost_ns(rt, pair)` — `(rt − 2·pair) / 2`: 한 방향의 "블록 → 깨움 → 스위치" 추정.
- `batch_for_resolution(res, op, err_ppm)` — 시계 한 눈금이 측정값의 `err_ppm` 이하가 되려면 한 표본에 연산을 몇 개 묶어야 하나.
- 그리고 `demo_bench.c` 로 getppid · selfpipe · 스레드 · 프로세스 · 조건 변수 · 스핀 핑퐁을 실측해 위 함수들로 해석한다.

### Part C — 센서 스레드 → 로거 스레드: 배치 전달

드론 컴패니언 컴퓨터(Jetson·i.MX8 같은 Cortex-A 리눅스)에서 IMU 리더 스레드가 4 kHz 로 12바이트 샘플을 만들고, 로거 스레드가 파이프로 받아 저장한다. 샘플마다 깨우면 초당 4000번의 깨움이 든다.

- `write_all` — 짧은 쓰기(short write)·EINTR 처리.
- `send_batched(wfd, s, n, batch)` — `batch` 개씩 묶어 한 번에 쓰고 묶음 수 반환.
- `recv_all(rfd, out, max)` — EOF 까지 읽어 샘플로 **재조립**. read 는 12바이트 경계를 모른다 (5바이트씩 흘려 보내는 "나쁜 드라이버" 로도 테스트).
- `choose_batch(rate_hz, wake_ns, cpu_budget_ppm, deadline_ns)` — CPU 예산과 지연 마감을 모두 만족하는 가장 작은 배치. 없으면 0.

MCU 로 옮기면 같은 계산이 "UART/ADC 샘플마다 태스크를 깨울까, DMA half/full 전송 완료(BM-M09)마다 깨울까" 가 된다.

## 2. Problem (English)

> **Measure context-switch cost with a pipe ping-pong.**
>
> 1. Two threads, and then two processes (`fork`), exchange one byte over two pipes for N round trips (N ≥ 100 000). The responder echoes `b+1`; the initiator verifies every reply and records per-round-trip latency. Implement `int xwrite1(int fd, uint8_t b)`, `int xread1(int fd, uint8_t *b)` (retry on `EINTR`; 1 ok / 0 EOF / −1 error), `unsigned pp_pong(int rfd, int wfd, unsigned rounds)` and `unsigned pp_ping(int rfd, int wfd, unsigned rounds, uint64_t *rt_ns)`. Closing the initiator's write end must terminate the responder with EOF.
> 2. Implement the measurement helpers: overflow-safe `uint64_t ticks_to_ns(uint64_t t, uint32_t numer, uint32_t denom)`; `int summarize(const uint64_t *s, size_t n, size_t warm, lat_stats_t *out)` (min, lower median, nearest-rank p99, max, mean, skipping `warm` samples); `int64_t switch_cost_ns(uint64_t rt_ns, uint64_t pair_ns)`; `uint64_t batch_for_resolution(uint64_t res_ns, uint64_t op_ns, uint64_t max_err_ppm)`.
> 3. A sensor thread sends 12-byte samples to a logger thread through a pipe. Implement `write_all`, `send_batched` (one write per batch, returns the number of batches) and `recv_all` (reassemble samples from arbitrary read sizes until EOF). Then `unsigned choose_batch(uint64_t rate_hz, uint64_t wake_ns, uint64_t cpu_budget_ppm, uint64_t deadline_ns)`: smallest batch meeting both the CPU budget and the latency deadline, 0 if impossible. Integer math only.
>
> Follow-ups: Your round trip is 2.8 µs — how much of it is the switch? Why can't you pin both threads to one core on macOS, and how does that bias the result? Process vs thread switch — what differs? Which statistic do you report, and why not the mean? What does the same experiment look like on an RTOS?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력):

```text
FAIL starter.c:244  xwrite1(p[1], 0xA5) == 1 && xread1(p[0], &b) == 1 && b == 0xA5
FAIL starter.c:246  xread1(p[0], &b) == 0
thread ping-pong wall: 0.04 ms for 20000 RT
FAIL starter.c:256  done == R
FAIL starter.c:257  served == R
FAIL starter.c:264  done == R
FAIL starter.c:265  child_ok
FAIL starter.c:269  run_thread_pp(300, 100, NULL, &served) == 100 && served == 100
FAIL starter.c:273  ticks_to_ns(24000000u, 125, 3) == 1000000000u
...
FAIL starter.c:318  choose_batch(4000, 1500, 1000, 2000000) == 6u
FAIL starter.c:320  choose_batch(1000, 1500, 10000, 5000000) == 1u
FAIL starter.c:321  choose_batch(32000, 3000, 1000, 10000000) == 96u
10/36 passed
```

하네스 설계: 모든 대기는 **EOF 로 끝난다.** 하네스는 핑이 돌아오면 핑의 쓰기 끝을, 퐁이 돌아오면 퐁의 쓰기 끝을 닫는다. 그래서 TODO 가 빈 채로(바로 0 반환) 돌려도, 한쪽만 구현해도 상대는 EOF 를 보고 빠져나온다 — 시작 코드는 멈추지 않는다. 읽는 쪽이 사라진 파이프에 쓰면 SIGPIPE 로 프로세스가 죽으므로 `main` 첫 줄에서 `signal(SIGPIPE, SIG_IGN)` 으로 EPIPE 오류로 바꿔 받는다. CHECK 는 왕복 수·에코 값·통계 함수의 고정 입력·샘플 순서처럼 **올바르면 매번 성립하는 것** 만 본다. 시간은 출력만 한다.

관찰용 파일 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o bench && ./bench all 5 200000   # 6가지 실측 (약 17초)
./bench thread 3 100000 bg                                                                  # QoS: def | ui | bg
cc -std=c11 -Wall -Wextra -Werror -O2 demo_timer.c -o dt && ./dt                            # 시계 해상도·비용
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_affinity.c -o da && ./da                # 코어 고정 시도 + QoS 배치
# 리눅스(Lima VM elinux)에서만: 코어 고정 비교
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_linux_pin.c -o pin && ./pin 0 0 && ./pin 0 1 && ./pin -1 -1
```

## 4. 힌트

<details>
<summary>힌트 1 — 파이프가 왜 두 개인가, 누가 어느 끝을 닫나</summary>

파이프는 한 방향이다. 핑→퐁(`a2b`), 퐁→핑(`b2a`) 두 개가 필요하다. `read` 가 0(EOF)을 돌려주는 조건은 "그 파이프의 **쓰기 끝을 가진 fd 가 하나도 안 남았을 때**" 다. fork 하면 네 개 fd 가 모두 복제되므로 부모·자식 모두 자기가 안 쓰는 끝을 닫아야 EOF 가 온다. 스레드는 fd 테이블을 공유하니 하나만 닫으면 된다.

</details>

<details>
<summary>힌트 2 — 시간은 어디서 어디까지 재나</summary>

`write` 직전에 t0, 답장 `read` 가 돌아온 직후 t1. 검증(`r != i+1`)과 기록은 t1 뒤에. 루프 안에서 `printf` 는 금지 — 그 자체가 시스템 콜이다. 시계는 맥에서 `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` 또는 `mach_absolute_time()`. `CLOCK_MONOTONIC` 은 맥에서 1 µs 단위다 (`demo_timer.c`).

</details>

<details>
<summary>힌트 3 — t * numer 오버플로</summary>

`t * 125` 는 t 가 약 1.5×10¹⁷ 을 넘으면 64비트를 넘친다. `t = q·denom + r` 로 쪼개면 `t·numer/denom = q·numer + r·numer/denom` 이고, `r < denom` 이라 `r·numer` 는 작다. 정확히 같은 값(내림)이 나온다.

</details>

<details>
<summary>힌트 4 — 스위치 비용 = (왕복 − 2·쌍) / 2</summary>

왕복 동안 핑이 write·read, 퐁이 read·write 를 한다 = write+read 쌍 2개. 블록하지 않는 write+read 쌍은 같은 스레드에서 파이프에 1바이트 쓰고 바로 읽어서 잰다 (selfpipe). 나머지를 두 방향으로 나눈다. **같은 조건(같은 코어 종류, 같은 QoS)에서 잰 기준선** 이어야 뺄 수 있다.

</details>

<details>
<summary>힌트 5 — read 는 경계를 모른다</summary>

파이프는 바이트 스트림이다. 12바이트 샘플 8개를 한 번에 써도 읽는 쪽은 100바이트, 그다음 −4바이트… 처럼 받을 수 있다. 버퍼에 남은 조각 길이를 기억해 두고, 12바이트가 모일 때마다 하나씩 꺼낸 뒤 남은 조각을 앞으로 당긴다 (`memmove`).

</details>

## 5. 풀이

### Part A — 핑퐁의 정확성

```c
static int xread1(int fd, uint8_t *b) {
    for (;;) {
        ssize_t n = read(fd, b, 1);
        if (n == 1) return 1;
        if (n == 0) return 0;              /* EOF: 쓰기 끝이 모두 닫힘 */
        if (errno == EINTR) continue;      /* 시그널이 끼어들면 다시 */
        return -1;
    }
}
```

- **EINTR:** 블록된 `read`/`write` 중 시그널 핸들러가 돌면 -1/EINTR 로 돌아올 수 있다 (`SA_RESTART` 가 없을 때). 측정 루프가 수십만 번이면 언젠가 만난다.
- **EOF 와 fd 닫기:** `run_proc_pp` 의 자식은 `close(a2b[1]); close(b2a[0]);` 부터 한다. 이걸 빼면 자식 자신이 `a2b` 의 쓰기 끝을 들고 있어서, 부모가 닫아도 자식의 `read` 는 **영원히** EOF 를 못 본다. 테스트 `pp_pong(..., rounds + 5u)` 가 정확히 `rounds` 를 돌려줘야 하는 이유가 이 검사다.
- **SIGPIPE:** 반대로 읽는 쪽이 모두 닫힌 파이프에 쓰면 커널이 SIGPIPE 를 보낸다 (기본 동작: 종료). 무시해 두면 `write` 가 -1/EPIPE 를 돌려준다.
- **fork 전 `fflush(stdout)`:** 안 하면 부모의 stdio 버퍼가 자식에 복제되어 같은 줄이 두 번 찍힌다.
- **바이트 순환 검증:** `(uint8_t)i` 를 보내고 `(uint8_t)(i+1)` 을 기대하므로 256 마다 0 으로 돌아간다. 2만 번 중 한 번이라도 순서가 꼬이거나 빠지면 그 자리에서 멈추고 `done < R`.

`pp_ping` 의 타이밍 위치:

```c
uint64_t t0 = now_ns();
if (xwrite1(wfd, (uint8_t)i) != 1 || xread1(rfd, &r) != 1) return i;
uint64_t t1 = now_ns();
if (r != (uint8_t)(i + 1u)) return i;
if (rt_ns) rt_ns[i] = t1 - t0;
```

### Part B — 실측: 6가지를 같은 프로그램으로

`demo_bench.c` 는 왕복마다 `mach_absolute_time` 으로 재고, 처음 2000 왕복은 버린 뒤 min/med/p99/max 를 낸다. 핑과 퐁은 깨어난 직후 `pthread_cpu_number_np` 로 **자기가 어느 코어에 있는지** 기록한다. getppid·selfpipe 는 한 번이 시계 눈금(41.67 ns)과 비슷하거나 짧으므로 100번씩 묶어 잰다.

<!-- code: demo_bench.c -->

조용한 편일 때 (load average ≈ 4, 5회 × 20만 왕복, 실제 출력에서 열 일부를 줄임):

```text
$ ./bench all 5 200000
timebase 125/3 (1 tick = 41.67 ns), rounds/run 200000 (first 2000 discarded), qos def
getppid   run1..5  med 101.2 101.2 101.7 120.8 101.7   min 98.3~99.6   p99 171~196 ns per call
selfpipe  run1..5  med 516.7 506.7 432.5 432.9 440.4   min 418.8~425.0 p99 601~986 ns per write+read
thread    run1  mean 4970  min 2042  med 4250  p99 15667  max 2519083 | first 10208 | same-core 80.6%
thread    run2  mean 4802  min 1917  med 4000  p99 16333  max 6198208 | first  9167 | same-core 85.5%
thread    run3  mean 3524  min 1917  med 2833  p99 12708  max 2816625 | first  7667 | same-core 92.8% (med 2792) cross 7.2% (med 4542)
thread    run4  mean 3045  min 1917  med 2833  p99  7917  max 1724208 | first  8917 | same-core 93.7% (med 2792) cross 6.3% (med 4000)
thread    run5  mean 3172  min 1917  med 2833  p99  8917  max   85500 | first  7875 | same-core 92.9% (med 2792) cross 7.1% (med 4167)
proc      run1  mean 4937  min 2167  med 3750  p99 25292  max 2842917 | first 163250 | same-core 90.7% (med 3625) cross 9.3% (med 6875)
proc      run2  mean 3959  min 2167  med 2833  p99 23583  max   92208 | first 137667 | same-core 91.8%
proc      run3  mean 3462  min 2208  med 2833  p99 10292  max 2598917 | first 155250 | same-core 95.5%
proc      run4  mean 3520  min 2167  med 2917  p99  9375  max 2858583 | first  93667 | same-core 94.1%
proc      run5  mean 3990  min 2167  med 3167  p99 12917  max 2665292 | first 147833 | same-core 95.4% (med 3042) cross 4.6% (med 6000)
cond      run1..5  min 1667~1708  med 2583 2125 2583 2542 2583  p99 7917~10292 | same-core 92~97%
spin      run1..5  mean 83~90  min 0  med 83  p99 125~208 | first 9625~14458 | same-core 0.0% cross 100.0%
```

같은 측정을 QoS 만 바꿔서 (`./bench thread|proc|selfpipe 3 100000 bg`):

```text
thread    bg  med 7042 6417 6417  min 3250~3792  p99 67708~186625 | same-core 31~62% | ping on E 100.0%
proc      bg  med 14250 11500 12958  min 3000~3792  p99 76792~79167 | same-core 3~16%  | ping on E 100.0%
selfpipe  bg  med 825.8 770.0 787.1  min 741.7~743.3 ns per write+read
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">왕복(또는 호출) 중앙값 — 실측, 5회(bg 는 3회) 중앙값의 중앙값, 0.04 px/ns</text>
  <g fill="currentColor">
    <text x="10" y="52">spin (원자 변수)</text>
    <text x="10" y="84">getppid 1회</text>
    <text x="10" y="116">selfpipe 쌍</text>
    <text x="10" y="148">cond 핑퐁</text>
    <text x="10" y="180">thread 핑퐁</text>
    <text x="10" y="212">proc 핑퐁</text>
    <text x="10" y="244">thread 핑퐁 bg</text>
    <text x="10" y="276">proc 핑퐁 bg</text>
  </g>
  <rect x="150" y="40" width="3.3" height="18" class="f-ok" stroke="currentColor"/>
  <rect x="150" y="72" width="4.1" height="18" class="f-ok" stroke="currentColor"/>
  <rect x="150" y="104" width="17.6" height="18" class="f-ok" stroke="currentColor"/>
  <rect x="150" y="136" width="103.3" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="150" y="168" width="113.3" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="150" y="200" width="116.7" height="18" class="f-acc" stroke="currentColor"/>
  <rect x="150" y="232" width="256.7" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="150" y="264" width="518.3" height="18" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="160" y="54">83 ns (커널 없음, 코어 간 캐시 라인 이동만)</text>
    <text x="160" y="86">102 ns (모드 전환 왕복 1번)</text>
    <text x="174" y="118">440 ns (write+read, 블록 없음)</text>
    <text x="260" y="150">2583 ns</text>
    <text x="270" y="182">2833 ns</text>
    <text x="274" y="214">2917 ns</text>
    <text x="414" y="246">6417 ns (100% E 코어)</text>
    <text x="520" y="298">12958 ns (E 코어, 다른 코어 깨움 84~97%)</text>
  </g>
  <line x1="150" y1="34" x2="150" y2="288" stroke="currentColor"/>
  <text x="10" y="322" fill="currentColor">초록 = 스위치 없는 기준선, 주황 = 블록→깨움 포함, 노랑 = BACKGROUND QoS (E 코어). 축 0 은 x=150.</text>
</svg>
```

### Part B — 해석: 스위치는 얼마인가

`switch_cost_ns` 에 넣으면 (같은 조건의 기준선끼리):

| 기준 | 왕복 | selfpipe 쌍 | (RT − 2·쌍)/2 = 한 방향 블록→깨움→스위치 |
|---|---|---|---|
| min (가장 안정적) | 1917 ns (thread) | 418.8 ns | **≈ 540 ns** |
| median, 조용할 때 | 2833 ns (thread) | 432.5 ns | **≈ 984 ns** |
| min, proc | 2167 ns | 418.8 ns | ≈ 665 ns (스레드보다 +125 ns) |
| median, bg (E 코어) | 6417 ns (thread) | 787.1 ns | ≈ 2421 ns |

읽는 법:

1. **"컨텍스트 스위치 = 약 0.5~1 µs (P 코어), 2.4 µs (E 코어)"** 가 이 맥의 답이다. 단, 여기엔 순수한 레지스터 교체뿐 아니라 깨울 스레드 찾기·런큐 조작·스케줄러 결정·캐시 콜드가 다 들어 있다. 순수 레지스터 저장/복원은 그중 작은 부분이다 (Cortex-M PendSV 는 거의 그것만 한다 — Q7).
2. **스레드 vs 프로세스:** 조용할 때 median 은 2833 vs 2917 ns 로 거의 같고, min 은 1917 vs 2167 ns 로 프로세스가 약 250 ns(왕복당, 스위치 2번) 더 든다. 차이는 주소 공간 교체(arm64 의 TTBR0 + ASID 교체)와 그 뒤의 TLB·캐시 미스다. ASID 가 있어서 TLB 를 통째로 비우지 않으므로 차이가 작다.
3. **첫 왕복:** 스레드 7.7~10 µs, 프로세스 **94~163 µs**. fork 직후 자식은 부모 페이지를 COW 로 공유하므로 처음 쓰는 페이지마다 페이지 폴트가 난다 (OS-M03). 워밍업을 버리는 이유가 이것이다.
4. **평균은 거짓말한다:** thread run2 의 mean 4802 ns 는 median 4000 보다 크고, max 는 6.2 ms 다. 20만 개 중 몇 개가 ms 단위로 선점당하면(다른 스레드에 타임슬라이스를 뺏김) 평균이 끌려 올라간다. 보고는 min(고유 비용) · median(전형) · p99(꼬리) 셋으로.
5. **조건 변수 핑퐁(2125~2583 ns)이 파이프보다 약간 싸다:** 파이프 버퍼 복사와 fd 계층이 없고, macOS 의 pthread_cond 는 psynch 커널 대기를 쓴다. 그래도 같은 µs 대다 — 비용의 대부분은 "잠들고 깨우기" 이지 전달 수단이 아니다.
6. **스핀(83 ns)은 커널이 없다:** 두 스레드가 서로 다른 코어에서 원자 변수 하나를 돌린다(same-core 0%). 83 ns 는 캐시 라인이 코어 사이를 한 번 오가는 비용이다. 대신 코어 두 개를 100% 태운다 (Q6).

### 오차 요인 1 — 코어 고정이 안 된다 (macOS)

```text
$ ./da
thread_policy_set(THREAD_AFFINITY_POLICY, tag=1) -> 46 ((os/kern) service not supported)
USER_INTERACTIVE busy 1 s:  cpu0 x135 (best 36541 ns)  cpu1 x59 ... cpu5 x701 (best 36541 ns)  cpu6 x20914 (best 4333 ns)
                            cpu7 x24519 (best 4333 ns)  cpu8 x2341 ... cpu11 x10086 (best 4500 ns)
BACKGROUND       busy 1 s:  cpu0 x313 (best 36583 ns) ... cpu5 x1077  cpu6 x8826 (best 4416 ns) ... cpu11 x6945 (best 4750 ns)
```

- `sched_setaffinity` 는 리눅스 전용이라 macOS 헤더에 없다. Mach 의 `THREAD_AFFINITY_POLICY`(affinity tag)는 인텔 맥 시절 "같은 L2 를 공유하게 해 달라" 는 **힌트** 였고, 이 Apple Silicon 맥에서는 `KERN_NOT_SUPPORTED`(46) 를 돌려준다 — 직접 확인한 값이다.
- 코어 번호는 **읽기만** 된다: `pthread_cpu_number_np()` (macOS 11+). 같은 20000회 빈 루프가 cpu 0~5 에서 36.5 µs, cpu 6~11 에서 4.3~4.8 µs 걸렸으므로 이 맥에서 **0~5 = E 코어, 6~11 = P/Super 코어** 로 본다. 1초 동안 한 스레드가 12개 코어를 다 돌아다녔다 — 고정은커녕 계속 옮겨 다닌다.
- QoS 클래스는 **추천** 이다. 바쁘게 도는 BACKGROUND 스레드도 P 코어에서 대부분을 보냈다. 반면 블록·깨움을 반복하는 BACKGROUND 핑퐁은 100% E 코어에서 깨어났다 (위 bg 표). 즉 QoS 는 "깨울 때 어느 클러스터에 놓을지" 에 주로 작용한다. "QoS 로 P 코어에 고정했다" 고 말하면 틀린다.

그래서 이 레슨은 고정 대신 **기록하고 나눈다.** 핑퐁 각 왕복에서 "퐁이 깨어난 코어 == 그다음 핑이 깨어난 코어" 면 한 코어에서 진짜 스위치가 일어난 것이다 (퐁이 블록하며 비운 코어에서 핑이 깨어남).

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 코어 깨움 (80~95%) vs 다른 코어 깨움 — 무엇을 재게 되나</text>
  <text x="10" y="58" fill="currentColor">코어 6</text>
  <rect x="80" y="40" width="120" height="30" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="90" y="60" fill="currentColor">핑 write, read</text>
  <rect x="200" y="40" width="80" height="30" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="208" y="60" fill="currentColor">스위치</text>
  <rect x="280" y="40" width="120" height="30" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="290" y="60" fill="currentColor">퐁 read↩ write</text>
  <rect x="400" y="40" width="80" height="30" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="408" y="60" fill="currentColor">스위치</text>
  <rect x="480" y="40" width="100" height="30" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="490" y="60" fill="currentColor">핑 read↩</text>
  <text x="590" y="60" fill="currentColor">med 2792 ns</text>
  <text x="10" y="128" fill="currentColor">코어 6</text>
  <text x="10" y="188" fill="currentColor">코어 7</text>
  <rect x="80" y="110" width="120" height="30" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="90" y="130" fill="currentColor">핑 write, read</text>
  <rect x="200" y="110" width="200" height="30" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="210" y="130" fill="currentColor">idle (WFI)</text>
  <rect x="80" y="170" width="150" height="30" rx="3" class="f-warn" stroke="currentColor"/>
  <text x="88" y="190" fill="currentColor">idle → IPI 로 깨움</text>
  <rect x="230" y="170" width="120" height="30" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="240" y="190" fill="currentColor">퐁 read↩ write</text>
  <rect x="400" y="110" width="140" height="30" rx="3" class="f-warn" stroke="currentColor"/>
  <text x="408" y="130" fill="currentColor">IPI → 핑 read↩</text>
  <path d="M150 140 L150 170 M300 200 L420 140" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="590" y="130" fill="currentColor">med 4000~6875 ns</text>
  <g fill="currentColor">
    <text x="10" y="232">다른 코어 깨움은 "스위치" 가 아니라 프로세서 간 인터럽트(IPI) + idle 탈출 + 캐시 라인 이동을 잰다.</text>
    <text x="10" y="252">고정할 수 없으니 왕복마다 코어 번호를 기록해 두 그룹으로 나눠 보고한다 (연습 1).</text>
  </g>
</svg>
```

macOS 스케줄러는 "깨운 쪽이 곧 블록할" 핑퐁 패턴에서 대부분 같은 코어에 깨웠다 (조용할 때 thread 81~94%, proc 91~96%). 다른 코어로 깨운 왕복은 median 이 1.2~3.3 µs 더 길었다.

**리눅스에서는 고정할 수 있다.** 같은 핑퐁을 Lima VM(aarch64 Ubuntu, 커널 7.0.0, vCPU 6개, gcc 15.2)에서 `sched_setaffinity` 로 묶어 본 실제 결과:

```text
$ ./pin 0 0          (핑·퐁 모두 CPU0)     thread  min 1208  med 1625~1708  p99 2291~2624 ns
$ ./pin 0 1          (핑 CPU0, 퐁 CPU1)    thread  min 1624~4083  med 14080~15954  p99 31950~54570 ns
$ ./pin -1 -1        (고정 안 함)          thread  min 1083~4624  med 14496~17579  p99 42364~66400 ns
$ ./pin 0 0 proc                           proc    min 1291~1333  med 1708  p99 2707~3166 ns
$ ./pin 0 1 proc                           proc    min 3874~4541  med 14663~17453 p99 34866~56278 ns
```

같은 코어로 묶으면 1.7 µs 로 안정적이고, 다른 vCPU 로 갈라지면 **10배** 가 된다. VM 이라 과장된 값이다 — 쉬고 있는 vCPU 를 깨우려면 호스트가 그 vCPU 스레드를 다시 스케줄해야 한다(가상 IPI). 실기 리눅스에서도 방향은 같지만 차이는 훨씬 작다. 교훈: **"고정했나, 어디에 고정했나" 를 안 밝힌 컨텍스트 스위치 숫자는 해석할 수 없다.** OSTEP 6장 과제가 `sched_setaffinity` 를 쓰라고 하는 이유다.

### 오차 요인 2 — 시계

```text
$ ./dt
CLOCK_REALTIME       getres  1000 ns   min step  1000 ns   12.2 ns/call
CLOCK_MONOTONIC      getres  1000 ns   min step  1000 ns   15.0 ns/call
CLOCK_MONOTONIC_RAW  getres    42 ns   min step    41 ns   9.7 ns/call
CLOCK_UPTIME_RAW     getres    42 ns   min step    41 ns   9.6 ns/call
mach_absolute_time   timebase 125/3  min step 1 tick = 41.67 ns   5.8 ns/call
back-to-back mach_absolute_time deltas: 0 tick 90876, 1 tick 9120, 2 ticks 4, >=3 0 (of 100000)
```

- macOS 의 `CLOCK_MONOTONIC` 은 **1 µs 단위** 다. 2.8 µs 왕복을 이걸로 재면 표본 하나에 ±35% 양자화 오차. `CLOCK_UPTIME_RAW`/`MONOTONIC_RAW`/`mach_absolute_time` 은 24 MHz 카운터(41.67 ns)다. `ticks_to_ns(1,125,3) = 41` 이 그 한 칸이다. (리눅스는 `CLOCK_MONOTONIC` 이 vDSO 로 ns 단위로 돌려주지만, arm64 VM 에서는 값이 41~42 ns 간격으로 보였다 — 위 1208/1249/1291. 밑의 카운터가 같은 24 MHz 로 추정.)
- 시계 읽기는 시스템 콜이 아니다 (5.8~9.6 ns): 커널이 매핑해 준 commpage 에서 카운터를 읽는다 — 리눅스의 vDSO 와 같은 역할 (OS-B03).
- **스핀 핑퐁의 min 0** 은 시계의 한계다: 83 ns 짜리 사건은 0, 1, 2, 3 tick 으로만 보인다. 이럴 땐 `batch_for_resolution` 으로 묶는다 — 42 ns 시계로 100 ns 연산을 1% 정확도로 재려면 42개를 묶어야 한다. 스핀의 신뢰할 숫자는 전체 시간/왕복 수인 mean 83~90 ns 다.

### 오차 요인 3 — 워밍업, DVFS, 부하

- **실행(run) 수준의 워밍업:** 조용할 때 thread run1·2 의 median 은 4250·4000 ns, run3~5 는 2833 ns. 같은 프로그램인데 처음 두 번이 느리다. 코어 클럭이 올라가는(DVFS) 데 시간이 걸리고 스케줄러가 패턴을 아직 모르기 때문으로 본다 — 그래서 **여러 번 돌리고 앞 run 은 따로 본다.**
- **왕복 수준의 워밍업:** 첫 왕복 7.7~163 µs (위). 처음 2000 왕복을 버린다.
- **백그라운드 부하:** 다른 에이전트의 벤치마크와 VM 이 돌아 load average 가 7~8 일 때 다시 잰 값:

```text
$ ./bench all 3 200000      (load average 6.9 → 8.1)
thread  run1..3  min 1958 1875 1917   med 4042 4458 4333   p99 17833 42583 41458 | same-core 78~86% | ping on E 3~13%
proc    run1..3  min 2208 1708 2333   med 4542 6958 5542   p99 31583 36042 42375 | same-core 62~84% | ping on E 7~54%
cond    run1..3  min 1833 1792 1792   med 4583 6250 10167  p99 21417~39833       | same-core 41~81%
```

median 은 1.4~4배, p99 는 1.5~5배 뛰었지만 **스레드 min 은 1.88~1.96 µs 로 거의 그대로** 다 (proc 은 한 run 에서 오히려 1708 ns 가 나왔다 — min 도 표본 하나라는 것을 잊지 말 것). min 은 "아무 방해가 없을 때의 고유 비용", median 과 p99 는 "그 시점 시스템 상태" 를 잰다. 부하가 오르자 same-core 비율이 떨어지고 E 코어 비율이 올랐다 — 코어 배치까지 바뀐 것이다.

정리:

| 오차 요인 | 증상 (이 맥 실측) | 통제 |
|---|---|---|
| 코어 고정 불가 | same-core 41~97% 로 흔들림, cross 가 1.2~3.3 µs 더 김 | 코어 번호 기록 후 분리 보고, 리눅스에선 `taskset -c` |
| P / E 코어 | bg: 100% E, 왕복 2.3~4.5배 | QoS 명시, "ping on E %" 함께 보고 |
| 시계 해상도 | `CLOCK_MONOTONIC` 1 µs, 스핀 min 0 | `mach_absolute_time`/`UPTIME_RAW`, 짧은 건 배치 |
| 워밍업 | 첫 왕복 최대 163 µs, 첫 run median +41~50% | 앞 2000 왕복 버림, 여러 run |
| 캐시 효과 | proc 첫 왕복(COW 폴트), cross-core 라인 이동 | 워밍업, 같은 코어 그룹만 비교 |
| 부하·선점 | max 1.7~7 ms, median 1.5~4배 | 조용한 시간, min 우선, load 기록 |
| 측정 자체 비용 | 시계 읽기 2번 ≈ 12 ns, 코어 번호 읽기 | 왕복(µs) 대비 1% 미만이라 무시 가능 |
| 기준선 조건 불일치 | bg selfpipe 787 vs 기본 440 ns | 같은 QoS·같은 코어 종류의 기준선만 뺀다 |

### Part B — 함수들

`summarize` 는 복사본을 정렬해서 인덱스로 뽑는다. nearest-rank p99 = 정렬된 m 개 중 `ceil(0.99·m)` 번째(1부터) = 인덱스 `(99m + 99)/100 − 1`. m = 10 이면 인덱스 9(최댓값), m = 1000 이면 인덱스 989. median 은 짝수 개면 낮은 쪽 `(m−1)/2` 로 정의했다 (정수 결과, 결정적 테스트). 평균의 합은 `uint64_t` 로 — 20만 개 × 수 ms 여도 안 넘친다.

`ticks_to_ns`:

```c
uint64_t q = t / denom, r = t % denom;
return q * numer + r * numer / denom;     /* t*numer 를 직접 만들지 않는다 */
```

`batch_for_resolution`: 표본 하나 = batch × op_ns 이고 그 위의 양자화 오차는 최대 한 눈금. `res / (batch·op) ≤ err_ppm/10⁶` 을 batch 에 대해 풀고 올림한다 — 1 µs 시계로 3 µs 연산을 0.1% 로 재려면 334개.

### Part C — 배치 전달과 재조립

```c
static unsigned recv_all(int rfd, sample_t *out, unsigned max) {
    uint8_t buf[RD_CHUNK + sizeof(sample_t)];
    size_t have = 0;                       /* 남은 조각 */
    ...
        ssize_t n = read(rfd, buf + have, RD_CHUNK);
        ...
        have += (size_t)n;
        size_t off = 0;
        while (have - off >= sizeof(sample_t)) { /* 완성된 샘플을 꺼낸다 */ ... off += sizeof(sample_t); }
        memmove(buf, buf + off, have - off);   /* 조각을 앞으로 */
        have -= off;
```

- 버퍼 크기는 `RD_CHUNK + 12`: 남은 조각은 최대 11바이트이므로 그 뒤에 `RD_CHUNK` 를 더 읽어도 넘치지 않는다.
- **PIPE_BUF:** POSIX 는 `PIPE_BUF`(이 맥 512바이트, `practice.c` 출력) 이하의 write 가 원자적(다른 writer 의 데이터와 섞이지 않음)이라고만 보장한다. 1000개 묶음(12000 B)은 여러 조각으로 들어가고, 읽는 쪽은 조각 경계에서 샘플이 잘린 채 받을 수 있다 — 테스트 `batch = 1000` 과 5바이트 dribble 이 그 경우다. writer 가 하나라 섞일 일은 없지만, writer 가 여럿이면 512 B 이하로 쓰거나 길이 헤더 + 락이 필요하다.
- `send_batched` 의 반환값(묶음 수)이 곧 "로거를 깨울 수 있는 최대 횟수" 다. 2만 샘플을 8개씩 → 2500번, 1개씩이면 2만 번.

`choose_batch` — 측정값을 설계로 바꾸는 곳:

```text
CPU:  (rate/B) · wake_ns / 1e9  ≤  budget_ppm / 1e6   →   B ≥ rate · wake_ns / (budget_ppm · 1000)
지연: (B−1) · 1e9 / rate + wake_ns  ≤  deadline_ns
```

| 상황 | rate | wake_ns | 예산 | 마감 | B |
|---|---|---|---|---|---|
| 4 kHz IMU, P 코어 깨움 ≈ 쌍 0.43 + 스위치 1.0 µs | 4000 | 1500 | 0.1% | 2 ms | **6** (지연 1.25 ms) |
| 같은 조건, 마감 1 ms | 4000 | 1500 | 0.1% | 1 ms | **0** — 지연은 B≤4, CPU 는 B≥6 |
| 1 kHz, 예산 1% | 1000 | 1500 | 1% | 5 ms | **1** |
| 32 kHz IMU, E 코어 깨움 ≈ 3 µs | 32000 | 3000 | 0.1% | 10 ms | **96** (지연 2.97 ms) |

두 번째 줄처럼 답이 0 이면 설계를 바꿔야 한다: 예산을 늘리거나, 스레드를 합치거나(전달 자체를 없앰), 깨움 없이 공유 메모리 링 + 폴링으로(DS-A01, 코어 하나를 태우는 대가 — 스핀 83 ns). 마감은 정수 비교 `(B−1)·10⁹ ≤ (deadline − wake)·rate` 로 해서 부동소수 반올림을 피했다. B 가 커질수록 지연만 늘어나므로 CPU 조건의 최소 B 하나만 검사하면 된다.

**펌웨어로 옮기면:** STM32 에서 ADC 샘플마다 ISR → `vTaskNotifyGiveFromISR` 로 태스크를 깨우면 샘플마다 예외 진입 + PendSV 스위치가 든다. DMA 를 half/full-transfer 인터럽트로 쓰면 B = 버퍼 절반 크기로 깨움이 줄어든다(BM-M09) — 위 식 그대로, wake_ns 만 수백 사이클로 바뀐다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_asan && ./so_asan
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan
```

실제 출력 (6회 연속 실행 모두 36/36, 시간 줄은 실행마다 다름):

```text
thread ping-pong wall: 97.29 ms for 20000 RT
thread ping-pong: 20000 RT, min 2250  med 4167  p99 10542 ns (참고값, CHECK 안 함)
proc   ping-pong: 20000 RT, min 2541  med 4208  p99 13375 ns (참고값, CHECK 안 함)
36/36 passed
```

ASan+UBSan, TSan 빌드 모두 36/36, 보고 없음. TSan 은 파이프 `read`/`write` 도 가로채 동기화로 모델링한다. 스레드 간 공유 데이터(`pong_arg_t.served`, `g_tx`/`g_rx`)는 `pthread_create`/`pthread_join` 이 happens-before 를 만든다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| fork 후 안 쓰는 파이프 끝을 안 닫음 | 자기 자신이 쓰기 끝을 들고 있어 EOF 가 영원히 안 옴 → 퐁이 멈춤 | 자식 `close(a2b[1]); close(b2a[0]);`, 부모 반대쪽 |
| 왕복 시간을 "스위치 1번" 으로 보고 | 왕복 = write+read 쌍 2개 + 스위치 2번 | `(rt − 2·pair) / 2` |
| 맥에서 `CLOCK_MONOTONIC` 으로 µs 이하 측정 | 1 µs 눈금 → 표본마다 ±35% | `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` / `mach_absolute_time` |
| 평균만 보고 | ms 단위 선점 몇 개가 평균을 끌어올림 (mean 4802 vs med 4000) | min / median / p99 |
| 워밍업 포함 | 첫 프로세스 왕복 163 µs (COW 폴트) | 앞 2000개 버림, 여러 run |
| "macOS 에서 affinity 로 고정했다" | affinity tag 는 Apple Silicon 에서 `KERN_NOT_SUPPORTED`, QoS 는 추천 | 코어 번호 기록 후 분리, 또는 리눅스 `taskset` |
| `read(fd, &sample, 12)` 가 12를 준다고 가정 | 스트림이라 아무 길이나 옴 → 샘플이 밀림 | 남은 조각을 들고 재조립 (`recv_all`) |
| `write` 반환값 무시 | 큰 쓰기는 짧은 쓰기·EINTR 가능 | `write_all` 루프 |
| SIGPIPE 를 그대로 둠 | 상대가 먼저 죽으면 내 프로세스도 조용히 종료 | `signal(SIGPIPE, SIG_IGN)` → EPIPE 처리 |
| 측정 루프 안에서 `printf` | 그 자체가 시스템 콜·락 → 측정 대상을 바꿈 | 배열에 기록, 끝나고 출력 |
| 다른 조건의 기준선을 뺌 | bg(E 코어) selfpipe 는 787 ns, 기본은 440 ns | 같은 QoS·같은 코어 종류끼리 |
| fork 전 `fflush` 안 함 | 버퍼가 자식에 복제돼 출력이 두 번 | `fflush(stdout); fork();` |

## 8. 면접 Q&A

<details>
<summary>Q1. 이 머신에서 컨텍스트 스위치 한 번은 얼마인가요? 어떻게 쟀죠?</summary>

두 스레드가 파이프 두 개로 1바이트 핑퐁을 20만 번, 왕복마다 `mach_absolute_time` 으로 쟀다. Apple M6 P 코어에서 왕복 min 1.92 µs / median 2.83 µs. 같은 스레드에서 블록 없이 write+read 하는 기준선이 0.42~0.43 µs 라서, `(왕복 − 2·기준선)/2` = 한 방향 "블록 → 깨움 → 스위치" 가 **약 0.54 µs(min 기준) ~ 0.98 µs(median 기준)**. E 코어(BACKGROUND QoS)에서는 약 2.4 µs. 단서 셋을 꼭 붙인다: 코어 고정이 안 돼서 왕복의 80~95% 만 같은 코어 깨움이었다, 순수 레지스터 교체가 아니라 스케줄러 경로 전체다, 부하에 따라 median 이 1.5~4배 흔들린다(min 은 안정). 꼬리: "getppid 는?" → 약 100 ns. 시스템 콜 한 번의 모드 전환보다 스위치가 5~10배 비싸다.

</details>

<details>
<summary>Q2. 프로세스 스위치와 스레드 스위치는 무엇이 다른가요? 측정에서 보였나요?</summary>

같은 프로세스의 스레드끼리는 주소 공간이 같아 레지스터·스택·스레드 상태만 바꾼다. 프로세스가 바뀌면 페이지 테이블 베이스(arm64 TTBR0, x86 CR3)도 바꾼다. ASID(x86 은 PCID)가 있으면 TLB 를 비우지 않아도 되지만, 새 주소 공간의 TLB·캐시 엔트리가 없으면 미스가 난다. 실측: min 1917 vs 2167 ns 로 왕복당 약 250 ns, median 은 조용할 때 2833 vs 2917 ns 로 거의 차이 없음. 첫 왕복은 프로세스가 94~163 µs 로 스레드(7.7~10 µs)보다 훨씬 컸다 — fork 후 COW 페이지 폴트(OS-M03). 꼬리: "ASID 가 없으면?" → 스위치마다 TLB flush, 그 뒤 페이지 워크 비용이 스위치 비용에 더해진다. 옛 x86(PCID 이전)과 KPTI 초기에 문제가 됐다. MCU(Cortex-M)는 MMU 가 없으니 "프로세스" 스위치 개념이 없다 — MPU 영역 재설정이 그 자리다.

</details>

<details>
<summary>Q3. 왜 두 스레드를 같은 코어에 묶어야 하나요? 맥에서는 어떻게 했죠?</summary>

다른 코어에 있으면 핑이 블록해도 퐁은 이미 다른 코어에서 깨어나므로 "스위치" 가 아니라 "다른 코어 깨우기(IPI, idle 탈출, 캐시 라인 이동)" 를 재게 된다. 리눅스 VM 실측: 같은 vCPU 1.7 µs, 다른 vCPU 14~16 µs. 맥에는 `sched_setaffinity` 가 없고, Mach affinity tag 는 Apple Silicon 에서 `KERN_NOT_SUPPORTED` 를 돌려준다(직접 확인). QoS 는 클러스터 추천일 뿐이다. 그래서 왕복마다 `pthread_cpu_number_np` 로 코어 번호를 기록하고 "같은 코어 깨움" 과 "다른 코어 깨움" 으로 나눠 보고했다: 2792 vs 4000~6875 ns. 꼬리: "그 코어 번호는 믿을 수 있나?" → 읽는 순간의 값일 뿐이고 바로 다음에 옮겨질 수 있다(헤더 주석도 그렇게 경고). 그래서 분류는 근사다.

</details>

<details>
<summary>Q4. 지연 측정 결과는 어떤 통계로 보고하나요? 평균은 왜 안 되죠?</summary>

min · median · p99 (+ max, 표본 수, 조건). min 은 방해가 없을 때의 고유 비용이라 부하가 바뀌어도 안정적이다(load 4 → 8 에서 1.9 µs 그대로). median 은 전형적인 경우, p99 는 꼬리 — 실시간 루프의 마감은 p99/최악값이 결정한다. 평균은 꼬리에 끌린다: 20만 개 중 몇 개가 2~6 ms 선점을 당하면 mean 이 median 보다 커진다 (실측 run 별로 7~32%). 꼬리: "p99 는 어떻게 계산?" → 정렬 후 nearest-rank `ceil(0.99·n)` 번째. 수백만 표본이면 정렬 대신 log2 히스토그램(연습 4)이나 HdrHistogram 으로 근사한다. 꼬리 2: "몇 번 돌려야 하나?" → 한 번의 실행 안에서도, 실행 사이에서도 흔들리므로(run1·2 가 +40%) 최소 3~5 run 과 각 run 의 통계를 함께 보인다.

</details>

<details>
<summary>Q5. 시스템 콜 비용은 얼마이고, 시계 읽기는 왜 그보다 훨씬 싼가요?</summary>

`getppid` 가 약 100 ns(이 맥, 100개 묶음 측정). 사용자 → 커널 모드 전환(arm64 `svc`), 시스템 콜 테이블 디스패치, 돌아오기(`eret`) 비용이다. 시계 읽기(`clock_gettime_nsec_np`, `mach_absolute_time`)는 5.8~9.7 ns 로 시스템 콜이 아니다 — 커널이 모든 프로세스에 읽기 전용으로 매핑해 둔 commpage 의 변환 계수와 하드웨어 타이머 카운터를 사용자 모드에서 바로 읽는다. 리눅스는 vDSO 가 같은 일을 한다. 꼬리: "그럼 `getpid` 로 시스템 콜 비용을 재면 안 되나?" → 일부 libc 는 getpid 를 캐시했었다(glibc 2.25 이전). 확실히 커널에 들어가는 호출을 고르거나 `strace -c`/`dtruss` 로 확인한다 (OS-B03).

</details>

<details>
<summary>Q6. 스핀 핑퐁은 83 ns 인데 왜 다들 블록하는 동기화를 쓰나요?</summary>

스핀은 커널을 안 거치고 캐시 라인만 오가서 빠르지만, 기다리는 동안 코어 하나를 100% 태운다. 기다림이 길면 전력·다른 스레드의 CPU 를 버리고, 단일 코어거나 기다리는 쪽이 상대보다 우선순위가 높으면 상대가 못 돌아 영원히 기다릴 수 있다(우선순위 역전, RTOS-M03). 그래서 실무는 **잠깐 스핀 후 블록** 한다 — 리눅스 futex 기반 adaptive mutex, macOS os_unfair_lock 의 짧은 스핀 (OS-A01). 기준: 예상 대기 < 스위치 비용(약 1 µs)이면 스핀이 이득. 꼬리: "83 ns 는 무엇의 비용?" → 공 변수의 캐시 라인이 한 코어의 L1 에서 다른 코어로 넘어갔다 돌아오는 시간(코어 간 일관성 트래픽). 같은 클러스터인지 다른 클러스터인지에 따라 달라진다.

</details>

<details>
<summary>Q7. RTOS(Cortex-M)에서는 컨텍스트 스위치를 어떻게 하고 어떻게 재나요?</summary>

Cortex-M 은 PendSV 예외로 스위치한다(ARM-A01): 예외 진입 시 하드웨어가 r0–r3, r12, lr, pc, xPSR 8워드를 자동으로 쌓고, PendSV 핸들러가 r4–r11 (FPU 를 쓴 태스크면 s16–s31, lazy stacking)을 저장한 뒤 다음 TCB 의 PSP 를 복원한다. MMU 도, 스케줄러 클래스도, 코어 간 IPI 도 없어서 결정적이고 수백 사이클 수준이다. 측정은 핑퐁과 같은 구조로: 태스크 A 가 `xTaskNotifyGive(B)` 후 `ulTaskNotifyTake` 로 블록, B 가 돌려준다. 시간은 `DWT->CYCCNT`(사이클 단위)로, 1000번 왕복의 min/max 를 본다(RTOS-A03). 이 레슨과의 차이: 코어 고정 문제가 없고(단일 코어), 시계 해상도가 1 사이클이며, 대신 플래시 대기 상태·캐시 유무가 오차 요인이다. 꼬리: "ISR 에서 깨운 태스크가 바로 실행되려면?" → `portYIELD_FROM_ISR(xHigherPriorityTaskWoken)` 로 PendSV 를 걸어야 한다(RTOS-M02).

</details>

<details>
<summary>Q8. 이 숫자로 실제 설계 결정을 하나 내려 보세요.</summary>

4 kHz IMU 샘플을 리더 스레드에서 로거 스레드로 넘긴다. 한 번 넘기는 비용이 약 1.5 µs(쓰기+읽기 0.43 µs + 깨움·스위치 1 µs)면, 샘플마다 깨우는 건 초당 6 ms = CPU 0.6%. 예산이 0.1% 면 `choose_batch` → 6개씩 묶어야 하고, 그러면 첫 샘플 지연이 1.25 ms. 마감이 1 ms 면 해가 없다(0) → 스레드를 합치거나 lock-free 링 + 주기 폴링(로거가 1 ms 마다 비우기)으로 바꾼다. 같은 계산이 MCU 에서 "샘플마다 ISR 로 태스크를 깨울까, DMA half-transfer 마다 깨울까" 를 정한다. 꼬리: "E 코어로 밀리면?" → 깨움 비용이 2~3배(3 µs)가 되니 같은 예산에서 B 도 2배. 그래서 지연 민감 스레드는 QoS 를 높이고(맥) 또는 `SCHED_FIFO`+affinity(리눅스).

</details>

## 9. 추가 연습

### 연습 1 — 같은 코어 / 다른 코어로 나눠 보고하기

`demo_bench.c` 처럼 왕복마다 `cpu_ping[i]`, `cpu_pong[i]` 를 기록했다. `split_by_core(rt, cpu_ping, cpu_pong, n)` 이 두 그룹의 개수와 (낮은) 중앙값을 돌려주게 하라. 그룹이 비면 중앙값 0.

<details>
<summary>답 보기</summary>

같은 코어면 배열 a, 아니면 b 에 넣고 각각 정렬해 인덱스 `(k−1)/2`. 고정 입력 8개(같은 코어 5, 다른 코어 3)에서 2800 / 4500 ns. 실측 해석: 같은 코어 그룹 median 이 "진짜 한 코어 스위치" 에 가깝고, 다른 코어 그룹은 IPI·idle 탈출이 섞인다. 보고서에는 두 그룹의 비율도 같이 쓴다 — 비율이 바뀌면 전체 median 도 바뀌기 때문이다 (부하 7~8 에서 same-core 41~86%).

</details>

### 연습 2 — 조건 변수 핑퐁

파이프 없이 `mutex + 조건 변수 2개 + turn` 으로 핑퐁하라. 각 쪽은 자기 차례에만 로그에 `P`/`Q` 를 남긴다. 5만 왕복 후 로그가 정확히 `PQPQ…` 여야 한다.

<details>
<summary>답 보기</summary>

핑: 락 → `log='P'`, `turn=1`, `signal(cv_pong)` → `while (turn != 0) wait(cv_ping)` → 언락. 퐁은 대칭. 조건 변수 규칙(락을 쥔 채 조건 검사, `while` 로 재검사 — POSIX 는 가짜 깨움(spurious wakeup)을 허용한다)은 OS-B06 의 bounded buffer 와 같다. 기다리는 쪽마다 조건 변수를 따로 둔 이유도 같다: 하나만 쓰면 `signal` 이 같은 편을 깨울 수 있다(여기선 둘뿐이라 문제가 덜하지만 습관). 실측 왕복 median 2125~2583 ns — 파이프(2833)보다 약간 싸지만 같은 µs 대: 비용은 "잠들고 깨우기" 에 있다.

</details>

### 연습 3 — 원자 변수 스핀 핑퐁

`atomic_uint ball` 하나로 핑퐁하라: 홀수 = 퐁 차례, 짝수 = 핑 차례. 공에 일반 변수 `payload` 를 실어 양방향으로 보내고, 20만 왕복 동안 받은 값이 한 번도 틀리지 않아야 한다 (TSan 깨끗).

<details>
<summary>답 보기</summary>

보내는 쪽: `payload = x; store(ball, next, release)`, 받는 쪽: `while (load(ball, acquire) != expect) {}` 후 `payload` 읽기. release/acquire 가 payload 의 happens-before 를 만든다(C-A03 의 1칸 채널과 같은 구조, 여기선 양방향). `ball` 을 `_Alignas(128)` 로 둔 건 Apple Silicon 캐시 라인이 128 B(`hw.cachelinesize`)라 다른 변수와 같은 라인을 나눠 쓰지 않게(false sharing) 하려는 것이다. 실측 왕복 83 ns, same-core 0% — 두 스레드가 다른 코어에서 계속 돈다. 시험 삼아 practice.c 의 release/acquire 를 전부 relaxed 로 바꿔 보면: TSan 빌드는 `WARNING: ThreadSanitizer: data race ... SUMMARY: ThreadSanitizer: data race pr_relaxed.c:192 in main` 를 보고하고, TSan 없이 `-O2` 로 3번 돌려도 매번 `FAIL ... bad == 0u` (13/14) 였다 — 이 맥에서 실제로 payload 가 깨진다 (TOOL-M09, C-A03).

</details>

### 연습 4 — log2 지연 히스토그램

`hist_log2(s, n, bins, nbins)`: bin k 는 [2^k, 2^(k+1)) ns, 0 과 1 은 bin 0, `nbins−1` 이상은 마지막 칸. 2048 은 bin 11, 5×10⁹ 는 마지막 칸.

<details>
<summary>답 보기</summary>

`k = floor(log2(v))` 를 시프트 루프로 구한다(`while (v >= 2) { v >>= 1; k++; }`; 실무에선 `63 − __builtin_clzll(v)`). 표본 수백만 개를 정렬 없이 O(n) 으로 요약하고 꼬리 모양을 본다. 칸 읽는 법: median 2.8 µs 는 bin 11(2048~4095 ns), p99 8~16 µs 는 bin 13, 2~6 ms 선점은 bin 21~22 에 들어간다. 경계가 2배씩이라 p99 를 정확히 읽을 수는 없다 — 정밀하게 하려면 bin 을 2^k 안에서 다시 나눈다(HdrHistogram 방식). RTOS-A03 의 지터 분석에서 같은 도구를 쓴다.

</details>

### 연습 5 — 블록하지 않는 기준선 확인

O_NONBLOCK 파이프에서 ① 빈 파이프 read 가 기다리지 않고 -1/EAGAIN 인지 ② write+read 쌍 10만 번이 한 번도 실패하지 않는지 ③ 1바이트씩 가득 채우면 몇 바이트에서 EAGAIN 인지 확인하라.

<details>
<summary>답 보기</summary>

`fcntl(fd, F_SETFL, fl | O_NONBLOCK)` 를 양 끝에. ①② 는 selfpipe 기준선이 정말 "스위치 없는" 경로라는 증거다 — 블록할 일이 없으면 스케줄러가 끼지 않는다. ③ 실측 출력: `pipe capacity with 1-byte nonblocking writes: 65536 bytes (PIPE_BUF=512)`. 용량(65536)과 원자성 한계(PIPE_BUF 512)는 다른 개념이다. 용량은 구현마다 다르다(리눅스 기본 64 KiB, `F_SETPIPE_SZ` 로 조정) — 그래서 CHECK 는 "512 이상" 만 본다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice        # 14/14 passed
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o practice_t && ./practice_t     # 14/14, 보고 없음
```

## 10. 핵심 정리

- 스위치는 따로 못 잰다 → **1바이트 파이프 핑퐁**: 기다리면 반드시 상대에게 넘어가는 구조를 만든다 (OSTEP 6장).
- 왕복 = **write+read 쌍 2개 + 블록→깨움→스위치 2번**. 같은 조건의 selfpipe 기준선을 빼서 `(RT − 2·쌍)/2`.
- 이 맥(Apple M6): getppid ≈ 100 ns, selfpipe 쌍 ≈ 0.43 µs, 스레드/프로세스 왕복 median ≈ 2.8~2.9 µs → 스위치 ≈ **0.5~1 µs**(P 코어), E 코어 ≈ 2.4 µs. 프로세스는 min 기준 +250 ns/왕복, 첫 왕복은 COW 폴트로 100 µs 대.
- macOS 는 **코어 고정 불가**(`sched_setaffinity` 없음, affinity tag `KERN_NOT_SUPPORTED`, QoS 는 추천) → 코어 번호를 기록해 같은 코어/다른 코어로 나눠 보고. 리눅스는 `taskset`: VM 에서 같은 vCPU 1.7 µs vs 다른 vCPU 15 µs.
- 시계: 맥 `CLOCK_MONOTONIC` 은 1 µs 단위 → `mach_absolute_time`/`CLOCK_UPTIME_RAW`(41.67 ns). 눈금보다 짧은 건 배치로 잰다.
- 보고는 **min / median / p99 + 조건(코어 종류, QoS, load)**. min 은 부하에도 안정, median·p99 는 시스템 상태를 잰다. 평균은 ms 선점에 끌린다. 워밍업과 첫 run 은 버리거나 따로.
- 측정은 설계로 이어진다: 깨움 비용 × 깨움 횟수 = CPU, 배치 크기는 CPU 예산과 지연 마감 사이에서 고른다 (MCU 의 DMA half/full 인터럽트와 같은 계산).

## 참고자료

- OSTEP 6장 *Mechanism: Limited Direct Execution* — Homework (Measurement): 시스템 콜·컨텍스트 스위치 측정, 파이프 두 개와 `sched_setaffinity`: <https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-mechanisms.pdf>
- OSTEP 30장 *Condition Variables* (연습 2): <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-cv.pdf>
- POSIX.1-2017 `pipe`, `read`, `write` (PIPE_BUF 원자성, EOF, EPIPE/SIGPIPE, EINTR): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/write.html>
- POSIX `pthread_cond_wait` (가짜 깨움 허용, 뮤텍스 보유 필수): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_cond_wait.html>
- Apple — `mach_absolute_time`, `clock_gettime_nsec_np`, QoS 클래스 (*Energy Efficiency Guide for Mac Apps — Prioritize Work with Quality of Service Classes*): <https://developer.apple.com/documentation/kernel/1462446-mach_absolute_time>
- Apple — *Thread Affinity API Release Notes* (affinity tag 는 힌트): <https://developer.apple.com/library/archive/releasenotes/Performance/RN-AffinityAPI/>
- `pthread_cpu_number_np` — SDK 헤더 `pthread/pthread.h` 주석 (macOS 11+)
- Linux `sched_setaffinity(2)`, `taskset(1)`, `perf bench sched pipe` (같은 핑퐁의 리눅스 표준 도구): <https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html>
- lmbench `lat_ctx` (McVoy & Staelin, *lmbench: Portable tools for performance analysis*, USENIX 1996)
- ARMv7-M Architecture Reference Manual — 예외 진입 스택 프레임, PendSV: <https://developer.arm.com/documentation/ddi0403/latest/>
- 관련 레슨: OS-B02 (파이프·dup2), OS-B03 (시스템 콜 비용), OS-B06 (조건 변수), OS-B14 (IPC 비교), OS-M03 (COW fork), OS-A01 (futex, 스핀 후 블록), C-A03 (release/acquire), TOOL-M09 (TSan), LPERF-B10 (런큐 대기 분해), RTOS-B03 (ucontext 스위치), RTOS-M01/M02 (태스크 알림, FromISR), RTOS-A03 (WCET·지터 측정), ARM-A01 (PendSV 어셈블리), BM-M09 (ADC+DMA 더블 버퍼), DS-A01 (lock-free SPSC 링)
