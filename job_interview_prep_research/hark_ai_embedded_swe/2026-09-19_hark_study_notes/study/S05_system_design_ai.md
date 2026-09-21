# S05. 시스템 설계 — Always-on 음성 AI 웨어러블을 45분에 설계하기

> **목표**: "MCU가 마이크 스트림에서 VAD/wake word를 돌리고 → application SoC를 깨우고 → on-device 또는 클라우드 모델로 넘기는" 기기를 면접관 앞에서 45분 안에 구조적으로 설계한다. 요구사항 확인, 블록도, 전원 상태, 데이터 흐름·버퍼, 메모리·레이턴시 예산, MCU↔SoC IPC, 실패 모드, 관측성, OTA, factory까지 한 흐름으로 말한다. 그리고 on-device AI 팀과 협업할 때 나오는 질문 12개를 연습한다.
> **선행**: C03(I2S/PDM, DMA), C04(RTOS), C05(저전력), C08(on-device ML), S04(OTA)
> **사용법**: 질문을 먼저 소리 내어 답해 보고, 그다음 모범 답안을 읽는다. 1~10절은 면접 대화 형식이다. "면접관" 문단을 읽고, 멈추고, 스스로 말한 뒤 "Don" 문단과 비교한다.

---

## 0. 45분을 어떻게 쓰나

시스템 설계 면접에서 떨어지는 가장 흔한 이유는 **기술을 몰라서가 아니라 순서가 없어서**다. 바로 칩 이름부터 말하거나, 한 부분(예: wake word 모델)에 20분을 쓰고 전원·실패 모드를 못 다룬다. 그래서 시간표를 먼저 머리에 둔다.

```
 분     단계                         화이트보드에 남길 것
 --------------------------------------------------------------------------
 0-5    요구사항·가정 확인            가정 목록(숫자 포함), 성공 기준
 5-12   블록도                        칩, 버스, 전원 도메인
 12-18  전원 상태 머신                 상태도 + 상태별 전류 목표
 18-26  데이터 흐름·버퍼링             mic -> DMA -> ring -> 모델 -> IPC 파이프라인
 26-32  메모리·레이턴시 예산            두 개의 표
 32-37  IPC · wake 핸드셰이크           타이밍도
 37-42  실패 모드 · 관측성 · OTA · factory  표 하나
 42-45  trade-off 요약 + 다음 단계       "if I had more time..."
```

말하는 규칙 세 가지:

1. 매 단계 시작에 "Next I'll cover X" 라고 선언한다. 면접관이 방향을 바꾸고 싶으면 그때 말한다.
2. 숫자는 가정이라고 밝히고 쓴다. "Let me assume 16 kHz mono for the wake word path." 숫자가 없으면 예산 논의가 안 된다.
3. 결정마다 대안 하나와 이유를 붙인다. "I'd use SPI rather than UART here because..." 이것이 시니어 신호다.

> 이 기기 구조(SoC + always-on MCU, Hexagon DSP, Ambiq급 MCU)는 Hark 채용 공고에서 나온 **추정**이다(context 2.2). 면접에서는 "I don't know your actual architecture, so I'll assume a common split" 이라고 말하고 시작한다.

---

## 1. 요구사항 확인 (0~5분)

**면접관**: "Design the firmware architecture for an always-on voice AI wearable. The user says a wake word, and the device answers using an AI model."

**Don**: 바로 그리지 않고 질문한다. 아래 질문 중 5~6개를 고른다.

| 질문 | 왜 중요한가 | 답이 없을 때 쓸 가정 |
|---|---|---|
| 배터리 용량과 목표 사용 시간은? | always-on 평균 전류 예산이 여기서 나온다 | 300 mAh, 대기 위주로 하루 이상 |
| wake word 하나인가, 사용자 지정인가? | 모델 크기·학습 방식이 다르다 | 고정 wake word 1개 |
| 마이크 개수는? beamforming 필요? | always-on 경로 전류, 데이터량 | 마이크 2개, wake 경로는 1개만 사용 |
| 응답 경로는 클라우드인가 on-device인가? | SoC 메모리·연결 요구 | 둘 다: 짧은 명령은 on-device, 나머지는 클라우드 |
| 목표 레이턴시는? (wake → 첫 응답) | SoC resume, 네트워크 경로 설계 | wake 감지까지 0.5초 이내, 첫 오디오 응답 1초대 목표 |
| wake word 전 오디오도 보내야 하나? | pre-roll 버퍼 크기 | 1.5초 pre-roll |
| 프라이버시 요구? | 오디오를 언제 SoC/클라우드로 보내도 되나 | wake 전 오디오는 MCU 밖으로 안 나감 |
| 오탐(false accept) 허용치는? | 오탐 1번 = SoC wake 1번 = 전력 | 하루 수 회 이하 |

**Don (영어로 이렇게 시작)**: "Before drawing anything, let me pin down a few requirements. What's the battery size and the standby target? Is the wake word fixed? Should audio before the wake word be sent along? And is the response path on-device, cloud, or both? If you don't have numbers, I'll assume a 300 mAh battery, one fixed wake word, a 1.5 second pre-roll, and a hybrid path, and I'll write those assumptions here so we can revisit them."

**성공 기준을 한 줄로 적는다**: "Always-on listening under a tight average current budget, wake detection within about half a second, no audio leaves the MCU before the wake word, and the system recovers by itself from any single chip failure."

---

## 2. 블록도 (5~12분)

**면접관**: "OK, draw the system."

**Don**:

