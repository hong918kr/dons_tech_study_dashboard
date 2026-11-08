---
id: C-M15
time: 80분
updated: 2026-11-07
---
# C-M15 드라이버 스타일 에러 처리: 반환 코드와 goto cleanup

## 학습 목표

- 에러 코드 규약 하나(**음수 errno 스타일** + 이름 붙인 `enum`)를 골라 그 이유를 설명하고, 모든 단계가 같은 규약으로 에러를 **덮어쓰지 않고 전파(propagate)** 하게 만든다.
- 4단계 초기화(클럭 → GPIO → DMA → IRQ)를 **goto 언와인딩(unwind) 사다리**로 작성한다: 실패한 단계 k 에서 1..k-1 을 정확히 **역순**으로, **한 번씩** 해제한다.
- 실패 주입(fault injection)과 자원 장부(ledger)로 "누수 0, 이중 해제 0, 올바른 코드, 올바른 순서" 를 단계별로 증명하는 테스트를 짠다.
- 어려운 경계: 단계 안의 **부분 성공**(클럭은 켰는데 ready 가 안 옴), **double init**, **멱등(idempotent) deinit**, **정리 함수 자체의 실패**, 경계 있는(bounded) 폴링.
- goto 사다리를 중첩 if · 단일 출구 · switch fall-through · Linux `devm_*` · `__attribute__((cleanup))` 과 비교하고, MISRA C:2012 Rule 15.1/15.2/15.3 이 goto 를 어떻게 보는지 말할 수 있다.

## 1. 문제

드라이버 `init()` 은 자원을 여러 개 순서대로 잡는다. 셋째 단계에서 실패했는데 첫째·둘째를 안 놓으면 **클럭이 켜진 채 남고(전력), 핀이 묶이고, DMA 채널이 영영 새서** 다음 재시도가 `-EBUSY` 로 죽는다. 면접에서 "이 probe 함수의 에러 경로를 써 보세요" 는 Linux·RTOS·베어메탈 어디서나 나오는 시니어 펌웨어 단골 문제이고, 리뷰어는 **해제 순서와 에러 코드 전파** 를 본다.

이번 문제의 드라이버는 UART + RX DMA 다. 단계마다 자원을 하나씩 잡는다:

| 단계 | acquire (잡기) | release (놓기) | 실패하면 흔한 코드 |
|---|---|---|---|
| ① 클럭 | `clk_enable()` + `wait_ready(&CLK_SR, CLK_RDY, 100)` | `clk_disable()` | `-ETIMEDOUT` (PLL/HSE 가 lock 안 됨) |
| ② GPIO | `gpio_request(UART_PINS)` | `gpio_free()` | `-EBUSY` (다른 드라이버가 핀 사용) |
| ③ DMA | `dma_chan_alloc()` → 채널 번호 | `dma_chan_free(ch)` (**실패할 수 있음**) | `-ENOMEM` (빈 채널 없음) |
| ④ IRQ | `irq_request(UART_IRQn, isr, d)` | `irq_free()` | `-ENODEV`/`-EBUSY` |

호스트(맥)에는 진짜 클럭도 NVIC 도 없으므로, 시뮬레이터가 세 가지를 준다. ① **자원 장부** `led.clk/gpio/dma/irq` — 잡으면 +1, 놓으면 −1, 0 에서 또 놓으면 `led.bad_release++` (이중 해제 탐지). ② **trace 문자열** — `"+CLK +GPIO !DMA -GPIO -CLK"` (`+` 잡기, `!` 실패, `-` 놓기). ③ **실패 주입** `fault_inject(k, code)` — k 번째 단계의 acquire 가 `code` 를 돌려준다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m15a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">goto 사다리: 내려가며 잡고, 실패한 줄에서 오른쪽으로 점프한 뒤 위로 거슬러 놓는다</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="240" height="36" rx="6" class="f-acc"/>
    <rect x="10" y="95" width="240" height="36" rx="6" class="f-acc"/>
    <rect x="10" y="150" width="240" height="36" rx="6" class="f-acc"/>
    <rect x="10" y="205" width="240" height="36" rx="6" class="f-acc"/>
    <rect x="10" y="260" width="240" height="36" rx="6" class="f-ok"/>
    <rect x="480" y="40" width="250" height="36" rx="6" class="f-mut"/>
    <rect x="480" y="95" width="250" height="36" rx="6" class="f-warn"/>
    <rect x="480" y="150" width="250" height="36" rx="6" class="f-warn"/>
    <rect x="480" y="205" width="250" height="36" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="22" y="63">① clk_enable; wait_ready</text>
    <text x="22" y="118">② gpio_request</text>
    <text x="22" y="173">③ ch = dma_chan_alloc</text>
    <text x="22" y="228">④ irq_request</text>
    <text x="22" y="283">ready = true; return 0</text>
    <text x="492" y="63">err_out:  return ret;</text>
    <text x="492" y="118">err_clk:  clk_disable();</text>
    <text x="492" y="173">err_gpio: gpio_free();</text>
    <text x="492" y="228">err_dma:  dma_release(d);</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m15a)">
    <path d="M250 58 H478"/>
    <path d="M250 113 H478"/>
    <path d="M250 168 H478"/>
    <path d="M250 223 H478"/>
    <path d="M250 66 L478 106" stroke-dasharray="4 3"/>
    <path d="M605 205 V188"/>
    <path d="M605 150 V133"/>
    <path d="M605 95 V78"/>
    <path d="M130 76 V93"/><path d="M130 131 V148"/><path d="M130 186 V203"/><path d="M130 241 V258"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="270" y="52">clk_enable 실패: 놓을 것 없음</text>
    <text x="380" y="80">ready 타임아웃</text>
    <text x="290" y="107">gpio 실패</text>
    <text x="290" y="162">dma 실패</text>
    <text x="290" y="217">irq 실패</text>
    <text x="615" y="198">fall-through ↑</text>
    <text x="480" y="270">소스 순서: err_dma → err_gpio →</text>
    <text x="480" y="288">err_clk → err_out (위 그림은 아래→위)</text>
    <text x="10" y="318">규칙: 단계 k 가 실패하면 "k-1 단계를 놓는 라벨" 로 간다. 라벨 이름 = 그 라벨이 놓는 것.</text>
  </g>
