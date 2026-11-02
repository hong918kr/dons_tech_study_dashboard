# D5. 가벼운 신호 수단 — task notification · event group · stream buffer · k_event · k_poll

> **이 노트를 다 읽으면**: FreeRTOS task notification을 binary / counting semaphore · 이벤트 비트 · mailbox 네 가지로 쓰고 TCB 안의 `ulNotifiedValue[]`·`ucNotifyState[]`가 어떻게 바뀌는지 설명할 수 있다 · 같은 핑퐁을 semaphore / notification / event group으로 바꿔 잰 숫자(QEMU icount)로 "notification이 몇 % 빠른가, 문서의 45%는 무엇과 비교한 숫자인가"를 정직하게 말할 수 있다 · event group의 wait-all · clear-on-exit · `xEventGroupSync`, 24비트 한계, `xEventGroupSetBitsFromISR`가 타이머 데몬으로 미뤄지는 이유와 그 지연을 숫자로 보여 줄 수 있다 · stream / message buffer의 trigger level을 UART 수신에 맞게 고르고, single-writer single-reader 가정을 깨면 무엇이 부서지는지 재현할 수 있다 · Zephyr `k_event`·`k_poll`의 의미 차이(브로드캐스트, state 재초기화, signal 하나에 poller 하나)를 알고 상황별로 고를 수 있다
> **왜 필요한가**: 드론 FC·모터 컨트롤러 면접에서 "How would you signal a task from an ISR with minimum overhead?", "Event group vs semaphore?", "How do you get UART bytes from an ISR to a parser task?"가 단골이고, 실무에서는 IMU data-ready → rate loop, DMA 완료 → 드라이버 태스크, GNSS NMEA 바이트 → 파서 같은 경로를 전부 이 도구들로 짠다.
> **Don 기준 난이도**: 비트마스크 · doorbell 레지스터 · 링버퍼 · ISR → 백그라운드 루프 신호는 SSD 펌웨어에서 매일 하던 일이다 / 그것들이 RTOS 객체로 어떻게 포장되어 있는지, 각 객체의 "누가 기다릴 수 있나 · ISR에서 무엇을 할 수 있나 · 얼마나 드나"가 서로 어떻게 다른지, 그리고 FreeRTOS와 Zephyr가 같은 문제를 다르게 푼 지점을 새로 배운다
> **선행 노트**: A5(API 로제타 — 이름 대응표만 거기서 봤다), C1(커널 자료구조 — 이벤트 리스트·ready 리스트), C2(컨텍스트 스위치 — PendSV 비용), C7(크리티컬 섹션). 같은 모듈: D1(세마포어), D4(메시지 큐 · 큐 셋), D6(소프트웨어 타이머 — 타이머 데몬), D7(작업 위임 — `xTimerPendFunctionCallFromISR`), D8(락 없는 SPSC 링버퍼), D10(Zephyr `k_fifo`·`k_msgq`·`k_pipe`·zbus)

---

## 0. 큰 그림 — 큐는 "택배", 이 노트의 도구들은 "초인종"이다

D4의 메시지 큐는 데이터를 **복사해서** 옮기는 범용 도구다. 큐 하나에는 저장 영역, 보내려는 태스크 리스트, 받으려는 태스크 리스트, 잠금 카운터가 다 들어 있다. FreeRTOS의 binary semaphore조차 "항목 크기 0인 큐"라서 이 무게를 그대로 진다(A5 §7에서 `queueYIELD_IF_USING_PREEMPTION` 경로를 봤다). 그런데 펌웨어에서 오가는 신호의 대부분은 데이터가 아니라 **"일어났다"는 사실 하나**다. IMU가 샘플을 준비했다, DMA가 끝났다, 바이트가 몇 개 쌓였다, 센서 세 개가 다 준비됐다.

Don의 SSD 펌웨어로 바꿔 보면 이렇다. 호스트가 NVMe SQ tail doorbell을 쓰면 컨트롤러는 그 레지스터 하나로 "새 명령이 있다"를 안다. 명령 자체는 이미 메모리(SQ)에 있다. NAND 채널 완료는 status 레지스터의 비트로 오고, 펌웨어 메인 루프는 "채널 0~7 중 어느 비트가 섰나"를 본다. 호스트 DMA로 들어오는 데이터는 링버퍼에 쌓이고 head/tail만 움직인다. 이 노트의 네 도구가 정확히 이 세 모양이다.

- **task notification** = 태스크마다 붙어 있는 doorbell 레지스터 하나(32비트). 받는 쪽이 정해져 있어서 따로 객체를 만들 필요가 없다.
- **event group / `k_event`** = status 레지스터. 비트 여러 개, 여러 태스크가 서로 다른 비트 조합을 기다릴 수 있다.
- **stream / message buffer** = 생산자 1, 소비자 1인 바이트 링버퍼 + "몇 바이트 이상 쌓이면 깨워라"(trigger level).
- **`k_poll`** = 메인 루프의 "여러 소스 중 아무거나 준비되면" — Zephyr에서 세마포어 · FIFO · signal을 한 번에 기다린다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">무거운 큐 대신 쓰는 가벼운 신호 수단 4가지 (FreeRTOS · Zephyr)</text>
<rect x="15.0" y="50.0" width="160.0" height="56.0" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="95.0" y="74.0" font-size="13" text-anchor="middle">IMU data-ready ISR</text>
<text x="95.0" y="91.0" font-size="13" text-anchor="middle">(1~8 kHz)</text>
<rect x="15.0" y="130.0" width="160.0" height="56.0" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="95.0" y="154.1" font-size="13" text-anchor="middle">DMA 완료 ISR</text>
<text x="95.0" y="171.1" font-size="13" text-anchor="middle">SPI/UART</text>
<rect x="15.0" y="210.0" width="160.0" height="56.0" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="95.0" y="234.1" font-size="13" text-anchor="middle">UART RX ISR</text>
<text x="95.0" y="251.1" font-size="13" text-anchor="middle">GNSS NMEA 바이트</text>
<rect x="15.0" y="290.0" width="160.0" height="56.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="95.0" y="314.1" font-size="13" text-anchor="middle">센서 태스크 3개</text>
<text x="95.0" y="331.1" font-size="13" text-anchor="middle">gyro · acc · baro</text>
<rect x="245.0" y="50.0" width="190.0" height="56.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340.0" y="74.0" font-size="13" text-anchor="middle">task notification</text>
<text x="340.0" y="91.0" font-size="13" text-anchor="middle">TCB 안 32비트 값 1개</text>
<rect x="245.0" y="130.0" width="190.0" height="56.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340.0" y="154.1" font-size="13" text-anchor="middle">notification (eSetBits)</text>
<text x="340.0" y="171.1" font-size="13" text-anchor="middle">half / full 완료 비트</text>
<rect x="245.0" y="210.0" width="190.0" height="56.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340.0" y="234.1" font-size="13" text-anchor="middle">stream buffer</text>
<text x="340.0" y="251.1" font-size="13" text-anchor="middle">1 writer · 1 reader 링</text>
<rect x="245.0" y="290.0" width="190.0" height="56.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340.0" y="314.1" font-size="13" text-anchor="middle">event group</text>
<text x="340.0" y="331.1" font-size="13" text-anchor="middle">24비트 + 대기자 리스트</text>
<rect x="505.0" y="50.0" width="160.0" height="56.0" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="585.0" y="74.0" font-size="13" text-anchor="middle">rate loop 태스크</text>
<text x="585.0" y="91.0" font-size="13" text-anchor="middle">수신자 정확히 1개</text>
<rect x="505.0" y="130.0" width="160.0" height="56.0" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="585.0" y="154.1" font-size="13" text-anchor="middle">드라이버 태스크</text>
<text x="585.0" y="171.1" font-size="13" text-anchor="middle">비트로 원인 구분</text>
<rect x="505.0" y="210.0" width="160.0" height="56.0" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="585.0" y="234.1" font-size="13" text-anchor="middle">NMEA 파서 태스크</text>
<text x="585.0" y="251.1" font-size="13" text-anchor="middle">trigger level로 깨움</text>
<rect x="505.0" y="290.0" width="160.0" height="56.0" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="585.0" y="314.1" font-size="13" text-anchor="middle">fusion 태스크</text>
<text x="585.0" y="331.1" font-size="13" text-anchor="middle">wait-all 3비트</text>
<line x1="177.0" y1="78.0" x2="243.0" y2="78.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="437.0" y1="78.0" x2="503.0" y2="78.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="177.0" y1="158.0" x2="243.0" y2="158.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="437.0" y1="158.0" x2="503.0" y2="158.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="177.0" y1="238.0" x2="243.0" y2="238.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="437.0" y1="238.0" x2="503.0" y2="238.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="177.0" y1="318.0" x2="243.0" y2="318.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<line x1="437.0" y1="318.0" x2="503.0" y2="318.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — 이 노트에서 다룰 신호 경로. 왼쪽은 신호를 만드는 쪽(대부분 ISR), 가운데는 RTOS 객체, 오른쪽은 깨어나는 태스크다. 어느 도구를 고르느냐는 "받는 쪽이 몇 개인가, 데이터가 같이 가는가, 비트 조합을 기다리나"로 정해진다(§6).

이 노트의 질문은 셋이다. **(1) 의미가 어떻게 다른가** — 누가 기다릴 수 있고, 신호가 쌓이는지(count) 덮이는지(bit), ISR에서 무엇을 할 수 있는지. **(2) 얼마나 가벼운가** — 같은 핑퐁을 바꿔 가며 cycle과 바이트로 잰다. **(3) 커널 안에서 어떻게 구현됐나** — `tasks.c`, `event_groups.c`, `stream_buffer.c`, Zephyr `kernel/events.c`·`kernel/poll.c`.

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 25 MHz) + FreeRTOS V11.3.2(ARM_CM3 포트, 랩 템플릿) / Zephyr v4.4.2(`mps2/an385`, 같은 가상 보드). 타이밍은 전부 **`ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 실행마다 같은 숫자)** 로 쟀고, 시계는 MPS2 FPGAIO COUNTER(0x40028018, 25 MHz — 1 cycle = 40 ns = 명령 1.25개)다. **QEMU icount 기준이므로 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 · 예외 스태킹 사이클을 모델링하지 않는다). 비교는 같은 조건에서의 **상대값**으로 읽는다. Zephyr는 `qemu-system-arm`을 직접 `-icount shift=5,align=off,sleep=off`로 돌려 FreeRTOS와 같은 시간 척도로 맞췄다.

> 작성 메모: 이 노트를 쓰는 동안 VM 루트 파일시스템이 ext4 오류로 emergency read-only가 되어 `/var/tmp/rtos/D5`를 만들 수 없었다. 그래서 빌드 산출물은 VM의 tmpfs `/tmp/rtos/D5/`에 두었다(재부팅하면 사라진다). 명령의 `OUT=` 경로만 다르고 나머지는 랩 규칙과 같다.

예제 빌드·실행은 전부 이 작은 스크립트로 했다(VM 안에서 실행).

```sh
#!/bin/bash
# run.sh <name> [EXTRA_CFLAGS] [timeout]  — ~/.rtos_scratch/D5/<name>/main.c 빌드 → ICOUNT=1로 실행
N=$1; X=$2; TO=${3:-30}; O=/tmp/rtos/D5/${N}${TAG}
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D5/$N/main.c OUT=$O EXTRA_CFLAGS="$X" ${MKX} || exit 9
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/app.elf $TO
```

---

## 1. Task notification — TCB 안에 박힌 32비트 doorbell

### 1.1 직관: 객체를 따로 만들지 않는 신호

세마포어로 ISR → 태스크 신호를 보내려면 (1) 세마포어 객체를 만들고(heap에서 96 B, §2.3), (2) 태스크는 그 객체의 대기 리스트에 자기를 걸고, (3) ISR은 그 객체를 통해 대기 리스트 맨 앞의 태스크를 깨운다. 받는 태스크가 **정해져 있다면** 중간 객체는 필요 없다. ISR이 "IMU 태스크야, 일어나"라고 그 태스크의 TCB에 직접 쓰면 된다. 이것이 FreeRTOS의 **direct-to-task notification**(줄여서 task notification)이다. FreeRTOS 8.2.0에서 들어왔고, 10.4.0부터 태스크마다 값이 하나가 아니라 **배열**이 되었다(`task.h`의 Backward compatibility 설명).

비유하면 세마포어는 "공용 우편함"이고 notification은 "그 사람 책상 위 초인종"이다. 우편함은 누구나 열어 볼 수 있지만 초인종은 주인 한 명만 듣는다. 그래서 가볍고, 그래서 한계도 분명하다(§1.6).

### 1.2 정의: 필드 두 개와 API 묶음

`tasks.c`의 TCB 정의(438~439행)에 이 두 줄이 있다.

```c
volatile uint32_t ulNotifiedValue[ configTASK_NOTIFICATION_ARRAY_ENTRIES ];
volatile uint8_t ucNotifyState[ configTASK_NOTIFICATION_ARRAY_ENTRIES ];
```

- `configTASK_NOTIFICATION_ARRAY_ENTRIES`: 태스크마다 notification이 몇 개인가. `FreeRTOS.h`에서 정의하지 않으면 **1**, 1보다 작으면 `#error`. 인덱스 하나당 TCB가 값 4 B + 상태 1 B만큼(정렬 포함) 커진다(§2.3에서 96 → 100 → 112 → 132 B로 확인).
- `ulNotifiedValue[i]`: 32비트 값. 용법에 따라 카운터 · 비트 집합 · 값(mailbox)이 된다.
- `ucNotifyState[i]`: `taskNOT_WAITING_NOTIFICATION`(0) · `taskWAITING_NOTIFICATION`(1) · `taskNOTIFICATION_RECEIVED`(2) (`tasks.c` 113~115행). 값과 별개로 "알림이 왔는데 아직 안 읽었나"를 기억한다.
- `configUSE_TASK_NOTIFICATIONS`: 0으로 두면 이 필드와 API가 통째로 빠진다(태스크당 5 B 절약). 기본 1.

API는 이름이 많아 보이지만 실제 커널 함수는 아래 표 셋째 칸의 7개뿐이고 나머지는 전부 `task.h`의 매크로다.

| 사용자 API (인덱스 0) | Indexed 판 | 실제 커널 함수 | 하는 일 |
|---|---|---|---|
| `xTaskNotifyGive(t)` | `xTaskNotifyGiveIndexed(t, i)` | `xTaskGenericNotify(t, i, 0, eIncrement, NULL)` | 값 +1 (semaphore give) |
| `vTaskNotifyGiveFromISR(t, &w)` | `vTaskNotifyGiveIndexedFromISR` | `vTaskGenericNotifyGiveFromISR` | ISR에서 값 +1 |
| `ulTaskNotifyTake(clr, to)` | `ulTaskNotifyTakeIndexed(i, clr, to)` | `ulTaskGenericNotifyTake` | 값이 0이면 블록, 반환 후 0으로(binary) 또는 −1(counting) |
| `xTaskNotify(t, v, act)` | `xTaskNotifyIndexed` | `xTaskGenericNotify` | eAction대로 값 갱신 |
| `xTaskNotifyAndQuery(t, v, act, &prev)` | `xTaskNotifyAndQueryIndexed` | `xTaskGenericNotify` | 갱신 + 이전 값 반환 |
| `xTaskNotifyFromISR(...)` | `xTaskNotifyIndexedFromISR` | `xTaskGenericNotifyFromISR` | ISR판 |
| `xTaskNotifyWait(in, out, &v, to)` | `xTaskNotifyWaitIndexed(i, ...)` | `xTaskGenericNotifyWait` | 진입 시 / 퇴장 시 비트 클리어, 값 반환 |
| `xTaskNotifyStateClear(t)` | `xTaskNotifyStateClearIndexed` | `xTaskGenericNotifyStateClear` | RECEIVED → NOT_WAITING |
| `ulTaskNotifyValueClear(t, bits)` | `ulTaskNotifyValueClearIndexed` | `ulTaskGenericNotifyValueClear` | 지정 비트만 클리어, 이전 값 반환 |

인덱스 없는 이름은 전부 `tskDEFAULT_INDEX_TO_NOTIFY`(`task.h` 86행, 값 `0`)를 넘긴다. eAction은 다섯 가지다(`task.h` 122~126행).

| eAction | 값 갱신 | 용도 | 실패할 수 있나 |
|---|---|---|---|
| `eNoAction` | 그대로 | "깨우기만" (stream buffer가 내부에서 쓴다) | 아니오 |
| `eSetBits` | `value OR= ulValue` | 이벤트 비트 (event group 대용) | 아니오 |
| `eIncrement` | `value++` | counting semaphore (`xTaskNotifyGive`가 이것) | 아니오 |
| `eSetValueWithOverwrite` | `value = ulValue` | 최신 값만 의미 있는 mailbox (덮어쓰기) | 아니오 |
| `eSetValueWithoutOverwrite` | 상태가 RECEIVED가 **아니면** `value = ulValue` | 길이 1 큐처럼 (안 읽은 값 보호) | 예: `pdFAIL` |

