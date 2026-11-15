---
id: RTOS-M08
time: 90분
updated: 2026-11-11
---
# RTOS-M08 태스크 스택 예산: ISR 중첩·예외 프레임 포함 최악 스택

## 학습 목표

- `clang/gcc -fstack-usage` 의 `.su` 와 objdump 로 뽑은 호출 그래프를 읽어 **태스크별 최악 호출 경로(worst-case path)** 를 계산하는 도구를 C 로 짠다 — 재귀·모르는 함수(라이브러리, 함수 포인터)·VLA 는 에러로 멈춘다.
- `.su` 숫자가 "버퍼 크기" 와 다른 이유(최적화기가 지운 버퍼, 컴파일러가 넣은 `memcpy`, naked 함수 0 B, 꼬리 호출, `for(;;)` 가 재귀로 보이는 착시)를 실제 출력으로 설명한다.
- Cortex-M4F 의 예외 프레임(기본 8 워드 / FP 확장 26 워드 + 정렬 4 B)과 FreeRTOS ARM_CM4F PendSV 의 소프트웨어 저장(r4–r11, r14 + s16–s31)을 바이트로 더한다.
- **Cortex-M 에서 ISR 은 MSP 에서 돈다 — 태스크 스택(PSP)에는 첫 예외 프레임만 쌓인다** 를 두 모델(ISR on MSP / ISR on task stack)의 예산표 차이로 보여 준다.
- 여유율 정책(+25 %, 최소 64 B)을 `-O0`·gcc 빌드 숫자로 정당화하고, FreeRTOS POSIX 포트 HWM(무의미)과 arm64 칠하기(painting) 실측으로 정적 값과 대조한다.

## 1. 문제

"태스크 스택 몇 바이트로 잡으셨어요? 근거는요?" — RTOS 펌웨어 면접의 단골 꼬리질문이다. "예제에서 1 KB 복사했어요" 나 "HWM 보고 줄였어요" 에서 멈추면 감점이고, "정적 분석으로 최악 호출 경로 + 예외 프레임(FPU 면 104 B) + 커널 문맥, ISR 은 MSP 라 첫 프레임만, 거기에 여유 25 %, 보드에서 HWM 으로 교차 확인" 까지 말하면 합격선이다. 스택 오버플로는 조용히 옆 태스크의 TCB 를 덮는 가장 흔한 RTOS 현장 버그이고(탐지는 RTOS-M09), Toyota ETCS 소송(2013, Michael Barr 증언)에서도 스택 여유가 쟁점 중 하나였다 (공개 증언 자료 기준).

이 레슨의 대상은 실제로 컴파일되는 작은 펌웨어 `fw_graph.c` 다 — IMU SPI 읽기 → 상보 필터 → PID 3개 → `log_printf` 포매터(96 B 줄 버퍼), 텔레메트리 패킷 → CRC16 → COBS → UART, 명령 프레임 파서, ADC DMA ISR. 같은 소스를 Cortex-M4F(clang `--target=thumbv7em-none-eabi`)와 맥 arm64 로 컴파일해 `.su` 를 얻고, FreeRTOS V11.3.2 커널까지 같이 컴파일해 태스크 루프(`cm4/fw_tasks.c`)의 커널 API 비용도 넣는다. TOOL-A08(함수별 스택 도구)은 아직 없으므로 함수별 부분도 여기서 직접 한다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">ctrl 태스크의 최악 순간 (Cortex-M4F + FreeRTOS, 1 px = 2 B) — solution.c -v 의 실제 숫자</text>
  <text x="30" y="42" fill="currentColor" font-weight="bold">태스크 스택 (PSP) 616 B = 154 워드</text>
  <rect x="30" y="50" width="230" height="32" class="f-acc" stroke="currentColor"/><text x="38" y="70" fill="currentColor">ctrl_task 8 + control_step 56</text><text x="268" y="70" fill="currentColor">64</text>
  <rect x="30" y="82" width="230" height="60" class="f-acc" stroke="currentColor"/><text x="38" y="116" fill="currentColor">log_printf (줄 버퍼 96)</text><text x="268" y="116" fill="currentColor">120</text>
  <rect x="30" y="142" width="230" height="28" class="f-acc" stroke="currentColor"/><text x="38" y="160" fill="currentColor">log_vformat</text><text x="268" y="160" fill="currentColor">56</text>
  <rect x="30" y="170" width="230" height="22" class="f-acc" stroke="currentColor"/><text x="38" y="185" fill="currentColor">fmt_u32 (경로 합 284)</text><text x="268" y="185" fill="currentColor">44</text>
  <rect x="30" y="192" width="230" height="54" class="f-warn" stroke="currentColor"/><text x="38" y="214" fill="currentColor">예외 프레임 FP 확장 26 워드</text><text x="38" y="232" fill="currentColor">+ 정렬 패딩 4 (하드웨어)</text><text x="268" y="222" fill="currentColor">108</text>
  <rect x="30" y="246" width="230" height="50" class="f-ok" stroke="currentColor"/><text x="38" y="266" fill="currentColor">PendSV: r4–r11, r14 (36)</text><text x="38" y="284" fill="currentColor">+ s16–s31 (64)  (port.c 518–520)</text><text x="268" y="274" fill="currentColor">100</text>
  <rect x="30" y="296" width="230" height="62" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/><text x="38" y="322" fill="currentColor">여유 +25 % (최소 64)</text><text x="38" y="340" fill="currentColor">492 → 616 (8 배수)</text><text x="268" y="330" fill="currentColor">124</text>
  <text x="30" y="378" fill="currentColor">↓ 스택은 아래(낮은 주소)로 자란다. 바닥 = pxStack (HWM 은 여기서부터 0xa5 를 센다)</text>
  <text x="440" y="42" fill="currentColor" font-weight="bold">MSP (모든 ISR 공용) 252 B — 연습 3</text>
  <rect x="500" y="50" width="200" height="22" class="f-mut" stroke="currentColor"/><text x="508" y="65" fill="currentColor">SysTick/PendSV 레벨 44</text>
  <rect x="500" y="72" width="200" height="18" class="f-warn" stroke="currentColor"/><text x="508" y="86" fill="currentColor">예외 프레임 36</text>
  <rect x="500" y="90" width="200" height="48" class="f-mut" stroke="currentColor"/><text x="508" y="118" fill="currentColor">isr_adc_dma (prio 6) 96</text>
  <rect x="500" y="138" width="200" height="18" class="f-warn" stroke="currentColor"/><text x="508" y="152" fill="currentColor">예외 프레임 36</text>
  <rect x="500" y="156" width="200" height="20" class="f-mut" stroke="currentColor"/><text x="508" y="171" fill="currentColor">USART2 (prio 5) 40</text>
  <line x1="262" y1="219" x2="492" y2="62" stroke="currentColor" stroke-dasharray="5 3"/>
  <polygon points="492,62 482,64 488,72" fill="currentColor"/>
  <text x="292" y="238" fill="currentColor">IRQ 가 태스크를 선점하면</text>
  <text x="292" y="254" fill="currentColor">첫 프레임만 PSP 에,</text>
  <text x="292" y="270" fill="currentColor">핸들러·중첩은 MSP 로</text>
  <text x="452" y="214" fill="currentColor">ISR 이 태스크 스택에서 도는 포트라면</text>
  <text x="452" y="232" fill="currentColor">이 칸 전체(+ 첫 프레임)가 PSP 쪽에 쌓여</text>
  <text x="452" y="250" fill="currentColor">ctrl 예산이 492 → 644 B 로 커진다 (Part C)</text>
  <text x="452" y="286" fill="currentColor">FP 를 쓰는 태스크면 프레임 104+100 B,</text>
  <text x="452" y="304" fill="currentColor">정수만 쓰는 rx 태스크는 36+36 B.</text>
  <text x="452" y="322" fill="currentColor">float 곱셈 하나가 136 B 를 더한다.</text>
