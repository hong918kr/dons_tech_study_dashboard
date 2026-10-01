# E2. ARM Cortex-M / Cortex-A와 SIMD — M4F·M7·M55 Helium, A55·A7x, NEON·SDOT·i8mm

> **이 노트를 다 읽으면**: Arm 프로파일(A/R/M)과 ISA 버전(Armv7E-M, Armv8.1-M, Armv8/9-A)을 보고 그 코어에 어떤 SIMD가 있는지 말할 수 있다 · int8 내적이 Cortex-M4(SXTB16+SMLAD), M55(Helium VMLADAV + tail predication), Cortex-A(SDOT, SMMLA)에서 각각 어떤 명령으로 도는지 어셈블리로 확인할 수 있다 · NEON intrinsics로 int8 GEMV/GEMM을 짜서 결과를 bit-exact로 맞추고 속도를 재고, 레지스터 블로킹이 왜 필요한지 숫자로 설명할 수 있다 · `-Rpass` remark와 `-S` 출력으로 컴파일러가 루프를 벡터화했는지 검사하고, M-profile 컴파일 플래그(`-mcpu`, `-mfpu`, `-mfloat-abi`)를 정확히 고를 수 있다
> **JD 연결**: "Optimizing models for MCUs & edge processors (**Cortex-M/A**, RISC-V, DSP)" — study_prep_list **E2** 행: M4F, M7, M33, **M55/M85 + Helium(MVE)** / A55·A7x·X 계열, big.LITTLE / **NEON**, SVE2, dot-product 명령(SDOT/UDOT), i8mm ("SIMD가 ML 커널의 핵심", P0). **J3** 행(고정소수점·SIMD 커널: CMSIS-DSP/NN, NEON·Helium intrinsics, INT8 matmul 직접 작성)의 기초 절반을 여기서 깐다
> **Don 기준 난이도**: Cortex-R8/R82/M0+ bare-metal bring-up, 벡터 테이블·캐시·TCM·예외 모델, 어셈블리 읽기는 이미 강하다 / 새로 배울 것은 "SIMD 레인으로 생각하기" — 32-bit 레지스터를 16-bit 두 칸으로 쓰는 DSP 확장, Helium의 beat·tail predication, NEON의 dot-product·matrix 명령, 그리고 컴파일러가 이걸 알아서 해 주는지 확인하는 습관
> **선행 노트**: A1(행렬곱 = 이중 루프 MAC), C1 7절(int32 누산 → requantization, `dense_s8` C 커널)·11.2절(CMSIS-NN 개요), D3 5.6절(NEON fp32 FMA 천장, 누산기 개수 실험)·roofline, D4(계열별 성능, GEMV vs GEMM). 병렬 작성 중인 E1(CPU 마이크로아키텍처), E3(RISC-V RVV), E4(DSP·Hexagon), E5(NPU)와 ID로 서로 참조한다

---

## 0. 큰 그림 — 이게 왜 필요한가

edge ML 추론 연산의 대부분은 결국 **int8 곱셈-누산(MAC)** 이다. conv도, dense도, attention의 QKᵀ도 "int8 두 벡터의 내적을 int32에 쌓는다"로 바뀐다 (C1 7절). 그러니 "이 칩에서 모델이 얼마나 빨리 도나"는 거의 "이 코어는 **한 명령에 int8 MAC을 몇 개** 하고, 그걸 **매 cycle 몇 개** 내보낼 수 있나"로 줄어든다.

Arm 코어마다 그 답이 완전히 다르다.

```
같은 C 한 줄:   acc += a[i] · b[i];     (int8 × int8 → int32)

Cortex-M0+   MULS + ADDS 2개 = MAC 1개   (SIMD 없음)
Cortex-M4    SMLAD 1개 = MAC 2개         (32-bit 레지스터를 16-bit 두 칸으로: DSP 확장)
Cortex-M55   VMLADAVA 1개 = MAC 16개     (128-bit Helium 벡터, M55는 2 tick에 나눠 실행)
Cortex-A55   SDOT 1개 = MAC 16개         (128-bit NEON, lane마다 int8 4쌍)
Armv8.6-A+   SMMLA 1개 = MAC 32개        (2×8 · 8×2 행렬 조각)
```

그리고 **컴파일러가 이 명령들을 알아서 써 주느냐**가 또 별개의 문제다. 이 노트에서 직접 확인하겠지만, 같은 C 루프가 `-O2`에서는 SMLAD를 안 쓰고 `-O3`에서는 쓰고, `-Os`에서는 NEON 벡터화를 포기하고, `-mcpu=cortex-a53`에서는 SMULL로, `-mcpu=cortex-a55`에서는 SDOT으로 바뀐다. 펌웨어 엔지니어가 "빌드 옵션 한 줄"이 성능을 몇 배 바꾸는 걸 가장 먼저 알아채야 하는 이유다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 365">
<defs>
<marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="22.0" font-size="14" text-anchor="start">Arm 프로파일 지도 — 세로축: 성능·전력 (위로 갈수록 큼), 색: 벡터/SIMD 종류</text><text x="115.0" y="52.0" font-size="13" text-anchor="middle">M-profile (마이크로컨트롤러)</text><text x="345.0" y="52.0" font-size="13" text-anchor="middle">R-profile (실시간)</text><text x="565.0" y="52.0" font-size="13" text-anchor="middle">A-profile (애플리케이션)</text><line x1="8" y1="336" x2="8" y2="70" stroke="currentColor" marker-end="url(#ar)"/><text x="14.0" y="80.0" font-size="12" text-anchor="start">↑</text><rect x="20" y="290" width="190" height="46" rx="6" fill="#888" fill-opacity="0.18" stroke="#888"/><text x="115.0" y="309.0" font-size="12" text-anchor="middle">M0+ · Armv6-M</text><text x="115.0" y="325.0" font-size="12" text-anchor="middle">SIMD 없음 · FPU 없음</text><rect x="20" y="235" width="190" height="46" rx="6" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/><text x="115.0" y="254.0" font-size="12" text-anchor="middle">M4(F) · Armv7E-M</text><text x="115.0" y="270.0" font-size="12" text-anchor="middle">DSP SIMD32 · SP FPU 옵션</text><rect x="20" y="180" width="190" height="46" rx="6" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/><text x="115.0" y="199.0" font-size="12" text-anchor="middle">M33 · Armv8-M Main</text><text x="115.0" y="215.0" font-size="12" text-anchor="middle">TrustZone · DSP 옵션</text><rect x="20" y="125" width="190" height="46" rx="6" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/><text x="115.0" y="144.0" font-size="12" text-anchor="middle">M7 · Armv7E-M</text><text x="115.0" y="160.0" font-size="12" text-anchor="middle">dual-issue · SP/DP FPU 옵션</text><rect x="20" y="70" width="190" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.18" stroke="#3f9a6b"/><text x="115.0" y="89.0" font-size="12" text-anchor="middle">M55 / M85 · Armv8.1-M</text>
<text x="115.0" y="105.0" font-size="12" text-anchor="middle">Helium(MVE) 128-bit</text><rect x="250" y="235" width="190" height="56" rx="6" fill="#888" fill-opacity="0.18" stroke="#888"/><text x="345.0" y="259.0" font-size="12" text-anchor="middle">R5 / R8 · Armv7-R</text><text x="345.0" y="275.0" font-size="12" text-anchor="middle">Don: R8 양산 FW</text><rect x="250" y="160" width="190" height="56" rx="6" fill="#888" fill-opacity="0.18" stroke="#888"/><text x="345.0" y="184.0" font-size="12" text-anchor="middle">R52 · Armv8-R (32-bit)</text><text x="345.0" y="200.0" font-size="12" text-anchor="middle">R82 · Armv8-R AArch64</text><text x="345.0" y="310.0" font-size="12" text-anchor="middle">(R52/R82의 SIMD·FPU 옵션은</text><text x="345.0" y="326.0" font-size="12" text-anchor="middle">구성별 TRM 확인)</text><rect x="470" y="235" width="200" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/><text x="570.0" y="259.0" font-size="12" text-anchor="middle">A53 (v8.0) / A55 (v8.2)</text><text x="570.0" y="275.0" font-size="12" text-anchor="middle">little · in-order · NEON</text><rect x="470" y="160" width="200" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/><text x="570.0" y="184.0" font-size="12" text-anchor="middle">A76~A78 (v8.2) → A7xx (v9)</text><text x="570.0" y="200.0" font-size="12" text-anchor="middle">big · OoO · SDOT → +SVE2·i8mm</text><rect x="470" y="85" width="200" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/><text x="570.0" y="109.0" font-size="12" text-anchor="middle">X1 ~ X4 / X925</text><text x="570.0" y="125.0" font-size="12" text-anchor="middle">최대 성능 코어</text><rect x="20" y="342" width="10" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/><text x="33.0" y="352.0" font-size="11" text-anchor="start">SIMD 없음/옵션</text><rect x="160" y="342" width="10" height="10" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/><text x="173.0" y="352.0" font-size="11" text-anchor="start">32-bit SIMD (DSP 확장)</text><rect x="330" y="342" width="10" height="10" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/><text x="343.0" y="352.0" font-size="11" text-anchor="start">Helium</text>
<rect x="430" y="342" width="10" height="10" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/><text x="443.0" y="352.0" font-size="11" text-anchor="start">NEON (±SVE2)</text></svg>
```

그림 1 — Arm 코어 지도. 세 열은 프로파일(M: 마이크로컨트롤러, R: 실시간, A: 애플리케이션), 세로는 대략적인 성능·전력 순서다(같은 열 안에서도 엄밀한 순위는 아니다). 색은 int8 연산에 쓸 수 있는 SIMD 종류: 회색 = 없음(또는 구성 옵션), 주황 = 32-bit 레지스터 안의 SIMD(DSP 확장), 초록 = Helium(MVE), 파랑 = NEON(Armv9 코어는 SVE2 포함). Don이 양산 FW를 짠 R8은 가운데 열이다.

웨어러블(예를 들어 Hark 같은 기기라고 **가정**하면)에서는 이 지도의 여러 칸이 한 기기 안에 같이 있다.

```
 ┌─────────────── always-on 도메인 (µW~mW) ───────────────┐   ┌──────── 메인 SoC (수백 mW~W) ────────┐
 │ 마이크 → VAD/wake word, IMU → 제스처·착용 감지          │   │ Cortex-A55/A7x (NEON SDOT/i8mm)       │
 │ Cortex-M4F (SMLAD) 또는 M55 (Helium) [+ Ethos-U55 NPU]  │──▶│ + DSP(Hexagon, E4) + NPU(E5)          │
 │ 상시 켜짐, SRAM 수백 KB~수 MB, RTOS/bare-metal          │IPC│ LPDDR, Linux/Android, ASR·LLM          │
 └────────────────────────────────────────────────────────┘   └───────────────────────────────────────┘
```

이 노트의 순서: 1절 지도 읽는 법 → 2절 Cortex-M DSP 확장(SMLAD) → 3절 Helium → 4절 NEON 명령 → 5절 이 Mac에서 실측 → 6절 자동 벡터화 검사 → 7절 SVE → 8절 정밀도·FPU → 9절 CMSIS와 컴파일 플래그 → 10절 웨어러블에서 코어 고르기.

---

## 1. Arm 지도 — 프로파일(A/R/M)과 ISA 버전

### 1.1 세 프로파일

Arm 아키텍처는 **프로파일**이 세 개다. Don은 이미 둘을 써 봤다.

| 프로파일 | 목적 | 특징 | Don 경험 |
|---|---|---|---|
| **M** (Microcontroller) | 저전력 MCU, 센서 허브 | Thumb 명령만, NVIC·벡터 테이블(주소 목록), MPU(가상 메모리 없음), 결정적 인터럽트 지연 | M0+ bring-up |
| **R** (Real-time) | SSD 컨트롤러, 모뎀, 자동차 | MPU(R82는 MMU 옵션), TCM, 낮은 인터럽트 지연 + 높은 성능 | R8, R82 양산 FW |
| **A** (Application) | 폰·AP·리눅스 | MMU(가상 메모리), 풀 OS, 큰 캐시, NEON이 사실상 기본 | 없음 (Apple 칩 사용자로서만) |

펌웨어 비유로 한 줄: **M은 "인터럽트 지연과 전력"**, **R은 "결정성 + 성능"**, **A는 "OS와 처리량"** 에 최적화된 설계다. SIMD도 이 성격을 따라간다 — M은 작은 면적의 SIMD(DSP 확장, Helium), A는 넓고 빠른 NEON/SVE2.

### 1.2 아키텍처 버전 vs 코어 이름

헷갈리기 쉬운 것 하나: **"Armv7E-M"은 아키텍처(명령어 규약)이고 "Cortex-M4"는 그걸 구현한 코어(마이크로아키텍처)다.** 문서도 둘로 나뉜다.

- 아키텍처 매뉴얼(Arm ARM): "SMLAD가 무엇을 계산하나"를 정의 → Armv7-M ARM, Armv8-M ARM, A-profile ARM.
- 코어 TRM(Technical Reference Manual)·Optimization Guide: "SMLAD가 몇 cycle인가, 파이프라인은 몇 단인가"를 정의.

| 아키텍처 | 대표 코어 | int8에 쓸 SIMD | 비고 |
|---|---|---|---|
| Armv6-M | M0, M0+ | 없음 | Thumb 부분집합, 곱셈기는 구성에 따라 1-cycle 또는 32-cycle |
| Armv7-M | M3 | 없음 | 하드웨어 나눗셈, 조건부 실행(IT) |
| **Armv7E-M** | **M4, M7** | **DSP 확장** (32-bit 안의 2×16, 4×8 SIMD) | "E" = DSP extension. FPU는 옵션 |
| Armv8-M Baseline | M23 | 없음 | TrustZone-M |
| Armv8-M Mainline | M33, M35P | DSP 확장 **옵션** | TrustZone-M |
| **Armv8.1-M** Mainline | **M55, M85** (그리고 M52) | **MVE = Helium** (128-bit 벡터) + DSP 확장 | low-overhead loop(LE/DLS/WLS) 추가 |
| Armv8-A (v8.0) | A53, A72 | NEON (Advanced SIMD) | SDOT 없음 |
| Armv8.2-A | **A55**, A75~A78, X1 | NEON + **SDOT/UDOT**(dotprod), FP16 | dotprod는 v8.2에서 옵션, v8.4부터 필수. 이 코어들은 구현한다 |
| Armv8.6-A | (Apple M2가 흔히 v8.6급으로 소개됨) | + **i8mm (SMMLA/UMMLA/USMMLA)**, BF16 | i8mm·BF16은 v8.2부터 옵션 확장, v8.6부터는 Advanced SIMD가 있으면 필수로 정의된 것으로 안다 — 실제 칩은 feature 레지스터/`sysctl`로 확인 |
| **Armv9-A** | A510/A520, A710~A725, X2~X4 | NEON + **SVE2** (+ i8mm, BF16) | SVE2는 Armv9-A의 필수 요소 |

마지막 행 옆의 주의: 확장이 "아키텍처 버전에서 필수인가 옵션인가"는 버전마다 복잡하게 바뀐다. **실무 규칙: 버전 번호로 추측하지 말고, 대상 코어의 TRM과 런타임 feature 레지스터(`ID_AA64ISAR0_EL1` 등, 리눅스에선 `/proc/cpuinfo`의 `asimddp`, `i8mm` 플래그)를 확인한다.**

M52는 2023년에 발표된 작은 Helium 코어다(M33급 면적에 Helium을 넣은 것으로 소개됨). 이 노트의 표에는 M55/M85만 자세히 쓴다.

### 1.3 웨어러블에 관련된 코어 표

확실한 것만 적었다. 파이프라인 단수는 Arm이 공식 자료로 밝힌 것 중 널리 인용되는 값이고, 모르는 칸은 비워 두었다.

| 코어 | 아키텍처 | 파이프라인 | SIMD | FPU | 전형적 용도 |
|---|---|---|---|---|---|
| M0+ | Armv6-M | 2단 | 없음 | 없음 | 초저전력 제어, 부트 헬퍼 |
| M4 (F) | Armv7E-M | 3단 | DSP 확장 | SP(fp32) 옵션 → "M4F" | 센서 허브, 오디오 전처리, CMSIS-NN 기본 타깃 |
| M7 | Armv7E-M | 6단, dual-issue superscalar | DSP 확장 | SP 또는 SP+DP 옵션 | 고성능 MCU, I/D 캐시·TCM |
| M33 | Armv8-M Mainline | 3단 | DSP 확장 옵션 | SP 옵션 | TrustZone이 필요한 IoT MCU |
| M55 | Armv8.1-M | (TRM 확인) | DSP + **Helium** (정수만 또는 정수+FP 구성) | 스칼라 FP 옵션(fp16/fp32/fp64) | ML MCU, Ethos-U55와 짝 |
| M85 | Armv8.1-M | (TRM 확인), superscalar | DSP + **Helium** | 옵션 | 가장 빠른 Cortex-M |
| A55 | Armv8.2-A | in-order (제한적 dual-issue) | NEON + SDOT + FP16 | 있음 | little 코어, 저전력 AP |
| A7x (A76~A78, A710~A725) | v8.2-A → v9-A | out-of-order | NEON + SDOT (v9는 SVE2·i8mm) | 있음 | big 코어 |
| X1~X4 | v8.2-A → v9.2-A | 넓은 out-of-order | 위와 같음 | 있음 | 최대 성능 코어 |

M55 Helium의 "한 tick에 몇 beat"(3.2절)나 A55의 NEON 파이프 폭처럼 **처리량을 정하는 숫자는 코어마다, 구성마다 다르다.** 면접에서 모르는 숫자를 지어내는 것보다 "TRM/Software Optimization Guide를 본다"고 말하는 게 훨씬 신뢰를 준다.

### 1.4 코드로 확인 — 컴파일러가 코어별로 켜는 기능 매크로

무엇을 확인하나: `-mcpu`를 바꾸면 컴파일러가 ACLE(Arm C Language Extensions) 기능 매크로를 다르게 정의한다. 이 매크로가 CMSIS-NN이 "어느 경로(DSP / MVE / 순수 C)로 컴파일할지" 고르는 스위치다.

```sh
for c in cortex-m0plus cortex-m4 cortex-m7 cortex-m33 cortex-m55 cortex-m85; do
  printf "%-14s " $c
  echo | cc --target=arm-none-eabi -mcpu=$c -dM -E - |
    grep -E "#define (__ARM_ARCH|__ARM_FEATURE_DSP|__ARM_FEATURE_MVE|__ARM_FP) " |
    sed "s/#define //" | tr "\n" " "; echo
done
```

```text
cortex-m0plus  __ARM_ARCH 6 
cortex-m4      __ARM_ARCH 7 __ARM_FEATURE_DSP 1 __ARM_FP 0x6 
cortex-m7      __ARM_ARCH 7 __ARM_FEATURE_DSP 1 __ARM_FP 0xe 
cortex-m33     __ARM_ARCH 8 __ARM_FEATURE_DSP 1 __ARM_FP 0x6 
cortex-m55     __ARM_ARCH 8 __ARM_FEATURE_DSP 1 __ARM_FEATURE_MVE 3 __ARM_FP 0xe 
cortex-m85     __ARM_ARCH 8 __ARM_FEATURE_DSP 1 __ARM_FEATURE_MVE 3 __ARM_FP 0xe 
```

출력에서 볼 것:

- `__ARM_FEATURE_DSP 1`: M4·M7·M33·M55·M85에만 있다. M0+에는 없다 → SMLAD 경로 불가.
- `__ARM_FEATURE_MVE 3`: M55·M85만. 값은 비트 필드로, 1 = 정수 MVE, 2 = 부동소수점 MVE, 3 = 둘 다.
- `__ARM_FP`: 비트 필드로 0x2 = half(fp16 저장·변환), 0x4 = single, 0x8 = double. M4는 0x6(fp16 변환 + fp32), M7(clang 기본 구성)·M55는 0xe(+fp64). 실제 칩이 어떤 FPU 구성으로 만들어졌는지는 칩 데이터시트가 정한다 — 컴파일러 기본값은 "가장 흔한/가장 큰" 구성을 가정할 뿐이다.

CMSIS-NN 소스 안에는 `#if defined(ARM_MATH_MVEI)` / `#elif defined(ARM_MATH_DSP)` 같은 분기가 있고, 이 매크로들은 CMSIS 헤더가 위의 컴파일러 매크로를 보고 정한다. 즉 **`-mcpu`를 잘못 주면 같은 라이브러리가 조용히 느린 C 경로로 빌드된다.** 경고는 없다.

### 1.5 big.LITTLE과 DynamIQ

- **big.LITTLE**: 같은 ISA를 쓰는 큰 코어(성능)와 작은 코어(효율)를 한 칩에 두고, OS 스케줄러가 작업을 옮긴다. 초기에는 big 클러스터와 LITTLE 클러스터가 따로였다(예: A57 ×4 + A53 ×4).
- **DynamIQ**(A75/A55 세대부터): 서로 다른 코어를 **한 클러스터 안에** 섞고, DSU(DynamIQ Shared Unit)의 공유 L3를 같이 쓴다. 코어별로 전압·클럭을 더 세밀하게 나눌 수 있다.
- Apple M2도 같은 개념이다: P-core 4개 + E-core 4개 (`sysctl hw.perflevel0.physicalcpu` = 4, `hw.perflevel1.physicalcpu` = 4를 이 Mac에서 확인).

ML 관점의 포인트: **같은 바이너리가 big에서 돌 때와 LITTLE에서 돌 때 속도가 몇 배 다르다.** 벤치마크할 때 어느 코어에서 돌았는지 모르면 숫자가 흔들린다(D6의 지연 분포, M2의 벤치마크 방법론). 리눅스라면 `taskset`으로 코어를 고정하고, 이 노트의 Mac 실측처럼 고정이 어려우면 반복 측정의 중앙값을 쓴다.

### 1.6 함정

- "Cortex-M4"라고만 쓰여 있으면 FPU가 있는지 모른다. M4F(FPU 있음)인지 데이터시트를 확인한다. M7도 FPU 없음 / SP / SP+DP 구성이 다 가능하다.
- "Armv8.2-A 코어니까 SDOT이 있다"도 원칙적으로는 옵션이다. A55·A76 등 실제 코어는 구현하지만, 확인은 feature 레지스터로.
- M33은 DSP 확장이 **옵션**이다. `-mcpu=cortex-m33+nodsp`로 컴파일하면 6절에서 쓸 `int8x4_t` 같은 SIMD32 타입부터 정의되지 않는다(실제로 해 보면 `use of undeclared identifier 'int8x4_t'` 에러).

