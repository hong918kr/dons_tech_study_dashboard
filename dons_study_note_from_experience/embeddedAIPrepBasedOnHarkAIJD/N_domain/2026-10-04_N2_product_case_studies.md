# N2. 기존 제품 사례 — AI 웨어러블은 무엇이 성공하고 왜 실패했나

> **이 노트를 다 읽으면**: Apple Watch·AirPods, Pixel Watch, Galaxy Watch·Ring, Oura, Meta Ray-Ban, Humane AI Pin, Rabbit R1, AI 펜던트(Limitless·Bee·Friend·Plaud), OpenAI–io 기기를 "AI를 어디서 돌리나(기기·폰·클라우드) · 배터리와 열 · 지연 · 프라이버시 · 가격과 사업 모델 · 결과"로 나눠 출처와 함께 말할 수 있다 · 공개된 숫자만으로 비교표, 기능 배치 행렬, 평균 전류 역산, 지연·열 모델을 코드로 만들 수 있다 · 실패 사례를 "증상 → 원인 → 재발 방지"의 failure analysis로 설명하고, Hark 같은 1세대 기기에서 펌웨어·AI 엔지니어가 무엇을 다르게 해야 하는지 면접에서 근거를 대며 말할 수 있다
> **JD 연결**: JD 우대 "wearables", "hybrid edge-LLM pipelines" · study_prep_list **N2** 행: Apple Watch/AirPods, Pixel Watch, Meta Ray-Ban, Oura, Humane Pin · Rabbit R1이 실패한 이유 ("Hark 제품 이해, 인터뷰 대화"). 함께 닿는 행: **N1**(웨어러블 제약), **N4**(always-listening UX), **I1**(예산), **I3**(cascade·분할), **L3**(hybrid routing), **L4**(음성 파이프라인 지연), **H6**(프라이버시), **M3**(실리콘 지형)
> **Don 기준 난이도**: 제품 post-mortem, 필드 불량 분석, 전력·열 margin sign-off, NPI 일정 압박은 SSD·RF 경험으로 이미 안다 / 새로 배울 것은 컨슈머 AI 기기 시장의 사례와 숫자, "AI 기능을 어느 계층에 둘지"라는 제품 수준의 판단, 그리고 그 판단을 면접 대화로 꺼내는 법
> **선행 노트**: I1(예산 설정), I3(partitioning·cascade), L3(hybrid routing), L4(음성 파이프라인 e2e), H6(프라이버시·동의), M3(벤더 지형), E9(전력·열 하드웨어). 이 노트는 그 내용을 다시 설명하지 않고 ID로 가리킨다

---

## 0. 큰 그림 — 이게 왜 필요한가

Hark는 "AI 랩 + 전용 하드웨어" 회사다. 면접관은 거의 확실하게 "우리 시장을 어떻게 보나?"를 묻는다. 이 질문에는 정답이 없지만 **나쁜 답**은 있다. "Humane은 실패했고 Meta는 잘 됐죠" 같은 뉴스 요약이 그렇다. 좋은 답은 엔지니어의 답이다. 즉 **어떤 기능이 어느 칩에서 돌았고, 그래서 배터리·열·지연이 어떻게 됐고, 그게 사용자 경험과 매출로 어떻게 번졌는지**를 말하는 것이다.

Don은 이 일의 축소판을 이미 해 봤다. SSD 펌웨어의 에러 처리·telemetry로 불량을 "증상 → 재현 → root cause → 수정 → 재발 방지" 순서로 좇았고, Apple에서는 새 실리콘의 버스 장애를 root cause 했다. RF 칩 통합에서도 "전력·성능 margin이 어디서 깎였나"를 sign-off 했다. 제품 사례 분석은 그 failure analysis를 **시장 단위로** 하는 것이다. 차이는 증거가 로그가 아니라 리뷰·보도자료·리콜 공지라는 점뿐이다.

### 0.1 이 노트의 규칙 — 사실과 판단을 섞지 않는다

- 구체적 사실(날짜, 가격, 배터리 시간, 칩, 판매량)에는 `[n]`을 붙인다. `[n]`은 맨 아래 **참고 자료** 번호다. **확인일은 모두 2026-10-04**다.
- 공식 자료(뉴스룸, 제품 페이지, 리콜 공지)를 우선한다. 매체 기사만 있으면 "2차"라고 쓴다. 본문을 직접 열지 못하고 검색 요약으로만 확인한 것은 참고 자료에 "검색 요약"이라고 표시한다.
- 확인하지 못한 것은 쓰지 않거나 **"확인 필요"**라고 쓴다. 루머(예: OpenAI 기기의 형태·칩)는 "미확인 보도"로만 언급한다.
- "성공/실패", 요인 점수, Hark 시사점은 **이 노트의 판단(분석)**이다. Hark에 대한 사실은 이 작업공간의 리서치 파일(`hark_ai_embedded_ai_context.md` §2, `hark_ai_embedded_swe_context.md` §2)과, 이번에 다시 검색해 본 결과(2026-10-04 기준 하드웨어 발표 없음)뿐이다.
- 배터리 시간은 **벤더 주장**이고 조건이 제각각이다(반지는 "최대 7~8일", 이어버드는 "ANC 켜고 음악", 녹음기는 "연속 녹음"). 같은 축에 놓을 때는 이 차이를 먼저 말한다.

결과 라벨의 정의도 미리 정해 둔다.

| 라벨 | 이 노트에서의 뜻 | 예 |
|---|---|---|
| success | 공개 지표(판매량·매출·세대 교체)가 꾸준히 커졌다 | Ray-Ban Meta, Oura |
| mixed | 계속 팔리지만 리뷰나 사건으로 평가가 엇갈린다 | Pixel Watch 1세대, Galaxy Ring |
| failed | 사용자 이탈·환불·서비스 종료가 공개적으로 확인됐다 | Humane AI Pin, Rabbit R1(하드웨어로서), Friend |
| acquired | 독립 제품으로는 끝나고 큰 회사에 흡수됐다 | Limitless, Bee |
| ongoing | 출시된 지 얼마 안 돼 판단 이르다 | Ray-Ban Display, Pixel Watch 5 |

### 0.2 사례를 보는 렌즈 — 세 계층과 일곱 요인

모든 AI 웨어러블은 계산을 **기기(D) · 폰(P) · 클라우드(C)** 세 계층에 나눠 둔다. 이 노트는 각 제품을 이 그림 위에 올려놓고 본다.

```svg
<svg viewBox="0 0 680 318" xmlns="http://www.w3.org/2000/svg">
<rect x="20" y="40" width="200" height="190" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<rect x="250" y="40" width="180" height="190" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<rect x="460" y="40" width="200" height="190" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="120" y="30" font-size="14" text-anchor="middle">기기 (D)</text>
<text x="340" y="30" font-size="14" text-anchor="middle">폰 (P)</text>
<text x="560" y="30" font-size="14" text-anchor="middle">클라우드 (C)</text>
<text x="32" y="66" font-size="12">always-on MCU</text>
<text x="32" y="84" font-size="12">VAD · wake word · IMU 제스처</text>
<text x="32" y="102" font-size="12">착용 감지 · 센서 로그</text>
<text x="32" y="132" font-size="12">SoC CPU/DSP/NPU</text>
<text x="32" y="150" font-size="12">간단 명령 · 받아쓰기</text>
<text x="32" y="168" font-size="12">소형 SLM · 오프라인 번역</text>
<text x="32" y="198" font-size="12">예: Watch 기기 Siri [1],</text>
<text x="32" y="216" font-size="12">Display 받아쓰기 [20]</text>
<text x="262" y="66" font-size="12">앱 · 연결(인터넷 중계)</text>
<text x="262" y="84" font-size="12">더 큰 on-device 모델</text>
<text x="262" y="102" font-size="12">개인 데이터 · 화면</text>
<text x="262" y="198" font-size="12">예: AirPods 번역·심박</text>
<text x="262" y="216" font-size="12">모델 = iPhone [4]</text>
<text x="472" y="66" font-size="12">대형 LLM · 에이전트</text>
<text x="472" y="84" font-size="12">장기 메모리 · 검색</text>
<text x="472" y="102" font-size="12">전사·요약 (지연 허용)</text>
<text x="472" y="198" font-size="12">예: AI Pin [26], R1 [32],</text>
<text x="472" y="216" font-size="12">Plaud 전사 [41]</text>
<line x1="220" y1="135" x2="250" y2="135" stroke="currentColor" stroke-width="1.5"/>
<line x1="430" y1="135" x2="460" y2="135" stroke="currentColor" stroke-width="1.5"/>
<text x="235" y="128" font-size="11" text-anchor="middle">BT</text>
<text x="445" y="128" font-size="11" text-anchor="middle">IP</text>
<path d="M 120 232 Q 340 300 560 232" fill="none" stroke="currentColor" stroke-dasharray="4 4"/>
<text x="340" y="286" font-size="12" text-anchor="middle">Wi-Fi / 셀룰러 직결 (AI Pin, Muse Charm처럼 폰 없이)</text>
<text x="20" y="310" font-size="12">왼쪽일수록 빠르고 오프라인·사적, 오른쪽일수록 똑똑하지만 링크·전력·서버 비용에 묶인다</text>
</svg>
```
그림 1 — AI 웨어러블의 세 계층. 왼쪽(기기)으로 갈수록 빠르고 오프라인이며 사적이다. 오른쪽(클라우드)으로 갈수록 똑똑하지만 무선 링크·배터리·서버 비용에 묶인다. 실패한 제품들은 대체로 "폰"을 건너뛰고 기기와 클라우드를 셀룰러로 직결했다 (점선). 각 예는 본문 절에서 출처와 함께 다룬다.

그리고 일곱 요인으로 점수를 매긴다. 이 요인은 12절에서 표와 코드로 다시 나온다.

| 요인 | 질문 | Don의 펌웨어 감각으로 바꾸면 |
|---|---|---|
| 명확한 용도 | 사용자가 매일 이걸 꺼내는 한 가지 이유가 있나? | "이 펌웨어 기능의 acceptance criteria가 한 줄로 쓰이나?" |
| 지연·신뢰성 | 말하고 몇 초 만에, 몇 %의 확률로 원하는 결과가 오나? | p90 latency, 실패율, 재시도 정책 |
| 배터리·열 | 하루를 버티나? 피부에 닿는 표면이 뜨겁지 않나? | 평균 전류 예산, thermal throttling 정책 |
| 폰 통합 | 폰과 경쟁하나, 폰을 도와주나? | host와의 인터페이스 설계 (SSD ↔ host처럼) |
| 프라이버시·사회적 수용 | 주변 사람이 불편해하지 않나? 데이터가 어디로 가나? | 보안 요구사항, 표시등 같은 HW 인터락 |
| 가격·구독 | 2년 총비용이 가치에 맞나? | BOM + 운영비(서버) 구조 |
| 업데이트 속도 | 출시 후 OTA로 얼마나 빨리 고치나? | FW 릴리스 주기, A/B OTA, 회귀 테스트 |

### 0.3 실행 환경

모든 예제는 스크래치 폴더 `/private/tmp/claude-501/n2/`에서 `.venv/bin/python`으로 실행했다. 데이터 모듈(`products.py`, `placement.py`, `scores.py`)을 같은 폴더에 두고 예제가 import 한다. 노트 폴더에는 아무 파일도 남기지 않는다.

```sh
cd /private/tmp/claude-501/n2
VENV=~/workspace/dons_tech_study_dashboard/dons_study_note_from_experience/embeddedAIPrepBasedOnHarkAIJD/.venv
$VENV/bin/python ex1.py
```

---

## 1. 타임라인과 데이터셋 — 4년을 한 장에

### 1.1 한눈에 보기

```svg
<svg viewBox="0 0 680 433" xmlns="http://www.w3.org/2000/svg">
<line x1="40" y1="383" x2="640" y2="383" stroke="currentColor" stroke-width="1.5"/>
<line x1="107.1" y1="12" x2="107.1" y2="383" stroke="currentColor" stroke-opacity="0.2"/><text x="107.1" y="399" font-size="12" text-anchor="middle">2023</text>
<line x1="240.2" y1="12" x2="240.2" y2="383" stroke="currentColor" stroke-opacity="0.2"/><text x="240.2" y="399" font-size="12" text-anchor="middle">2024</text>
<line x1="373.7" y1="12" x2="373.7" y2="383" stroke="currentColor" stroke-opacity="0.2"/><text x="373.7" y="399" font-size="12" text-anchor="middle">2025</text>
<line x1="506.9" y1="12" x2="506.9" y2="383" stroke="currentColor" stroke-opacity="0.2"/><text x="506.9" y="399" font-size="12" text-anchor="middle">2026</text>
<circle cx="75.4" cy="24" r="5" fill="#4a7bd0"/><text x="84.4" y="28" font-size="12">Pixel Watch 1 발표</text>
<circle cx="199.8" cy="45" r="5" fill="#4a7bd0"/><text x="208.8" y="49" font-size="12">Apple Watch S9 — 기기 안 Siri</text>
<circle cx="205.2" cy="66" r="5" fill="#4a7bd0"/><text x="214.2" y="70" font-size="12">Ray-Ban Meta 1세대 발표</text>
<circle cx="220.9" cy="87" r="5" fill="#4a7bd0"/><text x="229.9" y="91" font-size="12">Humane AI Pin 발표 ($699 + $24/월)</text>
<circle cx="243.2" cy="108" r="5" fill="#4a7bd0"/><text x="252.2" y="112" font-size="12">Rabbit R1 발표 (CES, $199)</text>
<circle cx="277.1" cy="129" r="5" fill="#d0564a"/><text x="286.1" y="133" font-size="12">AI Pin 출하 → 혹평 (발열·지연)</text>
<circle cx="309.9" cy="150" r="5" fill="#4a7bd0"/><text x="318.9" y="154" font-size="12">Galaxy Ring (구독 없음)</text>
<circle cx="340.9" cy="171" r="5" fill="#4a7bd0"/><text x="349.9" y="175" font-size="12">Oura Ring 4</text>
<circle cx="391.2" cy="192" r="5" fill="#d0564a"/><text x="382.2" y="196" font-size="12" text-anchor="end">HP가 Humane 자산 인수, Pin 서비스 종료</text>
<circle cx="424.8" cy="213" r="5" fill="#e08a3c"/><text x="415.8" y="217" font-size="12" text-anchor="end">OpenAI, io 인수 발표</text>
<circle cx="447.4" cy="234" r="5" fill="#e08a3c"/><text x="438.4" y="238" font-size="12" text-anchor="end">Amazon, Bee 인수</text>
<circle cx="468.2" cy="255" r="5" fill="#4a7bd0"/><text x="459.2" y="259" font-size="12" text-anchor="end">Ray-Ban Meta Gen 2 · Ray-Ban Display</text>
<circle cx="497.0" cy="276" r="5" fill="#e08a3c"/><text x="488.0" y="280" font-size="12" text-anchor="end">Meta, Limitless 인수 (펜던트 판매 중단)</text>
<circle cx="523.3" cy="297" r="5" fill="#d0564a"/><text x="514.3" y="301" font-size="12" text-anchor="end">OpenAI 첫 기기 2027년으로 연기 보도</text>
<circle cx="536.8" cy="318" r="5" fill="#3f9a6b"/><text x="527.8" y="322" font-size="12" text-anchor="end">Hark stealth 해제</text>
<circle cx="588.2" cy="339" r="5" fill="#4a7bd0"/><text x="579.2" y="343" font-size="12" text-anchor="end">Pixel Watch 5</text>
<circle cx="603.5" cy="360" r="5" fill="#4a7bd0"/><text x="594.5" y="364" font-size="12" text-anchor="end">Meta Connect 2026 — Muse Charm</text>
<circle cx="60" cy="419" r="5" fill="#4a7bd0"/><text x="70" y="423" font-size="12">출시·발표</text>
<circle cx="200" cy="419" r="5" fill="#d0564a"/><text x="210" y="423" font-size="12">실패·종료·지연</text>
<circle cx="340" cy="419" r="5" fill="#e08a3c"/><text x="350" y="423" font-size="12">인수</text>
<circle cx="480" cy="419" r="5" fill="#3f9a6b"/><text x="490" y="423" font-size="12">Hark</text>
</svg>
```
그림 2 — 2022~2026 AI 웨어러블 주요 사건. 파랑은 출시·발표, 빨강은 실패·종료·지연, 주황은 인수, 초록은 Hark. 날짜와 출처는 본문 각 절 [1]–[46]. "Pin 출하"는 판매 개시와 첫 리뷰가 나온 2024-04-11 기준이다 [27].

이 그림에서 읽을 것은 세 가지다.

1. **2023 말~2024 초에 "폰을 대체하는 AI 기기"가 몰려 나왔고 (AI Pin, R1), 2025년 초까지 둘 다 사실상 끝났다.** Humane은 발표에서 서비스 종료까지 약 15개월, 출하에서 종료까지 10개월 남짓이었다.
2. **같은 시기에 "이미 쓰는 물건에 AI를 더한" 제품(시계, 안경, 반지)은 세대를 거듭했다.** Ray-Ban Meta는 Gen 1 → Gen 2 → Gen 3과 Display·Audio로 갈라졌다.
3. **2025년 하반기부터는 빅테크가 작은 회사를 사들이는 단계다** (Bee → Amazon, Limitless → Meta, io → OpenAI). 기능 자체는 살아남고 독립 하드웨어 회사가 사라진다. Hark는 이 시점(2026-03)에 등장했다.

### 1.2 데이터셋 — 확인된 속성만 담기

손으로 표를 만들면 숫자가 섞인다. 먼저 **출처 있는 숫자만** Python dict에 넣는다. 모르는 값은 `None`으로 둔다. 펌웨어에서 레지스터 맵을 데이터시트에서 옮길 때 "확인 안 된 비트는 reserved로 둔다"와 같은 원칙이다.

