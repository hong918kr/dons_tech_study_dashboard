# B03. Linux·Android BSP — Cortex-A 쪽에서 "BSP"라는 단어가 뜻하는 것

> **시리즈**: BSP 집중 3/6 · **선행**: B00(BSP 정의·전략), B01(BSP 해부), B02(Zephyr 보드 포팅) · **JD 근거**: "Own BSP development, peripheral driver integration (SPI, I2C, UART, I2S), and RTOS task scheduling" / "Experience with ARM Cortex-M or Cortex-A processors and associated toolchains"
> **Don 상태**: ❌ 약함 — Cortex-R/M bare-metal과 FPGA pre-silicon bring-up은 강하지만, Cortex-A + Linux + Android 스택은 레쥬메에 근거가 없다. 이 노트의 목적은 "해봤다"고 말하게 만드는 게 아니라 **지도를 정확히 알고 경계를 정직하게 긋게** 하는 것이다.
> **이 노트를 다 읽으면**: ① BootROM에서 Android init까지 부트 체인을 단계 이름과 실행 EL까지 붙여 화이트보드에 그릴 수 있다 ② Linux devicetree와 Zephyr devicetree가 왜 문법은 같은데 완전히 다른 물건인지 설명할 수 있다 ③ "Linux BSP는 안 해봤습니다"를 손해 없이, 오히려 신뢰를 얻는 방식으로 말할 수 있다.

---

## 0. 큰 그림

### 0.1 왜 이 노트가 있나

Hark 공고 제목이 `Embedded Software Engineer, BSP`로 바뀌었다(B00 참고). 그런데 "BSP"라는 단어는 두 세계에서 서로 다른 것을 가리킨다.

| 세계 | BSP가 가리키는 것 | 크기 | Don |
|---|---|---|---|
| MCU / RTOS (Cortex-M, Zephyr, FreeRTOS) | 보드 파일 몇 개 + devicetree + Kconfig + 핀 먹싱 + 클럭 설정 + 드라이버 바인딩 | 수백~수천 줄 | 🟡 개념은 명확, 실무는 Zephyr 실습으로 메움 (B02) |
| AP / Linux (Cortex-A, Android) | 부트로더 + TF-A + 커널 포크 + DTS + 벤더 드라이버 + HAL + 빌드 메타데이터 전체 | 수백만 줄, git 저장소 여러 개 | ❌ 경험 없음 — 이 노트의 대상 |

context 2.2절의 단서로 보면 Hark 기기는 **Qualcomm SoC(Cortex-A, Android) + 저전력 always-on MCU** 조합일 가능성이 크다 [추정]. 그렇다면 이 역할의 무게중심은 MCU 쪽이지만, **SoC 쪽 BSP와 매일 부딪힌다**. 부딪히는 지점은 IPC, 전원/wake 시퀀스, 펌웨어 업데이트 경로, 오디오 경로 소유권이다.

### 0.2 두 프로세서 기기의 전체 그림

```
 [Cortex-A SoC / Android]            [always-on MCU]
  Apps / Framework                    앱 로직
  HAL (AIDL, vendor 파티션)           RTOS (Zephyr?)
  Linux kernel + vendor 모듈          BSP / 드라이버
  DTB (런타임 전달)                   DT (빌드타임 매크로)
  U-Boot / ABL                        부트로더 / MCUboot
  BootROM → TF-A                      Reset vector
          ▲                                 ▲
          └──── IPC (UART/SPI/공유메모리) ───┘
                + GPIO wake / 전원 시퀀스
  ← B03이 다루는 쪽 →          ← B01/B02가 다루는 쪽 →
```

암기 대상은 **이름과 순서**다. 구현 경험이 없는 상태에서 세부 API를 외우면 꼬리질문에서 바로 바닥이 보인다. 대신 "이 층의 이름은 이것이고, 이 층이 푸는 문제는 이것이고, 내가 해본 층은 여기까지다"를 정확히 말하는 쪽이 훨씬 강하다. 8절이 그 경계선을 명시한다.

---

## 1. 부트 체인 — 전원 인가에서 Android init까지

### 1.1 전체 순서 한 장

```
 전원 인가 / PMIC 시퀀스 OK / 리셋 해제
   ▼ [BootROM]   SoC 내부 마스크 ROM, 수정 불가. 부트 미디어 선택(strap/eFuse)
   │             서명 검증의 root of trust 시작점
   ▼ [BL1]       TF-A "AP Trusted ROM", EL3 (BootROM이 겸하는 SoC도 많다)
   ▼ [BL2]       TF-A "Trusted Boot Firmware", Secure-EL1
   │             DRAM 초기화(플랫폼에 따라) + 다음 이미지들을 로드
   ▼ [BL31]      TF-A "EL3 Runtime Software", EL3 — 상주한다. PSCI 제공
   │             (선택) [BL32] "Secure-EL1 Payload" = OP-TEE 등 TEE
   ▼ [BL33]      TF-A "Non-trusted Firmware", EL2/EL1 = U-Boot / UEFI / ABL
   ▼ [Linux kernel]  x0 = DTB 물리주소, MMU off 상태로 진입
   ▼ [initramfs / first-stage init] → [init] → [zygote] → 앱
```

### 1.2 BootROM — 고칠 수 없는 첫 코드

- SoC 다이 안에 구워진 마스크 ROM이다. 양산 후 수정이 불가능하므로 **chain of trust의 뿌리**가 된다.
- 하는 일: 최소 클럭 설정, SRAM 사용 가능하게 만들기, strap 핀/eFuse를 읽어 부트 미디어 선택(eMMC/UFS/SD/USB), 다음 이미지를 SRAM으로 읽어 서명 검증, 점프.
- 이 단계에서 DRAM은 보통 아직 안 산다. 그래서 다음 스테이지는 **내부 SRAM에 들어갈 만큼 작아야** 한다. 이것이 부트 체인이 여러 단으로 쪼개진 근본 이유다.
- Don 접점: SSD 컨트롤러도 똑같다. ROM code → SRAM에 로드된 loader → DRAM 초기화 → 본 펌웨어. **"작은 ROM이 큰 이미지를 SRAM 제약 안에서 단계적으로 끌어올린다"는 구조는 동일**하다. 이건 정직하게 쓸 수 있는 비유다.

### 1.3 TF-A 스테이지 이름 — 정확히 외울 것

Trusted Firmware-A 공식 문서 기준(확인함, 참고 자료 표 참조).

| 스테이지 | 공식 명칭 | 실행 EL | 역할 |
|---|---|---|---|
| BL1 | AP Trusted ROM | EL3 | 리셋 벡터에서 시작. BL2를 로드·검증 |
| BL2 | Trusted Boot Firmware | Secure-EL1 (AArch64) | BL31/BL32/BL33 이미지를 플랫폼 저장소에서 로드 |
| BL31 | EL3 Runtime Software | EL3 | 부팅 후에도 **상주**. PSCI 등 런타임 서비스 제공 |
| BL32 | Secure-EL1 Payload (선택) | Secure-EL1 | TEE (예: OP-TEE) |
| BL33 | Non-trusted Firmware | EL2 또는 EL1 | normal world의 부트로더 = U-Boot / UEFI / ABL |

- AArch32에서는 BL31 대신 **SP_MIN**이 "EL3 Runtime Software" 역할을 한다. BL2가 EL3에서 도는 빌드 옵션도 있으니 면접에서 단정하지 말 것.
- 이미지들은 보통 **FIP (Firmware Image Package)** 하나로 묶인다: "packing bootloader images (and potentially other payloads) into a single archive that can be loaded by TF-A from non-volatile platform storage". 구조는 목차 + 페이로드.
- **PSCI**는 면접 가치가 높다. 커널이 다른 코어를 깨우거나(CPU_ON) 끌 때(CPU_OFF) `SMC`로 BL31을 호출한다. 즉 **멀티코어 전원 제어의 실제 주인은 커널이 아니라 EL3에 상주하는 BL31**이다.

### 1.4 U-Boot의 단계 이름

U-Boot 공식 문서 기준(확인함). "U-Boot goes through the following boot phases where TPL, VPL, SPL are optional":

| 단계 | 정의(문서 표현) |
|---|---|
| TPL | "Very early init, as tiny as possible. This loads SPL (or VPL if enabled)." |
| VPL | A/B verified boot 선택을 위한 검증 단계(선택) |
| SPL | "Sets up SDRAM and loads U-Boot proper. It may also load other firmware components." |
| U-Boot proper | "U-Boot proper, containing the command line and boot logic." |

