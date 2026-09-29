# 🗣️ N07 · 스토리 · 자기소개 · 영어 답변 · 역질문

> 45분 인터뷰의 절반 가까이는 말로 한다. 테스트 포지션용으로 다시 짠 60초 자기소개, STAR 스토리 3개, "왜 테스트 역할이냐" 답변, 갭 인정 문장, 역질문을 정리했다. 사실관계는 Devin과 Adam에게 이미 말한 스토리와 **똑같이 유지**하고, 숫자는 Don이 직접 채운다.

## 0. 규칙

- 리크루터 콜(09-16)과 Adam 인터뷰(09-18)에서 한 이직 스토리와 **사실관계를 바꾸지 않는다**: SK hynix/Solidigm 약 7년 SSD FW → 다른 영역을 보려고 Apple consumer device로 → RF 새 실리콘 통합, 넓은 범위였지만 조직이 세분화돼 기여 범위에 한계 → 스타트업을 찾다가 Neros
- 경력 기간은 "about seven years"로 말한다 (레쥬메 약 7년 1개월, Devin에게는 7년 3개월이라고 말함)
- Apple 이야기는 "R&R 한계"까지만. 조직 불만으로 넘어가지 않는다
- `(Don: …)` 칸은 **실제 기억으로 채운다.** 기억이 안 나는 숫자는 말하지 않는다

## 1. Tell me about yourself — 테스트 버전 (60초)

> I'm an embedded engineer with about seven years in firmware, and through every role I've built test infrastructure around real hardware.
>
> At SK hynix, I started by building a test platform for chamber reliability testing — a Python and web-based automation layer with APIs for temperature control, test scheduling, device status monitoring, and automated UART test sequences. Other engineers used it to run large-scale testing across the chambers. I then moved into production SSD firmware in C, where I also designed a chip reliability system covering our NAND and PCIe interface generations, and a telemetry-based debug feature that data-center customers like Microsoft and Dell relied on.
>
> Since last December I've been at Apple on RF chipset integration, designing the factory test-node architecture and stress-based test cases to catch latent silicon and integration defects before mass production.
>
> So what I bring to this role is a firmware engineer who builds HIL — I can write the test framework in Python, and when something fails, I can go down into the firmware, the bus capture, or the commit and find the root cause.

- 30초 버전은 1문단과 마지막 문단만 말한다
- 말하는 속도: 1분 안에 끝나도록. 녹음해서 70초가 넘으면 SK hynix 부분의 telemetry를 뺀다

## 2. 스토리 A — 챔버 테스트 플랫폼 SDK (HIL + 프레임워크 + 사용자)

**언제 쓰나**: "Tell me about a test system you built", "Have you built a framework other people used?", "HIL experience?"

| STAR | 내용 |
|---|---|
| Situation | SK hynix, 칩 신뢰성 테스트. 대형 챔버 여러 대에 eSSD 다수. 테스트를 사람이 손으로 돌리고 있었다 (Don: 당시 규모 — 챔버 수, 드라이브 수) |
| Task | 여러 엔지니어가 챔버를 공유하면서 테스트를 **자동으로, 동시에** 돌리고 결과를 한곳에서 분석할 수 있게 한다 |
| Action | web 기반 플랫폼에 테스트 자동화 스크립트를 통합했다. SDK API를 만들었다: 온도 제어, 테스트 시나리오 스케줄링, eSSD 상태 모니터링, **UART 기반 테스트 시퀀스 자동 실행**. 외부 벤더·IT·보안·네트워크 팀과 협업해 사내 테스트 인프라를 구축했다 |
| Result | 다른 엔지니어들이 SDK로 챔버를 직접 운용했다 (Don: 사용자 수, 절감된 시간, 동시 테스트 수 등 기억나는 숫자) |
| 배운 점 | 사용자가 엔지니어면 **API가 곧 제품**이다. 장비가 죽거나 드라이브가 응답을 멈추는 경우를 프레임워크가 먼저 처리해야 테스트 결과를 믿을 수 있다 |

> **영어 핵심 문장**: "The hardest part wasn't the happy path — it was making results trustworthy when a chamber drifted or a drive hung mid-test. The framework had to detect that and mark the run as an infrastructure failure, not a device failure."

- (Don: 위 문장이 실제 경험과 맞는지 확인. 다르면 실제로 겪은 인프라 실패 사례로 바꿀 것)

## 3. 스토리 B — Apple factory test-node · stress 기반 latent defect

**언제 쓰나**: "How do you decide what to test?", "Tell me about a bug you caught before release", RF 제품 테스트 질문

| STAR | 내용 |
|---|---|
| Situation | Apple RF-Hardware Chipset Integration. 새 RF 칩이 전체 HW/SW 시스템에 들어갈 때 인터페이스 레벨 실패가 생긴다 (PCIe, I2C, SPMI, RFFE) |
| Task | bring-up → NPI → MP 사이클에서 **칩 validation이 놓친 결함을 MP 전에** 잡는다 |
| Action | factory test-node 아키텍처를 설계했다. 시나리오와 stress 기반 case study를 만들었다. DSO와 protocol analyzer로 신호·버스·시스템 레벨에서 디버그하고 하드웨어 safety margin(신뢰성 vs 성능·전력)을 sign-off했다 |
| Result | (Don: 공개 가능한 수준으로 — 잡아낸 결함의 종류, 예: "an intermittent bus failure that only reproduced under X") |
| 배운 점 | happy path는 거의 통과한다. **결함은 corner와 stress에서 나온다**: 온도, 전원 전이, 동시 트래픽, 반복 |