</svg>
```

### Part A — `.su` 파서와 최악 경로 (DFS)

- `su_parse_line(line, &entry)` — **TODO 1.** clang `file:line:name⇥bytes⇥qual`, gcc `file:line:col:name⇥…`, C++ 이름(`int Spi::xfer(uint8_t*, int)`), 한정자 `static` / `dynamic` / `dynamic,bounded`. 잘못된 줄(탭 없음, 숫자 아님, 음수, 빈 이름, 모르는 한정자, 너무 긴 이름)은 -1. 여러 `.su` 를 합칠 때 같은 이름(다른 파일의 static 함수)은 큰 값을 남긴다(주어진 `su_add`).
- `dfs()` — **TODO 2.** `best[u] = self(u) + max(best[v])`, 메모(BLACK) 로 공유 하위 트리는 한 번만. GRAY 재방문 = 재귀 → `WCS_RECURSION`, callee 가 `.su` 에도 `assume`(사람이 확인한 값) 에도 없으면 `WCS_UNKNOWN`, `dynamic` 이면 `WCS_DYNAMIC`.
- 테스트: 실제 M4 데이터에서 `ctrl_task_once` = 276 B (`… > log_printf > log_vformat > fmt_u32`), `assume` 없이는 **`__aeabi_memcpy` 때문에 UNKNOWN**, `fmt_u32_rec` 는 RECURSION, arm64 는 400 B, 무작위 DAG 300개를 전수 탐색과 대조.

### Part B — 예외 프레임 · 커널 문맥 · ISR 중첩

- `exc_frame_bytes(fpu_ctx, align_pad)` — **TODO 3.** 32 / 104 B (+4).
- `switch_frame_bytes(task_fpu)` — **TODO 4.** FreeRTOS ARM_CM4F PendSV 가 PSP 에 더 쌓는 36 / 100 B. 확인: `36 + 36 = 72 B = pxPortInitialiseStack 의 첫 프레임 18 워드`.
- `isr_nest_bytes(isrs, n, task_fpu, pad)` — **TODO 5.** ISR 이 태스크 스택 위에서 돈다면: NVIC 우선순위 레벨마다 하나씩, 덜 급한 레벨부터 중첩. 레벨 k 의 프레임 크기는 **바로 아래 문맥이 FP 를 썼나** 로 정해진다 → path 가 큰 ISR 보다 FP 를 쓰는 ISR 이 최악일 수 있다(2-상태 DP).

### Part C — STM32F4 급 비행 제어기의 태스크별 예산표

태스크 3개(ctrl 1 kHz 제어 · FPU / comms 텔레메트리 · FPU / rx 명령 파서 · 정수), ISR 3 레벨(USART2 prio 5, ADC DMA prio 6, SysTick·PendSV prio 15). 커널 + 태스크 + fw_graph 의 `.su` 48개와 간선 71개(실제 출력)를 합친다.

- `apply_margin(raw, &m)` — **TODO 6.** `round_up(raw + max(ceil(raw·25 %), 64), 8)`.
- `task_budget(...)` — **TODO 7.** `path + max(ctx, isr)` → 여유 → FreeRTOS `usStackDepth` 워드. 두 ISR 모델을 나란히.
- 비교: FreeRTOS POSIX 포트 `uxTaskGetStackHighWaterMark`(데모), arm64 칠한 스택 실측(데모) — 표 `함수 | .su M4 | .su arm64 | 정적 경로 arm64 | 실측`.

## 2. Problem (English)

> **Worst-case task stack budget from compiler stack-usage data.**
>
> You are given the `-fstack-usage` output (`file:line[:col]:function<TAB>bytes<TAB>static|dynamic|dynamic,bounded`) of a Cortex-M4F firmware image and its direct-call graph as `caller callee` pairs extracted from the object code. Implement:
>
> 1. `int su_parse_line(const char *line, su_entry_t *out)` — accept clang and gcc formats (gcc adds a column; C++ names contain `::` and spaces); reject malformed lines with -1.
> 2. `int worst_stack(const graph_t *g, const char *root, wcs_t *out)` — the maximum sum of frame sizes over all call paths from `root`, plus the path. Report an error (not a number) for recursion, for a callee with no stack data (library call, function pointer) unless the user supplied an assumed value, and for an unbounded dynamic frame.
> 3. `exc_frame_bytes(fpu, pad)` (8 or 26 words, +4 alignment), `switch_frame_bytes(fpu)` for FreeRTOS's ARM_CM4F PendSV (r4–r11, r14, and s16–s31 when the task has FP context), and `isr_nest_bytes()` for a port where interrupts run on the interrupted task's stack (one ISR per preemption level; the frame size at each level depends on whether the interrupted context had FP state).
> 4. `task_budget()` = `path + max(context, isr_nesting)` (isr_nesting = 0 when ISRs run on MSP), then a margin of +25 % (at least 64 B) rounded to 8 B, in FreeRTOS words.
>
> Example: `ctrl_task` path 284 B (`ctrl_task > control_step > log_printf > log_vformat > fmt_u32`), FP task → context 108 + 100 = 208 B, raw 492 B, budget 616 B = 154 words. With ISRs on the task stack: nesting 360 B, raw 644 B, 808 B.
>
> Follow-ups: Which stack does a Cortex-M interrupt handler use? Why does a single float multiply in a task add ~136 B to its stack? Why is the static number larger than what you measure with stack painting? Why is FreeRTOS's high-water mark meaningless on the POSIX simulator? How do you size the MSP? What do you do with recursion and function pointers?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter     # 15/65 passed, exit 1
./starter -v                                                              # (풀고 나면) 경로표·예산표 출력
```

처음엔 "잘못된 줄 거부" 테스트 몇 개만 통과한다(파서 stub 이 늘 -1 이라서). `DATA` 블록의 문자열은 손으로 만든 예제가 아니라 아래 스크립트의 **실제 출력을 그대로 붙인 것** 이다. pthread 도 FreeRTOS 도 필요 없는 단일 파일이다. 재료가 되는 파일:

| 파일 | 역할 |
|---|---|
| `fw_graph.c` / `fw_graph.h` | 분석 대상 펌웨어 (freestanding, 모든 함수 `noinline`) |
| `measure_su.sh` | fw_graph.c → M4 `-O2` / M4 `-O0` / arm64 `.su` + 간선 |
| `callgraph.sh` | `.o` → `caller callee` (llvm-objdump 재배치 + 해결된 `bl`) |
| `cm4/kernel_su.sh`, `cm4/fw_tasks.c`, `cm4/FreeRTOSConfig.h` | FreeRTOS 커널 + 태스크 루프를 M4 로 컴파일(링크 안 함) |
| `demo_su_shrink.c` | ".su 가 버퍼보다 작다" 미스터리 |
| `demo_paint_measure.c` | arm64 ucontext 스택 칠하기 실측 |
| `demo_freertos_hwm.c` (+ `FreeRTOSConfig.h`, `hooks.c` — RTOS-B07 복사) | POSIX 포트 HWM |

<!-- code: fw_graph.c -->

## 4. 힌트

<details>
<summary>힌트 1 — TODO 1: 이름은 "두 번째(또는 세 번째) 콜론 뒤부터 첫 탭까지"</summary>

마지막 콜론으로 자르면 C++ 의 `Spi::xfer` 가 깨진다. 앞에서부터: 파일 이름 뒤 첫 `:` → 숫자들 + `:` (줄) → 숫자들 + `:` 가 한 번 더 있으면 gcc 의 열. 남은 것이 이름이다. 바이트는 숫자만, 그 뒤는 반드시 탭, 한정자는 세 문자열 중 하나와 **길이까지** 같아야 한다(`staticx` 거부). 줄 끝 `\r` 은 봐준다.

</details>

<details>
<summary>힌트 2 — TODO 2: 메모가 있는 DFS + 3색</summary>

WHITE(처음) → GRAY(지금 경로 위) → BLACK(끝남, `best[]` 확정). 공유 하위 트리(`crc16_ccitt` 를 comms 와 rx 가 둘 다 부름)는 BLACK 이라 다시 안 내려간다 — 커널 그래프(함수 112개, 간선 187개)도 즉시 끝난다. 에러는 그 자리에서 `set_err` 후 -1 을 위로 그대로 전파. 경로는 `next[]` 만 기억해 두면 `worst_stack` 이 따라가며 채운다.

</details>

<details>
<summary>힌트 3 — TODO 3·4: 숫자는 port.c 에 다 있다</summary>

하드웨어 프레임: r0–r3, r12, lr, pc, xPSR = 8 워드. FP 문맥이면 s0–s15, FPSCR, 예약 1 이 더해져 26 워드. PendSV: `tst r14, #0x10` → `vstmdbeq r0!, {s16-s31}` (16 워드) → `stmdb r0!, {r4-r11, r14}` (9 워드). 검산: FP 없는 새 태스크의 첫 프레임은 `pxPortInitialiseStack` 이 만든 18 워드 = 패딩 1 + 하드웨어 8 + EXC_RETURN 1 + r4–r11 8.

</details>

<details>
<summary>힌트 4 — TODO 5·7: "동시에 쌓이는가" 를 따져라</summary>

같은 NVIC 우선순위끼리는 선점이 없으니 레벨당 하나. ISR 중첩 프레임과 PendSV 의 소프트웨어 프레임은 동시에 존재하지 않는다 — PendSV 는 최저 우선순위라 모든 ISR 이 끝난 뒤(tail-chaining) 돈다. 그래서 `max(ctx, isr)` 이지 합이 아니다. ISR 이 MSP 에서 돌면 첫 프레임은 이미 `ctx` 안에 있다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 도구·API 대응표

