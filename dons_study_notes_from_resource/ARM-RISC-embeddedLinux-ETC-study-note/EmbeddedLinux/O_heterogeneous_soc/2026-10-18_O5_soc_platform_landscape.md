# O5. 대표 SoC 플랫폼 지형 — NXP · TI · Rockchip · Qualcomm · NVIDIA · Raspberry Pi · ST · Renesas를 커널 트리로 검증하며 비교하기

> **이 노트를 다 읽으면**: 8개 SoC 패밀리(19개 칩)의 CPU · 보조 코어 · NPU/GPU/ISP · 카메라 · CAN-FD · BSP 이름을 한 장 표로 말할 수 있다 · "이 칩 mainline 잘 돼요?"라는 질문에 감이 아니라 커널 트리 grep(DT 노드 + 드라이버 compatible 매칭)으로 답하는 스크립트를 짤 수 있다 · 로봇 센서 박스용 SoC를 기준(AI 성능, 카메라 수, 실시간/CAN, mainline, 공급 수명, 비용)으로 고르고 그 이유를 30초에 말할 수 있다 · 면접관에게 그 회사 스택에 대해 좋은 질문을 던질 수 있다
> **왜 필요한가**: 로보틱스 · 국방 · 자동차 회사 면접은 "우리는 Jetson Orin + STM32를 쓴다", "다음 세대는 TI TDA4로 갈까 고민 중" 같은 말로 시작한다. 그때 칩 이름만 듣고 "아, 그건 A78AE 12코어에 BSP가 L4T라 커널이 벤더 트리에 묶여 있죠" 수준으로 받아치면 대화의 주도권이 생긴다. 실무에서는 새 제품의 SoC 선정 회의에서 바로 이 비교표가 필요하다
> **Don 기준 난이도**: SoC 블록(코어 · DMA · 인터커넥트 · PCIe · 버스)과 실리콘 bring-up 감각은 Don이 이미 강하다 / 새로 배울 부분은 **Linux 생태계 관점** — 어느 벤더가 mainline에 얼마나 들어가 있는지, BSP · Yocto 레이어 · SDK 이름, 그리고 그것이 제품 수명과 보안 패치 비용에 어떻게 연결되는지
> **선행 노트**: O1(Cortex-A + Cortex-M 구조), O2(remoteproc · rpmsg), O4(가속기와 Linux — DRM accel · dma-buf). 같이 보면 좋은 노트: D1~D7(Device Tree), G2(디바이스 · 드라이버 매칭), K4(Yocto), P2(BSP와 벤더 커널), Q1(카메라 HW), Q4(CAN 실무), N4(Linux vs MCU 분담)

---

## 0. 큰 그림 — SoC 지형을 "두 축"으로 본다

Don이 SSD 컨트롤러를 고를 일은 없었겠지만, 컨트롤러 안에 Cortex-R 몇 개 · 하드웨어 가속기(ECC, 압축) · NAND 채널 수가 몇인지가 펌웨어 구조를 정했던 것은 기억할 것이다. Embedded Linux에서 SoC를 보는 눈도 같다. 다만 축이 하나 더 있다. **"이 칩을 리눅스 커널 본가(mainline)가 얼마나 아는가"** 다.

- **성능 축**: CPU 클러스터(A53 → A55 → A72/A76 → A78AE), 그리고 요즘은 무엇보다 **AI 가속기(NPU/DSP/GPU) TOPS**와 **카메라 파이프라인(ISP, CSI-2 lane 수)**.
- **mainline 친화도 축**: 같은 칩이라도 mainline 커널에 DT · 드라이버가 다 들어가 있으면 "아무 LTS 커널이나 올려도 부팅하고 카메라가 나온다". 반대로 벤더 커널(vendor tree)에만 있으면 그 벤더가 내주는 커널 버전(예: 5.10, 5.15)에 제품이 묶인다. 보안 패치 · 커널 업그레이드(P5) · CVE 대응 비용이 여기서 갈린다.

아래 그림이 이 노트 전체의 요약이다. 가로축은 이 노트에서 **커널 트리를 직접 grep해서 계산한 점수**(4절의 ex8, 실측)이고, 세로축은 **AI TOPS**(벤더 발표 수치, 기억 기반 — 데이터시트로 확인 필요)다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="70" y1="340" x2="665" y2="340" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="70" y1="340" x2="70" y2="25" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="66" y="356" font-size="12">0</text>
<text x="149" y="356" font-size="12">1</text>
<text x="232" y="356" font-size="12">2</text>
<text x="315" y="356" font-size="12">3</text>
<text x="397" y="356" font-size="12">4</text>
<text x="480" y="356" font-size="12">5</text>
<text x="563" y="356" font-size="12">6</text>
<text x="646" y="356" font-size="12">7</text>
<text x="250" y="374" font-size="13">mainline 점수 (ex8, 0~7, 실측)</text>
<line x1="66" y1="282" x2="70" y2="282" stroke="currentColor"/>
<text x="32" y="286" font-size="12">0.5</text>
<line x1="66" y1="231" x2="70" y2="231" stroke="currentColor"/>
<text x="32" y="235" font-size="12">2</text>
<line x1="66" y1="181" x2="70" y2="181" stroke="currentColor"/>
<text x="32" y="185" font-size="12">8</text>
<line x1="66" y1="131" x2="70" y2="131" stroke="currentColor"/>
<text x="32" y="135" font-size="12">32</text>
<line x1="66" y1="81" x2="70" y2="81" stroke="currentColor"/>
<text x="32" y="85" font-size="12">128</text>
<text x="10" y="319" font-size="12">NPU 없음</text>
<line x1="70" y1="300" x2="650" y2="300" stroke="#888" stroke-dasharray="3 3"/>
<text x="10" y="28" font-size="12">AI TOPS (log, 기억 기반)</text>
<circle cx="567" cy="226" r="5" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="575" y="230" font-size="12">i.MX8M Plus</text>
<circle cx="319" cy="282" r="5" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="327" y="286" font-size="12">i.MX93</text>
<circle cx="230" cy="231" r="5" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="180" y="235" font-size="12">i.MX95</text>
<circle cx="484" cy="315" r="5" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="494" y="319" font-size="12">AM62x</text>
<circle cx="401" cy="231" r="5" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="409" y="235" font-size="12">AM62A</text>
<circle cx="319" cy="315" r="5" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="329" y="319" font-size="12">AM64x</text>
<circle cx="401" cy="131" r="5" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="409" y="135" font-size="12">AM69</text>
<circle cx="395" cy="167" r="5" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="329" y="171" font-size="12">QCS6490</text>
<circle cx="407" cy="159" r="5" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="419" y="163" font-size="12">QRB5165</text>
<circle cx="394" cy="315" r="5" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="336" y="307" font-size="12">RZ/G2L</text>
<circle cx="153" cy="98" r="5" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="161" y="102" font-size="12">RZ/V2H</text>
<circle cx="236" cy="260" r="5" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="248" y="266" font-size="12">RK3568</text>
<circle cx="236" cy="192" r="5" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="244" y="196" font-size="12">RK3588</text>
<circle cx="408" cy="315" r="5" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="418" y="329" font-size="12">STM32MP157</text>
<circle cx="242" cy="246" r="5" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="254" y="252" font-size="12">STM32MP257</text>
<circle cx="153" cy="54" r="5" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="161" y="58" font-size="12">Orin (AGX)</text>
<circle cx="236" cy="315" r="5" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="248" y="319" font-size="12">RPi 5</text>
</svg>
```

그림 1 — SoC 지형도. 가로 = 6.18.55 트리에서 레퍼런스 보드 DT의 7개 IP 블록(GPU · NPU · ISP · CSI · CAN · remoteproc · mailbox) 중 in-tree 드라이버가 매칭되는 개수(ex8 실측). 세로 = AI TOPS(기억 기반, log). 오른쪽 아래 = "성능은 낮지만 mainline으로 오래 굴리기 좋은 칩", 왼쪽 위 = "성능은 최고지만 벤더 BSP에 묶이는 칩".

이 그림에서 읽어야 할 것은 세 가지다.

1. **왼쪽 위(Jetson Orin, RZ/V2H)**: AI 성능은 압도적이지만 mainline DT에는 CPU와 mailbox 정도만 있다. 실제 제품은 100% 벤더 BSP(NVIDIA Jetson Linux, Renesas VLP)로 간다.
2. **오른쪽(i.MX8M Plus, AM62x)**: NPU는 작거나 없지만 GPU · ISP · CSI · CAN · remoteproc까지 mainline에 들어가 있다. 산업용 · 의료 · 10년 제품이 이쪽을 고른다.
3. **가운데(TI AM62A/AM69, Qualcomm)**: 커널 쪽은 꽤 mainline이지만 **AI 가속기 자체는 remoteproc(펌웨어 로드)으로만 보이고, 추론 스택은 벤더 SDK**다. "커널은 upstream, AI는 벤더"라는 가장 흔한 현실이다.

> 주의: 가로축 점수는 "레퍼런스 보드 DT 하나에 그 IP의 노드가 있고 매칭되는 드라이버가 트리에 있다"만 센다. 드라이버 품질, 기능 완성도(예: ISP 튜닝, NPU 컴파일러), 벤더가 실제로 지원하는 커널 버전은 반영하지 않는다. 그래서 "대략적인 방향"으로만 쓴다. 한계는 4.6절에서 정리한다.

---

## 1. SoC를 읽는 틀 — 어떤 칩이든 같은 8개 질문

새 SoC 데이터시트를 받으면 Don은 습관적으로 블록 다이어그램부터 볼 것이다. Embedded Linux 관점에서는 그 블록 다이어그램에 아래 8개 질문을 던진다.

```svg
<svg viewBox="0 0 680 285" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="20" width="210" height="54" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="43" font-size="13" text-anchor="middle">Cortex-A 클러스터</text>
<text x="115" y="61" font-size="13" text-anchor="middle">Linux (SMP)</text>
<rect x="235" y="20" width="210" height="54" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="43" font-size="13" text-anchor="middle">Cortex-M/R · PRU · DSP</text>
<text x="340" y="61" font-size="13" text-anchor="middle">RTOS · 펌웨어</text>
<rect x="460" y="20" width="210" height="54" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="565" y="43" font-size="13" text-anchor="middle">보안 코어 (HSM/TEE)</text>
<text x="565" y="61" font-size="13" text-anchor="middle">부트 · 키 · 펌웨어 관리</text>
<rect x="10" y="110" width="660" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="135" font-size="13" text-anchor="middle">인터커넥트 (NoC) + DDR 컨트롤러 + mailbox · 공유 SRAM</text>
<rect x="10" y="186" width="160" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="90" y="209" font-size="13" text-anchor="middle">NPU / GPU</text>
<text x="90" y="227" font-size="13" text-anchor="middle">AI · 그래픽</text>
<rect x="180" y="186" width="160" height="54" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="260" y="209" font-size="13" text-anchor="middle">CSI-2 수신 + ISP</text>
<text x="260" y="227" font-size="13" text-anchor="middle">카메라 파이프</text>
<rect x="350" y="186" width="160" height="54" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="430" y="209" font-size="13" text-anchor="middle">CAN-FD · TSN</text>
<text x="430" y="227" font-size="13" text-anchor="middle">UART · SPI · I2C</text>
<rect x="520" y="186" width="150" height="54" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="595" y="209" font-size="13" text-anchor="middle">PCIe · USB</text>
<text x="595" y="227" font-size="13" text-anchor="middle">외부 칩 (RP1 등)</text>
<line x1="115" y1="74" x2="115" y2="108" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="340" y1="74" x2="340" y2="108" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="565" y1="74" x2="565" y2="108" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="90" y1="150" x2="90" y2="184" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="260" y1="150" x2="260" y2="184" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="430" y1="150" x2="430" y2="184" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="595" y1="150" x2="595" y2="184" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="220" y1="47" x2="233" y2="47" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="10" y="270" font-size="13">벤더마다 칸을 채우는 IP가 다를 뿐, Linux가 보는 구조(DT 노드 → 드라이버)는 같다</text>
</svg>
```

그림 2 — 이종(heterogeneous) SoC의 공통 해부도. 어느 벤더든 이 칸들을 채우는 방식만 다르다.

| 질문 | 왜 중요한가 | 커널 트리에서 확인하는 곳 |
|---|---|---|
| 1. Linux가 도는 Cortex-A는 몇 개 · 어떤 코어인가 | 성능, 전력, PREEMPT_RT 여유 | `cpus { cpu@N { compatible = "arm,cortex-a55" } }` (DT) |
| 2. 실시간 보조 코어(M4/M7/M33/R5F)가 있나 | 모터 · CAN · 안전 로직을 Linux 밖으로 (N4, O1) | `drivers/remoteproc/*`, DT의 `-cm7`, `-r5fss` 노드 |
| 3. AI 가속기는 NPU인가 DSP인가 GPU인가 | 추론 스택 · 드라이버 모델이 완전히 다름 (O4) | `drivers/accel`, `drivers/gpu/drm`, remoteproc DSP |
| 4. 카메라: CSI-2 몇 개 · ISP 유무 | 로봇 카메라 수, 화질 튜닝 가능성 (Q1, Q2) | `drivers/media/platform/<vendor>` |
| 5. CAN-FD 몇 채널 | 모터 컨트롤러 · BMS · 차량 버스 (Q4) | `drivers/net/can/*`, DT `can@` |
| 6. 보조 코어와의 통신 수단 | mailbox + 공유 메모리 → rpmsg (O2) | `drivers/mailbox`, DT `-mu`, `-hsp`, `ipcc` |
| 7. BSP는 무엇이고 mainline은 어디까지 | 커널 버전 선택권, 보안 패치 비용 (P2, P5) | 이 노트 4절의 매트릭스 |
| 8. 공급 수명(longevity) · 등급(산업/차량 온도, ASIL) | 국방 · 자동차는 10~15년 공급, 안전 인증 | 커널에는 없음 → 벤더 longevity 프로그램 문서 |

이 표의 마지막 열이 이 노트의 핵심 기법이다. **"이 칩에 X가 있나?"를 블로그가 아니라 커널 트리로 확인한다.** DT(.dts/.dtsi)는 하드웨어 명세서이고, `drivers/` 안 `of_device_id` 표의 `.compatible` 문자열은 "커널이 그 하드웨어를 안다"는 증거다(D1, G2).

### 1.1 출처 표기 규칙 (이 노트 전체)

- **DT✓** = 6.18.55 커널 트리의 DT/드라이버 소스로 이 노트에서 직접 확인한 사실 (명령과 출력이 본문에 있음).
- **기억 기반** = 벤더 발표 자료 · 데이터시트에서 기억하는 값. TOPS, 클럭, SDK 이름, 고객사 같은 것. **"기억 기반 — 데이터시트로 확인 필요"**. 면접에서 숫자를 말할 때는 "대략" "제 기억으로는"을 붙이는 게 안전하다.

---

## 2. 커널 트리를 SoC 카탈로그로 쓰기

실행 환경: VM `elinux`(호스트 커널 `7.0.0-38-generic`, aarch64)에서 공용 랩의 커널 소스 `/var/lab/src/linux-6.18.55`를 읽는다. QEMU 게스트는 `Image`(6.18.55)와 `Image-dom`(6.18.55-dom)을 쓴다. 스크립트는 `~/.elinux_scratch/O5/`, 산출물은 `/var/tmp/O5/`.

### 2.1 예제 1 — 벤더별 DT 파일 수 세기

무엇을 확인하나: arm64 DT 디렉터리에서 벤더별 보드(.dts) · SoC/모듈(.dtsi) · 오버레이(.dtso) 수. "커뮤니티와 벤더가 이 계열에 얼마나 투자했나"의 아주 거친 지표다.

```sh
#!/bin/bash
# ex1: count board .dts (and SoC .dtsi) per vendor dir in arch/arm64/boot/dts
K=${K:-/var/lab/src/linux-6.18.55}
cd "$K/arch/arm64/boot/dts" || exit 1
printf "%-12s %6s %6s %6s\n" vendor dts dtsi dtso
for v in freescale ti rockchip qcom nvidia broadcom st renesas; do
  d=$(find "$v" -name '*.dts'  | wc -l)
  i=$(find "$v" -name '*.dtsi' | wc -l)
  o=$(find "$v" -name '*.dtso' | wc -l)
  printf "%-12s %6d %6d %6d\n" "$v" "$d" "$i" "$o"
done
echo "--- all arm64 vendors: top 10 by .dts count"
for v in */; do v=${v%/}; echo "$(find "$v" -name '*.dts' | wc -l) $v"; done \
  | sort -rn | head -10
echo "--- arm32 (STM32MP1 lives here)"
find "$K/arch/arm/boot/dts/st" -name 'stm32mp1*.dts' | wc -l
```

```sh
limactl shell elinux -- bash ~/.elinux_scratch/O5/ex1_dts_count.sh
```

```text
vendor          dts   dtsi   dtso
freescale       210    211     61
ti               57    106     48
rockchip        193     99     13
qcom            298    208     11
nvidia           19     29      0
broadcom         30     21      1
st                4     20      0
renesas          77    115     19
--- all arm64 vendors: top 10 by .dts count
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
--- arm32 (STM32MP1 lives here)
44
```

출력에서 볼 것: Qualcomm(298) · NXP(freescale, 210) · Rockchip(193)이 압도적이다. Qualcomm은 휴대폰 · 노트북(Snapdragon X) 보드가 많아서, Rockchip은 중국 SBC(Radxa, FriendlyElec, Pine64 …) 커뮤니티가 크기 때문이다. NVIDIA는 19개뿐이다 — Jetson은 mainline보다 벤더 BSP로 쓰인다는 첫 번째 신호. STM32MP1은 Cortex-A7(32비트)이라 `arch/arm/boot/dts/st`에 따로 있다(44개).

```svg
<svg viewBox="0 0 680 254" xmlns="http://www.w3.org/2000/svg">
<text x="8" y="28" font-size="12">qcom</text>
<rect x="130" y="14" width="477" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="613" y="28" font-size="12">298</text>
<text x="8" y="54" font-size="12">freescale</text>
<rect x="130" y="40" width="336" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="472" y="54" font-size="12">210</text>
<text x="8" y="80" font-size="12">rockchip</text>
<rect x="130" y="66" width="309" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="445" y="80" font-size="12">193</text>
<text x="8" y="106" font-size="12">renesas</text>
<rect x="130" y="92" width="123" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="259" y="106" font-size="12">77</text>
<text x="8" y="132" font-size="12">ti</text>
<rect x="130" y="118" width="91" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="227" y="132" font-size="12">57</text>
<text x="8" y="158" font-size="12">broadcom</text>
<rect x="130" y="144" width="48" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="184" y="158" font-size="12">30</text>
<text x="8" y="184" font-size="12">nvidia</text>
<rect x="130" y="170" width="30" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="166" y="184" font-size="12">19</text>
<text x="8" y="210" font-size="12">st (arm64)</text>
<rect x="130" y="196" width="6" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="142" y="210" font-size="12">4</text>
<text x="8" y="236" font-size="12">st (arm32 MP1)</text>
<rect x="130" y="222" width="70" height="18" fill="#888" fill-opacity="0.35" stroke="#888"/>
<text x="206" y="236" font-size="12">44</text>
</svg>
```

그림 3 — 6.18.55 arm64 DT의 벤더별 .dts 수(예제 1 실측). 회색 막대는 arm32 디렉터리(STM32MP1).

### 2.2 예제 2 — SoC 하나에 보드가 몇 개 붙어 있나

무엇을 확인하나: SoC의 top-level compatible(예: `"fsl,imx8mp"`)을 문자열로 grep해서, 그 SoC를 쓰는 보드 파일 수를 센다.

```sh
#!/bin/bash
# ex2: for each SoC compatible, how many board files (.dts) and SoC files (.dtsi) mention it
K=${K:-/var/lab/src/linux-6.18.55}
cd "$K/arch" || exit 1
printf "%-22s %5s %5s  %s\n" compatible dts dtsi example-board
while read -r c; do
  files=$(grep -rl --include='*.dts' --include='*.dtsi' "\"$c\"" arm64/boot/dts arm/boot/dts/st 2>/dev/null)
  nd=$(printf "%s\n" "$files" | grep -c '\.dts$')
  ni=$(printf "%s\n" "$files" | grep -c '\.dtsi$')
  ex=$(printf "%s\n" "$files" | grep '\.dts$' | head -1 | sed 's#.*/##')
  printf "%-22s %5d %5d  %s\n" "$c" "$nd" "$ni" "$ex"
