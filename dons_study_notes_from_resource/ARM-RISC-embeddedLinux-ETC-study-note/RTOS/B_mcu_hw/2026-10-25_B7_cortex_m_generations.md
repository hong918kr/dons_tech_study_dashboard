# B7. Cortex-M 세대 지도 — M0+에서 M85까지, RTOS 포트가 코어마다 달라지는 이유

> **이 노트를 다 읽으면**: Armv6-M · v7-M · v7E-M · v8-M Baseline/Mainline · v8.1-M이 각각 무엇을 더했는지 표 한 장으로 설명할 수 있다 · M0+/M3/M4/M7/M23/M33/M35P/M52/M55/M85를 파이프라인 · MPU · FPU · DSP · TrustZone · 캐시/TCM · Helium · PACBTI · LOB로 구분하고 대표 칩을 댈 수 있다 · FreeRTOS `portable/GCC/ARM_CMx` 디렉터리 중 무엇을 골라야 하는지, 그리고 그 포트가 왜 PRIMASK 또는 BASEPRI를 쓰는지 소스로 보여 줄 수 있다 · 같은 C 코드를 7개 코어로 빌드해 QEMU에서 명령 수 · 코드 크기 차이를 직접 잴 수 있다
> **왜 필요한가**: 면접에서 "Why does the FreeRTOS Cortex-M0 port use PRIMASK instead of BASEPRI?", "What changed in Armv8-M for an RTOS?", "M7 vs M33 vs M55 for a flight controller?"가 나오고, 입사하면 "이 보드는 M33인데 우리 코드 M4 포트에서 옮겨야 한다", "RP2040은 LDREX가 없어서 락을 어떻게 하나" 같은 포팅 작업을 바로 맡는다.
> **Don 기준 난이도**: SSD 컨트롤러의 멀티코어 ARM(Cortex-R/M) 경험이 있어 "코어마다 명령·예외 모델이 조금씩 다르다"는 감각은 있다 / M 프로파일 세대별 기능 차이(v6-M에 없는 것들, v8-M의 PSPLIM · TrustZone, v8.1-M의 Helium · PACBTI · LOB)와 그것이 RTOS 포트 코드에 어떻게 드러나는지는 새로 배운다
> **선행 노트**: B1(프로그래머 모델), B3(NVIC · PRIMASK · BASEPRI), B4(SysTick · PendSV · SVC). 이어지는 노트: B6(FPU · Helium 상세), B8(캐시 · TCM), B9(Cortex-R), B10(RISC-V), C7(크리티컬 섹션 구현), F4(MPU), F6(TrustZone-M)
> 🔁 **Bare-Metal 겹침**: BM C11(Cortex-M 세대 · Armv8.1-M) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "레지스터를 직접 만지는 칩 bring-up 관점(CPACR · SHCSR · CCR 초기화 순서, 벤더 데이터시트에서 코어 옵션 읽기)"만 더한다

---

## 0. 큰 그림 — "Cortex-M"은 한 개의 CPU가 아니라 다섯 세대의 아키텍처다

Don이 SSD 컨트롤러 펌웨어에서 Cortex-R 코어 몇 개와 Cortex-M 코어 하나를 같이 다뤘다면, 아마 칩 하나 안에서 코어 종류가 고정돼 있었을 것이다. 드론 · 로봇 · 차량 회사로 가면 사정이 다르다. 비행 제어기는 STM32H7(Cortex-M7), ESC는 STM32G4(Cortex-M4F), 무선 모듈은 nRF54L(Cortex-M33), 카메라 AI 보드는 STM32N6(Cortex-M55)처럼 **한 제품 안에 세대가 다른 Cortex-M이 서너 종류** 섞인다. 그리고 같은 RTOS 코드를 그 위에 올린다.

이때 펌웨어 엔지니어가 알아야 하는 것은 두 가지다.

- **아키텍처(architecture)**: Arm이 정한 "명령어와 예외 모델의 규격"이다. Armv6-M, Armv7-M, Armv7E-M, Armv8-M(Baseline/Mainline), Armv8.1-M. RTOS 포트는 아키텍처 단위로 달라진다. 예를 들어 Armv6-M에는 `BASEPRI` 레지스터도, `LDREX`/`STREX` 명령도, 하드웨어 나눗셈도 없다.
- **코어(core, processor)**: 그 규격을 구현한 Arm의 IP 제품이다. Cortex-M3, M4, M7 … . 같은 Armv7E-M이라도 M4는 3단 파이프라인, M7은 6단 superscalar에 캐시 · TCM이 붙는다. 칩 회사(ST, NXP, Nordic …)는 코어를 사서 **옵션(MPU 유무, FPU 유무, 캐시 크기, TrustZone 유무)을 골라** 칩을 만든다.

SSD 비유로 말하면 아키텍처는 "NVMe 규격 버전", 코어는 "그 규격을 구현한 컨트롤러 IP", 칩 회사의 옵션 선택은 "컨트롤러에 붙일 채널 수 · DRAM 크기 결정"과 닮았다. 같은 NVMe 1.4라도 컨트롤러마다 큐 깊이 · 성능이 다르고, 펌웨어는 규격에 맞춰 쓰되 구현 차이에 맞춰 튜닝한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="340.0" y="20" font-size="14" text-anchor="middle" font-weight="bold">Cortex-M 코어 발표 연표 (2004 ~ 2026)</text>
<rect x="602.7" y="34" width="61.5" height="246" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="633.5" y="52" font-size="12" text-anchor="middle">신규 코어</text>
<text x="633.5" y="67" font-size="12" text-anchor="middle">없음</text>
<text x="10" y="99" font-size="12">Armv7-M / 7E-M</text>
<line x1="130" y1="95" x2="650" y2="95" stroke="#4a7bd0" stroke-opacity="0.5" stroke-width="2"/>
<circle cx="130.0" cy="95" r="6" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<text x="130.0" y="84" font-size="12" text-anchor="middle">M3 2004</text>
<circle cx="271.8" cy="95" r="6" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<text x="271.8" y="84" font-size="12" text-anchor="middle">M4 2010</text>
<circle cx="366.4" cy="95" r="6" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<text x="366.4" y="84" font-size="12" text-anchor="middle">M7 2014</text>
<text x="10" y="141" font-size="12">Armv6-M</text>
<line x1="130" y1="137" x2="650" y2="137" stroke="#d0564a" stroke-opacity="0.5" stroke-width="2"/>
<circle cx="200.9" cy="137" r="6" fill="#d0564a" fill-opacity="0.8" stroke="#d0564a"/>
<text x="200.9" y="126" font-size="12" text-anchor="middle">M1 2007</text>
<circle cx="248.2" cy="137" r="6" fill="#d0564a" fill-opacity="0.8" stroke="#d0564a"/>
<text x="248.2" y="157" font-size="12" text-anchor="middle">M0 2009</text>
<circle cx="319.1" cy="137" r="6" fill="#d0564a" fill-opacity="0.8" stroke="#d0564a"/>
<text x="319.1" y="126" font-size="12" text-anchor="middle">M0+ 2012</text>
<text x="10" y="183" font-size="12">Armv8-M Base</text>
<line x1="130" y1="179" x2="650" y2="179" stroke="#888" stroke-opacity="0.5" stroke-width="2"/>
<circle cx="413.6" cy="179" r="6" fill="#888" fill-opacity="0.8" stroke="#888"/>
<text x="413.6" y="168" font-size="12" text-anchor="middle">M23 2016</text>
<text x="10" y="225" font-size="12">Armv8-M Main</text>
<line x1="130" y1="221" x2="650" y2="221" stroke="#3f9a6b" stroke-opacity="0.5" stroke-width="2"/>
<circle cx="413.6" cy="221" r="6" fill="#3f9a6b" fill-opacity="0.8" stroke="#3f9a6b"/>
<text x="413.6" y="210" font-size="12" text-anchor="middle">M33 2016</text>
<circle cx="460.9" cy="221" r="6" fill="#3f9a6b" fill-opacity="0.8" stroke="#3f9a6b"/>
<text x="460.9" y="241" font-size="12" text-anchor="middle">M35P 2018</text>
<text x="10" y="267" font-size="12">Armv8.1-M</text>
<line x1="130" y1="263" x2="650" y2="263" stroke="#e08a3c" stroke-opacity="0.5" stroke-width="2"/>
<circle cx="508.2" cy="263" r="6" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="508.2" y="252" font-size="12" text-anchor="middle">M55 2020</text>
<circle cx="555.5" cy="263" r="6" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="555.5" y="283" font-size="12" text-anchor="middle">M85 2022</text>
<circle cx="579.1" cy="263" r="6" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="579.1" y="252" font-size="12" text-anchor="middle">M52 2023</text>
<line x1="130" y1="290" x2="650" y2="290" stroke="currentColor"/>
<line x1="130.0" y1="290" x2="130.0" y2="295" stroke="currentColor"/>
<text x="130.0" y="309" font-size="12" text-anchor="middle">2004</text>
<line x1="177.3" y1="290" x2="177.3" y2="295" stroke="currentColor"/>
<text x="177.3" y="309" font-size="12" text-anchor="middle">2006</text>
<line x1="224.5" y1="290" x2="224.5" y2="295" stroke="currentColor"/>
<text x="224.5" y="309" font-size="12" text-anchor="middle">2008</text>
<line x1="271.8" y1="290" x2="271.8" y2="295" stroke="currentColor"/>
<text x="271.8" y="309" font-size="12" text-anchor="middle">2010</text>
<line x1="319.1" y1="290" x2="319.1" y2="295" stroke="currentColor"/>
<text x="319.1" y="309" font-size="12" text-anchor="middle">2012</text>
<line x1="366.4" y1="290" x2="366.4" y2="295" stroke="currentColor"/>
<text x="366.4" y="309" font-size="12" text-anchor="middle">2014</text>
<line x1="413.6" y1="290" x2="413.6" y2="295" stroke="currentColor"/>
<text x="413.6" y="309" font-size="12" text-anchor="middle">2016</text>
<line x1="460.9" y1="290" x2="460.9" y2="295" stroke="currentColor"/>
<text x="460.9" y="309" font-size="12" text-anchor="middle">2018</text>
<line x1="508.2" y1="290" x2="508.2" y2="295" stroke="currentColor"/>
<text x="508.2" y="309" font-size="12" text-anchor="middle">2020</text>
<line x1="555.5" y1="290" x2="555.5" y2="295" stroke="currentColor"/>
<text x="555.5" y="309" font-size="12" text-anchor="middle">2022</text>
<line x1="602.7" y1="290" x2="602.7" y2="295" stroke="currentColor"/>
<text x="602.7" y="309" font-size="12" text-anchor="middle">2024</text>
<line x1="650.0" y1="290" x2="650.0" y2="295" stroke="currentColor"/>
<text x="650.0" y="309" font-size="12" text-anchor="middle">2026</text>
</svg>
```

그림 1 — Cortex-M 코어 발표 연표. 가로는 Arm이 코어를 발표한 해, 세로는 아키텍처 계열이다. 2023년 11월 Cortex-M52 이후 2026년 10월까지 Arm은 새 Cortex-M 코어를 발표하지 않았다(§2.6에서 출처로 확인).

### 0.1 이 노트의 지도

| 절 | 내용 | 실행 예제 |
|---|---|---|
| §1 | 아키텍처 세대 — 각 세대가 더한 것 | — |
| §2 | 코어별 카드, 기능 매트릭스, 대표 칩, "2026 신규 코어 없음" 확인 | 예제 1 (CPUID로 코어 확인) |
| §3 | 같은 코드, 다른 코어 — 7개 코어 빌드 · QEMU 측정 | 예제 2 (명령 수), 예제 3 (objdump), 예제 4 (코드 크기) |
| §4 | RTOS 포트에서 달라지는 것 8가지 | 예제 5 (v6-M 빌드 에러), 예제 6 (BASEPRI 무시), 예제 7 (PSPLIM), 예제 8 (Helium), 예제 9 (PACBTI) |
| §5 | FreeRTOS `portable/GCC` 디렉터리 ↔ 코어 | 예제 10 (포트 트리 · diff), 예제 11 (같은 FreeRTOS 앱을 6개 코어에서) |
| §6 | Zephyr 보드 이름과 Kconfig | 예제 12 (같은 Zephyr 앱을 5개 보드에서) |
| §7 | RTOS 커널 안에서는 | — |

### 실습 환경 한 줄

예제는 Lima VM `elinux`(aarch64 Ubuntu 26.04)에서 `arm-none-eabi-gcc 14.2.1`로 빌드하고 **QEMU 10.2.1**의 여섯 가상 머신에서 돌렸다: `microbit`(Cortex-M0, 16 MHz) · `mps2-an385`(M3, 25 MHz) · `mps2-an386`(M4, 25 MHz) · `mps2-an500`(M7, 25 MHz) · `mps2-an505`(M33, 20 MHz) · `mps3-an547`(M55, 32 MHz). M23 · M52 · M85는 QEMU 모델이 없어서 **빌드만** 했다. RTOS 예제는 FreeRTOS V11.3.2(`/var/rtoslab/src/FreeRTOS-Kernel`)와 Zephyr v4.4.2(`/var/rtoslab/zephyrproject/zephyr`)다. 명령 수 측정은 전부 `-icount shift=0`(명령 1개 = 가상 시간 1 ns)으로 돌려서 **실행할 때마다 같은 숫자**가 나온다. 단, **QEMU icount 기준이고 실제 칩과 절대값은 다르다** — QEMU는 파이프라인 · 캐시 · 플래시 wait state · 버스 · 예외 스태킹 사이클을 모델링하지 않는다. 그래서 이 노트의 숫자는 "사이클"이 아니라 "실행한 명령 수"다. M7의 dual-issue나 M55의 beat 구조처럼 사이클에만 드러나는 차이는 실행해서 보여 줄 수 없고, 출처와 함께 글로만 적었다.

---

## 1. 아키텍처 세대 — 각 세대가 무엇을 더했나

> 🔁 이 절은 Bare-Metal BM C11과 겹친다 (주인: 이 노트).

### 1.1 직관 — 계단처럼 쌓인다, 단 한 칸은 옆으로 갈라진다

M 프로파일 아키텍처는 대체로 **앞 세대의 상위 집합(superset)** 이다. Armv6-M에서 도는 바이너리는 Armv7-M에서도 돈다(Thumb 16비트 명령 대부분 + 몇 개의 32비트 명령). Armv7-M 바이너리는 Armv8-M Mainline에서 돈다. 예외가 하나 있다. **Armv8-M은 Baseline과 Mainline 두 갈래로 나뉜다.** Baseline은 "v6-M의 후계"(작은 코어용), Mainline은 "v7-M의 후계"(큰 코어용)다. 그리고 Armv8.1-M은 Mainline 위에 쌓인다.

```
Armv6-M  ──►  Armv8-M Baseline (M23)
   │
   ▼