### 1.3 상태 기계

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="330.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">ucNotifyState[i] — 인덱스마다 상태 3개 (tasks.c)</text>
<rect x="30.0" y="110.0" width="190.0" height="64.0" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="125.0" y="138.1" font-size="13" text-anchor="middle" font-weight="bold">NOT_WAITING (0)</text>
<text x="125.0" y="155.1" font-size="13" text-anchor="middle">초기값 · 소비 후</text>
<rect x="440.0" y="40.0" width="190.0" height="64.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="535.0" y="68.0" font-size="13" text-anchor="middle" font-weight="bold">WAITING (1)</text>
<text x="535.0" y="85.0" font-size="13" text-anchor="middle">Take/Wait로 블록 중</text>
<rect x="440.0" y="190.0" width="190.0" height="64.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="535.0" y="218.1" font-size="13" text-anchor="middle" font-weight="bold">RECEIVED (2)</text>
<text x="535.0" y="235.1" font-size="13" text-anchor="middle">알림 도착 · 아직 안 읽음</text>
<line x1="222.0" y1="125.0" x2="438.0" y2="75.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="318.0" y="86.0" font-size="12" text-anchor="middle">Take/Wait, 값 0이면 블록</text>
<line x1="535.0" y1="106.0" x2="535.0" y2="188.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="527.0" y="152.0" font-size="12" text-anchor="end">Notify → Ready로</text>
<line x1="222.0" y1="160.0" x2="438.0" y2="215.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="300.0" y="210.0" font-size="12" text-anchor="middle">Notify (대기 안 할 때)</text>
<line x1="438.0" y1="240.0" x2="130.0" y2="240.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<line x1="130.0" y1="240.0" x2="130.0" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="285.0" y="258.0" font-size="12" text-anchor="middle">Take/Wait가 읽고 반환 → NOT_WAITING</text>
<line x1="438.0" y1="60.0" x2="125.0" y2="60.0" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
<line x1="125.0" y1="60.0" x2="125.0" y2="108.0" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
<text x="280.0" y="52.0" font-size="12" text-anchor="middle">timeout → NOT_WAITING</text>
<text x="330.0" y="290.0" font-size="12" text-anchor="middle">eSetValueWithoutOverwrite는 상태가 RECEIVED면 pdFAIL (값이 아니라 상태로 판단)</text>
</svg>
```
그림 2 — 인덱스 하나의 상태 전이. 송신자는 값을 바꾸고 상태를 무조건 RECEIVED로 만든다. 원래 상태가 WAITING이었을 때만 태스크를 ready 리스트로 옮긴다. 받는 쪽(Take/Wait)이 값을 읽고 나면 NOT_WAITING으로 돌아간다.

핵심은 **값과 상태가 따로 논다**는 점이다. `ulTaskNotifyTake`는 "값이 0인가"로 블록 여부를 정하고, `xTaskNotifyWait`는 "상태가 RECEIVED인가"로 정한다. 그래서 같은 인덱스를 두 방식으로 섞어 쓰면 헷갈린다(§9 표).

### 1.4 예제 1 — notification을 semaphore로: pdTRUE와 pdFALSE, 인덱스 독립성

**무엇을 확인하나**: 받는 태스크가 아직 블록되기 전에 3번 give하면 값이 3으로 쌓이는지, `ulTaskNotifyTake(pdFALSE)`(counting)와 `pdTRUE`(binary)의 반환값 차이, 타임아웃, 인덱스 1에 준 알림이 인덱스 0 대기자를 깨우지 않는지. `configTASK_NOTIFICATION_ARRAY_ENTRIES=2`로 빌드했다.

```c
/* ex1: task notification = 가벼운 binary/counting semaphore. pdTRUE vs pdFALSE, 인덱스 독립성 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
static TaskHandle_t hRx;
static void rx(void *pv) {
    (void)pv;
    vTaskDelay(1);                                   /* tx가 먼저 3번 give 하도록 */
    uint32_t v = ulTaskNotifyTake(pdFALSE, 0);       /* counting: 1 감소, 감소 전 값 반환 */
    board_printf_raw("take(pdFALSE) -> %u, left=%u\n", (unsigned)v,
                     (unsigned)ulTaskNotifyValueClear(NULL, 0));  /* 0 비트 지우기 = 값 읽기 */
    xTaskNotifyGive(xTaskGetCurrentTaskHandle());    /* 자기에게 3번 다시 */
    xTaskNotifyGive(xTaskGetCurrentTaskHandle());
    xTaskNotifyGive(xTaskGetCurrentTaskHandle());
    v = ulTaskNotifyTake(pdTRUE, 0);                 /* binary: 0으로 클리어, 이전 값 반환 */
    board_printf_raw("take(pdTRUE)  -> %u, left=%u\n", (unsigned)v,
                     (unsigned)ulTaskNotifyValueClear(NULL, 0));
    v = ulTaskNotifyTake(pdTRUE, 5);                 /* 아무도 안 주면 5 tick 뒤 0 */
    board_printf_raw("take timeout  -> %u at tick %u\n", (unsigned)v, (unsigned)xTaskGetTickCount());
    /* 인덱스 1에 give → 인덱스 0 대기자는 깨지 않는다 */
    xTaskNotifyGiveIndexed(xTaskGetCurrentTaskHandle(), 1);
    v = ulTaskNotifyTakeIndexed(0, pdTRUE, 3);
    board_printf_raw("idx0 take -> %u (idx1 give 무시됨), idx1 take -> %u\n", (unsigned)v,
                     (unsigned)ulTaskNotifyTakeIndexed(1, pdTRUE, 0));
    board_printf_raw("ENTRIES=%u, sizeof(StaticTask_t)=%u\n",
                     (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES, (unsigned)sizeof(StaticTask_t));
    board_exit(0);
}
static void tx(void *pv) {
    (void)pv;
    for (int i = 0; i < 3; i++) xTaskNotifyGive(hRx);   /* rx는 아직 delay 중: 값만 3이 된다 */
    board_printf_raw("tx: gave 3 times, rx state=%d (2=Blocked)\n", (int)eTaskGetState(hRx));
    vTaskDelete(NULL);
}
int main(void) {
    xTaskCreate(rx, "rx", 256, NULL, 2, &hRx);
    xTaskCreate(tx, "tx", 256, NULL, 1, NULL);
    vTaskStartScheduler();
}
```

```sh
./run.sh ex1 "-DconfigTASK_NOTIFICATION_ARRAY_ENTRIES=2"
```

```text
tx: gave 3 times, rx state=2 (2=Blocked)
take(pdFALSE) -> 3, left=2
take(pdTRUE)  -> 5, left=0
take timeout  -> 0 at tick 6
idx0 take -> 0 (idx1 give 무시됨), idx1 take -> 1
ENTRIES=2, sizeof(StaticTask_t)=100
[board_exit] code=0
```

**출력에서 볼 것**: rx가 `vTaskDelay`로 Blocked인 동안 받은 3번의 give는 **값 3으로 쌓였다**(binary semaphore였다면 2, 3번째 give는 실패한다 — D1). `take(pdFALSE)`는 감소 전 값 3을 반환하고 2를 남긴다. 다시 3을 더해 5가 된 뒤 `take(pdTRUE)`는 5를 반환하고 0으로 만든다. 즉 binary semaphore로 쓸 때도 반환값은 "그동안 몇 번 신호가 왔나"를 알려 준다 — ISR이 두 번 울렸는데 태스크가 한 번만 처리하는 버그를 반환값으로 잡을 수 있다. 타임아웃은 5 tick 뒤(tick 1에서 시작해 6) 0을 반환했다. 인덱스 1에 준 알림은 인덱스 0의 `Take`를 깨우지 못하고 3 tick 타임아웃 뒤 인덱스 1에서 그대로 1로 읽혔다.

### 1.5 예제 2 — eAction 다섯 가지와 xTaskNotifyWait의 두 마스크

**무엇을 확인하나**: 값 하나를 비트 집합 · 카운터 · mailbox로 바꿔 쓰는 모습, `eSetValueWithoutOverwrite`가 **상태**를 보고 실패하는 것, `xTaskNotifyWait(ulBitsToClearOnEntry, ulBitsToClearOnExit, ...)`의 진입 마스크가 "대기 중인 알림이 없을 때만" 적용되는 것. 값 읽기에는 `ulTaskNotifyValueClear(me, 0)`(0비트 클리어 = 상태를 안 바꾸고 읽기)를 썼다. `xTaskNotifyAndQuery(..., eNoAction, ...)`로 읽으면 상태가 RECEIVED로 바뀌어 다음 실험을 망친다 — 처음에 그렇게 짰다가 결과가 이상해서 바꿨다.

```c
/* ex2: eAction 5가지와 xTaskNotifyWait의 clear 마스크 — 값 하나로 bits / mailbox / count */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
static TaskHandle_t me;
#define P(fmt, ...) board_printf_raw(fmt, ##__VA_ARGS__)
static void show(const char *what, BaseType_t r) {
    uint32_t now = ulTaskNotifyValueClear(me, 0);    /* 0 비트 클리어 = 상태 안 바꾸고 읽기 */
    P("%s: ret=%d value=0x%x\n", what, (int)r, (unsigned)now);
}
static void t(void *pv) {
    (void)pv; uint32_t v;
    me = xTaskGetCurrentTaskHandle();
    show("eSetBits 0x01", xTaskNotify(me, 0x01, eSetBits));
    show("eSetBits 0x80", xTaskNotify(me, 0x80, eSetBits));
    show("eIncrement", xTaskNotify(me, 0, eIncrement));
    show("eSetValueWithOverwrite 0x1234", xTaskNotify(me, 0x1234, eSetValueWithOverwrite));
    show("eSetValueWithoutOverwrite 0x5678", xTaskNotify(me, 0x5678, eSetValueWithoutOverwrite));
    /* 대기: 진입 시 0 클리어 안 함, 퇴장 시 0xFFFFFFFF 클리어 → mailbox 소비 */
    xTaskNotifyWait(0, 0xFFFFFFFFu, &v, 0);
    P("wait(entry=0, exit=all) got 0x%x\n", (unsigned)v);
    show("after wait", pdPASS);
    show("eSetValueWithoutOverwrite 0x5678", xTaskNotify(me, 0x5678, eSetValueWithoutOverwrite));
    /* bits 용법: exit 마스크로 처리한 비트만 지운다 */
    xTaskNotify(me, 0x0F, eSetBits);                 /* 0x5678 | 0x0F */
    xTaskNotifyWait(0, 0x0000000Fu, &v, 0);
    P("wait(exit=0x0F) got 0x%x\n", (unsigned)v);
    show("after wait(exit=0x0F)", pdPASS);
    /* entry 마스크: '대기 중인 알림이 없을 때만' 블록 전에 지운다 */
    xTaskNotify(me, 0x100, eSetBits);                /* 0x5770, state=RECEIVED */
    BaseType_t r = xTaskNotifyWait(0xFFFFFFFFu, 0, &v, 2);
    P("pending + wait(entry=all) ret=%d v=0x%x tick=%u\n", (int)r, (unsigned)v, (unsigned)xTaskGetTickCount());
    r = xTaskNotifyWait(0xFFFFFFFFu, 0, &v, 2);      /* 이번엔 pending 없음 → 0x5770 지우고 블록 */
    P("no pending + wait(entry=all) ret=%d v=0x%x tick=%u\n", (int)r, (unsigned)v, (unsigned)xTaskGetTickCount());
    board_exit(0);
}
int main(void) {
    xTaskCreate(t, "t", 384, NULL, 2, NULL);
    vTaskStartScheduler();
}
```

```text
eSetBits 0x01: ret=1 value=0x1
eSetBits 0x80: ret=1 value=0x81
eIncrement: ret=1 value=0x82
eSetValueWithOverwrite 0x1234: ret=1 value=0x1234
eSetValueWithoutOverwrite 0x5678: ret=0 value=0x1234
wait(entry=0, exit=all) got 0x1234
after wait: ret=1 value=0x0
eSetValueWithoutOverwrite 0x5678: ret=1 value=0x5678
wait(exit=0x0F) got 0x567f
after wait(exit=0x0F): ret=1 value=0x5670
pending + wait(entry=all) ret=1 v=0x5770 tick=0
no pending + wait(entry=all) ret=0 v=0x0 tick=2
[board_exit] code=0
```

**출력에서 볼 것**:

1. `eSetBits`는 OR(0x1 → 0x81), `eIncrement`는 +1(0x82), `eSetValueWithOverwrite`는 무조건 덮는다(0x1234).
2. 바로 뒤의 `eSetValueWithoutOverwrite`는 **ret=0(pdFAIL)**, 값 0x1234 유지. 상태가 RECEIVED(안 읽음)라서다. 한 번 `Wait`로 소비한 뒤에는 값이 0이든 아니든 성공한다 — 판단 기준이 값이 아니라 상태다.
3. `wait(exit=0x0F)`는 0x567f를 돌려주고 아래 4비트만 지워 0x5670을 남긴다. 이벤트 비트 용법에서 "내가 처리한 비트만 지우고 나머지는 다음 루프에 처리"하는 패턴이다.
4. 진입 마스크 0xFFFFFFFF: 알림이 pending(RECEIVED)이면 **지우지 않고** 즉시 반환(0x5770, tick 0). pending이 없으면 블록 전에 값을 지운다 — 그래서 두 번째 호출은 남아 있던 0x5770을 버리고 2 tick 타임아웃 뒤 0을 반환했다. 진입 마스크는 "블록하기 전 오래된 비트를 버리는" 용도이고, 잘못 쓰면 **설정됐지만 상태는 이미 소비된 비트를 잃는다**.

### 1.6 한계 — 가벼운 대가

- **받는 쪽은 정확히 태스크 1개.** 핸들로 지정하므로 "여러 태스크 중 아무나" 같은 일은 못 한다. 두 태스크를 동시에 깨우려면 각각에게 따로 보낸다(브로드캐스트 없음 — event group이나 `k_event`를 쓴다).
- **ISR은 받는 쪽이 될 수 없다.** ISR에는 TCB가 없다. 보내는 것만 가능(`...FromISR`).
- **보내는 쪽은 블록하지 못한다.** 큐처럼 "자리가 날 때까지 기다리며 보내기"가 없다. `eSetValueWithoutOverwrite`가 `pdFAIL`을 돌려줄 뿐이고, 재시도는 보내는 쪽 몫이다.
- **버퍼링이 없다.** 값 하나(32비트)다. 카운터로 쓰면 횟수는 세지만 데이터는 못 쌓는다.
- **인덱스 0은 공용일 수 있다.** FreeRTOS V11.3.2 커널 안에서 notification을 내부적으로 쓰는 곳은 `stream_buffer.c` 하나뿐이다(커널 소스 전체를 grep — `tasks.c`·MPU 래퍼 말고 `xTaskNotify` 계열을 부르는 파일은 이것뿐). stream / message buffer는 블록할 때 `xTaskNotifyWaitIndexed(pxStreamBuffer->uxNotificationIndex, ...)`로 기다리고, 그 인덱스 기본값이 `tskDEFAULT_INDEX_TO_NOTIFY` = 0이다(`prvInitialiseNewStreamBuffer`, 1654행). V11.1.0부터 `vStreamBufferSetStreamBufferNotificationIndex()`로 바꿀 수 있다(History.txt "Add the ability to change task notification index for stream buffers"). 같은 태스크가 인덱스 0을 직접 쓰면서 stream buffer도 받으면, 버퍼를 기다리다 **엉뚱한 알림에 깨어나거나**(커널은 깬 뒤 바이트 수를 다시 확인하니 0 반환으로 끝난다) 반대로 **stream buffer가 블록 전에 `xTaskNotifyStateClearIndexed`로 내 알림 상태를 지운다**. 커널 밖 라이브러리(FreeRTOS+TCP 등)가 어느 인덱스를 쓰는지는 이 랩에 소스가 없어 확인 못 함 — 쓰는 라이브러리의 문서를 확인하고, 내 용도는 인덱스 1부터 쓰는 습관이 안전하다.

---

## 2. 얼마나 가벼운가 — 같은 핑퐁을 세 가지로 재기

### 2.1 예제 3 — semaphore / notification / event group, 세 경로 × 세 설정

**무엇을 확인하나**: 우선순위 3인 H가 신호를 기다리고 우선순위 1인 L이 계속 신호를 준다. (a) **task→task**: L이 `t0`를 찍고 give → H가 깨어나 선점 → H가 `CYC() − t0`를 잰다(API + 깨움 + PendSV 전환 전부). (b) **ISR→task**: L이 NVIC ISPR로 TIMER0 IRQ를 pending시키면 ISR이 FromISR로 give, `portYIELD_FROM_ISR`. (c) **give+take**: 기다리는 태스크 없이 give 하고 바로 take(0) — 전환 없는 순수 API 비용. 각 1000번 평균. `MODE` 0/1/2로 같은 코드를 세 객체로 바꾼다.

```c
/* ex3: 같은 핑퐁을 binary semaphore(0) / task notification(1) / event group(2)로 — task→task, ISR→task, 무대기 give+take */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"
#include "board.h"
#define CYC() (*(volatile uint32_t *)0x40028018u)      /* FPGAIO 25 MHz counter */
#define NVIC_ISPR0 REG32(0xE000E200u)                  /* set-pending 레지스터 */
#define N 1000
#if MODE == 0
#define WAIT(to)    xSemaphoreTake(sem, to)
#define GIVE()      xSemaphoreGive(sem)
#define GIVE_ISR(w) xSemaphoreGiveFromISR(sem, w)
#elif MODE == 1
#define WAIT(to)    ulTaskNotifyTake(pdTRUE, to)
#define GIVE()      xTaskNotifyGive(hH)
#define GIVE_ISR(w) vTaskNotifyGiveFromISR(hH, w)
#else
#define WAIT(to)    xEventGroupWaitBits(eg, 1, pdTRUE, pdFALSE, to)
#define GIVE()      xEventGroupSetBits(eg, 1)
#define GIVE_ISR(w) xEventGroupSetBitsFromISR(eg, 1, w)  /* 타이머 데몬에 위임 */
#endif
static TaskHandle_t hH;
static SemaphoreHandle_t sem;
static EventGroupHandle_t eg;
static volatile uint32_t t0, phase;
void TIMER0_IRQHandler(void) {                         /* NVIC ISPR로 소프트웨어 트리거 */
    BaseType_t w = pdFALSE;
    GIVE_ISR(&w);
    portYIELD_FROM_ISR(w);
}
static void report(const char *what, uint32_t sum, uint32_t mx) {
    board_printf_raw("MODE=%d %s avg %u.%u cyc max %u cyc\n", MODE, what,
                     (unsigned)(sum / N), (unsigned)(sum % N * 10 / N), (unsigned)mx);
}
static void high(void *pv) {                           /* 우선순위 3: 신호를 기다린다 */
    (void)pv;
    static const char *nm[2] = {"task->task", "ISR->task "};
    uint32_t sum, mx, d;
    for (phase = 0; phase < 2; phase++) {
        sum = mx = 0;
        for (int i = 0; i < N; i++) {
            WAIT(portMAX_DELAY);
            d = CYC() - t0; sum += d; if (d > mx) mx = d;
        }
        report(nm[phase], sum, mx);
    }
    sum = mx = 0;                                      /* 대기자 없음: give + take(0) 한 쌍 */
    for (int i = 0; i < N; i++) {
        uint32_t a = CYC(); GIVE(); WAIT(0); d = CYC() - a;
        sum += d; if (d > mx) mx = d;
    }
    report("give+take ", sum, mx);
    board_exit(0);
}
static void low(void *pv) {                            /* 우선순위 1: 계속 신호를 준다 */
    (void)pv;
    for (;;) {
        t0 = CYC();
        if (phase == 0) GIVE();
        else NVIC_ISPR0 = 1u << 8;                     /* TIMER0 IRQ pending → ISR 실행 */
    }
}
int main(void) {
    sem = xSemaphoreCreateBinary(); eg = xEventGroupCreate();
    NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;          /* 0x40 ≥ MAX_SYSCALL(0x20): API 허용 */
    xTaskCreate(high, "H", 256, NULL, 3, &hH);
    xTaskCreate(low, "L", 256, NULL, 1, NULL);
    vTaskStartScheduler();
}
```

설정은 세 가지로 바꿨다. **tpl** = 랩 템플릿 그대로(`configASSERT` 켬, `configCHECK_FOR_STACK_OVERFLOW 2`, run-time stats, trace facility). **lean** = 스택 검사 · run-time stats · trace를 끔(assert는 켬). **noassert** = lean + `configASSERT` 끔(양산 빌드에 가까움). 앱 폴더에 둔 설정 파일이 템플릿 것을 감싼다.

```c
/* ex3lean: 템플릿 설정에서 측정 잡음(스택 검사, run-time stats, trace)을 뺀다 */
#ifndef D5_LEAN_CONFIG_H
#define D5_LEAN_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#undef configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW 0
#undef configGENERATE_RUN_TIME_STATS
#define configGENERATE_RUN_TIME_STATS 0
#undef configUSE_TRACE_FACILITY
#define configUSE_TRACE_FACILITY 0
#undef configUSE_STATS_FORMATTING_FUNCTIONS
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#ifdef D5_NOASSERT                    /* 양산 빌드처럼 configASSERT까지 끈다 */
#undef configASSERT
#define configCHECK_HANDLER_INSTALLATION 0
#endif
#endif
```

`configCHECK_HANDLER_INSTALLATION 0`은 assert를 끄면 `port.c`의 `pxVectorTable` 변수가 안 쓰여 `-Wunused-variable` 경고가 나서 넣었다(그 검사 블록 자체를 빼는 공식 스위치다). `ex3lean/main.c`는 `#include "../ex3/main.c"` 한 줄이다.

```sh
# all.sh — 3 설정(tpl = 랩 템플릿, lean = 스택검사·stats·trace 끔, noassert = lean + configASSERT 끔) × 3 MODE
for cfg in tpl lean noassert; do for m in 0 1 2; do
  case $cfg in tpl) D=ex3; X="-DMODE=$m";; lean) D=ex3lean; X="-DMODE=$m";; noassert) D=ex3lean; X="-DMODE=$m -DD5_NOASSERT";; esac
  TAG=_${cfg}$m /Users/donh/.rtos_scratch/D5/run.sh $D "$X" | grep MODE | sed "s/^/$cfg /"
done; done
```

```text
tpl MODE=0 task->task avg 242.3 cyc max 279 cyc
tpl MODE=0 ISR->task  avg 229.2 cyc max 266 cyc
tpl MODE=0 give+take  avg 122.5 cyc max 160 cyc
tpl MODE=1 task->task avg 196.2 cyc max 233 cyc
tpl MODE=1 ISR->task  avg 203.4 cyc max 240 cyc
tpl MODE=1 give+take  avg 88.1 cyc max 125 cyc
tpl MODE=2 task->task avg 233.1 cyc max 283 cyc
tpl MODE=2 ISR->task  avg 879.7 cyc max 928 cyc
tpl MODE=2 give+take  avg 156.9 cyc max 207 cyc
lean MODE=0 task->task avg 206.6 cyc max 244 cyc
lean MODE=0 ISR->task  avg 193.8 cyc max 231 cyc
lean MODE=0 give+take  avg 122.5 cyc max 160 cyc
lean MODE=1 task->task avg 161.0 cyc max 198 cyc
lean MODE=1 ISR->task  avg 168.1 cyc max 205 cyc
lean MODE=1 give+take  avg 88.1 cyc max 125 cyc
lean MODE=2 task->task avg 197.9 cyc max 248 cyc
lean MODE=2 ISR->task  avg 809.4 cyc max 858 cyc
lean MODE=2 give+take  avg 157.0 cyc max 194 cyc
noassert MODE=0 task->task avg 169.0 cyc max 206 cyc
noassert MODE=0 ISR->task  avg 159.4 cyc max 196 cyc
noassert MODE=0 give+take  avg 74.6 cyc max 112 cyc
noassert MODE=1 task->task avg 144.2 cyc max 181 cyc
noassert MODE=1 ISR->task  avg 138.6 cyc max 176 cyc
noassert MODE=1 give+take  avg 76.9 cyc max 114 cyc
noassert MODE=2 task->task avg 175.5 cyc max 226 cyc
noassert MODE=2 ISR->task  avg 681.1 cyc max 730 cyc
noassert MODE=2 give+take  avg 116.1 cyc max 165 cyc
```

**출력에서 볼 것**: 숫자를 "notification이 semaphore보다 몇 % 짧은가"로 정리하면 이렇다.

| 경로 | tpl (랩 기본) | lean (assert 켬) | noassert (양산형) |
|---|---|---|---|
| task→task (깨움 + 전환) | 242.3 → 196.2 (19.0% ↓) | 206.6 → 161.0 (22.1% ↓) | 169.0 → 144.2 (14.7% ↓) |
| ISR→task | 229.2 → 203.4 (11.3% ↓) | 193.8 → 168.1 (13.3% ↓) | 159.4 → 138.6 (13.0% ↓) |
| give+take (대기자 없음) | 122.5 → 88.1 (28.1% ↓) | 122.5 → 88.1 (28.1% ↓) | 74.6 → 76.9 (3.1% ↑) |
| event group, ISR→task | 879.7 | 809.4 | 681.1 |

