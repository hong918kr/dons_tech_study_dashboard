---
id: SIV-B09
time: 60분
updated: 2026-11-08
---
# SIV-B09 Poll-with-Timeout 헬퍼와 에러 처리

## 학습 목표

- `wait_for_field(reg, mask, value, timeout_us)` 를 **반드시 끝나는** 루프로 짜고, 끝났을 때 **마지막 읽은 값 · poll 횟수 · 경과시간 · 호출 위치(파일:줄 함수)** 를 남긴다.
- 시간을 **함수 포인터(now_us / delay_us)** 로 주입해서, 타임아웃 테스트를 실제로 기다리지 않고 0 ms 만에 · 매번 같은 결과로 돌린다.
- 타임아웃 루프의 함정 4가지 — **마감 후 재확인(false timeout)**, **32비트 시계 wrap**, **sleep 이 마감을 넘는 것**, **절대 만족할 수 없는 조건** — 을 테스트로 고정한다.
- 상태값을 **BUS_DEAD > HW_ERROR > OK > PENDING** 순서로 분류하고, 그 순서가 왜 중요한지 설명한다.
- PCIe 링크(LTSSM → L0)와 DMA 완료 대기에 결함을 주입하고, 디버그 엔지니어가 바로 행동할 수 있는 **한 줄 리포트**를 만든다.

## 1. 문제

first silicon 브링업 첫 주에 가장 많이 보는 버그는 "테스트가 **멈췄다**" 이다 — PLL이 lock 안 되고, 링크가 안 올라오고, DMA가 DONE을 안 세운다. Rivian/NVIDIA 검증 면접에서 "레지스터 상태를 기다리는 코드를 써보라"는 거의 공식처럼 나오고, 면접관은 **타임아웃 + 에러 경로 + 무엇을 로그에 남기는지**를 본다.

```c
while (!(readl(base + STATUS) & READY)) ;   /* 칩이 대답 안 하면? → 영원히 멈춤 */
```

검증 랩에서 이 한 줄은 이렇게 터진다: 밤새 돌던 회귀(regression) 스위트가 300번째 테스트에서 멈추고, 아침에 보면 로그는 마지막 PASS 줄에서 끊겨 있다. 어느 레지스터를 기다리다 멈췄는지 알려면 JTAG(OpenOCD)로 붙어서 PC를 봐야 한다. 그래서 검증 코드의 대기는 **항상 타임아웃을 갖고, 실패하면 "무엇을 · 어디서 · 얼마나 기다렸고 · 마지막에 무엇을 봤는지"** 를 남긴다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b09-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">wait_for_field_ex 한 바퀴 (Part B 완성형)</text>
  <g stroke="currentColor" stroke-width="1.3">
    <rect x="20" y="40" width="170" height="34" rx="6" class="f-mut"/>
    <rect x="20" y="100" width="170" height="44" rx="6" class="f-acc"/>
    <rect x="250" y="100" width="190" height="44" rx="6" class="f-mut"/>
    <rect x="500" y="40" width="220" height="30" rx="6" class="f-warn"/>
    <rect x="500" y="80" width="220" height="30" rx="6" class="f-warn"/>
    <rect x="500" y="120" width="220" height="30" rx="6" class="f-ok"/>
    <rect x="250" y="190" width="190" height="44" rx="6" class="f-mut"/>
    <rect x="500" y="190" width="220" height="44" rx="6" class="f-warn"/>
    <rect x="250" y="280" width="190" height="44" rx="6" class="f-mut"/>
    <rect x="20" y="280" width="170" height="44" rx="6" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="105" y="62">start = now_us()</text>
    <text x="105" y="118">v = read32(off)</text>
    <text x="105" y="136" font-size="12">polls++, last=v, elapsed</text>
    <text x="345" y="118">classify(v)</text>
    <text x="345" y="136" font-size="12">DEAD&gt;ERR&gt;OK&gt;PENDING</text>
    <text x="610" y="60">BUS_DEAD (v == 0xFFFFFFFF)</text>
    <text x="610" y="100">HW_ERROR (v &amp; err_mask)</text>
    <text x="610" y="140">OK ((v &amp; mask) == value)</text>
    <text x="345" y="208">PENDING: 마감 지났나?</text>
    <text x="345" y="226" font-size="12">elapsed &gt;= timeout</text>
    <text x="610" y="208">이미 last_look 였으면</text>
    <text x="610" y="226">TIMEOUT</text>
    <text x="345" y="298">delay(min(interval,</text>
    <text x="345" y="316">남은 시간))</text>
    <text x="105" y="298">예 → last_look = true</text>
    <text x="105" y="316" font-size="12">sleep 없이 한 번 더 읽기</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#b09-arr)">
    <path d="M105 74 L105 98"/>
    <path d="M190 122 L248 122"/>
    <path d="M440 112 L498 58"/>
    <path d="M440 118 L498 96"/>
    <path d="M440 128 L498 134"/>
    <path d="M345 144 L345 188"/>
    <path d="M440 212 L498 212"/>
    <path d="M345 234 L345 278"/>
    <path d="M250 300 C 210 300 205 170 150 146"/>
    <path d="M250 222 C 160 222 120 250 105 278"/>
    <path d="M105 280 L105 146"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="300" y="262">아니오</text>
    <text x="160" y="214">예 (처음)</text>
  </g>
  <text x="10" y="362" fill="currentColor">모든 출구에서 res 에 status · last · polls · elapsed · file:line · func 가 채워진다.</text>
  <text x="10" y="386" fill="currentColor">시계 = now_us()/delay_us() 함수 포인터. 테스트에선 읽기마다 1us 전진하는 가짜 시계.</text>
