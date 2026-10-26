# A6. 2026 RTOS 지형도 — 누가 만들고, 누가 쓰고, 무엇으로 고르나

> **이 노트를 다 읽으면**: FreeRTOS·Zephyr·ThreadX·NuttX·RT-Thread·ChibiOS·RTEMS·µC/OS와 SafeRTOS·VxWorks·QNX·INTEGRITY·PikeOS·embOS를 출신·라이선스·거버넌스·인증 상태로 한 장에 정리할 수 있다 · 드론·자동차·우주·IoT·웨어러블에서 "누가 무엇을 쓰는지"를 출처와 함께 말할 수 있다 · 라이선스·인증·생태계·LTS·CRA 대응을 기준으로 RTOS를 근거 있게 고르고 그 이유를 영어로 설명할 수 있다 · 생태계 크기를 소스 트리와 GitHub에서 직접 숫자로 뽑을 수 있다
> **왜 필요한가**: 면접에서 "Why Zephyr over FreeRTOS?", "What does PX4 run on?", "Can we certify this RTOS?"가 나오고, 입사하면 "다음 제품 RTOS를 뭘로 할지" 회의에 들어간다. 지형을 모르면 둘 다 감으로 답하게 된다.
> **Don 기준 난이도**: SSD 펌웨어에서 자체 executive와 벤더 SDK를 써 봐서 "플랫폼을 고른다"는 감각은 있다 / 범용 RTOS 각각의 출신·라이선스·인증 현황, 재단 거버넌스, EU CRA, 2026년 트렌드는 새로 배운다
> **선행 노트**: A1(실시간이란), A3(RTOS 구성 요소). 이어지는 노트: A5(API 로제타석), A7(bare-metal vs RTOS vs Linux), H1(Zephyr 큰 그림), I1~I11(다른 RTOS·Rust), O2(안전 RTOS), L7(CRA 실무)

---

## 0. 큰 그림 — RTOS는 "커널"이 아니라 "플랫폼"을 고르는 일이다

Don이 SSD 컨트롤러 펌웨어를 짤 때 RTOS를 고른 적은 아마 없을 것이다. 회사가 오래 다듬어 온 자체 executive(태스크 테이블 + 이벤트 루프 + 인터럽트 핸들러)가 이미 있었고, 바꿀 이유도 없었다. 그런데 드론·로봇·차량 회사로 가면 사정이 다르다. 새 제품 라인을 시작하면서 "FreeRTOS로 갈까, Zephyr로 갈까, 인증 때문에 SafeRTOS를 사야 하나"를 실제로 결정하고, 그 결정이 5~10년 동안 팀의 일상을 지배한다.

이 결정에서 **커널 자체의 성능 차이는 생각보다 작다**. 스케줄러는 다들 고정 우선순위 선점형이고(§10), 컨텍스트 스위치는 수 µs 안팎이다. 실제로 갈리는 것은 커널 바깥이다.

- **라이선스**: 소스를 공개해야 하나? 상용 라이선스 비용은?
- **거버넌스**: 누가 로드맵을 정하나? 그 회사가 사라지거나 방향을 바꾸면?
- **인증**: IEC 61508 SIL 3, ISO 26262 ASIL D, DO-178C DAL A 증거를 살 수 있나?
- **생태계**: 내 MCU, 내 무선 스택(BLE·Thread·Wi-Fi), 내 부트로더가 이미 붙어 있나?
- **수명**: LTS가 몇 년인가? EU CRA가 요구하는 보안 업데이트를 누가 해 주나?

그래서 이 노트는 RTOS를 **"커널 + 그 커널을 둘러싼 회사·재단·라이선스·인증·커뮤니티"** 로 본다. 아래 그림이 전체 지도다.

```svg
<svg viewBox="0 0 680 378" xmlns="http://www.w3.org/2000/svg"><text x="340" y="18" font-size="13" text-anchor="middle" font-weight="bold">2026 RTOS 지형 — 라이선스(가로) × 인증 지향(세로)</text><text x="175" y="40" font-size="12" text-anchor="middle" font-weight="bold">오픈소스 (MIT · Apache-2.0 · BSD · GPL)</text><text x="510" y="40" font-size="12" text-anchor="middle" font-weight="bold">상용 (라이선스 비용 · 벤더 지원)</text><rect x="10" y="50" width="320" height="158" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="20" y="69" font-size="13" text-anchor="start" font-weight="bold">범용 · 생태계 중심</text><text x="20" y="90" font-size="12" text-anchor="start">FreeRTOS — AWS, MIT, 가장 넓은 포트</text><text x="20" y="108" font-size="12" text-anchor="start">Zephyr — LF, Apache-2.0, 보드 1,000+</text><text x="20" y="126" font-size="12" text-anchor="start">NuttX — ASF, POSIX, PX4의 OS</text><text x="20" y="144" font-size="12" text-anchor="start">RT-Thread — Apache-2.0, 중국 생태계</text><text x="20" y="162" font-size="12" text-anchor="start">ChibiOS — GPL3/상용, ArduPilot</text><text x="20" y="180" font-size="12" text-anchor="start">µC/OS-III — Apache-2.0 (2020~)</text><text x="20" y="198" font-size="12" text-anchor="start">Rust: Embassy · RTIC · Hubris · Tock</text><rect x="345" y="50" width="325" height="104" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="355" y="69" font-size="13" text-anchor="start" font-weight="bold">범용 상용 · 툴 중심</text><text x="355" y="90" font-size="12" text-anchor="start">embOS — SEGGER, 1992~</text><text x="355" y="108" font-size="12" text-anchor="start">VxWorks — Wind River, 1987~</text><text x="355" y="126" font-size="12" text-anchor="start">QNX SDP 8.0 — 마이크로커널, 자동차</text><text x="355" y="144" font-size="12" text-anchor="start">INTEGRITY — Green Hills</text><rect x="10" y="230" width="320" height="122" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="20" y="249" font-size="13" text-anchor="start" font-weight="bold">오픈소스 · 인증 진행 / 유산 / 종료</text><text x="20" y="270" font-size="12" text-anchor="start">Zephyr — 안전 인증 작업 진행 중</text><text x="20" y="288" font-size="12" text-anchor="start">ThreadX — MS 시절 인증 유산, Eclipse</text><text x="20" y="306" font-size="12" text-anchor="start">RTEMS — 우주 임무 다수 (ESA·NASA)</text><text x="20" y="324" font-size="12" text-anchor="start">FreeRTOS — 인증은 SafeRTOS로 우회</text><text x="20" y="342" font-size="12" text-anchor="start">Mbed OS (Arm) — 2026-07 지원 종료</text><rect x="345" y="230" width="325" height="104" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="355" y="249" font-size="13" text-anchor="start" font-weight="bold">안전 인증 상용 (SIL 3 · ASIL D · DAL A)</text><text x="355" y="270" font-size="12" text-anchor="start">SafeRTOS — FreeRTOS 모델, TÜV SÜD</text><text x="355" y="288" font-size="12" text-anchor="start">QNX OS for Safety · VxWorks Cert</text><text x="355" y="306" font-size="12" text-anchor="start">INTEGRITY-178 · PikeOS (분리 커널)</text><text x="355" y="324" font-size="12" text-anchor="start">embOS-Safe · AUTOSAR OS (ECU)</text><text x="340" y="370" font-size="12" text-anchor="middle">위로 갈수록 생태계·무료, 아래로 갈수록 인증 증거·비용. 선은 흐리다 — 한 RTOS가 여러 칸에 걸친다</text></svg>
```

그림 1 — 2026년 RTOS 지형. 가로는 라이선스(오픈소스/상용), 세로는 범용/안전 인증 지향. 경계는 흐리다: Zephyr는 위 왼쪽에서 아래 왼쪽으로 내려오는 중이고(인증 작업), FreeRTOS는 같은 기능 모델을 가진 상용 SafeRTOS로 아래 오른쪽과 이어진다.

bare-metal 비유로 말하면 이렇다. RTOS를 고르는 것은 **"SSD 컨트롤러에 어떤 NAND 벤더를 붙일지"** 고르는 일과 닮았다. 셀 특성(=커널 성능)도 보지만, 결국 공급 안정성(=거버넌스), 인증·신뢰성 데이터(=안전 인증), 레퍼런스 디자인과 FAE 지원(=생태계), 단종 정책(=LTS·EOL)이 결정을 좌우한다.

### 0.1 이 노트의 지도

| 절 | 내용 | 실행 예제 |
|---|---|---|
| §1 | 지형을 보는 다섯 개의 축 (용어 정리) | — |
| §2 | 40년 연표 — 시대별로 무엇이 바뀌었나 | — |
| §3 | 오픈소스 RTOS 하나씩 | 예제 1 (FreeRTOS 포트 수), 예제 2 (Zephyr 보드 수) |
| §4 | 상용 · 안전 인증 RTOS, AUTOSAR OS | — |
| §5 | 라이선스 · 거버넌스 매트릭스 | 예제 3 (SPDX 라이선스 스캔) |
| §6 | 누가 무엇을 쓰나 — 도메인 지도 | — |
| §7 | 생태계 크기를 숫자로 | 예제 4 (커널 LOC), 예제 5 (GitHub 지표), 예제 6 (릴리스 주기), 예제 7 (최소 이미지 크기) |
| §8 | 고르는 법 — 기준 매트릭스, 결정 흐름, Don 맞춤 사례 4개 | — |
| §9 | 2026 트렌드 — contents §5를 한 단계 더 | — |
| §10 | RTOS 커널 안 — 설계 선택을 한눈에 | 예제 8, 예제 9 (소스에서 설계 확인) |

### 실습 환경 한 줄

예제는 전부 Lima VM `elinux`(aarch64 Ubuntu 26.04)에서 돌렸다. FreeRTOS V11.3.2(`/var/rtoslab/src/FreeRTOS-Kernel`)와 Zephyr v4.4.2(`/var/rtoslab/zephyrproject/zephyr`)는 랩 소스, 나머지 6개 RTOS(ThreadX, µC/OS-III, ChibiOS, RT-Thread, NuttX, RTEMS)는 2026-10-25에 GitHub에서 `git clone --depth 1`로 받은 **trunk(개발 브랜치) 사본**이다(`/var/tmp/rtos/A6/src/`). 예제 7만 QEMU `mps2-an385`(Cortex-M3 @ 25 MHz)에서 `ICOUNT=1`로 실행했다. 이 노트의 숫자는 "조사 시점의 스냅숏"이고, 날짜가 붙은 사실은 전부 참고 자료의 출처에서 2026-10-25에 확인했다. 확인하지 못한 것은 "확인 못 함"이라고 적었다.

```sh
# 다른 RTOS 소스 받기 (내 VM 작업 폴더 안에서만)
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/rtos/A6/src && cd /var/tmp/rtos/A6/src &&
  for r in apache/nuttx eclipse-threadx/threadx RT-Thread/rt-thread ChibiOS/ChibiOS RTEMS/rtems weston-embedded/uC-OS3;
  do git clone -q --depth 1 https://github.com/$r.git $(basename $r); done; du -sh *'
```

```text
472M	ChibiOS
587M	nuttx
1.9G	rt-thread
284M	rtems
118M	threadx
4.7M	uC-OS3
```

출력에서 볼 것: RT-Thread가 1.9 GB로 가장 크다. 커널이 커서가 아니라 저장소 안에 수백 개 보드의 BSP와 벤더 HAL을 함께 넣어 두기 때문이다. "저장소 크기 = 커널 크기"가 아니라는 것이 §7의 첫 교훈이다.

---

## 1. 지형을 보는 다섯 개의 축

RTOS 비교 글을 읽다 보면 같은 단어가 다른 뜻으로 쓰인다. 먼저 이 노트에서 쓰는 축과 용어를 고정한다.

### 1.1 라이선스 — "고쳐서 팔 때 무엇을 공개해야 하나"

- **permissive(허용적) 라이선스**: MIT, Apache-2.0, BSD-2/3-Clause. 소스를 고쳐서 바이너리로 팔아도 **내 소스를 공개할 의무가 없다**. 저작권 고지만 남기면 된다. Apache-2.0은 여기에 **특허 허락(patent grant)** 조항이 있어서 큰 회사들이 선호한다. FreeRTOS(MIT), Zephyr(Apache-2.0), ThreadX(MIT), NuttX(Apache-2.0), RT-Thread(Apache-2.0), µC/OS(Apache-2.0)가 여기에 속한다.
- **copyleft 라이선스**: GPL 계열. 고친 소스를 배포하면 그 소스도 같은 라이선스로 공개해야 한다. MCU 펌웨어는 커널과 앱이 한 바이너리로 링크되므로, 순수 GPL 커널을 쓰면 **앱 소스까지** 공개 대상이 될 수 있다. 그래서 GPL을 쓰는 RTOS는 보통 **linking exception**(예외 조항)을 붙이거나 **상용 라이선스를 따로 판다**(dual licensing). ChibiOS/RT가 GPL3 + 상용, 예전 FreeRTOS가 "수정 GPLv2 + 예외", RTEMS가 "GPL-2.0 + RTEMS 예외 → BSD-2-Clause로 이전 중"이다.
- **상용(proprietary) 라이선스**: 소스를 받더라도(또는 못 받더라도) 계약 조건대로 쓴다. 개발 좌석(seat)당, 제품(SKU)당, 출하량당 비용 구조가 회사마다 다르다. VxWorks, QNX, INTEGRITY, PikeOS, SafeRTOS, embOS가 여기다.

Don의 SSD 경험으로 말하면: 컨트롤러 펌웨어에 GPL 코드 한 조각이 섞이면 법무팀이 출하를 막는다. RTOS는 펌웨어 전체와 링크되므로 이 문제가 **가장 큰 규모로** 터지는 자리다.

### 1.2 거버넌스 — "누가 결정하나"

- **단일 벤더**: 회사가 소스와 로드맵을 소유한다. 의사결정이 빠르고 책임 소재가 분명하지만, 회사가 인수·전략 변경을 하면 제품의 운명이 바뀐다(§2의 ThreadX·µC/OS·Mbed OS 사례).
- **회사 주도 오픈소스**: 소스는 공개지만 한 회사가 유지보수와 릴리스를 쥔다. FreeRTOS(AWS)가 대표적이다.
- **재단(foundation)**: Linux Foundation, Eclipse Foundation, Apache Software Foundation 같은 중립 비영리 단체가 상표·인프라를 소유하고, 멤버 회사와 기술 운영 위원회(**TSC**, Technical Steering Committee)가 결정한다. Zephyr(LF), ThreadX(Eclipse), NuttX(ASF)가 여기다. 벤더 하나가 빠져도 프로젝트는 남는다.

### 1.3 안전 인증 — "증거를 살 수 있나"

- **IEC 61508**: 산업용 기능 안전의 모(母) 표준. 위험도를 **SIL 1~4**로 나눈다(4가 가장 엄격). 이미 있는 소프트웨어를 인증할 때 쓰는 경로(Route 3S 등)가 있다.
- **ISO 26262**: 자동차용. **ASIL A~D**(D가 가장 엄격).
- **DO-178C**: 항공 소프트웨어. **DAL A~E**(A가 가장 엄격). DO-178C는 "RTOS 제품"이 아니라 **항공기 시스템 단위로** 인증받는다. 그래서 RTOS 벤더는 "DAL A 인증 패키지(증거 자료)"를 판다.
- **pre-certified(사전 인증)**: RTOS 벤더가 인증 기관(TÜV SÜD, TÜV Rheinland, SGS-TÜV Saar 등)에게 **커널을 독립 컴포넌트로** 평가받아 인증서를 받아 둔 상태. 고객은 이 인증서와 **safety manual(안전 매뉴얼: "이 조건으로 써야 인증이 유효하다"는 사용 제약 목록)** 을 자기 시스템 인증에 가져다 쓴다.
- **SEooC(Safety Element out of Context)**: "어떤 차에 들어갈지 모르는 상태로 개발한 안전 요소". 범용 RTOS는 대부분 이 방식으로 인증한다. 그래서 safety manual의 **가정(assumptions of use)** 을 지키는 것이 고객의 일이 된다.

중요한 구분: **"인증받은 RTOS"는 버전·설정·컴파일러·CPU까지 고정된 특정 구성**이다. "ThreadX는 인증받았다"는 말은 "ThreadX 6.1.x의 특정 구성에 인증서가 있다"는 뜻이지, 오늘 trunk를 받아서 쓰면 인증품이라는 뜻이 아니다(§3.3).

### 1.4 생태계 — "내 일을 누가 이미 해 놨나"

커널은 전체 펌웨어의 몇 %에 불과하다(§7의 LOC 비교). 실제 일정은 **드라이버, 무선 스택, 파일시스템, 부트로더, 빌드·테스트 도구, 디버거의 RTOS 인식(RTOS-aware debugging)** 에서 결정된다. 생태계 크기는 보드·SoC 지원 수, 미들웨어 수, 벤더 SDK 채택, 기여자 수, 채용 시장으로 잰다.

### 1.5 수명 — LTS, EOL, 그리고 규제

- **LTS(Long-Term Support)**: 기능 추가 없이 보안·버그 수정만 오래 해 주는 릴리스. Zephyr LTS는 최소 5년(3.7 LTS: 2024-07-26 → EOL 2029-07-27), FreeRTOS LTS는 2년(202604 LTS: 2028-04-30까지).
- **EOL(End of Life)**: 지원 종료. Arm Mbed OS는 2026년 7월에 EOL이 되었다(§3.9).
- **EU CRA(Cyber Resilience Act)**: 디지털 요소가 있는 제품에 보안 업데이트·취약점 신고 의무를 지우는 EU 규정. 2026-09-11부터 신고 의무가 이미 시행 중이다(§9.3). RTOS 선택이 이제 **법적 의무 이행 능력**과 이어진다.

### 1.6 다섯 축 요약

| 축 | 묻는 질문 | 대표 차이 |
|---|---|---|
| 라이선스 | 내 소스 공개 의무? 비용? | MIT·Apache(자유) vs GPL(공개 의무·예외) vs 상용(비용) |
| 거버넌스 | 누가 로드맵을 정하나? | 단일 벤더 vs 회사 주도 오픈소스 vs 재단 |
| 인증 | SIL·ASIL·DAL 증거를 살 수 있나? | 상용 pre-certified vs 오픈소스 인증 진행 중 |
| 생태계 | 내 칩·스택·툴이 붙어 있나? | 보드 1,000+ (Zephyr) vs 포트 140+ (FreeRTOS) vs 특정 도메인 |
| 수명 | 몇 년 지원? CRA 대응? | LTS 2년 / 5년 / 상용 배포판 최대 15년 |

---

## 2. 40년 연표 — 시대마다 "누가 RTOS를 소유하나"가 바뀌었다

```svg
<svg viewBox="0 0 680 396" xmlns="http://www.w3.org/2000/svg"><text x="340" y="18" font-size="13" text-anchor="middle" font-weight="bold">RTOS 역사 연표 — 탄생과 거버넌스 사건</text><line x1="76" y1="32" x2="76" y2="368" stroke="currentColor" stroke-width="1.3"/><text x="66" y="45" font-size="12" text-anchor="end" font-weight="bold">1980</text><circle cx="76" cy="41" r="5" fill="#4a7bd0" fill-opacity="0.85"/><text x="88" y="45" font-size="12" text-anchor="start">QNX (Quantum Software Systems) 창업</text><text x="66" y="71" font-size="12" text-anchor="end" font-weight="bold">1987</text><circle cx="76" cy="67" r="5" fill="#4a7bd0" fill-opacity="0.85"/><text x="88" y="71" font-size="12" text-anchor="start">VxWorks 첫 출시 (Wind River)</text><text x="66" y="97" font-size="12" text-anchor="end" font-weight="bold">1988</text><circle cx="76" cy="93" r="5" fill="#3f9a6b" fill-opacity="0.85"/><text x="88" y="97" font-size="12" text-anchor="start">RTEMS 시작 (미 육군 미사일 사령부)</text><text x="66" y="123" font-size="12" text-anchor="end" font-weight="bold">1991</text><circle cx="76" cy="119" r="5" fill="#4a7bd0" fill-opacity="0.85"/><text x="88" y="123" font-size="12" text-anchor="start">µC/OS (Jean Labrosse)</text><text x="66" y="149" font-size="12" text-anchor="end" font-weight="bold">1992</text><circle cx="76" cy="145" r="5" fill="#4a7bd0" fill-opacity="0.85"/><text x="88" y="149" font-size="12" text-anchor="start">SEGGER 창업 · embOS</text><text x="66" y="175" font-size="12" text-anchor="end" font-weight="bold">1993</text><circle cx="76" cy="171" r="5" fill="#888" fill-opacity="0.85"/><text x="88" y="175" font-size="12" text-anchor="start">OSEK 시작 → ISO 17356</text><text x="66" y="201" font-size="12" text-anchor="end" font-weight="bold">1996</text><circle cx="76" cy="197" r="5" fill="#4a7bd0" fill-opacity="0.85"/><text x="88" y="201" font-size="12" text-anchor="start">Green Hills INTEGRITY</text><text x="66" y="227" font-size="12" text-anchor="end" font-weight="bold">1997</text><circle cx="76" cy="223" r="5" fill="#4a7bd0" fill-opacity="0.85"/><text x="88" y="227" font-size="12" text-anchor="start">ThreadX (Express Logic)</text><text x="66" y="253" font-size="12" text-anchor="end" font-weight="bold">2002</text><circle cx="76" cy="249" r="5" fill="#888" fill-opacity="0.85"/><text x="88" y="253" font-size="12" text-anchor="start">AUTOSAR 파트너십 출범</text><text x="66" y="279" font-size="12" text-anchor="end" font-weight="bold">2003</text><circle cx="76" cy="275" r="5" fill="#3f9a6b" fill-opacity="0.85"/><text x="88" y="279" font-size="12" text-anchor="start">FreeRTOS (Richard Barry)</text><text x="66" y="305" font-size="12" text-anchor="end" font-weight="bold">2006</text><circle cx="76" cy="301" r="5" fill="#3f9a6b" fill-opacity="0.85"/><text x="88" y="305" font-size="12" text-anchor="start">RT-Thread 시작</text><text x="66" y="331" font-size="12" text-anchor="end" font-weight="bold">2007</text><circle cx="76" cy="327" r="5" fill="#3f9a6b" fill-opacity="0.85"/><text x="88" y="331" font-size="12" text-anchor="start">NuttX · ChibiOS 공개 · SafeRTOS 인증</text><text x="66" y="357" font-size="12" text-anchor="end" font-weight="bold">2009</text><circle cx="76" cy="353" r="5" fill="#888" fill-opacity="0.85"/><text x="88" y="357" font-size="12" text-anchor="start">seL4 형식 검증 완료</text><line x1="411" y1="32" x2="411" y2="368" stroke="currentColor" stroke-width="1.3"/><text x="401" y="45" font-size="12" text-anchor="end" font-weight="bold">2010</text><circle cx="411" cy="41" r="5" fill="#4a7bd0" fill-opacity="0.85"/><text x="423" y="45" font-size="12" text-anchor="start">BlackBerry(RIM)가 QNX 인수</text><text x="401" y="71" font-size="12" text-anchor="end" font-weight="bold">2016</text><circle cx="411" cy="67" r="5" fill="#3f9a6b" fill-opacity="0.85"/><text x="423" y="71" font-size="12" text-anchor="start">Zephyr Project (LF) 출범</text><text x="401" y="97" font-size="12" text-anchor="end" font-weight="bold">2017</text><circle cx="411" cy="93" r="5" fill="#3f9a6b" fill-opacity="0.85"/><text x="423" y="97" font-size="12" text-anchor="start">Amazon이 FreeRTOS 인수 → MIT</text><text x="401" y="123" font-size="12" text-anchor="end" font-weight="bold">2018</text><circle cx="411" cy="119" r="5" fill="#888" fill-opacity="0.85"/><text x="423" y="123" font-size="12" text-anchor="start">ArduPilot 3.6 ChibiOS 채택</text><text x="401" y="149" font-size="12" text-anchor="end" font-weight="bold">2019</text><circle cx="411" cy="145" r="5" fill="#3f9a6b" fill-opacity="0.85"/><text x="423" y="149" font-size="12" text-anchor="start">MS가 ThreadX 인수 · NuttX → ASF</text><text x="401" y="175" font-size="12" text-anchor="end" font-weight="bold">2020</text><circle cx="411" cy="171" r="5" fill="#3f9a6b" fill-opacity="0.85"/><text x="423" y="175" font-size="12" text-anchor="start">µC/OS Apache-2.0 공개 · seL4 재단</text><text x="401" y="201" font-size="12" text-anchor="end" font-weight="bold">2022</text><circle cx="411" cy="197" r="5" fill="#3f9a6b" fill-opacity="0.85"/><text x="423" y="201" font-size="12" text-anchor="start">NuttX ASF 졸업 · Aptiv→Wind River</text><text x="401" y="227" font-size="12" text-anchor="end" font-weight="bold">2023</text><circle cx="411" cy="223" r="5" fill="#3f9a6b" fill-opacity="0.85"/><text x="423" y="227" font-size="12" text-anchor="start">ThreadX → Eclipse · FreeRTOS V11 SMP</text><text x="401" y="253" font-size="12" text-anchor="end" font-weight="bold">2024</text><circle cx="411" cy="249" r="5" fill="#e08a3c" fill-opacity="0.85"/><text x="423" y="253" font-size="12" text-anchor="start">Zephyr 3.7 LTS · EU CRA 발효</text><text x="401" y="279" font-size="12" text-anchor="end" font-weight="bold">2026-04</text><circle cx="411" cy="275" r="5" fill="#3f9a6b" fill-opacity="0.85"/><text x="423" y="279" font-size="12" text-anchor="start">Zephyr 4.4 · FreeRTOS 202604 LTS</text><text x="401" y="305" font-size="12" text-anchor="end" font-weight="bold">2026-07</text><circle cx="411" cy="301" r="5" fill="#d0564a" fill-opacity="0.85"/><text x="423" y="305" font-size="12" text-anchor="start">Arm Mbed OS 지원 종료</text><text x="401" y="331" font-size="12" text-anchor="end" font-weight="bold">2026-09</text><circle cx="411" cy="327" r="5" fill="#e08a3c" fill-opacity="0.85"/><text x="423" y="331" font-size="12" text-anchor="start">CRA 신고 의무 · Canonical Zephyr LTS</text><text x="401" y="357" font-size="12" text-anchor="end" font-weight="bold">2027-12</text><circle cx="411" cy="353" r="5" fill="#e08a3c" fill-opacity="0.85"/><text x="423" y="357" font-size="12" text-anchor="start">CRA 전면 적용 (예정)</text><circle cx="30" cy="382" r="5" fill="#4a7bd0" fill-opacity="0.85"/><text x="40" y="386" font-size="12" text-anchor="start">상용 RTOS</text><circle cx="128.8" cy="382" r="5" fill="#3f9a6b" fill-opacity="0.85"/><text x="139" y="386" font-size="12" text-anchor="start">오픈소스 RTOS</text><circle cx="251.6" cy="382" r="5" fill="#888" fill-opacity="0.85"/><text x="262" y="386" font-size="12" text-anchor="start">표준·연구</text><circle cx="346.56" cy="382" r="5" fill="#e08a3c" fill-opacity="0.85"/><text x="357" y="386" font-size="12" text-anchor="start">규제·LTS</text><circle cx="438.4" cy="382" r="5" fill="#d0564a" fill-opacity="0.85"/><text x="448" y="386" font-size="12" text-anchor="start">지원 종료</text></svg>
```