대부분 보드는 SPL + U-Boot proper만 쓴다. SPL이 TF-A의 BL31을 로드하거나 Linux를 직접 띄우는 구성도 문서에 명시돼 있다. 즉 **TF-A와 U-Boot의 경계는 SoC마다 다르다** — "BL33은 항상 U-Boot"라고 말하면 틀린다.

### 1.5 Qualcomm 계열 이름 — 여기서는 반드시 조심할 것

Qualcomm SoC의 부트 스테이지는 흔히 PBL → SBL/XBL → ABL(LK 기반) → kernel 식으로 이야기되고 TZ/QSEE가 별도로 들어간다. 그런데 **이 이름과 단계 구성은 칩 세대·제품군마다 다르고, 공개된 1차 문서가 거의 없다.** 대부분의 설명은 커뮤니티·역공학 문서이고 벤더 BSP는 NDA·포털 배포다. 면접에서의 안전한 표현:

> "Qualcomm 플랫폼은 세대마다 스테이지 이름이 달라서 제가 외운 이름을 단정적으로 말하진 않겠습니다. 구조 자체는 ARM의 일반적인 패턴 — 마스크 ROM → 작은 로더가 DRAM을 세움 → EL3 런타임 상주 → normal world 부트로더 → 커널 — 로 이해하고 있고, 실제 이름과 흐름은 벤더 BSP 문서를 받아서 맞추겠습니다."

이건 약점 고백이 아니라 **벤더 플랫폼을 다뤄 본 사람의 말투**다. Don의 Apple 경험(벤더 실리콘 + NDA 문서 기반 통합)이 이 말투를 정당화한다.

### 1.6 커널 진입 규약 (arm64)

커널 문서 `arch/arm64/booting` 기준(확인함). 부트로더가 지켜야 하는 것:

- 커널이 쓸 RAM을 전부 초기화해 두고, DTB를 **8바이트 경계**에 **2MB 이하**로 배치할 것.
- 진입 시 레지스터: `x0` = "physical address of device tree blob (dtb) in system RAM", `x1`/`x2`/`x3` = 0 (예약).
- MMU는 off, 모든 인터럽트는 `PSTATE.DAIF`로 마스크. 로드된 커널 이미지 영역은 **PoC(Point of Coherency)까지 clean**되어 있어야 한다.
- 진입 EL은 **EL2 권장**(가상화 확장 사용 가능), EL1도 허용. 모든 CPU가 같은 EL로 진입해야 한다.
- 이미지 헤더 64바이트에 `magic = 0x644d5241` ("ARM\x64"), `text_offset`, `image_size`, `flags`가 들어 있다. 이미지는 2MB 정렬 base로부터 `text_offset` 위치에 놓는다.

이 한 항목은 면접에서 매우 잘 먹힌다. **"커널 진입점에서 x0에 DTB 물리주소가 들어 있고 MMU는 꺼져 있다"**는 한 문장이, 부트 체인을 책으로만 읽은 사람과 규약 수준으로 이해한 사람을 가른다.

### 1.7 Android 쪽 이미지와 파티션

Android 공식 문서 기준(확인함). 부트로더는 "vendor-proprietary image responsible for bringing up the kernel on a device"이고, `boot.img` / `vendor_boot.img` / `init_boot.img`와 벤더 독자 부트 이미지를 로드한다. 부트 플로우는 메모리 초기화 → Verified Boot로 기기 검증 → 부트 파티션(`boot`, `dtbo`, `init_boot`, `recovery`) 검증 → A/B 슬롯 결정 → recovery 여부 결정 → 커널·ramdisk 로드다. 커널 커맨드라인은 부트로더·device tree·defconfig·`boot.img`에서 합쳐지는데, **Android 12 이상은 `androidboot.*`를 커맨드라인이 아니라 bootconfig로 전달**한다.

`dtbo` 파티션은 이름 그대로 **devicetree overlay**를 담는다. 같은 커널·같은 base DTB에 보드 리비전별 overlay를 얹는 방식이고, B01/B02의 "보드 리비전 관리"와 정확히 같은 문제를 Linux 쪽에서 푸는 방법이다 — 면접에서 연결하기 좋은 지점.

### 1.8 init — 커널 다음에 무슨 일이 일어나나

커널이 마지막에 하는 일은 사용자 공간의 첫 프로세스(PID 1)를 실행하는 것이다. Android에서 그 PID 1은 `init`이고, `.rc` 파일로 서비스와 트리거를 기술한다(`on early-init`, `on boot`, `service <name> <path>` 같은 스탠자가 **기동 순서**를 정의한다). 일반 임베디드 Linux에서는 BusyBox init이나 systemd가 그 자리에 온다. 그 위로 Android는 `zygote` → 시스템 서버 → 앱 순으로 올라간다.

### 1.9 Cortex-M 부트와 나란히 놓고 보기 (Don 경험과의 접점)

| 항목 | Cortex-M (Don이 아는 쪽) | Cortex-A + Linux |
|---|---|---|
| 첫 실행 | 벡터 테이블 [0]=MSP 초기값, [1]=Reset 핸들러 | BootROM 고정 주소 |
| 초기 코드 위치 | 내장 flash에서 XIP | 내부 SRAM으로 복사 후 실행 |
| DRAM | 대개 없음(내장 SRAM) | 반드시 별도 스테이지에서 초기화 |
| 주소 변환 | 없음(선택적 MPU) | MMU + 페이지 테이블, 커널이 켠다 |
| 특권 레벨 | Thread/Handler, privileged/unprivileged | EL0/EL1/EL2/EL3 + Secure/Non-secure |
| 다음 단계 점프 | `.data` 복사, `.bss` 클리어, `main()` | 이미지 로드 + **서명 검증** + EL 전환 |
| 멀티코어 기동 | 대개 단일 코어 | PSCI `CPU_ON`으로 EL3가 깨운다 |
| "BSP"의 크기 | 파일 몇 개 | 저장소 여러 개 |

이 표를 외워 두면 "Cortex-M과 Cortex-A 차이를 설명해보라"는 질문(J10 Q10/Q11과 겹침)에 30초로 답할 수 있다. 그리고 **모르는 쪽을 아는 쪽으로 번역하는 능력**을 보여 준다.

---

## 2. Device Tree — Linux와 Zephyr는 문법만 같다

### 2.1 DT가 푸는 문제

커널 문서 `devicetree/usage-model` 기준(확인함). Linux가 DT를 쓰는 용도는 세 가지다.

1. **Platform identification** — 부팅 초기에 어떤 머신인지 식별해 머신별 fixup을 적용한다.
2. **Runtime configuration** — 펌웨어가 커널에 데이터를 넘기는 통로. 커널 파라미터, initrd 위치 등이 `/chosen` 노드에 온다.
3. **Device population** — 하드코딩된 플랫폼 정의 대신, 파싱한 데이터로 디바이스 구조체를 동적으로 만든다.

문서의 핵심 표현: DT는 "a language for decoupling the hardware configuration from the board and device driver support in the Linux kernel"이다. 즉 **보드가 하나 늘 때마다 커널에 C 코드를 추가하던 시대를 끝내려고 나온 것**이다(ARM 쪽 board file 폭발 문제).

### 2.2 최소 골격

```
/ {
    compatible = "myvendor,myboard", "qcom,someplatform";
    model = "My Board rev A";

    chosen {
        bootargs = "console=ttyMSM0,115200n8";
    };

    i2c3: i2c@a90000 {                     /* [예시] 주소 */
        compatible = "myvendor,i2c-controller";
        reg = <0x0 0x00a90000 0x0 0x1000>;
        interrupts = <0 123 4>;            /* [예시] */
        clocks = <&gcc GCC_I2C3_CLK>;
        pinctrl-names = "default", "sleep";
        pinctrl-0 = <&i2c3_active>;
        pinctrl-1 = <&i2c3_sleep>;
        #address-cells = <1>;
        #size-cells = <0>;

        imu@68 {
            compatible = "invensense,icm42688";  /* [예시] */
            reg = <0x68>;
            vdd-supply = <&pm_l7>;
            interrupt-parent = <&tlmm>;
            interrupts = <42 1>;                 /* [예시] */
        };
    };
};
```

읽는 순서는 항상 같다. `compatible`로 **누가 이 노드를 맡나**, `reg`로 **어디에 있나**, `interrupts`로 **어떻게 알리나**, `clocks`/`*-supply`/`pinctrl-*`로 **살아나려면 뭐가 필요한가**.

### 2.3 compatible과 매칭

