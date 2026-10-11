# D1. 왜 Device Tree인가 — 물어볼 수 없는 하드웨어를 커널에 알려 주는 데이터

> **이 노트를 다 읽으면**: discoverable 버스(PCIe·USB)와 non-discoverable 장치(MMIO UART·I2C 센서·GPIO·IRQ 배선)의 차이로 "DT가 왜 필요한가"를 30초 안에 설명할 수 있다 · board file(`arch/arm/mach-*`, C 코드 속 `platform_device`) 시대가 왜 무너졌고 DT가 "커널 하나, 보드 여러 개"를 어떻게 가능하게 했는지 말할 수 있다 · `.dts → dtc → .dtb → 부트로더 → 커널(unflatten) → platform_device → driver` 경로를 QEMU 랩에서 직접 따라가며 `/proc/device-tree`, `of_node`·`driver` 심볼릭 링크, uevent로 보여 줄 수 있다 · DT가 아닌 것(코드, 정책)과 ACPI와의 차이를 말할 수 있다
> **왜 필요한가**: Embedded Linux 면접의 단골 첫 질문이 "Device Tree가 뭐고 왜 쓰나요?"이고, 실무 첫 주에 만나는 "커널은 떴는데 센서(UART·I2C)가 안 보인다"의 절반은 DT 문제다
> **Don 기준 난이도**: PCIe enumeration·config space·BAR, MMIO 레지스터 맵, 인터럽트 배선은 이미 몸에 붙어 있다 / 새로 배울 부분은 "그 정보를 커널 바이너리 밖의 데이터로 빼서 부팅 때 넘긴다"는 발상, 그리고 그 데이터가 커널 안에서 디바이스 모델(`device_node` → `platform_device` → driver)로 바뀌는 과정
> **선행 노트**: C1(부팅 흐름 개관), C3(U-Boot) — 없어도 읽을 수 있다. D2(문법)·D3(드라이버 매칭)의 입구다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 컨트롤러 펌웨어를 짤 때를 떠올려 보자. NAND 채널 컨트롤러가 `0x4000_0000`에, UART가 `0x5000_1000`에, 타이머 인터럽트가 NVIC 15번에 있다는 사실은 **헤더 파일에 `#define`으로 박혀 있었다.** 칩이 바뀌면 헤더를 바꾸고 다시 빌드한다. 펌웨어는 그 칩 하나만 위해 존재하니까 아무 문제가 없다.

Linux 커널은 사정이 다르다. 같은 `Image` 하나가 i.MX8MP 보드에서도, TI AM62 보드에서도, Raspberry Pi에서도, QEMU에서도 떠야 한다. 그런데 커널이 부팅하자마자 알아야 하는 것들 — "RAM은 어디부터 얼마나 있나", "콘솔 UART는 어느 주소인가", "인터럽트 컨트롤러는 GICv2인가 v3인가", "I2C 버스 3번의 0x68에 IMU가 붙어 있고 그 인터럽트는 GPIO 1-12에 물려 있다" — 이 정보 대부분은 **하드웨어에 물어볼 방법이 없다.**

반면 PCIe는 물어볼 수 있다. Don이 매일 보던 그 config space다. 루트 컴플렉스가 Bus/Device/Function을 하나씩 읽어 보면 Vendor ID가 `0xFFFF`가 아닌 곳에 장치가 있고, 장치가 직접 "나는 8086:100e, class 0200(이더넷), BAR0는 128KB 필요"라고 답한다. 이게 **discoverable** 버스다. SoC 안의 UART나 I2C에 매달린 센서는 그런 표준 질의 방법이 없다. 이게 **non-discoverable** 장치다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="30" width="320" height="285" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b"/>
  <text x="170" y="52" text-anchor="middle" font-size="13" font-weight="bold">Discoverable — PCIe / USB</text>
  <rect x="90" y="68" width="160" height="32" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="89" text-anchor="middle" font-size="13">CPU + Root Complex</text>
  <line x1="170" y1="100" x2="170" y2="146" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="178" y="128" font-size="12">config read (B/D/F)</text>
  <line x1="40" y1="150" x2="300" y2="150" stroke="#3f9a6b" stroke-width="2"/>
  <line x1="72" y1="150" x2="72" y2="170" stroke="#3f9a6b"/>
  <line x1="172" y1="150" x2="172" y2="170" stroke="#3f9a6b"/>
  <line x1="272" y1="150" x2="272" y2="170" stroke="#3f9a6b"/>
  <rect x="25" y="170" width="95" height="48" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="72" y="190" text-anchor="middle" font-size="12">00:01.0</text>
  <text x="72" y="208" text-anchor="middle" font-size="12">1af4:1000</text>
  <rect x="125" y="170" width="95" height="48" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="172" y="190" text-anchor="middle" font-size="12">00:02.0</text>
  <text x="172" y="208" text-anchor="middle" font-size="12">8086:100e</text>
  <rect x="225" y="170" width="95" height="48" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="272" y="190" text-anchor="middle" font-size="12">00:03.0</text>
  <text x="272" y="208" text-anchor="middle" font-size="12">1af4:1005</text>
  <text x="25" y="246" font-size="12">장치가 스스로 답한다:</text>
  <text x="25" y="266" font-size="12">Vendor/Device ID, class, BAR 크기, MSI</text>
  <text x="25" y="290" font-size="12">→ 커널이 순회(enumeration)로 찾는다</text>
  <rect x="350" y="30" width="320" height="285" rx="6" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a"/>
  <text x="510" y="52" text-anchor="middle" font-size="13" font-weight="bold">Non-discoverable — SoC 안·보드 위</text>
  <rect x="440" y="68" width="140" height="32" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="89" text-anchor="middle" font-size="13">CPU (Cortex-A)</text>
  <line x1="510" y1="100" x2="510" y2="146" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="518" y="128" font-size="12">load/store (MMIO)</text>
  <line x1="365" y1="150" x2="655" y2="150" stroke="#d0564a" stroke-width="2"/>
  <line x1="408" y1="150" x2="408" y2="170" stroke="#d0564a"/>
  <line x1="510" y1="150" x2="510" y2="170" stroke="#d0564a"/>
  <line x1="612" y1="150" x2="612" y2="170" stroke="#d0564a"/>
  <rect x="362" y="170" width="92" height="48" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="408" y="190" text-anchor="middle" font-size="12">UART</text>
  <text x="408" y="208" text-anchor="middle" font-size="12">주소 = ?</text>
  <rect x="464" y="170" width="92" height="48" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="190" text-anchor="middle" font-size="12">I2C ctrl</text>
  <text x="510" y="208" text-anchor="middle" font-size="12">주소 = ?</text>
  <rect x="566" y="170" width="92" height="48" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="612" y="190" text-anchor="middle" font-size="12">GPIO</text>
  <text x="612" y="208" text-anchor="middle" font-size="12">주소 = ?</text>
  <line x1="510" y1="218" x2="510" y2="236" stroke="#d0564a" stroke-dasharray="4 3"/>
  <rect x="464" y="236" width="92" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="255" text-anchor="middle" font-size="12">IMU @0x68 ?</text>
  <line x1="556" y1="250" x2="612" y2="250" stroke="#d0564a" stroke-dasharray="4 3"/>
  <line x1="612" y1="250" x2="612" y2="218" stroke="#d0564a" stroke-dasharray="4 3"/>
  <text x="600" y="270" font-size="12">IRQ 배선 ?</text>
  <text x="362" y="298" font-size="12">물어볼 표준 방법이 없다 → 누군가 알려 줘야 한다</text>
  <text x="340" y="345" text-anchor="middle" font-size="12">단, PCIe host bridge 자체(ECAM 주소·IRQ 매핑)는 non-discoverable — DT에 pcie 노드 하나가 있다</text>
</svg>
```
그림 1 — 왼쪽은 장치가 스스로 정체를 밝히는 버스, 오른쪽은 누군가 "어디에 무엇이 어떻게 붙어 있다"를 알려 줘야 하는 장치들. 왼쪽 숫자는 이 노트 §7에서 QEMU 게스트가 실제로 enumeration한 값이다.

"누군가 알려 줘야 한다"의 답이 시대마다 달랐다.

- **2011년 무렵까지의 ARM**: 커널 소스 안의 **board file**(`arch/arm/mach-omap1/board-osk.c` 같은 C 파일)에 `struct platform_device`와 `struct resource`로 주소·IRQ를 하드코딩했다. 보드마다 C 파일 하나, 그리고 대개 보드마다 다른 커널 빌드.
- **지금의 ARM/arm64/RISC-V**: 하드웨어 설명을 커널 밖의 **데이터 파일**(Device Tree)로 뺐다. 부트로더가 그 데이터의 메모리 주소를 커널에 넘기고(arm64는 레지스터 `x0`), 커널은 그걸 읽어 디바이스를 만든다. 커널 바이너리는 보드를 몰라도 된다.
- **x86 PC·서버, 일부 arm64 서버**: 펌웨어(UEFI)가 **ACPI** 테이블로 알려 준다 (§9, C8).

커널 문서 `Documentation/devicetree/usage-model.rst`(Grant Likely 작성)는 Linux가 DT를 쓰는 목적을 세 가지로 정리한다: **(1) platform identification**(어떤 기계인지 — 루트의 `compatible`), **(2) runtime configuration**(`/chosen`의 `bootargs`, initrd 위치 등), **(3) device population**(디바이스를 만들어 드라이버와 짝지어 주기). 이 노트는 세 가지를 모두 랩에서 확인한다.

Embedded Linux 엔지니어가 이 그림을 머릿속에 갖고 있어야 하는 장면:

- **새 보드 bring-up**: 벤더 EVK의 `.dts`를 복사해 우리 보드(센서 키트)의 배선대로 고친다. 커널 C 코드는 거의 손대지 않는다. Don이 Apple에서 새 실리콘 bring-up 때 하던 "이 블록은 어느 주소, 어느 IRQ"를 정리하는 일이 그대로 `.dts` 작성이다.
- **"커널은 떴는데 장치가 없다"**: `/sys/bus/i2c/devices`에 IMU가 없다. 드라이버가 없는 걸까, DT 노드가 없는 걸까, `status = "disabled"`일까, `compatible` 오타일까? §6, §10의 도구로 5분 안에 가른다.
- **면접**: "DT가 뭐고 왜 쓰나요?", "PCI 장치는 왜 DT에 안 적나요?", "ACPI랑 뭐가 달라요?" — §15에 답이 있다.

> 실행 환경: 모든 출력은 실제 실행 결과다. 두 곳에서 돌렸다. (1) Lima VM `elinux`(aarch64, `uname -r` = `7.0.0-38-generic`, Apple Virtualization 위 — **ACPI로 부팅**하는 머신이라 §9의 대조군이 된다). (2) 그 VM 안의 공용 QEMU 랩: `qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M`, 커널 **Linux 6.18.55** LTS(`/var/lab/out/Image`), BusyBox initramfs, DT는 QEMU가 만든 `virt` 머신 DT(`/var/lab/out/virt.dts`). 게스트 명령은 랩 initramfs의 `LAB_CMD=`로 실행했고, `ls`의 색상 escape 코드만 지웠다. 타임스탬프·KASLR·주소는 실행마다 다르다.

---

## 1. Discoverable 버스 — PCIe와 USB는 스스로 말한다

### 1.1 직관 — "전화번호부가 아니라 호출 응답"

Don에게 PCIe enumeration은 설명할 필요가 없지만, "왜 DT가 필요 없나"의 관점으로 다시 보자. 커널(또는 펌웨어)은 다음만 알면 된다.

1. **config space에 접근하는 방법** — ECAM 영역의 물리 주소. `addr = ECAM_base + (bus << 20 | dev << 15 | fn << 12) + offset`.
2. 그다음은 **전부 장치가 답한다**: offset 0x00의 Vendor/Device ID, 0x08의 Class Code, 0x0E의 Header Type, BAR에 `0xFFFFFFFF`를 써서 읽는 크기, Capability 리스트(MSI/MSI-X, PCIe Cap) …

그래서 PCI 드라이버는 "내가 다룰 장치는 `8086:100e`"라고 ID 테이블(`struct pci_device_id`)만 선언하면 되고, 어떤 주소에 있을지는 enumeration이 정한 BAR에서 받아 간다. **장치의 위치·자원은 런타임에 발견된다.**

USB도 같다. 호스트가 새 장치를 address 0에서 만나 `GET_DESCRIPTOR`를 보내면 장치가 Device Descriptor(idVendor, idProduct, bDeviceClass), Configuration/Interface/Endpoint Descriptor를 돌려준다. 커널은 이 값으로 `usb_device_id` 테이블과 짝짓는다. 핫플러그까지 된다.

### 1.2 그런데 "발견"에도 출발점이 필요하다

여기서 Don이 놓치기 쉬운 점이 있다. **PCIe Root Complex 자체는 discoverable이 아니다.** ECAM이 물리 주소 어디에 있는지, 레거시 INTx가 GIC의 몇 번 SPI로 연결되는지, MSI는 어느 컨트롤러로 가는지, CPU 주소 공간의 어느 창(window)을 PCI 메모리 공간으로 쓸지 — 이건 SoC 설계가 정한 것이고 PCIe 프로토콜로 물어볼 수 없다. 그래서 랩의 DT에도 PCIe 장치는 하나도 없지만 **host bridge 노드 `pcie@10000000`은 있다** (§4.1, §7). USB도 마찬가지로 xHCI 컨트롤러 자체(MMIO 주소, IRQ)는 SoC에 박힌 non-discoverable 장치다.

| 질문 | PCIe 엔드포인트 | PCIe host bridge | USB 장치 | SoC UART / I2C 컨트롤러 | I2C 위의 IMU |
|---|---|---|---|---|---|
| 정체(ID)를 물어볼 수 있나 | 예 (Vendor/Device ID) | 아니오 | 예 (descriptor) | 아니오 (AMBA PrimeCell은 예외적으로 ID 레지스터가 있다, §2.3) | 대개 WHO_AM_I 레지스터가 있지만 **주소를 알아야** 읽는다 |
| 주소(자원)를 물어볼 수 있나 | 예 (BAR 크기 → OS가 배치) | 아니오 (ECAM·window) | 예 (주소는 호스트가 부여) | 아니오 | 아니오 (7비트 주소는 핀 strap) |
| 인터럽트 | MSI/MSI-X capability 또는 INTx swizzle | 아니오 (`interrupt-map`) | 호스트 컨트롤러 경유 | 아니오 (GIC SPI 번호) | 아니오 (어느 GPIO에 물렸나) |
| 누가 알려 주나 | 장치 자신 | DT / ACPI | 장치 자신 | DT / ACPI / board file | DT / ACPI / board file |

### 1.3 무엇을 알려 줘야 하나 — 드라이버가 `probe()`에서 필요한 것

센서 키트의 IMU 드라이버가 일하려면 다음을 알아야 한다. 이 목록이 곧 DT 노드에 적히는 속성 목록이다 (자세한 문법은 D2, 자원 기술은 D4).

- **무엇인가**: `compatible = "st,lsm6dso"` — 어떤 드라이버를 붙일지.
- **어디 있나**: MMIO 장치라면 `reg = <주소 크기>`, I2C 장치라면 `reg = <0x6a>`(버스 위 주소). 그리고 **어느 버스 밑에 있나**(트리 구조 자체).
- **인터럽트**: `interrupts`/`interrupt-parent` — "GPIO1의 12번 핀, falling edge".
- **클록·리셋·전원**: `clocks`, `resets`, `vdd-supply` — Don이 bring-up 때 "클록 게이트 열고, 리셋 풀고, 레일 올리고"를 하던 바로 그 순서의 재료.
- **핀**: `pinctrl-0` — 그 패드를 I2C 기능으로 mux.
- **보드 고유 사실**: 센서 장착 방향(`mount-matrix`), 크리스털 주파수(`clock-frequency`) 같은 것.

이것들은 **보드마다 다르고, 하드웨어에 물어볼 수 없고, 드라이버 코드가 알 수 없다.** 이 세 조건이 모두 참인 정보를 담는 그릇이 DT다.

---

## 2. Non-discoverable 장치 — 왜 "그냥 찔러 보기"가 안 되나

### 2.1 MMIO UART — 없는 주소를 읽으면?

"주소를 모르면 후보 주소를 다 읽어 보면 되지 않나?" bare-metal 감각으로는 그럴듯하다. 하지만:

- **없는 주소를 읽으면 bus error**가 난다. AXI 인터커넥트가 DECERR/SLVERR를 돌려주면 Cortex-A는 동기/비동기 External Abort(arm64에서는 SError)로 커널을 죽인다. Don이 SSD 펌웨어에서 잘못된 주소를 찔러 Data Abort를 봤던 것과 같다.
- **있는 주소라도 읽기에 부작용**이 있다. UART의 수신 FIFO(DR)를 읽으면 문자 하나가 사라지고, 많은 인터럽트 상태 레지스터는 read-to-clear다.
- **클록이 꺼진 블록**은 접근하면 버스가 멈출(hang) 수 있다. 클록·전원 도메인을 먼저 켜야 하는데, 어떤 클록인지는 또 물어볼 수 없다.

### 2.2 I2C 센서 — 스캔은 "추측"이다

`i2cdetect`로 버스를 스캔하면 ACK하는 주소는 보인다. 하지만 이건 증거가 아니라 추측이다.

- 0x68에서 ACK가 오면 그게 MPU-6050인지, DS1307 RTC인지 모른다 (둘 다 0x68을 쓴다).
- 일부 칩은 "읽기 시도"만으로도 상태가 바뀐다. 그래서 `i2cdetect`도 기본 동작에서 특정 주소 범위는 조심스럽게 다루고 경고 문구를 띄운다.
- 인터럽트 핀이 어느 GPIO에 물렸는지, 리셋 핀은 어디인지, 레귤레이터는 무엇인지 — 버스 스캔으로는 절대 알 수 없다. **PCB 회로도에만 있는 사실**이다.

### 2.3 AMBA PrimeCell — "반쯤 discoverable"한 예외

랩의 UART(PL011), RTC(PL031), GPIO(PL061)는 ARM의 **PrimeCell** IP다. 이 블록들은 레지스터 맵의 끝(오프셋 0xFE0~0xFFC)에 **Peripheral ID / PrimeCell ID 레지스터**가 있어서, 주소만 알면 "나는 PL011 rev1"이라고 답한다. 그래서 Linux에는 `amba` 버스가 따로 있고, 드라이버는 compatible 문자열이 아니라 이 ID로 매칭된다. §6에서 보듯 `9000000.pl011`은 `/sys/bus/platform`이 아니라 `/sys/bus/amba`에 있고, uevent에 `AMBA_ID=00141011`이 찍힌다.

그래도 **주소는 알려 줘야 한다.** DT 노드의 `compatible = "arm,pl011", "arm,primecell"` 중 `"arm,primecell"`이 커널에게 "이건 platform device 말고 amba device로 만들어라"라고 알려 주는 표시다 (`drivers/of/platform.c`의 `of_platform_bus_create()`에서 `of_device_is_compatible(bus, "arm,primecell")`이면 `of_amba_device_create()`로 간다 — §12에서 소스로 본다). 정체는 물어볼 수 있어도 위치는 물어볼 수 없다 — non-discoverable의 정의가 "정체를 모른다"보다 **"어디 있는지·어떻게 배선됐는지 모른다"**에 가깝다는 좋은 예다.

### 2.4 bare-metal과 연결

| bare-metal 펌웨어 (Don의 SSD) | Linux + DT |
|---|---|
| `#define UART0_BASE 0x50001000` (헤더) | `reg = <0x0 0x9000000 0x0 0x1000>` (DT) |
| `NVIC_EnableIRQ(UART0_IRQn)` — 번호는 헤더 | `interrupts = <GIC_SPI 1 IRQ_TYPE_LEVEL_HIGH>` — 커널이 virq로 매핑 |
| 보드 초기화 함수 `board_init()`가 핀 mux·클록 설정 | `pinctrl-0`, `clocks` 속성 → 드라이버 `probe()`에서 프레임워크가 처리 |
| 칩 바뀌면 헤더 바꾸고 재빌드 | 보드 바뀌면 `.dtb`만 바꾼다 — 커널 그대로 |
| 장치 목록 = 펌웨어 코드에 하드코딩된 init 순서 | 장치 목록 = DT 노드 → 커널이 `platform_device` 생성 → 드라이버 매칭 |

---

## 3. DT 이전의 ARM — board file 시대

### 3.1 board file이란 — 예제 1: 지금도 남아 있는 board file 읽기

무엇을 확인하나: 6.18 커널 트리에 아직 남아 있는 옛 방식 board file을 직접 열어, 하드웨어 설명이 **C 코드**로 어떻게 적혔는지 본다.

```sh
cd /var/lab/src/linux-6.18.55/arch/arm/mach-omap1
sed -n 130,149p board-osk.c        # 이더넷 칩 하나를 설명하는 C 구조체
sed -n 444,454p board-osk.c        # 이 보드를 식별하는 machine descriptor
```

```text
static struct resource osk5912_smc91x_resources[] = {
	[0] = {
		.start	= OMAP_OSK_ETHR_START,		/* Physical */
		.end	= OMAP_OSK_ETHR_START + 0xf,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static struct platform_device osk5912_smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.dev	= {
		.platform_data	= &osk5912_smc91x_info,
	},
	.num_resources	= ARRAY_SIZE(osk5912_smc91x_resources),
	.resource	= osk5912_smc91x_resources,
};
MACHINE_START(OMAP_OSK, "TI-OSK")
	/* Maintainer: Dirk Behme <dirk.behme@de.bosch.com> */
	.atag_offset	= 0x100,
	.map_io		= omap1_map_io,
	.init_early	= omap1_init_early,
	.init_irq	= omap1_init_irq,
	.init_machine	= osk_init,
	.init_late	= omap1_init_late,
	.init_time	= omap1_timer_init,
	.restart	= omap1_restart,
MACHINE_END
```

