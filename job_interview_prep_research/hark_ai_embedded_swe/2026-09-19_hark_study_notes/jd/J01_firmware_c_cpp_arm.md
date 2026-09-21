# J01. Develop and maintain embedded firmware in C/C++ targeting ARM-based SoCs and microcontrollers

> **분류**: Responsibility 1/7 · **관련 개념 노트**: C01(Cortex/부트/툴체인), C02(제약 환경 C/C++), C10(디버깅), S01(C 코딩 드릴)
> **Don 현재 상태**: ✅ 강함 — 레쥬메에 "embedded C and C++ on bare-metal (pre/post-silicon)", "ARM Cortex R8/R82/M0+ FW bring-up"이 그대로 있다. 다만 이 문장의 무게중심인 **maintain**(여러 보드 리비전·여러 이미지·릴리스 운영)은 SSD 양산 FW 경험으로 번역해서 말해야 한다.
> **이 노트를 다 읽으면**: ① JD 첫 문장이 "코딩 잘하냐"가 아니라 "펌웨어 코드베이스를 **운영**해 본 적 있냐"는 질문임을 알게 된다 ② SoC(Cortex-A/Android)와 MCU(Cortex-M/RTOS)가 한 제품에 같이 있을 때 코드·빌드·버전이 어떻게 갈라지는지 설명할 수 있다 ③ 컨슈머 양산 제품에서 C와 C++를 어디까지 쓰는지, 그 결정을 면접에서 근거와 함께 말할 수 있다.

---

## 0. 문장 뜯어보기

원문: `Develop and maintain embedded firmware in C/C++ targeting ARM-based SoCs and microcontrollers`

| 구(句) | 표면적 의미 | 채용담당자가 이 단어를 고른 이유 (해석) |
|---|---|---|
| `Develop` | 새 기능·새 드라이버를 만든다 | 1세대 기기라 아직 만들 게 대부분이다. 0→1 단계에서 "빈 화면에서 시작"이 가능한 사람을 찾는다 |
| `and maintain` | 이미 있는 코드를 고치고 지킨다 | **가장 많이 흘려 읽는 단어**. 보드 리비전이 바뀌고, 부품이 EOL 되고, 필드 버그가 올라오고, 릴리스 브랜치가 갈라지는 걸 견디는 코드베이스를 운영해 봤는지 묻는다. 신생 회사가 이 단어를 쓴 건 "빨리 짠 코드가 6개월 뒤 자기 발등을 찍는" 상황을 이미 겪고 있거나 두려워한다는 신호다 [추정] |
| `embedded firmware` | 앱이 아니라 펌웨어 | 리셋 벡터·링커 스크립트·메모리 맵을 직접 건드리는 층 |
| `in C/C++` | C 또는 C++ | 슬래시가 중요하다. "둘 다 쓴다"는 뜻이고, 현업에선 보통 **드라이버/ISR은 C, 상위 로직은 C++ 서브셋**으로 갈린다. 요건(Requirement)에도 `C and/or C++`이라 써서 한쪽만 강해도 받는다는 뜻 |
| `targeting` | 타깃 빌드 | 호스트에서 돌리는 코드가 아니라 **크로스 컴파일** 한다는 뜻. 툴체인·링커·이미지 포맷을 다룰 줄 알아야 한다 |
| `ARM-based SoCs` | Cortex-A급 SoC | context 2.2의 단서(System Test 공고 "Qualcomm chipsets", Embedded Application 공고 "Android")로 보면 **Qualcomm Cortex-A SoC + Android** 쪽 [추정]. 이쪽 펌웨어는 커널 드라이버·firmware blob·부트로더 영역 |
| `and microcontrollers` | Cortex-M급 MCU | On-Device AI 공고의 "Ambiq-class MCUs" 단서 [확인됨][11]. **always-on MCU + RTOS** 쪽 [추정] |
| (문장 전체) | 두 종류의 ARM 타깃 | **가장 중요한 해석**: 이 회사의 기기는 프로세서가 하나가 아니다. 한 사람이 SoC 쪽과 MCU 쪽을 **둘 다** 건드린다. 그래서 "SoC와 MCU를 어떻게 나눌 건가"가 시스템 설계 질문으로 반드시 나온다 |

### 이 문장이 Responsibility 1번인 이유
나머지 6개 항목(BSP·전력·OTA·AI·factory·디버깅)은 전부 이 문장 위에서 돌아간다. 즉 J01은 **다른 모든 노트의 바탕**이고, 면접에서는 "기본기 확인" 구간이다. 여기서 흔들리면 뒤가 안 열린다.

---

## 1. Hark에서 실제로 하게 될 일 (추정)

아래는 전부 `[추정]`이다. 근거는 context 파일 2.2절(채용 공고 57개에서 뽑은 구조 단서)과 2.7절이다. 면접에서 말할 때는 "제 추측인데요"를 붙이고, 확인 질문으로 되돌려야 한다.

### 1.1 추정 컴퓨트 구조

```
        ┌──────────────────────────────────────────────────┐
        │  Qualcomm SoC  (Cortex-A, Android)               │
        │  - 앱/에이전트 런타임, 디스플레이(있다면), 네트워크 │
        │  - Hexagon DSP: 음성 추론 일부                     │
        │  - 커널 드라이버 / HAL / firmware blob 로딩        │
        └───────▲──────────────────────┬───────────────────┘
                │ IPC (UART / SPI /    │ power enable, reset
                │ SDIO / shared mem)   ▼
        ┌───────┴──────────────────────────────────────────┐
        │  always-on MCU (Cortex-M, RTOS)   ← 이 역할의 중심 │
        │  - 센서 허브, 버튼/햅틱, 전원 상태 머신            │
        │  - I2S/PDM 마이크 → wake word → SoC wake          │
        │  - PMIC / fuel gauge 관리                         │
        └───┬──────────┬──────────┬──────────┬─────────────┘
          I2C 센서들 · SPI 플래시/라디오 · I2S 마이크 · UART 콘솔
        ┌──────────────────────────────────────────────────┐
        │  라디오 서브시스템: Cellular / Wi-Fi / BT /        │
        │  GNSS / NFC / UWB  (벤더 펌웨어, 호스트가 로드)    │
        └──────────────────────────────────────────────────┘
```

### 1.2 하루가 어떤 모습일지

| 시간대 | 하는 일 | J01과의 관계 |
|---|---|---|
| 오전 | 어제 올린 PR 리뷰 코멘트 반영, EVT2 보드에서 회귀 하나 재현 | maintain |
| 오전~점심 | HW 엔지니어가 "EVT2에서 센서 전원 레일이 바뀌었다"고 알려 옴 → devicetree overlay 또는 board config 분기 추가 | 보드 리비전 유지보수 |
| 오후 | 새 햅틱 드라이버 작성(I2C), 유닛 테스트, 보드에서 파형 확인. AI 팀이 "arena를 40KB 더 달라"고 요청 → 링커 맵 보고 SRAM 재배치 검토 | develop · 메모리 예산 |
| 저녁 | 나이틀리 빌드가 flash 사용량 2% 증가 경고 → `bloaty` 비교로 원인 커밋 찾기 | maintain |

### 1.3 한 주~한 분기 단위로 보면

- **주 단위**: 릴리스 브랜치에 체리픽, 보드 팜(board farm)에서 야간 테스트 결과 triage, 벤더 SDK 버전 업 검토.
- **보드 리비전 단위(EVT1 → EVT2 → DVT)**: 새 보드 도착 → 전원 시퀀스 확인 → 기존 이미지 부팅 시도 → 바뀐 핀맵/레일 반영 → "구 보드도 계속 빌드되게" 유지. 이 "둘 다 지원"이 `maintain`의 실체다.
- **분기 단위**: 툴체인 업그레이드, RTOS/Zephyr 버전 업, 코드 구조 리팩터링, MISRA/정적분석 도입 여부 결정.

### 1.4 이 역할이 소유할 산출물 (추정)

| 산출물 | 설명 |
|---|---|
| MCU 펌웨어 이미지 + 부트로더 | RTOS + BSP + 드라이버 + 앱 로직(서명된 바이너리), MCUboot 또는 벤더 부트로더 (J04) |
| SoC 쪽 일부 | 커널 드라이버 / devicetree / firmware blob 패키징 일부 [추정] |
| factory 이미지 | 양산 라인 전용 테스트 펌웨어 (J06) |
| 빌드/CI 설정 + 릴리스 번들 | CMake/west/Kconfig, GitHub Actions, HIL 러너, 이미지 버전 매니페스트 |

---

## 2. 핵심 개념

### 2.1 SoC와 MCU는 "같은 ARM"이 아니다

| 항목 | Cortex-A SoC (예: Qualcomm + Android) | Cortex-M MCU (예: Ambiq Apollo급) |
|---|---|---|
| 주소 공간 | MMU, 가상 주소, 페이지 테이블 | MPU(있으면), 물리 주소 직결 |
| 실행 환경 | Linux/Android 커널 + userspace | RTOS 또는 bare-metal, 단일 주소 공간 |
| 코드 위치 | eMMC/UFS에서 DRAM으로 로드 | 내장 flash에서 XIP 또는 SRAM 복사 |
| 부트 체인 | BootROM → 벤더 1차 부트 → TF-A → 부트로더 → 커널 | BootROM(있으면) → 부트로더 → 앱 |
| 메모리 | GB 단위 DRAM | 수백 KB ~ 수 MB SRAM |
| 전력 | 수백 mW ~ W | 수십 µA ~ 수 mW |
| 빌드 산출물 / 시스템 | `.ko`, `.dtb`, 파티션 이미지 · Yocto 또는 AOSP | `.elf` → `.bin` → 서명 이미지 · CMake+Ninja 또는 west |
| 디버깅 | `dmesg`, ftrace, kgdb, JTAG(드묾) | JTAG/SWD 상시, RTT, ITM |
| "펌웨어"의 뜻 | 커널 드라이버 + 벤더 blob | 제품 코드 전부 |

