# Embedded Linux — Study Contents (나침반)

> **작성일**: 2026-10-05 · **목표**: ① Embedded Linux / 커널 드라이버 인터뷰 통과 ② 입사 후 바로 bring-up·드라이버·디버깅을 해내는 실무자
> **용도**: 실무자가 알아야 할 개념을 **빠짐없이 순서대로 나열한 목차**. 각 항목을 하나씩 노트(쉬운 설명 · 그림 · 실제로 돌려 본 코드)로 채워 간다
> **근거**: Bootlin 공개 교육 과정 3종(Embedded Linux System Dev · Linux Kernel & Driver Dev · Debugging/Tracing) 아젠다, kernel.org driver-api 문서 구성, 표준 교재 목차, Scout AI·Verkada JD 요구사항
> **노트 사이트**: [전체 노트 목록](../index.html) — 빌드 `python3 build_site.py`

---

## 0. 이 문서 읽는 법

**표기**
- **ID**: 모듈 알파벳 + 번호 (예: `G3`). 나중에 노트 파일 이름으로 쓴다 → `G_driver_model/2026-10-xx_G3_char_device.md`
- **우선순위**: `P0` = 1차 기술 면접 전에 반드시 · `P1` = 온사이트·입사 전 · `P2` = 넓게 알아 두기 (실무에서 만나면 깊게)
- **Don 출발점**:
  - ✅ 이미 강함 (bare-metal FW · SSD · PCIe/I2C/SPI 디버그) → Linux 용어로 **번역**만 하면 됨
  - 🟡 인접 경험 있음 → 기존 지식에서 확장
  - ❌ 새로 배움 → 처음부터

**전체 그림 — Embedded Linux 시스템의 층**
```svg
<svg viewBox="0 0 680 484" xmlns="http://www.w3.org/2000/svg"><rect x="10" y="10" width="660" height="128" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-width="1.3"/><text x="22" y="30" font-size="13" text-anchor="start" font-weight="bold">User space</text><rect x="22" y="40" width="636" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="34" y="58" font-size="13" text-anchor="start" font-weight="normal">App · 서비스 (C/C++/Python, ROS 2, GStreamer)</text><text x="646" y="58" font-size="12" text-anchor="end" font-weight="bold">A, Q</text><rect x="22" y="71" width="636" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="34" y="89" font-size="13" text-anchor="start" font-weight="normal">systemd · udev · D-Bus · OTA 에이전트</text><text x="646" y="89" font-size="12" text-anchor="end" font-weight="bold">L</text><rect x="22" y="102" width="636" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="34" y="120" font-size="13" text-anchor="start" font-weight="normal">libc (glibc/musl) · 라이브러리 · rootfs (Buildroot/Yocto)</text><text x="646" y="120" font-size="12" text-anchor="end" font-weight="bold">B, K</text><rect x="10" y="146" width="660" height="28" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/><text x="340" y="165" font-size="13" text-anchor="middle" font-weight="normal">경계: syscall · /dev · /sys · /proc · ioctl · mmap</text><rect x="10" y="182" width="660" height="190" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.3"/><text x="22" y="202" font-size="13" text-anchor="start" font-weight="bold">Linux kernel</text><rect x="22" y="212" width="636" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="34" y="230" font-size="13" text-anchor="start" font-weight="normal">스케줄러 · 메모리 관리 · 락 · 인터럽트 · 시간</text><text x="646" y="230" font-size="12" text-anchor="end" font-weight="bold">F</text><rect x="22" y="243" width="636" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="34" y="261" font-size="13" text-anchor="start" font-weight="normal">드라이버 모델: bus · device · driver · class</text><text x="646" y="261" font-size="12" text-anchor="end" font-weight="bold">G</text><rect x="22" y="274" width="636" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="34" y="292" font-size="13" text-anchor="start" font-weight="normal">서브시스템: IIO · V4L2 · input · net · CAN · ALSA …</text><text x="646" y="292" font-size="12" text-anchor="end" font-weight="bold">I</text><rect x="22" y="305" width="636" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="34" y="323" font-size="13" text-anchor="start" font-weight="normal">HW 접근: MMIO · regmap · clk · GPIO · IRQ · DMA</text><text x="646" y="323" font-size="12" text-anchor="end" font-weight="bold">H</text><rect x="22" y="336" width="636" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="34" y="354" font-size="13" text-anchor="start" font-weight="normal">Device Tree: 이 보드에 무엇이 어디 붙어 있나</text><text x="646" y="354" font-size="12" text-anchor="end" font-weight="bold">D</text><rect x="10" y="380" width="660" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="22" y="400" font-size="13" text-anchor="start" font-weight="normal">Bootloader: BootROM → SPL → TF-A/OP-TEE → U-Boot</text><text x="658" y="400" font-size="12" text-anchor="end" font-weight="bold">C</text><rect x="10" y="418" width="660" height="30" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/><text x="22" y="438" font-size="13" text-anchor="start" font-weight="normal">SoC (Cortex-A + Cortex-M) · DDR · eMMC/NAND · 센서 · 카메라</text><text x="340" y="472" font-size="13" text-anchor="middle" font-weight="normal">모든 층을 가로지르는 것: 디버깅·트레이싱(M) · 실시간·전원(N) · bring-up 실무(P)</text></svg>
```

그림 1 — Embedded Linux 시스템의 층. 오른쪽 글자는 해당 모듈 ID

