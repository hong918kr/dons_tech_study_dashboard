# B06. 임베디드 OS 지형도 — bare-metal에서 eLinux·AOSP·VxWorks·QNX까지, 그리고 Don의 정직한 좌표

> **시리즈**: BSP 집중 6/6 · **선행**: B00(BSP 지도), B03(Linux·Android BSP), C04(RTOS), J11(RTOS 정직 스크립트) · **JD 근거**: "Hands-on experience with embedded operating systems, such as eLinux, AOSP, VxWorks and RTOSes" (2026-09-23 개정판, `JD_SNAPSHOT_2026-09-29.md` BSP 공고 Requirements)
> **Don 상태**: ❌ → 🟡. **이 노트가 다루는 것은 Don의 가장 큰 남은 갭이다.** bare-metal + 사내 스케줄러 + Cortex-R/M bring-up은 강하지만, **상용 OS를 출하 제품에서 운영해 본 경험이 없다**. VxWorks·QNX는 만져 본 적조차 없다.
> **이 노트를 다 읽으면**: ① 임베디드 OS 클래스 5종을 문제 기준으로 고를 수 있다 ② VxWorks·QNX를 아는 척하지 않고 정확히 설명할 수 있다 ③ "hands-on"이 면접관에게 어떤 5단계로 들리는지 알고 자기 좌표를 먼저 밝힐 수 있다.

> **중복 회피**: Linux/AOSP 부트 체인·devicetree·커널 드라이버·HAL·GKI는 **B03**이 깊게 다룬다. FreeRTOS/Zephyr API와 커널 내부(PendSV, priority inversion, tickless)는 **C04**. "RTOS 안 써 봤다"를 말하는 단독 스크립트와 1주 Zephyr 램프업은 **J11 6.3/6.4**. 이 노트는 그 위층 — **OS를 고르는 판단, 이름만 들어본 OS 두 개, hands-on의 등급, 번역표, OS 폭(breadth) 질문용 스크립트**만 다룬다.

> **버전·벤더 경고**: 상용 OS의 기능·인증·툴 구성은 **에디션과 버전마다 다르다**. 이 노트의 VxWorks 항목은 Wind River가 공개한 VxWorks 7 데이터시트(Rev. 12/2019)와 제품 페이지 기준이고, QNX 항목은 qnx.com 개발자 문서(SDP 8.0 System Architecture, 그리고 adaptive partitioning은 6.5 문서) 기준이다. **가격·라이선스 조건은 공개 1차 자료가 없어 이 노트에 적지 않는다.** 면접에서도 적지 말고 말하지 말 것.

---

## 0. 큰 그림 — 한 제품 안에 OS가 몇 개 도는가

```
                    Hark 1세대 컨슈머 기기 [추정 구조]
 ┌──────────────────────────────────────────────────────────────────────────┐
 │  Application Processor (Cortex-A, multi-core)                            │
 │   ├ AOSP 또는 커스텀 embedded Linux   ← UI, 네트워크, 큰 모델, 앱 프레임워크 │
 │   └ (옵션) TEE(OP-TEE) / hypervisor   ← secure world, multi-OS            │
 ├──────────────────────────────────────────────────────────────────────────┤
 │  Always-on sensor·audio MCU (Cortex-M)                                   │
 │   └ RTOS(Zephyr / FreeRTOS / ThreadX) 또는 bare-metal                     │
 │      ← wake word, IMU 융합, 전원 상태기계, AP 깨우기                        │
 ├──────────────────────────────────────────────────────────────────────────┤
 │  Radio subsystem (Wi-Fi/BT 콤보칩, cellular 모뎀, GNSS)                   │
 │   └ 벤더 펌웨어. 안이 안 보인다. RTOS일 수도, bare-metal일 수도 있다         │
 ├──────────────────────────────────────────────────────────────────────────┤
 │  PMIC / fuel gauge / 햅틱 / 터치 / PDM 마이크                              │
 │   └ 전용 하드웨어 상태기계 또는 아주 작은 bare-metal 코드                    │
 └──────────────────────────────────────────────────────────────────────────┘
      경계마다 IPC가 하나씩 있다: mailbox/IPCC, SPI/I2C 명령 프로토콜,
      shared memory + doorbell IRQ, HCI over UART/SDIO/PCIe
```

이 그림이 이 노트의 전제다. JD 한 문장("embedded operating systems, such as eLinux, AOSP, VxWorks and RTOSes")은 **OS 하나를 아느냐**를 묻는 게 아니라 **이 층들 전체를 머리에 갖고 있느냐**를 묻는다. Hark의 Systems 공고가 그걸 직접 말한다: "you will work across RTOS, bare metal, and Linux environments on the same product" (`JD_SNAPSHOT_2026-09-29.md`, id 4201692009 About the Role).

---

## 1. JD 문장이 바뀐 것을 어떻게 읽어야 하나

| 시점 | BSP 공고의 OS 요구 문장 |
|---|---|
| 2026-09-18 수집 (구판) | "Hands-on experience with RTOS (FreeRTOS, Zephyr, or similar)" |
| 2026-09-23 개정 (현행) | "Hands-on experience with embedded operating systems, such as eLinux, AOSP, VxWorks and RTOSes" |

같은 팀 5개 공고에 OS 이름이 어떻게 흩어져 있는지가 더 중요한 단서다(전부 `JD_SNAPSHOT_2026-09-29.md` 원문).

| 공고 | OS 관련 문장 |
|---|---|
| BSP (4186968009) | "eLinux, AOSP, VxWorks and RTOSes" |
| Embedded Software (4201692009) | "across RTOS, bare metal, and Linux environments on the same product" / 우대 "Android platform experience, including AOSP" |
| Platform (4221269009) | "Experience with one or more of: Unix or BSD Linux, VxWorks, QNX, or other modern operating systems" |
| Frontier UX (4396895009) | 우대 "a system-level UI framework or graphics stack on iOS, Android, Linux and/or VxWorks" |
| Embedded DevOps (4404597009) | "build system for bare metal, RTOSes, and/or Linux/Android based systems" |

여기서 읽어야 할 세 가지.

1. **한 제품에 OS가 여러 개 있다는 것은 추측이 아니라 JD가 직접 쓴 문장이다.** 그래서 "OS 하나를 깊게" 못 쓰는 답보다 "경계를 설계할 수 있다"는 답이 더 잘 먹힌다.
2. **VxWorks가 서로 다른 세 공고(BSP·Platform·UX)에 나타난다.** 컨슈머 웨어러블에 VxWorks를 쓴다는 1차 근거는 어디에도 없다. 훨씬 개연성 높은 설명은 **팀 전체가 같은 "OS 폭" 문구 템플릿을 돌려 쓰고 있다**는 것이다 [추정]. UX 공고에 VxWorks가 붙은 것("VxWorks 위의 system-level UI framework")은 특히 템플릿 냄새가 난다.
3. **그래도 이름이 적혀 있으므로 "그게 뭔지 모릅니다"는 답은 불가능하다.** 면접관이 VxWorks 경험자일 수도 있다(항공·방산·자동차 출신이면 흔하다).

### 1.1 그래서 준비 깊이를 이렇게 나눈다

**A. 설계 대화가 가능해야 한다 (막히면 감점)** — RTOS, embedded Linux(Yocto/Buildroot), AOSP. 이 셋은 B03·C04·B02가 이미 커버한다. 이 노트에서는 "언제 무엇을 고르나"만 보탠다. **B. 정확히 설명할 수 있고, 안 써 봤다고 먼저 밝힌다** — VxWorks, QNX. 5·6절이 그 준비다. 목표는 **10분짜리 대화를 버티는 것**이고, 경험을 주장하는 게 아니다. **C. 준비하지 말 것** — VxWorks/QNX의 API 이름 외우기(`taskSpawn`, `ChannelCreate` 같은 것을 나열하면 오히려 "안 써 본 사람이 위키 읽고 왔다"로 읽힌다), 라이선스 가격, 인증 취득 절차 디테일, ThreadX/µC-OS 내부.

---

## 2. OS 클래스 결정 지도

### 2.1 결정 트리

```
 질문 1: MMU(또는 최소한 MPU)가 있는 프로세서인가?
   NO  ─────────────────────────────────► bare-metal 또는 RTOS
   YES
    │
 질문 2: 파일시스템·TCP/IP·동적 앱·디스플레이 스택·큰 모델 런타임이 필요한가?
   NO  ─────────────────────────────────► RTOS (또는 Linux 없는 RTOS + lwIP)
   YES
    │
 질문 3: 앱 생태계 / UI 프레임워크 / 미디어 프레임워크를 "받아서" 쓰고 싶은가?
   NO  ─────────────────────────────────► embedded Linux (Yocto / Buildroot / 벤더 BSP)
   YES ─────────────────────────────────► AOSP
    │
 질문 4(직교): 기능안전 인증서가 필요한가? 최악 지연을 계약으로 보장해야 하나?
   YES ─────────────────────────────────► 인증 가능한 상용 RTOS (VxWorks, QNX, 인증판 RTOS)
                                            또는 Linux + 별도 안전 MCU로 분리
```

질문 4가 **VxWorks와 QNX가 존재하는 이유 전체**다. 나머지 세 질문만으로는 이 두 OS가 선택되지 않는다.

### 2.2 클래스 비교 표

숫자는 **자릿수 감각용 [예시]**이고 칩·구성·측정 방법에 따라 크게 달라진다. 면접에서는 숫자를 단정하지 말고 "자릿수"로 말한다.

| 클래스 | 대표 | 코드/RAM 자릿수 [예시] | 부팅 | 스케줄 지연 [예시] | 메모리 보호 | 인증 | 개발·유지 비용 |
|---|---|---|---|---|---|---|---|
| bare-metal / 사내 스케줄러 | 직접 작성 | 수 KB ~ 수백 KB | 수 ms 이하 | 인터럽트 지연 = 사실상 하드웨어 한계 | 없음 또는 MPU 직접 | 코드 전체를 자기가 증명 | 초기 최저, 규모 커지면 최악 |
| RTOS (오픈소스) | FreeRTOS, Zephyr, ThreadX | 커널 수 KB ~ 수십 KB | 수 ms | 마이크로초 자릿수 | 없음 ~ MPU 스레드 분리 | 별도 인증판 필요 | 낮음, 커뮤니티 의존 |
| 상용 RTOS | VxWorks, QNX | 수백 KB ~ 수 MB | 수십 ms ~ | 마이크로초 자릿수 | 커널/유저 분리 + MMU | 인증 패키지 존재 | 높음(라이선스 + 툴 학습) |
| embedded Linux | Yocto/Buildroot 산출물 | 수 MB ~ 수백 MB | 수백 ms ~ 수 s | 기본 커널은 밀리초 자릿수, 최악은 더 나쁨 | 프로세스 격리 + MMU | 기본적으로 없음 | 중간, 생태계 최대 |
| AOSP | Android 플랫폼 | 수 GB 이미지 | 수 s ~ 수십 s | 앱 계층은 실시간 보장 없음 | 프로세스 + SELinux + Treble 경계 | CDD/CTS 호환성(안전 인증과 다름) | 최고(빌드·업스트림 부담) |

### 2.3 각 클래스가 실제로 푸는 문제

**bare-metal / 사내 스케줄러**가 푸는 문제는 **"커널이 끼어들면 안 되는 데이터 경로"**다. 최악 지연을 코드 전체를 읽어서 증명해야 하는 상황, 커널이 먹는 사이클·RAM조차 아까운 상황, 혹은 워크로드가 너무 특수해서 범용 스케줄러가 오히려 방해되는 상황. 엔터프라이즈 SSD 컨트롤러 펌웨어가 정확히 여기다 — 호스트 명령 경로의 지연 분포가 제품 스펙이고, 코어마다 하는 일이 고정돼 있으며, 스케줄링 정책을 워크로드에 맞춰 직접 깎는 게 이득이다. **RTOS**가 푸는 문제는 **"주기가 다른 일이 열 개로 늘어났을 때의 구조 붕괴"**다. superloop에서 가장 급한 일과 느린 일이 같은 루프를 공유하기 시작하면, 새 기능 하나가 기존 타이밍을 깨고 그 사실을 아무도 예측할 수 없게 된다. RTOS는 우선순위와 블로킹을 언어로 만들어 준다. 대가는 컨텍스트 스위치 비용, 스택을 task마다 따로 잡는 RAM 낭비, 그리고 "잘못 쓸 수 있는 새 방법들"(우선순위 역전, 스택 오버플로, ISR-safe API 혼동)이다.