</svg>
```

**테스트 환경 (주어진 코드).** SIV-B02의 mock MMIO를 "읽기 + 스크립트된 상태 변화 + 결함 주입"만 남기고 축약해 넣었다. 버스는 함수 포인터 묶음이다 — 실보드에서는 `read32` 가 `*(volatile uint32_t *)(base+off)`, `now_us` 가 하드웨어 타이머(예: Cortex-M DWT CYCCNT를 µs로 환산, Linux `ktime_get`), `delay_us` 가 `udelay`/`usleep_range` 이다.

| mock 손잡이 | 의미 | 흉내내는 실제 상황 |
|---|---|---|
| `steps[] = {at_read, val}` | 그 레지스터의 at_read번째 읽기 직전에 HW가 값을 val로 바꿈 | 장치 상태 진행 (busy → done, Detect → L0) |
| `read_cost_us` (=1) | 읽기 1번이 가짜 시계를 1µs 전진 | 버스 읽기 지연 |
| `jump_after_read`, `jump_us` | 그 읽기 직후 시계가 확 튐 | 테스트 스레드 선점, 긴 ISR, 디버거 정지 |
| `now = 0xFFFFFF00` 로 시작 | 32비트 µs 카운터가 곧 wrap | 71.6분마다 넘어가는 free-running 타이머 |
| `rc_mask` | 그 비트는 읽으면 지워짐 (RC) | read-to-clear 상태 레지스터 |
| `dead = true` | 모든 읽기 0xFFFFFFFF | 전원/클럭 off 블록, PCIe 링크 다운 후 config read |

### Part A — 기본 wait_for_field + 진단 기록

`wait_for_field_at(bus, reg, mask, value, timeout_us, &res, file, line, func)` 를 구현한다. `WAIT_FOR_FIELD(...)` 매크로가 `__FILE__/__LINE__/__func__` 를 넣어준다.

- `(read & mask) == value` 가 되면 `WAIT_OK`, 경과시간 ≥ `timeout_us` 이면 `WAIT_TIMEOUT` (busy-poll, sleep 없음).
- 어느 출구에서든 `res` 에 status, reg 이름/오프셋, mask/value/timeout, **last**(마지막 원시값), **polls**, **elapsed_us**, **file/line/func** 가 있어야 한다.
- 테스트: N번째 읽기에 ready / 영원히 안 됨 / 여러 비트 필드(`[7:4]==5`) / 이미 ready / `timeout_us = 0`.

### Part B — 에러 비트 · 마감 후 재확인 · wrap · 간격 · 인자 검사

`wait_for_field_ex_at(bus, reg, &spec, &res, ...)` — `spec` 에 `mask, value, err_mask, timeout_us, interval_us, allones_dead`.

1. **에러 비트**: `err_mask` 중 하나라도 서면 타임아웃까지 기다리지 말고 즉시 `WAIT_HW_ERROR`. DONE과 ERR가 같이 서면 에러가 이긴다.
2. **버스 사망**: `allones_dead` 면 `0xFFFFFFFF` 는 `WAIT_BUS_DEAD` — ERR 비트 판정보다 먼저.
3. **마감 후 재확인**: 마감을 넘긴 걸 알아챈 순간, sleep 없이 **한 번 더 읽고** 판정한다 (선점 주입 테스트로 검증 — Part A 버전은 같은 주입에서 거짓 타임아웃을 낸다).
4. **wrap**: 시계가 `0xFFFFFF00` 에서 시작해도 정확히 1000µs 에서 끝나야 한다.
5. **간격**: `interval_us` 만큼 쉬되, 마지막 sleep 은 **남은 시간으로 자른다**.
6. **인자 검사**: `mask == 0`, mask 밖에 있는 value 비트, err_mask 와 겹치는 value → 버스에 손대기 전에 `WAIT_BAD_ARG`.
7. **RC 상태 비트**: 성공 후 헬퍼가 다시 읽지 않는다 — 결과는 `res.last` 에만 남는다.

### Part C — PCIe 링크 L0 대기 + DMA 완료 대기와 한 줄 리포트

mock 맵: `PCIE_LTSSM` (0x104, [5:0] LTSSM 상태, 나머지 reserved=0), `DMA_STATUS` (0x204, DONE[0] ERR[1] BUSY[2] CODE[11:8]).

- `pcie_wait_l0()` — LTSSM == L0(0x11), 100 ms, 100 µs 간격, all-ones = 버스 사망.
- `dma_wait_done()` — DONE, ERR 는 즉시 실패, 5 ms, 10 µs 간격.
- `wait_report(buf, n, &res, decode)` — 한 줄: `상태 reg= off= mask= exp= last= [해독] err= polls= elapsed=../..us at file:line func()`. BUS_DEAD 일 땐 해독하지 않는다(쓰레기를 해독하면 오진). 버퍼가 작아도 넘치지 않는다.
- 결함 주입: 정상 트레이닝 / **Polling.Compliance 에 갇힘** / **EP 전원 off** / DMA 정상 / **DMA DECERR**.

## 2. Problem (English)

> **Poll a hardware register with a timeout, and fail usefully.**
>
> You are given bus ops `{ read32(ctx, off), now_us(ctx), delay_us(ctx, us) }` where `now_us` is a free-running 32-bit microsecond counter that wraps.
>
> 1. Implement `wait_status_t wait_for_field(bus, reg, mask, value, timeout_us, wait_result_t *res)` (via a macro that captures `__FILE__`, `__LINE__`, `__func__`). Return `WAIT_OK` when `(read & mask) == value`, else `WAIT_TIMEOUT` once `timeout_us` has elapsed. In every case fill `res` with the last raw value read, the number of reads, the elapsed time, and the call site.
> 2. Harden it: `wait_for_field_ex(bus, reg, spec, res)` with an error mask (abort immediately with `WAIT_HW_ERROR`), an optional "all-ones means the bus is dead" rule (`WAIT_BUS_DEAD`, checked first), a poll interval whose last sleep is clipped to the remaining time, one final read after the deadline (so a preempted caller does not report a false timeout), wrap-safe elapsed time, and rejection of unsatisfiable specs (`WAIT_BAD_ARG`, zero bus accesses).
> 3. Use it to wait for a PCIe controller's LTSSM to reach L0 (100 ms) and for a DMA engine's DONE bit (5 ms, ERR aborts). Produce a one-line report a debug engineer can act on, e.g.
>    `TIMEOUT reg=PCIE_LTSSM off=0x104 mask=0x0000003F exp=0x00000011 last=0x00000003 [POLL_COMPLIANCE] polls=993 elapsed=100002us/100000us at solution.c:151 pcie_wait_l0()`
>
> Constraints: no real sleeping in tests — time is injected. The helper must terminate even if the register never changes.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

실제로 돌린 결과 (앞·뒤만):

```text
FAIL starter.c:211  WAIT_FOR_FIELD(&g_bus, &STATUS, ST_READY, ST_READY, 1000, &res) == WAIT_OK
FAIL starter.c:212  res.polls == 5 && res.last == ST_READY && res.elapsed_us == 5
FAIL starter.c:218  st == WAIT_TIMEOUT && res.status == WAIT_TIMEOUT
...
FAIL starter.c:335  strstr(line, "err=0x00000002") != NULL && strstr(line, "code=DECERR") != NULL