| 이 레슨 | 실제 도구 / API | 비고 |
|---|---|---|
| `.su` 파싱 | gcc/clang `-fstack-usage` | 함수 **자기 프레임** 만. 호출 합은 안 해 준다 |
| `callgraph.sh` (objdump) | gcc `-fcallgraph-info=su` (`.ci`, VCG 형식) | clang 21 에는 없음 → 바이너리에서 뽑는다 |
| `worst_stack()` | GNU `-Wstack-usage=N`(함수 단위 경고), 링커/IDE 의 스택 분석(Keil `--callgraph`, IAR 스택 사용 분석), AbsInt StackAnalyzer | 상용 도구 동작은 문서 기준, 미검증 |
| `task_budget().words` | FreeRTOS `xTaskCreate(…, usStackDepth 워드, …)` / Zephyr `K_THREAD_STACK_DEFINE(name, 바이트)` / CMSIS-RTOS2 `osThreadAttr_t.stack_size`(바이트) | FreeRTOS 만 워드 (RTOS-B07) |
| HWM 비교 | `uxTaskGetStackHighWaterMark()`(워드) / Zephyr `k_thread_stack_space_get()`(바이트, `CONFIG_THREAD_STACK_INFO`) / `osThreadGetStackSpace()` | Zephyr·CMSIS 는 문서 기준 |
| MSP 크기 (연습 3) | 링커 스크립트의 메인 스택 크기 (STM32Cube 생성 `.ld` 의 `_Min_Stack_Size` 등), Zephyr `CONFIG_ISR_STACK_SIZE` | 문서 기준, 미검증 |
| `exc_frame_bytes` | Armv7-M ARM B1.5.6–B1.5.7 (예외 진입 · 정렬), FPCCR.ASPEN/LSPEN | 문서 기준 |

### Part A — 컴파일러가 낸 숫자 읽기

`bash measure_su.sh` 의 실제 출력 (Apple clang 21.0.0, `-O2`; 가운데 열은 같은 소스 `-O0`):

```text
function                 M4 -O2   M4 -O0    arm64
spi_burst_read               20       16        0
imu_read                     32       48       64
filter_update                 8       24        0
pid_step                      0       28        0
fmt_u32                      44       40       48
log_vformat                  56       80      112
uart_write                   20       12        0
log_printf                  120      136      128
control_step                 56       56      112
crc16_ccitt                  16       24        0
cobs_encode                  32       28        0
comms_send_telemetry        120      160      128
rx_parse_once                56       72       80
isr_uart_rx                   0        0        0
adc_average                  88       64       64
isr_adc_dma                   8        8       16
isr_tick                      0        0        0
fmt_u32_rec                  16       24       32
ctrl_task_once                0       16        0
comms_task_once               0       16        0
-- edges only in arm64:
adc_average __stack_chk_fail;comms_send_telemetry __stack_chk_fail;fmt_u32 __stack_chk_fail;imu_read __stack_chk_fail;log_printf __stack_chk_fail;log_vformat __stack_chk_fail;log_vformat memcpy;rx_parse_once __stack_chk_fail;
```

숫자 읽는 법 — M4 어셈블리(`-S`)와 맞춰 본 것:

| 함수 | M4 바이트 | 어디서 왔나 (실제 asm) |
|---|---|---|
| `log_printf` | 120 | `sub sp,#12` (가변 인자 r1–r3 을 스택에 내림) + `push {r4,lr}` 8 + `sub sp,#100` (줄 버퍼 96 + 정렬) |
| `control_step` | 56 | `push {r4,lr}` 8 + **`vpush {d8,d9,d10}` 24** (FP callee-saved — 이 태스크가 FPU 를 쓴다는 증거) + `sub sp,#24` |
| `spi_burst_read` | 20 | 잎(leaf) 함수지만 `push {r4,r5,r6,r7,lr}` — 레지스터가 모자라 callee-saved 를 빌림 |
| `ctrl_task_once` | 0 | `b.w control_step` = **꼬리 호출**(R_ARM_THM_JUMP24). 자기 프레임 없이 점프 |
| arm64 잎 함수들 | 0 | arm64 는 잎에서 lr 저장이 필요 없고 레지스터가 많다. 대신 배열이 있는 함수는 **스택 프로텍터 카나리**(`___stack_chk_guard`) 8 B 가 붙는다 — Apple clang 기본값 |

`-O0` 이 `-O2` 보다 **작은** 함수도 있다(`adc_average` 64 vs 88, `cobs_encode` 28 vs 32): 최적화가 루프를 펼치며 레지스터를 더 쓰고 그만큼 callee-saved 를 push 한다. "디버그 빌드가 늘 크다" 도 아니다 — 측정은 **출시 빌드와 같은 옵션** 으로.

**오케스트레이터가 본 "4 바이트 미스터리".** `int f(int x){volatile char b[64];b[0]=x;return b[0];}` 가 M4 `-O2` 에서 4 B 로 나왔다. `demo_su_shrink.c` 로 원인을 갈랐다 (세 번 컴파일한 `.su` 값을 한 표로 모았다 — gcc 열은 Lima VM 의 arm-none-eabi-gcc 14.2.1 `-O2`):

```text
                 clang -O2   clang -O0   gcc -O2
only_first           4          68          64      ← b[0] 만 접근
any_index           64          68          64      ← 인덱스가 변수
escapes             72          80          72      ← sink(b) 로 주소가 나감
unused_big           0        1028           0      ← 안 쓰는 1 KB 배열
vla                 16 dynamic  32 dynamic   8 dynamic
vla_masked          16 dynamic  32 dynamic   8 dynamic
```

LLVM IR 이 답이다 (`clang --target=thumbv7em-none-eabi … -S -emit-llvm demo_su_shrink.c`, `only_first` 안의 줄만):

```text
== -O0
  %2 = alloca i32, align 4
  %3 = alloca [64 x i8], align 1
  store volatile i8 %5, ptr %6, align 1
  %8 = load volatile i8, ptr %7, align 1
== -O2
  %2 = alloca i8, align 1
  store volatile i8 %3, ptr %2, align 1, !tbaa !3
  %4 = load volatile i8, ptr %2, align 1, !tbaa !3
```

SROA(Scalar Replacement of Aggregates) 패스가 **실제로 접근되는 조각만** 남기고 배열을 쪼갰다 — `volatile` 은 "이 접근을 지우지 마라" 일 뿐 "나머지 63 B 도 메모리에 있어라" 가 아니다. 1 B 슬롯이 4 B 정렬로 잡혀 `.su` 4. 즉 **.su 는 틀리지 않았다 — 생성된 코드가 정말 4 B 만 쓴다.** gcc 는 volatile 배열을 쪼개지 않아 64 B. 교훈: (1) 측정용 예제는 버퍼가 진짜 쓰이게(변수 인덱스, 주소 escape) 만들어야 하고 — `fw_graph.c` 의 모든 버퍼가 그렇다, (2) 소스를 보고 "64 B 배열이니 64 B" 라고 손으로 세는 것은 위로도 아래로도 틀린다. VLA 는 둘 다 `dynamic` 이고, 크기를 마스크로 묶어도 이번 컴파일러들은 `dynamic,bounded` 를 내지 않았다 (gcc 문서에 있는 한정자 — 이 레슨에선 재현 못 함). 도구는 `dynamic` 을 에러로, `bounded` 는 값 그대로 쓴다.

<!-- code: demo_su_shrink.c -->

**호출 그래프 뽑기.** clang 에는 gcc 의 `-fcallgraph-info` 가 없다. 그래서 `callgraph.sh` 는 객체 파일에서 직접 호출을 읽는다: 재배치 `R_ARM_THM_CALL`(bl) / `R_ARM_THM_JUMP24`(꼬리 호출 b.w) / `ARM64_RELOC_BRANCH26`, 그리고 같은 섹션 안에서 이미 해결돼 재배치가 없는 `bl 0xd0c <_fmt_u32_rec>` (arm64 자기 재귀가 그랬다). 처음 버전은 `b <심볼>` 도 전부 간선으로 쳤다가 **`rx_task` 가 재귀** 라는 거짓 에러가 났다:

```text
00000088 <rx_task>:
      88:   movs  r0, #0x0
      ...
      90:   bl    0x90 <rx_task+0x8>      @ ulTaskGenericNotifyTake (재배치)
      94:   bl    0x94 <rx_task+0xc>      @ rx_parse_once
      9a:   bne   0x94 <rx_task+0xc>
      9c:   b     0x88 <rx_task>          ← for(;;) 의 뒤 분기가 함수 첫 주소로
```

`b` 로 자기 자신에 가는 것은 루프다(스택이 안 늘어난다). `bl` 자기 자신만 재귀로 남겼다. 대조로 Lima VM 의 arm-none-eabi-gcc 14.2.1 `-fcallgraph-info=su` 가 낸 `.ci` 간선 17개는 clang 쪽과 같은 구조였다 (단 gcc 는 `memcpy`, clang 은 `__aeabi_memcpy`).

<!-- code: callgraph.sh -->

**DFS.** 그래프가 DAG 이면 `best[u] = self(u) + max_v best[v]` 한 줄이다. 재귀가 있으면 "최악" 이 정의되지 않으므로 숫자를 내지 말고 **에러** 를 내는 게 맞다 (상한이 증명된 재귀는 연습 1). `./solution -v` 실제 출력:

```text
== Part A: worst path per root (Cortex-M4 clang -O2 | arm64 clang -O2)
  imu_read         M4   52 B            arm64   64 B   imu_read > spi_burst_read
  log_printf       M4  220 B            arm64  288 B   log_printf > log_vformat > fmt_u32
  ctrl_task_once   M4  276 B            arm64  400 B   ctrl_task_once > control_step > log_printf > log_vformat > fmt_u32
  comms_task_once  M4  152 B            arm64  128 B   comms_task_once > comms_send_telemetry > cobs_encode
  rx_parse_once    M4   72 B            arm64   80 B   rx_parse_once > crc16_ccitt
  isr_adc_dma      M4   96 B            arm64   80 B   isr_adc_dma > adc_average
  fmt_u32_rec      M4   -1 B RECURSION  arm64   -1 B
```

