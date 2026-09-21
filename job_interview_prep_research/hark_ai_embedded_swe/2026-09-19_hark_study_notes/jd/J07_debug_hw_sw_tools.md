# J07. Debug complex hardware-software interactions using logic analyzers, oscilloscopes, and JTAG

> **분류**: Responsibility 7/7 · **관련 개념 노트**: C10, C03, C09, S06
> **Don 현재 상태**: ✅ 강함 — 레쥬메에 "JTAG, Oscilloscope, Logic Analyzer, Power Analyzer", "DSOs and protocol Analyzer", "root-cause analysis of fundamental and interface level failures ... when a new chip meets the full HW/SW system"가 그대로 있다
> **이 노트를 다 읽으면**: ① 컨슈머 하드웨어 프로그램에서 버그가 접수→분류→소유자 배정→근본원인→재발방지로 흐르는 과정을 말로 설명할 수 있다 ② HW/FW/ML 중 누구의 버그인지 판정하는 근거를 증상별로 댈 수 있다 ③ 재현 안 되는 간헐 결함을 "계측을 먼저 심고 통계로 좁히는" 방식으로 공략하는 절차와 root cause 보고서 형식을 갖는다

---

## 0. 문장 뜯어보기

| 구(句) | 표면적 의미 | 채용담당자가 이 단어를 고른 이유 [추정] |
|---|---|---|
| Debug | 고치는 게 아니라 **원인을 찾는** 일 | "증상을 없앴다"가 아니라 "왜 그랬는지 안다"를 요구. 컨슈머 양산은 원인 미상으로 출하할 수 없다 |
| complex | 단일 모듈 버그가 아님 | 한 사람이 한 파일만 봐서는 안 풀리는 것. 여러 팀·여러 칩이 얽힌 것 |
| hardware-software interactions | HW와 SW의 **경계면** | 1세대 기기는 새 실리콘·새 보드·새 펌웨어가 동시에 움직인다. 버그의 다수가 "둘 다 스펙대로인데 시스템에서 안 되는" 경계면에서 난다 |
| logic analyzers | 디지털 버스의 **프로토콜 수준** 관측 | I2C/SPI/I2S/UART가 실제로 무엇을 주고받았는지 증거로 보라는 것 |
| oscilloscopes | 아날로그 **전기 수준** 관측 | 파워 램프, 글리치, 링잉, 레벨 위반처럼 디코더가 못 잡는 층 |
| JTAG | 코어 내부 상태 관측·제어 | halt/step/메모리 덤프, fault 레지스터, 트레이스. 실무에선 대부분 SWD지만 관용적으로 JTAG이라 부른다 |
| (문장 전체가 Responsibility의 마지막 항목) | 앞 6개 업무를 **떠받치는 공통 능력** | 드라이버·전력·OTA·factory 어느 항목이 깨져도 결국 이 도구로 들어간다. "이걸 못하면 나머지를 소유할 수 없다"는 뜻 |

> **표현 주의**: JD의 "JTAG"은 대부분 **SWD(2선)**를 뜻한다. 면접에서 "요즘 Cortex-M은 대부분 SWD고, JTAG은 데이지 체인이나 boundary scan이 필요할 때 쓴다"고 한 문장 덧붙이면 그 자체로 깊이 신호가 된다. 전기·프로토콜 차이는 C10 §1.

---

## 1. Hark에서 실제로 하게 될 일 (추정)

context 파일 2.2·2.7절의 구조 추정(Qualcomm SoC(Cortex-A, Android) + always-on 저전력 MCU(Ambiq급 Cortex-M, RTOS), Cellular·Wi-Fi·BT·GNSS·NFC·UWB 멀티 라디오, 마이크·센서·햅틱, 배터리)을 전제로 한다. 전부 **[추정]**이다.

### 1.1 이 기기에서 버그가 태어나는 자리 [추정]

```
        [클라우드/폰 앱]
              │ BLE / Wi-Fi
   ┌──────────┴───────────────────────────────┐
   │  Qualcomm SoC (Cortex-A, Android)        │  ← 앱, 에이전트, 큰 모델
   │   Hexagon DSP : speech 추론              │
   └───┬──────────────────────────┬───────────┘
       │ SPI/UART IPC  +  GPIO wake│
   ┌───┴──────────────────────────┴───────────┐
   │  always-on MCU (Cortex-M, RTOS)          │  ← 이 포지션의 주 무대
   │   센서 허브 · wake word · 전원 상태기    │
   └─┬────┬────┬─────┬──────┬──────┬──────────┘
     │I2C │SPI │I2S  │PDM   │GPIO  │ SPMI/I2C
   [IMU][플래시][코덱][마이크][햅틱] [PMIC/게이지]

경계면 = 버그가 사는 곳:
  ① MCU ↔ SoC IPC + wake 핸드셰이크
  ② MCU ↔ 센서/코덱 버스 (전원 시퀀스·레벨·pull-up과 얽힘)
  ③ PMIC 레일 ↔ 펌웨어의 전원 상태 전이
  ④ 라디오 동시 동작(coexistence) ↔ 오디오/추론 타이밍
  ⑤ 기구물에 넣었을 때만 달라지는 것(열, 안테나, 마이크 음향)
```

### 1.2 하루가 어떤 모습인가 [추정]

| 시간대 | 하는 일 | 쓰는 도구 |
|---|---|---|
| 오전 | 밤새 돌린 DUT 랙의 실패 로그 확인, 새 실패를 티켓으로 분류 | 자동 수집된 RTT/coredump 로그, 대시보드 |
| 오전 중 | triage 회의(FW/HW/ML/시스템 테스트). 어제 실패의 소유자 배정 | 증상 표, 재현률, 최근 변경 목록 |
| 오후 | 랩에서 재현 시도. LA + 스코프 + 디버거를 한 DUT에 동시에 걸고 트리거 정렬 | LA, DSO, J-Link/프로브, 전력 계측기 |
| 늦은 오후 | 원인 후보를 좁히는 실험 설계(온도, 전압, 보드 rev, FW 빌드 bisect) | 챔버/가변 전원, 빌드 서버 |
| 저녁 | 원인 확정 시 보고서 작성 + 재현 테스트를 회귀 스위트에 추가 | 8D/5-why 문서, CI |

### 1.3 주 단위로 반복되는 큰 덩어리 [추정]

- 새 보드 rev 입고 → bring-up 체크리스트(C10 §9) → 이번 rev에서만 나는 문제 목록화
- EVT/DVT 빌드 앞 "top bug" 회의 — 남은 결함을 출하 가능/불가로 나누는 자리. 여기서 "원인 모름"은 곧 일정 리스크로 취급된다
- factory 라인 불량(yield drop)이 랩으로 되돌아오는 흐름. 이건 J06/C09 영역이지만 실제 디버깅은 이 노트의 방법이다
- 필드/드록푸드 기기에서 올라온 crash를 재현 없이 분석 — telemetry와 coredump가 유일한 증거

### 1.4 이 역할에서 기대되는 산출물

| 산출물 | 무엇인가 | 왜 중요한가 |
|---|---|---|
| 근본원인 보고서 | 증상·격리 근거·원인·수정·재발방지 | HW rev 변경이나 출하 판단의 근거 문서가 된다 |
| 계측 인프라 | 로그/트레이스/coredump/telemetry 훅 | 다음 버그를 몇 배 빨리 잡는다. 이게 이 역할의 레버리지 |
| 재현 하네스 | 버그를 부르는 자동 스크립트/스트레스 시나리오 | 회귀 방지. 고치고 나서 "정말 사라졌나"를 증명하는 유일한 수단 |
| 랩 셋업 문서 | 프로브 포인트, 트리거 조건, 지그 | 다음 사람이 같은 캡처를 5분 안에 재현할 수 있게 |

---

## 2. 핵심 개념

### 2.1 층을 나눈다 — 디버깅의 1원칙

경계면 버그는 "누가 틀렸나"가 아니라 **"어느 층까지는 맞았나"**를 아래에서 위로 확정해 가면 풀린다.

