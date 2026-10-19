# S3. 실제 보드 bring-up — 보드 고르기, 사기 전에 VM에서 끝내 둘 것, 그리고 보드가 온 날의 순서

> **이 노트를 다 읽으면**: Raspberry Pi 5 · BeaglePlay · Jetson Orin Nano(+ i.MX8MP EVK · ROCK 5B)를 Don의 목표 기준으로 비교해 한 대를 고를 수 있다 · BeaglePlay용 TF-A · U-Boot(R5 + A53) · 커널 DTB · DT overlay · SD 카드 이미지를 보드 없이 직접 만들고 검사할 수 있다 · 보드가 도착한 날 콘솔 배선부터 부팅 시간 · GPIO 지연 측정까지 순서대로 진행할 수 있다
> **왜 필요한가**: "실제 보드에서 부트로더와 커널을 직접 올려 봤나요?"는 Embedded Linux 면접의 단골 질문이고, GitHub에 "내가 만든 이미지로 부팅한 로그"가 있으면 답이 달라진다
> **Don 기준 난이도**: 새 실리콘 bring-up · 측정 장비 · 버스 디버그는 이미 익숙 / 새로 배울 것은 K3 멀티 코어 부트 체인, binman, U-Boot bootstd, SD 이미지 레이아웃
> **선행 노트**: C1–C5(부트 체인), C8(Jetson UEFI), D5 · D6(DT 파일 · overlay), B1 · B5(툴체인), K1 · K2(rootfs), M10 · M11(부팅 분석 · HW 연계 측정), O5(SoC 지형 · mainline 매트릭스), P1 · P4(bring-up 순서 · 회로도→DT), S1(QEMU 랩)

---

## 0. 큰 그림 — 보드가 없어도 bring-up의 70%는 지금 할 수 있다

Don은 지금 랩에 보드가 없다. 그래서 이 노트는 두 부분으로 나뉜다.

1. **지금 VM에서 실제로 실행한 것** — TF-A BL31 빌드, BeaglePlay용 U-Boot R5 SPL과 A53 SPL/U-Boot 빌드, 후보 보드 5종 DTB 빌드와 노드 검사, BMI160 overlay 적용, SD 카드 이미지 **파일** 생성, 그 이미지를 QEMU의 U-Boot로 읽어 bootflow · overlay를 검증하는 리허설, pre-flight 체크리스트. 출력은 전부 실제 실행 결과다.
2. **보드가 오면 하는 것** — 콘솔 배선, stock 이미지로 golden log 확보, 우리 이미지로 SD 부팅, JTAG, 로직 분석기 측정. 이 단계들은 전부 **"가이드(실행 안 함 — 보드 필요)"** 라벨을 붙였다.

SSD 펌웨어 시절로 치면, 실리콘이 오기 전에 FPGA/시뮬레이터에서 펌웨어 이미지 빌드 · 이미지 포맷 검증 · 플래싱 스크립트를 끝내 두는 것과 같다. 실리콘이 도착한 날 남는 일은 "전원 · 콘솔 · 첫 배너"뿐이어야 한다(P1 §0의 "믿을 수 있는 영역을 한 칸씩 넓히기").

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" markerWidth="10" markerHeight="10" refX="9" refY="5" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#888"/></marker></defs>
<text x="20" y="22" font-size="14">지금 VM에서 (실행함)</text>
<text x="370" y="22" font-size="14">보드가 오면 (가이드 — 보드 필요)</text>
<rect x="20" y="34" width="290" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="32" y="54" font-size="13">TF-A BL31 빌드 (PLAT=k3, lite)</text>
<rect x="20" y="70" width="290" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="32" y="90" font-size="13">U-Boot R5 SPL + A53 빌드</text>
<rect x="20" y="106" width="290" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="32" y="126" font-size="13">후보 보드 DTB 5종 빌드 · 검사</text>
<rect x="20" y="142" width="290" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="32" y="162" font-size="13">BMI160 overlay 컴파일 · 적용</text>
<rect x="20" y="178" width="290" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="32" y="198" font-size="13">SD 이미지 파일 (sfdisk)</text>
<rect x="20" y="214" width="290" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="32" y="234" font-size="13">QEMU 리허설 (bootflow · 타이밍)</text>
<rect x="20" y="250" width="290" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="32" y="270" font-size="13">pre-flight 체크리스트</text>
<rect x="370" y="34" width="290" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="382" y="54" font-size="13">USB-UART 3.3 V 콘솔 배선</text>
<rect x="370" y="70" width="290" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="382" y="90" font-size="13">stock 이미지 부팅 · golden log</text>
<rect x="370" y="106" width="290" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="382" y="126" font-size="13">SD 부팅 (USR 버튼 누른 채 전원)</text>
<rect x="370" y="142" width="290" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="382" y="162" font-size="13">R5 SPL → U-Boot → 커널 → 셸</text>
<rect x="370" y="178" width="290" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="382" y="198" font-size="13">BMI160 probe → S4 드라이버</text>
<rect x="370" y="214" width="290" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="382" y="234" font-size="13">JTAG (OpenOCD, TC2050)</text>
<rect x="370" y="250" width="290" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="382" y="270" font-size="13">로직 분석기: 부팅 · GPIO 지연</text>
<line x1="312" y1="193" x2="366" y2="121" stroke="#888" stroke-width="2" marker-end="url(#ah1)"/>
<text x="20" y="308" font-size="13">연결고리: 같은 SD 이미지 파일을 카드에 dd 한다 — 이미지가 두 세계의 인터페이스</text>
</svg>
```
그림 1 — 이 프로젝트의 두 절반. 초록은 이 노트에서 실제로 돌린 단계, 주황은 보드가 와야 하는 단계.

### 0.1 최종 산출물 (GitHub에 올릴 것)

- `board-bringup/` 저장소: 빌드 스크립트 9개, DTB 검사 툴, overlay, SD 레이아웃, pre-flight, GPIO 측정 툴 (지금 `~/.elinux_scratch/S3/board-bringup/`에 있다)
- 보드 도착 후: 부팅 로그(golden vs mainline), 부팅 단계별 시간 표, GPIO 토글 지연 히스토그램, LA 캡처 스크린샷
- README 한 장: "BeaglePlay를 TI SDK 없이 mainline TF-A + U-Boot + Linux 6.18로 부팅" (13절)

### 0.2 실행 환경

| 항목 | 값 |
|---|---|
| VM | Lima `elinux`, Ubuntu 26.04 arm64, `uname -r` = `7.0.0-38-generic`, 6 CPU (빌드는 `-j2`) |
| 소스 | `/var/lab/src`: U-Boot v2026.10, TF-A v2.15, Linux 6.18.55 (읽기 전용, 전부 `O=`/`BUILD_BASE=`로 밖에 빌드) |
| 툴체인 | A53: VM 기본 gcc 15 (arm64 호스트라 native = "cross"), R5: `arm-none-eabi-gcc` 14.2.1 |
| 산출물 | `/var/tmp/S3/` (331 MB) |
| 프로젝트 소스 | `~/.elinux_scratch/S3/board-bringup/` |

x86 PC에서 같은 스크립트를 돌리려면 `CC64=aarch64-linux-gnu-`만 주면 된다(B1, B5).

---

## 1. 보드 비교 — 무엇을 기준으로 고르나

### 1.1 Don의 기준 6개

Don의 목표는 (1) Embedded Linux · 드라이버 면접 통과, (2) 국방 로보틱스(Scout AI) · 드론(Neros) · 자동차 실리콘(Rivian) · AI 가속기 쪽 직무, (3) GitHub 포트폴리오(S4 IIO 드라이버가 핵심)다. 그래서 기준은:

| 기준 | 왜 중요한가 | 어디서 확인했나 |
|---|---|---|
| mainline 친화도 | 벤더 BSP 없이 U-Boot · 커널을 직접 빌드해야 "내가 했다"가 된다 (P2, P5) | O5 §4.2 예제 8 실측 점수 + 이 노트 3 · 4절 빌드 |
| 문서 | TRM · 회로도가 공개돼야 P3 · P4 루틴(데이터시트→드라이버, 회로도→DT)을 연습한다 | 기억 기반 + U-Boot 트리 문서 존재 여부(5절) |
| UART · JTAG 접근 | 콘솔 없으면 bring-up 불가, JTAG는 SPL 단계 디버그 (M4) | 이 노트 4절 DTB 검사 + U-Boot 문서 |
| 가격 | 보드 + 액세서리 총액 | **기억 기반 — 확인 필요** |
| 카메라 · CAN | Q1 · Q2 · Q4 직무 키워드, S7 | DTB 노드 검사(4절) |
| 직무 연관성 | 면접에서 "그 SoC 써 봤다"가 먹히는가 | O5 §3 패밀리별 상세 |

### 1.2 후보 5종 사실표

| 보드 | SoC · 코어 | mainline 점수 (O5 실측) | 콘솔 (DTB 실측) | 카메라 · CAN | 부트 체인 | 가격 (기억 기반 — 확인 필요) |
|---|---|---|---|---|---|---|
| BeaglePlay | TI AM625, 4x A53 + R5F + M4F | 5/7 | `serial2` = main_uart0 @0x2800000, 115200 | CSI-2 1 (overlay로 OV5640), MCAN 3개는 SoC에 있으나 DT에서 disabled · 트랜시버 없음 | ROM(R5) → R5 SPL → TF-A → OP-TEE → A53 SPL → U-Boot, **전부 mainline** | 약 $100 |
| Raspberry Pi 5 | BCM2712, 4x A76 + RP1 | 2/7 | `serial10` = serial@7d001000 (3핀 디버그 UART), 115200 | CSI 2개(RP1 CFE)지만 mainline DT에 노드 없음, CAN은 SPI HAT | 비공개 EEPROM 펌웨어 + `config.txt` → (U-Boot 선택) → Linux | 4GB 약 $60~85 |
| Jetson Orin Nano Dev Kit | Tegra234, 6x A78AE + Ampere GPU | 1/7 | `serial0` = `/serial` (TCU, Tegra Combined UART) | CSI 많음 · MTTCAN 있으나 mainline DT에 CSI · CAN 노드 0개 | BootROM → MB1/MB2 → UEFI → L4TLauncher → L4T 커널 | 약 $249 (Super) |
| i.MX8MP EVK | NXP i.MX8M Plus, 4x A53 + M7 + NPU | 6/7 | serial@30890000, 115200 | ISP 2 + CSI 2, FlexCAN 1개 enabled | ROM → U-Boot SPL(DDR 학습 블롭) → TF-A → U-Boot | 약 $600 이상 |
| ROCK 5B | RK3588, 4x A76 + 4x A55 + NPU | 2/7 | `serial2` = serial@feb50000, **1500000 baud** | CSI 노드 disabled, CAN 노드 없음 | ROM → TPL(rkbin DDR 블롭) → SPL → TF-A → U-Boot | 약 $130~200 |

"DTB 실측" 열은 4절 예제 6 · 7의 실제 출력이다. ROCK 5B의 1.5 Mbaud 콘솔은 DTB를 보고 처음 알게 되는 종류의 사실이다 — 싸구려 USB-UART(CH340 일부)는 1.5 Mbaud가 불안정하다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="168" y="26" font-size="12">mainline</text>
<text x="246" y="26" font-size="12">문서</text>
<text x="314" y="26" font-size="12">디버그</text>
<text x="390" y="26" font-size="12">가격</text>
<text x="450" y="26" font-size="12">카메라·CAN</text>
<text x="528" y="26" font-size="12">직무 연관</text>
<text x="612" y="26" font-size="12">합계</text>
<text x="10" y="60" font-size="13">BeaglePlay</text>
<rect x="160" y="40" width="68" height="32" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/><text x="190" y="61" font-size="13">5</text>
<rect x="232" y="40" width="68" height="32" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/><text x="262" y="61" font-size="13">5</text>
<rect x="304" y="40" width="68" height="32" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/><text x="334" y="61" font-size="13">5</text>
<rect x="376" y="40" width="68" height="32" fill="#3f9a6b" fill-opacity="0.48" stroke="#3f9a6b"/><text x="406" y="61" font-size="13">4</text>
<rect x="448" y="40" width="68" height="32" fill="#3f9a6b" fill-opacity="0.36" stroke="#3f9a6b"/><text x="478" y="61" font-size="13">3</text>
<rect x="520" y="40" width="68" height="32" fill="#3f9a6b" fill-opacity="0.48" stroke="#3f9a6b"/><text x="550" y="61" font-size="13">4</text>
<text x="612" y="61" font-size="14">26</text>
<text x="10" y="104" font-size="13">i.MX8MP EVK</text>
<rect x="160" y="84" width="68" height="32" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/><text x="190" y="105" font-size="13">5</text>
<rect x="232" y="84" width="68" height="32" fill="#3f9a6b" fill-opacity="0.48" stroke="#3f9a6b"/><text x="262" y="105" font-size="13">4</text>
<rect x="304" y="84" width="68" height="32" fill="#3f9a6b" fill-opacity="0.48" stroke="#3f9a6b"/><text x="334" y="105" font-size="13">4</text>
<rect x="376" y="84" width="68" height="32" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="406" y="105" font-size="13">1</text>
<rect x="448" y="84" width="68" height="32" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/><text x="478" y="105" font-size="13">5</text>
<rect x="520" y="84" width="68" height="32" fill="#3f9a6b" fill-opacity="0.48" stroke="#3f9a6b"/><text x="550" y="105" font-size="13">4</text>
<text x="612" y="105" font-size="14">23</text>
<text x="10" y="148" font-size="13">Raspberry Pi 5</text>
<rect x="160" y="128" width="68" height="32" fill="#3f9a6b" fill-opacity="0.24" stroke="#3f9a6b"/><text x="190" y="149" font-size="13">2</text>
<rect x="232" y="128" width="68" height="32" fill="#3f9a6b" fill-opacity="0.36" stroke="#3f9a6b"/><text x="262" y="149" font-size="13">3</text>
<rect x="304" y="128" width="68" height="32" fill="#3f9a6b" fill-opacity="0.36" stroke="#3f9a6b"/><text x="334" y="149" font-size="13">3</text>
<rect x="376" y="128" width="68" height="32" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/><text x="406" y="149" font-size="13">5</text>
<rect x="448" y="128" width="68" height="32" fill="#3f9a6b" fill-opacity="0.48" stroke="#3f9a6b"/><text x="478" y="149" font-size="13">4</text>
<rect x="520" y="128" width="68" height="32" fill="#3f9a6b" fill-opacity="0.36" stroke="#3f9a6b"/><text x="550" y="149" font-size="13">3</text>
<text x="612" y="149" font-size="14">20</text>
<text x="10" y="192" font-size="13">Jetson Orin Nano</text>
<rect x="160" y="172" width="68" height="32" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="190" y="193" font-size="13">1</text>
<rect x="232" y="172" width="68" height="32" fill="#3f9a6b" fill-opacity="0.36" stroke="#3f9a6b"/><text x="262" y="193" font-size="13">3</text>
<rect x="304" y="172" width="68" height="32" fill="#3f9a6b" fill-opacity="0.24" stroke="#3f9a6b"/><text x="334" y="193" font-size="13">2</text>
<rect x="376" y="172" width="68" height="32" fill="#3f9a6b" fill-opacity="0.24" stroke="#3f9a6b"/><text x="406" y="193" font-size="13">2</text>
<rect x="448" y="172" width="68" height="32" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/><text x="478" y="193" font-size="13">5</text>
<rect x="520" y="172" width="68" height="32" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/><text x="550" y="193" font-size="13">5</text>
<text x="612" y="193" font-size="14">18</text>
<text x="10" y="236" font-size="13">ROCK 5B</text>
<rect x="160" y="216" width="68" height="32" fill="#3f9a6b" fill-opacity="0.24" stroke="#3f9a6b"/><text x="190" y="237" font-size="13">2</text>
<rect x="232" y="216" width="68" height="32" fill="#3f9a6b" fill-opacity="0.24" stroke="#3f9a6b"/><text x="262" y="237" font-size="13">2</text>
<rect x="304" y="216" width="68" height="32" fill="#3f9a6b" fill-opacity="0.36" stroke="#3f9a6b"/><text x="334" y="237" font-size="13">3</text>
<rect x="376" y="216" width="68" height="32" fill="#3f9a6b" fill-opacity="0.36" stroke="#3f9a6b"/><text x="406" y="237" font-size="13">3</text>
<rect x="448" y="216" width="68" height="32" fill="#3f9a6b" fill-opacity="0.24" stroke="#3f9a6b"/><text x="478" y="237" font-size="13">2</text>
<rect x="520" y="216" width="68" height="32" fill="#3f9a6b" fill-opacity="0.36" stroke="#3f9a6b"/><text x="550" y="237" font-size="13">3</text>
<text x="612" y="237" font-size="14">15</text>
<text x="10" y="274" font-size="12">1~5점, 진할수록 좋음. mainline은 O5 실측 점수를 5점 척도로 환산, 나머지는 이 노트의 판단(주관).</text>
<text x="10" y="292" font-size="12">가격 점수는 기억 기반 가격 기준 — 구매 전 확인 필요.</text>
</svg>
```
그림 2 — Don의 목표 기준 보드 비교. 합계가 BeaglePlay > i.MX8MP EVK > RPi 5 > Jetson > ROCK 5B 순.

### 1.3 추천: BeaglePlay를 1번 보드로

**결론: BeaglePlay(AM625)를 먼저 산다. AI 쪽 직무가 본격화되면 두 번째로 Jetson Orin Nano를 산다.**

이유:

1. **부트 체인 전체가 mainline으로 빌드된다 — 이 노트에서 실제로 증명했다.** TF-A(`PLAT=k3 TARGET_BOARD=lite`), U-Boot(`am62x_beagleplay_r5_defconfig`, `am62x_beagleplay_a53_defconfig`), 커널 DTB(`ti/k3-am625-beagleplay.dtb`)가 모두 랩 트리에 있고 VM에서 빌드됐다(3 · 4절). 닫힌 것은 TI 펌웨어 블롭 3개(TIFS, DM, TIFS stub)뿐이고, 이름까지 U-Boot 트리에 적혀 있다(5절).
2. **멀티 코어 부트가 "진짜 SoC"처럼 생겼다.** ROM이 R5에서 돌고, R5 SPL이 DDR을 켠 뒤 A53을 깨우고, R5는 DM 펌웨어로 남는다. Don이 Apple에서 보는 "보조 코어가 메인 코어를 깨우는" 실리콘 bring-up과 같은 구조라 면접 스토리가 된다(O1, O2). RPi 5는 이 부분이 비공개 펌웨어 안에 숨어 있다.
3. **디버그 접근이 문서화돼 있다.** U-Boot 문서가 직접 "Serial Console is critical", "JTAG is useful" 이라고 쓰고, Tag-Connect TC2050 + OpenOCD 설정까지 준다(6.5절). USR 버튼으로 SD 부팅을 강제할 수 있어 "벽돌"이 되지 않는다.
4. **S4와 바로 연결된다.** mikroBUS · Grove · Qwiic 헤더가 각각 I2C 버스에 연결돼 있어(4.3절) BMI160 같은 IMU를 납땜 없이 붙일 수 있다. S4의 overlay를 이 노트 6절에서 이미 BeaglePlay DTB에 적용해 봤다.
5. **TI K3는 산업 · 국방 · 자동차 계열 SoC다.** AM62/AM64/TDA4(J7)가 같은 K3 구조라, 한 번 익히면 Jacinto(자동차 ADAS)까지 이어진다(O5 §3.2).
6. **Mac 호스트와 맞는다.** 빌드가 전부 arm64 VM에서 돈다. Jetson은 플래싱 도구(SDK Manager, `flash.sh`)가 x86_64 Ubuntu 호스트를 전제로 해서(기억 기반 — 확인 필요) M2 Mac만 가진 Don에게는 장벽이 크다.

약점도 정직하게: AM625에는 NPU가 없고(AI는 Jetson 몫), CAN 트랜시버가 보드에 없다(mikroBUS용 MCP2518FD CAN click 같은 모듈이 필요 — S7). 카메라는 CSI 커넥터 1개 + overlay(`k3-am625-beagleplay-csi2-ov5640.dtbo`)다.

### 1.4 다른 보드를 고를 때 달라지는 점 (요약)

| 보드 | 언제 고르나 | 이 노트 절차에서 바뀌는 곳 |
|---|---|---|
| Raspberry Pi 5 | 카메라 · libcamera 데모, 가장 싼 시작 | U-Boot는 `rpi_arm64_defconfig`(실제 빌드함, 3.4절), TF-A · SPL 없음, `config.txt`에 `kernel=u-boot.bin` (기억 기반 — 확인 필요). DT는 펌웨어가 넘김(`CONFIG_OF_BOARD=y` 실측) |
| Jetson Orin Nano | AI 추론 · ROS 2 · 로봇 데모, Scout · Neros 직무 | U-Boot 없음(UEFI). L4T BSP 커널을 써야 GPU · CSI · CAN이 동작. mainline 6.18 DT는 CSI · CAN 노드 0개(4.2절 실측) |
| i.MX8MP EVK | 회사 지원 · 예산이 있을 때, NPU + ISP + CAN-FD 모두 mainline | U-Boot `imx8mp_evk_defconfig`, TF-A `PLAT=imx8mp`, NXP `firmware-imx`의 LPDDR4 학습 블롭 4개(5절 실측) |
| ROCK 5B | RK3588 NPU · 성능, 커뮤니티 mainline 추적 | U-Boot `rock5b-rk3588_defconfig`, rkbin의 DDR TPL 블롭 + BL31, 콘솔 1.5 Mbaud |

---

## 2. 프로젝트 레이아웃과 준비물

### 2.1 저장소 구조

```
board-bringup/
  scripts/01_build_tfa_k3.sh         TF-A BL31 (k3/lite)
  scripts/02_build_uboot_beagleplay.sh   R5 SPL + A53 SPL/U-Boot
  scripts/03_build_dtbs.sh           candidate board DTBs only
  scripts/04_blob_check.sh           firmware blob names from U-Boot docs
  scripts/05_make_sd_image.sh        SD card IMAGE FILE (sfdisk + mtools + mkfs.ext4 -d)
  scripts/06_uboot_preflight.exp     QEMU U-Boot reads the image: bootflow + fdt apply
  scripts/07_qemu_gpio_toggle.sh     gpio_toggle on gpio-sim (QEMU guest)
  scripts/08_qemu_rehearsal.sh       same layout booted end-to-end, timestamped
  scripts/09_preflight.sh            PASS/WARN/FAIL checklist
  scripts/s3_gpio_sim.sh             runs inside the guest
  tools/dtb_inventory.py  tools/ts_capture.py  tools/boot_time.py
  src/gpio_toggle.c                  GPIO v2 uAPI toggle + timing
  overlays/s3-bmi160-mikrobus.dtso   first DT overlay (-> S4)
  layout/beagleplay-sd.sfdisk  layout/extlinux.conf  layout/extlinux-qemu.conf
  configs/s3_beagleplay.config       kernel config fragment
  logs/                              real outputs saved from the VM
```

### 2.2 쇼핑 리스트 (가격은 전부 기억 기반 — 확인 필요)

| 품목 | 권장 사양 | 왜 | 대략 가격 |
|---|---|---|---|
| BeaglePlay | 본체 | 1번 보드 | 약 $100 |
| USB-UART 어댑터 | **3.3 V TTL**, FTDI FT232R 계열 또는 CP2102, 점퍼선 포함 | 콘솔. 5 V 어댑터나 RS-232 레벨은 SoC 핀을 망가뜨린다 | $10~20 |
| 전원 | USB-C 5 V, 3 A 이상 (BeaglePlay 문서로 확인) | 전원 부족 = 랜덤 리셋 | $10~20 |
| microSD | 16~32 GB, A1 등급, 2장 | U-Boot 실험은 SD로 (문서 권장) | 장당 $8 |
| SD 리더 | USB, Mac에서 `/dev/rdiskN`으로 보이는 것 | 이미지 쓰기 | $10 |
| 로직 분석기 | 8채널 24 MHz FX2 계열(sigrok/PulseView) 또는 Saleae Logic 8 | GPIO 지연 · I2C 디코드 · 부팅 타이밍 | $15 / 약 $500 |
| (선택) JTAG | FT2232H 계열(TUMPA 등, OpenOCD 지원) 또는 TI XDS110 + **TC2050-IDC-NL** 케이블 + 리테이닝 클립 | SPL 단계 디버그 (U-Boot 문서 6.5절) | $30~100 + 케이블 $40 |
| (선택) USB-SD-Mux 또는 SDWire | SD를 호스트 ↔ 보드로 전환 | CI · 반복 부팅 자동화 (P6) | $50~100 |
| (선택) IMU 모듈 | BMI160 또는 같은 계열 I2C 브레이크아웃, Qwiic/Grove 케이블 | S4 | $10~20 |
| (선택) CAN | mikroBUS용 MCP2518FD click 같은 모듈 + 트랜시버 | S7, Q4 | $20~40 |

RPi 5라면 USB-UART 대신 3핀 JST-SH 디버그 커넥터에 맞는 **Raspberry Pi Debug Probe**(UART + SWD, 약 $12, 기억 기반), 공식 27 W(5 V 5 A) 전원이 필요하다. Jetson Orin Nano는 전원 어댑터가 포함돼 있고, 디버그 UART는 보드 헤더에 있다(핀 위치는 기억 기반 — 캐리어 보드 문서로 확인 필요).

### 2.3 호스트 준비

- VM: 이 노트의 스크립트가 쓰는 것 — `arm-none-eabi-gcc`, `dtc`, `fdtget`, `fdtoverlay`, `sfdisk`, `mtools`, `mkfs.ext4`, `fakeroot`, `expect`, `qemu-system-aarch64`. 전부 VM에 이미 있다.
- binman은 Python 모듈 `yamllint`가 필요하다(VM에 없었다). 전역 설치 대신 `/var/tmp/S3/venv`에 venv를 만들었다(3.2절).
- Mac: 콘솔 터미널 `picocom` 또는 `screen`, 이미지 쓰기는 `diskutil` + `dd`. 9절 pre-flight가 `dfu-util`, `openocd`, `sigrok-cli`, `pulseview`가 없다고 WARN을 낸다 — 보드 오기 전에 설치할 목록이다.

---

## 3. 단계 1~3 — 부트로더를 직접 빌드한다 (VM에서 실행함)

### 3.1 단계 1: TF-A BL31 (AM62x)

AM625의 A53에서 처음 도는 코드는 TF-A BL31이다(EL3 런타임, PSCI — C5). TI K3는 BL1/BL2를 쓰지 않는다: 그 역할은 ROM과 R5 SPL이 한다. 그래서 `bl31` 타깃 하나만 빌드한다.

```sh
#!/bin/bash
# 01_build_tfa_k3.sh: TF-A BL31 for TI K3 AM62x (TARGET_BOARD=lite) -> bl31.bin
# usage: 01_build_tfa_k3.sh [tfa_src] [out_dir]   (runs on an arm64 host: native gcc is the "cross" compiler)
set -euo pipefail
TFA=${1:-/var/lab/src/tfa}
OUT=${2:-/var/tmp/S3/tfa-k3}
CC64=${CC64:-}                       # empty on an arm64 VM; aarch64-linux-gnu- on x86
mkdir -p "$OUT"
make -C "$TFA" -j2 CROSS_COMPILE=$CC64 ARCH=aarch64 PLAT=k3 TARGET_BOARD=lite \
     SPD=opteed BUILD_BASE="$OUT" bl31 > "$OUT/build.log" 2>&1 \
  || { tail -20 "$OUT/build.log"; exit 1; }
ls -l "$OUT"/k3/lite/release/bl31.bin
```

```sh
limactl shell elinux -- bash -lc 'time bash ~/.elinux_scratch/S3/board-bringup/scripts/01_build_tfa_k3.sh'
```
(실제로는 Mac 경로 `/Users/donh/.elinux_scratch/...`로 실행했다.)

```text
-rw-rw-r-- 1 donh donh 43776 Oct 18 16:30 /var/tmp/S3/tfa-k3/k3/lite/release/bl31.bin

real	0m1.967s
user	0m2.290s
sys	0m0.772s
  OD      /var/tmp/S3/tfa-k3/k3/lite/release/bl31/bl31.dump

Built /var/tmp/S3/tfa-k3/k3/lite/release/bl31.bin successfully
```
출력에서 볼 것: 43,776바이트. `BUILD_BASE=`로 산출물을 소스 밖에 두었다 — 공용 `/var/lab/src/tfa`에는 아무것도 생기지 않는다. `SPD=opteed`는 BL31이 OP-TEE를 BL32로 디스패치하겠다는 뜻이다(U-Boot 문서의 명령 그대로).

**체크포인트**: 이 단계가 끝나면 `bl31.bin`(약 43 KB)이 보여야 한다.

**흔한 실패**: `TARGET_BOARD`를 빼면 `generic`(J7 계열 메모리 맵)로 빌드돼 AM62에서 BL31이 엉뚱한 주소에 있다. Sitara(`am6x`)는 `lite`다(U-Boot `k3.rst`).

### 3.2 단계 2: U-Boot — R5 SPL(tiboot3.bin)과 A53(tispl.bin, u-boot.img)

K3의 U-Boot는 **두 번** 빌드한다. 32비트 R5용 하나, 64비트 A53용 하나. defconfig 이름은 U-Boot 트리 `configs/`에서 확인했다:

```text
am62x_beagleplay_a53_defconfig
am62x_beagleplay_r5_defconfig
```

```sh
#!/bin/bash
# 02_build_uboot_beagleplay.sh: R5 SPL (tiboot3.bin) + A53 SPL/U-Boot (tispl.bin, u-boot.img)
# usage: 02_build_uboot_beagleplay.sh <bl31.bin> [tee.bin] ; FW=path/to/ti-linux-firmware (optional)
set -euo pipefail
UB=${UB:-/var/lab/src/u-boot}
OUT=${OUT:-/var/tmp/S3/uboot}
BL31=$1
TEE=${2:-}                            # OP-TEE tee-raw.bin; empty -> binman fakes it
FW=${FW:-/var/tmp/S3/no-ti-firmware}  # real board: clone ti-linux-firmware here
CC32=${CC32:-arm-none-eabi-}
CC64=${CC64:-}
mkdir -p "$OUT/r5" "$OUT/a53" "$FW"
# missing blobs -> binman writes zero-filled fakes and lists them (NOT bootable)
MISS=BINMAN_ALLOW_MISSING=1
make -C "$UB" O="$OUT/r5" am62x_beagleplay_r5_defconfig > /dev/null
make -C "$UB" O="$OUT/r5" -j2 CROSS_COMPILE=$CC32 BINMAN_INDIRS="$FW" $MISS \
     > "$OUT/r5/build.log" 2>&1 || { tail -30 "$OUT/r5/build.log"; exit 1; }
make -C "$UB" O="$OUT/a53" am62x_beagleplay_a53_defconfig > /dev/null
make -C "$UB" O="$OUT/a53" -j2 CROSS_COMPILE=$CC64 BINMAN_INDIRS="$FW" $MISS \
     BL31="$BL31" ${TEE:+TEE="$TEE"} \
     > "$OUT/a53/build.log" 2>&1 || { tail -30 "$OUT/a53/build.log"; exit 1; }
ls -l "$OUT"/r5/tiboot3*.bin "$OUT"/a53/tispl.bin "$OUT"/a53/u-boot.img
```

첫 실행은 실패했다 — 실제 bring-up에서 흔한 종류의 실패라 그대로 남긴다:

```text
  BINMAN  .binman_stamp
binman: Unknown entry type 'ti-board-config' in node '/binman/board-cfg/ti-board-config' (expected etype/ti_board_config.py, error 'No module named 'yamllint''
make[1]: *** [/var/lab/src/u-boot/Makefile:1410: .binman_stamp] Error 1
```
binman의 `ti-board-config` etype은 TI 보드 설정 YAML(리소스 관리 · 보안 · PM 설정)을 바이너리로 바꾸는데, 그 검증에 `yamllint`를 쓴다. 전역 pip 설치 대신 자기 디렉터리에 venv를 만든다:

```sh
python3 -m venv --system-site-packages /var/tmp/S3/venv
/var/tmp/S3/venv/bin/pip install -q yamllint
export PATH=/var/tmp/S3/venv/bin:$PATH
bash ~/.elinux_scratch/S3/board-bringup/scripts/02_build_uboot_beagleplay.sh /var/tmp/S3/tfa-k3/k3/lite/release/bl31.bin
```

```text
lrwxrwxrwx 1 donh donh      20 Oct 18 16:33 /var/tmp/S3/uboot/a53/tispl.bin -> ./tispl.bin_unsigned
-rw-rw-r-- 1 donh donh 1201424 Oct 18 16:32 /var/tmp/S3/uboot/a53/u-boot.img
-rw-rw-r-- 1 donh donh  128951 Oct 18 16:32 /var/tmp/S3/uboot/r5/tiboot3-am62x-gp-evm.bin
-rw-rw-r-- 1 donh donh  131459 Oct 18 16:32 /var/tmp/S3/uboot/r5/tiboot3-capsule.bin
lrwxrwxrwx 1 donh donh      26 Oct 18 16:32 /var/tmp/S3/uboot/r5/tiboot3.bin -> ./tiboot3-am62x-gp-evm.bin

real	1m26.306s
```
출력에서 볼 것: R5 + A53 두 빌드가 `-j2`로 1분 26초. `tiboot3.bin`이 **`gp`** 변형을 가리킨다 — BeaglePlay의 AM625가 GP(일반, 보안 키 미융착) 디바이스라는 뜻이다. TI EVM용 설정은 `hs-fs`/`hs` 변형도 함께 만든다. `-capsule.bin`은 UEFI capsule 업데이트용이다(L4).

**체크포인트**: `tiboot3.bin`(약 126 KB), `tispl.bin`(약 240 KB), `u-boot.img`(약 1.2 MB) 세 파일. 이 세 개가 SD 카드 BOOT 파티션에 들어갈 전부다(U-Boot 문서 "Target Images").

### 3.3 단계 2 계속: 빌드 결과를 해부한다 — 무엇이 진짜이고 무엇이 가짜인가

TI 펌웨어 블롭은 VM에서 받을 수 없었다(`git.ti.com` 접속이 75초 타임아웃, 2026-10-18). 그래서 `BINMAN_ALLOW_MISSING=1`로 빌드했고, binman은 빠진 블롭을 0바이트 가짜로 채우고 **정직하게 보고한다**:

```text
Image 'tiboot3-am62x-gp-evm.bin' is missing external blobs and is non-functional: ti-fs-gp.bin
Image 'tiboot3-am62x-gp-evm.bin' has faked external blobs and is non-functional: ti-fs-firmware-am62x-gp.bin
Image 'tifsstub-gp' is missing optional external blobs but is still functional: tifsstub-gp.bin
/binman/tifsstub-gp/tifsstub-gp.bin (ti-sysfw/ti-fs-stub-firmware-am62x-gp.bin):
Image 'ti-spl_unsigned' is missing external blobs and is non-functional: tee-os ti-dm
/binman/ti-spl_unsigned/fit/images/tee/tee-os (tee-raw.bin):
/binman/ti-spl_unsigned/fit/images/dm/ti-dm (ti-dm/am62xx/ipc_echo_testb_mcu1_0_release_strip.xer5f):
Some images are invalid
```
(R5 · A53 빌드 로그에서 핵심 줄만 모았다.)

binman이 쓰는 맵 파일로 이미지 안을 본다. 크기 0인 항목이 가짜 블롭이다:

```text
ImagePos    Offset      Size  Name
00000000  00000000  0001f7b7  tiboot3-am62x-gp-evm.bin
00000000   00000000  000004f1  ti-secure-rom
000004f1   000004f1  0001e6b8  u-boot-spl
0001eba9   0001eba9  00000000  ti-fs-gp.bin
0001eba9   0001eba9  000006cc  combined-tifs-cfg-gp.bin
0001f275   0001f275  00000542  combined-dm-cfg-gp.bin
```
출력에서 볼 것: `tiboot3.bin` = ROM이 검사하는 X.509 인증서(`ti-secure-rom`, 0x4f1) + R5 SPL(약 122 KB) + TIFS 펌웨어(지금 0바이트) + 보드 설정 두 개. C2 §3의 "ROM이 읽는 봉투"가 바로 이 인증서다.

`tispl.bin`은 FIT 이미지라 U-Boot의 `mkimage -l`로 읽힌다:

```sh
/var/tmp/S3/uboot/a53/tools/mkimage -l /var/tmp/S3/uboot/a53/tispl.bin_unsigned
```
```text
FIT description: Configuration to load ATF and SPL
 Image 0 (atf)
  Description:  ARM Trusted Firmware
  Data Size:    43776 Bytes = 42.75 KiB = 0.04 MiB
  Load Address: 0x80000000
 Image 1 (tee)
  Description:  OP-TEE
  Data Size:    0 Bytes = 0.00 KiB = 0.00 MiB
 Image 2 (tifsstub-gp)
  Data Size:    970 Bytes = 0.95 KiB = 0.00 MiB
  Load Address: 0x9dc00000
 Image 3 (dm)
  Description:  DM binary
  Data Size:    0 Bytes = 0.00 KiB = 0.00 MiB
  Load Address: 0x89000000
 Image 4 (spl)
  Description:  SPL (64-bit)
  Data Size:    183760 Bytes = 179.45 KiB = 0.18 MiB
  Load Address: 0x80080000
  Entry Point:  0x80080000
 Image 5 (fdt-0)
  Description:  k3-am625-beagleplay
 Configuration 0 (conf-0)
  Firmware:     atf
  FDT:          fdt-0
  Loadables:    tee
                dm
                spl
```
(Created · Compression 등 반복 줄은 생략했다.)