**embedded Linux**가 푸는 문제는 **"이미 남이 만든 것을 쓰고 싶다"**다. TCP/IP·Wi-Fi supplicant·TLS·파일시스템·USB·디스플레이 파이프라인·Python·ML 런타임. 이걸 직접 쓰면 몇 사람-년이다. 대가는 부팅 시간, 메모리, 실시간성, 그리고 **업스트림 관리 부담**(커널 버전, CVE, 벤더 BSP 리베이스). **AOSP**가 푸는 문제는 **"UI/미디어/앱 프레임워크와 그 위의 인력 시장"**이다. Android 프레임워크, 그래픽 스택, 카메라·오디오 HAL 계약, 그리고 Android를 아는 엔지니어를 채용할 수 있다는 것. 대가는 이미지 크기, 빌드 인프라, vendor 경계 관리(B03 5절), 그리고 AOSP 릴리스에 끌려다니는 일정.

**인증 가능한 상용 RTOS(VxWorks, QNX)**가 푸는 문제는 **"규제 기관이나 OEM에 문서로 증명해야 하는 결정성과 안전성"**이다. 항공(DO-178C), 산업(IEC 61508), 자동차(ISO 26262), 의료(IEC 62304). 여기서 사는 것은 기능이 아니라 **증거 패키지와 책임**이다.

### 2.4 컨슈머 AI 웨어러블의 어느 층에 무엇이 오나 [전부 추정]

| 층 | 가장 개연성 높은 OS | 이유 |
|---|---|---|
| Application Processor | AOSP 또는 커스텀 eLinux | 디스플레이·네트워크·모델 런타임·앱 프레임워크가 필요. Hark가 Graphics 공고에서 "kernel GPU driver와 userspace graphics stack"을 말한다 → 리눅스 계열 확정적 |
| always-on MCU | Zephyr 또는 FreeRTOS 기반 벤더 SDK | 수십 µA 예산, wake word, 센서 융합. RAM이 수백 KB 규모면 리눅스는 불가 |
| 라디오 콤보칩 / 모뎀 | 벤더 펌웨어(블랙박스) | 우리가 고르지 않는다. HCI/드라이버 경계만 본다 |
| PMIC·fuel gauge·햅틱·터치 | 하드웨어 상태기계 또는 초소형 bare-metal | OS가 들어갈 자리가 아니다 |
| VxWorks / QNX | **없을 것으로 본다** | 컨슈머 웨어러블은 기능안전 인증 대상이 아니다. JD 언급은 팀 템플릿으로 해석 |

이 표를 면접에서 그대로 말하면 안 된다. 대신 **"어느 층에 무엇이 올지 저는 이렇게 추측하는데, 실제로는 어떤 구성인가요?"** 로 질문으로 바꾼다. 추측을 사실처럼 말하는 순간 손해고, 질문으로 바꾸면 설계 대화가 열린다.

---

## 3. bare-metal과 사내 스케줄러 — Don의 출발점을 정확히 이름 붙이기

"bare-metal"은 한 가지가 아니다. 면접관이 "in-house scheduler"라는 말을 들으면 **반드시 어느 종류였는지 되묻는다.** 그래서 이 스펙트럼을 먼저 정확히 갖고 있어야 한다.

| 형태 | 동작 | 최악 응답시간을 결정하는 것 | 스택 |
|---|---|---|---|
| superloop | `while(1)` 안에서 함수들을 순서대로 호출 | 루프 전체 최장 경로 | 하나 |
| superloop + ISR | 급한 일은 ISR, 나머지는 루프 | ISR 지연 + 루프 최장 경로 | 하나 + ISR 프레임 |
| 인터럽트 + 상태기계 (event-driven) | ISR이 이벤트를 큐에 넣고 메인이 디스패치. 함수는 블로킹하지 않고 상태만 전이 | 가장 긴 상태 전이 핸들러 | 하나 |
| 협조적(cooperative) 멀티태스킹 | 태스크가 명시적으로 `yield()` 할 때만 전환 | 가장 긴 non-yield 구간 | 태스크별 |
| 선점형(preemptive) | 타이머/이벤트로 커널이 강제 전환 | 크리티컬 섹션 + 커널 경로 | 태스크별 |

`<확인 필요: SSD 펌웨어의 사내 스케줄러가 위 다섯 중 어느 형태였는가 — 선점형이었나, 협조적이었나, 인터럽트 + 상태기계였나. 또한 코어별로 달랐을 수 있다(Cortex-R8/R82 데이터 경로 vs M0+ 보조 코어). 이 답 하나가 3절·9절·10절·14절 Q12의 정확도를 전부 결정한다.>`

왜 이게 결정적인가. 이 질문에 **"선점형이었습니다"** 라고 답하면 다음 질문은 자동으로 "그럼 컨텍스트 스위치는 어떻게 구현했나, 크리티컬 섹션 최장 구간은 얼마였나, 우선순위 역전은 어떻게 막았나"다. 이건 Don에게 **최고의 질문**이다(RTOS 커널을 직접 만든 셈이니까). 반대로 실제로는 인터럽트 + 상태기계였는데 "선점형"이라고 말해 버리면 두 번째 꼬리질문에서 무너진다. **맞는 이름을 붙이는 게 과장보다 훨씬 강하다.**

한 가지는 확실히 말할 수 있다. bare-metal 멀티코어 펌웨어에서는 **RTOS가 대신 해 주는 일을 사람이 다 해야 한다**: 스택 크기 실측, 코어 간 동기화 프리미티브, 최악 지연 예산 분배, 워치독 전략, 에러 전파. 이건 "OS를 안 써 봤다"가 아니라 **"OS가 없는 쪽에서 같은 문제를 풀었다"**다. 10절 번역표가 그걸 문장으로 만든다.

---

## 4. RTOS 4종 — 고를 때 실제로 보는 것

API와 커널 내부는 C04에 있다. 여기서는 **선택 기준**만 본다.

| OS | 정체 | 라이선스 | 검증한 사실 |
|---|---|---|---|
| FreeRTOS | 커널 3파일(`list.c`, `queue.c`, `tasks.c`) 규모의 초경량 커널 | MIT | github.com/FreeRTOS/FreeRTOS-Kernel ✅ |
| Zephyr | Linux Foundation 프로젝트. 커널 + devicetree + Kconfig + 드라이버 + 네트워크·BLE 스택까지 포함한 "작은 OS 배포판" | Apache 2.0 | docs.zephyrproject.org ✅ "small-footprint kernel designed for use on resource-constrained and embedded systems", 14개 아키텍처 계열 |
| Eclipse ThreadX | Express Logic → Microsoft Azure RTOS → Eclipse Foundation으로 기부된 RTOS 스위트 | permissive open source | threadx.io ✅ "Microsoft has contributed the Azure RTOS technology to the Eclipse Foundation" |
| VxWorks | Wind River의 상용 RTOS. 커널/유저공간 분리 + POSIX + 인증 패키지 | 상용 (조건은 공개 자료 없음) | 5절 |

### 4.1 실제 선택 기준 다섯 가지

| 순위 | 기준 | 왜 이 순서인가 |
|---|---|---|
| 1 | **칩 벤더 SDK가 무엇 위에 올라와 있나** | 현실에서 가장 센 힘이다. nRF Connect SDK는 Zephyr, ESP-IDF는 FreeRTOS, 많은 Cortex-M 벤더 HAL은 FreeRTOS 예제. 거스르면 드라이버·BLE 스택·전력관리를 직접 포팅해야 한다. 그래서 "FreeRTOS냐 Zephyr냐"는 종종 결정이 아니라 **칩 선택의 결과**다 |
| 2 | **스택이 얼마나 딸려 오나** | BLE 호스트, Wi-Fi supplicant, TCP/IP, 파일시스템, 셸, 로깅, 전원관리, OTA. Zephyr는 포함하는 방향, FreeRTOS는 커널만 주고 조합(FreeRTOS+TCP, lwIP). 팀이 작으면 "다 딸려 오는 쪽"의 가치가 크다 |
| 3 | **하드웨어 기술 방식** | Zephyr는 devicetree + Kconfig로 보드를 데이터로 기술(B02). FreeRTOS는 보드 개념이 없고 벤더 코드가 그 역할. EVT/DVT/PVT로 리비전이 계속 바뀌면 devicetree 쪽이 관리하기 쉽다 — Don의 Apple 리비전 경험과 직결 |
| 4 | **MPU 격리·유저 모드가 필요한가** | Zephyr는 유저 모드와 메모리 도메인 지원, FreeRTOS는 MPU 포트. 여기까지 필요하면 이미 "상용 RTOS나 리눅스로 갈 것인가" 질문이 시작된다 |
| 5 | **인증·장기 지원·법적 책임** | 오픈소스 RTOS에는 "누가 서명해 주느냐"가 없다. 이 요구가 있으면 5·6절로 간다 |

1순위 기준 때문에 J11 6.3 스크립트의 마지막 역질문("선택이었나, SDK를 따라간 것인가")이 힘을 갖는다. 그 질문 하나로 면접관은 팀의 실리콘 선택 과정을 이야기하게 된다.

---

## 5. VxWorks — 이름이 JD에 있으니 정확히 알고 있어야 하는 것

### 5.1 한 문장 정의

VxWorks는 **Wind River의 상용 실시간 운영체제**로, 결정적 응답시간과 **기능안전 인증 가능성**을 제품의 핵심 가치로 파는 OS다. 항공·방산·철도·산업·의료·자동차처럼 "고장이 사람을 죽이거나 기관에 제출할 문서가 필요한" 분야에서 수십 년 쓰였다. 데이터시트가 스스로 이렇게 쓴다: "the world's most widely deployed RTOS", "the first and only RTOS on Mars".

### 5.2 커널과 프로세스 모델

VxWorks의 고전적 모델은 **모든 것이 하나의 주소공간(커널 공간)에 있는 task 집합**이었다. 그래서 컨텍스트 스위치와 함수 호출이 아주 싸고, 대신 한 task의 잘못된 포인터가 전체를 망가뜨릴 수 있다. Don의 SSD 펌웨어와 구조적으로 가장 가까운 상용 OS가 이것이다.

현대 VxWorks(7)는 그 위에 **유저 공간을 얹었다**. 데이터시트가 명시하는 항목: "Separation between kernel and memory-protected user space environments", "State-of-the-art memory protection and memory management". 유저 공간에서 도는 애플리케이션 단위를 **RTP(Real-Time Process)** 라고 부른다 — Wind River 교육 자료 설명으로는 "a way to execute VxWorks applications in user space"이며, 보호된 메모리 모델·시스템 콜 메커니즘·RTP 간 공유 라이브러리를 쓴다. 즉 **커널 task와 RTP가 공존하는 하이브리드**다.

면접에서 쓸 한 줄: **"커널 task는 싸고 위험하고, RTP는 비싸고 안전하다 — 그 둘을 한 시스템에서 고르게 해 주는 게 VxWorks의 프로세스 모델이다."**

### 5.3 스케줄링과 결정성

데이터시트가 명시하는 스케줄링 항목은 세 가지다: "Priority-based preemption with optional round-robin", "Time and space partitioning", "Adaptive scheduling offering foreground and background threading". 멀티코어는 "asymmetric multiprocessing (AMP), symmetric multiprocessing (SMP), and optional support for CPU affinity for bound multiprocessing (BMP)". 제품 페이지는 결정성을 "Single-Nanosecond Determinism", "Low latency and jitter for determinism and reliability"라고 광고한다 — **이건 마케팅 문구이므로 면접에서 숫자로 인용하지 말 것.** 대신 "time and space partitioning을 제공한다"는 사실 쪽을 말하는 게 안전하다. space partitioning은 MMU로 메모리를 격리하는 것이고, time partitioning은 CPU 시간을 예산으로 나눠 한 파티션이 다른 파티션의 시간을 못 먹게 하는 것이다(같은 아이디어의 다른 구현이 QNX의 adaptive partitioning — 6.4절).

POSIX 쪽: 데이터시트는 "POSIX PSE52 conformant"라고 명시한다. PSE52는 POSIX 실시간 프로파일 중 하나다. **즉 VxWorks 코드를 리눅스 코드처럼 쓸 수 있는 구간이 실제로 있다** — 이게 "VxWorks 경험자"와 "리눅스 경험자" 사이에 다리가 있는 이유다. 언어 지원으로 "C11 and C++17", "Boost C++ libraries", "Rust", "Python 3.8"이 적혀 있다(2019년 Rev 기준).

### 5.4 인증 — VxWorks를 고르는 진짜 이유

데이터시트의 "Safety Certifiable" 항목에 적힌 표준: **DO-178C DAL A, IEC 61508 SIL 3, ISO 26262 ASIL D, IEC 62304.** 제품 페이지는 "Safety-Certifiable, 600+ Programs Certified"라고 쓴다.

