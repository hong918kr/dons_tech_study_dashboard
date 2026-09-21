# J09. Strong proficiency in C and/or C++ in resource-constrained environments

> **분류**: Requirement 2/7 · **관련 개념 노트**: C02(자원 제약 C/C++), C01(부트·링커·툴체인), S01(C 코딩 드릴), C08 §7(메모리 예산)
> **Don 현재 상태**: ✅ 강함 — 레쥬메 근거 "Developed using embedded C and C++ programming on bare-metal (pre/post-silicon)". 갭은 언어가 아니라 **임베디드 C++ 서브셋 규칙을 말로 설명하는 연습**과 **화이트보드에서 손으로 쓰는 속도**
> **이 노트를 다 읽으면**: ① 면접관이 이 한 줄을 검증하는 네 가지 채널(라이브 코딩·코드 리뷰·트릭 질문·메모리/스택 질문)과 각각의 채점 기준을 안다 ② 기초/중급/심화 15문항을 모범 답안 수준으로 말할 수 있다 ③ `-fno-exceptions`, RAII, 템플릿 코드 크기, 정적 초기화 순서 같은 임베디드 C++ 질문에 근거를 대며 답할 수 있다

---

## 0. 문장 뜯어보기

| 구(句) | 표면적 의미 | 채용담당자가 이 단어를 고른 이유 |
|---|---|---|
| `Strong proficiency` | 능숙함 | "안다"가 아니라 "면접에서 손으로 쓴다". 실제 검증은 라이브 코딩 + 코드 리뷰로 이뤄진다 |
| `in C and/or C++` | C 또는 C++ | **양자택일을 허용**한다는 뜻. 팀 코드베이스가 순수 C일 수도, C++ 서브셋일 수도 있다는 신호. C만 해도 감점이 아니지만, C++를 쓴다고 했으면 **임베디드 서브셋 규칙**을 알아야 한다 |
| `resource-constrained` | 자원 제약 | 이 문장의 진짜 무게중심. 언어 문법이 아니라 **메모리·스택·코드 크기·실행 시간 예산을 의식하며 코드를 쓰는가**를 묻는다 |
| `environments` | 환경 | 단수가 아니다. always-on MCU(수백 KB SRAM)와 SoC(Cortex-A, Android) 두 환경이 한 기기 안에 있다는 JD 구조와 맞물린다 |

**같이 읽어야 할 JD 문장**: `Develop and maintain embedded firmware in C/C++ targeting ARM-based SoCs and microcontrollers`(Responsibility 1)와 `support model inference within memory and latency budgets`(Responsibility 5). 즉 "resource-constrained"는 수사가 아니라, **wake word 모델의 tensor arena와 오디오 버퍼와 RTOS 스택이 같은 SRAM을 두고 싸우는 상황**을 가리킨다.

**Requirement 노트의 관점**: 개념 자체는 C02가 교과서로 다룬다. 이 노트는 **면접관이 그 개념을 어떻게 측정하는가**만 다룬다. 같은 주제라도 C02는 "volatile이 무엇을 보장하나", 이 노트는 "volatile 질문에서 떨어지는 사람은 무엇을 빠뜨리나"를 쓴다.

---

## 1. Hark에서 실제로 하게 될 일 (추정)

컨텍스트 2.2·2.7절 기준 구조 추정: Qualcomm SoC(Cortex-A, Android) + 상시 켜진 저전력 MCU(Ambiq Apollo 계열 Cortex-M, RTOS). 이 구조에서 "resource-constrained C/C++"가 실제로 걸리는 지점은 셋이다 [추정].

```
   always-on MCU (수백 KB SRAM)                        SoC (Cortex-A, Android)
  ┌──────────────────────────────────────┐            ┌────────────────────────┐
  │ RTOS 커널 + 태스크 스택 N개          │            │ Linux/Android, 힙 넉넉 │
  │ I2S/PDM 오디오 DMA 더블 버퍼         │ ── IPC ──▶ │ 큰 모델, 앱            │
  │ 링버퍼(센서, 로그)                   │            │                        │
  │ wake word 모델 tensor arena          │            └────────────────────────┘
  │ OTA 스테이징 / 부트로더 영역         │
  └──────────────────────────────────────┘
         ▲ 전부 같은 SRAM을 나눠 쓴다. 합이 넘으면 링크가 실패하거나, 더 나쁘게는 런타임에 스택이 힙을 밟는다.
```

- **예산 협상이 코드 리뷰 안건이 된다.** "tensor arena를 8KB 더 달라"는 요청이 오면, 오디오 버퍼 깊이나 태스크 스택에서 깎아야 한다. 이때 숫자를 댈 수 있어야 한다(`.map`, `-fstack-usage`, high-water mark).
- **malloc이 사실상 금지되거나 강하게 제한된다.** 부팅 시 한 번 할당하고 이후 정적·풀 할당만 쓰는 형태가 흔하다.
- **ISR과 태스크가 데이터를 주고받는 코드를 매일 쓴다.** 오디오 DMA 완료 인터럽트 → 처리 태스크가 이 역할의 기본 일과다.
- **C++를 쓴다면 서브셋이다.** 예외·RTTI·동적 할당·iostream은 보통 꺼져 있고, RAII·`constexpr`·템플릿 일부만 쓴다. 이 경계를 모르면 팀 코드베이스에 맞추기 어렵다.

> Hark가 실제로 C를 쓰는지 C++ 서브셋을 쓰는지는 공개 정보가 없다 [추정]. 면접에서 역질문으로 확인할 가치가 있다: "What's the C++ policy on the MCU side — exceptions off, any STL?"

---

## 2. 핵심 개념 — 면접관은 이 한 줄을 어떻게 측정하나

### 2.1 네 가지 검증 채널

| 채널 | 전형적 형태 | 시간 | 무엇을 보나 | 즉시 탈락 신호 |
|---|---|---|---|---|
| **A. 라이브 코딩** | 공유 편집기나 화이트보드에 링버퍼·풀 할당자·파서 FSM 구현 | 25~45분 | 동작하는 코드를 쓰는가, 경계 조건을 스스로 찾는가, ISR 안전성을 고려하는가 | 컴파일도 안 되는 코드를 "대충 이런 식"이라며 넘김, 엣지 케이스를 묻기 전까지 생각 안 함 |
| **B. 코드 리뷰** | 20~30줄짜리 버그 있는 스니펫을 주고 "뭐가 문제인가" | 10~15분 | 읽는 눈. UB·경쟁 조건·정수 함정을 잡아내는가 | 스타일 지적만 하고 진짜 버그를 못 봄 |
| **C. 개념·트릭 질문** | volatile, static, const, 정수 승격, 정렬, 원자성 | 각 2~5분 | 정확도. "대충 이런 거"인지 "정확히 이것"인지 | 반쯤 맞는 답을 자신 있게 말함(가장 나쁜 신호) |
| **D. 메모리·스택 질문** | "이 태스크 스택 얼마 줄 건가", "`.map`에서 뭘 보나", "256KB를 어떻게 나누나" | 5~15분 | resource-constrained의 핵심. 숫자로 생각하는가 | "충분히 크게 잡는다"로 끝냄 |

**채널 D가 이 JD 문장의 진짜 과녁이다.** A/B/C는 어느 C 면접에나 있지만, `resource-constrained`를 명시한 회사는 D를 반드시 묻는다. 그리고 대부분의 지원자가 D에서 가장 약하다.

### 2.2 면접관 머릿속 채점 루브릭

| 축 | 1점 (탈락) | 3점 (통과) | 5점 (밴드 상단) |
|---|---|---|---|
| 정확성 | 동작 안 함 | 동작함 | 동작 + 엣지 케이스를 먼저 말함 |
| 동시성 안전 | ISR을 생각 안 함 | `volatile`을 붙임 | 어떤 접근이 원자적이고 아닌지 근거로 구분, 아키텍처(ARMv6-M vs ARMv7-M) 차이까지 |
| 메모리 규율 | `malloc` 사용 | 정적 배열 사용 | 예산을 숫자로 제시하고 실패 모드까지 설계 |
| UB 인식 | 모름 | "그건 UB일 수 있다" | 왜 UB인지, 컴파일러가 어떻게 악용하는지 |
| 커뮤니케이션 | 말없이 코딩 | 다 쓰고 설명 | 쓰면서 가정을 소리 내어 말하고 선택지를 비교 |
| 툴 감각 | 없음 | 디버거 언급 | `.map`, `-fstack-usage`, godbolt, UBSan 같은 구체적 수단 |