```
층  무엇                      무엇으로 보나            "여기까지 OK"의 증거
─────────────────────────────────────────────────────────────────────────
L0  전원·클럭·리셋           DSO                      레일 순서·리플·리셋 해제 시점
L1  전기 신호 품질           DSO (+능동/차동 프로브)  VIH/VIL, 상승시간, 링잉 없음
L2  프로토콜 프레이밍        LA 디코더                ADDR+ACK, 클럭 개수, 프레임 정렬
L3  트랜잭션 의미            LA + FW 로그             "0x0F 읽었더니 WHO_AM_I=0x6B"
L4  드라이버 상태            디버거 / RTT             상태기·큐·에러 카운터
L5  RTOS 스케줄링            트레이스(SystemView 등)  누가 언제 뛰었나, 지연·우선순위
L6  애플리케이션 의미        앱 로그·telemetry        기능이 의도대로 되었나
```

규칙 세 가지.

1. **아래 층을 건너뛰지 않는다.** L4에서 몇 시간을 태우고 나서 L0의 레일 하나가 늦게 올라오는 걸 발견하는 게 가장 흔한 시간 낭비다.
2. **각 층에서 "통과"의 정의를 숫자로 적는다.** "신호 괜찮아 보임"이 아니라 "VIH 최소 1.17 V 규격에 1.62 V로 여유 있음".
3. **한 번의 캡처로 두 층을 같이 본다.** 스코프 아날로그 2채널 + LA 디지털 8채널을 같은 트리거에 묶으면 L1과 L2가 한 화면에 온다.

### 2.2 소유권 판정 — HW 버그인가, FW 버그인가, ML 버그인가

면접에서 가장 자주 나오는 질문이다. 판정 기준을 **증상 축**으로 외워 둔다.

| 관찰 | HW 쪽을 강하게 시사 | FW 쪽을 강하게 시사 |
|---|---|---|
| 보드 의존 | 특정 보드·특정 rev·특정 위치에서만 | 모든 보드에서 동일 |
| 온도/전압 의존 | 챔버 온도나 VBAT를 바꾸면 발생률이 변함 | 조건 무관하게 일정 |
| 신호 자체 | 스코프에서 레벨·상승시간·글리치 위반 | 신호는 규격 내인데 값이 틀림 |
| 타이밍 | 마진이 규격 경계에 붙어 있음(셋업/홀드) | 특정 코드 경로에서만 순서가 뒤바뀜 |
| 재현성 | 같은 코드에서 개체마다 다름 | 같은 입력이면 항상 같음 |
| 빌드 의존 | 펌웨어 버전 무관 | 특정 커밋 이후 시작(bisect로 확정) |

ML/모델 쪽으로 넘어가는 신호는 조금 다르다.

| 관찰 | 해석 |
|---|---|
| 입력 오디오 버퍼를 덤프해 재생하면 정상인데 추론 결과가 틀림 | 모델·전처리 문제 → ML 팀 |
| 같은 오디오를 PC에서 같은 모델로 돌리면 결과가 다름 | 양자화·연산 경로·메모리 정렬 등 런타임 이식 문제 → FW+ML 공동 |
| 덤프한 오디오 자체에 끊김/클릭/DC 오프셋이 있음 | I2S/PDM/DMA 경로 → FW(우리) |
| 결과는 맞는데 마감 시간을 넘김 | 스케줄링·클럭·메모리 대역 → FW(우리) |

> 이 표의 첫 행이 실무에서 제일 중요하다. **"모델 입력을 바이트 그대로 덤프해서 오프라인에서 재생/재추론할 수 있게 만들어 두는 것"**이 FW 엔지니어가 ML 팀과의 논쟁을 5분에 끝내는 장치다. C08과 S05에 파이프라인 설명이 있다.

### 2.3 관찰 가능성(observability) 스택 — 계측을 먼저 심는다

랩에서만 보이는 버그는 도구로 잡는다. 랩 밖에서 나는 버그는 **미리 심어 둔 계측**으로만 잡는다.

```
비용/침습도 ↑
  │
  │  full instruction trace (ETM/J-Trace, Trace32 offchip)   ← 침습 0, 하드웨어 필요
  │  RTOS event trace (SystemView / Tracealyzer, RTT 경유)
  │  RTT 로그                                                 ← 호출당 수 μs 미만(SEGGER 주장, 클럭·구현 의존)
  │  GPIO 토글 마커 (LA와 시간 정렬)                          ← 사실상 수 사이클
  │  플래시 flight recorder (마지막 N개 이벤트 링버퍼)
  │  coredump (fault 시 레지스터+스택 스냅샷)
  │  telemetry 카운터 (재부팅 사유, 버스 에러 수)             ← 필드에서 유일한 증거
  │  UART printf                                              ← 가장 침습적. 타이밍 버그를 지운다
  ▼
```

각각의 역할.

| 계측 | 언제 켜나 | 무엇을 잡나 |
|---|---|---|
| GPIO 마커 | 랩 상시 | ISR 진입/퇴출, DMA 완료, 상태 전이 — LA 파형 옆에 코드 시점을 붙인다 |
| RTT 로그 | 개발 빌드 상시 | printf 수준 정보를 타이밍 거의 안 건드리고 |
| RTOS 트레이스 | 타이밍·기아·우선순위 의심 시 | 누가 언제 preempt 되었나 |
| flight recorder | 양산 빌드 포함 | 리셋 직전 마지막 사건들 |
| coredump | 양산 빌드 포함 | fault 시 PC/LR/SP + 스택 일부 |
| telemetry 카운터 | 양산 빌드 포함 | fleet 단위 발생률 |

> **Don 연결**: SSD 펌웨어의 NVMe telemetry와 error reporting 설계가 정확히 이 스택의 아래 두 칸이다. "필드 기기에 무엇을 남겨야 재현 없이 원인을 좁힐 수 있는가"를 이미 설계해 본 경험으로 말하면 된다.

### 2.4 계측 코드 세 가지 패턴

**패턴 1 — GPIO 마커 (LA와 코드 시점 정렬)**

```c
/* 가상의 GPIO 레지스터. 실제 SoC 헤더로 대체할 것. */
#include <stdint.h>
#define DBG_GPIO_SET   (*(volatile uint32_t *)0x50000508u)
#define DBG_GPIO_CLR   (*(volatile uint32_t *)0x5000050Cu)
#define DBG_PIN_ISR    (1u << 13)
#define DBG_PIN_DMA    (1u << 14)

static inline void dbg_mark_hi(uint32_t pin) { DBG_GPIO_SET = pin; }
static inline void dbg_mark_lo(uint32_t pin) { DBG_GPIO_CLR = pin; }

void I2S_IRQHandler(void)
{
    dbg_mark_hi(DBG_PIN_ISR);   /* LA에서 ISR 폭 = 실행 시간 */
    i2s_service();
    dbg_mark_lo(DBG_PIN_ISR);
}
```

읽는 법: LA에서 `DBG_PIN_ISR`의 **폭**은 ISR 실행 시간, **간격의 흔들림**은 지터, **누락된 펄스**는 인터럽트 유실이다. I2S BCLK/LRCLK 채널과 같은 화면에 놓으면 "몇 번째 프레임에서 놓쳤는지"가 바로 보인다.

**패턴 2 — flight recorder (리셋을 견디는 링버퍼)**

```c
#include <stdint.h>
#include <string.h>

#define FR_MAGIC   0x464C4952u   /* "FLIR" */
#define FR_SLOTS   64u

struct fr_event {
    uint32_t ts_us;      /* 자유 러닝 타이머 */
    uint16_t code;       /* 이벤트 ID */
    uint16_t arg;
};

struct fr_ring {
    uint32_t magic;
    uint32_t head;
    struct fr_event ev[FR_SLOTS];
    uint32_t crc32;
};

/* 링커 스크립트에서 NOINIT 섹션(리셋 시 지우지 않는 RAM)에 배치 */
extern struct fr_ring g_fr __attribute__((section(".noinit")));

void fr_init_if_needed(void)
{
    if (g_fr.magic != FR_MAGIC) {          /* 콜드 부트: 초기화 */
        memset(&g_fr, 0, sizeof(g_fr));
        g_fr.magic = FR_MAGIC;
    }
    /* 웜 리셋: 내용 그대로 두고 부팅 직후 덤프한다 */
}

void fr_log(uint16_t code, uint16_t arg, uint32_t ts_us)
{
    uint32_t i = g_fr.head % FR_SLOTS;     /* 단일 생산자 가정 */
    g_fr.ev[i].ts_us = ts_us;
    g_fr.ev[i].code  = code;
    g_fr.ev[i].arg   = arg;
    g_fr.head++;
}
```

