# C8. 커널 진입 경로 — API 한 줄이 커널에 들어가는 세 가지 길: 함수 호출, FreeRTOS-MPU SVC, Zephyr syscall

> **이 노트를 다 읽으면**: 기본 FreeRTOS에서 `xQueueSend`가 왜 `bl` 한 줄짜리 함수 호출인지 디스어셈블로 보여 줄 수 있다 · FreeRTOS-MPU(v2)가 매크로 이름 바꾸기 → 래퍼 → `svc` → 시스템 콜 스택 → 핸들·ACL 검사 → 커널 함수로 가는 길을 gdb로 한 단계씩 따라갈 수 있다 · Zephyr `CONFIG_USERSPACE`의 `__syscall` → 생성 코드(`z_impl_`/`z_vrfy_`/`z_mrsh_`) → `svc 3` → `_k_syscall_table` 경로를 설명할 수 있다 · 세 경로의 비용(QEMU에서 직접 잰 숫자)과 "대부분의 MCU RTOS가 경계를 두지 않는 이유"를 말할 수 있다
> **왜 필요한가**: 면접의 "Is a FreeRTOS API call a system call?", "How does an unprivileged task call the kernel?", "What does Zephyr userspace cost you?" 질문, 그리고 안전·보안 요구가 붙은 드론·차량 프로젝트에서 "태스크를 격리하자"는 결정을 내릴 때 바로 쓰는 지식이다.
> **Don 기준 난이도**: 함수 호출 규약(AAPCS), 예외 진입, 스택 프레임, 링커 섹션은 SSD 펌웨어에서 이미 안다 / "RTOS API 호출이 SVC 예외가 되는 순간", 시스템 콜 전용 스택, 핸들 = 정수 인덱스, Zephyr의 빌드 시점 코드 생성(`__syscall`)이 새롭다
> **선행 노트**: B1(특권·CONTROL·MSP/PSP), B2(예외 진입·복귀), B4(SVC), A3(라이브러리형 커널 §3), C7(크리티컬 섹션), EL E6(Linux syscall 경로 — `svc #0` → `sys_call_table`). 이어지는 노트: F4(MPU 리전 자체), F5(유저 모드 격리 설계), G4(FreeRTOS-MPU 실무), H9(Zephyr 고급 기능), O1·O6(안전·분할)

---

## 0. 큰 그림 — 이게 왜 필요한가

Linux에서 `read()`를 부르면 `svc #0`으로 예외가 나고, 커널이 레지스터를 저장하고, 번호로 `sys_call_table`을 찾아 커널 함수를 부른다(EL E6 §1). 유저 공간과 커널은 **다른 특권 레벨, 다른 주소 공간**이고, 그 사이를 넘는 유일한 문이 시스템 콜이다.

RTOS에서는 이야기가 다르다. 기본 FreeRTOS와 기본 Zephyr는 **라이브러리형 커널**이다(A3 §3). 태스크 코드와 커널 코드가 같은 이미지, 같은 주소 공간, 같은 특권 레벨에서 돈다. 그래서 `xQueueSend()`는 시스템 콜이 아니라 **그냥 C 함수 호출**이다. 문이 없으니 넘을 것도 없다.

그런데 MPU를 쓰는 보호 빌드(FreeRTOS-MPU, Zephyr `CONFIG_USERSPACE`)를 켜면 문이 생긴다. 비특권 태스크는 커널 데이터에 손댈 수 없고, 커널에 일을 시키려면 `svc` 명령으로 예외를 일으켜 **특권 코드에게 부탁**해야 한다. 이 노트는 이 세 가지 길을 소스·디스어셈블·gdb·측정으로 끝까지 따라간다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="115" y="22" font-size="13" text-anchor="middle" font-weight="bold">기본 FreeRTOS · Zephyr</text>
<text x="115" y="40" font-size="12" text-anchor="middle">라이브러리형 (경계 없음)</text>
<text x="340" y="22" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS-MPU (v2)</text>
<text x="340" y="40" font-size="12" text-anchor="middle">portUSING_MPU_WRAPPERS = 1</text>
<text x="565" y="22" font-size="13" text-anchor="middle" font-weight="bold">Zephyr userspace</text>
<text x="565" y="40" font-size="12" text-anchor="middle">CONFIG_USERSPACE = y</text>
<rect x="15" y="56" width="200" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="81" font-size="13" text-anchor="middle">태스크 (특권으로 실행)</text>
<rect x="240" y="56" width="200" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="81" font-size="13" text-anchor="middle">restricted 태스크 (비특권)</text>
<rect x="465" y="56" width="200" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="565" y="81" font-size="13" text-anchor="middle">K_USER 스레드 (비특권)</text>
<line x1="115" y1="96" x2="115" y2="284" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<text x="122" y="150" font-size="12">bl xQueueGenericSend</text>
<text x="122" y="168" font-size="12">= 그냥 함수 호출</text>
<text x="122" y="200" font-size="12">모드 전환 없음</text>
<text x="122" y="218" font-size="12">인자 검사 없음</text>
<line x1="340" y1="96" x2="340" y2="120" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<rect x="240" y="122" width="200" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="144" font-size="12" text-anchor="middle">MPU_xQueueGenericSend 래퍼</text>
<line x1="340" y1="156" x2="340" y2="190" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<text x="346" y="178" font-size="12">svc 33</text>
<line x1="465" y1="96" x2="565" y2="96" stroke="none"/>
<line x1="565" y1="96" x2="565" y2="120" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<rect x="465" y="122" width="200" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="565" y="144" font-size="12" text-anchor="middle">k_sem_give() 생성 인라인</text>
<line x1="565" y1="156" x2="565" y2="190" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<text x="559" y="178" font-size="12" text-anchor="end">svc 3 · r6 = 번호</text>
<line x1="230" y1="186" x2="672" y2="186" stroke="#d0564a" stroke-width="1.4" stroke-dasharray="6,4"/>
<text x="670" y="204" font-size="12" text-anchor="end">특권 경계 (SVC 예외)</text>
<rect x="240" y="212" width="200" height="56" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="232" font-size="12" text-anchor="middle">syscall 스택으로 교체</text>
<text x="340" y="248" font-size="12" text-anchor="middle">특권 올림 → Impl 함수</text>
<text x="340" y="264" font-size="12" text-anchor="middle">핸들 · ACL · 버퍼 검사</text>
<rect x="465" y="212" width="200" height="56" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="565" y="232" font-size="12" text-anchor="middle">특권 스택으로 교체</text>
<text x="565" y="248" font-size="12" text-anchor="middle">_k_syscall_table 점프</text>
<text x="565" y="264" font-size="12" text-anchor="middle">z_vrfy: 객체 · 메모리 검사</text>
<line x1="340" y1="268" x2="340" y2="284" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<line x1="565" y1="268" x2="565" y2="284" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<rect x="15" y="286" width="200" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="115" y="311" font-size="13" text-anchor="middle">xQueueGenericSend()</text>
<rect x="240" y="286" width="200" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="311" font-size="13" text-anchor="middle">xQueueGenericSend()</text>
<rect x="465" y="286" width="200" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565" y="311" font-size="13" text-anchor="middle">z_impl_k_sem_give()</text>
<text x="115" y="350" font-size="12" text-anchor="middle">돌아올 때: bx lr</text>
<text x="340" y="350" font-size="12" text-anchor="middle">돌아올 때: svc 103 한 번 더</text>
<text x="565" y="350" font-size="12" text-anchor="middle">돌아올 때: CONTROL.nPRIV = 1</text>
<text x="115" y="378" font-size="12" text-anchor="middle">xQueueSend 약 3.9 us</text>
<text x="340" y="378" font-size="12" text-anchor="middle">비특권 xQueueSend 약 13.2 us</text>
<text x="565" y="378" font-size="12" text-anchor="middle">비교는 §5 차트</text>
</svg>
```

그림 1 — 커널 진입 경로 세 가지. 왼쪽은 경계가 없어서 커널 함수까지 `bl` 한 번이다. 가운데(FreeRTOS-MPU)와 오른쪽(Zephyr userspace)은 비특권 코드가 `svc`로 예외를 일으키고, 특권 코드가 스택을 바꾸고 인자를 검사한 다음에야 진짜 커널 함수를 부른다. 아래 숫자는 이 노트에서 QEMU icount로 잰 값이다.

Don의 SSD 펌웨어로 비유하면, 기본 RTOS는 **FTL 코드와 NAND 드라이버가 같은 이미지에 링크되어 서로를 그냥 함수로 부르는 구조**다. 보호 빌드는 **호스트 명령이 반드시 명령 큐(문)를 통해서만 FTL에 들어오고, FTL이 명령의 LBA 범위·버퍼 주소를 검사한 뒤에야 실행하는 구조**에 가깝다. 검사 덕분에 잘못된 명령이 FTL 메타데이터를 깨지 못하지만, 명령 하나하나에 비용이 붙는다.

| 질문 | 기본 FreeRTOS · Zephyr | FreeRTOS-MPU v2 | Zephyr userspace |
|---|---|---|---|
| API 호출 = ? | C 함수 호출 (`bl`) | 비특권이면 `svc`, 특권이면 `b` | user면 `svc 3`, supervisor면 직접 호출 |
| 커널 진입 때 모드 전환 | 없음 | 비특권 → 특권 (CONTROL.nPRIV 0) | user → privileged |
| 인자 검사 | `configASSERT` 정도 | 핸들 인덱스 · ACL · 버퍼 접근 권한 | `K_SYSCALL_OBJ` · `K_SYSCALL_MEMORY_*` |
| 객체 핸들 | 커널 구조체 포인터 | 작은 정수 인덱스 (풀 테이블) | 커널 객체 포인터 + 권한 비트맵 |
| 커널 데이터 보호 | 없음 | `privileged_data` 리전 (MPU) | memory domain 밖은 접근 불가 |
| 커널 코드 보호 | 없음 | `privileged_functions` 리전 (비특권 실행 불가) | 없음 (코드는 user도 실행 가능, 데이터만 보호) |

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 @ 25 MHz, v7-M MPU 8리전 모델링) + FreeRTOS V11.3.2(`GCC/ARM_CM3`와 `GCC/ARM_CM3_MPU` 포트) + Zephyr v4.4.2(`mps2/an385`, `CONFIG_USERSPACE=y`). 시간 수치는 전부 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간)에서 MPS2 FPGAIO COUNTER(0x40028018, 25 MHz, 1 tick = 40 ns)로 쟀다. **QEMU icount 기준이고 실제 칩과 절대값은 다르다**(캐시·플래시 wait state·버스, 그리고 특히 **예외 스태킹 사이클**을 QEMU가 모델링하지 않는다 — SVC 경로는 실칩에서 이 노트 숫자보다 예외당 최소 12 cycle 이상 더 든다). 같은 조건에서 잰 **상대 비교**로 읽는다.

---

## 1. 기본 경로 — 라이브러리형 커널에서 API 호출은 함수 호출이다

### 1.1 직관

기본 FreeRTOS에서 태스크는 **처음부터 끝까지 특권(privileged) Thread 모드**로 돈다(B1). 커널 코드도 같은 특권이다. 그러니 태스크가 커널에 들어갈 때 넘어야 할 문이 없다. `xQueueSend(q, &v, 0)`은 `queue.h`의 매크로가 `xQueueGenericSend(q, &v, 0, queueSEND_TO_BACK)`로 바꾸고, 컴파일러는 그걸 AAPCS 함수 호출(`r0`–`r3`에 인자, `bl`)로 만든다. 커널 함수는 필요하면 크리티컬 섹션(C7)으로 자기 자료구조를 보호하고, 그냥 `bx lr`/`pop {pc}`로 돌아온다.

- **정의 — 커널 진입 경로(kernel entry path)**: 애플리케이션 코드가 커널 서비스(큐·세마포어·스케줄러)를 쓰려고 할 때 제어가 커널 코드로 넘어가는 방식. 함수 호출일 수도, 소프트웨어 예외(SVC·`ecall`·`syscall`)일 수도 있다.
- **정의 — 시스템 콜(system call)**: 특권 경계를 넘어 커널에 서비스를 요청하는 진입 경로. 보통 예외 명령으로 들어가고, 커널이 인자를 검사한다. 기본 RTOS에는 **이게 없다**.

```svg
<svg viewBox="0 0 640 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="20" width="620" height="210" rx="6" fill="#3f9a6b" fill-opacity="0.05" stroke="#3f9a6b" stroke-dasharray="4,3"/>
<text x="20" y="40" font-size="12">전부 특권 Thread 모드 · 같은 주소 공간 · 같은 PSP 스택 (태스크 스택)</text>
<rect x="30" y="60" width="170" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="80" font-size="12" text-anchor="middle">bench 태스크</text>
<text x="115" y="96" font-size="12" text-anchor="middle">xQueueSend(q, &amp;v, 0)</text>
<line x1="200" y1="82" x2="250" y2="82" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="225" y="74" font-size="12" text-anchor="middle">bl</text>
<rect x="252" y="60" width="170" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="337" y="80" font-size="12" text-anchor="middle">xQueueGenericSend</text>
<text x="337" y="96" font-size="12" text-anchor="middle">queue.c</text>
<line x1="422" y1="82" x2="472" y2="82" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="447" y="74" font-size="12" text-anchor="middle">bl</text>
<rect x="474" y="60" width="146" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="547" y="80" font-size="12" text-anchor="middle">vPortEnterCritical</text>
<text x="547" y="96" font-size="12" text-anchor="middle">BASEPRI 올림</text>
<rect x="252" y="130" width="368" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="436" y="155" font-size="12" text-anchor="middle">복사 → 대기 태스크 깨우기 → 임계 구역 해제</text>
<line x1="337" y1="104" x2="337" y2="128" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<line x1="252" y1="190" x2="115" y2="190" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<line x1="115" y1="190" x2="115" y2="106" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="260" y="210" font-size="12">pop {pc} 로 복귀 — 예외도, 스택 교체도, 특권 변경도 없다</text>
</svg>
```

그림 2 — 기본 FreeRTOS의 `xQueueSend` 경로. 처음부터 끝까지 같은 모드, 같은 스택이다. "커널 진입"이라고 부를 만한 순간은 사실 `vPortEnterCritical`이 BASEPRI를 올리는 것뿐이다(C7).

### 1.2 예제 1 — 기본 FreeRTOS API 한 번의 비용을 잰다

무엇을 확인하나: 빈 함수 호출, 가장 단순한 API(`uxQueueMessagesWaiting`), 큐 송신·수신 한 번이 각각 몇 ns(몇 명령)인지. 루프 256회 평균이고 루프 자체 비용(명령 3~4개)이 포함된다.

```c
/* C8 예제 1: 기본(라이브러리형) FreeRTOS — API 호출 = 그냥 함수 호출. 비용 측정 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"

#define FPGAIO_COUNTER REG32(0x40028018u)   /* 25 MHz 자유 카운터: 1 tick = 40 ns */
#define N 256
static QueueHandle_t q;

__attribute__((noinline)) UBaseType_t empty_call(QueueHandle_t h) { __asm volatile("" ::: "memory"); return (UBaseType_t)(uintptr_t)h; }

static inline uint32_t read_control(void) { uint32_t c; __asm volatile("mrs %0, control" : "=r"(c)); return c; }

static void report(const char *name, uint32_t ticks)
{
    uint32_t ns = ticks * 40u / N;                    /* 1회당 ns */
    board_printf_raw("%s: %u ns/call (~%u insn @32ns)\n", name, ns, ns / 32u);
}

static void bench(void *arg)
{
    (void)arg;
    uint32_t v = 0, t0, t1;
    volatile UBaseType_t sink = 0;

    t0 = FPGAIO_COUNTER;
    for (int i = 0; i < N; i++) sink += empty_call(q);
    t1 = FPGAIO_COUNTER;  report("empty noinline call     ", t1 - t0);

    t0 = FPGAIO_COUNTER;
    for (int i = 0; i < N; i++) sink += uxQueueMessagesWaiting(q);
    t1 = FPGAIO_COUNTER;  report("uxQueueMessagesWaiting  ", t1 - t0);

    t0 = FPGAIO_COUNTER;
    for (int i = 0; i < N; i++) { v = i; xQueueSend(q, &v, 0); }
    t1 = FPGAIO_COUNTER;  report("xQueueSend (no waiter)  ", t1 - t0);

    t0 = FPGAIO_COUNTER;
    for (int i = 0; i < N; i++) xQueueReceive(q, &v, 0);
    t1 = FPGAIO_COUNTER;  report("xQueueReceive (has item)", t1 - t0);

    board_printf_raw("CONTROL=0x%x (nPRIV=bit0=%u) last v=%u\n", read_control(), read_control() & 1u, v);
    board_exit(0);
}