**면접에서 쓸 한 줄**: "SoC 쪽은 OS가 이미 추상화해 준 위에 드라이버를 얹는 일이고, MCU 쪽은 그 추상화를 내가 만드는 일이다. 같은 ARM이어도 실패 모드가 다르다 — SoC는 커널 패닉과 스케줄링 지연, MCU는 HardFault와 스택 오버플로다."

### 2.2 기능을 SoC에 둘까 MCU에 둘까

always-on 기기의 핵심 설계 결정이다. 판단 기준:

| 기준 | MCU로 | SoC로 |
|---|---|---|
| 전력 | 상시 켜져 있어야 하면 MCU | 필요할 때만 깨우면 SoC |
| 레이턴시 결정성 | µs~ms 단위 하드 리얼타임(오디오 DMA, 센서 타이밍) | ms~수십 ms 허용(앱 로직) |
| 메모리·연산 / 가용성 | 수백 KB와 작은 INT8 모델로 끝나고, 죽으면 안 되는 것(전원 상태 머신, 워치독) | 수 MB~GB, 큰 모델·네트워크·멀티미디어. 재부팅해도 되는 것 |

전형적 분담: **MCU가 마이크를 상시 듣고 wake word를 검출 → SoC를 깨운다.** SoC가 켜지는 데 수백 ms가 걸려도, MCU가 오디오를 링버퍼에 담아 두었다가 넘겨주면 사용자는 끊김을 못 느낀다. 이 "pre-roll buffer" 설계는 J05/S05에서 다룬다.

### 2.3 펌웨어 코드베이스의 층 구조

```
  app/   제품 로직: 전원 상태 머신, 이벤트 라우팅, SoC IPC 프로토콜
  svc/   서비스: 오디오 파이프라인, 센서 퓨전, OTA 에이전트, 텔레메트리
  drv/   자체 드라이버: 센서/햅틱/PMIC 칩 드라이버 (버스는 HAL로)
  os/    RTOS 추상화: task, queue, mutex, timer에 대한 얇은 래퍼
  bsp/   보드: 핀맵, 클럭, 전원 시퀀스, 보드 리비전 분기
  hal/   벤더 SDK / CMSIS / Zephyr device driver
  ─────────────────────────────────────────────────────────
  ARM Cortex-M + 주변장치
```

이 구조의 목적은 **"보드가 바뀌어도 위 세 층은 안 바뀌게"** 하는 것이다. `maintain`의 핵심 설계다.

- `app`, `svc`는 레지스터를 직접 만지지 않는다(호스트 유닛 테스트를 위해). `drv`는 "버스 핸들 + 슬레이브 주소"를 주입받는다 — 하드코딩된 `I2C1`이 드라이버 안에 있으면 보드 리비전 대응이 지옥이 된다.
- `os` 래퍼를 둘지 말지는 논쟁거리다. FreeRTOS → Zephyr 전환 가능성이 있으면 두고, RTOS가 확정이면 굳이 안 둔다(래퍼가 디버깅을 어렵게 한다).

### 2.4 빌드 시스템 — 실제로 쓰는 것들

| 도구 | 어디서 쓰나 | 특징 | 주의 |
|---|---|---|---|
| Make | 소규모, 벤더 예제 | 직관적, 의존성 관리 취약 | 증분 빌드 버그가 잘 숨는다 |
| CMake + Ninja | 현대 MCU 프로젝트의 사실상 표준 | 툴체인 파일로 크로스 컴파일, `compile_commands.json` 생성 | 툴체인 파일에서 `CMAKE_TRY_COMPILE_TARGET_TYPE`를 `STATIC_LIBRARY`로 둬야 링크 테스트가 실패하지 않는다 |
| west + CMake + Kconfig + devicetree | Zephyr | 멀티 리포 관리(`west.yml` 매니페스트), 보드/SoC 추상화 | 학습 곡선이 가장 가파르다 |
| Yocto / AOSP 빌드 | Cortex-A Linux BSP / Android SoC | 레이어 또는 벤더 트리 위에서 합성 | 빌드 시간이 시간 단위, 벤더 릴리스 주기에 묶인다 |

MCU 쪽 선택지는 보통 둘 중 하나다. ① **Zephyr를 통째로** — 보드 포팅(`boards/`), devicetree, Kconfig, `west build`. BSP·드라이버·OS·빌드가 한 묶음. ② **벤더 SDK + CMake를 직접 조립** — FreeRTOS + 벤더 HAL + 자체 BSP. 통제력은 크지만 BSP를 전부 직접 만든다. Hark처럼 "Ambiq-class MCU"를 쓴다면 [추정] 벤더 SDK가 FreeRTOS 예제 중심이면 ②, Zephyr 보드 지원이 성숙하면 ①이 유리하다. 면접에서 물으면 **"이미 뭘 쓰고 계신지 먼저 묻고, 안 정했다면 팀 크기와 벤더 지원 성숙도로 고른다"**가 정답에 가깝다.

### 2.5 툴체인 (실제 이름)

| 툴체인 | 정체 | 비고 |
|---|---|---|
| Arm GNU Toolchain (`arm-none-eabi-gcc`) | Arm이 배포하는 GCC 기반 베어메탈 툴체인 | 가장 널리 쓰임. 무료 |
| Arm Compiler for Embedded 6 (`armclang`) | Arm 상용 LLVM 기반 컴파일러 | Keil MDK와 함께. MicroLib 제공. 유료 |
| LLVM Embedded Toolchain for Arm | Arm이 배포하는 LLVM + picolibc/newlib | Zephyr 지원 여부는 버전마다 다름 |
| Zephyr SDK | Zephyr용 GCC 기반 크로스 툴체인 묶음 | `west`가 기대하는 기본 툴체인 |

핵심 플래그(자세한 설명은 C01 §9.2):

```sh
# 예: Cortex-M4F 타깃 — 컴파일과 링크에서 실제로 쓰는 조합
ARCH="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"

arm-none-eabi-gcc $ARCH -Og -g3 -Wall -Wextra -Werror -fno-common \
  -ffunction-sections -fdata-sections -MMD -MP -c src/main.c -o build/main.o

arm-none-eabi-gcc $ARCH -T link/app.ld -nostartfiles \
  -Wl,--gc-sections -Wl,-Map=build/app.map -Wl,--print-memory-usage \
  build/main.o -o build/app.elf
```

- `-mfloat-abi=hard`와 `-mfpu`는 **모든 오브젝트와 라이브러리가 같아야** 한다. 다르면 링크 시 "uses VFP register arguments, output does not" 에러가 난다. 벤더 precompiled 라이브러리를 섞을 때 자주 터진다.
- `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections`는 안 쓰는 함수를 잘라낸다. 다만 **벡터 테이블처럼 참조되지 않지만 필요한 심볼**은 링커 스크립트에서 `KEEP()`으로 지켜야 한다.
- `-Wl,--print-memory-usage`는 GNU ld가 MEMORY 영역별 사용량을 출력한다. CI 회귀 감시에 쓰기 좋다. `-fno-common`은 중복 정의된 전역 변수를 링크 에러로 만든다(최신 GCC 기본값이지만 버전마다 다름).

### 2.6 C와 C++를 어떻게 나누나

**C++가 임베디드에서 공짜인 것**: 클래스와 멤버 함수(가상 함수가 없으면 C 구조체 + 함수와 동일), `constexpr`, 템플릿(코드 생성 비용은 있음), 참조, 오버로딩, `enum class`, 네임스페이스, RAII.

**C++가 비용을 내는 것**: 예외, RTTI, `std::function`/`std::string`/`std::vector`(동적 할당), 가상 함수(vtable + 간접 호출), 정적 객체의 동적 초기화, `iostream`(코드 크기 폭발).

양산 MCU 펌웨어의 흔한 플래그 조합: `-fno-exceptions`(unwind 테이블 수십 KB 제거), `-fno-rtti`(`typeid`/`dynamic_cast` 제거), `-fno-threadsafe-statics`(함수 지역 static의 가드 락 제거 — 단일 스레드 초기화 전제), `-fno-use-cxa-atexit`(전역 소멸자 등록 제거, 어차피 종료 안 함).

```cpp
// RAII로 critical section을 다루는 전형적 패턴 — 런타임 비용 0에 가깝다
class CriticalSection {
public:
    CriticalSection() : saved_(__get_PRIMASK()) { __disable_irq(); }
    ~CriticalSection() { if (!saved_) __enable_irq(); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
private:
    uint32_t saved_;
};

void push_sample(int16_t s) {
    CriticalSection cs;   // 진입 시 인터럽트 끔
    ring_push(s);
}                         // 스코프 끝에서 자동 복원 — early return이 있어도 안전
```

이게 C++를 쓰는 가장 설득력 있는 이유다. C로 쓰면 `enter()`/`exit()` 짝을 사람이 맞춰야 하고, 중간에 `return`이 하나 끼면 인터럽트가 영영 꺼진 채로 남는다.

**언어 경계선 제안 (면접에서 말할 수 있는 기준)**

| 층 | 언어 | 이유 |
|---|---|---|
| startup, 벡터 테이블, ISR | C (또는 어셈블리) | ABI가 단순하고 무슨 코드가 생기는지 눈에 보여야 한다 |
| HAL / 벤더 SDK | C (그대로) | 벤더가 C로 준다. 굳이 감싸지 않는다 |
| 드라이버 | C 또는 얇은 C++ | 팀 합의 문제. C로 두면 벤더 코드와 섞기 쉽다 |
| 서비스/앱 로직, 상태 머신 | C++ 서브셋 | RAII, `enum class`, 템플릿 기반 상태 머신이 실수를 줄인다 |
| 호스트 유닛 테스트 | C++ (GoogleTest 등) | 테스트 코드는 제약이 없다 |