핵심은 **`.noinit`(또는 backup/retention RAM) 배치**다. 리셋 원인이 watchdog이든 brown-out이든, 그 직전 64개 사건이 남는다. 스타트업 코드가 `.bss`처럼 0으로 밀어버리면 아무 의미가 없으니 링커 스크립트와 startup 코드를 같이 확인해야 한다(C01 참고).

**패턴 3 — 부팅 직후 리셋 사유 기록**

```c
/* 벤더마다 레지스터 이름이 다르다. 아래는 개념 예시. */
struct boot_info {
    uint32_t reset_reason;   /* POR / BOR / WDT / SOFT / PIN */
    uint32_t fault_pc;       /* coredump가 있으면 채움 */
    uint32_t uptime_before_s;
};

void boot_report(const struct boot_info *bi)
{
    /* telemetry 카운터를 증가시키고, 개발 빌드면 RTT로도 뿌린다 */
    telemetry_inc(TLM_RESET_BASE + bi->reset_reason);
}
```

필드 기기에서 "하루에 몇 대가, 어떤 사유로 리셋되는가"는 이 한 덩어리에서 나온다. 이게 없으면 OTA로 나간 펌웨어의 안정성을 **증명할 방법이 없다**.

### 2.5 간헐 결함 — 통계로 좁힌다

재현이 안 되는 게 아니라 **재현 확률이 낮은** 것이다. 숫자로 다루면 계획을 세울 수 있다.

시도당 실패 확률 `p`일 때, 한 번이라도 보려면 몇 번 돌려야 하나(95% 신뢰):

```
n = ln(0.05) / ln(1 - p)

p = 10%   →  n ≈ 29
p = 1%    →  n ≈ 299
p = 0.1%  →  n ≈ 2995
p = 0.01% →  n ≈ 29956
```

반대로 **고쳤다는 증명**에는 "rule of three"를 쓴다. `n`번 돌려 0회 실패면 실제 실패율의 95% 상한은 약 `3/n`이다.

```
1,000회 무결점  →  실패율 상한 약 0.3%
10,000회 무결점 →  실패율 상한 약 0.03%
```

> 면접에서 이 계산을 꺼내면 강하다. "원래 1%로 나던 버그를 300회 돌려 재현했고, 수정 후 3,000회를 돌려 상한 0.1%로 눌렀다"는 문장은 **검증 가능한 주장**이기 때문이다.

발생률을 인위적으로 올리는 축(stress dial)도 같이 준비한다.

| 축 | 방법 | 무엇을 흔드나 |
|---|---|---|
| 온도 | 챔버 또는 헤어드라이어/콜드 스프레이 | 타이밍 마진, 클럭 드리프트, 누설 |
| 전압 | 가변 전원으로 VBAT 스윕 | brown-out 경계, 레벨 마진 |
| 부하 | 라디오 TX + 추론 + 오디오 동시 | 전류 트랜지언트, 버스 경합, 스케줄 지연 |
| 타이밍 | 클럭 주파수 변경, 인위적 지연 삽입 | 경쟁 상태(race) |
| 개체 | 여러 DUT를 동시에 | 공정 산포에 걸린 마진 |

### 2.6 상관(correlation) — 여러 도구의 시간축을 맞춘다

가장 실무적인 기술이다. LA·스코프·펌웨어 로그는 각자의 시계로 돈다. 세 개를 한 사건으로 묶는 법.

```
                  트리거 소스 = 펌웨어가 만든 이벤트
 FW  ── 에러 감지 ──► GPIO DBG_PIN_TRIG 1회 펄스 ──┐
                  └─► RTT 로그 "ERR seq=812 t=..." │
                                                   │ (같은 순간)
 LA   ──────────────────── 이 에지에서 트리거 ─────┤ 이전 100 ms 프리트리거 저장
 DSO  ──────────────────── 같은 에지를 외부 트리거 ┘ 레일/신호 품질 저장

 → 캡처 후: RTT 로그의 seq 번호와 LA의 펄스 개수를 맞춰 정렬한다.
```

- 스코프의 **외부 트리거 입력** 또는 LA의 트리거 아웃을 서로 연결하면 두 장비가 같은 사건을 본다.
- **프리트리거(pre-trigger) 버퍼**가 핵심이다. 사건이 난 "직전"을 봐야 원인이 있다. 사건 후만 보면 결과만 본다.
- 장시간 간헐 결함은 LA의 **세그먼트/연속 캡처 모드**나, 스코프의 **mask/limit test + 저장** 기능으로 밤새 놔둔다(기종마다 지원 여부·이름 다름).

### 2.7 랩 셋업 — 실제로 자리에 있어야 하는 것

```
 [호스트 PC] ── USB ── [디버그 프로브 J-Link/CMSIS-DAP] ── 10핀 SWD ──┐
      │                                                                │
      ├── USB ── [로직 분석기] ── 플라잉 리드 ── 테스트 포인트 ────────┤
      │                                                            [DUT 보드]
      ├── LAN ── [DSO] ── 능동/패시브 프로브 + 전원 레일 프로브 ───────┤
      │                                                                │
      ├── USB ── [전력 프로파일러 PPK2/Joulescope] ── VBAT 인라인 ──────┤
      │                                                                │
      └── USB ── [프로그래머블 전원 / USB 릴레이] ── 전원 사이클 자동화 ┘

 옆에: 온도 챔버 또는 핫에어, 납땜 스테이션, 0Ω 점퍼·테스트 와이어,
       보드 rev별 라벨링, 그리고 "이 DUT는 무엇을 위한 것인지" 태그
```

랩 규율 — 면접에서 언급하면 경험자로 읽히는 것들.

- 한 DUT에 **한 가지 개조만** 한다. 개조 이력을 보드에 직접 라벨로 붙인다.
- 그라운드 스프링을 쓴다. 긴 그라운드 리드의 인덕턴스가 만든 링잉을 "신호 문제"로 오진하는 사고가 흔하다(C10 §7.2).
- 캡처는 **파일로 저장하고 티켓에 첨부**한다. 화면 사진은 다음 사람이 커서를 못 움직인다.
- "정상 동작" 캡처를 **먼저** 떠 둔다. 비교 대상이 없으면 이상한 파형도 정상으로 보인다. 이게 golden waveform이다.

---

## 3. 실무 패턴과 함정

### 3.1 triage 흐름 — 티켓 하나가 지나가는 길

```
 [증상 접수]  랩 실패 / DUT 랙 야간 실패 / factory 불량 / 드록푸드 crash
      │
      ▼
 [재현 조건 확정]  어떤 빌드, 어떤 보드 rev, 몇 %로 나는가, 언제부터인가
      │            ← 여기서 절반의 티켓이 "설정 실수"로 닫힌다
      ▼
 [층 나누기]  L0~L6 중 어디까지 정상인가 (§2.1)
      │
      ▼
 [소유자 배정]  HW / FW / ML / 시스템 (§2.2 표를 근거로)
      │            ← 근거 없이 넘기면 티켓이 핑퐁한다
      ▼
 [가설 → 실험]  한 번에 하나의 변수. 실험 전에 "이 결과면 무엇이 참/거짓인지" 적는다
      │
      ▼
 [원인 확정]  증상을 켜고 끌 수 있으면 확정. 못 켜고 끄면 아직 상관관계다
      │
      ▼
 [수정 + 증명]  수정본으로 §2.5의 n회 무결점. 회귀 테스트 추가
      │
      ▼
 [재발 방지]  같은 부류를 막는 계측/assert/리뷰 체크리스트
```