출력에서 볼 것: TI OSK5912 보드의 SMC91x 이더넷 칩 위치(`OMAP_OSK_ETHR_START`)와 IRQ가 **C 구조체**로 적혀 있고, `osk_init()` 안에서 `platform_add_devices()`로 커널에 등록된다(같은 파일 422행). 부트로더는 "나는 OSK 보드다"라는 **machine type 번호**를 레지스터 `r1`에 넣어 넘겼고(ARM 32비트 ATAGS 부팅 규약), 커널은 이 번호로 `MACHINE_START(OMAP_OSK, ...)` 블록을 골랐다. 보드 하나 = C 파일 하나 = `MACHINE_START` 하나.

`struct platform_device`와 `struct resource`는 지금도 쓰는 구조체다. 달라진 것은 **누가 이걸 만드느냐**뿐이다. 예전엔 board file의 정적 변수였고, 지금은 커널이 DT 노드를 읽어 **런타임에** 만든다 (§6, §12).

### 3.2 무엇이 문제였나

board file 하나는 무해하다. 문제는 **수**였다.

- **보드 수만큼 C 코드**: 2000년대 후반 ARM SoC 벤더(TI, Samsung, Freescale, Marvell, Qualcomm, NVIDIA …)가 폭발했고, SoC마다 `arch/arm/mach-<soc>/` 디렉터리, 보드마다 `board-*.c`가 쌓였다. 같은 SMC91x 칩을 쓰는 보드 수십 개가 비슷한 `struct resource`를 각자 복사했다.
- **보드마다 다른 커널**: 많은 `mach-*`가 서로 배타적인 Kconfig 선택이어서, "OMAP용 커널"과 "i.MX용 커널"을 따로 빌드해야 했다. 배포판(Debian, Ubuntu)이 ARM 보드를 지원하려면 보드마다 커널 패키지를 만들어야 했다.
- **merge window마다 충돌과 churn**: 매 릴리스마다 `arch/arm`에 막대한 양의 보드 코드 변경이 들어왔다.