**중요한 함정 — static initialization order fiasco**: 서로 다른 번역 단위의 전역 C++ 객체 초기화 순서는 표준이 정의하지 않는다. 펌웨어에서는 여기에 더해 `__libc_init_array()`(전역 생성자 호출)가 **클럭/전원 설정 전에** 돌 수 있다. 그래서 "전역 생성자에서 하드웨어를 만지지 않는다"가 철칙이다. 초기화는 명시적 `init()` 호출로 순서를 못 박는다.

### 2.7 보드 리비전을 한 코드베이스에서 지원하기

세 가지 방법이 있고, 셋 다 실제로 쓰인다.

| 방식 | 구현 | 장점 | 단점 |
|---|---|---|---|
| 컴파일 타임 분기 | `-DBOARD_REV=2`, 또는 보드별 config 파일 | 코드 크기 최소, 명확 | 이미지가 리비전 수만큼 늘어남. 잘못된 이미지를 굽는 사고 |
| 런타임 감지 | 보드에 저항 스트랩 → ADC/GPIO 읽기, 또는 EEPROM의 board ID | 이미지 하나로 모든 보드 | 코드가 커지고, 감지 코드 자체가 하드웨어에 의존 |
| 데이터 주도 | Zephyr devicetree overlay, 또는 자체 board descriptor 테이블 | 코드는 그대로, 데이터만 교체 | 빌드 시스템 이해가 필요 |

실무에서는 **섞어 쓴다**: 핀맵/레일 같은 정적 사실은 devicetree/보드 헤더로, "이 리비전에선 센서 A 대신 B가 붙는다" 같은 구조적 차이는 컴파일 타임 분기로, 그리고 **부팅 로그에 감지한 board ID를 찍어서** 잘못된 이미지를 바로 알아차리게 한다.

```c
/* 런타임 board ID 감지 — 저항 divider를 ADC로 읽는 흔한 방식.
   임계값은 실제 저항값으로 계산해야 한다(여기 숫자는 예시).            */
typedef enum { BOARD_UNKNOWN = 0, BOARD_EVT1, BOARD_EVT2, BOARD_DVT } board_rev_t;

static board_rev_t g_board_rev = BOARD_UNKNOWN;

void board_init(void)
{
    uint16_t mv = adc_read_mv(ADC_CH_BOARD_ID);   /* BSP 제공 */
    if      (mv <  200) g_board_rev = BOARD_EVT1; /* 0V 스트랩   */
    else if (mv < 1200) g_board_rev = BOARD_EVT2; /* 1/3 divider */
    else if (mv < 2300) g_board_rev = BOARD_DVT;  /* 2/3 divider */
    else                g_board_rev = BOARD_UNKNOWN;

    LOG_INF("board rev = %d (adc %u mV)", (int)g_board_rev, mv);
    /* 미지의 보드에서 전원 시퀀스를 추측으로 돌리면 부품이 탄다. */
    if (g_board_rev == BOARD_UNKNOWN) enter_safe_mode();
}
```

**Zephyr의 보드 리비전 기능**: Zephyr는 `west build -b <board>@<revision>` 형태로 보드 리비전을 공식 지원한다. 리비전별 `.overlay`/`.conf` 파일이 자동 선택된다. 다만 **보드 정의 포맷은 Zephyr 버전에 따라 크게 바뀌었다**(hardware model v2 도입 이후 `board.yml` 기반). 버전마다 다르므로 실제 쓰는 Zephyr 버전 문서를 봐야 한다.

### 2.8 버전 관리 — 이미지가 여러 개일 때

Hark 기기는 최소 세 종류의 펌웨어가 있다 [추정]: MCU 이미지, SoC 쪽 이미지(Android 빌드 포함), 라디오 펌웨어(벤더 제공). 여기에 부트로더까지 치면 네 개다.

| 버전 요소 | 내용 | 왜 |
|---|---|---|
| `MAJOR.MINOR.PATCH` | semver 형태 | 사람이 읽고, 롤백 정책의 기준이 된다 |
| 빌드 메타데이터 | `+1234` 또는 git short SHA | 정확히 어느 커밋인지 특정 |
| 빌드 종류 | `dev` / `factory` / `release` | factory 이미지를 필드에 못 내보내게 |
| 단조 증가 카운터 | anti-rollback용 security counter | 서명 검증과 별개. MCUboot의 security counter가 이 역할 (J04에서) |
| 번들 버전 | 여러 이미지를 묶은 릴리스 버전 | 사용자/지원팀에게 보이는 "기기 소프트웨어 버전"은 하나여야 한다 |

**핵심 설계**: 보이는 건 **번들 버전 하나**이고, 매니페스트가 각 이미지의 버전과 해시를 잠근다.

```
release-bundle 1.4.2
├── bootloader   0.9.1  (sha256: ...)
├── mcu-app      1.4.2  (sha256: ...)
├── soc-image    1.4.0  (sha256: ...)
└── radio-fw     3.2.7  (벤더 제공, sha256: ...)
```

그리고 **호환성 매트릭스**를 코드로 강제한다. MCU가 부팅할 때 SoC와 IPC 버전을 교환해서 맞지 않으면 안전 모드로 떨어지는 식이다. 이게 없으면 "OTA 중 전원이 끊겨서 MCU만 업데이트됨" 같은 상황에서 정의되지 않은 동작이 나온다.

```c
/* 빌드 시스템이 생성하는 헤더의 전형적 모습 (version.h.in → version.h) */
#define FW_VERSION_MAJOR   1
#define FW_VERSION_MINOR   4
#define FW_VERSION_PATCH   2
#define FW_GIT_SHA         "a3f91c2"
#define FW_BUILD_TYPE      "release"
#define FW_IPC_PROTO_VER   3        /* SoC와 맞춰야 하는 별도 버전 */

/* 문자열은 링커 맵에서 찾기 쉽게 고정 섹션에 둔다 */
__attribute__((used, section(".fw_version")))
static const char kVersionBanner[] =
    "HARKFW " "1.4.2" "+" FW_GIT_SHA " " FW_BUILD_TYPE;
```

`__attribute__((used))`가 없으면 `--gc-sections`가 지워 버린다. 이렇게 두면 `strings app.bin | grep HARKFW`로 **바이너리만 보고도 버전을 알 수 있다**. 필드에서 회수된 기기를 분석할 때 이게 생명을 구한다.

### 2.9 코드 리뷰 — 펌웨어에서 특별히 보는 것

일반 소프트웨어 리뷰 항목 외에 펌웨어 고유 체크리스트:

| 항목 | 무엇을 보나 |
|---|---|
| ISR 안전성 | ISR에서 블로킹 API, `malloc`, 로그 출력, 긴 루프를 쓰지 않았나. `FromISR` 변종을 썼나 |
| `volatile` | 하드웨어 레지스터·ISR 공유 변수에 붙었나. 반대로 남발해서 성능을 죽이지 않았나 |
| 원자성 | 32비트 이상 공유 변수의 read-modify-write가 보호되나. `volatile`은 원자성을 주지 않는다 |
| 스택 | 큰 지역 배열(예: `uint8_t buf[4096]`)을 스택에 잡지 않았나 |
| 동적 할당 | 런타임 `malloc`이 새로 들어오지 않았나. 들어왔다면 정당화됐나 |
| 에러 경로 | 반환값을 무시하지 않았나. 실패 시 하드웨어가 중간 상태로 남지 않나 |
| 타임아웃 | `while (!(REG & FLAG));` 같은 무한 대기가 없나 |
| 타입 | 정수 승격(integer promotion), 부호 혼용, 비트 시프트 폭 |
| 코드 크기 / 보드 의존성 | 이 PR이 flash/RAM을 얼마나 늘리나. 상위 층에 핀 번호나 레지스터 주소가 새어 들어오지 않았나 |

### 2.10 정적 분석과 코딩 표준

- 가장 싸고 효과 큰 도구는 **컴파일러 경고**다. `-Wall -Wextra -Werror`. 신규 프로젝트는 처음부터 켜야 한다(나중에 켜면 수백 개를 고쳐야 해서 아무도 안 켠다).
- `clang-format`(포맷 논쟁 제거, CI 검사), `clang-tidy`(`compile_commands.json`만 있으면 크로스 프로젝트에도 적용), `cppcheck`(오픈소스, MISRA 애드온 있음, 오탐 있음).
- 상용: Coverity, Klocwork, PC-lint Plus, Polyspace. 비싸고 셋업 비용이 크다.
- **MISRA C:2012**는 자동차·안전 도메인 표준이다. 컨슈머 제품에 전면 적용하는 경우는 드물고, 유용한 규칙(암묵적 변환, 부작용 있는 매크로, 반환값 무시)만 골라 린터 규칙으로 옮기는 게 현실적이다.

### 2.11 테스트 전략 — 하드웨어가 필요한 코드를 어떻게 테스트하나

```
  4. 보드 팜 (HIL)      실제 보드 N대, 야간 실행     느림 / 비쌈 / 진짜
  3. 에뮬레이터          QEMU, Renode               RTOS·타이밍 일부
  2. 온타깃 테스트       보드에서 ztest 실행         드라이버 통합
  1. 호스트 유닛테스트   드라이버를 fake HAL 위에    빠름 / 많이
```

1층을 가능하게 하려면 §2.3의 층 분리가 필요하다. 레지스터 접근이 `hal_i2c_write()` 같은 함수 뒤에 있으면 호스트에서 가짜 구현으로 바꿔치기할 수 있다.

실제 프레임워크: Unity + CMock(C), GoogleTest(C++ 호스트), Zephyr `ztest`(온타깃/시뮬), Zephyr `twister`(테스트 러너), Renode(다중 보드 시뮬레이션).

