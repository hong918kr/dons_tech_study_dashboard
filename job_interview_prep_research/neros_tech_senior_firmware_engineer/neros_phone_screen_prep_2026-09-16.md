# Neros 폰스크린 (리크루터 Devin) — 내일 스터디 시트

> **콜 예정**: 2026-09-16 · 30분 · **상대**: Devin (Recruiter) · **포지션 표기**: "Principal/Senior Embedded Software Engineer"
> **작성**: 2026-09-15 · 상세 조사는 `neros_tech_senior_firmware_engineer_context.md` 참고
> 신뢰도: `[확인됨]` 공식·복수 출처 · `[추정]` 추론 · `[영상요약·미검증]` Don이 받은 CEO/CTO 영상 요약에서 온 내용

---

## 0. 내일 30분의 목표

1. **"이 사람 다음 단계 보내자"** 라는 결론을 만드는 것. 리크루터 콜은 기술 검증이 아니라 **동기 · 배경 요약 · 로지스틱스** 체크임
2. **어느 팀·어느 req인지 알아내기** ("Principal/Senior Embedded Software Engineer"는 실제 공고에 없는 우산 타이틀임 → 2.3 참고)
3. **다음 라운드 포맷과 일정 확보** + ITAR/이주/연봉 밴드 정렬

### 절대 하지 말 것
- CEO 이름을 **"Saurin Shah"** 라고 말하기 → **틀렸음. 1번 섹션 필독**
- 영상에서 본 숫자를 단정적으로 인용하기 ("2분마다 1대라면서요") → "제가 본 인터뷰에서는 …라고 하시더라고요" 정도로
- 리크루터에게 보드 레벨 병목·아키텍처 질문 쏟아붓기 → 답 못 함. 그 질문은 **HM 라운드용으로 아껴둘 것**
- Apple 험담, 현재 팀 불만 토로
- 30분 콜에서 5분짜리 자기소개 → TMAY는 **60~90초**로 끊기

---

## 1. ⚠️ 먼저 고칠 사실 오류

| 요약본 내용 | 실제 | 조치 |
|---|---|---|
| "CEO Saurin Shah" | **CEO는 Soren Monroe-Anderson** (공동창업자, 전 프로 드론 레이서, Thiel Fellow). CTO는 **Olaf Hichwa** [확인됨] | 팟캐스트 진행자가 "Soren"을 부르는 걸 옮겨 적으면서 생긴 오류로 보임. **"Saurin Shah"라는 이름은 입 밖에 내지 말 것** |
| 발음 | Soren = "**소렌**" / Hichwa = "**히크와**" 정도 [추정] | 이름을 굳이 말할 필요는 없음. "your CEO", "the founders"로 충분 |
| "업무시간 기준 2분마다 1대" | 공식 수치인 **주당 약 1,200대** 와 계산이 맞음 (8시간 × 60분 ÷ 2분 = 240대/일 × 5일 = 1,200대/주) [정합] | 이건 써도 안전함. 단 "주당 1,200대"라는 **공식 숫자 쪽을 인용**하는 게 더 안전 |

### 영상 요약에서 온 것들 — 미검증 표시
아래는 Don이 받은 요약본에만 있고 공개 기사로는 확인하지 못했음 [영상요약·미검증]. **내용 자체는 인터뷰 소재로 아주 좋지만**, "제가 본 CTO 인터뷰에서"라는 단서를 반드시 붙일 것.
- Board A / Board B (부품 수급 대비, 다른 칩셋으로 동일 기능 보드를 병행 설계)
- 스케일 단계별 진화: 월 100대 = 직접 납땜 → 월 1,000대 = 커넥터 Poka-yoke → 월 10,000대 = 공급망 한계
- Bandit 250km/h 이상, Archer와 부품 80% 공유
- $5,000 군용 무전기 대신 $50 자체 통신 모듈 (수직계열화), 완제품 대신 핵심 칩 비축
- "Super IC / Responsible Engineer" 인재상, 다중 제품 확장에 따른 조직 카오스
- 데모용 기능(1,000대 스웜) 배제, 전장에서 실제 작동하는 것만

**공식 출처로 확인된 것** [확인됨]: Archer AI의 Terminal Guidance와 GPS-denied Position Hold, Bandit이 Class 2/3 (Shahed급) 요격용, 자체 C2·비디오 라디오 in-house 설계, 중국 부품 배제 + Blue UAS, Torrance 250,000 sqft 공장, 2028년 연 100만 대 목표.