```python
# products.py — N2 사례 데이터셋. 숫자는 공개 출처 [n] (확인일 2026-10-04). None = 확인 못 함
# sub = 필수 월 구독(USD, 0 = 필수 구독 없음). batt_h = 벤더 주장 배터리 시간. outcome = 이 노트의 판단 라벨
P = {
 "Apple Watch S9":     dict(form="watch",   date="2023-09-12", price=399, sub=0,     batt_h=18,   chip="S9 SiP, 4-core Neural Engine", outcome="success",  ref="[1]"),
 "Apple Watch S11":    dict(form="watch",   date="2025-09-09", price=399, sub=0,     batt_h=24,   chip=None,                           outcome="success",  ref="[2]"),
 "AirPods Pro 3":      dict(form="earbuds", date="2025-09-09", price=249, sub=0,     batt_h=8,    chip=None,                           outcome="success",  ref="[4]"),
 "Pixel Watch (1st)":  dict(form="watch",   date="2022-10-06", price=None, sub=0,    batt_h=24,   chip="Exynos 9110 + Cortex-M33",     outcome="mixed",    ref="[7]"),
 "Pixel Watch 4":      dict(form="watch",   date="2025-08-20", price=349, sub=0,     batt_h=30,   chip="Snapdragon W5 Gen 2 + co-proc", outcome="success", ref="[8]"),
 "Pixel Watch 5":      dict(form="watch",   date="2026-08-12", price=399, sub=0,     batt_h=40,   chip="Snapdragon W5 Gen 2",          outcome="ongoing",  ref="[9]"),
 "Galaxy Ring":        dict(form="ring",    date="2024-07-10", price=399.99, sub=0,  batt_h=168,  chip=None,                           outcome="mixed",    ref="[10]"),
 "Oura Ring 4":        dict(form="ring",    date="2024-10-03", price=349, sub=5.99,  batt_h=192,  chip=None,                           outcome="success",  ref="[13]"),
 "Ray-Ban Meta Gen1":  dict(form="glasses", date="2023-09-27", price=299, sub=0,     batt_h=None, chip="Snapdragon AR1 Gen 1",         outcome="success",  ref="[17]"),
 "Ray-Ban Meta Gen2":  dict(form="glasses", date="2025-09-17", price=379, sub=0,     batt_h=8,    chip=None,                           outcome="success",  ref="[18]"),
 "Meta Ray-Ban Display": dict(form="glasses", date="2025-09-17", price=799, sub=0,   batt_h=6,    chip=None,                           outcome="ongoing",  ref="[19]"),
 "Ray-Ban Meta Audio": dict(form="glasses", date="2026-09-23", price=349, sub=0,     batt_h=12,   chip=None,                           outcome="ongoing",  ref="[22]"),
 "Humane AI Pin":      dict(form="pin",     date="2023-11-09", price=699, sub=24,    batt_h=4.5,  chip="Snapdragon (모델 미공개), 4GB", outcome="failed",   ref="[25][26]"),
 "Rabbit R1":          dict(form="handheld",date="2024-01-09", price=199, sub=0,     batt_h=None, chip="MediaTek Helio P35",           outcome="failed",   ref="[31][32]"),
 "Limitless Pendant":  dict(form="pendant", date=None,         price=99,  sub=None,  batt_h=None, chip=None,                           outcome="acquired", ref="[36]"),
 "Bee":                dict(form="pendant", date=None,         price=49.99, sub=19,  batt_h=None, chip=None,                           outcome="acquired", ref="[37]"),
 "Friend":             dict(form="pendant", date=None,         price=129, sub=None,  batt_h=None, chip=None,                           outcome="failed",   ref="[39][40]"),
 "Plaud NotePin":      dict(form="pendant", date=None,         price=159, sub=0,     batt_h=20,   chip=None,                           outcome="success",  ref="[41][42]"),
}
```

필드 몇 가지를 짚는다.

- `sub`: **필수** 월 구독(USD). 0은 "필수 구독 없음"이다. Oura는 $5.99/월(또는 $69.99/년) 멤버십이 필요하다 [13]. Humane은 $24/월이 필수였다 [25].
- `batt_h`: 벤더가 주장한 시간. Humane 4.5는 "4~5시간"의 가운데 값이다 [26]. Ray-Ban Meta 1세대는 공식 숫자를 이번에 확인하지 못해 `None`으로 뒀다 (Gen 2가 "거의 2배인 최대 8시간"이라고 했으니 역산하면 4시간 안팎이다 [18]).
- `outcome`: 0.1절 라벨. 판단이다.

### 1.3 코드로 확인 — 비교표와 2년 총비용 (예제 1)

무엇을 확인하는 코드인가: dict를 DataFrame으로 바꿔 가격·구독·2년 총소유비용(TCO)·배터리를 한 표에 놓는다.

```python
# 예제 1: 검증된 속성만으로 비교표 만들기 — 가격·구독·배터리 주장·결과
import pandas as pd
from products import P

df = pd.DataFrame.from_dict(P, orient="index")
df["year"] = df["date"].str[:4].fillna("-")
# 2년 총소유비용(TCO) = 기기 가격 + 24개월 구독 (구독 정보가 없으면 NaN으로 둔다)
df["tco_2y"] = df["price"] + 24 * df["sub"]
cols = ["form", "year", "price", "sub", "tco_2y", "batt_h", "outcome"]
pd.set_option("display.width", 120)
print(df[cols].sort_values("tco_2y", ascending=False).to_string(na_rep="-"))
print("\n결과별 제품 수:", df["outcome"].value_counts().to_dict())
```

```text
                          form  year   price    sub   tco_2y  batt_h   outcome
Humane AI Pin              pin  2023  699.00  24.00  1275.00     4.5    failed
Meta Ray-Ban Display   glasses  2025  799.00   0.00   799.00     6.0   ongoing
Bee                    pendant     -   49.99  19.00   505.99       -  acquired
Oura Ring 4               ring  2024  349.00   5.99   492.76   192.0   success
Galaxy Ring               ring  2024  399.99   0.00   399.99   168.0     mixed
Apple Watch S9           watch  2023  399.00   0.00   399.00    18.0   success
Apple Watch S11          watch  2025  399.00   0.00   399.00    24.0   success
Pixel Watch 5            watch  2026  399.00   0.00   399.00    40.0   ongoing
Ray-Ban Meta Gen2      glasses  2025  379.00   0.00   379.00     8.0   success
Pixel Watch 4            watch  2025  349.00   0.00   349.00    30.0   success
Ray-Ban Meta Audio     glasses  2026  349.00   0.00   349.00    12.0   ongoing
Ray-Ban Meta Gen1      glasses  2023  299.00   0.00   299.00       -   success
AirPods Pro 3          earbuds  2025  249.00   0.00   249.00     8.0   success
Rabbit R1             handheld  2024  199.00   0.00   199.00       -    failed
Plaud NotePin          pendant     -  159.00   0.00   159.00    20.0   success
Pixel Watch (1st)        watch  2022       -   0.00        -    24.0     mixed
Limitless Pendant      pendant     -   99.00      -        -       -  acquired
Friend                 pendant     -  129.00      -        -       -    failed

결과별 제품 수: {'success': 8, 'ongoing': 3, 'failed': 3, 'mixed': 2, 'acquired': 2}
```

출력에서 볼 것: 2년 TCO 1위가 Humane AI Pin($1,275)이고, 이 표에서 가장 비싼 "실패"다. 반대로 성공한 제품들의 2년 비용은 $250~$500에 모여 있다. 단, 가격만으로 실패를 설명할 수는 없다. Rabbit R1은 $199에 구독도 없었는데 실패했다. 12절에서 요인을 나눠 본다.

### 1.4 함정

- **배터리 숫자를 같은 축에 놓고 바로 비교하기.** Plaud 20 h는 "연속 녹음" [41], AirPods 8 h는 "ANC 켜고 음악" [4], 반지는 "최대" 일수다. 사용 패턴이 다르다.
- **결과 라벨을 사실처럼 쓰기.** "Galaxy Ring은 실패"라고 말할 근거는 없다. 판매량이 공개되지 않았다.
- **발표일과 출하일 혼동.** AI Pin은 2023-11 발표, 2024-04 출하다 [30]. R1은 2024-01 발표, 봄 출하다 [31].

---

## 2. Apple Watch & AirPods — 폰이 두뇌, 기기는 센서와 빠른 반사신경

### 2.1 무엇을 하나

Apple Watch는 건강·운동·알림 기기이고, AirPods는 오디오 기기다. 둘 다 "AI 기기"로 팔리지 않는다. 그런데 안에는 ML이 가득하다. 이게 첫 번째 교훈이다. **성공한 제품은 AI를 앞세우지 않고 기능 안에 숨긴다.**

### 2.2 AI를 어디서 돌리나

| 기능 | 위치 | 근거 |
|---|---|---|
| 인터넷이 필요 없는 Siri 요청 (타이머, 운동 시작) | 기기 (S9 SiP, 4-core Neural Engine) | "Siri requests can be processed on device", Wi-Fi·셀룰러 없이 더 빠르고 안정적 [1] |
| Siri로 건강 데이터 조회·기록 | 기기 | Series 9 발표 [1] |
| Double tap 제스처 | 기기. 가속도계·자이로·광학 심박 센서를 새 ML 알고리즘으로 처리 | [1] |
| 고혈압 알림 (Series 11) | ML로 개발 (10만 명 이상 연구 데이터). 실행 위치는 확인 필요 | [2] |
| Workout Buddy (생성형 음성 코칭) | **폰**. Apple Intelligence가 되는 iPhone과 블루투스 헤드폰 필요 | [3] |
| AirPods Pro 3 Live Translation | **폰**. "Apple Intelligence-enabled iPhone nearby" 필요 | [4] |
| AirPods Pro 3 심박 기반 운동 추적 | 센서는 이어버드(PPG 256 Hz 펄스), 모델은 **iPhone 위 on-device 모델** | [4] |
| 일반 질의 (지식, 웹) | 네트워크 너머 (기기에서 처리하는 건 "인터넷이 필요 없는 요청"뿐이라는 문구에서 추정) | [1] 추정 |

말로 하면: **기기는 반사신경(즉시 반응해야 하는 것), 폰은 큰 두뇌(생성형·번역), 클라우드는 지식**이다. Apple은 Watch에서 클라우드로 바로 가지 않고 iPhone을 중간 계층으로 쓴다. 이게 Humane과 정반대 선택이다.

### 2.3 배터리와 칩

- Series 9: "18 hours all-day battery" [1]. Series 11: "up to 24 hours" [2]. 2년 사이에 같은 가격($399)에서 6시간이 늘었다.
- AirPods Pro 3: ANC 켜고 최대 8시간, 이전 세대 대비 33% 증가 [4].
- S9의 Neural Engine은 이전 대비 ML 처리 "up to two times faster" [1]. Double tap은 이 NPU가 있어서 가능했다고 Apple이 직접 설명한다 [1].

### 2.4 Siri의 경로 — 가장 큰 회사도 LLM 비서는 늦었다

- Apple은 2024년 WWDC에서 개인화된 Siri를 발표했지만 늦어졌다. 2026-01-12 Apple은 Google과 다년 협력을 발표했고, 차세대 Apple Foundation Models가 Gemini 기반이 된다고 확인했다 [5]. MacRumors 정리에 따르면 모델은 기기와 Apple의 Private Cloud Compute에서 돈다 [5].
- 2026-09-14 iOS 27과 함께 재설계된 Siri가 베타(opt-in, 대기자 명단 가능)로 나왔다고 보도됐다 [6]. 이 항목은 검색 요약으로만 확인했다. Watch·AirPods에서의 동작 방식은 확인 필요.

면접에서 쓸 포인트: **Apple조차 "범용 LLM 비서"는 2년 넘게 미뤘고, 그동안 Watch·AirPods는 좁고 확실한 ML 기능(double tap, 심박, 번역)으로 팔렸다.** 범용 비서는 마지막에 얹는 것이다.

### 2.5 프라이버시 UX

on-device Siri는 프라이버시 메시지이기도 하다. 건강 데이터를 Siri로 조회하는 기능을 기기 안에서 처리한다는 설명이 그렇다 [1]. Apple은 "어디서 처리되는가"를 기능 설명의 일부로 쓴다.

### 2.6 결과와 교훈

- 결과: success (세대 교체, 가격 유지, 기능 확장).
- 교훈 1: 기기 → 폰 → 클라우드의 **계단식 배치**. 기기에는 지연이 중요한 것만.
- 교훈 2: 새 ML 기능(double tap)은 **새 센서를 추가하지 않고 기존 센서 융합 + NPU**로 만들었다. 펌웨어 관점에서 BOM 증가 없는 기능이다.
- 교훈 3: 생성형 기능은 폰을 요구조건으로 두고 시작했다 (Workout Buddy, Live Translation). 웨어러블 단독에 억지로 넣지 않았다.

### 2.7 Don 경험과 연결

Apple RF 통합에서 Don은 "이 기능은 BB(baseband)에서, 이건 AP에서" 같은 분할을 봤을 것이다. AirPods → iPhone 구조는 SSD에서 **controller는 빠른 경로, host는 정책**을 맡는 분할과 같다. 면접에서 "host offload"라는 말로 연결할 수 있다.

---

## 3. Google Pixel Watch — 하루 배터리의 교훈과 co-processor

### 3.1 1세대: 배터리가 첫 리뷰를 결정했다

Pixel Watch 1세대는 2022-10-06 발표, 10-13 출시였다 [7]. 칩은 Samsung Exynos 9110 + Arm Cortex-M33 co-processor, 배터리 294 mAh, 주장 시간 약 24시간이다 [7]. 여러 리뷰(CNET, Engadget, CNN Underscored)가 배터리를 단점으로 꼽았다 [7]. 출시 가격은 이번에 확인하지 못했다 (확인 필요).

### 3.2 4세대·5세대: 하이브리드 계산 구조

- Pixel Watch 4: Snapdragon W5 Gen 2 + "next-gen ML-powered co-processor that's 25% faster at half the power" [8]. 배터리 41 mm 30시간, 45 mm 40시간, 이전 대비 25% 증가 [8]. 손목을 들기만 하면 Gemini에 말을 거는 Raise to Talk [8]. 가격 $349부터, 2025-10-09 매장 출시 [8].
- Pixel Watch 5: 2026-08-12 발표, 08-20 출시, $399부터 [9]. Snapdragon W5 Gen 2 유지, CPU 12%·RAM 50% 증가 [9]. Gemini로 **오프라인에서** 타이머·알람·운동을 다룬다 [9]. 배터리 최대 40시간, 용량 41 mm 332 mAh · 45 mm 465 mAh (2차) [9].

### 3.3 무엇을 배우나

Pixel Watch의 4년은 **"큰 코어 + 저전력 co-processor"** 구조를 다듬은 역사다. 알림·건강 측정 같은 배경 작업을 co-processor로 보내고, 큰 코어는 화면과 Gemini 대화에만 깨운다. Hark 공고에 나오는 "Qualcomm 칩셋 + Hexagon DSP + Ambiq급 MCU" 구조(컨텍스트 파일 §2.2)와 같은 발상이다. 그리고 5세대에서 "오프라인 Gemini 명령"이 생겼다는 건 **간단 명령의 기기 이전**이 업계 공통 방향이라는 신호다 (Apple Watch S9와 같은 방향 [1]).

함정: co-processor가 "25% 빠르고 전력 절반"이라는 건 Google의 주장이다. 무엇과 비교한 수치인지(이전 co-processor?)는 문구로만 짐작할 수 있다.

---

## 4. Samsung Galaxy Watch & Galaxy Ring — 생태계와 작은 배터리의 무게

### 4.1 Galaxy Watch8 — 첫 Gemini 스마트워치

Samsung은 2025-07-09 Galaxy Unpacked에서 Galaxy Watch8을 "Gemini를 탑재한 세계 첫 스마트워치"로 발표했다 [11]. Wear OS 6 기반이다 [11]. Gemini의 실행 위치(기기·폰·클라우드)는 공개 자료에서 확인하지 못했다.

### 4.2 Galaxy Ring

- 2024-07-10 발표, 07-24 일반 판매, $399.99, **구독 없음** [10]. 경쟁사 Oura와의 차별점으로 구독 없음을 내세웠다 [10].
- 배터리 18~23.5 mAh (사이즈별), 최대 7일 [10].
- AI 기능: Galaxy AI 기반 **Energy Score**. 수면·활동·수면 중 심박·HRV로 점수를 낸다. Samsung Health 앱과 동기화해야 볼 수 있다 [11]. 즉 반지는 센서와 로깅, 해석은 폰 쪽이다 (정확한 계산 위치는 확인 필요).

### 4.3 배터리 팽창 사건 — 작은 배터리의 안전

2025-09 한 테크 유튜버의 Galaxy Ring 배터리가 착용 중 부풀어 손가락에서 빠지지 않았고, 병원에서 제거했다 [12]. 그는 배터리 시간이 7일에서 1.5일로 줄었다고 했다 [12]. Samsung은 "extremely rare case"라고 했고, 이후 원인을 "crack in the internal moulding"으로 발표하며 더 넓은 안전 위험은 없다고 밝혔다 [12].

엔지니어의 교훈: **배터리 시간이 갑자기 줄어드는 것 자체가 안전 신호**다. fuel gauge가 용량 감소(SoH 하락)와 내부 저항 증가를 감지하면 경고를 띄우는 펌웨어 기능이 있었다면 사건 전에 알렸을 수 있다. 반지처럼 몸에 끼는 기기에서는 이게 기능이 아니라 안전 요구사항이다.

### 4.4 결과와 교훈

- 결과: mixed. 판매량은 공개되지 않았고 사건 하나로 평가할 수 없다.
- 교훈: 구독 없음은 강한 마케팅 메시지지만, Oura가 구독으로 매출을 키운 것(5절)과 비교하면 **구독이 나쁜 게 아니라 "구독에 걸맞은 지속적 가치"가 있느냐**가 핵심이다.

---

## 5. Oura Ring — 구독이 통하는 조건

### 5.1 제품과 숫자