**공부 흐름 (큰 순서)**
```svg
<svg viewBox="0 0 680 242" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10" y="34" font-size="13" text-anchor="start" font-weight="bold">Phase 1 기반</text><rect x="128" y="14" width="96" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="176.0" y="34" font-size="12" text-anchor="middle" font-weight="normal">A 유저스페이스</text><line x1="225" y1="29" x2="233" y2="29" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="234" y="14" width="96" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="282.0" y="34" font-size="12" text-anchor="middle" font-weight="normal">B 툴체인</text><line x1="331" y1="29" x2="339" y2="29" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="340" y="14" width="96" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="388.0" y="34" font-size="12" text-anchor="middle" font-weight="normal">C 부트</text><line x1="437" y1="29" x2="445" y2="29" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="446" y="14" width="96" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="494.0" y="34" font-size="12" text-anchor="middle" font-weight="normal">D Device Tree</text><line x1="543" y1="29" x2="551" y2="29" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="552" y="14" width="96" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="600.0" y="34" font-size="12" text-anchor="middle" font-weight="normal">E 커널 기본</text><line x1="400" y1="46" x2="400" y2="58" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><text x="10" y="80" font-size="13" text-anchor="start" font-weight="bold">Phase 2 드라이버</text><rect x="128" y="60" width="124" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="190.0" y="80" font-size="12" text-anchor="middle" font-weight="normal">F 커널 메커니즘</text><line x1="253" y1="75" x2="261" y2="75" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="262" y="60" width="124" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="324.0" y="80" font-size="12" text-anchor="middle" font-weight="normal">G 드라이버 모델</text><line x1="387" y1="75" x2="395" y2="75" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="396" y="60" width="124" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="458.0" y="80" font-size="12" text-anchor="middle" font-weight="normal">H HW 접근·버스</text><line x1="521" y1="75" x2="529" y2="75" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="530" y="60" width="124" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="592.0" y="80" font-size="12" text-anchor="middle" font-weight="normal">I 서브시스템</text><line x1="400" y1="92" x2="400" y2="104" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><text x="10" y="126" font-size="13" text-anchor="start" font-weight="bold">Phase 3 시스템</text><rect x="128" y="106" width="124" height="30" rx="5" fill="#e08a3c" fill-opacity="0.14" stroke="#e08a3c" stroke-width="1.3"/><text x="190.0" y="126" font-size="12" text-anchor="middle" font-weight="normal">J 스토리지·FS</text><line x1="253" y1="121" x2="261" y2="121" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="262" y="106" width="124" height="30" rx="5" fill="#e08a3c" fill-opacity="0.14" stroke="#e08a3c" stroke-width="1.3"/><text x="324.0" y="126" font-size="12" text-anchor="middle" font-weight="normal">K rootfs·빌드</text><line x1="387" y1="121" x2="395" y2="121" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="396" y="106" width="124" height="30" rx="5" fill="#e08a3c" fill-opacity="0.14" stroke="#e08a3c" stroke-width="1.3"/><text x="458.0" y="126" font-size="12" text-anchor="middle" font-weight="normal">L 시스템·OTA·보안</text><line x1="400" y1="138" x2="400" y2="150" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><text x="10" y="172" font-size="13" text-anchor="start" font-weight="bold">Phase 4 실무력</text><rect x="128" y="152" width="124" height="30" rx="5" fill="#d0564a" fill-opacity="0.14" stroke="#d0564a" stroke-width="1.3"/><text x="190.0" y="172" font-size="12" text-anchor="middle" font-weight="normal">M 디버깅</text><line x1="253" y1="167" x2="261" y2="167" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="262" y="152" width="124" height="30" rx="5" fill="#d0564a" fill-opacity="0.14" stroke="#d0564a" stroke-width="1.3"/><text x="324.0" y="172" font-size="12" text-anchor="middle" font-weight="normal">N 실시간·전원</text><line x1="387" y1="167" x2="395" y2="167" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="396" y="152" width="124" height="30" rx="5" fill="#d0564a" fill-opacity="0.14" stroke="#d0564a" stroke-width="1.3"/><text x="458.0" y="172" font-size="12" text-anchor="middle" font-weight="normal">O 이종 코어</text><line x1="521" y1="167" x2="529" y2="167" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="530" y="152" width="124" height="30" rx="5" fill="#d0564a" fill-opacity="0.14" stroke="#d0564a" stroke-width="1.3"/><text x="592.0" y="172" font-size="12" text-anchor="middle" font-weight="normal">P bring-up</text><rect x="10" y="202" width="660" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="340" y="222" font-size="13" text-anchor="middle" font-weight="normal">병행 — Q 도메인(카메라·센서·CAN) · R 인터뷰 · S 실습 프로젝트</text></svg>
```

그림 2 — 공부 흐름. Phase 1부터 내려가고, Q·R·S는 전체 기간 동안 병행

---

## 1. 디바이스 드라이버를 쓰려면 무엇을 알아야 하나 (선행 지식 지도)

> 드라이버 = "데이터시트의 레지스터"와 "커널이 정해 둔 인터페이스" 사이의 번역기. 두 쪽을 다 알아야 한다. Don은 **왼쪽(HW)이 이미 강하고 오른쪽(커널)이 비어 있다**.

```svg
<svg viewBox="0 0 680 318" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="105" y="18" font-size="13" text-anchor="middle" font-weight="bold">HW 쪽 — Don ✅</text><text x="530" y="18" font-size="13" text-anchor="middle" font-weight="bold">커널 쪽 — 채울 곳</text><rect x="10" y="52" width="190" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="105" y="72" font-size="12" text-anchor="middle" font-weight="normal">데이터시트 · 레지스터 맵</text><line x1="201" y1="67" x2="268" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="10" y="92" width="190" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="105" y="112" font-size="12" text-anchor="middle" font-weight="normal">버스 프로토콜 I2C/SPI/PCIe</text><line x1="201" y1="107" x2="268" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="10" y="132" width="190" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="105" y="152" font-size="12" text-anchor="middle" font-weight="normal">인터럽트 · 타이밍</text><line x1="201" y1="147" x2="268" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="10" y="172" width="190" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="105" y="192" font-size="12" text-anchor="middle" font-weight="normal">DMA · 캐시 일관성</text><line x1="201" y1="187" x2="268" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="10" y="212" width="190" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="105" y="232" font-size="12" text-anchor="middle" font-weight="normal">전원 시퀀스 · 클럭 · 리셋</text><line x1="201" y1="227" x2="268" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="10" y="252" width="190" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="105" y="272" font-size="12" text-anchor="middle" font-weight="normal">scope · LA · JTAG 디버그</text><line x1="201" y1="267" x2="268" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="270" y="156.0" width="80" height="40" rx="8" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c" stroke-width="1.3"/><text x="310" y="181.0" font-size="14" text-anchor="middle" font-weight="bold">드라이버</text><rect x="390" y="40" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="59" font-size="12" text-anchor="start" font-weight="bold">D</text><text x="420" y="59" font-size="12" text-anchor="start" font-weight="normal">Device Tree: HW를 커널에 알리는 법</text><line x1="388" y1="54" x2="352" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="74" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="93" font-size="12" text-anchor="start" font-weight="bold">G</text><text x="420" y="93" font-size="12" text-anchor="start" font-weight="normal">드라이버 모델: probe()는 언제 불리나</text><line x1="388" y1="88" x2="352" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="108" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="127" font-size="12" text-anchor="start" font-weight="bold">F</text><text x="420" y="127" font-size="12" text-anchor="start" font-weight="normal">컨텍스트: 여기서 잠들어도 되나? 어떤 락?</text><line x1="388" y1="122" x2="352" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="142" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="161" font-size="12" text-anchor="start" font-weight="bold">F</text><text x="420" y="161" font-size="12" text-anchor="start" font-weight="normal">메모리: kmalloc · GFP 플래그 · DMA 버퍼</text><line x1="388" y1="156" x2="352" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="176" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="195" font-size="12" text-anchor="start" font-weight="bold">H</text><text x="420" y="195" font-size="12" text-anchor="start" font-weight="normal">clk · regulator · reset · pinctrl · GPIO API</text><line x1="388" y1="190" x2="352" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="210" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="229" font-size="12" text-anchor="start" font-weight="bold">I</text><text x="420" y="229" font-size="12" text-anchor="start" font-weight="normal">어느 서브시스템(IIO/V4L2/input)에 붙이나</text><line x1="388" y1="224" x2="352" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="244" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="263" font-size="12" text-anchor="start" font-weight="bold">G</text><text x="420" y="263" font-size="12" text-anchor="start" font-weight="normal">유저에 보이기: /dev · sysfs · ioctl</text><line x1="388" y1="258" x2="352" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="278" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="297" font-size="12" text-anchor="start" font-weight="bold">M</text><text x="420" y="297" font-size="12" text-anchor="start" font-weight="normal">printk · ftrace · oops 해독</text><line x1="388" y1="292" x2="352" y2="176.0" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/></svg>
```

