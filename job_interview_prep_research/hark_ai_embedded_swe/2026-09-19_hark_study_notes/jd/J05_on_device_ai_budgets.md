# J05. Collaborate with the on-device AI team to support model inference within memory and latency budgets

> **분류**: Responsibility 5/7 · **관련 개념 노트**: C08(온디바이스 ML 추론), C03(I2S/DMA), C04(RTOS), C05(저전력), S05(시스템 설계)
> **Don 현재 상태**: ❌ 갭 — 레쥬메에 ML 런타임 경험이 없다(context 3.1). 다만 "메모리 맵·SRAM 예산·DMA 스케줄링을 엄격히 관리한 경험"은 그대로 전이되므로 **런타임 용어만 채우면 🟡로 올릴 수 있는 갭**이다.
> **이 노트를 다 읽으면**: ① 이 JD 문장이 요구하는 것이 "모델을 만드는 일"이 아니라 "실행 환경과 예산을 소유하는 일"임을 설명할 수 있다 · ② 메모리·레이턴시 예산을 숫자로 세우고 협상하는 절차를 말할 수 있다 · ③ "모델이 안 들어간다"는 상황에서 거절 대신 대안 사다리를 제시할 수 있다

---

## 0. 문장 뜯어보기

| 구(句) | 표면적 뜻 | 채용담당자가 이 단어를 고른 이유 |
|---|---|---|
| `Collaborate with` | 같이 일한다 | **Own이 아니다.** 다른 6개 Responsibility는 "Develop / Own / Optimize / Build / Debug"인데 이 문장만 Collaborate다. 모델의 정확도·구조는 내 책임이 아니라는 뜻이고, 동시에 "다른 팀과 인터페이스를 맞출 줄 아는가"를 본다 |
| `the on-device AI team` | 별도 조직이 있다 | Hark에는 On-Device AI Inference 공고가 따로 있다(밴드 $200K–$450K, "Hexagon DSP, Ambiq-class MCUs", INT8/INT4, speech 추론) [확인됨][context 2.2]. 즉 **모델·양자화·커널 최적화 전문가가 이미 있고**, 펌웨어는 그 아래 레이어를 책임진다 |
| `to support` | 지원한다 | 지원 = 실행 환경 제공. 런타임 통합, 메모리 배치, 스케줄링, 전력 상태, 측정 인프라. "모델이 왜 느린지 데이터를 내놓는 사람"이 펌웨어다 |
| `model inference` | 추론만 | 학습(training)·데이터 수집·아키텍처 탐색은 범위 밖. 기기에서 도는 것은 **forward pass 한 번**뿐이다 |
| `within` | 안에서 | **제약이 먼저 정해져 있다는 선언.** 모델이 예산을 정하는 게 아니라 하드웨어가 정한 예산에 모델이 들어와야 한다. 이 단어 하나가 협상의 방향을 정한다 |
| `memory ... budgets` | 메모리 예산 | flash(가중치·코드)와 SRAM(activation·arena·오디오 버퍼·스택)이 고정 자원. always-on MCU라면 수백 KB 단위 싸움 |
| `latency budgets` | 레이턴시 예산 | 두 종류다. 사용자 체감 레이턴시(말이 끝나고 반응까지)와 실시간성(프레임이 들어오는 속도보다 빨리 처리해야 함). 면접에서 이 둘을 구분하면 점수가 크다 |

**한 줄 요약**: 이 문장은 "ML을 할 줄 아는가"를 묻는 게 아니라 **"모델이라는 남의 코드를, 내가 소유한 좁은 하드웨어 안에서 제시간에 돌게 만들고, 그 한계를 숫자로 협상할 수 있는가"**를 묻는다.

> JD의 Bonus Qualifications에도 "Exposure to ML inference runtimes on embedded platforms"가 있다. **Requirement가 아니라 Bonus**라는 점이 중요하다. 모르는 것을 숨길 필요가 없고, "아직 런타임을 직접 통합해 본 적은 없지만 제약을 관리하는 일은 계속 해 왔다"가 정직하면서도 강한 답이다.

---

## 1. Hark에서 실제로 하게 될 일 (추정)

[추정] context 2.2·2.7의 구조 추정(Qualcomm SoC + Android, always-on 저전력 MCU(Ambiq Apollo 계열), Hexagon DSP, 마이크·센서 내장 웨어러블급 기기)을 근거로 한다. 확정 사실이 아니다.

### 1.1 하루 단위로 보면

- 아침: 밤새 돌린 야간 회귀 결과를 본다. 어제 AI 팀이 올린 새 모델 파일로 빌드한 이미지의 **arena 사용량, 추론 cycle 수, 평균 전류**가 테이블에 찍혀 있다. 전날 대비 arena가 8KB 늘어 SRAM 여유가 12KB 남았다는 경고가 떠 있다.
- 오전: AI 팀 엔지니어와 15분 싱크. "이번 모델은 채널 수를 늘려 정확도가 1.2%p 올랐다"는 얘기에 "그 대가로 arena가 8KB, 추론이 3.1ms 늘었고 SRAM 여유가 12KB다. 다음 모델까지 고려하면 여기서 멈춰야 한다"고 숫자로 답한다.
- 오후: 오디오 경로 디버깅. 마이크 DMA half/full 인터럽트와 추론 task의 우선순위가 겹쳐 가끔 프레임이 드롭된다. 로직 분석기와 GPIO 토글로 타임라인을 찍는다.
- 저녁: 새 모델 바이너리를 넣은 OTA 이미지 크기가 슬롯 한계를 넘는지 확인하고, 넘으면 OTA 담당(J04)과 슬롯 레이아웃을 다시 본다.

### 1.2 한 주 단위로 보면

| 활동 | 비중 [가정] | 산출물 |
|---|---|---|
| 런타임/커널 통합 및 모델 교체 대응 | 30% | 모델 바이너리를 넣고 빌드가 깨지지 않게 하는 통합 레이어 |
| 측정·프로파일링 리포트 | 25% | 레이어별 cycle 표, arena 사용량, 평균 전류, 실패 프레임 카운터 |
| 오디오 캡처 경로 유지 (I2S/PDM, DMA, 링버퍼) | 20% | 드롭 없는 프레임 공급 |
| 예산 협상·문서화 | 15% | "모델 계약" 문서 갱신, 회의 |
| 스케줄링·전력 상태 연동 | 10% | 추론 task 우선순위, SoC wake 프로토콜 |

### 1.3 이 역할이 실제로 소유하는 것 / 소유하지 않는 것

- 소유한다: 마이크에서 모델 입력 텐서까지의 모든 코드, 런타임 초기화와 메모리 배치, 추론이 도는 task와 우선순위, 측정 인프라, 모델 바이너리의 저장·업데이트·버전 검사, factory에서의 마이크 calibration(J06과 연결).
- 소유하지 않는다: 모델 구조, 학습 데이터, 양자화 방식 결정, 정확도 목표.
- 경계에서 같이 정한다: 지원 가능한 연산자(op) 목록, 입력 텐서의 shape·dtype·스케일, 특징 추출(feature) 알고리즘의 정확한 정의, 예산 숫자 자체.

> **왜 이 구분이 면접에서 중요한가**: 스타트업 1세대 기기에서 가장 흔한 사고는 "모델은 PC에서 95% 정확한데 기기에서 88%"이고, 원인의 대부분은 모델이 아니라 **펌웨어가 소유한 구간**(마이크 게인, 특징 추출 구현 차이, 프레임 드롭, 양자화 스케일 불일치)에 있다. 이 구간을 데이터로 배제해 줄 수 있는 사람이 이 자리에 필요한 사람이다.

---

## 2. 핵심 개념

### 2.1 예산은 세 개다

| 예산 | 단위 | 누가 정하나 | 넘으면 생기는 일 |
|---|---|---|---|
| 메모리 | KB (flash / SRAM 별도) | 하드웨어(부품 선정) | 링크 실패, `AllocateTensors()` 실패, 혹은 조용한 스택 오버플로 |
| 레이턴시 | ms (또는 cycle) | 제품/UX + 실시간성 | 프레임 드롭 → 정확도 하락, 사용자 체감 지연 |
| 에너지 | µA 평균, mJ/추론 | 배터리 수명 목표 | 배터리 목표 미달 (C05와 직결) |