```
                                      +---------------------------+
                          PMIC  ----> | rails: AON(MCU, mic), SoC,|
                          fuel gauge  | radio, DRAM               |
                                      +---------------------------+

 [PDM mic 0] --CLK/DATA--+
                         v
 [PDM mic 1] ------> +-----------------------------+   wake GPIO (MCU->SoC, SoC 쪽 wake 가능 IRQ)
                     | Always-on MCU (Cortex-M급)   |------------------------------+
                     |  PDM->PCM(HW decimation)     |   SPI (SoC master) + DRDY    |
                     |  DMA ping-pong               |<============================>|
                     |  VAD -> wake word (INT8)     |   MCU reset / SoC reset 선  |
                     |  pre-roll ring buffer        |                              v
                     |  RTOS (FreeRTOS or Zephyr)   |          +----------------------------------+
                     |  IMU, button, haptic, LED    |          | Application SoC (Cortex-A, Linux/ |
                     +-----------------------------+          | Android), DSP(예: Hexagon급)       |
                                                               |  2nd-stage wake verifier          |
                                                               |  ASR / on-device LLM / TTS        |
                                                               |  OTA orchestrator, app            |
                                                               +------+-------------------+-------+
                                                                      | SDIO/PCIe          | modem
                                                                      v                    v
                                                               [Wi-Fi/BT combo]      [Cellular]
                                                                      \                    /
                                                                       +---> Cloud model <+
```

**설명(말로)**: "The key idea is a split by power. The MCU and the microphones are the only things on during idle. The MCU does cheap filtering in stages, voice activity first and the wake word model only when there's speech. Only a confident detection wakes the SoC, which is expensive to wake, and the SoC runs a bigger second-stage verifier before streaming to the assistant model."

**대안을 하나 말한다**: "An alternative is to do the wake word on the SoC's low-power DSP island, which some application SoCs support. That removes one chip and the IPC, but the always-on floor depends on the SoC's island power, and we'd lose the ability to keep sensors and haptics alive when the SoC is fully off. I'd pick based on measured current of both options."

---

## 3. 전원 상태 머신 (12~18분)

**면접관**: "Walk me through the power states."

**Don**:

```
            +------------------+
            | SHIP / OFF       |  배송 모드: MCU만 최저 전력, 버튼 wake
            +--------+---------+
                     | button / charger
                     v
   no speech  +------------------+  VAD speech onset
  +---------->| IDLE_LISTEN      |-------------------+
  |           | MCU: VAD only    |                   v
  |           | SoC: off/suspend |         +------------------+
  |           +------------------+         | WAKE_CANDIDATE   |
  |                    ^                   | MCU: KWS model   |
  |      timeout/      |  score < th       | SoC: suspend     |
  |      reject        +-------------------+--------+---------+
  |                                                 | score >= th
  |                                                 v
  |           +------------------+  SoC ack  +------------------+
  |           | SESSION_ACTIVE   |<----------| WAKING_SOC       |
  |           | MCU: audio stream|           | MCU: wake GPIO,  |
  |           | SoC: ASR/LLM/TTS |           | pre-roll hold    |
  |           | radio: up        |           +------------------+
  |           +--------+---------+                 | ack timeout
  |                    | session end / reject      v
  +--------------------+                     (retry, then log + back to IDLE)
```

| 상태 | MCU | 마이크 | SoC | 라디오 | 전류 목표(가정) |
|---|---|---|---|---|---|
| SHIP | deep sleep, RTC | off | off | off | 최소 |
| IDLE_LISTEN | 대부분 sleep, DMA로 오디오 수집, 프레임마다 짧게 VAD | 1개 on(저전력 모드) | suspend 또는 off | BLE만 느린 주기 또는 off | 수백 µA대 이하 목표 |
| WAKE_CANDIDATE | KWS 추론 주기적 실행 | 1개 | suspend | 동일 | 짧은 구간만 높음 |
| WAKING_SOC | 버퍼 유지, wake 신호 | on | resume 중 | resume | 과도 구간 |
| SESSION_ACTIVE | 스트리밍, 센서 | 2개(beamforming) | active | Wi-Fi/셀룰러 active | 수십~수백 mA(SoC 지배) |

**핵심 포인트(말할 것)**:

1. 평균 전류는 IDLE_LISTEN이 지배한다. 그래서 VAD는 "프레임당 몇 µs" 수준으로 가볍게, KWS는 VAD가 말소리를 감지했을 때만 돈다(cascade).
2. 오탐은 전력 문제다. SoC wake 한 번의 에너지 x 하루 오탐 횟수를 계산해 오탐 목표를 정한다.
3. 마이크도 전력을 먹는다. 많은 디지털 마이크는 PDM 클럭 주파수에 따라 저전력 모드가 있다(부품마다 다름). always-on은 마이크 1개, 세션 중에만 2개.

**배터리 계산을 즉석에서 보여준다(가정 숫자)**:

```
 가정: 300 mAh, IDLE_LISTEN 평균 0.5 mA, 하루 SESSION 총 10분 @ 150 mA,
       오탐 wake 하루 5회 x (SoC wake 2초 @ 150 mA)

 idle    : 0.5 mA x 24 h                    = 12.0 mAh/day
 session : 150 mA x (10/60) h               = 25.0 mAh/day
 false   : 150 mA x (5 x 2 s / 3600) h      =  0.42 mAh/day
 합계                                        ~ 37.4 mAh/day  -> 약 8일(대기 위주 사용)
```

"여기서 보이는 건 세션 사용량이 가장 크다는 것, 그리고 idle 0.5 mA가 1 mA가 되면 하루 12 mAh가 더 든다는 것이다. 그래서 idle floor를 측정 가능한 목표로 잡고 매 빌드마다 전류를 회귀 테스트한다."

---

## 4. 데이터 흐름과 버퍼링 (18~26분)

**면접관**: "How does audio get from the microphone to the model?"

**Don**:

```
 PDM mic (1-bit, MHz급 클럭)
   |  PDM 인터페이스 HW가 decimation filter로 PCM 변환 (16 kHz, 16-bit)
   v
 DMA ping-pong: [buf A 10 ms][buf B 10 ms]   <- DMA가 A 채우는 동안 CPU는 B 처리
   |  half/full-complete IRQ -> task notify (ISR은 포인터만 넘김)
   v
 audio task (높은 우선순위)
   |-- pre-roll ring buffer (1.5 s = 48 KB) 에 항상 복사
   |-- VAD (에너지 + 간단한 특징)  -> speech면 KWS task 깨움
   v
 feature task: 30 ms window / 10~20 ms hop, log-mel 40 bin (예시 설정)
   v
 KWS inference (TFLM + CMSIS-NN, INT8) -> posterior 평활화 -> threshold
   v
 detect -> wake SoC -> ring buffer의 pre-roll + 실시간 오디오를 IPC로 스트림
```

**데이터량 계산**:

```
 16,000 samples/s x 2 bytes x 1 ch = 32,000 B/s = 32 KB/s (1 KB = 1000 B로 계산)
 10 ms DMA 블록                     = 160 samples = 320 B
 pre-roll 1.5 s                     = 48 KB
 2채널 세션 스트림                   = 64 KB/s
```

**왜 이 구조인가(말할 것)**:

1. **ISR은 짧게**: DMA 완료 ISR은 "어느 버퍼가 찼다"만 task에 알린다(FreeRTOS라면 `vTaskNotifyGiveFromISR()` 또는 `xQueueSendFromISR()`, Zephyr라면 `k_sem_give()`나 `k_msgq_put()`을 ISR에서 non-blocking으로). 처리 시간이 10 ms를 넘으면 ping-pong이 덮어써지므로 overrun 카운터를 둔다.
2. **pre-roll은 항상 채운다**: wake word는 끝난 뒤에야 감지되므로, 그 앞의 오디오를 SoC가 재검증하고 ASR이 문장 시작을 놓치지 않게 하려면 과거 오디오가 필요하다.
3. **우선순위**: audio capture task > KWS task > 기타(센서, BLE). KWS 추론이 길어도 캡처는 절대 밀리면 안 된다.

**Don 경험 연결**: "SSD FW에서 호스트 데이터 경로를 DMA와 링버퍼로 다뤘다. 오디오도 같은 producer-consumer 문제다. 차이는 데이터가 멈추지 않는다는 것, 즉 backpressure를 걸 수 없어서 overrun을 감지하고 세는 것이 중요하다."

```c
/* 오디오 DMA half/full 콜백 -> task 알림 (FreeRTOS 예시) */
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#define FRAME_SAMPLES 160u                   /* 10 ms @ 16 kHz */
static int16_t dma_buf[2][FRAME_SAMPLES];    /* ping-pong: DMA는 circular 모드로 두 버퍼를 연속 채움 */
static volatile uint32_t ready_idx;          /* 방금 찬 버퍼 번호 (ISR만 씀) */
static uint32_t overrun_count;               /* task만 씀 */
static TaskHandle_t audio_task_handle;

extern void process_block(const int16_t *pcm, uint32_t n);  /* ring 복사 + VAD */

/* 벤더 HAL의 half-complete / full-complete 콜백에서 호출한다고 가정 */
void audio_dma_block_done_isr(uint32_t idx)
{
    BaseType_t woken = pdFALSE;
    ready_idx = idx;
    vTaskNotifyGiveFromISR(audio_task_handle, &woken);   /* notification 값 +1 */
    portYIELD_FROM_ISR(woken);
}

void audio_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* pdTRUE: 값을 0으로 지우고, 지우기 전 값(= 그동안 찬 블록 수)을 반환 */
        uint32_t n = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (n > 1u) {
            overrun_count += n - 1u;         /* 처리 못 하고 덮어써진 블록 수 */
        }
        process_block(dma_buf[ready_idx], FRAME_SAMPLES);
    }
}
```

`ulTaskNotifyTake(pdTRUE, ...)`는 notification 값을 0으로 만들면서 **지우기 전 값**을 반환한다. ISR이 블록마다 한 번씩 give하므로, 반환값이 2 이상이면 task가 그 사이 블록을 놓쳤다는 뜻이다. 이렇게 하면 ISR 쪽에 비ISR API를 쓰지 않고도 overrun을 셀 수 있다. (ISR에서는 반드시 `FromISR` 계열 API만 쓴다.)

---

## 5. 메모리·레이턴시 예산 (26~32분)

**면접관**: "Give me a memory budget for the MCU."

**Don**: "MCU SRAM이 512 KB라고 가정하겠다. 실제 부품에 따라 바꾼다."

| 항목 | 크기(가정) | 근거 |
|---|---|---|
| DMA ping-pong | 1 KB 미만 | 2 x 320 B (+ 2채널이면 2배) |
| pre-roll ring | 48 KB | 1.5 s x 32 KB/s |
| feature buffer | 약 8 KB | 49 frame x 40 bin x int8 = 1,960 B + 작업 버퍼 |
| TFLM tensor arena | 30~100 KB | 모델에 따라, `arena_used_bytes()`로 실측 |
| 모델 가중치 | flash(XIP) | 수십~수백 KB, SRAM에 두면 빠르지만 비쌈 |
| IPC TX 버퍼 | 16 KB | SoC 응답 지연 흡수, 약 0.25 s @ 64 KB/s |
| RTOS task stacks | 약 24 KB | task 8개 x 평균 3 KB, 워터마크로 조정 |
| BLE 스택 / 센서 / 로그 | 64 KB 이상 | 스택 벤더 요구에 따름 |
| crash dump 영역(noinit) | 4 KB | 리셋 후에도 유지 |
| 여유 | 20% 이상 | 기능 추가·모델 교체 대비 |

**면접관**: "And latency?"

**Don**: "wake word가 끝난 시점부터 사용자가 '듣고 있다'는 피드백(소리/햅틱)을 받을 때까지를 쪼갠다."