---

## 2. Cortex-M DSP 확장 (Armv7E-M) — 32-bit 레지스터 안의 SIMD

### 2.1 직관 — 레지스터 하나를 두 칸으로

M4에는 벡터 레지스터가 없다. 대신 **일반 32-bit 레지스터(r0~r12)를 16-bit 두 칸이나 8-bit 네 칸으로 보는 명령**을 추가했다. 이게 DSP 확장, ACLE 용어로 **SIMD32**다. Don에게 익숙한 비유: 레지스터 하나에 Q15 샘플 두 개를 packing해 두고, 명령 하나로 둘 다 처리하는 것.

대표 명령:

| 명령 | 하는 일 | ML에서 쓰임 |
|---|---|---|
| `SMLAD Rd, Rn, Rm, Ra` | `Rd = Ra + Rn.lo·Rm.lo + Rn.hi·Rm.hi` (16×16 두 쌍, 32-bit 누산) | **int16 내적의 핵심. MAC 2개/명령** |
| `SMLALD` | 위와 같은데 64-bit 누산 | 긴 누산, 오버플로 방지 |
| `SMLABB` 등 | 16×16 한 쌍 + 누산 (B = bottom, T = top 반쪽 선택) | 컴파일러가 스칼라 MAC으로 즐겨 씀 |
| `SXTB16 Rd, Rm {, ROR #n}` | 바이트 0과 2를 부호 확장해 16-bit 두 칸으로 | **int8 → int16 확장** |
| `SADD16`, `QADD16`, `SSAT16` | 16-bit 두 칸 덧셈, 포화 덧셈, 포화 | 활성화·오프셋 처리 |
| `PKHBT`, `PKHTB` | 두 레지스터의 16-bit 반쪽을 합쳐 packing | 데이터 재배치 |

### 2.2 SMLAD 손계산

```
Rn = 0x0003_0002   → hi = 3, lo = 2
Rm = 0xFFFF_0005   → hi = −1, lo = 5
Ra = 100

SMLAD: Rd = 100 + (2·5) + (3·(−1)) = 100 + 10 − 3 = 107
```

말로 하면: **명령 하나가 곱셈 두 개와 덧셈 두 개를 한다.** M4에서 SMLAD는 단일 cycle 처리량 명령으로 알려져 있어서, 이론상 int16 MAC 2개/cycle이 M4 DSP 경로의 천장이다. (정확한 cycle 수는 Cortex-M4 TRM의 명령 타이밍 표 기준으로 확인한다.)

### 2.3 코드로 확인 — 평범한 C int16 내적을 컴파일러가 SMLAD로 바꾸나?

무엇을 확인하나: 가장 평범한 Q15 내적 루프를 M4 타깃으로 `-O2` 컴파일해서, 컴파일러가 SMLAD를 쓰는지 본다. (Apple clang은 링크 없이 `-S`로 Cortex-M 어셈블리를 만들 수 있다.)

```c
#include <stdint.h>
int32_t dot_q15(const int16_t *a, const int16_t *b, int n)
{
    int32_t acc = 0;
    for (int i = 0; i < n; i++)
        acc += (int32_t)a[i] * b[i];
    return acc;
}
```

```sh
cc --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2 -S dot16.c -o dot16_m4.s
```

`-O2` 결과의 내부 루프(지시어 줄은 뺐다):

```text
.LBB0_5:                                @ =>This Inner Loop Header: Depth=1
	adds	r5, r0, r4
	adds	r6, r1, r4
	ldrsh.w	r7, [r5, #2]
	ldrsh.w	r2, [r6, #2]
	ldrsh.w	r9, [r5, #8]
	smlabb	r8, r2, r7, r8
	add.w	r7, r0, r3, lsl #1
	add.w	r5, r1, r3, lsl #1
	ldrsh.w	r10, [r6, #8]
	ldrsh.w	r6, [r7, #2]
	ldrsh.w	r2, [r5, #2]
	ldrsh.w	r7, [r7, #4]
	ldrsh.w	r5, [r5, #4]
	smlabb	r2, r2, r6, r8
	smlabb	r2, r5, r7, r2
	adds	r3, #4
	smlabb	r8, r10, r9, r2
	cmp	lr, r3
	add.w	r4, r4, #8
	bne	.LBB0_5
```

출력에서 볼 것: **SMLAD가 없다.** 루프를 4번 펼쳤지만 16-bit를 하나씩 `ldrsh`(부호 확장 halfword 로드)로 읽고 `smlabb`(MAC 1개)를 4번 한다. MAC 4개에 로드 8개 + MAC 4개.

같은 파일을 `-O3`로 바꾸면:

```text
.LBB0_5:                                @ =>This Inner Loop Header: Depth=1
	adds	r5, r0, r4
	adds	r6, r1, r4
	ldr.w	r5, [r5, #2]
	ldr.w	r6, [r6, #2]
	add.w	r7, r0, r3, lsl #1
	add.w	r2, r1, r3, lsl #1
	ldr	r7, [r7, #4]
	ldr	r2, [r2, #4]
	smlad	r5, r6, r5, r8
	adds	r3, #4
	smlad	r8, r2, r7, r5
	cmp	lr, r3
	add.w	r4, r4, #8
	bne	.LBB0_5
```

출력에서 볼 것: 이번엔 **32-bit `ldr` 한 번에 int16 두 개**를 읽고 **SMLAD 2개로 MAC 4개**를 한다. 로드 4개 + MAC 명령 2개. LLVM에는 인접한 16-bit MAC 쌍을 찾아 SMLAD로 묶는 ARM 전용 최적화(ARMParallelDSP)가 있는데, 이 Apple clang 21에서는 `-O3`에서만 적용됐다(M7, M33 타깃도 같은 결과). **"컴파일러가 해 주겠지"는 최적화 레벨 하나에 달려 있다.** 임베디드 빌드는 코드 크기 때문에 `-Os`를 많이 쓰는데, `-Os`에서는 루프 펼치기도 없이 `smlabb` 1개짜리 루프가 나왔다.

함정 하나 더: `-O3` 코드의 `ldr.w r5, [r5, #2]`는 `int16_t` 포인터를 32-bit로 읽는다. 주소가 4의 배수라는 보장이 없으므로 **비정렬 접근**이다. M4는 `LDR`의 비정렬 접근을 하드웨어로 지원하지만(느려질 수 있음), 부트 코드에서 `CCR.UNALIGN_TRP`를 켜 두면 UsageFault가 난다. 그런 시스템이라면 `-mno-unaligned-access`로 컴파일해야 한다. Don이 bring-up에서 봤을 법한 종류의 버그다.

### 2.4 intrinsics로 SMLAD를 강제하기

무엇을 확인하나: 최적화 레벨과 상관없이 SMLAD를 쓰게 ACLE intrinsic `__smlad`를 직접 부른다. CMSIS-Core의 `__SMLAD()` 매크로도 결국 같은 명령을 낸다(CMSIS는 컴파일러별로 이걸 builtin이나 inline asm으로 감싼다).

```c
#include <stdint.h>
#include <arm_acle.h>
/* n은 2의 배수라고 가정. 16-bit 두 개를 32-bit 한 워드로 읽어 SMLAD 한 번에 MAC 2개 */
int32_t dot_q15_smlad(const int16_t *a, const int16_t *b, int n)
{
    int32_t acc = 0;
    for (int i = 0; i < n; i += 2) {
        int16x2_t va, vb;
        __builtin_memcpy(&va, &a[i], 4);       /* 정렬 걱정 없는 32-bit 로드 */
        __builtin_memcpy(&vb, &b[i], 4);
        acc = __smlad(va, vb, acc);     /* acc += a0*b0 + a1*b1 */
    }
    return acc;
}
```

`-O2`에서 내부 루프(컴파일러가 4번 펼쳤다):

```text
.LBB0_5:                                @ =>This Inner Loop Header: Depth=1
	ldr	r6, [r0, r4]
	ldr	r7, [r1, r4]
	adds	r5, r0, r4
	adds	r3, r1, r4
	smlad	lr, r6, r7, lr
	ldr.w	r10, [r5, #4]
	ldr	r7, [r5, #8]
	ldr.w	r9, [r5, #12]
	ldr	r5, [r3, #4]
	ldr	r6, [r3, #8]
	ldr	r3, [r3, #12]
	smlad	r5, r10, r5, lr
	smlad	r7, r7, r6, r5
	smlad	lr, r9, r3, r7
	add.w	r8, r8, #8
	subs	r2, #4
	add.w	r4, r4, #16
	bne	.LBB0_5
```

출력에서 볼 것: MAC 8개에 로드 8개 + SMLAD 4개. `-O2`의 순수 C(MAC 4개에 로드 8개 + SMLABB 4개)보다 **명령 수가 MAC당 절반 정도**다. `__builtin_memcpy`로 4바이트를 읽은 것은 C의 strict aliasing 규칙을 지키면서 32-bit 로드를 만드는 관용구다(`int16_t` 포인터를 `uint32_t` 포인터로 캐스팅해서 읽는 건 undefined behavior).

### 2.5 int8은? — SXTB16 두 번 + SMLAD 두 번

