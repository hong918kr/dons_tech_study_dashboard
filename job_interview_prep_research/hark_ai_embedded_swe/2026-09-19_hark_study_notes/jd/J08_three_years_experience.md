# J08. 3+ years of professional firmware or embedded systems development

> **분류**: Requirement 1/7 · **관련 개념 노트**: C00(로드맵), S06(§3 STAR, §4 행동 질문, §5 Why Hark / Why leave Apple)
> **Don 현재 상태**: ✅ 강함 — 레쥬메 기준 2021년부터 SSD 양산 펌웨어(Senior→Staff), 2025-12부터 Apple Embedded Systems, 그 앞 SW 경력 포함 약 8년. 요건 하한의 2배 이상
> **이 노트를 다 읽으면**: ① "3+ years"가 왜 하한선일 뿐이고 $120K–$300K 밴드 안에서 레벨이 어떻게 결정되는지 설명할 수 있다 ② 면접관이 "professional"로 인정하는 증거 세 종류(출하 경험·양산 디버깅·부서 간 소유권)를 내 경력에서 각각 한 문장으로 꺼낼 수 있다 ③ "컨슈머 배터리 기기를 출하해 본 적 없다"와 "Apple 9개월 이직"에 흔들리지 않고 답하고, 밴드 상단을 향한 협상 대화를 할 수 있다

---

## 0. 문장 뜯어보기

| 구(句) | 표면적 의미 | 채용담당자가 이 단어를 고른 이유 |
|---|---|---|
| `3+ years` | 최소 3년 | **하한선(floor)이지 목표치(target)가 아니다.** 신입·주니어를 서류에서 거르는 필터. 상한을 안 쓴 것은 시니어·스태프도 받겠다는 뜻이고, 실제로 밴드 상단 $300K가 그 자리를 비워 둔 것이다 |
| `professional` | 직업으로서 | 학교 프로젝트·부트캠프·취미 보드를 제외한다는 선언. "누군가 돈을 내고 쓰는 물건에 내 코드가 들어갔는가"를 묻는다 |
| `firmware` | 펌웨어 | 기기 안에서 도는, 사용자가 설치하지 않는 코드. 드라이버·부트로더·RTOS 태스크·제조 펌웨어 전부 포함 |
| `or` | 또는 | 타이틀이 "Firmware Engineer"가 아니어도 된다는 신호. Don의 Apple 타이틀(Embedded Systems / RF chipset integration)도 여기 걸린다 |
| `embedded systems development` | 임베디드 시스템 개발 | 펌웨어보다 넓게 — 보드 bring-up, 시스템 통합, 검증까지 포함. 스타트업이 "코드만 짜는 사람"이 아니라 "기기를 살려 내는 사람"을 찾을 때 쓰는 표현 |

**이 문장에 없는 것도 중요하다.** 학위 요건, 특정 칩·특정 RTOS 연차, "컨슈머 제품 경험 필수", "미국 시민권" — 전부 없다. 컨슈머 관련 요구는 Bonus Qualifications의 `Experience shipping consumer electronics through EVT/DVT/PVT milestones`로 내려가 있다. 즉 **컨슈머 기기 출하는 필수가 아니라 가산점**이다. 이 사실 하나가 뒤의 "배터리 기기 안 해 봤다" 질문에 대한 답의 뼈대가 된다.

**한 줄 요약**: 이 요건은 Don을 거르는 필터가 아니라, Don이 **어느 레벨로 들어갈지**를 정하는 눈금자다. 그래서 이 노트의 목표는 "3년 넘었다"를 증명하는 게 아니라 **"밴드 상단에 해당하는 사람"이라는 신호를 만드는 것**이다.

---

## 1. Hark에서 실제로 하게 될 일 (추정)

컨텍스트 파일 2.3·2.7절 기준으로, Hark는 2026-05에 Series A $700M을 받은 약 70명 규모 회사이고 1세대 기기는 EVT 전후로 추정된다. 이 단계의 회사에서 "3년 이상"이 실제로 요구하는 일의 모양은 이렇다 [추정].

```
  [대기업 펌웨어 조직]                    [Hark 같은 70명 스타트업]

  드라이버 팀 / RTOS 팀 / 전력 팀          펌웨어 엔지니어 3~6명
  테스트 조직 / 릴리스 조직 별도            한 사람이 드라이버+RTOS+전력+공장
  요구사항이 문서로 내려옴                  요구사항을 내가 HW/AI 팀과 정의
  내 변경은 리뷰 3단계 후 통합              내 커밋이 내일 아침 EVT 보드에 올라감
  실패하면 일정이 밀림                      실패하면 그 주 빌드가 멈춤
```

- **혼자 막히지 않는 능력**: 회로도, 벤더 SDK, 데이터시트, 로직 분석기만 주고 "센서 안 올라온다, 봐 달라"가 온다. 3년 미만이면 여기서 하루를 날리고, 5년 이상이면 두 시간 안에 "이건 FW가 아니라 전원 시퀀스"라고 결론을 낸다. 이 요건이 진짜로 사는 자리가 여기다.
- **범위 소유**: JD의 About the Role이 `You'll own critical pieces of the firmware stack`이라고 쓴 그대로, 기능 단위가 아니라 **서브시스템 단위**로 준다(예: 오디오 입력 경로 전체, 또는 OTA 전체).
- **부서 경계 작업**: HW 팀(새 실리콘·센서), agent 팀(모델 실행·메모리 제약), product 팀과 직접 협상한다. 연차를 요구하는 이유 중 절반이 기술이 아니라 **이 협상**이다.
- **양산 압박**: EVT → DVT → PVT → MP 일정이 실제로 돌아가면, "언제 고쳐지냐"에 숫자로 답해야 한다.

Don에게 이게 의미하는 바: JD가 요구하는 3년은 이미 통과했고, **회사가 실제로 필요로 하는 것은 Don이 가진 7~8년 쪽**이다. 면접에서 연차를 방어할 게 아니라, 연차가 만들어 낸 판단력을 증거로 보여 주는 쪽이 맞다.

---

## 2. 핵심 개념 — 검증 관점

### 2.1 밴드 $120,000 – $300,000이 말해 주는 것

JD에 명시된 base 밴드는 $120,000 – $300,000이다 [확인됨, JD 원문]. **상한이 하한의 2.5배**다. 이 폭은 한 레벨의 폭이 아니다. 실무에서 단일 레벨의 base 밴드는 보통 상하 25~40% 안에 들어간다. 2.5배는 **여러 레벨을 하나의 공고에 묶었다**는 신호로 읽는 게 합리적이다 [추정].

| 밴드 구간 (추정) | 대응 레벨 | 기대 범위 | Don 위치 |
|---|---|---|---|
| $120K–$155K | 주니어~미드 (3~5년) | 주어진 드라이버·기능을 구현, 리뷰 받음 | 해당 없음 |
| $155K–$200K | 미드 (5~7년) | 서브시스템 일부를 혼자 끝냄 | 하한선 |
| $200K–$250K | 시니어 (7년+) | 서브시스템 소유, 설계 결정, 남을 언랙 | **목표 하단** |
| $250K–$300K | 스태프/리드 급 | 여러 서브시스템 교차 설계, 팀 밖 협상 | **목표 상단** |