</svg>
```

### Part A — goto 언와인딩 init 과 멱등 deinit

- 에러 코드 규약: `drv_err_t` — `DRV_OK = 0`, 에러는 **음수**, 숫자는 Linux errno 값 (`DRV_ETIMEDOUT = -110` …). 핸들을 돌려주는 HAL 함수(`dma_chan_alloc`)는 `int` 로 "≥0 = 핸들, <0 = 에러".
- `drv_err_t uart_init(uart_dev_t *d)` — 위 4단계. 단계 k 실패 시 1..k-1 을 역순으로 놓고 **그 단계의 코드를 그대로** 반환 (`-1` 같은 일반 코드로 바꾸지 않는다). 성공 시 `d->ready = true`.
- `void uart_deinit(uart_dev_t *d)` — 완전히 init 된 장치를 역순(IRQ → DMA → GPIO → CLK)으로 정리. **두 번 불러도, init 실패 후 불러도, init 한 적 없는 장치에 불러도** 아무 일 없어야 한다.
- 테스트: k = 1..4 실패 주입 표(table-driven) → 반환 코드, trace, 누수 0, 실패 후 deinit 안전, **원인이 사라지면 재시도 성공**.

### Part B — 부분 실패의 경계들 (면접관이 파고드는 곳)

- `drv_err_t wait_ready(reg, mask, max_polls)` — mask 비트가 **전부** 설 때까지 최대 `max_polls` 번 읽는다. 타임아웃이면 정확히 `max_polls` 번 읽고 `DRV_ETIMEDOUT`, `max_polls == 0` 이면 0번 읽고 타임아웃, `mask == 0` 은 `DRV_EINVAL`. 무한 루프는 금지.
- 클럭 단계는 **두 동작**(enable → ready 대기)이다. enable 은 됐는데 ready 가 안 오면, 그 단계가 이미 켠 클럭을 그 단계가 꺼야 한다 → trace `"+CLK -CLK"`.
- double init: 이미 ready 인 장치에 `uart_init` → 아무것도 건드리지 않고 `DRV_EBUSY`.
- 정리 함수 자체의 실패: `dma_chan_free(ch)` 가 `-EBUSY`(전송 진행 중) 또는 `-EIO`(DMA 먹통)를 돌려줄 수 있다. `void dma_release(d)` 를 작성하라 — EBUSY 면 `dma_chan_abort()` 후 한 번 더, 그래도 실패면 `d->cleanup_err` 에 **첫 에러만** 기록하고 **정리는 계속**한다. init 은 원래 에러(IRQ 실패 코드)를 그대로 반환해야 한다.

### Part C — devm 스타일: 해제 함수를 쌓아 두는 드라이버 코어

Linux 의 `devm_*` 는 "잡을 때 해제 함수도 등록해 두면, probe 가 실패하거나 remove 될 때 드라이버 코어가 역순으로 불러 준다" 는 방식이다. 이것을 흉내 낸다.

- `drv_err_t devres_add_or_reset(d, fn)` — 해제 함수 `fn` 을 LIFO 스택에 쌓는다. 슬롯이 없으면 **즉시 `fn(d)` 를 불러** 방금 잡은 자원을 놓고 `DRV_ENOMEM` (Linux `devm_add_action_or_reset()` 의미).
- `void devres_release_all(d)` — 최근 것부터 모두 부르고 비운다.
- `drv_err_t uart_probe_devm(d)` — 같은 4단계를 **goto 도, 정리 코드도 없이** `if (ret < 0) return ret;` 만으로. 주어진 `bus_probe()` 가 실패 시 `devres_release_all()` 을 부른다.
- 같은 실패 행렬에서 goto 버전과 **trace 가 한 글자도 다르지 않아야** 한다. 슬롯 2개짜리 스택에서 3번째 등록 실패도 누수 0.

## 2. Problem (English)

> **Driver-style error handling with goto cleanup.**
>
> A UART+DMA driver initializes in four stages, each acquiring one resource: clock (enable, then poll a ready bit), GPIO pins, a DMA channel, and an IRQ line. Error codes are negative errno-style values in `typedef enum { DRV_OK = 0, DRV_EIO = -5, DRV_ENOMEM = -12, DRV_EBUSY = -16, DRV_ENODEV = -19, DRV_EINVAL = -22, DRV_ETIMEDOUT = -110 } drv_err_t;`.
>
> 1. `drv_err_t uart_init(uart_dev_t *d)` — acquire stages 1..4. If stage *k* fails, release stages *k−1..1* in reverse order, exactly once each, and return stage *k*'s error code unchanged. `void uart_deinit(uart_dev_t *d)` must be idempotent.
> 2. `drv_err_t wait_ready(const volatile uint32_t *reg, uint32_t mask, uint32_t max_polls)` — bounded poll until all `mask` bits are set. `void dma_release(uart_dev_t *d)` — `dma_chan_free()` may fail: on `-EBUSY` abort the channel and retry once; on any remaining failure record the first cleanup error in `d->cleanup_err` and keep unwinding. A second `uart_init` on a ready device returns `DRV_EBUSY` and acquires nothing.
> 3. Implement a devres stack (`devres_add_or_reset`, `devres_release_all`) and `uart_probe_devm()` that never releases anything itself; the bus core releases on failure.
>
> Tests inject a failure at each stage and assert: correct return code, zero leaked resources, zero double releases, the exact release order, and that a retry succeeds once the fault is removed.
>
> Example: fault at stage 3 with `DRV_ENOMEM` → returns `-12`, trace `"+CLK +GPIO !DMA -GPIO -CLK"`.
>
> Follow-ups: compare with nested-if and single-exit styles; how does Linux `probe()`/`devm_*` handle this? What does MISRA C:2012 say about `goto`?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄이 쭉 나오고 "20/62 passed" — TODO 를 채워 62/62 를 만든다
```

시작 코드에서 `wait_ready` 는 **한 번만** 읽고(PLL 이 늦게 lock 되면 실패), `dma_release` 는 **반환값을 버린다**(EBUSY 면 채널이 그대로 샌다). 둘 다 실제 코드에서 흔히 보는 "거의 맞는" 모양이다. `uart_init` / `uart_deinit` / devres 3개는 빈 스텁이다.

관찰용 파일 세 개 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -O2 -c demo_goto_pitfalls.c -o /dev/null          # goto 함정 3개: 경고 관찰 (-Werror 없음)
clang --analyze -std=c11 demo_goto_pitfalls.c -o /dev/null                  # 정적 분석기는 무엇을 더 잡나
cc -std=c11 -Wall -Wextra -O2 demo_cleanup_attr.c -o demo_cleanup_attr && ./demo_cleanup_attr   # cleanup 속성
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_codegen.c -o -          # 스타일별 asm
```

## 4. 힌트

<details>
<summary>힌트 1 — 성공 경로를 먼저, 라벨은 그 다음 거꾸로</summary>

성공 경로를 위에서 아래로 쭉 쓴다: `ret = 단계(); if (ret < 0) goto ???;`. 그 다음 `return DRV_OK;` 아래에 라벨을 **잡은 순서의 역순** 으로 놓는다. 각 라벨 밑에는 해제 한 줄만, 그리고 `break`/`return` 없이 아래 라벨로 흘러내리게(fall-through) 둔다. 마지막 라벨이 `return ret;`.

</details>

<details>
<summary>힌트 2 — 단계 k 가 실패하면 어디로?</summary>

"k 번째는 **안 잡혔으니** k-1 번째부터 놓는다". 그래서 IRQ(④) 실패는 `err_dma`, DMA(③) 실패는 `err_gpio`. 라벨 이름을 `err1`, `err2` 가 아니라 **그 라벨이 놓는 것** 으로 지으면 이 대응을 눈으로 검산할 수 있다 (Linux coding-style 의 권고). 클럭 단계는 둘로 쪼개 생각한다: enable 실패 → 놓을 것 없음, ready 타임아웃 → 클럭은 켜졌으니 `err_clk`.

</details>

<details>
<summary>힌트 3 — Part B: "정리는 멈추지 않는다, 원래 에러는 덮지 않는다"</summary>

`wait_ready`: `for (i = 0; i < max_polls; i++) if ((read & mask) == mask) return OK;` — `& mask` 만 보면 비트 하나만 서도 통과한다. `dma_release`: free → EBUSY 면 abort → free 재시도 → 그래도 음수면 `cleanup_err` 가 아직 OK 일 때만 기록. 여기서 `ret` 를 건드리면 안 된다 (`ret` 는 init 이 돌려줄 "진짜 원인"). double init 검사는 **아무것도 잡기 전** 맨 위에서.

</details>

<details>
<summary>힌트 4 — Part C: 스택 하나면 사다리가 필요 없다</summary>

`d->res[d->nres++] = fn;` 으로 쌓고, `while (d->nres > 0) d->res[--d->nres](d);` 로 비운다. 비운 뒤 `nres == 0` 이므로 두 번 불러도 안전하다. 등록이 실패하면 방금 잡은 자원은 스택에 없으니 **그 자리에서** `fn(d)` 로 놓아야 한다 — 이것이 `_or_reset` 의 의미다. probe 본문은 `acquire; add_or_reset; if (ret < 0) return ret;` 의 반복.

</details>

## 5. 풀이

### 에러 코드 규약 고르기 — 왜 "음수 errno 값 + enum 이름" 인가

선택지는 크게 넷이다.

| 규약 | 예 | 장점 | 단점 |
|---|---|---|---|
| bool / 0·-1 | `return -1;` | 단순 | **원인이 사라진다**: 타임아웃인지 핀 충돌인지 호출자가 모름 |
| 양수 상태 enum | STM32 HAL `HAL_OK=0, HAL_ERROR=1, HAL_BUSY=2, HAL_TIMEOUT=3` | 이름 있음 | 종류가 4개뿐, 값과 핸들을 같은 반환값에 못 섞음 |
| 벤더 고유 base | nrfx(2.x) `NRFX_SUCCESS = 0x0BAD0000` | 충돌 없음 | **성공이 0 이 아님** → `if (err)` 관용구가 깨짐 |
| **음수 errno** | Linux `-ETIMEDOUT`, Zephyr `init()` 반환, CMSIS-Driver `ARM_DRIVER_ERROR_TIMEOUT = -3` | `if (ret < 0)` 한 줄, 0 이상은 **값(핸들·바이트 수)** 으로 재사용, 리눅스·Zephyr 와 그대로 호환 | 숫자 자체엔 이름이 없음 |

그래서 이 레슨은 **음수 errno 값에 enum 으로 이름을 붙인다**. 세 가지 이유:

1. **호환**: `dma_chan_alloc()` 처럼 "핸들 또는 에러" 를 `int` 하나로 돌려줄 수 있고, 리눅스 커널·Zephyr 코드로 옮길 때 값이 같다.
2. **`<errno.h>` 를 쓰지 않는다**: errno 번호는 OS 마다 다르다. 이 맥의 SDK 헤더를 보면 `#define ETIMEDOUT 60`, 리눅스는 110. 게다가 freestanding 펌웨어엔 `<errno.h>` 가 없을 수도 있다(newlib 는 또 자기 번호). 그래서 **값을 직접 고정** 한 enum 을 둔다 — 로그·텔레메트리에 찍힌 `-110` 이 어느 빌드에서나 같은 뜻이 된다.
3. **이름과 도구**: `drv_err_t` 라는 타입 이름이 "이 함수는 상태만 돌려준다" 를 문서화하고, `switch` 에 빠진 값이 있으면 `-Wswitch` 가 알려 준다.

