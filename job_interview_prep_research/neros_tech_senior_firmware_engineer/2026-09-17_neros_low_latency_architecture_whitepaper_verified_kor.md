# 기술 백서 (검증판·한국어): 고처리량 무인 플랫폼을 위한 저지연 임베디드 아키텍처와 시스템 설계

> **원본**: Don의 Gemini 리서치 (`2026-09-17_neros_low_latency_architecture_whitepaper_gemini.md`, 수정하지 않고 보존) · **검토·수정**: 2026-09-17, Claude
> **영어판**: `2026-09-17_neros_low_latency_architecture_whitepaper_verified.md` — 면접에서 영어로 말할 문장은 영어판을 볼 것
> 모든 주장에 태그를 붙였다 — ✅ **Verified** (표준 사실 또는 출처 확인) · ✏️ **Modified** (수정함, 원문은 *원문:*으로 표시) · ⚠️ **Unverified** (확인 못 함, 인용 시 단서 필요) · 🧪 **Compile-tested** (코드를 `-std=c11 -Wall -Wextra -Wpedantic`로 빌드하고 ASan/UBSan에서 실행)

---

## 0. 변경 내역

| # | 위치 | 상태 | 무엇을 바꿨나 | 면접에서 왜 중요한가 |
|---|---|---|---|---|
| 1 | 3.2 링 버퍼 코드 | ✏️ Modified · 🧪 | `__asm__ volatile("dmb ish" ::: "memory")` 매크로와 `volatile` 인덱스를 **컴파일 검증한 C11 `<stdatomic.h>` 구현**으로 교체 | 원문은 컴파일되지 않는다. 링 버퍼는 **온사이트 확정 주제** |
| 2 | 3.2 / 3.1 | ✏️ Modified | `volatile`은 lock-free를 만들어주지 않는다는 점을 명시 | "volatile이면 충분한가?"의 정답은 **아니다** |
| 3 | 3.3 overwrite 정책 | ✏️ Modified · 🧪 | 생산자가 `tail`을 쓰면 SPSC 소유권이 깨짐 → drop-new + 소비자 측 건너뛰기, 또는 최신값 seqlock 슬롯으로 교체 | 단골 꼬리질문: "생산자가 덮어쓰면?" |
| 4 | 3.1 캐시 라인 패딩 | ✏️ Modified | 멀티코어 타깃에만 해당한다고 범위 제한. Cortex-M7은 단일 코어 | MCU 답변을 과하게 복잡하게 만들지 않기 |
| 5 | 1.1 FC 코어, 컴패니언 연결 | ✏️ Modified | 일반적인 FC = STM32 F4/F7/H7 (Cortex-M4/M7). MCU↔컴패니언은 보통 UART/SPI/USB/Ethernet이지 PCIe가 아님 | Cortex-R8·Xtensa는 SSD 컨트롤러 코어 (Don 배경이 섞여 들어감) |
| 6 | 2.1 지연 예산 | ✏️ Modified | 안쪽 rate 루프는 gyro + PID로 돌고, 자세 추정·EKF는 매 사이클이 아니라 더 낮은 주기로 돈다 | 실제 flight stack 구조(Betaflight 방식)를 아는지 보여줌 |
| 7 | 2.2 DMA 더블 버퍼 | ✏️ Modified | 각 반쪽은 DMA가 돌아오기 전에 처리해야 한다는 **마감 조건** 추가. 가변 길이 UART 프레임은 고정 반쪽 대신 IDLE-line + DMA 위치를 씀 | "경합 없음"에도 타이밍 계약이 있다 |
| 8 | Bandit > 250 km/h, Board A/B, 5초 플래싱 | ⚠️ Unverified | 태그 추가. Board A/B는 Don이 받은 영상 요약에서 나온 내용 | 인용할 때 "제가 본 인터뷰에서…"를 붙일 것 |
| 9 | 4. Adam 포커스 매트릭스 | ✏️ Modified · ⚠️ | ISO 26262는 Adam의 LinkedIn에 **없음** → "추정"으로 이동. 확인된 키워드(IPC 프로토콜, EOL 테스트, 플랫폼 재사용, OTA/PKI, BSP+CI) 추가 | 면접관에 대해 단정하지 않기 |
| 10 | 4.1 #2 토킹 포인트 | ✏️ Modified | 원문은 "과거 대량 생산 램프에서 FW와 실리콘을 분리한 경험"을 말하라고 함 → 레쥬메에 없음. Don의 실제 멀티 아키텍처 bring-up 경험으로 재구성 | 지어낸 경험은 Director 딥다이브에서 무너진다 |
| 11 | 5. FreeRTOS heap | ✏️ Modified | `heap_4`가 "필수"는 아님. 비행 핵심 코드는 정적 할당(`xTaskCreateStatic`, `heap_1` 또는 heap 없음)을 선호 | 결정성에 대한 정확한 답 |
| 12 | 5. 직렬화 | ✏️ Modified | Protocol Buffers는 MCU에서 heap 없는 런타임(예: nanopb)이 필요. FlatBuffers는 할당 없이 읽기 가능 | "MCU에서 protobuf"를 조건 없이 말하지 않기 |
| 13 | 표현 | ✏️ Modified | "1 clock cycle" → 나눗셈을 없애 실행 시간이 일정해짐. "cache trashing" → thrashing | 정확성 |

