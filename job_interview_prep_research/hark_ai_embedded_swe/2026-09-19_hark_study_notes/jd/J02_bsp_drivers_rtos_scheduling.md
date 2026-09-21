# J02. Own BSP development, peripheral driver integration (SPI, I2C, UART, I2S), and RTOS task scheduling

> **분류**: Responsibility 2/7 · **관련 개념 노트**: C03(BSP·드라이버), C04(RTOS), C01(부트·클럭), S02(드라이버·RTOS 문답)
> **Don 현재 상태**: 🟡 부분 — 버스 bring-up과 드라이버 자체는 ✅ 강함("I2C, SPI, DMA, PCIe, SRAM/DRAM bring-up", SPMI/RFFE). 갭은 두 개: **I2S/오디오**(레쥬메에 없음)와 **상용 RTOS의 task 스케줄링 실무**(bare-metal·자체 스케줄러 중심).
> **이 노트를 다 읽으면**: ① "BSP를 소유한다"가 구체적으로 어떤 파일과 어떤 결정을 소유하는 것인지 목록으로 말할 수 있다 ② 벤더 SDK 드라이버를 제품 코드베이스에 넣는 workflow와 그 과정의 함정을 설명할 수 있다 ③ always-on 기기의 task 집합·우선순위·스택을 근거를 가지고 설계할 수 있다.

---

## 0. 문장 뜯어보기

원문: `Own BSP development, peripheral driver integration (SPI, I2C, UART, I2S), and RTOS task scheduling`

| 구(句) | 표면적 의미 | 채용담당자가 이 단어를 고른 이유 (해석) |
|---|---|---|
| `Own` | 소유한다 | "돕는다(support)"나 "기여한다(contribute)"가 아니다. **단일 오너**를 뽑는다는 뜻이고, 팀이 작다는 신호다. 결정권과 함께 밤에 걸려 오는 전화도 온다 |
| `BSP development` | Board Support Package를 만든다 | 보드가 새로 나올 때마다 부팅시키는 사람. "이미 있는 BSP를 쓴다"가 아니라 **만든다** |
| `peripheral driver integration` | 드라이버 통합 | `development`가 아니라 `integration`인 게 핵심. 대부분은 벤더 SDK/Zephyr 드라이버가 이미 있고, 그걸 **우리 보드·우리 RTOS·우리 전력 정책에 맞게 붙이는** 일이 실제 업무다 |
| `(SPI, I2C, UART, I2S)` | 네 개의 버스 | SPI/I2C/UART는 모든 임베디드 제품의 기본이다. **I2S가 들어간 게 신호**다 — 마이크/스피커, 즉 음성 AI 기기라는 뜻. context 2.2의 "speech 중심 모델" 단서와 맞는다 |
| `RTOS task scheduling` | RTOS에서 task를 스케줄링한다 | "RTOS를 써 봤나"가 아니라 **"task 집합과 우선순위를 설계해 봤나"**를 묻는다. 이건 API 지식이 아니라 시스템 설계 능력이다 |
| (문장 전체) | 세 덩어리를 한 사람이 | BSP → 드라이버 → task. 이건 **아래에서 위로 쌓는 한 스택 전체**다. 즉 "보드가 안 깨어난다"부터 "오디오가 튄다"까지 전부 이 사람 책임이다 |

### J01과의 경계
J01이 "코드베이스를 어떻게 운영하나"였다면, J02는 **"그 코드베이스의 아래 두 층에서 실제로 무슨 일이 일어나나"**다. RTOS 검증 관점(면접관이 뭘로 판별하나)은 J11에서 따로 다룬다.

---

## 1. Hark에서 실제로 하게 될 일 (추정)

근거는 context 2.2/2.7절이다. 전부 `[추정]`.

### 1.1 always-on MCU에 붙어 있을 것들

```
                       ┌───────────────────────────┐
     PDM/I2S 마이크 ───▶│                           │──I2C──▶ IMU, 근접/광 센서
                       │   always-on MCU           │──I2C──▶ PMIC, fuel gauge
     햅틱 드라이버 ◀──I2C│   (Cortex-M + RTOS)       │──I2C──▶ 터치/버튼 컨트롤러
                       │                           │──SPI──▶ 외장 NOR flash (OTA 슬롯)
     버튼/GPIO ────────▶│                           │──SPI──▶ 라디오 (BLE 코프로세서 등)
                       │                           │──UART─▶ 디버그 콘솔
                       └───────┬───────────────────┘──UART─▶ SoC IPC
                               │ wake / reset / power enable
                               ▼
                      Qualcomm SoC (Cortex-A, Android)
```

JD의 네 버스가 전부 여기 있다. I2S는 마이크, I2C는 센서·PMIC·햅틱, SPI는 외장 flash·라디오, UART는 콘솔과 SoC IPC. 이 그림 하나가 J02 문장의 이유를 전부 설명한다.

### 1.2 BSP 오너의 한 주 (추정)

| 언제 | 하는 일 |
|---|---|
| EVT 보드가 처음 도착한 날 | 전원 인가 전 회로도로 레일·스트랩 확인 → 전류 제한 걸고 인가 → 레일 전압 측정 → SWD 연결 확인 → UART "hello" → GPIO 토글 |
| 그다음 며칠 | 클럭 트리 확정, I2C 스캔으로 센서 ACK 확인, SPI flash ID 읽기, LA로 파형 검증 |
| 새 부품 추가 | 데이터시트 → 회로도 배선 확인 → 벤더 드라이버 조사 → 통합 → LA 검증 → RTOS task 연결 |
| 오디오 라인이 살아날 때 | I2S 클럭 계산, DMA ping-pong, 첫 PCM 덤프를 호스트로 뽑아서 WAV로 들어 보기 |
| 상시 | "센서가 가끔 안 읽힌다" triage, 우선순위/스택 조정, CPU 부하 측정 |

### 1.3 이 역할이 소유할 산출물 (추정)

| 산출물 | 내용 |
|---|---|
| 보드 정의 | 핀맵/pinctrl, 클럭 설정, 전원 시퀀스, 보드 리비전 분기 (Zephyr라면 `<board>.dts`, `<board>_defconfig`) |
| 드라이버 계층 + 버스 설정 | 센서·PMIC·햅틱·flash 드라이버, 버스 속도와 pull-up 검증, DMA 채널·인터럽트 우선순위 배정 |
| task 설계 문서 | task 목록, 우선순위, 주기, deadline, 스택 크기, 통신 경로 |
| bring-up 체크리스트 | 새 보드를 받았을 때 따라가는 절차. 리비전마다 재사용 |

---

## 2. 핵심 개념

### 2.1 BSP는 정확히 어디까지인가

**안에 있는 것**: 핀 멀티플렉싱(pinmux/pinctrl), 클럭 트리 설정, 전원 레일 시퀀싱, 메모리 맵과 링커 스크립트, 벡터 테이블과 인터럽트 우선순위 배정, DMA 채널 할당, 보드 리비전 식별, 부트 배너, 콘솔 초기화, 워치독 초기화.

**밖에 있는 것**: 제품 로직, 센서 융합, 통신 프로토콜, 전력 정책 결정(정책은 앱, 기계적 실행은 BSP), OTA 로직.

**경계가 흐려지는 곳**: "센서 전원을 켜는 것"은 BSP인가 드라이버인가? 실무 답은 **"레일을 켜는 방법은 BSP가 알고, 언제 켜는지는 드라이버/앱이 정한다"**다. `board_sensor_power(bool on)` 같은 함수를 BSP가 제공하고, 드라이버가 호출한다.

### 2.2 Zephyr에서 BSP는 파일 몇 개다

Zephyr를 쓰면 "BSP 개발"의 결과물이 구체적인 파일 집합이 된다 (디렉터리 구조와 파일 이름은 **Zephyr 버전에 따라 다르다**. hardware model v2 도입 이후 `board.yml`이 추가됐다).

| 파일 | 역할 |
|---|---|
| `<board>.dts` | 하드웨어 서술: 어떤 SoC, 어떤 버스에 무엇이 붙었나, 핀 할당, 클럭 소스 |
| `<board>-pinctrl.dtsi` / `<board>_defconfig` | 핀 멀티플렉싱 정의 / 이 보드에서 기본으로 켜는 Kconfig 옵션 |
| `Kconfig.<board>` / `board.yml` | 보드 식별과 리비전 정의 |
| `board.cmake` | 플래싱/디버깅 러너 설정 (`west flash`가 어떤 도구를 쓸지) |
| `<board>.yaml` | twister가 쓰는 보드 메타데이터 (RAM/flash 크기, 지원 기능) |

**devicetree vs Kconfig vs 코드** — 면접에서 자주 묻는다.

| 어디에 | 무엇을 | 예 |
|---|---|---|
| devicetree | **하드웨어가 무엇이고 어떻게 연결됐나** (빌드 시 고정되는 물리적 사실) | 센서가 `i2c1`의 주소 `0x68`에 붙어 있다, 인터럽트 핀은 `P0.11` |
| Kconfig | **소프트웨어 기능을 켤까 말까** | `CONFIG_I2S=y`, `CONFIG_LOG_LEVEL_INF`, 드라이버 활성화 |
| 코드 | **정책과 로직** | 센서를 몇 Hz로 읽을지, 언제 sleep할지 |

가장 흔한 실수: 하드웨어 사실을 Kconfig에 넣는 것(`CONFIG_SENSOR_I2C_ADDR=0x68`). 보드가 두 개가 되는 순간 무너진다.

### 2.3 새 보드 bring-up 순서 (외워 둘 것)