**한 줄 전략**: Don은 정확성과 동시성·메모리 축에서 이미 강하다. 점수를 잃을 수 있는 곳은 **커뮤니케이션(말하면서 코딩)**과 **툴 이름을 구체적으로 대는 습관**이다. 둘 다 연습으로 해결된다.

### 2.3 "resource-constrained"의 숫자 감각

면접에서 숫자를 말할 수 있어야 한다. 아래는 급(class)별 대략치이며 **제품군마다 다르다**.

| 급 | 코어 | Flash | SRAM | 이 자리에서의 역할 |
|---|---|---|---|---|
| 초저전력 센서 허브 | Cortex-M0+ (ARMv6-M) | 32~256KB | 8~64KB | 폴링·GPIO·간단한 센서 수집 |
| always-on MCU | Cortex-M4F / M33 (ARMv7-M / ARMv8-M) | 512KB~2MB | 128KB~1MB | RTOS + 오디오 + wake word 추론 |
| 애플리케이션 SoC | Cortex-A + Linux/Android | (eMMC/UFS GB급) | DRAM GB급 | 큰 모델, 앱, 네트워크 |

같은 MCU 안에서 SRAM을 나누는 전형적 그림 [예시 — 실제 수치는 제품마다 다름]:

```
  512 KB SRAM
  ├─ RTOS 커널 오브젝트 + 태스크 스택 8개 × 2~8KB      ≈  40 KB
  ├─ I2S 오디오 DMA 더블 버퍼 (16kHz × 16bit × 20ms ×2) ≈   1.3 KB
  ├─ 특징 추출 링버퍼 (log-mel 프레임 히스토리)         ≈  10 KB
  ├─ wake word tensor arena (TFLM)                      ≈  40 KB
  ├─ .data + .bss (드라이버 상태, 설정, 로그 버퍼)      ≈ 120 KB
  ├─ OTA 스테이징 / 셰도우 버퍼                          ≈  64 KB
  └─ 여유(마진)                                          나머지
```

> 위 배분은 구조를 보여 주기 위한 예시다. TFLM tensor arena는 모델과 커널 구현에 따라 수십 KB에서 수백 KB까지 간다(C08 §7). 면접에서는 **"이 숫자들은 모델과 샘플레이트에 따라 달라지고, 제가 정하는 건 배분 규칙과 실패 모드입니다"**로 말하는 게 정확하다.

### 2.4 합격 신호 vs 탈락 신호 (이 요건 한정)

| 상황 | 탈락 신호 | 합격 신호 |
|---|---|---|
| 버퍼가 필요할 때 | `malloc(n)` | 컴파일 타임 상수 크기 정적 배열 + `_Static_assert`로 예산 검증 |
| 공유 변수 | `volatile`만 붙이고 끝 | 접근 폭·정렬·아키텍처로 원자성을 따지고, 아니면 critical section |
| 크기가 애매할 때 | "넉넉히 잡죠" | "`-fstack-usage`로 최악 경로를 보고, 패턴 채우기로 high-water mark를 재겠습니다" |
| 실패할 수 있는 연산 | 에러 무시 | 오버런·오버플로 시 동작을 먼저 정의(드롭? 덮어쓰기? assert?) |
| C++ 기능 | "STL 쓰면 되죠" | "`std::function`과 `std::string`은 힙을 쓰니 제외, `constexpr`과 RAII는 씁니다" |
| 모르는 것 | 추측을 자신 있게 | "이건 아키텍처 의존이라 확인해야 합니다" |

---

## 3. 실무 패턴과 함정

### 3.1 면접관이 특히 노리는 C 함정 열 가지

| # | 함정 | 무엇이 문제인가 | 면접에서 나오는 형태 |
|---|---|---|---|
| 1 | 정수 승격 | `uint8_t`/`uint16_t`가 `int`로 승격되어 부호 있는 오버플로(UB)가 됨 | `uint16_t a=0xFFFF; a*a`의 값은? |
| 2 | 부호 비교 | `int`와 `unsigned` 비교 시 `int`가 unsigned로 변환 | `if (len - 1 >= 0)`이 항상 참인 이유 |
| 3 | 시프트 UB | `1 << 31`은 `int`에서 UB, 음수·폭 이상 시프트도 UB | 31비트 마스크를 어떻게 쓰나 |
| 4 | `char` 부호 | plain `char`의 부호는 구현 정의. ARM AAPCS에서는 **unsigned** | `char c = buf[i]; if (c < 0)`가 x86과 ARM에서 다른 이유 |
| 5 | `volatile` 과신 | 원자성·메모리 배리어·캐시 일관성을 보장하지 않음 | `volatile`만으로 ISR 공유가 안전한가? |
| 6 | 정렬/언얼라인 | ARMv6-M(M0+)은 언얼라인 워드 접근 불가 → HardFault. `packed` 멤버의 주소를 일반 포인터로 넘기면 UB | 패킷 버퍼 오프셋 3에서 `uint32_t`를 읽는 코드 |
| 7 | strict aliasing | 다른 타입 포인터로 같은 메모리 접근은 UB. `-O2`에서 터짐 | `float`를 `uint32_t`로 보려면? |
| 8 | 동적 할당 | 단편화·비결정성·실패 경로 미처리. `_sbrk` 힙이 스택과 충돌 | 왜 `malloc`을 안 쓰나 |
| 9 | 스택 폭주 | 큰 지역 배열, 재귀, VLA, `printf`의 float 포맷 | 이 태스크 스택은 얼마여야 하나 |
| 10 | ISR 안에서 금지된 일 | 블로킹, 긴 임계구역, `printf`, 동적 할당, 비-FromISR API | ISR에서 하면 안 되는 일은? |

각 함정의 원리는 C02가 다룬다(§5 정수 승격, §4 정렬·엔디언, §3 메모리 할당, §1 volatile, §7 원자성). 여기서는 **면접에서 어떻게 드러나는지**만 본다.

### 3.2 코드 리뷰 채널(B) 연습 — 실제로 나오는 스니펫 형태

면접관은 20~30줄에 버그를 2~4개 심는다. 아래는 전형적인 형태다. **읽고 바로 몇 개 보이는지 세어 본다.**

```c
#include <stdint.h>
#include <string.h>

#define RX_SIZE 64

static uint8_t  rx_buf[RX_SIZE];
static uint8_t  rx_head;          /* ISR가 증가 */
static uint8_t  rx_tail;          /* task가 증가 */
static int      rx_count;

void UART_IRQHandler(void)
{
    uint8_t b = (uint8_t)UART->DR;      /* UART는 메모리 맵 레지스터 */
    rx_buf[rx_head++] = b;
    if (rx_head == RX_SIZE) rx_head = 0;
    rx_count++;
}

int uart_read(uint8_t *dst, int n)
{
    int got = 0;
    while (rx_count > 0 && got < n) {
        dst[got++] = rx_buf[rx_tail++];
        if (rx_tail == RX_SIZE) rx_tail = 0;
        rx_count--;
    }
    return got;
}
```

지적해야 할 것:

| 문제 | 설명 |
|---|---|
| `rx_count`에 `volatile` 없음 | 태스크 쪽 `while (rx_count > 0)`가 레지스터 캐싱으로 무한 루프가 될 수 있다 |
| `rx_count++`와 `rx_count--`의 경쟁 | read-modify-write가 원자적이지 않다. ISR가 태스크의 RMW 사이에 끼면 카운트가 어긋난다 |
| 오버런 처리 없음 | 버퍼가 가득 차도 ISR가 계속 덮어쓰고 `rx_count`가 `RX_SIZE`를 넘는다 |
| 공유 카운터 설계 자체 | SPSC 링버퍼는 카운터 대신 head/tail만으로 만들어 RMW 공유를 없애야 한다 |
| `UART->DR` 접근 | 레지스터 정의가 `volatile`인지 확인 필요 |
| `uint8_t` 인덱스 | `RX_SIZE`가 256을 넘으면 조용히 깨진다. `_Static_assert` 또는 `size_t` |

**말하는 순서가 점수다.** "정확성에 영향을 주는 순서로 말하겠습니다 — 경쟁 조건 먼저, 그다음 오버런, 그다음 타입"처럼 프레임을 먼저 깔면 시니어로 들린다.

올바른 형태는 S01 Q01(ISR-safe SPSC ring buffer)에 전체 코드가 있다. 여기서 중복하지 않는다.

### 3.3 메모리·스택 질문(채널 D)에 쓸 구체적 수단