C 표준 쪽 주의 두 가지. enum **상수** 의 타입은 `int` 다 (C11 6.7.2.2p3) — 그래서 `int ret` 와 섞어 비교해도 승격 문제가 없다. 반면 enum **타입** 의 크기는 구현 정의다 (6.7.2.2p4: char·signed·unsigned 정수형 중 하나와 호환, 모든 값을 표현할 수 있어야 함). 실제로 `sizeof(drv_err_t)` 를 찍어 보면:

```text
clang --target=arm64-apple-macos         sizeof(drv_err_t) = 4
clang --target=thumbv7em-none-eabi       sizeof(drv_err_t) = 4
clang --target=thumbv7em-none-eabi -fshort-enums   sizeof(drv_err_t) = 1
```

(arm-none-eabi-gcc 는 기본값이 short enums 라고 알려져 있다 — 이 맥엔 gcc 가 없어 직접 확인하진 못했다.) 그래서 **구조체에 저장하거나 라이브러리 경계를 넘는 ABI 에는 enum 대신 `int32_t`** 를 쓰는 것이 안전하고, 리눅스가 함수 반환형을 그냥 `int` 로 두는 이유이기도 하다. 이 레슨은 "상태만 돌려주는 함수 = `drv_err_t`, 핸들도 돌려주는 함수 = `int`" 로 나눴다.

### Part A — 사다리 짜기

정답의 `uart_init` (주석 생략):

```c
    if (d->ready) return DRV_EBUSY;
    d->cleanup_err = DRV_OK;

    ret = clk_enable();                                  if (ret < 0) goto err_out;
    ret = wait_ready(&CLK_SR, CLK_RDY, CLK_READY_POLLS); if (ret < 0) goto err_clk;
    ret = gpio_request(UART_PINS);                       if (ret < 0) goto err_clk;
    ret = dma_chan_alloc();                              if (ret < 0) goto err_gpio;
    d->dma_ch = ret;
    ret = irq_request(UART_IRQn, uart_isr, d);           if (ret < 0) goto err_dma;
    d->ready = true;
    return DRV_OK;
err_dma:  dma_release(d);
err_gpio: gpio_free(UART_PINS);
err_clk:  clk_disable();
err_out:  return (drv_err_t)ret;
```

읽는 법: **위에서 아래로 = 잡는 순서, 라벨을 아래로 흘러내림 = 놓는 순서(역순)**. 단계 k 의 goto 는 "k-1 까지 잡았다" 는 사실을 라벨 하나로 표현한다. 실패 주입 결과(실제 테스트 기대값):

| 주입 | 반환 | trace | 장부 |
|---|---|---|---|
| 없음 | `0` | `+CLK +GPIO +DMA +IRQ` | 전부 1 |
| ① `DRV_EIO` | `-5` | `!CLK` | 0 |
| ② `DRV_EBUSY` | `-16` | `+CLK !GPIO -CLK` | 0 |
| ③ `DRV_ENOMEM` | `-12` | `+CLK +GPIO !DMA -GPIO -CLK` | 0 |
| ④ `DRV_ENODEV` | `-19` | `+CLK +GPIO +DMA !IRQ -DMA -GPIO -CLK` | 0 |

각 행 뒤에 테스트는 `uart_deinit()` 을 한 번 더 부르고(no-op 이어야 함), 주입을 끄고 `uart_init()` 이 **성공** 하는지까지 본다. 실패 후 재시도가 되는지는 "정말 다 놓았나" 의 가장 실용적인 검사다 — 하나라도 남으면 실제 칩에서는 두 번째 `gpio_request` 가 `-EBUSY` 를 낸다.

설계 포인트 다섯:

| 포인트 | 이유 |
|---|---|
| 에러 코드를 **덮어쓰지 않는다** (`ret` 를 그대로 반환) | `-ETIMEDOUT` 을 `-1` 로 바꾸면 필드에서 "클럭 문제" 와 "핀 충돌" 을 구분 못 한다 |
| 라벨 이름 = **그 라벨이 놓는 것** (`err_dma:` 는 DMA 를 놓는다) | `err1/err2` 는 단계를 끼워 넣을 때 번호가 밀려 틀린다. 이름이면 "③ 실패 → err_gpio" 를 눈으로 검산 |
| 라벨 사이에 `return` / `break` 없음 | fall-through 가 곧 "나머지 전부 역순 해제" |
| 핸들(`d->dma_ch`)은 **잡자마자** 저장 | 해제 코드가 쓸 값. 늦게 저장하면 사이에서 실패 시 쓰레기 채널을 놓는다 (`-DMA:badch` 로 잡힘) |
| `ready = true` 는 **맨 마지막** | 도중에 실패한 장치가 "ready" 로 보이면 deinit 이 안 잡은 걸 놓는다 |

`uart_deinit` 은 `if (!d->ready) return;` 로 멱등성을 얻는다. `ready = false` 를 **정리 전에** 내리는 이유: 정리 도중 다른 경로(예: 에러 핸들러, 워치독 콜백)에서 deinit 이 다시 불려도 두 번 놓지 않는다. 해제 순서는 init 의 정확한 역순인데, 특히 **IRQ 를 먼저** 놓는 것이 중요하다 — DMA 를 먼저 놓으면 그 사이에 들어온 UART 인터럽트의 ISR 이 이미 반납된 채널을 만진다.

### Part B — 경계 1: 경계 있는 폴링

```c
static drv_err_t wait_ready(const volatile uint32_t *reg, uint32_t mask, uint32_t max_polls) {
    if (mask == 0) return DRV_EINVAL;
    for (uint32_t i = 0; i < max_polls; i++)
        if ((reg_read(reg) & mask) == mask) return DRV_OK;
    return DRV_ETIMEDOUT;
}
```

| 테스트 | 기대 | 잡아내는 버그 |
|---|---|---|
| 처음부터 ready | `OK`, 읽기 1번 | — |
| `reg = 0x1`, `mask = 0x3` | `ETIMEDOUT`, 읽기 정확히 10번 | `if (reg & mask)` (비트 하나만 서도 통과) — 시작 코드의 버그 |
| `max_polls = 0` | `ETIMEDOUT`, 읽기 0번 | `do { } while` 로 최소 1번 읽는 구현 |
| `mask = 0` | `EINVAL` | `(x & 0) == 0` 은 항상 참 → "언제나 ready" |
| PLL 이 4번째 읽기에 lock | `uart_init` 성공, 읽기 4번 | 한 번만 읽는 구현 (시작 코드) |

`reg` 는 `volatile` 포인터다 — 없으면 컴파일러가 읽기를 루프 밖으로 끌어올린다 (C-B09 Part A). **횟수** 기반 루프는 테스트가 결정적(deterministic)이라 좋지만, 실제 칩에서 "100번" 이 몇 µs 인지는 클럭·플래시 대기 상태에 따라 달라진다. 실무에서는 **시간** 기반(SysTick ms 카운터, `DWT->CYCCNT`)으로 쓰고, 그때는 두 가지를 더 지켜야 한다 → 추가 연습 5:

- tick 랩어라운드: `now() > deadline` 이 아니라 `(uint32_t)(now() - start) >= timeout` (부호 없는 빼기는 모듈러라 랩어라운드에 안전, C-B02 참고).
- **포기하기 전에 한 번 더 읽기**: 조건 확인과 시간 확인 사이에 오래 선점(preemption)되면, 그동안 하드웨어는 ready 가 됐는데 시간만 보고 타임아웃을 선언하는 오판이 생긴다. Linux `read_poll_timeout()` 도 타임아웃 직후 `op()` 를 한 번 더 부른 뒤 조건으로 판정한다.

### Part B — 경계 2: 단계 안의 부분 성공

클럭 단계는 `clk_enable()` (자원 획득) + `wait_ready()` (조건 확인) 두 동작이다. enable 은 성공했으니 **이 단계가 이미 자원을 하나 잡은 상태** 에서 실패한 것이다. 그래서 enable 실패는 `err_out`, ready 타임아웃은 `err_clk` 로 가야 한다 — 같은 "① 실패" 인데 점프 대상이 다르다. 기대 trace `"+CLK -CLK"`, 읽기 정확히 100번. 일반 규칙: **goto 대상은 "단계 번호" 가 아니라 "지금까지 잡은 것" 으로 정한다.** 한 단계가 자원을 둘 잡는다면(예: DMA 채널 + 디스크립터 메모리) 라벨도 둘이어야 한다.

### Part B — 경계 3: double init, 멱등 deinit

| 호출 순서 | 올바른 결과 | 흔한 버그 |
|---|---|---|
| init → init | 두 번째는 `-EBUSY`, 장부 그대로 1 | 검사 없이 다시 잡아 refcount 2 → deinit 한 번으로는 클럭이 안 꺼짐 |
| init → deinit → deinit | 두 번째는 no-op | 플래그 없이 매번 해제 → `bad_release` |
| (init 안 함) → deinit | no-op | 0 으로 초기화된 `dma_ch` 로 채널 0 을 반납 — **남의 채널** |
| init 실패 → deinit | no-op | init 이 일부만 정리하고 `ready` 를 세워 둠 |

double-init 검사가 **아무것도 잡기 전에** 와야 하는 것도 포인트다. 검사를 클럭 enable 뒤에 두면 "실패한 두 번째 init" 이 정상 장치의 클럭 refcount 를 올리거나, 자기 에러 경로에서 **첫 번째 init 이 켜 둔 클럭을 꺼 버린다**.

### Part B — 경계 4: 정리 함수가 실패하면

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m15b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">dma_release(d): 정리는 멈추지 않는다, 원래 에러는 덮지 않는다</text>
  <g stroke="currentColor">
    <rect x="10" y="45" width="150" height="34" rx="6" class="f-acc"/>
    <rect x="215" y="45" width="130" height="34" rx="6" class="f-mut"/>
    <rect x="400" y="45" width="150" height="34" rx="6" class="f-acc"/>
    <rect x="215" y="125" width="515" height="34" rx="6" class="f-warn"/>
    <rect x="215" y="195" width="515" height="34" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="22" y="67">r = dma_chan_free</text>
    <text x="227" y="67">dma_chan_abort</text>
    <text x="412" y="67">r = dma_chan_free</text>
    <text x="227" y="147">if (r &lt; 0 &amp;&amp; d-&gt;cleanup_err == DRV_OK) d-&gt;cleanup_err = r;</text>
    <text x="227" y="217">d-&gt;dma_ch = -1; return → 다음 라벨(gpio_free, clk_disable)</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m15b)">
    <path d="M160 62 H213"/>
    <path d="M345 62 H398"/>
    <path d="M85 79 V142 H213"/>
    <path d="M475 79 V123"/>
    <path d="M472 159 V193"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="166" y="56">EBUSY</text>
    <text x="352" y="56">재시도</text>
    <text x="92" y="110">0 또는 EIO</text>
    <text x="10" y="182">EIO: abort 로도 안 풀림</text>
    <text x="10" y="198">→ 기록만, ret 는 그대로</text>
    <text x="10" y="243">r == 0 이면 기록할 것 없이 바로 아래로. 어느 경우든 사다리는 계속 내려간다.</text>
  </g>
</svg>
```

정리 경로에서 에러가 나면 할 수 있는 일은 많지 않다 — 이미 실패 중이고, 호출자는 "init 이 실패했다" 만 알면 된다. 그래서 규칙은 셋:

1. **복구 가능한 실패는 그 자리에서 복구**: `-EBUSY`(전송 중)는 abort 후 재시도로 풀린다. trace `"... !IRQ -DMA:busy ABORT -DMA -GPIO -CLK"`, 장부 0, `cleanup_err == OK`.
2. **복구 불가능한 실패는 기록하고 계속**: `-EIO` 면 채널은 샌다(장부 `dma == 1`). 그래도 GPIO·클럭은 **놓는다** — 하나가 실패했다고 사다리를 멈추면 누수가 셋으로 늘어난다. `d->cleanup_err` 에 **첫 에러만** 남겨 로그·텔레메트리로 보낸다.
3. **원래 에러를 덮지 않는다**: init 은 `-ENODEV`(IRQ 실패, 진짜 원인)를 반환해야지 `-EIO`(정리 중 2차 피해)를 반환하면 안 된다. 그래서 정리 결과는 `ret` 가 아닌 별도 변수에 담는다.

이것이 Linux 가 정리 함수를 대부분 `void` 로 두는 이유다 (`kfree`, `clk_disable_unprepare`, `free_irq` 모두 `void`). 플랫폼 드라이버 `remove()` 도 원래 `int` 를 반환했지만 드라이버 코어가 그 값을 쓸 데가 없어서 최근 커널(6.11 무렵)에서 `void` 로 바뀌었다. 실패할 수 있는 정리라면 "실패해도 강제로 끝내는" 헬퍼(여기서는 `dma_release`)로 감싸서 사다리에는 **실패하지 않는 한 줄** 만 남긴다. (실무 주의: DMA 채널을 못 놓았는데 그 뒤에 클럭을 꺼 버리면, 살아 있는 DMA 가 클럭 꺼진 주변장치에 접근할 수 있다. abort 실패는 보통 "시스템 리셋 사유" 로 올린다.)

### Part C — devm: 사다리를 데이터로 바꾸기

goto 사다리의 본질은 "**잡은 순서의 역순으로 놓는다**" 하나다. 그렇다면 잡을 때마다 해제 함수를 스택에 쌓아 두면 사다리 자체가 필요 없다.

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m15c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">devres 스택 (cap = 2): ③ 등록이 실패하는 경우</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="55">clk_enable      → add(rel_clk)  ✔</text>
    <text x="10" y="80">gpio_request    → add(rel_gpio) ✔</text>
    <text x="10" y="105">dma_chan_alloc  → add(rel_dma)  ✘ 슬롯 없음</text>
    <text x="30" y="130">  └ or_reset: rel_dma(d) 즉시 → "-DMA"</text>
    <text x="10" y="155">return -ENOMEM</text>
    <text x="10" y="190">bus_probe: ret &lt; 0 → devres_release_all</text>
    <text x="30" y="215">  pop rel_gpio → "-GPIO", pop rel_clk → "-CLK"</text>
  </g>
  <g stroke="currentColor">
    <rect x="520" y="150" width="160" height="34" class="f-acc"/>
    <rect x="520" y="116" width="160" height="34" class="f-acc"/>
    <rect x="520" y="82" width="160" height="34" class="f-warn" stroke-dasharray="4 3"/>
    <path d="M510 190 H690" fill="none"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="535" y="172">res[0] rel_clk</text>
    <text x="535" y="138">res[1] rel_gpio</text>
    <text x="535" y="104">(res[2] 없음)</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m15c)">
    <path d="M700 120 V180"/>
  </g>
  <text x="590" y="210" fill="currentColor" font-size="12">pop 순서 (LIFO)</text>
  <text x="10" y="248" fill="currentColor">trace: +CLK +GPIO +DMA -DMA -GPIO -CLK  — goto 버전과 똑같은 역순, 누수 0</text>
</svg>
```