Armv7-M (M3) ──► Armv7E-M (M4, M7) ──► Armv8-M Mainline (M33, M35P) ──► Armv8.1-M (M55, M85, M52)
```

위 그림은 "누가 누구의 기능을 물려받았나"를 나타낸다(영문만이라 ASCII로 그렸다). v8-M Baseline은 v6-M에 몇 가지(하드웨어 나눗셈, LDREX/STREX, TrustZone 옵션)를 더했지만 **BASEPRI는 여전히 없다** — 이것이 RTOS 포팅에서 가장 중요한 갈림길이다(§4.1).

### 1.2 세대별로 더해진 것

| 아키텍처 | 대표 코어 | 이 세대가 더한 것 (RTOS 관점에서 중요한 것 굵게) | 없는 것 |
|---|---|---|---|
| Armv6-M | M0, M0+, M1 | Thumb 16비트 위주 + BL · MRS · MSR · DMB 등 32비트 몇 개, NVIC(최대 32 IRQ), **PRIMASK**, SysTick(옵션), SVC · PendSV, M0+는 VTOR · MPU · 단일 사이클 I/O 포트 옵션 | **BASEPRI · FAULTMASK**, **LDREX/STREX**, **UDIV/SDIV**, CBZ/CBNZ, IT 블록, MOVW/MOVT, 설정 가능한 fault(CFSR) |
| Armv7-M | M3 | Thumb-2(16/32비트 혼합), **BASEPRI · FAULTMASK**, **LDREX/STREX**, **UDIV/SDIV**, IT 블록, 비트필드 명령, MemManage · BusFault · UsageFault + CFSR, NVIC 최대 240 IRQ, 우선순위 그룹(PRIGROUP), 비트밴딩(옵션) | DSP · SIMD, FPU |
| Armv7E-M | M4, M7 | **DSP 확장**(SMLAD · QADD 같은 SIMD · 포화 연산), 단정도(M4) · 배정도(M7 옵션) FPU, **lazy FP stacking**(B6) | TrustZone, 스택 한계 레지스터 |
| Armv8-M Baseline | M23 | v6-M + **UDIV/SDIV**, **LDREX/STREX · LDA/STL**(acquire/release), CBZ/CBNZ, MOVW/MOVT, **TrustZone(옵션)**, 새 MPU 모델(PMSAv8), Secure 쪽 스택 한계 | **BASEPRI**, DSP, FPU, IT 블록 |
| Armv8-M Mainline | M33, M35P | v7E-M 기능 + **TrustZone(옵션)**, **PSPLIM · MSPLIM**(스택 한계), PMSAv8 MPU(영역 시작/끝 주소 방식), DSP · FPU는 옵션 | Helium |
| Armv8.1-M | M55, M85, M52 | **Helium(MVE, 128비트 벡터, 옵션)**, **LOB(저오버헤드 루프: `DLS`/`LE`/`WLS`, 꼬리 예측 `DLSTP`/`LETP`)**, 반정밀 FP, **PACBTI(옵션)**, PMU, RAS | — |

표에서 굵게 쓴 것이 RTOS 포트 코드를 바꾸는 항목이다. 하나씩 정의하고 넘어간다.

- **BASEPRI**: "이 우선순위 숫자 이상인 인터럽트는 막아라"라는 레지스터다(B3). 0이면 아무것도 막지 않는다. RTOS는 크리티컬 섹션에서 PRIMASK(전부 막기) 대신 BASEPRI를 써서, 우선순위가 아주 높은 인터럽트(모터 PWM 같은)는 커널과 상관없이 계속 돌게 한다.
- **LDREX/STREX**: "읽고 → 고치고 → 아무도 안 건드렸으면 쓴다"를 하드웨어가 보장하는 명령 쌍이다(exclusive monitor). 원자 카운터 · 락 없는 큐의 기본 재료다. 없으면 인터럽트를 꺼서 원자성을 만든다.
- **PSPLIM · MSPLIM**: 스택 포인터가 이 주소 아래로 내려가면 즉시 UsageFault(STKOF)를 내는 레지스터다. RTOS 태스크 스택 넘침을 하드웨어가 잡는다(§4.4).
- **TrustZone-M**: 한 코어를 Secure / Non-secure 두 세계로 나누는 기능이다. FreeRTOS에 `_NTZ`가 붙은 포트와 안 붙은 포트가 따로 있는 이유다(§4.5, 상세는 F6).
- **Helium(MVE)**: M-Profile Vector Extension. 128비트 Q 레지스터 8개(FPU 레지스터와 공유)로 int8 16개, int16 8개를 한 번에 처리한다(§4.7, 상세는 B6).
- **LOB(Low Overhead Branch)**: 루프 끝의 "비교 + 조건 분기"를 `LE` 명령 하나로 바꾸고, 코어가 루프 시작 주소를 기억해서 분기 비용을 거의 없앤다(§3.3에서 objdump로 확인).
- **PACBTI**: Pointer Authentication(리턴 주소에 서명) + Branch Target Identification(간접 분기는 `BTI` 명령이 있는 곳에만 착지). ROP · JOP 공격을 막는다(§4.8).

### 1.3 "Armv7-M"과 "Armv7E-M"은 같은 문서다

혼동하기 쉬운 점 하나. Arm의 아키텍처 참조 매뉴얼은 "Armv7-M Architecture Reference Manual" 한 권이고, DSP 확장을 포함한 변형을 Armv7E-M이라고 부른다. Arm의 공식 비교표(2023판)는 M3 · M4 · M7 모두 "Armv7-M"으로 적고 DSP 칸에서 구분한다. GCC는 `-march=armv7-m`(M3)과 `-march=armv7e-m`(M4 · M7)을 구분한다. 이 노트에서는 GCC 쪽 이름을 따른다.

### 1.4 흔한 함정

- "M0+는 M0의 개선판이니 기능이 더 많겠지" → 맞지만 **아키텍처는 같은 Armv6-M**이다. M0+가 더한 것은 2단 파이프라인(전력 감소), VTOR · MPU · 단일 사이클 I/O 포트 **옵션**이지 BASEPRI나 LDREX가 아니다.
- "M23은 v8-M이니 M33과 같은 포트를 쓰면 되겠지" → 아니다. M23은 Baseline이라 BASEPRI가 없다. FreeRTOS도 `ARM_CM23`과 `ARM_CM33`의 `portasm.c`가 완전히 다르다(§5.2 예제 10에서 diff로 확인: 472줄 차이).
- "M7은 M4보다 상위니 M4 바이너리를 그대로 올리면 된다" → 명령은 호환되지만 캐시가 생긴다. DMA 버퍼 일관성 문제가 새로 생긴다(B8).

---

## 2. 코어별 카드 — 무엇이 다르고, 어느 칩에 들어 있나

### 2.1 한 장 요약 표

아래 숫자는 Arm 공식 "Cortex-M Processor Comparison Table"(2023판, 참고 자료 1)과 각 코어 제품 페이지에서 확인했다. 파이프라인 단수는 Arm 제품 페이지 · 벤더 자료에서 확인한 것만 적었다.

| 코어 | 발표 | 아키텍처 | 파이프라인 | MPU 최대 | FPU | DSP | TrustZone | 캐시 · TCM | DMIPS/MHz | CoreMark/MHz |
|---|---|---|---|---|---|---|---|---|---|---|
| M0 | 2009 | v6-M | 3단 | 없음 | 없음 | 없음 | 없음 | 없음 | 0.96 | 2.33 |
| M0+ | 2012 | v6-M | 2단 | 8 영역(옵션) | 없음 | 없음 | 없음 | 없음 | 0.99 | 2.46 |
| M3 | 2004 | v7-M | 3단 | 8 | 없음 | 없음 | 없음 | 없음 | 1.24 | 3.45 |
| M4 | 2010 | v7E-M | 3단 | 8 | SP(옵션) | 있음 | 없음 | 없음 | 1.26 | 3.54 |
| M7 | 2014 | v7E-M | 6단 superscalar | 16 | SP 또는 SP+DP(옵션) | 있음 | 없음 | I/D 캐시 0~64 KB, I/D TCM 0~16 MB | 2.31 | 5.29 |
| M23 | 2016 | v8-M Base | 2단 | 16 | 없음 | 없음 | 옵션 | 없음 | 1.03 | 2.64 |
| M33 | 2016 | v8-M Main | 3단 | 16 | SP(옵션) | 옵션 | 옵션 | 없음 | 1.54 | 4.10 |
| M35P | 2018 | v8-M Main | 3단 | 16 | SP(옵션) | 옵션 | 옵션 | I 캐시 2~16 KB | 1.50 | 4.10 |
| M55 | 2020 | v8.1-M | 4단 | 16 | HP/SP/DP(옵션) | 있음 | 옵션 | I/D 캐시 0~64 KB, TCM 0~16 MB | 1.69 | 4.40 |
| M85 | 2022 | v8.1-M | 7단(정수), dual-issue | 16 | HP/SP/DP(옵션) | 있음 | 있음 | I/D 캐시 0~64 KB, TCM 0~16 MB | 3.13 | 6.28 |
| M52 | 2023 | v8.1-M | 4단 | 16 | HP/SP/DP(옵션) | 있음 | 옵션 | I/D 캐시 0~64 KB, TCM 0~16 MB | 1.60 | 4.30 |

같은 표에서 RTOS · 보안 관점 열만 따로 보면:

| 코어 | 하드웨어 나눗셈 | Helium | PACBTI | LOB | 최대 외부 IRQ | 특이점 |
|---|---|---|---|---|---|---|
| M0 / M0+ | 없음 | 없음 | 없음 | 없음 | 32 | 곱셈기 1사이클 또는 32사이클 옵션(M0+) |
| M3 / M4 | 있음 | 없음 | 없음 | 없음 | 240 | M4 = M3 + DSP + FPU 옵션 |
| M7 | 있음 | 없음 | 없음 | 없음 | 240 | 캐시 · TCM · 분기 예측, 6단 |
| M23 | 있음 | 없음 | 없음 | 없음 | 240 | v6-M 계열이지만 TrustZone 가능 |
| M33 / M35P | 있음 | 없음 | 없음 | 없음 | 480 | M35P = 물리 공격(탬퍼) 대응판 |
| M55 | 있음 | 옵션(dual-beat) | 없음 | 있음 | 480 | 첫 Helium 코어 |
| M85 | 있음 | 옵션(dual-beat) | 옵션 | 있음 | 480 | 가장 빠른 Cortex-M, 첫 PACBTI |
| M52 | 있음 | 옵션(single-beat) | 옵션 | 있음 | 480 | "가장 작은 Helium 코어" |

"beat"는 Helium이 128비트 벡터 명령 하나를 몇 조각으로 나눠 실행하느냐다. dual-beat는 한 사이클에 64비트(=벡터의 절반), single-beat는 32비트씩 처리한다. 같은 명령이 M55 · M85에서는 2사이클, M52에서는 4사이클 걸리는 대신 면적이 작다. 이것은 사이클 차이라 QEMU로는 보여 줄 수 없다(QEMU는 명령 단위로만 실행한다).

```svg
<svg viewBox="0 0 680 349" xmlns="http://www.w3.org/2000/svg">
<text x="340.0" y="20" font-size="14" text-anchor="middle" font-weight="bold">코어별 기능 매트릭스 (Arm 비교표 2023판 + 아키텍처 규칙)</text>
<text x="95.0" y="46" font-size="11" text-anchor="middle">DIV</text>
<text x="145.0" y="46" font-size="11" text-anchor="middle">LDREX</text>
<text x="195.0" y="46" font-size="11" text-anchor="middle">BASEPRI</text>
<text x="245.0" y="46" font-size="11" text-anchor="middle">MPU</text>
<text x="295.0" y="46" font-size="11" text-anchor="middle">FPU</text>
<text x="345.0" y="46" font-size="11" text-anchor="middle">DSP</text>
<text x="395.0" y="46" font-size="11" text-anchor="middle">TZ</text>
<text x="445.0" y="46" font-size="11" text-anchor="middle">SPLIM</text>
<text x="495.0" y="46" font-size="11" text-anchor="middle">$/TCM</text>
<text x="545.0" y="46" font-size="11" text-anchor="middle">LOB</text>
<text x="595.0" y="46" font-size="11" text-anchor="middle">MVE</text>
<text x="645.0" y="46" font-size="11" text-anchor="middle">PACBTI</text>
<rect x="10" y="58" width="660" height="27" fill="#888" fill-opacity="0.07"/>
<text x="18" y="76" font-size="13">M0+</text>
<line x1="90.0" y1="71.5" x2="100.0" y2="71.5" stroke="#888" stroke-width="2"/>
<line x1="140.0" y1="71.5" x2="150.0" y2="71.5" stroke="#888" stroke-width="2"/>
<line x1="190.0" y1="71.5" x2="200.0" y2="71.5" stroke="#888" stroke-width="2"/>
<circle cx="245.0" cy="71.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<line x1="290.0" y1="71.5" x2="300.0" y2="71.5" stroke="#888" stroke-width="2"/>
<line x1="340.0" y1="71.5" x2="350.0" y2="71.5" stroke="#888" stroke-width="2"/>
<line x1="390.0" y1="71.5" x2="400.0" y2="71.5" stroke="#888" stroke-width="2"/>
<line x1="440.0" y1="71.5" x2="450.0" y2="71.5" stroke="#888" stroke-width="2"/>
<line x1="490.0" y1="71.5" x2="500.0" y2="71.5" stroke="#888" stroke-width="2"/>
<line x1="540.0" y1="71.5" x2="550.0" y2="71.5" stroke="#888" stroke-width="2"/>
<line x1="590.0" y1="71.5" x2="600.0" y2="71.5" stroke="#888" stroke-width="2"/>
<line x1="640.0" y1="71.5" x2="650.0" y2="71.5" stroke="#888" stroke-width="2"/>
<text x="18" y="103" font-size="13">M3</text>
<circle cx="95.0" cy="98.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="145.0" cy="98.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="195.0" cy="98.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="245.0" cy="98.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<line x1="290.0" y1="98.5" x2="300.0" y2="98.5" stroke="#888" stroke-width="2"/>
<line x1="340.0" y1="98.5" x2="350.0" y2="98.5" stroke="#888" stroke-width="2"/>
<line x1="390.0" y1="98.5" x2="400.0" y2="98.5" stroke="#888" stroke-width="2"/>
<line x1="440.0" y1="98.5" x2="450.0" y2="98.5" stroke="#888" stroke-width="2"/>
<line x1="490.0" y1="98.5" x2="500.0" y2="98.5" stroke="#888" stroke-width="2"/>
<line x1="540.0" y1="98.5" x2="550.0" y2="98.5" stroke="#888" stroke-width="2"/>
<line x1="590.0" y1="98.5" x2="600.0" y2="98.5" stroke="#888" stroke-width="2"/>
<line x1="640.0" y1="98.5" x2="650.0" y2="98.5" stroke="#888" stroke-width="2"/>
<rect x="10" y="112" width="660" height="27" fill="#888" fill-opacity="0.07"/>
<text x="18" y="130" font-size="13">M4</text>
<circle cx="95.0" cy="125.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="145.0" cy="125.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="195.0" cy="125.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="245.0" cy="125.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="295.0" cy="125.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="345.0" cy="125.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<line x1="390.0" y1="125.5" x2="400.0" y2="125.5" stroke="#888" stroke-width="2"/>
<line x1="440.0" y1="125.5" x2="450.0" y2="125.5" stroke="#888" stroke-width="2"/>
<line x1="490.0" y1="125.5" x2="500.0" y2="125.5" stroke="#888" stroke-width="2"/>
<line x1="540.0" y1="125.5" x2="550.0" y2="125.5" stroke="#888" stroke-width="2"/>
<line x1="590.0" y1="125.5" x2="600.0" y2="125.5" stroke="#888" stroke-width="2"/>
<line x1="640.0" y1="125.5" x2="650.0" y2="125.5" stroke="#888" stroke-width="2"/>
<text x="18" y="157" font-size="13">M7</text>
<circle cx="95.0" cy="152.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="145.0" cy="152.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="195.0" cy="152.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="245.0" cy="152.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="295.0" cy="152.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="345.0" cy="152.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<line x1="390.0" y1="152.5" x2="400.0" y2="152.5" stroke="#888" stroke-width="2"/>
<line x1="440.0" y1="152.5" x2="450.0" y2="152.5" stroke="#888" stroke-width="2"/>
<circle cx="495.0" cy="152.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<line x1="540.0" y1="152.5" x2="550.0" y2="152.5" stroke="#888" stroke-width="2"/>
<line x1="590.0" y1="152.5" x2="600.0" y2="152.5" stroke="#888" stroke-width="2"/>
<line x1="640.0" y1="152.5" x2="650.0" y2="152.5" stroke="#888" stroke-width="2"/>
<rect x="10" y="166" width="660" height="27" fill="#888" fill-opacity="0.07"/>
<text x="18" y="184" font-size="13">M23</text>
<circle cx="95.0" cy="179.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="145.0" cy="179.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<line x1="190.0" y1="179.5" x2="200.0" y2="179.5" stroke="#888" stroke-width="2"/>
<circle cx="245.0" cy="179.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<line x1="290.0" y1="179.5" x2="300.0" y2="179.5" stroke="#888" stroke-width="2"/>
<line x1="340.0" y1="179.5" x2="350.0" y2="179.5" stroke="#888" stroke-width="2"/>
<circle cx="395.0" cy="179.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="445.0" cy="179.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<line x1="490.0" y1="179.5" x2="500.0" y2="179.5" stroke="#888" stroke-width="2"/>
<line x1="540.0" y1="179.5" x2="550.0" y2="179.5" stroke="#888" stroke-width="2"/>
<line x1="590.0" y1="179.5" x2="600.0" y2="179.5" stroke="#888" stroke-width="2"/>
<line x1="640.0" y1="179.5" x2="650.0" y2="179.5" stroke="#888" stroke-width="2"/>
<text x="18" y="211" font-size="13">M33</text>
<circle cx="95.0" cy="206.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="145.0" cy="206.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="195.0" cy="206.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="245.0" cy="206.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="295.0" cy="206.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="345.0" cy="206.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="395.0" cy="206.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="445.0" cy="206.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<line x1="490.0" y1="206.5" x2="500.0" y2="206.5" stroke="#888" stroke-width="2"/>
<line x1="540.0" y1="206.5" x2="550.0" y2="206.5" stroke="#888" stroke-width="2"/>
<line x1="590.0" y1="206.5" x2="600.0" y2="206.5" stroke="#888" stroke-width="2"/>
<line x1="640.0" y1="206.5" x2="650.0" y2="206.5" stroke="#888" stroke-width="2"/>
<rect x="10" y="220" width="660" height="27" fill="#888" fill-opacity="0.07"/>
<text x="18" y="238" font-size="13">M55</text>
<circle cx="95.0" cy="233.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="145.0" cy="233.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="195.0" cy="233.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="245.0" cy="233.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="295.0" cy="233.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="345.0" cy="233.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="395.0" cy="233.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="445.0" cy="233.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="495.0" cy="233.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="545.0" cy="233.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="595.0" cy="233.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<line x1="640.0" y1="233.5" x2="650.0" y2="233.5" stroke="#888" stroke-width="2"/>
<text x="18" y="265" font-size="13">M52</text>
<circle cx="95.0" cy="260.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="145.0" cy="260.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="195.0" cy="260.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="245.0" cy="260.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="295.0" cy="260.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="345.0" cy="260.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="395.0" cy="260.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="445.0" cy="260.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="495.0" cy="260.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="545.0" cy="260.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="595.0" cy="260.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="645.0" cy="260.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<rect x="10" y="274" width="660" height="27" fill="#888" fill-opacity="0.07"/>
<text x="18" y="292" font-size="13">M85</text>
<circle cx="95.0" cy="287.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="145.0" cy="287.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="195.0" cy="287.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="245.0" cy="287.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="295.0" cy="287.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="345.0" cy="287.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="395.0" cy="287.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="445.0" cy="287.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="495.0" cy="287.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="545.0" cy="287.5" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/>
<circle cx="595.0" cy="287.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="645.0" cy="287.5" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<circle cx="30" cy="323" r="7" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/><text x="44" y="327" font-size="12">항상 있음</text>
<circle cx="130" cy="323" r="7" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/><text x="144" y="327" font-size="12">칩 설계자가 고르는 옵션</text>
<line x1="300" y1="323" x2="310" y2="323" stroke="#888" stroke-width="2"/><text x="318" y="327" font-size="12">없음   (M23 SPLIM = Secure 쪽만)</text>
</svg>
```

그림 2 — 코어별 기능 매트릭스. 초록 원은 "그 코어면 항상 있음", 주황 원은 "칩 설계자가 넣을지 고르는 옵션"이다. 같은 M33이라도 칩마다 FPU · TrustZone 유무가 다를 수 있다는 뜻이다. DIV = 하드웨어 나눗셈, SPLIM = 스택 한계 레지스터, $/TCM = 캐시 · TCM, MVE = Helium. M85의 TrustZone은 Arm 비교표에 "Yes"(옵션 아님)로 적혀 있다.

### 2.2 Armv6-M — M0 · M0+ (그리고 FPGA용 M1)

- **어디 쓰나**: 가장 싼 MCU, 배터리 센서 노드, 큰 SoC 안의 전원 관리 · 부트 코어. 예: STM32F0 · G0 · C0(M0/M0+), RP2040(M0+ 두 개), nRF51(M0, QEMU `microbit`이 이 칩을 흉내 낸다).
- **RTOS 관점 요약**: PRIMASK만 있다 → 크리티컬 섹션 = 인터럽트 전부 끄기. LDREX/STREX가 없다 → 원자 연산도 인터럽트 끄기(단일 코어)나 칩 고유 하드웨어 스핀락(RP2040 SIO)으로. 나눗셈은 라이브러리 함수 호출(`__aeabi_uidiv`) → 수백 명령. `PUSH`/`STM`은 R0~R7만 다룬다 → PendSV가 R8~R11을 옮겨 담는 명령이 더 많다(§4.3).
- **M0+가 더한 것**: 2단 파이프라인(분기 비용 감소, 전력 감소), VTOR(벡터 테이블 위치 변경 — 부트로더에 필요), MPU 옵션(8 영역), 단일 사이클 I/O 포트, Micro Trace Buffer.

### 2.3 Armv7-M / 7E-M — M3 · M4 · M7

- **M3(2004)**: Thumb-2를 처음 가져온 "원조 Cortex-M"이다. QEMU `mps2-an385`가 M3이고, 이 RTOS 랩의 기본 머신이다. 지금 새 칩에 M3를 넣는 경우는 드물다(같은 면적이면 M4나 M33).
- **M4(2010)**: M3 + DSP 명령 + 단정도 FPU 옵션. "M4F"는 FPU가 들어간 M4를 부르는 업계 관용어다(Arm 공식 코어 이름은 아니다). 모터 제어(FOC) · 오디오 · 센서 융합의 표준. 예: STM32F4(최대 180 MHz), STM32G4, nRF52840(64 MHz), TI · NXP 다수.
- **M7(2014)**: 6단 superscalar(한 사이클에 명령 2개까지), 분기 예측, I/D 캐시, ITCM · DTCM, 배정도 FPU 옵션, 64비트 AXI 버스. 드론 비행 제어기 · 고속 모터 · 오디오의 고성능 칸. 예: STM32F7, STM32H7(H723 계열 550 MHz, H7R/S 계열 600 MHz), NXP i.MX RT1060(600 MHz), i.MX RT1170(M7 1 GHz + M4 400 MHz). 캐시 · TCM의 RTOS 쪽 함정은 B8에서 다룬다.

### 2.4 Armv8-M — M23 · M33 · M35P

- **M23(2016)**: v6-M 크기의 코어에 TrustZone을 넣을 수 있게 한 것이다. 하드웨어 나눗셈 · LDREX/STREX도 생겼다. 하지만 BASEPRI는 없다. 예: Microchip SAML10/L11, Nuvoton M2351 계열(확인한 범위의 예; 출하 현황은 칩사 자료로 다시 확인).
- **M33(2016)**: 2020년대 범용 MCU의 "새 기본값". M4 수준 성능 + TrustZone + 스택 한계 레지스터 + 새 MPU. 예: STM32U5(160 MHz), STM32H5, nRF5340, nRF54L 계열(128 MHz), nRF54H20(애플리케이션 코어 320 MHz — Nordic 1차 자료로는 확인 못 함, CNX Software 기준), RP2350(M33 두 개 150 MHz, RISC-V Hazard3 두 개와 부팅 시 선택), NXP LPC55S, Renesas RA6 · RA4 일부.
- **M35P(2018)**: M33에 물리 공격(전력 분석 · 글리치) 대응을 더한 보안 칩용 코어. 스마트카드 · 보안 요소 쪽이라 RTOS 면접에서는 이름만 알면 된다. 그래도 FreeRTOS에 `ARM_CM35P` 포트가 따로 있다(내용은 M33과 같고 `portmacro.h`의 이름만 다르다 — 예제 10).

### 2.5 Armv8.1-M — M55 · M85 · M52

- **M55(2020)**: 첫 Helium 코어. 4단 파이프라인, 캐시 · TCM 옵션, PMU. "MCU에서 신경망 추론" 흐름의 시작이고 Ethos-U55/U65 NPU와 짝을 이룬다. 예: STM32N6(800 MHz + ST Neural-ART NPU, 2024년 12월 출시), Alif Ensemble E3/E7(M55 HP 400 MHz + M55 HE 160 MHz + Ethos-U55), Infineon PSoC Edge E8x(M55 + M33 + Ethos-U55).
- **M85(2022)**: 가장 빠른 Cortex-M. 7단 정수 파이프라인, dual-issue, Helium, PACBTI 옵션, TrustZone. 예: Renesas RA8M1(2023년 10월, 업계 최초 M85 MCU, 480 MHz, 6.39 CoreMark/MHz), 이후 RA8D1 · RA8P1 · RA8M2 · RA8D2.
- **M52(2023년 11월)**: "Helium을 가장 작은 면적으로". single-beat Helium, 4단 파이프라인, PACBTI · TrustZone 옵션. Arm 발표 기준 이전 세대 대비 ML 최대 5.6배, DSP 최대 2.7배. **출하 중인 M52 칩은 이 노트 작성 시점에 확인하지 못했다**(확인 못 함). FreeRTOS(`ARM_CM52`)와 Zephyr(`CONFIG_CPU_CORTEX_M52`)는 이미 지원한다.

### 2.6 "2026년 신규 Cortex-M 코어 없음"은 사실인가

Bare-Metal 나침반 BM C11에 "2026 신규 코어 없음"이라고 적혀 있다. 2026-10-25에 다시 확인했다.

- Arm의 마지막 Cortex-M 코어 발표는 **Cortex-M52(2023-11-22)** 다(Arm newsroom · developer 블로그).
- eeNews Europe 2026-07-27 기사 "What now for the Arm Cortex-M microcontroller?"에서 Arm의 엣지 컴퓨팅 담당 Lionel Belnet은 "다음 코어의 개념을 작업 중이지만 민감한 내용"이라고 했고, "Cortex-M의 성능을 더 밀어 올리는 것은 우선순위가 아니다", 다음 세대는 "전통적인 마이크로컨트롤러가 아닐 수도 있다"고 말했다. 고성능 엣지 AI는 Cortex-A320 + Ethos-U85 쪽으로 옮겨 가는 중이다.
- 그 사이 생태계 쪽 움직임은 "새 코어"가 아니라 **기존 코어를 쓴 새 칩**이다: Renesas RA8 계열 확장(M85, 일부는 M33 동반), STM32N6(M55), Alif의 Ethos-U85 채택 칩 예고.
- 참고로 FreeRTOS 소스에는 `ARM_STAR_MC3` 포트가 있는데, 이것은 Arm China가 만든 Armv8.1-M 호환 코어 STAR-MC3용이다(`portARCH_NAME "STAR-MC3"`, 예제 10). Arm의 Cortex-M 제품은 아니다.

결론: **2024년 1월~2026년 10월 사이 Arm의 새 Cortex-M 코어 발표는 찾지 못했다.** 이 판단은 위 출처 기준이고, Arm이 "다음 코어 작업 중"이라고 밝힌 상태이므로 면접에서는 "as of late 2026, the newest Cortex-M cores are still M85 (2022) and M52 (2023)"라고 시점을 붙여 말한다.

### 2.7 예제 1 — 같은 소스로 여섯 머신의 CPUID 읽기

무엇을 확인하는 코드인가: SCB의 CPUID 레지스터(0xE000ED00)로 "지금 어느 코어 위에 있는지"를 읽고, SysTick으로 "명령 1,000,000개가 몇 틱인지" 재서 각 머신의 CPU 클록을 역산한다. 이 보정값이 §3의 명령 수 측정 기준이 된다.

먼저 이 노트 전체가 쓰는 **공용 하네스**다. 랩 템플릿(`templates/common`)은 AN385(M3) 전용이라, 코어마다 메모리 맵과 `-mcpu`만 바꾸면 되도록 작게 다시 만들었다. 출력은 UART 대신 **semihosting**(`BKPT 0xAB`)으로 해서 머신마다 다른 UART 주소를 신경 쓰지 않는다.

```sh
#!/bin/bash
# mc.sh <core> <out_dir> <src...> — 코어 하나용으로 빌드, RUN=1이면 그 코어의 QEMU 머신에서 실행
set -e
core=$1; out=$2; shift 2
H=/Users/donh/.rtos_scratch/B7/mc
case $core in
 m0p) cpu=cortex-m0plus; mach=microbit; qcpu="-cpu cortex-m0"; mem="0x0 256K 0x20000000 16K";;
 m3)  cpu=cortex-m3;  mach=mps2-an385; mem="0x0 4M 0x20000000 4M";;
 m4)  cpu=cortex-m4;  mach=mps2-an386; mem="0x0 4M 0x20000000 4M";;
 m7)  cpu=cortex-m7;  mach=mps2-an500; mem="0x0 4M 0x20000000 4M";;
 m23) cpu=cortex-m23; mach=;           mem="0x0 4M 0x20000000 4M";;
 m33) cpu=cortex-m33; mach=mps2-an505; mem="0x10000000 4M 0x38000000 4M";;
 m55) cpu=cortex-m55; mach=mps3-an547; mem="0x10000000 512K 0x30000000 512K";;
 m85) cpu=cortex-m85; mach=;           mem="0x10000000 512K 0x30000000 512K";;
 m52) cpu=cortex-m52; mach=;           mem="0x10000000 512K 0x30000000 512K";;
esac
set -- "$@"; read fo fl ro rl <<<"$mem"
mkdir -p $out
sed -e "s/@FO@/$fo/;s/@FL@/$fl/;s/@RO@/$ro/;s/@RL@/$rl/" $H/mem.ld.in > $out/mem.ld
arm-none-eabi-gcc -mthumb -mcpu=$cpu ${FLOAT:--mfloat-abi=soft} -O2 -g -std=gnu11 -Wall -Wextra -ffreestanding \
  -ffunction-sections -fdata-sections -I$H $CFL "$@" $H/start.c $H/semi.c \
  -nostdlib -T $out/mem.ld -Wl,--gc-sections -Wl,-Map=$out/app.map -lgcc -o $out/app.elf
if [ -n "$RUN" ]; then
  [ -z "$mach" ] && { echo "($core: no QEMU machine — build only)"; exit 0; }
  timeout --foreground ${TO:-20} qemu-system-arm -machine $mach $qcpu -nographic -monitor none -serial null \
    -semihosting-config enable=on,target=native -icount shift=0,align=off,sleep=off -kernel $out/app.elf