done <<'LIST'
fsl,imx8mm
fsl,imx8mp
fsl,imx93
fsl,imx95
ti,am625
ti,am62a7
ti,am642
ti,j721e
ti,j721s2
ti,j784s4
rockchip,rk3568
rockchip,rk3588
rockchip,rk3588s
qcom,qcm6490
qcom,sc7280
qcom,sm8250
nvidia,tegra234
brcm,bcm2712
st,stm32mp157
st,stm32mp257
renesas,r9a07g044
renesas,r9a09g057
LIST
```

```text
1024:		.compatible = "renesas,r9a09g057-csi2",
arch/arm64/boot/dts/renesas/r9a09g047.dtsi
compatible               dts  dtsi  example-board
fsl,imx8mm                39     6  imx8mm-verdin-nonwifi-dev.dts
fsl,imx8mp                56    12  imx8mp-verdin-nonwifi-dev.dts
fsl,imx93                 10     4  imx93-11x11-evk.dts
fsl,imx95                  4     1  imx95-19x19-evk.dts
ti,am625                  16     3  k3-am625-verdin-wifi-ivy.dts
ti,am62a7                  2     2  k3-am62a7-sk.dts
ti,am642                   5     3  k3-am642-phyboard-electra-rdk.dts
ti,j721e                   3     1  k3-j721e-beagleboneai64.dts
ti,j721s2                  3     2  k3-j721s2-common-proc-board.dts
ti,j784s4                  2     1  k3-am69-sk.dts
rockchip,rk3568           18     2  rk3568-fastrhino-r68s.dts
rockchip,rk3588           28    12  rk3588-nanopc-t6.dts
rockchip,rk3588s          14     0  rk3588s-rock-5c.dts
qcom,qcm6490               5     0  qcs6490-rb3gen2.dts
qcom,sc7280               15     0  sc7280-herobrine-evoker.dts
qcom,sm8250               10     0  sm8250-xiaomi-elish-boe.dts
nvidia,tegra234            6     5  tegra234-p3768-0000+p3767-0005.dts
brcm,bcm2712               1     1  bcm2712-rpi-5-b-ovl-rp1.dts
st,stm32mp157             27     4  stm32mp157c-phycore-stm32mp1-3.dts
st,stm32mp257              2     0  stm32mp257f-dk.dts
renesas,r9a07g044          3     5  r9a07g044l2-remi-pi.dts
renesas,r9a09g057          2     1  r9a09g057h48-kakip.dts
```

출력에서 볼 것:

- **i.MX8M Plus 56개, i.MX8M Mini 39개** — Toradex Verdin, PHYTEC, TQ, Variscite, Kontron 같은 SoM(System-on-Module) 회사들이 전부 mainline에 보드를 넣었다. 산업용 SoM 생태계가 크다는 뜻이다.
- **RK3588 28 + RK3588S 14** — SBC 커뮤니티.
- **QCS6490은 `qcom,qcm6490`으로 잡힌다**. RB3 Gen2의 compatible은 `"qcom,qcs6490-rb3gen2", "qcom,qcm6490"`이다 — 마케팅 이름(QCS6490)과 DT 이름(qcm6490, 그리고 그 밑의 sc7280.dtsi)이 다르다. QRB5165(RB5)도 SoC compatible은 `qcom,sm8250`(휴대폰용 Snapdragon 865 계열)이다.
- **Jetson Orin = `nvidia,tegra234`**, **RPi5 = `brcm,bcm2712`** 보드 1개, **RZ/G2L = `r9a07g044`**, **RZ/V2H = `r9a09g057`** (Renesas는 부품 번호로 이름 짓는다).
- dtsi 열이 큰 것(i.MX8MP 12, RK3588 12)은 SoM dtsi에 SoC compatible을 넣는 패턴이라 dts 수가 과소 계산된다는 뜻이다. 정확한 보드 수는 `#include` 그래프를 따라가야 한다(연습 문제 3).

### 2.3 예제 3 — 레퍼런스 보드 DT를 실제로 컴파일해서 하드웨어 목록 뽑기

텍스트 grep은 `#include`와 `&label` 덮어쓰기를 못 따라간다. 그래서 커널 빌드가 하는 것과 똑같이 **cpp로 전처리 → dtc로 dtb 컴파일 → 다시 dts로 풀어서(flatten)** 하나의 완성된 트리를 만든 뒤 분석한다(D5, D7에서 본 `dtc_cpp` 규칙과 같은 단계).

```sh
#!/bin/bash
# dtflat.sh <board.dts relative to arch/arm64/boot/dts or arch/arm/boot/dts> <outdir>
# preprocess like the kernel's dtc_cpp rule, compile to dtb, decompile flattened
K=${K:-/var/lab/src/linux-6.18.55}
b=$1; out=$2; mkdir -p "$out"
case $b in st/stm32mp1*) A=arm;; *) A=arm64;; esac
n=$(basename "$b" .dts)
cpp -nostdinc -I "$K/include" -I "$K/arch/$A/boot/dts/$(dirname "$b")" \
    -I "$K/scripts/dtc/include-prefixes" -I "$K/arch/$A/boot/dts" \
    -undef -D__DTS__ -x assembler-with-cpp "$K/arch/$A/boot/dts/$b" -o "$out/$n.pp" || exit 1
dtc -q -I dts -O dtb -o "$out/$n.dtb" "$out/$n.pp" || exit 1
dtc -q -I dtb -O dts -o "$out/$n.flat.dts" "$out/$n.dtb"
echo "$out/$n.flat.dts"
```

flatten된 트리에서 CPU 노드와 IP 블록을 분류하는 파서다. `cpus/cpu@N`의 첫 compatible을 세고, 나머지 노드는 첫 compatible(또는 노드 이름 `npu@` 같은 것)로 분류한다. `status = "okay"`인 개수도 같이 센다 — SoC dtsi는 블록을 전부 `disabled`로 선언하고 보드 dts가 쓰는 것만 켜기 때문이다(D4).

```python
#!/usr/bin/env python3
"""dtinv.py <flat.dts>: inventory CPU cores + accelerator/camera/CAN/remote-core nodes"""
import re, sys, collections
CLS = [  # (class, regex on first compatible)
    ("NPU",  r"rknn|ethos|-npu$|neutron"),
    ("GPU",  r"mali|adreno|gpu|vivante|powervr|img,img|v3d|gc7000"),
    ("ISP",  r"(^|[,-])isp|camss|pisp|rkisp|dcmipp|renesas,.*-cru"),
    ("CSI",  r"csi|mipi-rx|rkcif|-cif|-isi|-vi$"),
    ("CAN",  r"(^|[,-])m_can|flexcan|canfd|fdcan|(^|[,-])can$|-can$"),
    ("RPROC", r"r5f|(^|[,-])cm(4|7|33)|-m4$|-m4f$|c7.*dsp|-dsp$|-pas$|-pru$|rproc"),
    ("MBOX", r"mailbox|mbox|-mu$|-hsp$|ipcc"),
]
txt = open(sys.argv[1]).read().splitlines()
path, nodes, cur = [], [], None
for ln in txt:
    s = ln.strip()
    m = re.match(r'^([\w@,.+\-/]+)\s*\{$', s) or re.match(r'^[\w]+:\s*([\w@,.+\-]+)\s*\{$', s)
    if m:
        path.append(m.group(1)); cur = {"path": "/".join(path), "compat": [], "status": "okay"}
        nodes.append(cur); continue
    if s.startswith("};"):
        path.pop(); continue
    if cur and s.startswith("compatible ="):
        cur["compat"] = re.findall(r'"([^"]+)"', s)
    if cur and s.startswith("status ="):
        cur["status"] = re.findall(r'"([^"]+)"', s)[0]
cpus = collections.Counter(c["compat"][0] for c in nodes
         if re.search(r"/cpus/cpu@\w+$", c["path"]) and c["compat"])
pmu = [c["compat"][0].split(",")[-1] for c in nodes
       if c["compat"] and c["compat"][0].endswith("-pmu") and "cortex" in c["compat"][0]]
print("CPU  :", ", ".join(f"{n}x {k.split(',')[-1]}" for k, n in cpus.items()),
      ("(PMU: " + ", ".join(pmu) + ")") if "qcom,kryo" in cpus else "")
for cls, rx in CLS:
    hit = collections.Counter()
    on = collections.Counter()
    for c in nodes:
        leaf = c["path"].split("/")[-1].split("@")[0]
        bynode = leaf == cls.lower()  # e.g. npu@38500000 with a generic compatible
        if c["compat"] and (re.search(rx, c["compat"][0]) or bynode) and "/cpus/" not in c["path"]:
            k = c["compat"][0] + ("[" + leaf + "@]" if bynode else "")
            hit[k] += 1
            on[k] += c["status"] in ("okay", "ok")
    if hit:
        print(f"{cls:5}:", "; ".join(f"{k} x{n}(on {on[k]})" for k, n in sorted(hit.items())[:6])
              + (" ..." if len(hit) > 6 else ""))
```

```sh
#!/bin/bash
# ex3: compile one reference board per SoC from the 6.18.55 tree and inventory it
D=/var/tmp/O5/dt; S=/Users/donh/.elinux_scratch/O5
for b in freescale/imx8mm-evk.dts freescale/imx8mp-evk.dts freescale/imx93-11x11-evk.dts \
         freescale/imx95-19x19-evk.dts ti/k3-am625-sk.dts ti/k3-am62a7-sk.dts ti/k3-am642-evm.dts \
         ti/k3-j721e-sk.dts ti/k3-am68-sk-base-board.dts ti/k3-am69-sk.dts \
         rockchip/rk3568-evb1-v10.dts rockchip/rk3588-rock-5b.dts qcom/qcs6490-rb3gen2.dts \
         qcom/qrb5165-rb5.dts nvidia/tegra234-p3768-0000+p3767-0005.dts \
         nvidia/tegra234-p3737-0000+p3701-0000.dts broadcom/bcm2712-rpi-5-b.dts \
         st/stm32mp157f-dk2.dts st/stm32mp257f-ev1.dts renesas/r9a07g044l2-smarc.dts \
         renesas/r9a09g057h44-rzv2h-evk.dts; do
  f=$(bash $S/dtflat.sh "$b" $D 2>/dev/null) || { echo "== $b : BUILD FAIL"; continue; }
  echo "== $(basename "$b" .dts)"
  python3 -I $S/dtinv.py "$f"
done
```

출력(21개 보드 중 일부만 남김 — 전체는 111줄, 나머지는 `...`로 생략):

```text
...
== imx8mp-evk
CPU  : 4x cortex-a53 
NPU  : vivante,gc[npu@] x1(on 1)
GPU  : vivante,gc x1(on 1); vivante,gc[gpu@] x2(on 2)
ISP  : fsl,imx8mp-isp[isp@] x2(on 0)
CSI  : fsl,imx8mp-isi x1(on 0); fsl,imx8mp-mipi-csi2[csi@] x2(on 0)
CAN  : fsl,imx8mp-flexcan[can@] x2(on 1)
MBOX : fsl,imx8mp-mu x2(on 2)
== imx93-11x11-evk
CPU  : 2x cortex-a55 
CAN  : fsl,imx93-flexcan[can@] x2(on 1)
RPROC: fsl,imx93-cm33 x1(on 1)
MBOX : fsl,imx93-mu x2(on 2)
...
== k3-am625-sk
CPU  : 4x cortex-a53 
GPU  : ti,am62-gpu[gpu@] x1(on 1)
CSI  : ti,j721e-csi2rx x1(on 1); ti,j721e-csi2rx-shim x1(on 0)
CAN  : bosch,m_can[can@] x3(on 0)
RPROC: ti,am62-r5f x1(on 1); ti,am62-r5fss x1(on 1); ti,am625-pru x2(on 2)
MBOX : ti,am64-mailbox x1(on 1)
...
== k3-am642-evm
CPU  : 2x cortex-a53 
CAN  : bosch,m_can[can@] x2(on 2)
RPROC: ti,am64-r5f x4(on 4); ti,am64-r5fss x2(on 2); ti,am642-pru x4(on 4); ti,am642-tx-pru x4(on 4)
MBOX : ti,am64-mailbox x6(on 3)
== k3-j721e-sk
CPU  : 2x cortex-a72 
CSI  : ti,j721e-csi2rx x2(on 2); ti,j721e-csi2rx-shim x2(on 0)
CAN  : bosch,m_can[can@] x16(on 4)
RPROC: ti,j721e-c66-dsp x2(on 2); ti,j721e-c71-dsp x1(on 1); ti,j721e-pru x4(on 4); ti,j721e-r5f x6(on 6); ti,j721e-r5fss x3(on 3); ti,j721e-tx-pru x4(on 4)
MBOX : ti,am654-mailbox x12(on 5)
...
== k3-am69-sk
CPU  : 8x cortex-a72 
CSI  : ti,j721e-csi2rx x3(on 3); ti,j721e-csi2rx-shim x3(on 0); ti,j721s2-dmss-bcdma-csi x1(on 1)
CAN  : bosch,m_can[can@] x20(on 4)
RPROC: ti,j721s2-c71-dsp x4(on 4); ti,j721s2-r5f x8(on 8); ti,j721s2-r5fss x4(on 4)
MBOX : ti,am654-mailbox x24(on 6)
...
== rk3588-rock-5b
CPU  : 4x cortex-a55, 4x cortex-a76 
NPU  : rockchip,rk3588-rknn-core[npu@] x3(on 3)
GPU  : rockchip,rk3588-mali[gpu@] x1(on 1)
CSI  : rockchip,rk3588-csi-dphy x2(on 0); rockchip,rk3588-csidphy-grf x2(on 2)
== qcs6490-rb3gen2
CPU  : 8x kryo (PMU: cortex-a55-pmu, cortex-a78-pmu)
GPU  : qcom,adreno-635.0[gpu@] x1(on 1); qcom,adreno-gmu-635.0 x1(on 1); qcom,sc7280-gpucc x1(on 1)
ISP  : qcom,sc7280-camss[isp@] x1(on 0)
RPROC: qcom,sc7280-adsp-pas x1(on 1); qcom,sc7280-cdsp-pas x1(on 1); qcom,sc7280-mpss-pas x1(on 1); qcom,sc7280-wpss-pas x1(on 1)
MBOX : qcom,sc7280-ipcc x1(on 1)
== qrb5165-rb5
CPU  : 8x kryo485 
GPU  : qcom,adreno-650.2[gpu@] x1(on 1); qcom,adreno-gmu-650.2 x1(on 1); qcom,sm8250-gpucc x1(on 1)
ISP  : qcom,sm8250-camss x1(on 0)
CAN  : microchip,mcp2518fd[can@] x1(on 1)
RPROC: qcom,sm8250-adsp-pas x1(on 1); qcom,sm8250-cdsp-pas x1(on 1); qcom,sm8250-slpi-pas x1(on 1)
MBOX : qcom,sm8250-ipcc x1(on 1)
== tegra234-p3768-0000+p3767-0005
CPU  : 12x cortex-a78ae 
MBOX : nvidia,tegra234-hsp x2(on 2)
...
== bcm2712-rpi-5-b
CPU  : 4x cortex-a76 
GPU  : brcm,bcm2712-vc6[gpu@] x1(on 1)
MBOX : brcm,bcm2835-mbox x1(on 1)
...
== stm32mp257f-ev1
CPU  : 2x cortex-a35 
ISP  : st,stm32mp25-dcmipp x1(on 1)
CSI  : st,stm32mp25-csi[csi@] x1(on 1)
...
== r9a09g057h44-rzv2h-evk
CPU  : 4x cortex-a55 
GPU  : renesas,r9a09g057-mali[gpu@] x1(on 1)
```

출력에서 볼 것:

- **CPU 구성은 전부 DT✓**: i.MX8MP 4x A53, i.MX93 2x A55, i.MX95 6x A55, AM62x 4x A53, AM69 8x A72, RK3588 4x A55 + 4x A76, Orin 12x A78AE, BCM2712 4x A76, STM32MP257 2x A35, RZ/V2H 4x A55. Qualcomm은 `qcom,kryo`라는 뭉뚱그린 이름이라 코어 종류를 알 수 없고, PMU 노드(`arm,cortex-a55-pmu`, `arm,cortex-a78-pmu`)를 보고 A78 + A55 조합임을 안다.
- **i.MX8MP의 NPU는 `compatible = "vivante,gc"`**, 즉 GPU와 같은 Vivante 계열 IP이고 GPU 드라이버 etnaviv가 맡는다. 노드 이름 `npu@38500000`으로만 구분된다.
- **TI는 보조 코어 백화점**이다: AM625 = R5F 1 + PRU 2, AM642 = R5F 4 + PRU 4 + TX-PRU 4, TDA4VM(J721E) = R5F 6 + C66x DSP 2 + C71x DSP 1, AM69 = R5F 8 + C7x 4. CAN(MCAN)도 J721E 16개, AM68/AM69 20개.
- **Tegra234는 CPU와 HSP mailbox만 보인다.** GPU, DLA, VI(카메라), MTTCAN 노드가 mainline DT에 없다. 반대로 Orin Nano(p3767-0005) DT에도 CPU 노드가 12개다 — 실물 Orin Nano는 6코어(기억 기반)이므로 mainline DT는 SoC 최대 구성을 적어 두고 펌웨어/부트로더 쪽에서 조정한다고 봐야 한다(확인 필요).
- **QRB5165 RB5의 CAN은 SoC 내장이 아니라 SPI에 붙은 `microchip,mcp2518fd`** 다. Qualcomm 모바일 계열 SoC에는 CAN 컨트롤러가 없다(기억 기반 — 데이터시트로 확인 필요). 로봇에서 Qualcomm을 쓰면 CAN은 외부 칩이나 MCU로 해결해야 한다는 뜻이다.

---

## 3. 패밀리별 상세

각 패밀리마다 (1) 코어 배치 그림, (2) 사양 표, (3) BSP · mainline 상태, (4) 누가 쓰나, 순서로 본다. 그림에서 **실선 상자는 DT✓**(예제 3에서 노드를 확인), **점선 상자는 기억 기반**이다. 색은 파랑 = Linux가 도는 Cortex-A, 주황 = 실시간 코어(M/R/PRU), 초록 = 가속기(GPU/NPU/DSP), 빨강 = 카메라, 회색 = CAN · I/O.

### 3.1 NXP i.MX 8M / 8M Plus / 93 / 95 — "산업용 표준"