그림 2 — RTOS 역사 연표. 파랑은 상용 RTOS, 초록은 오픈소스 RTOS, 회색은 표준·연구, 주황은 규제·LTS, 빨강은 지원 종료. 1980~2009년(왼쪽)은 탄생의 시대, 2010~2027년(오른쪽)은 소유권·거버넌스·규제의 시대다.

연표를 다섯 시대로 나누면 흐름이 보인다.

### 2.1 1980~2000: 상용 RTOS의 시대

- **1980 QNX**(Quantum Software Systems, 캐나다)가 메시지 패싱 마이크로커널로 출발했다.
- **1987 VxWorks**(Wind River)가 나왔다. 이후 화성 로버들을 포함한 우주·국방 시스템의 대표 RTOS가 된다.
- **1988 RTEMS**는 미 육군 미사일 사령부를 위해 OAR Corporation이 개발을 시작했다. 상용이 아니라 **정부 과제 출신 오픈소스**라는 점이 특이하다(정확한 시작 연도는 "1988-2026" 저작권 표기와 2차 자료로 확인, 1차 문서의 날짜는 확인 못 함).
- **1991 µC/OS**: Jean Labrosse가 잡지 기사와 책으로 커널 소스를 공개했다. 이 "책으로 배우는 커널"이 수많은 펌웨어 엔지니어의 교과서가 된다(Wikipedia는 1991 설계로 기록. 1992로 쓰는 자료도 있다 — 정확한 연도는 확인 못 함).
- **1992 SEGGER**가 embOS를 팔기 위해 창업했다.
- **1993 OSEK**(독일 자동차 업계 표준 OS 규격)이 시작되어 나중에 ISO 17356이 되고, AUTOSAR Classic OS의 기반이 된다.
- **1996 INTEGRITY**(Green Hills)와 **1997 ThreadX**(Express Logic, William Lamie)가 나왔다. INTEGRITY 첫 출시 연도는 Green Hills 보도자료끼리 1996/1998이 엇갈린다 — 1996 전후로 본다.

이 시대의 RTOS는 **회사의 제품**이었다. 소스는 비싸게 팔거나 아예 주지 않았고, 대신 벤더가 인증·지원을 책임졌다.

### 2.2 2002~2010: 오픈소스 RTOS의 등장

- **2002 AUTOSAR** 파트너십이 출범했다(개발 계약 서명은 2003년 7월).
- **2003 FreeRTOS**(Richard Barry). "수정 GPL + 링크 예외"로 무료 배포하고, 상용 지원은 별도 회사가 파는 모델이었다.
- **2006 RT-Thread**(중국, Bernard Xiong).
- **2007 NuttX**(Gregory Nutt, BSD 라이선스로 공개), **ChibiOS**(Giovanni Di Sirio), 그리고 FreeRTOS의 기능 모델을 바탕으로 다시 만든 **SafeRTOS가 TÜV SÜD 인증**을 처음 받았다.
- **2009 seL4**: 범용 마이크로커널 최초로 기능적 정확성을 형식 증명했다(O7에서 다시 본다).
- **2010** BlackBerry(당시 RIM)가 QNX를 인수했다.

### 2.3 2016~2019: 클라우드·칩 회사가 RTOS를 산다

- **2016-02-17 Zephyr Project**가 Linux Foundation 산하에서 출범했다. 출발점은 Wind River가 기여한 **Rocket** 커널이다.
- **2017-11** Amazon이 FreeRTOS의 관리권(stewardship)을 넘겨받고 커널 V10.0.0부터 **MIT**로 바꿨다.
- **2018** ArduPilot 3.6이 STM32 비행 제어기에서 **ChibiOS**를 쓰기 시작했다. RT-Thread는 v3.1.1(2018-11-14)부터 **Apache-2.0**으로 바꿨다.
- **2019-04-18** Microsoft가 Express Logic을 인수해 ThreadX를 **Azure RTOS**로 이름 붙였다. 같은 해 12월 NuttX가 Apache 인큐베이터에 들어갔다.

이 시대의 동기는 분명하다: **클라우드 회사는 디바이스를 자기 클라우드에 붙이고 싶었다**. RTOS는 미끼였다.

### 2.4 2020~2023: 재단으로, 그리고 permissive 라이선스로

- **2020** Silicon Labs가 µC/OS를 **Apache-2.0**으로 공개했다(발표 2020-01-27, 시행 2020-02-28). 이후 Weston Embedded가 유지보수한다. 같은 해 4월 seL4 Foundation이 LF 산하에 섰다.
- **2022-11-17** NuttX가 Apache 최상위 프로젝트(TLP)가 되었다. 2022-12-23 Aptiv가 Wind River 인수를 마쳤다(약 35억 달러, 처음 합의한 43억 달러에서 조정).
- **2023-11** Microsoft가 ThreadX를 **Eclipse Foundation**에 기여했다. 첫 MIT 릴리스는 **Eclipse ThreadX 6.4.1(2024-02-29)** 이다(6.4.0은 아직 Azure RTOS 이름·Microsoft 라이선스).
- **2023-12-18** FreeRTOS V11.0.0이 SMP(대칭 멀티코어) 지원을 메인라인에 합쳤다.

여기서 패턴이 보인다: **상용이던 커널이 하나씩 permissive 라이선스 + 중립 재단으로 옮겨 간다**(ThreadX, µC/OS, NuttX). 커널 자체로는 돈을 벌기 어려워졌고, 돈은 인증 증거·지원·클라우드·칩 판매에서 번다.

### 2.5 2024~2027: LTS와 규제의 시대

- **2024-07-26** Zephyr 3.7 LTS. **2024-12-10** EU CRA 발효.
- **2026-04** Zephyr 4.4(2026-04-14, 연 2회 주기로 바뀐 첫 릴리스)와 FreeRTOS 202604 LTS(kernel 11.3.0, 2년 지원).
- **2026-07** Arm Mbed OS 지원 종료.
- **2026-09-11** CRA 신고 의무 시작. **2026-09-21** Canonical이 최대 15년 보안 유지보수를 파는 **Zephyr 26.04 LTS**를 "곧 나올 릴리스"로 발표했다.
- **2027-12-11** CRA 전면 적용.

지금은 "어느 커널이 빠른가"가 아니라 **"누가 10년 동안 보안 패치와 증거를 대 줄 것인가"** 의 시대다.

### 2.6 역사에서 얻는 실무 교훈

| 사건 | 교훈 | 실무 행동 |
|---|---|---|
| ThreadX: Express Logic → Microsoft → Eclipse | 소유자가 두 번 바뀌며 라이선스가 상용 → MIT로 | 계약서·라이선스 파일을 버전별로 보관 |
| ThreadX 인증서는 6.1.x에만 | 소유권이 바뀌어도 인증은 버전에 묶인다 | 인증 버전과 실제 쓰는 버전을 일치시킨다 |
| Mbed OS EOL (2026-07) | 대기업 RTOS도 접는다 | "벤더가 접으면?" 계획(포크 가능성, 이전 비용)을 선택 단계에서 적는다 |
| FreeRTOS GPL 예외 → MIT | 라이선스는 바뀔 수 있고, 보통 더 자유로운 쪽으로 | 옛 버전 소스의 라이선스 헤더는 옛 조건대로 |
| Zephyr: LF + 다수 멤버 | 재단 거버넌스는 단일 회사 리스크를 줄인다 | 멤버 구성(칩 벤더 다양성)을 본다 |

---

## 3. 오픈소스 RTOS 하나씩

각 RTOS를 같은 틀로 본다: **한 줄 정체성 → 출신·거버넌스 → 라이선스 → 2026 현황 → 강점 → 약점 → 대표 사용처 → 더 깊게(모듈 ID)**.

### 3.1 FreeRTOS — "어디에나 있는 작은 커널"

- **한 줄**: 커널은 파일 몇 개(tasks.c, queue.c, list.c, timers.c …)뿐인 작은 라이브러리형 RTOS. 가장 많은 CPU·컴파일러 조합에 포팅되어 있다.
- **출신·거버넌스**: 2003년 Richard Barry. 2017년 11월부터 **AWS가 관리**한다. 재단이 아니라 회사 주도 오픈소스다.
- **라이선스**: **MIT** (V10.0.0부터. 그 전은 수정 GPLv2 + 예외).
- **2026 현황**: 커널 **V11.3.2**(2026-10-16). LTS는 **202604 LTS**(2026-04-29 공개, kernel 11.3.0, 라이브러리 전부 2028-04-30까지 지원). 202604.01-LTS(2026-08-21)가 커널을 11.3.1로 올렸다. LTS 내용: MPU 리전을 커널이 덜 차지하도록 개선(`configTOTAL_MPU_REGIONS`가 물리 리전 수와 같지 않아도 됨), **coreMQTT v5.0.2**(MQTT v5.0 프로토콜 지원), **coreSNTP v2.0.0**(2038년 문제 대응). 이전 LTS 202406(kernel 11.1.0)은 2026-06-30에 지원이 끝났다.
- **강점**: 작고 단순해서 읽기 쉽다. 포트가 압도적으로 많다(예제 1). 거의 모든 칩 벤더 SDK가 FreeRTOS 예제를 준다. ESP-IDF는 FreeRTOS를 기반으로 한다. 채용 시장에서 가장 흔한 키워드다.
- **약점**: 커널만 준다. 드라이버 모델·디바이스 트리·빌드 시스템이 없어서 그 부분은 **벤더 HAL마다 제각각**이다. 안전 인증판은 FreeRTOS 자체가 아니라 **별도 상용 제품 SafeRTOS**다.
- **더 깊게**: G1~G7(FreeRTOS 실무), C(커널 내부는 FreeRTOS 소스로 설명).

#### 예제 1 — FreeRTOS 포트는 몇 개이고 어떤 CPU를 덮나

무엇을 확인하나: `portable/` 아래 `port.c` 또는 `portmacro.h`를 가진 디렉터리를 "포트 하나"로 세고, 컴파일러와 CPU 계열별로 묶는다.

```python
#!/usr/bin/env python3
# e1: FreeRTOS V11.3.2 -- count ports by compiler and CPU family
# a "port" = <compiler>/<target> dir that has port.c or portmacro.h
import os, re, collections
P = "/var/rtoslab/src/FreeRTOS-Kernel/portable"
ports = set()
for root, dirs, files in os.walk(P):
    rel = os.path.relpath(root, P)
    top = rel.split(os.sep)[0]
    if top in ("MemMang", "Common", "template", "ARMv8M"):   # ARMv8M = source of the CMxx copies
        continue
    if "port.c" in files or "portmacro.h" in files:
        rel = re.sub(r"/(non_secure|secure|r0p1|include|small|large_untested)$", "", rel)
        ports.add(rel)
ports = sorted(ports)

RULES = [  # (regex on target name, family)
    (r"ARM_CM|CM0|CM3|CM4|CM7|STAR_MC|RP2040|ARM_TFM", "Arm Cortex-M"),
    (r"ARM_CR|Cortex-R", "Arm Cortex-R"),
    (r"ARM_CA|AARCH64", "Arm Cortex-A / AArch64"),
    (r"ARM7|SAM7|SAM9|LPC2|STR7|STR9", "Arm7/9 (legacy)"),
    (r"RISC-V|CK802", "RISC-V / T-Head"),
    (r"Xtensa|ESP32", "Xtensa"),
    (r"ARC", "Synopsys ARC"),
    (r"RX|RL78|SH2A|78K0|V850|F1Kx|H8S", "Renesas"),
    (r"PIC|Cygnal", "Microchip PIC / 8051"),
    (r"AVR|ATMega|ATmega", "AVR"),
    (r"MSP430", "TI MSP430"),
    (r"MicroBlaze|NiosII|PPC4", "FPGA soft / PowerPC"),
    (r"Posix|MSVC", "Host simulator"),
]
def family(p):
    name = p.split(os.sep, 1)[-1]
    for rx, f in RULES:
        if re.search(rx, name):
            return f
    return "other (ColdFire, HCS12, x86, TriCore, ...)"

comp = collections.Counter(p.split(os.sep)[0] for p in ports)
fam = collections.Counter(family(p) for p in ports)
print(f"ports (compiler/target dirs): {len(ports)}")
print("by compiler:", ", ".join(f"{c}={n}" for c, n in comp.most_common(6)), ", ...")
print("by CPU family:")
for f, n in fam.most_common():
    print(f"  {f:<42}{n:>3}")
others = [p for p in ports if family(p).startswith("other")]
print("other =", " ".join(o.split('/')[-1] for o in others))
print("ARMv8-M cores in ARMv8M/:", " ".join(sorted(
    d for d in os.listdir(os.path.join(P, "ARMv8M/non_secure/portable/GCC")))))
```

```sh
limactl shell elinux -- bash -lc 'python3 -I /Users/donh/.rtos_scratch/A6/e1_freertos_ports.py'
```

```text
ports (compiler/target dirs): 143
by compiler: GCC=54, IAR=42, ThirdParty=8, RVDS=7, Renesas=6, MPLAB=5 , ...
by CPU family:
  Arm Cortex-M                               49
  Renesas                                    21
  other (ColdFire, HCS12, x86, TriCore, ...) 16
  Arm7/9 (legacy)                            12
  AVR                                         7
  Microchip PIC / 8051                        7
  Arm Cortex-R                                6
  Arm Cortex-A / AArch64                      6
  FPGA soft / PowerPC                         6
  TI MSP430                                   5
  RISC-V / T-Head                             3
  Host simulator                              2
  Synopsys ARC                                2
  Xtensa                                      1
other = Flsh186 PC ColdFire_V1 ColdFire_V2 HCS12 CORTUS_APS3 ColdFire_V2 HCS12 IA32_flat TriCore_1782 Tern_EE MB91460 MB96340 XCOREAI Flsh186 PC
ARMv8-M cores in ARMv8M/: ARM_CM23 ARM_CM23_NTZ ARM_CM33 ARM_CM33_NTZ ARM_CM35P ARM_CM52 ARM_CM55 ARM_CM85 ARM_STAR_MC3
```

출력에서 볼 것: 컴파일러×타깃 조합이 **143개**다. Cortex-M이 49개로 가장 많지만, Renesas(RX·RL78·SH2A)만 21개, 그 밖에 ColdFire·HCS12·TriCore·8051 같은 오래된 CPU까지 있다. 이것이 "FreeRTOS는 어디에나 있다"의 실체다 — 20년 동안 포트를 **지우지 않고 쌓았다**. 반대로 RISC-V는 3개뿐인데, RISC-V 포트는 하나의 `GCC/RISC-V` 포트가 칩별 확장을 헤더로 받는 구조라서 디렉터리 수가 적다(숫자를 그대로 "지원 수준"으로 읽으면 안 된다는 예).

함정: 한 디렉터리 안의 포트 품질은 제각각이다. 공식 데모와 CI가 도는 것은 Cortex-M·RISC-V·POSIX 같은 주력 포트이고, 16비트 DOS 같은 것은 "남아 있을 뿐"이다. 새 제품에서는 **공식 지원(AWS 유지) 포트인지, ThirdParty/커뮤니티 포트인지**를 먼저 본다.

### 3.2 Zephyr — "RTOS 커널 + Linux식 생태계"

- **한 줄**: 커널 + 드라이버 모델 + 디바이스 트리 + Kconfig + west 빌드 + 무선 스택 + 테스트 프레임워크를 한 저장소로 묶은 **플랫폼**. 칩 벤더들이 공동 투자한다.
- **출신·거버넌스**: 2016-02-17 Linux Foundation 산하로 출범. Wind River의 Rocket 커널이 출발점. 멤버 회사(Platinum·Silver)와 TSC가 결정한다.
- **라이선스**: **Apache-2.0** (예제 3에서 파일의 99%가 Apache-2.0 SPDX를 가진 것을 확인).
- **2026 현황**: **4.4.0 = 2026-04-14**. "930명 넘는 개인의 기여"로 발표되었고, 4.4가 **연 2회(4월·10월) 주기의 첫 릴리스**다. 4.4.1(2026-06-10), 4.4.2(2026-08-07, CVE 수정)가 나왔다. **4.5.0은 2026-10-25 현재 아직 rc3**(rc1 09-30, rc2 10-15, rc3 10-22)이다. LTS는 **3.7(2024-07-26, EOL 2029-07-27)**, 다음 LTS는 **4.6 "LTS4"(2027년 4월 예정)**. 공식 문서상 LTS는 "최소 5년" 유지된다.
- **안전 인증**: 2024년에 IEC 61508 인증을 위한 **서면 concept approval**을 받았고, Route 3S 경로로 **SIL 3 / SC 3**을 목표로 작업 중이다. 인증 범위는 커널 서비스·초기화·API를 포함한 **약 15,000줄**(전체 240만 줄+ 중)로 잡혀 있다. **아직 인증 완료가 아니다**(홈페이지 표현: "coming soon"). 인증 기관 이름(TÜV 여부)은 확인 못 함 — 자료는 "certifying authority"라고만 쓴다.
- **강점**: 보드 1,000개 이상(예제 2), 칩 벤더 다수가 공식 지원, BLE·Thread·Matter·Wi-Fi·USB·파일시스템·MCUboot가 한 트리에, Twister·native_sim으로 CI가 쉽다. Nordic의 nRF Connect SDK가 Zephyr 기반이다. 상용 장기 지원(Canonical, 최대 15년)까지 생겼다.
- **약점**: 배울 것이 많다(west·CMake·Kconfig·DT). 빌드 시점에 모든 것이 정해지는 구조라 처음엔 "왜 이 매크로가 안 풀리지?"로 하루를 쓴다. 서브시스템을 많이 켜면 footprint가 빠르게 커진다(최소 구성은 FreeRTOS와 비슷하다 — 예제 7).
- **대표 사용처**: Chromebook 임베디드 컨트롤러(EC, 2021년 이후 새 설계), Oticon More 보청기(Polaris 칩셋의 BLE), Nordic nRF 계열 제품 전반.
- **더 깊게**: H1~H10 전체.

#### 예제 2 — Zephyr는 정말 보드가 1,000개인가

무엇을 확인하나: Hardware Model v2의 `boards/<vendor>/<board>/board.yml`을 파싱해 보드·벤더·SoC·아키텍처 수를 직접 센다.

```python
#!/usr/bin/env python3
# e2: Zephyr v4.4.2 tree -- count boards per vendor, per arch, and SoC families
import os, sys, collections, yaml
Z = "/var/rtoslab/zephyrproject/zephyr"
boards = []            # (vendor, board_name, [socs])
for root, dirs, files in os.walk(os.path.join(Z, "boards")):
    if "board.yml" in files:
        vendor = os.path.relpath(root, os.path.join(Z, "boards")).split(os.sep)[0]
        doc = yaml.safe_load(open(os.path.join(root, "board.yml")))
        items = doc.get("boards") or [doc["board"]]
        for b in items:
            socs = [s["name"] for s in b.get("socs", [])]
            boards.append((vendor, b["name"], socs))

per_vendor = collections.Counter(v for v, _, _ in boards)
print(f"boards total      : {len(boards)}  (board.yml entries)")
print(f"vendors           : {len(per_vendor)}")
print("top 12 vendors by board count:")
for v, n in per_vendor.most_common(12):
    print(f"  {v:<14}{n:>4}")
rest = sum(n for v, n in per_vendor.items()) - sum(n for _, n in per_vendor.most_common(12))
print(f"  {'(others)':<14}{rest:>4}")

# SoC definitions: soc/<vendor>/.../soc.yml lists families/series/socs
nsoc = 0
for root, dirs, files in os.walk(os.path.join(Z, "soc")):
    if "soc.yml" in files:
        doc = yaml.safe_load(open(os.path.join(root, "soc.yml")))
        def walk(d):
            global nsoc
            if isinstance(d, dict):
                if "socs" in d:
                    nsoc += len(d["socs"])
                for k in ("family", "series"):
                    for x in d.get(k, []) or []:
                        walk(x)
        walk(doc)
print(f"SoCs in soc.yml   : {nsoc}")
archs = sorted(d for d in os.listdir(os.path.join(Z, "arch"))
               if os.path.isdir(os.path.join(Z, "arch", d)) and d != "common")
print(f"CPU archs (arch/) : {len(archs)} -> {' '.join(archs)}")
```

```sh
limactl shell elinux -- bash -lc 'source /var/rtoslab/zephyr-venv/bin/activate;
  python3 -I /Users/donh/.rtos_scratch/A6/e2_zephyr_boards.py'
```

```text
boards total      : 1014  (board.yml entries)
vendors           : 160
top 12 vendors by board count:
  st             136
  nxp             84
  renesas         62
  silabs          49
  adafruit        31
  adi             26
  weact           22
  nordic          22
  ezurio          21
  qemu            20
  microchip       20
  intel           17
  (others)       504
SoCs in soc.yml   : 1505
CPU archs (arch/) : 11 -> arc arm arm64 mips openrisc posix riscv rx sparc x86 xtensa
```

출력에서 볼 것: v4.4.2 트리에 보드 **1,014개, 벤더 160곳**, `soc.yml`에 정의된 SoC **1,505개**, CPU 아키텍처 **11개**다. 홈페이지의 "1,000개 이상"이 소스로 확인된다. ST(136), NXP(84), Renesas(62), Silicon Labs(49)가 상위권 — 즉 **칩 벤더가 직접 보드 지원을 넣는다**. 상위 12곳을 뺀 나머지 148곳이 504개를 가지고 있는 "긴 꼬리"가 진짜 생태계 지표다.

```svg
<svg viewBox="0 0 640 332" xmlns="http://www.w3.org/2000/svg"><text x="320" y="18" font-size="13" text-anchor="middle" font-weight="bold">Zephyr v4.4.2 보드 수 상위 12개 벤더 (전체 1,014개 · 160개 벤더, 예제 2)</text><text x="82" y="49" font-size="12" text-anchor="end">st</text><rect x="90" y="36" width="457" height="16" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="552" y="49" font-size="12" text-anchor="start">136</text><text x="82" y="71" font-size="12" text-anchor="end">nxp</text><rect x="90" y="58" width="282" height="16" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="377" y="71" font-size="12" text-anchor="start">84</text><text x="82" y="93" font-size="12" text-anchor="end">renesas</text><rect x="90" y="80" width="208" height="16" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="303" y="93" font-size="12" text-anchor="start">62</text><text x="82" y="115" font-size="12" text-anchor="end">silabs</text><rect x="90" y="102" width="164" height="16" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="260" y="115" font-size="12" text-anchor="start">49</text><text x="82" y="137" font-size="12" text-anchor="end">adafruit</text><rect x="90" y="124" width="104" height="16" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="199" y="137" font-size="12" text-anchor="start">31</text><text x="82" y="159" font-size="12" text-anchor="end">adi</text><rect x="90" y="146" width="87" height="16" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="182" y="159" font-size="12" text-anchor="start">26</text><text x="82" y="181" font-size="12" text-anchor="end">weact</text><rect x="90" y="168" width="74" height="16" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="169" y="181" font-size="12" text-anchor="start">22</text><text x="82" y="203" font-size="12" text-anchor="end">nordic</text><rect x="90" y="190" width="74" height="16" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="169" y="203" font-size="12" text-anchor="start">22</text><text x="82" y="225" font-size="12" text-anchor="end">ezurio</text><rect x="90" y="212" width="70" height="16" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="166" y="225" font-size="12" text-anchor="start">21</text><text x="82" y="247" font-size="12" text-anchor="end">qemu</text><rect x="90" y="234" width="67" height="16" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="162" y="247" font-size="12" text-anchor="start">20</text><text x="82" y="269" font-size="12" text-anchor="end">microchip</text><rect x="90" y="256" width="67" height="16" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="162" y="269" font-size="12" text-anchor="start">20</text><text x="82" y="291" font-size="12" text-anchor="end">intel</text><rect x="90" y="278" width="57" height="16" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="152" y="291" font-size="12" text-anchor="start">17</text><line x1="90" y1="302" x2="560" y2="302" stroke="currentColor"/><text x="320" y="322" font-size="12" text-anchor="middle">나머지 148개 벤더가 504개 — 긴 꼬리가 생태계 크기의 진짜 지표</text></svg>
```