루트 노드의 `compatible`은 "a sorted list of strings starting with the exact name of the machine, followed by an optional list of boards it is compatible with sorted from most compatible to least"다. 커널은 `machine_desc` 테이블을 돌며 `dt_compat` 리스트와 비교해 가장 잘 맞는 항목을 고르고, 그 `machine_desc`가 플랫폼별 셋업 훅을 제공한다. 드라이버 쪽은 `of_device_id` 테이블의 compatible 문자열로 바인딩된다. 문자열 형식은 **`<vendor>,<device>`** 이고, 새 compatible은 커널 트리의 바인딩 스키마(YAML)로 문서화해야 한다.

### 2.4 노드가 디바이스가 되는 경로

`of_platform_populate()`가 디바이스 생성을 시작한다. 루트 레벨에서 `compatible`을 가진 노드는 디바이스로 간주돼 **`platform_device`로 등록**되고, `of_default_bus_match_table`에 맞는 노드의 자식들도 같은 방식으로 등록된다. 반면 I2C 컨트롤러 노드의 자식(위 예의 `imu@68`)은 platform device가 아니라 **I2C 코어가 `i2c_client`로** 만든다(SPI도 마찬가지). 이 구분을 모르면 드라이버 타입을 잘못 고른다.

### 2.5 Linux DT vs Zephyr DT — 이 표가 이 절의 핵심

Zephyr 공식 문서 기준(확인함): 입력은 `.dts`, `.dtsi`, `.overlay`, 바인딩 `.yaml`이고, 출력은 빌드 디렉터리의 `zephyr.dts.pre`(전처리된 소스), `include/generated/zephyr/devicetree_generated.h`(생성된 매크로), `zephyr.dts`(최종 병합 결과)다.

| 축 | Linux | Zephyr |
|---|---|---|
| DT가 소비되는 시점 | **런타임** — 부트로더가 DTB를 메모리에 두고 `x0`로 넘김 | **빌드 타임** — 매크로로 펼쳐져 이미지에 박힘 |
| 산출물 | `.dtb` 바이너리 (별도 파티션/이미지) | `devicetree_generated.h` (C 헤더) |
| 바인딩 | `Documentation/devicetree/bindings/*.yaml` (문서/검증용) | `dts/bindings/*.yaml` (**빌드가 실제로 읽는다**) |
| 드라이버 매칭 | `of_match_table`의 compatible로 런타임 매칭 | `DT_DRV_COMPAT` + `DEVICE_DT_INST_DEFINE`로 **컴파일 타임 인스턴스화** |
| 없는 디바이스 | 노드가 없으면 그냥 probe 안 됨, 커널 크기는 그대로 | 노드가 없으면 **코드 자체가 생성되지 않음**(플래시 절약) |
| DT 바꾸면 | 커널 재빌드 없이 DTB만 교체 가능 | **반드시 재빌드** |
| overlay | `dtbo` 파티션, 런타임/부트로더가 적용 | `.overlay` 파일, 빌드 시 병합 |

면접용 한 문장:

> "문법은 같지만 소비 시점이 다릅니다. Linux는 DTB를 런타임에 파싱해서 platform device를 만들고, Zephyr는 빌드할 때 매크로로 펼쳐서 아예 코드로 굳힙니다. 그래서 Zephyr는 DT를 바꾸면 반드시 다시 빌드해야 하고, 대신 안 쓰는 디바이스는 바이너리에 안 들어갑니다."

### 2.6 흔한 DT 함정

- `status = "okay"` / `"disabled"`를 안 바꿔서 드라이버가 안 뜬다. overlay로 노드를 추가해도 base에서 disabled면 그대로다.
- `#address-cells` / `#size-cells` 불일치로 `reg` 해석이 어긋나 주소가 엉뚱한 곳을 가리킨다.
- `interrupt-parent`를 상속에 맡겼는데 상위가 다른 컨트롤러라서 엉뚱한 라인에 붙는다.
- DT에 **정책을 넣는다**. DT는 하드웨어 기술이지 소프트웨어 정책이 아니다. "어떤 알고리즘을 쓸지"는 DT에 들어가면 안 된다. 리뷰에서 자주 지적되는 경계다.
- 라벨(`i2c3:`)과 노드 이름(`i2c@a90000`)을 혼동한다. 참조는 라벨(`&i2c3`)로 한다.

---

## 3. 커널 드라이버와 platform device

### 3.1 드라이버 모델의 골격

Linux는 **bus / device / driver** 삼각형으로 되어 있다. 버스가 디바이스와 드라이버를 짝지어 주고(match), 짝이 맞으면 드라이버의 `probe()`가 불린다. SoC 안에 통합돼 있어서 열거(enumeration)가 불가능한 블록들은 **platform bus**라는 pseudo-bus에 올린다.

커널 문서 표현: platform device/driver는 "legacy port-based devices and host bridges to peripheral buses, and most controllers integrated into system-on-chip platforms"를 위한 것이다.

### 3.2 최소 platform driver

```c
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>

struct myctl {
    void __iomem     *base;
    struct clk       *clk;
    struct regulator *vdd;
    int               irq;
};

static int myctl_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct myctl *p;
    int ret;

    p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
    if (!p)
        return -ENOMEM;

    /* 아래 네 줄이 DT의 reg / interrupts / clocks / vdd-supply 에 1:1 대응한다.
     * 에러 처리는 지면 관계로 생략 — 실제 코드는 IS_ERR / 음수 반환을 전부 검사한다. */
    p->base = devm_platform_ioremap_resource(pdev, 0);
    p->irq  = platform_get_irq(pdev, 0);
    p->clk  = devm_clk_get(dev, NULL);
    p->vdd  = devm_regulator_get(dev, "vdd");

    ret = regulator_enable(p->vdd);        /* 전원 먼저 */
    if (ret)
        return ret;

    ret = clk_prepare_enable(p->clk);      /* 그다음 클럭 */
    if (ret) {
        regulator_disable(p->vdd);
        return ret;
    }

    platform_set_drvdata(pdev, p);
    return 0;
}

/* 해제는 probe의 역순. 반환형은 커널 버전 의존(아래 주의 참고) */
static void myctl_remove(struct platform_device *pdev)
{
    struct myctl *p = platform_get_drvdata(pdev);

    clk_disable_unprepare(p->clk);
    regulator_disable(p->vdd);
}

static const struct of_device_id myctl_of_match[] = {
    { .compatible = "myvendor,myctl" },
    { }
};
MODULE_DEVICE_TABLE(of, myctl_of_match);

static struct platform_driver myctl_driver = {
    .probe  = myctl_probe,
    .remove = myctl_remove,
    .driver = {
        .name           = "myctl",
        .of_match_table = myctl_of_match,
    },
};
module_platform_driver(myctl_driver);

MODULE_LICENSE("GPL");
```

> 주의: `remove` 콜백의 시그니처(`int` 반환 vs `void` 반환)는 커널 버전에 따라 바뀌었다. 위 코드는 최신 트리 기준의 `void` 형태다. **버전 의존 항목**이니 면접에서 시그니처를 단정하지 말 것.

### 3.3 이 코드에서 면접용으로 뽑아낼 것 세 가지

1. **`devm_` 접두어** — device-managed. 디바이스가 detach될 때 자동 해제되므로 에러 경로에서 일일이 되돌릴 필요가 없다. bare-metal에서 손으로 관리하던 정리 코드가 프레임워크로 올라온 것.
2. **`-EPROBE_DEFER`** — probe 중 필요한 자원(레귤레이터, 클럭, GPIO 컨트롤러)이 아직 없으면 이 값을 반환하고 커널이 나중에 다시 probe한다. **이게 Linux가 초기화 순서 문제를 푸는 방식**이다. MCU BSP에서는 개발자가 순서를 직접 정하지만(Zephyr는 init level + priority), Linux는 의존성이 생길 때마다 재시도한다.
3. **바인딩 경로가 두 개** — DT가 있으면 `of_match_table`의 compatible로, 없으면 `platform_device`의 `name`으로 매칭한다(`id_table`도 있다).

### 3.4 컨트롤러 드라이버 vs 클라이언트 드라이버

| 대상 | 드라이버 타입 | 등록 | 예 |
|---|---|---|---|
| SoC 내부 I2C/SPI/I2S 컨트롤러 | `platform_driver` | `module_platform_driver` | i2c 어댑터, SPI master |
| I2C 버스에 달린 센서 | `i2c_driver` | `module_i2c_driver` | IMU, PMIC, 오디오 코덱 |
| SPI 버스에 달린 칩 | `spi_driver` | `module_spi_driver` | flash, 터치 |
| 상위 서브시스템 | IIO / input / ALSA SoC(ASoC) / hwmon | 서브시스템 API | 센서는 IIO, 마이크는 ASoC |

