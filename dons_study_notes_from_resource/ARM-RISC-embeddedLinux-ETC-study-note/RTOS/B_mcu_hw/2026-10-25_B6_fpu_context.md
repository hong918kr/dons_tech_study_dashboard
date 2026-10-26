# B6. FPU와 컨텍스트 — FPU를 켜는 순간 예외 프레임 · 컨텍스트 스위치 · 스택 예산이 바뀐다

> **이 노트를 다 읽으면**: Cortex-M4F에서 FPU를 올바른 순서(CPACR → DSB/ISB)로 켜고, 안 켰을 때의 NOCP fault를 레지스터 값으로 알아본다 · 26워드 확장 예외 프레임과 lazy stacking(FPCCR.ASPEN/LSPEN, FPCAR, CONTROL.FPCA, EXC_RETURN 0xFFFFFFE1/E9/ED)을 ISR 안에서 직접 덤프해 설명한다 · FreeRTOS ARM_CM4F 포트와 Zephyr `CONFIG_FPU_SHARING`이 FP 문맥을 어떻게 저장하는지 소스로 짚고, 태스크당 비용(+136 B)을 측정값으로 말한다 · SMLAD · SSAT · QADD 같은 DSP 명령과 Helium(MVE) beat 구조로 신호 처리 루프를 몇 배 줄이는지 숫자로 보여 준다
> **왜 필요한가**: 드론 자세 제어 · 모터 FOC · 센서 퓨전 코드는 거의 다 float이나 Q15다. 면접에서는 "ISR에서 float 써도 되나?", "M3에서 M4F로 옮겼더니 가끔 계산이 틀린다", "lazy stacking이 뭔가"가 단골이고, 실무에서는 FP를 한 번 쓴 태스크 때문에 스택이 넘치거나 인터럽트 지연이 늘어난다
> **Don 기준 난이도**: 예외 진입 · 스택 프레임 · 레지스터 저장은 SSD 펌웨어에서 익숙하다 / 새로 배울 부분은 FP 레지스터를 "누가, 언제, 어디에" 저장하는지(하드웨어 lazy vs RTOS PendSV), FPCA 한 비트가 프레임 크기를 바꾼다는 사실, DSP · Helium 명령의 실제 이득
> **선행 노트**: B1(프로그래머 모델, CONTROL), B2(예외 진입 · 8워드 스태킹 · EXC_RETURN), B4(PendSV). 이어지는 노트: B7(코어 세대 지도 — M55/M85 Helium 위치), C2(PendSV 어셈블리 한 줄씩), E 모듈(인터럽트 설계)
> 🔁 **Bare-Metal 겹침**: BM C10(FPU · DSP · Helium) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 RTOS 없이 main 루프 하나에서 FPU · DSP를 쓰는 bring-up 관점(시작 코드 `SystemInit`의 FPU 켜기, CMSIS-DSP 링크 설정)만 더한다

---

## 0. 큰 그림 — FPU는 "레지스터 32개짜리 두 번째 CPU 상태"다

Don이 SSD 컨트롤러 펌웨어에서 인터럽트 핸들러를 쓸 때 걱정한 것은 정수 레지스터뿐이었다. Cortex-R/M 코어에 FPU가 없거나, 있어도 펌웨어가 쓰지 않았기 때문이다. 그런데 드론 비행 제어기(STM32F4/H7급)에서는 사정이 다르다. 자세 추정(쿼터니언), PID, 필터가 전부 `float`로 짜여 있고, 컴파일러는 `-mfloat-abi=hard`로 함수 인자까지 FP 레지스터(S0, S1 …)로 넘긴다.

FPU를 켜는 순간 CPU에는 **정수 레지스터 말고 "또 하나의 상태"**가 생긴다. Cortex-M4F의 FPU(FPv4-SP-D16)는 32비트 단정밀도 레지스터 S0–S31(= 64비트 D0–D15) 32개와 상태 레지스터 FPSCR을 가진다. 정수 레지스터처럼 이 상태도 **인터럽트가 끼어들 때, 태스크가 바뀔 때 누군가 저장해야** 한다. 문제는 그 양이다. 정수 문맥은 17워드인데 FP 문맥은 33워드가 더 붙는다.

Arm은 이걸 두 갈래로 나눴다. 정수 레지스터와 똑같은 원리다.