---

## 1. Neros 타깃 하드웨어·펌웨어 아키텍처 분석

전파 방해가 있는 환경에서 운용하는 소모형 무인기(UAS)는 기존의 일체형 항공 전자 구조와 다른 설계가 필요하다. Neros의 **Archer**(FPV 공격) ✅, **Archer AI**(종말 유도, GPS 없이 위치 유지) ✅, **Bandit**(Class 2/3 위협을 잡는 대드론 요격기) ✅는 저지연, 높은 기계적 스트레스, 강한 전자기 간섭(EMI), 빠른 대량 생산에 맞춘 아키텍처를 요구한다.
- Bandit 속도 "> 250 km/h" ⚠️ **Unverified** — 영상 요약에만 있고 Neros 보도자료에는 없다.

```
+-----------------------------------------------------------------------------------+
|                               시스템 아키텍처                                      |
+-----------------------------------------------------------------------------------+

 [비전 AI / 페이로드]            [RF 모듈 / 지상 통신]              [GNSS / 자력계]
  (컴패니언 SoC + NPU,            (C2 + 영상 라디오,                (GNSS: UART,
   embedded Linux)                 Neros 자체 설계)                  기압계·자력계: I2C/SPI)
          │                                  │                               │
          │ UART / SPI / USB / Ethernet      │ UART (예: CRSF) / SPI         │
          ▼                                  ▼                               ▼
+───────────────────────────────────────────────────────────────────────────────────+
|                         FLIGHT CONTROLLER (FC)                                    |
|  - 실시간 MCU: 보통 STM32 F4/F7/H7 (Cortex-M4/M7)                                 |
|  - IMU는 SPI 연결. 안쪽 rate 루프(gyro → 필터 → PID → mixer)를 1–8 kHz로          |
|  - 자세 추정(상보 필터 / EKF)은 더 낮은 주기                                       |
|  - BSP / HAL, 스케줄러, failsafe, blackbox 로깅                                   |
+───────────────────────────────────────────────────────────────────────────────────+
          │                                  │
          │ DShot (양방향 포함)              │ ADC / GPIO / I2C
          ▼                                  ▼
   [4-in-1 ESC + BLDC 모터]           [전원·안전 관리]
   (RPM 텔레메트리를 FC로 되돌림)     (전류·전압 센싱, arming / safe-arm)
```
✏️ **Modified** — *원문:* FCC 코어 "Cortex-M7 / Cortex-R8 / Xtensa", 컴패니언 연결 "PCIe / USB / High-Speed UART", EKF가 1–8 kHz 안쪽 루프 안에 있음.

### 1.1 서브시스템 분해와 연결 구조

- **Flight Controller (FC):** 결정적인 실시간 마이크로컨트롤러가 중심이며, 보통 **STM32 F4/F7/H7 (Cortex-M4/M7, 약 168–480 MHz)**이다. **안쪽 rate 루프**(gyro → 필터 → PID → mixer)는 **1–8 kHz**로 돌고, 자세 추정(상보 필터 또는 EKF)은 보통 더 낮은 주기로 돈다. ✏️ **Modified** — *원문:* "Cortex-M7 400–600 MHz 또는 Cortex-R 계열 … EKF + PID를 1–8 kHz로". Cortex-R8과 Xtensa는 스토리지·모뎀 컨트롤러 코어이지 일반적인 비행 제어기가 아니다.
- **라디오와 재밍 대응 트랜시버:** UART(예: ExpressLRS 계열 링크의 CRSF, 420 kbaud) 또는 SPI로 연결한다. 결정적인 프레이밍, CRC, 링크 품질 모니터링으로 재밍 상황에서 출력과 모드를 조절한다. ✅ **Verified** (일반적인 방식). Neros는 C2·영상 라디오를 자체 설계한다 ✅ **Verified** (neros.tech). 원문의 속도 "921.6 kbps–3 Mbps"는 Neros에 대해 ⚠️ **Unverified**.
- **ESC (Electronic Speed Controller):** DMA로 구동하는 타이머 PWM으로 DShot300/600을 만들어 BLDC 모터를 돌린다. **양방향 DShot**은 eRPM을 돌려주고 이 값으로 RPM 노치 필터를 건다. ✅ **Verified**. CAN/CAN-FD ESC는 대형 기체에는 있지만 소형 FPV에는 드물다. ✏️ **Modified** (범위 설명 추가).
- **컴패니언 컴퓨트 / 비전 (Archer AI):** 가속기가 달린 embedded Linux SoC가 종말 유도를 위한 표적 추적을 돌린다. MCU↔컴패니언 연결은 **보통 UART, SPI, USB, Ethernet**이고, 마이크로컨트롤러 FC와 PCIe로 연결하는 경우는 드물다. ✏️ **Modified**. 표적 근처에서 RF 링크가 약해지면 조종 권한이 조종사 링크에서 비전 유도로 결정적으로 넘어가야 한다. ✅ **Verified** (Neros의 Archer AI 설명과 일치).