여기서 반드시 정확해야 할 구분이 있고, 면접에서 이걸 틀리면 크게 깎인다.

**"인증 가능(certifiable)"과 "인증된(certified)"은 다르다.** OS 벤더가 파는 것은 인증 심사에 제출할 수 있는 **증거 패키지**(요구사항 추적성, 테스트 커버리지 근거, 도구 검증 자료)이고, 인증을 받는 주체는 **제품**이다. 그래서 "VxWorks를 쓰면 인증된다"는 문장은 틀렸다. 맞는 문장은 "VxWorks Cert Edition을 쓰면 인증 심사에 필요한 증거의 상당 부분을 벤더에게서 살 수 있다"다. 에디션 이름(Cert Edition, VxWorks 653)과 각각이 어느 표준을 겨냥하는지는 **버전·에디션마다 다르므로 단정하지 말 것.**

그리고 **컨슈머 웨어러블은 이 표준들의 대상이 아니다.** 이것이 "Hark가 실제로 VxWorks를 쓸 개연성이 낮다"는 판단의 근거이고, 면접에서 이 판단을 보여 주는 것 자체가 OS 선택을 이해한다는 증거가 된다.

### 5.5 툴링

데이터시트 기준: **Wind River Workbench** = "Eclipse-based integrated development environment"로 빌드 시스템·디버거·시스템 분석 툴을 포함. 컴파일러는 "LLVM for Arm and Intel architectures", "GCC for PowerPC architecture"(그리고 제품 페이지에는 Diab Compiler가 등장). **"Built-in VxWorks simulator"** 도 있다 — 즉 보드 없이 돌려 볼 수 있다. 보드 지원은 "Over 80 different boards supported"이고 **BSP라는 단어를 Wind River가 그대로 쓴다**("Board Support Packages", 그리고 Professional Services 항목에 "BSP development"). **VxWorks 세계에서 BSP는 Don이 지원하는 직무명과 정확히 같은 뜻이다** — 이 연결은 면접에서 한 번 언급할 값이 있다.

가상화: Wind River의 Helix Virtualization Platform이 "multiple operating systems, such as VxWorks, Linux and Android, to run simultaneously"를 가능하게 한다고 제품 페이지가 말한다. Hark Platform 공고의 "virtualization frameworks and multi-OS system design"과 같은 종류의 이야기다.

### 5.6 왜 컨슈머 회사가 이 이름을 적나 [추정]

가장 개연성 높은 세 가지. ① 채용 담당이 **"임베디드 OS 폭"을 표현하는 관용구**로 썼다(1절 표의 증거). ② 채용하려는 인재 풀이 **항공·방산·자동차 출신**이고 그 사람들의 이력서에 VxWorks/QNX가 있다. ③ 팀에 실제로 그 배경을 가진 사람이 있고 그 사람이 JD를 썼다. 어느 쪽이든 **Don에게 요구되는 것은 "써 봤다"가 아니라 "그 세계를 안다"**다.

### 5.7 면접에서 말할 수 있는 세 문장 (그대로 외울 것)

> "I have not used VxWorks. What I know about it is the shape: a commercial RTOS where the classic model is tasks in a single kernel address space — very cheap switches, no protection — and VxWorks 7 added memory-protected user space with real-time processes on top of that, so you choose per component. It is POSIX PSE52 conformant, it does time and space partitioning, and the reason people pay for it is the safety certification evidence — DO-178C, IEC 61508, ISO 26262. That last part is also why I would not expect it on a consumer wearable, so if it is in your stack I would genuinely like to know what drove that."

---

## 6. QNX — 마이크로커널이라는 다른 답

### 6.1 한 문장 정의

QNX는 **POSIX API를 마이크로커널 위에 올린 상용 실시간 OS**다. 문서가 스스로 목표를 이렇게 쓴다: "deliver the open systems POSIX API in a robust, scalable form suitable for a wide range of systems—from tiny, resource-constrained embedded systems to high-end distributed computing environments." 자동차 인포테인먼트·디지털 콕핏·의료기기·산업 제어에서 널리 쓰인다. 현재 배포 이름은 **QNX SDP**(Software Development Platform)이고, 제품 페이지 표현은 "High-performance, safety-ready microkernel RTOS platform"이다.

### 6.2 마이크로커널 — 무엇이 커널 안에 없는가

이게 QNX의 전부다. 커널 안에는 최소한만 둔다: "the microkernel implements the core POSIX features used in embedded realtime systems, along with the fundamental QNX OS message-passing services." 커널 프로세스 이름은 **`procnto`** 다(마이크로커널 + 프로세스 매니저). 그리고 문서가 명시한다: "The POSIX features that aren't implemented in the procnto microkernel (file and device I/O, for example) are provided by optional processes and shared libraries."

```
  모놀리식 (Linux)                     마이크로커널 (QNX)
 ┌────────────────────────┐          ┌──────────┐ ┌──────────┐ ┌──────────┐
 │ app │ app │ app        │          │  app     │ │ 파일시스템 │ │ 네트워크  │
 ├────────────────────────┤          │          │ │ (프로세스) │ │ (프로세스) │
 │ syscall 경계            │          └────┬─────┘ └────┬─────┘ └────┬─────┘
 ├────────────────────────┤               │ MsgSend()  │            │
 │ 커널: 스케줄러 + VFS +    │          ─────┴────────────┴────────────┴─────
 │ 파일시스템 + 네트워크 +   │               procnto: 스케줄링, 메시지 패싱,
 │ 드라이버  (모두 한 공간)  │               타이머, 인터럽트, 프로세스 관리
 └────────────────────────┘          ─────────────────────────────────────
  드라이버 버그 = 커널 패닉             파일시스템 버그 = 그 프로세스만 죽는다
```

문서가 확인해 주는 구체 사실: "Filesystems execute outside the kernel; applications use them by communicating via messages", "Networking services execute outside the kernel", 그리고 사용자가 쓴 프로세스가 "resource managers that can be started and stopped dynamically"가 된다. **드라이버를 재시작할 수 있다**는 건 모놀리식 커널 세계에서는 없는 성질이다.

대가는 **메시지 패싱 비용**이다. 모놀리식에서 함수 호출 한 번이면 되는 일이 QNX에서는 프로세스 경계를 넘는 메시지가 된다. 그래서 QNX의 성능 이야기는 언제나 "메시지 패싱을 얼마나 싸게 만들었나"로 간다.

### 6.3 메시지 패싱 = 시스템의 접착제

문서 표현: "IPC is the glue that connects those components into a cohesive whole", "Synchronous messaging is the main form of IPC in the QNX OS".

핵심 사실 네 개(전부 문서 확인).

- **동기식이고 복사한다.** "message passing (as implemented in `MsgSend()`, `MsgReceive()`, and `MsgReply()`) is synchronous and copies data." 메시지는 "directly from the address space of one thread to another without intermediate buffering" 옮겨진다. 즉 `MsgSend()`를 부른 스레드는 상대가 `MsgReply()` 할 때까지 블록된다 — RPC와 같은 모양이다.
- **스레드가 아니라 채널로 보낸다.** "message passing is directed towards channels and connections." 수신자가 채널을 만들고, 송신자가 연결을 붙인다.
- **비동기 신호가 필요하면 pulse.** "fixed-size, nonblocking messages"로 작은 페이로드를 나른다. 인터럽트 핸들러가 상위로 알릴 때 쓰는 게 이것이다. 그리고 **우선순위 역전을 IPC 차원에서 막는다** — "QNX OS uses message-driven priority inheritance to avoid priority-inversion problems." 서버 스레드가 클라이언트 우선순위를 물려받는다.

스케줄링에서 문서가 확인해 주는 것: "The microkernel makes scheduling decisions whenever it's entered as the result of a kernel call, exception, or hardware interrupt", "Threads are scheduled globally across all processes", 그리고 준비된 스레드 중 최고 우선순위가 돈다. 블록에서 풀린 스레드는 자기 우선순위 큐의 **뒤**에, 선점당한 스레드는 **앞**에 들어간다. 세부 정책 목록(FIFO/round-robin/sporadic)과 우선순위 범위·방향은 **버전 문서를 봐야 하므로 이 노트에서 단정하지 않는다.**

### 6.4 Adaptive partitioning — 이름을 알아 둘 값이 있는 기능

QNX 문서(6.5 사용자 가이드) 기준. 파티션은 "a virtual wall that separates competing processes or threads"이고, 각 파티션에 CPU 시간 비율인 **budget**을 준다. 사용량 추적은 "microbilling". 안 쓴 예산은 버려지지 않고 "free time is redistributed to other scheduler partitions". 설계 목표로 두 가지가 적혀 있다: "to guarantee proper function when the system is fully loaded", "to prevent unimportant or untrusted applications from monopolizing the system".

왜 이게 중요한가. **우선순위만으로는 "이 컴포넌트에 CPU의 20%를 보장한다"를 표현할 수 없다.** 우선순위는 순서를 정하지 양을 정하지 않는다. 그래서 신뢰할 수 없는 서브시스템(서드파티 앱, 로깅, 진단)이 시스템을 굶기는 것을 막으려면 예산 개념이 따로 필요하다. VxWorks의 "time partitioning", 리눅스의 cgroup CPU 컨트롤러가 같은 문제의 다른 답이다. **이 세 개를 한 문장으로 묶어 말할 수 있으면 OS 폭이 있다는 신호가 된다.**

인증에 관해서는: qnx.software 제품 페이지가 "safety-ready"와 "100% Success rate In achieving safety certification"을 말하지만, **구체적 표준 이름과 에디션은 이 노트 작성 시 공개 페이지에서 확인하지 못했다.** 따라서 QNX의 인증 표준을 면접에서 나열하지 말 것. 안전한 표현은 "QNX도 인증 지향 에디션을 판다, 세부는 에디션별로 확인해야 한다"다.

### 6.5 Don 경험과의 접점

이게 이 절에서 가장 쓸모 있는 부분이다. 멀티코어 SSD 펌웨어에서 코어 사이는 함수 호출로 못 넘어간다. **shared memory에 요청을 쓰고 doorbell로 알리고 완료를 기다리는 구조** — 그게 `MsgSend()`/`MsgReply()`와 개념적으로 같은 모양이다. 그리고 "이 요청을 처리하는 쪽이 요청자보다 우선순위가 낮으면 어떻게 되나"는 Don이 이미 실물로 마주쳤을 문제고, QNX의 답이 message-driven priority inheritance다.

`<확인 필요: SSD 펌웨어에서 코어 간 통신이 shared memory + doorbell/mailbox 인터럽트 구조였는가, 그리고 그 요청 처리의 우선순위 문제를 실제로 다뤘는가.>` 사실이면 6.6의 마지막 문장을 쓸 수 있다. 아니면 쓰지 않는다.

### 6.6 면접에서 말할 수 있는 세 문장

> "I have not used QNX either. The model I do understand: it is a microkernel, so the filesystem, the network stack and the drivers are ordinary processes outside the kernel, and everything is glued together by synchronous message passing — `MsgSend`, `MsgReceive`, `MsgReply` — with message-driven priority inheritance so a low-priority server does not invert a high-priority client. The trade is that a call that is a function call in Linux becomes a process boundary in QNX, and the payoff is that a driver fault does not take the kernel down. The closest thing I have actually built is cross-core request passing in SSD firmware over shared memory with a doorbell interrupt, which is the same shape one layer lower."

---

## 7. eLinux — "embedded Linux"를 만드는 세 가지 방법

B03이 부트 체인·devicetree·커널 드라이버·HAL을 다룬다. 여기서는 **빌드 시스템 선택**만 본다. JD의 "eLinux"는 특정 제품 이름이 아니라 **embedded Linux 일반**을 가리키는 업계 약어다(elinux.org 위키에서 온 표현).

### 7.1 세 갈래

| 방법 | 정체 | 언제 고르나 | 대가 |
|---|---|---|---|
| **Yocto / OpenEmbedded** | 배포판을 만드는 프레임워크. BitBake가 "a generic task execution engine", 메타데이터는 레시피·클래스, 레퍼런스 배포가 Poky | 제품을 여러 개 오래 유지해야 하고, 라이선스·SBOM·패키지 관리가 필요할 때 | 학습 곡선과 빌드 시간이 가장 크다 |
| **Buildroot** | "a tool that simplifies and automates the process of building a complete Linux system for an embedded system, using cross-compilation" | 이미지 하나를 빠르게, 단순하게 만들 때. 런타임 패키지 관리가 필요 없을 때 | 대규모 제품 라인 관리에는 약하다 |
| **벤더 BSP tarball / SDK** | SoC 벤더가 커널 포크 + U-Boot + 빌드 스크립트를 묶어 주는 것. Yocto 레이어 형태로 오는 경우도 많다 | 새 실리콘 초기. 사실 선택이 아니라 전제 | 업스트림에서 멀어지고, 리베이스 부담이 누적된다 |

