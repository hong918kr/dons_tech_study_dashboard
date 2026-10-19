# RTOS — Study Contents (나침반)

> **작성일**: 2026-10-18 · **목표**: ① RTOS를 **원천부터** (레지스터·스택·스케줄링 이론까지) 설명할 수 있는 수준 ② FreeRTOS·Zephyr 실무, 드론·로봇·차량 펌웨어 면접 통과 ③ 입사 후 바로 태스크 설계·디버깅·인증 대응을 해내는 실무자
> **용도**: 연구할 개념을 **빠짐없이 순서대로 나열한 목차**. Embedded Linux 나침반과 같은 구조(모듈 A–S, ID · 우선순위 · Don 출발점)로 만들고, 각 항목을 노트(쉬운 설명 · 그림 · 실제로 돌려 본 코드)로 채워 간다
> **근거**: FreeRTOS 커널 문서·소스, Zephyr 공식 문서(4.4), Arm v7-M/v8-M 아키텍처 레퍼런스, 실시간 스케줄링 교재(Buttazzo, Liu, Burns & Wellings), 2026-10 웹 확인 트렌드(§5), Neros·Anduril·Scout·Rivian JD 요구사항
> **중복 규칙**: [Embedded Linux 나침반](../../../EmbeddedLinux/site/compass/contents.html)에서 이미 다룬 개념은 **다시 설명하지 않고** "RTOS·MCU에서 달라지는 점"만 다룬다 (§3 경계표). 노트에는 `→ EL F4`처럼 Linux 노트 ID로 링크
> **노트 사이트**: [전체 노트 목록](../index.html) — 빌드 `python3 build_site.py`

---

## 0. 이 문서 읽는 법

**표기**
- **ID**: 모듈 알파벳 + 번호 (예: `C2`). 노트 파일 이름으로 쓴다 → `C_kernel_internals/2026-10-xx_C2_context_switch.md`
- **EL 참조**: `EL F4` = Embedded Linux 나침반의 F4 항목. 그 노트를 먼저 읽었다고 가정한다
- **우선순위**: `P0` = 1차 기술 면접 전에 반드시 · `P1` = 온사이트·입사 전 · `P2` = 넓게 알아 두기 (실무에서 만나면 깊게)
- **Don 출발점** (추정 — Don이 고쳐 쓰면 됨):
  - ✅ 이미 강함 (SSD 컨트롤러 FW 7년 · 인터럽트·DMA·JTAG·scope) → RTOS 용어로 **번역**만 하면 됨
  - 🟡 인접 경험 있음 → 기존 지식에서 확장
  - ❌ 새로 배움 → 처음부터

**전체 그림 — RTOS 기반 펌웨어의 층**
```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg"><rect x="10" y="10" width="660" height="97" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-width="1.3"/><text x="22" y="30" font-size="13" text-anchor="start" font-weight="bold">애플리케이션 영역</text><rect x="22" y="40" width="636" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="34" y="58" font-size="13" text-anchor="start" font-weight="normal">앱 태스크: 제어 루프 · 센서 처리 · 통신 · 추론</text><text x="646" y="58" font-size="12" text-anchor="end" font-weight="bold">Q</text><rect x="22" y="71" width="636" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="34" y="89" font-size="13" text-anchor="start" font-weight="normal">미들웨어: TCP/IP · BLE · 파일시스템 · USB · 로깅 · micro-ROS</text><text x="646" y="89" font-size="12" text-anchor="end" font-weight="bold">K</text><rect x="10" y="115" width="660" height="28" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/><text x="340" y="134" font-size="13" text-anchor="middle" font-weight="normal">경계: RTOS API — 태스크 · 큐 · 세마포어 · 뮤텍스 · 이벤트 · 타이머 (D)</text><rect x="10" y="151" width="660" height="159" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.3"/><text x="22" y="171" font-size="13" text-anchor="start" font-weight="bold">RTOS 커널 — FreeRTOS · Zephyr · NuttX … (G, H, I)</text><rect x="22" y="181" width="636" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="34" y="199" font-size="13" text-anchor="start" font-weight="normal">스케줄러 · ready list · tick · 타임아웃 · 컨텍스트 스위치</text><text x="646" y="199" font-size="12" text-anchor="end" font-weight="bold">C</text><rect x="22" y="212" width="636" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="34" y="230" font-size="13" text-anchor="start" font-weight="normal">메모리: 정적 할당 · 메모리 풀 · 태스크 스택 · MPU 격리</text><text x="646" y="230" font-size="12" text-anchor="end" font-weight="bold">F</text><rect x="22" y="243" width="636" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="34" y="261" font-size="13" text-anchor="start" font-weight="normal">드라이버 · HAL: CMSIS · 벤더 HAL · Zephyr device model</text><text x="646" y="261" font-size="12" text-anchor="end" font-weight="bold">J, H</text><rect x="22" y="274" width="636" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="34" y="292" font-size="13" text-anchor="start" font-weight="normal">포팅 계층: PendSV · SysTick · SVC · 크리티컬 섹션</text><text x="646" y="292" font-size="12" text-anchor="end" font-weight="bold">B, C</text><rect x="10" y="318" width="660" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="22" y="338" font-size="13" text-anchor="start" font-weight="normal">부트로더: ROM → MCUboot → 앱 (서명 검증 · 슬롯 교체 · 롤백)</text><text x="658" y="338" font-size="12" text-anchor="end" font-weight="bold">L</text><rect x="10" y="356" width="660" height="30" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/><text x="22" y="376" font-size="13" text-anchor="start" font-weight="normal">MCU: Cortex-M/R · RISC-V · NVIC · MPU · SRAM/TCM · Flash · 주변장치</text><text x="658" y="376" font-size="12" text-anchor="end" font-weight="bold">B</text><text x="340" y="410" font-size="13" text-anchor="middle" font-weight="bold">모든 층을 가로지르는 것:</text><text x="340" y="430" font-size="13" text-anchor="middle" font-weight="normal">인터럽트·타이밍(E) · 디버깅·테스트(M) · 실시간 분석(N) · 안전(O) · 전원·멀티코어(P)</text></svg>
```

그림 1 — RTOS 펌웨어의 층. 오른쪽 글자는 해당 모듈 ID

**공부 흐름 (큰 순서)**
```svg
<svg viewBox="0 0 680 244" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10" y="34" font-size="13" text-anchor="start" font-weight="bold">Phase 1 원천</text><rect x="140" y="14" width="100" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="190.0" y="34" font-size="12" text-anchor="middle" font-weight="normal">A 개념·지형</text><line x1="241" y1="29" x2="247" y2="29" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="248" y="14" width="100" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="298.0" y="34" font-size="12" text-anchor="middle" font-weight="normal">B MCU HW</text><line x1="349" y1="29" x2="355" y2="29" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="356" y="14" width="100" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="406.0" y="34" font-size="12" text-anchor="middle" font-weight="normal">C 커널 내부</text><line x1="457" y1="29" x2="463" y2="29" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="464" y="14" width="100" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="514.0" y="34" font-size="12" text-anchor="middle" font-weight="normal">D 동기화</text><line x1="565" y1="29" x2="571" y2="29" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="572" y="14" width="100" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="622.0" y="34" font-size="12" text-anchor="middle" font-weight="normal">E 인터럽트·시간</text><line x1="400" y1="46" x2="400" y2="58" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><text x="10" y="80" font-size="13" text-anchor="start" font-weight="bold">Phase 2 플랫폼</text><rect x="140" y="60" width="124" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="202.0" y="80" font-size="12" text-anchor="middle" font-weight="normal">F 메모리·보호</text><line x1="265" y1="75" x2="271" y2="75" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="272" y="60" width="124" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="334.0" y="80" font-size="12" text-anchor="middle" font-weight="normal">G FreeRTOS</text><line x1="397" y1="75" x2="403" y2="75" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="404" y="60" width="124" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="466.0" y="80" font-size="12" text-anchor="middle" font-weight="normal">H Zephyr</text><line x1="529" y1="75" x2="535" y2="75" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="536" y="60" width="124" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="598.0" y="80" font-size="12" text-anchor="middle" font-weight="normal">J 드라이버</text><line x1="400" y1="92" x2="400" y2="104" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><text x="10" y="126" font-size="13" text-anchor="start" font-weight="bold">Phase 3 시스템</text><rect x="140" y="106" width="124" height="30" rx="5" fill="#e08a3c" fill-opacity="0.14" stroke="#e08a3c" stroke-width="1.3"/><text x="202.0" y="126" font-size="12" text-anchor="middle" font-weight="normal">K 미들웨어</text><line x1="265" y1="121" x2="271" y2="121" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="272" y="106" width="124" height="30" rx="5" fill="#e08a3c" fill-opacity="0.14" stroke="#e08a3c" stroke-width="1.3"/><text x="334.0" y="126" font-size="12" text-anchor="middle" font-weight="normal">L 부트·보안</text><line x1="397" y1="121" x2="403" y2="121" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="404" y="106" width="124" height="30" rx="5" fill="#e08a3c" fill-opacity="0.14" stroke="#e08a3c" stroke-width="1.3"/><text x="466.0" y="126" font-size="12" text-anchor="middle" font-weight="normal">P 전원·멀티코어</text><line x1="400" y1="138" x2="400" y2="150" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><text x="10" y="172" font-size="13" text-anchor="start" font-weight="bold">Phase 4 이론·품질</text><rect x="140" y="152" width="124" height="30" rx="5" fill="#d0564a" fill-opacity="0.14" stroke="#d0564a" stroke-width="1.3"/><text x="202.0" y="172" font-size="12" text-anchor="middle" font-weight="normal">M 디버깅·테스트</text><line x1="265" y1="167" x2="271" y2="167" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="272" y="152" width="124" height="30" rx="5" fill="#d0564a" fill-opacity="0.14" stroke="#d0564a" stroke-width="1.3"/><text x="334.0" y="172" font-size="12" text-anchor="middle" font-weight="normal">N 실시간 분석</text><line x1="397" y1="167" x2="403" y2="167" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="404" y="152" width="124" height="30" rx="5" fill="#d0564a" fill-opacity="0.14" stroke="#d0564a" stroke-width="1.3"/><text x="466.0" y="172" font-size="12" text-anchor="middle" font-weight="normal">O 안전·인증</text><rect x="10" y="204" width="660" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="340" y="224" font-size="12" text-anchor="middle" font-weight="normal">병행 — I 기타 RTOS·Rust · Q 도메인(드론·모터·AI) · R 인터뷰 · S 실습 (S2 미니 RTOS는 Phase 1과 함께)</text></svg>
```

