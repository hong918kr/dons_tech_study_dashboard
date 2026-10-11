# D4. 자원 기술 — reg · interrupts · clocks · resets · regulators · gpios · pinctrl · dmas

> **이 노트를 다 읽으면**: DT 노드의 자원 속성(`reg`, `interrupts`, `clocks`, `resets`, `*-supply`, `*-gpios`, `pinctrl-*`, `dmas`)을 한 줄씩 읽고 "어느 provider의 몇 번 무엇"인지 풀어 말할 수 있다 · 모든 자원 참조가 "phandle + 인자 셀(`#xxx-cells`)"이라는 한 가지 문법이라는 것을 설명할 수 있다 · 드라이버 probe에서 각 자원을 `devm_` API로 받아 오는 코드를 짜고, `/proc/interrupts`·`clk_summary`·`regulator_summary`·`debug/gpio`·`devices_deferred`로 결과를 검증할 수 있다 · GIC SPI 번호의 "+32" 함정, active-low GPIO, dummy regulator, `-EPROBE_DEFER`를 진단할 수 있다
> **왜 필요한가**: 센서 하나를 새 보드에 붙이는 일은 결국 "IMU 노드 하나에 인터럽트·전원·리셋 핀·핀먹스를 정확히 적는 일"이고, 면접에서는 "`interrupts = <0 80 4>`가 무슨 뜻이죠?", "probe가 안 불리는데 어떻게 디버그하나요?", "EPROBE_DEFER가 뭐죠?"로 나온다
> **Don 기준 난이도**: 인터럽트 번호·트리거 타입·클럭 게이팅·전원 시퀀스·리셋 핀·핀먹스 자체는 펌웨어에서 매일 하던 일이다 / 새로 배울 것은 "그걸 C 헤더가 아니라 DT 데이터로 적고, 커널 프레임워크(irq domain, CCF, regulator, gpiolib, pinctrl, dmaengine)가 대신 해석해 주는 구조"
> **선행 노트**: D1(왜 DT인가), D2(문법: node·property·`reg`·phandle·label), D3(`compatible` → `probe()` 매칭) — D5(파일 구조), D8(드라이버에서 DT 읽기)와 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 컨트롤러 펌웨어에서 새 주변장치를 붙이던 장면을 떠올려 보자. 보통 `board.h` 같은 헤더에 이런 것이 있었다.

```c
#define IMU_I2C_BASE      0x30A40000u   /* I2C3 레지스터 베이스 */
#define IMU_INT_IRQ       (32 + 21)     /* NVIC/GIC 번호 */
#define IMU_RESET_PORT    GPIO3
#define IMU_RESET_PIN     6             /* active-low */
#define IMU_CLK_GATE_BIT  (1u << 12)    /* CCM 게이트 레지스터 비트 */
```

그리고 `imu_init()`가 순서대로 클럭 게이트를 열고, 전원 스위치 GPIO를 켜고, 1 ms 기다리고, 리셋 핀을 풀고, 핀먹스 레지스터에 "이 패드는 GPIO 입력"을 쓰고, 인터럽트 컨트롤러에 트리거 타입을 설정하고, 벡터를 등록했다. **보드가 바뀌면 이 헤더와 init 코드를 고쳐서 다시 빌드**했다.

Linux에서는 이 "보드 배선 정보"가 **Device Tree 노드의 속성**으로 옮겨 간다. 드라이버는 숫자를 하나도 모른다. 대신 이렇게 말한다.

- "내 `reg` 0번 창을 매핑해 줘" → MMIO 주소
- "내 인터럽트 0번을 줘" → Linux IRQ 번호 (트리거 타입까지 설정 끝난 상태)
- "`core`라는 이름의 클럭을 켜 줘" → 어느 클럭 컨트롤러의 몇 번 게이트인지는 DT가 안다
- "`vdd` 전원을 켜 줘" → 어느 레귤레이터인지는 DT가 안다
- "`reset` 핀을 줘" → 어느 GPIO 컨트롤러 몇 번 핀, active-low인지는 DT가 안다

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="265" y="12" width="150" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="33" text-anchor="middle" font-size="12">MMIO 주소 공간</text>
  <text x="340" y="52" text-anchor="middle" font-size="12">0x09030fe0 + 0x20</text>
  <line x1="340" y1="95" x2="340" y2="65" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="350" y="84" font-size="12">reg (주소, phandle 아님)</text>
  <rect x="265" y="95" width="150" height="240" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="2"/>
  <text x="340" y="200" text-anchor="middle" font-size="13" font-weight="bold">d4-res@9030fe0</text>
  <text x="340" y="222" text-anchor="middle" font-size="12">consumer 노드</text>
  <text x="340" y="242" text-anchor="middle" font-size="12">드라이버: d4_res</text>
  <rect x="10" y="100" width="160" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="90" y="121" text-anchor="middle" font-size="13">GIC intc@8000000</text>
  <text x="90" y="140" text-anchor="middle" font-size="12">#interrupt-cells = 3</text>
  <rect x="10" y="180" width="160" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="90" y="201" text-anchor="middle" font-size="13">fixed-clock d4-osc</text>
  <text x="90" y="220" text-anchor="middle" font-size="12">#clock-cells = 0</text>
  <rect x="10" y="260" width="160" height="50" rx="6" fill="#888" fill-opacity="0.08" stroke="#888" stroke-dasharray="5,4"/>
  <text x="90" y="281" text-anchor="middle" font-size="13">reset controller</text>
  <text x="90" y="300" text-anchor="middle" font-size="12">#reset-cells = 1</text>
  <line x1="265" y1="125" x2="173" y2="125" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="182" y="118" font-size="12">interrupts</text>
  <line x1="265" y1="205" x2="173" y2="205" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="195" y="198" font-size="12">clocks</text>
  <line x1="265" y1="285" x2="173" y2="285" stroke="currentColor" stroke-dasharray="5,4" marker-end="url(#ah1)"/>
  <text x="195" y="278" font-size="12">resets</text>
  <rect x="510" y="95" width="160" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="590" y="116" text-anchor="middle" font-size="13">regulator-fixed ×2</text>
  <text x="590" y="135" text-anchor="middle" font-size="12">d4-vdd, d4-vddio</text>
  <rect x="510" y="155" width="160" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="590" y="176" text-anchor="middle" font-size="13">pl061 GPIO</text>
  <text x="590" y="195" text-anchor="middle" font-size="12">#gpio-cells = 2</text>
  <rect x="510" y="215" width="160" height="50" rx="6" fill="#888" fill-opacity="0.08" stroke="#888" stroke-dasharray="5,4"/>
  <text x="590" y="236" text-anchor="middle" font-size="13">pin controller</text>
  <text x="590" y="255" text-anchor="middle" font-size="12">상태별 핀 그룹</text>
  <rect x="510" y="275" width="160" height="50" rx="6" fill="#888" fill-opacity="0.08" stroke="#888" stroke-dasharray="5,4"/>
  <text x="590" y="296" text-anchor="middle" font-size="13">DMA controller</text>
  <text x="590" y="315" text-anchor="middle" font-size="12">#dma-cells = N</text>
  <line x1="415" y1="120" x2="507" y2="120" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="425" y="113" font-size="12">*-supply ×2</text>
  <line x1="415" y1="180" x2="507" y2="180" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="422" y="173" font-size="12">reset-gpios</text>
  <line x1="415" y1="240" x2="507" y2="240" stroke="currentColor" stroke-dasharray="5,4" marker-end="url(#ah1)"/>
  <text x="428" y="233" font-size="12">pinctrl-0</text>
  <line x1="415" y1="300" x2="507" y2="300" stroke="currentColor" stroke-dasharray="5,4" marker-end="url(#ah1)"/>
  <text x="445" y="293" font-size="12">dmas</text>
  <line x1="20" y1="360" x2="60" y2="360" stroke="currentColor"/>
  <text x="70" y="364" font-size="12">실선 = 이 노트의 QEMU 랩에서 실제로 받아 본 자원</text>
  <line x1="20" y1="386" x2="60" y2="386" stroke="currentColor" stroke-dasharray="5,4"/>
  <text x="70" y="390" font-size="12">점선 = QEMU virt 머신에 하드웨어가 없어 설명과 i.MX8MM 보드 예제로 다룬다</text>
</svg>
```
그림 1 — 하나의 consumer 노드가 여러 provider 노드를 가리킨다. `reg`만 주소 값이고, 나머지는 전부 "phandle(어느 provider) + 인자(그 안의 몇 번)"이다.

이 노트의 결론을 미리 말하면 세 줄이다.

1. **`reg`를 뺀 모든 자원 참조는 같은 문법**이다: `속성 = <&provider 인자...>`, 인자 개수는 provider 노드의 `#xxx-cells`가 정한다 (§1).
2. **드라이버 쪽 API도 같은 모양**이다: `devm_xxx_get(dev, "이름")` → 핸들 구조체. 해석·번역은 프레임워크가 한다 (§2~§9, 그림 4).
3. **provider가 아직 없으면 `-EPROBE_DEFER`**, consumer가 속성을 잘못 쓰면 `-EINVAL`/`-ENOENT`, 전원은 없으면 dummy로 넘어간다 — 이 세 반응을 구분할 줄 알면 "센서가 안 잡혀요"의 절반은 풀린다 (§11).

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안의 공용 QEMU 랩에서 실제로 돌린 결과다. 게스트는 QEMU `virt` 머신(cortex-a57, 512 MB) + Linux **6.18.55** + BusyBox initramfs이고, 커널 모듈은 `make -C /var/lab/out/linux-build M=$PWD`로 빌드해 게스트 안에서 `insmod`했다. 타임스탬프·virq 번호는 실행마다 다를 수 있다.

---

## 1. 공통 문법 — phandle + 인자 셀 (`#xxx-cells`)

### 1.1 직관 — "포트 번호가 붙은 포인터"

D2에서 label과 phandle을 배웠다. `&gpio`라고 쓰면 dtc가 그 노드에 `phandle = <N>`을 붙이고, 참조한 자리에 숫자 N을 넣는다. 즉 phandle은 **노드를 가리키는 32비트 포인터**다.

하지만 "GPIO 컨트롤러"만 가리켜서는 부족하다. **몇 번 핀인지, active-low인지**도 알아야 한다. 그래서 phandle 뒤에 인자 셀을 붙인다. 인자가 몇 개인지는 consumer가 아니라 **provider가 선언**한다. GPIO 컨트롤러가 `#gpio-cells = <2>`라고 써 두면 "나를 가리키는 phandle 뒤에는 셀 2개(핀 번호, 플래그)가 따라온다"는 뜻이다.

Don에게 익숙한 말로 하면, `(peripheral_base_ptr, channel, flags)` 튜플을 DT에 적는 것이다. 튜플의 길이는 주변장치 종류마다 다르므로 주변장치가 스스로 알려 준다.

| provider가 선언 | consumer 속성 | 인자 예 | 인자 의미 (provider 바인딩이 정함) |
|---|---|---|---|
| `#interrupt-cells` | `interrupts` (+ `interrupt-parent`) / `interrupts-extended` | GIC: 3셀, GPIO irqchip: 2셀 | 종류·번호·트리거 / 핀·트리거 |
| `#clock-cells` | `clocks` | 0셀(고정 클럭) 또는 1셀(클럭 ID) | 컨트롤러 안의 클럭 번호 |
| `#reset-cells` | `resets` | 보통 1셀 | 리셋 라인 번호 |
| `#gpio-cells` | `<이름>-gpios` | 보통 2셀 | 핀 번호, 플래그(active-low 등) |
| `#dma-cells` | `dmas` | 컨트롤러마다 다름 (i.MX SDMA는 3셀) | 요청 라인·스크립트 종류·우선순위 등 |
| (셀 없음) | `<이름>-supply` | phandle만 | 레귤레이터 노드 자체 |
| (셀 없음) | `pinctrl-N` | phandle 목록 | 핀 설정 그룹 노드들 |

마지막 두 줄이 예외다. 레귤레이터와 pinctrl 그룹은 **노드 하나가 곧 자원 하나**라서 인자가 필요 없다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="300" height="110" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="42" font-size="13" font-weight="bold">consumer: d4-res@9030fe0</text>
  <text x="20" y="90" font-size="13">reset-gpios =</text>
  <rect x="120" y="72" width="72" height="28" rx="3" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="156" y="91" text-anchor="middle" font-size="13">0x8004</text>
  <rect x="200" y="72" width="40" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="220" y="91" text-anchor="middle" font-size="13">5</text>
  <rect x="248" y="72" width="40" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="268" y="91" text-anchor="middle" font-size="13">1</text>
  <text x="156" y="120" text-anchor="middle" font-size="12">phandle</text>
  <text x="244" y="120" text-anchor="middle" font-size="12">인자 2셀</text>
  <rect x="400" y="20" width="270" height="130" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="410" y="42" font-size="13" font-weight="bold">provider: pl061@9030000</text>
  <text x="410" y="68" font-size="13">phandle = &lt;0x8004&gt;;</text>
  <text x="410" y="92" font-size="13">gpio-controller;</text>
  <text x="410" y="116" font-size="13">#gpio-cells = &lt;2&gt;;</text>
  <text x="410" y="140" font-size="12">of_xlate = of_gpio_twocell_xlate</text>
  <line x1="192" y1="78" x2="397" y2="64" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="250" y="60" font-size="12">① 노드 찾기</text>
  <line x1="400" y1="112" x2="292" y2="96" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="315" y="128" font-size="12">② 셀 2개</text>
  <rect x="10" y="165" width="660" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="20" y="190" font-size="13">③ provider의 xlate가 인자를 해석 → 5번 핀, 플래그 1 = GPIO_ACTIVE_LOW → struct gpio_desc</text>
  <text x="20" y="235" font-size="13" font-weight="bold">같은 패턴</text>
  <text x="20" y="260" font-size="12">clocks = &lt;&amp;clk 166&gt;</text>
  <text x="250" y="260" font-size="12">#clock-cells = &lt;1&gt;</text>
  <text x="460" y="260" font-size="12">→ struct clk</text>
  <text x="20" y="284" font-size="12">resets = &lt;&amp;src 3&gt;</text>
  <text x="250" y="284" font-size="12">#reset-cells = &lt;1&gt;</text>
  <text x="460" y="284" font-size="12">→ struct reset_control</text>
  <text x="20" y="308" font-size="12">dmas = &lt;&amp;sdma1 22 4 0&gt;</text>
  <text x="250" y="308" font-size="12">#dma-cells = &lt;3&gt;</text>
  <text x="460" y="308" font-size="12">→ struct dma_chan</text>
  <text x="20" y="332" font-size="12">interrupts = &lt;0 80 1&gt;</text>
  <text x="250" y="332" font-size="12">#interrupt-cells = &lt;3&gt;</text>
  <text x="460" y="332" font-size="12">→ int virq (phandle은 상속)</text>
</svg>
```
그림 2 — phandle + 인자 셀은 모든 자원 참조의 공통 문법이다. consumer는 "어느 노드의 몇 번"만 적고, 인자를 어떻게 읽을지는 provider의 `#xxx-cells`와 드라이버의 xlate 함수가 정한다.

### 1.2 커널의 범용 파서 — `of_parse_phandle_with_args()`

이 문법은 커널 안에서 **함수 하나**로 파싱된다.

```c
int of_parse_phandle_with_args(const struct device_node *np,
                               const char *list_name,   /* "reset-gpios" */
                               const char *cells_name,  /* "#gpio-cells" */
                               int index, struct of_phandle_args *out_args);
/* out_args->np = provider 노드, ->args_count, ->args[] = 인자 셀 */
```

clk, reset, gpiolib, dmaengine, 그리고 `interrupts-extended`가 전부 내부에서 이 함수(또는 같은 계열의 `of_parse_phandle_with_args_map` 등)를 부른다. 프레임워크마다 다른 것은 **찾은 provider 노드를 자기 provider 목록에서 찾아 xlate를 부르는 부분**뿐이다. 이 노트의 실습 드라이버는 이 함수를 직접 불러서 파싱 결과를 찍어 본다 (§2의 `d4_show_args`).

### 1.3 흔한 함정