fi
```

메모리 맵이 머신마다 다른 점이 눈에 띈다. MPS2 AN385/386/500은 코드 0x0 · RAM 0x20000000이지만, **TrustZone이 있는 AN505(M33)와 AN547(M55)은 Secure로 부팅**하므로 Secure 별칭 주소(코드 0x10000000, RAM 0x38000000 · 0x30000000)에 올려야 한다. Zephyr의 `mps2_an521_cpu0.dts`(`ssram1: memory@10000000`, `ssram2_3: memory@38000000`)와 `mps3_corstone300_an547.dts`(ITCM 0x10000000, DTCM 0x30000000)에서 같은 주소를 확인했다. 링커 스크립트 틀(`mem.ld.in`)은 `MEMORY`의 주소만 `sed`로 바꾸고 나머지는 랩 템플릿과 같은 `.isr_vector → .text → .data(AT FLASH) → .bss` 구성이다(B5).

시작 코드는 벡터 16개와 Reset만 있다. 하나만 눈여겨보면 된다: **FPU · Helium이 있는 빌드에서는 `.data` 복사 루프보다 먼저 CPACR을 켠다.** 이유는 §4.7 예제 8에서 실제 fault로 보인다.

```c
/* start.c — 모든 Cortex-M QEMU 머신용 최소 벡터 테이블 + 리셋 */
#include "semi.h"
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);
void SysTick_Handler(void);
void Reset_Handler(void);
void Fault_Handler(void);
void SVC_Handler(void) __attribute__((weak, alias("Fault_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Fault_Handler")));
void Fault_Handler(void)
{
    uint32_t ipsr; __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
#if __ARM_ARCH_6M__ || __ARM_ARCH_8M_BASE__
    semi_printf("[fault] exception %u\n", ipsr);   /* no CFSR on v6-M */
#else
    semi_printf("[fault] exception %u CFSR=0x%x HFSR=0x%x\n", ipsr,
                REG32(0xE000ED28u), REG32(0xE000ED2Cu));
#endif
    semi_exit(255);
}
__attribute__((section(".isr_vector"), used))
void (*const g_vectors[16])(void) = {
    (void (*)(void))&_estack, Reset_Handler, Fault_Handler, Fault_Handler,
    Fault_Handler, Fault_Handler, Fault_Handler, Fault_Handler, 0, 0, 0,
    SVC_Handler, Fault_Handler, 0, PendSV_Handler, SysTick_Handler,
};
void Reset_Handler(void)
{
#if (defined(__ARM_FP) || defined(__ARM_FEATURE_MVE)) && !defined(NO_CPACR)
    REG32(0xE000ED88u) |= 0xFu << 20;          /* CPACR: CP10/CP11 full access — 복사 루프보다 먼저! */
    __asm volatile("dsb\n isb" : : : "memory");
#endif
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss; ) *d++ = 0;
    semi_exit(main());
}
void *memcpy(void *d, const void *s, unsigned n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, unsigned n) { char *a = d; while (n--) *a++ = (char)c; return d; }
```

`semi.c`(57줄)는 semihosting 출력(`SYS_WRITE0` = 0x04), 종료(`SYS_EXIT` = 0x18), 그리고 SysTick 자유 실행 시계 두 함수다. 핵심만 발췌한다.

```c
/* semi.c 발췌 — semihosting 호출과 SysTick 시계 */
static int semi_call(int op, void *arg)
{
    register int r0 __asm__("r0") = op;
    register void *r1 __asm__("r1") = arg;
    __asm volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
#define SYST_CSR REG32(0xE000E010u)
#define SYST_RVR REG32(0xE000E014u)
#define SYST_CVR REG32(0xE000E018u)
static volatile uint32_t wraps;
__attribute__((weak)) void SysTick_Handler(void) { wraps++; }   /* FreeRTOS가 쓰면 그쪽이 이긴다 */
void tick_start(void)
{
    SYST_RVR = 0xFFFFFFu; SYST_CVR = 0; SYST_CSR = 7;          /* CPU 클록, 인터럽트, enable */
    while (SYST_CVR == 0) { }                /* 첫 reload까지 기다린다 */
    wraps = 0;
}
uint32_t tick_now(void)
{
    uint32_t w, v;
    do { w = wraps; v = SYST_CVR; } while (w != wraps);
    return w * 0x1000000u + (0xFFFFFFu - v);
}
```

SysTick은 v6-M부터 v8.1-M까지 **모든 Cortex-M에 같은 주소로** 있다(v6-M에서는 옵션이지만 QEMU는 넣어 준다). 그래서 "코어 무관 시계"로 쓰기 좋다. 처음 만든 판에서는 `while (SYST_CVR == 0)` 줄이 없어서, 쓰기 직후 CVR=0을 읽은 시작값이 0xFFFFFF로 계산돼 첫 측정이 음수(4278215080 틱)로 나왔다. SysTick의 CVR은 0을 쓰면 다음 클록에 reload된다는 점을 잊으면 생기는 버그다.

이제 예제 1 본체다.

```c
/* ex1: 같은 소스로 각 코어의 CPUID를 읽고, SysTick 1틱이 명령 몇 개인지 보정한다 */
#include "semi.h"
#define CPUID REG32(0xE000ED00u)
static const char *part_name(uint32_t p)
{
    switch (p) {
    case 0xC20: return "Cortex-M0";  case 0xC60: return "Cortex-M0+";
    case 0xC23: return "Cortex-M3";  case 0xC24: return "Cortex-M4";
    case 0xC27: return "Cortex-M7";  case 0xD20: return "Cortex-M23";
    case 0xD21: return "Cortex-M33"; case 0xD22: return "Cortex-M55";
    case 0xD23: return "Cortex-M85"; case 0xD24: return "Cortex-M52";
    default:    return "?";
    }
}
static void spin(uint32_t n)                 /* 정확히 2n+α 명령: subs + bne */
{
    __asm volatile(".syntax unified\n1: subs %0, %0, #1\n bne 1b\n"
#if __ARM_ARCH_ISA_THUMB == 1
                   ".syntax divided\n"          /* v6-M/v8-M.base: GCC는 divided 문법으로 출력 */
#endif
                   : "+l"(n) : : "cc");
}
int main(void)
{
    uint32_t id = CPUID, part = (id >> 4) & 0xFFF;
    semi_printf("CPUID=0x%x  part=0x%x (%s)  r%up%u\n", id, part, part_name(part),
                (id >> 20) & 0xF, id & 0xF);
    tick_start();
    uint32_t t0 = tick_now();
    spin(500000);                            /* 1,000,000 명령 */
    uint32_t dt = tick_now() - t0;
    semi_printf("1,000,000 insn = %u SysTick ticks -> CPU clock seen by SysTick = %u MHz (icount 1 insn = 1 ns)\n",
                dt, (dt + 500) / 1000);
    return 0;
}
```

```sh
# run_all.sh: 코어 목록을 돌며 mc.sh를 RUN=1로 부른다
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/B7/run_all.sh ex1 /Users/donh/.rtos_scratch/B7/ex1_cpuid.c'
```

```text
--- m0p
CPUID=0x410cc200  part=0xc20 (Cortex-M0)  r0p0
1,000,000 insn = 16000 SysTick ticks -> CPU clock seen by SysTick = 16 MHz (icount 1 insn = 1 ns)
--- m3
CPUID=0x410fc231  part=0xc23 (Cortex-M3)  r0p1
1,000,000 insn = 25000 SysTick ticks -> CPU clock seen by SysTick = 25 MHz (icount 1 insn = 1 ns)
--- m4
CPUID=0x410fc240  part=0xc24 (Cortex-M4)  r0p0
1,000,000 insn = 25000 SysTick ticks -> CPU clock seen by SysTick = 25 MHz (icount 1 insn = 1 ns)
--- m7
CPUID=0x411fc272  part=0xc27 (Cortex-M7)  r1p2
1,000,000 insn = 25000 SysTick ticks -> CPU clock seen by SysTick = 25 MHz (icount 1 insn = 1 ns)
--- m23
(m23: no QEMU machine — build only)
--- m33
CPUID=0x410fd213  part=0xd21 (Cortex-M33)  r0p3
1,000,000 insn = 20000 SysTick ticks -> CPU clock seen by SysTick = 20 MHz (icount 1 insn = 1 ns)
--- m55
CPUID=0x410fd221  part=0xd22 (Cortex-M55)  r0p1
1,000,000 insn = 32000 SysTick ticks -> CPU clock seen by SysTick = 32 MHz (icount 1 insn = 1 ns)
```

출력에서 볼 것: (1) `microbit`은 M0+가 아니라 **M0**(part 0xC20)이다 — nRF51이 M0 칩이기 때문이다. M0+용(`-mcpu=cortex-m0plus`)으로 빌드한 코드가 M0에서 그대로 도는 것은 둘 다 Armv6-M이라서다. (2) QEMU의 M7은 **r1p2**로 보고한다. FreeRTOS의 M7 errata 포트 선택(§4.6)에서 이 r/p 숫자가 쓰인다. (3) 머신마다 SysTick이 보는 CPU 클록이 16 · 25 · 20 · 32 MHz로 다르다. icount에서는 명령 1개 = 1 ns이므로 "명령 수 = 틱 × 1000 / MHz"로 바꿀 수 있다. 이 보정 덕분에 클록이 다른 머신끼리 명령 수를 공정하게 비교한다.

Don 경험과 연결: SSD 컨트롤러 bring-up에서 "이 코어가 정말 내가 생각한 리비전인가"를 JTAG로 MIDR/CPUID부터 읽던 것과 같다. RTOS 포트 중에는 CPUID로 자기 자신을 검사하는 것도 있다(FreeRTOS `ARM_CM4F/port.c`는 M7 r0p0 · r0p1이면 `configASSERT`로 멈춘다 — §4.6).

---

## 3. 같은 코드, 다른 코어 — 7개 코어로 빌드하고 재 보기

### 3.1 직관 — 컴파일러가 "그 코어에 있는 명령"으로 번역한다

C 한 줄 `s += x / d;`는 코어에 따라 전혀 다른 기계어가 된다. 하드웨어 나눗셈이 있으면 `UDIV` 한 개, 없으면 `__aeabi_uidiv`라는 libgcc 함수 호출(안에서 수십~수백 명령 루프)이다. `__atomic_fetch_add`는 LDREX/STREX 루프가 되거나, 아예 링크가 안 된다. 루프 끝의 `subs; bne`는 Armv8.1-M에서 `LE` 하나가 된다. 이 차이가 RTOS에서는 **크리티컬 섹션 길이, ISR 실행 시간, 코드 크기**로 드러난다.

그래서 작은 벤치마크 네 개를 만들어 7개 코어(`-mcpu=cortex-m0plus / m3 / m4 / m7 / m23 / m33 / m55`)로 빌드하고, M23을 뺀 6개를 QEMU에서 icount로 돌렸다.

| 벤치 | 무엇을 보나 | 코어 차이가 드러나는 곳 |
|---|---|---|
| `crc32_sw` | 1 KB 비트 단위 CRC32 | Thumb-2 명령(SBFX, 시프트 결합 EOR), LOB |
| `div_loop` | 2,000번 32비트 나눗셈 | UDIV 유무 |
| `atomic_loop` | 원자 카운터 2,000번 증가 | LDREX/STREX 유무 (없으면 PRIMASK) |
| `dot16` | int16 256개 내적 | DSP 명령(SMLABB), LOB |

### 3.2 예제 2 — 명령 수 측정

무엇을 확인하는 코드인가: 같은 C 함수 네 개를 코어마다 다시 컴파일해서 실행 명령 수를 센다. 결과값(`result=`)이 코어마다 같은지도 같이 본다 — 같아야 "같은 일을 했다"고 말할 수 있다.

```c
/* bench: 같은 C 코드(CRC32 · 나눗셈 · 원자 카운터 · int16 내적)를 코어마다 돌려 명령 수를 센다 */
#include "semi.h"
#define N 1024
static uint8_t  buf[N];
static int16_t  va[256], vb[256];
static volatile uint32_t counter;
__attribute__((noinline)) uint32_t crc32_sw(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u)); }
    return ~c;
}
__attribute__((noinline)) uint32_t div_loop(uint32_t x, uint32_t n)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) { x = x * 1103515245u + 12345u; s += x / ((i & 15u) + 3u); }
    return s;
}
static inline void atomic_inc(volatile uint32_t *p)
{
#if __ARM_ARCH_6M__                       /* v6-M: LDREX/STREX 없음 → PRIMASK 크리티컬 섹션 */
    uint32_t pm;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(pm) : : "memory");
    (*p)++;
    __asm volatile("msr primask, %0" : : "r"(pm) : "memory");
#else                                     /* v7-M 이상, v8-M Baseline: LDREX/STREX 루프 */
    __atomic_fetch_add(p, 1u, __ATOMIC_RELAXED);
#endif
}
__attribute__((noinline)) void atomic_loop(uint32_t n) { while (n--) atomic_inc(&counter); }
__attribute__((noinline)) int32_t dot16(const int16_t *a, const int16_t *b, uint32_t n)
{
    int32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}
static uint32_t cal;                      /* 1,000,000 명령에 해당하는 tick 수 */
static uint32_t insn(uint32_t ticks) { return (uint32_t)((uint64_t)ticks * 1000000u / cal); }
#define MEASURE(name, expr) do { uint32_t t0 = tick_now(); uint32_t r = (uint32_t)(expr); \
    uint32_t d = tick_now() - t0; semi_printf("%s result=0x%x insn=%u\n", name, r, insn(d)); } while (0)