---

## 2. 콜 전 5분 브리핑

### 2.1 암기할 숫자 10개
| 숫자 | 내용 |
|---|---|
| 2023 | 창업 (Soren Monroe-Anderson, Olaf Hichwa) |
| $2.5B | 2026-08 Series C 밸류 ($250M 조달, Sequoia + ASTF 공동 리드) |
| $500M | US Army PBAS IDIQ 상한, 5년 (2026-07 발표) |
| ~1,200대/주 | 현재 Archer 생산 속도 |
| 100만 대/년 | 2028년 목표 |
| 250,000 sqft | Torrance 공장 |
| 250명+ | 임직원 (SpaceX·Palantir·Anduril 출신 포함) |
| ~$2,000 | Archer 대당 가격, 사거리 20km+ |
| 8,000대 / 6,000대 | USMC 계약 물량 / 우크라이나 공급 물량 |
| $195K–273K | Senior Firmware Engineer, Platform 공고의 base 밴드 |

### 2.2 제품 3줄
- **Archer**: 5~10인치 FPV strike 쿼드, 모듈형 탄두, 중국 부품 없음, Blue UAS 인증
- **Archer AI**: Archer + autonomy. 링크가 끊기는 종말 단계에서 비전으로 타겟을 물고 들어가는 **Terminal Guidance**, GPS 없이 버티는 **Position Hold**
- **Bandit**: Shahed급(Class 2/3)을 잡는 **요격 드론**. 고속 비행이 칩·모터를 한계로 밀어붙임

### 2.3 ⚠️ 포지션 타이틀 불일치 — 반드시 물어볼 것
Devin이 쓴 **"Principal/Senior Embedded Software Engineer"** 는 현재 Neros 채용 페이지에 **그대로 존재하지 않는 타이틀**임 (2026-09-15 확인). 현재 열려 있는 firmware/embedded 계열은 이렇음:

| req | 밴드 | 성격 |
|---|---|---|
| **Senior Firmware Engineer, Platform** | $195–273K | 공통 runtime·logging/telemetry/IPC·**Bazel 빌드**·SDK (Don이 조사한 그 공고) |
| Firmware Engineer – Flight Software | $145.5–204K | Betaflight/PX4 포팅, flight mode, 벤치·비행시험 |
| Senior Embedded Linux Engineer | $165–231K | Yocto/kernel/U-Boot/BSP |
| Flight Software Manager / Ground Software Manager | $198–277.5K | 매니저 트랙 |
| Firmware Engineer – Tennessee Office | $138.5–194K | Zephyr/FreeRTOS/STM32, TN 근무 |

→ **"제가 본 공고는 Senior Firmware Engineer, Platform이었는데, 지금 논의하는 req가 그건가요? 아니면 Principal 레벨로 따로 열린 자리인가요?"** 를 초반에 정리하면 나머지 30분의 조준이 달라짐. 파이프라인성 통합 req일 가능성이 큼 [추정].

---

## 3. 30분 콜 진행 시나리오

```
0–2분    인사 · 스몰토크 (LA 날씨, 지원 경위)
2–5분    Devin의 회사·포지션 소개  → 메모하며 듣기. 여기서 팀/레벨/프로세스가 나옴
5–7분    "Tell me about yourself"  → 60~90초 스크립트 (4.1)
7–15분   배경 질문: 최근 일, Apple에서 뭐 하는지, 왜 이직, 드론/방산 관심
15–22분  로지스틱스: 이주, onsite, ITAR/US Person, 연봉 기대, 시작 가능 시점, 타사 진행 상황
22–28분  내 질문 3~4개 (6번 섹션)
28–30분  다음 단계·일정 확인 + 감사 인사
```
- 30분은 짧음. **답변 1개당 60~90초**를 넘기지 말 것. 길어지면 로지스틱스에서 잘림
- 메모지에 6번 역질문 + 2.3 표만 프린트해 두면 충분

---

## 4. 영어 답변 스크립트