FAIL starter.c:340  wait_report(tiny, sizeof tiny, &res, dma_decode) > (int)sizeof tiny && strlen(tiny) == sizeof tiny - 1
3/39 passed
```

(통과하는 3개는 B7 — "BAD_ARG 를 돌려줘야 하는" 테스트라 스텁의 기본 반환값과 우연히 맞는다. exit 1.)

## 4. 힌트

<details>
<summary>힌트 1 — 경과시간은 "뺄셈 하나"</summary>

`elapsed = now_us() - start;` 를 `uint32_t` 로 계산하면 wrap 이 있어도 맞다 (모듈로 2^32 산술, C11 6.2.5p9). 마감 시각 `deadline = start + timeout` 을 만들어 `now < deadline` 으로 비교하는 순간 wrap 에서 깨진다.

</details>

<details>
<summary>힌트 2 — res 는 루프 들어가기 전에 채운다</summary>

`res_init()` 으로 무엇을 기다리는지(reg, mask, value, timeout, file, line, func)를 먼저 기록하고, 루프 안에서는 읽을 때마다 `polls++`, `last = v`, `elapsed` 만 갱신. 그러면 어느 `return` 으로 나가도 리포트가 완전하다.

</details>

<details>
<summary>힌트 3 — 마감 후 "한 번 더"</summary>

`if (elapsed >= timeout) { last_look = true; continue; }` — sleep 없이 루프 맨 위로 돌아가 한 번 더 읽는다. 그 읽기도 PENDING 이고 `last_look` 이 이미 참이면 그때 TIMEOUT.

</details>

<details>
<summary>힌트 4 — 분류 순서</summary>

`0xFFFFFFFF` 는 ERR 비트도 1 이고 DONE 비트도 1 이다. 그래서 **dead 를 먼저**, 그 다음 err, 마지막에 완료 조건. DONE+ERR 가 같이 선 경우 에러가 이겨야 "에러가 났는데 성공으로 보고" 하는 최악을 피한다.

</details>

<details>
<summary>힌트 5 — Part C 의 호출 위치</summary>

`pcie_wait_l0()` 안에서 `WAIT_EX(...)` 매크로를 쓰면 `__func__` 가 `"pcie_wait_l0"` 로 기록된다. 리포트에서 `strrchr(file, '/')` 로 경로를 잘라 basename 만 찍는다.

</details>

## 5. 풀이

### Part A — 반드시 끝나는 루프 + 진단 기록

```c
res_init(res, r, mask, value, 0, timeout_us, file, line, func);
uint32_t start = b->now_us(b->ctx);
for (;;) {
    uint32_t v = b->read32(b->ctx, r->off);
    res->polls++;  res->last = v;
    res->elapsed_us = b->now_us(b->ctx) - start;      /* unsigned 뺄셈 = wrap 안전 */
    if ((v & mask) == value) return res->status = WAIT_OK;
    if (res->elapsed_us >= timeout_us) return res->status = WAIT_TIMEOUT;
}
```

순서가 "읽기 → 조건 → 시간" 인 이유: `timeout_us = 0` 이어도 **최소 한 번은 읽고** 판정한다 (A5 테스트: polls==1). "시간 → 읽기" 순서면 timeout 0 은 한 번도 안 읽고 실패한다.

**왜 이 필드들을 남기나** — 실패 리포트의 각 칸이 디버그 엔지니어의 다음 행동으로 이어져야 한다.

| 기록 | 디버그 엔지니어가 하는 일 |
|---|---|
| `reg` 이름 + `off` | 스펙에서 레지스터를 찾고, 같은 주소를 JTAG/devmem 으로 직접 다시 읽어본다 |
| `mask` / `exp` | 테스트가 기대한 게 맞는지(테스트 버그?) 스펙과 대조 |
| `last` 원시값 | **가장 중요.** 0 이면 "아무 일도 안 일어남", 0xFFFFFFFF 면 "버스가 죽음", 중간값이면 "어느 단계에서 멈췄나" (LTSSM 상태, BUSY 비트) |
| `polls` | 0 이면 아예 안 읽음(인자 오류), 1 이면 첫 읽기에 끝남(에러/dead), 수천이면 정말로 기다렸음 |
| `elapsed/timeout` | 타임아웃이 너무 짧은지(스펙 시간 대비), 시계가 이상한지 |
| `file:line func()` | 수백 개 테스트 중 **어느 대기에서** 멈췄는지 — 같은 레지스터를 여러 곳에서 기다릴 때 필수 |

`__FILE__/__LINE__/__func__` 는 **매크로** 안에 있어야 호출한 쪽의 값이 들어간다. 함수 안에서 쓰면 항상 헬퍼 자신의 위치가 찍힌다 (실수 표 참고). Linux `WARN_ON`, `assert` 가 매크로인 것과 같은 이유다.

### Part B — 실전 타임아웃 루프의 함정들

**① 분류 순서: BUS_DEAD > HW_ERROR > OK > PENDING**

| 읽은 값 | 순서 맞음 | dead 검사를 뒤로 미루면 | err 를 OK 뒤로 미루면 |
|---|---|---|---|
| `0xFFFFFFFF` | BUS_DEAD ✔ | HW_ERROR (B3 테스트의 오진) | OK! — DONE 비트도 1 이라 **성공으로 보고** |
| `DONE \| ERR` | HW_ERROR ✔ | HW_ERROR | OK — 깨진 데이터로 다음 단계 진행 |
| `ERR \| code` | HW_ERROR, polls=4 ✔ | 같음 | 같음 |

0xFFFFFFFF 는 PCIe 에서 "응답 없음"의 대표값이다: 링크가 내려가거나 장치가 없으면 대부분의 루트 컴플렉스가 읽기에 all-ones 를 돌려준다. 클럭 게이트된 APB/AXI 블록은 SoC 에 따라 0, 0xDEADBEEF 같은 고정값, 또는 버스 에러(SLVERR → CPU abort) 로 나타난다. `allones_dead` 를 **옵션**으로 둔 이유: all-ones 가 정상값일 수 있는 레지스터(32비트 카운터, 데이터)에서는 켜면 안 된다. LTSSM 처럼 상위 비트가 reserved=0 인 레지스터에서만 켠다.

**② 마감 후 재확인 — false timeout**

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">B4 주입: 2번째 읽기 직후 테스트 스레드가 5 ms 선점됨 (timeout 1 ms)</text>
  <line x1="40" y1="70" x2="720" y2="70" stroke="currentColor"/>
  <text x="725" y="74" fill="currentColor" text-anchor="end" font-size="12" opacity=".7">시간 →</text>
  <rect x="60" y="56" width="12" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="90" y="56" width="12" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="110" y="50" width="430" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="325" y="75" fill="currentColor" text-anchor="middle">선점 5000us — 그 사이 장치는 이미 READY</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="66" y="104">read1</text><text x="96" y="104">read2</text>
    <text x="66" y="118">t=1</text><text x="96" y="118">t=2</text>
  </g>
  <rect x="40" y="140" width="320" height="66" rx="8" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="52" y="162" font-weight="bold">Part A (재확인 없음)</text>
    <text x="52" y="182" font-size="12">read2 후 elapsed=5002 ≥ 1000 → 바로 TIMEOUT</text>
    <text x="52" y="198" font-size="12">polls=2, 장치는 멀쩡한데 FAIL (거짓 타임아웃)</text>
  </g>
  <rect x="380" y="140" width="340" height="66" rx="8" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="392" y="162" font-weight="bold">Part B (last_look)</text>
    <text x="392" y="182" font-size="12">마감 넘김 감지 → sleep 없이 read3</text>
    <text x="392" y="198" font-size="12">READY → OK, polls=3</text>
  </g>
  <rect x="560" y="56" width="12" height="28" class="f-ok" stroke="currentColor"/>
  <text x="566" y="104" fill="currentColor" font-size="12" text-anchor="middle">read3</text>
  <text x="10" y="240" fill="currentColor">"시간이 지났다" 는 "장치가 안 됐다" 의 증거가 아니다 — 마지막으로 읽은 게 마감 훨씬 전일 수 있다.</text>
  <text x="10" y="262" fill="currentColor">마감 이후의 값을 한 번 보고 판정해야 실패가 장치의 실패가 된다.</text>
  <text x="10" y="284" fill="currentColor">Linux read_poll_timeout() 도 마감을 넘기면 op 를 한 번 더 호출한 뒤 cond 로 판정한다.</text>
</svg>
```