- Oura Ring 4: 2024-10-03 발표, $349부터, 최대 8일 배터리, Smart Sensing 플랫폼 [13]. 멤버십 $5.99/월 또는 $69.99/년 [13].
- Oura Advisor: 2025-03-31 전체 멤버에게 출시. 건강 센싱 알고리즘과 **LLM**으로 개인화된 조언을 준다 [14]. 이전 대화를 기억하는 "Memories" 기능이 있다 [14]. 실행 위치는 공개되지 않았다. LLM 규모를 생각하면 클라우드로 추정한다.
- 2025-10: $900M 이상 Series E, 기업가치 $11B, 누적 550만 개 판매 (검색 요약) [15].
- 2026-09-03: 공개 S-1 제출. 회계연도 2025 매출 $907.9M(전년 $406.8M 대비 +123%), 2026-06 기준 유료 멤버 500만 명, Nasdaq 티커 OURA 예정 [16].

### 5.2 왜 통했나 (분석)

1. **한 가지 일**: 수면과 회복. 화면도 없고 말도 안 한다. 반지는 센서와 로거다.
2. **배터리가 문제가 되지 않는다**: 주 1회 충전. 사용자가 배터리를 "관리"하지 않는다.
3. **구독이 지속적 해석을 판다**: 하드웨어는 센서, 돈은 매일 아침의 점수와 조언에서 나온다. LLM(Advisor)은 이 해석 층에 얹혔다. 기기에 LLM을 넣은 게 아니다.
4. **사회적 마찰이 거의 없다**: 카메라·마이크가 없다.

펌웨어 관점의 핵심은 2번이다. 반지 배터리는 수십 mAh다. Galaxy Ring으로 역산하면 평균 전류가 0.1 mA 수준이다 (11.2절 예제 3). 이런 기기에서 무선 전송은 **배치 동기화**가 기본이다. 실시간 스트리밍은 예산에 들어가지 않는다.

---

## 6. Meta Ray-Ban — "이미 쓰는 물건"에 AI를 넣었다

### 6.1 세대별 숫자

| 제품 | 발표·출시 | 가격 | 배터리 (주장) | 확인된 하드웨어·AI | 출처 |
|---|---|---|---|---|---|
| Ray-Ban Meta (Gen 1) | 2023-09-27 발표, 10-17 판매 | $299부터 | 케이스 포함 총 36시간 (안경 단독 수치는 확인 필요) | Snapdragon AR1 Gen 1, 12 MP 카메라, 마이크 5개, "Hey Meta", Meta AI는 미국 베타로 시작 | [17] |
| Ray-Ban Meta (Gen 2) | 2025-09 (Connect 2025) | $379부터 | 최대 8시간 (Gen 1의 거의 2배), 케이스 +48시간 | 3K 영상, 6개 언어 실시간 번역 (오프라인 가능), conversation focus | [18] |
| Meta Ray-Ban Display | 2025-09 발표, 09-30 판매 | $799 (Neural Band 포함) | 혼합 사용 6시간, 케이스 포함 30시간, Neural Band 18시간 | 오른쪽 렌즈 컬러 디스플레이, EMG 손목 밴드, 받아쓰기는 on-device 음성 인식 | [19][20] |
| Ray-Ban Meta (Gen 3) | 2026-09-23 (Connect 2026) | $449부터 (2차) | Gen 2보다 약 1시간 더 (2차) | 12 MP, 마이크 6개, action button (2차) | [22] |
| Ray-Ban Meta Audio | 2026-09-23 | $349부터 (2차) | 최대 12시간 | 카메라 없음 | [22] |

판매: EssilorLuxottica 기준 2023-10~2025-02 약 200만 개, **2025년 한 해 700만 개 이상** (전년의 3배 이상) [21].

### 6.2 AI 배치

- 일반 질의(Meta AI): 네트워크가 필요하다. 안경은 폰을 통해 인터넷에 붙는다. Display 리뷰는 "인터넷 연결은 전적으로 폰의 블루투스에 의존"하고, 그래서 앱 로딩이 10초를 넘기도 한다고 썼다 [20].
- 받아쓰기: Display는 **on-device 음성 인식 모델**을 쓰고, 지연이 낮고 정확도가 놀랍다는 평이다 [20]. 입 가까이에 마이크 6개 배열이 있다 [20].
- 번역: Gen 2는 6개 언어 대화 번역을 **오프라인으로도** 지원한다 [18]. 기기와 폰 중 어디서 도는지는 공개 문구로 구분할 수 없다 (그림 3의 "L").
- 디스플레이 깨우기: 제스처 후 1~2초 걸린다는 지적 [20].

### 6.3 왜 통했나 (분석)

1. **AI가 없어도 사는 물건이다.** 선글라스, 카메라, 오픈 이어 스피커, 통화. AI는 덤으로 시작했다. 기대치 관리가 저절로 된다.
2. **폰을 대체하지 않고 폰에 붙는다.** 연결·계산을 폰에 기대 안경의 전력·열을 줄였다.
3. **가격이 $299~$379** 로 일반 선글라스 브랜드 가격대에 가깝다. 구독이 없다.
4. **유통이 이미 있다.** EssilorLuxottica의 안경 매장이다.
5. **배터리를 세대마다 고쳤다.** 4시간대 → 8시간 → 더 길게. 사용자 불만 1순위를 하드웨어 세대로 정면 해결했다.

### 6.4 프라이버시 — 표시등을 하드웨어 인터락으로

안경 앞의 흰 LED는 촬영 중임을 알린다. 1세대부터 LED 쪽에 조도 센서가 있어 LED를 가리면 카메라가 동작하지 않게 했다고 보도됐다 [24]. 2026-07에는 LED가 물리적으로 훼손되면 카메라를 끄는 탐지 업데이트가 강제 배포됐다는 보도가 있다 (검색 요약) [24]. 이건 펌웨어 엔지니어의 일이다. **프라이버시 약속을 UI 문구가 아니라 하드웨어 상태 확인으로 강제**한다. H6의 "펌웨어 쪽 게이트"와 같은 발상이다.

### 6.5 2026년의 새 변수 — Muse Charm

Meta는 Connect 2026(2026-09-23~24)에서 개인 AI 에이전트 Muse를 담은 휴대용 기기 **Muse Charm**을 공개했다 [22][23]. Dezeen 보도에 따르면 2인치 화면에 캐릭터 아바타, 카메라, 지문 센서(대화 시작), **5G 모뎀 내장**(Wi-Fi 없이 인터넷)이고 가격은 미공개, 연말 출시 예정이다 [23]. Zuckerberg는 "안경을 쓰지 않을 때 Muse와 말하는 가장 빠른 방법"이라고 했다 [23].

분석: 이건 Humane·Rabbit이 시도한 "폰 없이 동작하는 AI 전용 기기" 영역에 Meta가 다시 들어온 것이다. 차이는 이미 수백만 명이 쓰는 에이전트(앱·WhatsApp·웹)를 **기기 밖에서 먼저** 만들었다는 점이다 [22]. Hark에게는 가장 직접적인 비교 대상이 하나 더 생긴 셈이다.

---

## 7. Humane AI Pin — 실패의 해부

### 7.1 사실

| 항목 | 내용 | 출처 |
|---|---|---|
| 회사 | 2018 설립, 전 Apple 출신 창업자, 총 $230M 이상 조달 | [29][30] |
| 발표 | 2023-11-09, 주문 2023-11-16부터 | [25][30] |
| 가격 | $699 + 필수 $24/월 (전화번호, 무제한 통화·문자·데이터, AI 질의, 클라우드 저장) | [25][26] |
| 하드웨어 | Qualcomm Snapdragon 옥타코어 2.1 GHz (모델명 확인 필요), RAM 4 GB, 저장 32 GB, 13 MP 초광각 카메라, 녹색 레이저 프로젝터(손바닥에 UI), Trust Light(마이크·카메라 동작 표시), wake word 없음 | [25] |
| 연결 | LTE eSIM (T-Mobile 망), Wi-Fi, BT 5.1 — **폰 없이 단독 동작** | [25] |
| AI | GPT 등 여러 클라우드 LLM과 검색을 상황별로 호출 | [26] |
| 배터리 | 핀 단독 4~5시간, 자석식 battery booster로 약 9시간 | [26] |
| 출하 | 2024-04 | [30] |
| 리뷰 | The Verge: 몇 분만 써도 "overheated and needs to cool down" 알림, 전화가 절반은 안 걸림 | [27] |
| 열 제약 | 프로젝터 사용이 열 때문에 9분으로 제한됐다는 정리 (2차) | [30] |
| 판매 | 2024 여름 반품이 판매를 앞지름 | [29] |
| 리콜 | 2024-10-31 충전 케이스 약 10,500개, 리튬 배터리 과열·화재 위험 | [28] |
| 가격 인하 | 2024-10 $499 | [29][30] |
| 종료 | 2025-02-18 HP가 자산을 $116M에 인수 (CosmOS, 특허 300여 건, 인력). 2025-02-28 서버 연결 종료 → 통화·메시지·AI 질의 불가 | [29] |

### 7.2 엔지니어링 관점의 원인 분석

증상에서 원인으로 거꾸로 올라가 본다. SSD 필드 불량 분석과 같은 순서다.

```
증상                      → 1차 원인                         → 설계 결정(root cause)
-----------------------------------------------------------------------------------------
몇 분 쓰면 과열·대기      → 작은 몸체에서 수 W 버스트          → 폰 없이 LTE + SoC + 카메라 + 프로젝터를 한 몸에
4~5시간 배터리            → 셀룰러 모뎀·SoC가 자주 깨어남      → 모든 질의가 클라우드 왕복 (기기 처리 없음)
느리고 자주 실패          → LTE RTT + 다단 클라우드 호출       → 범용 비서를 첫 기능으로, 간단 명령도 클라우드
전화 실패 등 기본기 불안  → 폰의 기본 기능을 처음부터 재구현  → "폰 대체"를 목표로 함
2년 $1,275                → 셀룰러 요금 + 서버비를 구독으로    → 단독 통신 기기라는 사업 구조
```

말로 하면: **"폰을 대체한다"는 제품 목표 하나가 셀룰러 직결, 클라우드 전면 의존, 단독 전원을 강제했고, 그게 열·배터리·지연을 한꺼번에 무너뜨렸다.** 각각의 문제는 따로 고칠 수 있어 보이지만 뿌리는 하나다.

### 7.3 코드로 확인 — 작은 몸체는 몇 W를 몇 초 버티나 (예제 7)

무엇을 확인하는 코드인가: 1차 RC 열 모델(E9에서 본 열저항·열용량)로, 핀 크기 기기가 전력 P를 계속 쓸 때 표면 설계 한계에 몇 초 만에 닿는지 계산한다. **R_th, C_th, 한계 온도는 모두 설명용 가정**이다. Humane의 실제 값이 아니다.

```
T(t) = T_amb + P·R_th·(1 − e^(−t/τ)),   τ = R_th·C_th
한계 도달 시간 t_hit = −τ·ln(1 − (T_lim − T_amb)/(P·R_th))
```

말로 하면: 열은 전기 RC 회로와 같다. 전력이 전류, 온도가 전압이다. 정상 상태 상승은 P·R_th이고, 그 값이 허용 상승폭보다 크면 언젠가는 넘는다. 넘는 시간은 τ에 비례한다.

손으로 먼저: τ = 25 × 10 = 250 s. 허용 상승 = 43 − 25 = 18 °C. 연속 허용 전력 = 18/25 = 0.72 W. P = 3 W면 정상 상태 상승 75 °C, t_hit = −250·ln(1 − 18/75) = −250·ln(0.76) ≈ 69 s.

```python
# 예제 7: 1차 RC 열 모델 — 작은 기기가 몇 W에서 몇 초 만에 표면 한계에 닿나 (모든 값 가정)
import numpy as np
T_amb, T_lim = 25.0, 43.0      # 주변 온도, 표면 설계 한계(°C) — 설명용 가정
R_th, C_th = 25.0, 10.0        # 열저항 °C/W, 열용량 J/°C (작은 pin급 가정)
tau = R_th * C_th              # 시정수 s
print(f"tau = {tau:.0f} s, 연속 허용 전력 = {(T_lim - T_amb) / R_th:.2f} W")
for P in (0.5, 1.0, 2.0, 3.0, 4.0):
    dT_ss = P * R_th                                   # 정상 상태 상승
    if dT_ss <= T_lim - T_amb:
        print(f"P={P:.1f} W: 정상상태 {T_amb + dT_ss:5.1f} °C → 한계 안 넘음")
        continue
    t_hit = -tau * np.log(1 - (T_lim - T_amb) / dT_ss)   # T(t)=T_amb+P·R(1-e^(-t/τ))
    print(f"P={P:.1f} W: 정상상태 {T_amb + dT_ss:5.1f} °C, 한계 도달 {t_hit:5.0f} s")
# duty cycling: 3 W 버스트를 평균 0.7 W로 만들려면 활성 비율은?
P_idle, P_act = 0.1, 3.0
print(f"평균 0.7 W 를 지키는 3 W 활성 비율 = {(0.7 - P_idle) / (P_act - P_idle):.1%}")
```

```text
tau = 250 s, 연속 허용 전력 = 0.72 W
P=0.5 W: 정상상태  37.5 °C → 한계 안 넘음
P=1.0 W: 정상상태  50.0 °C, 한계 도달   318 s
P=2.0 W: 정상상태  75.0 °C, 한계 도달   112 s
P=3.0 W: 정상상태 100.0 °C, 한계 도달    69 s
P=4.0 W: 정상상태 125.0 °C, 한계 도달    50 s
평균 0.7 W 를 지키는 3 W 활성 비율 = 20.7%
```

출력에서 볼 것: 연속으로 버틸 수 있는 전력은 0.72 W인데, 셀룰러 + SoC + 카메라 + 프로젝터가 동시에 도는 순간은 쉽게 수 W다 (가정). 3 W면 약 1분 만에 한계에 닿는다. "몇 분 쓰면 과열" [27]이라는 증상이 이 단순한 모델로도 자연스럽게 나온다. 그리고 평균 0.7 W를 지키려면 3 W 활성 구간을 약 21%로 제한해야 한다. 사용자 입장에서는 "5번 중 4번은 기다려야 하는 기기"다.

### 7.4 같은 일을 펌웨어로 — 열 governor (C)

무엇을 확인하는 코드인가: 같은 RC 모델을 1초 간격 이산 시뮬레이션으로 돌리고, hysteresis가 있는 governor가 "무거운 작업(클라우드 세션)"을 허용·거절하는 모습을 본다. 사용자가 15분 동안 쉬지 않고 질문하는 최악의 경우다. 상수는 가정이다.

```c
/* 열 governor: 1차 RC 모델로 표면 온도를 추정해 "무거운 작업(클라우드 세션)" 허용 여부를 정한다.
   모든 상수는 설명용 가정. 실제 기기는 skin 센서(NTC) 값을 쓰고 모델은 보정용으로만 쓴다. */
#include <stdio.h>

#define T_AMB    25.0f   /* °C */
#define R_TH     25.0f   /* °C/W */
#define C_TH     10.0f   /* J/°C  → tau = 250 s */
#define T_HIGH   41.0f   /* 이 위로 가면 무거운 작업 금지 */
#define T_LOW    38.0f   /* 이 아래로 내려오면 다시 허용 (hysteresis) */

int main(void) {
    float t = T_AMB;                 /* 추정 표면 온도 */
    int heavy_allowed = 1, denied = 0;
    for (int s = 0; s < 900; s++) {  /* 1 s 간격, 15분 */
        int wants_heavy = 1;         /* 사용자가 계속 질문한다고 가정 (최악) */
        float p = (wants_heavy && heavy_allowed) ? 3.0f : 0.3f;   /* W */
        t += (p - (t - T_AMB) / R_TH) / C_TH;                      /* dT = (P - ΔT/R)/C · dt */
        if (heavy_allowed && t > T_HIGH) heavy_allowed = 0;
        else if (!heavy_allowed && t < T_LOW) heavy_allowed = 1;
        if (wants_heavy && !heavy_allowed) denied++;
        if (s % 150 == 0 || s == 899)
            printf("t=%3d s  T=%5.1f C  heavy=%d\n", s, (double)t, heavy_allowed);
    }
    printf("15분 중 무거운 작업 거절 %d s (%.0f%%)\n", denied, 100.0 * denied / 900);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 governor.c -o governor && ./governor
```

```text
t=  0 s  T= 25.3 C  heavy=1
t=150 s  T= 38.4 C  heavy=0
t=300 s  T= 39.2 C  heavy=1
t=450 s  T= 40.6 C  heavy=0
t=600 s  T= 39.9 C  heavy=0
t=750 s  T= 39.2 C  heavy=0
t=899 s  T= 38.6 C  heavy=0
15분 중 무거운 작업 거절 763 s (85%)
```

출력에서 볼 것: governor는 온도를 지켜 주지만, 무거운 작업 요청의 85%를 거절한다. **throttling은 안전장치이지 해결책이 아니다.** 해결은 무거운 작업의 전력 자체를 줄이는 것이다. 즉 (1) 간단 명령을 기기 NPU·DSP에서 끝내 모뎀을 깨우지 않기, (2) 폰을 연결 계층으로 써서 BT 저전력 링크로 바꾸기, (3) 프로젝터·카메라 같은 고전력 출력의 사용 시간을 설계 단계에서 줄이기다. Ray-Ban Meta가 실제로 택한 길이 (2)다 [20].

### 7.5 함정

- "Humane은 하드웨어가 나빠서 망했다"는 반쪽 설명이다. 같은 Qualcomm Snapdragon 브랜드 칩(AR1 Gen 1)을 쓴 Ray-Ban Meta는 성공했다 [17]. **차이는 칩이 아니라 계층 배치와 제품 목표**다.
- "LLM이 아직 부족해서"도 반쪽이다. 같은 시기 Plaud는 클라우드 LLM으로 매출을 키웠다 (9절). 차이는 **실시간이냐, 나중에 봐도 되느냐**다.

---

## 8. Rabbit R1 — 싼 가격도 "앱이면 되잖아"를 이기지 못했다

### 8.1 사실