| 질문 | 답에 반드시 나와야 할 수단 |
|---|---|
| "이 함수가 스택을 얼마나 쓰나?" | GCC `-fstack-usage` (함수별 `.su` 파일), `-Wstack-usage=N`으로 임계 초과 경고 |
| "런타임 최대 사용량은?" | 스택을 패턴(예: `0xA5`)으로 칠하고 나중에 훼손되지 않은 구간을 세어 high-water mark 산출. FreeRTOS는 `uxTaskGetStackHighWaterMark()` 제공 |
| "오버플로를 어떻게 잡나?" | FreeRTOS `configCHECK_FOR_STACK_OVERFLOW` 1 또는 2 + `vApplicationStackOverflowHook()`. 하드웨어로는 MPU 가드 영역, ARMv8-M(Cortex-M33)은 `MSPLIM`/`PSPLIM` 스택 리밋 레지스터 |
| "코드/데이터 크기를 어떻게 보나?" | `arm-none-eabi-size`, 링커 `.map`, `-Wl,--print-memory-usage`, `puncover`, `bloaty` |
| "어디서 크기를 줄이나?" | `-Os`, `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections`, `--specs=nano.specs`(newlib-nano), `-flto`, float `printf` 제거 |
| "이 상수 테이블은 RAM에 있나?" | `const`가 `.rodata`로 가는지 `.map`으로 확인. 포인터 테이블의 초기화 데이터는 `.data`에 남을 수 있음 |
| "재귀는?" | 최악 깊이를 모르면 임베디드에서 금지. MISRA C:2012 Rule 17.2가 재귀를 금지한다 |

**`printf` 함정 한 줄**: newlib-nano(`--specs=nano.specs`)는 기본적으로 `printf`의 부동소수점 포맷을 링크하지 않는다. 필요하면 `-u _printf_float`를 줘야 하고, 그 순간 코드 크기가 크게 는다. "왜 `%f`가 `%f` 그대로 찍히냐"는 실제 현장 질문이자 좋은 면접 소재다 [툴체인 버전·specs 파일에 따라 다름].

### 3.4 임베디드 C++ 서브셋 — 무엇을 끄고 무엇을 쓰나

| 기능 | 보통의 선택 | 이유 (면접에서 말할 근거) |
|---|---|---|
| 예외 (exceptions) | `-fno-exceptions` | 언와인드 테이블(`.ARM.exidx`, `.ARM.extab`)과 런타임(`__cxa_throw`, personality routine)이 수십 KB, throw 경로의 시간이 비결정적, 예외 객체가 힙을 요구 |
| RTTI | `-fno-rtti` | `typeinfo` 객체와 `dynamic_cast` 런타임이 플래시를 먹음. 임베디드에서 쓸 일이 거의 없음 |
| 함수 지역 static의 스레드 안전 | `-fno-threadsafe-statics` | 기본값은 Itanium C++ ABI의 guard 변수 + `__cxa_guard_acquire/release`를 부른다. 싱글 스레드 초기화가 보장되면 꺼서 코드·RAM을 아낌 |
| 전역 객체 소멸자 등록 | `-fno-use-cxa-atexit` | `__cxa_atexit`가 소멸자 목록을 RAM에 쌓는다. 임베디드에서 `main`은 반환하지 않으므로 무의미 |
| 동적 할당 | `operator new` 금지 또는 풀 기반 재정의 | 단편화·비결정성. 필요하면 placement new로 정적 저장소에 생성 |
| STL 컨테이너 | `std::vector`/`std::string`/`std::function` 제외 | 전부 힙. 대안은 고정 용량 컨테이너(ETL 등)나 직접 만든 정적 컨테이너 |
| iostream | 제외 | 정적 초기화 + 거대한 코드 크기 |
| RAII | **적극 사용** | 비용이 0(인라인). critical section, 락, 핀 토글, DMA 소유권에 그대로 맞음 |
| `constexpr` | **적극 사용** | 룩업 테이블·비트 마스크를 컴파일 타임에 계산해 플래시로 보냄 |
| 템플릿 | 제한적으로 | 타입 안전을 얻지만 인스턴스마다 코드가 복제됨(3.5절) |
| 가상 함수 | 필요할 때만 | 객체당 vptr 4바이트 + 플래시의 vtable + 간접 호출로 인라인 불가. 드라이버 추상화 1~2단계 정도면 합리적 |

크기 관련 링크 옵션 세트(실제로 쓰는 조합):

```sh
arm-none-eabi-g++ -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
  -Os -ffunction-sections -fdata-sections \
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
  -c main.cpp -o main.o
arm-none-eabi-g++ ... -Wl,--gc-sections -Wl,--print-memory-usage -specs=nano.specs \
  -T link.ld main.o -o fw.elf
arm-none-eabi-size fw.elf
```

> 정확한 플래그 가용성과 기본값은 **툴체인 버전에 따라 다르다**. GCC의 `-fno-threadsafe-statics`·`-fno-use-cxa-atexit`는 C++ Dialect Options 문서에, `-ffunction-sections`는 Optimize Options 문서에 있다(4절 링크).

### 3.5 템플릿과 코드 크기 — 말로 설명할 수 있어야 한다

템플릿은 타입마다 별도 인스턴스를 만든다. `RingBuffer<uint8_t,64>`와 `RingBuffer<uint16_t,64>`는 서로 다른 코드다. 줄이는 방법 셋.

```cpp
// 1) 타입 독립 로직을 비템플릿 기반 클래스로 내리고, 템플릿은 얇은 래퍼만 둔다
class RingBase {
protected:
    RingBase(unsigned char *buf, unsigned cap) : buf_(buf), cap_(cap) {}
    bool push(const void *item, unsigned sz);   // 여기에 실제 로직 (한 벌만 생성)
    unsigned char *buf_;
    unsigned cap_;
    unsigned head_ = 0, tail_ = 0;
};

template <typename T, unsigned N>
class Ring : private RingBase {                  // 얇은 타입 안전 껍데기
public:
    Ring() : RingBase(storage_, N) {}
    bool push(const T &v) { return RingBase::push(&v, sizeof(T)); }
private:
    alignas(T) unsigned char storage_[N * sizeof(T)];
};
```

- 2) `extern template`으로 인스턴스화를 한 번역 단위에 고정해 중복 생성을 막는다.
- 3) 링커에게 맡긴다: `-ffunction-sections -fdata-sections -Wl,--gc-sections`로 미사용 인스턴스를 버리고, 동일 코드 접기(gold의 `--icf=all`, lld의 `--icf=all`)로 중복을 합친다 [링커·버전에 따라 지원 여부가 다름].

**면접에서의 한 문장**: "템플릿은 런타임 비용이 아니라 **플래시 비용**이다. 나는 로직을 비템플릿으로 내리고 템플릿은 타입 껍데기로만 쓴다. 그리고 `.map`으로 실제 증가분을 확인한다."

### 3.6 정적 초기화 순서 — 왜 이게 임베디드 질문인가

```cpp
// a.cpp
Uart uart(115200);          // 전역 객체
// b.cpp
Logger logger(&uart);       // 생성자에서 uart를 사용 → uart가 먼저 생성됐다는 보장이 없다
```

- 같은 번역 단위 안에서는 정의 순서대로 초기화되지만, **번역 단위 사이의 순서는 표준이 정하지 않는다**(static initialization order fiasco).
- 임베디드에서 더 나쁜 이유: 전역 생성자는 `main` 전에 startup 코드가 `.init_array`를 돌며 호출한다. 그 시점에는 클럭·전원·NVIC가 아직 설정되지 않았을 수 있다. 즉 **순서뿐 아니라 "하드웨어가 준비되기 전"이라는 문제가 겹친다.**

해결책 세 가지와 각각의 비용:

| 해결책 | 형태 | 비용·주의 |
|---|---|---|
| 지연 생성(Meyers singleton) | `Uart& uart() { static Uart u(115200); return u; }` | 기본 설정에서는 guard 변수 + `__cxa_guard_acquire` 호출이 붙는다. `-fno-threadsafe-statics`로 제거 가능하지만 그러면 멀티스레드 동시 최초 호출이 안전하지 않다 |
| 명시적 2단계 초기화 | 전역은 POD로 두고 `main`에서 `uart_init()` 호출 | 가장 예측 가능. 임베디드에서 가장 흔한 선택 |
| 컴파일 타임 초기화 | `constexpr` 생성자 / C++20 `constinit` | 런타임 생성자 자체가 사라짐. 하드웨어 접근이 없는 설정 객체에만 가능 |