> 위 구간 나눔은 밴드 폭·JD 문구·샌프란시스코 베이 지역 관행에서 추론한 것이고 Hark가 공개한 레벨 표가 아니다 [추정]. 컨텍스트 2.6절의 비교 공고(On-Device AI Inference Engineer $200K–$450K, Tech Lead $300K–$500K)를 보면 이 Embedded SWE 공고의 상단 $300K가 회사 기준으로 **Tech Lead 밴드 하단과 맞닿는 지점**이라는 점은 확인된다 [확인됨, 출처 11].

여기서 나오는 실전 결론 셋.

1. **연봉 밴드가 넓은 공고일수록 레벨은 인터뷰 루프 안에서 정해진다.** 이력서의 연차가 아니라, 루프에서 나온 신호(설계 질문 깊이, 모호함 처리, 소유 범위 서술)가 레벨을 만든다.
2. **하단을 먼저 언급하면 하단이 앵커가 된다.** 리크루터 스크린에서 숫자를 먼저 말하지 않는다(3.4절).
3. **base만 적혀 있다.** JD는 `The total compensation package may also include additional components/benefits`라고만 쓴다 [확인됨, JD 원문]. equity는 별도 협상 축이고, $6B 비상장 밸류에서는 base보다 변동 폭이 크다.

### 2.2 레벨은 언제, 누가 정하나

```
 지원 → 리크루터 스크린            여기서 "years / scope / comp 기대"를 태그
      → 기술 스크린                여기서 "코딩 되는 사람"인지 판정 (J09)
      → 온사이트 루프              여기서 "설계·소유·협상 되는 사람"인지 판정  ← 레벨 결정 구간
      → 디브리프(HM + 면접관들)     "hire at level X" 합의
      → 리크루터가 밴드 안에서 오퍼 산출
```

컨텍스트 4.1절 기준 Hark 프로세스는 Figure 패턴을 참고한 추정이며 전체 2~3주로 빠르다 [추정]. **빠른 루프 = 라운드 수가 적다 = 라운드 하나의 신호 가중치가 크다.** 스크린 하나에서 "드라이버 구현자"로 읽히면 밴드 중간으로 굳는다.

레벨을 올리는 신호 vs 내리는 신호:

| 축 | 밴드 상단 신호 | 밴드 하단 신호 |
|---|---|---|
| 범위 | "그 서브시스템을 제가 설계하고 출하까지 소유했습니다" | "그 기능을 구현했습니다" |
| 모호함 | "요구사항이 없어서 HW팀과 기준을 먼저 정의했습니다" | "스펙대로 했습니다" |
| 실패 처리 | "왜 우리가 못 잡았는지 보고 테스트 체계를 바꿨습니다" | "고쳤습니다" |
| 타인 영향 | "이후 팀 전체가 그 test-node 흐름을 씁니다" | "제 부분은 끝냈습니다" |
| 트레이드오프 | "신뢰성과 전력 마진을 두고 기준을 이렇게 잡았습니다" | "안전하게 잡았습니다" |
| 숫자 | 전류·시간·수율·불량 수를 말함 | 형용사만 말함 |

### 2.3 "professional"로 인정되는 증거 세 종류

면접관은 연차 숫자를 믿지 않는다. 대신 **세 가지 증거**를 찾는다. 하나라도 비면 "경력은 있는데 출하는 안 해 본 사람"으로 분류된다.

| 증거 | 무엇을 보나 | 검증 질문의 형태 | Don의 재료 (레쥬메 근거) |
|---|---|---|---|
| **A. 출하한 제품(shipped product)** | 내 코드가 고객 손에 갔는가, 언제, 몇 개 | "그 제품 출하됐나요? 얼마나 나갔나요?" | 엔터프라이즈 SSD 양산 펌웨어(MS/DELL/HPE·Meta/Google 고객), Apple 출하 플랫폼에 새 무선 실리콘 통합 |
| **B. 양산·현장 디버깅(production debugging)** | 재현 안 되는 문제를 실제 하드웨어에서 좁혀 봤는가 | "가장 어려웠던 버그는? 어떻게 HW/FW를 갈랐나요?" | PCIe/I2C/SPMI/RFFE 인터페이스 장애 root cause, FPGA pre-silicon bring-up 부팅 실패 |
| **C. 부서 간 소유권(cross-functional ownership)** | 내 팀 밖 사람과 기준을 협상해 봤는가 | "HW 엔지니어와 의견이 갈렸을 때 어떻게 했나요?" | factory test-node 아키텍처 설계·리드, 신뢰성 vs 성능/전력 마진 sign-off |

**세 증거를 각각 한 문장으로 준비해 두면, 연차 질문 계열 절반이 자동으로 답해진다.**

### 2.4 연차 ≠ 레벨 — 세 축으로 생각한다

```
  scope(범위)      함수 → 모듈 → 서브시스템 → 제품 → 여러 제품/플랫폼
  ambiguity(모호함) 티켓 받음 → 문제 받음 → 영역 받음 → 방향만 받음
  blast radius     내 브랜치 → 팀 빌드 → 출하 이미지 → 양산 라인 → 필드 전체 기기
```

3년차는 보통 (모듈, 문제 받음, 팀 빌드)에 있다. Hark가 밴드 상단에 두고 싶은 사람은 (서브시스템~제품, 영역 받음, 양산 라인~필드)이다. Don의 factory test-node 설계와 MP sign-off 경험은 정확히 **blast radius가 양산 라인**인 사례다. 이건 5년차가 쉽게 못 만드는 증거이므로, 스토리에서 이 축을 반드시 드러낸다.

### 2.5 7~8년 경력자가 "3+ years" 공고에 지원할 때 생기는 두 가지 위험

| 위험 | 어떻게 드러나나 | 대응 |
|---|---|---|
| **다운 레벨링** | 루프가 기초 질문 위주로 흘러가고, 디브리프에서 "미드로 뽑자"가 된다 | 기초 질문에도 **설계 맥락을 얹어서** 답한다. "링버퍼는 이렇게 씁니다 + 이걸 오디오 DMA에 붙일 때 오버런 정책을 어떻게 정했는지" |
| **오버퀄 우려** | "이 역할 범위가 좁게 느껴지지 않겠나", "왜 시니어 타이틀을 버리나" | 범위가 아니라 **단계**를 이유로 든다. "대기업에서 쪼개져 있던 스택을 한 사람이 끝까지 소유하는 단계로 가고 싶다" |

여기에 Hark 고유 리스크 하나가 더 붙는다. 컨텍스트 2.6·2.8절 기준 Adcock 계열 회사는 고강도·고속 문화로 알려져 있다 [추정, 출처 8·9]. 경력이 많을수록 "이 사람이 우리 템포를 견디나"를 본다. 그래서 **속도에 대한 증거**(고객 기한 대응, bring-up 주간 사이클)를 하나 준비한다.