| 항목 | 내용 | 출처 |
|---|---|---|
| 발표 | CES 2024 (2024-01-09). 발표 직후 13만 대 사전 판매 | [31] |
| 가격 | $199, 구독 없음 | [32] |
| 하드웨어 | MediaTek Helio P35 2.3 GHz, RAM 4 GB, 저장 128 GB, 2.88인치 터치스크린, 8 MP 카메라, 스크롤 휠 | [31] |
| OS | Android 13 기반 rabbitOS. "사실상 Android 앱"이라는 보도와 CEO의 반박 | [31] |
| AI | Large Action Model(LAM). Uber 같은 웹앱을 **클라우드에서 대신 조작** | [32] |
| 리뷰 | 가만히 둬도 8시간 만에 배터리 소진, 업데이트 후에도 하루를 못 버팀. 음식 주문에 1분, 확정까지 몇 분 | [32] |
| 보안 | 2024-06 하드코딩된 API 키(ElevenLabs, Azure, Yelp, Google Maps) 노출. 2024-07 기기에 대화·페어링 데이터가 삭제 불가하게 남는 문제 | [31] |
| 사용자 | 2024-09 CEO 발언 기준 하루 약 5,000명 사용 (사전 판매 10만 대 이상 대비), OTA 16회 | [33] |
| 이후 | 2025-09-08 rabbitOS 2 (카드 UI, 생성형 "creations") | [34] |
| 2026 | 2026-09 OS3 공개. Rabbit 클라우드에서 돌며 사용자의 컴퓨터를 로컬 에이전트로 조작. R1은 여러 접점 중 하나 (검색 요약) | [35] |

### 8.2 원인 분석 (분석)

1. **폰 앱과 구별되지 않았다.** 하드웨어가 폰보다 나은 점(센서, 착용성, 항상 켜짐)이 없었다. 저가 Android 칩에 앱을 올린 구조였다 [31].
2. **클라우드 에이전트의 지연과 신뢰성.** 웹앱을 원격으로 조작하는 LAM은 단계가 많다. 단계마다 실패 확률이 곱해진다 (11.3절 예제 6).
3. **idle 전력 관리 실패.** 아무것도 안 하는데 8시간 만에 배터리가 바닥났다 [32]. 펌웨어 엔지니어에게 이건 "suspend가 안 들어가는 버그"로 들린다. 실제로 OTA 패치로 크게 개선됐다는 보도가 있다 [32]. 출시 전 idle 전류 측정 하나로 잡을 수 있었던 문제다.
4. **보안 기본기.** 기기 이미지에 API 키를 넣으면 모든 기기가 같은 비밀을 갖는다. 기기별 credential과 서버 측 프록시가 기본이다.
5. **업데이트 속도는 좋았다.** OTA 16회 [33], OS 2, OS 3. 하지만 **소프트웨어로 고칠 수 없는 것(제품 존재 이유)** 은 업데이트로 안 고쳐졌다. 결국 회사는 소프트웨어 에이전트(OS3)로 무게를 옮겼다 [35].

---

## 9. AI 펜던트·녹음기 — Limitless, Bee, Friend, Plaud

같은 "목에 걸거나 옷에 다는 마이크"인데 결과가 갈렸다. 비교가 가장 교육적인 그룹이다.

| 제품 | 가격 · 구독 | 무엇을 하나 | AI 위치 | 결과 | 출처 |
|---|---|---|---|---|---|
| Limitless Pendant | $99, 구독 플랜 있음 | 대화 녹음 → 전사 → 요약·할 일 | 클라우드 (전사·요약) — 세부는 확인 필요 | 2025-12-05 Meta가 인수. 하드웨어 판매 중단, 기존 고객 1년 지원, 구독 무료 전환 | [36] |
| Bee | $49.99 + $19/월 | 손목 밴드(또는 클립). 대화를 듣고 할 일·리마인더 생성 | 클라우드. 오디오는 저장하지 않는다고 주장. on-device 처리 "작업 중" | 2025-07-22 Amazon 인수 발표. CES 2026에서 Amazon이 Actions·Daily Insights 등 새 기능 소개 (검색 요약) | [37][38] |
| Friend | $129 | 항상 듣는 "친구" 펜던트. 응답은 폰 앱 텍스트로 | 기기는 마이크 + BT, 응답은 폰 앱. LLM 위치는 확인 필요 | 응답 지연 7~10초, 연결 끊김, 맥락 망각 (Fortune 리뷰). 약 4,000대 활성화, 뉴욕 지하철 광고 약 $95만, 광고 훼손 등 반발 | [39][40] |
| Plaud NotePin | $159, 무료 월 300분 전사 + 유료 플랜 | 회의·통화 녹음 → 전사(112개 언어, 화자 구분) → 요약 | 녹음은 기기(64 GB), 전사·요약은 클라우드 | 2026-06 ARR $100M, 사용자 200만 명 이상, 170개국 이상 | [41][42] |

### 9.1 같은 클라우드 의존인데 왜 결과가 갈렸나 (분석)

- Plaud의 핵심 작업은 **회의가 끝난 뒤** 전사·요약을 받는 것이다. 지연이 10초든 1분이든 상관없다. 클라우드 의존이 약점이 아니다. 기기는 녹음만 잘 하면 된다. 그래서 연속 녹음 20시간, 대기 40일이라는 배터리 [41]가 가능하다. 마이크 + 저장 + 저전력이라는, 펌웨어가 가장 잘하는 일만 기기에 남겼다.
- Friend의 핵심 작업은 **실시간 대화**다. 7~10초 지연 [39]은 대화를 죽인다. 그리고 "항상 듣는 친구"는 주변 사람에게 설명하기 어렵다 (사회적 수용성). 지하철 광고 훼손은 그 반감이 공개적으로 드러난 사례다 [40].
- Limitless와 Bee는 기능이 통하는 것을 보여 줬지만, **플랫폼 회사가 같은 기능을 흡수**하는 쪽으로 끝났다. Limitless 공동창업자는 큰 회사들(OpenAI, Meta)과의 경쟁이 어려워졌다고 했다 [36].

말로 하면: **클라우드 의존 자체가 실패 원인이 아니다. 실시간성이 필요한 작업을 클라우드에 둔 것이 실패 원인이다.** 이 문장은 면접에서 그대로 쓸 수 있다.

---

## 10. OpenAI–io 기기 — 확인된 것만

확인된 것:

- io Products는 2024년 Jony Ive, Scott Cannon, Evans Hankey, Tang Tan이 세웠다 [44].
- OpenAI는 2025-05-21 io 인수를 발표했다. 가치 $6.5B(전액 주식)로 보도됐고, 2025-07-09 완료, io 직원 55명이 합류했다 [44].
- 상표 소송(iyO) 과정의 법원 선언문에서, io 측은 첫 제품이 "**not an in-ear device, nor a wearable device**"이고 출시·판매까지 "at least a year away"라고 했다 (2025-06 보도) [43]. Altman은 이전에 "fits in your pocket or sits on your desk"라고 묘사했다 [43].
- 2026-02 보도: OpenAI가 'io' 브랜드를 접고 첫 기기를 2027년으로 미뤘다 [45].
- 2026-09-29 DevDay에서 Altman은 "New hardware is very hard to do"라며 서두르지 않겠다는 취지로 말했다 (검색 요약) [46].

확인되지 않은 것 (미확인 보도 — 근거로 쓰지 않는다): 귀걸이형·이어버드형 여부, 맞춤 칩, 첫해 생산 대수, 가격. 2025년 선언문(웨어러블 아님)과 2026년 이어버드 보도는 서로 부딪힌다. 면접에서는 "공개된 법원 문서 기준으로는 웨어러블이 아니라고 했고, 이후 보도는 엇갈린다" 정도로만 말한다.

분석: OpenAI의 지연 자체가 하나의 데이터다. **세계 최고 수준의 모델 회사와 Apple 디자인 팀이 합쳐도 AI 전용 기기는 일정이 밀린다.** Hark의 1세대 일정(컨텍스트 파일 §2.4: 하드웨어 공개 전, 공고상 EVT 전후 추정)도 같은 압력 아래에 있다.


---

## 11. 데이터로 비교하기 — 배치, 배터리, 지연

### 11.1 기능 배치 행렬

각 제품의 대표 기능 다섯 개가 어느 계층에서 도는지 표로 만든다. 규칙이 중요하다. **대문자는 출처 문구로 확인된 칸, 소문자는 이 노트의 추정, `?`는 공개 정보 없음.** `?`를 억지로 채우지 않는 것이 이 표의 가치다.

```python
# placement.py — 기능별 실행 위치. D=기기 P=폰 L=로컬(기기 또는 폰, 구분 미공개) C=클라우드
# 대문자 = 출처 문구로 확인, 소문자 = 이 노트의 추정, ? = 공개 정보 없음, - = 해당 기능 없음
FEATURES = ["간단 명령", "일반 질의(LLM)", "센서·건강 ML", "받아쓰기·전사", "생성형 부가기능"]
M = {
 "Apple Watch S9+":   ["D", "c", "D", "?", "P"],   # [1] on-device Siri, double tap / [3] Workout Buddy
 "AirPods Pro 3":     ["p", "c", "P", "-", "P"],   # [4] Live Translation, HR 모델 = iPhone
 "Pixel Watch 5":     ["D", "?", "d", "?", "?"],   # [9] 오프라인 타이머·알람·운동
 "Galaxy Ring":       ["-", "-", "p", "-", "p"],   # [11] Energy Score는 Samsung Health 동기화 필요
 "Oura Ring 4":       ["-", "-", "d", "-", "c"],   # [14] Advisor = LLM
 "Ray-Ban Meta Gen2": ["?", "c", "-", "?", "L"],   # [18] 오프라인 번역
 "Ray-Ban Display":   ["?", "c", "l", "D", "?"],   # [20] 받아쓰기 = on-device ASR
 "Humane AI Pin":     ["?", "C", "?", "?", "C"],   # [26] GPT 등 클라우드 LLM
 "Rabbit R1":         ["?", "C", "-", "?", "C"],   # [32] LAM이 클라우드에서 웹앱 조작
 "Friend":            ["-", "c", "-", "?", "-"],   # [40] 폰 앱으로 텍스트 응답
 "Bee":               ["-", "-", "-", "C", "C"],   # [37] 클라우드 처리, on-device는 작업 중
 "Plaud NotePin":     ["-", "-", "-", "C", "C"],   # [41] 녹음은 로컬, 전사·요약은 클라우드
}
```

무엇을 확인하는 코드인가: 위치가 알려진 칸 중 클라우드(C/c)의 비율을 제품별로 세고, 결과 라벨 옆에 놓는다.

```python
# 예제 2: 기능 배치 행렬 → 제품별 "알려진 기능 중 클라우드 비율"과 결과 라벨 비교
from placement import FEATURES, M
from products import P
alias = {"Apple Watch S9+": "Apple Watch S9", "Ray-Ban Display": "Meta Ray-Ban Display"}

print("F1..F5 =", " / ".join(FEATURES))
print(f"{'product':<18}" + "".join(f"{'F'+str(i+1):>4}" for i in range(5)) + "  cloud%  outcome")
for name, row in M.items():
    known = [x for x in row if x not in "?-"]              # 위치가 알려진 기능만
    cloud = sum(x.upper() == "C" for x in known)
    pct = 100 * cloud / len(known) if known else float("nan")
    outcome = P.get(alias.get(name, name), {}).get("outcome", "-")
    print(f"{name:<18}" + "".join(f"{x:>4}" for x in row) + f"  {pct:5.0f}%  {outcome}")

# 출처로 확인된(대문자) 칸만 세어서 계층별 분포
from collections import Counter
c = Counter(x for row in M.values() for x in row if x.isupper())
print("\n확인된 칸의 계층 분포:", dict(sorted(c.items())))
```

```text
F1..F5 = 간단 명령 / 일반 질의(LLM) / 센서·건강 ML / 받아쓰기·전사 / 생성형 부가기능
product             F1  F2  F3  F4  F5  cloud%  outcome
Apple Watch S9+      D   c   D   ?   P     25%  success
AirPods Pro 3        p   c   P   -   P     25%  success
Pixel Watch 5        D   ?   d   ?   ?      0%  ongoing
Galaxy Ring          -   -   p   -   p      0%  mixed
Oura Ring 4          -   -   d   -   c     50%  success
Ray-Ban Meta Gen2    ?   c   -   ?   L     50%  success
Ray-Ban Display      ?   c   l   D   ?     33%  ongoing
Humane AI Pin        ?   C   ?   ?   C    100%  failed
Rabbit R1            ?   C   -   ?   C    100%  failed
Friend               -   c   -   ?   -    100%  failed
Bee                  -   -   -   C   C    100%  acquired
Plaud NotePin        -   -   -   C   C    100%  success

확인된 칸의 계층 분포: {'C': 8, 'D': 4, 'L': 1, 'P': 3}
```

```svg
<svg viewBox="0 0 680 414" xmlns="http://www.w3.org/2000/svg">
<text x="198.0" y="24" font-size="12" text-anchor="middle">간단 명령</text>
<text x="294.0" y="24" font-size="12" text-anchor="middle">일반 질의</text><text x="294.0" y="40" font-size="12" text-anchor="middle">(LLM)</text>
<text x="390.0" y="24" font-size="12" text-anchor="middle">센서·건강</text><text x="390.0" y="40" font-size="12" text-anchor="middle">ML</text>
<text x="486.0" y="24" font-size="12" text-anchor="middle">받아쓰기</text><text x="486.0" y="40" font-size="12" text-anchor="middle">·전사</text>
<text x="582.0" y="24" font-size="12" text-anchor="middle">생성형</text><text x="582.0" y="40" font-size="12" text-anchor="middle">부가기능</text>
<text x="142" y="72" font-size="12" text-anchor="end">Apple Watch S9+</text><rect x="152" y="58" width="92" height="20" rx="3" fill="#3f9a6b" fill-opacity="0.85"/><text x="198.0" y="72" font-size="12" text-anchor="middle">D</text><rect x="248" y="58" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.35"/><text x="294.0" y="72" font-size="12" text-anchor="middle">c</text><rect x="344" y="58" width="92" height="20" rx="3" fill="#3f9a6b" fill-opacity="0.85"/><text x="390.0" y="72" font-size="12" text-anchor="middle">D</text><rect x="440" y="58" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="486.0" y="72" font-size="12" text-anchor="middle">?</text><rect x="536" y="58" width="92" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.85"/><text x="582.0" y="72" font-size="12" text-anchor="middle">P</text>
<text x="142" y="96" font-size="12" text-anchor="end">AirPods Pro 3</text><rect x="152" y="82" width="92" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.35"/><text x="198.0" y="96" font-size="12" text-anchor="middle">p</text><rect x="248" y="82" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.35"/><text x="294.0" y="96" font-size="12" text-anchor="middle">c</text><rect x="344" y="82" width="92" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.85"/><text x="390.0" y="96" font-size="12" text-anchor="middle">P</text><rect x="440" y="82" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="486.0" y="96" font-size="12" text-anchor="middle">-</text><rect x="536" y="82" width="92" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.85"/><text x="582.0" y="96" font-size="12" text-anchor="middle">P</text>
<text x="142" y="120" font-size="12" text-anchor="end">Pixel Watch 5</text><rect x="152" y="106" width="92" height="20" rx="3" fill="#3f9a6b" fill-opacity="0.85"/><text x="198.0" y="120" font-size="12" text-anchor="middle">D</text><rect x="248" y="106" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="294.0" y="120" font-size="12" text-anchor="middle">?</text><rect x="344" y="106" width="92" height="20" rx="3" fill="#3f9a6b" fill-opacity="0.35"/><text x="390.0" y="120" font-size="12" text-anchor="middle">d</text><rect x="440" y="106" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="486.0" y="120" font-size="12" text-anchor="middle">?</text><rect x="536" y="106" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="582.0" y="120" font-size="12" text-anchor="middle">?</text>
<text x="142" y="144" font-size="12" text-anchor="end">Galaxy Ring</text><rect x="152" y="130" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="198.0" y="144" font-size="12" text-anchor="middle">-</text><rect x="248" y="130" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="294.0" y="144" font-size="12" text-anchor="middle">-</text><rect x="344" y="130" width="92" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.35"/><text x="390.0" y="144" font-size="12" text-anchor="middle">p</text><rect x="440" y="130" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="486.0" y="144" font-size="12" text-anchor="middle">-</text><rect x="536" y="130" width="92" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.35"/><text x="582.0" y="144" font-size="12" text-anchor="middle">p</text>
<text x="142" y="168" font-size="12" text-anchor="end">Oura Ring 4</text><rect x="152" y="154" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="198.0" y="168" font-size="12" text-anchor="middle">-</text><rect x="248" y="154" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="294.0" y="168" font-size="12" text-anchor="middle">-</text><rect x="344" y="154" width="92" height="20" rx="3" fill="#3f9a6b" fill-opacity="0.35"/><text x="390.0" y="168" font-size="12" text-anchor="middle">d</text><rect x="440" y="154" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="486.0" y="168" font-size="12" text-anchor="middle">-</text><rect x="536" y="154" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.35"/><text x="582.0" y="168" font-size="12" text-anchor="middle">c</text>
<text x="142" y="192" font-size="12" text-anchor="end">Ray-Ban Meta Gen2</text><rect x="152" y="178" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="198.0" y="192" font-size="12" text-anchor="middle">?</text><rect x="248" y="178" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.35"/><text x="294.0" y="192" font-size="12" text-anchor="middle">c</text><rect x="344" y="178" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="390.0" y="192" font-size="12" text-anchor="middle">-</text><rect x="440" y="178" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="486.0" y="192" font-size="12" text-anchor="middle">?</text><rect x="536" y="178" width="92" height="20" rx="3" fill="#e08a3c" fill-opacity="0.85"/><text x="582.0" y="192" font-size="12" text-anchor="middle">L</text>
<text x="142" y="216" font-size="12" text-anchor="end">Ray-Ban Display</text><rect x="152" y="202" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="198.0" y="216" font-size="12" text-anchor="middle">?</text><rect x="248" y="202" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.35"/><text x="294.0" y="216" font-size="12" text-anchor="middle">c</text><rect x="344" y="202" width="92" height="20" rx="3" fill="#e08a3c" fill-opacity="0.35"/><text x="390.0" y="216" font-size="12" text-anchor="middle">l</text><rect x="440" y="202" width="92" height="20" rx="3" fill="#3f9a6b" fill-opacity="0.85"/><text x="486.0" y="216" font-size="12" text-anchor="middle">D</text><rect x="536" y="202" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="582.0" y="216" font-size="12" text-anchor="middle">?</text>
<text x="142" y="240" font-size="12" text-anchor="end">Humane AI Pin</text><rect x="152" y="226" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="198.0" y="240" font-size="12" text-anchor="middle">?</text><rect x="248" y="226" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.85"/><text x="294.0" y="240" font-size="12" text-anchor="middle">C</text><rect x="344" y="226" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="390.0" y="240" font-size="12" text-anchor="middle">?</text><rect x="440" y="226" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="486.0" y="240" font-size="12" text-anchor="middle">?</text><rect x="536" y="226" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.85"/><text x="582.0" y="240" font-size="12" text-anchor="middle">C</text>
<text x="142" y="264" font-size="12" text-anchor="end">Rabbit R1</text><rect x="152" y="250" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="198.0" y="264" font-size="12" text-anchor="middle">?</text><rect x="248" y="250" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.85"/><text x="294.0" y="264" font-size="12" text-anchor="middle">C</text><rect x="344" y="250" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="390.0" y="264" font-size="12" text-anchor="middle">-</text><rect x="440" y="250" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="486.0" y="264" font-size="12" text-anchor="middle">?</text><rect x="536" y="250" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.85"/><text x="582.0" y="264" font-size="12" text-anchor="middle">C</text>
<text x="142" y="288" font-size="12" text-anchor="end">Friend</text><rect x="152" y="274" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="198.0" y="288" font-size="12" text-anchor="middle">-</text><rect x="248" y="274" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.35"/><text x="294.0" y="288" font-size="12" text-anchor="middle">c</text><rect x="344" y="274" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="390.0" y="288" font-size="12" text-anchor="middle">-</text><rect x="440" y="274" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="486.0" y="288" font-size="12" text-anchor="middle">?</text><rect x="536" y="274" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="582.0" y="288" font-size="12" text-anchor="middle">-</text>
<text x="142" y="312" font-size="12" text-anchor="end">Bee</text><rect x="152" y="298" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="198.0" y="312" font-size="12" text-anchor="middle">-</text><rect x="248" y="298" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="294.0" y="312" font-size="12" text-anchor="middle">-</text><rect x="344" y="298" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="390.0" y="312" font-size="12" text-anchor="middle">-</text><rect x="440" y="298" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.85"/><text x="486.0" y="312" font-size="12" text-anchor="middle">C</text><rect x="536" y="298" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.85"/><text x="582.0" y="312" font-size="12" text-anchor="middle">C</text>
<text x="142" y="336" font-size="12" text-anchor="end">Plaud NotePin</text><rect x="152" y="322" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="198.0" y="336" font-size="12" text-anchor="middle">-</text><rect x="248" y="322" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="294.0" y="336" font-size="12" text-anchor="middle">-</text><rect x="344" y="322" width="92" height="20" rx="3" fill="none" stroke="currentColor" stroke-opacity="0.25"/><text x="390.0" y="336" font-size="12" text-anchor="middle">-</text><rect x="440" y="322" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.85"/><text x="486.0" y="336" font-size="12" text-anchor="middle">C</text><rect x="536" y="322" width="92" height="20" rx="3" fill="#d0564a" fill-opacity="0.85"/><text x="582.0" y="336" font-size="12" text-anchor="middle">C</text>
<rect x="20" y="357" width="14" height="14" rx="2" fill="#3f9a6b" fill-opacity="0.85"/><text x="40" y="368" font-size="12">D 기기</text>
<rect x="120" y="357" width="14" height="14" rx="2" fill="#4a7bd0" fill-opacity="0.85"/><text x="140" y="368" font-size="12">P 폰</text>
<rect x="210" y="357" width="14" height="14" rx="2" fill="#e08a3c" fill-opacity="0.85"/><text x="230" y="368" font-size="12">L 로컬(기기/폰)</text>
<rect x="370" y="357" width="14" height="14" rx="2" fill="#d0564a" fill-opacity="0.85"/><text x="390" y="368" font-size="12">C 클라우드</text>
<text x="480" y="368" font-size="12">소문자·연한 색 = 추정</text>
<text x="20" y="390" font-size="12">? = 공개 정보 없음 · - = 해당 기능 없음 · 대문자 = 출처 문구로 확인</text>
</svg>
```
그림 3 — 기능 배치 행렬. 진한 색은 출처로 확인, 연한 색은 추정. 위쪽(시계·이어버드·반지·안경)은 초록(기기)과 파랑(폰)이 섞이고, 아래쪽(Pin·R1·펜던트)은 빨강(클라우드)이 대부분이다. 근거 [1][3][4][9][11][14][18][20][26][32][37][40][41].