검증 랩에서 이 버그는 "가끔 실패하는 테스트(flaky)" 로 나타난다: 호스트 PC 에서 JTAG 로 돌리는 테스트, 리눅스 위 user-space 테스트, 인터럽트가 많은 펌웨어 — 모두 선점이 흔하다. 같은 보드에서 재현이 안 되니 HW 팀과 SW 팀이 서로 떠넘긴다. B4 의 두 번째 CHECK 는 **Part A 버전이 이 주입에 걸린다**는 걸 확인한다 — "결함 주입이 실제로 물린다(the injection bites)" 를 보여야 그 테스트를 믿을 수 있다.

**③ 32비트 µs 시계의 wrap**

| 방식 | start=0xFFFFFF00, 1000 µs 뒤 now=0x000002E8 | 결과 |
|---|---|---|
| `now - start >= timeout` (unsigned) | `0x2E8 - 0xFFFFFF00 = 0x3E8 = 1000` | ✔ 정확히 1000 |
| `deadline = start + timeout; now >= deadline` | deadline = 0x2E8 → 처음 읽자마자 now(0xFFFFFF01) ≥ 0x2E8 | ✘ 즉시 타임아웃 |
| `now - start` 를 `int` 로 저장 후 비교 | 구현 정의 변환 + 부호 혼동 | ✘ 위험 |

B5 는 wrap 을 지나는 TIMEOUT(polls=1001, elapsed=1001: 1000번 + 마감 후 1번) 과, 256번째 읽기에서 0 을 지나 300번째 읽기에 READY 인 OK 둘 다 검사한다. 32비트 µs 는 2^32 µs ≈ 71.6분마다 넘어간다 — 밤샘 회귀에서는 **반드시** 넘어간다.

**④ 간격과 마지막 sleep 자르기 (B6: timeout 1000, interval 300, 읽기 1µs)**

| 읽기 | 읽은 후 t | elapsed | 다음 동작 |
|---|---|---|---|
| 1 | 1 | 1 | sleep 300 (남은 999) |
| 2 | 302 | 302 | sleep 300 |
| 3 | 603 | 603 | sleep 300 |
| 4 | 904 | 904 | sleep **96** (남은 시간으로 자름) |
| 5 | 1001 | 1001 | 마감 → last_look |
| 6 | 1002 | 1002 | 여전히 PENDING → TIMEOUT |

총 sleep 996µs, polls 6. 자르지 않으면 마지막에 300µs 를 통째로 자서 1204µs 에 끝난다 — interval 이 크면(링크 대기 10ms 간격 등) 타임아웃이 스펙보다 눈에 띄게 늘어난다. 반대로 interval 0 (busy-poll) 은 반응은 빠르지만 버스를 점유한다: 공유 인터커넥트에서 CPU 가 같은 레지스터를 미친 듯이 읽으면 측정 대상 DMA 의 대역폭까지 바뀔 수 있다.

**⑤ 만족 불가능한 조건 = 반드시 타임아웃**

`mask=0x0F, value=0x10` 은 `(v & 0x0F) == 0x10` 이 될 수 없다 — 이 테스트는 장치와 상관없이 항상 100 ms 를 낭비하고 FAIL 하며, 리포트는 HW 를 탓한다. 오타 하나(`value = 1 << 4` 를 `mask = 0xF` 와 짝지음)로 흔히 생긴다. `value & err_mask` 도 마찬가지(에러 비트가 서야 성공인데 서면 즉시 에러). 버스 접근 0회로 거부해서 **테스트 버그를 HW 버그와 구분**한다.

**⑥ RC(read-to-clear) 상태 비트**