Yocto 문서가 스스로 강조하는 것: **Yocto는 배포판이 아니다.** Poky조차 "you cannot use it as a product 'out of the box' in its current form"이다. **레이어 모델**이 핵심이고, BSP 레이어가 머신 고유 설정을 분리한다(B03 6.2에 `meta-<bsp>`, `conf/machine/*.conf`, `MACHINE` 구조가 있다).

### 7.2 진짜 어려운 것은 빌드 시스템이 아니다

면접에서 Yocto 문법을 물을 확률은 낮다. 실제로 어려운 것은 셋이다.

**업스트림 거리 관리.** 벤더가 준 커널은 4.19나 5.10 같은 포크고, 거기에 out-of-tree 드라이버가 붙어 있다. 보안 패치를 어떻게 따라갈 것인가, 커널을 올릴 때 벤더 패치를 어떻게 이식할 것인가. Android는 이 문제를 GKI/KMI로 제도화했다(B03 3.5). **부팅 시간과 전력.** 컨슈머 기기에서 리눅스 부팅 시간은 제품 스펙이다. 그래서 대부분 **완전 종료를 안 하고** suspend-to-RAM으로 살려 두거나, always-on MCU가 앞에서 받아 주고 AP는 필요할 때만 깨운다. 이게 0절 그림에서 MCU가 존재하는 이유의 절반이다.

**실시간성.** 기본 리눅스 커널은 최악 지연을 보장하지 않는다. 오디오·모터·라디오 타이밍처럼 보장이 필요하면 세 가지 답이 있다: 실시간 프리엠션 구성을 쓴다(`PREEMPT_RT` 계열 — **메인라인 병합 상태는 커널 버전에 따라 다르므로 단정하지 말 것**), 하드웨어 DMA/큐로 CPU를 경로에서 빼낸다, 또는 **그 일을 별도 MCU로 옮긴다**. 컨슈머 웨어러블은 세 번째를 많이 고른다. **이 세 가지를 나란히 말할 수 있으면 "리눅스를 소유해 본 적 없음"이 훨씬 덜 아프다.**

---

## 8. AOSP — 언제 리눅스가 아니라 안드로이드인가

B03 5절이 HAL·HIDL/AIDL·GKI를 다룬다. 여기서는 **선택 판단**만.

source.android.com이 기술하는 층: 커널("the central part of any operating system and talks to the underlying hardware"), HAL("an abstraction layer with a standard interface for hardware vendors to implement"), 네이티브 데몬·라이브러리(init, healthd, logd, libc…), ART, 시스템 서비스(system_server, SurfaceFlinger, MediaService), 프레임워크, 앱. HAL의 목적은 명시적이다: "allow Android to be agnostic about lower-level driver implementations."

### 8.1 AOSP를 고르는 이유와 안 고르는 이유

**고르는 이유.** 디스플레이·카메라·오디오·미디어·센서의 HAL 계약이 이미 정의돼 있고, 칩 벤더가 그 계약에 맞춘 코드를 이미 준다. UI 프레임워크와 그래픽 스택이 온다. Android를 아는 엔지니어를 뽑을 수 있다. 앱 모델과 권한 모델이 공짜다. **안 고르는 이유.** 이미지가 크고 부팅이 느리고 메모리를 많이 쓴다. AOSP 릴리스 주기에 일정이 묶인다. **UI를 완전히 새로 만들려면 Android가 주는 것의 절반은 오히려 방해다** — Hark의 Graphics 공고가 "building the rendering engine and the UI framework that everything else on the product is drawn with"라고 쓴 것과 Frontier UX 공고가 "Much of the work has no precedent"라고 쓴 것은, **프레임워크를 받아 쓰기보다 직접 만들고 있다는 신호**로 읽힌다 [추정]. 그렇다면 AP는 AOSP가 아니라 커스텀 eLinux일 가능성도 충분하다.

**BSP 엔지니어 입장에서의 차이는 하나로 요약된다.** 순수 리눅스에서는 "커널 드라이버를 만들면 끝"이지만, Android에서는 **커널 드라이버 + HAL 구현 + AIDL 인터페이스 + vendor 파티션 배치 + VINTF 매니페스트**까지가 하나의 작업 단위다. 이 문장 하나가 "AOSP를 안다"의 실질적 최소치다.

---

## 9. "hands-on experience with an OS"는 면접관에게 몇 단계로 들리나

### 9.1 5단계 격자

| 단계 | 이름 | 무엇을 했으면 이 단계인가 | 판별 질문 |
|---|---|---|---|
| L1 | SDK를 썼다 | 벤더 예제를 고쳐 기능을 만들었다. OS API를 호출했다 | "빌드는 어떻게 했고, 무엇을 고쳤나?" |
| L2 | 드라이버를 썼다 | 그 OS의 드라이버 모델에 맞춰 주변장치 드라이버를 작성·통합했다 | "그 OS의 드라이버가 등록되는 경로를 말해 보라" |
| L3 | BSP를 포팅했다 | 새 보드/새 SoC를 그 OS에 올렸다. 클럭·핀·메모리맵·부트 순서를 기술했다 | "안 켜졌을 때 무엇을 먼저 봤나?" |
| L4 | 스케줄러/시스템을 튜닝했다 | 우선순위·예산·인터럽트 지연을 측정해서 바꿨다. 타이밍 예산을 문서로 만들었다 | "최악 지연을 어떻게 증명했나?" |
| L5 | 커널을 디버깅·수정했다 | 커널/커널 포트 자체의 버그를 잡았다. 컨텍스트 스위치·MMU·락 경로를 건드렸다 | "그 버그의 증상과 원인을 말해 보라" |

중요한 성질: **L3~L5는 순서가 아니다.** RTOS에서 L5를 하고도 리눅스에서 L1인 사람이 흔하다. 그래서 면접관이 "hands-on 하냐"고 물을 때 실제로 알고 싶은 것은 **"OS별로 몇 단계인가"** 이고, 정직한 답은 표 하나다.

### 9.2 Don 자기 배치 그리드

레쥬메 근거(`../hark_ai_embedded_swe_context.md` 3.1절)만 사용한다. 근거 없는 칸은 비운다.

| OS / 환경 | 단계 | 근거 | 말할 때의 표현 |
|---|---|---|---|
| bare-metal (Cortex-R8/R82/M0+, Xtensa) | **L3~L5 상당** | "ARM Cortex R8/R82/M0+ FW bring-up", "SoC verification … I2C, SPI, DMA, PCIe, SRAM/DRAM bring-up", FPGA pre-silicon | "OS 없이 같은 층을 소유했다" — L5라고 부르지 말고 "OS가 없는 쪽의 등가물"이라고 말한다 |
| 사내 스케줄러 (SSD 펌웨어) | **L4 상당, 성격 미확정** | 엔터프라이즈 SSD 양산 펌웨어, firmware performance tuning | `<확인 필요: 3절 스케줄러 형태>` 를 채운 뒤에만 구체적으로 말한다 |
| FreeRTOS | L0~L1 | 레쥬메에 없음 | "안 써 봤다." Xtensa 계열에서 FreeRTOS 기반 SDK를 스쳤는지는 `<확인 필요>` — 아니면 말하지 않는다 |
| Zephyr | L1 (램프업 후) | 없음. B02/J11 실습을 하면 L1 | "개인 프로젝트로 보드를 올려 봤다" 정확히 그만큼만 |
| embedded Linux (Yocto/Buildroot) | L0 | 없음 | "구조는 안다, 운영해 본 적 없다" |
| AOSP | L0 | 없음 | 같음. 단, HAL/AIDL/GKI 경계는 설명 가능 |
| VxWorks / QNX | L0 | 없음 | "만져 본 적 없다"를 먼저 말한다. 5.7/6.6 문장으로 대화는 유지 |

### 9.3 이 격자를 면접에서 쓰는 방법

"어떤 OS를 해 봤냐"는 질문에 **격자 자체를 답으로 내는 것**이 가장 강하다. 왜냐하면 ① 거짓이 없고 ② 질문을 등급의 언어로 재정의해서 대화를 Don이 강한 쪽(L3~L5의 문제)으로 끌고 가고 ③ 면접관이 하려던 검증(어디까지 갔나)을 Don이 먼저 해 주기 때문이다. 애매하게 "RTOS 조금 해 봤습니다"로 시작하면 면접관은 **등급을 스스로 알아내야 하고, 그 과정은 심문처럼 흘러간다.**

---

## 10. 번역표 — bare-metal 멀티코어 SSD 펌웨어 ↔ OS 개념

| 영역 | Don이 한 일 (레쥬메 근거 범위) | 대응하는 OS 개념 | 안전한 영어 표현 | 넘지 말 선 |
|---|---|---|---|---|
| 스케줄링 | 코어별 역할 분할 + 사내 스케줄러로 명령 경로와 백그라운드 작업 분리 | task/thread 우선순위, 선점, RMS, CPU 예산(time partitioning, adaptive partitioning, cgroup) | "the same partitioning problem a scheduler gives you a vocabulary for" | 스케줄러 성격을 확정하기 전엔 "preemptive"라고 말하지 않는다 |
| ISR → task | 인터럽트에서 최소만 하고 데이터 경로로 넘기기, DMA 완료 처리 | deferred interrupt processing, bottom half, ISR-safe API, QNX pulse | "do almost nothing in the handler and hand it off" | 특정 RTOS의 `…FromISR` API를 써 봤다고 말하지 않는다 |
| 동기화 | 코어 간 공유 상태 보호, 요청/완료 큐 | mutex, semaphore, message passing, priority inheritance | "I had to build the primitive, not call it" | 구현했는지 사용했는지 `<확인 필요>` 확인 후 |
| 메모리 보호 | 링커 스크립트·메모리맵 직접 설계, SRAM/DRAM bring-up | MPU 영역, MMU 페이지 테이블, 프로세스 격리, VxWorks RTP, QNX 프로세스 | "the protection I had was a memory map and code review, not hardware" | "MPU/MMU를 구성해 봤다"는 근거 없으면 금지 |
| 부트 | reset 벡터 → 클럭 → DRAM 학습 → 이미지 로드, FPGA pre-silicon 포함 | BootROM → BL1/BL2 → BL31 → U-Boot → kernel → init (B03 1절) | "same ladder, fewer rungs and no signature checks in my case" | Qualcomm 스테이지 이름 단정 금지 |
| 전원관리 | 전력/성능 마진 sign-off, Power Analyzer 측정 (Apple) | idle/suspend 상태, tickless idle, runtime PM, DVFS, wake source | "I have owned the measurement side of power, not the kernel PM framework" | "runtime PM 드라이버를 썼다" 금지 |
| 관측성 | NVMe telemetry, error reporting/handling 설계 | 커널 로그·ftrace·LTTng, watchdog, crash dump, coredump | "field observability was a feature I shipped, in a world with no dmesg" | 특정 리눅스 트레이싱 툴 사용 경험 주장 금지 |

### 10.1 번역표를 쓸 때의 규칙 두 개

**규칙 1: 대응은 주장하고 동일성은 부정한다.** "같은 문제다"는 강하고 정직하다. "그러니까 해 본 셈이다"는 거짓이고, 면접관은 그 도약을 정확히 잡아낸다. 문장 끝에 항상 경계를 붙인다: "…which is the same design question, though I was solving it without a kernel." **규칙 2: 번역표는 먼저 꺼내지 않는다.** 먼저 사실(안 써 봤다)을 말하고, 그다음 번역표를 꺼낸다. 순서가 뒤집히면 변명으로 들린다.

---

## 11. 정직한 포지셔닝 스크립트 (OS 폭 질문용)

> **J11 6.3과의 차이**: J11 스크립트는 "RTOS 써 봤냐"는 **단일 질문**용이다. 여기 스크립트는 개정된 JD 문장에 대응하는 **OS 폭 질문**("eLinux, AOSP, VxWorks, RTOS 중 무엇을 해 봤냐")용이다. 둘 다 준비해 두고 질문의 폭에 따라 고른다.

### 11.1 구조

① 격자를 먼저 준다(OS별로 어디까지인지) → ② 강한 사실 한 덩어리(bare-metal L3~L5 상당) → ③ 안 해 본 것을 이름으로 명시 → ④ 그래도 지도는 있다는 증거 한 개(선택을 판단하는 문장) → ⑤ 최근에 실제로 만진 것 → ⑥ 대화를 설계 레벨로 넘기는 역질문.