`assume` 없이 돌리면 `ctrl_task_once → ERROR UNKNOWN: __aeabi_memcpy`. 소스 어디에도 `memcpy` 는 없다 — `log_vformat` 의 "숫자 문자열 복사" 루프를 clang 이 `bl __aeabi_memcpy` 로 바꿨다 (`-O0` 에선 이 간선이 없다). 라이브러리 함수엔 `.su` 가 없으니 사람이 확인해야 한다: Lima 에서 newlib(`thumb/v7e-m+fp/hard/libc.a`)을 디스어셈블하니 `__aeabi_memcpy` 는 `b.w memcpy` 한 줄, `memcpy` 는 push 없이 `bx lr` 로 끝났다 → **0 B 로 확인** 하고 `assume` 에 적었다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="14" fill="currentColor" font-weight="bold" font-size="13">ctrl_task 의 호출 그래프 (M4 clang -O2, 숫자 = 자기 프레임 B) — 굵은 칸 = 최악 경로 284 B</text>
  <g stroke="currentColor" fill="none">
    <path d="M130 162 L160 87"/><path d="M130 162 L160 202"/><path d="M130 162 L160 237"/><path d="M130 162 L160 272"/>
    <path d="M280 87 L310 42"/><path d="M280 87 L310 72"/><path d="M280 87 L310 102"/><path d="M280 87 L310 132"/>
    <path d="M280 202 L310 219"/><path d="M280 237 L310 219"/>
    <path d="M430 42 L460 42"/><path d="M430 132 L460 132"/><path d="M430 132 L460 162"/><path d="M430 219 L460 219"/>
    <path d="M580 132 L610 117"/><path d="M580 132 L610 147"/>
  </g>
  <rect x="10" y="150" width="120" height="24" class="f-acc" stroke="currentColor" stroke-width="2"/><text x="16" y="166" fill="currentColor">ctrl_task 8</text>
  <rect x="160" y="75" width="120" height="24" class="f-acc" stroke="currentColor" stroke-width="2"/><text x="166" y="91" fill="currentColor">control_step 56</text>
  <rect x="160" y="190" width="120" height="24" class="f-ok" stroke="currentColor"/><text x="166" y="206" fill="currentColor">xQueueGenericSend 48</text>
  <rect x="160" y="225" width="120" height="24" class="f-ok" stroke="currentColor"/><text x="166" y="241" fill="currentColor">xTaskDelayUntil 24</text>
  <rect x="160" y="260" width="120" height="24" class="f-ok" stroke="currentColor"/><text x="166" y="276" fill="currentColor">xTaskGetTickCount 0</text>
  <rect x="310" y="30" width="120" height="24" class="f-mut" stroke="currentColor"/><text x="316" y="46" fill="currentColor">imu_read 32</text>
  <rect x="310" y="60" width="120" height="24" class="f-mut" stroke="currentColor"/><text x="316" y="76" fill="currentColor">filter_update 8</text>
  <rect x="310" y="90" width="120" height="24" class="f-mut" stroke="currentColor"/><text x="316" y="106" fill="currentColor">pid_step 0</text>
  <rect x="310" y="120" width="120" height="24" class="f-acc" stroke="currentColor" stroke-width="2"/><text x="316" y="136" fill="currentColor">log_printf 120</text>
  <rect x="310" y="207" width="120" height="24" class="f-ok" stroke="currentColor"/><text x="316" y="223" fill="currentColor">xTaskResumeAll 40</text>
  <rect x="460" y="30" width="120" height="24" class="f-mut" stroke="currentColor"/><text x="466" y="46" fill="currentColor">spi_burst_read 20</text>
  <rect x="460" y="120" width="120" height="24" class="f-acc" stroke="currentColor" stroke-width="2"/><text x="466" y="136" fill="currentColor">log_vformat 56</text>
  <rect x="460" y="150" width="120" height="24" class="f-mut" stroke="currentColor"/><text x="466" y="166" fill="currentColor">uart_write 20</text>
  <rect x="460" y="207" width="120" height="24" class="f-ok" stroke="currentColor"/><text x="466" y="223" fill="currentColor">xTaskIncrementTick 36</text>
  <rect x="610" y="105" width="120" height="24" class="f-acc" stroke="currentColor" stroke-width="2"/><text x="616" y="121" fill="currentColor">fmt_u32 44</text>
  <rect x="610" y="135" width="120" height="24" class="f-warn" stroke="currentColor" stroke-dasharray="4 3"/><text x="616" y="151" fill="currentColor">__aeabi_memcpy ?</text>
  <text x="310" y="262" fill="currentColor">커널 쪽 최악: 8 + 48 + 40 + 36 = 132 B</text>
  <text x="310" y="280" fill="currentColor">(밀린 tick 을 태스크 문맥에서 처리, tasks.c 4157)</text>
  <text x="10" y="306" fill="currentColor">점선 칸 = .su 없음 → 도구가 UNKNOWN 으로 멈춘다. newlib 디스어셈블로 0 B 확인 후 assume 에 기록.</text>
</svg>
```

### Part B — 예외 프레임과 커널 문맥

FreeRTOS V11.3.2 `portable/GCC/ARM_CM4F/port.c` 의 PendSV (504 행부터):

```text
516   tst r14, #0x10            /* Is the task using the FPU context?  If so, push high vfp registers. */
518   vstmdbeq r0!, {s16-s31}
520   stmdb r0!, {r4-r11, r14}  /* Save the core registers. */
523   stmdb sp!, {r0, r3}       ← 여기부터는 sp = MSP (핸들러 모드)
528   bl vTaskSwitchContext     ← 스케줄러 코드도 MSP 위에서
```

| 조각 | 누가 | 어디에 | FP 없음 | FP 문맥 |
|---|---|---|---|---|
| 하드웨어 예외 프레임 | CPU 가 자동 | 선점당한 쪽의 SP (태스크면 PSP) | 8 워드 32 B | 26 워드 104 B (s0–s15, FPSCR, 예약) |
| 정렬 패딩 | CPU (CCR.STKALIGN) | 같은 곳 | +4 B (진입 SP 가 8 정렬이 아닐 때) | +4 B |
| PendSV 소프트웨어 저장 | port.c 518–520 | PSP | r4–r11, r14 = 36 B | + s16–s31 64 B = 100 B |
| `vTaskSwitchContext` 등 | 커널 | **MSP** | 태스크 예산에 안 들어감 | — |

`r14` 를 왜 저장하나? 핸들러 안의 lr 은 **EXC_RETURN** 이고, 그 bit 4 가 "이 태스크의 프레임이 확장형인가" 를 기억한다 — 다음에 돌아올 때 `tst r14, #0x10` 으로 s16–s31 을 꺼낼지 정한다. 새 태스크의 EXC_RETURN 은 `0xfffffffd`(port.c 93, 스레드 모드 · PSP · 기본 프레임)로 시작하므로 FP 를 처음 쓰기 전까지는 작은 프레임이다. 한 번이라도 FP 명령을 실행하면 CONTROL.FPCA = 1 이 되고 그때부터 선점될 때마다 104 + 64 B. port.c 447–448 이 `FPCCR |= ASPEN | LSPEN` ("Lazy save always") 을 켜므로 **지연 스태킹(lazy stacking)**: 프레임 공간 104 B 는 진입 때 바로 잡히고(SP 가 내려감), s0–s15 실제 쓰기는 핸들러가 FP 를 쓸 때까지 미룬다 (Armv7-M 문서 기준). 예산은 **공간** 으로 세야 한다 — 칠하기 실측은 안 쓴 칸을 못 본다(Part C).

검산 테스트: `exc_frame_bytes(0,1) + switch_frame_bytes(0) = 36 + 36 = 72 = 18 워드` — `pxPortInitialiseStack`(port.c 202–231)이 새 태스크 스택에 미리 만들어 두는 프레임(오프셋 1 + xPSR·PC·LR·R12·R3–R0 8 + EXC_RETURN 1 + R4–R11 8)과 정확히 같다.

**ISR 은 어느 스택에?** FreeRTOS Cortex-M 포트의 태스크는 스레드 모드 + PSP 로 돈다. IRQ 가 태스크를 선점하면 하드웨어 프레임은 **선점 직전에 쓰던 스택(PSP)** 에 쌓이고, 핸들러는 핸들러 모드라 **MSP** 를 쓴다. 그 위에서 더 급한 IRQ 가 중첩되면 그 프레임도 MSP 로 간다. 스케줄러 시작 때 `prvPortStartFirstTask` 가 벡터 테이블 첫 워드(초기 MSP)를 다시 읽어 `msr msp, r0` (port.c 285–288) — `main()` 이 쓰던 스택을 통째로 ISR 전용 스택으로 되돌린다. 결론: **태스크 예산에 들어가는 ISR 몫 = 첫 프레임 하나, 그것도 PendSV 프레임과 같은 자리** → MSP 모델에선 `isr = 0`.