출력에서 볼 것: 우리가 빌드한 BL31(43,776바이트 — 3.1절과 같은 크기)이 DDR 0x80000000에, A53 SPL이 0x80080000에 들어간다. OP-TEE와 DM은 0바이트다. 이 FIT 하나가 "R5 SPL이 A53 세계를 차리기 위해 로드하는 모든 것"의 목록이다.

설정에서 확인한 주소 · 이름(실측):

```text
r5/.config:CONFIG_CPU_V7R=y
r5/.config:CONFIG_DEFAULT_DEVICE_TREE="k3-am625-r5-beagleplay"
r5/.config:CONFIG_SPL_TEXT_BASE=0x43c00000
r5/.config:CONFIG_SPL_FS_LOAD_PAYLOAD_NAME="tispl.bin"
r5/.config:CONFIG_SYS_MMCSD_FS_BOOT_PARTITION=1
a53/.config:CONFIG_DEFAULT_DEVICE_TREE="ti/k3-am625-beagleplay"
a53/.config:CONFIG_SPL_TEXT_BASE=0x80080000
a53/.config:CONFIG_BOOTCOMMAND="run set_led_state_start_load; run envboot; bootflow scan -lb;run set_led_state_fail_load"
a53/.config:CONFIG_SPL_FS_LOAD_PAYLOAD_NAME="u-boot.img"
```
R5 SPL은 SRAM(0x43c00000)에서 돌고, 파티션 1의 FAT에서 `tispl.bin`이라는 **이름**으로 다음 단계를 찾는다. 파일 이름을 바꾸면 부팅이 멈춘다. A53 U-Boot의 bootcmd는 bootstd의 `bootflow scan -lb`다 — 7절의 extlinux.conf를 찾는 주체다.

**보드가 오기 전에 할 일 (가이드 — 네트워크 되는 곳에서)**: `ti-linux-firmware` 저장소(브랜치 `ti-linux-firmware`)를 받아 `FW=`로 넘기고, OP-TEE를 `PLATFORM=k3-am62x`로 빌드해 두 번째 인자로 넘긴다. 그러면 위 경고가 사라져야 하고 9절 pre-flight의 FAIL 두 줄이 PASS가 된다.

```sh
# 가이드(실행 안 함 — 네트워크 필요): 진짜 블롭으로 다시 빌드
git clone --depth 1 -b ti-linux-firmware https://git.ti.com/git/processor-firmware/ti-linux-firmware.git /var/tmp/S3/ti-linux-firmware
git clone --depth 1 https://github.com/OP-TEE/optee_os.git /var/tmp/S3/optee_os
make -C /var/tmp/S3/optee_os CROSS_COMPILE=arm-none-eabi- CROSS_COMPILE64= CFG_ARM64_core=y PLATFORM=k3-am62x
FW=/var/tmp/S3/ti-linux-firmware bash scripts/02_build_uboot_beagleplay.sh \
   /var/tmp/S3/tfa-k3/k3/lite/release/bl31.bin /var/tmp/S3/optee_os/out/arm-plat-k3/core/tee-raw.bin
```

**흔한 실패**: (1) `yamllint` 없음 → 위와 같다. (2) 펌웨어 디렉터리 구조가 다름 → binman은 `BINMAN_INDIRS` 아래 `ti-sysfw/...`, `ti-dm/am62xx/...` **상대 경로**로 찾는다. (3) 가짜 블롭 이미지를 그대로 카드에 쓰면 ROM이 TIFS를 못 올려 LED가 전부 꺼진 채 멈춘다(문서의 LED 표 "00000 = R5 image not started").

### 3.4 다른 보드: Raspberry Pi 5용 U-Boot (실행함)

비교를 위해 RPi용도 빌드했다. RPi 5(BCM2712) 지원은 `rpi_arm64_defconfig` 안에 있다 — 문서 목록에는 아직 Pi 5가 없지만 보드 코드에는 있다(5절 예제에서 `rpi.c`의 `bcm2712-rpi-5-b.dtb` 확인).

```sh
make -C /var/lab/src/u-boot O=/var/tmp/S3/uboot/rpi rpi_arm64_defconfig
time make -C /var/lab/src/u-boot O=/var/tmp/S3/uboot/rpi -j2 CROSS_COMPILE=
```
```text
real	0m56.353s
-rw-rw-r-- 1 donh donh 934144 Oct 18 16:35 /var/tmp/S3/uboot/rpi/u-boot.bin
CONFIG_TEXT_BASE=0x0
CONFIG_DEFAULT_DEVICE_TREE="bcm2711-rpi-4-b"
CONFIG_BOOTSTD_DEFAULTS=y
CONFIG_BOOTCOMMAND="bootflow scan"
CONFIG_OF_BOARD=y
```
출력에서 볼 것: 파일이 **하나**다(`u-boot.bin`). SPL도 TF-A도 블롭 패키징도 없다 — 그 일은 RPi의 비공개 EEPROM 펌웨어가 한다. `CONFIG_OF_BOARD=y`는 "내장 DT 대신 펌웨어가 넘겨 준 DT를 쓴다"는 뜻이라 `DEFAULT_DEVICE_TREE`가 Pi 4 이름이어도 Pi 5에서 돈다. 배우기에는 쉽지만, 면접에서 말할 "부트 체인을 직접 조립했다" 이야기는 BeaglePlay 쪽이 훨씬 길다.

---

## 4. 단계 3 — 커널 DTB만 빌드하고 노드를 검사한다 (VM에서 실행함)

### 4.1 DTB 이름이 6.18.55 트리에 정말 있나

```sh
cd /var/lab/src/linux-6.18.55/arch/arm64/boot/dts
ls ti/ | grep beagleplay; ls nvidia | grep -E "p3768"; grep -n "model" nvidia/tegra234-p3768-0000+p3767-0005.dts
```
```text
k3-am625-beagleplay-csi2-ov5640.dtso
k3-am625-beagleplay-csi2-tevi-ov5640.dtso
k3-am625-beagleplay.dts
tegra234-p3768-0000+p3767-0000.dts
tegra234-p3768-0000+p3767-0005.dts
tegra234-p3768-0000+p3767.dtsi
	model = "NVIDIA Jetson Orin Nano Developer Kit";
```
출력에서 볼 것: Jetson은 `p3768`(캐리어 보드) + `p3767-0005`(모듈 SKU) 조합 이름이다. `-0000`은 Orin NX 쪽이다(같은 파일의 model 줄이 "Orin NX"). 이름을 잘못 고르면 메모리 · 전원 레일이 다른 SKU의 DT로 부팅한다.

### 4.2 대상 DTB만 빌드 (전체 커널 빌드 아님)

```sh
#!/bin/bash
# 03_build_dtbs.sh: build ONLY the candidate boards' DTBs from a mainline tree (no Image)
# usage: 03_build_dtbs.sh [linux_src] [O_dir]
set -euo pipefail
K=${1:-/var/lab/src/linux-6.18.55}
O=${2:-/var/tmp/S3/kdtb}
DTBS="ti/k3-am625-beagleplay.dtb
ti/k3-am625-beagleplay-csi2-ov5640.dtbo
broadcom/bcm2712-rpi-5-b.dtb
nvidia/tegra234-p3768-0000+p3767-0005.dtb
freescale/imx8mp-evk.dtb
rockchip/rk3588-rock-5b.dtb"
mkdir -p "$O"
[ -f "$O/.config" ] || make -C "$K" O="$O" ARCH=arm64 defconfig > /dev/null
for d in $DTBS; do
  make -C "$K" O="$O" ARCH=arm64 -j2 "$d" > "$O/dtb-$(basename $d).log" 2>&1 \
    || { echo "FAIL $d"; tail -5 "$O/dtb-$(basename $d).log"; continue; }
  f="$O/arch/arm64/boot/dts/$d"
  printf '%-44s %7d bytes\n' "$d" "$(stat -c %s "$f")"
done
```
```text
ti/k3-am625-beagleplay.dtb                     61433 bytes
ti/k3-am625-beagleplay-csi2-ov5640.dtbo         3225 bytes
broadcom/bcm2712-rpi-5-b.dtb                   22508 bytes
nvidia/tegra234-p3768-0000+p3767-0005.dtb     144263 bytes
freescale/imx8mp-evk.dtb                       76379 bytes
rockchip/rk3588-rock-5b.dtb                   186314 bytes

real	0m10.862s
```
출력에서 볼 것: 11초. `make <vendor>/<board>.dtb`는 `scripts/dtc`만 빌드하고 해당 DTB 하나만 만든다(D5). RPi 5 DTB가 22 KB로 가장 작다 — 기술된 하드웨어가 적다는 뜻이고, 이게 O5의 "2/7"과 같은 이야기다.

### 4.3 DTB 인벤토리 툴 — 콘솔 · I2C · SPI · CAN · CSI

```python
#!/usr/bin/env python3
"""dtb_inventory.py <x.dtb>...: model, console, and UART/I2C/SPI/CAN/CSI nodes (enabled/total)"""
import re, subprocess, sys
CLS = [("UART", r"uart|serial|pl011|8250"), ("I2C", r"i2c(?!.*mux)"),
       ("SPI", r"spi(?!nlock)(?!.*nor)"), ("CAN", r"m_can|mttcan|flexcan|canfd|-can$"),
       ("CSI", r"csi")]
def parse(dtb):
    dts = subprocess.run(["dtc", "-q", "-I", "dtb", "-O", "dts", dtb],
                         capture_output=True, text=True, check=True).stdout
    path, nodes, cur = [], {}, None
    for ln in dts.splitlines():
        s = ln.strip()
        m = re.match(r'(?:[\w-]+:\s*)?([\w,.+@-]+|/)\s*\{$', s)
        if m:
            path.append(m.group(1)); cur = "/" + "/".join(path[1:])
            nodes[cur] = {}; continue
        if s == "};":
            path.pop(); cur = "/" + "/".join(path[1:]) if path else None; continue
        m = re.match(r'([\w,.#+-]+)\s*=\s*(.*);$', s)
        if m and cur is not None:
            nodes[cur][m.group(1)] = m.group(2)
    return nodes
for dtb in sys.argv[1:]:
    n = parse(dtb)
    root = n.get("/", {})
    print(f"== {dtb.split('/')[-1]}  model={root.get('model', '?')}")
    print(f"   chosen stdout-path={n.get('/chosen', {}).get('stdout-path', '-')}")
    al = n.get("/aliases", {})
    print("   aliases: " + " ".join(f"{k}" for k in al if re.match(r"serial\d|i2c\d|can\d", k)))
    for cls, rx in CLS:
        hits = [(p, v) for p, v in n.items()
                if re.search(rx, v.get("compatible", "").split(",")[-1].split('"')[0]
                             + " " + v.get("compatible", ""))
            and "syscon" not in v.get("compatible", "")]
        on = [p for p, v in hits if v.get("status", '"okay"') in ('"okay"', '"ok"')]
        short = ", ".join(p.split("/")[-1] for p in on[:4]) + (" ..." if len(on) > 4 else "")
        print(f"   {cls:4} {len(on):2}/{len(hits):<2} enabled: {short}")
```

BeaglePlay는 CSI overlay를 `fdtoverlay`로 미리 합친 것(`bp+ov5640.dtb`)도 함께 검사한다:

```sh
cd /var/tmp/S3/kdtb/arch/arm64/boot/dts
fdtoverlay -i ti/k3-am625-beagleplay.dtb -o /var/tmp/S3/bp+ov5640.dtb ti/k3-am625-beagleplay-csi2-ov5640.dtbo
python3 -I ~/.elinux_scratch/S3/board-bringup/tools/dtb_inventory.py ti/k3-am625-beagleplay.dtb /var/tmp/S3/bp+ov5640.dtb \
  broadcom/bcm2712-rpi-5-b.dtb nvidia/tegra234-p3768-0000+p3767-0005.dtb freescale/imx8mp-evk.dtb rockchip/rk3588-rock-5b.dtb
```
```text
== k3-am625-beagleplay.dtb  model="BeagleBoard.org BeaglePlay"
   chosen stdout-path="serial2:115200n8"
   aliases: i2c0 i2c1 i2c2 i2c3 i2c4 i2c5 serial0 serial1 serial2
   UART  3/9  enabled: serial@2800000, serial@2850000, serial@2860000
   I2C   5/6  enabled: i2c@4900000, i2c@20000000, i2c@20010000, i2c@20020000 ...
   SPI   1/5  enabled: spi@20120000
   CAN   0/3  enabled: 
   CSI   1/2  enabled: csi-bridge@30101000
== bp+ov5640.dtb  model="BeagleBoard.org BeaglePlay"
   chosen stdout-path="serial2:115200n8"
   aliases: i2c0 i2c1 i2c2 i2c3 i2c4 i2c5 serial0 serial1 serial2
   UART  3/9  enabled: serial@2800000, serial@2850000, serial@2860000
   I2C   6/6  enabled: i2c@4900000, i2c@2b200000, i2c@20000000, i2c@20010000 ...
   SPI   1/5  enabled: spi@20120000
   CAN   0/3  enabled: 
   CSI   2/2  enabled: ticsi2rx@30102000, csi-bridge@30101000
== bcm2712-rpi-5-b.dtb  model="Raspberry Pi 5"
   chosen stdout-path="serial10:115200n8"
   aliases: serial10
   UART  2/2  enabled: serial@7d001000, serial@7d50c000
   I2C   2/2  enabled: i2c@7d508200, i2c@7d508280
   SPI   0/0  enabled: 
   CAN   0/0  enabled: 
   CSI   0/0  enabled: 
== tegra234-p3768-0000+p3767-0005.dtb  model="NVIDIA Jetson Orin Nano Developer Kit"
   chosen stdout-path="serial0:115200n8"
   aliases: i2c0 i2c1 i2c2 i2c3 i2c4 i2c5 i2c6 i2c7 i2c8 serial0 serial1 serial2
   UART  3/3  enabled: serial@3100000, serial@3140000, serial@31d0000
   I2C   3/9  enabled: i2c@3160000, i2c@c240000, i2c
   SPI   1/5  enabled: spi@3270000
   CAN   0/0  enabled: 
   CSI   0/0  enabled: 
== imx8mp-evk.dtb  model="NXP i.MX8MPlus EVK board"
   chosen stdout-path="/soc@0/bus@30800000/spba-bus@30800000/serial@30890000"
   aliases: i2c0 i2c1 i2c2 i2c3 i2c4 i2c5 serial0 serial1 serial2 serial3
   UART  3/4  enabled: serial@30860000, serial@30880000, serial@30890000
   I2C   3/6  enabled: i2c@30a20000, i2c@30a30000, i2c@30a40000
   SPI   1/4  enabled: spi@30bb0000
   CAN   1/2  enabled: can@308c0000
   CSI   0/2  enabled: 
== rk3588-rock-5b.dtb  model="Radxa ROCK 5B"
   chosen stdout-path="serial2:1500000n8"
   aliases: i2c0 i2c1 i2c2 i2c3 i2c4 i2c5 i2c6 i2c7 i2c8 serial0 serial1 serial2 serial3 serial4 serial5 serial6 serial7 serial8 serial9
   UART  2/10 enabled: serial@feb50000, serial@feb90000
   I2C   5/9  enabled: i2c@fd880000, i2c@fea90000, i2c@feac0000, i2c@fec80000 ...
   SPI   1/5  enabled: spi@feb20000
   CAN   0/0  enabled: 
   CSI   0/2  enabled: 
```
출력에서 볼 것:

- **콘솔이 어디냐**: BeaglePlay `serial2`(115200), RPi 5 `serial10`, ROCK 5B `serial2`인데 **1,500,000 baud**. 보드 오기 전에 USB-UART가 그 속도를 지원하는지 확인해야 한다.
- **BeaglePlay CAN 0/3**: SoC에는 MCAN 3개(`main_mcan0`, `mcu_mcan0/1`)가 있지만 전부 `disabled` — 보드에 트랜시버가 없어서다. CAN을 하려면 mikroBUS CAN 모듈 + overlay가 필요하다(S7).
- **BeaglePlay CSI 1/2 → 2/2**: base DT는 CSI 수신기(`ticsi2rx`)를 꺼 두고, 카메라 모듈 overlay가 그것과 카메라 I2C 버스(`i2c@2b200000`)를 켠다. "보드 DT는 커넥터까지, 꽂는 것은 overlay"라는 D6의 원칙이 실물로 보인다.
- **Jetson CSI 0/0 · CAN 0/0**: mainline DT에는 카메라 · CAN 노드 자체가 없다. Jetson으로 카메라 · CAN을 하려면 L4T 커널이 필요하다는 근거다.
- 한계: 이 툴은 compatible 문자열 패턴으로 분류한다. ROCK 5B의 CSI D-PHY GRF처럼 `syscon`인 노드는 일부러 뺐고, `i2c` 이름만 있는 노드(Jetson의 `i2c`)는 그대로 셌다.

### 4.4 BeaglePlay 콘솔 UART와 CAN 노드를 직접 본다