> **"증상을 켜고 끌 수 있는가"**가 원인 확정의 유일한 기준이다. 면접에서 이 문장을 쓰면 좋다. 영어로: "I only call it root cause when I can turn the failure on and off at will."

### 3.2 HW 엔지니어와 일하는 법

1세대 하드웨어에서 FW와 HW는 매일 같은 버그를 본다. 갈등이 나는 지점과 대처.

| 상황 | 나쁜 대응 | 좋은 대응 |
|---|---|---|
| "펌웨어 문제 같은데요" | 반박부터 | 신호 캡처를 같이 본다. "이 에지에서 레벨이 VIH 아래다"처럼 **측정치**로 말한다 |
| 규격 경계에 걸린 마진 | "동작은 하니까" | 마진을 숫자로 적고 온도/전압 코너에서 다시 측정 → 데이터로 rev 변경을 제안 |
| 보드 개조로 임시 회피 | 개조본으로 계속 개발 | 개조 = 임시. **원래 보드에서도 재현되는 조건**을 문서화해 rev에 반영 |
| pull-up/스트랩 값 의심 | 데이터시트만 인용 | 실제 상승시간을 스코프로 재고 I2C 규격 tr 한계와 비교(C10 §8.3) |
| 새 실리콘 stepping 변경 | FW만 본다 | errata 문서를 먼저 읽는다. stepping ID를 부팅 로그에 남긴다 |

**FW가 HW에게 먼저 줘야 하는 것**: 테스트 포인트 요구사항, 디버그 GPIO 핀 확보, 레일별 인에이블 순서 요청, 부팅 실패 시 상태를 알리는 LED/핀, 프로브 커넥터(10핀 SWD)의 물리적 접근성. **스키매틱 리뷰 단계에서** 요구해야 한다. 보드가 나온 뒤에는 늦다.

### 3.3 도구 선택 — 증상별 첫 수

| 증상 | 첫 도구 | 이유 |
|---|---|---|
| 부팅 자체가 안 됨 | DSO (레일 + 리셋 + 클럭) | 코어가 안 돌면 디버거도 못 붙는다 |
| 디버거 연결 실패 | DSO (SWCLK/SWDIO, nRESET) + 전원 상태 확인 | 슬립·전원 게이팅·디버그 포트 잠금 가능성 |
| 센서를 못 읽음 | LA (I2C 디코드) | ADDR+NACK인지, 아예 클럭이 없는지 한 화면에 |
| 값은 오는데 이상 | 디버거/RTT | 프로토콜은 정상, 해석이 문제 |
| 오디오 클릭/끊김 | LA (I2S + DMA GPIO 마커) + RTOS 트레이스 | 프레임 정렬과 버퍼 교체 타이밍 |
| 드물게 리셋 | flight recorder + coredump + 리셋 사유 | 랩에서 못 본다 |
| 전류가 예상보다 큼 | 전력 프로파일러 + GPIO 마커 | 언제 무엇이 깨어 있는지 (C05) |
| 라디오 켜면 다른 게 깨짐 | DSO(전류 트랜지언트/레일 디프) + LA | TX 버스트의 전기적 영향 |
| 온도 올리면 실패 | 핫에어 + 해당 버스 LA/DSO | 타이밍 마진 이동 |

### 3.4 함정 표 — 디버깅 중에 스스로 만드는 사고

| 함정 | 어떻게 드러나나 | 원인 | 피하는 법 |
|---|---|---|---|
| Heisenbug: 로그를 켜면 사라짐 | UART printf 추가 후 증상 소멸 | printf가 수 ms를 먹어 경쟁 상태를 덮음 | RTT/GPIO 마커로 바꾼다. 타이밍 침습을 재서 적는다 |
| 브레이크포인트가 시스템을 깬다 | halt 후 이어가면 버스/라디오가 죽음 | 주변장치 타이머·watchdog·링크가 halt를 모름 | watchpoint+로그로 대체, 또는 디버그 halt 시 주변장치 정지 옵션 사용 |
| 프로브가 신호를 바꾼다 | 프로브 대면 정상, 떼면 실패 | 프로브 용량·LA 입력 부하가 상승시간을 바꿈 | 능동 프로브, 접점 최소화. "프로브 대면 낫다"는 것 자체가 마진 부족 증거 |
| 그라운드 리드 링잉 | 오버슈트/언더슈트가 심함 | 긴 그라운드 리드 인덕턴스 | 그라운드 스프링, 짧은 리턴 경로 |
| 트리거를 잘못 잡음 | 캡처가 늘 "정상 구간" | 사후 트리거만 봄 | 프리트리거 비율을 올린다. 에러 조건 자체를 FW가 트리거로 내보낸다 |
| 두 개 이상 동시에 바꿈 | 좋아졌는데 뭐 때문인지 모름 | 조급함 | 변수 하나. 되돌려서 다시 재현되는지 확인 |
| 상관관계를 원인으로 부름 | "이 커밋 되돌리니 사라짐"으로 종결 | 타이밍만 흔든 커밋일 수 있음 | 메커니즘을 설명할 수 있어야 원인이다 |
| 한 DUT만 본다 | 고쳤다고 했는데 라인에서 재발 | 개체 산포 | 최소 5~10대, 가능하면 다른 rev도 |
| 로그 타임스탬프가 제각각 | 이벤트 순서를 못 정함 | 각 서브시스템이 자기 시계 사용 | 공통 자유 러닝 타이머, 또는 동기 이벤트 마커 |
| coredump가 비어 있음 | fault 후 아무 정보 없음 | 스택이 이미 깨졌거나 저장 경로가 fault를 또 냄 | 별도 fault 스택(MSP 분리), 저장 루틴은 최소·무할당 |

### 3.5 root cause 보고서 형식

8D를 간소화한 8칸. 한 장을 넘기지 않는다.

```
1. 증상        누가/무엇이/얼마나 자주. 영향 범위(몇 대, 어느 rev, 어느 빌드).
2. 격리        어디까지 정상인지 (L0~L6). 어떤 실험으로 좁혔는지.
3. 근거        캡처 파일, 로그, 측정치. 파형 스크린샷 + 커서 값.
4. 근본 원인   메커니즘을 한 문단으로. "A 조건에서 B가 C보다 먼저 일어나 D가 된다."
5. 증명        원인을 넣으면 증상이 나고, 빼면 사라진다는 실험.
6. 수정        무엇을 바꿨나(FW 커밋 / HW rev / 부품값). 대안과 선택 이유.
7. 검증        n회 시도 0회 실패, 코너(온도·전압·개체) 포함. 통계 상한.
8. 재발 방지   추가한 assert/계측/테스트, 리뷰 체크리스트 항목, 유사 코드 감사 결과.
```

실무 팁: **4번을 한 문장으로 쓸 수 없으면 아직 원인을 모르는 것**이다. 이 문장이 회의에서 그대로 인용된다.

### 3.6 필드 버그 — 재현 없이 좁히기

컨슈머 기기는 출하 후가 본 게임이다. OTA로 배포된 fleet에서 나는 문제는 랩에 오지 않는다.

| 가진 것 | 여기서 뽑는 정보 |
|---|---|
| 리셋 사유 카운터 | watchdog vs brown-out vs fault — 첫 갈림길 |
| coredump의 PC/LR | 어느 함수에서 죽었나. `.elf`와 `addr2line`으로 줄 번호 |
| flight recorder | 죽기 직전 무슨 일이 있었나 |
| 배터리·온도 로그 | brown-out/열 관련성 |
| 펌웨어 버전 분포 | 특정 버전에서만? → 변경점 bisect |
| 지역/무선 환경 | 특정 통신사·특정 AP에서만? → 라디오/coex |

**단계적 롤아웃**이 사실상 가장 강력한 디버깅 도구다. 1% → 10% → 100%로 올리면서 crash rate를 보면, 원인을 모르는 상태에서도 위험을 봉쇄할 수 있다(OTA 쪽은 J04/C07).

---