```svg
<svg viewBox="0 0 680 216" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="10" width="12" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="26" y="21" font-size="12">Linux 코어</text>
<rect x="109" y="10" width="12" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="125" y="21" font-size="12">실시간 코어</text>
<rect x="206" y="10" width="12" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="222" y="21" font-size="12">가속기</text>
<rect x="272" y="10" width="12" height="12" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="288" y="21" font-size="12">카메라</text>
<rect x="338" y="10" width="12" height="12" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="354" y="21" font-size="12">CAN·I/O</text>
<line x1="420" y1="16" x2="442" y2="16" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="446" y="21" font-size="12">점선 = 기억 기반</text>
<text x="10" y="60" font-size="13">i.MX8M Mini</text>
<rect x="112" y="40" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="58" font-size="12">4x A53</text>
<rect x="179" y="40" width="38" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="187" y="58" font-size="12">M4F</text>
<rect x="223" y="40" width="61" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="231" y="58" font-size="12">GPU GC</text>
<rect x="290" y="40" width="76" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="298" y="58" font-size="12">MIPI-CSI</text>
<text x="10" y="104" font-size="13">i.MX8M Plus</text>
<rect x="112" y="84" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="102" font-size="12">4x A53</text>
<rect x="179" y="84" width="31" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="187" y="102" font-size="12">M7</text>
<rect x="216" y="84" width="68" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="224" y="102" font-size="12">NPU(GC)</text>
<rect x="290" y="84" width="83" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="298" y="102" font-size="12">GPU GC x2</text>
<rect x="379" y="84" width="61" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="387" y="102" font-size="12">ISP x2</text>
<rect x="446" y="84" width="61" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="454" y="102" font-size="12">CSI x2</text>
<rect x="513" y="84" width="83" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="521" y="102" font-size="12">CAN-FD x2</text>
<text x="10" y="148" font-size="13">i.MX93</text>
<rect x="112" y="128" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="146" font-size="12">2x A55</text>
<rect x="179" y="128" width="38" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="187" y="146" font-size="12">M33</text>
<rect x="223" y="128" width="68" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="231" y="146" font-size="12">NPU U65</text>
<rect x="297" y="128" width="38" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="305" y="146" font-size="12">CSI</text>
<rect x="341" y="128" width="83" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="349" y="146" font-size="12">CAN-FD x2</text>
<text x="10" y="192" font-size="13">i.MX95</text>
<rect x="112" y="172" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="190" font-size="12">6x A55</text>
<rect x="179" y="172" width="31" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="187" y="190" font-size="12">M7</text>
<rect x="216" y="172" width="38" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="224" y="190" font-size="12">M33</text>
<rect x="260" y="172" width="38" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="268" y="190" font-size="12">NPU</text>
<rect x="304" y="172" width="76" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="312" y="190" font-size="12">GPU Mali</text>
<rect x="386" y="172" width="38" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="394" y="190" font-size="12">ISP</text>
<rect x="430" y="172" width="83" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="438" y="190" font-size="12">CAN-FD x5</text>
</svg>
```

그림 4 — NXP i.MX 코어 배치. 실선 = DT✓, 점선 = 기억 기반.

| 칩 | Linux 코어 | 실시간 코어 | AI · GPU | 카메라 | CAN | 출처 |
|---|---|---|---|---|---|---|
| i.MX8M Mini | 4x A53 | M4F | GPU GC (3D+2D), NPU 없음 | MIPI CSI 1 | 없음 | 코어·GPU·CSI DT✓, CAN 없음 DT✓, M4F 기억 |
| i.MX8M Plus | 4x A53 | M7 | NPU(VeriSilicon, 약 2.3 TOPS) + GPU GC | ISP 2 + CSI 2 | CAN-FD 2 | 블록 DT✓, TOPS·M7 기억 |
| i.MX93 | 2x A55 | M33 | Arm Ethos-U65 microNPU(약 0.5 TOPS), 3D GPU 없음 | MIPI CSI 1 | CAN-FD 2 | A55·M33·CAN DT✓, NPU·CSI 기억 |
| i.MX95 | 6x A55 | M7 + M33(safety) | eIQ Neutron NPU(약 2 TOPS) + Mali GPU | ISP | CAN-FD 5 | A55·CAN DT✓, 나머지 기억 |

- **BSP**: NXP "Linux BSP"(LF 릴리스, 예: `LF6.6.x_y` 형식), 벤더 커널 `github.com/nxp-imx/linux-imx`, Yocto는 벤더 레이어 `meta-imx` + 커뮤니티 레이어 `meta-freescale`. ML은 **eIQ**, M 코어 쪽은 **MCUXpresso SDK**. (기억 기반 — 데이터시트로 확인 필요)
- **mainline**: 8M 계열은 매우 좋다. 예제 4에서 보듯 i.MX8MP의 ISP는 **Rockchip의 rkisp1 드라이버를 재사용**하고(같은 IP를 라이선스한 것), NPU는 etnaviv가 맡는다. i.MX93의 Ethos-U65와 i.MX95의 Neutron NPU는 6.18 트리에 드라이버가 없다(예제 4, 7).
- **누가 쓰나**: 산업용 HMI, 의료기기, 스마트홈 허브, 로봇의 보조 컴퓨터, 차량 텔레매틱스. NXP의 10~15년 공급 프로그램 때문에 장수 제품에 많다(기억 기반).
- **면접 포인트**: "i.MX8M Plus는 작은 NPU + ISP 2개 + CAN-FD 2개 + M7이라 **카메라 2개짜리 로봇 센서 노드**에 딱 맞는 크기, mainline도 좋다."