그림 3 — Zephyr v4.4.2 보드 수 상위 12개 벤더(예제 2의 출력). 초록 막대는 각 벤더가 `boards/<vendor>/` 아래 둔 보드 정의 수.

함정: 보드 수는 "그 보드에서 부팅한다"는 뜻이지 "모든 주변장치 드라이버가 있다"는 뜻이 아니다. 내 칩의 **필요한 드라이버(예: 특정 ADC, CAN-FD, USB 장치 클래스)** 가 `drivers/` 아래에 있는지 따로 확인한다(H5).

### 3.3 Eclipse ThreadX — "인증 유산을 가진 MIT 커널"

- **한 줄**: preemption-threshold 같은 독특한 기능을 가진 작고 빠른 상용 출신 커널. NetX Duo(TCP/IP), FileX, USBX, GUIX가 한 세트.
- **출신·거버넌스**: 1997년 Express Logic(William Lamie). 2019-04 Microsoft 인수(Azure RTOS). 2023-11 Eclipse Foundation 기여. 2024-10-08 **ThreadX Alliance**(멤버가 안전 인증 자료를 라이선스받는 회원 프로그램)를 시작했다.
- **라이선스**: **MIT** (6.4.1부터). 예제 3에서 커널(`common/`) 197개 파일 전부가 MIT SPDX를 가진 것을 확인했다.
- **2026 현황**: 최신 릴리스는 v6.5.2.202603(2026-10-02). 클론한 trunk는 6.5.2.
- **안전 인증 — 정확히 구분해야 한다**: Microsoft 시절 SGS-TÜV Saar가 IEC 61508 SIL 4, IEC 62304 Class C, ISO 26262 ASIL D, EN 50128 인증을 했다. 그 인증서는 **6.1.x 계열**(커널 6.1.1, SMP 6.1.3, NetX Duo 6.1.9 등)에 대한 것이고 2024년 10월 Eclipse Foundation으로 이전되었다. **6.4.x는 아직 인증 전**이다. 프로젝트 리드는 2026-04-15 메일링 리스트에 "올해 v6.4.1 인증 일정대로 진행 중"이라고 썼다. 즉 2026-10-25 현재 "MIT이면서 인증된 최신 ThreadX"는 아직 없다.
- **강점**: 작은 footprint, 빠른 응답, 인증 이력, 같은 회사가 만든 미들웨어 세트, "120억 대 이상 배포"(프로젝트 자체 주장).
- **약점**: 재단 이관 후 커뮤니티가 아직 작다(GitHub contributors 57명, 예제 5). 최신 버전(6.4.x 이후)의 인증서가 아직 없다.
- **대표 사용처**: 프로젝트가 밝힌 "120억 대"가 대표 수치다. 개별 제품명은 이 노트에서 따로 검증하지 않았다.
- **더 깊게**: I2.

### 3.4 Apache NuttX — "MCU 위의 작은 Unix"

- **한 줄**: POSIX·ANSI 표준을 최대한 따르는 RTOS. `/dev` 장치 파일, VFS, 셸(NSH), 프로세스 같은 Unix 개념을 MCU에서 제공한다.
- **출신·거버넌스**: 2007년 2월 Gregory Nutt(BSD 라이선스). 2019-12-09 Apache 인큐베이터, **2022-11-17 Apache 최상위 프로젝트**.
- **라이선스**: **Apache-2.0** (예제 3: SPDX 90% + Apache 표준 헤더 5%).
- **2026 현황**: **13.1.0(2026-10-04)**. 12.13.0(2026-04-02), 13.0.0(2026-07-12), 13.0.1(2026-09-15) — 최근에는 약 3개월마다 기능 릴리스.
- **강점**: Linux 코드를 거의 그대로 옮길 수 있다. flat / protected / kernel 세 가지 빌드 모드(예제 9)로 MPU·MMU 격리를 고를 수 있다. **PX4 오토파일럿의 주 RTOS**다.
- **약점**: POSIX 계층이 두꺼워 아주 작은 MCU에는 무겁다. 문서가 Zephyr만큼 정돈되지 않았다.
- **대표 사용처**: PX4 비행 제어기, Sony Spresense SDK, Xiaomi Vela(오픈 버전 openvela가 NuttX 기반). NuttX 홈페이지의 "Trusted by" 목록에는 Sony, Xiaomi, Samsung, Google 등이 있다.
- **더 깊게**: I1, I6.

### 3.5 RT-Thread — "중국 생태계의 플랫폼"

- **한 줄**: 커널 + 컴포넌트 + 패키지 관리자를 갖춘 중국발 RTOS. 작은 MCU용 Nano와 MMU용 RT-Smart(유저 모드 프로세스)를 함께 가진다.
- **출신·거버넌스**: 2006년 Bernard Xiong. 동명의 회사(RT-Thread)가 주도한다.
- **라이선스**: **Apache-2.0** (v3.1.1, 2018-11-14부터).
- **2026 현황**: 최신 릴리스 v5.3.0(2026-09-10). trunk는 5.4.0 개발 중(예제 4). 2025년 중반 보도에 따르면 설치 기반 **25억 대 이상**(제3자 보도 기준).
- **강점**: 중국 MCU(GigaDevice, WCH, 각종 RISC-V)와 보드 지원, 패키지 생태계, 국산화 수요.
- **약점**: 영어권 자료가 적고, 서구 회사 채용에서 키워드로 거의 안 나온다.
- **더 깊게**: I3.

### 3.6 ChibiOS — "ArduPilot의 OS"

- **한 줄**: 작은 커널 두 개(RT: 완전한 RTOS, NIL: 초소형)와 잘 다듬은 STM32 HAL을 가진 RTOS.
- **출신·거버넌스**: 2007년 Giovanni Di Sirio. 저자 중심 프로젝트.
- **라이선스**: RT·NIL 커널은 **GPL3 또는 상용**(무료 상용판은 500코어 상한), HAL은 **Apache-2.0**. 예제 3에서 `os/rt`의 49개 파일 중 48개가 GPL 헤더를 가진 것을 확인했다(SPDX 태그는 없음).
- **2026 현황**: GitHub 저장소는 미러다. trunk의 `ch.h`는 커널 8.0.0(개발판).
- **강점**: STM32에서의 성숙도, 작은 크기, 빠른 HAL.
- **약점**: GPL3 — 제품에 쓰려면 상용 라이선스나 예외가 필요하다(ArduPilot처럼 앱도 GPL인 프로젝트는 문제없다). 기여자 수가 적다.
- **대표 사용처**: **ArduPilot**(Copter 3.6, 2018부터 STM32 보드에서 ChibiOS, HAL 이름 `AP_HAL_ChibiOS`).
- **더 깊게**: I3, Q3.

### 3.7 RTEMS — "우주로 가는 오픈소스 RTOS"

- **한 줄**: POSIX·Classic API를 가진, SMP 스케줄러가 풍부한 RTOS. 방사선 내성 프로세서(LEON, GR740, ColdFire 등)에서 오래 검증되었다.
- **출신·거버넌스**: 1988년 OAR Corporation이 미 육군 미사일 사령부를 위해 개발. 지금은 커뮤니티 프로젝트(OAR가 호스팅).
- **라이선스**: **GPL-2.0 + RTEMS 예외에서 BSD-2-Clause로 이전 중**. 2018년 OAR가 자기 기여분의 재라이선스를 허락했다. 예제 3에서 `cpukit/` 파일의 85%가 이미 BSD-2-Clause SPDX다.
- **2026 현황**: **6.2(2025-12-20 발표)**, 6.1(2025-01-22). 2022년 ESA 우주 도메인 qualification 소식이 있다.
- **대표 사용처**: NASA MMS(주 비행 컴퓨터, RTEMS 4.10 on 방사선 내성 ColdFire), Solar Dynamics Observatory(ColdFire 5개), MRO의 Electra 무선 장비, Nancy Grace Roman 망원경(GR740 3개). Parker Solar Probe·Solar Orbiter에 쓰였다는 주장은 확인 못 함.
- **강점**: SMP·EDF 스케줄러(예제 9), 우주 도메인 이력, 자격 인정 데이터 패키지.
- **약점**: 일반 MCU 생태계(무선 스택, 칩 벤더 지원)는 약하다.
- **더 깊게**: I3, Q(우주).

### 3.8 µC/OS — "교과서 커널의 두 번째 인생"

- **한 줄**: Labrosse의 책으로 유명한 커널. µC/OS-II, µC/OS-III, 그리고 TCP/IP·파일시스템·USB 모듈.
- **출신·거버넌스**: 1991년 Jean Labrosse → Micrium → **Silicon Labs가 2016-10 인수** → **2020-02-28 Apache-2.0 공개** → 지금은 Micrium 출신들이 만든 **Weston Embedded Solutions**가 유지보수하고, 상용판을 **Cesium RTOS**로 판다.
- **라이선스**: **Apache-2.0** (예제 3: `uC-OS3` 133개 파일 100%).
- **인증 유산**: IEC 61508 SIL 3·IEC 62304 Class C·EN 50128용 Cert-Kit(TÜV 형식 승인)이 있었다. DO-178 Level A는 제3자 검증 패키지로 제공되었고, "µC/OS 자체의 DO-178B Level A 인증서"는 확인 못 함(DO-178은 원래 시스템 단위 인증이다).
- **강점**: 코드가 깔끔하고 책이 있다 — C·D 모듈 공부용으로 최고.
- **약점**: 커뮤니티 활동이 작다(trunk 마지막 커밋 2026-07-15).
- **더 깊게**: I3, C1~C4(교재로 활용).

### 3.9 Mbed OS — "접힌 플랫폼"에서 배우기

Arm은 2024-07-09 공지에서 **"Mbed 플랫폼과 OS는 2026년 7월에 EOL"** 이라고 밝혔다. 지금 os.mbed.com은 GitHub 아카이브로 리다이렉트된다. Mbed OS는 Apache-2.0 오픈소스였고 Arm이라는 거대 회사가 주도했지만, **회사 전략이 바뀌자 끝났다**. 소스는 남지만 보안 패치·툴·온라인 컴파일러는 사라진다.

실무 교훈: Mbed 기반 제품을 가진 회사는 지금 **Zephyr 등으로 이전**하거나 **자체 포크를 유지**하는 비용을 치르고 있다. CRA 시대에는 "포크를 유지한다" = "취약점 대응 책임을 우리가 진다"다. RTOS 선택 문서에 **"이 RTOS가 접히면 우리는 무엇을 하나"** 한 줄을 반드시 쓴다.

### 3.10 Rust 계열 — Embassy · RTIC · Hubris · Tock (짧게, I 모듈이 본편)

| 이름 | 한 줄 | 출신 · 라이선스 | 상태 (확인한 것) |
|---|---|---|---|
| Embassy | async/await 실행기 — 태스크가 컴파일 시점 상태 머신, 힙 없음 | 커뮤니티, MIT 또는 Apache-2.0 | 저장소 2020-09 생성, GitHub stars 1만+ (예제 5) |
| RTIC | 하드웨어 인터럽트 우선순위로 SRP 스케줄링, 데드락 없음 | 커뮤니티, MIT 또는 Apache-2.0 | 2.0.0 = 2023-05-31, 2.4.0 = 2026-10-21 |
| Hubris | 정적 태스크·메시지 패싱·태스크 재시작 | Oxide Computer, MPL-2.0 | 2021년 말 공개 (정확한 날짜는 확인 못 함) |
| Tock | capsule + 프로세스 격리, 보안 지향 | 대학 연구(Stanford 등), Apache-2.0 또는 MIT | 2.3.0 = 2026-10-08 |

인증 쪽 신호: **Ferrocene**(Ferrous Systems의 Rust 컴파일러)이 2023년 TÜV SÜD로부터 ISO 26262 ASIL D·IEC 61508 인증을 받았다(2023 자료는 SIL 4, 2026-09 보도자료는 SIL 3로 표기 — 현재 등급은 확인 못 함). 2025년 12월에는 core 라이브러리 일부가 IEC 61508 SIL 2 인증을 받았다. Zephyr도 `zephyr-lang-rust` 모듈로 Rust 앱을 지원한다(v4.4.2 문서 `doc/develop/languages/rust`). 자세한 내용은 I7~I11.

---

## 4. 상용 · 안전 인증 RTOS

상용 RTOS를 사는 이유는 거의 항상 둘 중 하나다: **인증 증거**가 필요하거나, **벤더가 책임지는 지원**이 필요하다. 아래 인증 정보는 대부분 **벤더 발표 기준**이다(인증서 원문·범위는 공개되지 않는 경우가 많다).

### 4.1 SafeRTOS (WITTENSTEIN high integrity systems)

- FreeRTOS의 **기능 모델(functional model)** 을 HAZOP 분석한 뒤 **처음부터 다시 만든** 별도 제품. API가 FreeRTOS와 비슷해서 "FreeRTOS로 개발 → SafeRTOS로 인증" 경로가 생긴다.
- TÜV SÜD가 **IEC 61508-3 SIL 3, ISO 26262 ASIL D** 사전 인증. IEC 62304용 패키지도 있다. 첫 인증은 2007년.
- DO-178C는 "DAL A까지 지원하도록 설계"라고만 하고 **인증서를 주장하지 않는다**.
- 많은 칩 벤더(TI 등)가 ROM이나 SDK로 SafeRTOS를 제공한다.

### 4.2 VxWorks (Wind River)

- 1987년 첫 출시. 소유권: Intel(2009) → TPG(2018) → **Aptiv(2022-12-23 인수 완료, 약 35억 달러)**. 2025년 Aptiv가 Wind River를 매각한다는 공식 발표는 확인 못 함(분사 대상은 EDS 사업부). 2025년 3분기 보고서에서 Wind River 영업권 손상 테스트 언급이 있었다.
- **화성 로버**: Wind River 발표에 따르면 Curiosity는 BAE RAD750에서 VxWorks를 돌리고, Perseverance는 "VxWorks로 화성에서 도는 네 번째 로버"다. 단 **Ingenuity 헬리콥터는 VxWorks가 아니라** Snapdragon 위 Linux + JPL F´ 프레임워크다(2차 자료). NASA 1차 문서로는 확인 못 함 — Wind River 자료 기준.
- **VxWorks Cert Edition**: DO-178C DAL A, IEC 61508 SIL 3, ISO 26262 ASIL D, IEC 62304 Class C 증거. **VxWorks 653**: ARINC 653 파티셔닝(항공 IMA).
- 최신: VxWorks 7 25.09(2025-08). 2026-07 Kyndryl과의 Wind River 소프트웨어 공급 제휴 발표.

### 4.3 QNX (BlackBerry)

- 1980년 Quantum Software Systems. Harman(2004) → **BlackBerry(2010)**.
- **마이크로커널 + 메시지 패싱**: 드라이버·파일시스템이 유저 공간 프로세스로 돈다. 하나가 죽어도 재시작할 수 있다. 엄밀히는 MCU RTOS가 아니라 **MMU가 있는 Cortex-A급**에서 도는 POSIX RTOS다.
- **QNX SDP 8.0** GA는 **2024-01-08**(CES). **QNX OS for Safety 8.0**은 TÜV Rheinland 인증: ISO 26262 ASIL D, IEC 61508 SIL 3, IEC 62304 Class C, ISO/SAE 21434.
- BlackBerry FY2026 연차 보고서(2026-02-28 회계연도 종료): **2억 7,500만 대 이상의 차량**, 상위 10개 OEM 포함 45개 이상 자동차 회사.
- 2025~2026: QNX Cabin(디지털 콕핏 레퍼런스, CES 2025), QNX General Embedded Development Platform(Embedded World 2026, 휴머노이드 로봇 데모), 비상업용 무료 접근 "QNX Everywhere".

### 4.4 INTEGRITY (Green Hills Software)

- 1996년 전후 출시. **분리 커널(separation kernel)**: 파티션 사이에 시간·공간 격리를 보장하는 커널.
- **INTEGRITY-178B**가 2008년 NSA 분리 커널 보호 프로파일 기준 **Common Criteria EAL 6+** 를 받았다. **INTEGRITY-178 tuMP**는 DO-178B/C DAL A 시스템에 쓰였고, 멀티코어 DO-178C / CAST-32A 인증의 첫 RTOS라고 주장한다.
- 국방·항공(F-35 같은 이름이 자주 거론되지만 이 노트에서는 확인하지 않았다).

### 4.5 PikeOS (SYSGO, Thales 자회사)

- SYSGO는 2012년부터 Thales 자회사. PikeOS는 **분리 커널 기반 Type 1 하이퍼바이저**로, 파티션 안에 RTOS·Linux·ARINC 653 APEX 게스트를 올린다.
- PikeOS 5.1.3 TÜV SÜD 사전 인증: **DO-178C DAL A, ISO 26262 ASIL D, IEC 61508 SIL 3**(SIL 4가 아니다), EN 50716 SIL 4(철도), ECSS Cat. A(우주). 분리 커널은 Common Criteria EAL 5+(BSI).
- 혼합 중요도(mixed criticality, O6)의 대표 솔루션.

### 4.6 embOS (SEGGER)

- SEGGER는 1992년 embOS를 팔기 위해 창업했다. J-Link·SystemView·Ozone을 만드는 그 회사다.
- **embOS-Safe**: ISO 26262 ASIL D, IEC 62304 Class C, IEC 61508 SIL 3(TÜV SÜD).
- **embOS-Ultra**(2021-10-28 발표): 주기적 tick 없이 **사이클 단위 타이밍**. embOS-Ultra-MPU(2025-03-18).
- 비상업·평가·교육용은 무료("SEGGER Friendly License"), 상용은 1회 비용·로열티 없음.

### 4.7 Micrium 유산과 "인증 키트" 비즈니스

µC/OS 시절의 Cert-Kit(§3.8)처럼, **오픈소스 커널 + 별도 판매 인증 패키지** 모델은 지금 Zephyr·ThreadX가 가려는 길이다. ThreadX Alliance가 바로 "커널은 MIT, 안전 증거는 회원 라이선스"다.

### 4.8 AUTOSAR Classic OS — 자동차 ECU의 공용어 (짧게, I5가 본편)

- **OSEK/VDX**(1993~, ISO 17356)를 기반으로 한 **정적 구성** RTOS 규격. 태스크·알람·카운터·스케줄 테이블을 빌드 전에 ARXML로 다 정한다.
- **Scalability Class**: SC1(OSEK + 스케줄 테이블), SC2(+ 타이밍 보호), SC3(+ 메모리 보호), SC4(둘 다).
- 규격이므로 구현은 벤더 제품이다: Vector MICROSAR OS(최신 1차 제품 페이지는 확인 못 함), ETAS RTA-OS, Elektrobit EB tresos AutoCore OS(Safety OS ASIL D).
- Rivian 같은 회사의 차량 ECU 팀은 "RTOS"보다 "AUTOSAR"라는 말을 더 자주 쓴다. 존 컨트롤러(zone controller)에서도 AUTOSAR Classic이 기본값이다.

### 4.9 seL4 — 증명된 마이크로커널 (참고)

2009년 SOSP 논문으로 C 구현의 기능적 정확성 증명을 마쳤다. 2020년 4월 seL4 Foundation이 LF 산하에 섰다. RTOS라기보다 **보안 분리 커널**이고, 국방(DARPA HACMS 등)과 고보안 시스템에서 쓰인다. 자세한 것은 O7.

### 4.10 인증 현황 한 장 요약

| 제품 | IEC 61508 | ISO 26262 | DO-178C | 인증 기관 · 근거 | 확인 수준 |
|---|---|---|---|---|---|
| SafeRTOS | SIL 3 | ASIL D | 인증 주장 없음 (설계 지원) | TÜV SÜD | 벤더 페이지 |
| QNX OS for Safety 8.0 | SIL 3 | ASIL D | — | TÜV Rheinland | 벤더 페이지 |
| VxWorks Cert Edition | SIL 3 | ASIL D | DAL A 증거 | Wind River 자료 | 벤더 페이지 |
| INTEGRITY-178 tuMP | — | — | DAL A 시스템 | Green Hills | 벤더 페이지 |
| PikeOS 5.1.3 | SIL 3 | ASIL D | DAL A | TÜV SÜD | 벤더 페이지 |
| embOS-Safe | SIL 3 | ASIL D | — | TÜV SÜD | 벤더 페이지 |
| ThreadX 6.1.x (MS 시절) | SIL 4 | ASIL D | — | SGS-TÜV Saar | Eclipse 메일링 리스트 |
| ThreadX 6.4.x (Eclipse) | 진행 중 | 진행 중 | — | 2026 목표 | 프로젝트 리드 메일 |
| Zephyr | SIL 3 목표 (concept approval 2024) | 옵션 | — | 기관명 확인 못 함 | LF 보도자료·문서 |
| FreeRTOS | 없음 (SafeRTOS로) | 없음 | — | — | — |

이 표를 읽는 법: "SIL 3 인증 RTOS를 썼다"고 해서 제품이 SIL 3가 되는 것이 아니다. 인증된 **구성**(버전·컴파일러·옵션)을 safety manual대로 쓰고, 그 위에 내 시스템의 안전 분석을 쌓아야 한다(O1, O2).

---

## 5. 라이선스 · 거버넌스 매트릭스

### 5.1 한 장 표

| RTOS | 라이선스 | 거버넌스 | 최신 (2026-10-25) | 장기 지원 |
|---|---|---|---|---|
| FreeRTOS | MIT | AWS (회사 주도) | 커널 V11.3.2 (2026-10-16) | LTS 2년 (202604 LTS → 2028-04-30), 유료 연장 플랜 |
| Zephyr | Apache-2.0 | Linux Foundation (TSC) | 4.4.2 (2026-08-07), 4.5.0-rc3 | LTS 최소 5년 (3.7 → 2029-07-27), 상용 최대 15년 (Canonical) |
| Eclipse ThreadX | MIT | Eclipse Foundation | 6.5.2.202603 (2026-10-02) | Alliance 회원 프로그램 |
| Apache NuttX | Apache-2.0 | Apache SF | 13.1.0 (2026-10-04) | 공식 LTS 없음 (확인한 범위) |
| RT-Thread | Apache-2.0 | RT-Thread 회사 | 5.3.0 (2026-09-10) | 확인 못 함 |
| ChibiOS | GPL3 / 상용 (HAL은 Apache-2.0) | 저자 중심 | trunk RT 8.0 개발판 | 확인 못 함 |
| RTEMS | BSD-2-Clause로 이전 중 (일부 GPL+예외) | OAR + 커뮤니티 | 6.2 (2025-12) | 확인 못 함 |
| µC/OS-III | Apache-2.0 | Weston Embedded | 3.08.02 (trunk) | 상용판 Cesium |
| Mbed OS | Apache-2.0 | Arm | 지원 종료 (2026-07) | 없음 |
| SafeRTOS · VxWorks · QNX · INTEGRITY · PikeOS · embOS | 상용 | 단일 벤더 | 벤더별 | 계약에 따름 |

```svg
<svg viewBox="0 0 680 282" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340" y="18" font-size="13" text-anchor="middle" font-weight="bold">누가 결정권을 쥐나 — 거버넌스 스펙트럼</text><line x1="20" y1="44" x2="660" y2="44" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="20" y="36" font-size="12" text-anchor="start">한 회사 소유</text><text x="660" y="36" font-size="12" text-anchor="end">중립 재단</text><rect x="10" y="56" width="325" height="86" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="20" y="75" font-size="13" text-anchor="start" font-weight="bold">① 상용 단일 벤더</text><text x="20" y="96" font-size="12" text-anchor="start">embOS (SEGGER) · SafeRTOS (WHIS)</text><text x="20" y="114" font-size="12" text-anchor="start">VxWorks (Wind River) · QNX (BlackBerry)</text><text x="20" y="132" font-size="12" text-anchor="start">INTEGRITY (Green Hills) · PikeOS (SYSGO)</text><rect x="345" y="56" width="325" height="86" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="355" y="75" font-size="13" text-anchor="start" font-weight="bold">② 회사 주도 오픈소스</text><text x="355" y="96" font-size="12" text-anchor="start">FreeRTOS (AWS) · RT-Thread (동명 회사)</text><text x="355" y="114" font-size="12" text-anchor="start">ChibiOS (저자 회사) · µC/OS (Weston)</text><text x="355" y="132" font-size="12" text-anchor="start">Hubris (Oxide)</text><rect x="10" y="168" width="325" height="68" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="20" y="187" font-size="13" text-anchor="start" font-weight="bold">③ 커뮤니티 프로젝트</text><text x="20" y="208" font-size="12" text-anchor="start">RTEMS (OAR·커뮤니티) · Embassy · RTIC</text><text x="20" y="226" font-size="12" text-anchor="start">Tock · PX4 · ArduPilot (앱 스택)</text><rect x="345" y="168" width="325" height="68" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="355" y="187" font-size="13" text-anchor="start" font-weight="bold">④ 재단 소속</text><text x="355" y="208" font-size="12" text-anchor="start">Zephyr — Linux Fdn · ThreadX — Eclipse Fdn</text><text x="355" y="226" font-size="12" text-anchor="start">NuttX — Apache SF · seL4 — seL4 Fdn (LF)</text><text x="340" y="256" font-size="12" text-anchor="middle">① 쪽: 로드맵·가격을 벤더가 정함, 대신 책임 주체가 분명</text><text x="340" y="274" font-size="12" text-anchor="middle">④ 쪽: 멤버 회사들이 TSC에서 결정, 벤더 하나가 사라져도 프로젝트는 남음</text></svg>
```