그림 3 — 드라이버는 HW 지식(왼쪽)과 커널 인터페이스(오른쪽)를 잇는다. 초록 상자의 글자는 해당 모듈 ID

**드라이버 한 개를 처음부터 쓰기 위한 최소 체크리스트** (이게 다 "예"면 실무 투입 가능)
- [ ] 커널 C 관용구: `container_of`, `list_head`, `ERR_PTR`/`IS_ERR`, `goto` 에러 정리, `__iomem`, `likely()` (E5)
- [ ] 모듈을 빌드·로드하고 `dmesg`로 확인할 수 있다 (E3)
- [ ] DT 노드의 `compatible`이 드라이버의 `of_match_table`과 어떻게 만나 `probe()`가 불리는지 설명할 수 있다 (D3, G2)
- [ ] `devm_*` 자원 관리와 deferred probe(`-EPROBE_DEFER`)를 설명할 수 있다 (G3)
- [ ] 레지스터 접근: `ioremap`/`readl`/`writel` 또는 `regmap`을 쓴다 (H1, H2)
- [ ] 인터럽트: `request_threaded_irq`로 top/bottom half를 나눈다 (F6, H6)
- [ ] 지금 코드가 **atomic 컨텍스트인지 sleep 가능한지** 구분하고, 그에 맞는 락·할당 플래그를 고른다 (F2, F4, F5)
- [ ] 유저에게 보여 주기: char device `file_operations` 또는 적절한 서브시스템(IIO 등) 등록 (G4, I)
- [ ] DMA 버퍼를 `dma_map_single`/`dma_alloc_coherent`로 올바르게 다룬다 (H9)
- [ ] 전원·클럭·리셋 켜는 순서를 `clk`/`regulator`/`reset` API로 표현한다 (H3)
- [ ] 문제가 나면 oops 로그에서 함수·줄 번호를 찾아낸다 (M3)

---

## 2. 업무 장면 → 필요한 모듈

| 실무자가 실제로 하는 일 | 필요한 모듈 |
|---|---|
| 새 보드가 왔다. 시리얼 콘솔에 아무것도 안 뜬다 | C1–C6, M1, P1 |
| 커널은 뜨는데 센서(I2C IMU)가 `/sys/bus/iio`에 안 보인다 | D1–D6, G2–G3, H4, I2, M1 |
| 데이터시트만 있는 새 센서용 드라이버를 써야 한다 | E, F, G, H, I2, P3 |
| 카메라 4대를 동기화해서 프레임을 받아야 한다 | H9, I5, Q1–Q3 |
| CAN 버스로 모터 컨트롤러와 통신 | I10, Q4 |
| 부팅이 12초 걸린다. 3초로 줄여라 | C7, L1, M10 |
| 가끔 센서 샘플이 늦게 온다(지터) | F6–F8, M5–M6, N1–N4 |
| 커널 패닉이 필드에서 1주일에 한 번 난다 | M3–M4, M8, L7 |
| 펌웨어 업데이트가 중간에 전원이 꺼지면 벽돌이 된다 | J, L4 |
| 벤더 BSP 커널 5.15를 6.x LTS로 올려라 | E7, P2, P5 |
| 메인 SoC(Linux)와 보조 MCU(RTOS)가 통신해야 한다 | O1–O3, H10 |
| 보안팀: secure boot와 루트FS 무결성을 넣어라 | C5, L5–L6 |
| 이미지 빌드·패키지·라이선스 관리 | B, K |

---

## 3. 모듈별 목차

### A. Linux 유저스페이스 & 시스템 프로그래밍 — "커널 바깥에서 본 Linux"

> 커널을 공부하기 전에 커널이 **무엇을 제공하는지**를 유저 입장에서 먼저 안다. 드라이버가 노출하는 인터페이스를 테스트하는 툴도 결국 이 코드로 짠다.

| ID | 주제 | 핵심 개념 | 왜 필요 (인터뷰 / 실무) | 우선 | Don |
|---|---|---|---|---|---|
| A1 | Linux 전체 구조와 "everything is a file" | 커널 vs 유저, syscall, VFS, fd, `/dev` `/sys` `/proc` | 모든 질문의 출발점. "open()하면 무슨 일이?" | P0 | 🟡 |
| A2 | 프로세스와 스레드 | fork/exec/wait, pthread, 프로세스 상태, 좀비, `task_struct` 미리보기 | 기본 질문 단골 | P0 | 🟡 |
| A3 | 파일 I/O와 디바이스 접근 | open/read/write/ioctl/close, blocking vs non-blocking, `O_NONBLOCK` | 드라이버 테스트 코드 | P0 | 🟡 |
| A4 | I/O 다중화 | select/poll/epoll, eventfd, timerfd | 센서·소켓 이벤트 루프, 드라이버 `poll()` 구현과 짝 | P0 | ❌ |
| A5 | 메모리 맵핑과 가상 메모리(유저 관점) | mmap, page fault, 공유 메모리, `/proc/<pid>/maps`, mlock | 카메라 버퍼 zero-copy, RT 메모리 잠금 | P0 | 🟡 |
| A6 | 시그널 | 시그널 처리, async-signal-safe, signalfd | 데몬 종료 처리, 면접 함정 | P1 | ❌ |
| A7 | IPC | pipe, UNIX socket, shared memory, message queue, D-Bus 미리보기 | 서비스 간 통신 설계 | P1 | 🟡 |
| A8 | 동기화(유저) | mutex, condvar, semaphore, futex, priority inheritance | 커널 락과 비교 질문 | P0 | 🟡 |
| A9 | 네트워크 소켓 | TCP/UDP 소켓, raw socket, SocketCAN도 소켓이라는 점 | LiDAR UDP, CAN, 원격 툴 | P1 | 🟡 |
| A10 | 셸·커맨드라인 생존 키트 | grep/find/awk/sed, dmesg, lsmod, ip, ss, top/htop, journalctl | 실무 하루 종일 쓰는 것 | P0 | 🟡 |

### B. 툴체인 & 빌드 기초

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| B1 | 크로스 컴파일 툴체인 | triplet(`aarch64-linux-gnu`), sysroot, binutils, GCC vs Clang | "호스트 x86에서 ARM 바이너리 만드는 원리" | P0 | ✅ |
| B2 | C 라이브러리 | glibc vs musl vs uClibc-ng, 동적 로더 `ld-linux`, ABI | 이미지 크기·호환성 결정 | P1 | 🟡 |
| B3 | ELF와 링킹 (Linux 관점) | 섹션, 동적 링킹, PLT/GOT, `readelf`/`objdump`/`nm`, `LD_LIBRARY_PATH` | "실행 안 됨 — not found" 디버그 | P1 | ✅ |
| B4 | 빌드 시스템 | Make, Kbuild/Kconfig 미리보기, CMake, Meson, pkg-config | 앱·드라이버 빌드 | P1 | 🟡 |
| B5 | 툴체인 만들기·얻기 | 벤더 SDK, Linaro/Arm GNU, crosstool-NG, Buildroot/Yocto가 만든 SDK | 팀 개발 환경 세팅 | P2 | 🟡 |