출력에서 볼 것:

- 실패 3개(Humane, Rabbit, Friend)는 알려진 기능이 **100% 클라우드**다.
- 그런데 **Plaud도 100% 클라우드인데 성공**했다. 그래서 "클라우드 비율"만으로는 결과를 설명할 수 없다. 9.1절의 결론(실시간성이 필요한 기능이 클라우드에 있느냐)이 데이터에서도 보인다.
- 확인된 칸(대문자) 16개 중 8개가 C다. 기업들이 "클라우드에서 돈다"는 건 쉽게 밝히고, "기기에서 돈다"는 건 차별점일 때만 밝힌다(Apple Siri, Meta 받아쓰기). 공개 정보의 편향도 염두에 둔다.

### 11.2 배터리 — 폼팩터마다 다른 세계

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="330" x2="660" y2="330" stroke="currentColor"/><line x1="70" y1="30" x2="70" y2="330" stroke="currentColor"/>
<line x1="70" y1="330.0" x2="660" y2="330.0" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="334.0" font-size="12" text-anchor="end">3</text>
<line x1="70" y1="251.6" x2="660" y2="251.6" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="255.6" font-size="12" text-anchor="end">10</text>
<line x1="70" y1="194.5" x2="660" y2="194.5" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="198.5" font-size="12" text-anchor="end">24</text>
<line x1="70" y1="101.6" x2="660" y2="101.6" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="105.6" font-size="12" text-anchor="end">100</text>
<line x1="70" y1="67.8" x2="660" y2="67.8" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="71.8" font-size="12" text-anchor="end">168</text>
<text x="74" y="190.5" font-size="11" opacity="0.7">하루(24 h)</text>
<text x="74" y="63.8" font-size="11" opacity="0.7">일주일(168 h)</text>
<text x="119.2" y="348" font-size="12" text-anchor="middle">반지</text>
<text x="217.5" y="348" font-size="12" text-anchor="middle">시계</text>
<text x="315.8" y="348" font-size="12" text-anchor="middle">이어버드</text>
<text x="414.2" y="348" font-size="12" text-anchor="middle">안경</text>
<text x="512.5" y="348" font-size="12" text-anchor="middle">핀</text>
<text x="610.8" y="348" font-size="12" text-anchor="middle">펜던트·녹음기</text>
<text x="18" y="180.0" font-size="12" text-anchor="middle" transform="rotate(-90 18 180.0)">주장 배터리 시간 (h, log)</text>
<circle cx="92.0" cy="59.1" r="5" fill="#3f9a6b"/><text x="100.0" y="63.1" font-size="11">Oura Ring 4</text>
<circle cx="92.0" cy="67.8" r="5" fill="#e08a3c"/><text x="100.0" y="76.1" font-size="11">Galaxy Ring</text>
<circle cx="190.3" cy="161.3" r="5" fill="#4a7bd0"/><text x="198.3" y="165.3" font-size="11">Pixel Watch 5 (45mm)</text>
<circle cx="190.3" cy="180.0" r="5" fill="#3f9a6b"/><text x="198.3" y="184.0" font-size="11">Pixel Watch 4 (41mm)</text>
<circle cx="190.3" cy="194.5" r="5" fill="#3f9a6b"/><text x="198.3" y="198.5" font-size="11">Apple Watch S11</text>
<circle cx="190.3" cy="194.5" r="5" fill="#e08a3c"/><text x="198.3" y="211.5" font-size="11">Pixel Watch 1</text>
<circle cx="190.3" cy="213.3" r="5" fill="#3f9a6b"/><text x="198.3" y="224.5" font-size="11">Apple Watch S9</text>
<circle cx="288.7" cy="266.1" r="5" fill="#3f9a6b"/><text x="296.7" y="270.1" font-size="11">AirPods Pro 3 (ANC)</text>
<circle cx="387.0" cy="239.7" r="5" fill="#4a7bd0"/><text x="395.0" y="243.7" font-size="11">Ray-Ban Meta Audio</text>
<circle cx="387.0" cy="266.1" r="5" fill="#3f9a6b"/><text x="395.0" y="270.1" font-size="11">Ray-Ban Meta Gen 2</text>
<circle cx="387.0" cy="284.8" r="5" fill="#4a7bd0"/><text x="395.0" y="288.8" font-size="11">Ray-Ban Display</text>
<circle cx="485.3" cy="303.6" r="5" fill="#d0564a"/><text x="493.3" y="307.6" font-size="11">Humane AI Pin (단독)</text>
<circle cx="601.7" cy="206.4" r="5" fill="#3f9a6b"/><text x="672" y="200.4" font-size="11" text-anchor="end">Plaud NotePin (연속 녹음)</text>
<circle cx="90" cy="402" r="5" fill="#3f9a6b"/><text x="100" y="406" font-size="12">성공(판단)</text>
<circle cx="210" cy="402" r="5" fill="#e08a3c"/><text x="220" y="406" font-size="12">엇갈림</text>
<circle cx="330" cy="402" r="5" fill="#4a7bd0"/><text x="340" y="406" font-size="12">진행 중</text>
<circle cx="450" cy="402" r="5" fill="#d0564a"/><text x="460" y="406" font-size="12">실패</text>
</svg>
```
그림 4 — 폼팩터별 주장 배터리 시간 (로그 축). 반지는 일주일, 시계는 하루~이틀, 안경·이어버드는 반나절, Humane Pin은 4~5시간이다. 조건이 제각각이라는 점(1.4절)을 기억할 것. 출처 [1][2][4][7][8][9][10][13][18][19][22][26][41].

같은 "웨어러블"이라도 전력 세계가 100배 차이 난다. 배터리 용량(mAh)이 공개된 제품만 골라 평균 전류를 역산해 보자.

무엇을 확인하는 코드인가: `평균 전류 = 용량 / 주장 시간`으로 각 기기의 평균 전류 예산을 구하고, 거꾸로 가정 기기의 예산에서 "무거운 활성 구간"이 하루 몇 분 허용되는지 계산한다.

```
I_avg = Q / t            (mA = mAh / h)
duty_max = (I_avg − I_idle) / (I_active − I_idle)
```

말로 하면: 평균 전류는 배터리 용량을 목표 시간으로 나눈 값이다. 바닥 전류(always-on)를 빼고 남은 예산을, 활성 구간이 바닥 전류보다 더 쓰는 양으로 나누면 허용 duty가 나온다. I1에서 한 top-down 예산과 같은 식이다.

손으로 먼저: Pixel Watch 1세대 294 mAh / 24 h = 12.25 mA. Galaxy Ring 큰 사이즈 23.5 mAh / 168 h ≈ 0.14 mA. 두 기기의 평균 전류가 약 90배 차이다.

```python
# 예제 3: 공개된 mAh와 배터리 시간 주장으로 "평균 전류"를 역산한다 (derived)
cases = {   # 이름: (용량 mAh, 주장 시간 h, 출처)
    "Pixel Watch (1st)":   (294, 24, "[7]"),
    "Pixel Watch 5 41mm":  (332, 30, "[9]"),
    "Pixel Watch 5 45mm":  (465, 40, "[9]"),
    "Galaxy Ring (small)": (18, 7 * 24, "[10]"),
    "Galaxy Ring (large)": (23.5, 7 * 24, "[10]"),
}
for name, (mah, h, ref) in cases.items():
    i_avg = mah / h                         # mA. 공칭 3.8 V 가정으로 전력도 본다
    print(f"{name:<20} {mah:6.1f} mAh / {h:4.0f} h = {i_avg:7.3f} mA "
          f"≈ {i_avg * 3.8:6.2f} mW (3.8 V 가정) {ref}")

# 거꾸로: Hark 같은 기기를 가정 — 400 mAh, 목표 16 h (둘 다 설명용 가정)
budget = 400 / 16
print(f"\n가정 기기 평균 전류 예산 = {budget:.1f} mA")
for active_ma in (150, 300, 600):           # SoC+모뎀 활성 구간 전류 (가정)
    idle_ma = 3.0                           # always-on MCU+센서 바닥 전류 (가정)
    duty = (budget - idle_ma) / (active_ma - idle_ma)
    print(f"  활성 {active_ma:3d} mA일 때 허용 duty = {100 * duty:5.1f}% "
          f"→ 하루 16 h 중 {duty * 16 * 60:5.0f}분")
```

```text
Pixel Watch (1st)     294.0 mAh /   24 h =  12.250 mA ≈  46.55 mW (3.8 V 가정) [7]
Pixel Watch 5 41mm    332.0 mAh /   30 h =  11.067 mA ≈  42.05 mW (3.8 V 가정) [9]
Pixel Watch 5 45mm    465.0 mAh /   40 h =  11.625 mA ≈  44.17 mW (3.8 V 가정) [9]
Galaxy Ring (small)    18.0 mAh /  168 h =   0.107 mA ≈   0.41 mW (3.8 V 가정) [10]
Galaxy Ring (large)    23.5 mAh /  168 h =   0.140 mA ≈   0.53 mW (3.8 V 가정) [10]

가정 기기 평균 전류 예산 = 25.0 mA
  활성 150 mA일 때 허용 duty =  15.0% → 하루 16 h 중   144분
  활성 300 mA일 때 허용 duty =   7.4% → 하루 16 h 중    71분
  활성 600 mA일 때 허용 duty =   3.7% → 하루 16 h 중    35분
```

출력에서 볼 것:

- 시계 3종은 세대가 바뀌어도 평균 전류가 11~12 mA에 머문다. 배터리 시간을 늘린 건 주로 **용량 증가**(294 → 465 mAh)와 co-processor 활용이다 [7][9]. 화면 달린 시계의 "바닥"이 대략 이 수준이라는 감을 잡는다.
- 반지는 0.1 mA대다. 이 예산에서는 무선 스트리밍, 마이크 상시 녹음, LLM은 들어갈 자리가 없다. 반지가 **센서 + 배치 동기화**만 하는 이유다.
- 가정 기기(400 mAh, 16 h)에서 SoC + 모뎀이 300 mA로 활성화되면 하루 71분만 허용된다. 클라우드 대화가 한 번에 30초라면 하루 140번 남짓이다. 넉넉해 보이지만, **wake word 오인식으로 SoC가 헛되이 깨어나는 것**과 셀룰러 재연결 비용이 이 예산을 빠르게 먹는다 (N4의 false wake 비용).

Don 경험과 연결: SSD에서 PS3/PS4 저전력 상태 진입·복귀 지연과 평균 전력을 맞췄던 일과 같다. 차이는 웨어러블에서는 "복귀"가 사용자의 말 한마디로 시작된다는 점이다.

### 11.3 지연과 신뢰성 — 단계가 많을수록 곱해진다

무엇을 확인하는 코드인가: 음성 한 턴(말 끝 → 첫 응답)을 세 구조로 모델링해 p50·p90 지연과 단계별 성공 확률의 곱(end-to-end 성공률)을 비교한다. **모든 단계 숫자는 설명용 가정**이다 (L4의 실측과 I1의 예산을 참고해 정한 대략값). 실제 제품의 측정값이 아니다.

```
T_total = Σ T_i         (각 단계 지연은 lognormal로 가정)
P_success = Π p_i       (단계가 독립이라고 가정)
```

말로 하면: 단계 지연은 더해지고, 성공 확률은 곱해진다. 단계가 99%씩 5개면 95%다. 사용자는 20번에 한 번 실패를 겪는다.

```python
# 예제 6: 음성 한 턴(말 끝 → 첫 응답)의 지연·성공률을 구조별로 비교 — 숫자는 모두 설명용 가정
import numpy as np
rng = np.random.default_rng(1)
N = 20_000
def ln(med, spread):                      # 중앙값 med(ms), 꼬리 두께 spread 인 lognormal 표본
    return med * np.exp(spread * rng.standard_normal(N))

arch = {   # 단계: (중앙값 ms, spread, 단계 성공 확률)
  "A 클라우드 직결(LTE)":  {"endpoint": (500, .2, 1), "LTE 업링크+RTT": (250, .8, .97),
                          "클라우드 ASR+LLM 첫 토큰": (900, .5, .99), "TTS 첫 음성": (300, .3, 1)},
  "B 폰 경유(BT→폰→클라우드)": {"endpoint": (500, .2, 1), "BT 오디오 전송": (120, .4, .98),
                          "폰→클라우드 RTT": (120, .6, .99), "클라우드 ASR+LLM 첫 토큰": (900, .5, .99),
                          "폰→기기 텍스트/TTS": (350, .3, 1)},
  "C 하이브리드(간단 명령은 기기)": {"endpoint": (300, .2, 1), "기기 KWS/의도 분류": (60, .2, .995),
                          "기기 응답(톤·햅틱)": (50, .1, 1)},
}
for name, st in arch.items():
    t = sum(ln(m, s) for m, s, _ in st.values())
    ok = np.prod([p for *_, p in st.values()])
    p50, p90 = np.percentile(t, [50, 90])
    print(f"{name:<24} p50 {p50:6.0f} ms  p90 {p90:6.0f} ms  성공률 {ok:6.1%}")