### 11.2 한국어 (뜻을 먼저 몸에 붙인다)

> "OS별로 말씀드리는 게 정확할 것 같습니다. bare-metal은 제 홈그라운드입니다 — Cortex-R8/R82와 M0+를 FPGA에서 실리콘 나오기 전에 올리고, I2C·SPI·DMA·PCIe·SRAM/DRAM을 하나씩 살리고, 엔터프라이즈 SSD 양산 펌웨어의 사내 스케줄러 위에서 멀티코어로 작업을 나눠 봤습니다. 거기서는 RTOS가 대신 해 주는 일 — 스택 실측, 코어 간 동기화, 최악 지연 예산, 워치독 전략 — 을 제가 다 했습니다.
>
> 반면 상용 OS를 출하 제품에서 소유해 본 적은 없습니다. FreeRTOS와 Zephyr는 개인 실습 수준이고, embedded Linux와 AOSP는 구조를 알지만 운영해 본 적이 없고, VxWorks와 QNX는 만져 본 적이 없습니다. 이건 그냥 사실로 말씀드리는 게 맞다고 생각합니다.
>
> 제가 갖고 있는 건 선택 판단입니다. 항상 켜져 있는 MCU에 왜 리눅스를 안 올리는지, 인증 가능한 상용 RTOS를 왜 사는지, 우선순위만으로 왜 CPU 배분을 표현할 수 없어서 time partitioning이나 adaptive partitioning, cgroup 같은 예산 개념이 따로 필요한지는 설명할 수 있습니다.
>
> 그리고 최근에 손으로 해 본 것도 있습니다. <램프업 완료 후 12절 산출물로 채운다.>
>
> 여쭤보고 싶은 게 있는데, AP와 always-on MCU가 실제로 어떤 OS로 나뉘어 있고, 그 경계의 IPC를 누가 소유하고 있습니까?"

### 11.3 English — verbatim, 90~100초

> "Let me answer per OS, because a single yes or no would be misleading.
>
> Bare metal is my home ground. I brought up Cortex-R8, R82 and M0+ cores on FPGA before silicon existed, and then on production silicon — I2C, SPI, DMA, PCIe, SRAM and DRAM, one at a time. On enterprise SSD firmware I worked on top of an in-house scheduler across several cores, splitting the latency-critical command path from background work. In that world you do by hand everything an RTOS would do for you: size stacks by measurement, build the cross-core synchronisation yourself, budget worst-case latency, design the watchdog strategy.
>
> What I have not done is own a commercial OS on a shipping product. FreeRTOS and Zephyr are personal hands-on, not shipped. Embedded Linux and AOSP I can reason about — the boot chain, devicetree, the HAL and vendor boundary — but I have not operated them. VxWorks and QNX I have never touched. I would rather say that plainly than have it surface three questions in.
>
> What I do bring is the selection judgement. Why the always-on MCU does not run Linux and what it costs you when someone tries. Why anyone pays for a certifiable RTOS, and that 'certifiable' is an evidence package, not a certificate. Why priority alone cannot express 'this subsystem gets twenty percent of the CPU', which is why time partitioning in VxWorks, adaptive partitioning in QNX and cgroups in Linux all exist.
>
> And I did not want to say that without doing something about it. `<12절 산출물로 채운다>`
>
> Can I ask how the OSes are actually split between the application processor and the always-on MCU, and who owns the IPC across that boundary?"

### 11.4 절대 하지 말 것

- **"OS는 다 비슷하죠"** — OS 폭 질문에 대한 최악의 답이다. 이 질문의 요지가 "차이를 아느냐"인데 정확히 그 반대를 말하는 셈이다.
- **"VxWorks는 안 써 봤지만 RTOS니까 비슷할 겁니다"** — 커널/유저 분리와 인증 패키지를 모른다는 신호. 5.7 문장을 쓰면 같은 시간에 훨씬 좋은 인상을 준다.
- **"QNX는 리눅스 같은 거죠"** — 마이크로커널 대 모놀리식은 이 두 OS의 **거의 유일한 정체성 차이**다. 여기서 틀리면 다른 답의 신뢰도까지 깎인다.
- **"Android는 그냥 리눅스죠"** (B03 8.4와 동일) — HAL/vendor 경계를 모른다는 신호.
- **격자 없이 "좀 해 봤습니다"로 시작하기** — 뒤이은 10분이 심문이 된다.
- **Zephyr 샘플 하나 돌린 것을 "hands-on Zephyr experience"로 부풀리기** — 레쥬메 기재 규칙은 context 3.5절과 J11 7절이 이미 정했다. "personal project"라는 단어를 빼지 않는다.

---

## 12. 램프업 플랜 — 풀타임 근무 중에 갭을 답변으로 바꾸기

목표는 **전문가가 되는 것이 아니라 11.3 스크립트의 다섯 번째 문단을 사실로 채우는 것**이다. 필요한 최소 산출물은 두 개: "RTOS 위에서 무언가 만들어 봤다"와 **"embedded Linux 이미지를 직접 빌드해서 부팅시켜 봤다"**. 두 번째가 이번 개정 JD 때문에 새로 필요해진 부분이다.

### 12.1 1주 (하루 1~1.5시간, 총 8~10시간)

**Day 1~3: Zephyr 최소 경로.** 보드가 있으면 가장 좋고, 없으면 `native_sim`으로 커널 부분은 전부 된다(Zephyr 문서 확인: "does not intend to simulate any particular HW"이므로 하드웨어 의존 코드는 못 본다). 깊은 버전은 J11 6.4에 있으니 여기서는 최단 경로만.

```sh
# Zephyr Getting Started Guide 순서 (macOS). 사전 패키지는 문서 참조
west init -m https://github.com/zephyrproject-rtos/zephyr ~/zephyrproject
cd ~/zephyrproject
west update
west packages pip --install
cd ~/zephyrproject/zephyr
west sdk install

# 보드가 없을 때: 호스트 실행 파일로 빌드해서 바로 돌린다
west build -p always -b native_sim samples/basic/threads
./build/zephyr/zephyr.exe          # Ctrl+C 로 종료

# 보드가 있을 때 (보드 이름은 `west boards` 로 확인. 버전에 따라 표기가 다르다)
west build -p always -b <your-board-name> samples/basic/blinky
west flash
```

이 3일의 산출물은 하나면 된다: **ISR(또는 타이머) → 메시지 큐 → 스레드 파이프라인을 직접 써서 돌리고, 스레드 스택 사용률을 실측한 것.** 우선순위를 바꿔 가며 어느 쪽이 먼저 도는지 직접 확인한다(Zephyr는 숫자가 작을수록 높다 — C04 2.3).

**Day 4~6: embedded Linux 이미지를 직접 만들어 QEMU에서 부팅.** Buildroot가 Yocto보다 훨씬 빠르게 끝난다. 아래는 buildroot의 `board/qemu/aarch64-virt/readme.txt`에 적힌 그대로다(확인 ✅).

```sh
git clone https://gitlab.com/buildroot.org/buildroot.git
cd buildroot
make list-defconfigs | grep qemu_aarch64        # 어떤 타깃이 있나 확인
make qemu_aarch64_virt_defconfig
make menuconfig                                 # 무엇이 들어가는지 눈으로 본다 (선택)
make -j$(sysctl -n hw.ncpu)                     # 처음이면 30분~수 시간

# readme.txt에 적힌 실행 명령 그대로
qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -smp 1 \
  -kernel output/images/Image \
  -append "rootwait root=/dev/vda console=ttyAMA0" \
  -netdev user,id=eth0 -device virtio-net-device,netdev=eth0 \
  -drive file=output/images/rootfs.ext4,if=none,format=raw,id=hd0 \
  -device virtio-blk-device,drive=hd0
```

부팅되면 **반드시 안을 들여다본다** — 이게 이 3일의 진짜 목적이다.

```sh
# 게스트 안에서
cat /proc/cmdline                  # 방금 -append 로 준 것이 그대로 보인다
ls /proc/device-tree/              # QEMU가 만들어 준 DT가 런타임에 노출된다
dmesg | head -60                   # 부팅 순서를 눈으로 본다
cat /proc/interrupts               # 인터럽트 컨트롤러와 핸들러
ls /sys/class/                     # 커널이 만든 클래스들 (B03 3절 드라이버 모델의 결과물)
```

그리고 `output/images/`에 무엇이 나왔는지(`Image`, `rootfs.ext4`)와 `output/`의 구조(`build/`, `host/`, `staging/`, `target/`)를 확인한다 — Buildroot 매뉴얼이 명시하는 레이아웃이다.

**Day 7: 문장으로 만든다.** 11.3 스크립트의 다섯 번째 문단을 두 문장으로 채운다. 예: "I built a Zephyr image with an ISR feeding a message queue into a thread and measured the stack headroom, and I built a Buildroot aarch64 image from a defconfig and booted it under QEMU to walk the boot log and the runtime device tree." **이 두 문장이 1주의 전부이고, 이게 있고 없고가 스크립트의 신뢰도를 가른다.**

### 12.2 1개월 (주당 4~6시간)

**Week 1**: 위 1주 계획.

**Week 2 — Zephyr에서 L2로 올라간다.** devicetree 오버레이로 핀을 바꾸고 `build/zephyr/zephyr.dts`와 `devicetree_generated.h`를 실제로 열어 본다(절차는 B02). 기존 바인딩을 쓰는 센서 하나를 DT에 붙여 읽어 본다. 목표 문장: **"Zephyr에서 보드 정의가 코드로 바뀌는 경로를 파일 단위로 따라가 봤다."**

**Week 3 — 리눅스 커널을 직접 빌드한다.** 라즈베리파이가 있으면 크로스 컴파일이 가장 빠른 현실 경험이다(raspberrypi.com 문서 확인 ✅). 기기 없이도 소스 트리 구조와 defconfig 흐름은 볼 수 있다.

```sh
git clone --depth=1 https://github.com/raspberrypi/linux
cd linux
# Pi 5 계열 예시. 보드마다 defconfig 이름이 다르다 — 문서의 표를 확인할 것
KERNEL=kernel_2712
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- bcm2712_defconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image modules dtbs -j8
```

여기서 봐야 할 것은 컴파일 성공이 아니라 **`arch/arm64/boot/dts/` 안의 `.dts`/`.dtsi`/`overlays/` 구조**다. 보드 리비전을 오버레이로 다루는 방식이 Don이 Apple에서 겪은 리비전 문제와 같은 문제의 리눅스식 답이다.

**Week 4 — OS 선택을 말로 정리한다.** 2.1 결정 트리와 9.2 격자를 백지에 그리고, 14절 질문 17개를 소리 내어 답한다. 5.7과 6.6 문장을 외운다. 11.3 스크립트를 타이머로 재서 100초 안에 맞춘다.

### 12.3 이 램프업이 끝나면 쓸 수 있게 되는 표현

| 쓸 수 있다 | 여전히 쓸 수 없다 |
|---|---|
| "hands-on with Zephyr (personal project): devicetree, ISR→queue→thread, stack measurement" | "Zephyr 경험 N년", "production Zephyr" |
| "built and booted a Buildroot embedded Linux image under QEMU" | "embedded Linux BSP 소유", "Yocto 레이어 관리" |

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| "RTOS 써 봤다"를 등급 없이 말한다 | 두 번째 꼬리질문에서 답이 얇아지고, 그 뒤 모든 답의 신뢰도가 떨어진다 | hands-on의 5단계(9.1)를 모른다 | 격자를 먼저 준다. OS별로 L 번호를 붙여 말한다 |
| QNX를 "리눅스 비슷한 것"으로 말한다 | 면접관이 즉시 마이크로커널을 되묻고 대화가 끊긴다 | 정체성 차이가 커널 구조라는 걸 모른다 | 6.2의 그림 한 장 + 6.6 문장 |
| "certifiable"과 "certified"를 섞어 쓴다 | 안전 도메인 출신 면접관에게 확실한 감점 | 인증이 제품 단위라는 것을 모른다 | 5.4의 구분을 한 문장으로 외운다 |
| 우선순위로 CPU 배분을 설명한다 | "그럼 로깅 프로세스가 폭주하면요?"에 답이 없다 | 예산(budget) 개념이 우선순위와 직교임을 모른다 | time partitioning / adaptive partitioning / cgroup을 한 묶음으로 |
| always-on MCU에 리눅스를 제안한다 | 전력·RAM 예산 질문에서 즉시 막힌다 | 2.2 자릿수 감각이 없다 | 결정 트리 질문 1~2로 되돌아간다 |
| 추정한 제품 구조를 사실처럼 말한다 | "그건 어디서 보셨나요?"에서 무너진다 | [추정] 표기를 말로 옮기지 않았다 | 2.4처럼 추측을 **질문으로** 바꾼다 |