---

## 3. 실무 패턴과 함정

### 3.1 연차를 말하는 방식 — 숫자 하나가 아니라 구조로

Don의 이력은 단순 합산이 애매하다. 레쥬메 기준 2021년부터 SSD 펌웨어(SK hynix → Solidigm), 2025-12부터 Apple, 그리고 컨텍스트 3.1절이 말하는 "앞선 3년 SW 포함 약 8년"이다. 면접관이 "펌웨어 몇 년"을 좁게 세면 5년 언저리, 엔지니어링 전체로 세면 8년이다. **둘 다 3+를 크게 넘으므로 방어가 아니라 구성의 문제**다.

권장 표현 구조(숫자 → 영역 → 증거 순서):

```
"About eight years in embedded and systems software.
 The last five have been production firmware —
 enterprise SSD firmware at SK hynix and Solidigm,
 and now wireless chipset integration at Apple.
 In that time I've taken silicon from FPGA pre-silicon bring-up
 all the way to mass production twice."
```

- 총 연차를 먼저 말하고, 그중 펌웨어 비중을 스스로 밝힌다. 면접관이 나중에 계산해서 깎는 상황을 막는다.
- 마지막 문장이 핵심이다. 연차보다 **"끝까지 간 횟수"**가 더 강한 신호다.

> <확인 필요: 2021년 이전 약 3년의 직무가 정확히 어떤 소프트웨어였는지(회사·도메인). 임베디드/시스템 계열이면 위 문장 그대로, 웹·앱 계열이면 "eight years in software, five in firmware"로 바꿔야 한다. 컨텍스트 파일에는 "앞선 3년 SW"까지만 적혀 있다.>

### 3.2 "You haven't shipped a battery-powered consumer device" — 3단 대응

이 질문은 거의 확실히 나온다. Don의 제품은 엔터프라이즈 SSD(전원 무제한, 데이터센터)와 Apple 무선 실리콘 통합(컨슈머 제품이지만 역할이 통합·검증)이다. Hark 기기는 배터리·always-on·웨어러블급이다.

**나쁜 답의 형태**: ① 부인("배터리도 비슷합니다") ② 사과("그 부분은 약합니다…" 로 끝남) ③ 없는 경험 만들기.

**좋은 답 = 인정 → 전이 → 증명, 각 한 문장.**

| 단계 | 내용 | 문장 예 |
|---|---|---|
| 인정 | 정확히 무엇이 없는지 1초 안에 인정한다 | "I haven't owned the battery budget on a wearable." |
| 전이 | 같은 종류의 제약을 다룬 경험으로 옮긴다 | "But the SSD work was the same discipline with a different budget — fixed SRAM, fixed thermal envelope, and at Apple I signed off on reliability-versus-power margins with a power analyzer." |
| 증명 | 지금 배우고 있다는 구체적 증거를 댄다 | "Right now I'm running a Zephyr project on an nRF52840 DK measuring sleep current with a PPK2, so I can talk about DTIM and connection interval in real numbers rather than theory." |

전이 단계에서 쓸 수 있는 **실제 공통점**(지어내지 않은 것):
- 고정된 SRAM/메모리 예산 안에서 자료구조를 배치하는 습관 → 모델 tensor arena·오디오 버퍼 예산(C08 §7)과 같은 문제
- 성능 vs 신뢰성 vs 전력 마진의 sign-off 경험 → always-on 기기의 duty cycle 결정과 같은 종류의 판단
- 고속 인터페이스 margin 튜닝(shmoo, SI 협업) → 라디오 공존·전력 상태 전환 시의 마진 감각

> 증명 단계는 **실제로 한 뒤에만** 말한다. 컨텍스트 4.8절 체크리스트의 nRF52840 + Zephyr 미니 프로젝트가 아직이면 "앞으로 할 것"이라고 말하지 말고, 대신 대체 증거(전력 분석기 사용 경험)로 끝낸다.

### 3.3 Apple 9개월 이직 — 프레이밍

2025-12 입사, 2026-09 현재 약 9~10개월이다. 면접관이 이걸 묻는 이유는 단 하나: **"우리한테 와서도 9개월 뒤에 나가나?"** 충성심 테스트가 아니라 리스크 평가다.

| 유형 | 답의 뼈대 | 왜 이게 되나 / 안 되나 |
|---|---|---|
| ❌ 불만형 | "조직이 느리고 범위가 좁아서" | 같은 불만이 Hark에서도 나올 거라고 들린다 |
| ❌ 보상형 | "보상 때문에" | 다음 회사가 더 주면 또 나간다고 들린다 |
| ❌ 과잉설명형 | 3분짜리 조직도 설명 | 방어적으로 들리고, 진짜 이유가 따로 있다고 의심받는다 |
| ✅ 방향형 | "Apple에서 bring-up부터 MP까지 한 사이클을 끝까지 봤다. 다음 단계로 펌웨어 스택 전체를 소유하고 싶고, 그건 제품을 처음부터 만드는 팀에서만 가능하다" | 떠나는 이유가 아니라 **가는 이유**가 된다 |
| ✅ 타이밍형 보강 | "지금이 그 사이클의 자연스러운 경계다" | 무책임하게 중간에 나간다는 인상을 지운다 |

30초 영어 버전(컨텍스트 4.5절을 이 노트용으로 다듬음):

```
"I joined Apple last December and I've seen a full cycle there —
 taking a new radio chip through integration, root-causing host-interface
 failures, and standing up the factory test architecture before mass production.
 That's the work I wanted and I got it.
 What I want next is ownership of the whole firmware stack on a product
 that's being defined right now, with the model and the hardware team in the
 same room. At a company Apple's size that scope is split across several orgs.
 That's a career-direction move, not an exit."
```

**꼬리질문 대비**: "그럼 여기서 스코프가 좁아지면 또 나가나?" → "제가 원한 게 스코프 자체가 아니라 1세대 제품을 끝까지 보는 경험이다. 그 사이클은 최소 2~3년짜리다"라고 **기간**으로 답한다.

### 3.4 협상 — 메커니즘과 순서

밴드가 공개된 캘리포니아 포지션이라는 점을 활용한다.

- 캘리포니아 노동법 Labor Code §432.3은 ① 고용주가 **지원자의 급여 이력을 묻는 것을 금지**하고, ② 지원자가 요청하면 그 포지션의 **pay scale을 제공**하도록 규정한다 [확인됨·법령 개정에 따라 달라질 수 있음]. 즉 "현재 얼마 받으세요?"에 답할 **법적 의무가 없다**. 다만 자발적으로 밝히는 것은 허용된다.
- 그래서 리크루터 스크린의 표준 응답은 "현재 얼마"가 아니라 "이 역할의 레벨과 범위를 먼저 맞추고 싶다"이다.

협상 순서(권장):