static void spin(uint32_t n)
{
    __asm volatile(".syntax unified\n1: subs %0, %0, #1\n bne 1b\n"
#if __ARM_ARCH_ISA_THUMB == 1
                   ".syntax divided\n"
#endif
                   : "+l"(n) : : "cc");
}
int main(void)
{
    for (int i = 0; i < N; i++) buf[i] = (uint8_t)(i * 7 + 1);
    for (int i = 0; i < 256; i++) { va[i] = (int16_t)(i * 3 - 300); vb[i] = (int16_t)(500 - i * 5); }
    tick_start();
    uint32_t t0 = tick_now(); spin(500000); cal = tick_now() - t0;
    MEASURE("crc32 ", crc32_sw(buf, N));
    MEASURE("div   ", div_loop(1, 2000));
    MEASURE("atomic", (atomic_loop(2000), counter));
    MEASURE("dot16 ", dot16(va, vb, 256));
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/B7/run_all.sh bench /Users/donh/.rtos_scratch/B7/bench.c'
```

```text
--- m0p
crc32  result=0xe00d889 insn=71750
div    result=0xdc058011 insn=386437
atomic result=0x7d0 insn=16000
dot16  result=0xfe93b180 insn=1812
--- m3
crc32  result=0xe00d889 insn=46080
div    result=0xdc058011 insn=16040
atomic result=0x7d0 insn=14000
dot16  result=0xfe93b180 insn=1320
--- m4
crc32  result=0xe00d889 insn=46080
div    result=0xdc058011 insn=16040
atomic result=0x7d0 insn=14040
dot16  result=0xfe93b180 insn=1280
--- m7
crc32  result=0xe00d889 insn=46080
div    result=0xdc058011 insn=16040
atomic result=0x7d0 insn=14040
dot16  result=0xfe93b180 insn=1280
--- m23
(m23: no QEMU machine — build only)
--- m33
crc32  result=0xe00d889 insn=46100
div    result=0xdc058011 insn=16050
atomic result=0x7d0 insn=14000
dot16  result=0xfe93b180 insn=1300
--- m55
crc32  result=0xe00d889 insn=38937
div    result=0xdc058011 insn=14031
atomic result=0x7d0 insn=12031
dot16  result=0xfe93b180 insn=1062
```

출력에서 볼 것: 결과값은 여섯 코어 모두 같다. 명령 수는 세 무리로 갈린다 — **M0+는 나눗셈에서 24배(386,437 대 16,040)**, M3 · M4 · M7 · M33은 거의 같고, **M55는 모든 벤치에서 10~15% 적다**. 14,000과 14,040 같은 작은 차이는 측정 해상도(SysTick 1틱 = 31~62 명령) 때문이니 무시한다.

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
<text x="340.0" y="22" font-size="14" text-anchor="middle" font-weight="bold">같은 C 코드의 실행 명령 수 (M3 = 100%, QEMU icount 실측)</text>
<line x1="70" y1="310.0" x2="660" y2="310.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="314.0" font-size="12" text-anchor="end">0%</text>
<line x1="70" y1="245.0" x2="660" y2="245.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="249.0" font-size="12" text-anchor="end">50%</text>
<line x1="70" y1="180.0" x2="660" y2="180.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="184.0" font-size="12" text-anchor="end">100%</text>
<line x1="70" y1="115.0" x2="660" y2="115.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="119.0" font-size="12" text-anchor="end">150%</text>
<line x1="70" y1="50.0" x2="660" y2="50.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="54.0" font-size="12" text-anchor="end">200%</text>
<line x1="70" y1="310" x2="660" y2="310" stroke="currentColor"/>
<line x1="70" y1="50" x2="70" y2="310" stroke="currentColor"/>
<rect x="85.8" y="107.6" width="26" height="202.4" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="98.8" y="103.6" font-size="12" text-anchor="middle">156%</text>
<rect x="115.8" y="180.0" width="26" height="130.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="128.8" y="176.0" font-size="12" text-anchor="middle">100%</text>
<rect x="145.8" y="179.9" width="26" height="130.1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="158.8" y="175.9" font-size="12" text-anchor="middle">100%</text>
<rect x="175.8" y="200.2" width="26" height="109.8" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="188.8" y="196.2" font-size="12" text-anchor="middle">84%</text>
<text x="143.8" y="330" font-size="13" text-anchor="middle">CRC32</text>
<rect x="233.2" y="50.0" width="26" height="260.0" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="246.2" y="66.0" font-size="12" text-anchor="middle">↑</text>
<text x="263.2" y="64.0" font-size="12">2409% (24배)</text>
<rect x="263.2" y="180.0" width="26" height="130.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="276.2" y="176.0" font-size="12" text-anchor="middle">100%</text>
<rect x="293.2" y="179.9" width="26" height="130.1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="306.2" y="175.9" font-size="12" text-anchor="middle">100%</text>
<rect x="323.2" y="196.3" width="26" height="113.7" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="336.2" y="192.3" font-size="12" text-anchor="middle">87%</text>
<text x="291.2" y="330" font-size="13" text-anchor="middle">나눗셈</text>
<rect x="380.8" y="161.4" width="26" height="148.6" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="393.8" y="157.4" font-size="12" text-anchor="middle">114%</text>
<rect x="410.8" y="180.0" width="26" height="130.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="423.8" y="176.0" font-size="12" text-anchor="middle">100%</text>
<rect x="440.8" y="180.0" width="26" height="130.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="453.8" y="176.0" font-size="12" text-anchor="middle">100%</text>
<rect x="470.8" y="198.3" width="26" height="111.7" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="483.8" y="194.3" font-size="12" text-anchor="middle">86%</text>
<text x="438.8" y="330" font-size="13" text-anchor="middle">원자 카운터</text>
<rect x="528.2" y="131.5" width="26" height="178.5" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="541.2" y="127.5" font-size="12" text-anchor="middle">137%</text>
<rect x="558.2" y="180.0" width="26" height="130.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="571.2" y="176.0" font-size="12" text-anchor="middle">100%</text>
<rect x="588.2" y="182.0" width="26" height="128.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="601.2" y="178.0" font-size="12" text-anchor="middle">98%</text>
<rect x="618.2" y="205.4" width="26" height="104.6" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="631.2" y="201.4" font-size="12" text-anchor="middle">80%</text>
<text x="586.2" y="330" font-size="13" text-anchor="middle">int16 내적</text>
<rect x="150" y="346" width="14" height="12" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="170" y="356" font-size="12">M0+</text>
<rect x="260" y="346" width="14" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="280" y="356" font-size="12">M3</text>
<rect x="370" y="346" width="14" height="12" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="390" y="356" font-size="12">M33</text>
<rect x="480" y="346" width="14" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="500" y="356" font-size="12">M55</text>
</svg>
```

그림 3 — 같은 C 코드의 실행 명령 수(M3 = 100%). M4 · M7은 M3와 같아서(±1틱 오차) 그리지 않았다. M0+의 나눗셈은 2409%라 막대를 잘랐다. QEMU icount 실측이고 사이클이 아니다.

### 3.3 예제 3 — 왜 그런 숫자가 나왔나: objdump로 루프 읽기

무엇을 확인하는 코드인가: 예제 2의 ELF를 역어셈블해서 루프 한 바퀴가 몇 명령인지 세고, 측정값과 맞는지 검산한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/B7/bench; for c in m0p m3 m23 m55; do echo "=== $c";
  arm-none-eabi-objdump -d --no-show-raw-insn $c/app.elf | awk "/<div_loop>:/,/^$/"; done'
```

```asm
=== m0p   (발췌: 루프 몸체)
  96:	mov	r3, r9
  98:	muls	r4, r3
  9a:	ldr	r3, [pc, #44]	@ (c8 <div_loop+0x4c>)
  9c:	mov	r1, r8
  9e:	mov	ip, r3
  a0:	ands	r1, r5
  a2:	add	r4, ip
  a4:	movs	r0, r4
  a6:	adds	r1, #3
  a8:	bl	658 <__udivsi3>
  ac:	adds	r5, #1
  ae:	adds	r6, r6, r0
  b0:	cmp	r7, r5
  b2:	bne.n	96 <div_loop+0x1a>
=== m3
  88:	mla	ip, r4, ip, lr
  8c:	and.w	r3, r2, #15
  90:	adds	r3, #3
  92:	udiv	r3, ip, r3
  96:	adds	r2, #1
  98:	cmp	r1, r2
  9a:	add	r0, r3
  9c:	bne.n	88 <div_loop+0x10>
=== m23   (발췌)
  82:	movw	r5, #20077	@ 0x4e6d
  8c:	movt	r5, #16838	@ 0x41c6
  ...
  96:	muls	r2, r5
  98:	ands	r3, r4
  9a:	add	r2, ip
  9c:	adds	r3, #3
  9e:	udiv	r3, r2, r3
=== m55
10000086:	dls	lr, r1
  ...
10000092:	mla	r2, r4, r2, r1
10000096:	and.w	r3, ip, #15
1000009a:	adds	r3, #3
1000009c:	udiv	r3, r2, r3
100000a0:	add.w	ip, ip, #1
100000a4:	add	r0, r3
100000a6:	le	lr, 10000092 <div_loop+0x16>
```

출력에서 볼 것:

- **M0+**: `bl __udivsi3` — 하드웨어 나눗셈이 없어서 libgcc 함수를 부른다(`__udivsi3`는 `__aeabi_uidiv`의 별칭). 루프 몸체 14개 + 함수 안 약 180개 = 한 바퀴 약 193 명령 → 2,000바퀴에 386,437. 게다가 v6-M은 R8 이상 레지스터를 산술에 바로 못 써서 `mov r3, r9` 같은 옮겨 담기가 섞인다.
- **M3**: 8 명령 × 2,000 = 16,000 → 측정 16,040과 일치. `mla`(곱셈-누적) · `and.w`(32비트 Thumb-2) · `udiv`.
- **M23**: Baseline인데도 `udiv`가 있다. 그리고 32비트 상수를 `movw`/`movt`로 만든다(v6-M은 리터럴 풀 `ldr`만 가능). v8-M Baseline이 v6-M에 더한 명령이 그대로 보인다.
- **M55**: 루프 앞에 `dls lr, r1`(루프 횟수를 LR에 넣고 시작), 끝에 `le lr, …`(LR을 1 줄이고 0이 아니면 되돌아감). `adds; cmp; bne` 세 개가 `le` 하나로 줄어서 7 명령 × 2,000 = 14,000 → 측정 14,031과 일치. 이것이 **LOB**다.

원자 카운터도 같은 식으로 보자.

```asm
=== m0p  atomic_loop
  d4:	mrs	r0, PRIMASK
  d8:	cpsid	i
  da:	ldr	r3, [r1, #0]
  dc:	adds	r3, #1
  de:	str	r3, [r1, #0]
  e0:	msr	PRIMASK, r0
  e4:	subs	r2, #1
  e6:	bcs.n	d4 <atomic_loop+0x8>
=== m3   atomic_loop
  ae:	ldrex	r1, [r2]
  b2:	adds	r1, #1
  b4:	strex	r0, r1, [r2]
  b8:	cmp	r0, #0
  ba:	bne.n	ae <atomic_loop+0x6>
  bc:	subs	r3, #1
  be:	bcs.n	ae <atomic_loop+0x6>
=== m55  atomic_loop
100000be:	ldrex	r2, [r3]
100000c2:	adds	r2, #1
100000c4:	strex	r1, r2, [r3]
100000c8:	cmp	r1, #0
100000ca:	bne.n	100000be <atomic_loop+0xa>
100000cc:	le	lr, 100000be <atomic_loop+0xa>
```

출력에서 볼 것: M0+는 8 명령(16,000), M3는 7 명령(14,000), M55는 6 명령(12,000). **명령 수는 비슷하지만 의미가 다르다.** M0+ 판은 그 사이 모든 인터럽트를 막는다(인터럽트 지연이 생긴다). M3 판은 아무것도 막지 않고, 다른 쪽이 끼어들면 `strex`가 실패해서 다시 시도할 뿐이다. RTOS 관점에서 이 차이가 §4.1~4.2의 주제다.

CRC 안쪽 루프의 LOB도 확인했다(M55는 `dls lr, r3` … `le lr`로 바깥 루프 8회를 돌고, 바이트당 4 × 8 + 6 = 38 명령 → 1,024바이트에 38,912 ≈ 측정 38,937). M3는 바이트당 45(46,080 = 45 × 1,024 정확히 일치), M0+는 바이트당 70(71,680 ≈ 71,750)이다. M0+에는 `sbfx`(비트필드 부호 확장)와 "시프트한 피연산자로 EOR" 같은 Thumb-2 명령이 없어서 `lsrs` · `negs` · `ands`로 풀어 쓴다.

```asm
=== crc m3   안쪽 루프 (5 명령 × 8비트)
  58:	sbfx	r3, r2, #0, #1
  5c:	ands	r3, r0
  5e:	subs	r1, #1
  60:	eor.w	r2, r3, r2, lsr #1
  64:	bne.n	58 <crc32_sw+0x18>
=== crc m55  안쪽 루프 (4 명령 × 8비트, 카운터는 LR)
1000005c:	sbfx	r3, r2, #0, #1
10000060:	ands	r3, r1
10000062:	eor.w	r2, r3, r2, lsr #1
10000066:	le	lr, 1000005c <crc32_sw+0x1c>
```

내적(`dot16`)에서는 M4 · M7 · M33 · M55가 DSP 명령 `smlabb`(16비트 × 16비트 + 누적)를 쓰고, M3는 일반 `mla`, M0+는 `muls` + `adds`를 쓴다. 다만 **GCC 14는 이 루프를 Helium으로 자동 벡터화하지 않았다**(§4.7에서 확인).

### 3.4 사이클로는 어떻게 다른가 — QEMU가 못 보여 주는 것

명령 수가 같아도 사이클은 다르다. 이 노트의 측정으로는 보일 수 없으니 출처의 숫자로만 정리한다.

- **나눗셈**: M3 · M4의 `UDIV`는 2~12 사이클(피연산자에 따라 일찍 끝남, Arm TRM의 일반 설명). M0+의 소프트웨어 나눗셈은 수십~수백 사이클.
- **곱셈**: M0/M0+의 곱셈기는 칩 설계 시 1사이클 또는 32사이클(작은 면적) 중 하나로 고른다. 같은 `muls`라도 칩마다 32배 차이가 날 수 있다.
- **M7 dual-issue**: 한 사이클에 명령 2개까지 → 같은 명령 수라도 사이클이 줄어든다. 대신 캐시 미스 · 플래시 wait state의 영향이 커진다(B8).
- **벤치마크 지표**: Arm 비교표의 CoreMark/MHz가 이 차이를 요약한다 — M0+ 2.46, M3 3.45, M4 3.54, M33 4.10, M55 4.40, M7 5.29, M85 6.28. M55가 M7보다 클록당 성능이 낮은 것은 파이프라인이 짧고 superscalar가 아니기 때문이다. 대신 Helium으로 DSP · ML을 벌충한다.

### 3.5 예제 4 — 코드 크기

무엇을 확인하는 코드인가: 예제 2의 ELF에서 `nm -S`로 함수별 크기를 읽는다. M23은 실행은 못 해도 크기는 잴 수 있다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/B7/bench; printf "%-5s %7s %7s %7s %7s %7s %7s\n" core crc32 div atomic dot16 udivsi text
for c in m0p m3 m4 m7 m23 m33 m55; do f=$c/app.elf
  g(){ arm-none-eabi-nm -S $f | awk -v n=$1 "\$4==n{print strtonum(\"0x\"\$2)}"; }
  printf "%-5s %7s %7s %7s %7s %7s %7s\n" $c $(g crc32_sw) $(g div_loop) $(g atomic_loop) $(g dot16) "$(g __udivsi3)" \
    $(arm-none-eabi-size $f | awk "NR==2{print \$1}"); done'
```

```text
core    crc32     div  atomic   dot16  udivsi    text
m0p        60      80      36      34     266    2568
m3         56      48      32      40            2232
m4         56      52      32      38            2228
m7         60      48      32      38            2244
m23        58      52      32      32            2788
m33        56      48      32      40            2320
m55        60      56      40      56            2388
```

출력에서 볼 것: 함수 하나하나는 30~80바이트로 비슷하다. Thumb-2의 32비트 명령이 섞여도 한 명령이 일을 더 많이 하니 크기가 거의 그대로다. 크기 차이를 만드는 것은 **라이브러리 도우미**다 — M0+는 나눗셈 하나 때문에 `__udivsi3` 266바이트가 붙었다. M23의 전체 `.text`가 가장 큰 것(2,788)도 같은 이유다. 측정용 `insn()` 함수의 64비트 나눗셈을 처리하는 libgcc `__udivmoddi4`가 v7-M판은 754바이트(0x2f2), v8-M Baseline판은 980바이트(0x3d4)다(`nm -S --size-sort`로 확인). Baseline에는 IT 블록 · 시프트 결합 피연산자가 없어서 같은 C 코드가 더 길어진다. M55는 LOB 루프 앞에 반복 횟수 계산 명령이 붙어서 `dot16`이 56바이트로 가장 크다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="340.0" y="22" font-size="14" text-anchor="middle" font-weight="bold">벤치 함수 4개의 코드 크기 (바이트, -O2, nm -S 실측)</text>
<text x="62" y="63" font-size="13" text-anchor="end">M0+</text>
<rect x="70.0" y="48" width="63.0" height="22" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="133.0" y="48" width="84.0" height="22" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="217.0" y="48" width="37.8" height="22" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
<rect x="254.8" y="48" width="35.7" height="22" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<rect x="290.5" y="48" width="279.3" height="22" fill="#888" fill-opacity="0.7" stroke="#888"/>
<text x="575.8" y="63" font-size="12">476 B</text>
<text x="62" y="95" font-size="13" text-anchor="end">M3</text>
<rect x="70.0" y="80" width="58.8" height="22" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="128.8" y="80" width="50.4" height="22" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="179.2" y="80" width="33.6" height="22" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
<rect x="212.8" y="80" width="42.0" height="22" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="260.8" y="95" font-size="12">176 B</text>
<text x="62" y="127" font-size="13" text-anchor="end">M4</text>
<rect x="70.0" y="112" width="58.8" height="22" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="128.8" y="112" width="54.6" height="22" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="183.4" y="112" width="33.6" height="22" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
<rect x="217.0" y="112" width="39.9" height="22" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="262.9" y="127" font-size="12">178 B</text>
<text x="62" y="159" font-size="13" text-anchor="end">M7</text>
<rect x="70.0" y="144" width="63.0" height="22" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="133.0" y="144" width="50.4" height="22" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="183.4" y="144" width="33.6" height="22" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
<rect x="217.0" y="144" width="39.9" height="22" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="262.9" y="159" font-size="12">178 B</text>
<text x="62" y="191" font-size="13" text-anchor="end">M23</text>
<rect x="70.0" y="176" width="60.9" height="22" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="130.9" y="176" width="54.6" height="22" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="185.5" y="176" width="33.6" height="22" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
<rect x="219.1" y="176" width="33.6" height="22" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="258.7" y="191" font-size="12">174 B</text>
<text x="62" y="223" font-size="13" text-anchor="end">M33</text>
<rect x="70.0" y="208" width="58.8" height="22" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="128.8" y="208" width="50.4" height="22" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="179.2" y="208" width="33.6" height="22" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
<rect x="212.8" y="208" width="42.0" height="22" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="260.8" y="223" font-size="12">176 B</text>
<text x="62" y="255" font-size="13" text-anchor="end">M55</text>
<rect x="70.0" y="240" width="63.0" height="22" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<rect x="133.0" y="240" width="58.8" height="22" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="191.8" y="240" width="42.0" height="22" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
<rect x="233.8" y="240" width="58.8" height="22" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="298.6" y="255" font-size="12">212 B</text>
<rect x="20" y="297" width="14" height="12" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="40" y="308" font-size="12">crc32_sw</text>
<rect x="122.0" y="297" width="14" height="12" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<text x="142.0" y="308" font-size="12">div_loop</text>
<rect x="224.0" y="297" width="14" height="12" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
<text x="244.0" y="308" font-size="12">atomic_loop</text>
<rect x="348.5" y="297" width="14" height="12" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="368.5" y="308" font-size="12">dot16</text>
<rect x="428.0" y="297" width="14" height="12" fill="#888" fill-opacity="0.7" stroke="#888"/>
<text x="448.0" y="308" font-size="12">__udivsi3 (libgcc)</text>
</svg>
```

그림 4 — 벤치 함수 4개의 코드 크기(바이트). M0+만 나눗셈 도우미 `__udivsi3`(회색)가 붙는다. 코어 세대 차이보다 "그 코어에 없는 명령을 라이브러리로 채우는 비용"이 크다.

Don 경험과 연결: SSD 펌웨어에서 "핫 패스에 나눗셈 금지, 시프트로 바꿔라" 같은 규칙이 있었다면, 그 규칙은 코어에 따라 의미가 달라진다. M3 이상에서는 `UDIV`가 몇 사이클이라 규칙의 가치가 작지만, M0+에서는 ISR 안의 나눗셈 하나가 수백 명령이 된다. 드론 ESC가 M0+급이라면 PWM 주기 계산은 반드시 미리 계산한 표나 시프트로 한다.

---

## 4. RTOS 포트에서 달라지는 것 — 8가지

RTOS 커널의 99%(`tasks.c`, `queue.c`, Zephyr `kernel/`)는 코어와 무관한 C다. 코어마다 다른 부분은 **포트(port)** 라는 작은 층에 모여 있다: 크리티컬 섹션, 컨텍스트 스위치(PendSV), 첫 태스크 시작(SVC), tick(SysTick), 스택 프레임 초기화. 이 절은 세대 차이가 그 층의 어디를 바꾸는지 하나씩 본다.

### 4.1 크리티컬 섹션 — PRIMASK냐 BASEPRI냐

직관: 크리티컬 섹션은 "잠깐 아무도 끼어들지 마"다. 방법은 두 가지다. **전부 막기(PRIMASK=1)** 또는 **"이 우선순위 이하만 막기"(BASEPRI=N)**. 앞쪽은 단순하지만, 커널이 리스트를 고치는 수 µs 동안 모터 PWM 갱신 같은 급한 인터럽트까지 늦어진다. 뒤쪽은 커널과 상관없는 급한 인터럽트를 살려 둔다. 이렇게 살아남는 인터럽트를 Zephyr에서는 **zero-latency interrupt**라고 부른다(대신 그 ISR은 RTOS API를 부르면 안 된다 — B3, E 모듈).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">크리티컬 섹션이 막는 인터럽트 범위 — PRIMASK vs BASEPRI</text>
<text x="170" y="48" font-size="13" text-anchor="middle" font-weight="bold">ARM_CM0 / ARM_CM23 포트</text>
<text x="170" y="66" font-size="12" text-anchor="middle">Armv6-M · Armv8-M Baseline: PRIMASK=1</text>
<rect x="40" y="80" width="260" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="52" y="100" font-size="12">NMI · HardFault</text>
<text x="290" y="100" font-size="12" text-anchor="end">통과</text>
<rect x="40" y="116" width="260" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="52" y="136" font-size="12">우선순위 0x00 (모터 PWM)</text>
<text x="290" y="136" font-size="12" text-anchor="end">막힘</text>
<rect x="40" y="152" width="260" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="52" y="172" font-size="12">우선순위 0x10 (IMU DRDY)</text>
<text x="290" y="172" font-size="12" text-anchor="end">막힘</text>
<rect x="40" y="188" width="260" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="52" y="208" font-size="12">우선순위 0x20 (UART)</text>
<text x="290" y="208" font-size="12" text-anchor="end">막힘</text>
<rect x="40" y="224" width="260" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="52" y="244" font-size="12">우선순위 0x80 (타이머)</text>
<text x="290" y="244" font-size="12" text-anchor="end">막힘</text>
<rect x="40" y="260" width="260" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="52" y="280" font-size="12">0xFF PendSV · SysTick</text>
<text x="290" y="280" font-size="12" text-anchor="end">막힘</text>
<text x="170" y="308" font-size="12" text-anchor="middle">NMI·HardFault 말고 전부 막힌다</text>
<text x="500" y="48" font-size="13" text-anchor="middle" font-weight="bold">ARM_CM3 / CM4F / CM33 포트</text>
<text x="500" y="66" font-size="12" text-anchor="middle">Armv7-M · Armv8-M Mainline: BASEPRI=0x20</text>
<rect x="370" y="80" width="260" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="382" y="100" font-size="12">NMI · HardFault</text>
<text x="620" y="100" font-size="12" text-anchor="end">통과</text>
<rect x="370" y="116" width="260" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="382" y="136" font-size="12">우선순위 0x00 (모터 PWM)</text>
<text x="620" y="136" font-size="12" text-anchor="end">통과</text>
<rect x="370" y="152" width="260" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="382" y="172" font-size="12">우선순위 0x10 (IMU DRDY)</text>
<text x="620" y="172" font-size="12" text-anchor="end">통과</text>
<rect x="370" y="188" width="260" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="382" y="208" font-size="12">우선순위 0x20 (UART)</text>
<text x="620" y="208" font-size="12" text-anchor="end">막힘</text>
<rect x="370" y="224" width="260" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="382" y="244" font-size="12">우선순위 0x80 (타이머)</text>
<text x="620" y="244" font-size="12" text-anchor="end">막힘</text>
<rect x="370" y="260" width="260" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="382" y="280" font-size="12">0xFF PendSV · SysTick</text>
<text x="620" y="280" font-size="12" text-anchor="end">막힘</text>
<text x="500" y="308" font-size="12" text-anchor="middle">0x00~0x1F는 커널 밖에서 계속 돈다</text>
</svg>
```

그림 5 — 크리티컬 섹션이 막는 범위. 왼쪽(Armv6-M · v8-M Baseline)은 BASEPRI가 없어서 PRIMASK로 전부 막는다. 오른쪽(Armv7-M 이상 Mainline)은 `configMAX_SYSCALL_INTERRUPT_PRIORITY`(여기서는 0x20)보다 숫자가 작은 인터럽트를 살려 둔다. 우선순위 이름(모터 PWM, IMU)은 드론 비행 제어기 예시다.

FreeRTOS 소스에서 그대로 확인할 수 있다.

```sh
limactl shell elinux -- bash -lc 'cd /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC
  grep -n "portDISABLE_INTERRUPTS\|portENABLE_INTERRUPTS\|portSET_INTERRUPT_MASK_FROM_ISR" \
    ARM_CM0/portmacro.h ARM_CM3/portmacro.h ARM_CM33_NTZ/non_secure/portmacro.h ARM_CM33_NTZ/non_secure/portmacrocommon.h'
```

```text
ARM_CM0/portmacro.h:306:#define portSET_INTERRUPT_MASK_FROM_ISR()       ulSetInterruptMask()
ARM_CM0/portmacro.h:308:#define portDISABLE_INTERRUPTS()                __asm volatile ( " cpsid i " ::: "memory" )
ARM_CM0/portmacro.h:309:#define portENABLE_INTERRUPTS()                 __asm volatile ( " cpsie i " ::: "memory" )
ARM_CM3/portmacro.h:117:#define portSET_INTERRUPT_MASK_FROM_ISR()         ulPortRaiseBASEPRI()
ARM_CM3/portmacro.h:119:#define portDISABLE_INTERRUPTS()                  vPortRaiseBASEPRI()
ARM_CM3/portmacro.h:120:#define portENABLE_INTERRUPTS()                   vPortSetBASEPRI( 0 )
ARM_CM33_NTZ/non_secure/portmacro.h:73:#define portDISABLE_INTERRUPTS()    ulSetInterruptMask()
ARM_CM33_NTZ/non_secure/portmacro.h:74:#define portENABLE_INTERRUPTS()     vClearInterruptMask( 0 )
ARM_CM33_NTZ/non_secure/portmacrocommon.h:446:#define portSET_INTERRUPT_MASK_FROM_ISR()         ulSetInterruptMask()
```

v8-M 포트는 이름이 같은 `ulSetInterruptMask()`를 Baseline과 Mainline에서 다르게 구현한다.

```c
/* ARM_CM23_NTZ/non_secure/portasm.c — Baseline: PRIMASK */
uint32_t ulSetInterruptMask( void ) /* __attribute__(( naked )) PRIVILEGED_FUNCTION */
{
    __asm volatile
    (
        "   .syntax unified                                 \n"
        "                                                   \n"
        "   mrs r0, PRIMASK                                 \n"
        "   cpsid i                                         \n"
        "   bx lr                                           \n"
        ::: "memory"
    );
}
/* ARM_CM33_NTZ/non_secure/portasm.c — Mainline: BASEPRI */
uint32_t ulSetInterruptMask( void ) /* __attribute__(( naked )) PRIVILEGED_FUNCTION */
{
    __asm volatile
    (
        "   .syntax unified                                 \n"
        "                                                   \n"
        "   mrs r0, basepri                                 \n" /* r0 = basepri. Return original basepri value. */
        "   mov r1, %0                                      \n" /* r1 = configMAX_SYSCALL_INTERRUPT_PRIORITY. */
        "   msr basepri, r1                                 \n" /* Disable interrupts up to configMAX_SYSCALL_INTERRUPT_PRIORITY. */
        "   dsb                                             \n"
        "   isb                                             \n"
        "   bx lr                                           \n" /* Return. */
        ::"i" ( configMAX_SYSCALL_INTERRUPT_PRIORITY ) : "memory"
    );
}
```

Zephyr도 같은 갈림길을 Kconfig 기호로 나눈다(`include/zephyr/arch/arm/asm_inline_gcc.h`).

```c
/* include/zephyr/arch/arm/asm_inline_gcc.h:44 발췌 (Zephyr v4.4.2) */
static ALWAYS_INLINE unsigned int arch_irq_lock(void)
{
	unsigned int key;

#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
#if CONFIG_MP_MAX_NUM_CPUS == 1 || defined(CONFIG_ARMV8_M_BASELINE)
	key = __get_PRIMASK();
	__disable_irq();
#else
#error "Cortex-M0 and Cortex-M0+ require SoC specific support for cross core synchronisation."
#endif
#elif defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
	key = __get_BASEPRI();
	__set_BASEPRI_MAX(_EXC_IRQ_DEFAULT_PRIO);
	__ISB();
```

출력(소스)에서 볼 것: Zephyr는 기호 이름 자체가 `ARMV6_M_ARMV8_M_BASELINE`(v6-M과 v8-M Baseline을 한 묶음)과 `ARMV7_M_ARMV8_M_MAINLINE`(v7-M과 v8-M Mainline을 한 묶음)이다. §1.1의 "두 갈래" 그림이 커널 소스에 그대로 박혀 있다. 그리고 `#error` 줄이 중요하다: **듀얼 M0+(RP2040 같은)에서 PRIMASK는 자기 코어만 막는다.** 다른 코어와의 동기화는 SoC 고유 하드웨어(RP2040은 SIO 블록의 하드웨어 스핀락 32개)가 필요하다는 뜻이다.

#### 예제 5 — v6-M에서 `msr basepri`는 어셈블은 되지만 아무 일도 안 한다

무엇을 확인하는 코드인가: BASEPRI에 0x20을 쓰고 다시 읽는다. 어셈블러는 v6-M에서도 `msr basepri`를 받아 준다(MSR 명령 자체는 v6-M에 있고, BASEPRI는 "구현 안 된 특수 레지스터 번호"일 뿐이라). 그러면 실제 코어는 어떻게 할까?

```c
/* ex_basepri: v6-M에서 'msr basepri'는 어셈블은 되지만 아무 일도 하지 않는다 — 읽어서 확인 */
#include "semi.h"
int main(void)
{
    uint32_t w = 0x20, r;
    __asm volatile(".syntax unified\n msr basepri, %0\n mrs %1, basepri\n"
#if __ARM_ARCH_ISA_THUMB == 1
                   ".syntax divided\n"
#endif
                   : "+r"(w), "=r"(r));
    semi_printf("wrote BASEPRI=0x20, read back 0x%x -> %s\n", r,
                r == 0x20 ? "masking works" : "ignored: no BASEPRI on this core");
    return 0;
}
```

```text
--- m0p
wrote BASEPRI=0x20, read back 0x0 -> ignored: no BASEPRI on this core
--- m3
wrote BASEPRI=0x20, read back 0x20 -> masking works
--- m33
wrote BASEPRI=0x20, read back 0x20 -> masking works
```

출력에서 볼 것: M0(QEMU `microbit`)에서는 쓴 값이 사라진다. **빌드 에러도, fault도 없이** 크리티컬 섹션이 아무것도 막지 않게 된다. M3/M4용 코드(직접 짠 `BASEPRI` 크리티컬 섹션)를 M0+로 옮기면서 이 줄을 그대로 두면, 버그는 "아주 가끔 리스트가 깨진다"로만 나타난다. 같은 어셈블이 M23용 빌드(`-mcpu=cortex-m23`)에서도 통과하는 것을 확인했다(`msr BASEPRI, r3`로 역어셈블됨). 아키텍처 매뉴얼은 구현 안 된 특수 레지스터 접근을 UNPREDICTABLE 또는 무시로 다루므로, QEMU의 "무시" 동작을 실칩도 똑같이 한다고 기대하면 안 된다 — 어느 쪽이든 쓰면 안 되는 코드다.

### 4.2 원자 연산 — LDREX/STREX가 없는 코어

#### 예제 6 — v6-M에 없는 것을 쓰면 어디서 막히나

무엇을 확인하는 코드인가: (가) C11 `__atomic_fetch_add`를 쓰는 파일을 M0+ · M23 · M3로 링크해 본다. (나) `udiv` · `ldrex`를 인라인 어셈블리로 직접 쓴 파일을 같은 세 코어로 어셈블해 본다.

```c
/* atom.c — v6-M에서 C11 원자 연산을 쓰면? */
#include <stdint.h>
volatile uint32_t counter;
void inc(void) { __atomic_fetch_add(&counter, 1u, __ATOMIC_SEQ_CST); }
int main(void) { inc(); return (int)counter; }
void _start(void) { main(); for (;;) { } }
```

```c
/* insn.c — v6-M에 없는 명령을 인라인 어셈블리로 직접 쓰면? */
#include <stdint.h>
uint32_t f(uint32_t *p, uint32_t a, uint32_t b)
{
    uint32_t q, v;
    __asm volatile(".syntax unified\n udiv %0, %1, %2" : "=r"(q) : "r"(a), "r"(b));
    __asm volatile(".syntax unified\n ldrex %0, [%1]" : "=r"(v) : "r"(p));
    __asm volatile(".syntax unified\n msr basepri, %0" : : "r"(a));
    return q + v;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B7/v6; O=/var/tmp/rtos/B7/v6
for c in cortex-m0plus cortex-m23 cortex-m3; do echo "== $c: atom.c link"
  arm-none-eabi-gcc -mthumb -mcpu=$c -O2 -Wall -Wextra -nostdlib atom.c -o $O/atom_$c.elf -lgcc 2>&1 | head -3
done
for c in cortex-m0plus cortex-m23 cortex-m3; do echo "== $c: insn.c assemble"
  arm-none-eabi-gcc -mthumb -mcpu=$c -O2 -Wall -Wextra -c insn.c -o $O/insn_$c.o 2>&1 | grep Error; done'
```

```text
== cortex-m0plus: atom.c link
ld: /tmp/cckBkTDN.o: in function `inc':
atom.c:(.text+0x8): undefined reference to `__atomic_fetch_add_4'
ld: (__atomic_fetch_add_4): Unknown destination type (ARM/Thumb) in /tmp/cckBkTDN.o
== cortex-m23: atom.c link
== cortex-m3: atom.c link
== cortex-m0plus: insn.c assemble
tmp.s:29: Error: selected processor does not support `udiv r2,r1,r2' in Thumb mode
tmp.s:33: Error: selected processor does not support `ldrex r0,[r0]' in Thumb mode
== cortex-m23: insn.c assemble
== cortex-m3: insn.c assemble
```

(링커 경로 앞부분은 `ld:`로 줄였다.) 출력에서 볼 것: M0+에서 GCC는 원자 연산을 인라인으로 만들 수 없어 `__atomic_fetch_add_4` 호출을 남기고, bare-metal `arm-none-eabi`에는 libatomic이 없어서 **링크 에러**가 난다. `udiv` · `ldrex`는 **어셈블 에러**로 막힌다. 그런데 세 번째 줄 `msr basepri`는 M0+에서도 에러가 없다 — 예제 5의 함정이 여기서 다시 보인다. M23 · M3는 셋 다 통과한다.

M23의 `inc()`를 역어셈블하면 Armv8-M에서 생긴 acquire/release 배타 명령이 보인다.

```asm
0000801c <inc>:   (cortex-m23, __ATOMIC_SEQ_CST)
    8024:	ldaex	r1, [r3]
    8028:	adds	r1, #1
    802a:	stlex	r2, r1, [r3]
    802e:	cmp	r2, #0
    8030:	bne.n	8024 <inc+0x8>
```

M3는 같은 코드를 `dmb ish; ldrex; …; strex; dmb ish`로 만든다(v7-M에는 LDAEX/STLEX가 없어서 배리어를 따로 넣는다). 메모리 순서 자체는 D8에서 다룬다.

RTOS 커널 안에서는: FreeRTOS 커널은 단일 코어에서 원자 연산을 쓰지 않고 **크리티컬 섹션**으로 보호한다. 그래서 `ARM_CM0` 포트가 있어도 커널은 잘 돈다. 문제는 **앱 코드**다. 락 없는 SPSC 링버퍼(Anduril 면접 단골)를 M4에서 `__atomic`으로 짰다가 M0+로 옮기면 링크가 안 된다. 고치는 법: (1) 단일 생산자 · 단일 소비자이고 인덱스가 32비트 정렬 변수면 원자 RMW가 필요 없다(읽기와 쓰기 각각은 원래 원자적이다 — 인덱스를 각자 한쪽만 쓴다), (2) RMW가 필요하면 PRIMASK 크리티컬 섹션(예제 2의 `atomic_inc` v6-M 판), (3) 멀티코어 M0+면 칩의 하드웨어 스핀락.

### 4.3 명령 · 레지스터 차이가 PendSV를 바꾼다

v6-M의 `STM`/`LDM`/`PUSH`는 R0~R7만 다룬다. 컨텍스트 스위치는 R4~R11을 저장해야 하므로, M0 포트는 R8~R11을 R4~R7로 옮겨 담아 두 번에 나눠 저장한다. 예제 11의 ELF(같은 FreeRTOS 앱을 코어별로 빌드)에서 PendSV를 역어셈블해 비교한다.

```asm
=== ARM_CM0 (M0+ 빌드) PendSV_Handler — 발췌
     e30:	mrs	r0, PSP
     e38:	subs	r0, #36	@ 0x24
     e3c:	mov	r3, lr
     e3e:	stmia	r0!, {r3, r4, r5, r6, r7}
     e40:	mov	r4, r8
     e42:	mov	r5, r9
     e44:	mov	r6, sl
     e46:	mov	r7, fp
     e48:	stmia	r0!, {r4, r5, r6, r7}
     e4a:	cpsid	i
     e4c:	bl	894 <vTaskSwitchContext>
     e50:	cpsie	i
=== ARM_CM3 PendSV_Handler — 발췌
     b74:	mrs	r0, PSP
     b80:	stmdb	r0!, {r4, r5, r6, r7, r8, r9, sl, fp}
     b88:	mov.w	r0, #32
     b8c:	msr	BASEPRI, r0
     b90:	bl	86c <vTaskSwitchContext>
     b94:	mov.w	r0, #0
     b98:	msr	BASEPRI, r0
=== ARM_CM4F PendSV_Handler — 발췌
     bdc:	tst.w	lr, #16
     be0:	it	eq
     be2:	vstmdbeq	r0!, {s16-s31}
     be6:	stmdb	r0!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
     bee:	mov.w	r0, #32
     bf2:	msr	BASEPRI, r0
     bf6:	dsb	sy
     bfa:	isb	sy
```

출력에서 볼 것: (1) M0 포트는 `stmia` 두 번 + `mov` 네 번, M3 포트는 `stmdb` 한 번. (2) `vTaskSwitchContext` 호출을 감싸는 보호가 M0는 `cpsid i`, M3 · M4F는 `msr BASEPRI, #32`다(§4.1). (3) M4F는 EXC_RETURN의 bit 4(`tst.w lr, #16`)로 "이 태스크가 FPU를 썼나"를 보고, 썼을 때만 S16~S31을 저장한다 — **lazy stacking**과 짝을 이루는 코드다(상세 B6). (4) v6-M에는 IT 블록이 없어서 조건부 저장 같은 코드를 못 쓴다.

### 4.4 Armv8-M의 스택 한계 레지스터 — PSPLIM · MSPLIM

직관: v7-M까지는 태스크 스택이 넘쳐도 하드웨어가 모른다. FreeRTOS는 `configCHECK_FOR_STACK_OVERFLOW`로 컨텍스트 스위치 때 스택 끝의 패턴을 검사하지만, 그것은 **사후 검사**다. 넘친 순간 옆 메모리(다른 태스크의 TCB, 큐 버퍼)는 이미 덮였다. Armv8-M Mainline은 `PSPLIM`(태스크 스택용)과 `MSPLIM`(인터럽트 스택용)을 더했다. SP가 그 값보다 아래로 내려가는 **바로 그 명령에서** UsageFault(STKOF)가 난다. Armv8-M Baseline(M23)에는 Secure 쪽 한계 레지스터만 있다.

#### 예제 7 — 같은 스택 넘침을 M3와 M33 · M55에서

무엇을 확인하는 코드인가: 512바이트 "태스크 스택"을 만들고 그 아래에 guard 영역을 둔다. PSP로 전환해서(RTOS 태스크처럼) 프레임 40바이트짜리 재귀를 17단 부른다(680바이트 > 512). v8-M에서는 PSPLIM을 스택 바닥으로 설정한다.

```c
/* ex_stklim: 태스크 스택(PSP) 넘침 — v8-M은 PSPLIM이 잡고, v7-M은 조용히 옆 메모리를 덮는다 */
#include "semi.h"
static struct { uint32_t guard[64]; uint32_t stack[128]; } area;   /* 스택 아래(낮은 주소)에 guard */
__attribute__((noinline)) uint32_t rec(uint32_t n)
{
    volatile uint32_t buf[8];
    buf[0] = n;
    if (n == 0) return 0;
    return rec(n - 1) + buf[0];
}
int main(void)
{
    for (int i = 0; i < 64; i++) area.guard[i] = 0xDEADBEEFu;
    uint32_t top = (uint32_t)&area.stack[128], base = (uint32_t)&area.stack[0];
    REG32(0xE000ED24u) |= 1u << 18;                    /* SHCSR.USGFAULTENA (v6-M에는 없음) */
    __asm volatile("msr psp, %0" : : "r"(top));
#if defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8_1M_MAIN__)
    __asm volatile("msr psplim, %0" : : "r"(base));
    semi_printf("PSPLIM = 0x%x (stack base)\n", base);
#else
    (void)base; semi_printf("no PSPLIM on this core\n");
#endif
    __asm volatile("mrs r0, control\n orr r0, r0, #2\n msr control, r0\n isb" : : : "r0", "memory");
    uint32_t r = rec(16);                               /* 프레임 ≈ 48 B × 17 > 512 B */
    __asm volatile("mrs r0, control\n bic r0, r0, #2\n msr control, r0\n isb" : : : "r0", "memory");
    int bad = 0;
    for (int i = 0; i < 64; i++) bad += area.guard[i] != 0xDEADBEEFu;
    semi_printf("rec()=%u returned normally, guard words overwritten: %u of 64\n", r, (uint32_t)bad);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'H=/Users/donh/.rtos_scratch/B7; for c in m3 m33 m55; do echo "--- $c";
  CFL=-O1 RUN=1 $H/mc/mc.sh $c /var/tmp/rtos/B7/stklim/$c $H/ex_stklim.c; done'
```

```text
--- m3
no PSPLIM on this core
rec()=136 returned normally, guard words overwritten: 9 of 64
--- m33
PSPLIM = 0x38000100 (stack base)
[fault] exception 6 CFSR=0x100000 HFSR=0x0
--- m55
PSPLIM = 0x30000100 (stack base)
[fault] exception 6 CFSR=0x100000 HFSR=0x0
```

출력에서 볼 것: M3는 **정상 리턴하고 결과도 맞다(136 = 1+2+…+16)**. 그런데 guard 9워드가 이미 덮였다 — 실제 펌웨어였다면 옆 태스크의 데이터가 깨지고, 증상은 한참 뒤 엉뚱한 곳에서 나온다. M33 · M55는 넘치는 순간 **exception 6(UsageFault), CFSR=0x00100000** — UFSR의 bit 4가 STKOF(stack overflow)다. HFSR=0이므로 HardFault로 승격되지 않았다(SHCSR.USGFAULTENA를 켰기 때문). 코드 주석의 "≈ 48 B"는 실제로 역어셈블해 보니 `push {lr}` + `sub sp, #36` = 40바이트였다.

RTOS 커널 안에서는: FreeRTOS v8-M 포트는 태스크마다 PSPLIM을 저장 · 복원한다. `ARM_CM33_NTZ/non_secure/portasm.c`의 PendSV에 `mrs r3, psplim` … `msr psplim, r3`가 있고(예제 10의 grep 출력), `pxPortInitialiseStack()`이 스택 바닥 주소를 첫 PSPLIM 값으로 넣는다. Zephyr는 `CONFIG_BUILTIN_STACK_GUARD`로 같은 일을 하고(`arch/arm/core/cortex_m/svc.S:468: msr PSPLIM, ip`), v8-M 보드에서는 `CONFIG_HW_STACK_PROTECTION=y`가 자동으로 이것을 고른다(예제 12). v7-M에서는 MPU 영역 하나를 스택 바닥 guard로 쓰는 `CONFIG_MPU_STACK_GUARD`가 대안이다(F4).

Don 경험과 연결: SSD 펌웨어에서 스택 넘침은 "가끔 NVMe 커맨드 하나가 이상한 상태로 끝난다" 같은 형태로 나타나서, 결국 스택 영역에 패턴을 칠해 두고 덤프로 high-water mark를 찾았을 것이다. PSPLIM은 그 검사를 **명령 단위 하드웨어 체크**로 바꾼다. 다만 M23(Baseline)의 Non-secure 쪽에는 없으니, M23 칩에서 Non-secure FreeRTOS를 돌리면 이 보호가 없다(FreeRTOS `ARM_CM23_NTZ` 포트는 Secure-only일 때만 `mrs r3, psplim`을 쓰고, 아니면 `movs r3, #0`으로 빈 칸을 채운다 — §5 예제 10 발췌).

### 4.5 TrustZone — ARM_CM33 vs ARM_CM33_NTZ

> 상세는 F6. 여기서는 "포트를 고르는 데 필요한 만큼"만.

Armv8-M에 TrustZone이 있으면 코어는 Secure 상태와 Non-secure 상태를 오간다. FreeRTOS는 v8-M 코어마다 포트를 두 벌 둔다.

- `ARM_CMxx`(`non_secure/` + `secure/`): FreeRTOS를 **Non-secure 쪽에서** 돌리고, 태스크가 Secure 쪽 함수(암호 키 · 부트 검증 같은)를 부를 수 있게 한다. 태스크마다 **Secure 쪽 컨텍스트(Secure 스택)** 를 따로 관리해야 해서 `secure/secure_context.c`(374줄) · `secure_heap.c`(485줄) · `secure_init.c`가 붙는다. `configENABLE_TRUSTZONE=1`.
- `ARM_CMxx_NTZ`(`non_secure/`만): TrustZone을 쓰지 않거나, 코어가 TrustZone 없이 만들어졌거나, **FreeRTOS 전체를 Secure 쪽에서만** 돌릴 때. 이 노트의 QEMU 예제는 전부 이쪽이다.

함정 하나: QEMU `mps2-an505` · `mps3-an547`은 Secure로 부팅하므로, NTZ 포트를 그대로 쓰면 첫 태스크로 돌아갈 때 EXC_RETURN이 "Non-secure로 복귀"(0xFFFFFFBC)가 되어 fault가 난다. **`configRUN_FREERTOS_SECURE_ONLY=1`** 이 EXC_RETURN을 0xFFFFFFFD(Secure, PSP, 기본 프레임)로 바꾼다.

```text
ARM_CM33_NTZ/non_secure/port.c:288:#if ( configRUN_FREERTOS_SECURE_ONLY == 1 )
ARM_CM33_NTZ/non_secure/port.c:304:    #define portINITIAL_EXC_RETURN    ( 0xfffffffd )
ARM_CM33_NTZ/non_secure/port.c:321:    #define portINITIAL_EXC_RETURN    ( 0xffffffbc )
```

(위는 `grep -n "configRUN_FREERTOS_SECURE_ONLY\|portINITIAL_EXC_RETURN"` 결과 중 해당 줄이다.) `port.c` 머리 주석(61~67줄)이 세 가지 조합 — Secure-only, TrustZone 사용, TrustZone 없는 Non-secure — 을 설명한다.

### 4.6 FPU 포트 — ARM_CM4F, 그리고 ARM_CM7/r0p1

> 상세(lazy stacking · FPCCR · 26워드 확장 프레임)는 B6.

- FPU가 있는 코어에서 하드 플로트(`-mfloat-abi=hard`)로 빌드하면 **`ARM_CM4F`** 포트를 쓴다. `ARM_CM3` 포트로 빌드하면 태스크 전환 때 S16~S31이 저장되지 않아 태스크끼리 FPU 레지스터를 덮어쓴다. `ARM_CM4F/port.c`는 시작할 때 `FPCCR`(0xE000EF34)에 ASPEN · LSPEN 비트(`portASPEN_AND_LSPEN_BITS`, 448줄)를 켠다.
- **Cortex-M7 r0p1 errata**: 초기 M7 리비전(r0p0 · r0p1)에는 "BASEPRI를 올리는 쓰기가 바로 다음 명령에 적용되지 않는다"는 errata가 있다(FreeRTOS 소스와 커밋은 837070번으로 부른다). 그 한 명령 사이에 낮은 우선순위 인터럽트가 들어올 수 있어서, FreeRTOS `ARM_CM7/r0p1` 포트는 `msr basepri` 앞뒤를 `cpsid i` / `cpsie i`로 감싼다.

```sh
limactl shell elinux -- bash -lc 'cd /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC; sed -n 1,20p ARM_CM7/ReadMe.txt;
  diff <(sed "s/r0p1//" ARM_CM7/r0p1/port.c) ARM_CM4F/port.c | head -40'
```

```text
There are two options for running FreeRTOS on ARM Cortex-M7 microcontrollers.
The best option depends on the revision of the ARM Cortex-M7 core in use.  The
revision is specified by an 'r' number, and a 'p' number, so will look something
like 'r0p1'.  Check the documentation for the microcontroller in use to find the
revision of the Cortex-M7 core used in that microcontroller.  If in doubt, use
the FreeRTOS port provided specifically for r0p1 revisions, as that can be used
with all core revisions.
...
30c30
< * Implementation of functions defined in portable.h for the ARM CM7 port.
---
> * Implementation of functions defined in portable.h for the ARM CM4F port.
58a59,64
> /* Constants used to detect a Cortex-M7 r0p1 core, which should use the ARM_CM7
>  * r0p1 port. */
> #define portCPUID                             ( *( ( volatile uint32_t * ) 0xE000ed00 ) )
> #define portCORTEX_M7_r0p1_ID                 ( 0x410FC271UL )
> #define portCORTEX_M7_r0p0_ID                 ( 0x410FC270UL )
...
>     configASSERT( portCPUID != portCORTEX_M7_r0p1_ID );
>     configASSERT( portCPUID != portCORTEX_M7_r0p0_ID );
...
513d524
<         "   cpsid i                             \n" /* ARM Cortex-M7  Errata 837070 workaround. */
517d527
<         "   cpsie i                             \n" /* ARM Cortex-M7  Errata 837070 workaround. */
```

(ReadMe 뒤쪽과 diff 중간은 `...`로 줄였다.) 출력에서 볼 것: 두 포트의 차이는 **CPUID 검사 assert 두 줄**과 **PendSV 안의 `cpsid i`/`cpsie i` 두 줄**뿐이다. `ARM_CM4F` 포트를 M7 r0p1에서 쓰면 assert가 걸려서 잘못된 선택을 알려 준다. 예제 1에서 QEMU M7은 r1p2(`0x411FC272`)였으니 둘 다 쓸 수 있다. 참고로 Arm의 공개 errata 문서에서는 같은 현상에 다른 번호가 붙어 있다는 보고가 있어, 실칩에서는 그 칩의 errata 문서 번호로 다시 확인한다(확인 못 함 — 번호만).

### 4.7 Helium(MVE) — 컴파일러, 포트, 그리고 CPACR

> 상세(벡터 레지스터와 FPU 레지스터 공유, 컨텍스트 크기, beat)는 B6.

#### 예제 8 — M55에서 Helium on/off, 그리고 CPACR을 잊으면

무엇을 확인하는 코드인가: (가) int8 배열 덧셈은 GCC 자동 벡터화에 맡기고, (나) int16 내적은 Helium intrinsic(`vctp16q`로 남은 원소만 켜는 tail predication + `vmladavaq_p_s16`)으로 쓴다. 같은 소스를 MVE 켜고(`-mfloat-abi=hard`) 끄고(`-mcpu=cortex-m55+nomve+nofp`) 빌드해 명령 수를 비교한다. N=1000으로 8 · 16의 배수가 아니게 해서 꼬리 처리를 일부러 만든다.

```c
/* ex_mve: M55 Helium — 자동 벡터화(add8)와 intrinsic(dot16, tail predication)을 스칼라와 비교 */
#include "semi.h"
#ifdef __ARM_FEATURE_MVE
#include <arm_mve.h>
#endif
#define N 1000                               /* 8·16의 배수가 아님 → 꼬리 처리 필요 */
static int16_t a[N], b[N];
static int8_t x8[N], y8[N], z8[N];
__attribute__((noinline)) int32_t dot16(const int16_t *p, const int16_t *q, uint32_t n)
{
#ifdef __ARM_FEATURE_MVE
    int32_t s = 0;
    while (n > 0) {                           /* vctp16q: 남은 원소 수만큼만 lane을 켠다 */
        mve_pred16_t m = vctp16q(n);
        s = vmladavaq_p_s16(s, vldrhq_z_s16(p, m), vldrhq_z_s16(q, m), m);
        p += 8; q += 8; n = n > 8 ? n - 8 : 0;
    }
    return s;
#else
    int32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += p[i] * q[i];
    return s;
#endif
}
__attribute__((noinline)) void add8(int8_t *restrict d, const int8_t *p, const int8_t *q, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = (int8_t)(p[i] + q[i]);   /* 자동 벡터화 대상 */
}
static uint32_t cal;
static void spin(uint32_t n) { __asm volatile("1: subs %0, %0, #1\n bne 1b" : "+l"(n) : : "cc"); }
#define INSN(d) ((uint32_t)((uint64_t)(d) * 1000000u / cal))
int main(void)
{
    for (int i = 0; i < N; i++) { a[i] = (int16_t)(i * 3 - 1500); b[i] = (int16_t)(700 - i);
                                  x8[i] = (int8_t)i; y8[i] = (int8_t)(3 * i); }
    tick_start();
    uint32_t t0 = tick_now(); spin(500000); cal = tick_now() - t0;
#ifdef __ARM_FEATURE_MVE
    const char *mode = "MVE on ";
#else
    const char *mode = "MVE off";
#endif
    t0 = tick_now(); int32_t r = dot16(a, b, N); uint32_t d1 = tick_now() - t0;
    t0 = tick_now(); add8(z8, x8, y8, N);        uint32_t d2 = tick_now() - t0;
    uint32_t cs = 0; for (int i = 0; i < N; i++) cs = cs * 31u + (uint8_t)z8[i];
    semi_printf("%s dot16 n=%u result=0x%x insn=%u\n", mode, N, (uint32_t)r, INSN(d1));
    semi_printf("%s add8  n=%u check =0x%x insn=%u\n", mode, N, cs, INSN(d2));
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'H=/Users/donh/.rtos_scratch/B7
  CFL="-O3" FLOAT=-mfloat-abi=hard RUN=1 $H/mc/mc.sh m55 /var/tmp/rtos/B7/mve_on $H/ex_mve.c
  CFL="-O3 -mcpu=cortex-m55+nomve+nofp" RUN=1 $H/mc/mc.sh m55 /var/tmp/rtos/B7/mve_off $H/ex_mve.c
  CFL="-O3 -DNO_CPACR" FLOAT=-mfloat-abi=hard RUN=1 $H/mc/mc.sh m55 /var/tmp/rtos/B7/mve_nocp $H/ex_mve.c; echo "exit=$?"'
```

```text
MVE on  dot16 n=1000 result=0xf114b7ac insn=1531
MVE on  add8  n=1000 check =0xebeb67d0 insn=406
MVE off dot16 n=1000 result=0xf114b7ac insn=4031
MVE off add8  n=1000 check =0xebeb67d0 insn=2531
[fault] exception 3 CFSR=0x80000 HFSR=0x40000000
exit=1
```

출력에서 볼 것: (1) 결과값은 같다. 내적은 4,031 → 1,531 명령(2.6배), 덧셈은 2,531 → 406 명령(6.2배) 줄었다. int8은 한 벡터에 16개, int16은 8개가 들어가니 기대와 맞는 방향이다. 사이클 이득은 M55가 dual-beat라 명령 수 이득보다 작다(QEMU로는 못 봄). (2) **세 번째 빌드는 CPACR을 안 켰더니 HardFault(exception 3)** 가 났다. CFSR=0x00080000은 UFSR bit 3 **NOCP**(코프로세서 접근 금지), HFSR=0x40000000은 FORCED(UsageFault가 HardFault로 승격). 어디서 났는지 역어셈블해 보면 `Reset_Handler` 안이다.

```text
1000033e:	vldrw.u32	q3, [r2], #16
10000342:	vstrw.32	q3, [r3], #16
100003aa:	vstrw.32	q3, [r3], #16
```

`-O3`가 `.data` 복사 · `.bss` 0 채우기 루프를 Helium으로 벡터화했다. 그래서 start.c는 **복사 루프보다 먼저** CPACR(CP10 · CP11)을 켠다. 이 노트의 첫 판 start.c는 순서가 반대여서 바로 이 fault를 만났다. 실칩에서도 같다: CMSIS의 `SystemInit()`이 CPACR을 켜는 이유이고, 벤더 startup 순서를 바꿀 때 조심할 점이다.

자동 벡터화도 확인했다. GCC 14.2는 `add8`은 `vldrb.u8` · `vadd.i8` · `vstrb.8` + `dls`/`le`로 벡터화했지만, **int16 → int32 곱셈-누적(내적)은 벡터화하지 않았다**(`-fopt-info-vec`에 `dot16` 줄이 없고, 역어셈블은 스칼라 `smlabb` + `le`였다). intrinsic 판 `dot16`은 이렇게 나온다.

```asm
10000046:	vctp.16	r2
10000050:	vpst
10000054:	vldrht.u16	q3, [r3], #16
10000058:	vpst
1000005c:	vldrht.u16	q2, [r1], #16
10000060:	subs	r2, #8
10000062:	vpst
10000066:	vmlavat.s16	r0, q3, q2
1000006a:	bne.n	10000046 <dot16+0x6>
```

`vctp.16` + `vpst` + `…t`(then) 접미사가 tail predication이다 — 마지막 바퀴에서 남은 원소 수만큼만 lane이 켜진다. ML · DSP 라이브러리(CMSIS-DSP, CMSIS-NN)가 intrinsic · 어셈블리로 직접 쓰는 이유가 여기 있다.

RTOS 커널 안에서는: Helium의 Q0~Q7은 FPU의 S0~S31(D0~D15)과 **같은 레지스터 파일**이다. 그래서 컨텍스트 스위치는 FPU 컨텍스트를 저장하면 MVE도 같이 저장된다. FreeRTOS v8.1-M 포트(`ARM_CM55`, `ARM_CM85`, `ARM_CM52`, `ARM_STAR_MC3`)는 `configENABLE_MVE`를 **반드시 정의**하게 한다(`#error configENABLE_MVE must be defined in FreeRTOSConfig.h`). 반대로 `ARM_CM33` 포트에서 1로 두면 `#error ... must be left undefined, or defined to 0 for the Cortex-M33`이 난다(예제 10의 diff).

### 4.8 PACBTI — 리턴 주소에 서명하기 (M85 · M52)

직관: 스택 버퍼 오버플로로 저장된 LR을 덮어 원하는 곳으로 리턴하게 만드는 것이 ROP 공격이다. **PAC**는 함수 시작에서 LR · SP와 비밀 키로 서명(PAC 값)을 만들어 R12에 두고 같이 저장했다가, 리턴 직전에 다시 계산해서 다르면 fault를 낸다. **BTI**는 간접 분기(`BLX r3` 같은)가 `BTI` 명령이 있는 주소에만 착지하게 한다.

#### 예제 9 — M85용 PACBTI 빌드를 M55에서 실행

무엇을 확인하는 코드인가: `-mcpu=cortex-m85 -mbranch-protection=standard`로 빌드해서 컴파일러가 넣는 명령을 보고, 그 ELF를 PACBTI가 없는 M55(QEMU)에서 실행해 본다.

```c
/* ex_pac: -mbranch-protection=standard (PACBTI)이 함수에 무엇을 넣는가 — M85용으로 빌드해 M55에서 실행 */
#include "semi.h"
typedef uint32_t (*op_t)(uint32_t);
__attribute__((noinline)) uint32_t twice(uint32_t x) { return x * 2u; }
__attribute__((noinline)) uint32_t apply(op_t f, uint32_t x)   /* 간접 호출 + 리턴 주소 저장(non-leaf) */
{
    uint32_t r = f(x);
    semi_printf("apply: f(%u) = %u\n", x, r);
    return r + 1u;
}
int main(void)
{
    volatile op_t f = twice;                                   /* 간접 분기 대상 → BTI 착지점 */
    uint32_t r = apply(f, 21u);
    semi_printf("PACBTI build ran: result %u\n", r);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'H=/Users/donh/.rtos_scratch/B7; O=/var/tmp/rtos/B7/pac
  CFL="-mbranch-protection=standard" $H/mc/mc.sh m85 $O $H/ex_pac.c &&
  arm-none-eabi-objdump -d --no-show-raw-insn $O/app.elf | awk "/<twice>:/,/^$/;/<apply>:/,/^$/"
  timeout 10 qemu-system-arm -machine mps3-an547 -nographic -monitor none -serial null \
    -semihosting-config enable=on,target=native -kernel $O/app.elf; echo "exit=$?"'
```

```text
10000040 <twice>:
10000040:	bti
10000044:	lsls	r0, r0, #1
10000046:	bx	lr

10000048 <apply>:
10000048:	pacbti	r12, lr, sp
1000004c:	stmdb	sp!, {r4, r5, ip, lr}
10000050:	mov	r3, r0
10000052:	mov	r0, r1
10000054:	mov	r5, r1
10000056:	blx	r3
...
10000066:	ldmia.w	sp!, {r4, r5, ip, lr}
1000006a:	aut	r12, lr, sp
1000006e:	bx	lr

apply: f(21) = 42
PACBTI build ran: result 43
exit=0
```

출력에서 볼 것: (1) 간접 호출 대상 `twice`의 첫 명령이 `bti`, non-leaf 함수 `apply`는 `pacbti r12, lr, sp`로 시작해서 `ip`(=R12, PAC 값)를 LR과 함께 스택에 저장하고, 리턴 전에 `aut r12, lr, sp`로 검증한다. (2) 이 바이너리가 **PACBTI 없는 M55에서도 정상 실행**됐다. `PACBTI` · `AUT` · `BTI`는 NOP 힌트 공간에 인코딩돼 있어서, 기능이 없는 v8.1-M 코어는 NOP으로 실행한다. 그래서 한 바이너리를 M55와 M85 제품에 같이 쓸 수 있다. 단, QEMU는 PACBTI를 구현하지 않으므로 **"서명이 틀리면 fault가 난다"는 쪽은 이 랩에서 보일 수 없다**(실 M85 칩, 예: Renesas RA8에서 CONTROL.PAC_EN을 켜야 한다).

RTOS 커널 안에서는: PAC 키는 코어 레지스터(`PAC_KEY_P_0`~`3`)에 있고, 태스크마다 다른 키를 쓰면 한 태스크에서 훔친 서명이 다른 태스크에서 통하지 않는다. FreeRTOS v8-M 포트에 `configENABLE_PAC` · `configENABLE_BTI`가 있고, 켜면 CONTROL 레지스터의 `PAC_EN`(bit 6) · `UPAC_EN`(bit 7) · `BTI_EN`(bit 4) · `UBTI_EN`(bit 5)를 설정하며(`ARM_CM85_NTZ/non_secure/port.c` 384~387줄), PendSV가 **태스크 전용 PAC 키를 컨텍스트에서 꺼내 키 레지스터에 쓴다**:

```text
ARM_CM85_NTZ/non_secure/portasm.c:103: "   ldmdb r1!, {r2-r5}  \n" /* Read task's dedicated PAC key from the task's context. */
ARM_CM85_NTZ/non_secure/portasm.c:104: "   msr  PAC_KEY_P_0, r2 \n" /* Write the task's dedicated PAC key to the PAC key registers. */
ARM_CM85_NTZ/non_secure/portasm.c:105: "   msr  PAC_KEY_P_1, r3 \n"
```

(grep 결과에서 공백을 줄였다.) PACBTI는 컨텍스트 크기를 키 4워드만큼 늘린다 — "보안 기능 하나가 컨텍스트 스위치 비용을 바꾼다"는 예다.

### 4.9 LOB — 포트가 따로 할 일은 없다

예제 3에서 본 `DLS`/`LE`는 루프 횟수를 LR에 둔다. 인터럽트가 루프 한가운데 들어오면? LR은 예외 진입 때 하드웨어가 스택 프레임(R0~R3, R12, LR, PC, xPSR)에 저장하고, 코어가 기억해 둔 "루프 시작 주소" 캐시(LO_BRANCH_INFO)는 예외 진입 · 복귀 때 무효화된다. 그래서 루프는 복귀 후 `LE`를 만나 다시 정상 동작한다. RTOS 포트는 LOB 때문에 추가로 저장할 것이 없다. Helium의 꼬리 예측 루프(`DLSTP`/`LETP`)는 FPSCR의 LTPSIZE 필드를 쓰는데, 이것은 FPU 컨텍스트의 일부라 §4.7의 FPU/MVE 저장에 포함된다. (이 문단의 LO_BRANCH_INFO 동작은 Armv8.1-M 아키텍처 설명을 기준으로 썼고, 이 랩에서 실행으로 확인하지는 않았다.)

---

## 5. FreeRTOS `portable/GCC` 디렉터리 ↔ 코어

### 5.1 예제 10 — 포트 트리를 직접 열어 보기

무엇을 확인하는 코드인가: FreeRTOS V11.3.2의 `portable/GCC/ARM_CM*` 디렉터리마다 어떤 파일이 있는지, 그리고 v8-M 포트들이 서로 얼마나 다른지 `diff`로 센다.

```sh
limactl shell elinux -- bash -lc 'cd /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC
  for d in ARM_CM*; do echo "== $d: $(find $d -type f | sed "s|$d/||" | tr "\n" " ")"; done'
```

```text
== ARM_CM0: portmacro.h port.c mpu_wrappers_v2_asm.c portasm.c portasm.h
== ARM_CM23: non_secure/portmacro.h non_secure/portmacrocommon.h non_secure/port.c non_secure/mpu_wrappers_v2_asm.c non_secure/portasm.c non_secure/portasm.h secure/secure_heap.h secure/secure_context_port.c secure/secure_init.h secure/secure_heap.c secure/secure_context.h secure/secure_context.c secure/secure_port_macros.h secure/secure_init.c
== ARM_CM23_NTZ: non_secure/portmacro.h non_secure/portmacrocommon.h non_secure/port.c non_secure/mpu_wrappers_v2_asm.c non_secure/portasm.c non_secure/portasm.h
== ARM_CM3: portmacro.h port.c
== ARM_CM33: (ARM_CM23과 같은 파일 구성)
== ARM_CM33_NTZ: (ARM_CM23_NTZ와 같은 파일 구성)
== ARM_CM35P: (같음)
== ARM_CM35P_NTZ: (같음)
== ARM_CM3_MPU: portmacro.h port.c mpu_wrappers_v2_asm.c
== ARM_CM4F: portmacro.h port.c
== ARM_CM4_MPU: portmacro.h port.c mpu_wrappers_v2_asm.c
== ARM_CM52: (같음)
== ARM_CM52_NTZ: (같음)
== ARM_CM55: (같음)
== ARM_CM55_NTZ: (같음)
== ARM_CM7: ReadMe.txt r0p1/portmacro.h r0p1/port.c
== ARM_CM85: (같음)
== ARM_CM85_NTZ: (같음)
```

(v8-M 포트의 파일 목록은 ARM_CM23 · ARM_CM23_NTZ와 한 글자도 다르지 않아서 "(같음)"으로 줄였다. `ARM_STAR_MC3` · `ARM_STAR_MC3_NTZ`도 있다.) 다음으로 v8-M 포트를 `ARM_CM33_NTZ`와 diff해서 다른 줄 수를 센다.

```sh
limactl shell elinux -- bash -lc 'cd /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC
for p in ARM_CM35P_NTZ ARM_CM52_NTZ ARM_CM55_NTZ ARM_CM85_NTZ ARM_CM23_NTZ; do
  for f in port.c portasm.c portmacro.h portmacrocommon.h; do
    n=$(diff ARM_CM33_NTZ/non_secure/$f $p/non_secure/$f | grep -c "^[<>]"); printf "%s/%s:%s  " $p $f $n; done; echo; done
ls ../ARMv8M'
```

```text
ARM_CM35P_NTZ/port.c:0  ARM_CM35P_NTZ/portasm.c:0  ARM_CM35P_NTZ/portmacro.h:6  ARM_CM35P_NTZ/portmacrocommon.h:0
ARM_CM52_NTZ/port.c:0  ARM_CM52_NTZ/portasm.c:0  ARM_CM52_NTZ/portmacro.h:19  ARM_CM52_NTZ/portmacrocommon.h:0
ARM_CM55_NTZ/port.c:0  ARM_CM55_NTZ/portasm.c:0  ARM_CM55_NTZ/portmacro.h:18  ARM_CM55_NTZ/portmacrocommon.h:0
ARM_CM85_NTZ/port.c:0  ARM_CM85_NTZ/portasm.c:0  ARM_CM85_NTZ/portmacro.h:18  ARM_CM85_NTZ/portmacrocommon.h:0
ARM_CM23_NTZ/port.c:0  ARM_CM23_NTZ/portasm.c:472  ARM_CM23_NTZ/portmacro.h:16  ARM_CM23_NTZ/portmacrocommon.h:0
ReadMe.txt
copy_files.py
non_secure
secure
```

출력에서 볼 것: **모든 v8-M 포트의 `port.c`는 바이트 단위로 같다.** 원본은 `portable/ARMv8M/`에 하나 있고 `copy_files.py`가 코어별 디렉터리로 복사한다. 코어마다 다른 것은 `portmacro.h`(아키텍처 이름, `portARMV8M_MINOR_VERSION`, MVE 허용 여부)뿐이고, **M23만 `portasm.c`가 472줄 다르다** — Baseline 명령으로 다시 쓴 어셈블리이기 때문이다. M55와 M33의 `portmacro.h` 차이는 이것이다.

```text
< #define portARCH_NAME                    "Cortex-M33"
---
> #define portARCH_NAME                    "Cortex-M55"
< #define portARMV8M_MINOR_VERSION         0
---
> #define portARMV8M_MINOR_VERSION         1
< #define portVALIDATED_FOR_SMP            1
---
> #define portVALIDATED_FOR_SMP            0
<     #error configENABLE_MVE must be left undefined, or defined to 0 for the Cortex-M33.
```

(`diff ARM_CM33_NTZ/non_secure/portmacro.h ARM_CM55_NTZ/non_secure/portmacro.h`의 핵심 줄만 남겼다.) `portVALIDATED_FOR_SMP`가 M33은 1, M55는 0인 것도 눈여겨볼 만하다. FreeRTOS SMP는 M33 듀얼(RP2350 같은)에서 검증됐다는 뜻이다.

### 5.2 포트 선택표

| 디렉터리 | 쓰는 코어 | 아키텍처 | 크리티컬 섹션 | 컨텍스트에 더 저장하는 것 | 메모 |
|---|---|---|---|---|---|
| `ARM_CM0` | M0, M0+, M1 | v6-M | PRIMASK (`cpsid i`) | — | `configMAX_SYSCALL_INTERRUPT_PRIORITY` 안 씀, R8~R11 옮겨 담기 |
| `ARM_CM3` | M3 | v7-M | BASEPRI | — | 랩 기본 템플릿 |
| `ARM_CM3_MPU` | M3 + MPU 격리 | v7-M | BASEPRI | MPU 영역 설정 | F4 |
| `ARM_CM4F` | M4F, M7(r0p0 · r0p1 제외) | v7E-M | BASEPRI | S16~S31(사용 시) | lazy stacking, M7 r0p0/1이면 assert |
| `ARM_CM4_MPU` | M4F + MPU 격리 | v7E-M | BASEPRI | FPU + MPU | M7 errata 대응은 `configENABLE_ERRATA_837070_WORKAROUND`(커밋 기록 기준) |
| `ARM_CM7/r0p1` | M7 전 리비전 | v7E-M | BASEPRI + `cpsid` 감싸기 | S16~S31(사용 시) | errata 837070 우회 |
| `ARM_CM23` / `_NTZ` | M23 | v8-M Base | PRIMASK | PSPLIM(Secure-only일 때), CONTROL | portasm.c가 다른 v8-M과 다름 |
| `ARM_CM33` / `_NTZ` | M33 | v8-M Main | BASEPRI | PSPLIM, CONTROL, (FPU) | `_NTZ` 없는 쪽은 Secure 컨텍스트 관리 |
| `ARM_CM35P` / `_NTZ` | M35P | v8-M Main | BASEPRI | 위와 같음 | M33과 내용 동일 |
| `ARM_CM55` / `_NTZ` | M55 | v8.1-M | BASEPRI | 위 + MVE(FPU와 공유) | `configENABLE_MVE` 필수 |
| `ARM_CM85` / `_NTZ` | M85 | v8.1-M | BASEPRI | 위 + PAC 키 4워드(켰을 때) | `configENABLE_PAC` · `configENABLE_BTI` |
| `ARM_CM52` / `_NTZ` | M52 | v8.1-M | BASEPRI | M85와 같음 | 2023 코어, 포트는 이미 있음 |
| `ARM_STAR_MC3` / `_NTZ` | Arm China STAR-MC3 | v8.1-M 호환 | BASEPRI | M55류 | Arm Cortex 제품 아님 |

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah7" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">FreeRTOS portable/GCC 포트 고르기</text>
<rect x="225" y="36" width="230" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340.0" y="57.0" font-size="12" text-anchor="middle">CPUID / 데이터시트로 코어 확인</text>
<rect x="20" y="110" width="190" height="46" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="115.0" y="129.0" font-size="12" text-anchor="middle">Armv6-M</text>
<text x="115.0" y="145.0" font-size="12" text-anchor="middle">M0 · M0+ · M1</text>
<rect x="245" y="110" width="190" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340.0" y="129.0" font-size="12" text-anchor="middle">Armv7-M / 7E-M</text>
<text x="340.0" y="145.0" font-size="12" text-anchor="middle">M3 · M4 · M7</text>
<rect x="460" y="110" width="212" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="566.0" y="129.0" font-size="12" text-anchor="middle">Armv8-M / 8.1-M</text>
<text x="566.0" y="145.0" font-size="12" text-anchor="middle">M23 · M33 · M55 · M85 · M52</text>
<line x1="300" y1="70" x2="115" y2="108" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="340" y1="70" x2="340" y2="108" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="380" y1="70" x2="565" y2="108" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="20" y="200" width="190" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="115.0" y="224.0" font-size="12" text-anchor="middle">ARM_CM0</text>
<line x1="115" y1="156" x2="115" y2="198" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="245" y="180" width="190" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340.0" y="201.0" font-size="12" text-anchor="middle">FPU를 쓰나?</text>
<line x1="340" y1="156" x2="340" y2="178" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="230" y="250" width="100" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="280.0" y="271.0" font-size="12" text-anchor="middle">ARM_CM3</text>
<rect x="350" y="250" width="100" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="400.0" y="271.0" font-size="12" text-anchor="middle">ARM_CM4F</text>
<line x1="300" y1="214" x2="280" y2="248" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="236" y="236" font-size="12">아니오</text>
<line x1="380" y1="214" x2="400" y2="248" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="398" y="236" font-size="12">예</text>
<rect x="245" y="310" width="190" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340.0" y="329.0" font-size="12" text-anchor="middle">M7 r0p0 / r0p1이면</text>
<text x="340.0" y="345.0" font-size="12" text-anchor="middle">ARM_CM7/r0p1</text>
<line x1="400" y1="284" x2="380" y2="308" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="470" y="180" width="190" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565.0" y="199.0" font-size="12" text-anchor="middle">Non-secure에서 Secure</text>
<text x="565.0" y="215.0" font-size="12" text-anchor="middle">서비스를 부르나?</text>
<line x1="565" y1="156" x2="565" y2="178" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="470" y="260" width="90" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="515.0" y="279.0" font-size="12" text-anchor="middle">ARM_CMxx</text>
<text x="515.0" y="295.0" font-size="12" text-anchor="middle">+ secure/</text>
<rect x="570" y="260" width="90" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="615.0" y="279.0" font-size="12" text-anchor="middle">ARM_CMxx</text>
<text x="615.0" y="295.0" font-size="12" text-anchor="middle">_NTZ</text>
<line x1="530" y1="226" x2="515" y2="258" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="496" y="248" font-size="12">예</text>
<line x1="600" y1="226" x2="615" y2="258" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="616" y="248" font-size="12">아니오</text>
<rect x="440" y="330" width="232" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="556.0" y="349.0" font-size="11" text-anchor="middle">Secure에서만 돌면</text>
<text x="556.0" y="365.0" font-size="11" text-anchor="middle">configRUN_FREERTOS_SECURE_ONLY=1</text>
<line x1="615" y1="306" x2="590" y2="328" stroke="currentColor" marker-end="url(#ah7)"/>
</svg>
```

그림 6 — FreeRTOS 포트 고르기. 핵심 분기는 세 개다: 아키텍처 계열, FPU 사용 여부(v7-M), TrustZone으로 Secure 서비스를 부르는지(v8-M). v8-M에서 FPU · MVE · PAC는 디렉터리가 아니라 `configENABLE_FPU` · `configENABLE_MVE` · `configENABLE_PAC` 설정으로 고른다.

### 5.3 예제 11 — 같은 FreeRTOS 앱을 6개 코어에서

무엇을 확인하는 코드인가: 태스크 두 개가 direct-to-task notification으로 1,000번 핑퐁(왕복 1회 = 컨텍스트 스위치 2번)하는 앱을 코어별 포트로 빌드해서 돌린다. 크리티컬 섹션 안에서 PRIMASK · BASEPRI를 읽어 "그 포트가 실제로 무엇을 쓰는지" 보고, SysTick 카운터로 왕복 비용을 명령 수로 잰다.

빌드 스크립트는 코어 → 포트 디렉터리 · CPU 클록만 바꾼다. FreeRTOSConfig.h는 정적 할당만 써서 heap이 필요 없게 했다(microbit의 RAM이 16 KB라서).

```sh
#!/bin/bash
# fr.sh <core> <out> <app.c> — 같은 FreeRTOS 앱을 코어별 포트로 빌드(+RUN=1이면 실행)
core=$1; out=$2; app=$3
K=/var/rtoslab/src/FreeRTOS-Kernel; P=$K/portable/GCC; H=/Users/donh/.rtos_scratch/B7
case $core in
 m0p) port=$P/ARM_CM0; hz=16000000;;
 m3)  port=$P/ARM_CM3; hz=25000000;;
 m4)  port=$P/ARM_CM4F; hz=25000000; export FLOAT=-mfloat-abi=hard;;
 m7)  port=$P/ARM_CM7/r0p1; hz=25000000; export FLOAT=-mfloat-abi=hard;;
 m23) port=$P/ARM_CM23_NTZ/non_secure; hz=25000000;;
 m33) port=$P/ARM_CM33_NTZ/non_secure; hz=20000000;;
 m55) port=$P/ARM_CM55_NTZ/non_secure; hz=32000000;;
 m85) port=$P/ARM_CM85_NTZ/non_secure; hz=32000000;;
