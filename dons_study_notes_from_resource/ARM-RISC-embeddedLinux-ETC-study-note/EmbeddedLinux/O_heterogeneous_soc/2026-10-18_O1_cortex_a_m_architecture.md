# O1. Cortex-A + Cortex-M 구조 — 한 칩 안의 "Linux 컴퓨터"와 "MCU"는 메모리·인터럽트·자원을 어떻게 나눠 쓰나

> **이 노트를 다 읽으면**: SoC가 왜 Linux 코어 옆에 MCU 코어를 두는지 네 가지 이유로 설명 · i.MX93 / AM62x / Jetson Orin의 보조 코어 배치를 커널 DT에서 직접 찾아 읽기 · `reserved-memory` + `no-map` 캐브아웃을 만들고 Linux가 안 건드리는 걸 증명 · 공유 메모리 링 + doorbell로 두 "코어"가 메시지를 주고받는 코드를 짜고 왜 실물은 메일박스 인터럽트를 쓰는지 수치로 말하기
> **왜 필요한가**: 로봇·차량·국방 장비 면접의 단골 설계 질문 "모터 루프/CAN/전원 감시는 어디서 돌리나, Linux와 MCU 코어는 메모리를 어떻게 나누나, Linux가 죽으면 어떻게 되나"에 SoC 이름과 주소까지 대며 답하기 위해
> **Don 기준 난이도**: 멀티코어 펌웨어의 IPC 레지스터·공유 SRAM·캐시 관리는 이미 손에 익음 / 새로 배울 것은 그걸 **Linux 쪽에서** 선언하는 방법(DT `reserved-memory`, mailbox 프레임워크, remoteproc 진입점)과 벤더별 배치 지형
> **선행 노트**: D4(DT 자원), F10(CMA·reserved-memory), H1(MMIO), H10(메일박스·hwspinlock), N4(Linux vs RTOS vs MCU 분담). 이어지는 노트: O2(remoteproc·rpmsg), O3(외부 MCU 링크), O5(SoC 지형)

---

## 0. 큰 그림 — 칩 하나에 두 종류의 컴퓨터

Don이 SSD 컨트롤러에서 본 구조를 떠올리면 쉽다. SSD SoC에는 호스트 명령을 처리하는 코어, FTL을 돌리는 코어, NAND 채널을 다루는 코어가 따로 있고, 서로 IPC 레지스터(doorbell)와 공유 SRAM으로 이야기했다. 요즘 응용 프로세서도 똑같다. 다만 한쪽이 **Linux를 돌리는 Cortex-A 클러스터**(MMU, 큰 캐시, 수 초 걸리는 부팅)이고, 다른 쪽이 **RTOS나 bare-metal을 돌리는 Cortex-M/R 코어**(TCM, MPU, ms 단위 부팅)다. 이런 구성을 **이종(heterogeneous) 멀티코어**, 각 코어가 서로 다른 OS를 독립적으로 돌리는 방식을 **AMP**(Asymmetric Multi-Processing)라고 부른다. Linux가 모든 코어를 한 커널로 스케줄링하는 SMP와 반대말이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="660" height="280" rx="8" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="20" y="30" font-size="13" font-weight="bold">SoC (한 다이)</text>
  <rect x="30" y="42" width="260" height="110" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="66" text-anchor="middle" font-size="13" font-weight="bold">Cortex-A 클러스터 (Linux)</text>
  <text x="160" y="92" text-anchor="middle" font-size="12">카메라 · AI · ROS 2 · 네트워크</text>
  <text x="160" y="114" text-anchor="middle" font-size="12">MMU, 캐시 코히어런트 인터커넥트</text>
  <text x="160" y="136" text-anchor="middle" font-size="12">부팅 수 초, 지연 꼬리 ms</text>
  <rect x="390" y="42" width="260" height="110" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="520" y="66" text-anchor="middle" font-size="13" font-weight="bold">Cortex-M/R 코어 (RTOS)</text>
  <text x="520" y="92" text-anchor="middle" font-size="12">모터 루프 · CAN · 전원 감시</text>
  <text x="520" y="114" text-anchor="middle" font-size="12">TCM, MPU, 캐시 작거나 없음</text>
  <text x="520" y="136" text-anchor="middle" font-size="12">부팅 ms, 지터 us</text>
  <rect x="300" y="52" width="80" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="74" text-anchor="middle" font-size="12">메일박스</text>
  <rect x="300" y="104" width="80" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="126" text-anchor="middle" font-size="12">공유 SRAM</text>
  <line x1="290" y1="69" x2="298" y2="69" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="382" y1="69" x2="388" y2="69" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="290" y1="121" x2="298" y2="121" stroke="currentColor"/>
  <line x1="382" y1="121" x2="390" y2="121" stroke="currentColor"/>
  <rect x="25" y="180" width="145" height="92" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="97" y="202" text-anchor="middle" font-size="13" font-weight="bold">1. 빠른 부팅</text>
  <text x="97" y="226" text-anchor="middle" font-size="12">후방 카메라 · CAN을</text>
  <text x="97" y="246" text-anchor="middle" font-size="12">Linux보다 먼저</text>
  <rect x="185" y="180" width="145" height="92" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="257" y="202" text-anchor="middle" font-size="13" font-weight="bold">2. 실시간 루프</text>
  <text x="257" y="226" text-anchor="middle" font-size="12">us 지터 제어 루프</text>
  <text x="257" y="246" text-anchor="middle" font-size="12">Linux 지연과 분리</text>
  <rect x="345" y="180" width="145" height="92" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="417" y="202" text-anchor="middle" font-size="13" font-weight="bold">3. 저전력 상시</text>
  <text x="417" y="226" text-anchor="middle" font-size="12">A 코어는 꺼 두고</text>
  <text x="417" y="246" text-anchor="middle" font-size="12">센서 · 웨이크업 감시</text>
  <rect x="505" y="180" width="145" height="92" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="577" y="202" text-anchor="middle" font-size="13" font-weight="bold">4. 안전 섬</text>
  <text x="577" y="226" text-anchor="middle" font-size="12">별도 전원 · 클럭</text>
  <text x="577" y="246" text-anchor="middle" font-size="12">Linux 죽어도 생존</text>
</svg>
```
그림 1 — 이종 SoC의 기본 모양. 두 종류의 코어가 메일박스(신호)와 공유 메모리(본문)로 이야기하고, 보조 코어를 두는 이유는 대개 아래 네 가지 중 하나다.

N4에서 "무엇을 MCU로 내리나"(판단 기준)를 다뤘다면, 이 노트는 그 MCU가 **같은 칩 안에** 있을 때의 하드웨어 구조를 본다: 어떤 SoC에 어떤 코어가 있는지(§1~2), 메모리를 어떻게 나누는지(§3~4), 캐시를 어떻게 맞추는지(§5), 신호를 어떻게 보내는지(§6), 클럭·핀·보안 자원을 누가 가지는지(§7), 누가 먼저 부팅하는지(§8), 한쪽이 죽으면 어떻게 되는지(§9). 그리고 QEMU로 두 가지를 직접 해 본다: **캐브아웃을 만들고 Linux가 손 안 대는 것 증명**(§4), **두 QEMU 인스턴스를 공유 메모리로 이어 "A 코어 ↔ M 코어" 메시지 교환과 왕복 시간 측정**(§10~11).

### 실습 환경 한 줄

VM 호스트 커널은 `7.0.0-38-generic`(Ubuntu 26.04, aarch64), 게스트 커널은 공용 랩의 `/var/lab/out/Image-dom`(`6.18.55-dom`), 게스트 루트는 BusyBox initramfs 복사본이다. QEMU 10.2.1, KVM 없이 TCG로 돈다. **이 VM은 동시에 19명이 쓰고 있어 load average가 11~23이었다. 노트의 모든 시간 값은 TCG 에뮬레이션 + 과부하 상태의 숫자이지 실제 하드웨어 수치가 아니다.** 비율과 경향만 본다.

---

## 1. 왜 Linux 코어 옆에 MCU 코어를 두나 — 네 가지 이유

### 1.1 빠른 부팅 (early CAN, 후방 카메라)

차량 규정은 시동 후 짧은 시간 안에 후방 카메라 영상이 떠야 한다(정확한 초 수는 지역·규정마다 다르므로 데이터시트·규정 문서로 확인할 것). CAN 버스도 ECU가 켜진 직후부터 메시지에 응답해야 한다. Linux는 ROM → TF-A → U-Boot → 커널 → systemd까지 수 초가 걸린다(C1, M10). 그래서 **부트로더 단계에서 M 코어를 먼저 띄워** CAN 응답·카메라 미리보기·LED 표시를 맡기고, Linux는 다 뜬 뒤에 그 코어에 "붙는다"(attach, §8).

### 1.2 실시간 제어 루프

PREEMPT_RT Linux도 최악 지연이 수십 us 수준까지는 내려오지만(N1~N3), 인증 가능한 결정론은 아니다. 20 kHz FOC 전류 루프, 1 kHz 짐벌 자세 루프처럼 **놓치면 하드웨어가 위험해지는 루프**는 인터럽트 지연이 수십 사이클로 고정된 Cortex-M/R에서 돌린다. Linux는 목표값(setpoint)만 내려보낸다.

### 1.3 저전력 상시 동작 (always-on)

배터리 장비는 대부분의 시간을 A 코어와 DDR을 끈 상태(suspend)로 보낸다(N5). 그동안 IMU 움직임 감지, 버튼, 충전 상태, RTC 알람을 보는 건 수 mW로 도는 작은 코어다. 이 코어는 **always-on 전원 도메인**에 있고, 조건이 맞으면 A 코어를 깨운다. Jetson의 SPE, AM62x의 wake-up 도메인 R5F가 이런 위치다(§2).

### 1.4 안전 섬 (safety island)

ISO 26262 / IEC 61508 같은 기능 안전 인증을 받으려면 안전 기능이 **Linux와 독립적으로** 동작해야 한다. 그래서 별도 전원·클럭·리셋·워치독을 가진 "섬"에 lockstep(두 코어가 같은 명령을 동시에 실행하고 결과를 비교) Cortex-R을 둔다. Linux가 패닉해도 섬은 모터를 안전 정지시키고 고장 코드를 남긴다.

| 이유 | 대표 작업 | 필요한 HW 성질 | Linux가 못 하는 이유 |
|---|---|---|---|
| 빠른 부팅 | CAN 응답, 후방 카메라, 부팅 LED | 부트로더가 먼저 실행 가능, 자기 메모리(TCM/OCRAM) | 커널+유저 공간까지 수 초 |
| 실시간 루프 | FOC, 짐벌, 엔코더 | 고정 인터럽트 지연, TCM, 캐시 없음 | 스케줄러·IRQ 스레드·캐시 미스로 지연 꼬리 |
| 저전력 상시 | 움직임 감지, 웨이크업 | always-on 도메인, 작은 SRAM | A 코어·DDR 켜 두면 전력 수백 mW 이상 |
| 안전 섬 | 워치독, 안전 정지, 고장 기록 | 독립 전원/클럭/리셋, lockstep, ECC | 인증 불가, 공유 자원 장애 전파 |

**흔한 함정**: "M 코어가 있으니 실시간은 해결"이라고 말하는 것. 같은 칩 안의 M 코어는 DDR·인터커넥트·클럭 트리·전원 레일을 Linux와 **공유할 수 있다**. 코드를 DDR에 두면 Linux의 GPU 트래픽에 끌려 지연이 흔들리고, Linux 드라이버가 공유 클럭을 끄면 같이 멈춘다. 그래서 §3(메모리 배치)과 §7(자원 소유권)이 실제 설계의 핵심이다.

---

## 2. 대표 SoC 배치 — 커널 DT에서 직접 확인하기

벤더 자료를 외우는 대신, **mainline 커널 트리의 DT에 무엇이 들어 있는지** 직접 본다. DT에 노드가 있으면 그 코어·메일박스·캐브아웃이 실제로 존재한다는 강한 증거다. 아래 표에서 "확인"은 `/var/lab/src/linux-6.18.55`에서 grep으로 본 것, "기억 기반"은 데이터시트로 확인이 필요한 것이다.

| SoC | 응용 코어 (확인) | 보조 코어 | 메일박스 | DT 상태 |
|---|---|---|---|---|
| NXP i.MX93 | Cortex-A55 x2 (`imx93.dtsi`) | Cortex-M33 `remoteproc-cm33` (확인) | MU1/MU2 `fsl,imx93-mu` (확인) | EVK DT에 vring·rsc-table 캐브아웃까지 있음 |
| NXP i.MX8M Plus | Cortex-A53 x4 (기억 기반) | Cortex-M7 — 드라이버에 `fsl,imx8mp-cm7` 있음, mainline EVK DT 노드는 없음 | MU `fsl,imx8mp-mu` (확인) | M7 노드는 벤더 BSP DT나 보드 DT에서 추가 |
| ST STM32MP15 | Cortex-A7 (32비트 `arch/arm` DT) | Cortex-M4 `st,stm32mp1-m4` (확인) | IPCC `st,stm32mp1-ipcc` (확인) | DK 보드 DT에 MCU SRAM·vring 캐브아웃 있음 |
| ST STM32MP25 | Cortex-A35 (기억 기반) | Cortex-M33 (기억 기반) — 6.18 mainline remoteproc 드라이버에 없음 | IPCC (기억 기반) | RIFSC 접근 제어 노드는 확인 |
| TI AM62x | Cortex-A53 x4 (`k3-am625.dtsi`) | MCU 도메인 Cortex-M4F `ti,am64-m4fss` + wake-up 도메인 Cortex-R5F `ti,am62-r5fss` (확인) | `ti,am64-mailbox` (확인) | `k3-am62-ti-ipc-firmware.dtsi`에 캐브아웃·메일박스 연결 |
| NVIDIA Jetson Orin (Tegra234) | Cortex-A78AE x12 (`tegra234.dtsi`) | BPMP (확인, 코어 종류는 기억 기반), SPE·SCE·RCE (패브릭/AON 노드만 확인, 코어 종류 기억 기반) | HSP `nvidia,tegra234-hsp` (확인) | BPMP는 SYSRAM tx/rx 창 + HSP doorbell |
| Raspberry Pi 5 | Cortex-A76 (기억 기반) | RP1 사우스브리지 안의 Cortex-M3 (기억 기반) | 해당 없음 (PCIe 장치) | `drivers/misc/rp1/rp1_pci.c` + RP1 DT 오버레이 (확인) |

### 2.1 예제 1 — i.MX93: M33 노드, MU, EVK 캐브아웃

무엇을 확인하나: i.MX93의 보조 코어가 DT에서 어떻게 선언되고, 보드 DT가 메모리와 메일박스를 어떻게 연결하는지.

```sh
cd /var/lab/src/linux-6.18.55/arch/arm64/boot/dts/freescale
grep -n -A4 "imx93-cm33" imx93.dtsi
grep -n -A7 "mu1: mailbox" imx93.dtsi
grep -n -A4 "vdev0vring0:\|rsc_table:\|vdevbuffer:" imx93-11x11-evk.dts
grep -n -A8 "^&cm33" imx93-11x11-evk.dts
```

```text
9:	cm33: remoteproc-cm33 {
10:		compatible = "fsl,imx93-cm33";
11-		clocks = <&clk IMX93_CLK_CM33_GATE>;
12-		status = "disabled";
13-	};
49:	mu1: mailbox@44230000 {
50-		compatible = "fsl,imx93-mu", "fsl,imx8ulp-mu";
51-		reg = <0x44230000 0x10000>;
52-		interrupts = <GIC_SPI 22 IRQ_TYPE_LEVEL_HIGH>;
53-		clocks = <&clk IMX93_CLK_MU1_B_GATE>;
54-		#mbox-cells = <2>;
55-		status = "disabled";
56-	};
51:		vdev0vring0: vdev0vring0@a4000000 {
52-			reg = <0 0xa4000000 0 0x8000>;
53-			no-map;
54-		};
55-
--
71:		rsc_table: rsc-table@2021e000 {
72-			reg = <0 0x2021e000 0 0x1000>;
73-			no-map;
74-		};
75-
76:		vdevbuffer: vdevbuffer@a4020000 {
77-			compatible = "shared-dma-pool";
78-			reg = <0 0xa4020000 0 0x100000>;
79-			no-map;
80-		};
244:&cm33 {
245-	mbox-names = "tx", "rx", "rxdb";
246-	mboxes = <&mu1 0 1>,
247-		 <&mu1 1 1>,
248-		 <&mu1 3 1>;
249-	memory-region = <&vdevbuffer>, <&vdev0vring0>, <&vdev0vring1>,
250-			<&vdev1vring0>, <&vdev1vring1>, <&rsc_table>;
251-	status = "okay";
252-};
```

출력에서 볼 것: SoC `.dtsi`는 "이 칩에 M33이 있다"(`status = "disabled"`)만 말하고, **보드 `.dts`가 켜면서 메모리와 메일박스를 붙인다**(D5의 dtsi/dts 분업 그대로). 캐브아웃 세 종류가 다 보인다: DDR의 vring(32 KiB씩, `no-map`), DDR의 메시지 버퍼(`shared-dma-pool` + `no-map`, 1 MiB), 그리고 **M33 TCM 안의** resource table(`0x2021e000`). `mboxes`의 두 셀은 MU 채널 타입·인덱스다(H10 §1.3). 이 노드들을 remoteproc 드라이버가 어떻게 쓰는지는 O2에서 본다.

### 2.2 예제 2 — 같은 TCM, 두 개의 주소: `imx_rproc`의 주소 변환표

무엇을 확인하나: M 코어가 보는 주소와 A 코어(시스템 버스)가 보는 주소가 다르다는 사실. 드라이버가 이 표로 ELF 세그먼트 주소를 번역한다.

```sh
grep -n -A20 "imx_rproc_att_imx93\[\]" /var/lab/src/linux-6.18.55/drivers/remoteproc/imx_rproc.c
```

```text
121:static const struct imx_rproc_att imx_rproc_att_imx93[] = {
122-	/* dev addr , sys addr  , size	    , flags */
123-	/* TCM CODE NON-SECURE */
124-	{ 0x0FFC0000, 0x201C0000, 0x00040000, ATT_OWN | ATT_IOMEM },
125-
126-	/* TCM CODE SECURE */
127-	{ 0x1FFC0000, 0x201C0000, 0x00040000, ATT_OWN | ATT_IOMEM },
128-
129-	/* TCM SYS NON-SECURE*/
130-	{ 0x20000000, 0x20200000, 0x00040000, ATT_OWN | ATT_IOMEM },
131-
132-	/* TCM SYS SECURE*/
133-	{ 0x30000000, 0x20200000, 0x00040000, ATT_OWN | ATT_IOMEM },
134-
135-	/* DDR */
136-	{ 0x80000000, 0x80000000, 0x10000000, 0 },
137-	{ 0x90000000, 0x80000000, 0x10000000, 0 },
138-
139-	{ 0xC0000000, 0xC0000000, 0x10000000, 0 },
140-	{ 0xD0000000, 0xC0000000, 0x10000000, 0 },
141-};
```

출력에서 볼 것: M33은 자기 코드 TCM을 `0x0FFC0000`(non-secure)·`0x1FFC0000`(secure 별칭)에서 보지만, A55는 같은 256 KiB를 `0x201C0000`에서 본다. 펌웨어 ELF는 M33 주소로 링크되므로, Linux가 그걸 TCM에 써 넣으려면 이 표로 `da`(device address) → `sys`(시스템 물리 주소)로 바꿔야 한다. `ATT_IOMEM`은 "이건 RAM이 아니라 버스 너머의 SRAM이니 `ioremap` 계열로 매핑하라"는 표시다. 위 EVK의 resource table `0x2021e000`은 `0x20200000 + 0x1e000`, 즉 **TCM SYS 영역 안**이다.

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="120" y="24" text-anchor="middle" font-size="13" font-weight="bold">M33이 보는 주소 (da)</text>
  <text x="540" y="24" text-anchor="middle" font-size="13" font-weight="bold">A55 / 시스템 버스 주소</text>
  <rect x="30" y="40" width="180" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="120" y="58" text-anchor="middle" font-size="12">0x0FFC0000 TCM code</text>
  <text x="120" y="76" text-anchor="middle" font-size="12">256 KiB (non-secure)</text>
  <rect x="30" y="94" width="180" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="5,4"/>
  <text x="120" y="112" text-anchor="middle" font-size="12">0x1FFC0000 TCM code</text>
  <text x="120" y="130" text-anchor="middle" font-size="12">secure 별칭</text>
  <rect x="30" y="148" width="180" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="120" y="166" text-anchor="middle" font-size="12">0x20000000 TCM sys</text>
  <text x="120" y="184" text-anchor="middle" font-size="12">256 KiB (non-secure)</text>
  <rect x="30" y="230" width="180" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="248" text-anchor="middle" font-size="12">0x80000000 DDR 창</text>
  <text x="120" y="266" text-anchor="middle" font-size="12">256 MiB + 별칭 창</text>
  <rect x="450" y="40" width="180" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="540" y="58" text-anchor="middle" font-size="12">0x201C0000 TCM code</text>
  <text x="540" y="76" text-anchor="middle" font-size="12">ioremap_wc로 접근</text>
  <rect x="450" y="148" width="180" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="540" y="166" text-anchor="middle" font-size="12">0x20200000 TCM sys</text>
  <text x="540" y="184" text-anchor="middle" font-size="12">rsc-table @0x2021e000</text>
  <rect x="450" y="230" width="180" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="540" y="248" text-anchor="middle" font-size="12">0x80000000 DDR</text>
  <text x="540" y="266" text-anchor="middle" font-size="12">Linux RAM과 같은 칩</text>
  <line x1="212" y1="62" x2="446" y2="62" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="212" y1="116" x2="446" y2="70" stroke="currentColor" stroke-dasharray="4,3" marker-end="url(#ah2)"/>
  <line x1="212" y1="170" x2="446" y2="170" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="212" y1="252" x2="446" y2="252" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="330" y="304" text-anchor="middle" font-size="12">출처: drivers/remoteproc/imx_rproc.c imx_rproc_att_imx93[] (6.18.55)</text>
</svg>
```
그림 2 — i.MX93의 주소 별칭. 같은 SRAM이 M33에게는 `0x0FFC0000`, A55에게는 `0x201C0000`이다. 펌웨어 ELF 주소를 그대로 Linux 물리 주소로 쓰면 엉뚱한 곳에 쓴다.