```
 0. 전원 인가 전   회로도로 레일·스트랩·부트 모드 핀 확인. 파워 서플라이 전류 제한 설정
 1. 전원           레일을 순서대로 인가, DMM/스코프로 전압과 ramp 확인. 소비 전류가 예상 범위인가
 2. 클럭           XTAL 발진 확인(스코프 프로브는 로딩 주의), PLL lock
 3. 디버그 포트    SWD/JTAG로 코어가 잡히나. ID 코드 읽기
 4. 콘솔           UART로 문자 하나 출력 — 여기까지 오면 절반은 끝났다
 5. GPIO           LED 토글, 스트랩 읽기, board ID 감지
 6. 타이머/RTOS    tick이 도나, 두 task가 번갈아 도나
 7. 버스           I2C 스캔(누가 ACK하나), SPI flash JEDEC ID 읽기
 8. 개별 디바이스  센서 WHO_AM_I, PMIC 레지스터, 햅틱
 9. 고난도        I2S 오디오, 라디오, 고속 인터페이스
10. 전력          sleep 진입/복귀, 대기 전류 측정
```

**핵심 원칙**: 한 번에 한 층씩만 올린다. 4번(콘솔)을 건너뛰고 7번(I2C)을 디버깅하려 들면 "무엇이 실패했는지 보이지 않는" 상태로 몇 시간을 태운다.

### 2.4 클럭과 전원 시퀀스 — BSP의 두 지뢰밭

| 항목 | 왜 중요한가 | 흔한 실패 |
|---|---|---|
| 클럭 소스 선택 | 정확도(XTAL vs 내부 RC)와 전력이 트레이드오프. UART baud, I2S 샘플레이트, BLE 타이밍이 여기 걸린다 | 내부 RC로 UART를 돌리면 온도에 따라 깨진다. I2S는 분주가 안 맞아 샘플레이트가 틀어진다 |
| 클럭 전환 타이밍 | 주변장치 동작 중 소스를 바꾸면 깨진다 | DMA 전송 중 클럭 변경 → 데이터 손상 |
| 레일 인가 순서 | 부품 데이터시트가 순서와 ramp 시간을 규정한다 | 코어 전원보다 I/O 전원이 먼저 오면 latch-up 위험 |
| enable 후 대기 | 레귤레이터 안정화, 센서 부팅 시간 | 데이터시트의 `t_boot`를 안 지켜서 첫 I2C가 NACK |
| pull-up 소스 | I2C pull-up이 붙은 레일이 꺼지면 버스 전체가 죽는다 | 센서 전원을 끄면 다른 센서도 안 읽힘 |

### 2.5 네 버스 한눈에

| 항목 | SPI | I2C | UART | I2S |
|---|---|---|---|---|
| 선 개수 / 클럭 | 4+ (SCLK, MOSI, MISO, CS), master 동기 | 2 (SDA, SCL), master 동기 | 2 (TX, RX) + RTS/CTS, 비동기 baud 합의 | 3 (BCLK, WS, SD), 연속 비트 클럭 |
| 주소 / 속도 | CS 라인 선택, 수십 MHz | 7비트 주소, 100k~5M (모드별) | 점대점, ~수 Mbps | WS로 채널 구분, fs × 비트 × 채널 |
| 전기 | push-pull | open-drain + pull-up | push-pull | push-pull |
| 전형적 용도 | flash, 디스플레이, 라디오 | 센서, PMIC, EEPROM | 콘솔, 모듈 통신, IPC | 마이크, 코덱, 스피커 |
| 대표 함정 | mode(CPOL/CPHA) 불일치, CS 타이밍 | pull-up 값, 주소 충돌, 버스 hang | baud 오차, 플로 컨트롤 없음 | 클럭 분주 오차, 버퍼 언더런 |

I2C 속도 모드는 NXP `UM10204` 기준이다: Standard-mode 최대 100 kbit/s, Fast-mode 400 kbit/s, Fast-mode Plus 1 Mbit/s, High-speed mode 3.4 Mbit/s, Ultra Fast-mode 5 Mbit/s(단방향, push-pull).

### 2.6 SPI — mode와 CS가 전부다

```
  CPOL=0, CPHA=0 (mode 0) — 가장 흔하다
  SCLK  ___┌─┐_┌─┐_┌─┐_      idle low, 상승 엣지에서 샘플
  MOSI  ══╳═══╳═══╳═══╳══    하강 엣지에서 출력 변경
  CS    ‾‾‾\_______________/  전송 동안 low
```

| mode | CPOL | CPHA | 샘플 엣지 |
|---|---|---|---|
| 0 | 0 | 0 | 상승 |
| 1 | 0 | 1 | 하강 |
| 2 | 1 | 0 | 하강 |
| 3 | 1 | 1 | 상승 |

- **mode 불일치의 증상**: 읽은 값이 1비트 시프트되어 있거나, 전부 0/0xFF다. LA로 SCLK idle 레벨과 첫 데이터 엣지를 보면 5분 안에 판별된다.
- **CS 타이밍**: 많은 칩이 CS를 워드마다 토글해야 하고, 어떤 칩은 전체 트랜잭션 동안 유지해야 한다. 하드웨어 CS 자동 제어가 데이터시트 요구와 다르면 GPIO로 직접 제어한다.
- **다중 슬레이브**: MISO가 공유되므로 선택되지 않은 칩은 MISO를 고임피던스로 놔야 한다. 그렇지 않은 칩이 섞이면 버퍼가 필요하다.

### 2.7 I2C — open-drain이 모든 것을 설명한다

SDA/SCL은 누구도 high로 구동하지 않는다. low로 당기거나(구동) 놓거나(해제) 둘 뿐이고, high는 pull-up 저항이 만든다. 여기서 세 가지가 따라온다.

1. **pull-up 값이 속도를 결정한다.** 작을수록 rise time이 짧아 빠르지만 전류를 더 먹는다. UM10204는 버스 커패시턴스(최대 400 pF)와 rise time 규격을 준다. 실무에서는 스코프로 rise time을 직접 본다.
2. **clock stretching이 가능하다.** slave가 SCL을 low로 잡아 master를 기다리게 할 수 있다. 이걸 지원하지 않는 master IP가 있으면 특정 센서가 간헐적으로 실패한다.
3. **버스가 hang될 수 있다.** slave가 바이트 중간에 리셋되면 SDA를 low로 잡은 채 멈춘다. MCU만 리셋해서는 안 풀린다.

**버스 복구(bus clear)** — UM10204가 규정하는 절차: master가 SCL을 최대 9번 토글해 slave가 남은 비트를 흘려보내게 하고, SDA가 high로 풀리면 STOP 조건을 만든다.

```c
/* I2C 버스 복구 — 핀을 GPIO로 되돌려서 수동으로 흔든다.
   gpio_* 함수는 BSP가 제공하는 것으로 가정.                 */
void i2c_bus_recover(void)
{
    gpio_configure_output_od(PIN_SCL);   /* open-drain 출력으로 */
    gpio_configure_input(PIN_SDA);

    for (int i = 0; i < 9; i++) {
        gpio_set(PIN_SCL, 0);
        delay_us(5);                     /* 100 kHz 기준 half period */
        gpio_set(PIN_SCL, 1);
        delay_us(5);
        if (gpio_read(PIN_SDA)) break;   /* SDA가 풀렸으면 중단 */
    }

    /* STOP 조건: SCL high인 상태에서 SDA low → high */
    gpio_configure_output_od(PIN_SDA);
    gpio_set(PIN_SDA, 0); delay_us(5);
    gpio_set(PIN_SCL, 1); delay_us(5);
    gpio_set(PIN_SDA, 1); delay_us(5);

    pinctrl_restore_i2c();               /* 핀을 다시 I2C 주변장치로 */
}
```

Zephyr에는 이걸 표준 API로 제공하는 `i2c_recover_bus()`가 있다(드라이버가 지원해야 한다).

**BSP 오너가 할 설계**: 복구 루틴을 "있으면 좋은 것"이 아니라 **드라이버 에러 경로의 정식 단계**로 넣고, 복구 횟수를 텔레메트리로 센다. 필드에서 어느 보드가 병들었는지 이걸로 안다.

### 2.8 UART — 단순해 보이지만 always-on에서 까다롭다

```
  8N1 프레임, 문자 'A' (0x41 = 0b0100_0001), LSB first
  idle ‾‾‾\_____/‾‾‾\___________/‾‾‾‾‾‾‾‾‾‾‾‾\______/‾‾‾‾‾‾
          start  1  0  0  0  0  0  1  0       stop
```

- **baud 오차**: 클럭 분주로 만들다 보니 정확히 안 맞는다. 일반적으로 송수신 합쳐 2% 이내면 동작하고 3%를 넘으면 위험하다. 내부 RC 클럭 + 온도 변화가 겹치면 여기서 깨진다.
- **수신 누락**: 폴링이나 바이트 인터럽트로 받으면 고속에서 놓친다. DMA + idle-line 감지(수신이 멈추면 인터럽트)가 표준 해법이다. Zephyr의 async UART API(`uart_callback_set`, `uart_rx_enable`)가 이 패턴을 제공한다.
- **저전력 충돌**: UART가 활성이면 특정 클럭을 못 끄고, deep sleep에 못 들어간다. 디버그 콘솔이 대기 전류 측정을 망치는 대표적 원인이다.
- **IPC로 쓸 때**: 프레이밍(길이 + CRC)과 재동기화 규칙이 필요하다. 전원이 끊긴 반대편에서 반쪽 패킷이 날아오는 상황을 처리해야 한다.

### 2.9 I2S — Don에게 새로운 부분

**신호 세 개**: BCLK(비트 클럭), WS 또는 LRCLK(워드 선택, 좌/우 채널 구분), SD(직렬 데이터). 마스터가 BCLK와 WS를 만든다.