M4에는 **int8×int8 SIMD MAC 명령이 없다.** 그래서 int8을 int16으로 넓힌 다음 SMLAD를 쓴다. 넓히는 명령이 `SXTB16`이다: 32-bit 워드의 **바이트 0과 2**를 부호 확장해 16-bit 두 칸으로 만든다. 바이트 1과 3은 먼저 8비트 회전(`ROR #8`)한 뒤 SXTB16을 하면 된다. ARM에서는 `SXTB16 Rd, Rm, ROR #8`처럼 회전을 명령 안에 넣을 수 있어서 회전은 공짜다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<defs>
<marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="22.0" font-size="14" text-anchor="start">Cortex-M4에서 int8 4쌍 → int32: LDR 1번 + SXTB16 2번 + SMLAD 2번 (한쪽 피연산자 기준)</text><text x="20.0" y="62.0" font-size="12" text-anchor="start">32-bit 워드 a (메모리 a[0..3], 리틀엔디언)</text><rect x="20" y="72" width="48" height="30" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/><text x="44.0" y="92.0" font-size="12" text-anchor="middle">a3</text><rect x="68" y="72" width="48" height="30" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/><text x="92.0" y="92.0" font-size="12" text-anchor="middle">a2</text><rect x="116" y="72" width="48" height="30" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/><text x="140.0" y="92.0" font-size="12" text-anchor="middle">a1</text><rect x="164" y="72" width="48" height="30" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/><text x="188.0" y="92.0" font-size="12" text-anchor="middle">a0</text><text x="218.0" y="92.0" font-size="11" text-anchor="start">← bit 0 쪽</text><line x1="80" y1="104" x2="80" y2="150" stroke="currentColor" marker-end="url(#ar)"/><line x1="190" y1="104" x2="390" y2="150" stroke="currentColor" marker-end="url(#ar)"/><text x="90.0" y="135.0" font-size="12" text-anchor="start">SXTB16</text><text x="300.0" y="118.0" font-size="12" text-anchor="start">ROR #8 → SXTB16</text><rect x="20" y="158" width="96" height="30" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/><text x="68.0" y="178.0" font-size="12" text-anchor="middle">a2 (int16)</text><rect x="116" y="158" width="96" height="30" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/><text x="164.0" y="178.0" font-size="12" text-anchor="middle">a0 (int16)</text><rect x="300" y="158" width="96" height="30" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/>
<text x="348.0" y="178.0" font-size="12" text-anchor="middle">a3 (int16)</text><rect x="396" y="158" width="96" height="30" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/><text x="444.0" y="178.0" font-size="12" text-anchor="middle">a1 (int16)</text><text x="44.0" y="212.0" font-size="13" text-anchor="start">SMLAD: acc += a0·b0 + a2·b2</text><text x="324.0" y="212.0" font-size="13" text-anchor="start">SMLAD: acc += a1·b1 + a3·b3</text><line x1="34" y1="190" x2="34" y2="230" stroke="currentColor" marker-end="url(#ar)"/><line x1="314" y1="190" x2="314" y2="230" stroke="currentColor" marker-end="url(#ar)"/><rect x="20" y="234" width="440" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.18" stroke="#3f9a6b"/><text x="240.0" y="258.0" font-size="12" text-anchor="middle">int32 acc  (b도 똑같이 SXTB16 2번)</text><text x="500.0" y="90.0" font-size="12" text-anchor="start">바이트 0,2 = 주황</text><text x="500.0" y="108.0" font-size="12" text-anchor="start">바이트 1,3 = 파랑</text><text x="500.0" y="160.0" font-size="12" text-anchor="start">순서가 섞여도(0,2 / 1,3)</text><text x="500.0" y="178.0" font-size="12" text-anchor="start">합은 같다 → 내적 OK</text><text x="500.0" y="240.0" font-size="12" text-anchor="start">4 MAC당 명령 수:</text><text x="500.0" y="258.0" font-size="12" text-anchor="start">plain C ≈ 12, DSP ≈ 8</text></svg>
```

그림 2 — M4에서 int8 4쌍을 처리하는 방법. 워드 하나를 SXTB16 두 번으로 (바이트 0, 2)와 (바이트 1, 3) 두 레지스터로 나눈다. 짝이 섞였지만 내적은 덧셈 순서와 무관하므로 a와 b를 **같은 방식으로** 나누기만 하면 결과는 같다.

손계산: a = [1, −2, 3, −4] (메모리 순서)를 리틀엔디언 워드로 읽으면 바이트가 `01 FE 03 FC`이므로 워드 값은 `0xFC03FE01`이다.

```
SXTB16(0xFC03FE01)          → 바이트 0 = 0x01 → +1,   바이트 2 = 0x03 → +3   → 0x0003_0001
SXTB16(ROR(0xFC03FE01, 8))  → ROR 결과 0x01FC03FE: 바이트 0 = 0xFE → −2, 바이트 2 = 0xFC → −4 → 0xFFFC_FFFE
```

무엇을 확인하나: 이 트릭이 정말 bit-exact인지, SXTB16·SMLAD의 동작을 C로 흉내 내서 이 Mac에서 돌려 본다(M4 실물이 없어도 알고리즘은 검증할 수 있다 — 펌웨어에서 host-side 모델을 먼저 만드는 것과 같다).

```c
/* M4의 SXTB16 / SMLAD를 C로 흉내 내서 int8 내적 트릭이 정확한지 Mac에서 확인 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t ror32(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }
static uint32_t sxtb16(uint32_t x)           /* 바이트 0, 2를 부호 확장해 int16 두 개로 */
{
    uint16_t lo = (uint16_t)(int16_t)(int8_t)(x & 0xFF);
    uint16_t hi = (uint16_t)(int16_t)(int8_t)((x >> 16) & 0xFF);
    return ((uint32_t)hi << 16) | lo;
}
static int32_t smlad(uint32_t x, uint32_t y, int32_t acc)   /* acc + x.lo*y.lo + x.hi*y.hi */
{
    return acc + (int16_t)(x & 0xFFFF) * (int16_t)(y & 0xFFFF) + (int16_t)(x >> 16) * (int16_t)(y >> 16);
}
int main(void)
{
    int8_t a[8] = {1, -2, 3, -4, 127, -128, 50, -60}, b[8] = {5, 6, -7, 8, 127, -128, -3, 2};
    int32_t ref = 0, dsp = 0;
    for (int i = 0; i < 8; i++) ref += a[i] * b[i];
    for (int i = 0; i < 8; i += 4) {
        uint32_t wa, wb; memcpy(&wa, a + i, 4); memcpy(&wb, b + i, 4);   /* 리틀엔디언 워드 */
        dsp = smlad(sxtb16(wa), sxtb16(wb), dsp);                        /* 바이트 0,2 */
        dsp = smlad(sxtb16(ror32(wa, 8)), sxtb16(ror32(wb, 8)), dsp);    /* 바이트 1,3 */
    }
    printf("wa(0..3)=0x%08X  sxtb16=0x%08X  sxtb16(ror8)=0x%08X\n", 0xFC03FE01u, sxtb16(0xFC03FE01u), sxtb16(ror32(0xFC03FE01u, 8)));
    printf("ref=%d  dsp=%d  %s\n", ref, dsp, ref == dsp ? "match" : "MISMATCH");
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 emu_m4.c -o emu_m4 && ./emu_m4
```

```text
wa(0..3)=0xFC03FE01  sxtb16=0x00030001  sxtb16(ror8)=0xFFFCFFFE
ref=32183  dsp=32183  match
```

출력에서 볼 것: 손계산과 같은 `0x00030001`, `0xFFFCFFFE`가 나오고, 극단값(127·127, −128·−128)을 포함한 8개 원소 내적이 평범한 C 결과(32183)와 정확히 같다.

이제 진짜 M4 intrinsics 버전을 컴파일해 본다.

```c
#include <stdint.h>
#include <arm_acle.h>
/* int8 내적, M4 DSP 확장 방식: 4바이트 워드 → SXTB16으로 int16 두 쌍 → SMLAD 두 번 */
int32_t dot_s8_dsp(const int8_t *a, const int8_t *b, int n)   /* n % 4 == 0 */
{
    int32_t acc = 0;
    for (int i = 0; i < n; i += 4) {
        int8x4_t wa, wb;
        __builtin_memcpy(&wa, &a[i], 4);
        __builtin_memcpy(&wb, &b[i], 4);
        int16x2_t a02 = __sxtb16(wa);                 /* 바이트 0,2 → int16 */
        int16x2_t a13 = __sxtb16(__ror((uint32_t)wa, 8)); /* 바이트 1,3 → int16 */
        int16x2_t b02 = __sxtb16(wb);
        int16x2_t b13 = __sxtb16(__ror((uint32_t)wb, 8));
        acc = __smlad(a02, b02, acc);                 /* a0b0 + a2b2 */
        acc = __smlad(a13, b13, acc);                 /* a1b1 + a3b3 */
    }
    return acc;
}
/* 비교용: 평범한 C */
int32_t dot_s8_c(const int8_t *a, const int8_t *b, int n)
{
    int32_t acc = 0;
    for (int i = 0; i < n; i++) acc += a[i] * b[i];
    return acc;
}
```

`dot_s8_dsp`의 `-O2` 내부 루프 앞부분(4번 펼쳐져 MAC 16개가 한 반복이다):

```text
.LBB0_5:                                @ =>This Inner Loop Header: Depth=1
	ldr	r5, [r2, #16]!
	adds	r6, r1, r4
	sxtb16	r9, r5
	sxtb16	r8, r5, ror #8
	ldr	r5, [r6, #16]
	ldr	r7, [r6, #20]
	ldr	r3, [r6, #24]
	ldr.w	r11, [r6, #28]
	sxtb16	r6, r5
	sxtb16	r5, r5, ror #8
	smlad	r6, r9, r6, lr
	smlad	lr, r8, r5, r6
	ldr	r5, [r2, #4]
	...
```

한 반복 전체의 명령을 세면 `ldr` 8개, `sxtb16` 16개, `smlad` 8개 + 루프 관리 4개 = MAC 16개에 36개. 평범한 C 버전 `dot_s8_c`는 `-O2`, `-O3` 모두 `ldrsb`(바이트 부호 확장 로드) + `smlabb`로만 나왔다 — **int8 C 루프를 SXTB16+SMLAD로 바꿔 주는 컴파일러 최적화는 이번 실험에서 한 번도 나오지 않았다.**

| 방식 (M4, MAC 4개당) | 로드 | 확장 | MAC 명령 | 합계 |
|---|---|---|---|---|
| 순수 C (`ldrsb` + `smlabb`) | 8 | 0 (로드가 확장) | 4 | 12 |
| DSP (`ldr` + `sxtb16` + `smlad`) | 2 | 4 | 2 | 8 |
| DSP + 한쪽을 미리 int16으로 확장해 둠 | 1 + 1 (int16 워드 2개) | 2 | 2 | 6 |

마지막 행이 CMSIS-NN이 실제로 쓰는 아이디어다(다음 절).

### 2.6 CMSIS-NN이 M4에서 하는 일 (일반적인 수준)

CMSIS-NN(C1 11.2절)은 Arm이 Cortex-M용으로 만든 int8/int16 신경망 커널 라이브러리다. 2018년 논문(Lai, Suda, Chandra, "CMSIS-NN: Efficient Neural Network Kernels for Arm Cortex-M CPUs")과 소스에서 확인되는 DSP 경로의 뼈대:

1. **확장을 한 번만 한다**: conv는 im2col(입력 패치를 한 줄로 펴기)로 GEMM이 되는데, 입력 패치를 **int16으로 넓힌 버퍼**에 한 번 복사해 둔다. 이 버퍼는 여러 출력 채널(가중치 행)과 곱해지므로 확장 비용이 나뉜다(amortize).
2. **가중치 쪽만 실시간 확장**: int8 가중치 워드를 읽어 SXTB16(+ROR)으로 두 레지스터로 나누고, int16 입력 워드와 SMLAD.
3. **레지스터 블로킹**: 한 번에 출력 2채널 × 입력 2열 같은 작은 타일을 계산해서 로드한 레지스터를 여러 번 재사용한다(5절에서 NEON으로 같은 효과를 잰다).
4. **requantization은 C1 7절 그대로**: int32 누산 → per-channel multiplier·shift → zero-point → clamp.

함수 이름(`arm_nn_mat_mult_kernel_s8_s16` 같은)과 세부 구조는 버전마다 바뀌므로, 실제로 쓸 땐 저장소(github.com/ARM-software/CMSIS-NN) 소스를 보고 확인한다. 면접에서 말할 핵심은 **"M4에는 int8 SIMD MAC이 없어서 int16으로 넓혀 SMLAD를 쓰고, 넓히는 비용을 재사용으로 나눈다"** 이다.

### 2.7 M0+와 M33

- **M0+**: DSP 확장이 없다. 같은 int16 내적을 `-mcpu=cortex-m0plus`로 컴파일하면 `ldrsh`/`muls` + 레지스터 부족으로 인한 스택 spill(`str ... @ 4-byte Spill`)이 보인다. MAC 1개에 명령 여러 개. Don이 M0+를 부트 헬퍼로 썼다면, 그 코어에 ML을 올리는 건 거의 항상 나쁜 선택이다.
- **M33**: DSP 확장이 **있으면** M4와 같은 명령을 쓴다(`-mcpu=cortex-m33`으로 위 `dot8_m4.c`를 컴파일하면 `sxtb16` 28개, `smlad` 14개가 나왔다). 없으면(`+nodsp`) SIMD32 타입 자체가 정의되지 않는다.

### 2.8 함정

- **SMLAD 오버플로**: SMLAD의 32-bit 누산은 포화하지 않고 wrap하며, 오버플로 시 APSR의 Q 플래그만 세운다. int16×int16 곱 하나가 최대 2³⁰이므로 긴 누산은 SMLALD(64-bit)로 하거나 K를 제한한다. int8을 넓힌 경우는 곱 하나가 최대 2¹⁴라 여유가 크다.
- **엔디언**: SXTB16 트릭의 "바이트 0 = 메모리 첫 원소"는 리틀엔디언 가정이다. Cortex-M은 거의 항상 리틀엔디언이지만 구성상 빅엔디언도 가능하다.
- **정렬**: 2.3절처럼 32-bit 로드가 비정렬일 수 있다. `LDRD`/`LDM`은 비정렬을 지원하지 않는다.
- **짝 순서**: a와 b를 같은 방식으로 섞으면 내적은 맞지만, **출력 벡터를 만드는 연산**(예: elementwise 곱)에서는 섞인 순서를 되돌려야 한다.

---

## 3. Helium (MVE) — Cortex-M55/M85의 128-bit 벡터

### 3.1 직관 — "MCU용 NEON"이지만 설계 목표가 다르다

Helium은 Armv8.1-M의 **M-profile Vector Extension (MVE)** 의 마케팅 이름이다. 128-bit 벡터로 int8 16개, int16 8개, int32 4개, (MVE-FP 구성이면) fp16 8개, fp32 4개를 한 명령으로 다룬다.

NEON과 비슷해 보이지만 MCU답게 **면적과 전력**을 먼저 생각한 설계다.

| 항목 | NEON (A-profile) | Helium (M-profile) |
|---|---|---|
| 벡터 레지스터 | V0~V31, 32개 × 128-bit, 전용 | **Q0~Q7, 8개** × 128-bit, **FPU 레지스터 파일(S0~S31)을 공유** |
| 실행 폭 | 코어에 따라 128-bit 파이프 여러 개 | 128-bit를 **beat**(32-bit 조각) 단위로 1·2·4 beat/tick 실행 (구현 선택) |
| 루프 | 일반 분기 | **low-overhead loop**(DLS/WLS/LE)와 **tail predication**(DLSTP/LETP) |
| predication | 없음(SVE에는 있음) | **VPT/VPST 블록**, `VCTP`로 lane 마스크 |
| 스칼라로 바로 누산 | ADDV 등 가로 합을 따로 | **VMLADAV**: 벡터 곱-합을 범용 레지스터에 바로 누산 |

Q 레지스터가 8개뿐이라서 5절에서 볼 "누산기 16개" 같은 큰 레지스터 블로킹은 Helium에서 불가능하다. 대신 VMLADAV처럼 범용 레지스터(r0~r12)에 누산하는 명령이 레지스터 압박을 덜어 준다.

### 3.2 Beat와 instruction overlap

128-bit 명령 하나를 32-bit 조각 4개(**beat** A, B, C, D)로 나눈다. 구현은 한 tick(cycle)에 beat 1개, 2개, 4개 중 하나를 처리한다. M55는 **dual-beat**(tick당 2 beat)로 소개된다. 그러면 128-bit 명령 하나가 2 tick 걸린다. 여기서 끝나면 NEON의 절반 속도일 뿐인데, Helium은 **서로 다른 종류의 명령을 겹친다**: 로드의 뒤쪽 beat와 MAC의 앞쪽 beat를 같은 tick에 실행한다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 350">
<defs>
<marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="22.0" font-size="14" text-anchor="start">Helium: 128-bit 명령 = beat 4개(A B C D, 각 32-bit), dual-beat 코어는 한 tick에 2 beat</text><text x="165.0" y="50.0" font-size="12" text-anchor="middle">tick 1</text><text x="255.0" y="50.0" font-size="12" text-anchor="middle">tick 2</text><text x="345.0" y="50.0" font-size="12" text-anchor="middle">tick 3</text><text x="435.0" y="50.0" font-size="12" text-anchor="middle">tick 4</text><text x="525.0" y="50.0" font-size="12" text-anchor="middle">tick 5</text><text x="110.0" y="81.0" font-size="12" text-anchor="end">VLDRB q1</text><rect x="122" y="62" width="86" height="26" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/><text x="165.0" y="80.0" font-size="12" text-anchor="middle">A B</text><rect x="212" y="62" width="86" height="26" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/><text x="255.0" y="80.0" font-size="12" text-anchor="middle">C D</text><text x="110.0" y="115.0" font-size="12" text-anchor="end">VMLADAVA</text><rect x="212" y="96" width="86" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/><text x="255.0" y="114.0" font-size="12" text-anchor="middle">A B</text><rect x="302" y="96" width="86" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/><text x="345.0" y="114.0" font-size="12" text-anchor="middle">C D</text><text x="110.0" y="149.0" font-size="12" text-anchor="end">VLDRB q1</text><rect x="302" y="130" width="86" height="26" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/><text x="345.0" y="148.0" font-size="12" text-anchor="middle">A B</text><rect x="392" y="130" width="86" height="26" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<text x="435.0" y="148.0" font-size="12" text-anchor="middle">C D</text><text x="110.0" y="183.0" font-size="12" text-anchor="end">VMLADAVA</text><rect x="392" y="164" width="86" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/><text x="435.0" y="182.0" font-size="12" text-anchor="middle">A B</text><rect x="482" y="164" width="86" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/><text x="525.0" y="182.0" font-size="12" text-anchor="middle">C D</text><text x="120.0" y="215.0" font-size="12" text-anchor="start">→ 로드의 뒤쪽 beat(C D)와 MAC의 앞쪽 beat(A B)가 같은 tick에 겹친다 (instruction overlap)</text><text x="10.0" y="250.0" font-size="13" text-anchor="start">Tail predication: n = 20 (int8) → 반복 1: 16 lane 활성, 반복 2: 4 lane만 활성 (나머지 12는 끔)</text><text x="110.0" y="281.0" font-size="12" text-anchor="end">반복 1</text><rect x="120" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="135.5" y="280.0" font-size="11" text-anchor="middle">0</text><rect x="154" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="169.5" y="280.0" font-size="11" text-anchor="middle">1</text><rect x="188" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="203.5" y="280.0" font-size="11" text-anchor="middle">2</text><rect x="222" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="237.5" y="280.0" font-size="11" text-anchor="middle">3</text><rect x="256" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="271.5" y="280.0" font-size="11" text-anchor="middle">4</text><rect x="290" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="305.5" y="280.0" font-size="11" text-anchor="middle">5</text><rect x="324" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="339.5" y="280.0" font-size="11" text-anchor="middle">6</text><rect x="358" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="373.5" y="280.0" font-size="11" text-anchor="middle">7</text><rect x="392" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="407.5" y="280.0" font-size="11" text-anchor="middle">8</text><rect x="426" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="441.5" y="280.0" font-size="11" text-anchor="middle">9</text><rect x="460" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="475.5" y="280.0" font-size="11" text-anchor="middle">10</text><rect x="494" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="509.5" y="280.0" font-size="11" text-anchor="middle">11</text><rect x="528" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="543.5" y="280.0" font-size="11" text-anchor="middle">12</text><rect x="562" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="577.5" y="280.0" font-size="11" text-anchor="middle">13</text><rect x="596" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="611.5" y="280.0" font-size="11" text-anchor="middle">14</text><rect x="630" y="262" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="645.5" y="280.0" font-size="11" text-anchor="middle">15</text><text x="110.0" y="319.0" font-size="12" text-anchor="end">반복 2</text><rect x="120" y="300" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="135.5" y="318.0" font-size="11" text-anchor="middle">16</text><rect x="154" y="300" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="169.5" y="318.0" font-size="11" text-anchor="middle">17</text><rect x="188" y="300" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="203.5" y="318.0" font-size="11" text-anchor="middle">18</text>
<rect x="222" y="300" width="31" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="237.5" y="318.0" font-size="11" text-anchor="middle">19</text><rect x="256" y="300" width="31" height="26" fill="#888" fill-opacity="0.08" stroke="#888"/><text x="271.5" y="318.0" font-size="11" text-anchor="middle">–</text><rect x="290" y="300" width="31" height="26" fill="#888" fill-opacity="0.08" stroke="#888"/><text x="305.5" y="318.0" font-size="11" text-anchor="middle">–</text><rect x="324" y="300" width="31" height="26" fill="#888" fill-opacity="0.08" stroke="#888"/><text x="339.5" y="318.0" font-size="11" text-anchor="middle">–</text><rect x="358" y="300" width="31" height="26" fill="#888" fill-opacity="0.08" stroke="#888"/><text x="373.5" y="318.0" font-size="11" text-anchor="middle">–</text><rect x="392" y="300" width="31" height="26" fill="#888" fill-opacity="0.08" stroke="#888"/><text x="407.5" y="318.0" font-size="11" text-anchor="middle">–</text><rect x="426" y="300" width="31" height="26" fill="#888" fill-opacity="0.08" stroke="#888"/><text x="441.5" y="318.0" font-size="11" text-anchor="middle">–</text><rect x="460" y="300" width="31" height="26" fill="#888" fill-opacity="0.08" stroke="#888"/><text x="475.5" y="318.0" font-size="11" text-anchor="middle">–</text><rect x="494" y="300" width="31" height="26" fill="#888" fill-opacity="0.08" stroke="#888"/><text x="509.5" y="318.0" font-size="11" text-anchor="middle">–</text><rect x="528" y="300" width="31" height="26" fill="#888" fill-opacity="0.08" stroke="#888"/><text x="543.5" y="318.0" font-size="11" text-anchor="middle">–</text><rect x="562" y="300" width="31" height="26" fill="#888" fill-opacity="0.08" stroke="#888"/><text x="577.5" y="318.0" font-size="11" text-anchor="middle">–</text><rect x="596" y="300" width="31" height="26" fill="#888" fill-opacity="0.08" stroke="#888"/><text x="611.5" y="318.0" font-size="11" text-anchor="middle">–</text>
<rect x="630" y="300" width="31" height="26" fill="#888" fill-opacity="0.08" stroke="#888"/><text x="645.5" y="318.0" font-size="11" text-anchor="middle">–</text></svg>
```

그림 3 — 위: dual-beat 구현에서 VLDRB(로드)와 VMLADAVA(곱-합)가 beat 단위로 겹치는 모습(Arm이 공개한 Helium 소개 자료의 개념을 단순화한 그림 — 실제 겹침 규칙과 제약은 코어 TRM에 있다). 아래: 원소 20개짜리 int8 루프에서 두 번째 반복은 lane 4개만 활성이다(tail predication).

throughput 산수(이론치, 가정 명시): dual-beat면 tick당 32-bit beat 2개 = int8 8개. VMLADAV가 매 tick 2 beat씩 흘러가면 **int8 MAC 8개/cycle**이다. M4의 SMLAD 경로(int16 MAC 2개/cycle, int8은 확장 비용 때문에 그보다 낮음)보다 4배 이상이다. 실제 커널은 로드·requantization·루프 오버헤드 때문에 이보다 낮다.

### 3.3 Low-overhead loop와 tail predication

Armv8.1-M은 루프 전용 명령을 추가했다.

- `DLS lr, rN` (Do-Loop Start): 반복 횟수를 LR에 넣고 루프 시작.
- `WLS lr, rN, label` (While-Loop Start): 0회면 건너뛰는 버전.
- `LE lr, label` (Loop End): LR을 줄이고 0이 아니면 되돌아간다. 하드웨어가 루프 끝 주소를 기억해서 **분기 비용을 거의 없앤다.**
- `DLSTP.8 lr, rN` / `LETP`: 위의 **tail-predicated** 버전. rN은 반복 횟수가 아니라 **원소 개수**다. 마지막 반복에서 남은 원소 수만큼만 lane을 켜 준다.

Don의 경험과 연결: Xtensa의 `LOOP` 명령(zero-overhead loop, LBEG/LEND/LCOUNT 레지스터)과 같은 발상이다. DSP들이 오래전부터 가진 기능(E4에서 Hexagon의 hardware loop도 본다)을 Cortex-M이 Armv8.1-M에서 들여온 것이다. tail predication은 거기에 "남는 원소 처리 코드(epilogue)가 필요 없음"을 더한다. NEON 코드의 전형적인 "16으로 나눈 몫은 벡터로, 나머지는 스칼라로" 이중 구조가 사라진다.

### 3.4 코드로 확인 — MVE intrinsics int8 내적

무엇을 확인하나: ACLE MVE intrinsics(`arm_mve.h`)로 int8 내적을 쓰고, M55 타깃 어셈블리에서 tail-predicated loop가 나오는지 본다. 이 Apple clang 21은 `arm_mve.h`를 포함하고 있었고 `-mcpu=cortex-m55`, `-march=armv8.1-m.main+mve`, `-march=armv8.1-m.main+mve.fp` 모두 컴파일됐다(실행은 불가 — 어셈블리만 확인).

```c
#include <stdint.h>
#include <arm_mve.h>
/* int8 내적 — Helium(MVE) intrinsics. n은 아무 값이나 OK: tail은 predication으로 */
int32_t dot_s8_mve(const int8_t *a, const int8_t *b, int n)
{
    int32_t acc = 0;
    while (n > 0) {
        mve_pred16_t p = vctp8q((uint32_t)n);   /* 남은 원소 수만큼만 lane 활성 */
        int8x16_t va = vldrbq_z_s8(a, p);        /* 비활성 lane은 0 */
        int8x16_t vb = vldrbq_z_s8(b, p);
        acc = vmladavaq_p_s8(acc, va, vb, p);   /* acc += Σ va[i]·vb[i] (활성 lane만) */
        a += 16; b += 16; n -= 16;
    }
    return acc;
}
```

```sh
cc --target=thumbv8.1m.main-none-eabihf -mcpu=cortex-m55 -mfloat-abi=hard -O2 -Wall -Wextra -S dot8_mve.c -o dot8_mve.s
```

```text
dot_s8_mve:                             @ @dot_s8_mve
	push	{r7, lr}
	cmp	r2, #1
	blt	.LBB0_4
	mov.w	r12, #0
	dlstp.8	lr, r2
.LBB0_2:                                @ =>This Inner Loop Header: Depth=1
	vldrb.u8	q0, [r0], #16
	vldrb.u8	q1, [r1], #16
	vmlava.s8	r12, q0, q1
	letp	lr, .LBB0_2
	mov	r0, r12
	pop	{r7, pc}
.LBB0_4:
	mov.w	r12, #0
	mov	r0, r12
	pop	{r7, pc}
```

출력에서 볼 것:

- `dlstp.8 lr, r2`: r2(원소 개수 n)로 **8-bit 원소 tail-predicated loop** 시작. 반복 횟수 계산(`(n+15)/16`)도, 나머지 처리 루프도 없다.
- 루프 본체가 **명령 4개**: 로드 2개(`vldrb.u8`, post-increment `[r0], #16`), 곱-합 1개, 루프 끝 1개. MAC 16개에 명령 4개.
- `vmlava.s8 r12, q0, q1`: 어셈블러가 `VMLADAVA`를 `VMLAVA`라는 별칭으로 출력한 것이다. r12(범용 레지스터)에 16쌍 곱의 합을 누산한다.
- 코드에 쓴 `vctp8q` / `vldrbq_z_s8` / `vmladavaq_p_s8`(predicate를 명시한 버전)가 **사라졌다**: 컴파일러가 "이 predicate는 루프의 tail predication과 같다"는 걸 알아보고 DLSTP/LETP로 바꿨다. 로드는 `.u8`로 나왔지만 8-bit lane에 8-bit를 채우는 것이라 부호와 무관하다(부호는 `vmlava.s8`에서 쓴다).

### 3.5 같은 것을 평범한 C로 — 자동 벡터화

```c
#include <stdint.h>
int32_t dot_s8_c(const int8_t *a, const int8_t *b, int n)
{
    int32_t acc = 0;
    for (int i = 0; i < n; i++) acc += a[i] * b[i];
    return acc;
}
```

```sh
cc --target=thumbv8.1m.main-none-eabihf -mcpu=cortex-m55 -mfloat-abi=hard -O2 -S dot8c.c -o dot8c_m55.s -Rpass=loop-vectorize
```

```text
dot8c.c:5:5: remark: vectorized loop (vectorization width: 16, interleaved count: 1) [-Rpass=loop-vectorize]
    5 |     for (int i = 0; i < n; i++) acc += a[i] * b[i];
      |     ^
```

생성된 루프는 intrinsics 버전과 **같은 4개 명령**(`dlstp.8` / `vldrb.u8` ×2 / `vmlava.s8` / `letp`)이었다. M4에서는 int8 C 루프가 절대 SMLAD로 안 바뀌었던 것과 대조적이다. Helium은 "컴파일러가 쓰기 쉬운 벡터 ISA"로 설계됐다는 Arm의 설명과 맞는 결과다. 단, 6.3절에서 보듯 **fp32 누산은 `-ffast-math` 없이는 벡터화되지 않는다.**

### 3.6 VMLADAV 가족 정리

| 명령 | 뜻 | 결과 위치 |
|---|---|---|
| `VMLADAV{A}.s8/s16/s32` | 벡터 곱의 합(across) {+ 누산} | 32-bit 범용 레지스터 1개 |
| `VMLALDAV{A}.s16/s32` | 위와 같은데 64-bit 누산 | 범용 레지스터 2개(RdaLo, RdaHi) |
| `VMLADAVX` | 짝을 교차(exchange)해서 곱 — 복소수 곱에 쓰임 | 범용 레지스터 |
| `VMLA` / `VMLAS` | lane별 곱-누산(벡터 결과) | Q 레지스터 |
| `VQDMULH`, `VQRDMULH` | 포화 doubling high multiply (C1의 SRDHM) | Q 레지스터 |

`VQRDMULH`가 C1 7.4절의 `SaturatingRoundingDoublingHighMul`을 lane 단위로 하는 명령이라서, Helium에서는 requantization도 벡터로 한다.

### 3.7 함정

- **FPU와 레지스터 공유**: Q0~Q7은 S0~S31(D0~D15)과 같은 물리 레지스터다. RTOS의 context switch에서 FPU lazy stacking을 켜 둔 경우, Helium 코드도 "FP 컨텍스트를 쓴 스레드"로 취급된다. 스택 프레임 크기와 인터럽트 지연이 달라진다 — Don이 R8에서 VFP 컨텍스트 저장을 다뤄 봤다면 같은 종류의 문제다.
- **MVE는 구성 옵션**: M55는 MVE 없음 / 정수 MVE / 정수+FP MVE로 구성될 수 있다. 칩 데이터시트에서 확인하고 `-mcpu=cortex-m55+nomve.fp` 같은 수식어로 맞춘다.
- **레지스터 8개**: NEON 코드를 그대로 옮기면 spill이 난다. 블로킹 크기를 작게 잡는다.
- **시뮬레이션**: 실물 없이 Helium 코드를 **실행**하려면 Arm의 Fixed Virtual Platform(FVP, Corstone-300 등)이나 QEMU의 해당 머신 지원을 써야 한다. 이 노트는 어셈블리 확인까지만 했다.


---

## 4. NEON (Advanced SIMD) — Cortex-A와 이 Mac의 int8 명령

### 4.1 레지스터와 lane 표기

AArch64 NEON은 **128-bit 레지스터 V0~V31**(32개)을 가진다. 같은 레지스터를 원소 크기에 따라 다르게 부른다.

```
V0 (128 bit)
 .16b  │b15│b14│b13│b12│b11│b10│b9 │b8 │b7 │b6 │b5 │b4 │b3 │b2 │b1 │b0 │  int8 × 16
 .8h   │  h7   │  h6   │  h5   │  h4   │  h3   │  h2   │  h1   │  h0   │  int16 × 8
 .4s   │      s3       │      s2       │      s1       │      s0       │  int32/fp32 × 4
 .2d   │              d1               │              d0               │  int64/fp64 × 2
 하위 64비트만 쓰면 .8b / .4h / .2s  (intrinsic 타입: int8x8_t 등)
```

intrinsic 타입 이름이 이 표기를 그대로 따른다: `int8x16_t`(= `.16b`), `int16x8_t`(= `.8h`), `int32x4_t`(= `.4s`). 함수 이름 규칙은 `v` + 연산 + (`q` = 128-bit 전체) + `_` + 원소 타입. 예: `vdotq_s32` = 128-bit, 결과 원소 s32인 dot. Apple 어셈블러는 `sdot.4s v0, v4, v2`처럼 모양을 명령 뒤에 붙이고, GNU/리눅스 문법은 `sdot v0.4s, v4.16b, v2.16b`처럼 레지스터 뒤에 붙인다 — 같은 명령이다.

### 4.2 int8 연산 명령 가족

| 명령 (intrinsic) | 입력 → 출력 | 한 명령의 int8 MAC | 필요한 확장 |
|---|---|---|---|
| `SMULL`/`SMULL2` (`vmull_s8`, `vmull_high_s8`) | int8 × 8 lane → int16 × 8 (곱만, 넓히기) | 8 (곱) | 기본 NEON |
| `SMLAL`/`SMLAL2` (`vmlal_s8`) | int16 += int8·int8 (넓히며 누산) | 8 | 기본 NEON |
| `SADALP` (`vpadalq_s16`) | int16 이웃 쌍을 더해 int32에 누산 | (덧셈만) | 기본 NEON |
| **`SDOT`** (`vdotq_s32`) | lane마다 int8 4쌍 내적 → int32 누산 | **16** | FEAT_DotProd (`+dotprod`) |
| `SDOT` (by element) (`vdotq_laneq_s32`) | 한쪽을 4바이트 그룹 하나로 broadcast | 16 | FEAT_DotProd |
| `USDOT` (`vusdotq_s32`) | uint8 × int8 혼합 | 16 | FEAT_I8MM |
| **`SMMLA`** (`vmmlaq_s32`) | (2×8 int8) · (8×2 int8) → 2×2 int32 누산 | **32** | FEAT_I8MM (`+i8mm`) |
| `BFDOT` (`vbfdotq_f32`) | bf16 2쌍 내적 → fp32 누산, lane 4개 | (bf16 8곱) | FEAT_BF16 |
| `BFMMLA` (`vbfmmlaq_f32`) | (2×4 bf16) · (4×2 bf16) → 2×2 fp32 | (bf16 16곱) | FEAT_BF16 |

SDOT 이전의 Armv8.0 코어(A53 등)에서는 SMULL(넓히며 곱) → SADALP 또는 SADDW(넓히며 더하기)의 2단계가 필요했다. SDOT은 이걸 한 명령으로 하고, int16 중간값을 레지스터에 두지 않아서 명령 수와 레지스터를 모두 아낀다.

`USDOT`이 필요한 이유: TFLite 관례에서 activation은 uint8(비대칭)일 때가 있고 weight는 int8(대칭)이다(C1 4절). 부호가 다른 둘을 곱하려면 SDOT·UDOT으로는 한쪽에 128을 빼는 보정이 필요했다.

### 4.3 SDOT — lane 하나 = int8 4쌍

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<defs>
<marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="22.0" font-size="14" text-anchor="start">SDOT Vd.4S, Vn.16B, Vm.16B — 32-bit lane 하나 = int8 4쌍의 내적 + 기존 값</text><text x="52.0" y="69.0" font-size="13" text-anchor="end">Vn</text><rect x="60" y="50" width="36" height="28" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="78.0" y="69.0" font-size="12" text-anchor="middle">1</text><rect x="98" y="50" width="36" height="28" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="116.0" y="69.0" font-size="12" text-anchor="middle">2</text><rect x="136" y="50" width="36" height="28" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="154.0" y="69.0" font-size="12" text-anchor="middle">3</text><rect x="174" y="50" width="36" height="28" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="192.0" y="69.0" font-size="12" text-anchor="middle">4</text><rect x="212" y="50" width="36" height="28" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="230.0" y="69.0" font-size="12" text-anchor="middle">5</text><rect x="250" y="50" width="36" height="28" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="268.0" y="69.0" font-size="12" text-anchor="middle">6</text><rect x="288" y="50" width="36" height="28" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="306.0" y="69.0" font-size="12" text-anchor="middle">7</text><rect x="326" y="50" width="36" height="28" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="344.0" y="69.0" font-size="12" text-anchor="middle">8</text><rect x="364" y="50" width="36" height="28" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="382.0" y="69.0" font-size="12" text-anchor="middle">9</text>
<rect x="402" y="50" width="36" height="28" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="420.0" y="69.0" font-size="12" text-anchor="middle">10</text><rect x="440" y="50" width="36" height="28" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="458.0" y="69.0" font-size="12" text-anchor="middle">11</text><rect x="478" y="50" width="36" height="28" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="496.0" y="69.0" font-size="12" text-anchor="middle">12</text><rect x="516" y="50" width="36" height="28" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="534.0" y="69.0" font-size="12" text-anchor="middle">13</text><rect x="554" y="50" width="36" height="28" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="572.0" y="69.0" font-size="12" text-anchor="middle">14</text><rect x="592" y="50" width="36" height="28" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="610.0" y="69.0" font-size="12" text-anchor="middle">15</text><rect x="630" y="50" width="36" height="28" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="648.0" y="69.0" font-size="12" text-anchor="middle">16</text><text x="52.0" y="111.0" font-size="13" text-anchor="end">Vm</text><rect x="60" y="92" width="36" height="28" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="78.0" y="111.0" font-size="12" text-anchor="middle">1</text><rect x="98" y="92" width="36" height="28" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="116.0" y="111.0" font-size="12" text-anchor="middle">0</text><rect x="136" y="92" width="36" height="28" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="154.0" y="111.0" font-size="12" text-anchor="middle">0</text><rect x="174" y="92" width="36" height="28" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="192.0" y="111.0" font-size="12" text-anchor="middle">0</text><rect x="212" y="92" width="36" height="28" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/>
<text x="230.0" y="111.0" font-size="12" text-anchor="middle">1</text><rect x="250" y="92" width="36" height="28" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="268.0" y="111.0" font-size="12" text-anchor="middle">0</text><rect x="288" y="92" width="36" height="28" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="306.0" y="111.0" font-size="12" text-anchor="middle">0</text><rect x="326" y="92" width="36" height="28" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="344.0" y="111.0" font-size="12" text-anchor="middle">0</text><rect x="364" y="92" width="36" height="28" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="382.0" y="111.0" font-size="12" text-anchor="middle">1</text><rect x="402" y="92" width="36" height="28" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="420.0" y="111.0" font-size="12" text-anchor="middle">0</text><rect x="440" y="92" width="36" height="28" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="458.0" y="111.0" font-size="12" text-anchor="middle">0</text><rect x="478" y="92" width="36" height="28" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="496.0" y="111.0" font-size="12" text-anchor="middle">0</text><rect x="516" y="92" width="36" height="28" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="534.0" y="111.0" font-size="12" text-anchor="middle">1</text><rect x="554" y="92" width="36" height="28" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="572.0" y="111.0" font-size="12" text-anchor="middle">0</text><rect x="592" y="92" width="36" height="28" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="610.0" y="111.0" font-size="12" text-anchor="middle">0</text><rect x="630" y="92" width="36" height="28" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="648.0" y="111.0" font-size="12" text-anchor="middle">0</text><line x1="135" y1="124" x2="135" y2="168" stroke="currentColor" marker-end="url(#ar)"/>
<text x="141.0" y="150.0" font-size="11" text-anchor="start">Σ 4곱</text><line x1="287" y1="124" x2="287" y2="168" stroke="currentColor" marker-end="url(#ar)"/><text x="293.0" y="150.0" font-size="11" text-anchor="start">Σ 4곱</text><line x1="439" y1="124" x2="439" y2="168" stroke="currentColor" marker-end="url(#ar)"/><text x="445.0" y="150.0" font-size="11" text-anchor="start">Σ 4곱</text><line x1="591" y1="124" x2="591" y2="168" stroke="currentColor" marker-end="url(#ar)"/><text x="597.0" y="150.0" font-size="11" text-anchor="start">Σ 4곱</text><rect x="60" y="172" width="150" height="34" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="135.0" y="194.0" font-size="12" text-anchor="middle">lane0: 100 + 1 = 101</text><rect x="212" y="172" width="150" height="34" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="287.0" y="194.0" font-size="12" text-anchor="middle">lane1: 100 + 5 = 105</text><rect x="364" y="172" width="150" height="34" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="439.0" y="194.0" font-size="12" text-anchor="middle">lane2: 100 + 9 = 109</text><rect x="516" y="172" width="150" height="34" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="591.0" y="194.0" font-size="12" text-anchor="middle">lane3: 100 + 13 = 113</text><text x="52.0" y="194.0" font-size="13" text-anchor="end">Vd</text><text x="60.0" y="236.0" font-size="12" text-anchor="start">명령 1개 = int8 곱 16개 + 누산. lane끼리는 섞이지 않는다 → 마지막에 가로 합(ADDV) 한 번</text><text x="60.0" y="256.0" font-size="12" text-anchor="start">최악의 lane 값: 4 × (−128)·(−128) = 65,536 → int32 누산 여유 충분 (그래도 긴 K에서 오버플로 계산은 필요)</text><text x="60.0" y="276.0" font-size="12" text-anchor="start">예: Vn = 1..16, Vm = [1,0,0,0] 반복, 누산기 초기값 100 (예제 lanes.c의 실제 출력)</text></svg>
```

그림 4 — `SDOT Vd.4S, Vn.16B, Vm.16B`. 16바이트를 4바이트씩 4그룹으로 나누고, 그룹 k의 4쌍 곱의 합을 Vd의 lane k(int32)에 더한다. lane끼리는 섞이지 않으므로 내적 전체를 원하면 마지막에 `ADDV`(가로 합)를 한 번 한다.

손계산(그림의 숫자): Vn = 1, 2, …, 16, Vm = [1, 0, 0, 0]을 4번 반복, 누산기 초기값 100.

```
lane0 = 100 + (1·1 + 2·0 + 3·0 + 4·0)     = 101
lane1 = 100 + (5·1 + 6·0 + 7·0 + 8·0)     = 105
lane2 = 100 + (9·1 + ...)                  = 109
lane3 = 100 + (13·1 + ...)                 = 113
```

말로 하면: SDOT은 **"길이 4짜리 내적 4개를 동시에"** 하는 명령이다. 긴 내적은 이걸 반복해서 lane에 쌓고 마지막에 lane 4개를 더한다.

오버플로 감각: lane 하나에 한 번에 더해지는 최대값은 4 × (−128)·(−128) = 65,536 = 2¹⁶. int32는 2³¹까지이므로 최악의 경우에도 2¹⁵ = 32,768번 SDOT(= K가 약 13만)까지 안전하다. 실제 모델의 K(수백~수천)에서는 걱정할 필요가 없지만, C1의 "int32 누산 여유" 계산을 한 번은 해 두는 것이 원칙이다.

### 4.4 SMMLA — 2×8 · 8×2 행렬 조각

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 290">
<defs>
<marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="22.0" font-size="14" text-anchor="start">SMMLA Vd.4S, Vn.16B, Vm.16B — (2×8) · (8×2) → 2×2 int32 누산 = MAC 32개</text><text x="20.0" y="52.0" font-size="12" text-anchor="start">Vn = A의 2행 × 8 (행 i, i+1)</text><rect x="20" y="60" width="24" height="26" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="32.0" y="78.0" font-size="11" text-anchor="middle">1</text><rect x="46" y="60" width="24" height="26" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="58.0" y="78.0" font-size="11" text-anchor="middle">1</text><rect x="72" y="60" width="24" height="26" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="84.0" y="78.0" font-size="11" text-anchor="middle">1</text><rect x="98" y="60" width="24" height="26" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="110.0" y="78.0" font-size="11" text-anchor="middle">1</text><rect x="124" y="60" width="24" height="26" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="136.0" y="78.0" font-size="11" text-anchor="middle">1</text><rect x="150" y="60" width="24" height="26" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="162.0" y="78.0" font-size="11" text-anchor="middle">1</text><rect x="176" y="60" width="24" height="26" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="188.0" y="78.0" font-size="11" text-anchor="middle">1</text><rect x="202" y="60" width="24" height="26" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="214.0" y="78.0" font-size="11" text-anchor="middle">1</text><rect x="20" y="88" width="24" height="26" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="32.0" y="106.0" font-size="11" text-anchor="middle">1</text>
<rect x="46" y="88" width="24" height="26" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="58.0" y="106.0" font-size="11" text-anchor="middle">2</text><rect x="72" y="88" width="24" height="26" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="84.0" y="106.0" font-size="11" text-anchor="middle">3</text><rect x="98" y="88" width="24" height="26" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="110.0" y="106.0" font-size="11" text-anchor="middle">4</text><rect x="124" y="88" width="24" height="26" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="136.0" y="106.0" font-size="11" text-anchor="middle">5</text><rect x="150" y="88" width="24" height="26" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="162.0" y="106.0" font-size="11" text-anchor="middle">6</text><rect x="176" y="88" width="24" height="26" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="188.0" y="106.0" font-size="11" text-anchor="middle">7</text><rect x="202" y="88" width="24" height="26" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="214.0" y="106.0" font-size="11" text-anchor="middle">8</text><text x="20.0" y="142.0" font-size="12" text-anchor="start">Vm = B의 2열 × 8 (Bᵀ 두 행 = 열 j, j+1)</text><rect x="20" y="150" width="24" height="26" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="32.0" y="168.0" font-size="11" text-anchor="middle">1</text><rect x="46" y="150" width="24" height="26" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="58.0" y="168.0" font-size="11" text-anchor="middle">0</text><rect x="72" y="150" width="24" height="26" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="84.0" y="168.0" font-size="11" text-anchor="middle">0</text><rect x="98" y="150" width="24" height="26" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="110.0" y="168.0" font-size="11" text-anchor="middle">0</text><rect x="124" y="150" width="24" height="26" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/>
<text x="136.0" y="168.0" font-size="11" text-anchor="middle">0</text><rect x="150" y="150" width="24" height="26" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="162.0" y="168.0" font-size="11" text-anchor="middle">0</text><rect x="176" y="150" width="24" height="26" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="188.0" y="168.0" font-size="11" text-anchor="middle">0</text><rect x="202" y="150" width="24" height="26" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="214.0" y="168.0" font-size="11" text-anchor="middle">0</text><rect x="20" y="178" width="24" height="26" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="32.0" y="196.0" font-size="11" text-anchor="middle">-1</text><rect x="46" y="178" width="24" height="26" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="58.0" y="196.0" font-size="11" text-anchor="middle">-1</text><rect x="72" y="178" width="24" height="26" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="84.0" y="196.0" font-size="11" text-anchor="middle">-1</text><rect x="98" y="178" width="24" height="26" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="110.0" y="196.0" font-size="11" text-anchor="middle">-1</text><rect x="124" y="178" width="24" height="26" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="136.0" y="196.0" font-size="11" text-anchor="middle">-1</text><rect x="150" y="178" width="24" height="26" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="162.0" y="196.0" font-size="11" text-anchor="middle">-1</text><rect x="176" y="178" width="24" height="26" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="188.0" y="196.0" font-size="11" text-anchor="middle">-1</text><rect x="202" y="178" width="24" height="26" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="214.0" y="196.0" font-size="11" text-anchor="middle">-1</text><line x1="240" y1="110" x2="330" y2="130" stroke="currentColor" marker-end="url(#ar)"/>
<line x1="240" y1="180" x2="330" y2="150" stroke="currentColor" marker-end="url(#ar)"/><rect x="350" y="90" width="112" height="42" fill="#4a7bd0" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="2"/><text x="406.0" y="108.0" font-size="11" text-anchor="middle">c00 = A행0·B열0</text><text x="406.0" y="124.0" font-size="12" text-anchor="middle">= 1</text><rect x="470" y="90" width="112" height="42" fill="#4a7bd0" fill-opacity="0.15" stroke="#d0564a" stroke-width="2"/><text x="526.0" y="108.0" font-size="11" text-anchor="middle">c01 = A행0·B열1</text><text x="526.0" y="124.0" font-size="12" text-anchor="middle">= -8</text><rect x="350" y="140" width="112" height="42" fill="#e08a3c" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="2"/><text x="406.0" y="158.0" font-size="11" text-anchor="middle">c10 = A행1·B열0</text><text x="406.0" y="174.0" font-size="12" text-anchor="middle">= 1</text><rect x="470" y="140" width="112" height="42" fill="#e08a3c" fill-opacity="0.15" stroke="#d0564a" stroke-width="2"/><text x="526.0" y="158.0" font-size="11" text-anchor="middle">c11 = A행1·B열1</text><text x="526.0" y="174.0" font-size="12" text-anchor="middle">= -36</text><text x="350.0" y="80.0" font-size="12" text-anchor="start">Vd (lane 0..3 = c00 c01 c10 c11)</text><text x="20.0" y="236.0" font-size="12" text-anchor="start">SDOT: 입력 레지스터 2개 → MAC 16개 (벡터 × 벡터, 4 그룹)</text><text x="20.0" y="256.0" font-size="12" text-anchor="start">SMMLA: 입력 레지스터 2개 → MAC 32개 (한 입력 행이 두 열과 재사용됨) — 대신 데이터를 2행씩 묶어(pack) 둬야 한다</text><text x="20.0" y="276.0" font-size="12" text-anchor="start">숫자는 예제 lanes.c의 실제 출력: [[1 −8] [1 −36]]</text></svg>
```

그림 5 — `SMMLA Vd.4S, Vn.16B, Vm.16B`. Vn의 16바이트는 A의 두 행(각 8개), Vm의 16바이트는 B의 두 열(= Bᵀ의 두 행, 각 8개)로 해석한다. 결과 2×2가 Vd의 lane 0~3에 행 우선(c00, c01, c10, c11)으로 누산된다.

손계산(그림의 숫자):

```
A 행0 = [1 1 1 1 1 1 1 1],   A 행1 = [1 2 3 4 5 6 7 8]
B 열0 = [1 0 0 0 0 0 0 0],   B 열1 = [−1 −1 −1 −1 −1 −1 −1 −1]

c00 = 행0·열0 = 1            c01 = 행0·열1 = −8
c10 = 행1·열0 = 1            c11 = 행1·열1 = −(1+2+…+8) = −36
```

SDOT과의 핵심 차이는 **재사용**이다. SDOT은 입력 레지스터 두 개(32바이트)로 MAC 16개를 한다: 바이트당 MAC 0.5개. SMMLA는 같은 32바이트로 MAC 32개를 한다: 바이트당 1개. A의 한 행이 B의 두 열과 곱해지고, B의 한 열이 A의 두 행과 곱해지기 때문이다. D3의 arithmetic intensity를 **레지스터 레벨**에서 두 배로 올린 명령이다. 대가는 데이터 배치다: A를 "두 행씩 8바이트 번갈아" 놓아야 하므로 **packing**(재배치) 단계가 필요하다(5절 `pack_pairs`).

### 4.5 코드로 확인 — lane 의미를 작은 숫자로

무엇을 확인하나: 위 두 손계산을 이 Mac(M2)에서 intrinsics로 실제 실행해서 lane 배치가 맞는지 본다.

```c
/* SDOT와 SMMLA가 lane을 어떻게 묶는지 작은 숫자로 확인 */
#include <arm_neon.h>
#include <stdio.h>
int main(void)
{
    int8_t a[16], b[16];
    for (int i = 0; i < 16; i++) { a[i] = (int8_t)(i + 1); b[i] = (int8_t)(i % 4 == 0 ? 1 : 0); }
    int32x4_t acc = vdupq_n_s32(100);                   /* 누산기 초기값 100 */
    int32x4_t d = vdotq_s32(acc, vld1q_s8(a), vld1q_s8(b));
    printf("SDOT : %d %d %d %d\n", vgetq_lane_s32(d, 0), vgetq_lane_s32(d, 1),
           vgetq_lane_s32(d, 2), vgetq_lane_s32(d, 3));
    /* SMMLA: A = 2x8 (행 두 개), B^T = 2x8 (B의 열 두 개) → 2x2 */
    int8_t A[16] = {1, 1, 1, 1, 1, 1, 1, 1,   1, 2, 3, 4, 5, 6, 7, 8};
    int8_t Bt[16] = {1, 0, 0, 0, 0, 0, 0, 0,   -1, -1, -1, -1, -1, -1, -1, -1};
    int32x4_t m = vmmlaq_s32(vdupq_n_s32(0), vld1q_s8(A), vld1q_s8(Bt));
    printf("SMMLA: [[%d %d] [%d %d]]\n", vgetq_lane_s32(m, 0), vgetq_lane_s32(m, 1),
           vgetq_lane_s32(m, 2), vgetq_lane_s32(m, 3));
    int8x16_t mn = vdupq_n_s8(-128);                    /* 최악 경우: -128 × -128 × 4 */
    printf("SDOT worst lane: %d\n", vgetq_lane_s32(vdotq_s32(vdupq_n_s32(0), mn, mn), 0));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 -mcpu=apple-m2 lanes.c -o lanes && ./lanes
```

```text
SDOT : 101 105 109 113
SMMLA: [[1 -8] [1 -36]]
SDOT worst lane: 65536
```

출력에서 볼 것: SDOT의 lane 값 101/105/109/113, SMMLA의 2×2 = [[1 −8] [1 −36]]이 손계산과 같다. 최악 lane 값 65536 = 2¹⁶도 확인된다.

### 4.6 이 Mac이 무엇을 지원하나, 그리고 컴파일 플래그

무엇을 확인하나: macOS는 CPU 기능을 `sysctl hw.optional.arm.*`로 알려 준다(리눅스의 `/proc/cpuinfo` Features 줄에 해당).

```sh
sysctl hw.optional.arm.FEAT_DotProd hw.optional.arm.FEAT_I8MM hw.optional.arm.FEAT_BF16 hw.optional.arm.FEAT_FP16 hw.optional.arm.FEAT_SME
sysctl hw.optional.arm.FEAT_SVE
sysctl -a | grep -i "arm.feat_sve"
```

```text
hw.optional.arm.FEAT_DotProd: 1
hw.optional.arm.FEAT_I8MM: 1
hw.optional.arm.FEAT_BF16: 1
hw.optional.arm.FEAT_FP16: 1
hw.optional.arm.FEAT_SME: 0
sysctl: unknown oid 'hw.optional.arm.FEAT_SVE'
hw.optional.arm.FEAT_SVE_B16B16: 0
```

출력에서 볼 것: DotProd, I8MM, BF16, FP16은 1(있음). SME는 0. **SVE는 키 자체가 없다**(`unknown oid`) — M2에는 SVE가 없다(7절). `FEAT_SVE_B16B16: 0`은 SVE의 한 부속 기능 키가 0으로 보고되는 것이다.

컴파일 플래그 실험 결과(이 Apple clang 21):

| 플래그 | `vdotq_s32` | `vmmlaq_s32` |
|---|---|---|
| (없음, arm64 기본) | 컴파일됨 | **에러**: `'vmmlaq_s32' requires target feature 'i8mm'` |
| `-march=armv8.2-a+dotprod` | 컴파일됨 | 에러 (같은 메시지) |
| `-march=armv8.6-a+dotprod+i8mm` | 컴파일됨 | 컴파일됨, 실행 OK |
| `-mcpu=apple-m2` | 컴파일됨 | 컴파일됨, 실행 OK (`__ARM_FEATURE_MATMUL_INT8`, `__ARM_FEATURE_BF16`, `__ARM_FEATURE_DOTPROD` 정의) |

기본 arm64 타깃이 dotprod는 켜 두지만(Apple Silicon의 최소 사양이 M1) i8mm은 안 켠다는 뜻이다. **i8mm 코드를 배포하려면 런타임 feature 확인 후 분기**해야 한다 — M1에는 i8mm이 없다고 알려져 있다. 이 노트의 NEON 예제는 모두 `-mcpu=apple-m2`로 빌드했다.

---

## 5. 실측 — 이 Mac에서 int8 커널 네 가지

### 5.1 실험 설계

- 기계: Apple M2, P-core 한 개(스레드 1개). 다른 작업이 병렬로 돌고 있어서, 한 실행 안에서 11번 재고 **중앙값**, 그 실행을 7번 반복해 다시 **중앙값**을 썼다.
- 데이터: int8 난수(−128..127 전 범위, `srand(1)`), 누산 int32.
- GEMV: `y[64] = W[64×1024] · x[1024]`. W는 64 KiB로 P-core L1D(128 KiB)에 들어간다 → 메모리 병목이 아니라 **코어 안쪽**의 속도를 재는 설정.
- GEMM: `C[64×64] = A[64×1024] · B[1024×64]`. B는 전치해서 `Bt[64×1024]`로 둔다(열 = 연속 메모리). 16 KiB C + 64 KiB A + 64 KiB Bt.
- 버전: (a) 스칼라(벡터화 금지 pragma), (a') 같은 C를 자동 벡터화 허용, (b) NEON SMULL+SADALP, (c) SDOT, (c') SDOT 4×4 레지스터 블록 GEMM, (d)/(d') SMMLA 4×4 / 8×8 블록 GEMM.
- 검증: 모든 버전의 int32 결과를 스칼라 결과와 **원소 단위로 비교**(`match=yes`).

### 5.2 코드

아래 다섯 조각은 한 프로그램이다(`k_scalar.h`, `k_neon.h`, `k_gemm.h`, `k_mmla.h`, `bench.c`). 커널부터.

(a)·(a') 스칼라와 자동 벡터화 C — 무엇을 확인하나: 같은 C가 pragma 하나로 스칼라/벡터가 된다.

```c
/* (a) 스칼라 기준: 컴파일러의 자동 벡터화를 이 루프에서만 끈다 */
static int32_t dot_scalar(const int8_t *a, const int8_t *b, int k)
{
    int32_t acc = 0;
#pragma clang loop vectorize(disable) interleave(disable)
    for (int i = 0; i < k; i++)
        acc += (int32_t)a[i] * (int32_t)b[i];
    return acc;
}
/* (a') 같은 C, 자동 벡터화 허용 (-O2 기본) */
static int32_t dot_autovec(const int8_t *a, const int8_t *b, int k)
{
    int32_t acc = 0;
    for (int i = 0; i < k; i++)
        acc += (int32_t)a[i] * (int32_t)b[i];
    return acc;
}
```

(b)·(c) NEON 두 방식 — 무엇을 확인하나: SDOT이 없던 시절의 2단계(SMULL → SADALP)와 SDOT 한 단계. 둘 다 누산기를 2개 둬서 의존 체인을 둘로 나눴다.

```c
/* (b) SDOT 없는 NEON: int8×int8 → int16 (SMULL) → 쌍끼리 더해 int32에 누산 (SADALP) */
static int32_t dot_widen(const int8_t *a, const int8_t *b, int k)   /* k % 16 == 0 */
{
    int32x4_t acc0 = vdupq_n_s32(0), acc1 = vdupq_n_s32(0);
    for (int i = 0; i < k; i += 16) {
        int8x16_t va = vld1q_s8(a + i), vb = vld1q_s8(b + i);
        int16x8_t lo = vmull_s8(vget_low_s8(va), vget_low_s8(vb));  /* 8 lane × int16 */
        int16x8_t hi = vmull_high_s8(va, vb);                       /* 나머지 8 lane   */
        acc0 = vpadalq_s16(acc0, lo);   /* int16 이웃 쌍을 더해 int32 4개에 누산 */
        acc1 = vpadalq_s16(acc1, hi);
    }
    return vaddvq_s32(vaddq_s32(acc0, acc1));   /* 4 lane 가로 합 */
}
/* (c) SDOT: 명령 하나가 int8 16쌍을 곱해 lane마다 4개씩 int32에 누산 */
static int32_t dot_sdot(const int8_t *a, const int8_t *b, int k)    /* k % 32 == 0 */
{
    int32x4_t acc0 = vdupq_n_s32(0), acc1 = vdupq_n_s32(0);
    for (int i = 0; i < k; i += 32) {
        acc0 = vdotq_s32(acc0, vld1q_s8(a + i),      vld1q_s8(b + i));
        acc1 = vdotq_s32(acc1, vld1q_s8(a + i + 16), vld1q_s8(b + i + 16));
    }
    return vaddvq_s32(vaddq_s32(acc0, acc1));
}
```

(c') GEMM, SDOT 4×4 레지스터 블로킹 — 무엇을 확인하나: 출력 4×4 = 16개를 누산기 16개에 들고, A 4행·Bt 4행을 한 번씩만 로드해서 16번 SDOT한다.

```c
/* GEMM C[M][N] = A[M][K] · B[K][N],  B는 전치해 둔 Bt[N][K] (행 = B의 열) */
/* (c') SDOT 4×4 레지스터 블로킹: 누산기 16개, 입력 8개 로드로 SDOT 16번 */
static void gemm_sdot_4x4(const int8_t *A, const int8_t *Bt, int32_t *C, int M, int N, int K)
{
    for (int i = 0; i < M; i += 4)
        for (int j = 0; j < N; j += 4) {
            int32x4_t acc[4][4];
            for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) acc[r][c] = vdupq_n_s32(0);
            for (int k = 0; k < K; k += 16) {
                int8x16_t a[4], b[4];
                for (int r = 0; r < 4; r++) a[r] = vld1q_s8(A + (i + r) * K + k);
                for (int c = 0; c < 4; c++) b[c] = vld1q_s8(Bt + (j + c) * K + k);
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 4; c++) acc[r][c] = vdotq_s32(acc[r][c], a[r], b[c]);
            }
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++) C[(i + r) * N + j + c] = vaddvq_s32(acc[r][c]);
        }
}
```

(d)·(d') SMMLA — 무엇을 확인하나: 두 행씩 묶는 packing과, 매크로 하나로 만든 누산기 4개(4×4)·16개(8×8) 블록. 루프 한계가 상수라서 컴파일러가 누산기 배열을 전부 레지스터에 올린다(5.6절에서 확인).

```c
/* SMMLA용 packing: 행 두 개를 8바이트씩 번갈아 → [r0 k0..7 | r1 k0..7][r0 k8..15 | r1 k8..15]... */
static void pack_pairs(const int8_t *X, int8_t *P, int rows, int K)
{
    for (int p = 0; p < rows; p += 2)
        for (int k = 0; k < K; k += 8)
            for (int t = 0; t < 8; t++) {
                P[p * K + (k / 8) * 16 + t]     = X[p * K + k + t];        /* 위 행 8개   */
                P[p * K + (k / 8) * 16 + 8 + t] = X[(p + 1) * K + k + t];  /* 아래 행 8개 */
            }
}
/* (d)/(d') SMMLA 블록 GEMM. RP = 행 쌍 수, CP = 열 쌍 수 → 누산기 RP×CP개, 출력 (2RP)×(2CP) */
#define GEMM_MMLA(NAME, RP, CP)                                                            \
static void NAME(const int8_t *Ap, const int8_t *Bp, int32_t *C, int M, int N, int K)      \
{                                                                                          \
    for (int i = 0; i < M; i += 2 * RP)                                                    \
        for (int j = 0; j < N; j += 2 * CP) {                                              \
            int32x4_t acc[RP][CP];                                                         \
            for (int r = 0; r < RP; r++) for (int c = 0; c < CP; c++) acc[r][c] = vdupq_n_s32(0); \
            for (int k = 0; k < 2 * K; k += 16) {          /* k-step 8 = 16바이트(행 2개) */ \
                int8x16_t a[RP], b[CP];                                                    \
                for (int r = 0; r < RP; r++) a[r] = vld1q_s8(Ap + (i + 2 * r) * K + k);    \
                for (int c = 0; c < CP; c++) b[c] = vld1q_s8(Bp + (j + 2 * c) * K + k);    \
                for (int r = 0; r < RP; r++)                                               \
                    for (int c = 0; c < CP; c++) acc[r][c] = vmmlaq_s32(acc[r][c], a[r], b[c]); \
            }                                                                              \
            for (int r = 0; r < RP; r++) for (int c = 0; c < CP; c++) {  /* [c00 c01 c10 c11] */ \
                int32_t *o = C + (i + 2 * r) * N + j + 2 * c;                              \
                o[0] = vgetq_lane_s32(acc[r][c], 0);  o[1] = vgetq_lane_s32(acc[r][c], 1);  \
                o[N] = vgetq_lane_s32(acc[r][c], 2);  o[N + 1] = vgetq_lane_s32(acc[r][c], 3); \
            }                                                                              \
        }                                                                                  \
}
GEMM_MMLA(gemm_mmla_4x4, 2, 2)   /* 누산기 4개  */
GEMM_MMLA(gemm_mmla_8x8, 4, 4)   /* 누산기 16개 */
```

측정·검증 하네스 — 무엇을 확인하나: 중앙값 타이밍, 컴파일러가 반복 호출을 루프 밖으로 빼지 못하게 하는 빈 `asm volatile` 메모리 barrier, 결과 비교.

```c
/* int8 내적·GEMV·GEMM: 스칼라 / 자동벡터화 / SMULL+SADALP / SDOT / SMMLA — 결과 일치 확인 + 속도 */
#include <arm_neon.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "k_scalar.h"
#include "k_neon.h"
#include "k_gemm.h"
#include "k_mmla.h"
enum { M = 64, N = 64, K = 1024, REPS = 11 };
static int8_t A[M * K], Bt[N * K], x[K], Ap[M * K], Bp[N * K];
static int32_t y[5][M], C[4][M * N];
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int cmpd(const void *p, const void *q) { double a = *(const double *)p, b = *(const double *)q; return (a > b) - (a < b); }
typedef int32_t (*dotfn)(const int8_t *, const int8_t *, int);
static double bench_gemv(dotfn f, int32_t *out, int iters)       /* GMAC/s (median) */
{
    double t[REPS];
    for (int r = 0; r < REPS; r++) {
        double t0 = now();
        for (int it = 0; it < iters; it++) {
            __asm__ volatile("" ::: "memory");                    /* 호출이 루프 밖으로 빠지지 않게 */
            for (int m = 0; m < M; m++) out[m] = f(A + m * K, x, K);
        }
        t[r] = now() - t0;
    }
    qsort(t, REPS, sizeof t[0], cmpd);
    return (double)M * K * iters / t[REPS / 2] / 1e9;
}
static void gemm_scalar(const int8_t *A_, const int8_t *Bt_, int32_t *C_, int M_, int N_, int K_)
{
    for (int i = 0; i < M_; i++)
        for (int j = 0; j < N_; j++) C_[i * N_ + j] = dot_scalar(A_ + i * K_, Bt_ + j * K_, K_);
}
typedef void (*gemmfn)(const int8_t *, const int8_t *, int32_t *, int, int, int);
static double bench_gemm(gemmfn f, const int8_t *a, const int8_t *b, int32_t *out, int iters)
{
    double t[REPS];
    for (int r = 0; r < REPS; r++) {
        double t0 = now();
        for (int it = 0; it < iters; it++) { __asm__ volatile("" ::: "memory"); f(a, b, out, M, N, K); }
        t[r] = now() - t0;
    }
    qsort(t, REPS, sizeof t[0], cmpd);
    return (double)M * N * K * iters / t[REPS / 2] / 1e9;
}
int main(void)
{
    srand(1);
    for (int i = 0; i < M * K; i++) A[i] = (int8_t)(rand() % 256 - 128);   /* -128..127 전 범위 */
    for (int i = 0; i < N * K; i++) Bt[i] = (int8_t)(rand() % 256 - 128);
    for (int i = 0; i < K; i++) x[i] = (int8_t)(rand() % 256 - 128);
    pack_pairs(A, Ap, M, K);  pack_pairs(Bt, Bp, N, K);                   /* 타이밍 밖에서 1회 */
    dotfn fs[4] = {dot_scalar, dot_autovec, dot_widen, dot_sdot};
    const char *nm[4] = {"(a) scalar", "(a') autovec C", "(b) SMULL+SADALP", "(c) SDOT"};
    printf("GEMV %dx%d int8 -> int32\n", M, K);
    for (int v = 0; v < 4; v++) {
        double g = bench_gemv(fs[v], y[v], v == 0 ? 400 : 4000);
        int ok = 1; for (int m = 0; m < M; m++) ok &= (y[v][m] == y[0][m]);
        printf("  %-18s %7.2f GMAC/s  y[0]=%d  match=%s\n", nm[v], g, y[v][0], ok ? "yes" : "NO");
    }
    printf("GEMM %dx%dx%d int8 -> int32\n", M, N, K);
    double g0 = bench_gemm(gemm_scalar, A, Bt, C[0], 5);
    double g1 = bench_gemm(gemm_sdot_4x4, A, Bt, C[1], 200);
    double g2 = bench_gemm(gemm_mmla_4x4, Ap, Bp, C[2], 200);
    double g3 = bench_gemm(gemm_mmla_8x8, Ap, Bp, C[3], 200);
    int ok1 = 1, ok2 = 1, ok3 = 1;
    for (int i = 0; i < M * N; i++) { ok1 &= C[1][i] == C[0][i]; ok2 &= C[2][i] == C[0][i]; ok3 &= C[3][i] == C[0][i]; }
    printf("  %-18s %7.2f GMAC/s  C[0]=%d\n", "(a) scalar", g0, C[0][0]);
    printf("  %-18s %7.2f GMAC/s  match=%s\n", "(c') SDOT 4x4", g1, ok1 ? "yes" : "NO");
    printf("  %-18s %7.2f GMAC/s  match=%s\n", "(d) SMMLA 4x4", g2, ok2 ? "yes" : "NO");
    printf("  %-18s %7.2f GMAC/s  match=%s\n", "(d') SMMLA 8x8", g3, ok3 ? "yes" : "NO");
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 -mcpu=apple-m2 bench.c -o bench && ./bench
```

한 번의 실행 출력(7번 실행 중 두 번째):

```text
GEMV 64x1024 int8 -> int32
  (a) scalar            3.06 GMAC/s  y[0]=-139513  match=yes
  (a') autovec C       48.67 GMAC/s  y[0]=-139513  match=yes
  (b) SMULL+SADALP     28.71 GMAC/s  y[0]=-139513  match=yes
  (c) SDOT             71.06 GMAC/s  y[0]=-139513  match=yes
GEMM 64x64x1024 int8 -> int32
  (a) scalar            3.15 GMAC/s  C[0]=-35560
  (c') SDOT 4x4       190.05 GMAC/s  match=yes
  (d) SMMLA 4x4        76.47 GMAC/s  match=yes
  (d') SMMLA 8x8      196.68 GMAC/s  match=yes
```

출력에서 볼 것: 모든 버전이 `match=yes` — int8 → int32는 정수 연산이라 덧셈 순서를 바꿔도(SDOT의 lane 분할, SMMLA의 2×2 타일) **비트 단위로 같다.** fp32였다면 이렇게 안 된다(6.3절). 속도는 스칼라 약 3 → GEMV SDOT 71 → GEMM 블록 약 190~197 GMAC/s.

### 5.3 결과 (7회 실행의 중앙값)

| 커널 | GMAC/s (중앙값) | 범위 (min~max) | 스칼라 대비 |
|---|---|---|---|
| GEMV (a) 스칼라 | 3.1 | 3.0~3.1 | 1× |
| GEMV (a') 자동 벡터화 C | 48.7 | 48.5~48.8 | 16× |
| GEMV (b) SMULL+SADALP | 28.5 | 28.5~28.7 | 9× |
| GEMV (c) SDOT | 71.1 | 58.5~71.5 | 23× |
| GEMM (a) 스칼라 | 3.1 | 2.8~3.2 | 1× |
| GEMM (c') SDOT 4×4 블록 | 199.3 | 190.1~199.7 | 64× |
| GEMM (d) SMMLA 4×4 블록 | 78.8 | 76.5~79.4 | 25× |
| GEMM (d') SMMLA 8×8 블록 | 198.9 | 195.3~199.8 | 64× |

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 340">
<text x="10.0" y="22.0" font-size="14" text-anchor="start">Apple M2 P-core 1개, int8 → int32, GMAC/s (7회 실행 중앙값, 높을수록 빠름)</text><line x1="200.0" y1="40" x2="200.0" y2="302" stroke="currentColor" stroke-opacity="0.2"/><text x="200.0" y="318.0" font-size="11" text-anchor="middle">0</text><line x1="305.0" y1="40" x2="305.0" y2="302" stroke="currentColor" stroke-opacity="0.2"/><text x="305.0" y="318.0" font-size="11" text-anchor="middle">50</text><line x1="410.0" y1="40" x2="410.0" y2="302" stroke="currentColor" stroke-opacity="0.2"/><text x="410.0" y="318.0" font-size="11" text-anchor="middle">100</text><line x1="515.0" y1="40" x2="515.0" y2="302" stroke="currentColor" stroke-opacity="0.2"/><text x="515.0" y="318.0" font-size="11" text-anchor="middle">150</text><line x1="620.0" y1="40" x2="620.0" y2="302" stroke="currentColor" stroke-opacity="0.2"/><text x="620.0" y="318.0" font-size="11" text-anchor="middle">200</text><text x="10.0" y="60.0" font-size="12" text-anchor="start">GEMV 64×1024</text><text x="194.0" y="60.0" font-size="12" text-anchor="end">(a) 스칼라</text><rect x="200" y="47" width="6.5" height="20" fill="#888" fill-opacity="0.8"/><text x="211.5" y="62.0" font-size="12" text-anchor="start">3.1</text><text x="194.0" y="90.0" font-size="12" text-anchor="end">(a') 자동 벡터화 C</text><rect x="200" y="77" width="102.3" height="20" fill="#4a7bd0" fill-opacity="0.8"/><text x="307.3" y="92.0" font-size="12" text-anchor="start">48.7</text><text x="194.0" y="120.0" font-size="12" text-anchor="end">(b) SMULL+SADALP</text><rect x="200" y="107" width="59.9" height="20" fill="#e08a3c" fill-opacity="0.8"/><text x="264.9" y="122.0" font-size="12" text-anchor="start">28.5</text><text x="194.0" y="150.0" font-size="12" text-anchor="end">(c) SDOT</text><rect x="200" y="137" width="149.3" height="20" fill="#3f9a6b" fill-opacity="0.8"/><text x="354.3" y="152.0" font-size="12" text-anchor="start">71.1</text>
<text x="10.0" y="198.0" font-size="12" text-anchor="start">GEMM 64×64×1024</text><text x="194.0" y="198.0" font-size="12" text-anchor="end">(a) 스칼라</text><rect x="200" y="185" width="6.5" height="20" fill="#888" fill-opacity="0.8"/><text x="211.5" y="200.0" font-size="12" text-anchor="start">3.1</text><text x="194.0" y="228.0" font-size="12" text-anchor="end">(c') SDOT 4×4 블록</text><rect x="200" y="215" width="418.5" height="20" fill="#3f9a6b" fill-opacity="0.8"/><text x="623.5" y="230.0" font-size="12" text-anchor="start">199.3</text><text x="194.0" y="258.0" font-size="12" text-anchor="end">(d) SMMLA 4×4 블록</text><rect x="200" y="245" width="165.5" height="20" fill="#d0564a" fill-opacity="0.8"/><text x="370.5" y="260.0" font-size="12" text-anchor="start">78.8</text><text x="194.0" y="288.0" font-size="12" text-anchor="end">(d') SMMLA 8×8 블록</text><rect x="200" y="275" width="417.7" height="20" fill="#d0564a" fill-opacity="0.8"/><text x="622.7" y="290.0" font-size="12" text-anchor="start">198.9</text><line x1="200" y1="40" x2="200" y2="304" stroke="currentColor"/><text x="620.0" y="334.0" font-size="12" text-anchor="end">GMAC/s</text></svg>
```

그림 6 — 위 표의 중앙값. GEMV는 SDOT으로 가도 71에서 멈추고, GEMM은 레지스터 블로킹이 충분할 때(SDOT 4×4, SMMLA 8×8) 약 199까지 간다. SMMLA 4×4(빨강 짧은 막대)는 명령이 더 "강한"데도 SDOT 4×4의 40%밖에 안 된다.

### 5.4 해석 1 — 왜 GEMV는 71에서 멈추나

GEMV는 **가중치를 한 번 쓰고 버린다**(D4 4.2절). SDOT 하나(MAC 16개)마다 W 16바이트와 x 16바이트를 로드한다. 레지스터 레벨 intensity = MAC 16 / 32바이트 = 0.5 MAC/byte.

```
71 GMAC/s × 2 byte/MAC ≈ 142 GB/s  (L1 → 레지스터)
D3 5.3절에서 잰 이 Mac 1코어 L1 대역폭 ≈ 185~210 GB/s
```

말로 하면: GEMV 커널은 이미 **L1 로드 대역폭의 상당 부분**을 쓰고 있다. SDOT을 더 빨리 내도 로드가 못 따라온다. 레지스터에서 재사용이 없기 때문이다. 개선 방법은 x를 한 번 로드해서 **W의 여러 행에 재사용**하는 것(행 4개를 동시에 계산하면 x 로드가 1/4로) — 13절 연습문제로 남긴다.

(b) SMULL+SADALP가 SDOT의 40%인 이유는 명령 수다. MAC 16개에 SDOT 경로는 로드 2 + SDOT 1 = 3개, SMULL 경로는 로드 2 + SMULL 2 + SADALP 2 = 6개다. 게다가 SADALP은 int16 → int32 누산 체인을 만든다.

(a')의 자동 벡터화 C도 **SDOT을 썼다**(6.2절에서 어셈블리 확인). 그런데 수동 SDOT(71)보다 느린 49다. 커널 본체는 거의 같은데(`ldp` 2개 + `sdot` 2개) 루프 앞의 분기·정렬 처리와 코드 배치가 다르다. 흥미로운 관찰 하나: 처음 빌드(SMMLA 8×8 추가 전)에서는 (a')가 65 GMAC/s였고, 코드를 추가한 뒤의 빌드들에서는 46~49였다. 커널 코드는 그대로였으므로 **함수 배치(정렬)의 차이**로 추정한다. 마이크로벤치마크에서 이 정도 흔들림은 흔하다 — 하나의 숫자로 결론을 내지 말고, 어셈블리와 함께 본다.

### 5.5 해석 2 — 레지스터 블로킹과 누산기 개수

GEMM이 약 199까지 가는 이유는 **레지스터 재사용**이다. SDOT 4×4 블록의 내부 루프 한 번:

```
로드: A 4행 × 16B + Bt 4행 × 16B = 8 레지스터 (128 B)
연산: 4 × 4 = 16 SDOT = 256 MAC
→ 레지스터 레벨 intensity = 256 / 128 = 2 MAC/byte   (GEMV의 4배)
레지스터 사용: 누산기 16 + 입력 8 = 24개  (NEON 레지스터 32개 안에 들어간다)
```

그런데 SMMLA 4×4는 MAC 수가 같은 급인데 왜 79일까? 누산기가 **4개**뿐이기 때문이다. 이걸 확인하려고 메모리 접근이 전혀 없는 루프로 **명령 처리량 천장**을 쟀다(D3 5.6절의 FMA 실험과 같은 방법).

무엇을 확인하나: 독립 누산기(의존 체인) 개수 NACC를 바꿔 가며 SDOT/SMMLA를 반복할 때의 처리량.

```c
/* 메모리 접근 없이 SDOT / SMMLA만 반복: 명령 처리량 천장 (누산기 NACC개, 독립 체인) */
#include <arm_neon.h>
#include <stdio.h>
#include <time.h>
#ifndef NACC
#define NACC 16
#endif
#define ITERS 20000000L
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int argc, char **argv)
{
    (void)argv;
    int8x16_t a = vdupq_n_s8((int8_t)argc), b = vdupq_n_s8(3);
    int32x4_t acc[NACC];
    for (int r = 0; r < 2; r++) {                       /* r=0: SDOT, r=1: SMMLA */
        double best = 1e9;
        for (int rep = 0; rep < 7; rep++) {
            for (int i = 0; i < NACC; i++) acc[i] = vdupq_n_s32(i);
            double t0 = now();
            for (long it = 0; it < ITERS; it++)
                for (int i = 0; i < NACC; i++)
                    acc[i] = r == 0 ? vdotq_s32(acc[i], a, b) : vmmlaq_s32(acc[i], a, b);
            double t = now() - t0; if (t < best) best = t;
        }
        int32x4_t s = acc[0]; for (int i = 1; i < NACC; i++) s = vaddq_s32(s, acc[i]);
        double ginst = (double)ITERS * NACC / best / 1e9;
        printf("%s NACC=%2d: %5.2f G instr/s -> %6.1f GMAC/s (chk %d)\n", r == 0 ? "SDOT " : "SMMLA",
               NACC, ginst, ginst * (r == 0 ? 16 : 32), vaddvq_s32(s));
    }
    return 0;
}
```

```sh
for n in 1 2 4 8 16 24; do cc -std=c11 -Wall -Wextra -O2 -mcpu=apple-m2 -DNACC=$n peak.c -o peak$n && ./peak$n; done
```

NACC = 4와 16의 한 번 실행 출력:

```text
SDOT  NACC= 4:  4.66 G instr/s ->   74.5 GMAC/s (chk -454967272)
SMMLA NACC= 4:  2.31 G instr/s ->   73.8 GMAC/s (chk -909934568)
SDOT  NACC=16: 13.92 G instr/s ->  222.6 GMAC/s (chk -1819868704)
SMMLA NACC=16:  6.92 G instr/s ->  221.6 GMAC/s (chk 655229408)
```

프로그램을 NACC마다 3번씩 실행한 중앙값(각 실행은 7번 중 최고값):

| NACC | SDOT G명령/s | SDOT GMAC/s | SMMLA G명령/s | SMMLA GMAC/s |
|---|---|---|---|---|
| 1 | 1.16 | 18.5 | 0.58 | 18.6 |
| 2 | 2.32 | 37.1 | 1.16 | 37.1 |
| 4 | 4.64 | 74.2 | 2.30 | 73.6 |
| 8 | 9.11 | 145.7 | 4.32 | 138.3 |
| 16 | 13.91 | 222.6 | 6.94 | 222.0 |
| 24 | 2.90 | 46.5 | 2.90 | 92.9 |

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 320">
<text x="10.0" y="22.0" font-size="14" text-anchor="start">누산기(독립 체인) 개수 vs 처리량 — 메모리 접근 없는 SDOT/SMMLA 루프, M2 P-core 1개</text><line x1="80" y1="270" x2="620" y2="270" stroke="currentColor"/><line x1="80" y1="270" x2="80" y2="40" stroke="currentColor"/><line x1="80" y1="270.0" x2="620" y2="270.0" stroke="currentColor" stroke-opacity="0.15"/><text x="74.0" y="274.0" font-size="11" text-anchor="end">0</text><line x1="80" y1="212.5" x2="620" y2="212.5" stroke="currentColor" stroke-opacity="0.15"/><text x="74.0" y="216.5" font-size="11" text-anchor="end">60</text><line x1="80" y1="155.0" x2="620" y2="155.0" stroke="currentColor" stroke-opacity="0.15"/><text x="74.0" y="159.0" font-size="11" text-anchor="end">120</text><line x1="80" y1="97.5" x2="620" y2="97.5" stroke="currentColor" stroke-opacity="0.15"/><text x="74.0" y="101.5" font-size="11" text-anchor="end">180</text><line x1="80" y1="40.0" x2="620" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/><text x="74.0" y="44.0" font-size="11" text-anchor="end">240</text><text x="80.0" y="288.0" font-size="12" text-anchor="middle">1</text><text x="188.0" y="288.0" font-size="12" text-anchor="middle">2</text><text x="296.0" y="288.0" font-size="12" text-anchor="middle">4</text><text x="404.0" y="288.0" font-size="12" text-anchor="middle">8</text><text x="512.0" y="288.0" font-size="12" text-anchor="middle">16</text><text x="620.0" y="288.0" font-size="12" text-anchor="middle">24</text><text x="350.0" y="310.0" font-size="12" text-anchor="middle">누산기 개수 NACC</text><text x="20.0" y="140.0" font-size="12" text-anchor="middle" transform="rotate(-90 20 140)">GMAC/s</text><polyline points="80.0,252.3 188.0,234.4 296.0,198.9 404.0,130.4 512.0,56.7 620.0,225.4" fill="none" stroke="#4a7bd0" stroke-width="2.2"/><circle cx="80.0" cy="252.3" r="4" fill="#4a7bd0"/><circle cx="188.0" cy="234.4" r="4" fill="#4a7bd0"/>
<circle cx="296.0" cy="198.9" r="4" fill="#4a7bd0"/><circle cx="404.0" cy="130.4" r="4" fill="#4a7bd0"/><circle cx="512.0" cy="56.7" r="4" fill="#4a7bd0"/><circle cx="620.0" cy="225.4" r="4" fill="#4a7bd0"/><polyline points="80.0,252.2 188.0,234.4 296.0,199.5 404.0,137.5 512.0,57.2 620.0,181.0" fill="none" stroke="#e08a3c" stroke-width="2.2"/><circle cx="80.0" cy="252.2" r="4" fill="#e08a3c"/><circle cx="188.0" cy="234.4" r="4" fill="#e08a3c"/><circle cx="296.0" cy="199.5" r="4" fill="#e08a3c"/><circle cx="404.0" cy="137.5" r="4" fill="#e08a3c"/><circle cx="512.0" cy="57.2" r="4" fill="#e08a3c"/><circle cx="620.0" cy="181.0" r="4" fill="#e08a3c"/><text x="502.0" y="60.7" font-size="12" text-anchor="end">≈222 (포화)</text><text x="614.0" y="243.4" font-size="12" text-anchor="end">SDOT 47</text><text x="614.0" y="173.0" font-size="12" text-anchor="end">SMMLA 93</text><text x="95.0" y="92.0" font-size="12" text-anchor="start">NACC=24: 누산기 배열이</text><text x="95.0" y="108.0" font-size="12" text-anchor="start">레지스터에 안 들어가고</text><text x="95.0" y="124.0" font-size="12" text-anchor="start">메모리(ldr/str)로 → 급락</text><line x1="90" y1="48" x2="110" y2="48" stroke="#4a7bd0" stroke-width="3"/><text x="116.0" y="52.0" font-size="12" text-anchor="start">SDOT (16 MAC/명령)</text><line x1="90" y1="66" x2="110" y2="66" stroke="#e08a3c" stroke-width="3"/><text x="116.0" y="70.0" font-size="12" text-anchor="start">SMMLA (32 MAC/명령)</text></svg>
```

그림 7 — 누산기 개수와 처리량. 16까지는 누산기를 두 배로 하면 처리량도 거의 두 배(latency-bound), 16에서 포화(throughput-bound). 24에서는 급락한다.

읽는 법:

- **NACC = 1은 latency를 보여 준다.** 체인 하나는 이전 결과를 기다려야 하므로 "명령/s = 클럭 ÷ latency". M2 P-core 최대 클럭은 약 3.49 GHz로 널리 알려져 있다(Apple 공식 사양서 값은 아니다). 그러면 SDOT latency ≈ 3.49 ÷ 1.16 ≈ 3 cycle, SMMLA ≈ 3.49 ÷ 0.58 ≈ 6 cycle로 추정된다.
- **포화점은 throughput을 보여 준다.** SDOT 13.9 G명령/s ÷ 3.49 GHz ≈ cycle당 4개 → SIMD 파이프 4개로 추정(D3의 FMA 파이프 4개 추정과 같은 결론). SMMLA는 cycle당 약 2개. 거꾸로 "cycle당 64 MAC"을 가정하면 222.6 ÷ 64 ≈ 3.48 GHz로, 알려진 클럭과 맞아떨어진다.
- **그래서 이 코어에서 SDOT과 SMMLA의 MAC 천장은 같다(≈ 222 GMAC/s ≈ cycle당 64 MAC).** SMMLA는 명령당 MAC이 2배지만 명령 처리량이 절반이다. SMMLA의 이득은 "천장"이 아니라 **로드·레지스터가 절반으로 드는 것**이다. 다른 코어(Neoverse, Cortex-X 등)에서는 SMMLA 처리량이 SDOT과 같아서 MAC 천장이 2배가 되는 경우가 있다고 알려져 있으니, 코어별 Software Optimization Guide의 처리량 표를 봐야 한다.
- **SMMLA 4×4 GEMM(79)이 NACC = 4의 천장(74)과 비슷하다**(조금 높은 것은 out-of-order 코어가 다음 타일의 체인을 조금 겹쳐 실행하기 때문으로 보인다). 즉 그 커널은 메모리가 아니라 **누산기 부족(latency)** 으로 막혀 있었다. 8×8로 누산기를 16개로 늘리자 199가 됐다.
- **NACC = 24의 급락**: 어셈블리를 보면 컴파일러가 24개짜리 내부 루프를 펼치지 않고 누산기 배열을 **메모리(스택)에 둔 채** 매 명령마다 `ldr q0` → `sdot` → `str q0`을 한다. 레지스터가 32개라도 컴파일러가 배열을 레지스터에 올릴지는 따로다. 블로킹은 "레지스터에 들어갈 만큼만"이 아니라 "**컴파일러가 실제로 레지스터에 올린 만큼만**" 효과가 있다 — 반드시 어셈블리로 확인한다.

Don의 경험과 연결: 이건 SSD FW에서 "NAND 채널 하나에 명령을 한 개씩만 걸면 tR 동안 버스가 논다, queue depth를 올려야 대역폭이 나온다"와 같은 구조다. latency × throughput = 동시에 떠 있어야 할 일의 양(Little's law). SIMD 파이프 4개 × latency 3~4 cycle ≈ 12~16개의 독립 누산기가 필요하다.

### 5.6 컴파일러가 만든 SMMLA 8×8 내부 루프

무엇을 확인하나: intrinsics가 정말 로드 8개 + SMMLA 16개로 컴파일됐는지. (Apple의 `-S` 출력과 `objdump`는 이 명령의 피연산자를 생략해서 보여 줬다. 그래서 같은 코드를 `--target=aarch64-linux-gnu -march=armv8.6-a+dotprod+i8mm -ffreestanding -S`로 GNU 문법 어셈블리를 만들었다.)

```text
	ldr	q24, [x13]
	ldr	q25, [x13, #2048]
	ldr	q26, [x14]
	ldr	q27, [x13, #4096]
	cmp	x12, #2032
	ldr	q28, [x13, #6144]
	ldr	q29, [x14, #2048]
	add	x12, x12, #16
	ldr	q30, [x14, #4096]
	ldr	q31, [x14, #6144]
	smmla	v23.4s, v26.16b, v24.16b
	smmla	v22.4s, v26.16b, v25.16b
	smmla	v21.4s, v26.16b, v27.16b
	smmla	v20.4s, v26.16b, v28.16b
	smmla	v19.4s, v29.16b, v24.16b
	smmla	v18.4s, v29.16b, v25.16b
	smmla	v17.4s, v29.16b, v27.16b
	smmla	v16.4s, v29.16b, v28.16b
	smmla	v7.4s, v30.16b, v24.16b
	smmla	v6.4s, v30.16b, v25.16b
	smmla	v5.4s, v30.16b, v27.16b
	smmla	v4.4s, v30.16b, v28.16b
	smmla	v3.4s, v31.16b, v24.16b
	smmla	v2.4s, v31.16b, v25.16b
	smmla	v1.4s, v31.16b, v27.16b
	smmla	v0.4s, v31.16b, v28.16b
	add	x13, x13, #16
	b.lo	.LBB1_3
```

출력에서 볼 것: 누산기 v0~v7, v16~v23(16개)이 루프 내내 레지스터에 머문다. 입력 8개(v24~v31)는 각각 4번씩 재사용된다. 로드 오프셋 2048·4096·6144는 packing된 행 쌍 사이 거리(2K = 2048바이트)다. 이게 "레지스터 블로킹이 잘 된 커널"의 모양이다. (이 listing은 5.2절의 최종 코드, 즉 매크로로 만든 `gemm_mmla_8x8`에서 만들었다.) 같은 모양이 SDOT 4×4에서도 나오며, 라이브러리(XNNPACK, Arm Compute Library, ruy 등)의 int8 GEMM 마이크로커널이 이런 구조를 손으로(또는 생성기로) 만든다.

---

## 6. 자동 벡터화 — 언제 되고 언제 안 되나, 어떻게 확인하나

### 6.1 도구

clang(LLVM)의 루프 벡터화기는 `-O2` 이상에서 켜진다. 결과를 보는 방법은 셋이다.

- `-Rpass=loop-vectorize`: 벡터화한 루프와 폭(width), interleave 수를 알려 준다.
- `-Rpass-missed=loop-vectorize`: 못 한 루프.
- `-Rpass-analysis=loop-vectorize`: 못 한 **이유**.
- 그리고 `-S`로 어셈블리를 직접 본다. **remark만 믿으면 안 된다**는 걸 6.3절에서 본다.

GCC라면 `-fopt-info-vec-optimized`, `-fopt-info-vec-missed`가 같은 역할이다.

### 6.2 다섯 가지 루프

무엇을 확인하나: 전형적인 루프 다섯 개가 M2 타깃 `-O2`에서 어떻게 되는지.

```c
#include <stdint.h>
/* 1) int8 → int32 누산: 정수 덧셈은 순서를 바꿔도 결과가 같다 */
int32_t dot_i8(const int8_t *a, const int8_t *b, int n)
{ int32_t s = 0; for (int i = 0; i < n; i++) s += a[i] * b[i]; return s; }
/* 2) float 누산: 덧셈 순서를 바꾸면 결과가 달라질 수 있다 */
float dot_f32(const float *a, const float *b, int n)
{ float s = 0; for (int i = 0; i < n; i++) s += a[i] * b[i]; return s; }
/* 3) 루프 간 의존: y[i]가 y[i-1]을 쓴다 (IIR/prefix sum) */
void prefix(int32_t *y, const int32_t *x, int n)
{ for (int i = 1; i < n; i++) y[i] = y[i - 1] + x[i]; }
/* 4) 포인터 aliasing 가능성: out과 in이 겹칠 수도 있다 */
void scale_q7(int8_t *out, const int8_t *in, int8_t g, int n)
{ for (int i = 0; i < n; i++) out[i] = (int8_t)((in[i] * g) >> 7); }
/* 5) 같은 루프 + restrict: "겹치지 않는다"고 약속 */
void scale_q7_r(int8_t *restrict out, const int8_t *restrict in, int8_t g, int n)
{ for (int i = 0; i < n; i++) out[i] = (int8_t)((in[i] * g) >> 7); }
```

```sh
cc -std=c11 -O2 -mcpu=apple-m2 -c av_cases.c -o /dev/null -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize
```

```text
av_cases.c:4:18: remark: vectorized loop (vectorization width: 16, interleaved count: 2) [-Rpass=loop-vectorize]
av_cases.c:7:16: remark: vectorized loop (vectorization width: 4, interleaved count: 4) [-Rpass=loop-vectorize]
av_cases.c:10:36: remark: loop not vectorized: unsafe dependent memory operations in loop. Use #pragma clang loop distribute(enable) to allow loop distribution to attempt to isolate the offending operations into a separate loop
av_cases.c:10:3: remark: loop not vectorized [-Rpass-missed=loop-vectorize]
av_cases.c:13:3: remark: vectorized loop (vectorization width: 16, interleaved count: 4) [-Rpass=loop-vectorize]
av_cases.c:16:3: remark: vectorized loop (vectorization width: 16, interleaved count: 4) [-Rpass=loop-vectorize]
```

출력에서 볼 것:

- (1) int8 내적: width 16(int8 16개 = 128-bit), interleave 2(누산기 2개). 어셈블리를 보면 **SDOT**이다:

```text
LBB0_7:                                 ; =>This Inner Loop Header: Depth=1
	ldp	q2, q3, [x11, #-16]
	ldp	q4, q5, [x8, #-16]
	sdot.4s	v0, v4, v2
	sdot.4s	v1, v5, v3
	add	x8, x8, #32
	add	x11, x11, #32
	subs	x12, x12, #32
	b.ne	LBB0_7
```

- (2) float 내적: "vectorized"라고 나왔다. **그런데 6.3절에서 보면 반만 사실이다.**
- (3) prefix sum `y[i] = y[i−1] + x[i]`: 진짜 루프 간 의존(loop-carried dependence)이라 벡터화 불가. 이건 알고리즘을 바꿔야 한다(IIR 필터·RNN step이 같은 구조 — D4의 "순차 의존" 이야기).
- (4)와 (5) `scale_q7`: 둘 다 벡터화됐다. `restrict`가 없는 (4)는 어떻게 안전을 보장했을까? 6.4절.

### 6.3 함정 — "vectorized"인데 덧셈은 한 줄로 서 있다 (fp32 reduction)

float 내적의 M2 어셈블리 내부 루프:

```text
LBB0_7:                                 ; =>This Inner Loop Header: Depth=1
	ldp	q1, q2, [x10, #-32]
	ldp	q3, q4, [x10], #64
	ldp	q5, q6, [x11, #-32]
	ldp	q7, q16, [x11], #64
	fmul.4s	v1, v1, v5
	mov	s5, v1[3]
	mov	s17, v1[2]
	mov	s18, v1[1]
	...  (fmul.4s 3개와 lane 꺼내기 mov 9개 더)
	fadd	s0, s0, s1
	fadd	s0, s0, s18
	fadd	s0, s0, s17
	fadd	s0, s0, s5
	...  (fadd s0, s0, ... 가 모두 16번 이어진다)
	subs	x12, x12, #16
	b.ne	LBB0_7
```

곱셈(`fmul.4s`)은 벡터인데, 덧셈은 **스칼라 `fadd s0, s0, …` 16개가 한 줄로 의존**한다. C 표준의 부동소수점 덧셈 순서를 지키려면(`(((s + p0) + p1) + p2) …`) 이렇게 할 수밖에 없다. 컴파일러는 이걸 "in-order(strict) reduction으로 벡터화했다"고 보고한다. D3 7.2절에서 fp32 단순 루프가 약 2.1 GFLOP/s로 느렸던 이유가 바로 이것이다(같은 숫자가 여기서도 나온다).

무엇을 확인하나: 같은 함수를 기본 옵션과 `-ffast-math`로 따로 컴파일해서 속도와 결과를 비교한다.

```c
/* 같은 fp32 내적을 기본 옵션 / -ffast-math로 컴파일해 속도와 결과를 비교 */
#include <stdio.h>
#include <time.h>
float dot_strict(const float *a, const float *b, int n);
float dot_fast(const float *a, const float *b, int n);
enum { N = 4096, IT = 20000 };
static float a[N], b[N];
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(void)
{
    for (int i = 0; i < N; i++) { a[i] = (float)((i * 37) % 101) / 101.0f - 0.5f; b[i] = (float)((i * 53) % 97) / 97.0f; }
    float (*f[2])(const float *, const float *, int) = {dot_strict, dot_fast};
    const char *nm[2] = {"default (strict order)", "-ffast-math"};
    for (int v = 0; v < 2; v++) {
        double best = 1e9; float r = 0;
        for (int rep = 0; rep < 7; rep++) {
            double t0 = now();
            for (int it = 0; it < IT; it++) { __asm__ volatile("" ::: "memory"); r = f[v](a, b, N); }
            double t = now() - t0; if (t < best) best = t;
        }
        printf("%-24s %6.2f GFLOP/s  result=%.9g\n", nm[v], 2.0 * N * IT / best / 1e9, r);
    }
    return 0;
}
```

```sh
sed 's/dot_f32/dot_strict/' f.c > fs.c; sed 's/dot_f32/dot_fast/' f.c > ff.c   # f.c = 위 (2)의 dot_f32 한 함수
cc -std=c11 -Wall -Wextra -O2 -mcpu=apple-m2 -c fs.c
cc -std=c11 -Wall -Wextra -O2 -mcpu=apple-m2 -ffast-math -c ff.c
cc -std=c11 -Wall -Wextra -O2 fdrv.c fs.o ff.o -o fdrv && ./fdrv
```

```text
default (strict order)     2.19 GFLOP/s  result=-10.369854
-ffast-math               27.69 GFLOP/s  result=-10.369853
```

출력에서 볼 것: **12.6배** 차이. `-ffast-math`판은 `fmla.4s` 4개(누산기 4개 × lane 4 = 부분합 16개)로 바뀌고, 마지막 자리 결과가 다르다(−10.369854 vs −10.369853). 덧셈 순서가 바뀌었기 때문이다. 교훈 셋:

- **remark의 "vectorized"는 "빠르다"가 아니다.** 어셈블리를 본다.
- 정수(int8 → int32)는 덧셈 순서를 바꿔도 결과가 같아서 이 문제가 없다(5.2절의 `match=yes`). int8 양자화가 SIMD와 궁합이 좋은 또 하나의 이유다.
- `-ffast-math`는 파일 전체의 NaN/Inf 처리 가정까지 바꾼다. 커널 파일에만 국소적으로 쓰거나, 누산기를 여러 개 두는 코드를 직접 쓰는 편이 안전하다. 참고로 Cortex-M55 타깃에서는 이 float 루프가 아예 "loop not vectorized: cannot prove it is safe to reorder floating-point operations"로 나왔고, `-ffast-math`를 주자 `vfma.f32` + `dlstp.32`/`letp` tail-predicated 루프가 됐다.

### 6.4 aliasing — restrict가 없으면 컴파일러는 런타임 검사를 넣는다

`scale_q7`(restrict 없음)의 함수 앞부분:

```text
_scale_q7:                              ; @scale_q7
	cmp	w3, #1
	b.lt	LBB3_14
	mov	x9, #0                          ; =0x0
	mov	w8, w3
	cmp	w3, #8
	b.lo	LBB3_12
	sub	x10, x0, x1
	cmp	x10, #63
	b.ls	LBB3_12
	...
```

출력에서 볼 것: `sub x10, x0, x1; cmp x10, #63; b.ls LBB3_12` — **out − in이 0~63이면(= 벡터 한 번에 처리하는 64바이트 안에서 out이 in 바로 뒤에 겹치면) 스칼라 루프로 간다.** 컴파일러는 "겹칠 수도 있다"를 런타임에 검사하는 버전(runtime check, loop versioning)을 만든 것이다. `restrict`판(`scale_q7_r`)에는 이 두 줄이 없다.

- 포인터가 두세 개면 검사가 싸다. 포인터가 많거나 접근 패턴이 복잡하면 컴파일러가 검사를 포기하고 벡터화를 안 한다.
- `restrict`는 **약속**이다. 실제로 겹치는 버퍼를 넘기면 결과가 틀려도 컴파일러 잘못이 아니다. DMA 버퍼를 in-place로 처리하는 펌웨어 코드에서 특히 조심한다.

### 6.5 최적화 레벨과 타깃에 따라

같은 `av_cases.c`를 바꿔 가며 remark를 모았다.

| 설정 | int8 내적 | float 내적 | prefix | scale_q7 |
|---|---|---|---|---|
| M2, `-O1` | remark 0개 (벡터화기 안 돔) | 같음 | 같음 | 같음 |
| M2, `-O2` / `-O3` | 벡터화 (SDOT) | "벡터화"(strict, 6.3) | 불가 | 벡터화 + 런타임 검사 |
| M2, `-Os` | **"the cost-model indicates that vectorization is not beneficial"** | 같음 | 불가 | 같음 |
| Cortex-M4, `-O2` | remark 없음 (벡터 레지스터가 없어 벡터화 대상이 아님) | 없음 | 없음 | 없음 |
| Cortex-M55, `-O2` | 벡터화 width 16 (MVE) | **불가** (재배열 금지) | 불가 | 벡터화 width 8 |

`-Os`가 특히 중요하다. **MCU 펌웨어는 flash 크기 때문에 `-Os`가 기본인 경우가 많은데, 그러면 벡터화가 조용히 꺼진다.** 해결책: 핫 커널 파일만 `-O2`/`-O3`로 빌드하거나(빌드 시스템에서 파일별 플래그), 함수 단위 `__attribute__((optimize))`(GCC) 또는 `#pragma clang loop vectorize(enable)`로 명시한다.

타깃(`-mcpu`)이 코드 모양을 바꾸는 예 하나 더 — int8 내적(`av.c`)을 리눅스 AArch64 타깃으로:

```text
-- cortex-a53  (Armv8.0: SDOT 없음)
	smull2	v20.8h, v16.16b, v17.16b
	smull	v16.8h, v16.8b, v17.8b
	...
	saddw2	v2.4s, v2.4s, v20.8h
	saddw	v1.4s, v1.4s, v20.4h
	...
-- cortex-a55  (Armv8.2 + dotprod)
	sdot	v0.4s, v4.16b, v2.16b
	sdot	v1.4s, v5.16b, v3.16b
```

같은 C가 A53에서는 SMULL(넓히며 곱) + SADDW(넓히며 더하기)로, A55에서는 SDOT으로 나온다. **`-mcpu`를 실제 코어에 맞추지 않으면 새 명령을 못 쓴다.** 반대로 A55용으로 빌드한 바이너리를 A53에서 돌리면 SDOT에서 illegal instruction 예외가 난다.

### 6.6 체크리스트 — "컴파일러가 벡터화했나?"

1. `-Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize`로 remark를 본다.
2. `-S`로 핫 루프의 어셈블리를 연다. 벡터 레지스터(`q`, `v.16b`)와 기대한 명령(SDOT, SMLAD, VMLADAV)이 있는가?
3. reduction이면 누산이 벡터인지(`fmla.4s`), 스칼라 체인인지(`fadd s0, s0`) 본다.
4. 루프 앞의 런타임 alias 검사, 나머지(epilogue) 루프를 확인한다.
5. 최적화 레벨(`-Os`?)과 `-mcpu`를 확인한다.
6. 마지막으로 **재서** 확인한다(5절처럼 결과 비교 + 중앙값).


---

## 7. SVE / SVE2 — 벡터 길이를 모르는 채로 짜는 SIMD (개념)

### 7.1 직관

NEON 코드는 "벡터 = 128-bit = int8 16개"를 코드에 박아 넣는다(`i += 16`). **SVE(Scalable Vector Extension)** 는 벡터 길이를 128~2048비트(128의 배수) 중 **하드웨어가 정하게** 하고, 코드는 그 길이를 모르는 채로 짠다. 이걸 **VLA(Vector-Length Agnostic)** 프로그래밍이라고 한다. 같은 바이너리가 128-bit 코어에서도 256-bit 코어에서도 돈다.

핵심 장치 두 개:

- `svcntb()`: 지금 코어의 벡터 하나가 몇 바이트인지 런타임에 알려 준다. 루프 증가량으로 쓴다.
- **predicate 레지스터**(P0~P15): lane마다 켜짐/꺼짐 비트. `whilelt`가 "i..n−1 범위만 켜진" 마스크를 만들어 주므로 **tail 처리가 공짜**다. Helium의 tail predication(3.3절)과 같은 아이디어를 A-profile에 넣은 것이다.

SVE2(Armv9-A)는 SVE에 NEON 수준의 정수·DSP 연산(넓히기, 포화, 복소수 등)을 채워 넣어 "NEON의 후계자"가 되게 한 확장이다. Armv9-A 코어(A510/A520, A710~A725, X2~X4)는 SVE2를 가진다. 다만 **이 코어들의 SVE 벡터 길이는 128-bit로 알려져 있어서**, 모바일에서 SVE2의 이득은 "더 넓은 벡터"보다 predication·새 명령·VLA 이식성 쪽이다. 서버용 Neoverse V1은 256-bit SVE로 알려져 있다. 또 하드웨어가 SVE2를 가져도 **OS 커널이 켜 줬는지**는 기기마다 확인해야 한다(리눅스 `/proc/cpuinfo`의 `sve`, `sve2` 플래그).

### 7.2 이 Mac에는 없다

4.6절 출력처럼 `sysctl hw.optional.arm.FEAT_SVE`는 `unknown oid`, 즉 M2에는 SVE가 없다. Apple은 대신 CPU 옆의 전용 행렬 유닛(D3 5.5절에서 Accelerate 행렬곱 1.2 TFLOP/s로 간접 확인)을 쓰는 것으로 알려져 있고, 이 Mac의 `FEAT_SME`(Scalable Matrix Extension) 키는 0이다.

### 7.3 코드로 확인 — SVE int8 내적 (컴파일만)

무엇을 확인하나: VLA 루프가 어떤 명령으로 컴파일되는지. 실행은 SVE 하드웨어(또는 QEMU user-mode 같은 에뮬레이터)가 있어야 해서 여기서는 **어셈블리만** 본다.

```c
/* SVE: 벡터 길이를 모르는 채로(VLA) 짠 int8 내적 — 컴파일만 (이 Mac은 SVE 없음) */
#include <arm_sve.h>
#include <stdint.h>
int32_t dot_s8_sve(const int8_t *a, const int8_t *b, int64_t n)
{
    svint32_t acc = svdup_s32(0);
    for (int64_t i = 0; i < n; i += svcntb()) {        /* svcntb() = 벡터 하나의 바이트 수 */
        svbool_t pg = svwhilelt_b8_s64(i, n);           /* i..n-1 범위만 활성 lane */
        svint8_t va = svld1_s8(pg, a + i);              /* 비활성 lane은 0으로 로드 */
        svint8_t vb = svld1_s8(pg, b + i);
        acc = svdot_s32(acc, va, vb);                   /* SDOT의 SVE 버전 */
    }
    return (int32_t)svaddv_s32(svptrue_b32(), acc);     /* 가로 합 */
}
```

```sh
cc -O2 --target=aarch64-linux-gnu -march=armv9-a+sve2 -ffreestanding -Wall -Wextra -S sve_dot.c -o sve_dot.s
```

```text
dot_s8_sve:                             // @dot_s8_sve
	movi	v0.2d, #0000000000000000
	cmp	x2, #1
	b.lt	.LBB0_3
	mov	x8, xzr
.LBB0_2:                                // =>This Inner Loop Header: Depth=1
	whilelt	p0.b, x8, x2
	ld1b	{ z1.b }, p0/z, [x0, x8]
	ld1b	{ z2.b }, p0/z, [x1, x8]
	incb	x8
	sdot	z0.s, z1.b, z2.b
	cmp	x8, x2
	b.lt	.LBB0_2
.LBB0_3:
	ptrue	p0.s
	saddv	d0, p0, z0.s
	fmov	w0, s0
	ret
```

출력에서 볼 것: `whilelt p0.b`가 매 반복 마스크를 만들고, `ld1b ... p0/z`가 꺼진 lane을 0으로 채워 로드하고(z = zeroing), `incb x8`이 "벡터 바이트 수만큼" i를 늘린다. **16이라는 숫자가 코드 어디에도 없다.** 나머지 처리 루프도 없다. Helium의 `dlstp.8`/`letp`와 나란히 놓고 보면 두 설계가 같은 문제(tail 처리, 이식성)를 푼다는 게 보인다.


---

## 8. FPU와 정밀도 지원 — 무엇을 어느 코어에서 빠르게 할 수 있나

### 8.1 표

"지원한다"와 "빠르다"는 다르다. 아래는 **명령으로 지원하는지**(하드웨어 연산 여부)다. 구성 옵션이 많은 코어는 칩 데이터시트가 최종 답이다.

| 코어 | 스칼라 FP | fp16 연산 | bf16 | int8 내적 경로 | 벡터 FP |
|---|---|---|---|---|---|
| M0+ | 없음 (소프트웨어) | 없음 | 없음 | MULS, MAC 1개/명령 | 없음 |
| M4F | fp32 (FPv4-SP) | 변환만 (fp16 ↔ fp32) | 없음 | SXTB16 + SMLAD | 없음 |
| M7 | fp32 또는 fp32+fp64 (FPv5) | 변환만 | 없음 | SXTB16 + SMLAD | 없음 |
| M33 | fp32 옵션 | 변환만 | 없음 | DSP 확장 옵션 | 없음 |
| M55 / M85 | fp16·fp32·fp64 스칼라 (옵션) | **스칼라 연산 있음**, MVE-FP면 벡터 fp16 × 8 | 알려진 지원 없음 (확인 필요) | VMLADAV int8 × 16 | MVE-FP 구성: fp16 × 8, fp32 × 4 |
| A55 | fp32·fp64 | 있음 (FEAT_FP16, 스칼라·벡터) | 없음 | SDOT | NEON fp16/fp32/fp64 |
| A710 급 이후 (Armv9) | 있음 | 있음 | 있음 (BFDOT, BFMMLA) | SDOT, SMMLA, SVE2 | NEON + SVE2 |
| Apple M2 (이 Mac) | 있음 | 있음 (`FEAT_FP16: 1`) | 있음 (`FEAT_BF16: 1`) | SDOT, SMMLA | NEON |

말로 하면:

- **MCU에서 int8이 기본인 이유**가 이 표에 있다. M4/M7에는 벡터 FP가 없고 fp16 연산도 없다. int8/int16 정수 SIMD(SMLAD)만 있다.
- **M55부터는 fp16 벡터도 가능**하지만, Ethos-U 같은 NPU는 int8(및 int16 activation) 중심이므로 모델은 여전히 int8로 만든다(C1 6.6절, C2).
- A-profile에서 bf16은 Armv8.6/Armv9 세대부터. 온디바이스 LLM(L1)의 일부 연산을 bf16으로 돌릴 때 확인할 항목.

### 8.2 float ABI — `-mfloat-abi`가 실제로 바꾸는 것

무엇을 확인하나: 같은 두 함수를 float ABI만 바꿔 M4/M7로 컴파일한다.

```c
float mulf(float a, float b) { return a * b; }
double muld(double a, double b) { return a * b; }
```

```sh
cc --target=thumbv7em-none-eabi   -mcpu=cortex-m4 -mfloat-abi=soft                     -O2 -S fabi.c -o -
cc --target=thumbv7em-none-eabi   -mcpu=cortex-m4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16 -O2 -S fabi.c -o -
cc --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfloat-abi=hard   -mfpu=fpv4-sp-d16 -O2 -S fabi.c -o -
cc --target=thumbv7em-none-eabihf -mcpu=cortex-m7 -mfloat-abi=hard   -mfpu=fpv5-d16    -O2 -S fabi.c -o -
```

```text
[soft, M4]
mulf:
	push	{r7, lr}
	bl	__aeabi_fmul
	pop	{r7, pc}
muld:
	push	{r7, lr}
	bl	__aeabi_dmul
	pop	{r7, pc}
[softfp, M4 fpv4-sp-d16]
mulf:
	vmov	s0, r1
	vmov	s2, r0
	vmul.f32	s0, s2, s0
	vmov	r0, s0
	bx	lr
muld:
	b	__aeabi_dmul
[hard, M4 fpv4-sp-d16]
mulf:
	vmul.f32	s0, s0, s1
	bx	lr
muld:
	push	{r7, lr}
	vmov	r0, r1, d0
	vmov	r2, r3, d1
	bl	__aeabi_dmul
	vmov	d0, r0, r1
	pop	{r7, pc}
[hard, M7 fpv5-d16]
mulf:
	vmul.f32	s0, s0, s1
	bx	lr
muld:
	vmul.f64	d0, d0, d1
	bx	lr
```

(`@ @mulf`, `@ -- End function` 같은 주석 줄과 지시어는 뺐다.)

출력에서 볼 것:

- **soft**: FPU가 있어도 안 쓴다. `__aeabi_fmul` 라이브러리 호출(정수 연산으로 흉내, 수십 cycle).
- **softfp**: FPU 명령(`vmul.f32`)을 쓰지만 인자·반환값은 **정수 레지스터(r0, r1)** 로 주고받는다. 그래서 `vmov`가 앞뒤로 붙는다. soft로 빌드된 라이브러리와 링크할 수 있다(ABI 호환).
- **hard**: 인자를 **FP 레지스터(s0, s1)** 로 받는다. 가장 빠르지만 soft/softfp 오브젝트와 섞어 링크할 수 없다.
- **M4F에서 double은 여전히 소프트웨어**(`bl __aeabi_dmul`). FPv4-SP는 fp32만 한다. M7(fpv5-d16, DP 구성)은 `vmul.f64` 한 줄.

펌웨어 함정: `float y = x * 0.1;`은 `0.1`이 double 상수라서 M4F에서 **double 소프트웨어 곱셈**이 된다. `0.1f`로 쓰고 `-Wdouble-promotion`을 켠다. Don이 R8에서 VFP를 켜고 context를 저장하던 경험과 같은 레벨의 이야기다: FPU가 있다 ≠ 컴파일러가 쓴다.

---

## 9. CMSIS와 M-profile 컴파일 플래그 — 실무 정리

### 9.1 CMSIS의 세 층

| 구성요소 | 무엇 | 예 | 이 노트와의 연결 |
|---|---|---|---|
| **CMSIS-Core** | 코어 레지스터 정의, 시작 코드, NVIC/SysTick 함수, 컴파일러 중립 intrinsic | `SCB->CCR`, `NVIC_EnableIRQ()`, `__DSB()`, `__WFI()`, `__SMLAD()`, `__SXTB16()` | 2.4절 SMLAD intrinsic. Don이 bring-up에서 쓰던 그 헤더 |
| **CMSIS-DSP** | 신호처리 라이브러리 (f32/f16/q31/q15/q7) | FIR, biquad, `arm_rfft_fast_f32`, 행렬, 통계, MFCC 관련 함수 | G4(MFCC) 전처리, IMU 필터 |
| **CMSIS-NN** | 신경망 int8/int16 커널 | conv, depthwise conv, fully connected, pooling, softmax | 2.6절. TFLite Micro가 Cortex-M에서 이 커널을 호출한다(TFLM 소스의 `kernels/cmsis_nn`) |

셋 다 내부에 **순수 C / DSP 확장 / MVE(Helium)** 경로를 두고, 1.4절의 컴파일러 매크로로 고른다. 그래서 같은 모델·같은 TFLM 코드가 M0+ → M4 → M55로 가면 커널만 바뀌어 빨라진다. **CMSIS-NN이 중요한 이유**: (1) int8 SIMD를 제대로 쓰는 손 최적화 커널이고(2절에서 봤듯 컴파일러는 int8 C를 SMLAD로 못 바꾼다), (2) TFLite의 양자화 관례(C1 11.1절)와 bit-exact하게 맞춰져 있어서 PC에서 검증한 결과를 MCU에서 그대로 재현할 수 있다.

### 9.2 M-profile 컴파일 플래그

| 플래그 | 뜻 | 예 |
|---|---|---|
| `-mcpu=` | 코어 지정 → 아키텍처, 명령 스케줄링, (clang은) 기본 FPU·확장까지 | `cortex-m4`, `cortex-m7`, `cortex-m33`, `cortex-m55` |
| `-mcpu=...+확장` | 확장 켜고 끄기 | `cortex-m33+nodsp`, `cortex-m55+nomve.fp`, `cortex-m55+nofp` |
| `-march=` | 코어 대신 아키텍처로 지정 | `armv7e-m`, `armv8.1-m.main+mve.fp+fp.dp` |
| `-mfpu=` | FPU 종류 (주로 GCC 스타일) | M4: `fpv4-sp-d16`, M7: `fpv5-sp-d16` 또는 `fpv5-d16`, M33: `fpv5-sp-d16` |
| `-mfloat-abi=` | soft / softfp / hard (8.2절) | 한 제품의 모든 오브젝트·라이브러리가 같아야 함 |
| `-mthumb` | Thumb 명령 (M-profile은 Thumb만 있음, GCC에서 명시) | |
| `--target=` (clang) | 트리플 | `thumbv7em-none-eabihf`, `thumbv8.1m.main-none-eabihf` |

이 노트에서 확인한 사실(Apple clang 21):

- `-mcpu=cortex-m55`는 `__ARM_FEATURE_MVE 3`(정수+FP MVE)과 `__ARM_FP 0xe`를 켠다. `+nomve.fp` → MVE 1, `+nomve` → MVE 매크로 없음, `+nofp` → FP 매크로 없음(MVE 1은 남음).
- `--target=thumbv7em-none-eabi -mcpu=cortex-m4`만 주면 softfp처럼 동작했다(FPU 명령 + 정수 레지스터 인자). `-eabihf` 트리플이나 `-mfloat-abi=hard`로 명시해야 hard가 된다.
- GCC(arm-none-eabi-gcc)는 이 Mac에 없어서 확인하지 않았다. GCC는 툴체인 빌드 설정에 따라 기본 float ABI가 다르므로, 항상 `-mcpu -mthumb -mfpu -mfloat-abi`를 **네 개 다 명시**하는 것이 안전하다.

### 9.3 함정

- 라이브러리(CMSIS-DSP 미리 빌드된 `.a`, 벤더 BSP)와 float ABI가 다르면 링커가 "uses VFP register arguments" 류의 에러를 내거나, 더 나쁘게는 softfp/soft 조합에서 조용히 느리다.
- `-mcpu`를 칩보다 높게 주면(예: FPU 없는 M4에 `-mfpu=fpv4-sp-d16`) 첫 FP 명령에서 UsageFault(NOCP)가 난다. FPU가 있어도 **CPACR로 FPU를 켜는 시작 코드**(`SCB->CPACR |= (0xF << 20)`)가 없으면 같은 fault. Don에게는 익숙한 bring-up 단계다.
- CMSIS-NN을 `-O0`이나 `-Os`로 빌드하면 intrinsics가 있어도 느려질 수 있다. 커널 라이브러리는 `-O2`/`-O3`로 따로 빌드한다(6.5절).

---

## 10. 임베디드 관점에서 다시 보기 — 웨어러블에서 코어 고르기

### 10.1 역할 분담 (E8과 연결)

예를 들어 Hark 같은 웨어러블이라면(**가정**), 연산은 크게 두 층으로 나뉜다.

| 층 | 일 | 요구 | 후보 코어 |
|---|---|---|---|
| always-on | VAD, wake word, IMU 제스처·착용 감지, 센서 로그 버퍼링 | µW~mW, 상시, 지연 수십 ms, 모델 수십~수백 KB | M4F, M33, **M55(+Ethos-U55)** |
| main compute | ASR, 대화 모델, 카메라, 앱 | 수백 mW 이상 가능(짧게), GB급 DRAM, 리눅스/안드로이드 | **A55**(little) + A7x(big) + DSP(E4) + NPU(E5) |

always-on 층에서 모델이 깨울지 판단하고, 메인 SoC는 필요할 때만 켠다. 이 경계가 전력 예산의 대부분을 결정한다(D7, E9).

### 10.2 손계산 — 키워드 인식 하나를 어디서 돌릴까

가정(모두 설명용 숫자다): KWS 모델 추론 1회 = 3 M MAC, 초당 10회 → **30 M MAC/s** 필요.

```
M4F @ 80 MHz, 실효 0.5 MAC/cycle (가정: SMLAD 이론 2의 1/4 — 확장·로드·requant 오버헤드)
   → 40 M MAC/s  → 가동률 30/40 = 75%   (빠듯하다. 전처리 MFCC 몫까지 넣으면 부족)

M55 @ 160 MHz, 실효 4 MAC/cycle (가정: dual-beat 이론 8의 절반)
   → 640 M MAC/s → 가동률 약 4.7%      (나머지 시간은 sleep → 평균 전력이 크게 준다)

M55 + Ethos-U55 128 MAC/cycle 구성 @ 160 MHz, 활용률 50% (가정)
   → 약 10 G MAC/s → 가동률 약 0.3%   (이제 연산보다 SRAM·flash 대역폭과 NPU 기동 오버헤드가 문제)

A55 @ 1.5 GHz, 실효 8 MAC/cycle (가정: SDOT 이론의 절반 수준이라고 가정 — 실제 처리량은 코어 TRM 확인)
   → 12 G MAC/s  → 가동률 0.25%       (그러나 A55 클러스터 + LPDDR를 always-on으로 켜 두는 전력이 문제)
```

말로 하면: **"빠르다"만 보면 A55가 이기지만, always-on 과제는 "평균 전력 = 활성 전력 × 가동률 + 대기 전력"(D7)으로 판단한다.** A-class는 대기·기동 비용(DRAM refresh, 클러스터 전원, 캐시 재충전)이 커서 상시 작업에 불리하다. M55+U55는 MCU 전력 도메인 안에서 "큰 MAC/cycle"을 얻는 조합이라 always-on ML의 전형적인 선택지가 됐다(예: Alif Ensemble처럼 Cortex-M55 + Ethos-U55를 묶은 MCU가 있다 — M3). M4F는 가장 싸고 성숙하지만 모델 크기를 강하게 제한한다.

### 10.3 선택 기준표

| 기준 | M4F | M55 (+U55) | A55 |
|---|---|---|---|
| int8 연산 경로 | SMLAD (int16 2-way) | Helium 16-lane (+ NPU) | NEON SDOT |
| 이론 int8 MAC/cycle (코어) | 2 (int16 기준) | 8 (dual-beat 추정) / U55 32~256 구성 | 코어 구현에 따름 |
| 메모리 | 수백 KB~수 MB SRAM, TCM | 같음 + NPU용 SRAM | LPDDR (GB), 캐시 |
| 소프트웨어 | bare-metal/RTOS, TFLM + CMSIS-NN | 같음 + Vela 컴파일러(U55용 모델 변환) | 리눅스/안드로이드, TFLite(XNNPACK), ONNX Runtime, llama.cpp |
| 부동소수점 | fp32 스칼라 | fp16/fp32 벡터 (MVE-FP) | fp16/fp32/fp64 벡터 |
| 결정적 지연 | 매우 좋음 | 좋음 | OS 스케줄링 영향 |
| always-on 적합성 | 좋음 | 좋음 | 나쁨 |
| 큰 모델(수 MB 이상) | 불가 | 제한적 | 가능 |

### 10.4 펌웨어 엔지니어가 볼 체크포인트

- `-mcpu`/`-mfpu`/`-mfloat-abi`가 실제 칩 구성과 맞는가, CMSIS-NN이 DSP/MVE 경로로 빌드됐는가(1.4절 매크로를 `#error`로 검사할 수도 있다: `#if !defined(__ARM_FEATURE_MVE)` → `#error`).
- 핫 커널의 어셈블리에 SMLAD / VMLADAV / SDOT이 실제로 있는가(6.6절).
- 텐서 버퍼 정렬(16바이트), 비정렬 접근 트랩 설정(2.3절), TCM/SRAM 배치.
- FPU/MVE 컨텍스트 저장이 RTOS 설정과 맞는가(3.7절), 인터럽트 지연 영향.
- 벤치마크: 같은 모델을 순수 C / CMSIS-NN / NPU로 돌려 cycle 수(DWT->CYCCNT)를 비교한다(PJ7, K 모듈).

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `-mcpu`를 안 주거나 낮게 줌 | CMSIS-NN이 느림, 어셈블리에 SMLAD/SDOT 없음 | 기능 매크로가 꺼져 C 경로로 빌드 | 실제 코어로 `-mcpu` 지정, 매크로를 `#error`로 검사 |
| 펌웨어 전체 `-Os` | 커널이 스칼라 루프 | `-Os`에서 벡터화 cost model이 거부 (6.5) | 커널 파일만 `-O2`/`-O3` |
| float ABI 불일치 | 링크 에러, 또는 soft 경로로 조용히 느림 | 라이브러리와 앱의 `-mfloat-abi` 다름 | 모든 오브젝트를 같은 ABI로 |
| `0.1` 같은 double 상수 | M4F에서 `__aeabi_dmul` 호출, 느림 | FPv4-SP는 fp32만 | `0.1f`, `-Wdouble-promotion` |
| fp32 reduction을 벡터화됐다고 믿음 | "vectorized" remark인데 12배 느림 | strict 순서 reduction = 스칼라 fadd 체인 (6.3) | 누산기 여러 개를 직접 쓰거나 커널에만 `-ffast-math` |
| 누산기 부족 | SIMD인데 천장의 1/3 | latency-bound (SMMLA 4×4 = 79) | 누산기 12~16개, 어셈블리로 레지스터 유지 확인 |
| 누산기 과다 | 갑자기 느려짐 | 컴파일러가 배열을 스택에 둠 (NACC 24) | 블록 크기 줄이기, 수동 unroll |
| i8mm 코드를 M1/구형 코어에서 실행 | illegal instruction | 런타임 feature 미확인 | `sysctl`/HWCAP으로 확인 후 분기 |
| 비정렬 32-bit 로드 + UNALIGN_TRP | M4에서 UsageFault | `-O3`가 int16 쌍을 `ldr`로 묶음 (2.3) | 버퍼 4바이트 정렬, 또는 `-mno-unaligned-access` |
| Helium 코드 + FPU lazy stacking 미설정 | 인터럽트 후 벡터 레지스터 깨짐, 지연 증가 | Q 레지스터 = FP 레지스터 | RTOS FP 컨텍스트 저장 설정 확인 |

---

## 12. 면접에서 이렇게 말한다

**Q.** How does an int8 dot product run on a Cortex-M4 versus a Cortex-M55 versus an A-class core with SDOT?

**A.** M4에는 int8 SIMD MAC이 없어서 워드 하나를 SXTB16 두 번으로 int16 쌍으로 넓히고 SMLAD로 MAC 2개씩 한다. CMSIS-NN은 한쪽을 미리 int16으로 확장해 재사용한다. M55는 Helium 128-bit 벡터로 VMLADAV 한 명령에 int8 16쌍을 곱해 범용 레지스터에 누산하고, tail-predicated low-overhead loop로 루프 오버헤드와 나머지 처리가 없다. A-class는 NEON SDOT이 lane마다 int8 4쌍, 명령당 16 MAC을 int32에 누산하고, 여러 누산기로 레지스터 블로킹해 파이프를 채운다.

> On an M4 there's no int8 SIMD multiply, so you sign-extend bytes to int16 pairs with SXTB16 and use SMLAD, two MACs per instruction — CMSIS-NN pre-expands one operand to amortize that. On an M55, Helium's VMLADAV multiplies sixteen int8 pairs and accumulates into a scalar register, inside a tail-predicated low-overhead loop. On an A-class core, SDOT does sixteen int8 MACs into four int32 lanes, and you block registers across many accumulators to keep the SIMD pipes busy.

**Q.** What is Helium?

**A.** Armv8.1-M의 M-profile Vector Extension이다. FPU 레지스터 파일을 공유하는 128-bit Q 레지스터 8개, int8/16/32와 (구성에 따라) fp16/fp32 벡터, 명령을 beat로 나눠 1·2·4 beat/tick으로 실행하며 서로 다른 명령을 겹치는 설계, low-overhead loop와 tail predication이 특징이다. M55, M85가 대표 코어이고, NEON보다 면적·전력을 우선한 MCU용 벡터 ISA다.

> Helium is the M-profile Vector Extension from Armv8.1-M. It adds eight 128-bit vector registers that alias the FPU registers, integer and optional half and single precision vector ops, and it executes each instruction in 32-bit beats — one, two, or four per cycle depending on the core — overlapping loads with MACs. Together with low-overhead loops and tail predication, it gives MCUs like the M55 several times the DSP and ML throughput of an M4 at MCU power.

**Q.** What does i8mm add over SDOT?

**A.** SMMLA는 2×8 int8과 8×2 int8을 곱해 2×2 int32에 누산한다. 입력 레지스터 두 개로 MAC 32개, SDOT의 두 배라서 레지스터 레벨 재사용이 두 배다. 대신 두 행씩 interleave하는 packing이 필요하다. 실제 이득은 코어의 처리량에 달려 있다 — 이 Mac(M2)에서 재 보니 SMMLA는 명령 처리량이 SDOT의 절반이라 MAC 천장은 같았고(약 222 GMAC/s), 이득은 로드와 레지스터 절감이었다. 그리고 누산기가 부족하면(4×4 블록) SDOT보다 오히려 느렸다.

> i8mm adds SMMLA, a 2-by-8 times 8-by-2 int8 matrix multiply-accumulate into a 2-by-2 int32 tile — thirty-two MACs from two input registers, so twice the data reuse of SDOT, at the cost of packing rows in pairs. Whether it doubles throughput depends on the core: on an Apple M2 I measured SMMLA at half SDOT's instruction rate, so the MAC ceiling was the same, and with too few accumulators it was actually slower.

**Q.** Why does CMSIS-NN matter?

**A.** 컴파일러는 int8 C 루프를 M4의 SXTB16+SMLAD로 바꿔 주지 못한다(직접 확인했다). CMSIS-NN은 DSP 확장과 Helium을 제대로 쓰는 손 최적화 커널을 같은 API 아래 두고, TFLite 양자화 관례와 bit-exact하게 맞춰져 있어서, TFLM이 코어에 맞는 최적 경로를 자동으로 쓴다. 우리는 모델과 메모리 배치에 집중할 수 있다.

> Compilers generally won't turn an int8 C loop into SXTB16 plus SMLAD on an M4 — I checked. CMSIS-NN provides hand-optimized kernels for the DSP extension and Helium behind one API, bit-exact with TFLite's quantization scheme, so TFLite Micro picks the fast path for each core and results match what we validated on the host.

**Q.** How do you check that the compiler vectorized your loop?

**A.** 세 단계다. `-Rpass=loop-vectorize`/`-Rpass-missed`/`-Rpass-analysis`로 remark를 보고, `-S`로 핫 루프 어셈블리에서 벡터 레지스터와 기대 명령을 확인하고, 마지막으로 잰다. remark만 믿으면 안 된다: fp32 내적이 "vectorized"라고 나왔는데 덧셈은 스칼라 체인이어서 `-ffast-math`판보다 12배 느렸다. 최적화 레벨(`-Os`에서는 벡터화가 꺼졌다)과 `-mcpu`도 확인한다.

> I look at the optimization remarks, then read the assembly of the hot loop for vector registers and the instructions I expect, and then I measure. Remarks alone can mislead — a float dot product was reported as vectorized, but the adds were an in-order scalar chain, twelve times slower than the reassociated version. I also check the optimization level, since -Os turned vectorization off, and that -mcpu matches the real core.

**Q.** M4F, M55 with Ethos-U55, or A55 — which would you put on the always-on path of a wearable?

**A.** always-on은 평균 전력으로 판단한다. A55는 가장 빠르지만 클러스터와 DRAM을 상시 켜야 해서 대기 비용이 크다. M4F는 싸고 성숙했지만 SMLAD 경로로는 작은 KWS도 가동률이 높다. M55+U55는 MCU 전력 도메인 안에서 MAC/cycle을 크게 늘려 가동률을 몇 % 이하로 낮추므로 always-on ML에 가장 맞다. 최종 결정은 모델 MAC·SRAM 크기와 실측 에너지/추론으로 한다.

> For always-on I'd optimize average power, not peak speed. An A55 is fastest but keeping the cluster and DRAM up costs too much idle power. An M4F is cheap and mature but runs near full duty even for a small keyword model. An M55 with an Ethos-U55 gives large MACs per cycle inside the MCU power domain, so duty cycle drops to a few percent — that's usually the best fit, confirmed with measured energy per inference.

---

## 13. 직접 해보기

**연습 1 — 손계산 (SMLAD).** Rn = 0x0002_FFFD, Rm = 0x0004_0003, Ra = 10일 때 `SMLAD Rd, Rn, Rm, Ra`의 결과는?

정답: lo = (−3)·3 = −9, hi = 2·4 = 8 → 10 − 9 + 8 = **9**.

**연습 2 — 손계산 (SXTB16).** 메모리 int8 배열 [−1, 5, 7, −128]을 리틀엔디언 워드로 읽고, `SXTB16`과 `SXTB16(ROR #8)`의 결과를 16진수로 써라.

정답: 워드 = 0x8007_05FF. SXTB16 → 바이트 0(0xFF = −1), 바이트 2(0x07 = 7) → **0x0007_FFFF**. ROR 8 → 0xFF80_0705 → 바이트 0(0x05 = 5), 바이트 2(0x80 = −128) → **0xFF80_0005**.

**연습 3 — 손계산 (SDOT 오버플로).** K = 4096인 int8 내적을 SDOT 누산기 2개(각 lane 4개)로 계산한다. lane 하나에 더해지는 곱의 개수와 최악의 절댓값은? int32로 안전한가?

정답: 누산기 2개 × lane 4개 = 8개 lane에 나눠지므로 lane당 4096/8 = 512개 곱, 최악 512 × 16384 = 2²³ ≈ 8.4 M < 2³¹ → **안전**.

**연습 4 — 코드 (GEMV 레지스터 블로킹).** 5.2절 `dot_sdot`을 고쳐 **W의 4행을 동시에** 계산하는 `gemv_sdot_4rows`를 짜라(x 로드 1번을 4행이 공유). 결과를 스칼라와 비교하고 GEMV의 71 GMAC/s가 얼마나 오르는지 재라.

힌트: 누산기 4개(또는 8개), 한 반복에 x 로드 1개 + W 로드 4개 + SDOT 4개. 바이트/MAC이 2 → 1.25로 준다. L1 대역폭 한계(5.4절)가 올라가는지 확인.

**연습 5 — 코드 (자동 벡터화 사냥).** 6.2절 `prefix`(prefix sum)는 벡터화되지 않았다. `y`를 두 배열로 나눠 짝수/홀수 부분합을 따로 하는 식으로는 왜 여전히 안 되는지 설명하고, 대신 **블록 단위 prefix sum**(블록 안은 벡터 스캔, 블록 사이는 스칼라 carry)을 써 보라. `-Rpass`로 어떤 루프가 벡터화됐는지 확인한다.

힌트: 두 배열로 나눠도 각자 loop-carried 의존이 남는다. 블록 방식은 carry만 순차다.

**연습 6 — 어셈블리 읽기 (Helium).** 3.4절 `dot_s8_mve`를 `-march=armv8.1-m.main+mve`(정수 MVE만)로, 그리고 `int16_t` 버전(`vldrhq_z_s16`, `vctp16q`, `vmladavaq_p_s16`)으로 바꿔 컴파일하라. `dlstp` 뒤의 숫자와 루프 한 번의 원소 수가 어떻게 바뀌나?

정답/힌트: `dlstp.16`이 되고 한 반복에 원소 8개(128/16). 명령 수는 같고 MAC 수가 절반.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| 프로파일 (A/R/M) | Arm 아키텍처의 세 갈래 | 애플리케이션 / 실시간 / 마이크로컨트롤러 |
| Armv7E-M | M4·M7의 아키텍처 | "E" = DSP 확장(SIMD32) 포함 |
| Armv8.1-M | M55·M85의 아키텍처 | MVE(Helium)와 low-overhead loop 추가 |
| DSP 확장 / SIMD32 | 32-bit 범용 레지스터 안의 SIMD | 2×16 또는 4×8 lane, SMLAD·SXTB16 등 |
| SMLAD | Signed Multiply Accumulate Dual | 16×16 곱 두 개를 32-bit에 누산, MAC 2개/명령 |
| SXTB16 | Sign-extend Byte 16 | 바이트 0·2를 int16 두 칸으로 부호 확장 |
| Helium / MVE | M-profile Vector Extension | 128-bit 벡터, Q0~Q7, beat 실행, tail predication |
| beat | 128-bit 명령의 32-bit 조각 | 구현이 tick당 1·2·4 beat를 처리 |
| low-overhead loop | DLS/WLS/LE | 루프 끝 분기 비용을 하드웨어가 없앰 |
| tail predication | DLSTP/LETP, VCTP | 마지막 반복에서 남은 원소만 lane 활성 |
| VMLADAV | Vector Multiply Add Dual Accumulate Across Vector | lane 곱의 합을 범용 레지스터에 누산 |
| NEON / Advanced SIMD | A-profile 128-bit SIMD | V0~V31, int8~fp64 |
| SDOT / UDOT | Dot product 명령 | lane마다 int8 4쌍 내적 → int32 누산 |
| i8mm / SMMLA | Int8 Matrix Multiply | 2×8 · 8×2 → 2×2 int32, MAC 32개 |
| BFDOT / BFMMLA | bf16 dot / matrix | bf16 곱을 fp32에 누산 |
| SVE / SVE2 | Scalable Vector Extension | 벡터 길이 128~2048 bit, VLA, predicate |
| VLA | Vector-Length Agnostic | 벡터 길이를 모른 채 짜는 코드 |
| 레지스터 블로킹 | 출력 타일을 레지스터에 들고 계산 | 로드 재사용 ↑, 독립 누산기로 latency 숨김 |
| packing | 커널이 원하는 순서로 데이터 재배치 | SMMLA의 두 행 interleave |
| auto-vectorization | 컴파일러의 자동 SIMD 변환 | `-Rpass=loop-vectorize`로 확인 |
| float ABI | 부동소수점 인자 전달 규약 | soft / softfp / hard |
| CMSIS-Core / DSP / NN | Arm의 Cortex-M 소프트웨어 표준 | 코어 헤더 / 신호처리 / 신경망 커널 |
| big.LITTLE / DynamIQ | 이종 코어 구성 | 큰 코어 + 작은 코어, DynamIQ는 한 클러스터에 혼합 |

---

## 15. 요약 & 체크리스트

Arm 코어는 M(MCU), R(실시간), A(애플리케이션) 세 프로파일로 나뉘고, int8 연산 능력은 아키텍처 버전이 정한다. Armv7E-M(M4/M7)은 32-bit 레지스터 안의 SIMD(SMLAD, MAC 2개)를 주는데 int8 곱셈이 없어서 SXTB16으로 int16으로 넓혀 써야 하고, 컴파일러는 int8 C 루프를 이렇게 바꿔 주지 못해서 CMSIS-NN 같은 손 최적화 커널이 필요하다(int16은 `-O3`에서만 SMLAD가 나왔다). Armv8.1-M(M55/M85)의 Helium은 128-bit 벡터를 beat로 나눠 실행하고, VMLADAV와 tail-predicated low-overhead loop로 int8 내적을 명령 4개짜리 루프로 만든다 — 이건 평범한 C도 자동 벡터화로 나왔다. A-profile NEON은 SDOT(명령당 16 MAC)과 i8mm SMMLA(32 MAC)를 주고, 이 Mac에서 스칼라 약 3 → GEMV SDOT 71 → 레지스터 블로킹 GEMM 약 199 GMAC/s를 쟀다. GEMV는 재사용이 없어 L1 로드에 묶이고, GEMM은 누산기 16개로 파이프를 채워야 천장(약 222)에 닿는다. M2에서 SMMLA는 명령 처리량이 SDOT의 절반이라 MAC 천장이 같았다. 컴파일러 벡터화는 remark와 어셈블리와 측정으로 확인해야 한다: fp32 reduction은 "vectorized"여도 스칼라 체인이었고, `-Os`는 벡터화를 껐고, `-mcpu`가 SDOT 사용 여부를 바꿨다. 웨어러블에서는 always-on에 M4F/M55(+U55), 메인에 A55/A7x를 두고 평균 전력으로 판단한다.

- [ ] A/R/M 프로파일과 Armv7E-M, Armv8.1-M, Armv8.2-A, Armv9-A가 각각 어떤 SIMD를 주는지 말할 수 있다
- [ ] SMLAD와 SXTB16(+ROR 8)을 손으로 계산하고, M4에서 int8 내적을 짜는 법을 설명할 수 있다
- [ ] `-O2`와 `-O3`에서 M4 int16 내적 어셈블리가 어떻게 다른지(SMLABB vs SMLAD) 보여 줄 수 있다
- [ ] Helium의 Q 레지스터, beat, DLSTP/LETP tail predication, VMLADAV를 그림으로 설명할 수 있다
- [ ] SDOT lane과 SMMLA 2×2 타일을 손으로 계산할 수 있다
- [ ] NEON intrinsics로 int8 GEMV/GEMM을 짜서 스칼라와 bit-exact 비교하고 GMAC/s를 잴 수 있다
- [ ] 누산기 개수와 처리량의 관계(latency × throughput)를 설명하고, spill을 어셈블리에서 찾을 수 있다
- [ ] `-Rpass` remark와 `-S`로 벡터화 여부를 확인하고, fp32 reduction·aliasing·`-Os` 함정을 설명할 수 있다
- [ ] `-mcpu`, `-mfpu`, `-mfloat-abi`(soft/softfp/hard)의 차이를 어셈블리로 보여 줄 수 있다
- [ ] 웨어러블의 always-on과 main compute에 M4F / M55+U55 / A55 중 무엇을 왜 둘지 가동률 계산으로 말할 수 있다

---

## 참고 자료

- Arm, **Armv7-M Architecture Reference Manual** (DDI 0403) — SMLAD, SXTB16 등 DSP 확장 명령 정의
- Arm, **Armv8-M Architecture Reference Manual** (DDI 0553) — Armv8.1-M, MVE(Helium), low-overhead loop 포함
- Arm, **Arm Architecture Reference Manual for A-profile architecture** (DDI 0487) — NEON, SDOT, SMMLA, SVE/SVE2
- Arm, 각 코어의 Technical Reference Manual과 Software Optimization Guide (Cortex-M4/M7/M55/M85, Cortex-A55/A7x) — 명령 latency·처리량
- Arm, "Introduction to the Armv8.1-M architecture" 백서와 Helium 소개 자료 (developer.arm.com) — beat, instruction overlap
- ACLE (Arm C Language Extensions): https://arm-software.github.io/acle/ — `__smlad`, `arm_neon.h`, `arm_mve.h`, `arm_sve.h`, 기능 매크로
- Arm Intrinsics 검색: https://developer.arm.com/architectures/instruction-sets/intrinsics/ — NEON/MVE/SVE intrinsic별 명령·의미
- CMSIS-NN: https://github.com/ARM-software/CMSIS-NN · CMSIS-DSP: https://github.com/ARM-software/CMSIS-DSP · CMSIS 6: https://github.com/ARM-software/CMSIS_6
- L. Lai, N. Suda, V. Chandra, "CMSIS-NN: Efficient Neural Network Kernels for Arm Cortex-M CPUs", arXiv:1801.06601 (2018)
- LLVM Auto-Vectorization 문서: https://llvm.org/docs/Vectorizers.html · Clang 최적화 remark(`-Rpass`): https://clang.llvm.org/docs/UsersManual.html
- Joseph Yiu, "The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors" (3판) — M4 DSP 명령, FPU, float ABI
- 이 노트 시리즈: C1(requantization, CMSIS-NN 개요), D3(roofline, NEON FMA 천장), D4(GEMV vs GEMM), 그리고 E1·E3·E4·E5·E7·E8·E9, J3, M1·M3