**Don 경험과 연결**: SSD 컨트롤러에서 각 코어의 ITCM이 자기에게는 `0x0`, 디버거·다른 코어에게는 글로벌 주소로 보이던 것과 같다. JTAG으로 다른 코어 TCM을 덤프할 때 "글로벌 별칭 주소"를 쓰던 기억이 있다면 이 표가 바로 그것이다.

### 2.3 예제 3 — TI AM62x: MCU 도메인 M4F와 wake-up 도메인 R5F

무엇을 확인하나: 한 SoC에 보조 코어가 둘, 각각 다른 전원 도메인에 있는 구조.

```sh
cd /var/lab/src/linux-6.18.55/arch/arm64/boot/dts/ti
grep -n -A11 "mcu_m4fss: m4fss" k3-am62-mcu.dtsi
grep -n -A13 "wkup_r5fss0_core0: r5f" k3-am62-wakeup.dtsi
sed -n 8,20p k3-am62-ti-ipc-firmware.dtsi
grep -n -A4 "wkup_r5fss0_core0_memory_region: memory" k3-am62x-sk-common.dtsi
```

```text
178:	mcu_m4fss: m4fss@5000000 {
179-		compatible = "ti,am64-m4fss";
180-		reg = <0x00 0x5000000 0x00 0x30000>,
181-		      <0x00 0x5040000 0x00 0x10000>;
182-		reg-names = "iram", "dram";
183-		resets = <&k3_reset 9 1>;
184-		firmware-name = "am62-mcu-m4f0_0-fw";
185-		ti,sci = <&dmsc>;
186-		ti,sci-dev-id = <9>;
187-		ti,sci-proc-ids = <0x18 0xff>;
188-		status = "disabled";
189-	};
118:		wkup_r5fss0_core0: r5f@78000000 {
119-			compatible = "ti,am62-r5f";
120-			reg = <0x78000000 0x00008000>,
121-			      <0x78100000 0x00008000>;
122-			reg-names = "atcm", "btcm";
123-			resets = <&k3_reset 121 1>;
124-			firmware-name = "am62-wkup-r5f0_0-fw";
125-			ti,atcm-enable = <1>;
126-			ti,btcm-enable = <1>;
127-			ti,loczrama = <1>;
128-			ti,sci = <&dmsc>;
129-			ti,sci-dev-id = <121>;
130-			ti,sci-proc-ids = <0x01 0xff>;
131-			status = "disabled";
&reserved_memory {
	mcu_m4fss_dma_memory_region: memory@9cb00000 {
		compatible = "shared-dma-pool";
		reg = <0x00 0x9cb00000 0x00 0x100000>;
		no-map;
	};

	mcu_m4fss_memory_region: memory@9cc00000 {
		compatible = "shared-dma-pool";
		reg = <0x00 0x9cc00000 0x00 0xe00000>;
		no-map;
	};
};
59:		wkup_r5fss0_core0_memory_region: memory@9db00000 {
60-			compatible = "shared-dma-pool";
61-			reg = <0x00 0x9db00000 0x00 0xc00000>;
62-			no-map;
63-		};
```

출력에서 볼 것: M4F는 192 KiB IRAM + 64 KiB DRAM(`0x30000`, `0x10000`), R5F는 32 KiB ATCM + 32 KiB BTCM을 가진다. 둘 다 DDR에 **두 개씩** 캐브아웃을 받는다: 1 MiB "DMA 영역"(vring·버퍼용)과 큰 "코드/데이터 영역"(M4F 14 MiB, R5F 12 MiB). 리셋·전원은 직접 레지스터가 아니라 `ti,sci`(TI System Controller Interface)로 **보안 관리 코어(DMSC/TIFS)에게 요청**한다는 점이 i.MX와 다르다. AM62x의 wake-up R5F는 TI SDK에서 **DM(Device Manager)** 펌웨어를 돌려 전원·클럭 요청을 처리하는 시스템 코어 역할을 겸한다(기억 기반 — TI 문서로 확인할 것). 그래서 "R5F는 내 마음대로 쓰는 코어"가 아니다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <rect x="10" y="10" width="660" height="310" rx="8" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="20" y="30" font-size="13" font-weight="bold">TI AM62x (DT로 확인한 것 실선, 기억 기반 점선)</text>
  <rect x="25" y="45" width="300" height="130" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="175" y="68" text-anchor="middle" font-size="13" font-weight="bold">MAIN 도메인</text>
  <text x="175" y="92" text-anchor="middle" font-size="12">Cortex-A53 x4 (Linux)</text>
  <text x="175" y="114" text-anchor="middle" font-size="12">mailbox0_cluster0 @0x29000000</text>
  <text x="175" y="136" text-anchor="middle" font-size="12">GPU, CSI-2, DSS, CPSW</text>
  <text x="175" y="158" text-anchor="middle" font-size="12">DDR 컨트롤러</text>
  <rect x="345" y="45" width="150" height="130" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="420" y="68" text-anchor="middle" font-size="13" font-weight="bold">MCU 도메인</text>
  <text x="420" y="92" text-anchor="middle" font-size="12">Cortex-M4F</text>
  <text x="420" y="114" text-anchor="middle" font-size="12">IRAM 192 KiB</text>
  <text x="420" y="136" text-anchor="middle" font-size="12">DRAM 64 KiB</text>
  <text x="420" y="158" text-anchor="middle" font-size="12">MCU CAN, UART</text>
  <rect x="510" y="45" width="150" height="130" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="585" y="68" text-anchor="middle" font-size="13" font-weight="bold">WKUP 도메인</text>
  <text x="585" y="92" text-anchor="middle" font-size="12">Cortex-R5F</text>
  <text x="585" y="114" text-anchor="middle" font-size="12">ATCM/BTCM 32K x2</text>
  <text x="585" y="136" text-anchor="middle" font-size="12">DM 펌웨어 겸함</text>
  <text x="585" y="158" text-anchor="middle" font-size="12">DMSC/TIFS (ti,sci)</text>
  <rect x="25" y="195" width="635" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="342" y="222" text-anchor="middle" font-size="12">DDR: Linux RAM ... 0x9cb00000 M4F DMA 1M · 0x9cc00000 M4F 14M · 0x9db00000 R5F 12M ...</text>
  <rect x="25" y="255" width="300" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5,4"/>
  <text x="175" y="276" text-anchor="middle" font-size="12">부팅: ROM → R5F SPL이 먼저 뜸</text>
  <text x="175" y="295" text-anchor="middle" font-size="12">(K3 부트 흐름, 기억 기반)</text>
  <rect x="345" y="255" width="315" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5,4"/>
  <text x="502" y="276" text-anchor="middle" font-size="12">리셋 · 전원 · 클럭 = TI-SCI 메시지로 요청</text>
  <text x="502" y="295" text-anchor="middle" font-size="12">Linux가 레지스터 직접 안 만짐</text>