별도 인터럽트 스택이 없는 포트(일부 8/16비트 MCU 포트, 또는 태스크를 MSP 로 돌리는 직접 만든 스케줄러 — 포트마다 다르니 확인 필요)라면 중첩 ISR 전체가 지금 돌던 태스크 스택에 쌓이고, **모든** 태스크 예산에 들어가야 한다. `isr_nest_bytes` 의 DP 가 필요한 경우(테스트 `dp[]`): 레벨 10 에 A(path 40, 정수) 와 B(path 0, FP 사용), 레벨 5 에 C. A 를 고르면 32+40+32 = 104, B 를 고르면 32+0+**104** = 136 — B 가 쓴 FP 문맥 때문에 C 의 프레임이 확장형이 된다. 레벨마다 path 최대만 고르는 탐욕법은 틀린다.

### Part C — 커널까지 넣은 예산표

`bash cm4/kernel_su.sh` 는 FreeRTOS `tasks.c`·`queue.c`·`list.c`·`ARM_CM4F/port.c` 와 태스크 루프 `cm4/fw_tasks.c` 를 Cortex-M4F 로 컴파일한다 (실제 출력 일부):

```text
functions:      112  edges:      187
xTaskDelayUntil	24	static
xTaskIncrementTick	36	static
ulTaskGenericNotifyTake	32	static
xQueueGenericSend	48	static
xQueueReceive	48	static
xPortPendSVHandler	0	static
xPortSysTickHandler	8	static
ctrl_task	8	static
comms_task	48	static
rx_task	0	static
-- 직접 부르는 외부 함수(.su 없음) / 간접 호출:
  __aeabi_memcpy  (from: log_vformat xQueueGenericSend xQueueGenericSendFromISR xQueuePeek ... xQueueReceive ...)
  pvPortMalloc  (from: xQueueCreateMutex xQueueGenericCreate xTaskCreate )
```

<!-- code: cm4/fw_tasks.c -->

solution 바이너리는 그대로 **도구** 로도 쓴다 (실제 출력):

```text
$ ./solution --su /tmp/RTOS-M08_k/all.su --edges /tmp/RTOS-M08_k/all.edges --assume __aeabi_memcpy=0 \
    --root ctrl_task --root rx_task
tool: 112 functions (0 dup), 187 edges, 1 assumed
  ctrl_task                284 B  ctrl_task > control_step > log_printf > log_vformat > fmt_u32
  rx_task                  108 B  rx_task > ulTaskGenericNotifyTake > xTaskResumeAll > xTaskIncrementTick
```

두 가지가 눈에 띈다. (1) `xQueueGenericSend`·`xQueueReceive`·`ulTaskGenericNotifyTake`·`xTaskDelayUntil` 의 최악 경로 끝이 모두 `xTaskResumeAll > xTaskIncrementTick` 이다: 스케줄러가 잠긴 동안 밀린 tick 을 `xTaskResumeAll` 이 **호출한 태스크의 문맥에서** 처리한다 (tasks.c 4151–4169, `xTaskIncrementTick()` 호출 4157). tick 처리가 ISR 만의 일이 아니다. (2) `xPortPendSVHandler` 는 0 B — naked 함수라 컴파일러가 asm 의 `stmdb sp!, {r0, r3}` 8 B 를 모른다. `.su` 는 inline asm 을 세지 않는다.

`./solution -v` 의 Part C (여유 +25 %, 최소 64 B, 8 정렬):

```text
== Part C: ISR paths (MSP)  USART2_IRQHandler(prio 5)=40 isr_adc_dma(prio 6)=96 xPortSysTickHandler(prio 15)=44 xPortPendSVHandler(prio 15)=0
  task   fpu  path   ctx | ISR on MSP: raw total w | ISR on task stack: isr raw total w
  ctrl     1   284   208 |   492   616   154        |   360   644   808   202
  comms    1   200   208 |   408   512   128        |   360   560   704   176
  rx       0   108    72 |   180   248    62        |   288   396   496   124
  ctrl   path: ctrl_task > control_step > log_printf > log_vformat > fmt_u32
  comms  path: comms_task > comms_send_telemetry > cobs_encode
  rx     path: rx_task > ulTaskGenericNotifyTake > xTaskResumeAll > xTaskIncrementTick
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">태스크 예산 (B, 1 px = 1.67 B) — 위: ISR 은 MSP / 아래: ISR 이 태스크 스택에</text>
  <text x="10" y="56" fill="currentColor" font-weight="bold">ctrl (FPU)</text><text x="10" y="72" fill="currentColor">MSP</text><text x="10" y="96" fill="currentColor">태스크 스택</text>
  <rect x="130" y="58" width="170" height="20" class="f-acc" stroke="currentColor"/><rect x="300" y="58" width="125" height="20" class="f-ok" stroke="currentColor"/><rect x="425" y="58" width="74" height="20" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="507" y="73" fill="currentColor">492 → 616 B · 154 w</text>
  <rect x="130" y="82" width="170" height="20" class="f-acc" stroke="currentColor"/><rect x="300" y="82" width="216" height="20" class="f-warn" stroke="currentColor"/><rect x="516" y="82" width="98" height="20" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="622" y="97" fill="currentColor">644 → 808 · 202 w</text>
  <text x="10" y="126" fill="currentColor" font-weight="bold">comms (FPU)</text><text x="10" y="142" fill="currentColor">MSP</text><text x="10" y="166" fill="currentColor">태스크 스택</text>
  <rect x="130" y="128" width="120" height="20" class="f-acc" stroke="currentColor"/><rect x="250" y="128" width="125" height="20" class="f-ok" stroke="currentColor"/><rect x="375" y="128" width="62" height="20" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="445" y="143" fill="currentColor">408 → 512 · 128 w</text>
  <rect x="130" y="152" width="120" height="20" class="f-acc" stroke="currentColor"/><rect x="250" y="152" width="216" height="20" class="f-warn" stroke="currentColor"/><rect x="466" y="152" width="86" height="20" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="560" y="167" fill="currentColor">560 → 704 · 176 w</text>
  <text x="10" y="196" fill="currentColor" font-weight="bold">rx (정수)</text><text x="10" y="212" fill="currentColor">MSP</text><text x="10" y="236" fill="currentColor">태스크 스택</text>
  <rect x="130" y="198" width="65" height="20" class="f-acc" stroke="currentColor"/><rect x="195" y="198" width="43" height="20" class="f-ok" stroke="currentColor"/><rect x="238" y="198" width="41" height="20" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="287" y="213" fill="currentColor">180 → 248 · 62 w</text>
  <rect x="130" y="222" width="65" height="20" class="f-acc" stroke="currentColor"/><rect x="195" y="222" width="173" height="20" class="f-warn" stroke="currentColor"/><rect x="368" y="222" width="60" height="20" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="436" y="237" fill="currentColor">396 → 496 · 124 w</text>
  <rect x="130" y="266" width="14" height="12" class="f-acc" stroke="currentColor"/><text x="150" y="277" fill="currentColor">최악 호출 경로</text>
  <rect x="250" y="266" width="14" height="12" class="f-ok" stroke="currentColor"/><text x="270" y="277" fill="currentColor">ctx = 예외 프레임 + PendSV</text>
  <rect x="440" y="266" width="14" height="12" class="f-warn" stroke="currentColor"/><text x="460" y="277" fill="currentColor">중첩 ISR (ctx 보다 크면)</text>
  <rect x="620" y="266" width="14" height="12" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/><text x="640" y="277" fill="currentColor">여유</text>
</svg>
```

ISR 이 태스크 스택에 쌓이는 포트였다면 rx 처럼 작은 태스크가 **두 배 이상**(62 → 124 워드) 커진다 — 모든 태스크가 남의 ISR 비용을 짊어지기 때문이다. Cortex-M 의 MSP/PSP 분리가 RTOS 메모리를 아끼는 이유가 이것이다. 테스트도 이 성질을 잡는다: prio 1 ISR(모터 브레이크 폴트, 24 B)을 하나 더하면 MSP 모델 예산은 그대로, 태스크 스택 모델은 정확히 36 + 24 B 늘어난다.

**여유율 정책: +25 %, 최소 64 B, 8 B 정렬.** 근거는 "같은 소스가 빌드에 따라 얼마나 흔들리나" 를 잰 숫자다 (연습 4, 실제 데이터):

| ctrl_task_once 최악 경로 | B | -O2 clang 대비 |
|---|---|---|
| clang 21 `-O2` (출시) | 276 | 100 % |
| clang 21 `-O0` (디버그) | 328 | 118 % |
| arm-none-eabi-gcc 14.2.1 `-O2` | 232 | 84 % |

- 컴파일러 버전·옵션이 바뀌면 ±20 % 가 그냥 움직인다 → 25 % 는 "다음 툴체인 업데이트나 디버그 빌드에서도 안 넘친다" 의 최소선.
- 최소 64 B 는 퍼센트가 무의미하게 작은 태스크(rx 180 B → +45 B)에 "분석이 놓친 것 하나"(간접 호출 한 단계, naked asm 8 B, 라이브러리 함수) 를 덮는 고정 몫.
- 8 정렬: AAPCS 의 SP 8 B 정렬과 `portBYTE_ALIGNMENT 8` (portmacro.h 83).
- 이 여유는 **분석이 완전할 때** 의 값이다. 함수 포인터·재귀·`dynamic` 이 남아 있으면 퍼센트로 덮지 말고 분석을 먼저 끝낸다 — 도구가 에러로 멈추는 이유.