### C. 부트 흐름 — 전원 ON부터 로그인 프롬프트까지

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| C1 | 부트 전체 지도 | BootROM → SPL/TPL → (TF-A/OP-TEE) → U-Boot → kernel → initramfs → init | **면접 1순위 질문** "Linux 보드 부팅 과정 설명" | P0 | 🟡 |
| C2 | BootROM과 부트 미디어 | boot pin/fuse, eMMC boot partition, SD, SPI NOR, USB 복구 모드(SDP, DFU) | 벽돌 복구, 공장 플래싱 | P1 | ✅ |
| C3 | U-Boot 사용 | 콘솔 명령, 환경변수, `bootcmd`/`bootargs`, TFTP/NFS 부팅, 스크립트, distro boot, FIT 이미지 | bring-up 첫날 일 | P0 | ❌ |
| C4 | U-Boot 포팅 | defconfig, U-Boot DT, DDR 초기화(SPL), driver model | 새 보드 bring-up | P1 | ❌ |
| C5 | ARM 신뢰 부팅 체인 | Exception Level(EL3/2/1/0), TF-A BL1/BL2/BL31, PSCI, OP-TEE, secure boot 서명 검증 | 보안 질문, 전원 관리(PSCI) | P1 | 🟡 |
| C6 | 커널 시작 과정 | Image/zImage, DTB 전달, `start_kernel`, initcall 레벨, earlycon/earlyprintk, rootfs 마운트, `init=` | "커널이 콘솔에 아무것도 안 찍어요" | P0 | ❌ |
| C7 | init과 부팅 완료 | BusyBox init vs systemd, initramfs 역할, 부팅 시간 측정·단축 | 부팅 시간 과제 | P1 | ❌ |
| C8 | x86/UEFI와 비교 | UEFI, GRUB, ACPI vs DT | 넓은 시야(NVIDIA·서버 쪽 면접) | P2 | 🟡 |

### D. Device Tree — 보드를 커널에 설명하는 언어

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| D1 | 왜 DT인가 | discoverable(PCI/USB) vs non-discoverable HW, board file의 역사, ACPI | 면접 단골 "DT가 뭐고 왜 씀" | P0 | ❌ |
| D2 | 문법 | node, property, `reg`, `#address-cells`/`#size-cells`, `ranges`, label/phandle, `status` | 읽고 고치기 | P0 | ❌ |
| D3 | 드라이버 매칭 | `compatible` 문자열 → `of_match_table` → `probe()`, 우선순위 | **드라이버 질문의 핵심 고리** | P0 | ❌ |
| D4 | 자원 기술 | interrupts/`interrupt-parent`, clocks, resets, regulators(`*-supply`), gpios, pinctrl, dmas | 센서 하나 붙이기에 다 나옴 | P0 | ❌ |
| D5 | 파일 구조 | `.dtsi`(SoC) / `.dts`(보드) 계층, include, override, `dtc`, `fdtdump`, `/proc/device-tree` | 벤더 BSP 읽기 | P0 | ❌ |
| D6 | Overlay | `.dtbo`, U-Boot/configfs 적용, Raspberry Pi/Jetson 방식 | 확장 보드·실습 | P1 | ❌ |
| D7 | Bindings와 검증 | `Documentation/devicetree/bindings`, YAML 스키마, `make dtbs_check` | upstream 기여, 리뷰 | P2 | ❌ |
| D8 | 드라이버에서 DT 읽기 | `of_property_read_*`, `device_property_*`(fwnode, ACPI 겸용) | 드라이버 작성 | P0 | ❌ |

### E. 커널 구조 & 개발 기본

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| E1 | 커널 아키텍처 개요 | monolithic + 모듈, 서브시스템 지도, 소스 트리 구조(`arch/` `drivers/` `kernel/` `mm/`) | 코드 찾아가기 | P0 | ❌ |
| E2 | 설정과 빌드 | Kconfig/menuconfig, defconfig, `make Image dtbs modules`, Kbuild Makefile(`obj-m`/`obj-y`) | 매일 하는 일 | P0 | ❌ |
| E3 | 커널 모듈 (LKM) | `module_init/exit`, `insmod/rmmod/modprobe`, 모듈 파라미터, `EXPORT_SYMBOL(_GPL)`, 의존성, out-of-tree 빌드 | 첫 실습, 면접 단골 | P0 | ❌ |
| E4 | 커널 프로그래밍 제약 | libc 없음, FPU 금지, 작은 커널 스택(8–16KB), 재귀 금지, 유저 포인터 직접 접근 금지 | "커널 코드가 앱 코드와 다른 점" | P0 | 🟡 |
| E5 | 커널 C 관용구 | `container_of`, `list_head`, `ERR_PTR`, `goto` 정리 패턴, `__iomem`/`__user` 주석, sparse | 코딩 면접에서 직접 씀 | P0 | 🟡 |
| E6 | 유저 ↔ 커널 경계 | syscall 경로, `copy_to_user`/`copy_from_user`, 에러 코드(`-EINVAL` 등) 규약 | 드라이버 read/write/ioctl | P0 | ❌ |
| E7 | 커널 버전·커뮤니티·라이선스 | mainline/stable/LTS, 벤더 커널, 패치 제출(`git format-patch`, `checkpatch.pl`, 메일링 리스트), GPL과 모듈 | 업그레이드·upstream 판단 | P1 | ❌ |
| E8 | Rust for Linux | Rust 드라이버 현황, 바인딩, 언제 쓰나 | 최신 트렌드 질문 대비 | P2 | ❌ |