esac
psrc=$(ls $port/port.c $port/portasm.c 2>/dev/null)
CFL="-DCPU_HZ=${hz}u -I$H/fr -I$K/include -I$port $CFL" $H/mc/mc.sh $core $out $app \
   $K/tasks.c $K/list.c $K/queue.c $psrc
```

```c
/* FreeRTOSConfig.h 발췌 — 코어 차이를 흡수하는 줄들 */
#define configCPU_CLOCK_HZ                      ( CPU_HZ )
#define configTICK_RATE_HZ                      1000
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0
#define configKERNEL_PROVIDED_STATIC_MEMORY     1
#define configKERNEL_INTERRUPT_PRIORITY         255
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    0x20   /* v6-M 포트(ARM_CM0)는 이 값을 쓰지 않는다 */
/* Armv8-M 포트(ARM_CM23/33/55/85 …)가 요구하는 옵션 */
#define configENABLE_MPU                        0
#define configENABLE_TRUSTZONE                  0
#define configRUN_FREERTOS_SECURE_ONLY          1      /* QEMU는 Secure로 부팅 → EXC_RETURN S=1 */
#define configENABLE_FPU                        0
#define configENABLE_MVE                        0
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
```

v6-M · v7-M 포트는 핸들러 이름이 `vPortSVCHandler`처럼 FreeRTOS식이라 `#define`으로 CMSIS 이름에 연결하고, `ARM_CM0`(V11의 새 구조)과 v8-M 포트는 `SVC_Handler` · `PendSV_Handler` · `SysTick_Handler`를 직접 정의한다. 그래서 하네스의 벡터 테이블은 CMSIS 이름만 알면 된다. v8-M 포트에 필요한 옵션을 빠뜨리면 `#error configENABLE_MVE must be defined` 같은 컴파일 에러로 알려 준다.