`uart_probe_devm` 에는 해제 코드가 **한 줄도 없다**. 모든 실패가 `return ret;` 이고, 정리는 `bus_probe()` (= Linux 드라이버 코어의 `really_probe()` 에 해당) 가 한다. 그래서 같은 실패 행렬에서 trace 가 goto 버전과 글자 단위로 같다 (테스트가 같은 `fault_matrix[]` 를 재사용해 확인). `_or_reset` 이 왜 필요한가: 등록에 실패한 자원은 스택에 없으므로, 그 자리에서 놓지 않으면 아무도 놓지 않는다.

실제 Linux 에서 같은 드라이버를 두 방식으로 쓰면 이렇다 (설명용, 컴파일하지 않음):

```c
/* (1) 전통적인 goto 사다리 */
static int foo_probe(struct platform_device *pdev)
{
	struct foo *foo;
	int ret;

	foo = kzalloc(sizeof(*foo), GFP_KERNEL);
	if (!foo)
		return -ENOMEM;
	foo->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(foo->clk)) {
		ret = PTR_ERR(foo->clk);
		goto err_free;
	}
	ret = clk_prepare_enable(foo->clk);
	if (ret)
		goto err_put_clk;
	ret = request_irq(platform_get_irq(pdev, 0), foo_isr, 0, "foo", foo);
	if (ret)
		goto err_disable_clk;
	platform_set_drvdata(pdev, foo);
	return 0;

err_disable_clk:
	clk_disable_unprepare(foo->clk);
err_put_clk:
	clk_put(foo->clk);
err_free:
	kfree(foo);
	return ret;
}

/* (2) devm: 해제는 드라이버 코어가 역순으로 */
static int foo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct foo *foo;
	int irq;

	foo = devm_kzalloc(dev, sizeof(*foo), GFP_KERNEL);
	if (!foo)
		return -ENOMEM;
	foo->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(foo->clk))
		return dev_err_probe(dev, PTR_ERR(foo->clk), "no clock\n");
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	return devm_request_irq(dev, irq, foo_isr, 0, "foo", foo);
}
```

(1) 에는 실제로 버그가 하나 있다: `platform_get_irq()` 의 음수 반환(예: `-EPROBE_DEFER`)을 검사하지 않고 `request_irq()` 에 넘겨서, 원인 코드가 `request_irq` 의 다른 에러로 **바뀌어** 버린다 — 이번 레슨의 "코드 덮어쓰기" 와 같은 병이다. (2) 는 `irq < 0` 을 따로 검사해 그대로 돌려준다 — 사다리가 길수록 이런 작은 검사가 빠지기 쉽다는 게 devm 의 존재 이유다. `dev_err_probe()` 는 `-EPROBE_DEFER`(의존 장치가 아직 없음, 나중에 다시 probe) 일 때 에러 로그를 찍지 않는다.

devm 의 함정 (면접 꼬리질문으로 잘 나온다):

| 함정 | 설명 |
|---|---|
| devm IRQ 는 **remove() 가 끝난 뒤** 해제 | remove() 에서 하드웨어/버퍼를 먼저 해체하면, 그 사이에 ISR 이 들어와 해체된 것을 만진다. 필요하면 remove() 에서 `disable_irq()` 또는 IRQ 만 수동 관리 |
| devm 과 수동 해제를 섞으면 순서가 뒤엉킴 | devm 자원은 수동 해제(remove 본문)보다 **나중에** 풀린다. "devm 으로 잡은 것에 의존하는 수동 자원" 은 안전, 반대는 위험 |
| 장치 수명보다 오래 사는 객체 | 사용자 공간이 연 파일·char dev 가 unbind 뒤에도 `devm_kzalloc` 메모리를 가리킬 수 있다 |

### Part C — 다른 스타일과의 비교

같은 4단계를 다섯 가지로 써 볼 수 있다. 추가 연습 1·2 는 그중 둘을 실제로 구현해 **같은 실패 행렬에서 같은 trace** 를 내는지 확인한다.

| 스타일 | 해제 코드의 위치 | 장점 | 단점 |
|---|---|---|---|
| **goto 사다리** | 함수 끝에 한 번씩 | 평평함, 해제가 한 곳·역순, 단계 추가가 쉬움 | `goto` (MISRA 15.1 advisory), 라벨 오배선 위험 |
| 중첩 if (연습 1) | 각 블록 닫힐 때 | goto 없음 | 4단계면 들여쓰기 4단, 단계 추가 시 전체 재들여쓰기, `if (ret != 0)` 반복 |
| 실패마다 즉시 return + 해제 복붙 | 각 실패 지점 | 직관적 | 해제 코드가 k(k-1)/2 줄로 **중복** → 단계 추가 시 한 곳을 빠뜨림 (가장 흔한 누수 원인) |
| 단계 카운터 + switch fall-through (연습 2) | `teardown(done)` 한 함수 | goto 없음, **init 실패 경로와 deinit 이 같은 코드** | 단계 번호를 손으로 맞춰야 함, 코드가 약간 큼 |
| devm / 해제 스택 (Part C) | 등록한 콜백 | 해제 코드 0줄 | 스택 메모리, 함수 포인터 간접 호출, 순서가 "숨겨짐" |

Cortex-M4 에서 스타일별 코드 크기는 어떨까? `demo_codegen.c` 의 세 함수를 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2` 로 컴파일한 실제 결과 (`xcrun llvm-nm --size-sort -S`):

```text
00000000 00000044 T init_goto       ← 68 B
00000044 00000048 T init_nested     ← 72 B
0000008c 00000062 T init_staged     ← 98 B
```

goto 와 중첩 if 는 **사실상 같은 기계어** 다. 둘 다 "실패 지점마다 `mov r4, r0` 후 해제 꼬리로 분기" 하는 같은 CFG 로 접힌다 (중첩 if 의 4바이트 차이는 `return ret < 0 ? ret : 0` 의 `and.w r0, r0, r0, asr #31` 한 줄). `init_goto` 의 꼬리:

```text
.LBB0_4:  mov r4, r0 ; bl dma_free ; b .LBB0_7
.LBB0_5:  mov r4, r0 ; b .LBB0_8
.LBB0_6:  mov r4, r0
.LBB0_7:  bl gpio_free
.LBB0_8:  bl clk_disable ; mov r0, r4 ; pop {r4, pc}
```

라벨의 fall-through 가 그대로 기계어의 fall-through 다. 반면 switch 버전은 `done` 값을 레지스터에 만들고 `cmp r1,#1 / cmp r1,#2 / cmp r1,#3` 비교 사슬을 거쳐 같은 꼬리로 들어가서 30바이트 더 크다. 즉 goto 를 피하는 것은 **가독성·규칙 준수의 선택** 이지 성능의 선택이 아니다 — 컴파일러에게는 모든 `if`/`switch` 가 결국 goto 다.

### MISRA C 와 goto

| 규칙 (MISRA C:2012) | 분류 | 내용 | goto 사다리는? |
|---|---|---|---|
| 15.1 | Advisory | goto 를 쓰지 말아야 한다 (should not) | **위반** → 문서화된 deviation 으로 허용 가능 |
| 15.2 | Required | goto 는 같은 함수의 **뒤쪽** 라벨로만 | 준수 (모든 점프가 아래로) |
| 15.3 | Required | goto 가 참조하는 라벨은 같은 블록이나 **둘러싼** 블록에 | 준수 (라벨은 함수 최외곽 블록) |
| 15.5 | Advisory | 함수의 출구는 끝에 하나 | goto 사다리는 오히려 **잘 맞는다** (`err_out: return ret;`). 다만 정답의 `return DRV_OK;` 와 double-init 의 조기 return 은 출구를 늘린다 — 엄격히 하려면 성공도 `ret = DRV_OK; goto out;` 으로 정리 라벨들 아래의 `out: return ret;` 하나로 모은다 |
| 17.7 | Required | 반환값이 있는 함수의 결과를 쓰거나 `(void)` 로 명시적으로 버린다 | 시작 코드의 `(void)dma_chan_free(...)` 가 "규칙은 지키지만 버그" 인 예 |