---

## 14. 면접 질문 17개

### Q01. [기초] Which embedded operating systems have you worked with, hands-on?

**왜 묻나**: 개정된 JD 문장의 직접 검증. 그리고 과장 여부를 첫 30초에 본다.
**30초 답변**: OS별 격자로 답한다. bare-metal과 사내 스케줄러는 L3~L5 상당, Zephyr/FreeRTOS는 개인 실습, eLinux/AOSP는 구조만, VxWorks/QNX는 없음. 그다음 "OS가 없는 쪽에서 같은 문제를 풀었다"로 넘어간다.
**English answer**: "Let me answer per OS rather than yes or no. Bare metal and an in-house scheduler on multi-core SSD firmware is where I have real depth — bring-up on Cortex-R and Cortex-M, stacks sized by measurement, cross-core synchronisation built by hand. FreeRTOS and Zephyr are personal hands-on. Embedded Linux and AOSP I can reason about but have not operated. VxWorks and QNX I have never touched."
**꼬리질문**: "그럼 사내 스케줄러는 선점형이었나?" → Q12. / "왜 상용 RTOS를 안 썼나?" → 3절 bare-metal이 푸는 문제. / "가장 빠르게 배울 수 있는 건 뭔가?" → 12절.

### Q02. [기초] What is the actual difference between an RTOS and embedded Linux?

**왜 묻나**: 가장 기본적인 판별. 여기서 "RTOS는 빠르고 리눅스는 느리다"라고 답하면 끝난다.
**30초 답변**: 차이는 속도가 아니라 **보장과 범위**다. RTOS는 최악 지연을 예측 가능하게 만드는 데 최적화된 스케줄러 + 라이브러리이고, 메모리 보호도 없거나 얇다. 리눅스는 MMU 기반 프로세스 격리와 거대한 서브시스템 생태계를 주고, 대가로 기본 커널이 최악 지연을 보장하지 않는다.
**English answer**: "It is not speed, it is guarantees and scope. An RTOS gives you a scheduler you can reason about for worst-case latency, in a few tens of kilobytes, usually in one address space. Linux gives you MMU-backed process isolation and an enormous set of subsystems you do not have to write — networking, filesystems, graphics, ML runtimes — and in exchange the stock kernel makes no worst-case latency promise. Most products end up with both, on different processors."
**꼬리질문**: "리눅스를 실시간으로 만들 방법?" → 7.2 세 가지. / "그럼 MPU만 있는 칩에서는?" → RTOS + MPU 스레드 격리. / "왜 둘 다 필요한가?" → Q03.

### Q03. [기초] Why put an RTOS on a second MCU instead of doing everything on the Linux AP?

**왜 묻나**: 0절 구조를 스스로 설명할 수 있는지. 컨슈머 배터리 기기의 핵심 설계다.
**30초 답변**: 전력, 웨이크 지연, 타이밍 보장 세 가지. AP는 깨어 있는 것 자체가 비싸고 부팅/재개도 느리다. 항상 켜져 있어야 하는 일(마이크 감시, IMU, 버튼, 전원 상태기계)을 수십 µA급 MCU로 내리고, AP는 필요할 때만 깨운다. 부수 효과로 MCU가 타이밍 보장이 필요한 일을 떠안는다.
**English answer**: "Three reasons. Power — keeping an application processor awake costs orders of magnitude more than a Cortex-M running at microamps. Latency to respond — the MCU is already awake, so it can catch the wake word or the button and then bring the AP up. And timing guarantees — anything with a hard deadline moves off the OS that will not promise one. The MCU becomes the thing that decides when the expensive half of the product exists."
**꼬리질문**: "둘 사이 IPC는?" → mailbox/IPCC, shared memory + doorbell, SPI/UART 명령 프로토콜. / "MCU 펌웨어 업데이트는 누가?" → OTA 다중 이미지(C07). / "MCU가 AP를 깨우는 판단이 틀리면?" → false accept 예산 + 전력 회귀.

### Q04. [기초] What does "real-time" actually mean? Is Linux real-time?

**왜 묻나**: 이 단어를 마케팅으로 쓰는 사람과 정의로 쓰는 사람을 가른다.
**30초 답변**: 실시간은 빠름이 아니라 **마감을 지킨다는 보장**이다. hard는 마감 미스가 실패, soft는 품질 저하. 기본 리눅스는 hard real-time이 아니고, 실시간 프리엠션 구성을 쓰면 최악 지연이 크게 좋아지지만 여전히 코드 전체를 증명하는 것과는 다르다.
**English answer**: "Real-time means bounded, not fast. Hard real-time means missing a deadline is a failure; soft means it degrades quality. Stock Linux is not hard real-time — you can improve worst-case latency a lot with the realtime preemption configuration, and measure it with cyclictest, but that is a statistical argument about a huge codebase. When I actually needed a bound I proved it the other way: keep the path bare metal and read every branch on it."
**꼬리질문**: "최악 지연을 어떻게 측정하나?" → 하드웨어 타이머 + GPIO 토글 + 오실로스코프, 트레이스. / "평균과 최악 중 무엇을 스펙에 쓰나?" → 최악, 그리고 분포 꼬리. / "PREEMPT_RT는 메인라인인가?" → **커널 버전에 따라 다르므로 단정하지 않는다**고 말한다.

### Q05. [기초] The word "BSP" means different things in VxWorks, Zephyr and Linux. Explain.

**왜 묻나**: 직무명이 BSP다. 이 단어를 OS별로 정확히 쓸 수 있는지 본다.
**30초 답변**: 공통 정의는 "이 보드에서 OS가 돌게 하는 코드와 데이터"다. VxWorks는 그걸 문자 그대로 BSP라고 부르고 벤더가 판다. Zephyr는 보드를 devicetree + Kconfig + board.yml 데이터로 기술한다. 리눅스에서는 "BSP"가 벤더가 주는 커널 포크 + 부트로더 + DT + Yocto 레이어 묶음을 뜻한다.
**English answer**: "The invariant is the same: whatever makes this OS run on this board. In VxWorks it is literally a product — Wind River sells BSPs and BSP development services. In Zephyr the board is data: a board.yml, a devicetree, a defconfig, and the driver bindings. In Linux 'BSP' usually means the vendor drop — a forked kernel, the bootloader, the device tree and often a Yocto layer — and the real work is keeping your product on it as the vendor and upstream both move."
**꼬리질문**: "벤더 BSP를 받으면 먼저 뭘 하나?" → B03 7.1. / "보드 리비전은 어떻게 다루나?" → Zephyr revision / Linux dtbo. / "devicetree와 Kconfig 역할 차이?" → B02 핵심 질문.

### Q06. [중급] Pick an OS for a new always-on audio MCU. Walk me through the decision.

**왜 묻나**: 판단을 본다. 정답은 없고 **기준의 순서**가 평가 대상이다.
**30초 답변**: 순서를 말한다. ① RAM/Flash 예산과 전력 예산 → 리눅스는 탈락 ② 벤더 SDK가 무엇 위인가 → 이게 실질적 결정자 ③ 필요한 스택(BLE 호스트, 오디오, 전원관리, OTO)이 딸려 오는가 ④ 보드 리비전 관리 방식 ⑤ MPU 격리·인증 필요 여부. 결론은 대개 "칩을 고른 순간 정해져 있다".
**English answer**: "I would start from the budgets — if the part has a few hundred kilobytes of RAM and a microamp average current target, Linux is out before we discuss anything else. Then the honest determinant: what the vendor SDK is built on, because fighting it means porting the BLE host and the power management yourself. Then what ships with the kernel versus what I assemble. Then how board revisions are described, which matters a lot between EVT and PVT. Certification and user-mode isolation only if the product needs them. Usually the answer was decided when we picked the silicon, and I would want to say that out loud rather than pretend it was a clean OS bake-off."
**꼬리질문**: "SDK를 거슬러 본 경험?" → 없으면 없다고. / "Zephyr vs FreeRTOS 하나만?" → 4.1의 둘째·셋째 기준. / "나중에 바꿀 수 있나?" → 추상화 경계를 어디 두느냐 문제.

### Q07. [중급] What is VxWorks, and how is it different from FreeRTOS?

**왜 묻나**: JD에 이름이 있고, Don은 안 써 봤다. **정직 + 정확**이 평가된다.
**30초 답변**: 먼저 "안 써 봤다". 그다음 구조 차이: FreeRTOS는 커널 몇 파일짜리 라이브러리이고 프로세스 개념이 없다. VxWorks는 상용 OS로 커널 task와 **메모리 보호된 유저 공간(RTP)** 이 공존하고, POSIX PSE52 conformant이며, time/space partitioning을 제공하고, 진짜 차별점은 **안전 인증 증거 패키지**(DO-178C, IEC 61508, ISO 26262, IEC 62304)다.
**English answer**: 5.7 문장을 그대로 쓴다.
**꼬리질문**: "왜 그럼 안 쓰는 회사가 많나?" → 비용과 생태계, 그리고 컨슈머는 인증 대상이 아니다. / "certifiable이 무슨 뜻인가?" → Q17. / "RTP와 커널 task 중 뭘 고르나?" → 보호 대 비용의 컴포넌트별 선택.

### Q08. [중급] Explain QNX's microkernel model. What does it buy you, and what does it cost?

**왜 묻나**: Platform 공고에 QNX가 명시돼 있다. 구조를 아는지 본다.
**30초 답변**: 커널에는 스케줄링·메시지 패싱·타이머·인터럽트만 두고, 파일시스템·네트워크·드라이버는 **평범한 프로세스**(resource manager)로 밖에 둔다. 이득은 장애 격리와 동적 재시작, 그리고 스케일 다운. 비용은 함수 호출이 프로세스 경계 메시지가 되는 것. 그래서 동기 메시지 패싱(`MsgSend`/`MsgReceive`/`MsgReply`)을 아주 싸게 만드는 게 QNX 엔지니어링의 중심이고, 우선순위 역전은 message-driven priority inheritance로 막는다.
**English answer**: 6.6 문장을 그대로 쓴다.
**꼬리질문**: "드라이버를 재시작할 수 있다는 게 실제로 얼마나 유용한가?" → 장기 가동 기기에서 크지만, 상태 복구 설계가 따라야 한다. / "Linux도 유저공간 드라이버가 있지 않나?" → FUSE/UIO/VFIO가 있지만 기본 모델은 여전히 in-kernel. / "adaptive partitioning?" → Q11 꼬리.

### Q09. [중급] Yocto or Buildroot? How do you decide?

**왜 묻나**: eLinux를 실제로 다뤄 본 사람만 이 답에 뉘앙스가 있다.
**30초 답변**: 제품 수와 수명이 기준이다. 이미지 하나를 빠르게 만들고 런타임 패키지 관리가 필요 없으면 Buildroot. 제품 라인을 여러 개, 여러 해 유지하고 라이선스·SBOM·레이어 재사용·벤더 BSP 레이어 통합이 필요하면 Yocto. 그리고 실제로는 **벤더가 Yocto 레이어로 BSP를 주면 그게 결정을 대신한다.**
**English answer**: "Buildroot when I want one image, quickly, and I do not need runtime package management — the manual describes it exactly that way, a tool that builds a complete Linux system by cross-compilation. Yocto when there is a product line rather than a product: layers I can reuse, license and SBOM output, and the ability to drop in a vendor BSP layer. And Yocto is not a distribution — it is a system for building one, which is also why its learning curve is real. In practice the vendor's BSP format often decides for you."
**꼬리질문**: "빌드 시간은 어떻게 줄이나?" → sstate/ccache, 공유 캐시, CI 빌더. / "레시피를 써 본 적?" → 없으면 없다고. / "이미지에 뭐가 들어갔는지 어떻게 증명하나?" → 매니페스트/SBOM.

### Q10. [중급] We ship AOSP on the AP. As a BSP engineer, what changes versus plain Linux?

**왜 묻나**: 우대 항목이고 Systems 공고 우대에도 있다. 경계를 아는지 본다.
**30초 답변**: 작업 단위가 커진다. 순수 리눅스는 커널 드라이버로 끝이지만 Android는 **커널 드라이버 + HAL 구현 + AIDL 인터페이스 + vendor 파티션 배치 + VINTF 매니페스트**까지가 한 덩어리다. 그리고 커널이 GKI/KMI로 고정되므로 vendor module로 빼야 한다. HAL의 목적은 문서대로 "allow Android to be agnostic about lower-level driver implementations"다.
**English answer**: "The unit of work gets bigger. On plain Linux a new sensor is a kernel driver and a sysfs or IIO interface. On Android it is the driver plus a HAL implementation, a stable AIDL interface, placement on the vendor partition, and a VINTF manifest entry — because the whole point of the HAL is to let the framework stay agnostic about the driver underneath. And the kernel is not freely mine: with GKI the generic image is fixed and my code goes in as a vendor module against the KMI."
**꼬리질문**: "HIDL과 AIDL?" → HIDL deprecated, 신규는 AIDL(B03 5.3). / "CTS/CDD가 뭘 강제하나?" → 호환성, 안전 인증과 다르다. / "AOSP 릴리스 올릴 때 가장 아픈 것?" → vendor 코드 리베이스.

