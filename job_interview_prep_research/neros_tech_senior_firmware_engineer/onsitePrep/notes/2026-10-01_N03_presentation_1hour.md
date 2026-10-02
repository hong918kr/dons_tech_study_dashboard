# 🎤 N03 · 경력 발표 1시간 — 구성 · 슬라이드 · 리허설

> 온사이트에서 Don이 주도하는 유일한 시간이다. 리크루터가 준 주제는 세 가지: **가장 큰 challenge**, **issue resolving/debugging**, **bring-up 경험**. 이 노트는 시간 배분, 슬라이드 20장 뼈대, 사례 고르는 법, 예상 질문, 기밀 처리, 리허설 계획을 담았다. 마지막은 Neros의 지금 문제(테스트 FW, factory test FW)로 끝낸다.

## 0. 이 발표가 평가하는 것

| 면접관이 보는 것 | 발표에서 보여 줄 방법 |
|---|---|
| 기술 깊이: 정말 본인이 했나 | 한 사례를 **바닥까지** 판다. 파형, 레지스터, 가설 순서 |
| 디버깅 사고방식 | 가설 → 증거 → 제거의 순서를 그대로 보여 준다 |
| bring-up 경험 | 아무것도 안 될 때 어디서부터 시작하는지 |
| 소통 | 청중(FW, Test, Platform)에 맞춘 설명, 질문 받는 태도 |
| 우리 회사에 맞는가 | 마지막 5분: Neros의 문제에 내 경험을 연결 |

- 면접관은 여러 명일 가능성이 높다: Adam(Director), Michael(Test), Platform HM, FW 엔지니어 [추정]
- **발표 시간 안에 질문이 끼어든다고 가정**하고 본 발표를 **40분**으로 짠다. 남는 20분은 질문

## 1. 시간 배분 (60분)

| 구간 | 시간 | 내용 |
|---|---|---|
| 오프닝 | 3분 | 나는 누구인가, 경력 한 줄 줄거리, 오늘 할 이야기 |
| Part 1 · Bring-up | 10분 | 실리콘 이전(FPGA)부터 MP까지 bring-up을 어떻게 해 왔나 |
| Part 2 · 가장 큰 challenge (디버깅 사례 심층) | 15분 | **사례 하나**를 처음부터 끝까지 |
| Part 3 · 테스트 인프라와 재발 방지 | 7분 | 챔버 SDK, reliability system, factory test-node |
| Part 4 · Neros에 가져올 것 | 5분 | 테스트 FW, HIL, factory test FW 30/60/90일 |
| Q&A | 20분 | 중간 질문 포함 |

## 2. 슬라이드 20장 뼈대

| # | 제목 (영어) | 내용 | 재료 |
|---|---|---|---|
| 1 | Title | 이름, "Bring-up, Debugging, and Test Infrastructure on Real Hardware" | — |
| 2 | About me | 7년 타임라인 한 장: SK hynix App SW → Senior FW → Solidigm Staff FW → Apple | 레쥬메 |
| 3 | The thread | "Testing and debugging real hardware is the thread through every role" | FW Test 준비 N09 0절 |
| 4 | Agenda | 4개 Part | — |
| 5 | Bring-up · where I've done it | FPGA 단계 SoC(Cortex R8/R82/M0+, PCIe/NVMe IP) → production SSD → Apple 새 RF 칩의 system bring-up → NPI → MP | 레쥬메 |
| 6 | Bring-up · my playbook | 전원·클럭 → 디버그 포트 → 메모리 → 버스 하나씩 → DMA·인터럽트 → 멀티코어 → 성능 | 아래 3절 |
| 7 | Bring-up · pre-silicon (FPGA) | RTL freeze 전에 I2C, SPI, DMA, PCIe 컨트롤러, SRAM/DRAM 검증. FW 버그 vs RTL 버그 vs FPGA 빌드 가르기 | FW Test N09 S4 |
| 8 | Bring-up · a story | (Don: FPGA 단계에서 실제로 막혔던 것 하나와 해결) | — |
| 9 | Bring-up · lessons | 가장 작은 재현 단위, 한 번에 변수 하나, 모든 bring-up 단계는 나중에 테스트가 된다 | — |
| 10 | Biggest challenge · the problem | 증상, 영향, 왜 어려웠나 (소유 팀 없음, 간헐적, 시스템에서만 재현) | 4절에서 고른 사례 |
| 11 | Biggest challenge · first hypotheses | 처음 세운 가설 목록과 각 가설을 확인할 방법 | — |
| 12 | Biggest challenge · making it reproducible | 재현율을 올린 방법 (stress, 조건 겹치기, 자동 반복) | — |
| 13 | Biggest challenge · the evidence | 스코프·protocol analyzer 캡처 (**다시 그린 개념도**, 실제 캡처 아님) | — |
| 14 | Biggest challenge · root cause & fix | 원인, 수정, 누구와 어떻게 합의했나 | — |
| 15 | Biggest challenge · preventing it | 추가한 테스트, test-node, 체크리스트 | — |
| 16 | Test infrastructure I built | 챔버 플랫폼 SDK, chip reliability system, NVMe telemetry 디버그, factory test-node 한 장 | FW Test N09 S12·S10·S8·S2 |
| 17 | What I learned | 3줄: 재현이 반이다 / 증거로 설득한다 / 모든 버그는 테스트로 끝난다 | — |
| 18 | At Neros · what I heard | 테스트 FW가 가장 급하다, factory test FW가 없다, 1,200대/주에서 1년 100만 대 목표 | 컨텍스트 §6 |
| 19 | At Neros · 30/60/90 | HIL smoke on every MR → factory test FW v0 on one product → 확장 | N08 |
| 20 | Thank you / Questions | 연락처 없이 간단히 | — |

