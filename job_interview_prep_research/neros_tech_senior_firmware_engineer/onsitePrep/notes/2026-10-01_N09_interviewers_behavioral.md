# 👥 N09 · 면접관별 대응 · 행동 질문 · Tour · 당일 준비

> 온사이트에 들어올 사람들(Adam, Michael, Platform HM, FW 엔지니어)이 각각 무엇을 보고 싶어 하는지, 행동·동기 질문의 답, Tour 30분을 정보 수집에 쓰는 법, 레벨·보상 이야기, 당일 체크리스트를 정리했다. 지금까지 한 말(리크루터 콜, Adam, Michael)과 **사실관계를 똑같이 유지**한다.

## 0. 지금까지 한 말 — 바꾸지 않는다

| 언제 | 누구 | Don이 한 말 |
|---|---|---|
| 09-16 | 리크루터 Devin | SK hynix/Solidigm 약 7년 SSD FW → 다른 영역을 보려고 Apple consumer device → RF 새 실리콘 통합, 범위는 넓었지만 조직이 세분화돼 기여에 한계 → 스타트업을 찾다 1년 반 전 받은 Neros 메일을 보고 콜드메일. **base 최소 $200K** |
| 09-18 | Adam Kibit | 위 스토리 + 칩셋 bring-up, scalable manufacturing FW로 기여 (상세: `../neros_hm_adam_technical_prep_2026-09-18.md`, `../2026-09-18_adam_interview_D-6h_prep.md`) |
| 10-01 | Michael Honor | 정의 안 된 것의 테스트: 격리된 unit부터 쌓는다 / done: 80%에서 내보내고 고객·타 팀 피드백으로 보완 (반응 좋음) |

- 경력은 "about seven years"
- Apple 이야기는 "R&R 한계"까지. 조직 비판 금지

## 1. 면접관별 관심사와 대응

### Adam Kibit — Director of Firmware

- **배경** [확인됨: 이전 조사]: Johnson Controls(Test Eng → SW Eng → Tech Lead, BSP를 Jenkins CI와 유닛테스트에 통합, IPC 프로토콜, **EOL 테스트 앱 개선으로 양산 throughput +20%**, 자동 테스트 시스템), Visteon 모듈형 플랫폼, Faraday·Aeris OTA·보안, 2025-05 Neros 합류
- **Adam이 보고 싶은 것** [추정]: 레벨(Senior냐 그 이상이냐), Test 조직을 키울 사람인지, 제조 테스트 감각, 플랫폼적 사고
- **꺼낼 것**: 발표 Part 4의 factory test 비전. Adam 본인이 **Test Engineer 출신이고 EOL 테스트를 개선한 사람**이라 factory test FW 이야기에 가장 공감할 사람이다
- **물어볼 것**: "How do you see the Test organization growing over the next year, and where does factory test firmware fit?"

### Michael Honor — Firmware Test 리드 (HM)

- **확인된 것**: 퀴즈형으로 묻는다. 테스트 철학을 본다. 2명을 데리고 있고 최대 5명을 더 뽑는다. factory test FW가 필요하다고 했다
- (Don: LinkedIn에서 배경 확인 — 테스트 출신인지 FW 출신인지, 이전 회사)
- **꺼낼 것**: 임베디드 기본기 퀴즈에 빠르고 정확하게 ([N07](2026-10-01_N07_embedded_quiz_bank.md)), HIL·factory test 설계 ([N08](2026-10-01_N08_factory_test_firmware.md))
- **지난번 보완**: PCIe를 SW 관점(enumeration → BAR → MMIO → DMA → MSI)으로 다시 말할 기회가 오면 짧게 보완해도 좋다: "Last time I answered PCIe at the pin level — from the software side, the flow is…"
- **물어볼 것**: "With up to five more hires, how would you split the team — HIL and CI, factory test, product-specific test?"

### Senior FW Platform HM (들어올 수 있음)

- 누구인지 미확인. "Lead, Platform Software" 공고가 열려 있어 리더 자리가 비어 있을 수도 있다 [추정] → Adam이 겸하거나 시니어 엔지니어가 들어올 수 있다
- **보고 싶은 것**: logging·telemetry·IPC·config 같은 공통 라이브러리 설계, 인터페이스 안정성, 빌드 시스템, MCU와 Linux 양쪽
- **꺼낼 것**: NVMe telemetry 디버그 기능(로깅 설계), error reporting scheme(여러 모듈이 쓰는 공통 코드), 챔버 SDK(다른 엔지니어가 쓰는 API), [N06](2026-10-01_N06_generic_system_design.md)의 로깅·프로토콜·config 설계
- **정직하게 말할 갭**: Bazel, Yocto, 커널 드라이버. "I've used, not owned"

### FW 엔지니어 (1:1 코딩·설계)

- C/C++ 기본기, ring buffer, system design. 실무자 시각에서 "같이 일하기 좋은가"
- 코딩할 때 **말하면서**, 질문을 먼저, 테스트로 마무리 ([N05](2026-10-01_N05_ring_buffer_onsite.md))

## 2. 행동·동기 질문