```
  Philips I2S, 16-bit stereo — 데이터는 WS 전환 후 1 BCLK 늦게 시작, MSB first
  WS   ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\________________/‾‾‾‾‾‾‾
          (right)        |   (left)      |
  BCLK ┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌┐┌
  SD   ═╳═══╳═══╳═══╳═══╳═══╳═══╳═══╳═══╳
        MSB ............ LSB MSB ....... LSB
```

**클럭 계산** — 외워 둘 것:

```
  BCLK = sample_rate × bits_per_channel × channels
  예: 16 kHz, 16-bit, stereo → 16000 × 16 × 2 = 512 kHz
      48 kHz, 24-bit(32-bit 슬롯), stereo → 48000 × 32 × 2 = 3.072 MHz
  WS(LRCLK) = sample_rate
  MCLK(코덱이 요구하면) = 보통 sample_rate의 256배 또는 384배
```

**포맷 변형**

| 포맷 | 차이 |
|---|---|
| Philips I2S | WS 전환 후 1 BCLK 지연, WS low = left |
| Left-justified | 지연 없이 WS 전환과 동시에 MSB |
| TDM / PDM | TDM은 WS가 프레임 동기가 되고 한 프레임에 채널 여러 개(마이크 4개 이상). PDM은 1비트 고속 스트림이라 MCU가 decimation 필터로 PCM을 만든다 |

**always-on 기기에서의 핵심**: 오디오는 멈추면 안 되는 스트림이다. 그래서 **DMA ping-pong(double buffering)**이 필수다. DMA가 버퍼 A를 채우는 동안 CPU가 버퍼 B를 처리하고, 절반/전체 완료 인터럽트에서 교체한다. 이 구조를 못 지키면 "가끔 오디오가 튄다"는, 재현이 어렵고 사용자가 바로 알아차리는 버그가 된다.

```c
/* I2S DMA ping-pong — ISR은 신호만 보내고 처리는 task가 한다 */
#define FRAME_SAMPLES 160                       /* 16 kHz에서 10 ms */
static int16_t buf[2][FRAME_SAMPLES];
static volatile uint8_t dma_active;             /* DMA가 지금 쓰는 버퍼 */

/* DMA 완료 ISR — 여기서 절대 필터를 돌리지 않는다 */
void i2s_dma_complete_isr(void)
{
    BaseType_t woken = pdFALSE;
    uint8_t done = dma_active;
    dma_active ^= 1u;                           /* 다음 버퍼로 교체 */
    /* 포인터만 큐에 넣는다. 복사도 하지 않는다. */
    int16_t *p = buf[done];
    xQueueSendFromISR(audio_q, &p, &woken);
    portYIELD_FROM_ISR(woken);
}

void audio_task(void *arg)
{
    int16_t *frame;
    for (;;) {
        if (xQueueReceive(audio_q, &frame, portMAX_DELAY) == pdTRUE) {
            dc_remove(frame, FRAME_SAMPLES);
            feature_extract(frame, FRAME_SAMPLES);   /* wake word 전단 */
        }
    }
}
```

**이 코드에서 면접관이 보는 것**: ① ISR이 짧은가 ② 데이터를 복사하지 않고 포인터만 넘기는가 ③ task가 한 프레임을 처리하는 시간이 한 프레임 주기(여기선 10 ms)보다 짧은가 — 아니면 언더런이 난다. ③을 스스로 언급하면 점수가 크게 올라간다.

### 2.10 드라이버 "통합" 워크플로

새 부품이 회로도에 추가됐을 때 실제 순서:

```
 1. 데이터시트    인터페이스, 주소, 레지스터 맵, 부팅 시간, 전원 요구, 최대 클럭
 2. 회로도        어느 버스, 어느 CS/주소, 인터럽트 핀 어디, pull-up 있나, 어느 레일
 3. 드라이버 조사  Zephyr in-tree 드라이버 / 벤더 SDK / 제조사 예제 / 직접 작성
 4. 최소 통합      WHO_AM_I 또는 ID 레지스터 읽기 — 이것만 되면 배선과 버스는 정상
 5. 파형 검증      LA로 실제 트랜잭션 확인. ACK, 타이밍, 클럭 주파수
 6. 기능 구현      설정 레지스터, 인터럽트 모드, FIFO
 7. RTOS 통합      어느 task가 읽나, ISR에서 뭘 하나, 얼마나 자주
 8. 전력 통합      sleep 진입 시 이 칩은 어떤 상태가 되나, 누가 깨우나
 9. 실패 처리      타임아웃, 재시도, 버스 복구, 에러 카운터
```

**4번(WHO_AM_I)이 가장 중요한 단계**다. 이게 되면 전원·핀맵·버스 설정·주소가 전부 맞다는 뜻이고, 안 되면 그 넷 중 하나다. 기능 구현으로 바로 뛰어들면 무엇이 틀렸는지 알 수 없다.

**벤더 드라이버를 쓸 때의 판단**

| 상황 | 선택 |
|---|---|
| Zephyr in-tree 드라이버가 use case를 커버 | 그대로 쓴다. devicetree 노드만 추가 |
| 벤더 SDK 드라이버가 우리 RTOS/HAL과 안 맞음 | 얇은 shim으로 감싼다. 벤더 코드는 수정하지 않는다 |
| 블로킹 전제인데 비동기가 필요 / `malloc`·`printf`를 쓴다 | 감싸기보다 다시 쓰는 게 빠를 수 있다. 저전력 경로에서 터지는 것들은 반드시 걸러낸다 |

**벤더 드라이버 통합 함정 체크리스트**: 블로킹 delay를 쓰는가(`for` 루프 delay는 RTOS에서 CPU를 낭비한다) / 전역 상태를 쓰는가(인스턴스 두 개를 못 만든다) / 재진입 가능한가 / 인터럽트 컨텍스트에서 호출해도 되는가 / 에러를 반환하는가 아니면 무한 대기하는가 / 필요한 지연을 데이터시트대로 지키는가.

### 2.11 polling / interrupt / DMA — 무엇을 언제

| 방식 | 적합한 경우 | CPU 비용 | 함정 |
|---|---|---|---|
| polling | 초기 bring-up, 아주 짧은 트랜잭션, 부팅 중 | 대기 시간 전부 | 타임아웃 없으면 hang. 저전력과 상극 |
| interrupt (바이트/워드마다) | 중저속, 이벤트가 드문 경우 | 인터럽트마다 수십~수백 사이클 | 고속에서 인터럽트 폭주 |
| DMA | 블록 전송, 오디오 스트림, 대량 flash 읽기 | 거의 0 | 캐시 일관성, 버퍼 정렬, 완료 처리 누락 |

**감을 잡는 계산**: 16 kHz 스테레오 16-bit 오디오는 초당 32000 샘플, 샘플마다 인터럽트를 받으면 초당 32000번이다. ISR 진입/탈출에 100 사이클만 잡아도 3.2 MCycle/s — 64 MHz MCU의 5%를 인터럽트 오버헤드로만 쓴다. DMA로 10 ms 블록을 받으면 초당 100번으로 떨어진다. **이 계산을 면접에서 해 보이면 설득력이 크다.**

### 2.12 always-on 기기의 task 집합 설계

먼저 원칙 세 가지.

1. **task는 "동시에 일어나야 하는 일"의 단위**이지 "기능"의 단위가 아니다. 순차적으로만 일어나는 두 기능은 한 task로 묶는다. task가 많을수록 스택과 컨텍스트 스위치 비용이 는다.
2. **우선순위는 급한 정도(deadline)로 정하지 중요도로 정하지 않는다.** "배터리 관리가 중요하니까 최고 우선순위"는 틀렸다. 배터리는 1초 늦어도 되고 오디오는 10 ms 늦으면 깨진다.
3. **모든 블로킹은 타임아웃을 가진다.** 무한 대기는 워치독이 물게 되고, 그때는 이미 원인을 잃는다.

전형적인 설계 예 (숫자는 예시, FreeRTOS 기준으로 숫자가 클수록 높은 우선순위):

| task | 우선순위 | 주기/트리거 | deadline | 스택 | 하는 일 |
|---|---|---|---|---|---|
| `audio` | 6 (최고) | 10 ms 프레임 (DMA 완료) | 10 ms | 2~4 KB | I2S 프레임 전처리, wake word 전단 |
| `sensor` | 4 | 20~50 ms 또는 인터럽트 | 50 ms | 1~2 KB | IMU/근접 센서 읽기, 이벤트 생성 |
| `ipc` | 4 | SoC UART 수신 | 수십 ms | 1~2 KB | SoC와의 메시지 송수신 |
| `power` | 3 | 100 ms | 수백 ms | 1 KB | PMIC/fuel gauge 폴링, 상태 머신, sleep 결정 |
| `ota` | 2 | 이벤트 | 초 단위 | 2 KB | 이미지 수신·검증·기록 (J04) |
| `log` / `idle` | 1 / 0 | 이벤트 / — | 없음 | 1 KB | 로그 플러시(절대 급하지 않다), tickless idle 진입 |

**우선순위 설계의 근거를 말하는 법**: "Rate Monotonic 관점에서 주기가 짧을수록 높은 우선순위를 준다. 오디오가 10 ms로 가장 짧으니 최고, 로그는 deadline이 없으니 최저다. 그리고 모든 task의 (실행시간/주기) 합으로 CPU 사용률을 계산해 여유를 확인한다."

**CPU 사용률 계산 예**:

```
  audio:   1.5 ms 실행 / 10 ms 주기   = 15.0 %
  sensor:  0.4 ms / 20 ms             =  2.0 %
  ipc:     0.3 ms / 50 ms             =  0.6 %
  power:   0.5 ms / 100 ms            =  0.5 %
  ────────────────────────────────────────────
  합계 약 18 %  →  나머지 82 %는 sleep 가능 (전력 예산의 기초, J03으로 이어짐)
```