MISRA C:2004 에서는 goto 가 **Required** 금지(14.4)였고, 단일 출구도 Required(14.7)였다. 2012 판이 15.1 을 Advisory 로 낮추고 15.2/15.3 으로 "어떤 goto 가 안전한가" 를 정의한 것은 바로 이 **앞으로만 가는 정리 점프** 를 위한 여지다. BARR-C:2018 도 goto 를 "피하는 것이 바람직" 수준으로 두고 같은 방향의 제한을 건다. 실무 결론: 안전 인증 프로젝트라면 연습 2 의 단계 카운터 + `teardown()` 을 쓰거나, goto 사다리를 deviation 으로 등록한다. 그 외에는 CERT C MEM12-C 와 Linux coding-style 7장이 goto 체인을 **권장** 한다.

### goto 의 C 표준 규칙과 함정 (demo_goto_pitfalls.c)

- 라벨은 **함수 범위(function scope)** 를 갖는다 (C11 6.2.1p3) — 같은 함수 어디서나 보이고, 이름 공간도 따로다 (6.2.3).
- goto 로 **VLA 의 범위 안으로** 뛰어드는 것은 제약 위반이다 (6.8.6.1p1). 일반 변수는 그렇지 않다 — 초기화 있는 선언을 건너뛰어도 **합법** 이지만, 그 변수는 초기화가 실행되지 않아 값이 불확정(indeterminate)이다 (6.8p3, 6.2.4p6). C++ 은 이것을 컴파일 에러로 막는다.
- C11 에서 라벨 뒤에는 반드시 **문장** 이 와야 한다 (6.8.1). `out: }` 는 C23 부터 허용.

`demo_goto_pitfalls.c` 를 `cc -std=c11 -Wall -Wextra -O2 -c` 로 빌드한 실제 출력:

```text
demo_goto_pitfalls.c:16:9: warning: variable 'ch' is used uninitialized whenever 'if' condition is true [-Wsometimes-uninitialized]
   16 |     if (ret < 0) goto err;
demo_goto_pitfalls.c:23:15: note: uninitialized use occurs here
   23 |     chan_free(ch);              /* ret < 0 첫 경로에서 ch 는 indeterminate */
demo_goto_pitfalls.c:47:1: warning: label at end of compound statement is a C23 extension [-Wc23-extensions]
2 warnings generated.
```

두 번째 함정인 **"one err bug"** (라벨 하나에 몰아서 `free(foo->bar); free(foo);` — `foo == NULL` 경로에서 NULL 역참조, Linux coding-style 이 이 이름으로 경고하는 패턴)는 **컴파일러 경고가 없다**. `clang --analyze` 로는 잡힌다:

```text
demo_goto_pitfalls.c:23:5: warning: 1st function call argument is an uninitialized value [core.CallAndMessage]
demo_goto_pitfalls.c:37:10: warning: Access to field 'bar' results in a dereference of a null pointer (loaded from variable 'foo') [core.NullDereference]
```

같은 파일을 C++ 로 컴파일하면(`c++ -std=c++17 -x c++`) 첫 함정은 `error: cannot jump from this goto statement to its label` 로 막힌다. 교훈: 라벨을 **잡은 자원 개수만큼** 나누면 one err bug 는 구조적으로 사라지고, 변수는 **함수 맨 위에서 선언** 해 두면 skip-init 함정도 사라진다.

### 덤: `__attribute__((cleanup))` — 컴파일러가 대신 놓아 주기

GCC/clang 확장(ISO C 아님)인 `__attribute__((cleanup(fn)))` 을 지역 변수에 붙이면, 그 변수가 **스코프를 벗어날 때** `fn(&var)` 가 불린다 — 변수 선언의 역순으로. Linux 6.5 의 `<linux/cleanup.h>` (`__free(kfree)`, `guard(mutex)(&lock)`) 가 이것으로 만들어졌다. `demo_cleanup_attr.c` 의 실제 출력:

```text
fail_at=3
  +CLK
  +GPIO
  !DMA
  -GPIO
  -CLK
  -> ret=-12, held=0
fail_at=4
  +CLK
  +GPIO
  +DMA
  !IRQ
  -DMA
  -GPIO
  -CLK
  -> ret=-19, held=0
fail_at=5
  +CLK
  +GPIO
  +DMA
  -> ret=0, held=3
```

성공 경로에서는 변수를 0 으로 바꿔 "소유권을 넘긴다" (커널의 `no_free_ptr()` 와 같은 발상) — 그래서 `held=3` 이 맞다. `-std=c11 -pedantic` 으로도 clang 은 경고하지 않았다 (`__attribute__` 는 구현 예약 식별자라 확장이 허용됨). 하지만 IAR·옛 ARMCC·MISRA 검사기에서는 이식성이 없고, 표준화는 WG14 의 `defer` 기술 명세(TS) 논의로 진행 중이다. 펌웨어에선 "알아 두되 기본값은 goto 사다리" 가 무난하다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution            # 62/62 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 62/62, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 단계 k 실패에서 **k 를 놓는 라벨** 로 점프 (`if (dma 실패) goto err_dma;`) | 안 잡은 채널을 놓음 → 이중 해제/남의 자원 해제 | k-1 을 놓는 라벨로: `goto err_gpio;` |
| 라벨 순서를 잡은 순서대로 배치 (`err_clk:` 가 맨 위) | fall-through 방향이 거꾸로 → 클럭을 먼저 끄고 DMA 를 나중에 | 라벨은 **역순**: `err_dma → err_gpio → err_clk` |
| 정리 경로에서 `ret = -1;` 또는 정리 함수의 반환값을 `ret` 에 대입 | 원인(`-ETIMEDOUT`)이 지워지거나 2차 에러(`-EIO`)로 덮임 | 정리 결과는 `cleanup_err` 같은 별도 변수 |
| 정리 함수 하나가 실패하면 `return` | 나머지 자원 전부 누수 | 기록하고 계속 (Part B 경계 4) |
| 라벨 하나 `err:` 에 모든 해제를 몰고 NULL/-1 검사로 구분 | 검사 하나만 빠져도 one err bug (NULL 역참조, 쓰레기 핸들) | 자원마다 라벨, 또는 연습 2 의 단계 카운터 |
| goto 가 초기화 있는 선언을 건너뜀 | C 에서는 합법이지만 변수 값 불확정 (`-Wsometimes-uninitialized`) | 변수를 함수 맨 위에서 선언·초기화 |
| `if (status & READY_MASK)` 로 다중 비트 대기 | 비트 하나만 서도 통과 | `(status & mask) == mask` |
| `while (!(REG & RDY));` 무한 대기 | 하드웨어 고장·클럭 미공급 시 영원히 멈춤 → 워치독 리셋 루프 | 경계 있는 루프 + `-ETIMEDOUT` |
| `ready = true` 를 중간에 세움 | 그 뒤 단계가 실패하면 deinit 이 안 잡은 것까지 놓음 | 모든 단계 성공 후 마지막 줄에 |
| double-init 검사를 자원 획득 **뒤** 에 둠 | 실패한 두 번째 init 의 에러 경로가 첫 init 의 자원을 놓음 | 함수 첫 줄에서 검사 |
| deinit 에서 DMA 를 IRQ 보다 먼저 놓음 | 그 사이 ISR 이 반납된 채널을 만짐 | IRQ → DMA → GPIO → CLK |

## 8. 면접 Q&A

<details>
<summary>Q1. goto 는 나쁜 것 아닌가요? 왜 커널은 goto 를 씁니까?</summary>

Dijkstra(1968)가 비판한 것은 **뒤로·아무 데로나** 뛰는 스파게티 제어 흐름이다. 에러 정리용 goto 는 **앞으로만, 함수 끝의 정리 블록으로만** 뛴다 — 사실상 C 에 없는 `try/finally` 를 흉내 내는 구조적 사용이다. Linux coding-style 7장의 근거: 무조건 점프라 이해가 쉽고, 들여쓰기가 줄고, 출구마다 해제를 복붙하다 하나를 빠뜨리는 실수를 막는다. 실측으로도 goto 사다리와 중첩 if 는 Cortex-M4 에서 68 B vs 72 B 로 같은 기계어 구조였다. 꼬리질문 "MISRA 는?" → 2012 판 15.1 은 Advisory 이고 15.2(앞으로만)/15.3(같은·바깥 블록)을 지키면 deviation 으로 허용하는 프로젝트가 많다.