### F. 커널 핵심 메커니즘 — 드라이버의 전제 지식

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| F1 | 프로세스·스케줄러 (커널 관점) | `task_struct`, CFS/EEVDF, 스케줄링 클래스(FIFO/RR/DEADLINE), preemption 모델 | 지연·우선순위 질문 | P0 | 🟡 |
| F2 | 실행 컨텍스트 | process context vs interrupt context(hardirq/softirq), atomic 컨텍스트, "잠들어도 되나" 규칙 | **가장 많이 틀리는 개념** | P0 | 🟡 |
| F3 | 잠자기와 깨우기 | wait queue, `wait_event_interruptible`, completion, `schedule()` | blocking read 구현 | P0 | 🟡 |
| F4 | 동시성과 락 | spinlock(+`_irqsave`), mutex, semaphore, rwlock, atomic, RCU, seqlock, per-CPU 변수 | **면접 최빈출** "spinlock vs mutex" | P0 | 🟡 |
| F5 | 메모리 배리어와 순서 | compiler barrier, `smp_mb`/`rmb`/`wmb`, `READ_ONCE`/`WRITE_ONCE`, MMIO 순서 | 멀티코어·DMA 버그 | P1 | ✅ |
| F6 | 인터럽트 처리 | IRQ 흐름(GIC → irqchip → handler), top half/bottom half, threaded IRQ, IRQ affinity | 드라이버 핵심 | P0 | 🟡 |
| F7 | 지연 작업 (deferred work) | softirq, tasklet(퇴출 중), workqueue, kthread, 언제 무엇을 | "ISR에서 I2C 읽으면 안 되는 이유" | P0 | ❌ |
| F8 | 시간과 타이머 | jiffies, HZ, `timer_list`, hrtimer, `ktime`, clocksource/clockevent, `udelay` vs `msleep` | 폴링·타임아웃·타임스탬프 | P0 | 🟡 |
| F9 | 커널 메모리 관리 | 물리/가상, 페이지, zone, 버디 할당자, slab/SLUB, `kmalloc` vs `vmalloc` vs `alloc_pages`, GFP 플래그(`GFP_KERNEL`/`GFP_ATOMIC`) | 면접 단골 | P0 | 🟡 |
| F10 | 큰 연속 메모리 | CMA, reserved-memory(DT), `memblock`, 카메라·GPU 버퍼 | 멀티미디어 드라이버 | P1 | ❌ |
| F11 | 가상 메모리 심화 | 페이지 테이블, TLB, page fault, OOM killer, swap 없는 임베디드 | OSTEP과 연결 | P1 | 🟡 |
| F12 | VFS 내부 | inode/dentry/file, `file_operations`가 불리는 경로 | char device 이해 | P1 | ❌ |

### G. 디바이스 드라이버 모델 — probe()가 불리기까지

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| G1 | 드라이버 종류 | char / block / network, 그리고 "대부분은 서브시스템 드라이버" | 면접 첫 질문 | P0 | ❌ |
| G2 | 디바이스 모델 | bus · device · driver · class, 매칭과 바인딩, kobject/kref, sysfs 트리 | "probe가 언제 불리나" | P0 | ❌ |
| G3 | platform driver와 생명주기 | `platform_driver`, probe/remove, `devm_*` 자원 관리, deferred probe, `driver_data` | **실무 드라이버의 90% 뼈대** | P0 | ❌ |
| G4 | 문자 디바이스 | major/minor, `cdev`, `alloc_chrdev_region`, `file_operations`(open/read/write/release), `private_data` | 화이트보드 코딩 단골 | P0 | ❌ |
| G5 | ioctl과 mmap | ioctl 설계(`_IOR`/`_IOW`), compat, `remap_pfn_range`, 왜 sysfs가 나은 경우가 있나 | 유저 인터페이스 설계 | P0 | ❌ |
| G6 | poll/select와 비동기 통지 | `.poll`, `poll_wait`, fasync | 이벤트 기반 드라이버 | P1 | ❌ |
| G7 | misc device | `misc_register`, 언제 쓰나 | 간단한 드라이버 지름길 | P1 | ❌ |
| G8 | sysfs · debugfs · configfs | `DEVICE_ATTR`, attribute group, debugfs 파일, 무엇을 어디에 노출하나 | 튜닝·디버그 인터페이스 | P0 | ❌ |
| G9 | 핫플러그와 udev | uevent, `MODULE_DEVICE_TABLE`, modalias, 자동 모듈 로드, udev rule | 장치 노드 권한·이름 | P1 | ❌ |
| G10 | 펌웨어 로딩 | `request_firmware`, `/lib/firmware`, 비동기 로딩 | Wi-Fi·카메라·DSP 칩 | P1 | 🟡 |

### H. HW 접근 & 버스 서브시스템 — Don의 강점을 Linux API로 번역

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| H1 | MMIO | `ioremap`, `devm_platform_ioremap_resource`, `readl`/`writel`(+`_relaxed`), 배리어, `/dev/mem`이 나쁜 이유 | 레지스터 드라이버 | P0 | ✅ |
| H2 | regmap | regmap-i2c/spi/mmio, 캐시, 필드(`regmap_field`), 왜 쓰나 | 센서·PMIC 드라이버 표준 | P0 | ❌ |
| H3 | 클럭·리셋·레귤레이터 | common clock framework(`clk_prepare_enable`), reset controller, regulator consumer API, 전원 시퀀스 | bring-up 시 "칩이 응답 안 함" | P0 | 🟡 |
| H4 | pinctrl과 GPIO | pin mux, pinctrl state, gpiolib, `gpiod_*` 디스크립터 API, GPIO→IRQ, libgpiod(유저), sysfs GPIO 폐기 | 센서 enable·인터럽트 핀 | P0 | 🟡 |
| H5 | I2C 서브시스템 | adapter/client/driver, `i2c_transfer`, SMBus API, i2c-tools(`i2cdetect`/`i2cget`), 버스 복구 | 센서 드라이버 1순위 | P0 | ✅ |
| H6 | 인터럽트 컨트롤러 (커널 내부) | GIC, irq_domain, irqchip, chained/nested IRQ, GPIO 확장기 IRQ | 복잡한 보드 IRQ 디버그 | P1 | 🟡 |
| H7 | SPI 서브시스템 | `spi_device`, `spi_sync`/`spi_async`, 메시지/트랜스퍼, spidev | IMU·ADC·디스플레이 | P0 | ✅ |
| H8 | UART · TTY · serial | serial core, TTY 계층, line discipline, `termios`, serdev(UART 위 장치) | GNSS·BT 모듈 | P1 | 🟡 |
| H9 | DMA | coherent vs streaming 매핑, `dma_alloc_coherent`, `dma_map_single/sg`, 캐시 관리, DMA mask, IOMMU/SMMU, dmaengine, dma-buf | **고급 면접 단골**, 카메라 | P0 | 🟡 |
| H10 | 메일박스 · 하드웨어 스핀락 | mailbox framework, hwspinlock | 이종 코어 통신 | P2 | ❌ |
| H11 | PCIe (Linux 관점) | enumeration, config space, BAR, `pci_driver`, MSI/MSI-X, `lspci -vvv` | NVMe·Wi-Fi·가속기 | P1 | ✅ |
| H12 | USB (Linux 관점) | host/gadget, usbcore, `usb_driver`, configfs gadget, `lsusb` | 장치 연결·디버그 포트 | P2 | 🟡 |