### 1.2 멀티 실리콘 이식성과 "Board A / Board B" 이중화

Neros는 **2028년까지 연 100만 대**를 목표로 한다 ✅ **Verified** (Series C 보도자료). 이 규모에서는 핵심 부품의 이중 소싱이 현실적인 요구다. 다만 "Board A / Board B"라는 구체적 관행은 ⚠️ **Unverified** — Don이 받은 CTO 인터뷰 요약에서 나온 내용이다.

- **HAL / 드라이버 격리** ✅ **Verified** (표준 관행): 제어 코드와 상태 머신은 레지스터 주소를 직접 만지지 않고, 드라이버가 공통 인터페이스를 제공한다.

```c
typedef struct { int32_t x, y, z; } axis3_t;

typedef struct {
    int (*init)(void);
    int (*read_accel)(axis3_t *data);
    int (*read_gyro)(axis3_t *data);
} imu_driver_t;
```
✏️ **Modified** — 코드만으로 완결되도록 `axis3_t` 정의를 추가.

- **빌드 타임 보드 설정** ✅ **Verified** (표준 관행): 핀 맵, 버스 할당, 드라이버 선택을 보드 설정 데이터와 빌드 플래그에서 가져온다. 그래서 MCU 계열을 바꿔도(예: STM32 → NXP) 제어 코드는 건드리지 않는다.
- **보드 간 회귀 검증** ✅ **Verified** (표준 관행): 캡처한 버스 트레이스를 재생하거나 두 보드에 같은 HIL 테스트를 돌려 동작이 같음을 증명한다. *표현 수정:* 원문의 "bit-for-bit validation nodes"는 흔히 쓰는 용어가 아니다.

### 1.3 양산을 고려한 펌웨어와 실리콘 bring-up

- **라인 내 플래싱과 공장 테스트 모드** ✅ **Verified** (표준 관행): 스테이션에서 MCU와 외부 QSPI를 플래싱하고, SWD/JTAG 또는 보조 UART로 공장 테스트 모드를 연다. "5초 이내" 목표는 ⚠️ **Unverified** (예시 수치).
- **자동 EOL(End-of-Line) 테스트** ✅ **Verified** (표준 관행): 전원 레일·션트 측정, 오실레이터 확인, SPI/I2C 루프백, 게이트 드라이버 도통, 센서 존재 확인과 캘리브레이션. *용어 수정:* "Factory Acceptance Test (FAT)"는 보통 공장 설비를 인수할 때 쓰는 말이다. 여기서는 **EOL / 기능 테스트(FCT)**가 정확하다.
- **현장 블랙박스 로깅** ✅ **Verified** (Betaflight blackbox가 대표 사례): gyro, 모터 출력, 버스 오류, 브라운아웃·리셋 원인을 SPI 플래시에 순환 기록한다.

---

## 2. 저지연·결정적 데이터 파이프라인 아키텍처

급기동 중에도 센서→액추에이터 지연을 낮고 예측 가능하게 유지하려면, IMU 샘플에서 모터 명령까지의 경로가 결정적이고 불필요한 복사가 없어야 한다. ✏️ **Modified** — *원문:* "밀리초 미만 … (>250 km/h)". 속도 수치는 미검증이고, 목표 자체는 그 수치 없이도 성립한다.

### 2.1 지연 예산 분해 (예시)

```
τ_total = τ_sensor + τ_bus + τ_ingest + τ_rate_loop + τ_dispatch
```