단위는 FPGAIO cycle(40 ns). max는 평균보다 약 37 cycle 큰데, 1000번 중 SysTick이 한 번 끼어든 경우다.

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">신호 하나 전달 비용 — 양산형 설정(assert·stack 검사·stats 끔), QEMU icount</text>
<line x1="70.0" y1="300.0" x2="660.0" y2="300.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="70.0" y1="300.0" x2="70.0" y2="50.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="70" y1="300.0" x2="660" y2="300.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="304.0" font-size="12" text-anchor="end">0</text>
<line x1="70" y1="240.0" x2="660" y2="240.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="244.0" font-size="12" text-anchor="end">200</text>
<line x1="70" y1="180.0" x2="660" y2="180.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="184.0" font-size="12" text-anchor="end">400</text>
<line x1="70" y1="120.0" x2="660" y2="120.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="124.0" font-size="12" text-anchor="end">600</text>
<line x1="70" y1="60.0" x2="660" y2="60.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="64.0" font-size="12" text-anchor="end">800</text>
<text x="16.0" y="160.0" font-size="12" text-anchor="start">cyc</text>
<text x="168.3" y="318.0" font-size="12" text-anchor="middle">task→task (깨움+전환)</text>
<rect x="96.3" y="249.3" width="44.0" height="50.7" fill="#888" fill-opacity="0.55" stroke="#888"/>
<text x="118.3" y="244.3" font-size="12" text-anchor="middle">169</text>
<rect x="146.3" y="256.7" width="44.0" height="43.3" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="168.3" y="251.7" font-size="12" text-anchor="middle">144</text>
<rect x="196.3" y="247.3" width="44.0" height="52.6" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="218.3" y="242.3" font-size="12" text-anchor="middle">176</text>
<text x="365.0" y="318.0" font-size="12" text-anchor="middle">ISR→task</text>
<rect x="293.0" y="252.2" width="44.0" height="47.8" fill="#888" fill-opacity="0.55" stroke="#888"/>
<text x="315.0" y="247.2" font-size="12" text-anchor="middle">159</text>
<rect x="343.0" y="258.4" width="44.0" height="41.6" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="365.0" y="253.4" font-size="12" text-anchor="middle">139</text>
<rect x="393.0" y="95.7" width="44.0" height="204.3" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="415.0" y="90.7" font-size="12" text-anchor="middle">681</text>
<text x="561.7" y="318.0" font-size="12" text-anchor="middle">give+take (대기자 없음)</text>
<rect x="489.7" y="277.6" width="44.0" height="22.4" fill="#888" fill-opacity="0.55" stroke="#888"/>
<text x="511.7" y="272.6" font-size="12" text-anchor="middle">75</text>
<rect x="539.7" y="276.9" width="44.0" height="23.1" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="561.7" y="271.9" font-size="12" text-anchor="middle">77</text>
<rect x="589.7" y="265.2" width="44.0" height="34.8" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="611.7" y="260.2" font-size="12" text-anchor="middle">116</text>
<rect x="90.0" y="338.0" width="14.0" height="14.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
<text x="110.0" y="350.0" font-size="12" text-anchor="start">binary semaphore</text>
<rect x="260.8" y="338.0" width="14.0" height="14.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="280.8" y="350.0" font-size="12" text-anchor="start">task notification</text>
<rect x="438.5" y="338.0" width="14.0" height="14.0" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="458.5" y="350.0" font-size="12" text-anchor="start">event group</text>
<text x="340.0" y="366.0" font-size="12" text-anchor="middle">event group의 ISR→task는 타이머 데몬을 한 번 더 거친다 (§3.5)</text>
</svg>
```
그림 3 — noassert 설정의 세 경로 비교(예제 3 실측). 전환이 있는 경로에서 notification이 25 cycle 안팎 짧다. 전환이 없는 give+take에서는 차이가 없다(오히려 3% 길다). event group의 ISR 경로만 튀는데, 이것은 객체가 무거워서가 아니라 구조가 다르기 때문이다(§3.5).

### 2.2 "45% 빠르다"는 주장 — 무엇과 비교한 숫자인가

FreeRTOS 공식 문서는 "unblocking an RTOS task with a direct notification is 45% faster and uses less RAM than unblocking a task using an intermediary object such as a binary semaphore"라고 쓰고, 각주에서 **이 45%는 V8.1.2의 binary semaphore와 비교한 값이고 V8.2.0에서 개선된 semaphore와 비교하면 약 35%** 라고 밝힌다(참고 자료의 FreeRTOS task notifications 페이지). 2015년 8.2.0 릴리스 당시의 측정이다.

이 랩에서 V11.3.2로 잰 결과는 **11~22%**다(전환 포함 경로). 차이가 줄어든 이유를 숫자로 나누어 보면 이렇다.

1. **전환 비용이 공통 분모다.** task→task 144~242 cycle 중 PendSV 컨텍스트 스위치와 스케줄러 선택이 대부분이다(C1 예제 6에서 give → H 복귀가 약 192 cycle였다). API 차이(25~46 cycle)가 그 위에 얹히니 비율이 작아진다.
2. **semaphore 경로가 그동안 가벼워졌다.** V11의 `xQueueSemaphoreTake`는 세마포어 전용 함수라 복사 루틴을 타지 않고, `ulTaskNotifyTake`도 V11.2.0(2025-03)에서 "이미 알림이 있으면 스케줄러를 정지하지 않는" 최적화가 들어갔다(History.txt "Optimize xTaskNotifyWait and ulTaskNotifyTake APIs to suspend the scheduler only if the task is not already notified"). 양쪽이 같이 좋아졌다.
3. **assert가 semaphore 쪽에 더 많이 붙어 있다.** give+take에서 assert를 켜면 semaphore는 74.6 → 122.5(+48 cycle), notification은 76.9 → 88.1(+11 cycle)이다. `xQueueGenericSend`와 `xQueueSemaphoreTake`에 `configASSERT`가 많다. 개발 빌드(assert 켬)에서 28%로 보이던 차이가 양산 빌드에서는 0이 된다.
4. **QEMU는 메모리 접근이 공짜다.** 실칩에서는 큐 구조체(84 B)를 건드리는 로드/스토어가 캐시 미스나 플래시 wait state로 더 비쌀 수 있다. 그래서 실칩에서는 차이가 이 숫자보다 클 가능성이 있지만, 이 랩에서는 보여 줄 수 없다.

면접에서는 이렇게 말하면 정직하다. "문서는 최대 45%라고 하지만 그건 2015년 semaphore 기준이다. 내가 V11에서 재 보니 깨움 + 전환 경로에서 15~20% 정도, 전환 없는 경로는 거의 같았다. 진짜 이점은 객체 RAM이 0이라는 것과, 받는 쪽이 하나로 고정되는 단순함이다."

### 2.3 예제 4 — 객체 하나당 RAM, 그리고 코드 크기

**무엇을 확인하나**: 각 객체의 static 구조체 크기와, heap_4에서 동적 생성할 때 실제로 빠지는 바이트(블록 헤더 8 B + 8 B 정렬 포함). notification 인덱스 수에 따라 TCB가 얼마나 커지는지.

```c
/* ex4: 객체 하나당 RAM — static 구조체 크기와 heap_4에서 실제로 빠지는 바이트 */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"
#include "stream_buffer.h"
#include "message_buffer.h"
#include "board.h"
#define P board_printf_raw
static size_t before;
static void mark(void) { before = xPortGetFreeHeapSize(); }
static unsigned used(void) { return (unsigned)(before - xPortGetFreeHeapSize()); }
static void t(void *pv) { (void)pv; for (;;) vTaskDelay(100); }
int main(void) {
    vPortFree(pvPortMalloc(8));                       /* heap_4는 첫 malloc 때 초기화된다 */
    P("ENTRIES=%u sizeof: StaticTask_t=%u StaticSemaphore_t=%u StaticEventGroup_t=%u "
      "StaticStreamBuffer_t=%u\n", (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
      (unsigned)sizeof(StaticTask_t), (unsigned)sizeof(StaticSemaphore_t),
      (unsigned)sizeof(StaticEventGroup_t), (unsigned)sizeof(StaticStreamBuffer_t));
    mark(); SemaphoreHandle_t s = xSemaphoreCreateBinary();      P("heap: binary sem   %u B\n", used());
    mark(); SemaphoreHandle_t c = xSemaphoreCreateCounting(10, 0); P("heap: counting sem %u B\n", used());
    mark(); EventGroupHandle_t e = xEventGroupCreate();          P("heap: event group  %u B\n", used());
    mark(); QueueHandle_t q = xQueueCreate(8, 4);                P("heap: queue 8x4B   %u B\n", used());
    mark(); StreamBufferHandle_t sb = xStreamBufferCreate(64, 1); P("heap: stream 64B   %u B\n", used());
    mark(); MessageBufferHandle_t mb = xMessageBufferCreate(64);  P("heap: msgbuf 64B   %u B\n", used());
    mark(); TaskHandle_t h; xTaskCreate(t, "t", 128, NULL, 1, &h);  P("heap: task 128w    %u B (TCB+stack 512)\n", used());
    P("notification: 0 B extra (TCB 안 ulNotifiedValue[%u] + ucNotifyState[%u])\n",
      (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES, (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES);
    (void)s; (void)c; (void)e; (void)q; (void)sb; (void)mb;
    board_exit(0);
}
```

```sh
for n in 1 4; do TAG=_n$n ./run.sh ex4 -DconfigTASK_NOTIFICATION_ARRAY_ENTRIES=$n; done
```

```text
ENTRIES=1 sizeof: StaticTask_t=96 StaticSemaphore_t=84 StaticEventGroup_t=32 StaticStreamBuffer_t=40
heap: binary sem   96 B
heap: counting sem 96 B
heap: event group  40 B
heap: queue 8x4B   128 B
heap: stream 64B   120 B
heap: msgbuf 64B   120 B
heap: task 128w    624 B (TCB+stack 512)
notification: 0 B extra (TCB 안 ulNotifiedValue[1] + ucNotifyState[1])
ENTRIES=4 sizeof: StaticTask_t=112 StaticSemaphore_t=84 StaticEventGroup_t=32 StaticStreamBuffer_t=40
heap: binary sem   96 B
heap: counting sem 96 B
heap: event group  40 B
heap: queue 8x4B   128 B
heap: stream 64B   120 B
heap: msgbuf 64B   120 B
heap: task 128w    640 B (TCB+stack 512)
notification: 0 B extra (TCB 안 ulNotifiedValue[4] + ucNotifyState[4])
```

**출력에서 볼 것**: binary semaphore 하나가 heap에서 **96 B**(구조체 84 B + 헤더 8 B, 8 B 정렬), event group 40 B, 64 B stream buffer는 120 B(구조체 40 + 저장 영역 65 + 헤더 8 → 정렬)다. stream buffer는 요청 크기보다 **1바이트를 더** 잡는다 — head == tail을 "비었음"으로 쓰기 위해 한 칸을 비워 두는 링버퍼의 고전 기법이고, `xStreamBufferGenericCreate`의 주석이 "this is a quirk of the implementation"이라고 설명한다. notification은 객체가 없으니 0 B이고, 비용은 TCB 쪽에 있다: 인덱스 1 → 4로 늘리면 `StaticTask_t`가 96 → 112 B, 태스크 생성 시 heap 사용이 624 → 640 B가 된다. 같은 예제를 ENTRIES=2, 8로도 돌렸을 때 `StaticTask_t`는 각각 100 B, 132 B였다(ENTRIES=2는 예제 1의 마지막 줄에도 나온다). 인덱스 하나당 4 B 값 + 1 B 상태가 정렬되어 붙는다. 태스크 20개짜리 FC에서 semaphore 20개(1920 B)를 notification으로 바꾸면 1.9 KB를 아낀다 — STM32H7(1 MB RAM)에서는 사소하지만 32 KB짜리 Cortex-M0 센서 노드에서는 의미가 있다.

**코드 크기**도 같이 봤다(예제 3 noassert 빌드의 `arm-none-eabi-nm --size-sort -S`, 크기는 16진수 바이트).

```sh
arm-none-eabi-nm --size-sort -S ex3lean_noassert0/app.elf | grep -E "xQueueSemaphoreTake|xQueueGenericSend$|xQueueGiveFromISR"
arm-none-eabi-nm --size-sort -S ex3lean_noassert1/app.elf | grep -E "Notify"
arm-none-eabi-nm --size-sort -S ex3lean_noassert2/app.elf | grep -E "EventGroup|PendFunctionCallFromISR"
arm-none-eabi-size ex3lean_noassert0/app.elf ex3lean_noassert1/app.elf ex3lean_noassert2/app.elf
```

```text
00001670 00000076 T xQueueGiveFromISR
00001530 00000140 T xQueueGenericSend
00001804 0000014a T xQueueSemaphoreTake
0000145c 00000056 t prvNotifyQueueSetContainer
000010a4 000000b8 T ulTaskGenericNotifyTake
00001260 000000f4 T vTaskGenericNotifyGiveFromISR
0000115c 00000104 T xTaskGenericNotify
00001c80 00000004 T vEventGroupSetBitsCallback
00001b40 0000001c T xEventGroupCreate
00001c84 0000001c T xEventGroupSetBitsFromISR
00001b10 00000030 T xTimerPendFunctionCallFromISR
00001c18 00000066 T xEventGroupSetBits
00001b5c 000000bc T xEventGroupWaitBits
   text	   data	    bss	    dec	    hex	filename
  11812	    100	 135368	 147280	  23f50	ex3lean_noassert0/app.elf
  11396	    100	 135368	 146864	  23db0	ex3lean_noassert1/app.elf
  11536	    100	 135368	 147004	  23e3c	ex3lean_noassert2/app.elf
```

**출력에서 볼 것**: semaphore 경로 함수 세 개 = 0x76 + 0x140 + 0x14a = 768 B, notification 세 개 = 0xb8 + 0xf4 + 0x104 = 688 B. 전체 text는 11812 → 11396 B로 416 B 줄었다. 단 이 랩 설정은 타이머 데몬이 큐를 쓰므로 `queue.c` 자체는 어차피 링크된다 — 큐를 하나도 안 쓰는 시스템이어야 `queue.c` 전체(수 KB)를 뺄 수 있다. event group의 `xEventGroupSetBitsFromISR`는 0x1c = 28 B짜리 껍데기로, 하는 일은 `xTimerPendFunctionCallFromISR`를 부르는 것뿐이다. 콜백 `vEventGroupSetBitsCallback`도 4 B — `xEventGroupSetBits`로 꼬리 호출(tail call)만 한다.

---
## 3. Event group — 여러 태스크가 보는 status 레지스터

### 3.1 직관과 정의

notification이 "한 사람 책상 위 초인종"이라면 event group은 **게시판의 램프 24개**다. 아무나 램프를 켜고 끌 수 있고, 여러 태스크가 각자 "0번과 2번이 둘 다 켜지면 깨워 줘"(wait-all) 또는 "1번이나 3번 중 하나라도 켜지면"(wait-any)을 걸어 둘 수 있다. Don의 SSD 펌웨어라면 채널별 완료 status 레지스터를 여러 백그라운드 루틴이 각자 다른 마스크로 보는 것과 같다.

`event_groups.c`의 구조체는 짧다(54~66행).

```c
typedef struct EventGroupDef_t
{
    EventBits_t uxEventBits;
    List_t xTasksWaitingForBits; /**< List of tasks waiting for a bit to be set. */
    /* + uxEventGroupNumber (trace), ucStaticallyAllocated */
} EventGroup_t;
```

비트 하나와 대기 리스트 하나가 전부다(예제 4에서 32 B). 주요 API는 다음과 같다.

| API | 하는 일 | 블록? | ISR? |
|---|---|---|---|
| `xEventGroupSetBits(eg, bits)` | OR로 세우고, 조건이 맞은 대기자를 **전부** 깨움 | 아니오 | 아니오 (FromISR 판은 데몬 위임) |
| `xEventGroupClearBits(eg, bits)` | 비트 내리기 | 아니오 | FromISR 판도 데몬 위임 |
| `xEventGroupWaitBits(eg, bits, clear, all, to)` | wait-any(`all=pdFALSE`) / wait-all, 반환 시 기다린 비트 클리어 옵션 | 예 | 아니오 |
| `xEventGroupSync(eg, set, wait, to)` | 내 비트 세우기 + wait-all을 **원자적으로**, 성공 시 wait 비트 클리어 | 예 | 아니오 |
| `xEventGroupGetBits(eg)` / `xEventGroupGetBitsFromISR` | 현재 값 읽기 | 아니오 | FromISR 판 있음 |

반환값은 항상 "반환 순간의 비트 값"이다. 타임아웃으로 돌아와도 비트 값을 주므로, **조건이 맞았는지는 반환값을 마스크로 검사해서** 판단해야 한다(예제 5의 timeout 줄).

### 3.2 24비트 — 위 8비트는 커널이 쓴다

`EventBits_t`는 `TickType_t`와 같은 타입이다(`event_groups.h` 113행 `typedef TickType_t EventBits_t;`). 대기 태스크는 자기 TCB의 `xEventListItem` 값 칸에 "기다리는 비트 + 제어 플래그"를 넣어 대기 리스트에 걸린다. 그 제어 플래그가 위 8비트를 차지한다.

```svg
<svg viewBox="0 0 660 210" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="330.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">EventBits_t (TickType_t 32비트일 때) — 위 8비트는 커널 몫</text>
<rect x="20.0" y="50.0" width="19.5" height="28.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="29.8" y="96.0" font-size="11" text-anchor="middle">31</text>
<rect x="39.5" y="50.0" width="19.5" height="28.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="59.0" y="50.0" width="19.5" height="28.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="78.5" y="50.0" width="19.5" height="28.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="88.2" y="96.0" font-size="11" text-anchor="middle">28</text>
<rect x="98.0" y="50.0" width="19.5" height="28.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="117.5" y="50.0" width="19.5" height="28.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="137.0" y="50.0" width="19.5" height="28.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="156.5" y="50.0" width="19.5" height="28.0" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="166.2" y="96.0" font-size="11" text-anchor="middle">24</text>
<rect x="176.0" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="185.8" y="96.0" font-size="11" text-anchor="middle">23</text>
<rect x="195.5" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="215.0" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="234.5" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="244.2" y="96.0" font-size="11" text-anchor="middle">20</text>
<rect x="254.0" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="273.5" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="293.0" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="312.5" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="322.2" y="96.0" font-size="11" text-anchor="middle">16</text>
<rect x="332.0" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="351.5" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="371.0" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="390.5" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="400.2" y="96.0" font-size="11" text-anchor="middle">12</text>
<rect x="410.0" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="429.5" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="449.0" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="468.5" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="478.2" y="96.0" font-size="11" text-anchor="middle">8</text>
<rect x="488.0" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="507.5" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="527.0" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="546.5" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="556.2" y="96.0" font-size="11" text-anchor="middle">4</text>
<rect x="566.0" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="585.5" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="605.0" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<rect x="624.5" y="50.0" width="19.5" height="28.0" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="634.2" y="96.0" font-size="11" text-anchor="middle">0</text>
<text x="98.0" y="124.0" font-size="12" text-anchor="middle">제어 바이트 0xff000000</text>
<text x="410.0" y="124.0" font-size="12" text-anchor="middle">사용자 비트 0~23 (24개)</text>
<text x="20.0" y="152.0" font-size="12" text-anchor="start">bit 24 eventCLEAR_EVENTS_ON_EXIT_BIT · bit 25 eventUNBLOCKED_DUE_TO_BIT_SET</text>
<text x="20.0" y="172.0" font-size="12" text-anchor="start">bit 26 eventWAIT_FOR_ALL_BITS — 대기 태스크의 xEventListItem 값에 조건과 함께 저장</text>
<text x="20.0" y="194.0" font-size="12" text-anchor="start">16비트 tick이면 8비트, 64비트 tick이면 56비트만 쓸 수 있다</text>
</svg>
```
그림 4 — `event_groups.h` 43~55행의 정의. 32비트 tick 설정(`configTICK_TYPE_WIDTH_IN_BITS = TICK_TYPE_WIDTH_32_BITS`, 이 랩 기본)에서 쓸 수 있는 비트는 0~23이다. `xEventGroupSetBits`는 첫 줄에서 `configASSERT( ( uxBitsToSet & eventEVENT_BITS_CONTROL_BYTES ) == 0 )`로 위 바이트를 막는다(예제 5 마지막 줄).

이 값 칸은 원래 우선순위 정렬용인데(C1 §2), event group 대기 리스트는 정렬하지 않고 값을 "조건 저장소"로 재활용한다. 그래서 깨울 때 `vTaskRemoveFromUnorderedEventList`라는 별도 함수를 쓴다.

### 3.3 예제 5 — 드론 FC의 센서 준비 비트, 랑데부, 제어 바이트

**무엇을 확인하나**: gyro(1 tick마다) · acc(2 tick) · baro(5 tick) 세 태스크가 각자 비트를 세우고, fusion 태스크가 세 비트 wait-all + clear-on-exit로 기다린다. gyro가 그 사이 몇 번 비트를 세웠는지 같이 센다. 이어서 wait-any가 아니라 wait-all이 타임아웃되는 모양, 세 태스크가 2·4·8 tick 일한 뒤 `xEventGroupSync`로 만나는 랑데부, 마지막으로 bit 24를 세우려 할 때 무슨 일이 생기는지.

```c
/* ex5: event group — 센서 3개 wait-all(clear on exit), 비트는 '횟수'를 세지 않는다, xEventGroupSync 랑데부, 제어 바이트 */
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "board.h"
#define GYRO (1u << 0)
#define ACC  (1u << 1)
#define BARO (1u << 2)
#define ALL3 (GYRO | ACC | BARO)
static EventGroupHandle_t eg, rv;
static volatile uint32_t gyro_sets;
static void sensor(void *pv) {                       /* 주기(tick)마다 자기 비트를 세운다 */
    uint32_t bit = (uint32_t)pv, period = bit == GYRO ? 1 : bit == ACC ? 2 : 5;
    for (;;) {
        vTaskDelay(period);
        if (bit == GYRO) gyro_sets++;
        xEventGroupSetBits(eg, bit);
    }
}
static void party(void *pv) {                        /* 랑데부: 각자 다른 시간 일한 뒤 Sync */
    uint32_t bit = (uint32_t)pv;
    vTaskDelay(bit * 2);                             /* bit 1,2,4 → 2,4,8 tick 일함 */
    EventBits_t r = xEventGroupSync(rv, bit, 0x7, portMAX_DELAY);
    board_printf_raw("  sync bit%u: worked %u ticks, left@tick %u, returned 0x%x\n", (unsigned)bit,
                     (unsigned)(bit * 2), (unsigned)xTaskGetTickCount(), (unsigned)r);
    vTaskDelete(NULL);
}
static void fusion(void *pv) {
    (void)pv;
    for (int i = 0; i < 3; i++) {
        uint32_t g0 = gyro_sets;
        EventBits_t b = xEventGroupWaitBits(eg, ALL3, pdTRUE, pdTRUE, 20);  /* clear, wait-all */
        board_printf_raw("fusion @tick %u bits=0x%x gyro set %u times → bit 1개, now=0x%x\n",
                         (unsigned)xTaskGetTickCount(), (unsigned)b, (unsigned)(gyro_sets - g0),
                         (unsigned)xEventGroupGetBits(eg));
    }
    EventBits_t b = xEventGroupWaitBits(eg, BARO, pdFALSE, pdTRUE, 2);     /* 2 tick엔 BARO 안 옴 */
    board_printf_raw("timeout: returned 0x%x (BARO bit 0 → 타임아웃 판별)\n", (unsigned)b);
    for (uint32_t bit = 1; bit <= 4; bit <<= 1)
        xTaskCreate(party, "p", 256, (void *)bit, 3, NULL);
    board_printf_raw("parties start @tick %u\n", (unsigned)xTaskGetTickCount());
    vTaskDelay(12);
    board_printf_raw("EventBits_t=%u bit, control mask 0x%x\n", (unsigned)(8 * sizeof(EventBits_t)),
                     (unsigned)eventEVENT_BITS_CONTROL_BYTES);
    xEventGroupSetBits(eg, 1u << 23);
    board_printf_raw("bit 23 ok, now setting bit 24...\n");
    xEventGroupSetBits(eg, 1u << 24);                /* 커널 제어 비트 → configASSERT */
    board_exit(0);
}
int main(void) {
    eg = xEventGroupCreate(); rv = xEventGroupCreate();
    xTaskCreate(sensor, "gyro", 256, (void *)GYRO, 2, NULL);
    xTaskCreate(sensor, "acc", 256, (void *)ACC, 2, NULL);
    xTaskCreate(sensor, "baro", 256, (void *)BARO, 2, NULL);
    xTaskCreate(fusion, "fus", 384, NULL, 4, NULL);
    vTaskStartScheduler();
}
```

```text
fusion @tick 5 bits=0x7 gyro set 4 times → bit 1개, now=0x0
fusion @tick 10 bits=0x7 gyro set 5 times → bit 1개, now=0x0
fusion @tick 15 bits=0x7 gyro set 5 times → bit 1개, now=0x0
timeout: returned 0x3 (BARO bit 0 → 타임아웃 판별)
parties start @tick 17
  sync bit4: worked 8 ticks, left@tick 25, returned 0x7
  sync bit1: worked 2 ticks, left@tick 25, returned 0x7
  sync bit2: worked 4 ticks, left@tick 25, returned 0x7
EventBits_t=32 bit, control mask 0xff000000
bit 23 ok, now setting bit 24...

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/event_groups.c:563
[board_exit] code=253
```

**출력에서 볼 것**:

1. fusion은 baro가 오는 5 tick마다 깨어났다(5, 10, 15). 그 사이 gyro는 비트를 **4~5번** 세웠지만 fusion이 본 것은 비트 1개다. **event bit는 횟수를 세지 않는다** — 세마포어나 notification의 `eIncrement`와 결정적으로 다르다. "샘플이 몇 개 쌓였나"가 중요하면(예: gyro FIFO 소비) 비트가 아니라 카운트나 큐를 써야 한다.
2. clear-on-exit(`pdTRUE`) 덕분에 깨어난 직후 `now=0x0`이다. 이 클리어는 `xEventGroupSetBits` 안에서, 대기자를 깨우는 루프가 끝난 뒤 한꺼번에 일어난다(§7). 그래서 같은 비트를 기다리던 다른 태스크도 놓치지 않고 깨어난다.
3. wait-all 타임아웃의 반환값은 0x3(GYRO|ACC)이다. 0이 아니므로 "뭔가 받았다"로 착각하기 쉽다 — 반드시 `(b & BARO) != 0`처럼 검사한다.
4. 랑데부: 2·4·8 tick 일한 세 태스크가 모두 tick 25(시작 17 + 8)에 빠져나갔다. 가장 늦게 도착한 bit4 태스크가 먼저 출력했는데, 마지막 도착자는 블록하지 않고 바로 반환하고(같은 우선순위라 계속 실행), 앞의 둘은 그 `Sync` 안의 set이 깨운 것이다. `xEventGroupSync`가 "set 한 번 + wait 한 번"을 따로 부르는 것과 다른 점은 **원자성**이다. 따로 부르면 내가 set한 순간 다른 태스크가 깨어나 clear-on-exit로 비트를 지워 버려, 내 wait가 영원히 안 끝나는 경쟁이 생긴다.
5. `EventBits_t`는 32비트, 제어 마스크 0xff000000. bit 23은 되고 bit 24는 `event_groups.c:563`의 `configASSERT`에 걸렸다.

### 3.4 예제 6 — xEventGroupSetBits 비용은 대기자 수에 비례한다

**무엇을 확인하나**: `xEventGroupSetBits`는 대기 리스트를 처음부터 끝까지 훑으며 각 태스크의 조건을 검사한다. 대기 태스크를 0~32개로 늘리며 (a) 아무도 안 기다리는 비트를 세울 때(훑기만), (b) 전원이 기다리는 비트를 세울 때(전원 깨움) set 호출 하나의 시간을 잰다. 측정 태스크(우선순위 5)가 대기자(우선순위 1)보다 높아서 set 안에서 전환은 일어나지 않는다 — 순수하게 set 호출 비용이다.

```c
/* ex6: xEventGroupSetBits 비용 = 대기자 수에 비례 (리스트 전체를 훑는다) — 그래서 ISR에서 직접 못 한다 */
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "board.h"
#define CYC() (*(volatile uint32_t *)0x40028018u)
static EventGroupHandle_t eg;
static void waiter(void *pv) {                        /* 우선순위 1: pv 비트를 기다린다 */
    for (;;) xEventGroupWaitBits(eg, (EventBits_t)(uint32_t)pv, pdTRUE, pdFALSE, portMAX_DELAY);
}
static uint32_t cost(EventBits_t bit) {               /* set 호출 하나만 잰다 (8번 평균) */
    uint32_t s = 0;
    for (int k = 0; k < 8; k++) {
        uint32_t a = CYC(); xEventGroupSetBits(eg, bit); s += CYC() - a;
        xEventGroupClearBits(eg, bit);
        vTaskDelay(1);                                /* 깨어난 대기자들이 다시 블록하도록 */
    }
    return s / 8;
}
static void meas(void *pv) {                          /* 우선순위 5 */
    (void)pv;
    static const int steps[] = {0, 1, 2, 4, 8, 16, 32};
    int made = 0;
    for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        while (made < steps[i]) {                     /* 대기자는 모두 bit0 대기 */
            xTaskCreate(waiter, "w", 128, (void *)1u, 1, NULL); made++;
        }
        vTaskDelay(2);                                /* 모두 블록할 시간 */
        uint32_t miss = cost(1u << 5);                /* 아무도 안 기다리는 비트: 훑기만 */
        uint32_t hit = cost(1u << 0);                 /* 전원 깨우기 */
        board_printf_raw("waiters=%d set(no match)=%u cyc set(wake all)=%u cyc\n",
                         made, (unsigned)miss, (unsigned)hit);
    }
    board_exit(0);
}
int main(void) {
    eg = xEventGroupCreate();
    xTaskCreate(meas, "M", 384, NULL, 5, NULL);
    vTaskStartScheduler();
}
```

```text
waiters=0 set(no match)=69 cyc set(wake all)=69 cyc
waiters=1 set(no match)=80 cyc set(wake all)=136 cyc
waiters=2 set(no match)=88 cyc set(wake all)=201 cyc
waiters=4 set(no match)=104 cyc set(wake all)=331 cyc
waiters=8 set(no match)=136 cyc set(wake all)=590 cyc
waiters=16 set(no match)=200 cyc set(wake all)=1108 cyc
waiters=32 set(no match)=328 cyc set(wake all)=2145 cyc
```

```svg
<svg viewBox="0 0 640 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="320.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">xEventGroupSetBits() 한 번의 비용 — 대기 태스크 수에 선형 (예제 6)</text>
<line x1="70.0" y1="280.0" x2="610.0" y2="280.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="70.0" y1="280.0" x2="70.0" y2="45.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="70" y1="280.0" x2="610" y2="280.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="284.0" font-size="12" text-anchor="end">0</text>
<line x1="70" y1="222.5" x2="610" y2="222.5" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="226.5" font-size="12" text-anchor="end">600</text>
<line x1="70" y1="165.0" x2="610" y2="165.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="169.0" font-size="12" text-anchor="end">1200</text>
<line x1="70" y1="107.5" x2="610" y2="107.5" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="111.5" font-size="12" text-anchor="end">1800</text>
<line x1="70" y1="50.0" x2="610" y2="50.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="54.0" font-size="12" text-anchor="end">2400</text>
<text x="70.0" y="298.0" font-size="12" text-anchor="middle">0</text>
<text x="86.9" y="298.0" font-size="12" text-anchor="middle">1</text>
<text x="103.8" y="298.0" font-size="12" text-anchor="middle">2</text>
<text x="137.5" y="298.0" font-size="12" text-anchor="middle">4</text>
<text x="205.0" y="298.0" font-size="12" text-anchor="middle">8</text>
<text x="340.0" y="298.0" font-size="12" text-anchor="middle">16</text>
<text x="610.0" y="298.0" font-size="12" text-anchor="middle">32</text>
<text x="340.0" y="318.0" font-size="12" text-anchor="middle">대기 태스크 수</text>
<text x="14.0" y="50.0" font-size="12" text-anchor="start">cyc</text>
<polyline points="70.0,273.4 86.9,267.0 103.8,260.7 137.5,248.3 205.0,223.5 340.0,173.8 610.0,74.4" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="70.0" cy="273.4" r="3" fill="#e08a3c"/>
<circle cx="86.9" cy="267.0" r="3" fill="#e08a3c"/>
<circle cx="103.8" cy="260.7" r="3" fill="#e08a3c"/>
<circle cx="137.5" cy="248.3" r="3" fill="#e08a3c"/>
<circle cx="205.0" cy="223.5" r="3" fill="#e08a3c"/>
<circle cx="340.0" cy="173.8" r="3" fill="#e08a3c"/>
<circle cx="610.0" cy="74.4" r="3" fill="#e08a3c"/>
<polyline points="70.0,273.4 86.9,272.3 103.8,271.6 137.5,270.0 205.0,267.0 340.0,260.8 610.0,248.6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="70.0" cy="273.4" r="3" fill="#4a7bd0"/>
<circle cx="86.9" cy="272.3" r="3" fill="#4a7bd0"/>
<circle cx="103.8" cy="271.6" r="3" fill="#4a7bd0"/>
<circle cx="137.5" cy="270.0" r="3" fill="#4a7bd0"/>
<circle cx="205.0" cy="267.0" r="3" fill="#4a7bd0"/>
<circle cx="340.0" cy="260.8" r="3" fill="#4a7bd0"/>
<circle cx="610.0" cy="248.6" r="3" fill="#4a7bd0"/>
<text x="606.0" y="64.4" font-size="12" text-anchor="end">전원 깨움 2145 (약 65 cyc/대기자)</text>
<text x="606.0" y="238.6" font-size="12" text-anchor="end">맞는 비트 없음 328 (8 cyc/대기자)</text>
</svg>
```
그림 5 — 예제 6 실측. 맞는 비트가 없어도 대기자 하나당 8 cycle씩 늘고, 전원을 깨우면 대기자 하나당 약 65 cycle(ready 리스트로 옮기기)씩 는다. 32개를 깨우는 set 하나가 2145 cycle = 86 us(icount 기준)다.

**출력에서 볼 것**: (328 − 69) / 32 = 8.1 cycle/대기자(훑기), (2145 − 69) / 32 = 64.9 cycle/대기자(깨움). 완전히 선형이다. 이 O(n)이 **스케줄러 정지(`vTaskSuspendAll`) 안에서** 돈다는 점이 중요하다. 인터럽트는 막지 않으니 ISR 지연에는 영향이 없지만, 그동안 더 높은 우선순위 태스크가 깨어나도 전환이 미뤄진다. 그리고 ISR에서는 스케줄러를 정지할 수 없다 — ISR에서 이 루프를 돌리려면 인터럽트를 막고(크리티컬 섹션) 돌려야 하는데, 그러면 **인터럽트 마스킹 구간이 대기자 수에 비례**하게 된다. FreeRTOS가 이것을 거부한 결과가 다음 절이다.

### 3.5 xEventGroupSetBitsFromISR는 왜 타이머 데몬으로 미루나

`event_groups.c` 817~831행이 전부다.

```c
BaseType_t xEventGroupSetBitsFromISR( EventGroupHandle_t xEventGroup,
                                      const EventBits_t uxBitsToSet,
                                      BaseType_t * pxHigherPriorityTaskWoken )
{
    BaseType_t xReturn;
    traceENTER_xEventGroupSetBitsFromISR( xEventGroup, uxBitsToSet, pxHigherPriorityTaskWoken );
    traceEVENT_GROUP_SET_BITS_FROM_ISR( xEventGroup, uxBitsToSet );
    xReturn = xTimerPendFunctionCallFromISR( &vEventGroupSetBitsCallback, ( void * ) xEventGroup, ( uint32_t ) uxBitsToSet, pxHigherPriorityTaskWoken );
    traceRETURN_xEventGroupSetBitsFromISR( xReturn );
    return xReturn;
}
```

ISR은 "이 event group에 이 비트를 세워 줘"라는 요청을 **타이머 명령 큐**에 넣을 뿐이다(`xTimerPendFunctionCallFromISR`, D7에서 자세히). 실제 set은 타이머 서비스 태스크(daemon, D6)가 `vEventGroupSetBitsCallback` → `xEventGroupSetBits`로 한다. `event_groups.h`의 주석이 이유를 그대로 적는다: set은 "not a deterministic operation because there are an unknown number of tasks that may be waiting", 그래서 인터럽트나 크리티컬 섹션 안에서 하지 않는다. 대가는 세 가지다.

- **지연이 늘어난다**: 큐 send(ISR) → 데몬 깨어남 → 콜백 → set → 대상 태스크 깨어남. 전환이 한 번 더 있다.
- **데몬 우선순위에 묶인다**: 데몬보다 높은 우선순위 태스크가 바쁘면 콜백이 그만큼 밀린다.
- **실패할 수 있다**: 타이머 명령 큐(`configTIMER_QUEUE_LENGTH`, 이 랩은 10)가 꽉 차면 `pdFAIL`. 반환값을 검사해야 한다.
- 그리고 **`configUSE_TIMERS = 1`과 `INCLUDE_xTimerPendFunctionCall = 1`이 필수**다. 함수 정의 전체가 `#if ( ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 1 ) )`(833행) 안에 있다.

### 3.6 예제 7 — 직접 알림 vs event group FromISR, 데몬 우선순위 바꾸기

**무엇을 확인하나**: 우선순위 4인 H가 신호를 기다린다. 우선순위 3인 M이 IRQ를 트리거하고 **100 us(2500 cycle) 바쁘게 일한 뒤** 1 tick 쉰다(드론 FC라면 자세 추정 같은 중간 우선순위 계산). ISR이 (1) `vTaskNotifyGiveFromISR`로 H를 직접 깨우는 경우와 (2) `xEventGroupSetBitsFromISR`로 데몬에 위임하는 경우를, 데몬 우선순위 `configTIMER_TASK_PRIORITY`(TPRI)를 7, 5, 2, 1로 바꿔 가며 200번 잰다.

```c
/* ex7: 타이머 데몬 우선순위(TPRI)와 configUSE_TIMERS를 바꿔 본다 */
#ifndef D5_EX7_CONFIG_H
#define D5_EX7_CONFIG_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#undef  configTIMER_TASK_PRIORITY
#define configTIMER_TASK_PRIORITY TPRI
#ifdef D5_NOTIMERS
#undef  configUSE_TIMERS
#define configUSE_TIMERS 0
#endif
#endif
```

```c
/* ex7: ISR → 태스크. 직접 알림(1) vs xEventGroupSetBitsFromISR(2, 타이머 데몬 경유). 중간 우선순위 바쁜 태스크 M이 있다 */
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "board.h"
#define CYC() (*(volatile uint32_t *)0x40028018u)
#define NVIC_ISPR0 REG32(0xE000E200u)
#define N 200
static TaskHandle_t hH;
static EventGroupHandle_t eg;
static volatile uint32_t t0;
void TIMER0_IRQHandler(void) {
    BaseType_t w = pdFALSE;
#if MODE == 1
    vTaskNotifyGiveFromISR(hH, &w);
#else
    if (xEventGroupSetBitsFromISR(eg, 1, &w) != pdPASS) board_printf_raw("timer queue full!\n");
#endif
    portYIELD_FROM_ISR(w);
}
static void high(void *pv) {                          /* 우선순위 4: 센서 처리 */
    (void)pv;
    uint32_t sum = 0, mx = 0, mn = ~0u;
    for (int i = 0; i < N; i++) {
#if MODE == 1
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#else
        xEventGroupWaitBits(eg, 1, pdTRUE, pdFALSE, portMAX_DELAY);
#endif
        uint32_t d = CYC() - t0; sum += d; if (d > mx) mx = d; if (d < mn) mn = d;
    }
    board_printf_raw("MODE=%d TPRI=%d ISR->H avg %u min %u max %u cyc\n", MODE,
                     (int)configTIMER_TASK_PRIORITY, (unsigned)(sum / N), (unsigned)mn, (unsigned)mx);
    board_exit(0);
}
static void medium(void *pv) {                        /* 우선순위 3: 트리거 후 100 us(2500 cyc) 바쁨 */
    (void)pv;
    for (;;) {
        t0 = CYC();
        NVIC_ISPR0 = 1u << 8;
        while (CYC() - t0 < 2500) { }
        vTaskDelay(1);
    }
}
int main(void) {
    eg = xEventGroupCreate();
    NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;
    xTaskCreate(high, "H", 256, NULL, 4, &hH);
    xTaskCreate(medium, "M", 256, NULL, 3, NULL);
    vTaskStartScheduler();
}
```

```sh
# all.sh — MODE(1=notify, 2=event group FromISR) × 타이머 데몬 우선순위
for c in "1 7" "2 7" "2 5" "2 2" "2 1"; do set -- $c
  TAG=_m$1t$2 /Users/donh/.rtos_scratch/D5/run.sh ex7 "-DMODE=$1 -DTPRI=$2" 2>&1 | grep -v "text\|app.elf\|board_exit"
done
```

```text
MODE=1 TPRI=7 ISR->H avg 202 min 202 max 202 cyc
MODE=2 TPRI=7 ISR->H avg 879 min 879 max 879 cyc
MODE=2 TPRI=5 ISR->H avg 879 min 879 max 879 cyc
MODE=2 TPRI=2 ISR->H avg 3030 min 3029 max 3277 cyc
MODE=2 TPRI=1 ISR->H avg 3026 min 3025 max 3277 cyc
```

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">xEventGroupSetBitsFromISR — ISR은 "나중에 해 줘"를 타이머 큐에 넣을 뿐</text>
<text x="95.0" y="65.0" font-size="12" text-anchor="end">ISR</text>
<line x1="100" y1="60" x2="660" y2="60" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
<text x="95.0" y="115.0" font-size="12" text-anchor="end">Tmr Svc</text>
<line x1="100" y1="110" x2="660" y2="110" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
<text x="95.0" y="165.0" font-size="12" text-anchor="end">H (prio 4)</text>
<line x1="100" y1="160" x2="660" y2="160" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
<text x="95.0" y="215.0" font-size="12" text-anchor="end">M (prio 3)</text>
<line x1="100" y1="210" x2="660" y2="210" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
<rect x="110.0" y="200.0" width="60.0" height="20.0" fill="#888" fill-opacity="0.4" stroke="#888"/>
<rect x="170.0" y="50.0" width="60.0" height="20.0" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="200.0" y="44.0" font-size="11" text-anchor="middle">pend call</text>
<rect x="230.0" y="100.0" width="110.0" height="20.0" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="285.0" y="94.0" font-size="11" text-anchor="middle">SetBits 콜백</text>
<rect x="340.0" y="150.0" width="90.0" height="20.0" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="385.0" y="144.0" font-size="11" text-anchor="middle">H 실행</text>
<rect x="430.0" y="200.0" width="230.0" height="20.0" fill="#888" fill-opacity="0.4" stroke="#888"/>
<line x1="230.0" y1="70.0" x2="240.0" y2="100.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<line x1="340.0" y1="120.0" x2="350.0" y2="150.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<text x="370.0" y="252.0" font-size="12" text-anchor="middle">TPRI ≥ M: 879 cyc (데몬이 바로 돈다)</text>
<text x="370.0" y="272.0" font-size="12" text-anchor="middle">TPRI &lt; M: 3030 cyc (M이 블록할 때까지 콜백이 밀린다)</text>
<text x="140.0" y="252.0" font-size="12" text-anchor="middle">직접 알림: 202 cyc</text>
</svg>
```
그림 6 — TPRI ≥ M일 때의 타임라인. ISR은 타이머 큐에 넣기만 하고, 데몬이 깨어나 set 콜백을 돌린 뒤에야 H가 실행된다. TPRI가 M보다 낮으면 데몬 칸이 M의 바쁜 구간 뒤로 밀린다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">ISR → 우선순위 4 태스크 지연, 100 us 바쁜 M(우선순위 3)이 있을 때 (예제 7)</text>
<text x="242.0" y="63.0" font-size="12" text-anchor="end">직접 알림 (vTaskNotifyGiveFromISR)</text>
<rect x="250.0" y="50.0" width="22.7" height="20.0" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="278.7" y="64.0" font-size="12" text-anchor="start">202 cyc</text>
<text x="242.0" y="101.0" font-size="12" text-anchor="end">EG FromISR, TPRI=7</text>
<rect x="250.0" y="88.0" width="98.9" height="20.0" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="354.9" y="102.0" font-size="12" text-anchor="start">879 cyc</text>
<text x="242.0" y="139.0" font-size="12" text-anchor="end">EG FromISR, TPRI=5</text>
<rect x="250.0" y="126.0" width="98.9" height="20.0" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="354.9" y="140.0" font-size="12" text-anchor="start">879 cyc</text>
<text x="242.0" y="177.0" font-size="12" text-anchor="end">EG FromISR, TPRI=2 (M보다 낮음)</text>
<rect x="250.0" y="164.0" width="340.9" height="20.0" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="596.9" y="178.0" font-size="12" text-anchor="start">3030 cyc</text>
<text x="242.0" y="215.0" font-size="12" text-anchor="end">EG FromISR, TPRI=1</text>
<rect x="250.0" y="202.0" width="340.4" height="20.0" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="596.4" y="216.0" font-size="12" text-anchor="start">3026 cyc</text>
<line x1="250.0" y1="238.0" x2="610.0" y2="238.0" stroke="currentColor" stroke-width="1.3"/>
<text x="250.0" y="254.0" font-size="11" text-anchor="middle">0</text>
<text x="340.0" y="254.0" font-size="11" text-anchor="middle">800</text>
<text x="430.0" y="254.0" font-size="11" text-anchor="middle">1600</text>
<text x="520.0" y="254.0" font-size="11" text-anchor="middle">2400</text>
<text x="610.0" y="254.0" font-size="11" text-anchor="middle">3200</text>
</svg>
```
그림 7 — 예제 7 실측(QEMU icount). 직접 알림 202 cycle(8 us), 데몬 경유 879 cycle(35 us), 데몬이 M보다 낮으면 3030 cycle(121 us) = M의 바쁜 구간 2500 cycle이 통째로 더해진다.

**출력에서 볼 것**: 데몬 경유는 데몬 우선순위가 충분히 높아도 직접 알림의 **4.4배**다(879 / 202). 데몬이 M보다 낮으면 지연이 M의 실행 시간에 종속된다 — 이것은 우선순위 역전(D3)과 같은 모양이다: H(4)가 기다리는 일을 데몬(2)이 쥐고 있고, 그 사이 M(3)이 돈다. min과 max가 거의 같은 것은 icount가 결정적이기 때문이고, 3277은 SysTick이 끼어든 경우다. 결론: **ISR → 태스크 신호에 event group FromISR를 기본으로 쓰지 말 것.** 받는 태스크가 하나면 `xTaskNotifyFromISR(h, BIT, eSetBits, &w)`가 같은 "비트" 의미를 데몬 없이 준다. event group이 꼭 필요하면(여러 태스크가 서로 다른 조합을 기다림) 데몬 우선순위를 신호를 받는 태스크보다 높게 둔다 — 템플릿 기본값 `configMAX_PRIORITIES − 1`이 그 이유다.

`configUSE_TIMERS 0`으로 빌드하면 이렇게 된다(`D5_NOTIMERS` 정의, MODE=2).

```sh
TAG=_nt ./run.sh ex7 "-DMODE=2 -DTPRI=7 -DD5_NOTIMERS" 2>&1 | grep error
```

```text
ex7/main.c:17:9: error: implicit declaration of function 'xEventGroupSetBitsFromISR'; did you mean 'xEventGroupGetBitsFromISR'? [-Wimplicit-function-declaration]
/var/rtoslab/src/FreeRTOS-Kernel/timers.c:43:6: error: #error configUSE_TIMERS must be set to 1 to make the xTimerPendFunctionCall() function available.
   43 |     #error configUSE_TIMERS must be set to 1 to make the xTimerPendFunctionCall() function available.
```

함수 선언 자체가 사라져 암시적 선언 오류가 나고, `timers.c`는 `INCLUDE_xTimerPendFunctionCall`이 1인데 타이머가 꺼졌다고 `#error`를 낸다. 타이머를 끄는 저전력 · 초소형 설정에서는 event group을 ISR에서 쓸 방법이 없다는 뜻이다.

---

## 4. Stream buffer · message buffer — 1 writer 1 reader 바이트 링

### 4.1 직관: SSD의 호스트 데이터 링, UART의 RX FIFO

stream buffer는 Don이 수없이 짠 **SPSC(single-producer single-consumer) 링버퍼**에 "몇 바이트 이상 쌓이면 소비자를 깨운다"를 붙인 것이다. 생산자는 head만, 소비자는 tail만 움직이므로 둘 사이에 락이 필요 없다(D8의 lock-free SPSC와 같은 원리). 그 대신 **생산자가 둘이면 head를 두 곳에서 움직이게 되어** 깨진다(§4.5).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">StreamBuffer_t 하나 + 저장 영역(N+1 바이트)을 한 번에 malloc</text>
<rect x="20.0" y="44.0" width="230.0" height="150.0" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="135.0" y="83.2" font-size="12" text-anchor="middle" font-weight="bold">StreamBuffer_t (40 B)</text>
<text x="135.0" y="99.2" font-size="12" text-anchor="middle">xTail · xHead · xLength</text>
<text x="135.0" y="115.2" font-size="12" text-anchor="middle">xTriggerLevelBytes</text>
<text x="135.0" y="131.2" font-size="12" text-anchor="middle">xTaskWaitingToReceive</text>
<text x="135.0" y="147.2" font-size="12" text-anchor="middle">xTaskWaitingToSend</text>
<text x="135.0" y="163.2" font-size="12" text-anchor="middle">ucFlags · uxNotificationIndex</text>
<rect x="290.0" y="70.0" width="23.0" height="30.0" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="313.0" y="70.0" width="23.0" height="30.0" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="336.0" y="70.0" width="23.0" height="30.0" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="359.0" y="70.0" width="23.0" height="30.0" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="382.0" y="70.0" width="23.0" height="30.0" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="405.0" y="70.0" width="23.0" height="30.0" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="428.0" y="70.0" width="23.0" height="30.0" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="451.0" y="70.0" width="23.0" height="30.0" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="474.0" y="70.0" width="23.0" height="30.0" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="497.0" y="70.0" width="23.0" height="30.0" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="520.0" y="70.0" width="23.0" height="30.0" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="543.0" y="70.0" width="23.0" height="30.0" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="566.0" y="70.0" width="23.0" height="30.0" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="589.0" y="70.0" width="23.0" height="30.0" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="612.0" y="70.0" width="23.0" height="30.0" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="635.0" y="70.0" width="23.0" height="30.0" fill="#888" fill-opacity="0.08" stroke="#888"/>
<text x="359.0" y="124.0" font-size="12" text-anchor="middle">xTail</text>
<line x1="359.0" y1="112.0" x2="359.0" y2="102.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<text x="520.0" y="124.0" font-size="12" text-anchor="middle">xHead</text>
<line x1="520.0" y1="112.0" x2="520.0" y2="102.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/>
<text x="474.0" y="60.0" font-size="12" text-anchor="middle">쓰기: ISR(writer 1개) → xHead 이동</text>
<text x="474.0" y="146.0" font-size="12" text-anchor="middle">읽기: 태스크(reader 1개) → xTail 이동</text>
<text x="474.0" y="168.0" font-size="12" text-anchor="middle">바이트 수 ≥ trigger면 sbSEND_COMPLETED</text>
<text x="340.0" y="218.0" font-size="13" text-anchor="middle" font-weight="bold">message buffer 한 칸의 모양 (configMESSAGE_BUFFER_LENGTH_TYPE = size_t 4 B)</text>
<rect x="60.0" y="232.0" width="120.0" height="34.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="120.0" y="253.2" font-size="12" text-anchor="middle">len = 3 (4 B)</text>
<rect x="180.0" y="232.0" width="90.0" height="34.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="225.0" y="253.2" font-size="12" text-anchor="middle">a b c</text>
<rect x="270.0" y="232.0" width="130.0" height="34.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="335.0" y="253.2" font-size="12" text-anchor="middle">len = 10 (4 B)</text>
<rect x="400.0" y="232.0" width="210.0" height="34.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="505.0" y="253.2" font-size="12" text-anchor="middle">0 1 2 3 4 5 6 7 8 9</text>
<text x="340.0" y="290.0" font-size="12" text-anchor="middle">메시지 3 B는 7 B, 10 B는 14 B를 쓴다 (ex9 실측). uint8_t로 바꾸면 +1 B</text>
</svg>
```
그림 8 — `StreamBuffer_t`(40 B)와 저장 영역이 한 번에 할당된다(예제 4: 64 B 버퍼 → heap 120 B). 위는 stream buffer의 링, 아래는 message buffer가 메시지마다 붙이는 길이 접두.

- **stream buffer**: 바이트 흐름. 경계가 없다. 10바이트를 두 번 보내면 받는 쪽은 7 + 13으로 받을 수도 있다. UART RX, 오디오 샘플, 로그 바이트에 맞다.
- **message buffer**: stream buffer 위에 **길이 접두(length prefix)** 를 붙인 것. 보낸 단위 그대로 받는다. 가변 길이 패킷(MAVLink 프레임, CAN-FD 프레임, 명령 패킷)에 맞다. 구현은 같은 `stream_buffer.c`이고 `ucFlags`의 `sbFLAGS_IS_MESSAGE_BUFFER` 비트만 다르다.
- **stream batching buffer**(V11.1.0에서 추가, `xStreamBatchingBufferCreate`): reader가 비어 있지 않은 버퍼를 읽어도 **trigger level을 넘을 때까지 블록**한다. 일반 stream buffer는 1바이트라도 있으면 즉시 반환한다.
- `configUSE_STREAM_BUFFERS`(기본 1)로 통째로 뺄 수 있다.

### 4.2 API와 trigger level의 정확한 의미

| API | 설명 |
|---|---|
| `xStreamBufferCreate(size, trigger)` / `...Static` | trigger가 0이면 1로 바꾼다(`if( xTriggerLevelBytes == ( size_t ) 0 )`), size보다 크면 assert |
| `xStreamBufferSend(sb, p, n, to)` | 공간이 될 때까지 블록 가능. stream은 일부만 쓸 수도 있다(반환값 = 쓴 바이트) |
| `xStreamBufferSendFromISR(sb, p, n, &w)` | 블록 없음 |
| `xStreamBufferReceive(sb, p, max, to)` | 비어 있으면 블록. **1바이트라도 있으면 즉시 반환**(stream) |
| `xStreamBufferReceiveFromISR` | 블록 없음 |
| `xStreamBufferSetTriggerLevel(sb, n)` | 실행 중 trigger 변경 (n ≤ 길이) |
| `xStreamBufferBytesAvailable` / `SpacesAvailable` / `Reset` | 상태 · 초기화 |
| `xMessageBufferCreate(size)` · `Send` · `Receive` · `NextLengthBytes` | message 판 |
| `vStreamBufferSetStreamBufferNotificationIndex(sb, i)` | 내부가 쓸 notification 인덱스 (V11.1.0+) |

trigger level은 **송신 쪽에서만** 쓰인다. `xStreamBufferSend`의 끝부분이다.

```c
/* Was a task waiting for the data? */
if( prvBytesInBufferMeetTriggerLevel( pxStreamBuffer, prvBytesInBuffer( pxStreamBuffer ) ) != pdFALSE )
{
    prvSEND_COMPLETED( pxStreamBuffer );
}
```

즉 "쓴 뒤 쌓인 바이트 수 ≥ trigger면 기다리는 reader를 깨운다". reader 쪽 `xStreamBufferReceive`는 trigger를 보지 않고 "0바이트면 블록"만 한다(batching이면 "trigger 이하면 블록"). 그래서 reader가 일을 마치고 다시 `Receive`를 불렀을 때 그사이 몇 바이트가 들어와 있으면 trigger 미만이라도 그대로 가져간다 — 예제 8의 stream 결과에서 평균 덩어리가 trigger보다 약간 작은 이유다.

reader가 깨는 또 하나의 길은 **타임아웃**이다. trigger가 8인데 7바이트만 오고 끝나면(문장 끝, 링크 끊김) reader는 타임아웃까지 기다린 뒤 7바이트를 받는다. UART라면 "idle line" 검출(E6)이 이 역할을 하드웨어로 한다.

**"단일 writer · 단일 reader" 가정이 소스에 박힌 곳**: `StreamBuffer_t`에는 기다리는 태스크를 담는 칸이 리스트가 아니라 **핸들 하나씩**(`xTaskWaitingToReceive`, `xTaskWaitingToSend`)이다. 블록하기 전에 `configASSERT( pxStreamBuffer->xTaskWaitingToReceive == NULL )`(1119행, 주석 "Should only be one reader")와 `configASSERT( pxStreamBuffer->xTaskWaitingToSend == NULL )`(주석 "Should only be one writer")로 검사한다. 그리고 `prvWriteBytesToBuffer`는 크리티컬 섹션 없이 `memcpy` 후 `xHead`를 갱신한다 — 쓰는 쪽이 하나라는 가정 덕분에 큐보다 가볍다. 깨움도 큐처럼 이벤트 리스트가 아니라 **task notification**(`sbSEND_COMPLETED` → `xTaskNotifyIndexed(..., eNoAction)`)이다.

### 4.3 예제 8 — UART GNSS NMEA 수신: trigger level을 1~82로 바꾸기

**무엇을 확인하나**: 115200 baud 8N1 UART는 바이트당 86.8 us다(10비트 / 115200). TIMER0를 25 MHz / 2170 = 11521 Hz로 돌려 RX 인터럽트를 흉내 내고, ISR이 NMEA GGA 문장을 한 바이트씩 `xStreamBufferSendFromISR`로 넣는다. 파서 태스크는 `xStreamBufferReceive`로 받고 덩어리마다 20 us 파싱 비용을 쓴다. 2048 바이트를 받는 동안 **깨어난 횟수**와 **덩어리 첫 바이트가 기다린 시간**(도착 시각을 ISR이 링에 기록)을 trigger level별로, stream과 batching 두 종류로 잰다. QEMU의 CMSDK UART는 baud 타이밍을 모델링하지 않아 실제 UART 대신 타이머로 바이트 간격을 만들었다.

```c
/* ex8: UART RX(115200 baud = 86.8 us/byte, TIMER0로 흉내) ISR → stream buffer → 파서 태스크. trigger level을 바꾼다 */
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "board.h"
#define CYC() (*(volatile uint32_t *)0x40028018u)
#define TOTAL 2048
static const char nmea[] = "$GNGGA,123519.00,3723.2475,N,12158.3416,W,1,12,0.9,10.5,M,-25.6,M,,*47\r\n";
static StreamBufferHandle_t sb;
static uint32_t ts[256];                              /* 바이트 도착 시각(cyc), 링 */
static volatile uint32_t sent;
void TIMER0_IRQHandler(void) {
    TIMER_INTCLR(TIMER0_BASE) = 1;
    BaseType_t w = pdFALSE;
    uint8_t c = (uint8_t)nmea[sent % (sizeof nmea - 1)];
    ts[sent++ & 255] = CYC();                         /* 바이트는 끝없이 들어온다 */
    if (xStreamBufferSendFromISR(sb, &c, 1, &w) != 1) board_printf_raw("overrun!\n");
    portYIELD_FROM_ISR(w);
}
static void parser(void *pv) {
    (void)pv;
    uint8_t buf[128];
    uint32_t got = 0, wakes = 0, lat_max = 0, lat_sum = 0;
    while (got < TOTAL) {
        size_t n = xStreamBufferReceive(sb, buf, sizeof buf, pdMS_TO_TICKS(20));
        if (n == 0) continue;
        uint32_t lat = CYC() - ts[got & 255];               /* 이번 덩어리 첫 바이트의 대기 시간 */
        wakes++; got += n; lat_sum += lat; if (lat > lat_max) lat_max = lat;
        for (uint32_t t = CYC(); CYC() - t < 500; ) { }   /* 파싱 비용 20 us 흉내 */
    }
    board_printf_raw("%s trig=%u wakes=%u avg_chunk=%u.%u B lat_avg=%u us lat_max=%u us\n",
                     BATCH ? "batch " : "stream", (unsigned)TRIG, (unsigned)wakes,
                     (unsigned)(TOTAL / wakes), (unsigned)(TOTAL % wakes * 10 / wakes),
                     (unsigned)(lat_sum / wakes / 25), (unsigned)(lat_max / 25));
    board_exit(0);
}
int main(void) {
#if BATCH
    sb = xStreamBatchingBufferCreate(256, TRIG);
#else
    sb = xStreamBufferCreate(256, TRIG);
#endif
    TIMER_RELOAD(TIMER0_BASE) = 2170 - 1;             /* 25 MHz / 2170 = 11521 Hz */
    TIMER_VALUE(TIMER0_BASE) = 2170 - 1;
    TIMER_CTRL(TIMER0_BASE) = (1u << 0) | (1u << 3);  /* EN + IRQ EN */
    NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;
    xTaskCreate(parser, "nmea", 384, NULL, 3, NULL);
    vTaskStartScheduler();
}
```

```sh
# all.sh — trigger level sweep (stream) + batching 비교
for b in 0 1; do for t in 1 2 4 8 16 32 64 82; do
  TAG=_b${b}t$t /Users/donh/.rtos_scratch/D5/run.sh ex8 "-DTRIG=$t -DBATCH=$b" 20 2>&1 | grep -v "text\|app.elf\|board_exit"
done; done
```

```text
stream trig=1 wakes=2048 avg_chunk=1.0 B lat_avg=17 us lat_max=24 us
stream trig=2 wakes=1025 avg_chunk=1.9 B lat_avg=104 us lat_max=106 us
stream trig=4 wakes=513 avg_chunk=3.9 B lat_avg=277 us lat_max=280 us
stream trig=8 wakes=257 avg_chunk=7.9 B lat_avg=623 us lat_max=627 us
stream trig=16 wakes=129 avg_chunk=15.8 B lat_avg=1310 us lat_max=1322 us
stream trig=32 wakes=65 avg_chunk=31.5 B lat_avg=2668 us lat_max=2711 us
stream trig=64 wakes=33 avg_chunk=62.0 B lat_avg=5322 us lat_max=5490 us
stream trig=82 wakes=26 avg_chunk=78.7 B lat_avg=6780 us lat_max=7051 us
batch  trig=1 wakes=1024 avg_chunk=2.0 B lat_avg=104 us lat_max=106 us
batch  trig=2 wakes=683 avg_chunk=2.9 B lat_avg=191 us lat_max=193 us
batch  trig=4 wakes=410 avg_chunk=4.9 B lat_avg=365 us lat_max=366 us
batch  trig=8 wakes=228 avg_chunk=8.9 B lat_avg=712 us lat_max=714 us
batch  trig=16 wakes=121 avg_chunk=16.9 B lat_avg=1407 us lat_max=1408 us
batch  trig=32 wakes=63 avg_chunk=32.5 B lat_avg=2796 us lat_max=2797 us
batch  trig=64 wakes=32 avg_chunk=64.0 B lat_avg=5574 us lat_max=5575 us
batch  trig=82 wakes=25 avg_chunk=81.9 B lat_avg=7137 us lat_max=7138 us
```

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">UART 2048 B 수신: trigger level ↑ → 깨움 ↓, 첫 바이트 대기 ↑ (예제 8, 115200 baud)</text>
<line x1="70.0" y1="280.0" x2="590.0" y2="280.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="70.0" y1="280.0" x2="70.0" y2="60.0" stroke="currentColor" stroke-width="1.3"/>
<line x1="590.0" y1="280.0" x2="590.0" y2="60.0" stroke="currentColor" stroke-width="1.3"/>
<text x="64.0" y="284.0" font-size="12" text-anchor="end">1</text>
<line x1="70" y1="280.0" x2="590" y2="280.0" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="223.1" font-size="12" text-anchor="end">10</text>
<line x1="70" y1="219.1" x2="590" y2="219.1" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="162.2" font-size="12" text-anchor="end">100</text>
<line x1="70" y1="158.2" x2="590" y2="158.2" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="64.0" y="101.3" font-size="12" text-anchor="end">1000</text>
<line x1="70" y1="97.3" x2="590" y2="97.3" stroke="#888" stroke-width="0.5" stroke-dasharray="4 3"/>
<text x="596.0" y="284.0" font-size="12" text-anchor="start">0 ms</text>
<text x="596.0" y="229.0" font-size="12" text-anchor="start">2 ms</text>
<text x="596.0" y="174.0" font-size="12" text-anchor="start">4 ms</text>
<text x="596.0" y="119.0" font-size="12" text-anchor="start">6 ms</text>
<text x="596.0" y="64.0" font-size="12" text-anchor="start">8 ms</text>
<text x="70.0" y="298.0" font-size="12" text-anchor="middle">1</text>
<text x="151.8" y="298.0" font-size="12" text-anchor="middle">2</text>
<text x="233.6" y="298.0" font-size="12" text-anchor="middle">4</text>
<text x="315.4" y="298.0" font-size="12" text-anchor="middle">8</text>
<text x="397.2" y="298.0" font-size="12" text-anchor="middle">16</text>
<text x="479.0" y="298.0" font-size="12" text-anchor="middle">32</text>
<text x="560.8" y="298.0" font-size="12" text-anchor="middle">64</text>
<text x="590.0" y="298.0" font-size="12" text-anchor="middle">82</text>
<text x="330.0" y="318.0" font-size="12" text-anchor="middle">trigger level (바이트, log 축)</text>
<text x="14.0" y="48.0" font-size="12" text-anchor="start">깨움 수</text>
<polyline points="70.0,78.3 151.8,96.6 233.6,114.9 315.4,133.2 397.2,151.5 479.0,169.6 560.8,187.5 590.0,193.8" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="70.0" cy="78.3" r="3" fill="#4a7bd0"/>
<circle cx="151.8" cy="96.6" r="3" fill="#4a7bd0"/>
<circle cx="233.6" cy="114.9" r="3" fill="#4a7bd0"/>
<circle cx="315.4" cy="133.2" r="3" fill="#4a7bd0"/>
<circle cx="397.2" cy="151.5" r="3" fill="#4a7bd0"/>
<circle cx="479.0" cy="169.6" r="3" fill="#4a7bd0"/>
<circle cx="560.8" cy="187.5" r="3" fill="#4a7bd0"/>
<circle cx="590.0" cy="193.8" r="3" fill="#4a7bd0"/>
<polyline points="70.0,96.7 151.8,107.4 233.6,120.9 315.4,136.4 397.2,153.2 479.0,170.4 560.8,188.3 590.0,194.9" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<circle cx="70.0" cy="96.7" r="3" fill="#3f9a6b"/>
<circle cx="151.8" cy="107.4" r="3" fill="#3f9a6b"/>
<circle cx="233.6" cy="120.9" r="3" fill="#3f9a6b"/>
<circle cx="315.4" cy="136.4" r="3" fill="#3f9a6b"/>
<circle cx="397.2" cy="153.2" r="3" fill="#3f9a6b"/>
<circle cx="479.0" cy="170.4" r="3" fill="#3f9a6b"/>
<circle cx="560.8" cy="188.3" r="3" fill="#3f9a6b"/>
<circle cx="590.0" cy="194.9" r="3" fill="#3f9a6b"/>
<polyline points="70.0,279.5 151.8,277.1 233.6,272.4 315.4,262.9 397.2,244.0 479.0,206.6 560.8,133.6 590.0,93.5" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="70.0" cy="279.5" r="3" fill="#e08a3c"/>
<circle cx="151.8" cy="277.1" r="3" fill="#e08a3c"/>
<circle cx="233.6" cy="272.4" r="3" fill="#e08a3c"/>
<circle cx="315.4" cy="262.9" r="3" fill="#e08a3c"/>
<circle cx="397.2" cy="244.0" r="3" fill="#e08a3c"/>
<circle cx="479.0" cy="206.6" r="3" fill="#e08a3c"/>
<circle cx="560.8" cy="133.6" r="3" fill="#e08a3c"/>
<circle cx="590.0" cy="93.5" r="3" fill="#e08a3c"/>
<rect x="80.0" y="326.0" width="14.0" height="14.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="100.0" y="338.0" font-size="12" text-anchor="start">stream 깨움 수 (왼쪽)</text>
<rect x="256.2" y="326.0" width="14.0" height="14.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<text x="276.2" y="338.0" font-size="12" text-anchor="start">batching 깨움 수 (왼쪽)</text>
<rect x="446.2" y="326.0" width="14.0" height="14.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="466.2" y="338.0" font-size="12" text-anchor="start">stream 평균 대기 (오른쪽)</text>
</svg>
```
그림 9 — 예제 8 실측. 깨움 수(왼쪽 log 축)는 trigger에 반비례하고, 첫 바이트가 기다린 평균 시간(오른쪽 축)은 trigger × 86.8 us에 비례한다. "CPU 부담"과 "지연"을 맞바꾸는 손잡이다.

**출력에서 볼 것**:

1. trigger 1이면 바이트마다 깨어나 2048번, 지연 17 us(ISR → 전환 → 파서). trigger 16이면 129번, 지연 1.3 ms. trigger 82(NMEA 최대 문장 길이)면 26번, 6.8 ms다. 깨움 하나가 전환 + API로 약 200 cycle(8 us, §2)이니 trigger 1은 초당 11521번 × 8 us = **CPU의 9%**를 깨움에만 쓴다. 실제 칩(H7 480 MHz)에서는 비율이 훨씬 작지만, 921600 baud GNSS나 여러 UART가 겹치면 무시할 수 없다.
2. stream은 평균 덩어리가 trigger보다 약간 작고(16 → 15.8, 82 → 78.7) batching은 trigger + 1이다(16 → 16.9, 1 → 2.0). batching은 "**초과**할 때" 깨우기 때문이다(`prvBytesInBufferMeetTriggerLevel`의 batching 분기 `xBytesInBuffer > pxStreamBuffer->xTriggerLevelBytes`). V11.3.2 History.txt에 "Fix batching stream buffers to unblock a receiver only after the buffered byte count exceeds, rather than reaches, the trigger level"이 있다 — 이 동작은 V11.3.x에서 고쳐진 것이다.
3. 지연은 거의 정확히 (trigger − 1) × 86.8 us + 17 us다: trigger 16 → 15 × 86.8 + 17 = 1319 us(측정 1310). 계산으로 예측 가능하다는 것이 이 도구의 장점이다.
4. 실무 선택: NMEA처럼 "문장 하나가 와야 의미 있는" 데이터는 trigger를 문장 길이 근처(수십 바이트)로 두고 타임아웃(또는 UART idle 인터럽트에서 `xStreamBufferSendCompletedFromISR`로 강제 깨움)으로 꼬리를 처리한다. MAVLink 명령처럼 지연이 중요한 링크는 trigger를 작게 둔다.

### 4.4 예제 9 — message buffer의 길이 접두, 작은 수신 버퍼, batching의 꼬리

**무엇을 확인하나**: 64 B message buffer에 3 B, 10 B 메시지를 넣었을 때 줄어드는 공간, 다음 메시지 길이 조회, 2 B짜리 수신 버퍼로 받으면 어떻게 되나, 절대 안 들어가는 크기를 블록하며 보내면, 그리고 trigger 8인 stream / batching buffer에 5 B만 있을 때 `Receive(…, 3 tick)`의 차이. `configMESSAGE_BUFFER_LENGTH_TYPE`을 기본(`size_t`, 4 B)과 `uint8_t`로 두 번 돌린다.

```c
/* ex9: message buffer의 길이 접두(length prefix), 너무 작은 수신 버퍼, batching buffer의 꼬리 바이트 */
#include "FreeRTOS.h"
#include "task.h"
#include "message_buffer.h"
#include "stream_buffer.h"
#include "board.h"
#define P board_printf_raw
static void t(void *pv) {
    (void)pv;
    uint8_t big[64] = {0}, small[2];
    MessageBufferHandle_t mb = xMessageBufferCreate(64);
    P("len type = %u B, empty space = %u\n", (unsigned)sizeof(configMESSAGE_BUFFER_LENGTH_TYPE),
      (unsigned)xMessageBufferSpacesAvailable(mb));
    xMessageBufferSend(mb, "abc", 3, 0);
    P("after 3 B msg : space %u\n", (unsigned)xMessageBufferSpacesAvailable(mb));
    xMessageBufferSend(mb, "0123456789", 10, 0);
    P("after 10 B msg: space %u, next msg len %u\n", (unsigned)xMessageBufferSpacesAvailable(mb),
      (unsigned)xMessageBufferNextLengthBytes(mb));
    size_t n = xMessageBufferReceive(mb, small, sizeof small, 0);
    P("receive into 2 B buf -> %u (메시지는 그대로 남음, next=%u)\n", (unsigned)n,
      (unsigned)xMessageBufferNextLengthBytes(mb));
    n = xMessageBufferReceive(mb, big, sizeof big, 0);
    P("receive into 64 B buf -> %u '%c%c%c'\n", (unsigned)n, big[0], big[1], big[2]);
    xMessageBufferReset(mb);
    TickType_t t0 = xTaskGetTickCount();
    n = xMessageBufferSend(mb, big, 61, 10);          /* 61 + 길이 > 63 → 절대 안 들어감 */
    P("send 61 B (wait 10) -> %u after %u ticks\n", (unsigned)n, (unsigned)(xTaskGetTickCount() - t0));
    /* 꼬리 바이트: stream vs batching, trigger 8에 5 B만 들어 있을 때 */
    StreamBufferHandle_t s = xStreamBufferCreate(64, 8), b = xStreamBatchingBufferCreate(64, 8);
    xStreamBufferSend(s, "12345", 5, 0); xStreamBufferSend(b, "12345", 5, 0);
    t0 = xTaskGetTickCount(); n = xStreamBufferReceive(s, big, 64, 3);
    P("stream  trig 8, 5 B: got %u after %u ticks\n", (unsigned)n, (unsigned)(xTaskGetTickCount() - t0));
    t0 = xTaskGetTickCount(); n = xStreamBufferReceive(b, big, 64, 3);
    P("batching trig 8, 5 B: got %u after %u ticks, still %u B inside\n", (unsigned)n,
      (unsigned)(xTaskGetTickCount() - t0), (unsigned)xStreamBufferBytesAvailable(b));
    board_exit(0);
}
int main(void) {
    xTaskCreate(t, "t", 512, NULL, 2, NULL);
    vTaskStartScheduler();
}
```

```sh
./run.sh ex9; TAG=_u8 ./run.sh ex9 -DconfigMESSAGE_BUFFER_LENGTH_TYPE=uint8_t
```

```text
len type = 4 B, empty space = 64
after 3 B msg : space 57
after 10 B msg: space 43, next msg len 3
receive into 2 B buf -> 0 (메시지는 그대로 남음, next=3)
receive into 64 B buf -> 3 'abc'
send 61 B (wait 10) -> 0 after 0 ticks
stream  trig 8, 5 B: got 5 after 0 ticks
batching trig 8, 5 B: got 0 after 3 ticks, still 5 B inside
---
len type = 1 B, empty space = 64
after 3 B msg : space 60
after 10 B msg: space 49, next msg len 3
receive into 2 B buf -> 0 (메시지는 그대로 남음, next=3)
receive into 64 B buf -> 3 'abc'
send 61 B (wait 10) -> 61 after 0 ticks
stream  trig 8, 5 B: got 5 after 0 ticks
batching trig 8, 5 B: got 0 after 3 ticks, still 5 B inside
```

**출력에서 볼 것**:

1. 3 B 메시지는 7 B, 10 B 메시지는 14 B를 썼다 — 길이 접두 `sbBYTES_TO_STORE_MESSAGE_LENGTH` = `sizeof( configMESSAGE_BUFFER_LENGTH_TYPE )` = 4 B. `uint8_t`로 바꾸면 접두가 1 B가 되어 4 B, 11 B만 쓴다. 메시지가 255 B를 넘지 않는 CAN · 짧은 명령 채널이라면 `uint8_t`로 바이트를 아낄 수 있다(대신 255 B 초과 메시지는 보낼 수 없다).
2. 수신 버퍼가 메시지보다 작으면 **0을 반환하고 메시지는 그대로 남는다**(next=3). 잘라서 주지 않는다. 받는 쪽은 `xMessageBufferNextLengthBytes`로 길이를 먼저 확인하거나 최대 크기 버퍼를 쓴다. 0을 "비었음"으로 해석하는 루프는 여기서 영원히 같은 메시지를 못 꺼낸다.
3. 61 B + 접두 4 B = 65 B > 최대 63 B(길이 64 − 1)이면 **기다려도 소용없으니 즉시 0을 반환**한다(10 tick 블록을 요청했지만 0 tick). `xStreamBufferSend`의 "The message would not fit even if the entire buffer was empty, so don't wait for space" 분기다. `uint8_t` 접두면 62 B라 들어간다.
4. **batching buffer의 꼬리 함정**: trigger 8에 5 B가 있을 때 stream buffer는 즉시 5 B를 주지만, batching buffer는 3 tick 타임아웃 뒤에도 **0을 반환하고 5 B를 버퍼에 남긴다**. 타임아웃 뒤 재확인 조건도 "trigger 초과"이기 때문이다(`if( xBytesAvailable > xBytesToStoreMessageLength )`에서 batching이면 그 값이 trigger). 데이터 흐름이 끊기면 마지막 조각은 다음 바이트가 올 때까지 못 꺼낸다. 처음 예제 8을 "2048 B 보내고 멈추는" ISR로 짰을 때 batching 실행이 끝나지 않았던 이유가 이것이다 — 그래서 예제 8은 ISR이 계속 보내도록 바꿨다.

### 4.5 예제 10 — single-writer · single-reader 가정을 깨면

**무엇을 확인하나**: (A) 태스크와 ISR **두 writer**가 같은 message buffer에 8 B 메시지("AAAAAAAA", "BBBBBBBB")를 넣고, 태스크가 꺼내며 "길이 8, 같은 글자 8개"인지 검사한다. ISR은 TIMER0로 약 40 us마다 쏜다. `GUARD=1`이면 태스크 쪽 send를 `taskENTER_CRITICAL()`로 감싼다. (B) 빈 버퍼에 reader 두 개가 블록하면.

```c
/* ex10: 단일 writer / 단일 reader 가정을 깨면? (A) ISR + 태스크 두 writer → 메시지 손상 (B) reader 두 개 → assert */
#include "FreeRTOS.h"
#include "task.h"
#include "message_buffer.h"
#include "board.h"
static MessageBufferHandle_t mb;
static volatile uint32_t isr_sent;
void TIMER0_IRQHandler(void) {                        /* 두 번째 writer: ISR이 "BBBBBBBB"를 넣는다 */
    TIMER_INTCLR(TIMER0_BASE) = 1;
    if (xMessageBufferSendFromISR(mb, "BBBBBBBB", 8, NULL) == 8) isr_sent++;
}
static void rd(void *pv) {
    (void)pv;
    uint8_t m[16];
    xMessageBufferReceive(mb, m, sizeof m, portMAX_DELAY);
    vTaskDelete(NULL);
}
static void wr(void *pv) {                            /* 첫 번째 writer + 검사기 */
    (void)pv;
    uint8_t m[64];
    uint32_t ok = 0, bad = 0, sent = 0;
    while (sent < 20000) {
#if GUARD
        taskENTER_CRITICAL();                         /* writer 쪽을 잠가 ISR writer와 직렬화 */
#endif
        if (xMessageBufferSend(mb, "AAAAAAAA", 8, 0) == 8) sent++;
#if GUARD
        taskEXIT_CRITICAL();
#endif
        size_t n;
        while ((n = xMessageBufferReceive(mb, m, sizeof m, 0)) != 0) {
            int same = (n == 8);
            for (size_t i = 1; i < n && same; i++) same = (m[i] == m[0]);
            if (same) ok++; else bad++;
        }
    }
    board_printf_raw("GUARD=%d task sent %u, isr sent %u, good %u, corrupted %u\n", GUARD,
                     (unsigned)sent, (unsigned)isr_sent, (unsigned)ok, (unsigned)bad);
    TIMER_CTRL(TIMER0_BASE) = 0; xMessageBufferReset(mb);
    xTaskCreate(rd, "r1", 256, NULL, 3, NULL);        /* (B) 빈 버퍼에 reader 두 개가 블록 */
    xTaskCreate(rd, "r2", 256, NULL, 3, NULL);
    vTaskDelay(2);
    board_printf_raw("two readers blocked?\n");
    board_exit(0);
}
int main(void) {
    mb = xMessageBufferCreate(128);
    TIMER_RELOAD(TIMER0_BASE) = 997; TIMER_CTRL(TIMER0_BASE) = 1u | (1u << 3);
    NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;
    xTaskCreate(wr, "w", 384, NULL, 2, NULL);
    vTaskStartScheduler();
}
```

```sh
for g in 0 1; do TAG=_g$g ./run.sh ex10 -DGUARD=$g 60; done
```

```text
GUARD=0 task sent 20000, isr sent 15085, good 30241, corrupted 160

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/stream_buffer.c:1119
[board_exit] code=253
GUARD=1 task sent 20000, isr sent 17671, good 37671, corrupted 0

[configASSERT] /var/rtoslab/src/FreeRTOS-Kernel/stream_buffer.c:1119
[board_exit] code=253
```

**출력에서 볼 것**:

1. GUARD=0: 메시지 35085개를 넣었는데 온전한 것 30241개, **깨진 것 160개**, 나머지(4684개)는 길이 접두가 망가지면서 사라졌다. 태스크의 `prvWriteMessageToBuffer`가 길이를 쓰고 `xHead`를 갱신하기 전에 ISR이 같은 `xHead`에서 자기 메시지를 써 버리기 때문이다. assert도 경고도 없다 — **조용히 데이터가 깨진다**. 블록하지 않는 send 두 개는 커널이 검사할 방법이 없다.
2. GUARD=1: 태스크 쪽 send를 크리티컬 섹션으로 감싸면(이 ISR의 우선순위 0x40은 BASEPRI 0x20으로 가려진다) 37671개 전부 온전하다. `stream_buffer.h` 머리 주석도 같은 처방을 한다: "If there are to be multiple different writers then the application writer must serialize calls to writing API functions ... One way to achieve such serialization ... is to place each API call inside a critical section and use a block time of 0." 그 순간 stream buffer는 "락 없는 가벼운 도구"가 아니게 된다 — 여러 writer라면 큐(D4)가 맞는 도구다.
3. (B) reader 두 개: 두 번째 reader가 블록하려는 순간 `stream_buffer.c:1119`의 `configASSERT( pxStreamBuffer->xTaskWaitingToReceive == NULL )`에 걸렸다. 블록하는 reader / writer 중복은 assert가 잡아 준다(assert를 끈 양산 빌드에서는 먼저 기다리던 태스크의 핸들이 덮어써져 그 태스크가 영원히 안 깨어난다).

### 4.6 sbSEND_COMPLETED — 멀티코어(AMP)에서 코어 사이 링으로 쓰기

`stream_buffer.c` 146~160행의 기본 `sbSEND_COMPLETED`는 스케줄러를 정지하고 `xTaskWaitingToReceive`에 `xTaskNotifyIndexed(..., eNoAction)`을 보내는 매크로다. `#ifndef`로 감싸 있어 **FreeRTOSConfig.h에서 재정의할 수 있다**. 용도는 비대칭 멀티코어(AMP)다. 예를 들어 STM32H745(Cortex-M7 + M4)나 i.MX RT1170에서 각 코어가 자기 FreeRTOS를 돌리고, 공유 SRAM에 stream buffer를 하나 둔다. writer 코어에서는 상대 코어의 reader 태스크 핸들이 의미가 없으니, `sbSEND_COMPLETED`를 "상대 코어에 하드웨어 인터럽트(MU / HSEM / IPCC)를 건다"로 바꾸고, 받는 코어의 그 ISR에서 `xStreamBufferSendCompletedFromISR(sb, &w)`를 불러 reader를 깨운다. V10.5.0(2022-09)부터는 버퍼마다 콜백을 따로 줄 수 있다: `configUSE_SB_COMPLETED_CALLBACK 1` + `xStreamBufferCreateWithCallback(size, trigger, pxSendCompletedCallback, pxReceiveCompletedCallback)` — 일부 버퍼는 코어 간, 일부는 코어 안에서 쓰는 구성이 가능하다. 이 랩의 듀얼 코어 보드(mps2-an521)에서 실제로 돌리는 것은 C10(SMP)·후속 모듈 몫으로 남긴다. FreeRTOS 공식 데모의 AMP 예제가 이 방식을 쓴다(참고 자료 — 데모 소스는 이 랩에 없어 확인 못 함).

---
## 5. Zephyr — `k_event`와 `k_poll`

Zephyr에는 FreeRTOS task notification에 직접 대응하는 것이 없다(A5 §8의 이식 주의점). 용도별로 `k_sem`(카운트, D1), `k_event`(비트), `k_poll`(여러 소스)을 고른다. `k_fifo`·`k_msgq`·`k_pipe`·zbus는 D10에서 다룬다 — 여기서는 `k_poll`이 그것들을 "기다리는" 쪽만 본다.

### 5.1 `k_event` — 32비트, ISR에서 바로 post, 기본이 브로드캐스트

`include/zephyr/kernel.h` 2651행의 구조체는 대기 큐, 32비트 값, 스핀락 셋이다.

```c
struct k_event {
	_wait_q_t         wait_q;
	uint32_t          events;
	struct k_spinlock lock;
	/* + SYS_PORT_TRACING_TRACKING_FIELD(k_event) */
};
```

`CONFIG_EVENTS=y`로 켠다(Kconfig 도움말: "Threads may wait on event objects for specific events, but both threads and ISRs may deliver events"). FreeRTOS event group과 비교하면:

| 항목 | FreeRTOS event group | Zephyr `k_event` |
|---|---|---|
| 사용자 비트 | 24 (32비트 tick 기준) | 32 전부 |
| 세우기 | `xEventGroupSetBits` (OR) | `k_event_post` (OR), `k_event_set` (덮어쓰기), `k_event_set_masked` |
| 지우기 | `xEventGroupClearBits` | `k_event_clear` |
| ISR에서 세우기 | FromISR → **타이머 데몬 위임** | `k_event_post`를 **ISR에서 직접** (스핀락 안에서 대기 큐 훑기) |
| wait-any / wait-all | `xEventGroupWaitBits(…, xWaitForAllBits)` | `k_event_wait` / `k_event_wait_all` |
| 대기 전 클리어 | 없음 (진입 마스크 없음) | `reset=true` 인자: 대기 **전에** 전부 지움 |
| 깨어날 때 소비 | clear-on-exit 인자 | `k_event_wait_safe` / `k_event_wait_all_safe` (맞은 비트만 원자적으로 지움) |
| 랑데부 | `xEventGroupSync` | 없음 (k_barrier 같은 별도 객체 없음 — 직접 조합) |
| RAM (이 랩) | 32 B (static) | 12 B (아래 예제 13) |

Zephyr가 ISR에서 직접 post를 허용한다는 것은 **FreeRTOS가 피한 O(n)을 받아들였다**는 뜻이다. `kernel/events.c`의 `k_event_post_internal`은 `k_spin_lock` 안에서 `z_sched_waitq_walk`로 대기 스레드를 전부 훑는다(단일 코어에서 스핀락 = 인터럽트 잠금). 대기자가 많으면 그만큼 인터럽트가 잠긴다. Zephyr의 선택은 "대기자 수는 설계 시점에 알 수 있고 보통 적다, 그 대신 지연이 짧고 데몬이 필요 없다"이다. 어느 쪽이 맞는지는 시스템에 달렸고, 면접에서는 이 트레이드오프를 말할 수 있으면 된다.

### 5.2 예제 11 — `k_event`의 post / set / wait / reset / safe, ISR post

**무엇을 확인하나**: 두 스레드가 같은 비트를 기다릴 때 post 한 번에 둘 다 깨는지(브로드캐스트), post(OR)와 set(덮어쓰기)의 차이, wait가 비트를 소비하지 않는 것, `k_event_wait_safe`의 원자적 소비, `reset=true`가 기존 비트를 잃게 하는 것, `k_timer` 콜백(ISR 컨텍스트)에서 직접 post. mps2/an385 보드의 기본 tick이 **100 Hz**(`CONFIG_SYS_CLOCK_TICKS_PER_SEC=100`, 빌드의 `.config`에서 확인)라서 처음 돌렸을 때 2 ms 타이머가 10 ms에 울렸다 — prj.conf에서 1000으로 올렸다.

```text
CONFIG_PRINTK=y
CONFIG_EVENTS=y
CONFIG_LOG=n
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000
```

```c
/* z1: Zephyr k_event — post(OR) / set(덮어쓰기) / wait(any) / wait_all / reset / _safe, ISR에서 바로 post */
#include <zephyr/kernel.h>
#define GYRO BIT(0)
#define ACC  BIT(1)
#define BARO BIT(2)
static K_EVENT_DEFINE(ev);
static volatile int64_t t_isr;
static void tmr_cb(struct k_timer *t) { ARG_UNUSED(t); t_isr = k_uptime_get(); k_event_post(&ev, BARO); }   /* ISR 컨텍스트 */
static K_TIMER_DEFINE(tmr, tmr_cb, NULL);
static void waiter(void *a, void *b, void *c) {       /* 두 스레드가 같은 비트를 기다린다 */
    ARG_UNUSED(b); ARG_UNUSED(c);
    uint32_t r = k_event_wait(&ev, BIT(5), false, K_FOREVER);
    printk("  waiter%d woke: 0x%x\n", (int)(uintptr_t)a, r);
}
K_THREAD_DEFINE(w1, 512, waiter, 1, NULL, NULL, 3, 0, 0);
K_THREAD_DEFINE(w2, 512, waiter, 2, NULL, NULL, 3, 0, 0);
int main(void) {
    k_msleep(1);                                       /* w1, w2가 먼저 블록 */
    printk("post BIT5 → waiters (broadcast):\n");
    k_event_post(&ev, BIT(5));
    k_msleep(1);
    printk("events now 0x%x (post는 비트를 남긴다)\n", k_event_test(&ev, 0xffffffff));
    k_event_post(&ev, GYRO);
    printk("post GYRO      -> 0x%x\n", k_event_test(&ev, 0xffffffff));
    k_event_set(&ev, ACC);
    printk("set ACC        -> 0x%x (set은 덮어쓴다)\n", k_event_test(&ev, 0xffffffff));
    uint32_t r = k_event_wait_all(&ev, GYRO | ACC, false, K_MSEC(5));
    printk("wait_all(G|A, 5ms) -> 0x%x (GYRO 없음 → timeout)\n", r);
    k_event_post(&ev, GYRO);
    k_timer_start(&tmr, K_MSEC(2), K_NO_WAIT);          /* 2 ms 뒤 ISR이 BARO post */
    int64_t t0 = k_uptime_get();
    r = k_event_wait_all(&ev, GYRO | ACC | BARO, false, K_MSEC(10));
    printk("wait_all(G|A|B) -> 0x%x after %d ms (ISR post at +%d ms)\n", r, (int)(k_uptime_get() - t0), (int)(t_isr - t0));
    printk("events still 0x%x (wait는 소비하지 않는다)\n", k_event_test(&ev, 0xffffffff));
    r = k_event_wait_safe(&ev, GYRO | ACC, false, K_NO_WAIT);
    printk("wait_safe(G|A) -> 0x%x, events now 0x%x (맞은 비트만 원자적으로 소비)\n", r,
           k_event_test(&ev, 0xffffffff));
    r = k_event_wait(&ev, BARO, true, K_MSEC(3));
    printk("wait(BARO, reset=true) -> 0x%x (대기 전에 전부 지워서 BARO도 잃음)\n", r);
    return 0;
}
```

```sh
rtos-zbuild mps2/an385 ~/.rtos_scratch/D5/z1 /tmp/rtos/D5/z1
timeout 3 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
    -icount shift=5,align=off,sleep=off -kernel /tmp/rtos/D5/z1/zephyr/zephyr.elf
```

```text
*** Booting Zephyr OS build v4.4.2 ***
post BIT5 → waiters (broadcast):
  waiter1 woke: 0x20
  waiter2 woke: 0x20
events now 0x20 (post는 비트를 남긴다)
post GYRO      -> 0x21
set ACC        -> 0x2 (set은 덮어쓴다)
wait_all(G|A, 5ms) -> 0x0 (GYRO 없음 → timeout)
wait_all(G|A|B) -> 0x7 after 2 ms (ISR post at +2 ms)
events still 0x7 (wait는 소비하지 않는다)
wait_safe(G|A) -> 0x3, events now 0x4 (맞은 비트만 원자적으로 소비)
wait(BARO, reset=true) -> 0x0 (대기 전에 전부 지워서 BARO도 잃음)
```

**출력에서 볼 것**(QEMU는 `timeout 3`이 끝낸다 — stderr의 종료 메시지는 이 출력에 없다):

1. post 한 번에 waiter1, waiter2가 **둘 다** 깨었다. `k_event`는 기본이 브로드캐스트이고, `_safe`가 아닌 wait는 비트를 소비하지 않으니 post 뒤에도 0x20이 남는다.
2. post는 OR(0x20 → 0x21), set은 덮어쓰기(→ 0x2). set으로 "현재 상태"를 통째로 표현하는 용법(예: 링크 상태 비트 묶음)과 post로 "사건"을 쌓는 용법을 구분한다.
3. wait_all은 GYRO가 없어 타임아웃 0을 반환했다. Zephyr는 타임아웃이면 **0**을 준다(FreeRTOS는 그 순간의 비트 값을 준다 — 예제 5의 0x3과 다르다).
4. timer ISR이 2 ms 뒤 BARO를 post하자 wait_all이 정확히 2 ms에 0x7로 끝났다.
5. `k_event_wait_safe(G|A)`는 0x3을 받고 **그 두 비트만** 지워 0x4를 남겼다. 반면 `k_event_wait(BARO, reset=true)`는 대기 전에 전부 지워서 이미 서 있던 BARO까지 잃고 3 ms 타임아웃으로 0을 반환했다. `kernel.h` 주석이 `_safe` 판을 "mitigates the need for calling `k_event_clear`, or passing a reset argument, since doing so may result in lost event information"이라고 설명하는 이유다. 신호를 놓치면 안 되는 경로(센서 준비, DMA 완료)는 `_safe`를 쓴다.

### 5.3 `k_poll` — 서로 다른 객체 여러 개를 한 번에 기다리기

`k_poll`은 POSIX `poll()`의 RTOS판이다. `struct k_poll_event` 배열을 넘기면 그중 하나라도 준비되면 반환한다. `CONFIG_POLL=y`가 필요하다. 기다릴 수 있는 종류(`kernel.h` 6540행 근처, v4.4.2):

- `K_POLL_TYPE_SEM_AVAILABLE` — `k_sem` 카운트 > 0
- `K_POLL_TYPE_DATA_AVAILABLE` = `K_POLL_TYPE_FIFO_DATA_AVAILABLE` — `k_queue` / `k_fifo` / `k_lifo`가 비어 있지 않음
- `K_POLL_TYPE_MSGQ_DATA_AVAILABLE` — `k_msgq`에 메시지 있음
- `K_POLL_TYPE_PIPE_DATA_AVAILABLE` — `k_pipe`에 데이터 있음
- `K_POLL_TYPE_SIGNAL` — `k_poll_signal_raise()`로 올린 signal (signal은 값 하나 `result`를 실어 보낼 수 있다)
- `K_POLL_TYPE_IGNORE` — 배열 칸 비활성

중요한 성질 세 가지가 `kernel/poll.c`에 그대로 있다.

1. **`k_poll`은 가져가지 않는다.** `is_condition_met`는 `k_sem_count_get(event->sem) > 0U`처럼 **보기만** 한다. 반환 후 `k_sem_take(…, K_NO_WAIT)`나 `k_fifo_get(…, K_NO_WAIT)`로 직접 꺼내야 하고, 그 사이 다른 스레드가 먼저 가져갈 수 있다(그래서 꺼내기는 반드시 K_NO_WAIT + 실패 처리).
2. **state는 누적되고 커널이 지워 주지 않는다.** `set_event_ready`는 `event->state |= state;`다. 다음 `k_poll` 전에 `event.state = K_POLL_STATE_NOT_READY`로 사용자가 되돌려야 한다. signal도 `k_poll_signal_reset()`으로 사용자가 내려야 한다(`kernel.h`: "stays set until the user sets it back to 0 with k_poll_signal_reset()").
3. **한 객체에 poller가 여럿이면 깨움은 하나에게만 간다.** `z_impl_k_poll_signal_raise`는 `sys_dlist_get(&sig->poll_events)`로 등록된 poll event를 **하나만** 꺼내 신호를 준다. `add_event`가 우선순위 순으로 넣으므로 가장 높은 우선순위 poller다. 다른 poller는 signal이 서 있는데도 계속 잔다.

### 5.4 예제 12 — sem · fifo · signal 셋을 하나의 k_poll로, 그리고 함정 둘

**무엇을 확인하나**: 1 ms마다 timer ISR이 sem give → fifo put → signal raise(result 42)를 차례로 한다. 메인은 `k_poll`로 셋을 기다리고 준비된 것을 꺼낸다. (A) 매 라운드 state를 NOT_READY로 되돌리는 올바른 코드, (B) 되돌리지 않는 코드, (C) 우선순위 3과 4인 두 스레드가 같은 signal을 poll할 때 raise 한 번의 결과.

```c
/* z2: k_poll — sem / fifo / signal 셋을 한 번에 기다리기. state 재초기화를 빼먹으면? 한 signal에 poller 둘이면? */
#include <zephyr/kernel.h>
K_SEM_DEFINE(sem, 0, 1);
K_FIFO_DEFINE(fifo);
static struct k_poll_signal sig = K_POLL_SIGNAL_INITIALIZER(sig);
static struct item { void *reserved; int v; } it = {NULL, 7};
static int step;
static void tmr_cb(struct k_timer *t) {               /* ISR: 1 ms마다 소스 하나씩 */
    ARG_UNUSED(t);
    if (step == 0) k_sem_give(&sem);
    else if (step == 1) k_fifo_put(&fifo, &it);
    else if (step == 2) k_poll_signal_raise(&sig, 42);
    step++;
}
static K_TIMER_DEFINE(tmr, tmr_cb, NULL);
static struct k_poll_event ev[3] = {
    K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &sem, 0),
    K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &fifo, 0),
    K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig, 0),
};
static void poll_round(int reset_state) {
    step = 0; k_timer_start(&tmr, K_MSEC(1), K_MSEC(1));
    for (int r = 0; r < 3; r++) {
        int rc = k_poll(ev, 3, K_MSEC(20));
        printk("  round %d rc=%d states=[0x%x 0x%x 0x%x]", r, rc, ev[0].state, ev[1].state, ev[2].state);
        if (ev[0].state == K_POLL_STATE_SEM_AVAILABLE)
            printk(" sem_take=%d", k_sem_take(&sem, K_NO_WAIT));       /* poll은 가져가지 않는다 */
        if (ev[1].state == K_POLL_STATE_FIFO_DATA_AVAILABLE)
            printk(" fifo_get=%s", k_fifo_get(&fifo, K_NO_WAIT) ? "item" : "NULL");
        if (ev[2].state == K_POLL_STATE_SIGNALED) {
            unsigned int s; int res;
            k_poll_signal_check(&sig, &s, &res); k_poll_signal_reset(&sig);
            printk(" signal result=%d", res);
        }
        printk("\n");
        if (reset_state) for (int i = 0; i < 3; i++) ev[i].state = K_POLL_STATE_NOT_READY;
    }
    k_timer_stop(&tmr);
    for (int i = 0; i < 3; i++) ev[i].state = K_POLL_STATE_NOT_READY;
}
static void poller(void *a, void *b, void *c) {       /* 같은 signal을 두 스레드가 poll */
    ARG_UNUSED(b); ARG_UNUSED(c);
    struct k_poll_event e = K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig);
    int rc = k_poll(&e, 1, K_MSEC(30));
    printk("  poller%d rc=%d at %u ms\n", (int)(uintptr_t)a, rc, (unsigned)k_uptime_get_32());
}
K_THREAD_DEFINE(p1, 768, poller, 1, NULL, NULL, 3, 0, 100);
K_THREAD_DEFINE(p2, 768, poller, 2, NULL, NULL, 4, 0, 100);
int main(void) {
    printk("(A) state를 매번 NOT_READY로 되돌림:\n");  poll_round(1);
    printk("(B) 되돌리지 않음:\n");                    poll_round(0);
    k_poll_signal_reset(&sig);
    k_msleep(110 - k_uptime_get_32());                  /* p1, p2가 100 ms에 poll 시작 */
    printk("(C) raise once at %u ms\n", (unsigned)k_uptime_get_32());
    k_poll_signal_raise(&sig, 1);
    k_msleep(50);
    return 0;
}
```

```text
             RAM:        9296 B         4 MB      0.22%
*** Booting Zephyr OS build v4.4.2 ***
(A) state를 매번 NOT_READY로 되돌림:
  round 0 rc=0 states=[0x2 0x0 0x0] sem_take=0
  round 1 rc=0 states=[0x0 0x4 0x0] fifo_get=item
  round 2 rc=0 states=[0x0 0x0 0x1] signal result=42
(B) 되돌리지 않음:
  round 0 rc=0 states=[0x2 0x0 0x0] sem_take=0
  round 1 rc=0 states=[0x2 0x4 0x0] sem_take=-16 fifo_get=item
  round 2 rc=0 states=[0x2 0x4 0x1] sem_take=-16 fifo_get=NULL signal result=42
(C) raise once at 111 ms
  poller1 rc=0 at 111 ms
  poller2 rc=-11 at 132 ms
```

**출력에서 볼 것**:

1. (A) 라운드마다 준비된 칸 하나만 state가 서 있고(0x2 = SEM_AVAILABLE, 0x4 = FIFO_DATA_AVAILABLE, 0x1 = SIGNALED) 꺼내기가 모두 성공했다.
2. (B) state를 안 되돌리면 라운드 1에서 sem 칸이 **이전 라운드의 0x2를 그대로** 들고 있어 코드가 sem을 또 꺼내려 하고 `k_sem_take`가 −16(`-EBUSY`)으로 실패했다. 라운드 2에서는 fifo 칸도 낡은 상태라 `k_fifo_get`이 NULL. 이런 코드가 실무에서는 "가끔 NULL 포인터 역참조" HardFault로 나타난다.
3. (C) raise 한 번에 **poller1(우선순위 3)만** 깨어났고, poller2(우선순위 4)는 signal이 서 있는데도 30 ms 타임아웃(`rc=-11` = `-EAGAIN`)까지 잤다. 브로드캐스트가 필요하면 `k_poll_signal`이 아니라 `k_event`를 쓴다.

### 5.5 예제 13 — Zephyr 세 가지 신호의 비용과 크기

**무엇을 확인하나**: 예제 3과 같은 측정(thread→thread, ISR→thread, 1000번 평균, FPGAIO cycle)을 `k_sem`(0), `k_event`(1, `k_event_wait_safe`), `k_poll_signal`(2, `k_poll` + reset)로. ISR은 `IRQ_CONNECT(8, …)` + `NVIC_SetPendingIRQ(8)`로 소프트웨어 트리거했다. 빌드 옵션으로 MODE를 넘긴다(`west build … -- -DMODE=n`).

```text
CONFIG_PRINTK=y
CONFIG_POLL=y
CONFIG_EVENTS=y
CONFIG_LOG=n
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000
```

```c
/* z3: Zephyr 신호 비용 — k_sem(0) / k_event(1) / k_poll_signal(2). thread→thread, ISR→thread (FPGAIO 25 MHz) */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#define CYC() (*(volatile uint32_t *)0x40028018u)
#define N 1000
K_SEM_DEFINE(sem, 0, 1);
K_EVENT_DEFINE(ev);
static struct k_poll_signal sig = K_POLL_SIGNAL_INITIALIZER(sig);
static volatile uint32_t t0, phase;
static inline void give(void) {
#if MODE == 0
    k_sem_give(&sem);
#elif MODE == 1
    k_event_post(&ev, 1);
#else
    k_poll_signal_raise(&sig, 0);
#endif
}
static inline void wait(void) {
#if MODE == 0
    k_sem_take(&sem, K_FOREVER);
#elif MODE == 1
    k_event_wait_safe(&ev, 1, false, K_FOREVER);       /* 받은 비트를 원자적으로 소비 */
#else
    struct k_poll_event e = K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig);
    k_poll(&e, 1, K_FOREVER);
    k_poll_signal_reset(&sig);
#endif
}
static void isr(const void *arg) { ARG_UNUSED(arg); give(); }
static void high(void *a, void *b, void *c) {          /* 우선순위 2: 기다린다 */
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    static const char *nm[2] = {"thread->thread", "ISR->thread   "};
    for (phase = 0; phase < 2; phase++) {
        uint32_t sum = 0, mx = 0;
        for (int i = 0; i < N; i++) {
            wait();
            uint32_t d = CYC() - t0; sum += d; if (d > mx) mx = d;
        }
        printk("MODE=%d %s avg %u.%u cyc max %u cyc\n", MODE, nm[phase], sum / N, sum % N * 10 / N, mx);
    }
    k_sleep(K_FOREVER);
}
K_THREAD_DEFINE(th, 1024, high, NULL, NULL, NULL, 2, 0, 0);
int main(void) {                                        /* main = 우선순위 0 → 5로 낮춘다 */
    IRQ_CONNECT(8, 2, isr, NULL, 0);
    irq_enable(8);
    k_thread_priority_set(k_current_get(), 5);
    while (phase < 2) {
        t0 = CYC();
        if (phase == 0) give();
        else NVIC_SetPendingIRQ(8);
    }
    return 0;
}
```

```sh
export XDG_CACHE_HOME=/tmp/rtos/D5/cache
for m in 0 1 2; do
  /var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/D5/z3 /tmp/rtos/D5/z3_$m -- -DMODE=$m 2>&1 | grep -i "warn\|error"
  timeout 5 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -icount shift=5,align=off,sleep=off -kernel /tmp/rtos/D5/z3_$m/zephyr/zephyr.elf | grep MODE
done
```

```text
MODE=0 thread->thread avg 473.0 cyc max 473 cyc
MODE=0 ISR->thread    avg 480.0 cyc max 480 cyc
MODE=1 thread->thread avg 539.2 cyc max 540 cyc
MODE=1 ISR->thread    avg 548.8 cyc max 549 cyc
MODE=2 thread->thread avg 555.2 cyc max 556 cyc
MODE=2 ISR->thread    avg 565.6 cyc max 566 cyc
```

객체 크기는 각 빌드의 심볼 크기로 봤다.

```sh
arm-none-eabi-nm -S z3_0/zephyr/zephyr.elf | grep -E " (sem)$"     # 이하 빌드별로 같은 방식
```

```text
# z3_0 (k_sem)
200000a4 00000018 D sem
# z3_1 (k_event)
200000a4 0000000c D ev
# z3_2 (k_poll_signal)
20000000 00000010 d sig
# z2 (k_poll_event[3], k_fifo)
20000000 0000003c d ev
20000150 00000018 D fifo
```

**출력에서 볼 것**: Zephyr 안에서는 `k_sem`이 가장 가볍고(473 / 480 cycle), `k_event`가 14% 더, `k_poll_signal`이 17% 더 든다. `k_event`는 깨운 뒤 조건 재검사와 소비 단계가 있고, `k_poll`은 매번 이벤트 등록 → 대기 → 등록 해제를 하기 때문이다. 크기는 `k_sem` 24 B(대기 큐 8 + count 4 + limit 4 + `CONFIG_POLL`의 poll_events 리스트 8), `k_event` 12 B(대기 큐 8 + events 4 + 단일 코어라 0 B인 스핀락), `k_poll_signal` 16 B, `k_poll_event` 하나 20 B(3개 배열 60 B), `k_fifo` 24 B다. FreeRTOS(예제 3 tpl: 196~242 cycle)보다 2배 이상 큰 숫자는 같은 보드 · 같은 icount지만 **설정이 다르다**(Zephyr 기본은 `CONFIG_TIMESLICING`, 스택 정보, 스핀락 · 스케줄러 경로가 더 일반적이다) — RTOS 간 우열로 읽지 말고, 각 RTOS 안에서의 상대 비교로만 쓴다. Zephyr 커널 경로 비용 자체는 C2·C12에서 다룬다.

---

## 6. 무엇을 쓸까 — 결정 표

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah10" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">무엇을 쓸까 — 질문 4개로 고르기</text>
<rect x="230.0" y="40.0" width="220.0" height="44.0" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340.0" y="66.5" font-size="13" text-anchor="middle">데이터가 함께 가나?</text>
<rect x="20.0" y="120.0" width="250.0" height="44.0" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="145.0" y="146.6" font-size="13" text-anchor="middle">바이트 흐름 / 가변 길이인가?</text>
<rect x="410.0" y="120.0" width="250.0" height="44.0" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="535.0" y="146.6" font-size="13" text-anchor="middle">받는 태스크가 정확히 1개?</text>
<rect x="15.0" y="210.0" width="125.0" height="56.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="77.5" y="234.1" font-size="13" text-anchor="middle">stream /</text>
<text x="77.5" y="251.1" font-size="13" text-anchor="middle">message buffer</text>
<rect x="150.0" y="210.0" width="125.0" height="56.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="212.5" y="234.1" font-size="13" text-anchor="middle">queue (D4)</text>
<text x="212.5" y="251.1" font-size="13" text-anchor="middle">고정 크기 항목</text>
<rect x="405.0" y="210.0" width="125.0" height="56.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="467.5" y="234.1" font-size="13" text-anchor="middle">task</text>
<text x="467.5" y="251.1" font-size="13" text-anchor="middle">notification</text>
<rect x="540.0" y="210.0" width="125.0" height="56.0" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="602.5" y="234.1" font-size="13" text-anchor="middle">여러 조건 /</text>
<text x="602.5" y="251.1" font-size="13" text-anchor="middle">여러 수신자?</text>
<rect x="400.0" y="310.0" width="130.0" height="56.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="465.0" y="334.1" font-size="13" text-anchor="middle">event group</text>
<text x="465.0" y="351.1" font-size="13" text-anchor="middle">k_event</text>
<rect x="540.0" y="310.0" width="130.0" height="56.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="605.0" y="334.1" font-size="13" text-anchor="middle">semaphore (D1)</text>
<text x="605.0" y="351.1" font-size="13" text-anchor="middle">/ k_poll</text>
<line x1="300.0" y1="84.0" x2="160.0" y2="118.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah10)"/>
<text x="200.0" y="100.0" font-size="12" text-anchor="middle">예</text>
<line x1="380.0" y1="84.0" x2="520.0" y2="118.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah10)"/>
<text x="470.0" y="100.0" font-size="12" text-anchor="middle">아니오 (신호만)</text>
<line x1="90.0" y1="164.0" x2="80.0" y2="208.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah10)"/>
<text x="68.0" y="190.0" font-size="12" text-anchor="middle">예</text>
<line x1="200.0" y1="164.0" x2="210.0" y2="208.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah10)"/>
<text x="232.0" y="190.0" font-size="12" text-anchor="middle">아니오</text>
<line x1="480.0" y1="164.0" x2="470.0" y2="208.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah10)"/>
<text x="455.0" y="190.0" font-size="12" text-anchor="middle">예</text>
<line x1="590.0" y1="164.0" x2="600.0" y2="208.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah10)"/>
<text x="624.0" y="190.0" font-size="12" text-anchor="middle">아니오</text>
<line x1="580.0" y1="266.0" x2="470.0" y2="308.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah10)"/>
<text x="505.0" y="296.0" font-size="12" text-anchor="middle">비트 조합</text>
<line x1="610.0" y1="266.0" x2="605.0" y2="308.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah10)"/>
<text x="640.0" y="296.0" font-size="12" text-anchor="middle">개수·소스</text>
<text x="150.0" y="330.0" font-size="12" text-anchor="middle">모두 ISR에서 보낼 수 있다</text>
<text x="150.0" y="350.0" font-size="12" text-anchor="middle">단 FreeRTOS event group은 데몬 경유</text>
<text x="150.0" y="370.0" font-size="12" text-anchor="middle">Zephyr는 여러 객체를 k_poll로 묶는다</text>
</svg>
```
그림 10 — 고르는 순서. 데이터가 같이 가면 큐나 버퍼, 신호뿐이면 받는 쪽 수와 조건 모양으로 고른다.

| 상황 | FreeRTOS | Zephyr | 이유 |
|---|---|---|---|
| ISR → 정해진 태스크 1개, "일어났다"만 | `vTaskNotifyGiveFromISR` | `k_sem_give` | 가장 짧은 경로, 객체 RAM 0 (FreeRTOS) |
| ISR → 태스크 1개, 원인 여러 개(half/full/error) | `xTaskNotifyFromISR(…, eSetBits)` | `k_event_post` + `_safe` wait | 비트로 원인 구분, 데몬 없음 |
| 최신 값 하나만 의미 있음(최신 고도) | `eSetValueWithOverwrite` | `k_msgq` 길이 1 + purge, 또는 원자 변수 (D10, D8) | 덮어쓰기 mailbox |
| 여러 태스크가 서로 다른 비트 조합을 기다림 | event group (데몬 우선순위 주의) | `k_event` | 대기 리스트가 필요 |
| 여러 태스크의 시작점 맞추기(랑데부) | `xEventGroupSync` | `k_event` 조합 또는 세마포어 쌍 | set + wait 원자성 |
| 신호 횟수가 중요(샘플 개수, 자원 수) | notification `eIncrement` 또는 counting semaphore | `k_sem` | 비트는 횟수를 잃는다 |
| ISR → 태스크 바이트 흐름(UART, 오디오) | stream buffer (+ trigger level) | `k_pipe` 또는 ring_buf + `k_sem` (D10) | 복사 1번, 경계 불필요 |
| 가변 길이 패킷, writer · reader 각 1개 | message buffer | `k_msgq`(고정 크기) 또는 `k_pipe` + 길이 (D10) | 길이 접두 |
| writer 여럿 또는 reader 여럿 | 큐 (D4) | `k_msgq` / `k_fifo` (D10) | stream buffer는 SWSR 전용 |
| 서로 다른 객체 여러 개 중 아무거나 | 큐 셋 (D4) 또는 notification 비트로 통합 | `k_poll` | 다중 소스 대기 |
| 수신자 여러 명에게 같은 사건 알림 | event group | `k_event` (브로드캐스트) 또는 zbus (D10) | notification · k_poll_signal은 1명만 |
| 코어 간(AMP) 바이트 · 메시지 | stream / message buffer + `sbSEND_COMPLETED` 재정의 | IPM / mbox + ring_buf (확인 범위 밖) | 공유 메모리 링 + 하드웨어 doorbell |

실무에서 가장 흔한 선택은 단순하다: **ISR → 태스크는 notification(FreeRTOS) / `k_sem`(Zephyr), 데이터는 큐 · stream buffer, 여러 조건 조합만 event group / `k_event`**.

### 예제 14 — 측정 표의 비율을 계산한 Python

**무엇을 확인하나**: 본문의 "몇 % ↓" 숫자가 측정 출력에서 어떻게 나왔는지. 예제 3·13의 평균값을 그대로 넣고 (a − b) / a를 계산했다(VM 아님, Mac의 python3).

```python
for a,b in [(242.3,196.2),(229.2,203.4),(122.5,88.1),(206.6,161.0),(193.8,168.1),(169.0,144.2),(159.4,138.6),(74.6,76.9),(473.0,539.2),(480,548.8),(473,555.2),(480,565.6)]: print(a,b,round((a-b)/a*100,1))
```

```text
242.3 196.2 19.0
229.2 203.4 11.3
122.5 88.1 28.1
206.6 161.0 22.1
193.8 168.1 13.3
169.0 144.2 14.7
159.4 138.6 13.0
74.6 76.9 -3.1
473.0 539.2 -14.0
480 548.8 -14.3
473 555.2 -17.4
480 565.6 -17.8
```

**출력에서 볼 것**: 음수는 "더 느리다"는 뜻이다(noassert give+take의 notification −3.1%, Zephyr의 `k_event`·`k_poll_signal`이 `k_sem`보다 14~18% 느림). 그림 3·5·7·9의 막대와 점 좌표도 같은 숫자를 Python으로 환산해서 그렸다.

---

## 7. RTOS 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah11" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">소스 지도 — 어디서 무엇이 일어나나 (FreeRTOS V11.3.2 · Zephyr v4.4.2)</text>
<rect x="15.0" y="44.0" width="320.0" height="150.0" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="175.0" y="83.2" font-size="12" text-anchor="middle" font-weight="bold">tasks.c</text>
<text x="175.0" y="99.2" font-size="12" text-anchor="middle">TCB_t.ulNotifiedValue[ ] · ucNotifyState[ ]</text>
<text x="175.0" y="115.2" font-size="12" text-anchor="middle">xTaskGenericNotify / FromISR</text>
<text x="175.0" y="131.2" font-size="12" text-anchor="middle">ulTaskGenericNotifyTake</text>
<text x="175.0" y="147.2" font-size="12" text-anchor="middle">xTaskGenericNotifyWait</text>
<text x="175.0" y="163.2" font-size="12" text-anchor="middle">vTaskRemoveFromUnorderedEventList</text>
<rect x="345.0" y="44.0" width="320.0" height="150.0" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="505.0" y="83.2" font-size="12" text-anchor="middle" font-weight="bold">stream_buffer.c</text>
<text x="505.0" y="99.2" font-size="12" text-anchor="middle">prvWriteMessageToBuffer → …WriteBytes…</text>
<text x="505.0" y="115.2" font-size="12" text-anchor="middle">prvBytesInBufferMeetTriggerLevel</text>
<text x="505.0" y="131.2" font-size="12" text-anchor="middle">sbSEND_COMPLETED → xTaskNotifyIndexed</text>
<text x="505.0" y="147.2" font-size="12" text-anchor="middle">uxNotificationIndex (기본 0)</text>
<text x="505.0" y="163.2" font-size="12" text-anchor="middle">configASSERT: writer·reader 1개</text>
<rect x="15.0" y="210.0" width="320.0" height="150.0" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="175.0" y="249.2" font-size="12" text-anchor="middle" font-weight="bold">event_groups.c + timers.c</text>
<text x="175.0" y="265.2" font-size="12" text-anchor="middle">EventGroup_t: uxEventBits + 대기 리스트</text>
<text x="175.0" y="281.2" font-size="12" text-anchor="middle">xEventGroupSetBits: 스케줄러 정지 + 훑기</text>
<text x="175.0" y="297.2" font-size="12" text-anchor="middle">FromISR → xTimerPendFunctionCallFromISR</text>
<text x="175.0" y="313.2" font-size="12" text-anchor="middle">vEventGroupSetBitsCallback (데몬에서)</text>
<text x="175.0" y="329.2" font-size="12" text-anchor="middle">xEventGroupSync: set + wait-all 원자적</text>
<rect x="345.0" y="210.0" width="320.0" height="150.0" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="505.0" y="249.2" font-size="12" text-anchor="middle" font-weight="bold">zephyr/kernel/events.c · poll.c</text>
<text x="505.0" y="265.2" font-size="12" text-anchor="middle">struct k_event: wait_q + events + spinlock</text>
<text x="505.0" y="281.2" font-size="12" text-anchor="middle">k_event_post_internal → waitq_walk</text>
<text x="505.0" y="297.2" font-size="12" text-anchor="middle">k_poll: register_events → z_pend_curr</text>
<text x="505.0" y="313.2" font-size="12" text-anchor="middle">signal_poll_event: state |= …</text>
<text x="505.0" y="329.2" font-size="12" text-anchor="middle">k_poll_signal_raise: 대기 event 1개만</text>
<line x1="335.0" y1="120.0" x2="343.0" y2="120.0" stroke="currentColor" stroke-width="1.3"/>
</svg>
```
그림 11 — 이 노트의 개념이 구현된 함수들. 위 두 칸(notification, stream buffer)은 서로 이어져 있다 — stream buffer가 깨움에 notification을 쓴다.

### 7.1 notification: 이벤트 리스트를 거치지 않는 깨움

`xTaskGenericNotify`(`tasks.c` 8010행~)의 뼈대는 이렇다(위에서 본 소스 그대로, 주석 줄임).

```c
taskENTER_CRITICAL();
{
    ucOriginalNotifyState = pxTCB->ucNotifyState[ uxIndexToNotify ];
    pxTCB->ucNotifyState[ uxIndexToNotify ] = taskNOTIFICATION_RECEIVED;
    switch( eAction ) { /* eSetBits: |=, eIncrement: ++, eSetValueWith(out)Overwrite, eNoAction */ }
    if( ucOriginalNotifyState == taskWAITING_NOTIFICATION )
    {
        listREMOVE_ITEM( &( pxTCB->xStateListItem ) );     /* delayed 또는 suspended 리스트에서 */
        prvAddTaskToReadyList( pxTCB );
        configASSERT( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) == NULL );
        taskYIELD_ANY_CORE_IF_USING_PREEMPTION( pxTCB );
    }
}
taskEXIT_CRITICAL();
```

semaphore give(`xQueueGenericSend`)와 비교하면 차이가 보인다. semaphore는 (1) 큐 구조체의 `uxMessagesWaiting` 검사와 증가, (2) `xTasksWaitingToReceive` **이벤트 리스트**에서 맨 앞 태스크를 꺼내는 `xTaskRemoveFromEventList`, (3) 그 태스크를 delayed 리스트에서도 빼고 ready로 — 리스트 두 개를 건드린다. notification은 이벤트 리스트가 없으니(`xEventListItem`이 어디에도 안 걸려 있다는 assert가 그 증거) **상태 리스트 하나만** 옮긴다. 받는 쪽 `ulTaskGenericNotifyTake`(7797행~)는 값이 0이고 기다릴 의사가 있을 때만 `vTaskSuspendAll` + 크리티컬 섹션 안에서 `ucNotifyState = taskWAITING_NOTIFICATION`으로 표시하고 `prvAddCurrentTaskToDelayedList`로 블록한다. 주석이 "We MUST enter a critical section to atomically check if a notification has occurred and set the flag ... If we do not do so, a notification sent from an ISR will get lost"라고 lost-wakeup을 설명한다.

ISR판 `vTaskGenericNotifyGiveFromISR`(8316행~)는 `portASSERT_IF_INTERRUPT_PRIORITY_INVALID()`로 ISR 우선순위를 검사(E1)하고 `taskENTER_CRITICAL_FROM_ISR`로 들어간다. 스케줄러가 정지돼 있으면(`uxSchedulerSuspended != 0`) ready 리스트 대신 `xPendingReadyList`에 넣는다(C1). `vTaskSuspend`·`vTaskResume` 쪽에도 notification 대기 상태를 정리하는 루프가 있다(2620, 3239행 근처: 알림을 기다리다 suspend된 태스크의 상태를 NOT_WAITING으로 되돌린다).

### 7.2 event group: 정렬 안 된 리스트 + 스케줄러 정지

`xEventGroupSetBits`(547행~)는 `vTaskSuspendAll()` → 비트 OR → 대기 리스트 전체를 돌며 각 항목 값에서 제어 바이트(`eventEVENT_BITS_CONTROL_BYTES`)를 떼어 조건 검사 → 맞으면 `vTaskRemoveFromUnorderedEventList(pxListItem, uxEventBits | eventUNBLOCKED_DUE_TO_BIT_SET)`로 깨우고 "깨어날 때의 비트 값"을 항목 값에 넣어 준다 → 루프가 끝난 뒤 clear-on-exit 비트를 한꺼번에 지움 → `xTaskResumeAll()`. 깨어난 태스크는 `xEventGroupWaitBits` 안에서 `uxTaskResetEventItemValue()`로 그 값을 읽어 `eventUNBLOCKED_DUE_TO_BIT_SET`이 있으면 "비트로 깨어남", 없으면 "타임아웃"으로 판단한다. 인터럽트를 막지 않는 대신 O(n)을 스케줄러 정지 아래 두는 설계이고, 그래서 ISR에서는 `xTimerPendFunctionCallFromISR`로 데몬에 미룬다(예제 7). 콜백 `vEventGroupSetBitsCallback`은 4 B짜리 `b.w xEventGroupSetBits` 한 줄이다(objdump로 확인).

### 7.3 stream buffer: 핸들 두 칸 + notification

`StreamBuffer_t`(233~252행)에는 대기 리스트가 없고 `xTaskWaitingToReceive`·`xTaskWaitingToSend` 핸들 두 칸뿐이다. 블록은 "핸들 칸에 나를 적고(assert로 비어 있는지 확인) → `xTaskNotifyWaitIndexed(uxNotificationIndex, 0, 0, NULL, xTicksToWait)`", 깨움은 `sbSEND_COMPLETED` / `sbRECEIVE_COMPLETED` 매크로가 `xTaskNotifyIndexed(…, eNoAction)`. 바이트 복사는 `prvWriteBytesToBuffer`(1488행~)가 링 끝에서 두 번에 나눠 `memcpy`하고, 이것을 감싸는 `prvWriteMessageToBuffer`(1012행~)가 message buffer면 길이 접두를 먼저 쓴다. 쓰기 경로에는 크리티컬 섹션이 없다(블록 판단 부분에만 있다) — 이것이 SWSR 가정의 대가이자 이득이다.

### 7.4 Zephyr: 대기 큐 훑기와 poller 등록

`k_event_post_internal`(`kernel/events.c` 178행~)은 `k_spin_lock` → 새 값 계산 → `z_sched_waitq_walk(&event->wait_q, event_walk_op, …)`로 대기 스레드마다 `are_wait_conditions_met`(any면 `current & desired`, all이면 전부 일치) 검사 → 맞으면 `z_abort_thread_timeout` + `z_sched_wake_thread_locked` → 소비된 비트(`K_EVENT_OPTION_CLEAR`, 즉 `_safe` 판)를 지움 → `z_reschedule`. `CONFIG_WAITQ_SCALABLE`(rbtree 대기 큐)이면 훑는 동안 지울 수 없어서 리스트를 따로 모았다가 깨운다(`event_post_walk_op`). 이 랩 빌드는 `CONFIG_WAITQ_SIMPLE=y`였다.

`z_impl_k_poll`(`kernel/poll.c` 283행~)은 `register_events`로 배열을 돌며 이미 준비된 것은 `set_event_ready`, 아니면 각 객체의 `poll_events` 리스트에 이 이벤트를 우선순위 순(`add_event`)으로 건다 → 하나도 준비 안 됐으면 `z_pend_curr`로 잔다 → 깨면 `clear_event_registrations`로 전부 떼어 낸다. 객체 쪽(`k_sem_give`, `k_queue_insert` 등)은 `z_handle_obj_poll_events`를 불러 등록된 poll event 하나를 깨운다. 흥미로운 점은 `z_pend_curr`에 넘기는 대기 큐가 함수 안의 `static _wait_q_t wait_q`, 즉 **모든 poller가 공유하는 큐 하나**라는 것이다 — 깨울 때는 큐를 거치지 않고 `signal_poller`가 poller 스레드를 직접 unpend하므로 문제가 없다.

모듈 연결: 대기 리스트와 ready 리스트 자체는 C1, `taskYIELD`·PendSV는 C2, 크리티컬 섹션과 스케줄러 정지의 차이는 C7, FromISR 규칙과 `portASSERT_IF_INTERRUPT_PRIORITY_INVALID`는 E1, 타이머 데몬은 D6, `xTimerPendFunctionCallFromISR`는 D7.

---
## 8. 실무에서 다시 보기

### 8.1 드론 FC — IMU data-ready → rate loop, 센서 준비 비트

예를 들어 STM32H7 드론 비행 제어기라면 IMU(예: 8 kHz gyro)의 data-ready EXTI가 SPI DMA를 시작하고, DMA 완료 ISR이 rate loop 태스크를 깨운다. 받는 태스크가 하나이고 지연이 곧 제어 품질이므로 notification이 정답이다. 원인을 구분하려면 비트를 쓴다.

```c
#define EV_GYRO_DMA   (1u << 0)
#define EV_ACC_DMA    (1u << 1)
#define EV_SPI_ERR    (1u << 2)
#define IDX_SENSOR    1                       /* 인덱스 0은 stream buffer 몫으로 비워 둔다 (§1.6) */
void DMA2_Stream0_IRQHandler(void) {          /* gyro SPI RX DMA 완료 (H7 이름 예시) */
    BaseType_t w = pdFALSE;
    /* ... DMA 플래그 클리어 ... */
    xTaskNotifyIndexedFromISR(hRate, IDX_SENSOR, EV_GYRO_DMA, eSetBits, &w);
    portYIELD_FROM_ISR(w);
}
static void rate_loop(void *pv) {
    for (;;) {
        uint32_t ev;
        if (xTaskNotifyWaitIndexed(IDX_SENSOR, 0, 0xFFFFFFFFu, &ev, pdMS_TO_TICKS(2)) == pdFALSE) {
            /* 2 ms 동안 IMU가 안 옴 → 센서 고장 처리 (failsafe) */
            continue;
        }
        if (ev & EV_SPI_ERR) { /* 재초기화 */ }
        if (ev & EV_GYRO_DMA) { /* 자세 추정 + PID + 모터 출력 */ }
    }
}
```

(이 코드는 STM32 HAL 없이 구조만 보인 것이라 이 랩에서 빌드하지 않았다. 같은 API 조합은 예제 2와 예제 3에서 실행했다.) 반면 "gyro · accel · baro가 모두 새 샘플을 낸 뒤에 fusion을 한 번 돈다" 같은 저속 경로는 event group wait-all이 자연스럽다(예제 5). 이때 **비트는 횟수를 잃는다**는 점과, 센서 ISR이 직접 `xEventGroupSetBitsFromISR`를 부르면 데몬 경유 지연(예제 7, 35 us 이상)이 붙는다는 점을 기억한다. 깔끔한 구조는 ISR → 센서 드라이버 태스크(notification) → 드라이버 태스크가 `xEventGroupSetBits`(태스크 컨텍스트라 데몬 없음)다.

### 8.2 DMA half / full 완료 → 드라이버 태스크

UART나 ADC를 circular DMA로 받으면 half-transfer와 transfer-complete 인터럽트가 번갈아 온다(E6). 두 사건을 notification 비트 두 개로 보내고, 태스크가 처리 도중 둘이 다 서 있으면 "처리가 늦어 반 버퍼를 놓칠 뻔했다"는 overrun 신호로 쓸 수 있다. `xTaskNotifyAndQueryFromISR`로 이전 값을 받아 보면 ISR 안에서도 "아직 안 읽은 비트가 있었나"를 알 수 있다.

### 8.3 GNSS NMEA — stream buffer와 trigger level

예제 8이 그대로 실무 코드의 뼈대다. 실칩에서는 바이트마다 인터럽트 대신 **UART RX DMA + idle line 인터럽트**로 덩어리를 받고, ISR에서 `xStreamBufferSendFromISR(sb, dma_chunk, n, &w)`로 한 번에 넣는다. 그러면 trigger level은 "문장 하나쯤"(예: 32~64)으로 두고, idle 인터럽트(문장 사이 공백)에서 `xStreamBufferSendCompletedFromISR(sb, &w)`로 trigger 미만이어도 파서를 깨운다. 파서 태스크는 하나, ISR writer도 하나 — SWSR 조건이 자연스럽게 맞는다. 만약 같은 UART에 텔레메트리 송신 태스크 두 개가 쓰려 한다면(TX 방향), 그건 writer 둘이므로 큐나 뮤텍스 + stream buffer 조합으로 바꾼다(예제 10).

### 8.4 SSD 펌웨어와의 대응

Don의 SSD 컨트롤러 펌웨어에서 쓰던 구조를 RTOS 도구로 옮기면 이렇다.

| SSD FW (bare-metal) | RTOS 도구 | 주의 |
|---|---|---|
| NVMe SQ tail doorbell → 코어 깨움 | notification (`eSetValueWithOverwrite`로 최신 tail 전달) | 최신 값만 의미 있으므로 덮어쓰기가 맞다 |
| NAND 채널 완료 status 비트 | notification `eSetBits` (처리 태스크 1개) 또는 event group (여러 태스크) | 채널 8개 = 8비트, 24비트 한도 안 |
| 코어 간 메일박스 (FW 코어 ↔ 호스트 코어) | message buffer + `sbSEND_COMPLETED` → IPC 인터럽트 | 공유 SRAM의 캐시 일관성은 직접 (D8, E6) |
| 호스트 DMA 데이터 링 (head/tail) | stream buffer | writer · reader 각 1개일 때만 |
| "모든 die idle이면 저전력" 조건 | event group wait-all | 비트는 횟수 없음 — die별 카운트는 따로 |

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 센서 ISR에서 `xEventGroupSetBitsFromISR`를 기본으로 씀 | 제어 루프 지연이 수십 us 늘고, 중간 우선순위 태스크가 바쁘면 ms 단위로 튐 | 타이머 데몬 경유, 데몬 우선순위에 종속 (예제 7) | 받는 태스크가 하나면 `xTaskNotifyFromISR(eSetBits)`, 아니면 데몬 우선순위를 최상위로 |
| `xEventGroupSetBitsFromISR` 반환값 무시 | 가끔 이벤트가 사라짐 | 타이머 명령 큐(`configTIMER_QUEUE_LENGTH`)가 꽉 참 | 반환값 검사, 큐 길이 늘리기 |
| event bit로 개수를 세려 함 | 샘플 4개 중 1개만 처리 (예제 5 "gyro set 5 times → bit 1개") | 비트는 OR일 뿐 | `eIncrement` / counting semaphore / 큐 |
| wait-all 타임아웃 반환값을 `!= 0`으로 판정 | 일부 센서만 준비됐는데 fusion이 돎 | FreeRTOS는 타임아웃이어도 그 순간 비트를 반환 (0x3) | `(r & ALL) == ALL`로 검사 |
| 사용자 비트 24~31 사용 | `event_groups.c:563` assert, assert 끈 빌드에서는 대기자 제어 플래그 오염 | 위 8비트는 커널 제어 바이트 | 0~23만 쓰기, 더 필요하면 그룹 2개 |
| notification 인덱스 0을 앱이 쓰면서 같은 태스크가 stream buffer도 받음 | 버퍼 대기가 엉뚱하게 깨거나, 앱 알림 상태가 지워짐 | stream buffer 기본 인덱스 = 0 (`uxNotificationIndex`) | 앱은 인덱스 1+, 또는 `vStreamBufferSetStreamBufferNotificationIndex` |
| `xTaskNotifyWait` 진입 마스크로 매번 0xFFFFFFFF | 블록 직전에 들어온 비트가 사라짐 (예제 2 마지막 줄) | 상태가 이미 소비된 뒤 값만 남은 비트를 지움 | 진입 마스크 0, 퇴장 마스크로 처리한 비트만 지우기 |
| stream buffer에 writer 둘 (태스크 + ISR) | 조용한 데이터 손상, assert도 없음 (예제 10: 160개 깨짐) | `xHead` 갱신이 락 없음 (SWSR 가정) | writer 직렬화(크리티컬 섹션 + block 0) 또는 큐 |
| message buffer 수신 버퍼가 메시지보다 작음 | `Receive`가 계속 0, 버퍼가 꽉 차서 송신도 막힘 | 잘라 주지 않고 메시지를 남긴다 (예제 9) | 최대 메시지 크기 버퍼, `NextLengthBytes` 확인 |
| batching buffer의 꼬리를 타임아웃으로 꺼내려 함 | 마지막 조각이 영원히 안 나옴 | 타임아웃 뒤에도 "trigger 초과"만 읽기 허용 (예제 9) | 일반 stream buffer + 타임아웃, 또는 trigger 조정 |
| Zephyr `k_poll` 뒤 state를 안 되돌림 | 같은 이벤트를 또 처리, `k_fifo_get` NULL → HardFault | `state OR=`로 누적 (예제 12 B) | 매 루프 `state = K_POLL_STATE_NOT_READY` |
| Zephyr `k_poll_signal`로 여러 스레드 깨우기 | 가장 높은 우선순위 하나만 깸 (예제 12 C) | raise가 poll event 하나만 꺼냄 | `k_event` 브로드캐스트 |
| Zephyr mps2 보드에서 ms 타이밍이 10 ms 단위로 뭉개짐 | 2 ms 타이머가 10 ms에 울림 | 보드 기본 `CONFIG_SYS_CLOCK_TICKS_PER_SEC=100` | prj.conf에서 1000 (또는 필요한 값) |

---

## 10. 면접에서 이렇게 말한다

**Q.** What is a FreeRTOS task notification and why is it lighter than a binary semaphore?

**A.** 태스크마다 TCB 안에 32비트 값과 상태 바이트가 있고, 다른 태스크나 ISR이 핸들로 그 태스크에 직접 값을 갱신하며 깨운다. 중간 객체가 없으니 RAM이 0이고(semaphore는 heap 96 B), 깨울 때 이벤트 리스트를 거치지 않아 상태 리스트 하나만 옮긴다. 내가 V11.3.2에서 재 보니 깨움 + 전환 경로가 15~20% 짧았다. 문서의 45%는 2015년 V8.1.2 semaphore 기준이다.

> A notification is a 32-bit value plus a state byte inside each task's TCB. The sender writes it directly through the task handle, so there is no intermediate kernel object: zero extra RAM, and on unblock the kernel only moves the task from the delayed list to the ready list instead of going through a queue's event list. The docs say up to 45% faster, but that was measured against the V8.1.2 semaphore; on V11 in QEMU I measured about 15 to 20% on the wake-plus-switch path and no difference on the non-blocking path.

**꼬리 질문**: "So why not always use notifications?" → 받는 쪽이 정확히 태스크 하나일 때만 된다. 브로드캐스트, ISR 수신, 보내는 쪽 블록, 버퍼링이 없다.

**Q.** How do you use a notification as a counting semaphore versus a binary semaphore?

**A.** `xTaskNotifyGive`는 값을 +1 한다. 받는 쪽 `ulTaskNotifyTake(pdTRUE, …)`는 0으로 지우는 binary, `pdFALSE`는 1 감소하는 counting이다. 둘 다 반환값이 "그동안 몇 번 왔나"라서 binary로 써도 놓친 신호를 셀 수 있다.

> Give increments the value. Take with clear-on-exit set to pdTRUE behaves like a binary semaphore and resets it to zero; pdFALSE decrements by one, like a counting semaphore. In both cases the return value is the count before the take, which I use to detect when an ISR fired more than once before the task ran.

**Q.** Why can't you set event group bits directly from an ISR in FreeRTOS?

**A.** set은 대기 리스트 전체를 훑으며 조건을 검사하고 맞는 태스크를 전부 깨운다 — 대기자 수에 비례하는 O(n)이다(32개에 2145 cycle을 쟀다). FreeRTOS는 이 비결정적 작업을 인터럽트를 막은 채로 하지 않기로 했고, 태스크에서는 스케줄러 정지로 보호한다. ISR판은 `xTimerPendFunctionCallFromISR`로 타이머 데몬에 미루므로 `configUSE_TIMERS`가 필요하고, 지연이 늘고(직접 알림 202 vs 879 cycle), 데몬 우선순위가 낮으면 그만큼 밀린다.

> Setting bits walks the whole waiting list and may unblock any number of tasks, so its cost is linear in the number of waiters; I measured about 65 cycles per woken task. FreeRTOS refuses to do non-deterministic work with interrupts disabled, so from an ISR it defers the set to the timer daemon with xTimerPendFunctionCallFromISR. That needs configUSE_TIMERS, adds a context switch, can fail if the timer queue is full, and is only as fast as the daemon's priority allows.

**꼬리 질문**: "How does Zephyr do it?" → `k_event_post`는 ISR에서 직접 되고 스핀락 안에서 대기 큐를 훑는다. 데몬이 없는 대신 그 O(n)을 인터럽트 잠금 안에서 받아들인 설계다.

**Q.** What does the trigger level of a stream buffer control, and how would you choose it for a UART?

**A.** 쓴 뒤 바이트 수가 trigger 이상이면 블록된 reader를 깨운다. 작으면 지연이 짧지만 깨움이 많고, 크면 반대다. 115200 baud에서 trigger 1은 바이트마다(초당 1만 번) 깨우고 지연 17 us, 16이면 129번에 1.3 ms였다. NMEA는 문장 길이 근처로 두고 idle-line에서 `xStreamBufferSendCompletedFromISR`로 꼬리를 밀어 준다.

> The trigger level is checked on the send side: once the buffered byte count reaches it, the blocked reader is woken. It trades wake-ups against latency. For a 115200-baud GNSS link I measured 2048 wake-ups and 17 microseconds with a trigger of 1, versus 129 wake-ups and 1.3 milliseconds with 16. I'd set it near a sentence length and use the UART idle-line interrupt to force a wake for the tail.

**Q.** Stream buffers are described as single-writer single-reader. What actually breaks with two writers?

**A.** 쓰기 경로에 락이 없어서 두 writer가 같은 `xHead`에서 동시에 쓰면 데이터와 길이 접두가 섞인다. 태스크 + ISR 두 writer로 재현했더니 35085개 중 160개가 깨지고 수천 개가 사라졌는데 assert는 없었다. 블록하는 writer · reader가 둘이면 `xTaskWaitingToSend/Receive` 핸들 칸이 하나라 assert가 잡는다. 여러 writer면 직렬화하거나 큐를 쓴다.

> The write path updates the head index without a lock, because it assumes one writer. With a task and an ISR both writing, I reproduced silent corruption: 160 corrupted messages and thousands lost out of 35,000, with no assert. Only the blocking case is caught, since the buffer stores a single waiting-task handle per direction. For multiple writers you either serialize the sends in a critical section with zero block time, or use a queue.

**Q.** Event bits versus a semaphore — when does each fit?

**A.** 세마포어는 횟수(자원 수, 사건 수)를 세고 깨어나는 것은 하나다. event bit는 횟수 없이 "상태"이고 여러 태스크가 서로 다른 조합(any / all)을 기다릴 수 있다. 센서 세 개가 다 준비됐나는 bit, 샘플이 몇 개 쌓였나는 count다.

> A semaphore counts and wakes one waiter per give. Event bits are level-like state with no count, and several tasks can wait on different any or all combinations. "All three sensors ready" is a bit pattern; "how many samples are pending" is a count, and bits would silently lose that.

**Q.** In Zephyr, what is `k_poll` for and what are its pitfalls?

**A.** sem · fifo · msgq · pipe · signal 여러 개를 한 번에 기다리는 POSIX poll 같은 것이다. 가져가지는 않아서 반환 후 K_NO_WAIT로 직접 꺼내야 하고, state가 누적되므로 매번 NOT_READY로 되돌려야 하며, signal은 reset해야 한다. 같은 객체를 여러 스레드가 poll하면 가장 높은 우선순위 하나만 깬다.

> k_poll waits on several heterogeneous objects at once, like POSIX poll. It only reports readiness; you still take the semaphore or get the FIFO item with K_NO_WAIT and handle failure. The kernel ORs readiness into each event's state and never clears it, so you reset state to NOT_READY every loop and reset signals explicitly. And one raise or give wakes only the highest-priority poller, so for broadcast I'd use k_event.

**Q.** How would you pass "latest altitude" from a baro task to the control task with minimum overhead?

**A.** 최신 값만 의미 있으니 큐보다 덮어쓰기가 맞다. 받는 태스크가 하나면 `xTaskNotify(h, alt_mm, eSetValueWithOverwrite)` — 32비트 안에 들어가는 값이면 복사도 객체도 없다. 값이 크면 더블 버퍼 + 시퀀스 번호(D8) 위에 notification으로 깨운다.

> Only the newest value matters, so overwrite semantics fit better than a queue. If it fits in 32 bits, xTaskNotify with eSetValueWithOverwrite delivers it with no object and no copy. For a larger struct I'd use a double buffer with a sequence counter and send a notification just to wake the reader.

---

## 11. 직접 해보기

1. **개념**: 예제 2에서 `xTaskNotifyAndQuery(me, 0, eNoAction, &prev)`로 값을 읽으면 왜 다음 줄의 `eSetValueWithoutOverwrite`가 결과를 바꾸는가? 정답/힌트: `xTaskGenericNotify`는 eAction과 상관없이 상태를 `taskNOTIFICATION_RECEIVED`로 만든다. "읽기"가 "알림"이 되어 버린다.
2. **코드**: 예제 3에 MODE 3 — `xTaskNotify(hH, 1, eSetBits)` + `xTaskNotifyWait(0, 1, NULL, …)`을 추가해 notification을 이벤트 비트로 쓸 때의 비용을 재라. `ulTaskNotifyTake`와 차이가 있나? 정답/힌트: 같은 `xTaskGenericNotify`이지만 받는 쪽 `xTaskGenericNotifyWait`가 상태 기반이라 경로가 약간 다르다. 숫자는 직접 재서 확인한다.
3. **측정**: 예제 6을 고쳐 대기자를 "절반은 bit0, 절반은 bit1"로 두고 bit0만 세울 때 비용을 예측하고 재 보라. 정답/힌트: 훑기 8 × N + 깨움 65 × (N/2) 근처일 것이다. 선형성이 유지되는지 본다.
4. **측정**: 예제 8을 `xStreamBufferSetTriggerLevel`로 실행 중 바꾸는 "적응형 trigger"로 고쳐라 — 바이트가 몰리면 trigger를 올리고 한가하면 내린다. 평균 지연과 깨움 수를 고정 trigger 16과 비교하라. 정답/힌트: 바이트 간격이 일정한 이 실험에서는 이득이 없고, 버스트 트래픽(문장 사이 공백)을 흉내 내야 차이가 보인다.
5. **코드**: 예제 10의 (B)를 "writer 두 개가 꽉 찬 버퍼에 블록 send"로 바꿔 어느 assert가 걸리는지 확인하라. 정답/힌트: `xStreamBufferSend` 안의 `configASSERT( pxStreamBuffer->xTaskWaitingToSend == NULL )`("Should only be one writer").
6. **Zephyr**: 예제 12 (C)를 `k_event`로 바꿔(두 스레드가 `k_event_wait(&ev, BIT(0), false, …)`) post 한 번에 둘 다 깨는지, `_safe`로 바꾸면 어떻게 되는지 확인하라. 정답/힌트: 일반 wait는 둘 다 깬다. `_safe`는 깨어나는 스레드들의 맞은 비트를 모아 post 끝에 한 번 지우므로 같은 post에서 깨는 스레드는 모두 받는다 — `event_walk_op`의 `clear_events` 누적을 읽고 실행으로 확인한다.
7. **설계**: 드론 FC에서 IMU(8 kHz) · baro(50 Hz) · GNSS(10 Hz, UART) · RC 입력(SBUS, UART 100 kbaud) · 텔레메트리 TX를 태스크로 나누고, 각 경로에 쓸 신호 도구를 §6 표로 고른 뒤 이유를 한 줄씩 써라. 정답/힌트: IMU = notification, baro → fusion = event group(태스크에서 set), GNSS · SBUS = stream buffer(ISR 1 writer), 텔레메트리 TX = 여러 생산자이므로 큐.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| direct-to-task notification | 태스크 직접 알림 | TCB 안 32비트 값 + 상태로 객체 없이 신호, 수신자 1개 |
| `configTASK_NOTIFICATION_ARRAY_ENTRIES` | 알림 배열 크기 | 태스크당 독립 알림 개수, 기본 1, 인덱스당 TCB 약 4~5 B |
| `ucNotifyState` | 알림 상태 | NOT_WAITING / WAITING / RECEIVED (0/1/2) |
| eAction | 알림 동작 | NoAction, SetBits, Increment, SetValueWithOverwrite, SetValueWithoutOverwrite |
| clear on entry / exit | 진입 / 퇴장 마스크 | `xTaskNotifyWait`가 블록 전 / 반환 시 지울 비트 |
| event group | 이벤트 그룹 | 비트 집합 + 대기 리스트, wait-any / wait-all, 여러 대기자 |
| `eventEVENT_BITS_CONTROL_BYTES` | 제어 바이트 | 32비트 tick에서 0xff000000 — 사용자 비트는 24개 |
| rendezvous | 랑데부 | 여러 태스크가 한 지점에서 서로를 기다림 (`xEventGroupSync`) |
| timer daemon | 타이머 서비스 태스크 | 소프트웨어 타이머 · 지연 함수 호출을 실행하는 커널 태스크 (D6) |
| deferred call | 지연 호출 | ISR이 함수 실행을 데몬에 맡김 (`xTimerPendFunctionCallFromISR`, D7) |
| stream buffer | 스트림 버퍼 | SWSR 바이트 링 + trigger level, 깨움은 notification |
| message buffer | 메시지 버퍼 | stream buffer + 메시지마다 길이 접두 |
| stream batching buffer | 배칭 버퍼 | reader가 trigger 초과까지 블록하는 stream buffer (V11.1.0+) |
| trigger level | 트리거 레벨 | reader를 깨울 최소 바이트 수 (송신 쪽에서 검사) |
| SWSR / SPSC | 단일 생산자 · 단일 소비자 | writer 1개, reader 1개를 전제로 락 없이 짠 구조 |
| `sbSEND_COMPLETED` | 송신 완료 훅 | 기본은 reader에게 notification, 재정의하면 코어 간 인터럽트 |
| AMP | 비대칭 멀티프로세싱 | 코어마다 별도 RTOS, 공유 메모리 + 하드웨어 doorbell로 통신 |
| `k_event` | Zephyr 이벤트 객체 | 32비트, ISR에서 직접 post, 기본 브로드캐스트 |
| `k_event_wait_safe` | 안전한 대기 | 맞은 비트만 원자적으로 소비 (v4.4.2) |
| `k_poll` | Zephyr 다중 대기 | 여러 객체의 준비를 한 번에 기다림, 가져가지는 않음 |
| `k_poll_signal` | poll 신호 | `raise`로 올리고 `reset`으로 내리는 깃발 + result 값 |
| lost wakeup | 깨움 유실 | 검사와 블록 사이에 온 신호를 놓치는 경쟁 — 크리티컬 섹션으로 막는다 |

---

## 13. 요약 & 체크리스트

FreeRTOS task notification은 TCB 안의 32비트 값과 상태 바이트로 만든 "수신자 1명짜리 초인종"이다. eAction에 따라 binary / counting semaphore, 이벤트 비트, mailbox가 되고, 객체 RAM이 0이며 깨움 경로가 짧다 — 이 랩의 V11.3.2 측정으로는 전환 포함 경로에서 semaphore보다 11~22% 짧았고(문서의 45%는 2015년 semaphore 기준), 전환 없는 경로는 assert를 끄면 차이가 없었다. 한계는 수신자 1개, ISR 수신 불가, 송신 블록 불가, 버퍼링 없음이고, 인덱스 0은 stream buffer가 기본으로 쓴다. event group은 여러 태스크가 비트 조합을 기다리는 도구인데, 비트는 횟수를 세지 않고, 사용자 비트는 24개이며, set 비용이 대기자 수에 선형(대기자당 8~65 cycle)이라 ISR판은 타이머 데몬으로 미뤄져 지연이 4배 이상 늘고 데몬 우선순위에 묶인다. stream / message buffer는 SWSR 링 + trigger level로, trigger는 깨움 수와 지연을 맞바꾸는 손잡이이며(UART 115200에서 trigger 1 → 2048번 · 17 us, 16 → 129번 · 1.3 ms), writer가 둘이면 assert 없이 조용히 깨진다. Zephyr는 `k_event`(32비트, ISR 직접 post, 브로드캐스트, `_safe` 소비)와 `k_poll`(여러 객체 대기, 가져가지 않음, state 수동 재초기화, 깨움은 최고 우선순위 하나)로 같은 문제를 푼다.

- [ ] notification 필드 두 개(`ulNotifiedValue[]`, `ucNotifyState[]`)와 상태 세 개를 그리고, Give / Take / Wait가 각각 무엇을 바꾸는지 말할 수 있다
- [ ] eAction 다섯 가지와 `eSetValueWithoutOverwrite`가 상태로 실패하는 이유를 설명할 수 있다
- [ ] `xTaskNotifyWait`의 진입 마스크가 언제 적용되고 언제 비트를 잃게 하는지 안다
- [ ] notification이 semaphore보다 얼마나 빠른지 내 측정 숫자와 45% 주장의 출처로 정직하게 말할 수 있다
- [ ] notification의 한계 4가지와 인덱스 0 · stream buffer 충돌을 안다
- [ ] event group wait-any / wait-all / clear-on-exit / Sync를 쓰고, 24비트 한계와 "비트는 횟수를 세지 않는다"를 안다
- [ ] `xEventGroupSetBitsFromISR`가 데몬으로 미뤄지는 이유(O(n))와 그 비용, `configUSE_TIMERS` 의존을 숫자로 말할 수 있다
- [ ] stream buffer의 trigger level이 송신 쪽에서 검사된다는 것과, UART에 맞게 trigger를 고르는 기준을 안다
- [ ] message buffer의 길이 접두 크기, 작은 수신 버퍼의 동작, batching buffer의 꼬리 함정을 안다
- [ ] SWSR 가정을 깨면 무엇이 assert로 잡히고 무엇이 조용히 깨지는지 안다
- [ ] Zephyr `k_event`의 post / set / `_safe` / reset 차이와 ISR 직접 post의 트레이드오프를 안다
- [ ] Zephyr `k_poll`의 세 함정(가져가지 않음, state 누적, poller 하나만 깸)을 피해 코드를 쓸 수 있다

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스(랩 `/var/rtoslab/src/FreeRTOS-Kernel`): `tasks.c`(`xTaskGenericNotify`, `ulTaskGenericNotifyTake`, `xTaskGenericNotifyWait`, `vTaskGenericNotifyGiveFromISR`), `include/task.h`(notification API 매크로, `tskDEFAULT_INDEX_TO_NOTIFY`), `event_groups.c` · `include/event_groups.h`(제어 바이트, FromISR 주석), `stream_buffer.c` · `include/stream_buffer.h` · `include/message_buffer.h`, `History.txt`(V11.1.0 batching buffer · notification index, V11.2.0 notify 최적화, V11.3.2 batching 수정, V10.5.0 per-instance callback)
- FreeRTOS 문서 — RTOS task notifications: [freertos.org/RTOS-task-notifications.html](https://FreeRTOS.org/RTOS-task-notifications.html) (45% / 35% 각주, 한계 목록)
- FreeRTOS 문서 — Task notification as binary semaphore: [freertos.org/RTOS_Task_Notification_As_Binary_Semaphore.html](https://FreeRTOS.org/RTOS_Task_Notification_As_Binary_Semaphore.html)
- FreeRTOS 공식 문서 페이지(새 주소): [Direct to task notifications](https://en.freertos.org/Documentation/02-Kernel/02-Kernel-features/03-Direct-to-task-notifications/01-Task-notifications)
- FreeRTOS 블로그 — Decrease RAM footprint and accelerate execution with FreeRTOS notifications (2020): [freertos.org/2020/09/...](https://freeRTOS.org/2020/09/decrease-ram-footprint-and-accelerate-execution-with-freertos-notifications.html)
- FreeRTOS 포럼 보관 — Speed Improvements in 8.2.0 (2015): [freertos.org 포럼 보관](https://www.freertos.org/FreeRTOS_Support_Forum_Archive/March_2015/freertos_Speed_Improvements_in_8.2.0_2e07b6b4j.html)
- Embedded Artistry — FreeRTOS Task Notifications: A Lightweight Method for Waking Threads (2018): [embeddedartistry.com](https://embeddedartistry.com/blog/2018/05/28/freertos-task-notifications-a-lightweight-method-for-waking-threads/)
- Zephyr v4.4.2 소스(랩 `/var/rtoslab/zephyrproject/zephyr`): `kernel/events.c`(`k_event_post_internal`, `event_walk_op`, `k_event_wait_internal`), `kernel/poll.c`(`z_impl_k_poll`, `register_events`, `add_event`, `z_impl_k_poll_signal_raise`), `include/zephyr/kernel.h`(`struct k_event`, `struct k_poll_event`, `K_POLL_TYPE_*`), `kernel/Kconfig`(`CONFIG_EVENTS`, `CONFIG_POLL`)
- Zephyr 문서 — Kernel Services: Events, Polling API (docs.zephyrproject.org의 kernel services 절)
- 이 노트와 이어지는 노트: A5(API 대응), C1(리스트), C2(전환), C7(크리티컬 섹션), D1(세마포어), D4(큐 · 큐 셋), D6(타이머 데몬), D7(지연 호출), D8(SPSC 링), D10(Zephyr IPC), E1(FromISR 규칙), E6(DMA + idle line)