### Q11. [중급] Two OSes on one product. How do they talk, and how do you debug across that boundary?

**왜 묻나**: Don이 강한 영역이다(Apple에서 호스트-라디오 경계 디버깅이 본업). 반드시 잘 답해야 한다.
**30초 답변**: 채널은 셋 중 하나. 공유 메모리 + doorbell/mailbox 인터럽트, 직렬 버스 위의 명령 프로토콜(SPI/UART/I2C), 또는 표준 프로토콜(HCI over UART/SDIO/PCIe). 디버깅은 **양쪽에 같은 타임스탬프를 만들 수 있느냐**가 전부다. 그게 안 되면 GPIO 토글 + 로직 분석기로 물리적 공통 시간축을 만든다. 실제 사고는 대개 경계의 계약 불일치 — 엔디언, 패딩, 버전, 링버퍼 인덱스 소유권, 인터럽트 유실.
**English answer**: "Shared memory with a doorbell interrupt, a command protocol over SPI or UART, or a standard transport like HCI over UART, SDIO or PCIe. Debugging it comes down to one question: can I put both sides on one timeline. If the two OSes cannot agree on a timestamp, I make a physical one — toggle a GPIO on each side and capture with a logic analyser, which is exactly how I chased host-interface failures on new wireless silicon at Apple. Most real bugs there are contract bugs: struct padding, endianness, who owns the ring index, a version skew between two images that shipped separately, or a lost edge-triggered interrupt."
**꼬리질문**: "인터럽트 유실을 어떻게 증명하나?" → 카운터 양쪽 비교 + 파형. / "두 이미지 버전 스큐는 어떻게 막나?" → 경계 버전 협상 + OTA 동시 업데이트(C07). / "우선순위 역전이 경계를 넘어 생기면?" → 서버 측 우선순위, QNX의 message-driven inheritance가 같은 문제의 OS 차원 답.

### Q12. [심화] Your in-house scheduler — was it preemptive? Walk me through it.

**왜 묻나**: Q01의 필연적 후속이고, **Don의 답이 가장 크게 갈리는 질문**이다. 잘 답하면 "RTOS 커널을 만든 사람"이 되고, 부풀리면 즉시 들킨다.
**30초 답변**: `<확인 필요: 3절 스케줄러 형태>` 를 채운 뒤에만 구체적으로 답한다. 답의 골격은 어느 경우든 같다. ① 실제 구조를 이름으로 말한다(선점형/협조적/인터럽트+상태기계) ② 전환 시점이 무엇이었나 ③ 최악 응답시간을 무엇이 결정했나 ④ 공유 상태를 어떻게 보호했나 ⑤ 지금 다시 만들면 무엇을 바꿀까.
**English answer (골격, 대괄호를 사실로 채운다)**: "It was [exact model]. Switching happened on [trigger]. The worst-case response on the command path was bounded by [longest non-preemptible section], and we knew that number because [how it was measured]. Shared state across cores was protected by [mechanism]. If I built it again the thing I would change is [one concrete thing], and that is also the part an off-the-shelf kernel would have given me for free."
**꼬리질문**: "컨텍스트 스위치를 직접 구현했나?" → 사실만. / "크리티컬 섹션 최장 구간은?" → 측정 방법을 말한다. / "왜 RTOS를 안 썼나?" → 3절. / "그 경험이 Zephyr에서 어떻게 쓰이나?" → 10절 번역표.

### Q13. [심화] After a new Linux driver landed, audio started under-running occasionally. Find it.

**왜 묻나**: 리눅스 경험이 없어도 **방법론**은 평가할 수 있다. Don의 강점 영역으로 끌어올 수 있는 질문.
**30초 답변**: 먼저 증상을 숫자로 만든다(언제, 얼마나 자주, 버퍼 언더런 카운터). 그다음 세 가설로 나눈다: 인터럽트가 늦게 처리된다(지연), 인터럽트가 유실된다, 또는 데이터 생산이 늦다. 커널 쪽은 트레이싱(ftrace/trace-cmd, 인터럽트 지연 측정)으로, 하드웨어 쪽은 DMA 완료 시점을 GPIO로 밖에 내보내 로직 분석기로 본다. 새 드라이버가 크리티컬 섹션을 길게 잡거나 인터럽트를 오래 막는지 본다.
**English answer**: "First I make it measurable — under-run counter, rate, and whether it correlates with any other activity. Then three hypotheses: the interrupt is serviced late, the interrupt is lost, or the producer is late. For 'late' I would use kernel tracing to get interrupt and scheduling latency distributions, and I would bisect by loading and unloading the new driver. For anything I do not trust in software I fall back to what I do every day: toggle a GPIO at the DMA completion and at the consumer, and put a logic analyser on both. On SSD firmware the equivalent bug was almost always someone holding a lock or masking interrupts across a long path, and I would look there first."
**꼬리질문**: "재현이 몇 시간에 한 번이면?" → 장시간 캡처 + 트리거 조건 + 링버퍼 트레이스. / "드라이버를 못 고치는 벤더 바이너리면?" → 경계를 재설계(더 큰 버퍼, DMA 체인, 일을 MCU로 이동). / "예방은?" → 지연 회귀 테스트를 CI에.

### Q14. [심화] How would you prove an OS choice meets the latency budget before committing to silicon?

**왜 묻나**: Don의 FPGA pre-silicon 경험이 정확히 이 질문이다. **가장 유리한 심화 질문.**
**30초 답변**: 세 단계. ① 예산을 먼저 쓴다 — 어떤 이벤트가 몇 µs/ms 안에 어떤 응답을 해야 하는지 표로. ② 가장 싼 실험으로 가장 비싼 불확실성을 먼저 죽인다: FPGA 프로토타입이나 평가 보드에 후보 OS를 올려 **합성 워크로드**로 최악 지연을 측정한다. 인터럽트→응답을 GPIO로 밖에 내보내 파형으로 잡는다. ③ 측정을 스펙에 붙인다 — 평균이 아니라 분포의 꼬리, 그리고 최악을 만들어 내는 조건.
**English answer**: "I write the budget before I measure anything: which event, which deadline, which response. Then I buy certainty in the cheapest order. At Solidigm we ran firmware on an FPGA before silicon existed, so I am used to answering timing questions on a prototype that is slower but structurally real. I would put each candidate OS on an eval board or the FPGA, drive it with a synthetic worst case rather than a demo, and get interrupt-to-response out on a GPIO so the number comes from a scope and not from printf. And I would report the tail of the distribution and the conditions that produce it, because the average is not the thing that ships."
**꼬리질문**: "합성 워크로드를 어떻게 만드나?" → 최악을 만드는 조건을 설계(동시 DMA, 캐시 미스, 최대 인터럽트율). / "FPGA는 느린데 어떻게 외삽하나?" → 사이클 수 기준 + 병목 위치 유지. / "OS 두 개가 비슷하면?" → 생태계·유지비로 tie-break.

### Q15. [심화] Design the OS topology for this product: two radios, a mic array, a display, an on-device model, three-day battery.

**왜 묻나**: 시스템 설계 질문이고 0절 그림 전체를 쓴다. **Don이 이 노트에서 가장 잘 답할 수 있는 문항.**
**30초 답변**: 층을 먼저 그리고 각 층에 이유를 붙인다. always-on MCU에 RTOS(마이크 감시, IMU, 버튼, 전원 상태기계, AP 웨이크 판단), AP에 리눅스 계열(디스플레이, 네트워크, 큰 모델, OTA), 라디오는 벤더 펌웨어 블랙박스. 그다음 **경계 세 개의 계약**을 정의한다: MCU↔AP IPC, AP↔라디오 전송, 전원 상태 기계의 소유권. 마지막으로 3일 배터리를 **평균 전류 예산 표**로 분해해서 "AP가 깨어 있는 시간 비율"이 지배항임을 보인다.
**English answer**: "I would draw three domains and defend each. An always-on MCU on an RTOS owns the microphone watch, the IMU, the buttons and the power state machine, because that is the only way a three-day target survives. The application processor runs Linux or AOSP and owns the display, the network, the model and OTA, and it is asleep by default. The radios are vendor firmware I do not own, so I define the transport contract instead. Then I would spend most of the time on the three boundaries — the MCU-to-AP IPC, the radio transport, and who is allowed to change power state — because that is where products like this actually fail. And I would size the battery from an average-current table where the dominant term is what fraction of the day the AP is awake, not the peak numbers."
**꼬리질문**: "웨이크워드 오검출률이 전력에 미치는 영향?" → false accept × AP 웨이크 비용. / "모델을 MCU에 올릴 수 있나?" → C08 예산 계산. / "OTA는 몇 개 이미지인가?" → AP + MCU + 라디오, 원자성과 롤백(C07). / "디스플레이가 AP를 못 자게 하면?" → 프레임 소스를 MCU로 내리거나 패널 self-refresh.

### Q16. [심화] You have two weeks to bring up a commercial RTOS you have never used, on a new board. Plan it.

**왜 묻나**: 램프업 속도와 작업 순서 감각. Don의 bring-up 경험이 직접 답이 된다.
**30초 답변**: bring-up 순서는 OS가 바뀌어도 같다. ① 벤더가 주는 레퍼런스 보드에서 먼저 돌린다(내 보드 문제와 내 무지를 분리) ② 최소 경로만: 클럭 → UART 콘솔 → 타이머 틱 → 인터럽트 하나 ③ 그다음 하나씩 켠다: GPIO, I2C, SPI, DMA ④ 매 단계 스코프/로직 분석기로 물리적 확인 ⑤ 안 되면 항상 "레퍼런스와 내 보드의 차이"로 되돌아간다. OS 특이점은 문서에서 세 가지만 먼저 찾는다: 부팅 진입점, 인터럽트 등록 방식, 콘솔 출력.
**English answer**: "The order does not change with the OS. Day one, get it running on the vendor's reference board, so my board's problems and my ignorance are never the same variable. Then the minimum path: clocks, a UART console, a timer tick, one interrupt. Then peripherals one at a time, each confirmed on a scope or a logic analyser rather than by a log line. From the OS documentation I would only chase three things at first — where execution enters, how an interrupt handler is registered, and how the console gets out — because almost everything else can wait. That is exactly how I brought up Cortex-R and Cortex-M on FPGA before silicon, and the unfamiliar kernel is a smaller unknown than a new SoC is."
**꼬리질문**: "레퍼런스 보드가 없으면?" → 시뮬레이터(VxWorks 내장 시뮬레이터, Zephyr native_sim, QEMU). / "2주 후 무엇을 못 했을 것인가?" → 전력 튜닝, 최악 지연 증명, 생산 빌드. 정직하게 말한다. / "가장 큰 리스크?" → 벤더 문서 접근과 툴 라이선스.

### Q17. [심화] What does a "safety-certifiable OS" actually mean, and does a consumer wearable need one?

**왜 묻나**: 5.4의 구분을 아는지. 안전 도메인 출신 면접관이 있으면 이 질문이 꼭 나온다.
**30초 답변**: 벤더가 파는 것은 인증서가 아니라 **증거 패키지**(요구사항 추적성, 테스트 커버리지 근거, 도구 검증 자료, 결정성 논거)다. 인증을 받는 주체는 제품이고, 심사 표준은 도메인마다 다르다(DO-178C 항공, IEC 61508 산업, ISO 26262 자동차, IEC 62304 의료). 컨슈머 웨어러블은 이 표준들의 대상이 아니다. 다만 배터리·무선·의료 주장 같은 **다른 종류의 규제**는 존재한다.
**English answer**: "Certifiable is not certified. What the vendor sells is an evidence package — requirement traceability, coverage rationale, tool qualification, a determinism argument — that lets your product's certification go through. The certificate belongs to the product, and the standard depends on the domain: DO-178C in avionics, IEC 61508 in industrial, ISO 26262 in automotive, IEC 62304 for medical devices. A consumer wearable is not in scope for any of those, which is the main reason I would not expect VxWorks or QNX in this product. What a wearable does have is a different regulatory surface — battery and wireless certification, and anything that looks like a medical claim."
**꼬리질문**: "그럼 인증이 필요 없으면 결정성은 안 중요한가?" → 오디오 글리치는 제품 품질이다. 보장은 계속 필요하다. / "인증 OS를 안 쓰고 안전 기능을 구현하려면?" → 안전 기능을 별도 MCU로 분리. / "그 증거 패키지를 본 적 있나?" → 없다고 말한다.