그림 4 — 거버넌스 스펙트럼. ①에서 ④로 갈수록 결정권이 한 회사에서 여러 멤버 회사로 퍼진다. 오픈소스라고 다 재단 소속은 아니다(FreeRTOS는 AWS가 쥔다).

### 5.2 예제 3 — 소스 트리가 실제로 선언한 라이선스를 센다

"MIT라더라"는 말 대신, 각 파일 머리의 **SPDX 식별자**(`SPDX-License-Identifier: Apache-2.0` 같은 한 줄 표준 표기)를 직접 센다. SBOM(소프트웨어 구성 명세)과 CRA 대응(L7)의 첫 단계가 바로 이 작업이다.

```python
#!/usr/bin/env python3
# e3: which license does each tree actually declare? scan SPDX ids in .c/.h headers
import os, re, collections
S = "/var/tmp/rtos/A6/src"
TREES = {
    "FreeRTOS": ("/var/rtoslab/src/FreeRTOS-Kernel", ""),
    "Zephyr":   ("/var/rtoslab/zephyrproject/zephyr", ""),
    "ThreadX":  (f"{S}/threadx", ""),
    "uC/OS-III": (f"{S}/uC-OS3", ""),
    "ChibiOS":  (f"{S}/ChibiOS", "os/rt"),      # kernel only (HAL differs)
    "RT-Thread": (f"{S}/rt-thread", "src"),
    "NuttX":    (f"{S}/nuttx", ""),
    "RTEMS":    (f"{S}/rtems", "cpukit"),
}
RX = re.compile(r"SPDX-License-Identifier:\s*([A-Za-z0-9.+\-() ]+?)\s*(\*/|$)", re.M)
LIC = re.compile(r"(Licensed to the Apache Software Foundation|GNU General Public License"
                 r"|Apache License, Version 2.0|Permission is hereby granted)")
for name, (root, sub) in TREES.items():
    top = os.path.join(root, sub)
    ids, total, none = collections.Counter(), 0, 0
    hint = collections.Counter()
    for r, d, fs in os.walk(top):
        d[:] = [x for x in d if x != ".git"]
        for f in fs:
            if not f.endswith((".c", ".h")):
                continue
            total += 1
            head = open(os.path.join(r, f), errors="replace").read(4000)
            m = RX.search(head)
            if m:
                ids[m.group(1).strip()] += 1
            else:
                none += 1
                h = LIC.search(head)
                hint[h.group(1)[:28] if h else "(no SPDX, no text)"] += 1
    top3 = ", ".join(f"{k} {v*100//total}%" for k, v in ids.most_common(3))
    print(f"{name:<10}{total:>6} files  SPDX: {top3 or '-'}")
    if none:
        print(f"{'':<10}{'':>6}        no-SPDX {none*100//total}%: " +
              ", ".join(f"{k}={v}" for k, v in hint.most_common(2)))
```

```sh
limactl shell elinux -- bash -lc 'python3 -I /Users/donh/.rtos_scratch/A6/e3_spdx_scan.py'
```

```text
FreeRTOS     655 files  SPDX: MIT 94%, MIT AND BSD-3-Clause 1%, Apache-2.0 0%
                        no-SPDX 3%: (no SPDX, no text)=20
Zephyr     13628 files  SPDX: Apache-2.0 99%, MIT 0%, BSD-3-Clause 0%
                        no-SPDX 0%: (no SPDX, no text)=12, Apache License, Version 2.0=5
ThreadX     3393 files  SPDX: MIT 60%, MIT and CC0-1.0 4%, Apache-2.0 3%
                        no-SPDX 31%: (no SPDX, no text)=1031, Apache License, Version 2.0=11
uC/OS-III    133 files  SPDX: APACHE-2.0 100%
ChibiOS       49 files  SPDX: -
                        no-SPDX 100%: GNU General Public License=48, Apache License, Version 2.0=1
RT-Thread     73 files  SPDX: Apache-2.0 100%
NuttX      18285 files  SPDX: Apache-2.0 90%, BSD-3-Clause 3%, ISC 0%
                        no-SPDX 5%: Licensed to the Apache Softw=871, (no SPDX, no text)=39
RTEMS       2158 files  SPDX: BSD-2-Clause 85%, GPL-2.0-with-RTEMS-exception 1%, BSD-2-Clause OR Apache-2.0 OR GPL-2.0-or-later 0%
                        no-SPDX 10%: (no SPDX, no text)=234, GNU General Public License=2
```

출력에서 볼 것:

- **Zephyr 99% Apache-2.0** — 프로젝트가 기여 시점에 SPDX를 강제한다. SBOM 만들기가 가장 쉽다.
- **FreeRTOS 94% MIT**, 일부 `MIT AND BSD-3-Clause`(서드파티 포트).
- **ChibiOS 커널은 SPDX가 0%** — 48개 파일이 GPL 문구 헤더만 가진다. 도구가 SPDX만 보면 "라이선스 없음"으로 잘못 분류한다.
- **ThreadX는 31%가 SPDX 없음**. 커널(`common/`)은 100% MIT인데 왜일까? 아래 추가 확인을 보자.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/A6/src/threadx;
  echo EULA: $(grep -rl "End User License Agreement" --include=*.c --include=*.h . | wc -l);
  echo common_total: $(find common -name "*.[ch]" | wc -l) common_MIT: $(grep -rl "SPDX-License-Identifier: MIT" common | wc -l)'
```

```text
EULA: 884
common_total: 197 common_MIT: 197
```

출력에서 볼 것: ThreadX 저장소 안의 **884개 파일**(주로 `ports_*/…/example_build/` 아래 Arm 예제 코드)이 MIT가 아니라 **"Arm 제품 최종 사용자 라이선스 계약(EULA)을 가진 사람만 사용 가능"** 이라는 헤더를 가진다. 커널은 MIT지만, 예제 빌드 파일을 무심코 제품에 복사하면 MIT가 아닌 코드를 출하하게 된다. **"RTOS의 라이선스" ≠ "저장소 모든 파일의 라이선스"**. 그래서 출하 전에 SPDX/라이선스 스캐너(예: ScanCode, REUSE 도구)를 CI에 넣는다(L7).

함정: SPDX가 `APACHE-2.0`처럼 대문자로 쓰인 파일(µC/OS)도 있다. SPDX 식별자는 대소문자를 구분하지 않도록 정의되어 있지만, 도구에 따라 매칭이 실패할 수 있다.

---

## 6. 누가 무엇을 쓰나 — 도메인 지도

```svg
<svg viewBox="0 0 680 296" xmlns="http://www.w3.org/2000/svg"><text x="340" y="18" font-size="13" text-anchor="middle" font-weight="bold">누가 무엇을 쓰나 — 도메인별 대표 조합 (출처는 본문 표)</text><rect x="6" y="32" width="220" height="104" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="16" y="51" font-size="13" text-anchor="start" font-weight="bold">드론 · 비행 제어</text><text x="16" y="72" font-size="12" text-anchor="start">PX4 → NuttX (주 RTOS)</text><text x="16" y="90" font-size="12" text-anchor="start">ArduPilot → ChibiOS (STM32)</text><text x="16" y="108" font-size="12" text-anchor="start">Betaflight → 자체 스케줄러</text><text x="16" y="126" font-size="12" text-anchor="start">고수준: Linux 컴패니언</text><rect x="229" y="32" width="220" height="104" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="239" y="51" font-size="13" text-anchor="start" font-weight="bold">자동차</text><text x="239" y="72" font-size="12" text-anchor="start">ECU → AUTOSAR Classic OS</text><text x="239" y="90" font-size="12" text-anchor="start">콕핏·ADAS → QNX (2.75억 대+)</text><text x="239" y="108" font-size="12" text-anchor="start">존 컨트롤러 → AUTOSAR/RTOS</text><text x="239" y="126" font-size="12" text-anchor="start">인포테인먼트 → Linux·Android</text><rect x="452" y="32" width="220" height="104" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="462" y="51" font-size="13" text-anchor="start" font-weight="bold">우주 · 항공 · 국방</text><text x="462" y="72" font-size="12" text-anchor="start">화성 로버 → VxWorks</text><text x="462" y="90" font-size="12" text-anchor="start">MMS·SDO 위성 → RTEMS</text><text x="462" y="108" font-size="12" text-anchor="start">항공 DAL A → INTEGRITY-178</text><text x="462" y="126" font-size="12" text-anchor="start">ARINC 653 → PikeOS · VxWorks</text><rect x="6" y="164" width="220" height="104" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="16" y="183" font-size="13" text-anchor="start" font-weight="bold">IoT · 무선 연결</text><text x="16" y="204" font-size="12" text-anchor="start">nRF Connect SDK → Zephyr</text><text x="16" y="222" font-size="12" text-anchor="start">AWS IoT → FreeRTOS</text><text x="16" y="240" font-size="12" text-anchor="start">Azure RTOS 유산 → ThreadX</text><text x="16" y="258" font-size="12" text-anchor="start">Thread · Matter → Zephyr 등</text><rect x="229" y="164" width="220" height="104" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="239" y="183" font-size="13" text-anchor="start" font-weight="bold">소비자 · 웨어러블</text><text x="239" y="204" font-size="12" text-anchor="start">ESP-IDF → FreeRTOS (SMP 변형)</text><text x="239" y="222" font-size="12" text-anchor="start">Oticon 보청기 → Zephyr</text><text x="239" y="240" font-size="12" text-anchor="start">Chromebook EC → Zephyr</text><text x="239" y="258" font-size="12" text-anchor="start">Xiaomi Vela → NuttX 기반</text><rect x="452" y="164" width="220" height="104" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="462" y="183" font-size="13" text-anchor="start" font-weight="bold">산업 · 의료 · 스토리지</text><text x="462" y="204" font-size="12" text-anchor="start">의료 → SafeRTOS · embOS-Safe</text><text x="462" y="222" font-size="12" text-anchor="start">ThreadX → 120억+ 기기 (자체)</text><text x="462" y="240" font-size="12" text-anchor="start">RT-Thread → 25억+ 대 (보도)</text><text x="462" y="258" font-size="12" text-anchor="start">SSD FW → 자체 executive (Don)</text></svg>
```

그림 5 — 도메인별 대표 RTOS 조합. 아래 표의 출처로 확인한 것만 넣었다.

| 도메인 | 주장 | 확인 결과 | 근거 |
|---|---|---|---|
| 드론 | PX4는 NuttX를 쓴다 | 확인 — "비행 제어 보드에서 PX4를 돌리는 주 RTOS" | PX4 문서 (architecture) |
| 드론 | ArduPilot은 ChibiOS를 쓴다 | 확인 — STM32 보드 포팅은 ChibiOS 기반 | ArduPilot 개발 문서 (porting), Copter 3.6 릴리스 노트 |
| 드론 | Betaflight는 RTOS가 없다 | 확인 — 자체 협력형 우선순위 스케줄러 (`scheduler.c`) | Betaflight 소스 |
| 드론 | Anduril·Skydio·DJI가 쓰는 RTOS | 확인 못 함 — 채용 공고에 FreeRTOS·Zephyr·RTX·ThreadX 경험 요구(Anduril), "STM32 bare-metal과 RTOS"(Skydio)만 보임 | 채용 공고 (제품 사실 아님) |
| 자동차 | 콕핏·ADAS에 QNX | 확인 — 2억 7,500만 대+ 차량, ADAS는 safety OS | BlackBerry FY2026 10-K, QNX Cabin 발표 |
| 자동차 | ECU는 AUTOSAR Classic OS | 확인 — EB·ETAS 제품이 SC1~SC4 지원 | 벤더 페이지 |
| 우주 | 화성 로버는 VxWorks | 확인 (Wind River 자료) — Curiosity·Perseverance | Wind River |
| 우주 | 위성·관측선에 RTEMS | 확인 — MMS, SDO, MRO Electra, Roman 망원경 | RTEMS 뉴스, JPL 논문 |
| 우주 | Perseverance가 RTEMS | **틀림** — 주 컴퓨터는 VxWorks | Wind River |
| IoT | nRF Connect SDK = Zephyr | 확인 | Nordic |
| IoT | ESP-IDF = FreeRTOS | 확인 — "Vanilla FreeRTOS v10.5.1 기반 + 듀얼코어용 SMP 변경" | Espressif 문서 |
| IoT | ThreadX 120억 대 | 프로젝트 자체 주장 | eclipse-threadx/rtos-docs |
| 소비자 | Chromebook EC = Zephyr | 확인 — 2021년경 이후 새 설계 | ChromiumOS EC 문서 |
| 소비자 | Oticon More 보청기 = Zephyr | 확인 — Polaris 칩셋의 BLE | Zephyr 블로그 (2021-01) |
| 소비자 | Xiaomi Vela = NuttX 기반 | 확인 — openvela, Apache-2.0 | NuttX 홈페이지, 보도 |
| 산업 | RT-Thread 25억 대 | 제3자 보도 (2025 중반) | riscv.org 뉴스 |

### 6.1 시장 조사 숫자 하나

Eclipse Foundation의 **2024 IoT & Embedded Developer Survey**(2024-12-03 발표, 약 750명 응답)에서 임베디드 OS 사용 비율은 Linux 46%, **FreeRTOS 29%, Zephyr 21%, ThreadX 13%** 였다. 2025년 조사(2026-03-05 발표)의 RTOS 비율은 확인 못 함. 설문 응답자는 Eclipse 커뮤니티 쪽으로 치우쳐 있으므로 "시장 점유율"이 아니라 "개발자 체감"으로 읽는다.

### 6.2 Don에게 의미

- Neros·Anduril 같은 **드론 회사** → PX4(NuttX) 또는 ArduPilot(ChibiOS) 위에서 일하거나, 자체 비행 스택이면 FreeRTOS/Zephyr. 면접에서 "PX4가 무엇 위에서 도나"는 기본 질문이다.
- Rivian 같은 **차량 회사** → ECU는 AUTOSAR Classic, 콕핏은 QNX/Linux, 센서·전원 MCU는 FreeRTOS/Zephyr/벤더 RTOS가 섞인다.
- Scout AI 같은 **국방 로보틱스** → 고수준은 Linux, 실시간은 MCU RTOS, 인증이 필요한 부분은 INTEGRITY·VxWorks·PikeOS가 후보.

---

## 7. 생태계 크기를 숫자로

"생태계가 크다"를 말로만 하지 않고 숫자로 만든다. 단, 각 숫자가 **무엇을 재고 무엇을 못 재는지**를 같이 적는다.

### 7.1 예제 4 — 커널 코어는 몇 줄인가

무엇을 확인하나: 각 RTOS에서 "스케줄러 + IPC + 타이머" 코어에 해당하는 디렉터리를 정하고, 빈 줄과 주석을 뺀 코드 줄 수를 센다.

```python
#!/usr/bin/env python3
# e4: "kernel core" size of 8 RTOSes -- files, physical lines, code lines (no blank/comment)
import os, re, sys
S = "/var/tmp/rtos/A6/src"
CORE = {  # name: (root, [sub-paths that make up the scheduler/IPC core, no ports/drivers])
    "FreeRTOS 11.3.2": ("/var/rtoslab/src/FreeRTOS-Kernel",
                        ["tasks.c", "queue.c", "list.c", "timers.c", "event_groups.c",
                         "stream_buffer.c", "croutine.c", "include"]),
    "Zephyr 4.4.2":    ("/var/rtoslab/zephyrproject/zephyr", ["kernel"]),
    "ThreadX 6.5.2":   (f"{S}/threadx", ["common/src", "common/inc"]),
    "uC/OS-III 3.08":  (f"{S}/uC-OS3", ["Source"]),
    "ChibiOS/RT 8.0":  (f"{S}/ChibiOS", ["os/rt"]),
    "RT-Thread 5.4":   (f"{S}/rt-thread", ["src", "include"]),
    "NuttX trunk":     (f"{S}/nuttx", ["sched"]),
    "RTEMS trunk":     (f"{S}/rtems", ["cpukit/score", "cpukit/include/rtems/score"]),
}
CMT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
def count(path):
    files = phys = code = 0
    paths = [path] if os.path.isfile(path) else [
        os.path.join(r, f) for r, _, fs in os.walk(path) for f in fs]
    for p in paths:
        if not p.endswith((".c", ".h")):
            continue
        t = open(p, errors="replace").read()
        files += 1
        phys += t.count("\n")
        code += sum(1 for l in CMT.sub("", t).splitlines() if l.strip())
    return files, phys, code

print(f"{'RTOS':<17}{'files':>6}{'lines':>9}{'code':>9}  scope")
rows = []
for name, (root, subs) in CORE.items():
    f = p = c = 0
    for s in subs:
        a, b, d = count(os.path.join(root, s))
        f, p, c = f + a, p + b, c + d
    rows.append((name, c))
    print(f"{name:<17}{f:>6}{p:>9}{c:>9}  {', '.join(subs)[:46]}")
print("CSV:" + ";".join(f"{n}={c}" for n, c in rows))
```

```sh
limactl shell elinux -- bash -lc 'python3 -I /Users/donh/.rtos_scratch/A6/e4_kernel_loc.py'
```

```text
RTOS              files    lines     code  scope
FreeRTOS 11.3.2      28    35573    15507  tasks.c, queue.c, list.c, timers.c, event_grou
Zephyr 4.4.2         66    21355    13164  kernel
ThreadX 6.5.2       197    43044    15543  common/src, common/inc
uC/OS-III 3.08       20    17584    10494  Source
ChibiOS/RT 8.0       49    15488     5086  os/rt
RT-Thread 5.4        87    37938    23326  src, include
NuttX trunk         295    57771    21770  sched
RTEMS trunk         564   142837    72833  cpukit/score, cpukit/include/rtems/score
CSV:FreeRTOS 11.3.2=15507;Zephyr 4.4.2=13164;ThreadX 6.5.2=15543;uC/OS-III 3.08=10494;ChibiOS/RT 8.0=5086;RT-Thread 5.4=23326;NuttX trunk=21770;RTEMS trunk=72833
```

```svg
<svg viewBox="0 0 660 334" xmlns="http://www.w3.org/2000/svg"><text x="330" y="20" font-size="13" text-anchor="middle" font-weight="bold">커널 코어 코드 줄 수 (빈 줄·주석 제외, 예제 4)</text><text x="167" y="55" font-size="12" text-anchor="end">RTEMS (score)</text><rect x="175" y="40" width="413" height="20" fill="#888" fill-opacity="0.55" stroke="#888"/><text x="594" y="55" font-size="12" text-anchor="start">72,833</text><text x="167" y="85" font-size="12" text-anchor="end">RT-Thread 5.4</text><rect x="175" y="70" width="132" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="313" y="85" font-size="12" text-anchor="start">23,326</text><text x="167" y="115" font-size="12" text-anchor="end">NuttX (sched/)</text><rect x="175" y="100" width="123" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="304" y="115" font-size="12" text-anchor="start">21,770</text><text x="167" y="145" font-size="12" text-anchor="end">ThreadX 6.5.2</text><rect x="175" y="130" width="88" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="269" y="145" font-size="12" text-anchor="start">15,543</text><text x="167" y="175" font-size="12" text-anchor="end">FreeRTOS 11.3.2</text><rect x="175" y="160" width="88" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="269" y="175" font-size="12" text-anchor="start">15,507</text><text x="167" y="205" font-size="12" text-anchor="end">Zephyr 4.4.2 (kernel/)</text><rect x="175" y="190" width="75" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="256" y="205" font-size="12" text-anchor="start">13,164</text><text x="167" y="235" font-size="12" text-anchor="end">uC/OS-III 3.08</text><rect x="175" y="220" width="59" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="240" y="235" font-size="12" text-anchor="start">10,494</text><text x="167" y="265" font-size="12" text-anchor="end">ChibiOS/RT 8.0</text><rect x="175" y="250" width="29" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="210" y="265" font-size="12" text-anchor="start">5,086</text><line x1="175" y1="284" x2="600" y2="284" stroke="currentColor"/><line x1="175" y1="284" x2="175" y2="289" stroke="currentColor"/><text x="175" y="302" font-size="12" text-anchor="middle">0k</text><line x1="317" y1="284" x2="317" y2="289" stroke="currentColor"/><text x="317" y="302" font-size="12" text-anchor="middle">25k</text><line x1="458" y1="284" x2="458" y2="289" stroke="currentColor"/><text x="458" y="302" font-size="12" text-anchor="middle">50k</text><line x1="600" y1="284" x2="600" y2="289" stroke="currentColor"/><text x="600" y="302" font-size="12" text-anchor="middle">75k</text><text x="330" y="324" font-size="12" text-anchor="middle">범위가 다르다: RTEMS는 SMP 스케줄러 여러 개 포함, NuttX는 POSIX 프로세스·시그널 포함</text></svg>
```

그림 6 — 커널 코어 코드 줄 수(예제 4). 파랑은 C 라이브러리형 커널, 초록은 Zephyr, 주황은 POSIX형·중국 생태계, 회색은 RTEMS.

출력에서 볼 것:

- 마지막 `CSV:` 줄은 그림 6을 그리려고 스크립트가 함께 찍은 것이다.
- **코어는 다들 1만~2만 줄대**다. FreeRTOS 15.5k, ThreadX 15.5k, Zephyr `kernel/` 13.2k, µC/OS-III 10.5k. ChibiOS/RT는 5k로 가장 작다.
- 흥미로운 일치: Zephyr 안전 인증 범위가 "약 15,000줄"(§3.2)인데, `kernel/` 디렉터리가 13,164줄이다. 인증 범위가 대략 **커널 코어 + 초기화 + API 헤더**라는 말과 맞는다.
- RTEMS score가 73k로 큰 것은 **SMP 스케줄러 여러 종류(EDF, 우선순위 affinity, strong APA 등)와 Classic·POSIX 공통 객체**가 다 들어 있기 때문이다. NuttX `sched/`는 시그널·프로세스·POSIX 타이머까지 포함한다.
- FreeRTOS의 28개 파일 중 큰 부분은 `include/`(MPU 래퍼 선언 등)다. 함정: 주석을 빼면 FreeRTOS `tasks.c`도 Zephyr처럼 SMP 분기(`#if configNUMBER_OF_CORES > 1`)가 많아서(예제 8에서 87곳) "작은 커널"의 이미지보다 복잡하다.

저장소 전체(.c/.h, 주석 포함 물리 줄)도 같이 보면 "커널 vs 생태계" 비율이 보인다.

```sh
limactl shell elinux -- bash -lc 'for d in /var/rtoslab/src/FreeRTOS-Kernel /var/rtoslab/zephyrproject/zephyr /var/tmp/rtos/A6/src/{threadx,uC-OS3,ChibiOS,rt-thread,nuttx,rtems}; do
  n=$(find $d -name .git -prune -o -type f \( -name "*.c" -o -name "*.h" \) -print | wc -l);
  l=$(find $d -name .git -prune -o -type f \( -name "*.c" -o -name "*.h" \) -print0 | xargs -0 cat | wc -l);
  printf "%-14s %7d files %10d lines\n" $(basename $d) $n $l; done'
```

```text
FreeRTOS-Kernel     655 files     348494 lines
zephyr           13628 files    3901845 lines
threadx           3373 files     930739 lines
uC-OS3             133 files      48006 lines
ChibiOS          4503 files    6115907 lines
rt-thread       23044 files   12353657 lines
nuttx           18284 files    6806696 lines
rtems            8904 files    4126914 lines
```

출력에서 볼 것: Zephyr는 커널 13k줄 위에 **390만 줄**의 드라이버·서브시스템·보드·테스트가 있다(커널은 0.3%). FreeRTOS-Kernel 저장소는 35만 줄인데 대부분이 포트다 — 드라이버·스택은 **다른 저장소(FreeRTOS-Plus, 벤더 SDK)** 에 있다. ChibiOS·RT-Thread가 수백만 줄인 것은 STM32 등 **벤더 레지스터 헤더와 HAL을 통째로 넣어서**다. 물리 줄 수는 생태계 크기보다 "저장소에 무엇을 넣는 정책"을 더 많이 반영한다.

### 7.2 예제 5 — GitHub 커뮤니티 지표