```sh
D=ti/k3-am625-beagleplay.dtb
fdtget $D /aliases serial2; fdtget $D /aliases i2c3
dtc -q -I dtb -O dts $D > /var/tmp/S3/bp.dts
awk "/serial@2800000 \{/,/};/" /var/tmp/S3/bp.dts
awk "/can@20701000 \{/,/};/" /var/tmp/S3/bp.dts
```
```text
/bus@f0000/serial@2800000
/bus@f0000/i2c@20030000
		serial@2800000 {
			compatible = "ti,am64-uart", "ti,am654-uart";
			reg = <0x00 0x2800000 0x00 0x100>;
			interrupts = <0x00 0xb2 0x04>;
			power-domains = <0x03 0x92 0x01>;
			clocks = <0x02 0x92 0x00>;
			clock-names = "fclk";
			status = "okay";
			bootph-all;
			pinctrl-names = "default";
			pinctrl-0 = <0x0d>;
			phandle = <0x81>;
		};
		can@20701000 {
			compatible = "bosch,m_can";
			reg = <0x00 0x20701000 0x00 0x200 0x00 0x20708000 0x00 0x8000>;
			reg-names = "m_can", "message_ram";
			power-domains = <0x03 0x62 0x01>;
			clocks = <0x02 0x62 0x06 0x02 0x62 0x01>;
			clock-names = "hclk", "cclk";
			interrupts = <0x00 0x9b 0x04 0x00 0x9c 0x04>;
			interrupt-names = "int0", "int1";
			bosch,mram-cfg = <0x00 0x80 0x40 0x40 0x40 0x40 0x20 0x20>;
			status = "disabled";
			phandle = <0xae>;
		};
```
출력에서 볼 것: 콘솔 UART가 `ti,am654-uart` → 커널 `8250_omap` 드라이버, 이름은 alias 번호를 따라 `ttyS2`가 된다. `power-domains = <0x03 0x92 0x01>`은 TI SCI 프로토콜(장치 ID 0x92)이다 — K3에서는 클럭 · 전원을 레지스터로 직접 켜지 않고 **DM 펌웨어에 메시지로 요청한다**. 그래서 DM 블롭이 없으면 커널의 UART조차 켜지지 않는다. `bootph-all`은 "SPL 단계에서도 이 노드를 쓴다"는 U-Boot용 표시다.

mikroBUS · Grove · Qwiic가 어느 I2C인지는 커널 DTS 소스에서 확인했다:

```text
29:		i2c3 = &main_i2c3;
558:&mcu_i2c0 {
560:	pinctrl-0 = <&i2c_qwiic_pins_default>;
561:	clock-frequency = <100000>;
770:&main_i2c1 {
772:	pinctrl-0 = <&grove_pins_default>;
773:	clock-frequency = <100000>;
820:&main_i2c3 {
822:	pinctrl-0 = <&mikrobus_i2c_pins_default>;
823:	clock-frequency = <400000>;
```
(`grep -n`의 일부 줄.) mikroBUS = `main_i2c3`(alias i2c3 → `/dev/i2c-3`, 400 kHz), Qwiic = `mcu_i2c0`(100 kHz), Grove = `main_i2c1`(100 kHz).

---

## 5. 단계 4 — 필요한 펌웨어 블롭은 어디에 적혀 있나 (VM에서 실행함)

"이 보드를 부팅하려면 소스가 아닌 무엇이 필요한가"는 보드를 사기 전에 답할 수 있어야 한다(P2). U-Boot 트리 문서와 binman DT에서 이름을 뽑는다.

```sh
#!/bin/bash
# 04_blob_check.sh: where does each candidate board's non-source firmware come from?
# greps U-Boot docs/binman DT + TF-A platforms in the given trees
UB=${1:-/var/lab/src/u-boot}; TFA=${2:-/var/lab/src/tfa}
hit() { grep -n -o -E "$2" "$UB/$1" | sort -u -t: -k2,2 | head -${3:-4} | sed "s|^|  $1:|"; }
echo "[BeaglePlay / AM625] binman blob names (k3-am625-sk-binman.dtsi)"
hit arch/arm/dts/k3-am625-sk-binman.dtsi 'ti-(sysfw|dm)/[a-z0-9_./-]*gp[a-z0-9_.-]*|ti-dm/[a-z0-9_./-]+' 3
hit doc/board/beagle/am62x_beagleplay.rst 'TFA_BOARD=[a-z]+|OPTEE_PLATFORM=[a-z0-9-]+' 2
echo "[i.MX8MP EVK] DDR PHY training blobs + BL31"
hit doc/board/nxp/imx8mp_evk.rst 'lpddr4_pmu_train_[12]d_[di]mem_[0-9]+\.bin|PLAT=imx8mp bl31' 5
echo "[ROCK 5B / RK3588] TPL (DDR init) + BL31 from rkbin"
hit doc/board/rockchip/rockchip.rst 'rk3588_[A-Za-z0-9_.]+\.(bin|elf)' 2
echo "[RPi5] U-Boot board code knows these DTBs (firmware passes its own DT)"
hit board/raspberrypi/rpi/rpi.c 'bcm2712-[a-z0-9-]+\.dtb' 4
echo "[TF-A platforms present in $TFA/plat]"
for p in ti/k3 imx/imx8m/imx8mp rockchip/rk3588 nvidia/tegra/soc/t234 nvidia/tegra/soc/t194; do
  [ -d "$TFA/plat/$p" ] && echo "  yes  $p" || echo "  no   $p"
done
```
```text
[BeaglePlay / AM625] binman blob names (k3-am625-sk-binman.dtsi)
  arch/arm/dts/k3-am625-sk-binman.dtsi:260:ti-dm/am62xx/ipc_echo_testb_mcu1_0_release_strip.xer5f
  arch/arm/dts/k3-am625-sk-binman.dtsi:124:ti-sysfw/ti-fs-firmware-am62x-gp.bin
  arch/arm/dts/k3-am625-sk-binman.dtsi:207:ti-sysfw/ti-fs-stub-firmware-am62x-gp.bin
  doc/board/beagle/am62x_beagleplay.rst:69:OPTEE_PLATFORM=k3-am62x
  doc/board/beagle/am62x_beagleplay.rst:66:TFA_BOARD=lite
[i.MX8MP EVK] DDR PHY training blobs + BL31
  doc/board/nxp/imx8mp_evk.rst:24:PLAT=imx8mp bl31
  doc/board/nxp/imx8mp_evk.rst:45:lpddr4_pmu_train_1d_dmem_202006.bin
  doc/board/nxp/imx8mp_evk.rst:46:lpddr4_pmu_train_1d_imem_202006.bin
  doc/board/nxp/imx8mp_evk.rst:47:lpddr4_pmu_train_2d_dmem_202006.bin
  doc/board/nxp/imx8mp_evk.rst:48:lpddr4_pmu_train_2d_imem_202006.bin
[ROCK 5B / RK3588] TPL (DDR init) + BL31 from rkbin
  doc/board/rockchip/rockchip.rst:323:rk3588_bl31_v1.33.elf
  doc/board/rockchip/rockchip.rst:324:rk3588_ddr_lp4_2112MHz_lp5_2736MHz_v1.09.bin
[RPi5] U-Boot board code knows these DTBs (firmware passes its own DT)
  board/raspberrypi/rpi/rpi.c:204:bcm2712-d-rpi-5-b.dtb
  board/raspberrypi/rpi/rpi.c:203:bcm2712-rpi-5-b.dtb
  board/raspberrypi/rpi/rpi.c:215:bcm2712-rpi-500.dtb
  board/raspberrypi/rpi/rpi.c:210:bcm2712-rpi-cm5-cm5io.dtb
[TF-A platforms present in /var/lab/src/tfa/plat]
  yes  ti/k3
  yes  imx/imx8m/imx8mp
  yes  rockchip/rk3588
  no   nvidia/tegra/soc/t234
  yes  nvidia/tegra/soc/t194
```
출력에서 볼 것:

- **블롭의 성격이 보드마다 다르다.** AM625는 보안 · 전원 관리 펌웨어(TIFS, DM), i.MX8MP와 RK3588은 **DDR 학습 펌웨어**(Synopsys PHY 학습 코드, Rockchip TPL). Don의 SSD 경험으로 치면 "NAND 타이밍 트레이닝 코드를 벤더가 바이너리로만 준다"와 같은 종류다.
- **mainline TF-A에 Orin(t234) 플랫폼이 없다.** Xavier(t194)까지만 있다. Jetson Orin의 BL31은 NVIDIA가 L4T 안에서 따로 준다 — "Jetson은 부트 체인을 mainline으로 조립할 수 없다"는 사실을 트리로 증명했다.
- RPi 5 DTB가 4종(Pi 5, D 스테핑, Pi 500, CM5) — 같은 SoC라도 보드 리비전마다 DT가 다르다.

---

## 6. 단계 5 — 첫 DT overlay: mikroBUS의 BMI160 (VM에서 실행함, S4로 이어짐)

S4의 IIO 드라이버가 붙을 노드를 미리 만든다. 커널 6.18.55에는 이미 `bosch,bmi160` 드라이버(`drivers/iio/imu/bmi160/bmi160_i2c.c`)가 있으니, 보드가 오면 이 overlay만으로 in-tree 드라이버가 먼저 probe되는지 확인하고, 그 다음 S4의 자작 드라이버로 바꾼다.

```dts
// SPDX-License-Identifier: GPL-2.0
/*
 * s3-bmi160-mikrobus.dtso: BMI160 IMU on BeaglePlay mikroBUS I2C (main_i2c3, 400 kHz)
 * INT pin left out on purpose: check the mikroBUS INT net in the schematic first (P4).
 */
/dts-v1/;
/plugin/;

&main_i2c3 {
	#address-cells = <1>;
	#size-cells = <0>;
	status = "okay";

	s3_imu: imu@68 {
		compatible = "bosch,bmi160";
		reg = <0x68>;
		/* interrupt-parent = <&main_gpio1>; interrupts = <N IRQ_TYPE_EDGE_RISING>; */
	};
};
```

`&main_i2c3` 같은 label 참조가 풀리려면 base DTB에 `__symbols__`가 있어야 한다. TI Makefile이 `DTC_FLAGS := -@`(293행)로 빌드하므로 있다 — 5개 DTB 모두 `__symbols__` 프로퍼티가 82~943개였다.

```sh
cd /var/tmp/S3
dtc -@ -I dts -O dtb -o s3-bmi160.dtbo ~/.elinux_scratch/S3/board-bringup/overlays/s3-bmi160-mikrobus.dtso
fdtoverlay -i kdtb/arch/arm64/boot/dts/ti/k3-am625-beagleplay.dtb -o bp+imu.dtb s3-bmi160.dtbo
fdtget -p bp+imu.dtb /bus@f0000/i2c@20030000/imu@68
fdtget bp+imu.dtb /bus@f0000/i2c@20030000/imu@68 compatible
fdtget -t x bp+imu.dtb /bus@f0000/i2c@20030000/imu@68 reg
fdtget bp+imu.dtb /__symbols__ s3_imu
```
```text
-rw-rw-r-- 1 donh donh 457 Oct 18 16:36 s3-bmi160.dtbo
phandle
reg
compatible
bosch,bmi160
68
/bus@f0000/i2c@20030000/imu@68
```
출력에서 볼 것: overlay가 `main_i2c3`(= `i2c@20030000`) 아래에 `imu@68`을 만들었고, `-@`로 컴파일한 overlay의 label `s3_imu`도 합쳐진 DT의 `__symbols__`에 들어갔다(다음 overlay가 이 노드를 참조할 수 있다 — D6 §2).

인터럽트 줄을 주석으로 둔 이유: mikroBUS의 INT 핀이 `main_gpio1`의 몇 번 라인인지는 **회로도**로 확인해야 한다. DT의 `gpio-line-names`는 이름만 준다(실측, `main_gpio1` = `gpio@601000`):

```text
[(7, 'MIKROBUS_GPIO1_7'), (8, 'MIKROBUS_GPIO1_8'), (9, 'MIKROBUS_GPIO1_9'), (10, 'MIKROBUS_GPIO1_10'), (11, 'MIKROBUS_GPIO1_11'), (12, 'MIKROBUS_GPIO1_12'), (13, 'MIKROBUS_W1_GPIO0'), (14, 'MIKROBUS_GPIO1_14'), (22, 'MIKROBUS_GPIO1_22'), (23, 'MIKROBUS_GPIO1_23'), (24, 'MIKROBUS_GPIO1_24'), (25, 'MIKROBUS_GPIO1_25')]
```
어느 것이 INT/RST/PWM/CS인지는 BeaglePlay 회로도 PDF(U-Boot 문서가 링크하는 `BeaglePlay_sch.pdf`)에서 net을 따라가야 한다 — P4 §0의 "net 하나를 끝까지 따라가기" 연습 그 자체다.

커널 설정 쪽도 준비한다. arm64 defconfig는 BeaglePlay 필수 드라이버를 이미 다 켜고 있고(실측), BMI160만 빠져 있다:

```text
ARCH_K3                    CONFIG_ARCH_K3=y
TI_SCI_PROTOCOL            CONFIG_TI_SCI_PROTOCOL=y
SERIAL_8250_OMAP           CONFIG_SERIAL_8250_OMAP=y
MMC_SDHCI_AM654            CONFIG_MMC_SDHCI_AM654=y
TI_K3_UDMA                 CONFIG_TI_K3_UDMA=y
CAN_M_CAN_PLATFORM         CONFIG_CAN_M_CAN_PLATFORM=m
BMI160_I2C                 # CONFIG_BMI160_I2C is not set
VIDEO_TI_J721E_CSI2RX      CONFIG_VIDEO_TI_J721E_CSI2RX=m
VIDEO_OV5640               CONFIG_VIDEO_OV5640=m
MISC_RP1                   CONFIG_MISC_RP1=m
ARCH_TEGRA_234_SOC         CONFIG_ARCH_TEGRA_234_SOC=y
SERIAL_TEGRA_TCU           CONFIG_SERIAL_TEGRA_TCU=y
OF_OVERLAY                 CONFIG_OF_OVERLAY=y
```
(`.config`에서 뽑은 일부.) fragment를 합친다:

```sh
cd /var/lab/src/linux-6.18.55
ARCH=arm64 scripts/kconfig/merge_config.sh -m -O /var/tmp/S3/kdtb /var/tmp/S3/kdtb/.config \
    ~/.elinux_scratch/S3/board-bringup/configs/s3_beagleplay.config
make -s O=/var/tmp/S3/kdtb ARCH=arm64 olddefconfig
diff <(grep "^CONFIG" /var/tmp/S3/kdtb/.config.defconfig) <(grep "^CONFIG" /var/tmp/S3/kdtb/.config)
```
```text
#
# merged configuration written to /var/tmp/S3/kdtb/.config (needs make)
#
3938a3939,3940
> CONFIG_BMI160=m
> CONFIG_BMI160_I2C=m
```
출력에서 볼 것: fragment의 9줄 중 실제로 바뀐 것은 BMI160 두 줄뿐이다(나머지는 defconfig에 이미 있었다). fragment는 "의도"를 기록하는 문서이기도 해서 중복이어도 남겨 둔다(K5의 config fragment 관리와 같은 습관). 커널 `Image` 전체 빌드는 하지 않았다 — SD 이미지에는 같은 6.18.55 defconfig 계열인 랩 `Image`를 넣었다(7절).

---

## 7. 단계 6 — SD 카드 이미지 파일 만들기 (VM에서 실행함, 실제 장치는 건드리지 않음)

### 7.1 레이아웃 설계

AM625 ROM의 SD 부팅은 "FS 모드"다: MBR의 **active(부팅 가능) FAT 파티션**에서 `tiboot3.bin`이라는 파일을 찾는다. R5 SPL은 같은 파티션(`SYS_MMCSD_FS_BOOT_PARTITION=1`)에서 `tispl.bin`, A53 SPL은 `u-boot.img`를 찾는다(3.3절 설정 실측). 커널 · DTB · extlinux.conf도 같은 FAT에 둔다 — bootstd가 디스크의 파티션을 훑어 `extlinux/extlinux.conf`를 찾는데, 8.1절 리허설에서 p1에서 찾는 것을 확인했다. 한 파티션에 모아 두면 SD만 바꿔 끼워서 부트로더와 커널을 같이 교체할 수 있다.

```text
# beagleplay-sd.sfdisk: MBR, p1 FAT32 (active) = BOOT for ROM/SPL, p2 ext4 = rootfs
label: dos
label-id: 0x53335333
unit: sectors
sector-size: 512

# 1 MiB .. 129 MiB : K3 ROM (SD "FS mode") reads tiboot3.bin from the active FAT partition
start=2048,   size=262144, type=c,  bootable
# 129 MiB .. end   : rootfs (root=PARTUUID=53335333-02)
start=264192,              type=83
```
`label-id`를 고정하면 MBR 파티션의 PARTUUID가 `53335333-01`, `-02`로 정해져 커널 인자에 `root=PARTUUID=53335333-02`를 쓸 수 있다. `/dev/mmcblk1p2`처럼 번호로 쓰면 eMMC · SD 열거 순서(이 DT에서는 alias `mmc0` = eMMC `mmc@fa10000`, `mmc1` = SD `mmc@fa00000` — 실측)에 따라 틀릴 수 있다.