## 4. 리서치 — 근거 자료

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| Arm Debug Interface Architecture Specification (ADIv5, IHI0031) | DP/AP, SWD·JTAG 트랜잭션, MEM-AP | SWD 패킷 포맷, ACK 코드, MEM-AP CSW/TAR/DRW | https://developer.arm.com/documentation/ihi0031/latest/ |
| ARMv7-M Architecture Reference Manual (DDI0403) | 예외 모델, fault 레지스터, 디버그 블록 | CFSR/HFSR/MMFAR/BFAR, DWT, ITM, FPB 장(章) | https://developer.arm.com/documentation/ddi0403/latest/ |
| Arm CoreSight Technology System Design Guide (DGI0012) | 디버그·트레이스 서브시스템 구성, 커넥터 | 디버그/트레이스 커넥터, ROM table | https://developer.arm.com/documentation/dgi0012/latest/ |
| OpenOCD User's Guide | 오픈소스 디버그 서버 전체 | Debug Adapter Configuration, GDB/Target 절, `rtos` 옵션 | https://openocd.org/doc/html/index.html |
| GDB 온라인 매뉴얼 | 브레이크/워치포인트, 원격 프로토콜 | Setting Watchpoints, Remote Debugging | https://sourceware.org/gdb/current/onlinedocs/gdb.html/ |
| SEGGER RTT 기술 문서 | RTT 제어 블록 구조, 채널, 성능 | "About Real Time Transfer", 버퍼 모드 | https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/ |
| SEGGER 지식베이스 RTT | 구현 세부, 문제 해결 | 제어 블록 탐색 실패 시 대처 | https://kb.segger.com/RTT |
| SEGGER SystemView | RTOS 이벤트 실시간 시각화 | 계측 방식(RTT 경유), 오버헤드 | https://www.segger.com/products/development-tools/systemview/ |
| Zephyr Debugging 문서 | `west debug/attach`, 백엔드 | 디버그 프로브 설정, GDB 연결 | https://docs.zephyrproject.org/latest/develop/debug/index.html |
| Zephyr Coredump | 온디바이스 coredump 백엔드와 오프라인 분석 | 백엔드(로그/플래시 파티션), `coredump_gdbserver.py` | https://docs.zephyrproject.org/latest/services/debugging/coredump.html |
| Zephyr Thread Analyzer | 스레드 스택 사용량·CPU 점유 | 활성화 Kconfig, 출력 해석 | https://docs.zephyrproject.org/latest/services/debugging/thread-analyzer.html |
| FreeRTOS Stack Overflow Checking | 스택 오버플로 감지 방식 1/2 | `configCHECK_FOR_STACK_OVERFLOW`, 훅 | https://www.freertos.org/Stacks-and-stack-overflow-checking.html |
| FreeRTOS Run Time Stats | 태스크별 CPU 시간 측정 | 타이머 설정 매크로, `vTaskGetRunTimeStats` | https://www.freertos.org/rtos-run-time-stats.html |
| NXP UM10204 (I2C-bus specification) | I2C 전기·타이밍 규격 | 모드별 최대 상승시간 tr, pull-up 계산 부록 | https://www.nxp.com/docs/en/user-guide/UM10204.pdf |
| Tektronix "ABCs of Probes" primer | 프로빙 기초, 로딩, 그라운드 | 그라운드 리드 인덕턴스와 링잉 | https://www.tek.com/en/search?q=ABCs%20of%20Probes |
| sigrok / PulseView | 오픈소스 LA 소프트웨어와 디코더 | 디코더 목록, 트리거 설정 | https://sigrok.org/wiki/PulseView |
| Saleae 지원 문서 | 상용 LA 사용법, 프로토콜 분석기 | 트리거, 아날로그+디지털 동시 캡처 | https://support.saleae.com/ |
| Memfault Interrupt — Cortex-M HardFault 디버깅 | fault 레지스터 해석 실습 | stacked frame에서 PC 복원 절차 | https://interrupt.memfault.com/blog/cortex-m-hardfault-debug |
| Memfault Interrupt — Cortex-M 디버그 인터페이스 심층 | DP/AP를 소프트웨어 관점에서 | MEM-AP로 메모리를 읽는 과정 | https://interrupt.memfault.com/blog/a-deep-dive-into-arm-cortex-m-debug-interfaces |
| Nordic Power Profiler Kit II | 전류 프로파일링 하드웨어 | 측정 범위·샘플레이트, source meter 모드 | https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2 |
| Joulescope | 고해상도 전류·전력 측정기 | 동적 레인지, 소프트웨어 연동 | https://www.joulescope.com/ |

> **버전 의존 주의**: 프로브 최대 SWO 속도, LA 샘플레이트/채널 수, 전력계의 측정 하한, Trace32 명령 세부는 **제품 세대·펌웨어·라이선스마다 다르다**. 면접에서 수치를 단정하지 말고 "기종마다 다르지만 원리는 —"으로 말한다. `<확인 필요: Don이 Apple/Solidigm에서 쓴 LA·DSO·전력계의 정확한 모델명>`

---

## 5. 예상 면접 질문

### Q01. Walk me through how you debug a hardware-software interaction bug.

**왜 묻나**: JD 문장 그 자체. 도구 이름이 아니라 **절차**가 있는 사람인지 본다. 이 답 하나로 나머지 디버깅 질문의 톤이 정해진다.

**30초 답변**: 먼저 재현 조건을 고정한다(빌드, 보드 rev, 발생률). 그다음 층을 나눠 아래에서 위로 확정한다 — 전원/클럭 → 신호 품질 → 프로토콜 → 드라이버 상태 → 스케줄링 → 애플리케이션. 각 층에서 "통과"를 숫자로 적는다. 소유권은 보드 의존·온도 의존·빌드 의존 여부로 나눈다. 원인은 증상을 켜고 끌 수 있을 때만 원인이라고 부른다.

**English answer**: "First I pin down the repro: which build, which board revision, and what the failure rate actually is. Then I work bottom-up through layers — rails and clocks on the scope, signal integrity, then protocol framing on the logic analyzer, then driver state and RTOS scheduling over the debugger or RTT. At each layer I write down what 'passing' means as a number, not as 'looks fine.' To decide whether it's hardware or firmware I look at three axes: does it track a specific board or revision, does it move with temperature or voltage, and did it start at a specific commit. And I don't call anything root cause until I can turn the failure on and off at will."

**꼬리질문**
- "What if you can't attach a debugger at all?" → 전원/클럭/리셋을 스코프로 먼저. 슬립 상태나 디버그 포트 잠금, nRESET 유지 여부를 확인한다.
- "How do you know when to stop going down and start going up?" → 해당 층에서 규격 대비 마진을 측정해 여유가 확인되면 올라간다.
- "Give me an example where the layer below fooled you." → 정상으로 보이던 레일이 특정 부하에서만 처지던 사례처럼, 부하 조건을 바꿔 재측정한 이야기.

### Q02. A sensor read fails on about 5% of EVT boards. How do you own this?

**왜 묻나**: 개체 산포가 있는 실제 EVT 상황. 통계·소유권·HW 협업이 한 번에 걸린다.

**30초 답변**: 먼저 5%가 특정 위치·특정 rev·특정 로트에 몰리는지 본다. 불량 보드와 정상 보드를 나란히 놓고 I2C를 LA로 디코드해 NACK인지 클럭 자체가 없는지 가른다. 동시에 스코프로 SDA/SCL 상승시간과 VIH 마진, 센서 전원 레일 순서를 잰다. 온도·전압을 흔들어 발생률이 움직이면 HW 마진 쪽, 안 움직이고 빌드에만 반응하면 FW 쪽이다.

**English answer**: "I'd start by checking whether the five percent clusters — same board revision, same build slot, same component lot. Then I put a failing board next to a passing one and capture the I2C transaction on the logic analyzer: address plus NACK is a very different problem from no clock at all. In parallel I scope SDA and SCL for rise time against the I2C spec limit and check the sensor's rail sequencing against the schematic. Then I sweep temperature and supply voltage. If the failure rate moves with those, it's a hardware margin issue and I take the data to the hardware team; if it only moves with firmware builds, it's mine."