READY 가 RC 면 헬퍼가 읽는 순간 지워진다. 성공 뒤에 "확인 차 한 번 더" 읽는 헬퍼는 이벤트를 먹어버리고, 호출자가 다시 읽으면 0 이 나온다 (B8 의 두 번째 CHECK). 그래서 **결과 판단에 쓴 원시값을 res.last 로 돌려주고**, 호출자는 그 값을 쓴다 (RC 레지스터의 리셋값/RW 테스트 다루기는 SIV-B03·B04 참고).

**결함 모델 → 어느 테스트가 잡나**

| 주입한 결함/상황 | 잡는 테스트 | 헬퍼 결과 |
|---|---|---|
| 장치가 영영 ready 안 됨 | A2, C2 | TIMEOUT + last + 위치 |
| 중간에 ERR 비트 | B1, C5 | HW_ERROR, 타임아웃 기다리지 않음 |
| DONE+ERR 동시 | B2 | HW_ERROR |
| 전원/클럭 off (all-ones) | B3, C3 | BUS_DEAD, polls=1 |
| 테스트 스레드 선점 | B4 | OK (A 버전은 거짓 TIMEOUT) |
| 타이머 wrap | B5 | 정확한 elapsed |
| 잘못된 mask/value | B7 | BAD_ARG, 버스 접근 0 |
| RC 상태 비트 | B8 | last 에 보존 |

### Part C — PCIe 링크 + DMA, 실행 가능한 리포트

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b09-arr2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">PCIE_LTSSM[5:0] 진행 (C1 정상) 과 C2 의 갇힘</text>
  <g stroke="currentColor" stroke-width="1.2">
    <rect x="10" y="44" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="125" y="44" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="240" y="44" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="355" y="44" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="470" y="44" width="120" height="40" rx="6" class="f-mut"/>
    <rect x="605" y="44" width="120" height="40" rx="6" class="f-ok"/>
    <rect x="240" y="140" width="130" height="40" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="60" y="61">Detect.Quiet</text><text x="60" y="77" font-family="monospace">0x00 @r1</text>
    <text x="175" y="61">Detect.Active</text><text x="175" y="77" font-family="monospace">0x01 @r3</text>
    <text x="290" y="61">Polling.Active</text><text x="290" y="77" font-family="monospace">0x02 @r6</text>
    <text x="405" y="61">Polling.Config</text><text x="405" y="77" font-family="monospace">0x04 @r10</text>
    <text x="530" y="61">Cfg.Linkwd.Start</text><text x="530" y="77" font-family="monospace">0x07 @r14</text>
    <text x="665" y="61">L0</text><text x="665" y="77" font-family="monospace">0x11 @r20</text>
    <text x="305" y="157">Polling.Compliance</text><text x="305" y="173" font-family="monospace">0x03 @r9 → 영원히</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#b09-arr2)">
    <path d="M110 64 L123 64"/><path d="M225 64 L238 64"/><path d="M340 64 L353 64"/>
    <path d="M455 64 L468 64"/><path d="M590 64 L603 64"/><path d="M290 84 L300 138"/>
  </g>
  <text x="390" y="157" fill="currentColor" font-size="12">receiver 는 감지됐지만 TS1/TS2 교환 실패 →</text>
  <text x="390" y="173" fill="currentColor" font-size="12">레인/신호 무결성/refclk/상대편 의심</text>
  <text x="10" y="212" fill="currentColor">@rN = 그 레지스터의 N번째 읽기에서 HW 가 값을 바꿈 (mock steps).</text>
  <text x="10" y="234" fill="currentColor">인코딩은 DesignWare 계열 LTSSM 디버그 필드를 본뜸</text>
  <text x="10" y="256" fill="currentColor">(Linux pcie-designware.h 의 enum dw_pcie_ltssm, L0 = 0x11). 실제 칩은 IP 데이터북 값.</text>
  <text x="10" y="282" fill="currentColor">리포트의 핵심: last 원시값이 상태 이름으로 해독되어 찍힌다.</text>