### 3.2 TI Sitara AM62x / AM62A / AM64x + Jacinto TDA4VM / AM68 / AM69 — "보조 코어 백화점"

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="10" width="12" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="26" y="21" font-size="12">Linux 코어</text>
<rect x="109" y="10" width="12" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="125" y="21" font-size="12">실시간 코어</text>
<rect x="206" y="10" width="12" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="222" y="21" font-size="12">가속기</text>
<rect x="272" y="10" width="12" height="12" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="288" y="21" font-size="12">카메라</text>
<rect x="338" y="10" width="12" height="12" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="354" y="21" font-size="12">CAN·I/O</text>
<line x1="420" y1="16" x2="442" y2="16" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="446" y="21" font-size="12">점선 = 기억 기반</text>
<text x="10" y="60" font-size="13">AM62x</text>
<rect x="112" y="40" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="58" font-size="12">4x A53</text>
<rect x="179" y="40" width="68" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="187" y="58" font-size="12">R5F(DM)</text>
<rect x="253" y="40" width="38" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="261" y="58" font-size="12">M4F</text>
<rect x="297" y="40" width="61" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="305" y="58" font-size="12">PRU x2</text>
<rect x="364" y="40" width="68" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="372" y="58" font-size="12">GPU AXE</text>
<rect x="438" y="40" width="46" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="446" y="58" font-size="12">CSI2</text>
<rect x="490" y="40" width="83" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="498" y="58" font-size="12">CAN-FD x3</text>
<text x="10" y="104" font-size="13">AM62A</text>
<rect x="112" y="84" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="102" font-size="12">4x A53</text>
<rect x="179" y="84" width="61" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="187" y="102" font-size="12">R5F x2</text>
<rect x="246" y="84" width="68" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="254" y="102" font-size="12">C7x+MMA</text>
<rect x="320" y="84" width="38" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="328" y="102" font-size="12">ISP</text>
<rect x="364" y="84" width="46" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="372" y="102" font-size="12">CSI2</text>
<rect x="416" y="84" width="83" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="424" y="102" font-size="12">CAN-FD x3</text>
<text x="10" y="148" font-size="13">AM64x</text>
<rect x="112" y="128" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="146" font-size="12">2x A53</text>
<rect x="179" y="128" width="61" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="187" y="146" font-size="12">R5F x4</text>
<rect x="246" y="128" width="38" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="254" y="146" font-size="12">M4F</text>
<rect x="290" y="128" width="83" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="298" y="146" font-size="12">PRU-ICSSG</text>
<rect x="379" y="128" width="83" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="387" y="146" font-size="12">CAN-FD x2</text>
<text x="10" y="192" font-size="13">TDA4VM</text>
<rect x="112" y="172" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="190" font-size="12">2x A72</text>
<rect x="179" y="172" width="61" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="187" y="190" font-size="12">R5F x6</text>
<rect x="246" y="172" width="61" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="254" y="190" font-size="12">C66 x2</text>
<rect x="313" y="172" width="38" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="321" y="190" font-size="12">C71</text>
<rect x="357" y="172" width="68" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="365" y="190" font-size="12">CSI2 x2</text>
<rect x="431" y="172" width="68" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="439" y="190" font-size="12">CAN x16</text>
<text x="10" y="236" font-size="13">AM69</text>
<rect x="112" y="216" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="234" font-size="12">8x A72</text>
<rect x="179" y="216" width="61" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="187" y="234" font-size="12">R5F x8</text>
<rect x="246" y="216" width="61" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="254" y="234" font-size="12">C7x x4</text>
<rect x="313" y="216" width="68" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="321" y="234" font-size="12">CSI2 x3</text>
<rect x="387" y="216" width="68" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="395" y="234" font-size="12">CAN x20</text>
</svg>
```

그림 5 — TI K3 계열 코어 배치(전부 같은 K3 아키텍처). R5F · DSP · PRU 개수는 DT✓.

| 칩 | Linux 코어 | 실시간 코어 | AI · GPU | 카메라 | CAN | 출처 |
|---|---|---|---|---|---|---|
| AM62x | 4x A53 | R5F 1(Device Manager) + M4F + PRU 2 | GPU(IMG AXE 계열), AI 없음 | CSI-2 1 | MCAN 3 | M4F만 기억, 나머지 DT✓ |
| AM62A | 4x A53 | R5F 2 | C7x DSP + MMA(약 1~2 TOPS), GPU 없음 | CSI-2 + ISP(VPAC) | MCAN 3 | DSP·CSI·CAN DT✓, TOPS·ISP 기억 |
| AM64x | 2x A53 | R5F 4 + M4F + PRU-ICSSG | 없음 | 없음 | MCAN 2 | M4F만 기억 |
| TDA4VM (J721E) | 2x A72 | R5F 6 | C66x 2 + C71x 1 + MMA(약 8 TOPS) | CSI-2 2 | MCAN 16 | TOPS만 기억 |
| AM68 (J721S2) | 2x A72 | R5F 6 | C7x 2(약 8 TOPS) + GPU | CSI-2 2 | MCAN 20 | TOPS만 기억 |
| AM69 (J784S4) | 8x A72 | R5F 8 | C7x 4(약 32 TOPS) | CSI-2 3 | MCAN 20 | TOPS만 기억 |

- **K3 아키텍처의 특징**: SoC 전원 · 클럭 · 리소스 관리를 **TI-SCI 펌웨어**(DM R5F + TIFS 보안 코어)가 맡는다. Linux는 클럭을 직접 만지지 않고 TI-SCI 메시지로 요청한다 — Qualcomm의 RPMh, NVIDIA의 BPMP와 같은 "펌웨어가 SoC를 관리하는" 스타일이다.
- **BSP**: **Processor SDK Linux**(AM62x/AM64x), **Edge AI SDK**(AM62A/TDA4/AM68/AM69), 벤더 커널 `ti-linux-kernel`(git.ti.com), Yocto `meta-ti`(meta-ti-bsp) + 배포판 레이어 `meta-arago`. R5F/C7x 쪽은 MCU+ SDK, AI는 **TIDL**. (기억 기반 — 데이터시트로 확인 필요)
- **mainline**: 커널 쪽은 상위권이다(예제 4: R5F · C7x DSP remoteproc, MCAN, CSI-2 수신기, PowerVR GPU 전부 in-tree). 하지만 **C7x+MMA AI 가속은 `ti_k3_dsp_remoteproc`으로 "펌웨어를 올리는 DSP"로만 보인다** — 추론 스택(TIDL)은 그 펌웨어와 유저 라이브러리에 있다. ISP(VPAC)도 mainline 드라이버가 없다.
- **누가 쓰나**: TDA4VM/AM68/AM69 = ADAS · 자율주행 도메인 컨트롤러, AMR(자율 이동 로봇). AM62A = 스마트 카메라. AM64x = 산업용 이더넷(EtherCAT, PROFINET — PRU-ICSSG) 모터 드라이브. AM62x = HMI, 게이트웨이. (기억 기반)
- **면접 포인트**: "TI는 같은 K3 소프트웨어 구조로 AM62x(저가)부터 AM69(32 TOPS)까지 가니까 **제품 라인을 위아래로 확장하기 쉽고**, R5F가 많아 CAN · 모터 · 안전 로직을 Linux 밖에 둘 수 있다."

### 3.3 Rockchip RK3568 / RK3588 — "가성비 + 커뮤니티 mainline"

```svg
<svg viewBox="0 0 680 128" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="10" width="12" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="26" y="21" font-size="12">Linux 코어</text>
<rect x="109" y="10" width="12" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="125" y="21" font-size="12">실시간 코어</text>
<rect x="206" y="10" width="12" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="222" y="21" font-size="12">가속기</text>
<rect x="272" y="10" width="12" height="12" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="288" y="21" font-size="12">카메라</text>
<rect x="338" y="10" width="12" height="12" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="354" y="21" font-size="12">CAN·I/O</text>
<line x1="420" y1="16" x2="442" y2="16" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="446" y="21" font-size="12">점선 = 기억 기반</text>
<text x="10" y="60" font-size="13">RK3568</text>
<rect x="112" y="40" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="58" font-size="12">4x A55</text>
<rect x="179" y="40" width="68" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="187" y="58" font-size="12">GPU G52</text>
<rect x="253" y="40" width="38" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="261" y="58" font-size="12">NPU</text>
<rect x="297" y="40" width="38" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="305" y="58" font-size="12">ISP</text>
<rect x="341" y="40" width="83" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="349" y="58" font-size="12">CAN-FD x3</text>
<text x="10" y="104" font-size="13">RK3588</text>
<rect x="112" y="84" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="102" font-size="12">4x A76</text>
<rect x="179" y="84" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="187" y="102" font-size="12">4x A55</text>
<rect x="246" y="84" width="90" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="254" y="102" font-size="12">NPU 3 core</text>
<rect x="342" y="84" width="76" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="350" y="102" font-size="12">GPU G610</text>
<rect x="424" y="84" width="38" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="432" y="102" font-size="12">ISP</text>
<rect x="468" y="84" width="83" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="476" y="102" font-size="12">CSI D-PHY</text>
<rect x="557" y="84" width="61" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="565" y="102" font-size="12">CAN-FD</text>
</svg>
```

그림 6 — Rockchip 코어 배치. RK3588은 big.LITTLE(A76 4 + A55 4)과 NPU 코어 3개가 DT✓.

| 칩 | Linux 코어 | 실시간 코어 | AI · GPU | 카메라 | CAN | 출처 |
|---|---|---|---|---|---|---|
| RK3568 | 4x A55 | 없음 | NPU 약 1 TOPS, GPU Mali-G52 | ISP | CAN-FD 3 | A55·GPU·CAN DT✓, NPU·ISP 기억 |
| RK3588 | 4x A76 + 4x A55 | 없음 | NPU 3코어(약 6 TOPS), GPU Mali-G610 | ISP 2(48MP급) | CAN-FD(있음) | 코어·NPU·GPU DT✓, 나머지 기억 |

- **BSP**: Rockchip Linux SDK(벤더 커널 5.10/6.1 계열 브랜치), NPU 런타임 **RKNPU2 + RKNN-Toolkit2**(벤더 커널 드라이버 `rknpu`), Yocto는 커뮤니티 `meta-rockchip`. 보드 벤더(Radxa, FriendlyElec, Pine64)와 Armbian이 실질적 배포 창구. (기억 기반)
- **mainline**: 6.18에서 큰 변화가 있다 — **`drivers/accel/rocket`(DRM accel, RK3588 NPU용)** 이 들어왔고(예제 7), GPU는 panthor(Mali-G610, CSF 계열)가 맡는다. 하지만 RK3588의 ISP · 카메라 수신(VICAP) · CAN-FD 노드는 6.18 DT에 없다(예제 3: CSI D-PHY만 있음). 즉 **mainline으로 NPU는 돌려볼 수 있지만 카메라는 아직 벤더 커널**이다. rocket은 Mesa의 Teflon(TFLite delegate)과 짝을 이룬다(기억 기반).
- **누가 쓰나**: SBC, NVR, 키오스크, 저가 로봇 · 드론 프로토타입, 엣지 AI 박스. 국방 · 미국 정부 납품 제품에서는 공급망 규제(중국 반도체) 때문에 피하는 경우가 많다(업계 일반론 — 회사별로 확인).
- **면접 포인트**: "RK3588은 가격 대비 CPU · NPU가 강하고 mainline이 Collabora 등의 노력으로 빠르게 따라오지만, 카메라 · ISP는 아직 벤더 커널이라 제품화 시 커널 선택이 갈린다."

### 3.4 Qualcomm QCS6490(RB3 Gen2) / QRB5165(RB5) — "휴대폰 SoC를 로봇으로"

```svg
<svg viewBox="0 0 680 128" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="10" width="12" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="26" y="21" font-size="12">Linux 코어</text>
<rect x="109" y="10" width="12" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="125" y="21" font-size="12">실시간 코어</text>
<rect x="206" y="10" width="12" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="222" y="21" font-size="12">가속기</text>
<rect x="272" y="10" width="12" height="12" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="288" y="21" font-size="12">카메라</text>
<rect x="338" y="10" width="12" height="12" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="354" y="21" font-size="12">CAN·I/O</text>
<line x1="420" y1="16" x2="442" y2="16" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="446" y="21" font-size="12">점선 = 기억 기반</text>
<text x="10" y="60" font-size="13">QCS6490</text>
<rect x="112" y="40" width="68" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="58" font-size="12">8x Kryo</text>
<rect x="186" y="40" width="46" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="194" y="58" font-size="12">cDSP</text>
<rect x="238" y="40" width="46" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="246" y="58" font-size="12">aDSP</text>
<rect x="290" y="40" width="83" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="298" y="58" font-size="12">MPSS/WPSS</text>
<rect x="379" y="40" width="61" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="387" y="58" font-size="12">Adreno</text>
<rect x="446" y="40" width="53" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="454" y="58" font-size="12">CAMSS</text>
<text x="10" y="104" font-size="13">QRB5165</text>
<rect x="112" y="84" width="68" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="102" font-size="12">8x Kryo</text>
<rect x="186" y="84" width="46" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="194" y="102" font-size="12">cDSP</text>
<rect x="238" y="84" width="46" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="246" y="102" font-size="12">aDSP</text>
<rect x="290" y="84" width="46" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="298" y="102" font-size="12">SLPI</text>
<rect x="342" y="84" width="90" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="350" y="102" font-size="12">Adreno 650</text>
<rect x="438" y="84" width="53" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="446" y="102" font-size="12">CAMSS</text>
<rect x="497" y="84" width="68" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="505" y="102" font-size="12">SPI CAN</text>
</svg>
```

그림 7 — Qualcomm 로보틱스 플랫폼 코어 배치. Hexagon DSP들은 remoteproc(PAS)로 DT✓.

| 칩 | Linux 코어 | DSP · 보조 코어 | AI · GPU | 카메라 | CAN | 출처 |
|---|---|---|---|---|---|---|
| QCS6490 (DT: qcm6490 / sc7280) | 8x Kryo (A78 + A55) | cDSP · aDSP · MPSS(모뎀) · WPSS(Wi-Fi) | Hexagon NPU 약 12 TOPS, Adreno GPU | CAMSS + Spectra ISP | 없음 | 코어·DSP·GPU·CAMSS DT✓, TOPS 기억 |
| QRB5165 (DT: sm8250) | 8x Kryo 585 | cDSP · aDSP · SLPI(센서 허브) | Hexagon 약 15 TOPS, Adreno 650 | CAMSS + Spectra ISP | 보드에 SPI MCP2518FD | 블록 DT✓, TOPS 기억 |

- **특징**: DSP 펌웨어는 **PAS(Peripheral Authentication Service)** — TrustZone이 서명을 검증해서 올린다(`qcom_q6v5_pas`). 즉 벤더가 서명한 펌웨어 blob 없이는 DSP가 안 뜬다. Linux는 `fastrpc`(drivers/misc/fastrpc.c)로 DSP에 연산을 던진다.
- **BSP**: **Qualcomm Linux**(Yocto 기반, `meta-qcom` 계열), AI는 **Qualcomm AI Engine Direct(QNN) / AI Hub**, 로보틱스용 **Qualcomm Intelligent Robotics(QIRP) SDK**. RB5 시절에는 Ubuntu 기반 LU 빌드도 있었다. (기억 기반)
- **mainline**: 커널 쪽은 Linaro와 Qualcomm 자체의 upstream 덕에 놀랄 만큼 좋다(GPU msm/Adreno, CAMSS, remoteproc, Venus/Iris 코덱 전부 in-tree). 하지만 **NPU(Hexagon)는 fastrpc + 벤더 런타임**, ISP 화질 튜닝(Spectra)은 벤더 영역이다. `drivers/accel/qaic`는 이름 때문에 헷갈리기 쉬운데 **데이터센터용 PCIe 카드 Cloud AI 100** 드라이버다(예제 7: pci=1).
- **누가 쓰나**: 드론(QRB5165 기반 드론 사례가 있음 — 기억 기반, 회사별 확인 필요), AMR, 스마트 카메라, 산업용 핸드헬드. 5G/Wi-Fi 연결이 강점.
- **면접 포인트**: "Qualcomm은 연결성(5G, Wi-Fi 6E)과 와트당 AI 성능이 강하고 커널도 upstream이 잘 돼 있지만, DSP 펌웨어가 서명 blob이고 CAN이 없어서 로봇에서는 외부 MCU가 필수다."

### 3.5 NVIDIA Jetson Orin (Tegra234) — "AI 성능의 기준점"

```svg
<svg viewBox="0 0 680 128" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="10" width="12" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="26" y="21" font-size="12">Linux 코어</text>
<rect x="109" y="10" width="12" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="125" y="21" font-size="12">실시간 코어</text>
<rect x="206" y="10" width="12" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="222" y="21" font-size="12">가속기</text>
<rect x="272" y="10" width="12" height="12" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="288" y="21" font-size="12">카메라</text>
<rect x="338" y="10" width="12" height="12" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="354" y="21" font-size="12">CAN·I/O</text>
<line x1="420" y1="16" x2="442" y2="16" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="446" y="21" font-size="12">점선 = 기억 기반</text>
<text x="10" y="60" font-size="13">AGX Orin</text>
<rect x="112" y="40" width="83" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="58" font-size="12">12x A78AE</text>
<rect x="201" y="40" width="90" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="209" y="58" font-size="12">Ampere GPU</text>
<rect x="297" y="40" width="61" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="305" y="58" font-size="12">DLA x2</text>
<rect x="364" y="40" width="38" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="372" y="58" font-size="12">PVA</text>
<rect x="408" y="40" width="90" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="416" y="58" font-size="12">SPE/RCE R5</text>
<rect x="504" y="40" width="61" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="512" y="58" font-size="12">VI+ISP</text>
<rect x="571" y="40" width="61" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="579" y="58" font-size="12">MTTCAN</text>
<text x="10" y="104" font-size="13">Orin Nano</text>
<rect x="112" y="84" width="76" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-dasharray="4 3"/>
<text x="120" y="102" font-size="12">6x A78AE</text>
<rect x="194" y="84" width="90" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="202" y="102" font-size="12">Ampere GPU</text>
<rect x="290" y="84" width="61" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="298" y="102" font-size="12">VI+ISP</text>
<rect x="357" y="84" width="61" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="365" y="102" font-size="12">MTTCAN</text>
</svg>
```

그림 8 — Jetson Orin 코어 배치. mainline DT에서 확인되는 건 CPU 12개와 HSP mailbox뿐이라 대부분 점선이다.

| 모듈 | Linux 코어 | 보조 코어 | AI · GPU | 카메라 | CAN | 출처 |
|---|---|---|---|---|---|---|
| AGX Orin | 12x A78AE | SPE · RCE(Cortex-R5) 등 | Ampere GPU + DLA 2 + PVA, 최대 약 275 TOPS | CSI-2 최대 16 lane, VI + ISP | MTTCAN | A78AE 12 DT✓, 나머지 기억 |
| Orin NX | 6~8x A78AE | 같음 | 약 70~100 TOPS (Super 모드 더 높음) | 같음 | MTTCAN | 기억 |
| Orin Nano | 6x A78AE | 같음 | 약 20~40 TOPS (Super 모드 더 높음), DLA 없음 | 같음 | MTTCAN | 기억 (DT는 CPU 12 노드) |

- **BSP**: **JetPack** = **Jetson Linux(L4T)** + CUDA · cuDNN · TensorRT · DeepStream · Isaac ROS. JetPack 6.x = Jetson Linux 36.x, 커널 5.15 기반(기억 기반 — 데이터시트로 확인 필요). NVIDIA 커널 소스는 공개되지만(out-of-tree 모듈 `nvgpu`, `nvidia-oot`) mainline과는 별개 트리다. Yocto는 커뮤니티 `meta-tegra`(OE4T).
- **mainline**: 6.18 트리의 tegra234 DT에는 GPU, DLA, VI(카메라), MTTCAN 노드가 없다(예제 3). `nvidia,tegra234-vi`, `nvidia,tegra194-mttcan` 같은 compatible을 가진 드라이버도 없다(예제 4 앞 단계 grep). staging의 `tegra-video`는 Tegra210 시절 VI다. 실무는 100% L4T.
- **누가 쓰나**: AMR · 휴머노이드 · 농업 로봇 · 국방 엣지 AI · 드론의 "AI 뇌". 로보틱스 스타트업 채용 공고에서 가장 흔한 이름. 안전 · 실시간은 보통 옆에 STM32 같은 MCU를 따로 둔다(N4, O3).
- **면접 포인트**: "Orin은 CUDA/TensorRT 생태계 때문에 고른다. 대가로 커널은 NVIDIA가 주는 L4T 버전에 묶이고, 커스텀 캐리어 보드는 NVIDIA의 pinmux 스프레드시트 → DT 생성 흐름을 탄다. 카메라는 NVIDIA 카메라 프레임워크(Argus) 또는 V4L2 직접."

### 3.6 Raspberry Pi 5 (BCM2712 + RP1) — "프로토타입의 기본값, 그리고 CM5"

```svg
<svg viewBox="0 0 680 128" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="10" width="12" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="26" y="21" font-size="12">Linux 코어</text>
<rect x="109" y="10" width="12" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="125" y="21" font-size="12">실시간 코어</text>
<rect x="206" y="10" width="12" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="222" y="21" font-size="12">가속기</text>
<rect x="272" y="10" width="12" height="12" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="288" y="21" font-size="12">카메라</text>
<rect x="338" y="10" width="12" height="12" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="354" y="21" font-size="12">CAN·I/O</text>
<line x1="420" y1="16" x2="442" y2="16" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="446" y="21" font-size="12">점선 = 기억 기반</text>
<text x="10" y="60" font-size="13">BCM2712</text>
<rect x="112" y="40" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="58" font-size="12">4x A76</text>
<rect x="179" y="40" width="113" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="187" y="58" font-size="12">VideoCore VII</text>
<rect x="298" y="40" width="68" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="306" y="58" font-size="12">PiSP BE</text>
<rect x="372" y="40" width="68" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="380" y="58" font-size="12">PCIe x4</text>
<text x="10" y="104" font-size="13">RP1 (PCIe)</text>
<rect x="112" y="84" width="105" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="120" y="102" font-size="12">CSI x2 (CFE)</text>
<rect x="223" y="84" width="76" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="231" y="102" font-size="12">Ethernet</text>
<rect x="305" y="84" width="68" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="313" y="102" font-size="12">USB3 x2</text>
<rect x="379" y="84" width="46" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="387" y="102" font-size="12">GPIO</text>
<rect x="431" y="84" width="53" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="439" y="102" font-size="12">M3 x2</text>
</svg>
```

그림 9 — Raspberry Pi 5. BCM2712(CPU · GPU · ISP)와 RP1 사우스브리지(I/O 칩)가 PCIe로 연결된다.

| 칩 | Linux 코어 | 보조 코어 | AI · GPU | 카메라 | CAN | 출처 |
|---|---|---|---|---|---|---|
| BCM2712 | 4x A76 | 없음 | VideoCore VII(v3d 7.1), NPU 없음(AI HAT의 Hailo로 보강) | PiSP Back End ISP | 없음 | A76·VideoCore(vc6) DT✓, PiSP·v3d 드라이버 DT✓(노드는 아직 없음), Hailo 기억 |
| RP1 | 없음 | Cortex-M3 2(기억) | 없음 | CSI-2 2(CFE = PiSP Front End) | 없음(SPI HAT로 MCP2515/251xFD) | RP1 GPIO·GEM·USB DT✓, CFE 드라이버 DT✓(노드 없음) |

- **구조의 특이점**: I/O(GPIO, UART, SPI, I2C, Ethernet MAC, USB, CSI/DSI)가 **SoC 밖의 RP1 칩**에 있고 PCIe 2.0 x4로 붙는다. 커널에서는 `drivers/misc/rp1/rp1_pci.c`가 PCI 장치(`pci1de4,1`)로 RP1을 잡고, 그 아래 DT 오버레이(`rp1-common.dtsi`)로 플랫폼 장치들을 만든다(DT✓ — 2.3절 출력). PCIe 장치 밑에 DT 서브트리를 붙이는 흥미로운 패턴이다.
- **BSP**: Raspberry Pi OS, 벤더 커널 `github.com/raspberrypi/linux`(rpi-6.x.y 브랜치), 카메라는 **libcamera + rpicam-apps**, Yocto는 `meta-raspberrypi`. 제품용은 **Compute Module 5(CM5)**. (기억 기반)
- **mainline**: CPU · 디스플레이(vc4/vc6) · RP1 일부는 들어왔고, PiSP BE와 RP1 CFE 드라이버는 6.18 `drivers/media`에 있지만 해당 DT 노드는 아직 mainline DT에 없다(예제 12의 "드라이버가 DT보다 앞서 있는" 패턴과 같은 결). 실무는 벤더 커널.
- **누가 쓰나**: 프로토타입, 교육, 소량 산업 제품(CM5), 로봇 연구 플랫폼. 국방 · 자동차 양산 제품에는 드물다(온도 등급 · 공급 프로그램 때문 — 기억 기반).
- **면접 포인트**: "RPi5는 카메라 소프트웨어(libcamera)가 가장 성숙한 플랫폼이라 비전 프로토타입에 좋지만, CAN · 실시간 코어 · 산업 등급이 없어서 제품에서는 CM5 + 외부 MCU 구성이 된다."

### 3.7 STMicroelectronics STM32MP1 / STM32MP2 — "MCU 회사가 만든 리눅스 MPU"

```svg
<svg viewBox="0 0 680 128" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="10" width="12" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="26" y="21" font-size="12">Linux 코어</text>
<rect x="109" y="10" width="12" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="125" y="21" font-size="12">실시간 코어</text>
<rect x="206" y="10" width="12" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="222" y="21" font-size="12">가속기</text>
<rect x="272" y="10" width="12" height="12" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="288" y="21" font-size="12">카메라</text>
<rect x="338" y="10" width="12" height="12" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="354" y="21" font-size="12">CAN·I/O</text>
<line x1="420" y1="16" x2="442" y2="16" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="446" y="21" font-size="12">점선 = 기억 기반</text>
<text x="10" y="60" font-size="13">STM32MP157</text>
<rect x="112" y="40" width="53" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="58" font-size="12">2x A7</text>
<rect x="171" y="40" width="31" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="179" y="58" font-size="12">M4</text>
<rect x="208" y="40" width="61" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="216" y="58" font-size="12">GPU GC</text>
<rect x="275" y="40" width="46" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="283" y="58" font-size="12">DCMI</text>
<rect x="327" y="40" width="76" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="335" y="58" font-size="12">FDCAN x2</text>
<text x="10" y="104" font-size="13">STM32MP257</text>
<rect x="112" y="84" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="102" font-size="12">2x A35</text>
<rect x="179" y="84" width="38" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="187" y="102" font-size="12">M33</text>
<rect x="223" y="84" width="38" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="231" y="102" font-size="12">NPU</text>
<rect x="267" y="84" width="38" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="275" y="102" font-size="12">GPU</text>
<rect x="311" y="84" width="98" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="319" y="102" font-size="12">CSI2+DCMIPP</text>
<rect x="415" y="84" width="53" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="423" y="102" font-size="12">FDCAN</text>
</svg>
```

그림 10 — STM32MP 코어 배치. MP157은 A7(32비트) + M4, MP257은 A35(64비트) + M33.

| 칩 | Linux 코어 | 실시간 코어 | AI · GPU | 카메라 | CAN | 출처 |
|---|---|---|---|---|---|---|
| STM32MP157 | 2x A7 (32비트) | M4 | GPU GC Nano, AI 없음 | DCMI(병렬) | FDCAN 2 | A7·M4·GPU·CAN DT✓, DCMI 기억 |
| STM32MP257 | 2x A35 | M33 (+ 저전력 M0+) | NPU 약 1.35 TOPS + GPU | CSI-2 + DCMIPP(경량 ISP) | FDCAN 3 | A35·CSI·DCMIPP DT✓, 나머지 기억 |

- **BSP**: **OpenSTLinux**(STM32 MPU Ecosystem), Yocto `meta-st-stm32mp`, 벤더 커널 `github.com/STMicroelectronics/linux`, M 코어는 **STM32Cube**, AI는 **X-LINUX-AI**. (기억 기반)
- **mainline**: MP1은 아주 좋다(예제 4: `stm32_rproc`, FDCAN = 표준 Bosch M_CAN 드라이버, etnaviv). MP2는 진행 중이다 — 6.18 DT에 CSI-2 · DCMIPP는 있지만 FDCAN · M33 remoteproc · GPU/NPU 노드는 없다. 흥미롭게도 `CONFIG_STM32_RPROC`은 arm64 defconfig에도 랩 커널에도 꺼져 있다(예제 6).
- **누가 쓰나**: 산업용 게이트웨이, PLC, 의료 장비, 스마트 계량기 — "STM32 MCU 팀이 리눅스를 처음 도입할 때". MCU 툴체인(CubeMX, CubeIDE)이 그대로 이어지는 게 장점.
- **면접 포인트**: "STM32MP는 성능보다 **MCU 개발 흐름과 장기 공급, 낮은 전력**이 강점이고 MP1은 mainline 완성도가 높다. 비전 AI가 필요하면 MP2의 작은 NPU 정도까지."

### 3.8 Renesas RZ/G2L / RZ/V2H — "차량 회사의 산업용 라인 + DRP-AI"

```svg
<svg viewBox="0 0 680 128" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="10" width="12" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="26" y="21" font-size="12">Linux 코어</text>
<rect x="109" y="10" width="12" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="125" y="21" font-size="12">실시간 코어</text>
<rect x="206" y="10" width="12" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="222" y="21" font-size="12">가속기</text>
<rect x="272" y="10" width="12" height="12" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="288" y="21" font-size="12">카메라</text>
<rect x="338" y="10" width="12" height="12" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="354" y="21" font-size="12">CAN·I/O</text>
<line x1="420" y1="16" x2="442" y2="16" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="446" y="21" font-size="12">점선 = 기억 기반</text>
<text x="10" y="60" font-size="13">RZ/G2L</text>
<rect x="112" y="40" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="58" font-size="12">2x A55</text>
<rect x="179" y="40" width="38" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="187" y="58" font-size="12">M33</text>
<rect x="223" y="40" width="76" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="231" y="58" font-size="12">Mali-G31</text>
<rect x="305" y="40" width="76" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="313" y="58" font-size="12">CRU+CSI2</text>
<rect x="387" y="40" width="61" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="395" y="58" font-size="12">CAN-FD</text>
<text x="10" y="104" font-size="13">RZ/V2H</text>
<rect x="112" y="84" width="61" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="102" font-size="12">4x A55</text>
<rect x="179" y="84" width="53" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="187" y="102" font-size="12">R8 x2</text>
<rect x="238" y="84" width="38" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="246" y="102" font-size="12">M33</text>
<rect x="282" y="84" width="68" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="290" y="102" font-size="12">DRP-AI3</text>
<rect x="356" y="84" width="76" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="364" y="102" font-size="12">Mali-G31</text>
<rect x="438" y="84" width="38" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="446" y="102" font-size="12">ISP</text>
<rect x="482" y="84" width="61" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="490" y="102" font-size="12">CAN-FD</text>
</svg>
```

그림 11 — Renesas RZ 코어 배치. RZ/V2H는 실시간 코어와 DRP-AI 가속기가 있지만 6.18 DT에는 아직 없다.

| 칩 | Linux 코어 | 실시간 코어 | AI · GPU | 카메라 | CAN | 출처 |
|---|---|---|---|---|---|---|
| RZ/G2L (r9a07g044) | 2x A55 | M33 | Mali-G31, AI 없음 | MIPI CSI-2 + CRU | CAN-FD | A55·GPU·CSI·CRU·CAN DT✓, M33 기억 |
| RZ/V2H (r9a09g057) | 4x A55 | R8 2 + M33 | DRP-AI3(희소 연산 기준 수십 TOPS급), Mali-G31 | ISP + CSI-2 | CAN-FD | A55·GPU DT✓, 나머지 기억 |

- **BSP**: **RZ/G Verified Linux Package(VLP)** — **CIP(Civil Infrastructure Platform) SLTS 커널**(10년 이상 유지되는 장기 커널) 기반, Yocto `meta-renesas`. RZ/V는 **RZ/V AI SDK + DRP-AI TVM**. (기억 기반)
- **mainline**: RZ/G2L은 좋다(예제 4: CAN-FD는 R-Car 드라이버 `rcar_canfd` 재사용, 카메라는 `rzg2l-cru`, GPU는 panfrost). RZ/V2H는 CPU · GPU · 클럭 · 핀 정도. 그런데 예제 12에서 보듯 `rzg2l-csi2.c`는 이미 `renesas,r9a09g057-csi2`를 알고 있다 — 드라이버는 V2H를 준비했는데 V2H DT 노드는 아직이다.
- **누가 쓰나**: 산업용 HMI, 비전 AI 카메라, 서비스 로봇(RZ/V2H는 로봇 · AMR을 겨냥해 R8 실시간 코어를 넣었다고 발표 — 기억 기반).
- **면접 포인트**: "Renesas는 CIP 커널로 10년 유지라는 스토리가 강하고, RZ/V2H는 DRP-AI + 실시간 R8 코어로 로봇 컨트롤러 한 칩 통합을 노린다. 다만 mainline은 G2L이 앞서 있다."

### 3.9 한 장 요약 표

| 패밀리 | 대표 칩 | Linux 코어 (DT✓) | 실시간 코어 | AI (기억 기반) | CAN-FD | BSP / Yocto 레이어 (기억 기반) | mainline 점수 (ex8) |
|---|---|---|---|---|---|---|---|
| NXP | i.MX8M Plus | 4x A53 | M7 | NPU 약 2.3 TOPS | 2 | LF BSP / meta-imx, meta-freescale | 6/7 |
| NXP | i.MX93 | 2x A55 | M33 | Ethos-U65 약 0.5 | 2 | 같음 | 3/7 |
| NXP | i.MX95 | 6x A55 | M7 + M33 | Neutron 약 2 | 5 | 같음 | 2/7 |
| TI | AM62x | 4x A53 | R5F + M4F + PRU | 없음 | 3 | Processor SDK / meta-ti, meta-arago | 5/7 |
| TI | AM62A | 4x A53 | R5F 2 | C7x+MMA 약 1~2 | 3 | Edge AI SDK / meta-ti | 4/7 |
| TI | AM69 | 8x A72 | R5F 8 | C7x 4 약 32 | 20 | Edge AI SDK / meta-ti | 4/7 |
| Rockchip | RK3588 | 4x A76 + 4x A55 | 없음 | NPU 약 6 | 있음(DT 없음) | Rockchip SDK / meta-rockchip | 2/7 |
| Qualcomm | QCS6490 | 8x Kryo | Hexagon DSP들 | 약 12 | 없음 | Qualcomm Linux / meta-qcom | 4/7 |
| NVIDIA | AGX Orin | 12x A78AE | R5 계열 | 약 275 | MTTCAN | JetPack · L4T / meta-tegra | 1/7 |
| RPi | BCM2712 + RP1 | 4x A76 | (RP1 M3) | 없음 | 없음 | RPi OS / meta-raspberrypi | 2/7 |
| ST | STM32MP157 | 2x A7 | M4 | 없음 | 2 | OpenSTLinux / meta-st-stm32mp | 4/7 |
| ST | STM32MP257 | 2x A35 | M33 | 약 1.35 | 3 | 같음 | 2/7 |
| Renesas | RZ/G2L | 2x A55 | M33 | 없음 | 있음 | VLP(CIP) / meta-renesas | 4/7 |
| Renesas | RZ/V2H | 4x A55 | R8 2 + M33 | DRP-AI3 | 있음 | RZ/V AI SDK / meta-renesas | 1/7 |

---

## 4. mainline 지원 매트릭스 — 트리에서 직접 만든다

이 절이 이 노트의 "실측" 부분이다. 질문은 하나: **"레퍼런스 보드 DT에 있는 각 IP 블록을 6.18.55의 어떤 드라이버가 잡는가?"**

### 4.1 예제 4 — DT 노드의 compatible 목록 전체로 드라이버 찾기

처음에는 SoC 고유 compatible(예: `fsl,imx8mp-mipi-csi2`)만 `drivers/`에서 grep했다. 그러면 절반이 "드라이버 없음"으로 나온다. 실제로는 DT 노드가 **fallback compatible**을 같이 적고, 드라이버는 그 일반 이름을 매칭하기 때문이다(D3, G2):

```dts
/* imx8mp.dtsi (6.18.55) — 앞이 구체적, 뒤가 일반 */
compatible = "fsl,imx8mp-mipi-csi2", "fsl,imx8mm-mipi-csi2";
```

커널의 `of_match_device()`는 노드의 compatible 목록을 앞에서부터 보면서 드라이버의 `of_device_id` 표와 맞춰 본다. 그래서 스크립트도 목록 전체를 봐야 한다.

```python
#!/usr/bin/env python3
"""ex4_matrix.py <kernel> <flat.dts>...: which in-tree driver binds each SoC IP node
(walks the node's whole compatible list, so fallback compatibles count)"""
import os, re, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
K = sys.argv[1]
idx = collections.defaultdict(set)              # compatible -> driver files
for root in ("drivers", "sound"):
    for d, _, fs in os.walk(os.path.join(K, root)):
        for f in fs:
            if f.endswith(".c"):
                p = os.path.join(d, f)
                for c in re.findall(r'\.compatible\s*=\s*"([^"]+)"', open(p, errors="ignore").read()):
                    idx[c].add(os.path.relpath(p, K))