**꼬리질문**
- "It's a NACK on address. Now what?" → 주소 스트랩 핀, 전원 인가 후 센서 부팅 시간, 버스 홀드 상태, 다른 디바이스와 주소 충돌.
- "Rise time is 900 ns at 400 kHz. Verdict?" → Fast mode 규격 tr 최대 300 ns 초과. pull-up이 너무 약하거나 용량이 큼 → HW 변경 근거.
- "How many boards do you need to test to trust a fix?" → §2.5의 rule of three로 목표 상한에서 역산.

### Q03. How do you decide whether a bug belongs to hardware, firmware, or the AI team?

**왜 묻나**: Hark처럼 HW·FW·모델 팀이 붙어 있는 조직에서 **핑퐁을 안 만드는 사람**인지 본다.

**30초 답변**: 증상의 의존축으로 가른다. 보드·온도·전압에 반응하면 HW, 빌드에 반응하면 FW. 모델 쪽은 입력을 바이트 그대로 덤프해서 PC에서 같은 모델로 재추론해 보면 바로 갈린다 — 덤프가 깨져 있으면 우리 데이터 경로, 덤프는 멀쩡한데 결과가 다르면 런타임/양자화, 결과도 같은데 늦으면 우리 스케줄링이다.

**English answer**: "I sort it by what the symptom depends on. If it tracks a board, a temperature or a supply voltage, it's hardware. If it tracks a commit, it's mine. For the model side I keep a path to dump the exact input buffer off the device, byte for byte. If the dumped audio itself is glitched, it's my DMA or I2S path. If the dump is clean but the on-device result differs from running the same model on a host, it's the runtime or the quantization and I bring the AI team in with that evidence. If the result is correct but late, it's my scheduling budget. I try never to hand a ticket over without that kind of evidence attached."

**꼬리질문**
- "What if hardware insists it's firmware?" → 캡처를 같이 보며 측정치로 말한다. 필요하면 FW를 최소 루프로 줄여 같은 증상이 나는지 보인다.
- "How do you keep the dump path from perturbing timing?" → 별도 버퍼 + 유휴 시 전송, 또는 RTT/대용량 저장. 덤프가 켜진 빌드와 꺼진 빌드 양쪽에서 재현률 비교.

### Q04. You have a failure that reproduces once every few hours. What's your plan?

**왜 묻나**: 간헐 결함은 시간·자원 계획 문제다. 무작정 붙잡고 있는지, 계측을 심고 병렬로 돌리는지를 본다.

**30초 답변**: 첫째, 계측을 먼저 심는다 — flight recorder, coredump, 에러 시 GPIO 트리거. 둘째, DUT를 여러 대 병렬로 돌려 시간당 시도 횟수를 늘린다. 셋째, 온도·전압·부하로 발생률을 올린다. 넷째, LA와 스코프를 프리트리거로 걸어 놓고 자동 저장한다. 고친 뒤에는 rule of three로 필요한 무결점 시도 횟수를 정해 증명한다.

**English answer**: "I stop trying to catch it live and instead make the device tell me what happened. I add a flight recorder in non-initialized RAM, a coredump on fault, and a GPIO pulse the firmware fires the moment it detects the error, wired to the logic analyzer and scope as an external trigger with a large pre-trigger buffer. Then I parallelize: several DUTs running the same stress, and I push the failure rate up with temperature, voltage and concurrent load. Once I have a fix, I decide up front how many clean runs I need — roughly three over n gives the ninety-five percent upper bound on the remaining rate — and I run that before I call it closed."

**꼬리질문**
- "What goes in the flight recorder?" → 타임스탬프, 이벤트 ID, 인자. 링버퍼 64~256칸, `.noinit` 배치, CRC.
- "How do you survive a watchdog reset with data intact?" → 리셋으로 지워지지 않는 RAM 영역 + startup이 밀지 않도록 링커/스타트업 확인.

### Q05. What do you instrument in production firmware so you can debug devices in the field?

**왜 묻나**: OTA로 운영되는 컨슈머 fleet을 아는 사람인지. 랩만 아는 엔지니어와 갈리는 질문.

**30초 답변**: 리셋 사유 카운터, fault coredump(레지스터+스택 일부), 마지막 N개 이벤트 링버퍼, 서브시스템별 에러 카운터, 배터리·온도 스냅샷, 펌웨어 버전. 용량과 개인정보 때문에 평상시엔 카운터만 올리고, 이상 시에만 덤프를 업로드한다. 그리고 단계적 롤아웃으로 crash rate를 버전별로 비교한다.

**English answer**: "Reset reason, a coredump with the core registers and a slice of the faulting stack, a small ring of the last events before the reset, per-subsystem error counters, and a battery and temperature snapshot, all tagged with the firmware version. Day to day I only ship counters, because bandwidth and privacy matter on a consumer device; the full dump goes up only when something actually faulted. Then staged rollout does the rest — if crash rate per thousand device-hours moves between one percent and ten percent of the fleet, I can stop the rollout before I even know the root cause."

**꼬리질문**
- "How big is your coredump?" → 레지스터 + 현재 스택 수 KB. 전체 RAM 덤프는 컨슈머 기기에선 비현실적.
- "What if the fault handler itself faults?" → 별도 fault 스택, 핸들러 안에서 동적 할당·로깅 스택 호출 금지, 최소 코드.

### Q06. Tell me about the hardest hardware-software bug you've personally root-caused.

**왜 묻나**: Don의 최강 카드. 스토리의 구조(증상→가설→측정→원인→수정→재발방지)를 보는 질문.

**30초 답변**: S06의 ST1(Apple 새 무선 칩 인터페이스 장애) 구조를 그대로 쓴다. 증상의 발생 조건을 먼저 숫자로 말하고, 어떤 층에서 갈렸는지, 어떤 측정이 결정적이었는지, 그리고 수정 이후 어떻게 증명했는지 순으로.

**English answer**: (S06 §3 ST1의 영어 스토리를 그대로 사용한다. 이 노트에서는 구조만 확인한다.) "Symptom with numbers → what I ruled out and how → the one measurement that split the problem → mechanism in one sentence → the fix and who owned it → how I proved it was gone and what I added so it can't come back."

**꼬리질문**
- "How did you know it wasn't firmware?" → 빌드를 되돌려도 남았고, 보드·온도 의존이 있었다는 식의 축 근거.
- "What would you do differently?" → 계측을 더 일찍 심었어야 했다는 식의, 프로세스 개선으로 답한다.

### Q07. How do you set up a capture that correlates the logic analyzer, the scope, and your firmware logs?

**왜 묻나**: 도구를 각각 쓰는 사람과, 한 사건으로 묶는 사람을 가른다. 실제로 해본 사람만 답한다.

**30초 답변**: 펌웨어가 에러를 감지한 순간 GPIO를 한 번 때리고 같은 순간 시퀀스 번호를 로그에 남긴다. 그 에지를 LA 트리거로 쓰고, LA 트리거 아웃(또는 같은 GPIO)을 스코프 외부 트리거로 넣는다. 프리트리거를 크게 잡아 사건 이전을 본다. 캡처 후 로그의 시퀀스 번호와 LA의 펄스 수를 맞춰 정렬한다.

**English answer**: "I make the firmware generate the trigger. When the driver detects the error condition it pulses a dedicated debug GPIO and logs a sequence number over RTT in the same place. That edge triggers the logic analyzer, and I feed the same edge into the scope's external trigger input so both instruments capture the same instant. I set a large pre-trigger window, because the cause is always before the symptom. Afterwards I line up the pulse count on the analyzer with the sequence numbers in the log, so I can say exactly which transaction the firmware was in when the signal misbehaved."

**꼬리질문**
- "What if the failure is rare and you can't sit there?" → 세그먼트/연속 캡처 저장, 스코프 마스크 테스트 저장, 야간 자동 수집.
- "What if adding the GPIO changes the timing?" → 몇 사이클 수준이라 대개 안전하지만, 켠 빌드/끈 빌드의 발생률을 비교해 확인한다.

### Q08. Your printf statements make the bug disappear. What now?

**왜 묻나**: Heisenbug 대처는 계측 감각의 리트머스다.