```c
/* 호스트에서 테스트 가능한 드라이버 형태 — 버스를 주입받는다 */
typedef struct {
    int (*write)(void *ctx, uint8_t addr, const uint8_t *buf, size_t len);
    int (*read)(void *ctx, uint8_t addr, uint8_t *buf, size_t len);
    void *ctx;
} i2c_bus_t;

typedef struct { const i2c_bus_t *bus; uint8_t addr; } tempsensor_t;

int tempsensor_read_c(const tempsensor_t *s, int16_t *out_centi_c)
{
    uint8_t reg = 0x00, raw[2];
    if (s->bus->write(s->bus->ctx, s->addr, &reg, 1) != 0) return -1;
    if (s->bus->read(s->bus->ctx, s->addr, raw, sizeof raw) != 0) return -1;
    /* 12-bit, 0.0625 C/LSB 인 가상의 센서 */
    int16_t ticks = (int16_t)(((uint16_t)raw[0] << 4) | (raw[1] >> 4));
    if (ticks & 0x0800) ticks |= (int16_t)0xF000;   /* 부호 확장 */
    *out_centi_c = (int16_t)((int32_t)ticks * 625 / 100);
    return 0;
}
```

이 드라이버는 `i2c_bus_t`에 fake를 꽂으면 x86 호스트에서 그대로 테스트된다. 부호 확장 버그, 바이트 순서 버그를 보드 없이 잡는다.

---

## 3. 실무 패턴과 함정

### 3.1 코드 크기·RAM 회귀를 막는 법

```sh
arm-none-eabi-size -A build/app.elf                                 # 영역별 총량
bloaty -d symbols --domain=vm build/app.elf -- baseline/app.elf     # 심볼 단위 diff
west build -t rom_report && west build -t ram_report                # Zephyr 내장 리포트
```

CI에서 베이스라인 대비 델타를 PR에 코멘트하게 하면, "왜 갑자기 12KB가 늘었지?"를 6개월 뒤가 아니라 그날 알 수 있다. 흔한 범인:

| 증상 | 원인 | 대응 |
|---|---|---|
| flash 수 KB~수십 KB 증가 | `printf`류의 `%f` 사용 → 부동소수점 포맷 코드 링크 | 정수 포맷만 쓰거나 경량 printf 사용 |
| 갑자기 20KB+ 증가 | C++ 예외 활성화, `std::string`/`std::function` 유입 | `-fno-exceptions`, ETL 같은 정적 컨테이너 |
| RAM 증가 | 큰 정적 버퍼, RTOS 스택 상향, 링버퍼 크기 | 맵 파일의 `.bss` 상위 심볼 확인 |
| 링커가 미사용 코드를 안 지움 | `-ffunction-sections`/`--gc-sections` 누락 | 플래그 확인, `KEEP()` 남용 여부 확인 |

### 3.2 `maintain`의 실제 모습: 릴리스 브랜치

```
main    ──●──●──●──●──●──●──●──●──●───▶  (개발 계속)
           \                 ↑
release/1.4 ●──●──●          │ cherry-pick
               \             │
                ●(1.4.1) ●(1.4.2)
```

- 필드 버그는 `release/1.4`에서 고치고, **반드시 `main`으로도 포팅**한다. 반대 방향(main에서 고치고 릴리스로 체리픽)도 흔하고, 팀이 하나만 골라 일관되게 지키는 게 중요하다.
- 태그는 빌드가 만든 버전과 1:1이어야 한다. `git describe --tags --dirty --always`를 빌드에 박아 두면 "이 바이너리가 어느 커밋인지"가 영원히 추적된다.
- `--dirty`가 붙은 이미지는 **절대 공장이나 필드로 내보내지 않는다**. CI에서 릴리스 빌드는 dirty면 실패시킨다.

### 3.3 툴체인 업그레이드는 릴리스가 아니라 별도로

GCC 메이저 버전을 올리면 코드 크기와 최적화 동작이 바뀌고, 없던 경고가 쏟아지고, 가끔 정의되지 않은 동작(UB)에 의존하던 코드가 깨진다. 그래서 툴체인 버전은 **컨테이너 이미지나 Zephyr SDK 버전으로 고정**하고("내 노트북에선 되는데"를 없애는 가장 빠른 방법), 업그레이드는 기능 릴리스와 분리된 PR로 크기 델타·전체 HIL을 붙여서 한다. 릴리스 브랜치는 원칙적으로 툴체인을 안 바꾼다.

### 3.4 벤더 SDK를 리포에 어떻게 넣나

| 방식 | 장점 | 단점 |
|---|---|---|
| 그대로 vendor 폴더에 복사 | 단순, 오프라인 빌드 | 업스트림 업데이트 병합이 고통 |
| git submodule / west manifest | 버전이 명확, 업데이트 추적 | SDK가 비공개면 접근 관리 필요 |
| 패치 파일로 관리 | 원본 + 우리 수정이 분리 | 패치 충돌 관리 |

**절대 하면 안 되는 것**: 벤더 SDK 파일을 표시 없이 직접 수정하는 것. 6개월 뒤 SDK를 올릴 때 왜 깨지는지 아무도 모른다. 수정이 필요하면 패치 파일이나 최소한 `/* HARK-PATCH: ... */` 주석 마커를 남긴다.

### 3.5 흔한 함정 정리

| 함정 | 증상 | 원인 | 대응 |
|---|---|---|---|
| 전역 C++ 생성자에서 하드웨어 접근 | 부팅 중 HardFault, 간헐적 | 클럭/전원 설정 전에 `__libc_init_array()`가 실행됨 | 생성자는 순수 메모리 초기화만. 하드웨어는 명시적 `init()` |
| float ABI 불일치 | 링크 에러 또는 이상한 인자 값 | 라이브러리와 앱의 `-mfloat-abi` 불일치 | 모든 대상에 동일 플래그 강제, 툴체인 파일에서 일괄 지정 |
| `-O0`과 `-O2`에서 동작이 다름 | 릴리스 빌드만 깨짐 | `volatile` 누락, UB, 타이밍 의존 delay 루프 | `volatile` 점검, delay는 타이머로, UBSan을 호스트 테스트에 적용 |
| 증분 빌드 오염 | "클린 빌드하면 된다" | 헤더 의존성 누락(`-MMD -MP`) | CI는 항상 클린 빌드 |
| 한 이미지가 여러 보드에서 애매하게 동작 | 특정 리비전에서만 센서 실패 | 보드 감지 없음 또는 잘못된 이미지 | 부팅 로그에 board ID, 이미지에 지원 리비전 명시 |
| 버전 문자열이 바이너리에 없음 | 회수 기기 분석 불가 | `--gc-sections`가 지움 | `__attribute__((used, section(...)))` |
| 로그가 릴리스 빌드에 그대로 | flash 낭비, 정보 노출, 타이밍 변화 | 로그 레벨 빌드 분기 없음 | 컴파일 타임 로그 레벨, 문자열 대신 ID 기반 로깅 고려 |
| 헤더에 `static` 함수 정의 | 코드 크기 증가, 링커 맵 혼란 | 헤더에 구현을 넣음 | `static inline`만 헤더에, 나머지는 `.c`로 |

### 3.6 로깅 — 크기와 타이밍을 같이 생각한다

```c
/* 릴리스에서 문자열 리터럴까지 통째로 지우는 전형적 패턴 */
#if defined(CONFIG_LOG_LEVEL_DEBUG)
#  define LOG_DBG(fmt, ...) log_emit(LOG_LVL_DBG, fmt, ##__VA_ARGS__)
#else
#  define LOG_DBG(fmt, ...) ((void)0)
#endif
```

UART 로그가 **블로킹**이면 타이밍을 바꿔서 버그를 숨긴다. 링버퍼 + DMA, 또는 SEGGER RTT(거의 비침습)로 뺀다(C10). always-on 기기에서 로그 출력은 **전력**이기도 하므로, 로그를 켠 채 배터리 수명을 측정하면 안 된다.

---

## 4. 리서치 — 근거 자료

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| Arm GNU Toolchain (공식 배포) | `arm-none-eabi-gcc` 다운로드와 릴리스 노트 | 릴리스 노트의 "known issues", 지원 코어 목록 | https://developer.arm.com/Tools-and-Software/GNU-Toolchain |
| GCC ARM 옵션 문서 | `-mcpu`, `-mfpu`, `-mfloat-abi`, `-mthumb` 전체 목록 | ARM Options 절 전부. 코어별 유효 조합 | https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html |
| Arm ABI 문서 (abi-aa) | AAPCS(호출 규약), ELF for Arm, C++ ABI | AAPCS32의 레지스터 사용·스택 정렬. 인터뷰에 자주 나옴 | https://github.com/ARM-software/abi-aa |
| Zephyr — 빌드/설정 | CMake+Kconfig+devicetree 통합 빌드 | Build and Configuration Systems 전체 | https://docs.zephyrproject.org/latest/build/index.html |
| Zephyr — west | 멀티 리포 매니페스트, 워크스페이스 | west workspace, manifest 문법 | https://docs.zephyrproject.org/latest/develop/west/index.html |
| Zephyr — 보드 포팅 | 새 보드 추가, 보드 리비전 | Board Porting Guide. **포맷이 버전마다 다름**(hardware model v2 전후) | https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html |
| Zephyr — 애플리케이션 버전 관리 | `VERSION` 파일 → `app_version.h` 생성 | Application version management | https://docs.zephyrproject.org/latest/build/version/index.html |
| Zephyr — C++ 지원 | 어떤 C++ 기능이 켜지고 꺼지는지 | 지원되는 표준, 제약 목록. **버전마다 다름** | https://docs.zephyrproject.org/latest/develop/languages/cpp/index.html |
| Zephyr — twister | 테스트 러너, 보드/시뮬레이터 매트릭스 실행 | Test Runner 절 | https://docs.zephyrproject.org/latest/develop/test/twister.html |
| FreeRTOS 공식 문서 | 커널 API, 포팅, 설정 매크로 | `FreeRTOSConfig.h` 설정 목록 | https://www.freertos.org/Documentation/00-Overview |
| CMake 크로스 컴파일 | 툴체인 파일 작성법 | cmake-toolchains 매뉴얼의 Cross Compiling 절 | https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html |
| bloaty | 바이너리 크기 분석·두 빌드 비교 | README의 `-d symbols`, `--domain` 사용법 | https://github.com/google/bloaty |
| Unity / CMock | C 유닛 테스트 + 자동 mock 생성 | Unity assertion 목록, CMock 사용 흐름 | https://github.com/ThrowTheSwitch/Unity |
| ETL (Embedded Template Library) | 동적 할당 없는 C++ 컨테이너 | `etl::vector`, `etl::string`의 고정 용량 개념 | https://www.etlcpp.com/ |
| Memfault Interrupt 블로그 | 펌웨어 버전 번호, 코드 크기 관리, CI 등 실무 글 | "firmware version numbers", "code size deltas" 글 | https://interrupt.memfault.com/ |
| MISRA 공식 | MISRA C:2012 규칙 문서 판매/개요 | 도입 여부 판단용 개요만 | https://misra.org.uk/ |
| Yocto / AOSP | Cortex-A 쪽 BSP 빌드 흐름 | Yocto의 layer 개념, AOSP의 Setup·Build 절 | https://docs.yoctoproject.org/ · https://source.android.com/docs/setup/build |
| MCUboot 문서 | 이미지 헤더, 버전, security counter | Design 문서의 image format | https://docs.mcuboot.com/ |