</svg>
```
그림 3 — AM62x 배치. 보조 코어 둘이 각자 다른 전원 도메인에 있고, 리셋·전원은 보안 관리 코어에게 메시지로 요청한다.

### 2.4 예제 4 — Jetson Orin(Tegra234) BPMP, STM32MP1 M4, RPi5 RP1

무엇을 확인하나: "보조 코어"가 꼭 사용자 펌웨어용은 아니다. Jetson의 BPMP는 클럭·리셋·전원을 맡는 **시스템 관리 코어**이고, Linux는 그 클라이언트다.

```sh
cd /var/lab/src/linux-6.18.55/arch
sed -n 5292,5322p arm64/boot/dts/nvidia/tegra234.dtsi | grep -v "^$"
grep -n "hsp_aon: hsp\|sce-fabric@\|rce-fabric@\|gpio_aon: gpio" arm64/boot/dts/nvidia/tegra234.dtsi
grep -n -A7 "m4_rproc: m4@" arm/boot/dts/st/stm32mp151.dtsi
ls /var/lab/src/linux-6.18.55/drivers/misc/rp1
```

```text
	sram@40000000 {
		compatible = "nvidia,tegra234-sysram", "mmio-sram";
		reg = <0x0 0x40000000 0x0 0x80000>;
		#address-cells = <1>;
		#size-cells = <1>;
		ranges = <0x0 0x0 0x40000000 0x80000>;
		no-memory-wc;
		cpu_bpmp_tx: sram@70000 {
			reg = <0x70000 0x1000>;
			label = "cpu-bpmp-tx";
			pool;
		};
		cpu_bpmp_rx: sram@71000 {
			reg = <0x71000 0x1000>;
			label = "cpu-bpmp-rx";
			pool;
		};
	};
	bpmp: bpmp {
		compatible = "nvidia,tegra234-bpmp", "nvidia,tegra186-bpmp";
		mboxes = <&hsp_top0 TEGRA_HSP_MBOX_TYPE_DB
				    TEGRA_HSP_DB_MASTER_BPMP>;
		shmem = <&cpu_bpmp_tx>, <&cpu_bpmp_rx>;
		#clock-cells = <1>;
		#reset-cells = <1>;
		#power-domain-cells = <1>;
3887:		sce-fabric@b600000 {
3894:		rce-fabric@be00000 {
3901:		hsp_aon: hsp@c150000 {
3992:		gpio_aon: gpio@c2f0000 {
2033:		m4_rproc: m4@10000000 {
2034-			compatible = "st,stm32mp1-m4";
2035-			reg = <0x10000000 0x40000>,
2036-			      <0x30000000 0x40000>,
2037-			      <0x38000000 0x10000>;
2038-			resets = <&rcc MCU_R>;
2039-			reset-names = "mcu_rst";
2040-			st,syscfg-holdboot = <&rcc 0x10C 0x1>;
Kconfig
Makefile
rp1-pci.dtso
rp1_pci.c
```

출력에서 볼 것: BPMP 통신은 이 노트 §10 실습과 **똑같은 모양**이다: SYSRAM의 4 KiB tx 창 + 4 KiB rx 창(공유 메모리) + HSP doorbell(신호). 그리고 `#clock-cells`, `#reset-cells`, `#power-domain-cells`가 BPMP 노드에 있다는 건 Linux의 클럭·리셋·전원 도메인 **공급자(provider)가 BPMP 펌웨어**라는 뜻이다(D4의 provider/consumer 구조). STM32MP1 M4는 MCU SRAM 3블록(`0x10000000`, `0x30000000`, `0x38000000`)과 RCC의 "hold boot" 비트로 시작을 제어한다. RPi5의 RP1은 Linux 쪽에서 보면 그냥 **PCIe 장치**다(H11). 안에 있는 Cortex-M3는 RPi 펌웨어 몫이고 mainline에는 그 코어용 remoteproc이 없다(기억 기반).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="660" height="280" rx="8" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="20" y="30" font-size="13" font-weight="bold">NVIDIA Tegra234 (Orin) — 실선 DT 확인, 점선 기억 기반</text>
  <rect x="25" y="45" width="230" height="100" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="140" y="70" text-anchor="middle" font-size="13" font-weight="bold">CCPLEX</text>
  <text x="140" y="94" text-anchor="middle" font-size="12">Cortex-A78AE x12 (Linux)</text>
  <text x="140" y="116" text-anchor="middle" font-size="12">L4T 커널, CUDA, ROS 2</text>
  <rect x="300" y="45" width="160" height="100" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="380" y="70" text-anchor="middle" font-size="13" font-weight="bold">BPMP</text>
  <text x="380" y="94" text-anchor="middle" font-size="12">클럭 · 리셋 · 전원</text>
  <text x="380" y="116" text-anchor="middle" font-size="12">provider 펌웨어</text>
  <rect x="490" y="45" width="165" height="100" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="5,4"/>
  <text x="572" y="70" text-anchor="middle" font-size="13" font-weight="bold">AON 클러스터</text>
  <text x="572" y="94" text-anchor="middle" font-size="12">SPE (Cortex-R5)</text>
  <text x="572" y="116" text-anchor="middle" font-size="12">hsp_aon, gpio_aon</text>
  <rect x="25" y="175" width="230" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="140" y="202" text-anchor="middle" font-size="12">SYSRAM tx 0x70000 · rx 0x71000</text>
  <rect x="300" y="175" width="160" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="380" y="202" text-anchor="middle" font-size="12">HSP doorbell</text>
  <rect x="490" y="175" width="165" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5,4"/>
  <text x="572" y="196" text-anchor="middle" font-size="12">SCE 안전 · RCE 카메라</text>
  <text x="572" y="212" text-anchor="middle" font-size="12">패브릭 노드만 확인</text>
  <line x1="140" y1="147" x2="140" y2="172" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="255" y1="120" x2="298" y2="185" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="380" y1="173" x2="380" y2="148" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="340" y="252" text-anchor="middle" font-size="12">Linux: 본문을 tx 창에 쓰고 → BPMP doorbell → BPMP가 rx 창에 응답</text>
  <text x="340" y="274" text-anchor="middle" font-size="12">= §10 실습의 "공유 메모리 링 + doorbell"과 같은 모양</text>
</svg>
```
그림 4 — Tegra234. 보조 코어 중 BPMP는 Linux의 클럭·리셋·전원 공급자이므로, BPMP 펌웨어가 멈추면 Linux 드라이버 probe부터 실패한다.

### 2.5 예제 5 — 랩 커널에 켜진 이종 SoC 드라이버

무엇을 확인하나: 이 노트에서 쓰는 `Image-dom`에 무엇이 켜져 있는지.

```sh
grep -E "^CONFIG_(REMOTEPROC|MAILBOX|UIO|UIO_PCI_GENERIC|RPMSG|IMX_REMOTEPROC|TI_K3_M4_REMOTEPROC|TI_K3_R5_REMOTEPROC|IMX_MBOX|OMAP2PLUS_MBOX|TEGRA_HSP_MBOX|STM32_RPROC|STM32_IPCC|DEVMEM|STRICT_DEVMEM|ARCH_KEEP_MEMBLOCK|DMA_CMA)=" /var/lab/out/linux-build-dom/.config
```

```text
CONFIG_ARCH_KEEP_MEMBLOCK=y
CONFIG_DEVMEM=y
CONFIG_UIO=y
CONFIG_UIO_PCI_GENERIC=m
CONFIG_MAILBOX=y
CONFIG_IMX_MBOX=y
CONFIG_OMAP2PLUS_MBOX=m
CONFIG_TEGRA_HSP_MBOX=y
CONFIG_REMOTEPROC=y
CONFIG_IMX_REMOTEPROC=y
CONFIG_TI_K3_M4_REMOTEPROC=m
CONFIG_TI_K3_R5_REMOTEPROC=m
CONFIG_RPMSG=y
CONFIG_DMA_CMA=y
CONFIG_STRICT_DEVMEM=y
```

출력에서 볼 것: i.MX(MU + `imx_rproc`), TI(mailbox + K3 M4/R5), Tegra(HSP)는 arm64 defconfig 계열에 켜져 있다. `STM32_RPROC`·`STM32_IPCC`는 없다 — STM32MP1은 32비트 `arch/arm` SoC라 arm64 defconfig에 안 들어간다. QEMU `virt`에는 이 하드웨어가 없으니 드라이버들이 probe되지는 않는다. 그래서 아래 실습은 **구조만 같은 대용품**으로 한다.

---

## 3. 메모리 지도와 소유권 — 누가 어느 RAM을 가지나

### 3.1 직관 — 세 종류의 메모리

이종 SoC에서 M 코어가 쓸 수 있는 메모리는 크게 세 종류다.

| 종류 | 어디 있나 | 크기 감 | 지연 | 누가 쓰나 | Linux 쪽 선언 |
|---|---|---|---|---|---|
| TCM (Tightly Coupled Memory) | M 코어 바로 옆, 캐시 없이 1사이클 | 수십~수백 KiB | 결정론적 | M 코어 코드·스택·ISR | remoteproc 노드의 `reg` (예: AM62 `iram`/`dram`, `atcm`/`btcm`) |
| OCRAM / 시스템 SRAM | 온칩 버스 위 | 수백 KiB~수 MiB | 낮음, 버스 경합 있음 | 공유 버퍼, 저전력 모드 데이터 | `mmio-sram` 노드 또는 `reserved-memory` |
| DDR 캐브아웃 | 외부 DRAM의 일부 | 수 MiB~수십 MiB | 높고 흔들림 | 큰 코드, vring, 메시지 버퍼, 카메라 프레임 | `reserved-memory` + `no-map` (+ `shared-dma-pool`) |

**캐브아웃(carveout)** 은 "큰 RAM에서 잘라 내 다른 주인에게 준 조각"이다. Linux 입장에서는 그 주소가 **애초에 없는 RAM**이어야 한다. 그래야 버디 할당자가 그 페이지를 다른 프로세스에 주지 않고, 커널이 그 영역에 캐시 가능한 선형 매핑을 만들지도 않는다. 이걸 DT로 말하는 방법이 `reserved-memory` 노드의 `no-map`이다. 속성 하나하나(`reg`/`size`/`no-map`/`reusable`/`shared-dma-pool`)는 F10 §4에서 자세히 다뤘으니, 여기서는 **AMP 관점의 배치 규칙**만 본다.

### 3.2 AMP 배치 규칙 다섯 개

1. **실시간 코드는 TCM에.** DDR 코드는 Linux의 GPU·카메라 DMA 트래픽 때문에 페치 지연이 흔들린다. "M 코어가 있으니 실시간"은 코드가 TCM에 있을 때만 성립한다.
2. **M 코어의 DDR 영역은 고정 주소(`reg`) + `no-map`.** 펌웨어 ELF가 링크 주소에 묶여 있으니 동적 배치(`size`)는 쓸 수 없다. `no-map`이 없으면 Linux 선형 매핑에 "캐시 가능" 속성으로 남아, 투기적 프리페치가 그 줄을 캐시에 올려 둘 수 있다(§5의 함정).
3. **메시지 버퍼는 `shared-dma-pool` + `no-map`.** 그래야 remoteproc/rpmsg가 `dma_alloc_coherent()`로 그 영역에서만 버퍼를 받는다(O2). 일반 CMA에서 받으면 M 코어가 모르는 주소가 된다.
4. **두 코어가 모두 아는 주소여야 한다.** §2.2처럼 별칭이 있으면 펌웨어 링커 스크립트와 DT가 같은 물리 영역을 가리키는지 표로 맞춘다.
5. **보안 하드웨어로 담장을 친다.** DT는 "약속"일 뿐이다. 버그 있는 Linux 드라이버의 DMA가 M 코어 영역을 덮어쓰는 걸 막는 건 §7의 RDC/TrustZone/IOMMU다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13" font-weight="bold">i.MX93 EVK 예 (DT 확인 주소) — 누가 소유하나</text>
  <rect x="20" y="40" width="120" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="80" y="58" text-anchor="middle" font-size="12">TCM code</text>
  <text x="80" y="74" text-anchor="middle" font-size="12">0x201C0000</text>
  <rect x="140" y="40" width="120" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="200" y="58" text-anchor="middle" font-size="12">TCM sys</text>
  <text x="200" y="74" text-anchor="middle" font-size="12">rsc-table 4K</text>
  <text x="300" y="66" font-size="12">M33 전용 (Linux는 로드할 때만 ioremap)</text>
  <rect x="20" y="110" width="250" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="145" y="135" text-anchor="middle" font-size="12">Linux System RAM (0x80000000~)</text>
  <rect x="270" y="110" width="60" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="300" y="135" text-anchor="middle" font-size="12">vring</text>
  <rect x="330" y="110" width="90" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="375" y="135" text-anchor="middle" font-size="12">vdevbuffer</text>
  <rect x="420" y="110" width="240" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="540" y="135" text-anchor="middle" font-size="12">Linux System RAM (CMA 포함)</text>
  <text x="270" y="172" font-size="12">0xa4000000</text>
  <text x="380" y="172" font-size="12">0xa4020000 (1 MiB)</text>
  <rect x="20" y="195" width="200" height="105" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="120" y="218" text-anchor="middle" font-size="13" font-weight="bold">vring (no-map)</text>
  <text x="30" y="242" font-size="12">virtqueue 링 32 KiB x4</text>
  <text x="30" y="264" font-size="12">양쪽이 인덱스 읽고 씀</text>
  <text x="30" y="286" font-size="12">Linux: 비캐시 매핑</text>
  <rect x="240" y="195" width="200" height="105" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="218" text-anchor="middle" font-size="13" font-weight="bold">vdevbuffer</text>
  <text x="250" y="242" font-size="12">shared-dma-pool + no-map</text>
  <text x="250" y="264" font-size="12">rpmsg 메시지 본문</text>
  <text x="250" y="286" font-size="12">dma_alloc_coherent 출처</text>
  <rect x="460" y="195" width="200" height="105" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="560" y="218" text-anchor="middle" font-size="13" font-weight="bold">System RAM</text>
  <text x="470" y="242" font-size="12">버디 · 페이지 캐시 · CMA</text>
  <text x="470" y="264" font-size="12">M 코어는 접근 금지</text>
  <text x="470" y="286" font-size="12">(RDC로 강제해야 안전)</text>
</svg>
```
그림 5 — i.MX93 EVK DT에서 읽은 소유권 지도(비율 무시). 빨강·초록은 DDR에서 잘라 낸 공유 영역, 주황은 M33 TCM, 파랑은 Linux 몫이다.

---

## 4. 실습 1 — 캐브아웃을 만들고 Linux가 손대지 않는 걸 증명하기

### 4.1 실험 설계

i.MX93 EVK를 흉내 내어 QEMU `virt`(RAM `0x40000000`부터 1 GiB)에 네 개의 캐브아웃을 만든다. 그리고 "증명"을 세 겹으로 한다.

1. **커널 장부**: `/proc/iomem`, `memblock`, `MemTotal`에서 그 영역이 System RAM이 아님을 본다.
2. **유저 공간 접근**: `STRICT_DEVMEM` 커널에서 `/dev/mem`이 System RAM은 거부하고 캐브아웃은 허용하는지 본다(= 커널이 "내 RAM이 아니다"라고 판단).
3. **덮어쓰기 시험**: 부팅 **전에** QEMU `-device loader`로 "M 코어 펌웨어"를 캐브아웃에 미리 써 둔다(부트로더가 M 코어 이미지를 올려 둔 상황). Linux 안에서 RAM을 거의 다 먹고 모든 바이트를 `0x5A`로 칠한 뒤, QEMU 모니터 `pmemsave`로 그 물리 주소를 밖에서 덤프해 원본과 비교한다. 대조군은 일반 RAM `0x60000000`에 같은 크기로 넣은 패턴이다.

```
 phys addr     size     node                         flags
 0x7e000000    2 MiB    o1-mcore-fw@7e000000         no-map          (M-core code/data in DDR)
 0x7e200000   32 KiB    o1-vdev0vring0@7e200000      no-map
 0x7e208000   32 KiB    o1-vdev0vring1@7e208000      no-map
 0x7e400000    1 MiB    o1-vdevbuffer@7e400000       shared-dma-pool, no-map
 0x60000000    2 MiB    (control: ordinary RAM, pattern only)
```

### 4.2 예제 6 — DT 조각과 DTB 만들기

무엇을 확인하나: 부팅에 쓸 `-m`/`-smp`/`-cpu`와 **같은 인자로** `virt` DT를 덤프하고, 끝에 `reserved-memory`를 덧붙인다.

```dts
/* o1_rsv.dtsi — AMP-style carveouts, modelled on imx93-11x11-evk.dts (RAM here: 0x40000000 + 1 GiB) */
/ {
	reserved-memory {
		#address-cells = <2>;
		#size-cells = <2>;
		ranges;

		o1_mfw: o1-mcore-fw@7e000000 {          /* M-core code/data in DDR */
			reg = <0x0 0x7e000000 0x0 0x200000>;  /* 2 MiB */
			no-map;
		};
		o1_vring0: o1-vdev0vring0@7e200000 {
			reg = <0x0 0x7e200000 0x0 0x8000>;
			no-map;
		};
		o1_vring1: o1-vdev0vring1@7e208000 {
			reg = <0x0 0x7e208000 0x0 0x8000>;
			no-map;
		};
		o1_vdevbuf: o1-vdevbuffer@7e400000 {    /* rpmsg payload buffers (O2) */
			compatible = "shared-dma-pool";
			reg = <0x0 0x7e400000 0x0 0x100000>;
			no-map;
		};
	};
};
```

```sh
#!/bin/bash
# o1_mkdtb.sh — dump virt DT with the SAME -m/-smp/-cpu we boot with, then append o1_rsv.dtsi
set -e; cd /var/tmp/O1; S=/Users/donh/.elinux_scratch/O1
qemu-system-aarch64 -machine virt,dumpdtb=o1_base.dtb -cpu cortex-a57 -smp 2 -m 1024M -nographic
dtc -q -I dtb -O dts -o o1_base.dts o1_base.dtb
cat o1_base.dts $S/o1_rsv.dtsi > o1_rsv.dts
dtc -q -I dts -O dtb -o o1_rsv.dtb o1_rsv.dts
fdtget -l o1_rsv.dtb /reserved-memory 2>/dev/null || dtc -I dtb -O dts o1_rsv.dtb | grep -A3 "o1-"
```

```text
o1-mcore-fw@7e000000
o1-vdev0vring0@7e200000
o1-vdev0vring1@7e208000
o1-vdevbuffer@7e400000
```

출력에서 볼 것: dts 문법은 `/ { }` 블록이 여러 번 나와도 합쳐 준다(D2). 그래서 덤프한 dts 뒤에 조각을 이어 붙이기만 하면 된다. 대조 실험용 "캐브아웃 없는" DTB는 `dtc -I dts -O dtb o1_base.dts`로 다시 컴파일한 `o1_norsv.dtb`를 쓴다.

**랩 함정 (직접 겪음)**: `dumpdtb`가 만든 원본 `o1_base.dtb`(QEMU가 1 MiB로 패딩한 파일)를 그대로 `-dtb`로 주고 `Image-dom`을 부팅하면, `earlycon`을 켜도 시리얼에 **한 글자도 안 나오고 멈췄다**(150초 timeout 두 번). dtc로 한 번 왕복한 DTB는 정상 부팅했다. 원인은 끝까지 추적하지 않았다. 덤프한 DTB는 dtc로 다시 컴파일해서 쓰자.

### 4.3 예제 7 — 게스트에서 본 커널 장부

무엇을 확인하나: 커널이 캐브아웃을 "자기 RAM이 아닌 것"으로 기록했는지. 게스트 스크립트와 호스트 스크립트는 다음과 같다(호스트 스크립트는 예제 8에서 함께 본다).

```sh
#!/bin/sh
# o1_lab1_guest.sh — runs inside the guest (BusyBox initramfs)
echo "=== kernel $(uname -r)"
echo "=== /proc/iomem (RAM part)"; grep -E "System RAM|reserved|Kernel" /proc/iomem
echo "=== DT /reserved-memory"; ls -1 /sys/firmware/devicetree/base/reserved-memory | grep o1
echo "=== dmesg"; dmesg | grep -iE "reserved mem|OF: reserved"
echo "=== memblock memory (NOMAP flags)"; cat /sys/kernel/debug/memblock/memory
echo "=== MemTotal"; grep MemTotal /proc/meminfo
echo "=== devmem (explicit mapping of no-map carveout)"
devmem 0x7e000000 32; devmem 0x7e000004 32
/o1_hog 24
```

캐브아웃 있는 DTB(`o1_rsv.dtb`)로 부팅한 출력이다(`dmesg`의 `[T0]` 같은 호출자 표시는 이 커널의 `PRINTK_CALLER` 설정 때문).

```text
=== kernel 6.18.55-dom
=== /proc/iomem (RAM part)
40000000-7dffffff : System RAM
  40200000-42e2ffff : Kernel code
  42e30000-4325ffff : reserved
  43260000-43a6ffff : Kernel data
  48000000-482f3fff : reserved
  48400000-48408fff : reserved
  7c000000-7dffffff : reserved
7e000000-7e20ffff : reserved
7e210000-7e3fffff : System RAM
7e400000-7e4fffff : reserved
7e500000-7fffffff : System RAM
  7eb00000-7fbfffff : reserved
  ...
=== DT /reserved-memory
o1-mcore-fw@7e000000
o1-vdev0vring0@7e200000
o1-vdev0vring1@7e208000
o1-vdevbuffer@7e400000
=== dmesg
[    0.000000][    T0] OF: reserved mem: 0x000000007e000000..0x000000007e1fffff (2048 KiB) nomap non-reusable o1-mcore-fw@7e000000
[    0.000000][    T0] OF: reserved mem: 0x000000007e200000..0x000000007e207fff (32 KiB) nomap non-reusable o1-vdev0vring0@7e200000
[    0.000000][    T0] OF: reserved mem: 0x000000007e208000..0x000000007e20ffff (32 KiB) nomap non-reusable o1-vdev0vring1@7e208000
[    0.000000][    T0] Reserved memory: created DMA memory pool at 0x000000007e400000, size 1 MiB
[    0.000000][    T0] OF: reserved mem: initialized node o1-vdevbuffer@7e400000, compatible id shared-dma-pool
[    0.000000][    T0] OF: reserved mem: 0x000000007e400000..0x000000007e4fffff (1024 KiB) nomap non-reusable o1-vdevbuffer@7e400000
=== memblock memory (NOMAP flags)
   0: 0x0000000040000000..0x000000007dffffff    0 NONE
   1: 0x000000007e000000..0x000000007e20ffff    0 NOMAP
   2: 0x000000007e210000..0x000000007e3fffff    0 NONE
   3: 0x000000007e400000..0x000000007e4fffff    0 NOMAP
   4: 0x000000007e500000..0x000000007fffffff    0 NONE
=== MemTotal
MemTotal:         970548 kB
=== devmem (explicit mapping of no-map carveout)
0x464D314F
0x00000057
```

(`/proc/iomem`의 맨 아래 작은 `reserved` 줄 여덟 개는 `...`로 생략했다.)

출력에서 볼 것:

- `/proc/iomem`에서 `7e000000-7e20ffff`와 `7e400000-7e4fffff`가 **System RAM 줄 밖으로** 빠져 최상위 `reserved`가 됐다. 들여쓰기된 `reserved`(System RAM 안)는 "RAM이긴 한데 지금 커널이 쓰는 중"이고, 최상위 `reserved`는 "커널 RAM이 아님"이다. 붙어 있는 fw(2 MiB)와 vring 두 개(32 KiB x2)는 한 줄로 합쳐 보인다.
- `memblock`에 `NOMAP` 플래그가 붙었다. 이게 "선형 매핑을 만들지 마"의 실체다(§12).
- `shared-dma-pool`은 "DMA memory pool"로 따로 초기화됐다. 이 풀을 `memory-region`으로 가리키는 장치만 여기서 `dma_alloc_coherent()`를 받는다.
- `devmem`(BusyBox, `/dev/mem` + `mmap`)이 캐브아웃을 읽었고, 값 `0x464D314F` = 바이트 `4F 31 4D 46` = `"O1MF"`, 다음 워드 `0x57` = `"W"`다. 부팅 전에 QEMU loader가 써 둔 "펌웨어" 머리말이 그대로 있다. Linux가 이 영역을 보려면 **명시적으로 매핑해야만** 한다는 뜻이다. remoteproc 드라이버가 하는 일이 바로 이것이다(`imx_rproc`는 `ioremap_wc`).
- `MemTotal`은 캐브아웃 없는 부팅(973688 kB, 예제 9)보다 **3140 kB 적다**. 캐브아웃 합 2048 + 32 + 32 + 1024 = 3136 kB와 4 kB 차이(페이지 경계 반올림 수준)다.

### 4.4 예제 8 — 덮어쓰기 시험: 메모리를 다 먹어도 펌웨어는 그대로인가

무엇을 확인하나: Linux가 RAM을 거의 전부 써도 캐브아웃의 바이트는 바뀌지 않는다는 것을 **게스트 밖에서** 확인한다.

```c
/* o1_hog.c — eat (MemAvailable - margin) of RAM and touch every byte, then wait.
 * If the kernel could hand out carveout pages, the "firmware" pattern there would be overwritten. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static long meminfo_kb(const char *key)
{
	char line[128]; long v = -1; FILE *f = fopen("/proc/meminfo", "r");
	if (!f) { perror("meminfo"); exit(1); }
	while (fgets(line, sizeof line, f))
		if (!strncmp(line, key, strlen(key))) { sscanf(line + strlen(key), " %ld", &v); break; }
	fclose(f);
	return v;
}
int main(int argc, char **argv)
{
	long margin_mb = argc > 1 ? atol(argv[1]) : 24;
	long avail = meminfo_kb("MemAvailable:");
	size_t sz = (size_t)(avail / 1024 - margin_mb) << 20;
	printf("MemTotal=%ld kB MemAvailable=%ld kB -> hog %zu MiB\n", meminfo_kb("MemTotal:"), avail, sz >> 20);
	char *p = malloc(sz);
	if (!p) { perror("malloc"); exit(1); }
	memset(p, 0x5A, sz);                         /* fault in + dirty every page */
	__asm__ volatile("" :: "r"(p) : "memory");  /* keep the memset (no dead-store elim.) */
	printf("after hog: MemFree=%ld kB\nO1_HOG_DONE\n", meminfo_kb("MemFree:"));
	fflush(stdout);
	sleep(600);                                   /* host dumps guest RAM now */
	return 0;
}
```

```sh
#!/bin/bash
# o1_lab1_host.sh <Image> <dtb> — preload "M-core firmware" into the carveout, boot, hog RAM, dump & compare
set -e; cd /var/tmp/O1; K=$1; DTB=$2
python3 -c "import os,random; random.seed(1); open('fw.bin','wb').write(b'O1MFW\x00\x00\x00'+random.randbytes(2*1024*1024-8)); open('ctl.bin','wb').write(random.randbytes(2*1024*1024))"
rm -f mon.sock lab1.log
timeout 900 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1024M -nographic -kernel $K -dtb $DTB \
  -initrd o1_init.gz -device loader,file=fw.bin,addr=0x7e000000,force-raw=on \
  -device loader,file=ctl.bin,addr=0x60000000,force-raw=on \
  -monitor unix:mon.sock,server,nowait -serial file:lab1.log \
  -append "console=ttyAMA0 LAB_CMD=${G:-/o1_lab1_guest.sh}" & QPID=$!
for i in $(seq 1 600); do grep -q O1_HOG_DONE lab1.log 2>/dev/null && break; sleep 1; done
mon() { (echo "$1"; sleep 2) | socat - UNIX-CONNECT:mon.sock >/dev/null; }
mon "pmemsave 0x7e000000 0x200000 \"/var/tmp/O1/fw_after.bin\""
mon "pmemsave 0x60000000 0x200000 \"/var/tmp/O1/ctl_after.bin\""
sleep 1; mon quit; wait $QPID || true
tr -d '\r' < lab1.log | sed -n '/=== kernel/,/O1_HOG_DONE/p'
echo "=== host compare"
cmp -s fw.bin  fw_after.bin  && echo "carveout 0x7e000000: IDENTICAL (Linux never touched it)" || echo "carveout: CHANGED"
cmp -s ctl.bin ctl_after.bin && echo "control  0x60000000: IDENTICAL" || echo "control  0x60000000: CHANGED, $(cmp -l ctl.bin ctl_after.bin | wc -l) bytes differ; sample: $(od -A n -t x1 -N 8 ctl_after.bin)"
```

```text
MemTotal=970548 kB MemAvailable=883784 kB -> hog 839 MiB
after hog: MemFree=59700 kB
O1_HOG_DONE
=== host compare
carveout 0x7e000000: IDENTICAL (Linux never touched it)
control  0x60000000: CHANGED, 2089006 bytes differ; sample:  5a 5a 5a 5a 5a 5a 5a 5a
```

출력에서 볼 것: 839 MiB를 칠하자 대조군 2 MiB 중 2,089,006바이트(99.6%)가 `5a`로 바뀌었다(나머지는 원래 랜덤 값이 우연히 `0x5A`였던 바이트). 캐브아웃은 바이트 하나 안 바뀌었다.

두 번 당한 함정이 있다. 첫째, `memset` 결과를 아무도 읽지 않으니 GCC `-O2`가 `malloc`+`memset`을 통째로 지워 버려 `MemFree`가 그대로였다(dead-store elimination). 빈 `asm volatile`로 포인터를 "사용"해 막았다. 둘째, QEMU 모니터의 `pmemsave`는 파일 이름을 **따옴표로** 감싸야 한다. 안 그러면 `invalid char 'v' in expression`이 나며 아무 파일도 안 생긴다.

### 4.5 예제 9 — 대조 실험: 캐브아웃이 없으면 무슨 일이

무엇을 확인하나: 같은 "펌웨어"를 같은 주소에 미리 써 두고 `reserved-memory` 없는 DTB로 부팅한다.

```text
=== /proc/iomem (RAM part)
40000000-7fffffff : System RAM
  40200000-42e2ffff : Kernel code
  42e30000-4325ffff : reserved
  43260000-43a6ffff : Kernel data
  48000000-482f3fff : reserved
  48400000-48408fff : reserved
  7cb00000-7fbfffff : reserved
  ...
=== memblock memory (NOMAP flags)
   0: 0x0000000040000000..0x000000007fffffff    0 NONE
=== MemTotal
MemTotal:         973688 kB
=== devmem (explicit mapping of no-map carveout)
devmem: mmap: Operation not permitted
devmem: mmap: Operation not permitted
...
carveout 0x7e000000: IDENTICAL (Linux never touched it)
control  0x60000000: CHANGED, 2089006 bytes differ; sample:  5a 5a 5a 5a 5a 5a 5a 5a
```

그리고 두 부팅의 CMA 위치(`dmesg`의 `cma:` 줄, 따로 부팅해서 확인):

```text
reserved-memory 없음:  cma: Reserved 32 MiB at 0x000000007cc00000
o1_rsv.dtb:           cma: Reserved 32 MiB at 0x000000007c000000
```

출력에서 볼 것 — 이 대조군이 이 절의 진짜 교훈이다.

- `0x7e000000`은 이제 System RAM이고 `NOMAP`도 없다. `STRICT_DEVMEM` 때문에 `/dev/mem`도 `EPERM`을 낸다. 커널이 "내 RAM"이라고 주장하는 상태다.
- 이번 hog 실험에서는 우연히 그 2 MiB가 살아남았다. 이유는 CMA다: 기본 CMA 32 MiB가 `0x7cc00000~0x7ebfffff`에 잡혔고 `0x7e000000`은 **그 안**이다. CMA 페이지는 movable 할당에만 빌려 주므로 이번 hog가 거기까지 오지 않았을 뿐이다. 카메라 드라이버가 `dma_alloc_coherent()`로 큰 버퍼를 받는 순간(F10) 그 자리, 즉 M 코어 펌웨어 위에 프레임이 써진다.
- 캐브아웃을 선언하면 커널이 CMA를 그 아래 `0x7c000000`으로 **피해서** 잡는다.
- 정리하면 "패턴이 안 바뀌었다"만으로는 증명이 안 된다(대조군에서도 안 바뀌었다). 증명은 커널 장부(System RAM 밖, `NOMAP`, `MemTotal` 감소)와 덮어쓰기 시험을 **같이** 봐야 한다.

---

## 5. 캐시 코히어런시 — 두 코어가 같은 바이트를 다르게 볼 때

### 5.1 직관

Cortex-A 클러스터 안의 코어끼리는 하드웨어가 캐시를 맞춰 준다(SMP 코히어런시). 하지만 M 코어는 대개 그 코히어런시 도메인 **밖**에 있다. A 코어가 공유 버퍼에 쓴 값이 아직 A 코어의 L1/L2 캐시에만 있고 DDR에는 안 내려갔다면, M 코어는 DDR에서 옛 값을 읽는다. 반대로 M7처럼 D-cache가 있는 M 코어가 쓴 값은 M7 캐시에 머문다. Don이 SSD에서 DMA 디스크립터를 쓸 때 "clean 하고 doorbell", 완료 후 "invalidate 하고 읽기"를 하던 것과 똑같은 규칙이 코어 사이에 적용된다(F9, G5의 DMA 매핑 규칙도 같은 이야기).

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="40" y="16" width="150" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="37" text-anchor="middle" font-size="13">A 코어 (Linux)</text>
  <rect x="265" y="16" width="150" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="37" text-anchor="middle" font-size="13">공유 DDR / SRAM</text>
  <rect x="490" y="16" width="150" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="37" text-anchor="middle" font-size="13">M 코어 (D-cache)</text>
  <line x1="115" y1="50" x2="115" y2="345" stroke="currentColor" stroke-dasharray="3,3"/>
  <line x1="340" y1="50" x2="340" y2="345" stroke="currentColor" stroke-dasharray="3,3"/>
  <line x1="565" y1="50" x2="565" y2="345" stroke="currentColor" stroke-dasharray="3,3"/>
  <text x="125" y="78" font-size="12">1. 버퍼에 쓰기 (캐시에만)</text>
  <line x1="115" y1="100" x2="336" y2="100" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="125" y="94" font-size="12">2. clean (DC CVAC) → DDR</text>
  <text x="125" y="124" font-size="12">3. DMB, 그다음 인덱스 갱신</text>
  <line x1="115" y1="146" x2="561" y2="146" stroke="#d0564a" marker-end="url(#ah4)"/>
  <text x="250" y="140" font-size="12">4. 메일박스 doorbell → M 코어 IRQ</text>
  <line x1="561" y1="176" x2="344" y2="176" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="372" y="170" font-size="12">5. invalidate 후 읽기</text>
  <text x="372" y="204" font-size="12">6. 응답 쓰기 + clean</text>
  <line x1="565" y1="214" x2="344" y2="214" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="561" y1="246" x2="119" y2="246" stroke="#d0564a" marker-end="url(#ah4)"/>
  <text x="250" y="240" font-size="12">7. doorbell → A 코어 IRQ</text>
  <line x1="119" y1="276" x2="336" y2="276" stroke="currentColor" marker-start="url(#ah4)"/>
  <text x="125" y="296" font-size="12">8. invalidate 후 응답 읽기</text>
  <text x="340" y="330" text-anchor="middle" font-size="12">비캐시(Normal-NC)로 매핑하면 2·5·6·8 대신 barrier만 필요</text>
</svg>
```
그림 6 — 코히어런트하지 않은 두 코어 사이의 메시지 한 번. clean/invalidate를 하나라도 빼먹으면 "가끔 옛날 값"이 나오는 버그가 된다.

### 5.2 실무의 세 가지 선택

| 방법 | Linux 쪽 | M 코어 쪽 | 장단점 |
|---|---|---|---|
| 비캐시 매핑 | `no-map` + `ioremap_wc`/`memremap(MEMREMAP_WC)` (= Normal Non-cacheable) | MPU로 그 영역 non-cacheable | 가장 단순. 대신 큰 버퍼를 CPU로 처리하면 느리다. rpmsg vring의 기본 방식 |
| 캐시 + 명시적 관리 | `dma_sync_single_for_device/cpu()`, 또는 streaming DMA API | `SCB_CleanDCache_by_Addr` 류(CMSIS) | 큰 프레임에 유리. 캐시 라인(64 B) 정렬과 크기를 맞춰야 한다 |
| 하드웨어 코히어런시 | DT `dma-coherent` | 인터커넥트가 snoop | 일부 고급 SoC만. 있는지 데이터시트로 확인 |

`imx_rproc`가 캐브아웃을 `ioremap_wc`로 매핑하는 건 소스에서 확인할 수 있다(`drivers/remoteproc/imx_rproc.c` 467·659·701행). 즉 첫 번째 방식이다.

**흔한 함정 — `no-map`을 빼먹은 경우**: Linux 선형 매핑에 그 영역이 **캐시 가능**으로 남아 있으면, 아무 코드도 그 주소를 안 읽어도 CPU의 투기적 프리페치가 그 줄을 캐시에 올릴 수 있다. 그 상태에서 다른 매핑(비캐시)으로 접근하면 같은 물리 주소에 속성이 다른 두 매핑이 생기는 "mismatched attributes"가 되고, Arm 아키텍처에서 결과를 보장하지 않는다. 증상은 "M 코어가 쓴 값이 Linux에서 가끔 옛 값으로 보인다"로 나타나고 재현이 아주 어렵다. `no-map`은 이걸 원천 차단한다.

### 5.3 QEMU에서는 왜 재현이 안 되나

QEMU TCG는 캐시를 모델링하지 않는다. 그래서 이 노트의 실습은 clean/invalidate가 없어도 맞게 동작한다. **실습 결과를 근거로 "캐시 관리 필요 없다"고 결론 내리면 안 된다.** 대신 TCG도 메모리 **순서**는 진짜 문제가 된다. 두 QEMU 인스턴스가 서로 다른 호스트 스레드로 돌고, 호스트 M2는 약한 메모리 순서(weak ordering)의 Arm이라, 게스트 코드의 `dmb`가 호스트 barrier로 번역된다. 그래서 §10의 링 코드는 barrier를 정확히 넣었다.

---

## 6. 메일박스 — 인터럽트 한 줄의 가치

메일박스 하드웨어(i.MX MU, STM32 IPCC, TI Mailbox, Tegra HSP, Arm MHU)와 Linux mailbox 프레임워크(`mbox_controller` / `mbox_client`, `mbox_send_message()`, `rx_callback`)는 H10에서 가짜 컨트롤러 드라이버까지 짜 가며 다뤘다. 여기서는 **AMP 구조 안에서의 역할**만 정리한다.

- 메일박스는 **신호**, 공유 메모리는 **본문**이다. MU의 데이터 레지스터 4개(기억 기반)로는 센서 프레임 하나도 못 담는다. 그래서 거의 모든 프로토콜(rpmsg, BPMP, SCMI)이 "본문은 공유 메모리, 메일박스엔 '봐라' 한 마디(+ 인덱스)" 구조다. 그림 4의 BPMP가 그 예다.
- doorbell이 인터럽트라는 것이 핵심이다. 상대 코어는 자고 있다가(`WFI`, Linux는 스레드 sleep) 깨어난다. 공유 메모리만 있으면 상대는 **폴링**해야 하고, 그 비용은 §11에서 직접 잰다.
- 메일박스는 **전원 관리 이벤트**도 나른다. A 코어 suspend 중에 M 코어가 MU에 쓰면 그게 웨이크업 소스가 된다(STM32 IPCC 노드에 `wakeup-source;`가 있는 것을 §2.4 dtsi에서 볼 수 있다 — 실제로 `stm32mp151.dtsi` 136행).

| 프로토콜 | 신호 | 본문 | 쓰는 곳 |
|---|---|---|---|
| rpmsg / virtio | 메일박스 doorbell (vring 번호) | DDR 캐브아웃의 vring + 버퍼 | i.MX, STM32MP, TI (O2) |
| BPMP IPC | HSP doorbell | SYSRAM tx/rx 창 | Tegra |
| SCMI | MHU 등 doorbell | SRAM `shmem` | Arm 기반 전원·클럭 펌웨어 |
| TI-SCI | Secure Proxy (메시지 큐 하드웨어, 기억 기반) | 프록시 버퍼 | K3의 DMSC/TIFS |

---

## 7. 자원 분할 — 클럭·전원·핀·보안을 누가 쥐나

### 7.1 왜 문제인가

같은 칩 안의 두 OS가 같은 레지스터 블록을 각자 "내 것"이라고 생각하면 사고가 난다. 예를 들어 센서 키트에서 M 코어가 CAN 컨트롤러를 쓰는데, Linux DT에도 그 CAN 노드가 `okay`로 켜져 있으면 Linux 드라이버가 probe하면서 컨트롤러를 리셋한다. 더 미묘한 건 클럭이다: Linux의 `clk` 프레임워크는 부팅 끝에 **아무 소비자도 없는 클럭을 꺼 버린다**(`clk_disable_unused`, late initcall). M 코어만 쓰는 UART 클럭이 Linux DT에 소비자로 안 적혀 있으면 부팅 5초쯤에 M 코어 UART가 갑자기 죽는다. 이건 실제 bring-up에서 아주 흔한 증상이다.

### 7.2 분할의 세 층

| 층 | 무엇을 나누나 | 수단 | 예 |
|---|---|---|---|
| 약속 (소프트웨어) | 어느 OS가 어느 장치를 드라이버로 쓰나 | Linux DT `status = "disabled"`, M 코어 펌웨어 설정 | M 코어 몫 CAN·UART·GPIO는 Linux DT에서 끔 |
| 공유 자원 중재 | 클럭·전원·핀·리셋처럼 한 레지스터에 여럿이 걸린 것 | 시스템 관리 코어에 요청 (TI-SCI, BPMP, SCMI), 또는 hwspinlock (H10) | AM62의 `ti,sci` 리셋, Tegra의 BPMP 클럭 |
| 강제 (하드웨어) | 버스 마스터별 주변장치·메모리 접근 권한 | NXP RDC/TRDC, ST ETZPC(MP1)/RIFSC(MP2), TrustZone TZC/TZASC, SMMU/IOMMU | M 코어 영역에 Linux DMA 접근 차단 |

ST는 커널 DT에서 이 "강제" 층이 직접 보인다. `stm32mp151.dtsi`에 `etzpc: bus@5c007000`(`st,stm32-etzpc`)가 있고, 그 아래 장치들이 `access-controllers = <&etzpc 16>`처럼 자기 방화벽 슬롯을 가리킨다. MP25는 `rifsc: bus@42080000`(`st,stm32mp25-rifsc`)로 같은 일을 한다. 둘 다 이 노트 §2 grep에서 확인했다. NXP의 RDC/TRDC 설정은 보통 TF-A나 부트로더가 한다(기억 기반 — BSP 문서로 확인).

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="160" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="100" y="50" text-anchor="middle" font-size="13">A 코어 (Linux)</text>
  <rect x="20" y="90" width="160" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="100" y="120" text-anchor="middle" font-size="13">GPU / 카메라 DMA</text>
  <rect x="20" y="160" width="160" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="100" y="190" text-anchor="middle" font-size="13">M 코어 (RTOS)</text>
  <rect x="250" y="20" width="150" height="190" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="325" y="48" text-anchor="middle" font-size="13" font-weight="bold">방화벽</text>
  <text x="325" y="76" text-anchor="middle" font-size="12">RDC / TRDC</text>
  <text x="325" y="98" text-anchor="middle" font-size="12">ETZPC / RIFSC</text>
  <text x="325" y="120" text-anchor="middle" font-size="12">TZASC (DDR)</text>
  <text x="325" y="142" text-anchor="middle" font-size="12">SMMU (DMA)</text>
  <text x="325" y="180" text-anchor="middle" font-size="12">설정: TF-A / 부트로더</text>
  <rect x="470" y="20" width="190" height="50" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="50" text-anchor="middle" font-size="12">Linux 몫: eMMC, Ethernet</text>
  <rect x="470" y="90" width="190" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="120" text-anchor="middle" font-size="12">M 몫: CAN, PWM, TCM</text>
  <rect x="470" y="160" width="190" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="565" y="190" text-anchor="middle" font-size="12">공유: 클럭 · 전원 · 핀</text>
  <line x1="182" y1="45" x2="246" y2="45" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="182" y1="115" x2="246" y2="115" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="182" y1="185" x2="246" y2="185" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="402" y1="45" x2="466" y2="45" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="402" y1="115" x2="466" y2="115" stroke="#d0564a" stroke-dasharray="4,3"/>
  <text x="434" y="108" text-anchor="middle" font-size="12">차단</text>
  <line x1="402" y1="185" x2="466" y2="185" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="340" y="250" text-anchor="middle" font-size="12">공유 자원은 직접 쓰지 말고 관리 코어(TI-SCI, BPMP, SCMI)에 요청하거나 hwspinlock으로 잠근다</text>
</svg>
```
그림 7 — 버스 마스터(왼쪽)와 주변장치·메모리(오른쪽) 사이의 하드웨어 방화벽. DT의 `status`는 약속, 방화벽은 강제다.

### 7.3 클럭 함정 막기

M 코어 전용 클럭을 Linux가 끄지 않게 하는 방법은 SoC마다 다르다. 공통적으로 쓰는 것은 다음과 같다.

1. 그 클럭을 remoteproc 노드의 `clocks`에 적는다(i.MX93 `cm33` 노드의 `clocks = <&clk IMX93_CLK_CM33_GATE>` — §2.1). remoteproc 드라이버가 소비자로 클럭을 켜 두니 `clk_disable_unused`가 건드리지 않는다.
2. 클럭 드라이버 쪽에서 "critical" 클럭으로 표시한다(`CLK_IS_CRITICAL`, 벤더 드라이버 패치).
3. 디버그 중에는 커널 인자 `clk_ignore_unused`로 원인을 좁힌다. 이 인자로 증상이 사라지면 범인은 거의 확실히 클럭이다. 단 제품에 남기지 않는다(전력 낭비).

---

## 8. 누가 먼저 부팅하나 — "Linux가 띄운다" vs "부트로더가 띄우고 Linux는 붙는다"

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13" font-weight="bold">(a) Linux remoteproc이 시작</text>
  <rect x="20" y="34" width="60" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="50" y="53" text-anchor="middle" font-size="12">ROM</text>
  <rect x="80" y="34" width="80" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="53" text-anchor="middle" font-size="12">TF-A</text>
  <rect x="160" y="34" width="100" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="210" y="53" text-anchor="middle" font-size="12">U-Boot</text>
  <rect x="260" y="34" width="250" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="385" y="53" text-anchor="middle" font-size="12">Linux 커널 + 유저 공간</text>
  <rect x="510" y="74" width="150" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="585" y="93" text-anchor="middle" font-size="12">M 코어 실행</text>
  <text x="510" y="122" font-size="12">echo start → state</text>
  <text x="20" y="160" font-size="13" font-weight="bold">(b) 부트로더가 시작, Linux는 attach</text>
  <rect x="20" y="172" width="60" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="50" y="191" text-anchor="middle" font-size="12">ROM</text>
  <rect x="80" y="172" width="80" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="191" text-anchor="middle" font-size="12">TF-A</text>
  <rect x="160" y="172" width="100" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="210" y="191" text-anchor="middle" font-size="12">U-Boot</text>
  <rect x="260" y="172" width="250" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="385" y="191" text-anchor="middle" font-size="12">Linux 커널 + 유저 공간</text>
  <rect x="180" y="212" width="480" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="420" y="231" text-anchor="middle" font-size="12">M 코어 실행 (CAN 응답 · 카메라 미리보기가 일찍 시작)</text>
  <text x="180" y="260" font-size="12">bootaux 류 명령</text>
  <text x="660" y="260" text-anchor="end" font-size="12">remoteproc: detached → attached</text>
</svg>
```
그림 8 — 두 가지 시작 순서(시간축 비율 무시). (b)에서는 M 코어가 Linux 부팅 내내 이미 일하고 있다.

### 8.1 (a) Linux가 시작

remoteproc 드라이버가 `/lib/firmware`의 ELF를 TCM/캐브아웃에 쓰고, 리셋을 풀어 M 코어를 시작한다. 장점은 **펌웨어 업데이트가 rootfs OTA와 한 번에** 된다는 것(L 모듈), 단점은 M 코어가 Linux 부팅이 끝나야 일한다는 것. 사용자 공간 인터페이스(`/sys/class/remoteproc/remoteprocN/{firmware,state}`)와 로딩 과정은 O2에서 실행해 본다.

### 8.2 (b) 부트로더가 시작, Linux는 attach

U-Boot가 M 코어 이미지를 올리고 시작시킨다(NXP U-Boot의 `bootaux` 명령 — 기억 기반, 사용법은 BSP 문서로 확인). Linux의 remoteproc 드라이버는 probe 시 "이미 돌고 있다"를 감지하면 상태를 `RPROC_DETACHED`로 두고, 다시 로드하지 않고 **붙기만** 한다. 6.18 소스에서 확인되는 감지 경로:

```sh
grep -n "RPROC_DETACHED\|detect_mode(struct\|static int imx_rproc_attach" /var/lab/src/linux-6.18.55/drivers/remoteproc/imx_rproc.c | head -8
grep -n "M4_STATE_" /var/lab/src/linux-6.18.55/drivers/remoteproc/stm32_rproc.c | head -6
```

```text
573:static int imx_rproc_attach(struct rproc *rproc)
864:		priv->rproc->state = RPROC_DETACHED;
869:static int imx_rproc_arm_smc_detect_mode(struct rproc *rproc)
876:		priv->rproc->state = RPROC_DETACHED;
881:static int imx_rproc_mmio_detect_mode(struct rproc *rproc)
928:		priv->rproc->state = RPROC_DETACHED;
933:static int imx_rproc_scu_api_detect_mode(struct rproc *rproc)
965:	priv->rproc->state = RPROC_DETACHED;
45:#define M4_STATE_OFF		0
46:#define M4_STATE_INI		1
47:#define M4_STATE_CRUN		2
48:#define M4_STATE_CSTOP		3
49:#define M4_STATE_STANDBY	4
50:#define M4_STATE_CRASH		5
```

출력에서 볼 것: i.MX는 감지 방법이 세 가지(SMC로 TF-A에 묻기, MMIO로 리셋 레지스터 읽기, SCU 펌웨어 API)다. STM32MP1은 M4 펌웨어가 TAMP 백업 레지스터(`st,syscfg-m4-state`, §2.4 dtsi)에 자기 상태(`CRUN` = 돌고 있음)를 써 두고 Linux가 그걸 읽는다. 어느 쪽이든 "Linux가 아닌 누군가가 먼저 띄웠다"는 사실을 **하드웨어 상태로 남겨야** attach가 된다.

| 비교 | (a) Linux가 시작 | (b) 부트로더가 시작 + attach |
|---|---|---|
| M 코어가 일하기 시작하는 시점 | Linux 유저 공간 이후 (수 초) | 부트로더 단계 (수백 ms, 보드마다 다름) |
| 펌웨어 업데이트 | rootfs와 함께 (`/lib/firmware`) | 부트 파티션/부트로더 경로도 갱신해야 함 |
| Linux 재부팅 시 M 코어 | 같이 멈췄다 다시 시작 | 설계에 따라 계속 돌 수 있음 (detach) |
| 쓰는 곳 | 개발 초기, 데모, 비실시간 보조 작업 | 차량 early CAN·카메라, 모터 안전 정지 |

---

## 9. 장애 격리 — 한쪽이 죽으면

| 무엇이 죽나 | 다른 쪽에 미치는 영향 | 감지 | 복구 |
|---|---|---|---|
| M 코어 펌웨어 crash/hang | Linux는 계속 돈다. 그 기능(모터·CAN)이 멈춤 | M 코어 워치독 IRQ → remoteproc `rproc_report_crash()`; 또는 heartbeat 카운터 정지 | remoteproc recovery(재로드·재시작), 실패 시 안전 상태 |
| Linux 패닉 | 같은 리셋 도메인이면 M 코어도 리셋됨. 분리돼 있으면 M 코어는 계속 | M 코어가 Linux heartbeat 끊김 감지 | M 코어가 안전 정지 → Linux 재부팅 동안 유지 |
| 공유 자원 장애 (DDR, 클럭, 전원 레일) | **둘 다** 영향 | 보통 둘 다 늦게 앎 | 설계 단계에서 TCM·별도 클럭·별도 레일로 의존을 없앰 |
| 버그 있는 DMA가 남의 메모리를 씀 | 상대 쪽 데이터 손상 (조용히) | 거의 못 함 | 방화벽(RDC/TZASC/SMMU)으로 예방 |

워치독 → crash 보고 경로는 소스에서 확인된다: `stm32_rproc.c`의 `stm32_rproc_wdg()` IRQ 핸들러가 `rproc_report_crash(rproc, RPROC_WATCHDOG)`를 부르고(280·285행), `imx_rproc.c`도 828행에서 같은 함수를 부른다. remoteproc 코어의 recovery 동작(`/sys/class/remoteproc/.../recovery`)은 O2에서 다룬다. 이 노트의 실습 2에서는 "M 코어 프로세스를 죽였을 때 Linux 쪽이 heartbeat로 감지하고 살아남는지"를 본다(§11.5).

**설계 문장 하나**: "같은 칩"은 "같은 운명"을 뜻할 수 있다. 안전 기능을 M 코어에 맡겼다면 면접에서 반드시 "그 코어의 코드는 TCM에, 클럭·리셋·전원은 Linux와 분리, 워치독은 독립"까지 말해야 한다. 그게 안 되면 외부 MCU(O3)를 쓰는 이유가 된다.

---

## 10. 실습 2 준비 — QEMU 두 대로 "A 코어 + M 코어" 흉내 내기

### 10.1 구성

QEMU `virt`에는 보조 코어도 메일박스도 없다. 그래서 **두 개의 QEMU 인스턴스가 같은 호스트 파일을 RAM처럼 공유**하게 만든다.

- QEMU #1 = "A 코어": `Image-dom` Linux 게스트. `ivshmem-plain` PCI 장치를 붙이면 공유 파일이 **BAR2**로 보인다. 게스트는 sysfs `resource2`(또는 `resource2_wc`)를 `mmap`한다.
- QEMU #2 = "M 코어": 아무 OS 없는 bare-metal aarch64 프로그램. `-machine virt,memory-backend=...`로 **게스트 RAM 전체를 같은 파일로** 만든다. 그러면 이 "MCU"에게 공유 영역은 그냥 자기 RAM `0x40000000`부터다. i.MX의 OCRAM이나 DDR 캐브아웃처럼 "한쪽에게는 자기 메모리, 다른 쪽에게는 버스 너머 창"인 상황과 같은 모양이다.
- 공유 파일은 `/dev/shm/o1_ivshmem`(16 MiB, `share=on`). 링은 파일 오프셋 `0x800000`에 둔다. 앞쪽 8 MiB에는 "MCU"의 코드(`0x40200000` = 오프셋 2 MiB)가 들어간다.
- 메일박스 대신 링의 `head` 인덱스 갱신을 "doorbell 플래그"로 쓰고, 상대는 그걸 **폴링**한다. 실물 메일박스 인터럽트가 없을 때 무슨 대가를 치르는지 재기 위해서다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="660" height="280" rx="8" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="20" y="30" font-size="13" font-weight="bold">VM 호스트 (Lima elinux, aarch64)</text>
  <rect x="25" y="45" width="250" height="140" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="68" text-anchor="middle" font-size="13" font-weight="bold">QEMU #1 — Linux "A 코어"</text>
  <text x="150" y="92" text-anchor="middle" font-size="12">Image-dom + BusyBox</text>
  <text x="150" y="114" text-anchor="middle" font-size="12">o1_ping (유저 공간)</text>
  <text x="150" y="136" text-anchor="middle" font-size="12">ivshmem-plain BAR2</text>
  <text x="150" y="158" text-anchor="middle" font-size="12">0x8000000000 (16 MiB)</text>
  <rect x="405" y="45" width="250" height="140" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="530" y="68" text-anchor="middle" font-size="13" font-weight="bold">QEMU #2 — bare-metal "M 코어"</text>
  <text x="530" y="92" text-anchor="middle" font-size="12">o1_mcu.elf (OS 없음)</text>
  <text x="530" y="114" text-anchor="middle" font-size="12">RAM 전체 = 공유 파일</text>
  <text x="530" y="136" text-anchor="middle" font-size="12">링 @0x40800000</text>
  <text x="530" y="158" text-anchor="middle" font-size="12">UART 로그 → mcu.log</text>
  <rect x="180" y="215" width="320" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="238" text-anchor="middle" font-size="13">/dev/shm/o1_ivshmem (share=on)</text>
  <text x="340" y="258" text-anchor="middle" font-size="12">오프셋 0x800000: 헤더 + a2m 링 + m2a 링</text>
  <line x1="150" y1="187" x2="250" y2="212" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="530" y1="187" x2="430" y2="212" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="277" y1="100" x2="401" y2="100" stroke="#d0564a" stroke-dasharray="5,4" marker-end="url(#ah6)"/>
  <text x="340" y="92" text-anchor="middle" font-size="12">요청 (head++)</text>
  <line x1="403" y1="130" x2="279" y2="130" stroke="#d0564a" stroke-dasharray="5,4" marker-end="url(#ah6)"/>
  <text x="340" y="148" text-anchor="middle" font-size="12">응답 (폴링)</text>
</svg>
```
그림 9 — 실습 2의 구성. 메일박스 인터럽트가 없으니 두 쪽 다 상대의 인덱스를 폴링한다.

### 10.2 예제 10 — 양쪽이 공유하는 링 헤더

무엇을 확인하나: 단일 생산자/단일 소비자(SPSC) 링 두 개(A→M, M→A)와 barrier 위치. Don이 펌웨어에서 짜던 디스크립터 링과 같다.

```c
/* o1_ring.h — shared-memory mailbox stand-in used by both sides (A side: Linux, M side: bare-metal) */
#ifndef O1_RING_H
#define O1_RING_H
#include <stdint.h>
#define O1_MAGIC   0x4F31524Eu        /* "O1RN" */
#define O1_SLOTS   16                 /* power of two */
#define O1_SHM_OFF 0x800000u          /* ring lives 8 MiB into the shared 16 MiB region */

struct o1_msg  { uint32_t seq, cmd, arg[6]; };              /* 32 bytes */
struct o1_ring {                      /* single-producer / single-consumer */
	volatile uint32_t head;           /* written only by producer */
	uint32_t pad0[15];                /* keep head and tail on different 64-byte lines */
	volatile uint32_t tail;           /* written only by consumer */
	uint32_t pad1[15];
	struct o1_msg slot[O1_SLOTS];
};
struct o1_shm {
	volatile uint32_t magic;          /* M side writes O1_MAGIC when ready */
	volatile uint32_t m_served;       /* M side: number of requests handled */
	uint32_t pad[14];
	struct o1_ring a2m;               /* A -> M requests */
	struct o1_ring m2a;               /* M -> A replies  */
};

/* barriers: dmb ish is enough between two CPUs sharing Normal memory */
#define o1_wmb() __asm__ volatile("dmb ishst" ::: "memory")
#define o1_rmb() __asm__ volatile("dmb ishld" ::: "memory")

/* copy 32-bit word by word: safe even when the window is mapped as Device memory
 * (struct assignment may become memcpy with unaligned/vector accesses -> SIGBUS there) */
#ifdef O1_MEMCPY
#define o1_cpy(d, s) (*(d) = *(s))
#else
static inline void o1_cpy(struct o1_msg *d, const struct o1_msg *s)
{
	volatile uint32_t *dw = (volatile uint32_t *)d;
	const volatile uint32_t *sw = (const volatile uint32_t *)s;
	for (int i = 0; i < 8; i++) dw[i] = sw[i];
}
#endif
static inline int o1_push(struct o1_ring *r, const struct o1_msg *m)
{
	uint32_t h = r->head;
	if (h - r->tail == O1_SLOTS) return 0;             /* full */
	o1_cpy(&r->slot[h % O1_SLOTS], m);
	o1_wmb();                                          /* payload before index */
	r->head = h + 1;                                   /* = "doorbell" */
	return 1;
}
static inline int o1_pop(struct o1_ring *r, struct o1_msg *m)
{
	uint32_t t = r->tail;
	if (r->head == t) return 0;                        /* empty */
	o1_rmb();                                          /* index before payload */
	o1_cpy(m, &r->slot[t % O1_SLOTS]);
	o1_rmb();                                          /* loads done before tail store (ishld orders L->S) */
	r->tail = t + 1;
	return 1;
}
#endif
```

코드에서 볼 것:

- 인덱스는 **자유롭게 증가하는 32비트 카운터**이고 슬롯 번호는 `% O1_SLOTS`다. `head - tail`이 차 있는 개수라 "가득 참/빔"을 플래그 없이 구분한다(2의 거듭제곱 크기라 wrap도 안전).
- `head`와 `tail`을 다른 64바이트 줄에 둔 이유: 실물에서 캐시 관리(§5)를 줄 단위로 하므로, 한쪽만 쓰는 변수끼리 같은 줄에 두면 clean/invalidate가 상대 값을 덮는다.
- barrier 세 개. 생산자: 본문 쓰기 → `dmb ishst` → `head` 쓰기. 소비자: `head` 읽기 → `dmb ishld` → 본문 읽기 → `dmb ishld` → `tail` 쓰기. 마지막 것은 **읽기가 끝난 다음에 슬롯을 돌려준다**는 순서(load → store)라 `ishst`로는 부족하고 `ishld`가 필요하다. 처음 초안에서 여기를 `ishst`로 잘못 썼다가 고쳤다. QEMU에서는 증상이 안 났지만 약한 순서의 실물 코어에서는 생산자가 아직 읽히지 않은 슬롯을 덮어쓸 수 있다.
- 실물 SoC의 공유 메모리라면 `dmb ish`(inner shareable)가 아니라 `dmb osh`(outer shareable)나 `dmb sy`가 필요한 경우도 있다. M 코어가 A 클러스터와 같은 inner shareable 도메인에 있지 않을 수 있기 때문이다(SoC마다 다름 — 데이터시트로 확인).

### 10.3 예제 11 — bare-metal "MCU" 쪽

무엇을 확인하나: OS 없는 쪽 코드. 스택 설정, BSS 초기화, 링 폴링, UART 출력이 전부다.

```c
/* o1_mcu.c — bare-metal "MCU" stand-in: polls A->M ring, answers on M->A ring */
#include "o1_ring.h"
#define UART0 ((volatile uint32_t *)0x09000000)          /* QEMU virt PL011 DR */
#define SHM   ((struct o1_shm *)(0x40000000u + O1_SHM_OFF)) /* RAM base + offset */

static void puts_(const char *s) { while (*s) *UART0 = (uint32_t)*s++; }
static void puthex(uint32_t v)
{
	for (int i = 28; i >= 0; i -= 4) *UART0 = (uint32_t)"0123456789abcdef"[(v >> i) & 15];
}

void o1_main(void)
{
	struct o1_shm *s = SHM;
	struct o1_msg m;
	s->a2m.head = s->a2m.tail = 0;
	s->m2a.head = s->m2a.tail = 0;
	s->m_served = 0;
	o1_wmb();
	s->magic = O1_MAGIC;
	puts_("o1_mcu: up, ring at 0x"); puthex((uint32_t)(uintptr_t)s); puts_("\n");
	for (;;) {
		if (!o1_pop(&s->a2m, &m)) continue;            /* busy-poll: no IRQ here */
		if (m.cmd == 0xDEAD) { puts_("o1_mcu: stop\n"); for (;;) __asm__ volatile("wfi"); }
		uint32_t sum = 0;
		for (int i = 0; i < 6; i++) sum += m.arg[i];
		m.cmd |= 0x80000000u;                          /* reply flag */
		m.arg[0] = sum;                                /* "work": checksum of args */
		while (!o1_push(&s->m2a, &m)) ;
		s->m_served++;
	}
}
```

```text
/* o1_start.S — EL1 entry for QEMU virt -kernel ELF: set stack, zero bss, call C */
	.section .text.start
	.global _start
_start:
	mrs  x0, mpidr_el1
	and  x0, x0, #0xff
	cbnz x0, 1f                 /* only CPU0 runs */
	ldr  x0, =__stack_top
	mov  sp, x0
	ldr  x0, =__bss_start
	ldr  x1, =__bss_end
2:	cmp  x0, x1
	b.hs 3f
	str  xzr, [x0], #8
	b    2b
3:	bl   o1_main
1:	wfi
	b    1b
```

```text
/* o1_mcu.ld */
ENTRY(_start)
SECTIONS {
  . = 0x40200000;
  .text : { *(.text.start) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(8); __bss_end = .; }
  . = ALIGN(16); . += 0x10000; __stack_top = .;
}
```

여기서 중요한 컴파일 옵션 두 개가 있다. MMU를 켜지 않은 EL1에서 데이터 접근은 **Device 메모리 속성**으로 취급된다. 그래서 정렬 안 된 접근은 alignment fault다. `-mstrict-align`(비정렬 접근 생성 금지)과 `-mgeneral-regs-only`(구조체 복사에 SIMD `q` 레지스터를 쓰지 않음)로 막는다. 실물 Cortex-M에서는 MPU 영역 속성이 같은 역할을 한다.

빌드 스크립트(호스트 VM, 경고 0개):

```sh
#!/bin/bash
# o1_build.sh — build all O1 binaries in /var/tmp/O1
set -e; cd /var/tmp/O1; S=/Users/donh/.elinux_scratch/O1; W="-std=gnu11 -Wall -Wextra -O2 -static -I$S"
gcc $W $S/o1_ping.c -o o1_ping
gcc $W -DO1_MEMCPY $S/o1_ping.c -o o1_ping_memcpy
gcc $W $S/o1_hog.c -o o1_hog
gcc -ffreestanding -nostdlib -nostartfiles -static -O2 -Wall -Wextra -mstrict-align -mgeneral-regs-only \
    -I$S -T $S/o1_mcu.ld $S/o1_start.S $S/o1_mcu.c -o o1_mcu.elf -Wl,--build-id=none,--no-warn-rwx-segments
ls -l o1_ping o1_ping_memcpy o1_hog o1_mcu.elf
```

```text
-rwxrwxr-x 1 donh donh 852208 Oct 18 09:50 o1_hog
-rwxrwxr-x 1 donh donh  67376 Oct 18 09:50 o1_mcu.elf
-rwxrwxr-x 1 donh donh 716544 Oct 18 09:50 o1_ping
-rwxrwxr-x 1 donh donh 716544 Oct 18 09:50 o1_ping_memcpy
```

### 10.4 예제 12 — Linux 쪽 `o1_ping`

무엇을 확인하나: BAR를 `mmap`해서 링에 요청을 넣고 응답을 기다리며 왕복 시간(RTT)을 잰다. 세 번째 인자가 0이면 바쁜 폴링(busy-poll), 0보다 크면 폴링 사이에 `usleep`한다. 1초 안에 응답이 없으면 "상대가 죽었다"고 보고 종료 코드 3으로 끝난다(heartbeat 감시).

```c
/* o1_ping.c — Linux (A side): mmap the ivshmem BAR, ping the "MCU" over the ring, time round trips
 * usage: o1_ping <resource2 | /dev/uioN> <count> <poll_sleep_us (0 = busy poll)> [stop] */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include "o1_ring.h"
#define SHM_SIZE (16u << 20)
static double now_us(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static int cmpd(const void *a, const void *b)
{ double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: %s path count sleep_us [stop]\n", argv[0]); return 2; }
	int n = atoi(argv[2]), slp = atoi(argv[3]);
	off_t off = strncmp(argv[1], "/dev/uio", 8) ? 0 : 1 * getpagesize(); /* uio: map1 = BAR2 */
	int fd = open(argv[1], O_RDWR | O_SYNC);
	if (fd < 0) { perror("open"); exit(1); }
	char *base = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, off);
	if (base == MAP_FAILED) { perror("mmap"); exit(1); }
	struct o1_shm *s = (struct o1_shm *)(base + O1_SHM_OFF);
	while (s->magic != O1_MAGIC) usleep(1000);
	o1_rmb();
	double *rtt = malloc(sizeof(double) * n);
	struct rusage r0, r1; getrusage(RUSAGE_SELF, &r0);
	double t_all = now_us();
	for (int i = 0; i < n; i++) {
		struct o1_msg m = { .seq = (uint32_t)i, .cmd = 1, .arg = { 1, 2, 3, 4, 5, (uint32_t)i } }, r;
		double t0 = now_us();
		while (!o1_push(&s->a2m, &m)) ;
		while (!o1_pop(&s->m2a, &r)) {
			if (slp) usleep(slp);
			if (now_us() - t0 > 1e6) { printf("seq %d: no reply in 1 s, served=%u -> remote dead, Linux keeps running\n", i, s->m_served); return 3; }
		}
		rtt[i] = now_us() - t0;
		if (r.seq != (uint32_t)i || r.cmd != 0x80000001u || r.arg[0] != 15u + (uint32_t)i) {
			fprintf(stderr, "bad reply at %d: seq=%u cmd=%x sum=%u\n", i, r.seq, r.cmd, r.arg[0]); exit(1);
		}
	}
	t_all = now_us() - t_all; getrusage(RUSAGE_SELF, &r1);
	double cpu = (r1.ru_utime.tv_sec - r0.ru_utime.tv_sec + r1.ru_stime.tv_sec - r0.ru_stime.tv_sec) * 1e6
	           + (r1.ru_utime.tv_usec - r0.ru_utime.tv_usec + r1.ru_stime.tv_usec - r0.ru_stime.tv_usec);
	qsort(rtt, n, sizeof(double), cmpd);
	printf("poll_sleep=%dus n=%d  rtt_us min=%.1f p50=%.1f p99=%.1f max=%.1f  cpu=%.0f%%  served=%u\n",
	       slp, n, rtt[0], rtt[n / 2], rtt[n * 99 / 100], rtt[n - 1], 100.0 * cpu / t_all, s->m_served);
	if (argc > 4) { struct o1_msg st = { .cmd = 0xDEAD }; while (!o1_push(&s->a2m, &st)) ; }
	return 0;
}
```

(`/dev/uioN` 분기는 §11.4에서 보듯 ivshmem에서는 쓸 수 없었다. 그 결과까지 보여 주려고 남겨 뒀다.)

### 10.5 예제 13 — 먼저 VM 호스트에서 연기 시험

무엇을 확인하나: Linux 게스트 없이, "MCU" QEMU 하나와 VM 호스트 프로세스가 같은 `/dev/shm` 파일로 대화하는지. 프로토콜 버그를 게스트 부팅 전에 잡는 단계다(같은 `o1_ping`이 일반 파일도 `mmap`할 수 있다).

```sh
cd /var/tmp/O1; rm -f /dev/shm/o1_smoke
timeout 60 qemu-system-aarch64 -machine virt,memory-backend=o1mem -cpu cortex-a53 -m 16M -nographic \
  -object memory-backend-file,id=o1mem,size=16M,mem-path=/dev/shm/o1_smoke,share=on \
  -kernel o1_mcu.elf -monitor none -serial file:smoke.log &
sleep 3; ./o1_ping /dev/shm/o1_smoke 20000 0 stop; sleep 1; cat smoke.log
od -A x -t x4 -j $((0x800000)) -N 8 /dev/shm/o1_smoke; wait; rm -f /dev/shm/o1_smoke
```

```text
poll_sleep=0us n=20000  rtt_us min=0.2 p50=0.4 p99=1.1 max=9063.7  cpu=30%  served=20000
o1_mcu: up, ring at 0x40800000
o1_mcu: stop
800000 4f31524e 00004e20
800008
```

출력에서 볼 것: 20000번 왕복이 모두 검증을 통과했고, 파일 오프셋 `0x800000`에 매직 `0x4F31524E`와 처리 수 `0x4e20` = 20000이 남았다. `cpu=30%`는 "CPU 시간 / 벽시계 시간"인데, 바쁜 폴링인데도 100%가 안 되는 건 VM의 vCPU 6개를 19명이 나눠 쓰느라 이 프로세스가 자주 밀려났기 때문이다(측정 환경의 한계). `max=9063.7`us 꼬리도 같은 이유다.

---

## 11. 실습 2 — Linux 게스트 ↔ bare-metal "MCU" 왕복 측정

### 11.1 실행 스크립트

```sh
#!/bin/bash
# o1_lab2_host.sh — QEMU #1: Linux (Image-dom) with ivshmem-plain;  QEMU #2: bare-metal "MCU" whose RAM is the same file
set -e; cd /var/tmp/O1; SHM=/dev/shm/o1_ivshmem
rm -f $SHM lab2.log mcu.log
timeout 900 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M -nographic \
  -kernel ${K:-/var/lab/out/Image-dom} -initrd o1_init.gz -serial file:lab2.log -monitor none \
  -object memory-backend-file,id=o1sh,size=16M,mem-path=$SHM,share=on -device ivshmem-plain,memdev=o1sh \
  -append "console=ttyAMA0 quiet LAB_CMD=/o1_lab2_guest.sh" & LPID=$!
sleep 1
timeout 900 qemu-system-aarch64 -machine virt,memory-backend=o1mem -cpu cortex-a53 -m 16M -nographic \
  -object memory-backend-file,id=o1mem,size=16M,mem-path=$SHM,share=on \
  -kernel o1_mcu.elf -serial file:mcu.log -monitor none & MPID=$!
for i in $(seq 1 800); do grep -q O1_READY_FOR_KILL lab2.log 2>/dev/null && break; sleep 1; done
kill $MPID; wait $MPID 2>/dev/null || true; echo "[host] killed MCU qemu (pid $MPID)"
for i in $(seq 1 120); do grep -q O1_LAB2_DONE lab2.log 2>/dev/null && break; sleep 1; done
sleep 2; kill $LPID 2>/dev/null || true; wait $LPID 2>/dev/null || true
echo "--- mcu.log"; cat mcu.log; echo "--- lab2.log"; tr -d '\r' < lab2.log | sed -n '/=== ivshmem/,/O1_LAB2_DONE/p'
rm -f $SHM
```

```sh
#!/bin/sh
# o1_lab2_guest.sh — find the ivshmem BAR, ping the bare-metal "MCU" through it
for d in /sys/bus/pci/devices/*; do
  [ "$(cat $d/vendor)" = 0x1af4 ] && [ "$(cat $d/device)" = 0x1110 ] && D=$d
done
echo "=== ivshmem at $D"; head -3 $D/resource; ls $D | grep resource
echo 1 > $D/enable
echo "=== struct-copy build on resource2 (Device mapping)"; /o1_ping_memcpy $D/resource2 100 0; dmesg | grep -i "o1_ping" | tail -2
echo "=== struct-copy build on resource2_wc (Normal non-cacheable)"; /o1_ping_memcpy $D/resource2_wc 20000 0
for a in "20000 0" "2000 50" "500 1000"; do /o1_ping $D/resource2 $a; done
echo "=== uio_pci_generic"
modprobe uio_pci_generic && echo "1af4 1110" > /sys/bus/pci/drivers/uio_pci_generic/new_id
for m in /sys/class/uio/uio0/maps/map*; do echo "$m addr=$(cat $m/addr) size=$(cat $m/size)"; done
dmesg | grep -i uio_pci_generic | tail -2
echo "=== heartbeat test"; /o1_ping $D/resource2 2000 0
echo O1_READY_FOR_KILL; sleep 8
/o1_ping $D/resource2 100 0; echo "o1_ping rc=$? ; Linux still alive: $(cut -d' ' -f1 /proc/uptime)s" 
echo O1_LAB2_DONE
```

`1af4:1110`은 ivshmem의 PCI vendor:device ID다. initramfs에는 `modules-dom`의 `uio*.ko`와 `modules.*` 파일을 복사해 넣어 BusyBox `modprobe`가 의존성을 풀게 했다.

### 11.2 예제 14 — BAR 찾기와 "Bus error"

```text
--- mcu.log
o1_mcu: up, ring at 0x40800000
--- lab2.log
=== ivshmem at /sys/bus/pci/devices/0000:00:02.0
0x0000000010081000 0x00000000100810ff 0x0000000000040200
0x0000000000000000 0x0000000000000000 0x0000000000000000
0x0000008000000000 0x0000008000ffffff 0x000000000014220c
resource
resource0
resource2
resource2_wc
=== struct-copy build on resource2 (Device mapping)
Bus error
=== struct-copy build on resource2_wc (Normal non-cacheable)
poll_sleep=0us n=20000  rtt_us min=0.5 p50=1.2 p99=3.5 max=15877.8  cpu=90%  served=20000
```

출력에서 볼 것:

- `resource` 파일의 세 줄은 BAR0(레지스터 256 B), BAR1(없음), BAR2(공유 메모리 16 MiB, 64비트 창 `0x8000000000`)다. BAR2의 플래그 `0x14220c`에는 prefetchable 비트가 있어서 커널이 `resource2_wc`(write-combine 매핑)도 만들어 줬다.
- 링 코드에서 `o1_cpy`를 **구조체 대입**으로 바꾼 빌드(`-DO1_MEMCPY`)는 `resource2`에서 즉시 `Bus error`(SIGBUS)로 죽었다. 같은 바이너리가 `resource2_wc`에서는 20000번을 멀쩡히 돌았다. 이유는 다음 예제가 보여 준다.

### 11.3 예제 15 — 매핑 속성 시험: Device vs Normal-NC

무엇을 확인하나: 같은 BAR를 두 가지 속성으로 매핑했을 때 어떤 접근이 살아남는지. SIGBUS를 잡아 `si_code`를 출력한다.

```c
/* o1_memtype.c — which accesses survive on a PCI BAR mmap? (resource2 = Device, resource2_wc = Normal-NC)
 * usage: o1_memtype <sysfs resourceN file> */
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
static sigjmp_buf jb;
static void on_bus(int sig, siginfo_t *si, void *uc)
{
	(void)uc;
	printf("    -> signal %d si_code=%d (%s) addr=%p\n", sig, si->si_code,
	       si->si_code == BUS_ADRALN ? "BUS_ADRALN alignment" : "other", si->si_addr);
	siglongjmp(jb, 1);
}
#define TRY(name, stmt) do { printf("%-34s", name); fflush(stdout);           \
	if (!sigsetjmp(jb, 1)) { stmt; printf("    ok\n"); } } while (0)

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s resourceN\n", argv[0]); return 2; }
	struct sigaction sa = { .sa_sigaction = on_bus, .sa_flags = SA_SIGINFO };
	sigaction(SIGBUS, &sa, NULL);
	int fd = open(argv[1], O_RDWR | O_SYNC);
	if (fd < 0) { perror("open"); exit(1); }
	volatile uint8_t *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) { perror("mmap"); exit(1); }
	printf("%s\n", argv[1]);
	TRY("32-bit aligned store/load", *(volatile uint32_t *)(p + 0x100) = 0x11; (void)*(volatile uint32_t *)(p + 0x100));
	TRY("64-bit store at +0x104 (unaligned)", *(volatile uint64_t *)(p + 0x104) = 0x22);
	TRY("128-bit q store, 16B-aligned", __asm__ volatile("movi v0.16b, #0x33\n str q0, [%0]" :: "r"(p + 0x200) : "v0", "memory"));
	TRY("128-bit q store at +0x208", __asm__ volatile("movi v0.16b, #0x44\n str q0, [%0]" :: "r"(p + 0x208) : "v0", "memory"));
	TRY("dc zva (cache-line zero)", __asm__ volatile("dc zva, %0" :: "r"(p + 0x400) : "memory"));
	return 0;
}
```

게스트에서 `sh /o1_mt_guest.sh`(장치를 enable하고 두 파일에 대해 실행)로 돌린 결과:

```text
/sys/bus/pci/devices/0000:00:02.0/resource2
32-bit aligned store/load             ok
64-bit store at +0x104 (unaligned)    -> signal 7 si_code=1 (BUS_ADRALN alignment) addr=0xffffaeaa8104
128-bit q store, 16B-aligned          ok
128-bit q store at +0x208             -> signal 7 si_code=1 (BUS_ADRALN alignment) addr=0xffffaeaa8208
dc zva (cache-line zero)              ok
/sys/bus/pci/devices/0000:00:02.0/resource2_wc
32-bit aligned store/load             ok
64-bit store at +0x104 (unaligned)    ok
128-bit q store, 16B-aligned          ok
128-bit q store at +0x208             ok
dc zva (cache-line zero)              ok
```

출력에서 볼 것:

- `resource2`는 커널이 `pgprot_device()`(arm64의 Device-nGnRE)로 매핑한다(`drivers/pci/mmap.c` 38행, `_wc`면 36행의 `pgprot_writecombine()`). **Device 메모리에서는 크기만큼 정렬되지 않은 접근이 alignment fault**이고, 커널은 그걸 `SIGBUS`/`BUS_ADRALN`으로 돌려준다.
- 구조체 대입 빌드를 `objdump`로 보면 32바이트 복사가 `ldr q31` / `str q31` 128비트 접근으로 컴파일돼 있었다. 컴파일러는 `struct o1_msg`가 4바이트 정렬이라는 것만 알고, Normal 메모리라면 비정렬이 허용되니 이렇게 만든다. 그 가정이 Device 매핑에서 깨진 것이다. 커널 드라이버가 MMIO에 `memcpy` 대신 `memcpy_toio()`/`memcpy_fromio()`를 써야 하는 이유와 정확히 같다(H1).
- `dc zva`가 Device 매핑에서 "ok"로 나온 건 **QEMU TCG가 이 검사를 모델링하지 않아서**로 본다. Arm 아키텍처 규칙상 Device 메모리에 대한 DC ZVA는 alignment fault다(Arm ARM 기준, 실물에서 확인 권장). glibc `memset`이 큰 영역에 DC ZVA를 쓰므로, 실물에서는 Device 매핑에 `memset`을 하면 죽는다.
- 그래서 실물 AMP 공유 메모리는 **Normal Non-cacheable**(`ioremap_wc`/`memremap(..., MEMREMAP_WC)`, 유저 공간이면 `_wc` 매핑)로 쓰는 게 표준이다. §5의 `imx_rproc`가 `ioremap_wc`를 쓰는 것과 같은 결론이다.

### 11.4 예제 16 — 폴링 방식별 왕복 시간과 CPU 사용, 그리고 UIO의 한계

```text
poll_sleep=0us n=20000  rtt_us min=0.8 p50=1.0 p99=4.6 max=9587.4  cpu=84%  served=40000
poll_sleep=50us n=2000  rtt_us min=238.1 p50=398.2 p99=7494.7 max=19496.5  cpu=41%  served=42000
poll_sleep=1000us n=500  rtt_us min=1256.4 p50=1435.6 p99=10645.0 max=24684.4  cpu=19%  served=42500
=== uio_pci_generic
/sys/class/uio/uio0/maps/map0 addr=0x0000000010081000 size=0x0000000000001000
[   15.010906][   T75] uio_pci_generic 0000:00:02.0: No IRQ assigned to device: no support for interrupts?
```

```svg
<svg viewBox="0 0 640 310" xmlns="http://www.w3.org/2000/svg">
  <text x="185" y="22" text-anchor="middle" font-size="13" font-weight="bold">왕복 시간 p50 (us, 로그 축)</text>
  <text x="505" y="22" text-anchor="middle" font-size="13" font-weight="bold">CPU 사용 (%)</text>
  <line x1="70" y1="250" x2="300" y2="250" stroke="currentColor"/>
  <line x1="70" y1="50" x2="70" y2="250" stroke="currentColor"/>
  <text x="62" y="254" text-anchor="end" font-size="12">0.1</text>
  <text x="62" y="214" text-anchor="end" font-size="12">1</text>
  <text x="62" y="174" text-anchor="end" font-size="12">10</text>
  <text x="62" y="134" text-anchor="end" font-size="12">100</text>
  <text x="62" y="94" text-anchor="end" font-size="12">1000</text>
  <text x="62" y="54" text-anchor="end" font-size="12">10000</text>
  <line x1="70" y1="210" x2="300" y2="210" stroke="#888" stroke-opacity="0.3"/>
  <line x1="70" y1="170" x2="300" y2="170" stroke="#888" stroke-opacity="0.3"/>
  <line x1="70" y1="130" x2="300" y2="130" stroke="#888" stroke-opacity="0.3"/>
  <line x1="70" y1="90" x2="300" y2="90" stroke="#888" stroke-opacity="0.3"/>
  <rect x="95" y="210" width="40" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="204" text-anchor="middle" font-size="12">1.0</text>
  <rect x="165" y="106" width="40" height="144" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="185" y="100" text-anchor="middle" font-size="12">398</text>
  <rect x="235" y="84" width="40" height="166" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="255" y="78" text-anchor="middle" font-size="12">1436</text>
  <text x="115" y="270" text-anchor="middle" font-size="12">busy</text>
  <text x="185" y="270" text-anchor="middle" font-size="12">50us</text>
  <text x="255" y="270" text-anchor="middle" font-size="12">1ms</text>
  <line x1="390" y1="250" x2="620" y2="250" stroke="currentColor"/>
  <line x1="390" y1="50" x2="390" y2="250" stroke="currentColor"/>
  <text x="382" y="254" text-anchor="end" font-size="12">0</text>
  <text x="382" y="154" text-anchor="end" font-size="12">50</text>
  <text x="382" y="54" text-anchor="end" font-size="12">100</text>
  <line x1="390" y1="150" x2="620" y2="150" stroke="#888" stroke-opacity="0.3"/>
  <rect x="415" y="82" width="40" height="168" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="435" y="76" text-anchor="middle" font-size="12">84</text>
  <rect x="485" y="168" width="40" height="82" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="505" y="162" text-anchor="middle" font-size="12">41</text>
  <rect x="555" y="212" width="40" height="38" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="575" y="206" text-anchor="middle" font-size="12">19</text>
  <text x="435" y="270" text-anchor="middle" font-size="12">busy</text>
  <text x="505" y="270" text-anchor="middle" font-size="12">50us</text>
  <text x="575" y="270" text-anchor="middle" font-size="12">1ms</text>
  <text x="320" y="298" text-anchor="middle" font-size="12">Linux 쪽 폴링 간격별 (QEMU TCG + 과부하 VM 측정, 실물 수치 아님)</text>
</svg>
```
그림 10 — 메일박스 인터럽트 없이 폴링할 때의 트레이드오프(예제 16의 실측값). 지연을 줄이면 CPU를 태우고, CPU를 아끼면 지연이 폴링 간격 + 타이머 슬랙만큼 늘어난다.

출력에서 볼 것:

- **바쁜 폴링**: p50 1.0 us로 빠르지만 Linux 쪽 CPU 하나를 84% 태운다(100%가 아닌 건 과부하 VM에서 vCPU가 밀려나서). "MCU" 쪽도 내내 돌고 있다. 실물이라면 배터리와 열을 먹고, 그 CPU에서 돌 수 있었던 일을 밀어낸다.
- **50 us 쉬며 폴링**: CPU는 41%로 줄었지만 p50이 398 us다. 50 us를 요청해도 `usleep`은 타이머 해상도·슬랙(`timer_slack_ns` 기본 50 us)·스케줄링 지연 때문에 훨씬 길게 잔다. TCG에서는 특히 심하다.
- **1 ms 쉬며 폴링**: CPU 19%, p50 1.4 ms. 지연의 하한이 폴링 주기 자체가 된다.
- 어느 쪽이든 p99·max 꼬리는 ms~수십 ms로 길다. Linux 쪽 스케줄링 지연이 그대로 드러난다(N3에서 본 지연 꼬리).
- 실물 메일박스는 이 트레이드오프를 깬다. 상대는 IRQ가 올 때까지 **자고**(CPU 0%), 깨어나는 지연은 인터럽트 지연 + 스레드 깨우기 수준이다. 그래서 rpmsg·BPMP·SCMI 모두 "공유 메모리 + 메일박스 IRQ"다. 바쁜 폴링이 정당한 경우는 M 코어 쪽이 어차피 고정 주기 루프를 돌면서 그 루프 안에서 인덱스를 확인하는 정도다.
- **UIO의 한계(직접 확인)**: `uio_pci_generic`은 BAR0(4 KiB)만 `map0`으로 내보냈고 BAR2(공유 메모리)는 빠졌다. 소스(`drivers/uio/uio_pci_generic.c` 110행)가 `r->flags != (IORESOURCE_SIZEALIGN | IORESOURCE_MEM)`이면 건너뛰는데, 64비트 prefetchable BAR는 `IORESOURCE_MEM_64`·`IORESOURCE_PREFETCH` 플래그가 더 붙어 있어 조건에 안 맞는다. 게다가 `ivshmem-plain`은 인터럽트가 없어서 "No IRQ assigned" 경고가 나온다. UIO를 쓰는 의미(유저 공간 IRQ 대기)가 없으니 이 실습에서는 sysfs `resource2`가 정답이다. 인터럽트가 필요하면 QEMU `ivshmem-doorbell` + `ivshmem-server`가 있지만 이 VM에는 `ivshmem-server` 바이너리가 없어 해 보지 못했다.

### 11.5 예제 17 — 장애 격리: "MCU"가 죽어도 Linux는 산다

```text
=== heartbeat test
poll_sleep=0us n=2000  rtt_us min=1.5 p50=1.6 p99=3.2 max=4146.9  cpu=85%  served=44500
O1_READY_FOR_KILL
seq 0: no reply in 1 s, served=44500 -> remote dead, Linux keeps running
o1_ping rc=3 ; Linux still alive: 24.07s
```

호스트 쪽 출력에는 `[host] killed MCU qemu (pid ...)`가 찍혔다(PID는 실행마다 다르다).

출력에서 볼 것: 호스트가 "MCU" QEMU를 `kill`한 뒤 Linux 쪽은 1초 타임아웃으로 상대의 죽음을 감지했다. 공유 메모리의 `m_served` 카운터가 44500에서 멈춘 게 heartbeat다. Linux 게스트는 계속 살아 있고(`uptime` 24 s) 다음 일을 할 수 있다. 실물에서도 같은 구조를 쓴다: M 코어가 주기적으로 카운터를 올리거나 메일박스로 heartbeat를 보내고, Linux 쪽 감시 스레드가 멈춤을 보면 remoteproc recovery를 건다(O2). 단 이 실습은 **두 프로세스**라서 격리가 완벽하다. 실물 SoC는 §9의 공유 자원(DDR·클럭·전원) 때문에 이만큼 깔끔하지 않을 수 있다.

---

## 12. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="200" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="45" text-anchor="middle" font-size="12">DTB /reserved-memory</text>
  <rect x="20" y="85" width="200" height="54" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="107" text-anchor="middle" font-size="12">fdt_scan_reserved_mem()</text>
  <text x="120" y="127" text-anchor="middle" font-size="12">__reserved_mem_reserve_reg()</text>
  <rect x="20" y="165" width="200" height="54" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="187" text-anchor="middle" font-size="12">no-map: memblock_mark_nomap</text>
  <text x="120" y="207" text-anchor="middle" font-size="12">아니면 memblock_reserve</text>
  <rect x="20" y="245" width="200" height="54" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="120" y="267" text-anchor="middle" font-size="12">선형 매핑 없음, buddy 제외</text>
  <text x="120" y="287" text-anchor="middle" font-size="12">/proc/iomem 최상위 reserved</text>
  <rect x="250" y="85" width="200" height="54" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="350" y="107" text-anchor="middle" font-size="12">shared-dma-pool:</text>
  <text x="350" y="127" text-anchor="middle" font-size="12">rmem_dma_setup()</text>
  <rect x="250" y="165" width="200" height="54" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="350" y="187" text-anchor="middle" font-size="12">of_reserved_mem_device_init</text>
  <text x="350" y="207" text-anchor="middle" font-size="12">장치별 coherent 풀 연결</text>
  <rect x="250" y="245" width="200" height="54" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="350" y="267" text-anchor="middle" font-size="12">dma_alloc_coherent()</text>
  <text x="350" y="287" text-anchor="middle" font-size="12">rpmsg 버퍼 (O2)</text>
  <rect x="480" y="85" width="180" height="54" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="107" text-anchor="middle" font-size="12">remoteproc 드라이버</text>
  <text x="570" y="127" text-anchor="middle" font-size="12">of_reserved_mem_lookup</text>
  <rect x="480" y="165" width="180" height="54" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="187" text-anchor="middle" font-size="12">ioremap_wc (Normal-NC)</text>
  <text x="570" y="207" text-anchor="middle" font-size="12">ELF 로드, rsc table</text>
  <rect x="480" y="245" width="180" height="54" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="267" text-anchor="middle" font-size="12">mbox_send_message</text>
  <text x="570" y="287" text-anchor="middle" font-size="12">doorbell (H10)</text>
  <line x1="120" y1="62" x2="120" y2="82" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="120" y1="141" x2="120" y2="162" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="120" y1="221" x2="120" y2="242" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="222" y1="112" x2="247" y2="112" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="350" y1="141" x2="350" y2="162" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="350" y1="221" x2="350" y2="242" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="452" y1="112" x2="477" y2="112" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="570" y1="141" x2="570" y2="162" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="570" y1="221" x2="570" y2="242" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="340" y="328" text-anchor="middle" font-size="12">함수 이름은 6.18.55 drivers/of/of_reserved_mem.c, kernel/dma/coherent.c 기준</text>
</svg>
```
그림 11 — 캐브아웃 선언이 커널 안에서 흘러가는 길. 왼쪽 열은 부팅 극초반(memblock 시절), 가운데는 DMA 풀, 오른쪽은 보조 코어 드라이버가 쓰는 쪽이다.

단계별로 정리한다(함수 위치는 랩 커널 소스에서 grep으로 확인).

1. **부팅 극초반**: `fdt_scan_reserved_mem()`(`drivers/of/of_reserved_mem.c` 301행)이 `/reserved-memory` 자식을 훑고, `reg`가 있는 노드마다 `__reserved_mem_reserve_reg()` → `early_init_dt_reserve_memory()`를 부른다. `no-map`이면 `memblock_mark_nomap()`(155행), 아니면 `memblock_reserve()`. 예제 7의 `memblock` 덤프에서 본 `NOMAP` 플래그가 여기서 붙는다. 이건 버디 할당자가 생기기도 전이다(F10 §3).
2. **선형 매핑 생성**: arm64 `paging_init()` 계열이 memblock을 보고 선형 매핑을 만들 때 `NOMAP` 영역은 건너뛴다. 그래서 그 물리 주소에는 커널 가상 주소가 아예 없고, `pfn_valid()`나 `page_is_ram()` 류 판단에서도 RAM이 아니다. `/dev/mem`이 캐브아웃은 열어 주고 System RAM은 `EPERM`으로 막은 것(예제 7·9)이 이 판단의 결과다(`STRICT_DEVMEM`).
3. **DMA 풀**: `compatible = "shared-dma-pool"`이면 `RESERVEDMEM_OF_DECLARE(dma, "shared-dma-pool", rmem_dma_setup)`(`kernel/dma/coherent.c` 409행)이 등록한 콜백이 돈다. dmesg의 `Reserved memory: created DMA memory pool at ...` 줄이 그 흔적이다. 장치 드라이버가 `of_reserved_mem_device_init_by_idx()`(654행)를 부르면 그 장치의 `dma_alloc_coherent()`가 이 풀에서 나온다.
4. **보조 코어 드라이버**: remoteproc 드라이버(`imx_rproc` 등)는 `memory-region` phandle을 `of_reserved_mem_lookup()`(758행)으로 풀어 주소를 얻고, `ioremap_wc()`로 Normal Non-cacheable 매핑을 만든 뒤 `rproc_mem_entry`로 등록한다. 펌웨어 ELF 로드와 resource table 파싱이 그 매핑 위에서 일어난다(O2).
5. **신호**: `mbox_request_channel()`로 받은 채널에 `mbox_send_message()`를 보내면 컨트롤러 드라이버의 `send_data`가 MU 레지스터에 쓴다. 상대의 응답 IRQ는 컨트롤러 ISR → `mbox_chan_received_data()` → 클라이언트 `rx_callback`으로 온다(H10 §3~4).
6. **유저 공간 창(실습 경로)**: sysfs `resourceN` `mmap`은 `pci_mmap_resource_range()`(`drivers/pci/mmap.c`)로 가서 `pgprot_device()` 또는 `pgprot_writecombine()`을 고른다(예제 15). UIO는 `uio_mmap_physical()`에서 `pgprot_noncached()`(`drivers/uio/uio.c` 759행)를 쓴다. 드라이버를 쓸 때 `.mmap` 구현에서 `remap_pfn_range()` 앞에 어떤 `pgprot`를 고르느냐가 G5에서 배운 그 결정이다.

| Don이 펌웨어에서 하던 것 | Linux에서 대응하는 것 |
|---|---|
| 링커 스크립트로 코어별 SRAM 구역 나누기 | DT `reserved-memory` + `no-map` |
| MPU로 공유 구역 non-cacheable 지정 | `ioremap_wc` / `memremap(MEMREMAP_WC)` / `_wc` 매핑 |
| 디스크립터 쓰고 clean → doorbell | `dma_sync_single_for_device()` → `mbox_send_message()` |
| IPC IRQ에서 invalidate → 읽기 | `rx_callback` → `dma_sync_single_for_cpu()` → 읽기 |
| 다른 코어 리셋·부팅 레지스터 직접 쓰기 | remoteproc `start`/`stop` (리셋은 reset/SCI/SMC 경유) |
| 코어 hang 감지 워치독 | remoteproc `rproc_report_crash()` + recovery |

---

## 13. Embedded 실무에서 다시 보기

### 13.1 장면 1 — 센서 키트의 CAN 모터 제어를 M 코어로

예를 들어 i.MX93 기반 로봇에서 Linux는 카메라·ROS 2를, M33은 CAN 모터 드라이버 4개와 비상 정지를 맡는다고 하자. 체크리스트:

- M33 펌웨어는 TCM에서 실행, CAN 버퍼도 TCM. DDR은 Linux와의 메시지(rpmsg)에만.
- Linux DT에서 M33이 쓸 CAN 노드(`flexcan`)는 `status = "disabled"`, 핀(pinctrl)도 Linux 쪽 그룹에서 뺀다(H4).
- 그 CAN 클럭을 Linux가 `clk_disable_unused`로 끄지 않도록 remoteproc 노드나 클럭 드라이버에 걸어 둔다(§7.3).
- 부팅 직후부터 모터를 "브레이크 걸린 안전 상태"로 잡아야 하면 U-Boot에서 M33을 먼저 띄우고 Linux는 attach(§8.2).
- Linux 쪽 감시: rpmsg heartbeat가 200 ms 끊기면 로그 + remoteproc recovery. M33 쪽 감시: Linux 명령이 100 ms 끊기면 모터 정지(정확한 시간은 시스템 위험 분석으로 정한다 — N4 §1.2).

### 13.2 장면 2 — "가끔 M 코어 응답이 옛날 값"

현장 버그 보고: "rpmsg 응답의 센서 값이 가끔 직전 값으로 온다." 원인 후보와 확인 순서:

1. 캐브아웃에 `no-map`이 빠졌나? `/proc/iomem`에서 그 영역이 System RAM 안에 있으면 의심(예제 7과 비교).
2. M7(D-cache 있음)에서 응답을 쓴 뒤 clean을 빼먹었나? M 코어 펌웨어의 MPU 설정과 `SCB_CleanDCache_by_Addr` 호출 확인.
3. 링 인덱스와 본문 사이 barrier가 빠졌나? 예제 10의 세 위치와 대조.
4. QEMU나 시뮬레이터에서 재현이 안 된다고 "HW 문제"로 넘기지 말 것 — §5.3처럼 TCG는 캐시가 없다.

### 13.3 장면 3 — Jetson에서 클럭 드라이버 probe가 실패

Jetson 보드에서 커스텀 DT를 쓰다 `tegra-bpmp` probe가 실패하면 거의 모든 장치의 클럭·리셋이 안 잡혀 연쇄적으로 `-EPROBE_DEFER`가 난다. BPMP는 Linux가 띄우는 코어가 아니라 **Linux보다 먼저 떠 있어야 하는** 시스템 코어다. 확인할 것: BPMP 펌웨어(부트 체인의 일부)와 커널 DT의 `shmem`(SYSRAM 창)·`mboxes`(HSP doorbell)가 일치하는지, `/sys/kernel/debug/devices_deferred`에 무엇이 쌓였는지(M2).

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 캐브아웃에 `no-map` 누락 | 가끔 옛 값, 랜덤 손상, 재현 불가 | 선형 매핑의 캐시 가능 속성 + 프리페치, 또는 CMA·버디가 그 페이지를 줌 | `no-map` 추가, `/proc/iomem`에서 System RAM 밖인지 확인 |
| 캐브아웃 주소가 기본 CMA와 겹침 | 카메라 켜면 M 코어 펌웨어가 죽음 | 예제 9처럼 CMA가 그 자리에 잡힘 | `reserved-memory`로 고정 선언, 부팅 로그의 `cma: Reserved ... at` 확인 |
| M 코어 주소(da)를 시스템 주소로 착각 | 펌웨어 로드 후 즉시 HardFault | TCM 별칭(§2.2) | 드라이버 ATT 표·데이터시트 메모리 맵으로 변환 |
| 공유 창을 Device로 매핑하고 `memcpy`/`memset` | `SIGBUS`(유저), alignment fault oops(커널) | Device 메모리의 비정렬·DC ZVA 금지 | Normal-NC(`_wc`/`ioremap_wc`) 매핑, 커널은 `memcpy_toio` |
| 인덱스와 본문 사이 barrier 누락 | 수백만 번에 한 번 잘못된 메시지 | 약한 메모리 순서 | 생산자 `dmb ishst`(또는 osh), 소비자 `dmb ishld` 두 곳 |
| M 코어 전용 장치를 Linux DT에서도 켬 | M 코어 CAN이 Linux 부팅 중 리셋됨 | 두 OS가 같은 블록을 소유 | Linux DT `status = "disabled"`, 핀 그룹 분리 |
| M 코어 클럭을 Linux가 끔 | 부팅 몇 초 뒤 M 코어 UART·타이머 정지 | `clk_disable_unused` | remoteproc `clocks`에 등록 또는 critical 표시, `clk_ignore_unused`로 진단 |
| 공유 창을 폴링으로만 동기화 | CPU 한 개 상시 사용 또는 ms 지연 | 메일박스 IRQ 미사용 | 메일박스 doorbell + 공유 메모리 본문 (예제 16) |
| "QEMU에서 잘 됨" = 끝 | 실물에서만 캐시 버그 | TCG는 캐시 미모델링 | 실물에서 스트레스 + 캐시 관리 코드 리뷰 |

---

## 15. 면접에서 이렇게 말한다

**Q.** Why do SoCs like i.MX93 or AM62x put a Cortex-M or R core next to the Linux cores?

**A.** 네 가지다. 빠른 부팅(Linux가 뜨기 전 CAN·카메라), 결정론적 실시간 루프, A 코어를 끈 상태의 저전력 상시 감시, 그리고 Linux와 독립적으로 살아야 하는 안전 기능. 실제 예로 i.MX93은 A55 두 개 옆에 M33, AM62x는 A53 네 개 옆에 MCU 도메인 M4F와 wake-up 도메인 R5F가 있다.

> Four reasons: fast boot, so CAN and a camera preview work before Linux is up; deterministic real-time loops; an always-on low-power domain that watches sensors while the A cores and DDR sleep; and safety functions that must survive a Linux crash. For example, i.MX93 has a Cortex-M33 next to two A55s, and AM62x has an M4F in the MCU domain plus an R5F in the wake-up domain. The key is that the M core's code, clocks and reset are independent enough that this actually holds.

**꼬리 질문**: "Then why not just use an external MCU?" → 같은 칩이면 BOM·대역폭·OTA가 유리하지만 DDR·클럭·전원 공유로 격리가 약하다. 안전 등급이 높으면 외부 MCU(O3).

**Q.** How do you make sure Linux never touches the memory the M core uses?

**A.** DT `reserved-memory`에 고정 주소 `reg`와 `no-map`으로 선언한다. 그러면 memblock에 NOMAP이 붙어 선형 매핑도 버디 할당도 없다. `/proc/iomem`에서 System RAM 밖으로 빠졌는지, `MemTotal`이 줄었는지로 확인한다. 직접 해 봤을 때 RAM을 거의 다 써도 캐브아웃의 바이트는 그대로였고, 반대로 선언을 빼면 기본 CMA가 바로 그 주소에 잡혔다. 그리고 DT는 약속일 뿐이니 RDC나 TZASC 같은 방화벽으로 DMA까지 막는다.

> I declare it under /reserved-memory with a fixed reg and no-map. The kernel then marks it NOMAP in memblock, so there's no linear mapping and the buddy allocator never sees it. I verify in /proc/iomem that it's outside System RAM and that MemTotal dropped by the carveout size. In a QEMU test, filling almost all RAM left the carveout bytes intact, and without the node the default CMA area landed right on top of that address. Since DT is only a promise, I also use the SoC's firewall, like RDC or TZASC, to stop rogue DMA.

**Q.** The A core and the M core share a buffer. What about cache coherency?

**A.** M 코어는 보통 A 클러스터의 코히어런시 도메인 밖이다. 가장 흔한 해법은 공유 영역을 양쪽 다 non-cacheable로 매핑하는 것(Linux는 `ioremap_wc`, M 코어는 MPU)이고, 이때도 인덱스와 본문 사이 barrier는 필요하다. 큰 버퍼를 캐시로 쓰면 보내기 전 clean, 받은 뒤 invalidate를 하고 캐시 라인 정렬을 맞춘다.

> Usually the M core is outside the A cluster's coherency domain, so I either map the shared region non-cacheable on both sides — ioremap_wc on Linux, an MPU region on the M core — or keep it cacheable and do explicit clean before the doorbell and invalidate after it, with cache-line-aligned buffers. Either way I still need barriers between the payload and the index. One trap: QEMU doesn't model caches, so a simulator passing proves nothing about coherency.

**Q.** Why do real SoCs use a mailbox interrupt instead of just polling a flag in shared memory?

**A.** 직접 재 봤다. QEMU 두 대를 공유 메모리로 잇고 폴링하면 바쁜 폴링은 왕복 1 us지만 CPU 하나를 84% 태웠고, 50 us씩 쉬면 CPU는 41%로 줄지만 중앙값이 400 us로 뛰었다. 메일박스 IRQ는 상대가 자다가 깨면 되니 CPU 0%에 지연도 인터럽트 지연 수준이다. 또 메일박스는 suspend 상태의 웨이크업 소스가 된다.

> I measured it with two QEMU instances sharing memory: busy polling gave about 1 microsecond round trips but burned 84 percent of a CPU, while sleeping 50 microseconds between polls cut CPU to 41 percent but pushed the median to around 400 microseconds. Those were TCG numbers, so only the trend matters. A mailbox interrupt lets the other side sleep and wake on demand, so you get low latency without burning a core, and it can also be a wakeup source from suspend.

**Q.** Who should start the M core — the bootloader or Linux?

**A.** 요구에 따라 다르다. M 코어가 Linux 부팅 전부터 일해야 하면(early CAN, 후방 카메라, 모터 안전 상태) 부트로더가 띄우고 Linux remoteproc은 detached 상태로 attach만 한다. 그렇지 않으면 Linux가 `/lib/firmware`에서 로드하는 게 OTA가 단순하다. attach하려면 "이미 돌고 있음"을 하드웨어 상태로 남겨야 한다. STM32MP1은 TAMP 레지스터, i.MX는 SMC나 리셋 레지스터로 감지한다.

> It depends on when the M core must be useful. If it must answer CAN or hold motors safe before Linux is up, the bootloader starts it and the Linux remoteproc driver probes in the detached state and just attaches. Otherwise Linux loads it from /lib/firmware, which keeps OTA simple. For attach to work, the running state must be visible in hardware — STM32MP1 reads a TAMP backup register, and i.MX detects it via an SMC call or the reset registers.

**Q.** The M core firmware hangs. What happens to Linux, and what happens if Linux panics?

**A.** M 코어가 죽으면 Linux는 계속 돌고, 워치독 IRQ나 heartbeat 정지로 감지해 remoteproc recovery를 건다. 실습에서도 상대 프로세스를 죽였더니 Linux 쪽이 1초 타임아웃으로 감지하고 살아 있었다. Linux가 패닉하면 리셋 도메인 설계에 달렸다. 분리돼 있으면 M 코어가 Linux heartbeat 끊김을 보고 안전 정지한다. DDR·클럭·전원 레일을 공유하면 같이 죽을 수 있다는 게 같은 칩의 한계다.

> If the M core hangs, Linux keeps running; a watchdog interrupt or a stalled heartbeat counter triggers rproc_report_crash and recovery. In my lab, killing the remote side was detected by a one-second timeout on the Linux side, and Linux stayed up. If Linux panics, it depends on the reset domains: if they're separate, the M core sees the Linux heartbeat stop and brings the actuators to a safe state. Shared DDR, clocks or power rails can still take both down, which is the main limit of on-die AMP.

**Q.** How do you stop Linux from breaking peripherals the M core owns?

**A.** 세 층이다. 약속: Linux DT에서 그 장치와 핀을 `disabled`로. 중재: 클럭·전원·리셋처럼 공유되는 건 관리 코어(TI-SCI, BPMP, SCMI)에 요청하거나 hwspinlock. 강제: RDC, ETZPC/RIFSC, TZASC, SMMU로 버스 수준 차단. 실제로 제일 많이 만나는 건 Linux가 사용되지 않는 클럭을 끄는 `clk_disable_unused`다.

> Three layers. By convention: disable the device and its pins in the Linux device tree. By arbitration: shared clocks, power and resets go through a system controller like TI-SCI, BPMP or SCMI, or are guarded by a hardware spinlock. By enforcement: the SoC firewall — RDC, ETZPC or RIFSC, TZASC, the SMMU — blocks access at the bus. The classic bug is clk_disable_unused turning off a clock only the M core uses, so I attach it to the remoteproc node or mark it critical.

---

## 16. 직접 해보기

1. (개념) i.MX93 EVK DT에서 resource table이 DDR이 아니라 TCM(`0x2021e000`)에 있다. 왜 그게 유리한지 "누가 먼저 부팅하나"와 연결해 설명하라.
정답/힌트: 부트로더가 M 코어를 먼저 띄우는 경우 Linux는 펌웨어 ELF를 다시 읽지 않고 attach하는데, 그때 리소스 테이블을 미리 알려진 고정 주소에서 찾아야 한다.

2. (DT) 예제 6의 `o1_rsv.dtsi`에 `o1-mcore-fw`와 **겹치는** 노드(`0x7e100000`, 2 MiB)를 하나 더 넣고 부팅해 dmesg를 보라. 커널은 겹침을 어떻게 보고하나?
정답/힌트: `dmesg | grep -i "reserved mem"`에서 overlap 경고를 찾는다. 경고 문구는 직접 확인해서 적을 것.

3. (코드) `o1_ring.h`의 두 번째 `o1_rmb()`(tail 쓰기 직전)를 지우고 `o1_ping`을 1,000,000번 돌려 보라. QEMU에서 오류가 나나? 안 난다면 그게 "안전하다"는 뜻인가?
정답/힌트: TCG + 호스트 M2에서 재현될 수도, 안 될 수도 있다. 재현 안 됨은 증거가 아니다 — §10.2의 load → store 순서 논리로 답한다.

4. (측정) `o1_ping`에 `sched_setscheduler(SCHED_FIFO)`와 `mlockall()`을 넣고 `poll_sleep=50`의 p99가 어떻게 바뀌는지 재라(N2). TCG라는 한계를 결과와 함께 적어라.
정답/힌트: 중앙값은 타이머 슬랙(`prctl(PR_SET_TIMERSLACK, 1)`)의 영향이 더 크다. 둘을 따로 바꿔 본다.

5. (관찰) 실습 2 게스트에서 `o1_ping`을 백그라운드로 돌리며 `cat /proc/<pid>/maps`로 `resource2` 매핑 줄을 찾고, `resource2_wc`로 바꿨을 때와 비교하라.
정답/힌트: 둘 다 `/sys/bus/pci/devices/.../resource2(_wc)` 파일 매핑으로 보인다. 속성 차이는 maps에 안 나오니 예제 15처럼 행동으로 구분한다.

6. (설계) Jetson Orin 기반 AMR에서 "비상 정지 버튼 → 모터 차단"을 Linux, SPE, 외부 MCU 중 어디에 둘지 표로 비교하고 하나를 골라라.
정답/힌트: N4의 판단 기준표 + 이 노트 §9의 공유 자원 장애 행. 안전 등급이 요구되면 외부 안전 MCU나 하드웨어 차단 경로.

7. (소스 읽기) `drivers/remoteproc/ti_k3_m4_remoteproc.c`에서 M4F를 시작·정지할 때 리셋을 어떻게 다루는지(직접 레지스터인지 TI-SCI인지) 찾아라.
정답/힌트: `ti_sci` 호출과 `reset_control_*` 호출을 grep한다. §2.3의 `ti,sci-dev-id` 속성과 연결된다.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| AMP | Asymmetric Multi-Processing | 코어마다 다른 OS/펌웨어가 독립적으로 도는 구성 |
| SMP | Symmetric Multi-Processing | 한 커널이 모든 코어를 스케줄링 (Linux A 클러스터) |
| 이종 SoC | heterogeneous SoC | 종류가 다른 코어(Cortex-A + M/R, DSP, NPU)가 한 칩에 |
| TCM | Tightly Coupled Memory | 코어 전용 1사이클 SRAM, 캐시 없음, 결정론적 |
| OCRAM | On-Chip RAM | 온칩 공유 SRAM (NXP 용어) |
| 캐브아웃 | carveout | DDR에서 잘라 다른 주인에게 준 고정 영역 |
| `no-map` | reserved-memory 속성 | 커널 선형 매핑에서 제외, memblock NOMAP |
| `shared-dma-pool` | reserved-memory compatible | 장치 전용 DMA 풀 (no-map이면 coherent 풀, reusable이면 CMA) |
| ATT | Address Translation Table | `imx_rproc`의 M 코어 주소 ↔ 시스템 주소 변환표 |
| MU | Messaging Unit | NXP 메일박스 하드웨어 |
| IPCC | Inter-Processor Communication Controller | ST 메일박스 하드웨어 (doorbell형) |
| HSP | Hardware Synchronization Primitives | Tegra의 메일박스·doorbell 블록 |
| BPMP | Boot and Power Management Processor | Tegra의 클럭·리셋·전원 관리 보조 코어 |
| SPE | Sensor Processing Engine | Tegra AON 클러스터의 보조 코어 (기억 기반) |
| TI-SCI | TI System Control Interface | K3 SoC에서 리셋·전원·클럭을 관리 코어에 요청하는 프로토콜 |
| RDC / TRDC | (Trusted) Resource Domain Controller | NXP 버스 마스터별 주변장치·메모리 접근 제어 |
| ETZPC / RIFSC | ST 접근 제어 블록 | STM32MP1 / MP2의 주변장치 방화벽 |
| attach / detached | remoteproc 상태 | 이미 돌고 있는 원격 코어에 Linux가 다시 로드 없이 붙는 것 |
| ivshmem | Inter-VM shared memory | QEMU PCI 장치, 호스트 파일을 BAR로 노출 |
| Device / Normal-NC | Arm 메모리 속성 | Device는 정렬 엄격·병합 금지, Normal Non-cacheable은 비정렬 허용 |
| heartbeat | 생존 신호 | 주기적으로 올라가는 카운터·메시지, 멈추면 상대 고장 |

---

## 18. 요약 & 체크리스트

이종 SoC는 Linux용 Cortex-A 클러스터 옆에 빠른 부팅·실시간·저전력·안전을 위한 Cortex-M/R 코어를 둔다. 커널 트리 DT만 봐도 i.MX93(M33 + MU + vring/rsc-table 캐브아웃), AM62x(M4F + R5F + mailbox + DDR 캐브아웃, TI-SCI로 리셋), Tegra234(BPMP = SYSRAM 창 + HSP doorbell)의 구조가 그대로 보인다. 메모리는 TCM(실시간 코드), OCRAM/SRAM, DDR 캐브아웃으로 나누고, Linux 쪽에서는 `reserved-memory` + `no-map`으로 "내 RAM이 아님"을 선언한다. 실습 1에서 이 선언이 memblock NOMAP·`/proc/iomem`·`MemTotal`·`/dev/mem` 판단에 반영되고 RAM을 다 써도 캐브아웃이 그대로임을 확인했으며, 선언이 없으면 기본 CMA가 바로 그 자리에 잡히는 것도 봤다. 두 코어는 보통 코히어런트하지 않으니 비캐시 매핑이나 clean/invalidate + barrier가 필요하고, 실습 2에서 Device 매핑의 정렬 함정(`BUS_ADRALN`)과 폴링의 지연·CPU 트레이드오프를 실측해 메일박스 인터럽트가 왜 필요한지 수치로 확인했다. 마지막으로 자원 소유권(약속·중재·강제), 부팅 순서(Linux 시작 vs 부트로더 시작 + attach), 장애 격리의 한계(공유 DDR·클럭·전원)를 설계 질문에 답할 언어로 정리했다.

- [ ] 이종 SoC에 보조 코어를 두는 네 가지 이유를 예와 함께 말할 수 있다
- [ ] i.MX93 / AM62x / Tegra234에서 보조 코어·메일박스·캐브아웃 노드를 커널 DT에서 찾아 읽을 수 있다
- [ ] TCM 별칭(M 코어 주소 vs 시스템 주소)이 왜 생기고 어디서 변환하는지 설명할 수 있다
- [ ] `reserved-memory` + `no-map` + `shared-dma-pool` 캐브아웃을 DT로 쓰고 `/proc/iomem`·memblock으로 검증할 수 있다
- [ ] 캐브아웃이 없을 때 CMA나 버디가 그 자리를 쓸 수 있음을 실험으로 보일 수 있다
- [ ] 코히어런트하지 않은 두 코어 사이의 clean/invalidate/barrier 순서를 그릴 수 있다
- [ ] Device 매핑과 Normal-NC 매핑의 차이와 `memcpy`/`memset`이 깨지는 이유를 설명할 수 있다
- [ ] SPSC 링 + doorbell 인덱스 프로토콜을 barrier까지 정확히 코드로 짤 수 있다
- [ ] 폴링 vs 메일박스 IRQ의 트레이드오프를 실측값으로 말할 수 있다
- [ ] 자원 분할의 세 층(DT 약속, 관리 코어 중재, 하드웨어 방화벽)과 `clk_disable_unused` 함정을 설명할 수 있다
- [ ] 부트로더 시작 + remoteproc attach가 필요한 경우와 감지 방법을 말할 수 있다
- [ ] 한쪽 코어가 죽었을 때의 영향과 감지·복구 경로, 그리고 on-die AMP 격리의 한계를 설명할 수 있다

## 참고 자료

- 커널 문서 — Reserved memory 바인딩: `Documentation/devicetree/bindings/reserved-memory/` (랩 소스 트리, 일부 스키마는 dt-schema 저장소로 이동)
- 커널 문서 — Remote Processor Framework: https://docs.kernel.org/staging/remoteproc.html
- 커널 문서 — The Common Mailbox Framework: https://docs.kernel.org/driver-api/mailbox.html
- 커널 문서 — The Userspace I/O HOWTO: https://docs.kernel.org/driver-api/uio-howto.html
- 커널 소스 (6.18.55, elixir.bootlin.com에서도 열람 가능): `drivers/remoteproc/imx_rproc.c`, `drivers/remoteproc/stm32_rproc.c`, `drivers/remoteproc/ti_k3_m4_remoteproc.c`, `drivers/remoteproc/ti_k3_r5_remoteproc.c`, `drivers/of/of_reserved_mem.c`, `kernel/dma/coherent.c`, `drivers/pci/mmap.c`, `drivers/uio/uio_pci_generic.c`
- 커널 DT: `arch/arm64/boot/dts/freescale/imx93.dtsi`, `imx93-11x11-evk.dts`, `arch/arm64/boot/dts/ti/k3-am62-mcu.dtsi`, `k3-am62-wakeup.dtsi`, `k3-am62-ti-ipc-firmware.dtsi`, `arch/arm64/boot/dts/nvidia/tegra234.dtsi`, `arch/arm/boot/dts/st/stm32mp151.dtsi`, `stm32mp15xx-dkx.dtsi`
- QEMU 문서 — ivshmem: https://www.qemu.org/docs/master/system/devices/ivshmem.html
- Arm Architecture Reference Manual for A-profile (메모리 속성, Device 메모리 정렬 규칙, DC ZVA) — developer.arm.com
- 선행·후속 노트: D4, D5, F9, F10, G5, H1, H4, H10, H11, M2, N2, N3, N4, N5, O2, O3, O5