```text
# extlinux.conf for U-Boot bootstd (bootmeth extlinux) on the BOOT partition
default s3-mainline
timeout 3
label s3-mainline
	kernel /Image
	fdt /k3-am625-beagleplay.dtb
	fdtoverlays /overlays/s3-bmi160.dtbo
	append console=ttyS2,115200n8 earlycon=ns16550a,mmio32,0x02800000 root=PARTUUID=53335333-02 rootwait rw init=/init
```
`earlycon` 주소 0x02800000은 4.4절의 콘솔 UART `reg`에서 왔다(M1). `fdtoverlays`는 U-Boot extlinux가 지원하는 키워드로, 커널에 넘기기 전에 overlay를 적용한다. `init=/init`은 S1/K2의 BusyBox 트리에 있는 랩용 `/init` 스크립트를 쓰겠다는 뜻이다(`/etc/inittab` 없이 셸까지).

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<rect x="20" y="30" width="96" height="60" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30" y="56" font-size="12">MBR + 갭</text>
<text x="30" y="74" font-size="12">0 ~ 1 MiB</text>
<rect x="116" y="30" width="270" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="128" y="56" font-size="13">p1 FAT32 "BOOT" (active, type c)</text>
<text x="128" y="74" font-size="12">1 ~ 129 MiB, PARTUUID 53335333-01</text>
<rect x="386" y="30" width="274" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="398" y="56" font-size="13">p2 ext4 "rootfs" (type 83)</text>
<text x="398" y="74" font-size="12">129 ~ 384 MiB, PARTUUID 53335333-02</text>
<text x="128" y="114" font-size="12">tiboot3.bin ← ROM (R5)</text>
<text x="128" y="132" font-size="12">tispl.bin ← R5 SPL</text>
<text x="128" y="150" font-size="12">u-boot.img ← A53 SPL</text>
<text x="128" y="168" font-size="12">extlinux/extlinux.conf ← U-Boot</text>
<text x="128" y="186" font-size="12">Image, k3-am625-beagleplay.dtb</text>
<text x="128" y="204" font-size="12">overlays/s3-bmi160.dtbo</text>
<text x="398" y="114" font-size="12">BusyBox 트리 (S1 · K2)</text>
<text x="398" y="132" font-size="12">/init, /bin, /sbin, /etc ...</text>
<text x="398" y="150" font-size="12">파일 소유자 0:0 (fakeroot)</text>
<text x="398" y="168" font-size="12">나중에 Debian rootfs로 교체</text>
<text x="20" y="236" font-size="12">eMMC로 옮길 때: tiboot3.bin만 HW boot0 파티션, 나머지는 UDA의 BOOT 파티션 (U-Boot 문서)</text>
</svg>
```
그림 3 — BeaglePlay SD 카드 레이아웃(비례 아님). 화살표 왼쪽 파일을 오른쪽 단계가 이름으로 찾는다.

### 7.2 이미지 생성 스크립트 — 블록 장치 없이

루프 장치(`losetup`)는 VM 전역 상태라 쓰지 않는다. 파티션 내용을 각각 **파일**로 만든 뒤 `dd conv=notrunc`로 오프셋에 써 넣는다.

```sh
#!/bin/bash
# 05_make_sd_image.sh: build a BeaglePlay SD card IMAGE FILE (never touches a block device)
# usage: 05_make_sd_image.sh <out.img>   env: UBO, KDTB, IMAGE, ROOTFS, OVL
set -euo pipefail
IMG=${1:-/var/tmp/S3/img/beagleplay-sd.img}
P=$(cd "$(dirname "$0")/.." && pwd)
UBO=${UBO:-/var/tmp/S3/uboot}; KDTB=${KDTB:-/var/tmp/S3/kdtb/arch/arm64/boot/dts}
IMAGE=${IMAGE:-/var/lab/out/Image}; ROOTFS=${ROOTFS:-/var/lab/build/rootfs}
OVL=${OVL:-/var/tmp/S3/s3-bmi160.dtbo}; EXTLINUX=${EXTLINUX:-$P/layout/extlinux.conf}
W=$(dirname "$IMG"); mkdir -p "$W"; rm -f "$IMG" "$W"/boot.vfat "$W"/rootfs.ext4
truncate -s 384M "$IMG"
sfdisk -q "$IMG" < "$P/layout/beagleplay-sd.sfdisk"
# p1: FAT32 BOOT, 128 MiB = 131072 KiB
mkfs.vfat -F 32 -n BOOT -C "$W/boot.vfat" 131072 > /dev/null
export MTOOLS_SKIP_CHECK=1
mcopy -i "$W/boot.vfat" "$UBO/r5/tiboot3.bin" "$UBO/a53/tispl.bin" "$UBO/a53/u-boot.img" ::/
mcopy -i "$W/boot.vfat" "$IMAGE" ::/Image
mcopy -i "$W/boot.vfat" "$KDTB/ti/k3-am625-beagleplay.dtb" ::/
mmd   -i "$W/boot.vfat" ::/overlays ::/extlinux
mcopy -i "$W/boot.vfat" "$OVL" ::/overlays/s3-bmi160.dtbo
mcopy -i "$W/boot.vfat" "$EXTLINUX" ::/extlinux/extlinux.conf
# p2: ext4 rootfs from a directory (S1/K2 BusyBox tree), files owned by root
P2_SECT=$(sfdisk -d "$IMG" | awk -F'[=,]' '/img2/ {print $4}')
# fakeroot: copy + chown to 0:0 inside a fake-root session so the image has root-owned files
fakeroot sh -c "cp -a '$ROOTFS' '$W/rootfs.d' && chown -R 0:0 '$W/rootfs.d' &&
  mkfs.ext4 -q -L rootfs -d '$W/rootfs.d' '$W/rootfs.ext4' $((P2_SECT / 2))K"
rm -rf "$W/rootfs.d"
dd if="$W/boot.vfat"   of="$IMG" bs=512 seek=2048   conv=notrunc,sparse status=none
dd if="$W/rootfs.ext4" of="$IMG" bs=512 seek=264192 conv=notrunc,sparse status=none
rm -f "$W/boot.vfat" "$W/rootfs.ext4"
ls -ls "$IMG"
```
```text
45308 -rw-rw-r-- 1 donh donh 402653184 Oct 18 16:38 /var/tmp/S3/img/beagleplay-sd.img
```
출력에서 볼 것: 논리 크기 384 MiB, 실제 디스크 사용 약 44 MiB(첫 열 45308 KiB) — sparse 파일이다.

### 7.3 이미지 검증

```sh
cd /var/tmp/S3/img
sfdisk -d beagleplay-sd.img; fdisk -l beagleplay-sd.img | tail -4
MTOOLS_SKIP_CHECK=1 mdir -i beagleplay-sd.img@@1M -/ ::
dd if=beagleplay-sd.img of=p2.ext4 bs=512 skip=264192 status=none && e2fsck -fn p2.ext4 | tail -1
debugfs -R "ls -l /" p2.ext4 | sed -n 4,7p
```
```text
label: dos
label-id: 0x53335333
device: beagleplay-sd.img
unit: sectors
sector-size: 512

beagleplay-sd.img1 : start=        2048, size=      262144, type=c, bootable
beagleplay-sd.img2 : start=      264192, size=      522240, type=83

Device             Boot  Start    End Sectors  Size Id Type
beagleplay-sd.img1 *      2048 264191  262144  128M  c W95 FAT32 (LBA)
beagleplay-sd.img2      264192 786431  522240  255M 83 Linux
 Volume in drive : is BOOT       
Directory for ::/
tiboot3  bin    128951 2026-10-18  16:38 
tispl    bin    245135 2026-10-18  16:38 
u-boot   img   1201424 2026-10-18  16:38 
IMAGE         42740224 2026-10-18  16:38  Image
K3-AM6~1 DTB     61433 2026-10-18  16:38  k3-am625-beagleplay.dtb
overlays     <DIR>     2026-10-18  16:38 
extlinux     <DIR>     2026-10-18  16:38 
rootfs: 430/65280 files (0.0% non-contiguous), 8843/65280 blocks
     13   40775 (2)      0      0    4096 10-Oct-2026 10:06 bin
    107   40775 (2)      0      0    4096 10-Oct-2026 10:06 dev
    108   40775 (2)      0      0    4096 10-Oct-2026 10:06 etc
    109  100775 (1)      0      0     493 10-Oct-2026 10:06 init
```
(mdir의 하위 디렉터리 목록은 생략했다.)

출력에서 볼 것: p1에 `*`(boot 플래그)와 type `c`, p2 시작이 264192섹터로 sfdisk 스크립트와 같다. rootfs 파일의 소유자가 `0 0`이다. 첫 버전은 `mkfs.ext4 -E root_owner=0:0`만 썼는데 그 옵션은 **루트 디렉터리만** 바꿔서 나머지 파일이 `501 1000`(VM 사용자)으로 들어갔다 — 그래서 fakeroot로 바꿨다. 실제 보드에서 `/bin/busybox`가 일반 사용자 소유면 setuid 류 동작과 권한 검사가 꼬인다.

**체크포인트**: `fdisk -l`에 boot 플래그 달린 FAT32 128 MiB + Linux 255 MiB, FAT 안에 3+2개 부트 파일과 두 디렉터리.

**흔한 실패**: (1) FAT16/FAT32를 너무 작게 만들면 `mkfs.vfat -F 32`가 경고 — 128 MiB면 충분하다. (2) MBR 대신 GPT를 쓰면 일부 ROM은 FS 모드에서 파티션을 못 찾는다(AM62 ROM의 GPT 지원은 TRM으로 확인 필요 — 기억 기반). (3) 파일을 FAT에 "8.3 이름"으로만 넣으면 `tiboot3.bin`은 괜찮지만 긴 DTB 이름이 짧아진다 — mtools는 VFAT 긴 이름을 같이 쓰므로 문제없다(위 출력의 `K3-AM6~1 DTB` 옆에 긴 이름이 있다).

---

## 8. 단계 7 — QEMU 리허설: 같은 이미지를 U-Boot가 읽게 한다 (VM에서 실행함)

BeaglePlay는 QEMU에 없다. 하지만 **이미지 레이아웃 · extlinux.conf · overlay 적용**은 SoC와 무관한 U-Boot 공통 코드라 QEMU `virt` + 랩 U-Boot(`qemu_arm64`)로 검증할 수 있다. 실리콘 전 펌웨어 검증에서 "SoC 의존 부분과 공통 부분을 나눠 공통 부분은 시뮬레이터에서 먼저 끝낸다"와 같은 전략이다.

### 8.1 U-Boot 명령으로 이미지와 overlay 검사

```text
#!/usr/bin/expect -f
# 06_uboot_preflight.exp <sd.img>: QEMU qemu_arm64 U-Boot reads the BeaglePlay SD IMAGE as a
# virtio disk -> checks partitions, extlinux bootflow, and applies the overlay with "fdt apply".
set img [lindex $argv 0]
set timeout 120
spawn qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -bios /var/lab/out/u-boot.bin \
  -drive if=none,id=sd,file=$img,format=raw,snapshot=on -device virtio-blk-device,drive=sd
expect "Hit any key to stop autoboot" { send "\r" }
proc cmd {c} { expect "=> "; send "$c\r" }
cmd "virtio scan"
cmd "part list virtio 0"
cmd "fatls virtio 0:1 /"
cmd "bootflow scan -l virtio"
cmd "load virtio 0:1 0x48000000 k3-am625-beagleplay.dtb"
cmd "fdt addr 0x48000000"
cmd "fdt resize 8192"
cmd "load virtio 0:1 0x49000000 overlays/s3-bmi160.dtbo"
cmd "fdt apply 0x49000000"
cmd "fdt print /bus@f0000/i2c@20030000/imu@68"
cmd "fdt get value m / model"
cmd "printenv m"
expect "=> "
send "\x01x"
expect eof
```
```text
=> part list virtio 0

Partition Map for virtio device 0  --   Partition Type: DOS

Part	Start Sector	Num Sectors	UUID		Type
  1	2048      	262144    	53335333-01	0c Boot
  2	264192    	522240    	53335333-02	83
=> fatls virtio 0:1 /
   128951   tiboot3.bin
   245135   tispl.bin
  1201424   u-boot.img
 42740224   Image
    61433   k3-am625-beagleplay.dtb
            overlays/
            extlinux/

5 file(s), 2 dir(s)

=> bootflow scan -l virtio
Scanning for bootflows with label 'virtio'
Seq  Method       State   Uclass    Part  Name                      Filename
---  -----------  ------  --------  ----  ------------------------  ----------------
Hunting with: virtio
Scanning bootdev 'virtio-blk#31.bootdev':
  0  extlinux     ready   virtio       1  virtio-blk#31.bootdev.par /extlinux/extlinux.conf
No more bootdevs
---  -----------  ------  --------  ----  ------------------------  ----------------
(1 bootflow, 1 valid)
=> load virtio 0:1 0x48000000 k3-am625-beagleplay.dtb
61433 bytes read in 3 ms (19.5 MiB/s)
=> fdt addr 0x48000000
Working FDT set to 48000000
=> fdt resize 8192
=> load virtio 0:1 0x49000000 overlays/s3-bmi160.dtbo
457 bytes read in 1 ms (446.3 KiB/s)
=> fdt apply 0x49000000
=> fdt print /bus@f0000/i2c@20030000/imu@68
imu@68 {
	phandle = <0x000000d4>;
	reg = <0x00000068>;
	compatible = "bosch,bmi160";
};
=> fdt get value m / model
=> printenv m
m=BeagleBoard.org BeaglePlay
```
(앞부분 U-Boot 배너와 `virtio scan`은 생략했다.)

출력에서 볼 것: U-Boot가 본 PARTUUID가 `53335333-01/02`로 sfdisk의 `label-id`와 같다. `bootflow scan`이 p1의 `/extlinux/extlinux.conf`를 `ready` 상태로 찾았다 — BeaglePlay의 U-Boot도 같은 bootstd 코드로 같은 결과를 내야 한다(bootcmd가 `bootflow scan -lb`, 3.3절). `fdt apply`가 에러 없이 끝나고 `imu@68`이 보인다 — 보드에서 extlinux의 `fdtoverlays` 줄이 하는 일을 손으로 한 것이다(D6 §5). `fdt resize`를 빼면 실패한다 — 일부러 빼고 다시 돌린 실제 출력:

```text
=> fdt apply 0x49000000
failed on fdt_overlay_apply(): FDT_ERR_NOSPACE
```
base DTB에서 `__symbols__`를 지운 사본(`fdtput -r nosym.dtb /__symbols__`)에 `fdtoverlay`를 돌리면 이렇게 실패한다:

```text
Failed to apply 's3-bmi160.dtbo': FDT_ERR_BADOFFSET
base blob does not have a '/__symbols__' node, make sure you have compiled the base blob with '-@' option
```

### 8.2 끝까지 부팅해 보는 리허설 + 단계별 시간

같은 스크립트로 extlinux만 QEMU용으로 바꾼 이미지를 만들고(`fdt` 줄 없음 → U-Boot가 QEMU의 DT를 넘긴다), 콘솔 줄마다 호스트 시간을 찍어 부팅 단계를 잰다. 보드가 오면 똑같이 `cat /dev/cu.usbserial-XXXX`를 `ts_capture.py`에 파이프한다.

```text
# extlinux-qemu.conf: SAME image layout, rehearsed on QEMU virt (no fdt line -> U-Boot passes QEMU's DT)
default s3-rehearsal
label s3-rehearsal
	kernel /Image
	append console=ttyAMA0 root=PARTUUID=53335333-02 rootwait rw init=/init LAB_CMD=cat~/proc/cmdline
```

```python
#!/usr/bin/env python3
"""ts_capture.py: prefix every console line from stdin with host time since start (s).
Real board: picocom/screen can't timestamp -> `cat /dev/ttyUSB0 | ts_capture.py > boot.log`."""
import sys, time
t0 = time.monotonic()
for line in sys.stdin.buffer:
    s = line.decode(errors="replace").rstrip("\r\n")
    sys.stdout.write(f"[{time.monotonic() - t0:9.3f}] {s}\n"); sys.stdout.flush()
```

```python
#!/usr/bin/env python3
"""boot_time.py <ts_capture.log>: boot milestones (host clock) -> per-stage durations."""
import re, sys
MS = [("first output", r"."),
      ("U-Boot SPL banner", r"U-Boot SPL 20"),
      ("U-Boot proper banner", r"^U-Boot 20"),
      ("bootflow/extlinux found", r"Retrieving file: .*extlinux|extlinux\.conf"),
      ("Starting kernel", r"Starting kernel"),
      ("kernel first printk", r"\[\s*0\.000000\]"),
      ("rootfs mounted", r"VFS: Mounted root"),
      ("init / userspace", r"Run /init|lab initramfs|login:")]
t = {}
for ln in open(sys.argv[1], errors="replace"):
    m = re.match(r"\[\s*([\d.]+)\] (.*)", ln)
    if not m:
        continue
    ts, txt = float(m.group(1)), m.group(2)
    for name, rx in MS:
        if name not in t and re.search(rx, txt):
            t[name] = ts
prev = None
print(f"{'milestone':26} {'t(s)':>8} {'delta':>8}")
for name, _ in MS:
    if name in t:
        d = "" if prev is None else f"{t[name] - prev:8.3f}"
        print(f"{name:26} {t[name]:8.3f} {d:>8}")
        prev = t[name]
    else:
        print(f"{name:26} {'-':>8}")
```

```sh
#!/bin/bash
# 08_qemu_rehearsal.sh: same SD layout + extlinux, booted by lab U-Boot (qemu_arm64) on QEMU, timestamped
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd); IMG=/var/tmp/S3/img/rehearsal-sd.img
EXTLINUX=$P/layout/extlinux-qemu.conf bash "$P/scripts/05_make_sd_image.sh" "$IMG" > /dev/null
timeout 150 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -bios /var/lab/out/u-boot.bin -drive if=none,id=sd,file=$IMG,format=raw,snapshot=on \
  -device virtio-blk-device,drive=sd < /dev/null | python3 -I "$P/tools/ts_capture.py" > /var/tmp/S3/rehearsal.log || true