```
 wake word 발화 끝
   |-- 0 ~ 20 ms    : 마지막 DMA 블록 도착 (블록 크기 10 ms + 여유)
   |-- ~ 50 ms      : feature 계산 + KWS 추론 (hop마다 실행, 추론 수 ms 가정)
   |-- ~ 150 ms     : posterior 평활화 창(오탐 억제용 연속 N 프레임)
   |   ==> MCU 감지 : 약 200 ms   -> 즉시 햅틱/LED 피드백(MCU 단독 가능!)
   |-- + SoC resume : 수백 ms (SoC와 OS 설정에 따라 매우 다름 - 측정 대상)
   |-- + 2단계 검증, 스트림 시작
   |-- + 네트워크 RTT + 모델 첫 토큰 + TTS 첫 오디오
   v
 첫 음성 응답
```

**핵심(말할 것)**: "사용자가 느끼는 반응성은 MCU만으로 줄 수 있다. 감지 즉시 MCU가 햅틱이나 짧은 소리를 내면 SoC resume 지연이 가려진다. 측정은 각 경계에 GPIO를 토글해 로직 분석기로 한 번에 본다. 그러면 칩 경계를 넘는 레이턴시도 한 타임라인에 나온다."

---

## 6. MCU ↔ SoC IPC (32~37분)

**면접관**: "How do the MCU and SoC talk?"

**Don**:

| 옵션 | 장점 | 단점 | 판단 |
|---|---|---|---|
| UART | 단순, 핀 2개(+흐름제어 2) | 속도 제한(보통 수 Mbps), 오디오 + 제어 동시 시 여유 적음 | 제어 전용이면 충분 |
| SPI (SoC master, MCU slave) + DRDY GPIO | 수~수십 MHz, DMA 친화적 | slave 쪽은 master 클럭에 맞춰 TX 준비 필요, 프로토콜 직접 설계 | **선택** |
| I2C | 핀 적음 | 느림(보통 400 kHz~1 MHz급) | 오디오에는 부적합 |
| USB | 빠름, 표준 | 전력·복잡도, always-on에 과함 | 제외 |

"SPI를 쓰고 MCU가 보낼 데이터가 있으면 DRDY(data ready) GPIO를 올린다. 별도로 wake GPIO는 SoC의 wake 가능한 인터럽트 핀에 연결한다. pre-roll 48 KB를 10 MHz SPI로 보내면 약 40 ms(48,000 x 8 / 10^7 ≈ 38 ms, 오버헤드 제외)라서 충분하다."

**프레임 형식**:

```c
/* MCU <-> SoC IPC 프레임 (예시 설계) */
#include <stdint.h>

#define IPC_SYNC 0xA55Au

typedef enum {
    IPC_MSG_HELLO       = 0x01,   /* 부팅 핸드셰이크: 버전 교환 */
    IPC_MSG_WAKE_EVENT  = 0x02,   /* KWS 감지: score, 타임스탬프 */
    IPC_MSG_AUDIO       = 0x03,   /* PCM 청크 */
    IPC_MSG_ACK         = 0x04,
    IPC_MSG_CMD         = 0x05,   /* SoC->MCU: 세션 종료, 모드 변경, OTA 청크 등 */
    IPC_MSG_HEARTBEAT   = 0x06,
    IPC_MSG_LOG         = 0x07
} ipc_msg_type_t;

typedef struct __attribute__((packed)) {
    uint16_t sync;        /* IPC_SYNC: 재동기화 기준점 */
    uint8_t  proto_ver;   /* N / N-1 호환 판단 (S04 Q15) */
    uint8_t  type;        /* ipc_msg_type_t */
    uint16_t seq;         /* 누락·중복 검출 */
    uint16_t len;         /* payload 길이 */
    uint32_t ts_us;       /* MCU 타임스탬프: 칩 간 레이턴시 측정용 */
    /* payload[len] 다음에 CRC-16 */
} ipc_hdr_t;
```

**wake 핸드셰이크 타이밍**:

```
 MCU  wake GPIO  ___/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\____________________
 SoC  resume     ......[kernel resume][driver probe]
 SoC  SPI                                 HELLO ->   <- ACK
 MCU  DRDY       ____________________________/‾‾\_/‾‾\_/‾‾\___ (pre-roll + live 청크)
 MCU  timer      |---------- ack timeout (예: 1 s) ---------|
                 타임아웃이면: wake 재시도 1회 -> 실패 시 로그, IDLE로 복귀, 카운터 증가
```

**설계 포인트(말할 것)**:

1. **버전 핸드셰이크**: 부팅 때 HELLO로 proto_ver와 FW 버전을 교환한다. OTA 중 버전이 섞여도(N/N-1) 동작해야 한다.
2. **흐름 제어**: MCU TX 버퍼가 가득 차면 가장 오래된 오디오를 버리고 `drop` 카운터를 올린다. 제어 메시지는 별도 큐로 우선 전송한다.
3. **재동기화**: CRC 실패나 sync 불일치 시 다음 sync 워드까지 스킵. seq로 누락을 센다.
4. **heartbeat 양방향**: SoC가 MCU heartbeat를 못 받으면 MCU reset 선을 토글한다. MCU는 SoC가 응답 없으면 wake를 반복하지 않고 백오프한다(배터리 보호).

**Don 경험 연결**: "Apple에서 새 무선 칩과 호스트 사이 PCIe/I2C/SPMI 인터페이스 장애를 root cause 했다. 칩 간 링크는 반드시 버전, 시퀀스, CRC, 타임아웃, 리셋 경로가 있어야 필드에서 디버깅이 된다."

---

## 7. 실패 모드 (37~40분)

**면접관**: "What can go wrong?"