```

```text
A 클라우드 직결(LTE)           p50   2057 ms  p90   3001 ms  성공률  96.0%
B 폰 경유(BT→폰→클라우드)        p50   2062 ms  p90   2887 ms  성공률  96.0%
C 하이브리드(간단 명령은 기기)       p50    411 ms  p90    501 ms  성공률  99.5%
```

출력에서 볼 것:

- A(클라우드 직결)와 B(폰 경유)는 지연이 거의 같다. **BT 한 단계가 추가돼도 지배적인 건 클라우드 ASR + LLM 첫 토큰**이다. "폰을 거치면 느리다"는 직관은 대체로 틀린다. 오히려 폰이 Wi-Fi에 있으면 LTE보다 RTT가 안정적일 수 있다.
- C(간단 명령을 기기에서)는 p90이 0.5초 안팎이고 실패율도 낮다. 사용자가 하루에 가장 자주 하는 말("타이머", "볼륨", "다음 곡")이 이 경로를 타면 체감 품질이 확 바뀐다. Apple Watch S9 [1], Pixel Watch 5 오프라인 Gemini [9]가 이 방향이다.
- 같은 모델로 Rabbit LAM 같은 "웹앱을 원격 조작하는" 에이전트를 생각해 보자. 단계가 10개를 넘고 단계마다 외부 사이트 상태에 좌우된다. 리뷰의 "음식 주문 1분, 확정까지 몇 분" [32]은 이 구조의 자연스러운 결과다.

함정: 이 모델은 단계가 독립이라고 가정한다. 실제로는 망이 나쁘면 여러 단계가 같이 느려진다(상관). 그래서 실제 p90은 이 모델보다 나쁘다. 실측은 L4 방식으로 한다.

---

## 12. 교차 교훈 — 일곱 가지

### 12.1 교훈 표

| 교훈 | 실패 쪽 사례 | 성공 쪽 사례 | 펌웨어·AI 엔지니어의 대응 |
|---|---|---|---|
| 1. 실시간 기능을 클라우드에 두지 마라 | AI Pin (모든 질의 클라우드) [26], R1 LAM [32], Friend 7~10초 [39] | Watch 기기 Siri [1], Display 기기 받아쓰기 [20], Plaud (실시간 아님) [41] | 간단 명령·wake·VAD·받아쓰기는 기기. 클라우드는 지연 허용 작업 또는 스트리밍 응답 |
| 2. 배터리·열은 아키텍처가 결정한다 | AI Pin 과열 [27], 4~5시간 [26], R1 idle 소진 [32] | Ray-Ban Gen 2 8시간 [18], Oura 8일 [13] | 평균 전류 예산부터 (I1), 열 RC 모델·governor, idle 전류 회귀 테스트 |
| 3. 범용 비서보다 명확한 첫 용도 | AI Pin, R1, Friend | Oura(수면), Plaud(회의록), Ray-Ban(카메라·오디오) | 첫 기능의 acceptance 지표(정확도·지연·전력)를 숫자로 정의 |
| 4. 폰과 싸우지 말고 폰을 써라 | AI Pin (전화 기능 재구현) [27], R1 ("앱이면 된다") [31] | AirPods 번역·심박 모델은 iPhone에서 [4], Ray-Ban은 폰 경유 [20] | BT 링크 설계, 폰 앱과의 역할 분담, 폰 없을 때의 degrade 모드 |
| 5. 프라이버시는 하드웨어로 강제 | Friend 반발 [40], R1 데이터 삭제 불가 [31] | Meta LED + 조도 센서·훼손 탐지 [24], Humane Trust Light [25] | 표시등 인터락, 저장 직전 게이트(H6), 기기별 credential |
| 6. 가격·구독은 지속 가치와 맞아야 | AI Pin 2년 $1,275 (예제 1) | Oura 구독 + 유료 멤버 500만 [16], Ray-Ban 구독 없음 | 서버비를 줄이는 on-device 처리 = 구독 의존도를 낮추는 기술 |
| 7. 출시 후 업데이트 속도 | Humane은 개선 전에 종료 [29] | R1의 OTA 16회 [33]는 좋은 예이지만 존재 이유는 못 고침 | A/B OTA, 모델 OTA, 현장 텔레메트리로 회귀 감지 |

### 12.2 요인 점수와 근거 — 주관 평가임을 밝힌다

아래 점수(1~5)는 **이 노트의 주관 판단**이다. 근거를 한 줄씩 붙여 둔다. 점수는 결과를 이미 알고 매긴 것이므로 **사후 확증 편향(hindsight bias)**이 들어 있다. 12.4절에서 이 점을 다시 다룬다.

| 제품 | 용도 | 지연 | 배터리·열 | 폰 통합 | 프라이버시·수용 | 가격·구독 | 업데이트 | 한 줄 근거 |
|---|---|---|---|---|---|---|---|---|
| Apple Watch | 5 | 4 | 4 | 5 | 4 | 3 | 4 | 건강·운동이라는 명확한 용도, 기기 Siri [1], iPhone 계층 [3] |
| Ray-Ban Meta | 4 | 3 | 3 | 4 | 3 | 4 | 4 | 안경·카메라 기본 가치, 폰 의존 로딩 지연 [20], 세대별 배터리 개선 [18] |
| Oura Ring | 5 | 4 | 5 | 4 | 5 | 3 | 4 | 수면 한 가지, 8일 [13], 구독 필요 [13] |
| Plaud | 5 | 3 | 4 | 4 | 3 | 4 | 4 | 회의록, 실시간 불필요, 20시간 녹음 [41] |
| Galaxy Ring | 4 | 4 | 5 | 3 | 5 | 3 | 3 | 구독 없음 [10], Samsung Health 의존 [11] |
| Humane Pin | 2 | 1 | 1 | 1 | 2 | 1 | 2 | 과열·지연 [27], 폰 대체 시도, $699 + $24/월 [25] |
| Rabbit R1 | 2 | 1 | 2 | 1 | 3 | 3 | 4 | LAM 지연 [32], 앱과 구별 안 됨 [31], OTA 빈번 [33] |
| Friend | 2 | 1 | 3 | 3 | 1 | 3 | 2 | 7~10초 지연 [39], 지하철 광고 반발 [40] |

### 12.3 코드로 확인 — 점수 합계와 요인별 격차 (예제 4)

무엇을 확인하는 코드인가: 점수 표를 모듈로 만들고, 동일 가중 평균으로 순위를 매긴 뒤, "성공 그룹 평균 − 실패 그룹 평균"을 요인별로 계산한다.

```python
# scores.py — 요인 점수(1~5). 전부 이 노트의 주관 평가다. 근거는 본문 12.2 표
F = ["use_case", "latency", "power_thermal", "phone_integ", "social_privacy", "price_sub", "update_vel"]
S = {  # 제품: ([7개 점수], 결과)
    "Apple Watch":  ([5, 4, 4, 5, 4, 3, 4], "success"),
    "Ray-Ban Meta": ([4, 3, 3, 4, 3, 4, 4], "success"),
    "Oura Ring":    ([5, 4, 5, 4, 5, 3, 4], "success"),
    "Plaud":        ([5, 3, 4, 4, 3, 4, 4], "success"),
    "Galaxy Ring":  ([4, 4, 5, 3, 5, 3, 3], "mixed"),
    "Humane Pin":   ([2, 1, 1, 1, 2, 1, 2], "failed"),
    "Rabbit R1":    ([2, 1, 2, 1, 3, 3, 4], "failed"),
    "Friend":       ([2, 1, 3, 3, 1, 3, 2], "failed"),
}
```

```python
# 예제 4: "왜 성공/실패했나" 요인 점수 — 동일 가중 합계와 요인별 격차
import numpy as np
from scores import F, S

X = np.array([v[0] for v in S.values()], dtype=float)
w = np.ones(len(F)) / len(F)                       # 동일 가중
score = X @ w
for (name, (_, out)), s in sorted(zip(S.items(), score), key=lambda t: -t[1]):
    print(f"{name:<13} {s:4.2f}  {out}")
succ = np.array([o == "success" for _, o in S.values()])
fail = np.array([o == "failed" for _, o in S.values()])
gap = X[succ].mean(0) - X[fail].mean(0)
print("\n요인별 (성공 평균 - 실패 평균):")
for f, g in sorted(zip(F, gap), key=lambda t: -t[1]):
    print(f"  {f:<15} {g:+.2f}")
```

```text
Oura Ring     4.29  success
Apple Watch   4.14  success
Plaud         3.86  success
Galaxy Ring   3.86  mixed
Ray-Ban Meta  3.57  success
Rabbit R1     2.29  failed
Friend        2.14  failed
Humane Pin    1.43  failed

요인별 (성공 평균 - 실패 평균):
  use_case        +2.75
  phone_integ     +2.58
  latency         +2.50
  power_thermal   +2.00
  social_privacy  +1.75
  update_vel      +1.33
  price_sub       +1.17
```

```svg
<svg viewBox="0 0 680 316" xmlns="http://www.w3.org/2000/svg">
<line x1="200" y1="20" x2="200" y2="254" stroke="currentColor" stroke-opacity="0.6"/>
<text x="200" y="270" font-size="12" text-anchor="middle">+0</text>
<line x1="350" y1="20" x2="350" y2="254" stroke="currentColor" stroke-opacity="0.15"/>
<text x="350" y="270" font-size="12" text-anchor="middle">+1</text>
<line x1="500" y1="20" x2="500" y2="254" stroke="currentColor" stroke-opacity="0.15"/>
<text x="500" y="270" font-size="12" text-anchor="middle">+2</text>
<line x1="650" y1="20" x2="650" y2="254" stroke="currentColor" stroke-opacity="0.15"/>
<text x="650" y="270" font-size="12" text-anchor="middle">+3</text>
<text x="192" y="45" font-size="12" text-anchor="end">명확한 용도</text>
<rect x="200" y="32" width="412.5" height="20" rx="2" fill="#3f9a6b"/>
<text x="618.5" y="46" font-size="12">+2.75</text>
<text x="192" y="77" font-size="12" text-anchor="end">폰 통합</text>
<rect x="200" y="64" width="387.5" height="20" rx="2" fill="#3f9a6b"/>
<text x="593.5" y="78" font-size="12">+2.58</text>
<text x="192" y="109" font-size="12" text-anchor="end">지연·신뢰성</text>
<rect x="200" y="96" width="375.0" height="20" rx="2" fill="#3f9a6b"/>
<text x="581.0" y="110" font-size="12">+2.50</text>
<text x="192" y="141" font-size="12" text-anchor="end">배터리·열</text>
<rect x="200" y="128" width="300.0" height="20" rx="2" fill="#888"/>
<text x="506.0" y="142" font-size="12">+2.00</text>
<text x="192" y="173" font-size="12" text-anchor="end">프라이버시·사회적 수용</text>
<rect x="200" y="160" width="262.5" height="20" rx="2" fill="#888"/>
<text x="468.5" y="174" font-size="12">+1.75</text>
<text x="192" y="205" font-size="12" text-anchor="end">업데이트 속도</text>
<rect x="200" y="192" width="200.0" height="20" rx="2" fill="#888"/>
<text x="406.0" y="206" font-size="12">+1.33</text>
<text x="192" y="237" font-size="12" text-anchor="end">가격·구독</text>
<rect x="200" y="224" width="175.0" height="20" rx="2" fill="#888"/>
<text x="381.0" y="238" font-size="12">+1.17</text>
<text x="200" y="290" font-size="12">성공 그룹 평균 − 실패 그룹 평균 (주관 점수 1~5)</text>
<rect x="20" y="296" width="12" height="12" fill="#3f9a6b"/><text x="38" y="306" font-size="12">이 요인 하나만으로도 두 그룹이 갈림 (예제 5)</text>
</svg>
```
그림 5 — 요인별 격차 (성공 평균 − 실패 평균). 초록은 그 요인 하나만으로도 성공 그룹과 실패 그룹이 완전히 갈리는 요인(예제 5). 주관 점수에서 나온 그림이다.

출력에서 볼 것: 격차가 큰 순서는 **용도(+2.75) → 폰 통합(+2.58) → 지연(+2.50) → 배터리·열(+2.00)**이다. 가격·구독(+1.17)은 가장 작다. 즉 "비싸서 망했다"보다 "쓸 이유가 약했고, 느렸고, 폰과 싸웠다"가 더 큰 설명이다. 이건 이 노트의 점수에서 나온 결론이니, 면접에서는 "제 나름의 정리로는"이라고 앞에 붙인다.

### 12.4 민감도 — 가중치와 점수를 흔들어도 결론이 남나 (예제 5)

무엇을 확인하는 코드인가: (1) 무작위 가중치 1만 세트, (2) 요인 하나만 쓰는 경우, (3) 모든 점수를 −1/0/+1로 무작위로 흔든 경우에 "성공 그룹 최저점 > 실패 그룹 최고점"이 유지되는지 센다.

```python
# 예제 5: 민감도 — 가중치를 마구 바꿔도 "성공 > 실패" 순서가 유지되나? 점수 ±1 흔들기도
import numpy as np
from scores import F, S
X = np.array([v[0] for v in S.values()], dtype=float)
out = [o for _, o in S.values()]
succ = np.array([o == "success" for o in out]); fail = np.array([o == "failed" for o in out])
rng = np.random.default_rng(0)

def separated(Xm, w):                      # 성공 그룹 최저점 > 실패 그룹 최고점?
    s = Xm @ w
    return s[succ].min() > s[fail].max()

W = rng.dirichlet(np.ones(len(F)), size=10_000)          # 무작위 가중 1만 세트
print(f"무작위 가중치: 분리 유지 {np.mean([separated(X, w) for w in W]):.1%}")

for i, f in enumerate(F):                  # 요인 하나만 남기면?
    w = np.zeros(len(F)); w[i] = 1
    print(f"  {f:<15}만 볼 때 분리: {separated(X, w)}")

noisy = [separated(np.clip(X + rng.integers(-1, 2, X.shape), 1, 5), np.ones(len(F)) / len(F))
         for _ in range(10_000)]           # 모든 점수를 -1/0/+1로 무작위로 흔든다
print(f"점수 ±1 노이즈(동일 가중): 분리 유지 {np.mean(noisy):.1%}")
```

```text
무작위 가중치: 분리 유지 100.0%
  use_case       만 볼 때 분리: True
  latency        만 볼 때 분리: True
  power_thermal  만 볼 때 분리: False
  phone_integ    만 볼 때 분리: True
  social_privacy 만 볼 때 분리: False
  price_sub      만 볼 때 분리: False
  update_vel     만 볼 때 분리: False
점수 ±1 노이즈(동일 가중): 분리 유지 99.6%
```

출력에서 볼 것과 해석:

- 가중치를 어떻게 줘도 100% 분리된다. 점수에 ±1 노이즈를 줘도 99.6% 유지된다. **결론이 가중치 선택에 민감하지 않다**는 뜻이다.
- 하지만 이걸 "모델이 맞다"로 읽으면 안 된다. 점수 자체를 결과를 알고 매겼기 때문에 분리가 잘 되는 건 당연하다. 이 분석이 말해 주는 건 **"내 판단 안에서 어떤 요인이 결정적이냐"** 다. 용도·지연·폰 통합은 각각 단독으로도 두 그룹을 가른다. 배터리·열, 프라이버시, 가격, 업데이트는 단독으로는 못 가른다.
- 펌웨어 비유: 이건 테스트 벡터에 맞춰 튜닝한 펌웨어의 "통과율"과 같다(A4의 overfitting). 새 제품(Hark, Muse Charm, OpenAI 기기)이 나오면 **출시 전에 점수를 먼저 적어 두고** 결과와 비교해야 진짜 검증이 된다. 17절의 연습문제가 그것이다.

---

## 13. Hark에 주는 시사점 — 분석

이 절 전체가 **분석**이다. Hark에 대해 확인된 사실은 컨텍스트 파일 §2의 내용뿐이다.

### 13.1 확인된 Hark 사실 (컨텍스트 파일 기준)

- "proactive, multimodal, persistent memory" 개인 AI와 전용 하드웨어를 수직 통합으로 만드는 AI 랩. 목표는 "universal interface between humans and machines".
- 하드웨어: "a family of AI devices, for yourself and for the home". 공고상 1세대 기기는 **Cellular, Wi-Fi, BT, GNSS, NFC, UWB**를 담는다. Qualcomm 칩셋, Android 기반 메인 앱. On-Device AI 공고에 Hexagon DSP, Ambiq급 MCU, INT8/INT4, speech 추론.
- 모델: speech·memory 중심 multimodal. 2026-08-05 Handoff(웹 에이전트) technical preview.
- 경쟁: OpenAI(io), Meta Ray-Ban, Apple/Google 폰 에이전트. Humane·Rabbit 실패 선례가 투자 기사에서 직접 언급됐다.
- 2026-10-04 검색 기준 Hark의 하드웨어 발표는 찾지 못했다.

### 13.2 포지셔닝 스케치

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<line x1="90" y1="360" x2="640" y2="360" stroke="currentColor"/><line x1="90" y1="40" x2="90" y2="360" stroke="currentColor"/>
<line x1="365.0" y1="40" x2="365.0" y2="360" stroke="currentColor" stroke-opacity="0.2" stroke-dasharray="4 4"/>
<line x1="90" y1="200.0" x2="640" y2="200.0" stroke="currentColor" stroke-opacity="0.2" stroke-dasharray="4 4"/>
<text x="90" y="380" font-size="12">← 폰 대체 (단독 동작)</text>
<text x="640" y="380" font-size="12" text-anchor="end">폰 보완 (폰과 협업) →</text>
<text x="80" y="46" font-size="12" text-anchor="end">범용</text><text x="80" y="62" font-size="12" text-anchor="end">비서</text>
<text x="80" y="346" font-size="12" text-anchor="end">좁고</text><text x="80" y="362" font-size="12" text-anchor="end">명확</text>
<ellipse cx="238.5" cy="155.2" rx="78" ry="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="5 4"/>
<text x="238.5" y="151.2" font-size="12" text-anchor="middle">Hark 1세대 — 현재 단서로 본</text>
<text x="238.5" y="167.2" font-size="12" text-anchor="middle">출발 위치 (분석)</text>
<line x1="298.5" y1="181.2" x2="376.0" y2="219.20000000000002" stroke="#3f9a6b" stroke-width="2"/>
<polygon points="376.0,219.20000000000002 364.0,216.20000000000002 371.0,208.20000000000002" fill="#3f9a6b"/>
<text x="382.0" y="233.20000000000002" font-size="12">권장 이동: 명확한 첫 용도 + 폰 협업 (분석)</text>
<circle cx="145.0" cy="78.4" r="6" fill="#d0564a"/>
<text x="154.0" y="82.4" font-size="12" text-anchor="start">Humane AI Pin</text>
<circle cx="211.0" cy="110.4" r="6" fill="#d0564a"/>
<text x="220.0" y="114.4" font-size="12" text-anchor="start">Rabbit R1</text>
<circle cx="431.0" cy="136.0" r="6" fill="#d0564a"/>
<text x="440.0" y="140.0" font-size="12" text-anchor="start">Friend</text>
<circle cx="299.0" cy="91.2" r="6" fill="#4a7bd0"/>
<text x="308.0" y="95.2" font-size="12" text-anchor="start">Muse Charm (미출시)</text>
<circle cx="453.0" cy="200.0" r="6" fill="#3f9a6b"/>
<text x="462.0" y="204.0" font-size="12" text-anchor="start">Ray-Ban Meta</text>
<circle cx="541.0" cy="251.2" r="6" fill="#3f9a6b"/>
<text x="532.0" y="255.2" font-size="12" text-anchor="end">Apple Watch</text>
<circle cx="497.0" cy="315.2" r="6" fill="#3f9a6b"/>
<text x="506.0" y="319.2" font-size="12" text-anchor="start">Plaud</text>
<circle cx="585.0" cy="328.0" r="6" fill="#3f9a6b"/>
<text x="576.0" y="332.0" font-size="12" text-anchor="end">Oura</text>
<circle cx="409.0" cy="276.8" r="6" fill="#e08a3c"/>
<text x="418.0" y="280.8" font-size="12" text-anchor="start">Bee·Limitless</text>
<text x="255.0" y="59.2" font-size="12" opacity="0.8">OpenAI 기기: 형태 미공개 → 표시 안 함</text>
<text x="90" y="404" font-size="12">정성적 배치 — 이 노트의 판단. 빨강 실패 · 초록 성공 · 주황 인수 · 파랑 미출시</text>
</svg>
```
그림 6 — 정성적 포지셔닝 (이 노트의 판단). 가로축은 폰 대체(단독) ↔ 폰 보완, 세로축은 범용 비서 ↔ 좁고 명확한 용도. 실패한 제품은 왼쪽 위에 몰려 있다. Hark의 공개 단서(셀룰러 포함 무선 전부, "universal interface", "proactive" 비서)는 출발점을 왼쪽 위 근처로 보이게 한다. 화살표는 이 노트가 권하는 이동 방향이다. OpenAI 기기는 형태가 공개되지 않아 표시하지 않았다.