1. **스크린 단계**: 숫자 회피 + 밴드 확인. "The posted range is 120 to 300. I'd expect to be in the upper part of that given my scope, but I'd rather align on level first."
2. **루프 중**: 레벨 신호를 쌓는다(2.2절 표). 협상 얘기는 하지 않는다.
3. **오퍼 직전**: 기준선을 정리해 둔다 — 현재 Apple TC(base + RSU 연환산 + 보너스)를 한 숫자로. <확인 필요: Don의 현재 Apple TC 실수치. 컨텍스트 4.8절에도 "보상 기준선 정리" 항목이 미완으로 남아 있다.>
4. **오퍼 후**: base와 equity를 **따로** 다룬다. base는 밴드 상단 대비 위치로, equity는 아래 질문들로.

**Equity에 대해 반드시 물어볼 것** (비상장 $6B 회사라 base보다 불확실성이 크다):

| 질문 | 왜 중요한가 |
|---|---|
| ISO/NSO/RSU 중 무엇인가 | 세금 구조가 완전히 다르다 |
| 주식 수와 현재 발행주식 총수(또는 지분 %) | "$X 상당"은 밸류 가정에 의존한다. 분모를 알아야 한다 |
| 행사가(strike price)와 최근 409A 평가일 | Series A 직후면 409A가 곧 갱신되어 오를 수 있다 |
| vesting 스케줄과 cliff | 1년 cliff 4년이 표준 |
| 퇴사 후 행사 기간(post-termination exercise window) | 90일이면 사실상 행사 불가한 경우가 많다 |
| refresh grant 정책 | 초기 grant만 보고 판단하면 3년 차에 역전된다 |
| 우선주 대비 보통주 구조, liquidation preference | $700M 라운드가 들어간 회사에서 exit 시 분배 순서를 바꾼다 |

> 위 항목들은 비상장 스타트업 주식 보상의 일반 구조다. Hark가 어떤 형태를 쓰는지는 공개되지 않았다 [추정].

**협상에서 쓸 지렛대(Don에게 실제 있는 것)**: ① 현재 Apple 재직 중이라 "지금 나가야 할 이유가 없다"는 위치 ② JD 우대사항 중 EVT/DVT/PVT 출하와 factory test를 이미 가진 드문 조합 ③ 컨슈머 무선 실리콘 통합 도메인이 Hark 1세대 기기(Cellular/Wi-Fi/BT/GNSS/NFC/UWB [확인됨, 출처 11])와 직결된다는 점.

**하지 말 것**: 다른 오퍼가 없는데 있다고 말하기, 밴드 상한 초과 요구를 먼저 던지기, base만 보고 equity를 무시하기, "9개월 만에 옮기는 사람"이라는 약점을 스스로 협상 테이블에 올리기.

### 3.5 흔한 함정 표

| 함정 | 어떻게 드러나나 | 대신 이렇게 |
|---|---|---|
| 연차 부풀리기 | "10년"이라 말했는데 이력서 연도와 안 맞음 | 이력서 연도와 말이 정확히 일치하게. 애매하면 "about eight years, five in firmware" |
| "우리(we)"로만 말하기 | 면접관이 개인 기여를 못 뽑아냄 | 팀 맥락은 한 문장, 나머지는 "I decided / I measured / I owned" |
| NDA 위반 | Apple/Solidigm의 미공개 제품·수치를 말함 | "구체적 제품은 말할 수 없지만 구조는 이렇다"로 추상화. **이 절제 자체가 시니어 신호다** |
| 형용사만 | "아주 복잡한 문제였다" | 숫자로: 보드 수, 재현율, 전류 mA, 테스트 시간 초, 수율 % |
| 스토리 길이 | 5분짜리 배경 설명 | STAR 90초, 꼬리질문으로 깊이 들어가게 남겨 둔다 |
| 기초 질문 얕게 넘김 | "그건 기본이죠" | 다운 레벨링 직행. 기초 질문일수록 정확하게 + 설계 맥락 한 줄 |
| 급여 먼저 말하기 | 스크린 첫 5분에 숫자 언급 | 레벨·범위 정렬을 먼저 요구 |
| 갭을 먼저 사과 | "RTOS는 실무로 안 써 봐서…" | 인정 → 전이 → 증명(3.2절)으로만 |

---

## 4. 리서치 — 근거 자료

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| Hark JD 원문 (Greenhouse) | 요건 7개, 우대 4개, base 밴드 $120K–$300K | Requirements 첫 줄, Compensation 문단 | https://job-boards.greenhouse.io/hark/jobs/4186968009 |
| Hark 전체 공고 (Greenhouse API) | 57개 공고의 밴드·요건 비교 → 레벨 구조 추론 | On-Device AI Inference($200K–$450K), Tech Lead($300K–$500K) 밴드와 비교 | https://boards-api.greenhouse.io/v1/boards/hark/jobs?content=true |
| California Labor Code §432.3 | 급여 이력 질문 금지, pay scale 제공 의무 | (b)항(이력 질문), (c)항(pay scale 요청) | https://leginfo.legislature.ca.gov/faces/codes_displaySection.xhtml?lawCode=LAB&sectionNum=432.3 |
| California DIR — Equal Pay Act FAQ | 위 조항의 실무 해석 Q&A | "Pay Scale" / "Salary History" 항목 | https://www.dir.ca.gov/dlse/California_Equal_Pay_Act.htm |
| Levels.fyi | 회사·레벨별 실제 보상 데이터, 레벨 매핑 | Bay Area Embedded/Firmware Engineer, 시니어 레벨 중앙값 | https://www.levels.fyi/ |
| Holloway — Guide to Equity Compensation | 비상장 주식 보상 구조 전반 | 409A, strike price, exercise window, liquidation preference 장 | https://www.holloway.com/g/equity-compensation |
| progression.fyi | 공개된 엔지니어링 커리어 래더 모음 | "Engineering" 카테고리에서 스타트업 래더 2~3개 비교 | https://progression.fyi/ |
| Dropbox Engineering Career Framework | 레벨별 scope·ambiguity·impact 정의의 대표 공개 사례 | IC3~IC5 구간의 Scope / Ambiguity 항목 | https://dropbox.github.io/dbx-career-framework/ |
| jorgef/engineeringladders | 레이더 차트 형태의 레벨 정의 오픈소스 | Senior / Tech Lead 축 정의 | https://github.com/jorgef/engineeringladders |
| Glassdoor — Figure AI 인터뷰 | 같은 창업자·같은 캠퍼스 회사의 루프 패턴·템포 | 인터뷰 길이, case study 언급 후기 | https://www.glassdoor.com/Interview/Figure-AI-Interview-Questions-E9642582.htm |
| TechCrunch — Hark Series A | $700M / $6B, 인원 약 70명, 모델→하드웨어 순서 | 규모·단계 확인 | https://techcrunch.com/2026/05/21/hark-raises-700m-series-a-for-its-secretive-universal-ai-interface/ |
| Intel Capital — Hark 투자 발표 | 수직 통합 전략, Adcock 인용 | "why we invested" 문단 | https://www.intelcapital.com/hark-raises-700m-series-a-at-a-6b-valuation/ |