**센서 드라이버를 쓴다 = 보통 IIO 서브시스템에 등록한다**는 사실은 알아 두면 좋다. 오디오는 ASoC(codec driver + platform driver + machine driver 3분할)다. 여기까지만 알아도 "오디오 경로를 누가 소유하나" 같은 질문에 지도는 그릴 수 있다.

### 3.5 out-of-tree 모듈, GKI, KMI

Android 공식 문서 기준(확인함). GKI 이전에는 기기 커널의 "as much as 50% of kernel code being out-of-tree code"였다. GKI 커널은 `boot.img`에 들어가는 "a single-kernel binary plus associated loadable modules per architecture, per LTS release"이고, SoC/보드 고유 코드는 **vendor module**로 분리돼 `vendor_boot.img` / `vendor_dlkm`에 들어간다. 둘 사이에 **KMI (Kernel Module Interface)** 라는 안정 인터페이스를 둬서 "modules and kernel can be updated independently". 도입 이유는 보안 패치 백포트 비용, LTS 머지 난이도, 프레임워크가 커널 기능을 일관되게 못 쓰는 문제, 업스트림 반영 지연이다.

실무적 의미: **벤더 BSP의 상당 부분이 "커널 포크"가 아니라 "모듈 묶음"으로 온다.** 커널 버전을 올려도 벤더 드라이버를 통째로 다시 포팅하지 않아도 되는 구조를 지향한다(현실에서는 여전히 힘들다).

### 3.6 Don 접점

bare-metal 드라이버를 쓸 때 Don이 손으로 하던 것들이 Linux에서는 이름이 붙어 프레임워크로 존재한다.

| bare-metal에서 손으로 | Linux 프레임워크 |
|---|---|
| 전역 포인터로 레지스터 베이스 정의 | `reg` 프로퍼티 → `devm_platform_ioremap_resource` |
| IRQ 번호 상수 + 벡터 테이블에 핸들러 등록 | `interrupts` 프로퍼티 → `platform_get_irq` + `devm_request_irq` |
| 클럭 enable 비트를 직접 세팅 | Common Clock Framework |
| LDO enable GPIO를 직접 토글 | Regulator framework |
| 핀 먹싱 레지스터를 보드별 `#ifdef`로 | pinctrl + DT의 `pinctrl-0` |
| init 함수 호출 순서를 배열에 명시 | probe 순서 + `-EPROBE_DEFER` |

이 표는 면접에서 그대로 쓸 수 있다. "해본 적은 없지만, 제가 손으로 하던 일이 그 프레임워크의 어느 자리인지는 압니다"가 정확히 이 표다.

---

## 4. 클럭·레귤레이터·pinctrl — 공유 자원 세 개

### 4.1 왜 프레임워크가 필요한가

MCU에서는 클럭 enable 비트를 아무 데서나 켜도 된다. 소유자가 한 명이기 때문이다. Linux에서는 **하나의 클럭/레귤레이터를 여러 드라이버가 공유**한다. 그래서 세 가지가 필요하다: 참조 카운트, 부모 관계(트리), 그리고 잠들 수 있는 문맥과 아닌 문맥의 구분.

### 4.2 Common Clock Framework

커널 문서 기준(확인함). 제공자 쪽 구조체는 `struct clk_hw`, 동작은 `struct clk_ops`(`prepare`/`unprepare`, `enable`/`disable`, `set_rate`, `recalc_rate`, `set_parent`/`get_parent`)다. 핵심은 **prepare와 enable의 분리**다: "The clk_enable/clk_disable and clk_prepare/clk_unprepare pairs allow implementations to split any work between atomic (enable) and sleepable (prepare) contexts." enable lock은 spinlock이라 `.enable`은 잠들면 안 되고, prepare lock은 mutex다. 소비자는 보통 `clk_prepare_enable()` / `clk_disable_unprepare()`를 쓴다.

왜 나눴나: **PLL은 락업에 시간이 걸린다**. 락 대기는 잠드는 동작이고 인터럽트 문맥에서는 잠들 수 없다. 그래서 "느린 준비"와 "빠른 게이팅"을 분리한 것이다. Don이 PLL lock 대기를 폴링해 본 경험이 있다면 이 설계 의도를 바로 이해했다고 말할 수 있다 <확인 필요: Don이 SSD/RF에서 PLL lock 시퀀스를 직접 다뤘는지>.

### 4.3 Regulator

커널 문서 기준(확인함). `regulator_get(dev, "Vcc")` / `regulator_put()`을 `probe()`/`remove()`에서 부르고, `regulator_enable()` / `regulator_disable()`로 켜고 끈다. 전압은 `regulator_set_voltage(regulator, min_uV, max_uV)` — **마이크로볼트 단위, 범위로** 준다. 묶음 API(`regulator_bulk_get/enable/disable`), 부하·모드 API(`regulator_set_load`, `regulator_set_mode`)도 있다.

가장 중요한 문장: "This may not disable the supply if it's shared with other consumers. The regulator will only be disabled when the enabled reference count is zero." 그리고 `regulator_force_disable()`은 "will immediately and forcefully shutdown the regulator output. All consumers will be powered off."

면접 포인트: **"disable을 불렀는데 전압이 안 떨어진다"는 버그**가 여기서 나온다. 공유 레일을 다른 소비자가 아직 잡고 있기 때문이다. 저전력 디버깅에서 자주 만나는 증상이고, Don의 Power Analyzer 경험과 바로 연결된다.

### 4.4 pinctrl

커널 문서 기준(확인함). 하는 일은 셋 — 핀 열거·명명, **PINMUX**(신호를 핀에 라우팅), **PINCONF**(풀업/풀다운, 드라이브 강도). 핀은 group으로 묶이고 function(예: "spi0", "i2c0")이 group에 매핑된다: "A certain function is always associated with a certain set of pin groups."

상태(state) 이름은 `default`(probe 전에 선택), `init`, `sleep`, `idle`이다. 디바이스 probe 시 코어가 `pinctrl_bind_pins()`로 표준 상태를 자동 적용하므로 드라이버는 "boilerplate code"를 안 써도 된다. 명시적으로 쓸 때는 `devm_pinctrl_get()` → `pinctrl_lookup_state(pinctrl, PINCTRL_STATE_DEFAULT)` → `pinctrl_select_state()`. DT 표기는 `pinctrl-names = "default", "sleep";` 과 `pinctrl-0` / `pinctrl-1`로, **이름 리스트의 인덱스가 번호와 대응**한다.

저전력에서는 `sleep` 상태가 중요하다. 슬립에서 핀을 떠 있는 채로 두면 입력단이 중간 전압에 걸려 **관통 전류**가 흐르고, always-on 배터리 기기에서 수십 µA가 여기서 샌다(C05와 연결).

### 4.5 나머지 한 줄씩

- **GPIO**: descriptor API인 `gpiod_get()` / `gpiod_set_value()` 계열. DT에서는 `reset-gpios = <&tlmm 30 GPIO_ACTIVE_LOW>;` 식으로 준다(옛 정수 기반 `gpio_request`는 레거시).
- **reset controller**: SoC 내부 리셋 라인은 `devm_reset_control_get` 계열의 별도 서브시스템.
- **runtime PM / power domain(genpd)**: `pm_runtime_get_sync()` / `pm_runtime_put()`으로 디바이스 단위 자동 전원 관리. 참조 카운트 개념은 레귤레이터와 같다.

### 4.6 Don 접점 — 전원 시퀀싱

Don이 보드 bring-up에서 늘 확인하는 것(J13 2.4 power tree)이 Linux에서는 **DT의 `*-supply` + probe 안의 enable 순서 + 필요한 지연**으로 표현된다. "레일 A가 올라오고 1ms 뒤 리셋 해제, 그다음 클럭"이라는 데이터시트 요구가 드라이버 probe 코드에 그대로 박힌다. 이 대응 관계를 말할 수 있으면 "Linux는 안 해봤지만 같은 문제를 안다"가 성립한다.

---

## 5. Android — HAL, HIDL/AIDL, vendor 경계

### 5.1 층 구조

Android 공식 문서 기준(확인함), 위에서 아래로: Apps(일반/privileged/제조사) → Android Framework(Java 클래스·인터페이스, public API와 system API) → ART("a Java runtime environment provided by AOSP") → native daemon·라이브러리 → **HAL** → Linux kernel(가능한 한 hardware-agnostic 모듈과 vendor 모듈로 분리).