**면접 답의 골격**: "번역 단위 간 순서는 미정의다. 임베디드에서는 거기에 '하드웨어가 아직 안 켜졌다'가 더해진다. 그래서 나는 전역 생성자에서 하드웨어를 만지지 않고, `main`에서 명시적으로 초기화한다."

### 3.7 RAII — 임베디드에서 실제로 값어치 하는 곳

```cpp
class CriticalSection {           // ARMv7-M 기준, CMSIS 사용
public:
    CriticalSection()  : saved_(__get_PRIMASK()) { __disable_irq(); }
    ~CriticalSection() { __set_PRIMASK(saved_); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
private:
    uint32_t saved_;
};

void update_shared(void)
{
    CriticalSection cs;           // 진입 시 마스크, 어느 경로로 빠져나가도 복원
    shared_count += 1;
}
```

핵심은 **PRIMASK를 저장했다가 복원**한다는 점이다. 무조건 `__enable_irq()`로 끝내면, 이미 인터럽트가 꺼진 문맥에서 호출됐을 때 임계구역을 조기에 열어 버린다. 이 한 줄 차이가 면접에서 중급과 심화를 가른다. (C02 §7.3, §8.2)

`-fno-exceptions`여도 RAII는 완전히 유효하다. 예외가 없어도 `return` 경로가 여러 개면 소멸자가 값을 한다.

---

## 4. 리서치 — 근거 자료

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| ISO/IEC 9899 C11 공개 드래프트 N1570 | C 언어 표준 본문(무료 드래프트) | §6.3.1.1 정수 승격, §6.5 식(시퀀스 포인트·aliasing), §6.7.3 `volatile`, Annex J UB 목록 | https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf |
| WG14 문서 색인 | C17/C23 드래프트 등 후속 표준 | 최신 드래프트 번호 확인 | https://www.open-std.org/jtc1/sc22/wg14/www/docs/ |
| cppreference — Implicit conversions | 정수 승격·일반 산술 변환 규칙 요약 | "Integral promotion", "Usual arithmetic conversions" | https://en.cppreference.com/w/c/language/conversion |
| SEI CERT C Coding Standard | 규칙별로 위험한 C 패턴과 준수 코드 | INT02-C(변환 규칙), INT30-C(unsigned wrap), INT32-C(signed overflow), EXP36-C(정렬), MEM(메모리) 장 | https://wiki.sei.cmu.edu/confluence/display/c/SEI+CERT+C+Coding+Standard |
| MISRA | 자동차·안전 계열 C/C++ 코딩 표준 원문(유료) | Dir 4.12·Rule 21.3(동적 할당 금지), Rule 17.2(재귀 금지), Rule 10.x(essential type model) | https://misra.org.uk/ |
| NASA/JPL "The Power of 10" (Holzmann) | 임베디드 안전 코드 10계명 | 규칙 2(루프 상한), 규칙 3(동적 할당 금지) | https://spinroot.com/gerard/pdf/P10.pdf |
| Arm ABI (AAPCS32/AAPCS64) | 호출 규약, 타입 크기·정렬, `char` 부호 | AAPCS32 "Data types" 절(plain `char`는 unsigned), 스택 정렬 규칙 | https://github.com/ARM-software/abi-aa |
| Armv7-M Architecture Reference Manual | 언얼라인 접근 규칙, 배타적 접근(LDREX/STREX), CCR의 UNALIGN_TRP | "Alignment support", "Synchronization and semaphores" | https://developer.arm.com/documentation/ddi0403/latest/ |
| Armv8-M Architecture Reference Manual | MSPLIM/PSPLIM 스택 리밋, TrustZone-M | "Stack limit checking" | https://developer.arm.com/documentation/ddi0553/latest/ |
| CMSIS 문서 | `__disable_irq`, `__get_PRIMASK`, `__DMB/__DSB/__ISB` 등 코어 함수 | Core(M) → "Intrinsic Functions" | https://arm-software.github.io/CMSIS_6/latest/General/index.html |
| GCC — Optimize Options | `-Os`, `-ffunction-sections`, `-fdata-sections`, `-flto` | 각 플래그 정의 | https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html |
| GCC — C++ Dialect Options | `-fno-exceptions`, `-fno-rtti`, `-fno-threadsafe-statics`, `-fno-use-cxa-atexit` | 각 플래그 정의 | https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html |
| GCC — ARM Options | `-mcpu`, `-mthumb`, `-mfloat-abi`, `-mfpu` | 조합 규칙 | https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html |
| GCC — Volatiles | `volatile` 접근에 대한 GCC의 해석 | 문서 전체(짧음) | https://gcc.gnu.org/onlinedocs/gcc/Volatiles.html |
| Itanium C++ ABI | 정적 로컬 guard 변수, `__cxa_atexit` 규정 | "One-time construction API" 절 | https://itanium-cxx-abi.github.io/cxx-abi/abi.html |
| C++ Core Guidelines | 현대 C++ 관용구. 임베디드에 그대로는 못 쓰지만 근거로 유용 | R(자원 관리), Per(성능) 섹션 | https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines |
| ETL (Embedded Template Library) | 힙 없는 고정 용량 컨테이너 구현 | `etl::vector`, `etl::string`, `etl::delegate` | https://www.etlcpp.com/ |
| Zephyr — C++ 지원 문서 | RTOS가 실제로 허용하는 C++ 서브셋 | "Language Support → C++" (예외·RTTI 기본 정책) | https://docs.zephyrproject.org/latest/develop/languages/cpp/index.html |
| FreeRTOS 문서 | 스택 오버플로 검출, 스택 high-water mark API | `configCHECK_FOR_STACK_OVERFLOW`, `uxTaskGetStackHighWaterMark` | https://www.freertos.org/ |
| Memfault Interrupt 블로그 | 임베디드 실무 글 모음(스택 사용량 측정, 코드 크기 줄이기, HardFault 분석) | 검색: "stack", "code size", "fault" | https://interrupt.memfault.com/ |
| puncover | ELF에서 함수별 코드·스택 사용량을 웹 UI로 | README의 사용법 | https://github.com/HBehrens/puncover |
| bloaty | 바이너리 크기 분해 도구 | `bloaty fw.elf -d sections,symbols` | https://github.com/google/bloaty |
| Compiler Explorer (godbolt) | 플래그별 생성 어셈블리 확인 | `arm-none-eabi-gcc`, `-O0` vs `-O2`로 `volatile` 차이 관찰 | https://godbolt.org/ |
| Clang UndefinedBehaviorSanitizer | 호스트에서 정수 UB 잡기 | `-fsanitize=undefined` 체크 목록 | https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html |

**버전 의존 표시**
- MISRA 규칙 번호는 **판본마다 다르다**(MISRA C:2004 vs C:2012 vs C:2012 AMD). 위 번호는 MISRA C:2012 기준이다.
- CMSIS 문서 URL은 CMSIS 5 → 6에서 경로가 바뀌었다. 검색으로 최신 경로를 확인한다.
- `--specs=nano.specs`의 `printf` float 동작, `-u _printf_float` 필요 여부는 **newlib/툴체인 배포판마다 다르다**.
- 링커 ICF(`--icf=all`)는 gold/lld에서 지원하며 기본 BFD ld에는 없다.
- `stdatomic.h`(C11)의 임베디드 지원 여부와 lock-free 여부는 **타깃·컴파일러마다 다르다**. ARMv6-M(Cortex-M0+)에는 LDREX/STREX가 없어 상당수 연산이 라이브러리 호출로 떨어진다.

---

## 5. 예상 면접 질문

난이도: **[기초]** 5분 안에 판별되는 필수 통과 문항 · **[중급]** 실무 경험이 있어야 답이 되는 문항 · **[심화]** 밴드 상단 신호를 만드는 문항.

### Q01. [기초] What does `volatile` actually guarantee, and what does it not?

**왜 묻나**: 임베디드 C의 리트머스. "최적화 방지"까지만 말하면 3년차, 보장 범위의 경계를 말하면 시니어.

**30초 답변**: volatile은 그 객체에 대한 접근을 추상 기계가 쓴 그대로, 생략·병합 없이, volatile 접근끼리의 순서를 지켜 수행하게 한다. 보장하지 않는 것은 원자성, 비-volatile 접근과의 순서, 캐시/쓰기 버퍼 일관성, 멀티코어 가시성이다.