always-on 기기에서 이 계산은 성능 문제가 아니라 **배터리 문제**다. CPU 사용률이 곧 깨어 있는 시간이다.

### 2.13 ISR에서 task로 — 규칙과 API

**규칙**: ISR은 하드웨어를 진정시키고(플래그 클리어, 버퍼 포인터 교체), 신호를 보내고, 끝낸다. 파싱·필터·로그·할당은 전부 task에서 한다.

| 목적 | FreeRTOS | Zephyr |
|---|---|---|
| 신호 하나 | `xTaskNotifyFromISR`, `xSemaphoreGiveFromISR` | `k_sem_give` (ISR에서 호출 가능) |
| 데이터 전달 / 일 미루기 | `xQueueSendFromISR`, timer daemon task | `k_msgq_put` (K_NO_WAIT), `k_work_submit` (workqueue) |
| 즉시 스위치 | `portYIELD_FROM_ISR(woken)` | 커널이 처리 |

**Cortex-M 인터럽트 우선순위 함정** — 반드시 알아야 한다. NVIC에서는 **숫자가 작을수록 높은 우선순위**이고, FreeRTOS task 우선순위는 **숫자가 클수록 높다**. 방향이 반대다. 게다가 FreeRTOS의 `...FromISR` API는 **`configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 우선순위가 낮거나 같은(즉 숫자가 크거나 같은) ISR에서만** 호출할 수 있다. 이 규칙을 어기면 커널 자료구조가 조용히 깨진다. `configASSERT`를 켜 두면 잡아 준다.

### 2.14 공유 버스와 동기화

I2C 하나에 센서 3개가 붙고 task 2개가 접근하면 반드시 직렬화해야 한다.

| 방법 | 언제 |
|---|---|
| 버스마다 mutex | 가장 일반적. 우선순위 상속이 있는 mutex(FreeRTOS `xSemaphoreCreateMutex`, Zephyr `k_mutex`) |
| 버스 전담 task + 요청 큐 | 트랜잭션이 길거나 전력 정책(버스 클럭 게이팅)을 한곳에서 관리할 때. Zephyr 드라이버는 대개 내부 락이 있으니 두 번 잠그지 않게 주의 |

**priority inversion**: 낮은 우선순위 task가 버스 mutex를 쥔 채 중간 우선순위 task에게 선점당하면, 높은 우선순위 오디오 task가 버스를 기다리며 deadline을 놓친다. 해법은 **priority inheritance를 지원하는 mutex**다. 세마포어로 상호배제를 흉내 내면 상속이 없어서 이 문제가 그대로 남는다 — 면접 단골이다.

---

## 3. 실무 패턴과 함정

### 3.1 BSP·드라이버 함정

| 함정 | 증상 | 원인 | 대응 |
|---|---|---|---|
| I2C pull-up 부족/과다 | 고속에서만 실패, 특정 보드만 | rise time 초과 또는 구동 전류 부족 | 스코프로 rise time 측정, 저항 재선정 |
| I2C 버스 hang | 모든 센서가 갑자기 NACK | slave가 바이트 중간에 리셋 | bus clear 9클럭 + STOP, 복구 카운터 |
| SPI mode 불일치 | 값이 1비트 시프트, 0/0xFF | CPOL/CPHA 오설정 | LA로 idle 레벨과 샘플 엣지 확인 |
| 센서 부팅 대기 누락 | 전원 인가 직후 첫 접근만 실패 | 데이터시트 `t_boot` 무시 | BSP에 레일 enable + 대기를 묶어 제공 |
| UART 수신 누락 | 긴 패킷에서 간헐적 깨짐 | 바이트 인터럽트, 폴링 | DMA + idle line |
| I2S 언더런/오버런 | 오디오가 주기적으로 튄다 | task 처리 시간 > 프레임 주기, 버퍼 부족 | ping-pong, 처리 시간 측정, 우선순위 상향 |
| 클럭 변경 중 전송 / DMA 캐시 | 드문 데이터 손상, 일부 데이터만 이상 | DMA 중 PLL 변경, 캐시 clean/invalidate 누락 | 클럭 변경은 주변장치 정지 후. non-cacheable 영역 또는 명시적 캐시 관리 |
| 인터럽트 핀 설정 누락 / 벤더 드라이버의 블로킹 delay | 이벤트를 놓침, 다른 task가 굶는다 | GPIO 엣지·극성 오설정, 라이브러리 busy-wait | 회로도로 극성 재확인, shim에서 RTOS sleep으로 치환 |

### 3.2 RTOS 스케줄링 함정

| 함정 | 증상 | 원인 | 대응 |
|---|---|---|---|
| priority inversion | 고우선 task가 가끔 늦는다 | 세마포어로 상호배제, 상속 없음 | priority inheritance mutex |
| 스택 오버플로 | 무작위 크래시, 엉뚱한 변수 변조 | 스택 과소 산정, 큰 지역 배열, 재귀 | high water mark 측정, 오버플로 훅/MPU 가드 |
| 우선순위 방향 혼동 / 최고 우선순위 task의 busy loop | ISR에서 커널 API 후 이상 동작, 다른 task가 안 돈다 | NVIC는 숫자 작을수록 높고 FreeRTOS task는 반대. 블로킹 없는 루프 | `configMAX_SYSCALL_INTERRUPT_PRIORITY` 준수 + `configASSERT`, 반드시 큐에서 블록 |
| 워치독을 타이머로 feed | 죽었는데도 리셋 안 됨 | "타이머가 사는지"만 확인 | task check-in 방식 |
| tick 기반 지연 오차 | 정밀 타이밍이 어긋남 | tick 해상도(보통 1 ms) | 하드웨어 타이머 사용, tickless와의 상호작용 확인 |
| 큐 오버플로 무시 | 조용한 데이터 손실 | 반환값 미확인 | 실패 카운터 + 텔레메트리 |

### 3.3 계측 없이는 스케줄링을 설계할 수 없다

| 알고 싶은 것 | 방법 |
|---|---|
| task 실행 시간 / 인터럽트 지연 | GPIO를 진입·탈출에 토글 → 스코프/LA로 측정. 가장 정확하고 침습이 적다 |
| CPU 사용률 | idle task에서 카운터를 돌리거나 RTOS 런타임 통계 기능 |
| task 전환 순서 | SEGGER SystemView, Percepio Tracealyzer, Zephyr tracing 서브시스템 |
| 스택 여유 | FreeRTOS `uxTaskGetStackHighWaterMark`, Zephyr thread analyzer |

**GPIO 토글 + 로직 분석기**는 이 분야에서 가장 저평가된 도구다. 로그와 달리 타이밍을 거의 바꾸지 않는다. Don이 이미 LA/DSO에 익숙하다는 점이 여기서 바로 강점이 된다.

---

## 4. 리서치 — 근거 자료

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| NXP UM10204 — I2C-bus specification | I2C 공식 스펙: 속도 모드, 전기 규격, pull-up 계산, bus clear 절차 | 속도 모드 표, rise time/커패시턴스, "Bus clear" 절 | https://www.nxp.com/docs/en/user-guide/UM10204.pdf |
| Philips I2S bus specification (1996) | I2S 원 스펙: 신호 정의, 타이밍, 1 BCLK 지연 | 전체(짧다). 타이밍 다이어그램 | https://www.sparkfun.com/datasheets/BreakoutBoards/I2SBUS.pdf (공개 미러) |
| Devicetree Specification | devicetree 소스 문법, 바인딩, 표준 프로퍼티 | 2장(Devicetree 구조), 프로퍼티 규칙 | https://www.devicetree.org/specifications/ |
| Zephyr — 보드 포팅 가이드 | 새 보드 추가 절차, 파일 구성, 보드 리비전 | 전체. **파일 구성이 버전마다 다름** | https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html |
| Zephyr — devicetree 가이드 | DT가 빌드에 어떻게 들어가나, 바인딩 작성 | Introduction, Bindings, `DT_` 매크로 | https://docs.zephyrproject.org/latest/build/dts/index.html |
| Zephyr — device driver model | `DEVICE_DT_DEFINE`, `device_get_binding`, 초기화 레벨 | Device Driver Model 전체 | https://docs.zephyrproject.org/latest/kernel/drivers/index.html |
| Zephyr — I2C API | `i2c_write_read_dt`, `i2c_recover_bus`, 설정 플래그 | API 목록과 에러 코드 | https://docs.zephyrproject.org/latest/hardware/peripherals/i2c.html |
| Zephyr — SPI API | `spi_dt_spec`, `spi_transceive_dt`, 버퍼 세트 | 설정 구조체의 mode 비트 | https://docs.zephyrproject.org/latest/hardware/peripherals/spi.html |
| Zephyr — UART API | polling / interrupt / async 세 가지 API | async API(`uart_callback_set`, `uart_rx_enable`) | https://docs.zephyrproject.org/latest/hardware/peripherals/uart.html |
| Zephyr — I2S API | `i2s_configure`, `i2s_read`, `i2s_trigger`, 메모리 슬랩 기반 버퍼 | 설정 구조체 필드(word size, channels, format) | https://docs.zephyrproject.org/latest/hardware/peripherals/audio/i2s.html |
| Zephyr — 스케줄링 / workqueue | 협조·선점, 우선순위 범위, ISR 후처리를 스레드로 넘기는 표준 방법 | Scheduling 전체, Workqueue Threads | https://docs.zephyrproject.org/latest/kernel/services/scheduling/index.html |
| Zephyr — tracing | SystemView/Tracealyzer 연동, 트레이스 포맷 | Tracing 개요 | https://docs.zephyrproject.org/latest/services/tracing/index.html |
| FreeRTOS 공식 문서 | 커널 API, `...FromISR` 규칙, `FreeRTOSConfig.h` | Cortex-M 포팅 절의 인터럽트 우선순위 규칙 | https://www.freertos.org/Documentation/00-Overview |
| Arm Cortex-M4 Devices Generic User Guide | NVIC, 우선순위 필드, 예외 모델 | NVIC 레지스터, 우선순위 그룹화 | https://developer.arm.com/documentation/dui0553/latest/ |
| SEGGER SystemView / Percepio Tracealyzer | RTOS 실시간 트레이스·시각화 도구 | 지원 RTOS와 계측 방법 | https://www.segger.com/products/development-tools/systemview/ · https://percepio.com/tracealyzer/ |
| Liu & Layland (1973) | Rate Monotonic 스케줄링 원논문 | 이용률 한계 정리(주기적 task의 스케줄 가능성) | https://dl.acm.org/doi/10.1145/321738.321743 |
| Nordic 개발자 문서 | nRF Connect SDK(Zephyr 기반) 보드·드라이버 실습용 | 샘플과 보드 지원 목록 | https://docs.nordicsemi.com/ |

> 벤더 MCU SDK(Ambiq 등)는 포털·NDA 배포가 많아 공개 URL이 안정적이지 않다. 실습은 Zephyr + nRF52840 DK 조합이 문서가 가장 잘 갖춰져 있다.

---

## 5. 예상 면접 질문

### Q01. What does it mean to you to "own" a BSP?

**왜 묻나**: JD의 첫 단어가 `Own`이다. 소유의 범위를 스스로 정의할 수 있는지 본다.

**30초 답변**: 보드가 부팅해서 모든 주변장치가 정상 동작하는 상태까지를 책임진다는 뜻이다. 구체적으로는 핀맵, 클럭 트리, 전원 시퀀스, 메모리 맵과 링커 스크립트, 인터럽트 우선순위 배정, DMA 채널 할당, 보드 리비전 식별, 콘솔·워치독 초기화다. 그리고 문서화된 bring-up 체크리스트를 남겨서, 다음 리비전에서 나 말고도 보드를 살릴 수 있게 만드는 것까지가 소유다.

**English answer**: "Owning the BSP means I'm accountable for everything between power-on and 'all peripherals behave'. Concretely that's pin mux, the clock tree, power rail sequencing, the memory map and linker script, the interrupt priority assignment, DMA channel allocation, board revision detection, and console and watchdog init. It also means owning the interface the rest of the firmware sees, so application code never touches a register. And the part people forget: writing down the bring-up checklist, so the next board revision doesn't depend on me being in the building."

**꼬리질문**
- "BSP와 드라이버의 경계는?" → 레일을 켜는 방법은 BSP가, 언제 켜는지는 드라이버/앱이 정한다.
- "혼자 소유하면 버스 팩터가 1 아닌가?" → 그래서 체크리스트와 devicetree 같은 데이터 주도 구조가 필요하다.

### Q02. A brand new EVT board just landed on your desk. What do you do first?

**왜 묻나**: bring-up 경험의 진위 판별. 해 본 사람은 순서가 있고, 안 해 본 사람은 코드부터 말한다.

**30초 답변**: 전원을 넣기 전에 회로도로 레일과 스트랩 핀을 확인하고, 전류 제한을 건 채로 인가한다. 그다음 순서는 전원 → 클럭 → 디버그 포트 → UART 콘솔 → GPIO → 타이머/RTOS tick → 버스 스캔 → 개별 디바이스 → 오디오·라디오 → 전력이다. 한 번에 한 층만 올린다. 콘솔이 살아나는 순간이 심리적으로도 실제로도 절반이다.

**English answer**: "Before power, I read the schematic — rails, straps, boot mode pins — and I bring it up on a bench supply with current limiting, so a short costs me a board and not a fire. Then strictly bottom-up: rails and their ramp on the scope, crystal and PLL lock, can the debugger see the core, one character out of the UART, toggle a GPIO, get the timer tick and two threads alternating, then scan I2C and read the SPI flash JEDEC ID, then each device's ID register, and only then the hard stuff — I2S and radios. One layer at a time, because if you skip ahead you end up debugging four unknowns at once. I've done exactly this loop on FPGA pre-silicon and then again on production silicon."

**꼬리질문**
- "부팅이 아예 안 되면 제일 먼저 보는 것은?" → 전원과 클럭, 그다음 리셋 핀. 코드가 아니다.
- "레일 전압은 맞는데 코어가 안 잡히면?" → 리셋 유지, 부트 모드 스트랩, 디버그 포트 잠금 여부, SWD 핀이 다른 기능으로 뮤싱됐는지.
- "EVT1에서 되던 게 EVT2에서 안 되면?" → 회로도 diff부터 본다. 코드보다 보드가 바뀌었을 확률이 높다.

### Q03. How do you integrate a driver you didn't write — a vendor SDK driver — into your codebase?

**왜 묻나**: JD가 `development`가 아니라 `integration`이라고 썼다. 실제 업무의 대부분이다.

**30초 답변**: 먼저 벤더 코드를 수정하지 않는다는 원칙을 세우고, 얇은 shim으로 우리 HAL과 RTOS에 맞춘다. 검수 항목은 정해져 있다 — 블로킹 busy-wait를 쓰는가, 전역 상태로 인스턴스를 하나만 허용하는가, 재진입 가능한가, ISR에서 부를 수 있는가, 에러를 반환하는가 아니면 무한 대기하는가, `malloc`이나 `printf`를 쓰는가. 통합 후에는 반드시 로직 분석기로 실제 버스 트랜잭션을 확인한다. 드라이버가 "동작하는 것처럼 보이는" 것과 "데이터시트대로 말하는" 것은 다르다.

**English answer**: "Rule one: I don't edit vendor source in place, because six months later nobody knows why the SDK upgrade breaks. I wrap it in a thin shim that adapts it to our bus HAL and our RTOS primitives. Before I trust it I audit for a specific list: busy-wait delays that burn CPU under an RTOS, global state that prevents two instances, reentrancy, whether it's safe from an ISR, whether it returns errors or spins forever, and whether it calls malloc or printf. Then I put a logic analyzer on the bus and compare the actual transaction against the datasheet, because a driver that appears to work and a driver that talks correctly are not the same thing — especially around reset timing and register write ordering."

**꼬리질문**
- "벤더 드라이버가 명백히 버그가 있으면?" → 패치 파일이나 명확한 마커 주석으로 분리하고, 벤더에 리포트한다.
- "Zephyr in-tree 드라이버가 있으면 항상 쓰나?" → 대개 그렇다. 우리 use case(전력 모드, 비동기 요구)를 커버하는지만 확인한다.
- "통합 후 회귀는 어떻게 막나?" → 그 디바이스에 대한 스모크 테스트(ID 읽기 + 기본 동작)를 HIL에 넣는다.

### Q04. An I2C bus is stuck — SDA is held low and every transaction NACKs. What happened, and how do you recover?

**왜 묻나**: I2C의 전기적 특성을 이해하는지 보는 고전 문제. Don의 강점 영역이다.

**30초 답변**: slave가 바이트를 내보내는 중간에 리셋되거나 전원이 꺼지면 SDA를 low로 잡은 채 멈춘다. open-drain이라 master가 high로 끌 수 없고, MCU만 리셋해도 안 풀린다. 복구는 핀을 GPIO로 되돌려 SCL을 최대 9번 토글해 남은 비트를 흘려보내고, SDA가 풀리면 STOP 조건을 만드는 것이다. 이건 I2C 스펙(UM10204)의 bus clear 절차다. 그리고 복구 횟수를 카운터로 남겨서 어느 보드가 병들었는지 필드에서 본다.

**English answer**: "Classic symptom of a slave that got reset or lost power in the middle of driving a data bit. The bus is open-drain, so the master physically cannot pull SDA high, and resetting the MCU doesn't help because the slave is the one holding the line. The recovery is in the I2C spec: switch the pins to GPIO, toggle SCL up to nine times so the slave clocks out the rest of its byte and releases SDA, then generate a STOP condition and hand the pins back to the peripheral. I'd make that a normal step in the driver's error path, not a special case, and I'd count recoveries in telemetry — a unit that recovers once a week is telling you something about a marginal rail or a flaky part before it turns into a return."

**꼬리질문**
- "복구가 안 되면?" → 해당 슬레이브의 전원 레일을 껐다 켠다. 그래서 BSP가 레일 제어를 제공해야 한다.
- "clock stretching과 어떻게 구분하나?" → stretching은 SCL이 low로 잡힌 것이고, hang은 SDA가 low다. 파형에서 바로 구분된다.
- "EVT 보드 100대 중 5대만 그렇다면?" → 부품 편차, pull-up 값, 배선 길이/커패시턴스, 온도. 실패 보드와 정상 보드의 rise time을 비교한다.

### Q05. Walk me through bringing up an I2S microphone path.

**왜 묻나**: JD가 I2S를 명시했다. 그리고 Don의 자각된 갭이다 — 여기서 준비한 티가 난다.

**30초 답변**: 먼저 클럭을 계산한다. BCLK = 샘플레이트 × 비트 × 채널. 16 kHz, 16-bit 스테레오면 512 kHz다. WS는 샘플레이트와 같다. 포맷(Philips I2S인지 left-justified인지, WS 전환 후 1 BCLK 지연이 있는지)과 마스터가 누구인지를 데이터시트로 확정하고, 로직 분석기로 BCLK/WS 주파수와 데이터 정렬을 먼저 확인한다. 그다음 DMA ping-pong으로 10 ms 프레임을 받아 ISR에서는 버퍼 포인터만 큐에 넣고 오디오 task가 처리한다. 마지막으로 첫 PCM을 호스트로 덤프해서 WAV로 들어 본다 — 눈으로 보는 것보다 귀로 듣는 게 빠르다.

**English answer**: "First arithmetic, then wires. BCLK is sample rate times bits per channel times channels, so 16 kHz 16-bit stereo is 512 kHz, and word select runs at the sample rate. I confirm from the datasheet which side is master, and whether the format is Philips I2S with the one-bit-clock delay after word select or left-justified, because getting that wrong gives you audio that's shifted by a bit and sounds like noise. Then I put a logic analyzer on BCLK and WS and verify the frequencies and the data alignment before I trust any software. For the data path I use DMA with ping-pong buffers sized to a frame — ten milliseconds is a convenient unit — and the DMA completion ISR only swaps the buffer pointer and posts it to a queue; all processing happens in an audio task. And the single most useful bring-up step is dumping the first PCM buffer to the host and playing it as a WAV. Your ears find clipping, DC offset, wrong channel order and half-rate clocks faster than any debugger."

**꼬리질문**
- "오디오가 주기적으로 튄다면?" → 언더런. task 처리 시간이 프레임 주기를 넘었거나 버퍼가 부족하다. GPIO 토글로 처리 시간을 실측한다.
- "PDM 마이크면 뭐가 달라지나?" → 1비트 고속 스트림이 오고 decimation 필터로 PCM을 만든다. MCU에 PDM 하드웨어 블록이 있는지가 관건이다.
- "마이크가 4개면?" → TDM으로 간다. 프레임에 슬롯을 여러 개 두고 BCLK가 그만큼 올라간다.

### Q06. How do you decide between polling, interrupts, and DMA for a peripheral?

**왜 묻나**: BSP 오너의 기본 판단. 그리고 전력으로 이어진다.

**30초 답변**: 데이터 양과 이벤트 빈도, 그리고 전력으로 결정한다. 폴링은 bring-up이나 아주 짧은 트랜잭션에만 쓰고, 이벤트가 드물면 인터럽트, 블록 전송이나 스트림이면 DMA다. 계산을 해 보면 명확해진다 — 16 kHz 스테레오를 샘플마다 인터럽트로 받으면 초당 32000번이고, ISR 오버헤드만으로 64 MHz MCU의 몇 퍼센트를 쓴다. DMA로 10 ms 블록을 받으면 초당 100번이 된다. always-on 기기에서 이건 성능이 아니라 배터리 문제다.

**English answer**: "Data volume, event rate, and power. Polling only during bring-up or for transactions so short that setting up anything else costs more — and always with a timeout. Interrupts when events are infrequent. DMA for block transfers and anything streaming. I like to do the arithmetic out loud: sixteen kilohertz stereo audio is thirty-two thousand samples a second, so a per-sample interrupt at a hundred cycles of entry and exit overhead is over three megacycles a second, which is a few percent of a 64 MHz core doing nothing but interrupt bookkeeping. Move to DMA with ten-millisecond blocks and it's a hundred interrupts a second. On an always-on battery device that difference isn't performance, it's runtime."

**꼬리질문**
- "DMA의 함정은?" → 캐시 있는 코어에서의 일관성, 버퍼 정렬, 완료 콜백 누락, 채널 경합.
- "폴링이 더 나은 경우가 있나?" → 매우 짧은 레지스터 읽기. 인터럽트 셋업 비용이 대기 시간보다 클 때.
- "DMA 채널이 부족하면?" → 우선순위를 정해 스트리밍(오디오)에 먼저 주고, 나머지는 인터럽트로.

### Q07. Design the task set and priorities for an always-on voice device.

**왜 묻나**: `RTOS task scheduling`의 핵심 질문. 시스템 설계 능력을 본다.

**30초 답변**: task는 "동시에 일어나야 하는 일"의 단위로 나누고, 우선순위는 중요도가 아니라 deadline으로 준다. 오디오가 10 ms로 가장 급하니 최고, 센서와 IPC가 중간, 전력 상태 머신이 그다음, OTA와 로그가 최저다. Rate Monotonic 관점에서 주기가 짧을수록 높은 우선순위다. 그리고 각 task의 실행시간/주기 합으로 CPU 사용률을 계산해서 — 예를 들어 18% — 나머지 82%가 sleep 가능한 시간이라는 걸 확인한다. 그게 전력 예산의 출발점이다.

**English answer**: "I'd start from what must happen concurrently, not from a feature list, because every extra thread costs a stack and context switches. For a voice device: an audio thread driven by the I2S DMA at the highest priority, because a ten-millisecond frame deadline is the tightest thing in the system; sensor and SoC-IPC threads in the middle; a power and PMIC state machine below that, since it can tolerate hundreds of milliseconds; and OTA and logging at the bottom, because they have no deadline at all. Priorities come from deadlines, rate-monotonic style, not from importance — people want to make battery management top priority because it sounds important, and that's exactly backwards. Then I compute utilization, execution time over period for each thread, and if that's say eighteen percent, the other eighty-two percent is sleep, which is the real currency on a battery device."

**꼬리질문**
- "task를 하나 더 만들지 기존에 합칠지 어떻게 정하나?" → 블로킹 지점이 다르고 동시에 진행돼야 하면 분리, 순차적이면 합친다.
- "우선순위가 같은 task 둘이면?" → 라운드 로빈/타임 슬라이스에 의존하게 되는데, 그건 설계가 아니라 우연이다. 가능하면 다르게 준다.
- "CPU 사용률이 70%면 괜찮은가?" → 평균은 괜찮아 보여도 burst가 겹치는 최악 경우를 봐야 한다. 응답 시간 분석이 필요하다.

### Q08. What is priority inversion, and where would it actually bite you in this device?

**왜 묻나**: RTOS 면접 단골. 개념만 아는지, 실제 위치를 짚을 수 있는지가 갈린다.

**30초 답변**: 낮은 우선순위 task가 자원을 쥔 상태에서 중간 우선순위 task에게 선점당하면, 높은 우선순위 task가 낮은 task를 무한정 기다리게 된다. 이 기기에서는 공유 I2C 버스가 가장 위험하다. 로그 task가 EEPROM을 쓰느라 버스 mutex를 쥐었는데 센서 task가 선점하면, 오디오 task가 코덱 설정을 못 바꿔 프레임을 놓친다. 해법은 priority inheritance를 지원하는 mutex를 쓰는 것이고, 세마포어로 상호배제를 흉내 내면 상속이 없어서 그대로 당한다.

**English answer**: "Priority inversion is when a high-priority thread is blocked on a resource held by a low-priority thread, and a medium-priority thread that doesn't need the resource preempts the low one, so the high-priority thread waits on something entirely unrelated. In this device the shared I2C bus is the obvious place: the logging thread is writing to an EEPROM and holds the bus mutex, the sensor thread preempts it, and meanwhile the audio thread can't reconfigure the codec and misses a frame. The fix is a mutex with priority inheritance, which FreeRTOS and Zephyr both provide, so the holder is temporarily boosted. The trap is using a binary semaphore for mutual exclusion — it looks identical in the code and gives you no inheritance at all."

**꼬리질문**
- "priority ceiling과 inheritance의 차이는?" → ceiling은 락을 잡는 순간 정해진 상한 우선순위로 올리고, inheritance는 실제로 기다리는 task가 생겼을 때 올린다.
- "inheritance가 해결 못 하는 건?" → 데드락과, 홀더의 실행 시간 자체가 긴 경우. 임계 구역을 짧게 유지하는 게 먼저다.
- "어떻게 관측하나?" → SystemView/Tracealyzer 같은 트레이스 도구로 스레드 전환을 본다.

### Q09. How do you get data from an ISR to a task?

**왜 묻나**: 드라이버와 RTOS의 접합부. 실무 경험이 바로 드러난다.

**30초 답변**: ISR은 하드웨어를 진정시키고 신호만 보낸다. 데이터를 복사하지 말고 버퍼 포인터를 큐에 넣는다. FreeRTOS면 `xQueueSendFromISR`이나 `xTaskNotifyFromISR`을 쓰고, `xHigherPriorityTaskWoken`을 받아 `portYIELD_FROM_ISR`로 즉시 전환한다. Zephyr면 `k_msgq_put`이나 `k_work_submit`으로 workqueue에 넘긴다. Cortex-M에서는 인터럽트 우선순위 규칙을 꼭 지켜야 한다 — FromISR API는 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높은 우선순위 ISR에서 호출하면 커널이 조용히 깨진다.

**English answer**: "The ISR's job is to quiet the hardware and hand off — clear the flag, swap a buffer pointer, post it. No parsing, no filtering, no logging, no allocation. I pass pointers rather than copying data, so a DMA audio frame costs four bytes on a queue instead of a memcpy in interrupt context. On FreeRTOS that's xQueueSendFromISR or a direct task notification, capturing xHigherPriorityTaskWoken and calling portYIELD_FROM_ISR so the woken thread runs immediately instead of at the next tick. On Zephyr it's k_msgq_put or submitting work to a workqueue. And the Cortex-M rule that bites people: the FromISR APIs are only legal from interrupts at or below configMAX_SYSCALL_INTERRUPT_PRIORITY, and since NVIC priorities are inverted — lower number is higher priority — it's easy to get backwards. I keep configASSERT enabled so it fails loudly instead of corrupting a queue."

**꼬리질문**
- "큐가 가득 차면?" → 반환값을 확인하고 드롭 카운터를 올린다. 조용한 손실이 최악이다.
- "왜 ISR에서 처리하면 안 되나?" → 그 시간 동안 같거나 낮은 우선순위 인터럽트가 전부 막힌다. 지터가 커진다.
- "notification과 queue 중 언제 뭘?" → 데이터가 없으면 notification(가장 가볍다), 데이터가 있으면 queue.

### Q10. Two threads need two different sensors on the same I2C bus. How do you handle that?

**왜 묻나**: 공유 자원 설계. Q08과 짝이다.

**30초 답변**: 버스마다 priority inheritance mutex를 두고 트랜잭션 단위로 잡는다. 임계 구역은 최대한 짧게 — 트랜잭션 하나이지 "센서 읽기 로직 전체"가 아니다. 트랜잭션이 길거나 버스 클럭 게이팅 같은 전력 정책을 한곳에서 관리해야 하면, 버스 전담 task와 요청 큐로 바꾼다. 그러면 우선순위 문제가 큐 안의 순서 문제로 바뀌어서 다루기 쉬워진다.

**English answer**: "A per-bus mutex with priority inheritance, taken around a single transaction and released immediately — not held across the whole 'read the sensor and decide something' function, because the length of the critical section is what determines the worst-case blocking of everything above it. If transactions get long, or if I want one place to own bus power and clock gating, I'd switch to a dedicated bus thread with a request queue, so callers post a transaction and wait on a completion. That turns a priority problem into an ordering problem inside a queue, which I can reason about and instrument. Zephyr's drivers already serialize internally, so the thing to avoid there is double-locking and accidentally nesting."

**꼬리질문**
- "mutex를 ISR에서 잡을 수 있나?" → 안 된다. ISR은 블록할 수 없다. 그래서 ISR은 버스에 직접 접근하지 않는다.
- "데드락을 어떻게 피하나?" → 락 순서를 전역으로 정하고, 중첩을 최소화한다.
- "타임아웃은?" → 항상 건다. 무한 대기는 워치독 리셋으로 끝나고 원인을 잃는다.

### Q11. How do you size task stacks, and how do you know when you're wrong?

**왜 묻나**: 실전에서 가장 자주 나는 사고 중 하나. 측정 습관을 본다.

**30초 답변**: 처음엔 넉넉하게 잡고 실측으로 줄인다. FreeRTOS면 `uxTaskGetStackHighWaterMark`, Zephyr면 thread analyzer로 최악 사용량을 보고 여유를 30~50% 남긴다. 주의할 점은 최악 경로가 평소에 안 도는 경우다 — 에러 처리, 로그 포맷팅(`printf`가 스택을 많이 먹는다), 인터럽트 중첩. 감지는 스택 오버플로 훅이나 MPU 스택 가드로 하고, 증상은 무작위 크래시나 엉뚱한 변수 변조라서 감지 장치가 없으면 며칠을 잃는다.

**English answer**: "Start generous, then measure and shrink. FreeRTOS gives you uxTaskGetStackHighWaterMark and Zephyr has a thread analyzer, so I run the device through its worst paths and keep thirty to fifty percent headroom. The subtlety is that the worst path usually isn't the normal path — it's error handling, or printf-style formatting which is surprisingly stack hungry, or nested interrupts on top of the deepest call chain. For detection I turn on the stack overflow hook, and if the part has an MPU I use it as a stack guard so an overflow becomes a clean fault at the moment it happens instead of silent corruption three functions later. Random crashes that move when you add a variable are almost always this."

**꼬리질문**
- "ISR은 어느 스택을 쓰나?" → Cortex-M에서 핸들러 모드는 MSP를 쓴다. task 스택과 별개지만 부팅 스택과는 겹칠 수 있어 계산에 넣어야 한다.
- "스택을 정적으로 잡는 이유는?" → 런타임 할당 실패가 없고 링커 맵에서 총량이 보인다.
- "`printf`를 쓰면?" → 부동소수점 포맷이 특히 크다. 임베디드용 경량 구현을 쓰거나 로그를 ID 기반으로.

### Q12. An SPI sensor returns garbage — values look shifted. How do you debug it?

**왜 묻나**: 하드웨어-소프트웨어 경계 디버깅. Don의 최강 영역이다.

**30초 답변**: 먼저 로직 분석기를 건다. SCLK의 idle 레벨로 CPOL을, 첫 데이터 엣지로 CPHA를 읽으면 mode 불일치가 바로 보인다. 그다음 CS 타이밍 — 워드마다 토글해야 하는 칩인지 트랜잭션 전체를 유지해야 하는 칩인지를 데이터시트와 대조한다. 그래도 아니면 클럭 주파수가 칩의 최대치를 넘었는지, 신호 무결성(오버슈트, 긴 배선), 다중 슬레이브에서 MISO를 놓지 않는 칩이 있는지를 본다. 1비트 시프트는 거의 항상 mode 아니면 CS다.

**English answer**: "Logic analyzer first, before I touch the code. The idle level of SCLK tells me CPOL and the position of the first data edge tells me CPHA, so a mode mismatch is visible in about thirty seconds — and a one-bit shift is almost always mode or chip select. Next I check CS behavior against the datasheet, because some parts want CS toggled per word and some want it held for the whole transaction, and hardware CS automation often does the wrong one. If the waveform looks correct but the data doesn't, I look at clock frequency against the part's maximum, then signal integrity — overshoot and ringing on long traces — and in a multi-slave setup, whether some device isn't releasing MISO. This is the kind of loop I ran constantly at Apple, root-causing interface failures between a new chip and the host."

**꼬리질문**
- "파형은 완벽한데 값이 이상하면?" → 레지스터 맵이나 엔디언, 또는 부호 확장 버그. 소프트웨어 쪽으로 넘어간다.
- "스코프와 LA 중 언제 뭘?" → 프로토콜 내용은 LA, 신호 품질(링잉, 레벨, rise time)은 스코프.
- "간헐적이면?" → 온도, 전압, 클럭 속도를 흔들어 재현 조건을 찾고 트리거를 건다.

### Q13. How do you know the system actually meets its timing requirements?

**왜 묻나**: 스케줄링을 "설계"로 다루는지 "감"으로 다루는지 본다.

**30초 답변**: 측정한다. task 진입/탈출에 GPIO를 토글해 로직 분석기로 실행 시간과 지터를 보고, RTOS 런타임 통계나 idle 카운터로 CPU 사용률을 낸다. 스레드 전환 순서를 보려면 SystemView나 Tracealyzer, Zephyr tracing을 붙인다. 숫자가 나오면 Rate Monotonic 이용률과 비교하고, 최악 경우가 겹치는 시나리오(오디오 + 센서 인터럽트 + OTA 기록 동시)를 일부러 만들어 돌린다. 로그로 타이밍을 재려는 시도는 하지 않는다 — 로그 자체가 타이밍을 바꾼다.

**English answer**: "I measure rather than argue. GPIO toggles at thread entry and exit, read with a logic analyzer, give me execution time and jitter with almost no intrusion — unlike logging, which changes the timing you're trying to measure. RTOS runtime stats or an idle-loop counter give me utilization. For ordering and preemption I attach a trace tool like SystemView or Tracealyzer. Then I compare against rate-monotonic utilization and, more importantly, I construct the worst case deliberately: audio streaming while a sensor burst arrives while OTA is writing flash, because the average case never fails and the correlated case always does. And I'd want a deadline-miss counter in the firmware itself, reported through telemetry, so the field tells me when my analysis was wrong."

**꼬리질문**
- "GPIO 토글이 왜 로그보다 나은가?" → 몇 사이클이면 끝나고 타이밍을 거의 안 바꾼다.
- "flash 쓰기가 타이밍을 망친다면?" → 내부 flash 쓰기는 코드 실행을 멈출 수 있다. 오디오 프레임 사이로 조각내거나 외장 flash로 옮긴다.
- "tickless idle을 켜면 측정이 달라지나?" → tick이 불규칙해지므로 tick 기반 통계는 신뢰도가 떨어진다. 하드웨어 타이머 기준으로 재야 한다.

### Q14. In a Zephyr-based BSP, what goes in devicetree, what goes in Kconfig, and what goes in code?

**왜 묻나**: Zephyr 실무 여부를 가르는 질문. Don의 학습 항목이자, 개념으로 답할 수 있는 영역.

**30초 답변**: devicetree는 하드웨어가 무엇이고 어떻게 연결됐나 — 센서가 어느 버스의 어느 주소에 붙었고 인터럽트 핀이 무엇인지. Kconfig는 소프트웨어 기능을 켤지 말지 — 드라이버 활성화, 로그 레벨, 스택 크기 기본값. 코드는 정책 — 몇 Hz로 읽을지, 언제 sleep할지. 가장 흔한 실수는 하드웨어 사실을 Kconfig에 넣는 것이고, 보드가 두 개가 되는 순간 무너진다.

**English answer**: "Devicetree describes the hardware: which SoC, what's on which bus at which address, which pin is the interrupt, what the clock sources are — the physical facts that differ per board and per revision. Kconfig turns software features on and off: enable this driver subsystem, set the log level, pick default stack sizes. Code holds policy: how often to sample, when to sleep, what to do with an event. The classic mistake is putting a hardware fact in Kconfig, like a sensor's I2C address, and it works fine right up until there are two board revisions, at which point you're branching in C to undo a build-time decision. I'll be honest that my hands-on Zephyr time is recent and self-directed rather than production, but the separation itself matches how I've structured bare-metal BSPs — board facts in data, policy in code."

**꼬리질문**
- "보드 리비전은 어디에?" → devicetree overlay. Zephyr는 보드 리비전 문법을 제공한다(버전마다 세부는 다름).
- "devicetree overlay와 `.conf` 중첩 규칙은?" → 앱 레벨이 보드 기본값을 덮어쓴다.
- "벤더가 devicetree 바인딩을 안 주면?" → 직접 작성한다. YAML 바인딩 파일 + 드라이버 매크로.

---

## 6. Don 매핑

### 6.1 레쥬메에서 바로 쓸 수 있는 근거 (context 3.1절)

| JD 요소 | 레쥬메 근거 | 매칭 | 어떻게 말할까 |
|---|---|---|---|
| BSP development | "Silicon/system bring up -> NPI -> MP", "SoC verification … SRAM/DRAM bring-up", FPGA pre-silicon | ✅ 강함 | "BSP라는 단어를 안 썼을 뿐 같은 일이다. 아무것도 없는 보드를 부팅시키는 일" |
| SPI, I2C, UART | "I2C, SPI, DMA, PCIe", Apple에서 "PCIe, I2C, SPMI, RFFE" root cause | ✅ 강함 | 드라이버를 쓴 수준이 아니라 **실패를 파형으로 파고든** 수준이라는 게 차별점 |
| I2S | 없음 | ❌ 갭 | 정직하게 인정하고 §2.9의 이해를 보여준다. 클럭 계산과 DMA ping-pong은 말할 수 있다 |
| peripheral driver integration | 드라이버를 FPGA → 실리콘으로 이관, 주변 IP bring-up | ✅ | "벤더 IP/SDK를 우리 환경에 붙이는 일" 프레이밍 |
| RTOS task scheduling | bare-metal·SSD 자체 스케줄러. FreeRTOS/Zephyr 이름 없음 | 🟡 부분 | "SSD FW에서 command queue, ISR, 동기화를 직접 다뤘다. RTOS API는 그 위의 추상화" (context 3.3) |
| DMA | "I2C, SPI, DMA" | ✅ | I2S ping-pong 설명의 기반으로 쓴다 |
| 디버깅 도구 | "JTAG, Oscilloscope, Logic Analyzer, Power Analyzer", "DSOs and protocol Analyzer" | ✅ 강함 | Q04/Q05/Q12/Q13에서 전부 쓸 수 있는 자산 |

### 6.2 강점 스토리

**스토리 A: FPGA pre-silicon에서 주변 IP bring-up (Q02, Q03에 쓰기 좋다)** — "I2C, SPI, DMA, SRAM/DRAM bring-up"을 pre-silicon 환경에서 하고 production silicon으로 이관했다. bring-up 순서를 몸으로 아는 사람이라는 증거이고, Q02의 10단계를 자기 경험으로 말할 수 있다.

**스토리 B: 인터페이스 장애 root cause (Q04, Q12에 쓰기 좋다)** — Apple에서 새 무선 칩이 전체 HW/SW 시스템을 만날 때의 PCIe/I2C/SPMI/RFFE 장애를 맡았다. "파형부터 본다"는 접근이 몸에 배어 있다는 걸 구체적 사례로 보여줄 수 있다. <확인 필요: 공개 가능한 수준으로 각색한 버스 장애 사례 1건 — 증상 → 가설 → 측정 → 원인 → 수정 → 재발 방지>

**스토리 C: SPMI/RFFE 경험을 I2C 설명에 연결** — SPMI와 RFFE는 전력 관리·RF 프런트엔드용 직렬 버스로, I2C처럼 주소 기반 멀티 슬레이브 구조다. "I2C의 사촌을 실무로 디버깅했다"는 프레이밍이 가능하다. 다만 전기적 특성(SPMI/RFFE는 open-drain이 아니다)이 다르므로 **동일하다고 말하지 않는다**.

### 6.3 갭과 프레이밍

| 갭 | 사실 | 프레이밍 (거짓말하지 않는 선) |
|---|---|---|
| I2S / 오디오 | 레쥬메에 없음 | "오디오 인터페이스는 이번에 새로 공부한 영역이다. 다만 DMA 스트리밍과 링버퍼, 실시간 데이터 경로는 SSD 데이터 패스에서 매일 하던 일이라 개념 이동은 빨랐다." 그리고 §2.9의 클럭 계산을 실제로 보여준다 |
| FreeRTOS / Zephyr 실무 | context 3.1: 상용 RTOS 이름 없음 | "멀티 task, ISR 후처리, 동기화, 우선순위는 SSD FW에서 직접 다뤘다. 차이는 커널을 내가 썼느냐 남이 썼느냐다." 사이드 프로젝트를 **한 뒤에만** 언급 (context 4.8) |
| Zephyr devicetree/Kconfig | 경험 근거 없음 | Q14처럼 **개념은 정확히, 경험은 정직하게**. "production Zephyr는 아직 없다"를 먼저 말하고 개념으로 채운다 |
| task 집합 설계 문서화 | <확인 필요: SSD FW에서 task/스레드 구조와 우선순위를 직접 설계했는지, 아니면 기존 구조 위에서 기능을 얹었는지> | 직접 설계했다면 Q07의 1급 근거. 아니면 "설계는 이렇게 하겠다"로 답한다 |
| 스택 크기·CPU 부하 측정 | <확인 필요: SSD FW에서 스택 사용량이나 CPU 사용률을 측정·튜닝한 경험이 있는지> | 있으면 Q11/Q13의 강한 근거 |
| PMIC / fuel gauge 드라이버 | <확인 필요: Apple에서 전원 관련 칩 드라이버나 레일 시퀀스를 직접 다뤘는지> | J03과 함께 쓸 수 있다 |

### 6.4 쓰지 말아야 할 표현

"I2S 드라이버를 작성해 봤다", "FreeRTOS로 제품을 출하했다", "Zephyr 보드 포팅을 했다" — 전부 근거가 없다. 실습을 한 뒤라면 "사이드 프로젝트로 해 봤다"까지만 말한다.

---

## 7. 준비 체크리스트

- [ ] bring-up 10단계(§2.3)를 순서대로 암기하고, 각 단계에서 실패했을 때 무엇을 의심하는지 한 줄씩 붙인다 (Q02)
- [ ] I2S 클럭 공식(`BCLK = fs × bits × channels`)을 외우고, 16 kHz/16-bit/스테레오와 48 kHz/32-bit 슬롯 두 경우를 암산으로 계산한다 (Q05)
- [ ] I2C bus clear 절차(9 SCL + STOP)를 화이트보드에 그리고, open-drain 때문에 필요하다는 것까지 설명한다 (Q04)
- [ ] SPI mode 표(CPOL/CPHA → 샘플 엣지)를 외우고, "1비트 시프트 = mode 아니면 CS"를 즉답한다 (Q12)
- [ ] always-on 기기의 task 표(§2.12)를 직접 다시 써 보고, 각 우선순위의 근거를 deadline으로 설명한다 (Q07)
- [ ] CPU 사용률 계산 예제를 스스로 만들어 본다. 오디오 프레임 처리 시간이 프레임 주기를 넘으면 무슨 일이 나는지까지 (Q06, Q13)
- [ ] `xQueueSendFromISR` + `xHigherPriorityTaskWoken` + `portYIELD_FROM_ISR` 패턴을 코드로 5분 안에 쓸 수 있게 연습. Zephyr `k_work_submit` 대응도 (Q09)
- [ ] NVIC 우선순위 방향(숫자 작을수록 높음)과 FreeRTOS task 우선순위 방향(반대)을 헷갈리지 않게 정리 (Q09)
- [ ] priority inversion 타임라인을 그림으로 그리고, 이 기기에서 어디가 위험한지(공유 I2C 버스) 한 문장으로 말한다 (Q08)
- [ ] nRF52840 DK + Zephyr로 ① I2C 스캔 ② PDM/I2S 마이크 샘플 ③ ISR → workqueue 예제를 실제로 돌린다 (context 4.8과 연동. **이걸 해야만** Zephyr를 언급할 수 있다)
- [ ] I2S 갭에 대한 정직한 30초 답변을 영어로 다듬는다 (§6.3)
- [ ] 역질문 준비: "MCU 쪽 RTOS는 정해졌나요? 오디오 전단은 MCU에서 어디까지 하나요? 마이크는 PDM인가요 I2S인가요, 몇 개인가요?"

---

## 8. 더 읽기

| 주제 | 어디로 | 왜 |
|---|---|---|
| BSP 구성 요소, Zephyr 보드 디렉터리, bring-up 순서 | C03 §1 | §2.1~2.3의 전체 배경 |
| 클럭 트리의 함정, polling/interrupt/DMA 선택과 계산, DMA와 캐시 | C03 §2, §4 | §2.4, §2.11의 상세 |
| UART 프레임·RX 링버퍼·Zephyr UART 3종 API, SPI 신호와 모드 | C03 §5, §6 | §2.8, §2.6, Q12 |
| I2C 전기 구조, pull-up 계산, 버스 복구, 파형 | C03 §7 | §2.7, Q04 |
| I2S/PDM/TDM 전체, 클럭 master, Zephyr I2S 코드 | C03 §8 | §2.9, Q05 — **가장 먼저 읽을 것** |
| DMA ping-pong 상세와 ISR→task 전달 | C03 §9 | §2.9의 코드 |
| 드라이버 계층(HAL/CMSIS/Zephyr device model), devicetree와 binding | C03 §10, §11 | §2.10, Q14 |
| 스케줄러·task 상태·우선순위 숫자 방향, 동기화 선택(queue/semaphore/mutex/notification) | C04 §2, §5 | §2.12, §2.14, Q07, Q09, Q10 |
| priority inversion과 inheritance 타임라인 | C04 §6 | Q08 |
| ISR→task, Cortex-M 우선순위 규칙, `portYIELD_FROM_ISR` | C04 §7 | Q09 |
| 스택 크기와 오버플로 감지, Rate Monotonic·응답 시간 분석 | C04 §10.3, §11, §16 | Q11, Q07, Q13 |
| 드라이버·BSP·RTOS 면접 문항 25개 이상 | S02 전체 | 이 노트의 문답을 더 넓게 |
| 로직 분석기·스코프·회로도 읽기, 보드 bring-up 체크리스트 | C10 | Q02, Q12 |
| 같은 JD 문장군 | J11(RTOS — 검증 관점), J13(회로도·bring-up 협업), J01(코드베이스 운영) | J02는 업무 관점 |