CLS = [("NPU", r"rknn|ethos|-npu$|neutron"), ("GPU", r"mali|adreno|gpu$|v3d|vc6|powervr|vivante,gc"),
       ("ISP", r"(^|[,-])isp$|camss|pispbe|dcmipp|renesas,.*-cru"), ("CSI", r"csi2?(rx)?$|csis|rp1-cfe"),
       ("CAN", r"m_can|flexcan|canfd|-can$"), ("RPROC", r"r5fss|cm4$|cm7$|cm33$|-m4$|-dsp$|-pas$")]
for fn in sys.argv[2:]:
    print("==", os.path.basename(fn).replace(".flat.dts", ""))
    seen, node = set(), ""
    for ln in open(fn):
        n = re.match(r'\s*(?:\w+:\s*)?([\w,.+-]+)(@\w+)?\s*\{', ln)
        if n:
            node = n.group(1)
        m = re.match(r'\s*compatible = (.*);', ln)
        if not m:
            continue
        cl = re.findall(r'"([^"]+)"', m.group(1))
        for cls, rx in CLS:
            if re.search(rx, cl[0]) and (cl[0], node) not in seen:
                seen.add((cl[0], node))
                if cl[0] == "vivante,gc" and node == "npu":
                    cls = "NPU"
                hit = next((c for c in cl if c in idx), None)
                drv = sorted(idx[hit])[0].replace("drivers/", "") if hit else "-- NO DRIVER"
                via = f" (via {hit})" if hit and hit != cl[0] else ""
                print(f"  {cls:5} {cl[0]:28} -> {drv}{via}")
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/O5/dt && python3 -I ~/.elinux_scratch/O5/ex4_matrix.py \
  /var/lab/src/linux-6.18.55 imx8mp-evk.flat.dts imx93-11x11-evk.flat.dts ... r9a07g044l2-smarc.flat.dts'
```

```text
== imx8mp-evk
  CAN   fsl,imx8mp-flexcan           -> net/can/flexcan/flexcan-core.c
  ISP   fsl,imx8mp-isp               -> media/platform/rockchip/rkisp1/rkisp1-dev.c
  CSI   fsl,imx8mp-mipi-csi2         -> media/platform/nxp/imx-mipi-csis.c (via fsl,imx8mm-mipi-csi2)
  GPU   vivante,gc                   -> gpu/drm/etnaviv/etnaviv_gpu.c
  NPU   vivante,gc                   -> gpu/drm/etnaviv/etnaviv_gpu.c
== imx93-11x11-evk
  CAN   fsl,imx93-flexcan            -> net/can/flexcan/flexcan-core.c
  RPROC fsl,imx93-cm33               -> remoteproc/imx_rproc.c
== k3-am625-sk
  CAN   bosch,m_can                  -> net/can/m_can/m_can_platform.c
  RPROC ti,am62-r5fss                -> remoteproc/ti_k3_r5_remoteproc.c
  GPU   ti,am62-gpu                  -> gpu/drm/imagination/pvr_drv.c (via img,img-axe)
  CSI   ti,j721e-csi2rx              -> media/platform/cadence/cdns-csi2rx.c (via cdns,csi2rx)
== k3-am62a7-sk
  CAN   bosch,m_can                  -> net/can/m_can/m_can_platform.c
  RPROC ti,am62-r5fss                -> remoteproc/ti_k3_r5_remoteproc.c
  CSI   ti,am62a-dmss-bcdma-csirx    -> dma/ti/k3-udma.c
  CSI   ti,j721e-csi2rx              -> media/platform/cadence/cdns-csi2rx.c (via cdns,csi2rx)
  RPROC ti,am62a-c7xv-dsp            -> remoteproc/ti_k3_dsp_remoteproc.c
== rk3568-evb1-v10
  GPU   rockchip,rk3568-mali         -> gpu/drm/panfrost/panfrost_drv.c (via arm,mali-bifrost)
  CAN   rockchip,rk3568v2-canfd      -> net/can/rockchip/rockchip_canfd-core.c
== rk3588-rock-5b
  GPU   rockchip,rk3588-mali         -> gpu/drm/panthor/panthor_drv.c
  NPU   rockchip,rk3588-rknn-core    -> accel/rocket/rocket_drv.c
== qcs6490-rb3gen2
  GPU   qcom,adreno-635.0            -> gpu/drm/msm/adreno/adreno_device.c (via qcom,adreno)
  GPU   qcom,adreno-gmu-635.0        -> iommu/arm/arm-smmu/arm-smmu-qcom.c (via qcom,adreno-gmu)
  RPROC qcom,sc7280-mpss-pas         -> remoteproc/qcom_q6v5_pas.c
  RPROC qcom,sc7280-adsp-pas         -> remoteproc/qcom_q6v5_pas.c
  RPROC qcom,sc7280-wpss-pas         -> remoteproc/qcom_q6v5_pas.c
  RPROC qcom,sc7280-cdsp-pas         -> remoteproc/qcom_q6v5_pas.c
  ISP   qcom,sc7280-camss            -> media/platform/qcom/camss/camss.c
== tegra234-p3768-0000+p3767-0005
== bcm2712-rpi-5-b
  GPU   brcm,bcm2712-vc6             -> gpu/drm/vc4/vc4_drv.c
== stm32mp257f-ev1
  CSI   st,stm32mp25-csi             -> media/platform/st/stm32/stm32-csi.c
  ISP   st,stm32mp25-dcmipp          -> media/platform/st/stm32/stm32-dcmipp/dcmipp-core.c
== r9a07g044l2-smarc
  CAN   renesas,r9a07g044-canfd      -> net/can/rcar/rcar_canfd.c (via renesas,rzg2l-canfd)
  ISP   renesas,r9a07g044-cru        -> media/platform/renesas/rzg2l-cru/rzg2l-core.c (via renesas,rzg2l-cru)
  CSI   renesas,r9a07g044-csi2       -> media/platform/renesas/rzg2l-cru/rzg2l-csi2.c (via renesas,rzg2l-csi2)
  GPU   renesas,r9a07g044-mali       -> gpu/drm/panfrost/panfrost_drv.c (via arm,mali-bifrost)
```

출력에서 볼 것:

- **IP 재사용이 드러난다**: i.MX8MP ISP → `rockchip/rkisp1`, RZ/G2L CAN-FD → `rcar/rcar_canfd.c`, TI AM62 CSI-2 → `cadence/cdns-csi2rx.c`(TI가 Cadence IP를 씀), TI · STM32 CAN → Bosch `m_can`, RK3568 · RZ/G2L GPU → `panfrost`(Arm Mali-Bifrost), AM62 GPU → `imagination/pvr_drv.c`(`img,img-axe`). **SoC 벤더는 IP를 사 오고, 커널은 IP 단위로 드라이버를 짠다.** 면접에서 "그 SoC의 CAN 드라이버 아세요?"에 "그거 Bosch M_CAN이라 m_can 드라이버죠"라고 답할 수 있으면 강하다.
- **AI 가속기의 세 가지 모습**: RK3588 NPU → `accel/rocket`(진짜 DRM accel 드라이버), i.MX8MP NPU → `drm/etnaviv`(GPU 드라이버가 NPU도 맡음), AM62A C7x → `remoteproc/ti_k3_dsp_remoteproc.c`(DSP에 펌웨어만 올림), Qualcomm cDSP → `remoteproc/qcom_q6v5_pas.c`(+ fastrpc). O4에서 본 구분이 실제 트리에서 그대로 보인다.
- **Tegra234는 한 줄도 안 나온다.** RPi5는 `vc4`(디스플레이 파이프라인) 하나뿐인데, 이것은 3D GPU(v3d)가 아니다 — 분류 정규식의 한계다(4.6절).
- `qcom,adreno-gmu → arm-smmu-qcom.c`는 오탐이다. SMMU 드라이버가 quirk 표에 그 compatible을 갖고 있을 뿐, GMU 자체는 msm 드라이버가 맡는다. **compatible grep은 "후보"를 주지 "정답"을 주지 않는다** — 마지막 확인은 그 파일의 `of_device_id`가 `platform_driver`의 `.of_match_table`인지 보는 것이다.

### 4.2 예제 8 — 보드별 지원 그리드(이 노트의 매트릭스)

예제 4를 Y/n/. 그리드로 압축한다. 이 점수가 그림 1의 가로축이다.

```python
#!/usr/bin/env python3
"""ex8_grid.py <kernel> <flat.dts>...: mainline support grid per board.
Y = DT node exists and an in-tree driver matches it, n = node but no driver, . = no node"""
import os, re, sys, collections
K = sys.argv[1]
idx = set()
for d, _, fs in os.walk(os.path.join(K, "drivers")):
    for f in fs:
        if f.endswith(".c"):
            idx.update(re.findall(r'\.compatible\s*=\s*"([^"]+)"',
                                  open(os.path.join(d, f), errors="ignore").read()))
CLS = [("GPU", r"mali|adreno|gpu$|v3d|vc6|powervr"), ("NPU", r"rknn|ethos|-npu$|neutron"),
       ("ISP", r"(^|[,-])isp$|camss|pispbe|dcmipp|renesas,.*-cru"),
       ("CSI", r"csi2?(rx)?$|csis|rp1-cfe|-isi$"), ("CAN", r"m_can|flexcan|canfd|-can$"),
       ("RPROC", r"r5fss|cm4$|cm7$|cm33$|-m4$|-dsp$|-pas$"), ("MBOX", r"mailbox|-mu$|-hsp$|ipcc|mbox")]
print(f"{'board':34}" + "".join(f"{c:>6}" for c, _ in CLS) + "  score")
for fn in sys.argv[2:]:
    st = collections.defaultdict(lambda: ".")
    node = ""
    for ln in open(fn):
        n = re.match(r'\s*(?:\w+:\s*)?([\w,.+-]+)(@\w+)?\s*\{', ln)
        if n:
            node = n.group(1)
        m = re.match(r'\s*compatible = (.*);', ln)
        if not m:
            continue
        cl = re.findall(r'"([^"]+)"', m.group(1))
        for cls, rx in CLS:
            hit = re.search(rx, cl[0]) or (cl[0] == "vivante,gc" and node == cls.lower())
            if hit and st[cls] != "Y":
                st[cls] = "Y" if any(c in idx for c in cl) else "n"
    score = sum(st[c] == "Y" for c, _ in CLS)
    name = os.path.basename(fn).replace(".flat.dts", "")
    print(f"{name:34}" + "".join(f"{st[c]:>6}" for c, _ in CLS) + f"  {score}/{len(CLS)}")
```

```text
board                                GPU   NPU   ISP   CSI   CAN RPROC  MBOX  score
bcm2712-rpi-5-b                        Y     .     .     .     .     .     Y  2/7
imx8mp-evk                             Y     Y     Y     Y     Y     .     Y  6/7
imx93-11x11-evk                        .     .     .     .     Y     Y     Y  3/7
imx95-19x19-evk                        .     .     .     .     Y     .     Y  2/7
k3-am625-sk                            Y     .     .     Y     Y     Y     Y  5/7
k3-am62a7-sk                           .     .     .     Y     Y     Y     Y  4/7
k3-am642-evm                           .     .     .     .     Y     Y     Y  3/7
k3-am69-sk                             .     .     .     Y     Y     Y     Y  4/7
qcs6490-rb3gen2                        Y     .     Y     .     .     Y     Y  4/7
qrb5165-rb5                            Y     .     Y     .     .     Y     Y  4/7
r9a07g044l2-smarc                      Y     .     Y     Y     Y     .     .  4/7
r9a09g057h44-rzv2h-evk                 Y     .     .     .     .     .     .  1/7
rk3568-evb1-v10                        Y     .     .     .     Y     .     .  2/7
rk3588-rock-5b                         Y     Y     .     .     .     .     .  2/7
stm32mp157f-dk2                        Y     .     .     .     Y     Y     Y  4/7
stm32mp257f-ev1                        .     .     Y     Y     .     .     .  2/7
tegra234-p3768-0000+p3767-0005         .     .     .     .     .     .     Y  1/7
```

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<text x="238" y="20" font-size="12">GPU</text>
<text x="296" y="20" font-size="12">NPU</text>
<text x="354" y="20" font-size="12">ISP</text>
<text x="412" y="20" font-size="12">CSI</text>
<text x="470" y="20" font-size="12">CAN</text>
<text x="528" y="20" font-size="12">RPROC</text>
<text x="586" y="20" font-size="12">MBOX</text>
<text x="642" y="20" font-size="12">점수</text>
<text x="8" y="44" font-size="12">bcm2712-rpi-5-b</text>
<rect x="230" y="30" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="288" y="30" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="30" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="404" y="30" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="462" y="30" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="520" y="30" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="578" y="30" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="646" y="43" font-size="12">2/7</text>
<text x="8" y="64" font-size="12">imx8mp-evk</text>
<rect x="230" y="50" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="288" y="50" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="346" y="50" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="404" y="50" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="462" y="50" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="520" y="50" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="578" y="50" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="646" y="63" font-size="12">6/7</text>
<text x="8" y="84" font-size="12">imx93-11x11-evk</text>
<rect x="230" y="70" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="288" y="70" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="70" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="404" y="70" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="462" y="70" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="520" y="70" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="578" y="70" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="646" y="83" font-size="12">3/7</text>
<text x="8" y="104" font-size="12">imx95-19x19-evk</text>
<rect x="230" y="90" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="288" y="90" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="90" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="404" y="90" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="462" y="90" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="520" y="90" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="578" y="90" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="646" y="103" font-size="12">2/7</text>
<text x="8" y="124" font-size="12">k3-am625-sk</text>
<rect x="230" y="110" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="288" y="110" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="110" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="404" y="110" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="462" y="110" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="520" y="110" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="578" y="110" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="646" y="123" font-size="12">5/7</text>
<text x="8" y="144" font-size="12">k3-am62a7-sk</text>
<rect x="230" y="130" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="288" y="130" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="130" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="404" y="130" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="462" y="130" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="520" y="130" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="578" y="130" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="646" y="143" font-size="12">4/7</text>
<text x="8" y="164" font-size="12">k3-am642-evm</text>
<rect x="230" y="150" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="288" y="150" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="150" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="404" y="150" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="462" y="150" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="520" y="150" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="578" y="150" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="646" y="163" font-size="12">3/7</text>
<text x="8" y="184" font-size="12">k3-am69-sk</text>
<rect x="230" y="170" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="288" y="170" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="170" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="404" y="170" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="462" y="170" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="520" y="170" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="578" y="170" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="646" y="183" font-size="12">4/7</text>
<text x="8" y="204" font-size="12">qcs6490-rb3gen2</text>
<rect x="230" y="190" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="288" y="190" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="190" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="404" y="190" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="462" y="190" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="520" y="190" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="578" y="190" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="646" y="203" font-size="12">4/7</text>
<text x="8" y="224" font-size="12">qrb5165-rb5</text>
<rect x="230" y="210" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="288" y="210" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="210" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="404" y="210" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="462" y="210" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="520" y="210" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="578" y="210" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="646" y="223" font-size="12">4/7</text>
<text x="8" y="244" font-size="12">r9a07g044l2-smarc</text>
<rect x="230" y="230" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="288" y="230" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="230" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="404" y="230" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="462" y="230" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="520" y="230" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="578" y="230" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<text x="646" y="243" font-size="12">4/7</text>
<text x="8" y="264" font-size="12">r9a09g057h44-rzv2h-evk</text>
<rect x="230" y="250" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="288" y="250" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="250" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="404" y="250" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="462" y="250" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="520" y="250" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="578" y="250" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<text x="646" y="263" font-size="12">1/7</text>
<text x="8" y="284" font-size="12">rk3568-evb1-v10</text>
<rect x="230" y="270" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="288" y="270" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="270" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="404" y="270" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="462" y="270" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="520" y="270" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="578" y="270" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<text x="646" y="283" font-size="12">2/7</text>
<text x="8" y="304" font-size="12">rk3588-rock-5b</text>
<rect x="230" y="290" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="288" y="290" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="346" y="290" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="404" y="290" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="462" y="290" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="520" y="290" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="578" y="290" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<text x="646" y="303" font-size="12">2/7</text>
<text x="8" y="324" font-size="12">stm32mp157f-dk2</text>
<rect x="230" y="310" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="288" y="310" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="310" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="404" y="310" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="462" y="310" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="520" y="310" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="578" y="310" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="646" y="323" font-size="12">4/7</text>
<text x="8" y="344" font-size="12">stm32mp257f-ev1</text>
<rect x="230" y="330" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="288" y="330" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="330" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="404" y="330" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="462" y="330" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="520" y="330" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="578" y="330" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<text x="646" y="343" font-size="12">2/7</text>
<text x="8" y="364" font-size="12">tegra234-p3768-0000+p3767-0005</text>
<rect x="230" y="350" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="288" y="350" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="346" y="350" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="404" y="350" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="462" y="350" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="520" y="350" width="54" height="17" fill="#888" fill-opacity="0.08" stroke="#888"/>
<rect x="578" y="350" width="54" height="17" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="646" y="363" font-size="12">1/7</text>
<text x="8" y="390" font-size="12">초록 = DT 노드 + in-tree 드라이버 매칭, 주황 = 노드만, 회색 = DT에 노드 없음</text>
</svg>
```