세 예산은 서로 거래된다. 예를 들어 클럭을 올리면 레이턴시는 줄지만 에너지는 늘고, 가중치를 SRAM으로 복사하면 레이턴시는 줄지만 메모리가 준다. **면접에서 "레이턴시를 줄이려면?"이라는 질문에 "어느 예산을 대신 쓸 수 있나요?"라고 되묻는 것이 좋은 답의 시작**이다.

### 2.2 파이프라인 — 마이크에서 액션까지

```
[아날로그/PDM 마이크]
      | PDM 또는 I2S 비트스트림 (하드웨어 주변장치가 받음)
      v
[PDM decimator / I2S 주변장치]  --DMA-->  [ping-pong PCM 버퍼]
      |                                        (예: 16kHz, 16-bit, 10ms = 320 bytes/프레임)
      | half/full 인터럽트
      v
[오디오 task]  프레임 큐에 push  (ISR은 포인터만 넘긴다)
      |
      v
[특징 추출]  pre-emphasis -> 윈도우 -> FFT -> mel filterbank -> log -> (선택) 양자화
      |        결과: 예를 들어 40개 mel bin x 1 프레임
      v
[슬라이딩 윈도우 버퍼]  최근 N 프레임(예: 49프레임 = 약 1초)을 모아 모델 입력 텐서 구성
      |
      v
[추론 엔진]  TFLite Micro / 벤더 런타임 / DSP·NPU 오프로드
      |        출력: 클래스별 점수
      v
[후처리]  smoothing, 임계값, 연속 히트 수 조건, 쿨다운
      |
      v
[액션]  (1) SoC를 깨운다  (2) pre-roll 오디오를 SoC로 넘긴다  (3) 햅틱/LED 피드백
```

핵심은 **"모델"이 이 그림에서 차지하는 비중이 생각보다 작다**는 점이다. JD가 펌웨어 엔지니어에게 맡기는 것은 그림의 나머지 전부다.

### 2.3 메모리 예산 — 어디에 무엇이 가나

| 항목 | 보통 어디에 | 크기 성격 | 펌웨어가 조절 가능한가 |
|---|---|---|---|
| 모델 가중치 | flash (XIP) 또는 SRAM으로 복사 | 모델이 결정 | 배치 위치만 (속도 vs SRAM 거래) |
| tensor arena (activation + 스크래치) | SRAM, 정적 배열 | 모델 그래프가 결정 | 크기는 못 줄임, 배치(TCM/일반 SRAM)는 선택 가능 |
| 런타임 코드 + 커널 | flash | op 개수에 비례 | op resolver를 좁혀 줄일 수 있음 |
| 오디오 링버퍼 + pre-roll | SRAM | 펌웨어가 결정 | **여기가 협상 카드다** |
| 특징 추출 스크래치 (FFT 버퍼 등) | SRAM | 펌웨어가 결정 | 재사용·공유 가능 |
| RTOS task 스택 | SRAM | 펌웨어가 결정 | 측정해서 조이기 |
| 무선 스택 (BLE/Wi-Fi 호스트) | SRAM | 스택이 결정 | 연결 수·버퍼 수 튜닝 |

> **가장 흔한 초보 실수**: tensor arena를 함수 지역 변수로 선언하는 것. 수십 KB가 task 스택에 올라가 부팅 직후 HardFault가 난다. 반드시 정적 전역으로, 가능하면 링커 스크립트에서 전용 섹션에 배치한다. (C08 §12에 증상 표가 있다.)

### 2.4 레이턴시 예산 — 두 종류를 구분하라

**(A) 실시간성 (real-time factor).** 오디오가 10ms마다 한 프레임씩 들어온다면, 특징 추출 + 추론 + 후처리의 합이 10ms보다 작아야 한다. 이 비율을 real-time factor라고 부른다. 0.3이면 여유가 70%다.

```
프레임 주기 = 10 ms  (16kHz, 160 샘플 hop 기준)
|<---------------- 10 ms ---------------->|
| feature 1.2ms | inference 3.5ms | post 0.1ms |        idle 5.2ms          |
                                              real-time factor = 4.8/10 = 0.48
```

**(B) 사용자 체감 레이턴시 (end-to-end).** 사용자가 웨이크워드를 말한 순간부터 기기가 반응(LED·햅틱·SoC wake)하기까지. 여기엔 모델이 판단을 내리기 위해 기다려야 하는 오디오 길이(윈도우), 후처리의 연속 히트 조건, IPC 지연, SoC가 깨어나는 시간이 다 들어간다.

```
[가정: 실제 값은 제품·SoC마다 다르다. 아래는 예산표를 "어떻게 쓰는지" 보여주는 예시 숫자다]
발화 끝  0 ms
 +  0~30 ms   마지막 프레임이 DMA로 채워지기까지 대기
 + 30~35 ms   특징 추출
 + 35~40 ms   추론
 + 40~60 ms   후처리(연속 2프레임 히트 조건)
 + 60~65 ms   MCU -> SoC 인터럽트 + IPC 메시지
 + 65~???     SoC 웨이크 (수십~수백 ms, 전원 상태에 따라 크게 다름)
----------------------------------------------------
MCU 구간 목표 [가정]: 100 ms 이하
```

면접에서 좋은 답은 이것이다. "레이턴시 목표를 하나의 숫자로 받지 않는다. **어느 구간의 레이턴시인지, 평균인지 p99인지**부터 정의한다. 나는 MCU 구간의 p99를 보장하고, SoC 웨이크 구간은 전원 상태 정책에 따라 별도로 예산을 잡는다."

### 2.5 "모델 계약" — 펌웨어와 AI 팀이 문서로 고정할 것

실무에서 이 JD 문장을 잘 수행한다는 것은 **회의가 아니라 문서를 만든다**는 뜻이다. 한 장짜리 계약 문서에 들어갈 항목:

| 항목 | 예시 값 [가정] | 누가 채우나 |
|---|---|---|
| 입력 텐서 shape / dtype | `[1, 49, 40, 1]`, int8 | AI 팀 |
| 입력 quantization scale / zero_point | scale 0.0472, zp -128 | AI 팀 |
| 특징 추출 정의 (샘플레이트, 윈도우, hop, mel 개수, log 밑, 정규화) | 16kHz, 30ms 윈도우, 10ms hop, 40 mel | 합의 |
| 출력 텐서 shape / 클래스 순서 | `[1, 4]`, {silence, unknown, wake, cancel} | AI 팀 |
| 허용 연산자 목록 | CONV_2D, DEPTHWISE_CONV_2D, FULLY_CONNECTED, SOFTMAX, RESHAPE | 펌웨어가 제시, AI 팀이 준수 |
| flash 예산 (가중치) | 180 KB | 펌웨어 |
| SRAM 예산 (arena) | 64 KB | 펌웨어 |
| 추론 레이턴시 상한 | 5 ms @ 96 MHz | 펌웨어가 측정해서 제시 |
| golden 테스트 벡터 | 입력 WAV 20개 + 기대 출력 | 합의 |
| 모델 버전 규칙 | 헤더에 model_id, schema_version | 펌웨어 |

**golden 벡터가 계약의 핵심이다.** 펌웨어 쪽 특징 추출 C 구현과 AI 팀의 Python 구현이 bit-exact가 아니면 정확도가 조용히 떨어진다(training/serving skew). 같은 WAV를 넣어 양쪽 결과를 비교하는 테스트를 CI에 넣어 두면 "기기에서만 정확도가 낮다"는 분쟁의 90%가 사라진다.

### 2.6 측정 — 숫자가 없으면 협상도 없다

| 방법 | 무엇을 재나 | 정확도 | 주의 |
|---|---|---|---|
| DWT CYCCNT (`DWT->CYCCNT`) | 코어 cycle 수 | 매우 정확 | Cortex-M0/M0+에는 없다(벤더·코어마다 다름). `CoreDebug->DEMCR`의 TRCENA를 먼저 켜야 함 |
| GPIO 토글 + 오실로스코프/로직 분석기 | 실제 벽시계 시간 | 매우 정확, 비침습 | Don이 가장 잘하는 방법. ISR 지터까지 보인다 |
| RTOS 런타임 통계 (FreeRTOS `vTaskGetRunTimeStats` 등) | task별 CPU 점유 | 중간 | 타이머 설정 필요, 오버헤드 있음 |
| TFLM `tflite::MicroProfiler` | op별 시간 | 중간 | 프로파일러 자체 오버헤드 있음. API는 버전에 따라 다름 |
| 전류계 (Nordic PPK2, Joulescope 등) | 추론 1회 에너지 | 높음 | GPIO 토글과 동기화하면 구간별 에너지를 뗄 수 있다 |