| 질문 | 답의 뼈대 (영어) |
|---|---|
| Why Neros? | "Real hardware at real scale, shipping in months. At your volume, test and bring-up are leverage — one good test protects every unit." + 미션 |
| Why leave Apple after less than a year? | "Apple gave me system-level breadth on new silicon, but the organization is highly divided, which limited how much I could own. I want a place where I can own a problem end to end." (이미 한 말과 같다) |
| Why a test role? (Test 트랙일 때) | "Test infrastructure on real hardware has been part of every role I've had, and at Neros it's the most urgent need. I can read and debug the firmware under test, not just report failures." |
| Defense / lethal products? | 거부감이 없다면 짧고 분명하게: 억지력, 동맹 방어. 애매하게 답하지 않는다 (Don: 본인 입장 확인) |
| Tell me about a conflict / disagreement | (Don: 데이터로 설득한 사례 — Apple 크로스팀 루트코즈가 좋다) |
| Tell me about a failure | (Don: 실패와 그 후 바꾼 습관. "그 뒤로 모든 버그는 테스트로 끝낸다") |
| Ambiguous requirements? | Michael에게 한 답의 강화판: "Start from what I can isolate and verify, build up, and make assumptions explicit so the team can correct them early." |
| What does "done" mean? | "Done is when it's useful to the person who needs it and we have a way to know when it breaks — so I ship at the point where feedback becomes more valuable than polishing, with tests that protect what's there." |
| Fast-paced, changing priorities? | NPI→MP 일정, 고객 qualification 대응 |
| Relocation to Torrance? | (Don: 결정 — 이주 가능 여부와 시기) |
| US Person / ITAR? | (Don: 본인 체류 신분 확인 후 사실대로) |

## 3. 레벨·보상 이야기 — 언제, 어떻게

- 온사이트에서 먼저 꺼내지 않는다. **오퍼 단계에서 리크루터와** 한다
- 다만 Adam이나 HM이 "어떤 역할에 관심 있나?"라고 물으면:

> "I'm excited about the test role — especially building factory test firmware and HIL from the ground up. I'd also welcome a scope where I can contribute to the firmware itself, since debugging the code under test is where I add the most."

- 사실 정리: FW Test 밴드 $145.5–204K, Don이 말한 최저선 $200K → **밴드 최상단 근처**를 요구하게 된다. 레벨이 Senior/Lead로 잡히는지가 관건 [추정]. Platform 밴드는 $195–273K
- (Don: 오퍼 때 말할 숫자와 우선순위 — base, equity, 이주 지원, 레벨 — 미리 정해 두기)

## 4. Tour 30분 — 정보 수집 시간으로 쓴다

**볼 것**

- 생산 라인 규모, 자동화 정도, 테스트 스테이션이 있는지, 사람이 손으로 하는 검사는 무엇인지
- HIL 리그나 테스트 벤치가 있는지, 몇 대인지
- 펌웨어 엔지니어 자리와 생산 라인의 거리

**물어볼 것 (가볍게)**

- "Where does a board get its firmware flashed and tested today?"
- "Which step on this line takes the longest or has the most rework?"
- "Is that a test fixture? What does it check?"

**쓸 곳**: 발표 Part 4와 이후 1:1에서 "On the tour I saw … — that's where factory test firmware would cut time." 한 문장이면 준비된 사람으로 보인다

- 투어 때 사진 촬영 금지일 가능성이 높다 (방산). 묻기 전에 찍지 않는다

## 5. 역질문 뱅크 (사람별로 2개씩)

| 대상 | 질문 |
|---|---|
| Adam | "What does success look like for the Test org a year from now?" / "How do you balance Betaflight and PX4 across products?" |
| Michael | "What's the current HIL setup, and what's still manual?" / "What would you want factory test firmware v1 to do?" |
| Platform HM | "How are logging and telemetry shared between the MCU side and the Linux side today?" / "How do test engineers interact with the platform team?" |
| FW 엔지니어 | "What kind of bug costs you the most time today?" / "How do you test a change before it flies?" |
| 누구에게나 | "Which STM32 families are you standardizing on?" / "How do field issues from Ukraine come back into engineering?" |

## 6. 당일 체크리스트

- [ ] 발표 자료: 노트북 + PDF 사본(USB, 이메일) + HDMI/USB-C 어댑터
- [ ] 레쥬메 인쇄본 몇 장
- [ ] 화이트보드용: ring buffer 순서를 머릿속에 ([N05](2026-10-01_N05_ring_buffer_onsite.md))
- [ ] 면접관 이름 메모 (Adam Kibit, Michael Honor, + 당일 받는 명단)
- [ ] 복장: 하드웨어 스타트업 공장 → 비즈니스 캐주얼, 편한 신발 (투어에서 걷는다)
- [ ] 물, 가벼운 간식 (여러 라운드 연속)
- [ ] 끝나고 바로 면접관별 질문 메모 → 컨텍스트 파일 §6

## 7. 끝난 뒤

- 24시간 안에 리크루터에게 짧은 감사 메일 (면접관별 한 줄 언급)
- 받은 질문을 전부 기록: 다음 라운드나 다른 회사에서 재사용

## 체크

- [ ] 0절의 이미 한 말을 다시 읽었다
- [ ] Michael Honor와 (가능하면) 다른 면접관 LinkedIn을 확인했다
- [ ] "Why leave Apple", "Why test role", "done", "ambiguous" 답을 소리 내어 연습했다
- [ ] 이주·US Person·레벨·보상에 대한 본인 입장을 정했다
- [ ] Tour에서 물어볼 질문 3개를 외웠다
- [ ] 사람별 역질문 2개씩 골랐다