이 상황이 2011년 3월 LKML에서 폭발했다. v2.6.39 merge window에 들어온 OMAP 쪽 pull request 스레드에서 Linus Torvalds가 ARM 플랫폼 코드의 끝없는 churn에 강하게 불만을 터뜨렸는데, 자주 인용되는 첫 문장이 `"Gaah. Guys, this whole ARM thing is a f*cking pain in the ass."`(욕설 일부 가림)이다. 요지는 "보드마다 중복된 코드와 서로 밟는 변경을 그만 보내라, 공통화하라"였다. 이 사건 이후 ARM 커뮤니티는 Linaro를 중심으로 `arm-soc` 트리(Arnd Bergmann, Olof Johansson 등이 관리)로 SoC 코드를 모아 리뷰하는 체계를 만들고, **새 플랫폼은 DT로만 받는다**는 방향을 굳혔다. (원문: [LKML 2011-03-17, "Re: [GIT PULL] omap changes for v2.6.39 merge window"](https://lkml.org/lkml/2011/3/17/492). 당시 상황은 LWN.net의 2011년 기사 "ARM wrestling"에도 정리돼 있다.)

> 인용은 조심해서 쓰자. 면접에서는 "2011년에 Linus가 ARM 보드 코드의 중복과 churn을 공개적으로 비판했고, 그게 DT 전면 도입과 arm-soc 정리의 계기가 됐다" 정도면 충분하다. 정확한 문장을 외울 필요는 없다.

### 3.3 DT는 어디서 왔나

DT는 ARM이 발명한 것이 아니다. 커널 문서 `usage-model.rst`의 History 절에 따르면:

- 원래 **Open Firmware**(IEEE 1275, Sun SPARC·Apple PowerPC Mac의 펌웨어)가 OS에 하드웨어 토폴로지를 넘기던 자료 구조다.
- **2005년** PowerPC Linux가 32/64비트를 합치면서 Open Firmware가 없는 임베디드 PowerPC 보드에도 DT를 강제했고, 이를 위해 펌웨어 없이 바이너리 덩어리로 넘길 수 있는 **FDT(Flattened Device Tree)** 형식을 만들었다. U-Boot와 kexec가 dtb를 넘기고 고칠 수 있게 바뀌었다.
- 이후 FDT 인프라가 아키텍처 공통 코드(`drivers/of/`)로 일반화됐고, ARM 32비트가 2011년 무렵부터 본격적으로 옮겨 왔다.
- **arm64**(2012년 메인라인 진입)는 처음부터 board file 개념이 없다. 하드웨어 설명은 DT 또는 ACPI로만 받는다.

예제 2에서 이 역사가 지금 트리에 어떻게 남아 있는지 센다.

### 3.4 예제 2 — 6.18 트리에 남은 board file 세기

무엇을 확인하나: 32비트 ARM에는 board file이 소수 남아 있고, arm64에는 `mach-*` 디렉터리 자체가 없다는 것.

```sh
cd /var/lab/src/linux-6.18.55
ls -d arch/arm/mach-* | wc -l
ls arch/arm/mach-*/board-*.c | wc -l
ls arch/arm/mach-*/board-*.c
ls arch/arm64/ | xargs
ls -d arch/arm64/mach-*
```

```text
55
20
arch/arm/mach-artpec/board-artpec6.c
arch/arm/mach-clps711x/board-dt.c
arch/arm/mach-gemini/board-dt.c
arch/arm/mach-lpc18xx/board-dt.c
arch/arm/mach-mvebu/board-v7.c
arch/arm/mach-omap1/board-ams-delta.c
arch/arm/mach-omap1/board-nokia770.c
arch/arm/mach-omap1/board-osk.c
arch/arm/mach-omap1/board-palmte.c
arch/arm/mach-omap1/board-sx1-mmc.c
arch/arm/mach-omap1/board-sx1.c
arch/arm/mach-omap2/board-generic.c
arch/arm/mach-omap2/board-n8x0.c
arch/arm/mach-orion5x/board-d2net.c
arch/arm/mach-orion5x/board-dt.c
arch/arm/mach-orion5x/board-mss2.c
arch/arm/mach-orion5x/board-rd88f5182.c
arch/arm/mach-sti/board-dt.c
arch/arm/mach-stm32/board-dt.c
arch/arm/mach-tegra/board-paz00.c
Kbuild Kconfig Kconfig.debug Kconfig.platforms Makefile boot configs crypto hyperv include kernel kvm lib mm net tools xen
ls: cannot access 'arch/arm64/mach-*': No such file or directory
```

출력에서 볼 것: 이름이 `board-*.c`인 파일 20개 중 상당수가 이름에 `-dt`(`board-dt.c`, `board-generic.c`)가 붙어 있다 — 이름만 board file이고 실제로는 "DT를 쓰는 SoC 공통 초기화"다. 진짜 옛 방식은 OMAP1(`board-osk.c` 등), Nokia N8x0, Orion5x 몇 개 같은 오래된 32비트 보드뿐이다. arm64 디렉터리 목록에는 `mach-*`가 아예 없다.

### 3.5 같은 칩, 세 가지 설명 방식 — smc91x 드라이버

무엇을 확인하나: 한 드라이버가 board file(이름 매칭), DT(compatible 매칭), ACPI(_HID 매칭)를 **동시에** 지원하는 모습. 드라이버 코드는 하드웨어가 어떻게 설명됐는지와 무관하게 `platform_get_resource()`로 같은 자원을 받는다.

```sh
cd /var/lab/src/linux-6.18.55
sed -n 191,195p arch/arm/boot/dts/arm/versatile-ab.dts    # DT로 쓴 같은 칩
sed -n 2170,2182p drivers/net/ethernet/smsc/smc91x.c        # ACPI·OF 매칭 테이블
sed -n 2468,2477p drivers/net/ethernet/smsc/smc91x.c        # platform_driver
```

```text
	net@10010000 {
		compatible = "smsc,lan91c111";
		reg = <0x10010000 0x10000>;
		interrupts = <25>;
	};
static const struct acpi_device_id smc91x_acpi_match[] = {
	{ "LNRO0003", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, smc91x_acpi_match);

#if IS_BUILTIN(CONFIG_OF)
static const struct of_device_id smc91x_match[] = {
	{ .compatible = "smsc,lan91c94", },
	{ .compatible = "smsc,lan91c111", },
	{},
};
MODULE_DEVICE_TABLE(of, smc91x_match);
static struct platform_driver smc_driver = {
	.probe		= smc_drv_probe,
	.remove		= smc_drv_remove,
	.driver		= {
		.name	= CARDNAME,
		.pm	= &smc_drv_pm_ops,
		.of_match_table   = of_match_ptr(smc91x_match),
		.acpi_match_table = smc91x_acpi_match,
	},
};
```

출력에서 볼 것: `CARDNAME`은 `"smc91x"`로, §3.1 board file의 `.name = "smc91x"`와 이름으로 매칭된다. ARM Versatile 보드의 DT 노드 4줄(`compatible`, `reg`, `interrupts`)이 board file의 `struct resource` 배열 + `struct platform_device` 20줄을 대체한다. 그리고 그 4줄은 **커널 바이너리 밖**에 산다. 드라이버 쪽 `probe()`는 셋 중 어느 경로로 왔든 `platform_get_resource(pdev, IORESOURCE_MEM, 0)`과 `platform_get_irq(pdev, 0)`으로 똑같이 자원을 받는다 — 매칭 우선순위와 상세는 D3.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="165" y="22" text-anchor="middle" font-size="14" font-weight="bold">Board-file 시대 (32비트 ARM, ~2011)</text>
  <text x="510" y="22" text-anchor="middle" font-size="14" font-weight="bold">DT 시대 (ARM·arm64·RISC-V)</text>
  <rect x="10" y="38" width="150" height="190" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a"/>
  <text x="85" y="58" text-anchor="middle" font-size="13">zImage (OSK용)</text>
  <rect x="22" y="70" width="126" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="88" text-anchor="middle" font-size="12">core + drivers</text>
  <rect x="22" y="104" width="126" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="85" y="122" text-anchor="middle" font-size="12">board-osk.c</text>
  <text x="22" y="150" font-size="12">platform_device</text>
  <text x="22" y="168" font-size="12">smc91x @ 하드코딩</text>
  <text x="22" y="186" font-size="12">MACHINE_START</text>
  <text x="22" y="212" font-size="12">r1 = mach type</text>
  <rect x="175" y="38" width="150" height="190" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a"/>
  <text x="250" y="58" text-anchor="middle" font-size="13">zImage (보드 B용)</text>
  <rect x="187" y="70" width="126" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="88" text-anchor="middle" font-size="12">core + drivers</text>
  <rect x="187" y="104" width="126" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="250" y="122" text-anchor="middle" font-size="12">board-b.c</text>
  <text x="187" y="150" font-size="12">같은 칩, 다른 주소</text>
  <text x="187" y="168" font-size="12">복사·붙여넣기</text>
  <text x="187" y="186" font-size="12">mach-* Kconfig</text>
  <text x="187" y="212" font-size="12">서로 배타적일 때 많음</text>
  <text x="165" y="252" text-anchor="middle" font-size="12">보드 추가 = 커널 C 코드 수정 + 재빌드</text>
  <text x="165" y="272" text-anchor="middle" font-size="12">보드 N개 → 커널 빌드 여러 벌</text>
  <line x1="340" y1="34" x2="340" y2="290" stroke="currentColor" stroke-dasharray="4 4"/>
  <rect x="360" y="38" width="140" height="120" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="430" y="62" text-anchor="middle" font-size="13" font-weight="bold">Image (공용)</text>
  <text x="372" y="88" font-size="12">core + drivers</text>
  <text x="372" y="108" font-size="12">of_match_table</text>
  <text x="372" y="128" font-size="12">보드를 모른다</text>
  <rect x="540" y="38" width="130" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="605" y="58" text-anchor="middle" font-size="12">imx8mp-evk.dtb</text>
  <rect x="540" y="78" width="130" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="605" y="98" text-anchor="middle" font-size="12">k3-am62x-sk.dtb</text>
  <rect x="540" y="118" width="130" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="605" y="138" text-anchor="middle" font-size="12">sensorkit.dtb</text>
  <rect x="400" y="190" width="230" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="212" text-anchor="middle" font-size="12">부트로더: Image + 보드의 .dtb 하나</text>
  <line x1="430" y1="158" x2="470" y2="188" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="605" y1="148" x2="570" y2="188" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="515" y="252" text-anchor="middle" font-size="12">보드 추가 = .dts 파일 추가 (커널 바이너리 그대로)</text>
  <text x="515" y="272" text-anchor="middle" font-size="12">arm64 x0 = DTB 물리 주소</text>
  <text x="340" y="320" text-anchor="middle" font-size="12">platform_device / driver 구조는 그대로 — "누가 디바이스를 만드나"만 C 코드에서 데이터로 바뀌었다</text>
</svg>
```
그림 2 — board file 시대에는 보드 설명이 커널 바이너리 안의 C 코드였다. DT 시대에는 공용 커널 하나에 보드마다 `.dtb` 하나를 짝지어 부트로더가 넘긴다.

### 3.6 예제 3 — "커널 하나, 보드 여러 개"를 직접 보기

무엇을 확인하나: **바이트 하나 다르지 않은 같은 커널 `Image`**가, DT가 다른 두 "보드"(CPU 1개·RAM 512MB vs CPU 4개·RAM 1GB)에서 각자 맞게 뜬다. QEMU `virt`는 명령행 옵션에 따라 DT를 다르게 생성한다 — 실제 보드에서 `.dtb`를 바꿔 끼우는 것과 같은 상황이다.

```sh
# 게스트 스크립트 g_board.sh
uname -v
ls /proc/device-tree/cpus
hexdump -C /proc/device-tree/memory@40000000/reg
nproc; grep MemTotal /proc/meminfo
```

```sh
md5sum /var/lab/out/Image
echo "### board A: -smp 1 -m 512M"
qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel /var/lab/out/Image \
  -initrd d1_initrd.gz -append "console=ttyAMA0 quiet LAB_CMD=sh~/g_board.sh"
echo "### board B: -smp 4 -m 1G"     # 같은 Image, 옵션만 다름
qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 4 -m 1G -nographic -kernel /var/lab/out/Image \
  -initrd d1_initrd.gz -append "console=ttyAMA0 quiet LAB_CMD=sh~/g_board.sh"
```

```text
a82062c7b6c73f49d076b1c8e45e92b2  /var/lab/out/Image
### board A: -smp 1 -m 512M
lab initramfs: 6.18.55 up 8.93s
$ uname -v
#1 SMP PREEMPT Sat Oct 10 10:05:19 PDT 2026
$ ls /proc/device-tree/cpus
#address-cells #size-cells cpu-map cpu@0 name
$ hexdump -C /proc/device-tree/memory@40000000/reg
00000000  00 00 00 00 40 00 00 00  00 00 00 00 20 00 00 00  |....@....... ...|
00000010
$ nproc; grep MemTotal /proc/meminfo
1
MemTotal:         473340 kB
[   10.264046] reboot: Power down
### board B: -smp 4 -m 1G
lab initramfs: 6.18.55 up 6.72s
$ uname -v
#1 SMP PREEMPT Sat Oct 10 10:05:19 PDT 2026
$ ls /proc/device-tree/cpus
#address-cells #size-cells cpu-map cpu@0 cpu@1 cpu@2 cpu@3 name
$ hexdump -C /proc/device-tree/memory@40000000/reg
00000000  00 00 00 00 40 00 00 00  00 00 00 00 40 00 00 00  |....@.......@...|
00000010
$ nproc; grep MemTotal /proc/meminfo
4
MemTotal:         986824 kB
[    8.676758] reboot: Power down
```

출력에서 볼 것: `uname -v`의 빌드 시각이 같다 — 같은 커널이다. 그런데 DT의 `/cpus` 아래 `cpu@N` 노드 수가 1 → 4로, `/memory@40000000/reg`의 크기 칸이 `0x20000000`(512MB) → `0x40000000`(1GB)로 바뀌었고, 커널은 그대로 따른다. 펌웨어로 치면 `#define NUM_CORES`와 `#define DRAM_SIZE`를 바꾸고 **재빌드 없이** 다른 보드에서 돌린 셈이다. (MemTotal이 512MB보다 작은 것은 커널 이미지·예약 영역을 뺀 값이라서다.)

---

## 4. DT는 데이터다 — `.dts → dtc → .dtb → 부트로더 → 커널 → 드라이버`

### 4.1 전체 파이프라인

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" font-size="13" font-weight="bold">빌드 시간 (호스트 PC)</text>
  <text x="511" y="22" font-size="13" font-weight="bold">부팅 시간 (보드)</text>
  <rect x="10" y="34" width="150" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="56" text-anchor="middle" font-size="13" font-weight="bold">.dts / .dtsi</text>
  <rect x="177" y="34" width="150" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="252" y="56" text-anchor="middle" font-size="13" font-weight="bold">dtc (+ cpp)</text>
  <rect x="344" y="34" width="150" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="419" y="56" text-anchor="middle" font-size="13" font-weight="bold">.dtb (FDT)</text>
  <rect x="511" y="34" width="150" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="586" y="56" text-anchor="middle" font-size="13" font-weight="bold">부트로더</text>
  <line x1="160" y1="51" x2="175" y2="51" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="327" y1="51" x2="342" y2="51" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="494" y1="51" x2="509" y2="51" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="18" y="90" font-size="12">사람이 쓰는 텍스트</text>
  <text x="18" y="108" font-size="12">노드·속성·label</text>
  <text x="18" y="126" font-size="12">#include, 매크로</text>
  <text x="185" y="90" font-size="12">컴파일러 겸 디컴파일러</text>
  <text x="185" y="108" font-size="12">문법·phandle 검사</text>
  <text x="185" y="126" font-size="12">-I dtb -O dts 역변환</text>
  <text x="352" y="90" font-size="12">big-endian 바이너리</text>
  <text x="352" y="108" font-size="12">header·struct·strings</text>
  <text x="352" y="126" font-size="12">magic d00dfeed</text>
  <text x="519" y="90" font-size="12">U-Boot / QEMU</text>
  <text x="519" y="108" font-size="12">RAM에 로드</text>
  <text x="519" y="126" font-size="12">/chosen 수정 (bootargs</text>
  <text x="519" y="144" font-size="12">initrd, memory)</text>
  <path d="M586,152 L586,180 L85,180 L85,204" fill="none" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="300" y="173" font-size="12">x0 = DTB 물리 주소로 커널 진입</text>
  <rect x="10" y="206" width="150" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="85" y="228" text-anchor="middle" font-size="13" font-weight="bold">early scan</text>
  <rect x="177" y="206" width="150" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="252" y="228" text-anchor="middle" font-size="13" font-weight="bold">unflatten</text>
  <rect x="344" y="206" width="150" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="419" y="228" text-anchor="middle" font-size="13" font-weight="bold">populate</text>
  <rect x="511" y="206" width="150" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="586" y="228" text-anchor="middle" font-size="13" font-weight="bold">match → probe</text>
  <line x1="160" y1="223" x2="175" y2="223" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="327" y1="223" x2="342" y2="223" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="494" y1="223" x2="509" y2="223" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="18" y="262" font-size="12">평평한 blob 직접 읽기</text>
  <text x="18" y="280" font-size="12">memory, bootargs</text>
  <text x="18" y="298" font-size="12">earlycon, rng-seed</text>
  <text x="185" y="262" font-size="12">struct device_node 트리</text>
  <text x="185" y="280" font-size="12">of_root</text>
  <text x="185" y="298" font-size="12">/sys/firmware/devicetree</text>
  <text x="352" y="262" font-size="12">platform_device</text>
  <text x="352" y="280" font-size="12">amba_device 생성</text>
  <text x="352" y="298" font-size="12">dev.of_node = 노드</text>
  <text x="519" y="262" font-size="12">compatible ↔</text>
  <text x="519" y="280" font-size="12">of_match_table</text>
  <text x="519" y="298" font-size="12">→ 드라이버 probe()</text>
  <text x="10" y="322" font-size="12">커널 (setup_arch → arch_initcall_sync → 드라이버 등록 시점)</text>
</svg>
```
그림 3 — DT의 일생. 위 줄은 데이터가 만들어져 전달되는 과정, 아래 줄은 커널이 그 데이터를 디바이스 모델로 바꾸는 과정이다. 각 칸을 §4~§6에서 랩으로 하나씩 확인한다.

### 4.2 랩의 DT 읽기 — 모든 장치 식별하기

랩의 `virt.dts`는 QEMU가 `virt` 머신을 만들 때 생성한 DT를 `dtc`로 역변환한 것이다 (그래서 label 없이 phandle이 숫자 `0x8002` 같은 형태로 보인다 — 사람이 쓴 `.dts`라면 `&gic` 같은 label을 쓴다, D2). 노드를 하나씩 읽어 "무슨 장치인가, 누가 쓰는가"를 표로 만들면:

| DT 노드 | `compatible` (첫 번째) | 정체 | 주소 / 자원 | 커널에서 누가 쓰나 |
|---|---|---|---|---|
| `/` | `linux,dummy-virt` | 머신 식별 | `#address-cells = 2`, `#size-cells = 2` | `Machine model:` 출력, platform identification |
| `/cpus/cpu@0` | `arm,cortex-a57` | CPU 코어 | `reg = 0` (MPIDR) | SMP 부팅, `/sys/devices/system/cpu` |
| `/memory@40000000` | (없음, `device_type = "memory"`) | DRAM | 0x4000_0000 + 512MB | early scan → memblock |
| `/intc@8000000` | `arm,cortex-a15-gic` | GICv2 인터럽트 컨트롤러 | distributor 0x0800_0000, CPU if 0x0801_0000 | `IRQCHIP_DECLARE` early init |
| `/intc@8000000/v2m@8020000` | `arm,gic-v2m-frame` | MSI 프레임 (PCIe MSI용) | 0x0802_0000 | GICv2m |
| `/timer` | `arm,armv8-timer` | 아키텍처 타이머 | PPI 13·14·11·10 | `TIMER_OF_DECLARE` early init |
| `/pmu` | `arm,armv8-pmuv3` | 성능 카운터 | PPI 7 | `armv8-pmu` 드라이버 |
| `/psci` | `arm,psci-1.0` | 전원 관리 펌웨어 인터페이스 | `method = "hvc"` | CPU on/off, poweroff |
| `/apb-pclk` | `fixed-clock` | 24MHz 고정 클록 | `#clock-cells = 0` | `CLK_OF_DECLARE` early init |
| `/pl011@9000000` | `arm,pl011` | UART (콘솔) | 0x0900_0000, SPI 1 | `uart-pl011` (amba) |
| `/pl031@9010000` | `arm,pl031` | RTC | 0x0901_0000, SPI 2 | `rtc-pl031` (amba) |
| `/fw-cfg@9020000` | `qemu,fw-cfg-mmio` | QEMU 설정 채널 | 0x0902_0000 | (이 커널에선 드라이버 꺼짐) |
| `/pl061@9030000` | `arm,pl061` | GPIO 컨트롤러 | 0x0903_0000, SPI 7, `gpio-controller` | `pl061_gpio` (amba) |
| `/gpio-keys` | `gpio-keys` | 전원 버튼 (GPIO 3번 핀) | `gpios = <pl061 3 0>`, `linux,code = 116` | `gpio-keys` 입력 드라이버 |
| `/virtio_mmio@a000000` … `@a003e00` (32개) | `virtio,mmio` | virtio 전송 슬롯 | 0x0A00_0000부터 0x200 간격, SPI 16~47 | `virtio-mmio` (빈 슬롯은 probe 거절) |
| `/pcie@10000000` | `pci-host-ecam-generic` | PCIe host bridge | ECAM 0x40_1000_0000, `ranges`, `interrupt-map` | `pci-host-generic` → 그 아래는 enumeration |
| `/flash@0` | `cfi-flash` | NOR 플래시 2뱅크 | 0x0 + 64MB, 0x0400_0000 + 64MB | `physmap-flash` |
| `/platform-bus@c000000` | `qemu,platform` | 동적 sysbus 장치용 창 | 0x0C00_0000 + 32MB | (비어 있음) |
| `/chosen` | — | 런타임 설정 | `stdout-path`, `rng-seed`, `kaslr-seed` (부팅 시 `bootargs`, initrd 추가) | early scan |
| `/aliases` | — | 별명 | `serial0 = "/pl011@9000000"` | `ttyAMA0`의 번호 0 |

`interrupts = <0x00 0x01 0x04>` 같은 3칸 값은 GIC 바인딩 규칙대로 읽는다: 첫 칸 0 = SPI(1이면 PPI), 둘째 칸 = 번호, 셋째 칸 4 = level-high(1이면 rising edge). §5.3에서 이 값이 `/proc/interrupts`의 GIC 33번(SPI 번호 + 32)으로 바뀌는 걸 확인한다. 셀 해석 규칙은 D2·D4에서 자세히.

### 4.3 예제 4 — `.dtb`를 열어 보기: `dtc` 역변환과 크기

무엇을 확인하나: `.dtb`는 바이너리이고 `dtc`로 `.dts` 텍스트와 왕복 변환된다. 그리고 QEMU가 넘기는 blob에는 여유 공간이 있다.

```sh
cd /var/tmp/D1
dtc -I dtb -O dtb /var/lab/out/virt.dtb -o d1_virt_small.dtb   # 다시 굳혀서 여유 공간 제거
ls -l /var/lab/out/virt.dtb d1_virt_small.dtb | awk '{print $5, $9}'
dtc -I dtb -O dts /var/lab/out/virt.dtb | head -8
fdtget -t s /var/lab/out/virt.dtb /pl011@9000000 compatible
fdtget -t x /var/lab/out/virt.dtb /pl011@9000000 reg
```

```text
1048576 /var/lab/out/virt.dtb
7566 d1_virt_small.dtb
/dts-v1/;

/ {
	interrupt-parent = <0x8002>;
	dma-coherent;
	model = "linux,dummy-virt";
	#size-cells = <0x02>;
	#address-cells = <0x02>;
arm,pl011 arm,primecell
0 9000000 0 1000
```

출력에서 볼 것: QEMU가 덤프한 blob은 정확히 1 MiB(1048576)지만 실제 내용은 7.5KB다. 나머지는 빈 공간이다 — 부트로더·QEMU가 부팅 직전에 `/chosen`에 `bootargs`나 initrd 주소를 **제자리에서 추가**할 수 있도록 남겨 둔 여유다. U-Boot에서도 `fdt resize`로 같은 일을 한다 (C3). `fdtget`은 컴파일 없이 blob에서 속성 하나를 꺼내는 도구로, 보드 위에서 디버깅할 때 유용하다.

### 4.4 FDT 바이너리 구조

`.dtb`는 Devicetree Specification(v0.4) 5장의 FDT 형식이다. 모든 정수는 **big-endian**이다 (Don의 PCIe config space는 little-endian이었다 — 헷갈리기 쉬운 지점).

```
offset 0x00  +---------------------------+
             | fdt_header (40 bytes)     |  magic 0xd00dfeed, totalsize,
             |                           |  off_dt_struct, off_dt_strings, ...
off_mem_rsvmap +-------------------------+
             | memory reservation block  |  {u64 address, u64 size}... {0,0}
off_dt_struct  +-------------------------+
             | structure block           |  FDT_BEGIN_NODE(1) "name\0" pad
             |   tokens, 4-byte aligned  |  FDT_PROP(3) len nameoff value pad
             |                           |  FDT_END_NODE(2) ... FDT_END(9)
off_dt_strings +-------------------------+
             | strings block             |  "compatible\0reg\0interrupts\0..."
             +---------------------------+
             | free space (padding)      |  room for bootloader edits
totalsize    +---------------------------+
```

속성 이름은 strings block에 **한 번만** 저장되고, 각 속성은 그 문자열의 offset(`nameoff`)만 들고 있다. `compatible`이 48번 나와도 문자열 `"compatible"`은 한 번만 있다.

### 4.5 예제 5 — FDT 헤더를 C로 직접 파싱

무엇을 확인하나: 커널이 부팅 직후 가장 먼저 보는 40바이트 헤더를 직접 읽는다. 같은 프로그램을 게스트에서 돌리면 **커널이 실제로 받은 blob**(`/sys/firmware/fdt`)을 읽는다.

```c
/* d1_fdthdr: .dtb(FDT) 헤더 40바이트를 읽어 필드를 출력한다 (FDT는 big-endian) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>   /* ntohl: big-endian -> host */

struct fdt_header {               /* Devicetree Specification v0.4, 5.2 */
    uint32_t magic, totalsize, off_dt_struct, off_dt_strings;
    uint32_t off_mem_rsvmap, version, last_comp_version;
    uint32_t boot_cpuid_phys, size_dt_strings, size_dt_struct;
};

int main(int argc, char **argv)
{
    struct fdt_header h;
    FILE *f = fopen(argc > 1 ? argv[1] : "/sys/firmware/fdt", "rb");
    if (!f) { perror("fopen"); exit(1); }
    if (fread(&h, sizeof h, 1, f) != 1) { perror("fread"); exit(1); }
    fclose(f);
    printf("magic             0x%08x %s\n", ntohl(h.magic),
           ntohl(h.magic) == 0xd00dfeed ? "(OK: d00dfeed)" : "(BAD)");
    printf("totalsize         %u bytes\n", ntohl(h.totalsize));
    printf("off_mem_rsvmap    0x%x\n", ntohl(h.off_mem_rsvmap));
    printf("off_dt_struct     0x%x  (size %u)\n", ntohl(h.off_dt_struct), ntohl(h.size_dt_struct));
    printf("off_dt_strings    0x%x  (size %u)\n", ntohl(h.off_dt_strings), ntohl(h.size_dt_strings));
    printf("version           %u (last_comp %u)\n", ntohl(h.version), ntohl(h.last_comp_version));
    printf("boot_cpuid_phys   %u\n", ntohl(h.boot_cpuid_phys));
    return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static ~/.elinux_scratch/D1/d1_fdthdr.c -o d1_fdthdr   # 경고 0
./d1_fdthdr /var/lab/out/virt.dtb
./d1_fdthdr d1_virt_small.dtb
```

```text
magic             0xd00dfeed (OK: d00dfeed)
totalsize         1048576 bytes
off_mem_rsvmap    0x30
off_dt_struct     0x40  (size 7048)
off_dt_strings    0x1bc8  (size 462)
version           17 (last_comp 16)
boot_cpuid_phys   0
magic             0xd00dfeed (OK: d00dfeed)
totalsize         7566 bytes
off_mem_rsvmap    0x28
off_dt_struct     0x38  (size 7048)
off_dt_strings    0x1bc0  (size 462)
version           17 (last_comp 16)
boot_cpuid_phys   0
```

출력에서 볼 것: magic `0xd00dfeed`("dood feed")가 FDT의 서명이다. 부트로더가 넘긴 주소에서 이 값이 안 보이면 커널은 DT를 못 찾았다고 판단한다. 두 파일의 structure block(7048)·strings block(462) 크기는 같고 `totalsize`만 다르다 — 내용은 같고 여유 공간만 다르다는 증거다. 이 프로그램을 게스트에서 인자 없이 돌린 결과는 §5.2에 있다.

### 4.6 예제 6 — structure block 토큰을 걸으며 장치 목록 뽑기

무엇을 확인하나: 커널의 early scan(`of_scan_flat_dt()`)이 하는 일을 50줄 C로 흉내 낸다. 토큰을 순서대로 읽어 노드 경로를 만들고, `compatible` 속성이 있는 노드를 센다.

```c
/* d1_fdtwalk: FDT structure block의 토큰을 직접 걸으며 노드와 compatible을 센다 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>

enum { BEGIN_NODE = 1, END_NODE = 2, PROP = 3, NOP = 4, END = 9 };
static uint32_t be(const uint8_t *p) { return ntohl(*(const uint32_t *)p); }

int main(int argc, char **argv)
{
    static uint8_t b[1 << 20];
    FILE *f = fopen(argc > 1 ? argv[1] : "/sys/firmware/fdt", "rb");
    if (!f) { perror("fopen"); exit(1); }
    size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n < 40 || be(b) != 0xd00dfeed) { fprintf(stderr, "not a dtb\n"); exit(1); }
    const uint8_t *p = b + be(b + 8);             /* off_dt_struct  */
    const char *strs = (const char *)b + be(b + 12); /* off_dt_strings */
    int nodes = 0, props = 0, compat = 0, verbose = argc > 2;
    char path[512] = "";
    for (;;) {
        uint32_t tok = be(p); p += 4;
        if (tok == BEGIN_NODE) {
            const char *name = (const char *)p; nodes++;
            if (name[0] && strlen(path) + strlen(name) + 2 < sizeof path)
                strcat(strcat(path, "/"), name);
            p += (strlen(name) + 4) & ~3u;         /* 이름 + NUL, 4바이트 정렬 */
        } else if (tok == END_NODE) {
            char *slash = strrchr(path, '/');
            if (slash) *slash = '\0';
        } else if (tok == PROP) {
            uint32_t len = be(p), nameoff = be(p + 4);
            const char *val = (const char *)p + 8;
            props++;
            if (!strcmp(strs + nameoff, "compatible")) {
                compat++;
                if (verbose) printf("%-30s %s\n", path[0] ? path : "/", val); /* 첫 문자열만 */
            }
            p += 8 + ((len + 3) & ~3u);
        } else if (tok == NOP) {
            continue;
        } else if (tok == END) {
            break;
        } else { fprintf(stderr, "bad token %u\n", tok); exit(1); }
    }
    printf("nodes=%d properties=%d nodes-with-compatible=%d\n", nodes, props, compat);
    return 0;
}
```

```sh
./d1_fdtwalk /var/lab/out/virt.dtb v | grep -v "virtio_mmio@a00[1-3]"   # virtio 슬롯 24줄은 생략
```

```text
/                              linux,dummy-virt
/psci                          arm,psci-1.0
/platform-bus@c000000          qemu,platform
/fw-cfg@9020000                qemu,fw-cfg-mmio
/virtio_mmio@a000000           virtio,mmio
/virtio_mmio@a000200           virtio,mmio
/virtio_mmio@a000400           virtio,mmio
/virtio_mmio@a000600           virtio,mmio
/virtio_mmio@a000800           virtio,mmio
/virtio_mmio@a000a00           virtio,mmio
/virtio_mmio@a000c00           virtio,mmio
/virtio_mmio@a000e00           virtio,mmio
/gpio-keys                     gpio-keys
/pl061@9030000                 arm,pl061
/pcie@10000000                 pci-host-ecam-generic
/pl031@9010000                 arm,pl031
/pl011@9000000                 arm,pl011
/pmu                           arm,armv8-pmuv3
/intc@8000000                  arm,cortex-a15-gic
/intc@8000000/v2m@8020000      arm,gic-v2m-frame
/flash@0                       cfi-flash
/cpus/cpu@0                    arm,cortex-a57
/timer                         arm,armv8-timer
/apb-pclk                      fixed-clock
nodes=57 properties=221 nodes-with-compatible=48
```

출력에서 볼 것: 노드 57개, 속성 221개, `compatible`을 가진 노드 48개. 57 = 루트 1 + 루트 직속 49 + `/cpus` 아래 5(`cpu-map`, `socket0`, `cluster0`, `core0`, `cpu@0`) + `/gpio-keys/poweroff` 1 + `/intc/v2m` 1. `compatible` 문자열별로 세면 32개가 `virtio,mmio`이고 나머지는 전부 하나씩이다:

```sh
grep "compatible =" /var/lab/out/virt.dts | sed "s/^[ \t]*//" | sort | uniq -c | sort -rn
```

```text
     32 compatible = "virtio,mmio";
      1 compatible = "qemu,platform", "simple-bus";
      1 compatible = "qemu,fw-cfg-mmio";
      1 compatible = "pci-host-ecam-generic";
      1 compatible = "linux,dummy-virt";
      1 compatible = "gpio-keys";
      1 compatible = "fixed-clock";
      1 compatible = "cfi-flash";
      1 compatible = "arm,psci-1.0", "arm,psci-0.2", "arm,psci";
      1 compatible = "arm,pl061", "arm,primecell";
      1 compatible = "arm,pl031", "arm,primecell";
      1 compatible = "arm,pl011", "arm,primecell";
      1 compatible = "arm,gic-v2m-frame";
      1 compatible = "arm,cortex-a57";
      1 compatible = "arm,cortex-a15-gic";
      1 compatible = "arm,armv8-timer", "arm,armv7-timer";
      1 compatible = "arm,armv8-pmuv3";
```

`compatible`이 여러 문자열인 경우(`"arm,psci-1.0", "arm,psci-0.2", "arm,psci"`)는 **가장 구체적인 것부터 일반적인 것 순서**다. 커널은 앞에서부터 맞는 드라이버를 찾는다 — 매칭 규칙은 D3.

---

## 5. 게스트 안에서 DT 보기 — `/proc/device-tree`와 `/sys/firmware`

### 5.1 직관 — 커널이 펼친 트리를 파일 시스템으로 보여 준다

커널은 받은 FDT를 부팅 초기에 **unflatten**한다 — 평평한 바이트 덩어리를 `struct device_node`(노드)와 `struct property`(속성)의 포인터 트리로 바꾼다. 그 트리를 sysfs에 그대로 비춘 것이 `/sys/firmware/devicetree/base`이고, `/proc/device-tree`는 옛 사용자 공간 호환용 심볼릭 링크다. 규칙은 단순하다.

- **노드 = 디렉터리**, **속성 = 파일**, 파일 내용 = 속성 값 바이트 그대로 (big-endian 정수, NUL로 끝나는 문자열).
- 원본 FDT(부트로더가 넘긴 blob)는 `/sys/firmware/fdt`에 읽기 전용으로 남는다 (root만 읽기 가능).

### 5.2 예제 7 — `/proc/device-tree` 둘러보기

무엇을 확인하나: 게스트 커널이 본 DT의 최상위, 머신 식별(`compatible`, `model`), 그리고 부트로더(여기선 QEMU)가 런타임에 채워 넣은 `/chosen`.

```sh
# 게스트 스크립트 g_procdt.sh (virtio_mmio 32개 중 4개만 남기고 grep으로 걸렀다)
uname -r
ls -l /proc/device-tree
ls /sys/firmware/devicetree/base | grep -v '^virtio_mmio@a00[0-3][2-9a-e]' | xargs -n 4
tr '\0' '\n' < /proc/device-tree/compatible
tr '\0' '\n' < /proc/device-tree/model
tr '\0' '\n' < /proc/device-tree/chosen/bootargs
ls /proc/device-tree/chosen | xargs
```

```text
lab initramfs: 6.18.55 up 1.59s
$ uname -r
6.18.55
$ ls -l /proc/device-tree
lrwxrwxrwx    1 0        0               29 Oct 10 17:24 /proc/device-tree -> /sys/firmware/devicetree/base
$ ls /sys/firmware/devicetree/base
#address-cells #size-cells aliases apb-pclk
chosen compatible cpus dma-coherent
flash@0 fw-cfg@9020000 gpio-keys intc@8000000
interrupt-parent memory@40000000 model name
pcie@10000000 pl011@9000000 pl031@9010000 pl061@9030000
platform-bus@c000000 pmu psci timer
virtio_mmio@a000000 virtio_mmio@a001000 virtio_mmio@a002000 virtio_mmio@a003000
$ cat /proc/device-tree/compatible
linux,dummy-virt
$ cat /proc/device-tree/model
linux,dummy-virt
$ cat /proc/device-tree/chosen/bootargs
console=ttyAMA0 quiet LAB_CMD=sh~/g_procdt.sh
$ ls /proc/device-tree/chosen
bootargs kaslr-seed linux,initrd-end linux,initrd-start name stdout-path
[    2.091881] reboot: Power down
```

출력에서 볼 것: `/chosen`에 원본 `virt.dts`에는 없던 `bootargs`, `linux,initrd-start`, `linux,initrd-end`가 생겼다. QEMU가 `-append`와 `-initrd`를 보고 부팅 직전에 DT에 써 넣은 것이다 — U-Boot의 `bootm`/`booti`도 똑같이 한다 (C3). 반대로 원본에 있던 `rng-seed`는 **사라졌다**: 커널이 early scan에서 난수 시드로 쓴 뒤 `fdt_nop_property()`로 지운다(`drivers/of/fdt.c`, "try to clear seed so it won't be found"). 이게 usage-model의 둘째 목적, **runtime configuration**이다.

### 5.3 예제 8 — 노드 하나를 바이트 단위로, 그리고 unflatten 전후 비교

무엇을 확인하나: UART 노드의 속성 파일이 big-endian 바이트 그대로라는 것, 커널이 실제로 받은 FDT 헤더(§4.5 프로그램을 게스트에서 실행), 그리고 unflatten된 트리의 노드·속성 수가 FDT와 맞는지.

```sh
# 게스트 스크립트 g_pl011node.sh
N=/sys/firmware/devicetree/base/pl011@9000000
ls $N | xargs
tr "\0" " " < $N/compatible; echo
hexdump -C $N/reg
hexdump -C $N/interrupts
/d1_fdthdr                 # 인자 없음 → /sys/firmware/fdt
/d1_fdtwalk
find /proc/device-tree/ -type d | wc -l
find /proc/device-tree/ -type f | wc -l
find /proc/device-tree/ -type f -name name | wc -l
```

```text
lab initramfs: 6.18.55 up 1.61s
$ ls $N
clock-names clocks compatible interrupts name reg
$ tr "\0" " " < $N/compatible
arm,pl011 arm,primecell 
$ hexdump -C $N/reg
00000000  00 00 00 00 09 00 00 00  00 00 00 00 00 00 10 00  |................|
00000010
$ hexdump -C $N/interrupts
00000000  00 00 00 00 00 00 00 01  00 00 00 04              |............|
0000000c
$ /d1_fdthdr
magic             0xd00dfeed (OK: d00dfeed)
totalsize         1048576 bytes
off_mem_rsvmap    0x30
off_dt_struct     0x40  (size 7152)
off_dt_strings    0x1c30  (size 507)
version           17 (last_comp 16)
boot_cpuid_phys   0
$ /d1_fdtwalk
nodes=57 properties=223 nodes-with-compatible=48
$ find /proc/device-tree/ -type d | wc -l
57
$ find /proc/device-tree/ -type f | wc -l
280
$ find /proc/device-tree/ -type f -name name | wc -l
57
[    2.240439] reboot: Power down
```

출력에서 볼 것:

- `reg` 16바이트 = 주소 2셀(`00000000 09000000` → 0x0900_0000) + 크기 2셀(`00000000 00001000` → 4KB). 루트의 `#address-cells = 2`, `#size-cells = 2`가 이 해석을 정한다 (D2).
- `interrupts` 12바이트 = 3셀 `<0 1 4>` = SPI, 1번, level-high.
- 게스트의 FDT는 structure block이 7048 → 7152, strings가 462 → 507바이트로 **커졌다**. QEMU가 `/chosen`에 속성을 추가했기 때문이고, 속성 수도 221 → 223(+`bootargs`, +initrd 2개, −`rng-seed`)이다.
- unflatten된 트리는 디렉터리 57개 = FDT 노드 57개로 정확히 일치한다. 파일은 280개 = 223 + 57인데, 커널이 노드마다 옛 Open Firmware 호환용 `name` 속성을 하나씩 만들어 붙이기 때문이다 (FDT에는 없다).

### 5.4 예제 9 — DT의 인터럽트가 `/proc/interrupts`가 되기까지

무엇을 확인하나: DT의 `<0 1 4>`가 커널에서 어떤 번호가 되는지, 그리고 DT 배선(SPI, GPIO)과 PCIe가 스스로 협상한 MSI-X의 차이.

```sh
# 게스트: cat /proc/interrupts
```

```text
lab initramfs: 6.18.55 up 9.99s
$ cat /proc/interrupts
           CPU0       
 11:       1195 GIC-0  27 Level     arch_timer
 13:          0 GIC-0  33 Level     uart-pl011
 16:          0 GICv2m-PCI-MSIX-0000:00:01.0   0 Edge      virtio0-config
 17:          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
 18:          0 GICv2m-PCI-MSIX-0000:00:01.0   2 Edge      virtio0-output.0
 19:          0 GIC-0  34 Level     rtc-pl031
 20:          0 GIC-0  23 Level     arm-pmu
 21:          0 9030000.pl061   3 Edge      GPIO Key Poweroff
IPI0:         0       Rescheduling interrupts
IPI1:         0       Function call interrupts
IPI2:         0       CPU stop interrupts
IPI3:         0       CPU stop NMIs
IPI4:         0       Timer broadcast interrupts
IPI5:         0       IRQ work interrupts
IPI6:         0       CPU backtrace interrupts
IPI7:         0       KGDB roundup interrupts
Err:          0
[   12.333535] reboot: Power down
```

출력에서 볼 것: 한 줄에 번호가 두 개다. 왼쪽 `13`은 커널 내부의 가상 IRQ 번호(virq), `GIC-0 33`은 하드웨어 번호(hwirq)다. DT의 SPI 1 → GIC hwirq 33(SPI는 32부터), RTC의 SPI 2 → 34. `arch_timer`의 27은 PPI 11(PPI는 16부터) — DT `/timer`의 `<1 0x0b ...>` 항목이다. `GPIO Key Poweroff`는 `/gpio-keys/poweroff`의 `gpios = <pl061 3 0>`에서 왔다: GIC가 아니라 **GPIO 컨트롤러가 인터럽트 부모**다. 반면 virtio-net의 MSI-X 3줄은 DT에 한 글자도 없다 — PCI capability로 장치가 스스로 알린 것이다. Don이 펌웨어에서 `NVIC_EnableIRQ(33)`처럼 다루던 번호를, Linux에서는 DT가 hwirq로 적고 irq domain이 virq로 바꿔 드라이버에 준다 (H 모듈).

### 5.5 예제 10 — DT로 보드 메모리 맵 만들기

무엇을 확인하나: `/proc/device-tree`를 유저 공간 C로 읽어 Don에게 익숙한 "주소 → 장치" 메모리 맵을 만든다. `#address-cells`/`#size-cells`를 읽어서 `reg`를 해석하는 연습이기도 하다.

```c
/* d1_regmap: /proc/device-tree 최상위 노드의 reg를 읽어 "DT가 말하는 메모리 맵"을 만든다 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <arpa/inet.h>

#define DT "/proc/device-tree"

static int slurp(const char *node, const char *prop, void *buf, int max)
{
    char path[512];
    snprintf(path, sizeof path, DT "/%s/%s", node, prop);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;                       /* 속성 없음 */
    int n = (int)fread(buf, 1, max, f);
    fclose(f);
    return n;
}

int main(void)
{
    uint32_t cells[2], reg[16];
    if (slurp("", "#address-cells", cells, 4) != 4 ||
        slurp("", "#size-cells", cells + 1, 4) != 4) { perror("cells"); exit(1); }
    int ac = ntohl(cells[0]), sc = ntohl(cells[1]);   /* 루트에서 둘 다 2 */
    printf("root: #address-cells=%d #size-cells=%d\n", ac, sc);
    DIR *d = opendir(DT);
    if (!d) { perror(DT); exit(1); }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char compat[128] = "-";
        if (e->d_type != DT_DIR || e->d_name[0] == '.') continue;
        int n = slurp(e->d_name, "reg", reg, sizeof reg);
        if (n < 4 * (ac + sc)) continue;     /* reg 없는 노드는 건너뜀 */
        if (slurp(e->d_name, "compatible", compat, sizeof compat - 1) < 0) strcpy(compat, "-");
        compat[sizeof compat - 1] = '\0';
        uint64_t addr = 0, size = 0;
        for (int i = 0; i < ac; i++) addr = addr << 32 | ntohl(reg[i]);
        for (int i = 0; i < sc; i++) size = size << 32 | ntohl(reg[ac + i]);
        printf("0x%010llx +0x%08llx  %-24s %s\n", (unsigned long long)addr,
               (unsigned long long)size, e->d_name, compat);
    }
    closedir(d);
    return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static ~/.elinux_scratch/D1/d1_regmap.c -o d1_regmap   # 경고 0, initramfs에 넣음
# 게스트: virtio 슬롯 28개는 grep으로 생략
/d1_regmap | sort | grep -v 'virtio_mmio@a00[0-3][1-9a-f]'
```

```text
lab initramfs: 6.18.55 up 5.98s
$ /d1_regmap | sort
0x0000000000 +0x04000000  flash@0                  cfi-flash
0x0008000000 +0x00010000  intc@8000000             arm,cortex-a15-gic
0x0009000000 +0x00001000  pl011@9000000            arm,pl011
0x0009010000 +0x00001000  pl031@9010000            arm,pl031
0x0009020000 +0x00000018  fw-cfg@9020000           qemu,fw-cfg-mmio
0x0009030000 +0x00001000  pl061@9030000            arm,pl061
0x000a000000 +0x00000200  virtio_mmio@a000000      virtio,mmio
0x000a001000 +0x00000200  virtio_mmio@a001000      virtio,mmio
0x000a002000 +0x00000200  virtio_mmio@a002000      virtio,mmio
0x000a003000 +0x00000200  virtio_mmio@a003000      virtio,mmio
0x0040000000 +0x20000000  memory@40000000          -
0x4010000000 +0x10000000  pcie@10000000            pci-host-ecam-generic
root: #address-cells=2 #size-cells=2
[    7.589642] reboot: Power down
```

출력에서 볼 것: SSD 펌웨어의 `memory_map.h`와 같은 정보가 DT에서 나온다. `flash@0`과 `intc@8000000`은 `reg`에 영역이 2개 있지만 이 프로그램은 첫 번째만 출력한다. 재미있는 점: `pcie@10000000` 노드의 이름(unit-address 0x1000_0000)과 실제 `reg`(0x40_1000_0000, 4GB 위의 "highmem ECAM")가 다르다 — QEMU가 생성한 DT의 특이점이다. 커널은 이름이 아니라 `reg`를 믿고, 그래서 §6의 platform device 이름이 `4010000000.pcie`가 된다.

---

## 6. DT 노드 ↔ platform_device ↔ driver — sysfs로 고리 확인하기

### 6.1 직관 — 세 개의 객체, 두 개의 포인터

DT 노드는 **데이터**일 뿐 그 자체로는 아무 일도 안 한다. 커널은 노드마다(조건이 맞으면) **디바이스 객체**를 만들고, 디바이스는 노드를 가리키는 포인터(`dev->of_node`)를 갖는다. 드라이버가 등록되면 버스가 디바이스와 드라이버를 짝짓고(`compatible` ↔ `of_match_table`, 또는 AMBA ID), 성공하면 `dev->driver`가 채워지고 `probe()`가 불린다. sysfs는 이 두 포인터를 심볼릭 링크로 보여 준다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" font-size="13" font-weight="bold">커널 객체 (메모리)</text>
  <rect x="10" y="30" width="200" height="86" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="50" text-anchor="middle" font-size="13" font-weight="bold">struct device_node</text>
  <text x="20" y="72" font-size="12">/pl011@9000000</text>
  <text x="20" y="90" font-size="12">compatible, reg, interrupts</text>
  <text x="20" y="108" font-size="12">(DT에서 unflatten)</text>
  <rect x="240" y="30" width="200" height="86" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="50" text-anchor="middle" font-size="13" font-weight="bold">struct amba_device</text>
  <text x="250" y="72" font-size="12">9000000.pl011</text>
  <text x="250" y="90" font-size="12">res: 0x9000000, irq 13</text>
  <text x="250" y="108" font-size="12">periphid 0x00141011</text>
  <rect x="470" y="30" width="200" height="86" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="50" text-anchor="middle" font-size="13" font-weight="bold">struct amba_driver</text>
  <text x="480" y="72" font-size="12">uart-pl011</text>
  <text x="480" y="90" font-size="12">id_table (AMBA ID)</text>
  <text x="480" y="108" font-size="12">.probe = pl011_probe</text>
  <line x1="240" y1="60" x2="212" y2="60" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="226" y="135" text-anchor="middle" font-size="12">dev.of_node</text>
  <line x1="440" y1="60" x2="468" y2="60" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="454" y="135" text-anchor="middle" font-size="12">dev.driver</text>
  <line x1="110" y1="116" x2="110" y2="196" stroke="#888" stroke-dasharray="4 3"/>
  <line x1="340" y1="116" x2="340" y2="196" stroke="#888" stroke-dasharray="4 3"/>
  <line x1="570" y1="116" x2="570" y2="196" stroke="#888" stroke-dasharray="4 3"/>
  <text x="10" y="186" font-size="13" font-weight="bold">sysfs (파일 시스템)</text>
  <rect x="10" y="198" width="200" height="54" rx="5" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b"/>
  <text x="20" y="220" font-size="12">/sys/firmware/devicetree/</text>
  <text x="20" y="240" font-size="12">base/pl011@9000000/</text>
  <rect x="240" y="198" width="200" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
  <text x="250" y="220" font-size="12">/sys/devices/platform/</text>
  <text x="250" y="240" font-size="12">9000000.pl011/</text>
  <rect x="470" y="198" width="200" height="54" rx="5" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c"/>
  <text x="480" y="220" font-size="12">/sys/bus/amba/drivers/</text>
  <text x="480" y="240" font-size="12">uart-pl011/</text>
  <line x1="240" y1="225" x2="212" y2="225" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="440" y1="225" x2="468" y2="225" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="226" y="272" text-anchor="middle" font-size="12">of_node 링크</text>
  <text x="454" y="272" text-anchor="middle" font-size="12">driver 링크</text>
  <rect x="140" y="290" width="400" height="58" rx="5" fill="#888" fill-opacity="0.08" stroke="#888"/>
  <text x="150" y="310" font-size="12">uevent: OF_FULLNAME=/pl011@9000000</text>
  <text x="150" y="328" font-size="12">OF_COMPATIBLE_0=arm,pl011  AMBA_ID=00141011</text>
  <text x="150" y="344" font-size="12">MODALIAS → udev/modprobe가 모듈을 고르는 열쇠</text>
</svg>
```
그림 4 — 위 줄은 커널 메모리 안의 세 객체와 두 포인터, 아래 줄은 그것이 sysfs에 비친 모습이다. 디바이스 디렉터리 안의 `of_node`·`driver` 심볼릭 링크가 곧 `dev->of_node`·`dev->driver` 포인터다.

### 6.2 예제 11 — `of_node`·`driver` 링크와 uevent 따라가기

무엇을 확인하나: 그림 4의 고리를 UART(amba)와 전원 버튼(platform) 두 장치에서 실제로 따라간다.

```sh
# 게스트 스크립트 g_link.sh
D=/sys/bus/amba/devices/9000000.pl011
ls -l /sys/bus/amba/devices/ | sed 's/.*:[0-9][0-9] //'      # 날짜 열까지 잘라냄
ls -l $D/ | grep -- '->' | sed 's/.*:[0-9][0-9] //'          # 링크만
readlink -f $D/of_node
readlink -f $D/driver
cat $D/uevent
ls $D/tty
P=/sys/bus/platform/devices/gpio-keys
cat $P/uevent
cat $P/modalias; echo
readlink -f $P/driver
```

```text
lab initramfs: 6.18.55 up 1.31s
$ ls -l /sys/bus/amba/devices/
total 0
9000000.pl011 -> ../../../devices/platform/9000000.pl011
9010000.pl031 -> ../../../devices/platform/9010000.pl031
9030000.pl061 -> ../../../devices/platform/9030000.pl061
$ ls -l $D/ (links only)
driver -> ../../../bus/amba/drivers/uart-pl011
of_node -> ../../../firmware/devicetree/base/pl011@9000000
subsystem -> ../../../bus/amba
$ readlink -f $D/of_node
/sys/firmware/devicetree/base/pl011@9000000
$ readlink -f $D/driver
/sys/bus/amba/drivers/uart-pl011
$ cat $D/uevent
DRIVER=uart-pl011
OF_NAME=pl011
OF_FULLNAME=/pl011@9000000
OF_COMPATIBLE_0=arm,pl011
OF_COMPATIBLE_1=arm,primecell
OF_COMPATIBLE_N=2
OF_ALIAS_0=serial0
AMBA_ID=00141011
MODALIAS=amba:d00141011
$ ls $D/tty
ls: /sys/bus/amba/devices/9000000.pl011/tty: No such file or directory
$ cat $P/uevent
DRIVER=gpio-keys
OF_NAME=gpio-keys
OF_FULLNAME=/gpio-keys
OF_COMPATIBLE_0=gpio-keys
OF_COMPATIBLE_N=1
MODALIAS=of:Ngpio-keysT(null)Cgpio-keys
$ cat $P/modalias
of:Ngpio-keysT(null)Cgpio-keys

$ readlink -f $P/driver
/sys/bus/platform/drivers/gpio-keys
```

출력에서 볼 것:

- **세 고리가 다 보인다**: `of_node` → DT 노드, `driver` → 드라이버, `subsystem` → 버스. 디바이스 이름 `9000000.pl011`은 "`reg` 주소 + 노드 이름"으로 커널이 지은 것이다.
- uevent의 `OF_*` 줄은 DT에서 왔고(`OF_ALIAS_0=serial0`은 `/aliases`에서), `AMBA_ID`는 **하드웨어 레지스터에서 읽은 값**이다(§2.3). 그래서 MODALIAS가 `of:...`가 아니라 `amba:d00141011`이다.
- platform 장치인 `gpio-keys`의 MODALIAS는 `of:N<이름>T<device_type>C<compatible>` 형식이다. 드라이버가 모듈(`.ko`)이면 udev가 이 문자열로 `modprobe`를 불러 자동 로드한다 (§13.2).
- `ls $D/tty`가 실패한 것도 배울 점이다. 6.x 커널에서 `ttyAMA0`은 UART 디바이스 바로 밑이 아니라 serial core가 만든 포트 디바이스 아래 `9000000.pl011:0/9000000.pl011:0.0/tty/ttyAMA0`에 있다. 경로를 외우지 말고 `find`로 찾자.

```sh
# 게스트
find /sys/devices/platform/9000000.pl011 -name "ttyAMA*"
```

```text
/sys/devices/platform/9000000.pl011/9000000.pl011:0/9000000.pl011:0.0/tty/ttyAMA0
```

### 6.3 예제 12 — 모든 디바이스의 DT 노드와 드라이버를 한 표로 (C)

무엇을 확인하나: 버스의 모든 디바이스에 대해 두 링크를 `readlink()`로 따라가는 C 프로그램. 보드 bring-up 때 "DT에는 있는데 드라이버가 안 붙은 장치"를 한눈에 찾는 도구가 된다.

```c
/* d1_devmap: 버스의 각 디바이스에 대해 DT 노드(of_node)와 드라이버(driver) 링크를 따라간다 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>

static void link_target(const char *dir, const char *name, char *out, size_t sz, int base)
{
    char path[PATH_MAX], tgt[PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    ssize_t n = readlink(path, tgt, sizeof tgt - 1);
    if (n < 0) { snprintf(out, sz, "-"); return; }   /* 링크 없음 */
    tgt[n] = '\0';
    const char *s = tgt;
    if (base) { const char *slash = strrchr(tgt, '/'); s = slash ? slash + 1 : tgt; }
    else if ((s = strstr(tgt, "devicetree/base")) != NULL) s += strlen("devicetree/base");
    else s = tgt;
    if (!s[0]) s = "/";
    size_t l = strlen(s);
    if (l >= sz) l = sz - 1;
    memcpy(out, s, l);
    out[l] = '\0';
}

int main(int argc, char **argv)
{
    const char *bus = argc > 1 ? argv[1] : "platform";
    char dir[64], devdir[PATH_MAX], node[256], drv[128];
    snprintf(dir, sizeof dir, "/sys/bus/%s/devices", bus);
    DIR *d = opendir(dir);
    if (!d) { perror(dir); exit(1); }
    struct dirent *e;
    printf("%-28s %-26s %s\n", "DEVICE", "of_node -> DT path", "driver");
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(devdir, sizeof devdir, "%s/%s", dir, e->d_name);
        link_target(devdir, "of_node", node, sizeof node, 0);
        link_target(devdir, "driver", drv, sizeof drv, 1);
        printf("%-28s %-26s %s\n", e->d_name, node, drv);
    }
    closedir(d);
    return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static ~/.elinux_scratch/D1/d1_devmap.c -o d1_devmap   # 경고 0
# 게스트 (virtio_mmio 28줄은 grep으로 생략)
/d1_devmap platform | grep -v 'virtio_mmio  *\/virtio_mmio@a00[0-3][2-9a-e]'
/d1_devmap amba
ls /sys/bus/virtio/devices | xargs
```

```text
lab initramfs: 6.18.55 up 1.98s
$ /d1_devmap platform
DEVICE                       of_node -> DT path         driver
alarmtimer.0.auto            -                          alarmtimer
0.flash                      /flash@0                   physmap-flash
a003000.virtio_mmio          /virtio_mmio@a003000       -
a002000.virtio_mmio          /virtio_mmio@a002000       -
psci                         /psci                      psci-cpuidle-domain
a001000.virtio_mmio          /virtio_mmio@a001000       -
gpio-keys                    /gpio-keys                 gpio-keys
timer                        /timer                     -
a000000.virtio_mmio          /virtio_mmio@a000000       -
psci-cpuidle                 -                          -
9020000.fw-cfg               /fw-cfg@9020000            -
platform-bus@c000000         /platform-bus@c000000      -
serial8250                   -                          serial8250
4010000000.pcie              /pcie@10000000             pci-host-generic
pmu                          /pmu                       armv8-pmu
$ /d1_devmap amba
DEVICE                       of_node -> DT path         driver
9000000.pl011                /pl011@9000000             uart-pl011
9030000.pl061                /pl061@9030000             pl061_gpio
9010000.pl031                /pl031@9010000             rtc-pl031
$ ls /sys/bus/virtio/devices
virtio0
```

이 표를 네 부류로 나눠 읽는 것이 이 노트의 핵심 연습이다.

| 부류 | 예 | 왜 그런가 |
|---|---|---|
| DT 노드 있음 + 드라이버 붙음 | `9000000.pl011`, `gpio-keys`, `0.flash`, `4010000000.pcie`, `pmu` | 정상. `compatible`(또는 AMBA ID)이 드라이버와 맞았고 `probe()` 성공 |
| DT 노드 있음 + 드라이버 없음 | `a00xxxx.virtio_mmio`, `9020000.fw-cfg`, `timer`, `platform-bus@c000000` | virtio 빈 슬롯은 `virtio_mmio` 드라이버가 device ID 0을 보고 `-ENODEV`로 거절했다(`drivers/virtio/virtio_mmio.c`: "virtio-mmio device with an ID 0 is a (dummy) placeholder"). fw-cfg는 이 커널에서 `CONFIG_FW_CFG_SYSFS`가 꺼져 있다. `timer`는 이미 early init(`TIMER_OF_DECLARE`)이 썼고 platform 드라이버가 따로 없다 |
| DT 노드 없음 (코드가 만든 디바이스) | `alarmtimer.0.auto`, `psci-cpuidle`, `serial8250` | DT와 무관하게 커널 코드가 `platform_device_register*()`로 만든 것. platform 버스는 "DT 전용"이 아니다 — board file 시대의 유산이 이렇게 남아 있다 |
| DT 노드는 있는데 디바이스 자체가 없음 | `/intc@8000000`, `/apb-pclk`, `/cpus`, `/memory@...`, `/chosen`, `/aliases` | `compatible`이 없거나(`/memory`, `/chosen`, `/aliases`, `/cpus`), 이미 early init이 차지했다(GIC는 `IRQCHIP_DECLARE`, fixed-clock은 `CLK_OF_DECLARE` — 둘 다 노드에 `OF_POPULATED` 표시를 남겨 populate가 건너뛴다) |

마지막으로 `virtio0`은 platform 쪽 `virtio_mmio`가 아니라 **PCI** 쪽 장치다(§7에서 보듯 QEMU가 기본으로 넣는 virtio-net-pci). 이 표 하나로 "DT 노드 → 디바이스 → 드라이버" 사슬의 어느 고리가 끊겼는지 가를 수 있다.

### 6.4 예제 13 — MODALIAS와 모듈 별명: IMU 드라이버는 무엇으로 불리나

무엇을 확인하나: 센서 키트에 흔한 ST LSM6DSx IMU 드라이버 모듈이 어떤 "이름표"를 달고 있는지. 모듈의 `alias`가 uevent의 `MODALIAS`와 glob 매칭되면 udev가 그 모듈을 자동으로 올린다.

```sh
cd /var/lab/build/linux      # 랩 커널의 빌드 트리 (모듈 빌드됨)
modinfo -F alias drivers/iio/imu/st_lsm6dsx/st_lsm6dsx_i2c.ko | head -6
modinfo -F alias drivers/iio/imu/st_lsm6dsx/st_lsm6dsx_i2c.ko | wc -l
modinfo -F alias drivers/iio/imu/st_lsm6dsx/st_lsm6dsx_i2c.ko | cut -d: -f1 | sort | uniq -c
```

```text
of:N*T*Cst,asm330lhhxg1C*
of:N*T*Cst,asm330lhhxg1
of:N*T*Cst,asm330lhbC*
of:N*T*Cst,asm330lhb
of:N*T*Cst,ism330isC*
of:N*T*Cst,ism330is
73
      1 acpi*
     24 i2c
     48 of
```

출력에서 볼 것: 한 IMU 드라이버가 **세 종류의 이름표**를 단다 — DT용 `of:` 48개(칩 24종 × 2형식), board file/수동 생성용 `i2c` 24개, ACPI용 1개. §3.5의 smc91x와 같은 구조다. 드라이버 소스의 `MODULE_DEVICE_TABLE(of, ...)`가 이 `of:` 별명을 만든다 (E 모듈, D3).

---

## 7. 대조: PCI 장치에는 DT 노드가 없다

### 7.1 직관

§1에서 "PCIe는 스스로 말한다"고 했다. 그렇다면 랩에 PCI 장치를 더 꽂아도 **DT는 한 글자도 바뀌지 않아야** 하고, 커널은 그 장치를 enumeration으로 찾아야 하며, 그 장치의 `of_node`는 없어야 한다. DT가 아는 것은 host bridge 하나뿐이다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="30" width="300" height="270" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
  <text x="160" y="22" text-anchor="middle" font-size="13" font-weight="bold">DT가 말해 준 것 (57 노드, 그대로)</text>
  <text x="25" y="56" font-size="12">/</text>
  <text x="45" y="80" font-size="12">pl011@9000000</text>
  <text x="45" y="104" font-size="12">pl061@9030000</text>
  <text x="45" y="128" font-size="12">intc@8000000</text>
  <text x="45" y="152" font-size="12">... (virtio_mmio x32 등)</text>
  <rect x="38" y="168" width="200" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="48" y="188" font-size="12">pcie@10000000 (host bridge)</text>
  <text x="25" y="226" font-size="12">ECAM 0x40_1000_0000</text>
  <text x="25" y="246" font-size="12">ranges (I/O, MEM 창)</text>
  <text x="25" y="266" font-size="12">interrupt-map (INTx → GIC)</text>
  <text x="25" y="286" font-size="12">msi-map → v2m</text>
  <rect x="370" y="30" width="300" height="270" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b"/>
  <text x="520" y="22" text-anchor="middle" font-size="13" font-weight="bold">커널이 enumeration으로 찾은 것</text>
  <rect x="385" y="44" width="270" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="395" y="63" font-size="12">platform: 4010000000.pcie</text>
  <text x="395" y="96" font-size="12">pci0000:00 (root bus)</text>
  <rect x="400" y="110" width="250" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="128" font-size="12">00:00.0 1b36:0008 host bridge</text>
  <rect x="400" y="144" width="250" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="162" font-size="12">00:01.0 1af4:1000 virtio-net</text>
  <rect x="400" y="178" width="250" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="196" font-size="12">00:02.0 8086:100e e1000</text>
  <rect x="400" y="212" width="250" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="230" font-size="12">00:03.0 1af4:1005 virtio-rng</text>
  <text x="400" y="266" font-size="12">of_node: 전부 없음</text>
  <text x="400" y="286" font-size="12">ID·BAR·MSI-X는 config space에서</text>
  <line x1="238" y1="183" x2="383" y2="60" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="340" y="104" text-anchor="middle" font-size="12">of_node</text>
</svg>
```
그림 5 — DT는 출발점(host bridge의 ECAM 주소, 주소 창, 인터럽트 매핑)만 준다. 그 아래 장치들은 커널이 config space를 읽어 스스로 찾는다.

### 7.2 예제 14 — `-device e1000 -device virtio-rng-pci`로 장치를 꽂고 config space 읽기

무엇을 확인하나: QEMU에 PCI 장치 두 개를 더 꽂고, 게스트에서 BusyBox `lspci`, sysfs, 그리고 config space를 직접 해석하는 C 프로그램으로 들여다본다. 각 장치에 `of_node`가 있는지도 본다.

```c
/* d1_pcicfg: sysfs의 PCI config space를 읽어 헤더를 해석하고, DT 노드가 있는지 본다 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

int main(void)
{
    const char *dir = "/sys/bus/pci/devices";
    DIR *d = opendir(dir);
    if (!d) { perror(dir); exit(1); }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[512];
        uint8_t c[64];
        snprintf(path, sizeof path, "%s/%s/config", dir, e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) { perror(path); exit(1); }
        if (fread(c, 1, sizeof c, f) != sizeof c) { perror("fread"); exit(1); }
        fclose(f);
        uint16_t ven = c[0] | c[1] << 8, dev = c[2] | c[3] << 8;  /* config는 LE */
        printf("%s  ven=%04x dev=%04x class=%02x%02x%02x hdr=%u\n", e->d_name,
               ven, dev, c[0x0b], c[0x0a], c[0x09], c[0x0e] & 0x7f);
        for (int i = 0; i < 6 && (c[0x0e] & 0x7f) == 0; i++) {
            uint32_t lo, hi = 0;
            memcpy(&lo, c + 0x10 + 4 * i, 4);
            if (!lo) continue;
            if (lo & 1) { printf("    BAR%d  I/O  0x%x\n", i, lo & ~3u); continue; }
            if ((lo & 6) == 4 && i < 5) memcpy(&hi, c + 0x10 + 4 * ++i, 4);  /* 64-bit BAR */
            printf("    BAR%d  MEM  0x%llx%s\n", (lo & 6) == 4 ? i - 1 : i,
                   ((unsigned long long)hi << 32) | (lo & ~0xfu),
                   (lo & 6) == 4 ? " (64-bit)" : "");
        }
        snprintf(path, sizeof path, "%s/%s/of_node", dir, e->d_name);
        printf("    of_node: %s\n", access(path, F_OK) == 0 ? "YES" : "none");
    }
    closedir(d);
    return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static ~/.elinux_scratch/D1/d1_pcicfg.c -o d1_pcicfg   # 경고 0
qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel /var/lab/out/Image \
  -initrd d1_initrd.gz -append "console=ttyAMA0 quiet LAB_CMD=sh~/g_pci.sh" \
  -device e1000 -device virtio-rng-pci
```

```text
lab initramfs: 6.18.55 up 2.40s
$ lspci
00:01.0 Class 0200: 1af4:1000
00:00.0 Class 0600: 1b36:0008
00:03.0 Class 00ff: 1af4:1005
00:02.0 Class 0200: 8086:100e
$ ls /sys/bus/pci/devices
0000:00:00.0 0000:00:01.0 0000:00:02.0 0000:00:03.0
$ /d1_pcicfg
0000:00:01.0  ven=1af4 dev=1000 class=020000 hdr=0
    BAR0  I/O  0x1040
    BAR1  MEM  0x10120000
    BAR4  MEM  0x8000000000 (64-bit)
    of_node: none
0000:00:00.0  ven=1b36 dev=0008 class=060000 hdr=0
    of_node: none
0000:00:03.0  ven=1af4 dev=1005 class=00ff00 hdr=0
    BAR0  I/O  0x1060
    BAR1  MEM  0x10121000
    BAR4  MEM  0x8000004000 (64-bit)
    of_node: none
0000:00:02.0  ven=8086 dev=100e class=020000 hdr=0
    BAR0  MEM  0x10100000
    BAR1  I/O  0x1000
    of_node: none
$ readlink -f /sys/bus/pci/devices/0000:00:00.0
/sys/devices/platform/4010000000.pcie/pci0000:00/0000:00:00.0
$ readlink -f /sys/bus/pci/devices/0000:00:01.0
/sys/devices/platform/4010000000.pcie/pci0000:00/0000:00:01.0
$ readlink -f /sys/bus/pci/devices/0000:00:02.0
/sys/devices/platform/4010000000.pcie/pci0000:00/0000:00:02.0
$ readlink -f /sys/bus/pci/devices/0000:00:03.0
/sys/devices/platform/4010000000.pcie/pci0000:00/0000:00:03.0
$ ls -l <root bus>/ of_node?
(root bus dir: /sys/devices/platform/4010000000.pcie/pci0000:00)
$ readlink -f /sys/bus/platform/devices/4010000000.pcie/of_node
/sys/firmware/devicetree/base/pcie@10000000
$ cat /sys/bus/pci/devices/0000:00:01.0/uevent
DRIVER=virtio-pci
PCI_CLASS=20000
PCI_ID=1AF4:1000
PCI_SUBSYS_ID=1AF4:0001
PCI_SLOT_NAME=0000:00:01.0
MODALIAS=pci:v00001AF4d00001000sv00001AF4sd00000001bc02sc00i00
$ readlink /sys/bus/pci/devices/0000:00:01.0/driver
../../../../../bus/pci/drivers/virtio-pci
$ cat /sys/bus/pci/devices/0000:00:02.0/uevent
DRIVER=e1000
PCI_CLASS=20000
PCI_ID=8086:100E
PCI_SUBSYS_ID=1AF4:1100
PCI_SLOT_NAME=0000:00:02.0
MODALIAS=pci:v00008086d0000100Esv00001AF4sd00001100bc02sc00i00
$ readlink /sys/bus/pci/devices/0000:00:02.0/driver
../../../../../bus/pci/drivers/e1000
$ hexdump -C /sys/bus/pci/devices/0000:00:01.0/config | head -4
00000000  f4 1a 00 10 07 04 10 00  00 00 00 02 00 00 00 00  |................|
00000010  41 10 00 00 00 00 12 10  00 00 00 00 00 00 00 00  |A...............|
00000020  0c 00 00 00 80 00 00 00  00 00 00 00 f4 1a 01 00  |................|
00000030  00 00 00 00 98 00 00 00  00 00 00 00 0f 01 00 00  |................|
[    3.244551] reboot: Power down
```

출력에서 볼 것:

- 우리가 꽂은 `8086:100e`(e1000)와 `1af4:1005`(virtio-rng) 외에 `1af4:1000`(virtio-net)이 있다 — QEMU가 `-nic`을 따로 안 주면 기본으로 넣는 NIC이다. `1b36:0008`은 QEMU의 PCIe host bridge 기능(function)이다.
- **네 장치 모두 `of_node: none`**. root bus(`pci0000:00`) 디렉터리에도 `of_node` 링크가 없다(grep 결과 빈 줄). DT 노드를 가진 것은 그 위의 platform 디바이스 `4010000000.pcie` → `/pcie@10000000` 하나뿐이다.
- config space 첫 줄 `f4 1a 00 10`은 **little-endian**으로 Vendor 0x1af4, Device 0x1000이다. 같은 게스트의 DT 속성은 big-endian이었다(§5.3). 오프셋 0x20의 `0c 00 00 00` + 0x24의 `80 00 00 00`이 64비트 prefetchable BAR4 = `0x80_0000_0000` — DT의 `ranges`가 열어 준 64비트 MMIO 창 안에 커널이 배치했다.
- uevent의 `MODALIAS=pci:v...d...`는 DT의 `of:N...C...`와 같은 역할이다. 매칭 키가 compatible 문자열이냐 Vendor/Device ID냐의 차이일 뿐 드라이버 모델은 같다.

예외도 알아 두자. 보드에 납땜된 PCIe 칩(예: 이더넷 MAC)에 MAC 주소나 리셋 GPIO처럼 **물어볼 수 없는 정보**를 줘야 할 때는 DT의 `pcie` 노드 아래 자식 노드를 bus/device/function 주소로 적어 연결할 수 있다. 또 6.x에는 `CONFIG_PCI_DYNAMIC_OF_NODES`(이 랩 커널에서 `=y`)가 있어 PCI **브리지**와 일부 quirk 장치에 DT 노드를 동적으로 만들어 준다 — 대표적인 예가 Raspberry Pi 5의 RP1처럼 "PCIe로 붙지만 내부에 UART·I2C·GPIO 같은 non-discoverable 장치를 잔뜩 품은 칩"이다(`drivers/pci/quirks.c`에 `PCI_DEVICE_ID_RPI_RP1_C0`용 `of_pci_make_dev_node` fixup이 있다). 우리 랩에는 브리지가 없어서 노드가 생기지 않았다.

### 7.3 예제 15 — PCI 장치를 꽂아도 DT는 그대로

무엇을 확인하나: 장치 2개를 추가한 부팅에서도 커널이 받은 FDT의 노드 수가 같고, `pcie` 노드에 자식이 없다는 것.

```sh
# 게스트 (같은 -device e1000 -device virtio-rng-pci 부팅)
/d1_fdtwalk
ls /proc/device-tree/pcie@10000000 | xargs -n 5
```

```text
lab initramfs: 6.18.55 up 5.52s
$ /d1_fdtwalk
nodes=57 properties=223 nodes-with-compatible=48
$ ls /proc/device-tree/pcie@10000000
#address-cells #interrupt-cells #size-cells bus-range compatible
device_type dma-coherent interrupt-map interrupt-map-mask linux,pci-domain
msi-map name ranges reg
[    7.152140] reboot: Power down
```

출력에서 볼 것: 노드 57개, 속성 223개 — §5.3의 장치 없는 부팅과 **완전히 같다**. `pcie@10000000` 디렉터리에는 속성 파일만 있고 하위 디렉터리(자식 노드)가 없다. 하드웨어 구성이 바뀌었는데 DT가 안 바뀌어도 되는 것 — 그것이 discoverable의 정의다.

---

## 8. DT가 아닌 것 — 코드도 아니고 정책도 아니다

### 8.1 DT는 코드가 아니다

DT에는 조건문도 반복문도 함수 호출도 없다. 순수한 **선언형 데이터**다. "리셋 핀을 10ms 내렸다가 올려라" 같은 절차는 DT에 적지 않는다 — DT에는 "리셋 핀은 GPIO1-5, active-low"(`reset-gpios = <&gpio1 5 GPIO_ACTIVE_LOW>`)라는 **사실**만 적고, 절차(얼마나 내릴지)는 드라이버 코드가 칩 데이터시트를 보고 정한다. (데이터시트마다 다른 타이밍을 드라이버가 몰라서 속성으로 받는 경우 — 예: `reset-assert-us` — 도 있지만, 그것도 "하드웨어 사실"의 범주로 binding에 정의된 것이다.)

이 점이 ACPI와 가장 크게 다르다. ACPI의 DSDT에는 **AML 바이트코드**(메서드 `_ON`, `_OFF`, `_STA` …)가 들어 있고 커널의 AML 인터프리터가 그걸 **실행**한다. 펌웨어가 OS에 코드를 준다. DT는 그러지 않는다 (§9).

### 8.2 DT는 드라이버 동작 정책이 아니다

DT 메인테이너들이 리뷰에서 가장 자주 하는 말이 "**DT describes hardware, not configuration**"이다. 판단 기준은 "이 값이 **보드의 물리적 사실**인가, 아니면 **소프트웨어가 어떻게 동작할지에 대한 선택**인가"다.

| DT에 적어도 되는 것 (하드웨어 사실) | DT에 적으면 안 되는 것 (정책·설정) | 정책은 어디로 가나 |
|---|---|---|
| IMU가 I2C3의 0x6a에 있다 | IMU 샘플링 주파수 기본값을 400Hz로 | sysfs/IIO 속성, 유저 공간 설정 |
| UART에 RTS/CTS 선이 배선돼 있다(`uart-has-rtscts`) | 이 UART의 RX 버퍼를 4KB로 | 드라이버 기본값, module parameter |
| 센서 장착 방향 행렬(`mount-matrix`) | 이 드라이버의 debug 로그 레벨 | dynamic debug, `loglevel` |
| 크리스털이 24MHz다(`clock-frequency`) | CPU 거버너를 performance로 | sysfs cpufreq, 부팅 스크립트 |
| 이 핀은 active-low다 | 장치가 열리면 LED를 켜라 | 유저 공간 데몬, LED trigger |

경계가 애매한 것도 있다. `/chosen`의 `bootargs`(커널 명령행)와 `stdout-path`는 명백히 설정이지만 **부트로더 → 커널 전달 통로**로서 스펙에 정의돼 있다 — "런타임 설정을 위한 예외 구역"으로 생각하면 된다. `linux,code = <116>`(전원 버튼이 KEY_POWER를 낸다)도 "이 버튼에 인쇄된 기능"이라는 하드웨어 사실로 받아들여진다.

### 8.3 `status = "disabled"`는 전원 끄기가 아니다

`status = "disabled"`는 "커널아, 이 노드로 **디바이스를 만들지 마라**"라는 뜻이다. 하드웨어 블록의 클록이나 전원을 끄는 것이 아니다. 블록은 부트로더가 켜 둔 상태 그대로 살아 있을 수 있고, 누군가(earlycon처럼) 주소를 직접 알면 여전히 쓸 수 있다. §10에서 실험으로 확인한다. 보통 SoC `.dtsi`가 모든 블록을 `disabled`로 선언하고 보드 `.dts`가 실제로 쓰는 것만 `"okay"`로 켠다 (D5).

### 8.4 DT는 드라이버가 아니다

DT 노드를 아무리 잘 써도 **드라이버가 커널에 없으면** 아무 일도 안 일어난다(§6.3의 `fw-cfg`). 반대로 드라이버만 있고 노드가 없으면 `probe()`가 불리지 않는다. 면접에서 "DT를 고쳤는데 장치가 안 떠요"라고 하면 "드라이버가 빌드됐나(`CONFIG_`), 모듈이 로드됐나, compatible이 드라이버 테이블과 글자 하나까지 같은가"를 같이 봐야 한다.

---

## 9. ACPI와 비교 — 같은 문제의 다른 답 (C8 미리보기)

### 9.1 표로 비교

| 항목 | Device Tree | ACPI |
|---|---|---|
| 출신 | Open Firmware (SPARC·PowerPC) → Linux FDT | Intel·Microsoft 등의 PC 표준, UEFI Forum 관리 |
| 주 무대 | 임베디드 ARM/arm64, RISC-V, PowerPC | x86 PC·서버, arm64 서버(SBBR/SystemReady) |
| 형태 | 선언형 데이터만 (`.dtb`) | 테이블(MADT, MCFG, GTDT …) + **실행 코드**(DSDT/SSDT의 AML) |
| 누가 만드나 | 보드 개발자(커널 트리나 BSP의 `.dts`) | 펌웨어(UEFI) 벤더 |
| 장치 식별 | `compatible = "vendor,device"` | `_HID` (예: `PNP0A08` PCIe host bridge, `ARMH0061` PL061 GPIO) |
| 전원 관리 | 드라이버가 clocks/regulators 프레임워크로 직접 | AML 메서드(`_PS0`, `_PS3`)를 OS가 호출 |
| PCIe ECAM 위치 | `pcie` 노드의 `reg` | MCFG 테이블 |
| 인터럽트 컨트롤러 | `interrupt-controller` 노드 | MADT(테이블 이름 `APIC`) |
| 커널 API | `of_*()` | `acpi_*()` — 공통 추상화는 `fwnode`/`device_property_*()` (D8) |
| Linux에서 보기 | `/proc/device-tree` | `/sys/firmware/acpi/tables`, `/sys/bus/acpi/devices` |

DT 문법을 ACPI 안에서 쓰는 다리도 있다. ACPI의 `_DSD`에 DT 스타일 속성을 넣고 `_HID`를 `PRP0001`로 주면 커널이 그 장치의 `compatible`로 DT 드라이버를 매칭한다. 그래서 드라이버를 `device_property_read_u32()` 같은 fwnode API로 쓰면 DT와 ACPI를 한 코드로 지원할 수 있다 (D8).

### 9.2 예제 16 — Lima VM은 ACPI 머신이다

무엇을 확인하나: 이 노트를 쓰는 Lima VM(Apple Virtualization 위 arm64 Ubuntu)은 UEFI + ACPI로 부팅한다. 같은 arm64인데 `/proc/device-tree`가 없다.

```sh
# Lima VM 호스트에서 (QEMU 랩 아님)
uname -r
cat /sys/class/dmi/id/product_name
ls -l /proc/device-tree
ls /sys/firmware | xargs
ls -A /sys/firmware/devicetree
sudo ls -l /sys/firmware/fdt
sudo cat /sys/firmware/fdt | dtc -I dtb -O dts 2>/dev/null
sudo ls -l /sys/firmware/acpi/tables | grep -v "^d\|total"
for d in /sys/bus/acpi/devices/*; do printf "%-12s %s\n" ${d##*/} "$(cat $d/path)"; done
ls /sys/bus/amba/devices/
readlink /sys/bus/amba/devices/ARMH0061:00/firmware_node
readlink /sys/bus/amba/devices/ARMH0061:00/driver
readlink /sys/bus/amba/devices/ARMH0061:00/of_node
```

```text
$ uname -r
7.0.0-38-generic
$ cat /sys/class/dmi/id/product_name
Apple Virtualization Generic Platform
$ ls -l /proc/device-tree
ls: cannot access '/proc/device-tree': No such file or directory
$ ls /sys/firmware | xargs
acpi devicetree dmi efi fdt
$ ls -A /sys/firmware/devicetree
$ sudo ls -l /sys/firmware/fdt
-r-------- 1 root root 560 Oct 10 10:22 /sys/firmware/fdt
$ sudo cat /sys/firmware/fdt | dtc -I dtb -O dts 2>/dev/null
/dts-v1/;

/ {
	#size-cells = <0x02>;
	#address-cells = <0x02>;

	chosen {
		linux,uefi-secure-boot = <0x02>;
		kaslr-seed = <0x00 0x00>;
		linux,uefi-mmap-desc-ver = <0x01>;
		linux,uefi-mmap-desc-size = <0x30>;
		linux,uefi-mmap-size = <0x750>;
		linux,uefi-mmap-start = <0x03 0x6fbf7540>;
		linux,uefi-system-table = <0x03 0x6fbd0018>;
		bootargs = "BOOT_IMAGE=/vmlinuz-7.0.0-38-generic root=UUID=e8c86924-872a-4501-b66c-34b21fe8c63f ro console=tty1 console=ttyAMA0";
	};
};
$ sudo ls -l /sys/firmware/acpi/tables | grep -v "^d\|total"
-r-------- 1 root root 588 Oct 10 10:22 APIC
-r-------- 1 root root 974 Oct 10 10:22 DSDT
-r-------- 1 root root 276 Oct 10 10:22 FACP
-r-------- 1 root root 104 Oct 10 10:22 GTDT
-r-------- 1 root root  60 Oct 10 10:22 MCFG
$ for d in /sys/bus/acpi/devices/*; do printf "%-12s %s\n" ${d##*/} "$(cat $d/path)"; done
ACPI0007:00  \_PR_.CP00
ACPI0007:01  \_PR_.CP01
ACPI0007:02  \_PR_.CP02
ACPI0007:03  \_PR_.CP03
ACPI0007:04  \_PR_.CP04
ACPI0007:05  \_PR_.CP05
ARMH0061:00  \_PR_.GPIO
LNXSYBUS:00  \_SB_
LNXSYBUS:01  \_TZ_
LNXSYSTM:00  \
PNP0A08:00   \_SB_.PCI0
PNP0C02:00   \_SB_.PCI0.RES0
PNP0C0C:00   \_PR_.PWRB
$ ls /sys/bus/amba/devices/
ARMH0061:00
$ readlink /sys/bus/amba/devices/ARMH0061:00/firmware_node
../LNXSYSTM:00/ARMH0061:00
$ readlink /sys/bus/amba/devices/ARMH0061:00/driver
../../bus/amba/drivers/pl061_gpio
$ readlink /sys/bus/amba/devices/ARMH0061:00/of_node
```

출력에서 볼 것 — 이 출력에는 놀라운 점이 세 개 있다.

- **ACPI 머신에도 FDT가 있다.** `/sys/firmware/fdt`가 560바이트다. arm64의 UEFI 부팅에서 커널의 EFI stub이 UEFI 메모리 맵 위치·명령행을 넘기려고 `/chosen`만 있는 최소 DT를 만든다. 하드웨어 설명은 하나도 없다.
- **그런데 `/proc/device-tree`가 없다.** `arch/arm64/kernel/setup.c`의 `setup_arch()`는 `if (acpi_disabled) unflatten_device_tree();` — ACPI가 켜져 있으면 FDT를 펼치지 않는다. 그래서 `of_root`가 없고, `drivers/of/base.c`의 `of_core_init()`은 `/sys/firmware/devicetree` 디렉터리만 만들고(그래서 비어 있다) `if (of_root)` 조건 때문에 `/proc/device-tree` 링크를 만들지 않는다.
- **같은 PL061 GPIO, 같은 드라이버, 다른 설명서.** QEMU 랩에서는 DT 노드 `/pl061@9030000`이 `9030000.pl061`을 만들었고, 여기서는 ACPI 장치 `\_PR_.GPIO`(`_HID` = `ARMH0061`)가 `ARMH0061:00`을 만들었다. 둘 다 `/sys/bus/amba`에 있고 둘 다 `pl061_gpio` 드라이버가 잡았다. 링크만 `of_node` 대신 `firmware_node`다(마지막 `readlink`는 링크가 없어서 아무것도 출력하지 않았다). PCIe host bridge도 DT의 `pcie@...` 노드 대신 `PNP0A08`(+ ECAM은 `MCFG` 테이블)로 설명된다.

DSDT 전체가 974바이트, `virt.dtb`는 7.5KB다. 숫자만 보면 ACPI가 작지만 ACPI는 **장치 대부분을 PCI로 꽂는**(lspci로 보면 virtio-net·blk·console·rng 모두 PCI다) 가상 머신이라 설명할 non-discoverable 장치가 거의 없을 뿐이다. 임베디드 SoC는 반대로 non-discoverable 장치 수백 개를 설명해야 하고, ACPI로 그걸 하려면 펌웨어 벤더가 AML까지 맞춰 줘야 한다. 이것이 임베디드 SoC가 DT를 쓰는 현실적 이유다. 서버급 arm64(Ampere, Graviton 등)는 OS 하나로 여러 벤더 하드웨어를 지원해야 해서 ACPI를 택했다 — 자세한 비교는 C8.

---

## 10. 실험: UART 노드를 `status = "disabled"`로 — 무엇이 깨지나

### 10.1 예제 17 — 노드 하나 끄기: `.dts` 수정과 `dtc`

무엇을 확인하나: 랩의 DT를 복사해 UART 노드 하나에 `status = "disabled";`를 넣고 다시 컴파일한다.

```sh
cd /var/tmp/D1
cp /var/lab/out/virt.dts d1_virt.dts
sed "s/^\tpl011@9000000 {/&\n\t\tstatus = \"disabled\";/" d1_virt.dts > d1_nouart.dts
diff d1_virt.dts d1_nouart.dts
dtc -I dts -O dtb -o d1_virt.dtb d1_virt.dts;     echo rc=$?    # 대조군
dtc -I dts -O dtb -o d1_nouart.dtb d1_nouart.dts; echo rc=$?
```

```text
308a309
> 		status = "disabled";
d1_virt.dts:276.3-21: Warning (clocks_property): /pl061@9030000:clocks: cell 0 is not a phandle reference
d1_virt.dts:302.3-21: Warning (clocks_property): /pl031@9010000:clocks: cell 0 is not a phandle reference
d1_virt.dts:310.3-28: Warning (clocks_property): /pl011@9000000:clocks: cell 0 is not a phandle reference
d1_virt.dts:310.3-28: Warning (clocks_property): /pl011@9000000:clocks: cell 1 is not a phandle reference
d1_virt.dts:267.4-31: Warning (gpios_property): /gpio-keys/poweroff:gpios: cell 0 is not a phandle reference
rc=0
d1_nouart.dts:276.3-21: Warning (clocks_property): /pl061@9030000:clocks: cell 0 is not a phandle reference
d1_nouart.dts:302.3-21: Warning (clocks_property): /pl031@9010000:clocks: cell 0 is not a phandle reference
d1_nouart.dts:311.3-28: Warning (clocks_property): /pl011@9000000:clocks: cell 0 is not a phandle reference
d1_nouart.dts:311.3-28: Warning (clocks_property): /pl011@9000000:clocks: cell 1 is not a phandle reference
d1_nouart.dts:267.4-31: Warning (gpios_property): /gpio-keys/poweroff:gpios: cell 0 is not a phandle reference
rc=0
```

출력에서 볼 것: 컴파일은 성공(rc=0)했지만 경고가 5개씩 나온다. 원인은 우리 수정이 아니라 **역변환된 `.dts`의 한계**다. 원래 사람이 `clocks = <&apb_pclk>`처럼 label로 썼을 자리가 `<0x8000>`이라는 숫자로 굳어 있어서, `dtc`가 "이 셀이 phandle이어야 하는데 참조(`&label`)가 아니라 숫자다"라고 경고한다. 값 자체는 대상 노드의 `phandle = <0x8000>`과 맞으므로 동작에는 문제가 없다. 실무에서는 역변환한 `.dts`를 고쳐 쓰지 말고 **원본 `.dts`/`.dtsi`를 고치는** 것이 원칙이다 (D2, D5). 한 줄 수정만 필요하면 컴파일 없이 `fdtput -t s d1_virt_small.dtb /pl011@9000000 status disabled`로 blob을 직접 고쳐도 된다.

### 10.2 예제 18 — 같은 커널, 두 DTB로 부팅

무엇을 확인하나: 대조군 DTB로는 평소처럼 콘솔이 나오고, UART를 끈 DTB로는 무슨 일이 일어나는지.

```sh
echo "### A control"
qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel /var/lab/out/Image \
  -initrd d1_initrd.gz -dtb d1_virt.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=ls~/sys/bus/amba/devices"; echo "[qemu exit=$?]"
echo "### B nouart, serial only"     # 명령은 같고 -dtb만 다르다
time qemu-system-aarch64 ... -dtb d1_nouart.dtb -append "console=ttyAMA0 quiet LAB_CMD=ls~/sys/bus/amba/devices"; echo "[qemu exit=$?]"
```

```text
### A control
lab initramfs: 6.18.55 up 4.72s
9000000.pl011  9010000.pl031  9030000.pl061
[    5.715126] reboot: Power down
[qemu exit=0]
### B nouart, serial only
[qemu exit=0]

real	0m10.842s
user	0m3.191s
sys	0m0.207s
```

출력에서 볼 것: B는 **완전한 침묵**이다. 커널 로그 한 줄, 셸 출력 한 줄 없다. 그런데 QEMU는 정상 종료(exit 0)했고 10초가 걸렸다 — 커널은 부팅해서 `LAB_CMD`를 실행하고 `poweroff`까지 했다는 뜻이다. **시스템은 멀쩡히 돌았는데 우리가 볼 창이 없어진 것.** 실제 보드에서 이 증상은 "보드가 죽었다"로 오진되기 딱 좋다. Don이 bring-up에서 "UART 로그가 안 나오면 JTAG부터 붙인다"던 그 상황이다.

### 10.3 예제 19 — 다른 창(virtio console)으로 들어가 확인하기

무엇을 확인하나: 콘솔을 virtio console(`hvc0`)로 돌려서, UART가 꺼진 시스템 안을 들여다본다. 시리얼 출력은 파일로 받아 정말 0바이트인지 확인한다.

```sh
# 게스트 스크립트 g_nouart.sh
tr -d '\0' < /proc/device-tree/pl011@9000000/status; echo
ls /sys/bus/amba/devices | xargs
ls /dev/ttyAMA* /dev/hvc0
cat /proc/consoles
dmesg | grep -iE "pl011|ttyAMA|console|serial"
/d1_devmap amba
```

```sh
qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -display none -monitor none \
  -serial file:d1_serial.log \
  -chardev stdio,id=d1c -device virtio-serial-pci -device virtconsole,chardev=d1c \
  -kernel /var/lab/out/Image -initrd d1_initrd.gz -dtb d1_nouart.dtb \
  -append "console=hvc0 quiet LAB_CMD=sh~/g_nouart.sh"; echo "[qemu exit=$?]"
ls -l d1_serial.log
```

```text
lab initramfs: 6.18.55 up 2.40s
$ cat /proc/device-tree/pl011@9000000/status
disabled
$ ls /sys/bus/amba/devices
9010000.pl031 9030000.pl061
$ ls /dev/ttyAMA* /dev/hvc0
ls: /dev/ttyAMA*: No such file or directory
/dev/hvc0
$ cat /proc/consoles
hvc0                 -W- (EC  p  )  229:0
$ dmesg | grep -iE "pl011|ttyAMA|console|serial"
[    0.000000] Kernel command line: console=hvc0 quiet LAB_CMD=sh~/g_nouart.sh
[    0.016207] Console: colour dummy device 80x25
[    0.418221] Serial: AMBA PL011 UART driver
[    1.453888] Serial: 8250/16550 driver, 4 ports, IRQ sharing enabled
[    1.484618] msm_serial: driver initialized
[    1.521932] printk: legacy console [hvc0] enabled
$ /d1_devmap amba
DEVICE                       of_node -> DT path         driver
9030000.pl061                /pl061@9030000             pl061_gpio
9010000.pl031                /pl031@9010000             rtc-pl031
[qemu exit=0]
-rw-rw-r-- 1 donh donh 0 Oct 10 10:28 d1_serial.log
```

출력에서 볼 것 — 고리의 어느 부분이 끊겼는지 정확히 보인다:

- **DT 노드는 살아 있다**: `/proc/device-tree/pl011@9000000/status`가 `disabled`로 읽힌다. unflatten은 disabled 노드도 트리에 넣는다.
- **디바이스가 안 만들어졌다**: `/sys/bus/amba/devices`에 `9000000.pl011`이 없다. populate 단계가 `of_device_is_available()` 검사에서 건너뛰었다.
- **드라이버는 멀쩡하다**: `Serial: AMBA PL011 UART driver`가 등록됐다. 짝지을 디바이스가 없을 뿐이다.
- 그래서 `/dev/ttyAMA0`이 없고, `d1_serial.log`는 **0바이트**다. 같은 원리로 센서 키트에서 GNSS UART 노드를 `disabled`로 두면 `/dev/ttymxc2` 같은 장치 파일이 안 생기고, GNSS 데몬은 "No such file or directory"로 죽는다.

### 10.4 예제 20 — earlycon은 DT의 `status`를 무시한다

무엇을 확인하나: `earlycon`은 디바이스 모델을 거치지 않고 **주소에 직접 쓰는** 부팅 초기용 콘솔이다. UART를 꺼도 earlycon은 나올까?

```sh
# 같은 d1_nouart.dtb, 콘솔은 다시 시리얼
... -dtb d1_nouart.dtb -append "console=ttyAMA0 earlycon LAB_CMD=true" > d1_plain.log
grep -nE "bootconsole|earlycon|Warning|Run /init|Power" d1_plain.log; wc -l d1_plain.log
... -dtb d1_nouart.dtb -append "console=ttyAMA0 earlycon=pl011,0x9000000 LAB_CMD=true" > d1_early.log
sed -n "5,8p;244,252p" d1_early.log
# (부팅 래퍼 스크립트가 로그 마지막 줄에 "[qemu exit=$?]"를 남긴다)
```

```text
7:[    0.000000] earlycon: pl11 at MMIO 0x0000000009000000 (options '')
8:[    0.000000] printk: legacy bootconsole [pl11] enabled
38:[    0.000000] Kernel command line: console=ttyAMA0 earlycon LAB_CMD=true
246:[    2.639756] Warning: unable to open an initial console.
248:[    2.732033] Run /init as init process
251:[    3.086765] reboot: Power down
252 d1_plain.log
[    0.000000] Machine model: linux,dummy-virt
[    0.000000] earlycon: pl11 at MMIO 0x0000000009000000 (options '')
[    0.000000] printk: legacy bootconsole [pl11] enabled
[    0.000000] efi: UEFI not found.
[    5.598693] clk: Disabling unused clocks
[    5.599102] PM: genpd: Disabling unused power domains
[    5.601329] Warning: unable to open an initial console.
[    5.765451] Freeing unused kernel memory: 3264K
[    5.768855] Run /init as init process
[    6.656313] Flash device refused suspend due to active operation (state 20)
[    6.656795] Flash device refused suspend due to active operation (state 20)
[    6.668905] reboot: Power down
[qemu exit=0]
```

출력에서 볼 것:

- 인자 없는 `earlycon`도, 주소를 직접 준 `earlycon=pl011,0x9000000`도 **커널 로그 250줄을 끝까지** 찍었다. 인자 없는 `earlycon`은 DT의 `/chosen/stdout-path`를 따라가 PL011을 찾는데, `drivers/of/fdt.c`의 `early_init_dt_scan_chosen_stdout()`은 `compatible`만 보고 `status`를 검사하지 않는다(6.18.55 소스 확인). 하드웨어는 살아 있으니 MMIO에 쓰면 글자가 나간다.
- 반면 `lab initramfs: ...` 줄(유저 공간 `/init`의 `echo`)은 **안 나왔다**. `Warning: unable to open an initial console.` — `/dev/console` 뒤에 실제 tty 드라이버(`ttyAMA0`)가 없으니 유저 공간의 stdout은 갈 곳이 없다. earlycon(bootconsole)은 커널 `printk`만 내보낸다.
- 교훈: **"커널 로그는 나오는데 셸 프롬프트가 안 나온다"**는 증상은 earlycon만 살아 있고 진짜 UART 디바이스가 안 만들어진 경우일 수 있다. DT의 `status`, `compatible`, `/aliases`, `stdout-path`를 확인한다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">pl011 status = "disabled" 일 때 각 고리의 상태</text>
  <rect x="10" y="36" width="120" height="64" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="70" y="60" text-anchor="middle" font-size="12">DT 노드</text>
  <text x="70" y="84" text-anchor="middle" font-size="12">있음 (disabled)</text>
  <rect x="145" y="36" width="120" height="64" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="205" y="60" text-anchor="middle" font-size="12">amba_device</text>
  <text x="205" y="84" text-anchor="middle" font-size="12">안 만들어짐</text>
  <rect x="280" y="36" width="120" height="64" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="60" text-anchor="middle" font-size="12">uart-pl011</text>
  <text x="340" y="84" text-anchor="middle" font-size="12">등록됨 (짝 없음)</text>
  <rect x="415" y="36" width="120" height="64" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="475" y="60" text-anchor="middle" font-size="12">/dev/ttyAMA0</text>
  <text x="475" y="84" text-anchor="middle" font-size="12">없음</text>
  <rect x="550" y="36" width="120" height="64" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="610" y="60" text-anchor="middle" font-size="12">유저 공간 출력</text>
  <text x="610" y="84" text-anchor="middle" font-size="12">갈 곳 없음</text>
  <rect x="10" y="130" width="390" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="152" font-size="12">earlycon: 주소 0x9000000에 직접 쓴다</text>
  <text x="20" y="171" font-size="12">→ 디바이스 모델 우회, 커널 printk는 계속 보인다</text>
  <rect x="415" y="130" width="255" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="425" y="152" font-size="12">virtio console (hvc0)</text>
  <text x="425" y="171" font-size="12">→ 다른 창으로 들어가 진단</text>
  <text x="10" y="215" font-size="12">하드웨어(PL011 블록)는 처음부터 끝까지 살아 있다 — 커널이 "없는 것으로" 취급했을 뿐</text>
  <text x="10" y="237" font-size="12">시리얼만 보면: 로그 0바이트 = "보드가 죽었다"로 오진하기 쉽다</text>
</svg>
```
그림 6 — `status = "disabled"`는 "디바이스를 만들지 마라"이지 "하드웨어를 꺼라"가 아니다. 초록은 살아 있는 고리, 빨강은 끊긴 고리다.

---

## 11. 커널 트리 통계 — DT는 얼마나 많은 보드를 설명하나

### 11.1 예제 21 — 벤더별 `.dts` 수 세기

무엇을 확인하나: "커널 하나, 보드 여러 개"의 규모. arm64 커널 하나가 설명 파일만 바꿔서 지원하는 보드 수를 벤더별로 센다.

```sh
cd /var/lab/src/linux-6.18.55
find arch/arm64/boot/dts -name "*.dts" | cut -d/ -f5 | sort | uniq -c | sort -rn | head -15
find arch/arm64/boot/dts -name "*.dts" | wc -l                       # 보드 .dts 총수
find arch/arm64/boot/dts -name "*.dts" | cut -d/ -f5 | sort -u | wc -l   # 벤더 디렉터리 수
find arch/arm64/boot/dts -name "*.dtsi" | wc -l                      # SoC/공통 include
find arch/arm64/boot/dts -name "*.dtso" | wc -l                      # overlay 소스 (D6)
find arch/arm/boot/dts -name "*.dts" | wc -l                         # 32비트 ARM
find arch/riscv/boot/dts -name "*.dts" | wc -l                       # RISC-V
grep -rhoE "\.compatible *= *\"[^\"]+\"" drivers sound | wc -l        # 드라이버 쪽 compatible 항목
grep -rhoE "\.compatible *= *\"[^\"]+\"" drivers sound | sort -u | wc -l
find Documentation/devicetree/bindings -name "*.yaml" | wc -l        # binding 스키마 (D7)
```

```text
    298 qcom
    210 freescale
    193 rockchip
     98 mediatek
     95 amlogic
     92 apple
     77 renesas
     61 allwinner
     57 ti
     34 marvell
     30 broadcom
     23 xilinx
     20 exynos
     19 nvidia
     17 arm
1399
39
1185
169
1680
52
15076
14233
5182
```

```svg
<svg viewBox="0 0 680 452" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">Linux 6.18.55 arch/arm64/boot/dts — 벤더별 보드 .dts 수 (총 1399개, 39곳)</text>
  <text x="102" y="62" text-anchor="end" font-size="12">qcom</text>
  <rect x="110" y="50" width="470.0" height="15" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="586.0" y="62" font-size="12">298</text>
  <text x="102" y="84" text-anchor="end" font-size="12">freescale</text>
  <rect x="110" y="72" width="331.2" height="15" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="447.2" y="84" font-size="12">210</text>
  <text x="102" y="106" text-anchor="end" font-size="12">rockchip</text>
  <rect x="110" y="94" width="304.4" height="15" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="420.4" y="106" font-size="12">193</text>
  <text x="102" y="128" text-anchor="end" font-size="12">mediatek</text>
  <rect x="110" y="116" width="154.6" height="15" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="270.6" y="128" font-size="12">98</text>
  <text x="102" y="150" text-anchor="end" font-size="12">amlogic</text>
  <rect x="110" y="138" width="149.8" height="15" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="265.8" y="150" font-size="12">95</text>
  <text x="102" y="172" text-anchor="end" font-size="12">apple</text>
  <rect x="110" y="160" width="145.1" height="15" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="261.1" y="172" font-size="12">92</text>
  <text x="102" y="194" text-anchor="end" font-size="12">renesas</text>
  <rect x="110" y="182" width="121.4" height="15" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="237.4" y="194" font-size="12">77</text>
  <text x="102" y="216" text-anchor="end" font-size="12">allwinner</text>
  <rect x="110" y="204" width="96.2" height="15" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="212.2" y="216" font-size="12">61</text>
  <text x="102" y="238" text-anchor="end" font-size="12">ti</text>
  <rect x="110" y="226" width="89.9" height="15" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="205.9" y="238" font-size="12">57</text>
  <text x="102" y="260" text-anchor="end" font-size="12">marvell</text>
  <rect x="110" y="248" width="53.6" height="15" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="169.6" y="260" font-size="12">34</text>
  <text x="102" y="282" text-anchor="end" font-size="12">broadcom</text>
  <rect x="110" y="270" width="47.3" height="15" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="163.3" y="282" font-size="12">30</text>
  <text x="102" y="304" text-anchor="end" font-size="12">xilinx</text>
  <rect x="110" y="292" width="36.3" height="15" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="152.3" y="304" font-size="12">23</text>
  <text x="102" y="326" text-anchor="end" font-size="12">exynos</text>
  <rect x="110" y="314" width="31.5" height="15" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="147.5" y="326" font-size="12">20</text>
  <text x="102" y="348" text-anchor="end" font-size="12">nvidia</text>
  <rect x="110" y="336" width="30.0" height="15" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="146.0" y="348" font-size="12">19</text>
  <text x="102" y="370" text-anchor="end" font-size="12">arm</text>
  <rect x="110" y="358" width="26.8" height="15" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="142.8" y="370" font-size="12">17</text>
  <text x="102" y="392" text-anchor="end" font-size="12">기타 24곳</text>
  <rect x="110" y="380" width="118.3" height="15" fill="#888" fill-opacity="0.7" stroke="#888"/>
  <text x="234.3" y="392" font-size="12">75</text>
  <line x1="110" y1="44" x2="110" y2="402" stroke="currentColor"/>
  <rect x="110" y="416" width="12" height="12" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="128" y="426" font-size="12">센서 키트 후보 SoC 벤더 (i.MX = freescale, AM62 = ti, Jetson = nvidia)</text>
</svg>
```
그림 7 — 커널 6.18.55의 arm64 보드 `.dts` 1399개를 벤더 디렉터리별로 센 막대그래프 (상위 15곳 + 나머지 24곳 합 75). 막대 길이는 실제 개수에 비례한다.

출력에서 볼 것:

- arm64에만 보드 `.dts`가 **1399개**, 32비트 ARM에 1680개, RISC-V에 52개다. 이 보드들이 모두 같은 `arch/arm64` 커널 코드를 공유한다. board file 시대였다면 C 파일 1399개였을 것이다.
- `.dtsi`(1185개)가 `.dts`와 비슷하게 많다. SoC 공통 부분을 `.dtsi`로 빼고 보드 `.dts`가 include해서 덮어쓰는 계층 구조 때문이다 (D5). `.dtso`(169개)는 overlay 소스다 (D6).
- 드라이버 쪽에는 `.compatible = "..."` 항목이 15076개(중복 제거 14233개) 있다. DT 쪽 `compatible`과 이 문자열이 만나는 지점이 D3의 주제다. binding 스키마(YAML)는 5182개다 (D7).
- 센서 키트 후보로 보면 freescale(NXP i.MX) 210개 중 i.MX8MP 보드만 56개, TI AM62 계열 31개, NVIDIA Tegra234(Jetson Orin) 6개다:

```sh
cd arch/arm64/boot/dts
ls freescale/imx8mp*.dts | wc -l
ls ti/k3-am62*.dts | wc -l
ls nvidia/tegra234*.dts | wc -l
```

```text
56
31
6
```

---

## 12. 커널 안에서는 무슨 일이 일어나나

### 12.1 부팅부터 `probe()`까지 — 함수 경로

이 노트의 실험들이 커널 소스의 어디에 대응하는지 6.18.55 기준으로 정리한다. 함수 이름은 모두 랩의 소스 트리에서 확인했다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" font-size="13" font-weight="bold">시점</text>
  <text x="150" y="20" font-size="13" font-weight="bold">함수 (6.18.55)</text>
  <text x="470" y="20" font-size="13" font-weight="bold">이 노트에서 본 흔적</text>
  <rect x="140" y="32" width="320" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="150" y="50" font-size="12">head.S: x0 → __fdt_pointer</text>
  <text x="150" y="68" font-size="12">setup_machine_fdt() → early_init_dt_scan()</text>
  <text x="10" y="58" font-size="12">커널 진입</text>
  <text x="470" y="58" font-size="12">Machine model: 출력</text>
  <rect x="140" y="88" width="320" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="150" y="106" font-size="12">early_init_dt_scan_chosen(): bootargs,</text>
  <text x="150" y="124" font-size="12">rng-seed(지움) / ..._scan_memory()</text>
  <text x="10" y="114" font-size="12">setup_arch</text>
  <text x="470" y="106" font-size="12">§5.2 rng-seed 사라짐</text>
  <text x="470" y="124" font-size="12">§3.6 memory reg</text>
  <rect x="140" y="144" width="320" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="150" y="162" font-size="12">early_init_dt_scan_chosen_stdout()</text>
  <text x="150" y="180" font-size="12">earlycon (status 검사 없음)</text>
  <text x="10" y="170" font-size="12">earlycon</text>
  <text x="470" y="170" font-size="12">§10.4</text>
  <rect x="140" y="200" width="320" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="218" font-size="12">if (acpi_disabled) unflatten_device_tree()</text>
  <text x="150" y="236" font-size="12">→ device_node 트리, of_root</text>
  <text x="10" y="226" font-size="12">setup_arch</text>
  <text x="470" y="218" font-size="12">§9.2 ACPI면 안 함</text>
  <text x="470" y="236" font-size="12">§5.3 노드 57개</text>
  <rect x="140" y="256" width="320" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="274" font-size="12">irqchip_init() → of_irq_init()</text>
  <text x="150" y="292" font-size="12">of_clk_init(), timer_probe()</text>
  <text x="10" y="282" font-size="12">init_IRQ 등</text>
  <text x="470" y="274" font-size="12">GIC·clock·timer는</text>
  <text x="470" y="292" font-size="12">platform device 아님</text>
  <rect x="140" y="312" width="320" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="330" font-size="12">driver_init() → of_core_init()</text>
  <text x="150" y="348" font-size="12">/sys/firmware/devicetree, /proc 링크</text>
  <text x="10" y="338" font-size="12">do_basic_setup</text>
  <text x="470" y="338" font-size="12">§5.2 /proc/device-tree</text>
  <rect x="140" y="368" width="320" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="386" font-size="12">of_platform_default_populate_init()</text>
  <text x="150" y="404" font-size="12">→ of_platform_bus_create() 노드마다</text>
  <text x="10" y="394" font-size="12">arch_initcall_sync</text>
  <text x="470" y="394" font-size="12">§6.3, §10.3</text>
  <rect x="140" y="424" width="320" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="150" y="448" font-size="12">bus match → really_probe() → probe()</text>
  <text x="10" y="448" font-size="12">드라이버 등록 시</text>
  <text x="470" y="448" font-size="12">§6.2 driver 링크</text>
  <line x1="300" y1="76" x2="300" y2="86" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="300" y1="132" x2="300" y2="142" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="300" y1="188" x2="300" y2="198" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="300" y1="244" x2="300" y2="254" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="300" y1="300" x2="300" y2="310" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="300" y1="356" x2="300" y2="366" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="300" y1="412" x2="300" y2="422" stroke="currentColor" marker-end="url(#ah6)"/>
</svg>
```
그림 8 — 커널 진입부터 드라이버 `probe()`까지 DT가 거치는 함수. 주황은 평평한 blob을 직접 읽는 단계, 초록은 트리를 만들고 early 장치를 초기화하는 단계, 파랑·빨강은 디바이스 모델 단계다.

단계별로 보면:

1. **진입**: arm64 부팅 규약(`Documentation/arch/arm64/booting.rst`)은 "`x0` = 시스템 RAM에 있는 DTB의 물리 주소"를 요구한다. `head.S`가 이 값을 `__fdt_pointer`에 저장하고, `setup_arch()`가 `setup_machine_fdt(__fdt_pointer)` → `early_init_dt_scan()`을 부른다. 여기서 `early_init_dt_verify()`가 헤더(magic `d00dfeed`, 버전)를 검사하고 blob의 CRC를 계산해 둔다.
2. **early scan** (`drivers/of/fdt.c`): 아직 메모리 할당자도 없는 시점이라 평평한 blob을 직접 훑는다. `early_init_dt_scan_root()`(루트의 cells), `early_init_dt_scan_chosen()`(`bootargs` → `boot_command_line`, initrd 위치, `rng-seed` 사용 후 NOP 처리), `early_init_dt_scan_memory()`(`device_type = "memory"` 노드 → memblock). usage-model의 "runtime configuration"이 여기다.
3. **unflatten**: `if (acpi_disabled) unflatten_device_tree();` — `__unflatten_device_tree()`가 blob을 두 번 훑어(크기 계산, 실제 생성) `struct device_node`/`struct property` 트리를 만들고 전역 `of_root`에 매단다. `/aliases`도 이때 스캔된다(`of_alias_scan()`).
4. **early 장치**: 인터럽트 컨트롤러·클록·타이머는 드라이버 모델이 준비되기 전에 필요하다. 그래서 `IRQCHIP_DECLARE`, `CLK_OF_DECLARE`, `TIMER_OF_DECLARE` 매크로로 링커 섹션에 (compatible, init 함수) 표를 만들어 두고, `irqchip_init()` → `of_irq_init()`, `of_clk_init()`, `timer_probe()`가 DT를 뒤져 직접 초기화한다. 초기화된 노드에는 `OF_POPULATED` 표시가 붙어 나중에 platform device가 중복 생성되지 않는다(§6.3의 넷째 부류).
5. **sysfs**: `driver_init()` → `of_core_init()`이 노드마다 kobject를 만들어 `/sys/firmware/devicetree/base`를 채우고, `of_root`가 있으면 `/proc/device-tree` 링크를 만든다.
6. **populate**: `arch_initcall_sync(of_platform_default_populate_init)` → `of_platform_populate()` → 노드마다 `of_platform_bus_create()`. 여기서 걸러진다: `compatible` 없음 → 건너뜀, `of_device_is_available()` 거짓(`status = "disabled"`) → 건너뜀, `OF_POPULATED` → 건너뜀, `"arm,primecell"` → `of_amba_device_create()`, 그 밖 → `of_platform_device_create_pdata()` → `of_device_alloc()`(노드의 `reg`·`interrupts`를 `struct resource`로 변환 — board file이 손으로 쓰던 그 배열) → `of_device_add()` → `device_add()`. 노드가 `simple-bus` 같은 버스면 자식으로 재귀한다.
7. **매칭**: `device_add()`든 드라이버 등록(`platform_driver_register()`)이든, 나중에 온 쪽이 버스의 `match`를 부른다. `platform_match()`의 순서는 `driver_override` → **OF(`of_driver_match_device`)** → ACPI → `id_table` → 이름 비교 — §3.5의 smc91x가 세 테이블을 다 가진 이유다. AMBA 버스의 `amba_match()`는 이때 처음으로 하드웨어의 Peripheral ID 레지스터를 읽는다(`amba_read_periphid()`). 맞으면 `really_probe()` → 드라이버의 `probe()`. 자세한 우선순위와 `-EPROBE_DEFER`는 D3, G2.

### 12.2 드라이버를 쓸 때 다시 만나는 지점

| 이 노트에서 본 것 | 드라이버 코드에서 다시 만나는 모습 | 자세히 |
|---|---|---|
| `compatible` 문자열 | `static const struct of_device_id foo_of_match[] = { { .compatible = "vendor,foo" }, {} };` + `MODULE_DEVICE_TABLE(of, ...)` | D3, E3 |
| `reg` → `struct resource` | `devm_platform_ioremap_resource(pdev, 0)` | G2, F 모듈 |
| `interrupts` → virq | `platform_get_irq(pdev, 0)` → `devm_request_irq()` | H 모듈 |
| 노드의 다른 속성 | `of_property_read_u32(np, "clock-frequency", &v)` 또는 fwnode 공통 `device_property_read_u32()` | D8 |
| `status = "disabled"` | 디바이스가 없으니 `probe()`가 안 불린다 — 드라이버 디버깅 전에 DT부터 | D5, M1 |
| uevent `MODALIAS` | 모듈 자동 로드, `modinfo -F alias` | E 모듈 |
| `of_node` 링크 | 드라이버 안의 `dev->of_node`, `dev_fwnode(dev)` | D8 |

---

## 13. Embedded 실무에서 다시 보기

### 13.1 장면 1 — 센서 키트 bring-up: "IMU가 `/sys/bus/iio`에 안 보인다"

i.MX8MP 기반 센서 키트에 ST IMU를 I2C3에 붙였다. 커널은 뜨는데 `/sys/bus/iio/devices`가 비어 있다. 이 노트의 사슬을 그대로 따라간다.

```sh
# 1) DT에 노드가 있나? 켜져 있나?  (경로는 보드마다 다르다 — find로 찾는다)
find /proc/device-tree -name compatible | xargs grep -l "lsm6" 2>/dev/null
cat /proc/device-tree/soc@0/bus@30800000/i2c@30a40000/status    # 부모 I2C 버스도 okay여야 한다
# 2) 디바이스가 만들어졌나? I2C 디바이스 이름은 "<버스번호>-<주소4자리>"
ls /sys/bus/i2c/devices/
readlink /sys/bus/i2c/devices/2-006a/of_node
# 3) 드라이버가 붙었나?
readlink /sys/bus/i2c/devices/2-006a/driver
cat /sys/bus/i2c/devices/2-006a/modalias
# 4) 커널이 뭐라고 했나?
dmesg | grep -iE "lsm6|i2c"
```

(이 블록은 실제 보드용 점검 순서다. 랩에는 I2C 장치가 없어 출력은 싣지 않는다.) 판정표: 1)에서 노드가 없으면 `.dts` 문제, 노드는 있는데 `status`가 `disabled`면 보드 `.dts`에서 `okay`로 켜야 한다(부모 버스 노드도!). 2)에서 디바이스가 없으면 부모 I2C 컨트롤러 드라이버가 probe 안 됐을 가능성. 3)에서 `driver` 링크가 없으면 드라이버 미빌드(`CONFIG_IIO_ST_LSM6DSX`), 모듈 미로드, 또는 compatible 오타. 4)에서 `probe` 실패 메시지(예: WHO_AM_I 불일치)가 보이면 이제야 하드웨어·배선 문제다 — Don의 로직 분석기가 나설 차례.