| 구간 | 일반적 시간 | 결정성 | 대응 | 상태 |
|---|---|---|---|---|
| **센서 ODR·내부 필터** | 125–250 µs | 하드웨어에 묶임 | 고 ODR gyro(≥ 8 kHz), 칩 내부 DLPF 최소화 | ✅ Verified (일반값) |
| **SPI 전송 (버스트 읽기)** | 20–50 µs | 지터 발생 쉬움 | SPI ≥ 10–20 MHz, 버스트 읽기, DMA | ✅ Verified (일반값) |
| **수집·버퍼링** | 5–15 µs | 락 위험 | SPSC 링 버퍼(§3), 핫 패스에서 복사 피하기 | ✅ Verified (일반값) |
| **안쪽 rate 루프** (필터 + PID + mixer) | 수십~수백 µs 미만 | 상한이 보장돼야 함 | 고정소수점 또는 FPU, 동적 할당 금지, 필터 체인 길이 제한 | ✏️ Modified |
| **자세 추정** (상보 필터 / EKF) | 더 낮은 주기로 실행 | 가변 | 안쪽 루프와 분리, FPU, 사전 계산 | ✏️ Modified |
| **액추에이터 출력 (DShot600)** | 16비트 프레임 ≈ 26.7 µs | 하드웨어에 묶임 | DMA → 타이머 비교 레지스터, 비트마다 CPU 개입 없음 | ✅ Verified |

✏️ **Modified** — *원문:* "State Estimation (EKF) 100–300 µs"를 매 사이클 합계에 넣고 "Cache line pre-alignment"를 대응책으로 제시. 비행 제어기의 rate 루프는 매 사이클 gyro 값을 직접 쓰며, 안쪽 루프마다 EKF 전체를 돌리는 구조는 일반적이지 않다. 또 Cortex-M4에서 "캐시 라인"은 의미 있는 최적화 수단이 아니다.

### 2.2 인터럽트 수집 vs. 원형 DMA 더블 버퍼링

수 kHz로 바이트·샘플마다 인터럽트를 걸면 컨텍스트 전환 비용과 지터가 커지고, 캐시가 있는 코어에서는 cache thrashing도 생긴다. ✏️ **Modified** — *원문:* "cache trashing".

```
       원형 DMA Ping-Pong 버퍼 구조
   ┌──────────────────────┬──────────────────────┐
   │    반쪽 버퍼 0        │    반쪽 버퍼 1        │
   │    (바이트 0..N-1)    │    (바이트 N..2N-1)   │
   └──────────────────────┴──────────────────────┘
              ▲                      ▲
              │                      │
       DMA Half-Transfer      DMA Transfer-Complete
        인터럽트 (HT)             인터럽트 (TC)
              │                      │
              ▼                      ▼
    [버퍼 0 처리]            [버퍼 1 처리]
    (DMA가 1을 채우는 동안)  (DMA가 0을 채우는 동안)
```

1. **원형 DMA, 버퍼 크기 2N** ✅ **Verified** (STM32 HAL/LL 패턴).
2. **Half-Transfer (HT):** DMA가 반쪽 1을 쓰는 동안 반쪽 0을 처리한다. ✅ **Verified**
3. **Transfer-Complete (TC):** DMA가 반쪽 0으로 돌아가는 동안 반쪽 1을 처리한다. ✅ **Verified**
4. **DMA와 소프트웨어 사이에 락이 필요 없다** ✅ **Verified** — *단, 타이밍 계약이 있다:* 각 반쪽은 **DMA가 그 자리로 돌아오기 전에** 처리하거나 복사해 둬야 한다. 아니면 데이터가 조용히 덮어써진다. ✏️ **Modified** (계약 추가).
5. **가변 길이 UART 프레임**(라디오·텔레메트리): 고정된 반쪽이 프레임 경계와 맞지 않는다. **원형 DMA + UART IDLE-line 인터럽트**를 쓰고, DMA 쓰기 위치(`NDTR`)를 읽어 새로 들어온 바이트만 링 버퍼에 넣는다. ✏️ **Modified** (추가).

---

## 3. Lock-Free SPSC 링 버퍼 설계

ISR과 태스크 사이(또는 AMP 구성의 두 코어 사이)에서는 ISR 안에서 mutex를 잡을 수 없다. 단일 생산자·단일 소비자(SPSC) 링 버퍼는 락도, 대기도 없이 데이터를 넘긴다. ✅ **Verified**

### 3.1 설계 원칙