### I. 커널 서브시스템 (프레임워크) — 드라이버를 어디에 붙이나

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| I1 | 프레임워크 원칙 | "직접 char device 만들지 말고 서브시스템에 등록", ops 구조체 패턴 | 설계 질문 | P0 | ❌ |
| I2 | IIO (Industrial I/O) | channel, `iio_info`, buffered capture, trigger, kfifo, 타임스탬프 | **IMU·ADC·환경센서 = Scout 핵심** | P0 | ❌ |
| I3 | input 서브시스템 | `input_dev`, 이벤트, evtest | 버튼·터치·조이스틱 | P1 | ❌ |
| I4 | hwmon · thermal · power supply | 온도/전압 센서, thermal zone·cooling device, 배터리 | 열 관리·헬스 모니터 | P1 | 🟡 |
| I5 | V4L2와 media controller | `v4l2_device`/subdev, video node, media graph, buffer queue(videobuf2), 포맷 협상 | **카메라 = Scout 핵심** | P0 | ❌ |
| I6 | 네트워크 드라이버 | `net_device`, sk_buff, NAPI, PHY/MDIO, ethtool | LiDAR·차량 이더넷 | P1 | ❌ |
| I7 | ALSA / ASoC | codec/platform/machine, PCM | 오디오 장치 | P2 | ❌ |
| I8 | DRM/KMS (디스플레이) | CRTC/encoder/connector/plane, GPU와의 관계 | 디스플레이·GPU 이해 | P2 | ❌ |
| I9 | LED · PWM · RTC · watchdog | 작은 프레임워크들, watchdog 데몬 | 상태 표시·시스템 안정성 | P1 | 🟡 |
| I10 | CAN 서브시스템 | SocketCAN, `can_dev`, bitrate·error state·bus-off, CAN FD, MCP251x | **로보틱스·차량 필수** | P0 | ❌ |
| I11 | MTD · MMC · block | MTD 장치, NAND 컨트롤러 드라이버, MMC/SDHCI, blk-mq | 스토리지 드라이버 | P2 | ✅ |

### J. 스토리지 & 파일시스템

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| J1 | 임베디드 저장장치 | NOR, raw NAND, eMMC, SD, UFS, NVMe — 각각 무엇이 FTL을 갖나 | SSD 경험을 Linux로 연결 | P1 | ✅ |
| J2 | 블록 파일시스템 | ext4, F2FS, 저널링, 마운트 옵션, 파티션(GPT/MBR) | 일반 운영 | P1 | 🟡 |
| J3 | 읽기전용·메모리 FS | SquashFS, EROFS, tmpfs, overlayfs(RO 루트 + RW 상위층) | 견고한 rootfs 설계 | P1 | ❌ |
| J4 | Flash FS | MTD, UBI/UBIFS, JFFS2, wear leveling, bad block | raw NAND 보드 | P2 | ✅ |
| J5 | 전원 차단 안전성 | fsync, write barrier, 저널 모드, eMMC 캐시, 로그 저장 설계 | 필드 장비 데이터 손실 | P1 | ✅ |

### K. rootfs & 빌드 시스템 — 이미지 만들기

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| K1 | rootfs 구조 | FHS(`/bin` `/etc` `/lib` `/dev` …), 필수 구성, 장치 노드, devtmpfs | 최소 시스템 이해 | P0 | ❌ |
| K2 | BusyBox | 멀티콜 바이너리, init, 최소 이미지 | 작은 시스템·initramfs | P1 | ❌ |
| K3 | Buildroot | defconfig, 패키지 추가, external tree, 산출물 | 빠른 실습·작은 제품 | P0 | ❌ |
| K4 | Yocto / OpenEmbedded 기초 | layer, recipe(`.bb`/`.bbappend`), BitBake, MACHINE/DISTRO, image | **업계 표준 — JD 단골 키워드** | P0 | ❌ |
| K5 | Yocto 실무 | BSP layer, devtool, 커널 레시피·패치, SDK(eSDK), sstate 캐시, 빌드 재현성 | 입사 후 매일 | P1 | ❌ |
| K6 | 바이너리 배포판 방식 | Debian/Ubuntu 기반(Jetson L4T 등), 장단점 | Jetson 계열 회사 | P1 | ❌ |
| K7 | 라이선스와 SBOM | GPL/LGPL 의무, 소스 공개, SPDX/SBOM | 제품 출시 | P2 | ❌ |

### L. 유저스페이스 시스템 · 업데이트 · 보안

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| L1 | systemd | unit/service/target, 의존성, watchdog 연동, journald, `systemd-analyze` | 서비스 관리·부팅 시간 | P0 | ❌ |
| L2 | udev와 장치 관리 | rule 작성, 고정 이름(symlink), 권한 | 카메라 `/dev/video*` 순서 문제 | P1 | ❌ |
| L3 | D-Bus와 네트워크 구성 | D-Bus 개념, NetworkManager/systemd-networkd, 정적 IP | 시스템 통합 | P2 | ❌ |
| L4 | OTA 업데이트 | A/B 파티션, 롤백, SWUpdate/RAUC/Mender, U-Boot bootcount | **필드 장비 필수** 설계 질문 | P1 | 🟡 |
| L5 | Secure boot 체인 | ROM 키 → 서명된 부트로더 → 서명된 FIT → 커널, 키 관리 | 국방·보안 회사 | P1 | 🟡 |
| L6 | 런타임 보안 | dm-verity, dm-crypt, TPM/TEE, SELinux/AppArmor, capabilities, 최소 권한 | 보안 질문 | P2 | ❌ |
| L7 | 크래시 데이터 수집 | pstore/ramoops, kdump, coredump 수집, 원격 로그 | 필드 이슈 분석 | P1 | ❌ |
| L8 | 컨테이너 on embedded | namespace/cgroup, Docker/Podman on 장비, 장단점 | AI 장비(Jetson) 배포 | P2 | ❌ |

### M. 디버깅 · 트레이싱 · 프로파일링 — 실무 생존 기술

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| M1 | 로그 기반 디버깅 | printk 레벨, `dev_dbg`/`pr_*`, dynamic debug, `dmesg`, earlycon, `loglevel` | 매일 | P0 | ❌ |
| M2 | 커널 상태 들여다보기 | `/proc`(interrupts, iomem, meminfo), `/sys`, debugfs, `lsmod`, `/sys/kernel/debug/gpio` 등 | bring-up 진단 | P0 | ❌ |
| M3 | Oops·panic 해독 | 콜스택, PC/LR, `addr2line`, `decode_stacktrace.sh`, `System.map`, BUG/WARN | **면접 단골** "oops 로그 보면 뭐부터?" | P0 | ❌ |
| M4 | 커널 디버거 | kgdb, JTAG + OpenOCD/Lauterbach로 커널 디버그, Magic SysRq, `vmlinux` 심볼 | JTAG 경험의 Linux 연결 | P1 | ✅ |
| M5 | ftrace | function/function_graph tracer, tracepoints, `trace-cmd`, KernelShark, irqsoff/wakeup tracer | 지연·순서 문제 | P0 | ❌ |
| M6 | perf | `perf stat/record/report/top`, 하드웨어 카운터, flame graph | 성능 문제 | P1 | ❌ |
| M7 | eBPF 도구 | bpftrace, bcc, kprobe/uprobe | 최신 트렌드, 운영 중 진단 | P2 | ❌ |
| M8 | 버그 탐지기 | lockdep, KASAN, KCSAN, kmemleak, UBSAN, `CONFIG_DEBUG_*` | 레이스·메모리 버그 | P1 | ❌ |
| M9 | 유저스페이스 디버깅 | gdb + gdbserver(원격), core dump, strace, ltrace, valgrind | 앱·툴 디버그 | P0 | 🟡 |
| M10 | 부팅·시스템 분석 | `systemd-analyze`, bootchart, `initcall_debug`, vmstat/iostat/top | 부팅 시간·부하 | P1 | ❌ |
| M11 | HW 연계 디버깅 | scope·LA로 커널 타이밍 확인(GPIO 토글), 버스 트레이스와 커널 로그 맞추기 | **Don의 차별화 포인트** | P0 | ✅ |