- **백업 슬라이드 3장**: NVMe telemetry 디버그 기능 구조, shmoo 예시 그림, error reporting scheme 구조. 질문이 오면 꺼낸다

## 3. Part 1 — Bring-up 플레이북 (슬라이드 6에 그대로)

1. **전원과 클럭**: 레일 전압, 시퀀스, 리셋 라인, 크리스털·PLL lock
2. **디버그 포트**: JTAG/SWD로 코어에 붙는가, 레지스터를 읽을 수 있는가 (Trace32)
3. **메모리**: SRAM 테스트, DRAM 초기화와 트레이닝, 링커 배치
4. **버스 하나씩**: 레지스터 읽기 → 단일 트랜잭션 → 연속 → 오류 처리 (I2C, SPI, UART)
5. **DMA와 인터럽트**: 단독 → 동시
6. **멀티코어**: 코어 간 통신, 공유 메모리
7. **고속 인터페이스**: PCIe link training, 링크 안정성
8. **성능과 마진**: 정상 동작 확인 후 shmoo로 여유 측정

> 영어 한 줄: "My bring-up rule is: smallest step that can fail, one variable at a time — and every step I verify by hand during bring-up becomes an automated test afterwards."

- **Neros 연결**: STM32 보드 revision bring-up도 같은 순서다. 전원 → SWD → 클럭(HSE/PLL) → UART 콘솔 → IMU SPI → 각 UART(수신기, GPS) → 타이머(DShot) → 플래시

## 4. Part 2 — "가장 큰 challenge" 사례 고르기

**좋은 사례의 조건** (위에서부터 중요):

- [ ] Don이 **직접** 원인을 찾았다 (팀이 찾은 것 말고)
- [ ] 간헐적이거나 재현이 어려웠다
- [ ] 여러 팀이나 계층(HW, FW, 칩)이 얽혔다
- [ ] 측정 도구(스코프, protocol analyzer, Trace32)로 증거를 잡았다
- [ ] 결말이 있다: 수정 + 재발 방지 테스트
- [ ] 기밀을 일반화해도 이야기가 성립한다

**후보** (레쥬메 기반, Don이 실제 사례로 고른다):

| 후보 | 출처 | 장점 | 주의 |
|---|---|---|---|
| A. 새 칩의 시스템 레벨 인터페이스 실패 (PCIe/I2C/SPMI/RFFE) | Apple | 가장 최근, 크로스팀, 측정 장비 | **기밀 일반화 필수** |
| B. FPGA 단계에서 FW 버그인지 RTL 버그인지 가린 사례 | Solidigm | bring-up과 디버깅을 한 번에 | 오래된 디테일 기억 |
| C. 고객 qualification 중 발견된 문제 (telemetry, error handling) | SK hynix/Solidigm | 고객 영향, 데이터센터 규모 | 고객명은 레쥬메에 있는 수준까지만 |
| D. 고속 인터페이스 마진 문제 (shmoo로 발견) | SK hynix | 측정 중심, 시각 자료가 좋다 | — |

- 추천: **A를 메인**(최근이고 Neros의 "시스템에서만 재현되는 버그"와 같은 성격), 시간이 남거나 질문이 오면 **B를 보조**
- (Don: 고른 사례를 아래 표에 채운다)

| 칸 | 내용 |
|---|---|
| 증상 한 줄 | (Don) |
| 왜 어려웠나 | (Don) |
| 처음 가설 3개 | (Don) |
| 재현율을 높인 방법 | (Don) |
| 결정적 증거 | (Don) |
| 원인 | (Don) |
| 수정과 합의 과정 | (Don) |
| 추가한 테스트 | (Don) |
| 걸린 기간과 영향 | (Don) |

## 5. 기밀 처리 규칙

- Apple: **제품명, 칩 이름, 벤더 이름, 일정, 내부 툴 이름, 실제 캡처 화면은 쓰지 않는다.** "a new RF chip", "a control bus", "a power state transition"처럼 일반화
- 캡처는 **직접 다시 그린 개념도**로 대체한다 (타이밍 다이어그램을 손으로)
- SK hynix/Solidigm: 레쥬메에 나온 수준(고객명 Microsoft/Dell/HPE, Google/Meta)까지만
- 발표 첫 장 또는 말로: "Details are generalized to respect confidentiality." → 오히려 신뢰를 준다
- (Don: 재직 중인 Apple의 외부 발표 정책 확인. 애매하면 Part 2 메인을 B나 D로 바꾼다)