- **2의 거듭제곱 용량** ✅ **Verified**: `%` 대신 `& (CAPACITY - 1)`로 인덱싱해서 핫 패스의 나눗셈을 없애고 실행 시간을 일정하게 만든다. ✏️ **Modified** — *원문:* "인덱스 계산을 1 클럭 사이클로 줄인다".
- **자유 증가 인덱스** ✅ **Verified**: head와 tail은 계속 증가만 한다. 부호 없는 `head - tail`이 채워진 양이고, 배열에 접근할 때만 마스크를 씌운다. 2의 거듭제곱 용량은 2^32를 나누어떨어지게 하므로 정확하다.
- **소유권 규칙 — lock-free가 성립하는 이유** ✏️ **Modified** (명시): **생산자는 `head`만, 소비자는 `tail`만 쓴다.** 서로 상대 인덱스를 읽기만 하고 쓰지 않는다.
- **공개 순서** ✅ **Verified**: 데이터를 먼저 쓰고 인덱스를 나중에 올린다. 그래야 상대가 아직 안 쓴 데이터를 가리키는 인덱스를 보는 일이 없다.
- **`volatile`만으로는 부족하다** ✏️ **Modified** (추가): `volatile`은 컴파일러가 값을 캐싱하지 못하게 할 뿐, **원자성이나 코어 간 메모리 순서를 보장하지 않는다.** 단일 코어 ISR↔main 루프라면 일반적인 MCU에서 쓰기 순서만 지켜도 충분하다. 멀티코어라면 아래처럼 C11 atomics를 쓴다.
- **캐시 라인 패딩** ✏️ **Modified**: **캐시가 있는 멀티코어**(예: Cortex-A, 이기종 SoC)에서 생산자 코어와 소비자 코어 사이 false sharing을 피할 때만 의미가 있다. 단일 코어 Cortex-M4/M7에는 필요 없다. *원문:* "dual Cortex-R or M-core"를 포함한 일반 규칙으로 서술.

### 3.2 임베디드 C 구현 🧪 Compile-tested

✏️ **Modified** — 원문 코드를 교체했다. 원문은 컴파일되지 않고(`__asm__ volatile("dmb ish" ::: "memory")`는 C가 아님), lock-free를 `volatile`에 의존했다.

```c
#ifndef SPSC_RING_H
#define SPSC_RING_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#define RING_CAPACITY 512u               /* 반드시 2의 거듭제곱 */
#define RING_MASK     (RING_CAPACITY - 1u)

/* lock-free가 성립하는 소유권 규칙:
 *   생산자 (예: UART RX ISR) 는 head 만 쓴다
 *   소비자 (예: main loop)   는 tail 만 쓴다
 * 인덱스는 감싸지 않고 계속 증가한다. RING_CAPACITY 가 2^32 를 나누어떨어지게 하므로
 * 부호 없는 뺄셈이 곧 채워진 양이다.                                            */
typedef struct {
    uint8_t buf[RING_CAPACITY];
    atomic_uint_fast32_t head;           /* 생산자만 쓴다 */
    atomic_uint_fast32_t tail;           /* 소비자만 쓴다 */
    uint32_t dropped;                    /* 생산자 소유 오버플로 카운터 */
} spsc_ring_t;

static inline void ring_init(spsc_ring_t *r) {
    atomic_init(&r->head, 0u);
    atomic_init(&r->tail, 0u);
    r->dropped = 0u;
}

/* 생산자 측. drop-new 정책: tail 은 절대 건드리지 않는다. */
static inline bool ring_push(spsc_ring_t *r, uint8_t byte) {
    uint_fast32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);  /* 내 인덱스 */
    uint_fast32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);  /* 상대 인덱스 */
    if ((uint32_t)(h - t) >= RING_CAPACITY) {
        r->dropped++;
        return false;
    }
    r->buf[h & RING_MASK] = byte;                                             /* 1) 데이터 먼저 */
    atomic_store_explicit(&r->head, h + 1u, memory_order_release);            /* 2) 그다음 공개 */
    return true;
}

/* 소비자 측. */
static inline bool ring_pop(spsc_ring_t *r, uint8_t *out) {
    uint_fast32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint_fast32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (h == t) return false;
    *out = r->buf[t & RING_MASK];                                             /* 1) 먼저 읽고 */
    atomic_store_explicit(&r->tail, t + 1u, memory_order_release);            /* 2) 그다음 소비 */
    return true;
}

static inline uint32_t ring_count(spsc_ring_t *r) {
    return (uint32_t)(atomic_load_explicit(&r->head, memory_order_acquire) -
                      atomic_load_explicit(&r->tail, memory_order_acquire));
}

#endif /* SPSC_RING_H */
```
🧪 테스트 항목: 빈 버퍼 pop, 용량까지 채우기, 가득 찼을 때 drop(`dropped == 1`), FIFO 순서, 인덱스 wrap을 넘는 push/pop 5,000회 — `-Wall -Wextra -Wpedantic -O2` 경고 없음, ASan/UBSan 통과.