- **셀 개수 불일치**: provider가 `#gpio-cells = <2>`인데 consumer가 `<&gpio3 6>`처럼 1셀만 쓰면, 파서는 다음 phandle 자리를 인자로 먹어 버리거나 길이 부족 오류를 낸다. dtc가 경고(`gpios_property`)를 내는 경우가 많으니 **dtc 경고를 무시하지 말 것**.
- **phandle은 숫자다**: 디컴파일한 `.dts`(예: QEMU가 덤프한 `virt.dts`)에는 label이 없고 `<0x8004 ...>` 같은 숫자만 있다. 그 파일을 다시 컴파일하면 dtc가 "cell 0 is not a phandle reference" 경고를 낸다 (§2.3에서 실제로 본다). 동작에는 문제없지만, 손으로 고칠 때는 label을 붙여 쓰는 게 안전하다.

---

## 2. 실습 준비 — QEMU virt에 `d4-res` 노드 붙이기

이 노트의 나머지 절은 **하나의 실습 결과**를 자원별로 뜯어 본다. 먼저 판을 깐다.

### 2.1 무엇을 붙이나

QEMU `virt` 머신의 DT(`/var/lab/out/virt.dts`)에는 이미 GIC(`intc@8000000`, phandle 0x8002), PL061 GPIO(`pl061@9030000`, phandle 0x8004, 8핀), 24 MHz 고정 클럭(`apb-pclk`, phandle 0x8000)이 있다. 여기에 다음을 더한다.

| 추가 노드 | 역할 | 비고 |
|---|---|---|
| `d4_osc: d4-osc` | `fixed-clock` 12 MHz | 클럭 provider (`#clock-cells = <0>`) |
| `d4_vdd: d4-vdd` | `regulator-fixed` 3.3 V | 레귤레이터 provider |
| `d4_vddio: d4-vddio` | `regulator-fixed` 1.8 V | 레귤레이터 provider |
| `d4-res@9030fe0` | consumer, `compatible = "dons,d4-res"` | 우리 드라이버 `d4_res`가 붙는다 |

`reg`로 무엇을 줄지가 고민이다. virt 머신에서 아무 주소나 쓰면, 매핑되지 않은 곳을 읽다가 external abort가 날 수 있다. 그래서 **PL061 GPIO 레지스터 창의 끝 32바이트(0x9030fe0~0x9030fff)**를 빌린다. 여기는 PrimeCell 표준 **ID 레지스터**(PeriphID0~3, PCellID0~3)라서 읽기만 하면 아무 부작용이 없다. 대신 PL061 드라이버가 이미 그 영역을 점유하고 있으므로 "남이 쓰는 영역을 요청하면 어떻게 되는가"도 덤으로 보게 된다 (§3).

인터럽트는 virt에서 쓰지 않는 **SPI 80**을 고른다. virt의 virtio-mmio가 SPI 16~47, UART가 SPI 1, RTC가 SPI 2, GPIO가 SPI 7을 쓴다 (`virt.dts`에서 직접 확인할 수 있다). 하드웨어가 이 선을 올릴 일은 없으므로, 드라이버가 GIC에 **pending 비트를 직접 세워** 인터럽트를 만든다 (§4).

> **주의 (H6에서 발견)**: QEMU `virt`의 GICv2m(MSI 프레임)은 **SPI 80–143을 MSI용으로 예약**한다. 이 노트의 SPI 80은 그 범위 안이라, PCI 장치가 MSI를 많이 쓰는 구성에서는 MSI 벡터와 겹칠 수 있다. 이 노트의 실습은 MSI를 거의 쓰지 않아 충돌이 없었을 뿐이다. 직접 따라 할 때는 **SPI 120처럼 v2m 범위 밖** 또는 `/sys/kernel/debug/irq/domains`에서 v2m 범위를 확인한 뒤 번호를 고르자 (H6 노트 참고).


리셋 핀은 PL061의 **5번 핀**(3번은 `gpio-keys`의 poweroff 키가 쓴다)을 active-low로 쓴다.

### 2.2 예제 1 — DT 조각과 빌드 스크립트

무엇을 확인하나: 디컴파일된 `virt.dts` 사본에 label 두 개(`gic`, `gpio`)를 붙이고, 우리 노드를 `#include`해서 C 전처리 → dtc로 `.dtb`를 만든다. 실패 실험용 변형은 `-D` 매크로로 켠다.

```dts
/* d4_nodes.dtsi — virt.dts 사본 끝에 #include 되는 D4 데모 노드 */
#include <dt-bindings/interrupt-controller/arm-gic.h>
#include <dt-bindings/gpio/gpio.h>

/ {
	d4_osc: d4-osc {
		compatible = "fixed-clock";
		#clock-cells = <0>;
		clock-frequency = <12000000>;
		clock-output-names = "d4_osc12m";
	};

	d4_vdd: d4-vdd {
		compatible = "regulator-fixed";
		regulator-name = "d4_vdd_3v3";
		regulator-min-microvolt = <3300000>;
		regulator-max-microvolt = <3300000>;
	};

	d4_vddio: d4-vddio {
		compatible = "regulator-fixed";
		regulator-name = "d4_vddio_1v8";
		regulator-min-microvolt = <1800000>;
		regulator-max-microvolt = <1800000>;
#ifdef D4_DEFER
		status = "disabled";
#endif
	};

	d4-res@9030fe0 {
		compatible = "dons,d4-res";
		reg = <0x0 0x09030fe0 0x0 0x20>;	/* pl061 ID 레지스터 창 (읽기만) */
		interrupts = <GIC_SPI 80 IRQ_TYPE_EDGE_RISING>;
		clocks = <&d4_osc>;
		clock-names = "core";
#ifndef D4_NO_VDD
		vdd-supply = <&d4_vdd>;
#endif
		vddio-supply = <&d4_vddio>;
#ifdef D4_BAD_GPIO
		reset-gpios = <&d4_osc 5 GPIO_ACTIVE_LOW>;
#else
		reset-gpios = <&gpio 5 GPIO_ACTIVE_LOW>;
#endif
	};
};
```

```sh
#!/bin/sh
# mkdtb.sh <이름> [-DXXX]  → /var/tmp/D4/<이름>.dtb
set -e
W=/var/tmp/D4; K=/var/lab/src/linux-6.18.55/include; N=$1; shift
mkdir -p $W
sed -e 's/^\tpl061@9030000 {/\tgpio: pl061@9030000 {/' \
    -e 's/^\tintc@8000000 {/\tgic: intc@8000000 {/' /var/lab/out/virt.dts > $W/d4_virt.dts
echo '#include "/Users/donh/.elinux_scratch/D4/d4_nodes.dtsi"' >> $W/d4_virt.dts
cpp -nostdinc -I $K -undef -x assembler-with-cpp "$@" $W/d4_virt.dts > $W/$N.pp.dts
dtc -I dts -O dtb -o $W/$N.dtb $W/$N.pp.dts
```

요점 세 가지.

- `virt.dts`는 QEMU가 런타임에 만든 DT를 디컴파일한 것이라 label이 없다. `sed`로 기존 노드에 label만 붙이면, dtc는 그 노드에 이미 있는 `phandle = <0x8004>`를 그대로 쓴다.
- 루트 노드에 `interrupt-parent = <0x8002>`(GIC)가 있으므로, 우리 노드는 `interrupt-parent`를 따로 안 써도 GIC를 부모로 상속한다 (§4.1).
- `GIC_SPI`, `IRQ_TYPE_EDGE_RISING`, `GPIO_ACTIVE_LOW`는 커널 소스의 `include/dt-bindings/` 헤더에 있는 **매크로일 뿐**이다. 그래서 `cpp`를 먼저 돌린다. 커널 빌드(`make dtbs`)도 내부에서 똑같이 한다.

### 2.3 예제 2 — dtb 만들기와 dtc 경고 읽기

```sh
sh /Users/donh/.elinux_scratch/D4/mkdtb.sh d4_ok
```

```text
/var/tmp/D4/d4_virt.dts:276.3-21: Warning (clocks_property): /pl061@9030000:clocks: cell 0 is not a phandle reference
/var/tmp/D4/d4_virt.dts:302.3-21: Warning (clocks_property): /pl031@9010000:clocks: cell 0 is not a phandle reference
/var/tmp/D4/d4_virt.dts:310.3-28: Warning (clocks_property): /pl011@9000000:clocks: cell 0 is not a phandle reference
/var/tmp/D4/d4_virt.dts:310.3-28: Warning (clocks_property): /pl011@9000000:clocks: cell 1 is not a phandle reference
/var/tmp/D4/d4_virt.dts:267.4-31: Warning (gpios_property): /gpio-keys/poweroff:gpios: cell 0 is not a phandle reference
```

출력에서 볼 것: 경고는 전부 **원래 `virt.dts`의 노드**에서 나왔다. `clocks = <0x8000>`처럼 phandle 자리에 숫자가 직접 있어서 dtc가 "참조인지 확인할 수 없다"고 하는 것이다. dtc는 `clocks`, `*-gpios` 같은 **알려진 자원 속성의 셀 구조를 검사한다**는 증거이기도 하다. 우리 노드(`&d4_osc`, `&gpio`)는 label 참조라 경고가 없다.

### 2.4 예제 3 — 드라이버 `d4_res` (두 조각을 합쳐 한 파일)

무엇을 확인하나: 노드의 자원을 **종류별로 하나씩** 받아서 받은 값을 찍는다. 앞부분은 MMIO와 IRQ, 뒷부분은 probe 본체다.

```c
// SPDX-License-Identifier: GPL-2.0
/* d4_res: DT 노드에 적힌 자원을 하나씩 받아 출력하는 데모 platform 드라이버 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/reset.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>

struct d4_priv {
	void __iomem *base;
	int irq;
	unsigned int hits;
};

static irqreturn_t d4_isr(int irq, void *data)
{
	struct d4_priv *p = data;

	p->hits++;
	return IRQ_HANDLED;
}

static int d4_get_mmio(struct platform_device *pdev, struct d4_priv *p)
{
	struct device *dev = &pdev->dev;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res)
		return dev_err_probe(dev, -EINVAL, "no reg\n");
	dev_info(dev, "reg[0]      : %pR\n", res);
	p->base = devm_ioremap_resource(dev, res);	/* request + ioremap */
	if (IS_ERR(p->base)) {
		dev_warn(dev, "devm_ioremap_resource: %pe -> devm_ioremap (no request)\n",
			 p->base);
		p->base = devm_ioremap(dev, res->start, resource_size(res));
		if (!p->base)
			return -ENOMEM;
	}
	dev_info(dev, "PeriphID    : %02x %02x %02x %02x  PCellID: %02x %02x %02x %02x\n",
		 readl(p->base + 0x00), readl(p->base + 0x04),
		 readl(p->base + 0x08), readl(p->base + 0x0c),
		 readl(p->base + 0x10), readl(p->base + 0x14),
		 readl(p->base + 0x18), readl(p->base + 0x1c));
	return 0;
}
```

```c
static int d4_get_irq(struct platform_device *pdev, struct d4_priv *p)
{
	struct device *dev = &pdev->dev;
	int i, ret;

	p->irq = platform_get_irq(pdev, 0);		/* virq (Linux 번호) */
	if (p->irq < 0)
		return p->irq;
	ret = devm_request_irq(dev, p->irq, d4_isr, 0, "d4_res", p);
	if (ret)
		return dev_err_probe(dev, ret, "request_irq\n");
	dev_info(dev, "irq         : virq=%d hwirq=%lu trigger=0x%x\n", p->irq,
		 irqd_to_hwirq(irq_get_irq_data(p->irq)), irq_get_trigger_type(p->irq));
	for (i = 0; i < 3; i++) {			/* GIC에 pending을 직접 세워 IRQ 발생 */
		irq_set_irqchip_state(p->irq, IRQCHIP_STATE_PENDING, true);
		udelay(10);
	}
	dev_info(dev, "irq hits    : %u\n", p->hits);
	return 0;
}
// SPDX-License-Identifier: GPL-2.0
/* (뒷부분) probe 본체 — clocks, supplies, gpios, resets, dmas */
static bool rst_byidx;
module_param(rst_byidx, bool, 0444);

static void d4_show_args(struct device *dev, const char *prop, const char *cells)
{
	struct of_phandle_args a;
	char buf[64] = "";
	int i, n = 0;

	if (of_parse_phandle_with_args(dev->of_node, prop, cells, 0, &a)) {
		dev_info(dev, "%-12s: (none)\n", prop);
		return;
	}
	for (i = 0; i < a.args_count; i++)
		n += scnprintf(buf + n, sizeof(buf) - n, " %u", a.args[i]);
	dev_info(dev, "%-12s: -> %pOF  args(%d):%s\n", prop, a.np, a.args_count, buf);
	of_node_put(a.np);
}
```

```c
static int d4_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct d4_priv *p;
	struct clk *clk;
	struct gpio_desc *rst;
	struct reset_control *rc;
	struct dma_chan *ch;
	int ret, uv;

	dev_info(dev, "probe start (%pOF)\n", dev->of_node);
	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	d4_show_args(dev, "clocks", "#clock-cells");
	d4_show_args(dev, "reset-gpios", "#gpio-cells");

	ret = d4_get_mmio(pdev, p);
	if (ret)
		return ret;
	ret = d4_get_irq(pdev, p);
	if (ret)
		return ret;

	clk = devm_clk_get_enabled(dev, "core");		/* clocks + clock-names */
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "clk core\n");
	dev_info(dev, "clk core    : %lu Hz\n", clk_get_rate(clk));

	ret = devm_regulator_get_enable(dev, "vdd");		/* vdd-supply */
	if (ret)
		return dev_err_probe(dev, ret, "vdd\n");
	uv = devm_regulator_get_enable_read_voltage(dev, "vddio");
	if (uv < 0)
		return dev_err_probe(dev, uv, "vddio\n");
	dev_info(dev, "vddio       : %d uV\n", uv);

	rst = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);	/* reset-gpios, assert */
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "reset gpio\n");
	dev_info(dev, "reset gpio  : active_low=%d logical=%d raw=%d (asserted)\n",
		 gpiod_is_active_low(rst), gpiod_get_value(rst), gpiod_get_raw_value(rst));
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(rst, 0);			/* deassert */
	dev_info(dev, "reset gpio  : logical=%d raw=%d (released)\n",
		 gpiod_get_value(rst), gpiod_get_raw_value(rst));
```

```c
	/* resets: 이름 "core"로 찾는다 (rst_byidx=1이면 index 0 → reset-gpios 폴백) */
	rc = devm_reset_control_get_optional_exclusive(dev, rst_byidx ? NULL : "core");
	if (IS_ERR(rc))
		return dev_err_probe(dev, PTR_ERR(rc), "resets\n");
	dev_info(dev, "resets      : %s\n", rc ? "got" : "none (optional -> NULL)");

	ch = dma_request_chan(dev, "rx");			/* dmas + dma-names */
	dev_info(dev, "dma rx      : %pe\n", ch);
	if (!IS_ERR(ch))
		dma_release_channel(ch);

	platform_set_drvdata(pdev, p);
	dev_info(dev, "probe done\n");
	return 0;
}

static const struct of_device_id d4_of_match[] = {
	{ .compatible = "dons,d4-res" },
	{ }
};
MODULE_DEVICE_TABLE(of, d4_of_match);

static struct platform_driver d4_driver = {
	.probe = d4_probe,
	.driver = { .name = "d4_res", .of_match_table = d4_of_match },
};
module_platform_driver(d4_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("D4 DT resource demo");
```

코드 읽는 법:

- **모든 자원이 `devm_`**이다. probe가 중간에 실패하거나 `-EPROBE_DEFER`로 돌아가면, 그때까지 받은 IRQ·클럭 enable·레귤레이터 enable·GPIO가 **역순으로 자동 해제**된다. `remove()`를 아예 쓰지 않은 이유다 (G 모듈에서 자세히).
- 실패 경로는 전부 `dev_err_probe()`다. 이 함수는 `-EPROBE_DEFER`면 로그를 찍지 않고 **지연 사유를 `/sys/kernel/debug/devices_deferred`에 기록**한다. 나머지 에러는 `error -EINVAL: ...` 형식으로 찍는다. §7·§8에서 둘 다 보고 §11에서 정리한다.
- `rst_byidx`는 §6의 함정을 재현하는 스위치다. 기본값(0)에서는 리셋을 이름 `"core"`로 찾는다.

### 2.5 예제 4 — 모듈 빌드와 initramfs·부팅 스크립트

```sh
S=/Users/donh/.elinux_scratch/D4
mkdir -p /var/tmp/D4/mod && cd /var/tmp/D4/mod
cat $S/d4_res.c $S/d4_res_probe.c > d4_res.c
echo "obj-m := d4_res.o" > Kbuild
make -C /var/lab/out/linux-build M=$PWD modules
```