## 6. Part 4 — Neros에 가져올 것 (5분 스크립트)

> "Here's what I heard from the team: firmware test is the most urgent need, and there's no factory test firmware yet — which you'll need as you scale from about twelve hundred drones a week toward much higher volume.
>
> That's exactly the intersection I've worked in. At Apple I designed factory test-node architecture to catch latent defects before mass production. At SK hynix I built the chamber test platform other engineers used, and the debug firmware features that made failures observable.
>
> In my first 30 days, I'd learn the products and run every existing test myself. By 60, I'd have a HIL smoke test gating merges for one product line. By 90, a first version of factory test firmware for one board — a command interface, per-peripheral self-tests, and results tied to the unit's serial number and firmware hash. Then scale it across the product lines."

- 숫자 "1,200 a week"는 공개 보도 기준 [컨텍스트 §2.4]. 면접에서 들은 최신 숫자가 있으면 그걸 쓴다
- 상세 설계와 30/60/90은 [N08 factory test firmware](2026-10-01_N08_factory_test_firmware.md)

## 7. 예상 질문 (발표 중간·끝)

| 질문 | 답 방향 |
|---|---|
| "What exactly did *you* do vs the team?" | "I" 문장으로 다시. 내가 세운 가설, 내가 만든 재현 조건, 내가 잡은 증거 |
| "How long did it take? What would you do faster now?" | 기간 + 지금이라면 처음부터 자동 반복과 캡처 트리거를 걸겠다 |
| "Why didn't the chip validation catch it?" | 칩 단독 vs 시스템 조합. 그래서 시스템 레벨 stress 테스트가 필요하다 |
| "How did you convince the other team?" | 증거(캡처)와 재현 스크립트를 넘겼다 |
| "What's your bring-up order on a brand-new board?" | 3절 플레이북 |
| "How would you apply this to a drone FC?" | STM32 bring-up 순서, HIL로 재현 자동화 ([N02](2026-10-01_N02_stm32_what_it_means.md)) |
| "Have you done this on an RTOS / STM32 / Linux?" | 정직하게. 개념이 같다는 것과 학습 계획 |
| "What's the hardest bug you *didn't* solve?" | 준비해 둘 것: (Don: 해결 못 한 사례, 그리고 배운 점) |

## 8. 슬라이드 만드는 원칙

- 한 장에 메시지 하나. 제목을 **문장**으로 ("The failure only appeared under concurrent traffic")
- 글은 적게, 그림은 많이: 블록도, 타이밍도, 가설 트리
- 코드는 최대 10줄
- 폰트 크게 (회의실 화면), 다크 배경은 프로젝터에서 흐려질 수 있다
- 파일은 PDF로도 준비 (회의실 PC에서 열 경우), 노트북 + HDMI/USB-C 어댑터
- 발표 자료를 미리 보내 달라고 하면 보낸다. 안 물어보면 리크루터에게 형식(화면 공유? 회의실 화면?)을 물어본다

## 9. 리허설 계획

| 회차 | 무엇 | 목표 |
|---|---|---|
| 1 | 슬라이드 없이 아웃라인만 보고 말하기 | 이야기 흐름 확인, 40분에 들어오는지 |
| 2 | 슬라이드 초안으로, 녹음 | 막히는 문장 찾기, 영어 표현 다듬기 |
| 3 | 타이머 걸고 끝까지 + 7절 질문을 스스로 던지기 | 40분 ± 3분 |
| 4 | 누군가 앞에서 (가능하면) | 중간 질문에 흐름을 잃지 않기 |

- 영어 팁: 각 슬라이드 **첫 문장을 외운다**. 나머지는 자연스럽게
- 끊겼을 때 복귀 문장: "Let me come back to where we were — the evidence."

## 10. 연결 자료

- 레쥬메 bullet별 스토리 13개: [FW Test 준비 N09](../../firmwareTestEngineerPrep/site/notes/2026-09-29_N09_resume_storylines.html)
- 테스트·디버깅 시나리오 22개: [FW Test 준비 N08](../../firmwareTestEngineerPrep/site/notes/2026-09-29_N08_test_and_debug_scenarios.html)
- factory test FW: [N08](2026-10-01_N08_factory_test_firmware.md)

## 체크

- [ ] Part 2 메인 사례를 골랐고 4절 표를 채웠다
- [ ] Apple 사례의 기밀 일반화를 점검했다 (제품·칩·벤더·일정·툴·캡처 없음)
- [ ] 슬라이드 20장 초안을 만들었다 (백업 3장 포함)
- [ ] Part 4 스크립트를 들은 정보로 다듬었다
- [ ] 리허설 3회 이상, 40분 ± 3분
- [ ] 7절 질문 8개에 답을 준비했다 ("해결 못 한 버그" 포함)
- [ ] 발표 형식(화면 공유인지, 회의실 화면인지)을 리크루터에게 확인했다