| 실패 | 탐지 | 대응 | 예방 |
|---|---|---|---|
| SoC가 wake에 응답 없음 | ack 타임아웃 | 재시도 1회 → IDLE 복귀, 카운터 | resume 경로 테스트, wake 핀 풀업/극성 확인 |
| 오디오 overrun | DMA 콜백의 overrun 카운터 | 해당 프레임 버림, 로그 | 캡처 task 최고 우선순위, 추론 분할 |
| IPC 비동기화 | CRC/seq 오류 | sync 재탐색, 세션 재시작 | 신호 무결성 확인(SPI 클럭 속도 margin) |
| MCU HardFault/hang | 워치독, SoC heartbeat | 리셋 + crash dump 보존 | fault handler가 레지스터를 noinit RAM에 저장 |
| 오탐 폭주(TV 소리 등) | 시간당 wake 횟수 | rate limit, 임계값 일시 상향 | 2단계 검증, 필드 통계로 임계 튜닝 |
| 미탐(사용자 불만) | 세션 중 사용자 재시도 패턴(추정 지표) | 모델/임계 업데이트 | factory 마이크 cal, 모델 OTA |
| 낮은 배터리 | fuel gauge | 세션 제한, 클라우드 경로만 등 degrade | 상태별 전류 예산 |
| 과열 | 온도 센서 | 모델 크기/주기 낮춤, 세션 제한 | 열 시험(DVT) |
| 모델과 feature 버전 불일치 | 부팅 시 manifest 확인 | 이전 모델로 fallback | 코드·모델 호환 표 |

---

## 8. 관측성 (40~41분)

"필드에서 JTAG은 없다. 그래서 처음부터 계측을 설계한다."

1. **카운터**: VAD 트리거 수, KWS 실행 수, wake 수, SoC ack 타임아웃, IPC CRC 오류, 오디오 overrun, drop, 워치독 리셋, 리셋 원인.
2. **히스토그램**: KWS 추론 시간(DWT cycle), 감지 → SoC ack 레이턴시(`ts_us` 기반), 세션 길이.
3. **crash dump**: fault 레지스터(CFSR, HFSR, MMFAR, BFAR), stacked PC/LR, 최근 로그 링. 다음 부팅에 SoC가 수거해 업로드.
4. **전력 텔레메트리**: fuel gauge 기반 상태별 소모 추정, 상태 체류 시간.
5. **프라이버시**: 오디오는 절대 텔레메트리에 넣지 않는다. 점수와 카운터만.

**Don 경험 연결**: SK hynix NVMe telemetry 디버그 기능, Solidigm error reporting 설계 → "무엇을 기록할지, 저장·성능 비용을 어떻게 제한할지 이미 해봤다 <구체 사례 채우기>".

---

## 9. OTA와 Factory (41~42분)

**OTA(짧게)**: S04 6절 구조를 요약한다. "SoC가 오케스트레이터, MCU는 MCUboot로 자기 이미지를 검증, 모델은 코드와 별도 이미지로 업데이트, IPC는 N/N-1 호환, 전체가 healthy일 때 SoC가 마지막에 confirm."

**모델 OTA 특이사항**: 모델만 바뀌어도 오탐/미탐이 바뀌므로 단계적 롤아웃 게이트 지표에 "시간당 wake 수"와 "세션 중 재시도 비율(추정 지표)"을 넣는다.

**Factory**: 마이크 감도 cal(기준 톤, 채널별 gain), 마이크 막힘/누설 검사(음향 포트), wake word 경로 end-to-end 테스트(스피커로 녹음된 wake word 재생 → 감지 확인), IPC 루프백, 전류 측정(IDLE_LISTEN 전류가 한계 내인지 — 누설 불량 검출), 마지막에 디버그 잠금 확인.

---

## 10. 마무리 (42~45분)

**Don (영어)**: "To wrap up: the design separates an always-on MCU domain from an expensive SoC domain, uses a VAD-then-KWS cascade so the average current is dominated by a very cheap stage, keeps a pre-roll ring buffer so nothing is lost across the wake, and uses a versioned SPI protocol with sequence numbers, CRC and heartbeats so failures are detectable. The main trade-offs are the extra chip and IPC complexity versus using an SoC DSP island, and false-accept rate versus power. If I had more time, I'd go deeper into the second-stage verifier on the SoC and how we tune thresholds from fleet statistics without collecting audio."

**면접관이 흔히 이어서 파는 곳**:

| 꼬리질문 | 짧은 답 |
|---|---|
| "Why not always stream to the SoC?" | SoC active 전류가 MCU보다 수십~수백 배. 대기 시간 목표 불가 |
| "How do you pick the KWS threshold?" | ML 팀의 FAR/FRR 곡선 + 전력 모델(오탐 1회 에너지)로 합의. 필드 통계로 조정 |
| "What if the wake word is said during a session?" | 세션 중에는 SoC가 오디오를 받으므로 MCU KWS를 끄거나 barge-in 이벤트로만 사용 |
| "Echo — the device hears its own TTS?" | 세션 중 AEC(acoustic echo cancellation)는 SoC/DSP에서. 스피커 출력 레퍼런스 필요 |
| "How do you test the whole pipeline?" | 녹음된 오디오를 재생하는 음향 rig + GPIO 타임스탬프 + 전류 측정을 CI에 |

---

## 11. On-device AI 협업 문항

JD: "Collaborate with the on-device AI team to support model inference within memory and latency budgets." 면접관이 AI 팀 사람일 때 나오는 질문들이다.

### Q01. The ML team hands you a keyword-spotting model that needs a 600 KB tensor arena, but you only have 350 KB free on the MCU. What do you do?

**왜 묻나**: 예산 협상과 기술 옵션을 둘 다 아는지.

**30초 답변**: 먼저 숫자를 검증한다. 실제 기기에서 `AllocateTensors()` 후 `arena_used_bytes()`로 실측하고, 어느 레이어의 활성값이 피크를 만드는지 본다. 그다음 FW 쪽 옵션: arena를 다른 버퍼와 시간적으로 겹쳐 쓰기(추론 중 안 쓰는 버퍼), 가중치를 flash XIP로 유지, 스택·BLE 버퍼 재검토. ML 쪽에 요청할 옵션: 입력 길이/채널 축소, 피크 레이어 구조 변경, INT8 양자화 확인(float가 섞였는지), depthwise-separable 구조. 결론은 "측정 기반으로 양쪽이 줄일 수 있는 것"을 표로 만들어 합의한다.