**English answer**:
"`volatile` tells the compiler that every read and write of that object is observable behavior, so it can't cache the value in a register, delete a redundant access, or reorder it relative to other volatile accesses. That's exactly what you need for a memory-mapped register or a flag an ISR writes. What it does not give you is atomicity — a `volatile` increment is still load, add, store — and it says nothing about ordering against non-volatile accesses, or about caches and write buffers, so on a core with a store buffer or on a multi-core part you still need a barrier like `DMB`. My rule is: `volatile` for visibility, critical section or an exclusive-access loop for atomicity, barrier for ordering against hardware."

**꼬리질문**
- "`volatile` 카운터를 ISR와 태스크가 같이 증가시키면?" → RMW가 깨진다. 잃은 업데이트 발생.
- "그럼 무엇을 쓰나?" → 임계구역, 또는 LDREX/STREX 루프, 또는 SPSC 구조로 공유 RMW 자체를 없앤다.
- "`volatile` 구조체 포인터로 레지스터 블록을 쓰는 건 되나?" → 된다. 단 멤버마다 접근 폭이 정확해야 하고, 컴파일러가 폭을 바꾸지 않는다는 보장은 `volatile`이 아니라 타입에서 온다.

### Q02. [기초] What is `static` in C — all the meanings.

**왜 묻나**: 언어 기본기와 링크 모델 이해를 동시에 본다.

**30초 답변**: 세 가지다. ① 파일 스코프 변수/함수에 붙으면 내부 링키지(다른 번역 단위에서 안 보임) ② 함수 안 지역 변수에 붙으면 정적 저장 기간(호출 간 유지, `.data`/`.bss`) ③ 배열 파라미터의 `static`(C99)은 최소 원소 수 보장.

**English answer**:
"Three meanings. At file scope it means internal linkage — the symbol doesn't leave the translation unit, which is how you keep driver internals private and avoid name collisions at link time. Inside a function it changes storage duration, not linkage: the variable lives in `.data` or `.bss` for the whole program, which matters in embedded because it's RAM you pay for at link time rather than stack you pay for at call time. And in a parameter like `void f(uint8_t buf[static 16])` it's a promise to the compiler that at least sixteen elements exist. In firmware I default to `static` for anything that isn't part of a module's public header, because it lets the linker garbage-collect and it makes the `.map` readable."

**꼬리질문**
- "함수 안 `static` 변수는 ISR-safe한가?" → 아니다. 재진입성이 깨진다.
- "`static const` 배열은 어디에 놓이나?" → 보통 `.rodata`(플래시). `.map`으로 확인한다.

### Q03. [기초] `uint16_t a = 0xFFFF; uint32_t b = a * a;` — what is `b`?

**왜 묻나**: 정수 승격. 임베디드 C 면접의 고전이고, 틀리는 사람이 많다.

**30초 답변**: `a`가 `int`로 승격되어 `65535 * 65535 = 4294836225`가 32비트 `int` 범위를 넘는다. **부호 있는 오버플로는 UB**다. 실무 결과는 보통 하위 32비트인 `0xFFFE0001`이지만, 값을 논하기 전에 UB라는 점을 말해야 한다. 고치려면 `(uint32_t)a * a`.

**English answer**:
"Both operands get promoted to `int` before the multiply, because `int` can represent every `uint16_t` value. So this is a signed 32-bit multiply of 65535 by 65535, which overflows, and signed overflow is undefined behavior — not implementation-defined, undefined. In practice on ARM you'll see 0xFFFE0001, but the compiler is allowed to assume it can't happen and optimize on that assumption, which is how these bugs disappear at -O0 and appear at -O2. The fix is to force the arithmetic into the type you want: `(uint32_t)a * a`. I check this class of bug on the host with `-fsanitize=undefined` before it ever runs on the target."

**꼬리질문**
- "`uint32_t`끼리면?" → unsigned는 모듈러 연산으로 정의되어 있어 UB가 아니다(랩어라운드).
- "`int len; if (len - 1 >= 0)`는?" → `len`이 unsigned면 항상 참. 부호 혼합 비교 함정.
- "`1 << 31`은?" → `int`에서 UB. `1u << 31`로 쓴다.

### Q04. [기초] Stack, heap, `.data`, `.bss`, `.rodata` — where does each live and who sets them up?

**왜 묻나**: resource-constrained의 기본 지도. 부트 과정을 아는지도 같이 본다.

**30초 답변**: `.text`/`.rodata`는 플래시, `.data`는 플래시에 초기값이 있고 부팅 시 RAM으로 복사, `.bss`는 RAM에서 0으로 클리어, 힙과 스택은 남은 RAM을 양쪽에서 나눠 쓴다. 복사·클리어는 startup 코드가 링커 심볼을 보고 한다.

**English answer**:
"`.text` and `.rodata` stay in flash and execute or read in place. `.data` has initial values stored in flash and the startup code copies them into RAM before `main`. `.bss` is zeroed by startup. Then the heap and the stack share whatever RAM is left — typically the heap grows up from the end of `.bss` and the stack grows down from the top of RAM, which is why a heap and a deep call chain can silently meet in the middle. The linker script defines all of those boundaries as symbols, and the startup file is the only code that uses them. On a constrained part I read the `.map` after every significant change, because RAM is spent at link time and I'd rather find it there than at 3am."

**꼬리질문**
- "왜 `.bss`가 따로 있나?" → 0 초기화 데이터를 플래시에 저장할 필요가 없어 이미지 크기를 줄인다.
- "힙과 스택이 만나면 어떻게 아나?" → 가드 영역(MPU), 스택 패턴 검사, ARMv8-M은 PSPLIM.
- "`const` 포인터 배열이 `.data`에 있는 이유는?" → 포인터 값이 재배치 대상이면 초기화 데이터가 필요할 수 있다. 실제 배치는 `.map` 확인.

### Q05. [기초] What must you never do inside an ISR?

**왜 묻나**: 드라이버·RTOS 협업의 기본. JD의 드라이버·오디오 업무와 직결.

**30초 답변**: 블로킹(대기·긴 루프), 동적 할당, `printf` 같은 무거운/재진입 불가 라이브러리, 비-FromISR RTOS API 호출, 긴 임계구역. ISR는 **하드웨어를 진정시키고 데이터를 옮기고 플래그를 세우는 것**까지만.

**English answer**:
"An ISR should acknowledge the hardware, move the data, and hand off — nothing that can block and nothing unbounded. So: no waiting on a semaphore or a queue with a timeout, no `malloc`, no `printf` or anything else that isn't reentrant or that has unbounded execution time, and on an RTOS only the from-ISR variants of the API, because the normal ones can attempt a context switch from the wrong context. I also keep an eye on how long the ISR masks other interrupts, since that directly becomes worst-case latency for everything at lower priority. If work is genuinely needed, it goes to a task or a deferred handler and the ISR just posts."

**꼬리질문**
- "ISR에서 태스크를 깨우는 올바른 방법은?" → FreeRTOS라면 `xQueueSendFromISR`/`vTaskNotifyGiveFromISR` + `portYIELD_FROM_ISR(woken)`.
- "인터럽트 지연을 어떻게 측정하나?" → GPIO 토글 + 스코프, DWT 사이클 카운터, ITM/SWO 트레이스.

### Q06. [중급] Implement a lock-free SPSC ring buffer that a UART ISR writes and a task reads.

**왜 묻나**: 이 역할의 실제 코드. 채널 A(라이브 코딩)의 1순위 문제다.

**30초 답변**: 공유 카운터를 두지 않고 head는 생산자만, tail은 소비자만 쓴다. 크기는 2의 거듭제곱으로 잡아 마스크 연산을 쓴다. 가득 참 판정은 한 칸 비워 두거나 랩 카운트를 쓴다. 오버런 시 동작(드롭/덮어쓰기)을 **먼저 정의한다**.

**English answer**:
"The key idea is that no variable is written by both sides. The producer owns head, the consumer owns tail, and each only reads the other's index. That removes the read-modify-write race entirely, which is why it works without a lock on a single-core MCU. I make the capacity a power of two so wrapping is a mask, and I keep one slot empty so full and empty are distinguishable without a shared count. Both indices are `volatile` so neither side caches a stale value. Before writing a line of it I ask what happens on overrun — dropping the newest byte is usually right for a UART, overwriting the oldest is usually right for a log — and I return that as a counted statistic so it shows up in telemetry instead of disappearing."

**꼬리질문**
- "멀티코어라면?" → `volatile`로는 부족하다. C11 atomics의 acquire/release 또는 명시적 `DMB`.
- "가득 참을 어떻게 판정하나?" → 한 칸 비우기 vs 랩 비트. 각각의 장단.
- "왜 2의 거듭제곱인가?" → 나눗셈 없는 마스크 연산. 아니면 비교 후 0 복귀.