그림 12 — mainline 지원 그리드(예제 8 실측). 주황(노드는 있는데 드라이버 없음)이 한 칸도 없다는 점이 흥미롭다 — mainline DT 규칙상 **바인딩 문서와 드라이버 없이 노드만 먼저 들어가는 경우는 드물다**(D7: dt-schema 검증). 대신 "노드 자체가 없는" 회색 칸이 미지원의 실제 모습이다.

출력에서 볼 것: 점수 6의 i.MX8MP, 5의 AM625가 가장 "mainline으로 제품 만들기 좋은" 쪽이다. RK3588이 2점인 것은 NPU · GPU는 있지만 ISP · CSI · CAN 노드가 아직 없어서다. Orin 1점은 mailbox뿐.

### 4.3 예제 6 — Kconfig · defconfig · 랩 커널에서 켜져 있나

드라이버가 트리에 있어도 **arm64 defconfig에서 켜져 있는지**는 별개다. defconfig에 들어 있다는 것은 "커널 CI(KernelCI)가 매일 이 플랫폼을 빌드 · 부팅 테스트한다"는 신호다.

```sh
#!/bin/bash
# ex6: is each platform / SoC-IP driver a Kconfig symbol, and is it on in arm64 defconfig / the lab kernel?
K=${K:-/var/lab/src/linux-6.18.55}; L=/var/lab/out/linux-build-dom/.config
D=$K/arch/arm64/configs/defconfig
printf "%-30s %-7s %-9s %s\n" symbol exists defconfig lab-dom
for s in ARCH_MXC ARCH_K3 ARCH_ROCKCHIP ARCH_QCOM ARCH_TEGRA ARCH_BCM2835 ARCH_STM32 ARCH_RENESAS ARCH_R9A07G044 ARCH_R9A09G057 \
         IMX_REMOTEPROC TI_K3_R5_REMOTEPROC TI_K3_DSP_REMOTEPROC QCOM_Q6V5_PAS STM32_RPROC \
         DRM_ACCEL_ROCKET DRM_ETNAVIV DRM_PANTHOR DRM_PANFROST DRM_POWERVR DRM_MSM DRM_V3D \
         VIDEO_ROCKCHIP_ISP1 VIDEO_QCOM_CAMSS VIDEO_RASPBERRYPI_PISP_BE VIDEO_RP1_CFE \
         VIDEO_STM32_DCMIPP VIDEO_RZG2L_CRU VIDEO_IMX8_ISI VIDEO_IMX_MIPI_CSIS \
         CAN_FLEXCAN CAN_M_CAN_PLATFORM CAN_RCAR_CANFD CAN_ROCKCHIP_CANFD MISC_RP1; do
  e=$(grep -rqs --include='Kconfig*' -E "^\s*(menu)?config $s\$" "$K/arch" "$K/drivers" && echo yes || echo NO)
  d=$(grep -E "^CONFIG_$s=" "$D" | cut -d= -f2); l=$(grep -E "^CONFIG_$s=" "$L" 2>/dev/null | cut -d= -f2)
  printf "%-30s %-7s %-9s %s\n" "$s" "$e" "${d:--}" "${l:--}"
done
```

```text
symbol                         exists  defconfig lab-dom
ARCH_MXC                       yes     y         y
ARCH_K3                        yes     y         y
ARCH_ROCKCHIP                  yes     y         y
ARCH_QCOM                      yes     y         y
ARCH_TEGRA                     yes     y         y
ARCH_BCM2835                   yes     y         y
ARCH_STM32                     yes     y         y
ARCH_RENESAS                   yes     y         y
ARCH_R9A07G044                 yes     -         y
ARCH_R9A09G057                 yes     -         y
IMX_REMOTEPROC                 yes     y         y
TI_K3_R5_REMOTEPROC            yes     m         m
TI_K3_DSP_REMOTEPROC           yes     m         m
QCOM_Q6V5_PAS                  yes     m         m
STM32_RPROC                    yes     -         -
DRM_ACCEL_ROCKET               yes     -         m
DRM_ETNAVIV                    yes     m         m
DRM_PANTHOR                    yes     m         m
DRM_PANFROST                   yes     m         m
DRM_POWERVR                    yes     m         m
DRM_MSM                        yes     m         m
DRM_V3D                        yes     m         m
VIDEO_ROCKCHIP_ISP1            yes     -         -
VIDEO_QCOM_CAMSS               yes     m         m
VIDEO_RASPBERRYPI_PISP_BE      yes     -         -
VIDEO_RP1_CFE                  yes     -         -
VIDEO_STM32_DCMIPP             yes     -         -
VIDEO_RZG2L_CRU                yes     m         m
VIDEO_IMX8_ISI                 yes     m         m
VIDEO_IMX_MIPI_CSIS            yes     m         m
CAN_FLEXCAN                    yes     m         m
CAN_M_CAN_PLATFORM             yes     m         m
CAN_RCAR_CANFD                 yes     m         m
CAN_ROCKCHIP_CANFD             yes     -         -
MISC_RP1                       yes     m         m
```

출력에서 볼 것:

- 8개 플랫폼 `ARCH_*`는 전부 defconfig에 `y`다.
- defconfig 열의 `-`는 "꺼짐"이 아니라 **"defconfig 파일에 명시 안 됨"** 이다. `make savedefconfig`는 기본값과 같은 줄을 지우기 때문이다. 예: `ARCH_R9A07G044`는 defconfig에 없지만 `default ARCH_RENESAS`라서 실제 `.config`(랩-dom 열)에서는 `y`다. **판단은 항상 완성된 `.config`로 한다.**
- `DRM_ACCEL_ROCKET`, `VIDEO_ROCKCHIP_ISP1`, `CAN_ROCKCHIP_CANFD`, `VIDEO_RASPBERRYPI_PISP_BE`, `VIDEO_STM32_DCMIPP`는 defconfig에 없다 → 그 플랫폼을 mainline으로 쓰려면 직접 켜야 한다. 랩 커널 Image-dom은 rocket만 참조용 `=m`으로 켰다.

### 4.4 예제 7 — `drivers/accel`에는 무엇이 있나

```sh
#!/bin/bash
# ex7: what lives in drivers/accel (the DRM "compute accelerator" class) in 6.18.55, and how big each is
K=${K:-/var/lab/src/linux-6.18.55}; cd "$K/drivers/accel" || exit 1
printf "%-12s %6s  %s\n" dir lines kconfig-prompt
for d in */; do d=${d%/}
  n=$(find "$d" -name '*.[ch]' -exec cat {} + | wc -l)
  p=$(grep -h -m1 -E '^\s*tristate "' "$d/Kconfig" | sed 's/^\s*tristate //')
  printf "%-12s %6d  %s\n" "$d" "$n" "$p"
done
echo "--- of_match (DT-probed = SoC-integrated) vs pci_device_id (PCIe card / chipset)"
for d in */; do d=${d%/}
  o=$(grep -rl 'of_device_id' "$d" | wc -l); p=$(grep -rl 'pci_device_id' "$d" | wc -l)
  printf "%-12s of=%d pci=%d\n" "$d" "$o" "$p"
done
```

```text
dir           lines  kconfig-prompt
amdxdna        8756  "AMD AI Engine"
habanalabs   337290  "HabanaLabs AI accelerators"
ivpu          14098  "Intel NPU (Neural Processing Unit)"
qaic           7890  "Qualcomm Cloud AI accelerators"
rocket         5933  "Rocket (support for Rockchip NPUs)"
--- of_match (DT-probed = SoC-integrated) vs pci_device_id (PCIe card / chipset)
amdxdna      of=0 pci=1
habanalabs   of=0 pci=1
ivpu         of=0 pci=1
qaic         of=0 pci=1
rocket       of=1 pci=0
```

출력에서 볼 것: 6.18의 accel 드라이버 5개 중 **SoC 내장 NPU(DT로 probe)는 rocket 하나뿐**이고 나머지 넷(AMD XDNA, Habana Gaudi, Intel NPU, Qualcomm Cloud AI 100)은 전부 PCI 장치다. 임베디드 NPU의 mainline 지원은 이제 막 시작된 단계라는 뜻이다 — Don이 AI 가속기 SoC 쪽으로 가려 한다면 바로 이 빈칸이 기회다(O4).

### 4.5 예제 5 · 10 · 12 — MAINTAINERS, SoC ID 드라이버, "드라이버가 DT보다 앞선" 경우

MAINTAINERS 파일에서 벤더별 섹션 수와, 특정 드라이버 경로의 담당 섹션을 찾는다(P7 upstream 기여에서 `scripts/get_maintainer.pl`이 하는 일의 축소판).

```sh
#!/bin/bash
# ex5: MAINTAINERS sections per vendor family (title line = line before the first "M:"-style field)
K=${K:-/var/lab/src/linux-6.18.55}
awk '/^[A-Z0-9].*[A-Z]/ && !/^[A-Z]:\t/ {t=$0} /^F:\t/ {f=$2; sub(/\*.*$/,"",f); print t "\t" f}' "$K/MAINTAINERS" > /tmp/o5_mf.tsv
printf "%-10s %8s  %s\n" family sections sample-titles
while IFS=: read -r lab fam; do
  n=$(cut -f1 /tmp/o5_mf.tsv | sort -u | grep -E -c "$fam")
  s=$(cut -f1 /tmp/o5_mf.tsv | sort -u | grep -E "$fam" | head -2 | cut -c1-34 | paste -sd';' -)
  printf "%-10s %8d  %s\n" "$lab" "$n" "$s"
done <<'FAM'
NXP:\bI\.?MX|NXP|FREESCALE
TI:\bK3\b|OMAP|^TI |TEXAS INSTR
Rockchip:ROCKCHIP
Qualcomm:QUALCOMM|\bQCOM
NVIDIA:\bTEGRA|NVIDIA
RPi:BCM2712|BCM2835|RASPBERRY|\bRP1\b
ST:STM32
Renesas:RENESAS|\bRZ/
FAM
echo "--- who owns the SoC-specific drivers? (sections covering these paths)"
for p in drivers/accel/rocket drivers/remoteproc/ti_k3 drivers/remoteproc/imx_rproc \
         drivers/media/platform/raspberrypi/pisp_be drivers/net/can/rockchip drivers/gpu/drm/tegra; do
  printf "%-44s %s\n" "$p" "$(awk -F'\t' -v p="$p" 'index($2,p)==1 || index(p,$2)==1 {print length($2) "\t" $1}' /tmp/o5_mf.tsv \
      | sort -rn | head -1 | cut -f2)"
done
rm -f /tmp/o5_mf.tsv
```

```text
family     sections  sample-titles
NXP              87  ARM/FREESCALE IMX / MXC ARM ARCHIT;ARM/FREESCALE LAYERSCAPE ARM ARCHI
TI               75  ARM/TEXAS INSTRUMENT AEMIF/EMIF DR;ARM/TEXAS INSTRUMENT KEYSTONE ARCH
Rockchip         12  DRM ACCEL DRIVER FOR ROCKCHIP NPU;DRM DRIVERS FOR ROCKCHIP
Qualcomm         52  ARM/QUALCOMM CHROMEBOOK SUPPORT;ARM/QUALCOMM MAILING LIST
NVIDIA           27  ARM/TEGRA HDMI CEC SUBSYSTEM SUPPO;CORE DRIVER FOR NVIDIA GPUS [RUST]
RPi               5  BROADCOM BCM2711/BCM2835 ARM ARCHI;BROADCOM BCM2835 CAMERA DRIVERS
ST               14  ARM/STM32 ARCHITECTURE;MEDIA DRIVERS FOR STM32 - CSI
Renesas          46  ARM/RISC-V/RENESAS ARCHITECTURE;DRM DRIVERS FOR RENESAS R-CAR
--- who owns the SoC-specific drivers? (sections covering these paths)
drivers/accel/rocket                         DRM ACCEL DRIVER FOR ROCKCHIP NPU
drivers/remoteproc/ti_k3                     REMOTE PROCESSOR (REMOTEPROC) SUBSYSTEM
drivers/remoteproc/imx_rproc                 REMOTE PROCESSOR (REMOTEPROC) SUBSYSTEM
drivers/media/platform/raspberrypi/pisp_be   RASPBERRY PI PISP BACK END
drivers/net/can/rockchip                     ROCKCHIP CAN-FD DRIVER
drivers/gpu/drm/tegra                        DRM DRIVERS FOR NVIDIA TEGRA
```

출력에서 볼 것: 섹션 수는 정규식이 거칠어서(예: "NXP"는 NXP PHY 드라이버도 잡는다) 대략적인 크기 비교로만 본다. 더 의미 있는 건 아래쪽 — rocket, PiSP BE, Rockchip CAN-FD, Tegra DRM은 **전용 섹션(전용 메인테이너)이 있다.** 반면 imx_rproc · ti_k3 remoteproc은 remoteproc 서브시스템 섹션이 가장 구체적인 매칭이다(예제 스크립트는 F: 경로 최장 일치만 본다).

SoC 식별 드라이버(`/sys/devices/soc0`를 만드는 `soc_device_register` 호출자)도 벤더마다 다르다:

```sh
#!/bin/bash
# ex10: which vendor SoC-ID drivers populate /sys/devices/soc0 (soc_device_register callers)
K=${K:-/var/lab/src/linux-6.18.55}; cd "$K" || exit 1
for v in imx ti rockchip qcom tegra bcm st renesas; do
  f=$(grep -rl 'soc_device_register' drivers/soc/$v 2>/dev/null | sed 's#drivers/soc/##' | paste -sd' ' -)
  printf "%-9s %s\n" "$v" "${f:--- none}"
done
echo "--- fuse/ID registers read for i.MX8M (what soc0/revision shows)"
grep -n -m3 'OCOTP\|anatop' drivers/soc/imx/soc-imx8m.c
```

```text
imx       imx/soc-imx8m.c imx/soc-imx.c imx/soc-imx9.c
ti        ti/k3-socinfo.c
rockchip  -- none
qcom      qcom/socinfo.c
tegra     tegra/fuse/fuse-tegra.c
bcm       bcm/brcmstb/common.c
st        -- none
renesas   renesas/renesas-soc.c renesas/rz-sysc.c renesas/Makefile
--- fuse/ID registers read for i.MX8M (what soc0/revision shows)
23:#define OCOTP_UID_LOW			0x410
24:#define OCOTP_UID_HIGH			0x420
26:#define IMX8MP_OCOTP_UID_OFFSET		0x10
```

출력에서 볼 것: NXP · TI · Qualcomm · Tegra · Renesas는 fuse/ID 레지스터를 읽어 `soc0/family`, `soc_id`, `revision`을 보여 준다. 드라이버는 `soc_device_match()`로 "실리콘 리비전 A0에서만 이 errata 우회" 같은 quirk를 건다 — Don이 새 실리콘 bring-up에서 보던 "stepping별 workaround"가 Linux에서는 이렇게 표현된다. Rockchip · ST는 이 경로가 없다.

마지막으로 "드라이버는 이미 이 SoC를 아는데 DT 노드가 없는" compatible을 찾는다:

```sh
#!/bin/bash
# ex12: drivers that already list an SoC compatible which no in-tree .dts/.dtsi uses yet
K=${K:-/var/lab/src/linux-6.18.55}; cd "$K" || exit 1
for soc in r9a09g057 imx95 stm32mp25 rk3588 tegra234 bcm2712 am62a; do
  grep -rhoE "\.compatible\s*=\s*\"[a-z0-9,-]*$soc[a-z0-9-]*\"" drivers --include='*.c' \
    | sed -E 's/.*"(.*)"/\1/' | sort -u > /tmp/o5_drv.txt
  n=0; miss=""
  while read -r c; do
    grep -rqs --include='*.dts*' "\"$c\"" arch/arm64/boot/dts || { n=$((n+1)); miss="$miss $c"; }
  done < /tmp/o5_drv.txt
  printf "%-10s driver-compat=%-3d not-in-any-DT=%-2d %s\n" "$soc" "$(wc -l < /tmp/o5_drv.txt)" "$n" \
    "$(echo $miss | cut -c1-60)"
done; rm -f /tmp/o5_drv.txt
```

```text
bash: line 1: /Users/donh/.elinux_scratch/O5/ex2.out: Read-only file system
r9a09g057  driver-compat=13  not-in-any-DT=1  renesas,r9a09g057-du
imx95      driver-compat=22  not-in-any-DT=5  nxp,imx95-camera-csr nxp,imx95-display-csr nxp,imx95-display
stm32mp25  driver-compat=31  not-in-any-DT=2  st,stm32mp25-fmc2-ebi st,stm32mp25-fmc2-nfc
rk3588     driver-compat=29  not-in-any-DT=0  
tegra234   driver-compat=37  not-in-any-DT=0  
bcm2712    driver-compat=15  not-in-any-DT=0  
am62a      driver-compat=4   not-in-any-DT=0  
```

출력에서 볼 것: i.MX95는 카메라 · 디스플레이 CSR 등 5개 compatible이 드라이버에만 있다 — 다음 커널 버전에서 DT가 따라올 후보들이다. upstream은 보통 **바인딩 → 드라이버 → DT** 순서로 머지되어 이런 시차가 생긴다(P7). 반대로 RK3588 ISP처럼 드라이버 compatible 자체가 없는 것은 이 스크립트에 안 잡힌다(0으로 나오는 이유).

### 4.6 이 매트릭스의 한계 — 면접에서 정직하게 말하기

- 레퍼런스 보드 하나의 DT만 봤다. 다른 보드 DT에서 켜진 블록(예: i.MX8MP의 CM7 remoteproc 노드)은 안 잡힌다 — 드라이버는 `fsl,imx8mp-cm7`을 알지만 imx8mp-evk.dts에는 노드가 없다.
- "드라이버가 매칭된다" ≠ "기능이 완전하다". rkisp1이 i.MX8MP ISP를 잡아도 3A 튜닝은 libcamera 쪽 일이다.
- 정규식 분류의 오탐 · 미탐: `brcm,bcm2712-vc6`(디스플레이)를 GPU로 셌다. v3d(3D GPU)는 드라이버만 있고 노드가 없다.
- 그래도 이 방법의 가치는 **재현 가능하다는 것**이다. 새 커널 버전이 나오면 같은 스크립트를 돌려 차이를 본다. 면접에서 "저는 SoC를 고를 때 커널 트리에서 DT 노드와 드라이버 매칭을 스크립트로 확인합니다"라고 말할 수 있다.

---

## 5. 처음 보는 보드에서 30초 만에 SoC 알아내기

### 5.1 예제 9 — `/proc/device-tree`, MIDR, `soc0`

새 보드에 콘솔이 붙으면 Don이 제일 먼저 칠 명령들이다. 공용 랩의 QEMU virt로 연습한다(`-cpu`를 바꿔 가며 코어 식별이 어떻게 달라지는지 본다).