**실측 대조 ① — FreeRTOS POSIX 포트 HWM.** `demo_freertos_hwm.c` 는 스택을 전혀 다르게 쓰는 태스크 셋(제어 경로, 텔레메트리, 1500 B 지역 배열)을 깊이 256 워드로 만든다. 실제 출력:

```text
POSIX port: depth 256 words x 8 B = 2048 B per task
task   HWM(words)    depth-HWM   &local inside FreeRTOS stack [pxStackBase, +2048)?
ctrl          251            5   NO (local 0x16de4e99b, stack 0x1028f96f0..0x1028f9ef0)
comms         251            5   NO (local 0x16deda99b, stack 0x1028f9f90..0x1028fa790)
hog           251            5   NO (local 0x16df6699b, stack 0x1028fa8d0..0x1028fb0d0)
each task really runs on its own pthread stack: 536576 B (pthread_get_stacksize_np)
```

셋 다 `depth − HWM = 5` — RTOS-B07 이 찾은 그대로다. 포트의 `pxPortInitialiseStack` 은 스택 배열 맨 위에 `Thread_t`(40 B = 5 워드, POSIX port.c 226)만 놓고 태스크 코드는 512 KB pthread 스택에서 돈다. 지역 변수 주소가 FreeRTOS 스택 범위 **밖** 인 것이 증거다. HWM 측정 원리(생성 때 `0xa5` 로 칠하고 tasks.c 1835, 바닥부터 남은 `0xa5` 를 센다 6474–6480)는 맞는데, 칠한 곳에서 코드가 안 돈다. **이 포트의 HWM 으로는 스택 예산을 정할 수 없다.** (주소는 실행마다 다르고 HWM 숫자는 세 번 모두 같았다.)

<!-- code: demo_freertos_hwm.c -->

**실측 대조 ② — arm64 에서 진짜 칠하기.** 그래서 같은 원리를 코드가 실제로 도는 스택에 적용했다: `demo_paint_measure.c` 가 64 KB ucontext 스택을 `0xA5` 로 칠하고(macOS `makecontext` 가 스택을 0 으로 지우므로 그 **뒤에**, RTOS-B03), 함수 하나를 돌린 뒤 바닥부터 칠이 남은 곳을 찾는다. **`.su` 를 낸 바로 그 객체 파일** (`/tmp/RTOS-M08_su/a64/fw_graph.o`)을 링크하므로 바이트 단위 비교가 된다. 실측은 세 번 돌려 출력이 같았다(md5 동일).

| 함수(루트) | .su M4 (자기) | .su arm64 (자기) | 정적 최악 경로 arm64 | 실측 arm64 | 차이 이유 |
|---|---|---|---|---|---|
| spi_burst_read | 20 | 0 | 0 | 0 | |
| imu_read | 32 | 64 | 64 | 54 | `rx[14]` 가 `sp+0xa` — 아래 10 B 는 정렬용, 안 쓰임 |
| filter_update | 8 | 0 | 0 | 0 | |
| pid_step | 0 | 0 | 0 | 0 | |
| fmt_u32 | 44 | 48 | 48 | 36 | `tmp[12]` 가 `sp+0xc` — 아래 12 B 안 쓰임 |
| uart_write | 20 | 0 | 0 | 0 | |
| log_printf | 120 | 128 | 288 | 276 | 경로 끝 fmt_u32 의 12 B |
| ctrl_task_once | 0 | 0 | 400 | 388 | 같은 12 B |
| crc16_ccitt | 16 | 0 | 0 | 0 | |
| cobs_encode | 32 | 0 | 0 | 0 | |
| comms_task_once | 0 | 0 | 128 | 128 | 일치 |
| rx_parse_once | 56 | 80 | 80 | 72 | `frame[32]` 가 `sp+8` — 아래 8 B 안 쓰임 |
| isr_uart_rx | 0 | 0 | 0 | 0 | |
| isr_adc_dma | 8 | 16 | 80 | 72 | adc_average 의 `sorted[16]` 이 `sp+8` |
| fmt_u32_rec (10자리) | 16 | 32 | RECURSION | 320 | = 10 × 32, 연습 1 의 `worst_stack_rec` 와 일치 |

모든 행에서 **실측 ≤ 정적**, 차이는 "SP 는 내려갔지만 아무도 안 쓴 정렬 바이트" 뿐이다 (arm64 디스어셈블로 확인: `imu_read` 는 `sub sp,sp,#0x40` 후 `add x1, sp, #0xa` 가 버퍼 시작). 칠하기는 **쓴** 바이트를 세고, 넘침은 **SP 가 내려간 만큼** 일어난다 — 그 아래로 다음 예외 프레임이 들어오기 때문이다. 같은 이유로 지연 스태킹의 "잡혔지만 안 쓴" s0–s15 공간도 실측에 안 보인다. 그래서 예산은 정적 값으로 잡고, 실측은 "정적 분석이 놓친 경로가 있나" 를 잡는 데(실측 > 정적이면 경보, 연습 5) 쓴다. 처음 측정에선 프레임 0 인 잎 함수가 32 B 로 나왔다 — 함수가 돌아온 뒤 ucontext 종료 경로(`_ctx_done → setcontext`)가 썽크 SP 아래 32 B 를 덮었기 때문이라, 스캔을 "호출 직후, 함수 호출 없이" 하도록 고쳤다.

<!-- code: demo_paint_measure.c -->