무엇을 확인하나: GitHub REST API로 stars·forks·contributors·생성일·라이선스를 가져온다(인증 없이 읽기만, 2×13 요청).

```python
#!/usr/bin/env python3
# e5: community-size indicators from the GitHub REST API (unauthenticated, read-only)
import json, re, urllib.request, datetime as dt
REPOS = ["zephyrproject-rtos/zephyr", "FreeRTOS/FreeRTOS-Kernel", "apache/nuttx",
         "RT-Thread/rt-thread", "eclipse-threadx/threadx", "RTEMS/rtems", "ChibiOS/ChibiOS",
         "embassy-rs/embassy", "rtic-rs/rtic", "oxidecomputer/hubris", "tock/tock",
         "PX4/PX4-Autopilot", "ArduPilot/ardupilot"]
def get(url):
    req = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json",
                                               "User-Agent": "rtos-a6-study"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r), r.headers.get("Link", "")
print("fetched", dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d %H:%M UTC"))
print(f"{'repo':<27}{'stars':>7}{'forks':>7}{'contrib':>8}  created     license")
for full in REPOS:
    meta, _ = get(f"https://api.github.com/repos/{full}")
    # contributors: per_page=1 -> page count in the 'last' Link == number of contributors
    _, link = get(f"https://api.github.com/repos/{full}/contributors?per_page=1&anon=1")
    m = re.search(r'[?&]page=(\d+)>; rel="last"', link)
    contrib = m.group(1) if m else "?"
    lic = (meta.get("license") or {}).get("spdx_id", "-")
    print(f"{full.split('/')[1]:<27}{meta['stargazers_count']:>7}{meta['forks_count']:>7}"
          f"{contrib:>8}  {meta['created_at'][:10]}  {lic}")
```

```sh
limactl shell elinux -- bash -lc 'python3 -I /Users/donh/.rtos_scratch/A6/e5_github_stats.py'
```

```text
fetched 2026-10-25 08:45 UTC
repo                         stars  forks contrib  created     license
zephyr                       16897  10254    4725  2016-05-26  Apache-2.0
FreeRTOS-Kernel               4606   1634     247  2019-09-09  MIT
nuttx                         4084   1731    1265  2019-12-14  Apache-2.0
rt-thread                    12305   5482    1180  2013-01-02  Apache-2.0
threadx                       3550    938      57  2020-05-11  MIT
rtems                          771    418     403  2012-05-05  NOASSERTION
ChibiOS                        873    532      35  2014-12-01  GPL-3.0
embassy                      10007   1714    1002  2020-09-22  Apache-2.0
rtic                          2420    268     116  2017-03-05  Apache-2.0
hubris                        3639    242      59  2020-04-02  MPL-2.0
tock                          6476    867     290  2015-05-28  NOASSERTION
PX4-Autopilot                12897  16206    1225  2012-08-04  BSD-3-Clause
ardupilot                    16205  21670    1409  2013-01-09  GPL-3.0
```

```svg
<svg viewBox="0 0 680 398" xmlns="http://www.w3.org/2000/svg"><text x="340" y="18" font-size="13" text-anchor="middle" font-weight="bold">GitHub 지표 (2026-10-25 08:45 UTC, 예제 5)</text><text x="264" y="36" font-size="12" text-anchor="middle" font-weight="bold">stars</text><text x="535" y="36" font-size="12" text-anchor="middle" font-weight="bold">contributors (anon 포함)</text><text x="128" y="59" font-size="12" text-anchor="end">zephyr</text><rect x="132" y="46" width="247" height="17" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="383" y="59" font-size="12" text-anchor="start">16,897</text><rect x="430" y="46" width="198" height="17" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="632" y="59" font-size="12" text-anchor="start">4,725</text><text x="128" y="83" font-size="12" text-anchor="end">ardupilot</text><rect x="132" y="70" width="237" height="17" fill="#888" fill-opacity="0.55" stroke="#888"/><text x="373" y="83" font-size="12" text-anchor="start">16,205</text><rect x="430" y="70" width="59" height="17" fill="#888" fill-opacity="0.55" stroke="#888"/><text x="493" y="83" font-size="12" text-anchor="start">1,409</text><text x="128" y="107" font-size="12" text-anchor="end">PX4-Autopilot</text><rect x="132" y="94" width="188" height="17" fill="#888" fill-opacity="0.55" stroke="#888"/><text x="324" y="107" font-size="12" text-anchor="start">12,897</text><rect x="430" y="94" width="51" height="17" fill="#888" fill-opacity="0.55" stroke="#888"/><text x="485" y="107" font-size="12" text-anchor="start">1,225</text><text x="128" y="131" font-size="12" text-anchor="end">rt-thread</text><rect x="132" y="118" width="180" height="17" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="316" y="131" font-size="12" text-anchor="start">12,305</text><rect x="430" y="118" width="50" height="17" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="484" y="131" font-size="12" text-anchor="start">1,180</text><text x="128" y="155" font-size="12" text-anchor="end">embassy</text><rect x="132" y="142" width="146" height="17" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/><text x="282" y="155" font-size="12" text-anchor="start">10,007</text><rect x="430" y="142" width="42" height="17" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/><text x="476" y="155" font-size="12" text-anchor="start">1,002</text><text x="128" y="179" font-size="12" text-anchor="end">tock</text><rect x="132" y="166" width="95" height="17" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/><text x="231" y="179" font-size="12" text-anchor="start">6,476</text><rect x="430" y="166" width="12" height="17" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/><text x="446" y="179" font-size="12" text-anchor="start">290</text><text x="128" y="203" font-size="12" text-anchor="end">FreeRTOS-Kernel</text><rect x="132" y="190" width="67" height="17" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="203" y="203" font-size="12" text-anchor="start">4,606</text><rect x="430" y="190" width="10" height="17" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="444" y="203" font-size="12" text-anchor="start">247</text><text x="128" y="227" font-size="12" text-anchor="end">nuttx</text><rect x="132" y="214" width="60" height="17" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="196" y="227" font-size="12" text-anchor="start">4,084</text><rect x="430" y="214" width="53" height="17" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="487" y="227" font-size="12" text-anchor="start">1,265</text><text x="128" y="251" font-size="12" text-anchor="end">hubris</text><rect x="132" y="238" width="53" height="17" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/><text x="189" y="251" font-size="12" text-anchor="start">3,639</text><rect x="430" y="238" width="2" height="17" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/><text x="436" y="251" font-size="12" text-anchor="start">59</text><text x="128" y="275" font-size="12" text-anchor="end">threadx</text><rect x="132" y="262" width="52" height="17" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="188" y="275" font-size="12" text-anchor="start">3,550</text><rect x="430" y="262" width="2" height="17" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="436" y="275" font-size="12" text-anchor="start">57</text><text x="128" y="299" font-size="12" text-anchor="end">rtic</text><rect x="132" y="286" width="35" height="17" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/><text x="171" y="299" font-size="12" text-anchor="start">2,420</text><rect x="430" y="286" width="5" height="17" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/><text x="439" y="299" font-size="12" text-anchor="start">116</text><text x="128" y="323" font-size="12" text-anchor="end">ChibiOS (mirror)</text><rect x="132" y="310" width="13" height="17" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="149" y="323" font-size="12" text-anchor="start">873</text><rect x="430" y="310" width="1" height="17" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="435" y="323" font-size="12" text-anchor="start">35</text><text x="128" y="347" font-size="12" text-anchor="end">rtems (mirror)</text><rect x="132" y="334" width="11" height="17" fill="#888" fill-opacity="0.55" stroke="#888"/><text x="147" y="347" font-size="12" text-anchor="start">771</text><rect x="430" y="334" width="17" height="17" fill="#888" fill-opacity="0.55" stroke="#888"/><text x="451" y="347" font-size="12" text-anchor="start">403</text><line x1="132" y1="360" x2="395" y2="360" stroke="currentColor"/><line x1="430" y1="360" x2="640" y2="360" stroke="currentColor"/><rect x="40" y="374" width="12" height="12" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="57" y="385" font-size="12" text-anchor="start">C 커널 RTOS</text><rect x="155.72" y="374" width="12" height="12" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="173" y="385" font-size="12" text-anchor="start">Zephyr</text><rect x="240.48" y="374" width="12" height="12" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="257" y="385" font-size="12" text-anchor="start">POSIX형/중국</text><rect x="361.24" y="374" width="12" height="12" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/><text x="378" y="385" font-size="12" text-anchor="start">Rust</text><rect x="432.08000000000004" y="374" width="12" height="12" fill="#888" fill-opacity="0.55" stroke="#888"/><text x="449" y="385" font-size="12" text-anchor="start">비행 스택·우주</text></svg>
```

그림 7 — GitHub 지표(예제 5, 2026-10-25 08:45 UTC). 왼쪽 stars, 오른쪽 contributors(이메일만 있는 익명 기여 포함).

출력에서 볼 것과 함정:

- **Zephyr가 stars·contributors 모두 1위**(4,725명). 4.4 발표의 "930명+"는 한 릴리스 주기 동안의 기여자 수이고, 이것은 누적이다.
- **FreeRTOS-Kernel은 stars 4.6k, contributors 247명**으로 작아 보인다. 저장소가 2019년에야 GitHub로 옮겨졌고, 커널만 있는 저장소이기 때문이다. 실제 배포량(ESP32 하나만 해도 막대하다)과 GitHub 숫자는 무관하다. **stars는 "관심도", contributors는 "개발 참여 폭"이지 "배포량"이 아니다.**
- **ThreadX contributors 57명** — Eclipse 이관 후에도 외부 기여가 아직 적다. 생태계 건강도를 볼 때 이 숫자가 stars보다 중요하다.
- Rust 쪽 **Embassy stars 1만** — 신생인데 FreeRTOS-Kernel의 2배다. "관심"의 방향을 보여 준다.
- RTEMS·ChibiOS는 GitHub가 **미러**라서 숫자가 실제 커뮤니티보다 작다(RTEMS 본 저장소는 자체 GitLab).
- license 칸의 `NOASSERTION`은 GitHub가 라이선스 파일을 하나로 판정하지 못했다는 뜻이다(RTEMS는 여러 라이선스 혼재, Tock은 Apache-2.0 또는 MIT 이중).

### 7.3 예제 6 — 릴리스 주기를 계산한다

무엇을 확인하나: Zephyr는 git 태그 날짜, FreeRTOS는 `History.txt`의 "released <날짜>" 줄로 릴리스 간격을 계산한다. 이 파일의 날짜 표기는 세 가지 형식이 섞여 있어서 파서가 셋 다 처리한다.

```python
#!/usr/bin/env python3
# e6: release cadence -- Zephyr vX.Y.0 tag dates (git) and FreeRTOS kernel dates (History.txt)
import re, subprocess, datetime as dt, statistics as st
Z = "/var/rtoslab/zephyrproject/zephyr"
out = subprocess.run(["git", "-C", Z, "tag", "-l", "v*.*.0", "--sort=creatordate",
                      "--format=%(refname:short) %(creatordate:short)"],
                     capture_output=True, text=True).stdout.split("\n")
ver = lambda t: tuple(int(x) for x in re.findall(r"\d+", t)[:3])
z = [(t, dt.date.fromisoformat(d)) for t, d in (l.split() for l in out if l)
     if "rc" not in t and ver(t) >= (1, 6, 0)]
def gaps(rel):
    return [(rel[i][0], (rel[i][1] - rel[i - 1][1]).days) for i in range(1, len(rel))]
zg = gaps(z)
print("Zephyr feature releases:", len(z), f"({z[0][0]} {z[0][1]} .. {z[-1][0]} {z[-1][1]})")
for era, lo, hi in (("v1.x", "v1.", "v2.0.0"), ("v2.x", "v2.", "v3.0.0"),
                    ("v3.x", "v3.", "v4.0.0"), ("v4.x", "v4.", "v9")):
    g = [d for t, d in zg if (t.startswith(lo) and t != lo + "0.0") or t == hi]
    if g:
        print(f"  {era}: mean gap {st.mean(g):5.0f} d  min {min(g):3d}  max {max(g):3d}  n={len(g)}")
print("  last 5 gaps:", ", ".join(f"{t}={d}d" for t, d in zg[-5:]))
zd = dict(z)
print("  LTS tags   :", ", ".join(f"{t} ({zd[t]})" for t in ("v1.14.0", "v2.7.0", "v3.7.0")))

H = open("/var/rtoslab/src/FreeRTOS-Kernel/History.txt", errors="replace").read()
f = {}
for a, b, date in re.findall(r"Changes between (?:FreeRTOS )?V?(1\d\.[\d.]+) and "
                             r"(?:FreeRTOS )?V?(1\d\.[\d.]+),? released ([\w ,]+?):?\n", H):
    v = max(a, b, key=ver)                       # one entry is written in reverse order
    date = date.replace(",", "").strip()
    for fmt in ("%B %d %Y", "%d %B %Y", "%B %Y"):  # 3 date styles appear in History.txt
        try:
            f[v] = dt.datetime.strptime(date, fmt).date(); break
        except ValueError:
            pass
f = sorted(f.items(), key=lambda x: x[1])
fg = gaps(f)
print("FreeRTOS kernel releases (V10.0.1+):", len(f), f"({f[0][0]} .. {f[-1][0]} {f[-1][1]})")
minor = [(v, d) for v, d in f if v.endswith(".0")]
print("  minor/major (.0):", ", ".join(f"{v} {d:%Y-%m}" for v, d in minor))
print(f"  mean gap all {st.mean(d for _, d in fg):.0f} d, between .0 releases "
      f"{st.mean(d for _, d in gaps(minor)):.0f} d")
```

```sh
limactl shell elinux -- bash -lc 'python3 -I /Users/donh/.rtos_scratch/A6/e6_cadence.py'
```