```sh
#!/bin/sh
# o5_id.sh: "what SoC am I on?" - first 30 s on an unknown board
echo "model      : $(tr -d '\0' < /proc/device-tree/model 2>/dev/null)"
echo "compatible : $(tr '\0' ' ' < /proc/device-tree/compatible 2>/dev/null)"
echo "cpu nodes  : $(for c in /proc/device-tree/cpus/cpu@*; do tr -d '\0' < $c/compatible; echo; done | sort | uniq -c | tr -s ' \n' ' ')"
echo "CPU part   : $(grep 'CPU part' /proc/cpuinfo | awk '{print $NF}' | sort | uniq -c | tr -s ' \n' ' ')"
for f in family machine soc_id revision; do
  [ -r /sys/devices/soc0/$f ] && echo "soc0/$f : $(cat /sys/devices/soc0/$f)"
done
[ -d /sys/devices/soc0 ] || echo "soc0       : (no soc bus device - virt has no SoC ID driver)"
echo "remoteproc : $(ls /sys/class/remoteproc 2>/dev/null | wc -l) instance(s)"
echo "accel      : $(ls /sys/class/accel 2>/dev/null | wc -l) device(s)"
echo "media/video: $(ls /dev/media* /dev/video* 2>/dev/null | wc -l) node(s); can: $(ls -d /sys/class/net/can* 2>/dev/null | wc -l)"
echo "platform drivers bound: $(ls -d /sys/bus/platform/drivers/*/*.* 2>/dev/null | cut -d/ -f6 | sort -u | tr '\n' ' ')"
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/O5 && mkdir r && cp -a /var/lab/build/rootfs/. r/ \
  && cp ~/.elinux_scratch/O5/o5_id.sh r/ && (cd r && find . | cpio -o -H newc --quiet | gzip) > o5_i.gz \
  && for cpu in cortex-a57 cortex-a76 cortex-a55; do echo "### -cpu $cpu"; \
     timeout 120 qemu-system-aarch64 -machine virt -cpu $cpu -smp 2 -m 512M -nographic \
       -kernel /var/lab/out/Image -initrd o5_i.gz -append "console=ttyAMA0 quiet LAB_CMD=/o5_id.sh"; done'
```

```text
### -cpu cortex-a57
lab initramfs: 6.18.55 up 7.71s
model      : linux,dummy-virt
compatible : linux,dummy-virt 
cpu nodes  :  2 arm,cortex-a57 
CPU part   :  2 0xd07 
soc0       : (no soc bus device - virt has no SoC ID driver)
remoteproc : 0 instance(s)
accel      : 0 device(s)
media/video: 0 node(s); can: 0
platform drivers bound: alarmtimer pci-host-generic physmap-flash 
### -cpu cortex-a76
lab initramfs: 6.18.55 up 16.49s
model      : linux,dummy-virt
compatible : linux,dummy-virt 
cpu nodes  :  2 arm,cortex-a76 
CPU part   :  2 0xd0b 
soc0       : (no soc bus device - virt has no SoC ID driver)
remoteproc : 0 instance(s)
accel      : 0 device(s)
media/video: 0 node(s); can: 0
platform drivers bound: alarmtimer pci-host-generic physmap-flash 
### -cpu cortex-a55
lab initramfs: 6.18.55 up 4.88s
model      : linux,dummy-virt
compatible : linux,dummy-virt 
cpu nodes  :  2 arm,cortex-a55 
CPU part   :  2 0xd05 
soc0       : (no soc bus device - virt has no SoC ID driver)
remoteproc : 0 instance(s)
accel      : 0 device(s)
media/video: 0 node(s); can: 0
platform drivers bound: alarmtimer pci-host-generic physmap-flash 
```

(`reboot: Power down` 줄과 커널 로그 줄은 생략. `up` 시간은 실행마다 다르고, 공유 VM 부하 아래 TCG 에뮬레이션이라 실제 하드웨어 숫자가 아니다.)

출력에서 볼 것:

- `CPU part`는 MIDR_EL1 레지스터의 part number 필드다: **0xd07 = Cortex-A57, 0xd0b = Cortex-A76, 0xd05 = Cortex-A55**. DT의 `cpus` compatible과 하드웨어 레지스터가 서로 맞는지 보는 것은 bring-up에서 "DT를 잘못 붙였나"를 가르는 첫 검사다. (참고로 A53 = 0xd03, A72 = 0xd08, A78 = 0xd41 — Arm TRM 기준, 기억 기반)
- QEMU virt는 `linux,dummy-virt`이고 `soc0`가 없다. 실제 i.MX8MP 보드라면 `soc0/soc_id`가 `i.MX8MP`, `revision`이 실리콘 리비전으로 나온다(예제 10의 soc-imx8m.c가 만든다).
- `remoteproc`, `accel`, media/video, CAN이 전부 0 — 이 보드엔 그런 IP가 없다. 실제 SoC에서 이 숫자들이 그 칩의 "이종 코어 지도"가 된다(O1~O4).

### 5.2 예제 11 — 드라이버를 로드해도 DT 노드가 없으면 아무 일도 없다

랩 커널 Image-dom에는 RK3588 NPU(rocket), M_CAN, TI R5F remoteproc 드라이버가 모듈로 빌드되어 있다. QEMU virt에 올려 본다.

```sh
#!/bin/sh
# o5_mod.sh: load SoC IP drivers on a board that does not have that IP (QEMU virt)
uname -r
for m in rocket m_can_platform ti_k3_r5_remoteproc; do
  modprobe $m && echo "modprobe $m: ok"
done
lsmod | grep -E '^(rocket|m_can|ti_k3|gpu_sched|drm_shmem)' | cut -c1-48
for d in rocket m_can_platform k3_r5_rproc; do
  [ -d /sys/bus/platform/drivers/$d ] && \
    echo "driver $d bound devices: $(ls /sys/bus/platform/drivers/$d | grep -c '\.')"
done
echo "/sys/class/accel: $(ls /sys/class/accel 2>/dev/null | wc -l) device(s)"
echo "/sys/class/remoteproc: $(ls /sys/class/remoteproc 2>/dev/null | wc -l)"
dmesg | grep -i -E 'rocket|accel' | tail -3
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/O5 && mkdir r2 && cp -a /var/lab/build/rootfs/. r2/ \
  && mkdir -p r2/lib/modules && cp -a /var/lab/out/modules-dom/lib/modules/6.18.55-dom r2/lib/modules/ \
  && cp ~/.elinux_scratch/O5/o5_mod.sh r2/ && (cd r2 && find . | cpio -o -H newc --quiet | gzip -1) > o5_m.gz \
  && timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1024M -nographic \
     -kernel /var/lab/out/Image-dom -initrd o5_m.gz -append "console=ttyAMA0 quiet LAB_CMD=/o5_mod.sh"'
```

```text
lab initramfs: 6.18.55-dom up 21.86s
6.18.55-dom
modprobe rocket: ok
modprobe m_can_platform: ok
modprobe ti_k3_r5_remoteproc: ok
ti_k3_r5_remoteproc 32768 0 - Live 0xffffbfc86a7
ti_k3_common 20480 1 ti_k3_r5_remoteproc, Live 0
m_can_platform 12288 0 - Live 0xffffbfc86a72d000
m_can 45056 1 m_can_platform, Live 0xffffbfc86a7
rocket 32768 0 - Live 0xffffbfc86a701000
drm_shmem_helper 32768 1 rocket, Live 0xffffbfc8
gpu_sched 57344 1 rocket, Live 0xffffbfc86a6ba00
driver rocket bound devices: 0
driver m_can_platform bound devices: 0
driver k3_r5_rproc bound devices: 0
/sys/class/accel: 0 device(s)
/sys/class/remoteproc: 0
```

(주소 값은 실행마다 다르고, `lsmod` 줄은 스크립트가 48자에서 잘랐다.)

출력에서 볼 것: 세 모듈 모두 **로드는 성공**하고 의존 모듈(gpu_sched, drm_shmem_helper, ti_k3_common)도 따라 올라왔지만, **바인딩된 장치는 0개**다. 드라이버(`platform_driver`)는 DT가 만든 `platform_device`가 와야 probe된다(G2). 모듈의 alias도 확인할 수 있다:

```sh
limactl shell elinux -- modinfo -F alias \
  /var/lab/out/modules-dom/lib/modules/6.18.55-dom/kernel/drivers/accel/rocket/rocket.ko
```

```text
of:N*T*Crockchip,rk3588-rknn-coreC*
of:N*T*Crockchip,rk3588-rknn-core
```

RK3588 보드에서는 DT의 `npu@...` 노드가 platform_device가 되면서 uevent `MODALIAS=of:NnpuT(null)Crockchip,rk3588-rknn-core...`가 나가고, udev가 `modules.alias`에서 이 줄을 찾아 `rocket`을 자동 로드한다. 이것이 "SoC를 바꾸면 DT만 바꾸면 된다"의 실제 메커니즘이다.

---

## 6. 커널 안에서는 무슨 일이 일어나나

SoC 지형을 커널 관점에서 다시 보면, 모든 벤더 차이는 결국 **"DT 노드 하나 → compatible 매칭 → 서브시스템 드라이버 → 유저 인터페이스"** 라는 같은 파이프에 다른 부품을 끼운 것이다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="150" y="10" width="380" height="48" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="30" font-size="13" text-anchor="middle">board.dts: npu@fdab0000</text>
<text x="340" y="48" font-size="13" text-anchor="middle">compatible = rockchip,rk3588-rknn-core</text>
<line x1="340" y1="58" x2="340" y2="74" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="150" y="76" width="380" height="48" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="96" font-size="13" text-anchor="middle">of_platform_populate()</text>
<text x="340" y="114" font-size="13" text-anchor="middle">→ platform_device 생성</text>
<line x1="340" y1="124" x2="340" y2="140" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="150" y="142" width="380" height="48" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="162" font-size="13" text-anchor="middle">uevent MODALIAS=of:N..C..</text>
<text x="340" y="180" font-size="13" text-anchor="middle">udev → modules.alias → modprobe rocket</text>
<line x1="340" y1="190" x2="340" y2="206" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="150" y="208" width="380" height="48" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="228" font-size="13" text-anchor="middle">platform_match → of_match_table</text>
<text x="340" y="246" font-size="13" text-anchor="middle">rocket_probe()</text>
<line x1="340" y1="256" x2="340" y2="272" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="150" y="274" width="380" height="48" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="294" font-size="13" text-anchor="middle">DRM accel 등록</text>
<text x="340" y="312" font-size="13" text-anchor="middle">/dev/accel/accel0 (유저 런타임이 open)</text>
<text x="545" y="45" font-size="12">DT (D-모듈)</text>
<text x="545" y="111" font-size="12">G2 드라이버 모델</text>
<text x="545" y="177" font-size="12">예제 11 alias</text>
<text x="545" y="243" font-size="12">벤더별 IP 드라이버</text>
<text x="545" y="309" font-size="12">O4 · remoteproc ·</text>
<text x="545" y="325" font-size="12">V4L2 · CAN netdev</text>
</svg>
```

그림 13 — DT 노드에서 사용자 공간 장치 파일까지. 벤더가 달라도 이 경로는 같고, 바뀌는 건 맨 오른쪽 서브시스템뿐이다.

- **DT → platform_device**: 부팅 때 `of_platform_populate()`가 `simple-bus` 아래 노드마다 `platform_device`를 만든다(D1, G2). RP1처럼 PCIe 장치 아래에 DT 서브트리를 붙이면 PCI 드라이버가 probe에서 `of_platform_populate()`를 다시 부른다.
- **매칭**: `platform_match()` → `of_driver_match_device()` → compatible 목록 앞에서부터. fallback compatible이 IP 재사용(rkisp1, m_can, panfrost)의 열쇠다.
- **서브시스템별 등록**: 매칭 뒤 probe가 어느 서브시스템에 등록하느냐가 유저에게 보이는 모습을 정한다(아래 표).

| 블록 | probe 안의 등록 함수 | 유저에게 보이는 것 | 관련 노트 |
|---|---|---|---|
| NPU | `devm_drm_dev_alloc()` + `DRIVER_COMPUTE_ACCEL` + `drm_dev_register()` (rocket_device.c에서 확인) | `/dev/accel/accelN` | O4 |
| GPU | DRM render 드라이버 등록 | `/dev/dri/renderD128` (etnaviv, panthor, msm, pvr) | O4 |
| DSP · MCU | `rproc_alloc()` + `rproc_add()`, 펌웨어는 `request_firmware()` | `/sys/class/remoteproc/remoteprocN` | O2 |
| 카메라 | `v4l2_async` + media controller | `/dev/mediaN`, `/dev/videoN`, `/dev/v4l-subdevN` | I-모듈, Q2 |
| CAN | `alloc_candev()` + `register_candev()` | `can0` netdev | Q4 |

- **SoC 펌웨어 의존**: TI-SCI(TI), SCMI(i.MX95, STM32MP2 등), RPMh(Qualcomm), BPMP(Tegra) — 클럭 · 전원 · 리셋이 **펌웨어 프로토콜 뒤**에 있다. DT에서 `clocks = <&k3_clks 13 0>`처럼 보이는 숫자는 레지스터가 아니라 펌웨어가 정한 ID다. bring-up 때 "클럭이 안 켜진다"의 원인이 커널이 아니라 펌웨어 버전일 수 있다는 뜻이다(P1).

드라이버를 쓸 때 다시 만나는 지점: 새 센서 보드를 붙일 때 Don이 쓸 DT(P4)는 이 그림의 맨 왼쪽이고, 드라이버의 `of_device_id`(G2, P3)는 두 번째 칸이다.

---

## 7. Embedded 실무에서 다시 보기 — 로봇 센서 박스용 SoC 고르기

### 7.1 장면: "카메라 4대 + IMU + GNSS + LiDAR + CAN 모터 2개, 엣지 추론"

예를 들어 Scout 같은 국방 로보틱스 회사의 센서 박스를 생각하자. 요구사항을 기준으로 바꾸면:

| 기준 | 질문 | 측정 · 확인 방법 |
|---|---|---|
| AI 성능 | 모델(예: YOLO 계열 detection) 몇 FPS · 몇 카메라? | 벤더 모델 zoo 벤치마크 + 직접 측정. TOPS 숫자 비교는 무의미할 때가 많다 |
| 카메라 | CSI-2 몇 개 · lane 수 · GMSL/FPD-Link SerDes 필요? ISP 튜닝 가능? | 4대면 CSI-2 4 lane x 2 + SerDes(Q1). ISP 튜닝 툴 제공 여부 |
| 실시간 · CAN | 모터 루프 · 안전 정지를 SoC 안 R5F/M7로 할지 외부 MCU로 할지 | 내장 CAN-FD 수, 보조 코어 유무(N4, O3) |
| 시간 동기 | PTP 하드웨어 타임스탬프 · PPS 입력 | Ethernet MAC의 PTP 지원, `ethtool -T`(Q3) |
| 커널 · 보안 | 몇 년 동안 어떤 커널로 CVE 패치? | 4절 매트릭스, 벤더 BSP 커널 버전 · 업데이트 주기 |
| 공급 · 등급 | 10년+ 공급? 확장 온도? 국가 원산지 규정? | 벤더 longevity 프로그램, 국방은 원산지 정책 |
| 전력 · 열 | 박스 열 설계 몇 W? | 모듈 전력 모드(예: Orin 15/25/40W 등 — 기억 기반) |
| 개발 속도 | 팀이 이미 아는 스택? ROS 2 지원? | Isaac ROS(NVIDIA), QIRP(Qualcomm), 커뮤니티 BSP |

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="10" width="330" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="175" y="35" font-size="12" text-anchor="middle">AI가 제품 핵심? (다중 카메라 대형 모델)</text>
<line x1="340" y1="30" x2="418" y2="30" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="352" y="24" font-size="12">예</text>
<rect x="420" y="10" width="250" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="545" y="35" font-size="12" text-anchor="middle">Jetson Orin + 외부 MCU</text>
<line x1="175" y1="50" x2="175" y2="68" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="183" y="64" font-size="12">아니오</text>
<rect x="10" y="70" width="330" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="175" y="95" font-size="12" text-anchor="middle">실시간 · CAN · 안전 · 차량 등급이 핵심?</text>
<line x1="340" y1="90" x2="418" y2="90" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="352" y="84" font-size="12">예</text>
<rect x="420" y="70" width="250" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="545" y="95" font-size="12" text-anchor="middle">TI AM62A/TDA4 · i.MX95</text>
<line x1="175" y1="110" x2="175" y2="128" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="183" y="124" font-size="12">아니오</text>
<rect x="10" y="130" width="330" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="175" y="155" font-size="12" text-anchor="middle">10년 공급 · mainline 커널 유지가 핵심?</text>
<line x1="340" y1="150" x2="418" y2="150" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="352" y="144" font-size="12">예</text>
<rect x="420" y="130" width="250" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="545" y="155" font-size="12" text-anchor="middle">i.MX8MP · AM62x · RZ/G2L</text>
<line x1="175" y1="170" x2="175" y2="188" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="183" y="184" font-size="12">아니오</text>
<rect x="10" y="190" width="330" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="175" y="215" font-size="12" text-anchor="middle">연결성(5G/Wi-Fi) · 와트당 AI?</text>
<line x1="340" y1="210" x2="418" y2="210" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="352" y="204" font-size="12">예</text>
<rect x="420" y="190" width="250" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="545" y="215" font-size="12" text-anchor="middle">Qualcomm QCS6490</text>
<line x1="175" y1="230" x2="175" y2="248" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="183" y="244" font-size="12">아니오</text>
<rect x="10" y="250" width="330" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="175" y="275" font-size="12" text-anchor="middle">빠른 프로토타입 · 저비용?</text>
<line x1="340" y1="270" x2="418" y2="270" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="352" y="264" font-size="12">예</text>
<rect x="420" y="250" width="250" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="545" y="275" font-size="12" text-anchor="middle">RK3588 SBC · RPi5/CM5</text>
</svg>
```

그림 14 — 센서 박스 SoC 결정 흐름(경험칙). 화살표 아래 칩 이름은 대표 예시일 뿐 정답이 아니다.

### 7.2 같은 요구사항, 세 가지 답

1. **AI가 제품의 핵심(다중 카메라 실시간 인식)**: Jetson Orin NX/AGX + 외부 STM32(CAN · 모터 · watchdog). 커널은 L4T에 묶이는 것을 받아들이고, OTA(L4)와 보안 패치는 NVIDIA 릴리스 주기에 맞춘다.
2. **AI는 보조, 실시간 · 장기 공급이 핵심(차량 · 산업)**: TI AM62A 또는 TDA4/AM68 — C7x로 추론, R5F에서 CAN · 안전 로직, mainline 친화 커널. 또는 i.MX95(6x A55 + M7 + CAN-FD 5).
3. **빠른 프로토타입 · 소량**: RK3588 SBC(가성비) 또는 RPi5/CM5 + Hailo. 양산 전에 1 또는 2로 갈아탈 계획을 세운다.

### 7.3 실무 함정: 같은 SoC라도 "어느 커널"이 진짜 질문이다

같은 RK3588 보드라도 벤더 6.1 커널이면 카메라 · NPU(rknpu) · 하드웨어 코덱이 다 되고, mainline 6.18이면 NPU(rocket) · GPU(panthor)는 되지만 카메라는 안 된다. 그래서 SoC 선정 문서에는 반드시 **"BSP 커널 버전 · mainline 상태 · 우리가 필요한 블록별 지원 여부"** 표가 들어가야 하고, 4절 스크립트가 그 표의 근거가 된다.

---