> 벤더 SDK(Qualcomm, Ambiq 등)는 상당수가 포털·NDA 배포라 공개 URL이 바뀌거나 접근이 막혀 있다. 면접 준비용으로는 공개 문서(Zephyr 보드 지원, 오픈소스 미러)로 충분하고, 실제 업무에서는 벤더 FAE 채널이 1차 소스가 된다.

---

## 5. 예상 면접 질문

### Q01. Walk me through how a firmware codebase you owned was structured.

**왜 묻나**: `maintain`의 증거. 파일 몇 개짜리 예제가 아니라 **여러 사람이 몇 년을 건드린 코드베이스**를 다뤄 봤는지 본다.

**30초 답변**: 층으로 나눈다 — 벤더 HAL, 보드(BSP), 드라이버, OS 추상화, 서비스, 앱. 규칙은 하나다. 위 층은 아래 층의 레지스터를 모른다. 그래야 보드 리비전이 바뀌어도 상위가 안 바뀌고, 상위를 호스트에서 유닛 테스트할 수 있다. SSD 펌웨어에서도 같은 구조였다 — 호스트 인터페이스 층, 코어 로직, 플래시 채널 드라이버가 나뉘어 있었고, 실리콘이 바뀌면 아래 층만 갈아 끼웠다.

**English answer**: "I think of a firmware repo as layers: vendor HAL at the bottom, then the board support layer that owns pinmux, clocks and power sequencing, then device drivers, then a thin OS abstraction, and application logic on top. The one rule I enforce in review is that upper layers never touch a register or a pin number directly. That buys two things: when the hardware team spins a new board revision, only the BSP layer changes, and the upper layers can be unit tested on the host with a fake bus. On the SSD firmware I worked on, we had the same shape — host interface, core logic, and the flash channel drivers — so when the silicon changed we replaced the bottom and kept the rest."

**꼬리질문**
- "그 경계가 실제로 지켜지던가?" → 지켜지지 않는 순간이 온다. 그래서 리뷰 체크리스트와, 상위 층에서 레지스터 헤더를 include하면 실패하는 빌드 규칙 같은 기계적 장치가 필요하다.
- "OS 추상화 층은 오버엔지니어링 아닌가?" → RTOS가 확정이면 그렇다. 바뀔 가능성이 있거나 호스트 테스트를 하려면 값을 한다. 얇게 유지하는 게 조건이다.
- "테스트는 어디까지 했나?" → 호스트 유닛 테스트가 가장 많고, 보드에서 도는 통합 테스트가 그다음, 야간 HIL이 마지막이다.

### Q02. How do you decide what goes in C and what goes in C++ on a microcontroller?

**왜 묻나**: JD가 `C/C++`라고 슬래시로 썼다. 언어 종교 전쟁을 하는 사람인지, 비용을 계산하는 사람인지 본다.

**30초 답변**: 기준은 "런타임 비용을 내가 설명할 수 있는가"다. 클래스, 참조, `constexpr`, RAII, `enum class`는 C와 코드 크기가 거의 같으니 쓴다. 예외, RTTI, 동적 할당 컨테이너, `iostream`은 끈다. startup과 ISR은 C나 어셈블리로 둔다 — 무슨 코드가 나오는지 눈에 보여야 한다. RAII로 critical section이나 버스 락을 감싸는 게 C++를 쓰는 가장 실용적인 이유다.

**English answer**: "My rule is that I use C++ features whose cost I can explain in terms of generated code. Classes without virtuals, references, constexpr, enum class, and RAII cost essentially nothing over C, and RAII in particular removes a whole class of bugs — an early return that leaves interrupts disabled or a bus lock held. What I turn off is exceptions, RTTI, and anything that allocates: no std::string, no std::function in the hot path. Startup code, the vector table and ISRs stay in C, because I want to be able to read the disassembly and know exactly what happens. And I'd want the team to agree on that subset in writing, because the failure mode is everyone picking a different subset."

**꼬리질문**
- "`-fno-exceptions`를 켜면 STL의 어떤 부분이 깨지나?" → 할당 실패나 범위 위반에서 예외를 던지는 컨테이너는 사실상 못 쓴다. `std::vector::at`, 할당 실패 경로. 그래서 ETL 같은 고정 용량 컨테이너를 쓴다.
- "템플릿은 코드 크기를 늘리지 않나?" → 인스턴스화마다 코드가 생긴다. 타입이 몇 개로 제한되면 괜찮고, 무제한이면 링커 맵에서 바로 보인다. `-ffunction-sections`와 identical code folding이 일부 흡수한다.
- "static initialization order fiasco를 펌웨어에서 어떻게 피하나?" → 전역 생성자에서 하드웨어를 안 만진다. 초기화 순서가 중요한 건 전부 명시적 `init()` 호출로 옮긴다.

### Q03. What does "maintaining" firmware mean to you? How do you support three board revisions from one codebase?

**왜 묻나**: JD의 `maintain`. 이 질문은 거의 확실히 나온다.

**30초 답변**: 정적인 사실(핀맵, 레일, 클럭)은 데이터로 뺀다 — Zephyr면 devicetree overlay, 아니면 보드 descriptor 테이블. 구조적 차이(이 리비전엔 다른 센서가 붙는다)는 컴파일 타임으로 나눈다. 그리고 부팅할 때 board ID를 저항 스트랩이나 EEPROM에서 읽어 로그에 찍고, 모르는 보드면 안전 모드로 떨어진다. 잘못된 이미지를 구운 걸 30초 안에 알아야지, 부품 태우고 알면 안 된다.

**English answer**: "Maintaining means the codebase survives hardware churn. I push static facts — pin mux, rails, clock config — into data, either a devicetree overlay or a board descriptor table, so a new revision is a data change, not a code change. Structural differences, like a different sensor part on EVT2, I handle with compile-time configuration. On top of that I always add a board ID — a resistor strap read by ADC, or an ID in EEPROM — printed in the boot banner, and if the ID is unknown the firmware refuses to run the full power sequence and boots into a safe mode. The worst maintenance bug is flashing the wrong image onto a board and damaging a part, and the board ID check is cheap insurance against that."

**꼬리질문**
- "왜 런타임 감지가 컴파일 타임보다 낫나?" → 항상 낫진 않다. 이미지 수를 줄이고 공장/현장 혼동을 막는 대신 코드와 RAM을 조금 더 쓴다. 리비전이 많고 병렬로 돌아가는 초기 단계에선 런타임이 유리하다.
- "구 보드 지원을 언제 끊나?" → 하드웨어 팀과 합의해서 명시적으로 EOL 날짜를 정하고, 그 커밋에서 코드를 지운다. 안 지우면 영원히 남는다.
- "세 리비전 전부 테스트를 어떻게 돌리나?" → 보드 팜에 리비전별로 최소 한 대씩 두고 나이틀리로 같은 스위트를 돌린다.

### Q04. You ship an MCU image, an SoC image, and vendor radio firmware. How do you version that?

**왜 묻나**: `ARM-based SoCs and microcontrollers` — 복수형. 멀티 이미지 제품의 릴리스를 생각해 봤는지 본다.

**30초 답변**: 사용자에게 보이는 건 번들 버전 하나고, 그 안의 매니페스트가 각 이미지의 버전과 해시를 잠근다. 각 이미지는 semver + git SHA + 빌드 종류를 바이너리 안에 박아 둔다. 그리고 부팅 시 MCU와 SoC가 IPC 프로토콜 버전을 교환해서 안 맞으면 안전 모드로 간다 — OTA가 중간에 끊겨서 한쪽만 올라간 상황이 반드시 생기기 때문이다.

**English answer**: "Users and support should see one number, so I'd define a release bundle version, and the bundle manifest pins the exact version and hash of every component — bootloader, MCU app, SoC image, radio firmware. Each binary carries its own semver plus the git SHA and build type, embedded in a known section so I can recover it with strings from a returned unit. Separately from the marketing version, I keep an IPC protocol version that MCU and SoC exchange at boot. If they don't match, the device comes up degraded instead of doing something undefined, because partial updates will happen — a battery dies mid-OTA and only one side got the new image."

**꼬리질문**
- "anti-rollback은 어디에 넣나?" → 버전 문자열과 분리된 단조 증가 security counter를 쓴다. MCUboot에 그 개념이 있다. 자세한 건 J04.
- "라디오 펌웨어는 벤더가 주는데 버전을 어떻게 통제하나?" → 번들 매니페스트에 벤더 버전과 해시를 고정하고, 벤더 릴리스 노트를 근거로 승인 절차를 둔다.
- "`git describe --dirty`가 dirty면?" → 릴리스 빌드는 CI에서 실패시킨다. 추적 불가능한 바이너리는 출하 대상이 아니다.

### Q05. Describe your build system. How do you make a build reproducible?

**왜 묻나**: 신생 팀은 빌드가 사람마다 다르다. 그걸 정리해 본 사람인지 본다.