**전체 코드는 S01 Q01에 있다. 이 노트에서 중복하지 않는다.**

### Q07. [중급] Here's a driver snippet. Review it.

**왜 묻나**: 채널 B. 코드를 읽는 눈은 연차로만 얻어진다.

**30초 답변**: 3.2절 스니펫이 전형이다. 지적 순서를 **정확성 → 안전성 → 유지보수성**으로 잡고, 각 지적마다 "왜 문제인지 + 어떤 증상으로 나타나는지"를 붙인다.

**English answer**:
"Let me go in order of severity rather than top to bottom. First, correctness under concurrency: `rx_count` is incremented in the ISR and decremented in the task, and that read-modify-write isn't atomic, so counts get lost — the symptom is a receiver that slowly stops seeing data. Second, it's missing `volatile`, so the task's wait loop can be hoisted into a register and spin forever; that one shows up only at higher optimization levels. Third, there's no overrun policy: when the buffer fills, the ISR keeps writing and corrupts unread data silently. The structural fix is to drop the shared counter and derive fullness from head and tail, so each variable has exactly one writer. After that I'd note the `uint8_t` index, which breaks silently if someone raises the buffer size past 256."

**꼬리질문**
- "`volatile`만 붙이면 첫 번째 문제가 해결되나?" → 아니다. 가시성과 원자성은 다른 문제.
- "어떻게 테스트하겠나?" → ISR를 강제로 고빈도 발생시키는 스트레스 테스트, 오버런 카운터 노출.

### Q08. [중급] A sensor packet has 16- and 32-bit little-endian fields at odd offsets. Parse it safely on any CPU.

**왜 묻나**: 정렬·엔디언·strict aliasing을 한 문제에 담은 단골 문항.

**30초 답변**: 버퍼에 구조체 포인터를 캐스팅하지 않는다. 바이트 단위로 읽어 시프트로 조립하거나 `memcpy`를 쓴다. `packed` 구조체를 쓰더라도 그 멤버의 주소를 일반 포인터로 넘기지 않는다.

**English answer**:
"I never cast the receive buffer to a struct pointer. Two things break: alignment, because on ARMv6-M an unaligned word load faults outright and even where it's allowed, `LDM` and `LDRD` still fault; and strict aliasing, because accessing those bytes through an incompatible type is undefined and the optimizer is allowed to reorder around it. So I write byte-wise accessors that shift and or the bytes together in the wire order, which is endian-independent by construction and compiles down to almost nothing on a core that supports unaligned access anyway. If I do use a packed struct for documentation, I still never take the address of a packed member and pass it as a normal pointer, because that pointer is unaligned and the callee doesn't know."

**꼬리질문**
- "`packed`의 비용은?" → 컴파일러가 바이트 단위 접근으로 쪼개 코드가 커지고 느려진다.
- "호스트가 리틀 엔디언이면 그냥 캐스팅해도 되지 않나?" → 정렬과 aliasing 문제는 남는다.
- "`__builtin_bswap32`는?" → 엔디언 변환에는 좋지만 정렬 문제는 해결하지 못한다.

**코드는 S01 Q07 참조.**

### Q09. [중급] No `malloc` allowed. Write a fixed-block memory pool.

**왜 묻나**: `resource-constrained`의 직접 번역. 결정성·단편화 이해를 본다.

**30초 답변**: 고정 크기 블록 N개를 정적 배열로 잡고, 빈 블록들을 그 자체 공간에 free list로 엮는다. alloc/free 모두 O(1), 단편화 없음, 최악 시간 결정적. 정렬은 `alignas`(또는 `max_align_t` 크기 기준)로 맞춘다.

**English answer**:
"I allocate one static array of N fixed-size blocks and thread a free list through the blocks themselves, so there's zero per-block overhead. Allocation pops the head, free pushes it back — both O(1) and both deterministic, which is the real reason we do this rather than to save memory. There's no external fragmentation because every block is the same size. The two details people miss are alignment, so I make the block size a multiple of the strictest alignment and declare the storage with `alignas`, and concurrency, so if ISRs allocate I protect the list head with a short critical section or an exclusive-access loop. I also track a low-water mark of free blocks, because that number is what tells me at review time whether the pool is sized right."

**꼬리질문**
- "왜 `malloc`이 아니라 풀인가?" → 단편화 없음, 최악 시간 보장, 실패를 링크 타임 예산으로 밀어낼 수 있음. MISRA C:2012 Dir 4.12/Rule 21.3도 동적 할당을 금지.
- "크기가 다른 객체가 여러 종류면?" → 크기별 풀 여러 개. 어떤 요청이 어느 풀로 가는지 명시.
- "풀이 고갈되면?" → 정책을 미리 정한다(NULL 반환 후 상위에서 드롭, 또는 assert). 조용히 실패하지 않게.

**코드는 S01 Q02 참조.**

### Q10. [중급] How do you decide a task's stack size, and how do you know it's not overflowing?

**왜 묻나**: 채널 D의 대표 문항. "넉넉히"라고 답하면 바로 감점.

**30초 답변**: 정적으로는 `-fstack-usage`로 함수별 프레임을 보고 최악 호출 경로를 더한 뒤 ISR 중첩분을 얹는다. 동적으로는 스택을 패턴으로 칠하고 high-water mark를 측정한다. 검출은 RTOS 훅 + MPU 가드(또는 ARMv8-M의 PSPLIM).

**English answer**:
"Two halves: estimate, then measure. For the estimate I build with `-fstack-usage`, which emits a per-function frame size, and add up the worst call path — plus interrupt frames, because on Cortex-M an exception pushes a stack frame onto whichever stack is active, and nesting multiplies that. For the measurement I fill the stack with a known pattern at init and read back the high-water mark at runtime; FreeRTOS gives you that directly with `uxTaskGetStackHighWaterMark`. For detection I turn on `configCHECK_FOR_STACK_OVERFLOW` and, where the part supports it, put an unmapped MPU guard region below each stack so an overflow is a fault with a stack trace instead of silent corruption; on Armv8-M you get `PSPLIM` for free. And I keep headroom rather than trimming to the measured maximum, because the measured maximum is only the paths I exercised."

**꼬리질문**
- "재귀는?" → 최악 깊이를 증명할 수 없으면 금지(MISRA C:2012 Rule 17.2).
- "`printf`가 스택을 얼마나 쓰나?" → 구현마다 다르지만 크다. float 포맷을 켜면 더 커진다.
- "오버플로가 이미 일어난 뒤 증상은?" → 인접 태스크 스택/`.bss` 훼손, 엉뚱한 곳에서의 HardFault. 그래서 훼손 이후 증상으로 역추적하지 말고 가드로 즉시 잡는다.

### Q11. [심화] On Cortex-M, what is atomic and what isn't? How does that change between M0+ and M4?

**왜 묻나**: 아키텍처 수준의 정확도. 밴드 상단을 가르는 질문 중 하나.

**30초 답변**: 정렬된 워드/하프워드/바이트의 단일 로드·스토어는 원자적으로 볼 수 있지만, `x++` 같은 RMW는 아니다. ARMv7-M(M3/M4/M7)과 ARMv8-M(M33)은 LDREX/STREX 배타적 접근을 제공하고, ARMv6-M(M0/M0+)은 제공하지 않아 PRIMASK로 인터럽트를 막는 방법밖에 없다.

**English answer**:
"A single aligned load or store of a word, halfword or byte is indivisible on these cores, so a flag written by an ISR and read by a task is fine as long as it's one word and it's `volatile`. What isn't atomic is anything read-modify-write — increments, bit sets on a variable, updating two fields that must agree. For those, Armv7-M and Armv8-M give you `LDREX`/`STREX`: you load exclusive, modify, store exclusive, and retry if the store reports that the monitor was cleared, which happens on a context switch or a competing access. Cortex-M0 and M0-plus are Armv6-M and don't have those instructions at all, so there the only tool is masking interrupts with `PRIMASK` — and then the cost is interrupt latency, so the critical section has to be a handful of instructions. That difference is worth knowing before you port a driver from an M4 to an M0-plus sensor hub."