```c
/* DWT cycle counter로 구간 측정 (Cortex-M3/M4/M7/M33 등 DWT가 있는 코어 한정).
   CMSIS 헤더(core_cm4.h 등)가 CoreDebug, DWT를 정의한다.
   주의: Cortex-M0/M0+에는 CYCCNT가 없다. 코어별로 확인할 것. */
#include <stdint.h>
#include "stm32f4xx.h"   /* 예시. 실제로는 해당 벤더 CMSIS 헤더 */

static void cycle_counter_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; /* trace 블록 전원 인가 */
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;           /* 카운터 시작 */
}

static inline uint32_t cycles_now(void) { return DWT->CYCCNT; }

/* 사용 예: 추론 구간만 잰다 */
void measure_inference(void)
{
    uint32_t t0 = cycles_now();
    run_inference();                 /* TFLM Invoke() 등 */
    uint32_t dt = cycles_now() - t0; /* 32비트 wrap은 뺄셈으로 자연히 처리됨 */

    /* 96 MHz 기준 us 변환. 클럭이 바뀌면 이 상수도 바꿔야 한다 */
    uint32_t us = dt / 96u;
    log_metric("infer_us", us);
}
```

> **면접용 한 줄**: "cycle counter로 평균을 재고, GPIO 토글과 스코프로 최악값(p99)을 본다. 평균만 보고 예산을 정하면 프레임 드롭은 항상 최악값에서 난다."

---

## 3. 실무 패턴과 함정

### 3.1 예산표를 살아 있는 문서로 유지한다

빌드마다 자동으로 갱신되는 표 하나가 이 JD 문장의 실질적 산출물이다.

| 항목 | 예산 [가정] | 현재 | 여유 | 추세 |
|---|---|---|---|---|
| flash 총 사용 | 1600 KB | 1412 KB | 188 KB | +6 KB/주 |
| 모델 가중치 (flash) | 200 KB | 180 KB | 20 KB | 고정 |
| SRAM 총 사용 | 480 KB | 441 KB | 39 KB | +3 KB/주 |
| tensor arena | 64 KB | 58 KB | 6 KB | +8 KB (이번 모델) |
| 추론 레이턴시 (평균) | 5.0 ms | 3.5 ms | 1.5 ms | +0.4 ms |
| 추론 레이턴시 (p99) | 6.0 ms | 5.8 ms | 0.2 ms | 경고 |
| real-time factor | 0.6 | 0.48 | — | 상승 중 |
| 평균 전류 (KWS 상시) | 400 µA | 355 µA | 45 µA | 상승 중 |

**"추세" 열이 있으면 협상이 정치가 아니라 산수가 된다.** "지금은 들어가지만 이 속도로 커지면 3주 뒤에 안 들어간다"는 말은 반박하기 어렵다.

### 3.2 빌드 타임 예산 가드

숫자를 문서에만 두면 반드시 넘는다. 빌드가 깨지게 만든다.

```c
/* budget.h — 예산을 코드로 고정한다.
   아래 숫자는 모두 [가정]이다. 실제 값은 보드 SRAM/flash와 예산 회의 결과로 정한다. */
#include <assert.h>
#include <stddef.h>

#define BUDGET_TENSOR_ARENA_BYTES   (64u * 1024u)
#define BUDGET_MODEL_FLASH_BYTES    (200u * 1024u)
#define BUDGET_AUDIO_RING_BYTES     (32u * 1024u)

/* arena는 반드시 정적 전역. 지역 변수로 두면 task 스택을 터뜨린다.
   16바이트 정렬은 커널(특히 SIMD/DSP 경로)이 요구하는 경우가 있어 넉넉히 준다. */
_Alignas(16) static uint8_t g_tensor_arena[BUDGET_TENSOR_ARENA_BYTES];

/* 모델 바이너리를 배열로 링크했다면 크기를 컴파일 타임에 검사한다 */
extern const unsigned char g_model_data[];
extern const unsigned int  g_model_data_len;

/* C11 static_assert. C++이면 static_assert 그대로 사용 가능 */
_Static_assert(BUDGET_TENSOR_ARENA_BYTES <= 96u * 1024u,
               "arena budget raised without review");
```

런타임에서는 실제 사용량을 한 번 찍어 둔다. TFLM이라면 `interpreter.arena_used_bytes()`가 `AllocateTensors()` 성공 후 실제 사용 바이트를 돌려준다(메서드 이름은 TFLM 버전에 따라 다를 수 있으니 사용하는 리비전에서 확인할 것).

```cpp
// 부팅 시 1회: 예산 대비 실사용을 로그로 남기고, 여유가 임계 이하면 경고한다.
if (interpreter.AllocateTensors() != kTfLiteOk) {
    // 여기서 죽지 말고, 모델 없이도 기본 기능은 살아 있게 한다(안전 축퇴).
    model_state = MODEL_STATE_UNAVAILABLE;
} else {
    const size_t used = interpreter.arena_used_bytes();
    MicroPrintf("arena used=%u budget=%u", (unsigned)used,
                (unsigned)BUDGET_TENSOR_ARENA_BYTES);
}
```

> **안전 축퇴(graceful degradation)가 중요하다.** 모델 로드 실패가 기기 전체를 벽돌로 만들면 안 된다. 모델은 OTA로 바뀔 수 있는 데이터이므로, 펌웨어는 "모델이 없거나 깨졌을 때도 부팅하고 BLE로 붙을 수 있는" 상태를 반드시 가져야 한다. 이건 J04(OTA)와 이어지는 설계 원칙이다.

### 3.3 "모델이 안 들어간다"고 말하는 법 — 대안 사다리

면접에서 가장 자주 나오는 시나리오다(S05 Q01과 같은 계열). 나쁜 답은 "안 됩니다"이고, 좋은 답은 **비용이 낮은 순서대로 사다리를 제시하는 것**이다.

| 단계 | 조치 | 누가 하나 | 비용 | 위험 |
|---|---|---|---|---|
| 1 | 실제 사용량 측정 (추측 금지) | 펌웨어 | 없음 | 없음 |
| 2 | op resolver를 필요한 op만으로 좁혀 코드 크기 축소 | 펌웨어 | 낮음 | 누락 시 런타임 에러 |
| 3 | 다른 서브시스템 버퍼 조정 (pre-roll 2초 → 1.5초, BLE 버퍼 수) | 펌웨어 | 낮음 | 기능 저하 |
| 4 | arena를 더 빠른/느린 메모리로 재배치, 버퍼 공유(union) | 펌웨어 | 중간 | 동시 사용 시 손상 위험 |
| 5 | 가중치를 SRAM 복사 대신 flash XIP로 (또는 반대로) | 펌웨어 | 중간 | 레이턴시·전력 변동 |
| 6 | 모델 구조 축소 요청 (채널 수, 레이어 수, 입력 윈도우 길이) | AI 팀 | 높음 | 정확도 하락 |
| 7 | 더 강한 양자화 (INT8 → INT4 등) | AI 팀 | 높음 | 정확도 하락, 커널 지원 여부 확인 필요 |
| 8 | 2단계 cascade로 분할 (작은 1차 모델이 MCU, 큰 2차 모델이 SoC/DSP) | 합의 | 높음 | 아키텍처 변경 |
| 9 | 하드웨어 변경 (SRAM 더 큰 파트) | HW | 매우 높음 | 일정·BOM |

말하는 순서도 정해 두면 좋다. "① 현재 숫자 ② 어디까지가 내 쪽에서 가능한 절감 ③ 그래도 부족한 양 ④ 그쪽에 부탁할 선택지 ⑤ 각 선택지의 기한과 위험." 이 다섯 단계가 곧 "collaborate"의 실제 모습이다.