</svg>
```

`pcie_wait_l0` / `dma_wait_done` 은 `wait_spec_t` 를 채워 `WAIT_EX` 를 부르는 3줄 함수다 — **대기 정책(시간, 간격, 무엇이 에러인지)은 IP 별 래퍼에, 루프는 한 곳에**. 테스트 수백 개가 같은 헬퍼를 쓰니 리포트 형식이 통일되고, SIV-B14 의 리포트 생성기가 파싱할 수 있다.

실제 실행한 Part C 리포트:

```text
-- Part C reports --
OK reg=PCIE_LTSSM off=0x104 mask=0x0000003F exp=0x00000011 last=0x00000011 [L0] polls=20 elapsed=1920us/100000us at solution.c:151 pcie_wait_l0()
TIMEOUT reg=PCIE_LTSSM off=0x104 mask=0x0000003F exp=0x00000011 last=0x00000003 [POLL_COMPLIANCE] polls=993 elapsed=100002us/100000us at solution.c:151 pcie_wait_l0()
BUS_DEAD reg=PCIE_LTSSM off=0x104 mask=0x0000003F exp=0x00000011 last=0xFFFFFFFF polls=1 elapsed=1us/100000us at solution.c:151 pcie_wait_l0()
OK reg=DMA_STATUS off=0x204 mask=0x00000001 exp=0x00000001 last=0x00000001 [busy=0 done=1 err=0 code=NONE] polls=6 elapsed=56us/5000us at solution.c:157 dma_wait_done()
HW_ERROR reg=DMA_STATUS off=0x204 mask=0x00000001 exp=0x00000001 last=0x00000202 [busy=0 done=0 err=1 code=DECERR] err=0x00000002 polls=3 elapsed=23us/5000us at solution.c:157 dma_wait_done()
```

각 줄을 받은 사람이 할 일:

| 리포트 | 해석 | 다음 행동 |
|---|---|---|
| TIMEOUT … `[POLL_COMPLIANCE]` polls=993 | 100 ms 동안 정말 기다렸고, Polling.Compliance 에서 멈춤 | 상대편 receiver/레인 연결, refclk, eye 확인. "link 안 올라옴" 보다 훨씬 좁은 범위 |
| BUS_DEAD polls=1 elapsed=1us | 첫 읽기부터 all-ones — 링크 대기 이전의 문제 | 전원 레일, 리셋 해제, 클럭 enable 먼저 (브링업 순서는 SIV-B10) |
| HW_ERROR … code=DECERR elapsed=23us | DMA 가 3번째 poll 에 에러, 5 ms 를 낭비하지 않음 | AXI DECERR = 그 주소에 응답할 slave 가 없음 → 디스크립터의 주소/주소맵 확인 (SLVERR 이면 slave 가 에러 응답) |

BUS_DEAD 줄에 `[...]` 해독이 없는 것은 의도다 — all-ones 를 LTSSM 으로 해독하면 `LTSSM_?` 같은 쓰레기, DMA 로 해독하면 "done=1 err=1 code=?" 라는 **그럴듯한 거짓말**이 찍힌다.

**실보드 시계 버전 (demo_realtime.c).** 같은 루프를 `clock_gettime(CLOCK_MONOTONIC)` + `nanosleep` 에 연결해 3 ms 뒤 ready 가 되는 가짜 장치를 기다렸다. 세 번 돌린 실제 출력:

```text
ready@3ms   timeout=10ms interval=200us -> OK polls=13 elapsed=3074us
never ready timeout=5ms  interval=200us -> TIMEOUT polls=18 elapsed=5094us (overshoot 94us)
ready@3ms   timeout=10ms interval=200us -> OK polls=13 elapsed=3090us
never ready timeout=5ms  interval=200us -> TIMEOUT polls=22 elapsed=5049us (overshoot 49us)
ready@3ms   timeout=10ms interval=200us -> OK polls=13 elapsed=3074us
never ready timeout=5ms  interval=200us -> TIMEOUT polls=22 elapsed=5035us (overshoot 35us)
```

관찰: 200 µs sleep 이 실제로는 ~250 µs 씩 걸려서 3 ms 에 13 poll(이론상 16), 타임아웃은 35~94 µs 초과, poll 수도 18/22 로 흔들린다. 이걸 CHECK 에 넣으면 flaky 테스트가 된다 — 그래서 체크되는 테스트는 가짜 시계를 쓰고, 39개 테스트(가짜 100 ms 링크 타임아웃 포함) 전체가 `0.004 total` 에 끝났다 (`time /tmp/SIV-B09_so`). `CLOCK_MONOTONIC` 을 쓰는 이유: `CLOCK_REALTIME` 은 NTP/사용자가 시계를 바꾸면 뒤로 갈 수 있다.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_realtime.c -o demo_realtime && ./demo_realtime
```

**실무 연결.**

- **Linux**: `include/linux/iopoll.h` 의 `read_poll_timeout(op, val, cond, sleep_us, timeout_us, ...)` / `readl_poll_timeout()` — 마지막 값이 `val` 에 남고, 마감 후 한 번 더 읽고, 0 또는 `-ETIMEDOUT`. 원자 문맥용 `readl_poll_timeout_atomic()` 은 sleep 대신 `udelay`.
- **U-Boot** `wait_for_bit_le32(reg, mask, set, timeout_ms, breakable)`, **Zephyr** `WAIT_FOR(expr, timeout_us, delay_stmt)` — 같은 패턴.
- **Pre-silicon (UVM)**: 시퀀스에서 `fork ... join_any` 로 "상태 대기" 와 `#timeout` 을 경쟁시키고 `` `uvm_error `` 로 레지스터 이름·기대값을 남긴다. 같은 의도의 C 테스트가 post-silicon 에서 JTAG/OpenOCD 로 혹은 온칩 펌웨어로 돈다 (재사용 경로는 SIV-B13).
- **ATE vs SLT vs 시스템 검증**: ATE 는 패턴 단위 timeout 이 테스트 프로그램에 고정, SLT/시스템 검증은 펌웨어 대기 헬퍼의 타임아웃·리포트 품질이 디버그 시간을 좌우한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san
```

실제 결과: 위 Part C 리포트 5줄 + `39/39 passed`, exit 0. ASan/UBSan 빌드도 `39/39 passed`, 경고 없음.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `while (!(REG & READY));` | 칩이 안 대답하면 테스트 러너 전체가 멈춤, 로그도 없음 | 타임아웃 + `wait_result_t` 리포트 |
| `for (i = 0; i < 100000; i++)` 반복 횟수로 타임아웃 | CPU 클럭·캐시·최적화(-O0/-O2)·버스 지연에 따라 실제 시간이 수십 배 달라짐. 실리콘 클럭이 아직 PLL 전(저속 OSC)이면 더 심함 | 하드웨어 타이머 기반 `now_us()` |
| `deadline = start + timeout; while (now() < deadline)` | 시계 wrap 근처에서 즉시 만료하거나 영원히 안 끝남 | `(uint32_t)(now() - start) >= timeout` |
| 마감 넘기면 바로 TIMEOUT | 선점·긴 ISR 뒤의 거짓 타임아웃 (flaky) | `last_look` 로 한 번 더 읽고 판정 |
| `if (done) OK; else if (err) ERR;` 순서 | DONE+ERR 동시 → 성공 보고, all-ones → 성공 보고 | dead → err → done 순서 |
| 함수 안에서 `__LINE__` 기록 | 항상 헬퍼 자신의 줄이 찍혀 어느 대기인지 모름 | 매크로로 호출 위치 전달 |
| 타임아웃 리포트에 "timeout!" 만 | 마지막 값이 없으면 "아무 일도 안 일어남" vs "중간 단계에서 멈춤" 구분 불가 | last 원시값 + 해독 + polls + elapsed |
| 성공 후 "확인 차" 다시 읽기 | RC 비트면 이벤트를 먹어버림 | 판정에 쓴 값을 `res.last` 로 반환 |
| ISR/atomic 문맥에서 sleep 하는 대기 | Linux 에서 `readl_poll_timeout` (sleep) 을 spinlock 안에서 쓰면 BUG | atomic 버전(`udelay`) 또는 인터럽트 기반 완료 통지 |
| 타임아웃 값을 "넉넉히 10초" | 실패 시 회귀 스위트가 몇 시간씩 늘어남 | 스펙 최대값 × 여유(예: 2~10배), 근거를 주석으로 |

## 8. 면접 Q&A

<details>
<summary>Q1. 레지스터 비트를 기다리는 함수를 짜보세요. 무엇을 반환하고 무엇을 로그에 남기나요?</summary>