</details>

<details>
<summary>Q2. 에러 코드를 음수 errno 로 할지 enum 으로 할지, 어떻게 정하나요?</summary>

이 레슨은 둘을 합쳤다: **값은 음수 errno(리눅스 번호), 이름은 enum**. 음수 규약은 `if (ret < 0)` 한 줄과 "0 이상 = 핸들/바이트 수" 를 같이 쓸 수 있게 해 주고 Linux·Zephyr·CMSIS-Driver 와 맞는다. `<errno.h>` 를 직접 쓰지 않는 이유는 번호가 OS 마다 달라서다(macOS `ETIMEDOUT` 60, Linux 110). 꼬리질문 "enum 을 구조체에 넣어도 되나?" → enum 타입 크기는 구현 정의라 `-fshort-enums` 에서 1바이트가 된다(thumbv7em 에서 실측). ABI 경계나 직렬화에는 `int32_t` 를 쓴다. 성공 = 0 이 아닌 규약(nrfx 의 `NRFX_SUCCESS = 0x0BAD0000`)과 섞일 때는 경계에서 변환 함수를 한 번 두는 것이 안전하다.

</details>

<details>
<summary>Q3. 정리(cleanup) 도중에 에러가 나면 어떻게 하나요?</summary>

세 가지 원칙: ① 복구 가능하면 그 자리에서(EBUSY → abort → 재시도) ② 불가능하면 **기록하고 계속** — 나머지 자원은 놓는다 ③ 호출자에게는 **원래 에러** 를 돌려준다(2차 에러로 덮지 않음). 그래서 정리 API 는 가능하면 `void` 로 설계한다(Linux `free_irq`, `clk_disable_unprepare` 등). 꼬리질문 "그래도 채널이 샜다면?" → `cleanup_err` 를 로그/텔레메트리로 보내고, 그 자원을 다시 쓰려는 init 은 `-EBUSY` 로 실패하게 두거나, 안전 관련 시스템이면 리셋 사유로 올린다.

</details>

<details>
<summary>Q4. deinit 을 멱등하게 만드는 방법은? 왜 필요한가요?</summary>

방법: ① `ready` 플래그로 "완전히 init 됨" 을 표시하고 deinit 첫 줄에서 검사, 정리 **전에** 내린다 ② 또는 자원마다 핸들을 두고 놓은 뒤 무효값(-1/NULL)으로 바꾼다 (`dma_release` 의 `d->dma_ch = -1`) ③ devres 스택처럼 "놓은 것은 스택에서 빠짐". 필요한 이유: 에러 복구 경로, 전원 관리(suspend 실패 롤백), 워치독/결함 처리기, 상위 계층의 "혹시 모르니 deinit" 호출이 같은 장치를 여러 번 정리하려 하기 때문. 이중 해제는 refcount 를 음수로 만들거나(unsigned 면 `UINT_MAX` 로 돌아서 영영 안 꺼짐 — 연습 4) 다른 드라이버의 클럭을 꺼 버린다.

</details>

<details>
<summary>Q5. Linux devm_* 는 무엇이고, 함정은 무엇인가요?</summary>

`devm_kzalloc`, `devm_clk_get_enabled`, `devm_request_irq`, `devm_add_action_or_reset` 등은 자원을 잡으면서 해제 콜백을 장치(`struct device`)의 devres 리스트에 등록한다. probe 가 음수를 반환하거나 장치가 unbind 되면 드라이버 코어가 **등록 역순** 으로 해제한다 — Part C 의 `bus_probe`/`bus_remove` 가 그 흉내다. 함정: devm IRQ 는 `remove()` 가 끝난 **뒤** 에 해제되므로, remove() 가 먼저 하드웨어나 버퍼를 해체하면 그 사이 ISR 이 해체된 것을 만진다. 또 devm 과 수동 관리를 섞으면 해제 순서가 "수동 먼저, devm 나중" 으로 고정되어 의존 관계가 뒤집힐 수 있다.

</details>

<details>
<summary>Q6. 레지스터 ready 비트를 기다리는 루프를 쓸 때 무엇을 조심하나요?</summary>

① **경계**: 무한 루프 금지 — 클럭이 안 들어오면 영원히 멈추고 워치독 리셋이 반복된다 ② `volatile` 접근(C-B09) ③ 다중 비트면 `(v & mask) == mask` ④ 시간 기반이면 tick 랩어라운드에 안전한 `(uint32_t)(now - start) >= timeout` ⑤ 타임아웃 판정 직전 **한 번 더 읽기** — 선점 때문에 시간만 흘렀을 수 있다 (Linux `read_poll_timeout` 이 이렇게 함) ⑥ 실패 시 **이 단계가 이미 켠 것** 을 끄고 나간다. 꼬리질문 "타임아웃 값은 어떻게 정하나?" → 데이터시트의 최대 lock/startup 시간(예: HSE startup 수 ms) × 여유 계수, 그리고 그 값을 이름 있는 상수로.

</details>

<details>
<summary>Q7. 단계 하나가 자원을 두 개 잡는다면 라벨은 어떻게 하나요?</summary>

라벨은 "단계" 가 아니라 "자원" 마다 둔다. 예: DMA 단계가 채널 + 디스크립터 메모리를 잡으면 `err_free_desc:` 와 `err_free_chan:` 두 라벨. 채널 할당 실패 → 이전 단계 라벨, 디스크립터 할당 실패 → `err_free_chan`. 이번 문제의 클럭 단계가 그 축소판이다: `clk_enable` 실패는 `err_out`, `wait_ready` 실패는 `err_clk`. 이것을 하나로 뭉치면 "NULL 이면 건너뛰기" 같은 조건이 생기고, 조건 하나를 빠뜨리는 순간 one err bug 가 된다.

</details>

<details>
<summary>Q8. 반환값 무시를 컴파일 타임에 막을 수 있나요?</summary>

GCC/clang 의 `__attribute__((warn_unused_result))` (Linux 의 `__must_check`), C23 의 `[[nodiscard]]`. MISRA C:2012 Rule 17.7 은 "반환값을 쓰거나 `(void)` 로 명시적으로 버려라" 를 Required 로 둔다. 단, `(void)` 캐스트는 경고를 끌 뿐 버그를 고치지 않는다 — 시작 코드의 `(void)dma_chan_free(ch);` 가 정확히 그 예로, 규칙은 지키지만 EBUSY 일 때 채널이 샌다. 리뷰에서 `(void)` 가 붙은 정리 호출은 "정말 실패할 수 없나?" 를 물어볼 곳이다.

</details>

## 9. 추가 연습

### 연습 1 — 중첩 if 로 다시 쓰기

goto 없이 중첩 `if` 만으로 같은 4단계 init 을 써서, 본문의 실패 행렬(주입 없음 + k = 1..4)에서 trace 가 goto 버전과 같음을 확인하라.

<details>
<summary>답 보기</summary>

각 단계 성공 블록 안에 다음 단계를 넣고, 블록을 닫은 직후 `if (ret != 0) 해제();` 를 둔다. IRQ 실패는 가장 안쪽에서 `dma_free()`, 블록을 빠져나오며 `gpio_free()`, `clk_disable()` 이 차례로 실행되어 역순이 된다. 4단계에 들여쓰기 4단, `if (ret != 0)` 검사 3번 — 단계가 늘수록 오른쪽으로 밀린다. 기계어는 goto 버전과 같은 구조였다 (`demo_codegen.c`, 68 B vs 72 B).

</details>

### 연습 2 — 단계 카운터 + switch fall-through (goto 없는 단일 정리 함수)

성공한 단계 수 `done` 을 세고, `teardown(done)` 하나로 init 실패 경로와 deinit 을 모두 처리하라. 단계는 함수 포인터 배열로 돌려라.

<details>
<summary>답 보기</summary>