HAL 정의: "an abstraction layer with a standard interface for hardware vendors to implement". 존재 이유: "HALs allow Android to be agnostic about lower-level driver implementations." 즉 **프레임워크가 드라이버 구현에 의존하지 않게 하려고** 넣은 층이다.

### 5.2 HAL의 형태

**Binderized HAL**이 기본형이다: "A HAL that communicates with other processes using binder inter-process communication (IPC) calls. Binderized HALs run in a separate process from the client that uses them." 예외인 **Same Process (SP) HAL**은 OpenGL, Vulkan 등 Google이 통제하는 제한적 범주다. HAL 구현은 **vendor 파티션**에 들어가고, 벤더는 "all required HALs listed in the compatibility matrix for the release you target"를 구현해야 한다.

### 5.3 HIDL → AIDL

- HIDL(Hardware Interface Definition Language)이 먼저 나왔고 **Android 13에서 deprecated**다. 문서 권고: "Instead of HIDL, you should use Android Interface Definition Language (AIDL) for HALs." 기존 HIDL HAL은 계속 지원된다. AIDL HAL은 "Android 11 introduces the ability to use AIDL for HALs in Android, making it possible to implement parts of Android without HIDL."
- **Stable AIDL**: 프레임워크(`system.img`)와 하드웨어(`vendor.img`)가 파티션을 넘어 대화하려면 stable AIDL이어야 한다. 타입에 `@VintfStability`, 빌드 선언에 `stability: "vintf"`, 그리고 서버는 **VINTF 매니페스트에 선언**해야 한다.
- AIDL을 미는 이유로 문서가 드는 것: IPC 메커니즘 통일, 나은 버전 관리, 인터페이스를 여러 버전으로 복제하지 않고 제자리에서 진화시킬 수 있다는 점.

면접에서 이 정도면 충분하다. **"HIDL은 Android 13에서 deprecated고 새 HAL은 AIDL로 쓰며, 프레임워크-벤더 경계를 넘으려면 stable AIDL + VINTF 매니페스트 선언이 필요하다"** — 이 한 문장이 Android BSP 문맥을 안다는 신호다.

### 5.4 이 역할에서 실제로 부딪힐 지점 [추정]

context 2.7절 기준 추정이다. MCU 펌웨어 담당자가 Android 쪽과 만나는 접점:

| 접점 | Android 쪽에 필요한 것 | MCU 쪽에 필요한 것 |
|---|---|---|
| 센서 데이터 전달 | 센서 HAL 또는 커스텀 서비스 | IPC 프로토콜, 타임스탬프 동기화 |
| wake word로 SoC 깨우기 | 커널 wakeup source, wakelock 정책 | GPIO wake 라인, 디바운스, 재시도 |
| 오디오 스트림 | ASoC / audio HAL | I2S 마스터/슬레이브 결정, 클럭 소스 |
| MCU 펌웨어 업데이트 | vendor 서비스가 이미지 전달 | 부트로더, A/B 슬롯, 롤백 |
| 로그·telemetry | logcat / dumpstate 연동 | 링버퍼, 크래시 덤프 |
| factory 모드 | fastboot / 벤더 테스트 앱 | 테스트 커맨드 인터페이스 |

**이 표의 오른쪽 열이 Don의 영역**이고, 왼쪽 열은 "협업 대상"이다. 면접에서 이 구도를 먼저 제시하면 범위 설정을 Don이 주도하게 된다.

---

## 6. 빌드 시스템 — AOSP와 Yocto

### 6.1 AOSP

공식 문서 기준(확인함):

```sh
source build/envsetup.sh
lunch aosp_cf_x86_64_only_phone-aosp_current-userdebug
m
```

`lunch` 타깃 형식은 `product_name-release_config-build_variant`다. 빌드 변형 셋: `user`("limited security access and is suited for production"), `userdebug`("helps the device developers understand the performance and power of in-development releases"), `eng`("faster build time and is best suited for day-to-day development environments"). 빌드 시스템은 **Soong**, 모듈 정의는 `Android.bp`(옛 `Android.mk`는 레거시), 산출물은 `out/target/product/<device>/`의 `boot.img`·`vendor_boot.img`·`system.img`·`vendor.img` 등이다. 소스 동기화는 `repo`(여러 git 저장소를 manifest로 묶음).

### 6.2 Yocto

공식 BSP 가이드 기준(확인함). BSP 정의부터가 이 노트의 주제와 맞는다: "A Board Support Package (BSP) is a collection of information that defines how to support a particular hardware device, set of devices, or hardware platform." 부트로더 설정, 커널 설정, device tree, 드라이버, 그 외 필요한 소프트웨어를 포함한다.

```
meta-myboard/                    레이어 이름 규칙: meta-<bsp_root_name>
├── conf/layer.conf              레이어 식별
├── conf/machine/myboard.conf    머신 설정 파일 (MACHINE 변수로 선택)
├── recipes-bsp/                 부트로더·보드 고유 레시피
├── recipes-kernel/linux/        커널 레시피와 수정
└── README, 라이선스
```

- `MACHINE` 변수로 타깃을 고르면 빌드 시스템이 `conf/machine/`에서 해당 설정 파일을 찾는다.
- `KMACHINE`은 "the machine name as known by the kernel, which is sometimes a different name than what is known by the OpenEmbedded build system"이다.
- 빌드 엔진은 **BitBake**, 레시피는 `.bb`, 수정은 `.bbappend`.

### 6.3 비교와 준비 범위

| 축 | AOSP | Yocto |
|---|---|---|
| 목적 | Android 기기 전체 이미지 | 임의의 임베디드 Linux 배포판 생성 |
| 단위 | 모듈(`Android.bp`) + repo manifest | 레시피(`.bb`) + 레이어 |
| 보드 추가 | device/ 디렉터리 + product makefile | `meta-<bsp>` 레이어 + `conf/machine/*.conf` |
| 커스터마이즈 | product 상속, overlay | `.bbappend`, `PREFERRED_VERSION`, `PACKAGECONFIG` |
| Hark 가능성 [추정] | 높음 (Android 단서 있음) | 낮음 |

context의 단서(Embedded Application 공고가 Android 기반 앱, System Test 공고가 Qualcomm + Android)를 보면 **AOSP 쪽 확률이 높다** [추정]. 다만 이 역할 자체는 MCU 쪽이므로 둘 다 "이름과 구조를 안다" 수준이면 충분하다. Yocto를 굳이 파지 말 것.

---

## 7. "BSP drop"이 실제로 담고 있는 것

실리콘 벤더가 고객에게 주는 BSP 패키지의 전형적 내용물이다. 벤더마다 구성이 다르므로 **일반형**으로 정리한다.

| 항목 | 내용 | 주의할 점 |
|---|---|---|
| 부트로더 소스/바이너리 | TF-A 포트, 벤더 로더 스테이지, U-Boot 또는 ABL 포크 | 일부는 소스가 아니라 **서명된 바이너리만** 온다 |
| 커널 | 특정 LTS 버전 기반 **포크** + 벤더 패치 시리즈 | 업스트림과의 차이가 수천 커밋일 수 있다 |
| DTS/DTSI | SoC 공통 `.dtsi` + 레퍼런스 보드 `.dts` | 우리 보드용 `.dts`는 **우리가 쓴다** |
| 벤더 드라이버 | 모뎀, GPU, 카메라, 오디오, 센서 허브 | 일부는 모듈 바이너리(비공개) |
| 펌웨어 블롭 | DSP 펌웨어, 모뎀 이미지, Wi-Fi/BT 펌웨어 | 재배포 조건이 계약에 걸린다 |
| HAL 구현 | 벤더 AIDL/HIDL 서비스 | vendor 파티션용 |
| 빌드 메타데이터 | AOSP device 디렉터리 또는 Yocto 레이어 | 우리 제품용으로 복제·수정 |
| 툴 / 릴리스 노트 | flash·서명 도구, 크래시 파서, 알려진 이슈 목록 | 릴리스 노트부터 읽는 게 맞다 |
| 문서 | 레지스터 매뉴얼, 부트 흐름, 전원 시퀀스, 핀 먹싱 표 | **NDA**. 공개 URL 없음 |

### 7.1 BSP를 받은 뒤 실제로 하는 일