**English answer**: First I'd measure it on the target: allocate tensors and read arena_used_bytes, and find which layer creates the peak activation memory. On the firmware side I can overlap the arena with buffers that aren't live during inference and make sure weights stay in flash. On the model side I'd ask whether everything is really int8, whether the input window can be shorter, or whether the peak layer can be restructured. Then we agree on a budget table with numbers from both sides instead of arguing in the abstract.

**꼬리질문**:
- "arena를 다른 버퍼와 겹치면 위험은?" → 추론 중 해당 버퍼를 쓰는 코드가 없다는 것을 구조적으로 보장해야 한다(같은 task에서 순차 실행). 링커 섹션으로 명시하고 주석과 assert를 둔다.

### Q02. How do you measure inference latency on a Cortex-M, and how do you report it?

**30초 답변**: DWT cycle counter(CYCCNT)로 `Invoke()` 앞뒤를 잰다. 평균만이 아니라 최악값과 분포를 보고한다. 캐시·flash wait state·인터럽트 때문에 편차가 있다. 칩 경계를 넘는 전체 레이턴시는 GPIO 토글 + 로직 분석기로 잰다. TFLM에는 레이어별 시간을 보는 `MicroProfiler`가 있어 병목 op를 찾는다.

```c
/* DWT CYCCNT로 추론 시간 측정. CMSIS 코어 헤더(벤더 device 헤더가 include) 필요. */
#include <stdint.h>
#include "device.h"   /* 가상의 이름: 벤더의 CMSIS device 헤더로 바꿀 것 */

static inline void cycle_counter_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* trace 블록 enable. CMSIS 6의 Armv8-M 헤더에서는 DCB->DEMCR / DCB_DEMCR_TRCENA_Msk 로 바꿔야 할 수 있음 */
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t cycles_now(void) { return DWT->CYCCNT; }

/* 사용: uint32_t t0 = cycles_now(); run_inference(); uint32_t dt = cycles_now() - t0;
   unsigned 뺄셈이라 32비트 wrap 한 번까지는 올바름. us = dt / (SystemCoreClock / 1000000) */
```

**English answer**: I use the DWT cycle counter around Invoke, and I report the distribution, not just the mean, because flash wait states, cache misses and interrupts cause variance, and the worst case is what breaks real time. For the end-to-end number across chips I toggle GPIOs at each stage and capture them on a logic analyzer. TFLM's MicroProfiler gives per-operator timing to find the hot layer.

**꼬리질문**:
- "Cortex-M0+에는 DWT CYCCNT가 없는데?" → 맞다(Armv6-M에는 CYCCNT 없음). SysTick 또는 하드웨어 타이머, 또는 GPIO + 로직 분석기로 잰다.

### Q03. What should firmware know about INT8 quantization?

**30초 답변**: TFLite INT8은 실수 값을 `real = scale x (q - zero_point)`로 표현한다. 가중치는 보통 채널별(per-channel), 활성값은 텐서별(per-tensor) 양자화다. 펌웨어는 입력을 넣을 때 이 공식의 역으로 양자화하고(`q = round(x / scale) + zero_point`, -128~127 클램프), 출력도 필요하면 역양자화한다. scale과 zero_point는 하드코딩하지 말고 입력 텐서의 `params`에서 읽는다. 모델이 바뀌면 값이 바뀌기 때문이다. feature 추출(log-mel 등)을 학습 때와 **비트 단위로 같은 방식**으로 해야 정확도가 유지된다.

```cpp
// TFLM: 입력 양자화 (scale/zero_point는 모델에서 읽는다)
#include <cmath>
#include <cstdint>
#include "tensorflow/lite/micro/micro_interpreter.h"

void fill_input(tflite::MicroInterpreter& interp, const float* feat, int n)
{
    TfLiteTensor* in = interp.input(0);
    const float scale = in->params.scale;
    const int32_t zp = in->params.zero_point;
    for (int i = 0; i < n; ++i) {
        int32_t q = static_cast<int32_t>(std::lround(feat[i] / scale)) + zp;
        if (q < -128) q = -128;
        if (q > 127) q = 127;
        in->data.int8[i] = static_cast<int8_t>(q);
    }
}
```

**English answer**: In TFLite int8, a real value is scale times q minus zero point, with per-channel scales for weights and per-tensor for activations. Firmware quantizes the input using the scale and zero point read from the input tensor at runtime, never hardcoded, because they change when the model is retrained. The biggest practical risk is the feature front end: if my on-device log-mel doesn't match the training pipeline exactly, accuracy drops silently.

### Q04. Where should model weights live: flash, SRAM, or external memory?

**30초 답변**: 트레이드오프는 속도, 전력, 용량이다. 내부 flash(또는 MRAM) XIP는 SRAM을 아끼지만 wait state 때문에 느릴 수 있고, 캐시가 있으면 완화된다. SRAM에 복사하면 빠르지만 비싼 SRAM을 먹고, retention 전력이 든다. 외부 QSPI/OSPI flash는 용량이 크지만 느리고 전력이 든다. 원칙: 매 hop마다 도는 작은 KWS는 가중치를 내부 flash XIP + 캐시, arena는 SRAM. 큰 모델은 SoC 쪽으로.

**English answer**: It's a trade between speed, power and capacity. Executing weights in place from internal flash or MRAM saves SRAM but adds wait states, which a cache mostly hides for a small model. Copying to SRAM is fastest but uses the scarcest memory and costs retention power in sleep. External flash is big but slow and power hungry. For an always-on keyword model I'd keep weights in internal non-volatile memory and only the arena in SRAM, and anything bigger belongs on the SoC.

### Q05. How do you verify that the model on the device produces the same results as the Python reference?