- **호출자 저장(caller-saved) 쪽 S0–S15 + FPSCR**: 하드웨어가 예외 진입 때 자동으로 저장한다(확장 프레임). 그래야 ISR을 그냥 C 함수로 쓸 수 있다. 다만 매번 저장하면 비싸니 **lazy stacking** — 자리만 잡아 두고, ISR이 정말 FP 명령을 쓸 때 저장한다.
- **피호출자 저장(callee-saved) 쪽 S16–S31**: C 함수는 쓰기 전에 스스로 저장한다(AAPCS 규칙). 그래서 ISR은 걱정할 필요가 없지만, **태스크를 통째로 바꾸는 RTOS의 PendSV는 직접 저장해야** 한다. 정수의 R4–R11과 똑같은 처지다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">예외 · 컨텍스트 스위치 때 누가 무엇을 저장하나 (Cortex-M4F)</text>
<text x="250.0" y="52.0" font-size="13" text-anchor="middle" font-weight="bold">정수 레지스터</text>
<text x="520.0" y="52.0" font-size="13" text-anchor="middle" font-weight="bold">FP 레지스터 (FPv4-SP-D16)</text>
<rect x="10.0" y="70.0" width="120.0" height="90.0" rx="6" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="70.0" y="102.5" font-size="13" text-anchor="middle">하드웨어</text>
<text x="70.0" y="119.5" font-size="13" text-anchor="middle">예외 진입 때</text>
<text x="70.0" y="136.6" font-size="13" text-anchor="middle">자동 저장</text>
<rect x="10.0" y="180.0" width="120.0" height="90.0" rx="6" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="70.0" y="212.6" font-size="13" text-anchor="middle">소프트웨어</text>
<text x="70.0" y="229.6" font-size="13" text-anchor="middle">PendSV가</text>
<text x="70.0" y="246.6" font-size="13" text-anchor="middle">저장</text>
<rect x="145.0" y="70.0" width="210.0" height="90.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="250.0" y="102.5" font-size="13" text-anchor="middle">R0–R3, R12, LR, PC, xPSR</text>
<text x="250.0" y="119.5" font-size="13" text-anchor="middle">8워드 = 32 B</text>
<text x="250.0" y="136.6" font-size="13" text-anchor="middle">caller-saved (AAPCS)</text>
<rect x="370.0" y="70.0" width="300.0" height="90.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="520.0" y="102.5" font-size="13" text-anchor="middle">S0–S15 + FPSCR (+ 예약 1워드)</text>
<text x="520.0" y="119.5" font-size="13" text-anchor="middle">18워드 = 72 B, FPCA=1일 때만</text>
<text x="520.0" y="136.6" font-size="13" text-anchor="middle">lazy: 자리만 잡고 나중에 씀</text>
<rect x="145.0" y="180.0" width="210.0" height="90.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="250.0" y="212.6" font-size="13" text-anchor="middle">R4–R11 (+ EXC_RETURN)</text>
<text x="250.0" y="229.6" font-size="13" text-anchor="middle">9워드 = 36 B</text>
<text x="250.0" y="246.6" font-size="13" text-anchor="middle">callee-saved</text>
<rect x="370.0" y="180.0" width="300.0" height="90.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="520.0" y="212.6" font-size="13" text-anchor="middle">S16–S31</text>
<text x="520.0" y="229.6" font-size="13" text-anchor="middle">16워드 = 64 B</text>
<text x="520.0" y="246.6" font-size="13" text-anchor="middle">FP 태스크일 때만 (tst lr, #0x10)</text>
<text x="340.0" y="292.0" font-size="12" text-anchor="middle">FP를 쓰는 태스크는 전환될 때마다 32+36 B 대신 104+100 B(합 204 B)를 스택에 둔다</text>
</svg>
```

그림 1 — 예외 진입과 컨텍스트 스위치 때 저장 책임. 정수 쪽(왼쪽)은 B2 · C2에서 본 그대로이고, FP 쪽(오른쪽)이 이 노트의 주제다. 위 줄은 하드웨어, 아래 줄은 RTOS 몫이다.

이 노트의 질문은 셋이다.

1. FPU는 어떻게 켜고, 안 켜면 무슨 일이 나나? (§1)
2. 하드웨어는 FP 문맥을 언제 · 어떻게 저장하나? 확장 프레임, lazy stacking, EXC_RETURN (§2–§4)
3. RTOS는 그 위에 무엇을 더하나, 그 비용은 얼마인가? FreeRTOS ARM_CM4F, Zephyr, 고전 버그 (§5–§7)

그리고 FPU와 같은 "신호 처리 하드웨어"인 DSP 명령(SMLAD · SSAT · QADD)과 Helium(MVE)을 이어서 본다(§8–§10). Bare-Metal 나침반 C10이 이 묶음을 한 항목으로 두었고, 이 노트가 그 주인이다.

### Don의 경험과 연결

- **"FPCA = 이 문맥이 FP를 쓰는 중"이라는 깃발**: SSD 펌웨어에서 DMA 엔진을 쓰는 핸들러만 DMA 디스크립터 상태를 저장하던 것과 같다. 쓰는 쪽만 저장 비용을 낸다.
- **lazy stacking = write-back 캐시의 지연 쓰기**: 자리(라인)만 잡아 두고, 정말 덮어쓸 일이 생기면 그때 내려 쓴다. 인터럽트 대부분은 FP를 안 쓰므로 대개 아무것도 안 쓰고 끝난다.
- **Q15 고정소수점과 SMLAD**: SSD에서 ECC · 통계 계산을 정수 비트 연산으로 짰던 감각 그대로다. FPU가 없는 코어(M0+, M3)나 처리량이 필요한 루프에서는 지금도 Q 형식이 기본이다.

### 실습 환경 한 줄

- **QEMU `mps2-an386` = Cortex-M4 + FPv4-SP FPU**. 랩 템플릿(`templates/common`, `freertos-mps2`)을 내 폴더(`~/.rtos_scratch/B6/lab/`)로 복사해 `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`, FreeRTOS 포트 `portable/GCC/ARM_CM4F`로 바꿨다. 시작 코드에 FPU 켜기만 넣었다(§1).
- AN386의 메모리 맵 · 주변장치는 **AN385와 완전히 같다**(예제 1에서 QEMU `info mtree` 비교). UART0 `0x40004000`, TIMER0/1, FPGAIO COUNTER `0x40028018`(25 MHz)를 그대로 쓴다.
- Helium은 **QEMU `mps3-an547` = Cortex-M55**에서 실제로 실행했다(QEMU 10.2가 MVE를 구현한다). Zephyr는 v4.4.2 `mps2/an386` 보드.
- 타이밍은 **`ICOUNT=1`(명령 1개 = 32 ns 가상 시간)** 기준이다. QEMU icount 기준이라 실제 칩과 절대값은 다르다(캐시 · 플래시 wait state · 버스 미모델링). 특히 **예외 스태킹 사이클과 lazy stacking의 시간 비용은 icount에 전혀 잡히지 않는다** — §4에서 실제로 보여 주고, 실칩 비용은 "워드 수 × 사이클" 추정으로만 말한다.

### 예제 1 — AN386이 AN385와 같은 보드인지 확인 (QEMU 메모리 맵)

무엇을 확인하나: 랩 템플릿(AN385용 링커 스크립트 · UART 주소)을 AN386에서 그대로 써도 되는지.

```sh
for m in an385 an386; do
  (echo "info mtree -f"; sleep 1; echo quit) | timeout 5 qemu-system-arm -machine mps2-$m -nographic \
     -monitor stdio -serial null 2>&1 | grep -E "^  [0-9a-f]{16}" | sed -E "s/ +\(prio.*\): / /" \
     | head -60 > /var/tmp/rtos/B6/mtree_$m.txt
done
diff /var/tmp/rtos/B6/mtree_an385.txt /var/tmp/rtos/B6/mtree_an386.txt && echo SAME
grep -E "ssram|uart|fpgaio|timer" /var/tmp/rtos/B6/mtree_an386.txt | head -8
```

```text
SAME
  0000000000000000-00000000003fffff mps.ssram1
  0000000000400000-00000000007fffff mps.ssram1
  0000000020000000-00000000203fffff mps.ssram23
  0000000020400000-00000000207fffff mps.ssram23
  0000000040000000-0000000040000fff cmsdk-apb-timer
  0000000040001000-0000000040001fff cmsdk-apb-timer
  0000000040002000-0000000040002fff cmsdk-apb-dualtimer
  0000000040004000-0000000040004fff uart
```

출력에서 볼 것: 두 머신의 주소 맵이 한 줄도 다르지 않다(`SAME`). 차이는 CPU뿐이다(`mps2-an386` 기본 CPU = `cortex-m4`). 그래서 링커 스크립트(`mps2_m3.ld`를 `mps2_m4.ld`로 이름만 바꿈), `board.c`(UART · TIMER1 us 시계)를 그대로 쓴다. FPGAIO(`0x40028000`)도 같은 자리에 있다.

내 폴더에 둔 빌드 · 실행 파일의 핵심만 보인다(랩 Makefile에서 바꾼 줄).

```make
# ~/.rtos_scratch/B6/lab/fr.mk (FreeRTOS용) — bm.mk(bare-metal)도 같은 CFLAGS
PORT   ?= ARM_CM4F
CFLAGS := -mthumb -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2 -g3 -std=gnu11 -Wall -Wextra \
          ... -I$(K)/portable/GCC/$(PORT)
LDFLAGS:= -T $(L)/mps2_m4.ld -nostartfiles --specs=nano.specs ...
KSRC   := ... $(K)/portable/GCC/$(PORT)/port.c
```

```sh
# ~/.rtos_scratch/B6/lab/run.sh — rtos-run-mps2에서 머신만 바꿈
qemu-system-arm -machine ${MACHINE:-mps2-an386} -cpu ${CPU:-cortex-m4} -nographic -monitor none \
   -serial stdio -semihosting-config enable=on,target=native [-icount shift=5,align=off,sleep=off] -kernel app.elf
```

`--specs=nano.specs`에 `-mfloat-abi=hard`를 주면 링커가 `thumb/v7e-m+fp/hard` multilib(newlib-nano)을 고른다. 이 조합의 libc · libgcc는 이미 하드 float ABI로 빌드되어 있다.

---

## 1. FPU 켜기 — CPACR CP10/CP11, 그리고 DSB · ISB

> 🔁 이 절은 Bare-Metal BM C10과 겹친다 (주인: 이 노트).

### 1.1 직관 — FPU는 "꺼진 채로" 깨어난다

리셋 직후 Cortex-M4F의 FPU는 **접근 금지** 상태다. 전력을 아끼고, FPU를 안 쓰는 펌웨어가 실수로 FP 명령을 실행하면 바로 잡아내기 위해서다. FPU를 쓰려면 **CPACR(Coprocessor Access Control Register, `0xE000ED88`)**의 CP10 · CP11 필드에 접근 권한을 써 줘야 한다. Arm 아키텍처에서 FP(그리고 v8.1-M의 MVE)는 역사적으로 "보조 프로세서 10, 11번"이라는 이름을 달고 있어서다.

| CPACR 비트 | 필드 | 값 | 뜻 |
|---|---|---|---|
| 21:20 | CP10 | 0b00 / 0b01 / 0b11 | 금지 / privileged만 / 전부 허용 |
| 23:22 | CP11 | CP10과 같은 값 | 둘은 반드시 같은 값으로 쓴다 |

그래서 흔히 보는 한 줄이 `CPACR |= 0xF << 20` (CP10 = CP11 = 0b11)이다. Zephyr는 `CONFIG_USERSPACE`가 없으면 privileged만(0b01) 열어 준다(§7).

### 1.2 순서 — 왜 DSB · ISB인가, 왜 "가장 먼저"인가

```c
/* ~/.rtos_scratch/B6/lab/startup_b6.c — 랩 startup.c에 더한 부분 */
#define CPACR REG32(0xE000ED88u)
static inline void fpu_enable(void)
{
#ifndef B6_NO_FPU_ENABLE
    CPACR |= (0xFu << 20);                 /* CP10, CP11 = 0b11 (full access) */
    __asm volatile("dsb\n isb" ::: "memory"); /* 다음 명령부터 확실히 보이게 */
#endif
}

void Reset_Handler(void)
{
    uint32_t cp = CPACR;
    fpu_enable();                          /* FP 명령이 하나라도 실행되기 전에 */
    /* .data 복사, .bss 0 채우기, board_init(), main() ... */
}
```

- **DSB**: CPACR 쓰기(시스템 제어 공간 store)가 끝났음을 보장한다.
- **ISB**: 파이프라인에 이미 들어온 다음 명령들을 버리고 다시 가져온다. 그래야 바로 다음 FP 명령이 "새 권한"으로 실행된다. Arm 문서들이 CPACR을 바꾼 뒤 이 두 명령을 요구하는 이유다.
- **"가장 먼저"**: 컴파일러는 우리가 float을 쓰지 않은 곳에서도 FP 명령을 낸다. 예제 2에서 보듯 `main()`의 **프롤로그**가 `vpush {d8}`로 시작할 수 있다. 그래서 FPU 켜기는 `main()` 안이 아니라 **리셋 핸들러(CMSIS의 `SystemInit`)**에서 C 런타임 초기화보다 먼저 한다.

QEMU는 파이프라인을 모델링하지 않으므로 ISB를 빼도 실행 결과가 같다. **ISB 누락은 QEMU로 보여 줄 수 없고**, 실칩에서도 대부분 "우연히" 동작한다(CPACR 쓰기와 첫 FP 명령 사이에 명령이 충분히 많기 때문). 참고로 FreeRTOS ARM_CM4F의 `vPortEnableVFP()`도 DSB · ISB 없이 CPACR만 쓴다(§5.1) — 스케줄러 시작 직전에 다시 한 번 켜는 "보험"이고, 그 뒤 첫 태스크까지 예외 진입(자체가 동기화 지점)을 거치기 때문이다.

### 예제 2 — FPU의 신원과 리셋 상태: CPACR, FPCCR, CONTROL.FPCA

무엇을 확인하나: 리셋 직후 CPACR이 0인지, FPCCR의 기본값, 그리고 "FP 명령을 쓰면 CONTROL.FPCA가 켜진다"는 규칙.

```c
/* ex1: AN386(M4F)의 FPU 신원과 리셋 상태 — CPACR, FPCCR, CONTROL.FPCA */
#include <stdint.h>
#include "board.h"

extern uint32_t g_cpacr_at_reset, g_control_before_main;
#define CPUID  REG32(0xE000ED00u)
#define CPACR  REG32(0xE000ED88u)
#define FPCCR  REG32(0xE000EF34u)
#define FPCAR  REG32(0xE000EF38u)
#define FPDSCR REG32(0xE000EF3Cu)
#define MVFR0  REG32(0xE000EF40u)
#define MVFR1  REG32(0xE000EF44u)

static uint32_t control(void) { uint32_t c; __asm volatile("mrs %0, control" : "=r"(c)); return c; }

volatile float g_a = 1.5f, g_b = 2.25f;

int main(void)
{
    board_printf_raw("CPUID   = 0x%x (part 0x%x)\n", CPUID, (CPUID >> 4) & 0xFFFu);
    board_printf_raw("MVFR0   = 0x%x  MVFR1 = 0x%x\n", MVFR0, MVFR1);
    board_printf_raw("CPACR   at reset = 0x%x, now = 0x%x\n", g_cpacr_at_reset, CPACR);
    board_printf_raw("FPCCR   = 0x%x (ASPEN=%u LSPEN=%u)\n", FPCCR, FPCCR >> 31, (FPCCR >> 30) & 1u);
    board_printf_raw("FPDSCR  = 0x%x\n", FPDSCR);
    board_printf_raw("CONTROL before main  = 0x%x\n", g_control_before_main);
    board_printf_raw("CONTROL before FP op = 0x%x (FPCA=%u)\n", control(), (control() >> 2) & 1u);
    float c = g_a * g_b;                       /* 첫 FP 명령: vmul.f32 */
    board_printf_raw("CONTROL after  FP op = 0x%x (FPCA=%u)\n", control(), (control() >> 2) & 1u);
    board_printf_raw("1.5 * 2.25 = %d/1000\n", (int)(c * 1000.0f));
    return 0;
}
```

`startup_b6.c`는 켜기 전 CPACR 값을 `g_cpacr_at_reset`에, `main()` 직전의 CONTROL 값을 `g_control_before_main`에 남긴다.

```sh
make -s -f ~/.rtos_scratch/B6/lab/bm.mk SRCS=~/.rtos_scratch/B6/ex1/ex1.c OUT=/var/tmp/rtos/B6/ex1
~/.rtos_scratch/B6/lab/run.sh /var/tmp/rtos/B6/ex1/app.elf
```

```text
CPUID   = 0x410fc240 (part 0xc24)
MVFR0   = 0x10110021  MVFR1 = 0x11000011
CPACR   at reset = 0x0, now = 0xf00000
FPCCR   = 0xc0000000 (ASPEN=1 LSPEN=1)
FPDSCR  = 0x0
CONTROL before main  = 0x0
CONTROL before FP op = 0x4 (FPCA=1)
CONTROL after  FP op = 0x4 (FPCA=1)
1.5 * 2.25 = 3375/1000
[board_exit] code=0
```

출력에서 볼 것:

- `CPUID` part `0xC24` = Cortex-M4. `MVFR0 = 0x10110021`은 "단정밀도만, 레지스터 16개 × 64비트(= S0–S31), 나눗셈 · 제곱근 하드웨어 있음"을 뜻하는 FPv4-SP-D16의 표준 값이다.
- CPACR은 리셋 때 `0x0`이었고 시작 코드가 `0xF00000`으로 열었다.
- **FPCCR = `0xC0000000`** = ASPEN · LSPEN이 리셋부터 켜져 있다. 즉 **lazy stacking은 기본값**이다. 아무 설정을 안 해도 §4의 동작이 일어난다.
- `CONTROL before main = 0x0`인데 `main()` 안 첫 줄에서 이미 `FPCA=1`이다. float 연산(`g_a * g_b`)보다 먼저 켜졌다. 이유는 아래 objdump에 있다.

```sh
arm-none-eabi-objdump -d /var/tmp/rtos/B6/ex1/app.elf | awk '/^[0-9a-f]+ <.*>:/{f=$2} /\tv[a-z]/{print f, $0}'
```

```text
<main>:       c6:	ed2d 8b02 	vpush	{d8}
<main>:      130:	edd2 7a00 	vldr	s15, [r2]
<main>:      134:	ed93 7a00 	vldr	s14, [r3]
<main>:      138:	ee27 8a87 	vmul.f32	s16, s15, s14
<main>:      14e:	eddf 7a11 	vldr	s15, [pc, #68]	@ 194 <main+0xd4>
<main>:      154:	ee68 7a27 	vmul.f32	s15, s16, s15
<main>:      158:	eefd 7ae7 	vcvt.s32.f32	s15, s15
<main>:      15c:	ee17 1a90 	vmov	r1, s15
<main>:      164:	ecbd 8b02 	vpop	{d8}
```

출력에서 볼 것: 컴파일러가 곱셈 결과를 함수 호출(`board_printf_raw`) 너머로 들고 가려고 callee-saved인 S16(= D8의 아래 절반)을 골랐고, 그래서 **프롤로그 첫 명령이 `vpush {d8}`**다. 이 명령이 FP 명령이므로 그 순간 FPCA가 1이 된다. 그리고 FPU가 꺼져 있었다면 **`main()`의 첫 명령에서** 죽는다(다음 예제).

같은 ELF를 FPU가 없는 Cortex-M3 머신(`MACHINE=mps2-an385 CPU=cortex-m3`)에서 돌리면:

```text

[HardFault] PC=0xc6 LR=0x20f xPSR=0x21000000 CFSR=0x80000 HFSR=0x40000000
[board_exit] code=255
```

출력에서 볼 것: PC `0xc6` = 바로 그 `vpush {d8}`. M3에는 FPU가 없으니 CPACR 쓰기가 무시되고, FP 명령은 NOCP fault가 된다. "M4F용으로 빌드한 바이너리를 M3/M0+ 보드에 올렸다"는 실수의 증상이 정확히 이것이다.

### 예제 3 — FPU를 안 켜고 FP 명령: UsageFault NOCP, 그리고 핸들러에서 켜고 재시도

무엇을 확인하나: CPACR을 잊으면 어떤 fault가, 어떤 레지스터 값으로 나는지. 그리고 그 fault를 "필요할 때 켜기(lazy enable)"로 쓸 수 있는지.

```c
/* ex2: FPU를 안 켜고 FP 명령 실행 → UsageFault(NOCP). 핸들러에서 CP10/CP11을 켜고 같은 명령을 재시도 */
#include <stdint.h>
#include "board.h"

#define SHCSR REG32(0xE000ED24u)
#define CFSR  REG32(0xE000ED28u)
#define CPACR REG32(0xE000ED88u)
#define ICSR  REG32(0xE000ED04u)

static volatile int g_faults;

void usage_c(uint32_t *frame)                 /* 예외 프레임 포인터를 받는다 */
{
    uint32_t cfsr = CFSR;
    board_printf_raw("[UsageFault] CFSR=0x%x NOCP=%u stacked PC=0x%x IPSR=%u\n",
                     cfsr, (cfsr >> 19) & 1u, frame[6], ICSR & 0x1FFu);
    CFSR = cfsr;                              /* write-1-to-clear */
    CPACR |= 0xFu << 20;                      /* 이제 켠다 */
    __asm volatile("dsb\n isb" ::: "memory");
    g_faults++;                               /* return → 같은 PC를 다시 실행 */
}
__attribute__((naked)) void UsageFault_Handler(void)
{
    __asm volatile(" tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b usage_c\n");
}

volatile float g_x = 3.0f;
__attribute__((noinline)) int scale(int v) { return (int)((float)v * g_x); }

int main(void)
{
    board_printf_raw("CPACR=0x%x (FPU off)\n", CPACR);
#ifdef USE_USGFAULT
    SHCSR |= 1u << 18;                        /* USGFAULTENA: HardFault로 승격되지 않게 */
#endif
    int r = scale(14);                        /* 첫 FP 명령에서 NOCP */
    board_printf_raw("scale(14)=%d after %d fault(s), CPACR=0x%x\n", r, g_faults, CPACR);
    return 0;
}
```

```sh
# 시작 코드의 FPU 켜기를 끄고(-DB6_NO_FPU_ENABLE) 두 번 빌드: UsageFault 꺼짐 / 켜짐
make -s -B -f $L/bm.mk SRCS=.../ex2.c OUT=/var/tmp/rtos/B6/ex2 EXTRA_CFLAGS="-DB6_NO_FPU_ENABLE"
make -s -B -f $L/bm.mk SRCS=.../ex2.c OUT=/var/tmp/rtos/B6/ex2 EXTRA_CFLAGS="-DB6_NO_FPU_ENABLE -DUSE_USGFAULT"
```

```text
== EXTRA=
CPACR=0x0 (FPU off)

[HardFault] PC=0x126 LR=0x15b xPSR=0x21000000 CFSR=0x80000 HFSR=0x40000000
[board_exit] code=255
== EXTRA=-DUSE_USGFAULT
CPACR=0x0 (FPU off)
[UsageFault] CFSR=0x80000 NOCP=1 stacked PC=0x126 IPSR=6
scale(14)=42 after 1 fault(s), CPACR=0xf00000
[board_exit] code=0
```

출력에서 볼 것:

- **`CFSR = 0x00080000`** = UFSR의 NOCP 비트(CFSR bit 19, "No Coprocessor"). FPU가 꺼진 상태에서 FP 명령을 실행했다는 뜻이다. 이 숫자 하나를 외워 두면 bring-up 때 몇 시간을 아낀다.
- UsageFault가 꺼져 있으면(`SHCSR.USGFAULTENA = 0`, 리셋 기본) **HardFault로 승격**된다. 그 표시가 `HFSR = 0x40000000`(FORCED).
- 켜 두면 IPSR 6(UsageFault)으로 들어오고, stacked PC `0x126`은 `scale()`의 첫 FP 명령이다(아래). 핸들러가 CPACR을 열고 그냥 return하면 **같은 명령이 다시 실행되어 성공**한다(`scale(14)=42`).

```text
00000124 <scale>:
     124:	4b06      	ldr	r3, [pc, #24]	@ (140 <scale+0x1c>)
     126:	ee07 0a90 	vmov	s15, r0
     12a:	ed93 7a00 	vldr	s14, [r3]
```

마지막 동작은 Linux 커널이 예전에 x86에서 쓰던 "lazy FPU 복원"(첫 FP 사용 때 trap으로 문맥을 바꾸는 방식)과 같은 아이디어다. MCU RTOS에서는 Cortex-M 하드웨어 lazy stacking이 있어 거의 쓰지 않지만, **"이 태스크는 FP를 쓰면 안 된다"를 강제하는 감시 장치**로는 쓸 수 있다(태스크별로 CPACR을 닫아 두고 NOCP가 나면 버그로 보고).

### 1.3 함정

- FPU 켜기를 `main()`에 넣었다 → `main()` 프롤로그의 `vpush`에서 NOCP. 시작 코드(Reset_Handler / `SystemInit`)로 옮긴다.
- C 런타임 초기화(예: C++ 전역 생성자, `__libc_init_array`)가 float을 쓴다 → 그보다 먼저 켜야 한다.
- 부트로더가 CPACR을 열어 둔 채 앱으로 점프 → 앱은 "켜기를 잊어도" 동작하다가, 부트로더 없이 디버거로 바로 올릴 때만 죽는다. Zephyr가 시작 코드에서 CPACR을 **먼저 지우고** 다시 쓰는 이유다(`arch/arm/core/cortex_m/prep_c.c`의 `z_arm_floating_point_init()` 주석: "might be left un-cleared by firmware running before Zephyr boot").
- `-mfloat-abi=hard`로 빌드한 라이브러리와 `soft`로 빌드한 오브젝트를 섞는다 → 링커가 "uses VFP register arguments" 오류를 낸다. ABI는 프로젝트 전체가 하나여야 한다(§9).

---

## 2. FP 레지스터와 호출 규약 — 무엇이 caller-saved이고 무엇이 callee-saved인가

### 2.1 레지스터 지도

| 레지스터 | 개수 | AAPCS 역할 | 예외 때 누가 저장 | 컨텍스트 스위치 때 누가 저장 |
|---|---|---|---|---|
| S0–S15 (D0–D7) | 16 | 인자 · 반환값 · 임시(caller-saved) | 하드웨어(확장 프레임, lazy) | 하드웨어 프레임에 이미 있음 |
| FPSCR | 1 | 반올림 모드 · 예외 플래그 · FZ/DN | 하드웨어(S0–S15와 함께) | 하드웨어 프레임에 이미 있음 |
| S16–S31 (D8–D15) | 16 | 보존(callee-saved) | 아무도 안 함(쓰는 함수가 `vpush`) | **RTOS(PendSV)** |
| (v8.1-M) VPR | 1 | MVE predicate(P0) | 하드웨어(예약 워드 자리) | 하드웨어 프레임에 이미 있음 |

정수 레지스터와 완전히 같은 구도다. R0–R3 · R12 · LR · PC · xPSR은 하드웨어가, R4–R11은 RTOS가 저장했다(B2 · C2). FP에서는 S0–S15 · FPSCR이 하드웨어, S16–S31이 RTOS다.

### 2.2 hard float ABI에서 함수 경계

`-mfloat-abi=hard`는 float 인자를 S0, S1, S2 …로, 반환값을 S0으로 넘긴다. 그래서 FP 레지스터가 **모든 함수 호출의 일부**가 된다. 예제 13에서 같은 함수 `mac3(a, b, c)`가 ABI마다 어떻게 번역되는지 본다. 요점은:

- `hard`: 인자가 이미 S0–S2에 있다 → `vfma.f32` 한 명령.
- `softfp`: FPU 명령은 쓰되 인자는 정수 레지스터(R0–R2)로 → 들어올 때 `vmov` 3개, 나갈 때 1개.
- `soft`: FPU를 전혀 안 쓰고 `__aeabi_fmul` 같은 libgcc 함수를 부른다.

### 2.3 FPSCR과 FPDSCR

FPSCR에는 반올림 모드(RMode), flush-to-zero(FZ), default NaN(DN), 누적 예외 플래그(IOC, DZC, OFC …)가 있다. 예외에 들어가면 ISR은 **FPDSCR(`0xE000EF3C`)에 적힌 기본값**으로 FPSCR을 새로 시작한다(AHP · DN · FZ · RMode 필드가 복사된다). 즉 스레드가 FZ를 켜 놓았어도 ISR은 FPDSCR 기본값으로 계산한다. 예제 2의 `FPDSCR = 0x0`은 "IEEE 기본(반올림 to nearest, denormal 그대로)"이다.

함정: 제어 루프에서 FZ(denormal을 0으로)나 다른 반올림 모드를 원한다면 **스레드용 FPSCR과 ISR용 FPDSCR을 둘 다** 설정해야 한다. FPSCR만 바꾸면 ISR 안의 같은 계산이 다른 규칙으로 돌아서, "ISR에서 계산한 값과 태스크에서 계산한 값이 미세하게 다르다"는 찾기 힘든 버그가 된다.

---

## 3. 확장 예외 프레임 — 26워드, EXC_RETURN, CONTROL.FPCA

> 🔁 이 절은 Bare-Metal BM C10과 겹친다 (주인: 이 노트). 기본 8워드 프레임 자체는 B2(BM C4)가 주인이다.

### 3.1 직관 — 깃발 하나가 프레임 크기를 정한다

예외가 들어올 때 하드웨어는 **CONTROL.FPCA(FP Context Active)** 비트를 본다.

- FPCA = 0: "지금 실행 중인 문맥은 FP 상태가 없다" → **기본 프레임 8워드(32 B)**.
- FPCA = 1: "FP 상태가 살아 있다" → **확장 프레임 26워드(104 B)** = 기본 8워드 + S0–S15(16) + FPSCR(1) + 예약(1).

FPCA는 **FP 명령을 실행하면 하드웨어가 켠다**(FPCCR.ASPEN = 1일 때). 그리고 예외 진입 때 0으로 지워진다(ISR은 FP를 안 쓴 상태로 시작). 예외 복귀 때는 EXC_RETURN의 bit 4로 복원된다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">예외 스택 프레임: 기본 8워드 vs 확장 26워드 (낮은 주소가 위)</text>
<text x="180.0" y="50.0" font-size="13" text-anchor="middle" font-weight="bold">기본 프레임 (32 B)</text>
<rect x="90.0" y="60.0" width="180.0" height="48.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="180.0" y="88.2" font-size="12" text-anchor="middle">R0–R3</text>
<text x="84.0" y="71.0" font-size="12" text-anchor="end">+0x00</text>
<rect x="90.0" y="108.0" width="180.0" height="12.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="276.0" y="119.0" font-size="12" text-anchor="start">R12</text>
<text x="84.0" y="119.0" font-size="12" text-anchor="end">+0x10</text>
<rect x="90.0" y="120.0" width="180.0" height="12.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="276.0" y="131.0" font-size="12" text-anchor="start">LR</text>
<text x="84.0" y="131.0" font-size="12" text-anchor="end">+0x14</text>
<rect x="90.0" y="132.0" width="180.0" height="12.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="276.0" y="143.0" font-size="12" text-anchor="start">PC</text>
<text x="84.0" y="143.0" font-size="12" text-anchor="end">+0x18</text>
<rect x="90.0" y="144.0" width="180.0" height="12.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="276.0" y="155.0" font-size="12" text-anchor="start">xPSR</text>
<text x="84.0" y="155.0" font-size="12" text-anchor="end">+0x1C</text>
<text x="510.0" y="50.0" font-size="13" text-anchor="middle" font-weight="bold">확장 프레임 (104 B)</text>
<rect x="420.0" y="60.0" width="180.0" height="48.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="510.0" y="88.2" font-size="12" text-anchor="middle">R0–R3</text>
<text x="414.0" y="71.0" font-size="12" text-anchor="end">+0x00</text>
<rect x="420.0" y="108.0" width="180.0" height="12.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="606.0" y="119.0" font-size="12" text-anchor="start">R12</text>
<text x="414.0" y="119.0" font-size="12" text-anchor="end">+0x10</text>
<rect x="420.0" y="120.0" width="180.0" height="12.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="606.0" y="131.0" font-size="12" text-anchor="start">LR</text>
<text x="414.0" y="131.0" font-size="12" text-anchor="end">+0x14</text>
<rect x="420.0" y="132.0" width="180.0" height="12.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="606.0" y="143.0" font-size="12" text-anchor="start">PC</text>
<text x="414.0" y="143.0" font-size="12" text-anchor="end">+0x18</text>
<rect x="420.0" y="144.0" width="180.0" height="12.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="606.0" y="155.0" font-size="12" text-anchor="start">xPSR</text>
<text x="414.0" y="155.0" font-size="12" text-anchor="end">+0x1C</text>
<rect x="420.0" y="156.0" width="180.0" height="192.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="510.0" y="256.2" font-size="12" text-anchor="middle">S0–S15</text>
<text x="414.0" y="167.0" font-size="12" text-anchor="end">+0x20</text>
<rect x="420.0" y="348.0" width="180.0" height="12.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="606.0" y="359.0" font-size="12" text-anchor="start">FPSCR</text>
<text x="414.0" y="359.0" font-size="12" text-anchor="end">+0x60</text>
<rect x="420.0" y="360.0" width="180.0" height="12.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="606.0" y="371.0" font-size="12" text-anchor="start">예약·VPR</text>
<text x="414.0" y="371.0" font-size="12" text-anchor="end">+0x64</text>
<text x="90.0" y="175.0" font-size="12" text-anchor="start">SP(MSP 또는 PSP)는</text>
<text x="90.0" y="192.0" font-size="12" text-anchor="start">R0 칸(+0x00)을 가리킨다</text>
<text x="90.0" y="220.0" font-size="12" text-anchor="start">EXC_RETURN bit4 = 1</text>
<text x="90.0" y="237.0" font-size="12" text-anchor="start">→ 0xFFFFFFF1 / F9 / FD</text>
<text x="606.0" y="167.0" font-size="12" text-anchor="start">← FPCAR</text>
<text x="90.0" y="300.0" font-size="12" text-anchor="start">EXC_RETURN bit4 = 0</text>
<text x="90.0" y="317.0" font-size="12" text-anchor="start">→ 0xFFFFFFE1 / E9 / ED</text>
<text x="90.0" y="345.0" font-size="12" text-anchor="start">lazy일 때 S0–S15/FPSCR 칸은</text>
<text x="90.0" y="362.0" font-size="12" text-anchor="start">진입 시점엔 비어 있고(옛 값)</text>
<text x="90.0" y="379.0" font-size="12" text-anchor="start">FPCAR이 그 시작(+0x20)을 기억한다</text>
<text x="90.0" y="410.0" font-size="12" text-anchor="start">xPSR bit9 = 1이면 8바이트 정렬용 1워드가 더 붙는다</text>
</svg>
```

그림 2 — 기본 프레임과 확장 프레임. 같은 SP 기준 오프셋에 R0–xPSR이 있고, 확장 프레임은 그 위(높은 주소)로 S0–S15, FPSCR, 예약 워드가 더 붙는다. 8바이트 정렬 패딩이 붙으면 xPSR bit 9로 표시된다.

같은 내용을 ASCII로 (오프셋은 SP 기준):

```
+0x00 R0    +0x04 R1    +0x08 R2    +0x0C R3
+0x10 R12   +0x14 LR    +0x18 PC    +0x1C xPSR      <- basic frame ends (32 B)
+0x20 S0  ... +0x5C S15                              <- FPCAR points to +0x20
+0x60 FPSCR
+0x64 reserved (Armv8.1-M with MVE: VPR)             <- extended frame ends (104 B)
```

### 3.2 EXC_RETURN — bit 4가 "프레임 모양" 표시

ISR에 들어오면 LR에 EXC_RETURN이 들어간다(B2). FPU가 있는 코어에서는 **bit 4(FType)**가 "복귀할 때 확장 프레임을 풀어야 하나"를 기억한다. 0이면 확장 프레임이다.

| EXC_RETURN | 복귀할 곳 | 스택 | 프레임 |
|---|---|---|---|
| 0xFFFFFFF1 | Handler 모드(중첩 ISR) | MSP | 기본 |
| 0xFFFFFFF9 | Thread 모드 | MSP | 기본 |
| 0xFFFFFFFD | Thread 모드 | PSP | 기본 |
| 0xFFFFFFE1 | Handler 모드(중첩 ISR) | MSP | **확장** |
| 0xFFFFFFE9 | Thread 모드 | MSP | **확장** |
| 0xFFFFFFED | Thread 모드 | PSP | **확장** (FP를 쓰는 RTOS 태스크) |

RTOS 코드에서 `tst lr, #0x10`이 나오면 "이 문맥이 FP를 쓰나?"를 묻는 것이다.

### 예제 4 — ISR 안에서 프레임 덤프: ASPEN · LSPEN · FPCA 조합 다섯 가지

무엇을 확인하나: (1) 프레임이 정말 26워드(104 B)인지 SP 차이로, (2) lazy일 때 S0–S15 칸이 진입 순간엔 비어 있다가 ISR의 첫 FP 명령에서 채워지는지, (3) FPCA를 잘못 다루면 무엇이 깨지는지.

방법: `main`에서 S0–S15에 `0xA0000000 + i` 패턴을, FPSCR에 `0x03000000`을 넣고, 스택 아래를 `0xEEEEEEEE`로 칠한 뒤 IRQ 8을 소프트웨어로 건다(NVIC ISPR). ISR은 naked 래퍼로 진입 순간의 SP와 LR을 받아, FP 명령 전후로 프레임 26워드를 복사한다.

```c
/* ex3: ISR 안에서 예외 프레임 덤프 — FPCCR(ASPEN/LSPEN), CONTROL.FPCA에 따라 8워드/26워드, lazy 저장 */
#include <stdint.h>
#include "board.h"
#define FPCCR REG32(0xE000EF34u)
#define FPCAR REG32(0xE000EF38u)

static const uint32_t pat[16] = {0xA0000000,0xA0000001,0xA0000002,0xA0000003,0xA0000004,0xA0000005,
  0xA0000006,0xA0000007,0xA0000008,0xA0000009,0xA000000A,0xA000000B,0xA000000C,0xA000000D,0xA000000E,0xA000000F};
static struct { uint32_t exc, sp_irq, fpcar, lsp0, lsp1, w[2][26]; } g;
static uint32_t g_sp_before, g_s0, g_s15;
volatile float g_f = 1.0f;

void isr_c(uint32_t *f, uint32_t exc)
{
    g.exc = exc; g.sp_irq = (uint32_t)f; g.fpcar = FPCAR; g.lsp0 = FPCCR & 1u;
    for (int i = 0; i < 26; i++) g.w[0][i] = f[i];      /* FP 명령 쓰기 전 */
    __asm volatile("" ::: "memory");
    g_f = g_f * 2.0f;                                   /* ISR의 첫 FP 명령 */
    g.lsp1 = FPCCR & 1u;
    for (int i = 0; i < 26; i++) g.w[1][i] = f[i];      /* FP 명령 쓴 뒤 */
}
__attribute__((naked)) void TIMER0_IRQHandler(void)
{ __asm volatile(" mov r0, sp\n mov r1, lr\n push {r4, lr}\n bl isr_c\n pop {r4, pc}\n"); }

static void fire(int fpca)                              /* S0-S15에 패턴을 넣고 IRQ 8을 건다 */
{
    uint32_t sp; __asm volatile("mov %0, sp" : "=r"(sp));
    for (uint32_t *p = (uint32_t *)sp - 64; p < (uint32_t *)sp - 4; p++) *p = 0xEEEEEEEE;
    __asm volatile(" vldmia %0, {s0-s15}\n vmsr fpscr, %1\n" :: "r"(pat), "r"(0x03000000u)
        : "memory", "s0","s1","s2","s3","s4","s5","s6","s7","s8","s9","s10","s11","s12","s13","s14","s15");
    if (!fpca) __asm volatile("msr control, %0\n isb" :: "r"(0));  /* FPCA=0으로 만든다 */
    __asm volatile(" mov r1, sp\n str r1, [%0]\n str %2, [%1]\n dsb\n isb\n"
        :: "r"(&g_sp_before), "r"(&NVIC_ISER0 + 64), "r"(1u << 8) : "r1", "memory"); /* ISPR0 */
    __asm volatile("vmov %0, s0\n vmov %1, s15" : "=r"(g_s0), "=r"(g_s15));  /* 복귀 뒤 */
}

static void show(const char *name, uint32_t fpccr, int fpca)
{
    FPCCR = fpccr;
    fire(fpca);
    int ext = !(g.exc & 0x10u);
    board_printf_raw("%s: EXC_RETURN=0x%x frame=%s pushed=%u B FPCAR=0x%x(frame+0x%x) LSPACT %u->%u\n",
        name, g.exc, ext ? "EXT(26w)" : "basic(8w)", g_sp_before - g.sp_irq,
        ext ? g.fpcar : 0, ext ? g.fpcar - g.sp_irq : 0, g.lsp0, g.lsp1);
    board_printf_raw("  after return: s0=0x%x s15=0x%x -> %s\n", g_s0, g_s15,
        (g_s0 == pat[0] && g_s15 == pat[15]) ? "OK" : "CORRUPTED");
    if (ext) board_printf_raw("  S0 slot %x -> %x, S15 slot %x -> %x, FPSCR slot %x -> %x\n",
        g.w[0][8], g.w[1][8], g.w[0][23], g.w[1][23], g.w[0][24], g.w[1][24]);
}

int main(void)
{
    NVIC_IPR(8) = 0x80; NVIC_ISER0 = 1u << 8;
    show("A ASPEN=1 LSPEN=1", 0xC0000000u, 1);
    show("B ASPEN=1 LSPEN=0", 0x80000000u, 1);
    show("C ASPEN=0 LSPEN=0", 0x00000000u, 1);
    show("D ASPEN=1 LSPEN=1, FPCA=0", 0xC0000000u, 0);
    show("E ASPEN=0 LSPEN=0, FPCA=0", 0x00000000u, 0);
    return 0;
}
```

```text
A ASPEN=1 LSPEN=1: EXC_RETURN=0xffffffe9 frame=EXT(26w) pushed=104 B FPCAR=0x203fff78(frame+0x20) LSPACT 1->0
  after return: s0=0xa0000000 s15=0xa000000f -> OK
  S0 slot eeeeeeee -> a0000000, S15 slot 3c -> a000000f, FPSCR slot 1 -> 3000000
B ASPEN=1 LSPEN=0: EXC_RETURN=0xffffffe9 frame=EXT(26w) pushed=104 B FPCAR=0x203fff78(frame+0x20) LSPACT 0->0
  after return: s0=0xa0000000 s15=0xa000000f -> OK
  S0 slot a0000000 -> a0000000, S15 slot a000000f -> a000000f, FPSCR slot 3000000 -> 3000000
C ASPEN=0 LSPEN=0: EXC_RETURN=0xffffffe9 frame=EXT(26w) pushed=104 B FPCAR=0x203fff78(frame+0x20) LSPACT 0->0
  after return: s0=0xa0000000 s15=0xa000000f -> OK
  S0 slot a0000000 -> a0000000, S15 slot a000000f -> a000000f, FPSCR slot 3000000 -> 3000000
D ASPEN=1 LSPEN=1, FPCA=0: EXC_RETURN=0xfffffff9 frame=basic(8w) pushed=32 B FPCAR=0x0(frame+0x0) LSPACT 0->0
  after return: s0=0xa0000000 s15=0x41800000 -> CORRUPTED
E ASPEN=0 LSPEN=0, FPCA=0: EXC_RETURN=0xfffffff9 frame=basic(8w) pushed=32 B FPCAR=0x0(frame+0x0) LSPACT 0->0
  after return: s0=0xa0000000 s15=0x42000000 -> CORRUPTED
[board_exit] code=0
```

출력에서 볼 것 — 줄마다 하나씩:

- **A (리셋 기본값, lazy)**: SP가 정확히 **104 B** 내려갔다(26워드). 진입 순간 S0 칸은 `0xEEEEEEEE`(칠해 둔 옛 값), FPSCR 칸도 쓰레기(`0x1`)다 — **자리만 잡았다**. `LSPACT 1 → 0`: ISR이 `g_f * 2.0f`를 실행하자 하드웨어가 그 순간 S0–S15 · FPSCR을 FPCAR(= 프레임 + 0x20)에 써 넣었다. 복귀 뒤 스레드의 S0 · S15는 패턴 그대로다.
- **B (LSPEN = 0, 즉시 저장)**: 진입 순간 이미 S0 칸 = `0xA0000000`. LSPACT는 처음부터 0. 결과는 같고 **시점만** 다르다.
- **C (ASPEN = 0이지만 FPCA가 이미 1)**: 여전히 확장 프레임. ASPEN은 "FP 명령이 FPCA를 자동으로 켜는가"만 정한다. 프레임 모양은 **그 순간의 FPCA**가 정한다.
- **D (FPCA를 소프트웨어로 지움)**: 기본 프레임 32 B, EXC_RETURN `0xFFFFFFF9`. 하드웨어는 "FP 상태 없음"을 믿었고, ISR이 S15를 덮어써서(`0x41800000` = 16.0f) **스레드의 S15가 깨졌다**. FreeRTOS가 첫 태스크를 시작할 때 `msr control, 0`으로 FPCA를 지우는데(§5.1), 그 시점엔 살아 있는 FP 값이 없으니 안전하다. 아무 데서나 하면 이렇게 된다.
- **E (ASPEN = 0, FPCA = 0)**: FP 명령(`vldmia`)을 실행해도 FPCA가 켜지지 않는다 → 기본 프레임 → 똑같이 깨짐. **Zephyr의 "Unshared FP registers" 모드가 바로 이 설정**(ASPEN = LSPEN = 0)이다. 그 모드는 "FP를 쓰는 문맥이 하나뿐이고 ISR은 FP를 안 쓴다"는 약속 위에서만 안전하다(§7).

### 예제 5 — EXC_RETURN 세 가지(E9 / E1 / ED)와 IRQ 왕복 비용

무엇을 확인하나: 표의 EXC_RETURN 값이 실제로 나오는지(MSP 스레드, 중첩 ISR, PSP 스레드), 그리고 확장 프레임 · lazy 저장이 **QEMU icount에서** 비용으로 잡히는지.

```c
/* ex4: EXC_RETURN 세 가지(E9/E1/ED) + 왕복 비용을 ICOUNT로 — QEMU가 스태킹 비용을 세나? */
#include <stdint.h>
#include "board.h"
#define FPCCR   REG32(0xE000EF34u)
#define CNT     REG32(0x40028018u)              /* FPGAIO COUNTER, 25 MHz */
#define ISPR0   REG32(0xE000E200u)

volatile uint32_t g_lr8, g_lr10, g_usefp;
__attribute__((naked)) void DUALTIMER_IRQHandler(void)   /* IRQ10, 더 높은 우선순위 */
{ __asm volatile(" ldr r0, =g_lr10\n str lr, [r0]\n bx lr\n"); }
__attribute__((naked)) void TIMER0_IRQHandler(void)      /* IRQ8: LR 기록, FP 쓰고, IRQ10 중첩 */
{ __asm volatile(" ldr r0, =g_lr8\n str lr, [r0]\n vmov s0, r0\n"
                 " ldr r0, =0xE000E200\n mov r1, #1024\n str r1, [r0]\n dsb\n isb\n bx lr\n"); }
__attribute__((naked)) void TIMER1_IRQHandler(void)      /* IRQ9: 타이밍용 빈 ISR (옵션: FP 1개) */
{ __asm volatile(" ldr r0, =g_usefp\n ldr r0, [r0]\n cbz r0, 1f\n vmov s0, r0\n1: bx lr\n"); }

static uint32_t roundtrip(int fpca, uint32_t fpccr, int usefp)
{
    FPCCR = fpccr; g_usefp = usefp;
    __asm volatile("vmov s1, %0" :: "r"(0) : "s1");      /* FPCA=1 */
    if (!fpca) __asm volatile("msr control, %0\n isb" :: "r"(0));
    uint32_t t0 = CNT;
    for (int i = 0; i < 1000; i++) { ISPR0 = 1u << 9; __asm volatile("dsb\n isb" ::: "memory"); }
    return CNT - t0;
}

static uint64_t psp_stack[64];
int main(void)
{
    NVIC_IPR(8) = 0x80; NVIC_IPR(10) = 0x40; NVIC_IPR(9) = 0x80;
    NVIC_ISER0 = (1u << 8) | (1u << 9) | (1u << 10);
    __asm volatile("vmov s1, %0" :: "r"(0) : "s1");      /* main(MSP)에 FP 문맥 */
    ISPR0 = 1u << 8; __asm volatile("dsb\n isb");
    board_printf_raw("MSP thread + FP : IRQ8 LR=0x%x, nested IRQ10 LR=0x%x\n", g_lr8, g_lr10);
    __asm volatile("msr psp, %0\n mov r0, #2\n msr control, r0\n isb" :: "r"(&psp_stack[64]) : "r0");
    __asm volatile("vmov s1, %0" :: "r"(0) : "s1");      /* 이제 PSP에서, FPCA=1 */
    ISPR0 = 1u << 8; __asm volatile("dsb\n isb");
    board_printf_raw("PSP thread + FP : IRQ8 LR=0x%x\n", g_lr8);
    __asm volatile("mrs r0, control\n bic r0, r0, #6\n msr control, r0\n isb" ::: "r0"); /* MSP, FPCA=0 */
    ISPR0 = 1u << 8; __asm volatile("dsb\n isb");
    board_printf_raw("MSP thread no FP: IRQ8 LR=0x%x\n", g_lr8);

    static const struct { const char *n; int fpca; uint32_t fpccr; int fp; } v[] = {
        {"basic frame (FPCA=0)          ", 0, 0xC0000000u, 0},
        {"EXT lazy, ISR no FP           ", 1, 0xC0000000u, 0},
        {"EXT lazy, ISR uses FP         ", 1, 0xC0000000u, 1},
        {"EXT immediate(LSPEN=0), no FP ", 1, 0x80000000u, 0},
        {"EXT immediate(LSPEN=0), FP    ", 1, 0x80000000u, 1},
    };
    for (unsigned i = 0; i < 5; i++) {
        uint32_t c = roundtrip(v[i].fpca, v[i].fpccr, v[i].fp);
        board_printf_raw("%s: 1000 IRQ = %u ticks(40ns) = %u ns/IRQ\n", v[i].n, c, c * 40u / 1000u);
    }
    return 0;
}
```

```text
MSP thread + FP : IRQ8 LR=0xffffffe9, nested IRQ10 LR=0xffffffe1
PSP thread + FP : IRQ8 LR=0xffffffed
MSP thread no FP: IRQ8 LR=0xfffffff9
basic frame (FPCA=0)          : 1000 IRQ = 7202 ticks(40ns) = 288 ns/IRQ
EXT lazy, ISR no FP           : 1000 IRQ = 7202 ticks(40ns) = 288 ns/IRQ
EXT lazy, ISR uses FP         : 1000 IRQ = 8002 ticks(40ns) = 320 ns/IRQ
EXT immediate(LSPEN=0), no FP : 1000 IRQ = 7202 ticks(40ns) = 288 ns/IRQ
EXT immediate(LSPEN=0), FP    : 1000 IRQ = 8002 ticks(40ns) = 320 ns/IRQ
[board_exit] code=0
```

출력에서 볼 것:

- **E9 / E1 / ED가 모두 실측으로 나왔다**. IRQ 8 핸들러가 `vmov s0, r0`으로 FP를 쓴 뒤 더 높은 우선순위 IRQ 10을 걸었더니, IRQ 10의 LR이 `0xFFFFFFE1`(Handler 모드로 복귀, 확장 프레임)이다. PSP로 바꾼 스레드에서는 `0xFFFFFFED` — **FreeRTOS · Zephyr의 FP 태스크가 보게 되는 값**이다.
- **icount 왕복 비용은 프레임 모양과 무관하게 288 ns(= 명령 9개)**. FP 1개를 쓰면 정확히 명령 1개(32 ns)만 늘었다. 즉 **QEMU icount는 예외 스태킹도, lazy 저장도, 17워드 추가 쓰기도 0으로 센다**. 이게 이 랩에서 가장 중요한 한계다.

같은 ELF를 ICOUNT 없이(가상 시계 = 호스트 실시간) 5번 돌리면 그림이 달라진다.

```text
 1354 ns/IRQ  1375 ns/IRQ  2769 ns/IRQ  2675 ns/IRQ  2613 ns/IRQ
 1379 ns/IRQ  1378 ns/IRQ  2734 ns/IRQ  2669 ns/IRQ  2693 ns/IRQ
 1392 ns/IRQ  1418 ns/IRQ  2832 ns/IRQ  2662 ns/IRQ  2632 ns/IRQ
 1330 ns/IRQ  1345 ns/IRQ  2693 ns/IRQ  2659 ns/IRQ  2628 ns/IRQ
 1428 ns/IRQ  1514 ns/IRQ  2770 ns/IRQ  2685 ns/IRQ  2711 ns/IRQ
```

(열 순서는 위 표와 같다: 기본 / lazy·FP없음 / lazy·FP / 즉시·FP없음 / 즉시·FP.) 이 숫자는 **QEMU가 호스트 CPU에서 그 일을 에뮬레이션하는 데 쓴 시간**이지 Cortex-M4의 사이클이 아니다. 그런데 모양은 실칩의 원리와 같다. lazy인데 ISR이 FP를 안 쓰면 기본 프레임과 같은 값(약 1.38 µs), FP 레지스터 17워드를 실제로 옮기는 순간(lazy+FP, 또는 LSPEN = 0) 두 배가 된다. QEMU 안에서도 "실제로 쓴 워드"만큼 일이 늘어난다는 뜻이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">측정: IRQ 왕복 1회 (ns) — icount vs 호스트 실시간(5회 중앙값)</text>
<text x="212.0" y="65.0" font-size="12" text-anchor="end">기본 프레임 (FPCA=0)</text>
<rect x="220.0" y="45.0" width="38.4" height="16.0" rx="2" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="264.4" y="58.0" font-size="12" text-anchor="start">288 (icount)</text>
<rect x="220.0" y="64.0" width="183.9" height="16.0" rx="2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="409.9" y="77.0" font-size="12" text-anchor="start">1379</text>
<text x="212.0" y="113.0" font-size="12" text-anchor="end">확장 lazy, ISR FP 없음</text>
<rect x="220.0" y="93.0" width="38.4" height="16.0" rx="2" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="264.4" y="106.0" font-size="12" text-anchor="start">288 (icount)</text>
<rect x="220.0" y="112.0" width="183.7" height="16.0" rx="2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="409.7" y="125.0" font-size="12" text-anchor="start">1378</text>
<text x="212.0" y="161.0" font-size="12" text-anchor="end">확장 lazy, ISR FP 1개</text>
<rect x="220.0" y="141.0" width="42.7" height="16.0" rx="2" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="268.7" y="154.0" font-size="12" text-anchor="start">320 (icount)</text>
<rect x="220.0" y="160.0" width="369.2" height="16.0" rx="2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="595.2" y="173.0" font-size="12" text-anchor="start">2769</text>
<text x="212.0" y="209.0" font-size="12" text-anchor="end">확장 즉시(LSPEN=0), FP 없음</text>
<rect x="220.0" y="189.0" width="38.4" height="16.0" rx="2" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="264.4" y="202.0" font-size="12" text-anchor="start">288 (icount)</text>
<rect x="220.0" y="208.0" width="355.9" height="16.0" rx="2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="581.9" y="221.0" font-size="12" text-anchor="start">2669</text>
<text x="212.0" y="257.0" font-size="12" text-anchor="end">확장 즉시(LSPEN=0), FP 1개</text>
<rect x="220.0" y="237.0" width="42.7" height="16.0" rx="2" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="268.7" y="250.0" font-size="12" text-anchor="start">320 (icount)</text>
<rect x="220.0" y="256.0" width="350.9" height="16.0" rx="2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="576.9" y="269.0" font-size="12" text-anchor="start">2632</text>
<line x1="220.0" y1="40.0" x2="220.0" y2="290.0" stroke="currentColor" stroke-width="1.2"/>
<line x1="220.0" y1="290.0" x2="220.0" y2="295.0" stroke="currentColor" stroke-width="1.2"/>
<text x="220.0" y="310.0" font-size="12" text-anchor="middle">0</text>
<line x1="353.3" y1="290.0" x2="353.3" y2="295.0" stroke="currentColor" stroke-width="1.2"/>
<text x="353.3" y="310.0" font-size="12" text-anchor="middle">1000</text>
<line x1="486.7" y1="290.0" x2="486.7" y2="295.0" stroke="currentColor" stroke-width="1.2"/>
<text x="486.7" y="310.0" font-size="12" text-anchor="middle">2000</text>
<line x1="620.0" y1="290.0" x2="620.0" y2="295.0" stroke="currentColor" stroke-width="1.2"/>
<text x="620.0" y="310.0" font-size="12" text-anchor="middle">3000</text>
<text x="10.0" y="326.0" font-size="12" text-anchor="start">파랑 = ICOUNT=1 (스태킹 비용 0으로 셈), 주황 = ICOUNT 없이 (QEMU가 FP 프레임을 쓰는 호스트 시간)</text>
</svg>
```

그림 3 — IRQ 왕복 1회 측정(예제 5). 파랑(icount)은 스태킹을 0으로 세서 FP 명령 1개 차이(288 → 320 ns)만 보인다. 주황(호스트 실시간, 5회 중앙값)은 FP 레지스터를 실제로 옮기는 두 경우와 LSPEN = 0만 커진다. 둘 다 실칩 사이클은 아니다.

---

## 4. Lazy stacking — FPCCR.ASPEN · LSPEN, LSPACT, FPCAR

> 🔁 이 절은 Bare-Metal BM C10과 겹친다 (주인: 이 노트).

### 4.1 직관 — 자리만 맡아 두기

확장 프레임은 104 B다. 매 인터럽트마다 S0–S15 · FPSCR 17워드를 진짜로 쓰면 진입 지연이 그만큼 늘어난다. 그런데 **ISR 대부분은 FP를 쓰지 않는다**(UART 바이트 받기, 타이머 플래그 지우기, 세마포어 주기). 그래서 Cortex-M4F는 기본으로 이렇게 한다.

1. 예외 진입: 정수 8워드만 실제로 쓰고, FP 18워드는 **스택 공간만 예약**한다(SP는 104 B 내려간다).
2. 그 자리의 주소를 **FPCAR**(FP Context Address Register, `0xE000EF38`)에 적고, **FPCCR.LSPACT = 1**("저장이 미뤄져 있음")로 둔다.
3. ISR이 FP 명령을 **하나라도** 실행하려 하면, 그 명령 직전에 하드웨어가 S0–S15 · FPSCR을 FPCAR 위치에 쓰고 LSPACT = 0으로 만든다.
4. 복귀: ISR이 FP를 안 썼으면(LSPACT가 여전히 1) FP 복원을 건너뛴다 — 레지스터 값이 CPU 안에 그대로 있으니까. 썼으면 26워드를 다 복원한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">Lazy stacking (ASPEN=1, LSPEN=1): 자리만 잡고, ISR이 FP를 쓸 때 저장</text>
<text x="10.0" y="62.0" font-size="13" text-anchor="start" font-weight="bold">(a) ISR이 FP를 안 쓴다</text>
<rect x="10.0" y="75.0" width="110.0" height="50.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="65.0" y="96.2" font-size="12" text-anchor="middle">스레드 실행</text>
<text x="65.0" y="112.2" font-size="12" text-anchor="middle">FPCA=1</text>
<rect x="128.0" y="75.0" width="150.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="203.0" y="96.2" font-size="12" text-anchor="middle">IRQ 진입: 8워드 저장</text>
<text x="203.0" y="112.2" font-size="12" text-anchor="middle">+18워드 자리만</text>
<rect x="286.0" y="75.0" width="246.0" height="50.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="409.0" y="96.2" font-size="12" text-anchor="middle">ISR 정수 코드만 (FP 레지스터는</text>
<text x="409.0" y="112.2" font-size="12" text-anchor="middle">그대로 CPU 안에 살아 있다)</text>
<rect x="540.0" y="75.0" width="130.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="605.0" y="96.2" font-size="12" text-anchor="middle">복귀: 8워드만</text>
<text x="605.0" y="112.2" font-size="12" text-anchor="middle">복원, FP 생략</text>
<text x="128.0" y="145.0" font-size="12" text-anchor="start">LSPACT=1, FPCAR=프레임+0x20</text>
<text x="540.0" y="145.0" font-size="12" text-anchor="start">LSPACT가 복귀 때 0</text>
<text x="10.0" y="202.0" font-size="13" text-anchor="start" font-weight="bold">(b) ISR이 FP를 쓴다</text>
<rect x="10.0" y="215.0" width="110.0" height="50.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="65.0" y="236.2" font-size="12" text-anchor="middle">스레드 실행</text>
<text x="65.0" y="252.2" font-size="12" text-anchor="middle">FPCA=1</text>
<rect x="128.0" y="215.0" width="150.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="203.0" y="236.2" font-size="12" text-anchor="middle">IRQ 진입: 8워드 저장</text>
<text x="203.0" y="252.2" font-size="12" text-anchor="middle">+18워드 자리만</text>
<rect x="286.0" y="215.0" width="96.0" height="50.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="334.0" y="236.2" font-size="12" text-anchor="middle">ISR 정수</text>
<text x="334.0" y="252.2" font-size="12" text-anchor="middle">코드</text>
<rect x="390.0" y="215.0" width="140.0" height="50.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="460.0" y="236.2" font-size="12" text-anchor="middle">첫 FP 명령: 정지 →</text>
<text x="460.0" y="252.2" font-size="12" text-anchor="middle">S0–S15·FPSCR 기록</text>
<rect x="540.0" y="215.0" width="130.0" height="50.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="605.0" y="236.2" font-size="12" text-anchor="middle">복귀: 26워드</text>
<text x="605.0" y="252.2" font-size="12" text-anchor="middle">모두 복원</text>
<text x="128.0" y="285.0" font-size="12" text-anchor="start">LSPACT=1, FPCAR=프레임+0x20</text>
<text x="390.0" y="285.0" font-size="12" text-anchor="start">LSPACT=1 → 0</text>
<line x1="10.0" y1="318.0" x2="670.0" y2="318.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<text x="600.0" y="312.0" font-size="12" text-anchor="start">시간</text>
</svg>
```

그림 4 — lazy stacking 타임라인. (a) ISR이 FP를 안 쓰면 FP 레지스터는 한 번도 메모리에 가지 않는다. (b) ISR의 첫 FP 명령이 저장을 "터뜨리고", 그 순간 LSPACT가 1에서 0이 된다. 예제 4의 A줄이 (b)를 실측으로 보여 준다.

### 4.2 FPCCR의 비트들

| FPCCR 비트 | 이름 | 뜻 | 리셋값 |
|---|---|---|---|
| 31 | ASPEN | FP 명령 실행 때 CONTROL.FPCA를 자동으로 켠다 (= 확장 프레임을 자동으로) | 1 |
| 30 | LSPEN | 확장 프레임의 FP 부분을 lazy로 (자리만 예약) | 1 |
| 8, 6, 5, 4 | MONRDY, BFRDY, MMRDY, HFRDY | lazy 저장 도중 fault가 나면 어느 fault로 갈 수 있었나 기록 | — |
| 3 | THREAD | lazy 자리를 잡은 문맥이 Thread 모드였나 | — |
| 1 | USER | 그 문맥이 unprivileged였나 | — |
| 0 | LSPACT | lazy 저장이 아직 미뤄져 있다 (자리는 FPCAR) | 0 |

(Armv8-M에는 보안 확장용 비트 S · TS · CLRONRET · LSPENS가 더 있다. 예제 15에서 M55의 리셋값 `0xC0000004`의 bit 2가 S다.)

조합은 셋이 의미가 있다.

| ASPEN | LSPEN | 동작 | 언제 쓰나 |
|---|---|---|---|
| 1 | 1 | 자동 + lazy (리셋 기본) | 거의 항상. FreeRTOS · Zephyr 기본 |
| 1 | 0 | 자동 + 즉시 저장 | 디버깅(프레임에 FP 값이 늘 보이게), 일부 보안 요구 |
| 0 | 0 | 하드웨어는 FP를 저장 안 함 | FP 문맥이 하나뿐이라고 확신할 때(Zephyr unshared 모드) — 예제 4의 E줄 |

### 4.3 실칩에서 얼마나 아끼나 (추정 — 이 랩에서 측정 불가)

QEMU는 스태킹 사이클을 모델링하지 않는다(예제 5). 그래서 실칩 숫자는 **워드 수로 추정**만 한다.

- Cortex-M4의 기본 예외 진입은 **12사이클**(Arm이 공개한 값)이고, 그 대부분이 8워드 저장이다.
- FP를 즉시 저장하면 17워드(S0–S15 + FPSCR)를 더 써야 한다. 버스가 사이클당 1워드라면 **대략 17사이클 이상**이 진입에 붙는다. 복귀도 비슷하게 늘어난다.
- lazy면 FP를 안 쓰는 ISR은 이 비용을 **전혀** 내지 않는다. FP를 쓰는 ISR만 첫 FP 명령에서 그만큼 멈춘다.

Arm의 응용 노트 **AN298 "Cortex-M4(F) Lazy Stacking and Context Switching"**이 이 메커니즘과 RTOS 쪽 처리를 설명하는 원전이다(참고 자료). 정확한 사이클 수가 필요하면 그 문서와 칩 데이터시트를 보고, **실칩에서 DWT CYCCNT나 GPIO 토글 + 스코프로** 잰다. 이 노트는 "lazy일 때 FP 안 쓰는 ISR은 FP 없는 칩과 같은 진입 비용"이라는 **원리**와 "FP를 쓰는 ISR은 17워드 + α"라는 **규모**만 말한다.

### 4.4 lazy stacking의 함정

- **lazy 저장 도중의 fault**: 미뤄 둔 저장은 ISR 안에서 일어나지만 주소는 **원래 문맥의 스택**(FPCAR)이다. 그 스택이 MPU 가드 영역에 닿아 있으면 ISR 안에서 MemManage가 난다. FPCCR의 MMRDY · BFRDY 같은 비트와 MMFSR의 MLSPERR(lazy 저장 중 MemManage)이 이 경우를 위한 것이다. Zephyr가 FP 스레드의 MPU 스택 가드를 더 크게 잡는 이유다(`MPU_GUARD_ALIGN_AND_SIZE_FLOAT`, `arch/arm/core/cortex_m/thread.c`).
- **스택이 26워드를 감당 못 함**: lazy여도 **공간은 항상 104 B 예약**된다. FP를 쓰는 태스크의 스택 예산은 "인터럽트 한 번 = 104 B(+정렬 4 B)"로 계산해야 한다. 중첩 인터럽트가 FP를 쓰면 MSP에도 104 B씩 쌓인다.
- **FPCA를 손으로 지우기**: 예제 4의 D줄처럼, 살아 있는 FP 값이 있는데 FPCA를 지우면 하드웨어는 저장을 안 한다.

---

## 5. RTOS에서 — FreeRTOS ARM_CM4F 포트

### 5.1 port.c가 하는 일 네 가지 (V11.3.2, `portable/GCC/ARM_CM4F/port.c`)

**① 하드웨어 FP 옵션 없이 빌드하면 컴파일 에러**:

```c
#ifndef __ARM_FP
    #error This port can only be used when the project options are configured to enable hardware floating point support.
#endif
```

**② 스케줄러 시작 때 FPU를 다시 켜고, lazy를 강제**(`xPortStartScheduler()`):

```c
    /* Ensure the VFP is enabled - it should be anyway. */
    vPortEnableVFP();                          /* CPACR |= 0xF << 20 (naked asm, DSB/ISB 없음) */

    /* Lazy save always. */
    *( portFPCCR ) |= portASPEN_AND_LSPEN_BITS; /* 0xE000EF34 |= 3 << 30 */
```

**③ 첫 태스크 시작 전 CONTROL을 0으로**(`prvPortStartFirstTask()`): main()이 FP를 썼다면 FPCA = 1인 채로 SVC에 들어가 MSP에 쓸모없는 104 B 프레임이 생기므로, 그 비트를 지운다. 주석 그대로: "clears the bit that indicates the FPU is in use in case the FPU was used before the scheduler was started". 이 시점엔 살아 있는 FP 값이 없으니 예제 4의 D와 달리 안전하다.

```asm
    ldr r0, =0xE000ED08   /* VTOR → 벡터 테이블 → 초기 MSP 값 */
    ldr r0, [r0]
    ldr r0, [r0]
    msr msp, r0           /* main 스택을 처음으로 되돌린다 */
    mov r0, #0
    msr control, r0       /* FPCA = 0, SPSEL = 0 */
    cpsie i
    cpsie f
    dsb
    isb
    svc 0
```

**④ PendSV가 태스크마다 EXC_RETURN을 저장하고, bit 4를 보고 S16–S31을 저장 · 복원**:

```asm
    mrs r0, psp
    isb
    ldr r3, =pxCurrentTCB
    ldr r2, [r3]
    tst r14, #0x10          /* Is the task using the FPU context?  If so, push high vfp registers. */
    it eq
    vstmdbeq r0!, {s16-s31}
    stmdb r0!, {r4-r11, r14} /* r14 = 이 태스크의 EXC_RETURN도 같이 저장 */
    str r0, [r2]
    ...                      /* BASEPRI 올리고 vTaskSwitchContext() */
    ldmia r0!, {r4-r11, r14}
    tst r14, #0x10          /* Is the task using the FPU context?  If so, pop the high vfp registers too. */
    it eq
    vldmiaeq r0!, {s16-s31}
    msr psp, r0
    isb
    bx r14
```

ARM_CM3 포트와 비교하면 차이는 딱 두 가지다. **EXC_RETURN(r14)을 태스크마다 저장**한다(CM3 포트는 안 한다 — 모든 태스크의 EXC_RETURN이 `0xFFFFFFFD`로 같으니까). 그리고 **bit 4가 0인 태스크만** S16–S31 64 B를 더 옮긴다. 새 태스크의 초기 스택(`pxPortInitialiseStack()`)에는 `portINITIAL_EXC_RETURN = 0xfffffffd`가 들어가므로, **모든 태스크는 "FP 안 씀"으로 시작**하고 첫 FP 명령에서 FPCA가 켜지는 순간부터 FP 태스크가 된다.

중요한 미묘함: PendSV의 `vstmdbeq`는 그 자체가 FP 명령이다. 나가는 태스크가 FP 태스크라면 PendSV 진입 때 확장 프레임 자리만 잡혀 있고(LSPACT = 1), 바로 이 `vstmdb`가 **미뤄 둔 S0–S15 저장을 터뜨린다**. 그래서 FP 태스크의 문맥은 전환 순간 104 + 100 B가 모두 진짜로 메모리에 간다.

### 예제 6 — 빌드된 바이너리에서 PendSV 확인 (objdump)

무엇을 확인하나: 위 C 소스의 inline asm이 실제 ELF에서 그대로인지, BASEPRI 값이 설정(`configMAX_SYSCALL_INTERRUPT_PRIORITY = 0x20`)대로 들어갔는지.

```sh
arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/B6/ex5/app.elf | awk '$2=="<PendSV_Handler>:"{p=1;next} /^$/{p=0} p'
```

```text
    2050:	mrs	r0, PSP
    2054:	isb	sy
    2058:	ldr	r3, [pc, #76]	@ (20a8 <PendSV_Handler+0x58>)
    205a:	ldr	r2, [r3, #0]
    205c:	tst.w	lr, #16
    2060:	it	eq
    2062:	vstmdbeq	r0!, {s16-s31}
    2066:	stmdb	r0!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
    206a:	str	r0, [r2, #0]
    206c:	push	{r0, r3}
    206e:	mov.w	r0, #32
    2072:	msr	BASEPRI, r0
    2076:	dsb	sy
    207a:	isb	sy
    207e:	bl	f28 <vTaskSwitchContext>
    2082:	mov.w	r0, #0
    2086:	msr	BASEPRI, r0
    208a:	pop	{r0, r3}
    208c:	ldr	r1, [r3, #0]
    208e:	ldr	r0, [r1, #0]
    2090:	ldmia.w	r0!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
    2094:	tst.w	lr, #16
    2098:	it	eq
    209a:	vldmiaeq	r0!, {s16-s31}
    209e:	msr	PSP, r0
    20a2:	isb	sy
    20a6:	bx	lr
```

출력에서 볼 것: 랩의 `FreeRTOSConfig.h`가 `#define xPortPendSVHandler PendSV_Handler`로 CMSIS 이름에 연결했으므로 심볼이 `PendSV_Handler`다. `tst.w lr, #16`(= 0x10) 두 번, 조건부 `vstmdbeq` / `vldmiaeq`. `mov.w r0, #32`가 BASEPRI = 0x20이다. 명령 수로 보면 FP 태스크든 아니든 **같은 27개 명령**이 실행된다(조건이 거짓이어도 `it eq` 블록의 명령은 "실행"되어 icount에 1로 잡힌다). 이것이 다음 측정에서 icount 차이가 거의 안 나는 이유다.

### 예제 7 — 태스크 저장 문맥의 크기와 스택 high-water mark: 정수 vs FP vs "FP 한 번"

무엇을 확인하나: 전환되어 나간 태스크의 스택에 실제로 무엇이 몇 바이트 있는지, FP를 **딱 한 번** 쓴 태스크는 어떻게 되는지.

방법: TCB의 첫 멤버가 `pxTopOfStack`이라는 점(C1)을 이용해, 다른 태스크(`report`)가 세 태스크의 저장된 스택을 직접 읽는다. 저장 순서는 `r4–r11, r14` → (FP면) `s16–s31` → 하드웨어 프레임이다. 하드웨어 프레임의 xPSR bit 9로 정렬 패딩까지 따진다.

```c
/* ex5: FreeRTOS ARM_CM4F — 태스크의 저장 문맥(EXC_RETURN, 크기)과 스택 high-water mark를 FP 사용 여부별로 */
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

#define DEPTH 256
static StackType_t stk[3][DEPTH]; static StaticTask_t tcb[3];
static TaskHandle_t h[3];
static volatile uint32_t sink_i; static volatile float sink_f;

static void t_int(void *p)  { (void)p; uint32_t a = 0; for (;;) { for (int i = 0; i < 100; i++) a = a * 3u + (uint32_t)i; sink_i = a; taskYIELD(); } }
static void t_fp(void *p)   { (void)p; float a = 0;    for (;;) { for (int i = 0; i < 100; i++) a = a * 0.5f + (float)i; sink_f = a; taskYIELD(); } }
static void t_once(void *p) { (void)p; sink_f = sink_f + 1.0f;                /* FP 딱 한 번 */
                              uint32_t a = 0; for (;;) { for (int i = 0; i < 100; i++) a = a * 3u + (uint32_t)i; sink_i = a; taskYIELD(); } }

static void report(void *p)
{
    (void)p;
    vTaskDelay(20);                                     /* 세 태스크가 수천 번 yield하게 둔다 */
    for (int i = 0; i < 3; i++) {
        uint32_t *top = *(uint32_t **)h[i];             /* TCB 첫 멤버 = pxTopOfStack */
        uint32_t exc = top[8];                          /* r4-r11 다음에 저장된 r14 */
        int fp = !(exc & 0x10u);
        uint32_t *hw = top + 9 + (fp ? 16 : 0);         /* 하드웨어 프레임 시작 */
        uint32_t xpsr = hw[7], pad = (xpsr >> 9) & 1u;
        uint32_t ctx = 36u + (fp ? 64u : 0u) + (fp ? 104u : 32u) + pad * 4u;
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(h[i]);
        int a5 = 0; for (int k = 0; k < 8; k++) a5 += (top[k] == 0xa5a5a5a5u);   /* 저장된 r4-r11 */
        board_printf_raw("%s: EXC_RETURN=0x%x ctx=%u B  used(max, HWM)=%u B  r4-r11==0xa5a5a5a5: %d/8\n",
            pcTaskGetName(h[i]), exc, ctx, (unsigned)((DEPTH - hwm) * 4u), a5);
    }
    board_exit(0);
}

int main(void)
{
    h[0] = xTaskCreateStatic(t_int,  "t_int ", DEPTH, NULL, 1, stk[0], &tcb[0]);
    h[1] = xTaskCreateStatic(t_fp,   "t_fp  ", DEPTH, NULL, 1, stk[1], &tcb[1]);
    h[2] = xTaskCreateStatic(t_once, "t_once", DEPTH, NULL, 1, stk[2], &tcb[2]);
    xTaskCreate(report, "rep", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
make -s -B -f $L/fr.mk SRCS=.../ex5.c OUT=/var/tmp/rtos/B6/ex5 && ICOUNT=1 $L/run.sh /var/tmp/rtos/B6/ex5/app.elf 10
```

```text
t_int : EXC_RETURN=0xfffffffd ctx=68 B  used(max, HWM)=40 B  r4-r11==0xa5a5a5a5: 8/8
t_fp  : EXC_RETURN=0xffffffed ctx=204 B  used(max, HWM)=176 B  r4-r11==0xa5a5a5a5: 8/8
t_once: EXC_RETURN=0xffffffed ctx=204 B  used(max, HWM)=176 B  r4-r11==0xa5a5a5a5: 8/8
[board_exit] code=0
```

출력에서 볼 것:

- **정수 태스크 68 B, FP 태스크 204 B — 태스크당 +136 B**. 136 = 하드웨어 확장분 72 B(S0–S15 · FPSCR · 예약) + PendSV의 S16–S31 64 B. 스택 high-water mark로 잰 사용량도 40 → 176 B로 정확히 136 B 늘었다.
- **`t_once`는 시작할 때 float 덧셈 한 번만 했는데도 영원히 FP 태스크다**(EXC_RETURN `0xFFFFFFED`). FPCA는 그 태스크의 EXC_RETURN에 실려 전환마다 되살아나고, FreeRTOS ARM_CM4F에는 그것을 끄는 API가 없다(Zephyr에는 `k_float_disable()`이 있다, §7). 로그 한 줄에 `%f`를 쓴 태스크, `sqrtf()`를 한 번 부른 태스크가 모두 이렇게 된다.
- **HWM이 실제 사용량보다 작게 나온다**: 저장 문맥은 68 B인데 HWM 사용량은 40 B. 저장된 r4–r11 8워드가 모두 `0xA5A5A5A5`, 즉 **스택 채움 패턴과 같은 값**이라 HWM 계산이 "안 쓴 칸"으로 센다. 원인은 `pxPortInitialiseStack()`이 r4–r11 자리를 `pxTopOfStack -= 8`로 건너뛰어 채움 패턴이 그대로 레지스터 초기값이 되고, 이 태스크들이 r4–r11을 안 쓰기 때문이다. **HWM은 최대 32 B 정도 낙관적일 수 있다** — 여유를 그만큼 더 둔다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">FreeRTOS ARM_CM4F: 전환되어 나간 태스크 스택 (위 = 낮은 주소)</text>
<text x="140.0" y="48.0" font-size="13" text-anchor="middle" font-weight="bold">정수 태스크: 68 B</text>
<rect x="40.0" y="60.0" width="200.0" height="41.4" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="140.0" y="76.9" font-size="12" text-anchor="middle">R4–R11 + EXC_RETURN</text>
<text x="140.0" y="92.9" font-size="12" text-anchor="middle">36 B</text>
<rect x="40.0" y="101.4" width="200.0" height="36.8" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="140.0" y="116.0" font-size="12" text-anchor="middle">HW 기본 프레임</text>
<text x="140.0" y="132.0" font-size="12" text-anchor="middle">32 B</text>
<text x="500.0" y="48.0" font-size="13" text-anchor="middle" font-weight="bold">FP 태스크: 204 B</text>
<rect x="400.0" y="60.0" width="200.0" height="41.4" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="500.0" y="76.9" font-size="12" text-anchor="middle">R4–R11 + EXC_RETURN</text>
<text x="500.0" y="92.9" font-size="12" text-anchor="middle">36 B</text>
<rect x="400.0" y="101.4" width="200.0" height="73.6" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="500.0" y="134.4" font-size="12" text-anchor="middle">S16–S31</text>
<text x="500.0" y="150.4" font-size="12" text-anchor="middle">64 B</text>
<rect x="400.0" y="175.0" width="200.0" height="119.6" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="500.0" y="231.0" font-size="12" text-anchor="middle">HW 확장 프레임</text>
<text x="500.0" y="247.0" font-size="12" text-anchor="middle">104 B</text>
<text x="40.0" y="160.0" font-size="12" text-anchor="start">pxTopOfStack → 맨 위 (TCB 첫 멤버)</text>
<text x="40.0" y="185.0" font-size="12" text-anchor="start">EXC_RETURN = 0xFFFFFFFD</text>
<text x="40.0" y="210.0" font-size="12" text-anchor="start">(bit4=1 → S16–S31 없음)</text>
<text x="400.0" y="315.0" font-size="12" text-anchor="start">EXC_RETURN = 0xFFFFFFED (bit4=0)</text>
</svg>
```

그림 5 — 전환되어 나간 태스크 스택. 같은 태스크라도 FP 명령을 한 번 실행한 뒤로는 오른쪽 모양(204 B)으로 저장된다.

### 예제 8 — 컨텍스트 스위치 비용: FP 태스크 둘 vs 정수 태스크 둘

무엇을 확인하나: `taskYIELD()` 핑퐁 10000번의 시간을 FP 문맥이 있을 때와 없을 때 비교. icount가 무엇을 세고 무엇을 안 세는지.

```c
/* ex6: taskYIELD 핑퐁 — 두 태스크가 FP 문맥을 가질 때와 아닐 때 컨텍스트 스위치 비용 */
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#define CNT REG32(0x40028018u)                         /* FPGAIO COUNTER 25 MHz */
#define N 5000

static volatile int g_fp;                               /* 1이면 두 태스크가 매 바퀴 FP 레지스터를 만진다 */
static volatile uint32_t g_done;
static TaskHandle_t g_ctl;

static void pinger(void *p)
{
    (void)p;
    for (int i = 0; i < N; i++) {
        if (g_fp) __asm volatile("vmov s16, %0" :: "r"(i) : "s16");  /* FPCA=1 유지 */
        taskYIELD();
    }
    taskENTER_CRITICAL(); uint32_t d = ++g_done; taskEXIT_CRITICAL();
    if (d == 2) xTaskNotifyGive(g_ctl);               /* 마지막 태스크가 ctl을 깨운다 */
    vTaskSuspend(NULL);
}

static void ctl(void *p)
{
    (void)p;
    for (g_fp = 0; g_fp < 2; g_fp++) {
        g_done = 0;
        TaskHandle_t a, b;
        xTaskCreate(pinger, "A", 256, NULL, 1, &a);
        xTaskCreate(pinger, "B", 256, NULL, 1, &b);
        uint32_t t0 = CNT;
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);       /* ctl(우선순위 2)이 잠든 동안 A↔B 핑퐁 */
        uint32_t dt = CNT - t0;
        vTaskDelete(a); vTaskDelete(b);
        board_printf_raw("%s tasks: %u switches in %u us -> %u ns/switch (+tick)\n",
            g_fp ? "FP " : "INT", 2u * N, dt / 25u, dt * 40u / (2u * N));
    }
    board_exit(0);
}

int main(void)
{
    xTaskCreate(ctl, "ctl", 512, NULL, 2, &g_ctl);
    vTaskStartScheduler();
    return 1;
}
```

```text
ICOUNT
INT tasks: 10000 switches in 35717 us -> 3571 ns/switch (+tick)
FP  tasks: 10000 switches in 36042 us -> 3604 ns/switch (+tick)
[board_exit] code=0
HOST
INT tasks: 10000 switches in 39075 us -> 3907 ns/switch (+tick)
FP  tasks: 10000 switches in 36768 us -> 3676 ns/switch (+tick)
INT tasks: 10000 switches in 37450 us -> 3745 ns/switch (+tick)
FP  tasks: 10000 switches in 36437 us -> 3643 ns/switch (+tick)
INT tasks: 10000 switches in 40138 us -> 4013 ns/switch (+tick)
FP  tasks: 10000 switches in 35918 us -> 3591 ns/switch (+tick)
```

출력에서 볼 것:

- **icount: FP 태스크가 스위치당 33 ns(≈ 명령 1개) 더 든다** — 그 1개는 루프 안의 `vmov s16`이다. `vstmdb`/`vldmia` 16레지스터, lazy 저장 17워드, 확장 프레임 복원은 **0으로 잡힌다**.
- 호스트 시간은 잡음(±10 %)이 차이보다 커서 결론을 낼 수 없다. 예제 5처럼 ISR 하나만 재면 보이던 차이가, 스위치 한 번에 100개 넘는 명령(스택 검사 · run-time stats 포함)이 도는 경로에서는 묻힌다.
- 한 번에 111개 명령(3571 ns / 32 ns)은 랩 설정의 `configCHECK_FOR_STACK_OVERFLOW 2`(스위치마다 패턴 검사)와 run-time stats(`board_now_us()` 호출)가 들어간 값이다. 순수 스위치 비용은 C2 · A7이 다룬다.

**실칩 추정**(측정 아님, 1워드 ≈ 1사이클 가정): FP 태스크 → FP 태스크 전환 한 번에 더 옮기는 워드는 PendSV 진입의 확장분 약 18(lazy로 쓰이는 S0–S15 · FPSCR 17 + 예약 칸) + `vstmdb` 16 + `vldmia` 16 + 복귀 때 확장분 약 18 = **약 68워드, 대략 70사이클 이상**이다(예제 16의 계산). 168 MHz STM32F4에서 0.4 µs 남짓이다. 1 kHz 제어 루프에는 무시할 만하지만, **20 kHz FOC 전류 루프에서 태스크 전환을 하고 있다면** 무시 못 할 크기다(그래서 FOC 루프는 ISR에서 돈다, §13).

---

## 6. 고전 버그 — FP 문맥을 잃어버리는 세 가지 길

### 6.1 FPU가 있는 칩에서 FP를 모르는 포트(ARM_CM3)를 쓴다

M3 프로젝트를 M4F로 옮기면서 컴파일 옵션만 `-mfloat-abi=hard`로 바꾸고 FreeRTOS 포트는 `ARM_CM3`에 그대로 두는 실수가 흔하다. ARM_CM3 포트에는 `#error`도 없으니 그대로 빌드된다.

### 예제 9 — ARM_CM3 포트로 FP 태스크 두 개를 돌리면

무엇을 확인하나: 같은 앱을 ARM_CM4F / ARM_CM3 포트로 빌드해, 1 ms tick이 중간에 끊는 float 누적이 맞는지.

```c
/* ex7: 고전 버그 — FPU 칩(M4F)에서 FP를 저장하지 않는 포트(ARM_CM3)로 FP 태스크 두 개를 돌리면? */
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

#define N 300000
static volatile int g_bad[2], g_fin[2];

static void worker(void *p)
{
    int id = (int)(uintptr_t)p;
    float step = id ? 1.0f : 2.0f;                 /* A: 2씩, B: 1씩 더한다 */
    for (int round = 0; round < 3; round++) {
        float a = 0.0f;
        for (int i = 0; i < N; i++) a += step;     /* a는 S 레지스터에 산다; tick이 중간에 끊는다 */
        if (a != step * (float)N) g_bad[id]++;
        board_printf_raw("task %c round %d: a=%d (expect %d)\n", 'A' + id, round, (int)a, (int)(step * N));
    }
    g_fin[id] = 1;
    vTaskSuspend(NULL);
}

static void watch(void *p)
{
    (void)p;
    while (!(g_fin[0] && g_fin[1])) vTaskDelay(5);
    board_printf_raw("corrupted rounds: A=%d B=%d\n", g_bad[0], g_bad[1]);
    board_exit(0);
}

int main(void)
{
    xTaskCreate(worker, "A", 256, (void *)0, 1, NULL);
    xTaskCreate(worker, "B", 256, (void *)1, 1, NULL);
    xTaskCreate(watch, "W", 256, NULL, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
for P in ARM_CM4F ARM_CM3; do
  make -s -B -f $L/fr.mk SRCS=.../ex7.c OUT=/var/tmp/rtos/B6/ex7_$P PORT=$P
  ICOUNT=1 $L/run.sh /var/tmp/rtos/B6/ex7_$P/app.elf 20
done
```

```text
== PORT=ARM_CM4F
task A round 0: a=600000 (expect 600000)
task B round 0: a=300000 (expect 300000)
task A round 1: a=600000 (expect 600000)
task B round 1: a=300000 (expect 300000)
task A round 2: a=600000 (expect 600000)
task B round 2: a=300000 (expect 300000)
corrupted rounds: A=0 B=0
[board_exit] code=0
rc=0
== PORT=ARM_CM3
task B round 0: a=300000 (expect 300000)
task B round 1: a=300000 (expect 300000)
task B round 2: a=300000 (expect 300000)
qemu: fatal: Lockup: can't escalate 3 to HardFault (current priority -1)

R00=49127c00 R01=20000068 R02=a5a5a5a5 R03=200018b0
R04=20001898 R05=2000189c R06=20000e84 R07=a5a5a5a5
R08=a5a5a5a5 R09=a5a5a5a5 R10=a5a5a5a5 R11=a5a5a5a5
R12=2000186a R13=203fffe0 R14=ffffffed R15=00000004
XPSR=60000003 -ZC- A handler
... (s0–s31 덤프 생략)
FPSCR: 60000000
rc=134
```

출력에서 볼 것:

- CM4F 포트: 여섯 라운드 모두 정확하다.
- CM3 포트: **A는 한 줄도 못 찍고**, B만 끝난 뒤 CPU가 **lockup**(HardFault 안에서 또 fault — `XPSR` 예외 번호 3, "can't escalate 3 to HardFault"). `R14 = 0xFFFFFFED`가 단서다.
- 원인(추정 — 명령 단위로 추적하지는 않았다): CM3 포트의 PendSV는 태스크마다 EXC_RETURN을 저장하지 않고, **나가는 태스크의 LR(`0xFFFFFFED`)로 들어오는 태스크에 복귀**한다. 들어오는 태스크(B)의 스택에는 기본 프레임 8워드밖에 없는데 하드웨어는 26워드를 풀어 버린다. B의 PSP가 72 B 위(자기 스택 밖, heap_4의 이웃 블록 — A의 TCB나 스택일 수 있다)로 어긋난 채 실행되면서 다른 태스크의 메모리를 덮는다. B의 결과는 우연히 맞았고, 깨진 쪽(A)이 fault 연쇄로 lockup을 만든 것으로 보인다.
- **증상이 "가끔 값이 틀린다"일 수도, "랜덤 HardFault"일 수도, 이번처럼 lockup일 수도 있다**. 공통 원인은 "포트가 FP 문맥을 모른다"이다. 고치는 법: FPU 칩에는 ARM_CM4F(M4F · M7) / ARM_CM33 등에서 `configENABLE_FPU = 1`인 포트를 쓴다. ARM_CM33 계열 포트는 `configENABLE_FPU`를 정의하지 않으면 `#error`로 멈춰 이 실수를 막는다(`portmacrocommon.h`).

### 6.2 하드웨어 자동 저장을 끈 채 ISR이 FP를 쓴다

예제 4의 D · E줄이 이 경우다. ASPEN을 끄거나 FPCA를 지운 상태에서 ISR이 S0–S15를 건드리면, **fault도 경고도 없이** 스레드의 FP 값이 바뀐다. 실무에서는 다음 형태로 나타난다.

- "속도를 위해" 시작 코드에서 `FPCCR = 0`으로 lazy/자동 저장을 꺼 둔 BSP.
- RTOS 설정상 "FP는 한 문맥만 쓴다"(Zephyr unshared 모드)인데, 누군가 ISR에서 `float` 필터를 돌린다.
- ISR이 직접은 float을 안 쓰지만 **컴파일러가 memcpy · 구조체 복사에 FP 레지스터를 쓴다**(`vldmia`/`vstmia`로 64비트 복사 — 일부 GCC 버전 · 옵션에서 나타난다). 그래서 Zephyr Kconfig 도움말이 "some compiler configurations may activate a floating point context by generating FP instructions for any thread"라고 경고한다.

### 6.3 `printf("%f")` — 스택이 부풀고 태스크가 FP 태스크가 된다

### 예제 10 — snprintf `%d` vs `%f` vs `%e`: 스택 사용량, FLASH, FPCA

무엇을 확인하나: 로그 한 줄에 `%f`를 쓰는 대가.

```c
/* ex8: printf("%f")의 스택 비용 — 같은 태스크에서 %d와 %f를 찍고 high-water mark 비교 */
#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

#define DEPTH 1024                                      /* words = 4 KB */
static volatile float g_v = 3.14159f;
static volatile int   g_i = 314159;

static void probe(void *p)
{
    int kind = (int)(uintptr_t)p;
    char buf[48];
    if (kind == 0) snprintf(buf, sizeof buf, "i=%d", g_i);
    if (kind == 1) snprintf(buf, sizeof buf, "f=%f", (double)g_v);
    if (kind == 2) snprintf(buf, sizeof buf, "f=%.3e", (double)g_v);
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
    uint32_t ctl; __asm volatile("mrs %0, control" : "=r"(ctl));
    board_printf_raw("%s  -> stack used(max) = %u B, CONTROL.FPCA=%u\n",
                     buf, (unsigned)((DEPTH - hwm) * 4u), (ctl >> 2) & 1u);
    vTaskDelete(NULL);
}

static void ctl(void *p)
{
    (void)p;
    for (int k = 0; k < 3; k++) { xTaskCreate(probe, "probe", DEPTH, (void *)(uintptr_t)k, 3, NULL); vTaskDelay(2); }
    board_exit(0);
}

int main(void)
{
    xTaskCreate(ctl, "ctl", 256, NULL, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
make ... OUT=/var/tmp/rtos/B6/ex8a                                   # newlib-nano 기본
make ... OUT=/var/tmp/rtos/B6/ex8b EXTRA_LDFLAGS="-u _printf_float"  # float 지원 링크
```

```text
== nano, no _printf_float
  16664	    108	 135408	 152180	  25274	/var/tmp/rtos/B6/ex8a/app.elf
i=314159  -> stack used(max) = 484 B, CONTROL.FPCA=0
f=  -> stack used(max) = 396 B, CONTROL.FPCA=0
f=  -> stack used(max) = 396 B, CONTROL.FPCA=0
[board_exit] code=0
== nano + -u _printf_float
  31208	    472	 135408	 167088	  28cb0	/var/tmp/rtos/B6/ex8b/app.elf
i=314159  -> stack used(max) = 484 B, CONTROL.FPCA=0
f=3.141590  -> stack used(max) = 784 B, CONTROL.FPCA=1
f=3.142e+00  -> stack used(max) = 704 B, CONTROL.FPCA=1
[board_exit] code=0
```

출력에서 볼 것:

- newlib-nano 기본으로는 `%f`가 **아무것도 안 찍는다**(`f=`). 에러도 경고도 없다. "printf로 float이 안 나와요"의 원인 1순위다.
- `-u _printf_float`로 켜면 FLASH(text)가 16.7 KB → 31.2 KB로 **+14.5 KB**, 그 태스크의 스택 사용량은 484 → 784 B로 **+300 B**다.
- 그리고 **CONTROL.FPCA = 1** — `_printf_float` 내부가 FP 레지스터를 쓰므로 로그 한 줄로 그 태스크는 FP 태스크가 되어 전환마다 +136 B를 더 낸다(예제 7). 합치면 "로그에 `%f` 한 번 = 스택 약 +440 B".
- 실무 처방: 고정소수점으로 찍는다(`%d.%03d`), 로그를 한 태스크로 모은다(D 모듈의 deferred logging), 또는 그 태스크의 스택을 측정해서 정한다.

---

## 7. Zephyr — `CONFIG_FPU`, `CONFIG_FPU_SHARING`, `K_FP_REGS`

### 7.1 Zephyr의 설계 — S16–S31은 스택이 아니라 `struct k_thread` 안에

Zephyr v4.4.2 Cortex-M의 PendSV(`arch/arm/core/cortex_m/swap_helper.S`의 `z_arm_pendsv`)도 FreeRTOS와 같은 `tst lr, #_EXC_RETURN_FTYPE_Msk`로 FP 태스크를 가린다. 다른 점은 저장 위치다.

```asm
#ifdef CONFIG_FPU_SHARING
    /* Assess whether switched-out thread had been using the FP registers. */
    tst lr, #_EXC_RETURN_FTYPE_Msk
    bne .L_out_fp_endif

    /* FP context active: set FP state and store callee-saved registers.
     * Note: if Lazy FP stacking is enabled, storing the callee-saved
     * registers will automatically trigger FP state preservation in
     * the thread's stack. This will also clear the FPCCR.LSPACT flag.
     */
    add r0, r2, #_thread_offset_to_preempt_float
    vstmia r0, {s16-s31}
```

`_thread_offset_to_preempt_float`는 `struct k_thread` 안의 `arch.preempt_float`(`include/zephyr/arch/arm/thread.h`의 `struct _preempt_float`, s16–s31)를 가리킨다. 즉 **callee-saved 64 B는 스레드 구조체에, caller-saved 72 B는 스레드 스택(하드웨어 프레임)에** 들어간다. 복원 쪽에서는 FP를 안 쓰는 스레드로 들어갈 때 FPSCR을 0으로 만들고, 끝에서 CONTROL.FPCA를 지운다.

시작 코드(`arch/arm/core/cortex_m/prep_c.c`의 `z_arm_floating_point_init()`)는 CPACR을 먼저 지우고 연 다음, FPU_SHARING이면 `FPCCR = ASPEN | LSPEN`, 아니면 **ASPEN · LSPEN을 끈다**("Unshared FP registers mode … the use of FP registers in ISRs is not supported"). 그리고 DSB · ISB를 친다. Armv8.1-M에서는 FPSCR의 LTPSIZE를 4로 초기화하는데, 이게 왜 필요한지는 예제 15에서 실제로 부딪쳐 본다.

Zephyr 문서(`doc/kernel/services/other/float.rst`)의 Cortex-M 절 요약:

- Shared FP registers 모드가 Cortex-M의 기본이고, 커널은 **모든 스레드를 잠재적 FPU 사용자로 본다**. 첫 FP 사용을 자동 감지한다(FreeRTOS와 같은 FPCA 방식).
- `K_FP_REGS` 옵션으로 미리 표시(pretag)할 수 있다 — MPU 스택 가드 크기를 처음부터 FP용으로 잡기 위해서다.
- "Each thread that intends to use the floating point registers must provide an extra 72 bytes of stack space" — 위의 caller-saved 72 B 이야기다.
- `k_float_disable()`로 스레드의 FP 문맥 저장을 끌 수 있다(`arch_float_disable()`, `arch/arm/core/cortex_m/thread.c`).

### 예제 11 — Zephyr mps2/an386: FPU 공유 vs FPU 끔, 그리고 "끌 수 없는 FPU_SHARING"

무엇을 확인하나: 같은 두 스레드(1 ms 타임슬라이스로 끊기는 float 누적)를 `CONFIG_FPU=y`, `CONFIG_FPU=y CONFIG_FPU_SHARING=n`, `CONFIG_FPU=n`으로 빌드해 정확성 · `sizeof(struct k_thread)` · 시간을 비교.

```c
/* ex13: Zephyr v4.4.2 mps2/an386 — CONFIG_FPU / CONFIG_FPU_SHARING에 따라 FP 스레드 두 개가 안전한가 */
#include <zephyr/kernel.h>
#define N 300000
static volatile int bad[2];

static void worker(void *p1, void *p2, void *p3)
{
    int id = (int)(uintptr_t)p1; ARG_UNUSED(p2); ARG_UNUSED(p3);
    float step = id ? 1.0f : 2.0f;
    for (int r = 0; r < 3; r++) {
        float a = 0.0f;
        for (int i = 0; i < N; i++) a += step;          /* 1 ms 타임슬라이스가 중간에 끊는다 */
        if (a != step * (float)N) bad[id]++;
    }
}
K_THREAD_STACK_DEFINE(s0, 1024); K_THREAD_STACK_DEFINE(s1, 1024);
static struct k_thread t0, t1;

int main(void)
{
    uint32_t t_ms = k_uptime_get_32();
    k_tid_t a = k_thread_create(&t0, s0, 1024, worker, (void *)0, NULL, NULL, 5, 0, K_NO_WAIT);
    k_tid_t b = k_thread_create(&t1, s1, 1024, worker, (void *)1, NULL, NULL, 5, 0, K_NO_WAIT);
    k_thread_join(a, K_FOREVER); k_thread_join(b, K_FOREVER);
    t_ms = k_uptime_get_32() - t_ms;
    size_t u0, u1; k_thread_stack_space_get(a, &u0); k_thread_stack_space_get(b, &u1);
    printk("FPU=%d FPU_SHARING=%d sizeof(k_thread)=%u corrupted A=%d B=%d\n  unused stack A=%u B=%u  elapsed=%u ms\n",
           IS_ENABLED(CONFIG_FPU), IS_ENABLED(CONFIG_FPU_SHARING), (unsigned)sizeof(struct k_thread),
           bad[0], bad[1], (unsigned)u0, (unsigned)u1, t_ms);
    return 0;
}
```

```text
# prj.conf
CONFIG_PRINTK=y
CONFIG_TIMESLICING=y
CONFIG_TIMESLICE_SIZE=1
CONFIG_INIT_STACKS=y
CONFIG_THREAD_STACK_INFO=y
```

```sh
rtos-zbuild mps2/an386 ~/.rtos_scratch/B6/zfp /var/tmp/rtos/B6/z_un -- -DCONFIG_FPU=y -DCONFIG_FPU_SHARING=n
rtos-zbuild mps2/an386 ~/.rtos_scratch/B6/zfp /var/tmp/rtos/B6/z_sh -- -DCONFIG_FPU=y
rtos-zbuild mps2/an386 ~/.rtos_scratch/B6/zfp /var/tmp/rtos/B6/z_nofpu -- -DCONFIG_FPU=n
rtos-zrun /var/tmp/rtos/B6/z_<name> 60
```

```text
== un (-DCONFIG_FPU=y -DCONFIG_FPU_SHARING=n)
warning: FPU_SHARING (defined at arch/Kconfig:1075) was assigned the value 'n' but got the value
           FLASH:       18200 B         4 MB      0.43%
             RAM:        8816 B         4 MB      0.21%
FPU=1 FPU_SHARING=1 sizeof(k_thread)=192 corrupted A=0 B=0
  unused stack A=904 B=912  elapsed=700 ms
== sh (-DCONFIG_FPU=y)
           FLASH:       18200 B         4 MB      0.43%
             RAM:        8816 B         4 MB      0.21%
FPU=1 FPU_SHARING=1 sizeof(k_thread)=192 corrupted A=0 B=0
  unused stack A=904 B=912  elapsed=700 ms
== nofpu (-DCONFIG_FPU=n)
           FLASH:       13972 B         4 MB      0.33%
             RAM:        8488 B         4 MB      0.20%
FPU=0 FPU_SHARING=0 sizeof(k_thread)=128 corrupted A=0 B=0
  unused stack A=904 B=904  elapsed=12130 ms
```

(빌드 출력은 `rtos-zbuild`가 거른 줄만 남는다. 경고 문장이 중간에서 끊긴 것도 그 필터 때문이며 그대로 옮겼다.)

출력에서 볼 것:

- **Zephyr v4.4.2 Arm에서는 `CONFIG_FPU=y`이면 `CONFIG_FPU_SHARING`을 끌 수 없었다**. `arch/arm/core/Kconfig`의 "Floating point ABI" choice에서 `FP_HARDABI`와 `FP_SOFTABI`가 **둘 다 `select FPU_SHARING`**이다(주석: "Both FP_HARDABI and FP_SOFTABI require FPU sharing support as the compiler may generate code that uses the FPU even if the application does not explicitly use floating point operations"). 그래서 위험한 unshared 모드(예제 4의 E)는 이 버전 Cortex-M에서는 Kconfig로 막혀 있다. 문서(float.rst)의 "Unshared FP registers mode" 설명은 남아 있으니, **버전마다 Kconfig를 직접 확인**해야 한다.
- `sizeof(struct k_thread)`: 192 B vs 128 B — **차이 64 B = `preempt_float`(S16–S31)**. 스레드마다 RAM 64 B가 정적으로 붙는다(FP를 쓰든 안 쓰든).
- `CONFIG_FPU=n`(이 보드에서 소프트 float로 빌드)은 결과는 맞지만 **17배 느리다**(700 ms vs 12130 ms, Zephyr 보드 기본 `CONFIG_QEMU_ICOUNT_SHIFT=7` 가상 시간). FLASH는 4.2 KB 작다.
- 스택 사용량(1024 B 중 미사용 904~912 B)은 거의 같다 — FP 스레드는 확장 프레임 104 B, 소프트 float 스레드는 `__aeabi_fadd` 호출 프레임이 비슷한 크기로 들어갔다.

---

## 8. DSP 명령 — SMLAD, SSAT, QADD

> 🔁 이 절은 Bare-Metal BM C10과 겹친다 (주인: 이 노트).

### 8.1 직관 — FPU 없이도 신호 처리를 빠르게

Cortex-M4(그리고 M7, M33 · M55의 DSP 확장)는 FPU와 별개로 **DSP 확장 명령**을 가진다. 32비트 레지스터 하나에 16비트 값 두 개(또는 8비트 네 개)를 넣고 한 명령으로 동시에 처리하는 **SIMD32**와, 넘치면 감싸 돌지 않고 최댓값에 멈추는 **포화(saturating) 연산**이다. FPU가 없던 시절의 오디오 · 모터 제어 코드가 다 이걸로 짜였고, 지금도 CMSIS-DSP의 Q15 · Q31 함수 대부분이 이 명령을 쓴다.

Q15란: 16비트 정수를 `[-1, 1)` 범위의 소수로 읽는 약속이다. 값 = 정수 / 32768. Q15 × Q15 = Q30(32비트)이고, 다시 Q15로 돌리려면 15비트 오른쪽 시프트 후 **포화**시킨다.

| 명령 | 하는 일 | C로 쓰면 | 쓰는 곳 |
|---|---|---|---|
| SMLAD Rd, Rn, Rm, Ra | Rd = Ra + Rn.lo × Rm.lo + Rn.hi × Rm.hi (16비트 곱 2개 누적) | `__smlad(a, b, acc)` (ACLE) / `__SMLAD` (CMSIS) | FIR · 내적 · 상관 |
| SMLABB | Rd = Ra + Rn.lo × Rm.lo (곱 1개) | 컴파일러가 plain C에서 자동 생성 | 같은 루프, 1쌍씩 |
| SSAT Rd, #n, Rm | Rm을 n비트 부호 있는 범위로 포화 | `__ssat(x, 16)` | Q30 → Q15 변환, PWM 듀티 제한 |
| QADD Rd, Rm, Rn | 32비트 포화 덧셈, 포화되면 APSR.Q = 1 | `__qadd(a, b)` | 적분기 · 누산기 넘침 방지 |
| SMLALD | SMLAD의 64비트 누산판 | `__smlald` | 긴 FIR에서 누산기 넘침 방지 |

### 예제 12 — 64탭 Q15 내적: plain C vs `__smlad` vs float FPU, 그리고 포화

무엇을 확인하나: 같은 계산을 세 방식으로, 명령 수(objdump)와 icount 시간으로 비교. 포화 명령이 wrap-around와 어떻게 다른지.

```c
/* ex9: Q15 dot product(FIR 한 출력) — plain C vs SMLAD(SIMD) vs float FPU, 그리고 SSAT/QADD 포화 */
#include <stdint.h>
#include <string.h>
#include <arm_acle.h>
#include "board.h"
#define CNT REG32(0x40028018u)                          /* FPGAIO COUNTER 25 MHz (ICOUNT: 32 ns/명령) */
#define TAPS 64
#define REP  200

static int16_t x[TAPS], h[TAPS]; static float xf[TAPS], hf[TAPS];

__attribute__((noinline)) int32_t dot_c(const int16_t *a, const int16_t *b, int n)
{ int32_t acc = 0; for (int i = 0; i < n; i++) acc += (int32_t)a[i] * b[i]; return acc; }

__attribute__((noinline)) int32_t dot_smlad(const int16_t *a, const int16_t *b, int n)
{
    int32_t acc = 0;
    for (int i = 0; i < n; i += 2) {                    /* 두 쌍을 한 명령에: acc += a0*b0 + a1*b1 */
        int16x2_t va, vb; memcpy(&va, a + i, 4); memcpy(&vb, b + i, 4);
        acc = __smlad(va, vb, acc);
    }
    return acc;
}

__attribute__((noinline)) float dot_f(const float *a, const float *b, int n)
{ float acc = 0; for (int i = 0; i < n; i++) acc += a[i] * b[i]; return acc; }

static uint32_t ns_call(uint32_t c) { return c * 40u / REP; }   /* 한 번 호출당 ns */

int main(void)
{
    for (int i = 0; i < TAPS; i++) { x[i] = (int16_t)(i * 300 - 9000); h[i] = (int16_t)(1000 - i * 20);
                                     xf[i] = x[i] / 32768.0f; hf[i] = h[i] / 32768.0f; }
    volatile int32_t r1 = 0, r2 = 0; volatile float r3 = 0; uint32_t t0, c1, c2, c3;
    t0 = CNT; for (int k = 0; k < REP; k++) r1 = dot_c(x, h, TAPS);     c1 = CNT - t0;
    t0 = CNT; for (int k = 0; k < REP; k++) r2 = dot_smlad(x, h, TAPS); c2 = CNT - t0;
    t0 = CNT; for (int k = 0; k < REP; k++) r3 = dot_f(xf, hf, TAPS);   c3 = CNT - t0;
    board_printf_raw("dot_c     = %d  %u ns/call\n", r1, ns_call(c1));
    board_printf_raw("dot_smlad = %d  %u ns/call\n", r2, ns_call(c2));
    board_printf_raw("dot_f     = %d (x2^30)  %u ns/call\n", (int)(r3 * 1073741824.0f), ns_call(c3));

    volatile int32_t y = (r1 >> 15) * 12;               /* Q30 → Q15, 이득 12배 → 범위 밖 */
    board_printf_raw("Q15 out: y=%d  (int16_t)y=%d  ssat16=%d\n", y, (int16_t)y, __ssat(y, 16));
    volatile int32_t big = 0x7FFFFF00;
    int32_t q = __qadd(big, 0x1000); uint32_t apsr; __asm volatile("mrs %0, apsr" : "=r"(apsr));
    board_printf_raw("add: plain=%d  qadd=%d  APSR.Q=%u\n", (int32_t)((uint32_t)big + 0x1000u), q, (apsr >> 27) & 1u);
    return 0;
}
```

```text
dot_c     = -120384000  10720 ns/call
dot_smlad = -120384000  6592 ns/call
dot_f     = -120384000 (x2^30)  10625 ns/call
Q15 out: y=-44088  (int16_t)y=21448  ssat16=-32768
add: plain=-2147479808  qadd=2147483647  APSR.Q=1
[board_exit] code=0
```

출력에서 볼 것:

- 세 방식의 결과가 정확히 같다(float은 2^30을 곱해 Q30으로 비교).
- **`__smlad`가 1.63배 빠르다**(icount 기준 명령 수). plain C 10720 ns ≈ 335명령, SMLAD 6592 ns ≈ 206명령.
- float FPU(`vfma.f32`)는 plain C Q15와 거의 같다 — M4F에서 단정밀도 곱셈누산은 Q15 스칼라 MAC과 비슷한 비용이다. "FPU가 있으면 Q15는 필요 없다"가 반쯤 맞는 이유이고, SIMD(2쌍/명령)를 쓰면 Q15가 다시 앞서는 이유다.
- **포화**: Q30 → Q15 결과에 이득 12를 곱해 범위를 넘긴 `-44088`은 그냥 `int16_t`로 자르면 **`+21448`(부호가 뒤집힘!)**, `__ssat(y, 16)`이면 `-32768`에서 멈춘다. 모터 제어에서 이 차이는 "최대 역방향 토크"와 "갑자기 정방향 토크"의 차이다.
- `__qadd(0x7FFFFF00, 0x1000)`은 `INT32_MAX`에서 멈추고 **APSR.Q(sticky) = 1**을 남긴다. plain 덧셈은 음수로 감싼다. Q 플래그는 "이 블록에서 한 번이라도 포화가 있었나"를 나중에 검사하는 용도다.

컴파일러가 만든 루프를 보면 차이가 그대로 보인다.

```sh
arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/B6/ex9/app.elf   # dot_c, dot_smlad, dot_f
```

```text
--- dot_c
      d0:	ldrh.w	lr, [r3, #2]!
      d4:	ldrh.w	ip, [r1, #2]!
      d8:	cmp	r3, r2
      da:	smlabb	r0, lr, ip, r0
      de:	bne.n	d0 <dot_c+0x10>
--- dot_smlad
      f4:	ldr.w	lr, [ip], #4
      f8:	ldr.w	r2, [r1], #4
      fc:	adds	r3, #2
      fe:	cmp	r4, r3
     100:	smlad	r0, lr, r2, r0
     104:	bgt.n	f4 <dot_smlad+0xc>
--- dot_f
     118:	vldmia	r0!, {s14}
     11c:	vldmia	r1!, {s15}
     120:	cmp	r0, r2
     122:	vfma.f32	s0, s14, s15
     126:	bne.n	118 <dot_f+0xc>
```

(루프 본문만 남기고 프롤로그 · 에필로그는 생략했다.)

출력에서 볼 것: GCC는 plain C에서도 **`smlabb`(DSP 곱셈누산, 1쌍)**를 스스로 골랐다. `-mcpu=cortex-m4`가 DSP 확장을 알려 주기 때문이다. 하지만 두 쌍을 한 번에 읽어 `smlad`로 묶는 것(자동 SIMD화)은 이 루프에서 하지 않았다 — 그래서 intrinsic이나 CMSIS-DSP(`arm_dot_prod_q15()` 등)를 쓴다. 루프당 명령은 5개(1쌍) vs 6개(2쌍)다.

같은 `ex9.c`를 Cortex-M3용으로 컴파일하면 DSP 확장이 없으므로 intrinsic 타입부터 없다는 에러가 난다(실제 출력의 첫 줄).

```text
/Users/donh/.rtos_scratch/B6/ex9/ex9.c: In function 'dot_smlad':
/Users/donh/.rtos_scratch/B6/ex9/ex9.c:19:9: error: unknown type name 'int16x2_t'; did you mean 'int16_t'?
```

plain C `dot_c`만 M3로 컴파일하면 `ldrsh` 두 개와 일반 `mla`(32비트 곱셈누산)가 나온다. 이식성 있는 코드는 `#if defined(__ARM_FEATURE_DSP)`로 감싸고 plain C를 대체 경로로 둔다.

### 8.2 실칩에서는

Cortex-M4 TRM의 명령 타이밍 표에서 SMLAD · SMLABB · SSAT · QADD는 모두 단일 사이클 계열이다(M4의 32비트 MAC 하드웨어). 실칩에서 SMLAD 루프가 더 유리한 진짜 이유는 **메모리 접근이 절반**이라는 점이다 — 16비트 두 개를 32비트 로드 한 번(`ldr`)으로 가져온다. 플래시 wait state가 있는 실칩에서는 이 차이가 icount 비율(1.63배)보다 커질 수 있다. 정확한 값은 실칩의 DWT CYCCNT로 재야 한다.

---

## 9. float ABI — soft / softfp / hard의 비용

### 예제 13 — 같은 biquad 필터를 세 ABI로

무엇을 확인하나: `-mfloat-abi` 하나가 샘플당 비용과 코드 크기를 얼마나 바꾸는지, 함수 경계에서 무엇이 달라지는지.

```c
/* ex10: 같은 float 코드(biquad 필터)를 -mfloat-abi=soft / softfp / hard로 — 샘플당 비용과 크기 */
#include <stdint.h>
#include "board.h"
#define CNT REG32(0x40028018u)
#define NS 1000

typedef struct { float b0, b1, b2, a1, a2, x1, x2, y1, y2; } biquad_t;

__attribute__((noinline)) float mac3(float a, float b, float c) { return a * b + c; }  /* ABI 경계 보기용 */

__attribute__((noinline)) float biquad(biquad_t *f, float x)
{
    float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1; f->x1 = x; f->y2 = f->y1; f->y1 = y;
    return y;
}

int main(void)
{
    biquad_t lp = { 0.0675f, 0.1349f, 0.0675f, -1.1430f, 0.4128f, 0, 0, 0, 0 };   /* 저역통과 */
    volatile float out = 0; float x = 1.0f;
    uint32_t t0 = CNT;
    for (int n = 0; n < NS; n++) { out = biquad(&lp, x); x = -x; }      /* 나이퀴스트 입력 → 감쇠 */
    uint32_t dt = CNT - t0;
    board_printf_raw("ABI=%s  %u ns/sample  out*1e6=%d  mac3=%d\n", ABI, dt * 40u / NS,
                     (int)(out * 1e6f), (int)mac3(2.0f, 3.0f, 4.0f));
    return 0;
}
```

```sh
for F in soft softfp hard; do
  make -s -B -f $L/bm.mk SRCS=.../ex10.c OUT=/var/tmp/rtos/B6/ex10_$F FLOAT=$F EXTRA_CFLAGS="-DABI=\"$F\""
  ICOUNT=1 $L/run.sh /var/tmp/rtos/B6/ex10_$F/app.elf 10
done
```

```text
size text=5552
ABI=soft  12792 ns/sample  out*1e6=-39  mac3=10
size text=4572
ABI=softfp  896 ns/sample  out*1e6=-39  mac3=10
size text=4556
ABI=hard  864 ns/sample  out*1e6=-39  mac3=10
```

출력에서 볼 것:

- **soft는 hard보다 14.8배 느리고**(12792 vs 864 ns/샘플) 코드도 1 KB 크다(libgcc 소프트 float 루틴). FPU가 있는 칩에서 soft로 빌드하는 것은 대개 실수다(예: 다른 팀 라이브러리 때문에 ABI를 맞추느라).
- softfp와 hard의 차이는 **함수 경계의 `vmov`**뿐이라 작다(896 vs 864 = 호출당 명령 1개). 함수 호출이 많은 코드일수록 커진다.
- 출력값이 셋 다 같다(`out*1e6=-39`). 나이퀴스트 주파수(+1, -1 반복)를 넣은 저역통과 필터라 거의 0으로 감쇠한다.

```text
--- soft mac3
      c0:	push	{r4, lr}
      c2:	mov	r4, r2
      c4:	bl	4c0 <__aeabi_fmul>
      c8:	mov	r1, r4
      ca:	bl	634 <__addsf3>
      ce:	pop	{r4, pc}
--- softfp mac3
      c0:	vmov	s13, r0
      c4:	vmov	s14, r1
      c8:	vmov	s15, r2
      cc:	vfma.f32	s15, s13, s14
      d0:	vmov	r0, s15
      d4:	bx	lr
      d6:	nop
--- hard mac3
      c0:	vfma.f32	s2, s0, s1
      c4:	vmov.f32	s0, s2
      c8:	bx	lr
      ca:	nop
```

출력에서 볼 것: 같은 `a * b + c`가 soft에서는 libgcc 호출 두 번, softfp에서는 `vmov` 4개 + `vfma`, hard에서는 인자가 이미 S0–S2에 있어 `vfma` 하나다. soft 빌드의 `biquad()` 안에는 `bl __aeabi_fmul` · `__addsf3` · `__aeabi_fsub` 호출이 9개 있었다.

**RTOS 관점의 덤**: soft ABI로 빌드하면 FP 레지스터를 아무도 안 쓰므로 모든 태스크가 정수 태스크(68 B)다. 느린 대신 문맥 비용 문제가 사라진다. Zephyr `CONFIG_FPU=n`이 이 상태다(예제 11).

---

## 10. Helium (MVE) — Cortex-M55의 벡터 확장

> 🔁 이 절은 Bare-Metal BM C10과 겹친다 (주인: 이 노트). 코어 세대 속 위치(M55 · M85 · M52, Armv8.1-M)는 B7(BM C11)이 다룬다.

### 10.1 직관 — 128비트 벡터를 32비트씩 "beat"로 나눠 처리

Armv8.1-M의 **M-Profile Vector Extension(MVE)**, 상표명 **Helium**은 Cortex-A의 NEON과 비슷한 128비트 벡터 명령이다. 레지스터 Q0–Q7(128비트 8개)은 **FP 레지스터 S0–S31과 같은 저장소**를 쓴다(Q0 = S0–S3). 그래서 MVE도 CPACR의 CP10 · CP11로 켜고, 컨텍스트 저장도 FP와 똑같은 확장 프레임 · lazy stacking · S16–S31 규칙을 따른다 — 이 노트의 앞부분이 그대로 적용된다.

MCU 코어는 128비트 데이터 경로를 다 가질 여유가 없다. 그래서 MVE는 벡터 명령 하나를 **32비트 조각 4개 = beat A · B · C · D**로 나눠 실행하도록 정의되었다.

- **single-beat 구현**: 사이클당 beat 1개.
- **dual-beat 구현**: 사이클당 beat 2개. Cortex-M55가 dual-beat다.
- **beat 겹침(overlap)**: 앞 명령(예: 메모리 유닛의 `VLDR`)의 뒤쪽 beat와 다음 명령(예: MAC 유닛의 `VMLA`)의 앞쪽 beat가 같은 사이클에 돈다. 그래서 load와 MAC이 번갈아 나오는 루프는 두 유닛이 동시에 바쁘다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">Helium(MVE) beat: 128비트 벡터 명령 = 32비트 beat 4개 (A B C D)</text>
<text x="10.0" y="62.0" font-size="13" text-anchor="start" font-weight="bold">single-beat 구현 (사이클당 beat 1개 + 명령 겹침)</text>
<text x="195.0" y="82.0" font-size="12" text-anchor="middle">c1</text>
<text x="265.0" y="82.0" font-size="12" text-anchor="middle">c2</text>
<text x="335.0" y="82.0" font-size="12" text-anchor="middle">c3</text>
<text x="405.0" y="82.0" font-size="12" text-anchor="middle">c4</text>
<text x="475.0" y="82.0" font-size="12" text-anchor="middle">c5</text>
<text x="545.0" y="82.0" font-size="12" text-anchor="middle">c6</text>
<text x="150.0" y="107.0" font-size="12" text-anchor="end">VLDRH q0</text>
<rect x="162.0" y="90.0" width="66.0" height="24.0" rx="2" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<text x="195.0" y="107.0" font-size="12" text-anchor="middle">A</text>
<rect x="232.0" y="90.0" width="66.0" height="24.0" rx="2" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<text x="265.0" y="107.0" font-size="12" text-anchor="middle">B</text>
<rect x="302.0" y="90.0" width="66.0" height="24.0" rx="2" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<text x="335.0" y="107.0" font-size="12" text-anchor="middle">C</text>
<rect x="372.0" y="90.0" width="66.0" height="24.0" rx="2" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<text x="405.0" y="107.0" font-size="12" text-anchor="middle">D</text>
<text x="150.0" y="139.0" font-size="12" text-anchor="end">VMLAVA r0,q0,q1</text>
<rect x="302.0" y="122.0" width="66.0" height="24.0" rx="2" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/>
<text x="335.0" y="139.0" font-size="12" text-anchor="middle">A</text>
<rect x="372.0" y="122.0" width="66.0" height="24.0" rx="2" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/>
<text x="405.0" y="139.0" font-size="12" text-anchor="middle">B</text>
<rect x="442.0" y="122.0" width="66.0" height="24.0" rx="2" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/>
<text x="475.0" y="139.0" font-size="12" text-anchor="middle">C</text>
<rect x="512.0" y="122.0" width="66.0" height="24.0" rx="2" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/>
<text x="545.0" y="139.0" font-size="12" text-anchor="middle">D</text>
<text x="10.0" y="182.0" font-size="13" text-anchor="start" font-weight="bold">dual-beat 구현, 예: Cortex-M55 (사이클당 beat 2개)</text>
<text x="195.0" y="202.0" font-size="12" text-anchor="middle">c1</text>
<text x="265.0" y="202.0" font-size="12" text-anchor="middle">c2</text>
<text x="335.0" y="202.0" font-size="12" text-anchor="middle">c3</text>
<text x="405.0" y="202.0" font-size="12" text-anchor="middle">c4</text>
<text x="475.0" y="202.0" font-size="12" text-anchor="middle">c5</text>
<text x="545.0" y="202.0" font-size="12" text-anchor="middle">c6</text>
<text x="150.0" y="227.0" font-size="12" text-anchor="end">VLDRH q0</text>
<rect x="162.0" y="210.0" width="66.0" height="24.0" rx="2" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<text x="195.0" y="227.0" font-size="12" text-anchor="middle">AB</text>
<rect x="232.0" y="210.0" width="66.0" height="24.0" rx="2" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/>
<text x="265.0" y="227.0" font-size="12" text-anchor="middle">CD</text>
<text x="150.0" y="259.0" font-size="12" text-anchor="end">VMLAVA r0,q0,q1</text>
<rect x="232.0" y="242.0" width="66.0" height="24.0" rx="2" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/>
<text x="265.0" y="259.0" font-size="12" text-anchor="middle">AB</text>
<rect x="302.0" y="242.0" width="66.0" height="24.0" rx="2" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/>
<text x="335.0" y="259.0" font-size="12" text-anchor="middle">CD</text>
<text x="10.0" y="292.0" font-size="12" text-anchor="start">겹침: 앞 명령(메모리 유닛)의 뒷 beat와 다음 명령(MAC 유닛)의 앞 beat가 같은 사이클에 돈다 (개념도)</text>
</svg>
```

그림 6 — beat 개념도. 위: single-beat에서도 겹침 덕에 6사이클에 두 명령. 아래: dual-beat(M55)에서는 3사이클. 실제 겹침 규칙과 사이클 수는 코어마다 다르고 TRM의 명령 타이밍을 봐야 한다(이 그림은 원리만).

또 하나의 핵심이 **tail predication**이다. 길이가 8의 배수가 아닌 배열의 마지막 조각을 위해 VPR(Vector Predication Status and Control Register)의 lane 마스크를 쓰고, `DLSTP` / `LETP` 같은 **저오버헤드 루프(LOB)** 명령이 남은 원소 수에 맞춰 마스크를 자동으로 만든다. 꼬리 처리용 스칼라 루프가 필요 없다.

### 예제 14 — Cortex-M55에서 실제로 실행: scalar vs MVE intrinsic

무엇을 확인하나: QEMU `mps3-an547`(Cortex-M55)이 MVE를 실행하는지, 64탭 Q15 내적에서 명령 수가 얼마나 줄어드는지, 꼬리(61 = 8 × 7 + 5)가 맞는지.

AN547은 메모리 맵이 다르다(QEMU `info mtree`로 확인: ITCM `0x00000000` 512 KB, DTCM `0x20000000` 512 KB, FPGAIO `0x49302000`, UART0 `0x49303000`). 그래서 최소 시작 코드와 링커 스크립트를 따로 만들었고, 출력은 UART 대신 **semihosting**(`SYS_WRITE0`)으로 했다.

```c
/* ~/.rtos_scratch/B6/m55/start55.c 핵심 (전체는 출력 함수 포함 50줄) */
void Reset_Handler(void)
{
    *(volatile uint32_t *)0xE000ED88u |= 0xFu << 20;           /* CPACR: CP10/CP11 → FPU와 MVE 둘 다 */
    __asm volatile("dsb\n isb");
    for (uint32_t *s = &_sidata, *d = &_sdata; d < &_edata; ) *d++ = *s++;
    for (uint32_t *d = &_sbss; d < &_ebss; ) *d++ = 0;
    int rc = main();
    /* semihosting SYS_EXIT */
}
```

```c
/* ex11: Cortex-M55 Helium(MVE) — Q15 dot product: scalar SMLAD-식 vs MVE intrinsic(vmladavaq) */
#include <stdint.h>
#include <arm_mve.h>
void putu55(const char *label, int32_t v);
#define CNT (*(volatile uint32_t *)0x49302018u)      /* AN547 FPGAIO COUNTER (가정: AN385와 같은 오프셋) */
#define TAPS 64
#define REP  200
static int16_t x[TAPS], h[TAPS];

__attribute__((noinline)) int32_t dot_scalar(const int16_t *a, const int16_t *b, int n)
{ int32_t acc = 0; for (int i = 0; i < n; i++) acc += (int32_t)a[i] * b[i]; return acc; }

__attribute__((noinline)) int32_t dot_mve(const int16_t *a, const int16_t *b, int n)
{
    int32_t acc = 0;
    while (n > 0) {                                  /* 한 번에 8개(128비트) — 꼬리는 predicate로 */
        mve_pred16_t p = vctp16q(n);
        int16x8_t va = vldrhq_z_s16(a, p), vb = vldrhq_z_s16(b, p);
        acc = vmladavaq_p_s16(acc, va, vb, p);       /* acc += Σ va[i]*vb[i] (활성 lane만) */
        a += 8; b += 8; n -= 8;
    }
    return acc;
}

int main(void)
{
    for (int i = 0; i < TAPS; i++) { x[i] = (int16_t)(i * 300 - 9000); h[i] = (int16_t)(1000 - i * 20); }
    volatile int32_t r1 = 0, r2 = 0, r3 = 0; uint32_t t0, c1, c2;
    t0 = CNT; for (int k = 0; k < REP; k++) r1 = dot_scalar(x, h, TAPS); c1 = CNT - t0;
    t0 = CNT; for (int k = 0; k < REP; k++) r2 = dot_mve(x, h, TAPS);    c2 = CNT - t0;
    r3 = dot_mve(x, h, 61);                          /* 61 = 8×7 + 5 → 꼬리 predicate 확인 */
    putu55("dot_scalar", r1); putu55("  ns/call", (int32_t)(c1 * 40u / REP));
    putu55("dot_mve   ", r2); putu55("  ns/call", (int32_t)(c2 * 40u / REP));
    putu55("dot_mve(61)", r3); putu55("dot_scalar(61)", dot_scalar(x, h, 61));
    return 0;
}
```

```sh
arm-none-eabi-gcc -mthumb -mcpu=cortex-m55 -mfloat-abi=hard -O2 -g -Wall -Wextra -ffreestanding -nostdlib \
   -T m55.ld start55.c ex11.c -lgcc -o /var/tmp/rtos/B6/ex11/app_O2.elf
qemu-system-arm -machine mps3-an547 -cpu cortex-m55 -nographic -monitor none -serial null \
   -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off -kernel .../app_O2.elf
```

```text
   text	   data	    bss	    dec	    hex	filename
    980	     64	    256	   1300	    514	/var/tmp/rtos/B6/ex11/app_O2.elf
dot_scalar=-120384000
  ns/call=7066
dot_mve   =-120384000
  ns/call=1178
dot_mve(61)=-113460000
dot_scalar(61)=-113460000
rc=0
```

출력에서 볼 것:

- QEMU 10.2의 Cortex-M55는 **MVE를 실제로 실행한다**. 결과가 scalar와 같고, 61개(꼬리 5개)도 맞다 — tail predication이 동작한다.
- icount 기준 **6.0배 적은 명령**(7066 vs 1178 ns = 221 vs 37명령). 탭당 3.45명령 → 0.58명령.
- AN547의 FPGAIO COUNTER가 `0x49302018`에서 AN385와 같은 25 MHz로 동작했다(코드 주석의 "가정"이 맞았음을 이 출력으로 확인).
- **주의 — 이 6배는 실칩 속도가 아니다**. icount는 벡터 명령도 1개로 센다. 실제 M55(dual-beat)에서 128비트 `VMLAVA`는 2사이클 이상이고, 겹침 · 메모리 대기에 따라 달라진다. beat 구조의 이득 · 손해는 **QEMU로 보여 줄 수 없다**.

```text
--- O2 dot_mve
 1d6:	dlstp.16	lr, r2
 1da:	vldrh.u16	q3, [r3], #16
 1de:	vldrh.u16	q2, [r1], #16
 1e2:	vmlava.s16	r0, q3, q2
 1e6:	letp	lr, 1da <dot_mve+0xe>
--- O2 dot_scalar
 1ac:	dls	lr, lr
 ...
 1b4:	ldrh.w	ip, [r3, #2]!
 1b8:	ldrh.w	r2, [r1, #2]!
 1bc:	smlabb	r0, ip, r2, r0
 1c0:	le	lr, 1b4 <dot_scalar+0x20>
```

출력에서 볼 것: 컴파일러가 intrinsic 루프(`vctp16q` + `_z` load + `_p` MAC)를 알아보고 **`dlstp.16` / `letp`(tail-predicated 저오버헤드 루프)** 4명령짜리로 바꿨다. 8원소당 명령 4개다. scalar 쪽도 Armv8.1-M의 **`dls` / `le` 저오버헤드 루프**를 썼다(루프 카운트 · 분기를 하드웨어가 처리). `-O3`로도 scalar 루프는 자동 벡터화되지 않았다(같은 objdump, 시간 6989 ns).

### 예제 15 — M55 확장 프레임의 마지막 워드 = VPR, 그리고 LTPSIZE 함정

무엇을 확인하나: Armv7-M에서 "예약"이던 확장 프레임의 26번째 워드(+0x64)에 Armv8.1-M MVE 코어는 VPR을 저장하는지. SVC로 예외를 일으켜 프레임을 덤프한다(LSPEN = 0으로 즉시 저장).

```c
/* ex12: Cortex-M55(v8.1-M) 확장 프레임 — 예약 워드 자리에 VPR(MVE predicate)이 들어가나? */
#include <stdint.h>
void putx55(const char *label, uint32_t v);
#define FPCCR (*(volatile uint32_t *)0xE000EF34u)
static uint32_t fr[26], exc, sp_irq, g_sp0;

void svc_c(uint32_t *f, uint32_t lr) { sp_irq = (uint32_t)f; exc = lr; for (int i = 0; i < 26; i++) fr[i] = f[i]; }
__attribute__((naked)) void SVC_Handler(void)
{ __asm volatile(" mov r0, sp\n mov r1, lr\n push {r4, lr}\n bl svc_c\n pop {r4, pc}\n"); }

int main(void)
{
    putx55("FPCCR at reset ", FPCCR);
    FPCCR &= ~(1u << 30);                               /* LSPEN=0: 진입 때 바로 저장 */
    __asm volatile(" mov r0, #0x5555\n vmsr p0, r0\n"   /* VPR.P0 = 0x5555 */
                   " ldr r0, =0x03040000\n vmsr fpscr, r0\n"
                   " vmov s0, r0\n mov r0, sp\n str r0, [%0]\n svc 0\n" :: "r"(&g_sp0) : "r0", "s0", "memory");
    putx55("EXC_RETURN     ", exc);
    putx55("pushed bytes   ", g_sp0 - sp_irq);
    putx55("word 8  (S0)   ", fr[8]);
    putx55("word 24 (FPSCR)", fr[24]);
    putx55("word 25 (VPR)  ", fr[25]);
    return 0;
}
```

```text
FPCCR at reset =0xc0000004
EXC_RETURN     =0xffffffe9
pushed bytes   =0x00000068
word 8  (S0)   =0x03040000
word 24 (FPSCR)=0x03040000
word 25 (VPR)  =0x00005555
rc=0
```

출력에서 볼 것: 프레임은 여전히 **104 B(0x68)**, EXC_RETURN `0xFFFFFFE9`(Secure 상태에서도 같은 값). 마지막 워드에 우리가 넣은 **VPR = 0x5555**가 들어 있다. MVE의 predicate 상태도 S0–S15 · FPSCR과 같이 하드웨어가 저장해 준다는 뜻이다 — RTOS 포트가 MVE 때문에 따로 할 일이 거의 없다(S16–S31 = Q4–Q7은 여전히 PendSV 몫).

**처음 시도는 HardFault였다.** FPSCR에 v7-M 습관대로 `0x03000000`을 썼더니 SVC 핸들러 안의 루프에서 이렇게 죽었다.

```text
FPCCR(reset) hex-ish dec=-1073741820
[fault] IPSR=3
[fault] CFSR=131072
```

`CFSR = 131072 = 0x20000` = **INVSTATE**. Armv8.1-M의 FPSCR에는 bit 18:16에 **LTPSIZE**(저오버헤드 루프의 tail predication 원소 크기) 필드가 있고, "predication 없음"을 뜻하는 값이 0이 아니라 **4**다. LTPSIZE를 0으로 만든 채 컴파일러가 만든 `dls`/`le` 루프(위 objdump의 scalar 루프와 같은 종류)를 실행하니 INVSTATE가 났다. `0x03040000`(LTPSIZE = 4 유지)으로 고치니 정상이다. Zephyr가 v8.1-M에서 `__set_FPSCR(4 << FPU_FPDSCR_LTPSIZE_Pos)`로 초기화하는 이유가 이것이다(`prep_c.c` 주석: "the FPSCR[18:16] LTPSIZE field must be set to 0b100 for 'Tail predication not applied' as it's reset value"). **M4F 코드를 M55로 옮길 때 FPSCR을 통째로 쓰는 코드(`vmsr fpscr, rX`)는 반드시 읽고-고치고-쓰기로 바꾼다.**

---

## 11. 측정 종합

### 예제 16 — 측정값 정리와 실칩 추정 (Python)

무엇을 확인하나: 반복 측정의 중앙값, 문맥 크기 계산, 실칩 추정 사이클(측정 아님)을 한 곳에서. 그림의 막대 좌표도 같은 숫자로 계산했다.

```python
# ex15: 측정값 정리 — 5회 반복 중앙값, FP 문맥 비용(바이트), 실칩 추정 사이클(1워드≈1사이클 가정)
import statistics as st
host = {  # ex4를 ICOUNT 없이 5번 돌린 ns/IRQ (호스트 실시간 = QEMU 에뮬레이션 비용)
    "basic(FPCA=0)":      [1354, 1379, 1392, 1330, 1428],
    "EXT lazy, no FP":    [1375, 1378, 1418, 1345, 1514],
    "EXT lazy, FP":       [2769, 2734, 2832, 2693, 2770],
    "EXT immediate, noFP":[2675, 2669, 2662, 2659, 2685],
    "EXT immediate, FP":  [2613, 2693, 2632, 2628, 2711],
}
for k, v in host.items():
    print(f"{k:22s} median={st.median(v):5.0f} ns  spread={max(v)-min(v):3d} ns")

hw_basic, hw_ext, sw_int, sw_fp = 8, 26, 9, 9 + 16          # words
ctx_int, ctx_fp = (hw_basic + sw_int) * 4, (hw_ext + sw_fp) * 4
print(f"saved context: int task {ctx_int} B, FP task {ctx_fp} B, extra {ctx_fp-ctx_int} B per task")
# 실칩 추정(측정 아님): 스위치 한 번에 FP 태스크가 더 옮기는 워드 수
extra_words = (hw_ext - hw_basic) * 2 + 16 * 2              # 진입+복귀의 S0-S15/FPSCR/res, PendSV의 S16-S31 저장+복원
print(f"extra words moved per switch (FP->FP): {extra_words} -> roughly {extra_words}+ cycles at 1 word/cycle")
for name, ns in [("dot_c", 10720), ("dot_smlad", 6592), ("dot_f", 10625), ("m55 scalar", 7066), ("m55 mve", 1178)]:
    print(f"{name:10s} {ns/32:6.0f} instr/call  ({ns/32/64:4.2f} per tap)")
print(f"SMLAD speedup {10720/6592:.2f}x, MVE speedup {7066/1178:.2f}x, soft/hard biquad {12792/864:.1f}x")
```

```sh
limactl shell elinux -- python3 /Users/donh/.rtos_scratch/B6/py/ex15.py
```

```text
basic(FPCA=0)          median= 1379 ns  spread= 98 ns
EXT lazy, no FP        median= 1378 ns  spread=169 ns
EXT lazy, FP           median= 2769 ns  spread=139 ns
EXT immediate, noFP    median= 2669 ns  spread= 26 ns
EXT immediate, FP      median= 2632 ns  spread= 98 ns
saved context: int task 68 B, FP task 204 B, extra 136 B per task
extra words moved per switch (FP->FP): 68 -> roughly 68+ cycles at 1 word/cycle
dot_c         335 instr/call  (5.23 per tap)
dot_smlad     206 instr/call  (3.22 per tap)
dot_f         332 instr/call  (5.19 per tap)
m55 scalar    221 instr/call  (3.45 per tap)
m55 mve        37 instr/call  (0.58 per tap)
SMLAD speedup 1.63x, MVE speedup 6.00x, soft/hard biquad 14.8x
```

출력에서 볼 것: 호스트 시간 측정은 5회 퍼짐이 26~169 ns로 중앙값 차이(약 1.3 µs)보다 훨씬 작아서, "FP 레지스터를 실제로 옮길 때만 느려진다"는 결론은 안정적이다. 나머지 줄은 아래 두 차트의 숫자다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">측정: FP를 쓰면 스택이 얼마나 늘어나나 (bytes)</text>
<text x="192.0" y="60.0" font-size="12" text-anchor="end">저장 문맥, 정수 태스크</text>
<rect x="200.0" y="45.0" width="34.0" height="22.0" rx="2" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="240.0" y="60.0" font-size="12" text-anchor="start">68</text>
<text x="192.0" y="94.0" font-size="12" text-anchor="end">저장 문맥, FP 태스크</text>
<rect x="200.0" y="79.0" width="102.0" height="22.0" rx="2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="308.0" y="94.0" font-size="12" text-anchor="start">204</text>
<text x="192.0" y="128.0" font-size="12" text-anchor="end">HWM 사용량, 정수 태스크</text>
<rect x="200.0" y="113.0" width="20.0" height="22.0" rx="2" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="226.0" y="128.0" font-size="12" text-anchor="start">40</text>
<text x="192.0" y="162.0" font-size="12" text-anchor="end">HWM 사용량, FP 태스크</text>
<rect x="200.0" y="147.0" width="88.0" height="22.0" rx="2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="294.0" y="162.0" font-size="12" text-anchor="start">176</text>
<text x="192.0" y="196.0" font-size="12" text-anchor="end">snprintf %d</text>
<rect x="200.0" y="181.0" width="242.0" height="22.0" rx="2" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="448.0" y="196.0" font-size="12" text-anchor="start">484</text>
<text x="192.0" y="230.0" font-size="12" text-anchor="end">snprintf %f</text>
<rect x="200.0" y="215.0" width="392.0" height="22.0" rx="2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="598.0" y="230.0" font-size="12" text-anchor="start">784</text>
<text x="192.0" y="264.0" font-size="12" text-anchor="end">snprintf %.3e</text>
<rect x="200.0" y="249.0" width="352.0" height="22.0" rx="2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="558.0" y="264.0" font-size="12" text-anchor="start">704</text>
<line x1="200.0" y1="40.0" x2="200.0" y2="290.0" stroke="currentColor" stroke-width="1.2"/>
<line x1="200.0" y1="290.0" x2="200.0" y2="295.0" stroke="currentColor" stroke-width="1.2"/>
<text x="200.0" y="310.0" font-size="12" text-anchor="middle">0</text>
<line x1="300.0" y1="290.0" x2="300.0" y2="295.0" stroke="currentColor" stroke-width="1.2"/>
<text x="300.0" y="310.0" font-size="12" text-anchor="middle">200</text>
<line x1="400.0" y1="290.0" x2="400.0" y2="295.0" stroke="currentColor" stroke-width="1.2"/>
<text x="400.0" y="310.0" font-size="12" text-anchor="middle">400</text>
<line x1="500.0" y1="290.0" x2="500.0" y2="295.0" stroke="currentColor" stroke-width="1.2"/>
<text x="500.0" y="310.0" font-size="12" text-anchor="middle">600</text>
<line x1="600.0" y1="290.0" x2="600.0" y2="295.0" stroke="currentColor" stroke-width="1.2"/>
<text x="600.0" y="310.0" font-size="12" text-anchor="middle">800</text>
<text x="200.0" y="326.0" font-size="12" text-anchor="start">ex5(ARM_CM4F, ICOUNT) · ex8(newlib-nano + -u _printf_float)</text>
</svg>
```

그림 7 — FP 사용이 스택에 주는 비용(실측). 저장 문맥은 태스크 스택을 직접 읽어 계산(예제 7), HWM은 `uxTaskGetStackHighWaterMark()`(r4–r11 채움 패턴 때문에 32 B 낙관적), snprintf는 예제 10.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">측정: 같은 계산의 비용 (QEMU icount, ns — 작을수록 빠름)</text>
<text x="10.0" y="54.0" font-size="13" text-anchor="start" font-weight="bold">64탭 Q15 dot product (호출 1번)</text>
<text x="202.0" y="78.0" font-size="12" text-anchor="end">M4 plain C (SMLABB)</text>
<rect x="210.0" y="64.0" width="348.4" height="20.0" rx="2" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="564.4" y="78.0" font-size="12" text-anchor="start">10720</text>
<text x="202.0" y="106.0" font-size="12" text-anchor="end">M4 __smlad (2쌍/명령)</text>
<rect x="210.0" y="92.0" width="214.2" height="20.0" rx="2" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="430.2" y="106.0" font-size="12" text-anchor="start">6592</text>
<text x="202.0" y="134.0" font-size="12" text-anchor="end">M4 float FPU (VFMA)</text>
<rect x="210.0" y="120.0" width="345.3" height="20.0" rx="2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="561.3" y="134.0" font-size="12" text-anchor="start">10625</text>
<text x="202.0" y="162.0" font-size="12" text-anchor="end">M55 scalar (LOB)</text>
<rect x="210.0" y="148.0" width="229.6" height="20.0" rx="2" fill="#888" fill-opacity="0.5" stroke="#888"/>
<text x="445.6" y="162.0" font-size="12" text-anchor="start">7066</text>
<text x="202.0" y="190.0" font-size="12" text-anchor="end">M55 Helium (VMLAVA)</text>
<rect x="210.0" y="176.0" width="38.3" height="20.0" rx="2" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="254.3" y="190.0" font-size="12" text-anchor="start">1178</text>
<text x="10.0" y="232.0" font-size="13" text-anchor="start" font-weight="bold">biquad 1샘플</text>
<text x="202.0" y="256.0" font-size="12" text-anchor="end">-mfloat-abi=soft</text>
<rect x="210.0" y="242.0" width="383.8" height="20.0" rx="2" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="599.8" y="256.0" font-size="12" text-anchor="start">12792</text>
<text x="202.0" y="284.0" font-size="12" text-anchor="end">-mfloat-abi=softfp</text>
<rect x="210.0" y="270.0" width="26.9" height="20.0" rx="2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="242.9" y="284.0" font-size="12" text-anchor="start">896</text>
<text x="202.0" y="312.0" font-size="12" text-anchor="end">-mfloat-abi=hard</text>
<rect x="210.0" y="298.0" width="25.9" height="20.0" rx="2" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="241.9" y="312.0" font-size="12" text-anchor="start">864</text>
<text x="10.0" y="390.0" font-size="12" text-anchor="start">icount: 명령 1개 = 32 ns. 실칩 사이클(파이프라인 · beat 겹침 · 메모리 대기)과 다르다</text>
</svg>
```

그림 8 — 같은 계산의 비용(QEMU icount, ns). DSP 명령은 1.6배, Helium은 6배(명령 수 기준), FPU가 있는 칩에서 soft float은 15배 손해다.

### 11.1 숫자로 정리한 설계 규칙

| 항목 | 측정값 (이 노트) | 설계 규칙 |
|---|---|---|
| 확장 예외 프레임 | 104 B (기본 32 B) | FP 문맥에서의 인터럽트 한 번 = 스택 104 B(+4) 예약 |
| FP 태스크 저장 문맥 | 204 B (정수 68 B) | FP 쓰는 태스크마다 스택 +136 B |
| "FP 한 번" 태스크 | 영원히 204 B | FP는 몇몇 태스크에 몰아 둔다 |
| snprintf `%f` | 스택 +300 B, FLASH +14.5 KB, FP 태스크화 | 로그는 고정소수점으로 |
| Zephyr `struct k_thread` | +64 B (FPU 켜면 모든 스레드) | 스레드 수가 많으면 RAM 예산에 반영 |
| lazy vs 즉시 | icount 차이 0, 호스트 시간 2배 | 실칩 비용은 DWT/스코프로 잴 것 |
| SMLAD / Helium | 1.63배 / 6배 (명령 수) | 핫 루프는 CMSIS-DSP · intrinsic |
| soft vs hard ABI | 14.8배 | FPU 칩은 hard, 라이브러리 ABI 통일 |

---

## 12. RTOS 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">PendSV에서 FP 문맥을 다루는 길 — FreeRTOS와 Zephyr</text>
<rect x="30.0" y="40.0" width="400.0" height="36.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="230.0" y="62.2" font-size="12" text-anchor="middle">PendSV 진입 (lr = 나가는 태스크의 EXC_RETURN)</text>
<line x1="230.0" y1="76.0" x2="230.0" y2="86.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<rect x="30.0" y="86.0" width="400.0" height="36.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="230.0" y="108.2" font-size="12" text-anchor="middle">tst lr, #0x10 → bit4 = 0 이면 FP 태스크</text>
<line x1="230.0" y1="122.0" x2="230.0" y2="132.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<rect x="30.0" y="132.0" width="400.0" height="36.0" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="230.0" y="154.2" font-size="12" text-anchor="middle">vstmdb s16–s31 (lazy면 여기서 S0–S15도 기록)</text>
<line x1="230.0" y1="168.0" x2="230.0" y2="178.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<rect x="30.0" y="178.0" width="400.0" height="36.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="230.0" y="200.2" font-size="12" text-anchor="middle">stmdb r4–r11, lr → TCB에 SP 저장</text>
<line x1="230.0" y1="214.0" x2="230.0" y2="224.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<rect x="30.0" y="224.0" width="400.0" height="36.0" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="230.0" y="246.2" font-size="12" text-anchor="middle">vTaskSwitchContext() / Zephyr: ready_q.cache 읽기</text>
<line x1="230.0" y1="260.0" x2="230.0" y2="270.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<rect x="30.0" y="270.0" width="400.0" height="36.0" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="230.0" y="292.2" font-size="12" text-anchor="middle">ldmia r4–r11, lr → tst lr, #0x10 → vldmia s16–s31</text>
<line x1="230.0" y1="306.0" x2="230.0" y2="316.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<rect x="30.0" y="316.0" width="400.0" height="36.0" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="230.0" y="338.2" font-size="12" text-anchor="middle">bx lr → 하드웨어가 8 또는 26워드 복원</text>
<rect x="450.0" y="120.0" width="220.0" height="110.0" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="560.0" y="155.2" font-size="12" text-anchor="middle" font-weight="bold">Zephyr 차이</text>
<text x="560.0" y="171.2" font-size="12" text-anchor="middle">S16–S31을 스택이 아닌</text>
<text x="560.0" y="187.2" font-size="12" text-anchor="middle">thread-&gt;arch.preempt_float</text>
<text x="560.0" y="203.2" font-size="12" text-anchor="middle">(struct k_thread 안)</text>
<text x="450.0" y="260.0" font-size="12" text-anchor="start">FreeRTOS: 전부 태스크 스택</text>
<text x="450.0" y="280.0" font-size="12" text-anchor="start">Zephyr: 호출자 저장분만 스택</text>
</svg>
```

그림 9 — PendSV의 FP 처리 경로. FreeRTOS와 Zephyr 모두 EXC_RETURN bit 4로 판단하고, 저장 위치만 다르다.

### FreeRTOS V11.3.2

| 위치 | 함수 · 매크로 | 하는 일 |
|---|---|---|
| `portable/GCC/ARM_CM4F/port.c` | `#ifndef __ARM_FP` → `#error` | FP 없는 빌드 거부 |
| 같은 파일 | `pxPortInitialiseStack()` | 새 태스크 스택에 `portINITIAL_EXC_RETURN = 0xfffffffd` (FP 없음으로 시작) |
| 같은 파일 | `xPortStartScheduler()` | `vPortEnableVFP()`로 CPACR 열기, FPCCR에 `portASPEN_AND_LSPEN_BITS`(3 << 30)를 OR |
| 같은 파일 | `prvPortStartFirstTask()` | MSP 리셋, `msr control, 0`(FPCA 지움), `svc 0` |
| 같은 파일 | `vPortSVCHandler()` | 첫 태스크 복원: `ldmia r0!, {r4-r11, r14}` → `bx r14` |
| 같은 파일 | `xPortPendSVHandler()` | `tst r14, #0x10` → `vstmdbeq` / `vldmiaeq {s16-s31}` |
| `portable/GCC/ARM_CM33/non_secure/portmacrocommon.h` | `configENABLE_FPU` | v8-M 포트는 FPU 사용을 명시하도록 강제(`#error`) |
| `portable/GCC/ARM_CM3/port.c` | (FP 처리 없음) | FPU 칩에 쓰면 예제 9의 사고 |

FreeRTOS에는 Cortex-A/R 포트의 `portTASK_USES_FLOATING_POINT()`(태스크가 FP를 쓴다고 선언) 같은 API가 있지만 **ARM_CM4F에는 없다** — Cortex-M은 하드웨어 FPCA가 자동으로 표시하니까. V11.3.2 소스에서 그 매크로는 `ARM_CA9`, `ARM_CR5`, `ARM_CRx_No_GIC`, `ARM_AARCH64` 등의 `portmacro.h`에만 있다.

### Zephyr v4.4.2

| 위치 | 이름 | 하는 일 |
|---|---|---|
| `arch/arm/core/cortex_m/prep_c.c` | `z_arm_floating_point_init()` | CPACR 지우고 열기, FPCCR(공유: ASPEN+LSPEN / 비공유: 둘 다 끔), DSB · ISB, FPSCR 초기화(v8.1-M은 LTPSIZE = 4) |
| `arch/arm/core/cortex_m/swap_helper.S` | `z_arm_pendsv` | `tst lr, #_EXC_RETURN_FTYPE_Msk` → `vstmia`/`vldmia {s16-s31}`를 `thread->arch.preempt_float`로, FPCA 지우기 |
| `include/zephyr/arch/arm/thread.h` | `struct _preempt_float` | s16–s31 저장 공간 (k_thread 안) |
| `arch/arm/core/cortex_m/thread.c` | `arch_float_disable()` → `k_float_disable()` | 스레드의 FP 문맥 저장 끄기 |
| 같은 파일 | `MPU_GUARD_ALIGN_AND_SIZE_FLOAT` | FP 스레드의 MPU 스택 가드를 크게(lazy 저장 넘침 대비) |
| `arch/arm/core/Kconfig` | `FP_HARDABI` / `FP_SOFTABI` → `select FPU_SHARING` | Cortex-M에서 FPU 공유를 사실상 강제 (예제 11) |
| `include/zephyr/kernel.h` | `K_FP_REGS` | 스레드 생성 옵션: FP 사용 미리 표시 |

PendSV 어셈블리 전체를 한 줄씩 읽는 것은 C2, 태스크 생성 때 초기 프레임을 위조하는 것은 C3, MPU 스택 가드는 F 모듈에서 다룬다.

---

## 13. 실무에서 다시 보기

### 13.1 드론 비행 제어기 (STM32H7, Cortex-M7 + 배정밀 FPU)

- 자세 추정(EKF · 상보 필터)과 rate PID는 float이다. IMU 데이터 준비 인터럽트(1~8 kHz)에서 **ISR은 데이터만 DMA 버퍼에서 꺼내 태스크를 깨우고**, 계산은 높은 우선순위의 제어 태스크가 한다 — FP를 쓰는 문맥을 제어 태스크 하나로 모으면 다른 태스크(텔레메트리, 로깅, 파라미터 저장)는 68 B 정수 태스크로 남는다.
- 반대로 Betaflight처럼 RTOS 없이 루프를 도는 FC는 컨텍스트 스위치 비용이 없지만, ISR에서 float 필터를 돌리면 **lazy 저장이 그 ISR마다 터진다**. 그래서 "ISR에서 float을 쓰면 그 ISR의 진입은 FP 없는 칩보다 17워드+ 느리다"를 지연 예산에 넣는다.
- M7의 FPv5는 S0–S31 대신 D0–D15 배정밀을 지원하지만 **프레임 모양(26워드)은 같다**(배정밀 레지스터 D0–D7 = S0–S15 저장소). Cortex-M7의 캐시가 lazy 저장 위치(FPCAR 스택)와 상호작용하는 문제는 B8 범위다.

### 13.2 로봇 관절 모터 컨트롤러 (FOC 20 kHz, M4F)

- 전류 루프(Clarke/Park 변환, PI, SVPWM)는 **ADC 변환 완료 ISR 안에서** 돈다. 50 µs 주기에 태스크 전환을 넣지 않는다. 이 ISR이 float을 쓰면 lazy 저장이 매번 일어나므로, **그 ISR이 끼어드는 스레드가 FP를 쓰지 않게 하면**(FPCA = 0) 저장 자체가 없다.
- 고정소수점 FOC(Q15 + `__ssat`)는 여전히 흔하다. 예제 12처럼 포화를 빼먹으면 듀티 사이클 계산이 부호를 뒤집는다 — 하드웨어를 태우는 버그다. PWM 비교값은 항상 `__ssat` / `__usat`로 자른다.

```c
/* FOC 전류 ISR의 마지막 단계 — 듀티를 Q15에서 포화시켜 타이머 비교값으로 (빌드만 함: M4F -O2, 경고 0) */
#include <stdint.h>
#include <arm_acle.h>
uint16_t duty_q15_to_ccr(int32_t duty_q15, uint16_t arr)
{
    int32_t d = __ssat(duty_q15, 16);              /* -32768..32767로 자르기 */
    uint32_t u = (uint32_t)(d + 32768);            /* 0..65535 */
    return (uint16_t)((u * arr) >> 16);
}
```

```text
00000000 <duty_q15_to_ccr>:
   0:	ssat	r0, #16, r0
   4:	add.w	r0, r0, #32768	@ 0x8000
   8:	mul.w	r0, r1, r0
   c:	lsrs	r0, r0, #16
   e:	bx	lr
```

포화가 명령 하나(`ssat`)로 끝난다. 분기가 없으니 ISR의 실행 시간도 입력과 무관하게 일정하다.

### 13.3 Linux SoC 옆의 센서 허브 (Zephyr, M33/M4F)

- Zephyr에서 FPU를 켜면 **모든 스레드의 `struct k_thread`가 64 B 커진다**(예제 11). 스레드가 20개면 1.3 KB — 작은 센서 허브 MCU에서는 눈에 띈다.
- 센서 퓨전 스레드만 FP를 쓰게 하고, 실수로 FP가 번진 스레드는 `k_float_disable()`로 되돌릴 수 있다. 로그(`LOG_INF("%f")`)는 Zephyr에서도 FP를 번지게 하는 1순위다.

### 13.4 Don의 SSD 컨트롤러

- SSD 컨트롤러의 Cortex-R/M 코어는 대개 FPU를 쓰지 않거나 아예 없는 구성이다. 펌웨어가 정수 · 비트 연산만 쓰니 문맥 저장은 정수 레지스터뿐이다. 성능 통계(지연 히스토그램, 웨어 레벨링 지표)는 고정소수점으로 계산했을 것이다 — 이 노트의 Q 형식 · 포화 연산 감각이 그대로 통한다.
- 면접에서 "FPU를 써 본 적 있나?"에 Don이 할 수 있는 정직한 답: "양산 SSD 펌웨어는 정수 전용이라 FP 문맥을 다룰 일이 없었다. 대신 Cortex-M4F에서 확장 프레임과 lazy stacking을 직접 덤프해 보고 RTOS 포트가 S16–S31을 어떻게 저장하는지 확인했다."

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| CPACR을 안 열었다 / `main()`에서 늦게 열었다 | 부팅 직후 HardFault, `CFSR=0x00080000`, `HFSR=0x40000000` | NOCP: FPU 꺼진 채 FP 명령(프롤로그의 `vpush` 포함) | Reset_Handler / `SystemInit`에서 CPACR bit 20–23을 1로 쓰고 DSB · ISB, 그다음 C 런타임 |
| M4F용 바이너리를 M3/M0+ 보드에 올림 | 같은 NOCP fault, PC가 첫 FP 명령 | 칩에 FPU 없음 | `-mcpu`와 `-mfloat-abi`를 보드에 맞춘다 |
| FPU 칩에 ARM_CM3 포트 사용 | 가끔 틀린 float 결과, 랜덤 HardFault, lockup | 포트가 EXC_RETURN · S16–S31을 저장 안 함 | ARM_CM4F(M4F/M7) 또는 `configENABLE_FPU=1` 포트 |
| FPCCR을 0으로 만들어 "빠르게" 함 | ISR 뒤 스레드의 float 값이 조용히 바뀜 | 하드웨어가 S0–S15를 저장 안 함 (예제 4 E) | ASPEN · LSPEN은 리셋값(1)으로 둔다 |
| FP 태스크 스택을 정수 기준으로 잡음 | 스택 오버플로 훅, 이웃 메모리 손상 | 전환당 204 B, 인터럽트당 104 B 예약 | FP 태스크는 +136 B 이상, HWM으로 검증(32 B 여유 더) |
| 로그에 `printf("%f")` | 아무것도 안 찍힘(nano) 또는 스택 +300 B | `_printf_float` 미링크 / 링크 시 큰 스택 · FP 태스크화 | 고정소수점 출력, 로그 전용 태스크 |
| ISR에서 float 필터 | ISR 지연이 FP 없는 칩보다 커짐(실칩) | lazy 저장이 그 ISR마다 터짐 | 계산은 태스크로, 또는 그 ISR이 끼어드는 문맥의 FP 사용을 없앤다 |
| Q15 결과를 `(int16_t)`로 자름 | 큰 입력에서 부호가 뒤집힌 출력 | wrap-around | `__ssat(x, 16)`, 누산은 `__qadd` 또는 64비트(`__smlald`) |
| soft ABI 라이브러리에 맞추느라 전체를 soft로 | float 코드가 10배 이상 느림 | libgcc 소프트 float | 라이브러리를 hard로 다시 빌드하거나 softfp |
| v8.1-M에서 `vmsr fpscr, rX`로 통째로 씀 | `dls`/`le` 루프에서 INVSTATE (`CFSR=0x20000`) | FPSCR.LTPSIZE를 4가 아닌 값으로 덮음 | 읽고-고치고-쓰기, LTPSIZE = 4 유지 |

---

## 15. 면접에서 이렇게 말한다

**Q.** What happens on exception entry on a Cortex-M4F when the interrupted code is using the FPU?

**A.** CONTROL.FPCA가 1이면 하드웨어는 정수 8워드에 S0–S15 · FPSCR · 예약 1워드를 더한 26워드 확장 프레임을 쌓고, EXC_RETURN의 bit 4를 0으로 둔다(E1/E9/ED). 기본값인 lazy stacking이면 FP 부분은 자리만 잡고 FPCAR에 주소를 적었다가, ISR이 FP 명령을 실행할 때 저장한다. 실제로 ISR에서 프레임을 덤프해 SP가 104 B 내려가고, 첫 FP 명령 전후로 S0 칸이 옛 값에서 실제 값으로 바뀌는 걸 확인했다.

> If FPCA is set, the core pushes an extended 26-word frame: the usual 8 integer words plus S0–S15, FPSCR and a reserved word, and EXC_RETURN bit 4 is cleared, so you see 0xFFFFFFE9 or 0xFFFFFFED. With lazy stacking, which is the reset default, the FP part is only reserved; FPCAR records where it is and LSPACT is set. The registers are actually written only if the handler executes an FP instruction. I verified this by dumping the frame in an ISR: the stack pointer dropped by 104 bytes, and the S0 slot held stale data until the first FP instruction in the handler.

**꼬리 질문**: "Why is it 26 words, not 25?" → 8바이트 정렬을 맞추려는 예약 워드이고, Armv8.1-M MVE 코어는 그 자리에 VPR을 저장한다(M55에서 실측).

**Q.** What is lazy stacking and why does it matter for interrupt latency?

**A.** 대부분의 ISR은 FP를 안 쓰므로, 진입 때 FP 17워드를 쓰지 않고 공간만 예약한다. FP를 안 쓰는 ISR은 FPU가 없는 칩과 같은 진입 비용을 내고, 쓰는 ISR만 첫 FP 명령에서 저장 비용을 낸다. 주의할 점은 공간은 항상 104 B 예약된다는 것, 그리고 미뤄진 저장이 ISR 안에서 원래 문맥의 스택으로 쓰이므로 MPU 가드에 걸리면 ISR 안에서 MemManage가 난다는 것이다.

> Lazy stacking reserves stack space for the FP caller-saved registers but defers writing them until the handler actually uses the FPU. Most handlers don't, so they keep the normal integer-only entry cost. The space is always reserved, though, so stack budgets must assume 104 bytes per exception, and the deferred write targets the interrupted context's stack, which can fault inside the ISR if that stack hits an MPU guard. One more thing: QEMU doesn't model this timing at all, so I'd measure it on silicon with the DWT cycle counter.

**Q.** How does FreeRTOS handle the FPU on Cortex-M4F, and what does it cost per task?

**A.** ARM_CM4F 포트는 각 태스크의 EXC_RETURN을 r4–r11과 함께 저장하고, PendSV에서 `tst r14, #0x10`으로 bit 4를 보고 FP 태스크일 때만 S16–S31을 `vstmdb`/`vldmia`한다. S0–S15 · FPSCR은 하드웨어 확장 프레임에 이미 있다. 그래서 정수 태스크는 68 B, FP 태스크는 204 B를 저장한다 — 직접 스택을 읽어서 +136 B를 확인했다. 그리고 FP를 한 번이라도 쓴 태스크는 계속 FP 태스크로 남는다.

> The CM4F port saves each task's EXC_RETURN along with R4–R11, and in PendSV it tests bit 4. Only tasks with an active FP context get S16–S31 pushed and popped; S0–S15 and FPSCR are already in the hardware frame. Measured on QEMU, an integer task's saved context is 68 bytes and an FP task's is 204, so about 136 extra bytes per FP task. And once a task executes a single FP instruction, it stays an FP task for every subsequent switch, which is why I keep floating point confined to a few tasks.

**꼬리 질문**: "Does Zephyr do the same?" → 같은 bit 4 판단, 하지만 S16–S31을 `struct k_thread`의 `preempt_float`에 저장한다. 그래서 FPU를 켜면 모든 스레드 구조체가 64 B 커진다.

**Q.** Right after boot you get a HardFault with CFSR = 0x00080000. What's wrong?

**A.** CFSR bit 19 = NOCP, FPU가 꺼진 채 FP 명령을 실행했다. HFSR FORCED가 같이 켜져 있으면 UsageFault가 꺼져 있어 HardFault로 승격된 것이다. 보통은 CPACR을 열지 않았거나, `main()` 안에서 열어서 `main()` 프롤로그의 `vpush`가 먼저 실행된 경우다. 또는 M4F용 바이너리를 FPU 없는 칩에 올렸다.

> Bit 19 of CFSR is NOCP: an FP instruction ran while the coprocessor was disabled, and since UsageFault wasn't enabled it escalated, so HFSR shows FORCED. The usual causes are not setting CP10 and CP11 in CPACR, or doing it too late, inside main, when the compiler has already emitted a vpush in main's prologue. I'd move the enable into the reset handler before the C runtime init, followed by DSB and ISB, and check that the part actually has an FPU.

**Q.** Can you use floating point inside an ISR?

**A.** Cortex-M4F에서는 하드웨어가 S0–S15를 저장해 주므로 기능적으로는 안전하다(FreeRTOS CM4F, Zephyr 공유 모드). 비용은 그 ISR이 FP를 쓸 때마다 lazy 저장이 일어나 진입이 17워드 이상 늘어난다는 것, ISR 스택에도 확장 프레임이 쌓인다는 것이다. 하드웨어 자동 저장을 끈 구성(FPCCR = 0, Zephyr unshared 모드)에서는 조용히 값이 깨진다. 실무에서는 빠른 ISR은 데이터만 옮기고 계산은 태스크로 넘기되, 20 kHz FOC처럼 ISR이 곧 제어 루프인 경우는 그 비용을 예산에 넣고 쓴다.

> Functionally yes on an M4F with automatic state preservation enabled, because the hardware saves S0–S15 and FPSCR. The cost is that each such ISR triggers the lazy save, roughly 17 extra words on entry and on exit, and the ISR stack grows by the extended frame. If someone has disabled automatic preservation, it silently corrupts the interrupted thread's registers; I reproduced exactly that on QEMU. In practice I keep fast ISRs integer-only and defer math to a task, except for things like a 20 kHz FOC loop where the ISR is the control loop and I budget for it.

**Q.** After porting from an M3 to an M4F, two tasks occasionally get wrong float results. How do you debug it?

**A.** 먼저 RTOS 포트가 FP를 아는지(ARM_CM3를 그대로 쓰지 않았는지) 확인한다. 실제로 ARM_CM3 포트로 FP 태스크 둘을 돌리니 한 태스크가 죽고 lockup이 났다. 다음으로 FPCCR이 리셋값(ASPEN · LSPEN = 1)인지, 누가 FPCA를 지우거나 FPCCR을 0으로 쓰는지 본다. 마지막으로 스택: FP 태스크는 전환마다 204 B, 인터럽트마다 104 B가 필요하니 오버플로가 이웃 태스크를 덮지 않는지 HWM과 overflow hook으로 확인한다.

> First I check the port: if it's still the CM3 port, nothing saves S16–S31 or tracks per-task EXC_RETURN; I reproduced that and got corrupted tasks and a lockup. Then I check FPCCR is still at its reset value with ASPEN and LSPEN set and that nothing clears CONTROL.FPCA by hand. Finally stack sizes, because FP tasks need about 136 more bytes per switch, so I'd look at high-water marks with some margin, enable the overflow hook, and confirm that the saved EXC_RETURN of the failing tasks is 0xFFFFFFED.

**Q.** When would you use fixed-point DSP instructions instead of the FPU?

**A.** FPU가 없는 코어, 또는 처리량이 중요한 핫 루프다. M4에서 Q15 내적을 `__smlad`로 하면 두 쌍을 한 명령에 처리해 plain C보다 1.6배 적은 명령, 메모리 접근은 절반이다. 그리고 포화(`SSAT`, `QADD`)가 명령 하나라서 제어 출력이 넘쳐도 부호가 뒤집히지 않는다. 반대로 동적 범위가 크거나 개발 속도가 중요한 알고리즘(EKF 같은)은 float이 낫다. Helium이 있는 M55라면 벡터화된 Q15나 float 모두 크게 빨라진다.

> On cores without an FPU, or for hot loops where throughput matters. On an M4, a Q15 dot product with SMLAD processes two pairs per instruction; I measured about 1.6 times fewer instructions than plain C and half the loads. Saturation is a single instruction, so overflow clamps instead of wrapping, which matters for PWM duty or current commands. For algorithms with large dynamic range, like an EKF, float is safer and faster to develop. On Helium cores both get vectorized; on an M55 model I saw about six times fewer instructions with MVE.

**꼬리 질문**: "What's a beat?" → MVE 명령 하나를 32비트 조각 4개로 나눈 단위다. M55는 사이클당 2 beat(dual-beat)이고, 앞 명령의 뒤쪽 beat와 다음 명령의 앞쪽 beat가 겹쳐 실행된다. 그래서 128비트 연산을 작은 데이터 경로로 처리한다.

---

## 16. 직접 해보기

1. **개념**: FPCCR = `0x80000000`(ASPEN = 1, LSPEN = 0)일 때 FP를 쓰는 스레드에 인터럽트가 들어오면 SP는 몇 바이트 내려가고, 그 순간 프레임의 S0 칸에는 무엇이 있나?
   정답/힌트: 104 B(+정렬 4 B 가능), S0 칸에는 이미 실제 S0 값 — 예제 4의 B줄.
2. **개념**: FreeRTOS ARM_CM4F에서 태스크 A(FP)가 태스크 B(정수)로 전환될 때, PendSV 진입부터 B의 첫 명령까지 FP 관련으로 메모리에 쓰이는 워드 수는?
   정답/힌트: A 쪽: `vstmdb`가 터뜨리는 lazy 저장 17워드(S0–S15 · FPSCR, 자리는 진입 때 이미 예약) + `vstmdb` 자체 16워드. B 쪽: `vldmia` 없음, 기본 프레임으로 복귀. FP 관련 쓰기 33워드, 읽기 0.
3. **코드**: 예제 3을 고쳐서, 태스크별로 "이 태스크는 FP 금지"를 강제하는 감시 장치를 만든다. FreeRTOS `traceTASK_SWITCHED_IN()` 훅에서 태스크 태그에 따라 CPACR을 열고 닫고, NOCP가 나면 태스크 이름을 출력한다.
   정답/힌트: `vTaskSetApplicationTaskTag()` / `xTaskGetApplicationTaskTag()`, 훅 안에서 CPACR 쓰기 뒤 DSB · ISB. 주의: 닫힌 태스크가 FP 문맥(FPCA = 1)으로 이미 전환된 상태라면 PendSV의 `vldmia`가 fault를 낸다 — 닫는 것은 "아직 FP를 안 쓴 태스크"에만.
4. **측정**: 예제 7에 `t_once`와 같은 태스크를 다섯 개 더 만들어 heap_4의 `xPortGetFreeHeapSize()`와 각 HWM을 기록하고, "FP 한 번"이 RAM 예산에 주는 영향을 표로 만든다.
   정답/힌트: 태스크당 저장 문맥 +136 B. 스택은 생성 때 고정이므로 heap 사용량은 같고, 여유(HWM)가 줄어든다.
5. **측정**: 예제 12의 `dot_smlad`를 4쌍씩 펼치고(누산기 2개) icount 시간을 다시 잰다. 그리고 `__smlald`(64비트 누산)로 바꾼 버전과 비교한다.
   정답/힌트: 루프 오버헤드(`adds`/`cmp`/`b`)가 줄어 명령 수가 더 준다. `__smlald`는 누산기 넘침이 없지만 레지스터 2개를 쓴다.
6. **코드**: 예제 14를 float32 버전(`vfmaq_f32` 또는 `-O3` 자동 벡터화)으로 바꿔 M55에서 돌리고, Q15 MVE · float MVE · scalar float의 명령 수를 비교한다. FPU가 MVE와 레지스터를 공유한다는 점이 컨텍스트 저장에 어떤 의미인지 한 문장으로 쓴다.
   정답/힌트: Q 레지스터가 S 레지스터와 같은 저장소라 컨텍스트 저장 규칙(확장 프레임 + S16–S31)이 그대로이고 추가는 VPR 1워드뿐.
7. **측정(실칩 과제)**: STM32F4 보드가 있다면 GPIO 토글 + 스코프(또는 DWT CYCCNT)로 IRQ 진입 지연을 (a) 스레드가 FP를 안 쓸 때, (b) 쓰고 ISR은 안 쓸 때, (c) 둘 다 쓸 때, (d) LSPEN = 0일 때 재고 예제 5의 QEMU 결과와 비교한다.
   정답/힌트: (a)와 (b)가 같고 (c) · (d)가 커야 lazy stacking 설명이 맞다. QEMU icount는 넷 다 같게 나온다.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| FPU | Floating-Point Unit | Cortex-M4F의 FPv4-SP(단정밀), M7의 FPv5(SP 또는 DP) |
| FPv4-SP-D16 | M4F의 FP 확장 이름 | 단정밀, 64비트 레지스터 16개 = S0–S31 |
| CPACR | Coprocessor Access Control Register (`0xE000ED88`) | CP10 · CP11 필드로 FPU(와 MVE) 접근 허용 |
| DSB / ISB | Data / Instruction Synchronization Barrier | CPACR 쓰기를 끝내고 파이프라인을 새로 채움 |
| NOCP | No Coprocessor UsageFault (`CFSR` bit 19) | FPU가 꺼진 채 FP 명령 실행 |
| FORCED | `HFSR` bit 30 | 꺼진 fault 핸들러 대신 HardFault로 승격됨 |
| CONTROL.FPCA | FP Context Active (CONTROL bit 2) | 1이면 예외 때 확장 프레임 |
| 확장 프레임 | extended exception frame | 26워드 = 104 B: 기본 8 + S0–S15 + FPSCR + 예약/VPR |
| EXC_RETURN bit 4 | FType | 0이면 확장 프레임으로 복귀 (E1/E9/ED) |
| FPCCR | FP Context Control Register (`0xE000EF34`) | ASPEN · LSPEN · LSPACT 등 |
| ASPEN | Automatic State Preservation Enable | FP 명령이 FPCA를 자동으로 켬 |
| LSPEN | Lazy State Preservation Enable | FP 부분을 자리만 예약하고 나중에 저장 |
| LSPACT | Lazy State Preservation Active | 저장이 미뤄진 상태 |
| FPCAR | FP Context Address Register (`0xE000EF38`) | 미뤄진 저장의 목적지(프레임 + 0x20) |
| FPDSCR | FP Default Status Control Register | 예외 진입 때 ISR의 FPSCR 기본값 |
| FPSCR | FP Status and Control Register | 반올림 모드, FZ, DN, 예외 플래그, (v8.1-M) LTPSIZE |
| caller-saved / callee-saved | AAPCS 레지스터 분류 | S0–S15는 호출자, S16–S31은 피호출자가 보존 |
| hard / softfp / soft | `-mfloat-abi` | FP 레지스터로 인자 전달 / 정수 레지스터로 전달 / FPU 안 씀 |
| Q15 / Q31 | 고정소수점 형식 | 16 · 32비트 정수를 `[-1, 1)`로 해석 |
| SMLAD | Signed Multiply Accumulate Dual | 16비트 곱 두 개를 더해 누산 |
| SSAT / QADD | 포화 명령 | n비트 범위로 자르기 / 32비트 포화 덧셈 |
| APSR.Q | sticky saturation flag | 포화가 한 번이라도 일어나면 1 |
| MVE / Helium | M-Profile Vector Extension | Armv8.1-M의 128비트 벡터 확장 |
| beat | MVE 실행 단위 | 128비트 명령의 32비트 조각, 명령당 4개 |
| VPR | Vector Predication Status and Control Register | MVE lane 마스크, 확장 프레임 마지막 워드에 저장 |
| LOB | Low-Overhead Branch extension | `DLS`/`LE`, tail predication판 `DLSTP`/`LETP` |
| LTPSIZE | FPSCR bit 18:16 | 저오버헤드 루프의 predication 원소 크기, 4 = 없음 |
| `CONFIG_FPU_SHARING` | Zephyr Kconfig | 스레드 간 FP 레지스터 공유(문맥 저장) |
| `K_FP_REGS` | Zephyr 스레드 옵션 | FP 사용을 미리 표시 |
| `preempt_float` | Zephyr `struct _preempt_float` | 스레드의 S16–S31 저장 공간 |

---

## 18. 요약 & 체크리스트

FPU는 32개짜리 두 번째 레지스터 파일이고, 정수 레지스터와 같은 규칙으로 나눠 저장된다. S0–S15 · FPSCR은 하드웨어가 확장 프레임(26워드 = 104 B)에 저장하되, 리셋 기본값인 lazy stacking으로 자리만 잡았다가 ISR이 FP를 쓸 때 저장한다. 프레임 모양은 CONTROL.FPCA가 정하고 EXC_RETURN bit 4가 기억한다(E1/E9/ED). S16–S31은 RTOS의 몫이다. FreeRTOS ARM_CM4F는 PendSV에서 `tst r14, #0x10`으로 판단해 태스크 스택에, Zephyr는 `struct k_thread`의 `preempt_float`에 저장한다. 그 결과 FP 태스크는 전환마다 204 B(정수 68 B)를 쓰고, FP를 한 번이라도 쓰면 계속 FP 태스크다. FPU는 시작 코드에서 CPACR + DSB · ISB로 C 런타임보다 먼저 켜야 하고, 잊으면 NOCP(`CFSR=0x00080000`)다. FP를 모르는 포트, 자동 저장 끄기, `printf("%f")`가 고전 버그다. 신호 처리에서는 DSP 명령(SMLAD 1.6배, SSAT · QADD 포화)과 Helium(명령 수 6배 감소, beat 구조, tail predication)이 FPU만큼 중요하다. QEMU icount는 이 모든 스태킹 비용을 0으로 센다 — 실칩 비용은 워드 수로 추정하고 실칩에서 잰다.

- [ ] CPACR의 CP10 · CP11을 열고 DSB · ISB를 치는 코드를, 왜 Reset_Handler에 있어야 하는지와 함께 쓸 수 있다
- [ ] `CFSR=0x00080000` / `0x00020000`을 보고 NOCP / INVSTATE를 바로 말할 수 있다
- [ ] 확장 프레임 26워드의 오프셋(+0x20 S0, +0x60 FPSCR, +0x64 예약/VPR)을 그릴 수 있다
- [ ] EXC_RETURN 0xFFFFFFE1 / E9 / ED / F1 / F9 / FD를 구분한다
- [ ] ASPEN · LSPEN · LSPACT · FPCAR가 lazy stacking에서 각각 무엇을 하는지 순서대로 설명한다
- [ ] FreeRTOS ARM_CM4F PendSV의 `tst r14, #0x10` / `vstmdbeq` / `vldmiaeq`와 태스크별 EXC_RETURN 저장을 설명한다
- [ ] FP 태스크의 추가 비용(+136 B 스택, 전환당 약 68워드 이동)을 숫자로 말한다
- [ ] Zephyr가 S16–S31을 어디에 저장하는지, v4.4.2에서 FPU_SHARING을 끌 수 없었던 이유를 안다
- [ ] ARM_CM3 포트 · FPCCR = 0 · `%f` 로그가 왜 위험한지 증상과 함께 말한다
- [ ] SMLAD · SSAT · QADD의 의미와 Q15 포화가 필요한 이유를 예로 든다
- [ ] Helium의 beat · dual-beat · tail predication을 그림으로 설명한다
- [ ] QEMU icount가 무엇을 못 재는지 말하고, 실칩에서 무엇으로 잴지 안다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM C10 | FPU 켜는 순서 | §1 (CPACR, DSB · ISB, NOCP, 예제 2 · 3) | 벤더 `SystemInit()`이 FPU를 켜는 위치, `__FPU_PRESENT` · `__FPU_USED` 같은 CMSIS 매크로 확인 |
| BM C10 | lazy stacking | §3 · §4 (확장 프레임, FPCCR, 예제 4 · 5) | RTOS 없는 슈퍼 루프에서 "ISR만 FP를 쓰는" 구성의 지연 예산 |
| BM C10 | DSP 명령 (SMLAD · SSAT · QADD) | §8 · §9 (예제 12 · 13) | CMSIS-DSP 라이브러리 링크 · 빌드 옵션, `arm_math.h` 사용법 |
| BM C10 | MVE beat 구조 | §10 (그림 6, 예제 14 · 15) | Helium용 CMSIS-DSP/컴파일러 자동 벡터화 옵션, M55 보드 bring-up |
| BM C4 | 예외 진입 · 8워드 프레임 | §3에서 참조만 (주인: B2) | — |
| BM C11 | Cortex-M 세대 | §10에서 참조만 (주인: B7) | — |

---

## 참고 자료

- Arm, **Application Note 298 — Cortex-M4(F) Lazy Stacking and Context Switching** (DAI0298A). https://developer.arm.com/documentation/dai0298/a (Zephyr `float.rst`가 링크하는 문서)
- Arm, **Armv7-M Architecture Reference Manual** (DDI 0403) — 예외 진입 · 확장 프레임 · FPCCR/FPCAR/FPDSCR · CPACR 정의.
- Arm, **Armv8-M Architecture Reference Manual** (DDI 0553) — Armv8.1-M MVE, VPR, FPSCR.LTPSIZE, 저오버헤드 루프.
- Arm, **Cortex-M4 Devices Generic User Guide** (DUI 0553) — FPU 장(CPACR 켜기 예제 코드, FPCCR 비트).
- Arm, **Cortex-M4 Technical Reference Manual** — 명령 사이클 표(DSP 명령 · VLDM/VSTM).
- Arm, **Arm C Language Extensions (ACLE)** — `arm_acle.h`의 `__smlad` · `__ssat` · `__qadd`, `arm_mve.h`의 MVE intrinsic. https://arm-software.github.io/acle/
- ARM-software **CMSIS-DSP** — Q15/Q31/float DSP 함수, Helium 최적화 경로. https://github.com/ARM-software/CMSIS-DSP
- FreeRTOS Kernel V11.3.2 소스: `portable/GCC/ARM_CM4F/port.c`(`xPortPendSVHandler`, `xPortStartScheduler`, `prvPortStartFirstTask`, `vPortEnableVFP`, `pxPortInitialiseStack`), `portable/GCC/ARM_CM3/port.c`, `portable/GCC/ARM_CM33/non_secure/portmacrocommon.h`(`configENABLE_FPU`).
- Zephyr v4.4.2 소스 · 문서: `doc/kernel/services/other/float.rst`(Floating Point Services, Cortex-M 절), `arch/arm/core/cortex_m/swap_helper.S`(`z_arm_pendsv`), `arch/arm/core/cortex_m/prep_c.c`(`z_arm_floating_point_init`), `arch/arm/core/cortex_m/thread.c`(`arch_float_disable`), `include/zephyr/arch/arm/thread.h`(`struct _preempt_float`), `arch/arm/core/Kconfig`(`FP_HARDABI`/`FP_SOFTABI` → `select FPU_SHARING`), `boards/arm/mps2/mps2_an386_defconfig`.
- QEMU 문서 — Arm MPS2/MPS3 보드(`mps2-an386`, `mps3-an547`). https://www.qemu.org/docs/master/system/arm/mps2.html
- 이 노트의 모든 실행 결과: QEMU 10.2.1(`mps2-an386` Cortex-M4, `mps3-an547` Cortex-M55), arm-none-eabi-gcc 14.2.1, newlib-nano, Lima VM `elinux`. 소스는 `~/.rtos_scratch/B6/`.