### 3.4 함정 표

| 함정 | 증상 | 근본 원인 | 대응 |
|---|---|---|---|
| 특징 추출 skew | PC에서 95%, 기기에서 88% | 윈도우 함수·정규화·log 밑·dither가 Python과 C에서 다름 | golden 벡터 CI. 프레임 단위 수치 비교 |
| 마이크 게인·감도 미보정 | 조용한 환경에서 잘 되다 시끄러운 곳에서 무너짐 | 부품 편차 + AGC 미정의 | factory 마이크 calibration (J06 §3.3) |
| 프레임 드롭 | 간헐적 오인식, 재현 어려움 | 추론 task가 오디오 DMA 인터럽트보다 오래 잡고 있음 | 드롭 카운터를 항상 telemetry에 넣는다. 우선순위 재설계 |
| arena를 스택에 | 부팅 직후 HardFault | 수십 KB 지역 변수 | 정적 전역 + 섹션 배치 |
| 캐시가 있는 코어에서 DMA 버퍼 | 오디오에 잡음, 간헐적 | 캐시 clean/invalidate 누락 | non-cacheable 영역 또는 명시적 유지보수 (C08 §9) |
| 평균만 측정 | 예산 안인데 실전에서 드롭 | 최악값·지터 미측정 | p99와 최대값을 같이 기록 |
| 클럭 올려서 해결 | 레이턴시 OK, 배터리 미달 | 에너지 예산 무시 | 세 예산을 항상 같이 본다 |
| 모델 OTA 후 부팅 실패 | 필드 벽돌 | 모델과 런타임 버전 불일치 | 모델 헤더에 schema/op 버전, 부팅 시 검사, 실패 시 이전 모델로 폴백 |
| op resolver에 op 누락 | 런타임에 "op not found" | 모델이 새 op을 쓰기 시작 | 허용 op 목록을 계약에 넣고, 위반 시 빌드에서 잡는 스크립트 |

### 3.5 스케줄링 패턴 — 추론을 어떤 task에서 돌릴 것인가

```
우선순위 (높음 → 낮음)  [가정: 실제 값은 시스템 전체를 보고 정한다]
  ISR              : I2S/PDM DMA half/full  — 포인터만 큐에 넣고 즉시 반환
  prio 6  audio    : PCM -> 특징 추출, 슬라이딩 윈도우 갱신
  prio 5  radio    : BLE 호스트 (스택이 요구하는 우선순위 준수)
  prio 4  infer    : 모델 Invoke()  <-- 길게 도는 작업. 여기를 너무 높이면 오디오가 밀린다
  prio 2  app      : 후처리, 상태머신, SoC IPC
  prio 0  idle     : tickless idle -> 저전력 진입
```

원칙 세 가지.

1. **추론은 오디오 캡처보다 낮은 우선순위**에 둔다. 모델이 한 프레임 늦게 나오는 것은 괜찮지만 오디오 프레임을 잃으면 되돌릴 수 없다.
2. 추론이 도중에 선점되어도 상관없게 만든다. 즉 추론 중에 공유 버퍼를 건드리지 않는다. 입력 텐서는 추론 시작 전에 복사(또는 소유권 이전)해 둔다.
3. 추론은 절대 ISR에서 돌리지 않는다. 수 ms짜리 작업이 인터럽트 컨텍스트에 있으면 시스템 전체의 최악 지연이 무너진다.

### 3.6 오디오 캡처 쪽에서 펌웨어가 반드시 세는 것

```c
/* 이런 카운터가 없으면 "가끔 인식이 안 된다"는 버그를 절대 못 잡는다.
   telemetry(J04)와 factory 로그(J06) 양쪽에 같은 이름으로 실어 보낸다. */
typedef struct {
    uint32_t frames_captured;   /* DMA가 채운 프레임 수 */
    uint32_t frames_dropped;    /* 큐가 가득 차 버린 프레임 수 */
    uint32_t infer_invocations; /* 추론 호출 수 */
    uint32_t infer_overruns;    /* 프레임 주기 내에 못 끝낸 횟수 */
    uint32_t infer_us_max;      /* 관측된 최대 추론 시간 */
    uint32_t wake_triggers;     /* 후처리가 wake로 판단한 횟수 */
} ai_pipeline_stats_t;
```

> **면접에서 쓸 문장**: "내가 AI 팀에 주는 것은 의견이 아니라 이 카운터들이다. 드롭이 0인데 정확도가 낮으면 모델 쪽 문제고, 드롭이 있으면 내 쪽 문제다. 이 경계를 먼저 만들어 두면 서로 시간을 안 뺏는다."

---

## 4. 리서치 — 근거 자료

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| TensorFlow Lite for Microcontrollers (소스) | MCU용 인터프리터, 커널, 예제 | `tensorflow/lite/micro/examples/micro_speech`, `tensorflow/lite/micro/docs/` | https://github.com/tensorflow/tflite-micro |
| TFLM micro_speech 예제 | 웨이크워드 파이프라인의 최소 구현 | 특징 추출 + arena 크기 + op resolver 구성 | https://github.com/tensorflow/tflite-micro/tree/main/tensorflow/lite/micro/examples/micro_speech |
| LiteRT (구 TensorFlow Lite) 공식 문서 | 양자화, 변환, 마이크로 가이드 | post-training quantization 절 | https://ai.google.dev/edge/litert |
| CMSIS-NN | Cortex-M용 int8 신경망 커널 (`arm_convolve_s8`, `arm_fully_connected_s8`, `arm_depthwise_conv_s8` 등) | README의 지원 op 표, 성능 가이드 | https://github.com/ARM-software/CMSIS-NN |
| CMSIS-DSP | FFT·윈도우·필터 (`arm_rfft_fast_f32`, `arm_cmplx_mag_f32` 등) | FFT와 윈도우 함수 모듈 | https://github.com/ARM-software/CMSIS-DSP |
| Jacob et al. 2017, integer-arithmetic-only inference | TFLite int8 양자화 수식의 원전 (scale/zero-point, requantization) | 2장 수식 | https://arxiv.org/abs/1712.05877 |
| gemmlowp quantization 문서 | 정수 곱셈·requantization을 코드 수준으로 설명 | 전체(짧다) | https://github.com/google/gemmlowp/blob/master/doc/quantization.md |
| Hello Edge (KWS on MCU) 논문 | MCU 웨이크워드 모델의 메모리·연산 예산 비교(DS-CNN 등) | 표 형태의 메모리/MAC 비교 | https://arxiv.org/abs/1711.07128 |
| ARM ML-KWS-for-MCU | 위 논문의 코드·모델 | 모델 크기별 구성 | https://github.com/ARM-software/ML-KWS-for-MCU |
| Speech Commands 데이터셋 논문 | 웨이크워드 평가에 쓰이는 표준 데이터셋 | 평가 방법 절 | https://arxiv.org/abs/1804.03209 |
| MLPerf Tiny | MCU 추론 벤치마크(KWS 포함), 레이턴시·에너지 측정 방법론 | 벤치마크 규칙 | https://github.com/mlcommons/tiny |
| Arm Ethos-U NPU | microNPU 개요, Vela 컴파일러로 사전 컴파일하는 흐름 | 지원 op와 메모리 모드 | https://developer.arm.com/Processors/Ethos-U55 |
| Ethos-U Vela 컴파일러 | TFLite 모델을 NPU 커맨드 스트림으로 변환 | 사용법, 지원 op 목록 | https://pypi.org/project/ethos-u-vela/ |
| ARMv7-M Architecture Reference Manual | DWT CYCCNT, DEMCR 등 레지스터 정의 | DWT 절 | https://developer.arm.com/documentation/ddi0403/latest/ |
| Zephyr DMIC (디지털 마이크) API | `dmic_configure`, `dmic_trigger`, `dmic_read` | 샘플과 devicetree 바인딩 | https://docs.zephyrproject.org/latest/hardware/peripherals/audio/dmic.html |
| Qualcomm Hexagon DSP SDK | Hexagon DSP 개발 툴체인 (Hark 구조 추정과 연결) | 개요와 툴 구성 | https://developer.qualcomm.com/software/hexagon-dsp-sdk |
| Ambiq (Apollo 계열 초저전력 MCU) | always-on MCU 후보 벤더 | 제품별 SRAM/flash, AI 가속 기능 | https://ambiq.com/ |