1. 레퍼런스 보드(EVK)로 **벤더가 준 그대로** 빌드해서 부팅시킨다. 이게 기준선이다.
2. 우리 보드용 `.dts`/`.dtsi`를 만든다 — 레퍼런스에서 복사해 없는 장치 삭제, 우리 장치 추가, 핀 먹싱·전원 매핑 수정.
3. 하나씩 켠다: 콘솔 UART → 스토리지 → PMIC/레귤레이터 → I2C/SPI 센서 → 오디오 → 무선.
4. 우리 저장소에 올리고 **벤더 릴리스와의 차이를 관리**한다(브랜치 전략, 패치 시리즈).
5. 다음 BSP 드롭이 오면 리베이스한다. **이게 진짜 고통이고, BSP 오너의 일상**이다.

"BSP를 소유한다"는 말의 실제 의미 중 하나가 **벤더 드롭과 우리 포크 사이의 차이를 관리하는 것**이다. Don은 Apple에서 벤더 실리콘을 제품에 통합해 봤으므로 "벤더가 주는 것과 제품이 필요한 것 사이의 간극을 메우는 일"이라는 프레임은 정직하게 쓸 수 있다. 다만 **"Linux BSP 리베이스를 해봤다"고는 절대 말하지 말 것.**

---

## 8. Don의 방어 범위 설정 — 이 절이 이 노트의 핵심

### 8.1 현실적인 비중 추정

> **⚠ 2026-09-29 재작성**: 아래 판단의 전제 두 개가 깨졌다. ① "Embedded Application = Android 앱" 공고는 2026-09-22에 **시스템 소프트웨어 역할로 전면 재작성**되어 더 이상 앱 자리가 아니다. ② BSP JD 자신이 2026-09-23 개정에서 필수 요건에 **eLinux와 AOSP를 이름으로** 넣었다("Hands-on experience with embedded operating systems, such as eLinux, AOSP, VxWorks and RTOSes"). 따라서 "Linux는 안 해도 된다"는 옛 결론은 폐기한다. 아래 §8.2의 깊이 3단계와 §8.3 정직 스크립트는 그대로 유효하되, **A 목록을 한 단계 올려서** 준비한다.

이 포지션은 JD 문장상 **"ARM-based SoCs and microcontrollers"** 둘 다 적혀 있고, 요건에 eLinux·AOSP가 명시돼 있다. 다만 **Responsibilities 여섯 줄에는 커널·유저스페이스 작업이 한 줄도 없다** — board bring-up, 드라이버, 벤더 통합, 하드웨어 스펙 검증, 전력·발열, 디버깅뿐이다. 반면 같은 팀의 `Embedded Software Engineer`(시스템) 공고는 "kernel, user space, and MCU domains"와 "embedded Linux"를 업무로 못박는다.

**결론**: 이 자리에서 Linux/Android는 **요건 문장으로 검증받는 영역**이지, 매일 소유하는 영역은 아닐 가능성이 크다 [추정]. 그러므로 목표는 "커널 BSP를 소유할 수 있다"가 아니라 **"면접에서 Linux·AOSP 질문에 막히지 않고, 내가 어디까지 해봤는지 정확히 말할 수 있다"** 이다. 깊이는 §8.2의 A 목록까지 확실히, B는 개념까지, C는 이름만.

### 8.2 깊이 3단계

**A. 반드시 할 수 있어야 한다 (막히면 감점)**

- 부트 체인 순서를 이름과 함께 그린다: BootROM → BL1 → BL2 → BL31(상주) → BL33(U-Boot/ABL) → kernel → init.
- 커널 진입 규약 한 줄: `x0`에 DTB 물리주소, MMU off, EL2 권장.
- devicetree가 무엇을 푸는 문제인지, Linux(런타임 DTB)와 Zephyr(빌드타임 매크로)의 차이.
- `compatible` → `of_match_table` → `probe()` 라는 바인딩 경로.
- HAL이 왜 있는지, HIDL은 deprecated고 새 HAL은 AIDL이라는 것.
- GKI = 일반 커널 + vendor module, KMI로 분리.
- AOSP 빌드 명령 3줄(`source build/envsetup.sh` / `lunch` / `m`)과 `user`/`userdebug`/`eng` 차이.

**B. 물으면 대답하되 깊이 들어가면 경계를 긋는다**

- 클럭/레귤레이터/pinctrl 프레임워크의 존재 이유와 API 이름 수준.
- `-EPROBE_DEFER`가 초기화 순서를 푸는 방식.
- `dtbo` 파티션으로 보드 리비전을 다루는 방식.
- Yocto 레이어 구조(`meta-<bsp>`, `conf/machine/*.conf`, `MACHINE`).

**C. 준비하지 말 것 (시간 낭비 + 위험)**

- 실제 커널 드라이버를 외워서 쓰기. 코딩 문제로 나올 확률이 낮고, 어설프게 쓰면 손해다.
- Qualcomm 스테이지 이름을 단정적으로 외우기. **세대마다 다르고 공개 1차 문서가 없다.**
- SELinux 정책, binder 내부 구현, ART 내부, Yocto 레시피 작성 문법.

### 8.3 정직하게 말해야 하는 것 (그대로 외울 문장)

> "제 경험은 Cortex-R과 Cortex-M bare-metal입니다. FPGA에서 실리콘이 나오기 전에 코어를 올리고 I2C, SPI, DMA, SRAM/DRAM을 하나씩 살려 본 쪽이고, Linux나 Android BSP를 소유해 본 적은 없습니다. 그쪽은 부트 체인과 devicetree, HAL 경계까지 구조는 정확히 알고 있고, 제가 손으로 하던 일 — 클럭 켜기, 레일 순서, 핀 먹싱 — 이 Linux에서는 어느 프레임워크에 대응하는지도 압니다. 다만 그 스택을 제가 운영해 봤다고는 말하지 않겠습니다."

이 문장의 구조가 중요하다.

1. 강한 사실부터 (bare-metal pre-silicon bring-up).
2. 경계를 먼저 스스로 긋는다 (안 해봤다).
3. 그래도 지도는 있다는 증거를 준다 (구체적 용어).
4. 과장하지 않겠다고 명시한다.

면접관은 3번에서 안심하고 4번에서 신뢰한다. 반대로 경계를 흐리면 첫 꼬리질문에서 무너진다.

### 8.4 절대 하지 말 것

- "Linux 드라이버도 좀 해봤습니다" — 근거 없음. 첫 꼬리질문(`probe`에서 뭘 하나?)에서 끝난다.
- "Android는 그냥 Linux죠" — HAL/vendor 경계를 모른다는 신호가 된다.
- Qualcomm 스테이지 이름을 자신 있게 나열하기 — 틀릴 확률이 높고, 이 팀엔 아는 사람이 있다.
- DT와 Kconfig의 역할을 섞어 말하기(J02 Q14와 동일한 감점 포인트), "BSP 드롭 리베이스는 해봤다"(안 해봤다).

### 8.5 하루짜리 준비 플랜

이 영역에 쓸 시간은 **하루 이하**가 적정하다. MCU 쪽(B01/B02)과 코딩 드릴이 훨씬 중요하다. 1시간 부트 체인 백지 그리기 3회 → 1시간 2.5절 DT 비교표 암기 → 30분 3.2절 probe에서 벌어지는 일 5가지 말로 설명 → 30분 5절 HAL/AIDL/GKI 용어 → 1시간 11절 QEMU 실습 1개 실제 실행(실행해 봤다는 사실 자체가 답변 근거가 된다) → 30분 8.3절 스크립트 낭독 3회.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| DT 노드에 `status = "disabled"`가 남음 | 드라이버가 전혀 probe 안 됨, 로그에 아무것도 없음 | base DTS가 비활성, overlay에서 안 바꿈 | overlay에서 `status = "okay"` 설정, `/sys/firmware/devicetree`로 실제 적용값 확인 |
| `#address-cells`/`#size-cells` 불일치 | ioremap이 엉뚱한 주소, probe 실패 또는 시스템 행 | 부모·자식 셀 수가 다름 | DTS 계층 전체를 셀 수 기준으로 재검토 |
| 레귤레이터 `disable`을 불렀는데 전압이 안 떨어짐 | 슬립 전류가 목표보다 수백 µA 높음 | 공유 레일의 다른 소비자가 참조 카운트를 잡고 있음 | 소비자 전체 파악, `regulator_summary`(debugfs) 확인, 레일 분리 검토 |
| pinctrl `sleep` 상태 미정의 | 슬립에서 핀이 float, 관통 전류 | DT에 `pinctrl-1` 없음 | sleep 상태 정의, 미사용 핀은 명시적 풀 설정 |
| probe가 계속 재시도됨 | 부팅 지연, 로그에 deferred probe 반복 | 의존 자원(클럭/레귤레이터/GPIO 컨트롤러)이 끝내 안 나타남 | `/sys/kernel/debug/devices_deferred` 확인, 공급자 드라이버가 빌드에 포함됐는지 확인 |
| 커널은 뜨는데 콘솔이 안 나옴 | 무반응처럼 보임 | `chosen/bootargs`의 console 지정이 실제 UART와 불일치 | earlycon 활성화, UART 핀 먹싱 확인 |
| 벤더 BSP 리베이스 후 일부 기능 사라짐 | 특정 센서/오디오만 동작 안 함 | 우리 패치가 새 드롭에서 유실 | 패치 시리즈로 관리, 드롭별 diff 리뷰 프로세스 |
| DTB를 갱신했는데 반영 안 됨 | 예전 설정으로 동작 | 부트로더가 예전 DTB를 로드하거나 dtbo가 덮어씀 | 어느 DTB가 실제 로드됐는지부터 확인 |
| Android HAL을 system 쪽에 넣음 | 빌드/런타임에서 접근 거부 | vendor 파티션 경계 위반 | HAL은 vendor 파티션, stable AIDL + VINTF 선언 |