**꼬리질문**
- "임계구역을 RAII로 감싸면 뭘 조심하나?" → PRIMASK를 저장·복원해야 한다. 무조건 enable하면 중첩 시 조기 해제(3.7절).
- "BASEPRI는?" → ARMv7-M에서 특정 우선순위 이하만 막을 수 있어, 고우선 인터럽트(예: 오디오 DMA)는 살려 둘 수 있다.
- "C11 `stdatomic.h`를 쓰면?" → 타깃에 따라 lock-free가 아닐 수 있고, ARMv6-M에서는 라이브러리 호출로 떨어질 수 있다. 무엇으로 컴파일되는지 확인하고 쓴다.

### Q12. [심화] Why do embedded teams build C++ with `-fno-exceptions` and `-fno-rtti`?

**왜 묻나**: "C++ 쓸 줄 안다"와 "임베디드에서 C++ 쓸 줄 안다"의 차이.

**30초 답변**: 예외는 언와인드 테이블(`.ARM.exidx`/`.ARM.extab`)과 런타임을 링크해 플래시를 수십 KB 먹고, throw 경로의 실행 시간이 비결정적이며, 예외 객체 할당이 힙을 요구한다. RTTI는 `typeinfo`와 `dynamic_cast` 런타임 비용. 둘 다 임베디드에서 값어치보다 비용이 크다.

**English answer**:
"Three reasons, and cost is only one of them. Size: enabling exceptions pulls in the unwinder and the `.ARM.exidx` and `.ARM.extab` tables, plus the runtime in libsupc++, and on a part with a few hundred kilobytes of flash that's real money. Determinism: the throw path walks frames and runs destructors, and its worst-case time isn't something I can bound the way I can bound a return code, which matters when the function is on a deadline. Allocation: the standard exception object is allocated, and in a system where I've banned the heap that's a contradiction. RTTI is the same trade at smaller scale — `typeinfo` records and `dynamic_cast` machinery I'd almost never use. So the team convention becomes error codes or a `Result` type, and RAII stays, because RAII is free and it's still the right answer for critical sections, bus locks and DMA ownership even without exceptions."

**꼬리질문**
- "예외 없이 생성자 실패는 어떻게 다루나?" → 생성자에서 실패하지 않게 설계하고, 실패 가능한 초기화는 `init()`로 분리해 상태 코드를 반환.
- "`-fno-exceptions`인데 라이브러리가 throw하면?" → `std::terminate`로 간다. 서드파티 코드가 예외를 쓰는지 확인해야 한다.
- "RAII는 예외가 없으면 의미가 없지 않나?" → 아니다. 다중 return 경로만 있어도 값을 한다.

### Q13. [심화] Templates in firmware — when do they pay, and how do you keep code size under control?

**왜 묻나**: C++ 서브셋 운영 감각. 플래시 예산과 언어 기능을 같이 생각하는지.

**30초 답변**: 템플릿의 비용은 런타임이 아니라 **인스턴스마다 복제되는 코드(플래시)**다. 로직을 비템플릿 기반 클래스에 내리고 템플릿을 얇은 타입 껍데기로 쓰는 방식, `extern template`, `--gc-sections`와 ICF로 관리한다. 그리고 `.map`으로 실제 증가분을 본다.

**English answer**:
"Templates buy compile-time type safety and let me put constants like buffer capacity in the type, which turns a runtime check into nothing. The cost is that each instantiation is a separate copy of the code, so `Ring<uint8_t,64>` and `Ring<uint16_t,64>` are two functions in flash. The pattern I use is to push the type-independent logic into a non-template base that works on bytes and a size, and keep the template as a thin typed wrapper that inlines away — one copy of the real code, N copies of almost nothing. Beyond that, `extern template` stops redundant instantiation across translation units, and `-ffunction-sections -fdata-sections` with `--gc-sections` drops what nothing calls. Then I actually check: I diff `arm-none-eabi-size` and the `.map` before and after, because guessing about code size is how budgets get blown."

**꼬리질문**
- "`constexpr`는 크기에 어떤 영향인가?" → 계산이 컴파일 타임에 끝나면 코드가 사라지고 상수만 플래시에 남는다. 보통 이득.
- "가상 함수의 비용은?" → 객체당 vptr, 플래시의 vtable, 간접 호출로 인한 인라인 불가. 추상화 층이 얕으면 수용 가능.
- "헤더에 다 넣으면 빌드가 느려지지 않나?" → 느려진다. 인터페이스만 템플릿, 구현은 가능한 한 분리.

### Q14. [심화] Explain the static initialization order problem and how you avoid it on a microcontroller.

**왜 묻나**: C++를 실제로 펌웨어에 써 본 사람만 자연스럽게 답한다.

**30초 답변**: 번역 단위 간 전역 객체 초기화 순서는 표준이 정하지 않는다. 임베디드에서는 여기에 "전역 생성자가 `main` 전, 즉 클럭·전원·NVIC 설정 전에 `.init_array`에서 실행된다"는 문제가 겹친다. 해결은 명시적 2단계 초기화, 함수 지역 static, `constexpr`/`constinit`.

**English answer**:
"Within one translation unit, globals initialize in definition order. Across translation units the order is unspecified, so if a logger's constructor uses a UART object defined in another file, it may run first and use an object that doesn't exist yet. On a microcontroller there's a second, worse version of the same problem: all of those constructors run from `.init_array` before `main`, which means before I've configured clocks, power domains and the interrupt controller. So my rule is that no global constructor touches hardware. Either the object is a plain aggregate initialized at compile time — `constexpr`, or `constinit` in C++20 — or it's a function-local static so construction happens on first use, or, most often, I keep globals trivial and call explicit `init()` functions from `main` in an order I control and can read. If I do use function-local statics I know the default brings in guard variables and `__cxa_guard_acquire` from the Itanium ABI, and I decide deliberately whether `-fno-threadsafe-statics` is safe for that code."

**꼬리질문**
- "전역 소멸자는?" → `__cxa_atexit`가 등록 목록을 RAM에 쌓는다. `main`이 반환하지 않는 펌웨어에서는 `-fno-use-cxa-atexit`로 줄인다.
- "startup 코드에서 `.init_array`를 언제 도나?" → `.data` 복사·`.bss` 클리어 뒤, `main` 직전(C01 §5).

### Q15. [심화] You need 8 KB more SRAM for a model tensor arena. Where do you get it, and how do you decide?

**왜 묻나**: 이 JD 문장의 최종 형태. 언어 지식이 **예산 협상**으로 이어지는지 본다.

**30초 답변**: 먼저 측정한다 — `.map`으로 `.bss`/`.data` 상위 소비자, 태스크별 high-water mark, 버퍼 깊이의 실제 사용률. 그다음 후보를 비용과 함께 제시한다: 태스크 스택 과다 할당 회수, 로그 버퍼 축소, 오디오 버퍼 깊이 축소(대신 지연·오버런 위험), `const` 테이블을 플래시로 이동, 중복 버퍼 통합. 각 선택의 **실패 모드**를 같이 말한다.

**English answer**:
"I'd start from data, not opinion. Three measurements: the top RAM consumers in the `.map`, the high-water mark of every task stack, and the actual occupancy of the audio and log buffers under real load. Usually the first pass finds a couple of kilobytes of stack that was sized by copy-paste and a debug log buffer nobody reads in production. After that the choices start to cost something, and I'd bring them to the AI engineer as a menu with consequences: shrinking the audio double buffer gets you memory but tightens the deadline for the processing task and raises overrun risk under load; moving a lookup table to flash costs you a little latency if the bus is slow; reusing the OTA staging buffer as arena is free only if the two are never live at the same time, and that has to be enforced, not assumed. What I don't do is take it out of headroom silently — if we end up with less margin than before, that should be a decision somebody made, with a number attached."

**꼬리질문**
- "OTA 버퍼와 arena를 겹쳐 쓰는 건 위험하지 않나?" → 상태 머신으로 동시 활성 불가를 강제하고, 링커 섹션을 공유하되 런타임 assert를 건다.
- "어떻게 회귀를 막나?" → CI에서 `size` 출력과 `.map` 요약을 빌드마다 기록하고 임계 초과 시 빌드 실패.
- "모델이 더 커지면?" → 양자화(INT8/INT4), 연산자 교체, 또는 SoC로 오프로드. 경계는 C08 §10·§11.

---

## 6. Don 매핑

### 6.1 이 요건에서의 위치