**면접 팁:** 단일 코어 MCU에서 ISR이 생산자인 경우, 같은 코드를 평범한 `uint32_t` 인덱스로 먼저 보여주고 **왜 쓰기 순서만으로 충분한지** 설명한 뒤, 멀티코어라면 atomics가 필요하다고 덧붙인다. 이 순서로 말하면 "무엇"이 아니라 "왜"를 이해하고 있다는 게 드러난다.

### 3.3 오버플로 정책

- **Drop-new (텔레메트리, 명령, 로그)** ✅ **Verified**: 가득 차면 생산자가 새 항목을 버리고 `dropped`를 올린다. `tail`은 건드리지 않는다.
- **생산자가 `tail`을 옮겨 "가장 오래된 것 덮어쓰기"** ✏️ **Modified** — *원문:* 권장(`tail = head - CAPACITY + 1`). **SPSC 소유권 규칙을 깬다.** 소비자도 `tail`을 쓰기 때문에 생산자와 소비자가 같은 변수를 두고 경합한다.
- **최신값만 중요할 때 (IMU, 오도메트리)** — 올바른 방법 두 가지:
  1. **소비자 측 건너뛰기** ✅ **Verified**: 생산자는 drop-new를 유지하고, 소비자가 쌓인 것을 모두 꺼낸 뒤 마지막 것만 쓴다.
  2. **최신값 슬롯 (seqlock)** 🧪 **Compile-tested**: 슬롯 하나와 시퀀스 카운터. 쓰는 쪽은 절대 막히지 않고, 읽는 쪽은 쓰기와 겹쳤으면 다시 읽는다.

```c
typedef struct { int32_t gx, gy, gz; uint32_t t_us; } imu_sample_t;

typedef struct {
    atomic_uint_fast32_t seq;            /* 홀수 = 쓰는 중 */
    imu_sample_t v;
} latest_imu_t;

static inline void latest_write(latest_imu_t *s, const imu_sample_t *in) {   /* 쓰는 쪽은 하나 */
    uint_fast32_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 1u, memory_order_release);   /* 홀수: 쓰는 중 */
    s->v = *in;
    atomic_store_explicit(&s->seq, q + 2u, memory_order_release);   /* 짝수: 안정 */
}

static inline void latest_read(latest_imu_t *s, imu_sample_t *out) {
    uint_fast32_t a, b;
    do {
        a = atomic_load_explicit(&s->seq, memory_order_acquire);
        *out = s->v;
        b = atomic_load_explicit(&s->seq, memory_order_acquire);
    } while ((a & 1u) || a != b);
}
```
주의: 단일 코어에서 읽는 쪽이 쓰는 쪽보다 **우선순위가 낮으면** 재시도로 해결된다. 반대로 읽는 쪽이 쓰기 도중에 선점할 수 있으면(읽는 쪽이 더 높은 우선순위 ISR) 재시도가 끝나지 않을 수 있으니, 그때는 잠깐 쓰는 쪽 인터럽트를 막는다.

---

## 4. Director 인터뷰를 위한 아키텍처 대화 전략

Adam Kibit은 자동차 임베디드 경력이 긴 Director of Firmware다. 드론 공기역학보다 확장성, 결정성, 양산, 수명주기 신뢰성에 초점을 맞춘다. ✅ **Verified** (LinkedIn 경력 페이지)

```
       ADAM KIBIT — 포커스 매트릭스
       ┌────────────────────────────────────────────────────────────┐
       │ 경력에서 확인됨 (✅):                                        │
       │ - 프로세서 간 통신(IPC) 프로토콜 설계 (JCI)                  │
       │ - EOL 테스트 앱·자동 테스트 시스템 (JCI)                     │
       │ - 제품 등급 간 재사용되는 모듈형 플랫폼 (Visteon)            │
       │ - OTA 파이프라인, 보안 업데이트, PKI (Aeris, Visteon, FF)    │
       │ - BSP를 CI·유닛테스트에 통합 (JCI)                          │
       │ 추정, 명시되지 않음 (⚠️):                                    │
       │ - 기능안전 관점 (예: ISO 26262)                              │
       │ - EV 경험에서 오는 고전압/저전압 노이즈 통합 이슈            │
       └────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
        DON의 가치 연결 (레쥬메 기준 ✅)
       ┌────────────────────────────────────────────────────────────┐
       │ - 새 실리콘 bring-up → NPI → MP (Apple, Solidigm)            │
       │ - 인터페이스 루트코즈: PCIe, I2C, SPMI, RFFE (Apple)         │
       │ - 공장 test-node 아키텍처 (Apple)                           │
       │ - 챔버 신뢰성 테스트 플랫폼·SDK (SK hynix)                   │
       │ - 텔레메트리 기반 디버그 기능 (SK hynix)                     │
       └────────────────────────────────────────────────────────────┘
```
✏️ **Modified** — *원문:* ISO 26262를 조건 없이 Adam의 관심사로 제시. Don 칸에 레쥬메에 명시되지 않은 "Bare-Metal / RTOS Deterministic Scheduling"이 있었음.