```bash
bash measure_su.sh                                      # .su 표 + 간선 (/tmp/RTOS-M08_su)
cc -std=c11 -O2 -Wall -Wextra -Werror demo_paint_measure.c /tmp/RTOS-M08_su/a64/fw_graph.o \
   -o /tmp/RTOS-M08_paint && /tmp/RTOS-M08_paint        # 위 표의 "실측" 열
bash cm4/kernel_su.sh                                   # 커널까지 (/tmp/RTOS-M08_k)
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_hwm.c fw_graph.c hooks.c \
   $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c \
   -o /tmp/RTOS-M08_hwm && /tmp/RTOS-M08_hwm
```

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 65/65 passed (5회 반복 동일)
./solution -v                                                              # Part A/C 표
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o so_san && ./so_san   # 65/65, 경고 0
```

ASan+UBSan 은 테스트와 도구 모드(커널 `.su` 112줄 읽기) 둘 다 깨끗했다. 스레드가 없어 TSan 은 해당 없음. 구현 메모: `worst_stack` 의 `dfs_t` 는 `static` (배열 3개 × 288 노드) — 도구 자체도 재귀 DFS 인데, 깊이가 노드 수로 묶여 호스트에선 문제없다. 펌웨어 안에서 돌릴 도구라면 명시적 스택으로 바꿔야 한다(이 레슨의 교훈을 도구에도).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 것 |
|---|---|---|
| 소스를 보고 "배열 64 B 니까 64" | 최적화기가 지우거나(`only_first` 4 B, `unused_big` 0 B) 늘린다(정렬·카나리·callee-saved) | 출시 빌드의 `.su` 를 쓴다 |
| `-O0` 디버그 빌드로 재고 `-O2` 로 출시 (또는 반대) | 같은 경로가 276 ↔ 328 B, 함수에 따라 방향도 제각각 | 출시 옵션으로 분석, 디버그 빌드도 여유 안에 드는지 확인 (연습 4) |
| 소스의 호출 관계로 그래프를 그림 | 인라인되면 프레임이 호출자로 합쳐지고, 컴파일러가 없던 호출(`__aeabi_memcpy`)을 만든다 | 바이너리에서 간선을 뽑는다 (`callgraph.sh`, gcc `.ci`) |
| `.su` 없는 callee 를 0 으로 침 | 라이브러리·함수 포인터·asm 이 조용히 빠진다 | 도구가 UNKNOWN 으로 멈추게 하고, 확인한 값만 `assume` 에 (newlib memcpy 0 B 처럼 근거와 함께) |
| objdump 의 `b <자기 자신>` 을 재귀로 셈 / 꼬리 호출 무시 | `for(;;)` 루프가 재귀로 보였다(rx_task). 꼬리 호출은 간선으로는 남겨야 한다 | `bl` 자기 자신만 재귀, `b` 다른 함수는 간선(보수적) |
| ISR 중첩을 Cortex-M 태스크마다 다 더함 | ISR 은 MSP — 과대 예산(rx 62 → 124 워드)으로 RAM 낭비 | 태스크엔 첫 프레임만, 중첩은 MSP 예산으로 (연습 3) |
| 첫 예외 프레임을 32 B 로 고정 | FP 문맥 태스크는 104 B + PendSV 64 B 더 — 136 B 과소 | 태스크가 float 를 한 번이라도 쓰면 `fpu = 1` (comms: 곱셈 하나) |
| naked/asm 함수의 .su 를 믿음 | `xPortPendSVHandler` 0 B 인데 실제로 MSP 에 8 B push | asm 은 손으로 세서 `assume` |
| POSIX 포트 HWM 으로 깊이를 줄임 | HWM = 깊이 − 5 고정 (코드가 pthread 스택에서 돈다) | 타깃 보드 HWM 또는 호스트 칠하기 실측 |
| 깊이를 바이트로 넘김 | `xTaskCreate` 의 `usStackDepth` 는 워드 — 616 을 넘기면 2464 B | `total / sizeof(StackType_t)` |

## 8. 면접 Q&A

<details>
<summary>Q1. 태스크 스택 크기를 어떻게 정하셨나요?</summary>

세 단계. (1) 정적: `-fstack-usage` 로 함수별 프레임, 바이너리에서 호출 그래프를 뽑아 태스크 진입점부터 최악 경로(재귀·함수 포인터는 따로 처리). (2) 더하기: 예외 프레임 — FP 쓰는 태스크면 26 워드 + 정렬 4 B — 와 커널이 전환 때 쌓는 r4–r11·EXC_RETURN(+ s16–s31). Cortex-M 은 ISR 이 MSP 라 첫 프레임만. (3) 여유 20–30 % (빌드 변동을 재서 근거를 댄다, 이 레슨 25 %) 후 워드로. 그리고 보드에서 최악 시나리오를 돌린 뒤 `uxTaskGetStackHighWaterMark` 로 교차 확인 — 실측이 정적보다 크면 분석이 뭔가 놓친 것. 예: ctrl 태스크 284 + 208 = 492 → 616 B = 154 워드.

</details>

<details>
<summary>Q2. Cortex-M 에서 인터럽트 핸들러는 어느 스택을 쓰나요? 중첩 ISR 은 태스크 스택에 영향을 주나요?</summary>

RTOS 가 태스크를 스레드 모드 + PSP 로 돌리면, IRQ 진입 때 하드웨어 프레임은 선점당한 쪽 스택(PSP)에 쌓이고, 핸들러 본문은 핸들러 모드라 MSP 를 쓴다. 중첩된 IRQ 의 프레임도 MSP 로. 그래서 태스크 스택엔 첫 프레임 하나만 들어가고, 그 자리는 PendSV 전환 프레임과 겹친다. 중첩 깊이 × (프레임 + 핸들러) 는 MSP 하나에만 잡으면 된다 — 태스크가 10개여도 한 번. FreeRTOS 는 스케줄러 시작 때 MSP 를 벡터 테이블의 초기값으로 되돌려(port.c 285–288) `main()` 스택을 ISR 스택으로 재활용한다. 꼬리: "SysTick 에서 `xTaskIncrementTick` 은 어디서?" → MSP. 단 스케줄러가 잠긴 동안 밀린 tick 은 `xTaskResumeAll` 이 태스크 문맥에서 처리한다(tasks.c 4157) — 태스크 예산에 들어간다.

</details>

<details>
<summary>Q3. FPU 를 쓰는 태스크는 왜 스택이 더 필요하죠? lazy stacking 은요?</summary>

FP 명령을 한 번 실행하면 CONTROL.FPCA = 1. 그 상태에서 예외가 나면 하드웨어가 확장 프레임(s0–s15, FPSCR 포함 26 워드 = 104 B)을 잡고, FreeRTOS PendSV 는 EXC_RETURN bit 4 를 보고 s16–s31 64 B 를 더 저장한다(port.c 516–518). 정수 태스크 대비 72 + 64 = 136 B. Lazy stacking(FPCCR.LSPEN, port.c 448 에서 켬)은 104 B **공간** 은 진입 때 잡되 s0–s15 실제 저장을 핸들러가 FP 를 쓸 때까지 미뤄 지연·전력을 아낀다 — 공간은 그대로라 예산은 줄지 않는다. 함정: `float` 곱셈 하나(comms 태스크), `printf("%f")` 같은 숨은 FP 사용도 FPCA 를 켠다 — 태스크 함수의 `vpush {d8…}` 나 `vmul` 이 objdump 에 보이면 FP 태스크로 친다.

</details>

<details>
<summary>Q4. -fstack-usage 숫자를 그대로 믿어도 되나요?</summary>

함수 하나의 프레임으로는 믿을 만하다(생성된 코드의 SP 이동량). 하지만 (1) 호출 합은 안 해 준다, (2) 인라인되면 함수가 사라지고, 컴파일러가 만든 호출(`__aeabi_memcpy`)은 소스에 없다, (3) 라이브러리·asm·naked 함수는 숫자가 없거나 0(PendSV), (4) VLA/alloca 는 `dynamic`, (5) 최적화 수준·컴파일러마다 다르다(같은 경로 232 ~ 328 B). 그리고 "64 B 배열이 4 B 로 나온다" 같은 결과는 버그가 아니라 SROA 가 안 쓰는 부분을 지운 것이다 — 측정 대상 코드를 현실적으로 만들어야 한다. GCC 는 `-fcallgraph-info=su` 로 그래프까지 주고, `-Wstack-usage=N` 으로 함수 단위 상한 경고도 준다.

</details>

<details>
<summary>Q5. 그냥 HWM 으로 재면 안 되나요? 정적 분석과 무엇이 다르죠?</summary>

HWM 은 "시험 중에 실제로 밟은" 최대다. 최악 경로(256 주기마다 한 번 도는 로그, 에러 경로, 최대 길이 문자열)를 안 밟으면 낙관적이다. 또 칠하기는 **쓴** 바이트만 센다: arm64 실측이 정적보다 10–12 B 작았던 것은 정렬로 잡혔지만 안 쓴 칸 때문이고, FP 지연 스태킹 공간도 안 보인다. 반대로 정적 분석은 간접 호출·라이브러리를 놓칠 수 있다. 그래서 둘 다: 정적 = 예산, 실측 = 검증(실측 > 정적이면 분석 누락 경보). 시뮬레이터 함정: FreeRTOS POSIX 포트는 HWM 이 깊이 − 5 워드로 고정 — 코드가 pthread 스택에서 돈다.

</details>

<details>
<summary>Q6. 재귀나 함수 포인터가 있으면 어떻게 하나요?</summary>

재귀: 최악이 정의되지 않으니 도구는 에러를 내야 한다. 정말 필요하면 깊이 상한을 증명하고(32비트 10진수 = 10 단계) `깊이 × 프레임` 으로 넣는다 — 연습 1, arm64 에서 320 B 로 실측과 일치. 안전 규격(MISRA C:2012 Rule 17.2)은 직접·간접 재귀를 금지한다. 함수 포인터: 디스어셈블에선 `blx r3` 로만 보이므로 후보 집합(명령 테이블, 콜백 등록 지점)을 사람이 적어 간선으로 펼친다 — 연습 2. 후보 하나를 빠뜨리면 바로 과소평가(160 → 32 B).

</details>

<details>
<summary>Q7. MSP(메인 스택)는 얼마나 잡나요?</summary>

ISR 들을 NVIC 선점 우선순위 레벨로 묶고, 레벨마다 가장 깊은 핸들러 경로 + (두 번째 레벨부터) 예외 프레임을 더한다. 첫 프레임은 태스크 PSP 로 가므로 빼고, 아래 레벨 ISR 이 FP 를 쓰면 다음 프레임은 104 B. 이 레슨 설정이면 44 + (36 + 96) + (36 + 40) = 252 B (연습 3). 여기에 naked/asm 몫(PendSV 8 B), 스케줄러 시작 전 `main()` 의 사용량(시작 후엔 리셋됨), 여유. 꼬리: "우선순위 그룹(PRIGROUP)을 바꾸면?" → 서브우선순위는 선점을 안 하므로 선점 레벨 수가 줄어 중첩 깊이도 준다.

</details>

<details>
<summary>Q8. 커널 API 호출은 태스크 스택을 얼마나 쓰나요?</summary>

이 설정(clang -O2, Cortex-M4F)에서 `xQueueGenericSend`·`xQueueReceive` 124 B, `ulTaskGenericNotifyTake` 108 B, `xTaskDelayUntil` 100 B — 모두 끝이 `xTaskResumeAll > xTaskIncrementTick`. 큐 API 는 `__aeabi_memcpy` 로 아이템을 복사하므로 큰 아이템이어도 스택은 안 늘지만(복사 대상은 큐 저장소), `configASSERT`·trace 매크로·`configCHECK_FOR_STACK_OVERFLOW` 설정에 따라 커널 코드가 달라지므로(이 레슨에선 끄고 잼, 숫자 변화는 미측정) 출시 설정 그대로 커널도 같이 분석해야 한다. ctrl 태스크는 로그 경로(276 B)가 커널 경로(132 B)보다 커서 커널이 최악을 정하지 않았지만, rx 처럼 작은 태스크는 커널 경로가 최악이다(108 B).

</details>

## 9. 추가 연습

### 연습 1 — 깊이 상한이 있는 자기 재귀

`fmt_u32_rec` 는 32비트 값이면 최대 10 단계다. `worst_stack_rec(g, root, rec_fn, max_depth, &w)` 를 작성하라: 자기 간선을 빼고 `rec_fn` 의 프레임을 `max_depth` 배로. M4 와 arm64 값은? 실측(320 B)과 맞나?

<details>
<summary>답 보기</summary>

표를 복사해 `rec_fn` 의 bytes 에 `max_depth` 를 곱하고, `from == to == rec_fn` 간선만 걸러 낸 그래프로 `worst_stack`. 가장 깊은 단계에서 자식들을 부른다고 보면 "자기 × 깊이 + 자식 최악" 이 상한이다. M4 16 × 10 = **160 B**, arm64 32 × 10 = **320 B** = `demo_paint_measure` 의 `fmt_u32_rec(10 digits) 320 B` 와 정확히 같다(이 함수는 프레임 전부를 쓴다: `stp` 두 쌍). `ctrl_task_once` 처럼 `rec_fn` 에 안 닿는 루트는 276 그대로. 상호 재귀(a→b→a)는 여전히 에러 — 상한을 증명할 수 없으면 숫자를 내지 않는다.

</details>

### 연습 2 — 함수 포인터 디스패처

명령 디스패처 `cmd_dispatch`(24) 가 함수 포인터 테이블로 `cmd_led`(8) / `cmd_cfg_save`(96, `eeprom_write` 40 호출) / `cmd_status`(16) 를 부른다 (숫자는 연습용 가상 `.su`). objdump 에는 `cmd_dispatch *INDIRECT*` 만 보인다. `expand_indirect()` 로 후보를 펼쳐 최악을 구하라. 후보에서 `cmd_cfg_save` 를 빠뜨리면?

<details>
<summary>답 보기</summary>

`*INDIRECT*` 간선 하나를 후보 수만큼의 간선으로 바꾼다. 펼치기 전: `UNKNOWN: *INDIRECT*` 로 멈춤(올바른 동작). 세 후보: 24 + 96 + 40 = **160 B** (`cmd_dispatch > cmd_cfg_save > eeprom_write`). `cmd_led` 만 적으면 **32 B** — 5배 과소. 후보 집합은 테이블 정의(`const cmd_fn_t table[]`)에서 뽑고, 테이블을 `const` 로 두어 런타임에 엉뚱한 함수가 등록되지 않게 하는 것이 분석 가능성의 전제다.

</details>

### 연습 3 — MSP 크기

Part C 의 ISR (USART2 prio 5 = 40 B, ADC DMA prio 6 = 96 B, SysTick·PendSV prio 15 = 44 / 0 B) 에 대해 `msp_bytes(isrs, n, pad)` 를 작성하라. ADC ISR 이 float 필터를 쓰게 되면?

<details>
<summary>답 보기</summary>

`isr_nest_bytes` 와 같은 DP 인데 **레벨 1 의 프레임은 빼고**(PSP 로 간다) 시작한다. pad = 1: 44 + (36 + 96) + (36 + 40) = **252 B**, pad = 0: 240 B. ADC ISR 이 FP 를 쓰면 그 위에서 USART2 가 선점할 때 프레임이 확장형 → 44 + 132 + (108 + 40) = **324 B**. PendSV 의 naked push 8 B 는 `.su` 에 없으니 손으로 더한다.

</details>

### 연습 4 — 여유율의 근거: 같은 소스, 다른 빌드

`SU_FW_M4_O0`(clang `-O0`) 와 `SU_FW_M4_GCC`(arm-none-eabi-gcc 14.2.1 `-O2`, `file:line:col:name` 형식) 로 `ctrl_task_once` · `comms_task_once` 최악 경로를 구하고, `apply_margin(276)` 이 둘 다 덮는지 확인하라.

<details>
<summary>답 보기</summary>

-O0: 16 + 56 + 136 + 80 + 40 = **328 B** (118 %) — `-O0` 에는 `__aeabi_memcpy` 간선이 없어 assume 도 필요 없다. gcc: 0 + 32 + 112 + 56 + 32 = **232 B** (84 %, `memcpy` 는 assume 0). `apply_margin(276) = 352` ≥ 328, 232 — 25 % 가 디버그 빌드까지 덮는다. comms: gcc 132 B, -O0 16 + 160 + 28 = 204 B. 파서가 gcc 의 열 번호를 처리해야 이 연습이 돈다 (Part A 테스트 2).

</details>

### 연습 5 — HWM 판정기

`hwm_verdict(depth_words, word_bytes, hwm_words, static_raw, min_headroom_pct)` 를 작성하라: 사용량 = (깊이 − HWM) × 워드. 정적보다 크면 STATIC_MISSED, 정적의 1/4 미만이면 SUSPICIOUS, 여유 비율이 기준 미만이면 LOW_HEADROOM.

<details>
<summary>답 보기</summary>

POSIX 데모 값(256 워드 × 8 B, HWM 251, 정적 arm64 400 B) → 사용 40 B → **SUSPICIOUS** (HWM 이 무의미하거나 최악 경로를 안 밟음). 보드에서 읽었다고 가정한 값: 154 워드, HWM 40 → 456 B ≤ 492, 여유 26 % → **OK**; HWM 20 → 536 B > 492 → **STATIC_MISSED** (간접 호출·라이브러리·ISR 모델을 다시 보라); 130 워드에 HWM 20 → 여유 15 % → **LOW_HEADROOM**. 판정 순서가 중요하다: 누락 경보가 여유 경보보다 먼저다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 25/25 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o pr_san && ./pr_san   # 25/25, 경고 0
```