| 축 | 상태 | 근거 (컨텍스트 3.1절 레쥬메 인용) |
|---|---|---|
| bare-metal C/C++ | ✅ 강함 | "Developed using embedded C and C++ programming on bare-metal (pre/post-silicon)" |
| 자원 제약 감각 | ✅ 강함 | SSD 컨트롤러 펌웨어 — 고정 SRAM, 고정 열·전력 예산 안에서의 설계 |
| ISR·DMA·링버퍼 | ✅ 강함 | NOTES_SPEC 기준 "bare-metal, ISR, DMA, 레지스터, 링버퍼"가 익숙한 영역 |
| Cortex-M/R 레지스터·원자성 | ✅ | "ARM Cortex R8/R82/M0+ … FW bring-up". **M0+(ARMv6-M)에 LDREX/STREX가 없다는 점을 실제로 겪었을 가능성이 높다 — Q11의 최고의 재료** |
| 임베디드 C++ 서브셋 어휘 | 🟡 | C++를 썼다는 근거는 있으나, `-fno-exceptions`·정적 초기화 순서 같은 **정책을 말로 설명하는 연습**이 필요 |
| 메모리 예산 협상(채널 D) | 🟡 | 경험은 있으나 숫자·도구 이름(`-fstack-usage`, `.map`, high-water mark)으로 말하는 훈련 필요 |
| 화이트보드 속도 | 🟡 | S01 드릴로 해결. 실무에서 IDE 없이 쓸 일이 없었던 것이 유일한 약점 |

### 6.2 답변에 끼워 넣을 Don 고유 문장 (레쥬메 근거만)

| 상황 | 문장 |
|---|---|
| 자원 제약 이야기 | "SSD 컨트롤러 펌웨어는 SRAM이 고정이고 데이터 경로가 성능 지표라, 버퍼를 어디에 몇 개 둘지를 링크 타임에 결정하는 게 일상이었습니다." |
| 원자성·ISR | "Cortex-M0+와 Cortex-R을 같은 제품 안에서 다뤘기 때문에, 같은 드라이버 로직이 코어에 따라 배타적 접근을 쓸 수 있느냐 없느냐로 갈린다는 걸 실제로 겪었습니다." |
| pre-silicon | "FPGA pre-silicon에서 bring-up을 하면 툴이 거의 없어서, `.map`과 레지스터 덤프와 GPIO 토글만으로 판단해야 합니다. 그 습관이 지금도 남아 있습니다." |
| 디버깅 도구 | "JTAG·Trace32·로직 분석기·스코프·전력 분석기를 실제로 씁니다. 코드를 읽어서 안 나오면 신호를 봅니다." |
| 양산 관점 | "제 코드의 실패는 양산 라인이나 필드에서 드러납니다. 그래서 오버런이나 예산 초과를 조용히 넘기지 않고 카운터로 노출하는 습관이 있습니다." |

### 6.3 프레이밍 규칙

- **"C도 C++도 씁니다"로 끝내지 않는다.** "펌웨어는 C, 추상화가 필요한 곳은 예외·RTTI를 끈 C++ 서브셋"처럼 **정책**으로 말한다. 그게 이 요건이 실제로 묻는 것이다.
- **모든 코딩 답변의 마지막에 예산 한 줄을 붙인다.** "이 구조의 RAM 비용은 N바이트, 스택 사용은 프레임 하나"처럼. 채널 D 점수를 A 채널에서 미리 벌 수 있다.
- **모르는 것은 아키텍처 의존이라고 정확히 말한다.** "M0+에서는 다를 수 있어 확인하겠습니다"는 감점이 아니라 가점이다.
- **SSD 경험을 "제약 없는 서버"로 오해받지 않게 한다.** 엔터프라이즈 SSD 컨트롤러는 임베디드 MCU급 제약을 가진다는 점을 한 문장으로 먼저 깔아 둔다.

### 6.4 미확인 항목

- <확인 필요: Don이 쓴 C++가 어느 정도 범위였는지 — 클래스·RAII 수준인지, 템플릿·`constexpr`까지 썼는지. Q12~Q14의 답을 "우리 팀 정책은 이랬다"로 구체화할 수 있는지가 여기 달려 있다.>
- <확인 필요: 프로젝트에서 동적 할당 정책이 어땠는지(전면 금지 / 부팅 시 1회 / 풀). Q09의 실사례가 된다.>
- <확인 필요: 스택 사용량·코드 크기를 관리하는 팀 관행이 있었는지(`.map` 리뷰, CI 크기 게이트). 있으면 Q10·Q15가 즉시 강해진다.>
- <확인 필요: MISRA 또는 사내 코딩 표준 적용 경험 여부. 엔터프라이즈 SSD 펌웨어에서 흔하다.>

---

## 7. 준비 체크리스트

- [ ] S01 Q01(SPSC 링버퍼), Q02(memory pool), Q05(레지스터 필드 매크로), Q07(언얼라인 파싱)을 **종이에 15분 안에** 손으로 쓰기 — 3회 반복
- [ ] 3.2절 코드 리뷰 스니펫을 보지 않고 결함 6개 모두 지적, **심각도 순서로** 말하기
- [ ] `-fstack-usage`로 실제 프로젝트를 빌드해 `.su` 파일을 열어 보기 (godbolt 또는 로컬 arm-none-eabi-gcc)
- [ ] `arm-none-eabi-size`와 `.map`에서 상위 RAM 소비자 5개를 뽑는 과정을 한 번 해 보고, 그 절차를 영어 3문장으로 정리
- [ ] godbolt에서 `volatile` 유무 코드를 `-O0`/`-O2`로 비교해 어셈블리 차이를 눈으로 확인 (Q01의 근거)
- [ ] `-fno-exceptions`·`-fno-rtti` 유무로 같은 C++ 코드를 빌드해 `size` 차이를 측정, 실제 숫자를 답변에 넣기 (Q12)
- [ ] ARMv6-M(M0+)에 LDREX/STREX가 없다는 사실을 Armv7-M ARM 목차에서 확인하고, Don의 M0+ 경험과 엮은 한 문장 만들기 (Q11)
- [ ] 임베디드 C++ 플래그 세트(3.4절 표)를 외워서 말할 수 있게 — 플래그 이름 + 이유 한 줄씩
- [ ] 2.3절 SRAM 배분 그림을 빈 종이에 다시 그리고, 각 항목의 크기를 자기 말로 설명 (Q15)
- [ ] 정수 승격 함정 4종(Q03의 꼬리질문들)에 즉답할 수 있게 암기
- [ ] 모든 코딩 답변 끝에 붙일 "예산 한 줄" 문장 템플릿 만들기 — "RAM N bytes, stack one frame, no allocation"
- [ ] 역질문 준비: "What's the C++ policy on the MCU side — exceptions off, any STL, any dynamic allocation after boot?"

---

## 8. 더 읽기

| 어디 | 무엇 |
|---|---|
| `C02 §1. volatile` | Q01의 원리. 보장/비보장 경계와 올바른 플래그 패턴 |
| `C02 §3. 메모리 할당 전략` | Q09의 원리. 스택/힙/정적/풀 비교와 풀 구현 |
| `C02 §4. 정렬, 패킹, 엔디언` | Q08의 원리. `packed` 비용과 엔디언 변환 |
| `C02 §5. 정수 승격과 산술 함정` | Q03의 원리. 함정 모음 |
| `C02 §7. Cortex-M 원자성과 critical section` | Q11의 원리. LDREX/STREX와 PRIMASK 선택 가이드 |
| `C02 §8. 임베디드 C++` | Q12~Q14의 원리. RAII guard, `constexpr`, placement new, 정적 초기화 순서, 템플릿 비용 |
| `C02 §9. MISRA 개요` | 동적 할당·재귀 금지 규칙의 출처 |
| `C02 §12. 직접 해보기` | 실습 1(volatile 어셈블리 확인), 실습 3(C++ 기능별 크기 측정) — 7절 체크리스트와 같은 작업 |
| `C01 §5. 부트: reset에서 main()까지` | Q04·Q14의 `.init_array` 타이밍 |
| `C01 §6. 링커 스크립트` | Q04·Q15의 `.map`·섹션 배치 |
| `C01 §9. 툴체인` | `-fstack-usage`, `size`, `objdump`, nano.specs |
| `S01 Q01, Q02, Q04, Q05, Q07` | Q06·Q08·Q09의 전체 코드와 엣지 케이스 |
| `C08 §7. 메모리 예산` | Q15의 tensor arena 쪽 근거 |
| `jd/J01` | 같은 언어 요건의 "업무 관점". 무엇을 만드는가 |
| `jd/J10` | Cortex-M/A와 툴체인 — 이 노트의 툴 항목이 거기서 더 깊게 다뤄진다 |
| `jd/J08` | 바로 앞 요건. 이 노트의 질문들이 "3년이 진짜인지"를 검증하는 수단이다 |