### N. 실시간 · 성능 · 전원 관리

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| N1 | Linux는 실시간인가 | soft vs hard RT, 지연 원인(IRQ off, preempt off, 캐시, SMI), PREEMPT_RT(mainline 편입) | **로보틱스 면접 단골** | P0 | 🟡 |
| N2 | 실시간 튜닝 | SCHED_FIFO/DEADLINE, priority, CPU isolation(`isolcpus`, `nohz_full`), IRQ affinity, mlockall | 센서 루프 지터 | P1 | ❌ |
| N3 | 지연 측정 | cyclictest, rtla/osnoise/timerlat, 히스토그램 해석 | 수치로 말하기 | P1 | ❌ |
| N4 | Linux vs RTOS vs MCU 분담 | 무엇을 MCU로 내리나, 아키텍처 판단 기준 | 설계 질문 | P0 | ✅ |
| N5 | 전원 관리 | system suspend/resume, runtime PM(`pm_runtime_*`), cpufreq/cpuidle, devfreq, PM QoS | 배터리·열 | P1 | 🟡 |
| N6 | 드라이버의 PM 구현 | `dev_pm_ops`, suspend 순서, wakeup source | 드라이버 완성도 | P2 | ❌ |

### O. 이종 코어 · SoC 통합

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| O1 | Cortex-A + Cortex-M 구조 | i.MX8/9, STM32MP1, TI AM62, Jetson의 보조 코어, 메모리 공유 | 요즘 SoC 기본 | P1 | 🟡 |
| O2 | remoteproc · rpmsg · OpenAMP | 펌웨어 로드, virtio, rpmsg 채널, resource table | Linux ↔ RTOS 통신 | P1 | ❌ |
| O3 | Linux ↔ 외부 MCU 통신 설계 | UART/SPI/CAN 프로토콜, 타임스탬프, 펌웨어 업데이트 경로 | Scout 형태 시스템 | P0 | ✅ |
| O4 | 가속기와 Linux | GPU/NPU 드라이버 구조(DRM accel), dma-buf 공유, Jetson(L4T) 구조 | AI 칩 진로와 연결 | P2 | 🟡 |
| O5 | 대표 SoC 플랫폼 지형 | NXP i.MX, TI Sitara/AM6x, Rockchip, Qualcomm, NVIDIA Jetson, Raspberry Pi | 회사별 스택 대화 | P2 | 🟡 |

### P. 보드 bring-up & BSP 실무 — "입사 후 첫 6개월"

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| P1 | 새 보드 bring-up 순서 | 전원·클럭 → UART 콘솔 → DDR → 부트로더 → 커널 → DT 하나씩 활성화 → 주변장치 | **면접 스토리 질문 1순위** | P0 | 🟡 |
| P2 | BSP 구조와 벤더 커널 | 벤더 BSP 구성, 벤더 vs mainline 커널 선택, 패치 스택 관리 | 실무 판단 | P1 | 🟡 |
| P3 | 데이터시트 → 드라이버 워크플로 | 기존 드라이버 찾기(`git grep compatible`), 비슷한 드라이버 복제·수정, 레지스터 맵 → regmap | 실무 핵심 루틴 | P0 | 🟡 |
| P4 | 회로도 읽고 DT 쓰기 | 핀·레귤레이터·IRQ 라인 추적 → DT 노드 | HW 팀과 협업 | P0 | ✅ |
| P5 | 커널 업그레이드·리베이스 | LTS 이동, 패치 포팅, 회귀 확인 | 장수 제품 | P2 | ❌ |
| P6 | 테스트와 CI | kselftest, KUnit, LAVA, HIL 테스트, 생산 테스트 | 품질 | P1 | 🟡 |
| P7 | upstream 기여 | 패치 작성·리뷰 대응, 유지보수자 | 커리어 신호 | P2 | ❌ |

### Q. 도메인 스택 — 카메라 · 센서 · CAN · 로보틱스

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| Q1 | 카메라 HW 인터페이스 | MIPI CSI-2/D-PHY, lane, GMSL/FPD-Link SerDes, ISP | Scout·로보틱스 | P0 | 🟡 |
| Q2 | Linux 카메라 파이프라인 | 센서 subdev → CSI 수신기 → ISP → video node, `media-ctl`/`v4l2-ctl`, libcamera, GStreamer | 실무 디버그 | P0 | ❌ |
| Q3 | 멀티 센서 동기화 | HW trigger, frame sync, 타임스탬프(CLOCK_MONOTONIC), PPS, PTP/gPTP(`ptp4l`, `phc2sys`) | 센서 퓨전 품질 | P0 | 🟡 |
| Q4 | CAN 실무 | `ip link set can0`, candump/cansend, vcan, DBC, 에러 프레임 분석 | 차량·로봇 | P0 | ❌ |
| Q5 | IMU · GNSS · LiDAR on Linux | IIO IMU 드라이버, gpsd/UBX, LiDAR UDP 수신·손실 | 센서 bring-up | P1 | 🟡 |
| Q6 | ROS 2 on embedded | 노드/토픽, 센서 메시지 타임스탬프, DDS, 실시간 설정 | 로보틱스 회사 공통어 | P1 | ❌ |
| Q7 | 센서 데이터 품질 검증 툴 | 샘플레이트·지터·드롭 측정, 로깅, 헬스 모니터 | JD "tools for data quality" | P1 | 🟡 |

### R. 인터뷰 준비

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| R1 | 개념 질문 Top 50 | 모듈 A–N에서 뽑은 단골 질문 + 30초·2분 답 | 1차 기술 면접 | P0 | ❌ |
| R2 | "무슨 일이 일어나나" 시리즈 | 전원 ON→셸, `open("/dev/x")`→드라이버, 인터럽트 발생→유저 깨어남, `insmod`→probe | 깊이 확인 질문 | P0 | ❌ |
| R3 | 화이트보드 코딩 (커널 스타일) | char driver 뼈대, platform driver + DT, 링 버퍼, `container_of`, 비트 조작, 레지스터 구조체 | 라이브 코딩 | P0 | 🟡 |
| R4 | 디버깅 시나리오 | "센서가 안 보인다", "가끔 데이터가 깨진다", "부팅 중 멈춘다" → 단계별 진단 | 실무 감각 평가 | P0 | ✅ |
| R5 | 시스템 설계 | 차량 센서 키트(카메라 N + IMU + GNSS + CAN → 엣지 GPU), 로깅, OTA, degrade 모드 | 시니어 라운드 | P1 | 🟡 |
| R6 | Don 스토리 브리지 | SSD FW·RF bring-up 경험을 Linux 용어로 다시 말하기, "Linux가 약한 점" 답변 | 행동 면접 | P0 | ✅ |