**30초 답변**: CMake + Ninja를 기본으로, 툴체인 파일에서 컴파일러 경로와 ARM 플래그를 한 번에 고정한다. 툴체인 자체는 컨테이너 이미지나 Zephyr SDK 버전으로 핀한다. 의존성은 submodule이나 west manifest로 커밋 해시까지 고정한다. CI는 항상 클린 빌드를 하고, 빌드 산출물에 툴체인 버전과 git SHA를 기록한다. "내 노트북에선 된다"를 없애는 게 목표다.

**English answer**: "CMake with Ninja for the MCU side, with a toolchain file that pins the compiler and all the ARM flags in one place, so nobody can accidentally build half the objects with a different float ABI. The toolchain itself is pinned — a container image, or the Zephyr SDK version if we're on Zephyr — so CI and my laptop compile with the same binary. Third-party code is pinned by commit through submodules or a west manifest. CI always builds clean, and the build stamps the toolchain version and git describe output into the image. Reproducibility is not an ideal for me, it's what makes a field bug report actionable six months later."

**꼬리질문**
- "증분 빌드가 깨진 걸 어떻게 알았나?" → 로컬은 되는데 CI가 실패하거나 그 반대. 헤더 의존성(`-MMD -MP`)과 생성 헤더가 주범이다.
- "Zephyr의 Kconfig와 devicetree 차이는?" → Kconfig는 "소프트웨어 기능을 켤까 말까", devicetree는 "하드웨어가 무엇이 어떻게 붙어 있나". 섞으면 나중에 고생한다.
- "빌드 시간이 길어지면?" → ccache, 병렬 빌드, 불필요한 헤더 의존 제거. CI는 보통 병렬 잡으로 나눈다.

### Q06. How do you keep flash and RAM usage from creeping up release over release?

**왜 묻나**: 컨슈머 기기는 메모리를 나중에 늘릴 수 없다. 그리고 J05(AI 팀 메모리 예산)로 이어지는 다리다.

**30초 답변**: 빌드마다 크기를 기록하고, PR에 델타를 자동 코멘트한다. `arm-none-eabi-size`로 총량, `bloaty`로 두 빌드의 심볼 단위 차이, Zephyr면 `rom_report`/`ram_report`. 예산을 서브시스템별로 미리 나눠 두고(오디오 링버퍼 N KB, 모델 arena M KB, RTOS 스택 합계 K KB) 넘으면 리뷰에서 막는다. 원인은 대개 `printf`의 부동소수점 포맷, C++ 예외, 커진 정적 버퍼 셋 중 하나다.

**English answer**: "I treat code size like a test that can fail. Every CI build records flash and RAM per region, and the PR gets a comment with the delta against main. For attribution I use bloaty to diff two ELFs at symbol level, or the built-in ROM and RAM reports if we're on Zephyr. I also want an explicit budget per subsystem up front — so many kilobytes for audio buffers, so many for the inference arena, so many for RTOS stacks — because on an always-on device the memory fight with the AI team is a real negotiation, and it goes much better when both sides are looking at the same table. In my experience the usual culprits are floating-point printf formatting, C++ exceptions sneaking back on, and static buffers that someone doubled 'just to be safe'."

**꼬리질문**
- "RTOS 스택 크기는 어떻게 정하나?" → 워터마크 기반. FreeRTOS의 `uxTaskGetStackHighWaterMark`, Zephyr의 스택 분석으로 실측하고 여유를 둔다. C04 §10.3.
- "flash가 꽉 차면 마지막 수단은?" → 로그 문자열 제거/ID화, 링크 타임 최적화(`-flto`), `-Os`, 안 쓰는 기능 Kconfig로 끄기. LTO는 디버깅이 어려워지니 마지막에.
- "XIP와 SRAM 복사 중 뭘 고르나?" → 실행 속도와 전력, flash 접근 지연에 달렸다. 핫 루프만 RAM으로 옮기는 절충이 흔하다.

### Q07. What do you look for when you review a firmware pull request?

**왜 묻나**: 시니어 신호. 팀이 작을수록 리뷰가 유일한 품질 장치다.

**30초 답변**: 일반 리뷰 위에 펌웨어 전용 항목을 얹는다 — ISR에서 블로킹/할당/로그를 하는지, 공유 변수에 `volatile`과 원자성 보호가 맞게 걸렸는지, 무한 대기 루프에 타임아웃이 있는지, 큰 배열이 스택에 있는지, 에러 경로에서 하드웨어가 중간 상태로 남는지, 그리고 상위 층에 레지스터나 핀 번호가 새어 들어왔는지. 마지막으로 코드 크기 델타를 본다.

**English answer**: "On top of normal review — naming, tests, error handling — I have a firmware-specific list. Does this ISR do anything blocking, allocating, or logging? Is every shared variable either volatile and atomic-width, or protected by a critical section? Does every hardware wait loop have a timeout and a defined failure path? Are there large arrays on the stack? If the function fails halfway, does the peripheral get left in a half-configured state? And does anything above the BSP layer reference a pin number or a register? I also look at the size delta, because a PR that adds twelve kilobytes needs a sentence explaining why."

**꼬리질문**
- "리뷰에서 가장 자주 잡는 버그는?" → 반환값 무시와 타임아웃 없는 대기. 둘 다 정상 상황에선 안 보이다가 필드에서 터진다.
- "리뷰로 못 잡는 건?" → 타이밍·경쟁 상태. 그건 리뷰가 아니라 설계 규칙(누가 누구를 깨우는가)과 계측으로 잡는다.
- "리뷰 속도와 품질을 어떻게 맞추나?" → 기계가 할 수 있는 건 기계에게(포맷, 린트, 크기). 사람은 설계와 실패 모드만 본다.

### Q08. How do you test firmware in CI when the code needs real hardware?

**왜 묻나**: 컨슈머 제품은 회귀가 곧 리콜이다. 테스트 피라미드를 아는지 본다.

**30초 답변**: 네 층으로 나눈다. 호스트 유닛 테스트(드라이버를 fake 버스 위에서), 시뮬레이터(QEMU/Renode로 RTOS 로직), 보드에서 도는 온타깃 테스트, 그리고 야간 HIL 보드 팜. 1층이 가능하려면 코드가 버스를 주입받는 구조여야 하고, 그게 앞서 말한 층 분리의 진짜 이유다. HIL은 전원 제어(USB 릴레이), 플래싱, 로그 수집, 그리고 보드가 벽돌이 됐을 때 복구하는 경로까지 갖춰야 유지된다.

**English answer**: "Four layers. Host unit tests where drivers take an injected bus interface, so I can test sign extension, byte order and error paths on x86 in milliseconds. Emulation with QEMU or Renode for scheduler and protocol logic. On-target tests that run on a board — ztest if we're on Zephyr, run through twister. And a nightly hardware-in-the-loop farm with at least one board per revision, with switchable power, automated flashing, and log capture. The part teams underestimate is recovery: the farm has to be able to unbrick a board without a human walking over, or the farm goes red and people start ignoring it."

**꼬리질문**
- "HIL 테스트가 플래키하면?" → 플래키 테스트는 버그로 취급하고 격리한다. 무시하기 시작하면 팜 전체가 죽는다.
- "타이밍에 의존하는 테스트는?" → 절대 시간 대신 이벤트 기반 동기화. 꼭 필요하면 넉넉한 마진과 함께.
- "커버리지는 측정하나?" → 호스트 층은 gcov로 의미가 있다. 온타깃 커버리지는 비용 대비 효용이 낮다.

### Q09. What changes about how you write code when the target is a Cortex-A SoC running Linux versus a Cortex-M MCU?

**왜 묻나**: JD가 둘 다 명시했다. 그리고 Don의 갭(Cortex-A/Linux)을 정직하게 다룰 기회.

**30초 답변**: MCU에선 내가 메모리 맵의 주인이고 모든 사이클을 볼 수 있다. 실패 모드는 HardFault, 스택 오버플로, 놓친 인터럽트다. SoC/Linux에선 MMU와 스케줄러가 사이에 있어서 실패 모드가 커널 패닉, 스케줄링 지터, DMA 코히런시, 권한 문제로 바뀐다. 드라이버도 커널 API 계약(sleep 가능한 컨텍스트인지, IRQ 핸들러와 threaded IRQ)을 지켜야 한다. 내 경험은 MCU/bare-metal 쪽이 훨씬 깊고, Cortex-A는 학습 중이라고 솔직히 말하겠다.

**English answer**: "On an MCU I own the entire address map, and I can account for every cycle — the failure modes are hard faults, stack overflow, and a missed interrupt. On a Cortex-A SoC under Linux, there's an MMU and a scheduler between me and the hardware, so the failure modes change: cache coherency with DMA, scheduling jitter, permissions, and following kernel contracts about which context can sleep. The debugging tools change too — ftrace and dmesg instead of a JTAG probe on every build. I'll be straight with you: my depth is on the bare-metal and RTOS side, on Cortex-R and Cortex-M. I've worked next to Linux systems but I haven't owned a kernel driver end to end, and that's the part I'd be ramping on."

**꼬리질문**
- "그럼 SoC 쪽 일이 절반이면 어떻게 할 건가?" → 첫 분기는 기존 드라이버를 읽고 작은 버그부터 잡으며 커널 관례를 익힌다. 버스와 하드웨어 디버깅은 그대로 쓸 수 있는 자산이다.
- "DMA 코히런시는 어디서 문제가 되나?" → 캐시가 있는 시스템에서 CPU와 DMA가 같은 버퍼를 볼 때. MCU 쪽에서도 Cortex-M7 같은 캐시 있는 코어에서 같은 문제가 난다. C03 §4.2.
- "두 쪽이 같은 프로토콜로 통신한다면 코드 공유는?" → 프로토콜 정의(패킷 구조체, CRC, 상태 머신)는 순수 C로 한 곳에 두고 양쪽에서 컴파일한다. OS 의존 코드는 각자.