```text
Zephyr feature releases: 30 (v1.6.0 2016-12-03 .. v4.4.0 2026-04-14)
  v1.x: mean gap   112 d  min  84  max 218  n=9
  v2.x: mean gap   112 d  min  87  max 140  n=8
  v3.x: mean gap   125 d  min 104  max 154  n=8
  v4.x: mean gap   129 d  min 112  max 152  n=4
  last 5 gaps: v4.0.0=112d, v4.1.0=112d, v4.2.0=133d, v4.3.0=118d, v4.4.0=152d
  LTS tags   : v1.14.0 (2019-04-16), v2.7.0 (2021-10-16), v3.7.0 (2024-07-26)
FreeRTOS kernel releases (V10.0.1+): 26 (10.0.1 .. 11.3.2 2026-10-16)
  minor/major (.0): 10.1.0 2018-08, 10.2.0 2019-02, 10.3.0 2020-02, 10.4.0 2020-09, 10.5.0 2022-09, 10.6.0 2023-07, 11.0.0 2023-12, 11.1.0 2024-04, 11.2.0 2025-03, 11.3.0 2026-03
  mean gap all 129 d, between .0 releases 305 d
```

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg"><text x="340" y="18" font-size="13" text-anchor="middle" font-weight="bold">Zephyr 기능 릴리스 간격 (일) — git tag 날짜, 예제 6</text><line x1="50" y1="200" x2="660" y2="200" stroke="currentColor"/><line x1="50" y1="200" x2="50" y2="34" stroke="currentColor"/><line x1="46" y1="200" x2="660" y2="200" stroke="#888" stroke-opacity="0.35"/><text x="43" y="204" font-size="12" text-anchor="end">0</text><line x1="46" y1="160" x2="660" y2="160" stroke="#888" stroke-opacity="0.35"/><text x="43" y="164" font-size="12" text-anchor="end">60</text><line x1="46" y1="120" x2="660" y2="120" stroke="#888" stroke-opacity="0.35"/><text x="43" y="124" font-size="12" text-anchor="end">120</text><line x1="46" y1="80" x2="660" y2="80" stroke="#888" stroke-opacity="0.35"/><text x="43" y="84" font-size="12" text-anchor="end">180</text><line x1="46" y1="40" x2="660" y2="40" stroke="#888" stroke-opacity="0.35"/><text x="43" y="44" font-size="12" text-anchor="end">240</text><line x1="70" y1="200" x2="70" y2="204" stroke="currentColor"/><text x="70" y="217" font-size="12" text-anchor="middle">2017</text><line x1="131" y1="200" x2="131" y2="204" stroke="currentColor"/><text x="131" y="217" font-size="12" text-anchor="middle">2018</text><line x1="191" y1="200" x2="191" y2="204" stroke="currentColor"/><text x="191" y="217" font-size="12" text-anchor="middle">2019</text><line x1="252" y1="200" x2="252" y2="204" stroke="currentColor"/><text x="252" y="217" font-size="12" text-anchor="middle">2020</text><line x1="312" y1="200" x2="312" y2="204" stroke="currentColor"/><text x="312" y="217" font-size="12" text-anchor="middle">2021</text><line x1="373" y1="200" x2="373" y2="204" stroke="currentColor"/><text x="373" y="217" font-size="12" text-anchor="middle">2022</text><line x1="433" y1="200" x2="433" y2="204" stroke="currentColor"/><text x="433" y="217" font-size="12" text-anchor="middle">2023</text><line x1="494" y1="200" x2="494" y2="204" stroke="currentColor"/><text x="494" y="217" font-size="12" text-anchor="middle">2024</text><line x1="554" y1="200" x2="554" y2="204" stroke="currentColor"/><text x="554" y="217" font-size="12" text-anchor="middle">2025</text><line x1="615" y1="200" x2="615" y2="204" stroke="currentColor"/><text x="615" y="217" font-size="12" text-anchor="middle">2026</text><polyline points="82,135 98,136 111,144 127,139 142,139 157,138 172,139 209,55 232,104 248,139 263,138 277,142 296,124 320,107 338,127 360,111 381,115 398,131 418,122 441,105 461,122 482,116 502,116 528,97 547,125 565,125 587,111 607,121 632,99" fill="none" stroke="#4a7bd0" stroke-width="1.5"/><circle cx="82" cy="135" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="98" cy="136" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="111" cy="144" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="127" cy="139" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="142" cy="139" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="157" cy="138" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="172" cy="139" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="209" cy="55" r="5" fill="#d0564a" fill-opacity="0.8"/><text x="209" y="46" font-size="12" text-anchor="middle">1.14 LTS</text><circle cx="232" cy="104" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="248" cy="139" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="263" cy="138" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="277" cy="142" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="296" cy="124" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="320" cy="107" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="338" cy="127" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="360" cy="111" r="5" fill="#d0564a" fill-opacity="0.8"/><text x="360" y="102" font-size="12" text-anchor="middle">2.7 LTS</text><circle cx="381" cy="115" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="398" cy="131" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="418" cy="122" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="441" cy="105" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="461" cy="122" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="482" cy="116" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="502" cy="116" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="528" cy="97" r="5" fill="#d0564a" fill-opacity="0.8"/><text x="528" y="88" font-size="12" text-anchor="middle">3.7 LTS</text><circle cx="547" cy="125" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="565" cy="125" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="587" cy="111" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="607" cy="121" r="3" fill="#4a7bd0" fill-opacity="0.8"/><circle cx="632" cy="99" r="3" fill="#4a7bd0" fill-opacity="0.8"/><text x="632" y="90" font-size="12" text-anchor="middle">4.4</text><circle cx="60" cy="234" r="5" fill="#d0564a" fill-opacity="0.8"/><text x="70" y="238" font-size="12" text-anchor="start">LTS 릴리스</text><text x="160" y="238" font-size="12" text-anchor="start">평균 간격: v2.x 112일 · v3.x 125일 · v4.x 129일 (4.5.0은 2026-10-25 현재 rc3)</text></svg>
```

그림 8 — Zephyr 기능 릴리스 사이의 간격(일). 빨간 점이 LTS. LTS 앞에는 안정화 기간이 길어 간격이 튄다(1.14 = 218일).

출력에서 볼 것:

- Zephyr는 v2~v4 내내 평균 **112~129일**(약 4개월, 연 3회)로 일정했다. 4.4는 **152일** — 연 2회(4월·10월) 체제로 바뀌면서 간격이 6개월로 늘어나는 첫 구간이다. 공식 문서도 "roughly 6-month release cycle"로 적고 있다(v4.4.2 `doc/project/release_process.rst`). 4.5가 10월에 나오면 다음 간격은 약 180일이 된다.
- LTS(1.14, 2.7, 3.7) 앞에서 간격이 길어진다. LTS 전에는 API freeze(LTS−2)·feature freeze(LTS−1)가 있기 때문이다.
- FreeRTOS 커널은 패치 포함 평균 **129일**마다, 기능 릴리스(.0)는 평균 **305일**(약 10개월)마다 나온다. 대신 AWS가 2년마다 **LTS 묶음**(커널 + coreMQTT 등 라이브러리)을 낸다.

함정 하나 더 — 예제 7에서 보겠지만 **V11.3.2 소스의 `tskKERNEL_VERSION_NUMBER` 매크로는 아직 `"V11.1.0+"`** 이다(`include/task.h` 57행, "+"는 개발 중 버전이라는 뜻). 버전을 런타임 문자열로 보고하는 펌웨어는 엉뚱한 버전을 SBOM·로그에 남긴다. 버전은 **빌드 시스템이 git 태그나 패키지 버전에서** 가져와야 한다.

### 7.4 예제 7 — 같은 일을 하는 최소 이미지 크기 (가볍게, 자세한 것은 A3)

무엇을 확인하나: "3번 출력하고 10 ms씩 쉰다"는 같은 일을 FreeRTOS와 Zephyr로 만들어 같은 QEMU 보드(mps2-an385)에서 돌리고 `size`를 비교한다. FreeRTOS는 heap_1 + 힙 4 KB로 줄였고, Zephyr는 기본 설정(prj.conf에 `CONFIG_PRINTK=y`만)이다.

```c
/* e7a: minimal FreeRTOS app -- 1 task, prints 3 times, exits (footprint baseline) */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static void worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 3; i++) {
        board_printf_raw("FreeRTOS %s tick=%u\n", tskKERNEL_VERSION_NUMBER,
                         (unsigned)xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    board_exit(0);
}

int main(void)
{
    xTaskCreate(worker, "w", 256, NULL, 1, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

```c
/* e7b: minimal Zephyr app -- main thread prints 3 times (same job as e7a) */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/version.h>

int main(void)
{
    for (int i = 0; i < 3; i++) {
        printk("Zephyr %s uptime=%lld ms\n", KERNEL_VERSION_STRING, k_uptime_get());
        k_msleep(10);
    }
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'D=/Users/donh/.rtos_scratch/A6
  make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$D/e7_freertos_min.c OUT=/var/tmp/rtos/A6/e7f \
       HEAP=heap_1 EXTRA_CFLAGS=-DconfigTOTAL_HEAP_SIZE=4096 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A6/e7f/app.elf
  /var/rtoslab/bin/rtos-zbuild mps2/an385 $D/zmin /var/tmp/rtos/A6/e7z && arm-none-eabi-size /var/tmp/rtos/A6/e7z/zephyr/zephyr.elf
  timeout 3 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
       -icount shift=5 -kernel /var/tmp/rtos/A6/e7z/zephyr/zephyr.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
  11192	    100	   8400	  19692	   4cec	/var/tmp/rtos/A6/e7f/app.elf
FreeRTOS V11.1.0+ tick=0
FreeRTOS V11.1.0+ tick=10
FreeRTOS V11.1.0+ tick=20
[board_exit] code=0
           FLASH:       13028 B         4 MB      0.31%
             RAM:        6112 B         4 MB      0.15%
   text	   data	    bss	    dec	    hex	filename
  12860	    168	   5943	  18971	   4a1b	/var/tmp/rtos/A6/e7z/zephyr/zephyr.elf
*** Booting Zephyr OS build v4.4.2 ***
Zephyr 4.4.2 uptime=0 ms
Zephyr 4.4.2 uptime=20 ms
Zephyr 4.4.2 uptime=40 ms
qemu-system-arm: terminating on signal 15 from pid 1119878 (timeout)
```

```svg
<svg viewBox="0 0 660 172" xmlns="http://www.w3.org/2000/svg"><text x="330" y="18" font-size="13" text-anchor="middle" font-weight="bold">같은 일(3번 출력 + 10 ms 대기)의 이미지 크기, mps2-an385 (예제 7)</text><text x="122" y="61" font-size="12" text-anchor="end">FreeRTOS 11.3.2</text><rect x="130.0" y="44" width="218.8" height="24" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><rect x="348.8" y="44" width="2.0" height="24" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><rect x="350.8" y="44" width="164.2" height="24" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="521" y="61" font-size="12" text-anchor="start">19,692 B</text><text x="130" y="84" font-size="12" text-anchor="start">text 11,192 · data 100 · bss 8,400</text><text x="122" y="111" font-size="12" text-anchor="end">Zephyr 4.4.2</text><rect x="130.0" y="94" width="251.4" height="24" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><rect x="381.4" y="94" width="3.3" height="24" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><rect x="384.6" y="94" width="116.2" height="24" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="507" y="111" font-size="12" text-anchor="start">18,971 B</text><text x="130" y="134" font-size="12" text-anchor="start">text 12,860 · data 168 · bss 5,943</text><rect x="130" y="148" width="12" height="12" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="147" y="159" font-size="12" text-anchor="start">text (FLASH)</text><rect x="254.51999999999998" y="148" width="12" height="12" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="272" y="159" font-size="12" text-anchor="start">data</text><rect x="323.36" y="148" width="12" height="12" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="340" y="159" font-size="12" text-anchor="start">bss (RAM, 스택·힙 포함)</text></svg>
```

그림 9 — 같은 일을 하는 최소 이미지(예제 7). 파랑 text(코드·상수, FLASH), 주황 data, 초록 bss(RAM: 스택·힙·커널 객체).

출력에서 볼 것:

- 경고 0개로 빌드되었고, 둘 다 **text 11~13 KB**로 비슷하다. Zephyr의 RAM(bss 5.9 KB)이 오히려 작은 것은 FreeRTOS 쪽에 힙 4 KB(heap_1)가 통째로 bss에 잡혀 있어서다. "Zephyr는 무겁다"는 기본 설정 + 많은 서브시스템을 켰을 때의 이야기다.
- FreeRTOS는 tick 0·10·20(1 kHz tick, 10 ms 대기), Zephyr는 uptime 0·20·40 ms다. Zephyr mps2/an385 보드의 기본 `CONFIG_SYS_CLOCK_TICKS_PER_SEC=100`(10 ms tick)이라 `k_msleep(10)`이 **"최소 10 ms"를 보장하려고 한 tick을 더** 기다린다. 같은 API 이름이라도 tick 설정이 결과를 바꾼다(C5).
- FreeRTOS 출력의 `V11.1.0+`가 §7.3의 버전 매크로 함정이다.
- 마지막 `qemu-system-arm: terminating on signal 15` 줄은 Zephyr 앱이 `main()`에서 돌아온 뒤에도 idle 스레드가 계속 돌아서 `timeout 3`이 QEMU를 끝낸 흔적이다(오류 아님). FreeRTOS 템플릿은 `board_exit()`로 스스로 끝낸다.

수치는 QEMU icount 기준이다. 실제 칩과 절대값은 다르다(캐시·플래시 wait state·버스 미모델링). footprint를 줄이는 설정과 정밀 비교는 A3에서 한다.

### 7.5 숫자를 읽는 법 — 지표별 신뢰도

| 지표 | 잘 재는 것 | 못 재는 것 | 이 노트 값 |
|---|---|---|---|
| 보드 · 포트 수 | 시작점의 넓이 | 드라이버 완성도, 품질 | Zephyr 1,014 보드 / FreeRTOS 143 포트 |
| 커널 LOC | 커널 복잡도, 인증 범위 감 | 생태계 | 5k~73k |
| 저장소 LOC | 저장소 정책 | 실제 사용 | 0.05M~12M |
| GitHub stars | 관심도 | 배포량 | 0.8k~16.9k |
| contributors | 개발 참여 폭 | 유지보수 질 | 35~4,725 |
| 릴리스 간격 · LTS | 예측 가능성 | 패치 품질 | Zephyr 6개월 / LTS 5년 |
| 설문 비율 | 개발자 체감 | 출하량 | FreeRTOS 29% / Zephyr 21% |
| 벤더 자체 주장 (기기 수) | 마케팅 규모 | 검증 가능성 | ThreadX 120억, RT-Thread 25억 |

---

## 8. 고르는 법 — 기준 매트릭스와 결정 흐름

### 8.1 선택 기준 9가지

| 기준 | 질문 | 확인 방법 |
|---|---|---|
| 라이선스 | 내 앱 소스 공개 의무? 비용 구조? | LICENSE + SPDX 스캔 (예제 3), 법무 검토 |
| 인증 | 필요한 SIL·ASIL·DAL 증거가 있나? 어느 버전? | 인증서 사본, safety manual, 인증 버전 = 사용 버전 |
| 생태계 · 벤더 지원 | 칩 벤더가 이 RTOS로 SDK·예제·FAE 지원? | 벤더 SDK 문서, `boards/` 디렉터리 (예제 2) |
| MCU 지원 | 내 코어(M33·M55·R52·RISC-V)의 포트 품질? | 포트 디렉터리, CI 대상 여부 (예제 1) |
| 툴링 | 빌드·디버거 RTOS 인식·트레이스·테스트 | west/Twister, SystemView·Tracealyzer 지원 |
| footprint | FLASH·RAM 예산 안에 들어가나 | 최소 이미지 측정 (예제 7, A3) |
| 커뮤니티 크기 | 문제를 물어볼 곳, 채용 시장 | contributors·릴리스 빈도 (예제 5, 6) |
| 장기 지원 | 제품 수명(5~15년) 동안 패치? | LTS 정책, 상용 연장 지원 |
| 보안 프로세스 · CRA | 취약점 접수·공개(PSIRT), CVE 발행, SBOM | 보안 정책 페이지, CVE 이력, SPDX 품질 |

### 8.2 주요 RTOS 비교 (기준별)

| 기준 | FreeRTOS | Zephyr | ThreadX | NuttX | SafeRTOS | QNX |
|---|---|---|---|---|---|---|
| 라이선스 | MIT | Apache-2.0 | MIT | Apache-2.0 | 상용 | 상용 |
| 인증 | 없음 (SafeRTOS 경로) | 진행 중 (SIL 3 목표) | 6.1.x 인증, 6.4.x 진행 | 없음 | SIL 3 · ASIL D | SIL 3 · ASIL D |
| 벤더 지원 | 거의 모든 벤더 예제 | 다수 벤더 공식 (NCS 등) | Azure RTOS 시절 SDK 통합 이력 | 일부 (Sony 등) | 일부 벤더 ROM·SDK | 자동차 SoC |
| 드라이버 모델 | 없음 (벤더 HAL) | 있음 (DT + device) | 미들웨어 세트 | POSIX `/dev` | 없음 | 자원 관리자 (유저 공간) |
| footprint | 매우 작음 | 설정 따라 작음~중간 | 매우 작음 | 중간 | 매우 작음 | MMU 필요 |
| LTS | 2년 | 5년 (+ 상용 15년) | Alliance | 확인 못 함 | 계약 | 계약 |
| 보안 프로세스 | AWS 보안 공지 | 보안 공지 · CVE 수정 릴리스 (4.4.2) · `west spdx` SBOM | Eclipse 보안 정책 | ASF 보안 프로세스 | 벤더 | 벤더 |
| 강한 도메인 | IoT · 소비자 · 산업 | 무선 IoT · 웨어러블 · EC | 산업 · 의료 유산 | 드론 (PX4) | 산업 · 의료 · 자동차 | 자동차 · 로봇 |

### 8.3 결정 흐름

```svg
<svg viewBox="0 0 680 372" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340" y="18" font-size="13" text-anchor="middle" font-weight="bold">RTOS 고르기 — 위에서부터 묻는다</text><rect x="10" y="34" width="350" height="32" rx="14" fill="#888" fill-opacity="0.1" stroke="#888" stroke-width="1.3"/><text x="22" y="55" font-size="12" text-anchor="start">① 기능 안전 인증 (SIL 3 · ASIL D · DAL A)이 필수?</text><line x1="362" y1="50" x2="393" y2="50" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><text x="374" y="45" font-size="12" text-anchor="start">예</text><rect x="395" y="34" width="275" height="32" rx="6" fill="#d0564a" fill-opacity="0.14" stroke="#d0564a" stroke-width="1.3"/><text x="405" y="55" font-size="12" text-anchor="start">인증 상용 RTOS / AUTOSAR OS</text><line x1="50" y1="67" x2="50" y2="86" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><text x="58" y="81" font-size="12" text-anchor="start">아니오</text><rect x="10" y="88" width="350" height="32" rx="14" fill="#888" fill-opacity="0.1" stroke="#888" stroke-width="1.3"/><text x="22" y="109" font-size="12" text-anchor="start">② 칩 벤더 SDK가 RTOS를 이미 정했나?</text><line x1="362" y1="104" x2="393" y2="104" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><text x="374" y="99" font-size="12" text-anchor="start">예</text><rect x="395" y="88" width="275" height="32" rx="6" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="405" y="109" font-size="12" text-anchor="start">벤더 SDK의 RTOS (예: NCS → Zephyr)</text><line x1="50" y1="121" x2="50" y2="140" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><text x="58" y="135" font-size="12" text-anchor="start">아니오</text><rect x="10" y="142" width="350" height="32" rx="14" fill="#888" fill-opacity="0.1" stroke="#888" stroke-width="1.3"/><text x="22" y="163" font-size="12" text-anchor="start">③ PX4·ArduPilot·POSIX 코드를 재사용?</text><line x1="362" y1="158" x2="393" y2="158" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><text x="374" y="153" font-size="12" text-anchor="start">예</text><rect x="395" y="142" width="275" height="32" rx="6" fill="#e08a3c" fill-opacity="0.14" stroke="#e08a3c" stroke-width="1.3"/><text x="405" y="163" font-size="12" text-anchor="start">NuttX · ChibiOS (스택이 정함)</text><line x1="50" y1="175" x2="50" y2="194" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><text x="58" y="189" font-size="12" text-anchor="start">아니오</text><rect x="10" y="196" width="350" height="32" rx="14" fill="#888" fill-opacity="0.1" stroke="#888" stroke-width="1.3"/><text x="22" y="217" font-size="12" text-anchor="start">④ 여러 MCU · 무선 · 장기 유지보수 · CRA?</text><line x1="362" y1="212" x2="393" y2="212" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><text x="374" y="207" font-size="12" text-anchor="start">예</text><rect x="395" y="196" width="275" height="32" rx="6" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="405" y="217" font-size="12" text-anchor="start">Zephyr (LTS + 보안 프로세스)</text><line x1="50" y1="229" x2="50" y2="248" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><text x="58" y="243" font-size="12" text-anchor="start">아니오</text><rect x="10" y="250" width="350" height="32" rx="14" fill="#888" fill-opacity="0.1" stroke="#888" stroke-width="1.3"/><text x="22" y="271" font-size="12" text-anchor="start">⑤ 작은 MCU · 단순 · 팀이 FreeRTOS에 익숙?</text><line x1="362" y1="266" x2="393" y2="266" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><text x="374" y="261" font-size="12" text-anchor="start">예</text><rect x="395" y="250" width="275" height="32" rx="6" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="405" y="271" font-size="12" text-anchor="start">FreeRTOS (+ 필요 시 SafeRTOS 경로)</text><line x1="50" y1="283" x2="50" y2="302" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><text x="58" y="297" font-size="12" text-anchor="start">아니오</text><rect x="10" y="304" width="350" height="32" rx="14" fill="#888" fill-opacity="0.1" stroke="#888" stroke-width="1.3"/><text x="22" y="325" font-size="12" text-anchor="start">⑥ Rust 팀 · 메모리 안전 최우선?</text><line x1="362" y1="320" x2="393" y2="320" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><text x="374" y="315" font-size="12" text-anchor="start">예</text><rect x="395" y="304" width="275" height="32" rx="6" fill="#888" fill-opacity="0.14" stroke="#888" stroke-width="1.3"/><text x="405" y="325" font-size="12" text-anchor="start">Embassy · RTIC (I8, I9)</text><text x="340" y="364" font-size="12" text-anchor="middle">어느 경로든 마지막에: footprint 측정(A3) · 인력 시장 · 라이선스 검토 · 5년 뒤 지원 확인</text></svg>
```

그림 10 — RTOS 결정 흐름. 위 질문일수록 "어길 수 없는 제약"이다. 인증이 필수면 다른 장점은 의미가 없고, 칩 벤더가 이미 정했다면 거스르는 비용이 크다.

### 8.4 Don 맞춤 사례 4개

#### 사례 1 — 드론 비행 제어기 스타트업 (STM32H7, IMU 8 kHz, 시제품 6개월)

- 질문 ①: 초기엔 인증 불필요(국방 납품 시 나중에 DO-178C 이야기가 나올 수 있음).
- 질문 ③: **PX4나 ArduPilot을 쓰면 RTOS는 이미 정해져 있다** — PX4 → NuttX, ArduPilot → ChibiOS. 스타트업이 비행 스택을 처음부터 짜는 것은 드물다.
- 자체 스택이라면: 센서·모터 루프는 bare-metal ISR + FreeRTOS(팀이 익숙, 작음) 또는 Zephyr(여러 보드·CAN·USB·MCUboot를 한 번에). 6개월 시제품이면 **팀이 아는 것**이 이긴다.
- 리스크 메모: ChibiOS는 GPL3 — 자체 상용 비행 스택에 쓰려면 상용 라이선스 필요.
- 결론 예: "PX4 위에서 개발(NuttX), 우리 보조 MCU(ESC 텔레메트리 허브)는 Zephyr."

#### 사례 2 — 자동차 존 컨트롤러 (Cortex-R52 다중 코어, ASIL B~D)

- 질문 ①: 예 — ISO 26262. 차량 ECU 업계 표준인 **AUTOSAR Classic OS(SC3/SC4: 메모리·타이밍 보호)** 가 기본값. 벤더 제품(Vector, ETAS, EB) 중 고른다.
- 비 AUTOSAR 영역(예: 고속 게이트웨이 코어)은 SafeRTOS·QNX·PikeOS(하이퍼바이저로 혼합 중요도)가 후보.
- Zephyr·ThreadX의 인증 진행은 **2027년 이후 다시 평가** — 오늘 제품 계획에 "인증 예정"을 넣으면 일정 리스크다.
- Rivian 면접 포인트: "ASIL D 커널"보다 "safety manual의 가정을 시스템이 어떻게 지키나"를 말할 수 있어야 한다(O2).

#### 사례 3 — BLE 웨어러블 (nRF54/nRF53급, 배터리 2주, 소량 다품종)

- 질문 ②: Nordic이면 **nRF Connect SDK = Zephyr**. BLE 스택·전원 관리·MCUboot·DFU가 한 SDK에 있고 벤더가 지원한다. 거스를 이유가 거의 없다.
- ESP32 계열이면 ESP-IDF = FreeRTOS(SMP 변형).
- CRA: EU 판매 제품이므로 **보안 업데이트 기간·SBOM·취약점 신고 창구**가 필요하다. Zephyr의 SBOM 생성·PSIRT·LTS(+ 필요하면 상용 15년)가 이 비용을 줄인다.

#### 사례 4 — SSD 컨트롤러 (Don의 홈그라운드: 멀티코어 Cortex-R, 성능·지연 최우선)

- 엔터프라이즈 SSD 펌웨어는 대부분 **자체 executive**(이벤트 구동 + 코어별 역할 분담)다. 범용 RTOS의 태스크 전환·큐 오버헤드조차 IOPS에 보이기 때문이다(Don의 경험으로 확인할 부분).
- 질문 ④·⑤에서: 관리 코어(호스트 명령 파싱, 로그, 열 관리)는 ThreadX·FreeRTOS(SMP)·Zephyr도 후보다. 특정 SSD 회사가 어떤 RTOS를 쓰는지는 공개 자료로 **확인 못 함**.
- 결론 예: "데이터 경로 코어는 bare-metal executive 유지, 관리 코어만 RTOS — 경계는 메일박스·공유 메모리 큐(P4)."

---

## 9. 2026 트렌드 — contents §5를 한 단계 더

contents.md §5의 트렌드 표를 이 노트의 확인 결과로 보강한다. 각 항목 끝에 이어지는 모듈 ID를 붙였다.

### 9.1 Zephyr의 가속 — "사실상 표준" 쪽으로 (H1, H2)

- **사실**: 4.4.0(2026-04-14, 930명+ 기여), 연 2회 주기 전환, 4.5는 2026-10-25 현재 rc3, LTS4 = 4.6(2027-04 예정), 보드 1,014개(예제 2), contributors 4,725(예제 5), Canonical의 상용 LTS 발표(2026-09-21, 최대 15년, Ubuntu Pro for Devices).
- **해석**: Linux가 서버에서 그랬듯, 칩 벤더들이 "각자 RTOS를 유지하는 비용"을 공동 프로젝트로 옮기고 있다. Canonical의 진입은 **"Zephyr를 Ubuntu처럼 사서 쓰는"** 시장이 생겼다는 신호다. 5년 LTS로는 차량·산업 장비 수명(10~15년)을 못 덮는데 그 빈칸을 상용 배포판이 메운다.
- **반대 신호**: 배울 것이 많고, 작은 팀·작은 MCU에서는 FreeRTOS가 여전히 빠르다. 설문(2024)에서도 FreeRTOS 29% > Zephyr 21%.

### 9.2 안전 인증 경쟁 — 오픈소스 커널 + 판매되는 증거 (O2, I2)

- Zephyr: concept approval(2024) → SIL 3/SC 3, 약 15k LOC 범위, 2026-10 현재 미완료.
- ThreadX: 6.1.x 인증서 보유(이전), 6.4.1 인증을 2026년 목표로 진행(2026-04 메일 기준 "on track"), ThreadX Alliance로 증거 판매.
- FreeRTOS: 커널 자체 인증 계획 대신 SafeRTOS 경로.
- **해석**: "커널 무료 + 인증 증거·안전 매뉴얼은 회원/상용"이 표준 비즈니스 모델이 되고 있다. 면접에서 "오픈소스 RTOS로 ASIL D 가능하냐"에는 **"오늘은 상용 인증 커널, 내일은 Zephyr/ThreadX 인증판을 재평가"** 로 답한다.

### 9.3 규제가 펌웨어를 바꾼다 — EU CRA (L7)

```svg
<svg viewBox="0 0 680 218" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340" y="18" font-size="13" text-anchor="middle" font-weight="bold">EU Cyber Resilience Act 일정 — 펌웨어 팀 기준</text><line x1="40" y1="88" x2="640" y2="88" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/><line x1="97" y1="84" x2="97" y2="92" stroke="currentColor" stroke-width="1.3"/><text x="97" y="106" font-size="12" text-anchor="middle">2025</text><line x1="269" y1="84" x2="269" y2="92" stroke="currentColor" stroke-width="1.3"/><text x="269" y="106" font-size="12" text-anchor="middle">2026</text><line x1="440" y1="84" x2="440" y2="92" stroke="currentColor" stroke-width="1.3"/><text x="440" y="106" font-size="12" text-anchor="middle">2027</text><line x1="612" y1="84" x2="612" y2="92" stroke="currentColor" stroke-width="1.3"/><text x="612" y="106" font-size="12" text-anchor="middle">2028</text><circle cx="87" cy="88" r="6" fill="#888" fill-opacity="0.85"/><line x1="87" y1="81" x2="87" y2="68" stroke="currentColor" stroke-width="1.3"/><text x="87" y="64" font-size="12" text-anchor="middle" font-weight="bold">2024-12-10 발효</text><text x="87" y="49" font-size="12" text-anchor="middle">규정 2024/2847</text><circle cx="344" cy="88" r="6" fill="#4a7bd0" fill-opacity="0.85"/><line x1="344" y1="110" x2="344" y2="116" stroke="currentColor" stroke-width="1.3"/><text x="344" y="130" font-size="12" text-anchor="middle" font-weight="bold">2026-06-11</text><text x="344" y="146" font-size="12" text-anchor="middle">인증기관 조항</text><circle cx="388" cy="88" r="6" fill="#d0564a" fill-opacity="0.85"/><line x1="388" y1="81" x2="388" y2="68" stroke="currentColor" stroke-width="1.3"/><text x="388" y="64" font-size="12" text-anchor="middle" font-weight="bold">2026-09-11 신고 의무</text><text x="388" y="49" font-size="12" text-anchor="middle">24h · 72h · 14일</text><circle cx="602" cy="88" r="6" fill="#e08a3c" fill-opacity="0.85"/><line x1="602" y1="110" x2="602" y2="116" stroke="currentColor" stroke-width="1.3"/><text x="602" y="130" font-size="12" text-anchor="middle" font-weight="bold">2027-12-11 전면 적용</text><text x="602" y="146" font-size="12" text-anchor="middle">설계·SBOM·업데이트</text><rect x="10" y="160" width="660" height="50" rx="6" fill="#888" fill-opacity="0.08" stroke="#888" stroke-width="1.3"/><text x="20" y="180" font-size="12" text-anchor="start">신고 대상: 적극 악용되는 취약점 · 심각한 사고 → CSIRT/ENISA 단일 신고 플랫폼</text><text x="20" y="200" font-size="12" text-anchor="start">펌웨어 팀이 할 일: SBOM, 취약점 접수 창구(PSIRT), 서명된 업데이트(L2), 지원 기간 명시</text></svg>
```

그림 11 — EU CRA 일정. 2026-09-11부터 신고 의무가 이미 시행 중이고, 2027-12-11에 설계·업데이트·문서 의무가 전면 적용된다.

- **규정 (EU) 2024/2847**, 2024-12-10 발효. 제14조(신고)는 **2026-09-11**부터, 나머지는 **2027-12-11**부터 적용. 인증기관 관련 조항은 2026-06-11부터.
- **신고 기한**: 적극 악용되는 취약점 → 24시간 조기 경보, 72시간 통지, 수정·완화책이 나온 뒤 14일 안에 최종 보고. 심각한 사고 → 24시간, 72시간, 통지 후 1개월 안에 최종 보고. CSIRT와 ENISA의 단일 신고 플랫폼으로.
- 벌금은 최대 1,500만 유로 또는 전 세계 매출 2.5%.
- **오픈소스 스튜어드(open-source software steward)**: 상업적 사용을 위한 FOSS를 지속적으로 지원하는 법인(재단 등)은 가벼운 의무(보안 정책, 당국 협조)만 지고 행정 벌금에서 제외된다. 즉 **Zephyr·Eclipse·ASF 같은 재단이 아니라, 그 RTOS를 제품에 넣어 파는 회사(제조사)가 의무의 중심**이다.
- **펌웨어 팀의 할 일**: SBOM(예제 3의 SPDX 작업이 출발점), 의존성 취약점 모니터링, PSIRT 창구, 서명된 업데이트 경로(L2 MCUboot), 지원 기간 명시. RTOS 선택 기준에 "LTS 기간 ≥ 제품 지원 기간"이 들어가는 이유다.

### 9.4 Rust 임베디드 (I7~I11, S8)

- Embassy GitHub stars 1만+(예제 5), RTIC 2.4.0(2026-10-21), Tock 2.3.0(2026-10-08), Ferrocene 인증 컴파일러, Zephyr Rust 모듈.
- **해석**: "RTOS를 Rust로 바꾼다"보다 **"C RTOS 위에 Rust 앱" → "Rust 실행기(Embassy)로 새 제품"** 의 순서로 들어온다. 인증 제품은 Ferrocene + 상용 RTOS 조합이 먼저 나온다. 2026년 비행 제어기 본 루프를 Embassy로 짜는 회사는 아직 소수다(I8에서 실행 모델 비교).

### 9.5 RISC-V MCU (B10)

- Zephyr `arch/`에 riscv가 있고(예제 2), FreeRTOS 11.3.0이 RISC-V 포트에 **FPU·Vector 컨텍스트 저장**을 추가했다(`History.txt`). Raspberry Pi RP2350(2024-08-08 발표)은 Cortex-M33 두 개와 Hazard3 RISC-V 두 개를 함께 넣고 부팅 때 고른다. RT-Thread는 중국 RISC-V 칩의 기본 RTOS 역할을 한다.
- **해석**: RTOS 입장에서 RISC-V는 "포트 하나 더"가 아니라 **인터럽트 컨트롤러(CLINT·PLIC·CLIC)가 칩마다 다른** 문제다. 포트 디렉터리 수(예제 1에서 3개)보다 "내 칩의 인터럽트 구조를 지원하나"를 본다.

### 9.6 MCU 위의 AI (Q6, B7, S9)

- Arm **Ethos-U85**: 128~2048 MAC, 1 GHz에서 최대 **4 TOPS**, transformer 지원, Corstone-320(Cortex-M85 + Mali-C55 ISP)과 함께 발표. 초기 채택사 Alif·Infineon.
- **해석**: NPU 추론은 RTOS에 **"긴 DMA·NPU 작업 완료를 태스크에 알리고, 그동안 제어 루프를 지키는"** 문제를 준다(E6). RTOS 선택에서는 벤더 NPU 드라이버가 어느 RTOS에 먼저 나오나(대개 벤더 SDK의 RTOS)가 실무 변수다.

### 9.7 멀티코어 · 이종 MCU (C10, P4, H2)

- FreeRTOS: V11.0.0(2023-12-18) SMP 메인라인, 11.3.0의 Cortex-R82 SMP 포트, 11.3.1의 Armv8-M SMP. Zephyr: `CONFIG_SMP`(예제 8), sysbuild로 앱 코어 + 네트워크 코어 이미지를 함께 빌드. RTEMS는 SMP 스케줄러 여러 종(예제 9).
- **해석**: MCU에서도 "SMP 한 OS"와 "코어마다 다른 OS(AMP)"를 고르는 문제가 생겼다. 존 컨트롤러·드론 FC의 Cortex-R52/R82는 하이퍼바이저나 AMP 쪽이 많다(O6, P6).

### 9.8 §5 표와 다르거나 보강한 점

| §5 항목 | 이 노트의 확인 결과 |
|---|---|
| Zephyr 4.5 = 2026-10 예정 | 2026-10-25 현재 **rc3**, 아직 미출시 |
| Canonical Zephyr 26.04 LTS (2026-09-21) | 2026-09-21에 **"곧 나올 릴리스(upcoming)"로 발표**. 출시 날짜는 확인 못 함 |
| FreeRTOS 202604 LTS: MPU, MQTT v5, coreSNTP 2038 | 모두 확인 (AWS 발표, coreMQTT CHANGELOG). 공개일은 2026-04-29, AWS 발표 게시 2026-05-01 |
| Zephyr 인증: 약 15k LOC | 확인 (inovex). 인증 기관 이름은 확인 못 함 |
| ThreadX v6.4.1 인증 2026 목표 | 확인 (2026-04-15 메일). 첫 MIT 릴리스가 6.4.1(2024-02-29)이라는 점 추가 |
| Ethos-U85 최대 4 TOPS | 확인 (Arm 제품 페이지) |

---

## 10. RTOS 커널 안에서는 무슨 일이 일어나나

지형도 노트에서 커널 안을 보는 이유는 하나다: **"설계 선택"이 RTOS마다 실제로 얼마나 다른지** 소스로 확인해서, 비교 글의 과장을 걸러내기 위해서다. 결론부터 말하면 **스케줄링 핵심은 거의 같고, 차이는 IPC 구현 방식·보호·POSIX·SMP에 있다.**

### 10.1 설계 선택 비교표

| RTOS | 스케줄러 | IPC 스타일 | 메모리 보호 | SMP | POSIX | 근거 |
|---|---|---|---|---|---|---|
| FreeRTOS | 고정 우선순위 선점 + 같은 우선순위 time slicing, 우선순위별 ready list | 큐가 기본 — 세마포어·뮤텍스도 큐 위에 구현, 뮤텍스는 우선순위 상속 | MPU 포트 (`ARM_CM3_MPU`, `ARM_CM4_MPU`, `ARM_CRx_MPU`, Armv8-M) | V11부터 (`configNUMBER_OF_CORES`) | 커널에 없음 | 소스 (예제 8) |
| Zephyr | 고정 우선순위, 음수 = 협력형, 백엔드 선택 (simple/scalable/multiq), 같은 우선순위 안 EDF 옵션 | 객체별 구현 (sem, mutex, msgq, pipe, mailbox, events, condvar) | userspace + MPU/MMU | `CONFIG_SMP` | `lib/posix` (pthread, mqueue, sched 등) | 소스 (예제 8) |
| ThreadX | 고정 우선순위 + **preemption-threshold** | 큐·세마포어·뮤텍스·이벤트 플래그 | Module Manager (MPU/MMU) | 별도 SMP 커널 (`common_smp`) | 호환 계층 (`utility/rtos_compatibility_layers/posix`, FreeRTOS·OSEK 계층도 있음) | 소스 (예제 9) |
| NuttX | 우선순위 선점 + RR + **sporadic** | POSIX 메시지 큐·세마포어·시그널 | flat / protected / kernel 빌드 | `CONFIG_SMP` | 핵심 설계 원칙 | 소스 (예제 9) |
| RT-Thread | 우선순위 선점 + RR | 세마포어·뮤텍스·이벤트·메일박스·메시지 큐 | RT-Smart (MMU, `lwp`) | `RT_USING_SMP` | `components/libc/posix` | 소스 (예제 9) |
| ChibiOS | 우선순위 선점 (RT), 초소형 NIL 커널 별도 | 메시지·메일박스·이벤트·세마포어 | 커널 기준 확인 못 함 | `CH_CFG_SMP_MODE` 설정 존재 (trunk `chchecks.h`) | 없음 | 소스 (예제 9) |
| RTEMS | 플러그인 스케줄러: 우선순위, simple, **EDF**, SMP 변형들 | Classic API + POSIX | 단일 주소 공간 (프로세스 격리 없음이 기본) | 다수 SMP 스케줄러 | 있음 (`cpukit/posix`) | 소스 (예제 9) + 문서 |
| µC/OS-III | 고정 우선순위 + RR 옵션, 뮤텍스 우선순위 상속 | 세마포어·뮤텍스·메시지 큐·이벤트 플래그·태스크 내장 큐 | 없음 (커널 기준) | 없음 | 없음 | 소스 (예제 9) |
| QNX | 우선순위 선점 + FIFO/RR/sporadic | **동기 메시지 패싱** (마이크로커널) | 프로세스 (MMU) | 있음 | POSIX API가 기본 | 문서 |
| AUTOSAR OS | 정적 우선순위, 빌드 전 확정 | 이벤트·리소스 (priority ceiling) | SC3/SC4 | 멀티코어 규격 | 없음 | 규격 문서 (I5) |

### 10.2 예제 8 — FreeRTOS · Zephyr 설계를 소스에서 확인

무엇을 확인하나: 위 표의 FreeRTOS·Zephyr 칸을 grep으로 하나씩 증명한다.

```sh
#!/bin/bash
# e8: verify kernel design claims straight from source (FreeRTOS V11.3.2, Zephyr v4.4.2)
F=/var/rtoslab/src/FreeRTOS-Kernel; Z=/var/rtoslab/zephyrproject/zephyr
c() { printf "%-34s %s\n" "$1" "$2"; }
echo "== FreeRTOS"
c "ready lists (one per priority)" "$(grep -o 'pxReadyTasksLists\[ *configMAX_PRIORITIES *\]' $F/tasks.c | head -1)"
c "CLZ-based task selection" "$(grep -c 'configUSE_PORT_OPTIMISED_TASK_SELECTION' $F/tasks.c) refs in tasks.c"
c "time slicing switch" "$(grep -c 'configUSE_TIME_SLICING' $F/tasks.c) refs in tasks.c"
c "SMP (configNUMBER_OF_CORES > 1)" "$(grep -c 'configNUMBER_OF_CORES > 1' $F/tasks.c) #if blocks in tasks.c"
c "semaphore = queue underneath" "$(grep -o 'xQueueGenericCreate( ( UBaseType_t ) 1, semSEMAPHORE_QUEUE_ITEM_LENGTH' $F/include/semphr.h | head -1)"
c "MPU wrapper ports" "$(ls -d $F/portable/GCC/*MPU* $F/portable/GCC/ARM_CM33 2>/dev/null | xargs -n1 basename | tr '\n' ' ')"
c "mutex priority inheritance" "$(grep -c 'xTaskPriorityInherit' $F/queue.c) calls in queue.c"
c "POSIX layer in kernel repo" "$(grep -rl 'pthread_create' $F/*.c $F/include | wc -l) files"
echo "== Zephyr"
c "scheduler backends (Kconfig)" "$(grep -oE '^config SCHED_(SIMPLE|SCALABLE|MULTIQ|DEADLINE)' $Z/kernel/Kconfig* | sed 's/.*config //' | tr '\n' ' ')"
c "cooperative prios (negative)" "$(grep -m1 -oE 'config NUM_COOP_PRIORITIES' $Z/kernel/Kconfig)"
c "SMP / userspace / timeslice" "$(grep -hoE '^config (SMP|USERSPACE|TIMESLICING)$' $Z/kernel/Kconfig* $Z/arch/Kconfig | sed 's/config //' | sort -u | tr '\n' ' ')"
c "mutex priority inheritance" "$(grep -c 'adjust_owner_prio' $Z/kernel/mutex.c) uses of adjust_owner_prio in mutex.c"
c "context switch entry" "$(grep -hoE 'static inline int z_swap\(' $Z/kernel/include/kswap.h | head -1)"
c "IPC objects (kernel/*.c)" "$(cd $Z/kernel; ls msg_q.c pipe.c mailbox.c queue.c sem.c mutex.c events.c condvar.c 2>/dev/null | tr '\n' ' ')"
c "POSIX API (lib/posix/options)" "$(ls $Z/lib/posix/options/*.c | wc -l) .c files, e.g. $(cd $Z/lib/posix/options; ls *.c | grep -m3 -E 'pthread|sched|mqueue' | tr '\n' ' ')"
```

```sh
limactl shell elinux -- bash /Users/donh/.rtos_scratch/A6/e8_design_grep.sh
```

```text
== FreeRTOS
ready lists (one per priority)     pxReadyTasksLists[ configMAX_PRIORITIES ]
CLZ-based task selection           7 refs in tasks.c
time slicing switch                2 refs in tasks.c
SMP (configNUMBER_OF_CORES > 1)    87 #if blocks in tasks.c
semaphore = queue underneath       xQueueGenericCreate( ( UBaseType_t ) 1, semSEMAPHORE_QUEUE_ITEM_LENGTH
MPU wrapper ports                  ARM_CM33 ARM_CM3_MPU ARM_CM4_MPU ARM_CRx_MPU 
mutex priority inheritance         1 calls in queue.c
POSIX layer in kernel repo         0 files
== Zephyr
scheduler backends (Kconfig)       SCHED_DEADLINE SCHED_SIMPLE SCHED_SCALABLE SCHED_MULTIQ 
cooperative prios (negative)       config NUM_COOP_PRIORITIES
SMP / userspace / timeslice        SMP TIMESLICING USERSPACE 
mutex priority inheritance         4 uses of adjust_owner_prio in mutex.c
context switch entry               static inline int z_swap(
IPC objects (kernel/*.c)           condvar.c events.c mailbox.c msg_q.c mutex.c pipe.c queue.c sem.c 
POSIX API (lib/posix/options)      40 .c files, e.g. mqueue.c pthread.c sched.c 
```

출력에서 볼 것:

- FreeRTOS: ready list가 **우선순위마다 하나씩**(`pxReadyTasksLists[ configMAX_PRIORITIES ]`)이고, CLZ 명령으로 가장 높은 우선순위를 찾는 최적화(`configUSE_PORT_OPTIMISED_TASK_SELECTION`)가 있다. **세마포어는 `xQueueGenericCreate(1, 0, …)`** — 길이 1, 항목 크기 0인 큐다. "FreeRTOS는 큐가 모든 IPC의 바탕"이 소스로 확인된다. 뮤텍스 우선순위 상속은 `queue.c`가 `xTaskPriorityInherit()`을 부르는 한 곳에서 시작된다. 커널 저장소에는 POSIX 계층이 없다(0 files).
- Zephyr: 스케줄러 백엔드 3종(`SCHED_SIMPLE`·`SCALABLE`·`MULTIQ`) + `SCHED_DEADLINE`(같은 우선순위 안의 EDF)이 Kconfig 선택지다. 협력형 우선순위(`NUM_COOP_PRIORITIES`, 음수 우선순위), `SMP`·`USERSPACE`·`TIMESLICING`이 모두 커널 옵션이다. IPC는 **객체마다 별도 파일**이다. 컨텍스트 스위치 진입점은 `kernel/include/kswap.h`의 `z_swap()`. POSIX는 `lib/posix/options/`에 40개 .c 파일로 있다.

### 10.3 예제 9 — 다른 RTOS의 특징도 소스에서

```sh
#!/bin/bash
# e9: same design questions on the other trees (shallow clones of trunk, 2026-10-25)
S=/var/tmp/rtos/A6/src
c() { printf "%-30s %s\n" "$1" "$2"; }
echo "== ThreadX";   c "preemption-threshold API" "$(ls $S/threadx/common/src | grep preemption_change | tr "\n" " ")"
c "SMP kernel variant dir" "$(ls -d $S/threadx/common_smp 2>/dev/null | xargs -r basename)"
c "module (MPU/MMU) manager" "$(ls -d $S/threadx/common_modules 2>/dev/null | xargs -r basename)"
echo "== NuttX";     c "sched policies (Kconfig)" "$(grep -oE '^config (SCHED_SPORADIC|RR_INTERVAL|SMP)$' $S/nuttx/sched/Kconfig | sed 's/config //' | tr '\n' ' ')"
c "POSIX: pthread_create impl" "$(ls $S/nuttx/sched/pthread/pthread_create.c | xargs basename)"
c "build modes (BUILD_*)" "$(grep -oE '^config BUILD_(FLAT|PROTECTED|KERNEL)$' $S/nuttx/Kconfig | sed 's/config //' | tr '\n' ' ')"
echo "== RT-Thread"; c "SMP switch" "$(grep -rhoE '^config RT_USING_SMP$' $S/rt-thread/src/Kconfig | head -1)"
c "smart (MMU, user mode) dir" "$(ls -d $S/rt-thread/components/lwp 2>/dev/null | xargs -r basename)"
echo "== ChibiOS";   c "RT vs NIL kernels" "$(ls $S/ChibiOS/os | grep -E '^(rt|nil)$' | tr '\n' ' ')"
echo "== RTEMS";     c "schedulers (score/src)" "$(ls $S/rtems/cpukit/score/src | grep -oE '^scheduler(edf|priority|simple|strongapa)[a-z]*smp\.c$|^scheduleredf\.c$' | sort -u | tr '\n' ' ')"
c "POSIX API dir" "$(ls -d $S/rtems/cpukit/posix | xargs basename)"
echo "== uC/OS-III"; c "round-robin + mutex PI" "$(grep -c 'OS_CFG_SCHED_ROUND_ROBIN_EN' $S/uC-OS3/Source/os_core.c) refs RR, $(grep -c 'OS_TaskChangePrio' $S/uC-OS3/Source/os_mutex.c) refs PI-prio change"
```

```sh
limactl shell elinux -- bash /Users/donh/.rtos_scratch/A6/e9_others_grep.sh
```

```text
== ThreadX
preemption-threshold API       tx_thread_preemption_change.c txe_thread_preemption_change.c 
SMP kernel variant dir         common_smp
module (MPU/MMU) manager       common_modules
== NuttX
sched policies (Kconfig)       SMP RR_INTERVAL SCHED_SPORADIC 
POSIX: pthread_create impl     pthread_create.c
build modes (BUILD_*)          BUILD_FLAT BUILD_PROTECTED BUILD_KERNEL 
== RT-Thread
SMP switch                     config RT_USING_SMP
smart (MMU, user mode) dir     lwp
== ChibiOS
RT vs NIL kernels              nil rt 
== RTEMS
schedulers (score/src)         scheduleredf.c scheduleredfsmp.c schedulerpriorityaffinitysmp.c schedulerprioritysmp.c schedulersimplesmp.c 
POSIX API dir                  posix
== uC/OS-III
round-robin + mutex PI         4 refs RR, 3 refs PI-prio change
```

출력에서 볼 것: ThreadX의 **preemption-threshold**(`tx_thread_preemption_change.c`; `txe_`는 인자 검사 래퍼)와 별도 SMP 커널 디렉터리, NuttX의 **sporadic 스케줄링**과 세 가지 빌드 모드, RTEMS의 **EDF와 SMP 스케줄러들**, µC/OS-III의 round-robin과 뮤텍스 우선순위 변경(`OS_TaskChangePrio`) — 비교표의 "특이점" 칸이 전부 실제 파일로 존재한다. 이 특이점들이 각 RTOS의 "팔리는 이유"다(I1~I3에서 자세히).

### 10.4 같은 개념, 다른 파일

```svg
<svg viewBox="0 0 680 312" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340" y="18" font-size="13" text-anchor="middle" font-weight="bold">같은 개념, 다른 파일 — 커널 안 지도 (FreeRTOS V11.3.2 / Zephyr v4.4.2)</text><text x="88" y="44" font-size="12" text-anchor="middle" font-weight="bold">개념</text><text x="298" y="44" font-size="12" text-anchor="middle" font-weight="bold">FreeRTOS</text><text x="550" y="44" font-size="12" text-anchor="middle" font-weight="bold">Zephyr</text><rect x="8" y="52" width="160" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="88" y="69" font-size="12" text-anchor="middle">TCB / 스레드 객체</text><rect x="176" y="52" width="244" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="298" y="69" font-size="12" text-anchor="middle">tasks.c: TCB_t</text><rect x="428" y="52" width="244" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="550" y="69" font-size="12" text-anchor="middle">thread.h: struct k_thread</text><rect x="8" y="86" width="160" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="88" y="103" font-size="12" text-anchor="middle">ready 큐</text><rect x="176" y="86" width="244" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="298" y="103" font-size="12" text-anchor="middle">tasks.c: pxReadyTasksLists[]</text><rect x="428" y="86" width="244" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="550" y="103" font-size="12" text-anchor="middle">sched.c + priority_q.h</text><rect x="8" y="120" width="160" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="88" y="137" font-size="12" text-anchor="middle">컨텍스트 스위치</text><rect x="176" y="120" width="244" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="298" y="137" font-size="12" text-anchor="middle">port.c: xPortPendSVHandler</text><rect x="428" y="120" width="244" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="550" y="137" font-size="12" text-anchor="middle">kswap.h z_swap → swap_helper.S</text><rect x="8" y="154" width="160" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="88" y="171" font-size="12" text-anchor="middle">tick · 타임아웃</text><rect x="176" y="154" width="244" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="298" y="171" font-size="12" text-anchor="middle">tasks.c: xTaskIncrementTick</text><rect x="428" y="154" width="244" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="550" y="171" font-size="12" text-anchor="middle">timeout.c: sys_clock_announce</text><rect x="8" y="188" width="160" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="88" y="205" font-size="12" text-anchor="middle">세마포어 · 뮤텍스</text><rect x="176" y="188" width="244" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="298" y="205" font-size="12" text-anchor="middle">queue.c (큐 위에 구현)</text><rect x="428" y="188" width="244" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="550" y="205" font-size="12" text-anchor="middle">sem.c · mutex.c (별도 객체)</text><rect x="8" y="222" width="160" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="88" y="239" font-size="12" text-anchor="middle">메시지 큐</text><rect x="176" y="222" width="244" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="298" y="239" font-size="12" text-anchor="middle">queue.c: xQueueGenericSend</text><rect x="428" y="222" width="244" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="550" y="239" font-size="12" text-anchor="middle">msg_q.c: z_impl_k_msgq_put</text><rect x="8" y="256" width="160" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="88" y="273" font-size="12" text-anchor="middle">멀티코어</text><rect x="176" y="256" width="244" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="298" y="273" font-size="12" text-anchor="middle">configNUMBER_OF_CORES</text><rect x="428" y="256" width="244" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="550" y="273" font-size="12" text-anchor="middle">CONFIG_SMP</text><text x="340" y="304" font-size="12" text-anchor="middle">자세한 동작: C1 스케줄러 · C2 컨텍스트 스위치 · C5 tick · D 동기화 · C10 SMP</text></svg>
```

그림 12 — FreeRTOS와 Zephyr에서 같은 개념이 사는 곳. FreeRTOS는 `tasks.c`·`queue.c` 두 파일에 거의 모든 것이 모여 있고, Zephyr는 개념마다 파일이 나뉜다. 파일 이름은 V11.3.2 / v4.4.2 소스에서 확인했다(`sys_clock_announce()`는 `system_timer.h`의 inline 래퍼이고 본체는 `timeout.c`의 `sys_clock_announce_locked()`, `xQueueSend()`는 `xQueueGenericSend()`를 부르는 매크로다).

Don에게 번역하면: FreeRTOS는 "한 파일에 executive 전체가 있는 SSD 펌웨어", Zephyr는 "모듈별로 쪼개고 빌드 설정으로 조립하는 펌웨어"다. 읽는 순서도 다르다 — FreeRTOS는 `tasks.c`를 위에서 아래로, Zephyr는 `include/zephyr/kernel.h` API에서 `kernel/sched.c`로 따라 들어간다. 세부는 C1(스케줄러), C2(컨텍스트 스위치), C5(tick), D(동기화), C10(SMP).

---

## 11. 실무에서 다시 보기

### 11.1 장면 1 — "다음 제품 RTOS를 정해 와라" 한 장짜리 문서

선택 문서는 이렇게 쓴다. 감이 아니라 §8의 기준을 표로 채우고, **결정을 뒤집을 조건**을 적는 것이 핵심이다.

```text
RTOS Selection — Product X sensor hub (2026-10)
Constraints : nRF54L (Cortex-M33), BLE + Thread, EU market, 7-year support, no safety cert
Candidates  : Zephyr (via nRF Connect SDK) / FreeRTOS + vendor BLE stack
Decision    : Zephyr 4.4 -> move to LTS4 (4.6) in 2027-04
Why         : vendor SDK is Zephyr-native; MCUboot + DFU in tree; SBOM generation; PSIRT; LTS 5y
Risks       : learning curve (west/Kconfig/DT) -> 2-week onboarding; footprint -> measured 13 KB min
Exit plan   : if LTS4 slips or vendor drops Zephyr -> Canonical LTS or fork with own PSIRT
Revisit if  : safety cert becomes a requirement (re-evaluate SafeRTOS / Zephyr certified kernel)
```

### 11.2 장면 2 — FreeRTOS 제품을 Zephyr로 옮기자는 제안

옮기는 이유가 "유행"이면 반대한다. 옮길 가치가 있는 경우는 (1) 칩을 바꾸는데 새 벤더 SDK가 Zephyr 기반, (2) 여러 제품이 다른 RTOS·HAL을 써서 유지보수가 폭발, (3) CRA·LTS 요구를 기존 스택이 못 맞춤. 이전 비용은 API 번역(A5), 드라이버 재작성(J), 빌드·CI 재구성(H2, H8)이다. 같은 일을 하는 최소 앱 크기는 비슷하므로(예제 7) footprint가 반대 근거가 되는 경우는 적다.

### 11.3 장면 3 — 보안팀: "우리 펌웨어 SBOM과 CRA 대응 계획을 내라"

RTOS 팀이 할 일: 커널·미들웨어·벤더 HAL의 **버전과 라이선스 목록**(예제 3처럼 SPDX 수집), 각 구성 요소의 **보안 공지 구독**(Zephyr 보안 공지, AWS FreeRTOS 공지, NuttX·ASF 보안), **24시간 안에 판단할 수 있는 체계**(어떤 제품·버전이 영향을 받는지 바로 알 수 있는 빌드 메타데이터). 버전 문자열을 `tskKERNEL_VERSION_NUMBER`에서 가져오면 틀린다(§7.3).

### 11.4 장면 4 — 면접관: "우리는 PX4를 씁니다. NuttX 경험이 없는데요?"

답의 방향: "NuttX는 POSIX형이라 Embedded Linux 경험이 그대로 통한다(`/dev` 드라이버, `open/ioctl`, 워크 큐). RTOS 핵심 — 우선순위·ISR 규칙·우선순위 역전·스택 — 은 FreeRTOS·Zephyr와 같다. PX4의 uORB와 work queue를 먼저 읽겠다." 이것이 지형도를 아는 사람의 답이다(I1, K7).

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| "오픈소스니까 라이선스 문제 없음" | 출하 직전 법무가 막음 | GPL 커널(ChibiOS RT) 또는 EULA 예제 파일 혼입 (예제 3) | SPDX 스캔을 CI에, 커널·예제·HAL을 따로 확인 |
| "ThreadX는 인증받았다"로 계획 | 인증 감사에서 버전 불일치 | 인증서는 6.1.x, 쓰는 것은 6.4.x/6.5.x | 인증 버전·구성·safety manual을 계획서에 고정 |
| "Zephyr 인증 곧 나온다"를 일정에 반영 | 일정 지연 | 2026-10 현재 미완료, 날짜 미정 | 오늘은 상용 인증 커널, 인증판 나오면 재평가 |
| GitHub stars로 RTOS 순위 매김 | FreeRTOS를 "작은 프로젝트"로 오판 | stars ≠ 배포량, 저장소 이전 시점 차이 (예제 5) | 지표별 의미 표(§7.5)로 여러 지표를 함께 본다 |
| 벤더 EOL 리스크 무시 | Mbed OS 제품이 패치 없이 남음 | 회사 전략 변경 | 선택 문서에 Exit plan 한 줄 |
| LTS 기간 < 제품 지원 기간 | CRA 대응 중 커널 패치가 끊김 | FreeRTOS LTS 2년, Zephyr LTS 5년 | 상용 연장 지원 또는 LTS 이전 계획을 미리 |
| 런타임 버전 문자열을 SBOM에 사용 | SBOM에 V11.1.0+로 기록 | `tskKERNEL_VERSION_NUMBER` 미갱신 (예제 7) | 빌드 시스템이 태그·패키지 버전을 주입 |
| "Zephyr는 무겁다" 단정 | 불필요하게 FreeRTOS로 회귀 | 기본·풀 설정 기준의 인상 | 같은 기능으로 측정 (예제 7, A3) |
| 보드 수만 보고 선택 | 필요한 드라이버가 없음 | 보드 지원 ≠ 드라이버 완성도 | `drivers/`와 샘플 존재 확인, 벤더 SDK 확인 |
| "QNX는 MCU RTOS" | Cortex-M 설계에 QNX 후보를 넣음 | QNX는 MMU가 있는 프로세서용 | CPU 등급(M/R/A)부터 나눠서 후보를 고른다 |

---

## 13. 면접에서 이렇게 말한다

**Q.** "Why would you choose Zephyr over FreeRTOS — or the other way around?"

**A.** "커널 성능 차이는 작다. 차이는 플랫폼이다. Zephyr는 디바이스 트리·드라이버 모델·무선 스택·MCUboot·테스트 도구·SBOM까지 한 저장소에 있고, LF 재단 거버넌스와 5년 LTS가 있으며 칩 벤더가 직접 보드를 넣는다(보드 1,000개+). 대가는 학습 곡선이다. FreeRTOS는 작고 단순하고 거의 모든 칩에 포팅되어 있으며 MIT다. 대신 드라이버·빌드는 벤더 HAL에 맡긴다. 그래서 무선·다품종·장기 유지보수·CRA 대응이면 Zephyr, 작은 MCU·단일 제품·팀이 익숙하면 FreeRTOS, 그리고 벤더 SDK가 이미 정했다면 그걸 따른다."

> The kernels themselves are close — both are fixed-priority preemptive, and context-switch cost is in the same range. The difference is the platform. Zephyr gives you devicetree, a driver model, networking and BLE stacks, MCUboot, Twister for CI and SBOM generation in one tree, under Linux Foundation governance with a five-year LTS, and silicon vendors upstream their own boards — over a thousand of them. The cost is the learning curve. FreeRTOS is tiny, MIT-licensed and ported to almost everything, but drivers and build are left to the vendor HAL. So for a connected product family with long maintenance and CRA obligations I'd lean Zephyr; for a small single-purpose MCU with a team that knows FreeRTOS, FreeRTOS. And if the chip vendor's SDK already picked one — nRF Connect is Zephyr, ESP-IDF is FreeRTOS — I'd follow it.

**꼬리 질문**: "Isn't Zephyr too heavy?" → 같은 기능으로 재면 최소 이미지는 비슷하다(예제 7: text 12.9 KB vs 11.2 KB). 무거운 것은 켠 서브시스템이다.

**Q.** "Can we use an open-source RTOS in an ISO 26262 ASIL D product?"

**A.** "오늘(2026-10) 기준으로 오픈소스 커널 자체의 ASIL D 인증판은 없다. Zephyr는 IEC 61508 SIL 3을 목표로 작업 중이고(2024 concept approval, 약 15k줄 범위), ThreadX는 Microsoft 시절 6.1.x 인증서가 있고 6.4.1 인증을 진행 중이다. 그래서 지금 제품이면 SafeRTOS·QNX OS for Safety·PikeOS·AUTOSAR OS 벤더 제품처럼 사전 인증된 커널과 safety manual을 사고, 인증된 버전·구성을 고정해서 쓴다. 오픈소스 인증판이 나오면 다음 플랫폼에서 재평가한다."

> As of today there's no certified build of an open-source kernel I'd put under ASIL D. Zephyr is working toward IEC 61508 SIL 3 with a concept approval from 2024 and a scope of roughly fifteen thousand lines, and Eclipse ThreadX has the old Microsoft-era certificates for the 6.1 line while 6.4.1 is in progress. So for a product starting now I'd buy a pre-certified kernel — SafeRTOS, QNX OS for Safety, PikeOS, or the AUTOSAR OS from our tool vendor — freeze the certified version and configuration, and follow the safety manual's assumptions of use. I'd re-evaluate the open-source options for the next platform once a certified release actually exists.

**꼬리 질문**: "What's a safety manual?" → 인증이 유효하려면 지켜야 할 사용 조건(설정, 금지 API, 검증해야 할 가정)의 목록. SEooC 인증의 핵심 산출물.

**Q.** "What does PX4 run on, and what does ArduPilot run on?"

**A.** "PX4는 비행 제어 보드에서 NuttX를 주 RTOS로 쓰고, Linux 같은 POSIX 환경에서도 돈다. ArduPilot은 STM32 보드에서 ChibiOS를 쓴다(2018년 Copter 3.6부터). Betaflight는 RTOS 없이 자체 협력형 스케줄러다. NuttX는 POSIX형이라 Linux 경험이 그대로 통하고, ChibiOS는 GPL3이라 상용 자체 스택에는 라이선스 검토가 필요하다."

> PX4 uses NuttX as its primary RTOS on flight-controller boards, and it also runs on POSIX targets like Linux. ArduPilot runs on ChibiOS on STM32 boards — that started with Copter 3.6 in 2018. Betaflight doesn't use an RTOS at all; it has its own cooperative priority scheduler. Practically, NuttX feels like a small Unix — /dev drivers, POSIX threads — so Linux experience transfers, while ChibiOS is GPL3, which matters if you build a proprietary stack on it.

**Q.** "How does the EU Cyber Resilience Act affect firmware teams?"

**A.** "2026년 9월 11일부터 이미 신고 의무가 시행 중이다: 적극 악용되는 취약점은 24시간 조기 경보, 72시간 통지, 수정 후 14일 안에 최종 보고. 2027년 12월 11일부터는 보안 설계, 지원 기간 동안의 보안 업데이트, SBOM 같은 의무가 전면 적용된다. 의무는 제조사에 있다 — 재단은 가벼운 '오픈소스 스튜어드' 의무만 진다. 그래서 RTOS를 고를 때 LTS 기간, 보안 공지·CVE 프로세스, SPDX 품질을 기준에 넣고, 펌웨어에는 서명된 업데이트 경로와 정확한 버전 메타데이터를 넣는다."

> The reporting obligations already apply since September 11, 2026: for an actively exploited vulnerability you owe an early warning within 24 hours, a notification within 72 hours, and a final report within 14 days of a fix. From December 11, 2027 the full set applies — secure-by-design, security updates for the support period, technical documentation including an SBOM. The obligations sit with the manufacturer; foundations only carry the lighter open-source-steward duties. So in RTOS selection I now weigh LTS length, the project's security-advisory and CVE process, and how clean its SPDX metadata is, and in the firmware itself I make sure there's a signed update path and accurate version metadata.

**Q.** "What's an LTS release, and why does it matter for choosing an RTOS?"

**A.** "기능 추가를 멈추고 보안·버그 수정만 오래 해 주는 릴리스다. Zephyr LTS는 최소 5년(3.7은 2029-07까지, 다음은 2027-04의 4.6), FreeRTOS LTS는 2년(202604 LTS는 2028-04까지)이다. 제품 지원 기간이 LTS보다 길면 LTS 사이 이전 비용을 계획하거나, Canonical 같은 상용 장기 지원(최대 15년)을 산다. 인증 제품은 LTS가 '감사 가능한 브랜치'가 된다."

> An LTS is a release that stops taking features and gets security and bug fixes for an extended period. Zephyr's LTS is at least five years — 3.7 is supported to mid-2029 and the next one, 4.6, is planned for April 2027 — while a FreeRTOS LTS bundle is two years; 202604 runs to April 2028. If the product's support window is longer than that, you either budget the migration between LTS releases or buy extended support — Canonical now sells Zephyr LTS with up to fifteen years. For certified products the LTS is also the auditable branch.

**Q.** "Would you use Rust or Embassy for a flight controller today?"

**A.** "새 보조 MCU나 내부 도구라면 진지하게 고려한다 — Embassy는 힙 없이 async로 상태 머신을 컴파일 시점에 만들고, 커뮤니티가 빠르게 크고 있다(GitHub stars 1만+). 하지만 비행 제어 본 루프는 PX4·ArduPilot 생태계, 인증 경로, 팀 역량 때문에 오늘은 C RTOS가 현실적이다. 단계적으로: C RTOS 위 Rust 모듈 → 새 제품의 Rust 실행기. Ferrocene 같은 인증 컴파일러가 그 경로를 열고 있다."

> For a new auxiliary MCU or internal tooling, yes, seriously — Embassy turns async tasks into compile-time state machines with no heap, and its community is growing fast. For the main flight-control loop today I'd still stay with a C RTOS because of the PX4 and ArduPilot ecosystems, the certification path and the team's skills. The realistic path is incremental: Rust modules on top of the existing RTOS first, then a Rust executor on a new product, with a qualified toolchain like Ferrocene when certification matters.

**Q.** "How do you evaluate the long-term risk of an RTOS vendor or project?"

**A.** "세 가지를 본다. 거버넌스 — 한 회사가 쥐는지, 재단과 여러 멤버가 있는지. 역사 — ThreadX는 두 번 주인이 바뀌었고, Mbed OS는 2026년 7월에 접혔다. 지표 — contributors 수, 릴리스 주기의 규칙성, 보안 공지 이력. 그리고 선택 문서에 '이 RTOS가 접히면 우리는 무엇을 하나'를 한 줄로 적는다."

> I look at three things. Governance — is it one company's product or a foundation project with several members? History — ThreadX changed owners twice, and Arm ended Mbed OS in July 2026, so corporate strategy can end a platform. And health metrics — contributor count, release regularity, the security-advisory track record. Then I write an explicit exit plan into the selection doc: if this project stalls, do we fork and maintain it ourselves, buy extended support, or migrate?

---

## 14. 직접 해보기

1. **개념** — 다음 각 요구에 맞는 후보를 2개씩 고르고 이유를 한 줄로 써라: (a) DO-178C DAL A 항공 디스플레이 컴퓨터, (b) ESP32-S3 스마트 플러그, (c) PX4 기반 산업용 드론 FC, (d) Cortex-M33 BLE 혈당계(IEC 62304 Class C).
   정답/힌트: (a) INTEGRITY-178 · VxWorks 653/PikeOS, (b) ESP-IDF FreeRTOS · Zephyr, (c) NuttX(PX4가 정함), (d) SafeRTOS · embOS-Safe 계열(의료 인증) + 벤더 BLE 스택 — Zephyr는 인증 미완료.
2. **라이선스 과제** — 예제 3 스크립트에 "SPDX도 표준 문구도 없는 파일 목록 상위 10개 경로"를 출력하는 옵션을 추가하고, Zephyr에서 그 12개 파일이 무엇인지 보라.
   정답/힌트: `hint` 대신 경로를 리스트에 모아 정렬한다. 대개 생성 파일·테스트 데이터·아주 짧은 헤더다.
3. **생태계 측정** — 예제 2를 고쳐 Zephyr 보드를 **CPU 아키텍처별**로 세라(board.yml의 SoC 이름 → `soc/` 아래 Kconfig의 `select CPU_CORTEX_M`/`RISCV` 등으로 매핑). RISC-V 보드는 몇 개인가?
   정답/힌트: `soc.yml` 경로의 벤더/시리즈 디렉터리와 `Kconfig.soc`를 연결한다. Zephyr의 `scripts/list_boards.py`가 보드·SoC를 읽는 방식을 참고하면 빠르다.
4. **릴리스 계산** — 예제 6에 Zephyr LTS 3.7의 패치 릴리스(v3.7.1, v3.7.2…) 간격도 출력하게 하라. LTS 패치는 몇 달마다 나오나?
   정답/힌트: `git tag -l "v3.7.*"`의 creatordate를 쓴다. rc 태그는 제외.
5. **footprint 측정** — 예제 7의 Zephyr 앱에 `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000`을 넣어 다시 돌려라. uptime 출력이 어떻게 바뀌고, 왜 그런가?
   정답/힌트: 작성 시 실제로 돌려 보니 `uptime=0 / 13 / 26 ms`가 나왔다(FLASH 12,996 B). 10 ms tick일 때의 20 ms 간격이 13 ms로 줄었다 — 추가 대기가 1 tick(1 ms)로 작아졌기 때문이다(C5: "최소 시간 보장 = 올림 + 1 tick"). 남는 약 2 ms가 어디서 오는지(icount 기준 printk·UART 출력 비용?) 루프 안에 `k_cycle_get_32()`를 찍어 직접 분해해 보라.
6. **선택 문서** — §11.1의 양식으로 "Rivian 존 컨트롤러의 보조 전원 관리 MCU(Cortex-M4, ASIL B)" RTOS 선택 문서를 써라. Exit plan과 Revisit if를 반드시 채운다.
   정답/힌트: AUTOSAR Classic OS(SC3) 또는 SafeRTOS. 인증 버전 고정, 툴 qualification, safety manual 가정 목록이 Risks에 들어간다.
7. **트렌드 추적** — 3개월 뒤(2027-01) 예제 5를 다시 돌려 stars·contributors 변화를 표로 만들고, Zephyr 4.5 출시 날짜와 ThreadX 6.4.1 인증 여부를 확인해 이 노트의 §9.8 표를 갱신하라.
   정답/힌트: 숫자보다 "변화율"을 본다. 인증은 ThreadX Alliance 페이지와 Eclipse 메일링 리스트, Zephyr는 releases 페이지.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| permissive license | 허용적 라이선스 | MIT·Apache·BSD — 고쳐서 팔아도 소스 공개 의무 없음 |
| copyleft | 공유 의무 라이선스 | GPL — 배포하면 수정 소스도 같은 조건으로 공개 |
| linking exception | 링크 예외 | GPL 커널과 링크한 앱을 공개 의무에서 빼 주는 조항 |
| dual licensing | 이중 라이선스 | 같은 소스를 GPL과 상용으로 함께 판매 (ChibiOS) |
| SPDX identifier | SPDX 식별자 | 파일 머리의 표준 라이선스 표기 한 줄 |
| SBOM | 소프트웨어 구성 명세 | 제품에 들어간 구성 요소·버전·라이선스 목록 |
| foundation | 재단 | 프로젝트 상표·인프라를 가진 중립 비영리 단체 (LF, Eclipse, ASF) |
| TSC | 기술 운영 위원회 | 재단 프로젝트의 기술 결정 기구 |
| LTS | 장기 지원 릴리스 | 기능 동결 후 보안·버그 수정만 수년간 |
| EOL | 지원 종료 | 더 이상 패치가 없는 상태 (Mbed OS 2026-07) |
| SIL / ASIL / DAL | 안전 무결성 등급 | IEC 61508 / ISO 26262 / DO-178C의 위험 등급 |
| pre-certified | 사전 인증 | 벤더가 커널을 독립 요소로 미리 인증받아 둔 것 |
| SEooC | 맥락 밖 안전 요소 | 쓰일 시스템을 모르고 가정 아래 개발·인증한 요소 |
| safety manual | 안전 매뉴얼 | 인증이 유효하려면 지켜야 할 사용 조건 목록 |
| concept approval | 개념 승인 | 인증 기관이 인증 계획·방법을 서면으로 승인한 단계 (인증 완료 아님) |
| separation kernel | 분리 커널 | 파티션 간 시간·공간 격리를 보장하는 작은 커널 (INTEGRITY, PikeOS) |
| ARINC 653 | 항공 파티셔닝 규격 | 시간·공간 분할 RTOS API 표준 |
| preemption-threshold | 선점 임계값 | ThreadX 기능 — 일정 우선순위 이하에는 선점당하지 않게 |
| CRA | EU 사이버 복원력법 | 디지털 제품의 보안 설계·업데이트·취약점 신고 의무 |
| open-source steward | 오픈소스 스튜어드 | CRA에서 FOSS를 지원하는 법인 — 가벼운 의무만 |
| PSIRT | 제품 보안 사고 대응팀 | 취약점 접수·분석·공지 창구 |
| Hardware Model v2 | Zephyr 보드 모델 | `boards/<vendor>/<board>/board.yml` 구조 |

---

## 16. 요약 & 체크리스트

2026년 RTOS 지형은 커널 성능이 아니라 **플랫폼·거버넌스·인증·수명**으로 갈린다. 오픈소스 쪽에서는 FreeRTOS(AWS, MIT, 가장 넓은 포트, 2년 LTS)와 Zephyr(LF, Apache-2.0, 보드 1,014개, 5년 LTS + 상용 15년, 연 2회 릴리스)가 양대 축이고, ThreadX(Eclipse, MIT, 인증 유산)·NuttX(ASF, POSIX, PX4)·RT-Thread(중국)·ChibiOS(GPL3, ArduPilot)·RTEMS(우주)·µC/OS(Apache, 교과서)가 각자의 자리를 지킨다. 안전 인증이 필요하면 오늘은 SafeRTOS·QNX·VxWorks·INTEGRITY·PikeOS·embOS-Safe·AUTOSAR OS 같은 상용 사전 인증 커널을 사고, Zephyr·ThreadX의 인증 완료를 지켜본다. 역사는 "상용 → permissive + 재단"으로 흘렀고(ThreadX, µC/OS, NuttX), 대기업 RTOS도 접힐 수 있다(Mbed OS). EU CRA는 2026-09-11부터 신고 의무가 시행 중이라 LTS 기간·보안 프로세스·SBOM이 선택 기준에 들어왔다. 숫자(보드 수, LOC, stars, 릴리스 간격)는 직접 뽑되 각 지표가 무엇을 못 재는지 함께 말한다.

- [ ] FreeRTOS·Zephyr·ThreadX·NuttX·ChibiOS·RTEMS의 라이선스와 거버넌스를 한 줄씩 말할 수 있다
- [ ] Zephyr 4.4 날짜, 연 2회 주기, LTS 3.7과 LTS4(4.6) 일정을 말할 수 있다
- [ ] FreeRTOS 202604 LTS의 커널 버전과 지원 종료일을 말할 수 있다
- [ ] "ThreadX는 인증됐다"가 왜 부정확한지(6.1.x vs 6.4.x) 설명할 수 있다
- [ ] Zephyr 인증 상태(concept approval, SIL 3 목표, 약 15k줄, 미완료)를 정확히 말할 수 있다
- [ ] PX4 → NuttX, ArduPilot → ChibiOS, 화성 로버 → VxWorks, MMS → RTEMS, NCS → Zephyr, ESP-IDF → FreeRTOS를 출처와 함께 말할 수 있다
- [ ] 상용 인증 RTOS 5개 이상과 대표 인증(SIL·ASIL·DAL)을 말할 수 있다
- [ ] SPDX 스캔으로 저장소의 실제 라이선스를 확인하고, "RTOS 라이선스 ≠ 모든 파일 라이선스"를 예로 들 수 있다
- [ ] stars·contributors·LOC·보드 수 각각이 못 재는 것을 말할 수 있다
- [ ] CRA의 2026-09-11 / 2027-12-11 일정과 24h·72h·14일 신고 기한을 말할 수 있다
- [ ] 결정 흐름(인증 → 벤더 SDK → 기존 스택 → 생태계·수명 → 단순함 → Rust)으로 사례를 풀 수 있다
- [ ] RTOS 선택 문서에 Exit plan과 Revisit 조건을 쓸 수 있다

---

## 참고 자료

모든 URL은 2026-10-25에 확인했다. 인증 관련 수치는 대부분 벤더 자료다.

**FreeRTOS · SafeRTOS**
- AWS, "Announcing FreeRTOS Kernel Version 10" (2017-11-29, Amazon 관리·MIT 전환): https://aws.amazon.com/blogs/opensource/announcing-freertos-kernel-v10/
- AWS What's New, FreeRTOS 202604 LTS: https://aws.amazon.com/about-aws/whats-new/2026/04/freertos-lts/
- FreeRTOS-LTS 202604 README (지원 기한 2028-04-30): https://github.com/FreeRTOS/FreeRTOS-LTS/blob/202604-LTS/README.md
- coreMQTT CHANGELOG (v5.0.0 MQTT v5.0 지원): https://github.com/FreeRTOS/coreMQTT/blob/main/CHANGELOG.md
- FreeRTOS-Kernel 소스 `History.txt`, `include/task.h`, `include/semphr.h`, `tasks.c`, `queue.c` (V11.3.2, `/var/rtoslab/src/FreeRTOS-Kernel`)
- SafeRTOS (WITTENSTEIN high integrity systems): https://www.highintegritysystems.com/safertos/

**Zephyr**
- Linux Foundation 출범 보도자료 (2016-02-17): https://www.linuxfoundation.org/press/press-release/the-linux-foundation-announces-project-to-build-real-time-operating-system-for-internet-of-things-devices
- Wind River, Rocket → Zephyr: https://www.windriver.com/blog/wind-river-welcomes-linux-foundations-zephyr-project
- Zephyr 4.4 발표: https://www.zephyrproject.org/zephyr-4.4-announcement
- Zephyr 릴리스 목록 (LTS·EOL·4.5 일정): https://docs.zephyrproject.org/latest/releases/index.html
- Zephyr 안전 개요: https://docs.zephyrproject.org/latest/safety/safety_overview.html
- inovex, Zephyr 안전 인증 로드맵 (약 15,000줄 범위): https://www.inovex.de/en/?p=66655
- LF 보도자료 (2025-01-30, concept approval 언급): https://www.linuxfoundation.org/press/doulos-honda-hubble-network-iar-inovex-and-microchip-technology-join-the-zephyr-project-as-it-gets-closer-to-safety-certification
- Canonical, Zephyr 26.04 LTS 발표 (2026-09-21): https://canonical.com/blog/zephyr-lts-announcement · https://ubuntu.com/zephyr
- Zephyr 소스 `doc/project/release_process.rst`, `kernel/Kconfig`, `kernel/include/kswap.h`, `lib/posix/options/`, `boards/` (v4.4.2)
- Chromebook EC on Zephyr: https://chromium.googlesource.com/chromiumos/platform/ec/+/main/docs/zephyr
- Oticon More 보청기: https://zephyrproject.org/zephyr-on-a-hearing-aid-oticon-more/
- Nordic nRF Connect SDK = Zephyr: https://www.nordicsemi.com/Products/Technologies/Zephyr

**ThreadX · NuttX · RT-Thread · ChibiOS · RTEMS · µC/OS · Mbed**
- Microsoft의 Express Logic 인수 (2019-04-18): https://www.cnbc.com/2019/04/18/microsoft-acquires-express-logic-internet-of-things-company.html
- Introducing Eclipse ThreadX (2023-11-27): https://newsroom.eclipse.org/node/39792
- ThreadX Alliance 출범: https://newsroom.eclipse.org/node/41004 · 인증 현황: https://threadxalliance.org/subscription/safety-certifications
- ThreadX 메일링 리스트 (6.1.x 인증 내역 2024-07-11 / 6.4.1 일정 2026-04-15): https://www.eclipse.org/lists/threadx/msg00075.html · https://www.eclipse.org/lists/threadx/msg00150.html
- ThreadX 120억 대 (프로젝트 문서): https://github.com/eclipse-threadx/rtos-docs
- NuttX 인큐베이션: https://incubator.apache.org/projects/nuttx.html · TLP 발표: https://news.apache.org/foundation/entry/the-apache-software-foundation-announces-apache-nuttx-as-a-top-level-project · 다운로드: https://nuttx.apache.org/download/
- PX4 아키텍처 (NuttX): https://docs.px4.io/main/en/concept/architecture.html
- RT-Thread v3.1.1 (Apache-2.0 전환): https://github.com/RT-Thread/rt-thread/releases/tag/v3.1.1 · 설치 기반 보도: https://riscv.org/ecosystem-news/2025/07/the-installed-base-of-rt-thread-operating-system-has-exceeded-2-5-billion-units-and-is-actively-developing-risc-v-industry-layout
- ChibiOS 라이선스: https://www.chibios.org/dokuwiki/doku.php?id=chibios:licensing:start
- ArduPilot 포팅 (ChibiOS): https://ardupilot.org/dev/docs/porting.html · Copter 릴리스 노트: https://github.com/ArduPilot/ardupilot/blob/master/ArduCopter/ReleaseNotes.txt
- Betaflight 스케줄러 소스: https://github.com/betaflight/betaflight/blob/master/src/main/scheduler/scheduler.c
- RTEMS 6.2: https://www.rtems.org/news/2025-12-20-rtems-6-2-released/ · MMS: https://www.rtems.org/news/2015-03-11-rtems-on-board-nasa-mms-scheduled-for-la/ · SDO: https://www.rtems.org/news/2010-02-11-solar-dynamic-observatory-launches/
- Silicon Labs µC/OS 오픈소스 공개: https://news.silabs.com/Micrium-uC-RTOS-Software-Now-Available-Through-Open-Source-Licensing · Weston Embedded: https://weston-embedded.com/micrium-licensing
- Arm Mbed OS (EOL 2026-07): https://www.arm.com/products/iot/mbed-os
- ESP-IDF FreeRTOS: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos.html

**상용 · 인증 · 자동차**
- Wind River, Curiosity: https://www.windriver.com/resource/nasas-mars-rover-curiosity-powered-by-wind-river · Perseverance: https://www.windriver.com/news/press/news-20210219 · Aptiv 인수 완료: https://www.windriver.com/news/press/news-20221223 · 안전 자료: https://www.windriver.com/studio/develop/safety-artifacts
- QNX OS for Safety: https://qnx.software/en/software/products-and-solutions/qnx-os-and-os-for-safety · BlackBerry FY2026 10-K (2억 7,500만 대): https://www.sec.gov/Archives/edgar/data/1070235/000107023526000039/bbry-20260228.htm
- Green Hills INTEGRITY-178 tuMP: https://www.ghs.com/products/safety_critical/integrity_178_tump.html
- SYSGO PikeOS: https://www.sysgo.com/pikeos
- SEGGER embOS: https://www.segger.com/products/rtos/embos/ · 회사 연혁: https://www.segger.com/about-us/company-history
- AUTOSAR 연혁: https://www.autosar.org/about/history
- seL4 연혁: https://sel4.systems/About/history.html
- Ferrocene 인증: https://ferrous-systems.com/blog/officially-qualified-ferrocene/
- Embassy: https://embassy.dev/

**규제 · 트렌드 · 조사**
- EU CRA, Regulation (EU) 2024/2847: https://eur-lex.europa.eu/eli/reg/2024/2847/oj
- Jones Day, CRA 24시간 신고 의무 (2026-07): https://www.jonesday.com/en/insights/2026/07/eu-cyber-resilience-act-24hour-reporting-duties-start-september-11-2026
- Eclipse 2024 IoT & Embedded Developer Survey: https://newsroom.eclipse.org/node/41237
- Arm Ethos-U85: https://www.arm.com/products/silicon-ip-cpu/ethos/ethos-u85
- Raspberry Pi RP2350 / Pico 2 (2024-08-08): https://www.theregister.com/2024/08/08/pi_pico_2_risc_v/
- GitHub REST API (예제 5): https://api.github.com/repos/zephyrproject-rtos/zephyr 등