grep -E "U-Boot 20|Scanning bootdev|extlinux|Retrieving|Starting kernel|VFS: Mounted|lab initramfs|root=" /var/tmp/S3/rehearsal.log | cut -c1-120
python3 -I "$P/tools/boot_time.py" /var/tmp/S3/rehearsal.log
```
```text
[    0.110] U-Boot 2026.10 (Oct 10 2026 - 09:59:26 -0700)
[    2.659] Scanning bootdev 'fw-cfg@9020000.bootdev':
[    2.668] Scanning bootdev 'virtio-blk#31.bootdev':
[    2.671]   1  extlinux     ready   virtio       1  virtio-blk#31.bootdev.par /extlinux/extlinux.conf
[    2.671] ** Booting bootflow 'virtio-blk#31.bootdev.part_1' with extlinux
[    2.674] Retrieving file: /Image
[    2.783] append: console=ttyAMA0 root=PARTUUID=53335333-02 rootwait rw init=/init LAB_CMD=cat~/proc/cmdline
[    2.796] Starting kernel ...
[    3.566] [    0.000000] Kernel command line: console=ttyAMA0 root=PARTUUID=53335333-02 rootwait rw init=/init LAB_CMD
[    4.594] [    1.628937] VFS: Mounted root (ext4 filesystem) on device 254:2.
[    4.870] lab initramfs: 6.18.55 up 1.73s
[    4.976] console=ttyAMA0 root=PARTUUID=53335333-02 rootwait rw init=/init LAB_CMD=cat~/proc/cmdline
milestone                      t(s)    delta
first output                  0.110         
U-Boot SPL banner                 -
U-Boot proper banner          0.110    0.000
bootflow/extlinux found       2.671    2.561
Starting kernel               2.796    0.125
kernel first printk           3.552    0.756
rootfs mounted                4.594    1.042
init / userspace              4.658    0.064
```
출력에서 볼 것: **같은 레이아웃이 끝까지 동작한다** — U-Boot가 extlinux를 찾고, 커널이 `root=PARTUUID=53335333-02`로 ext4 p2를 마운트하고(`device 254:2` = virtio 디스크의 2번 파티션), BusyBox `/init`이 돌았다. QEMU에는 SPL이 없어서 "U-Boot SPL banner"가 `-`다 — BeaglePlay에서는 R5 SPL과 A53 SPL 배너 두 개가 이 앞에 찍힌다. U-Boot 구간 2.56초 중 약 2초는 `bootdelay` 카운트다운이다.

> 이 시간은 TCG 에뮬레이션(M2 호스트, KVM 없음)에서 잰 값이라 **실제 하드웨어 수치가 아니다**. 의미 있는 것은 "도구가 단계를 나눠 준다"는 점이다.

```svg
<svg viewBox="0 0 680 210" xmlns="http://www.w3.org/2000/svg">
<rect x="53" y="40" width="307" height="34" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="360" y="40" width="15" height="34" fill="#e08a3c" fill-opacity="0.45" stroke="#e08a3c"/>
<rect x="375" y="40" width="91" height="34" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="466" y="40" width="125" height="34" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
<rect x="591" y="40" width="8" height="34" fill="#888" fill-opacity="0.4" stroke="#888"/>
<line x1="40" y1="84" x2="640" y2="84" stroke="currentColor"/>
<line x1="40" y1="84" x2="40" y2="90" stroke="currentColor"/><text x="36" y="104" font-size="12">0</text>
<line x1="160" y1="84" x2="160" y2="90" stroke="currentColor"/><text x="156" y="104" font-size="12">1</text>
<line x1="280" y1="84" x2="280" y2="90" stroke="currentColor"/><text x="276" y="104" font-size="12">2</text>
<line x1="400" y1="84" x2="400" y2="90" stroke="currentColor"/><text x="396" y="104" font-size="12">3</text>
<line x1="520" y1="84" x2="520" y2="90" stroke="currentColor"/><text x="516" y="104" font-size="12">4</text>
<line x1="640" y1="84" x2="640" y2="90" stroke="currentColor"/><text x="626" y="104" font-size="12">5 s</text>
<text x="40" y="26" font-size="13">QEMU 리허설 부팅 타임라인 (호스트 시계, TCG — 실제 HW 아님)</text>
<rect x="40" y="122" width="14" height="12" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><text x="60" y="133" font-size="12">U-Boot (bootdelay 2 s 포함) 2.56 s</text>
<rect x="340" y="122" width="14" height="12" fill="#e08a3c" fill-opacity="0.45" stroke="#e08a3c"/><text x="360" y="133" font-size="12">extlinux · Image 로드 0.13 s</text>
<rect x="40" y="148" width="14" height="12" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="60" y="159" font-size="12">Starting kernel → 첫 printk 0.76 s</text>
<rect x="340" y="148" width="14" height="12" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/><text x="360" y="159" font-size="12">커널 초기화 → rootfs 마운트 1.04 s</text>
<rect x="40" y="174" width="14" height="12" fill="#888" fill-opacity="0.4" stroke="#888"/><text x="60" y="185" font-size="12">rootfs → /init 0.06 s</text>
<text x="340" y="185" font-size="12">보드에선 앞에 ROM · SPL 구간 추가</text>
</svg>
```
그림 4 — `boot_time.py` 실측 값(TCG)으로 그린 부팅 단계. 보드에서 같은 툴로 다시 재서 나란히 놓는 것이 측정 과제 1번이다(11.1절).

**흔한 실패**: QEMU용 extlinux에 `fdt /k3-am625-beagleplay.dtb`를 남기면 커널이 virt 머신에서 AM625 DT로 부팅해 콘솔도 없이 멈춘다 — 리허설은 "레이아웃 검증"이지 "보드 부팅"이 아니다.

---

## 9. 단계 8 — GPIO 토글 측정 툴을 미리 만들고 gpio-sim에서 돌린다 (VM에서 실행함)

보드가 오면 로직 분석기로 "유저 공간에서 GPIO를 바꾸는 데 걸리는 시간"과 "주기적으로 토글할 때의 지터"를 잰다(M11 §2, N3). 툴은 지금 만든다. GPIO v2 chardev uAPI(H4)를 쓴다 — sysfs GPIO는 deprecated다.

```c
// gpio_toggle.c: toggle one GPIO line via the v2 chardev uAPI and time each write.
// usage: gpio_toggle /dev/gpiochipN <offset> <count> [period_us]  (period 0 = as fast as possible)
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
static long long ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s chip off count [period_us]\n", argv[0]); return 2; }
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open chip"); return 1; }
    struct gpio_v2_line_request req;
    memset(&req, 0, sizeof req);
    req.offsets[0] = (unsigned)atoi(argv[2]);
    req.num_lines = 1;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    strcpy(req.consumer, "s3-toggle");
    if (ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) { perror("GET_LINE"); return 1; }
    long n = atol(argv[3]), period = argc > 4 ? atol(argv[4]) * 1000 : 0;
    long long t0 = ns(), next = t0, mn = 1LL << 62, mx = 0, late = 0;
    for (long i = 0; i < n; i++) {
        if (period) {
            next += period;
            struct timespec ts = { next / 1000000000LL, next % 1000000000LL };
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
            long long l = ns() - next;   // wake-up lateness = what a scope sees as edge jitter
            if (l > late) late = l;
        }
        struct gpio_v2_line_values v = { .bits = (unsigned)(i & 1), .mask = 1 };
        long long a = ns();
        if (ioctl(req.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v) < 0) { perror("SET"); return 1; }
        long long d = ns() - a;
        if (d < mn) mn = d;
        if (d > mx) mx = d;
    }
    long long tot = ns() - t0;
    printf("toggles=%ld total=%lld us avg=%lld ns/toggle set_ioctl min=%lld max=%lld ns",
           n, tot / 1000, tot / n, mn, mx);
    if (period) printf(" period=%ld us max_wake_late=%lld us", period / 1000, late / 1000);
    printf("\n");
    close(req.fd); close(fd);
    return 0;
}
```
VM 호스트 커널에는 gpio-sim을 올리지 않는다(공용 상태). 대신 QEMU 게스트(랩 `Image`, `GPIO_SIM` 내장) 안에서 configfs로 가짜 칩을 만들어 돌린다:

```sh
#!/bin/sh
# s3_gpio_sim.sh (runs INSIDE the QEMU guest): gpio-sim chip "s3_sim" -> gpio_toggle stand-in for a real pin
mount -t configfs none /sys/kernel/config 2>/dev/null
G=/sys/kernel/config/gpio-sim/s3_sim
mkdir -p $G/bank0 && echo 8 > $G/bank0/num_lines && echo 1 > $G/live || exit 1
CHIP=/dev/$(cat $G/bank0/chip_name)
echo "chip=$CHIP ($(cat $G/bank0/num_lines) lines, gpio-sim)"
/gpio_toggle $CHIP 3 100000
/gpio_toggle $CHIP 3 2000 1000
SIM=$(ls -d /sys/devices/platform/gpio-sim.*/$(basename $CHIP)/sim_gpio3)
echo "sim_gpio3 value after run: $(cat $SIM/value)"
```
```sh
#!/bin/bash
# 07_qemu_gpio_toggle.sh: pack gpio_toggle + s3_gpio_sim.sh into a copy of the lab initramfs, boot, run
set -euo pipefail
W=/var/tmp/S3/qemu; P=$(cd "$(dirname "$0")/.." && pwd)
rm -rf "$W/r"; mkdir -p "$W/r"; cp -a /var/lab/build/rootfs/. "$W/r/"
cp /var/tmp/S3/bin/gpio_toggle "$P/scripts/s3_gpio_sim.sh" "$W/r/"
(cd "$W/r" && find . | cpio -o -H newc --quiet | gzip) > "$W/i.gz"
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd "$W/i.gz" \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/s3_gpio_sim.sh" | tr -d '\r' | grep -v '^$'
```
```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static ~/.elinux_scratch/S3/board-bringup/src/gpio_toggle.c -o /var/tmp/S3/bin/gpio_toggle
bash ~/.elinux_scratch/S3/board-bringup/scripts/07_qemu_gpio_toggle.sh
```
```text
lab initramfs: 6.18.55 up 1.77s
chip=/dev/gpiochip1 (8 lines, gpio-sim)
toggles=100000 total=3885850 us avg=38858 ns/toggle set_ioctl min=25248 max=7853984 ns
toggles=2000 total=2000607 us avg=1000303 ns/toggle set_ioctl min=28256 max=3036880 ns period=1000 us max_wake_late=22746 us
sim_gpio3 value after run: 0
[    7.988448] reboot: Power down
```
출력에서 볼 것: 컴파일 경고 0개. 두 번째 실행의 마지막 쓰기는 `1999 & 1` = 1이었는데, 끝난 뒤 값이 0이다 — 프로그램이 line request fd를 닫으며 라인을 놓았고(release), 요청되지 않은 gpio-sim 라인은 시뮬레이션 pull(기본 pull-down) 값으로 돌아간다(`Documentation/admin-guide/gpio/gpio-sim.rst`의 `pull`/`value` 설명). 실제 보드에서도 **line request를 닫은 뒤의 핀 상태는 보장되지 않는다**. 숫자(평균 39 µs/toggle, 1 ms 주기에서 최대 22.7 ms 늦은 기상)는 TCG 에뮬레이션 값이라 **실제 하드웨어 수치가 아니다** — 실제 A53에서는 ioctl 한 번이 훨씬 짧을 것으로 예상하지만(추정), 그것이 바로 보드에서 LA로 잴 대상이다.

---

## 10. 단계 9 — pre-flight 체크리스트 (VM에서 실행함)

보드가 도착하기 전 마지막 관문. 자동으로 확인 가능한 것은 스크립트로, 손으로만 확인 가능한 것은 TODO로 찍는다.

```sh
#!/bin/bash
# 09_preflight.sh: everything checkable BEFORE the board arrives. PASS/WARN/FAIL per line.
UBO=${UBO:-/var/tmp/S3/uboot}; IMG=${1:-/var/tmp/S3/img/beagleplay-sd.img}
KO=${KO:-/var/tmp/S3/kdtb}; DTB=$KO/arch/arm64/boot/dts/ti/k3-am625-beagleplay.dtb
r() { printf '%-5s %s\n' "$1" "$2"; }
chk() { if eval "$2"; then r PASS "$1"; else r "${3:-FAIL}" "$1"; fi; }
magic() { xxd -p -l "$3" -s "$2" "$1" 2>/dev/null; }
chk "tiboot3.bin is X.509 DER (ROM needs a cert header)" '[ "$(magic $UBO/r5/tiboot3.bin 0 2)" = 3082 ]'
chk "tispl.bin is a FIT (d00dfeed)" '[ "$(magic $UBO/a53/tispl.bin 0 4)" = d00dfeed ]'
chk "u-boot.img is a FIT (d00dfeed)" '[ "$(magic $UBO/a53/u-boot.img 0 4)" = d00dfeed ]'
chk "no faked TI blobs in R5 build (ti-fs-firmware)" '! grep -q "faked external blobs" $UBO/r5/build.log'
chk "no faked blobs in A53 build (DM, OP-TEE)" '! grep -q "faked external blobs" $UBO/a53/build.log'
chk "Image is arm64 (ARMd magic at 0x38)" '[ "$(magic /var/lab/out/Image 56 4)" = 41524d64 ]'
chk "board DTB model = BeaglePlay" '[ "$(fdtget $DTB / model)" = "BeagleBoard.org BeaglePlay" ]'
chk "console alias serial2 -> main_uart0 @0x2800000" '[ "$(fdtget $DTB /aliases serial2)" = /bus@f0000/serial@2800000 ]'
chk "kernel config: 8250_OMAP, SDHCI_AM654, BMI160_I2C" \
    'grep -q "CONFIG_SERIAL_8250_OMAP=y" $KO/.config && grep -q "CONFIG_MMC_SDHCI_AM654=y" $KO/.config && grep -q "CONFIG_BMI160_I2C=m" $KO/.config'
D=$(sfdisk -d "$IMG" 2>/dev/null)
chk "SD p1 starts at 1 MiB, type c (FAT32 LBA), bootable" 'echo "$D" | grep -q "img1 : start= *2048, size= *262144, type=c, bootable"'
chk "SD p1 holds tiboot3.bin/tispl.bin/u-boot.img" \
    'MTOOLS_SKIP_CHECK=1 mdir -b -i $IMG@@1M :: 2>/dev/null | grep -c -E "tiboot3.bin|tispl.bin|u-boot.img" | grep -qx 3'
ID=$(echo "$D" | awk '/label-id/ {print $2}' | sed 's/0x//')
X=$(MTOOLS_SKIP_CHECK=1 mtype -i $IMG@@1M ::/extlinux/extlinux.conf 2>/dev/null)
chk "extlinux root=PARTUUID matches disk id $ID" 'echo "$X" | grep -q "root=PARTUUID=$ID-02"'
chk "extlinux console=ttyS2 (serial2 alias)" 'echo "$X" | grep -q "console=ttyS2,115200"'
for t in picocom dfu-util openocd sigrok-cli pulseview; do
  chk "host tool: $t" "command -v $t > /dev/null" WARN
done
echo "---- manual (board needed):"
for m in "USB-UART is 3.3 V (NOT 5 V / RS-232)" "TX->RX, RX->TX, GND->GND; leave VCC open" \
         "USB-C 5 V supply (>= 3 A, verify in board docs), red power LED on" "hold USR while powering on for SD boot" \
         "LA ground clipped to board GND; probe on a header pin, not on SoC balls"; do
  r TODO "$m"
done
```
```text
PASS  tiboot3.bin is X.509 DER (ROM needs a cert header)
PASS  tispl.bin is a FIT (d00dfeed)
PASS  u-boot.img is a FIT (d00dfeed)
FAIL  no faked TI blobs in R5 build (ti-fs-firmware)
FAIL  no faked blobs in A53 build (DM, OP-TEE)
PASS  Image is arm64 (ARMd magic at 0x38)
PASS  board DTB model = BeaglePlay
PASS  console alias serial2 -> main_uart0 @0x2800000
PASS  kernel config: 8250_OMAP, SDHCI_AM654, BMI160_I2C
PASS  SD p1 starts at 1 MiB, type c (FAT32 LBA), bootable
PASS  SD p1 holds tiboot3.bin/tispl.bin/u-boot.img
PASS  extlinux root=PARTUUID matches disk id 53335333
PASS  extlinux console=ttyS2 (serial2 alias)
PASS  host tool: picocom
WARN  host tool: dfu-util
WARN  host tool: openocd
WARN  host tool: sigrok-cli
WARN  host tool: pulseview
---- manual (board needed):
TODO  USB-UART is 3.3 V (NOT 5 V / RS-232)
TODO  TX->RX, RX->TX, GND->GND; leave VCC open
TODO  USB-C 5 V supply (>= 3 A, verify in board docs), red power LED on
TODO  hold USR while powering on for SD boot
TODO  LA ground clipped to board GND; probe on a header pin, not on SoC balls
```
출력에서 볼 것: **FAIL 두 줄은 의도된 정직한 결과**다 — 지금 이미지는 TIFS · DM · OP-TEE가 가짜라 보드에서 부팅되지 않는다. 3.3절의 가이드대로 진짜 블롭으로 다시 빌드하면 PASS가 돼야 하고, 그 전에는 카드에 쓰지 않는다. WARN은 Mac/VM에 설치할 도구 목록이다. (이 체크는 VM에서 돌렸으므로 `picocom`은 VM 쪽 결과다. Mac에도 따로 확인한다.)

**체크포인트**: FAIL 0개(진짜 블롭 빌드 후), WARN은 이해하고 넘어간 것만, TODO는 보드 앞에서 한 줄씩 지운다.

---

## 11. 보드가 온 날 — 순서대로 (전부 가이드(실행 안 함 — 보드 필요))

### 11.0 배선

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" markerWidth="10" markerHeight="10" refX="9" refY="5" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#888"/></marker></defs>
<rect x="10" y="140" width="96" height="80" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="22" y="174" font-size="13">Mac 호스트</text>
<text x="22" y="194" font-size="12">USB 포트 3개</text>
<rect x="150" y="30" width="160" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="160" y="52" font-size="13">USB-UART 3.3 V</text>
<text x="160" y="72" font-size="12">FT232R / CP2102</text>
<rect x="150" y="150" width="160" height="56" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="160" y="172" font-size="13">JTAG 프로브 (선택)</text>
<text x="160" y="192" font-size="12">FT2232H / XDS110</text>
<rect x="150" y="262" width="160" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="160" y="284" font-size="13">로직 분석기 8ch</text>
<text x="160" y="304" font-size="12">sigrok / Saleae</text>
<line x1="106" y1="160" x2="148" y2="60" stroke="#888" marker-end="url(#ah2)"/>
<line x1="106" y1="180" x2="148" y2="178" stroke="#888" marker-end="url(#ah2)"/>
<line x1="106" y1="200" x2="148" y2="290" stroke="#888" marker-end="url(#ah2)"/>
<rect x="420" y="16" width="250" height="350" fill="#888" fill-opacity="0.06" stroke="currentColor"/>
<text x="432" y="36" font-size="13">BeaglePlay (AM625)</text>
<rect x="432" y="48" width="226" height="52" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="442" y="68" font-size="12">디버그 UART 헤더 (main_uart0)</text>
<text x="442" y="88" font-size="12">TX · RX · GND → ttyS2, 115200</text>
<rect x="432" y="150" width="226" height="52" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="442" y="170" font-size="12">TC2050 Tag-Connect 패드</text>
<text x="442" y="190" font-size="12">JTAG (TRST 납땜 주의, 문서)</text>
<rect x="432" y="244" width="226" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="442" y="264" font-size="12">mikroBUS: GPIO1_9 · SCL · SDA</text>
<text x="442" y="284" font-size="12">main_i2c3 (i2c-3), BMI160 0x68</text>
<rect x="432" y="316" width="226" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="442" y="341" font-size="12">USB-C 5 V 전원 · USR 버튼</text>
<line x1="310" y1="58" x2="430" y2="74" stroke="#4a7bd0" stroke-width="2"/>
<text x="318" y="52" font-size="12">TX↔RX 교차, GND</text>
<line x1="310" y1="178" x2="430" y2="176" stroke="#e08a3c" stroke-width="2"/>
<text x="318" y="170" font-size="12">TC2050-IDC-NL</text>
<line x1="310" y1="278" x2="430" y2="262" stroke="#3f9a6b" stroke-width="2"/>
<line x1="310" y1="290" x2="430" y2="276" stroke="#3f9a6b" stroke-width="2"/>
<line x1="310" y1="302" x2="430" y2="290" stroke="#3f9a6b" stroke-width="2"/>
<text x="318" y="246" font-size="12">CH0 · CH1 · CH2</text>
<text x="318" y="336" font-size="12">GND 클립 필수</text>
<text x="10" y="372" font-size="12">VCC 핀은 연결하지 않는다</text>
</svg>
```
그림 5 — 배선도. UART는 반드시 3.3 V TTL, TX와 RX를 교차, GND 공통, 어댑터의 VCC는 비워 둔다. LA의 CH0은 토글할 GPIO, CH1 · CH2는 I2C SCL · SDA(디코드용). 헤더 핀 배치는 BeaglePlay 문서 · 회로도로 확인한다.

### 11.1 순서 (각 단계 = 들어가는 조건 · 나오는 조건, P1 형식)

1. **전원만** — 가이드(실행 안 함 — 보드 필요). SD 없이 USB-C 전원. 빨간 전원 LED 확인. 안 켜지면 전원부터(문서의 경고 그대로).
2. **콘솔 연결 · stock 부팅으로 golden log** — 가이드(실행 안 함 — 보드 필요). 공장 eMMC의 배포 이미지로 부팅하며 로그를 저장한다. 이것이 "정상 보드"의 기준선이다.