`switch (done) { case 4: irq_free(); case 3: dma_free(); case 2: gpio_free(); case 1: clk_disable(); default: break; }` — case 가 큰 수부터 내려가며 fall-through 하므로 역순 해제가 된다. init 은 `while (done < 4 && (ret = step[done]()) == 0) done++;` 후 실패면 `teardown(done)`. deinit 은 `teardown(4)`. MISRA 15.1 을 지키면서 해제 코드가 한 곳이다. 주의: MISRA 16.3 은 "모든 switch-clause 는 break 로 끝나야 한다" (Required)라서 이 fall-through 는 그 규칙에 걸린다 — 안전 인증 코드에서는 `if (done >= 4) irq_free(); if (done >= 3) dma_free(); ...` 로 바꾼다. 또 clang 의 `-Wimplicit-fallthrough` 는 `/* fall through */` 주석을 인정하지 않았다(실측: 경고 4개). 켜려면 `__attribute__((fallthrough));` 또는 C23 `[[fallthrough]];` 를 쓴다 (GCC 는 주석도 인정).

</details>

### 연습 3 — 에러 코드 → 이름 (`drv_strerror`)

로그용으로 `drv_strerror(int code)` 를 작성하라. 표에 없는 코드, 양수, `INT_MIN` 은 `"EUNKNOWN"`.

<details>
<summary>답 보기</summary>

지정 초기화자(designated initializer)로 `-code` 를 인덱스로 쓴다: `[-DRV_ETIMEDOUT] = "ETIMEDOUT"`. 표 크기는 111 이고 지정 안 한 칸은 NULL (정적 저장 기간 → 0 초기화, 6.7.9p10) 이라 NULL 검사로 구멍을 거른다. `_Static_assert` 로 표가 가장 큰 코드까지 덮는지 고정한다. 범위 검사는 `code <= -ERR_N` 으로 **부정하기 전에** 한다 — `-code` 를 먼저 계산하면 `INT_MIN` 에서 signed overflow (UB, C-M12 참고). 펌웨어에서 문자열 표가 아까우면 숫자만 로그에 남기고 호스트 도구에서 이름으로 바꾼다.

</details>

### 연습 4 — 두 드라이버가 공유하는 클럭 (refcount)

UART 와 SPI 가 같은 버스 클럭을 쓴다. `clk_get()` / `clk_put()` 을 refcount 로 구현해, 마지막 사용자가 놓을 때만 게이트를 끄고, 불균형한 `put` 은 `DRV_EINVAL` 로 거절하라.

<details>
<summary>답 보기</summary>

`if (clk_ref++ == 0) 게이트 on;` / `if (clk_ref == 0) return DRV_EINVAL; if (--clk_ref == 0) 게이트 off;`. 게이트 레지스터는 0↔1 전이에서만 쓴다(테스트: 쓰기 2번). 불균형 검사가 없으면 `unsigned` 카운터가 `UINT_MAX` 로 돌아(정의된 모듈러 연산이라 sanitizer 도 조용하다) 그 뒤로는 영영 꺼지지 않는다. 이것이 본문의 double-init 이 위험한 실제 이유다 — 두 번째 init 이 refcount 를 올리면 deinit 한 번으로는 클럭이 안 꺼진다. Linux 의 `clk_enable` 도 refcount 이고, 불균형 disable 은 `WARN` 을 찍는다.

</details>

### 연습 5 — 시간 기반 타임아웃: 랩어라운드와 마지막 재확인

32비트 tick 카운터 `now()` 로 `wait_ready_until(timeout)` 을 작성하라. (a) tick 이 `0xFFFFFFFC` 에서 시작해 0 을 넘어가도 정상 동작 (b) 조건 확인 직후 1000 tick 동안 선점된 사이에 하드웨어가 ready 가 됐다면 `OK` 를 돌려줄 것.

<details>
<summary>답 보기</summary>

`if ((uint32_t)(now() - start) >= timeout) return (rd() & 1u) ? DRV_OK : DRV_ETIMEDOUT;`. (a) `deadline = start + timeout` 을 만들어 `now() > deadline` 으로 비교하면, `0xFFFFFFFC + 0x20` 이 `0x1C` 로 돌아가 첫 비교(`0xFFFFFFFD > 0x1C`)에서 바로 타임아웃이 된다. 빼기 형태는 모듈러 차이라 랩어라운드와 무관하다 (timeout < 2³¹ 조건). (b) 시간 확인만으로 포기하면 "ready 인데 타임아웃" 오판 — 마지막 재확인으로 막는다. 테스트는 `now()` 의 2번째 호출에서 tick 을 1000 건너뛰고 동시에 ready 를 세워 이 상황을 결정적으로 만든다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 37/37 passed
```

## 10. 핵심 정리

- 에러 규약은 하나로: **0 = 성공, 음수 = 에러(errno 값을 enum 이름으로 고정), 0 이상 = 핸들**. 원인 코드를 **덮어쓰지 않고** 끝까지 전파한다.
- goto 사다리: 위에서 잡고, 단계 k 실패 → "k-1 을 놓는 라벨" 로, 라벨은 **역순** 으로 놓고 fall-through. 라벨 이름 = 그 라벨이 놓는 것.
- goto 대상은 단계 번호가 아니라 **"지금까지 잡은 것"** — enable 은 됐는데 ready 타임아웃이면 그 클럭도 끈다.
- 정리 중 실패: 복구 가능하면 재시도, 아니면 **기록하고 계속**, 반환은 원래 에러. 정리 API 는 가능하면 `void`.
- double init 은 첫 줄에서 `-EBUSY`, deinit 은 플래그/핸들 무효화로 **멱등**, `ready = true` 는 마지막 줄.
- 폴링은 반드시 경계 있게: `(v & mask) == mask`, 시간 기반이면 `(uint32_t)(now - start) >= t` + 마지막 재확인.
- 대안: 중첩 if(같은 기계어, 깊은 들여쓰기), 단계 카운터 + `teardown()`(MISRA 15.1 준수), devm 해제 스택(해제 코드 0줄, remove 순서 함정), `__attribute__((cleanup))`(비표준).
- MISRA C:2012: 15.1 goto 금지는 **Advisory**, 15.2 앞으로만·15.3 같은/바깥 블록은 Required — goto 사다리는 15.1 만 deviation.

## 참고자료

- C11 (N1570) 6.8.6.1 goto, 6.8.1 labeled statements, 6.2.1p3 label scope, 6.8p3·6.2.4p6 자동 변수 초기화 시점, 6.7.2.2 enum, 6.7.9p10 정적 객체 0 초기화 — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- [Linux kernel coding style — 7) Centralized exiting of functions](https://www.kernel.org/doc/html/latest/process/coding-style.html#centralized-exiting-of-functions)
- [Linux kernel — Devres: Managed Device Resource](https://www.kernel.org/doc/html/latest/driver-api/driver-model/devres.html)
- [Linux kernel — Scope-based resource management (`linux/cleanup.h`)](https://www.kernel.org/doc/html/latest/core-api/cleanup.html)
- Linux `include/linux/iopoll.h` — `read_poll_timeout()` (타임아웃 후 마지막 재확인)
- [SEI CERT C — MEM12-C. Consider using a goto chain when leaving a function on error when using and releasing resources](https://wiki.sei.cmu.edu/confluence/display/c/MEM12-C.+Consider+using+a+goto+chain+when+leaving+a+function+on+error+when+using+and+releasing+resources)
- MISRA C:2012 (MISRA C:2023 도 같은 번호) — Rule 15.1/15.2/15.3/15.5, 16.3, 17.7; MISRA C:2004 Rule 14.4/14.7
- Barr Group, *Embedded C Coding Standard* (BARR-C:2018) — 키워드 사용 규칙(goto)
- [Zephyr — Device Driver Model (init 함수는 0 / 음수 errno 반환)](https://docs.zephyrproject.org/latest/kernel/drivers/index.html)
- [CMSIS-Driver — 공통 반환 코드 (`ARM_DRIVER_OK`, `ARM_DRIVER_ERROR_TIMEOUT` …)](https://arm-software.github.io/CMSIS_6/latest/Driver/index.html)
- E. W. Dijkstra, "Go To Statement Considered Harmful", CACM 1968
- 관련 레슨: C-B02 (unsigned 모듈러 빼기), C-B09 (volatile 폴링), C-M06 (ops 구조체·콜백), C-M07 (상태 머신), C-M12 (signed overflow UB), C-M14 (ISR 공유 데이터)