```text
make: Entering directory '/var/lab/build/linux'
make[1]: Entering directory '/var/tmp/D4/mod'
  CC [M]  d4_res.o
  MODPOST Module.symvers
  LD [M]  d4_res.ko
make[1]: Leaving directory '/var/tmp/D4/mod'
make: Leaving directory '/var/lab/build/linux'
```

경고 0개다. 게스트 쪽은 랩 rootfs를 복사해 `.ko`와 관찰 스크립트 `d4.sh`를 넣고 `-dtb`로 우리 DT를 준다.

```sh
#!/bin/sh
# boot.sh <dtb이름> [LAB_CMD 앞에 붙일 것]
set -e
W=/var/tmp/D4
if [ ! -f $W/i.gz ] || [ $W/mod/d4_res.ko -nt $W/i.gz ] || [ /Users/donh/.elinux_scratch/D4/d4.sh -nt $W/i.gz ]; then
  rm -rf $W/r && mkdir $W/r && cp -a /var/lab/build/rootfs/. $W/r/
  cp $W/mod/d4_res.ko $W/r/ && cp /Users/donh/.elinux_scratch/D4/d4.sh $W/r/d4.sh
  (cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
fi
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd $W/i.gz -dtb $W/$1.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=${2}sh~/d4.sh"
```

```sh
#!/bin/sh
# d4.sh — 게스트 안에서 실행: 모듈 로드 → 자원 관찰
insmod /d4_res.ko $D4_ARGS
sleep 1
echo "=== dmesg (d4) ==="; dmesg | grep -i "d4"
echo "=== /proc/interrupts ==="; grep -E "CPU0|d4_res|pl061|uart" /proc/interrupts
N=$(grep d4_res /proc/interrupts | cut -d: -f1 | tr -d ' ')
echo "=== /sys/kernel/irq/$N ==="; for f in hwirq type chip_name name actions; do printf "%-10s %s\n" $f "$(cat /sys/kernel/irq/$N/$f)"; done
echo "=== clk_summary ==="; grep -E "enable|----|d4_osc|clk24" /sys/kernel/debug/clk/clk_summary
echo "=== regulator_summary ==="; cat /sys/kernel/debug/regulator/regulator_summary
echo "=== debug/gpio ==="; cat /sys/kernel/debug/gpio
echo "=== iomem ==="; grep -E "^0903|^  0903|^    0903" /proc/iomem
echo "=== devices_deferred ==="; cat /sys/kernel/debug/devices_deferred
echo "=== DT raw ==="; D=/proc/device-tree/d4-res@9030fe0; for p in reg interrupts clocks vdd-supply reset-gpios; do printf "%-12s" $p; hexdump -v -e '4/1 "%02x" " "' $D/$p 2>/dev/null; echo; done
echo "=== phandles ==="; for n in intc@8000000 pl061@9030000 d4-osc d4-vdd d4-vddio; do printf "%-15s" $n; hexdump -v -e '4/1 "%02x"' /proc/device-tree/$n/phandle; echo; done
```

랩 initramfs의 `/init`은 debugfs를 `/sys/kernel/debug`에 마운트해 두므로 `clk_summary` 같은 파일을 바로 읽을 수 있다.

### 2.6 예제 5 — 부팅: probe 로그 전체

```sh
sh /Users/donh/.elinux_scratch/D4/boot.sh d4_ok
```

```text
lab initramfs: 6.18.55 up 2.53s
[    3.370802] d4_res 9030fe0.d4-res: error -EBUSY: can't request region for resource [mem 0x09030fe0-0x09030fff]
=== dmesg (d4) ===
[    3.339366] d4_res: loading out-of-tree module taints kernel.
[    3.369246] d4_res 9030fe0.d4-res: probe start (/d4-res@9030fe0)
[    3.369437] d4_res 9030fe0.d4-res: clocks      : -> /d4-osc  args(0):
[    3.370394] d4_res 9030fe0.d4-res: reset-gpios : -> /pl061@9030000  args(2): 5 1
[    3.370513] d4_res 9030fe0.d4-res: reg[0]      : [mem 0x09030fe0-0x09030fff]
[    3.370802] d4_res 9030fe0.d4-res: error -EBUSY: can't request region for resource [mem 0x09030fe0-0x09030fff]
[    3.377587] d4_res 9030fe0.d4-res: devm_ioremap_resource: -EBUSY -> devm_ioremap (no request)
[    3.377858] d4_res 9030fe0.d4-res: PeriphID    : 61 10 04 00  PCellID: 0d f0 05 b1
[    3.378983] d4_res 9030fe0.d4-res: irq         : virq=22 hwirq=112 trigger=0x1
[    3.381852] d4_res 9030fe0.d4-res: irq hits    : 3
[    3.387821] d4_res 9030fe0.d4-res: clk core    : 12000000 Hz
[    3.391769] d4_res 9030fe0.d4-res: vddio       : 1800000 uV
[    3.403083] d4_res 9030fe0.d4-res: reset gpio  : active_low=1 logical=1 raw=0 (asserted)
[    3.407837] d4_res 9030fe0.d4-res: reset gpio  : logical=0 raw=1 (released)
[    3.408127] d4_res 9030fe0.d4-res: resets      : none (optional -> NULL)
[    3.408171] d4_res 9030fe0.d4-res: dma rx      : -ENODEV
[    3.408196] d4_res 9030fe0.d4-res: probe done
```

출력에서 볼 것: 첫 `error -EBUSY` 줄이 `=== dmesg` 앞에도 한 번 더 찍힌 것은, 에러 레벨 메시지라서 `quiet`(콘솔 loglevel 4)에서도 콘솔로 바로 나왔기 때문이다. `dev_info`는 콘솔엔 안 나오고 `dmesg`에만 남는다. `insmod`가 끝나기 전에 probe가 **동기적으로** 다 돌았다는 것도 볼 수 있다 — 노드가 이미 있으니 드라이버가 등록되는 순간 매칭되어 probe가 불린다 (D3).

이 로그 한 덩어리가 이 노트의 지도다. 줄마다 해당 절에서 해석한다.

| 로그 줄 | 무슨 자원 | 어느 절 |
|---|---|---|
| `clocks : -> /d4-osc args(0)` / `reset-gpios : -> /pl061@9030000 args(2)` | phandle + 인자 파싱 결과 | §1 |
| `reg[0]`, `-EBUSY`, `PeriphID 61 10 04 00` | `reg` → MMIO | §3 |
| `virq=22 hwirq=112 trigger=0x1`, `irq hits : 3` | `interrupts` → IRQ | §4 |
| `clk core : 12000000 Hz` | `clocks` | §5 |
| `resets : none` | `resets` | §6 |
| `vddio : 1800000 uV` | `*-supply` | §7 |
| `active_low=1 logical=1 raw=0` | `reset-gpios` | §8 |
| `dma rx : -ENODEV` | `dmas` | §10 |

---

## 3. `reg` — MMIO 창: `platform_get_resource` · `devm_platform_ioremap_resource`

### 3.1 직관과 정의

`reg`는 자원 속성 중 유일하게 **phandle이 아니라 주소 값**이다. `<주소 크기>` 쌍의 목록이고, 각 숫자가 몇 셀인지는 **부모 노드의** `#address-cells`/`#size-cells`가 정한다 (D2). virt 루트는 둘 다 2라서 `reg = <0x0 0x09030fe0 0x0 0x20>`는 "64비트 주소 0x09030fe0부터 0x20바이트"다.

커널이 DT로 platform device를 만들 때(`of_platform_populate()` → `of_device_alloc()`) `reg`의 각 쌍을 부모들의 `ranges`로 CPU 물리 주소까지 번역해서 `struct resource`(`IORESOURCE_MEM`) 배열로 바꿔 둔다. 드라이버는 그 배열에서 꺼내 쓴다.

| API | 하는 일 | 언제 |
|---|---|---|
| `platform_get_resource(pdev, IORESOURCE_MEM, i)` | i번째 `reg` 쌍의 `struct resource` 반환 | 주소·크기만 필요할 때 |
| `platform_get_resource_byname(pdev, IORESOURCE_MEM, "ctrl")` | `reg-names`로 찾기 | 창이 여러 개일 때 |
| `devm_ioremap_resource(dev, res)` | **request_mem_region + ioremap** (자동 해제) | 일반적인 경우 |
| `devm_platform_ioremap_resource(pdev, i)` | 위 두 단계를 한 번에 | 가장 흔한 한 줄 |
| `devm_platform_get_and_ioremap_resource(pdev, i, &res)` | 매핑 + `res`도 돌려줌 | 물리 주소도 필요할 때 (DMA 등) |
| `devm_ioremap(dev, start, size)` | ioremap만 (점유 표시 안 함) | 공유 레지스터 등 특별한 경우 |

펌웨어 비유: `#define I2C3_BASE 0x30A40000u` + `volatile uint32_t *` 캐스팅이 `devm_platform_ioremap_resource()` 한 줄이 된다. 차이는 MMU다. 물리 주소를 그대로 쓸 수 없고, 커널 가상 주소로 매핑(`ioremap`, Device 메모리 속성)해야 한다 (F 모듈).

### 3.2 실습 결과 읽기 — `-EBUSY`와 ID 레지스터

로그의 해당 줄:

```text
[    3.370513] d4_res 9030fe0.d4-res: reg[0]      : [mem 0x09030fe0-0x09030fff]
[    3.370802] d4_res 9030fe0.d4-res: error -EBUSY: can't request region for resource [mem 0x09030fe0-0x09030fff]
[    3.377587] d4_res 9030fe0.d4-res: devm_ioremap_resource: -EBUSY -> devm_ioremap (no request)
[    3.377858] d4_res 9030fe0.d4-res: PeriphID    : 61 10 04 00  PCellID: 0d f0 05 b1
```

- `%pR`이 `struct resource`를 `[mem 시작-끝]`으로 찍는다. 끝 주소는 **포함(inclusive)**이라 0x...fff다.
- `devm_ioremap_resource()`가 `-EBUSY`로 실패했다. PL061 드라이버가 이미 0x9030000~0x9030fff 전체를 `request`했기 때문이다. **request_mem_region은 "이 주소는 내 것" 깃발**이다. 같은 레지스터를 두 드라이버가 몰래 건드리는 사고를 막는다.
- 폴백으로 `devm_ioremap()`(깃발 없이 매핑만)을 써서 읽었다. 읽은 값 `61 10 04 00`은 PrimeCell PeriphID로, 부품 번호 0x061(= PL**061**)과 설계사 코드 0x41(ARM)이 들어 있다. `0d f0 05 b1`은 모든 PrimeCell에 공통인 PCellID다. **MMIO 매핑이 진짜 PL061 레지스터를 가리킨다는 증거**다.

> 실무 주의: 이 폴백은 "읽기 전용 ID 레지스터"라서 안전한 데모일 뿐이다. 진짜로 레지스터 블록을 두 드라이버가 나눠 써야 하면 `syscon` 노드 + regmap(공유 레지스터를 잠금과 함께 제공) 또는 MFD 구조를 쓴다 (H 모듈).

### 3.3 예제 6 — `/proc/iomem`에는 무엇이 보이나

```text
=== iomem ===
09030000-09030fff : pl061@9030000
  09030000-09030fff : 9030000.pl061 pl061@9030000
```

출력에서 볼 것: 우리 `0x09030fe0` 창은 **어디에도 없다**. 두 가지 이유가 겹친다.

1. 바깥 줄 `pl061@9030000`은 AMBA 버스가 PL061 장치를 등록하면서 넣은 항목이고, 안쪽 줄 `9030000.pl061 pl061@9030000`은 PL061 드라이버가 `devm_ioremap_resource()`로 request한 항목이다(이름 = 장치 이름 + 자원 이름).
2. DT로 만든 platform device는 `of_device_add()` → `device_add()`로 등록되어 `/proc/iomem`에 자원을 미리 넣지 않는다. 드라이버가 request해야 비로소 보인다. 우리는 request 없이 `devm_ioremap()`만 했으니 흔적이 없다.

그래서 **`/proc/iomem`은 "드라이버가 실제로 점유한 MMIO"를 보는 창**이다. 보드 bring-up에서 "이 드라이버가 레지스터를 잡았나?"를 볼 때 쓴다.

---

## 4. `interrupts` — GIC 3셀 지정자, `interrupt-parent`, `interrupts-extended`

### 4.1 직관 — "어느 인터럽트 컨트롤러의, 몇 번 입력, 어떤 트리거"

펌웨어에서 인터럽트를 연결할 때 하던 일은 세 가지였다: 컨트롤러 선택(NVIC냐 외부 GPIO 인터럽트냐), 번호, 트리거 타입(엣지/레벨, 극성). DT의 `interrupts`가 정확히 이 셋을 적는다.

- **어느 컨트롤러**: `interrupt-parent` 속성(phandle). 노드에 없으면 **부모, 조부모 … 로 올라가며 찾는다** (`of_irq_find_parent()`). virt는 루트에 `interrupt-parent = <0x8002>`(GIC)가 있어서 모든 노드가 기본으로 GIC를 쓴다.
- **몇 번 + 트리거**: `interrupts` 셀. 셀 개수는 컨트롤러의 `#interrupt-cells`가 정한다. GIC는 3, 대부분의 GPIO irqchip은 2다.

### 4.2 GIC 3셀 해독 — 그리고 "+32" 함정

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="44" font-size="13">interrupts = &lt;</text>
  <rect x="118" y="24" width="110" height="30" rx="3" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="173" y="44" text-anchor="middle" font-size="13">GIC_SPI (0)</text>
  <rect x="236" y="24" width="80" height="30" rx="3" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="276" y="44" text-anchor="middle" font-size="13">80</text>
  <rect x="324" y="24" width="220" height="30" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="434" y="44" text-anchor="middle" font-size="13">IRQ_TYPE_EDGE_RISING (1)</text>
  <text x="550" y="44" font-size="13">&gt;;</text>
  <line x1="173" y1="56" x2="115" y2="90" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="276" y1="56" x2="335" y2="90" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="434" y1="56" x2="560" y2="90" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="92" width="210" height="96" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="114" font-size="13" font-weight="bold">셀 1 — 종류</text>
  <text x="20" y="138" font-size="12">0 = SPI (공유 주변장치)</text>
  <text x="20" y="160" font-size="12">1 = PPI (코어 전용)</text>
  <text x="20" y="180" font-size="12">SGI는 DT에 안 적는다</text>
  <rect x="230" y="92" width="210" height="96" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="240" y="114" font-size="13" font-weight="bold">셀 2 — 상대 번호</text>
  <text x="240" y="138" font-size="12">SPI: INTID = n + 32</text>
  <text x="240" y="160" font-size="12">PPI: INTID = n + 16</text>
  <text x="240" y="180" font-size="12">80 → INTID 112</text>
  <rect x="450" y="92" width="220" height="96" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="460" y="114" font-size="13" font-weight="bold">셀 3 — 플래그</text>
  <text x="460" y="138" font-size="12">[3:0] 1=엣지↑ 4=레벨H</text>
  <text x="460" y="160" font-size="12">SPI는 이 둘만 허용</text>
  <text x="460" y="180" font-size="12">[15:8] PPI CPU 마스크</text>
  <text x="20" y="225" font-size="13" font-weight="bold">GIC INTID (= hwirq) 공간</text>
  <rect x="20" y="236" width="110" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="75" y="258" text-anchor="middle" font-size="12">SGI 0–15</text>
  <rect x="130" y="236" width="150" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="205" y="258" text-anchor="middle" font-size="12">PPI 16–31</text>
  <rect x="280" y="236" width="380" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="470" y="258" text-anchor="middle" font-size="12">SPI 32–1019</text>
  <line x1="233" y1="300" x2="233" y2="273" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="140" y="315" font-size="12">27 arch_timer (PPI 11)</text>
  <line x1="330" y1="300" x2="330" y2="273" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="300" y="315" font-size="12">112 d4_res (SPI 80)</text>
  <text x="20" y="345" font-size="12">DT에는 80, /proc/interrupts에는 112, Linux virq는 22 — 세 번호를 섞지 말 것</text>