```sh
# 가이드(실행 안 함 — 보드 필요): Mac에서
ls /dev/cu.usbserial-*                                  # 어댑터 이름 확인
stty -f /dev/cu.usbserial-XXXX 115200 raw
cat /dev/cu.usbserial-XXXX | python3 ~/board-bringup/tools/ts_capture.py | tee logs/golden-boot.log
# 다른 창: picocom -b 115200 /dev/cu.usbserial-XXXX  (로그인 후)
cat /proc/device-tree/model; uname -a; cat /proc/cmdline
ls /dev/ttyS* /dev/i2c-*; i2cdetect -l; gpioinfo | grep -i mikrobus
dmesg > logs/golden-dmesg.txt
```
나오는 조건: R5 SPL · A53 SPL · U-Boot · 커널 배너가 모두 보이고, `logs/golden-*.txt`가 저장됐다. 배포 이미지의 기본 계정은 기억 기반(확인 필요)이라 적지 않는다 — 보드 문서를 본다.

3. **SD 이미지 쓰기** — 가이드(실행 안 함 — 보드 필요). 진짜 블롭으로 다시 빌드해 pre-flight FAIL이 0인 이미지만 쓴다.

```sh
# 가이드(실행 안 함 — 보드 필요): Mac. diskN을 잘못 고르면 Mac 디스크가 지워진다 -> diskutil list로 두 번 확인
limactl copy elinux:/var/tmp/S3/img/beagleplay-sd.img ./beagleplay-sd.img
diskutil list external
diskutil unmountDisk /dev/diskN
sudo dd if=beagleplay-sd.img of=/dev/rdiskN bs=4m && sync
diskutil eject /dev/diskN
```

4. **SD 부팅** — 가이드(실행 안 함 — 보드 필요). USR 버튼을 누른 채 전원 → LED 켜지면 뗀다(문서). 기대하는 콘솔: `U-Boot SPL 2026.10`(R5) → TIFS · DM 로드 → `U-Boot SPL 2026.10`(A53) → `U-Boot 2026.10` → `bootflow scan` → `Retrieving file: /Image` → 커널. LED 표(문서): 11111이면 A53 SPL/U-Boot까지 왔고, 00000이면 R5 단계에서 멈춘 것.
5. **U-Boot 프롬프트에서 확인** — 가이드(실행 안 함 — 보드 필요). 자동 부팅을 멈추고 C3의 "첫날 명령"을 돌린다: `bdinfo`, `mmc list`, `mmc dev 1; mmc part`, `bootflow scan -l`, `fdt addr $fdtcontroladdr; fdt print /chosen`, `bootstage report`(M10 §2). 8.1절 리허설과 같은 출력이 나와야 한다.
6. **커널 → BusyBox 셸** — 가이드(실행 안 함 — 보드 필요). `cat /proc/device-tree/model`, `ls /sys/bus/i2c/devices/` 에 `3-0068`이 있으면 overlay가 먹은 것. BMI160을 실제로 꽂았다면 `modprobe bmi160_i2c` 후 `/sys/bus/iio/devices/iio:device0/name`(BusyBox 루트에는 모듈이 없으니 `modules_install`을 rootfs에 넣는 것이 다음 과제 — 12절).
7. **eMMC로 옮기기(선택)** — 가이드(실행 안 함 — 보드 필요). U-Boot 문서의 `mmc bootpart enable 1 2 /dev/mmcblk0` → boot0에 `tiboot3.bin`, UDA BOOT 파티션에 나머지. SD가 꽂혀 있으면 U-Boot가 SD를 먼저 고른다는 문서 경고를 기억한다.
8. **JTAG** — 가이드(실행 안 함 — 보드 필요). U-Boot 문서의 OpenOCD 설정을 그대로 쓴다:

```text
source [find interface/ftdi/tumpa.cfg]
transport select jtag
reset_config srst_only srst_push_pull
adapter srst delay 20
if { ![info exists SOC] } {
  set SOC am625
}
source [find target/ti_k3.cfg]
ftdi tdo_sample_edge falling
adapter speed 16000
```
(U-Boot `doc/board/beagle/am62x_beagleplay.rst`의 예시에서 주석을 뺀 것.) `openocd -f openocd_connect.cfg` 후 gdb로 R5 SPL(`r5/spl/u-boot-spl`, ARM 32비트 ELF)이나 A53 U-Boot(`a53/u-boot`) 심볼을 올린다 — 두 ELF 모두 3.2절 빌드에 `with debug_info, not stripped`로 있다(실측). OpenOCD는 v0.12.0 이상이 필요하다(문서).

### 11.2 측정 과제 (가이드(실행 안 함 — 보드 필요))

| # | 측정 | 방법 | 보여 줄 숫자 |
|---|---|---|---|
| 1 | 부팅 시간 단계별 | `ts_capture.py` + `boot_time.py` (8.2절과 같은 툴), U-Boot `bootstage report`, 커널 `printk.time` | ROM→R5 SPL, R5 SPL→A53 SPL, U-Boot, 커널, 셸 각각 ms. bootdelay 0으로 줄인 전후 |
| 2 | 부팅 시간을 핀으로 | U-Boot `led`/`gpio set` 명령과 `/init` 첫 줄에서 GPIO1_9 토글 → LA CH0 | 전원 인가(LA로 5 V 레일)부터 각 엣지까지 — 호스트 시계 오차 없음 (M11 §5) |
| 3 | GPIO 쓰기 비용 | `gpio_toggle <chip> 9 100000` + LA로 엣지 간격 | ioctl 평균/최대(소프트웨어) vs LA 엣지 간격 분포(하드웨어) |
| 4 | 주기 토글 지터 | `gpio_toggle <chip> 9 20000 1000`, `stress-ng` 부하 유무, PREEMPT_RT 커널(S5) | 1 ms 주기의 최대 지연, 히스토그램 |
| 5 | I2C 실제 클럭 | BMI160 읽기 중 LA I2C 디코드 | SCL 주파수(DT의 400 kHz와 비교), 트랜잭션 간 간격 |

`<chip>`은 `gpioinfo`에서 `MIKROBUS_GPIO1_9`가 보이는 칩(DT상 `main_gpio1`)이다. 측정 2번은 Don이 Apple에서 매일 하는 "scope로 소프트웨어 이벤트 증명"과 같다 — 이 프로젝트에서 가장 강한 면접 소재다.

### 11.3 다른 보드라면 — 부트 흐름 비교

```svg
<svg viewBox="0 0 680 352" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="#888"/></marker></defs>
<text x="20" y="22" font-size="13">BeaglePlay (AM625) — 전부 mainline, 블롭은 TIFS · DM만</text>
<rect x="20" y="32" width="120" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="30" y="50" font-size="12">ROM (R5)</text><text x="30" y="68" font-size="12">SD FAT 읽기</text>
<rect x="152" y="32" width="120" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="162" y="50" font-size="12">tiboot3.bin</text><text x="162" y="68" font-size="12">R5 SPL + TIFS</text>
<rect x="284" y="32" width="120" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="294" y="50" font-size="12">R5 SPL</text><text x="294" y="68" font-size="12">DDR, tispl</text>
<rect x="416" y="32" width="120" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="426" y="50" font-size="12">A53: BL31</text><text x="426" y="68" font-size="12">OP-TEE · SPL</text>
<rect x="548" y="32" width="120" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="558" y="50" font-size="12">U-Boot proper</text><text x="558" y="68" font-size="12">→ extlinux</text>
<line x1="140" y1="54" x2="150" y2="54" stroke="#888" marker-end="url(#ah3)"/>
<line x1="272" y1="54" x2="282" y2="54" stroke="#888" marker-end="url(#ah3)"/>
<line x1="404" y1="54" x2="414" y2="54" stroke="#888" marker-end="url(#ah3)"/>
<line x1="536" y1="54" x2="546" y2="54" stroke="#888" marker-end="url(#ah3)"/>
<text x="284" y="94" font-size="12">R5는 이후 DM 펌웨어로 남아 클럭 · 전원 요청을 처리</text>
<text x="20" y="124" font-size="13">Raspberry Pi 5 — 앞단은 비공개 펌웨어, U-Boot는 선택</text>
<rect x="20" y="134" width="120" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="30" y="152" font-size="12">BootROM</text><text x="30" y="170" font-size="12">SPI EEPROM</text>
<rect x="152" y="134" width="120" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="162" y="152" font-size="12">EEPROM 부트로더</text><text x="162" y="170" font-size="12">config.txt</text>
<rect x="284" y="134" width="120" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="294" y="152" font-size="12">펌웨어가</text><text x="294" y="170" font-size="12">DTB+overlay</text>
<rect x="416" y="134" width="120" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="426" y="152" font-size="12">(선택) U-Boot</text><text x="426" y="170" font-size="12">kernel=u-boot</text>
<rect x="548" y="134" width="120" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="558" y="152" font-size="12">Linux</text><text x="558" y="170" font-size="12">kernel_2712</text>
<line x1="140" y1="156" x2="150" y2="156" stroke="#888" marker-end="url(#ah3)"/>
<line x1="272" y1="156" x2="282" y2="156" stroke="#888" marker-end="url(#ah3)"/>
<line x1="404" y1="156" x2="414" y2="156" stroke="#888" marker-end="url(#ah3)"/>
<line x1="536" y1="156" x2="546" y2="156" stroke="#888" marker-end="url(#ah3)"/>
<text x="20" y="214" font-size="13">Jetson Orin Nano — NVIDIA 부트 체인 + UEFI, L4T BSP</text>
<rect x="20" y="224" width="120" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="30" y="242" font-size="12">BootROM</text><text x="30" y="260" font-size="12">QSPI 플래시</text>
<rect x="152" y="224" width="120" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="162" y="242" font-size="12">MB1 · MB2</text><text x="162" y="260" font-size="12">BPMP 등 초기화</text>
<rect x="284" y="224" width="120" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="294" y="242" font-size="12">UEFI (EDK2)</text><text x="294" y="260" font-size="12">NVIDIA 빌드</text>
<rect x="416" y="224" width="120" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="426" y="242" font-size="12">L4TLauncher</text><text x="426" y="260" font-size="12">extlinux.conf</text>
<rect x="548" y="224" width="120" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="558" y="242" font-size="12">Linux</text><text x="558" y="260" font-size="12">L4T 커널</text>
<line x1="140" y1="246" x2="150" y2="246" stroke="#888" marker-end="url(#ah3)"/>
<line x1="272" y1="246" x2="282" y2="246" stroke="#888" marker-end="url(#ah3)"/>
<line x1="404" y1="246" x2="414" y2="246" stroke="#888" marker-end="url(#ah3)"/>
<line x1="536" y1="246" x2="546" y2="246" stroke="#888" marker-end="url(#ah3)"/>
<text x="20" y="298" font-size="12">초록 = 직접 빌드 가능(이 노트에서 빌드), 주황 · 빨강 = 벤더 바이너리</text>
<text x="20" y="316" font-size="12">파랑 = 공개 소스지만 벤더가 빌드해 주는 경우가 많음</text>
<text x="20" y="340" font-size="12">RPi 5 · Jetson 단계 이름은 기억 기반 — 공식 문서로 확인 필요 (C8 참조)</text>
</svg>
```
그림 6 — 보드별 부트 흐름. BeaglePlay는 다섯 칸 모두를 이 노트에서 소스로 빌드했고, RPi 5는 앞 세 칸, Jetson은 앞 세 칸이 벤더 바이너리다.

| 단계 | BeaglePlay | Raspberry Pi 5 (기억 기반 — 확인 필요) | Jetson Orin Nano (기억 기반 — 확인 필요) |
|---|---|---|---|
| 콘솔 | 디버그 UART 헤더(핀 배치는 BeaglePlay 문서 확인), `ttyS2` 115200 | 3핀 JST-SH 디버그 UART(DT alias `serial10` 실측), Debug Probe | 보드 헤더의 UART, mainline DT는 TCU(`/serial`, 실측) → `ttyTCU0` |
| 이미지 쓰기 | SD에 dd, eMMC는 `mmc bootpart` | SD에 dd, 부트로더는 `rpi-eeprom` 도구로 EEPROM 갱신 | USB 리커버리 모드 + x86_64 Ubuntu 호스트의 `flash.sh`/`l4t_initrd_flash.sh` |
| 부트로더 설정 | `extlinux.conf` (bootstd) | FAT의 `config.txt`: `arm_64bit=1`, `kernel=…`, `dtoverlay=…`, `enable_uart=1` | `/boot/extlinux/extlinux.conf` (L4TLauncher가 읽음) + UEFI 변수 |
| 커널 | mainline 6.18 그대로 | mainline도 부팅은 되지만 카메라 · RP1 주변장치는 RPi 커널(`rpi-6.x.y`) 권장 | L4T 커널(JetPack 버전에 묶임) — mainline은 GPU · 카메라 없음 |
| DT overlay | extlinux `fdtoverlays` 또는 U-Boot `fdt apply` | `config.txt`의 `dtoverlay=` (펌웨어가 적용) | L4T의 `jetson-io.py` 또는 extlinux `OVERLAYS` (D6 §8) |
| 첫 측정 | 11.2절 그대로 | 같은 툴, GPIO가 PCIe 너머 RP1에 있어 지연이 더 클 수 있음(추정) — 측정 거리 | 같은 툴, 40핀 헤더 GPIO |

---

## 12. 확장 과제

1. **진짜 블롭으로 재빌드** — `ti-linux-firmware` + OP-TEE를 넣고 9절 FAIL 0 만들기(3.3절 가이드).
2. **커널 직접 빌드** — `configs/s3_beagleplay.config`를 합친 `/var/tmp/S3/kdtb`에서 `make -j2 Image modules`, `make modules_install INSTALL_MOD_PATH=<rootfs>`로 p2에 모듈을 넣기. (이 노트에서는 DTB만 빌드했다 — 전체 빌드는 VM 공유 CPU 때문에 일부러 생략.)
3. **Debian rootfs로 교체** — K6의 바이너리 배포판 방식(`debootstrap --foreign` 또는 배포판 rootfs tarball)으로 p2를 바꾸고 `systemd-analyze`로 유저 공간 구간 측정(M10 §7).
4. **A/B 부팅** — p3를 하나 더 만들어 extlinux 라벨 두 개 + U-Boot `bootcount`(L4).
5. **CI화** — USB-SD-Mux + `ts_capture.py` + `boot_time.py`를 묶어 "커밋마다 보드 부팅 → TAP 결과"(P6).
6. **S4 연결** — in-tree `bmi160_i2c`가 probe되는 것을 확인한 뒤 S4의 자작 IIO 드라이버로 교체, LA로 IRQ→타임스탬프 지연 측정.
7. **두 번째 보드** — Jetson Orin Nano를 사면 같은 `dtb_inventory.py`를 L4T의 DTB에 돌려 mainline과 노드 수 비교(4.3절 표의 "CSI 0/0"이 얼마로 바뀌나).

---

## 13. 면접 · GitHub에서 보여 주는 법

### 13.1 README 개요

- 제목: "BeaglePlay (TI AM625) bring-up from mainline: TF-A 2.15 + U-Boot 2026.10 + Linux 6.18"
- 왜 이 보드인가(1절 표 축약, mainline 점수 근거)
- 부트 체인 그림(그림 6 첫 줄)과 각 산출물 크기(tiboot3 126 KB, tispl 240 KB, u-boot.img 1.2 MB)
- 재현 방법: `scripts/01` → `09` 순서, 필요한 블롭 목록과 출처
- 결과: golden log vs mainline log diff, 부팅 단계 표(보드 실측), GPIO 지연 히스토그램, LA 스크린샷
- 알려진 한계: CAN 트랜시버 없음, NPU 없음, TCG 리허설 수치는 HW 아님

### 13.2 데모 스크립트 (3분)

1. `scripts/09_preflight.sh` 실행 화면 — "보드 오기 전에 여기까지 검증했다"
2. 콘솔 녹화: 전원 → R5 SPL 배너 → U-Boot → 커널 → 셸 (LED 상태 설명)
3. `ls /sys/bus/i2c/devices/3-0068` → overlay가 만든 장치
4. LA 화면: GPIO 엣지와 콘솔 타임스탬프 비교

### 13.3 보여 줄 숫자

| 숫자 | 어디서 |
|---|---|
| 부트로더 빌드 시간 · 산출물 크기 | 3절 (VM 실측) |
| 단계별 부팅 시간(ms), bootdelay 제거 전후 | 11.2절 측정 1 (보드) |
| GPIO 쓰기 ioctl 평균/최대, LA 엣지 간격 | 11.2절 측정 3 (보드) |
| 1 ms 주기 토글 최대 지연, RT 커널 전후 | 11.2절 측정 4 + S5 |
| I2C 실측 SCL 주파수 | 11.2절 측정 5 |

---

## 14. 커널 안에서는 무슨 일이 일어나나

이 프로젝트에서 만든 파일이 커널의 어디로 들어가는지 정리한다.