**30초 답변**: golden vector 테스트. ML 팀이 입력(원시 오디오와 중간 feature)과 기대 출력(INT8 출력 텐서)을 파일로 준다. 기기에서 같은 입력을 넣고 출력 텐서를 비교한다. feature 단계와 모델 단계를 분리해서 비교해야 어디서 어긋나는지 안다. 참조 커널과 CMSIS-NN 최적화 커널 사이에 작은 차이가 있을 수 있으므로 허용 오차를 합의한다. 이 테스트를 CI(가능하면 실제 보드나 에뮬레이터)에 넣는다.

**English answer**: Golden vectors. The ML team exports input audio, intermediate features and expected int8 outputs, and the device runs the same inputs and compares stage by stage, so a mismatch points at either the front end or the model. Optimized kernels can differ slightly from reference kernels, so we agree on a tolerance. And it runs in CI on hardware, because a toolchain or library update can break it silently.

### Q06. How would you schedule inference alongside audio capture and BLE in an RTOS?

**30초 답변**: 우선순위는 캡처 > 추론 > 통신/기타. 캡처는 DMA와 짧은 ISR이라 CPU를 거의 안 쓴다. 추론 task는 hop 주기(예: 20 ms) 안에 끝나야 하고, 최악 실행 시간을 측정해 CPU 사용률에 여유를 둔다. BLE 스택은 벤더가 요구하는 우선순위(보통 높음)가 있으니 그 제약을 먼저 확인한다. 추론이 너무 길면 레이어 단위로 쪼개거나(runtime에 따라 제한적), NPU/DSP로 넘기거나, hop을 늘린다.

**English answer**: Capture has the highest priority but costs almost no CPU because it's DMA plus a tiny ISR. Inference runs in its own task and has to finish within the hop period with margin, so I measure worst-case execution time, not average. The wireless stack often has its own priority requirements from the vendor, so I check those first. If inference doesn't fit, the options are a longer hop, a smaller model, or offloading to an NPU or DSP.

### Q07. How do you trade false-accept rate against power and user experience?

**30초 답변**: 오탐은 SoC wake 에너지와 프라이버시 비용, 미탐은 사용자 경험 비용이다. cascade로 푼다. MCU 1단계는 recall 높게(미탐 적게), SoC 2단계 verifier가 precision을 높인다. 1단계 오탐 한 번은 SoC wake 한 번이므로, 오탐률 x wake 에너지를 배터리 모델에 넣어 목표를 정한다. 임계값은 설정 가능하게 두고 fleet 통계(시간당 wake 수)로 조정한다.

**English answer**: A false accept costs energy and privacy, a false reject costs user trust. I'd use a cascade where the MCU stage is tuned for high recall and the SoC verifier for precision, and put "false accepts per day times energy per SoC wake" into the battery model to set a target. The threshold is a runtime parameter, so we can tune it from fleet statistics without a firmware release.

### Q08. The model will be updated more often than firmware. How do you design for that?

**30초 답변**: 모델을 별도 서명 이미지로 분리한다. 모델 파일에 메타데이터를 둔다: 모델 버전, 요구 feature 버전(샘플레이트, window, hop, bin 수, 정규화), 필요한 op 목록, arena 크기, 입력/출력 shape. FW는 로드 전 이 메타데이터를 검사하고, 맞지 않으면 이전 모델로 fallback한다. op resolver에 없는 op가 필요한 모델은 FW 업데이트가 먼저 필요하다는 것을 manifest 의존성으로 표현한다.

**English answer**: The model becomes its own signed artifact with metadata: model version, the feature front-end version, required operators, arena size, and input and output shapes. Firmware validates that metadata before loading and falls back to the previous model if anything doesn't match. If a new model needs an operator the firmware's op resolver doesn't register, the release manifest expresses that as a dependency on a firmware version.

```cpp
// TFLM 초기화 뼈대: 필요한 op만 등록 (등록 안 된 op가 있으면 AllocateTensors 실패)
#include <cstdint>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

extern const unsigned char g_kws_model[];          // 서명 검증된 모델 이미지
constexpr size_t kArenaSize = 64 * 1024;
alignas(16) static uint8_t tensor_arena[kArenaSize];

bool kws_init(tflite::MicroInterpreter** out)
{
    const tflite::Model* model = tflite::GetModel(g_kws_model);
    // 스키마 버전 확인 (TFLITE_SCHEMA_VERSION 정의 헤더는 TFLM 버전에 따라 다름)
    if (model->version() != TFLITE_SCHEMA_VERSION) return false;

    static tflite::MicroMutableOpResolver<5> resolver;
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddFullyConnected();
    resolver.AddReshape();
    resolver.AddSoftmax();

    static tflite::MicroInterpreter interp(model, resolver, tensor_arena, kArenaSize);
    if (interp.AllocateTensors() != kTfLiteOk) return false;
    // interp.arena_used_bytes() 를 로그로 남겨 예산 추적
    *out = &interp;
    return true;
}
```

### Q09. On the SoC, should the speech model stay resident in memory or be loaded on each wake?

**30초 답변**: 트레이드오프는 wake 레이턴시 vs 메모리 점유 vs 전력이다. resident면 wake 즉시 추론 가능하지만 DRAM을 계속 점유하고, suspend 동안 DRAM self-refresh 전력이 든다(어차피 DRAM이 켜져 있다면 추가 비용은 작다). load-on-wake면 저장소에서 읽는 시간(모델 크기 / 읽기 대역폭)과 DSP/NPU 초기화 시간이 레이턴시에 더해진다. 결정은 측정으로 한다: 모델 로드 시간, 첫 추론 시간(warm-up), 메모리 압박 시 OS가 페이지를 버리는지. 흔한 절충: 작은 2단계 verifier는 resident, 큰 모델은 세션 시작 시 병렬 로드하면서 pre-roll을 버퍼링.