## 8. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| TOPS 숫자로 SoC 비교 | 실제 모델 FPS가 기대의 1/3 | INT8 · 희소 연산 기준 피크, 메모리 대역폭 · 연산자 지원 무시 | 목표 모델로 벤더 툴체인 벤치마크 |
| mainline에 DT 있으니 다 된다고 가정 | 카메라 · NPU가 안 뜸 | DT에 노드가 없거나(RK3588 ISP) 드라이버만 있음 | 4절 매트릭스를 블록 단위로 확인 |
| defconfig에 없으니 미지원이라 판단 | 잘못된 SoC 탈락 | savedefconfig는 기본값 줄을 지움 | 완성된 `.config`나 Kconfig `default` 확인 |
| 마케팅 이름으로 DT grep | "QCS6490 지원 없음" 오판 | DT 이름이 qcm6490/sc7280, sm8250 | top-level compatible과 include 체인 확인 |
| 벤더 BSP 커널 수명 미확인 | 2년 뒤 CVE 패치 불가, 커널 업그레이드 대공사 | BSP가 오래된 LTS에 고정 | 선정 단계에서 BSP 로드맵 · CIP/LTS 정책 질문 |
| 보조 코어 펌웨어 · 서명 blob 무시 | DSP/R5F가 안 뜸, 양산 서명 키 문제 | PAS · 보안 부트가 서명 펌웨어 요구 | 펌웨어 배포 · 서명 경로를 BSP 계획에 포함(L2) |
| SoC 내장 CAN이 있다고 가정 | Qualcomm · RPi에서 CAN 없음 | 모바일 계열 SoC엔 CAN 컨트롤러가 없음 | 외부 MCP251xFD(SPI) 또는 MCU 게이트웨이(O3, Q4) |
| DT의 cpu 노드 수 = 실물 코어 수라고 가정 | Orin Nano에서 혼란 | mainline DT가 SoC 최대 구성을 기술 | `/proc/cpuinfo`, `soc0`, 부트 로그로 확인 |
| compatible grep 결과를 그대로 믿음 | 엉뚱한 드라이버(SMMU quirk 표)를 "지원"으로 셈 | 같은 문자열이 quirk 표에도 있음 | `of_match_table`로 등록된 표인지 확인 |

---

## 9. 면접에서 이렇게 말한다

**Q.** "Which SoC platforms have you worked with or evaluated, and how would you compare them?"

**A.** 직접 양산한 Linux SoC는 없지만 SSD 컨트롤러와 RF 칩셋 bring-up 경험을 바탕으로, 성능과 mainline 지원 두 축으로 비교한다고 답한다. 근거는 커널 트리 매트릭스.

> I compare SoCs on two axes: compute — CPU cluster, NPU or DSP, camera pipeline — and how well mainline Linux supports each block. I actually scripted that: I compile the reference board device trees from the kernel tree and check which IP nodes have an in-tree driver. For example, i.MX8M Plus has GPU, NPU, ISP, CSI and CAN all supported in mainline, while Jetson Orin's mainline device tree has basically just the CPUs and mailbox, so in practice you live on NVIDIA's L4T kernel.

**Q.** "Why would you pick Jetson Orin over TI TDA4 (or vice versa) for a robot?"

**A.** Orin = CUDA/TensorRT 생태계와 최고 AI 성능, 대신 커널이 L4T에 묶이고 전력이 크다. TDA4/AM68 = R5F가 많아 실시간 · CAN · 안전을 칩 안에서 처리, 자동차 등급 · 장기 공급, mainline 친화. AI 모델 크기와 안전 요구가 결정한다.

> If perception is the product — many cameras and large models — Orin wins because of the CUDA and TensorRT ecosystem; you accept being tied to NVIDIA's kernel and add an external MCU for CAN and safety. If deterministic control and functional safety matter more, TDA4 or AM68 gives you six R5F cores and many CAN-FD ports on the same die, automotive grade, and a kernel much closer to mainline. I'd decide with a benchmark of our actual model, not TOPS numbers.

**꼬리 질문**: "What's the downside of a vendor BSP kernel?" → 커널 버전 고정, CVE 백포트를 벤더에 의존, 업그레이드 시 패치 스택 리베이스 비용(P5).

**Q.** "What does 'mainline support' actually mean for a product?"

**A.** 우리 보드에 필요한 모든 블록(카메라, NPU, CAN, 보조 코어)의 DT 바인딩 · 드라이버가 kernel.org 트리에 있어서, 아무 LTS로 올려도 동작한다는 뜻. 장점은 보안 패치 · 커널 업그레이드가 쉬움, 단점은 벤더 최신 기능(ISP 튜닝, NPU 런타임)은 늦게 들어옴.

> It means every block our board needs has a DT binding and a driver in the kernel.org tree, so we can move to any LTS kernel and take security fixes without a vendor. It doesn't mean feature-complete — ISP tuning or the NPU compiler often still comes from the vendor. So I check per block, not per SoC.

**Q.** "How do you identify an unknown board quickly?"

**A.** `/proc/device-tree/model`과 `compatible`, `cpus` 노드, `/proc/cpuinfo`의 CPU part(MIDR), `/sys/devices/soc0`, 그리고 `/sys/class/remoteproc`, `/dev/accel`, `/dev/media*`, `can*`.

> I read /proc/device-tree/model and compatible, compare the cpus node compatibles with the MIDR part numbers in /proc/cpuinfo, check /sys/devices/soc0 for the silicon revision, and then list remoteproc, accel, media and CAN devices to map the heterogeneous parts.

**Q.** "Tell me about the AI accelerator software stack on these SoCs."

**A.** 세 모델: DRM accel 드라이버(RK3588 rocket, Intel NPU), GPU 드라이버가 겸함(i.MX8MP etnaviv), DSP를 remoteproc로 띄우고 RPC(TI C7x + TIDL, Qualcomm Hexagon + fastrpc). 6.18 기준 SoC 내장 NPU의 accel 드라이버는 rocket뿐.

> There are three patterns. A native DRM accel driver like Rockchip's rocket, which landed recently. A GPU driver that also drives the NPU, like etnaviv on i.MX8M Plus. Or the accelerator is a DSP that Linux boots through remoteproc and talks to by RPC — TI's C7x with TIDL, Qualcomm's Hexagon with fastrpc. In 6.18 rocket is the only DT-probed NPU driver under drivers/accel; everything else there is PCIe.

**Q.** "What questions would you ask before choosing an SoC for our next sensor box?"

**A.** 7.1절 기준표를 말한다: 목표 모델 · 카메라 수, 실시간 · CAN, 시간 동기, 커널 · 보안 수명, 공급 · 원산지, 전력.

> First the workload: which models, how many cameras, what latency. Then the control side: do we need CAN-FD and real-time cores on-die or an external MCU. Time sync — hardware PTP timestamps and PPS. Then lifecycle: which kernel the BSP is on, how we take CVE fixes for ten years, supply longevity and any country-of-origin rules. Power and thermal last, but they often kill the top option.

### 9.1 플랫폼별 30초 피치

- **i.MX8M Plus**: "4x A53 + M7, 2.3 TOPS급 NPU, ISP 2개, CAN-FD 2개. mainline 지원이 가장 좋은 축이라 산업용 카메라 노드의 안전한 선택."
- **i.MX93/95**: "93은 저전력 2x A55 + M33 + 작은 Ethos NPU, 95는 6x A55 + M7 + 안전용 M33 + CAN-FD 5개로 차량 · 로봇 게이트웨이를 노린다. 95는 mainline이 아직 따라오는 중."
- **TI AM62x/AM62A/AM64**: "같은 K3 구조. AM62x는 HMI · 게이트웨이, AM62A는 C7x 비전 AI 카메라, AM64는 PRU로 산업 이더넷. R5F로 실시간 분리가 쉽고 커널은 upstream 친화."
- **TI TDA4/AM68/AM69**: "ADAS 출신 칩. R5F 6~8개, C7x DSP로 8~32 TOPS, CAN 20개급. 자동차 등급 AMR · 차량용."
- **RK3568/RK3588**: "가성비 최강, RK3588은 A76 4 + A55 4 + 6 TOPS NPU. mainline은 NPU(rocket) · GPU(panthor)까지 왔지만 카메라는 벤더 커널. 국방 · 정부 납품은 원산지 이슈 확인."
- **Qualcomm QCS6490/QRB5165**: "휴대폰 SoC 기반, 와트당 AI와 연결성이 강하고 커널 upstream도 좋다. Hexagon은 서명 펌웨어 + fastrpc, CAN 없음."
- **Jetson Orin**: "로보틱스 AI의 기준점. A78AE 최대 12코어, Ampere GPU + DLA, CUDA/TensorRT/Isaac ROS. 대가는 L4T 커널 고정과 전력."
- **Raspberry Pi 5**: "4x A76 + RP1 I/O 칩. libcamera가 가장 성숙, 프로토타입 기본값. 제품은 CM5 + 외부 MCU · CAN."
- **STM32MP1/MP2**: "MCU 팀의 첫 Linux. MP1은 A7 2 + M4, mainline 완성도 높음. MP2는 A35 2 + M33 + 작은 NPU."
- **Renesas RZ/G2L/V2H**: "G2L은 CIP 커널 10년 유지 스토리, mainline 좋음. V2H는 DRP-AI + R8 실시간 코어로 로봇 컨트롤러 통합."

### 9.2 면접관에게 던질 질문 (그 회사 스택 파악용)

- "What SoC and BSP are you on today — vendor kernel or close to mainline? Which kernel version, and how do you pick up security fixes?"
- "How is the work split between the Linux SoC and the MCUs or real-time cores? Who owns the protocol between them?"
- "How many cameras, and what's the path — CSI direct or GMSL/FPD-Link SerDes? Who tunes the ISP?"
- "Where does inference run, and what's the toolchain — TensorRT, TIDL, RKNN, QNN? How do you validate model updates on the device?"
- "How do you do time synchronization across sensors — PTP, PPS, hardware triggers?"
- "Are you evaluating a next-generation SoC? What are the criteria — compute, supply longevity, country-of-origin, power?"
- "How much do you upstream, or carry patches on top of the vendor tree? How do you handle kernel upgrades?"

---

## 10. 직접 해보기

1. **개념**: i.MX8M Plus의 ISP를 rkisp1 드라이버가 맡는 이유를 한 문단으로 설명하라. 정답/힌트: 같은 계열의 ISP IP를 쓰기 때문이고(IP 출처는 기억 기반 — 확인 필요), DT에 `fsl,imx8mp-isp`가 있지만 드라이버는 `rkisp1-dev.c`의 match 표에 그 compatible을 추가해 재사용한다 — `grep -n imx8mp drivers/media/platform/rockchip/rkisp1/rkisp1-dev.c`.
2. **코드**: 예제 3의 `dtinv.py`에 "PCIe 컨트롤러 개수"와 "Ethernet MAC 개수(+ PTP 지원 여부를 알 수 있는 compatible)" 분류를 추가해 RK3588과 i.MX8MP를 비교하라. 정답/힌트: `pcie`, `ethernet@` 노드 이름과 `snps,dwmac`, `fsl,imx8mp-fec` 같은 compatible.
3. **코드**: 예제 2의 보드 수를 정확히 세려면 `#include` 그래프를 따라가야 한다. `rk3588.dtsi`를 (직접 또는 간접) include하는 .dts 수를 세는 스크립트를 짜라. 정답/힌트: 각 파일의 `#include "x.dtsi"`를 파싱해 역방향 그래프를 만들고 BFS. 또는 예제 3처럼 전부 cpp 전처리 후 top compatible을 본다.
4. **관찰**: 랩 Image-dom 게스트에서 `modprobe ti_k3_dsp_remoteproc` 후 `ls /sys/bus/platform/drivers/`에서 드라이버 이름을 찾고, 왜 장치가 0개인지 설명하라. 정답/힌트: virt DT에 `ti,am62a-c7xv-dsp` 노드가 없다 — 예제 11과 같은 이유.
5. **조사**: 6.18 트리에서 `drivers/accel/rocket/rocket_drv.c`의 `rocket_probe`부터 따라가 `/dev/accel/accel0`가 어디서 만들어지는지 찾아라. 정답/힌트: `rocket_device.c`의 `devm_drm_dev_alloc()` → `rocket_drv.c`의 `.driver_features = DRIVER_COMPUTE_ACCEL` → `drm_dev_register()`가 accel 마이너를 만든다(O4).
6. **설계**: "카메라 6대 + CAN-FD 4채널 + 30 TOPS 이상 + 10년 공급" 요구에 맞는 SoC 후보 2개와 각 플랫폼의 위험 요소를 표로 써라. 정답/힌트: AM69(CAN · 공급 강함, AI 툴체인 TIDL 학습 비용) vs AGX Orin(AI 강함, CAN은 MTTCAN이지만 커널 L4T, 전력) — 숫자는 데이터시트로 검증.
7. **관찰**: QEMU에서 `-cpu cortex-a53`과 `-cpu max`로 예제 9를 돌려 `CPU part`가 어떻게 바뀌는지 보고 MIDR의 implementer/part 필드를 설명하라. 정답/힌트: `CPU implementer 0x41`(Arm), part 0xd03(A53); `max`는 QEMU가 정한 값.

---

## 11. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| SoC | System on Chip | CPU · GPU · NPU · I/O를 한 다이에 넣은 칩 |
| SoM | System on Module | SoC + DDR + PMIC + eMMC를 작은 보드에 올린 모듈(Verdin, CM5 등) |
| BSP | Board Support Package | 벤더가 주는 부트로더 · 커널 · 드라이버 · 툴 묶음(P2) |
| mainline | kernel.org 본가 트리 | 벤더 트리와 대비되는 공식 커널 |
| vendor tree | 벤더 커널 트리 | 벤더가 특정 LTS에 자기 패치를 쌓은 커널 |
| fallback compatible | 일반 compatible | DT compatible 목록 뒤쪽의 일반 이름, IP 재사용 드라이버 매칭에 쓰임 |
| NPU | Neural Processing Unit | 행렬 · 컨볼루션 전용 가속기 |
| DRM accel | `drivers/accel` | NPU 같은 연산 가속기용 DRM 하위 클래스, `/dev/accel/accelN` |
| remoteproc | 원격 프로세서 프레임워크 | 보조 코어(M/R/DSP)에 펌웨어를 올리고 켜고 끄는 커널 프레임워크(O2) |
| PAS | Peripheral Authentication Service | Qualcomm TrustZone이 DSP 펌웨어 서명을 검증해 올리는 서비스 |
| TI-SCI · SCMI · BPMP · RPMh | SoC 관리 펌웨어 프로토콜 | 클럭 · 전원 · 리셋을 펌웨어에 요청하는 메시지 규약 |
| ISP | Image Signal Processor | RAW 센서 데이터를 디모자이크 · 노이즈 제거 · 3A로 영상으로 만드는 블록 |
| CSI-2 | MIPI Camera Serial Interface 2 | 카메라 센서 직렬 인터페이스(Q1) |
| M_CAN · FlexCAN · MTTCAN | CAN 컨트롤러 IP | Bosch M_CAN(TI · ST), NXP FlexCAN, Bosch M_TTCAN(NVIDIA) |
| MIDR | Main ID Register | 코어 종류(implementer, part)를 알려 주는 Arm 레지스터 |
| CIP | Civil Infrastructure Platform | 10년+ 유지되는 SLTS 커널을 관리하는 Linux Foundation 프로젝트 |
| L4T | Linux for Tegra | NVIDIA Jetson Linux BSP의 옛 이름 |
| longevity | 공급 수명 프로그램 | 벤더가 칩을 몇 년간 공급하겠다는 약속 |

---

## 12. 요약 & 체크리스트

SoC는 "성능(CPU · AI · 카메라)"과 "mainline 친화도" 두 축으로 본다. 성능 숫자는 벤더 자료(기억 기반, 데이터시트로 확인)지만, mainline 친화도는 커널 트리에서 직접 측정할 수 있다 — 레퍼런스 보드 DT를 cpp + dtc로 flatten하고, 각 IP 노드의 compatible 목록 전체를 `drivers/`의 `of_device_id` 표와 매칭하면 블록별 지원 그리드가 나온다. 6.18.55 기준 i.MX8M Plus · AM62x가 가장 mainline 친화적이고, RK3588은 NPU · GPU까지만, Jetson Orin은 거의 벤더 BSP다. 커널에서 보면 모든 벤더 차이는 "DT 노드 → compatible 매칭 → 서브시스템 드라이버 → /dev 노드"라는 같은 파이프에 다른 IP 드라이버(rkisp1, m_can, panfrost, rocket, remoteproc)를 끼운 것이고, SoC 벤더는 IP를 사 오기 때문에 드라이버가 벤더를 넘어 재사용된다. 센서 박스 SoC 선정은 TOPS가 아니라 목표 모델 벤치마크 · 카메라 경로 · 실시간/CAN 위치 · 시간 동기 · 커널 수명 · 공급으로 한다.

- [ ] 8개 패밀리 각각의 Linux 코어 · 실시간 코어 · AI 가속기 · CAN · BSP 이름을 말할 수 있다
- [ ] 마케팅 이름과 DT compatible이 다른 사례(QCS6490 = qcm6490/sc7280, QRB5165 = sm8250, Orin = tegra234)를 설명할 수 있다
- [ ] 보드 DT를 cpp + dtc로 flatten하는 스크립트를 짤 수 있다
- [ ] fallback compatible을 고려해 DT 노드 → 드라이버 매칭을 찾는 스크립트를 짤 수 있다
- [ ] IP 재사용 사례(rkisp1 · m_can · panfrost · cdns-csi2rx · rcar_canfd)를 3개 이상 말할 수 있다
- [ ] AI 가속기의 세 가지 커널 모델(DRM accel, GPU 겸용, DSP + remoteproc/RPC)을 예와 함께 설명할 수 있다
- [ ] defconfig의 "없음"과 `.config`의 "꺼짐"의 차이를 설명할 수 있다
- [ ] 처음 보는 보드에서 DT · MIDR · soc0 · remoteproc · accel로 SoC를 식별할 수 있다
- [ ] 드라이버를 로드해도 DT 노드가 없으면 probe되지 않는 이유를 설명할 수 있다
- [ ] 로봇 센서 박스 SoC 선정 기준 7~8개를 우선순위와 함께 말할 수 있다
- [ ] 10개 플랫폼 각각을 30초 안에 영어로 소개할 수 있다
- [ ] 면접관에게 그 회사 SoC 스택에 대한 질문 5개를 던질 수 있다

## 참고 자료

- Linux 6.18.55 소스: `arch/arm64/boot/dts/{freescale,ti,rockchip,qcom,nvidia,broadcom,st,renesas}`, `drivers/accel`, `drivers/remoteproc`, `drivers/media/platform`, `drivers/net/can`, `MAINTAINERS` — 온라인으로는 [elixir.bootlin.com](https://elixir.bootlin.com/linux/latest/source)
- [Compute Accelerators — docs.kernel.org](https://docs.kernel.org/accel/index.html)
- [Remote Processor Framework — docs.kernel.org](https://docs.kernel.org/staging/remoteproc.html)
- [Linux and the Devicetree — docs.kernel.org](https://docs.kernel.org/devicetree/usage-model.html)
- [Bootlin training materials (Embedded Linux, kernel drivers)](https://bootlin.com/docs/)
- 벤더 문서(이름으로 검색, 수치 확인용): NXP i.MX Linux User's Guide · TI Processor SDK Linux 문서 · Rockchip RKNN-Toolkit2 · Qualcomm Linux 문서 · NVIDIA Jetson Linux Developer Guide · Raspberry Pi documentation · STM32 MPU wiki · Renesas RZ/G VLP 문서
- [Civil Infrastructure Platform](https://www.cip-project.org/)
- 관련 노트: O1, O2, O3, O4, D1~D7, G2, K4, P2, P5, P7, Q1, Q2, Q4, N4