그림 2 — 공부 흐름. Phase 1에서 미니 RTOS(S2)를 직접 만들어 "원천 이해"를 먼저 끝내고, 그 위에 플랫폼·시스템·이론을 쌓는다

---

## 1. RTOS를 "원천적으로" 이해하려면 무엇을 알아야 하나 (선행 지식 지도)

> RTOS 커널 = "CPU가 예외를 처리하는 방식"과 "여러 일을 시간 안에 끝내야 한다는 약속" 사이의 번역기. Don은 **왼쪽(HW 동작)이 이미 강하고, 오른쪽(커널 내부 + 스케줄링 이론)이 비어 있다**.

```svg
<svg viewBox="0 0 680 318" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="105" y="18" font-size="13" text-anchor="middle" font-weight="bold">CPU · HW 쪽 — Don ✅</text><text x="530" y="18" font-size="13" text-anchor="middle" font-weight="bold">커널 · 이론 쪽 — 채울 곳</text><rect x="10" y="52" width="190" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="105" y="72" font-size="12" text-anchor="middle" font-weight="normal">예외 진입·복귀 · 자동 스태킹</text><line x1="201" y1="67" x2="268" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="10" y="92" width="190" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="105" y="112" font-size="12" text-anchor="middle" font-weight="normal">인터럽트 우선순위 · 마스킹</text><line x1="201" y1="107" x2="268" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="10" y="132" width="190" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="105" y="152" font-size="12" text-anchor="middle" font-weight="normal">HW 타이머 · 타임스탬프</text><line x1="201" y1="147" x2="268" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="10" y="172" width="190" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="105" y="192" font-size="12" text-anchor="middle" font-weight="normal">원자 명령 · 메모리 배리어</text><line x1="201" y1="187" x2="268" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="10" y="212" width="190" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="105" y="232" font-size="12" text-anchor="middle" font-weight="normal">DMA · 캐시 일관성</text><line x1="201" y1="227" x2="268" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="10" y="252" width="190" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0" stroke-width="1.3"/><text x="105" y="272" font-size="12" text-anchor="middle" font-weight="normal">JTAG · scope · LA 측정</text><line x1="201" y1="267" x2="268" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="270" y="156" width="80" height="40" rx="8" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c" stroke-width="1.3"/><text x="310" y="181" font-size="13" text-anchor="middle" font-weight="bold">RTOS 커널</text><rect x="390" y="40" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="59" font-size="12" text-anchor="start" font-weight="bold">C2</text><text x="426" y="59" font-size="12" text-anchor="start" font-weight="normal">컨텍스트 스위치: 스택에 무엇이 쌓이나</text><line x1="388" y1="54" x2="352" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="74" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="93" font-size="12" text-anchor="start" font-weight="bold">C1</text><text x="426" y="93" font-size="12" text-anchor="start" font-weight="normal">스케줄러: 다음 태스크를 어떻게 고르나</text><line x1="388" y1="88" x2="352" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="108" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="127" font-size="12" text-anchor="start" font-weight="bold">C5</text><text x="426" y="127" font-size="12" text-anchor="start" font-weight="normal">tick: 시간 · 타임아웃 · 주기 실행</text><line x1="388" y1="122" x2="352" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="142" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="161" font-size="12" text-anchor="start" font-weight="bold">D3</text><text x="426" y="161" font-size="12" text-anchor="start" font-weight="normal">우선순위 역전과 상속 · ceiling</text><line x1="388" y1="156" x2="352" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="176" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="195" font-size="12" text-anchor="start" font-weight="bold">E1</text><text x="426" y="195" font-size="12" text-anchor="start" font-weight="normal">ISR 규칙: FromISR · 지연 처리</text><line x1="388" y1="190" x2="352" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="210" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="229" font-size="12" text-anchor="start" font-weight="bold">F3</text><text x="426" y="229" font-size="12" text-anchor="start" font-weight="normal">스택 크기 산정 · 오버플로 탐지</text><line x1="388" y1="224" x2="352" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="244" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="263" font-size="12" text-anchor="start" font-weight="bold">N3</text><text x="426" y="263" font-size="12" text-anchor="start" font-weight="normal">응답 시간 분석: 마감을 지키나</text><line x1="388" y1="258" x2="352" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/><rect x="390" y="278" width="280" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b" stroke-width="1.3"/><text x="402" y="297" font-size="12" text-anchor="start" font-weight="bold">M1</text><text x="426" y="297" font-size="12" text-anchor="start" font-weight="normal">HardFault 해독: 어느 줄에서 죽었나</text><line x1="388" y1="292" x2="352" y2="176" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah)"/></svg>
```

그림 3 — RTOS 커널은 CPU의 예외 처리(왼쪽)를 이용해 태스크 전환과 시간 약속(오른쪽)을 만든다. 초록 상자의 글자는 해당 모듈 ID

**"RTOS를 안다"고 말하기 위한 체크리스트** (전부 "예"면 실무 투입 가능)
- [ ] 리셋 벡터부터 첫 태스크가 실행되기까지를 **레지스터·스택 수준**으로 설명할 수 있다 (B5, C3)
- [ ] PendSV 컨텍스트 스위치 어셈블리를 한 줄씩 설명하고, 왜 PendSV가 최저 우선순위인지 말할 수 있다 (B4, C2)
- [ ] `configMAX_SYSCALL_INTERRUPT_PRIORITY`와 `...FromISR` API가 왜 필요한지 설명할 수 있다 (B3, E1)
- [ ] `vTaskDelay`와 `vTaskDelayUntil`의 차이(드리프트)를 그림으로 그릴 수 있다 (C5)
- [ ] 우선순위 역전을 재현하고, 우선순위 상속과 priority ceiling의 차이를 말할 수 있다 (D3, N4)
- [ ] 큐에 넣는 순간 높은 우선순위 태스크가 깨어나 선점되기까지의 경로를 따라갈 수 있다 (D4, R2)
- [ ] 태스크 스택 크기를 근거 있게 정하고, 오버플로를 탐지하는 방법 3가지를 안다 (F3)
- [ ] HardFault 레지스터(CFSR·HFSR·BFAR)와 스택에 쌓인 PC로 원인 줄을 찾는다 (M1)
- [ ] 주어진 태스크 세트가 마감을 지키는지 응답 시간 분석(RTA)으로 계산한다 (N2, N3)
- [ ] tickless idle이 tick을 어떻게 건너뛰고 시간을 보정하는지 설명한다 (C6, P2)
- [ ] Zephyr 앱을 DT overlay + Kconfig로 빌드하고 Ztest/Twister로 테스트한다 (H2, H3, H8)
- [ ] MCUboot로 서명된 이미지를 교체·확정·롤백할 수 있다 (L2)

---

## 2. 업무 장면 → 필요한 모듈

| 실무자가 실제로 하는 일 | 필요한 모듈 |
|---|---|
| 드론 비행 제어 루프(1 kHz)가 가끔 늦게 돈다 | E3, E7, C4, M3, N3, Q3 |
| 필드에서 랜덤 HardFault가 난다 | M1, M5, F3, M7 |
| 장비가 몇 시간 뒤 멈춘다 (데드락 · 힙 단편화 · 우선순위 역전) | D3, D9, F1, M3, M4 |
| 새 MCU 보드에 Zephyr를 올려라 | B5, H2–H5, J1 |
| FreeRTOS 제품을 Zephyr로 옮겨라 | A5, G, H, D10 |
| 배터리 수명을 2배로 | C6, P1–P3, J9 |
| 모터 FOC 전류 루프 20 kHz + CAN 통신 | E2, J5, J6, Q4 |
| 무선 업데이트 중 전원이 꺼지면 벽돌이 된다 | L2, L3, J8, K3 |
| Linux SoC와 보조 MCU가 협업해야 한다 | P4, P5, K8 (EL O1–O3) |
| MCU의 NPU로 키워드·이상 감지 추론을 돌려라 | Q6, B7, E6 |
| ISO 26262 / IEC 61508 프로젝트에 투입 | O1–O5, N7, F1 |
| 보안팀: EU CRA 대응, 취약점 신고 체계 | L6, L7 |
| FW Test 직군: HIL·시뮬레이션 CI를 만들어라 | M8, M9, H8 |
| 면접: "bare-metal vs RTOS vs Linux, 언제 무엇?" | A2, A7 (EL N4) |

---

## 3. Embedded Linux와의 경계 (중복 제거 규칙)

> 아래 왼쪽 개념은 Linux 노트에서 이미 설명했다. RTOS 노트는 **오른쪽만** 새로 쓴다.