**버전·시점 의존 표시**
- 밴드 $120K–$300K는 2026-08-26 갱신된 JD 기준이다. 공고가 갱신되면 바뀔 수 있다.
- California Labor Code §432.3은 SB 1162(2023 시행) 이후 형태다. 법 개정 시 달라진다.
- levels.fyi·Glassdoor 수치는 자기보고 데이터이고 시점마다 변한다. **협상에서 인용하지 말고 기준선 감각으로만 쓴다.**
- Hark의 실제 레벨 체계·equity 형태는 공개 자료가 없다. 2.1절 구간 표는 추론이다.

---

## 5. 예상 면접 질문

난이도 표기: **[기초]** 리크루터·HM 스크린에서 나오는 것, **[중급]** 기술 면접관이 경력 검증용으로 던지는 것, **[심화]** 온사이트 후반·HM이 레벨과 보상을 가늠할 때 나오는 것.

### Q01. [기초] Tell me about your background — how many years have you been doing embedded work?

**왜 묻나**: 요건 첫 줄의 필터 통과 확인. 동시에 **스스로 연차를 어떻게 구성해서 말하는가**로 레벨 감각을 본다.

**30초 답변**: 총 약 8년, 그중 펌웨어가 5년. SK hynix·Solidigm에서 엔터프라이즈 SSD 양산 펌웨어, 2025년 12월부터 Apple 무선 칩셋 통합. 실리콘을 FPGA pre-silicon부터 양산까지 두 번 끝까지 가져갔다는 점으로 마무리한다.

**English answer**:
"About eight years in embedded and systems software, five of those in production firmware. At SK hynix and Solidigm I wrote C and C++ firmware for enterprise SSDs — bringing up ARM Cortex-R and Cortex-M cores on FPGA before tape-out, integrating I2C, SPI and DMA, and shipping telemetry and error-handling features to customers like Microsoft and Meta. Since last December I've been at Apple in wireless chipset integration, root-causing interface failures when new radio silicon meets the full system. The short version is that I've taken silicon from first power-on to mass production, twice."

**꼬리질문**
- "펌웨어 5년과 그 앞 3년은 뭐가 달랐나?" → 앞 3년은 소프트웨어 쪽, 펌웨어로 넘어온 이유는 하드웨어 경계에서 일하고 싶어서. <확인 필요: 앞 3년 직무 내용>
- "두 번 끝까지 갔다는 게 무슨 뜻인가?" → SSD 제품 하나(pre-silicon → 양산 FW), Apple 무선 실리콘 하나(bring-up → NPI → MP).
- "가장 최근에 출하한 게 언제인가?" → Apple에서 진행 중인 프로그램 사이클로 답하되 미공개 정보는 피한다.

### Q02. [기초] What does "professional firmware" mean in your case — what actually shipped?

**왜 묻나**: 2.3절의 증거 A. 취미·학교 프로젝트와 구분하려는 것.

**30초 답변**: 엔터프라이즈 SSD의 양산 펌웨어. 데이터센터 고객이 실제로 쓰는 드라이브에 들어갔고, NVMe telemetry·error handling 같은 고객 요구 기능을 기한 안에 출하했다. Apple에서는 출하 플랫폼에 새 무선 실리콘을 올리는 쪽이라 성격이 다르지만 마찬가지로 MP까지 간다.

**English answer**:
"Enterprise SSD firmware that ships in data centers — SK hynix and Solidigm drives used by hyperscale customers. I owned features that customers asked for by name, like NVMe telemetry for field debug and the error reporting path, and delivered them against customer release dates. At Apple it's a different shape: I'm integrating new wireless silicon into platforms that are already shipping, so my output is bring-up, root cause, and the factory test architecture rather than an application feature. Both are production work — if I'm wrong, a line stops or a drive fails in the field."

**꼬리질문**
- "몇 대에 들어갔나?" → 정확한 수치는 NDA. "데이터센터 규모"로 추상화.
- "출하 후 회귀가 있었나?" → 에러 처리·telemetry가 있었기에 현장 이슈를 분류할 수 있었다는 방향으로.

### Q03. [중급] Tell me about the hardest bug you've found in a production or pre-production system.

**왜 묻나**: 2.3절의 증거 B. 연차가 실제 디버깅 능력으로 바뀌었는지.

**30초 답변**: Apple에서 새 무선 칩이 호스트에 붙을 때 생긴 인터페이스 장애 하나를 STAR로. 증상 → 가설 → 측정(DSO/프로토콜 분석기) → HW/FW 분리 → 원인 → 수정 → 재발 방지(test-node에 시나리오 추가)까지.

**English answer**:
"The class of bug I deal with now is a new radio chip failing intermittently on its host interface — PCIe, I2C, SPMI or RFFE — where nobody knows yet whether it's silicon, board, or firmware. My approach is to make the failure observable before I make it go away: capture the bus with a scope and a protocol analyzer at the moment of failure, then split the space — does it follow the chip, the board, or the software image? Once I can say 'this transaction violates timing under this condition,' the fix is usually small. The part I care about is the last step: I add that condition to the factory test so we catch it on the line instead of in the field."

**꼬리질문**
- "HW 문제와 FW 문제를 어떻게 갈랐나?" → 스왑 실험(칩/보드/이미지 교차), 파형 증거.
- "재현율이 얼마였나?" → 구체 수치로 답할 것. <확인 필요: 실제 재현율·보드 수>
- "왜 먼저 못 잡았나?" → 커버리지 공백을 인정하고 그걸로 테스트를 바꿨다는 결론.

### Q04. [중급] What's the largest thing you've owned end to end?

**왜 묻나**: 2.4절의 scope 축 측정. 레벨 결정에 직결.

**30초 답변**: Apple의 factory test-node 아키텍처. 개별 테스트가 아니라 **양산 라인에서 무엇을 어떤 순서로, 얼마 동안 검사할지**를 설계하고 리드했다. blast radius가 라인 전체.

**English answer**:
"The factory test architecture for a new silicon integration. That wasn't a single test — it was deciding what the line checks, in what order, within a fixed seconds-per-unit budget, and what the pass criteria are. I designed it, led it, and worked with hardware, manufacturing and reliability to agree on margins. The reason I point at that one is the blast radius: if my coverage is wrong, defective units leave the factory; if my test time is wrong, the line's throughput drops. That forced me to argue about trade-offs with people outside my own team, which is the part I want more of."

**꼬리질문**
- "테스트 시간과 커버리지는 어떻게 맞췄나?" → 결함 모드별 기대 검출률 vs 초 단위 예산.
- "그게 양산에서 뭘 잡았나?" → latent defect 사례 1건.

### Q05. [중급] You've worked on SSDs and radio integration. You haven't shipped a battery-powered consumer device. Why should we hire you for one?

**왜 묻나**: 도메인 전이 가능성 판단. 방어적으로 답하는지, 구조적으로 답하는지를 본다.