```c
/* ex_pingpong: 같은 FreeRTOS 앱을 6개 코어에서 — 크리티컬 섹션이 쓰는 마스크와 왕복(컨텍스트 스위치 2회) 비용 */
#include "FreeRTOS.h"
#include "task.h"
#define ROUNDS 1000u
#define SYST_RVR REG32(0xE000E014u)
#define SYST_CVR REG32(0xE000E018u)
static TaskHandle_t hPing, hPong;
static StackType_t sPing[256], sPong[256];
static StaticTask_t tPing, tPong;
static uint32_t now_cyc(void)                  /* tick 수 × reload + SysTick 진행분 = CPU 클록 수 */
{
    uint32_t t, v, r = SYST_RVR + 1u;
    do { t = xTaskGetTickCount(); v = SYST_CVR; } while (t != xTaskGetTickCount());
    return t * r + (r - 1u - v);
}
static void show_mask(void)
{
    uint32_t pm, bp = 0;
    taskENTER_CRITICAL();
    __asm volatile("mrs %0, primask" : "=r"(pm));
#if !__ARM_ARCH_6M__ && !__ARM_ARCH_8M_BASE__
    __asm volatile("mrs %0, basepri" : "=r"(bp));
#endif
    taskEXIT_CRITICAL();
    semi_printf("in critical: PRIMASK=%u BASEPRI=0x%x\n", pm, bp);
}
static void pong(void *arg)
{
    (void)arg;
    for (;;) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); xTaskNotifyGive(hPing); }
}
static void ping(void *arg)
{
    (void)arg;
    uint32_t id = REG32(0xE000ED00u);
    semi_printf("CPUID part=0x%x  CPU_HZ=%u\n", (id >> 4) & 0xFFF, (uint32_t)configCPU_CLOCK_HZ);
    show_mask();
    vTaskDelay(2);
    uint32_t c0 = now_cyc();
    for (uint32_t i = 0; i < ROUNDS; i++) { xTaskNotifyGive(hPong); ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }
    uint32_t dc = now_cyc() - c0;
    uint32_t insn = (uint32_t)((uint64_t)dc * 1000u / (configCPU_CLOCK_HZ / 1000000u)) ; /* icount: 1 insn = 1 ns */
    semi_printf("%u round trips: %u insn total, %u insn per round trip (2 switches)\n",
                ROUNDS, insn, insn / ROUNDS);
    semi_exit(0);
}
int main(void)
{
    hPong = xTaskCreateStatic(pong, "pong", 256, NULL, 2, sPong, &tPong);
    hPing = xTaskCreateStatic(ping, "ping", 256, NULL, 1, sPing, &tPing);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'H=/Users/donh/.rtos_scratch/B7
  for c in m0p m3 m4 m7 m33 m55; do echo "--- $c"; RUN=1 $H/fr/fr.sh $c /var/tmp/rtos/B7/fr/$c $H/fr/ex_pingpong.c; done
  for c in m23 m85; do $H/fr/fr.sh $c /var/tmp/rtos/B7/fr/$c $H/fr/ex_pingpong.c; done'
```

```text
--- m0p
CPUID part=0xc20  CPU_HZ=16000000
in critical: PRIMASK=1 BASEPRI=0x0
1000 round trips: 601062 insn total, 601 insn per round trip (2 switches)
--- m3
CPUID part=0xc23  CPU_HZ=25000000
in critical: PRIMASK=0 BASEPRI=0x20
1000 round trips: 555040 insn total, 555 insn per round trip (2 switches)
--- m4
CPUID part=0xc24  CPU_HZ=25000000
in critical: PRIMASK=0 BASEPRI=0x20
1000 round trips: 569040 insn total, 569 insn per round trip (2 switches)
--- m7
CPUID part=0xc27  CPU_HZ=25000000
in critical: PRIMASK=0 BASEPRI=0x20
1000 round trips: 587040 insn total, 587 insn per round trip (2 switches)
--- m33
CPUID part=0xd21  CPU_HZ=20000000
in critical: PRIMASK=0 BASEPRI=0x20
1000 round trips: 585050 insn total, 585 insn per round trip (2 switches)
--- m55
CPUID part=0xd22  CPU_HZ=32000000
in critical: PRIMASK=0 BASEPRI=0x20
1000 round trips: 585000 insn total, 585 insn per round trip (2 switches)
```

출력에서 볼 것: (1) **같은 앱 소스가 포트 디렉터리만 바꿔 6개 코어에서 돌았다**(M23 · M85도 경고 없이 빌드됨). 커널 소스는 한 줄도 안 바꿨다. (2) 크리티컬 섹션 안에서 M0+는 PRIMASK=1, 나머지는 BASEPRI=0x20 — 그림 5가 실제로 그렇다. (3) 왕복 비용은 555~601 명령으로 **세대 차이가 10% 이내**다. M0+가 가장 많은 것은 PendSV의 R8~R11 옮겨 담기(§4.3), M4F는 FPU 사용 여부 검사, M7 r0p1 포트는 `cpsid`/`cpsie` 추가, v8-M은 PSPLIM · CONTROL 저장 때문이다. 커널 공통 코드(`xTaskNotifyGive` → `vTaskSwitchContext`)가 대부분을 차지하므로 코어보다 **API 선택**(알림 vs 세마포어 vs 큐 — D 모듈)이 비용을 더 크게 바꾼다.

포트 핸들러 크기도 뽑아 봤다(`nm -S`, 바이트).

```text
core   text PendSV    SVC SysTick EnterCrit
m0p    5100     64     32     36     24
m3     4552     68     36     44     76
m4     4832     92     32     44     76
m7     4684     96     32     48     80
m23    5040     70     24     36     26
m33    4692     68     18     36     28
m55    4736     68     18     36     28
m85    4736     68     18     36     28
```