| 주제 | Embedded Linux에서 다룸 | RTOS 노트에서 새로 다루는 것 |
|---|---|---|
| 동시성 기본 (spinlock · mutex · atomic) | EL F4, A8 | 단일 코어 MCU 관점, 우선순위 역전, ISR 규칙 (D1–D3, E1) |
| 메모리 배리어 | EL F5 | Cortex-M에서 DMB/DSB/ISB가 실제로 필요한 곳만 (D8) |
| 인터럽트 (GIC · threaded IRQ) | EL F6, H6 | NVIC · BASEPRI · zero-latency ISR · FromISR (B3, E1, E2) |
| 시간 (hrtimer · clocksource) | EL F8 | tick · tickless · HW 타이머 직접 사용 (C5, C6, E5) |
| 실시간 일반론 · PREEMPT_RT | EL N1–N4 | RTOS 커널 자체 + 스케줄 가능성 이론 (C, N) |
| Device Tree 문법 | EL D1–D8 | Zephyr의 **빌드 시점** DT → C 매크로 차이만 (H3) |
| Kconfig | EL E2 | Zephyr에서의 사용법만 (H2) |
| 크로스 툴체인 · ELF | EL B1–B4 | bare-metal libc · 링커 스크립트 · map 파일 (B5, B11) |
| DMA · 캐시 일관성 | EL H9 | MCU 캐시 유지 함수 · DMA 완료 → 태스크 통지 (B8, E6) |
| I2C · SPI · UART 프로토콜 | EL H5, H7, H8 | 프로토콜 설명 생략, RTOS 드라이버 패턴만 (J2–J4) |
| CAN | EL I10, Q4 | MCU CAN 컨트롤러 · 필터 · bus-off 복구 (J6) |
| OTA A/B · 롤백 | EL L4 | MCUboot · 플래시 슬롯 교체 (L2) |
| Secure boot · TPM/TEE | EL C5, L5, L6 | TrustZone-M · TF-M · OTP 키 (F6, L4, L5) |
| 전원 관리 | EL N5, N6 | MCU 저전력 모드 · tickless 연동 (P1, P2) |
| remoteproc · rpmsg | EL O1–O3 | RTOS **원격 측** 구현 (P5) |
| JTAG · 디버거 | EL M4, M11 | HardFault · RTOS-aware 디버깅 · SystemView (M1–M3) |
| 테스트 · CI | EL P6 | Ztest · Twister · Renode · HIL (M8, M9) |
| ROS 2 | EL Q6 | micro-ROS · uORB (K7) |
| PTP · 센서 동기 | EL Q3 | MCU 타임스탬프 → 호스트 시계 매핑 (Q7) |
| 라이선스 · SBOM | EL K7 | EU CRA · 취약점 신고 실무 (L7) |

**다른 Don 노트와의 경계** — 다시 쓰지 않고 링크만: OSTEP 스케줄링·락·조건변수(MLFQ, monitors) · Verkada 동시성 노트(mutex) · Hark Embedded AI 노트(F2 TFLite Micro, J1 추론 통합 패턴, J2 실시간 설계, J4 C++/Rust, E3 RISC-V) · Anduril C 문제은행(RTOS 개념 세트, SPSC 링버퍼, FSM)

---

## 4. 모듈별 목차

### A. 실시간 · RTOS 개념과 지형 — "왜 RTOS인가"

> 커널 내부로 들어가기 전에, **실시간이 무엇이고 RTOS가 무엇을 약속하는지**를 먼저 정한다.

| ID | 주제 | 핵심 개념 | 왜 필요 (인터뷰 / 실무) | 우선 | Don |
|---|---|---|---|---|---|
| A1 | 실시간이란 | hard / firm / soft RT, deadline, latency vs jitter, 결정성(determinism), "빠름 ≠ 실시간" | 면접 첫 질문 "real-time이 뭐냐" | P0 | 🟡 |
| A2 | 실행 모델의 진화 | super-loop → 인터럽트 + 메인 루프(foreground/background) → 협력형 스케줄러 → 선점형 RTOS → async executor | "언제 RTOS가 필요한가" 설계 판단 | P0 | ✅ |
| A3 | RTOS 구성 요소 지도 | 커널(스케줄러 · IPC · 타이머) vs 미들웨어 vs HAL, 라이브러리형 커널 vs 마이크로커널, 컴파일 시점 정적 구성 | 전체 지도 | P0 | 🟡 |
| A4 | 태스크 모델 | task/thread, TCB, 상태 전이(ready / running / blocked / suspended), 태스크별 스택, 정적 vs 동적 생성 | 모든 RTOS 질문의 기본 단위 | P0 | 🟡 |
| A5 | API 로제타석 | FreeRTOS · Zephyr · ThreadX · CMSIS-RTOS2 · POSIX에서 같은 개념의 다른 이름, OS 추상화 계층(OSAL) | 회사마다 RTOS가 다름 — 번역 능력 | P1 | ❌ |
| A6 | 2026 RTOS 지형도 | 오픈소스(FreeRTOS, Zephyr, NuttX, ThreadX, RT-Thread, ChibiOS, RTEMS) vs 상용 · 인증(SafeRTOS, VxWorks, QNX, INTEGRITY, PikeOS, embOS), 라이선스 · 거버넌스 · 지원 기간 | "왜 Zephyr를 고르나" 대화 | P1 | ❌ |
| A7 | bare-metal vs RTOS vs Linux vs 하이퍼바이저 | 결정 기준: 지연 · 메모리 · 인증 · 생태계 · 업데이트 · 인력 | 시스템 설계 단골 (EL N4의 MCU 쪽 판단 기준) | P0 | ✅ |

### B. MCU 하드웨어 기반 — RTOS가 기대는 CPU 기능