**버전 의존 경고.**

- TFLM의 C++ API는 리비전에 따라 바뀌었다. 특히 `tflite::ErrorReporter`/`MicroErrorReporter`는 최신 트리에서 `MicroPrintf`로 대체되었고, `AllocateTensors()` 이후 사용량을 얻는 메서드 이름도 과거와 다를 수 있다. **사용하는 커밋의 헤더를 직접 확인할 것.**
- CMSIS-NN 함수 시그니처는 메이저 버전에서 구조체 기반(`arm_nn_context`, `arm_conv_params` 등)으로 바뀌었다. 예전 튜토리얼 코드가 그대로 컴파일되지 않는다.
- TensorFlow Lite 문서는 tensorflow.org에서 ai.google.dev/edge(LiteRT)로 이전되었다. 오래된 링크는 리다이렉트되거나 끊길 수 있다.
- Ethos-U Vela의 지원 op 목록은 릴리스마다 달라진다. "이 op이 NPU로 갈지 CPU로 fallback할지"는 반드시 사용 중인 Vela 버전 기준으로 확인한다.
- 벤더 NPU/DSP(Qualcomm Hexagon, Ambiq 등)의 런타임과 툴은 NDA·SDK 버전에 강하게 묶인다. 면접에서는 일반 원리로 말하고 구체적 API를 단정하지 않는 편이 안전하다.

---

## 5. 예상 면접 질문

### Q01. What does "supporting model inference" actually mean for a firmware engineer?

**왜 묻나**: JD 문장 그대로다. 역할 경계를 아는지, 그리고 ML을 모른다고 위축되지 않는지 본다.

**30초 답변**: 모델을 만드는 게 아니라 모델이 도는 환경 전체를 소유한다는 뜻이다. 마이크에서 입력 텐서까지의 데이터 경로, 런타임 초기화와 메모리 배치, 추론이 도는 task와 우선순위, 측정 인프라, 모델 바이너리의 저장·업데이트·버전 검사가 내 몫이다. AI 팀은 무엇을 계산할지, 나는 그 계산을 이 하드웨어에서 제시간에 적은 전력으로 안전하게 돌리는 방법을 책임진다.

**English answer**: To me it means I own everything around the model, not the model itself. That's the audio path from the microphone DMA to the input tensor, the runtime setup and where the arena and weights live, the task that runs inference and its priority, the measurement infrastructure, and how the model binary gets stored, updated and version-checked. The AI team decides what to compute; I'm accountable for running that computation on this hardware in time, within the power budget, and safely. The most valuable thing I give them is numbers, not opinions.

**꼬리질문**: (a) "그럼 모델 정확도가 낮으면 누구 책임인가?" → 먼저 내 구간을 데이터로 배제한다. 프레임 드롭 0, 특징 추출이 golden 벡터와 일치, 마이크 게인이 calibration 범위 안임을 보이면 그다음이 모델 문제다. (b) "AI 팀이 요구를 계속 늘리면?" → 예산표의 추세 열을 보여 준다. 지금은 되지만 이 속도면 언제 깨지는지를 숫자로 말한다.

---

### Q02. We give you a model that needs a 600 KB tensor arena but you only have 350 KB of free SRAM. What do you do?

**왜 묻나**: 거절이 아니라 협상을 할 줄 아는지. 대안의 우선순위를 비용 순으로 세울 수 있는지.

**30초 답변**: 먼저 600KB가 측정값인지 추정값인지 확인한다. `AllocateTensors()` 후 실사용을 재면 보통 요구치보다 작다. 그다음 내 쪽 절감(op resolver 축소, pre-roll 버퍼 축소, 버퍼 공유, 스택 실측 후 조이기)을 모아 얼마를 만들 수 있는지 숫자로 낸다. 그래도 부족하면 그쪽 선택지를 준다. 가장 큰 activation 레이어의 채널 축소, 입력 윈도우 단축, 더 강한 양자화, 또는 작은 1차 모델을 MCU에 두고 큰 모델은 SoC로 보내는 cascade. 각 선택지의 절감량과 위험을 같이 적어서 결정은 그쪽이 하게 한다.

**English answer**: First I'd check whether 600 KB is measured or estimated — the actual arena usage after AllocateTensors is often smaller than the number people quote. Then I'd list what I can free on my side: narrowing the op resolver, shrinking the pre-roll audio buffer, sharing scratch buffers, and tightening task stacks after measuring high-water marks. Say that gets me 60 KB. I'd come back with a table: here is the gap, and here are your options ranked by cost — reduce the widest activation layer, shorten the input window, go to a more aggressive quantization, or split into a small first-stage model on the MCU and the full model on the SoC. I don't say no; I give them the numbers and let them pick the trade-off.

**꼬리질문**: (a) "arena는 왜 못 줄이나?" → arena 크기는 그래프의 activation 생존 구간이 결정한다. 펌웨어가 바꿀 수 있는 건 배치 위치지 크기가 아니다. (b) "외부 PSRAM을 쓰면?" → 용량은 해결되지만 레이턴시와 전력이 크게 나빠진다. always-on 경로에는 부적합할 가능성이 높고, 측정 후 판단한다.

---

### Q03. How do you measure inference latency on an MCU, and what number do you report?

**왜 묻나**: 측정 방법론. "평균"만 말하면 감점.

**30초 답변**: DWT cycle counter로 구간을 재서 평균과 최댓값을 모두 기록하고, GPIO 토글 + 로직 분석기로 실제 벽시계 시간과 지터를 확인한다. 리포트하는 숫자는 평균이 아니라 p99와 최댓값이다. 프레임 드롭은 항상 최악값에서 나기 때문이다. 그리고 반드시 조건을 같이 적는다. 클럭 주파수, 캐시 설정, 컴파일러 최적화 수준, 다른 task가 동시에 도는 상태인지.

**English answer**: I use the DWT cycle counter around the Invoke call to get cycles, and I convert with the actual core clock. But I never report just the mean — I report p99 and the max, because frame drops happen at the tail, not at the average. I cross-check with a GPIO toggle on a logic analyzer, which also shows jitter from preemption. And I always state the conditions with the number: clock frequency, cache configuration, optimization level, and whether the radio stack was active at the same time. A latency number without those conditions is not reproducible.

**꼬리질문**: (a) "Cortex-M0+에는 DWT CYCCNT가 없다면?" → 하드웨어 타이머를 최대 분해능으로 쓰거나 GPIO 토글로 대체한다. (b) "프로파일러 오버헤드는?" → op별 프로파일러는 자체 오버헤드가 있으므로 총 시간은 프로파일러를 끄고 따로 잰다.

---

### Q04. Walk me through the audio pipeline from microphone to model input.

**왜 묻나**: I2S/PDM과 DMA를 실제로 아는지. Don의 갭(I2S/오디오)이 직접 찔리는 질문이다.

**30초 답변**: 디지털 마이크는 PDM 또는 I2S로 비트스트림을 보낸다. PDM이면 주변장치나 소프트웨어 decimation 필터로 PCM을 만든다. PCM은 DMA가 ping-pong 버퍼에 채우고, half/full 인터럽트에서 ISR은 버퍼 포인터만 큐에 넣고 즉시 나온다. 오디오 task가 그 프레임을 받아 pre-emphasis, 윈도우, FFT, mel filterbank, log를 거쳐 특징 프레임을 만들고 슬라이딩 윈도우에 넣는다. 윈도우가 차면 입력 텐서를 구성해 추론 task를 깨운다.

**English answer**: A digital MEMS mic sends either PDM or I2S. With PDM, a hardware decimator or a software filter converts the one-bit stream to PCM at, say, 16 kHz 16-bit. DMA fills a ping-pong buffer, and on the half and full transfer interrupts the ISR does nothing but hand the buffer pointer to a queue. An audio task picks it up and runs feature extraction — pre-emphasis, windowing, FFT, mel filterbank, log — producing one feature frame per hop. Those frames go into a sliding window, and when the window is full the inference task builds the input tensor and runs. The rule I hold to is that the ISR never does work that can grow; if it does, you lose frames and you'll never reproduce the bug.