---

## 10. 면접에서 말하기

**Q. Describe the boot chain of an ARM application processor running Linux.**

**A.** 전원 시퀀스가 끝나면 SoC 내부 BootROM이 돈다(root of trust). strap/eFuse로 부트 미디어를 고르고 다음 이미지를 SRAM으로 읽어 검증한다. TF-A 용어로 BL1(AP Trusted ROM, EL3) → BL2(Trusted Boot Firmware, Secure-EL1)가 DRAM을 세우고 나머지를 로드한다. BL31은 EL3 Runtime Software로 부팅 후에도 상주하며 PSCI를 제공하고, 선택적 BL32가 OP-TEE 같은 TEE, BL33이 normal world 부트로더(U-Boot 또는 Android의 ABL)다. 커널은 `x0`에 DTB 물리주소를 받고 MMU가 꺼진 상태로 진입하고, Android면 그다음 init이 `.rc`로 서비스를 올린다.

> "After the power sequence completes, the on-die BootROM runs — that's the root of trust. In TF-A terms it's BL1 at EL3, then BL2 which brings up DRAM and loads the rest. BL31 is the EL3 runtime that stays resident and provides PSCI. BL32 is an optional secure payload like OP-TEE, and BL33 is the normal-world bootloader — U-Boot, or ABL on Android. The kernel is entered with the DTB physical address in x0 and the MMU off. On Android, init then brings up services from the .rc files. I should be upfront: I know this chain structurally, but I haven't owned a Linux BSP myself — my hands-on boot work is Cortex-R and Cortex-M bare metal."

**Q. What is device tree, and how is it different in Zephyr versus Linux?**

**A.** 같은 문법, 다른 소비 시점이다. Linux는 DTB를 부트로더가 메모리에 놓고 커널이 런타임에 파싱해서 platform device를 만든다. 그래서 커널을 다시 빌드하지 않고 DTB만 바꿔도 된다. Zephyr는 빌드할 때 DT를 매크로로 펼쳐 `devicetree_generated.h`를 만들고 그걸로 디바이스 인스턴스를 컴파일 타임에 확정한다. 그래서 DT를 고치면 반드시 재빌드해야 하고, 대신 안 쓰는 디바이스는 바이너리에 아예 안 들어간다. 목적은 둘 다 같다 — 보드가 바뀔 때 C 코드가 아니라 데이터가 바뀌게 하는 것.

> "Same syntax, different consumption model. In Linux the bootloader hands the kernel a DTB at runtime, and the kernel populates platform devices from it — you can swap the DTB without rebuilding the kernel. Zephyr expands devicetree into macros at build time into devicetree_generated.h, so device instances are fixed at compile time: change the devicetree and you must rebuild, but unused devices cost nothing in flash. Both exist for the same reason — when the board changes, data should change, not C code."

**Q. Have you worked with Cortex-A and Linux?**

**A.** 8.3절 스크립트를 그대로 쓴다. 짧게: "아니요, 제 bring-up 경험은 Cortex-R/M bare-metal입니다. Linux BSP는 구조와 경계는 알지만 소유해 본 적은 없습니다." 그리고 즉시 강점으로 넘어간다.

> "No — I haven't owned a Linux BSP. My bring-up experience is Cortex-R and Cortex-M bare metal: bringing cores up on FPGA before silicon, then carrying the same drivers onto production parts. I know the Linux side structurally — the boot chain, devicetree, how probe and the clock, regulator and pinctrl frameworks map to the register writes I used to do by hand — but I'd be learning the operational side, and I'd rather tell you that now than discover it in week two."

**Q. What does a vendor BSP drop actually contain, and what do you do with it?**

**A.** 7절 표 그대로. 요지는 "받으면 먼저 레퍼런스 보드에서 손대지 않고 부팅해 기준선을 만들고, 우리 보드 DTS를 쓰고, 콘솔 → 스토리지 → PMIC → 버스 → 오디오 → 무선 순으로 켠다. 그다음부터의 진짜 일은 우리 변경과 다음 드롭 사이의 차이 관리다."

> "The first thing I'd do is build it untouched and boot the reference board to get a baseline. Then write our board's device tree and bring things up in order — console, storage, PMIC, buses, audio, radios. After that the real job is managing the delta between our tree and the next vendor drop."

**Q. Why does Android have a HAL layer at all?**

**A.** 프레임워크가 드라이버 구현에 의존하지 않게 하려는 것이다. 벤더는 정해진 인터페이스만 구현하고 vendor 파티션에 넣는다. 그래서 시스템 이미지와 벤더 이미지를 따로 업데이트할 수 있다. 예전에는 HIDL로 했는데 Android 13에서 deprecated고 지금은 AIDL을 쓴다. 파티션 경계를 넘는 인터페이스는 stable AIDL이어야 하고 `@VintfStability` + VINTF 매니페스트 선언이 필요하다. 커널 쪽에도 같은 철학의 GKI가 있다 — 일반 커널과 vendor 모듈을 KMI로 분리.

> "So the framework doesn't depend on any particular driver implementation. Vendors implement a fixed interface and ship it on the vendor partition, which lets system and vendor images update independently. HIDL was the original mechanism; it's deprecated as of Android 13 and new HALs use AIDL. Anything crossing the framework/vendor boundary has to be stable AIDL — @VintfStability, declared in the VINTF manifest. GKI applies the same idea in the kernel: a generic kernel plus vendor modules against a stable KMI."

---

## 11. 직접 해보기

하드웨어 없이 전부 가능하다. **실제로 돌려 본 것만 면접에서 언급할 것.**

### 11.1 QEMU로 arm64 Linux 부팅 보기

```sh
# macOS: brew install qemu. 미리 빌드된 arm64 커널 Image와 rootfs가 필요하다
qemu-system-aarch64 -M virt -cpu cortex-a57 -m 1024 -nographic \
  -kernel Image -append "console=ttyAMA0 root=/dev/vda rw"
```

확인할 것: 콘솔 파라미터를 지우면 아무것도 안 보인다는 것(9절 "콘솔이 안 나옴"의 재현), 부트 로그에서 `OF:`와 `platform` 메시지가 나오는 순서.

### 11.2 QEMU가 만든 devicetree 꺼내 보기

```sh
qemu-system-aarch64 -M virt,dumpdtb=virt.dtb -cpu cortex-a57 -nographic
dtc -I dtb -O dts -o virt.dts virt.dtb   # brew install dtc
less virt.dts
```

확인할 것: `/chosen`, `compatible`, `reg`, `interrupts`, `#address-cells`. 2.2절 예시와 실제 DTS가 어떻게 대응하는지 눈으로 맞춰 본다. **이거 한 번 해보면 DT 질문에 대한 체감이 완전히 달라진다.**

### 11.3 동작 중인 리눅스에서 DT 보기 (라즈베리파이 등이 있으면)

```sh
cat /proc/device-tree/model; echo
ls /sys/bus/platform/devices/ | head
cat /sys/kernel/debug/devices_deferred   # root 필요
```

### 11.4 Zephyr 쪽과 나란히 비교

```sh
west build -p -b qemu_cortex_m3 samples/hello_world
less build/zephyr/zephyr.dts                                  # 최종 병합 결과
grep -m5 "DT_N_S_soc" build/zephyr/include/generated/zephyr/devicetree_generated.h
```