### S. 실습 프로젝트 — 손으로 증명하기

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| S1 | QEMU 랩 만들기 | QEMU `virt` ARM64 + Buildroot + 커널 빌드, 보드 없이 시작 | 모든 실습의 기반 | P0 | ❌ |
| S2 | 첫 모듈 · char device | hello 모듈 → 파라미터 → char device(read/write/ioctl/poll) | E·G 체득 | P0 | ❌ |
| S3 | 실제 보드 bring-up | Raspberry Pi 5 / BeaglePlay / Jetson Orin Nano 중 하나, U-Boot·커널 직접 빌드 | C·D 체득 | P1 | ❌ |
| S4 | I2C/SPI IMU IIO 드라이버 | DT overlay + regmap + IIO buffered + 타임스탬프 + 지터 측정 툴 | **포트폴리오 핵심 (GitHub)** | P0 | ❌ |
| S5 | 트레이싱 실습 | ftrace로 IRQ→유저 지연 측정, cyclictest PREEMPT_RT 전후 비교 | M·N 체득 | P1 | ❌ |
| S6 | Yocto 커스텀 이미지 | 자체 layer, 드라이버 레시피, 이미지에 포함 | K 체득, JD 키워드 | P1 | ❌ |
| S7 | SocketCAN 실습 | vcan → MCP2515 모듈, 송수신·에러 관찰 | Q4 체득 | P1 | ❌ |

---

## 4. 학습 순서 제안 (OSTEP과 병행 기준)

| 단계 | 기간(제안) | 모듈 | 끝났을 때 할 수 있는 것 |
|---|---|---|---|
| Phase 1 기반 | 1–2주 | A(P0만), B, C, D, E + S1·S2 | 부팅 과정과 DT를 설명하고, 모듈을 빌드해 QEMU에서 로드 |
| Phase 2 드라이버 | 2–3주 | F, G, H, I1·I2·I5·I10 + S4 시작 | 센서 드라이버 뼈대를 화이트보드에 쓰고, 컨텍스트·락을 근거 있게 고름 |
| Phase 3 시스템 | 1–2주 | J, K, L + S6 | Buildroot/Yocto로 이미지를 만들고 OTA·보안 설계를 말로 설명 |
| Phase 4 실무력 | 2주 | M, N, O, P + S5 | oops 해독, ftrace로 지연 분석, bring-up 순서 스토리 |
| 병행 | 전체 | Q, R | 회사별(Scout: Q 비중 ↑) 맞춤 답변 |

> **1차 면접이 급하면**: `P0`만 먼저 (76개) — A1–A5, A8, A10, B1, C1, C3, C6, D1–D5, D8, E1–E6, F1–F4, F6–F9, G1–G5, G8, H1–H5, H7, H9, I1, I2, I5, I10, K1, K3, K4, L1, M1–M3, M5, M9, M11, N1, N4, O3, P1, P3, P4, Q1–Q4, R1–R4, R6, S1, S2, S4

---

## 5. 참고 자료 (노트 작성 시 1차 출처)

**무료 · 최신**
- Bootlin 교육 자료 (슬라이드·실습 전부 공개): https://bootlin.com/training/ — Embedded Linux · Kernel & Driver · Yocto · Buildroot · Debugging · Graphics · Audio
- 커널 공식 문서: https://docs.kernel.org/ — 특히 `driver-api/`, `devicetree/`, `core-api/`, `trace/`
- Elixir 소스 브라우저: https://elixir.bootlin.com/
- LWN.net 커널 기사, eLinux.org, Embedded Linux Conference 발표 영상

**책**
- *Mastering Embedded Linux Development* (Frank Vasquez, Chris Simmonds) — A·B·C·K·L 모듈 축
- *Linux Device Driver Development* 2nd ed. (John Madieu) — G·H·I 모듈 축 (최신 API)
- *Linux Device Drivers* 3rd ed. (LDD3, 무료) — 개념은 고전, API는 2.6 시절이라 현재 코드와 대조하며 읽기
- *Linux Kernel Development* (Robert Love) — F 모듈 개념
- *The Linux Programming Interface* (Michael Kerrisk) — A 모듈 레퍼런스
- *Embedded Linux Primer* — 부트·bring-up 개념

**Don 기존 노트 재사용**
- Hark BSP 노트: `job_interview_prep_research/hark_ai_embedded_swe/2026-09-19_hark_study_notes/bsp/` (B01 BSP 구조, B03 Linux/Android BSP), `concepts/C03` 주변장치 드라이버, `C10` 디버깅·bring-up
- Verkada Embedded Linux 노트: `job_interview_prep_research/verkada_sr_embedded_linux_engineer_connectivity/notes_site/`, 동시성 가이드
- Scout AI 컨텍스트 §4.8 초안 (이 목차의 출발점): `job_interview_prep_research/scout_ai_embedded/scout_ai_embedded_context.md`
- OSTEP 노트 (F9·F11 가상 메모리, F4 동시성과 연결)

---

## 6. 진행 현황

| 모듈 | 항목 수 | 노트 작성 | 읽음 | 메모 |
|---|---|---|---|---|
| A 유저스페이스 | 10 | 10 | 0 | 2026-10-05 A1–A10 작성 (VM에서 예제 실행) |
| B 툴체인 | 5 | 5 | 0 | 2026-10-10 B1–B5 작성 (크로스 gcc·musl·crosstool-NG 실습) |
| C 부트 | 8 | 8 | 0 | 2026-10-10 C1–C8 작성 (QEMU 랩: TF-A·U-Boot·커널 실제 부팅) |
| D Device Tree | 8 | 8 | 0 | 2026-10-10 D1–D8 작성 (직접 만든 드라이버를 DT 노드에 probe) |
| E 커널 기본 | 8 | 8 | 0 | 2026-10-10 E1–E8 작성 (모듈·tinyconfig·Rust 커널 직접 빌드) |
| F 커널 메커니즘 | 12 | 12 | 0 | 2026-10-10 F1–F12 작성 (디버그 커널: ftrace·lockdep·CMA debugfs) |
| G 드라이버 모델 | 10 | 10 | 0 | 2026-10-10 G1–G10 작성 (sensorkit 공통 예제, char/misc/sysfs/poll/udev/펌웨어) |
| H HW 접근·버스 | 12 | 12 | 0 | 2026-10-10 H1–H12 작성 (edu PCI·가짜 I2C/SPI 컨트롤러·SMMU·dummy_hcd) |
| I 서브시스템 | 11 | 11 | 0 | 2026-10-11 I1–I11 작성 (Image-sub 가상 드라이버: vivid/vimc·vkms·snd·CAN 컨트롤러·IIO·nandsim) |
| J 스토리지·FS | 5 | 0 | 0 | |
| K rootfs·빌드 | 7 | 0 | 0 | |
| L 시스템·OTA·보안 | 8 | 0 | 0 | |
| M 디버깅 | 11 | 0 | 0 | |
| N 실시간·전원 | 6 | 0 | 0 | |
| O 이종 코어 | 5 | 0 | 0 | |
| P bring-up 실무 | 7 | 0 | 0 | |
| Q 도메인 | 7 | 0 | 0 | |
| R 인터뷰 | 6 | 0 | 0 | |
| S 실습 | 7 | 0 | 0 | |
| **합계** | **153** | **84** | **0** | 모듈 A (10-05) · B–H (10-10) · I (10-11) 완료 |