**꼬리질문**: (a) "ping-pong이 왜 필요한가?" → DMA가 한쪽을 채우는 동안 CPU가 다른 쪽을 처리해야 데이터 손실이 없다. (b) "캐시가 있는 코어라면?" → DMA 버퍼를 non-cacheable로 두거나 invalidate를 정확히 해야 한다. (c) "프레임을 놓쳤는지 어떻게 아나?" → 드롭 카운터를 두고 telemetry로 올린다. <확인 필요: Don이 SSD FW에서 DMA ping-pong 구조를 직접 구현했는지 — 있으면 이 답에 붙이면 매우 강해진다>

---

### Q05. The model gets 95% accuracy in Python but 88% on the device. How do you debug it?

**왜 묻나**: 협업 경계에서 가장 흔한 실제 분쟁. 체계적으로 좁히는지 본다.

**30초 답변**: 모델을 의심하기 전에 내 구간을 하나씩 배제한다. 첫째 프레임 드롭 카운터가 0인지. 둘째 특징 추출 C 구현이 Python과 같은 값을 내는지 golden 벡터로 확인한다. 셋째 입력 텐서의 quantization scale과 zero-point가 모델 파일의 값과 일치하는지. 넷째 마이크 게인과 감도가 calibration 범위 안인지. 다섯째 실제 녹음 환경이 학습 데이터와 다른지 — 기기 마이크로 녹음한 오디오를 PC에서 돌려 보면 모델 문제와 경로 문제가 갈린다.

**English answer**: I rule out my side first, in order. One: is the frame drop counter zero? Two: does my C feature extraction match the Python reference bit-for-bit on golden vectors — that's the single most common cause, usually a window function, a normalization constant or a log base. Three: do the input quantization scale and zero point match what's in the model file? Four: is the microphone gain within the calibrated range? Five: I take audio recorded through the actual device mic and replay it through the Python model. If Python also drops to 88% on that audio, it's a data and acoustics problem, not a firmware problem. That replay test settles the argument in one afternoon instead of a week.

**꼬리질문**: (a) "golden 벡터는 어떻게 만드나?" → AI 팀이 WAV와 기대 특징·출력을 주고, 펌웨어 CI가 호스트 빌드로 매 커밋 검증한다. (b) "bit-exact가 불가능하면?" → 허용 오차를 수치로 합의하고, 오차가 정확도에 미치는 영향을 실험으로 확인한다.

---

### Q06. Where should model weights live — flash or SRAM?

**왜 묻나**: 메모리 계층과 trade-off 이해.

**30초 답변**: 기본은 flash에 두고 XIP로 읽는다. SRAM을 아끼고, OTA로 모델만 갈아 끼우기도 쉽다. 다만 flash 읽기 레이턴시와 wait state 때문에 추론이 느려질 수 있으니, 레이턴시가 빠듯하면 가장 자주 쓰이는 레이어의 가중치만 SRAM이나 TCM으로 올리는 절충을 쓴다. 결정은 측정 후에 한다. 클럭, wait state, 캐시 유무에 따라 차이가 몇 배씩 달라진다.

**English answer**: Default is flash, executed in place. It keeps SRAM for activations and it makes model-only OTA much simpler. The cost is read latency — flash wait states at high core clocks can dominate a convolution inner loop. So if the latency budget is tight, I measure both and consider a hybrid: keep most weights in flash but copy the hottest layers into SRAM or TCM at init. On a part with a cache, the picture changes again, so I don't guess; I run the same model both ways and compare cycles and average current.

**꼬리질문**: (a) "SRAM으로 복사하면 전력은?" → 부팅 시 복사 비용은 한 번이지만 SRAM retention 전력이 상시 든다. always-on이면 이게 커질 수 있다. (b) "외부 flash라면?" → QSPI XIP는 훨씬 느리고, 캐시 없으면 실용적이지 않을 수 있다.

---

### Q07. How do you schedule inference alongside audio capture, BLE and the rest of the system?

**왜 묻나**: RTOS 실무. 실시간성 개념.

**30초 답변**: 오디오 캡처가 가장 높고 추론은 그보다 낮다. 오디오 프레임은 잃으면 복구 불가지만 추론 결과는 한 프레임 늦어도 된다. 무선 스택은 벤더가 요구하는 우선순위를 지킨다. 추론은 도중에 선점돼도 문제없게 입력을 미리 복사해 두고, 절대 ISR에서 돌리지 않는다. 그리고 real-time factor를 0.6 같은 상한으로 정해 두고 초과 횟수를 카운터로 센다.

**English answer**: Audio capture sits above inference. Losing an audio frame is unrecoverable; a late inference result is not. The radio stack keeps whatever priority the vendor requires — I don't fight that. Inference runs in its own task, never in an ISR, and I copy or transfer ownership of the input tensor before starting so preemption is harmless. I also set a real-time factor ceiling, say 0.6 of the frame period, and count every time we exceed it. That counter goes into telemetry, so a regression shows up as a number rather than as "sometimes it doesn't hear me."

**꼬리질문**: (a) "추론 중에 BLE 연결 이벤트가 오면?" → 무선 스택이 선점하고, 추론은 그만큼 늘어난다. 그래서 p99를 무선 활성 상태에서 측정해야 한다. (b) "추론이 프레임 주기를 넘으면?" → 프레임을 건너뛰는 정책(decimation)을 명시적으로 정의한다. 조용히 밀리게 두지 않는다.

---

### Q08. What is INT8 quantization, and what does firmware actually need to know about it?

**왜 묻나**: ML 용어를 펌웨어 관점으로 번역할 수 있는지. 고정소수점 경험과 연결되는지.

**30초 답변**: 실수 값을 `real = scale * (q - zero_point)` 형태의 8비트 정수로 표현하는 것이다. 펌웨어가 알아야 할 것은 세 가지다. 첫째 입력 텐서에 넣을 때 같은 scale/zero_point로 변환해야 한다. 둘째 int8 커널은 MCU에서 float보다 훨씬 빠르고 가중치가 1/4이다. 셋째 중간 누산은 int32로 하고 다시 8비트로 줄이는 requantization이 들어가므로, 곱셈 시프트 구현이 정확도에 영향을 준다. 사실상 임베디드에서 늘 쓰던 fixed-point 산술이고, 스케일이 레이어마다 다르다는 점만 추가된다.

**English answer**: It's fixed-point with a per-tensor or per-channel scale: real equals scale times quantized minus zero point. From the firmware side I care about three things. First, I must convert my feature values into the input tensor using exactly the scale and zero point recorded in the model file — getting that wrong silently destroys accuracy. Second, int8 kernels are much faster on Cortex-M and the weights are a quarter the size of float, which is why we do it at all. Third, accumulation happens in int32 and is requantized back to int8, so the multiplier-and-shift implementation matters. Honestly it's the same fixed-point arithmetic I've used in embedded code for years, just with a different scale per tensor.

**꼬리질문**: (a) "per-tensor와 per-channel 차이는?" → 가중치에 채널마다 다른 scale을 쓰면 정확도가 올라간다. 커널이 지원하는지 확인 필요. (b) "INT4는?" → 더 작지만 커널·툴 지원과 정확도를 확인해야 한다. Hark의 On-Device AI 공고에 INT4가 언급되어 있다 [확인됨][context 2.2].

---

### Q09. How would you decide what runs on the always-on MCU versus the application SoC?

**왜 묻나**: Hark의 추정 구조(SoC + MCU)를 직접 겨냥한 시스템 설계 질문.

**30초 답변**: 기준은 전력이다. 항상 켜져 있어야 하는 것만 MCU에 둔다. MCU는 마이크 캡처, 특징 추출, 작고 보수적인 1차 웨이크워드 모델, 전원 상태 관리를 맡는다. 1차가 트리거되면 pre-roll 오디오와 함께 SoC를 깨우고, 큰 2차 모델이 확인한다. 1차의 목표는 정확도가 아니라 false reject를 낮게 유지하면서 SoC를 자주 깨우지 않는 것이다. false accept율이 곧 배터리 수명이다.