루프: 읽기 → 조건 검사 → 경과시간 검사 → (sleep) 반복. 상태(OK/TIMEOUT/HW_ERROR/BUS_DEAD/BAD_ARG)를 반환하고, 레지스터 이름·오프셋, mask/기대값, **마지막 원시값**, poll 횟수, 경과/타임아웃 시간, 호출 위치(파일:줄 함수)를 남긴다. 꼬리질문 "그중 하나만 남길 수 있다면?" → 마지막 원시값. 장치가 어느 상태에서 멈췄는지가 디버그의 출발점이다.

</details>

<details>
<summary>Q2. test가 timeout 났을 때 어떤 정보를 로그에 남기나? 그걸로 무엇을 판단하나?</summary>

위 필드 + 가능하면 **관련 레지스터 스냅샷**(같은 블록의 STATUS/ERR/DEBUG 레지스터 몇 개), 보드·FW 버전·시드(SIV-B14). last 가 0 → 장치가 시작도 안 함(클럭/리셋/트리거 쓰기 누락), 중간값 → 상태 머신이 특정 단계에서 멈춤, 0xFFFFFFFF → 버스/전원. polls 와 elapsed 로 "정말 기다렸나" 와 "타임아웃이 스펙보다 짧지 않나" 를 판단한다. 꼬리질문 "타임아웃 직후 덤프할 때 주의할 점?" → RC 레지스터는 덤프가 상태를 바꾼다. 덤프 목록에서 빼거나 표시한다.

</details>

<details>
<summary>Q3. 타임아웃을 반복 횟수로 세면 안 되는 이유와, 시계가 wrap 할 때 비교는 어떻게 하나요?</summary>

반복 1회의 시간은 CPU 클럭, 최적화, 버스 지연, 캐시에 따라 변한다 — 특히 브링업 초기에는 PLL 전 저속 클럭이라 시간 감각이 완전히 다르다. 하드웨어 타이머를 쓰고 `(uint32_t)(now - start) >= timeout` 로 비교한다 (부호 없는 뺄셈은 모듈로 2^32 라 wrap 안전; 단 대기 시간이 2^32 µs 보다 짧아야 함). 꼬리질문 "Linux 의 `time_after(a,b)` 는?" → `(long)((b) - (a)) < 0`. 부호 있는 해석으로 "반 바퀴 이내" 순서를 판단한다 (추가 연습 1).

</details>

<details>
<summary>Q4. 마감이 지난 뒤 왜 한 번 더 읽나요? 그냥 timeout 을 늘리면 안 되나요?</summary>

마감을 감지한 시점의 "마지막 읽기"가 마감 훨씬 전일 수 있다(선점, 긴 ISR, 디버거 정지). 그 사이 장치가 준비됐으면 거짓 타임아웃이다. timeout 을 늘려도 선점 길이는 상한이 없으니 확률만 줄 뿐이다. 마감 **이후**에 읽은 값으로 판정하면 실패는 언제나 "마감 이후에도 조건 불만족" 이라는 사실이 된다. Linux `read_poll_timeout` 도 이렇게 한다.

</details>

<details>
<summary>Q5. 에러 비트와 완료 비트가 같이 서면? 0xFFFFFFFF 가 읽히면?</summary>

에러가 이긴다 — 완료+에러는 "끝났지만 결과가 틀렸다" 이고 성공으로 보고하면 깨진 데이터로 다음 단계가 진행된다. 0xFFFFFFFF 는 모든 비트가 1 이라 DONE 과 ERR 둘 다 만족하므로, all-ones 가 불가능한 레지스터라면 **가장 먼저** BUS_DEAD 로 분류한다. PCIe 에서 링크 다운/장치 없음의 읽기는 보통 all-ones, 클럭 게이트된 SoC 블록은 0/고정 패턴/버스 에러로 나타난다. 꼬리질문 "all-ones 가 정상값일 수 있는 레지스터는?" → 카운터, 데이터, 모든 비트가 정의된 마스크 레지스터. 그런 곳은 옆의 ID/VERSION 레지스터를 읽어 버스 생존을 따로 확인한다.

</details>

<details>
<summary>Q6. busy-poll 과 sleep-poll, 인터럽트 대기는 언제 쓰나요?</summary>

busy-poll: 수 µs 내로 끝나는 짧은 대기, 또는 sleep 불가 문맥(ISR, spinlock, 부트롬). sleep-poll: ms 단위(링크 트레이닝, PLL lock) — CPU 와 버스를 놓아준다. 지수 백오프로 버스 트래픽을 더 줄일 수 있지만 반응 지연이 커진다(추가 연습 2). 인터럽트: 운영 드라이버의 정석이지만, 검증에선 **인터럽트 경로 자체가 검증 대상**이라(SIV-B11) 폴링으로 상태를 확인하고 인터럽트는 따로 검사하는 경우가 많다. 꼬리질문 "폴링이 측정을 바꿀 수 있나?" → 공유 버스에서 busy-poll 이 DMA 대역폭·지연 측정을 왜곡할 수 있다. 성능 측정 중엔 interval 을 두거나 완료 인터럽트를 쓴다.

</details>

<details>
<summary>Q7. 타임아웃 테스트를 CI 에서 결정적으로 돌리려면? 실제로 100 ms 를 기다리나요?</summary>

시간을 의존성 주입한다: `now_us()`/`delay_us()` 를 함수 포인터로 받고, 테스트에선 mock 이 읽기마다 시계를 전진시키는 가짜 시계를 넣는다. 그러면 100 ms 타임아웃이 0 ms 에 끝나고 poll 횟수까지 매번 같아서 정확한 값(`polls == 993`)을 CHECK 할 수 있다. 실시간 버전은 demo 로만 — 실제로 돌려보면 sleep 오버슈트로 poll 수가 18/22 로 흔들린다. 꼬리질문 "선점 같은 타이밍 버그는 어떻게 재현?" → 가짜 시계에 "k번째 읽기 뒤 시계 점프" 를 주입한다 (B4).

</details>

## 9. 추가 연습

### 연습 1 — wrap 안전한 time_after(a, b)

"a 가 b 보다 나중인가" 를 2^31 µs 이내 차이라는 가정 하에 wrap 안전하게. 구현 정의 변환 없이.