### 4.1 Tell me about yourself (60~90초)
> I'm an embedded firmware engineer with about eight years in production firmware. I spent roughly seven years at SK hynix and Solidigm writing production SSD firmware in C on multi-core ARM Cortex-R and M and Xtensa — from FPGA bring-up before RTL freeze all the way to shipped PCIe Gen5 and Gen6 drives. Two pieces of that map closely to what you're building: I designed and shipped an NVMe telemetry-based logging and debug feature that data-center customers like Microsoft and Dell relied on, and I built a test-platform SDK that other engineering teams used to run large-scale reliability testing.
>
> Since last December I've been at Apple on the RF wireless chipset integration team. I lead integration of new silicon into shipping consumer platforms — I own root cause for interface-level failures, PCIe, I2C, SPMI, RFFE, at the chip-to-system boundary, and I drive that from bring-up through NPI into high-volume manufacturing, including the factory test-node architecture that catches latent defects before mass production.
>
> What pulled me toward Neros is that this is the same problem I already work on — firmware meeting real hardware at manufacturing scale — except the feedback loop is months instead of years, and the thing you ship actually matters. I'd like to hear where you think I'd fit best.

**45초 압축 버전** (Devin이 서두를 때):
> Eight years in embedded firmware. Seven of those writing production SSD firmware in C at SK hynix and Solidigm — multi-core Cortex-R and M, bring-up through shipped product, plus the telemetry and test infrastructure other teams used. Since December I've been at Apple integrating new wireless silicon into shipping platforms: I own root cause for interface-level bugs at the chip-to-system boundary, and drive bring-up through NPI into mass production. I want to do that on hardware that ships in months, not years — which is why Neros.

### 4.2 Why Neros? (가장 중요, 60초)
> Three things. First, leverage at scale — you're producing over a thousand Archers a week and heading toward a million a year. At that volume a firmware or integration decision multiplies across every unit, and that's exactly the kind of decision I've been making at Apple, where a marginal interface issue becomes a factory-wide problem.
>
> Second, the philosophy. From the founders' interviews, what struck me is that you delete anything that only works in a demo — jamming, moving targets, weather, that's the bar. I come from storage firmware where "works on the bench" means nothing; what counts is the margin the part still has after reliability, power and thermal stress. Same instinct.
>
> Third, the mission. You're onshoring a supply chain that the U.S. genuinely lost, and the feedback loop from Ukraine means the work is real. I'd rather my firmware fly than sit in a spec review.

### 4.3 Why leave Apple after ~9 months? (가장 위험, 45초 · 방어적으로 말하지 말 것)
> It's a fair question and I'd rather be direct about it. Apple has been a great place to sharpen system-level debugging — I own root cause when new silicon meets the full platform. But the role is integration-heavy by design: I'm validating and signing off on other people's silicon more than I'm shipping code I own. Before Apple I spent seven years writing production firmware, and that's the work I want back — owning a codebase end to end, on a product cycle measured in months. I wasn't looking to leave for its own sake; this is a specific pull, not a general push.

- 꼬리질문 "Then why did you leave Solidigm?" → "The PCIe Gen6 program was a long silicon cycle, and I wanted system-level breadth beyond storage. I got that at Apple — now I want to put it back into firmware ownership."

### 4.4 그 외 자주 나오는 질문 (각 30~45초)
| 질문 | 답변 핵심 |
|---|---|
| What are you looking for in your next role? | 코드를 직접 소유하는 IC 역할, 하드웨어와 양산 라인이 가까운 환경, 빠른 사이클. 매니저 트랙 아님 |
| What's your strongest technical area? | "The bugs nobody owns" — 칩과 시스템 경계에서 나는 인터페이스 결함. 스코프·프로토콜 분석기·JTAG로 신호 레벨까지 내려감 |
| Do you have drone / flight-stack experience? | **솔직하게 없다고 인정 + 전이 가능성**: "No flight stack yet. What I do have is bare-metal C on Cortex-M/R, sensor and bus integration over I2C/SPI, telemetry protocol design, and hardware-coupled debugging. I've started reading through Betaflight's parameter-group and blackbox code to understand the platform side." |
| Any defense / ITAR concerns? | (본인 신분 그대로) "I'm a [U.S. citizen / green card holder] — no ITAR issue." **신분이 애매하면 얼버무리지 말고 정확히 말할 것** |
| Comfortable working on lethal systems? | 짧고 분명하게. "Yes. Deterrence and allied defense — I'm clear-eyed about what this is, and I want to work on it." 망설이면 감점 |
| Relocation to Torrance? | 미리 결론 내고 들어갈 것. 열려 있다면 "Yes, I'd relocate for the right role — I'd want to understand the onsite expectation and timeline." 조건부면 조건을 정확히 |
| Compensation expectations? | **먼저 숫자를 던지지 말고 밴드를 물을 것**: "I saw the posted range for the platform role. I'd rather hear where this req sits and how you weight base versus equity — my current package is competitive, and I'm optimizing for scope and ownership, not just base." 밀어붙이면 "base in the $200K–240K range plus meaningful equity" 정도로 [본인 현재 패키지 기준으로 조정] |
| When could you start? | 2~4주 노티스 [본인 상황에 맞게] |
| Other processes? | 있으면 솔직하게 (Verkada 등). "One other embedded process in early stages — Neros is the one I've done the most homework on." |