**English answer**: The dividing line is power. Only what must be always on goes on the MCU: microphone capture, feature extraction, a small conservative first-stage detector, and the power state machine. When the first stage fires, the MCU wakes the SoC and hands over a pre-roll buffer so the second-stage model sees the whole utterance. The first stage is tuned for low false rejects, and its false accept rate is effectively a battery-life parameter — every false accept costs a SoC wake. I'd want that trade-off expressed as a number in the budget table, not as a vibe.

**꼬리질문**: (a) "pre-roll은 얼마나 필요한가?" → 2차 모델이 발화 시작을 보려면 트리거 이전 오디오가 필요하다. 길이는 UX와 SRAM의 거래다. (b) "SoC 웨이크 시간은?" → 전원 상태에 따라 수십~수백 ms. 이 값을 재서 end-to-end 예산에 반영한다.

---

### Q10. How do you ship a new model to devices in the field?

**왜 묻나**: J04(OTA)와의 연결. 모델을 데이터로 다루는 사고방식.

**30초 답변**: 모델을 펌웨어 이미지와 분리해 자체 슬롯에 둔다. 헤더에 model_id, schema 버전, 필요한 op 집합, 입력 shape, 해시를 넣고 부팅 때 런타임 호환성을 검사한다. 불일치면 이전 모델로 폴백한다. 모델만 바꿔도 되므로 업데이트가 훨씬 작고, 대신 펌웨어와 모델의 버전 조합 매트릭스를 관리해야 한다. 그리고 모델 교체는 정확도 회귀 위험이 있으므로 단계적 롤아웃과 필드 지표 모니터링이 필수다.

**English answer**: I keep the model in its own slot, separate from the firmware image, with a header carrying a model id, a schema version, the set of operators it needs, the input shape and a hash. At boot the runtime checks compatibility and falls back to the previous model if anything mismatches — a bad model must never brick the device. Decoupling makes updates small, but it creates a compatibility matrix between firmware versions and model versions that somebody has to own. And because a model swap can regress accuracy in ways unit tests won't catch, it goes out as a staged rollout with field metrics watched, same as any risky firmware change.

**꼬리질문**: (a) "모델도 서명해야 하나?" → 그렇다. 모델은 실행 동작을 바꾸는 입력이므로 펌웨어와 같은 신뢰 체인에 넣는다. (b) "슬롯 두 개면 flash가 부족하면?" → 모델 슬롯만 A/B로 하고 펌웨어는 단일 슬롯+recovery 같은 조합도 가능하다.

---

### Q11. The AI team says inference takes 4 ms on their desktop simulator. What do you tell them?

**왜 묻나**: 시뮬레이터와 실기의 차이를 아는지. 협업 태도.

**30초 답변**: 그 숫자는 우리 예산에 쓸 수 없다고 말한다. 실기에서는 코어 클럭, flash wait state, 캐시 유무, 커널 구현(참조 커널 vs CMSIS-NN), 컴파일러 최적화, 다른 task의 선점이 다 다르다. 대신 내가 실기에서 op별 프로파일을 내서 돌려주겠다고 한다. 그리고 앞으로는 모델을 줄 때마다 같은 보드에서 자동으로 측정되는 파이프라인을 만들자고 제안한다.

**English answer**: I'd tell them that number can't go in the budget, and then immediately offer the number that can. On the real part, the core clock, flash wait states, whether there's a cache, whether we're using reference kernels or CMSIS-NN, and preemption from the radio stack all change it — often by several times. So I'd run it on the target and give them a per-operator profile, which usually also tells them which layer to change if we need to save time. Then I'd set up CI so every model they push gets measured on a real board automatically. After that we stop arguing about which number is right.

**꼬리질문**: (a) "참조 커널과 최적화 커널 차이가 그렇게 큰가?" → Cortex-M에서 int8 conv는 최적화 커널이 수 배 빠른 경우가 흔하다(정확한 배수는 코어·모델·버전마다 다름). (b) "자동 측정 파이프라인은 어떻게?" → 보드 팜에 프로브를 물려 야간 회귀에서 arena·cycle·전류를 표로 찍는다.

---

### Q12. What's your experience with ML inference runtimes? (정직해야 하는 질문)

**왜 묻나**: Bonus Qualification이다. 없는 경험을 지어내는지 본다.

**30초 답변**: 런타임을 제품에 통합해 본 경험은 아직 없다고 분명히 말한다. 대신 인접한 경험을 댄다. SSD 펌웨어에서 SRAM 예산과 메모리 맵을 엄격히 관리했고, DMA와 데이터 경로 스케줄링, 고정소수점 산술, 성능 튜닝은 계속 해 왔다. TFLM의 arena 모델은 내가 다루던 정적 메모리 풀과 같은 종류의 문제다. 그리고 지금 keyword spotting 예제를 직접 돌려 보며 격차를 메우고 있다고 구체적으로 말한다.

**English answer**: I'll be straight with you: I haven't integrated an inference runtime into a shipping product. What I have done is the layer directly underneath it. On enterprise SSD firmware I owned SRAM budgets and memory maps where every kilobyte was accounted for, I built DMA data paths and tuned firmware performance, and I've worked in fixed-point arithmetic. A TFLite Micro tensor arena is the same class of problem as the static memory pools I've managed — you size it by measurement, you place it deliberately, and you never let it land on a stack. I'm closing the gap by running the micro_speech example and profiling it on a board, so I can talk about it from measurement rather than from reading.

**꼬리질문**: (a) "얼마나 빨리 따라올 수 있나?" → 런타임 통합 자체는 API 학습이고, 어려운 부분(메모리·스케줄링·측정·디버깅)은 이미 하던 일이다. (b) "TFLM 말고 벤더 런타임을 쓴다면?" → 원리는 같다. 그래프를 메모리에 배치하고, 커널을 고르고, 측정한다. <확인 필요: Don이 고정소수점(fixed-point) 산술을 실제 제품 코드에서 다뤘는지 — 레쥬메에는 명시 없음>

---

### Q13. How would you catch a regression where a new model quietly increases power consumption?

**왜 묻나**: 에너지 예산을 잊지 않는지. 관측성 설계.

**30초 답변**: 야간 회귀에 전류 측정을 넣는다. 보드에 전류 프로파일러를 물려 두고, 고정된 오디오 시나리오를 재생하면서 평균 전류와 추론 1회 에너지를 기록한다. 모델 커밋마다 이 값이 표에 찍히고, 임계를 넘으면 빌드에 경고를 낸다. 그리고 false accept율도 같이 본다. 정확도가 같아도 false accept가 늘면 SoC 웨이크가 늘어 실사용 전력이 나빠진다.

**English answer**: I'd put current measurement into nightly regression. A board sits on a power profiler, plays a fixed audio scenario, and we log average current and energy per inference alongside arena size and cycles. Every model commit lands a row in that table, and crossing a threshold fails the build. I'd also track the false accept rate, because a model with identical accuracy but more false accepts costs real battery through extra SoC wakes — that's a power regression that no latency or memory metric would show.

**꼬리질문**: (a) "어떤 장비로?" → Nordic PPK2나 Joulescope 같은 전류 프로파일러. GPIO 토글과 동기화하면 구간별로 뗄 수 있다. (b) "시나리오는 어떻게 고정하나?" → 스피커로 같은 WAV를 재생하거나, 오디오를 파일에서 주입하는 테스트 경로를 둔다.

---

### Q14. If you could ask the AI team one question before starting, what would it be?

**왜 묻나**: 협업 감각. 무엇이 중요한지 아는지.

**30초 답변**: "이 모델을 앞으로 6개월 동안 얼마나 자주, 얼마나 크게 바꿀 계획인가?"를 묻는다. 대답이 "자주, 크게"면 나는 모델을 펌웨어에서 완전히 분리하고 op 목록에 여유를 두고 예산에 헤드룸을 남긴다. "거의 고정"이면 더 공격적으로 최적화해 메모리를 짜낼 수 있다. 이 한 질문이 아키텍처 결정 대부분을 좌우한다.

**English answer**: I'd ask how often and how much the model is going to change over the next six months. If the answer is "frequently and significantly," I design for change — model fully decoupled from firmware, a generous operator set, headroom left in the arena budget, and a compatibility check at boot. If the answer is "basically frozen after DVT," I can optimize much harder and reclaim memory. That one answer drives most of the architecture, and it's cheaper to ask it in week one than to discover it in month four.