</svg>
```
그림 3 — GIC 3셀 지정자. 두 번째 셀은 SPI 영역 **안에서의** 번호라서 GIC 하드웨어 번호(INTID)는 +32, PPI는 +16이다.

이 변환은 GIC 드라이버의 `gic_irq_domain_translate()`가 한다(6.18 `drivers/irqchip/irq-gic.c`):

```c
		switch (fwspec->param[0]) {
		case 0:			/* SPI */
			*hwirq = fwspec->param[1] + 32;
			break;
		case 1:			/* PPI */
			*hwirq = fwspec->param[1] + 16;
			break;
```

그리고 GICv2의 `gic_set_type()`은 SPI에 대해 `IRQ_TYPE_LEVEL_HIGH`와 `IRQ_TYPE_EDGE_RISING` 말고는 `-EINVAL`을 돌려준다. **GIC SPI는 falling edge나 active-low 레벨을 못 받는다.** 센서의 INT 핀이 active-low라면 (1) 센서 레지스터로 극성을 바꾸거나, (2) GPIO 컨트롤러(엣지·극성 모두 지원하는 irqchip)에 연결해 `interrupt-parent = <&gpio3>`로 받는다. 실제 보드 대부분이 (2)다 (§12).

### 4.3 예제 7 — virt의 실제 번호로 검증하기

무엇을 확인하나: `virt.dts`에 적힌 지정자와 게스트의 `/proc/interrupts`를 맞춰 본다. 모듈을 넣은 뒤 `/proc/interrupts` 전체를 찍었다.

```sh
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd /var/tmp/D4/i.gz -dtb /var/tmp/D4/d4_ok.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=insmod~/d4_res.ko~&&~cat~/proc/interrupts"
```

```text
           CPU0       
 11:        569 GIC-0  27 Level     arch_timer
 13:          0 GIC-0  33 Level     uart-pl011
 16:          0 GICv2m-PCI-MSIX-0000:00:01.0   0 Edge      virtio0-config
 17:          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
 18:          0 GICv2m-PCI-MSIX-0000:00:01.0   2 Edge      virtio0-output.0
 19:          0 GIC-0  34 Level     rtc-pl031
 20:          0 GIC-0  23 Level     arm-pmu
 21:          0 9030000.pl061   3 Edge      GPIO Key Poweroff
 22:          3 GIC-0 112 Edge      d4_res
IPI0:         0       Rescheduling interrupts
...
```

(IPI1~7과 `Err:` 줄은 생략했다. `arch_timer` 횟수는 실행마다 다르다.) 열은 `virq: 횟수 컨트롤러 hwirq 트리거 이름`이다. DT와 대조하면:

| 장치 | DT `interrupts` | 해독 | `/proc/interrupts` hwirq |
|---|---|---|---|
| pl011 UART | `<0x00 0x01 0x04>` | SPI 1, level-high | 33 = 1 + 32 |
| pl031 RTC | `<0x00 0x02 0x04>` | SPI 2, level-high | 34 = 2 + 32 |
| PMU | `<0x01 0x07 0x104>` | PPI 7, CPU 마스크 0x1, level-high | 23 = 7 + 16 |
| arch timer (virtual) | `<0x01 0x0b 0x104>` (4개 중 셋째) | PPI 11 | 27 = 11 + 16 |
| **d4_res** | `<GIC_SPI 80 IRQ_TYPE_EDGE_RISING>` | SPI 80, edge-rising | **112** = 80 + 32 |
| poweroff 키 | `gpios = <0x8004 0x03 0x00>` | PL061 3번 핀 | 3 (PL061 irqchip 안의 번호) |

출력에서 볼 것: 마지막 줄 `9030000.pl061 3`은 GIC가 아니라 **PL061 GPIO 컨트롤러가 irqchip**인 경우다. 인터럽트 컨트롤러가 계층(GIC ← GPIO)으로 쌓인다는 뜻이고, 센서 INT 핀이 바로 이 경로를 탄다. `d4_res`의 횟수 3은 드라이버가 pending을 세 번 세운 결과다.

### 4.4 예제 8 — virq·hwirq를 sysfs와 DT 원본 바이트로 보기

```text
=== /sys/kernel/irq/22 ===
hwirq      112
type       edge
chip_name  
name       
actions    d4_res
=== DT raw ===
reg         00000000 09030fe0 00000000 00000020 
interrupts  00000000 00000050 00000001 
clocks      00000001 
vdd-supply  00000002 
reset-gpios 00008004 00000005 00000001 
```

출력에서 볼 것: `/proc/device-tree`(= `/sys/firmware/devicetree/base`)에 저장된 `interrupts`는 **big-endian 32비트 셀 3개** `0 0x50 1` 그대로다. 매크로 이름은 dtb에 남지 않는다. `/sys/kernel/irq/<virq>/hwirq`가 112라는 것은 커널이 GIC 드라이버의 translate를 거쳐 변환했다는 증거다. (`chip_name`이 비어 있는 것은 이 게스트의 GIC가 쓰는 `gic_chip`에 `.name`이 없고, 대신 `.irq_print_chip` 콜백이 `/proc/interrupts`에 `GIC-0`을 찍기 때문이다. `chip_name`은 `.name`만 보여 준다.)

### 4.5 드라이버 쪽 — `platform_get_irq()`와 그 친척

```c
int irq = platform_get_irq(pdev, 0);          /* 실패 시 음수, 없으면 "IRQ index 0 not found" 로그 */
int irq = platform_get_irq_byname(pdev, "INT1"); /* interrupt-names 사용 */
int irq = platform_get_irq_optional(pdev, 1); /* 없어도 로그 안 찍음 */
ret = devm_request_threaded_irq(dev, irq, NULL, imu_thread_fn,
                                IRQF_ONESHOT, "imu", priv); /* I2C 센서는 보통 threaded */
```

- 반환값은 **virq**(Linux 내부 번호, `irq_desc` 인덱스)다. hwirq(112)도, DT 번호(80)도 아니다. 이 숫자를 하드코딩하면 안 된다.
- 트리거 타입은 DT 셀 3에서 이미 설정되었으므로 `request_irq`의 flags에 `IRQF_TRIGGER_*`를 또 줄 필요가 없다. 주면 DT 설정을 덮어쓴다 (흔한 실수).
- `platform_get_irq()`는 인터럽트 컨트롤러 드라이버가 아직 안 떴으면 `-EPROBE_DEFER`를 돌려준다. **에러를 그대로 return**하면 된다.
- I2C/SPI 디바이스(`i2c_client`, `spi_device`)는 platform device가 아니다. 버스 코어가 DT의 첫 인터럽트를 미리 풀어 `client->irq`에 넣어 준다.

### 4.6 `interrupts-extended` — 인터럽트마다 다른 부모

`interrupt-parent`는 노드당 하나다. 인터럽트 두 개가 서로 다른 컨트롤러로 가면 `interrupts-extended`를 쓴다. 각 항목이 자기 phandle을 갖는 "phandle + 인자" 형식이다.

```dts
	imu@68 {
		/* INT1은 GPIO3 21번(falling), INT2는 SoC의 GIC SPI 60 (가상의 배선 예) */
		interrupts-extended = <&gpio3 21 IRQ_TYPE_EDGE_FALLING>,
				      <&gic GIC_SPI 60 IRQ_TYPE_LEVEL_HIGH>;
		interrupt-names = "INT1", "INT2";
	};
```

커널(`of_irq_parse_one()`)은 **`interrupts-extended`를 먼저 보고**, 없으면 `interrupts` + `interrupt-parent`를 본다. 둘 다 쓰면 `interrupts`는 무시된다.

---

## 5. `clocks` · `clock-names` · `assigned-clocks`

### 5.1 정의

- `clocks = <&provider 인자>, ...;` — 이 장치가 쓰는 클럭 목록. 인자 개수는 provider의 `#clock-cells`. 고정 오실레이터는 0셀, SoC 클럭 컨트롤러(CCM)는 보통 1셀(클럭 ID, `dt-bindings/clock/<soc>-clock.h`의 매크로).
- `clock-names = "core", ...;` — 같은 순서로 이름을 붙인다. 드라이버는 **이름으로** 찾는다.
- `assigned-clocks` / `assigned-clock-parents` / `assigned-clock-rates` — "probe 전에 이 클럭의 부모와 주파수를 이렇게 맞춰 놔라". platform bus의 probe 경로(`platform_probe()` → `of_clk_set_defaults()`)가 **드라이버 probe 직전에** 적용한다. 드라이버 코드는 이 속성을 모른다.

펌웨어 비유: CCM의 클럭 루트 mux·divider 설정(`assigned-*`)과 게이트 비트 켜기(`clk_prepare_enable`)를 분리해 둔 것이다.

### 5.2 드라이버 API

| API | 의미 |
|---|---|
| `devm_clk_get(dev, "core")` | 핸들만 얻기 |
| `devm_clk_get_enabled(dev, "core")` | 얻기 + `clk_prepare_enable` + 해제 시 자동 disable/unprepare |
| `devm_clk_get_optional_enabled(dev, "bus")` | 속성이 없으면 NULL(에러 아님) |
| `devm_clk_bulk_get_all_enabled(dev, &clks)` | 노드의 모든 클럭을 한 번에 |
| `clk_get_rate()` / `clk_set_rate()` | 주파수 읽기/요청 |

`clk_prepare`(잠들 수 있음, PLL 락 대기 등)와 `clk_enable`(원자적, 게이트 비트)이 나뉘어 있다는 점은 K 모듈에서 다룬다.

### 5.3 예제 9 — `clk_summary`로 consumer 확인

```text
=== clk_summary ===
                                 enable  prepare  protect                                duty  hardware                            connection
   clock                          count    count    count        rate   accuracy phase  cycle    enable   consumer                         id
---------------------------------------------------------------------------------------------------------------------------------------------
 d4_osc12m                           1       1        0        12000000    0          0     50000      Y   9030fe0.d4-res                  core                     
 clk24mhz                            5       5        0        24000000    0          0     50000      Y   9010000.pl031                   apb_pclk                 
```

출력에서 볼 것: 우리 `fixed-clock`(`clock-output-names = "d4_osc12m"`)의 enable/prepare count가 1이고, consumer 열에 `9030fe0.d4-res`, connection id에 `core`(= `clock-names`)가 보인다. 드라이버의 `devm_clk_get_enabled(dev, "core")` 한 줄이 이렇게 기록된다. virt의 기존 24 MHz 클럭은 UART·RTC·GPIO가 함께 써서 count가 5다(여기서는 첫 consumer만 한 줄로 보이도록 `grep`했다). 보드에서 "이 클럭이 왜 안 꺼지지?"를 추적할 때 이 파일이 출발점이다.

### 5.4 함정

- `clock-names` 순서와 `clocks` 순서가 어긋나면, 이름으로 찾은 클럭이 엉뚱한 것이 된다. 에러 없이 조용히 틀린다.
- `assigned-clock-rates`로 요청한 주파수를 하드웨어가 정확히 못 만들면 가장 가까운 값으로 잡힌다. `clk_summary`의 rate를 꼭 확인한다.
- 클럭 provider가 아직 등록 전이면 `devm_clk_get*`는 `-EPROBE_DEFER`. 그대로 return.

---

## 6. `resets` — 리셋 컨트롤러 API, 그리고 `reset-gpios` 폴백 함정

### 6.1 정의

SoC 안의 IP 블록 리셋 비트(i.MX의 SRC, Rockchip의 CRU 같은 리셋 컨트롤러)는 `resets`로 적는다.

```dts
	resets = <&src 3>, <&src 4>;		/* provider: #reset-cells = <1> */
	reset-names = "core", "phy";
```

```c
struct reset_control *rst = devm_reset_control_get_exclusive(dev, "core");
reset_control_assert(rst);  usleep_range(10, 20);  reset_control_deassert(rst);
/* 또는 reset_control_reset(rst); — 펄스를 지원하는 컨트롤러 */
```

변형: `_shared`(여러 장치가 공유, 참조 카운트), `_optional_`(속성 없으면 NULL), `devm_reset_control_array_get_*`(전부). virt 머신에는 리셋 컨트롤러가 없어서 실습에서는 "없음 → NULL" 경로만 확인했다.

```text
[    3.408127] d4_res 9030fe0.d4-res: resets      : none (optional -> NULL)
```

### 6.2 예제 10 — 함정: 이름 없이 index 0으로 찾으면 `reset-gpios`를 가로챈다

무엇을 확인하나: 같은 dtb에서 `rst_byidx=1`(리셋을 이름 없이 index 0으로 요청)로 모듈을 넣는다.

```sh
sh /Users/donh/.elinux_scratch/D4/boot.sh d4_ok D4_ARGS=rst_byidx=1~
```

```text
[    4.045783] d4_res 9030fe0.d4-res: reset gpio  : active_low=1 logical=1 raw=0 (asserted)
[    4.049823] d4_res 9030fe0.d4-res: reset gpio  : logical=0 raw=1 (released)
=== debug/gpio ===
gpiochip0: 8 GPIOs, parent: amba/9030000.pl061, 9030000.pl061:
 gpio-3   (                    |GPIO Key Poweroff   ) in  lo IRQ 
...
=== devices_deferred ===
9030fe0.d4-res	d4_res: resets
```

(앞부분 probe 로그는 정상 실행과 같아서 생략했다.) 출력에서 볼 것: `resets` 줄이 안 찍히고 probe가 **지연(defer)**되었다. 그리고 방금 잡았던 `gpio-5`가 `debug/gpio`에서 사라졌다 — probe가 `-EPROBE_DEFER`로 돌아가면서 `devm`이 GPIO·레귤레이터·클럭·IRQ를 전부 되돌렸다.

원인은 6.18 리셋 코어(`drivers/reset/core.c`의 `__of_reset_control_get()`)의 폴백이다. `resets` 속성이 없고 `CONFIG_RESET_GPIO`가 켜져 있으면, **`reset-gpios`를 보고 그 GPIO를 다루는 "reset-gpio" 장치를 자동으로 만든다.** 이 랩 커널은 `CONFIG_RESET_GPIO=m`인데 `reset-gpio.ko`가 initramfs에 없으니 리셋 컨트롤러가 영영 안 나타나고, 결과는 `-EPROBE_DEFER`다. 이름(`"core"`)으로 찾으면 `reset-names`가 없으니 폴백 전에 NULL로 끝난다.

교훈: **`reset-gpios` 한 줄을 GPIO API(`devm_gpiod_get(dev, "reset", ...)`)와 리셋 API 중 한쪽으로만 다룬다.** 바인딩이 `reset-gpios`를 정의한 드라이버는 대개 GPIO API를 쓴다. 이 폴백은 비교적 최근 커널에 들어온 기능이라 커널 버전에 따라 동작이 다르다.

---

## 7. `*-supply` — 레귤레이터: dummy, defer, `regulator_summary`

### 7.1 정의

`<이름>-supply = <&regulator>;` — 이 장치의 `<이름>` 전원 핀이 어느 레귤레이터에 붙었는지. 인자 셀이 없다. 드라이버는 `-supply`를 뗀 이름으로 요청한다.

```c
ret = devm_regulator_get_enable(dev, "vdd");           /* get + enable, 해제 시 자동 disable */
uv  = devm_regulator_get_enable_read_voltage(dev, "vddio"); /* get + enable + 전압(uV) 반환 */
struct regulator *r = devm_regulator_get(dev, "vdd");  /* 핸들이 필요하면 (전압 변경, PM 등) */
r = devm_regulator_get_optional(dev, "vref");          /* 없으면 -ENODEV (dummy 안 줌) */
```

펌웨어 비유: PMIC LDO를 켜고 램프업 시간을 기다리던 `power_on_imu()`가 레귤레이터 프레임워크로 옮겨 갔다. 램프 대기는 레귤레이터 노드의 `startup-delay-us` 같은 속성에 적고, 프레임워크가 enable 후 기다려 준다.

### 7.2 예제 11 — 정상: `regulator_summary`

```text
=== regulator_summary ===
 regulator                      use open bypass  opmode voltage current     min     max
---------------------------------------------------------------------------------------
 regulator-dummy                  1    0      0 unknown     0mV     0mA     0mV     0mV 
 d4_vddio_1v8                     2    1      0 unknown  1800mV     0mA  1800mV  1800mV 
    9030fe0.d4-res-vddio          1                                 0mA     0mV     0mV
 d4_vdd_3v3                       2    1      0 unknown  3300mV     0mA  3300mV  3300mV 
    9030fe0.d4-res-vdd            1                                 0mA     0mV     0mV
```

출력에서 볼 것: 레귤레이터 아래 들여쓴 줄이 consumer다(`장치이름-supply이름`). `open 1`은 consumer 핸들 1개, consumer 줄의 `1`은 그 consumer의 enable 횟수다. 레귤레이터 줄의 `use 2`는 1이 아니다 — 6.18 레귤레이터 코어(`set_machine_constraints()`)는 **enable GPIO도 enable 연산도 없는 레귤레이터를 always-on으로 취급**해서 use count를 1 올려 둔다. 우리 `regulator-fixed`에는 `gpio` 속성이 없으므로 여기에 해당하고, 그 위에 우리 enable 1회가 더해져 2가 되었다.

### 7.3 예제 12 — `vdd-supply`가 없으면: dummy regulator

무엇을 확인하나: `-DD4_NO_VDD`로 `vdd-supply` 줄을 뺀 dtb로 부팅한다.

```sh
sh /Users/donh/.elinux_scratch/D4/mkdtb.sh d4_novdd -DD4_NO_VDD
sh /Users/donh/.elinux_scratch/D4/boot.sh d4_novdd
```

```text
[    3.041024] d4_res 9030fe0.d4-res: clk core    : 12000000 Hz
[    3.041664] d4_res 9030fe0.d4-res: supply vdd not found, using dummy regulator
[    3.044580] d4_res 9030fe0.d4-res: vddio       : 1800000 uV
...
[    3.049554] d4_res 9030fe0.d4-res: probe done
=== regulator_summary ===
 regulator                      use open bypass  opmode voltage current     min     max
---------------------------------------------------------------------------------------
 regulator-dummy                  2    1      0 unknown     0mV     0mA     0mV     0mV 
    9030fe0.d4-res-vdd            1                                 0mA     0mV     0mV
 d4_vdd_3v3                       1    0      0 unknown  3300mV     0mA  3300mV  3300mV 
 d4_vddio_1v8                     2    1      0 unknown  1800mV     0mA  1800mV  1800mV 
    9030fe0.d4-res-vddio          1                                 0mA     0mV     0mV
```

출력에서 볼 것: probe는 **성공**했다. 레귤레이터 코어가 "DT에 전원이 안 적혀 있으면 보드가 항상 켜 둔 전원이겠지"라고 가정하고 `regulator-dummy`를 붙였다(`_regulator_get_common()`의 `NORMAL_GET` 경로). 경고 한 줄만 남는다. 이게 편하면서도 위험하다. **보드에서 정말로 전원이 꺼져 있으면, probe는 성공하고 첫 I2C 읽기에서 NACK가 난다.** 그래서 bring-up 중 이 경고를 보면 무시하지 말고 DT를 고쳐야 한다. `d4_vdd_3v3`은 consumer가 없어 use 1(always-on 몫)만 남았다.

> `devm_regulator_get_optional()`이나 exclusive get은 dummy를 주지 않고 `-ENODEV`를 돌려준다. "없으면 내부 기준전압을 쓴다" 같은 선택적 전원에 쓴다.

### 7.4 예제 13 — 레귤레이터 노드는 있는데 장치가 없으면: `-EPROBE_DEFER`

무엇을 확인하나: `-DD4_DEFER`로 `d4-vddio` 노드에 `status = "disabled"`를 넣는다. 노드는 있지만 레귤레이터 장치는 생기지 않는다.

```text
[    2.054357] d4_res 9030fe0.d4-res: clk core    : 12000000 Hz
=== regulator_summary ===
 regulator                      use open bypass  opmode voltage current     min     max
---------------------------------------------------------------------------------------
 regulator-dummy                  1    0      0 unknown     0mV     0mA     0mV     0mV 
 d4_vdd_3v3                       1    0      0 unknown  3300mV     0mA  3300mV  3300mV 
=== devices_deferred ===
9030fe0.d4-res	d4_res: vddio
```

출력에서 볼 것: `clk core` 다음 `vddio` 줄 없이 probe가 끝났다. `dev_err_probe()`는 `-EPROBE_DEFER`일 때 **로그를 찍지 않고** 사유(`vddio`)를 `devices_deferred`에 남긴다. `of_regulator_dev_lookup()`의 주석이 그대로 설명이다: "We have a node, but there is no device. assume it has not registered yet." — 노드가 있으니 언젠가 등록되리라 보고 기다린다. vdd도 받았다가 `devm`이 되돌려서 `d4_vdd_3v3`의 consumer 줄이 없다.

dummy(속성 없음)와 defer(속성은 있는데 provider 없음)의 차이가 핵심이다.

| 상황 | 결과 | 로그·흔적 |
|---|---|---|
| `vdd-supply` 속성 없음 | dummy regulator로 성공 | `supply vdd not found, using dummy regulator` |
| 속성 있음, provider 노드 disabled/드라이버 미로드 | `-EPROBE_DEFER` (무한 대기) | `devices_deferred`에 `d4_res: vddio` |
| 속성 있음, provider 정상 | 성공 | `regulator_summary`에 consumer 줄 |

---

## 8. `<이름>-gpios` — 핀 번호, 플래그, 논리값 vs 물리값

### 8.1 정의

```dts
	reset-gpios = <&gpio 5 GPIO_ACTIVE_LOW>;	/* provider #gpio-cells = <2>: 핀, 플래그 */
```

- 속성 이름은 `<con_id>-gpios`. 드라이버는 `devm_gpiod_get(dev, "reset", flags)`로 요청한다. gpiolib은 `reset-gpios`를 먼저, 다음에 옛 형식 `reset-gpio`를 찾는다(`gpio_suffixes[] = { "gpios", "gpio" }`). 새 DT에는 `-gpios`를 쓴다.
- 플래그(`dt-bindings/gpio/gpio.h`): `GPIO_ACTIVE_HIGH`(0), `GPIO_ACTIVE_LOW`(1), `GPIO_OPEN_DRAIN`, `GPIO_PULL_UP` 등.
- `GPIO_ACTIVE_LOW`면 gpiolib이 **논리값을 뒤집어 준다**. 드라이버는 "리셋 assert = 1"이라고만 생각하면 되고, 보드가 active-low인지 active-high인지는 DT만 안다.

펌웨어에서 `#define RESET_ASSERT() GPIO_CLR(PORT3, 6)` 같은 매크로로 극성을 숨기던 것을 gpiolib이 DT 플래그로 대신 한다.

### 8.2 예제 14 — 논리값과 물리값, `debug/gpio`

```text
[    3.403083] d4_res 9030fe0.d4-res: reset gpio  : active_low=1 logical=1 raw=0 (asserted)
[    3.407837] d4_res 9030fe0.d4-res: reset gpio  : logical=0 raw=1 (released)
=== debug/gpio ===
gpiochip0: 8 GPIOs, parent: amba/9030000.pl061, 9030000.pl061:
 gpio-3   (                    |GPIO Key Poweroff   ) in  lo IRQ 
 gpio-5   (                    |reset               ) out hi ACTIVE LOW
```

출력에서 볼 것:

- `GPIOD_OUT_HIGH`로 요청 → **논리 1 = assert**, 핀의 실제 레벨(raw)은 0 = low. active-low 리셋이 눌린 상태다.
- `gpiod_set_value_cansleep(rst, 0)`로 해제 → 논리 0, raw 1 = high.
- `debug/gpio`에서 5번 핀의 consumer 이름이 `reset`(= con_id), 방향 `out`, 레벨 `hi`, 그리고 `ACTIVE LOW` 표시가 있다. 레벨은 핀의 물리 상태(raw=1)와 일치한다.
- 3번 핀은 `gpio-keys`가 입력+IRQ로 쓰고 있다. 같은 핀을 우리가 요청했다면 `-EBUSY`였을 것이다.

### 8.3 함정 — 이중 반전

가장 흔한 실수: DT에 `GPIO_ACTIVE_LOW`를 쓰고, 드라이버도 "active-low니까 0을 써서 리셋"이라고 `gpiod_set_value(rst, 0)`을 쓴다. 그러면 논리 0 → 물리 high → **리셋이 안 걸린다.** gpiod API에서는 항상 **논리값**(1 = assert/active)으로 생각한다. 물리 레벨이 꼭 필요하면 `gpiod_set_raw_value()`가 따로 있다.

### 8.4 예제 15 — 잘못된 phandle: GPIO가 아닌 노드를 가리키면

무엇을 확인하나: `-DD4_BAD_GPIO`로 `reset-gpios = <&d4_osc 5 GPIO_ACTIVE_LOW>`(클럭 노드!)를 쓴다.

```sh
sh /Users/donh/.elinux_scratch/D4/mkdtb.sh d4_badgpio -DD4_BAD_GPIO 2>&1 | grep -v "pl0\|gpio-keys"
```

```text
/Users/donh/.elinux_scratch/D4/d4_nodes.dtsi:30.17-45.4: Warning (gpios_property): /d4-res@9030fe0: Missing property '#gpio-cells' in node /d4-osc or bad phandle (referred from reset-gpios[0])
```

dtc가 **컴파일 시점에 이미** 잡았다(경고라서 dtb는 만들어진다). 그대로 부팅하면:

```text
[    0.402194] OF: /d4-res@9030fe0: could not get #gpio-cells for /d4-osc
[    1.998215] d4_res 9030fe0.d4-res: probe start (/d4-res@9030fe0)
[    1.998451] d4_res 9030fe0.d4-res: clocks      : -> /d4-osc  args(0):
[    1.998593] OF: /d4-res@9030fe0: could not get #gpio-cells for /d4-osc
[    1.998641] d4_res 9030fe0.d4-res: reset-gpios : (none)
...
[    2.015718] OF: /d4-res@9030fe0: could not get #gpio-cells for /d4-osc
[    2.015849] d4_res 9030fe0.d4-res: error -EINVAL: reset gpio
[    2.015913] d4_res 9030fe0.d4-res: probe with driver d4_res failed with error -22
```

출력에서 볼 것: 0.40초의 첫 메시지는 모듈 로드 **전**, 부팅 중 fw_devlink가 DT의 공급자 관계를 미리 분석하면서 낸 것이다. 이후 범용 파서 `of_parse_phandle_with_args()`가 `/d4-osc`에서 `#gpio-cells`를 못 찾아 실패하고, `devm_gpiod_get()`은 `-EINVAL`로 probe를 끝낸다. defer와 달리 **영구 실패**라서 `devices_deferred`가 아니라 `probe ... failed with error -22`가 남는다.

---

## 9. `pinctrl-names` · `pinctrl-0` — 핀 상태(state)

### 9.1 정의

SoC 패드 하나는 여러 기능(ALT0 = I2C, ALT5 = GPIO …) 중 하나로 mux되고, 풀업·드라이브 세기·슬루율 같은 전기 설정을 갖는다. pinctrl은 이 설정 묶음을 **상태(state)** 단위로 적는다.

```dts
	pinctrl-names = "default", "sleep";	/* 상태 이름 목록 */
	pinctrl-0 = <&pinctrl_imu>;		/* 0번 상태 "default"의 그룹들 */
	pinctrl-1 = <&pinctrl_imu_sleep>;	/* 1번 상태 "sleep"의 그룹들 */
```

- `pinctrl-N`은 phandle 목록(인자 없음). 가리키는 노드는 **pin controller 노드 아래의 그룹 노드**이고, 그 안의 내용(`fsl,pins`, `pins`/`function`, `pinmux` …)은 SoC 벤더 바인딩마다 다르다.
- **드라이버가 아무것도 안 해도** `"default"` 상태는 적용된다. 드라이버 코어의 `really_probe()`가 probe 전에 `pinctrl_bind_pins()`를 불러 `"default"`(있으면 먼저 `"init"`)를 선택한다.
- 이름 상수: `"default"`, `"init"`, `"idle"`, `"sleep"`(`include/linux/pinctrl/pinctrl-state.h`). 시스템 suspend 때 `pinctrl_pm_select_sleep_state(dev)`로 `"sleep"`으로 바꾸는 식이다.
- 드라이버가 상태를 직접 바꿔야 하면(예: I2C 버스 복구 때 SCL을 GPIO로 바꿔 토글) `devm_pinctrl_get()` → `pinctrl_lookup_state(p, "gpio")` → `pinctrl_select_state()`.

virt 머신에는 pin controller가 없다. 그래서 실습 대신 i.MX8MM 보드 예제(§12)에서 실제 컴파일 결과로 본다.

### 9.2 i.MX의 `fsl,pins` 한 줄 해독

i.MX 계열은 한 핀을 6개 숫자로 적는다: `<mux_reg conf_reg input_reg mux_mode input_val>` 5개는 `imx8mm-pinfunc.h` 매크로가 채우고, 마지막 하나가 패드 설정값이다. §12 보드를 컴파일한 dtb에서 `fdtget`으로 꺼낸 실제 값:

```text
148 3b0 0 5 0 141 10c 374 0 5 0 19 14c 3b4 0 5 0 19
```

첫 6개 `148 3b0 0 5 0 141`은 `MX8MM_IOMUXC_SAI5_RXD0_GPIO3_IO21 0x141`이다: mux 레지스터 오프셋 0x148에 ALT **5**(= GPIO3_IO21 기능)를 쓰고, 패드 설정 레지스터 0x3b0에 0x141을 쓴다. Don이 펌웨어에서 IOMUXC 레지스터에 직접 쓰던 바로 그 두 번의 쓰기가 **데이터로** 적혀 있는 것이다. 이 노드가 없으면 패드는 리셋 기본 기능에 머물러, IMU INT가 아무리 토글해도 GPIO3 컨트롤러는 그 신호를 못 본다.

---

## 10. `dmas` · `dma-names`

### 10.1 정의

```dts
	/* i.MX8MM UART1 (imx8mm.dtsi 원문) */
	dmas = <&sdma1 22 4 0>, <&sdma1 23 4 0>;
	dma-names = "rx", "tx";
```

`sdma1`은 `#dma-cells = <3>`인 DMA 컨트롤러이고, 세 인자의 의미(요청 라인 번호, 전송 종류, 우선순위 등)는 **그 컨트롤러 바인딩**이 정한다. consumer 드라이버는 의미를 모르고 이름만 쓴다.

```c
struct dma_chan *rx = dma_request_chan(dev, "rx");  /* 실패 시 ERR_PTR, devm 아님 → 해제 필요 */
if (IS_ERR(rx))
	return dev_err_probe(dev, PTR_ERR(rx), "rx dma\n");
/* ... dmaengine_slave_config(), dmaengine_prep_slave_sg() ... */
dma_release_channel(rx);
```

내부 경로는 `dma_request_chan()` → `of_dma_request_slave_channel()` → `dma-names`에서 index 찾기 → `of_parse_phandle_with_args(..., "dmas", "#dma-cells", ...)` → DMA 컨트롤러 드라이버의 `of_dma_xlate`. 역시 phandle + 인자 패턴이다.

### 10.2 실습 결과

```text
[    3.408171] d4_res 9030fe0.d4-res: dma rx      : -ENODEV
```

우리 노드에는 `dmas`가 없으므로 `-ENODEV`. 실제 드라이버는 이것을 "PIO 모드로 폴백"의 신호로 쓴다(많은 UART·SPI 드라이버가 그렇다). 반면 DMA 컨트롤러 드라이버가 아직 probe 전이면 `-EPROBE_DEFER`가 오는데, 이를 PIO 폴백으로 처리해 버리면 **영원히 PIO로 도는** 성능 버그가 된다. 둘을 구분해서 처리한다. DMA 엔진 자체는 I 모듈에서 다룬다.

---

## 11. 실패 반응 한눈에 — "센서가 안 잡혀요" 진단 순서

지금까지 본 실패를 모으면 커널의 반응은 세 종류뿐이다.

| DT 상태 | 커널 반응 | 어디서 보나 | 이 노트의 예 |
|---|---|---|---|
| 속성 문법 오류 (셀 수, phandle이 엉뚱한 노드) | 영구 실패 `-EINVAL`/`-ENOENT` | `dmesg`: `probe ... failed with error -22`, 그 전에 `OF: ... could not get #gpio-cells` | 예제 15 |
| 속성은 맞는데 provider가 아직 없음 (드라이버 미로드, `status = "disabled"`, 모듈이 initramfs에 없음) | `-EPROBE_DEFER` → 나중에 재시도 | `/sys/kernel/debug/devices_deferred` (로그 없음!) | 예제 10, 13 |
| 전원 속성 자체가 없음 | dummy regulator로 성공 | `supply vdd not found, using dummy regulator` | 예제 12 |
| 남이 이미 점유 | `-EBUSY` | `can't request region`, GPIO는 `debug/gpio`에서 소유자 확인 | 예제 5, §3 |

그래서 bring-up 때의 순서는 이렇다.

```sh
dmesg | grep -iE "<장치이름>|OF:"                      # 영구 실패·경고
cat /sys/kernel/debug/devices_deferred                 # 기다리는 중인가, 무엇을?
ls /sys/bus/platform/devices/ | grep <unit-address>    # 장치는 만들어졌나 (D3)
ls -l /sys/bus/platform/devices/<장치>/driver           # 드라이버가 붙었나
cat /sys/kernel/debug/clk/clk_summary /sys/kernel/debug/regulator/regulator_summary /sys/kernel/debug/gpio
```

`-EPROBE_DEFER`는 **에러가 아니라 "나중에 다시"**라는 점이 핵심이다. 드라이버 코어는 지연된 장치를 목록에 넣어 두고, 다른 드라이버가 probe에 성공할 때마다 다시 시도한다. 부팅이 끝날 때까지 남아 있으면 그때가 진짜 문제다. 펌웨어로 치면 "init 순서 의존성을 하드코딩하는 대신, 준비 안 된 것은 큐에 넣고 다른 게 끝날 때마다 다시 돌리는 스케줄러"다.

---

## 12. 실전 보드 예 — I2C IMU 하나 붙이기 (i.MX8MM, dtc 컴파일만)

### 12.1 배선 가정

센서 키트 보드: i.MX8MM의 I2C3에 TDK ICM-42688 IMU(주소 0x68). INT1은 GPIO3_IO21 패드로, 리셋은 GPIO3_IO6(active-low), VDD 3.3 V는 GPIO3_IO22로 켜는 로드 스위치, VDDIO 1.8 V는 항상 켜진 레일.

> 이 dts는 **부팅하지 않았다.** 실제 커널 소스의 `imx8mm.dtsi`를 include해서 dtc로 컴파일만 하고, 결과 dtb를 `fdtget`으로 검사했다. 그리고 주의: ICM-42688의 실제 바인딩(`invensense,icm42600.yaml`)에는 `reset-gpios`가 없다(칩에 리셋 핀이 없다). 여기서는 리셋 핀 표기를 보여 주려고 넣었으므로 `make dtbs_check`(D7)에서는 이 줄이 거부된다. 리셋 핀이 있는 칩(예: `bosch,bno055` 바인딩)이라면 그대로 쓰는 형식이다.

```dts
// SPDX-License-Identifier: GPL-2.0
/* 센서 키트 데모 보드: i.MX8MM + I2C3의 ICM-42688 IMU (부팅하지 않음, dtc 컴파일만) */
/dts-v1/;
#include "imx8mm.dtsi"
#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/interrupt-controller/irq.h>

/ {
	model = "D4 sensor kit demo (i.MX8MM)";
	compatible = "dons,d4-sensorkit", "fsl,imx8mm";

	reg_imu_vdd: regulator-imu-vdd {
		compatible = "regulator-fixed";
		regulator-name = "IMU_VDD_3V3";
		regulator-min-microvolt = <3300000>;
		regulator-max-microvolt = <3300000>;
		gpio = <&gpio3 22 GPIO_ACTIVE_HIGH>;	/* 로드 스위치 EN */
		enable-active-high;
		startup-delay-us = <1000>;
	};

	reg_imu_vddio: regulator-imu-vddio {
		compatible = "regulator-fixed";
		regulator-name = "IMU_VDDIO_1V8";
		regulator-min-microvolt = <1800000>;
		regulator-max-microvolt = <1800000>;
		regulator-always-on;
	};
};

&i2c3 {
	clock-frequency = <400000>;
	assigned-clocks = <&clk IMX8MM_CLK_I2C3>;
	assigned-clock-parents = <&clk IMX8MM_CLK_24M>;
	assigned-clock-rates = <24000000>;
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_i2c3>;
	status = "okay";

	imu: imu@68 {
		compatible = "invensense,icm42688";
		reg = <0x68>;
		pinctrl-names = "default";
		pinctrl-0 = <&pinctrl_imu>;
		interrupt-parent = <&gpio3>;
		interrupts = <21 IRQ_TYPE_EDGE_FALLING>;
		interrupt-names = "INT1";
		vdd-supply = <&reg_imu_vdd>;
		vddio-supply = <&reg_imu_vddio>;
		reset-gpios = <&gpio3 6 GPIO_ACTIVE_LOW>;	/* 가상 배선 (본문 주의 참조) */
	};
};

&iomuxc {
	pinctrl_i2c3: i2c3grp {
		fsl,pins = <
			MX8MM_IOMUXC_I2C3_SCL_I2C3_SCL		0x400001c3
			MX8MM_IOMUXC_I2C3_SDA_I2C3_SDA		0x400001c3
		>;
	};

	pinctrl_imu: imugrp {
		fsl,pins = <
			MX8MM_IOMUXC_SAI5_RXD0_GPIO3_IO21	0x141	/* INT1 */
			MX8MM_IOMUXC_NAND_DATA00_GPIO3_IO6	0x19	/* RESET_N */
			MX8MM_IOMUXC_SAI5_RXD1_GPIO3_IO22	0x19	/* VDD EN */
		>;
	};
};
```

### 12.2 한 줄씩 읽기

| 줄 | 뜻 | 누가 처리하나 |
|---|---|---|
| `#include "imx8mm.dtsi"` | SoC 공통 노드(GIC, CCM, GPIO1~5, I2C1~4, IOMUXC …)를 가져온다 | dtc 전처리 (D5) |
| `reg_imu_vdd` … `gpio = <&gpio3 22 …>`, `enable-active-high` | 로드 스위치 EN 핀으로 켜는 고정 3.3 V 레일 | regulator-fixed 드라이버 |
| `startup-delay-us = <1000>` | enable 후 1 ms 대기 (레일 안정화) | 레귤레이터 코어 |
| `regulator-always-on` | VDDIO는 절대 끄지 않음 | 레귤레이터 코어 |
| `&i2c3 { ... status = "okay"; }` | dtsi에서 `disabled`였던 I2C3 컨트롤러를 켠다 (label 참조로 덮어쓰기, D5) | of_platform |
| `clock-frequency = <400000>` | I2C 버스 속도 400 kHz (Fast mode) | i2c-imx 드라이버가 읽음 |
| `assigned-clocks` / `-parents` / `-rates` | I2C3 클럭 루트를 24 MHz 오실레이터에서 24 MHz로 | `of_clk_set_defaults()`, probe 직전 |
| `pinctrl-0 = <&pinctrl_i2c3>` | SCL/SDA 패드를 I2C 기능으로 mux | `pinctrl_bind_pins()`, probe 직전 |
| `imu@68`, `reg = <0x68>` | I2C 7비트 주소. I2C 버스 노드의 `#address-cells = 1`, `#size-cells = 0`이라 크기 없음 | i2c 코어 → `i2c_client.addr` |
| `compatible = "invensense,icm42688"` | `inv_icm42600_i2c` 드라이버와 매칭 (D3) | i2c 코어 |
| `interrupt-parent = <&gpio3>` | 이 노드의 인터럽트는 GIC가 아니라 GPIO3 irqchip으로 | `of_irq_find_parent()` |
| `interrupts = <21 IRQ_TYPE_EDGE_FALLING>` | GPIO3의 21번 핀, falling edge. GPIO3의 `#interrupt-cells = <2>`라서 2셀 | gpio-mxc irqchip |
| `interrupt-names = "INT1"` | 드라이버가 INT1/INT2 중 어느 핀인지 구분 | 드라이버 (`fwnode_irq_get_byname`) |
| `vdd-supply`, `vddio-supply` | 전원 두 개 → 드라이버가 `"vdd"`, `"vddio"`로 요청 | 레귤레이터 코어 |
| `reset-gpios = <&gpio3 6 GPIO_ACTIVE_LOW>` | 리셋 핀 (가상 배선) | gpiolib |
| `pinctrl_imu` 그룹 | INT1·RESET_N·VDD_EN 세 패드를 GPIO(ALT5)로 mux + 패드 설정 | i.MX pinctrl 드라이버 |

GIC 쪽은 어디 갔을까? GPIO3 컨트롤러 노드(`imx8mm.dtsi`) 자신이 `interrupts = <GIC_SPI 68 IRQ_TYPE_LEVEL_HIGH>, <GIC_SPI 69 IRQ_TYPE_LEVEL_HIGH>;`로 GIC에 연결되어 있다. IMU의 falling edge는 GPIO3가 받아서 GIC에는 level-high로 올린다. §4.2의 "GIC SPI는 falling을 못 받는다" 문제가 이렇게 해결된다.

### 12.3 예제 16 — 컴파일하고 phandle 풀어 보기

```sh
K=/var/lab/src/linux-6.18.55
cpp -nostdinc -I $K/include -I $K/arch/arm64/boot/dts/freescale -undef -x assembler-with-cpp \
    /Users/donh/.elinux_scratch/D4/board/d4-sensorkit.dts > sk.pp.dts
dtc -@ -I dts -O dtb -o sk.dtb sk.pp.dts; echo "rc=$?"
dtc -I dtb -O dts sk.dtb 2>/dev/null | sed -n "/imu@68 {/,/};/p"
for n in gpio3 reg_imu_vdd reg_imu_vddio pinctrl_imu clk gic; do
  printf "%-14s %-45s phandle=" $n "$(fdtget sk.dtb /__symbols__ $n)"
  fdtget -t x sk.dtb "$(fdtget sk.dtb /__symbols__ $n)" phandle
done
```

```text
/var/lab/src/linux-6.18.55/arch/arm64/boot/dts/freescale/imx8mm.dtsi:1339.24-1372.5: Warning (unique_unit_address): /soc@0/pcie@33800000: duplicate unit-address (also used in node /soc@0/pcie-ep@33800000)
rc=0
				imu@68 {
					compatible = "invensense,icm42688";
					reg = <0x68>;
					pinctrl-names = "default";
					pinctrl-0 = <0x1f>;
					interrupt-parent = <0x20>;
					interrupts = <0x15 0x02>;
					interrupt-names = "INT1";
					vdd-supply = <0x21>;
					vddio-supply = <0x22>;
					reset-gpios = <0x20 0x06 0x01>;
					phandle = <0x67>;
				};
gpio3          /soc@0/bus@30000000/gpio@30220000             phandle=20
reg_imu_vdd    /regulator-imu-vdd                            phandle=21
reg_imu_vddio  /regulator-imu-vddio                          phandle=22
pinctrl_imu    /soc@0/bus@30000000/pinctrl@30330000/imugrp   phandle=1f
clk            /soc@0/bus@30000000/clock-controller@30380000 phandle=2
gic            /soc@0/interrupt-controller@38800000          phandle=1
```

출력에서 볼 것:

- 경고 하나는 upstream `imx8mm.dtsi`의 PCIe RC/EP 노드가 같은 주소를 쓰는 것이라 우리와 무관하다. 커널의 `make dtbs`는 `scripts/Makefile.dtbs`에서 이 검사를 `-Wno-unique_unit_address`로 끄고, 대신 "둘 다 enabled일 때만" 경고하는 `unique_unit_address_if_enabled`를 켠다. 맨 dtc는 그 설정이 없어서 경고가 보인 것이다.
- 디컴파일된 `imu@68`에는 label이 사라지고 **숫자만** 남는다. `interrupt-parent = <0x20>`과 `reset-gpios`의 첫 셀 `0x20`이 같은 노드(`gpio3`)다. `interrupts = <0x15 0x02>`는 21번 핀, `IRQ_TYPE_EDGE_FALLING`(2).
- `-@`로 컴파일하면 `/__symbols__` 노드에 label → 경로 표가 남아 `fdtget`으로 역추적할 수 있다(오버레이에 필요, D6).
- `imu@68`에도 `phandle = <0x67>`이 붙었다. 이 dts 안에서 `imu` label을 다른 곳이 참조하지 않아도, `-@`가 모든 label 노드에 phandle을 만들어 두기 때문이다.

같은 dtb에서 I2C3의 클럭 속성:

```sh
P=$(fdtget sk.dtb /__symbols__ i2c3); echo $P
for p in clocks assigned-clocks assigned-clock-parents assigned-clock-rates; do printf "%-24s" $p; fdtget -t x sk.dtb $P $p; done
```

```text
/soc@0/bus@30800000/i2c@30a40000
clocks                  2 a6
assigned-clocks         2 7d
assigned-clock-parents  2 2
assigned-clock-rates    16e3600
```

`2`는 CCM(`clk`)의 phandle, 뒤 숫자는 `#clock-cells = <1>`인 클럭 ID다. `imx8mm-clock.h`에서 0xa6 = 166 = `IMX8MM_CLK_I2C3_ROOT`(게이트), 0x7d = 125 = `IMX8MM_CLK_I2C3`(루트 mux/divider), 2 = `IMX8MM_CLK_24M`. 0x16e3600 = 24000000. 드라이버는 게이트(`clocks`)만 켜고, 루트 설정(`assigned-*`)은 코어가 미리 한다.

### 12.4 이 노드를 받는 드라이버 쪽 모양

I2C 드라이버의 probe는 이 노트 실습의 축소판이다. (아래는 일반적인 형태를 보여 주는 스케치이고 `inv_icm42600`의 실제 코드가 아니다.)

```c
static int imu_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct gpio_desc *rst;
	int irq, ret;

	ret = devm_regulator_get_enable(dev, "vdd");          /* vdd-supply, 1 ms 대기는 DT가 */
	if (ret)
		return dev_err_probe(dev, ret, "vdd\n");
	ret = devm_regulator_get_enable(dev, "vddio");
	if (ret)
		return dev_err_probe(dev, ret, "vddio\n");
	rst = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH); /* assert */
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "reset\n");
	if (rst) {
		usleep_range(100, 200);
		gpiod_set_value_cansleep(rst, 0);                    /* release */
	}
	irq = fwnode_irq_get_byname(dev_fwnode(dev), "INT1");     /* virq */
	if (irq < 0)
		return dev_err_probe(dev, irq, "INT1\n");
	/* ... WHO_AM_I 읽기 → devm_request_threaded_irq(dev, irq, NULL, fn, IRQF_ONESHOT, ...) */
	return 0;
}
```

이 코드에는 숫자가 하나도 없다. 보드가 바뀌어도(다른 GPIO, 다른 PMIC, 다른 I2C 버스) **드라이버는 그대로, DT만 바뀐다.** 이것이 D1에서 말한 DT의 존재 이유가 자원 수준에서 실현되는 모습이다.

---

## 13. 커널 안에서는 무슨 일이 일어나나

### 13.1 두 가지 경로를 끝까지 따라가기

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="24" text-anchor="middle" font-size="13" font-weight="bold">interrupts → virq</text>
  <text x="510" y="24" text-anchor="middle" font-size="13" font-weight="bold">reset-gpios → gpio_desc</text>
  <rect x="20" y="40" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="58" font-size="13">platform_get_irq(pdev, 0)</text>
  <text x="30" y="76" font-size="12">드라이버가 부르는 곳</text>
  <rect x="20" y="100" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="30" y="118" font-size="13">of_irq_parse_one()</text>
  <text x="30" y="136" font-size="12">interrupts-extended 먼저, 없으면 interrupts</text>
  <rect x="20" y="160" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="30" y="178" font-size="13">of_irq_find_parent()</text>
  <text x="30" y="196" font-size="12">interrupt-parent를 위로 올라가며 탐색</text>
  <rect x="20" y="220" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="238" font-size="13">irq_create_of_mapping()</text>
  <text x="30" y="256" font-size="12">GIC translate: SPI 80 → hwirq 112, 타입</text>
  <rect x="20" y="280" width="300" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="298" font-size="13">virq 22 ↔ hwirq 112</text>
  <text x="30" y="316" font-size="12">irq_desc 할당, 트리거 설정 완료</text>
  <line x1="170" y1="84" x2="170" y2="98" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="144" x2="170" y2="158" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="204" x2="170" y2="218" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="264" x2="170" y2="278" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="360" y="40" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="370" y="58" font-size="13">devm_gpiod_get(dev, "reset", …)</text>
  <text x="370" y="76" font-size="12">드라이버가 부르는 곳</text>
  <rect x="360" y="100" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="370" y="118" font-size="13">gpiod_find_and_request()</text>
  <text x="370" y="136" font-size="12">of_find_gpio: reset-gpios → reset-gpio</text>
  <rect x="360" y="160" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="370" y="178" font-size="13">of_parse_phandle_with_args()</text>
  <text x="370" y="196" font-size="12">#gpio-cells = 2 → 인자 5, 1</text>
  <rect x="360" y="220" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="370" y="238" font-size="13">gpio_chip->of_xlate()</text>
  <text x="370" y="256" font-size="12">of_gpio_twocell_xlate: 칩 안 5번 핀</text>
  <rect x="360" y="280" width="300" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="370" y="298" font-size="13">struct gpio_desc</text>
  <text x="370" y="316" font-size="12">active-low 플래그 기록, 소유자 "reset"</text>
  <line x1="510" y1="84" x2="510" y2="98" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="144" x2="510" y2="158" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="204" x2="510" y2="218" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="264" x2="510" y2="278" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="20" y="356" font-size="12">두 경로 모두: provider(irq domain / gpio_device)가 아직 등록 전이면 그 자리에서 -EPROBE_DEFER</text>
</svg>
```
그림 4 — 같은 "phandle + 인자" 문법이 프레임워크마다 다른 provider 목록과 xlate 함수로 해석된다. 왼쪽은 irq domain, 오른쪽은 gpiolib.

나머지도 같은 모양이다.

- **clk**: `devm_clk_get_enabled()` → `of_clk_get_hw()` → `of_parse_clkspec()`(= `clocks` + `#clock-cells`, 이름은 `clock-names`로 index 변환) → 등록된 clock provider 목록에서 노드 일치 → provider의 `get` 콜백(`of_clk_hw_simple_get`, `of_clk_hw_onecell_get` 등).
- **regulator**: `devm_regulator_get_enable()` → `regulator_dev_lookup()` → `of_get_regulator()`가 `"vdd-supply"` phandle을 읽고 → `of_find_regulator_by_node()`. 없으면 `-EPROBE_DEFER`, 속성이 없으면 dummy (§7).
- **reset**: `__of_reset_control_get()` → `resets` + `#reset-cells` → `reset_controller_dev`의 `of_xlate` (§6의 GPIO 폴백도 여기).
- **dma**: `dma_request_chan()` → `of_dma_request_slave_channel()` → `dmas` + `#dma-cells` → DMA 컨트롤러의 `of_dma_xlate`.
- **pinctrl**: `really_probe()` → `pinctrl_bind_pins()` → `pinctrl-names`/`pinctrl-N` 파싱 → pin controller 드라이버의 `dt_node_to_map`이 그룹 노드를 레지스터 쓰기 목록으로 변환 → `"default"` 선택.
- **assigned-clocks**: `platform_probe()` → `of_clk_set_defaults()` → `clk_set_parent()`/`clk_set_rate()` — 드라이버 probe 호출 **전**.

### 13.2 드라이버 쪽 요약표

```svg
<svg viewBox="0 0 680 402" xmlns="http://www.w3.org/2000/svg">
  <rect x="10" y="8" width="170" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="27" text-anchor="middle" font-size="13" font-weight="bold">DT 속성</text>
  <rect x="180" y="8" width="270" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="315" y="27" text-anchor="middle" font-size="13" font-weight="bold">드라이버 API</text>
  <rect x="450" y="8" width="220" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="27" text-anchor="middle" font-size="13" font-weight="bold">결과 타입 · 확인 위치</text>
  <rect x="10" y="40" width="660" height="38" fill="#888" fill-opacity="0.07"/>
  <text x="18" y="56" font-size="12">reg</text>
  <text x="18" y="72" font-size="12">reg-names</text>
  <text x="188" y="56" font-size="12">devm_platform_ioremap_resource()</text>
  <text x="188" y="72" font-size="12">platform_get_resource()</text>
  <text x="458" y="56" font-size="12">void __iomem *</text>
  <text x="458" y="72" font-size="12">/proc/iomem</text>
  <text x="18" y="94" font-size="12">interrupts</text>
  <text x="18" y="110" font-size="12">interrupts-extended</text>
  <text x="188" y="94" font-size="12">platform_get_irq()</text>
  <text x="188" y="110" font-size="12">devm_request_irq()</text>
  <text x="458" y="94" font-size="12">int virq</text>
  <text x="458" y="110" font-size="12">/proc/interrupts</text>
  <rect x="10" y="116" width="660" height="38" fill="#888" fill-opacity="0.07"/>
  <text x="18" y="132" font-size="12">clocks</text>
  <text x="18" y="148" font-size="12">clock-names</text>
  <text x="188" y="132" font-size="12">devm_clk_get_enabled()</text>
  <text x="188" y="148" font-size="12">clk_get_rate()</text>
  <text x="458" y="132" font-size="12">struct clk *</text>
  <text x="458" y="148" font-size="12">clk_summary</text>
  <text x="18" y="170" font-size="12">assigned-clocks</text>
  <text x="18" y="186" font-size="12">-parents, -rates</text>
  <text x="188" y="170" font-size="12">(코어가 probe 직전 적용)</text>
  <text x="188" y="186" font-size="12">of_clk_set_defaults()</text>
  <text x="458" y="170" font-size="12">부모·주파수 설정</text>
  <text x="458" y="186" font-size="12">clk_summary</text>
  <rect x="10" y="192" width="660" height="38" fill="#888" fill-opacity="0.07"/>
  <text x="18" y="208" font-size="12">resets</text>
  <text x="18" y="224" font-size="12">reset-names</text>
  <text x="188" y="208" font-size="12">devm_reset_control_get_exclusive()</text>
  <text x="188" y="224" font-size="12">reset_control_deassert()</text>
  <text x="458" y="208" font-size="12">struct reset_control *</text>
  <text x="458" y="224" font-size="12">드라이버 로그</text>
  <text x="18" y="246" font-size="12">vdd-supply</text>
  <text x="18" y="262" font-size="12">(셀 없음)</text>
  <text x="188" y="246" font-size="12">devm_regulator_get_enable()</text>
  <text x="188" y="262" font-size="12">devm_regulator_get()</text>
  <text x="458" y="246" font-size="12">struct regulator *</text>
  <text x="458" y="262" font-size="12">regulator_summary</text>
  <rect x="10" y="268" width="660" height="38" fill="#888" fill-opacity="0.07"/>
  <text x="18" y="284" font-size="12">reset-gpios</text>
  <text x="18" y="300" font-size="12">#gpio-cells = 2</text>
  <text x="188" y="284" font-size="12">devm_gpiod_get()</text>
  <text x="188" y="300" font-size="12">gpiod_set_value_cansleep()</text>
  <text x="458" y="284" font-size="12">struct gpio_desc *</text>
  <text x="458" y="300" font-size="12">debug/gpio</text>
  <text x="18" y="322" font-size="12">pinctrl-names</text>
  <text x="18" y="338" font-size="12">pinctrl-0, -1 …</text>
  <text x="188" y="322" font-size="12">(코어 pinctrl_bind_pins 자동)</text>
  <text x="188" y="338" font-size="12">pinctrl_select_state()</text>
  <text x="458" y="322" font-size="12">struct pinctrl *</text>
  <text x="458" y="338" font-size="12">debug/pinctrl</text>
  <rect x="10" y="344" width="660" height="38" fill="#888" fill-opacity="0.07"/>
  <text x="18" y="360" font-size="12">dmas</text>
  <text x="18" y="376" font-size="12">dma-names</text>
  <text x="188" y="360" font-size="12">dma_request_chan()</text>
  <text x="188" y="376" font-size="12">dma_release_channel()</text>
  <text x="458" y="360" font-size="12">struct dma_chan *</text>
  <text x="458" y="376" font-size="12">dmaengine 로그</text>
  <line x1="10" y1="382" x2="670" y2="382" stroke="currentColor" stroke-opacity="0.4"/>
</svg>
```
그림 5 — DT 속성 → 드라이버 API → 받는 핸들과 확인 위치. 모든 줄이 "이름으로 요청, 핸들로 받음, `devm`으로 자동 해제"라는 같은 모양이다.

### 13.3 fw_devlink — 부팅 순서를 DT에서 미리 계산

예제 15의 `[0.402194] OF: ... could not get #gpio-cells` 줄은 우리 모듈이 로드되기 **전**에 찍혔다. 커널은 부팅 초기에 모든 DT 노드의 자원 속성(`clocks`, `interrupts`, `*-supply`, `*-gpios`, `resets`, `dmas`, `pinctrl-N` …)을 훑어 **supplier → consumer 관계(device link)**를 미리 만들어 둔다. 이것이 fw_devlink다. supplier 드라이버가 bind되기 전에는 consumer의 probe를 아예 시도하지 않게 해서, 쓸데없는 `-EPROBE_DEFER` 왕복을 줄인다. 자원 속성을 정확히 쓰는 것은 그래서 **부팅 순서 최적화**와도 연결된다. 세부는 G 모듈(드라이버 모델)에서 다룬다.

### 13.4 다시 만나는 곳

| 이 노트의 내용 | 깊게 다루는 곳 |
|---|---|
| `devm_` 자원 관리, probe/defer, device link | G (드라이버 모델) |
| `ioremap`, Device 메모리 속성, `readl`/`writel` | F (메모리), H (MMIO 드라이버) |
| irq domain 계층, threaded IRQ, `IRQF_ONESHOT` | G·H (인터럽트 처리) |
| clk prepare/enable, 레귤레이터 PM, runtime PM | K (전원·클럭) |
| gpiolib, pinctrl, GPIO irqchip | H·M (GPIO·I2C·SPI) |
| dmaengine, `dma_map_*`, 캐시 일관성 | I (DMA) |
| `of_property_read_*` vs `device_property_*` | D8 |

---

## 14. Embedded 실무에서 다시 보기

### 14.1 "IMU가 `/sys/bus/iio`에 안 보여요"

센서 키트 bring-up 첫날의 단골이다. 순서대로 본다.

1. `dmesg | grep -i icm` — 아무것도 없으면 장치가 안 만들어졌거나 드라이버가 없다. I2C 버스 노드가 `status = "okay"`인지, `CONFIG_INV_ICM42600_I2C`가 켜졌는지(D3).
2. `cat /sys/kernel/debug/devices_deferred` — `vdd`, `INT1`, `reset` 같은 사유가 있으면 해당 provider(PMIC 드라이버, GPIO 컨트롤러)를 확인.
3. probe는 됐는데 데이터가 안 오면 `/proc/interrupts`의 카운트를 본다. 0이면 pinctrl(패드가 GPIO로 mux 안 됨) 또는 트리거 타입(센서는 active-low pulse인데 DT는 rising) 의심.
4. 스코프로 INT 핀을 찍는 것은 Don이 가장 잘하는 부분이다. 핀이 토글되는데 카운트가 0이면 소프트웨어(DT) 문제, 핀이 안 토글되면 센서 설정 문제다.

### 14.2 전원 시퀀스 요구사항을 DT로 옮기기

데이터시트에 "VDDIO가 VDD보다 먼저 올라와야 하고, VDD 안정 후 1 ms 뒤 리셋 해제" 같은 요구가 있다고 하자. 펌웨어에서는 `imu_power_on()`에 순서대로 적었다. Linux에서는:

- 순서는 **드라이버의 `devm_regulator_get_enable()` 호출 순서**(vddio 먼저).
- 램프 대기는 **레귤레이터 노드의 `startup-delay-us`**(보드 특성이므로 DT).
- 리셋 해제 전 대기는 **드라이버의 `usleep_range()`**(칩 특성이므로 드라이버).

"보드 특성은 DT, 칩 특성은 드라이버"라는 구분이 면접에서도 좋은 답이 된다.

### 14.3 같은 드라이버, 다른 보드

차세대 보드에서 IMU가 SPI로 옮기고, INT가 GPIO5로, VDD가 PMIC LDO로 바뀌었다. 드라이버 코드 변경은 0줄이다. 바뀌는 것은:

```dts
&ecspi2 {
	imu@0 {
		compatible = "invensense,icm42688";
		reg = <0>;					/* chip select 0 */
		spi-max-frequency = <24000000>;
		interrupt-parent = <&gpio5>;
		interrupts = <3 IRQ_TYPE_EDGE_FALLING>;
		interrupt-names = "INT1";
		vdd-supply = <&ldo4>;				/* PMIC 레귤레이터 노드 */
		vddio-supply = <&reg_1v8>;
	};
};
```

### 14.4 QEMU에서 드라이버 자원 코드를 먼저 검증

이 노트의 실습 방식(virt.dts 사본 + `fixed-clock` + `regulator-fixed` + 기존 GPIO 컨트롤러)은 실제 보드가 오기 전에 **드라이버의 자원 획득·defer·에러 경로를 검증하는 하네스**로 쓸 수 있다. 하드웨어가 없는 자원(reset, DMA)은 optional 경로를, 있는 자원은 실제 경로를 탄다. `devices_deferred`와 `devm` 해제까지 확인할 수 있다는 것이 예제 10·13에서 본 그대로다.

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 데이터시트의 GIC INTID(112)를 DT에 그대로 씀 | 엉뚱한 인터럽트가 오거나 영영 안 옴 | DT SPI 셀은 INTID − 32 | `GIC_SPI (INTID-32)`로 적고 `/proc/interrupts` hwirq로 검증 |
| GIC SPI에 `IRQ_TYPE_EDGE_FALLING`/`LEVEL_LOW` | `request_irq` 실패 또는 타입 설정 실패 | GICv2 `gic_set_type()`이 SPI에 두 타입만 허용 | GPIO irqchip 경유, 또는 센서 쪽 극성 변경 |
| `request_irq`에 `IRQF_TRIGGER_*`를 또 줌 | DT와 다른 트리거로 동작 | flags가 DT 설정을 덮어씀 | flags에서 트리거 비트를 빼고 DT만 신뢰 |
| `GPIO_ACTIVE_LOW` + 드라이버도 0으로 assert | 리셋이 안 걸림 / 늘 리셋 상태 | 이중 반전 | gpiod는 논리값(1 = assert)으로만 생각 |
| `*-supply` 누락 | probe는 성공, 첫 I2C 통신에서 NACK | dummy regulator가 "켜져 있다"고 가정 | `using dummy regulator` 경고를 bring-up 체크리스트에 |
| provider 노드 `disabled`·모듈 미포함 | probe가 조용히 안 끝남, 로그 없음 | `-EPROBE_DEFER` 무한 대기 | `devices_deferred` 확인, provider 활성화 |
| `clocks`와 `clock-names` 순서 불일치 | 에러 없이 다른 클럭을 켬, 주파수 이상 | 이름 → index 매핑이 순서 기반 | 두 목록을 같은 순서로, `clk_summary`의 connection id 확인 |
| pinctrl 그룹 누락 | 인터럽트 카운트 0, GPIO 읽기 값 고정 | 패드가 리셋 기본 기능에 머묾 | `pinctrl-0` 추가, `debug/pinctrl/.../pinmux-pins` 확인 |
| `#xxx-cells`와 인자 개수 불일치 | dtc 경고, `could not get #...-cells`, `-EINVAL` | 범용 파서가 셀 경계를 잘못 읽음 | dtc 경고를 에러처럼 취급 (D7 `dtbs_check`) |
| `reset-gpios`를 리셋 API로 index 요청 (6.18 등 최신 커널) | 원인 모를 `-EPROBE_DEFER` (`resets`) | 리셋 코어의 reset-gpio 폴백 + `reset-gpio.ko` 없음 | 이름으로 요청하거나 GPIO API만 사용 |

---

## 16. 면접에서 이렇게 말한다

**Q.** "What does `interrupts = <0 80 4>` mean in a device tree node?"

**A.** 인터럽트 부모가 GIC라고 가정하면 3셀 지정자다. 첫 셀 0은 SPI, 둘째 80은 SPI 영역 안의 번호라 실제 GIC INTID는 112, 셋째 4는 level-high 트리거다. 부모는 노드나 조상의 `interrupt-parent`가 정하고, 셀 개수는 부모의 `#interrupt-cells`가 정한다. 드라이버는 `platform_get_irq()`로 Linux virq를 받는데, 이는 80도 112도 아닌 별개의 번호다.

> Assuming the interrupt parent is a GIC, it's a three-cell specifier: 0 means SPI, 80 is the SPI-relative number so the hardware INTID is 112, and 4 is level-high. The parent comes from `interrupt-parent` on the node or an ancestor, and the cell count from the parent's `#interrupt-cells`. The driver never sees 80 or 112 — it calls `platform_get_irq()` and gets a Linux virq from the irq domain mapping.

**꼬리 질문**: "센서 INT가 active-low면?" → GIC SPI는 rising/level-high만 받으므로 GPIO 컨트롤러의 irqchip을 `interrupt-parent`로 쓰고 `IRQ_TYPE_EDGE_FALLING`을 준다.

**Q.** "How does a driver get its clocks, regulators and GPIOs from the device tree?"

**A.** 전부 같은 패턴이다. DT에는 `clocks`/`clock-names`, `vdd-supply`, `reset-gpios`처럼 "phandle + 인자"로 provider를 가리키고, 드라이버는 이름으로 `devm_clk_get_enabled(dev, "core")`, `devm_regulator_get_enable(dev, "vdd")`, `devm_gpiod_get(dev, "reset", ...)`을 부른다. 각 프레임워크가 phandle로 provider를 찾고, provider의 xlate가 인자를 해석해서 핸들을 준다. devm이라 실패나 언바인드 때 자동 해제된다.

> They all follow one pattern: the node references a provider by phandle plus arguments — `clocks` with `clock-names`, `vdd-supply`, `reset-gpios` — and the driver asks by name: `devm_clk_get_enabled(dev, "core")`, `devm_regulator_get_enable(dev, "vdd")`, `devm_gpiod_get(dev, "reset", ...)`. Each framework resolves the phandle to a registered provider and lets the provider's xlate interpret the argument cells. Using devm means everything is released automatically on probe failure or unbind.

**Q.** "What is `-EPROBE_DEFER` and how do you debug a driver stuck in it?"

**A.** probe 중 필요한 provider(클럭, 레귤레이터, GPIO, irq domain 등)가 아직 등록 전이라 "나중에 다시 해 달라"는 반환값이다. 드라이버 코어가 지연 목록에 넣고 다른 드라이버가 bind될 때마다 재시도한다. 디버깅은 `/sys/kernel/debug/devices_deferred`에서 사유를 보는 것이 첫 단계다. `dev_err_probe()`를 쓰면 사유가 거기 기록된다. 흔한 원인은 provider 노드가 disabled이거나 그 드라이버가 모듈인데 initramfs에 없는 경우다.

> It means a resource provider — a clock, regulator, GPIO controller or irq domain — isn't registered yet, so the driver asks to be retried later. The driver core keeps it on a deferred list and retries whenever another driver binds. To debug, I read `/sys/kernel/debug/devices_deferred`, which shows the reason recorded by `dev_err_probe()`. Typical causes are a disabled provider node or a provider driver built as a module that isn't in the initramfs.

**Q.** "What happens if a node has no `vdd-supply` but the driver requests `vdd`?"

**A.** 일반 `regulator_get` 계열이면 DT 기반 시스템에서 레귤레이터 코어가 dummy regulator를 주고 "supply vdd not found, using dummy regulator" 경고만 남긴다. probe는 성공하므로, 실제로 전원이 꺼져 있으면 나중에 통신 오류로 나타난다. 선택적 전원이면 `regulator_get_optional`을 써서 `-ENODEV`를 받고 분기한다.

> With a normal get, the regulator core on a DT system hands back a dummy regulator and logs "supply vdd not found, using dummy regulator". Probe succeeds, so if the rail is really off you only find out later as bus errors. For truly optional supplies I use `regulator_get_optional()`, which returns `-ENODEV` instead.

**Q.** "Explain `GPIO_ACTIVE_LOW` and how the driver should drive a reset pin."

**A.** 플래그는 보드 배선의 극성을 DT에 적는 것이다. gpiolib이 논리값을 뒤집어 주므로, 드라이버는 항상 "1 = assert"로 쓴다. 리셋은 `GPIOD_OUT_HIGH`로 요청해 assert 상태로 시작하고, 대기 후 `gpiod_set_value_cansleep(rst, 0)`으로 해제한다. 실제로 확인해 보면 logical 1일 때 raw 0이다. 드라이버가 극성을 또 처리하면 이중 반전 버그가 된다.

> The flag records board polarity in DT, and gpiolib inverts the logical value for you, so the driver always thinks "1 means asserted". I request the reset with `GPIOD_OUT_HIGH` so it starts asserted, wait, then call `gpiod_set_value_cansleep(rst, 0)` to release it — logical 1 shows as raw 0 on the pin. If the driver also inverts, you get a double-inversion bug.

**Q.** "Where does pinmux get applied for a device, and does the driver have to do it?"

**A.** `pinctrl-names`와 `pinctrl-N`에 상태별 그룹을 적으면, 드라이버 코어가 probe 직전에 `pinctrl_bind_pins()`로 `"default"` 상태를 자동 적용한다. 드라이버는 보통 아무것도 안 한다. suspend 때 `"sleep"` 상태로 바꾸거나 I2C 버스 복구처럼 런타임에 상태를 바꿔야 할 때만 `pinctrl_select_state()`를 직접 부른다.

> The node lists states in `pinctrl-names` and the groups for each in `pinctrl-0`, `pinctrl-1` and so on. The driver core applies the "default" state just before probe via `pinctrl_bind_pins()`, so most drivers do nothing. A driver only calls `pinctrl_select_state()` itself for things like a "sleep" state on suspend or switching I2C pins to GPIO for bus recovery.

**Q.** "What's the general mechanism behind `clocks`, `resets`, `dmas` and `*-gpios`?"

**A.** phandle + 인자 셀이다. 인자 개수는 provider 노드의 `#clock-cells`, `#reset-cells`, `#dma-cells`, `#gpio-cells`가 정하고, 커널은 `of_parse_phandle_with_args()` 하나로 파싱한다. 각 프레임워크는 그 결과(provider 노드 + 인자 배열)를 자기 provider 목록에서 찾아 xlate 콜백에 넘긴다. 그래서 새 provider 종류가 생겨도 문법은 같다.

> It's phandle plus argument cells. The provider declares how many cells follow via `#clock-cells`, `#reset-cells`, `#dma-cells` or `#gpio-cells`, and the kernel parses all of them with `of_parse_phandle_with_args()`. Each subsystem then looks up the provider node in its own registry and passes the argument array to the provider's xlate callback. That's why a new kind of provider doesn't need new syntax.

**꼬리 질문**: "`*-supply`와 `pinctrl-0`은 왜 인자가 없나?" → 레귤레이터와 pinctrl 그룹은 노드 하나가 자원 하나라서 인자가 필요 없다.

---

## 17. 직접 해보기

1. **개념**: `interrupts = <1 9 0xf04>`(GICv2)를 해독하라. INTID와 CPU 마스크, 트리거는?
   정답/힌트: PPI 9 → INTID 25, 비트[15:8] = 0xf → CPU 0~3, 비트[3:0] = 4 → level-high. `GIC_CPU_MASK_SIMPLE(4) | IRQ_TYPE_LEVEL_HIGH`와 같다.
2. **관찰**: 랩 게스트에서 `/proc/device-tree/timer/interrupts`를 `hexdump`로 읽고, 4개 지정자를 `/proc/interrupts`의 `arch_timer` hwirq와 대조하라. 어느 것이 실제로 쓰였나?
   정답/힌트: 셀 그룹 `1 0xd 0x104`, `1 0xe 0x104`, `1 0xb 0x104`, `1 0xa 0x104` → INTID 29, 30, 27, 26. 이 게스트에서는 27(virtual timer)이 보인다(예제 7).
3. **코드**: `d4_res`에 `interrupt-names = "data"`를 추가하고 `platform_get_irq_byname(pdev, "data")`로 바꿔 보라. 이름을 틀리게 쓰면 어떤 로그가 나오는가?
   정답/힌트: 6.18 `platform_get_irq_byname()`은 못 찾으면 `dev_err_probe(..., "IRQ %s not found\n", name)`으로 `IRQ data not found` 형태의 로그를 남기고 음수를 돌려준다. 직접 돌려서 확인할 것.
4. **코드**: `d4-vdd`에 `gpio = <&gpio 6 GPIO_ACTIVE_HIGH>; enable-active-high; startup-delay-us = <5000>;`를 추가해 부팅하라. `regulator_summary`의 use 값과 `debug/gpio`는 어떻게 바뀌는가?
   정답/힌트: enable GPIO가 생기면 always-on 취급이 사라져 use가 consumer 몫(1)만 남을 것이다. `debug/gpio`에 6번 핀이 레귤레이터 소유로 나타난다. 실제로 돌려서 확인할 것.
5. **관찰**: `-DD4_DEFER` dtb로 부팅한 뒤 셸에서 `echo okay`로 노드 상태를 바꿀 수는 없다. 대신 무엇을 하면 지연이 풀리는가? (힌트: D6 overlay, 또는 provider 모듈 로드)
   정답/힌트: provider가 등록되는 순간 지연 목록이 재시도된다. overlay로 `status = "okay"`를 적용하면 레귤레이터 장치가 생기고 `d4_res`가 probe된다.
6. **설계**: §12 보드에서 VDD를 PMIC(PCA9450) LDO로 바꾸면 dts의 어느 줄이 어떻게 바뀌고, 드라이버는 몇 줄 바뀌는가?
   정답/힌트: `vdd-supply = <&ldoN>` 한 줄(그리고 `reg_imu_vdd` 노드 삭제), 드라이버 0줄. PMIC 드라이버가 늦게 뜨면 그동안 IMU는 defer된다.
7. **관찰**: `rst_byidx=1` 실험(예제 10)에서 `reset-gpio.ko`를 initramfs에 넣고 먼저 `insmod`하면 무슨 일이 생길지 예측하고 확인하라.
   정답/힌트: reset-gpio 장치가 GPIO 5번을 잡으려 하는데 `d4_res`가 이미 `devm_gpiod_get`으로 잡고 있다. 한 핀을 두 API로 다루는 문제를 직접 보게 된다. 결과는 직접 확인.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| provider | 자원 제공 노드·드라이버 | GIC, 클럭 컨트롤러, 레귤레이터, GPIO 컨트롤러, DMA 컨트롤러 |
| consumer | 자원 사용 노드·드라이버 | 자원 속성으로 provider를 가리키는 장치 |
| phandle | 노드 참조 번호 | dtc가 노드마다 붙이는 32비트 ID, `&label`로 씀 |
| specifier | 지정자 | phandle 뒤에 오는 인자 셀 묶음 |
| `#xxx-cells` | 인자 셀 개수 선언 | provider가 "나를 가리키면 셀 N개가 따라온다"고 선언 |
| xlate | translate 콜백 | provider 드라이버가 인자 셀을 내부 번호로 바꾸는 함수 |
| SPI / PPI / SGI | GIC 인터럽트 종류 | 공유 주변장치 / 코어 전용 / 소프트웨어 생성 |
| INTID / hwirq | 하드웨어 인터럽트 번호 | GIC 기준 번호, SPI는 32부터 |
| virq | Linux IRQ 번호 | `irq_desc` 인덱스, `request_irq`에 쓰는 번호 |
| irq domain | hwirq ↔ virq 변환표 | 인터럽트 컨트롤러마다 하나 |
| `interrupt-parent` | 인터럽트 부모 | 없으면 조상에서 상속 |
| `interrupts-extended` | 항목별 부모 지정 | 인터럽트마다 다른 컨트롤러 |
| CCF | Common Clock Framework | 커널 클럭 트리 프레임워크 (`clk_summary`) |
| `assigned-clocks` | 클럭 기본값 | probe 전에 부모·주파수를 맞춤 |
| dummy regulator | 가짜 전원 | `*-supply`가 없을 때 주는 항상 켜진 레귤레이터 |
| active-low | 낮은 레벨이 활성 | `GPIO_ACTIVE_LOW`, gpiolib이 논리값을 뒤집음 |
| pin state | 핀 상태 | `default`/`init`/`idle`/`sleep` 이름의 mux+설정 묶음 |
| `-EPROBE_DEFER` | probe 지연 | provider가 아직 없음 → 나중에 재시도 |
| fw_devlink | 펌웨어 기반 device link | DT 자원 속성으로 supplier/consumer 순서를 미리 계산 |
| devm | managed 자원 | 장치 해제 시 자동 반납되는 자원 |

---

## 19. 요약 & 체크리스트

DT 노드의 자원 속성은 `reg`(주소 값) 하나를 빼면 전부 **"phandle + 인자 셀"**이라는 한 문법이고, 인자 개수는 provider의 `#xxx-cells`, 해석은 provider 드라이버의 xlate가 한다. 드라이버는 숫자를 모른 채 **이름으로 요청**하고(`platform_get_irq`, `devm_clk_get_enabled(dev, "core")`, `devm_regulator_get_enable(dev, "vdd")`, `devm_gpiod_get(dev, "reset", ...)`, `dma_request_chan(dev, "rx")`), pinctrl `default`와 `assigned-clocks`는 코어가 probe 직전에 대신 적용한다. GIC 지정자는 SPI 번호에 32를 더해야 INTID가 되고 SPI는 rising/level-high만 받는다. 실패 반응은 세 가지 — 문법 오류는 영구 실패, provider 부재는 `-EPROBE_DEFER`(`devices_deferred`), 전원 속성 부재는 dummy — 이고, `/proc/interrupts`·`clk_summary`·`regulator_summary`·`debug/gpio`·`/proc/iomem`이 각각의 증거를 보여 준다.

- [ ] `reg`가 다른 자원 속성과 왜 다른지(주소 값 vs phandle) 설명할 수 있다
- [ ] phandle + `#xxx-cells` 패턴을 그림으로 그리고 `of_parse_phandle_with_args()`의 역할을 말할 수 있다
- [ ] GIC 3셀 지정자를 해독하고 SPI +32, PPI +16, CPU 마스크, SPI 트리거 제한을 설명할 수 있다
- [ ] `interrupt-parent` 상속과 `interrupts-extended`의 차이를 설명할 수 있다
- [ ] virq·hwirq·DT 번호를 `/proc/interrupts`와 `/sys/kernel/irq`로 구분해 보일 수 있다
- [ ] `clocks`/`clock-names`/`assigned-clocks`를 쓰고 `clk_summary`로 검증할 수 있다
- [ ] `*-supply`가 없을 때(dummy)와 provider가 없을 때(defer)의 차이를 설명하고 재현할 수 있다
- [ ] `GPIO_ACTIVE_LOW`에서 논리값과 물리값의 관계를 설명하고 이중 반전 버그를 피할 수 있다
- [ ] pinctrl `default` 상태가 누가 언제 적용하는지 말할 수 있다
- [ ] `dmas`/`dma-names`와 `-ENODEV`/`-EPROBE_DEFER` 처리 차이를 설명할 수 있다
- [ ] I2C IMU 노드(INT on GPIO, vdd/vddio, reset-gpios, pinctrl)를 처음부터 쓰고 dtc로 검증할 수 있다
- [ ] `devices_deferred`·`dmesg`·debugfs로 "probe가 안 된다"를 단계적으로 진단할 수 있다

---

## 참고 자료

- Devicetree Specification (devicetree.org) — 2장 "Standard Properties"(`reg`, `interrupt-parent`), 2.4 "Interrupts and Interrupt Mapping": https://www.devicetree.org/specifications/
- 커널 문서 — Linux and the Devicetree (`Documentation/devicetree/usage-model.rst`): https://docs.kernel.org/devicetree/usage-model.html
- 커널 바인딩 — `Documentation/devicetree/bindings/interrupt-controller/arm,gic.yaml`, `bindings/gpio/gpio.txt`, `bindings/clock/`, `bindings/regulator/fixed-regulator.yaml`, `bindings/pinctrl/pinctrl-bindings.txt`, `bindings/dma/dma-common.yaml`, `bindings/iio/imu/invensense,icm42600.yaml` (6.18 소스 트리)
- 커널 문서 — GPIO Descriptor Consumer Interface: https://docs.kernel.org/driver-api/gpio/consumer.html
- 커널 문서 — Voltage and current regulator API: https://docs.kernel.org/driver-api/regulator.html
- 커널 문서 — The Common Clk Framework: https://docs.kernel.org/driver-api/clk.html
- 커널 문서 — PINCTRL subsystem: https://docs.kernel.org/driver-api/pin-control.html
- 커널 문서 — DMAengine client documentation: https://docs.kernel.org/driver-api/dmaengine/client.html
- 커널 소스(elixir.bootlin.com, v6.18) — `drivers/of/irq.c`(`of_irq_parse_one`, `of_irq_find_parent`), `drivers/irqchip/irq-gic.c`(`gic_irq_domain_translate`, `gic_set_type`), `drivers/regulator/core.c`(`_regulator_get_common`), `drivers/reset/core.c`(`__of_reset_control_get`), `drivers/gpio/gpiolib-of.c`, `drivers/base/dd.c`(`really_probe`), `drivers/base/platform.c`(`platform_probe`)
- Bootlin — "Linux kernel and driver development" 교육 슬라이드 (Device Tree, 자원 관리 장): https://bootlin.com/training/kernel/
- `include/dt-bindings/interrupt-controller/arm-gic.h`, `include/dt-bindings/gpio/gpio.h`, `arch/arm64/boot/dts/freescale/imx8mm.dtsi`, `imx8mm-pinfunc.h`