**30초 답변**: printf가 타이밍을 수 ms 단위로 바꿔 경쟁 상태를 덮은 것이다. 침습이 적은 계측으로 바꾼다 — GPIO 마커, RTT, 그리고 RTOS 이벤트 트레이스. 그래도 안 되면 로그를 메모리 링버퍼에만 쌓고 나중에 덤프한다. 증상이 타이밍에 민감하다는 사실 자체가 단서이므로, 그 방향(ISR 지연, 락 구간, DMA 완료 순서)을 먼저 의심한다.

**English answer**: "That's information, not an obstacle: it tells me the bug is timing-sensitive, so I should be looking at interrupt latency, critical sections or DMA completion ordering. Practically, I replace the UART printf with something cheap — a GPIO toggle I can see on the analyzer, or RTT, which writes into a RAM buffer that the probe drains over SWD instead of blocking on a UART. If I still need text, I log into a RAM ring buffer and dump it after the fact. The rule is that the instrument should cost microseconds, not milliseconds."

**꼬리질문**
- "Why is RTT cheaper?" → 코어가 RAM에 쓰기만 하고, 프로브가 백그라운드 메모리 접근으로 읽어간다. UART처럼 비트레이트에 묶이지 않는다.
- "Is RTT ever intrusive?" → 버퍼가 꽉 찰 때 블로킹 모드면 기다린다. 논블로킹(overwrite/skip) 모드를 쓰면 데이터가 빠진다.

### Q09. Board bring-up: first power-on, nothing works. What's your first hour?

**왜 묻나**: EVT 현장 그 자체. 순서가 몸에 배어 있는지.

**30초 답변**: 전원 인가 전 저항 체크(레일-GND 단락)부터. 그다음 전류 제한 걸고 인가해 레일 순서·전압·리플을 스코프로 확인. 리셋 해제 시점과 클럭 존재 확인. 그다음 디버거 연결 시도, IDCODE 읽기, halt. 여기까지 되면 최소 펌웨어(GPIO 토글)로 코어가 실제로 도는지 증명하고, 그 뒤 주변장치를 하나씩 올린다.

**English answer**: "Before power, I check the rails to ground for shorts. Then I bring it up on a current-limited supply and watch the sequencing on the scope — which rail comes up first, what the final voltages and the ripple are, and when reset releases relative to them. Next I confirm the clock source is actually oscillating. Only then do I try the debug probe: can I read the device ID over SWD and halt the core. Once I can halt, I flash the smallest possible program that toggles a GPIO, so I've proven the core executes from flash. After that, peripherals one at a time against the schematic, and I write down which test point I used for each so the next person can repeat it."

**꼬리질문**
- "Probe won't connect." → SWCLK/SWDIO 신호 유무, nRESET 상태, 코어 전원, 보호/잠금 설정, 다른 디바이스가 핀을 물고 있는지.
- "The board draws 10x expected current." → 즉시 차단, 열화상/손가락으로 발열 부품 탐색, 레일별로 분리해 좁힌다.

### Q10. How do you work with hardware engineers when the fix could be on either side?

**왜 묻나**: JD의 "work directly with the hardware team". 협업 태도와 데이터 기반 논쟁 능력.

**30초 답변**: 측정치로 말한다. "안 된다"가 아니라 "이 에지에서 VIH 대비 0.1 V 부족하고 온도 85도에서 실패율이 2%에서 12%로 올라간다". 그리고 양쪽 해법의 비용을 같이 올린다 — FW 우회(속도 낮추기, 재시도)와 HW 수정(저항 값, 레이아웃) 중 일정·리스크를 놓고 고른다. 임시 보드 개조는 임시라고 명시하고 rev에 반영되게 문서화한다.

**English answer**: "I bring numbers, not opinions — 'this edge is a hundred millivolts below VIH, and at eighty-five degrees the failure rate goes from two percent to twelve' is a conversation; 'your board is broken' is not. Then I put both fixes on the table with their costs: I can usually work around it in firmware by slowing the bus or retrying, but that spends margin and time budget, while changing a pull-up costs a board revision. We pick based on schedule and risk together. And if we bodge a board to keep going, I label it as a bodge and make sure it lands in the next revision instead of quietly becoming the baseline."

**꼬리질문**
- "When do you accept a firmware workaround?" → 메커니즘을 알고, 마진 손실을 정량화했고, 코너에서 검증됐을 때.
- "What do you ask hardware for before the board is laid out?" → 테스트 포인트, 디버그 GPIO, 프로브 접근성, 레일별 션트 저항(전류 측정용), SWD 커넥터 위치.

### Q11. How do you write up a root cause so the team can act on it?

**왜 묻나**: 시니어 신호. 고치는 것과 **조직이 다시 안 당하게 만드는 것**의 차이.

**30초 답변**: 한 장으로 여덟 칸 — 증상(숫자 포함), 격리 과정, 근거 캡처, 근본 원인 한 문단, 켜고 끄는 증명, 수정, 검증 통계, 재발 방지. 근본 원인은 반드시 메커니즘 문장이어야 하고, 재발 방지에는 assert·계측·회귀 테스트처럼 실행된 항목만 쓴다.

**English answer**: "One page, eight boxes: the symptom with numbers, how I isolated it layer by layer, the raw evidence, the mechanism in a single paragraph, the demonstration that I can switch the failure on and off, the fix and the alternatives I rejected, the verification statistics, and what I added so this class of bug can't come back. The mechanism paragraph is the one people quote in meetings, so if I can't write it in one sentence I know I'm not done. And prevention only counts if it's something that actually landed — an assert, a new counter, a regression test."

**꼬리질문**
- "What if you never find the root cause?" → 그 사실을 명시하고, 봉쇄(containment)와 감시 계측, 재현 조건을 남긴다. 미상은 미상으로 기록한다.

### Q12. How would you make this device easier to debug six months from now?

**왜 묻나**: 소유권과 레버리지. 주니어는 버그를 고치고 시니어는 **디버깅 비용을 낮춘다**.

**30초 답변**: 세 가지에 투자한다. ① 기기 자체의 계측(리셋 사유, coredump, flight recorder, 카운터)을 양산 빌드에 넣는다. ② 랩 자동화 — DUT 랙, 자동 전원 사이클, 야간 스트레스, 실패 시 자동 캡처 수집. ③ 하드웨어 요구 — 테스트 포인트, 디버그 GPIO, 레일 션트를 스키매틱 리뷰 때 확보한다. 그리고 매 버그마다 회귀 테스트를 하나씩 남긴다.

**English answer**: "Three investments. First, on-device observability that ships in production — reset reason, coredump, a small flight recorder, error counters per subsystem — because once we're in the field that's all we get. Second, lab automation: a rack of DUTs that power-cycle and run stress overnight, with logs and captures collected automatically, so a one-in-a-thousand bug becomes something we see every night instead of every month. Third, I push debuggability into the schematic review — test points, a couple of dedicated debug GPIOs, shunt resistors on the main rails — because you cannot add those after the board exists. And every bug we close leaves behind one regression test."

**꼬리질문**
- "What's the cost of all that in flash and current?" → 카운터/링버퍼는 수 KB 수준. 트레이스는 개발 빌드에만. 계측 오버헤드를 측정해 예산에 넣는다.
- "How do you convince a schedule-pressed team to spend time on it?" → 이번 분기에 디버깅에 쓴 시간을 집계해 보여준다. 대개 그 자체가 설득 자료다.

---

## 6. Don 매핑

### 6.1 레쥬메에서 그대로 쓸 수 있는 증거