## 10. 핵심 정리

- 태스크 스택 = **최악 호출 경로**(.su + 바이너리 호출 그래프, DFS) + **문맥**(예외 프레임 32/104 B + 정렬 4 + PendSV 36/100 B) + 여유 → `usStackDepth` 는 **워드**.
- Cortex-M + FreeRTOS: ISR 은 **MSP**. 태스크 스택엔 첫 프레임만(PendSV 프레임과 같은 자리), 중첩 ISR 은 MSP 예산 하나로. ISR 이 태스크 스택을 쓰는 포트라면 모든 태스크에 중첩 몫(이 예: rx 62 → 124 워드).
- FP 를 한 번이라도 쓴 태스크는 +136 B (확장 프레임 72 + s16–s31 64). Lazy stacking 은 시간만 아끼고 공간은 그대로.
- `.su` 는 생성 코드 기준: 안 쓰는 버퍼는 사라지고(SROA, 64 → 4 B), 소스에 없는 `__aeabi_memcpy` 가 생기고, naked asm 은 0. 재귀·간접 호출·`dynamic` 은 숫자 말고 **에러** 로.
- 커널 API 도 태스크 스택을 쓴다: 큐/알림/DelayUntil 의 최악 끝은 `xTaskResumeAll > xTaskIncrementTick` (밀린 tick 을 태스크 문맥에서).
- 여유 +25 %(최소 64 B)의 근거: 같은 경로가 gcc 232 / clang 276 / -O0 328 B 로 흔들린다.
- 실측은 검증용: 칠하기는 쓴 바이트만 센다(정적보다 0–12 B 작음), POSIX 포트 HWM 은 깊이 − 5 로 무의미. 실측 > 정적이면 분석 누락.

## 참고자료

- FreeRTOS-Kernel V11.3.2 — `portable/GCC/ARM_CM4F/port.c` (pxPortInitialiseStack 202–231, prvPortStartFirstTask 278–290, FPCCR lazy save 447–448, xPortPendSVHandler 504–557), `portmacro.h` (portBYTE_ALIGNMENT 83), `tasks.c` (tskSTACK_FILL_BYTE 121, 스택 칠하기 1832–1837, xTaskResumeAll 의 밀린 tick 4151–4169, prvTaskCheckFreeStackSpace 6470–6484, uxTaskGetStackHighWaterMark 6535), `portable/ThirdParty/GCC/Posix/port.c` (Thread_t 82–89, 226) — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- FreeRTOS 문서 — Stack usage and stack overflow checking <https://www.freertos.org/Stacks-and-stack-overflow-checking.html>, `uxTaskGetStackHighWaterMark` API
- Arm, *Armv7-M Architecture Reference Manual* (DDI 0403) — B1.5.6 예외 진입, B1.5.7 예외 진입 시 스택 정렬, FP 확장 프레임과 FPCCR (문서 기준)
- Arm Application Note AN298, *Cortex-M4(F) Lazy Stacking and Context Switching* (문서 기준)
- GCC 매뉴얼 — `-fstack-usage`, `-fcallgraph-info`, `-Wstack-usage` (Developer Options / Warning Options) <https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html>
- Clang 명령줄 레퍼런스 — `-fstack-usage`; LLVM SROA 패스 <https://llvm.org/docs/Passes.html>
- MISRA C:2012 Rule 17.2 (재귀 금지)
- 관련 레슨: RTOS-B03 (ucontext, 칠하기 HWM), RTOS-B07 (POSIX 포트 HWM = 깊이 − 5), RTOS-M01 (cm4 크기 측정 빌드), RTOS-M02 (ISR 지연 처리, FromISR), RTOS-M09 (스택 오버플로 탐지), RTOS-B04 (CLZ), C-M14 (ISR 공유 데이터, PRIMASK)