> RTOS의 "마법"은 전부 CPU의 예외 처리 기능 위에 있다. 이 모듈을 건너뛰면 C(커널 내부)가 외우기가 된다.

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| B1 | Cortex-M 프로그래머 모델 | R0–R15, xPSR, Thread / Handler 모드, privileged / unprivileged, MSP / PSP, CONTROL 레지스터 | 컨텍스트 스위치를 읽기 위한 전제 | P0 | 🟡 |
| B2 | 예외 진입 · 복귀 | 벡터 테이블, 하드웨어 자동 스태킹(8워드), EXC_RETURN, tail-chaining, late-arrival, VTOR | "ISR을 그냥 C 함수로 쓸 수 있는 이유" | P0 | ✅ |
| B3 | NVIC와 우선순위 마스킹 | 구현 우선순위 비트 수, PRIGROUP(선점 / 서브), PRIMASK · BASEPRI · FAULTMASK, "숫자가 작을수록 높다" 함정 | `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이해의 열쇠 | P0 | 🟡 |
| B4 | RTOS가 쓰는 세 예외 | SysTick(tick), PendSV(미뤄 둔 컨텍스트 스위치), SVC(시스템 콜 · 첫 태스크 시작), PendSV가 최저 우선순위인 이유 | **RTOS 포팅의 핵심** | P0 | 🟡 |
| B5 | 리셋부터 main까지 (MCU) | Reset_Handler, .data 복사 · .bss 0 채우기, 링커 스크립트(MEMORY / SECTIONS), 스택 · 힙 배치, XIP flash vs RAM 실행 | bring-up · 포팅 (EL B3 ELF 지식 재사용) | P0 | ✅ |
| B6 | FPU와 컨텍스트 | lazy stacking(FPCCR), 확장 스택 프레임(26워드), 태스크별 FPU 사용 비용 | 제어 · DSP 태스크의 숨은 비용 | P1 | 🟡 |
| B7 | Cortex-M 세대 지도 | M0+ / M3 / M4 / M7 / M23 / M33 / M55 / M85, Armv6-M · v7-M · v8-M(baseline / mainline) · v8.1-M, Helium(MVE), PACBTI | 칩 고르기 · 포팅 차이 | P1 | 🟡 |
| B8 | 캐시 · TCM이 있는 MCU | M7 / M85 I-cache · D-cache, TCM, MPU 속성으로 non-cacheable 영역 만들기, DMA 일관성(`SCB_CleanDCache_by_Addr`) | 고성능 MCU 버그 1순위 (EL H9의 MCU판) | P1 | ✅ |
| B9 | Cortex-R 계열 | R5 / R52 / R52+ / R82, TCM, lockstep(DCLS), GIC 기반 인터럽트, EL2 하이퍼바이저(R52), MPU 2단계 | SSD 컨트롤러 · 자동차 · AI SoC 안전 코어 | P1 | ✅ |
| B10 | RISC-V MCU 관점 | M / S / U 모드, mtvec · mcause · mepc, CLINT · PLIC · CLIC, PMP, A 확장(원자), ESP32-C 계열 · RP2350 Hazard3 | RISC-V 포팅이 다른 점 (Hark E3 ISA 개요 재사용) | P1 | ❌ |
| B11 | MCU 툴체인 특화 | arm-none-eabi, newlib / newlib-nano / picolibc, syscall stub(`_sbrk`, `_write`), `-ffunction-sections` + `--gc-sections`, LTO, map 파일 읽기, 코드 크기 줄이기 | 플래시 256 KB에 넣기 (EL B1–B4 기본은 생략) | P1 | 🟡 |

### C. 커널 내부 — 스케줄러 · 컨텍스트 스위치 · tick

> 이 모듈이 "원천 이해"의 중심이다. FreeRTOS와 Zephyr 소스를 실제로 열어 함수 단위로 따라간다.

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| C1 | 커널 자료구조 | ready list(우선순위별 리스트 + 비트맵), delayed / blocked list, CLZ로 O(1) 최고 우선순위 찾기, 이벤트 리스트 | "스케줄러 결정은 O(1)인가" | P0 | ❌ |
| C2 | 컨텍스트 스위치 해부 | PendSV 핸들러 어셈블리 한 줄씩: PSP 읽기 → R4–R11 저장 → TCB 교체 → 복원 → EXC_RETURN | **원천 이해의 중심**, 면접 심화 | P0 | ❌ |
| C3 | 첫 태스크 시작 | 초기 스택 프레임 위조(fake frame), SVC로 첫 태스크 진입, MSP 재설정, 스케줄러 시작 뒤 main 스택의 운명 | "태스크 함수가 return하면?" | P0 | ❌ |
| C4 | 스케줄링 정책 | 고정 우선순위 선점, 같은 우선순위 round-robin time slicing, 협력형, Zephyr 협력형(음수) / 선점형 / meta-IRQ 우선순위, ThreadX preemption-threshold | 정책별 지터 차이 | P0 | 🟡 |
| C5 | tick과 시간 관리 | tick 인터럽트, tick rate 선택, timeout 리스트, tick 카운터 오버플로 처리, `vTaskDelay` vs `vTaskDelayUntil`(드리프트) | 주기 태스크 구현 단골 질문 | P0 | 🟡 |
| C6 | tickless 커널 | idle일 때 tick 억제, 다음 깨울 시각 계산, 저전력 타이머(LPTIM · RTC)로 시간 보정, Zephyr의 tickless 기본값 | 배터리 장비 필수 | P1 | ❌ |
| C7 | 크리티컬 섹션 구현 | 인터럽트 마스킹 vs 스케줄러 잠금, BASEPRI 방식, 중첩 카운트, 길이가 지연에 미치는 영향 | "크리티컬 섹션 안에서 하면 안 되는 일" | P0 | 🟡 |
| C8 | 커널 진입 경로 | API 호출 → (보호 모드면) SVC → 커널 코드, Zephyr syscall(유저 모드) vs 직접 호출, FreeRTOS-MPU 래퍼 | 보호 모델 이해 | P1 | ❌ |
| C9 | idle 태스크와 훅 | idle hook, 태스크 삭제와 메모리 회수, malloc-failed · stack-overflow hook, 런타임 통계 훅 | 운영 품질 | P1 | 🟡 |
| C10 | SMP RTOS | FreeRTOS SMP, Zephyr SMP, core affinity, IPI, SMP에서 크리티컬 섹션 = 스핀락, 전역 스케줄링 결정 | 멀티코어 MCU 시대 (EL F4 스핀락 개념 재사용) | P1 | 🟡 |
| C11 | 다른 스케줄러 구현 | EDF(Zephyr `CONFIG_SCHED_DEADLINE`), cyclic executive(시간표 기반), ARINC 653 파티션 스케줄링 | 이론(N)과 구현 잇기 | P2 | ❌ |
| C12 | 커널 소스 읽기 투어 | FreeRTOS `tasks.c` · `queue.c` · `list.c` · `port.c`, Zephyr `kernel/sched.c` · `thread.c`, 읽는 순서와 핵심 함수 | 원천 소스 독해력 | P1 | ❌ |

### D. 동기화 · 태스크 간 통신

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| D1 | 세마포어 | binary vs counting, 신호용 vs 자원 계수용, ISR → 태스크 신호, "세마포어로 상호배제하면 안 되는 이유" | 면접 최빈출 "mutex vs semaphore" | P0 | 🟡 |
| D2 | 뮤텍스 | 소유권, 재귀 뮤텍스, 타임아웃, ISR에서 금지인 이유, 뮤텍스 vs 크리티컬 섹션 고르기 | 공유 주변장치 보호 | P0 | 🟡 |
| D3 | 우선순위 역전 | Mars Pathfinder(1997), 무한 역전, priority inheritance(PIP), priority ceiling(OCPP / ICPP), 전이적 상속, FreeRTOS · Zephyr 구현의 한계 | **RTOS 면접 1순위 질문** | P0 | 🟡 |
| D4 | 메시지 큐 | 복사 vs 포인터 전달, 깊이 설계, 블로킹 send / receive, 큐 셋 · poll, 큐로 버퍼 소유권 넘기기 | 태스크 설계의 기본 | P0 | 🟡 |
| D5 | 가벼운 신호 수단 | task notification(FreeRTOS), event group · event flags, `k_poll`, stream / message buffer(단일 생산자 · 소비자) | 성능 · RAM 절약 | P1 | ❌ |
| D6 | 소프트웨어 타이머 | 타이머 서비스 태스크(daemon), 콜백 컨텍스트 제약, one-shot / auto-reload, Zephyr `k_timer`(ISR 컨텍스트에서 실행) | "타이머 콜백에서 블록하면?" | P1 | 🟡 |
| D7 | 작업 위임 | Zephyr workqueue(`k_work`, delayable), FreeRTOS `xTimerPendFunctionCallFromISR`, 게이트키퍼 태스크 | ISR → 스레드 이관 패턴 | P1 | ❌ |
| D8 | 락 없는 통신 (MCU) | LDREX / STREX · CLREX, Cortex-M의 C11 atomics, M0에서의 한계(인터럽트 잠금으로 대체), ISR ↔ 태스크 SPSC 링버퍼, DMB / DSB / ISB가 필요한 곳 | 고속 ISR 경로 (EL F5 개념 재사용) | P0 | ✅ |
| D9 | 데드락 · 기아 · 라이브락 | 락 순서 규칙, 타임아웃 기반 탐지, 우선순위 기아, 대기 그래프 | 현장 "가끔 멈춤" 분석 | P1 | 🟡 |
| D10 | Zephyr 고유 IPC | `k_fifo` / `k_lifo` / `k_msgq` / `k_pipe` / `k_mbox`, zbus(publish-subscribe 버스) | Zephyr 실무 | P1 | ❌ |

### E. 인터럽트 · 타이밍 · 결정성

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| E1 | RTOS-aware ISR 규칙 | `...FromISR` API, `xHigherPriorityTaskWoken` + `portYIELD_FROM_ISR`, ISR에서 금지된 것, Zephyr `k_is_in_isr()` | **매일 쓰는 규칙**, 면접 단골 | P0 | 🟡 |
| E2 | 인터럽트 우선순위 설계 | kernel-aware vs zero-latency(커널 밖) 인터럽트, 우선순위 배치표 작성법, 중첩 | 모터 제어 · 고속 ISR | P0 | 🟡 |
| E3 | 지연과 지터 분해 | 인터럽트 지연 = HW 진입 + 마스킹 구간 + 핸들러 + 스케줄링 지연, 측정(GPIO + scope, DWT CYCCNT) | 수치로 말하기 | P0 | ✅ |
| E4 | ISR → 태스크 처리 경로 | deferred interrupt processing, 바로 깨우기 vs 이벤트 모아 처리, 인터럽트 폭주(storm) 대비 | 설계 질문 | P0 | 🟡 |
| E5 | HW 타이머 활용 | 입력 캡처 · 출력 비교, 32비트 카운터를 64비트 시간으로 확장, 고해상도 타임스탬프, 타이머 트리거 ADC | 센서 타임스탬프 | P1 | ✅ |
| E6 | DMA + RTOS I/O | double / ping-pong 버퍼, half / full 완료 콜백 → 태스크 통지, UART idle-line + DMA, 캐시 처리 | 고속 데이터 경로 | P0 | ✅ |
| E7 | 주기 실행의 정확도 | 지터 원인(tick 해상도, 선점, 캐시), HW 타이머 구동 vs tick 구동, 위상 정렬 | 제어 루프 품질 | P1 | 🟡 |
| E8 | 워치독 전략 | IWDG / WWDG, 태스크 heartbeat 감시 태스크, 윈도 워치독, 리셋 원인 기록 | 필드 안정성 (EL I9은 Linux watchdog) | P0 | ✅ |

### F. 메모리 관리 · 보호 · 격리

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| F1 | 정적 vs 동적 할당 | 정적 할당 정책(인증 프로젝트), FreeRTOS heap_1 ~ heap_5, 단편화, 할당 시간의 결정성 | "RTOS에서 malloc 써도 되나" | P0 | 🟡 |
| F2 | 메모리 풀 · 슬랩 | 고정 크기 블록 풀, Zephyr `k_mem_slab` / `k_heap`, ThreadX byte / block pool, O(1) 할당 | 결정적 메모리 | P1 | 🟡 |
| F3 | 스택 크기 산정과 오버플로 | high water mark, 카나리, MPU 가드, PSPLIM / MSPLIM(v8-M), `-fstack-usage` · 정적 분석, ISR 스택 분리 | **현장 버그 1순위** | P0 | 🟡 |
| F4 | MPU | 리전 · 정렬 · subregion(v7-M), v8-M MPU(base / limit), 속성(XN, RO, 캐시), 컨텍스트 스위치 때 리전 교체 | 격리의 하드웨어 기반 | P1 | 🟡 |
| F5 | 유저 모드 격리 | Zephyr userspace(memory domain / partition, syscall 인자 검증), FreeRTOS-MPU(restricted task), 결함 격리 vs 성능 | 보안 · 안전 설계 | P1 | ❌ |
| F6 | TrustZone-M | Secure / Non-secure, SAU / IDAU, NSC veneer, 보안 상태 간 호출, TF-M 파티션, RTOS가 NS 쪽에서 도는 구조 | Armv8-M 보안 질문 | P1 | ❌ |
| F7 | 재진입성과 C 라이브러리 | 재진입 함수, newlib `_reent` · malloc 락, 태스크별 errno, ISR에서 printf 금지 이유, picolibc TLS | 숨은 레이스 | P1 | 🟡 |
| F8 | 메모리 배치 실무 | TCM / SRAM 뱅크에 객체 배치(section attribute), noinit RAM(리셋 후 보존되는 로그), 링커 스크립트로 영역 분리, ECC RAM 초기화 | 성능 · 크래시 수집 | P1 | ✅ |
| F9 | MMU를 쓰는 RTOS | QNX · NuttX kernel build · VxWorks RTP의 프로세스 모델, Cortex-R82 / Cortex-A 위 RTOS | 넓은 시야 | P2 | ❌ |

### G. FreeRTOS 실무

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| G1 | FreeRTOS 구조와 설정 | 커널 파일 구성(tasks · queue · list · timers · event_groups · stream_buffer), `FreeRTOSConfig.h` 핵심 항목 | 가장 널리 쓰이는 RTOS | P0 | 🟡 |
| G2 | 포트 레이어 | `port.c` / `portmacro.h`, ARM_CM4F vs ARM_CM33(TrustZone · MPU), RISC-V 포트, 새 칩 포팅 절차 | 포팅 · 디버깅 | P1 | ❌ |
| G3 | 커널 객체 API 실전 | 태스크 · 큐 · 세마포어 · 뮤텍스 · 이벤트 · 알림 조합, 흔한 실수(우선순위 설계 실수, `portMAX_DELAY` 남용, 스택 부족) | 코드 리뷰 | P0 | 🟡 |
| G4 | FreeRTOS-MPU와 보안 | restricted task, MPU 리전 구성, 202604 LTS의 MPU 개선(커널이 차지하는 리전 감소) | 보안 강화 트렌드 | P2 | ❌ |
| G5 | FreeRTOS SMP | `configNUMBER_OF_CORES`, core affinity, 런 큐 정책, RP2040 · ESP32 사례, Cortex-R82 SMP 포트(2026) | 멀티코어 | P1 | ❌ |
| G6 | LTS와 AWS 라이브러리 | FreeRTOS 202604 LTS(kernel 11.3.0, 2년 지원), coreMQTT(MQTT v5), coreSNTP, FreeRTOS+TCP, CBMC 메모리 안전 증명 | IoT 회사 대화 | P2 | ❌ |
| G7 | ESP-IDF의 FreeRTOS | 듀얼코어 변형(IDF FreeRTOS vs Amazon SMP), 코어 고정 태스크, Wi-Fi · BT 태스크 우선순위 | 실제 제품에서 가장 흔함 | P2 | ❌ |

### H. Zephyr 실무 — 2026 트렌드의 중심

> Zephyr는 "RTOS 커널 + Linux식 생태계(west · Kconfig · DT · CI)". 문법은 Linux에서 배운 것을 재사용하고 **빌드 시점에 모든 것이 정해진다는 차이**에 집중한다.

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| H1 | Zephyr 큰 그림 | Linux Foundation 거버넌스, 릴리스 주기(4.4 = 2026-04, 연 2회), LTS 3.7 → LTS4 = 4.6(2027-04 예정), Canonical Zephyr 26.04 LTS 상용 배포판 | 채택이 급증하는 이유 | P0 | ❌ |
| H2 | 빌드 시스템 | west(멀티 repo · manifest), CMake, Kconfig, sysbuild(부트로더 + 앱 + 네트워크 코어 동시 빌드), Zephyr SDK 1.0(LLVM 실험 지원, C17 최소) | 첫날부터 막히는 곳 | P0 | ❌ |
| H3 | Zephyr의 Devicetree | **빌드 시점** DT → C 매크로(`DT_NODELABEL`, `DT_PROP`), overlay, bindings YAML, `DEVICE_DT_GET` — 문법은 EL D2 재사용 | Zephyr 드라이버 핵심 | P0 | ❌ |
| H4 | 보드 · SoC 포팅 | Hardware Model v2(board / soc / variant), 보드 정의 파일, pinctrl, 새 보드 추가 | bring-up | P1 | ❌ |
| H5 | 디바이스 드라이버 모델 | `DEVICE_DT_INST_DEFINE`, init level · 우선순위, API 구조체(sensor · i2c · gpio), device runtime PM | 드라이버 작성 | P0 | ❌ |
| H6 | 커널 서비스 실전 | 스레드 · 우선순위 체계(음수 = 협력형), `k_sem` / `k_mutex` / `k_msgq` / `k_work` / `k_timer`, `K_THREAD_DEFINE` 정적 생성 | 앱 작성 | P0 | ❌ |
| H7 | 서브시스템 투어 | logging(지연 로깅), shell, settings · NVS · ZMS, sensor API, input, zbus, retention | 툴 · 운영 기능 | P1 | ❌ |
| H8 | 테스트 체계 | Ztest, Twister(시뮬 · HW 매트릭스), native_sim, QEMU 타깃, 커버리지 | CI 질문 | P1 | ❌ |
| H9 | 고급 기능 | userspace, LLEXT(동적 로드 확장), 하드웨어 스택 보호, AMP 멀티코어(nRF5340 · i.MX), 하이퍼바이저 게스트 | 시니어 대화 | P2 | ❌ |
| H10 | 생태계 연결 | MCUboot · mcumgr, nRF Connect SDK, TF-M, OpenThread · Matter, Rust 바인딩(zephyr-lang-rust), SBOM 생성 | 벤더 SDK 이해 | P1 | ❌ |

### I. 다른 RTOS · 신세대 실행 모델 (Rust)

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| I1 | NuttX | POSIX · VFS 지향, `/dev` 드라이버 모델, flat / protected / kernel 빌드, work queue — **PX4가 돌아가는 OS** | 드론 회사 (Neros · Anduril) | P1 | ❌ |
| I2 | ThreadX (Eclipse) | preemption-threshold, event chaining, 안전 인증 현황(v6.4.1 IEC 61508 · ISO 26262 진행 중), NetX · FileX · USBX | SSD · Azure RTOS 계보 | P2 | 🟡 |
| I3 | ChibiOS · RT-Thread · RTEMS · µC/OS | ArduPilot(ChibiOS), 중국 생태계(RT-Thread), 우주 · 과학(RTEMS), 교과서(µC/OS-III) — 특징과 쓰이는 곳 | 지형 이해 | P2 | ❌ |
| I4 | 상용 RTOS · 마이크로커널 | QNX(메시지 패싱 마이크로커널), VxWorks, INTEGRITY, PikeOS, seL4 — 어디서 쓰고 왜 비싼가 | 국방 · 자동차 면접 | P2 | ❌ |
| I5 | AUTOSAR Classic OS / OSEK | basic / extended task, alarm · counter · schedule table, ISR category 1 / 2, 정적 구성(OIL · ARXML), 타이밍 보호 | 자동차(Rivian) 공통어 | P1 | ❌ |
| I6 | POSIX on RTOS | PSE51 ~ PSE54 프로파일, Zephyr · NuttX의 pthreads, 이식성 vs 결정성 | Linux 코드 재사용 판단 | P2 | ❌ |
| I7 | Embedded Rust 기초 | `no_std`, PAC → HAL → embedded-hal 1.0, 소유권으로 주변장치 독점, critical-section crate, probe-rs · defmt | 트렌드 1순위 (Hark J4 재사용) | P1 | ❌ |
| I8 | Embassy (async) | async / await → 컴파일 시점 상태 머신, 단일 스택 · 힙 없음, interrupt executor로 우선순위, 스레드형 RTOS와 비교 | 새 실행 모델 | P1 | ❌ |
| I9 | RTIC | Stack Resource Policy(SRP)를 컴파일 시점에 적용, 데드락 없음 보장, HW 인터럽트 우선순위 = 태스크 우선순위 | 이론(N4)과 연결 | P2 | ❌ |
| I10 | Hubris · Tock | Hubris(Oxide): 정적 태스크 · 메시지 패싱 · 태스크 재시작, Tock: capsule · 프로세스 격리 | 신뢰성 · 보안 설계 아이디어 | P2 | ❌ |
| I11 | Rust 인증 지형 | Ferrocene(인증된 Rust 컴파일러), C와 Rust 혼합 빌드, FFI 경계 설계 | 커리어 신호 | P2 | ❌ |

### J. 드라이버 · 주변장치 통합 — RTOS 공통

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| J1 | HAL 계층 지도 | CMSIS-Core · CMSIS-Driver, 벤더 HAL · LL(STM32Cube, MCUXpresso, nrfx), RTOS 드라이버와 HAL의 경계 | 코드베이스 읽기 | P0 | ✅ |
| J2 | 스레드 안전 드라이버 패턴 | 블로킹 API = 뮤텍스(버스 소유) + 세마포어(완료 대기) + ISR, 타임아웃 · 에러 복구, 비동기 콜백 API | **드라이버 화이트보드 단골** | P0 | 🟡 |
| J3 | 공유 버스 관리 | I2C · SPI 버스를 여러 태스크가 쓸 때, 트랜잭션 큐, I2C 버스 행 복구(9클럭), CS 관리 | 센서 여러 개 | P0 | ✅ |
| J4 | UART 드라이버 실전 | 링버퍼 + DMA + idle line, 오버런 처리, 프로토콜 파서 태스크, 흐름 제어 | 디버그 · 통신 | P0 | ✅ |
| J5 | ADC · PWM · 타이머 동기 | 타이머 트리거 ADC + DMA, PWM 중앙 정렬과 샘플링 시점, injected 채널 | 모터 · 전원 제어 | P1 | 🟡 |
| J6 | MCU CAN 드라이버 | bxCAN · FDCAN(M_CAN), 필터 · 메일박스 · FIFO, 에러 카운터 · bus-off 복구, CAN FD, 타임스탬프 (Linux SocketCAN은 EL I10) | 로봇 · 차량 | P0 | ❌ |
| J7 | 센서 드라이버 패턴 | data-ready 인터럽트 vs 폴링, FIFO watermark, ODR과 타임스탬프 보정, Zephyr sensor API | IMU · 드론 | P0 | 🟡 |
| J8 | 내부 플래시 · 저장 | 플래시 지우기 / 쓰기 중 실행 정지(RWW), 쓰는 동안의 인터럽트, EEPROM 에뮬레이션 | 설정 저장 · OTA | P1 | ✅ |
| J9 | 드라이버 전원 관리 | 장치 단위 suspend / resume, 클럭 게이팅, 웨이크업 소스, Zephyr PM device runtime | 저전력 장비 | P1 | 🟡 |

### K. 미들웨어 · 연결성

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| K1 | TCP/IP 스택 | lwIP(raw · netconn · socket API, pbuf), FreeRTOS+TCP, Zephyr net(net_buf, BSD 소켓), zero-copy, 스택 태스크 우선순위 | 이더넷 MCU | P1 | ❌ |
| K2 | 무선 스택 구조 | BLE host / controller 분리(HCI), Zephyr BT · NimBLE, Thread · OpenThread, Matter, Wi-Fi 오프로드, 무선 태스크의 실시간 요구 | IoT · 웨어러블 | P2 | ❌ |
| K3 | MCU 파일시스템 · KV 저장 | LittleFS(전원 차단 안전 copy-on-write · 웨어레벨링), FatFS, Zephyr NVS · ZMS, settings | 로그 · 설정 (EL J5 개념 재사용) | P1 | ✅ |
| K4 | USB 스택 | TinyUSB, Zephyr USB device_next, CDC-ACM · MSC · DFU 클래스, USB 인터럽트와 태스크 | 디버그 포트 · 업데이트 | P2 | 🟡 |
| K5 | IoT 프로토콜과 TLS | MQTT(v5) · CoAP · LwM2M, mbedTLS · PSA Crypto API, 메모리 예산, 인증서 저장 | 클라우드 연결 | P2 | ❌ |
| K6 | 로깅 · 셸 인프라 | 지연 로깅(포맷은 호스트에서), 딕셔너리 로깅, 셸 명령 설계, 로그가 타이밍을 망치는 방식 | 운영 · 디버그 | P1 | 🟡 |
| K7 | 로보틱스 미들웨어 | micro-ROS(rclc, DDS-XRCE + agent), uORB(PX4 pub / sub), MAVLink, DroneCAN · Cyphal | 드론 · 로봇 (EL Q6 ROS 2의 MCU 쪽) | P1 | ❌ |
| K8 | 호스트 ↔ MCU 직렬 프로토콜 | 프레이밍(COBS · SLIP), CRC, 시퀀스 · ACK · 재전송, 버전 협상, 시간 동기 메시지 | 호스트 연동 (EL O3은 Linux 쪽) | P0 | ✅ |

### L. 부트 · 업데이트 · 보안

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| L1 | MCU 부트 흐름 | ROM 부트로더 · boot pin · option byte, 부트로더 → 앱 점프 절차(스택 · VTOR · 인터럽트 정리), 복구 모드 | bring-up · 복구 | P0 | ✅ |
| L2 | MCUboot | 슬롯 구조, swap-using-move · scratch · overwrite · direct-XIP, imgtool 서명, test 부팅 → confirm, 롤백 · 다운그레이드 방지 | **MCU OTA 표준** (EL L4 A/B 개념 재사용) | P0 | ❌ |
| L3 | 업데이트 전송 경로 | mcumgr / SMP, DFU over BLE · UART · USB, CAN 부트로더(UDS · ISO-TP), 델타 업데이트, 듀얼뱅크 플래시 | 필드 업데이트 설계 | P1 | 🟡 |
| L4 | 신뢰 루트와 secure boot | OTP · eFuse 키 해시, ROM 검증, secure element, measured boot, 키 폐기 · 교체 | 보안 질문 | P1 | 🟡 |
| L5 | TF-M과 PSA | PSA Certified 레벨, TF-M 서비스(crypto · storage · attestation), NS RTOS에서 호출, SESIP | Armv8-M 보안 제품 | P2 | ❌ |
| L6 | 디바이스 보안 하드닝 | 디버그 포트 잠금(RDP 레벨), 스택 보호 · W^X, 글리치 · 사이드채널 기초, 암호 가속기, 안전한 키 저장 | 공격 표면 | P1 | 🟡 |
| L7 | 규제 · 공급망 | EU CRA(취약점 · 사고 신고 의무 2026-09-11 시행, 전면 적용 2027-12-11, 24h / 72h / 14일), SBOM(SPDX), CVE 대응 · 보안 업데이트 기간 | **2026 최신 트렌드** (EL K7 라이선스 재사용) | P1 | ❌ |

### M. 디버깅 · 트레이싱 · 테스트

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| M1 | HardFault 해독 | CFSR(MMFSR · BFSR · UFSR) · HFSR · MMFAR · BFAR, 스택에 쌓인 PC / LR 찾기, imprecise bus fault, fault 핸들러 덤프 작성 | **면접 · 현장 1순위** | P0 | 🟡 |
| M2 | RTOS-aware 디버깅 | J-Link · ST-Link · OpenOCD · pyOCD · probe-rs, GDB 스레드 인식(태스크 목록 · 각 스택), Ozone, 정지했을 때 주변장치 · 타이머 동작 | 일상 (EL M4 JTAG 지식 재사용) | P0 | ✅ |
| M3 | 이벤트 트레이싱 | SEGGER SystemView, Percepio Tracealyzer, FreeRTOS trace 매크로, Zephyr tracing(CTF), 타임라인 읽는 법 | 지터 · 역전 증명 | P0 | ❌ |
| M4 | 런타임 지표 | 태스크별 CPU 사용률, 스택 워터마크, 힙 최저치, 큐 최고 깊이, Zephyr thread analyzer | 헬스 모니터 | P1 | 🟡 |
| M5 | 크래시 수집 (MCU) | Zephyr coredump, noinit RAM 크래시 로그, 리셋 원인 레지스터, 원격 수집(Memfault 방식), 심볼화 | 필드 이슈 (EL L7은 Linux) | P1 | 🟡 |
| M6 | ARM 디버그 하드웨어 | DWT(CYCCNT · 워치포인트), ITM / SWO printf, ETM · MTB 명령 트레이스, CoreSight, 브레이크포인트 개수 한계 | 비침투 측정 | P1 | ✅ |
| M7 | RTOS 버그 패턴 사례집 | 스택 오버플로, ISR에서 블로킹 API, 우선순위 역전, 공유 변수 레이스, 힙 단편화, 우선순위 기아, 워치독이 버그를 숨김 — 증상 → 원인 표 | 디버깅 면접 | P0 | 🟡 |
| M8 | 단위 테스트 · 모킹 | Unity · CMock · Ceedling, Ztest, 호스트 테스트(HAL 모킹), FreeRTOS POSIX 포트 | 테스트 문화 | P1 | 🟡 |
| M9 | 시뮬레이션 · HIL · CI | QEMU, Renode(멀티노드 시뮬), Arm FVP, Twister, HIL 리그, 호스트 퍼징 | **FW Test 직군(Neros) 핵심** | P0 | 🟡 |
| M10 | 정적 분석 · 스택 분석 | cppcheck · clang-tidy · CodeChecker, 스택 사용량 분석(puncover), 콜 그래프, 복잡도 | 품질 게이트 | P1 | 🟡 |

### N. 실시간 분석 이론 — 원천 (수학 배경을 쓰는 곳)

> RTOS가 "마감을 지킨다"는 말을 **증명**하는 도구. UC Berkeley 수학 배경이 가장 빛나는 모듈이다.

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| N1 | 태스크 모델의 수학 | 주기 T · 실행시간 C · 마감 D · 위상, 이용률 U, 하이퍼피리어드, sporadic 모델 | 모든 분석의 언어 | P0 | ❌ |
| N2 | Rate Monotonic과 Liu-Layland | RM 최적성, 이용률 상한 n(2^(1/n) − 1) → ln 2, 조화(harmonic) 주기면 100% | **고전 면접 질문** | P0 | ❌ |
| N3 | 응답 시간 분석 (RTA) | R = C + B + Σ⌈R/Tj⌉·Cj 고정점 반복, 릴리스 지터, deadline monotonic | 실제 설계 검증 | P0 | ❌ |
| N4 | 블로킹 분석 | PIP vs PCP vs SRP의 블로킹 상한, 데드락 방지 증명, 중첩 락 | D3 · I9의 이론 근거 | P1 | ❌ |
| N5 | EDF와 동적 우선순위 | EDF 최적성(U ≤ 1), demand bound function, 과부하 때 도미노 효과, 고정 vs 동적 우선순위 비교 | 이론 심화 | P1 | ❌ |
| N6 | 비주기 작업 처리 | polling server, deferrable / sporadic server, 최소 도착 간격, 인터럽트를 태스크로 모델링 | 이벤트성 부하 | P2 | ❌ |
| N7 | WCET 분석 | 정적(aiT) · 측정 기반 · 하이브리드, 캐시 · 파이프라인 · 분기 예측 영향, 측정 WCET의 한계 | 안전 인증 필수 | P1 | ❌ |
| N8 | 멀티코어 실시간 | partitioned vs global 스케줄링(Dhall 효과), 공유 버스 · 캐시 간섭, 메모리 대역폭 조절(MemGuard) | 최신 연구 주제 | P2 | ❌ |
| N9 | 혼합 중요도 (mixed-criticality) | Vestal 모델, 시간 · 공간 분할, 중요도 모드 전환 | 국방 · 자동차 | P2 | ❌ |
| N10 | 오버헤드까지 넣은 실무 분석 | 컨텍스트 스위치 · tick · ISR 비용을 C에 반영, 계산 vs 실측 비교, 분석 도구(Cheddar, MAST) | 이론 ↔ 현장 다리 | P1 | ❌ |

### O. 기능 안전 · 인증 · 코딩 표준

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| O1 | 기능 안전 기초 | IEC 61508(SIL), ISO 26262(ASIL), DO-178C(DAL), IEC 62304, hazard · risk 분석, V-모델, 추적성 | 자동차 · 항공 · 국방 | P1 | ❌ |
| O2 | 안전 RTOS 지형 | SafeRTOS, QNX OS for Safety, VxWorks Cert, INTEGRITY, PikeOS / 오픈소스 인증 진행: Zephyr(IEC 61508 SIL 3 concept approval, 약 15k LOC 범위), ThreadX(v6.4.1 진행 중), SEooC 개념 | 2026 트렌드 | P1 | ❌ |
| O3 | 코딩 표준 | MISRA C:2023(+ amendment), CERT C, deviation 관리, 정적 분석기(Polyspace · CodeSonar · Coverity) | JD 단골 키워드 | P1 | 🟡 |
| O4 | 검증 · 커버리지 | 요구사항 기반 테스트, statement / branch / MC/DC 커버리지, 툴 qualification | 인증 프로젝트 일상 | P2 | ❌ |
| O5 | 안전 메커니즘 | lockstep 코어, ECC · 메모리 BIST, 클럭 모니터, 제어 흐름 모니터링, E2E 보호(CRC + 카운터), safe state 설계 | Cortex-R · 자동차 SoC | P1 | 🟡 |
| O6 | 시간 · 공간 분할 | ARINC 653, Cortex-R52 하이퍼바이저, RTOS on Xen · Bao, 파티션 간 통신 | 혼합 중요도 구현 | P2 | ❌ |
| O7 | 형식 검증 | seL4 증명, FreeRTOS CBMC 증명, TLA+ 프로토콜 검증, 모델 체킹(SPIN) | 연구 · 트렌드 | P2 | ❌ |

### P. 전원 · 멀티코어 · 이종 통합

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| P1 | MCU 전원 모드 | sleep / stop / standby / shutdown, 리텐션 범위, 웨이크업 소스와 깨어나는 시간, 클럭 트리 재설정 | 배터리 장비 | P1 | 🟡 |
| P2 | RTOS 전원 정책 | tickless + idle 훅, Zephyr PM 서브시스템(상태 · 지연 제약), 주변장치 사용 중 sleep 막기 | 소프트웨어 쪽 저전력 | P1 | ❌ |
| P3 | 에너지 측정 · 최적화 | 전류 프로파일(PPK2 · Otii · Joulescope), duty cycling, 평균 전류 계산, 배터리 수명 추정 | 수치로 말하기 | P2 | 🟡 |
| P4 | 비대칭 멀티코어 MCU | STM32H7(M7 + M4), nRF5340(앱 + 네트워크 코어), RP2350, i.MX RT1170 — HSEM · 메일박스 · 공유 RAM | 듀얼코어 MCU 설계 | P1 | 🟡 |
| P5 | Linux ↔ RTOS 원격 측 | OpenAMP · RPMsg-Lite 원격(remote) 구현, resource table, vring, 코어 시작 시점 (EL O2의 Linux 쪽 재사용) | Cortex-A + M SoC | P1 | ❌ |
| P6 | 하이퍼바이저 · 가상화 | Cortex-R52 EL2, 임베디드 하이퍼바이저(Xen, Bao, Jailhouse), RTOS 게스트, 주변장치 패스스루 | 자동차 존 컨트롤러 | P2 | ❌ |

### Q. 도메인 스택 — 드론 · 로봇 · 자동차 · AI · 스토리지

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| Q1 | PX4 아키텍처 | NuttX 위 uORB 모듈, work queue · 우선순위, 센서 → EKF2 → 컨트롤러 파이프라인, 루프 레이트 | **Neros · Anduril형 드론** | P0 | ❌ |
| Q2 | ArduPilot 아키텍처 | AP_HAL, ChibiOS 스케줄러, 메인 루프 vs 스레드, 타이밍 예산 | 드론 비교 | P2 | ❌ |
| Q3 | 비행 · 모터 제어 루프 | rate loop 1–8 kHz, IMU 샘플링 → 필터 → PID 지연 체인, ESC 프로토콜(DShot · PWM), 지터가 안정성에 주는 영향 | 드론 FW 면접 | P0 | 🟡 |
| Q4 | FOC 모터 제어 펌웨어 | 전류 루프(10–40 kHz) ISR 설계, PWM-ADC 동기, 엔코더, 속도 · 위치 루프를 태스크로 분리 | Nimble 교훈 — 포트폴리오 1순위 | P1 | ❌ |
| Q5 | 차량 ECU 펌웨어 | AUTOSAR 계층(BSW · RTE), 진단(UDS), CAN FD · CAN XL · 차량 이더넷, ECU 부트로더 | 자동차(Rivian) | P2 | ❌ |
| Q6 | TinyML on RTOS | LiteRT for Microcontrollers(TFLM) · CMSIS-NN · ExecuTorch, Ethos-U55 / U65 / U85 NPU 오프로드(U85 = transformer 지원), arena 메모리, 추론을 태스크로 스케줄 (Hark F2 · J1 재사용, RTOS 통합만) | AI 칩 진로 연결 | P1 | 🟡 |
| Q7 | 센서 허브 · 시간 동기 | 여러 센서 동시 샘플링, MCU 타임스탬프 → 호스트 시계 매핑, PPS 캡처 (Linux PTP는 EL Q3) | 센서 퓨전 | P1 | 🟡 |
| Q8 | 스토리지 컨트롤러 펌웨어 | 멀티코어 Cortex-R 위 실행부(executive) · RTOS, 명령 파이프라인 태스크 분할, NVMe 큐와 인터럽트 병합 | **Don 경력 다리** | P1 | ✅ |
| Q9 | IoT 제품 스택 | Matter · Thread 장치, 클라우드 OTA · 플릿 관리(Memfault · Golioth), 배터리 + 무선 + 보안 묶음 | 소비자 장치 | P2 | ❌ |

### R. 인터뷰 준비

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| R1 | RTOS 개념 Top 50 | 모듈 A–Q에서 뽑은 단골 질문 + 30초 · 2분 답 | 1차 기술 면접 | P0 | ❌ |
| R2 | "무슨 일이 일어나나" 시리즈 | 리셋 → 첫 태스크, 인터럽트 → 높은 우선순위 태스크 실행, `xQueueSend` → 컨텍스트 스위치, 뮤텍스 경합 → 우선순위 상속 | 깊이 확인 질문 | P0 | ❌ |
| R3 | 화이트보드 코딩 (RTOS 스타일) | 원자 연산으로 세마포어 만들기, 미니 스케줄러, 고정 블록 풀, 타이머 휠, ISR-safe 이벤트 플래그 (Anduril C 문제은행과 겹치는 링버퍼 · FSM은 링크만) | 라이브 코딩 | P0 | 🟡 |
| R4 | 디버깅 시나리오 | 랜덤 HardFault, 몇 시간 뒤 멈춤, 주기 지터, 업데이트 후 부팅 실패, 워치독 리셋 반복 → 단계별 진단 | 실무 감각 평가 | P0 | ✅ |
| R5 | 시스템 설계 | 드론 FC · 모터 컨트롤러 · 센서 허브: 태스크 / 우선순위 표 + RTA 계산 + 메모리 예산 + 업데이트 · 안전 | 시니어 라운드 | P1 | 🟡 |
| R6 | Don 스토리 브리지 | SSD FW(멀티코어 Cortex-R 실행부) · RF bring-up 경험을 RTOS 용어로 다시 말하기, "Zephyr 실무 경험 없음" 답변 | 행동 면접 | P0 | ✅ |

### S. 실습 프로젝트 — 손으로 증명하기

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don |
|---|---|---|---|---|---|
| S1 | 랩 만들기 | QEMU(mps2-an385 = M3 · mps2-an505 = M33 + TrustZone · mps3-an547 = M55), Zephyr native_sim, Renode, FreeRTOS POSIX 포트 / 선택: 실제 보드(Nucleo-H7, nRF5340 DK, Pico 2) | 모든 실습의 기반 | P0 | ❌ |
| S2 | 미니 RTOS 직접 만들기 | Cortex-M에서 PendSV 컨텍스트 스위치 + 우선순위 스케줄러 + tick + 세마포어 (약 500줄) | **원천 이해의 증명** | P0 | ❌ |
| S3 | FreeRTOS 실습 | 태스크 · 큐 · ISR 통지, 우선순위 역전 재현과 해결, 트레이스로 증명 | D · E 체득 | P0 | ❌ |
| S4 | Zephyr 센서 앱 | DT overlay + 센서 드라이버 + 셸 + 로깅 + Ztest / Twister CI | H 체득, JD 키워드 | P0 | ❌ |
| S5 | 스케줄 가능성 검증 | Python RTA 계산기 → 같은 태스크 세트를 실제로 돌려 응답시간 측정 · 비교 | N 체득 | P1 | ❌ |
| S6 | MCUboot 업데이트 | 서명 이미지 · swap · 롤백 · 전원 차단 테스트 | L 체득 | P1 | ❌ |
| S7 | 미니 비행 제어 루프 | IMU 시뮬 입력 → 1 kHz 제어 태스크 → PWM 출력, 지터 · 지연 예산 측정 | Q1 · Q3 체득, 드론 포트폴리오 | P1 | ❌ |
| S8 | Embassy async 앱 | 같은 센서 앱을 Rust Embassy로 다시 짜고 메모리 · 지연 비교 | I 체득, 트렌드 | P2 | ❌ |
| S9 | TinyML 추론 태스크 | Cortex-M55(+ Ethos-U FVP)에서 추론 태스크와 센서 태스크 공존, 지터 측정 | Q6 체득 | P2 | ❌ |

---

## 5. 2026 트렌드 지도 (2026-10-18 웹 확인)

| 트렌드 | 확인한 사실 | 관련 항목 |
|---|---|---|
| Zephyr가 사실상 표준으로 | 4.4.0 = 2026-04-14 (연 2회 릴리스로 전환, 930명+ 기여), 4.5 = 2026-10 예정, LTS 3.7(EOL 2029-07) → LTS4 = 4.6(2027-04 예정), Zephyr SDK 1.0(LLVM 실험 지원, C17 최소), Canonical Zephyr 26.04 LTS(2026-09-21, 최대 15년 유지보수 상용 배포판) | H1, H2 |
| FreeRTOS LTS 갱신 | 202604 LTS = kernel 11.3.0, MPU 개선(커널이 차지하는 리전 감소), coreMQTT MQTT v5, coreSNTP 2038 대응, 2년 지원 · Cortex-R82 SMP 포트(2026-04) | G4–G6 |
| 오픈소스 RTOS의 안전 인증 경쟁 | Zephyr: IEC 61508 SIL 3 concept approval(2024), 약 15k LOC 범위로 작업 중 — 아직 인증 완료 아님 · ThreadX: v6.4.1 IEC 61508 · ISO 26262 인증을 2026 목표로 진행 | O2, I2 |
| 규제가 펌웨어를 바꾼다 | EU CRA 신고 의무 2026-09-11 시행(24h 조기 경보 · 72h 통지 · 최종 보고), 나머지 의무 2027-12-11, MCU 펌웨어도 대상 | L7 |
| Rust 임베디드 | Embassy(async, 힙 없음 · 단일 스택), RTIC, Hubris, Tock, Ferrocene, Zephyr Rust 바인딩 | I7–I11, S8 |
| MCU 위의 AI | Ethos-U85(최대 4 TOPS, transformer 연산 지원), Cortex-M55 / M85 Helium, ExecuTorch · LiteRT Micro | Q6, B7, S9 |
| RISC-V MCU 확산 | ESP32-C 계열, RP2350(Arm + RISC-V 겸용), CLIC 표준화 | B10 |
| 멀티코어 · 이종 MCU | FreeRTOS · Zephyr SMP, nRF5340 · STM32H7 AMP, sysbuild 멀티 이미지 | C10, P4, H2 |
| 보안이 기본값 | Armv8-M TrustZone, PSA Certified, TF-M, MCUboot 서명 | F6, L2, L5 |
| 시뮬레이션 우선 개발 · CI | native_sim, Renode, Arm FVP, Twister, HIL | M9, H8 |
| 혼합 중요도 · 존 아키텍처 | Cortex-R52+ / R82, 하이퍼바이저 위 RTOS 게스트 | O6, P6, N9 |

---

## 6. 학습 순서 제안

| 단계 | 기간(제안) | 모듈 | 끝났을 때 할 수 있는 것 |
|---|---|---|---|
| Phase 1 원천 | 2주 | A, B, C, D, E + S1 · S2 | 리셋부터 컨텍스트 스위치까지 레지스터 수준으로 설명하고, 직접 만든 미니 RTOS로 증명 |
| Phase 2 플랫폼 | 2주 | F, G, H, J + S3 · S4 | FreeRTOS · Zephyr로 센서 앱을 짜고, 스택 · 메모리 · 드라이버 설계를 근거 있게 고름 |
| Phase 3 시스템 | 1–2주 | K, L, P + S6 | 업데이트 · 보안 · 저전력 · 멀티코어 설계를 말로 설명 |
| Phase 4 이론 · 품질 | 2주 | M, N, O + S5 | HardFault 해독, 트레이스로 지터 증명, RTA로 스케줄 가능성 계산, 안전 인증 대화 |
| 병행 | 전체 | I, Q, R + S7–S9 | 트렌드(Rust · Zephyr) 대화, 드론 · 차량 회사 맞춤 답변 |

> **1차 면접이 급하면**: `P0`만 먼저 — (64개) A1–A4, A7, B1–B5, C1–C5, C7, D1–D4, D8, E1–E4, E6, E8, F1, F3, G1, G3, H1–H3, H5, H6, J1–J4, J6, J7, K8, L1, L2, M1–M3, M7, M9, N1–N3, Q1, Q3, R1–R4, R6, S1–S4

---

## 7. 참고 자료 (노트 작성 시 1차 출처)

**무료 · 공식**
- FreeRTOS: 커널 문서 · *Mastering the FreeRTOS Real Time Kernel* (무료 PDF) · 소스 https://github.com/FreeRTOS/FreeRTOS-Kernel
- Zephyr 공식 문서: https://docs.zephyrproject.org/latest/ (kernel services, devicetree, build, testing)
- Arm: *Armv7-M / Armv8-M Architecture Reference Manual*, Cortex-M 예외 처리 · MPU 애플리케이션 노트
- NuttX 문서 https://nuttx.apache.org/docs/latest/ · PX4 개발 가이드 https://docs.px4.io/
- Embassy https://embassy.dev/ · RTIC book · Hubris 문서 · The Embedded Rust Book
- Memfault *Interrupt* 블로그(HardFault · 스택 · 부트로더 글), Embedded Artistry, Zephyr Developer Summit 발표

**책**
- Joseph Yiu, *The Definitive Guide to Arm Cortex-M3/M4* 및 *Cortex-M23/M33* — B 모듈 축
- Jean Labrosse, *µC/OS-III: The Real-Time Kernel* (무료 공개) — C · D 모듈 축 (커널 구현 해설)
- Giorgio Buttazzo, *Hard Real-Time Computing Systems* — N 모듈 축
- Burns & Wellings, *Real-Time Systems and Programming Languages* / Jane Liu, *Real-Time Systems* — N 보충
- Miro Samek, *Practical UML Statecharts in C/C++* + "Modern Embedded Systems Programming" 강의 — 이벤트 구동 · Active Object
- Phil Koopman, *Better Embedded System Software* — 설계 · 워치독 · 품질
- Elecia White, *Making Embedded Systems* 2nd ed. — 실무 전반

**2026 트렌드 확인 출처 (2026-10-18)**
- Zephyr 릴리스 목록 https://docs.zephyrproject.org/latest/releases/index.html · 4.4 발표 https://www.zephyrproject.org/zephyr-rtos-4-4-now-available-wireguard-wi-fi-direct-openrisc-and-more/
- Canonical Zephyr LTS https://canonical.com/blog/zephyr-lts-announcement
- FreeRTOS 202604 LTS https://aws.amazon.com/about-aws/whats-new/2026/04/freertos-lts/
- ThreadX 인증 진행 https://www.eclipse.org/lists/threadx/msg00151.html
- Zephyr 안전 인증 로드맵 https://www.inovex.de/en/?p=66655
- EU CRA 신고 의무 https://www.jonesday.com/en/insights/2026/07/eu-cyber-resilience-act-24hour-reporting-duties-start-september-11-2026
- Ethos-U85 https://newsroom.arm.com/blog/ethos-u85 · Embassy https://embassy.dev/

**Don 기존 노트 재사용**
- [Embedded Linux 나침반](../../../EmbeddedLinux/site/compass/contents.html) — §3 경계표의 EL 항목
- Hark Embedded AI 노트: `dons_study_note_from_experience/embeddedAIPrepBasedOnHarkAIJD/` (F2 TFLite Micro, J1 · J2 실시간 · 추론 통합, J4 C++/Rust, E3 RISC-V)
- OSTEP 노트(스케줄링 · 락 · 조건변수), Verkada 동시성 노트, Anduril C 문제은행(RTOS 개념 세트)
- Neros 저지연 아키텍처 백서: `job_interview_prep_research/neros_tech_senior_firmware_engineer/2026-09-17_neros_low_latency_architecture_whitepaper_verified_kor.md`

---

## 8. 진행 현황

| 모듈 | 항목 수 | 노트 작성 | 읽음 | 메모 |
|---|---|---|---|---|
| A 실시간 · RTOS 개념과 지형 | 7 | 0 | 0 | |
| B MCU 하드웨어 기반 | 11 | 0 | 0 | |
| C 커널 내부 | 12 | 0 | 0 | |
| D 동기화 · 태스크 간 통신 | 10 | 0 | 0 | |
| E 인터럽트 · 타이밍 · 결정성 | 8 | 0 | 0 | |
| F 메모리 관리 · 보호 · 격리 | 9 | 0 | 0 | |
| G FreeRTOS 실무 | 7 | 0 | 0 | |
| H Zephyr 실무 | 10 | 0 | 0 | |
| I 다른 RTOS · 신세대 실행 모델 (Rust) | 11 | 0 | 0 | |
| J 드라이버 · 주변장치 통합 | 9 | 0 | 0 | |
| K 미들웨어 · 연결성 | 8 | 0 | 0 | |
| L 부트 · 업데이트 · 보안 | 7 | 0 | 0 | |
| M 디버깅 · 트레이싱 · 테스트 | 10 | 0 | 0 | |
| N 실시간 분석 이론 | 10 | 0 | 0 | |
| O 기능 안전 · 인증 · 코딩 표준 | 7 | 0 | 0 | |
| P 전원 · 멀티코어 · 이종 통합 | 6 | 0 | 0 | |
| Q 도메인 스택 | 9 | 0 | 0 | |
| R 인터뷰 준비 | 6 | 0 | 0 | |
| S 실습 프로젝트 | 9 | 0 | 0 | |
| **합계** | **166** | **0** | **0** | 2026-10-18 목차 작성 — 노트는 Don 승인 후 모듈 단위로 |