| 레쥬메 근거 (context 3.1) | JD 문장과의 대응 | 면접에서 쓰는 방식 |
|---|---|---|
| "root-cause analysis of fundamental and interface level failures ... when a new chip meets the full HW/SW system" (Apple) | "Debug complex hardware-software interactions" 거의 직역 | Q01·Q06의 메인 스토리. "새 실리콘이 시스템을 만나는 지점"이라는 표현을 그대로 쓴다 |
| "JTAG, Oscilloscope, Logic Analyzer, Power Analyzer" | 문장의 도구 목록 3개 중 3개 | 도구 나열로 끝내지 말고 §3.3(증상→첫 도구) 논리로 말한다 |
| "DSOs and protocol Analyzer" (Apple) | logic analyzer / protocol decode | 프로토콜 분석기 경험은 LA 디코드와 같은 근육이다. J14 §6에서 번역법 |
| "PCIe, I2C, SPMI, RFFE" 장애 분석 | 버스 경계면 디버깅 | I2C/SPI/I2S로 옮겨도 같은 절차임을 보인다 |
| "Silicon/system bring up → NPI → MP" | EVT/DVT/PVT 단계별 디버깅 맥락 | Q09(bring-up 첫 한 시간)의 근거 |
| "SoC verification ... I2C, SPI, DMA, PCIe, SRAM/DRAM bring-up" (Solidigm) | 첫 부팅 디버깅 | FPGA pre-silicon 단계에서 트레이스·JTAG에 의존한 경험 |
| "error reporting/handling 설계", "NVMe telemetry 디버그 기능" | §2.3 관찰 가능성 스택 | Q05(필드 계측)의 핵심 증거. "재현 없이 좁히기"를 이미 해봤다 |
| "sign off on hardware safety margins (reliability vs performance/power)" | 마진 기반 HW 협업(§3.2) | Q10의 근거. "측정치로 논쟁한다"의 실제 사례 |
| "shmoo · health monitoring (SI 팀 협업)" (SK hynix) | 코너 스윕으로 마진 찾기(§2.5) | 전압·온도 축을 흔들어 발생률을 움직이는 방법론을 이미 안다 |

### 6.2 강점을 말로 다듬는 법 — 이 노트의 진짜 목적

Don의 문제는 실력이 아니라 **프레이밍**이다. 같은 경험도 아래처럼 바꾸면 급이 달라진다.

| 평범한 말 | 급이 올라가는 말 |
|---|---|
| "JTAG이랑 스코프로 디버깅했습니다" | "증상에 따라 어느 층부터 보는지 정해 놓고 들어갑니다. 전원·신호·프로토콜·드라이버·스케줄 순서로요" |
| "원인을 찾았습니다" | "원인을 넣으면 증상이 나고 빼면 사라지는 걸 보여준 다음에야 원인이라고 불렀습니다" |
| "가끔 나는 버그였습니다" | "시도당 약 1%였고, 300회면 95% 확률로 한 번은 본다는 계산으로 야간 자동화를 짰습니다" |
| "고쳤습니다" | "3,000회 무결점으로 잔여 실패율 상한을 0.1% 아래로 눌렀습니다" |
| "HW팀이랑 같이 봤습니다" | "VIH 대비 마진을 숫자로 제시하고, FW 우회와 HW 수정의 비용을 같이 올려놓고 골랐습니다" |
| "telemetry를 넣었습니다" | "재현 없이 필드 결함을 좁히려면 무엇이 기기에 남아 있어야 하는지를 기준으로 설계했습니다" |

### 6.3 갭과 프레이밍

| 갭 | 사실 | 프레이밍 |
|---|---|---|
| RTOS 이벤트 트레이스(SystemView/Tracealyzer) 실사용 | `<확인 필요: Don이 상용 RTOS 트레이스 툴을 써본 적 있는지>` | 없다면 솔직히. "SSD FW에서 자체 스케줄러의 태스크 타이밍을 로그·타이머로 추적했고, SystemView는 같은 일을 표준화한 도구로 이해한다" + 미니 프로젝트로 실사용 |
| coredump 프레임워크(Zephyr coredump, Memfault 류) | 레쥬메에 명시 없음 | "SSD FW의 error reporting과 telemetry가 기능적으로 같은 역할이었다"로 연결 후, 개념(§2.4, C10 §5)을 정확히 말한다 |
| 컨슈머 fleet 단계적 롤아웃 운영 | 엔터프라이즈 SSD는 고객 단위 배포 | "출하 이후 데이터로 판단하는 구조는 같고, 컨슈머는 규모와 속도가 다르다"로 |
| I2S/오디오 경로 디버깅 | 오디오 경험 없음(context 3.3) | "DMA ping-pong과 링버퍼는 SSD 데이터 경로에서 매일 다뤘다. I2S 프레이밍은 학습 중" — 지어내지 말 것 |

> `<확인 필요: Apple/Solidigm에서 만든 계측(로그·telemetry)이 필드/고객 현장 결함 분석에 실제로 쓰인 구체 사례가 있는지 — 있으면 Q05의 최고 답변이 된다>`
> `<확인 필요: Don이 8D/5-why 같은 공식 근본원인 보고 형식을 쓴 적이 있는지. SSD 업계는 8D가 흔하므로 있을 가능성이 높다>`

### 6.4 이 문장에서 목표로 할 인상

면접관이 끝나고 이렇게 적게 만드는 것이 목표다: "도구를 아는 사람이 아니라, **버그가 조직을 통과하는 과정을 설계해 본 사람**. HW와 데이터로 대화하고, 필드에서 재현 없이 좁히는 방법을 갖고 있다."

---

## 7. 준비 체크리스트

- [ ] §2.1 층 다이어그램(L0~L6)과 "각 층의 통과 기준을 숫자로"를 화이트보드에 3분 안에 그린다
- [ ] §2.2 HW/FW/ML 판정 표를 근거 없이 표로 재구성해 본다 (보드·온도·전압·빌드 축)
- [ ] §2.5 통계 두 줄(`n = ln(0.05)/ln(1-p)`, rule of three `3/n`)을 암산 가능한 예시와 함께 외운다
- [ ] Apple 인터페이스 장애 스토리(S06 ST1)를 §3.5의 8칸 형식으로 한 장 작성 — 숫자를 넣어서
- [ ] Q01·Q03·Q06·Q10의 English answer를 소리 내어 각 60초 안에 말한다
- [ ] nRF52840 DK 같은 보드에서 flight recorder(`.noinit` 링버퍼) + 강제 리셋 후 덤프를 실제로 구현해 본다
- [ ] 같은 보드에서 GPIO 마커 + LA 트리거 + RTT 로그 시퀀스 번호를 정렬하는 캡처를 한 번 만들어 본다
- [ ] 랩 셋업 다이어그램(§2.7)을 자기 경험 버전으로 다시 그린다 — 실제로 쓴 장비 모델명을 채운다
- [ ] "Debug라는 단어를 정의해 보라"에 답할 한 문장 준비: 증상을 켜고 끌 수 있을 때 원인이다
- [ ] 역질문 준비: "지금 가장 자주 나는 결함 유형은 무엇이고, 기기에 어떤 계측이 이미 들어가 있나요?"

---

## 8. 더 읽기

| 어디 | 무엇을 보충하나 |
|---|---|
| C10 §1 | JTAG vs SWD 전기·프로토콜 차이, 10핀 커넥터 |
| C10 §2.3 | Trace32 ↔ J-Link+GDB 명령 대응표 (Don 전용) |
| C10 §5 | HardFault 해석: CFSR/HFSR/MMFAR/BFAR, stacked PC 복원 |
| C10 §6~7 | 로직 분석기 샘플링·트리거, 스코프 대역폭·프로빙·그라운드 |
| C10 §9 | 보드 bring-up 체크리스트 (Q09의 원본) |
| C10 §10 | 흔한 고장 패턴 → 첫 확인 항목 |
| C03 | I2C/SPI/I2S 드라이버 구조 — 프로토콜 층을 읽을 때의 기준 |
| C05 | 전력 측정과 sleep 상태 — 전류 이상 디버깅 |
| C08 | 오디오·추론 파이프라인 — ML 팀과의 경계선 |
| C09 | factory 불량이 랩으로 돌아오는 흐름 |
| S06 D01~D12 | 디버깅 시나리오 12개의 구조화된 답 (이 노트의 §5와 짝) |
| S06 §3 | Don STAR 스토리 ST1~ST7 — Q06의 실제 대본 |
| S06 §6 | 디버깅 영어 표현 모음 |
| J13 | 회로도 읽기와 bring-up 협업 (HW 인터페이스 쪽) |
| J14 | 같은 주제의 **검증 관점** — 도구별 난이도별 질문, Trace32/DSO 경험을 증명으로 바꾸는 법 |