---

## 5. 영상 기반 talking point → Don 경험 매핑

**쓰는 법**: 콜 전체에서 **1~2개만** 자연스럽게 흘릴 것. 전부 쏟아내면 외워온 티가 남.

| Neros의 고민 [영상요약·미검증] | Don의 매칭 경험 | 한 문장 영어 |
|---|---|---|
| **Board A / Board B** — 부품 수급 때문에 완전히 다른 칩셋으로 같은 기능의 보드를 병행 설계 | 아키텍처가 다른 실리콘(Cortex-R8/R82/M0+, Xtensa) 위에서 FW bring-up, FPGA 단계부터 ASIC까지 이식 | "I've brought the same firmware feature up across different architectures and silicon revisions — Cortex-R, Cortex-M and Xtensa, FPGA before RTL freeze through production silicon. Second-source boards are the same problem: keep the interface stable, isolate what's silicon-specific." |
| **Poka-yoke 커넥터** → 조립 실수 방지, DFM | Apple에서 **factory test-node 아키텍처** 설계·리드, 스트레스 기반 케이스로 MP 전에 잠재 결함 노출 | "At Apple I design the factory test nodes — stress-based scenarios that surface latent silicon and integration defects before mass production. Poka-yoke on the mechanical side, test coverage on the electrical side: same goal." |
| **데모용 기능 배제, 전장 신뢰성** | reliability vs performance/power **safety margin sign-off**, 챔버 신뢰성 인프라 구축 | "My job includes signing off on hardware safety margins — how much reliability margin we trade for performance and power. That's a 'does it survive the real world' judgment, not a demo judgment." |
| **Super IC / Responsible Engineer** (스스로 결정하는 최상위 IC) | 벤더·보드·FW 팀 경계를 넘는 이슈를 끝까지 책임지고 출하까지 해결 | "The failures I own don't belong to any one team — chip vendor, board, or firmware. Somebody has to hold it end to end, and that's been my role." |
| **Make vs Buy / 칩 비축** | 공급망·고객 요구(NVMe 2.0, Google·Meta)와 FW 설계 트레이드오프 경험 | "I've made feature and resource trade-offs driven by customer and supply realities, not just by what's technically elegant." |
| **스케일별 설계 진화** (월 100 → 1,000 → 10,000대) | bring-up → NPI → MP 전 사이클을 반복해 본 경험 | "I've walked the same curve on consumer hardware — what works at prototype volume quietly breaks at mass production." |

---

## 6. 내가 물어볼 질문 (리크루터용 · 4개면 충분)

1. **"The posting I studied was Senior Firmware Engineer, Platform — is this the same req, or is the Principal role a separate one? Which team would I be screening with?"** ← 최우선
2. **"What does the rest of the process look like, and who would I be talking to?"** (라운드 수, 기술 스크린 포맷, 코딩 언어, 온사이트 여부, 전체 기간)
3. **"How does the team think about onsite — is this five days a week in Torrance, and is relocation support part of the package?"**
4. **"For this req, what's the level and the base range, and how does equity work at the current stage?"**
5. (시간 남으면) **"You've gone from one product to three — Archer, Archer AI, Bandit — in about a year. How much is the firmware shared across them versus forked per program?"** ← 숙제해 온 티가 나면서도 리크루터가 답할 수 있는 수준

**HM 라운드용으로 아껴둘 질문** (내일 쓰지 말 것):
- Board B / 칩셋 교체 시 firmware bring-up 효율화 방식, 지금 가장 시급한 보드 레벨 병목
- 빌드가 이미 Bazel monorepo인지, CMake에서 마이그레이션 중인지
- Platform 팀과 Embedded Linux 역할의 경계
- 필드(우크라이나) 로그·telemetry 회수 파이프라인

---