출력에서 볼 것: PendSV는 M4F(92) · M7 r0p1(96)이 FPU 처리 때문에 가장 크다. M55 · M85가 M33과 똑같은 것은 §5.1의 "port.c · portasm.c가 같은 파일"이라는 결과와 맞는다(이 빌드는 FPU · MVE · PAC를 끈 상태). v6-M 계열(m0p 5,100 · m23 5,040)의 전체 크기가 가장 큰 것은 커널 C 코드 전체가 Thumb-1 위주로 길어지기 때문이다.

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
<text x="320.0" y="22" font-size="14" text-anchor="middle" font-weight="bold">FreeRTOS 알림 왕복 1회(컨텍스트 스위치 2번)의 명령 수</text>
<line x1="70" y1="260.0" x2="620" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="264.0" font-size="12" text-anchor="end">0</text>
<line x1="70" y1="230.0" x2="620" y2="230.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="234.0" font-size="12" text-anchor="end">100</text>
<line x1="70" y1="200.0" x2="620" y2="200.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="204.0" font-size="12" text-anchor="end">200</text>
<line x1="70" y1="170.0" x2="620" y2="170.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="174.0" font-size="12" text-anchor="end">300</text>
<line x1="70" y1="140.0" x2="620" y2="140.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="144.0" font-size="12" text-anchor="end">400</text>
<line x1="70" y1="110.0" x2="620" y2="110.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="114.0" font-size="12" text-anchor="end">500</text>
<line x1="70" y1="80.0" x2="620" y2="80.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="84.0" font-size="12" text-anchor="end">600</text>
<line x1="70" y1="50.0" x2="620" y2="50.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="62" y="54.0" font-size="12" text-anchor="end">700</text>
<line x1="70" y1="260" x2="620" y2="260" stroke="currentColor"/>
<rect x="90.8" y="79.7" width="50" height="180.3" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="115.8" y="74.7" font-size="12" text-anchor="middle">601</text>
<text x="115.8" y="278" font-size="13" text-anchor="middle">M0+</text>
<text x="115.8" y="294" font-size="12" text-anchor="middle">ARM_CM0</text>
<rect x="182.5" y="93.5" width="50" height="166.5" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="207.5" y="88.5" font-size="12" text-anchor="middle">555</text>
<text x="207.5" y="278" font-size="13" text-anchor="middle">M3</text>
<text x="207.5" y="294" font-size="12" text-anchor="middle">ARM_CM3</text>
<rect x="274.2" y="89.3" width="50" height="170.7" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="299.2" y="84.3" font-size="12" text-anchor="middle">569</text>
<text x="299.2" y="278" font-size="13" text-anchor="middle">M4F</text>
<text x="299.2" y="294" font-size="12" text-anchor="middle">ARM_CM4F</text>
<rect x="365.8" y="83.9" width="50" height="176.1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="390.8" y="78.9" font-size="12" text-anchor="middle">587</text>
<text x="390.8" y="278" font-size="13" text-anchor="middle">M7</text>
<text x="390.8" y="294" font-size="12" text-anchor="middle">CM7/r0p1</text>
<rect x="457.5" y="84.5" width="50" height="175.5" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="482.5" y="79.5" font-size="12" text-anchor="middle">585</text>
<text x="482.5" y="278" font-size="13" text-anchor="middle">M33</text>
<text x="482.5" y="294" font-size="12" text-anchor="middle">CM33_NTZ</text>
<rect x="549.2" y="84.5" width="50" height="175.5" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="574.2" y="79.5" font-size="12" text-anchor="middle">585</text>
<text x="574.2" y="278" font-size="13" text-anchor="middle">M55</text>
<text x="574.2" y="294" font-size="12" text-anchor="middle">CM55_NTZ</text>
<text x="320.0" y="318" font-size="12" text-anchor="middle">QEMU icount(1 명령 = 1 ns) 기준 — 사이클이 아니라 실행 명령 수다</text>
</svg>
```

그림 7 — FreeRTOS 알림 왕복 1회(컨텍스트 스위치 2번)의 실행 명령 수. 6개 코어 모두 같은 소스, 포트만 다르다. QEMU icount 기준.

---

## 6. Zephyr — 보드 이름과 Kconfig가 코어를 고른다

Zephyr에서는 포트를 직접 고르지 않는다. **보드 이름**을 고르면 devicetree의 `compatible = "arm,cortex-m33"`과 SoC Kconfig가 `CONFIG_CPU_CORTEX_M33` → `CONFIG_ARMV8_M_MAINLINE` → `CONFIG_ARMV7_M_ARMV8_M_MAINLINE` · `CONFIG_CPU_CORTEX_M_HAS_BASEPRI` 같은 기호를 연쇄로 켠다(H 모듈에서 Kconfig 상세).

v4.4.2 소스(`boards/arm/mps2/board.yml`, `boards/arm/mps3/board.yml`)에서 확인한 Arm MPS 보드 이름이다.

| Zephyr 보드 (v4.4.2) | 코어 | QEMU에서 실행 | 메모 |
|---|---|---|---|
| `mps2/an383` | M0+ | 안 됨 (FVP만: `FVP_MPS2_Cortex-M0plus`) | QEMU에 an383 머신 없음 |
| `mps2/an385` | M3 | `mps2-an385` | 랩 기본 |
| `mps2/an386` | M4F | `mps2-an386` | |
| `mps2/an500` | M7 | `mps2-an500` (rtos-zrun은 실행 안 함, QEMU 직접 실행) | Zephyr 보드는 FPU 없이 구성 |
| `mps2/an521/cpu0`, `mps2/an521/cpu0/ns`, `mps2/an521/cpu1` | M33 × 2 | `mps2-an521` | an505는 Zephyr 보드 없음, `/ns` = TF-M 위 Non-secure |
| `mps3/corstone300/an547`, `.../an547/ns` | M55 | `mps3-an547` | an552 · fvp 변형도 있음 |
| `mps3/corstone310/an555`, `.../an555/ns` | M85 | 안 됨 (FPGA · FVP) | QEMU에 M85 없음 |

`CONFIG_CPU_CORTEX_M52`도 `arch/arm/core/cortex_m/Kconfig` 66줄에 있다(`select ARMV8_1_M_MAINLINE`, `select CPU_HAS_DCACHE` …). 반면 M35P용 기호는 이 Kconfig에서 찾지 못했다.

### 6.1 예제 12 — 같은 Zephyr 앱을 5개 보드에서

무엇을 확인하는 코드인가: 보드만 바꿔 빌드한 같은 앱이 실행 중에 CPUID와 "Kconfig가 켠 기능"을 출력한다. `prj.conf`에는 `CONFIG_HW_STACK_PROTECTION=y`를 넣어서, 코어마다 어떤 스택 보호 수단이 골라지는지 본다.

```c
/* B7 Zephyr: 같은 앱을 보드(코어)만 바꿔 빌드 — Kconfig가 코어 기능을 어떻게 고르는지 출력 */
#include <zephyr/kernel.h>
#include <cmsis_core.h>
int main(void)
{
	printk("board %s  CPUID part 0x%x\n", CONFIG_BOARD_TARGET, (unsigned)((SCB->CPUID >> 4) & 0xFFF));
	printk("  ARMV6_M_ARMV8_M_BASELINE=%d ARMV7_M_ARMV8_M_MAINLINE=%d ARMV8_M_SE=%d ARMV8_1_M=%d\n",
	       IS_ENABLED(CONFIG_ARMV6_M_ARMV8_M_BASELINE), IS_ENABLED(CONFIG_ARMV7_M_ARMV8_M_MAINLINE),
	       IS_ENABLED(CONFIG_ARMV8_M_SE), IS_ENABLED(CONFIG_ARMV8_1_M_MAINLINE));
	printk("  CPU_HAS_FPU=%d MVE=%d  stack guard: BUILTIN(PSPLIM)=%d MPU=%d\n",
	       IS_ENABLED(CONFIG_CPU_HAS_FPU), IS_ENABLED(CONFIG_ARMV8_1_M_MVEI),
	       IS_ENABLED(CONFIG_BUILTIN_STACK_GUARD), IS_ENABLED(CONFIG_MPU_STACK_GUARD));
	return 0;
}
```

```sh
# zrun.sh: 보드 5개를 돌며 rtos-zbuild + rtos-zrun (an500은 QEMU를 직접 실행)
for b in mps2/an385 mps2/an386 mps2/an500 mps2/an521/cpu0 mps3/corstone300/an547; do
  d=/var/tmp/rtos/B7/z/$(echo $b | tr / _)
  echo "=== $b"; /var/rtoslab/bin/rtos-zbuild $b /Users/donh/.rtos_scratch/B7/zapp $d 2>&1 | grep -E "FLASH|RAM|warning|error" | head -4
  /var/rtoslab/bin/rtos-zrun $d 5 2>&1 | grep -E "board|ARMV|FPU|Booting"
done
timeout 5 qemu-system-arm -machine mps2-an500 -nographic -monitor none -serial stdio -kernel /var/tmp/rtos/B7/z/mps2_an500/zephyr/zephyr.elf
```

```text
=== mps2/an385
           FLASH:       12996 B         4 MB      0.31%
             RAM:        6336 B         4 MB      0.15%
*** Booting Zephyr OS build v4.4.2 ***
board mps2/an385  CPUID part 0xc23
  ARMV6_M_ARMV8_M_BASELINE=0 ARMV7_M_ARMV8_M_MAINLINE=1 ARMV8_M_SE=0 ARMV8_1_M=0
  CPU_HAS_FPU=0 MVE=0  stack guard: BUILTIN(PSPLIM)=0 MPU=1
=== mps2/an386
warning: HW_STACK_PROTECTION (defined at soc/nxp/s32/s32k1/Kconfig.defconfig:20, arch/Kconfig:309)
           FLASH:       17532 B         4 MB      0.42%
             RAM:        6352 B         4 MB      0.15%
*** Booting Zephyr OS build v4.4.2 ***
board mps2/an386  CPUID part 0xc24
  ARMV6_M_ARMV8_M_BASELINE=0 ARMV7_M_ARMV8_M_MAINLINE=1 ARMV8_M_SE=0 ARMV8_1_M=0
  CPU_HAS_FPU=1 MVE=0  stack guard: BUILTIN(PSPLIM)=0 MPU=0
=== mps2/an500
warning: HW_STACK_PROTECTION (defined at soc/nxp/s32/s32k1/Kconfig.defconfig:20, arch/Kconfig:309)
           FLASH:       11976 B         4 MB      0.29%
             RAM:        6064 B         4 MB      0.14%
=== mps2/an521/cpu0
           FLASH:       14940 B         4 MB      0.36%
             RAM:        6208 B         4 MB      0.15%
*** Booting Zephyr OS build v4.4.2 ***
board mps2/an521/cpu0  CPUID part 0xd21
  ARMV6_M_ARMV8_M_BASELINE=0 ARMV7_M_ARMV8_M_MAINLINE=1 ARMV8_M_SE=1 ARMV8_1_M=0
  CPU_HAS_FPU=0 MVE=0  stack guard: BUILTIN(PSPLIM)=1 MPU=0
=== mps3/corstone300/an547
           FLASH:       15656 B       512 KB      2.99%
             RAM:        4224 B       512 KB      0.81%
*** Booting Zephyr OS build v4.4.2 ***
board mps3/corstone300/an547  CPUID part 0xd22
  ARMV6_M_ARMV8_M_BASELINE=0 ARMV7_M_ARMV8_M_MAINLINE=1 ARMV8_M_SE=1 ARMV8_1_M=1
  CPU_HAS_FPU=1 MVE=1  stack guard: BUILTIN(PSPLIM)=1 MPU=0
--- (an500을 QEMU로 직접 실행)
*** Booting Zephyr OS build v4.4.2 ***
board mps2/an500  CPUID part 0xc27
  ARMV6_M_ARMV8_M_BASELINE=0 ARMV7_M_ARMV8_M_MAINLINE=1 ARMV8_M_SE=0 ARMV8_1_M=0
  CPU_HAS_FPU=0 MVE=0  stack guard: BUILTIN(PSPLIM)=0 MPU=0
```

(an547의 SRAM · ISRAM 0 B 줄은 생략했다. `warning:` 줄은 빌드 스크립트의 grep이 Kconfig 경고의 첫 줄만 잘라 보인 것이다.) 출력에서 볼 것:

- **M33 · M55 보드는 `HW_STACK_PROTECTION`이 PSPLIM(`BUILTIN_STACK_GUARD=1`)으로** 풀렸다. 예제 7의 하드웨어 기능을 Zephyr가 그대로 쓴다.
- **M3(an385)는 MPU guard**(`MPU_STACK_GUARD=1`)로 풀렸다. PSPLIM이 없으니 MPU 영역 하나를 스택 바닥에 둔다.
- **an386 · an500은 Kconfig 경고와 함께 스택 보호가 꺼졌다**(BUILTIN=0, MPU=0). 이 두 보드 정의는 MPU 드라이버를 켜지 않아서 `HW_STACK_PROTECTION`의 의존성을 못 채운 것이다 — "코어에 MPU가 있다"와 "보드 정의가 그것을 쓴다"는 다른 문제다. 실제 제품 보드에서는 반드시 `.config`에서 확인한다.
- an500 보드는 M7인데 `CPU_HAS_FPU=0`이다. Zephyr의 이 보드 정의가 FPU 없는 M7으로 구성돼 있다는 뜻이다(QEMU M7 자체는 FPU가 있다). 코어 옵션은 **칩(여기서는 보드 정의)마다** 다르다는 그림 2의 교훈이 그대로 보인다.
- 같은 앱의 FLASH 크기가 M4F(17,532 B)에서 가장 큰 것은 FPU 컨텍스트 처리 코드가 붙어서다.

M55 보드의 `.config`에서 코어 관련 기호를 뽑으면 이렇다.

```text
CONFIG_BUILTIN_STACK_GUARD=y
CONFIG_CPU_CORTEX_M55=y
CONFIG_CPU_CORTEX_M_HAS_BASEPRI=y
CONFIG_ARMV8_M_DSP=y
CONFIG_ARMV8_1_M_MVEI=y
CONFIG_ARMV8_1_M_MVEF=y
CONFIG_ARMV8_1_M_PMU=y
CONFIG_CPU_HAS_FPU=y
```

`CONFIG_CPU_CORTEX_M_HAS_BASEPRI`는 FreeRTOS의 "ARM_CM0냐 ARM_CM3냐"에 해당하는 Zephyr 쪽 스위치다. an385 · an386 · an500의 `.config`에서도 `CONFIG_CPU_CORTEX_M_HAS_BASEPRI=y`를 확인했다.

---

## 7. RTOS 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 660 318" xmlns="http://www.w3.org/2000/svg"><text x="330" y="20" font-size="14" text-anchor="middle" font-weight="bold">코어 차이가 갇혀 있는 곳 — 포트 층</text><rect x="20" y="34" width="620" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="330" y="57" font-size="13" text-anchor="middle">앱 태스크: xTaskCreate · xQueueSend · vTaskDelay (코어 무관)</text><rect x="20" y="86" width="620" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="330" y="105" font-size="13" text-anchor="middle">공통 커널: tasks.c · queue.c · list.c — TCB_t, pxReadyTasksLists</text><text x="330" y="122" font-size="12" text-anchor="middle">Zephyr: kernel/sched.c · kernel/thread.c (z_swap 앞까지)</text><rect x="20" y="146" width="200" height="66" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="120" y="170" font-size="13" text-anchor="middle">portmacro.h</text><text x="120" y="188" font-size="12" text-anchor="middle">크리티컬 섹션 · 타입</text><text x="120" y="204" font-size="12" text-anchor="middle">PRIMASK 또는 BASEPRI</text><rect x="230" y="146" width="200" height="66" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="330" y="170" font-size="13" text-anchor="middle">port.c</text><text x="330" y="188" font-size="12" text-anchor="middle">첫 스택 프레임 · SysTick</text><text x="330" y="204" font-size="12" text-anchor="middle">EXC_RETURN · FPCCR · PAC</text><rect x="440" y="146" width="200" height="66" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="540" y="170" font-size="13" text-anchor="middle">portasm.c</text><text x="540" y="188" font-size="12" text-anchor="middle">PendSV · SVC 어셈블리</text><text x="540" y="204" font-size="12" text-anchor="middle">R4-R11 · PSPLIM · S16-S31</text><rect x="20" y="228" width="620" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="330" y="247" font-size="13" text-anchor="middle">코어 하드웨어: PRIMASK · BASEPRI · PSPLIM · FPU/MVE 레지스터 · PAC 키 · CONTROL</text><text x="330" y="264" font-size="12" text-anchor="middle">v6-M · v7-M · v7E-M · v8-M Base/Main · v8.1-M 마다 다름</text><text x="330" y="300" font-size="12" text-anchor="middle">FreeRTOS: portable/GCC/ARM_CMx/  ·  Zephyr: arch/arm/core/cortex_m/ + include/zephyr/arch/arm/</text></svg>
```

그림 8 — 코어 차이가 갇혀 있는 곳. 위 두 층(앱, 공통 커널)은 코어를 모른다. 초록 상자 세 개(포트)가 아래 주황 상자(코어 하드웨어)의 차이를 흡수한다.

경로로 정리하면:

- **크리티컬 섹션**: FreeRTOS `taskENTER_CRITICAL()` → `portENTER_CRITICAL()` → `vPortEnterCritical()`(`ARM_CMx/port.c`) → `portDISABLE_INTERRUPTS()`(`portmacro.h`: `cpsid i` 또는 `vPortRaiseBASEPRI()`). Zephyr `irq_lock()` → `arch_irq_lock()`(`include/zephyr/arch/arm/asm_inline_gcc.h:44`, `CONFIG_ARMV6_M_ARMV8_M_BASELINE`로 분기). 구현 상세는 C7.
- **컨텍스트 스위치**: `portYIELD()`가 ICSR.PENDSVSET을 세움 → `PendSV_Handler`(v6-M · v8-M은 `portasm.c`, v7-M은 `port.c`의 `xPortPendSVHandler`) → R4~R11(+ PSPLIM, CONTROL, FPU, PAC 키) 저장 → `vTaskSwitchContext()`(`tasks.c`, 공통) → 복원. Zephyr는 `arch/arm/core/cortex_m/swap_helper.S` 계열과 `arm-m-switch.c`(PSPLIM 필드가 48줄에 있다). 상세는 C3 · C4.
- **첫 태스크 시작**: `vTaskStartScheduler()` → `xPortStartScheduler()`(port.c: SysTick · PendSV 우선순위 설정, FPU면 FPCCR, PAC면 CONTROL) → SVC 또는 직접 복원으로 첫 태스크. Zephyr는 `arch/arm/core/cortex_m/reset.S:102`에서 `msr PSPLIM, r0`로 초기 스택 한계부터 설정한다.
- **스택 프레임 초기화**: `pxPortInitialiseStack()`이 코어별 EXC_RETURN(`portINITIAL_EXC_RETURN`: 0xFFFFFFFD 또는 0xFFFFFFBC)과 PSPLIM 초기값을 첫 프레임에 넣는다.
- **코어 감지**: 빌드 시에는 GCC가 정의하는 `__ARM_ARCH_6M__`, `__ARM_ARCH_7M__`, `__ARM_ARCH_7EM__`, `__ARM_ARCH_8M_BASE__`, `__ARM_ARCH_8M_MAIN__`, `__ARM_ARCH_8_1M_MAIN__`, `__ARM_FEATURE_MVE`, `__ARM_FP` 매크로(이 노트의 예제가 쓴 것), 실행 시에는 CPUID(`ARM_CM4F`의 M7 r0p1 assert)와 ID_ISAR5(`ARM_CM85` 포트가 PACBTI 구현 여부 확인, port.c 382줄의 `portID_ISAR5_REG` 0xE000ED74).

---

## 8. 실무에서 다시 보기

### 8.1 드론 비행 제어기 — M7(STM32H7)에서 M55(STM32N6)로 옮긴다면

예를 들어 드론 비행 제어기가 STM32H7(M7, 480~600 MHz)에서 돌고 있고, 다음 세대에 카메라 기반 장애물 회피를 넣으려고 STM32N6(M55 800 MHz + NPU)을 검토한다고 하자. 포팅 체크리스트는 이렇다.

- FreeRTOS 포트: `ARM_CM4F` 또는 `ARM_CM7/r0p1` → `ARM_CM55_NTZ`(TrustZone 안 쓰면) 또는 `ARM_CM55`. `FreeRTOSConfig.h`에 `configENABLE_FPU` · `configENABLE_MVE` · `configENABLE_MPU` · `configENABLE_TRUSTZONE` · `configRUN_FREERTOS_SECURE_ONLY`를 반드시 정의한다(빠지면 `#error`).
- 크리티컬 섹션: 둘 다 BASEPRI → `configMAX_SYSCALL_INTERRUPT_PRIORITY` 설계를 그대로 가져간다. 단 NVIC 우선순위 비트 수는 칩마다 다르니(STM32는 보통 4비트) 다시 확인한다(B3).
- 스택: M55에서 PSPLIM이 생긴다. 지금까지 "가끔 깨지던" 태스크가 포팅 직후 STKOF UsageFault로 멈출 수 있다 — 원래 있던 버그가 드러난 것이다. Fault 핸들러에 CFSR bit 20 해독을 넣어 둔다(M1).
- DSP 코드: CMSIS-DSP의 M7용 경로(SIMD `SMLAD`)가 M55에서는 Helium 경로로 바뀐다. 자동 벡터화에 기대지 말고(예제 8: GCC가 int16 내적을 벡터화하지 않았다) 라이브러리나 intrinsic을 쓴다.
- startup: CPACR을 `.data` 복사 전에 켠다(예제 8의 NOCP fault).
- 성능 기대치: CoreMark/MHz는 M7 5.29 > M55 4.40이다. 클록이 800 MHz로 올라서 절대 성능은 늘지만, rate loop 같은 스칼라 제어 코드의 클록당 성능은 오히려 떨어진다.

### 8.2 로봇 관절 모터 컨트롤러 — M4F의 FOC 루프와 zero-latency 인터럽트

FOC 전류 루프(20 kHz, 50 µs 주기)는 ADC 완료 인터럽트에서 돈다. 이 ISR을 BASEPRI 문턱(`configMAX_SYSCALL_INTERRUPT_PRIORITY`)보다 **높은** 우선순위(숫자 작게)에 두면 RTOS 크리티컬 섹션에 막히지 않는다 — 그림 5의 "모터 PWM" 칸이다. 대가는 그 ISR에서 `xQueueSendFromISR` 같은 API를 못 부른다는 것이다. 데이터는 락 없는 버퍼로 넘긴다. 이 설계는 **M0+로는 불가능**하다. 원가 절감으로 M0+ 칩(예: STM32G0)으로 내리자는 제안이 오면 "FreeRTOS ARM_CM0 포트는 PRIMASK로 전부 막으니, 커널 크리티컬 섹션 길이(수 µs)가 그대로 전류 루프 지터가 된다"고 근거를 대며 반대할 수 있다.

### 8.3 센서 허브 · 무선 칩 — M33 TrustZone과 두 개의 펌웨어

nRF54L(M33 128 MHz) 같은 칩에서 Zephyr를 쓰면 보드 이름이 `nrf54l15dk/nrf54l15/cpuapp`(Secure)와 `nrf54l15dk/nrf54l15/cpuapp/ns`(Non-secure + TF-M)로 갈린다(v4.4.2 `boards/nordic/nrf54l15dk/`에서 확인). 앞은 이 노트의 QEMU 예제처럼 Secure-only, 뒤는 Secure 쪽에 TF-M이라는 별도 펌웨어가 있고 RTOS는 Non-secure에서 돈다. 면접에서 "TrustZone 써 봤나?"에 답하려면 "RTOS가 어느 쪽에서 도는지, 포트가 `_NTZ`인지"부터 말한다(F6).

### 8.4 멀티코어 M0+ — RP2040에서 락 만들기

RP2040은 M0+ 두 개다. LDREX/STREX가 없고(예제 6), PRIMASK는 자기 코어만 막는다(Zephyr의 `#error "Cortex-M0 and Cortex-M0+ require SoC specific support for cross core synchronisation."`). 그래서 Raspberry Pi SDK와 FreeRTOS SMP RP2040 포트는 SIO 블록의 **하드웨어 스핀락**(읽으면 획득, 쓰면 해제)을 쓴다. 후속 RP2350(M33 두 개)은 LDREX/STREX가 있는 v8-M Mainline이라 일반적인 원자 연산을 쓸 수 있다. 같은 회사의 두 칩이 세대 하나 차이로 락 구현이 완전히 달라지는 예다.

### 8.5 Don의 SSD 컨트롤러와 비교