이 그림이 말하는 것: Hark의 야심(범용, 단독 동작 가능한 무선)은 Humane과 같은 사분면에 있다. 이건 나쁜 게 아니다. **같은 사분면에서 성공하려면 Humane이 무너진 지점(열·배터리·지연·용도)을 엔지니어링으로 막아야 한다**는 뜻이다. 셀룰러가 "있다"와 "기본 경로로 쓴다"는 다르다. 셀룰러는 폰이 없을 때의 fallback으로 두고, 평소에는 폰·Wi-Fi 경로를 쓰는 설계가 가능하다.

### 13.3 Hark 같은 기기에서 위험과 대응 (분석)

| 위험 | 선례 | 펌웨어·AI 대응 | 측정 지표 |
|---|---|---|---|
| 클라우드 왕복 지연으로 대화가 끊김 | AI Pin [27], Friend [39] | 간단 의도는 기기 NPU·DSP에서 처리, LLM 응답은 streaming + 첫 음절 빠르게(L4), 턴 시작 즉시 로컬 피드백(톤·햅틱) | 말 끝 → 첫 소리 p90, 오프라인 처리 비율 |
| 셀룰러·SoC 상시 활성으로 배터리·열 붕괴 | AI Pin [26][27] | always-on MCU cascade(VAD → KWS → SoC wake), 셀룰러는 fallback, 무거운 작업 duty·열 governor (예제 7, C) | 평균 전류(mA), idle 전류 회귀, 표면 온도 vs 시간 |
| false wake로 SoC가 헛되이 깨어남 | (N4 주제) | KWS 2단 검증, 오인식 시 SoC 깨우기 전에 MCU에서 컷 | false accept/시간, wake당 에너지(mJ) |
| "폰으로 하면 되잖아" | R1 [31] | 폰이 못 하는 것: 항상 켜진 센서·음성, 손을 안 쓰는 즉시 반응, 기억(memory) | 하루 사용 횟수, 폰 꺼내지 않은 상호작용 비율 |
| 주변인 프라이버시 반발 | Friend [40], Meta LED 이슈 [24] | 녹음·카메라 상태 표시등의 HW 인터락, 저장 직전 동의 게이트(H6), on-device 요약 후 원본 폐기 | 표시등 우회 불가 시험, 원본 오디오 보존 시간 |
| 출시 후 품질 회귀 | R1 idle 배터리 [32] | A/B OTA(모델·FW 분리), 필드 텔레메트리, 기기 farm 회귀 | OTA 주기, 회귀 탐지 시간 |
| 작은 배터리 안전 | Galaxy Ring 팽창 [12], AI Pin 케이스 리콜 [28] | fuel gauge SoH 감시, 이상 시 충전 제한·경고, 충전 중 열 감시 | SoH 추세, 충전 중 최고 온도 |

### 13.4 기능 배치 제안 — Hark 같은 기기라면 (분석)

| 기능 | 제안 위치 | 이유 | 선례 |
|---|---|---|---|
| VAD · wake word · 착용 감지 · IMU 제스처 | always-on MCU (또는 DSP) | µW~mW 예산, 지연 수십 ms | Watch double tap [1], Pixel co-processor [8] |
| 간단 명령 (타이머, 볼륨, 메모 시작) | SoC NPU/DSP, 오프라인 | p90 0.5초 목표, 네트워크 무관 | Watch 기기 Siri [1], Pixel Watch 5 [9] |
| 받아쓰기 · 짧은 ASR | 기기 (SoC NPU) | 프라이버시 + 지연 | Ray-Ban Display [20] |
| 대화형 LLM 응답 | 클라우드 (streaming), 짧은 응답은 기기 SLM 검토 | 품질, 단 첫 소리 지연 관리 | L3 hybrid routing |
| 장기 memory · 검색 · 에이전트(Handoff류) | 클라우드 | 저장·계산 규모, 지연 허용 | Plaud 전사 [41], Oura Advisor [14] |
| 번역 | 기기 또는 폰 (오프라인 우선) | 대화 지연 민감 | Ray-Ban Gen 2 오프라인 번역 [18], AirPods [4] |
| 연결 | 폰·Wi-Fi 우선, 셀룰러 fallback | 전력·열 | Ray-Ban (폰 경유) [20] vs AI Pin (LTE 직결) [25] |

---

## 14. 임베디드 관점에서 다시 보기

사례들을 펌웨어 체크리스트로 바꾸면 다음과 같다. 1세대 기기 EVT 단계에서 바로 쓸 수 있는 형태다.

```
[전력]   always-on 바닥 전류 (MCU + 센서 + 마이크)       목표 ≤ x mA, 매 빌드 측정
         idle → suspend 진입 확인 (R1 idle 소진 재발 방지)  회귀 테스트 자동화
         wake 1회당 에너지 (MCU→SoC→모뎀)                  mJ 단위로 기록
[열]     표면 온도 센서(NTC) + RC 모델 governor            hysteresis, 작업별 전력 예산
         최악 시나리오 (연속 질의 15분)                     거절률 ≤ 목표치
[지연]   말 끝 → 첫 피드백(로컬)                           p90 ≤ 수백 ms
         말 끝 → 첫 응답 소리(클라우드)                    p90 ≤ 1~2 s, 망 상태별
[신뢰성] 단계별 실패율 × 단계 수                           end-to-end 성공률 대시보드
         오프라인 degrade 모드                              네트워크 차단 시험
[안전]   fuel gauge SoH, 충전 중 온도                      이상 시 충전 제한
[보안]   기기별 credential, 이미지에 비밀 없음 (R1 교훈)   빌드 스캐너
[프라이버시] 표시등 인터락, 저장 직전 동의 게이트         우회 시험
[OTA]    FW와 모델 분리 A/B, 롤백                           주기·회귀 탐지 시간
```

C로 보면 7.4절의 governor처럼, 이 체크리스트 대부분은 **작은 상태기계 + 측정 루프**다. Don이 SSD에서 thermal throttling(NVMe의 온도 기반 성능 제한)을 다뤘다면 구조는 이미 안다. 다른 점은 입력이 "NAND 온도"가 아니라 "사람 피부에 닿는 표면 온도"이고, 출력이 "IOPS 제한"이 아니라 "AI 기능을 어느 계층으로 보낼지"라는 점이다. 열이 높으면 클라우드 세션 대신 기기의 짧은 응답으로 바꾸는 것도 governor의 출력이 될 수 있다.

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 모든 질의를 클라우드로 | 간단한 명령에도 2~3초, 망 나쁘면 실패 | 계층 배치를 안 함 | 의도 분류를 기기에서, 간단 명령은 로컬 완료 (예제 6) |
| 셀룰러를 기본 경로로 | 배터리 반나절, 몸체 발열 | 모뎀·SoC 상시 활성 | 폰·Wi-Fi 우선, 셀룰러 fallback, duty 예산 (예제 3) |
| 열을 throttling으로만 해결 | "식는 중" 대기가 잦음 | 평균 전력이 연속 허용치 초과 | 작업 전력 자체를 줄임, 고전력 출력 시간 제한 (예제 7, C) |
| idle 전류를 측정 안 함 | 안 써도 하루 못 감 | suspend 미진입, 깨우는 타이머 | 빌드마다 idle 전류 회귀 테스트 |
| 범용 비서를 첫 기능으로 | 리뷰 "뭘 해야 할지 모르겠다" | acceptance 기준 부재 | 매일 쓰는 한 가지 기능에 지표를 걸고 출시 |
| 폰 기능 재구현 | 통화·메시지 같은 기본기 불안 | 폰 대체 목표 | 폰과 역할 분담, 폰이 잘하는 건 폰에 |
| 표시등을 소프트웨어로만 | 표시등 우회·반발 | 인터락 없음 | 표시등 상태를 HW로 확인해야 카메라·마이크 활성 |
| 비밀을 기기 이미지에 | 키 유출 (R1 사례 [31]) | 공유 credential | 기기별 credential + 서버 프록시 |
| 배터리 시간 급감을 무시 | 팽창 등 안전 사고 | SoH 감시 없음 | fuel gauge 추세 경고, 충전 제한 |
| 사례 분석을 뉴스 요약으로 | 면접에서 깊이 없음 | 계층·숫자 없음 | "어디서 돌았나 → 전력·열·지연 → 결과" 순서로 말하기 |

---

## 16. 면접에서 이렇게 말한다

**Q.** "Why did the Humane AI Pin fail, from an engineering point of view?"

**A.** 제품 목표가 "폰 대체"였고, 그 목표가 셀룰러 직결, 모든 질의의 클라우드 처리, 작은 몸체의 단독 전원을 강제했다. 그 결과 몇 분 쓰면 과열 알림이 뜨고(The Verge), 배터리는 4~5시간이었고, 간단한 질문도 느렸다. 칩이 나빴던 게 아니라 계층 배치가 문제였다. Qualcomm Snapdragon 칩(AR1)을 쓴 Ray-Ban Meta는 폰을 거쳐 성공했다.

> "I'd frame it as a single root cause with three symptoms. The goal of replacing the phone forced a standalone LTE radio, cloud processing for every query, and a tiny self-contained battery. That produced the overheating warnings after a few minutes of use, four-to-five-hour battery life, and slow, unreliable answers. It wasn't the silicon brand — Meta's glasses also run on Qualcomm Snapdragon — it was where the work ran. Even a first-order RC thermal model with plausible assumed numbers says a pin-sized enclosure sustains well under a watt, so multi-watt radio-plus-compute bursts hit the surface limit in about a minute."

**Q.** "What makes Meta's glasses work where others didn't?"

**A.** AI가 없어도 사는 물건(선글라스, 카메라, 오디오)에 AI를 더했다. 연결과 큰 계산은 폰에 맡겨 안경의 전력·열을 줄였다. 가격은 $299~$379이고 구독이 없다. 그리고 배터리 불만을 세대마다 고쳤다(Gen 2 8시간). 2025년 한 해 700만 개 이상 팔렸다. 받아쓰기처럼 지연에 민감한 건 Display에서 기기 위에서 돈다.

> "They added AI to something people already wanted to wear, rather than asking people to adopt a new object for AI. Connectivity and heavy compute go through the phone, which keeps the glasses' power and thermal budget sane, and the latency-sensitive pieces — like dictation on the Display model — run on device. They also iterated on the top complaint: battery roughly doubled to eight hours in Gen 2. EssilorLuxottica reported over seven million units in 2025."

**Q.** "Which features should run on device vs phone vs cloud for our product?"

**A.** 기준은 세 가지다. 지연 민감도, 전력, 프라이버시. VAD·wake word·착용 감지·제스처는 always-on MCU. 간단 명령과 받아쓰기는 SoC NPU/DSP에서 오프라인으로. 대화형 LLM과 장기 memory, 에이전트는 클라우드에서 streaming으로. 연결은 폰·Wi-Fi 우선, 셀룰러는 fallback. 그리고 이 배치는 측정으로 계속 고친다.

> "I'd sort features by latency sensitivity, energy per invocation, and privacy. Always-on detection — VAD, wake word, wear detection, IMU gestures — belongs on the low-power MCU or DSP. Simple intents and dictation should complete offline on the SoC's NPU, because users judge the product by the most frequent interactions, and those need sub-second p90. Conversational LLM responses, long-term memory, and agent tasks go to the cloud with streaming, plus immediate local feedback so the turn never feels dead. Cellular should be a fallback, not the default path. Then I'd instrument every stage so the split is revisited with data."

**Q.** "How would you avoid the thermal and battery problems earlier products had?"

**A.** 설계 초기에 평균 전류 예산과 연속 허용 전력을 숫자로 정한다. 표면 온도 센서와 RC 모델로 governor를 만들되, throttling은 안전장치로만 쓰고 작업 전력 자체를 줄인다. idle 전류와 wake당 에너지를 빌드마다 회귀 테스트한다. Rabbit의 idle 소진은 이 테스트 하나로 잡혔을 문제다. 배터리 SoH 감시도 넣는다.

> "Start from numbers: an average-current budget from battery capacity and target hours, and a sustainable-power limit from the enclosure's thermal resistance. Then design the cascade so the expensive blocks — modem, SoC, camera — wake rarely. I'd add a skin-temperature governor with hysteresis, but treat throttling as a safety net, not the plan; if it fires often, the architecture is wrong. Idle current and energy per wake go into regression tests on every build — the R1's idle drain is exactly the bug that catches. Enterprise SSDs face the same class of problem with NVMe power states and thermal throttling; here it's the same discipline, except the heat sink is someone's skin."

**Q.** "Isn't cloud dependence the real problem with these devices?"

**A.** 반만 맞다. Plaud는 전사·요약을 클라우드에서 하고도 ARR $100M을 넘겼다. 회의가 끝난 뒤 받아도 되는 일이라서다. Friend는 실시간 대화를 클라우드에 둬서 7~10초 지연이 났다. 문제는 클라우드가 아니라 **실시간 기능을 클라우드에 둔 것**이다.

> "Only partly. Plaud runs transcription and summaries in the cloud and still passed a hundred million dollars in ARR, because nobody needs meeting notes within a second. Friend put a real-time conversation in the cloud and reviewers saw seven-to-ten-second replies. The rule I'd use is: anything in the conversational loop must have a local path; anything that can wait can go to the cloud."

**Q.** "What do you think about OpenAI's device and Meta's new Muse Charm as competitors?"

**A.** OpenAI는 법원 문서에서 첫 기기가 귀에 넣는 것도 웨어러블도 아니라고 했고, 이후 2027년으로 밀렸다는 보도가 있다. 형태는 확인된 게 없다. Muse Charm은 5G 모뎀, 카메라, 화면을 가진 휴대 기기로 공개됐고 가격은 미정이다. 둘 다 이미 큰 사용자 기반을 가진 에이전트를 기기 밖에서 먼저 만들었다는 점이 Humane과 다르다. Hark도 Handoff로 같은 순서를 밟고 있다.

> "On OpenAI I'd stick to what's public: court filings said the first product isn't in-ear or a wearable, and later reporting moved it to 2027 — so even the best-resourced team finds this hard. Muse Charm is a pocket device with a camera, a small screen and its own 5G modem, priced later. What both share, and Humane lacked, is an agent with real users before the hardware. Hark is following that order with Handoff, which I think is the right sequence; the hardware then has to win on latency, battery and an everyday use case."

**Q.** "Tell me about a product failure you analyzed and what you'd apply here."

**A.** SSD 에러 처리·불량 분석과 Apple bring-up에서의 버스 장애 root cause 경험을 말한다. 증상 → 재현 → root cause → 수정 → 재발 방지 테스트. 그리고 Humane 사례에 같은 틀을 적용해 본 것을 연결한다. 요점은 "증상 여러 개를 하나의 설계 결정으로 묶는 능력"이다.

> "In SSD firmware I worked on error handling and failure analysis, and at Apple I root-cause bus failures during new-silicon bring-up: reproduce, find the root cause, fix, and add a regression test so it can't come back. I applied the same frame to the AI wearables that failed. Overheating, short battery life and slow answers on the AI Pin all trace back to one architectural decision. For a first-generation device here, I'd turn each of those failure modes into a measurable gate before DVT — idle current, energy per wake, p90 latency to first sound, and surface temperature under a worst-case session."

---

## 17. 직접 해보기

1. Ray-Ban Meta Gen 2의 주장 배터리는 최대 8시간이고 Gen 1의 "거의 2배"다 [18]. Gen 1 안경 단독 시간을 역산하라. 그리고 이 값을 `products.py`에 넣을 때 무엇을 함께 기록해야 하나?
   정답: 약 4시간. `batt_h=4`와 함께 "derived, Gen 2 문구에서 역산"이라는 메모(또는 별도 필드)를 남긴다. 원 출처 숫자가 아니라는 표시가 핵심.
