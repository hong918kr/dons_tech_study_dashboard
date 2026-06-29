# Stanford CS230 | Autumn 2025 | Lecture 9: Career Advice in AI

> Stanford · 2025-12-17 · 01:45:08 · [영상 링크](https://www.youtube.com/watch?v=AuZoDsNmG_s) · 강사: Andrew Ng, Laurence Moroney, Kian Katanforoosh

## 📌 한 줄 요약

AI 커리어 조언을 다룬 강의로, 전반부는 Andrew Ng이 "지금이 AI로 무언가를 만들기 가장 좋은 시대"라는 메시지와 함께 product management(PM) 병목, 함께 일하는 사람의 중요성을 이야기하고, 후반부는 게스트 연사 Laurence Moroney가 변화한 채용 시장에서 살아남는 전략(business focus, technical debt, hype 걸러내기, small AI로의 분화)을 실제 사례 중심으로 풀어낸다. 핵심은 "fundamentals + AI tool + 비즈니스 감각 + 사람"을 갖추면 어려운 시장에서도 thrive할 수 있다는 것.

## 🎯 핵심 메시지

> 강력한 소프트웨어를 그 어느 때보다 빠르게 만들 수 있는 황금기다. 책임감 있게, 일단 많이 만들어라(go and build stuff). 그리고 함께 일할 사람을 가장 중요한 선택 기준으로 삼아라.

**보조 메시지**
- AI 코딩으로 빌드가 싸지고 빨라지면서 병목이 "무엇을 만들지 결정하는 일(PM)"로 이동했다 → 코드도 짜고 사용자와 대화도 하는 엔지니어가 가장 빠르게 움직인다.
- 채용 시장은 어렵지만(주니어 채용 둔화, 대량 해고) 전략적으로 접근하면 기회는 여전히 막대하다.
- 더 이상 "AI를 할 줄 안다"만으로는 안 된다. **이해의 깊이 + 비즈니스 포커스 + 실행(delivery)** 세 기둥을 보여줘야 한다.
- hype를 걸러 signal을 뽑아내고, "왜(why)"를 먼저 묻는 trusted advisor가 되라.
- fundamentals에 집중하고 스킬을 다양화하라(one-trick pony가 되지 말 것).

---

## 🚀 Part 1 — Andrew Ng: 지금이 AI를 만들기 가장 좋은 때

### AI는 느려지지 않았다

- "AI 진보가 둔화되는가?"라는 질문은 일부 benchmark가 100%에 가까워지면 더 오를 수 없기 때문에 생긴 착시이기도 하다.
- METR 연구: **AI가 수행 가능한 task의 길이(사람이 그 일을 하는 데 걸리는 시간 기준)가 약 7개월마다 2배**로 늘고 있다. AI 코딩은 doubling time이 더 짧아 약 70일.
- 두 가지 테마로 지금이 황금기:
  - **더 강력함(more powerful)**: LLM, RAG, agentic workflow, voice AI, deep learning 같은 AI building block으로 1년 전 누구도 못 만들던 소프트웨어를 만들 수 있다.
  - **더 빠름(faster)**: AI 코딩으로 작성 속도가 비약적으로 빨라졌다.

> 도구의 최전선에 머물러라. 반 세대만 뒤처져도 생산성이 눈에 띄게 떨어진다. AI 코딩 도구는 3~6개월마다 개인 최애 도구가 바뀔 만큼 빠르게 발전하는 섹터다. (Claude Code → Codex → Gemini 3 등)

### Product Management 병목

- 명확한 spec → 코드 변환이 쉬워질수록, 병목은 **무엇을 만들지 결정하기 / 명확한 spec 쓰기**로 옮겨간다.
- 소프트웨어 빌드 루프: 코드 작성 → 사용자에게 보여줌 → 피드백 → 무엇을 만들지 재정의 → 반복. 이 PM 작업이 이제 병목.

| 항목 | 전통 실리콘밸리 | 현재 트렌드 |
|---|---|---|
| Engineer:PM 비율 | 4:1, 7:1, 8:1 | 2:1, 심지어 1:1 |
| 이상적 형태 | 엔지니어와 PM 분리 | **엔지니어 + PM을 한 사람으로 합치기** |

> 코드도 짜고 사용자와 대화해 깊은 empathy를 쌓아 "무엇을 만들지" 결정하는 엔지니어 — 이들이 실리콘밸리에서 가장 빠르게 움직이는 사람들이다.

- (주의) Andrew는 과거 좋은 엔지니어들에게 PM을 강요해 상처를 준 실수를 후회한다고 고백. 강요가 아니라, 가능하다면 이 일을 좀 더 해보길 권하는 것.

### 함께 일하는 사람이 곧 커리어

> 학습 속도와 성공을 가장 강하게 예측하는 변수 중 하나는 **당신이 곁에 두는 사람들**이다. 우리는 모두 사회적 동물이라 주변 사람에게서 배운다.

- Stanford의 진짜 자산은 **connective tissue(연결 조직)** — frontier lab의 많은 사람이 Stanford 출신이라, 인터넷에 공개되지 않은 bleeding-edge 정보가 친구·관계망을 통해 흐른다. 전화 한두 통이 기술 아키텍처 선택을 바꾼다.
- **구직 시 경고 사례**: 한 학생이 hot한 브랜드의 회사에 합격했지만 어떤 팀에 배치될지 알려주지 않았고("일단 사인하면 매칭해줌"), 입사 후 backend Java 결제 시스템에 배정되어 1년 만에 퇴사. 몇 년 뒤 같은 회사에서 다른 학생에게도 유사한 일이 반복됨.
- 교훈: **데이데이 함께 일할 사람**을 확인하라. 팀을 안 알려주는 회사는 의심하라. 회사 로고가 덜 hot해도 똑똑하고 성실한 좋은 팀이면 더 빨리 배우고 성장한다.

### 책임감 있게, 그리고 열심히

- 빌드 비용·실패 비용이 크게 낮아졌다 → 주말 하나 날려도 뭔가 배우면 괜찮다. **허락을 기다리지 말고** 일단 만들어라(단, 남에게 해를 끼치지 말 것).
- 세상엔 아이디어가 그것을 만들 수 있는 사람보다 훨씬 많다. 당신이 안 만들면 아무도 안 만들 프로젝트가 많다.
- 열심히 일하라(work hard) — 다만 그럴 수 없는 처지의 사람(출산·부상·장애 등)은 존중하고 지원해야 한다. 일할 수 있는 위치라면 저녁·주말에 코딩하고 사용자 피드백을 받는 것이 성공 확률을 높인다.

---

## 💼 Part 2 — Laurence Moroney: 변화한 AI 채용 시장 생존법

### 도입 사례 — "backbone"의 함정

- 매우 뛰어난 코더가 4월에 해고(+여자친구 이별 + 반려견 사망), 300개 넘는 지원을 추적했지만 Meta·Microsoft·Amazon 등 인터뷰 루프 끝에서 매번 탈락.
- 원인: 채용 안내문의 "의견을 굽히지 말고 backbone을 가져라"를 잘못 해석해 인터뷰에서 **적대적/공격적(hostile)**으로 반응. 면접관 입장에선 10x 엔지니어라도 팀에 두고 싶지 않은 태도.
- teamwork를 중시하는 회사로 코칭 후 합격, 이전 연봉의 2배. 교훈: **회사도 당신을 고른다.** Stand your ground는 좋지만 jerk가 되지 마라.

### 세 가지 성공의 기둥

이제는 mindset을 "말하는" 것만으로는 부족하고 **보여줘야(show)** 한다. vibe coding으로 무언가를 실제로 만들어 보여주기 가장 좋은 시대.

| 기둥 | 의미 |
|---|---|
| **① 이해의 깊이 (Understanding in depth)** | (a) 학술적으로 ML·모델 아키텍처를 이해하고 논문을 읽고 적용 (b) 트렌드에 손을 얹고 signal/noise 비율에서 signal을 가려내는 것 |
| **② 비즈니스 포커스 (Business focus)** | 비즈니스가 무엇을 필요로 하는지 이해하고 그에 정렬된 output을 만든다 |
| **③ 실행 편향 (Bias towards delivery)** | "Ideas are cheap, execution is everything." 잘 grounded된 반쪽 아이디어가 화려하지만 근거 없는 아이디어를 이긴다 |

> **Hard work는 시간(996: 9-9-6)이 아니라 측정 가능한 output이다.** Laurence는 야구 시즌에 경기를 틀어놓고 그 시간에 책을 쓴다(약 2개월에 한 권). "you are what you measure."

> **"가진 직업이 아니라 원하는 직업을 위해 output을 내라."** Laurence는 Google PM 면접에 두 번 떨어졌지만, Java로 그들의 cloud에서 주가 예측 앱을 만들어 이력서에 코드를 올리자 인터뷰 전체가 그 코드 질문으로 채워져 엔지니어로 합격 → 면접의 주도권을 본인에게 가져옴.

### "지금 AI에서 일한다는 것" — 4가지 현실

과거: "어떤 걸 만들 수 있으면(이미지 분류기 등) 6자리 연봉." 지금: 모든 것이 **production**과 **delivery**로 기운다. "the bottom line is that the bottom line is the bottom line."

1. **Business focus는 협상 불가(non-negotiable)** — 지난 10년간 실리콘밸리는 직원의 "full self를 일터로 가져오기"와 activism을 과도하게 허용했고(예: Google Cloud 임원실 점거 사건), pendulum이 반대로 크게 흔들려 다시 비즈니스 중심으로 돌아왔다.
2. **Risk mitigation이 업무의 일부** — heuristic computing → intelligent computing으로의 비즈니스 전환에서 리스크를 식별하고 완화하는 mindset이 인터뷰에서 가장 강력한 스킬.
3. **Responsibility의 진화** — "모두를 위해 동작하게"라는 fluffy한 정의에서 "AI가 동작하고 → 비즈니스를 구동하고 → 모두를 위해 동작하게"로. 순서가 뒤집히면 재앙.
4. **실수로부터 학습 + 동료에게 grace 주기** — 사람도 AI도 실수한다.

> **Gemini 이미지 생성 사례**: 책임 있는 표현을 위해 prompt를 가로채 다양성을 주입했으나, "Caucasian/white" 요청은 거부(거짓말까지)하고, "Irish woman"은 전부 빨간 머리로 그리는 등 한 가지 좁은 시각이 모델·회사 평판을 훼손. (역사적 맥락의 다양성 주입 — 다양한 인종의 samurai 등 — 도 같은 문제.) 좋은 의도(good intent)가 잘못 적용된 사례.

### Vibe Coding과 Technical Debt

> 엔지니어는 generated code의 함의를 누구보다 잘 이해할 수 있다 → 더 숙련된 엔지니어일수록 vibe coding을 더 잘 쓴다. "trusted advisor"가 되라.

**Technical debt(기술 부채) 프레임워크** — 무언가를 만들 때마다 부채(버그, 지원, 문서, 신규 요구, 마케팅, 피드백)를 떠안는다. 부채를 피하는 유일한 방법은 아무것도 안 하는 것.

| | 비유 | 소프트웨어 |
|---|---|---|
| **좋은 부채** | 집 담보대출(30년 후 가치 ↑) | 명확한 목표를 충족하는, 가치를 내는 코드 |
| **나쁜 부채** | 고금리 신용카드 충동구매(신발 $200→$500) | 아무도 이해 못하는 코드, spaghetti code, 문제를 찾는 솔루션 |

**좋은(저금리) 기술 부채를 얻는 법**:
- **명확한 목표 + 충족 여부**: chat GPT부터 켜지 말 것. Laurence는 영화 제작 startup 앱을 만들며 빌드→테스트→버리기를 반복(generated code는 싸지만 *engineered* code는 싸지 않다). 목표 미충족도 학습이고, 버리는 데 부담 없다.
- **비즈니스 가치 전달되는가**: "멋진 웹사이트 만들었다 → so what?" VP가 처음 만든 사이트가 cool해도 비즈니스에 도움 안 되면 의미 없다.
- **사람의 이해(human understanding)**: 가장 과소평가되지만 중요. 나만 아는 코드를 남기고 떠나면 최악의 부채. 문서·명확한 알고리즘·변수명까지 신경 써라.

**현장에서 만나는 나쁜 부채**: 망치를 들면 모든 게 못으로 보임(solution looking for a problem), spaghetti code, 학습 데이터 편향(Swift UI Mac OS인데 iOS 코드를 줌), **authority over merit**(VP가 Replit 구독해서 만든 걸 결국 당신이 고쳐야 함).

### Hype 헤쳐나가기

> 소셜 미디어의 통화(currency)는 정확성이 아니라 **engagement**다. LinkedIn조차 engagement용 글로 넘쳐난다. signal을 noise에서 걸러내고, 주변 사람을 signal로 이끌 수 있다면 — 1:1 환경(면접, 직장)에서 엄청난 가치가 된다.

**Trusted advisor가 되는 법 — "왜(why)"를 먼저 물어라**

> **Agent 도입 사례**: 유럽 회사가 "agent를 구현해달라"고 요청 → 올바른 첫 질문은 "agent가 뭐냐"도 "무엇을 하고 싶냐"도 아닌 **"왜(why)?"**. 계속 peel apart 하니 본질은 "영업사원을 더 효율적으로 만들고 싶다"였다. 그 문장엔 AI도 agent도 없다. 영업사원은 시간의 80%를 리서치, 20%만 영업(커미션)에 쓰고 있었다 → 목표를 "20% 효율 향상"으로 잡고 agentic AI를 적용.

**Agentic AI의 4단계 패턴** (어떤 문제든 이 4단계로 쪼개면 agent가 된다):

1. **Understand intent (의도 이해)** — LLM이 가장 잘하는 건 "이해". 무엇을 할지, 어떻게 할지 의도를 파악.
2. **Planning (계획)** — agent에게 사용 가능한 도구(웹 검색·브라우징 등)를 선언하고, LLM이 실행 단계로 분해.
3. **Use tools → result (도구 사용으로 결과 도출)**.
4. **Reflect (반성)** — 결과를 intent와 비교. 충족 못하면 루프로 복귀.

- 결과: 영업사원 낭비 시간의 10~15% 절감 → 더 많은 판매·수입·만족(win-win-win). McKinsey 보고서: 기업 AI 프로젝트의 ~85%가 실패하는 주요 원인은 scoping 부재(hype 편승).
- 다른 hype 사례: "소프트웨어 엔지니어링은 죽었다", "Hollywood는 죽었다"(2년 전 "내년이면 prompt 하나로 90분 영상" 주장 — 아직도 불가능), "연말까지 AGI".

**Hype 항법 전략**:
- **능동적으로 필터링**(filter actively), fashionable distraction과 그것에 기대는 사람들을 무시.
- **fundamentals로 깊이 들어가기** — 어떤 것이든 "최대한 mundane(평범)하게" 만들어 설명하라. 예: text-to-video = "연속된 프레임을 조금씩 다르게 생성"으로 풀면 비전문가도 이해하고, 그 분야 전문가가 멋진 걸 만든다.
- **finger on the pulse 유지** — 가장 어려운 부분. Twitter/X/LinkedIn의 cesspool에 들어가서라도 사람들이 무엇에 노출되는지 이해해야 advisor가 될 수 있다(논문은 signal/noise 비율이 더 좋지만 landscape는 따로 파악해야).

### AI 버블과 분화(Bifurcation)

> Titanic 비유: crow's nest의 감시원들이 추위 얘기만 하고, 정작 쌍안경은 항구에 두고 왔다. 앞으로 나아가는 데 취해 리스크 감시를 소홀히 한 것 — 오늘날 AI 산업의 좋은 은유.

- **버블은 온다.** 하지만 dot-com 버블이 터졌어도 fundamentals를 이해한 Amazon·Google은 thrive했고, "build it and they will come"의 pets.com은 증발했다.
- **버블의 해부(피라미드)**: 꼭대기 hype → unrealistic valuations → me-too products → 바닥의 작은 real value kernel. 거대한 VC 투자는 이미 마르기 시작(스타트업 투자액 축소).

**향후 5년의 분화**:

| | Big AI | Small AI |
|---|---|---|
| 정의 | 남이 호스팅(GPT, Gemini, Claude), AGI 지향 "bigger is better" | self-hostable / open-weights 모델, 직접 호스팅 |
| 상태 | 버블이 먼저 올 가능성 | 현재 underserved, 버블은 나중 |
| 동향 | 계속 커짐 | YC 기업 80%가 (특히 중국) small model 사용. 7B 모델이 작년 50B 수준, 내년엔 작년 300B 수준 |
| 필요 스킬 | — | **fine-tuning**(downstream task용) |

- Small AI가 중요한 이유 = **privacy**. Hollywood 스튜디오는 IP 보호가 극단적이라(James Cameron Avatar 소송 등) GPT/Gemini에 시나리오를 못 넘긴다 → self-host small model을 fine-tune해 **분석**(왜 이 영화는 흥했나, 개봉 시기 등)에 활용. 법률·의료 사무소도 동일.
- 버블 영향을 피하려면: **fundamentals + real solution + 비즈니스 이해 + 스킬 다양화**. one-trick pony(특정 API/프레임워크만 잘하는 사람)는 산업이 옮겨가면 도태된다.

### Small AI = "AI everywhere all at once" (ARM에서의 일)

- 전통적 통념 "AI = CPU + GPU"가 깨지는 중. 모바일에서 **SME(Scalable Matrix Extensions)**로 AI 워크로드를 CPU에서 실행 → 별도 칩 불필요(전력·공간 절감).
- 선두: 중국 폰 벤더 Vivo·OPPO(SME 칩), Apple(A·M 시리즈 neural core)에 대거 투자.
- 예: Alipay가 사진 검색/슬라이드쇼를 **온디바이스**로 옮김 → ① privacy(제3자 공유 불필요) ② latency(업로드/다운로드 제거) ③ 백엔드 서비스 구축 비용 절감.
- small model이 똑똑해지고 저전력 기기가 이를 돌릴 수 있게 되는 **convergence** → embedded intelligence는 더 이상 sci-fi가 아니다.

### Artificial Understanding 데모

> AI의 숨은 본질은 "artificial understanding"이다. 모델이 당신을 대신해 이해하고, 그 이해로부터 새 것을 만들면 superpower가 생긴다.

- **실패 사례(non-agentic)**: 아들 아이스하키 사진에 직접 prompt로 "골 넣는 영상" → 빈 연습 경기장인데 관중을 그려넣고, 빗나간 슛에 환호, 스틱 2개, 이름도 틀림.
- **성공 사례(agentic)**: ① 장면 의도(여자가 벤치에 앉아 속상, 남자가 위로)와 전체 스토리·제약(8초, 명확한 대사)을 LLM에 전달해 intent 이해 → LLM이 더 묘사적인 prompt 생성 ② 사용할 도구(VO) 선언 → VO는 high-action엔 약하지만 느린 카메라 풀로 emotion 표현엔 강하다는 특성 반영 ③ 도구로 생성(VO는 4영상에 $2~3) → 토큰을 의도 이해·계획에 먼저 써서 2~3번 시도 안에 좋은 결과.

---

## 💬 Q&A에서 나온 통찰

- **Nvidia처럼 좁은 전문성을 원하는 회사 vs 스킬 다양화?** → 여전히 다양화가 낫다(한 바구니에 모든 달걀 X). 깊이는 좋지만 그것만 할 줄 알면 위험. 다양화는 LLM/CV 구분을 넘어 **앱 구축, 스케일링, software engineering, UX**까지 확장하는 것.
- **놀라웠던 점?** → ① hype가 의사결정권자들마저 압도한 것 ② 장기 이득보다 즉각 수익을 좇는 경향(예: 연 $10만짜리 TensorFlow 자격증 프로그램이 goodwill에도 불구하고 수익 안 난다고 폐지 — 시리아의 한 청년은 이 자격증으로 독일 취업·가족을 전쟁지역에서 구출). ③ 비전문가의 성공 — 13세에 학교를 그만둔 전직 아이스하키 선수가 ChatGPT로 분기 보고서를 직접 만들어 연 $15만 컨설팅 비용을 절감, 그 돈을 빈곤 아동에게.
- **비전문가의 hype 항법?** → 우리가 그들의 **trusted advisor**가 될 기회. reward mechanism(engagement 우대)을 꿰뚫어보고 peel apart 해주기. 평범한 사람도 매우 지적임을 인정하고 그들이 잘하는 곳에서 빛나게 하라.
- **과학 연구에서 AI?** → 거의 항상 좋은 생각. 가진 가장 강력한 도구를 쓰되 결과를 grounded reality와 대조 검증하라. 8년 전 뇌암 연구자의 최대 병목은 GPU 접근(10명이 1개 공유) → Google Colab 무료 GPU로 연구가 비약. **접근성 확대**가 연구를 진전시킨다.
- **AI는 평등의 힘인가 불평등의 힘인가?** → 둘 다 될 수도, 둘 다 아닐 수도. 어떤 도구든 어떤 목적에도 쓰일 수 있다. **"good intent를 가정하되 bad intent에 대비하라."** AI 자체엔 선택권이 없고, 결국 사람이 어떻게 쓰느냐의 문제.

---

## 🔑 핵심 용어

| 용어 | 설명 |
|---|---|
| **PM bottleneck (제품관리 병목)** | 빌드가 싸/빨라질수록 "무엇을 만들지 결정/spec 작성"이 새 병목이 됨 |
| **METR 7-month doubling** | AI가 할 수 있는 task의 길이가 약 7개월마다 2배(코딩은 ~70일) |
| **Engineer:PM ratio** | 엔지니어:PM 인원 비율. 4:1~8:1에서 2:1, 1:1로 하락 추세 |
| **Connective tissue** | 비공개 bleeding-edge 정보가 흐르는 사람·관계망 (Stanford의 강점) |
| **Three pillars** | 이해의 깊이 + 비즈니스 포커스 + 실행 편향 |
| **Technical debt (기술 부채)** | 무언가를 만들 때 떠안는 유지·버그·문서·이해 비용. 좋은 부채(가치↑) vs 나쁜 부채 |
| **Trusted advisor** | hype를 걸러 "왜"를 묻고 본질을 짚어주는 신뢰받는 조언자 |
| **Signal vs Noise** | 소셜미디어의 통화는 engagement(≠정확성). signal을 가려내는 능력이 가치 |
| **Agentic 4단계** | Understand intent → Plan → Use tools → Reflect(루프) |
| **Big vs Small AI** | 남이 호스팅하는 대형 모델 vs self-host 가능한 소형(open-weights) 모델 |
| **Fine-tuning** | open 모델을 downstream task에 맞게 조정 — Small AI 시대의 핵심 스킬 |
| **SME (Scalable Matrix Extensions)** | AI 워크로드를 CPU에서 돌리게 하는 모바일 기술(온디바이스 AI) |
| **Artificial understanding** | 모델이 나를 대신해 이해하고, 그 이해로 새 것을 만드는 superpower |

## ✍️ 학습 메모

- 이 강의는 모델/수식이 아니라 **커리어 전략**이 주제다. 두 연사의 메시지가 일관되게 수렴한다: 빌드는 싸졌으니 **무엇을·왜 만드는지(비즈니스+사람)**가 차별점이다.
- 실천 항목: ① AI 코딩 도구 최신 세대 유지 ② 사용자와 대화하는 엔지니어 되기 ③ "원하는 직업"용 output(보여줄 프로젝트) 만들기 ④ 무언가 만들 때 technical debt를 의식하고 human understanding 챙기기 ⑤ hype를 mundane하게 풀어 설명하는 연습 ⑥ fine-tuning·온디바이스 등 Small AI 흐름 학습 ⑦ 스킬 다양화.
- 인터뷰 관점: 회사도 "함께 일할 사람"으로 당신을 본다. backbone ≠ hostility. risk mitigation mindset이 강력한 차별점.
- 두 번 강조된 메시지: **"go and build stuff"** + **"함께 일하는 사람을 가장 중요한 기준으로"**.

---
<sub>Claude Code가 자막 전문(영어)을 읽고 작성한 강의 노트 · video_id: `AuZoDsNmG_s`</sub>
