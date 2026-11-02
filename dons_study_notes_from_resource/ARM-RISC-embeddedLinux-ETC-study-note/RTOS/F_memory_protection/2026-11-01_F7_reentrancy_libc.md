# F7. 재진입성과 C 라이브러리 — libc 안에 숨은 전역 변수

> **이 노트를 다 읽으면**: 재진입(reentrant) · 스레드 안전(thread-safe) · ISR 안전(async-signal-safe)을 예제 코드로 구분하고, newlib · picolibc에서 "숨은 상태"를 가진 함수를 `nm`으로 직접 찾아낼 수 있다 · newlib `struct _reent`와 `_impure_ptr`, FreeRTOS V11의 `configUSE_NEWLIB_REENTRANT` → `configUSE_C_RUNTIME_TLS_SUPPORT` 경로를 소스 줄 번호로 설명하고 태스크당 RAM 비용(76 B / 288 B + 지연 할당)을 숫자로 말할 수 있다 · `__malloc_lock` 훅과 retargetable lock(`__retarget_lock_*`)을 FreeRTOS로 구현하고, 락이 없을 때 힙이 깨져 HardFault가 나는 것을 재현할 수 있다 · picolibc의 TLS(`__thread`, `_set_tls`, 소프트웨어 스레드 포인터)와 Zephyr의 errno 세 가지 구현을 구분하고, ISR printf가 출력을 깨뜨리는 것을 보이고 지연 로깅으로 고칠 수 있다
> **왜 필요한가**: "strtok을 두 태스크에서 쓰면?", "ISR에서 printf 왜 안 되나?", "malloc은 thread-safe한가?"는 RTOS 면접 단골이다. 실무에서는 "가끔 로그가 깨진다", "몇 시간 뒤 malloc 안에서 HardFault", "errno가 엉뚱한 값" 같은 재현 어려운 버그의 원인이 libc 안의 전역 변수인 경우가 많다
> **Don 기준 난이도**: 재진입 함수 · 공유 버퍼 레이스 · ISR 짧게 쓰기는 SSD 펌웨어에서 이미 몸에 익었다 / 새로 배울 부분은 newlib · picolibc가 상태를 어디에 숨기는지, FreeRTOS · Zephyr가 그것을 태스크마다 바꿔 끼우는 구조(`_impure_ptr`, TLS), malloc 락 훅, Cortex-M에서 TLS가 동작하는 방식
> **선행 노트**: B11(MCU 툴체인 — libc 선택 · 크기, syscall stub), C2(컨텍스트 스위치), C7(크리티컬 섹션), D2(뮤텍스), D8(SPSC 링버퍼), E1(ISR 규칙), EL B2(glibc · musl의 errno · TLS). 이어지는 노트: F1(힙 알고리즘), F2(메모리 풀), F3(스택 크기), K6(로깅 인프라), H7(Zephyr 서브시스템)
> 🔁 **Bare-Metal 겹침**: BM F5(C 런타임 · libc — 재진입 · `_reent` · malloc 락 · errno 부분) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 RTOS 없는 super loop + ISR 구조에서의 libc 규칙(인터럽트 마스크로 만든 `__malloc_lock`, main과 ISR이 함께 쓰는 정적 버퍼)만 더한다. libc 선택 · 크기 비교는 RTOS B11이 주인

---

## 0. 큰 그림 — libc는 "전역 변수 묶음"이기도 하다

Don이 SSD 펌웨어에서 FTL 코드를 짤 때 가장 조심한 것은 "두 실행 흐름이 같은 메타데이터를 고치는 순간"이었다. 호스트 명령 처리 코어와 GC 코어가 같은 매핑 테이블 엔트리를 만지면 락이나 소유권 규칙이 있어야 한다. 그런데 그 규칙은 **내가 만든 전역 변수**에만 적용했다. C 라이브러리 함수는 "그냥 함수"라고 생각하고 불렀다.

C 라이브러리는 1970년대 단일 프로세스 유닉스를 위해 설계됐다. 그래서 편의를 위해 **함수 안에 전역 상태를 숨긴 API**가 여럿 있다.

- `strtok()`은 "다음에 어디서부터 자를지"를 기억한다.
- `rand()`는 난수 생성기의 상태를 기억한다.
- `asctime()`은 결과 문자열을 정적 버퍼에 담아 돌려준다.
- `errno`는 "마지막 에러"를 담는 전역 변수다.
- `printf()`는 `stdout`이라는 전역 `FILE` 구조체의 버퍼를 고친다.
- `malloc()`은 전역 힙의 free list를 고친다.