> **영어 핵심 문장**: "Most latent defects don't show up in a functional test. They show up when you stack stress — temperature, power transitions, concurrent traffic, thousands of cycles. So I design the test around the failure mode first."

- ⚠️ Apple 내부 정보는 일반화해서 말한다. 칩 이름, 제품명, 일정은 말하지 않는다

## 4. 스토리 C — 테스트 실패를 FW 루트코즈까지 (개발자 출신 강점)

**언제 쓰나**: "A test fails intermittently — what do you do?", "Why should we hire a developer for a test role?", "Tell me about a hard bug"

| STAR | 내용 |
|---|---|
| Situation | (Don: 가장 자신 있는 버그 하나. SK hynix/Solidigm FW에서든 Apple 통합에서든. 증상이 간헐적이었던 것이 가장 좋다) |
| Task | 재현이 안 되거나 소유 팀이 불분명한 실패를 끝까지 추적한다 |
| Action | 재현 조건을 좁힌다 → 로그와 버스 캡처(protocol analyzer, Trace32, JTAG)로 가설을 검증한다 → 원인 코드나 하드웨어를 찾는다 → 수정 후 **재발 방지 테스트를 추가한다** |
| Result | (Don: 결과 — 수정, 재발 방지 테스트, 영향) |

> **영어 핵심 문장**: "Every bug I root-caused ended with a new test — otherwise we'd just find it again in the next release."

## 5. 거의 확실히 나오는 질문과 답

### "You've been a firmware developer. Why a test role?"

> Honestly, test infrastructure has been part of every job I've had — the chamber platform, the reliability system, and now factory test nodes at Apple. What I enjoy is the part where the firmware meets real hardware and something breaks. At Neros's production rate, a solid HIL and CI setup is what lets the firmware team ship fast without shipping regressions, and I think a tester who can read and debug the firmware is more useful than one who can only report a red build.

- 레벨·보상 이야기는 여기서 꺼내지 않는다. 리크루터와 따로 한다
- HM이 "원래 Senior FW 포지션으로 인터뷰했었죠?"라고 물으면: "Yes — Adam suggested this role might be a strong fit given my test infrastructure background, and after reading the job description I agree there's a lot of overlap. I'd love to hear how you see the role."

### "Why Neros?" (Adam 때와 같은 뼈대)

> I was looking for a fast-moving hardware company where my work reaches the field quickly. Neros builds real hardware at real scale — thousands of drones a week — and at that scale, test automation is leverage: one good HIL test protects every unit that ships. And the mission matters to me.

### "Have you used GitLab CI or Jenkins?" (갭 인정 → 전환 → 학습)

> Not as the owner of the pipeline config — at SK hynix, orchestration lived inside our own chamber test platform, which handled queuing, scheduling, and result collection. The concepts map directly: jobs on shared hardware, exclusive access to a rig, artifacts, and pass/fail reports. I've been working through GitLab CI specifically — tagged self-hosted runners for the rigs, resource_group for exclusive access, JUnit reports — and I'd be comfortable owning it quickly.

### "Have you used pytest?"

- 레쥬메에 pytest가 없다. 실제로 써 봤다면 그대로 말한다. 안 써 봤다면:

> My automation was built on our own framework, but I know pytest's model well — fixtures with yield teardown for hardware setup, parametrize for test matrices, markers to separate HIL from SIL tests, and junitxml for CI.

### "How do you handle flaky tests?"

> First I measure it — rerun the same commit N times to get a failure rate. Then classify: is it the firmware (a real race), the test (a timing assumption), or the rig (power, USB, RF environment)? I quarantine it so it doesn't block merges, but track it with an owner, because a flaky test is often a real intermittent bug. Blind auto-retry hides exactly the bugs a drone can't afford.

### "What would your first 30/60/90 days look like?"

> First 30 days: learn the product and the current test setup — run the existing suites myself, sit with the firmware team, and list what's manual, what's flaky, and what's slow. By 60: automate the most painful manual test and get a HIL smoke test gating merges. By 90: a nightly regression with trustworthy results and a triage routine, so the team trusts a green build.

## 6. 역질문 (3개만 골라서)

1. "What does the HIL setup look like today — how many rigs, and what's still tested manually?"
2. "When a test fails, who triages it? Do test engineers dig into the firmware and push fixes, or hand off to the firmware team?"
3. "What does a release gate look like for firmware going to the field?"
4. "How do field issues — from Ukraine or from training units — come back into the test suite?"
5. "What would make someone in this role successful in the first six months?"
6. "Is RF link testing — range, attenuation, interference — part of this role's scope?"

- 2번이 가장 중요하다. **개발 기여 여지**를 확인하는 질문이다

## 7. 쓰지 말 표현

- "I'm not really a test engineer, but…" → 스스로 깎지 말 것
- "I just want to get in and move to firmware later" → 절대 금지. 내부 이동 가능성은 오퍼 단계에서 확인한다
- 과장: "I've built HIL for 5 years" (X) → "Test infrastructure on real hardware has been part of every role" (O)
- Apple 기밀: 제품명, 칩 이름, 일정

## 체크

- [ ] 60초 자기소개를 녹음해서 70초 안에 들어오는지 확인했다
- [ ] 스토리 A·B·C의 `(Don: …)` 칸을 실제 기억으로 채웠다
- [ ] "Why test role?"을 방어적으로 들리지 않게 3번 연습했다
- [ ] CI 갭 문장을 20초 안에 말할 수 있다
- [ ] 역질문 3개를 골라 메모해 뒀다