확인할 것: Linux는 `.dtb` 바이너리가 산출물인데 Zephyr는 **C 헤더**가 산출물이라는 것. 2.5절 표를 눈으로 확인하는 실습이다.

---

## 12. 요약 & 체크리스트

Cortex-A + Linux + Android의 BSP는 MCU BSP와 **같은 문제를 훨씬 큰 규모로** 푼다. 부트는 여러 단으로 쪼개지고(SRAM 제약 + 서명 검증), 보드 차이는 devicetree라는 데이터로 빠지고, 공유 자원은 참조 카운트를 가진 프레임워크(clk/regulator/pinctrl)로 올라가고, 초기화 순서는 개발자가 정하는 대신 `-EPROBE_DEFER` 재시도로 풀리고, 벤더와의 경계는 HAL·AIDL·GKI/KMI로 계약화된다. Don에게 이 영역은 **소유 대상이 아니라 대화 대상**이다. 목표는 정확한 지도와 정직한 경계선 하나다.

- [ ] BootROM → BL1 → BL2 → BL31 → BL33 → kernel → init을 EL까지 붙여 백지에 그릴 수 있다
- [ ] BL31이 부팅 후에도 상주하며 PSCI를 제공한다는 것을 설명할 수 있다
- [ ] U-Boot의 TPL/VPL/SPL/U-Boot proper 구분을 말할 수 있다
- [ ] arm64 커널 진입 규약(`x0`=DTB, MMU off, EL2 권장)을 말할 수 있다
- [ ] Qualcomm 스테이지 이름은 세대마다 다르다고 **말하고 단정하지 않는다**
- [ ] devicetree의 세 가지 용도(platform identification / runtime configuration / device population)를 댄다
- [ ] Linux DT와 Zephyr DT의 차이를 소비 시점 기준으로 설명한다
- [ ] `compatible` → `of_match_table` → `probe()` 경로와 `-EPROBE_DEFER`의 역할을 말할 수 있다
- [ ] clk의 prepare/enable 분리 이유(sleepable vs atomic)를 말할 수 있다
- [ ] 레귤레이터 disable이 항상 전압을 내리지 않는 이유를 말할 수 있다
- [ ] pinctrl의 default/sleep 상태와 저전력의 관계를 말할 수 있다
- [ ] HIDL이 Android 13에서 deprecated고 새 HAL은 AIDL이라는 것을 안다
- [ ] GKI/KMI가 무엇을 분리하는지 한 문장으로 말한다
- [ ] AOSP 빌드 3줄과 `user`/`userdebug`/`eng` 차이, Yocto의 `meta-<bsp>` 구조를 안다
- [ ] BSP 드롭 내용물 8가지를 나열할 수 있다
- [ ] 8.3절 정직 스크립트를 외워서 막힘없이 말할 수 있다
- [ ] QEMU에서 DTB를 덤프해 DTS로 열어 본 적이 있다

---

## 참고 자료

WebFetch로 내용을 직접 확인한 페이지에는 ✅를 표시했다. 나머지는 구조 파악용이며, **버전에 따라 달라질 수 있는 항목**은 비고에 적었다.

| 자료 | 확인 | 무엇을 담고 있나 | URL |
|---|---|---|---|
| TF-A — Firmware Design | ✅ 2026-09-21 | BL1/BL2/BL31/BL32/BL33 정의, 실행 EL, cold boot 순서, SP_MIN, FIP | https://trustedfirmware-a.readthedocs.io/en/latest/design/firmware-design.html |
| U-Boot — SPL/TPL | ✅ 2026-09-21 | TPL/VPL/SPL/U-Boot proper 단계 정의와 역할 | https://docs.u-boot.org/en/latest/develop/spl.html |
| Linux — Devicetree usage model | ✅ 2026-09-21 | DT의 세 가지 용도, `compatible` 매칭, `of_platform_populate`, platform_device 생성 | https://docs.kernel.org/devicetree/usage-model.html |
| Linux — Platform devices and drivers | ✅ 2026-09-21 | `struct platform_device`, `struct platform_driver`, probe/remove, 바인딩 경로 | https://docs.kernel.org/driver-api/driver-model/platform.html |
| Linux — arm64 booting | ✅ 2026-09-21 | 커널 진입 규약: `x0`=DTB, `x1~x3`=0, MMU off, PSTATE.DAIF, EL2 권장, Image 헤더 | https://docs.kernel.org/arch/arm64/booting.html |
| Linux — Common Clock Framework | ✅ 2026-09-21 | `clk_hw`, `clk_ops`, prepare(mutex/sleepable) vs enable(spinlock/atomic) | https://docs.kernel.org/driver-api/clk.html |
| Linux — Regulator consumer API | ✅ 2026-09-21 | `regulator_get/enable/disable/set_voltage`, 참조 카운트 경고, bulk API | https://docs.kernel.org/power/regulator/consumer.html |
| Linux — Pin control subsystem | ✅ 2026-09-21 | PINMUX/PINCONF, group·function, default/init/sleep/idle 상태, `pinctrl_bind_pins` | https://docs.kernel.org/driver-api/pin-control.html |
| Android — Platform architecture | ✅ 2026-09-21 | 커널/HAL/native/ART/framework/apps 층 정의, HAL의 목적 | https://source.android.com/docs/core/architecture |
| Android — HAL types | ✅ 2026-09-21 | binderized HAL, SP HAL, HIDL deprecated(Android 13), vendor 파티션 요구 | https://source.android.com/docs/core/architecture/hal |
| Android — AIDL for HALs | ✅ 2026-09-21 | AIDL HAL(Android 11~), stable AIDL, `@VintfStability`, `stability: "vintf"`, VINTF 매니페스트 | https://source.android.com/docs/core/architecture/aidl/aidl-hals |
| Android — Generic Kernel Image | ✅ 2026-09-21 | GKI 커널(`boot.img`), vendor module(`vendor_boot.img`/`vendor_dlkm`), KMI, 도입 배경 | https://source.android.com/docs/core/architecture/kernel/generic-kernel-image |
| Android — Bootloader | ✅ 2026-09-21 | `boot.img`/`vendor_boot.img`/`init_boot.img`/`dtbo`, 부트 플로우, A/B 슬롯, bootconfig(Android 12+) | https://source.android.com/docs/core/architecture/bootloader |
| AOSP — Build Android | ✅ 2026-09-21 | `source build/envsetup.sh`, `lunch product-release-variant`, `m`, user/userdebug/eng | https://source.android.com/docs/setup/build/building |
| Yocto — BSP Developer's Guide | ✅ 2026-09-21 | BSP 정의, `meta-<bsp_root_name>`, `conf/layer.conf`, `conf/machine/*.conf`, `MACHINE`, `KMACHINE` | https://docs.yoctoproject.org/bsp-guide/bsp.html |
| Zephyr — DT input/output files | ✅ 2026-09-21 | `.dts`/`.dtsi`/`.overlay`/바인딩 `.yaml`, `zephyr.dts.pre`, `devicetree_generated.h`, `zephyr.dts` | https://docs.zephyrproject.org/latest/build/dts/intro-input-output.html |
| Devicetree Specification | — | DTS 문법·표준 프로퍼티의 1차 규격 | https://www.devicetree.org/specifications/ |
| Linux — IIO / ASoC | — | 센서는 IIO, 임베디드 오디오는 ASoC(codec/platform/machine 분할) | https://docs.kernel.org/driver-api/iio/index.html · https://docs.kernel.org/sound/soc/index.html |
| OP-TEE 문서 | — | BL32 자리에 들어가는 대표적 TEE | https://optee.readthedocs.io/en/latest/ |
| PSCI 규격 (Arm) | — | `CPU_ON`/`CPU_OFF` 등 EL3 전원 제어 인터페이스 | https://developer.arm.com/documentation/den0022/latest/ |

> **버전 의존 표기**: 커널 API 시그니처(예: `platform_driver.remove`의 반환형), VNDK 관련 정책, Android 파티션 구성은 릴리스마다 바뀐다. 면접에서는 "버전에 따라 다릅니다"를 붙이는 게 정확하다.
> **벤더 문서 없음**: Qualcomm의 PBL/XBL/ABL 등 스테이지 명칭과 구성은 **세대·제품군마다 다르고 공개 1차 문서가 없다**. 이 노트는 의도적으로 단정하지 않았다. Ambiq 등 MCU 벤더 SDK도 포털/NDA 배포라 링크를 만들지 않았다.