2. 예제 3의 가정 기기에서 always-on 바닥 전류가 3 mA가 아니라 8 mA라면, 300 mA 활성 구간의 하루 허용 시간은 몇 분인가? 손으로 계산하라.
   정답: (25 − 8)/(300 − 8) = 5.82% → 16 h × 60 × 0.0582 ≈ 56분. 바닥 전류 5 mA 증가가 활성 시간 15분을 지운다.
3. 예제 7에서 R_th를 25에서 15 °C/W로 낮추면(방열 개선) 연속 허용 전력과 3 W에서의 한계 도달 시간은? (C_th는 그대로)
   정답: 허용 = 18/15 = 1.2 W. τ = 150 s, 정상 상승 45 °C, t = −150·ln(1 − 18/45) = −150·ln(0.6) ≈ 77 s. 연속 허용은 크게 늘지만 버스트 시간은 비슷하다 — 버스트 전력 자체를 줄여야 한다.
4. 예제 6에 "D 폰 경유 + 기기 SLM 짧은 응답" 구조를 추가하라. 기기 SLM 첫 토큰을 중앙값 400 ms(가정)로 두고 p50·p90·성공률을 비교하라.
   힌트: `arch` dict에 항목을 하나 추가한다. endpoint 300 ms, 기기 ASR, 기기 SLM, TTS 단계로 구성하면 p90이 1초 안쪽으로 나오는지 본다.
5. 출시 전 예측 연습: Muse Charm(2026 연말 출시 예정 [23])과 Hark 1세대(가정)를 `scores.py` 형식으로 **지금** 점수 매기고 날짜와 함께 저장하라. 출시 6개월 뒤 결과와 비교해 hindsight bias 없이 이 틀이 맞는지 검증한다.
   힌트: 정답이 없다. 점수마다 근거 문장과 출처를 남기는 것이 과제다.
6. `placement.py`에서 `?`인 칸 하나를 골라 공식 자료로 확인해 대문자로 바꾸거나, 확인이 안 되면 그대로 둬라. 예: Pixel Watch 5의 일반 Gemini 질의 경로.
   힌트: Google 공식 블로그·지원 문서를 먼저 본다. 리뷰의 추측으로 칸을 채우지 않는다.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| on-device | 기기 위 처리 | 네트워크 없이 기기의 CPU·DSP·NPU에서 추론 |
| co-processor | 보조 프로세서 | 큰 코어를 재우고 배경 작업을 맡는 저전력 코어 (Pixel Watch의 M33·M55류) |
| always-on MCU | 상시 동작 MCU | VAD·wake word·센서를 µW~mW로 계속 돌리는 칩 |
| cascade | 단계적 깨우기 | 작은 모델이 통과시킨 것만 다음 큰 블록을 깨우는 구조 (I3) |
| Private Cloud Compute | Apple의 보호된 클라우드 추론 | 기기에서 못 하는 요청을 Apple 서버에서 처리하는 계층 |
| LAM | Large Action Model | Rabbit이 내세운, 웹앱을 대신 조작하는 모델 |
| EMG / sEMG | 근전도 / 표면 근전도 | 손목 근육 신호로 손가락 동작을 읽는 센서 (Neural Band) |
| PPG | 광용적맥파 | 빛으로 혈류 변화를 읽어 심박을 재는 센서 |
| TCO | 총소유비용 | 기기 가격 + 기간 동안의 구독 |
| ARR | 연간 반복 매출 | 구독 사업의 연 환산 매출 |
| SoH | State of Health | 배터리의 현재 용량 ÷ 초기 용량 같은 건강 지표 |
| thermal governor | 열 관리 정책 | 온도에 따라 작업·클럭을 제한하는 상태기계 |
| hysteresis | 이력 | 켜는 문턱과 끄는 문턱을 다르게 해 진동을 막는 기법 |
| hindsight bias | 사후 확증 편향 | 결과를 알고 나서 원인을 그럴듯하게 맞추는 오류 |
| fallback | 대체 경로 | 기본 경로가 없을 때만 쓰는 경로 (예: 셀룰러) |

---

## 19. 요약 & 체크리스트

성공한 AI 웨어러블은 AI를 앞세우지 않았다. 이미 쓰는 물건(시계, 안경, 반지, 이어버드)에 ML을 숨기고, 지연이 중요한 일은 기기에서, 큰 계산은 폰에서, 지식과 지연 허용 작업은 클라우드에서 했다. 실패한 기기(Humane AI Pin, Rabbit R1, Friend)는 "폰 대체"나 "범용 비서"를 첫 기능으로 삼았고, 그 목표가 셀룰러 직결과 클라우드 전면 의존을 강제해 열·배터리·지연을 한꺼번에 무너뜨렸다. 클라우드 자체가 원인은 아니다(Plaud). 실시간 기능을 클라우드에 둔 것이 원인이다. Hark의 공개 단서는 야심 찬 사분면을 가리키고, 그래서 1세대 기기에서는 평균 전류·연속 허용 전력·p90 지연·false wake 비용을 출시 게이트로 숫자화하는 펌웨어·AI 엔지니어링이 결정적이다.

- [ ] 사례 다섯 개 이상을 "AI 위치 → 배터리·열 → 지연 → 프라이버시 → 가격 → 결과" 순서로 출처와 함께 말할 수 있다
- [ ] Humane AI Pin의 증상 세 개를 하나의 설계 결정으로 묶어 설명할 수 있다
- [ ] Ray-Ban Meta가 통한 이유를 계층 배치와 숫자(가격, 배터리, 판매량)로 설명할 수 있다
- [ ] "클라우드 의존이 문제다"라는 말에 Plaud와 Friend로 반론할 수 있다
- [ ] 용량과 주장 시간으로 평균 전류를 역산하고, 활성 duty 허용치를 손으로 계산할 수 있다
- [ ] RC 열 모델로 연속 허용 전력과 한계 도달 시간을 계산할 수 있다
- [ ] 단계별 성공 확률의 곱으로 end-to-end 신뢰성을 설명할 수 있다
- [ ] 주관 점수의 hindsight bias를 설명하고, 출시 전 예측으로 검증하는 법을 말할 수 있다
- [ ] Hark 같은 기기의 기능 배치표(기기·폰·클라우드)를 근거와 함께 그릴 수 있다
- [ ] OpenAI 기기에 대해 확인된 것과 미확인 보도를 구분해 말할 수 있다

---

## 참고 자료

모든 자료의 확인일은 **2026-10-04**다. "2차"는 매체의 정리, "검색 요약"은 본문을 직접 열지 못하고 검색 결과 요약으로만 확인한 것이다. 제품 정보는 바뀐다 — 인용 전에 다시 확인할 것.

1. Apple Newsroom, "Apple introduces the advanced new Apple Watch Series 9" (2023-09-12): https://www.apple.com/newsroom/2023/09/apple-introduces-the-advanced-new-apple-watch-series-9/
2. Apple Newsroom, "Apple debuts Apple Watch Series 11, featuring groundbreaking health insights" (2025-09): https://www.apple.com/newsroom/2025/09/apple-debuts-apple-watch-series-11-featuring-groundbreaking-health-insights/
3. Apple Newsroom, "watchOS 26 delivers more personalized ways to stay active and connected" (2025-06): https://www.apple.com/newsroom/2025/06/watchos-26-delivers-more-personalized-ways-to-stay-active-and-connected/ · 요구 조건(Apple Intelligence iPhone + 블루투스 헤드폰)은 검색 요약 · Apple Support "About watchOS 26 Updates": https://support.apple.com/en-us/123002
4. Apple Newsroom, "Introducing AirPods Pro 3, the ultimate audio experience" (2025-09-09): https://www.apple.com/newsroom/2025/09/introducing-airpods-pro-3-the-ultimate-audio-experience/
5. MacRumors (2차), "Apple Confirms Google Gemini Will Power Next-Generation Siri This Year" (2026-01-12): https://www.macrumors.com/2026/01/12/google-gemini-next-generation-siri/
6. CNBC, "Apple releases iOS 27, redesigned Siri AI" (2026-09-14, 검색 요약): https://www.cnbc.com/2026/09/14/apple-releases-ios-27-redesigned-siri-ai.html
7. Wikipedia (2차), "Pixel Watch": https://en.wikipedia.org/wiki/Pixel_Watch
8. Google Blog, "Pixel Watch 4: Design upgrades, new fitness features and expanded AI" (2025-08): https://blog.google/products/pixel/pixel-watch-4/
9. TechCrunch, "Google unveils the Pixel Watch 5 …" (2026-08-12): https://techcrunch.com/2026/08/12/google-unveils-the-pixel-watch-5-with-a-smarter-gemini-and-advanced-health-monitoring/ · 배터리 용량(2차) Droid Life: https://www.droid-life.com/2026/08/12/pixel-watch-5-official-at-399-with-huge-health-upgrades/
10. TechCrunch, "Samsung's Galaxy Ring, its first smart ring, arrives July 24 for $399" (2024-07-10): https://techcrunch.com/2024/07/10/samsungs-galaxy-ring-its-first-smart-ring-arrives-july-24-for-399 · 배터리 용량(2차) Wikipedia: https://en.wikipedia.org/wiki/Samsung_Galaxy_Ring
11. Samsung Newsroom, "[Galaxy Unpacked 2025] A First Look at the Galaxy Watch8 Series" (2025-07): https://news.samsung.com/global/galaxy-unpacked-2025-a-first-look-at-the-galaxy-watch8-series-streamlining-sleep-exercise-and-everything-in-between · Samsung Support, Energy Score: https://ushl.samsung.com/ae/support/mobile-devices/how-to-check-energy-score-with-the-galaxy-ring
12. Android Authority (2차), "Galaxy Ring battery scare leaves user stranded and hospitalized" (2025-09): https://www.androidauthority.com/samsung-galaxy-ring-battery-swelling-3602200/ · SamMobile: https://www.sammobile.com/news/samsung-says-viral-galaxy-ring-battery-swelling-is-an-extremely-rare-case/ · Samsung 조사 결과(검색 요약) Notebookcheck: https://www.notebookcheck.net/Samsung-finally-releases-statement-for-swollen-Galaxy-Ring.1199702.0.html
13. Business Wire, "Oura Ring 4, Powered by Smart Sensing, Is Here" (2024-10-03): https://www.businesswire.com/news/home/20241003761832/en/Oura-Ring-4-Powered-by-Smart-Sensing-Is-Here · 멤버십 가격(2차) Droid Life: https://www.droid-life.com/2024/10/03/oura-ring-4-official-at-349-with-improvements-everywhere-redesigned-a/
14. Business Wire, "Oura Advisor, an AI-powered Personal Health Companion, Now Rolling Out to All Oura Members" (2025-03-31): https://www.businesswire.com/news/home/20250331565896/en/Oura-Advisor-an-AI-powered-Personal-Health-Companion-Now-Rolling-Out-to-All-Oura-Members
15. CNBC, "Oura reaches $11 billion valuation with new $900 million fundraise" (2025-10-14, 검색 요약): https://www.cnbc.com/2025/10/14/oura-ringmaker-valuation-fundraise.html
16. Yahoo Finance (2차), "Oura takes step toward IPO with S-1 filing" (2026-09): https://uk.finance.yahoo.com/news/oura-takes-step-toward-ipo-215012105.html · FY2025 매출 수치(2차) the5krunner: https://the5krunner.com/2026/09/04/oura-ipo-s1-revenue/
17. Meta Newsroom, "Introducing the New Ray-Ban | Meta Smart Glasses" (2023-09-27): https://about.fb.com/news/2023/09/new-ray-ban-meta-smart-glasses/
18. Meta Newsroom, "Ray-Ban Meta (Gen 2) … Better Battery Life, Video Capture" (2025-09): https://about.fb.com/news/2025/09/ray-ban-meta-gen-2-better-battery-life-video-capture/
19. Meta Newsroom, "Meta Ray-Ban Display: AI Glasses With an EMG Wristband" (2025-09): https://about.fb.com/news/2025/09/meta-ray-ban-display-ai-glasses-emg-wristband/
20. UploadVR (2차), "Meta Ray-Ban Display Review": https://www.uploadvr.com/meta-ray-ban-display-review/
21. UploadVR (2차), "Meta & EssilorLuxottica Sold 7 Million Smart Glasses In 2025" (2026-02-11): https://www.uploadvr.com/meta-essilorluxottica-sold-7-million-smart-glasses-in-2025/
22. Meta Blog, "Everything We Announced at Meta Connect 2026" (2026-09): https://www.meta.com/blog/meta-connect-2026-everything-we-announced/ · 가격·Gen 3 배터리(2차) VR.org: https://vr.org/meta-connect-2026
23. Dezeen (2차), "Meta debuts Tamagotchi-style 'charm' housing an AI agent" (2026-09-25): https://www.dezeen.com/2026/09/25/meta-debuts-tamagotchi-style-ai-agent/
24. UploadVR (2차, 검색 요약), "Meta Is Improving Its Smart Glasses Privacy LED Tampering Detection": https://www.uploadvr.com/meta-improving-smart-glasses-privacy-led-tampering-detection/ · Tom's Guide (검색 요약): https://www.tomsguide.com/ai/less-pervert-y-meta-ai-glasses-will-now-stop-recording-if-you-cover-the-warning-light
25. 9to5Google (2차), "Humane fully details $699 'Ai Pin' and what it can do" (2023-11-09): https://9to5google.com/2023/11/09/humane-ai-pin-launch/
26. TechCrunch, "Humane's Ai Pin up close" (2023-11-09): https://techcrunch.com/2023/11/09/humanes-ai-pin-up-close/
27. The Verge 리뷰 (David Pierce, 2024-04-11) — Verge 본문은 직접 열지 못해 인용을 담은 Daring Fireball 링크로 확인: https://daringfireball.net/linked/2024/04/11/pierce-humane-ai-pin · 원문: https://www.theverge.com/24126502/humane-ai-pin-review
28. U.S. CPSC, "Humane Recalls Charge Case Accessory for Ai Pin Due to Lithium Battery Fire Hazard": https://www.cpsc.gov/Recalls/2025/Humane-Recalls-Charge-Case-Accessory-for-Ai-Pin-Due-to-Lithium-Battery-Fire-Hazard
29. TechCrunch, "Humane's AI Pin is dead, as HP buys startup's assets for $116M" (2025-02-18): https://techcrunch.com/2025/02/18/humanes-ai-pin-is-dead-as-hp-buys-startups-assets-for-116m
30. Wikipedia (2차), "Humane Inc.": https://en.wikipedia.org/wiki/Humane_Inc.
31. Wikipedia (2차), "Rabbit r1": https://en.wikipedia.org/wiki/Rabbit_r1
32. Engadget, "Rabbit R1 review: A $199 AI toy that fails at almost everything" (2024-05): https://www.engadget.com/rabbit-r1-review-a-199-ai-toy-that-fails-at-almost-everything-161043050.html
33. Boing Boing (2차, 검색 요약), "Rabbit R1 has 5,000 people using it daily" (2024-09-26): https://boingboing.net/2024/09/26/rabbit-r1-has-5000-active-users.html
34. 9to5Google, "Rabbit R1 wants a second chance with the new RabbitOS 2 update" (2025-09-08): https://9to5google.com/2025/09/08/rabbit-r1-gets-do-over-update/
35. rabbit newsroom, "rabbit unveils OS3" (2026-09, 검색 요약): https://www.rabbit.tech/newsroom/rabbitos-3-launch · Help Net Security (검색 요약, 2026-09-23): https://www.helpnetsecurity.com/2026/09/23/rabbit-os3-ai-agent-now-available/
36. TechCrunch, "Meta acquires AI device startup Limitless" (2025-12-05): https://www.techcrunch.com/2025/12/05/meta-acquires-ai-device-startup-limitless/
37. TechCrunch, "Amazon acquires Bee, the AI wearable that records everything you say" (2025-07-22): https://techcrunch.com/2025/07/22/amazon-acquires-bee-the-ai-wearable-that-records-everything-you-say/
38. GeekWire (검색 요약), "Amazon's AI on the web, wrist, and phone" (2026-01): https://www.geekwire.com/2026/amazons-ai-on-the-web-wrist-and-phone-tech-giant-chases-consumer-rivals-with-latest-moves/
39. Fortune, Friend AI necklace review (2025-10-03): https://fortune.com/2025/10/03/friend-ai-necklace-review-avi-schiffmann/
40. The San Francisco Standard, Avi Schiffmann / Friend profile (2025-11-16): https://sfstandard.com/2025/11/16/avi-schiffmann-friend-ai-pendant-loneliness-profile/
41. Plaud, "Plaud NotePin" 제품 페이지: https://www.plaud.ai/products/plaud-notepin
42. Plaud Blog, "Plaud Scales From $1M to $100M ARR Within Two Years" (2026-06-16): https://www.plaud.ai/blogs/news/plaud-scales-from-1m-to-100m-arr
43. 9to5Mac, "Court documents detail OpenAI's plans for its AI hardware project with Jony Ive" (2025-06-23): https://9to5mac.com/2025/06/23/not-a-wearable-court-documents-detail-openais-plans-for-its-ai-hardware-project-with-jony-ive/
44. Wikipedia (2차), "Io (company)": https://en.wikipedia.org/wiki/Io_(company)
45. PYMNTS (2차), "OpenAI Delays First Consumer Device to 2027" (2026-02-15): https://www.pymnts.com/news/artificial-intelligence/2026/openai-delays-first-consumer-device-to-2027/
46. CNBC, "OpenAI DevDay 2026 recap" (2026-09-29, 검색 요약): https://www.cnbc.com/2026/09/29/openai-devday-2026-live-updates.html

Hark 관련 사실은 이 작업공간의 `job_interview_prep_research/hark_ai_embedded_ai/hark_ai_embedded_ai_context.md` §2와 `job_interview_prep_research/hark_ai_embedded_swe/hark_ai_embedded_swe_context.md` §2에서 가져왔다 (그 파일들의 원 출처: TechCrunch, SiliconANGLE, Intel Capital, Business Wire, Bloomberg, Hark Greenhouse 공고).