RTOS에서는 태스크 여러 개와 ISR이 같은 libc를 부른다. 그러면 이 숨은 전역 변수들이 **내가 락을 걸지 않은 공유 자원**이 된다. 코드 리뷰에서 보이지 않으니 "숨은 레이스"다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="90" y="22" font-size="13" text-anchor="middle" font-weight="bold">호출하는 쪽</text>
<text x="315" y="22" font-size="13" text-anchor="middle" font-weight="bold">libc 함수</text>
<text x="560" y="22" font-size="13" text-anchor="middle" font-weight="bold">숨은 전역 상태</text>
<rect x="10" y="50" width="160" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="90.0" y="74.5" font-size="13" text-anchor="middle">태스크 A (IMU 파서)</text>
<rect x="10" y="140" width="160" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="90.0" y="164.6" font-size="13" text-anchor="middle">태스크 B (텔레메트리)</text>
<rect x="10" y="230" width="160" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="90.0" y="254.6" font-size="13" text-anchor="middle">ISR (UART RX)</text>
<rect x="230" y="40" width="170" height="38" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="315.0" y="63.5" font-size="13" text-anchor="middle">strtok() · rand()</text>
<rect x="230" y="102" width="170" height="38" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="315.0" y="125.5" font-size="13" text-anchor="middle">strtol() → errno</text>
<rect x="230" y="164" width="170" height="38" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="315.0" y="187.6" font-size="13" text-anchor="middle">printf()</text>
<rect x="230" y="226" width="170" height="38" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="315.0" y="249.6" font-size="13" text-anchor="middle">malloc() · free()</text>
<rect x="450" y="40" width="220" height="62" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="560.0" y="67.2" font-size="12" text-anchor="middle" font-weight="bold">struct _reent (_impure_ptr)</text>
<text x="560.0" y="83.2" font-size="12" text-anchor="middle">errno · strtok 위치 · rand 상태</text>
<rect x="450" y="132" width="220" height="62" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="560.0" y="159.2" font-size="12" text-anchor="middle" font-weight="bold">stdout FILE</text>
<text x="560.0" y="175.2" font-size="12" text-anchor="middle">버퍼 포인터 _p · 남은 칸 _w</text>
<rect x="450" y="224" width="220" height="62" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="560.0" y="251.2" font-size="12" text-anchor="middle" font-weight="bold">malloc 힙</text>
<text x="560.0" y="267.2" font-size="12" text-anchor="middle">free list · sbrk 끝</text>
<line x1="170" y1="70" x2="230" y2="59" stroke="#888" stroke-width="0.8"/>
<line x1="170" y1="70" x2="230" y2="121" stroke="#888" stroke-width="0.8"/>
<line x1="170" y1="70" x2="230" y2="183" stroke="#888" stroke-width="0.8"/>
<line x1="170" y1="70" x2="230" y2="245" stroke="#888" stroke-width="0.8"/>
<line x1="170" y1="160" x2="230" y2="59" stroke="#888" stroke-width="0.8"/>
<line x1="170" y1="160" x2="230" y2="121" stroke="#888" stroke-width="0.8"/>
<line x1="170" y1="160" x2="230" y2="183" stroke="#888" stroke-width="0.8"/>
<line x1="170" y1="160" x2="230" y2="245" stroke="#888" stroke-width="0.8"/>
<line x1="170" y1="250" x2="230" y2="59" stroke="#888" stroke-width="0.8"/>
<line x1="170" y1="250" x2="230" y2="121" stroke="#888" stroke-width="0.8"/>
<line x1="170" y1="250" x2="230" y2="183" stroke="#888" stroke-width="0.8"/>
<line x1="170" y1="250" x2="230" y2="245" stroke="#888" stroke-width="0.8"/>
<line x1="400" y1="59" x2="448" y2="71" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="400" y1="121" x2="448" y2="71" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="400" y1="183" x2="448" y2="163" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="400" y1="245" x2="448" y2="255" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="400" y1="190" x2="448" y2="250" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5,4" marker-end="url(#ah1)"/>
<text x="340" y="300" font-size="12" text-anchor="middle">세 호출자가 같은 전역 상태를 고친다 — 락도, 태스크별 사본도 없으면 "숨은 레이스"</text>
<text x="340" y="320" font-size="12" text-anchor="middle">(점선: printf가 처음 출력할 때 버퍼를 malloc — newlib)</text>
</svg>
```

그림 1 — 태스크 둘과 ISR이 같은 libc 함수를 부르면, 함수들이 안에서 공유하는 상태(오른쪽)가 경합 대상이 된다. 셋 다 소스 코드에서는 "그냥 함수 호출"로 보인다

이 노트는 그 숨은 상태를 세 갈래로 나눠 다룬다.

| 숨은 상태 | 예 | 고치는 방법 | 절 |
|---|---|---|---|
| 호출마다 이어지는 상태 | `strtok`, `rand`, `asctime`, `errno` | `_r` 버전, 또는 태스크별 사본(newlib `_reent`, picolibc TLS) | §1, §2, §4, §5 |
| 진짜 공유 자원 | malloc 힙, `stdout` 버퍼 | 락 (`__malloc_lock`, `__retarget_lock_*`) 또는 소유자 하나 | §3, §7 |
| ISR에서는 위 둘 다 안 통함 | printf, malloc, `%f` | ISR은 기록만, 처리는 태스크로 (지연 로깅) | §6, §7 |

### 0.1 이 노트의 경계

- libc **고르기**(newlib · newlib-nano · picolibc), 크기 비교, syscall stub(`_write`, `_sbrk`), printf 수준 선택은 **B11**에서 다뤘다. 여기서는 B11 §3.2 끝에서 넘겨받은 질문 — "`__malloc_lock`이 2바이트짜리 빈 함수인데 RTOS에서는 어떻게 되나" — 부터 시작한다.
- ISR에서 하면 안 되는 일의 일반 규칙(FromISR, 블로킹, UART 바쁜 대기 2 ms 계산)은 **E1**에 있다. 여기서는 **libc 쪽 이유**(FILE 상태, malloc, 스택, `_impure_ptr`)만 새로 본다.
- 뮤텍스 자체(소유권, 재귀, 상속)는 **D2**, 링버퍼 설계(acquire/release, 가득 참 판정)는 **D8**을 그대로 쓴다.
- 힙 알고리즘(heap_1 ~ heap_5, 단편화)은 **F1**, 고정 블록 풀은 **F2**.
- Linux에서 errno가 TLS(`__errno_location()`)라는 것은 **EL B2 §1.1**에서 봤다. MCU에서 달라지는 점은 "스레드 포인터 레지스터가 없다"와 "RTOS가 직접 바꿔 끼워야 한다"이다(§5).

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 25 MHz) + FreeRTOS V11.3.2(랩 템플릿, `--specs=nano.specs`) · Ubuntu `arm-none-eabi-gcc` 14.2.1 + **newlib 4.6.0**(`libnewlib-arm-none-eabi 4.6.0.20260123-1`) · **picolibc 1.8.10**(`picolibc-arm-none-eabi`) · Zephyr v4.4.2(Ubuntu 툴체인 `gnuarmemb`과 Zephyr SDK 1.0.1 두 가지). 시간 숫자는 `ICOUNT=1`(명령 1개 = 32 ns)과 MPS2 FPGAIO 카운터(25 MHz)로 쟀다 — **QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 미모델링). 레이스 재현은 타이머 IRQ 주기에 민감하다. 같은 바이너리는 ICOUNT에서 매번 같은 결과를 내지만, 주기를 바꾸면 결과가 바뀐다(§3.2에서 일부러 보여 준다).

---

## 1. 세 가지 "안전" — 재진입 · 스레드 안전 · ISR 안전

> 🔁 이 절은 Bare-Metal BM F5(재진입 부분)와 겹친다 (주인: 이 노트).

### 1.1 직관

함수를 "요리사"라고 하자.

- **재진입(reentrant)** 요리사는 자기 도마(지역 변수)만 쓴다. 요리 중간에 누가 끼어들어 같은 요리사에게 다른 요리를 시켜도(인터럽트, 선점), 끼어든 요리를 끝내고 돌아오면 원래 요리가 그대로다.
- **스레드 안전(thread-safe)** 요리사는 공용 도마를 쓰지만 "사용 중" 팻말(락)을 건다. 다른 사람은 팻말이 내려갈 때까지 기다린다. 기다릴 수 있는 사람(태스크)에게는 안전하다.
- **ISR 안전(async-signal-safe)** 은 "기다릴 수 없는 손님"(ISR)이 불러도 되는 요리사다. 팻말을 보고 기다릴 수 없으니, **팻말이 필요 없는**(재진입) 데다 **오래 걸리지 않는** 요리사여야 한다.

### 1.2 정의

| 성질 | 뜻 | 보장하는 것 | 대표 예 |
|---|---|---|---|
| 재진입 (reentrant) | 실행 도중 같은 함수가 다시 불려도(중첩) 두 호출 모두 맞다. 공유 가변 상태가 없다 | 중첩 호출 · 선점 · 인터럽트 모두 OK | `strtok_r`, `memcpy`, `strlen`, `asctime_r` |
| 스레드 안전 (thread-safe) | 여러 스레드가 동시에 불러도 맞다. 락으로 직렬화해도 된다 | 태스크끼리 OK. ISR은 보장 안 함 | `__malloc_lock`을 단 `malloc`, 뮤텍스를 단 로거 |
| ISR 안전 (async-signal-safe) | 비동기 문맥(시그널 핸들러 · ISR)에서 불러도 된다. 블록하지 않는다 | ISR에서 OK | 재진입 + 블로킹 없음 + 상한 있는 시간 |
| 어느 것도 아님 | 전역 상태를 락 없이 고친다 | 단일 흐름에서만 맞다 | `strtok`, `rand`, `asctime`, 전역 `errno` |

두 가지를 꼭 구분한다.

1. **스레드 안전 ≠ 재진입.** 락으로 만든 스레드 안전 함수는 ISR에서 부르면 안 된다. ISR이 락을 기다리면 락 주인(끊긴 태스크)은 ISR이 끝나야 돌 수 있으니 영원히 못 풀어 준다(데드락). RTOS라면 assert가 잡는다(§3.3, §6.3).
2. **재진입 ≠ 공짜.** 재진입 함수도 ISR에서 오래 걸리면 안 된다. `snprintf`는 정수만 포맷해도 15 µs, 스택 400 B가 넘는다(§6.4).

POSIX는 시그널 핸들러에서 불러도 되는 함수 목록을 따로 둔다(XSH 2.4.3 "Signal Actions"의 async-signal-safe 목록). `printf`, `malloc`은 그 목록에 없다. MCU의 ISR은 시그널 핸들러와 같은 처지다 — 메인 흐름의 아무 명령 사이에나 끼어든다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="30" width="660" height="200" rx="5" fill="#4a7bd0" fill-opacity="0.05" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="24" y="50" font-size="12" text-anchor="start" font-weight="bold">스레드 안전 (thread-safe) — 여러 태스크가 동시에 불러도 결과가 맞다 (락을 써서라도)</text>
<rect x="30" y="64" width="300" height="150" rx="5" fill="#3f9a6b" fill-opacity="0.08" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="42" y="84" font-size="12" text-anchor="start" font-weight="bold">재진입 (reentrant)</text>
<text x="42" y="102" font-size="12" text-anchor="start">공유 상태가 없어서 끼어들어도 맞다</text>
<rect x="45" y="116" width="270" height="86" rx="5" fill="#d0564a" fill-opacity="0.1" stroke="#d0564a" stroke-width="1.3"/>
<text x="180.0" y="147.2" font-size="12" text-anchor="middle" font-weight="bold">ISR 안전 (async-signal-safe)</text>
<text x="180.0" y="163.2" font-size="12" text-anchor="middle">재진입 + 블록 안 함 + 짧음</text>
<text x="180.0" y="179.2" font-size="12" text-anchor="middle">memcpy · strlen · strtok_r</text>
<rect x="350" y="64" width="300" height="150" rx="5" fill="#e08a3c" fill-opacity="0.08" stroke="#e08a3c" stroke-width="1.3"/>
<text x="500.0" y="119.2" font-size="12" text-anchor="middle" font-weight="bold">락으로만 안전한 것 (재진입 아님)</text>
<text x="500.0" y="135.2" font-size="12" text-anchor="middle">malloc + __malloc_lock</text>
<text x="500.0" y="151.2" font-size="12" text-anchor="middle">printf + stdout 뮤텍스</text>
<text x="500.0" y="167.2" font-size="12" text-anchor="middle">→ 태스크끼리는 OK, ISR에서는 데드락·assert</text>
<rect x="10" y="242" width="660" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="340.0" y="268.2" font-size="12" text-anchor="middle">어느 쪽도 아님: strtok · rand · asctime · 전역 errno (태스크별 사본 없이)</text>
</svg>
```

그림 2 — 세 가지 안전의 관계. ISR 안전은 재진입의 부분집합이고, "락으로만 안전한" 함수는 스레드 안전이지만 재진입이 아니다

### 1.3 예제 1 — 정적 버퍼를 돌려주는 함수를 main과 ISR이 같이 부르면 (bare-metal)

무엇을 확인하는 코드인지: `asctime()`이나 `inet_ntoa()`처럼 **정적 버퍼를 돌려주는 함수**가 인터럽트에 끊기면 무엇이 되는지. RTOS도 필요 없다 — main 루프와 ISR 둘이면 충분하다. 이것이 Bare-Metal BM F5와 겹치는 핵심 장면이다.

```c
/* ex0 — a function that returns a static buffer, called from main AND from an ISR (bare metal) */
#include <stdint.h>
#include <string.h>
#include "board.h"
#ifndef MODE
#define MODE 0                                       /* 0: static buffer, 1: caller-provided buffer */
#endif
static void spin(volatile int n) { while (n--) { } }
#if MODE == 0
static const char *to_hex(uint32_t v)                /* like asctime(), inet_ntoa(): NOT reentrant */
{
    static char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) { buf[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15]; spin(20); }
    buf[10] = '\0';
    return buf;
}
#define HEX(v, b) ((void)(b), to_hex(v))
#else
static const char *to_hex_r(uint32_t v, char *buf)   /* like asctime_r(): state lives in the caller */
{
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) { buf[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15]; spin(20); }
    buf[10] = '\0';
    return buf;
}
#define HEX(v, b) to_hex_r(v, b)
#endif
static volatile unsigned isr_calls;
void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    char b[11];
    (void)HEX(0xDEADBEEFu, b);                       /* e.g. an ISR formatting a fault code */
    isr_calls++;
}
int main(void)
{
    TIMER_RELOAD(TIMER0_BASE) = 3001; TIMER_VALUE(TIMER0_BASE) = 3001;   /* ~120 us */
    TIMER_CTRL(TIMER0_BASE) = 1 | 8; NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;
    unsigned bad = 0;
    for (int i = 0; i < 2000; i++) {
        char b[11];
        const char *s = HEX(0x12345678u, b);
        if (strcmp(s, "0x12345678") != 0 && bad++ < 3) board_printf_raw("  main got %s\n", s);
    }
    TIMER_CTRL(TIMER0_BASE) = 0;
    board_printf_raw("MODE=%s: bad=%u of 2000, isr calls=%u\n", MODE ? "caller buffer" : "static buffer", bad, isr_calls);
    board_exit(0);
}
```

```sh
for m in 0 1; do
  make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/F7/ex0/main.c \
       OUT=/var/tmp/rtos/F7/ex0_$m EXTRA_CFLAGS=-DMODE=$m >/dev/null
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F7/ex0_$m/app.elf
done
```

```text
  main got 0xdeadbe78
  main got 0xdeadbe78
  main got 0xdeadbe78
MODE=static buffer: bad=494 of 2000, isr calls=544
[board_exit] code=0
MODE=caller buffer: bad=0 of 2000, isr calls=533
[board_exit] code=0
```

출력에서 볼 것: `0xdeadbe78`은 **두 호출의 결과가 섞인 값**이다. main이 `0x12345678`의 앞 여섯 자리를 쓰던 중 ISR이 같은 버퍼에 `0xdeadbeef`를 통째로 쓰고 나갔고, main이 남은 두 자리 `78`을 이어 썼다. 2000번 중 494번(25%). 버퍼를 호출자가 주는 `_r` 스타일로 바꾸면 0번이다. "결과를 정적 버퍼로 돌려주는 API"는 재진입이 아니고, **락을 걸어도 ISR 문제는 못 고친다**(ISR은 기다릴 수 없다). 고치는 방법은 상태를 호출자에게 넘기는 것뿐이다.

### 1.4 libc의 대표 함정 함수와 짝

| 함정 함수 | 숨은 상태 | 재진입 짝 | newlib-nano에서 상태가 사는 곳 |
|---|---|---|---|
| `strtok` | 다음 토큰 위치 | `strtok_r(s, d, &save)` | `_reent->_misc` (처음 쓸 때 malloc 80 B) |
| `rand`, `srand` | LCG 상태 | `rand_r(&seed)` 또는 직접 만든 PRNG | `_reent->_r48` (처음 쓸 때 malloc) |
| `asctime`, `ctime` | 결과 문자열 버퍼 | `asctime_r(tm, buf)` | `_reent`의 asctime 버퍼 (malloc) |
| `localtime`, `gmtime` | 결과 `struct tm` | `localtime_r`, `gmtime_r` | `_reent->_localtime_buf` (malloc) |
| `strerror` | 결과 문자열 | `strerror_r` | `_reent`의 버퍼 |
| `errno` | 마지막 에러 번호 | (짝 없음 — 태스크별 사본 필요) | `_reent->_errno` |
| `printf` 계열 | `stdout` FILE 버퍼, `%f` 변환용 Bigint | 단일 소유자(로그 태스크) | 전역 `__sf`, `_reent->_mp` 등 |
| `malloc`, `free` | 힙 free list | 락 훅 | 전역 `__malloc_free_list` |

`_r` 짝이 있는 것은 고치기 쉽다 — 코딩 규칙으로 "`_r` 버전만 쓴다"를 정하고 리뷰 체크리스트에 넣는다. 문제는 `errno`, `printf`, `malloc`처럼 짝이 없는 것이다. 이것들은 libc와 RTOS의 협업(§2~§5)이 필요하다.

### 1.5 예제 2 — 숨은 상태를 가진 함수를 `nm`으로 직접 찾기

무엇을 확인하는 코드인지: 위 표를 외우는 대신, 라이브러리 아카이브에서 "현재 `struct _reent`를 참조하는 함수"(newlib)와 "`__thread` 변수"(picolibc)를 직접 뽑는다. 툴체인이 바뀌면 이 스크립트를 다시 돌리면 된다.

```sh
#!/bin/bash
# hidden_state.sh — which libc objects keep state outside the caller's arguments?
L=/usr/lib/arm-none-eabi/lib/thumb/v7-m/nofp; P=/usr/lib/picolibc/arm-none-eabi/lib/thumb/v7-m/nofp
n_obj=$(arm-none-eabi-nm $L/libc_nano.a 2>/dev/null | grep -c "\.o:$")
n_imp=$(arm-none-eabi-nm -A $L/libc_nano.a 2>/dev/null | grep -c " U _impure_ptr$")
echo "newlib-nano: $n_imp of $n_obj objects reference _impure_ptr (the current struct _reent)"
echo "  ...of those, objects that ALSO call malloc (lazy _REENT_CHECK allocation or stdio buffers):"
arm-none-eabi-nm -A $L/libc_nano.a 2>/dev/null | awk -F: '/ U _impure_ptr$/{i[$2]=1} / U (malloc|_malloc_r|_calloc_r)$/{m[$2]=1}
  END{for(o in i) if(o in m) {sub(/libc_a-/,"",o); sub(/\.o$/,"",o); printf "%s ", o}; print ""}' | fold -w 110 -s
echo "picolibc: TLS (__thread) variables = per-thread hidden state"
arm-none-eabi-objdump -t $P/libc.a 2>/dev/null | awk '($0 ~ /[.]tbss|[.]tdata/) && $NF !~ /^[.]/ {printf "  %-14s %4d B\n", $NF, strtonum("0x"$(NF-1))}' | sort -u | grep -v " 0 B$" | paste - - - | column -t
```

```text
newlib-nano: 180 of 596 objects reference _impure_ptr (the current struct _reent)
  ...of those, objects that ALSO call malloc (lazy _REENT_CHECK allocation or stdio buffers):
rand btowc lcong48 fopencookie strsignal wcsdup ungetc srand48 wcstod lcltime hash lrand48 calloc fmemopen
signal mrand48 random tmpnam l64a malloc gmtime funopen drand48 open_memstream wctob asctime gets_chk strtok
setvbuf seed48
picolibc: TLS (__thread) variables = per-thread hidden state
_asctime_buf    26  B  _l64a_buf.0   8   B  _locale     4    B
_localtime_buf  36  B  _rand48       14  B  _rand_next  8    B
_signal_buf.0   24  B  _strtok_last  4   B  ecvt_buf    18   B
ecvtf_buf       10  B  errno         4   B  fcvt_buf    326  B
fcvtf_buf       48  B  sseed.0       6   B
```

출력에서 볼 것:

- newlib-nano 오브젝트 596개 중 **180개가 `_impure_ptr`를 읽는다** — 즉 현재 `struct _reent`의 무언가(대부분 errno)를 고친다. `strtol`, `atof`, `fopen`처럼 "errno만 쓰는" 함수가 대부분이다.
- 그중 **`strtok`, `rand`, `asctime`, `gmtime` 등은 malloc까지 부른다.** newlib-nano는 RAM을 아끼려고 `_REENT_SMALL`로 빌드되어, 이 함수들의 상태를 `struct _reent` 안에 두지 않고 **처음 쓸 때 malloc한다**. "strtok 하나 불렀는데 malloc이 돈다"는 뜻이다 — ISR에서 strtok을 부르면 ISR 안에서 malloc이 돈다.
- picolibc는 같은 상태를 `__thread` 변수로 둔다. 14개, 다 합쳐 약 540 B지만 **링크된 함수의 것만** 이미지에 들어간다(§5의 예제에서는 errno 4 B뿐이었다). `fcvt_buf`(326 B)는 `fcvt()`를 쓸 때만 붙는다.

newlib-nano의 `strtok`이 실제로 무엇을 하는지 디스어셈블로 보면 더 분명하다(`ar x`로 꺼낸 `libc_a-strtok.o`, 앞부분):

```asm
00000000 <strtok>:
   0:	4b17      	ldr	r3, [pc, #92]	@ (60 <strtok+0x60>)     ; &_impure_ptr
   2:	b530      	push	{r4, r5, lr}
   4:	681d      	ldr	r5, [r3, #0]                            ; r5 = _impure_ptr (현재 _reent)
   6:	4604      	mov	r4, r0
   8:	6c6a      	ldr	r2, [r5, #68]	@ 0x44                  ; r2 = _reent->_misc
   a:	b083      	sub	sp, #12
   c:	b132      	cbz	r2, 1c <strtok+0x1c>                    ; 아직 없으면 할당하러
   ...
  18:	f7ff bffe 	b.w	0 <__strtok_r>                          ; 있으면 __strtok_r(s, d, &_misc->last)
  1c:	2050      	movs	r0, #80	@ 0x50
  20:	f7ff fffe 	bl	0 <malloc>                              ; _misc = malloc(80)
  28:	6468      	str	r0, [r5, #68]	@ 0x44
```

(오른쪽 `;` 주석은 내가 붙였다.) newlib의 `strtok`은 사실 `__strtok_r`에 "현재 `_reent` 안의 저장 위치"를 넘기는 얇은 껍데기다. 그래서 **`_impure_ptr`를 태스크마다 바꿔 주면 `strtok`조차 태스크별로 안전해진다** — 다음 절의 예제 3이 이것을 보여 준다.

### 1.6 Don 경험과 연결

SSD 펌웨어에서 printf 계열을 hot path에 안 쓴 이유는 보통 "느려서"였다. 이 절의 결론은 하나 더 있다 — **libc 함수는 숨은 전역 상태를 갖고 있어서, 코어 · 인터럽트 사이에서 부르면 내가 만든 공유 변수와 똑같이 취급해야 한다.** 멀티코어 SSD 컨트롤러에서 코어마다 따로 링크된 이미지라면 libc 전역도 코어마다 따로였겠지만, 한 이미지 안의 RTOS 태스크들은 libc 전역을 **하나** 공유한다.

### 1.7 함정

- "`_r` 버전이 있으니 안전" — `strtok_r`의 `save` 포인터를 **전역 변수**로 선언하면 다시 공유 상태다. `save`는 지역 변수여야 한다.
- "재진입 함수니까 ISR에서 OK" — `snprintf`는 재진입이지만(newlib-nano 정수 포맷은 malloc을 안 부른다, 예제 5) `%f`는 malloc을 부른다(§6.4). 그리고 둘 다 길다.
- "락을 걸었으니 ISR에서도 OK" — 반대다. 락은 ISR에서 데드락이나 assert를 만든다(§3.3).

---

## 2. newlib의 답 — `struct _reent`와 `_impure_ptr`

### 2.1 구조 — 숨은 상태를 한 구조체에 모아 두고 포인터 하나로 가리킨다

newlib은 숨은 상태를 거의 다 **`struct _reent`** 하나에 모았다. 그리고 "지금 쓰는 `_reent`"를 전역 포인터 **`_impure_ptr`** 가 가리킨다. 기본값은 libc 안의 `_impure_data` 하나다. 모든 libc 함수는 `_impure_ptr`를 통해 상태를 읽고 쓴다. `errno`도 매크로다.

```c
/* /usr/include/newlib/sys/errno.h (newlib 4.6.0) */
#define errno (*__errno())           /* __errno() 는 &_impure_ptr->_errno 를 돌려준다 */
/* /usr/include/newlib/sys/reent.h */
extern struct _reent *_impure_ptr __ATTRIBUTE_IMPURE_PTR__;
# define _REENT _impure_ptr          /* __getreent()를 정의하지 않는 단일 코어 구성 */
```

그래서 RTOS가 할 일은 단순하다 — **태스크마다 `struct _reent`를 하나씩 주고, 문맥 전환 때 `_impure_ptr`를 바꿔 끼운다.** SSD 펌웨어로 치면 "코어별 컨텍스트 블록 포인터를 전환 때 바꿔 주는 것"이다.

`struct _reent`에 무엇이 있는지 헤더(`sys/reent.h`)에서 직접 보면, 구성에 따라 두 모양이 있다.

| 필드 | `_REENT_SMALL` (newlib-nano) | 기본 (newlib full) |
|---|---|---|
| `_errno` | 맨 앞 (오프셋 0) | 맨 앞 (오프셋 0) |
| `_stdin`, `_stdout`, `_stderr` | 전역 `__sf`를 가리키는 포인터 | 같음 |
| strtok 위치, asctime 버퍼, localtime 결과, rand48, mbstate들 | `_misc`, `_r48`, `_asctime_buf`… **포인터** — 처음 쓸 때 malloc (`_REENT_CHECK`) | 구조체 안 `_new._reent` union에 **직접** |
| `%f` 변환용 Bigint (`_mp`, `_freelist`, `_result`) | `_mp` 포인터 — 처음 쓸 때 malloc | 구조체 안 |
| `_emergency` 버퍼 | 포인터 | 배열 |
| 크기 (Cortex-M3, 예제 5) | **76 B** | **288 B** |

`_REENT_CHECK` 매크로(같은 헤더 473행)가 지연 할당의 정체다.

```c
#define _REENT_CHECK(var, what, type, size, init) do { \
  struct _reent *_r = (var); \
  if (_r->what == NULL) { \
    _r->what = (type)malloc(size); \
    __reent_assert(_r->what); \
    init; \
  } \
} while (0)
```

Ubuntu 빌드는 `_REENT_CHECK_VERIFY`가 켜져 있어서(`nano/newlib.h`), 이 malloc이 실패하면 `__assert_func`로 죽는다. **힙이 꽉 찬 상태에서 처음 `strtok`을 부른 태스크는 assert로 죽는다** — 그것도 "REENT malloc succeeded"라는 알쏭달쏭한 메시지로.

두 구성 매크로를 툴체인에서 직접 확인했다.

```sh
for f in /usr/include/newlib/newlib.h /usr/include/newlib/nano/newlib.h; do echo "== $f"
  grep -E "REENT_SMALL|RETARGETABLE_LOCKING|REENT_CHECK_VERIFY|REENT_THREAD_LOCAL|NANO_MALLOC" $f; done
```

```text
== /usr/include/newlib/newlib.h
/* #undef _NANO_MALLOC */
#define _REENT_CHECK_VERIFY 1
/* #undef _RETARGETABLE_LOCKING */
/* #undef _WANT_REENT_SMALL */
/* #undef _WANT_REENT_THREAD_LOCAL */
== /usr/include/newlib/nano/newlib.h
#define _NANO_MALLOC 1
#define _REENT_CHECK_VERIFY 1
/* #undef _RETARGETABLE_LOCKING */
#define _WANT_REENT_SMALL 1
/* #undef _WANT_REENT_THREAD_LOCAL */
```

`_WANT_REENT_THREAD_LOCAL`은 newlib 4.x에 생긴 "errno 등을 `_reent` 대신 `__thread` 변수로" 구성인데(`sys/errno.h`의 `#define errno (_tls_errno)` 분기), Ubuntu 빌드는 꺼져 있다. `_RETARGETABLE_LOCKING`이 꺼져 있다는 것은 §3에서 중요해진다.

### 2.2 예제 3 — 두 태스크가 `strtok`을 쓰면

무엇을 확인하는 코드인지: 같은 우선순위의 두 태스크(1 kHz time slicing)가 각자 다른 문자열을 `strtok`으로 자른다. 토큰 사이에 150 µs씩 "파싱"을 해서 중간에 tick 선점이 걸리게 했다. ① `strtok` ② `strtok_r` ③ `strtok` + `configUSE_NEWLIB_REENTRANT=1` 세 경우.

```c
/* ex1 — two tasks tokenize different strings. MODE 0: strtok, 1: strtok_r */
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#ifndef MODE
#define MODE 0
#endif
#define ITERS 400
typedef struct { const char *name, *line, *delim; const char *tok[4]; } job_t;
static const job_t jobs[2] = {
    { "imu", "ALT=120,ROLL=-3,PITCH=4,YAW=90", ",", { "ALT=120", "ROLL=-3", "PITCH=4", "YAW=90" } },
    { "gps", "GPS:37.1:-122.3:FIX3",           ":", { "GPS", "37.1", "-122.3", "FIX3" } },
};
static volatile unsigned bad[2], good[2], done;
static void spin_us(uint32_t us) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < us) { } }
static void worker(void *arg)
{
    const job_t *j = &jobs[(int)arg];
    char buf[48];
    for (int it = 0; it < ITERS; it++) {
        strcpy(buf, j->line);
#if MODE == 0
        char *p = strtok(buf, j->delim);
#else
        char *save;
        char *p = strtok_r(buf, j->delim, &save);
#endif
        for (int k = 0; k < 4; k++) {
            if (p == NULL || strcmp(p, j->tok[k]) != 0) {
                if (bad[(int)arg]++ < 3)
                    board_printf_raw("  [%s] it=%d token%d expected '%s' got '%s'\n", j->name, it, k,
                                     j->tok[k], p ? p : "(null)");
                break;
            }
            good[(int)arg]++;
            spin_us(150);                       /* "parse" the field: widens the window */
#if MODE == 0
            p = strtok(NULL, j->delim);
#else
            p = strtok_r(NULL, j->delim, &save);
#endif
        }
    }
    taskENTER_CRITICAL(); done++; taskEXIT_CRITICAL();
    vTaskSuspend(NULL);
}
static void reporter(void *arg)
{
    (void)arg;
    while (done < 2) vTaskDelay(10);
    board_printf_raw("MODE=%s NEWLIB_REENTRANT=%d: imu good=%u bad=%u, gps good=%u bad=%u\n",
                     MODE ? "strtok_r" : "strtok", configUSE_NEWLIB_REENTRANT,
                     good[0], bad[0], good[1], bad[1]);
    board_exit(0);
}
int main(void)
{
    xTaskCreate(worker, "imu", 512, (void *)0, 1, NULL);
    xTaskCreate(worker, "gps", 512, (void *)1, 1, NULL);
    xTaskCreate(reporter, "rep", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
cat /Users/donh/.rtos_scratch/F7/run.sh       # 빌드 + ICOUNT=1 실행 도우미 (아래)
R=/Users/donh/.rtos_scratch/F7/run.sh
$R ex1 ex1_0; $R ex1 ex1_1 EXTRA_CFLAGS=-DMODE=1; $R ex1 ex1_0r EXTRA_CFLAGS=-DconfigUSE_NEWLIB_REENTRANT=1
```

```sh
#!/bin/bash
# run.sh <exdir> <outname> [make args...] — build FreeRTOS example and run with ICOUNT=1
ex=$1; out=$2; shift 2
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS="/Users/donh/.rtos_scratch/F7/$ex/main.c" OUT=/var/tmp/rtos/F7/$out "$@" >/var/tmp/rtos/F7/$out.build 2>&1 || { cat /var/tmp/rtos/F7/$out.build; exit 1; }
grep -i "warn" /var/tmp/rtos/F7/$out.build
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F7/$out/app.elf ${TMO:-20}
```

```text
  [imu] it=1 token3 expected 'YAW=90' got 'FIX3'
  [gps] it=1 token3 expected 'FIX3' got 'YAW=90'
  [imu] it=3 token3 expected 'YAW=90' got 'FIX3'
  [gps] it=3 token3 expected 'FIX3' got 'YAW=90'
  [imu] it=5 token3 expected 'YAW=90' got 'FIX3'
  [gps] it=5 token3 expected 'FIX3' got 'YAW=90'
MODE=strtok NEWLIB_REENTRANT=0: imu good=1400 bad=200, gps good=1400 bad=200
[board_exit] code=0
MODE=strtok_r NEWLIB_REENTRANT=0: imu good=1600 bad=0, gps good=1600 bad=0
[board_exit] code=0
MODE=strtok NEWLIB_REENTRANT=1: imu good=1600 bad=0, gps good=1600 bad=0
[board_exit] code=0
```

출력에서 볼 것:

- `strtok` 그대로: IMU 태스크가 네 번째 토큰을 달라고 했는데 **GPS 문자열의 토큰 `FIX3`** 를 받았다. 두 태스크가 `_impure_data._misc`의 "다음 위치" 하나를 공유하기 때문이다. 400번 중 200번(짝수 반복마다 선점 위치가 맞물렸다). 예외도 크래시도 없이 **틀린 데이터**가 나온다 — 이게 가장 위험한 증상이다.
- `strtok_r`: 0.
- `strtok` + `configUSE_NEWLIB_REENTRANT=1`: **역시 0.** newlib의 `strtok`은 상태를 `_impure_ptr->_misc`에 두므로, FreeRTOS가 `_impure_ptr`를 태스크마다 바꾸면 `strtok`도 태스크별로 안전해진다. 다만 각 태스크의 첫 `strtok`이 malloc 88 B를 한다(예제 5). picolibc에서는 같은 효과를 TLS가 낸다(§5).

규칙은 여전히 "`strtok_r`을 써라"다. `configUSE_NEWLIB_REENTRANT`에 기대면 ISR에서 부른 `strtok`이 **끊긴 태스크의** `_reent`를 고친다는 문제가 남는다(§2.6).

### 2.3 예제 4 — errno가 다른 태스크에게 덮인다

무엇을 확인하는 코드인지: 파서 태스크 A가 `strtol` 오버플로를 `errno == ERANGE`로 검사하는데, 그 사이 우선순위가 높은 태스크 B가 `errno = 0`(흔한 "호출 전 errno 지우기" 관용구)을 한다. ① 기본 ② `configUSE_NEWLIB_REENTRANT=1` ③ `configUSE_POSIX_ERRNO=1`.

```c
/* ex2 — does task B's errno write clobber task A's errno? (newlib-nano) */
#include <errno.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#define N 200
#if configUSE_POSIX_ERRNO == 1
extern int FreeRTOS_errno;                      /* defined in tasks.c, no header declares it */
#endif
static volatile unsigned lost, seen, b_runs;
static volatile int *errno_addr[2];
static void spin_us(uint32_t us) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < us) { } }
static void task_a(void *arg)                   /* parser: relies on errno to detect overflow */
{
    (void)arg;
    errno_addr[0] = &errno;
    for (int i = 0; i < N; i++) {
        errno = 0;
        long v = strtol("99999999999", NULL, 10);   /* > LONG_MAX → ERANGE */
        spin_us(300);                                /* meanwhile B may run */
        if (errno == ERANGE) seen++;
        else if (lost++ == 0)
            board_printf_raw("  A: v=%d but errno=%d (ERANGE=%d lost!)\n", (int)v, errno, ERANGE);
    }
    board_printf_raw("NEWLIB_REENTRANT=%d POSIX_ERRNO=%d: ERANGE seen=%u lost=%u (B ran %u times)\n",
                     configUSE_NEWLIB_REENTRANT, configUSE_POSIX_ERRNO, seen, lost, b_runs);
    board_printf_raw("  &errno in A=0x%x, in B=0x%x\n", (unsigned)errno_addr[0], (unsigned)errno_addr[1]);
#if configUSE_POSIX_ERRNO == 1
    board_printf_raw("  FreeRTOS_errno (separate variable) = %d\n", FreeRTOS_errno);
#endif
    board_exit(0);
}
static void task_b(void *arg)                   /* telemetry: clears errno before its own call */
{
    (void)arg;
    errno_addr[1] = &errno;
    for (;;) {
        vTaskDelay(1);
        errno = 0;
        (void)strtol("42", NULL, 10);
        b_runs++;
    }
}
int main(void)
{
    xTaskCreate(task_a, "A", 512, NULL, 1, NULL);
    xTaskCreate(task_b, "B", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
$R ex2 ex2_0; $R ex2 ex2_r EXTRA_CFLAGS=-DconfigUSE_NEWLIB_REENTRANT=1; $R ex2 ex2_p EXTRA_CFLAGS=-DconfigUSE_POSIX_ERRNO=1
```

```text
  A: v=2147483647 but errno=0 (ERANGE=34 lost!)
NEWLIB_REENTRANT=0 POSIX_ERRNO=0: ERANGE seen=139 lost=61 (B ran 61 times)
  &errno in A=0x2000000c, in B=0x2000000c
[board_exit] code=0
NEWLIB_REENTRANT=1 POSIX_ERRNO=0: ERANGE seen=200 lost=0 (B ran 61 times)
  &errno in A=0x20001918, in B=0x200021d8
[board_exit] code=0
  A: v=2147483647 but errno=0 (ERANGE=34 lost!)
NEWLIB_REENTRANT=0 POSIX_ERRNO=1: ERANGE seen=139 lost=61 (B ran 61 times)
  &errno in A=0x2000000c, in B=0x2000000c
  FreeRTOS_errno (separate variable) = 0
[board_exit] code=0
```

출력에서 볼 것:

- 기본 구성에서 `&errno`는 두 태스크 모두 `0x2000000c` — `_impure_data`의 맨 앞(`_errno`)이다(`nm`으로 보면 `_impure_ptr`가 `0x20000008`, 76 B짜리 `_impure_data`가 `0x2000000c`). B가 61번 돌았고, **61번 모두 A의 ERANGE가 지워졌다.** A는 `LONG_MAX`(2147483647)를 "정상 값"으로 받아들인다. 고도 센서 파서였다면 오버플로가 21 km 고도가 된다.
- `configUSE_NEWLIB_REENTRANT=1`: `&errno`가 태스크마다 다르다(각 TCB 안의 `_reent`). lost 0.
- **`configUSE_POSIX_ERRNO=1`은 libc errno를 고치지 않는다.** 이름 때문에 가장 많이 헷갈리는 옵션이다. 이것은 FreeRTOS가 **별도의 전역 변수 `FreeRTOS_errno`** 를 태스크마다 저장 · 복원해 주는 기능이고, FreeRTOS+ 라이브러리(FreeRTOS-Plus-FAT 같은)가 쓰라고 만든 것이다. libc의 `errno`(`_impure_ptr->_errno`)와는 아무 관계가 없다. 게다가 V11.3.2의 어떤 헤더도 `FreeRTOS_errno`를 선언하지 않는다 — `tasks.c` 501행에 정의만 있어서 `extern int FreeRTOS_errno;`를 직접 써야 했다.

### 2.4 FreeRTOS V11이 `_impure_ptr`를 바꾸는 곳 — 소스로 따라가기

V11에서 이 기능은 **"C 런타임 TLS 지원"이라는 일반 장치**의 한 사용 예가 됐다. `configUSE_NEWLIB_REENTRANT`라는 이름은 남아 있지만, 하는 일은 헤더 하나를 끌어오는 것뿐이다.

```c
/* include/FreeRTOS.h 120~130행 (V11.3.2) */
#ifndef configUSE_NEWLIB_REENTRANT
    #define configUSE_NEWLIB_REENTRANT    0
#endif
#if ( configUSE_NEWLIB_REENTRANT == 1 )
    #include "newlib-freertos.h"
#endif
```

```c
/* include/newlib-freertos.h (V11.3.2) — 주석 일부 생략 */
#include <reent.h>
#define configUSE_C_RUNTIME_TLS_SUPPORT    1
#ifndef configTLS_BLOCK_TYPE
    #define configTLS_BLOCK_TYPE           struct _reent
#endif
#ifndef configINIT_TLS_BLOCK
    #define configINIT_TLS_BLOCK( xTLSBlock, pxTopOfStack )    _REENT_INIT_PTR( &( xTLSBlock ) )
#endif
#ifndef configSET_TLS_BLOCK
    #define configSET_TLS_BLOCK( xTLSBlock )    ( _impure_ptr = &( xTLSBlock ) )
#endif
#ifndef configDEINIT_TLS_BLOCK
    #define configDEINIT_TLS_BLOCK( xTLSBlock )    _reclaim_reent( &( xTLSBlock ) )
#endif
```

이 네 매크로를 커널이 쓰는 곳은 `tasks.c` 다섯 군데다.

| 시점 | `tasks.c` (V11.3.2) | 하는 일 |
|---|---|---|
| TCB 정의 | 433~434행 `configTLS_BLOCK_TYPE xTLSBlock;` | TCB 안에 `struct _reent`를 통째로 둔다 (그래서 TCB가 76 B / 288 B 커진다) |
| 태스크 생성 | 1943~1946행 `configINIT_TLS_BLOCK(pxNewTCB->xTLSBlock, pxTopOfStack)` | `_REENT_INIT_PTR` — 0으로 채우고 stdin/out/err 포인터를 전역 `__sf`에 연결 |
| 스케줄러 시작 | 3789~3793행 `configSET_TLS_BLOCK(pxCurrentTCB->xTLSBlock)` | 첫 태스크의 `_reent`로 `_impure_ptr` 설정 |
| 문맥 전환 | 5288~5292행 (`vTaskSwitchContext()`, 단일 코어) | 새 `pxCurrentTCB`의 `_reent`로 `_impure_ptr` 교체 |
| 태스크 삭제 | 6575~6578행 (`prvDeleteTCB()`) | `_reclaim_reent` — 지연 할당된 `_misc` 등을 free |

`configUSE_NEWLIB_REENTRANT`를 쓰지 않고 `configUSE_C_RUNTIME_TLS_SUPPORT=1`과 네 매크로를 `FreeRTOSConfig.h`에 직접 정의해도 된다(그러면 `FreeRTOS.h` 153~170행이 빠진 매크로를 `#error`로 알려 준다). 다른 C 런타임(IAR DLib, 자체 libc)을 붙일 때 이 방법을 쓴다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">configUSE_NEWLIB_REENTRANT=1: 문맥 전환 때 커널이 C 런타임 포인터도 바꾼다</text>
<line x1="60" y1="150" x2="660" y2="150" stroke="currentColor" stroke-width="1.3"/>
<rect x="60" y="60" width="220" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="170.0" y="84.2" font-size="12" text-anchor="middle">태스크 A 실행</text>
<rect x="330" y="60" width="330" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="495.0" y="84.2" font-size="12" text-anchor="middle">태스크 B 실행</text>
<rect x="280" y="110" width="50" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="305.0" y="129.2" font-size="12" text-anchor="middle">SW</text>
<text x="20" y="84" font-size="12" text-anchor="start">CPU</text>
<text x="20" y="130" font-size="12" text-anchor="start">전환</text>
<rect x="60" y="165" width="220" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="170.0" y="187.2" font-size="12" text-anchor="middle">_impure_ptr = &amp;A.xTLSBlock</text>
<rect x="330" y="165" width="330" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="495.0" y="187.2" font-size="12" text-anchor="middle">_impure_ptr = &amp;B.xTLSBlock</text>
<line x1="305" y1="142" x2="335" y2="168" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<text x="340" y="225" font-size="12" text-anchor="middle">SW = PendSV → vTaskSwitchContext() → configSET_TLS_BLOCK(pxCurrentTCB-&gt;xTLSBlock)</text>
<text x="340" y="243" font-size="12" text-anchor="middle">errno는 (*__errno()) = _impure_ptr-&gt;_errno 이므로, 같은 errno 이름이 태스크마다 다른 주소가 된다</text>
</svg>
```

그림 3 — 문맥 전환 한 번에 커널이 하는 추가 일: `_impure_ptr` 대입 하나. errno 이름은 같아도 태스크마다 다른 주소가 된다

버전 이력은 `History.txt`에서 확인했다: 태스크별 POSIX errno(`configUSE_POSIX_ERRNO`)는 **V10.1.0(2018-08)**, newlib 전용이던 TLS 지원을 일반화한 `configUSE_C_RUNTIME_TLS_SUPPORT`는 **V10.5.0(2022-09)**, picolibc TLS는 **V10.6.0(2023-07)**, picolibc에서 TLS 블록이 스택 꼭대기를 덮어쓰던 `pxTopOfStack` 계산 수정은 **V11.0.0(2023-12)** 이다. `newlib-freertos.h` 머리 주석은 "newlib 지원은 요청이 많아 넣었지만 메인테이너는 쓰지 않는다 … 시스템 전역 malloc은 락을 제공받아야 한다"고 경고하고 Dave Nadler의 글(참고 자료)을 가리킨다. **FreeRTOS는 `_reent`만 바꿔 줄 뿐 malloc 락은 주지 않는다** — 그것이 §3이다.

### 2.5 예제 5 — 태스크 하나당 얼마인가: 정적 크기 + 지연 할당

무엇을 확인하는 코드인지: `sizeof(struct _reent)`, `sizeof(StaticTask_t)`(= TCB 크기), 그리고 각 태스크가 `strtok` · `rand` · `snprintf` · `printf`를 처음 부를 때 newlib 힙에서 얼마가 빠져나가는지(`mallinfo().uordblks`).

```c
/* ex4 — what a per-task struct _reent costs: static size + lazy heap allocations */
#include <malloc.h>
#include <reent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
static unsigned used(void) { return mallinfo().uordblks; }          /* newlib heap bytes in use */
static void probe(void *arg)
{
    char s[] = "a,b", buf[16];
    unsigned u0 = used();
    (void)strtok(s, ",");                     unsigned u1 = used();
    (void)rand();                             unsigned u2 = used();
    snprintf(buf, sizeof buf, "%d", 42);      unsigned u3 = used();
    printf("%s", "");                         unsigned u4 = used();
    board_printf_raw("  task %d: strtok +%u B, rand +%u B, snprintf +%u B, printf +%u B\n",
                     (int)arg, u1 - u0, u2 - u1, u3 - u2, u4 - u3);
    vTaskSuspend(NULL);
}
static void rep(void *arg)
{
    (void)arg;
    vTaskDelay(5);
    board_printf_raw("  newlib heap in use after 3 tasks = %u B\n", used());
    board_exit(0);
}
int main(void)
{
    board_printf_raw("_WANT_REENT_SMALL=%d NEWLIB_REENTRANT=%d POSIX_ERRNO=%d\n",
#ifdef _REENT_SMALL
                     1,
#else
                     0,
#endif
                     configUSE_NEWLIB_REENTRANT, configUSE_POSIX_ERRNO);
    board_printf_raw("  sizeof(struct _reent)=%u  sizeof(FILE)=%u  sizeof(StaticTask_t)=%u\n",
                     (unsigned)sizeof(struct _reent), (unsigned)sizeof(FILE), (unsigned)sizeof(StaticTask_t));
    for (int i = 0; i < 3; i++) xTaskCreate(probe, "p", 512, (void *)i, 2, NULL);
    xTaskCreate(rep, "r", 512, NULL, 1, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
R=/Users/donh/.rtos_scratch/F7/run.sh
$R ex4 ex4_a; $R ex4 ex4_b EXTRA_CFLAGS=-DconfigUSE_NEWLIB_REENTRANT=1          # newlib-nano
LD="-T /var/rtoslab/templates/common/mps2_m3.ld -nostartfiles -Wl,--gc-sections"  # nano.specs 뺀 링크 = full newlib
$R ex4 ex4_f "LDFLAGS=$LD"; $R ex4 ex4_g "LDFLAGS=$LD" EXTRA_CFLAGS=-DconfigUSE_NEWLIB_REENTRANT=1
$R ex4 ex4_e EXTRA_CFLAGS=-DconfigUSE_POSIX_ERRNO=1
```

```text
_WANT_REENT_SMALL=1 NEWLIB_REENTRANT=0 POSIX_ERRNO=0
  sizeof(struct _reent)=76  sizeof(FILE)=104  sizeof(StaticTask_t)=96
  task 0: strtok +88 B, rand +32 B, snprintf +0 B, printf +0 B
  task 1: strtok +0 B, rand +0 B, snprintf +0 B, printf +0 B
  task 2: strtok +0 B, rand +0 B, snprintf +0 B, printf +0 B
  newlib heap in use after 3 tasks = 120 B
[board_exit] code=0
_WANT_REENT_SMALL=1 NEWLIB_REENTRANT=1 POSIX_ERRNO=0
  sizeof(struct _reent)=76  sizeof(FILE)=104  sizeof(StaticTask_t)=172
  task 0: strtok +88 B, rand +32 B, snprintf +0 B, printf +0 B
  task 1: strtok +88 B, rand +32 B, snprintf +0 B, printf +0 B
  task 2: strtok +88 B, rand +32 B, snprintf +0 B, printf +0 B
  newlib heap in use after 3 tasks = 360 B
[board_exit] code=0
_WANT_REENT_SMALL=0 NEWLIB_REENTRANT=0 POSIX_ERRNO=0
  sizeof(struct _reent)=288  sizeof(FILE)=104  sizeof(StaticTask_t)=96
  task 0: strtok +0 B, rand +0 B, snprintf +0 B, printf +0 B
  task 1: strtok +0 B, rand +0 B, snprintf +0 B, printf +0 B
  task 2: strtok +0 B, rand +0 B, snprintf +0 B, printf +0 B
  newlib heap in use after 3 tasks = 0 B
[board_exit] code=0
_WANT_REENT_SMALL=0 NEWLIB_REENTRANT=1 POSIX_ERRNO=0
  sizeof(struct _reent)=288  sizeof(FILE)=104  sizeof(StaticTask_t)=384
  task 0: strtok +0 B, rand +0 B, snprintf +0 B, printf +0 B
  task 1: strtok +0 B, rand +0 B, snprintf +0 B, printf +0 B
  task 2: strtok +0 B, rand +0 B, snprintf +0 B, printf +0 B
  newlib heap in use after 3 tasks = 0 B
[board_exit] code=0
_WANT_REENT_SMALL=1 NEWLIB_REENTRANT=0 POSIX_ERRNO=1
  sizeof(struct _reent)=76  sizeof(FILE)=104  sizeof(StaticTask_t)=100
```

(마지막 실행은 처음 두 줄만 남겼다.)

출력에서 볼 것:

- **newlib-nano + 태스크별 `_reent`: TCB가 96 → 172 B(+76 B), 그리고 처음 쓸 때 힙에서 `strtok` 88 B(80 B + malloc 헤더 8 B), `rand` 32 B.** 지연 할당은 태스크마다 일어난다(task 0, 1, 2 모두 +88). 태스크 20개면 정적 1.5 KB + 지연 최대 수 KB.
- **full newlib: TCB가 96 → 384 B(+288 B), 지연 할당 없음.** 정적으로 다 들고 있다. 결정적이지만 크다.
- 기본 구성(`NEWLIB_REENTRANT=0`)에서는 첫 태스크만 할당하고 나머지는 0 — 전부 같은 `_impure_data`를 공유하니까. 예제 3 · 4의 레이스가 바로 이 공유다.
- `snprintf`(정수) · `printf`는 +0이다. 이 랩의 `board_init()`이 `stdout`을 무버퍼(`_IONBF`)로 만들어서 printf가 버퍼를 malloc하지 않는다. 기본 버퍼링이었다면 첫 printf가 `__smakebuf_r`로 버퍼를 malloc한다(B11 §3.2).
- 템플릿 Makefile은 한 번의 `gcc` 호출로 컴파일과 링크를 같이 하므로 `--specs=nano.specs`가 컴파일에도 적용되어 `_REENT_SMALL`이 1로 맞게 잡혔다. 컴파일과 링크를 나눠 하면서 컴파일에만 specs를 빼먹으면 TCB 안 `_reent`(full 배치)와 라이브러리(small 배치)가 어긋난다 — B11 §3.4의 함정이 여기서 실제 메모리 손상이 된다.

### 2.6 함정 — `_reent`를 켜도 남는 것

- **ISR은 끊긴 태스크의 `_reent`를 쓴다.** `_impure_ptr`는 문맥 전환 때만 바뀐다. ISR 안에서 `strtol`이 errno를 쓰면 **끊긴 태스크의 errno가 덮인다.** ISR에서 `strtok`을 부르면 끊긴 태스크의 토큰 위치가 바뀐다. 태스크별 `_reent`는 태스크끼리의 문제만 푼다.
- **`_reent`를 켜도 malloc 힙과 `stdout` FILE은 여전히 하나다.** 태스크별로 나뉘는 것은 `_reent` 안의 상태뿐이다(§3, §6).
- 태스크 삭제 때 `_reclaim_reent`가 지연 할당 블록을 free한다. 이 free는 **idle 태스크**(`prvCheckTasksWaitingTermination`)에서 돈다. idle이 굶으면 회수가 밀린다(C9).
- 정적 할당 태스크(`xTaskCreateStatic`)도 `StaticTask_t`에 `_reent` 공간이 들어 있어 크기가 같이 커진다(위 출력의 `sizeof(StaticTask_t)`). `StaticTask_t`를 직접 계산해서 배열을 잡았다면 다시 컴파일해야 한다.
- `configUSE_POSIX_ERRNO`를 libc errno 해결책으로 착각하지 말 것(예제 4).

---

## 3. malloc 락 — `__malloc_lock`과 retargetable lock

> 🔁 이 절은 Bare-Metal BM F5(malloc 락 부분)와 겹친다 (주인: 이 노트).

### 3.1 newlib의 락 체계 두 가지

malloc 힙은 태스크별로 나눌 수 없는 **진짜 공유 자원**이다. newlib은 힙을 고치기 전후에 락 함수를 부른다. 어떤 락 함수를 부르는지는 newlib이 **어떻게 빌드되었나**에 달렸다.

| 체계 | 언제 | 앱이 구현하는 함수 | 덮는 범위 |
|---|---|---|---|
| 옛 훅 (legacy) | `_RETARGETABLE_LOCKING` 꺼짐 — **Ubuntu newlib 4.6.0** | `__malloc_lock(struct _reent *)`, `__malloc_unlock`, `__env_lock/unlock`, `__tz_lock/unlock` | malloc, 환경변수, 시간대. **stdio FILE 락은 컴파일 단계에서 사라짐** |
| retargetable lock | `--enable-newlib-retargetable-locking`으로 빌드 | `__retarget_lock_init / _init_recursive / _close / _close_recursive / _acquire / _acquire_recursive / _try_acquire / _try_acquire_recursive / _release / _release_recursive` (10개) | malloc, stdio(`__sfp`, `__sinit`, FILE마다), atexit, env, tz, arc4random 등 전부 |

Ubuntu 빌드의 `sys/lock.h`를 보면 `_RETARGETABLE_LOCKING`이 없을 때 락이 전부 **빈 매크로**다.

```c
/* /usr/include/newlib/sys/lock.h (발췌) */
#if !defined(_RETARGETABLE_LOCKING)
typedef int _LOCK_T;
#define __lock_acquire_recursive(lock) ((void) 0)
#define __lock_release_recursive(lock) ((void) 0)
...
#else
extern void __retarget_lock_acquire_recursive(_LOCK_T lock);
#define __lock_acquire_recursive(lock) __retarget_lock_acquire_recursive(lock)
...
```

그래서 이 툴체인에서 `_flockfile(fp)` 같은 stdio 락은 libc를 빌드할 때 **아예 지워졌다.** 남은 것은 별도 오브젝트에 들어 있는 함수 몇 개다. 아카이브에서 정의(T)된 락 심볼을 뽑았다.

```sh
L=/usr/lib/arm-none-eabi/lib/thumb/v7-m/nofp
arm-none-eabi-nm -A $L/libc_nano.a | grep -E " [TWDBV] " | grep -E "retarget_lock|_lock$|_lock_acquire|_unlock$|_lock_release"
```

```text
/usr/lib/arm-none-eabi/lib/thumb/v7-m/nofp/libc_nano.a:libc_a-envlock.o:00000000 T __env_lock
/usr/lib/arm-none-eabi/lib/thumb/v7-m/nofp/libc_nano.a:libc_a-envlock.o:00000004 T __env_unlock
/usr/lib/arm-none-eabi/lib/thumb/v7-m/nofp/libc_nano.a:libc_a-findfp.o:000001f4 T __sfp_lock_acquire
/usr/lib/arm-none-eabi/lib/thumb/v7-m/nofp/libc_nano.a:libc_a-findfp.o:000001f8 T __sfp_lock_release
/usr/lib/arm-none-eabi/lib/thumb/v7-m/nofp/libc_nano.a:libc_a-tzlock.o:00000000 T __tz_lock
/usr/lib/arm-none-eabi/lib/thumb/v7-m/nofp/libc_nano.a:libc_a-tzlock.o:00000004 T __tz_unlock
/usr/lib/arm-none-eabi/lib/thumb/v7-m/nofp/libc_nano.a:libc_a-mlock.o:00000000 T __malloc_lock
/usr/lib/arm-none-eabi/lib/thumb/v7-m/nofp/libc_nano.a:libc_a-mlock.o:00000004 T __malloc_unlock
```

`__retarget_lock_*`는 하나도 없다. `__malloc_lock`은 `mlock.o`에 **혼자** 있으므로 앱이 같은 이름을 정의하면 링커가 `mlock.o`를 끌어오지 않는다(덮어쓰기가 된다). 하지만 `__sfp_lock_acquire`는 `__sinit`, `__sfp`와 같은 `findfp.o`에 있다.

### 3.2 예제 6 — 어느 훅을 앱이 덮어쓸 수 있나

무엇을 확인하는 코드인지: `__malloc_lock`과 `__sfp_lock_acquire`를 앱에서 정의하고 printf를 쓰는 프로그램을 링크한다.

```c
/* ex_sfp — which newlib lock hooks can an application override on this toolchain? */
#include <stdio.h>
#include "board.h"
static unsigned n_malloc, n_sfp;
void __malloc_lock(struct _reent *r)   { (void)r; n_malloc++; }
void __malloc_unlock(struct _reent *r) { (void)r; }
#ifdef OVERRIDE_SFP
void __sfp_lock_acquire(void) { n_sfp++; }
void __sfp_lock_release(void) { }
#endif
int main(void)
{
    printf("hello %d\n", 1);
    board_printf_raw("malloc_lock calls=%u sfp_lock calls=%u\n", n_malloc, n_sfp);
    return 0;
}
```

```sh
M="make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/F7/ex_sfp/main.c"
$M OUT=/var/tmp/rtos/F7/sfp0 >/dev/null && /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F7/sfp0/app.elf
$M OUT=/var/tmp/rtos/F7/sfp1 EXTRA_CFLAGS=-DOVERRIDE_SFP 2>&1 | grep -E "multiple|error"     # 경로는 줄였다
```

```text
hello 1
malloc_lock calls=0 sfp_lock calls=0
[board_exit] code=0
newlib/libc/stdio/findfp.c:270: multiple definition of `__sfp_lock_acquire'; main.c:8: first defined here
newlib/libc/stdio/findfp.c:273: multiple definition of `__sfp_lock_release'; main.c:9: first defined here
collect2: error: ld returned 1 exit status
```

출력에서 볼 것: `__malloc_lock`은 덮어써도 링크된다(이 실행에서 호출 0회인 것은 `stdout`이 무버퍼라 printf가 malloc을 안 했기 때문 — 예제 9에서 실제로 불리는 것을 센다). **`__sfp_lock_acquire`는 "multiple definition"으로 링크가 실패한다.** printf가 `__sinit`을 쓰려고 `findfp.o`를 끌어오면 같은 오브젝트의 `__sfp_lock_acquire`도 따라오기 때문이다. 결론: **Ubuntu newlib으로는 malloc만 스레드 안전하게 만들 수 있고, stdio는 만들 수 없다.** stdio를 여러 태스크가 쓰려면 retargetable locking으로 빌드된 newlib을 쓰거나, stdio 주인을 한 태스크로 정한다(§7). Arm이 배포하는 Arm GNU Toolchain의 newlib이 어떤 옵션으로 빌드되었는지는 이 랩에 그 배포판이 없어 **확인하지 못했다** — 쓰는 툴체인에서 `grep _RETARGETABLE_LOCKING $(arm-none-eabi-gcc -print-sysroot)/include/newlib.h`와 `nm libc.a | grep __retarget_lock`로 직접 확인하라. 위의 두 줄이 그 확인 방법이다.

### 3.3 예제 7 — 락 없는 newlib malloc을 두 태스크가 쓰면

무엇을 확인하는 코드인지: 낮은 우선순위 태스크 L이 8개 슬롯에 무작위 크기(8~207 B)를 malloc/free 하며 내용을 태그로 채우고 검사한다. 높은 우선순위 태스크 H는 TIMER0 IRQ(약 60 µs마다)로 깨어나 malloc/free를 한다. H가 L의 malloc **한가운데**를 선점하는 순간을 만든다. `LOCK`: 0 없음, 1 `vTaskSuspendAll`, 2 재귀 뮤텍스. `ISR_MALLOC`을 켜면 ISR에서도 malloc한다.

```c
/* ex3 — newlib-nano malloc from two tasks (+ optional ISR). LOCK 0 none, 1 suspend-all, 2 recursive mutex */
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"
#ifndef LOCK
#define LOCK 0
#endif
#define OPS 6000
#ifndef RELOAD
#define RELOAD 1499                                  /* TIMER0 period = RELOAD / 25 MHz ~ 60 us */
#endif
static TaskHandle_t hH; static SemaphoreHandle_t mlock;
static volatile unsigned bad, hops, isr_ops;
#if LOCK == 1
void __malloc_lock(struct _reent *r)   { (void)r; vTaskSuspendAll(); }
void __malloc_unlock(struct _reent *r) { (void)r; (void)xTaskResumeAll(); }
#elif LOCK == 2
void __malloc_lock(struct _reent *r)   { (void)r; if (mlock) xSemaphoreTakeRecursive(mlock, portMAX_DELAY); }
void __malloc_unlock(struct _reent *r) { (void)r; if (mlock) xSemaphoreGiveRecursive(mlock); }
#endif
static int check(unsigned char *p, unsigned n, unsigned char tag)
{ for (unsigned i = 0; i < n; i++) if (p[i] != tag) return 0; return 1; }
void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
#ifdef ISR_MALLOC
    void *q = malloc(16); if (q) { memset(q, 0x33, 16); free(q); } isr_ops++;
#endif
    BaseType_t w = pdFALSE; vTaskNotifyGiveFromISR(hH, &w); portYIELD_FROM_ISR(w);
}
static void high(void *arg)                          /* prio 3: woken by every TIMER0 IRQ */
{
    for ((void)arg;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); unsigned n = 24 + (hops % 5) * 8;
        unsigned char *p = malloc(n); if (!p) { bad++; continue; }
        memset(p, 0x5A, n); if (!check(p, n, 0x5A)) bad++;
        free(p); hops++;
    }
}
static void low(void *arg)                           /* prio 1: 8 slots, random alloc/free, tagged */
{
    unsigned char *slot[8] = {0}; unsigned len[8] = {0}; uint32_t rng = 1, t0 = board_now_us();
    for (int op = 0; op < OPS; op++) {
        rng = rng * 1103515245u + 12345u; int s = (rng >> 16) & 7;
        if (slot[s]) {
            if (!check(slot[s], len[s], (unsigned char)(0xA0 | s)) && bad++ < 3)
                board_printf_raw("  corrupt: slot %d (%u B at 0x%x) op=%d\n", s, len[s], (unsigned)slot[s], op);
            free(slot[s]); slot[s] = NULL;
        } else {
            len[s] = 8 + ((rng >> 8) % 200);
            if ((slot[s] = malloc(len[s])) == NULL) { bad++; continue; }
            memset(slot[s], 0xA0 | s, len[s]);
        }
    }
    TIMER_CTRL(TIMER0_BASE) = 0;
    board_printf_raw("LOCK=%d%s: low ops=%d high ops=%u isr ops=%u corrupt=%u, %u us\n", LOCK,
                     isr_ops ? "+ISR" : "", OPS, hops, isr_ops, bad, (unsigned)(board_now_us() - t0));
    board_exit((int)(uintptr_t)arg);
}
int main(void)
{
    mlock = xSemaphoreCreateRecursiveMutex();
    xTaskCreate(high, "H", 256, NULL, 3, &hH); xTaskCreate(low, "L", 512, NULL, 1, NULL);
    TIMER_RELOAD(TIMER0_BASE) = RELOAD; TIMER_VALUE(TIMER0_BASE) = RELOAD;
    TIMER_CTRL(TIMER0_BASE) = 1 | 8; NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;
    vTaskStartScheduler();
}
```

```sh
for l in 0 1 2; do TMO=40 $R ex3 ex3_$l EXTRA_CFLAGS=-DLOCK=$l; done
arm-none-eabi-addr2line -f -e /var/tmp/rtos/F7/ex3_0/app.elf 0x2756 0x2745     # 경로 앞부분은 줄였다
```

```text

[HardFault] PC=0x2756 LR=0x2745 xPSR=0x81000000 CFSR=0x8200 HFSR=0x40000000
[board_exit] code=255
LOCK=1: low ops=6000 high ops=3664 isr ops=0 corrupt=0, 219775 us
[board_exit] code=0
LOCK=2: low ops=6000 high ops=10917 isr ops=0 corrupt=0, 654961 us
[board_exit] code=0
_malloc_r
newlib/libc/stdlib/nano-mallocr.c:264
_malloc_r
newlib/libc/stdlib/nano-mallocr.c:259
```

출력에서 볼 것:

- **락 없음: `_malloc_r` 안에서 HardFault.** CFSR `0x8200` = BusFault의 `PRECISERR | BFARVALID` — 정확한 주소의 잘못된 메모리 접근이다. nano malloc이 free list를 따라가다(`nano-mallocr.c` 259~264행) 깨진 `next` 포인터를 읽은 것이다. H가 L의 malloc 중간(리스트에서 블록을 떼어 내는 두 문장 사이)에 들어가 같은 리스트를 고쳤다. **크래시 위치(malloc 안)와 원인(다른 태스크의 malloc)이 다른 곳**이라는 점이 현장에서 이 버그를 어렵게 만든다.
- **`vTaskSuspendAll` 락: 깨끗하게 끝난다.** 락이 걸린 동안 스케줄러가 전환을 미루므로 H가 L의 malloc 안에 못 들어간다. ISR은 계속 돈다(H를 깨우는 알림만 쌓인다).
- **재귀 뮤텍스 락: 역시 깨끗하지만 3배 느리다**(219 ms → 655 ms). H가 깨어날 때마다 L이 쥔 뮤텍스에 막히고, 우선순위 상속으로 L이 올라갔다가 내려오는 전환이 추가된다. high ops가 많은 것은 실행 시간이 길어 IRQ가 그만큼 더 왔기 때문이다.

같은 코드를 IRQ 주기만 바꿔 돌리면 결과가 달라진다. 주기를 바꿔 가며 6번씩 돌렸다(`-DRELOAD=…`).

```sh
#!/bin/bash
# sweep.sh — ex3 at several TIMER0 periods, each LOCK mode; prints one line per run
R=/Users/donh/.rtos_scratch/F7/run.sh
for rl in 1499 1753 2003 2251 2503 3001; do for l in 0 1 2; do
  out=$(TMO=40 $R ex3 ex3_${l}_$rl "EXTRA_CFLAGS=-DLOCK=$l -DRELOAD=$rl" 2>&1 | grep -E "LOCK=|HardFault|ASSERT|timeout" | head -1)
  printf "period=%3d us LOCK=%d  %s\n" $((rl/25)) $l "${out:-<no output: hang>}"
done; done
```

```text
period= 59 us LOCK=0  [HardFault] PC=0x2756 LR=0x2745 xPSR=0x81000000 CFSR=0x8200 HFSR=0x40000000
period= 59 us LOCK=1  LOCK=1: low ops=6000 high ops=3664 isr ops=0 corrupt=0, 219775 us
period= 59 us LOCK=2  LOCK=2: low ops=6000 high ops=10917 isr ops=0 corrupt=0, 654961 us
period= 70 us LOCK=0  [HardFault] PC=0x2756 LR=0x2745 xPSR=0xa1000000 CFSR=0x8200 HFSR=0x40000000
period= 70 us LOCK=1  LOCK=1: low ops=6000 high ops=2549 isr ops=0 corrupt=0, 178798 us
period= 70 us LOCK=2  LOCK=2: low ops=6000 high ops=5972 isr ops=0 corrupt=0, 418932 us
period= 80 us LOCK=0  LOCK=0: low ops=6000 high ops=1520 isr ops=0 corrupt=0, 121776 us
period= 80 us LOCK=1  LOCK=1: low ops=6000 high ops=1967 isr ops=0 corrupt=0, 157591 us
period= 80 us LOCK=2  LOCK=2: low ops=6000 high ops=4188 isr ops=0 corrupt=0, 335636 us
period= 90 us LOCK=0  [HardFault] PC=0x2756 LR=0x2745 xPSR=0x81000000 CFSR=0x8200 HFSR=0x40000000
period= 90 us LOCK=1  LOCK=1: low ops=6000 high ops=1601 isr ops=0 corrupt=0, 144172 us
period= 90 us LOCK=2  LOCK=2: low ops=6000 high ops=3110 isr ops=0 corrupt=0, 280120 us
period=100 us LOCK=0  LOCK=0: low ops=6000 high ops=1072 isr ops=0 corrupt=0, 107350 us
period=100 us LOCK=1  LOCK=1: low ops=6000 high ops=1346 isr ops=0 corrupt=0, 134795 us
period=100 us LOCK=2  LOCK=2: low ops=6000 high ops=2466 isr ops=0 corrupt=0, 246970 us
period=120 us LOCK=0  [HardFault] PC=0x2756 LR=0x2745 xPSR=0xa1000000 CFSR=0x8200 HFSR=0x40000000
period=120 us LOCK=1  LOCK=1: low ops=6000 high ops=1025 isr ops=0 corrupt=0, 123057 us
period=120 us LOCK=2  LOCK=2: low ops=6000 high ops=1746 isr ops=0 corrupt=0, 209675 us
```

출력에서 볼 것: **락 없는 경우 6번 중 4번 크래시, 2번(80 µs, 100 µs)은 6000번 연산을 아무 일 없이 통과했다.** 테스트가 통과했다고 레이스가 없는 것이 아니다 — 선점이 malloc의 몇 명령짜리 위험 구간에 정확히 걸려야만 깨진다. 실칩에서는 이것이 "몇 시간에 한 번, 엉뚱한 곳에서 HardFault"가 된다. 락이 있는 두 경우는 12번 모두 통과했다.

### 3.4 예제 8 — ISR에서 malloc하면 락이 무엇이 되나

무엇을 확인하는 코드인지: 같은 프로그램에 `-DISR_MALLOC`을 붙여 TIMER0 ISR에서도 malloc/free를 한다.

```sh
for l in 0 1 2; do TMO=30 $R ex3 ex3_${l}i "EXTRA_CFLAGS=-DLOCK=$l -DISR_MALLOC"; done
```

```text

[HardFault] PC=0x276e LR=0x275d xPSR=0xa1000018 CFSR=0x8200 HFSR=0x40000000
[board_exit] code=255

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:438
[board_exit] code=253

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:438
[board_exit] code=253
```

출력에서 볼 것:

- 락 없음: 이번에는 **ISR 문맥에서** HardFault(`xPSR` 하위 비트 `0x18` = IPSR 24 = IRQ 8, TIMER0).
- `vTaskSuspendAll` 락: `xTaskResumeAll()` 안의 `taskENTER_CRITICAL()` → `vPortEnterCritical()`의 assert(`port.c` 438행 "`portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK == 0`", ISR에서 부르면 안 되는 크리티컬 섹션). assert를 꺼도 고쳐지지 않는다 — **스케줄러 정지는 ISR을 막지 않으므로** ISR의 malloc은 태스크의 malloc 한가운데로 그대로 들어간다.
- 재귀 뮤텍스 락: 같은 assert. 뮤텍스 take 경로의 크리티컬 섹션이 먼저 걸렸다. (이 프로그램의 이전 버전에서는 `queue.c` 1682행 "스케줄러가 멈춘 동안 블록할 수 없다" assert가 먼저 걸리기도 했다 — 어느 assert가 먼저냐는 타이밍 문제이고, 결론은 같다.) 뮤텍스를 ISR에서 쓰면 왜 안 되는지는 D2 §4, E1 §4.5.

**ISR에서 malloc을 안전하게 만드는 락은 "인터럽트를 끄는 락"뿐이다.** 그러면 malloc 시간(예제 9: 수 µs, 단편화된 힙에서는 훨씬 길다) 동안 모든 IRQ가 막힌다. 그래서 규칙은 "ISR에서 malloc 금지, 필요하면 고정 블록 풀(F2)을 ISR 전용으로"다.

### 3.5 예제 9 — 락은 얼마나 비싼가

무엇을 확인하는 코드인지: 경합 없이 `malloc(32)` + `free` 한 쌍의 시간을 락 종류별로, 그리고 FreeRTOS heap_4(`pvPortMalloc`)와 비교한다.

```c
/* ex3b — what a malloc lock costs: one malloc(32)+free pair, no contention */
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"
#define CNT REG32(0x40028018u)                       /* FPGAIO 25 MHz counter */
#define N 1000
static SemaphoreHandle_t mlock;
static volatile unsigned nlock;
#if LOCK == 1
void __malloc_lock(struct _reent *r)   { (void)r; nlock++; vTaskSuspendAll(); }
void __malloc_unlock(struct _reent *r) { (void)r; (void)xTaskResumeAll(); }
#elif LOCK == 2
void __malloc_lock(struct _reent *r)   { (void)r; nlock++; xSemaphoreTakeRecursive(mlock, portMAX_DELAY); }
void __malloc_unlock(struct _reent *r) { (void)r; xSemaphoreGiveRecursive(mlock); }
#endif
static void bench(void *arg)
{
    (void)arg;
    void *volatile p;
    uint32_t c0 = CNT;
    for (int i = 0; i < N; i++) { p = malloc(32); free(p); }
    uint32_t c1 = CNT;
    for (int i = 0; i < N; i++) { p = pvPortMalloc(32); vPortFree(p); }
    uint32_t c2 = CNT;
    board_printf_raw("LOCK=%d: malloc+free %u ns/pair (lock calls %u), pvPortMalloc+vPortFree (heap_4) %u ns/pair\n",
                     LOCK, (c1 - c0) * 40 / N, nlock, (c2 - c1) * 40 / N);
    board_exit(0);
}
int main(void)
{
    mlock = xSemaphoreCreateRecursiveMutex();
    xTaskCreate(bench, "b", 512, NULL, 1, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
for l in 0 1 2; do $R ex3b ex3b_$l EXTRA_CFLAGS=-DLOCK=$l | head -1; done
```

```text
LOCK=0: malloc+free 2404 ns/pair (lock calls 0), pvPortMalloc+vPortFree (heap_4) 8879 ns/pair
LOCK=1: malloc+free 6891 ns/pair (lock calls 2000), pvPortMalloc+vPortFree (heap_4) 8879 ns/pair
LOCK=2: malloc+free 17274 ns/pair (lock calls 2000), pvPortMalloc+vPortFree (heap_4) 8879 ns/pair
```

```svg
<svg viewBox="0 0 680 210" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">malloc(32) + free 한 쌍의 비용 (경합 없음)</text>
<text x="202" y="58" font-size="12" text-anchor="end">newlib-nano, 락 없음 (기본)</text>
<rect x="210.0" y="45" width="38.5" height="18" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="254.5" y="58" font-size="12">2.40 µs — 태스크 2개면 힙이 깨진다</text>
<text x="202" y="88" font-size="12" text-anchor="end">+ vTaskSuspendAll 락</text>
<rect x="210.0" y="75" width="110.3" height="18" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="326.3" y="88" font-size="12">6.89 µs</text>
<text x="202" y="118" font-size="12" text-anchor="end">+ 재귀 뮤텍스 락</text>
<rect x="210.0" y="105" width="276.4" height="18" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="492.4" y="118" font-size="12">17.27 µs</text>
<text x="202" y="148" font-size="12" text-anchor="end">FreeRTOS heap_4 (참고)</text>
<rect x="210.0" y="135" width="142.1" height="18" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="358.1" y="148" font-size="12">8.88 µs</text>
<line x1="210" y1="40" x2="210" y2="164" stroke="currentColor"/>
<line x1="210" y1="164" x2="610" y2="164" stroke="currentColor"/>
<line x1="210.0" y1="164" x2="210.0" y2="169" stroke="currentColor"/>
<text x="210.0" y="183" font-size="12" text-anchor="middle">0</text>
<line x1="290.0" y1="164" x2="290.0" y2="169" stroke="currentColor"/>
<text x="290.0" y="183" font-size="12" text-anchor="middle">5</text>
<line x1="370.0" y1="164" x2="370.0" y2="169" stroke="currentColor"/>
<text x="370.0" y="183" font-size="12" text-anchor="middle">10</text>
<line x1="450.0" y1="164" x2="450.0" y2="169" stroke="currentColor"/>
<text x="450.0" y="183" font-size="12" text-anchor="middle">15</text>
<line x1="530.0" y1="164" x2="530.0" y2="169" stroke="currentColor"/>
<text x="530.0" y="183" font-size="12" text-anchor="middle">20</text>
<line x1="610.0" y1="164" x2="610.0" y2="169" stroke="currentColor"/>
<text x="610.0" y="183" font-size="12" text-anchor="middle">25</text>
<text x="410" y="200" font-size="12" text-anchor="middle">µs / 쌍 (QEMU icount, FPGAIO 카운터)</text>
</svg>
```

그림 4 — `malloc(32)`+`free` 한 쌍의 시간(경합 없음). 락은 malloc · free 각각 한 번씩, 쌍당 2번 불린다

출력에서 볼 것: lock calls가 2000 = 1000쌍 × 2 — 덮어쓴 `__malloc_lock`이 실제로 불렸다. `vTaskSuspendAll`/`xTaskResumeAll` 한 쌍이 약 2.2 µs, 재귀 뮤텍스 take/give 한 쌍이 약 7.4 µs를 더한다. heap_4(8.9 µs)는 내부에서 `vTaskSuspendAll`을 쓰고 인접 블록 병합을 한다(F1). 숫자는 QEMU icount 기준이다.

### 3.6 어떤 락을 고르나

| 락 구현 | 장점 | 단점 | 이럴 때 |
|---|---|---|---|
| `vTaskSuspendAll` / `xTaskResumeAll` | 싸다(예제 9), 중첩 가능(재귀 OK), 스케줄러 시작 전에도 동작 | malloc이 도는 동안 **모든 태스크 전환이 밀린다** — 높은 우선순위 태스크 지연 = malloc 최악 시간 | 대부분. FreeRTOS 커널 소스 안에서 `__malloc_lock`을 구현한 유일한 포트(`portable/GCC/AVR32_UC3/port.c` 186~199행)도 이것이고, heap_3(`portable/MemMang/heap_3.c` 63 · 87행)도 같은 방식으로 malloc을 감싼다 |
| 재귀 뮤텍스 | 관계없는 태스크는 안 막는다, 우선순위 상속 | 비싸다, 스케줄러 시작 **전** malloc에서 주의(생성 전이면 락 없이 지나가게 짜야 함 — 예제의 `if (mlock)`), 첫 malloc이 뮤텍스 생성보다 먼저일 수 있다 | malloc이 긴 큰 힙, 우선순위가 넓게 퍼진 시스템 |
| 크리티컬 섹션(인터럽트 마스크) | ISR까지 막는 유일한 방법 | malloc 시간 동안 IRQ 지연 | ISR malloc을 정말 피할 수 없을 때(권하지 않음), RTOS 없는 bare-metal |

**재귀**여야 하는 이유: newlib의 `realloc`, `_reclaim_reent`, stdio 경로는 락을 쥔 채 다시 malloc/free를 부를 수 있다. 그래서 newlib의 정적 락 이름도 `__lock___malloc_recursive_mutex`다(Zephyr `lib/libc/newlib/libc-hooks.c`). `xSemaphoreTake`(비재귀)로 구현하면 자기 교착이 생길 수 있다(D2 §2).

retargetable locking newlib이라면 10개 `__retarget_lock_*`를 FreeRTOS 재귀 뮤텍스(또는 suspend-all)로 구현하고, newlib이 정적으로 선언하는 락 객체(`__lock___sinit_recursive_mutex`, `__lock___sfp_recursive_mutex`, `__lock___atexit_recursive_mutex`, `__lock___malloc_recursive_mutex`, `__lock___env_recursive_mutex`, `__lock___at_quick_exit_mutex`, `__lock___tz_mutex`, `__lock___dd_hash_mutex`, `__lock___arc4random_mutex`)를 앱이 정의해야 한다. Zephyr가 이것을 `k_mutex` · `k_sem`으로 정확히 해 둔 코드가 `lib/libc/newlib/libc-hooks.c` 350~358행 · 386~486행에 있으니 FreeRTOS로 옮길 때 그대로 본뜨면 된다.

### 3.7 Bare-metal(RTOS 없음)에서는

super loop + ISR 구조에서 ISR이 malloc을 하지 않는다면 락이 필요 없다(실행 흐름이 main 하나). ISR이 malloc을 한다면 `__malloc_lock`을 "PRIMASK 저장 후 끄기, unlock에서 복원"으로 구현해야 하고, 재귀 호출 때문에 **중첩 카운터**가 있어야 한다(C7의 저장/복원식 크리티컬 섹션). 가장 좋은 것은 ISR에서 malloc을 안 하는 것이다.

---

## 4. 태스크별 errno — 구현 여섯 가지

### 4.1 비교

| 커널 · libc | 설정 | errno가 사는 곳 | 전환 비용 | 확인 |
|---|---|---|---|---|
| FreeRTOS + newlib | 기본 | 전역 `_impure_data._errno` 하나 (공유) | 0 | 예제 4: lost 61/200 |
| FreeRTOS + newlib | `configUSE_NEWLIB_REENTRANT=1` | 각 TCB의 `xTLSBlock._errno` | `_impure_ptr` 대입 1번 | 예제 4: lost 0 |
| FreeRTOS (libc 무관) | `configUSE_POSIX_ERRNO=1` | 전역 `FreeRTOS_errno`를 TCB `iTaskErrno`에 저장 · 복원 | 전환마다 load/store | **libc errno는 그대로 공유** |
| FreeRTOS + picolibc | `configUSE_PICOLIBC_TLS=1` | 태스크 스택 꼭대기의 TLS 블록 | `_set_tls` (전역 1개 대입) | 예제 10 |
| Zephyr + picolibc(모듈, TLS 없음) | `CONFIG_ERRNO=y`, `THREAD_LOCAL_STORAGE=n` | `struct k_thread`의 `errno_var` (`z_errno()` 경유) | 0 (포인터가 스레드 구조체를 따라감) | 예제 11 |
| Zephyr + picolibc(툴체인, TLS) | `THREAD_LOCAL_STORAGE=y` → `LIBC_ERRNO=y` | picolibc의 `__thread int errno` (스레드 스택 꼭대기) | `z_arm_tls_ptr` 대입 | 예제 11 |

Zephyr에는 하나가 더 있다 — `CONFIG_ERRNO_IN_TLS`(기본 y, 단 `LIBC_ERRNO`가 아닐 때만)이면 Zephyr 자체의 `Z_THREAD_LOCAL int z_errno_var`를 쓴다(`include/zephyr/sys/errno_private.h`). userspace에서 errno 접근이 syscall이 되지 않게 하려는 것이다(`kernel/Kconfig` 402~408행 도움말).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">태스크 하나당 C 런타임 상태 비용 (바이트)</text>
<text x="242" y="58" font-size="12" text-anchor="end">FreeRTOS 기본 (전역 _reent 공유)</text>
<text x="256.0" y="58" font-size="12">0 B — 대신 errno·strtok 공유</text>
<text x="242" y="88" font-size="12" text-anchor="end">FreeRTOS configUSE_POSIX_ERRNO</text>
<rect x="250.0" y="75" width="4.0" height="18" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="260.0" y="88" font-size="12">4 B (TCB) — libc errno 아님</text>
<text x="242" y="118" font-size="12" text-anchor="end">Zephyr, TLS 없음 (k_thread.errno_var)</text>
<rect x="250.0" y="105" width="4.0" height="18" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="260.0" y="118" font-size="12">4 B</text>
<text x="242" y="148" font-size="12" text-anchor="end">FreeRTOS + picolibc TLS</text>
<rect x="250.0" y="135" width="4.0" height="18" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="254.0" y="135" width="4.0" height="18" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="264.0" y="148" font-size="12">4 + 4 B (+정렬)</text>
<text x="242" y="178" font-size="12" text-anchor="end">Zephyr + picolibc TLS (SDK)</text>
<rect x="250.0" y="165" width="16.0" height="18" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="272.0" y="178" font-size="12">16 B (스택 꼭대기)</text>
<text x="242" y="208" font-size="12" text-anchor="end">FreeRTOS + newlib-nano _reent</text>
<rect x="250.0" y="195" width="76.0" height="18" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="326.0" y="195" width="120.0" height="18" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="452.0" y="208" font-size="12">76 B TCB + 120 B 힙(지연)</text>
<text x="242" y="238" font-size="12" text-anchor="end">FreeRTOS + newlib full _reent</text>
<rect x="250.0" y="225" width="288.0" height="18" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="544.0" y="238" font-size="12">288 B TCB</text>
<line x1="250" y1="40" x2="250" y2="254" stroke="currentColor"/>
<line x1="250" y1="254" x2="610" y2="254" stroke="currentColor"/>
<line x1="250.0" y1="254" x2="250.0" y2="259" stroke="currentColor"/>
<text x="250.0" y="273" font-size="12" text-anchor="middle">0</text>
<line x1="300.0" y1="254" x2="300.0" y2="259" stroke="currentColor"/>
<text x="300.0" y="273" font-size="12" text-anchor="middle">50</text>
<line x1="350.0" y1="254" x2="350.0" y2="259" stroke="currentColor"/>
<text x="350.0" y="273" font-size="12" text-anchor="middle">100</text>
<line x1="400.0" y1="254" x2="400.0" y2="259" stroke="currentColor"/>
<text x="400.0" y="273" font-size="12" text-anchor="middle">150</text>
<line x1="450.0" y1="254" x2="450.0" y2="259" stroke="currentColor"/>
<text x="450.0" y="273" font-size="12" text-anchor="middle">200</text>
<line x1="500.0" y1="254" x2="500.0" y2="259" stroke="currentColor"/>
<text x="500.0" y="273" font-size="12" text-anchor="middle">250</text>
<line x1="550.0" y1="254" x2="550.0" y2="259" stroke="currentColor"/>
<text x="550.0" y="273" font-size="12" text-anchor="middle">300</text>
<line x1="600.0" y1="254" x2="600.0" y2="259" stroke="currentColor"/>
<text x="600.0" y="273" font-size="12" text-anchor="middle">350</text>
<text x="430" y="290" font-size="12" text-anchor="middle">바이트 — 파랑: TCB 안, 초록: 태스크 스택에서 떼어 냄, 주황: 처음 쓸 때 malloc</text>
</svg>
```

그림 5 — 태스크 하나가 C 런타임 상태에 쓰는 RAM. newlib은 크고(76~288 B + 지연 할당), TLS 방식은 "링크된 함수의 상태만" 든다. 측정: 예제 5(newlib), 예제 10(FreeRTOS + picolibc), 예제 11(Zephyr)

### 4.2 errno와 ISR

어느 구현이든 **ISR은 끊긴 태스크의 errno를 본다.** newlib은 `_impure_ptr`, picolibc는 TLS 포인터, Zephyr는 `_current` — 셋 다 ISR 진입 때 바뀌지 않는다. ISR에서 errno를 쓰는 libc 함수(`strtol`, `sqrt` 일부 경로 등)를 부르면 그 순간 실행 중이던 태스크가 "다른 데서 생긴 에러"를 보게 된다. ISR에서는 errno를 쓰는 함수를 부르지 말고, 에러는 반환값으로 받는 함수를 쓴다.

---

## 5. picolibc — TLS로 푼 재진입

### 5.1 아이디어 — "숨은 상태를 전부 `__thread` 변수로"

picolibc(B11 §3)는 newlib에서 갈라져 나왔지만 `struct _reent`를 없앴다. 숨은 상태는 C11 `_Thread_local`(GCC `__thread`) 변수다 — 예제 2에서 본 `errno`, `_strtok_last`, `_rand_next`, `_asctime_buf` 같은 것들. 컴파일러는 이 변수를 `.tdata`(초기값 있음) · `.tbss`(0) 섹션에 넣고, 접근할 때마다 "현재 스레드의 TLS 블록 주소 + 오프셋"을 계산한다.

- `picolibc.specs`가 `-ftls-model=local-exec`를 넣는다(B11 §3.4). local-exec는 "TLS 변수는 전부 이 실행 파일 안에 있다"는 가장 단순한 모델이다 — 스레드 포인터(TP) + 링크 때 정해진 상수 오프셋.
- Arm EABI에서 TP는 `__aeabi_read_tp()` 함수로 읽는다. Cortex-A/R에는 CP15 `TPIDRURO` 레지스터가 있지만 **Cortex-M(v6-M/v7-M/v8-M)에는 스레드 포인터 레지스터가 없다.** 그래서 picolibc의 Cortex-M용 `__aeabi_read_tp`는 **전역 변수 `__tls`를 읽는 3명령 함수**다. Zephyr는 같은 일을 `arch/arm/core/cortex_m/__aeabi_read_tp.S`에서 전역 `z_arm_tls_ptr`로 한다.
- RTOS는 태스크마다 TLS 블록을 하나씩 만들고(`_init_tls(block)` = `.tdata` 복사 + `.tbss` 0), 문맥 전환 때 `_set_tls(block)`으로 `__tls`를 바꾼다. newlib의 `_impure_ptr` 대입과 같은 구조다. 차이는 **크기가 "링크된 상태만"** 이라는 것.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="20" font-size="12" text-anchor="middle" font-weight="bold">태스크 A의 스택 영역 (높은 주소가 위)</text>
<rect x="40" y="30" width="260" height="60" rx="5" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="170.0" y="56.2" font-size="12" text-anchor="middle" font-weight="bold">TLS 블록 (_tls_size B)</text>
<text x="170.0" y="72.2" font-size="12" text-anchor="middle">.tdata 복사 + .tbss 0 — errno, strtok…</text>
<rect x="40" y="90" width="260" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="170.0" y="109.2" font-size="12" text-anchor="middle">정렬 여백</text>
<rect x="40" y="120" width="260" height="120" rx="5" fill="#4a7bd0" fill-opacity="0.08" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="170.0" y="176.2" font-size="12" text-anchor="middle" font-weight="bold">스택 (아래로 자란다)</text>
<text x="170.0" y="192.2" font-size="12" text-anchor="middle">pxTopOfStack은 TLS 아래에서 시작</text>
<rect x="40" y="240" width="260" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="170.0" y="264.2" font-size="12" text-anchor="middle">pxStack (스택 바닥)</text>
<rect x="390" y="40" width="270" height="48" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="525.0" y="60.2" font-size="12" text-anchor="middle" font-weight="bold">TCB_t A</text>
<text x="525.0" y="76.2" font-size="12" text-anchor="middle">xTLSBlock → TLS 블록 시작</text>
<rect x="390" y="120" width="270" height="48" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="525.0" y="140.2" font-size="12" text-anchor="middle" font-weight="bold">전역 변수 __tls (소프트웨어 TP)</text>
<text x="525.0" y="156.2" font-size="12" text-anchor="middle">= 현재 태스크 블록 − 8</text>
<rect x="390" y="200" width="270" height="60" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="525.0" y="218.2" font-size="12" text-anchor="middle" font-weight="bold">errno 접근 (local-exec)</text>
<text x="525.0" y="234.2" font-size="12" text-anchor="middle">tp = __aeabi_read_tp()  // ldr ×2</text>
<text x="525.0" y="250.2" font-size="12" text-anchor="middle">&amp;errno = tp + 8 + 오프셋</text>
<line x1="388" y1="64" x2="302" y2="50" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<line x1="388" y1="144" x2="302" y2="60" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<line x1="525" y1="170" x2="525" y2="198" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<text x="340" y="300" font-size="12" text-anchor="middle">Cortex-M에는 Cortex-A/R의 TPIDRURO 같은 스레드 포인터 레지스터가 없다 →</text>
<text x="340" y="318" font-size="12" text-anchor="middle">전환 때마다 _set_tls()가 전역 변수 하나를 바꾼다 (Zephyr는 z_arm_tls_ptr)</text>
</svg>
```

그림 6 — FreeRTOS + picolibc에서 태스크 A의 TLS. 블록은 태스크 스택 꼭대기에서 떼어 내고, TCB는 시작 주소만 갖는다. Cortex-M은 TP 레지스터가 없어서 전역 변수 하나가 그 역할을 한다

FreeRTOS V11.3.2의 `include/picolibc-freertos.h`가 이것을 네 매크로로 구현한다(발췌, 스택이 아래로 자라는 경우).

```c
#include <picotls.h>
#define configUSE_C_RUNTIME_TLS_SUPPORT    1
#define configTLS_BLOCK_TYPE               void *
#define picolibcTLS_SIZE                   ( ( portPOINTER_SIZE_TYPE ) _tls_size() )
#define configINIT_TLS_BLOCK( xTLSBlock, pxTopOfStack )                                  \
    do {                                                                                     \
        xTLSBlock = ( void * ) ( ( ( ( portPOINTER_SIZE_TYPE ) pxTopOfStack ) -              \
                                   picolibcTLS_SIZE ) &                                      \
                                 ~picolibcTLS_ALIGNMENT_MASK );                              \
        pxTopOfStack = ( StackType_t * ) ( ( ( ( portPOINTER_SIZE_TYPE ) xTLSBlock ) - 1 ) & \
                                           ~picolibcSTACK_ALIGNMENT_MASK );                  \
        _init_tls( xTLSBlock );                                                              \
    } while( 0 )
#define configSET_TLS_BLOCK( xTLSBlock )    _set_tls( xTLSBlock )
#define configDEINIT_TLS_BLOCK( xTLSBlock )
```

읽을 곳: TLS 블록을 **스택 꼭대기에서 떼어 내고 `pxTopOfStack`을 그 아래로 내린다.** TCB에는 포인터(4 B)만 늘어난다. 그 대가로 **태스크 스택 크기에서 `_tls_size()`만큼이 빠진다** — `xTaskCreate(…, 512, …)`로 잡은 스택이 TLS만큼 작아진다. 많은 TLS 변수(예: `fcvt_buf` 326 B)를 링크하면 스택 high water mark가 갑자기 나빠지는 이유가 이것이다(F3).

picolibc 쪽 함수도 확인했다(`libc.a`의 `_set_tls`, `__aeabi_read_tp` 디스어셈블).

```asm
00000000 <_set_tls>:
   0:	4b02      	ldr	r3, [pc, #8]	; R_ARM_ABS32 __arm32_tls_tcb_offset
   2:	1ac0      	subs	r0, r0, r3          ; tp = block - tcb_offset (8)
   4:	4b02      	ldr	r3, [pc, #8]	; R_ARM_ABS32 __tls
   6:	6018      	str	r0, [r3, #0]        ; __tls = tp
   8:	4770      	bx	lr
00000000 <__aeabi_read_tp>:
   0:	4801      	ldr	r0, [pc, #4]	; R_ARM_ABS32 __tls
   2:	6800      	ldr	r0, [r0, #0]
   4:	4770      	bx	lr
```

(재배치 주석은 `objdump -dr` 출력을 한 줄로 합쳤다.) Arm의 TLS 배치는 "TP 뒤 8바이트는 TCB 자리, 그 다음부터 TLS 데이터"(variant 1)라서 `_set_tls`가 블록 주소에서 8을 뺀다. errno 한 번 읽는 비용 = `bl __aeabi_read_tp`(ldr 2개) + 오프셋 더하기 + ldr. 레지스터 하나로 끝나는 Cortex-A보다 조금 비싸지만 함수 호출 하나 수준이다.

### 5.2 예제 10 — FreeRTOS + picolibc: errno가 태스크별이 되는가, 그리고 TLS 포인터를 안 잡으면

무엇을 확인하는 코드인지: 예제 4와 같은 errno 실험을 picolibc로 한다. ① `configUSE_PICOLIBC_TLS=0` ② `=1` ③ 시작 코드에서 main용 TLS를 안 잡은 경우(`-DNO_MAIN_TLS`). 랩 템플릿의 링커 스크립트와 시작 코드는 TLS를 모르므로, 템플릿 `mps2_m3.ld`를 복사해 TLS 섹션과 picolibc가 찾는 심볼을 추가했다(`.bss` 바로 앞에 끼운 부분).

```text
    /* --- picolibc TLS: .tdata image in FLASH, main-thread block in RAM (F7) --- */
    .tdata : { . = ALIGN(8); __tls_base = .; *(.tdata .tdata.*) . = ALIGN(4); } > RAM AT > FLASH
    __tdata_source = LOADADDR(.tdata);
    __tdata_size   = SIZEOF(.tdata);
    .tbss (NOLOAD) : { *(.tbss .tbss.*) . = ALIGN(4); __tls_end = .; } > RAM
    .tbss_space (NOLOAD) : { . = ADDR(.tbss) + SIZEOF(.tbss); } > RAM
    __tbss_offset  = ADDR(.tbss) - __tls_base;
    __tbss_size    = SIZEOF(.tbss);
    __tls_align    = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss));
    __tls_size     = __tls_end - __tls_base;
    __arm32_tls_tcb_offset = MAX(8, __tls_align);
```

`.tbss`는 링커가 주소 공간을 차지하지 않는 섹션으로 다루므로(TLS 템플릿일 뿐이라서) 뒤에 `.tbss_space`로 main 스레드용 자리를 실제로 잡는다 — picolibc 자신의 `picolibc.ld`가 쓰는 방법을 줄인 것이다. `_init_tls`가 쓰는 심볼(`__tdata_source`, `__tdata_size`, `__tbss_offset`, `__tbss_size`)은 `objdump -r`로 `libc.a`의 `inittls.c.o` 재배치에서 찾았다.

빌드 스크립트(보드 코드는 B11의 picolibc용 `mb.c` · `pico_io.c`를 그대로 썼다):

```sh
#!/bin/bash
# build.sh <src.c> <out> [CFLAGS...] — FreeRTOS V11.3.2 + picolibc on mps2-an385 (own ld with TLS)
src=$1; out=/var/tmp/rtos/F7/$2; shift 2; mkdir -p $out
T=/var/rtoslab/templates; K=/var/rtoslab/src/FreeRTOS-Kernel; P=/Users/donh/.rtos_scratch/F7/pico
arm-none-eabi-gcc --specs=picolibc.specs -mthumb -mcpu=cortex-m3 -O2 -g -std=gnu11 -Wall -Wextra \
  -ffunction-sections -fdata-sections -I$(dirname $src) -I$T/freertos-mps2 -I$T/common -I$K/include -I$K/portable/GCC/ARM_CM3 "$@" \
  $src $P/mb.c $P/pico_io.c $T/common/startup.c $T/freertos-mps2/hooks.c \
  $K/tasks.c $K/list.c $K/queue.c $K/timers.c $K/portable/MemMang/heap_4.c $K/portable/GCC/ARM_CM3/port.c \
  -T $P/mps2_tls.ld -nostartfiles -Wl,--gc-sections -Wl,-Map=$out/app.map -o $out/app.elf && arm-none-eabi-size $out/app.elf | tail -1
```

```c
/* ex7 — FreeRTOS + picolibc: errno is a __thread variable. configUSE_PICOLIBC_TLS 0 vs 1 */
#include <errno.h>
#include <picotls.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#define N 200
extern char __tls_base[];
static volatile unsigned lost, seen;
static volatile int *addr[2];
static void spin_us(uint32_t us) { uint32_t t0 = board_now_us(); while (board_now_us() - t0 < us) { } }
static void task_a(void *arg)
{
    (void)arg; addr[0] = &errno;
    volatile int i_local = 0; (void)i_local;
    for (int i = 0; i < N; i++) {
        errno = 0;
        (void)strtol("99999999999", NULL, 10);       /* ERANGE */
        spin_us(300);
        if (errno == ERANGE) seen++; else lost++;
    }
    board_printf_raw("PICOLIBC_TLS=%d: ERANGE seen=%u lost=%u  tls_size=%u B\n",
                     configUSE_PICOLIBC_TLS, seen, lost, (unsigned)_tls_size());
    board_printf_raw("  &errno main=0x%x A=0x%x B=0x%x\n", (unsigned)__tls_base, (unsigned)addr[0], (unsigned)addr[1]);
    board_printf_raw("  a local variable in A lives at 0x%x (A's stack)\n", (unsigned)&i_local);
    board_exit(0);
}
static void task_b(void *arg)
{
    (void)arg; addr[1] = &errno;
    for (;;) { vTaskDelay(1); errno = 0; (void)strtol("42", NULL, 10); }
}
int main(void)
{
#ifndef NO_MAIN_TLS
    _init_tls(__tls_base);                            /* crt0 would do this; our startup.c does not */
    _set_tls(__tls_base);
#endif
    errno = 7;                                        /* main-thread errno write */
    board_printf_raw("main: &errno=0x%x errno=%d\n", (unsigned)&errno, errno);
    xTaskCreate(task_a, "A", 512, NULL, 1, NULL);
    xTaskCreate(task_b, "B", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
B=/Users/donh/.rtos_scratch/F7/pico/build.sh; S=/Users/donh/.rtos_scratch/F7/ex7/main.c
for m in 0 1; do $B $S ex7_$m -DconfigUSE_PICOLIBC_TLS=$m && ICOUNT=1 rtos-run-mps2 /var/tmp/rtos/F7/ex7_$m/app.elf; done
$B $S ex7_n -DconfigUSE_PICOLIBC_TLS=0 -DNO_MAIN_TLS && ICOUNT=1 rtos-run-mps2 /var/tmp/rtos/F7/ex7_n/app.elf 5
```

```text
   9292	      4	 135100	 144396	  2340c	/var/tmp/rtos/F7/ex7_0/app.elf
main: &errno=0x20000008 errno=7
PICOLIBC_TLS=0: ERANGE seen=142 lost=58  tls_size=4 B
  &errno main=0x20000008 A=0x20000008 B=0x20000008
  a local variable in A lives at 0x20001794 (A's stack)
[exit 0]
   9340	      4	 135108	 144452	  23444	/var/tmp/rtos/F7/ex7_1/app.elf
main: &errno=0x20000008 errno=7
PICOLIBC_TLS=1: ERANGE seen=200 lost=0  tls_size=4 B
  &errno main=0x20000008 A=0x200017bc B=0x20002034
  a local variable in A lives at 0x20001794 (A's stack)
[exit 0]
   9212	      4	 135100	 144316	  233bc	/var/tmp/rtos/F7/ex7_n/app.elf
main: &errno=0x8 errno=7
PICOLIBC_TLS=0: ERANGE seen=141 lost=59  tls_size=4 B
  &errno main=0x20000008 A=0x8 B=0x8
  a local variable in A lives at 0x20001794 (A's stack)
[exit 0]
```

출력에서 볼 것:

- `PICOLIBC_TLS=0`: 모든 태스크가 main의 TLS 블록(`__tls_base` = `0x20000008`)을 쓴다 → 예제 4와 같은 레이스(lost 58).
- `PICOLIBC_TLS=1`: A의 errno가 `0x200017bc` — A의 지역 변수(`0x20001794`) **바로 위 40 B**, 즉 A 스택의 꼭대기다. 그림 6 그대로다. lost 0. 이 프로그램의 TLS는 **4 B**(errno 하나)다 — newlib-nano의 76 B + 지연 할당과 비교하라(그림 5). FLASH는 48 B 늘었다(9292 → 9340).
- **`NO_MAIN_TLS`: `&errno = 0x8`.** 시작 코드가 `_set_tls`를 안 불러서 `__tls`가 0이고, errno 주소가 0 + 8 + 오프셋 0 = `0x8` — **벡터 테이블의 NMI 핸들러 자리**다. `errno = 7`이 NMI 벡터를 7로 덮어썼다. QEMU의 AN385는 주소 0이 쓰기 가능한 SSRAM이라 조용히 지나갔고, 실칩이라면 플래시 쓰기 → BusFault이거나, 0 근처가 RAM으로 리맵된 칩이라면 **조용한 벡터 손상**이다. picolibc의 `crt0`를 쓰면 이것을 해 주지만, 자기 시작 코드(B5)를 쓰는 펌웨어는 직접 챙겨야 한다. 스케줄러가 돌기 시작하면 `PICOLIBC_TLS=1`이 태스크마다 바르게 설정하지만, **main에서 스케줄러 시작 전에 errno를 건드리는 코드**(초기화 중 `strtol` 등)가 이 함정에 걸린다.

### 5.3 picolibc의 락

picolibc도 malloc과 stdio 일부를 `__retarget_lock_*`로 보호한다. 아카이브를 보면 기본 구현이 들어 있는데 크기가 각각 **2 B(`bx lr`)** 다.

```sh
P=/usr/lib/picolibc/arm-none-eabi/lib/thumb/v7-m/nofp
arm-none-eabi-nm -S $P/libc.a | grep -E "T __retarget_lock_acquire(_recursive)?$"
arm-none-eabi-nm -A $P/libc.a | grep "nano-malloc.c.o" | grep -E "lock|errno|read_tp"
```

```text
00000000 00000002 T __retarget_lock_acquire
00000000 00000002 T __retarget_lock_acquire_recursive
/usr/lib/picolibc/arm-none-eabi/lib/thumb/v7-m/nofp/libc.a:nano-malloc.c.o:         U __aeabi_read_tp
/usr/lib/picolibc/arm-none-eabi/lib/thumb/v7-m/nofp/libc.a:nano-malloc.c.o:         U __lock___libc_recursive_mutex
/usr/lib/picolibc/arm-none-eabi/lib/thumb/v7-m/nofp/libc.a:nano-malloc.c.o:         U __retarget_lock_acquire_recursive
/usr/lib/picolibc/arm-none-eabi/lib/thumb/v7-m/nofp/libc.a:nano-malloc.c.o:         U __retarget_lock_release_recursive
/usr/lib/picolibc/arm-none-eabi/lib/thumb/v7-m/nofp/libc.a:nano-malloc.c.o:         U errno
```

즉 picolibc는 **TLS로 "호출 간 상태"는 풀었지만, malloc 같은 공유 자원의 락은 여전히 RTOS가 줘야 한다.** 락 하나(`__lock___libc_recursive_mutex`)를 모든 곳이 같이 쓴다. FreeRTOS + picolibc라면 `__retarget_lock_*_recursive`를 §3.6처럼 구현한다. Zephyr는 `lib/libc/picolibc/locks.c`에서 `struct __lock { struct k_mutex m; }`로 구현해 두었다. 그리고 picolibc의 tinystdio는 **FILE 버퍼가 없다**(B11 §3.2: bss 0) — 예제 12의 "버퍼 중복" 같은 손상은 구조적으로 덜 생기지만, ISR과 태스크의 출력이 글자 단위로 섞이는 문제는 그대로다.

### 5.4 예제 11 — Zephyr v4.4.2: 툴체인에 따라 errno의 집이 바뀐다

무엇을 확인하는 코드인지: Zephyr의 기본 libc는 picolibc다(`lib/libc/Kconfig` 79~84행 `choice` 기본값 `PICOLIBC`). 같은 앱을 Ubuntu 툴체인(`gnuarmemb`)과 Zephyr SDK 1.0.1로 빌드해 errno가 어디 사는지 본다. 스레드 A(우선순위 5)가 ERANGE를 만들고 3 ms 바쁜 대기하는 동안 B(우선순위 2, 더 높음)가 매 tick `errno = 0`을 한다.

```c
/* z1 — Zephyr v4.4.2 + picolibc: is errno per thread, and where does it live? */
#include <errno.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/linker-defs.h>
#define N 200
static volatile unsigned seen, lost;
static volatile int *addr[2];
K_THREAD_STACK_DEFINE(stack_a, 1024);
K_THREAD_STACK_DEFINE(stack_b, 1024);
static struct k_thread th_a, th_b;
static void spin_us(uint32_t us) { k_busy_wait(us); }
static void task_a(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    addr[0] = &errno;
    for (int i = 0; i < N; i++) {
        errno = 0;
        (void)strtol("99999999999", NULL, 10);      /* ERANGE */
        spin_us(3000);                              /* B (higher prio) runs every tick meanwhile */
        if (errno == ERANGE) seen++; else lost++;
    }
    printk("ERANGE seen=%u lost=%u\n", seen, lost);
    printk("&errno seen by A=%p, by B=%p\n", (void *)addr[0], (void *)addr[1]);
    printk("A stack [%p .. %p)\n", (void *)th_a.stack_info.start,
           (void *)(th_a.stack_info.start + th_a.stack_info.size));
#ifdef CONFIG_THREAD_LOCAL_STORAGE
    printk("A tls=%p, TLS block: tdata=%u B tbss=%u B per thread\n", (void *)th_a.tls,
           (unsigned)(uintptr_t)__tdata_size, (unsigned)(uintptr_t)__tbss_size);
#else
    printk("no TLS: errno lives in struct k_thread (&th_a.errno_var=%p)\n", (void *)&th_a.errno_var);
#endif
}
static void task_b(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    addr[1] = &errno;
    for (;;) { k_sleep(K_TICKS(1)); errno = 0; (void)strtol("42", NULL, 10); }
}
int main(void)
{
    printk("PICOLIBC=%d THREAD_LOCAL_STORAGE=%d LIBC_ERRNO=%d ERRNO_IN_TLS=%d\n",
           IS_ENABLED(CONFIG_PICOLIBC), IS_ENABLED(CONFIG_THREAD_LOCAL_STORAGE),
           IS_ENABLED(CONFIG_LIBC_ERRNO), IS_ENABLED(CONFIG_ERRNO_IN_TLS));
    k_thread_create(&th_b, stack_b, K_THREAD_STACK_SIZEOF(stack_b), task_b, NULL, NULL, NULL, 2, 0, K_NO_WAIT);
    k_thread_create(&th_a, stack_a, K_THREAD_STACK_SIZEOF(stack_a), task_a, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    k_thread_join(&th_a, K_FOREVER);
    return 0;
}
```

`prj.conf`는 `CONFIG_PRINTK=y`, `CONFIG_QEMU_ICOUNT_SHIFT=5` 두 줄이다.

```sh
rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/F7/z1 /var/tmp/rtos/F7/z1
grep -E "CONFIG_(PICOLIBC|THREAD_LOCAL_STORAGE|LIBC_ERRNO|ERRNO|PICOLIBC_USE_[A-Z]+)=" /var/tmp/rtos/F7/z1/zephyr/.config
rtos-zrun /var/tmp/rtos/F7/z1 15
ZTC=zephyr rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/F7/z1 /var/tmp/rtos/F7/z1s      # Zephyr SDK 툴체인
grep -E "CONFIG_(PICOLIBC|THREAD_LOCAL_STORAGE|LIBC_ERRNO|ERRNO|PICOLIBC_USE_[A-Z]+)=" /var/tmp/rtos/F7/z1s/zephyr/.config
rtos-zrun /var/tmp/rtos/F7/z1s 15
```

```text
CONFIG_ERRNO=y
CONFIG_PICOLIBC=y
CONFIG_PICOLIBC_USE_MODULE=y
*** Booting Zephyr OS build v4.4.2 ***
PICOLIBC=1 THREAD_LOCAL_STORAGE=0 LIBC_ERRNO=0 ERRNO_IN_TLS=0
ERANGE seen=200 lost=0
&errno seen by A=0x20000188, by B=0x20000108
A stack [0x200007f0 .. 0x20000bf0)
no TLS: errno lives in struct k_thread (&th_a.errno_var=0x20000188)
CONFIG_LIBC_ERRNO=y
CONFIG_ERRNO=y
CONFIG_THREAD_LOCAL_STORAGE=y
CONFIG_PICOLIBC=y
CONFIG_PICOLIBC_USE_TOOLCHAIN=y
*** Booting Zephyr OS build v4.4.2 ***
PICOLIBC=1 THREAD_LOCAL_STORAGE=1 LIBC_ERRNO=1 ERRNO_IN_TLS=0
ERANGE seen=200 lost=0
&errno seen by A=0x20000bf4, by B=0x200007f4
A stack [0x200007f8 .. 0x20000bf8)
A tls=0x20000be8, TLS block: tdata=0 B tbss=8 B per thread
```

(QEMU의 `nic lan9118.0 has no peer` 경고 줄은 뺐다.)

출력에서 볼 것:

- **두 빌드 모두 lost 0** — Zephyr에서는 설정을 따로 하지 않아도 errno가 스레드별이다. FreeRTOS와 가장 큰 차이다.
- Ubuntu 툴체인: picolibc를 **Zephyr 모듈 소스로 빌드**(`PICOLIBC_USE_MODULE`)하고 TLS를 쓰지 않는다. errno는 `struct k_thread` 안의 `errno_var`(A의 errno 주소 = `&th_a.errno_var`, 스택 밖 커널 객체 안). picolibc는 `z_errno_wrap()` → `z_errno()`로 현재 스레드의 칸을 얻는다(`lib/libc/picolibc/errno_wrap.c`).
- Zephyr SDK: 툴체인의 picolibc(`PICOLIBC_USE_TOOLCHAIN`)를 쓰고, 이것이 `THREAD_LOCAL_STORAGE`를 select하고, `PICOLIBC`가 `LIBC_ERRNO`를 select한다(`lib/libc/Kconfig` 103~104행). errno는 picolibc의 `__thread` 변수이고, A의 errno(`0x20000bf4`)는 A 스택의 끝(`0x20000bf8`) 바로 아래다. TLS 블록 8 B = `errno` + Zephyr의 `z_tls_current`(현재 스레드 포인터 캐시, `readelf -s`로 확인). `arch_tls_stack_setup()`(`arch/arm/core/tls.c`)이 "TLS 크기 + 2워드"를 스택에서 떼므로 스레드당 16 B다(그림 5).
- 문맥 전환에서 Zephyr가 하는 일: `arch/arm/core/cortex_m/swap_helper.S` 177~178행 `ldr r4, =z_arm_tls_ptr; str r0, [r4]` — 새 스레드의 `tls` 값을 전역 `z_arm_tls_ptr`에 쓴다. FreeRTOS의 `_set_tls`와 같은 일이다.

---

## 6. ISR에서 printf 금지 — libc 쪽 이유를 실험으로

### 6.1 이유 여섯 가지

E1 §4는 "printf는 출력 장치를 기다린다(115200 baud에서 23자 = 2 ms)"를 보였다. 그것 말고도 libc 안에 이유가 다섯 개 더 있다.

| # | 이유 | 무엇이 깨지나 | 이 노트의 증거 |
|---|---|---|---|
| 1 | `stdout` FILE은 전역 공유 상태이고, Ubuntu newlib에는 FILE 락이 아예 없다 | 출력이 섞이고, 버퍼 포인터가 꼬여 **같은 줄이 두 번** 나오거나 사라진다 | 예제 12 |
| 2 | FILE 락이 있는 libc라면 ISR이 락을 기다린다 | 데드락 또는 assert | 예제 13 |
| 3 | malloc을 부른다 — 첫 출력 때 버퍼(`__smakebuf_r`), `%f` 변환 때 Bigint(`_Balloc`) | ISR 안 malloc → 예제 8의 크래시 | 예제 14, B11 §3.2 |
| 4 | 스택을 많이 쓴다 (정수 400 B대, `%f` 750 B) | ISR은 MSP를 쓴다. 중첩 IRQ마다 쌓인다 → MSP 오버플로 | 예제 14 |
| 5 | 오래 걸린다 (UART 없이 포맷만 15~110 µs) | 같거나 낮은 우선순위 IRQ와 모든 태스크 지연 | 예제 12 `isr_max`, 예제 14 |
| 6 | errno를 쓸 수 있다 | 끊긴 태스크의 errno가 바뀐다 (§4.2) | §2.6 |

### 6.2 예제 12 — 태스크가 printf하는 중에 ISR이 printf하면

무엇을 확인하는 코드인지: 로그 태스크가 `"T:nnnnn ABC…XYZ\n"`(35 B) 2000줄을 printf하고, TIMER0 ISR이 `"I:nnnnn\n"`(8 B)을 printf한다. 출력 전체를 호스트 Python으로 검사해 "온전한 줄 수"와 "**받은 바이트 − 보낸 바이트**"를 센다. 바이트 차이가 +이면 같은 내용이 두 번 나간 것이다. `BUF`: 0 무버퍼(랩 기본), 1 줄 버퍼(`setvbuf(stdout, iobuf, _IOLBF, 128)` — 실제 제품의 흔한 설정), 2 뮤텍스로 감싼 로거.

```c
/* ex5 — an ISR calls printf while a task is inside printf. BUF 0: unbuffered, 1: line-buffered, 2: mutex-guarded */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "board.h"
#ifndef BUF
#define BUF 0
#endif
#ifndef LINES
#define LINES 300
#endif
#ifndef RELOAD
#define RELOAD 24989                                  /* ~1 ms */
#endif
static char iobuf[128];
static SemaphoreHandle_t mtx;
static volatile unsigned isr_n, isr_max_us;
static void log_printf_u(const char *fmt, unsigned v)        /* "thread-safe" logger used by everyone */
{
#if BUF == 2
    xSemaphoreTake(mtx, portMAX_DELAY);
#endif
    printf(fmt, v);
#if BUF == 2
    xSemaphoreGive(mtx);
#endif
}
void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    uint32_t t0 = board_now_us();
    log_printf_u("I:%05u\n", isr_n++);
    uint32_t dt = board_now_us() - t0;
    if (dt > isr_max_us) isr_max_us = dt;
}
static void logger(void *arg)
{
    (void)arg;
    if (BUF == 1) setvbuf(stdout, iobuf, _IOLBF, sizeof iobuf);
    TIMER_CTRL(TIMER0_BASE) = 1 | 8;
    for (unsigned i = 0; i < LINES; i++)
        log_printf_u("T:%05u ABCDEFGHIJKLMNOPQRSTUVWXYZ\n", i);
    TIMER_CTRL(TIMER0_BASE) = 0;
    fflush(stdout);
    board_printf_raw("\n#SUMMARY BUF=%d task_lines=%d isr_calls=%u isr_max=%u us\n", BUF, LINES, isr_n, isr_max_us);
    board_exit(0);
}
int main(void)
{
    mtx = xSemaphoreCreateMutex();
    TIMER_RELOAD(TIMER0_BASE) = RELOAD; TIMER_VALUE(TIMER0_BASE) = RELOAD;
    NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;
    xTaskCreate(logger, "log", 1024, NULL, 1, NULL);
    vTaskStartScheduler();
    return 0;
}
```

`ex5/FreeRTOSConfig.h`(예제 13에서 assert를 끄려고 둔 것):

```c
/* ex5/ex6 — template config, optionally with configASSERT compiled out */
#ifndef F7_CONFIG_H
#define F7_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#ifdef NO_ASSERT
#undef configASSERT
#define configASSERT( x )    ( ( void ) sizeof( x ) )     /* "assert off" without unused warnings */
#endif
#endif
```

검사 스크립트:

```python
#!/usr/bin/env python3
"""check_log.py < output — classify lines printed by ex5/ex6 and account for every byte"""
import re, sys
TL = 'T:{:05d} ABCDEFGHIJKLMNOPQRSTUVWXYZ\n'; IL = 'I:{:05d}\n'
raw = sys.stdin.read()
body, _, tail = raw.partition('\n#SUMMARY')
s = re.search(r'task_lines=(\d+) isr_calls=(\d+)', tail); nt, ni = int(s[1]), int(s[2])
lines = body.split('\n')
good_t = sum(1 for l in lines if re.fullmatch(r'T:\d{5} ABCDEFGHIJKLMNOPQRSTUVWXYZ', l))
good_i = sum(1 for l in lines if re.fullmatch(r'I:\d{5}', l))
broken = [l for l in lines if l and not re.fullmatch(r'(T:\d{5} ABCDEFGHIJKLMNOPQRSTUVWXYZ)|(I:\d{5})', l)]
expect = sum(len(TL.format(k)) for k in range(nt)) + sum(len(IL.format(k)) for k in range(ni))
got = len(body)
print('#SUMMARY' + tail.splitlines()[0])
print(f"  intact task lines {good_t}/{nt}, intact isr lines {good_i}/{ni}, broken lines {len(broken)}")
print(f"  bytes expected {expect}, bytes received {got} (diff {got - expect:+d})")
for b in broken[:3]: print("  e.g.", repr(b[:60]))
```

```sh
for rl in 24989 4999; do for b in 0 1; do      # IRQ 주기 1 ms, 200 us
  TMO=60 $R ex5 ex5_${b}_$rl "EXTRA_CFLAGS=-DBUF=$b -DLINES=2000 -DRELOAD=$rl" > /var/tmp/rtos/F7/ex5_${b}_$rl.out
  python3 /Users/donh/.rtos_scratch/F7/check_log.py < /var/tmp/rtos/F7/ex5_${b}_$rl.out; done; done
```

```text
#SUMMARY BUF=0 task_lines=2000 isr_calls=409 isr_max=60 us
  intact task lines 1610/2000, intact isr lines 21/409, broken lines 762
  bytes expected 73272, bytes received 73283 (diff +11)
  e.g. 'T:0I:00000'
  e.g. '0005 ABCDEFGHIJKLMNOPQRSTUVWXYZ'
  e.g. 'T:00014 ABCDEFGHIJKLMNOPQRSTUVI:00002'
#SUMMARY BUF=1 task_lines=2000 isr_calls=129 isr_max=38 us
  intact task lines 1879/2000, intact isr lines 13/129, broken lines 243
  bytes expected 71032, bytes received 71065 (diff +33)
  e.g. 'T:00015 ABCDEFGHIJKLMNOPQRSTUVWXYI:00000'
  e.g. 'Z'
  e.g. 'T:I:00001'
#SUMMARY BUF=0 task_lines=2000 isr_calls=2693 isr_max=60 us
  intact task lines 1/2000, intact isr lines 98/2693, broken lines 4506
  bytes expected 91544, bytes received 91566 (diff +22)
  e.g. 'TI:00000'
  e.g. ':00001 ABCDEFGHIJKLMNOPQRI:00001'
  e.g. 'STUVWXYZ'
#SUMMARY BUF=1 task_lines=2000 isr_calls=737 isr_max=45 us
  intact task lines 1349/2000, intact isr lines 81/737, broken lines 1354
  bytes expected 75896, bytes received 76654 (diff +758)
  e.g. 'T:I:00000'
  e.g. '00003 ABCDEFGHIJKLMNOPQRSTUVWXYZ'
  e.g. 'T:00008I:00002'
```

200 µs · 줄 버퍼 출력에서 바이트가 남는 곳을 찾아보면 이렇다.

```sh
grep -E "^T:[0-9]{5} ABCDEFGHIJKLMNOPQRSTUVWXYZ$" ex5_1_4999.out | sort | uniq -d | head -1
grep -n -B1 -A2 "T:00621 ABCDEFGHIJKLMNOPQRSTUVWXYZ" ex5_1_4999.out
```

```text
T:00621 ABCDEFGHIJKLMNOPQRSTUVWXYZ
864-T:00620 ABCDEFGHIJKLMNOPQRSTUVWXYZ
865:T:00621 ABCDEFGHIJKLMNOPQRSTUVWXYZ
866-I:00229
867:T:00621 ABCDEFGHIJKLMNOPQRSTUVWXYZ
868-T:00622 ABCDEFGHIJKLMNOPQRSTUVWXYZ
```

출력에서 볼 것:

- **섞임은 당연하고, 중복은 FILE 손상이다.** 무버퍼에서도 ISR 줄이 태스크 줄 한가운데 끼어든다(`T:00014 …UVI:00002`). 줄 버퍼에서는 더 나쁘다 — **`T:00621` 줄이 두 번 출력됐다.** 200 µs 주기에서 758 B가 더 나갔다. 로그를 보고 "이 이벤트가 두 번 일어났다"고 판단하게 만드는 손상이다.
- 일어난 순서(그림 7): 태스크의 printf가 줄을 버퍼에 다 쓰고 flush를 시작한 순간 IRQ가 들어왔다. ISR의 printf가 **같은 버퍼**에 자기 줄을 이어 쓰고, `\n`이라 버퍼 전체를 내보낸 뒤 `_p`를 버퍼 처음으로 되돌렸다. 태스크는 미리 읽어 둔 길이로 한 번 더 내보냈다. newlib의 `__sflush_r`/`__sfvwrite_r`는 FILE 락이 있다는 전제로 짜여 있는데, 이 빌드에서는 그 락이 빈 매크로다(§3.1).
- ISR 안에서 보낸 시간이 최대 **38~60 µs**다(UART가 즉시 받는 QEMU에서). 포맷 + newlib stdio 경로만으로 이만큼이다. 실칩 115200 baud UART였다면 E1의 계산대로 ms 단위가 된다.
- IRQ가 잦을수록(1 ms → 200 µs) 손상이 커진다. 무버퍼 200 µs에서는 온전한 태스크 줄이 2000줄 중 1줄뿐이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">줄 버퍼 stdout을 태스크와 ISR이 같이 쓸 때 (newlib, 락 없음)</text>
<rect x="20" y="34" width="640" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="340.0" y="57.2" font-size="12" text-anchor="middle">① 태스크: "T:00621 ABC…XYZ\n" 을</text>
<text x="340.0" y="73.2" font-size="12" text-anchor="middle">버퍼에 다 쓰고 flush 시작 (_p, _w를 레지스터로 읽음)</text>
<rect x="20" y="98" width="640" height="54" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="340.0" y="121.2" font-size="12" text-anchor="middle">② IRQ → ISR printf("I:00229\n"): 같은 버퍼에 이어 쓰고</text>
<text x="340.0" y="137.2" font-size="12" text-anchor="middle">"\n"이라 flush — 버퍼 전체(태스크 줄 + ISR 줄)를 _write</text>
<line x1="340" y1="88" x2="340" y2="97" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="20" y="162" width="640" height="54" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="340.0" y="193.2" font-size="12" text-anchor="middle">③ ISR이 _p를 버퍼 처음으로 되돌리고 복귀</text>
<line x1="340" y1="152" x2="340" y2="161" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="20" y="226" width="640" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="340.0" y="249.2" font-size="12" text-anchor="middle">④ 태스크: 아까 읽어 둔 낡은 길이로 _write — 같은 줄을 한 번 더</text>
<text x="340.0" y="265.2" font-size="12" text-anchor="middle">출력: T:00621 / I:00229 / T:00621 (중복)</text>
<line x1="340" y1="216" x2="340" y2="225" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
</svg>
```

그림 7 — 줄 버퍼 `stdout`에서 같은 줄이 두 번 나오는 과정. 태스크와 ISR이 FILE 구조체(`_p`, `_w`)를 락 없이 같이 고친다

### 6.3 예제 13 — "그럼 printf를 뮤텍스로 감싸면?"

무엇을 확인하는 코드인지: 예제 12의 `BUF=2` — 모든 로그가 `xSemaphoreTake(mtx)` → printf → give를 거친다. 태스크끼리는 이것으로 충분하다. ISR도 같은 로거를 부르면? assert를 켠 경우와 끈 경우.

```sh
TMO=8 $R ex5 ex5_2 EXTRA_CFLAGS=-DBUF=2 | tail -3
TMO=8 $R ex5 ex5_2n "EXTRA_CFLAGS=-DBUF=2 -DNO_ASSERT" > /var/tmp/rtos/F7/ex5_2n.out; echo "rc=$?"; tail -c 150 /var/tmp/rtos/F7/ex5_2n.out
```

```text
[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:438
[board_exit] code=253
qemu-system-arm: terminating on signal 15 from pid 1004720 (timeout)
rc=124
STUVWXYZ
T:00001 ABCDEFGHIJKLMNOPQRSTUVWXYZ
T:00002 ABCDEFGHIJKLMNOPQRSTUVWXYZ
T:00003 ABCDEFGHIJKLMNOPQRSTUVWXYZ
T:00004 ABCDEFGHIJKLMNOPQRSTUVWXYZ
T
```

출력에서 볼 것: assert를 켜면 첫 ISR printf에서 `vPortEnterCritical()`의 "ISR에서 부르면 안 되는 API" assert(예제 8과 같은 438행). **assert를 끄면 다섯 줄 반을 찍고 멈춘다** — 태스크가 뮤텍스를 쥐고 `T`를 출력한 직후 ISR이 같은 뮤텍스를 기다리려 했고, 기다릴 수 없는 문맥에서 블로킹 경로를 탄 커널이 진행하지 못했다(QEMU는 8초 timeout으로 끝남, rc=124). 멈춘 정확한 위치를 이 노트에서 gdb로 확인하지는 않았다 — 같은 종류의 "ISR 안 블로킹 take"가 커널 안에서 어디서 도는지는 E1 예제 7에서 봤다. **"스레드 안전하게 만든 printf"는 ISR에서 더 위험하다**는 것이 결론이다(그림 2의 주황 상자).

### 6.4 예제 14 — printf 한 번의 시간과 스택, 그리고 `%f`가 malloc을 부른다

무엇을 확인하는 코드인지: UART를 빼고 `snprintf` 한 번의 비용만 잰다. 시간은 FPGAIO 25 MHz 카운터(ICOUNT), 스택은 SP 아래 4 KB를 칠해 두고 지워진 깊이를 본다(B11 §9.2와 같은 방법). libc 네 구성: newlib-nano, nano + `-u _printf_float`, full newlib, picolibc(기본 = double 지원).

```c
/* ex8 — cost of one snprintf: time (FPGAIO 25 MHz counter, ICOUNT) and stack (paint & measure) */
#include <stdint.h>
#include <stdio.h>
#include "board.h"
#define CNT   REG32(0x40028018u)                     /* MPS2 FPGAIO COUNTER, 25 MHz */
#define PAINT 4096
static char buf[48];
static volatile int vi = 12345;
static volatile double vd = 3.14159;
typedef int (*fn_t)(void);
static int f_int(void)   { return snprintf(buf, sizeof buf, "%d", vi); }
static int f_3int(void)  { return snprintf(buf, sizeof buf, "alt=%d roll=%d yaw=%d", vi, -vi, vi / 7); }
static int f_float(void) { return snprintf(buf, sizeof buf, "%.3f", vd); }
static uint32_t measure(fn_t f, uint32_t *stack_bytes)
{
    uint32_t sp; __asm volatile("mov %0, sp" : "=r"(sp));
    uint32_t *hi = (uint32_t *)(sp - 128), *lo = hi - PAINT / 4;
    for (uint32_t *p = lo; p < hi; p++) *p = 0xA5A5A5A5u;
    uint32_t c0 = CNT; f(); uint32_t c1 = CNT;
    uint32_t *p = lo; while (p < hi && *p == 0xA5A5A5A5u) p++;
    *stack_bytes = (uint32_t)((char *)hi - (char *)p) + 128;
    return c1 - c0;                                  /* counter ticks (40 ns each) */
}
int main(void)
{
    static const struct { const char *name; fn_t f; } t[] = {
        { "%d      ", f_int }, { "3 x %d  ", f_3int }, { "%.3f    ", f_float } };
    board_printf_raw("[%s]\n", LIBC);
    for (unsigned i = 0; i < 3; i++) {
        uint32_t s1, s2, c1 = measure(t[i].f, &s1), c2 = measure(t[i].f, &s2);
        board_printf_raw("  %s -> \"%s\" first %u ns, again %u ns, stack %u B\n",
                         t[i].name, buf, c1 * 40, c2 * 40, s2 > s1 ? s2 : s1);
    }
    board_exit(0);
}
```

```sh
#!/bin/bash
# run8.sh — build ex8 against four libc configurations and run each with ICOUNT=1
S=/Users/donh/.rtos_scratch/F7/ex8/main.c; O=/var/tmp/rtos/F7/ex8; T=/var/rtoslab/templates; P=/Users/donh/.rtos_scratch/F7/pico
LD="-T $T/common/mps2_m3.ld -nostartfiles -Wl,--gc-sections"
B="make -s -f $T/baremetal-mps2/Makefile SRCS=$S"
$B OUT=$O/nano  EXTRA_CFLAGS='-DLIBC="\"newlib-nano\""' >/dev/null
$B OUT=$O/nanof EXTRA_CFLAGS='-DLIBC="\"newlib-nano -u _printf_float\""' EXTRA_LDFLAGS="-u _printf_float" >/dev/null
$B OUT=$O/full  EXTRA_CFLAGS='-DLIBC="\"newlib full\""' LDFLAGS="$LD" >/dev/null
mkdir -p $O/pico && arm-none-eabi-gcc --specs=picolibc.specs -mthumb -mcpu=cortex-m3 -O2 -std=gnu11 -Wall -Wextra \
  -ffunction-sections -fdata-sections -I$T/common '-DLIBC="picolibc (double)"' $S $P/mb.c $P/pico_io.c $T/common/startup.c \
  -T $P/mps2_tls.ld -nostartfiles -Wl,--gc-sections -o $O/pico/app.elf
for v in nano nanof full pico; do ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/$v/app.elf | grep -v exit; done
for v in nano nanof full pico; do printf "%-6s " $v; arm-none-eabi-size $O/$v/app.elf | tail -1 | awk '{print "text="$1" data="$2" bss="$3}'; done
```

```text
[newlib-nano]
  %d       -> "12345" first 15800 ns, again 15800 ns, stack 412 B
  3 x %d   -> "alt=12345 roll=-12345 yaw=1763" first 52840 ns, again 52840 ns, stack 428 B
  %.3f     -> "" first 8280 ns, again 8280 ns, stack 308 B
[newlib-nano -u _printf_float]
  %d       -> "12345" first 15800 ns, again 15800 ns, stack 412 B
  3 x %d   -> "alt=12345 roll=-12345 yaw=1763" first 52800 ns, again 52800 ns, stack 428 B
  %.3f     -> "3.142" first 102600 ns, again 85880 ns, stack 760 B
[newlib full]
  %d       -> "12345" first 14640 ns, again 14600 ns, stack 508 B
  3 x %d   -> "alt=12345 roll=-12345 yaw=1763" first 50600 ns, again 50560 ns, stack 524 B
  %.3f     -> "3.142" first 95360 ns, again 81200 ns, stack 744 B
[picolibc (double)]
  %d       -> "12345" first 24520 ns, again 24480 ns, stack 256 B
  3 x %d   -> "alt=12345 roll=-12345 yaw=1763" first 69320 ns, again 69320 ns, stack 272 B
  %.3f     -> "3.142" first 109800 ns, again 109760 ns, stack 436 B
nano   text=7444 data=108 bss=380
nanof  text=22680 data=472 bss=376
full   text=31196 data=1732 bss=420
pico   text=8432 data=12 bss=52
```

```svg
<svg viewBox="0 0 680 378" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">snprintf 한 번의 시간 (QEMU icount, 두 번째 호출)</text>
<text x="212" y="58" font-size="12" text-anchor="end">newlib-nano  %d</text>
<rect x="220.0" y="45" width="51.4" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="277.4" y="58" font-size="12">15.8 µs</text>
<text x="212" y="82" font-size="12" text-anchor="end">newlib-nano  3×%d</text>
<rect x="220.0" y="69" width="171.7" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="397.7" y="82" font-size="12">52.8 µs</text>
<text x="212" y="106" font-size="12" text-anchor="end">newlib-nano  %.3f</text>
<text x="226.0" y="106" font-size="12">출력 없음 (float 미지원)</text>
<text x="212" y="130" font-size="12" text-anchor="end">nano + _printf_float  %d</text>
<rect x="220.0" y="117" width="51.4" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="277.4" y="130" font-size="12">15.8 µs</text>
<text x="212" y="154" font-size="12" text-anchor="end">nano + _printf_float  3×%d</text>
<rect x="220.0" y="141" width="171.6" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="397.6" y="154" font-size="12">52.8 µs</text>
<text x="212" y="178" font-size="12" text-anchor="end">nano + _printf_float  %.3f</text>
<rect x="220.0" y="165" width="279.1" height="12" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="505.1" y="178" font-size="12">85.9 µs</text>
<text x="212" y="202" font-size="12" text-anchor="end">newlib full  %d</text>
<rect x="220.0" y="189" width="47.4" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="273.4" y="202" font-size="12">14.6 µs</text>
<text x="212" y="226" font-size="12" text-anchor="end">newlib full  3×%d</text>
<rect x="220.0" y="213" width="164.3" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="390.3" y="226" font-size="12">50.6 µs</text>
<text x="212" y="250" font-size="12" text-anchor="end">newlib full  %.3f</text>
<rect x="220.0" y="237" width="263.9" height="12" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="489.9" y="250" font-size="12">81.2 µs</text>
<text x="212" y="274" font-size="12" text-anchor="end">picolibc (double)  %d</text>
<rect x="220.0" y="261" width="79.6" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="305.6" y="274" font-size="12">24.5 µs</text>
<text x="212" y="298" font-size="12" text-anchor="end">picolibc (double)  3×%d</text>
<rect x="220.0" y="285" width="225.3" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="451.3" y="298" font-size="12">69.3 µs</text>
<text x="212" y="322" font-size="12" text-anchor="end">picolibc (double)  %.3f</text>
<rect x="220.0" y="309" width="356.7" height="12" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="582.7" y="322" font-size="12">109.8 µs</text>
<line x1="220" y1="40" x2="220" y2="332" stroke="currentColor"/>
<line x1="220" y1="332" x2="610" y2="332" stroke="currentColor"/>
<line x1="220.0" y1="332" x2="220.0" y2="337" stroke="currentColor"/>
<text x="220.0" y="351" font-size="12" text-anchor="middle">0</text>
<line x1="285.0" y1="332" x2="285.0" y2="337" stroke="currentColor"/>
<text x="285.0" y="351" font-size="12" text-anchor="middle">20</text>
<line x1="350.0" y1="332" x2="350.0" y2="337" stroke="currentColor"/>
<text x="350.0" y="351" font-size="12" text-anchor="middle">40</text>
<line x1="415.0" y1="332" x2="415.0" y2="337" stroke="currentColor"/>
<text x="415.0" y="351" font-size="12" text-anchor="middle">60</text>
<line x1="480.0" y1="332" x2="480.0" y2="337" stroke="currentColor"/>
<text x="480.0" y="351" font-size="12" text-anchor="middle">80</text>
<line x1="545.0" y1="332" x2="545.0" y2="337" stroke="currentColor"/>
<text x="545.0" y="351" font-size="12" text-anchor="middle">100</text>
<line x1="610.0" y1="332" x2="610.0" y2="337" stroke="currentColor"/>
<text x="610.0" y="351" font-size="12" text-anchor="middle">120</text>
<text x="415" y="368" font-size="12" text-anchor="middle">µs (1 µs = 25 사이클 @ 25 MHz 기준)</text>
</svg>
```

그림 8 — `snprintf` 한 번의 시간(두 번째 호출, UART 출력 없음). 정수 하나도 15 µs, `%f`는 80~110 µs다

출력에서 볼 것:

- **정수 하나 포맷에 15~25 µs, 스택 256~508 B.** 25 MHz Cortex-M3(QEMU icount 기준)에서 375~610 명령이다. 20 kHz FOC ISR(주기 50 µs)이라면 정수 printf 한 번이 주기의 30~50%다.
- **`%f`는 81~110 µs, 스택 436~760 B.** newlib-nano 기본은 `%f`를 아예 출력하지 않는다(빈 문자열 — B11의 `-u _printf_float` 함정).
- **newlib의 `%f`는 첫 호출이 더 느리다**(102.6 µs → 85.9 µs). 첫 호출 때 변환용 Bigint를 malloc하기 때문이다. 확인:

```sh
E=/var/tmp/rtos/F7/ex8/nanof/app.elf
arm-none-eabi-objdump -d $E | awk "/<_Balloc>:/,/^$/" | grep -E "bl"
arm-none-eabi-nm /var/tmp/rtos/F7/ex8/pico/app.elf | grep -ciE "malloc|calloc|balloc|sbrk"
```

```text
    30bc:	f7ff fb24 	bl	2708 <malloc>
    30d6:	f000 fe83 	bl	3de0 <_calloc_r>
    30f4:	f000 fe74 	bl	3de0 <_calloc_r>
    310a:	f7ff fe8d 	bl	2e28 <__assert_func>
0
```

`_dtoa_r` → `_Balloc` → `malloc`/`_calloc_r`(실패하면 `__assert_func`). **ISR에서 `printf("%f")`는 ISR에서 malloc을 하는 것**이다(예제 8의 크래시로 가는 길). picolibc 빌드에는 malloc 계열 심볼이 0개 — picolibc의 printf는 malloc을 쓰지 않는다. 대신 picolibc는 정수 포맷이 더 느렸다(이 측정에서 1.5배). "작고 malloc 없음 vs 빠름"의 교환이다.
- 스택 숫자는 ISR이라면 **MSP**에서 빠진다. 랩의 MSP는 4 KB(`_Main_Stack_Size`)지만 실제 제품은 1~2 KB인 경우가 많고, 중첩 IRQ 각각이 자기 printf를 하면 더해진다(F3).

---

## 7. 대안 — 지연 로깅 (deferred logging)

### 7.1 구조

원칙: **ISR은 기록만 하고, 포맷과 출력은 태스크 하나가 한다.** stdout(그리고 malloc, UART)의 주인을 로그 태스크 하나로 정하면 §6의 여섯 이유가 모두 사라진다. D2 §8.2의 게이트키퍼 태스크를 ISR까지 넓힌 것이다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="30" width="150" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="85.0" y="56.2" font-size="12" text-anchor="middle">ISR (IMU, UART)</text>
<rect x="10" y="150" width="150" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="85.0" y="176.2" font-size="12" text-anchor="middle">태스크들</text>
<rect x="200" y="24" width="190" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="295.0" y="48.2" font-size="12" text-anchor="middle" font-weight="bold">SPSC 링버퍼 (D8)</text>
<text x="295.0" y="64.2" font-size="12" text-anchor="middle">8 B 레코드 · 가득 차면 버림</text>
<rect x="200" y="144" width="190" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="295.0" y="168.2" font-size="12" text-anchor="middle" font-weight="bold">큐 (xQueueSend)</text>
<text x="295.0" y="184.2" font-size="12" text-anchor="middle">태스크 → 로그 태스크</text>
<rect x="430" y="84" width="110" height="56" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="485.0" y="108.2" font-size="12" text-anchor="middle" font-weight="bold">로그 태스크</text>
<text x="485.0" y="124.2" font-size="12" text-anchor="middle">낮은 우선순위</text>
<rect x="570" y="84" width="100" height="56" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="620.0" y="108.2" font-size="12" text-anchor="middle">printf</text>
<text x="620.0" y="124.2" font-size="12" text-anchor="middle">→ UART</text>
<line x1="160" y1="52" x2="198" y2="52" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<line x1="160" y1="172" x2="198" y2="172" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<line x1="390" y1="52" x2="428" y2="100" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<line x1="390" y1="172" x2="428" y2="124" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<line x1="540" y1="112" x2="568" y2="112" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<text x="340" y="225" font-size="12" text-anchor="middle">stdout을 만지는 코드는 로그 태스크 하나뿐 → FILE·malloc·UART에 레이스가 없다</text>
<text x="340" y="243" font-size="12" text-anchor="middle">ISR이 하는 일은 레코드 복사 + 인덱스 저장 + 알림뿐 (예제 14: 최대 5 µs)</text>
</svg>
```

그림 9 — 지연 로깅. ISR → SPSC 링(D8), 태스크 → 큐, 둘 다 로그 태스크가 비운다

ISR이 넣는 것은 **포맷된 문자열이 아니라 작은 레코드**다 — 메시지 ID, 인자 몇 개, (필요하면) 타임스탬프. 이 예제는 8 B 레코드(`id`, `v`)를 쓴다. 문자열 포맷은 로그 태스크에서, 더 나아가 **호스트에서** 할 수도 있다(딕셔너리 로깅 — K6).

### 7.2 예제 15 — 예제 12와 같은 부하를 지연 로깅으로

무엇을 확인하는 코드인지: 예제 12의 가장 나쁜 경우(200 µs IRQ, 태스크 2000줄)를 그대로 두고, ISR은 D8의 SPSC 링에 레코드만 넣고(acquire/release 인덱스, 가득 차면 버리고 센다), 태스크 쪽 로그는 큐로 보내고, stdout은 로그 태스크만 만진다. 같은 `check_log.py`로 검사한다.

```c
/* ex6 — deferred logging: ISR pushes a 8-byte record into an SPSC ring, one log task owns stdout */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
#define LINES 2000
#define RN 64u                                       /* ring slots, power of two */
typedef struct { uint16_t id; uint16_t pad; uint32_t v; } rec_t;
static rec_t ring[RN];
static uint32_t head, tail;                          /* head: ISR only writes, tail: log task only */
static volatile unsigned isr_n, dropped, isr_max_us, done;
static QueueHandle_t appq;
static TaskHandle_t hlog;
void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    uint32_t t0 = board_now_us();
    uint32_t h = head, t = __atomic_load_n(&tail, __ATOMIC_ACQUIRE);
    if (h - t == RN) dropped++;                      /* full: count, never wait */
    else { ring[h & (RN - 1)] = (rec_t){ .id = 1, .v = isr_n }; __atomic_store_n(&head, h + 1, __ATOMIC_RELEASE); }
    isr_n++;
    BaseType_t w = pdFALSE; vTaskNotifyGiveFromISR(hlog, &w);
    uint32_t dt = board_now_us() - t0; if (dt > isr_max_us) isr_max_us = dt;
    portYIELD_FROM_ISR(w);
}
static void app(void *arg)                           /* task-side logs go through a queue */
{
    (void)arg;
    TIMER_CTRL(TIMER0_BASE) = 1 | 8;
    for (unsigned i = 0; i < LINES; i++) xQueueSend(appq, &i, portMAX_DELAY);
    TIMER_CTRL(TIMER0_BASE) = 0; done = 1; xTaskNotifyGive(hlog);
    vTaskSuspend(NULL);
}
static void logger(void *arg)                        /* the only code that touches stdout */
{
    (void)arg; unsigned v;
    setvbuf(stdout, NULL, _IOLBF, 128);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, 1);
        int more;
        do {                                         /* ISR ring first, then ONE task record, repeat */
            uint32_t t = tail;
            while (t != __atomic_load_n(&head, __ATOMIC_ACQUIRE)) {
                printf("I:%05u\n", (unsigned)ring[t & (RN - 1)].v);
                __atomic_store_n(&tail, ++t, __ATOMIC_RELEASE);
            }
            more = xQueueReceive(appq, &v, 0) == pdPASS;
            if (more) printf("T:%05u ABCDEFGHIJKLMNOPQRSTUVWXYZ\n", v);
        } while (more);
        if (done && uxQueueMessagesWaiting(appq) == 0 && tail == head) break;
    }
    fflush(stdout);
    board_printf_raw("\n#SUMMARY DEFERRED task_lines=%d isr_calls=%u dropped=%u isr_max=%u us\n",
                     LINES, isr_n - dropped, dropped, isr_max_us);
    board_exit(0);
}
int main(void)
{
    appq = xQueueCreate(32, sizeof(unsigned));
    TIMER_RELOAD(TIMER0_BASE) = 4999; TIMER_VALUE(TIMER0_BASE) = 4999;   /* ~200 us */
    NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;
    xTaskCreate(logger, "log", 1024, NULL, 1, &hlog);
    xTaskCreate(app, "app", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
TMO=60 $R ex6 ex6 > /var/tmp/rtos/F7/ex6.out; python3 /Users/donh/.rtos_scratch/F7/check_log.py < /var/tmp/rtos/F7/ex6.out
```

```text
#SUMMARY DEFERRED task_lines=2000 isr_calls=1077 dropped=0 isr_max=5 us
  intact task lines 2000/2000, intact isr lines 1077/1077, broken lines 0
  bytes expected 78616, bytes received 78616 (diff +0)
```

```svg
<svg viewBox="0 0 680 180" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">로그 한 줄을 ISR에서 처리하는 데 걸린 최대 시간 (200 µs마다 IRQ)</text>
<text x="242" y="58" font-size="12" text-anchor="end">ISR printf, stdout 비버퍼</text>
<rect x="250.0" y="45" width="144.0" height="18" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="400.0" y="58" font-size="12">60 µs — 깨진 줄 4,506, 중복 +22 B</text>
<text x="242" y="88" font-size="12" text-anchor="end">ISR printf, stdout 줄 버퍼</text>
<rect x="250.0" y="75" width="108.0" height="18" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="364.0" y="88" font-size="12">45 µs — 깨진 줄 1,354, 중복 +758 B</text>
<text x="242" y="118" font-size="12" text-anchor="end">ISR → 링버퍼, 로그 태스크 printf</text>
<rect x="250.0" y="105" width="12.0" height="18" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="268.0" y="118" font-size="12">5 µs — 깨진 줄 0, 차이 0 B</text>
<line x1="250" y1="40" x2="250" y2="134" stroke="currentColor"/>
<line x1="250" y1="134" x2="610" y2="134" stroke="currentColor"/>
<line x1="250.0" y1="134" x2="250.0" y2="139" stroke="currentColor"/>
<text x="250.0" y="153" font-size="12" text-anchor="middle">0</text>
<line x1="310.0" y1="134" x2="310.0" y2="139" stroke="currentColor"/>
<text x="310.0" y="153" font-size="12" text-anchor="middle">25</text>
<line x1="370.0" y1="134" x2="370.0" y2="139" stroke="currentColor"/>
<text x="370.0" y="153" font-size="12" text-anchor="middle">50</text>
<line x1="430.0" y1="134" x2="430.0" y2="139" stroke="currentColor"/>
<text x="430.0" y="153" font-size="12" text-anchor="middle">75</text>
<line x1="490.0" y1="134" x2="490.0" y2="139" stroke="currentColor"/>
<text x="490.0" y="153" font-size="12" text-anchor="middle">100</text>
<line x1="550.0" y1="134" x2="550.0" y2="139" stroke="currentColor"/>
<text x="550.0" y="153" font-size="12" text-anchor="middle">125</text>
<line x1="610.0" y1="134" x2="610.0" y2="139" stroke="currentColor"/>
<text x="610.0" y="153" font-size="12" text-anchor="middle">150</text>
<text x="430" y="170" font-size="12" text-anchor="middle">ISR 안에서 보낸 최대 시간 µs (board_now_us, 1 µs 해상도)</text>
</svg>
```

그림 10 — 같은 로그 부하(200 µs IRQ, 태스크 2000줄)에서 ISR이 쓴 최대 시간과 출력 손상. 측정: 예제 12, 15

출력에서 볼 것: **깨진 줄 0, 바이트 차이 0, ISR 최대 5 µs**(printf 방식의 1/9~1/12, 그리고 board_now_us 두 번 읽는 비용 포함). 링 64칸이 넘친 적이 없다(`dropped=0`).

이 예제를 처음 짰을 때는 **`dropped=849`** 가 나왔다. 로그 태스크가 큐를 `while (xQueueReceive(…, 0) == pdPASS)`로 끝까지 비우는 동안, 큐에 자리가 날 때마다 우선순위가 높은 app 태스크가 바로 다시 채워서 그 루프가 끝나지 않았고, 그동안 ISR 링은 한 번도 비워지지 않았다. 지금 코드처럼 "ISR 링 전부 → 큐에서 **하나** → 반복"으로 바꾸고 0이 됐다. 지연 로깅의 설계 포인트가 여기 다 있다.

- **소비자의 순서와 우선순위**: 로그 태스크가 ISR 링을 충분히 자주 비우는가. 비우는 주기 × IRQ 빈도 < 링 크기.
- **가득 찼을 때의 정책**: ISR은 절대 기다리지 않는다 — 버리고 **센다**(`dropped`). 버린 개수를 다음 로그에 같이 찍어야 "로그가 조용한 것"과 "로그를 잃은 것"을 구별한다.
- **다중 생산자**: ISR이 여럿이면(우선순위가 다른 IRQ끼리 선점) SPSC가 아니다. IRQ마다 링을 따로 두거나, 넣는 부분만 짧은 크리티컬 섹션으로 감싼다(D8 §5, C7).

### 7.3 Zephyr의 지연 로깅

Zephyr의 `LOG_*` 매크로는 이 구조가 기본이다. `subsys/logging/Kconfig.mode`에서 모드 기본값이 `LOG_MODE_DEFERRED`다(`ARCH_POSIX`는 `LOG_MODE_IMMEDIATE`, `LOG_DEFAULT_MINIMAL`이면 `LOG_MODE_MINIMAL`). 지연 모드는 `MPSC_PBUF`(다중 생산자 패킷 버퍼)를 select하고, 로그 메시지(포맷 문자열 포인터 + 인자, `cbprintf` 패키지)를 버퍼(`CONFIG_LOG_BUFFER_SIZE`, 기본 1024 B)에 넣고, 로그 처리 스레드(`CONFIG_LOG_PROCESS_THREAD`, 기본 y)가 백엔드로 내보낸다. ISR에서 `LOG_INF()`를 불러도 되는 이유가 이것이다. `printk()`는 로깅을 켜면 `CONFIG_LOG_PRINTK`(기본 `y if PRINTK`, `Kconfig.processing` 8~10행)로 로깅 경로에 들어가지만 "그 자리에서 포맷"된다(도움말 원문: formatted in place). 로깅을 켜지 않은 빌드의 `printk()`는 콘솔로 바로 나가므로 ISR에서 길게 쓰면 E1의 문제가 그대로다. 자세한 것은 H7(서브시스템 투어), K6(로깅 인프라).

---

## 8. RTOS 커널 안에서는 무슨 일이 일어나나

C 런타임과 커널이 만나는 곳은 세 군데뿐이다 — **(1) 태스크별 상태 블록을 만들고 지우기, (2) 문맥 전환 때 "현재 블록" 포인터 바꾸기, (3) 공유 자원 락 함수 제공.** 앞의 둘은 커널이 하고, 셋째는 FreeRTOS에서는 **앱이**, Zephyr에서는 **커널의 libc 통합 코드가** 한다.

| 일 | FreeRTOS V11.3.2 | Zephyr v4.4.2 |
|---|---|---|
| 블록 타입 | `configTLS_BLOCK_TYPE` (`struct _reent` 또는 `void *`) — `tasks.c` 434행 TCB 멤버 `xTLSBlock` | `struct k_thread`의 `tls`(주소) — 블록 자체는 스레드 스택 꼭대기 |
| 블록 만들기 | `configINIT_TLS_BLOCK` — `prvInitialiseNewTask()` 안, `tasks.c` 1946행 | `arch_tls_stack_setup()` — `kernel/thread.c` 473행에서 호출, `arch/arm/core/tls.c` |
| 전환 때 바꾸기 | `configSET_TLS_BLOCK` — `vTaskSwitchContext()` `tasks.c` 5292행 (SMP는 5389행) | `swap_helper.S` 177~178행 `z_arm_tls_ptr` 저장 |
| 첫 태스크 | `vTaskStartScheduler()` `tasks.c` 3793행 | `arch/arm/core/cortex_m/thread.c` 592행 (main 스레드) |
| 블록 지우기 | `configDEINIT_TLS_BLOCK` — `prvDeleteTCB()` `tasks.c` 6578행 | 스택과 함께 사라짐 |
| errno 별도 장치 | `configUSE_POSIX_ERRNO` — `FreeRTOS_errno` 저장/복원 `tasks.c` 5262 · 5284행 | `CONFIG_ERRNO`: `k_thread.errno_var` / `CONFIG_ERRNO_IN_TLS` / `CONFIG_LIBC_ERRNO` (`include/zephyr/sys/errno_private.h`) |
| 스레드 포인터 읽기 | picolibc 자체 `__aeabi_read_tp` (`__tls`) | `arch/arm/core/cortex_m/__aeabi_read_tp.S` (`z_arm_tls_ptr`) |
| malloc · stdio 락 | **없음** — 앱이 `__malloc_lock` 또는 `__retarget_lock_*` 구현 (커널 안 예: AVR32 포트, heap_3의 suspend-all) | newlib: `lib/libc/newlib/libc-hooks.c`(정적 락 9개 + `__retarget_lock_*` → `k_mutex`/`k_sem`), picolibc: `lib/libc/picolibc/locks.c` |

전환 비용은 둘 다 **포인터 대입 1번**(newlib `_impure_ptr`, picolibc `__tls`, Zephyr `z_arm_tls_ptr`)이다. C2에서 본 PendSV 경로에 store 하나가 더해질 뿐이다. 비싼 것은 전환이 아니라 **블록의 크기**(그림 5)와 **락**(그림 4)이다.

FreeRTOS의 `configTLS_BLOCK_TYPE`/`configINIT_TLS_BLOCK`… 매크로 묶음은 사실 "컴파일러의 TLS"가 아니라 **"C 런타임이 가리키는 컨텍스트를 RTOS가 바꿔 주는 훅"** 이다. 그래서 newlib(구조체를 TCB에 통째로)과 picolibc(포인터만 TCB에, 데이터는 스택에)가 같은 훅으로 다르게 구현된다. 앱이 자기 태스크별 데이터를 원하면 이것 대신 `configNUM_THREAD_LOCAL_STORAGE_POINTERS` + `vTaskSetThreadLocalStoragePointer()`(앱용 포인터 배열)를 쓴다 — 이름이 비슷하지만 다른 기능이다(G 모듈).

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="220" y="10" width="240" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/>
<text x="340.0" y="32.2" font-size="12" text-anchor="middle">이 libc 함수를 여기서 불러도 되나?</text>
<rect x="240" y="70" width="200" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="340.0" y="92.2" font-size="12" text-anchor="middle">호출 문맥이 ISR인가?</text>
<line x1="340" y1="46" x2="340" y2="68" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="10" y="140" width="290" height="70" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/>
<text x="155.0" y="163.2" font-size="12" text-anchor="middle" font-weight="bold">예 → 순수 함수만</text>
<text x="155.0" y="179.2" font-size="12" text-anchor="middle">memcpy · memset · strlen · strtok_r</text>
<text x="155.0" y="195.2" font-size="12" text-anchor="middle">printf · malloc · %f 포맷 · errno 금지</text>
<line x1="300" y1="106" x2="200" y2="138" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="380" y="140" width="290" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/>
<text x="525.0" y="162.2" font-size="12" text-anchor="middle">아니오(태스크) → 숨은 상태가 있나?</text>
<text x="525.0" y="178.2" font-size="12" text-anchor="middle">(§1 목록: _impure_ptr · TLS 변수)</text>
<line x1="380" y1="106" x2="500" y2="138" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="380" y="224" width="290" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/>
<text x="525.0" y="246.2" font-size="12" text-anchor="middle">없다 → 그냥 써도 된다</text>
<text x="525.0" y="262.2" font-size="12" text-anchor="middle">(단, 스택 비용 확인 — §6.4)</text>
<rect x="10" y="224" width="340" height="70" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="180.0" y="247.2" font-size="12" text-anchor="middle" font-weight="bold">있다 → 셋 중 하나</text>
<text x="180.0" y="263.2" font-size="12" text-anchor="middle">_r 버전 (strtok_r · asctime_r · rand_r)</text>
<text x="180.0" y="279.2" font-size="12" text-anchor="middle">또는 태스크별 _reent / picolibc TLS 켜기</text>
<line x1="525" y1="192" x2="525" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<line x1="440" y1="192" x2="300" y2="222" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="10" y="306" width="660" height="60" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/>
<text x="340.0" y="332.2" font-size="12" text-anchor="middle">malloc·stdio처럼 "공유 자원" 자체가 있는 것 → 락 (__malloc_lock / __retarget_lock_*)</text>
<text x="340.0" y="348.2" font-size="12" text-anchor="middle">+ 가능하면 한 태스크가 주인 (로그 태스크, 정적 풀 F2)</text>
<line x1="180" y1="294" x2="180" y2="304" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
</svg>
```

그림 11 — "이 libc 함수를 여기서 불러도 되나?" 결정 흐름

---

## 9. 실무에서 다시 보기

### 9.1 드론 비행 제어기 — IMU ISR에서 로그를 남기고 싶다

1 kHz rate loop를 돌리는 FC에서 "자이로 샘플이 가끔 튄다"를 추적하려고 IMU data-ready ISR에 `printf("gyro %f\n", g)`를 넣는 순간 문제가 셋 생긴다: ISR이 80~110 µs 길어져(예제 14) 다른 IRQ(ESC 텔레메트리, DShot 타이머)가 밀리고, `%f`가 ISR 안에서 malloc을 하고(예제 14), 태스크의 텔레메트리 printf와 FILE을 공유해 로그가 중복 · 손실된다(예제 12). 그러면 디버깅하려던 현상보다 로그가 만든 현상이 더 커진다.

정석은 예제 15다. ISR은 `{id=GYRO_SPIKE, t=timestamp, raw=int16 x3}` 같은 12~16 B 레코드를 SPSC 링에 넣고, 낮은 우선순위 로그 태스크가 포맷해 UART(또는 SD 카드, MAVLink)로 내보낸다. 값은 **정수 그대로**(raw 카운트, 밀리단위 고정소수점) 넣고 float 변환은 로그 태스크나 호스트에서 한다. 링이 넘치면 `dropped` 카운터를 올린다. 비행 로그에 "dropped=37"이 찍혀 있으면 그 구간의 공백을 해석할 수 있다.

### 9.2 로봇 관절 모터 컨트롤러 — 파서 태스크 둘

CAN 명령 파서와 UART 디버그 셸 파서가 둘 다 `strtok`과 `strtol`을 쓰는 코드가 흔하다(예제 3 · 4). 셸에서 `set kp 1.2`를 치는 순간 CAN 파서가 엉뚱한 토큰을 받거나, `strtol` 오버플로 검사가 셸 쪽의 `errno = 0` 때문에 빠져 **범위 밖 setpoint가 그대로 들어간다**. 고치는 순서: ① `strtok_r` · 지역 `save`로 바꾸고, ② 숫자 파싱은 `errno`를 보지 않는 자체 파서(범위 검사 포함)로 바꾸고, ③ 그래도 libc errno를 쓰는 코드가 남으면 `configUSE_NEWLIB_REENTRANT=1`(태스크당 76 B + 지연 할당) 또는 picolibc TLS를 켠다.

### 9.3 Linux SoC 옆의 센서 허브 (Zephyr)

Zephyr에서는 errno가 이미 스레드별이고(예제 11), picolibc 락이 `k_mutex`로 들어가 있고, `LOG_*`는 지연 모드가 기본이다. 남는 함정은 **ISR에서 `printk`**, 그리고 **TLS 크기**다. picolibc TLS 변수가 많이 링크되면(예: `localtime`, `ecvt`) 스레드마다 그만큼 스택이 줄어든다. 스레드 스택을 빡빡하게 잡았다면 `CONFIG_THREAD_ANALYZER`로 다시 본다(F3).

### 9.4 Don의 SSD 컨트롤러 펌웨어와 연결

| SSD 펌웨어에서 하던 것 | 이 노트의 말로 |
|---|---|
| hot path(I/O 명령 처리)에서 libc 호출을 쓰지 않음, 자체 `memcpy`/정수 포맷터 | 숨은 상태 · malloc · 긴 실행 시간을 피하는 가장 확실한 방법 (§1, §6.4) |
| 이벤트 로그를 고정 크기 바이너리 레코드로 DRAM 링에 남기고 호스트 툴이 해석 | 지연 로깅 + 딕셔너리 로깅 (§7, K6) |
| 런타임 동적 할당 금지, 부팅 때 풀 고정 | malloc 락 문제를 원천 제거 (F1, F2) |
| 코어마다 따로 링크된 펌웨어 이미지 | libc 전역이 코어마다 따로 — RTOS 한 이미지 안의 태스크들은 libc 전역을 **공유**한다는 점이 다르다 |
| 디버그 UART 출력은 idle 루프가 링을 비우며 | 로그 태스크를 가장 낮은 우선순위로 (예제 15의 `dropped` 교훈) |

### 9.5 C++를 쓰는 펌웨어라면

C++의 `new`는 기본적으로 `malloc`으로 간다 — §3의 락 문제가 그대로다. 함수 안 `static` 지역 객체의 첫 초기화는 GCC가 `__cxa_guard_acquire`로 보호하는데, `-fno-threadsafe-statics`로 빌드하면 그 보호가 없어져 두 태스크가 동시에 첫 호출을 하면 생성자가 두 번 돈다. 예외 처리(`-fexceptions`)도 런타임 malloc을 쓴다. C++ 런타임 쪽은 이 노트에서 실험하지 않았다 — Hark J4 노트(C++/Rust)와 G 모듈에서 이어 본다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 두 태스크가 `strtok` | 가끔 다른 태스크 문자열의 토큰이 나온다, 크래시는 없음 | `_reent` 하나의 "다음 위치"를 공유 (예제 3) | `strtok_r` + 지역 `save`. 코딩 규칙으로 금지 |
| `errno`로 에러 검사, 태스크별 errno 없음 | 오버플로 · 에러가 가끔 "정상"으로 보임 | 다른 태스크의 `errno = 0`이 덮음 (예제 4) | `configUSE_NEWLIB_REENTRANT=1` 또는 picolibc TLS, 또는 반환값으로 에러를 주는 함수 |
| `configUSE_POSIX_ERRNO=1`로 해결했다고 믿음 | 여전히 같은 증상 | 그 옵션은 별도 변수 `FreeRTOS_errno`용 (예제 4) | 위와 같음 |
| newlib malloc을 여러 태스크가, `__malloc_lock` 미구현 | 몇 시간 뒤 `_malloc_r`/`_free_r` 안 HardFault (CFSR PRECISERR), 테스트는 대부분 통과 | 빈 락 함수 (예제 7: 6번 중 4번) | `__malloc_lock`을 suspend-all 또는 재귀 뮤텍스로 (예제 9). 또는 heap_3 경유만 허용 |
| ISR에서 malloc · `strtok` · `rand` · `%f` printf | ISR 문맥 HardFault, 또는 락 assert | ISR 안 malloc (예제 8, 예제 14의 `_Balloc`) | ISR은 정적 버퍼 · 고정 블록 풀(F2) · 링버퍼만 |
| ISR에서 printf | 로그가 섞이고 **같은 줄이 두 번** 찍힘, ISR이 40~60 µs | 락 없는 FILE 공유 (예제 12) | 지연 로깅 (예제 15), Zephyr는 `LOG_*` |
| printf를 뮤텍스로 감싸고 ISR에서도 부름 | assert, 또는 assert를 끄면 멈춤 | ISR에서 블로킹 take (예제 13) | 같은 로거를 ISR에서 부르지 않는다. ISR은 링으로 |
| picolibc + 자체 시작 코드에서 `_set_tls` 누락 | errno 쓰기가 주소 `0x8` 근처(벡터 테이블)를 덮음, 증상은 엉뚱한 곳 | `__tls` = 0 (예제 10) | 시작 코드에서 `_init_tls` + `_set_tls`, 링커 스크립트에 `.tdata`/`.tbss` 심볼 |
| `configUSE_NEWLIB_REENTRANT=1` 켜고 TCB 크기 계산을 그대로 | 정적 할당 배열 부족, RAM 예산 초과 | TCB +76 B(nano) / +288 B(full), 지연 할당 +88~120 B (예제 5) | `sizeof(StaticTask_t)` 다시 보기, 지연 할당은 newlib 힙(`_sbrk`)에서 나간다는 것도 예산에 |
| `nano.specs`를 링크에만 주고 컴파일에는 안 줌 | `_reent`를 쓰는 코드가 이상한 필드를 고침 | 헤더(full)와 라이브러리(small) 구조체 배치 불일치 (§2.5) | 컴파일 · 링크 양쪽에 같은 specs |

---

## 11. 면접에서 이렇게 말한다

**Q.** What is the difference between a reentrant function and a thread-safe function?

**A.** 재진입 함수는 공유 가변 상태가 없어서 실행 도중에 같은 함수가 다시 불려도(인터럽트, 선점) 둘 다 맞다. 스레드 안전은 동시에 불려도 맞다는 것인데 락으로 얻을 수도 있다. 그래서 락으로 만든 스레드 안전 함수는 ISR에서 데드락이 난다. `strtok`은 둘 다 아니고, `strtok_r`은 둘 다이고, 락을 단 malloc은 스레드 안전이지만 재진입이 아니다.

> A reentrant function has no shared mutable state, so it stays correct even if it's interrupted and called again in the middle. Thread-safe only means concurrent calls from threads are correct, and that can be achieved with a lock. The distinction matters on an MCU: a lock-based thread-safe function deadlocks or asserts when called from an ISR, because the ISR can't wait for the task it interrupted. strtok is neither, strtok_r is both, and malloc with a lock hook is thread-safe but not reentrant.

**꼬리 질문**: "Is snprintf reentrant?" → newlib-nano에서 정수 포맷은 malloc을 안 부르지만 `%f`는 `_Balloc`으로 malloc한다. 그리고 재진입이어도 15 µs, 400 B 스택이라 ISR에서는 안 쓴다.

**Q.** Why shouldn't you call printf from an interrupt handler?

**A.** 여섯 가지다. stdout FILE이 공유 상태인데 우리 툴체인의 newlib은 FILE 락이 아예 컴파일되어 있지 않아서 출력이 섞이고 같은 줄이 두 번 나온다(실측). 락이 있으면 ISR이 락을 기다려 데드락. 첫 출력 버퍼와 `%f` 변환이 malloc을 부른다. 스택을 400~760 B 쓴다. UART 없이 포맷만 15~110 µs. 그리고 errno를 건드리면 끊긴 태스크의 errno가 바뀐다. 대신 ISR은 작은 레코드를 링버퍼에 넣고 로그 태스크가 포맷한다 — 그러면 ISR이 5 µs로 줄고 손상이 0이 됐다.

> Besides the blocking UART, printf touches shared libc state. stdout's FILE buffer is global; with newlib built without retargetable locking there is no FILE lock at all, and when I had an ISR printf while a task printed, lines were interleaved and some were emitted twice. If there is a lock, the ISR would wait on it and deadlock. printf can also call malloc, for the first buffer and for %f conversions, it uses 400 to 760 bytes of stack on the main stack, and formatting alone took 15 to 110 microseconds on a Cortex-M3 model. My fix is deferred logging: the ISR pushes a small binary record into a single-producer ring buffer, and one low-priority log task owns stdout. ISR time dropped to about 5 microseconds and the output was byte-exact.

**Q.** How does errno work when you have multiple tasks?

**A.** libc에 따라 다르다. newlib은 errno가 `_impure_ptr->_errno`라서 FreeRTOS에서 `configUSE_NEWLIB_REENTRANT=1`을 켜야 TCB마다 `struct _reent`가 생기고 전환 때 `_impure_ptr`가 바뀐다. picolibc는 errno가 `__thread` 변수라 RTOS가 TLS 블록을 바꿔 줘야 한다. Zephyr는 기본으로 스레드별이다. 주의할 것은 FreeRTOS의 `configUSE_POSIX_ERRNO`는 libc errno가 아니라 별도 변수라는 점, 그리고 ISR은 끊긴 태스크의 errno를 쓴다는 점이다.

> It depends on the C library. In newlib, errno expands to the errno field of the current struct _reent, found through _impure_ptr, so FreeRTOS has to give each task its own _reent and swap _impure_ptr on every context switch, which is configUSE_NEWLIB_REENTRANT. In picolibc errno is a thread-local variable, so the RTOS swaps a TLS block instead; on Cortex-M there's no thread pointer register, so that's a global variable read by __aeabi_read_tp. Zephyr gives every thread its own errno by default. Two traps: configUSE_POSIX_ERRNO is a separate FreeRTOS_errno variable, not libc's errno, and an ISR always sees the errno of whatever task it interrupted.

**Q.** Is malloc thread-safe on FreeRTOS with newlib?

**A.** 기본으로는 아니다. newlib은 `__malloc_lock`/`__malloc_unlock` 훅을 부르는데 기본 구현이 빈 함수다. 두 태스크가 malloc을 쓰는 실험에서 6번 중 4번 `_malloc_r` 안에서 HardFault가 났고 2번은 통과했다 — 테스트로는 못 잡는 레이스다. `vTaskSuspendAll`로 훅을 구현하면 고쳐지고 쌍당 2.2 µs 정도 든다. ISR에서 malloc하는 것은 어떤 태스크 락으로도 못 고친다.

> Not by default. newlib calls __malloc_lock and __malloc_unlock, and the default implementations are empty. In a two-task stress test with a timer-driven preemption, the heap's free list got corrupted and it HardFaulted inside _malloc_r in four of six runs; two runs passed, which is exactly why this bug escapes testing. Implementing the hooks with vTaskSuspendAll fixed it for about two microseconds per malloc-free pair; a recursive mutex also works but cost three times more. If the toolchain's newlib has retargetable locking, you implement the __retarget_lock functions instead, which also covers stdio. And nothing task-level makes malloc safe in an ISR.

**꼬리 질문**: "Why recursive?" → newlib 내부 경로(realloc, `_reclaim_reent`, stdio)가 락을 쥔 채 다시 malloc/free를 부를 수 있다.

**Q.** What does configUSE_NEWLIB_REENTRANT cost?

**A.** 태스크마다 `struct _reent`가 TCB에 들어간다. newlib-nano에서 76 B, full newlib에서 288 B를 재 봤다. nano는 `strtok` · `rand` 같은 상태를 처음 쓸 때 malloc해서 태스크당 88 B, 32 B가 더 나간다. 전환 비용은 포인터 대입 하나다. 그리고 malloc 락은 여전히 따로 구현해야 한다.

> Each TCB embeds a struct _reent: I measured 76 bytes with newlib-nano and 288 with full newlib. Nano allocates parts lazily, so the first strtok in each task costs another 88 bytes of heap and the first rand 32. The switch cost is a single pointer store. It does not provide malloc locking; that's still up to you.

**Q.** How would you log from an interrupt?

**A.** ISR은 포맷하지 않는다. 메시지 ID와 정수 인자, 타임스탬프를 SPSC 링버퍼에 넣고 로그 태스크를 깨운다. 링이 차면 기다리지 않고 버리고 개수를 센다. 로그 태스크가 유일한 stdout 주인이다. Zephyr의 deferred logging이 이 구조다. 더 줄이려면 포맷 문자열도 호스트로 보내는 딕셔너리 로깅을 쓴다.

> The ISR never formats. It writes a small record, a message ID, integer arguments and a timestamp, into a single-producer ring buffer and notifies a low-priority log task, which is the only code that touches stdout. If the ring is full the ISR drops the record and increments a counter so gaps are visible. That's essentially Zephyr's deferred logging mode. One lesson from my test: the consumer must drain the ISR ring between task messages, otherwise a busy producer task starves it and records get dropped.

**Q.** How does thread-local storage work on a Cortex-M?

**A.** 컴파일러는 TLS 변수를 "스레드 포인터 + 상수 오프셋"으로 접근하고, Arm EABI는 스레드 포인터를 `__aeabi_read_tp()`로 읽는다. Cortex-A/R은 `TPIDRURO` 레지스터가 있지만 M에는 없어서 전역 변수 하나(picolibc `__tls`, Zephyr `z_arm_tls_ptr`)를 쓰고, RTOS가 전환 때 그 변수를 바꾼다. 블록은 스레드 스택 꼭대기에서 떼어 낸다. 시작 코드에서 main용 포인터를 안 잡으면 errno가 주소 0 근처에 써진다.

> The compiler accesses a thread-local variable as thread pointer plus a link-time offset, and on Arm the thread pointer comes from __aeabi_read_tp. Cortex-A and R have the TPIDRURO register; Cortex-M has none, so picolibc and Zephyr keep the thread pointer in a global variable and the kernel rewrites it on every context switch. Each thread's TLS block is carved from the top of its stack. If your startup code forgets to set the pointer for main, errno writes land near address zero, in my test on the NMI vector.

**Q.** Two tasks call strtok on different strings. What happens?

**A.** newlib에서 `strtok`의 "다음 위치"는 `_reent` 안에 있다. 태스크별 `_reent`가 없으면 공유되어 한 태스크가 다른 태스크 문자열의 토큰을 받는다 — 실험에서 400번 중 200번. 흥미로운 점은 `configUSE_NEWLIB_REENTRANT=1`을 켜면 newlib의 `strtok`도 태스크별로 안전해진다는 것이다. 그래도 답은 `strtok_r`이다 — ISR이나 다른 libc에서는 그 보호가 없다.

> The saved position lives in the C library's per-context state. Without per-task state, the tasks share it and one gets tokens from the other's string; in my test that happened in 200 of 400 iterations with no crash at all, just wrong data. With FreeRTOS's newlib reentrancy enabled the problem disappears because the position moves into each task's _reent, but the right answer is still strtok_r with a local save pointer: it doesn't depend on RTOS configuration and it's safe even from an ISR.

---

## 12. 직접 해보기

1. **개념**: `asctime_r`은 재진입인가, ISR 안전인가? `localtime_r`은?
   정답/힌트: 둘 다 상태를 호출자 버퍼에 두므로 재진입이다. 하지만 `localtime_r`은 시간대(tz) 정보를 읽느라 `__tz_lock`을 부를 수 있다 — 락을 부르는 함수는 ISR 안전이 아니다. `nm -A libc_nano.a | grep tz_lock`으로 누가 부르는지 확인하라.
2. **코드**: 예제 3에 `rand()`를 쓰는 세 번째 태스크를 추가하고, 두 태스크가 같은 시드로 `srand(1)`했을 때 각자 같은 수열을 받는지 `configUSE_NEWLIB_REENTRANT` 0/1로 비교하라.
   정답/힌트: 0이면 수열을 나눠 갖고(서로 다른 값), 1이면 태스크마다 같은 수열을 받는다. 각 태스크 첫 `rand`에 malloc 32 B(예제 5).
3. **코드**: retargetable locking이 켜진 newlib을 쓴다고 가정하고, `__retarget_lock_*` 10개와 정적 락 객체 9개를 FreeRTOS 재귀 뮤텍스로 구현하라. 스케줄러 시작 전 호출과 ISR 호출을 어떻게 처리할지 정하라.
   정답/힌트: Zephyr `lib/libc/newlib/libc-hooks.c`를 본뜬다. 스케줄러 전에는 `xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED`면 그냥 통과, ISR(`xPortIsInsideInterrupt()`)이면 `configASSERT`로 막는다. 정적 락은 `struct __lock`에 `StaticSemaphore_t`를 넣어 첫 사용 때 `xSemaphoreCreateRecursiveMutexStatic`.
4. **측정**: 예제 7의 `LOCK=1`에서, H 태스크의 깨어남 지연(IRQ → H 실행 시작)을 재서 락 없을 때와 비교하라. suspend-all 락이 높은 우선순위 태스크의 지연을 malloc 시간만큼 늘리는 것을 숫자로 보여라.
   정답/힌트: ISR에서 `board_now_us()`를 전역에 저장, H가 깨자마자 차이를 히스토그램으로. 최대값이 L의 malloc 최악 시간(단편화된 nano free list 탐색)만큼 늘어난다.
5. **측정**: 예제 14에 picolibc `--printf=i`(정수 전용)와 newlib-nano `siprintf`를 추가해 정수 포맷 시간 · 스택을 비교하라.
   정답/힌트: B11 §3.3의 printf 수준. 정수 전용 판은 시간과 스택이 모두 줄어야 한다.
6. **코드**: 예제 15를 "IRQ 두 개(우선순위 다름)가 같은 링에 넣는" 구조로 바꾸면 무엇이 깨지는지 재현하고, IRQ별 링 두 개로 고쳐라.
   정답/힌트: 높은 IRQ가 낮은 IRQ의 `head` 읽기와 쓰기 사이에 들어오면 같은 칸에 두 번 쓰고 하나를 잃는다. SPSC는 "생산자 하나"일 때만 락이 필요 없다(D8).
7. **개념**: Zephyr에서 `CONFIG_ERRNO_IN_TLS`와 `CONFIG_LIBC_ERRNO`가 둘 다 있는 이유는?
   정답/힌트: 전자는 Zephyr 자신의 `z_errno_var`를 TLS에, 후자는 libc(picolibc)의 errno를 그대로 쓴다. `LIBC_ERRNO`가 켜지면 `ERRNO_IN_TLS`는 꺼진다(`depends on !LIBC_ERRNO`). 예제 11의 SDK 빌드가 이 경우다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| reentrant | 재진입 가능 | 실행 중 다시 불려도 맞는 함수. 공유 가변 상태가 없다 |
| thread-safe | 스레드 안전 | 동시 호출이 맞는 함수. 락으로 얻을 수 있다 |
| async-signal-safe | 비동기 시그널 안전 | 시그널 핸들러 · ISR에서 불러도 되는 함수. POSIX가 목록을 정한다 |
| hidden state | 숨은 상태 | 함수 안 `static`이나 libc 전역에 있는, 호출 간에 이어지는 상태 |
| `struct _reent` | newlib 재진입 구조체 | errno · strtok 위치 · rand 상태 등 newlib의 숨은 상태 묶음 |
| `_impure_ptr` | 현재 `_reent` 포인터 | 모든 newlib 함수가 상태를 찾는 전역 포인터. RTOS가 전환 때 바꾼다 |
| `_REENT_SMALL` | 작은 `_reent` | newlib-nano 구성. 상태 일부를 포인터로 두고 처음 쓸 때 malloc |
| `_REENT_CHECK` | 지연 할당 매크로 | `_reent` 필드가 NULL이면 malloc하고 실패하면 assert |
| `__malloc_lock` | malloc 락 훅 | newlib이 힙을 고치기 전후에 부르는 함수. 기본은 빈 함수 |
| retargetable locking | 바꿔 끼우는 락 | `__retarget_lock_*` 10개 함수로 newlib 전체(malloc · stdio …)의 락을 앱이 제공하는 구성 |
| `configUSE_C_RUNTIME_TLS_SUPPORT` | FreeRTOS C 런타임 TLS | TCB에 C 런타임 블록을 두고 전환 때 바꾸는 일반 장치(V10.5.0~) |
| `configUSE_POSIX_ERRNO` | FreeRTOS POSIX errno | 별도 전역 `FreeRTOS_errno`를 태스크별로 저장 · 복원. libc errno와 무관 |
| TLS | thread-local storage | 스레드마다 따로 있는 전역 변수. `__thread` / `_Thread_local` |
| `.tdata` / `.tbss` | TLS 섹션 | 초기값 있는 TLS / 0으로 시작하는 TLS의 템플릿 |
| thread pointer (TP) | 스레드 포인터 | 현재 스레드 TLS 블록을 가리키는 값. Cortex-M은 레지스터가 없어 전역 변수 |
| `__aeabi_read_tp` | TP 읽기 함수 | Arm EABI가 정한 "스레드 포인터를 r0에 돌려주는" 함수 |
| local-exec | TLS 모델 | 모든 TLS가 실행 파일 안에 있다고 보고 TP + 상수로 접근하는 가장 단순한 모델 |
| `_set_tls` / `_init_tls` | picolibc TLS API | TP 설정 / 블록 초기화(`.tdata` 복사, `.tbss` 0) |
| `z_arm_tls_ptr` | Zephyr Cortex-M TP | Zephyr가 전환 때 쓰는 전역 스레드 포인터 |
| deferred logging | 지연 로깅 | 기록은 즉시(링버퍼), 포맷 · 출력은 나중에 로그 태스크가 |
| `_Balloc` | newlib Bigint 할당 | `%f` 변환(`_dtoa_r`)이 쓰는 큰 정수 할당 — 안에서 malloc |

---

## 14. 요약 & 체크리스트

C 라이브러리는 `strtok` 위치, `rand` 상태, `errno`, `stdout` 버퍼, malloc 힙 같은 **숨은 전역 상태**를 갖고 있고, RTOS 태스크와 ISR이 그것을 공유하면 소스 코드에 보이지 않는 레이스가 된다. 호출 간 상태는 `_r` 함수나 태스크별 사본으로 푼다 — newlib은 `struct _reent`(nano 76 B, full 288 B, 지연 할당 추가)를 FreeRTOS가 `_impure_ptr`로 바꿔 끼우고(`configUSE_NEWLIB_REENTRANT` → `configUSE_C_RUNTIME_TLS_SUPPORT`), picolibc는 `__thread` 변수를 소프트웨어 스레드 포인터로 바꿔 끼운다(Cortex-M에는 TP 레지스터가 없다). 진짜 공유 자원(malloc 힙, stdio)은 락이 필요하다 — newlib의 `__malloc_lock`은 기본이 빈 함수라서 두 태스크가 malloc을 쓰면 힙이 깨지고(6번 중 4번 HardFault), Ubuntu newlib은 stdio 락이 아예 없다. ISR은 어느 쪽 해법도 못 쓴다 — 태스크별 사본은 끊긴 태스크의 것이고, 락은 기다릴 수 없다. 그래서 ISR은 기록만 하고(링버퍼 5 µs), 포맷과 출력은 로그 태스크 하나가 맡는다(Zephyr `LOG_MODE_DEFERRED`).

- [ ] 재진입 · 스레드 안전 · ISR 안전을 예 하나씩으로 구분할 수 있다 (예제 1, 그림 2)
- [ ] 내 툴체인 libc에서 숨은 상태를 가진 함수를 `nm`으로 뽑을 수 있다 (예제 2)
- [ ] newlib `errno`가 `_impure_ptr->_errno`이고 FreeRTOS가 어디서 `_impure_ptr`를 바꾸는지 `tasks.c` 줄로 말할 수 있다 (§2.4)
- [ ] `configUSE_POSIX_ERRNO`가 libc errno를 고치지 않는다는 것을 실험으로 보였다 (예제 4)
- [ ] 태스크당 `_reent` 비용(76 / 288 B + 지연 할당)을 말할 수 있다 (예제 5)
- [ ] 내 newlib이 retargetable locking인지 확인하고, 어떤 락 훅을 덮을 수 있는지 안다 (§3.1, 예제 6)
- [ ] `__malloc_lock`을 suspend-all과 재귀 뮤텍스로 구현하고 비용 차이를 안다 (예제 7, 9)
- [ ] ISR malloc은 어떤 태스크 락으로도 안 된다는 것을 설명할 수 있다 (예제 8)
- [ ] picolibc TLS 블록이 스택 꼭대기에 있고, 시작 코드가 `_set_tls`를 해야 한다는 것을 안다 (예제 10)
- [ ] Zephyr에서 errno가 어디 사는지 툴체인별로 말할 수 있다 (예제 11)
- [ ] ISR printf가 깨뜨리는 것 여섯 가지와 숫자(38~60 µs, 중복 바이트)를 말할 수 있다 (예제 12~14)
- [ ] ISR → 링 → 로그 태스크 구조를 짜고, 소비 순서 · dropped 정책을 설명할 수 있다 (예제 15)

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM F5 | C 런타임 · libc (재진입 부분) | §1(세 가지 안전, 예제 1 — main + ISR 정적 버퍼, 예제 2 — 숨은 상태 목록), §2.1(`_reent` 구조) | super loop 펌웨어에서 main과 ISR이 함께 쓰는 정적 버퍼 · 전역의 코딩 규칙, `_impure_ptr`가 하나뿐인 구성에서의 errno 규칙 |
| BM F5 | C 런타임 · libc (malloc 락 · errno 부분) | §3(락 체계, 예제 6~9), §3.7(RTOS 없는 `__malloc_lock`), §4(errno 구현 비교) | PRIMASK 저장/복원 + 중첩 카운터로 만든 `__malloc_lock` 실제 코드, `_sbrk`와 MSP 충돌 감지 |
| BM F5 | C 런타임 · libc (printf · stdio 부분) | §6(ISR printf, 예제 12~14), §7(지연 로깅, 예제 15) | RTOS 없이 main 루프가 링을 비우는 로그 구조, semihosting · SWO printf를 ISR에서 쓸 때의 차이 (libc 선택 · 크기 · syscall stub은 RTOS B11이 주인) |

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스: `include/FreeRTOS.h`(120~170행 `configUSE_NEWLIB_REENTRANT` · `configUSE_PICOLIBC_TLS` · `configUSE_C_RUNTIME_TLS_SUPPORT`, 2827행 `configUSE_POSIX_ERRNO`), `include/newlib-freertos.h`, `include/picolibc-freertos.h`, `tasks.c`(434 · 501 · 1946 · 3793 · 5262 · 5284 · 5292 · 6578행), `portable/MemMang/heap_3.c`, `portable/GCC/AVR32_UC3/port.c`(`__malloc_lock`), `History.txt`(V10.1.0, V10.5.0, V10.6.0, V11.0.0 항목)
- Zephyr v4.4.2 소스: `lib/libc/Kconfig`, `lib/libc/picolibc/Kconfig`, `lib/libc/picolibc/locks.c`, `lib/libc/picolibc/errno_wrap.c`, `lib/libc/newlib/libc-hooks.c`, `kernel/Kconfig`(`ERRNO`, `ERRNO_IN_TLS`, `LIBC_ERRNO`), `include/zephyr/sys/errno_private.h`, `kernel/include/kernel_tls.h`, `arch/arm/core/tls.c`, `arch/arm/core/cortex_m/__aeabi_read_tp.S`, `arch/arm/core/cortex_m/swap_helper.S`, `subsys/logging/Kconfig.mode`, `subsys/logging/Kconfig.processing`
- newlib 4.6.0 헤더(Ubuntu `libnewlib-dev 4.6.0.20260123-1`): `sys/reent.h`(`struct _reent`, `_REENT_CHECK`), `sys/lock.h`, `sys/errno.h`, `newlib.h`, `nano/newlib.h`; 라이브러리 `libc_nano.a`, `libc.a`(`mlock.o`, `findfp.o`, `strtok.o`, `nano-mallocr.c`)
- picolibc 1.8.10(Ubuntu `picolibc-arm-none-eabi`): `picotls.h`, `picolibc.specs`, `picolibc.ld`(TLS 심볼), `libc.a`(`_set_tls`, `__aeabi_read_tp`, `misc/lock.c`) — TLS 문서: https://github.com/picolibc/picolibc/blob/main/doc/tls.md (FreeRTOS `picolibc-freertos.h` 주석이 가리키는 문서)
- Dave Nadler, "newlib and FreeRTOS" — http://www.nadler.com/embedded/newlibAndFreeRTOS.html (FreeRTOS `newlib-freertos.h` 주석이 가리키는 글)
- Zephyr 문서 Logging — https://docs.zephyrproject.org/latest/services/logging/index.html
- The Open Group, POSIX.1 (IEEE Std 1003.1) XSH 2.4.3 "Signal Actions" — async-signal-safe 함수 목록
- Arm, "Run-time ABI for the Arm Architecture"(RTABI) — `__aeabi_read_tp` 정의 (Zephyr `__aeabi_read_tp.S` 주석이 인용하는 5.3.5절)
- 관련 노트: B11(libc 선택 · 크기 · syscall stub · `-u _printf_float` · 스택 칠하기), C2(문맥 전환), C7(크리티컬 섹션), C9(idle 태스크 — `_reclaim_reent`), D2(뮤텍스 · 게이트키퍼), D8(SPSC 링버퍼), E1(ISR 규칙 — UART printf 2 ms), F1(힙), F2(풀), F3(스택 · TLS가 스택을 줄임), K6(딕셔너리 로깅), H7(Zephyr 로깅), EL B2(glibc · musl errno · TLS)