**English answer**: It's wake latency against memory footprint and power. A resident model answers immediately but holds DRAM, and under memory pressure the OS may evict it anyway. Loading on wake adds storage read time plus accelerator initialization and first-inference warm-up. I'd measure those three numbers on the real device, and a common compromise is keeping the small verifier resident and loading the large model in parallel while the pre-roll audio is buffered.

### Q10. What does firmware have to do to use an NPU or DSP for inference?

**30초 답변**: 가속기마다 흐름이 다르지만 펌웨어 책임은 비슷하다. 모델을 가속기용으로 컴파일하는 툴체인(예: Arm Ethos-U는 Vela 컴파일러, 결과가 TFLM의 Ethos-U custom op로 실행됨; Qualcomm DSP는 벤더 SDK)을 빌드에 통합한다. 드라이버 초기화, 전원·클럭 도메인 관리, 가속기가 접근할 수 있는 메모리 영역(DMA 가능, 정렬, 캐시 일관성: CPU 캐시 clean/invalidate)을 보장한다. 가속기가 지원하지 않는 op는 CPU로 fallback되므로 그 시간을 프로파일링한다.

**English answer**: The firmware side is similar across accelerators: integrate the vendor compiler into the build, for example Vela for Arm Ethos-U where the compiled subgraph runs as a custom operator inside TFLM, bring up the driver, manage its power and clock domain, and make sure buffers are in memory the accelerator can reach, properly aligned and cache-coherent, which means cleaning and invalidating caches around the job. Operators the NPU doesn't support fall back to the CPU, and those are often where the latency hides.

### Q11. How do you define the interface contract between the firmware team and the ML team?

**30초 답변**: 문서 하나로 합의한다. 입력: 샘플레이트, 비트, 채널, feature 정의(코드로 공유), 정규화. 모델 제약: op 목록, 최대 arena, 최대 가중치 크기, 최악 추론 시간, 양자화 형식. 출력: 텐서 의미, 후처리(평활화, 임계값) 위치. 검증: golden vector, 허용 오차. 버전 규칙. 그리고 예산이 바뀌면 누가 결정하는지. 제일 중요한 것은 feature 추출 코드를 **한 소스**로 두는 것이다(같은 C 코드를 학습 파이프라인에서도 호출하는 식).

**English answer**: I'd write one contract: input format and the exact feature definition, preferably as shared code; model constraints such as allowed operators, maximum arena, maximum weight size and worst-case latency; output semantics and where post-processing lives; golden vectors with tolerances; and a versioning rule. The single most valuable thing is one implementation of the feature front end used by both training and firmware.

### Q12. The model runs fine on the dev board but is 3x slower on the product board. Where do you look?

**30초 답변**: 같은 칩이라면 하드웨어·설정 차이를 본다. 클럭(PLL 설정, 저전력 모드로 낮은 주파수), flash wait state 설정, 캐시 enable 여부, 가중치 위치(외부 flash로 링크됨?), 최적화 커널이 빌드에 들어갔는지(CMSIS-NN 대신 reference 커널), 컴파일러 최적화 레벨, 인터럽트 부하(BLE, 센서). 확인 방법: `SystemCoreClock`과 실제 클럭을 MCO 핀·스코프로 비교, map 파일로 섹션 위치 확인, MicroProfiler로 op별 시간 비교.

**English answer**: If it's the same silicon, it's configuration. I'd check the actual core clock on a clock-out pin, flash wait states, whether the cache is enabled, whether the linker put the weights in external flash, whether the build actually used the CMSIS-NN kernels or fell back to reference kernels, the optimization level, and interrupt load from the radio stack. The map file and a per-operator profile comparison between the two boards usually find it quickly.

**Don 스토리 연결**: SSD "firmware performance tuning" 경험 → "성능이 기대와 다를 때 클럭, 메모리 배치, 인터럽트 부하 순으로 체계적으로 좁혔다 <구체 사례 채우기>".

---

## 12. 최종 점검

- [ ] 45분 시간표를 보지 않고 말할 수 있다
- [ ] 요구사항 질문 5개와 답이 없을 때의 가정 숫자를 말할 수 있다
- [ ] MCU / SoC / 라디오 / PMIC 블록도를 3분 안에 그릴 수 있다
- [ ] 전원 상태 5개와 상태별 전류 목표, 배터리 계산을 즉석에서 할 수 있다
- [ ] mic → PDM → DMA ping-pong → ring → VAD → KWS → IPC 파이프라인과 32 KB/s, 48 KB pre-roll 계산을 설명할 수 있다
- [ ] MCU 메모리 예산 표와 레이턴시 분해를 그릴 수 있다
- [ ] IPC 프레임 헤더(sync, ver, type, seq, len, ts, CRC)와 wake 핸드셰이크를 설명할 수 있다
- [ ] 실패 모드 5개와 탐지·대응을 말할 수 있다
- [ ] INT8 양자화 공식, arena 실측, golden vector, 모델 metadata를 설명할 수 있다
- [ ] TFLM 초기화 순서(GetModel → op resolver → MicroInterpreter → AllocateTensors → Invoke)를 쓸 수 있다

| 자주 틀리는 포인트 | 바른 설명 |
|---|---|
| wake word 모델부터 설명 시작 | 요구사항·전력 예산이 먼저. 모델은 파이프라인의 한 블록 |
| 평균 전류를 세션 전류로 계산 | 대기 floor와 오탐 wake 에너지를 따로 계산 |
| ISR에서 feature 계산 | ISR은 알림만. 처리는 task |
| scale/zero_point 하드코딩 | 입력 텐서 `params`에서 읽기 |
| "MCU 추론 시간 = 평균" | 최악값과 분포, 인터럽트 포함 |
| pre-roll 없이 wake 후부터 녹음 | wake word와 문장 앞부분이 사라진다 |
| IPC에 버전/seq/CRC 없음 | 필드 디버깅과 OTA 버전 혼재가 불가능해진다 |
| Cortex-M0+에서 DWT CYCCNT 사용 | Armv6-M에는 없음. 타이머나 GPIO로 측정 |