**꼬리질문**: (a) "두 번째 질문은?" → "정확도를 어떤 조건에서 측정했는가"다. 조용한 환경 수치인지 실사용 소음 환경 수치인지에 따라 기기에서의 기대치가 다르다.

---

## 6. Don 매핑

### 6.1 현재 상태

context 3.1 매칭표에서 이 항목은 유일하게 ❌로 표시된 줄이다. 근거는 "firmware performance tuning, 성능 중심 NVMe 기능. **ML 런타임 경험 없음**". 다만 같은 표에서 이 항목은 JD의 Requirements가 아니라 Responsibilities에 있고, 대응하는 Bonus Qualification("Exposure to ML inference runtimes")은 필수가 아니다. **떨어뜨리는 항목이 아니라 차별화가 덜 되는 항목**으로 취급하면 된다.

### 6.2 전이 가능한 근거 (레쥬메 문장만 사용)

| 레쥬메 근거 (context 3.1·4.4) | 이 JD 문장과 연결되는 지점 | 면접에서 쓸 한 문장 |
|---|---|---|
| "Developed using embedded C and C++ programming on bare-metal (pre/post-silicon)" | TFLM/커널 통합은 결국 정적 메모리와 C++ 빌드 문제다 | "The runtime is just C++ on bare metal with a static arena." |
| "SoC verification … I2C, SPI, DMA, PCIe, SRAM/DRAM bring-up" | 오디오 DMA 경로, 메모리 계층 배치 | "I've owned DMA data paths and SRAM/DRAM bring-up; the audio path is the same discipline." |
| "firmware performance tuning" | 레이턴시 예산, 프로파일링 | "Performance tuning with measurement, not guesswork, is what I did on SSD firmware." |
| "sign off on hardware safety margins (reliability vs performance/power)" | 세 예산의 trade-off를 공식적으로 결정해 본 경험 | "I've signed off on margin trade-offs between reliability, performance and power." |
| "JTAG, Oscilloscope, Logic Analyzer, Power Analyzer" | GPIO 토글 + 스코프로 실제 레이턴시 측정, 전류 프로파일 | "My default way to verify timing is a GPIO toggle on a scope." |
| "NVMe telemetry 디버그 기능" (SK hynix) | 파이프라인 카운터를 telemetry로 올려 필드에서 모델 회귀를 잡는 설계 | "I've shipped telemetry features into production firmware; the AI pipeline needs the same." |
| "root-cause analysis … when a new chip meets the full HW/SW system" | "PC에서는 95%인데 기기에서 88%" 류의 경계 문제를 체계적으로 좁히는 능력 | "Most of my career is bisecting problems that live between two teams' assumptions." |

### 6.3 갭을 프레이밍하는 법

세 문장 구조로 고정해 둔다.

1. **인정**: "I haven't integrated an ML runtime into a shipping product."
2. **인접 경험으로 치환**: "What that job actually is — sizing a static memory region by measurement, placing it in the right memory, keeping a real-time data path from dropping frames, and profiling with cycle counters and a scope — is what I've been doing for years."
3. **행동 증거**: "I'm running the micro_speech example and profiling it on a board so I can talk about arena numbers from measurement."

> 3번이 비어 있으면 1·2번이 변명처럼 들린다. context 4.8 체크리스트의 "TFLite Micro keyword spotting 예제 실행"을 **면접 전에 반드시 실제로 한 번 돌려야 한다.** arena 사용량과 추론 cycle 수를 직접 본 사람과 안 본 사람은 답변의 구체성이 다르다.

### 6.4 절대 하면 안 되는 것

- 모델 학습이나 양자화를 해 봤다고 암시하지 않는다. 한 번의 꼬리질문에 무너진다.
- TFLM API를 외워서 정확한 척하지 않는다. 버전마다 다르다는 사실 자체를 아는 편이 더 신뢰를 준다.
- Apple의 구체적 제품·수치·내부 도구 이름을 말하지 않는다. 일반화해서 말한다.

### 6.5 확인 필요 항목

- <확인 필요: SSD 펌웨어에서 DMA ping-pong / double buffering 구조를 직접 설계하거나 구현한 적이 있는지 — 있으면 Q04에 붙일 최고의 재료다>
- <확인 필요: 고정소수점(fixed-point) 산술을 제품 코드에서 다룬 적이 있는지 — 있으면 INT8 양자화 답변이 훨씬 자연스러워진다>
- <확인 필요: SRAM 예산을 팀 간에 공식적으로 배분·협상해 본 경험이 있는지(예: 펌웨어 기능 vs 버퍼) — 이 JD 문장의 핵심이 협상이다>
- <확인 필요: Apple/Solidigm에서 오디오 또는 실시간 스트리밍 데이터 경로를 다룬 적이 있는지 — 없으면 이 노트의 파이프라인 지식으로만 답해야 한다>

---

## 7. 준비 체크리스트

- [ ] TFLM `micro_speech` 예제를 호스트 또는 보드에서 실제로 빌드·실행하고 `arena_used_bytes()` 값을 기록한다
- [ ] 같은 예제에서 DWT CYCCNT로 `Invoke()` 구간 cycle을 재고, 평균과 최댓값을 표로 적는다
- [ ] 2.5절 "모델 계약" 표를 빈칸 상태로 외워서, 화이트보드에 10줄로 다시 그릴 수 있게 한다
- [ ] 3.3절 대안 사다리 9단계를 순서대로 말할 수 있게 연습한다 (비용 낮은 것부터)
- [ ] 2.4절 레이턴시 두 종류(real-time factor / end-to-end)를 구분해 30초로 설명한다
- [ ] 오디오 파이프라인 블록도(2.2절)를 종이에 3분 안에 그린다
- [ ] "PC 95% vs 기기 88%" 디버깅 순서 다섯 단계를 암기한다 (Q05)
- [ ] 6.3절 갭 프레이밍 세 문장을 영어로 소리 내어 연습한다
- [ ] 6.5절 확인 필요 항목 4개를 본인 경험에 비추어 답을 확정한다
- [ ] 역질문 준비: "이 기기에서 wake word는 MCU와 SoC 중 어디서 도나요? 예산은 이미 정해져 있나요?" (context 4.7 3번과 연결)

---

## 8. 더 읽기

- **C08 §1** 웨이크워드/speech 파이프라인 단계 정의와 시간축 — 이 노트 2.2절의 상세 버전
- **C08 §2** I2S/PDM DMA double buffering과 FreeRTOS ISR→task 코드 — Q04의 코드 근거
- **C08 §3** log-mel 특징 추출과 training/serving skew — Q05의 이론 근거
- **C08 §4** 양자화 수식과 requantization 손계산 — Q08 보강
- **C08 §5** TFLM 인터프리터 설정 코드와 흔한 함정 — 실습 전에 읽을 것
- **C08 §6** CMSIS-NN, Ethos-U, 벤더 DSP 비교 — Q11 꼬리질문 근거
- **C08 §7** 메모리 예산과 메모리 맵 예시, §7.5 예산 협상 예시 — 이 노트 3.1·3.3절의 배경
- **C08 §8** 레이턴시 예산과 DWT/GPIO 프로파일링 — Q03 보강
- **C08 §9** 캐시와 DMA coherency
- **C08 §10** SoC + MCU 분담과 깨우기 시퀀스 — Q09 보강
- **C08 §11** 펌웨어 vs ML 엔지니어 역할 경계표 — Q01의 원본 표
- **C03** I2S/PDM/TDM 주변장치와 DMA ping-pong 드라이버 작성
- **C04 §** RTOS task 우선순위, ISR→task 전달, tickless idle — Q07 보강
- **C05** 전력 물리와 측정 장비 — Q13 보강
- **S05 §5** 메모리·레이턴시 예산 (시스템 설계 45분 중 26~32분 구간)
- **S05 Q01~Q06** on-device AI 협업 문항 — 이 노트 5절과 짝으로 풀 것
- **J03** always-on 전력 예산 (에너지 예산의 상위 문맥)
- **J04** OTA — 모델 슬롯과 버전 호환성 (Q10과 연결)
- **J06** factory에서의 마이크 calibration과 KWS self-test (Q05의 마이크 게인 항목과 연결)