**30초 답변**: 3.2절의 인정 → 전이 → 증명. 없는 것은 배터리 예산 소유권이고, 있는 것은 같은 종류의 제약(고정 메모리·열·마진 sign-off)과 무선 실리콘 통합, 그리고 EVT/DVT/PVT 사이클이다.

**English answer**:
"Straight answer: I haven't owned a battery budget on a wearable. What I have owned is the same discipline with a different constraint — fixed SRAM and a fixed thermal envelope on SSD controllers, and at Apple, signing off reliability against power and performance margins with a power analyzer. The two things I think transfer hardest are multi-radio integration, which your first device needs given it carries cellular, Wi-Fi, BT, GNSS, NFC and UWB, and the EVT-to-mass-production cycle including factory test. The gap I'd be closing is low-power firmware design itself — sleep states, duty cycling, average-current budgeting — and I'm actively working on that now."

**꼬리질문**
- "지금 뭘 하고 있나?" → 실제로 한 것만. nRF52840 + Zephyr + PPK2 미니 프로젝트를 했으면 그것, 아니면 전력 분석기 경험으로 대체.
- "전력 문제를 마지막으로 디버깅한 게 언제인가?" → 마진 sign-off 맥락에서.
- "배터리 수명 계산을 해 보라" → C05·S03으로 준비. 이 노트 범위 밖.

### Q06. [중급] How do you work with hardware engineers when you disagree?

**왜 묻나**: 2.3절의 증거 C. 3년차와 7년차를 가르는 지점.

**30초 답변**: 의견이 아니라 측정으로 옮긴다. "내 코드는 맞다"가 아니라 "이 조건에서 이 파형이 나온다, 이미지를 바꿔도 따라오지 않는다"까지 만들어서 대화한다. 그래도 갈리면 비용으로 결정(보드 수정 vs FW 우회의 일정·리스크).

**English answer**:
"I try to move the argument from opinions to evidence as fast as possible. When a failure could be hardware or firmware, I build the experiment that separates them — swap the image, swap the board, swap the part, and capture the bus at the moment it fails. Once there's a waveform on the table, the conversation stops being about whose domain is at fault. When it's genuinely ambiguous, I frame it as cost: what does a board spin cost us in schedule versus a firmware workaround that we have to carry forever. I've had to make that call under a mass-production deadline, and I'd rather make it explicitly with the hardware lead than quietly absorb it in firmware."

**꼬리질문**
- "FW 우회를 택한 사례가 있나?" → 있으면 그 결정의 조건과 남긴 부채를 같이 말한다.
- "그 우회를 나중에 걷어냈나?" → 솔직하게.

### Q07. [중급] This is a startup at EVT stage. What did you do the last time requirements didn't exist?

**왜 묻나**: ambiguity 축(2.4절). 대기업 출신에게 반드시 던지는 질문.

**30초 답변**: factory test-node 설계가 정확히 그 경우였다. 무엇을 검사할지 정의된 문서가 없어서, 결함 모드를 먼저 나열하고 각각에 대해 검출 방법과 시간 비용을 붙여 기준을 만들고 합의를 받았다.

**English answer**:
"The factory test work started with no spec — nobody hands you a document that says which failures the line must catch. So I started from failure modes: list what can actually go wrong in this integration, then for each one ask whether it's detectable at the node, what it costs in seconds, and what the pass threshold should be. Then I took that table to hardware and reliability and got agreement, because the expensive part isn't writing the test, it's choosing what not to test. I'd expect to do the same thing at Hark on day one — write down what the firmware must guarantee before anyone can argue about how."

**꼬리질문**
- "합의가 안 되면?" → 데이터로 좁히고, 그래도 안 되면 결정자를 명확히 하고 기록한다.

### Q08. [심화] The requirement says three years. You have about eight. Won't this role be too small for you?

**왜 묻나**: 오버퀄 우려 + 진짜 동기 확인. 동시에 **보상 기대치**를 가늠한다.

**30초 답변**: 범위가 좁아서 온 게 아니라 단계가 달라서 온다. 대기업에서 여러 조직에 쪼개져 있던 스택(드라이버·RTOS·전력·OTA·공장)을 한 사람이 끝까지 소유하는 건 오히려 더 큰 범위다.

**English answer**:
"I read that line as a floor, not a target. What makes this role big for me isn't the years, it's the surface: at a large company the driver layer, the RTOS, the power team, OTA and the factory flow are five different orgs, and I've worked at the seams between them. Here one person carries a slice of all of it from bring-up to the field. That's a step up in scope, not down. Where I'd want to be honest is level — I'd want us to agree on what I'm expected to own, because I'm coming in expecting to own a subsystem end to end and to help set how firmware works here, not to pick up tickets."

**꼬리질문**
- "그럼 어느 레벨이라고 생각하나?" → Q09로.
- "매니지먼트를 원하나?" → 솔직하게. 지금은 IC 기술 소유 쪽.

### Q09. [심화] What are your compensation expectations?

**왜 묻나**: 밴드 안 위치를 잡기 위한 질문. 스크린 초반에 나오면 **앵커링 시도**로 본다.

**30초 답변**: 숫자를 먼저 던지지 않는다. 밴드를 인용하고 상단을 기대한다고만 말한 뒤, 레벨·범위 정렬을 먼저 하자고 되돌린다. 캘리포니아에서는 현재 급여를 물을 수 없다는 점도 알아 둔다(3.4절).

**English answer**:
"The posting lists 120 to 300 base, which is a wide range, so I assume it covers several levels. Given that I'd be coming in to own a subsystem end to end and I've shipped through EVT to mass production, I'd expect to be in the upper part of that band, with equity as a separate conversation. But I'd rather align on level and scope first — once we agree what I'm owning, the number usually follows. Can you tell me how you map candidates onto that range?"

**꼬리질문**
- "현재 얼마 받나?" → 답할 의무 없음. "I'd rather anchor on the value of this role than on my current package." (캘리포니아 §432.3(b) 기준으로 질문 자체가 허용되지 않는다 [확인됨·개정 의존])
- "구체적인 숫자를 달라" → 기준선을 정한 뒤 총보상 관점의 범위를 제시. <확인 필요: Don의 현재 TC 실수치가 정리되어야 이 답이 가능>
- "equity를 base로 바꿀 수 있나?" → 비율을 묻고, 판단 전에 3.4절 equity 질문들을 먼저 받는다.

### Q10. [심화] What level do you see yourself at here, and what would you expect to own in the first 90 days?

**왜 묻나**: 레벨 자기 인식 + 입사 후 계획. 밴드 상단을 주려면 면접관이 **90일 그림**을 들어야 한다.

**30초 답변**: 시니어~스태프 범위. 90일은 30/60/90으로: 보드를 혼자 살릴 수 있게 되기 → 한 서브시스템 소유 → 그 서브시스템의 공장·필드 경로까지 책임.