### Q10. A bug is reported against shipped build 1.4.2, but main has moved on by 300 commits. What do you do?

**왜 묻나**: 릴리스 운영 경험. 신생 회사가 곧 겪을 상황이다.

**30초 답변**: 먼저 1.4.2 태그를 그대로 빌드해서 재현되는지 확인한다(재현 가능한 빌드가 여기서 값을 한다). 릴리스 브랜치에서 최소 수정을 하고 1.4.3을 내고, 같은 수정을 main으로 포팅한다. 회귀 테스트를 추가하고, 왜 CI가 못 잡았는지를 별도로 본다. 필드 롤아웃은 단계적으로 — 소수 기기에 먼저 내보내고 텔레메트리를 본다.

**English answer**: "First, build the exact tag and reproduce — that's where reproducible builds pay for themselves, because if I can't rebuild 1.4.2 bit-for-bit I'm debugging a guess. Then the minimal fix goes on the release branch, ships as 1.4.3, and the same fix is forward-ported to main, never the other way around by accident. I add a regression test at whatever layer can catch it, and separately I ask why CI didn't catch it — that's usually the more valuable output. And the rollout is staged: a small cohort first, watch the telemetry, then widen."

**꼬리질문**
- "체리픽 방향을 왜 한쪽으로 고정하나?" → 양방향으로 하면 어느 브랜치에 뭐가 들어갔는지 아무도 모르게 된다. 규칙이 뭐든 일관성이 중요하다.
- "텔레메트리는 뭘 보나?" → 크래시율, 재부팅 원인, 워치독 트립, 배터리 소모 이상. SSD에서 NVMe telemetry로 하던 것과 같은 종류의 설계다.
- "롤백은 어떻게?" → anti-rollback 카운터 때문에 진짜 되돌리기는 제약이 있다. 그래서 단계적 롤아웃이 1차 방어선이다. J04에서.

### Q11. How would you set up a brand new firmware repository on day one at a startup?

**왜 묻나**: 0→1 회사. 실제로 이걸 시킬 수도 있다.

**30초 답변**: 순서는 이렇다. ① 부팅하는 최소 이미지와 빌드(CMake 또는 west) ② 버전 문자열과 부팅 배너 ③ `-Wall -Wextra -Werror`와 포매터 ④ 호스트 유닛 테스트 한 개라도 굴러가게 ⑤ CI에서 클린 빌드 + 크기 리포트 ⑥ 로그와 fault 핸들러(CFSR 덤프) ⑦ 플래싱/디버깅 원커맨드 스크립트. 그다음에야 기능을 짠다. 이 일곱 개가 없으면 3개월 뒤 속도가 절반이 된다.

**English answer**: "I'd spend the first week on the things that are almost impossible to retrofit. A minimal image that boots and blinks, with the build system chosen and a toolchain pinned in a container. A version banner with git describe baked into the binary. Warnings as errors and a formatter from commit one, because turning on -Werror later means fixing four hundred warnings nobody will fix. One host unit test, so the harness exists. CI that builds clean and reports flash and RAM. A fault handler that dumps CFSR and the stacked PC over the console. And a single command that builds, flashes and attaches a debugger. After that I'd write features. Every one of those is cheap on day one and expensive on day ninety."

**꼬리질문**
- "RTOS 선택은 언제?" → 하드웨어와 벤더 SDK가 정해진 뒤. 벤더 지원이 성숙한 쪽을 고르는 게 일정상 옳은 경우가 많다.
- "문서는?" → README에 빌드/플래시/디버그 3개 명령, 그리고 보드 리비전 표. 그 이상은 초기엔 사치다.
- "`-Werror`가 개발을 막으면?" → 로컬은 끌 수 있게 하고 CI에서만 강제하는 절충도 쓴다. 다만 로컬에서 켜 두는 팀이 결국 빠르다.

### Q12. How do you share code between the MCU firmware and the SoC side?

**왜 묻나**: 멀티 프로세서 제품의 실질적 질문. IPC 설계로 이어진다.

**30초 답변**: 공유할 가치가 있는 건 딱 하나, **프로토콜 정의**다. 패킷 구조체, opcode enum, CRC, 파서 상태 머신을 OS 의존성 없는 순수 C로 한 폴더에 두고 양쪽에서 컴파일한다. 구조체는 명시적 크기 타입과 패킹, 엔디언을 못 박고 `static_assert`로 크기를 검증한다. 그 외의 코드는 공유하려다 오히려 양쪽 다 이상해진다.

**English answer**: "I'd share exactly one thing: the interface definition. The packet layout, the opcode enum, the CRC routine and the parser state machine, written in plain C with no OS dependencies, in a directory both builds compile. I'd use fixed-width types, explicit packing, a defined byte order, and a static assert on every struct size so a compiler difference between the two toolchains fails the build instead of corrupting messages in the field. Beyond that I'd resist sharing — driver code and application logic want to look different on an RTOS and on Linux, and forcing them to share makes both worse."

**꼬리질문**
- "버전이 안 맞으면?" → 핸드셰이크에서 프로토콜 버전을 교환하고, 하위 호환 규칙(모르는 TLV는 무시)을 미리 정한다.
- "`__attribute__((packed))`의 위험은?" → 정렬되지 않은 접근을 만들어 Cortex-M에서 성능 저하나 폴트를 유발할 수 있다. 그래서 필드 순서를 정렬 친화적으로 배치하고 패킹은 최소로.
- "IPC 물리 채널은?" → UART가 가장 단순하고, 대역폭이 필요하면 SPI나 공유 메모리 + mailbox. 깨우기 신호선은 별도로 두는 게 좋다.

### Q13. What coding standard or static analysis would you bring to a team like ours? And how do you handle a toolchain upgrade?

**왜 묻나**: 프로세스 감각. 과하면 속도를 죽이고, 없으면 품질이 무너진다. 툴체인 업그레이드는 `maintain`의 또 다른 얼굴이다.

**30초 답변**: MISRA 전면 도입은 컨슈머 스타트업 속도에 안 맞는다. `-Wall -Wextra -Werror`, `clang-format`, `clang-tidy` 핵심 체크, PR 크기 델타부터 시작하고, MISRA에서는 가치 큰 규칙만 골라 린터로 옮긴다 — 암묵적 변환, 부작용 있는 매크로, 반환값 무시. 규칙은 CI가 강제해야 의미가 있다. 툴체인 업그레이드는 기능 작업과 절대 섞지 않는다. 독립 PR로, 새 경고 전부 처리 + 크기 델타 + 전체 HIL을 붙이고, 릴리스 브랜치는 그대로 둔다.

**English answer**: "I wouldn't bring full MISRA to a consumer startup — the compliance overhead doesn't match the pace. I'd start with warnings as errors, clang-format enforced in CI, and a curated clang-tidy set, then cherry-pick the MISRA rules that actually catch firmware bugs: implicit conversions, macros with side effects, ignored return values, unbounded waits. A rule only exists if CI enforces it. Toolchain upgrades get the same discipline: never inside a feature branch, always its own PR with the warning cleanup, a size diff and a full hardware run, landed early in a cycle. Release branches stay on the compiler they shipped with, and optimizer changes are exactly what surface latent undefined behavior."

**꼬리질문**
- "정적 분석 오탐이 많으면?" → 체크를 선별하고, 억제할 때는 이유를 주석으로 남긴다. 무조건 억제 파일에 넣으면 도구가 죽는다.
- "기존 코드에 도입하려면?" → 새 코드에만 강제하고 기존은 베이스라인으로 동결한 뒤 점진적으로 줄인다.
- "UB에 의존하던 코드가 업그레이드로 깨지면?" → 고친다. 최적화 레벨을 낮춰 덮는 건 부채를 늘릴 뿐이다.

### Q14. Our device has an Android SoC and an always-on MCU. How do you decide what runs where?

**왜 묻나**: 시스템 설계 질문이자 Hark 제품 구조의 핵심. J03/J05와 이어진다.

**30초 답변**: 네 가지로 자른다. 상시 켜져야 하는가(전력) — MCU. µs~ms 결정성이 필요한가(오디오 DMA, 센서 타이밍) — MCU. 메모리와 연산이 큰가 — SoC. 재부팅해도 되는가 — SoC. 그래서 전원 상태 머신, 센서 허브, wake word 전단은 MCU에 두고, 큰 모델·네트워크·UI는 SoC에 둔다. 경계에서 중요한 건 MCU가 SoC를 깨우는 동안의 오디오 pre-roll 버퍼와, 양쪽 상태가 어긋났을 때의 복구 규칙이다.

**English answer**: "I'd use four questions. Does it have to run all the time? Then MCU, because the SoC's floor current will eat the battery. Does it need microsecond-to-millisecond determinism, like audio DMA or sensor timing? MCU. Does it need megabytes of memory or serious compute? SoC. Can it tolerate a reboot? SoC. That naturally puts the power state machine, the sensor hub, and the wake-word front end on the MCU, and the large model, networking and UI on the SoC. The interesting engineering is at the boundary: while the SoC is waking, which takes hundreds of milliseconds, the MCU has to keep a pre-roll audio buffer so the user's first word isn't lost, and both sides need a defined recovery when one of them resets unexpectedly."

**꼬리질문**
- "SoC를 깨우는 데 얼마나 걸리나?" → 시스템마다 다르다. 저전력 상태에서 수십 ms, 완전 종료에서 수 초. 실측해서 pre-roll 버퍼 크기를 정한다.
- "MCU가 SoC를 잘못 깨우면?" → false accept 비용이 전력이다. 2단계 검출(MCU가 가볍게, SoC가 확인)이 흔한 해법이다. J05.
- "둘 다 리셋되면 누가 먼저 올라오나?" → 전원 시퀀스와 리셋 도메인을 HW 팀과 합의해서 명시해야 한다. 보통 MCU가 전원의 주인이다.

---

## 6. Don 매핑

### 6.1 레쥬메에서 바로 쓸 수 있는 근거 (context 3.1절)