SSD 컨트롤러의 메인 코어는 보통 Cortex-R(R5 · R8 · R52)이고, 보조 코어로 M 계열이 들어가기도 한다. Cortex-R은 GIC 기반 인터럽트, CPSR의 I비트로 크리티컬 섹션, 캐시 · TCM이 기본이라 Cortex-M과 포트 구조가 다르다(FreeRTOS `ARM_CR5`, `ARM_CRx_No_GIC`, `ARM_CR82` — B9). "M 프로파일은 NVIC와 하드웨어 자동 스태킹 덕분에 PendSV 하나로 컨텍스트 스위치가 끝나고, R 프로파일은 IRQ 모드에서 레지스터를 직접 저장해야 한다"는 대비가 면접에서 경험을 살리는 좋은 연결 고리다.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| M4 코드를 M0+로 옮기면서 BASEPRI 크리티컬 섹션을 그대로 둠 | 빌드 · 실행은 정상, 가끔 리스트 · 버퍼 깨짐 | v6-M에서 `msr basepri`가 무시됨(예제 5) | PRIMASK로 바꾸거나 RTOS의 `taskENTER_CRITICAL()`만 쓴다 |
| v6-M에서 `__atomic_*`, `stdatomic.h` 사용 | `undefined reference to __atomic_fetch_add_4` | LDREX/STREX 없음, libatomic 없음(예제 6) | PRIMASK 감싸기 함수 직접 제공, 또는 설계를 SPSC로 |
| M7 r0p1 칩에 `ARM_CM4F` 포트 | 시작 직후 `configASSERT` 정지 | 포트가 CPUID로 r0p0/r0p1을 거부 | `ARM_CM7/r0p1` 포트 사용 |
| FPU 칩에서 하드 플로트 빌드 + `ARM_CM3` 포트 | 태스크끼리 float 값이 섞임, 간헐적 | S16~S31을 저장 안 함 | `ARM_CM4F`(v7-M) 또는 `configENABLE_FPU=1`(v8-M) |
| Secure로 부팅하는 M33에 NTZ 포트, `configRUN_FREERTOS_SECURE_ONLY=0` | 첫 태스크 진입에서 fault | EXC_RETURN이 Non-secure 복귀(0xFFFFFFBC) | Secure-only면 1로 설정 |
| Helium · FPU 빌드에서 CPACR을 늦게 켬 | 리셋 직후 HardFault, CFSR=0x00080000(NOCP) | 컴파일러가 startup 루프를 벡터화(예제 8) | CPACR을 `.data` 복사보다 먼저, 또는 startup을 `-O1`로 |
| v8-M 포팅 후 "새 fault"를 포트 버그로 오해 | UsageFault, CFSR=0x00100000(STKOF) | PSPLIM이 원래 있던 스택 넘침을 잡음(예제 7) | 스택 크기 재산정, high-water mark 측정 |
| "M55가 M7보다 신형이니 더 빠르다" | 스칼라 제어 루프가 기대보다 느림 | CoreMark/MHz M55 4.40 < M7 5.29 | DSP · ML만 Helium 이득, 제어 루프는 클록 · 캐시로 판단 |
| 자동 벡터화를 믿고 DSP 루프를 C로만 작성 | M55에서 성능이 M4 수준 | GCC 14가 int16 MAC 루프를 벡터화 안 함 | CMSIS-DSP/NN 또는 `arm_mve.h` intrinsic |
| 듀얼 M0+에서 PRIMASK로 코어 간 락 | 두 코어가 동시에 진입 | PRIMASK는 자기 코어만 막음 | SoC 하드웨어 스핀락(RP2040 SIO) |

---

## 10. 면접에서 이렇게 말한다

**Q.** "Why does the FreeRTOS Cortex-M0 port disable all interrupts in a critical section, while the Cortex-M3 port doesn't?"

**A.** Armv6-M에는 BASEPRI가 없어서 PRIMASK로 전부 막을 수밖에 없다. v7-M부터는 BASEPRI로 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이하만 막고, 그보다 높은 인터럽트는 커널과 무관하게 계속 돈다. 그래서 M0 계열에서는 커널 크리티컬 섹션 길이가 모든 인터럽트의 지연에 더해진다.

> Armv6-M has no BASEPRI register, so the only way to mask interrupts is PRIMASK, which blocks everything except NMI and HardFault. From Armv7-M on, the port raises BASEPRI to configMAX_SYSCALL_INTERRUPT_PRIORITY, so interrupts above that threshold keep running with zero kernel-induced latency — they just can't call FreeRTOS APIs. On an M0+ every kernel critical section adds directly to worst-case interrupt latency. I verified this by reading PRIMASK and BASEPRI inside taskENTER_CRITICAL on six QEMU cores: PRIMASK=1 on the M0, BASEPRI=0x20 on the others.

**꼬리 질문**: "What happens if you write BASEPRI on an M0?" → 어셈블은 되지만 무시된다(예제 5). 아무 경고 없이 크리티컬 섹션이 사라진다.

**Q.** "What did Armv8-M change for an RTOS port?"

**A.** 세 가지. 스택 한계 레지스터(PSPLIM · MSPLIM)로 스택 넘침을 하드웨어가 즉시 잡는다. TrustZone으로 Secure/Non-secure 컨텍스트가 생겨 포트가 두 벌(`ARM_CM33`, `ARM_CM33_NTZ`)이 됐다. MPU 모델이 PMSAv8(시작/끝 주소)로 바뀌어 영역 정렬 제약이 줄었다. 그리고 Baseline/Mainline 분리 때문에 M23은 여전히 BASEPRI가 없다.

> Three things. First, stack-limit registers: PSPLIM and MSPLIM fault on the exact instruction that overflows, so the port saves PSPLIM per task — I demoed a 512-byte task stack overflowing silently on an M3 but trapping with CFSR STKOF on M33 and M55. Second, TrustZone: FreeRTOS ships a TrustZone-aware port with secure-side context management and an NTZ port, and you need configRUN_FREERTOS_SECURE_ONLY when the whole RTOS runs secure. Third, the PMSAv8 MPU uses base and limit addresses instead of power-of-two regions. And note the Baseline/Mainline split — M23 still has no BASEPRI.

**Q.** "M7, M33, or M55 for a drone flight controller?"

**A.** 스칼라 제어 루프만 보면 M7이 클록당 가장 빠르다(CoreMark/MHz 5.29). M33은 TrustZone · 저전력이 필요한 보조 MCU에, M55는 비전 · ML 같은 벡터 연산이 있을 때 Helium과 NPU 때문에 고른다. 결정 기준은 워크로드, 보안 요구, 그리고 캐시 관리 부담이다.

> It depends on the workload. For pure scalar control loops the M7 still has the best per-clock performance, about 5.3 CoreMark per MHz, and parts like the STM32H7 run at 480 to 600 MHz. The M33 fits a secondary MCU where TrustZone and low power matter. The M55 makes sense when you add vision or ML, because Helium and an attached NPU — like on the STM32N6 at 800 MHz — accelerate that, even though its scalar per-clock performance is lower than the M7. Either way M7 and M55 have caches, so DMA coherency becomes part of the design.

**Q.** "How do you do atomic operations on a Cortex-M0+?"

**A.** LDREX/STREX가 없어서 단일 코어면 PRIMASK로 짧게 감싼다. 멀티코어 M0+(RP2040)는 PRIMASK가 자기 코어만 막으니 SoC 하드웨어 스핀락을 쓴다. 가능하면 SPSC 링버퍼처럼 RMW 자체가 필요 없게 설계한다.

> Armv6-M has no exclusive load/store, so GCC can't inline atomics and you get an undefined __atomic_fetch_add_4 at link time. On a single core I wrap the read-modify-write in a short PRIMASK critical section. On a dual M0+ like the RP2040 that isn't enough, because PRIMASK only masks the local core, so you use the SIO hardware spinlocks. Best of all, design it away: an SPSC ring buffer where each index has a single writer needs no RMW at all.

**Q.** "Which FreeRTOS port would you use on a Cortex-M7?"

**A.** 리비전을 먼저 본다. r0p0 · r0p1이면 BASEPRI 쓰기 errata 우회가 들어간 `ARM_CM7/r0p1`, 그 밖이면 `ARM_CM4F`를 권장한다. `ARM_CM4F`는 CPUID로 r0p1을 감지하면 assert로 멈춘다.

> I check the core revision in CPUID first. Early M7 revisions, r0p0 and r0p1, have an erratum where raising BASEPRI doesn't take effect for the next instruction, so the ARM_CM7/r0p1 port wraps the BASEPRI write with cpsid and cpsie. For later revisions the ReadMe recommends the ARM_CM4F port, and that port actually asserts if it detects an r0p1 core. The diff between them is literally those two assert lines and two instructions.

**Q.** "What are Helium and PACBTI, and does the RTOS care?"

**A.** Helium은 v8.1-M의 128비트 벡터 확장으로 FPU 레지스터를 공유하므로 FPU 컨텍스트 저장에 포함된다(`configENABLE_MVE`). PACBTI는 리턴 주소 서명과 간접 분기 착지 검사다. RTOS는 태스크별 PAC 키를 컨텍스트에 넣고 CONTROL의 PAC_EN · BTI_EN을 설정한다.

> Helium is the M-profile vector extension in Armv8.1-M; its Q registers alias the FPU register file, so saving the FP context saves the vector state too — FreeRTOS forces you to define configENABLE_MVE. PACBTI signs the return address with a key and restricts indirect branch targets. The RTOS does care: the FreeRTOS v8-M port can keep a per-task PAC key and swaps it in PendSV, and sets PAC_EN and BTI_EN in CONTROL. The instructions live in NOP hint space, so I ran an M85 PACBTI build unchanged on an M55 in QEMU.

**Q.** "Is there anything newer than the Cortex-M85?"

**A.** 2026년 10월 기준 Arm의 마지막 Cortex-M 발표는 M52(2023년 11월)이고, M85(2022)가 최고 성능이다. Arm은 다음 코어를 작업 중이라고만 밝혔고, 고성능 엣지 AI는 Cortex-A320 + Ethos-U85 쪽으로 가고 있다.

> As of late 2026, no. The newest Cortex-M announcement is the M52 from November 2023 — the smallest Helium core — and the M85 from 2022 is still the performance top. Arm said in mid-2026 that it's working on concepts for the next core but that pushing Cortex-M performance isn't a priority; high-end edge AI is moving to the Cortex-A320 with Ethos-U85. So the action is new chips on existing cores, like Renesas RA8 on M85 and STM32N6 on M55.

---

## 11. 직접 해보기

1. **개념**: Armv8-M Baseline이 Armv6-M에 더한 명령 세 가지와, 더하지 **않은** RTOS 관련 레지스터 하나를 말하라. 정답/힌트: UDIV/SDIV, LDREX/STREX(+LDA/STL), CBZ · MOVW/MOVT 중 셋. 빠진 것은 BASEPRI(§1.2).
2. **개념**: 같은 FreeRTOS 앱에서 `ARM_CM55_NTZ`와 `ARM_CM33_NTZ`의 `port.c`가 같은 파일인데, 왜 디렉터리를 따로 두는가? 정답/힌트: `portmacro.h`의 아키텍처 이름 · `portARMV8M_MINOR_VERSION` · MVE 허용 여부 · SMP 검증 여부가 다르다. 원본은 `portable/ARMv8M`에서 복사(예제 10).
3. **코드**: 예제 2의 `div_loop`을 나눗셈 없이(제수가 3~18이므로 역수 곱셈 또는 표) 다시 써서 M0+ 명령 수를 386,437에서 몇까지 줄일 수 있는지 재라. 결과값 `0xdc058011`이 같아야 한다. 정답/힌트: `x / d`를 `((uint64_t)x × magic[d]) >> s`로 바꾸면 64비트 곱셈이 `__aeabi_lmul` 호출이 된다 — M0+에서는 곱셈 도우미도 공짜가 아니니 실측으로 판단한다.
4. **코드**: 예제 7을 `mps2-an505`에서 **MSPLIM**으로 바꿔 보라(재귀를 MSP에서 돌림). fault 핸들러도 MSP를 쓰는데 무슨 일이 일어나는가? 정답/힌트: STKOF 후 핸들러 진입 자체가 스택 한계에 걸릴 수 있다. 그래서 RTOS는 태스크를 PSP에 두고 MSP는 넉넉히 둔다. 실행해서 CFSR · HFSR을 확인하라.
5. **측정**: 예제 11의 `ARM_CM4F` 빌드에서 ping 태스크가 루프마다 `float` 계산 한 번을 하게 바꾸고 왕복 명령 수가 얼마나 느는지 재라. 정답/힌트: EXC_RETURN bit 4가 0이 되어 PendSV가 S16~S31을 저장 · 복원한다(§4.3) — 늘어난 명령 수를 B6의 lazy stacking 설명과 맞춰 본다.
6. **측정**: `ex_mve.c`의 N을 1000, 1008, 1016, 1024로 바꿔 MVE on 판 `dot16` 명령 수를 그래프로 그려라. 꼬리 처리 비용이 보이는가? 정답/힌트: `vctp`로 꼬리를 처리하므로 계단 없이 거의 선형이어야 한다. 스칼라 꼬리 루프를 쓰는 `add8`과 비교한다.
7. **조사**: 지금 쓰는(또는 지원하는 회사의) 보드 하나를 골라 CPUID r/p 값, FPU 유무, MPU 영역 수, NVIC 우선순위 비트 수, TrustZone 유무를 데이터시트에서 찾아 그림 2 형식으로 한 줄을 채워라. 정답/힌트: CPUID는 예제 1을 그대로 돌리면 되고, 우선순위 비트 수는 NVIC_IPR에 0xFF를 쓰고 읽어 보면 안다(B3).

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| M 프로파일 | Microcontroller profile | Cortex-M이 구현하는 Arm 아키텍처 계열. A(application) · R(real-time)과 구분 |
| Armv6-M | 가장 작은 M 아키텍처 | M0 · M0+ · M1. BASEPRI · LDREX · 나눗셈 없음 |
| Armv7-M / 7E-M | Thumb-2 M 아키텍처 | M3 / M4 · M7. 7E-M = DSP 확장 포함 |
| Armv8-M Baseline | v6-M의 후계 | M23. 나눗셈 · LDREX · TrustZone 옵션, BASEPRI 없음 |
| Armv8-M Mainline | v7-M의 후계 | M33 · M35P. TrustZone · PSPLIM · PMSAv8 |
| Armv8.1-M | Mainline + 벡터 · 보안 | M55 · M85 · M52. Helium · LOB · PACBTI |
| BASEPRI | Base priority mask | 지정 우선순위 이하 인터럽트만 막는 레지스터 |
| PRIMASK | Priority mask | 1이면 NMI · HardFault 말고 전부 막음 |
| LDREX/STREX | Exclusive load/store | 배타 모니터로 원자적 RMW를 만드는 명령 쌍 |
| LDAEX/STLEX | Acquire/release exclusive | 메모리 순서 의미가 붙은 v8-M 배타 명령 |
| PSPLIM / MSPLIM | Stack pointer limit | SP가 이 아래로 가면 STKOF UsageFault |
| STKOF | Stack overflow flag | UFSR bit 4, CFSR 0x00100000 |
| NOCP | No coprocessor | CPACR로 안 켠 FPU · MVE를 쓰면 나는 UsageFault, CFSR 0x00080000 |
| CPACR | Coprocessor Access Control Register | 0xE000ED88, CP10 · CP11로 FPU · MVE 접근 허용 |
| TrustZone-M | Armv8-M 보안 확장 | Secure / Non-secure 두 상태로 코어를 나눔 |
| NTZ 포트 | No TrustZone port | FreeRTOS `ARM_CMxx_NTZ`, Secure 컨텍스트 관리 없음 |
| Helium / MVE | M-Profile Vector Extension | 128비트 벡터, FPU 레지스터 공유 |
| beat | Helium 실행 단위 | 128비트 명령을 1/4(single) · 1/2(dual)씩 처리 |
| LOB | Low Overhead Branch | `DLS`/`LE`/`WLS` 루프 명령, 분기 비용 제거 |
| tail predication | 꼬리 예측 | `VCTP`/`DLSTP`로 마지막 벡터의 남은 lane만 켬 |
| PACBTI | Pointer Authentication + BTI | 리턴 주소 서명 + 간접 분기 착지 제한 |
| DSP 확장 | Armv7E-M SIMD · 포화 명령 | `SMLAD` · `SMLABB` · `QADD` 등 |
| superscalar | 다중 발행 | 한 사이클에 명령 여러 개(M7 · M85는 2개) |
| TCM | Tightly Coupled Memory | 캐시 없이 고정 지연으로 접근하는 코어 전용 SRAM(B8) |
| CPUID | 0xE000ED00 | 구현자 · part · r/p 리비전을 담은 ID 레지스터 |
| semihosting | 디버거 경유 입출력 | `BKPT 0xAB`로 호스트(QEMU)에 출력 · 종료 요청 |
| icount | QEMU 명령 계수 모드 | 명령 1개당 가상 시간 2^shift ns, 결정적 타이밍 |

---

## 13. 요약 & 체크리스트

Cortex-M은 다섯 세대의 아키텍처(v6-M, v7-M/7E-M, v8-M Baseline, v8-M Mainline, v8.1-M)와 그것을 구현한 10여 개 코어로 이뤄져 있고, 칩 회사가 코어 옵션(MPU · FPU · TrustZone · 캐시 · Helium · PACBTI)을 골라 칩을 만든다. RTOS 포트는 아키텍처 단위로 갈린다: **v6-M · v8-M Baseline은 PRIMASK, 나머지는 BASEPRI**로 크리티컬 섹션을 만들고, v8-M은 PSPLIM과 TrustZone 컨텍스트, v8.1-M은 MVE와 PAC 키를 컨텍스트에 더한다. 7개 코어로 같은 C 코드를 재 보니 일반 코드의 명령 수 · 크기 차이는 작고(M3 ≈ M4 ≈ M7 ≈ M33, M55는 LOB로 10~15% 적음), **그 코어에 없는 명령을 라이브러리로 채우는 곳**(M0+의 나눗셈 24배, 원자 연산 링크 실패)에서 크게 갈렸다. 같은 FreeRTOS 앱은 포트만 바꿔 6개 코어에서 돌았고 왕복 비용 차이는 10% 이내였다. 2026년 10월 기준 새 Cortex-M 코어는 없고(M52, 2023이 마지막), 변화는 기존 코어를 쓴 새 칩(RA8, STM32N6)에서 온다.

- [ ] v6-M · v7-M · v7E-M · v8-M Base/Main · v8.1-M이 각각 더한 것을 표 없이 말할 수 있다
- [ ] M23이 v8-M인데도 BASEPRI가 없는 이유(Baseline)를 설명할 수 있다
- [ ] M0+/M4/M7/M33/M55/M85 각각의 대표 칩과 클록을 하나씩 댈 수 있다
- [ ] "2026 신규 Cortex-M 없음"을 출처(M52 2023-11, eeNews 2026-07)와 함께 말할 수 있다
- [ ] FreeRTOS `ARM_CM0` · `ARM_CM3` · `ARM_CM4F` · `ARM_CM7/r0p1` · `ARM_CMxx` · `_NTZ` 중 하나를 칩에 맞게 고를 수 있다
- [ ] v6-M에서 `msr basepri`가 조용히 무시되는 함정을 설명할 수 있다
- [ ] v6-M에서 원자 연산을 구현하는 세 가지 방법(PRIMASK, 하드웨어 스핀락, RMW 없는 설계)을 말할 수 있다
- [ ] PSPLIM이 잡는 STKOF를 CFSR 값(0x00100000)으로 알아볼 수 있다
- [ ] Secure로 부팅하는 M33에서 `configRUN_FREERTOS_SECURE_ONLY`가 왜 필요한지(EXC_RETURN) 말할 수 있다
- [ ] Helium 빌드의 NOCP fault(CFSR 0x00080000)와 CPACR 순서 문제를 설명할 수 있다
- [ ] PACBTI 명령이 M55에서 NOP으로 실행되는 이유(힌트 공간)를 말할 수 있다
- [ ] `mc.sh` 하네스로 새 코어용 실험을 직접 빌드 · 실행할 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM C11 | Cortex-M 세대 · Armv8.1-M | §1(세대), §2(코어 카드 · 매트릭스 · 대표 칩 · 2026 신규 코어 없음), §3(같은 코드 측정 · LOB), §4.7~4.9(Helium · PACBTI · LOB) | 칩 bring-up 순서(CPACR · SHCSR · CCR 초기화), 벤더 데이터시트에서 코어 옵션 읽는 법, RTOS 없는 코드에서의 LOB · Helium 사용 |
| BM C10 | FPU · Helium(부분) | §4.6 · §4.7은 포트 선택만 — 상세 주인은 RTOS B6 | B6을 링크 |
| BM C5 | NVIC · PRIMASK · BASEPRI(부분) | §4.1은 세대 차이만 — 주인은 RTOS B3 | B3을 링크 |
| BM C12 | TrustZone-M(부분) | §4.5는 포트 선택만 — 주인은 RTOS F6 | 두 이미지 구성, SAU · IDAU 설정 |

---

## 참고 자료

1. Arm, "Arm Cortex-M Processor Comparison Table" (2023판 PDF) — 아키텍처 · TrustZone · Helium · PACBTI · FPU · DSP · 나눗셈 · DMIPS/MHz · CoreMark/MHz · MPU · 캐시 · TCM 수치의 출처. https://documentation-service.arm.com/static/655e085f2c8b3557fee7048f
2. Arm Newsroom, "New Arm Cortex-M52 is the smallest, most area and cost-efficient processor enabled with Arm Helium technology" (2023-11) — https://newsroom.arm.com/news/arm-cortex-m52
3. Arm, Cortex-M52 제품 페이지 (4단 파이프라인, single-beat Helium, PACBTI · TrustZone 옵션) — https://www.arm.com/products/silicon-ip-cpu/cortex-m/cortex-m52
4. Arm Community 블로그, "Introducing Cortex-M52" (2023-11-22) — https://developer.arm.com/community/arm-community-blogs/b/internet-of-things-blog/posts/introducing-cortex-m52
5. eeNews Europe, "What now for the Arm Cortex-M microcontroller?" (2026-07-27) — 다음 코어 작업 중, 성능 확대는 우선순위 아님 — https://www.eenewseurope.com/en/what-now-for-the-arm-cortex-m-microcontroller/
6. Renesas 보도자료, RA8 — 업계 최초 Cortex-M85 MCU (2023-10-31, 480 MHz, 6.39 CoreMark/MHz) — https://www.renesas.com/en/about/newsroom/renesas-new-ultra-high-performance-mcus-are-industry-s-first-based-arm-cortex-m85-processor
7. CNX Software, "Arm Cortex-M85 is faster than Cortex-M7…" (2022-04-27, 7단 정수 파이프라인) — https://www.cnx-software.com/2022/04/27/arm-cortex-m85-is-faster-than-cortex-m7-offers-higher-ml-performance-than-cortex-m55/
8. ST Community, "STM32N6 has been officially launched" (2024-12) 와 CNX Software 2024-12-13 기사 — M55 800 MHz + Neural-ART — https://www.cnx-software.com/2024/12/13/stmicro-releases-stm32n6-cortex-m55-mcu-series-with-in-house-npu-and-dedicated-computer-vision-pipeline/
9. NXP, i.MX RT1170 제품 페이지 (M7 1 GHz + M4 400 MHz) — https://www.nxp.com/products/i.MX-RT1170
10. Alif Semiconductor, Ensemble E7 (M55 400 MHz + 160 MHz, Ethos-U55) — https://alifsemi.com/ensemble-e7-series/
11. Raspberry Pi, RP2350 제품 페이지 (M33 × 2 150 MHz, Hazard3) — https://www.raspberrypi.com/products/rp2350/
12. Nordic Semiconductor, nRF54L15 제품 페이지 (M33 128 MHz) — https://www.nordicsemi.com/Products/nRF54L15
13. FreeRTOS Kernel V11.3.2 소스: `portable/GCC/ARM_CM0/portmacro.h`, `ARM_CM3/portmacro.h`, `ARM_CM4F/port.c`, `ARM_CM7/ReadMe.txt`, `ARM_CM7/r0p1/port.c`, `ARM_CM23_NTZ/non_secure/portasm.c`, `ARM_CM33_NTZ/non_secure/port.c` · `portasm.c` · `portmacro.h`, `ARM_CM85_NTZ/non_secure/port.c` · `portasm.c`, `portable/ARMv8M/copy_files.py` (랩 `/var/rtoslab/src/FreeRTOS-Kernel`)
14. FreeRTOS-Kernel 커밋 "Add Cortex M7 r0p1 Errata 837070 workaround to CM4_MPU ports (#513)" — https://github.com/FreeRTOS/FreeRTOS-Kernel/pull/513
15. Zephyr v4.4.2 소스: `include/zephyr/arch/arm/asm_inline_gcc.h`(`arch_irq_lock`), `arch/arm/core/cortex_m/Kconfig`(CPU 기호), `arch/arm/core/cortex_m/reset.S` · `svc.S` · `arm-m-switch.c`(PSPLIM), `boards/arm/mps2/board.yml` · `boards/arm/mps3/board.yml`(보드 이름), `boards/arm/mps2/mps2_an521_cpu0.dts` · `boards/arm/mps3/mps3_corstone300_an547.dts`(메모리 맵)
16. Arm, "Armv8-M Architecture Reference Manual" (DDI0553) — Baseline/Mainline 구분, 스택 한계, PACBTI · LOB · MVE 정의. https://developer.arm.com/documentation/ddi0553/latest
17. Arm, "Armv7-M Architecture Reference Manual" (DDI0403) — https://developer.arm.com/documentation/ddi0403/latest
18. QEMU 문서, "Arm MPS2 and MPS3 boards" — https://www.qemu.org/docs/master/system/arm/mps2.html