**English answer**:
"Senior, with the scope of a staff engineer on the areas I already know — bring-up, host interfaces, factory and manufacturing flows. For the first ninety days: in the first month I'd want to be able to bring up a board alone, which means learning your schematics, your toolchain and your build and flash flow well enough that I'm not asking anyone. By sixty days I'd want to own one subsystem outright — audio input path or OTA, whichever is hurting most. By ninety I'd want that subsystem to have a story all the way to the factory and the field: how it's tested on the line and how we debug it after it ships, because that's the part startups usually postpone and then pay for."

**꼬리질문**
- "왜 OTA나 오디오인가?" → 전자는 SSD FW 업데이트·검증 흐름과 구조가 같고, 후자는 DMA·링버퍼 경험이 직결된다.
- "지금 우리 팀에서 제일 아픈 게 뭐일 것 같나?" → 역질문으로 전환(컨텍스트 4.7절).

### Q11. [심화] You joined Apple in December. Why are you leaving after nine months?

**왜 묻나**: 잔류 리스크 평가. 답의 **감정 온도**를 본다.

**30초 답변**: 3.3절 방향형. 한 사이클을 끝까지 봤고, 다음은 펌웨어 스택 전체를 소유하는 단계. 불만이 아니라 방향.

**English answer**: 3.3절의 영어 문단을 그대로 쓴다.

**꼬리질문**
- "Apple이 붙잡으면?" → "카운터오퍼로 해결될 문제가 아니다. 내가 원하는 범위는 조직 구조의 문제라서."
- "여기서도 9개월 뒤에 나가나?" → 기간으로 답한다. "1세대 제품을 정의부터 필드까지 보는 데는 최소 2~3년이 걸린다. 그게 내가 사려는 것이다."
- "레퍼런스로 Apple 매니저를 쓸 수 있나?" → 현 직장 레퍼런스는 오퍼 단계 전까지 제공 불가라고 정중히.

### Q12. [심화] This team moves fast and the hours can be long. Is that a fit?

**왜 묻나**: 컨텍스트 2.6·2.8절의 고강도 문화. 경력이 많을수록 더 묻는다.

**30초 답변**: 고객 기한에 맞춰 출하해 본 경험으로 답하되, 지속 가능성에 대한 내 방식(무엇을 자동화해서 시간을 되찾았는지)을 붙인다. 무조건 "다 됩니다"는 오히려 신뢰를 잃는다.

**English answer**:
"I've shipped against hard customer dates — hyperscale SSD releases don't move — and bring-up weeks are bring-up weeks; when the board is on the bench, you stay with it. So the intensity itself isn't new. What I'd add is that I try to buy the time back structurally: the factory test work I did exists partly because catching defects on the line is cheaper than catching them at 2am in a lab. I'm comfortable with a hard push toward a milestone. What I'd want to understand is whether the push is toward something, or whether it's the steady state."

**꼬리질문**
- "우리 템포가 steady state라면?" → 솔직하게 기대치를 맞춘다. 거짓말하면 6개월 뒤에 문제가 된다.

### Q13. [심화] Tell me about a technical decision you got wrong.

**왜 묻나**: 연차가 판단력으로 바뀌었는지, 그리고 심리적 안전성. 시니어는 실패를 구조 문제로 환원할 줄 안다.

**30초 답변**: 실제 사례 하나를 골라 ① 무엇을 가정했는지 ② 왜 그 가정이 틀렸는지 ③ 언제 알아챘는지 ④ 그 뒤 프로세스를 어떻게 바꿨는지. 네 번째가 핵심.

**English answer** (뼈대만 — 실제 사례로 채울 것):
"The pattern I've been wrong about most is assuming a failure is firmware because firmware is the thing I can change. Early on I'd spend a day instrumenting code for something that turned out to be a board or a part variation. What changed my process is that I now spend the first hour making the failure observable on the bus rather than in the log, because that's the measurement that can exonerate firmware as well as convict it. It costs an hour up front and it has saved me days."

> <확인 필요: 여기에 쓸 Don의 구체 실패 사례 1건. 컨텍스트 4.4절 STAR 표에 "실패한 프로젝트와 배운 점"이 미작성으로 남아 있다. S06 §3에 STAR 정리가 있으니 거기에 하나 추가할 것.>

**꼬리질문**
- "그때 팀에 어떻게 알렸나?" → 빨리, 그리고 대안과 함께.

### Q14. [기초] Why Hark?

**왜 묻나**: 연차 많은 지원자일수록 "아무 데나 가는 사람"인지 본다.

**30초 답변**: 모델·하드웨어·펌웨어를 한 팀에서 같이 설계하는 곳이 드물다. 내 경력이 "새 실리콘을 제품으로 만드는 일"이었고 그걸 on-device AI에 쓰고 싶다.

**English answer**:
"Two reasons. First, the work: my whole career has been taking new silicon and making it ship, and a first-generation device is that problem at its purest — nothing exists yet, the schematics change under you, and the firmware is what makes the hardware real. Second, the direction: I want to be where models meet power and memory budgets. Most places either build the model or build the device. You're doing both in one building, which means the memory budget conversation is a conversation with a person down the hall instead of a spec I inherit."

**꼬리질문**
- "우리 제품이 뭔지도 모르지 않나?" → 공개된 것(Handoff, family of AI devices, 멀티 라디오 웨어러블급 채용 신호)까지만 근거로 들고, 나머지는 역질문으로.

### Q15. [중급] What questions do you have for us?

**왜 묻나**: 시니어 판별의 마지막 관문. 질문의 수준이 레벨 신호다.

**30초 답변**: 컨텍스트 4.7절 다섯 개를 쓰되, 이 노트 주제와 연결되는 두 개를 우선한다 — ① 이 역할이 SoC 쪽인지 always-on MCU 쪽인지 ② 밴드가 여러 레벨을 덮는지, 레벨은 어떻게 결정되는지.

**English answer**:
"Three things. One: is this role primarily on the always-on MCU side or the SoC BSP side — and is that split already decided? Two: where is the hardware right now, proto or EVT, and what's the firmware team's biggest risk to first production? Three, and this one's practical: the posted range covers a wide band, so how do you map a candidate onto it, and what would you need to see from me to place me at the top of it?"

**꼬리질문**: 세 번째 질문은 협상 대화를 여는 문이다. 답을 받으면 그 기준에 맞춰 루프 후반 답변을 조정한다.

---

## 6. Don 매핑

### 6.1 요건 대비 위치

| 항목 | 상태 | 근거 (컨텍스트 3.1절) |
|---|---|---|
| 3+ years professional | ✅ 크게 상회 | 2021~ SSD FW(Senior→Staff), 2025-12~ Apple Embedded Systems, 앞선 SW 포함 약 8년 |
| 출하 증거 A | ✅ | 엔터프라이즈 SSD 양산 FW, 고객 MS/DELL/HPE·Meta/Google |
| 양산 디버깅 증거 B | ✅ | "root-cause analysis of fundamental and interface level failures … when a new chip meets the full HW/SW system" |
| 부서 간 소유권 증거 C | ✅ | "designing and leading factory test-node architecture", 신뢰성 vs 성능/전력 마진 sign-off |
| 컨슈머 배터리 기기 | 🟡 갭 (우대 사항) | 엔터프라이즈 SSD + 컨슈머 무선 실리콘 통합. 배터리 예산 소유 경험은 없음 |
| 재직 안정성 서사 | 🟡 리스크 | Apple 2025-12 입사 → 9~10개월. 방향형 답변 필요 |