### 13.2 장면 2 — 같은 제품, 보드 리비전 두 개

센서 키트 Rev A는 IMU 인터럽트가 GPIO1_IO12, Rev B는 GPIO4_IO03으로 바뀌었다. board file 시대라면 커널을 두 벌 빌드했겠지만, 지금은 `sensorkit-reva.dts`와 `sensorkit-revb.dts`(공통 부분은 `sensorkit.dtsi`)를 만들고 **커널 이미지는 하나**로 간다. 부트로더가 보드의 EEPROM이나 strap 핀으로 리비전을 읽어 맞는 `.dtb`를 고르거나(U-Boot의 FIT 이미지 configuration), 공통 `.dtb`에 리비전별 overlay를 얹는다 (D6). 펌웨어로 치면 "보드 ID GPIO를 읽어서 핀맵 테이블을 고르는 것"을 부트로더 + 데이터로 하는 셈이다.

### 13.3 장면 3 — "부팅 로그가 아예 안 나온다"

§10의 실험이 그대로 현장 시나리오다. 새 보드에서 시리얼이 조용하면:

- 먼저 `earlycon`(또는 `earlycon=<드라이버>,<주소>`)을 커널 명령행에 넣는다. 이걸로 로그가 나오면 **커널은 살아 있고** DT의 UART 노드(`status`, `compatible`, `/chosen/stdout-path`, `/aliases`의 `serial0`)나 pinctrl 쪽 문제다.
- `earlycon`으로도 안 나오면 더 앞 단계다 — 부트로더가 DTB 주소를 제대로 넘겼나(`x0`), DTB magic이 맞나, 커널 이미지 로드 주소가 맞나 (C1, C3).
- U-Boot 프롬프트에서 `fdt addr ${fdt_addr_r}; fdt print /serial@...`로 커널에 넘기기 직전의 DT를 확인할 수 있다 (C3).