int main(void)
{
    q = xQueueCreate(N, sizeof(uint32_t));
    xTaskCreate(bench, "bench", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/C8/ex1/main.c OUT=/var/tmp/rtos/C8/ex1 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C8/ex1/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
  12560	    100	 135408	 148068	  24264	/var/tmp/rtos/C8/ex1/app.elf
empty noinline call     : 256 ns/call (~8 insn @32ns)
uxQueueMessagesWaiting  : 1312 ns/call (~41 insn @32ns)
xQueueSend (no waiter)  : 3878 ns/call (~121 insn @32ns)
xQueueReceive (has item): 3110 ns/call (~97 insn @32ns)
CONTROL=0x2 (nPRIV=bit0=0) last v=255
[board_exit] code=0
```

출력에서 볼 것: `CONTROL=0x2` — bit1(SPSEL)=1이라 PSP(태스크 스택)를 쓰고, **bit0(nPRIV)=0이라 태스크가 특권으로 돈다**. 그리고 큐 송신 한 번이 약 121 명령이다. 이 중 함수 호출 자체(빈 호출 약 8 명령, 루프 포함)는 아주 작은 부분이고, 나머지는 크리티컬 섹션 진입·해제, 큐 상태 검사, `memcpy`, 대기 리스트 확인이다. **"커널 진입 비용"은 사실상 0**이고 비용의 대부분은 커널이 하는 일 자체다.

### 1.3 예제 2 — 호출 지점 디스어셈블: `bl` 하나, 그리고 이미지 전체에 `svc`는 딱 하나

무엇을 확인하나: 컴파일러가 `xQueueSend`를 어떤 명령으로 만들었는지, 그리고 기본 포트 이미지 안에 `svc`가 어디 있는지.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/C8/ex1;
  D="arm-none-eabi-objdump -d --no-show-raw-insn app.elf";
  $D | awk "/<bench>:/,/^$/" | grep -B6 -A2 "bl.*xQueueGenericSend";
  echo "svc count: $($D | grep -c svc)"; $D | grep svc;
  arm-none-eabi-addr2line -f -e app.elf 0x1fd6;
  $D | awk "/<uxQueueMessagesWaiting>:/,/^$/" | head -9'
```

```text
     136:	movs	r3, #0
     138:	str	r4, [sp, #0]
     13a:	mov	r1, sp
     13c:	mov	r2, r3
     13e:	ldr	r0, [r5, #0]
     140:	adds	r4, #1
     142:	bl	1564 <xQueueGenericSend>
     146:	cmp.w	r4, #256	@ 0x100
     14a:	bne.n	136 <bench+0x56>
svc count: 1
    1fd6:	svc	0
prvPortStartFirstTask
/var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:249
00001858 <uxQueueMessagesWaiting>:
    1858:	push	{r4, lr}
    185a:	cbz	r0, 186c <uxQueueMessagesWaiting+0x14>
    185c:	mov	r4, r0
    185e:	bl	2090 <vPortEnterCritical>
    1862:	ldr	r4, [r4, #56]	@ 0x38
    1864:	bl	20dc <vPortExitCritical>
    1868:	mov	r0, r4
    186a:	pop	{r4, pc}
```

출력에서 볼 것:

- `xQueueSend(q, &v, 0)`은 `r0`=핸들, `r1`=`&v`(sp), `r2`=0(timeout), `r3`=0(`queueSEND_TO_BACK`)을 채우고 `bl xQueueGenericSend` 한 줄이다. **SVC도, 모드 전환도 없다.**
- 이미지 전체의 `svc`는 `prvPortStartFirstTask`의 `svc 0` 하나뿐이다. 이건 스케줄러를 시작할 때 첫 태스크로 들어가는 용도이고(C3, B4) API 호출과 관계없다.
- `uxQueueMessagesWaiting`의 몸통은 `cbz r0`(NULL 검사 = `configASSERT`) → 임계 구역 → `ldr r4, [r4, #56]`(`Queue_t.uxMessagesWaiting` 읽기) → 임계 구역 해제가 전부다. **핸들이 진짜 큐인지는 확인하지 않는다.** 다음 예제에서 직접 속여 본다.

### 1.4 예제 3 — 인자 검사가 없다는 것의 의미: 가짜 핸들을 넘겨 본다

무엇을 확인하나: 라이브러리형 커널은 핸들을 "믿는다". 아무 메모리 주소를 큐 핸들이라고 넘겨도 커널은 그 주소 + 56을 그대로 읽는다.

```c
/* C8 예제 3: 기본 FreeRTOS는 핸들을 검사하지 않는다 — 가짜 "큐"를 넘겨도 그대로 읽는다 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"

static uint32_t fake[32];                         /* 큐인 척하는 아무 메모리 */

static void t(void *arg)
{
    (void)arg;
    fake[14] = 1234;                              /* offset 56 = Queue_t.uxMessagesWaiting (objdump로 확인) */
    UBaseType_t n = uxQueueMessagesWaiting((QueueHandle_t)fake);
    board_printf_raw("uxQueueMessagesWaiting(fake) = %u  (no error, no check)\n", (unsigned)n);
    n = uxQueueMessagesWaiting((QueueHandle_t)0x00000100);   /* 벡터 테이블 근처 FLASH */
    board_printf_raw("uxQueueMessagesWaiting(0x100) = 0x%x (whatever word is at 0x138)\n", (unsigned)n);
    board_exit(0);
}

int main(void)
{
    xTaskCreate(t, "t", 512, NULL, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```text
uxQueueMessagesWaiting(fake) = 1234  (no error, no check)
uxQueueMessagesWaiting(0x100) = 0x4803b508 (whatever word is at 0x138)
[board_exit] code=0
```

```sh
limactl shell elinux -- bash -lc 'arm-none-eabi-objdump -d /var/tmp/rtos/C8/ex1b/app.elf | grep -E "^\s+13[4-c]:"'
```

```text
     134:	00002bfc 	.word	0x00002bfc
     138:	b508      	push	{r3, lr}
     13a:	4803      	ldr	r0, [pc, #12]	@ (148 <vApplicationStackOverflowHook+0x10>)
     13c:	f000 f892 	bl	264 <board_printf_raw>
```

출력에서 볼 것: 커널은 가짜 배열을 큐로 믿고 1234를 "대기 중인 메시지 수"로 돌려준다. 0x100을 넘기면 0x138에 있는 **코드 바이트**(`b508 4803` = `push {r3, lr}; ldr r0, [pc, #12]`, 같은 빌드를 `objdump -d`로 보면 그 자리에 이 두 명령이 있다)를 숫자로 돌려준다. 읽기라서 이 정도지만, `xQueueSend`에 가짜 핸들을 주면 커널은 그 주소에 **쓴다** — 다른 태스크의 스택이나 TCB가 조용히 깨진다. 이게 "인자 검사 없음"의 진짜 의미다. 뒤의 §3.9 예제 9에서 같은 일을 FreeRTOS-MPU에 시키면 0이 돌아온다(핸들 검사에서 거절).

### 1.5 RTOS 커널 안과 연결, 그리고 Don 경험

- 소스 위치: `include/queue.h`의 `#define xQueueSend(...) xQueueGenericSend(..., queueSEND_TO_BACK)` → `queue.c`의 `xQueueGenericSend()` → `taskENTER_CRITICAL()` = `portENTER_CRITICAL()` = `vPortEnterCritical()`(`portable/GCC/ARM_CM3/port.c`). 핸들은 `typedef struct QueueDefinition * QueueHandle_t`이고 커널 안에서 그대로 `Queue_t *`로 캐스팅된다.
- Zephyr도 `CONFIG_USERSPACE=n`이면 똑같다. `k_sem_give(&sem)`은 생성된 인라인 함수가 곧바로 `z_impl_k_sem_give(&sem)`을 부른다(§4.2에서 생성 코드로 확인).
- Don의 SSD 펌웨어에서 FTL이 NAND 드라이버의 `nand_program(desc)`를 부를 때 디스크립터 포인터를 검사하지 않던 것과 같다. 같은 팀이 짠 같은 이미지 안의 코드끼리는 서로를 믿는 게 기본이고, 그래서 빠르다. 대신 한 모듈의 포인터 버그가 다른 모듈의 상태를 깨면 **증상은 엉뚱한 곳에서** 나타난다.

### 1.6 흔한 함정

- "RTOS API는 시스템 콜이니까 느리다"는 오해. 기본 빌드에서는 함수 호출이고, 비용은 커널이 하는 일(임계 구역, 리스트 조작, 복사)에서 나온다. 최적화하려면 진입 경로가 아니라 **호출 횟수와 임계 구역 길이**를 줄여야 한다.
- 반대로 "함수 호출이니까 ISR에서 아무 API나 불러도 된다"는 오해. ISR에서는 `...FromISR` 변형만 써야 한다. 진입 방식이 아니라 **블로킹 가능성과 임계 구역 방식(BASEPRI 저장·복원)** 때문이다(C7, E1).
- `configASSERT`를 끈 릴리스 빌드에서는 NULL 핸들조차 검사하지 않는다. `cbz r0`가 사라지고, NULL + 56을 읽는다.

---
## 2. 보호 빌드의 재료 — 특권 비트, MPU, SVC 세 가지

보호 빌드가 왜 SVC를 쓰는지는 Cortex-M 하드웨어 규칙 세 개로 설명된다. 자세한 레지스터는 B1(CONTROL), B4(SVC), F4(MPU)에 있고, 여기서는 "커널 진입"에 필요한 부분만 짚는다.

1. **CONTROL.nPRIV**(bit0): Thread 모드의 특권 여부. 1이면 비특권이다. **비특권 코드는 이 비트를 0으로 되돌릴 수 없다** — 비특권에서 `msr control`은 조용히 무시된다(예제 9의 BAD6에서 직접 확인). Handler 모드(예외 처리 중)는 nPRIV와 관계없이 항상 특권이다.
2. **MPU**: 비특권 접근은 MPU 리전이 허락한 주소에만 갈 수 있다. 특권 코드는 `PRIVDEFENA`(MPU_CTRL bit2)가 켜져 있으면 리전이 없는 곳도 기본 메모리 맵으로 접근한다. 그래서 "커널 데이터를 특권 전용 리전에 두면" 비특권 태스크는 그걸 못 건드린다.
3. **SVC**: `svc #imm8`은 동기 예외를 일으킨다. 예외에 들어가면 Handler 모드 = 특권이 되고, 하드웨어가 r0–r3·r12·lr·pc·xPSR 8워드를 현재 스택에 쌓는다(B2). 핸들러는 쌓인 PC에서 2바이트 앞의 명령을 읽어 `imm8`(서비스 번호)을 꺼낸다.

세 규칙을 합치면 결론이 하나다: **비특권 코드가 특권 코드를 실행시키는 유일한 정상 경로는 예외, 그중에서도 SVC다.** 그래서 보호 빌드의 모든 API 호출은 결국 `svc`가 된다. Linux의 `svc #0`(EL E6)과 같은 원리이고, RTOS에서 달라지는 점은 (1) 주소 공간이 하나라 "복사"가 필요 없고 포인터를 검사만 한다는 것, (2) MMU가 아니라 MPU라서 리전 수(v7-M 8개)와 정렬 제약이 설계를 좌우한다는 것, (3) 시스템 콜을 Handler 모드가 아니라 **특권 Thread 모드로 내려와서** 실행한다는 것(§3.7, §4.4 — 인터럽트에 선점될 수 있게)이다.

---

## 3. FreeRTOS-MPU — 같은 `xQueueSend`가 SVC가 되는 과정

### 3.1 어떤 포트가 MPU를 지원하나 (V11.3.2 트리 기준)

`portUSING_MPU_WRAPPERS`를 1로 정의하는 포트만 MPU 래퍼를 쓴다. 랩 트리 `portable/GCC/`에서 `mpu_wrappers_v2_asm.c`를 가진 디렉터리와 `portmacro.h`를 직접 확인한 결과다.

| 아키텍처 | 포트 디렉터리 (GCC) | MPU 켜는 법 | 비고 |
|---|---|---|---|
| Armv7-M (M3) | `ARM_CM3_MPU` | 포트 자체가 MPU 전용 | 이 노트의 실습 포트 |
| Armv7-M (M4/M4F) | `ARM_CM4_MPU` | 포트 자체가 MPU 전용 | FPU 포함 |
| Armv8-M (M23/M33/M35P/M52/M55/M85, STAR-MC3) | `ARM_CM33`, `ARM_CM33_NTZ`, `ARM_CM23`, `ARM_CM55` … | `configENABLE_MPU 1` | `portmacrocommon.h`에서 `portUSING_MPU_WRAPPERS` 정의, TrustZone 있는/없는(NTZ) 변형 모두 |
| Armv6-M (M0/M0+) | `ARM_CM0` | `configENABLE_MPU 1` | MPU 지원은 V11.1.0에서 추가(History.txt) |
| Armv7-R (R4/R5) | `ARM_CRx_MPU` | 포트 자체가 MPU 전용 | V11.1.0에서 추가 |
| Armv8-R AArch64 (R82) | `ARM_CR82` | MPU 지원 추가됨(History.txt) | SMP 포트 |
| 일반 `ARM_CM3`, `ARM_CM4F`, `ARM_CM7`, RISC-V | — | 없음 | 이 트리에서 `ARM_CM7`·`RISC-V` 디렉터리에는 MPU 래퍼 파일이 없다 |

드론 비행 제어기에서 흔한 STM32H7(Cortex-M7)은 이 표의 `ARM_CM7` 줄에 해당해서, 같은 트리 안에 M7 전용 MPU 포트 디렉터리가 없다는 점을 기억해 둔다(어떤 포트를 대신 쓰라는 공식 권고는 이 노트에서 확인하지 못했다).

### 3.2 예제 4 — 같은 소스 한 줄, 다른 함수 이름: `mpu_wrappers.h`의 매크로 바꿔치기

FreeRTOS-MPU의 첫 번째 장치는 놀랍게도 **전처리기**다. `FreeRTOS.h`가 끌어오는 `mpu_wrappers.h`는 `portUSING_MPU_WRAPPERS == 1`이면 공개 API 이름을 전부 `MPU_` 접두어 이름으로 `#define`한다. 앱 코드는 한 글자도 바꾸지 않아도 래퍼를 부르게 된다. 반면 커널 소스(`tasks.c`, `queue.c` …)는 맨 위에서 `MPU_WRAPPERS_INCLUDED_FROM_API_FILE`을 정의해서 이 바꿔치기를 끈다 — 커널 안에서는 진짜 함수를 불러야 하니까.

무엇을 확인하나: 같은 `call.c`를 일반 포트와 MPU 포트 헤더로 각각 전처리했을 때 호출되는 함수 이름.

```c
/* C8 예제 4: 같은 소스 한 줄이 포트에 따라 다른 함수 이름으로 바뀌는 것을 전처리기로 확인 */
#include "FreeRTOS.h"
#include "queue.h"
BaseType_t send_one(QueueHandle_t q, const uint32_t *v) { return xQueueSend(q, v, 0); }
```

```sh
limactl shell elinux -- bash -lc 'K=/var/rtoslab/src/FreeRTOS-Kernel; C="arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -E -P -I/var/rtoslab/templates/common -I$K/include";
echo "--- plain ARM_CM3 port:"; $C -I/var/rtoslab/templates/freertos-mps2 -I$K/portable/GCC/ARM_CM3 /Users/donh/.rtos_scratch/C8/ex2/call.c | grep -A1 "send_one" | tail -2;
echo "--- ARM_CM3_MPU port (portUSING_MPU_WRAPPERS=1):"; $C -I/Users/donh/.rtos_scratch/C8/mpu -I$K/portable/GCC/ARM_CM3_MPU /Users/donh/.rtos_scratch/C8/ex2/call.c 2>/dev/null | grep -A1 "send_one" | tail -2;
echo "--- macro chain:"; grep -n "define xQueueSend(" $K/include/queue.h; grep -n "define xQueueGenericSend " $K/include/mpu_wrappers.h; grep -rn "define MPU_WRAPPERS_INCLUDED_FROM_API_FILE" $K/*.c | sed "s#$K/##"'
```

```text
--- plain ARM_CM3 port:
BaseType_t send_one(QueueHandle_t q, const uint32_t *v) { return xQueueGenericSend( ( q ), ( v ), ( 0 ), ( ( BaseType_t ) 0 ) ); }
--- ARM_CM3_MPU port (portUSING_MPU_WRAPPERS=1):
BaseType_t send_one(QueueHandle_t q, const uint32_t *v) { return MPU_xQueueGenericSend( ( q ), ( v ), ( 0 ), ( ( BaseType_t ) 0 ) ); }
--- macro chain:
513:#define xQueueSend( xQueue, pvItemToQueue, xTicksToWait ) \
123:        #define xQueueGenericSend            MPU_xQueueGenericSend
event_groups.c:35:#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
list.c:35:#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
queue.c:35:#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
stream_buffer.c:35:#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
tasks.c:37:#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
timers.c:35:#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
```

출력에서 볼 것: 매크로 두 단계(`xQueueSend` → `xQueueGenericSend` → `MPU_xQueueGenericSend`)를 거쳐 앱의 호출 대상이 바뀐다. 커널 `.c` 파일 6개는 모두 이 바꿔치기를 끈다. 그래서 같은 이름 `xQueueGenericSend`가 **앱에서는 래퍼, 커널에서는 진짜 함수**를 뜻한다. 같은 헤더가 `PRIVILEGED_FUNCTION`·`PRIVILEGED_DATA`·`FREERTOS_SYSTEM_CALL`을 각각 `section("privileged_functions")`·`section("privileged_data")`·`section("freertos_system_calls")` 속성으로 정의한다(`mpu_wrappers.h` 283–285행). 커널 함수·변수 선언에 이미 이 매크로가 붙어 있어서, MPU 포트로 빌드하면 커널 코드와 데이터가 자동으로 별도 섹션에 모인다.

### 3.3 MPU 래퍼 v1 vs v2

FreeRTOS-MPU에는 래퍼 구현이 두 벌 있다. `configUSE_MPU_WRAPPERS_V1`(기본 0 = v2, `FreeRTOS.h` 79–80행)으로 고른다. v2는 V10.6.0(2023-07-13)에 들어왔고 V10.6.2(2023-11-29)에서 ACL과 "SVC 한 번으로 진입"이 더해졌다(History.txt).

| 항목 | v1 (`portable/Common/mpu_wrappers.c`) | v2 (`mpu_wrappers_v2.c` + 포트의 `mpu_wrappers_v2_asm.c`) |
|---|---|---|
| 진입 방식 | 래퍼가 `svc 102`(`portSVC_RAISE_PRIVILEGE`)로 **특권을 올린 뒤** 진짜 API를 직접 호출, 끝나면 `vResetPrivilege()`로 내림 | API마다 번호가 있는 `svc <SYSTEM_CALL_xxx>`(예: 33), 핸들러가 Impl 함수로 점프시킴 |
| 실행 스택 | 호출한 태스크의 스택 그대로 | 태스크마다 따로 있는 **시스템 콜 전용 특권 스택**(`configSYSTEM_CALL_STACK_SIZE` 워드) |
| 객체 핸들 | 커널 구조체 포인터 | 불투명한 **정수 인덱스**(커널 객체 풀 `configPROTECTED_KERNEL_OBJECT_POOL_SIZE`) |
| 인자 검사 | 없음 (특권으로 올라간 채 진짜 API 실행) | 인덱스 유효성, ACL(`configENABLE_ACCESS_CONTROL_LIST`), 포인터가 가리키는 버퍼에 태스크 권한이 있는지 |
| 커널 진입 위치 제한 | `configENFORCE_SYSTEM_CALLS_FROM_KERNEL_ONLY 1`이면 `svc 102`는 `freertos_system_calls` 섹션에서만 허용 | `svc`를 낸 PC가 `__syscalls_flash_start__`–`__syscalls_flash_end__` 안이어야 함 (항상) |
| 비특권 임계 구역 | `configALLOW_UNPRIVILEGED_CRITICAL_SECTIONS`로 허용 가능 | 허용 불가 (`#error`, `port.c` 68–69행) |
| 문맥 저장 위치 | 태스크 스택 | TCB 안(`xMPU_SETTINGS`) |
| 비특권 태스크에서 금지되는 API | 없음 | 생성·삭제 계열(`xTaskCreate`, `xQueueGenericCreate`, `vTaskDelete`, `vTaskSuspendAll` …)은 비특권에서 못 부른다 |

v1의 약점은 분명하다. 비특권 태스크가 **아무 포인터**를 핸들로 넘겨도 래퍼는 특권을 올린 채 진짜 API를 부른다. 즉 v1은 "커널 데이터에 직접 손대는 것"은 막지만 "커널에게 엉뚱한 곳을 쓰게 시키는 것"(confused deputy — 권한 있는 대리인을 속여 대신 나쁜 일을 하게 하는 공격)은 못 막는다. v2의 인덱스 핸들·버퍼 검사·ACL은 바로 이 구멍을 막는다.

### 3.4 설정 항목 — V11.3.2 소스에서 이름 확인

| 매크로 | 어디서 쓰나 (소스) | 이 노트의 값 | 뜻 |
|---|---|---|---|
| `portUSING_MPU_WRAPPERS` | 포트의 `portmacro.h` (ARM_CM3_MPU 78행) | 1 (포트가 정의) | 이 포트가 MPU 래퍼를 쓴다 |
| `configUSE_MPU_WRAPPERS_V1` | `FreeRTOS.h` 79행 기본 0 | 0 (v1 비교 때 1) | v1/v2 선택 |
| `configPROTECTED_KERNEL_OBJECT_POOL_SIZE` | `mpu_wrappers_v2.c` 54행 (`#error` 필수) | 16 | 핸들 테이블 칸 수 = 동시에 존재할 수 있는 커널 객체 수 |
| `configSYSTEM_CALL_STACK_SIZE` | 포트 `portmacro.h` 114행 (`#error` 필수) | 128 워드 | 태스크마다 TCB 안에 들어가는 syscall 스택 |
| `configENABLE_ACCESS_CONTROL_LIST` | `FreeRTOS.h` 84행 기본 0 | 1 (v2일 때) | 비특권 태스크의 객체별 접근 권한 |
| `configENFORCE_SYSTEM_CALLS_FROM_KERNEL_ONLY` | 포트 `portmacro.h` 384행 (없으면 `#warning`) | 1 | v1에서 `svc 102`를 래퍼 섹션에서만 허용 |
| `configALLOW_UNPRIVILEGED_CRITICAL_SECTIONS` | 포트 `port.c` 60–69행 | 0 | v2에서는 반드시 0 |
| `configKERNEL_PROVIDED_STATIC_MEMORY` | `tasks.c` 49행 (`#error`) | 0 | MPU 포트에서는 idle·timer 태스크 메모리를 앱이 줘야 한다 |
| `configENABLE_MPU` | `FreeRTOS.h` 3027행 | (ARMv8-M·M0 포트용) | v7-M `_MPU` 포트에서는 쓰지 않는다 |

### 3.5 예제 5 — MPU 빌드 만들기: 설정·링커 스크립트·Makefile

랩 템플릿(`freertos-mps2`)은 일반 `ARM_CM3` 포트 전용이라, MPU 빌드는 자기 폴더에 설정·링커 스크립트·Makefile을 새로 만들었다. FreeRTOS 공식 저장소에는 같은 QEMU 보드용 MPU 데모(`CORTEX_MPU_M3_MPS2_QEMU_GCC`)가 있다고 알려져 있지만, 랩의 데모 트리는 일부만 받아 둔(sparse) 것이라 그 데모는 없다. 그래서 **포트 소스가 `extern`으로 요구하는 링커 심볼**을 `port.c`에서 grep해서 직접 맞췄다.

```sh
limactl shell elinux -- bash -lc 'grep -ohE "__[A-Za-z_]+(start|end)__" /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3_MPU/port.c | sort -u'
```

```text
__FLASH_segment_end__
__FLASH_segment_start__
__SRAM_segment_end__
__SRAM_segment_start__
__privileged_data_end__
__privileged_data_start__
__privileged_functions_end__
__privileged_functions_start__
__syscalls_flash_end__
__syscalls_flash_start__
```

필요한 심볼은 `__FLASH_segment_start__/end__`, `__privileged_functions_start__/end__`, `__syscalls_flash_start__/end__`, `__SRAM_segment_start__/end__`, `__privileged_data_start__/end__` 다섯 쌍이다(`port.c`의 `prvSetupMPU()`·`vSystemCallEnter()`·`vSystemCallExit()`·`vPortStoreTaskMPUSettings()`에서 사용). `prvSetupMPU()`는 이 심볼로 리전 4개를 고정 설정한다: 리전 5 = FLASH 전체 읽기 전용(모두), 리전 6 = `privileged_functions` 특권 전용, 리전 7 = `privileged_data` 특권 전용 + XN, 리전 4 = 주변장치 0x40000000–0x5FFFFFFF 읽기·쓰기 + XN. v7-M MPU 리전은 **크기가 2의 거듭제곱이고 시작 주소가 크기에 정렬**되어야 하므로(F4), 두 특권 영역을 FLASH·RAM 맨 앞에 두고 크기를 고정했다.

설정 파일 — 랩 기본 설정을 그대로 포함하고 MPU 항목만 더한다.

```c
/* C8: FreeRTOS-MPU(ARM_CM3_MPU, MPU wrappers v2) 설정 — 랩 기본 설정 위에 MPU 항목만 더한다 */
#ifndef C8_MPU_CONFIG_H
#define C8_MPU_CONFIG_H
#define configTOTAL_HEAP_SIZE                    ( 16 * 1024 )   /* heap도 privileged_data 안에 들어간다 */
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#undef  configKERNEL_PROVIDED_STATIC_MEMORY       /* MPU 포트에서는 0이어야 한다(#error) */
#define configKERNEL_PROVIDED_STATIC_MEMORY      0
#define configENFORCE_SYSTEM_CALLS_FROM_KERNEL_ONLY 1
#ifndef configUSE_MPU_WRAPPERS_V1
#define configUSE_MPU_WRAPPERS_V1                0    /* 0 = v2 (V10.6+ 기본, 권장) */
#endif
#define configPROTECTED_KERNEL_OBJECT_POOL_SIZE  16   /* 커널 객체 핸들 테이블 크기 */
#define configSYSTEM_CALL_STACK_SIZE             128  /* 태스크마다 syscall 전용 스택 (words) */
#define configENABLE_ACCESS_CONTROL_LIST         ( configUSE_MPU_WRAPPERS_V1 == 0 )  /* 객체별 접근 권한(ACL), v2 전용 */
#define configALLOW_UNPRIVILEGED_CRITICAL_SECTIONS 0
#endif
```

링커 스크립트 — 랩의 `mps2_m3.ld`에 특권 섹션 세 개와 심볼을 더했다. `startup.c`가 `_sidata`부터 `_edata`까지 복사하므로 `privileged_data`를 `.data` 바로 앞에 붙였다(초기값이 있는 커널 변수도 있어서 FLASH에 실어야 한다).

```text
/* C8: MPS2-AN385 + FreeRTOS-MPU 링커 스크립트. 포트가 요구하는 심볼:
 * __FLASH_segment_start/end__, __privileged_functions_start/end__, __syscalls_flash_start/end__,
 * __SRAM_segment_start/end__, __privileged_data_start/end__  (MPU 리전은 2의 거듭제곱 크기 + 크기 정렬) */
MEMORY { FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 4M
         RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4M }
ENTRY(Reset_Handler)
_Main_Stack_Size = 0x1000;
_Privileged_Functions_Region_Size = 64K;
_Privileged_Data_Region_Size      = 32K;
__FLASH_segment_start__        = ORIGIN(FLASH);
__FLASH_segment_end__          = ORIGIN(FLASH) + LENGTH(FLASH);
__privileged_functions_start__ = ORIGIN(FLASH);
__privileged_functions_end__   = ORIGIN(FLASH) + _Privileged_Functions_Region_Size;
__SRAM_segment_start__         = ORIGIN(RAM);
__SRAM_segment_end__           = ORIGIN(RAM) + LENGTH(RAM);
__privileged_data_start__      = ORIGIN(RAM);
__privileged_data_end__        = ORIGIN(RAM) + _Privileged_Data_Region_Size;
SECTIONS
{
    .privileged_functions : {
        KEEP(*(.isr_vector)) *(privileged_functions)
        . = ALIGN(4); FILL(0xDEAD);
        . = _Privileged_Functions_Region_Size;          /* 리전 끝까지 채운다 */
    } > FLASH
    .freertos_system_calls : {
        . = ALIGN(4); __syscalls_flash_start__ = .;
        *(freertos_system_calls)
        . = ALIGN(4); __syscalls_flash_end__ = .;
    } > FLASH
    .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    _sidata = LOADADDR(.privileged_data);               /* startup.c가 여기부터 _edata까지 복사 */
    .privileged_data : {
        _sdata = .;
        *(privileged_data)
        . = ALIGN(4); FILL(0);
        . = _Privileged_Data_Region_Size;
    } > RAM AT > FLASH
    .data : { *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    .heap (NOLOAD) : { . = ALIGN(8); end = .; _end = .; __heap_start = .; } > RAM
    _estack = ORIGIN(RAM) + LENGTH(RAM);
    __heap_end = _estack - _Main_Stack_Size;
    ASSERT(LOADADDR(.data) == LOADADDR(.privileged_data) + SIZEOF(.privileged_data), "LMA gap")
}
```

Makefile — 일반 템플릿과 다른 점은 포트 디렉터리(`ARM_CM3_MPU`), 래퍼 소스 두 개, 링커 스크립트, 그리고 `kstatic.c`(idle·timer 태스크 메모리)·`fault.c`(MemManage 핸들러)다.

```make
# C8: FreeRTOS V11.3.2 + GCC/ARM_CM3_MPU + MPU wrappers v2 on QEMU mps2-an385
TPL := /var/rtoslab/templates
K   := /var/rtoslab/src/FreeRTOS-Kernel
P   := $(K)/portable/GCC/ARM_CM3_MPU
HERE:= /Users/donh/.rtos_scratch/C8/mpu
OUT ?= /var/tmp/rtos/C8/mpu
.DEFAULT_GOAL := $(OUT)/app.elf
SRCS ?= $(HERE)/main.c $(HERE)/kstatic.c $(HERE)/fault.c
CFLAGS := -mthumb -mcpu=cortex-m3 -O2 -g3 -std=gnu11 -Wall -Wextra -ffunction-sections -fdata-sections \
          -I$(HERE) -I$(TPL)/common -I$(K)/include -I$(P) $(EXTRA_CFLAGS)
LDFLAGS:= -T $(HERE)/mps2_mpu.ld -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,-Map=$(OUT)/app.map
WRAP ?= v2
WSRC := $(if $(filter v1,$(WRAP)),$(K)/portable/Common/mpu_wrappers.c,$(K)/portable/Common/mpu_wrappers_v2.c)
KSRC := $(K)/tasks.c $(K)/list.c $(K)/queue.c $(K)/timers.c $(K)/event_groups.c $(K)/stream_buffer.c \
        $(K)/portable/MemMang/heap_4.c $(P)/port.c $(WSRC)
ALL  := $(SRCS) $(TPL)/freertos-mps2/hooks.c $(TPL)/common/startup.c $(TPL)/common/board.c $(KSRC)
# 커널 소스의 naked 래퍼는 인자를 asm에서만 써서 -Wextra가 unused-parameter를 낸다 → 이 파일만 끔
$(OUT)/wrap.o: $(P)/mpu_wrappers_v2_asm.c $(HERE)/FreeRTOSConfig.h | $(OUT)
	arm-none-eabi-gcc $(CFLAGS) -Wno-unused-parameter -c $< -o $@
$(OUT)/app.elf: $(OUT)/wrap.o $(ALL) $(HERE)/FreeRTOSConfig.h $(HERE)/mps2_mpu.ld | $(OUT)
	arm-none-eabi-gcc $(CFLAGS) $(ALL) $(if $(filter v1,$(WRAP)),,$(OUT)/wrap.o) $(LDFLAGS) -o $@
	arm-none-eabi-size -A $@ | grep -E "privileged|system_calls|^.text|^.data|^.bss"
$(OUT): ; mkdir -p $@
```

```c
/* configKERNEL_PROVIDED_STATIC_MEMORY=0 이므로 idle/timer 태스크 메모리를 앱이 준다 (MPU 포트 요구) */
#include "FreeRTOS.h"
#include "task.h"
static StaticTask_t idle_tcb, timer_tcb;
static StackType_t idle_stack[configMINIMAL_STACK_SIZE] __attribute__((aligned(1024)));
static StackType_t timer_stack[configTIMER_TASK_STACK_DEPTH] __attribute__((aligned(2048)));
void vApplicationGetIdleTaskMemory(StaticTask_t **t, StackType_t **s, configSTACK_DEPTH_TYPE *n)
{ *t = &idle_tcb; *s = idle_stack; *n = configMINIMAL_STACK_SIZE; }
void vApplicationGetTimerTaskMemory(StaticTask_t **t, StackType_t **s, configSTACK_DEPTH_TYPE *n)
{ *t = &timer_tcb; *s = timer_stack; *n = configTIMER_TASK_STACK_DEPTH; }
```

빌드하고 섹션 크기와 핵심 심볼 위치를 본다.

```sh
limactl shell elinux -- bash -lc 'make -s -B -f /Users/donh/.rtos_scratch/C8/mpu/Makefile; cd /var/tmp/rtos/C8/mpu;
  arm-none-eabi-nm -n app.elf | grep -E " (__FLASH_segment|__privileged|__syscalls|__SRAM_segment)| (xQueueGenericSend|MPU_xQueueGenericSend|MPU_xQueueGenericSendImpl|vSystemCallEnter|pxCurrentTCB|ucHeap|user_task|board_printf_raw|uxSystemCallImplementations|user_stack)$"'
```

```text
.privileged_functions     65536           0
.freertos_system_calls     1328       65536
.text                      9064       66864
.privileged_data          32768   536870912
.data                       100   536903680
.bss                       9080   536905728
00000000 T __FLASH_segment_start__
00000000 T __privileged_functions_start__
00002cc8 T xQueueGenericSend
00005c60 T vSystemCallEnter
00006b44 T MPU_xQueueGenericSendImpl
00010000 T __privileged_functions_end__
00010000 T __syscalls_flash_start__
00010208 T MPU_xQueueGenericSend
00010530 T __syscalls_flash_end__
00010634 t user_task
0001091c T board_printf_raw
00400000 T __FLASH_segment_end__
20000000 T __SRAM_segment_start__
20000000 T __privileged_data_start__
20000144 D pxCurrentTCB
200002d4 d ucHeap
200042d8 D uxSystemCallImplementations
20008000 T __privileged_data_end__
20008800 b user_stack
20400000 T __SRAM_segment_end__
```

출력에서 볼 것:

- 진짜 커널 함수(`xQueueGenericSend`, `vSystemCallEnter`, `MPU_xQueueGenericSendImpl`)는 전부 0x0–0xFFFF `privileged_functions` 안에 있다. 래퍼 `MPU_xQueueGenericSend`는 0x10000–0x10530 `freertos_system_calls` 섹션(1328바이트 = 래퍼 수십 개)에 있다. 앱 코드 `user_task`와 `board_printf_raw`는 그 뒤 `.text`다.
- 커널 데이터(`pxCurrentTCB`, 힙 `ucHeap`, 시스템 콜 표 `uxSystemCallImplementations`)는 0x20000000–0x20007FFF `privileged_data` 안에 있다. **TCB와 큐도 힙에서 할당되므로 같은 보호를 받는다.**
- 시행착오 하나: 처음에 `_Privileged_Functions_Region_Size = 32K`로 했더니 링커가 `cannot move location counter backwards (from 000082c0 to 00008000)`로 실패했다. 이 설정(`-O2 -g3`, trace·stats 기능 켬)에서 커널 코드가 약 33.5 KB라 32 KB 리전에 안 들어간 것이다. 2의 거듭제곱 리전이라 다음 크기는 64 KB — 약 30 KB가 낭비된다. v7-M MPU 설계의 전형적인 비용이다(F4).

### 3.6 예제 6 — restricted 태스크가 큐를 쓴다: 핸들 = 인덱스, ACL, 그리고 비용

무엇을 확인하나: (1) 비특권 restricted 태스크와 특권 태스크의 CONTROL 값, (2) v2 핸들이 포인터가 아니라 작은 정수라는 것, (3) ACL로 권한을 받기 전에는 `xQueueSend`가 실패하는 것, (4) 같은 API의 비용이 특권/비특권 경로에서 얼마나 다른지.

- **restricted 태스크**: `xTaskCreateRestricted()`(또는 `...Static`)로 만든 태스크. `TaskParameters_t`에 스택 버퍼와 MPU 리전 3개(`xRegions[portNUM_CONFIGURABLE_REGIONS]`)를 직접 준다. 우선순위에 `portPRIVILEGE_BIT`가 없으면 비특권으로 시작한다. 접근할 수 있는 메모리는 **자기 스택 + 준 리전 + FLASH 읽기 + 주변장치**뿐이다.
- **특권 태스크**: 우선순위에 `portPRIVILEGE_BIT`(0x80000000)를 OR해서 만든다. 이 예제의 `priv_task`가 그렇다.

```c
/* C8 예제 6: FreeRTOS-MPU — 같은 xQueueSend가 특권 태스크에선 b, 비특권 태스크에선 svc로 커널에 들어간다 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
#define FPGAIO_COUNTER REG32(0x40028018u)              /* 25 MHz, 1 tick = 40 ns */
#define N 256
static QueueHandle_t g_q;                                 /* v2: 포인터가 아니라 "인덱스" */
static TaskHandle_t user_h;
static StackType_t user_stack[512] __attribute__((aligned(2048)));  /* MPU: 크기 정렬 필수 */

static inline uint32_t ctrl(void) { uint32_t c; __asm volatile("mrs %0, control" : "=r"(c)); return c; }

static void bench(QueueHandle_t q, const char *who)
{
    uint32_t v = 0, t0, t1; volatile UBaseType_t s = 0;
    t0 = FPGAIO_COUNTER; for (int i = 0; i < N; i++) s += uxQueueMessagesWaiting(q); t1 = FPGAIO_COUNTER;
    board_printf_raw("[%s] uxQueueMessagesWaiting  : %u ns/call\n", who, (t1 - t0) * 40u / N);
    t0 = FPGAIO_COUNTER; for (int i = 0; i < N; i++) { v = i; xQueueSend(q, &v, 0); } t1 = FPGAIO_COUNTER;
    board_printf_raw("[%s] xQueueSend (no waiter)  : %u ns/call\n", who, (t1 - t0) * 40u / N);
    t0 = FPGAIO_COUNTER; for (int i = 0; i < N; i++) xQueueReceive(q, &v, 0); t1 = FPGAIO_COUNTER;
    board_printf_raw("[%s] xQueueReceive (has item): %u ns/call\n", who, (t1 - t0) * 40u / N);
}

__attribute__((weak)) void after_bench(void) { }

static void user_task(void *arg)                         /* 비특권(restricted) 태스크 */
{
    QueueHandle_t q = (QueueHandle_t)arg;               /* 전역 g_q는 못 읽는다 → 인자로 받는다 */
    uint32_t v = 42;
    board_printf_raw("[user] CONTROL=0x%x nPRIV=%u  q handle=0x%x\n", ctrl(), ctrl() & 1u, (unsigned)(uintptr_t)q);
    BaseType_t r = xQueueSend(q, &v, 0);
    board_printf_raw("[user] xQueueSend before grant -> %d\n", (int)r);
    vTaskDelay(2);                                        /* 그동안 priv 태스크가 권한을 준다 */
    r = xQueueSend(q, &v, 0);
    board_printf_raw("[user] xQueueSend after grant  -> %d (msgs=%u)\n", (int)r, (unsigned)uxQueueMessagesWaiting(q));
    xQueueReceive(q, &v, 0);
    bench(q, "user");
    after_bench();                                        /* 예제 9에서 금지된 접근을 넣는 자리 */
    vTaskSuspend(NULL);
}

static void priv_task(void *arg)                         /* 특권 태스크 (portPRIVILEGE_BIT) */
{
    (void)arg;
    board_printf_raw("[priv] CONTROL=0x%x nPRIV=%u\n", ctrl(), ctrl() & 1u);
#if configUSE_MPU_WRAPPERS_V1 == 0
    vGrantAccessToQueue(user_h, g_q);                     /* v2 ACL: 이 큐를 user에게 허락 */
    board_printf_raw("[priv] granted queue to user\n");
#endif
    vTaskDelay(100);
    bench(g_q, "priv");
    board_exit(0);
}

int main(void)
{
    g_q = xQueueCreate(N, sizeof(uint32_t));
    TaskParameters_t p = {
        .pvTaskCode = user_task, .pcName = "user", .usStackDepth = 512, .pvParameters = (void *)g_q,
        .uxPriority = 3, .puxStackBuffer = user_stack,
        .xRegions = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } },
    };
    xTaskCreateRestricted(&p, &user_h);
    xTaskCreate(priv_task, "priv", 512, NULL, 2 | portPRIVILEGE_BIT, NULL);
    board_printf_raw("[main] handles: q=0x%x user_h=0x%x\n", (unsigned)(uintptr_t)g_q, (unsigned)(uintptr_t)user_h);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /Users/donh/.rtos_scratch/C8/mpu/Makefile >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C8/mpu/app.elf'
```

```text
[main] handles: q=0x1 user_h=0x2
[user] CONTROL=0x3 nPRIV=1  q handle=0x1
[user] xQueueSend before grant -> 0
[priv] CONTROL=0x2 nPRIV=0
[priv] granted queue to user
[user] xQueueSend after grant  -> 1 (msgs=1)
[user] uxQueueMessagesWaiting  : 8203 ns/call
[user] xQueueSend (no waiter)  : 13233 ns/call
[user] xQueueReceive (has item): 12209 ns/call
[priv] uxQueueMessagesWaiting  : 2496 ns/call
[priv] xQueueSend (no waiter)  : 6891 ns/call
[priv] xQueueReceive (has item): 5862 ns/call
[board_exit] code=0
```

출력에서 볼 것:

- **핸들이 1, 2다.** 큐 핸들 `q=0x1`, 태스크 핸들 `user_h=0x2`. v2는 `xQueueCreate`가 만든 진짜 `Queue_t *`를 커널 객체 풀(`mpu_wrappers_v2.c`의 `xKernelObjectPool[]`)에 넣고 그 칸 번호를 돌려준다. 비특권 코드는 커널 객체의 주소를 끝내 모른다.
- `user`는 `CONTROL=0x3`(nPRIV=1, PSP), `priv`는 `CONTROL=0x2`(nPRIV=0, PSP)다.
- **ACL**: 권한을 받기 전 `xQueueSend`는 0(`pdFAIL`)이다. 예외도 없이 조용히 실패한다 — `MPU_xQueueGenericSendImpl`이 `xPortIsAuthorizedToAccessKernelObject()`에서 걸러 낸다. `priv_task`가 `vGrantAccessToQueue(user_h, g_q)`를 부른 뒤에는 1(`pdPASS`)이다. 스케줄러가 시작되기 전에는 모든 접근이 허용된다(`port.c` `xPortIsAuthorizedToAccessKernelObject`의 `xSchedulerRunning == pdFALSE` 분기).
- restricted 태스크는 **전역 변수 `g_q`조차 읽을 수 없다**(`.bss`는 이 태스크의 리전에 없다). 그래서 핸들을 `pvParameters`로 넘겼다. 처음 작성할 때는 `user_task`가 전역 `q`를 바로 읽게 짰다가 `[MemManage] ... MMFAR=0x20009004`로 죽었다 — 0x20009004가 바로 그 전역 변수 주소였다. restricted 태스크를 처음 만들면 거의 누구나 이 벽에 부딪힌다.
- **비용**: 같은 `xQueueSend`가 특권 태스크에서는 6891 ns, 비특권에서는 13233 ns다. 기본 FreeRTOS(예제 1)는 3878 ns였다. 특권 경로도 기본보다 3 us 비싼 이유는 특권 태스크라도 `MPU_xQueueGenericSendImpl`(인덱스 → 포인터 변환, 버퍼 검사)을 거치기 때문이다. 비특권 경로의 추가 6.3 us(약 200 명령)가 SVC 진입·스택 교체·SVC 퇴장 비용이다.

같은 소스를 v1 래퍼로도 빌드해서 비교했다(`WRAP=v1`, `configUSE_MPU_WRAPPERS_V1=1`, ACL 없음).

```sh
limactl shell elinux -- bash -lc 'make -s -B -f /Users/donh/.rtos_scratch/C8/mpu/Makefile WRAP=v1 OUT=/var/tmp/rtos/C8/mpuv1 EXTRA_CFLAGS=-DconfigUSE_MPU_WRAPPERS_V1=1 >/dev/null; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C8/mpuv1/app.elf'
```

```text
[main] handles: q=0x200002e0 user_h=0x20000740
[user] CONTROL=0x3 nPRIV=1  q handle=0x200002e0
[user] xQueueSend before grant -> 1
[priv] CONTROL=0x2 nPRIV=0
[user] xQueueSend after grant  -> 1 (msgs=2)
[user] uxQueueMessagesWaiting  : 2400 ns/call
[user] xQueueSend (no waiter)  : 5152 ns/call
[user] xQueueReceive (has item): 4326 ns/call
[priv] uxQueueMessagesWaiting  : 1472 ns/call
[priv] xQueueSend (no waiter)  : 4230 ns/call
[priv] xQueueReceive (has item): 3398 ns/call
[board_exit] code=0
```

출력에서 볼 것: v1에서는 핸들이 **진짜 RAM 주소**(0x200002e0)이고 ACL이 없어 "권한을 받기 전"에도 송신이 성공한다. 비특권 `xQueueSend`는 5152 ns로 v2(13233 ns)의 약 40%다. v1이 싼 이유는 검사를 안 하기 때문이고, 그게 곧 v1의 보안 구멍이다. **보호의 강도와 진입 비용이 정확히 맞바꿈**이라는 걸 숫자로 볼 수 있다.

### 3.7 예제 7 — SVC 경로를 디스어셈블과 gdb로 한 단계씩 따라간다

무엇을 확인하나: 래퍼가 CONTROL을 보고 갈림길을 고르는 코드, SVC 핸들러가 번호로 분기하는 코드, 그리고 실제 실행 중 각 단계의 IPSR·CONTROL·스택 포인터.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/C8/mpu;
  arm-none-eabi-objdump -d --no-show-raw-insn --start-address=0x10208 --stop-address=0x1021c app.elf | tail -n +7;
  arm-none-eabi-objdump -d --no-show-raw-insn app.elf | awk "/<SVC_Handler>:/,/^$/";
  arm-none-eabi-objdump -d --no-show-raw-insn app.elf | awk "/<vRequestSystemCallExit>:/,/^$/";
  grep -n "SYSTEM_CALL_xQueueGenericSend \|NUM_SYSTEM_CALLS" /var/rtoslab/src/FreeRTOS-Kernel/include/mpu_syscall_numbers.h'
```

```asm
00010208 <MPU_xQueueGenericSend>:
   10208:	push	{r0}
   1020a:	mrs	r0, CONTROL
   1020e:	tst.w	r0, #1
   10212:	pop	{r0}
   10214:	bne.n	1021a <MPU_xQueueGenericSend_Unpriv>

00010216 <MPU_xQueueGenericSend_Priv>:
   10216:	b.w	6b44 <MPU_xQueueGenericSendImpl>

0001021a <MPU_xQueueGenericSend_Unpriv>:
   1021a:	svc	33	@ 0x21

0000581c <SVC_Handler>:
    581c:	tst.w	lr, #4
    5820:	ite	eq
    5822:	mrseq	r0, MSP
    5826:	mrsne	r0, PSP
    582a:	ldr	r2, [r0, #24]
    582c:	ldrb.w	r1, [r2, #-2]
    5830:	cmp	r1, #70	@ 0x46
    5832:	blt.w	5c60 <vSystemCallEnter>
    5836:	cmp	r1, #103	@ 0x67
    5838:	beq.w	5d10 <vSystemCallExit>
    583c:	b.w	5be4 <vSVCHandler_C>

000058b0 <vRequestSystemCallExit>:
    58b0:	svc	103	@ 0x67
    58b2:	nop
66:#define SYSTEM_CALL_xQueueGenericSend                      33
103:#define NUM_SYSTEM_CALLS                                   70  /* Total number of system calls. */
```

출력에서 볼 것:

- 래퍼는 5개 명령으로 **"지금 내가 특권인가"**를 본다(`mrs r0, CONTROL; tst #1`). 특권이면 `b MPU_xQueueGenericSendImpl`(함수 점프, 예외 없음), 비특권이면 `svc 33`. 인자 r0–r3는 건드리지 않으니 그대로 예외 프레임에 실려 간다. 래퍼는 `naked` 함수라 프롤로그도 없다.
- `SVC_Handler`는 쌓인 PC(`[r0, #24]`)의 2바이트 앞에서 `svc` 명령의 imm8을 꺼내(`ldrb r1, [r2, #-2]`) 번호로 세 갈래로 나눈다: 70(`NUM_SYSTEM_CALLS`) 미만 = 시스템 콜 진입, 103 = 시스템 콜 퇴장, 나머지(100 스케줄러 시작, 101 yield) = `vSVCHandler_C`.
- 시스템 콜이 끝나면 `vRequestSystemCallExit`의 `svc 103`이 퇴장 예외를 낸다. **v2는 호출 한 번에 SVC 예외가 두 번** 난다(들어갈 때 1번, 나올 때 1번).

이제 실행 중에 따라가 본다. QEMU를 `-s -S`로 띄우고 gdb 스크립트로 단계마다 멈춘다. QEMU gdbstub는 M-profile 시스템 레지스터(`$control`, `$msp`, `$psp`)를 보여 준다.

```text
set pagination off
set confirm off
target remote :1234
tbreak user_task
continue
tbreak MPU_xQueueGenericSend
continue
printf "1) wrapper  : pc=%#x control=%#x psp=%#x\n", $pc, $control, $psp
tbreak vSystemCallEnter
continue
printf "2) SVC entry: ipsr=%d svc#=%d control=%#x msp=%#x\n", $xpsr & 0x1ff, $r1, $control, $msp
tbreak MPU_xQueueGenericSendImpl
continue
printf "3) Impl     : ipsr=%d control=%#x psp=%#x  xQueue(index)=%d\n", $xpsr & 0x1ff, $control, $psp, $r0
bt
tbreak vSystemCallExit
continue
printf "4) SVC exit : ipsr=%d svc#=%d control=%#x\n", $xpsr & 0x1ff, *(unsigned char *)(((unsigned *)$r0)[6]-2), $control
tbreak main.c:33
continue
printf "5) back in user_task: ipsr=%d control=%#x r0(ret)=%d\n", $xpsr & 0x1ff, $control, r
kill
quit
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/C8/mpu; (timeout 20 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial file:/var/tmp/rtos/C8/mpu/gdb_uart_trace.txt -semihosting-config enable=on,target=native -s -S -kernel app.elf &); sleep 1;
  gdb -q -batch -x /Users/donh/.rtos_scratch/C8/gdb/trace.gdb app.elf 2>&1 | grep -v "^warning\|^$"'
```

```text
Temporary breakpoint 2, MPU_xQueueGenericSend (xQueue=xQueue@entry=0x1 <vApplicationStackOverflowHook>, pvItemToQueue=pvItemToQueue@entry=0x20008fe4 <user_stack+2020>, xTicksToWait=xTicksToWait@entry=0, xCopyPosition=xCopyPosition@entry=0) at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3_MPU/mpu_wrappers_v2_asm.c:943
1) wrapper  : pc=0x10208 control=0x3 psp=0x20008fe0
Temporary breakpoint 3, vSystemCallEnter (pulTaskStack=0x20008fc0 <user_stack+1984>, ucSystemCallNumber=33 '!') at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3_MPU/port.c:517
2) SVC entry: ipsr=11 svc#=33 control=0x1 msp=0x20400000
Temporary breakpoint 4, MPU_xQueueGenericSendImpl (xQueue=0x1 <MPU_xEventGroupSetBitsFromISR>, pvItemToQueue=0x20008fe4 <user_stack+2020>, xTicksToWait=0, xCopyPosition=0) at /var/rtoslab/src/FreeRTOS-Kernel/portable/Common/mpu_wrappers_v2.c:2187
3) Impl     : ipsr=0 control=0x2 psp=0x200009e0  xQueue(index)=1
#0  MPU_xQueueGenericSendImpl (xQueue=0x1 <MPU_xEventGroupSetBitsFromISR>, pvItemToQueue=0x20008fe4 <user_stack+2020>, xTicksToWait=0, xCopyPosition=0) at /var/rtoslab/src/FreeRTOS-Kernel/portable/Common/mpu_wrappers_v2.c:2187
#1  0x000058b0 in prvRestoreContextOfFirstTask () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3_MPU/port.c:710
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
Temporary breakpoint 5, vSystemCallExit (pulSystemCallStack=0x200009c0 <ucHeap+1772>) at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3_MPU/port.c:630
4) SVC exit : ipsr=11 svc#=103 control=0
Temporary breakpoint 6, user_task (arg=0x1 <MPU_xEventGroupSetBitsFromISR>) at /Users/donh/.rtos_scratch/C8/mpu/main.c:33
5) back in user_task: ipsr=0 control=0x3 r0(ret)=0
```

(앞쪽의 Reset_Handler·`user_task` 진입 줄은 생략했다.) 다섯 줄이 이 노트의 핵심 증거다.

1. **래퍼**: Thread 모드, `CONTROL=0x3`(비특권, PSP). PSP는 restricted 태스크의 스택(`user_stack`, 0x20008800–0x20008FFF) 안이다.
2. **SVC 진입**: `IPSR=11`(SVCall 예외 번호), 번호 33. Handler 모드라 이 순간 MSP를 쓴다. `vSystemCallEnter`는 (a) SVC를 낸 PC가 `freertos_system_calls` 안인지, (b) 이미 시스템 콜 중이 아닌지, (c) 그 API가 설정에서 켜져 있는지 확인하고, 하드웨어 프레임 8워드를 **TCB 안의 syscall 스택으로 복사**한 뒤 PSP를 그쪽으로 바꾸고, CONTROL.nPRIV를 0으로 만들고, 쌓인 PC를 `MPU_xQueueGenericSendImpl`, 쌓인 LR을 `vRequestSystemCallExit`로 고쳐 쓴다(`port.c` 491–590행).
3. **Impl**: `IPSR=0` — **Handler 모드가 아니라 Thread 모드**로 돌아와 있다. `CONTROL=0x2` — 이제 특권이다. PSP=0x200009e0은 `ucHeap` 안, 즉 힙에서 할당된 이 태스크 TCB의 `xSystemCallStackInfo.ulSystemCallStackBuffer`다. 백트레이스의 #1 = 0x58b0은 `vRequestSystemCallExit`(LR을 거기로 고쳐 써 두었으니 Impl이 `return`하면 그리로 간다; gdb가 naked 함수의 디버그 정보 때문에 이름을 `prvRestoreContextOfFirstTask`로 잘못 붙였다).
4. **SVC 퇴장**: 다시 `IPSR=11`, 번호 103. `vSystemCallExit`는 PC가 `privileged_functions` 안인지 확인하고, 프레임을 태스크 스택으로 되돌려 복사하고, nPRIV를 1로 되돌리고, PC를 "래퍼를 부른 곳"(진입 때 저장한 LR)으로 고친다.
5. **복귀**: 다시 Thread 모드, `CONTROL=0x3`, 반환값 0(권한 받기 전이라 `pdFAIL`).

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="110" y="20" font-size="13" text-anchor="middle" font-weight="bold">restricted 태스크</text>
<text x="110" y="37" font-size="12" text-anchor="middle">Thread · 비특권 · PSP=태스크 스택</text>
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">SVC 핸들러</text>
<text x="340" y="37" font-size="12" text-anchor="middle">Handler · 특권 · MSP · IPSR=11</text>
<text x="570" y="20" font-size="13" text-anchor="middle" font-weight="bold">시스템 콜 실행</text>
<text x="570" y="37" font-size="12" text-anchor="middle">Thread · 특권 · PSP=syscall 스택</text>
<line x1="110" y1="46" x2="110" y2="420" stroke="#888" stroke-dasharray="3,4"/>
<line x1="340" y1="46" x2="340" y2="420" stroke="#888" stroke-dasharray="3,4"/>
<line x1="570" y1="46" x2="570" y2="420" stroke="#888" stroke-dasharray="3,4"/>
<rect x="10" y="56" width="200" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="76" font-size="12" text-anchor="middle">① MPU_xQueueGenericSend</text>
<text x="110" y="96" font-size="12" text-anchor="middle">CONTROL=0x3 → svc 33</text>
<line x1="210" y1="96" x2="238" y2="124" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<rect x="240" y="116" width="200" height="66" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="136" font-size="12" text-anchor="middle">② vSystemCallEnter</text>
<text x="340" y="154" font-size="12" text-anchor="middle">PC 위치 · 재진입 · 번호 검사</text>
<text x="340" y="172" font-size="12" text-anchor="middle">프레임 복사 · nPRIV=0</text>
<line x1="440" y1="174" x2="468" y2="202" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<rect x="470" y="194" width="200" height="66" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="570" y="214" font-size="12" text-anchor="middle">③ ...GenericSendImpl</text>
<text x="570" y="232" font-size="12" text-anchor="middle">인덱스 · ACL · 버퍼 검사</text>
<text x="570" y="250" font-size="12" text-anchor="middle">→ 진짜 xQueueGenericSend</text>
<line x1="470" y1="252" x2="442" y2="280" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<text x="500" y="282" font-size="12">return → svc 103</text>
<rect x="240" y="272" width="200" height="66" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="292" font-size="12" text-anchor="middle">④ vSystemCallExit</text>
<text x="340" y="310" font-size="12" text-anchor="middle">프레임 → 태스크 스택</text>
<text x="340" y="328" font-size="12" text-anchor="middle">nPRIV=1, PC=원래 LR</text>
<line x1="240" y1="330" x2="212" y2="358" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<rect x="10" y="350" width="200" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="370" font-size="12" text-anchor="middle">⑤ 래퍼를 부른 다음 줄</text>
<text x="110" y="390" font-size="12" text-anchor="middle">CONTROL=0x3, r0=결과</text>
<text x="450" y="390" font-size="12" text-anchor="middle">비용: 기본 3.9 us → 비특권 13.2 us</text>
<text x="450" y="408" font-size="12" text-anchor="middle">(예외 2번 + 프레임 복사 2번 + 검사)</text>
</svg>
```

그림 3 — FreeRTOS-MPU v2에서 비특권 `xQueueSend` 한 번의 경로. 원 번호는 gdb 출력의 단계 번호와 같다. 진짜 커널 일(③)은 Handler 모드가 아니라 특권 Thread 모드에서 시스템 콜 전용 스택 위에서 돈다 — 그래서 시스템 콜 도중에도 인터럽트가 들어오고 컨텍스트 스위치가 될 수 있다.

### 3.8 예제 8 — 이 순간의 MPU 리전을 직접 읽는다

무엇을 확인하나: restricted 태스크가 돌 때 MPU 리전 8개가 실제로 어떻게 잡혀 있는지. 처음에는 `user_task`에서 멈춰 MPU 레지스터(0xE000ED94…)를 읽으려 했는데 gdb가 `Cannot access memory at address 0xe000ed94`로 실패했다. QEMU gdbstub는 **현재 CPU의 특권으로** 메모리를 읽기 때문에, 비특권 Thread 모드에서 멈추면 특권 전용인 시스템 제어 공간(PPB)도 못 읽는다. 그래서 첫 시스템 콜의 SVC 핸들러(특권, 하지만 리전은 아직 그 태스크 것)에서 멈춰 읽었다.

```text
set pagination off
set confirm off
target remote :1234
tbreak user_task
continue
tbreak vSystemCallEnter
continue
printf "MPU_CTRL=%#x  (user_task first syscall, handler mode, CONTROL=%#x)\n", *(unsigned*)0xE000ED94, $control
set $i = 0
while $i < 8
  set *(unsigned*)0xE000ED98 = $i
  set $rbar = *(unsigned*)0xE000ED9C
  set $rasr = *(unsigned*)0xE000EDA0
  printf "region %d: base=0x%08x size=2^%-2d AP=%d XN=%d EN=%d\n", $i, $rbar & 0xffffffe0, (($rasr>>1)&0x1f)+1, ($rasr>>24)&7, ($rasr>>28)&1, $rasr&1
  set $i = $i + 1
end
kill
quit
```

```text
MPU_CTRL=0x5  (user_task first syscall, handler mode, CONTROL=0x1)
region 0: base=0x00000000 size=2^1  AP=0 XN=0 EN=0
region 1: base=0x00000000 size=2^1  AP=0 XN=0 EN=0
region 2: base=0x00000000 size=2^1  AP=0 XN=0 EN=0
region 3: base=0x20008800 size=2^11 AP=3 XN=1 EN=1
region 4: base=0x40000000 size=2^29 AP=3 XN=1 EN=1
region 5: base=0x00000000 size=2^22 AP=6 XN=0 EN=1
region 6: base=0x00000000 size=2^16 AP=5 XN=0 EN=1
region 7: base=0x20000000 size=2^15 AP=1 XN=1 EN=1
```

출력에서 볼 것: `MPU_CTRL=0x5` = ENABLE + PRIVDEFENA. AP(접근 권한) 값은 v7-M 규칙으로 1 = 특권 RW/비특권 없음, 3 = 둘 다 RW, 5 = 특권 RO/비특권 없음, 6 = 둘 다 RO다(F4). 번호가 큰 리전이 겹치는 부분에서 이긴다. 리전 0–2는 이 태스크가 `xRegions`로 아무것도 안 줬으니 꺼져 있다. 리전 3(스택 2 KB = `user_stack` 512워드)만이 이 태스크가 쓸 수 있는 RAM이다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<text x="170" y="20" font-size="13" text-anchor="middle" font-weight="bold">FLASH 0x00000000 (리전 5: 4 MB 모두 RO)</text>
<text x="500" y="20" font-size="13" text-anchor="middle" font-weight="bold">RAM 0x20000000</text>
<rect x="20" y="34" width="300" height="80" rx="3" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="30" y="54" font-size="12">0x00000 – 0x0FFFF  privileged_functions</text>
<text x="30" y="72" font-size="12">벡터 · 커널 · port · Impl (약 33 KB 사용)</text>
<text x="30" y="90" font-size="12">리전 6: 특권 RO, 비특권 접근 불가</text>
<text x="30" y="106" font-size="12">→ 비특권이 실행하면 IACCVIOL</text>
<rect x="20" y="122" width="300" height="56" rx="3" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="142" font-size="12">0x10000 – 0x1052F  freertos_system_calls</text>
<text x="30" y="160" font-size="12">MPU_* 래퍼 (svc 를 내도 되는 유일한 곳)</text>
<rect x="20" y="186" width="300" height="56" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="206" font-size="12">0x10530 –  .text / .rodata</text>
<text x="30" y="224" font-size="12">앱 코드 · board.c (모두 읽기 · 실행)</text>
<rect x="20" y="250" width="300" height="40" rx="3" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30" y="275" font-size="12">… 0x3FFFFF  (privileged_data 초기값 LMA)</text>
<rect x="340" y="34" width="320" height="80" rx="3" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="350" y="54" font-size="12">0x20000000 – 0x20007FFF  privileged_data</text>
<text x="350" y="72" font-size="12">pxCurrentTCB · ucHeap(TCB · 큐) · 핸들 풀</text>
<text x="350" y="90" font-size="12">리전 7: 특권 RW + XN, 비특권 접근 불가</text>
<text x="350" y="106" font-size="12">→ 비특권이 읽으면 DACCVIOL</text>
<rect x="340" y="122" width="320" height="40" rx="3" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="350" y="146" font-size="12">0x20008000  .data (리전 없음 → 비특권 불가)</text>
<rect x="340" y="170" width="320" height="56" rx="3" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="350" y="190" font-size="12">0x20008800 – 0x20008FFF  user_stack</text>
<text x="350" y="208" font-size="12">리전 3: 모두 RW + XN (유일한 쓰기 가능 RAM)</text>
<rect x="340" y="234" width="320" height="56" rx="3" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="350" y="254" font-size="12">0x20009000 –  .bss 나머지 (g_q, app_counter)</text>
<text x="350" y="272" font-size="12">리전 없음 → 비특권 접근 불가</text>
<rect x="20" y="310" width="640" height="40" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="335" font-size="12" text-anchor="middle">0x40000000 – 0x5FFFFFFF 주변장치 · 리전 4: 모두 RW + XN (UART · FPGAIO 카운터 포함)</text>
<text x="340" y="374" font-size="12" text-anchor="middle">특권 코드는 PRIVDEFENA 덕분에 리전이 없는 곳도 기본 맵으로 접근한다</text>
<text x="340" y="392" font-size="12" text-anchor="middle">SCS(0xE000E000~)는 리전과 무관하게 항상 특권 전용</text>
</svg>
```

그림 4 — 예제 8에서 읽은 실제 리전으로 그린 메모리 맵(비례 아님). 빨강 = 비특권 접근 불가인 커널 영역, 파랑 = restricted 태스크의 유일한 RAM, 회색 = 리전이 없어 비특권이 못 가는 곳. 리전 5(FLASH 전체 RO) 위에 리전 6이 겹쳐서 커널 코드만 특권 전용이 된다.

### 3.9 예제 9 — 비특권 태스크가 하면 안 되는 일 6가지를 실제로 해 본다

무엇을 확인하나: 보호 장치 하나하나가 실제로 막는지, 막는다면 어떤 방식(MemManage 폴트 vs 조용한 거절)으로 막는지. 예제 6의 `after_bench()` 자리에 아래 코드를 링크해서 `BAD=1..6`으로 하나씩 빌드한다. MemManage 핸들러(`fault.c`)는 CFSR·MMFAR·쌓인 PC를 찍고 종료한다.

```c
/* C8: MemManage 핸들러 — 어느 스택에서 왔는지 보고 CFSR/MMFAR/PC 출력 후 QEMU 종료 */
#include <stdint.h>
#include "board.h"
void mm_report(uint32_t *frame, uint32_t exc_lr)
{
    uint32_t cfsr = REG32(0xE000ED28u), mmfar = REG32(0xE000ED34u), ctrl;
    __asm volatile("mrs %0, control" : "=r"(ctrl));
    board_printf_raw("\n[MemManage] PC=0x%x LR=0x%x CFSR=0x%x MMFAR=0x%x (MMARVALID=%u DACCVIOL=%u IACCVIOL=%u) "
                     "from %s, thread nPRIV=%u\n", frame[6], frame[5], cfsr, mmfar, (cfsr >> 7) & 1u, (cfsr >> 1) & 1u,
                     cfsr & 1u, (exc_lr & 4u) ? "PSP" : "MSP", ctrl & 1u);
    board_exit(3);
}
__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile(" tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n mov r1, lr\n b mm_report\n");
}
```

```c
/* C8 예제 9: 비특권(restricted) 태스크가 하면 안 되는 일 6가지 — BAD=1..6 로 하나씩 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
uint32_t app_counter;                                       /* 보통 전역(.bss) — 태스크 리전에 없음 */
extern void * volatile pxCurrentTCB;                        /* tasks.c의 커널 변수 (privileged_data) */
extern BaseType_t real_send(QueueHandle_t, const void *, TickType_t, BaseType_t) __asm__("xQueueGenericSend");

void after_bench(void)
{
#if BAD == 1
    board_printf_raw("[user] BAD1: app_counter++ (global not in my regions)\n");
    app_counter++;
#elif BAD == 2
    board_printf_raw("[user] BAD2: read pxCurrentTCB (kernel data)\n");
    board_printf_raw("pxCurrentTCB=%x\n", (unsigned)(uintptr_t)pxCurrentTCB);
#elif BAD == 3
    board_printf_raw("[user] BAD3: raw 'svc 33' from app code (outside freertos_system_calls)\n");
    register uint32_t r0 __asm__("r0") = 1;                 /* 큐 인덱스 1 (printf 뒤에 설정!) */
    register uint32_t r1 __asm__("r1") = 0, r2 __asm__("r2") = 0, r3 __asm__("r3") = 0;
    __asm volatile("svc 33" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3) : "memory");
    uint32_t ret = r0;
    board_printf_raw("[user] BAD3: returned r0=%u -> request ignored, still unprivileged CONTROL bit0=%u\n",
                     ret, (unsigned)({ uint32_t c; __asm volatile("mrs %0, control" : "=r"(c)); c; }) & 1u);
#elif BAD == 4
    uint32_t v = 7;
    board_printf_raw("[user] BAD4: call kernel's xQueueGenericSend directly (privileged flash)\n");
    real_send((QueueHandle_t)1, &v, 0, 0);
#elif BAD == 5
    board_printf_raw("[user] BAD5: uxQueueMessagesWaiting(0x20000100 raw pointer) -> %u (index check fails)\n",
                     (unsigned)uxQueueMessagesWaiting((QueueHandle_t)0x20000100));
#elif BAD == 6
    uint32_t c;
    __asm volatile("mrs %0, control\n bic %0, %0, #1\n msr control, %0\n isb\n mrs %0, control" : "=r"(c) :: "memory");
    board_printf_raw("[user] BAD6: msr CONTROL with nPRIV=0 from user -> CONTROL=0x%x (write ignored)\n", c);
#endif
}
```

```sh
#!/bin/bash
# C8 예제 9 실행: BAD=1..6 변형을 각각 빌드·실행
for b in 1 2 3 4 5 6; do
  make -s -f /Users/donh/.rtos_scratch/C8/mpu/Makefile OUT=/var/tmp/rtos/C8/bad$b \
    SRCS="/Users/donh/.rtos_scratch/C8/mpu/main.c /Users/donh/.rtos_scratch/C8/mpu/kstatic.c /Users/donh/.rtos_scratch/C8/mpu/fault.c /Users/donh/.rtos_scratch/C8/mpu/bad.c" \
    EXTRA_CFLAGS=-DBAD=$b > /dev/null || exit 1
  echo "=== BAD=$b"; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/C8/bad$b/app.elf | grep -E "BAD|MemManage|board_exit|pxCurrent"
done
```

```text
=== BAD=1
[user] BAD1: app_counter++ (global not in my regions)
[MemManage] PC=0x107f6 LR=0x107f5 CFSR=0x82 MMFAR=0x2000aa28 (MMARVALID=1 DACCVIOL=1 IACCVIOL=0) from PSP, thread nPRIV=1
[board_exit] code=3
=== BAD=2
[user] BAD2: read pxCurrentTCB (kernel data)
[MemManage] PC=0x107f8 LR=0x107f5 CFSR=0x82 MMFAR=0x20000144 (MMARVALID=1 DACCVIOL=1 IACCVIOL=0) from PSP, thread nPRIV=1
[board_exit] code=3
=== BAD=3
[user] BAD3: raw 'svc 33' from app code (outside freertos_system_calls)
[user] BAD3: returned r0=1 -> request ignored, still unprivileged CONTROL bit0=1
[board_exit] code=0
=== BAD=4
[user] BAD4: call kernel's xQueueGenericSend directly (privileged flash)
[MemManage] PC=0x2cc8 LR=0x10807 CFSR=0x1 MMFAR=0x0 (MMARVALID=0 DACCVIOL=0 IACCVIOL=1) from PSP, thread nPRIV=1
[board_exit] code=3
=== BAD=5
[user] BAD5: uxQueueMessagesWaiting(0x20000100 raw pointer) -> 0 (index check fails)
[board_exit] code=0
=== BAD=6
[user] BAD6: msr CONTROL with nPRIV=0 from user -> CONTROL=0x3 (write ignored)
[board_exit] code=0
```

| 시도 | 결과 | 막은 장치 | 출력의 증거 |
|---|---|---|---|
| BAD1 앱 전역 쓰기 | MemManage | 리전 없음(배경 맵은 특권만) | `DACCVIOL=1`, `MMFAR=0x2000aa28` = `app_counter` |
| BAD2 커널 변수 읽기 | MemManage | 리전 7 특권 전용 | `MMFAR=0x20000144` = `pxCurrentTCB` (예제 5의 nm) |
| BAD3 앱 코드에서 직접 `svc 33` | 조용히 무시 | `vSystemCallEnter`의 "PC가 syscall 섹션 안인가" 검사 | r0=1 그대로 돌아옴, 여전히 비특권 |
| BAD4 커널 함수 직접 호출 | MemManage | 리전 6 (커널 코드 특권 전용) | `IACCVIOL=1`, `PC=0x2cc8` = `xQueueGenericSend` 첫 명령 |
| BAD5 날 포인터를 핸들로 | 0 반환 | v2 인덱스 검사 `IS_EXTERNAL_INDEX_VALID` | 예제 3(기본 빌드)은 같은 짓에 1234를 돌려줬다 |
| BAD6 CONTROL로 특권 올리기 | 조용히 무시 | 하드웨어 규칙 | `CONTROL=0x3` 그대로 |

BAD3을 만들 때의 함정 하나: 처음에는 `register ... r0 = 1`을 `board_printf_raw` 호출 **앞에** 썼더니 결과가 0으로 나왔다. 디스어셈블해 보니 printf가 r0(포맷 문자열·반환값)를 덮어써서 `svc 33`에 들어갈 때 r0가 이미 0이었다. GCC의 `register asm` 변수는 **asm 피연산자로 쓰이는 순간에만** 그 레지스터를 보장한다. 함수 호출을 사이에 두면 안 된다.

### 3.10 예제 10 — 보호의 메모리 비용: 코드 크기와 TCB 크기

무엇을 확인하나: 같은 커널 소스를 일반 포트와 MPU 포트로 컴파일했을 때 파일별 코드 크기(`-Os`), 그리고 TCB 하나의 크기.

```sh
#!/bin/bash
# C8 예제 10: 같은 커널 소스를 일반 포트 / MPU 포트로 각각 컴파일해 파일별 코드 크기(text)를 비교
K=/var/rtoslab/src/FreeRTOS-Kernel; O=/var/tmp/rtos/C8/ex3; mkdir -p $O/plain $O/mpu
F="-mthumb -mcpu=cortex-m3 -Os -std=gnu11 -ffunction-sections -fdata-sections -I/var/rtoslab/templates/common -I$K/include"
build() { # $1=tag $2=cfgdir $3=portdir, 나머지=소스
  local t=$1 c=$2 p=$3; shift 3
  for s in "$@"; do arm-none-eabi-gcc $F -Wno-unused-parameter -I$c -I$p -c $s -o $O/$t/$(basename $s .c).o || exit 1; done
}
COMMON="$K/tasks.c $K/queue.c $K/list.c $K/timers.c $K/event_groups.c $K/stream_buffer.c"
build plain /var/rtoslab/templates/freertos-mps2 $K/portable/GCC/ARM_CM3 $COMMON $K/portable/GCC/ARM_CM3/port.c
build mpu /Users/donh/.rtos_scratch/C8/mpu $K/portable/GCC/ARM_CM3_MPU $COMMON $K/portable/GCC/ARM_CM3_MPU/port.c \
      $K/portable/GCC/ARM_CM3_MPU/mpu_wrappers_v2_asm.c $K/portable/Common/mpu_wrappers_v2.c
printf "%-22s %8s %8s\n" file plain mpu
for f in tasks queue list timers event_groups stream_buffer port mpu_wrappers_v2_asm mpu_wrappers_v2; do
  a=$(arm-none-eabi-size $O/plain/$f.o 2>/dev/null | awk 'NR==2{print $1}'); b=$(arm-none-eabi-size $O/mpu/$f.o | awk 'NR==2{print $1}')
  printf "%-22s %8s %8s\n" $f.o "${a:--}" "$b"
done
for t in plain mpu; do printf "TOTAL %-6s text=%s\n" $t $(arm-none-eabi-size -t $O/$t/*.o | tail -1 | awk '{print $1}'); done
```

```text
file                      plain      mpu
tasks.o                    9333    10033
queue.o                    3966     3960
list.o                      126      126
timers.o                   1896     1898
event_groups.o             1013     1015
stream_buffer.o            2502     2500
port.o                      918     2220
mpu_wrappers_v2_asm.o         -     1304
mpu_wrappers_v2.o             -     6288
TOTAL plain  text=19754
TOTAL mpu    text=29344
```

```c
/* C8 예제 10b: TCB(StaticTask_t) 크기 — MPU 포트는 TCB 안에 MPU 리전 + syscall 스택 + ACL을 넣는다 */
#include "FreeRTOS.h"
#include "task.h"
char tcb_size[sizeof(StaticTask_t)];
#if portUSING_MPU_WRAPPERS == 1
char mpu_settings_size[sizeof(xMPU_SETTINGS)];
#endif
```

```text
plain:
00000000 00000096 B tcb_size
mpu:
00000788 00000692 B mpu_settings_size
00000000 00000788 B tcb_size
```

출력에서 볼 것: 커널 코드는 19.7 KB → 29.3 KB로 **약 49% 늘었다**. 늘어난 9.6 KB의 대부분이 `mpu_wrappers_v2.c`(6.3 KB, Impl 함수 70개의 검사 코드)와 래퍼(1.3 KB), 포트(+1.3 KB, MPU 설정·syscall 진입/퇴장)다. TCB는 96바이트 → **788바이트**(nm `-t d`, 10진수)로 8배가 됐다. 그중 692바이트가 `xMPU_SETTINGS`인데, 대부분이 `configSYSTEM_CALL_STACK_SIZE` 128워드 = 512바이트 syscall 스택이고 나머지가 리전 레지스터 사본·리전 설정·ACL·문맥 저장 공간이다. 태스크 10개면 RAM 약 7 KB가 늘어난다. 여기에 MPU 정렬 낭비(예제 5의 64 KB 리전, 스택마다 2의 거듭제곱 정렬)가 더해진다.

### 3.11 흔한 함정 (FreeRTOS-MPU)

- restricted 태스크에서 전역 변수를 그냥 읽는다 → MemManage. 공유할 데이터는 `xRegions`로 리전을 주거나 큐로 넘긴다.
- 스택 배열을 크기에 맞게 정렬하지 않는다 → v7-M에서는 리전이 엉뚱하게 잡혀 첫 push에서 폴트. `__attribute__((aligned(스택 바이트 수)))`.
- `privileged_functions` 리전이 모자라 링크 실패(예제 5). 커널 기능을 줄이거나 다음 2의 거듭제곱으로.
- v2에서 비특권 태스크가 `xQueueCreate`·`xTaskCreate`를 부른다 → 금지된 시스템 콜이라 실패. 객체는 특권 코드(초기화 단계)에서 만들고 ACL로 나눠 준다.
- MemManage 핸들러에서 FreeRTOS API를 부른다 → 앱 코드의 API는 `MPU_` 래퍼라 CONTROL.nPRIV=1인 상태(폴트 난 태스크)에서는 `svc`를 내려 하고, Handler 모드에서 SVC는 HardFault로 번진다. 폴트 핸들러는 레지스터만 읽고 끝낸다.
- 핸들을 포인터로 착각해 출력·비교한다. v2에서 `q == 1`은 정상이다.

---
## 4. Zephyr userspace — `__syscall` 한 줄에서 생성되는 시스템 콜

### 4.1 직관과 정의

Zephyr의 기본(`CONFIG_USERSPACE=n`)은 FreeRTOS처럼 라이브러리형이다. `CONFIG_USERSPACE=y`(MPU가 있는 보드)를 켜면 두 종류의 스레드가 생긴다.

- **supervisor 스레드**: 특권으로 도는 보통 스레드. `main()`이 그렇다. 커널 API를 거의 직접 호출한다.
- **user 스레드**: `k_thread_create(..., K_USER, ...)`로 만들거나 `k_thread_user_mode_enter()`로 내려간 비특권 스레드. 커널 객체는 권한을 받은 것만, 메모리는 자기 스택 + 속한 **memory domain**의 파티션만 쓸 수 있다.

FreeRTOS-MPU와 가장 다른 점은 **시스템 콜을 손으로 쓰지 않는다**는 것이다. 헤더에서 API 선언 앞에 `__syscall`을 붙이면, 빌드 때 `scripts/build/gen_syscalls.py`가 진입 인라인·마샬링 함수·디스패치 표를 생성한다. 개발자는 두 함수만 쓴다:

- `z_impl_<이름>()`: 진짜 구현. supervisor에서는 이걸 바로 부른다.
- `z_vrfy_<이름>()`: user에서 왔을 때만 거치는 검증 함수. 객체 종류·권한(`K_SYSCALL_OBJ`), 포인터가 가리키는 메모리를 호출자가 읽고/쓸 수 있는지(`K_SYSCALL_MEMORY_READ/WRITE`)를 검사하고 `z_impl_`을 부른다. 실패하면 `K_OOPS`로 그 스레드에 kernel oops를 낸다.

```c
/* include/zephyr/kernel.h 3755행 (v4.4.2) */
__syscall void k_sem_give(struct k_sem *sem);

/* kernel/sem.c 124행 — 검증 함수는 이게 전부다 */
static inline void z_vrfy_k_sem_give(struct k_sem *sem)
{
	K_OOPS(K_SYSCALL_OBJ(sem, K_OBJ_SEM));
	z_impl_k_sem_give(sem);
}
#include <zephyr/syscalls/k_sem_give_mrsh.c>
```

`K_SYSCALL_OBJ`는 "이 포인터가 정말 커널이 아는 `k_sem` 객체이고, 지금 스레드가 권한 비트를 가졌나"를 본다. 커널이 아는 객체 목록은 빌드 때 `gen_kobject_list.py`가 ELF에서 모든 커널 객체를 찾아 **gperf로 perfect hash 표**를 만든다. 그래서 `CONFIG_USERSPACE` 빌드에는 호스트에 gperf가 필요하다 — 랩 VM에는 없어서 첫 빌드가 `GPERF-NOTFOUND: not found`로 실패했고, GNU gperf 3.1 소스를 `/var/tmp/rtos/C8/gperf`에 직접 빌드해서 `-DGPERF=...`로 넘겼다(apt 설치는 공용 상태라 하지 않았다).

### 4.2 예제 11 — 생성된 코드를 빌드 디렉터리에서 직접 본다

먼저 앱(예제 12의 `main.c`)을 `CONFIG_USERSPACE=y`로 빌드한다. 설정과 빌드 스크립트:

```text
CONFIG_USERSPACE=y
CONFIG_PRINTK=y
CONFIG_LOG=y
CONFIG_LOG_MODE_MINIMAL=y
CONFIG_MAIN_STACK_SIZE=2048
```

```sh
#!/bin/bash
# C8 예제 12·13: Zephyr userspace 빌드 변형 (gperf는 랩에 없어 /var/tmp/rtos/C8/gperf에 직접 빌드)
APP=/Users/donh/.rtos_scratch/C8/zus; B=/var/tmp/rtos/C8
G=-DGPERF=/var/tmp/rtos/C8/gperf/bin/gperf
run() { timeout 8 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
          -icount shift=5,align=off,sleep=off -kernel $1/zephyr/zephyr.elf 2>&1 | tr -d '\r'; }
v=$1
case $v in
  us)   /var/rtoslab/bin/rtos-zbuild mps2/an385 $APP $B/zus    -- $G && run $B/zus ;;
  nous) /var/rtoslab/bin/rtos-zbuild mps2/an385 $APP $B/znous  -- $G -DCONFIG_USERSPACE=n && run $B/znous ;;
  bad*) n=${v#bad}; /var/rtoslab/bin/rtos-zbuild mps2/an385 $APP $B/zbad$n -- $G -DEXTRA_CFLAGS=-DC8_BAD=$n && run $B/zbad$n ;;
esac
```

QEMU는 `rtos-run-mps2`와 같은 `-icount shift=5`로 직접 띄웠다(FreeRTOS 숫자와 같은 시간 기준). 빌드 뒤 생성 파일을 본다.

```sh
limactl shell elinux -- bash -lc 'Z=/var/tmp/rtos/C8/zus/zephyr/include/generated/zephyr;
  grep -n "K_SYSCALL_K_SEM_GIVE\b\|K_SYSCALL_LIMIT\|K_SYSCALL_BAD\b" $Z/syscall_list.h;
  grep -n "k_sem_give" -B2 -A16 $Z/syscalls/kernel.h | sed -n 1,17p;
  cat $Z/syscalls/k_sem_give_mrsh.c;
  grep -n "K_SYSCALL_K_SEM_GIVE\]" $Z/syscall_dispatch.c; ls $Z/syscalls | wc -l'
```

```text
79:#define K_SYSCALL_K_SEM_GIVE 73
165:#define K_SYSCALL_BAD 159
166:#define K_SYSCALL_LIMIT 160
1210-
1211-
1212:extern void z_impl_k_sem_give(struct k_sem * sem);
1213-
1214-__pinned_func
1215:static inline void k_sem_give(struct k_sem * sem)
1216-{
1217-#ifdef CONFIG_USERSPACE
1218-	if (z_syscall_trap()) {
1219-		union { uintptr_t x; struct k_sem * val; } parm0 = { .val = sem };
1220-		(void) arch_syscall_invoke1(parm0.x, K_SYSCALL_K_SEM_GIVE);
1221-		return;
1222-	}
1223-#endif
1224-	compiler_barrier();
1225:	z_impl_k_sem_give(sem);
1226-}
/* auto-generated by gen_syscalls.py, don't edit */

#include <zephyr/syscalls/kernel.h>

extern void z_vrfy_k_sem_give(struct k_sem * sem);
uintptr_t z_mrsh_k_sem_give(uintptr_t arg0, uintptr_t arg1, uintptr_t arg2,
		uintptr_t arg3, uintptr_t arg4, uintptr_t arg5, void *ssf)
{
	_current->syscall_frame = ssf;
	(void) arg1;	/* unused */
	(void) arg2;	/* unused */
	(void) arg3;	/* unused */
	(void) arg4;	/* unused */
	(void) arg5;	/* unused */
	union { uintptr_t x; struct k_sem * val; } parm0;
	parm0.x = arg0;
	z_vrfy_k_sem_give(parm0.val);
	_current->syscall_frame = NULL;
	return 0;
}
763:	[K_SYSCALL_K_SEM_GIVE] = z_mrsh_k_sem_give,
248
```

출력에서 볼 것:

- 이 빌드에는 시스템 콜이 159개(`K_SYSCALL_LIMIT` 160, 마지막 번호 159는 잘못된 번호용 `K_SYSCALL_BAD`)이고, `k_sem_give`는 73번이다. `syscalls/` 아래 생성 헤더가 248개다(서브시스템 헤더마다 하나씩).
- 앱이 부르는 `k_sem_give()`는 **생성된 인라인 함수**다. `z_syscall_trap()`(= user 컨텍스트인가?)이 참이면 `arch_syscall_invoke1(sem, 73)`, 아니면 `z_impl_k_sem_give()` 직접 호출. supervisor 스레드도 이 분기 비용은 낸다.
- `z_mrsh_k_sem_give`(marshal = 인자 정리)는 레지스터로 온 인자 6개 중 필요한 것을 원래 타입으로 되돌리고 `z_vrfy_k_sem_give`를 부른다. `ssf`(syscall stack frame)를 스레드에 기록해 두는 것은 검증 실패 때 oops 보고에 쓰기 위해서다.
- `_k_syscall_table[73] = z_mrsh_k_sem_give`. 번호 → 마샬링 함수 표다. Linux의 `sys_call_table`(EL E6 §1.4)과 같은 모양이다.

### 4.3 예제 12 — supervisor vs user, 그리고 USERSPACE=n: 비용 측정

무엇을 확인하나: 같은 API 네 개의 비용을 (a) `CONFIG_USERSPACE=n`, (b) `=y`의 supervisor 스레드(`main`), (c) `=y`의 user 스레드에서. user 스레드는 FPGAIO 카운터를 읽어야 하므로 카운터 페이지를 "특권 RW / user RO" 파티션으로 만들어 기본 memory domain에 넣었다(`K_MEM_PARTITION_DEFINE`, v7-M이라 4 KB 크기·4 KB 정렬). 세마포어와 메시지 큐는 `k_object_access_grant`로 권한을 준다. `sem_secret`은 일부러 권한을 주지 않는다(예제 13).

```c
/* C8 예제 12·13: Zephyr — 같은 k_sem/k_msgq 호출을 supervisor 스레드와 user 스레드에서 재 본다 */
#include <zephyr/kernel.h>
#include <zephyr/app_memory/mem_domain.h>
#define FPGAIO_COUNTER (*(volatile uint32_t *)0x40028018u)   /* 25 MHz, 1 tick = 40 ns */
#define N 256
K_SEM_DEFINE(sem, 0, 100000);
K_SEM_DEFINE(sem_secret, 0, 1);                              /* user 스레드에 권한을 주지 않는 객체 */
K_MSGQ_DEFINE(mq, sizeof(uint32_t), N, 4);
K_THREAD_STACK_DEFINE(ustack, 2048);
static struct k_thread uthread;
uint32_t kernel_global;                                      /* 파티션 밖 전역 */
#ifdef CONFIG_USERSPACE
K_MEM_PARTITION_DEFINE(fpga_part, 0x40028000, 0x1000, K_MEM_PARTITION_P_RW_U_RO);
#endif

static void bench(const char *who)
{
    uint32_t v = 0, t0, t1; volatile unsigned s = 0;
    t0 = FPGAIO_COUNTER; for (int i = 0; i < N; i++) s += k_sem_count_get(&sem); t1 = FPGAIO_COUNTER;
    printk("[%s] k_sem_count_get        : %u ns/call\n", who, (t1 - t0) * 40u / N);
    t0 = FPGAIO_COUNTER; for (int i = 0; i < N; i++) k_sem_give(&sem); t1 = FPGAIO_COUNTER;
    printk("[%s] k_sem_give (no waiter)  : %u ns/call\n", who, (t1 - t0) * 40u / N);
    t0 = FPGAIO_COUNTER; for (int i = 0; i < N; i++) { v = i; k_msgq_put(&mq, &v, K_NO_WAIT); } t1 = FPGAIO_COUNTER;
    printk("[%s] k_msgq_put (no waiter)  : %u ns/call\n", who, (t1 - t0) * 40u / N);
    t0 = FPGAIO_COUNTER; for (int i = 0; i < N; i++) k_msgq_get(&mq, &v, K_NO_WAIT); t1 = FPGAIO_COUNTER;
    printk("[%s] k_msgq_get (has item)   : %u ns/call\n", who, (t1 - t0) * 40u / N);
}

static void user_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    printk("[user] k_is_user_context()=%d\n", (int)k_is_user_context());
    bench("user");
#if C8_BAD == 1
    printk("[user] BAD1: k_sem_give(&sem_secret) without grant\n");
    k_sem_give(&sem_secret);
#elif C8_BAD == 2
    printk("[user] BAD2: kernel_global++ (not in my memory domain)\n");
    kernel_global++;
#elif C8_BAD == 3
    extern void z_impl_k_sem_give(struct k_sem *s);
    printk("[user] BAD3: call z_impl_k_sem_give() directly (skip SVC)\n");
    z_impl_k_sem_give(&sem);
#endif
    printk("[user] done\n");
}

int main(void)
{
    printk("[main] k_is_user_context()=%d USERSPACE=%d\n", (int)k_is_user_context(), IS_ENABLED(CONFIG_USERSPACE));
    bench("supv");
    k_tid_t t = k_thread_create(&uthread, ustack, K_THREAD_STACK_SIZEOF(ustack), user_fn, NULL, NULL, NULL,
                                5, K_USER, K_FOREVER);
#ifdef CONFIG_USERSPACE
    k_object_access_grant(&sem, t);
    k_object_access_grant(&mq, t);
    k_mem_domain_add_partition(&k_mem_domain_default, &fpga_part);
#endif
    k_thread_start(t);
    k_thread_join(t, K_FOREVER);
    printk("[main] user thread finished\n");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'for v in us nous; do echo "######## $v"; /Users/donh/.rtos_scratch/C8/zus/run_all.sh $v; done'
```

```text
######## us
           FLASH:       47776 B         4 MB      1.14%
             RAM:         16 KB         4 MB      0.39%
*** Booting Zephyr OS build v4.4.2 ***
[main] k_is_user_context()=0 USERSPACE=1
[supv] k_sem_count_get        : 512 ns/call
[supv] k_sem_give (no waiter)  : 960 ns/call
[supv] k_msgq_put (no waiter)  : 2656 ns/call
[supv] k_msgq_get (has item)   : 2304 ns/call
[user] k_is_user_context()=1
[user] k_sem_count_get        : 6368 ns/call
[user] k_sem_give (no waiter)  : 6848 ns/call
[user] k_msgq_put (no waiter)  : 12416 ns/call
[user] k_msgq_get (has item)   : 12096 ns/call
[user] done
[main] user thread finished
qemu-system-arm: terminating on signal 15 from pid 2360640 (timeout)
######## nous
           FLASH:       15080 B         4 MB      0.36%
             RAM:       10392 B         4 MB      0.25%
*** Booting Zephyr OS build v4.4.2 ***
[main] k_is_user_context()=0 USERSPACE=0
[supv] k_sem_count_get        : 192 ns/call
[supv] k_sem_give (no waiter)  : 640 ns/call
[supv] k_msgq_put (no waiter)  : 2336 ns/call
[supv] k_msgq_get (has item)   : 2016 ns/call
[user] k_is_user_context()=0
[user] k_sem_count_get        : 192 ns/call
[user] k_sem_give (no waiter)  : 640 ns/call
[user] k_msgq_put (no waiter)  : 2336 ns/call
[user] k_msgq_get (has item)   : 2016 ns/call
[user] done
[main] user thread finished
qemu-system-arm: terminating on signal 15 from pid 2364620 (timeout)
```

출력에서 볼 것:

- `USERSPACE=n`에서는 "user" 스레드도 `k_is_user_context()=0`이다 — `K_USER`가 무시되고 숫자가 supervisor와 똑같다. 이게 라이브러리형 Zephyr다. FLASH 15 KB.
- `USERSPACE=y`를 켜기만 해도 supervisor 경로가 약간 비싸진다(`k_sem_count_get` 192 → 512 ns, `k_msgq_put` 2336 → 2656 ns, 명령 약 10개). 생성 인라인의 `z_syscall_trap()` 때문이다(다음 디스어셈블). FLASH는 15 KB → 47.8 KB로 3배가 됐다 — syscall 마샬링 159개, 커널 객체 해시 표, MPU·memory domain 코드.
- user 스레드는 `k_sem_count_get` 하나에 6368 ns다. 이 API의 진짜 일은 `sem->count` 한 번 읽기인데, supervisor보다 약 5.9 us(약 180 명령) 비싸다. 이게 **Zephyr 시스템 콜 한 번의 고정 비용**(SVC 진입, 특권 스택 전환, 디스패치, 객체 해시 조회·권한 비트 검사, 복귀)이다. `k_msgq_put`은 여기에 버퍼 읽기 권한 검사(`K_SYSCALL_MEMORY_READ`)가 더해져 12.4 us다.

supervisor 쪽이 왜 조금 비싸졌는지 디스어셈블로 본다.

```sh
limactl shell elinux -- bash -lc 'Z=/var/tmp/rtos/C8/zus/zephyr; D="arm-none-eabi-objdump -d --no-show-raw-insn $Z/zephyr.elf";
  $D | awk "/<bench>:/,/^$/" | sed -n 33,42p; $D | awk "/<bench>:/,/^$/" | grep "z_impl";
  $D | awk "/<arch_is_user_context>:/,/^$/" | head -6; echo "copies: $($D | grep -c "<arch_is_user_context>:")";
  arm-none-eabi-nm $Z/zephyr.elf | grep -E " (z_arm_svc|z_arm_do_syscall|_k_syscall_table|z_mrsh_k_sem_give|z_impl_k_sem_give|z_object_gperf_find)$"'
```

```text
     752:	bl	6512 <arch_is_user_context>
     756:	cmp	r0, #0
     758:	beq.n	7e4 <bench+0xdc>
     75a:	ldr	r0, [pc, #176]	@ (80c <CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE+0xc>)
     75c:	movs	r6, #73	@ 0x49
     75e:	svc	3
     760:	subs	r4, #1
     762:	bne.n	752 <bench+0x4a>
     764:	movs	r3, #40	@ 0x28
     766:	ldr	r6, [pc, #160]	@ (808 <CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE+0x8>)
     7e6:	bl	4000 <z_impl_k_sem_give>
     7f4:	bl	73d2 <z_impl_k_msgq_put>
     802:	bl	3684 <z_impl_k_msgq_get>
00006512 <arch_is_user_context>:
    6512:	mrs	r3, IPSR
    6516:	cbnz	r3, 651c <arch_is_user_context+0xa>
    6518:	b.w	6c06 <z_arm_thread_is_in_user_mode>
    651c:	movs	r0, #0
    651e:	bx	lr
copies: 20
000085d8 R _k_syscall_table
0000116c T z_arm_do_syscall
000017a0 T z_arm_svc
00004000 T z_impl_k_sem_give
00004068 T z_mrsh_k_sem_give
000000c0 T z_object_gperf_find
```

출력에서 볼 것: 루프 한 바퀴마다 `bl arch_is_user_context` → `mrs IPSR`(Handler 모드면 user 아님) → `z_arm_thread_is_in_user_mode`(`mrs CONTROL`, bit0) → 결과가 0이면 `beq`로 `z_impl_k_sem_give` 직접 호출, 1이면 `r6 = 73`, `svc 3`. 즉 **같은 바이너리 코드가 두 경로를 다 갖고 있고, 실행 시점에 CONTROL을 보고 고른다** — FreeRTOS-MPU 래퍼의 `mrs CONTROL; tst #1`과 같은 생각이다. 덤으로, `arch_is_user_context`가 `static inline`인데 인라인되지 않고 번역 단위마다 복사본이 생겼다(출력의 `copies: 20`, 이 빌드는 `CONFIG_SIZE_OPTIMIZATIONS=y` 즉 `-Os`). Zephyr 앱 쪽 파일에 `__ZEPHYR_SUPERVISOR__`를 정의하면 이 검사를 컴파일 시점에 없앨 수 있다(`include/zephyr/syscall.h`의 `z_syscall_trap()`).

### 4.4 Zephyr의 진입 경로 — `svc 3`에서 `z_impl_`까지

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="110" y="20" font-size="13" text-anchor="middle" font-weight="bold">user 스레드</text>
<text x="110" y="37" font-size="12" text-anchor="middle">Thread · 비특권 · PSP=user 스택</text>
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">z_arm_svc</text>
<text x="340" y="37" font-size="12" text-anchor="middle">Handler · 특권 · svc.S</text>
<text x="570" y="20" font-size="13" text-anchor="middle" font-weight="bold">z_arm_do_syscall</text>
<text x="570" y="37" font-size="12" text-anchor="middle">Thread · 특권 · 특권 스택</text>
<line x1="110" y1="46" x2="110" y2="410" stroke="#888" stroke-dasharray="3,4"/>
<line x1="340" y1="46" x2="340" y2="410" stroke="#888" stroke-dasharray="3,4"/>
<line x1="570" y1="46" x2="570" y2="410" stroke="#888" stroke-dasharray="3,4"/>
<rect x="10" y="56" width="200" height="66" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="76" font-size="12" text-anchor="middle">① k_sem_give() 인라인</text>
<text x="110" y="94" font-size="12" text-anchor="middle">z_syscall_trap() = 1</text>
<text x="110" y="112" font-size="12" text-anchor="middle">r0=sem, r6=73 → svc 3</text>
<line x1="210" y1="112" x2="238" y2="136" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<rect x="240" y="128" width="200" height="66" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="148" font-size="12" text-anchor="middle">② svc 번호 3 확인</text>
<text x="340" y="166" font-size="12" text-anchor="middle">프레임 → 특권 스택</text>
<text x="340" y="184" font-size="12" text-anchor="middle">PC를 z_arm_do_syscall로</text>
<line x1="440" y1="184" x2="468" y2="208" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<rect x="470" y="200" width="200" height="66" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="570" y="220" font-size="12" text-anchor="middle">③ r6 &lt; LIMIT 확인</text>
<text x="570" y="238" font-size="12" text-anchor="middle">_k_syscall_table[73]</text>
<text x="570" y="256" font-size="12" text-anchor="middle">→ z_mrsh_k_sem_give</text>
<line x1="570" y1="266" x2="570" y2="282" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<rect x="470" y="284" width="200" height="56" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="570" y="306" font-size="12" text-anchor="middle">④ z_vrfy: 객체 해시 · 권한</text>
<text x="570" y="324" font-size="12" text-anchor="middle">→ z_impl_k_sem_give</text>
<line x1="470" y1="340" x2="212" y2="372" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah5)"/>
<text x="350" y="346" font-size="12" text-anchor="middle">⑤ PSP 복원 · msr CONTROL (nPRIV=1)</text>
<rect x="10" y="356" width="200" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="376" font-size="12" text-anchor="middle">⑥ svc 3 다음 명령</text>
<text x="110" y="396" font-size="12" text-anchor="middle">예외 없이 바로 복귀</text>
<text x="340" y="396" font-size="12" text-anchor="middle">권한 없음 → K_OOPS</text>
</svg>
```

그림 5 — Zephyr userspace의 `k_sem_give` 경로(Cortex-M, `arch/arm/core/cortex_m/svc.S` → `arch/arm/core/userspace.S`). FreeRTOS-MPU v2처럼 SVC 핸들러가 복귀 주소를 고쳐서 **특권 Thread 모드**로 떨어뜨린다. 다른 점은 ⑤ — 퇴장 때 SVC를 한 번 더 내지 않고, 특권 코드가 스스로 `msr CONTROL`로 nPRIV를 1로 쓴다. 검증에 실패하면 ④에서 `K_OOPS`로 그 스레드에 kernel oops를 낸다.

소스로 확인한 단계별 핵심 줄:

- ① `include/zephyr/arch/arm/syscall.h`의 `arch_syscall_invoke1()`: `register r0 = arg1; register r6 = call_id; svc _SVC_CALL_SYSTEM_CALL`(= 3, 같은 파일 23행). FreeRTOS와 달리 **SVC 번호는 항상 3**이고 API 번호는 r6에 실린다(SVC imm8은 8비트라 API 수백 개를 다 담을 수 없다).
- ② `z_arm_svc`: SVC 번호 0(context switch), 1(IRQ offload), 2(runtime except), 3(system call)을 나누고, 3이면 예외 프레임을 스레드의 특권 스택(`CONFIG_PRIVILEGED_STACK_SIZE`=1024, 이 빌드의 `.config`)으로 옮기고 복귀 PC를 `z_arm_do_syscall`로 바꾼다(`svc.S` 175행 이후 주석의 표).
- ③ `z_arm_do_syscall`(`userspace.S` 320행): `r6`이 `K_SYSCALL_BAD`면 그쪽으로, 아니면 `ldr ip, =_k_syscall_table; lsl r6, #2; add ip, r6; ldr ip, [ip]; blx ip`.
- ⑤ 같은 파일 끝부분: `ldr ip, [sp,#12]; msr PSP, ip`(user 스택 복원) → `_current->arch.mode |= 1` → `mrs ip, CONTROL; orrs ip, ip, #1; msr CONTROL, ip`.

### 4.5 예제 13 — user 스레드가 하면 안 되는 일 3가지

무엇을 확인하나: (BAD1) 권한 없는 객체로 syscall, (BAD2) memory domain 밖 전역 변수 쓰기, (BAD3) SVC를 건너뛰고 `z_impl_` 함수를 직접 호출. 예제 12와 같은 소스를 `-DEXTRA_CFLAGS=-DC8_BAD=n`으로 빌드한다(처음엔 매크로 이름을 `BAD`로 했다가 picolibc의 `regex2.h`도 `BAD`를 정의해서 `warning: "BAD" redefined`가 나와 이름을 바꿨다 — 전역 `-D`는 라이브러리 전체에 뿌려진다는 함정).

```sh
limactl shell elinux -- bash -lc 'for v in bad1 bad2 bad3; do echo "######## $v"; /Users/donh/.rtos_scratch/C8/zus/run_all.sh $v | grep -E "BAD|E:|######|warning"; done'
```

```text
######## bad1
[user] BAD1: k_sem_give(&sem_secret) without grant
E: thread 0x20000b70 (0) does not have permission on k_sem 0x20000938
E: permission bitmap
E: 00 00                   |..      
E: syscall z_vrfy_k_sem_give failed check: access denied
E: r0/a1:  0x00000000  r1/a2:  0x00000000  r2/a3:  0x00000000
E: r3/a4:  0x00000000 r12/ip:  0x00000000 r14/lr:  0x00000000
E:  xpsr:  0x00000000
E: Faulting instruction address (r15/pc): 0x00000000
E: >>> ZEPHYR FATAL ERROR 3: Kernel oops on CPU 0
E: Current thread: 0x20000b70 (unknown)
E: Halting system
######## bad2
[user] BAD2: kernel_global++ (not in my memory domain)
E: ***** MPU FAULT *****
E:   Data Access Violation
E:   MMFAR Address: 0x20000dd8
E: r0/a1:  0x0000896f  r1/a2:  0x00000000  r2/a3:  0x20000dd8
E: r3/a4:  0x00000017 r12/ip:  0x00006a73 r14/lr:  0x0000083f
E:  xpsr:  0x61000000
E: Faulting instruction address (r15/pc): 0x00000842
E: >>> ZEPHYR FATAL ERROR 19: Unknown error on CPU 0
E: Current thread: 0x20000b48 (unknown)
E: Halting system
######## bad3
[user] BAD3: call z_impl_k_sem_give() directly (skip SVC)
E: ***** MPU FAULT *****
E:   Data Access Violation
E:   MMFAR Address: 0x20000928
E: r0/a1:  0x20000928  r1/a2:  0x00000000  r2/a3:  0x00000000
E: r3/a4:  0x00000020 r12/ip:  0x00006a73 r14/lr:  0x00000845
E:  xpsr:  0x21000000
E: Faulting instruction address (r15/pc): 0x00004024
E: >>> ZEPHYR FATAL ERROR 19: Unknown error on CPU 0
E: Current thread: 0x20000b48 (unknown)
E: Halting system
```

확인용 심볼 주소:

```sh
limactl shell elinux -- bash -lc 'arm-none-eabi-nm /var/tmp/rtos/C8/zbad2/zephyr/zephyr.elf | grep -E " (kernel_global)$";
  arm-none-eabi-nm /var/tmp/rtos/C8/zbad3/zephyr/zephyr.elf | grep -E " (sem|z_impl_k_sem_give)$";
  arm-none-eabi-nm /var/tmp/rtos/C8/zbad1/zephyr/zephyr.elf | grep -E " (sem_secret)$"'
```

```text
20000dd8 B kernel_global
20000928 D sem
00004014 T z_impl_k_sem_give
20000938 D sem_secret
```

출력에서 볼 것:

- **BAD1**: `z_vrfy_k_sem_give`의 `K_SYSCALL_OBJ`가 권한 비트맵(`00 00` = 아무 스레드도 권한 없음)을 보고 거절 → 권한이 없는 객체 0x20000938 = `sem_secret`, `syscall z_vrfy_k_sem_give failed check: access denied` → `ZEPHYR FATAL ERROR 3: Kernel oops`. 레지스터 덤프가 0인 것은 하드웨어 폴트가 아니라 **소프트웨어가 낸 oops**라서다. FreeRTOS v2(ACL 실패 = 조용히 `pdFAIL` 반환)와 정책이 다르다: Zephyr는 "권한 위반 = 그 스레드의 버그"로 보고 죽인다(기본 fatal 핸들러는 시스템 정지, `k_sys_fatal_error_handler`를 앱이 재정의하면 스레드만 정리할 수 있다).
- **BAD2**: 하드웨어 MPU 폴트. `MMFAR Address` 0x20000dd8 = `kernel_global`의 주소(위 nm). FreeRTOS 예제 9의 BAD1과 같다.
- **BAD3**: 커널 함수 `z_impl_k_sem_give`는 **실행은 된다** — 폴트 PC 0x4024가 `z_impl_k_sem_give`(이 빌드에서 0x4014) 시작 + 0x10, 즉 함수 안이다. Zephyr는 커널 코드를 user에게 실행 가능하게 둔다. 하지만 그 함수가 세마포어 객체(커널 데이터, MMFAR 0x20000928 = `sem`)를 건드리는 순간 MPU 폴트가 난다. FreeRTOS-MPU는 같은 시도를 커널 코드 첫 명령에서 `IACCVIOL`로 막았다(예제 9 BAD4). **보호의 단위가 다르다**: FreeRTOS-MPU는 코드와 데이터 둘 다, Zephyr는 데이터만.

---

## 5. 측정 비교 — 세 경로의 비용을 한 차트에

### 5.1 예제 14 — 측정값으로 차트 만들기

예제 1(기본 FreeRTOS), 예제 6(FreeRTOS-MPU v1·v2, 특권·비특권), 예제 12(Zephyr 세 구성)의 숫자를 모아 Python으로 막대 좌표를 계산했다. "큐 송신 계열"(`xQueueSend` / `k_msgq_put`, 4바이트 메시지, 대기자 없음)과 "가장 단순한 조회 API"(`uxQueueMessagesWaiting` / `k_sem_count_get`) 두 계열이다. 조회 API는 진짜 일이 거의 없어서 **커널 진입 고정 비용**을 거의 그대로 보여 준다.

```python
#!/usr/bin/env python3
"""C8 예제 14: 측정 숫자(예제 1·6·12, QEMU icount, ns/call)로 가로 막대 차트 SVG를 만든다."""
import sys
rows = [  # (라벨, 큐 송신 계열, 가장 단순한 조회 API)
    ("FreeRTOS 기본 (특권)",        3878, 1312),
    ("FreeRTOS-MPU v1 특권 태스크", 4230, 1472),
    ("FreeRTOS-MPU v1 비특권",      5152, 2400),
    ("FreeRTOS-MPU v2 특권 태스크", 6891, 2496),
    ("FreeRTOS-MPU v2 비특권",     13233, 8203),
    ("Zephyr USERSPACE=n",          int(sys.argv[1]), int(sys.argv[2])),
    ("Zephyr USERSPACE=y supv",     2656, 512),
    ("Zephyr USERSPACE=y user",    12416, 6368),
]
W, X0, BW, BH, GAP, TOP = 680, 210, 380, 13, 14, 50
xmax = 14000
sx = lambda v: X0 + v * BW / xmax
out = [f'<svg viewBox="0 0 {W} {TOP + len(rows) * (2 * BH + GAP) + 50}" xmlns="http://www.w3.org/2000/svg">']
out.append(f'<text x="{W/2}" y="20" font-size="13" text-anchor="middle" font-weight="bold">API 한 번의 비용 (ns, QEMU icount, 25 MHz M3 가상)</text>')
out.append(f'<rect x="40" y="30" width="12" height="10" fill="#4a7bd0" fill-opacity="0.8"/><text x="56" y="39" font-size="12">xQueueSend / k_msgq_put</text>')
out.append(f'<rect x="280" y="30" width="12" height="10" fill="#e08a3c" fill-opacity="0.8"/><text x="296" y="39" font-size="12">uxQueueMessagesWaiting / k_sem_count_get</text>')
y = TOP
for lab, a, b in rows:
    out.append(f'<text x="{X0-8}" y="{y+BH+4}" font-size="12" text-anchor="end">{lab}</text>')
    for v, c, dy in ((a, "#4a7bd0", 0), (b, "#e08a3c", BH)):
        out.append(f'<rect x="{X0}" y="{y+dy}" width="{sx(v)-X0:.1f}" height="{BH-1}" fill="{c}" fill-opacity="0.8"/>')
        out.append(f'<text x="{sx(v)+4:.1f}" y="{y+dy+BH-2}" font-size="12">{v}</text>')
    y += 2 * BH + GAP
out.append(f'<line x1="{X0}" y1="{TOP-4}" x2="{X0}" y2="{y}" stroke="currentColor"/>')
for t in range(0, xmax + 1, 2000):
    out.append(f'<line x1="{sx(t):.1f}" y1="{y}" x2="{sx(t):.1f}" y2="{y+4}" stroke="currentColor"/>')
    out.append(f'<text x="{sx(t):.1f}" y="{y+18}" font-size="12" text-anchor="middle">{t}</text>')
out.append(f'<text x="{X0+BW/2}" y="{y+36}" font-size="12" text-anchor="middle">ns / call (루프 256회 평균, 루프 비용 포함)</text>')
out.append('</svg>')
print("\n".join(out))
```

```sh
python3 ~/.rtos_scratch/C8/tools/chart.py 2336 192 > ~/.rtos_scratch/C8/note/chart.svg   # 인자 = Zephyr USERSPACE=n 측정값
```

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<text x="340.0" y="20" font-size="13" text-anchor="middle" font-weight="bold">API 한 번의 비용 (ns, QEMU icount, 25 MHz M3 가상)</text>
<rect x="40" y="30" width="12" height="10" fill="#4a7bd0" fill-opacity="0.8"/><text x="56" y="39" font-size="12">xQueueSend / k_msgq_put</text>
<rect x="280" y="30" width="12" height="10" fill="#e08a3c" fill-opacity="0.8"/><text x="296" y="39" font-size="12">uxQueueMessagesWaiting / k_sem_count_get</text>
<text x="202" y="67" font-size="12" text-anchor="end">FreeRTOS 기본 (특권)</text>
<rect x="210" y="50" width="105.3" height="12" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="319.3" y="61" font-size="12">3878</text>
<rect x="210" y="63" width="35.6" height="12" fill="#e08a3c" fill-opacity="0.8"/>
<text x="249.6" y="74" font-size="12">1312</text>
<text x="202" y="107" font-size="12" text-anchor="end">FreeRTOS-MPU v1 특권 태스크</text>
<rect x="210" y="90" width="114.8" height="12" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="328.8" y="101" font-size="12">4230</text>
<rect x="210" y="103" width="40.0" height="12" fill="#e08a3c" fill-opacity="0.8"/>
<text x="254.0" y="114" font-size="12">1472</text>
<text x="202" y="147" font-size="12" text-anchor="end">FreeRTOS-MPU v1 비특권</text>
<rect x="210" y="130" width="139.8" height="12" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="353.8" y="141" font-size="12">5152</text>
<rect x="210" y="143" width="65.1" height="12" fill="#e08a3c" fill-opacity="0.8"/>
<text x="279.1" y="154" font-size="12">2400</text>
<text x="202" y="187" font-size="12" text-anchor="end">FreeRTOS-MPU v2 특권 태스크</text>
<rect x="210" y="170" width="187.0" height="12" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="401.0" y="181" font-size="12">6891</text>
<rect x="210" y="183" width="67.7" height="12" fill="#e08a3c" fill-opacity="0.8"/>
<text x="281.7" y="194" font-size="12">2496</text>
<text x="202" y="227" font-size="12" text-anchor="end">FreeRTOS-MPU v2 비특권</text>
<rect x="210" y="210" width="359.2" height="12" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="573.2" y="221" font-size="12">13233</text>
<rect x="210" y="223" width="222.7" height="12" fill="#e08a3c" fill-opacity="0.8"/>
<text x="436.7" y="234" font-size="12">8203</text>
<text x="202" y="267" font-size="12" text-anchor="end">Zephyr USERSPACE=n</text>
<rect x="210" y="250" width="63.4" height="12" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="277.4" y="261" font-size="12">2336</text>
<rect x="210" y="263" width="5.2" height="12" fill="#e08a3c" fill-opacity="0.8"/>
<text x="219.2" y="274" font-size="12">192</text>
<text x="202" y="307" font-size="12" text-anchor="end">Zephyr USERSPACE=y supv</text>
<rect x="210" y="290" width="72.1" height="12" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="286.1" y="301" font-size="12">2656</text>
<rect x="210" y="303" width="13.9" height="12" fill="#e08a3c" fill-opacity="0.8"/>
<text x="227.9" y="314" font-size="12">512</text>
<text x="202" y="347" font-size="12" text-anchor="end">Zephyr USERSPACE=y user</text>
<rect x="210" y="330" width="337.0" height="12" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="551.0" y="341" font-size="12">12416</text>
<rect x="210" y="343" width="172.8" height="12" fill="#e08a3c" fill-opacity="0.8"/>
<text x="386.8" y="354" font-size="12">6368</text>
<line x1="210" y1="46" x2="210" y2="370" stroke="currentColor"/>
<line x1="210.0" y1="370" x2="210.0" y2="374" stroke="currentColor"/>
<text x="210.0" y="388" font-size="12" text-anchor="middle">0</text>
<line x1="264.3" y1="370" x2="264.3" y2="374" stroke="currentColor"/>
<text x="264.3" y="388" font-size="12" text-anchor="middle">2000</text>
<line x1="318.6" y1="370" x2="318.6" y2="374" stroke="currentColor"/>
<text x="318.6" y="388" font-size="12" text-anchor="middle">4000</text>
<line x1="372.9" y1="370" x2="372.9" y2="374" stroke="currentColor"/>
<text x="372.9" y="388" font-size="12" text-anchor="middle">6000</text>
<line x1="427.1" y1="370" x2="427.1" y2="374" stroke="currentColor"/>
<text x="427.1" y="388" font-size="12" text-anchor="middle">8000</text>
<line x1="481.4" y1="370" x2="481.4" y2="374" stroke="currentColor"/>
<text x="481.4" y="388" font-size="12" text-anchor="middle">10000</text>
<line x1="535.7" y1="370" x2="535.7" y2="374" stroke="currentColor"/>
<text x="535.7" y="388" font-size="12" text-anchor="middle">12000</text>
<line x1="590.0" y1="370" x2="590.0" y2="374" stroke="currentColor"/>
<text x="590.0" y="388" font-size="12" text-anchor="middle">14000</text>
<text x="400.0" y="406" font-size="12" text-anchor="middle">ns / call (루프 256회 평균, 루프 비용 포함)</text>
</svg>
```

그림 6 — 이 노트에서 직접 잰 API 한 번의 비용(QEMU icount, 명령 1개 = 32 ns. 실제 칩과 절대값은 다르고, 특히 SVC 경로는 실칩에서 예외 스태킹 사이클만큼 더 든다). 파랑 = 큐 송신 계열, 주황 = 가장 단순한 조회 API.

### 5.2 숫자를 읽는 법

| 구성 | 큐 송신 (ns) | 조회 (ns) | 조회 기준 "진입 고정 비용" 추정 |
|---|---|---|---|
| FreeRTOS 기본 | 3878 | 1312 | 0 (함수 호출) |
| FreeRTOS-MPU v1 비특권 | 5152 | 2400 | 약 1.1 us (SVC 1번 + 특권 내림) |
| FreeRTOS-MPU v2 특권 태스크 | 6891 | 2496 | 약 1.2 us (래퍼 분기 + 인덱스 변환·검사, 예외 없음) |
| FreeRTOS-MPU v2 비특권 | 13233 | 8203 | 약 6.9 us (SVC 2번 + 프레임 복사 2번 + 검사) |
| Zephyr USERSPACE=n | 2336 | 192 | 0 (함수 호출) |
| Zephyr USERSPACE=y supv | 2656 | 512 | 약 0.3 us (CONTROL 검사 분기) |
| Zephyr USERSPACE=y user | 12416 | 6368 | 약 6.2 us (SVC 1번 + 특권 스택 + 디스패치 + 객체 해시·권한) |

- 두 보호 RTOS 모두 **비특권 진입 고정 비용이 약 6–7 us(QEMU에서 약 200 명령)**로 비슷하다. 구조가 거의 같기 때문이다(SVC → 특권 Thread 모드 → 표 디스패치 → 검사 → 진짜 함수).
- Zephyr의 기본 API가 FreeRTOS보다 싸게 나온 것(조회 192 vs 1312 ns)은 진입 경로가 아니라 **구현 차이**다. `k_sem_count_get`은 `sem->count`를 그냥 읽는 인라인이고, `uxQueueMessagesWaiting`은 임계 구역에 들어갔다 나온다. 이 노트의 관심은 같은 RTOS 안에서 **경로를 바꿨을 때의 차이**다.
- "특권이지만 보호 빌드"의 비용(v2 특권 +1.2 us, Zephyr supv +0.3 us)은 **보호 빌드에서 특권 태스크·supervisor 스레드로 남겨 둔 고주파 코드도 공짜는 아니다**라는 뜻이다. FreeRTOS v2는 특권 태스크도 Impl의 검사 경로를 탄다.

---
## 6. 설계 토론 — 왜 대부분의 MCU RTOS는 경계를 두지 않나, 언제 두어야 하나

### 6.1 경계를 두지 않는 이유 세 가지 (이 노트에서 잰 숫자로)

1. **시간**: 비특권 큐 송신 한 번이 기본 3.9 us → FreeRTOS-MPU v2 13.2 us로, Zephyr는 2.3 us(`USERSPACE=n`) → 12.4 us(user 스레드)로 늘었다(§5). 드론 rate loop가 8 kHz(125 us 주기)에 IMU 샘플을 큐로 넘기고, 모터 출력·로그·텔레메트리로 API를 10번 부른다고 하면, 기본 빌드는 약 40 us, v2 비특권 빌드는 약 130 us다 — **주기 전체를 넘는다**(QEMU icount 기준이라 절대값은 다르지만 비율이 이렇게 벌어진다는 점이 중요하다). 실칩에서는 SVC 진입·복귀마다 예외 스태킹 사이클이 더 붙는다.
2. **메모리**: 커널 코드 +49%(19.7 → 29.3 KB), TCB 96 → 788 바이트, 그리고 v7-M MPU의 2의 거듭제곱 정렬 낭비(이 노트의 빌드는 커널 코드 33.5 KB에 64 KB 리전). 256 KB 플래시·64 KB RAM MCU에서는 치명적이다.
3. **복잡도**: 링커 스크립트에 섹션 세 개, 스택 정렬, 리전 설계, 객체마다 ACL 부여, 비특권에서 금지된 API 목록, 폴트 핸들러 설계. 무엇보다 **데이터 공유가 어려워진다** — 전역 변수 하나 읽는 데도 리전이 필요하다(예제 6에서 직접 부딪혔다).

그리고 근본적인 이유: MCU 펌웨어는 보통 **한 팀이 쓰고, 한 번에 빌드하고, 한 번에 검증하는 한 덩어리**다. Linux처럼 서로 모르는 사람이 쓴 프로그램 수백 개를 한 커널 위에서 돌리는 게 아니다. 격리가 막아 줄 위협(악성 앱, 모르는 코드)이 애초에 적다.

### 6.2 경계가 필요한 경우

- **신뢰 수준이 다른 코드가 섞일 때**: 외부 입력(무선 링크·CAN·USB·네트워크)을 파싱하는 코드, 서드파티 라이브러리(센서 퓨전, 통신 스택), 현장에서 업데이트되는 앱 모듈. 파서 버그 하나가 제어 루프를 죽이면 안 될 때.
- **안전 인증에서 "간섭 없음(freedom from interference)"을 보여야 할 때**: ISO 26262·IEC 61508은 서로 다른 안전 등급(ASIL/SIL)의 소프트웨어를 한 MCU에 올리려면 낮은 등급 코드가 높은 등급 코드의 메모리를 깨지 못한다는 근거를 요구한다. MPU 격리는 그 근거의 하드웨어 기반이다(O1, O6).
- **보안 요구가 있을 때**: 국방·차량·의료 제품의 위협 모델에서 "원격으로 들어온 공격자가 한 태스크를 장악해도 다른 태스크·키·부트 상태를 못 건드린다"가 요구사항일 때. 이때는 MPU 격리에 TrustZone-M(F6)과 보안 부트가 함께 간다.
- **결함 격리·재시작**: 비필수 태스크가 죽으면 그 태스크만 재시작하고 시스템은 계속 돈다(MemManage 핸들러에서 해당 태스크를 정리). Hubris가 이 설계를 끝까지 밀고 간 예다(A3 §3.3).

```svg
<svg viewBox="0 0 660 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="190" y="10" width="280" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="330" y="35" font-size="13" text-anchor="middle">신뢰 수준이 다른 코드가 섞이나?</text>
<line x1="330" y1="50" x2="330" y2="78" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<text x="338" y="68" font-size="12">예</text>
<line x1="470" y1="30" x2="540" y2="30" stroke="currentColor" stroke-width="1.2"/>
<line x1="540" y1="30" x2="540" y2="268" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<text x="548" y="50" font-size="12">아니오</text>
<rect x="190" y="80" width="280" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="330" y="105" font-size="13" text-anchor="middle">안전·보안 근거가 요구되나?</text>
<line x1="330" y1="120" x2="330" y2="148" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<text x="338" y="138" font-size="12">예</text>
<line x1="190" y1="100" x2="110" y2="100" stroke="currentColor" stroke-width="1.2"/>
<line x1="110" y1="100" x2="110" y2="148" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<text x="118" y="92" font-size="12">아니오</text>
<rect x="10" y="150" width="200" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="110" y="173" font-size="12" text-anchor="middle">MPU 스택 가드만 (F3)</text>
<text x="110" y="191" font-size="12" text-anchor="middle">API는 함수 호출 그대로</text>
<rect x="190" y="150" width="280" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="330" y="175" font-size="13" text-anchor="middle">비특권 코드가 고주파 루프에 있나?</text>
<line x1="330" y1="190" x2="330" y2="218" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<text x="338" y="208" font-size="12">아니오</text>
<line x1="470" y1="170" x2="500" y2="170" stroke="currentColor" stroke-width="1.2"/>
<line x1="500" y1="170" x2="500" y2="300" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<text x="476" y="162" font-size="12">예</text>
<rect x="190" y="220" width="280" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="330" y="243" font-size="12" text-anchor="middle">FreeRTOS-MPU v2 / Zephyr userspace</text>
<text x="330" y="261" font-size="12" text-anchor="middle">신뢰 못 할 부분만 비특권으로</text>
<rect x="480" y="270" width="170" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="565" y="295" font-size="12" text-anchor="middle">기본 빌드 (경계 없음)</text>
<rect x="190" y="302" width="280" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="330" y="325" font-size="12" text-anchor="middle">고주파 루프는 특권 태스크에 남기고</text>
<text x="330" y="343" font-size="12" text-anchor="middle">비특권 쪽과는 큐 한 번으로만 만난다</text>
<line x1="500" y1="300" x2="472" y2="318" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
</svg>
```

그림 7 — 커널 경계를 둘지 정하는 흐름. 실무의 정답은 대개 "전부 격리"가 아니라 **"신뢰 못 할 부분만 비특권으로, 고주파 제어는 특권으로"**다.

### 6.3 FreeRTOS-MPU vs Zephyr userspace 한 장 비교

| 항목 | FreeRTOS-MPU v2 | Zephyr userspace |
|---|---|---|
| 켜는 법 | MPU 포트 선택 + 링커 섹션 + 설정 매크로 | `CONFIG_USERSPACE=y` (MPU가 있는 보드) |
| API 정의 방식 | 포트마다 손으로 쓴 asm 래퍼 + `mpu_wrappers_v2.c`의 Impl | `__syscall` 선언에서 빌드 때 생성 (`z_impl_`/`z_vrfy_`/`z_mrsh_`) |
| 객체 식별 | 정수 인덱스(풀 칸 번호) | 커널 객체 포인터 + 빌드 때 만든 perfect hash(gperf)로 검증 |
| 객체 권한 | ACL 비트맵(`vGrantAccessToQueue` 등) | 객체마다 스레드 권한 비트맵(`k_object_access_grant`) |
| 메모리 권한 | 태스크마다 리전 3개 + 스택 | memory domain(파티션 묶음), 스레드는 한 domain에 속함 |
| 커널 코드 보호 | 있음 (`privileged_functions` 특권 전용) | 없음 (코드는 user도 실행 가능, 데이터만 보호) |
| 진입·퇴장 | SVC 2번 (`svc N` + `svc 103`) | SVC 1번 (`svc 3`) + 특권에서 CONTROL로 직접 내려옴 |
| 비특권에서 객체 생성 | 금지 | 가능 (`CONFIG_DYNAMIC_OBJECTS`일 때 `k_object_alloc`) |
| 검증된 진입 위치 | `svc` PC가 syscall 섹션 안이어야 함 | 번호 범위만 확인 (`K_SYSCALL_BAD`), 코드 위치 제한 없음 |

---

## 7. RTOS 커널 안에서는 무슨 일이 일어나나

이 노트에서 따라간 경로를 소스 위치로 정리한다. 진짜 깊은 내용(MPU 리전 교체, memory domain 설계)은 F4·F5에서 다룬다.

| 단계 | FreeRTOS 기본 | FreeRTOS-MPU v2 | Zephyr userspace |
|---|---|---|---|
| 앱이 쓰는 이름 | `xQueueSend` 매크로 (`include/queue.h`) | 같은 매크로 → `mpu_wrappers.h`가 `MPU_xQueueGenericSend`로 바꿈 | `k_sem_give` (`include/zephyr/kernel.h`의 `__syscall` 선언) |
| 진입 코드 | 없음 (`bl`) | `portable/GCC/ARM_CM3_MPU/mpu_wrappers_v2_asm.c` (naked, CONTROL 검사 → `svc N`) | 생성된 `syscalls/kernel.h` 인라인 → `arch_syscall_invoke1()` (`include/zephyr/arch/arm/syscall.h`, `svc 3`, r6 = 번호) |
| 예외 핸들러 | 없음 | `vPortSVCHandler` → `vSystemCallEnter()` (`port.c`) | `z_arm_svc` (`arch/arm/core/cortex_m/svc.S`) → 복귀 주소를 `z_arm_do_syscall`로 고침 |
| 디스패치 | 없음 | `uxSystemCallImplementations[]` 표 (`mpu_wrappers_v2.c` 끝) | `_k_syscall_table[]` (생성된 `syscall_dispatch.c`) — `z_arm_do_syscall` (`arch/arm/core/userspace.S`) |
| 검사 | `configASSERT` | `MPU_xQueueGenericSendImpl()`: `IS_EXTERNAL_INDEX_VALID`, `xPortIsAuthorizedToAccessKernelObject`, `xPortIsAuthorizedToAccessBuffer` | `z_mrsh_k_sem_give` → `z_vrfy_k_sem_give()` (`kernel/sem.c`): `K_OOPS(K_SYSCALL_OBJ(sem, K_OBJ_SEM))` |
| 진짜 일 | `xQueueGenericSend()` (`queue.c`) | 같은 함수 | `z_impl_k_sem_give()` (`kernel/sem.c`) |
| 실행 스택 | 태스크 스택 | TCB 안의 `ulSystemCallStackBuffer` | 스레드의 특권 스택(`priv_stack_start`) |
| 복귀 | `bx lr` | `vRequestSystemCallExit()` → `svc 103` → `vSystemCallExit()` | `z_arm_do_syscall` 끝에서 PSP 복원 + `msr CONTROL`(nPRIV=1) |

두 보호 RTOS가 **같은 트릭**을 쓴다는 점이 재밌다. SVC 핸들러 안에서 시스템 콜을 실행하지 않고, 쌓인 예외 프레임의 PC를 고쳐 써서 **예외 복귀가 특권 Thread 모드의 커널 함수로 떨어지게** 만든다. 이유는 둘 다 같다 — Handler 모드에서 오래 일하면 같거나 낮은 우선순위 인터럽트가 전부 막히고, 시스템 콜이 블로킹(`xQueueReceive(..., portMAX_DELAY)`)해야 할 때 컨텍스트 스위치를 할 수 없다. 특권 Thread 모드에서 돌면 시스템 콜도 보통 태스크 코드처럼 선점되고, 블록되고, 깨어난다. 차이는 퇴장이다: FreeRTOS는 프레임을 다시 태스크 스택으로 옮기려고 SVC를 한 번 더 내고, Zephyr는 특권 코드가 스스로 CONTROL.nPRIV를 1로 써서 내려온다(특권은 언제든 자기 특권을 **내릴** 수 있다 — 올리는 것만 막혀 있다).

Linux(EL E6)와 비교하면: Linux arm64는 `svc #0` → `el0_svc` → `invoke_syscall()` → `sys_call_table[nr]`로 같은 모양이지만, (1) 주소 공간이 달라 `copy_from_user`로 **복사**하고, (2) 커널 스택이 스레드마다 따로 있고(Zephyr의 특권 스택·FreeRTOS의 syscall 스택이 그 대응물), (3) 시스템 콜이 원래부터 특권 모드에서 선점 가능하다.

---

## 8. 실무에서 다시 보기

### 8.1 드론 비행 제어기 — 텔레메트리 파서를 비특권으로

예를 들어 STM32H7급 비행 제어기라면, 자세 제어 rate loop(IMU 8 kHz)와 모터 출력은 특권 태스크에 두고, 무선 링크로 들어오는 MAVLink·CRSF 프레임을 파싱하는 태스크만 restricted로 만든다. 파서는 외부에서 온 바이트를 다루니 버퍼 오버런 버그가 가장 나기 쉬운 곳이다. 파서와 제어 루프는 큐 하나로만 만난다. 파서가 엉뚱한 주소에 쓰면 MemManage가 나고, 핸들러는 파서 태스크만 지우고 다시 만든다 — 기체는 계속 난다.

```c
/* 개념 코드 (빌드만 확인하지 않음): 파서 태스크에 RX 링버퍼 리전 하나만 준다 */
static uint8_t rx_ring[512] __attribute__((aligned(512)));          /* 리전 크기 = 정렬 */
static StackType_t parser_stack[256] __attribute__((aligned(1024)));
static const TaskParameters_t parser_params = {
    .pvTaskCode = parser_task, .pcName = "mavlink", .usStackDepth = 256,
    .pvParameters = NULL, .uxPriority = 2, .puxStackBuffer = parser_stack,
    .xRegions = {
        { rx_ring, sizeof(rx_ring), portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER },
        { 0, 0, 0 }, { 0, 0, 0 },
    },
};
/* 초기화(특권): xTaskCreateRestricted(&parser_params, &h); vGrantAccessToQueue(h, cmd_q); */
```

이 코드는 이 노트에서 돌리지 않은 설계 스케치다(필드 이름과 `portMPU_REGION_*` 매크로는 V11.3.2 `task.h`·`ARM_CM3_MPU/portmacro.h`에 있는 것만 썼다). 원칙은 두 가지다: **고주파 경로에는 SVC를 두지 않는다**, **비특권 쪽과의 접점은 큐 하나로 좁힌다**.

### 8.2 로봇 관절 모터 컨트롤러 — 20 kHz 루프에는 커널 진입 자체가 없다

FOC 전류 루프(20 kHz = 50 us 주기)는 보통 ADC 완료 ISR 안에서 끝난다. RTOS API를 부르지 않고, 부르더라도 `...FromISR` 한두 번이다. 여기서 보호 빌드의 비용은 거의 문제가 안 된다 — ISR은 Handler 모드라 원래 특권이고, 래퍼를 지나지 않는다(ISR 안에서 `MPU_xQueueGenericSendFromISR`도 CONTROL 검사 후 특권 경로를 탄다). 비용이 드는 건 CAN 명령 처리·진단 같은 태스크 쪽이다. 그래서 모터 컨트롤러에서 격리를 한다면 "CAN 진단·부트로더 통신 태스크만 비특권"이 현실적인 선택이다.

### 8.3 센서 허브 MCU — 서드파티 퓨전 라이브러리를 Zephyr user 스레드로

Linux SoC 옆의 센서 허브에서 벤더가 바이너리로 준 센서 퓨전 라이브러리를 돌린다면, Zephyr user 스레드 + 그 라이브러리 전용 memory partition이 깔끔하다. 라이브러리가 쓰는 전역 변수는 `K_APP_DMEM(part)`/`K_APP_BMEM(part)`로 파티션에 넣고, 쓸 수 있는 커널 객체(입력 `k_msgq`, 출력 `k_msgq`)만 `k_object_access_grant`로 준다. 라이브러리가 IMU 드라이버 상태나 타임스탬프 카운터를 깨는 일이 원천 차단된다. 비용은 메시지 한 번에 수 us 수준이고, 퓨전 주기(수백 Hz)에서는 무시할 만하다.

### 8.4 Don의 SSD 펌웨어와 연결

SSD 컨트롤러에서 호스트 인터페이스 코어가 받은 NVMe 명령을 FTL 코어에 넘길 때, FTL은 명령의 LBA 범위·PRP 주소를 검사한 뒤에야 실행했다. 이게 정확히 `z_vrfy_*`/`MPU_*Impl`이 하는 일이다 — **신뢰 경계에서 들어오는 요청은 검사하고, 경계 안쪽 코드끼리는 믿는다.** SSD에서도 코어 안 함수 호출에는 검사를 넣지 않았다(성능). 경계를 어디에 긋느냐가 설계의 핵심이라는 점이 RTOS와 같다.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| restricted 태스크가 전역 변수를 읽음 | 태스크 시작하자마자 MemManage, MMFAR = 그 전역 주소 | `.bss`/`.data`가 태스크 리전에 없음 | 인자·큐로 넘기거나 `xRegions`에 리전 추가 |
| 스택 배열 정렬 안 함 | 첫 push에서 MemManage 또는 다른 태스크 메모리 침범 | v7-M 리전은 크기 정렬 필수 | `aligned(스택 바이트)`, 크기도 2의 거듭제곱 |
| `privileged_functions` 리전이 작음 | 링크 실패 `cannot move location counter backwards` | 커널 코드가 리전보다 큼 | 다음 2의 거듭제곱, 또는 커널 기능 축소 |
| v2에서 ACL 부여를 잊음 | API가 예외 없이 `pdFAIL`/0 반환, 태스크가 이상 동작 | `xPortIsAuthorizedToAccessKernelObject` 실패 | 초기화 때 `vGrantAccessTo*` |
| 비특권 태스크에서 `xQueueCreate` 호출 | 생성 실패(NULL) | v2에서 생성 API는 비특권 금지 | 특권 초기화 코드에서 만들고 넘김 |
| 폴트 핸들러에서 FreeRTOS API 호출 | MemManage가 HardFault로 번짐 | 앱 코드 API = `MPU_` 래퍼, nPRIV=1이면 Handler 모드에서 `svc` | 핸들러는 레지스터만 읽고 특권 태스크에 알림 |
| Zephyr user 스레드에 객체 권한 안 줌 | `ZEPHYR FATAL ERROR 3: Kernel oops`, 기본 fatal 핸들러면 시스템 정지 | `z_vrfy_*`의 `K_SYSCALL_OBJ` 실패 | `k_object_access_grant` 또는 `k_thread_access_grant()` 매크로 |
| Zephyr user 스레드가 주변장치 레지스터 직접 접근 | MPU FAULT, Data Access Violation | 주변장치가 memory domain에 없음 | 드라이버를 syscall로 감싸거나 MMIO 파티션 추가 |
| `CONFIG_USERSPACE` 빌드에 gperf 없음 | 빌드 실패 `GPERF-NOTFOUND` | 커널 객체 perfect hash 생성에 gperf 필요 | 호스트에 gperf 설치 (이 노트는 직접 빌드) |
| 고주파 루프를 비특권으로 만듦 | 주기 초과·지터 증가 | API마다 SVC 2번 + 검사 | 고주파 경로는 특권에 두고 경계는 큐 하나로 |

---

## 10. 면접에서 이렇게 말한다

**Q.** "Is calling `xQueueSend()` in FreeRTOS a system call?"

**A.** 기본 빌드에서는 아니다. FreeRTOS는 라이브러리형 커널이라 태스크와 커널이 같은 특권·같은 주소 공간에서 돌고, `xQueueSend`는 `bl xQueueGenericSend` 한 줄짜리 함수 호출이다. 비용은 진입이 아니라 임계 구역과 큐 조작에서 나온다. MPU 포트를 쓰면 비특권 태스크의 호출이 `svc`를 거치는 진짜 시스템 콜이 된다.

> In a default build, no. FreeRTOS is a library-style kernel: tasks and the kernel run at the same privilege level in one address space, so `xQueueSend` compiles to a plain `bl xQueueGenericSend`. I've checked the disassembly — the only `svc` in the image is the one that starts the first task. The cost is in the critical section and queue bookkeeping, about 120 instructions on a Cortex-M3. It only becomes a real system call when you use an MPU port, where an unprivileged task's call goes through an SVC.

**꼬리 질문**: "Then what stops a buggy task from corrupting the kernel?" → 기본 빌드에서는 아무것도 없다. 스택 오버플로 검사와 MPU 스택 가드 정도, 진짜 격리는 FreeRTOS-MPU나 Zephyr userspace.

**Q.** "Walk me through how an unprivileged task calls the kernel in FreeRTOS-MPU."

**A.** 앱의 `xQueueSend`는 `mpu_wrappers.h` 매크로 때문에 `MPU_xQueueGenericSend` 래퍼를 부른다. 래퍼는 CONTROL.nPRIV를 보고 비특권이면 `svc 33`을 낸다. SVC 핸들러는 PC가 syscall 섹션 안인지 확인하고, 프레임을 TCB 안의 syscall 스택으로 복사하고, 특권을 올리고, PC를 Impl 함수로 고쳐서 예외 복귀한다. Impl은 특권 Thread 모드에서 핸들 인덱스·ACL·버퍼 권한을 검사하고 진짜 함수를 부른다. 끝나면 `svc 103`으로 원래 스택·비특권으로 돌아온다.

> The application's `xQueueSend` is macro-renamed to `MPU_xQueueGenericSend`. That naked wrapper reads CONTROL; if the task is unprivileged it executes `svc 33`. The SVC handler checks that the SVC came from the system-call section, copies the hardware frame onto a per-task privileged system-call stack, clears nPRIV, and rewrites the stacked PC to the `...Impl` function, so the exception return lands in privileged thread mode. The Impl validates the handle index, the access-control list and any buffer pointer, then calls the real `xQueueGenericSend`. On return a second SVC, number 103, copies the frame back and drops privilege. I traced exactly this in gdb on QEMU — IPSR 11, then IPSR 0 with CONTROL 0x2, then IPSR 11 again.

**꼬리 질문**: "Why not just run the call inside the SVC handler?" → 그러면 같은 우선순위 이하 인터럽트가 막히고, 블로킹 API가 컨텍스트 스위치를 못 한다.

**Q.** "Why can't an unprivileged task simply clear `CONTROL.nPRIV` and become privileged?"

**A.** 하드웨어가 막는다. 비특권 Thread 모드에서 `msr control`은 무시된다 — QEMU에서 직접 해 보면 CONTROL이 0x3 그대로다. 특권으로 가는 유일한 길은 예외이고, 예외 핸들러는 커널이 쓴 코드라 거기서 검사한다.

> Because the architecture ignores that write. On ARMv7-M an `msr control` from unprivileged thread mode has no effect — I tried it and CONTROL stayed 0x3. The only way up is an exception, and the exception handlers are kernel code, so that's where the checks live. Lowering privilege, on the other hand, is always allowed, which is why Zephyr can drop back to user mode with a plain `msr` at the end of a syscall.

**Q.** "What does Zephyr generate from a `__syscall` declaration?"

**A.** 빌드 때 스크립트가 헤더의 `__syscall` 함수를 모아 번호를 매기고, API 이름의 인라인 함수(유저 컨텍스트면 `arch_syscall_invoke`로 `svc`, 아니면 `z_impl_` 직접 호출), 인자를 풀어 `z_vrfy_`를 부르는 `z_mrsh_` 마샬링 함수, 그리고 디스패치 표 `_k_syscall_table`을 만든다. 개발자는 `z_impl_`(진짜 구현)과 `z_vrfy_`(검사)만 쓴다.

> At build time a script scans headers for `__syscall` prototypes, assigns each a number, and generates three things: an inline wrapper with the API name — it checks whether the caller is in user context and either traps with `svc` via `arch_syscall_invoke` or calls `z_impl_` directly; a `z_mrsh_` marshalling stub that unpacks the registers and calls the `z_vrfy_` verifier; and the `_k_syscall_table` dispatch table. The developer writes only `z_impl_`, the real implementation, and `z_vrfy_`, which checks object permissions and memory ranges before calling `z_impl_`.

**Q.** "What does memory protection cost you, and would you enable it on a flight controller?"

**A.** 내가 잰 숫자로는 비특권 큐 송신이 기본 3.9 us에서 FreeRTOS-MPU v2 13.2 us로 3배 이상, 커널 코드 +49%, TCB 8배다. 비행 제어기라면 rate loop는 특권에 두고, 외부 입력 파서 같은 신뢰 못 할 코드만 비특권으로 만들고, 둘 사이는 큐 하나로 연결하겠다.

> On QEMU with deterministic instruction counting, an unprivileged queue send went from 3.9 microseconds in a plain build to 13.2 with FreeRTOS-MPU v2 — over three times — and kernel code grew about 49 percent, the TCB from 96 to 788 bytes, plus MPU alignment waste. So I wouldn't put the rate loop behind the boundary. I'd keep the control loop and drivers privileged, run the parts that touch untrusted input — the telemetry or MAVLink parser, third-party libraries — as restricted tasks, and connect them through a single queue. That gives fault containment where bugs are likely, without paying the syscall cost in the 8 kHz path.

**Q.** "What problem does FreeRTOS-MPU v2 solve that v1 didn't?"

**A.** v1은 래퍼가 특권을 올린 채 진짜 API를 불러서, 비특권 태스크가 아무 포인터를 핸들로 넘기면 커널이 그 주소에 대신 써 준다(confused deputy). v2는 핸들을 정수 인덱스로 바꾸고, ACL과 버퍼 권한 검사를 넣고, syscall 전용 스택을 써서 그 구멍을 막았다. 대신 느리다 — 내 측정에서 v1 5.2 us, v2 13.2 us.

> v1 just raised privilege and called the real API, so an unprivileged task could pass any pointer as a handle and the kernel would happily write through it — a confused-deputy problem. v2, introduced in 10.6.0, makes handles opaque integer indices into a kernel object pool, adds per-task access-control lists and checks that every buffer pointer is accessible to the caller, and runs the call on a separate privileged stack. In my measurement the unprivileged queue send was 5.2 microseconds with v1 and 13.2 with v2, so the extra validation is not free.

**Q.** "How would you debug a MemManage fault in a restricted task?"

**A.** CFSR의 MMFSR 비트로 데이터 접근(DACCVIOL)인지 명령 접근(IACCVIOL)인지 보고, MMARVALID면 MMFAR이 문제 주소다. 쌓인 프레임의 PC로 명령을, MMFAR을 map 파일·nm으로 대조해 어떤 변수인지 찾는다. 그리고 그 순간 MPU 리전을 덤프해서 그 주소가 왜 막혔는지 확인한다.

> I read the CFSR: DACCVIOL means a data access, IACCVIOL an instruction fetch, and if MMARVALID is set, MMFAR holds the faulting address. I take the stacked PC from the exception frame for the instruction, and look up MMFAR in the map file — in my lab a fault at 0x20009004 turned out to be a global the restricted task wasn't granted. Then I dump the MPU regions to see which rule blocked it. One gotcha: dump them from privileged context; even the debugger's memory reads go through the MPU with the current privilege on QEMU.

---

## 11. 직접 해보기

1. **개념**: 기본 FreeRTOS에서 `xQueueSend`와 `xQueueSendFromISR`의 진입 경로는 같은가? 다르다면 무엇이 다른가?
   정답/힌트: 둘 다 함수 호출이다. 다른 건 임계 구역 방식(`vPortEnterCritical` 중첩 카운트 vs `portSET_INTERRUPT_MASK_FROM_ISR` BASEPRI 저장·복원)과 블로킹 가능 여부다(C7).
2. **코드**: 예제 6의 `user_task`에 `xRegions[0]`으로 32바이트 공유 배열(32바이트 정렬)을 주고, user가 쓰고 priv가 읽게 해 보라. 정렬을 빼먹으면 무엇이 나는가?
   정답/힌트: `{ shared, 32, portMPU_REGION_READ_WRITE }`. 정렬이 틀리면 리전 base 하위 비트가 잘려 엉뚱한 곳이 열리거나 접근이 폴트 난다.
3. **측정**: 예제 6에 `configENABLE_ACCESS_CONTROL_LIST 0`으로 빌드해서 비특권 `xQueueSend` 비용이 얼마나 줄어드는지 재 보라.
   정답/힌트: ACL 검사는 비트 연산 몇 개라 차이가 작을 것이다. 비용 대부분은 SVC 2번과 프레임 복사, 버퍼 권한 검사다. 숫자로 확인하라.
4. **코드**: Zephyr 예제 12에서 user 스레드가 `k_msgq_put(&mq, (void *)0x20000000, K_NO_WAIT)`처럼 커널 메모리를 데이터 포인터로 넘기면 무슨 일이 나는가?
   정답/힌트: `z_vrfy_k_msgq_put`의 `K_OOPS(K_SYSCALL_MEMORY_READ(data, msgq->msg_size))`에서 걸려 kernel oops. FreeRTOS v2의 `xPortIsAuthorizedToAccessBuffer`와 같은 역할.
5. **개념**: Zephyr는 커널 **코드**를 user 스레드에서 실행할 수 있게 둔다(예제 13 BAD3). 이게 보안 구멍이 아닌 이유는?
   정답/힌트: 코드를 실행해도 커널 **데이터**가 memory domain 밖이라 첫 접근에서 MPU 폴트가 난다. 특권은 코드가 아니라 CONTROL 비트가 결정한다.
6. **측정**: 예제 1과 예제 6을 `configSYSTEM_CALL_STACK_SIZE` 64/128/256으로 바꿔 TCB 크기(예제 10b)를 표로 만들고, 태스크 12개 시스템의 RAM 비용을 계산하라.
   정답/힌트: syscall 스택 1워드 = 4바이트 × 태스크 수. 128워드면 태스크당 512바이트.
7. **설계**: 드론 FC에서 "로그를 SD 카드에 쓰는 태스크"를 비특권으로 만들지 말지 판단하고 근거를 써라.
   정답/힌트: 외부 입력이 없고 신뢰할 수 있는 코드라면 이득이 적다. 다만 파일시스템 라이브러리가 서드파티이고 크다면 격리 가치가 있다. 그 태스크가 부르는 API 빈도(쓰기 블록 수/초)로 비용을 계산해 보라.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| kernel entry path | 커널 진입 경로 | 앱 코드에서 커널 코드로 제어가 넘어가는 방식(함수 호출 또는 예외) |
| system call | 시스템 콜 | 특권 경계를 넘어 커널에 서비스를 요청하는 진입, 보통 예외 명령으로 |
| library-type kernel | 라이브러리형 커널 | 커널이 앱과 같은 이미지·특권에서 도는 C 라이브러리 (FreeRTOS·Zephyr 기본) |
| SVC | Supervisor Call | Cortex-M의 동기 예외 명령, `svc #imm8`로 서비스 번호를 담는다 |
| CONTROL.nPRIV | 특권 비트 | Thread 모드가 비특권(1)인지 특권(0)인지, 비특권은 0으로 못 되돌린다 |
| MPU wrappers | MPU 래퍼 | FreeRTOS-MPU에서 API 이름을 `MPU_` 함수로 바꿔 특권 경계를 처리하는 층 |
| restricted task | 제한 태스크 | `xTaskCreateRestricted`로 만든 비특권 태스크, 자기 스택 + 준 리전만 접근 |
| `portPRIVILEGE_BIT` | 특권 태스크 표시 | 우선순위에 OR하면 그 태스크가 특권으로 돈다 |
| system call stack | 시스템 콜 스택 | v2에서 태스크마다 TCB 안에 있는 특권 전용 스택 (`configSYSTEM_CALL_STACK_SIZE`) |
| kernel object pool | 커널 객체 풀 | v2에서 진짜 객체 포인터를 담는 표, 앱은 칸 번호(인덱스)만 받는다 |
| ACL | Access Control List | v2에서 태스크마다 "접근 가능한 커널 객체" 비트맵 |
| confused deputy | 혼동된 대리인 | 권한 있는 코드가 권한 없는 쪽의 인자를 믿고 대신 나쁜 일을 하는 문제 |
| `CONFIG_USERSPACE` | Zephyr 유저 모드 | user 스레드, syscall 검사, memory domain을 켜는 Kconfig |
| `__syscall` | 시스템 콜 선언 | 이 표시가 붙은 함수에 대해 빌드가 진입·검사·디스패치 코드를 생성 |
| `z_impl_` / `z_vrfy_` / `z_mrsh_` | 구현 / 검증 / 마샬링 | Zephyr syscall 하나를 이루는 세 함수 |
| `_k_syscall_table` | 디스패치 표 | syscall 번호 → `z_mrsh_` 함수 포인터 표 |
| K_USER | user 스레드 옵션 | 스레드를 비특권으로 시작하게 하는 `k_thread_create` 옵션 |
| memory domain / partition | 메모리 도메인 / 파티션 | Zephyr에서 user 스레드가 접근할 수 있는 메모리 구역의 묶음 |
| `k_object_access_grant` | 객체 권한 부여 | 특정 스레드에게 커널 객체 사용 권한을 주는 API |
| MemManage fault | 메모리 관리 폴트 | MPU 규칙 위반 때 나는 예외, CFSR·MMFAR로 원인 확인 |
| PRIVDEFENA | 배경 리전 | 특권 코드가 리전이 없는 곳도 기본 맵으로 접근하게 하는 MPU_CTRL 비트 |

---

## 13. 요약 & 체크리스트

기본 FreeRTOS·Zephyr에서 커널 API는 **함수 호출**이다. 모드 전환도 인자 검사도 없고, 비용은 커널이 하는 일 자체(임계 구역·리스트·복사)다. 보호 빌드를 켜면 비특권 코드는 스스로 특권을 올릴 수 없으니 **SVC 예외**로 커널에 부탁해야 한다. FreeRTOS-MPU v2는 매크로로 API 이름을 래퍼로 바꾸고, 래퍼가 `svc N`을 내고, 핸들러가 syscall 전용 스택·특권 Thread 모드로 Impl을 실행시키며 인덱스 핸들·ACL·버퍼 권한을 검사하고, `svc 103`으로 돌아온다. Zephyr는 `__syscall` 선언에서 진입·마샬링·디스패치 코드를 생성하고, `svc 3` 한 번으로 들어가 `z_vrfy_`로 객체 권한과 메모리 범위를 검사한 뒤 `z_impl_`을 부르고, CONTROL을 직접 써서 내려온다. 보호의 대가는 이 노트 측정으로 호출당 수 배의 시간, 커널 코드 +49%, TCB 8배, MPU 정렬 낭비다. 그래서 대부분의 MCU 제품은 경계를 두지 않고, 두더라도 **신뢰 못 할 코드만 비특권으로, 고주파 제어는 특권으로** 나눈다.

- [ ] 기본 FreeRTOS의 `xQueueSend` 호출 지점이 `bl` 한 줄임을 디스어셈블로 보여 줄 수 있다
- [ ] 기본 이미지의 유일한 `svc`가 무엇을 하는지(첫 태스크 시작) 말할 수 있다
- [ ] "인자 검사 없음"을 가짜 핸들 예로 설명할 수 있다
- [ ] `mpu_wrappers.h`의 매크로 바꿔치기와 `MPU_WRAPPERS_INCLUDED_FROM_API_FILE`의 역할을 안다
- [ ] FreeRTOS-MPU 링커 섹션 세 개와 필요한 심볼 다섯 쌍을 쓸 수 있다
- [ ] v1과 v2의 차이(진입 방식·스택·핸들·검사)를 표로 말할 수 있다
- [ ] v2 비특권 호출의 5단계(래퍼 → SVC 진입 → Impl → SVC 퇴장 → 복귀)를 IPSR·CONTROL 값과 함께 설명할 수 있다
- [ ] 시스템 콜을 Handler 모드가 아니라 특권 Thread 모드에서 돌리는 이유를 말할 수 있다
- [ ] Zephyr `__syscall`에서 생성되는 파일·함수 이름을 안다
- [ ] Zephyr user 스레드가 객체 권한 없이 syscall을 하면 무엇이 나는지 안다
- [ ] 세 경로의 비용을 숫자(이 노트 측정, QEMU icount 기준)로 비교할 수 있다
- [ ] 커널 경계를 둘지 말지 프로젝트 조건으로 판단할 수 있다

---

## 참고 자료

- FreeRTOS-Kernel V11.3.2 소스(랩 `/var/rtoslab/src/FreeRTOS-Kernel`): `include/mpu_wrappers.h`, `include/mpu_syscall_numbers.h`, `include/mpu_prototypes.h`, `include/FreeRTOS.h`(79–85행 MPU 기본값), `portable/Common/mpu_wrappers.c`(v1), `portable/Common/mpu_wrappers_v2.c`(v2 Impl·객체 풀·`uxSystemCallImplementations`), `portable/GCC/ARM_CM3_MPU/port.c`(`vPortSVCHandler`, `vSystemCallEnter`, `vSystemCallExit`, `prvSetupMPU`, `vPortStoreTaskMPUSettings`, ACL 함수), `portable/GCC/ARM_CM3_MPU/mpu_wrappers_v2_asm.c`, `examples/template_configuration/FreeRTOSConfig.h`(MPU 설정 설명)
- FreeRTOS `History.txt`(같은 트리): V10.6.0(2023-07-13) MPU wrappers v2 도입, V10.6.2(2023-11-29) ACL·단일 SVC 진입, V11.1.0(2024-04-22) Cortex-M0 MPU·ARMv7-R MPU 포트, V11.3.2(2026-10-16) ARMv7-M MPU 포트 SVCall 우선순위 조정
- FreeRTOS 공식 사이트 https://www.freertos.org/ — FreeRTOS-MPU 문서 페이지(정확한 URL은 사이트 개편이 잦아 적지 않는다)
- FreeRTOS-Kernel GitHub: https://github.com/FreeRTOS/FreeRTOS-Kernel
- Zephyr v4.4.2 소스(랩 `/var/rtoslab/zephyrproject/zephyr`): `include/zephyr/arch/arm/syscall.h`(`arch_syscall_invoke1`, `arch_is_user_context`), `arch/arm/core/cortex_m/svc.S`(`z_arm_svc`), `arch/arm/core/userspace.S`(`z_arm_do_syscall`), `kernel/sem.c`(`z_vrfy_k_sem_give`), `include/zephyr/sys/kobject.h`(`k_object_access_grant`), `include/zephyr/app_memory/mem_domain.h`, `scripts/build/gen_syscalls.py`
- Zephyr 문서 "User Mode": https://docs.zephyrproject.org/latest/kernel/usermode/index.html (System Calls, Kernel Objects, Memory Protection Design 하위 페이지)
- Arm, "ARMv7-M Architecture Reference Manual" (DDI 0403) — CONTROL 레지스터, SVCall, PMSAv7 MPU(RBAR·RASR·AP 인코딩), MemManage 상태 레지스터
- EL E6 (이 사이트의 Linux syscall 경로 노트) — 비교 대상
- GNU gperf 3.1: https://ftp.gnu.org/gnu/gperf/ (Zephyr userspace 빌드 의존성, 이 노트에서 직접 빌드)