### 4.1 핵심 대화 축

#### 1. 실시간 결정성 vs. 시스템 인터커넥트 ✅ Verified (Don 레쥬메와 부합)
- *프레임:* bare-metal 루프, RTOS, Linux 컴패니언 컴퓨터를 오가는 구조.
- *토킹 포인트:* 고속 버스는 충격·진동·온도 변화에서 신호 마진을 잃는다. Don은 오실로스코프와 프로토콜 분석기로 물리 계층 문제(지터, 프레이밍 오류)와 펌웨어 타임아웃을 구분해 왔다. 진동하는 기체에도 같은 방법이 통한다.

#### 2. 부품 교체에 강한 펌웨어 추상화 (Board A / Board B) ✏️ Modified
- *프레임:* 공급망이 흔들리는 상황에서 생산 확대.
- *토킹 포인트 (Don의 실제 경험으로 수정):* "Cortex-R, Cortex-M, Xtensa 등 서로 다른 아키텍처에서, 그리고 FPGA 프로토타입에서 양산 실리콘까지 펌웨어를 올려봤습니다. 그 과정에서 나머지 코드가 실리콘 변화를 쫓아다니지 않으려면 하드웨어 경계를 어디에 그어야 하는지 배웠습니다. 대체 부품 보드에도 같은 원칙을 적용하겠습니다. 기능 단위 HAL, 데이터로 관리하는 보드 설정, 두 보드를 같은 CI·HIL에서 검증하는 방식입니다."
- *원문:* "과거 대량 생산 램프에서 FW를 특정 실리콘 레지스터와 어떻게 분리했는지 자세히 설명하라… 아키텍처 재작성 없이" — **Don의 레쥬메에 없는 경험이니 말하지 말 것.**

#### 3. 양산 캘리브레이션과 진단 ✅ Verified (Adam과 가장 강하게 맞닿는 주제)
- *프레임:* FW 엔지니어 12명 ✅(리크루터)과, 약 300명 중 절반 이상이 operator/technician인 공장 ✅(리크루터)을 잇는 역할. ✏️ **Modified** — *원문:* "수백 명의 생산 기술자".
- *토킹 포인트:* 공장 사이클 타임이 펌웨어 설계를 결정한다. 빠른 부팅, 버스 자가 진단, 자동 캘리브레이션, 디버그 장비 없이 진단 가능한 인터페이스, 그리고 결과를 설계로 되먹이는 것. Don의 Apple test-node 경험과 Adam의 EOL 테스트 경력을 연결한다.

---

## 5. 기술 학습 로드맵

### 영역 1: FreeRTOS 내부와 동시성
- **스케줄링** ✅ **Verified**: 선점형 고정 우선순위 vs. 협력형/타임 슬라이싱, 틱 인터럽트 비용, tickless idle(`vPortSuppressTicksAndSleep`).
- **메모리 관리** ✏️ **Modified**: `heap_1`(할당만), `heap_2`(병합 없음), `heap_3`(malloc 래핑), `heap_4`(first-fit + 병합), `heap_5`(여러 영역). 비행 핵심 코드에서 선호되는 답은 **정적 할당**(`xTaskCreateStatic`, `xQueueCreateStatic`) 또는 부팅 시에만 할당하는 `heap_1`이다. `heap_4`는 단편화를 줄일 뿐 없애지 못한다. *원문:* "단편화 없는 항공 전자에는 `heap_4`가 필수".
- **우선순위 역전** ✅ **Verified**: 낮은 우선순위 태스크가 mutex를 쥐고, 중간 우선순위가 선점하고, 높은 우선순위가 기다린다. FreeRTOS mutex는 우선순위 상속을 하고, binary semaphore는 하지 않는다.
- **Task notification** ✅ **Verified**: `xTaskNotifyGive` / `ulTaskNotifyTake`(ISR용 `xTaskNotifyGiveFromISR`)는 ISR→태스크 깨우기에서 세마포어보다 가볍다.