### 13.4 장면 4 — PCIe 칩이 보드에 납땜돼 있을 때

Don이 잘 아는 영역과의 접점이다. Jetson Orin 캐리어 보드에 PCIe Wi-Fi/5G 모듈이나 NVMe를 붙이면 그 장치 자체는 DT가 필요 없다(§7). 하지만 **그 장치의 전원 레일·PERST# 리셋 GPIO·CLKREQ#·레퍼런스 클록**은 SoC 쪽 PCIe 컨트롤러 노드의 속성(`reset-gpios`, `vpcie3v3-supply` 같은 이름 — 컨트롤러 binding마다 다르다)으로 DT에 적어야 한다. "링크가 안 올라온다(LTSSM이 Detect에서 멈춘다)"는 증상의 상당수가 이 DT 속성 누락이다. enumeration은 discoverable이지만, enumeration이 **시작될 수 있는 조건**은 non-discoverable이다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 노드에 `status = "okay"` 안 넣음 (SoC `.dtsi`의 기본 `disabled` 상속) | 디바이스 디렉터리가 없음, `probe()` 안 불림, dmesg 조용함 | populate가 `of_device_is_available()`에서 건너뜀 | 보드 `.dts`에서 `&i2c3 { status = "okay"; };` — 부모 버스 노드도 확인 |
| `compatible` 오타 (`"st,lsm6dso "`, 벤더 접두어 틀림) | 디바이스는 있는데 `driver` 링크 없음 | 문자열 완전 일치 비교라 공백 하나도 불일치 | `cat .../uevent`의 `OF_COMPATIBLE_0`과 드라이버의 `of_device_id` 비교 |
| 드라이버를 Kconfig에서 안 켬 | 노드·디바이스 정상인데 영원히 미바인딩 (§6.3의 fw-cfg) | DT는 데이터일 뿐, 드라이버는 커널에 있어야 함 | `zcat /proc/config.gz` 확인 후 `CONFIG_...=y/m` |
| 역변환한 `.dts`를 고쳐서 제품에 사용 | phandle 경고, label 사라짐, 다음 BSP 업데이트 때 diff 지옥 | `dtc -I dtb -O dts`는 label·include·매크로를 잃는다 | 원본 `.dts`/`.dtsi`를 고치고 다시 빌드 (D5) |
| 드라이버 정책을 DT 속성으로 만듦 (`foo,rx-buffer-size`) | upstream 리뷰에서 거절, 보드마다 다른 값 난립 | DT는 하드웨어 설명이지 설정 파일이 아님 | sysfs·module parameter·유저 공간 설정으로 |
| PCI 장치를 DT에 노드로 적으려 함 | 아무 효과 없음 또는 중복 장치 | PCI는 enumeration이 찾는다 | 정말 필요한 비발견 정보(MAC, reset GPIO)만 `pcie` 노드 자식으로 |
| UART 노드를 끄고 콘솔이 안 나오자 "보드 사망" 판정 | 시리얼 0바이트, 실제로는 부팅 완료 (§10.2) | 디바이스가 없어 `/dev/console`에 tty가 없음 | `earlycon`, 다른 콘솔(hvc, 네트워크)로 진단 |
| `earlycon`이 나오니 UART 설정이 맞다고 판단 | 커널 로그는 나오는데 로그인 프롬프트 없음 | earlycon은 DT `status`·디바이스 모델을 우회 | `ls /dev/ttyXXX`, `/sys/bus/*/devices`에 UART 디바이스가 있는지 확인 |
| ACPI 머신에서 `/proc/device-tree`를 찾음 | "No such file or directory" | ACPI면 커널이 FDT를 unflatten하지 않음 (§9.2) | `/sys/firmware/acpi/tables`, `/sys/bus/acpi/devices`, `firmware_node` 링크 |
| DT 정수를 little-endian으로 해석 | `reg` 주소가 `0x00000900` 같은 엉뚱한 값 | FDT·`/proc/device-tree`는 big-endian | `ntohl()`/`be32_to_cpu()`, 커널에선 `of_property_read_u32()` |