- **DTB → `unflatten_device_tree()` → `of_platform_populate()`**: U-Boot가 extlinux의 `fdt` + `fdtoverlays`를 합쳐 메모리에 둔 DTB 주소를 x0 레지스터로 넘긴다(C6). 커널은 `setup_arch()`에서 펼치고, `/bus@f0000` 아래 노드마다 `platform_device`를 만든다(D3).
- **`serial@2800000` → `8250_omap` probe → `ttyS2`**: alias `serial2`가 포트 번호를 정한다. probe 전에 `power-domains`의 TI SCI 요청(`ti_sci_pm_domain`)이 DM 펌웨어로 간다 — 그래서 DM 블롭이 가짜면 커널 UART가 안 뜬다(4.4절). earlycon(`ns16550a,mmio32,0x02800000`)은 이 모든 것 이전에 직접 레지스터를 쓰므로 DM 없이도 첫 메시지를 낼 수 있다(M1) — bring-up 첫날 "earlycon은 나오는데 정식 콘솔로 넘어가며 끊긴다"면 이 경로를 의심한다.
- **`imu@68` → i2c core가 `i2c_client` 생성 → `bmi160_i2c` probe**: `main_i2c3` 어댑터(compatible `ti,am64-i2c`, `ti,omap4-i2c` → `drivers/i2c/busses/i2c-omap.c`, 트리에서 확인)가 등록될 때 자식 노드를 `of_i2c_register_devices()`로 클라이언트로 만든다(H5). 드라이버 모듈이 로드되면 `of_match_table`의 `bosch,bmi160`과 매칭된다. S4에서는 여기에 regmap(H2) · IIO buffer가 얹힌다.
- **`root=PARTUUID=53335333-02` → `name_to_dev_t()` 계열 → `mount_root`**: 커널은 MBR 디스크 ID + 파티션 번호로 블록 장치를 찾는다. `rootwait`는 SD 컨트롤러(`sdhci-am654`) probe가 늦어도 기다리게 한다(C6, J2).
- **`gpio_toggle`의 ioctl → gpiolib cdev → `gpio_chip->set()`**: `GPIO_V2_LINE_SET_VALUES_IOCTL`은 `linereq_set_values()`를 거쳐 칩 드라이버(`main_gpio1`의 compatible `ti,am64-gpio`, `ti,keystone-gpio` → `drivers/gpio/gpio-davinci.c`, 트리에서 확인)의 레지스터 쓰기로 끝난다(H4). LA로 재는 엣지 간격 = syscall 진입 + gpiolib + MMIO 쓰기 + 반환의 합이다.

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 5 V USB-UART 사용 | 콘솔 깨짐, 최악은 UART 핀 손상 | AM625 IO는 3.3 V | 3.3 V TTL 어댑터, 연결 전 멀티미터로 TX 전압 확인 |
| TX-TX 직결 | 아무 출력 없음 | 교차 안 함 | TX↔RX 교차, GND 공통 |
| 가짜 블롭 이미지를 카드에 씀 | USR LED 전부 꺼짐, 콘솔 무출력 | TIFS 0바이트 | `09_preflight.sh` FAIL 0 확인 후 쓰기 |
| `tiboot3.bin` 이름 변경 · 다른 파티션 | ROM이 부팅 안 함 | ROM · SPL은 파일 이름과 파티션 1을 고정으로 찾는다 | 3.3절 `SPL_FS_LOAD_PAYLOAD_NAME` · `SYS_MMCSD_FS_BOOT_PARTITION` 확인 |
| FAT 파티션 boot 플래그 누락 | ROM SD FS 모드 실패 또는 bootflow가 다른 파티션 스캔 | MBR active 플래그 없음 | sfdisk 스크립트의 `bootable` |
| `root=/dev/mmcblk0p2` | 커널이 eMMC를 루트로 잡거나 `VFS: Unable to mount root` | alias상 SD는 `mmc1` | `root=PARTUUID=…` |
| `fdt resize` 없이 `fdt apply` | `failed on fdt_overlay_apply(): FDT_ERR_NOSPACE` (8.1절 실측) | DTB 끝에 여유 공간 없음 | `fdt resize 8192` (8.1절) |
| base DTB를 `-@` 없이 빌드 | `fdtoverlay`가 `FDT_ERR_BADOFFSET` + "does not have a `/__symbols__` node" (8.1절 실측) | `__symbols__` 없음 | 커널 TI Makefile은 `-@` — 직접 dtc로 빌드할 때 주의 (D6 §2) |
| ROCK 5B를 115200으로 접속 | 깨진 글자 | 콘솔이 1.5 Mbaud | DTB `stdout-path` 확인(4.3절) |
| binman `yamllint` 없음 | `Unknown entry type 'ti-board-config'` | Python 의존성 | venv에 `yamllint` (3.2절) |

---

## 16. 면접에서 이렇게 말한다

**Q.** Walk me through the boot flow of a TI AM62x board from power-on to Linux.

**A.** ROM이 R5에서 돌며 SD/eMMC에서 `tiboot3.bin`을 읽는다. 그 안의 X.509 인증서를 검사하고, R5 SPL과 보안 펌웨어 TIFS를 올린다. R5 SPL이 DDR을 켜고 `tispl.bin`(FIT: TF-A BL31, OP-TEE, DM 펌웨어, A53 SPL, DT)을 로드한 뒤 A53을 깨우고, 자신은 DM 펌웨어로 남아 클럭 · 전원 요청을 처리한다. A53은 BL31 → OP-TEE → A53 SPL → U-Boot → bootstd가 extlinux를 찾아 커널을 띄운다. 나는 이 체인 전체를 mainline 소스로 직접 빌드했다.

> On AM62x the ROM runs on an R5 core and loads tiboot3.bin, an X.509-wrapped image with the R5 SPL and the TIFS security firmware. The R5 SPL brings up DDR, loads tispl.bin — a FIT with TF-A BL31, OP-TEE, the Device Manager firmware and the A53 SPL — releases the A53 cluster, and then the R5 itself becomes the Device Manager that serves clock and power requests over TI SCI. On the A53 side it's BL31, OP-TEE, SPL, U-Boot, and U-Boot's bootstd finds an extlinux.conf and boots Linux. I built that whole chain from mainline TF-A, U-Boot and kernel sources for BeaglePlay.

**꼬리 질문**: "Why does the kernel UART depend on firmware?" → `power-domains`가 TI SCI로 DM에 요청하기 때문(14절).

**Q.** Which board would you pick to learn embedded Linux bring-up, and why?

**A.** mainline 지원을 숫자로 비교했다. 커널 트리에서 보드 DT의 주요 IP 노드에 in-tree 드라이버가 붙는지 세면 i.MX8MP 6/7, AM625 5/7, RPi 5 2/7, Orin 1/7이다. 가격 · 디버그 접근을 더해 BeaglePlay를 골랐다. AI 쪽은 Jetson이 맞지만 그건 벤더 BSP 경험이고, bring-up 학습용은 아니다.

> I scored candidates by how much of their device tree is backed by in-tree drivers: i.MX8MP 6 of 7 key blocks, AM625 5, Raspberry Pi 5 2, Orin 1. Combined with price and documented UART and JTAG access, BeaglePlay won. Jetson is the right board for AI workloads, but that's vendor-BSP experience, not mainline bring-up.

**Q.** What do you check before you even power a new board?

**A.** 이미지 포맷(인증서 헤더, FIT 매직), 빠진 블롭이 없는지, DT의 콘솔 alias와 baud, 파티션 레이아웃과 PARTUUID, 커널 config에 콘솔 · 스토리지 드라이버가 built-in인지를 스크립트로 확인한다. 그리고 공통 부분(extlinux, overlay 적용)은 QEMU의 U-Boot로 같은 이미지를 읽혀 리허설한다.

> I script a pre-flight: image headers — the X.509 cert on tiboot3, FIT magic on tispl and u-boot.img — no faked firmware blobs, the console alias and baud rate from the DTB, the partition layout and PARTUUID, and that console and storage drivers are built in. Then I rehearse the SoC-independent parts — extlinux and overlay application — by booting the same image file with U-Boot under QEMU.

**Q.** Your board prints nothing on the console. How do you debug?

**A.** 전원 LED · 레일 전압 → UART 레벨(3.3 V)과 TX/RX 교차 → baud(DT의 `stdout-path`) → 부트 모드(USR 버튼, 부트 핀) → 보드 LED 패턴으로 어느 단계에서 멈췄는지(BeaglePlay는 00000이면 R5 단계) → 이미지 문제면 golden 이미지로 되돌아가 하드웨어 문제와 분리 → 그래도 안 되면 JTAG로 SPL에 붙는다.

> Power first — LED and rails. Then the UART: 3.3-volt levels, TX/RX crossed, baud from the DT's stdout-path. Then boot mode straps. BeaglePlay's user LEDs tell me which stage died — all off means the R5 image never started. I swap in a known-good vendor image to separate hardware from my image, and if it's still dark I attach JTAG and halt the R5 SPL.

**Q.** How do you measure GPIO latency from user space?

**A.** v2 GPIO chardev로 라인을 요청해 루프에서 토글하고, 소프트웨어 타임스탬프와 로직 분석기 엣지를 둘 다 기록한다. 소프트웨어가 보는 것은 ioctl 비용, LA가 보는 것은 실제 엣지 간격과 지터다. 주기 토글은 `clock_nanosleep(TIMER_ABSTIME)`으로 하고 늦은 기상을 따로 센다 — 부하와 PREEMPT_RT 유무로 비교한다.

> I request the line through the GPIO v2 character device, toggle it in a loop and record both software timestamps and logic-analyzer edges — software sees the ioctl cost, the analyzer sees the real edge spacing and jitter. For periodic toggling I use clock_nanosleep with an absolute deadline and track wake-up lateness, then compare under load and with PREEMPT_RT.

**Q.** What's the difference between booting Raspberry Pi 5 and a TI K3 board?

**A.** RPi 5는 앞단이 비공개 EEPROM 펌웨어라 `config.txt`로만 제어하고 DT도 펌웨어가 넘긴다(U-Boot `CONFIG_OF_BOARD`). K3는 ROM 이후 전부 공개 소스이고 블롭은 보안 · 전원 펌웨어뿐이다. 그래서 K3에서는 SPL · DDR · TF-A 단계를 내가 디버그할 수 있다.

> On the Pi 5 everything before the kernel or U-Boot is closed EEPROM firmware configured through config.txt, and it even hands over the device tree — U-Boot just uses it. On K3 everything after the ROM is open source except the security and power-management firmware, so I can debug SPL, DDR init and TF-A myself.

**Q.** Why use PARTUUID instead of /dev/mmcblkXpY?

**A.** 블록 장치 번호는 컨트롤러 probe 순서와 DT alias에 따라 바뀐다. BeaglePlay는 alias상 eMMC가 mmc0, SD가 mmc1이지만 다른 보드나 커널에서는 다를 수 있다. 디스크 ID를 sfdisk에서 고정하면 PARTUUID가 결정적이다.

> Device numbering depends on probe order and DT aliases — on BeaglePlay eMMC is mmc0 and SD is mmc1, but that's not universal. I pin the MBR disk ID in the sfdisk script, so the PARTUUID is deterministic.

---

## 17. 직접 해보기

1. **개념**: `tiboot3.bin`의 첫 0x4f1바이트는 무엇이고 누가 검사하나? — 정답/힌트: binman 맵의 `ti-secure-rom` = ROM이 검사하는 X.509 인증서(3.3절).
2. **코드**: `dtb_inventory.py`에 "PWM"과 "GPIO 컨트롤러" 분류를 추가하고 5개 DTB에서 enabled 개수를 비교하라. — 정답/힌트: `pwm`, `gpio-controller` 프로퍼티 유무로 판정.
3. **관찰**: `bp+ov5640.dtb`와 base DTB를 `dtc -I dtb -O dts`로 풀어 `diff` 하라. 어떤 노드의 `status`가 바뀌었나? — 정답/힌트: `ticsi2rx`, 카메라용 I2C 버스, 센서 노드 추가.
4. **코드**: `05_make_sd_image.sh`를 GPT로 바꾼 변형을 만들고 8.1절 U-Boot 리허설에서 `part list`가 어떻게 달라지는지 보라. — 정답/힌트: `label: gpt`, PARTUUID가 GUID로 바뀐다. 실제 AM62 ROM의 GPT 지원은 TRM 확인.
5. **측정**: `07_qemu_gpio_toggle.sh`를 `Image-rt`(PREEMPT_RT)로 바꿔 `max_wake_late`를 비교하라. — 정답/힌트: TCG라 절대값은 의미 없고, 분포 차이만 본다(S5).
6. **조사**: i.MX8MP EVK를 골랐다면 2절 쇼핑 리스트와 3절 빌드 스크립트에서 무엇이 바뀌나? — 정답/힌트: `imx8mp_evk_defconfig`, TF-A `PLAT=imx8mp`, LPDDR4 학습 블롭 4개(5절), 이미지 오프셋(SD 32 KiB 등은 C2 · 문서 확인).
7. **회로도**: BeaglePlay 회로도에서 mikroBUS INT 핀이 `main_gpio1`의 몇 번인지 찾아 overlay의 주석을 실제 값으로 바꿔라. — 정답/힌트: P4 절차, `gpio-line-names`의 `MIKROBUS_GPIO1_*` 중 하나.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| K3 | TI의 64비트 SoC 아키텍처 | AM62/AM64/J7 공통: ROM은 R5, TI SCI로 자원 관리 |
| TIFS | TI Foundational Security | 보안 엔클레이브에서 도는 펌웨어 (블롭) |
| DM | Device Manager (TISCI server) | R5에서 도는 클럭 · 전원 · 자원 관리 펌웨어 (블롭) |
| TI SCI | TI System Control Interface | DT의 `power-domains`/`clocks`가 펌웨어로 보내는 메시지 프로토콜 |
| tiboot3.bin | R5 단계 이미지 | X.509 인증서 + R5 SPL + TIFS + 보드 설정 |
| tispl.bin | A53 단계 FIT | BL31 + OP-TEE + DM + A53 SPL + DT |
| binman | U-Boot 이미지 패키저 | DT로 기술한 레이아웃대로 바이너리를 조립, 빠진 블롭 보고 |
| FIT | Flattened Image Tree | 여러 이미지를 DT 형식으로 묶은 컨테이너 (매직 `d00dfeed`) |
| bootstd | U-Boot 표준 부팅 | `bootflow scan`으로 bootdev · bootmeth(extlinux 등)을 찾는다 |
| extlinux.conf | 부팅 메뉴 파일 | kernel · fdt · fdtoverlays · append 지정 |
| PARTUUID | 파티션 고유 ID | MBR에서는 `디스크ID-파티션번호` |
| earlycon | 초기 콘솔 | 드라이버 probe 전에 UART 레지스터를 직접 쓰는 콘솔 |
| TCU | Tegra Combined UART | Jetson의 펌웨어 중계 콘솔, mainline DT의 `serial0` |
| Tag-Connect | 납땜 없는 디버그 패드 커넥터 | BeaglePlay JTAG는 TC2050 패드 |
| golden log | 정상 기준 로그 | 벤더 이미지로 부팅한 첫 로그, 이후 비교 기준 |

---

## 19. 요약 & 체크리스트

보드가 없어도 bring-up 준비의 대부분은 끝낼 수 있다. Don의 목표 기준으로 보면 BeaglePlay(AM625)가 mainline 지원(O5 실측 5/7), 공개 문서, 문서화된 UART · JTAG, 가격의 균형이 가장 좋고, 부트 체인 전체(TF-A, R5/A53 U-Boot, 커널 DTB)를 이 노트에서 VM으로 실제 빌드했다. 닫힌 부분은 TI의 TIFS · DM 블롭과 OP-TEE 빌드뿐이고, binman과 pre-flight가 그것을 FAIL로 정직하게 보여 준다. SD 이미지 파일의 레이아웃 · extlinux · overlay는 QEMU의 U-Boot로 리허설했고, 부팅 단계 측정 툴과 GPIO 토글 툴도 미리 검증했다. 보드가 오면 남은 일은 배선, golden log, 진짜 블롭 이미지 쓰기, 그리고 LA로 숫자를 얻는 것이다. Jetson Orin Nano는 AI 직무를 위한 두 번째 보드로 남겨 둔다 — mainline DT에 카메라 · CAN 노드가 없고 TF-A도 mainline에 없다는 것을 트리로 확인했다.

- [ ] 후보 보드 5종을 mainline · 문서 · 디버그 · 가격 · 카메라/CAN · 직무 연관으로 비교하고 BeaglePlay를 고른 근거를 말할 수 있다
- [ ] AM625 부트 체인(ROM/R5 → R5 SPL+TIFS → BL31/OP-TEE/DM/A53 SPL → U-Boot → extlinux)을 그림 없이 설명할 수 있다
- [ ] `am62x_beagleplay_r5_defconfig` / `_a53_defconfig`로 U-Boot를 두 번 빌드하고 BL31을 넘길 수 있다
- [ ] binman의 missing/faked 블롭 메시지를 읽고 어떤 파일이 어디서 와야 하는지 말할 수 있다
- [ ] `make <vendor>/<board>.dtb`로 DTB만 빌드하고 `fdtget`/`dtc`로 콘솔 · I2C · CAN · CSI 노드를 확인할 수 있다
- [ ] overlay를 `-@`로 컴파일해 `fdtoverlay`와 U-Boot `fdt apply` 두 방식으로 적용할 수 있다
- [ ] sfdisk 스크립트로 MBR + active FAT + ext4 이미지 파일을 만들고 PARTUUID를 고정할 수 있다
- [ ] 같은 이미지를 QEMU의 U-Boot로 읽혀 bootflow · overlay를 검증할 수 있다
- [ ] 3.3 V USB-UART 배선(TX/RX 교차, GND, VCC 미연결)과 BeaglePlay SD 부팅(USR 버튼)을 설명할 수 있다
- [ ] 부팅 시간을 콘솔 타임스탬프와 GPIO+LA 두 방법으로 재는 계획을 세울 수 있다
- [ ] RPi 5 · Jetson의 부트 흐름이 BeaglePlay와 어떻게 다른지(펌웨어 DT, UEFI/L4T) 말할 수 있다

## 참고 자료

- U-Boot 문서 — AM62x BeaglePlay: 트리의 `doc/board/beagle/am62x_beagleplay.rst` (온라인: https://docs.u-boot.org/en/latest/board/beagle/am62x_beagleplay.html)
- U-Boot 문서 — K3 공통 빌드 절차 · OpenOCD: 트리의 `doc/board/ti/k3.rst`
- U-Boot 문서 — Raspberry Pi: `doc/board/broadcom/raspberrypi.rst`, i.MX8MP EVK: `doc/board/nxp/imx8mp_evk.rst`, Rockchip: `doc/board/rockchip/rockchip.rst`
- U-Boot 문서 — Standard Boot(bootstd): `doc/develop/bootstd/` (온라인 docs.u-boot.org의 "Standard Boot")
- TF-A 문서 — TI K3 플랫폼: 트리의 `docs/plat/ti-k3.rst` (온라인: https://trustedfirmware-a.readthedocs.io)
- Linux 6.18.55 트리: `arch/arm64/boot/dts/ti/k3-am625-beagleplay.dts`, `drivers/iio/imu/bmi160/`, `Documentation/devicetree/overlay-notes.rst`, `Documentation/userspace-api/gpio/`
- BeaglePlay 문서: https://docs.beagleboard.org (BeaglePlay 페이지, 시리얼 콘솔 튜토리얼), 하드웨어 저장소 https://git.beagleboard.org/beagleplay/beagleplay
- Raspberry Pi 문서: https://www.raspberrypi.com/documentation/ (`config.txt`, 부트 EEPROM)
- NVIDIA Jetson Linux Developer Guide (docs.nvidia.com의 Jetson Linux 문서 — 부트 흐름, 플래싱, extlinux)
- OpenOCD 사용자 가이드: https://openocd.org/doc/html/index.html
- sigrok / PulseView: https://sigrok.org
- 이 사이트의 관련 노트: C1–C5, C8, D5–D7, B1, B5, K1, K2, K6, H4, H5, H8, M1, M4, M10, M11, N3, O5, P1, P2, P4, P6, S1, S4, S5, S7