## 7. 혹시 기술 질문이 나온다면 (리크루터 콜이어도 5%)

가볍게 던질 수 있는 것들 — **깊게 안 가고 2~3문장으로** 답하고 넘어갈 것.
- "Bare-metal이랑 RTOS 중 어디가 편한가?" → bare-metal 멀티코어 위주, FreeRTOS 개념은 익숙, 필요한 건 빠르게 흡수
- "C 어느 정도 쓰나?" → 7년간 production C, 최근에도 디버그 FW·테스트 코드 작성
- "가장 어려웠던 버그?" → **30초 압축판 1개 준비** (증상 → 스코프/프로토콜 분석기로 좁힘 → 근본 원인 → 크로스팀 fix → 재발 방지). 상세는 HM 라운드에서
- "빌드 시스템 경험?" → 솔직하게: 사내 빌드를 사용하는 쪽이었고 멀티 타깃 이미지·툴체인 이슈에는 익숙. Bazel cross-compile은 현재 직접 학습 중
- "Betaflight/PX4 아나?" → 없음, 대신 지금 코드 구조를 읽고 있음 (parameter groups, blackbox)

---

## 8. 콜 직후 (5분 안에)

메모할 것: **팀·req·레벨 · 밴드 · 다음 라운드 포맷과 면접관 · 일정 · ITAR/이주 답변 · Devin이 강조한 키워드**

그리고 이 디렉토리에서 `claude`를 열어 이렇게 말하면 컨텍스트 파일이 갱신됨:
```
Devin 콜 했어. 팀은 <X>, 레벨 <Y>, 밴드 <Z>, 다음은 <포맷>. 이런 질문 받았어: ...
```

---

## 9. 오늘 밤 체크리스트

- [ ] **CEO 이름 = Soren Monroe-Anderson** 각인 (Saurin Shah 아님)
- [ ] 2.1 숫자 10개 훑기 (특히 1,200대/주 · $2.5B · $500M · 100만 대/2028)
- [ ] 4.1 TMAY **소리 내어 3회** (90초 넘는지 타이머로 확인)
- [ ] 4.3 "Why leave Apple" 소리 내어 2회 — 변명 톤 아닌지 확인
- [ ] 4.2 "Why Neros" 소리 내어 2회
- [ ] **ITAR 신분 · 이주 가능 여부 · 희망 연봉 숫자** 세 가지 미리 결론 내기
- [ ] 6번 역질문 4개 + 2.3 표를 종이에 프린트 (화면 안 보고 말할 수 있게)
- [ ] 조용한 장소 · 통화 품질 · 헤드셋 확인, 콜 5분 전 대기
- [ ] 5번 매핑 중 **딱 2개**만 골라 표시해 두기 (Board B ↔ 멀티 아키텍처 bring-up / Poka-yoke ↔ factory test-node 추천)
- [ ] (여유 있으면) Archer AI Terminal Guidance가 왜 필요한지 한 문장으로 설명해 보기 — "링크가 끊기는 종말 단계에서 비전으로 타겟을 유지"

---

## 10. 출처

1. https://en.wikipedia.org/wiki/Neros — 창업자·제품·계약·생산량 (2026-09-15 재확인)
2. https://www.crunchbase.com/person/soren-monroe-anderson-8441 · https://theorg.com/org/neros-technologies/org-chart/olaf-hichwa — CEO/CTO 신원 확인 (2026-09-15)
3. https://www.prnewswire.com/news-releases/neros-raises-250m-series-c-at-2-5b-valuation-to-scale-autonomous-and-interceptor-drone-programs-302848736.html — Series C, Archer AI·Bandit 정의 (2026-09-15)
4. https://boards-api.greenhouse.io/v1/boards/nerostechnologies/jobs — 현재 열린 firmware/embedded req 목록과 밴드 (2026-09-15 확인)
5. https://www.govconwire.com/articles/neros-500m-army-archer-fpv-contract — Army $500M IDIQ (2026-09-14)
6. https://creators.spotify.com/pod/profile/ti-morse/episodes/38---Olaf-Hichwa--Co-Founder--CTO-of-Neros-e371tk5 — CTO Olaf 인터뷰 (Relentless 팟캐스트 #38). Don이 받은 요약본의 원 출처로 추정, 내용 미검증
7. Don이 공유한 CEO/CTO 영상 요약본 (2026-09-15 수신) — [영상요약·미검증] 표시 항목의 출처