### 6.2 스토리 3종 — 연차 질문 계열에 전부 쓰인다

| 스토리 | 어떤 질문에 쓰나 | 반드시 들어갈 요소 |
|---|---|---|
| **인터페이스 root cause** (Apple, PCIe/I2C/SPMI/RFFE) | Q03, Q06, Q13 | 측정 도구(DSO·프로토콜 분석기), HW/FW 분리 실험, 재발 방지로 마무리 |
| **factory test-node 아키텍처** (Apple) | Q04, Q07, Q10 | 스펙이 없던 상태에서 기준을 만든 과정, 시간 vs 커버리지 트레이드오프, 양산에서 잡은 결과 |
| **FPGA pre-silicon bring-up** (Solidigm, Cortex-R8/R82/M0+) | Q01, Q05, Q12 | 아무것도 안 켜질 때의 순서(클럭·리셋·전원 → 부트 경로), 실리콘으로 넘어간 뒤의 차이 |

세 스토리 모두 S06 §3에 STAR 형태로 정리되어 있다. 이 노트에서는 **연차·레벨 관점의 포장만** 다룬다: 각 스토리 끝에 "그래서 이 결정의 blast radius는 ___였다"를 한 문장 붙이면 시니어 신호가 된다.

### 6.3 프레이밍 규칙

- **연차를 방어하지 않는다.** Don은 요건의 2배 이상이다. 이 요건 앞에서 할 일은 증명이 아니라 **레벨 앵커링**이다.
- **엔터프라이즈 → 컨슈머 전이를 "다른 제약, 같은 규율"로 표현한다.** 배터리 대신 열·SRAM·마진이라는 고정 예산을 다뤘다는 사실은 레쥬메에 근거가 있다.
- **Apple 경험을 컨슈머 경험으로 셈한다.** Apple 무선 칩셋 통합은 컨슈머 제품이다. "컨슈머 안 해 봤다"는 정확한 진술이 아니다. 정확한 갭은 "배터리 수명 예산을 직접 소유해 본 적 없다"이다. **갭을 정확히 좁게 말하는 것 자체가 시니어 신호다.**
- **NDA 선을 지킨다.** Apple·Solidigm의 미공개 제품·수치는 구조로 추상화한다.
- **없는 경험은 만들지 않는다.** RTOS·BLE 스택·OTA 실무는 J11·J12·J04에서 별도로 다룬다. 이 노트의 답변들에서 그 경험이 있는 것처럼 들리게 쓰지 않는다.

### 6.4 미확인 항목

- <확인 필요: 2021년 이전 약 3년의 직무 상세(회사·도메인). "8년"의 구성 설명이 여기에 달려 있다.>
- <확인 필요: 현재 Apple 총보상(base + 주식 연환산 + 보너스). Q09의 기준선이 없으면 협상이 불가능하다.>
- <확인 필요: Q13에 쓸 구체적 실패 사례 1건.>
- <확인 필요: Q03의 재현율·보드 수 등 숫자. 형용사 대신 숫자를 쓰려면 필요하다.>
- <확인 필요: SSD 펌웨어에서 이미지 업데이트/서명 검증 흐름을 다뤘는지. 있으면 우대사항(secure boot/firmware signing)과 연차 증거를 동시에 강화한다. 컨텍스트 3.5절에도 같은 확인 항목이 있다.>

---

## 7. 준비 체크리스트

- [ ] 연차 한 문장 확정 — "about eight years, five in production firmware" 형태로, 이력서 연도와 숫자가 정확히 맞는지 검증
- [ ] 2021년 이전 3년의 직무를 한 줄로 정리 (Q01 꼬리질문 대비)
- [ ] 증거 A/B/C 각각 한 문장으로 준비 (출하 제품 / 양산 디버깅 / 부서 간 소유권)
- [ ] STAR 3종(인터페이스 root cause, factory test-node, FPGA bring-up)에 **blast radius 한 문장**과 **숫자 한 개**씩 추가
- [ ] "배터리 컨슈머 기기 안 해 봤다" 3단 답변(인정 → 전이 → 증명) 영어로 소리 내어 연습, 45초 안에
- [ ] Apple 9개월 이직 답변 영어 45초 + 꼬리질문 2개("붙잡으면?", "여기서도 9개월?") 대비
- [ ] 현재 총보상 계산 → 협상 기준선 1개 숫자로 확정
- [ ] Equity 질문 7개(3.4절)를 메모 카드 한 장으로 만들어 오퍼 콜에 지참
- [ ] California Labor Code §432.3 요지 확인 — 급여 이력 질문에 답할 의무가 없음을 알고 들어가기
- [ ] 30/60/90 계획 한 장 (Q10) — 첫 달 보드 bring-up 자립, 둘째 달 서브시스템 소유, 셋째 달 공장·필드 경로까지
- [ ] 역질문 중 "레벨을 어떻게 매핑하나" 문장 암기 (Q15)
- [ ] 실패 사례 1건을 S06 §3에 STAR로 추가

---

## 8. 더 읽기

| 어디 | 무엇 |
|---|---|
| `S06 §3. Don의 STAR 스토리` | 이 노트의 스토리 3종이 STAR 형식으로 정리되어 있다. 여기서 문장을 가져다 쓴다 |
| `S06 §4. 행동 질문 ↔ 스토리 매핑` | Q06·Q07·Q12·Q13의 행동 질문 계열 대응표 |
| `S06 §5. Why Hark / Why leave Apple` | Q11·Q14의 원본. 이 노트는 거기에 협상·레벨 관점을 더한 것 |
| `S06 §6. 영어 표현 모음` | 위 English answer들을 자기 말로 바꿀 때 쓸 표현 |
| `C00 §4. 2주 학습 계획` | 갭(RTOS·BLE·저전력)을 언제 메울지. Q05의 "증명" 단계 재료가 여기서 나온다 |
| `../hark_ai_embedded_swe_context.md §1` | 레벨·밴드 원자료 |
| `../hark_ai_embedded_swe_context.md §3.3` | 갭별 보완 전략 — Q05의 전이 문장 근거 |
| `../hark_ai_embedded_swe_context.md §4.5` | 행동·동기 질문 원본 |
| `../hark_ai_embedded_swe_context.md §4.7` | 역질문 5개 (Q15) |
| `jd/J09` | 이 요건 바로 다음 줄. "3년"이 진짜인지 검증하는 수단이 J09의 C/C++ 질문들이다 |
| `jd/J13`, `jd/J14` | 증거 B(양산 디버깅)를 기술적으로 증명하는 요건들 |