### 영역 2: MCU 메모리 모델과 버스
- **Cortex-M7의 TCM** ✅ **Verified**: ITCM은 시간 핵심 코드, DTCM은 결정적인 데이터·스택. 주의: **STM32H7에서는 DMA가 DTCM에 접근하지 못한다** — DMA 버퍼는 AXI/AHB SRAM에 둬야 한다. 정확한 부품의 메모리 맵을 확인할 것. ✏️ **Modified** (*원문:* "DTCM은 결정적 스택/DMA 데이터용").
- **DMA와 캐시 일관성** ✅ **Verified**: DMA 송신 전 CMSIS `SCB_CleanDCache_by_Addr`, DMA 수신 후 `SCB_InvalidateDCache_by_Addr` — 또는 DMA 버퍼를 캐시되지 않는 영역에 둔다.
- **MPU** ✅ **Verified**: 스택 가드 영역, null 포인터 페이지, 특권·비특권 영역 분리. *표현 수정:* MPU는 이런 버그를 "크래시 없이" 넘기는 게 아니라, **기록하고 복구할 수 있는 MemManage fault로 바꿔준다.**

### 영역 3: 저수준 통신 프로토콜
- **SPI** ✅ **Verified**: CPOL/CPHA 모드, 칩 셀렉트 setup/hold 타이밍, 배선 정전용량 한계, 모터 전류 근처 노이즈.
- **I2C** ✅ **Verified**: 마스터 리셋 후 슬레이브가 SDA를 low로 잡아 버스가 멈추는 경우. SCL을 최대 9번 토글한 뒤 STOP을 만들어 복구한다.
- **PCIe** ✅ **Verified** (Don 배경과 관련, MCU 비행 제어기에는 드묾): Root Complex vs. Endpoint, MSI/MSI-X, 주소 변환(iATU), Lane Margining at the Receiver(PCIe 4.0+).
- **DShot** ✅ **Verified**: DShot300 비트 시간 ≈ 3.33 µs, DShot600 ≈ 1.67 µs. 16비트 프레임(11비트 스로틀 + 텔레메트리 비트 + 4비트 CRC). DMA로 타이머 비교 레지스터에 넣어 생성한다.

### 영역 4: 제어 루프와 비행 역학
- **Rate-monotonic 우선순위** ✅ **Verified**: 주기가 짧을수록 우선순위가 높다. 예시: gyro/rate 루프 125–250 µs → 라디오 2–10 ms → 텔레메트리 50 ms 이상.
- **PID 세부** ✅ **Verified**: 적분 클램핑(anti-windup), derivative kick을 피하는 측정값 미분, D항 저역 필터.
- **진동 억제** ✅ **Verified** / ⚠️ 대역: 프로펠러 진동은 모터 RPM에 따라 움직이고, 동적 노치·RPM 노치 필터가 이를 따라간다. "200–800 Hz"는 ⚠️ **예시 범위**이며 기체 크기와 RPM에 따라 다르다. 상보 필터 vs. EKF: EKF가 다중 센서 융합과 센서 고장 대응에 강한 대신 계산 비용이 크다.

### 영역 5: 엣지 IPC와 비전 인계
- **공유 메모리 링 버퍼** ✅ **Verified** — 공유 RAM이 있는 컴패니언 SoC(예: 이기종 SoC)에 해당. ✏️ **Modified** 범위: **별도 칩인** MCU와 컴패니언 컴퓨터 사이에는 공유 메모리가 없으니, COBS + CRC 같은 프레이밍을 씌운 링크(UART/SPI/Ethernet)를 쓴다.
- **직렬화** ✏️ **Modified**: FlatBuffers는 할당 없이 읽을 수 있다 ✅. MCU의 Protocol Buffers는 **nanopb** 같은 정적 할당 런타임이 필요하다 ✅. *원문:* 둘을 조건 없이 heap-free로 묶음.
- **조종 권한 FSM** ✅ **Verified** (설계 패턴): 조종사 RF 링크 → 자율 종말 유도 → failsafe(정책에 따라 호버 / disarm). 진입·이탈 조건과 타임아웃을 명시한다.

---

## 6. 출처와 교차 참조

- Neros Series C 보도자료 (Archer AI, Bandit, 2028년 연 100만 대) — https://www.prnewswire.com/news-releases/neros-raises-250m-series-c-at-2-5b-valuation-to-scale-autonomous-and-interceptor-drone-programs-302848736.html
- Neros 공식 사이트 (자체 라디오, flight computer) — https://www.neros.tech/
- Adam Kibit LinkedIn 경력 페이지 (2026-09-16 열람) — `neros_hm_adam_technical_prep_2026-09-18.md` §2에 요약
- 리크루터 콜 2026-09-16 (팀 규모, 회사 규모, 온사이트 구성) — `neros_tech_senior_firmware_engineer_context.md` §5
- 컴파일 검증 코드 — `practice/solutions/01_ring_logging.c`(SPSC), `practice/solutions/06_isr_timing.c`(재시도 스냅샷 패턴)와 같은 원리
- 아키텍처 배경 — `practice/notes/00_drone_architecture.md`