---

## 15. 면접에서 이렇게 말한다

**Q.** What is a Device Tree and why does Linux use it?

**A.** 하드웨어 중 PCIe·USB처럼 스스로 정체를 밝히지 못하는 장치 — SoC 안의 MMIO 주변장치, I2C/SPI 센서, GPIO·인터럽트 배선 — 를 커널에 알려 주는 **데이터 구조**다. 예전 ARM은 이걸 보드마다 C 코드(board file)로 커널에 박아서 보드마다 커널을 따로 빌드했는데, DT는 그 설명을 커널 밖의 `.dtb`로 빼서 부트로더가 넘기게 했다. 그래서 커널 하나로 수많은 보드를 지원한다.

> A Device Tree is a data structure that describes the hardware the kernel cannot discover by itself — memory-mapped SoC peripherals, I2C and SPI devices, GPIO and interrupt wiring, clocks and regulators. Before DT, 32-bit ARM hard-coded this in per-board C files under `arch/arm/mach-*`, which meant lots of duplicated code and often a separate kernel per board. With DT the description lives outside the kernel: the .dts is compiled by dtc into a .dtb, the bootloader passes its address to the kernel — x0 on arm64 — and the kernel unflattens it and creates platform devices that drivers bind to via compatible strings. One kernel image, many boards.