| JD 요소 | 레쥬메 근거 (그대로) | 어떻게 말할까 |
|---|---|---|
| `C/C++` | "Developed using embedded C and C++ programming on bare-metal (pre/post-silicon)" | 언어를 아는 수준이 아니라 **양산 제품 코드**를 썼다는 점을 강조. C++ 서브셋 결정 경험이 있으면 그것까지 |
| `ARM-based ... microcontrollers` | "ARM Cortex R8/R82/M0+ … FW bring-up", Xtensa | Cortex-M0+는 정확히 MCU 급이다. Cortex-R은 하드 리얼타임 코어라 RTOS 논의와 잘 붙는다 |
| `SoCs` | "SoC verification … I2C, SPI, DMA, PCIe, SRAM/DRAM bring-up" | 여기서 SoC는 SSD 컨트롤러 SoC다. Cortex-A/Android와는 다르므로 **정직하게 구분**해서 말해야 한다 |
| `maintain` | SSD 양산 펌웨어를 고객(MS/DELL/HPE)에게 출하, error reporting/handling 설계, NVMe telemetry | "출하 후에도 계속 사는 코드"를 다뤘다는 증거. 릴리스·회귀·필드 이슈 경험으로 번역 |
| `targeting` (크로스 빌드) | FPGA pre-silicon → production silicon으로 드라이버 이관 | 같은 코드가 여러 타깃(FPGA/실리콘)에서 돌게 만든 경험 = 보드 리비전 유지보수와 같은 근육 |

### 6.2 강점 스토리 — 이렇게 답한다

**스토리 A: FPGA pre-silicon → 실리콘 이관 (Q03, Q09, Q11에 쓰기 좋다)** — FPGA 상의 pre-silicon 환경에서 Cortex-R/M 코어와 주변 IP(I2C, SPI, DMA, SRAM/DRAM)를 bring-up하고 그 드라이버를 실제 실리콘으로 옮겼다. **"타깃이 바뀌어도 같은 코드베이스가 살아남게 만드는 일"**이고, Hark의 EVT1 → EVT2 → DVT 유지보수와 구조적으로 같다. 말하는 법: "FPGA와 실리콘은 클럭 속도도, 타이밍도, 버그도 다르다. 타이밍 의존 코드를 걷어내고 플랫폼 차이를 아래 층으로 밀어 넣었다. 보드 리비전 지원도 결국 같은 규율이다."

**스토리 B: 양산 SSD 펌웨어의 에러 처리와 텔레메트리 (Q06, Q10에 쓰기 좋다)** — 에러 분류·복구 전략·로깅 오버헤드를 설계하고 NVMe telemetry 디버그 기능을 고객 기한에 맞춰 출하했다. **필드에 나간 펌웨어를 관찰하고 고치는 체계**이자 OTA fleet observability의 전신이다. 말하는 법: "엔터프라이즈 SSD는 재현 안 되는 필드 이슈를 telemetry로 잡는다. 컨슈머 기기도 문제의 모양은 같다 — 재현 불가, 대량, 원격."

**스토리 C: Apple 무선 칩셋 통합 (Q09, Q12, Q14에 쓰기 좋다)** — 새 무선 칩을 출하 플랫폼에 통합하며 PCIe/I2C/SPMI/RFFE 인터페이스 장애의 root cause를 맡았다. **서로 다른 프로세서/서브시스템이 붙는 경계**를 다룬 경험이고, Hark의 SoC ↔ MCU ↔ 라디오 경계와 직결된다.

### 6.3 갭과 프레이밍

| 갭 | 사실 | 프레이밍 (거짓말하지 않는 선) |
|---|---|---|
| Cortex-A / Linux / Android BSP | 레쥬메에 없음. SSD SoC는 Cortex-R/M 중심 | "제 깊이는 bare-metal과 Cortex-R/M 쪽이다. Cortex-A 커널 드라이버를 end-to-end로 소유해 본 적은 없고, 그건 ramp-up 항목으로 본다. 다만 버스·하드웨어 디버깅 자산은 그대로 쓴다." Q09의 답이 이 프레이밍이다 |
| 상용 RTOS 실무 + Zephyr 빌드 체계 | context 3.1: bare-metal·자체 스케줄러 중심, FreeRTOS/Zephyr 이름 없음 | J02/J11에서 다룬다. "nRF52840 DK로 실습 중"은 **실제로 해 본 뒤에만** 말한다 (context 4.8) |
| 현대적 CI/HIL 파이프라인 | <확인 필요: SSD 팀/Apple에서 CI, 유닛 테스트, 정적 분석, 코드 리뷰 도구가 어떻게 돌아갔는지. 회사마다 매우 다르므로 실제 경험한 것만 말해야 한다> | 경험이 있으면 Q08의 강력한 근거. 없으면 "설계는 이렇게 하겠다"로 답하고 경험이라 주장하지 않는다 |
| C++ 사용 범위 | "embedded C and C++"라고만 되어 있음 | <확인 필요: 실제로 C++를 어느 층에서 얼마나 썼는지, 예외/RTTI를 껐는지, 템플릿을 썼는지. Q02의 답을 구체화하려면 이 사실 확인이 필요> |
| 버전·릴리스 브랜치 운영 | <확인 필요: SSD FW의 버전 체계, 릴리스 브랜치 전략, 고객별 브랜치 유무> | 있으면 Q04/Q10의 1급 근거가 된다 |
| 보드 리비전 대응 | <확인 필요: Apple에서 EVT/DVT/PVT를 거치며 보드 리비전별 FW를 어떻게 관리했는지 — 별도 이미지였는지, 런타임 감지였는지> | 있으면 Q03의 최고의 답이 된다 |

### 6.4 쓰지 말아야 할 표현

"Zephyr 프로젝트 경험이 있다"(실습 전에는 금지), "Android BSP를 해 봤다", "MISRA 프로젝트를 이끌었다", "OTA 인프라를 구축했다" — 전부 레쥬메 근거가 없다(context 3.1에서 OTA는 "레쥬메 명시 없음").

---

## 7. 준비 체크리스트

- [ ] 내가 소유했던 펌웨어 코드베이스의 **디렉터리 구조를 종이에 그려** 60초 안에 설명할 수 있게 만든다 (Q01)
- [ ] `-mcpu`, `-mthumb`, `-mfpu`, `-mfloat-abi`, `-ffunction-sections`, `--gc-sections`, `-Map`을 각각 한 문장으로 설명할 수 있게 암기 (C01 §9.2 복습)
- [ ] 임베디드 C++ 서브셋 정책을 한 장으로 작성: 켜는 것 / 끄는 것 / 이유. `-fno-exceptions -fno-rtti -fno-threadsafe-statics`가 왜 필요한지 설명 가능하게
- [ ] 보드 리비전 대응 3가지 방식(컴파일 타임 / 런타임 감지 / 데이터 주도)을 표로 외우고, Apple/Solidigm에서 **실제로 어떤 방식이었는지 확인**해 둔다 (6.3의 확인 필요 항목)
- [ ] 멀티 이미지 버전 체계를 화이트보드에 그리는 연습: 번들 → 매니페스트 → 각 이미지 버전 + 해시 + IPC 프로토콜 버전 (Q04)
- [ ] `arm-none-eabi-size`와 `bloaty`를 실제 ELF에 돌려 보고 출력 읽는 법 익히기. 펌웨어 코드 리뷰 체크리스트 10줄도 스스로 써 본다 (Q06, Q07)
- [ ] 테스트 피라미드 4층을 그림으로 설명하고, "드라이버를 호스트에서 테스트 가능하게 만드는 구조"를 코드로 5분 안에 쓸 수 있게 연습 (§2.11 코드)
- [ ] SoC vs MCU 분담 기준 4가지(전력/결정성/메모리·연산/재부팅 허용)를 외우고 Hark 기기에 적용해 말하는 연습 (Q14)
- [ ] Cortex-A/Linux 갭에 대한 **정직한 30초 답변**을 영어로 다듬는다 (Q09의 English answer를 자기 표현으로)
- [ ] 역질문 준비: "지금 MCU 쪽 빌드는 Zephyr인가요, 벤더 SDK + FreeRTOS인가요? 보드 리비전은 몇 개를 동시에 지원하고 있나요?" (context 4.7과 함께)

---

## 8. 더 읽기

| 주제 | 어디로 | 왜 |
|---|---|---|
| Cortex-M 부트, 링커 스크립트, 툴체인 옵션, map 파일 | C01 §5, §6, §9, §9.5 | 이 노트 §2.5의 배경 전부 |
| Cortex-A와 Cortex-M 차이(MMU, EL, GIC, 부트 체인), CMSIS | C01 §8, §10 | Q09 답변의 근거, 벤더 SDK의 모양 |
| 임베디드 C++ (예외/RTTI, RAII, 템플릿 비용, 정적 초기화 순서) | C02의 임베디드 C++ 절 | Q02의 깊은 근거 |
| `volatile`, 정렬/패킹/엔디언, 정수 승격 | C02 | Q07 리뷰 체크리스트, Q12의 패킷 구조체 |
| BSP 구성·bring-up 순서, 드라이버 계층화와 devicetree | C03 §1, §10 | §2.3 층 구조의 구체화, J02로 이어짐 |
| RTOS 스택 크기 정하기, Zephyr 개발 환경(west/Kconfig/DT) | C04 §10.3, §14 | Q06 꼬리질문, §2.4 빌드 시스템 |
| HardFault 해석, RTT/ITM 로깅, 디버그 워크플로 | C10 | §3.6 로깅, Q11의 fault 핸들러 |
| C 코딩 드릴(링버퍼, 비트 매크로, memory pool, FSM) · 디버깅 시나리오와 STAR 스토리 | S01 전체, S06 | 기술 스크린의 코딩 파트, §6.2 스토리 다듬기 |
| 같은 JD 문장군 | J09(자원 제약 C/C++), J10(Cortex와 툴체인) | J01은 업무 관점, J09/J10은 검증 관점 |