---

## 15. 확인 필요 (Don이 직접 채울 것)

- `<확인 필요: SSD 펌웨어 사내 스케줄러의 정확한 형태 — 선점형 / 협조적 / 인터럽트+상태기계. 3절 표에서 하나를 고를 수 있어야 하고, 코어별로 달랐다면 그 차이까지. Q12의 정확도가 여기에 전부 걸려 있다.>`
- `<확인 필요: 컨텍스트 스위치 코드나 스케줄러 코어 로직을 직접 작성·수정했는가, 아니면 이미 있던 프레임워크 위에서 태스크를 추가했는가.>`
- `<확인 필요: 코어 간 통신이 shared memory + doorbell/mailbox 인터럽트 구조였는가. 그리고 그 요청 처리에서 우선순위/기아 문제를 실제로 다뤘는가 (6.5절, Q11).>`
- `<확인 필요: 최악 지연·응답시간을 어떤 방법으로 측정했는가 — 하드웨어 타이머 카운터, GPIO + 스코프, 트레이스 버퍼, 호스트 측 측정. Q14의 구체성이 여기서 나온다.>`
- `<확인 필요: Xtensa 작업에서 FreeRTOS 기반 SDK를 접한 적이 있는가 (J11 6.5와 동일 항목). 없으면 절대 말하지 않는다.>`
- `<확인 필요: SSD 펌웨어에서 MPU 또는 메모리 보호를 설정해 본 적이 있는가. 있으면 10절 '메모리 보호' 행이 훨씬 강해진다.>`
- `<확인 필요: 12절 램프업을 실제로 수행했는가. 수행 전에는 11.2/11.3 스크립트의 다섯 번째 문단을 말하면 안 된다.>`

---

## 16. 요약 & 체크리스트

임베디드 OS는 다섯 클래스이고, 고르는 기준은 성능이 아니라 **무엇을 보장해야 하고 무엇을 남에게서 받아 쓸 것인가**다. bare-metal은 증명해야 하는 경로를 위해, RTOS는 주기가 다른 일이 늘어났을 때의 구조를 위해, embedded Linux는 남이 만든 생태계를 위해, AOSP는 프레임워크와 인력 시장을 위해, VxWorks·QNX는 **문서로 제출할 증거**를 위해 존재한다. Hark JD가 이 이름들을 나열한 것은 한 제품에 OS가 여러 개 있기 때문이고(JD가 직접 그렇게 쓴다), VxWorks·QNX는 팀 전체 템플릿의 "OS 폭" 문구일 가능성이 높다 [추정]. Don의 전략은 하나다: **hands-on을 5단계 격자로 재정의해서 자기 좌표를 먼저 밝히고**, bare-metal에서 같은 문제를 푼 증거를 번역표로 제시하고, VxWorks·QNX는 정확히 설명하되 경험을 주장하지 않는다. 그리고 1주 안에 Zephyr 하나와 Buildroot 이미지 하나를 실제로 만들어 스크립트의 빈 문단을 사실로 채운다.

- [ ] 2.1 결정 트리(질문 4개)를 백지에 그릴 수 있다
- [ ] 3절 bare-metal 다섯 형태를 구분해 말하고, **자기 경험이 어디였는지 정확히 안다** (15절 1번)
- [ ] VxWorks를 커널 task / RTP / POSIX PSE52 / time-space partitioning / 인증 네 단어로 설명한다
- [ ] "certifiable ≠ certified"를 한 문장으로 말한다
- [ ] QNX의 마이크로커널 그림(6.2)을 그리고 `MsgSend`/`MsgReceive`/`MsgReply`와 message-driven priority inheritance를 말한다
- [ ] adaptive partitioning / time partitioning / cgroup을 "우선순위로는 표현할 수 없는 CPU 예산"이라는 한 묶음으로 설명한다
- [ ] AOSP에서 센서 하나 추가의 작업 단위 5개(드라이버·HAL·AIDL·vendor 파티션·VINTF)를 나열한다
- [ ] 9.1 hands-on 5단계를 외우고, 9.2 자기 격자를 **근거와 함께** 말할 수 있다
- [ ] 10절 번역표 7행을 "대응은 주장하고 동일성은 부정하는" 문장으로 말한다
- [ ] 11.3 영어 스크립트를 100초 안에 막힘없이 말한다 (다섯 번째 문단은 램프업 후 채움)
- [ ] 12.1을 **실제로 실행한다** — `native_sim` 또는 보드에서 Zephyr 1개, QEMU에서 Buildroot 이미지 1개
- [ ] 14절 17문항을 소리 내어 답해 보고, Q12·Q14·Q15는 화이트보드로 설명한다
- [ ] 15절 `<확인 필요>` 8개를 본인 기억으로 채운다 — 특히 1번

---

## 참고 자료

WebFetch로 내용을 직접 확인한 페이지에 ✅를 표시했다. 확인 실패·버전 의존 항목은 비고에 적었다.

| 자료 | 확인 | 무엇을 확인했나 | URL |
|---|---|---|---|
| Wind River — VxWorks 제품 페이지 | ✅ 2026-09-29 | "Safety-Certifiable, 600+ Programs Certified", DO-178C/ED-12C·IEC 61508·IEC 62304·ISO 26262 언급, Cert Edition·653 존재, Helix가 "VxWorks, Linux and Android"를 동시 실행, 결정성 마케팅 문구 | https://www.windriver.com/products/vxworks |
| Wind River — VxWorks 7 데이터시트 (PDF, Rev. 12/2019) | ✅ 2026-09-29 | AMP/SMP/BMP, "Priority-based preemption with optional round-robin", "Time and space partitioning", "Separation between kernel and memory-protected user space environments", "POSIX PSE52 conformant", C11/C++17·Rust·Python 3.8, "Over 80 different boards supported", 안전 인증 4종(DO-178C DAL A, IEC 61508 SIL 3, ISO 26262 ASIL D, IEC 62304), Workbench = Eclipse 기반 IDE, "Built-in VxWorks simulator" | https://www.windriver.com/themes/Windriver/pdf/vxworks-7-datasheet.pdf |
| Wind River Learning — VxWorks 7 Real-Time Processes | 🟡 검색 스니펫만 | RTP = "a way to execute VxWorks applications in user space", 보호 메모리 모델·시스템 콜·RTP 간 공유 라이브러리 | https://learning.windriver.com/real-time-processes |
| QNX — System Architecture 개요 (SDP 8.0) | ✅ 2026-09-29 | POSIX API 목표 문장, 마이크로커널의 범위, 파일시스템·네트워크가 커널 밖 프로세스, resource manager 동적 시작/정지 | https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.sys_arch/topic/intro.html |
| QNX — The microkernel (SDP 8.0) | ✅ 2026-09-29 | `procnto`가 마이크로커널 + 프로세스 매니저, "The POSIX features that aren't implemented in the procnto microkernel … are provided by optional processes and shared libraries" | https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.sys_arch/topic/kernel.html |
| QNX — Interprocess Communication (SDP 8.0) | ✅ 2026-09-29 | `MsgSend()`/`MsgReceive()`/`MsgReply()`가 동기식이고 복사, 채널·연결, pulse = "fixed-size, nonblocking messages", "message-driven priority inheritance" | https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.sys_arch/topic/ipc.html |
| QNX — Thread scheduling (SDP 8.0) | ✅ 2026-09-29 | 스케줄링 결정 시점(커널 콜·예외·하드웨어 인터럽트), "Threads are scheduled globally across all processes", 최고 우선순위 준비 스레드 실행, 블록 해제는 큐 뒤 / 선점은 큐 앞 | https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.sys_arch/topic/kernel_SCHEDULING.html |
| QNX — Adaptive Partitioning 개요 (6.5 문서) | ✅ 2026-09-29 | 파티션 = "a virtual wall", budget = CPU 시간 비율, microbilling, 미사용 예산 재분배, 설계 목표 2개 | https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.adaptive_partitioning_en_user_guide/ap_overview.html |
| QNX 제품 사이트 | 🟡 2026-09-29 | "High-performance, safety-ready microkernel RTOS platform", "100% Success rate In achieving safety certification". **구체 인증 표준·에디션 확인 실패** | https://qnx.software/ |
| Zephyr — Introduction | ✅ 2026-09-29 | "small-footprint kernel designed for use on resource-constrained and embedded systems", Apache 2.0, 14개 아키텍처 계열, 기능 목록 | https://docs.zephyrproject.org/latest/introduction/index.html |
| Zephyr — native_sim 보드 | ✅ 2026-09-29 | `west build -b native_sim`, `./build/zephyr/zephyr.exe`, "does not intend to simulate any particular HW", 기본 ILP32 / `native_sim/native/64`는 LP64 | https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html |
| Zephyr — Getting Started Guide | ✅ 2026-09-29 | `west init -m …`, `west update`, `west packages pip --install`, `west sdk install`, `west build -p always -b <board> samples/basic/blinky`, `west flash`. 보드 이름은 문서가 플레이스홀더로 둔다 | https://docs.zephyrproject.org/latest/develop/getting_started/index.html |
| Buildroot — 매뉴얼 | ✅ 2026-09-29 | Buildroot 정의 문장, `make menuconfig`, `make list-defconfigs`, `output/` 레이아웃과 `output/images/`의 의미 | https://buildroot.org/downloads/manual/manual.html |
| Buildroot — `board/qemu/aarch64-virt/readme.txt` | ✅ 2026-09-29 | `qemu_aarch64_virt_defconfig` + 12.1절의 `qemu-system-aarch64` 명령 전문 (GitHub 미러의 master 기준) | https://github.com/buildroot/buildroot/blob/master/board/qemu/aarch64-virt/readme.txt |
| Yocto — Introduction (Overview Manual) | ✅ 2026-09-29 | "not a Linux distribution", BitBake = "a generic task execution engine", OE-Core·Poky 관계, 레이어 모델, BSP 레이어 | https://docs.yoctoproject.org/overview-manual/yp-intro.html |
| Android — Platform architecture | ✅ 2026-09-29 | 층 정의(커널·HAL·네이티브·ART·시스템 서비스·프레임워크·앱), HAL = "an abstraction layer with a standard interface for hardware vendors to implement", "allow Android to be agnostic about lower-level driver implementations", CDD | https://source.android.com/docs/core/architecture |
| FreeRTOS Kernel 저장소 | ✅ 2026-09-29 | MIT 라이선스, 커널 본체가 `list.c`·`queue.c`·`tasks.c` 세 파일 | https://github.com/FreeRTOS/FreeRTOS-Kernel |
| Eclipse ThreadX | ✅ 2026-09-29 | "Microsoft has contributed the Azure RTOS technology to the Eclipse Foundation", permissive open source, safety certified 주장. **구체 표준·컴포넌트 목록 확인 실패** | https://threadx.io/ |
| Raspberry Pi — Linux kernel 빌드 문서 | ✅ 2026-09-29 | `git clone --depth=1 https://github.com/raspberrypi/linux`, `bcm2712_defconfig`, `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image modules dtbs`, 오버레이가 `/boot/firmware/overlays/`로 간다 | https://www.raspberrypi.com/documentation/computers/linux_kernel.html |
| Hark JD 원문 5개 (Embedded Software 팀) | ✅ 2026-09-29 | 1절 표의 OS 관련 문장 전부. **이 노트의 기준 원문** | `JD_SNAPSHOT_2026-09-29.md` |

> **버전 의존 표기**: VxWorks의 기능·언어 지원·인증 항목은 확인한 데이터시트가 **Rev. 12/2019**이므로 현행 릴리스와 다를 수 있다. QNX의 우선순위 범위·스케줄링 정책 목록과 인증 표준은 버전·에디션 문서를 봐야 한다. Zephyr의 보드 이름 표기와 `west` 하위 명령은 릴리스마다 바뀐다(B02 버전 경고 참조). 리눅스 실시간 프리엠션(`PREEMPT_RT`)의 메인라인 병합 상태는 커널 버전에 따라 다르므로 **단정하지 않는다.**
> **의도적으로 적지 않은 것**: VxWorks·QNX의 **라이선스 조건과 가격**(공개 1차 자료 없음), QNX의 구체적 인증 표준·에디션 매핑(공개 페이지에서 확인 실패), 벤더 SDK(Qualcomm, Ambiq) 링크(포털·NDA 배포).