**꼬리 질문**: "그럼 DT에는 뭘 적고 뭘 안 적나요?" → 하드웨어 사실만, 드라이버 정책은 안 적는다(§8).

**Q.** Why don't PCIe devices need Device Tree nodes?

**A.** PCIe는 discoverable이라서다. 커널이 config space를 읽으면 장치가 Vendor/Device ID, class, BAR 크기, MSI capability를 직접 답한다. 다만 host bridge 자체 — ECAM 주소, 주소 창, INTx 매핑, MSI 컨트롤러 — 는 물어볼 수 없어서 DT에는 `pcie` 노드 하나가 있다. QEMU 랩에서 e1000을 꽂아도 DT 노드 수는 그대로였고 PCI 장치들의 `of_node`는 없었다.

> PCIe is self-describing: once the kernel can reach config space it enumerates bus, device and function, reads vendor and device IDs, the class code, sizes the BARs and finds MSI capabilities, so drivers match on PCI IDs. What is not discoverable is the root complex itself — where ECAM lives, the memory and I/O windows, how legacy INTx maps to the GIC, which MSI controller to use — so the DT has one host-bridge node and nothing below it. The exception is soldered-down PCIe chips that need extra non-discoverable info like a MAC address or a reset GPIO; those can get child nodes.

**꼬리 질문**: "PCIe 링크가 안 올라올 때 DT에서 뭘 보나요?" → 컨트롤러 노드의 reset GPIO(PERST#), 전원 레일, 클록, `status`(§13.4).

**Q.** Walk me through what happens from the .dts file to a driver's probe() being called.

**A.** `.dts`/`.dtsi`를 `dtc`가 big-endian 바이너리 `.dtb`로 컴파일한다. 부트로더가 그걸 RAM에 올리고 `/chosen`에 bootargs·initrd를 써 넣은 뒤 주소를 `x0`로 넘긴다. 커널은 early scan으로 메모리와 명령행을 읽고, `unflatten_device_tree()`로 `device_node` 트리를 만들고, GIC·클록·타이머는 `*_OF_DECLARE`로 먼저 초기화한다. `arch_initcall_sync`에서 `of_platform_populate()`가 `compatible`이 있고 `status`가 okay인 노드마다 `platform_device`(PrimeCell이면 `amba_device`)를 만들고 `reg`·`interrupts`를 resource로 바꾼다. 버스의 match가 `compatible`과 드라이버의 `of_match_table`을 비교해 맞으면 `probe()`가 불린다.

> dtc compiles the source into a flattened blob. The bootloader loads it, patches /chosen with bootargs and the initrd location, and jumps to the kernel with the blob's physical address in x0. Early in setup_arch the kernel scans the flat blob for memory and the command line, then unflattens it into a tree of device_node structures. Interrupt controllers, clocks and timers are initialized early through IRQCHIP_DECLARE, CLK_OF_DECLARE and TIMER_OF_DECLARE. At arch_initcall_sync, of_platform_populate walks the tree and creates a platform device for every available node with a compatible string, converting reg and interrupts into resources. When the device and driver meet on the bus, platform_match tries the OF match first, and on success the driver's probe runs. In sysfs you can see the result as the of_node and driver symlinks.

**꼬리 질문**: "`status = "disabled"`면 어디서 걸러지나요?" → populate 단계의 `of_device_is_available()` (§10.3, §12.1).

**Q.** How would you debug a device that is in the Device Tree but whose driver never probes?

**A.** 사슬을 순서대로 끊어 본다. `/proc/device-tree`에 노드가 있고 `status`가 okay인지, `/sys/bus/<bus>/devices`에 디바이스가 만들어졌는지, `of_node` 링크가 맞는 노드를 가리키는지, `driver` 링크가 있는지, uevent의 `OF_COMPATIBLE`이 드라이버 테이블과 글자까지 같은지, 드라이버가 빌드·로드됐는지 본다. 그다음에야 dmesg의 probe 에러나 `-EPROBE_DEFER`(의존 자원 대기)를 본다.

> I walk the chain. First, is the node in /proc/device-tree and is its status okay — including its parent bus. Second, was a device created under /sys/bus/platform or i2c, and does its of_node link point to the right node. Third, does it have a driver link; if not, compare OF_COMPATIBLE in the uevent with the driver's of_device_id table, and check that the driver is actually built and loaded. Only then do I look at dmesg for probe errors or deferred probing, and at /sys/kernel/debug/devices_deferred. That order separates DT problems from driver problems from hardware problems quickly.

**Q.** What's the difference between Device Tree and ACPI?

**A.** 둘 다 non-discoverable 하드웨어를 OS에 알려 주는 방법이다. DT는 순수 데이터이고 보드 개발자가 쓰며 임베디드 ARM·RISC-V에서 쓴다. ACPI는 펌웨어가 제공하는 테이블에 AML이라는 실행 코드까지 들어 있어서 전원 관리 같은 절차를 펌웨어가 정의하고, x86과 arm64 서버에서 쓴다. 장치 식별도 `compatible` 대 `_HID`다. 드라이버는 `device_property_*` fwnode API로 쓰면 둘 다 지원할 수 있다.

> Both describe non-discoverable hardware to the OS. Device Tree is pure declarative data, usually written by the board developer and shipped with the kernel or BSP; it dominates embedded ARM and RISC-V. ACPI comes from the platform firmware and includes AML bytecode the OS executes, so firmware can own procedures like power-state transitions; it is the norm on x86 and on arm64 servers. Devices are identified by compatible strings in DT and by _HID in ACPI. A driver written against the fwnode device_property API can support both. On an ACPI-booted arm64 machine Linux does not even unflatten the tiny FDT from the EFI stub, so there is no /proc/device-tree.

**꼬리 질문**: "임베디드에서 ACPI를 안 쓰는 이유는?" → SoC마다 수백 개의 비발견 장치를 펌웨어 벤더가 AML까지 맞춰야 하고, 커널 트리와 함께 진화하는 DT가 개발·디버깅 흐름에 맞다(§9.2).

**Q.** What is the history behind Device Tree on ARM?

**A.** DT는 Open Firmware에서 왔고, 2005년 PowerPC가 FDT 형식을 만들어 펌웨어 없이도 쓰게 했다. ARM은 board file로 버티다가 SoC·보드가 폭발하면서 `arch/arm`이 감당이 안 됐고, 2011년 Linus가 ARM 플랫폼 코드의 중복과 churn을 공개적으로 강하게 비판한 것을 계기로 arm-soc 정리와 DT 전면 도입이 진행됐다. arm64는 처음부터 board file 없이 DT 또는 ACPI만 쓴다.

> DT comes from Open Firmware on SPARC and PowerPC. In 2005 PowerPC Linux introduced the flattened form so boards without Open Firmware could pass a blob. ARM meanwhile relied on board files, and by around 2011 the explosion of SoCs made arch/arm unmaintainable — Linus publicly complained about the churn and duplication, which pushed the community to consolidate through the arm-soc tree and require DT for new platforms. arm64 never had board files; it boots with DT or ACPI only.

**Q.** Can you change the hardware description without rebuilding the kernel?

**A.** 예. 그게 DT의 핵심 가치다. `.dts`만 고쳐 `dtc`로 `.dtb`를 다시 만들고 부트로더가 그걸 넘기면 된다. 한 줄이면 `fdtput`으로 blob을 직접 고칠 수도 있고, 확장 보드는 overlay로 얹는다. 랩에서 같은 `Image`로 UART 노드만 끈 DTB를 부팅해 차이를 확인했다.

> Yes, that is the whole point. You edit the .dts, recompile just the .dtb with dtc, and have the bootloader pass the new blob — the kernel image is unchanged. For a one-line tweak you can even patch the blob with fdtput, and for add-on boards you apply overlays. In my QEMU lab I booted the identical kernel with a DTB where only the UART node was disabled: the system booted fine but the UART device was never created, so there was no ttyAMA0 and no user-space console — while earlycon still printed kernel logs because it writes the registers directly.

---

## 16. 직접 해보기

1. **개념**: SoC 안의 SPI 컨트롤러, SPI 버스 위의 IMU, USB 웹캠, M.2 NVMe SSD, I2C EEPROM 각각에 대해 "DT 노드가 필요한가, 필요하면 무엇을 적어야 하나"를 표로 써라.
   정답/힌트: SPI 컨트롤러·IMU·EEPROM은 필요(주소·IRQ·CS 번호·버스 위치), 웹캠·NVMe는 장치 자체는 불필요하지만 USB 컨트롤러·PCIe 컨트롤러 노드(+ 전원·리셋)는 필요.

2. **랩**: `virt.dts`에서 `/pl031@9010000`(RTC)를 `disabled`로 바꾼 DTB로 부팅해 `ls /sys/bus/amba/devices`, `ls /dev/rtc*`, `dmesg | grep rtc`를 확인하고, 시스템 시각이 어떻게 되는지(`date`) 관찰하라.
   정답/힌트: 랩에서 확인한 결과 `9010000.pl031`이 사라지고 `/dev/rtc0`이 없으며 `date`가 `Thu Jan  1 00:00:00 UTC 1970`이다. RTC에 의존하는 `alarmtimer.0.auto` platform 디바이스도 함께 사라진다 — 노드 하나를 끄면 연쇄 효과가 있다.

3. **코드**: §4.6의 `d1_fdtwalk.c`를 고쳐 `status`가 `"disabled"`인 노드 경로를 출력하게 하고, `d1_nouart.dtb`와 `d1_virt.dtb`에 돌려 차이를 확인하라.
   정답/힌트: `PROP` 분기에서 `strs + nameoff`가 `"status"`이고 값이 `"disabled"`면 `path`를 출력. 결과는 `/pl011@9000000` 한 줄.

4. **관찰**: 게스트에서 `/sys/bus/platform/devices/*/uevent`를 모두 모아 `OF_COMPATIBLE_0`별로 세고, §4.6의 compatible 통계(48개)와 왜 다른지 설명하라.
   정답/힌트: early init으로 소비된 노드(GIC, fixed-clock, cpus), amba로 간 3개, 코드가 만든 디바이스(OF 줄 없음)가 차이를 만든다.

5. **관찰**: QEMU에 `-device virtio-rng-device`(MMIO 버전)를 붙여 부팅하고, §6.3의 `d1_devmap platform` 결과에서 어느 `virtio_mmio` 슬롯에 드라이버가 붙었는지, `/sys/bus/virtio/devices`가 어떻게 바뀌는지 확인하라. PCI 버전(§7)과 비교해 "DT 슬롯은 미리 32개 깔려 있고 장치는 그중 하나에 꽂힌다"는 구조를 설명하라.
   정답/힌트: 랩에서는 맨 위 슬롯 `a003e00.virtio_mmio`에 `virtio-mmio` 드라이버가 붙고 `/sys/bus/virtio/devices`가 `virtio0 virtio1`이 됐다. 슬롯(주소·IRQ)은 non-discoverable이라 DT에 있고, 슬롯 안 장치 종류는 virtio device ID로 discoverable하다 — PrimeCell과 비슷한 "반쯤 discoverable" 구조.

6. **ACPI**: Lima VM에서 `sudo cat /sys/firmware/acpi/tables/DSDT > /tmp/dsdt.aml` 후 (패키지가 있다면) `iasl -d`로 역어셈블해 `ARMH0061` 장치의 `_CRS`(주소·IRQ)를 찾고, DT의 `reg`·`interrupts`와 대응시켜라. `iasl`이 없으면 설치하지 말고 `strings`로 `_HID` 문자열만 확인하라.
   정답/힌트: `_CRS`의 `Memory32Fixed`가 `reg`, `Interrupt`가 `interrupts`에 대응한다.

7. **설명 연습**: 면접관에게 "board file → DT"의 변화를 Don의 SSD 펌웨어 경험(헤더의 `#define` 주소, 보드별 빌드)에 빗대어 1분 안에 설명하는 스크립트를 영어로 써 보라.
   정답/힌트: "In firmware the memory map lives in headers and you rebuild per board; DT moves that header out of the binary into data the bootloader hands over."

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Device Tree (DT) | 장치 트리 | non-discoverable 하드웨어를 노드·속성 트리로 설명하는 데이터 구조 |
| discoverable | 발견 가능 | 장치가 표준 방법으로 정체·자원을 스스로 알려 주는 성질 (PCIe, USB) |
| non-discoverable | 발견 불가 | 위치·배선을 물어볼 표준 방법이 없는 장치 (MMIO 주변장치, I2C 센서) |
| `.dts` / `.dtsi` | DT 소스 / include | 사람이 쓰는 텍스트. `.dtsi`는 SoC 공통 부분 (D5) |
| `dtc` | Device Tree Compiler | `.dts` ↔ `.dtb` 변환, 문법·참조 검사 |
| `.dtb` / FDT | Device Tree Blob / Flattened DT | big-endian 바이너리 형식, magic `0xd00dfeed` |
| unflatten | 펼치기 | 평평한 blob을 `struct device_node` 포인터 트리로 바꾸는 과정 |
| `struct device_node` | DT 노드 객체 | 커널 메모리 안의 노드, `of_root`가 루트 |
| `compatible` | 호환 문자열 | "vendor,device" 형식, 드라이버 매칭 키 (D3) |
| `/chosen` | 선택 노드 | `bootargs`, initrd 위치, `stdout-path` 같은 런타임 설정 통로 |
| `status` | 상태 속성 | `"okay"`면 디바이스 생성, `"disabled"`면 건너뜀 |
| board file | 보드 파일 | DT 이전 ARM의 `arch/arm/mach-*/board-*.c`, C로 쓴 하드웨어 설명 |
| `MACHINE_START` | 머신 기술자 | board file 시대에 보드별 init 함수를 묶던 매크로 |
| `platform_device` | 플랫폼 디바이스 | 버스 enumeration이 없는 장치를 위한 디바이스 객체 |
| AMBA / PrimeCell | ARM 버스 / IP 계열 | 주소는 DT로, 정체는 Peripheral ID 레지스터로 아는 장치 |
| `of_node` | OF 노드 링크 | 디바이스가 가리키는 DT 노드 (`dev->of_node`) |
| `OF_POPULATED` | 생성 완료 표시 | early init이나 populate가 이미 처리한 노드 플래그 |
| earlycon | 초기 콘솔 | 디바이스 모델 이전에 UART 레지스터에 직접 쓰는 콘솔 |
| MODALIAS | 모듈 별명 | uevent의 매칭 문자열, udev가 `modprobe`에 사용 |
| ACPI | 고급 구성·전원 인터페이스 | 펌웨어 제공 테이블 + AML 코드로 하드웨어를 설명하는 PC/서버 표준 |
| `_HID` | Hardware ID | ACPI의 장치 식별자 (DT의 `compatible`에 해당) |
| ECAM | Enhanced Configuration Access Mechanism | PCIe config space를 MMIO로 접근하는 영역 |

---

## 18. 요약 & 체크리스트

PCIe·USB 장치는 config space와 descriptor로 정체와 자원을 스스로 밝히지만, SoC 안의 MMIO 주변장치, I2C·SPI 센서, GPIO·인터럽트 배선, 클록·전원 같은 것들은 물어볼 방법이 없다. 32비트 ARM은 이 정보를 보드마다 C 코드(board file의 `platform_device`·`resource`)로 커널에 박았고, 보드 폭발과 2011년 Linus의 공개 비판을 거치며 Open Firmware·PowerPC에서 온 Device Tree로 옮겨 갔다. DT는 순수 데이터다: `.dts`를 `dtc`가 big-endian FDT(`.dtb`)로 만들고, 부트로더가 `/chosen`을 채워 그 주소를 `x0`로 넘기면, 커널이 early scan → unflatten → early 장치 초기화 → populate(`platform_device`/`amba_device`) → 드라이버 매칭 → `probe()`로 디바이스 모델을 세운다. sysfs의 `of_node`·`driver` 링크와 uevent가 이 사슬을 보여 주고, `status = "disabled"`는 하드웨어를 끄는 게 아니라 디바이스 생성을 막을 뿐이다. ACPI는 같은 문제를 펌웨어 테이블 + AML 코드로 푸는 x86·서버 쪽 답이다.

- [ ] discoverable과 non-discoverable의 차이를 PCIe config space와 MMIO UART·I2C 센서 예로 설명할 수 있다
- [ ] PCIe host bridge는 왜 DT 노드가 필요한지, 그 아래 장치는 왜 필요 없는지 말할 수 있다
- [ ] board file(`platform_device`, `MACHINE_START`)이 무엇이고 왜 DT로 대체됐는지 역사와 함께 설명할 수 있다
- [ ] "커널 하나, 보드 여러 개"를 같은 `Image`·다른 DT 부팅으로 보여 줄 수 있다
- [ ] `.dts → dtc → .dtb → 부트로더 → 커널 → 드라이버` 단계마다 일어나는 일을 말할 수 있다
- [ ] FDT 헤더와 structure block을 C로 파싱할 수 있다 (big-endian 주의)
- [ ] `/proc/device-tree`, `/sys/firmware/fdt`, `/sys/firmware/devicetree/base`의 관계를 설명할 수 있다
- [ ] 디바이스의 `of_node`·`driver` 링크와 uevent로 "DT 노드 → 디바이스 → 드라이버" 사슬의 끊긴 고리를 찾을 수 있다
- [ ] DT 노드가 platform device가 되지 않는 경우(compatible 없음, disabled, early init)를 말할 수 있다
- [ ] DT에 적을 것(하드웨어 사실)과 적지 않을 것(드라이버 정책)을 예로 구분할 수 있다
- [ ] DT와 ACPI의 차이를 표로 말하고, ACPI 머신에 `/proc/device-tree`가 없는 이유를 설명할 수 있다
- [ ] `status = "disabled"` UART에서 earlycon은 나오고 유저 공간 콘솔은 안 나오는 이유를 설명할 수 있다

---

## 참고 자료

- Devicetree Specification (v0.4) — [devicetree.org/specifications](https://www.devicetree.org/specifications/) — FDT 형식(5장), 표준 속성
- Linux 커널 문서 "Linux and the Devicetree" (Grant Likely) — [docs.kernel.org/devicetree/usage-model.html](https://docs.kernel.org/devicetree/usage-model.html) — 역사, platform identification / runtime configuration / device population
- Linux 커널 문서 Devicetree 색인 — [docs.kernel.org/devicetree/index.html](https://docs.kernel.org/devicetree/index.html)
- arm64 부팅 규약 "Booting AArch64 Linux" — [docs.kernel.org/arch/arm64/booting.html](https://docs.kernel.org/arch/arm64/booting.html) — `x0` = DTB 물리 주소
- ACPI 기반 장치 열거 — [docs.kernel.org/firmware-guide/acpi/enumeration.html](https://docs.kernel.org/firmware-guide/acpi/enumeration.html) — `PRP0001`과 DT 호환 매칭
- 커널 소스 (랩의 6.18.55 트리, 온라인은 elixir.bootlin.com): `drivers/of/fdt.c`(early scan, unflatten), `drivers/of/platform.c`(`of_platform_bus_create`), `drivers/of/base.c`(`of_core_init`), `drivers/base/platform.c`(`platform_match`), `drivers/amba/bus.c`(`amba_match`), `arch/arm64/kernel/setup.c`, `arch/arm/mach-omap1/board-osk.c`, `drivers/net/ethernet/smsc/smc91x.c`
- [elixir.bootlin.com — drivers/of/platform.c](https://elixir.bootlin.com/linux/latest/source/drivers/of/platform.c)
- LWN.net, "ARM wrestling" (2011) — 2011년 ARM 플랫폼 코드 논쟁 정리
- Chris Simmonds, "Mastering Embedded Linux Programming" (3rd ed.) — 부트로더 장의 Device Tree 소개 절
- 이 노트의 다음: D2(문법), D3(드라이버 매칭), D4(자원 기술), D5(파일 구조), C3(U-Boot의 `fdt` 명령), C8(ACPI·UEFI)