<details>
<summary>답 보기</summary>

`(uint32_t)(a - b - 1u) < 0x7FFFFFFFu` — `(a - b) mod 2^32` 가 1 ~ 2^31−1 이면 a 가 나중. Linux 는 `(long)((b) - (a)) < 0` 로 쓰는데, unsigned → signed 변환에서 범위 밖 값은 C11 6.3.1.3p3 에 따라 **구현 정의**다 (실제 컴파일러는 모두 모듈로로 처리). `5 > 0xFFFFFFF0` 같은 순진한 비교는 wrap 에서 틀린다.

</details>

### 연습 2 — 지수 백오프(exponential backoff)

interval 을 10 µs 에서 시작해 매번 2배, 최대 1000 µs. 고정 10 µs 와 poll 수·반응 지연을 비교하라.

<details>
<summary>답 보기</summary>

`iv = (iv > max/2) ? max : iv*2` (오버플로 없이 2배). 10 ms 타임아웃에서 고정 10 µs 는 911 poll, 백오프는 18 poll (실행 결과). 대가: t=700 µs 에 ready 가 된 장치를 고정 간격은 ≤ 11 µs 안에, 백오프는 다음 깨어날 때인 t=1278 µs 에 본다. 링크 트레이닝·PLL 처럼 "언제 끝날지 모르는 긴 대기" 에 적합하다.

</details>

### 연습 3 — K회 연속 안정(debounce) 대기

PLL lock 이 t=20 에 섰다가 t=40 에 풀리고 t=60 부터 안정. 3회 연속 lock 일 때만 OK.

<details>
<summary>답 보기</summary>

`run = match ? run + 1 : 0; if (run >= k) OK;` 단순 대기는 t=21 의 **거짓 lock** 에 속아 3번째 읽기에서 OK, 안정 대기는 9번째 읽기(t=81)에서 OK. 계속 흔들리면 TIMEOUT 이고 last=0. 마감 후 재확인은 넣지 않는다 — 한 번 읽어서는 "안정" 을 증명할 수 없기 때문.

</details>

### 연습 4 — heartbeat: 카운터가 움직이는가

free-running 카운터(50 µs 마다 +1)를 읽어 값이 바뀌면 OK. 클럭이 게이트되어 카운터가 멈추면 TIMEOUT.

<details>
<summary>답 보기</summary>

첫 값을 기억하고 `v != first` 를 기다린다. 클럭 게이트된 블록은 레지스터 읽기가 "그럴듯한 고정값" 을 돌려줄 수 있어서 값 자체로는 생사를 모른다 — **변화**를 봐야 한다. 브링업에서 "이 블록에 클럭이 들어갔나" 를 확인하는 가장 싼 방법(SIV-B10).

</details>

### 연습 5 — 여러 단계가 전체 마감을 나눠 쓰기

PLL(t=300) → RESET_DONE(t=800) → LINK(t=5000) 를 전체 2 ms 예산 안에서. 각 대기의 timeout 은 "남은 예산".

<details>
<summary>답 보기</summary>

`deadline_t {start, budget}` 와 `deadline_left()` (used ≥ budget 이면 0). 앞 두 단계가 818 µs 를 써서 링크 대기엔 1182 µs 만 남고, 전체는 2002 µs (마감 후 재확인 1회 + 마지막 읽기) 에 끝난다. 단계마다 고정 timeout 을 주면 최악 시간이 합으로 불어나 회귀 스위트 시간을 예측할 수 없다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
```

실제 출력:

```text
P2 fixed10: polls=911  backoff: polls=18
P5 left_for_link=1182us total=2002us
20/20 passed
```

## 10. 핵심 정리

- HW 대기는 **항상 타임아웃**. 반복 횟수 말고 하드웨어 타이머, 비교는 `(uint32_t)(now - start) >= timeout` (wrap 안전).
- 실패 리포트 = reg 이름·오프셋 · mask/exp · **last 원시값(+해독)** · polls · elapsed/timeout · `file:line func()` (매크로로 캡처).
- 분류 순서 **BUS_DEAD(all-ones) > HW_ERROR > OK > PENDING** — 에러가 완료를 이긴다.
- 마감을 넘기면 **한 번 더 읽고** 판정 → 선점으로 인한 거짓 타임아웃 제거 (Linux `read_poll_timeout` 도 동일).
- 마지막 sleep 은 남은 시간으로 자르고, 만족 불가능한 mask/value 는 버스 접근 전에 BAD_ARG.
- RC 상태는 판정에 쓴 값을 `res.last` 로 돌려준다 — 다시 읽지 않는다.
- 테스트는 시간 주입(가짜 시계)으로 결정적으로: 100 ms 타임아웃도 0 ms, poll 수까지 정확히 CHECK.

## 참고자료

- [Linux `include/linux/iopoll.h` — read_poll_timeout / readl_poll_timeout](https://github.com/torvalds/linux/blob/master/include/linux/iopoll.h)
- [Linux `include/linux/jiffies.h` — time_after / time_before (wrap 안전 비교)](https://github.com/torvalds/linux/blob/master/include/linux/jiffies.h)
- [Linux `drivers/pci/controller/dwc/pcie-designware.h` — enum dw_pcie_ltssm](https://github.com/torvalds/linux/blob/master/drivers/pci/controller/dwc/pcie-designware.h)
- [U-Boot `include/wait_bit.h` — wait_for_bit_le32](https://github.com/u-boot/u-boot/blob/master/include/wait_bit.h)
- [Zephyr `include/zephyr/sys/util.h` — WAIT_FOR](https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/sys/util.h)
- PCI Express Base Specification — LTSSM (Detect / Polling / Configuration / L0) 장(chapter) "Link Training and Status State Machine"
- Arm AMBA AXI Protocol Specification — RRESP/BRESP: OKAY, EXOKAY, SLVERR, DECERR
- `clock_gettime(2)` — CLOCK_MONOTONIC vs CLOCK_REALTIME
- 관련 레슨: SIV-B02 (mock MMIO), SIV-B03/B04 (RC/W1C 비트 다루기), SIV-B08 (테스트 프레임워크), SIV-B10 (브링업 순서), SIV-B11 (인터럽트), SIV-B14 (리포트), C-M02 (W1C/RMW)
