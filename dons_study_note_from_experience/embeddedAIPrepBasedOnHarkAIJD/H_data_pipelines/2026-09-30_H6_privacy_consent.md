# H6. 프라이버시와 동의 — always-listening 기기의 데이터 수집을 설계로 안전하게

> **이 노트를 다 읽으면**: 웨어러블 센서 데이터를 민감도별로 분류하고 클래스마다 기본 처리(버림·특징만·암호화 보관·TTL)를 정할 수 있다 · 동의 ledger와 데이터셋 빌더 필터를 짜서 "철회한 사용자·범위 밖 데이터"를 자동으로 빼낼 수 있다 · 평문 해시가 왜 익명화가 아닌지 전수 대입으로 보여 주고 HMAC 가명화와 키 회전을 설명할 수 있다 · Laplace mechanism과 randomized response로 집계 telemetry에 차등 프라이버시를 걸고 ε에 따른 오차를 숫자로 말할 수 있다
> **JD 연결**: "Build data collection and ingestion pipelines … various sensors, at scale" — study_prep_list H6: always-listening 기기의 PII, 온디바이스 익명화, 보관 기간, GDPR/CCPA 기초 (컨슈머 AI 기기 필수). N4(always-listening UX: 프라이버시 표시, 사용자 신뢰)와 짝을 이룬다.
> **Don 기준 난이도**: 링버퍼·flash 수명·암호화 엔진·secure boot·factory provisioning, 그리고 SSD의 Sanitize/crypto erase 개념은 이미 강하다 / "어떤 데이터를 왜 모으면 안 되는가"를 판단하는 프라이버시 엔지니어링 원칙, 동의 상태를 데이터와 함께 흘리는 파이프라인 설계, 차등 프라이버시의 수학, 규제 용어는 새로 배운다
> **선행 노트**: B5(오디오·음성 모델 — VAD, KWS, 화자 인식), G6(카메라 프라이버시 표시 §2.7), H1(온디바이스 로깅), H2(전송 보안), H3(백엔드 ingestion·lifecycle), H4(라벨링), H5(데이터셋 관리)

> **먼저 분명히 해 둘 것 — 이 노트는 법률 자문이 아니다.** GDPR, CCPA/CPRA, BIPA, HIPAA, COPPA에 대한 설명은 엔지니어가 회의에서 길을 잃지 않을 정도의 **높은 수준 요약**이다. 법은 개정되고 해석은 판례·감독기관 지침으로 바뀌며, 이 노트의 요약은 이미 낡았을 수 있다. 실제 제품에서 "이 데이터를 모아도 되는가, 동의 문구는 무엇이어야 하는가"는 반드시 법무팀·프라이버시 담당자(DPO 등)와 정한다. 엔지니어의 일은 그 결정을 **시스템이 강제하도록** 만드는 것이다.

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 귀에 걸린 마이크는 "주인만" 녹음하지 않는다

예를 들어 Hark 같은 웨어러블 AI 기기라면(추정), 하루 종일 켜진 마이크, IMU, 어쩌면 카메라·PPG·위치까지 가진다. 데이터 수집 파이프라인(H1~H5)을 만드는 엔지니어 입장에서 이건 금광이다. wake word 오작동 사례, 시끄러운 식당의 음성, 걸음 패턴 — 모델을 개선할 재료가 끝없이 나온다.

그런데 이 데이터는 펌웨어 엔지니어가 익숙한 SSD telemetry(온도, ECC 카운트, latency 히스토그램)와 성격이 완전히 다르다.

- **주인의 사적인 말**: 의사와의 통화, 가족 다툼, 비밀번호를 소리 내 읽는 순간.
- **주변 사람(bystander)의 말**: 착용자는 기기를 샀고 약관에 동의했지만, 옆자리 동료나 카페 손님은 아무것도 동의하지 않았다.
- **몸에 대한 정보**: 목소리 자체(화자 embedding = 생체 정보), 심박(PPG), 걸음걸이(IMU), 위치 이력.

한 문장으로 하면: **always-on 기기의 데이터 파이프라인은 기본값이 "모든 것을 듣는다"이기 때문에, "무엇을 남기지 않을지"를 설계로 정하지 않으면 사고가 난다.**

### 0.2 실제로 신뢰가 깨진 적이 있다

2019년, 여러 대형 음성 비서 서비스에서 **사람(외주 인력 포함)이 녹음 일부를 듣고 전사·평가하는 품질 개선 프로그램**이 있었다는 사실이 언론 보도로 널리 알려졌다. 보도된 내용의 핵심은 (1) 사용자 다수가 사람이 들을 수 있다는 걸 몰랐고, (2) wake word 오작동으로 의도치 않게 녹음된 사적인 대화도 섞여 있었다는 점이다. 이후 여러 회사가 해당 프로그램을 일시 중단하거나, 사람 검토를 opt-in으로 바꾸거나, 녹음 보관 설정을 사용자에게 더 드러내는 쪽으로 바꿨다고 보도됐다. (회사별 세부 사항은 이 노트의 범위가 아니다. 교훈만 가져간다.)

엔지니어가 가져갈 교훈은 세 가지다.

1. **사람 검토(human review)는 ML 개발에 정상적인 공정**(H4 라벨링)이지만, 사용자가 모르면 배신이 된다. 문제는 기술이 아니라 **투명성과 동의 범위**였다.
2. **false wake(오작동) 녹음이 가장 위험한 데이터**다. 사용자가 기기를 부르지 않았는데 저장된 오디오이기 때문이다. KWS의 false accept rate(B5)는 정확도 지표인 동시에 프라이버시 지표다.
3. **신뢰는 제품 기능이다.** always-listening 기기는 사용자가 "이게 내 말을 몰래 올리지 않는다"고 믿어야 착용한다. 착용하지 않으면 모델도 데이터도 없다.

### 0.3 파이프라인 전체에 걸친 통제 지점

아래 그림은 H1~H5에서 만든 파이프라인 위에 이 노트의 통제 장치를 얹은 것이다. 이 노트의 절 번호가 괄호 안에 있다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="h6a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker>
  <marker id="h6r" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#d0564a"/></marker></defs>
  <rect x="10" y="30" width="150" height="62" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="85" y="54" font-size="13" text-anchor="middle">기기 (MCU / SoC)</text>
  <text x="85" y="74" font-size="12" text-anchor="middle">센서 → VAD · KWS</text>
  <rect x="185" y="30" width="120" height="62" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="245" y="54" font-size="13" text-anchor="middle">폰 앱</text>
  <text x="245" y="74" font-size="12" text-anchor="middle">동의 UI · 중계</text>
  <rect x="330" y="30" width="150" height="62" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="405" y="54" font-size="13" text-anchor="middle">백엔드 ingest</text>
  <text x="405" y="74" font-size="12" text-anchor="middle">object store · catalog</text>
  <rect x="505" y="30" width="165" height="62" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="587" y="54" font-size="13" text-anchor="middle">데이터셋 · 라벨링</text>
  <text x="587" y="74" font-size="12" text-anchor="middle">학습 · 평가</text>
  <line x1="160" y1="61" x2="183" y2="61" stroke="currentColor" stroke-width="1.5" marker-end="url(#h6a)"/>
  <line x1="305" y1="61" x2="328" y2="61" stroke="currentColor" stroke-width="1.5" marker-end="url(#h6a)"/>
  <line x1="480" y1="61" x2="503" y2="61" stroke="currentColor" stroke-width="1.5" marker-end="url(#h6a)"/>
  <text x="85" y="122" font-size="12" text-anchor="middle" fill="#3f9a6b">온디바이스 우선 (§5)</text>
  <text x="85" y="142" font-size="12" text-anchor="middle" fill="#3f9a6b">pre-roll만 RAM (§5)</text>
  <text x="85" y="162" font-size="12" text-anchor="middle" fill="#3f9a6b">특징만 저장 (§5)</text>
  <text x="85" y="182" font-size="12" text-anchor="middle" fill="#3f9a6b">flash 암호화 (§8)</text>
  <text x="245" y="122" font-size="12" text-anchor="middle" fill="#3f9a6b">세분화 동의 (§3)</text>
  <text x="245" y="142" font-size="12" text-anchor="middle" fill="#3f9a6b">TLS 전송 (§8)</text>
  <text x="245" y="162" font-size="12" text-anchor="middle" fill="#3f9a6b">철회 버튼 (§3)</text>
  <text x="405" y="122" font-size="12" text-anchor="middle" fill="#3f9a6b">HMAC 가명화 (§4)</text>
  <text x="405" y="142" font-size="12" text-anchor="middle" fill="#3f9a6b">접근 통제 · 감사 로그 (§8)</text>
  <text x="405" y="162" font-size="12" text-anchor="middle" fill="#3f9a6b">TTL · tombstone (§9)</text>
  <text x="405" y="182" font-size="12" text-anchor="middle" fill="#3f9a6b">DP 집계 (§6)</text>
  <text x="587" y="122" font-size="12" text-anchor="middle" fill="#3f9a6b">동의 범위 필터 (§3)</text>
  <text x="587" y="142" font-size="12" text-anchor="middle" fill="#3f9a6b">리뷰 전 redaction (§10)</text>
  <text x="587" y="162" font-size="12" text-anchor="middle" fill="#3f9a6b">삭제 후 재빌드 (§9)</text>
  <line x1="245" y1="230" x2="580" y2="230" stroke="#d0564a" stroke-width="2" stroke-dasharray="6 4" marker-end="url(#h6r)"/>
  <line x1="245" y1="200" x2="245" y2="230" stroke="#d0564a" stroke-width="2" stroke-dasharray="6 4"/>
  <line x1="405" y1="230" x2="405" y2="200" stroke="#d0564a" stroke-width="2" stroke-dasharray="6 4" marker-end="url(#h6r)"/>
  <line x1="245" y1="230" x2="95" y2="230" stroke="#d0564a" stroke-width="2" stroke-dasharray="6 4" marker-end="url(#h6r)"/>
  <text x="340" y="255" font-size="13" text-anchor="middle">철회 · 삭제 요청은 거꾸로 모든 단계에 전파된다 (§3, §9)</text>
  <text x="340" y="275" font-size="12" text-anchor="middle" fill="#888">초록 = 이 노트가 다루는 통제 장치</text>
</svg>
```

그림 0 — H1~H5 파이프라인의 각 단계에 걸리는 프라이버시 통제. 데이터는 왼쪽에서 오른쪽으로 흐르지만, 철회·삭제는 오른쪽 끝까지 거꾸로 따라가야 한다.

### 0.4 Don의 경험과 연결

Don은 이미 이 노트의 절반을 다른 이름으로 해 봤다.

| Don이 아는 것 | 이 노트에서의 이름 |
|---|---|
| SSD의 Sanitize / Format NVM with Crypto Erase: 미디어 암호화 키만 바꿔서 전체 데이터를 순식간에 읽을 수 없게 | **crypto-shredding** (§8) — 사용자별 키를 지워 백업까지 무력화 |
| telemetry 로그에 고객 데이터(LBA 내용)를 절대 안 담는 규칙 | **data minimization** (§1) |
| factory에서 기기별 키·인증서 주입, secure boot | **기기 인증과 TLS 상호 인증** (§8) |
| 링버퍼가 오래된 엔트리를 덮어쓰는 것 | **pre-roll 버퍼 = 자동 삭제** (§5) |
| FW 버전별로 로그 포맷이 다르면 파서가 버전 필드를 보고 분기 | **동의 정책 버전을 데이터에 붙이기** (§3) |

---

## 1. 프라이버시 엔지니어링 원칙

### 1.1 직관 — "모으고 나서 지키기"가 아니라 "애초에 안 모으기"

보안(security)은 "모은 데이터를 남이 못 보게"다. 프라이버시(privacy)는 한 걸음 앞선다: "**이 데이터를 우리가 가져야 하는가, 얼마나 오래, 무슨 목적으로?**" 아무리 잘 암호화해도 필요 없는 데이터를 들고 있으면 유출·남용·소환(법적 요구) 위험은 남는다. 가장 안전한 데이터는 **존재하지 않는 데이터**다.

펌웨어 비유: 버그 리포트에 core dump 전체를 올리는 대신 레지스터·콜스택 몇 개만 올리는 것과 같다. 디버깅엔 충분하고, 고객 데이터가 섞일 일이 없다.

### 1.2 원칙 일곱 가지

GDPR 제5조의 원칙들(합법성·공정성·투명성, 목적 제한, 데이터 최소화, 정확성, 보관 기간 제한, 무결성·기밀성, 책임성)과 "Privacy by Design" 개념을 엔지니어 언어로 옮기면 대략 이렇다.

| 원칙 | 한 줄 정의 | 웨어러블 파이프라인에서의 모습 |
|---|---|---|
| Data minimization | 목적에 필요한 최소한만 수집 | raw 오디오 대신 keyword 클립·특징만 (§5) |
| Purpose limitation | 모을 때 밝힌 목적으로만 사용 | "wake word 개선"으로 모은 클립을 광고 분석에 쓰지 않음. 목적을 데이터 태그로 강제 (§3) |
| On-device first | 기기에서 끝낼 수 있으면 올리지 않음 | VAD·KWS·화자 검증은 기기에서, 결과만 사용 (B5, L3) |
| Privacy by design / by default | 설계 단계에서 넣고, 기본값이 가장 보호적 | data donation은 기본 꺼짐(opt-in), 보관 기간 기본값은 짧게 |
| Storage limitation | 필요 기간 뒤 삭제 | 데이터 클래스별 TTL, 자동 sweeper (§9) |
| Transparency | 무엇을 왜 모으는지 사용자가 알 수 있음 | 앱의 "내 데이터" 화면, 녹음 표시 LED (G6 §2.7, N4) |
| User control | 사용자가 보고·끄고·지울 수 있음 | 세분화 토글, 다운로드·삭제, 철회 전파 (§3, §9) |

말로 하면: **모으는 양을 줄이고(minimization), 쓰는 곳을 묶고(purpose), 오래 두지 않고(storage), 사용자가 알고 고를 수 있게(transparency, control)** 한다. 나머지 기술(암호화, 가명화, DP)은 이 원칙을 지키는 도구일 뿐이다.

### 1.3 흔한 함정

- "암호화했으니 프라이버시 OK"는 틀린 말이다. 암호화는 무단 접근을 막지만, **회사 내부에서의 목적 외 사용**과 **과잉 수집**은 막지 못한다.
- "나중에 쓸지도 모르니 일단 raw로 다 모으자"는 minimization과 정면 충돌한다. ML 팀이 가장 자주 하는 요구이고, 데이터 엔지니어가 "무슨 목적, 얼마나, 언제까지?"를 되물어야 한다.
- 원칙은 문서에 있으면 소용없다. **파이프라인 코드가 강제해야** 한다 (§3의 필터, §9의 sweeper).

---

## 2. 데이터 분류 — 무엇이 얼마나 민감한가

### 2.1 왜 분류부터 하나

펌웨어에서 메모리 영역마다 MPU 권한(RO, RW, XN)을 다르게 거는 것처럼, 데이터도 **클래스마다 다른 규칙**을 건다. 클래스를 정하지 않으면 "모든 데이터에 가장 느슨한 규칙"이 기본값이 된다.

### 2.2 Hark 같은 기기의 데이터 분류표 (예시 — 추정 제품, 예시 정책)

민감도는 4단계로 둔다: **낮음 → 중간 → 높음 → 매우 높음(특수 범주 가능)**. 아래 "기본 처리"는 이 노트가 제안하는 출발점이지 정답이 아니다. 실제 정책은 법무·제품과 함께 정한다.

| 데이터 | 무엇을 드러낼 수 있나 | 민감도 | 기본 처리(제안) |
|---|---|---|---|
| raw 오디오 (상시) | 대화 내용, 주변인 목소리, 장소 소리 | 매우 높음 | 기기 RAM 링버퍼에서만 존재, 저장·전송 안 함 |
| wake 후 명령 클립 | 사용자 명령, 섞인 주변 소리 | 높음 | 제품 동작용은 처리 후 삭제. 학습용 보관은 별도 opt-in + 짧은 TTL |
| 전사(transcript) | 말한 내용 그대로, 이름·번호 등 PII | 높음 | PII redaction 후 보관, 짧은 TTL, 사람 검토는 opt-in |
| 화자 embedding (voiceprint) | 사람을 고유하게 식별 — 생체 정보 | 매우 높음 | 기기 안 secure storage에만, 서버 업로드 금지가 기본 |
| IMU 원시 신호 | 활동, 걸음걸이(개인 식별 가능성), 수면 | 중간~높음 | 세션 특징 위주, raw는 opt-in 연구 수집만 |
| PPG · 심박 · 피부온도 | 건강 상태 추정 | 높음 | 기기·앱 처리 우선, 서버는 집계 위주 |
| 위치(GPS, Wi-Fi 스캔) | 집·직장·병원 방문, 일상 패턴 | 높음 | 거친 해상도로 축소, 필요 기능에서만 |
| 카메라 프레임 | 얼굴(주변인 포함), 화면 내용, 문서 | 매우 높음 | 온디바이스 처리, 표시 LED, 저장은 명시적 사용자 동작으로만 |
| 기기 telemetry | 배터리, 온도, crash, FW 버전 | 낮음~중간 | 가명 ID + 집계, 필요 시 DP (§6) |

"말로 하면": 표의 아래로 갈수록 덜 민감한 게 아니다. **같은 데이터도 조합되면 민감도가 올라간다.** 배터리 로그(낮음)라도 분 단위 타임스탬프와 Wi-Fi 스캔이 붙으면 생활 패턴이 드러난다.

### 2.3 "익명" 센서 데이터가 사람을 가리킬 수 있다 — IMU 예

연구 문헌에는 가속도계 기반 걸음걸이(gait)로 사람을 구별하는 연구가 꽤 있다(정확도는 데이터셋·조건에 따라 크게 다르다). 즉 IMU 로그에서 사용자 ID 필드만 지운다고 익명이 되지 않을 수 있다. 직관을 잡기 위해 **합성 데이터**로 실험해 보자. 사용자 50명이 저마다 고유한 걸음 특징(보폭 주파수, 진폭, 좌우 비대칭, 걸음 간격 변동)을 갖고, 세션마다 노이즈가 섞인다. ID를 지운 세션을 "등록된 지문"(각 사람 세션 3개의 평균)과 가장 가까운 사람으로 매칭한다.

```python
# ID를 지운 IMU 걸음 특징으로도 사람을 다시 찾을 수 있나? (합성 데이터 — 정도는 실제와 다를 수 있다)
import numpy as np
rng = np.random.default_rng(2)
U, S = 50, 10                                    # 사용자 50명, 사람당 세션 10개
# 세션 특징 4개: 보폭 주파수(Hz), 수직 가속 진폭(g), 좌우 비대칭, 걸음 간격 변동
person = np.c_[rng.normal(1.9, 0.12, U), rng.normal(0.35, 0.06, U), rng.normal(0.05, 0.02, U), rng.normal(0.03, 0.008, U)]
noise = np.array([0.04, 0.025, 0.012, 0.005])     # 같은 사람이라도 날마다 달라지는 정도
X = np.repeat(person, S, 0) + rng.normal(0, 1, (U * S, 4)) * noise
y = np.repeat(np.arange(U), S)
mu, sd = X.mean(0), X.std(0); Z = (X - mu) / sd
enroll, probe = (np.arange(U * S) % S) < 3, (np.arange(U * S) % S) >= 3    # 세션 3개로 "지문" 만들기
cent = np.stack([Z[enroll & (y == u)].mean(0) for u in range(U)])
def reid(cols):
    d = ((Z[probe][:, None, cols] - cent[None, :, cols]) ** 2).sum(-1)
    return (d.argmin(1) == y[probe]).mean()
print(f"우연 수준: {1/U:.1%}")
for cols, name in [([0], "보폭 주파수만"), ([0, 1], "+ 진폭"), ([0, 1, 2, 3], "특징 4개 전부")]:
    print(f"{name:<10}: 재식별 정확도 {reid(cols):.1%}")
```

```text
우연 수준: 2.0%
보폭 주파수만   : 재식별 정확도 12.3%
+ 진폭      : 재식별 정확도 25.4%
특징 4개 전부  : 재식별 정확도 52.0%
```

출력에서 볼 것: 우연이면 2%인데, 특징을 하나씩 더할수록 재식별률이 12% → 25% → 52%로 오른다. 실제 수치는 합성 파라미터에 달려 있으므로 **숫자 자체가 아니라 경향**을 가져간다: 특징이 풍부할수록 "ID 없는 데이터"는 지문이 된다. 같은 이유로 화자 embedding은 정의상 식별용 벡터라서 가장 민감한 클래스에 넣는다(B5 §5).

### 2.4 함정

- 데이터 분류를 **필드 이름 기준**으로만 하면 놓친다. `debug_blob` 안에 오디오 버퍼 일부가 들어가는 일이 실제로 흔하다. 펌웨어 crash dump에 마이크 DMA 버퍼가 통째로 찍히는 상황을 떠올려라. crash dump 생성 코드에서 오디오 버퍼 영역을 마스킹해야 한다.
- 주변인(bystander)은 동의 주체가 아니다. 그래서 착용자의 동의만으로 "모든 오디오 수집 OK"가 되지 않는다는 점을 설계에 반영한다(§5의 keyword-only 보존이 대표적인 완화책).

---

## 3. 동의와 사용자 제어

### 3.1 두 종류의 동의를 섞지 말 것

| 구분 | 예 | 성격 | 기본값 |
|---|---|---|---|
| 제품 운영(product operation) | 명령 클립을 클라우드 ASR로 보내 답을 받기, crash 리포트 | 기능을 쓰려면 필요한 처리. 고지·약관으로 다루는 경우가 많다(법적 근거는 법무가 정한다) | 기능을 켜면 동작, 처리 후 삭제 |
| 데이터 기부(data donation) · dogfood | wake word 개선을 위한 클립 보관, 사람 검토, raw IMU 연구 수집 | 모델 개발 목적. 별도 opt-in, 언제든 철회 | **꺼짐** |

이 둘을 하나의 "동의함" 체크박스로 묶으면 두 가지가 동시에 망가진다. 사용자는 기능을 쓰려고 기부까지 강요당하고, 엔지니어는 나중에 "이 데이터가 학습에 써도 되는 데이터인지" 구분할 방법이 없어진다.

사내 dogfood(직원 테스트 기기)도 똑같이 다룬다. 직원이라도 회사 밖 대화(가족, 친구)가 녹음된다. dogfood 프로그램은 별도 동의서, 쉬운 일시정지, 짧은 TTL을 둔다(H8의 dogfood 기기 관리와 연결).

### 3.2 세분화(granular) 토글

예를 들어 앱에 이런 토글을 둔다(예시):

- 음성 명령 클립을 제품 개선에 사용 (기본 꺼짐)
- 사람이 내 클립을 들어도 됨 — 위 항목이 켜져 있을 때만 활성 (기본 꺼짐)
- 움직임 데이터(IMU) 연구 수집 (기본 꺼짐)
- 진단 telemetry 공유 (가명·집계)

각 토글은 **scope**(데이터 클래스 × 목적)로 내려간다. 예: `audio_clip:train`, `audio_clip:human_review`, `imu:research`.

### 3.3 동의 기록은 append-only ledger로, 그리고 데이터에 붙어 다닌다

동의 상태를 "users 테이블의 boolean 컬럼 하나"로 두면 안 된다. 질문은 언제나 두 개다.

1. **이 샘플을 수집했을 때** 사용자는 무엇에 동의한 상태였나? (수집 시점)
2. **지금** 사용자는 철회했나? (현재 시점)

그래서 동의는 **이벤트 로그(ledger)** 로 남긴다: `(user, 시각, grant/revoke, 정책 문서 버전, scopes)`. 덮어쓰지 않고 추가만 한다. 그리고 각 샘플의 메타데이터에 **수집 당시의 정책 버전**을 붙인다(H3의 메타데이터 스키마). 펌웨어 로그 레코드 헤더에 FW 버전을 넣는 것과 똑같다.

손으로 먼저 판정해 보자. 오늘이 30일째다.

- u1: 0일 grant(audio_clip, imu). 5일째 audio_clip → 수집 시점 범위 안, 현재도 유효 → **포함**.
- u2: 0일 grant(imu만). 5일째 audio_clip → 범위 밖 → **제외**.
- u3: 0일 grant, 20일 revoke. 5일째 샘플 → 수집 당시엔 유효했지만 **지금 철회** → 제외(그리고 삭제 대상, §9).
- u4: 10일 grant. 3일째 샘플 → 동의 전 수집 → **제외** (이런 샘플이 존재한다는 것 자체가 버그 신호다).
- u5: ledger에 없음 → 제외.

### 3.4 코드로 확인 — 동의 ledger + 데이터셋 빌더 필터

데이터셋 빌더(H5)가 학습 데이터를 뽑기 직전에 모든 샘플에 대해 "수집 시점 범위"와 "현재 철회 여부"를 둘 다 검사하는 코드다.

```python
# 동의 ledger(append-only) + 데이터셋 빌더 필터: 수집 시점의 동의 범위 + 현재 철회 여부를 둘 다 본다
import pandas as pd
ledger = pd.DataFrame([  # 사용자, 시각(일), 동작, 정책 버전, 허용 범위(scope)
    ("u1", 0,  "grant",  "v2", "audio_clip,imu"),
    ("u2", 0,  "grant",  "v2", "imu"),
    ("u3", 0,  "grant",  "v2", "audio_clip,imu"),
    ("u3", 20, "revoke", "v2", ""),               # u3: 20일째 철회
    ("u4", 10, "grant",  "v3", "audio_clip,imu"), # u4: 10일째 가입
], columns=["user", "day", "action", "policy", "scopes"])
samples = pd.DataFrame([(f"s{i:02d}", u, d, c) for i, (u, d, c) in enumerate([
    ("u1", 5, "audio_clip"), ("u1", 6, "imu"), ("u2", 5, "audio_clip"), ("u2", 7, "imu"),
    ("u3", 5, "audio_clip"), ("u3", 25, "imu"), ("u4", 3, "imu"), ("u4", 12, "audio_clip"),
    ("u5", 8, "imu")])], columns=["sample", "user", "day", "cls"])

def state_at(user, day):
    ev = ledger[(ledger.user == user) & (ledger.day <= day)].sort_values("day")
    if ev.empty or ev.iloc[-1].action == "revoke": return None
    return ev.iloc[-1]

def decide(s, today=30):
    if (cur := state_at(s.user, today)) is None:
        return "EXCLUDE: 현재 동의 없음/철회" if s.user in set(ledger.user) else "EXCLUDE: 동의 기록 없음"
    at = state_at(s.user, s.day)
    if at is None: return "EXCLUDE: 수집 시점에 동의 전"
    if s.cls not in at.scopes.split(","): return "EXCLUDE: 범위 밖 데이터"
    return f"INCLUDE (policy {at.policy})"
samples["decision"] = samples.apply(decide, axis=1)
print(samples.to_string(index=False))
print(samples.decision.str.split(" ").str[0].value_counts().to_dict())
```

```text
sample user  day        cls             decision
   s00   u1    5 audio_clip  INCLUDE (policy v2)
   s01   u1    6        imu  INCLUDE (policy v2)
   s02   u2    5 audio_clip    EXCLUDE: 범위 밖 데이터
   s03   u2    7        imu  INCLUDE (policy v2)
   s04   u3    5 audio_clip EXCLUDE: 현재 동의 없음/철회
   s05   u3   25        imu EXCLUDE: 현재 동의 없음/철회
   s06   u4    3        imu EXCLUDE: 수집 시점에 동의 전
   s07   u4   12 audio_clip  INCLUDE (policy v3)
   s08   u5    8        imu    EXCLUDE: 동의 기록 없음
{'EXCLUDE:': 5, 'INCLUDE': 4}
```

출력에서 볼 것: 9개 중 4개만 남고, 제외 이유가 **사유별로** 기록된다. 사유를 남기는 것이 중요하다. "동의 전 수집"(s06)이나 "동의 기록 없음"(s08)이 0이 아니면 그건 필터가 막아 준 것이 아니라 **상류 파이프라인(기기·ingest)의 버그**다. 이 카운트를 데이터 품질 대시보드(H7)에 경보로 건다.

실무 메모:
- 실제로는 샘플이 수억 개라 행 단위 `apply` 대신 ledger를 사용자별 구간(interval) 테이블로 만들어 join한다. 논리는 같다.
- scope 판정은 "데이터 클래스"만이 아니라 **목적**까지 본다. 같은 audio_clip이라도 `train`은 허용, `human_review`는 불허일 수 있다.
- 정책 문서가 v2 → v3으로 바뀌면서 목적이 넓어졌다면, v2 아래 수집된 데이터를 v3 목적에 쓰지 않도록 `min_policy` 조건을 건다.

### 3.5 철회는 파이프라인 전체로 전파된다

사용자가 앱에서 "데이터 기부 중단 + 지금까지 데이터 삭제"를 누르면 무슨 일이 일어나야 하나? H3(백엔드 lifecycle)의 관점에서 그리면 이렇다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="h6b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="120" width="120" height="56" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/>
  <text x="70" y="143" font-size="13" text-anchor="middle">사용자 "철회"</text>
  <text x="70" y="162" font-size="12" text-anchor="middle">앱 토글 · 삭제 요청</text>
  <rect x="170" y="120" width="140" height="56" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/>
  <text x="240" y="143" font-size="13" text-anchor="middle">consent service</text>
  <text x="240" y="162" font-size="12" text-anchor="middle">ledger append + 이벤트</text>
  <line x1="130" y1="148" x2="168" y2="148" stroke="currentColor" stroke-width="1.5" marker-end="url(#h6b)"/>
  <rect x="370" y="10" width="300" height="38" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="520" y="34" font-size="12" text-anchor="middle">① 기기: 동의 비트 OFF, 미전송 큐 삭제</text>
  <rect x="370" y="58" width="300" height="38" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="520" y="82" font-size="12" text-anchor="middle">② ingest: 해당 scope 새 업로드 거부</text>
  <rect x="370" y="106" width="300" height="38" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="520" y="130" font-size="12" text-anchor="middle">③ catalog: tombstone + 원본 삭제 작업</text>
  <rect x="370" y="154" width="300" height="38" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="520" y="178" font-size="12" text-anchor="middle">④ 데이터셋 빌더: 다음 빌드부터 제외 (H5)</text>
  <rect x="370" y="202" width="300" height="38" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="520" y="226" font-size="12" text-anchor="middle">⑤ 라벨링 큐: 미완료 작업 회수 (H4)</text>
  <rect x="370" y="250" width="300" height="38" rx="6" fill="none" stroke="#888" stroke-width="2" stroke-dasharray="5 4"/>
  <text x="520" y="274" font-size="12" text-anchor="middle">⑥ 백업 · 학습된 모델: 정책상 처리 (§9)</text>
  <line x1="310" y1="148" x2="368" y2="29" stroke="currentColor" stroke-width="1.2" marker-end="url(#h6b)"/>
  <line x1="310" y1="148" x2="368" y2="77" stroke="currentColor" stroke-width="1.2" marker-end="url(#h6b)"/>
  <line x1="310" y1="148" x2="368" y2="125" stroke="currentColor" stroke-width="1.2" marker-end="url(#h6b)"/>
  <line x1="310" y1="148" x2="368" y2="173" stroke="currentColor" stroke-width="1.2" marker-end="url(#h6b)"/>
  <line x1="310" y1="148" x2="368" y2="221" stroke="currentColor" stroke-width="1.2" marker-end="url(#h6b)"/>
  <line x1="310" y1="148" x2="368" y2="269" stroke="#888" stroke-width="1.2" stroke-dasharray="4 3" marker-end="url(#h6b)"/>
  <text x="170" y="220" font-size="12">각 단계는 처리 완료를</text>
  <text x="170" y="238" font-size="12">consent service에 ack</text>
  <text x="170" y="256" font-size="12">→ 기한 내 미완료면 경보</text>
</svg>
```

그림 1 — 철회 이벤트의 fan-out. 하나의 이벤트를 여러 소비자가 받아 각자 처리하고 완료를 보고한다. 점선(⑥)은 즉시 지울 수 없는 곳이라 별도 정책이 필요하다.

펌웨어 비유: 이건 **캐시 무효화**다. 원본(consent)이 바뀌었는데 사본(기기 큐, object store, 데이터셋, 라벨링 큐, 백업)이 여럿이다. 각 사본에 invalidate를 보내고 ack를 모은다. ack가 안 오면 타임아웃 경보 — 바로 Don이 아는 "명령 큐 + completion 확인" 패턴이다.

주의할 점:
- **기기 쪽 철회가 먼저**다. 서버에서 지워도 기기가 계속 올리면 소용없다. 기기가 오프라인이면 다음 연결 때 반영되므로, ingest 쪽 ②가 그 사이를 막는다.
- **학습이 끝난 모델**에서 특정 사용자 데이터의 영향을 정확히 지우는 문제(machine unlearning)는 아직 연구 주제다. 실무에서 흔한 정책은 "다음 정기 재학습부터 제외"이고, 그 정책을 사용자에게 어떻게 고지할지는 법무가 정한다.

### 3.6 펌웨어 쪽 게이트 — 저장 직전에 한 번 더

서버 필터만 믿지 말고, 기기에서도 **저장 직전에** 동의 비트를 검사한다. 앱이 BLE로 내려준 동의 상태를 flash에 두고, 로그 레코드 헤더에 정책 버전과 데이터 클래스를 넣는다(H1 로그 포맷과 연결).

```c
/* 펌웨어 쪽 동의 게이트: 로그 레코드 헤더에 정책 버전·데이터 클래스를 박고, 저장 직전에 동의 비트를 검사 */
#include <stdio.h>
#include <stdint.h>
enum data_class { DC_TELEMETRY = 0, DC_IMU = 1, DC_AUDIO_CLIP = 2, DC_TRANSCRIPT = 3 };
typedef struct {                 /* 앱이 BLE로 내려 주고 기기가 flash에 보관하는 동의 상태 */
    uint16_t policy_version;     /* 사용자가 동의한 정책 문서 버전 */
    uint8_t  scope_bits;         /* bit n = data_class n 허용 */
    uint8_t  revoked;            /* 1이면 모든 donation 중단 */
} consent_t;
typedef struct __attribute__((packed)) {
    uint32_t magic, seq;
    uint16_t policy_version;     /* 이 데이터가 어떤 동의 아래 수집됐는지 데이터와 함께 다닌다 */
    uint8_t  data_class, flags;
    uint32_t len;
} log_hdr_t;
static int may_persist(const consent_t *c, enum data_class dc, uint16_t min_policy) {
    if (dc == DC_TELEMETRY) return 1;                 /* 제품 운영 필수 항목(별도 고지 가정) */
    return !c->revoked && c->policy_version >= min_policy && ((c->scope_bits >> dc) & 1u);
}
int main(void) {
    consent_t c = { .policy_version = 3, .scope_bits = (1u << DC_IMU), .revoked = 0 };
    const char *names[] = {"telemetry", "imu", "audio_clip", "transcript"};
    for (int dc = 0; dc < 4; dc++) printf("%-10s -> %s\n", names[dc], may_persist(&c, (enum data_class)dc, 3) ? "저장" : "버림");
    c.revoked = 1;
    printf("철회 후 imu -> %s\n", may_persist(&c, DC_IMU, 3) ? "저장" : "버림");
    printf("sizeof(log_hdr_t) = %zu B\n", sizeof(log_hdr_t));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 consent_gate.c -o consent_gate && ./consent_gate
```

```text
telemetry  -> 저장
imu        -> 저장
audio_clip -> 버림
transcript -> 버림
철회 후 imu -> 버림
sizeof(log_hdr_t) = 16 B
```

출력에서 볼 것: IMU만 허용된 상태에서는 audio_clip·transcript가 **기기에서부터** 버려진다. 철회 비트가 서면 허용됐던 IMU도 멈춘다. 헤더에 `policy_version`이 들어 있으니 서버의 §3.4 필터가 수집 시점 동의를 검증할 수 있다. 16바이트 헤더는 H1의 로그 레코드 오버헤드 예산에 들어간다.

---

## 4. 가명화 — 해시 vs HMAC

### 4.1 정의 세 개

- **식별자(identifier)**: user ID, 기기 시리얼, 이메일, 전화번호, MAC 주소 등.
- **가명화(pseudonymization)**: 식별자를 다른 값으로 바꿔서, **추가 정보(키·매핑 테이블) 없이는** 사람을 직접 가리키지 못하게 하는 것. 추가 정보가 있으면 되돌리거나 연결할 수 있다. GDPR에서 가명 처리된 데이터는 여전히 개인 데이터로 취급된다.
- **익명화(anonymization)**: 어떤 합리적 수단으로도 사람을 다시 식별할 수 없는 상태. 센서 데이터에서는 매우 어렵다(§2.3).

말로 하면: **가명화는 "이름표를 번호표로 바꾸기"이지 "사람을 지우기"가 아니다.** 그래도 내부 분석가·라벨러가 실명을 볼 일을 없애고 유출 피해를 줄이므로 기본으로 한다.

### 4.2 평문 해시가 왜 위험한가 — 손계산

기기 시리얼이 `SN-` + 6자리 숫자라면 가능한 값은 10⁶ = 100만 개다. SHA-256은 단방향이지만, **입력 공간이 작으면 전부 해시해서 비교**하면 된다. Python으로 SHA-256을 초당 수백만 번 정도 계산할 수 있으므로 100만 개는 1초 안팎이다. 전화번호(국가별 10자리 내외), 이메일(유출 목록과 대조), MAC 주소(제조사 OUI를 알면 사실상 24비트)도 같은 운명이다.

### 4.3 코드로 확인 — 전수 대입 vs HMAC

```python
# 평문 SHA-256 "익명화"가 작은 ID 공간에서 왜 깨지는지: 6자리 기기 시리얼을 전수 대입
import hashlib, hmac, time
def sha(s): return hashlib.sha256(s.encode()).hexdigest()
leaked = sha("SN-482913")            # 데이터셋에 남은 "익명" 기기 ID
t0 = time.perf_counter()
for n in range(1_000_000):           # 공격자는 형식(SN- + 6자리)만 알면 된다
    if sha(f"SN-{n:06d}") == leaked:
        print("복원:", f"SN-{n:06d}", f"시도 {n+1:,}회")
        break
print(f"걸린 시간: {time.perf_counter()-t0:.2f} s (노트북 CPU 1코어, Python)")
# HMAC: 키(secret)를 모르면 같은 전수 대입이 불가능
key = bytes.fromhex("9f" * 32)       # 실제로는 KMS/secret manager에서 받는 32바이트 난수
def pseudo(s, k=key): return hmac.new(k, s.encode(), hashlib.sha256).hexdigest()[:16]
print("HMAC 가명:", pseudo("SN-482913"))
print("키 없이 맞춰 본 값 일치?", any(sha(f"SN-{n:06d}")[:16] == pseudo("SN-482913") for n in range(1000)))
```

```text
복원: SN-482913 시도 482,914회
걸린 시간: 0.29 s (노트북 CPU 1코어, Python)
HMAC 가명: 063910ea11fcbbba
키 없이 맞춰 본 값 일치? False
```

출력에서 볼 것: 평문 SHA-256 "익명 ID"는 0.3초 만에 원래 시리얼로 복원된다. HMAC 가명은 키를 모르는 공격자가 같은 후보를 해시해 봐도 맞지 않는다. 걸린 시간은 실행할 때마다 조금씩 다르다(실측값).

### 4.4 HMAC 가명화의 구조

**HMAC**(RFC 2104)은 키가 들어간 해시다: `HMAC(K, m) = H((K ⊕ opad) ‖ H((K ⊕ ipad) ‖ m))`. 말로 하면: **비밀 키를 섞어서 해시하므로, 키를 가진 쪽만 "이 ID의 가명이 뭔지" 계산할 수 있다.** 같은 ID는 같은 키 아래에서 항상 같은 가명이 되므로(결정적), 사용자 단위 join·split(H5)은 그대로 된다.

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="h6c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="24" font-size="13">평문 해시</text>
  <rect x="10" y="36" width="110" height="36" rx="5" fill="none" stroke="currentColor"/>
  <text x="65" y="59" font-size="12" text-anchor="middle">SN-482913</text>
  <line x1="120" y1="54" x2="168" y2="54" stroke="currentColor" stroke-width="1.5" marker-end="url(#h6c)"/>
  <rect x="170" y="36" width="100" height="36" rx="5" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="220" y="59" font-size="12" text-anchor="middle">SHA-256</text>
  <line x1="270" y1="54" x2="318" y2="54" stroke="currentColor" stroke-width="1.5" marker-end="url(#h6c)"/>
  <rect x="320" y="36" width="110" height="36" rx="5" fill="none" stroke="currentColor"/>
  <text x="375" y="59" font-size="12" text-anchor="middle">"익명" ID</text>
  <text x="450" y="50" font-size="12" fill="#d0564a">공격자: 10⁶개 후보를 같은</text>
  <text x="450" y="68" font-size="12" fill="#d0564a">SHA-256에 넣어 비교 → 복원</text>
  <line x1="10" y1="100" x2="670" y2="100" stroke="#888" stroke-dasharray="4 4"/>
  <text x="10" y="128" font-size="13">HMAC 가명화</text>
  <rect x="10" y="140" width="110" height="36" rx="5" fill="none" stroke="currentColor"/>
  <text x="65" y="163" font-size="12" text-anchor="middle">SN-482913</text>
  <rect x="170" y="196" width="100" height="28" rx="5" fill="none" stroke="#e08a3c" stroke-width="2"/>
  <text x="220" y="215" font-size="12" text-anchor="middle">키 K (KMS)</text>
  <line x1="220" y1="196" x2="220" y2="178" stroke="#e08a3c" stroke-width="1.5" marker-end="url(#h6c)"/>
  <line x1="120" y1="158" x2="168" y2="158" stroke="currentColor" stroke-width="1.5" marker-end="url(#h6c)"/>
  <rect x="170" y="140" width="100" height="36" rx="5" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="220" y="163" font-size="12" text-anchor="middle">HMAC-SHA256</text>
  <line x1="270" y1="158" x2="318" y2="158" stroke="currentColor" stroke-width="1.5" marker-end="url(#h6c)"/>
  <rect x="320" y="140" width="110" height="36" rx="5" fill="none" stroke="currentColor"/>
  <text x="375" y="163" font-size="12" text-anchor="middle">가명 p1_063910…</text>
  <text x="450" y="154" font-size="12" fill="#3f9a6b">공격자: K가 없으면 후보의</text>
  <text x="450" y="172" font-size="12" fill="#3f9a6b">가명을 계산할 수 없음</text>
</svg>
```

그림 2 — 평문 해시와 HMAC 가명화의 차이. 안전성의 근거가 "해시 함수"에서 "키의 비밀성"으로 옮겨 간다. 그러므로 키 관리가 곧 가명화의 보안이다.

설계 규칙:
- **키는 환경별로 다르게**(prod, staging, dev). staging 데이터가 유출돼도 prod 가명과 연결되지 않는다.
- 키는 KMS·secret manager에 두고, 가명화는 **ingest 경계의 한 서비스**에서만 한다. 분석가·ML 엔지니어는 가명만 본다.
- 가명에 **키 버전 접두어**(`p1_`, `p2_`)를 붙여 어떤 키로 만든 가명인지 알 수 있게 한다.
- salt와 헷갈리지 말 것: 비밀번호 저장용 salt는 사용자마다 달라서 같은 값도 다른 해시가 된다. 가명화는 join이 필요하므로 **전역 비밀 키(pepper)** 를 쓴다. 비밀번호 저장은 별개 문제(bcrypt, scrypt, Argon2 같은 느린 KDF)다.

### 4.5 키 회전의 결과

키는 언젠가 바꿔야 한다(유출 의심, 정기 정책). 그런데 HMAC 가명은 키가 바뀌면 **전부 바뀐다**. 회전 전후 데이터가 같은 사람이라는 걸 잃는다.

```python
# 환경별 키 + 키 회전: 가명이 어떻게 바뀌고, join이 어디서 끊기는지
import hashlib, hmac
def key(label): return hashlib.sha256(label.encode()).digest()   # 데모용 고정 키 (실제: KMS 난수)
KEYS = {("prod", 1): key("prod-v1"), ("prod", 2): key("prod-v2"), ("staging", 1): key("stg-v1")}
def pseudo(uid, env, ver):
    tag = hmac.new(KEYS[(env, ver)], uid.encode(), hashlib.sha256).hexdigest()[:12]
    return f"{env[0]}{ver}_{tag}"        # 키 버전을 접두어로 붙여 둔다
for uid in ["user-0001", "user-0002"]:
    print(uid, "->", pseudo(uid, "prod", 1), pseudo(uid, "prod", 2), pseudo(uid, "staging", 1))
# 회전 전(v1)에 모인 로그와 회전 후(v2) 로그를 사용자 단위로 묶어 보기
old = {pseudo(u, "prod", 1): 10 for u in ["user-0001", "user-0002"]}
new = {pseudo(u, "prod", 2): 5 for u in ["user-0001", "user-0002"]}
print("v1∩v2 공통 가명 수:", len(old.keys() & new.keys()))
# 해결책 1: 키 서비스 안에서만 v1->v2 재매핑 테이블을 만든다 (원본 uid는 밖으로 안 나감)
remap = {pseudo(u, "prod", 1): pseudo(u, "prod", 2) for u in ["user-0001", "user-0002"]}
merged = {}
for p, n in old.items(): merged[remap[p]] = merged.get(remap[p], 0) + n
for p, n in new.items(): merged[p] = merged.get(p, 0) + n
print("재매핑 후 사용자별 샘플 수:", merged)
```

```text
user-0001 -> p1_cb99b889fad8 p2_f7900d912307 s1_51c2718e57f6
user-0002 -> p1_cb81a21e8323 p2_b107a4c02bd6 s1_37e777dbd6c1
v1∩v2 공통 가명 수: 0
재매핑 후 사용자별 샘플 수: {'p2_f7900d912307': 15, 'p2_b107a4c02bd6': 15}
```

출력에서 볼 것: 같은 `user-0001`이 prod v1, prod v2, staging에서 서로 다른 가명이 된다. 회전 전후 공통 가명은 0개라 사용자 단위 split(H5)이 깨진다(같은 사람이 train과 test에 동시에 들어가는 leakage 위험). 키 서비스 내부에서만 v1→v2 재매핑 테이블을 만들어 이관하면 사용자별 샘플 수(10 + 5 = 15)가 복원된다.

트레이드오프 정리:

| 선택 | 장점 | 단점 |
|---|---|---|
| 회전하지 않음 | join 영구 유지 | 키 유출 시 전 기간 데이터가 연결됨 |
| 회전 + 재매핑 | 연속성 유지 | 재매핑 순간 키 두 개가 함께 쓰임 → 그 서비스가 가장 민감한 지점 |
| 회전 + 의도적 단절 | 기간별로 사람이 연결 안 됨 (오히려 프라이버시 기능) | 장기 사용자 분석 불가 |

"의도적 단절"은 실제로 쓰인다. 예를 들어 telemetry는 **90일마다 가명을 새로** 뽑아 장기 추적을 불가능하게 만들 수 있다. 목적에 장기 추적이 필요 없다면 이것이 minimization이다.

---

## 5. 온디바이스 최소화 — raw 대신 무엇을 남길까

### 5.1 손계산 — 하루치 오디오는 얼마인가

16 kHz, 16-bit mono = 32,000 B/s. 하루 깨어 있는 16시간 = 57,600 s.

```
상시 raw 녹음        : 32,000 × 57,600            = 1,843,200,000 B ≈ 1.84 GB
VAD-gated (말소리 30%) : 1.84 GB × 0.3                ≈ 553 MB
프레임 특징 (40밴드 fp16, 100 fps, 말소리 구간)
                     : 40 × 2 × 100 = 8,000 B/s × 57,600 × 0.3 ≈ 138 MB
keyword 클립만 (하루 20회 × 3.5 s)
                     : 20 × 3.5 × 32,000           = 2,240,000 B ≈ 2.24 MB
발화 단위 통계만 (2,000개 × 80 B)
                     : 2,000 × 80                  = 160,000 B = 160 KB
```

말로 하면: 무엇을 남기냐에 따라 **네 자릿수**가 달라진다. 이건 저장(H1)·전송(H2)·배터리 예산이기 전에 프라이버시 예산이다. 남긴 바이트가 곧 유출될 수 있는 바이트다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <line x1="200" y1="225" x2="620" y2="225" stroke="currentColor"/>
  <line x1="200" y1="225" x2="200" y2="30" stroke="currentColor"/>
  <line x1="200" y1="225" x2="200" y2="230" stroke="currentColor"/><text x="200" y="246" font-size="12" text-anchor="middle">100 KB</text>
  <line x1="284" y1="225" x2="284" y2="230" stroke="currentColor"/><text x="284" y="246" font-size="12" text-anchor="middle">1 MB</text>
  <line x1="368" y1="225" x2="368" y2="230" stroke="currentColor"/><text x="368" y="246" font-size="12" text-anchor="middle">10 MB</text>
  <line x1="452" y1="225" x2="452" y2="230" stroke="currentColor"/><text x="452" y="246" font-size="12" text-anchor="middle">100 MB</text>
  <line x1="536" y1="225" x2="536" y2="230" stroke="currentColor"/><text x="536" y="246" font-size="12" text-anchor="middle">1 GB</text>
  <line x1="620" y1="225" x2="620" y2="230" stroke="currentColor"/><text x="620" y="246" font-size="12" text-anchor="middle">10 GB</text>
  <text x="410" y="264" font-size="12" text-anchor="middle">하루 보존량 (로그 스케일)</text>
  <text x="192" y="52" font-size="12" text-anchor="end">상시 raw 녹음</text>
  <rect x="200" y="38" width="358.3" height="22" fill="#d0564a"/><text x="563" y="54" font-size="12">1.84 GB</text>
  <text x="192" y="90" font-size="12" text-anchor="end">VAD-gated raw (30%)</text>
  <rect x="200" y="76" width="314.4" height="22" fill="#e08a3c"/><text x="519" y="92" font-size="12">553 MB</text>
  <text x="192" y="128" font-size="12" text-anchor="end">프레임 특징 (40밴드)</text>
  <rect x="200" y="114" width="263.8" height="22" fill="#e08a3c"/><text x="469" y="130" font-size="12">138 MB</text>
  <text x="192" y="166" font-size="12" text-anchor="end">keyword 클립만</text>
  <rect x="200" y="152" width="113.4" height="22" fill="#3f9a6b"/><text x="318" y="168" font-size="12">2.24 MB</text>
  <text x="192" y="204" font-size="12" text-anchor="end">발화 단위 통계만</text>
  <rect x="200" y="190" width="17.1" height="22" fill="#3f9a6b"/><text x="222" y="206" font-size="12">160 KB</text>
</svg>
```

그림 3 — 보존 정책별 하루 오디오 데이터량(§5.1 손계산). 로그 축이라 막대 한 칸(84 px)이 10배다. 색은 아래 §5.2에서 말하는 "되살릴 수 있는 정도"의 대략적 위험도다.

### 5.2 특징만 남기면 안전한가? — 무엇을 버렸는지 보자

ML 팀에 "raw 대신 log-mel 특징을 보내자"고 하면 흔히 "특징은 소리가 아니니 안전하다"는 말이 나온다. **절반만 맞다.** 무엇을 버렸는지 따져야 한다.

| 표현 | 버린 것 | 남은 것 | 되살리기 |
|---|---|---|---|
| raw PCM | 없음 | 전부 | 그대로 재생 |
| STFT 크기 | 위상 | 시간·주파수 세밀한 모양 | 위상 추정(Griffin-Lim)으로 꽤 복원 |
| 프레임별 log-mel (40~80밴드) | 위상, 세밀한 주파수 | "언제 어떤 음색" | **neural vocoder가 바로 이 입력에서 음성을 만든다** (TTS 구조, B5 §7) |
| 발화 단위 통계 (평균·분산) | 시간 축 전체 | 평균 음색 | 단어 순서는 사라짐. 단 화자·환경 특성은 남을 수 있음 |
| 화자 embedding | 거의 모든 내용 | 누구인지 | 내용 복원은 어렵지만 **그 자체가 생체 식별자** |

실험으로 감을 잡자. 합성 "음절" 5개로 된 1초 신호를 만들고, 각 표현에서 **단순한 역변환**(Griffin-Lim 50회, mel은 pseudo-inverse)으로 되살린 뒤 원본과 log-spectrogram 모양을 비교한다. "시간패턴" 상관은 주파수별 평균을 뺀 뒤의 상관으로, **언제 무슨 소리가 났는지**가 남았는지를 본다.

```python
# raw 오디오 vs 특징: 크기, 그리고 "거꾸로 얼마나 되살릴 수 있나"를 단순 역변환으로 비교 (합성 신호)
import numpy as np, scipy.signal as ss
fs, rng = 16000, np.random.default_rng(0)
t = np.arange(fs) / fs; x = np.zeros(fs)
for k, f0 in enumerate([120, 150, 110, 170, 130]):          # 음절 5개 (200 ms씩), 배음 + 포먼트 모양 envelope
    seg = slice(k * 3200, k * 3200 + 3200)
    for h in range(1, 30):
        x[seg] += np.exp(-((h * f0 - 500 - 300 * k) / 400) ** 2) * np.sin(2 * np.pi * h * f0 * t[seg])
x = x / np.abs(x).max() * 0.5 + 0.003 * rng.standard_normal(fs)
stft = lambda y: ss.stft(y, fs, nperseg=512, noverlap=352)[2]          # hop 160 = 10 ms
S = np.abs(stft(x))
mel = np.maximum(0, 1 - np.abs(np.linspace(0, 40, 257)[None, :] - np.arange(40)[:, None]))  # 40밴드 삼각 필터 (mel 대신 선형 간격)
M = mel @ S                                                            # 40 × frames
def griffin_lim(mag, n=50):
    ang = np.exp(2j * np.pi * rng.random(mag.shape))
    for _ in range(n):
        y = ss.istft(mag * ang, fs, nperseg=512, noverlap=352)[1][:fs]
        ang = np.exp(1j * np.angle(stft(y)))
    return y
def score(y):  # (전체 log-spectrogram 상관, 시간 변화 패턴만의 상관) — 1이면 같은 모양
    a, b = np.log(S + 1e-4), np.log(np.abs(stft(y)) + 1e-4)
    da, db = a - a.mean(1, keepdims=True), b - b.mean(1, keepdims=True)   # 주파수별 평균을 빼면 "언제 무엇이" 남는다
    return np.corrcoef(a.ravel(), b.ravel())[0, 1], np.corrcoef(da.ravel(), db.ravel())[0, 1]
cands = {"STFT 크기(위상 버림)": S, "40밴드/프레임": np.linalg.pinv(mel) @ M,
         "밴드 평균만(시간 축 버림)": np.repeat(np.linalg.pinv(mel) @ M.mean(1, keepdims=True), S.shape[1], 1)}
print(f"{'raw int16 1 s':<16}: {fs*2:>6} B | 전체 1.000, 시간패턴 1.000")
for name, mag in cands.items():
    nb = {0: S.size, 1: M.size, 2: 40}[list(cands).index(name)] * 2   # fp16 저장 가정
    c_all, c_time = score(griffin_lim(np.maximum(mag, 0)))
    print(f"{name:<16}: {nb:>6} B | 전체 {c_all:.3f}, 시간패턴 {c_time:.3f}")
```

```text
raw int16 1 s   :  32000 B | 전체 1.000, 시간패턴 1.000
STFT 크기(위상 버림)  :  51914 B | 전체 0.990, 시간패턴 0.980
40밴드/프레임        :   8080 B | 전체 0.908, 시간패턴 0.819
밴드 평균만(시간 축 버림) :     80 B | 전체 0.706, 시간패턴 0.008
```

출력에서 볼 것:
- STFT 크기는 위상을 버렸는데도 시간패턴 0.98 — 사실상 원본이다(바이트는 오히려 raw보다 크다: 257 bins × 101 frames, overcomplete).
- 40밴드 프레임 특징은 **가장 단순한 역변환만으로도** 시간패턴 0.82가 남는다. 학습된 vocoder를 쓰는 공격자는 이보다 훨씬 잘한다. 그러므로 **프레임 단위 mel 특징은 "오디오에 준하는 데이터"로 분류**한다.
- 시간 축을 평균낸 통계는 시간패턴이 0.008 — 무엇이 언제 말해졌는지는 사라졌다. 전체 상관 0.71은 평균적인 음색(주파수 프로필)이 남았다는 뜻이다.

과장하지 말 것: 이 지표는 합성 신호에서의 **스펙트럼 모양 유사도**이지 "알아들을 수 있음(intelligibility)"을 잰 것이 아니다. 실제 판단은 음성 신호와 인간 청취·ASR 기반 평가로 해야 한다. 가져갈 결론은 하나다: **"특징이니 안전하다"가 아니라 "어느 축을 얼마나 버렸나"로 판단한다.**

### 5.3 keyword-gated 보존 — pre-roll 링버퍼

always-listening 기기의 오디오 보존 정책으로 가장 흔한 형태는 이렇다(B5의 VAD → KWS cascade 위에).

1. 마이크 프레임은 **짧은 RAM 링버퍼**(pre-roll, 예: 0.5 s)에만 들어간다. 오래된 프레임은 덮어써져 사라진다.
2. KWS가 wake word를 검출하면 pre-roll + 이후 명령 구간(예: 3 s)만 꺼내 처리한다.
3. keyword가 없으면 아무것도 flash·무선으로 나가지 않는다.

pre-roll이 필요한 이유: wake word의 시작 부분은 검출 시점보다 앞에 있다(KWS는 단어가 끝나야 확신한다). 서버에서 wake word를 재검증(second-stage)하려면 단어 전체가 필요하다.

```c
/* keyword-gated 보존: 0.5 s pre-roll 링버퍼, keyword 뒤 3 s만 flash로 commit. 나머지는 RAM에서 덮어써져 사라진다 */
#include <stdio.h>
#include <stdint.h>
#define FRAMES   6000u          /* 60 s, 10 ms 프레임 */
#define PREROLL  50u            /* 0.5 s */
#define POSTROLL 300u           /* 3 s */
static uint16_t ring[PREROLL];  /* 실제로는 프레임 데이터; 여기선 프레임 번호만 */
static unsigned head, count, committed, post_left;
static void commit(uint16_t f) { (void)f; committed++; }   /* flash write 자리 */
int main(void) {
    const unsigned kw_at[] = {1200u, 4100u};               /* keyword 검출 프레임 (B5 KWS 출력 가정) */
    unsigned speech = 0, k = 0;
    for (unsigned f = 0; f < FRAMES; f++) {
        int is_speech = (f % 1000u) < 400u;                /* 대화가 많은 환경: 40%가 말소리 */
        speech += (unsigned)is_speech;
        if (k < 2u && f == kw_at[k]) {                     /* keyword: pre-roll을 먼저 commit */
            for (unsigned i = 0; i < count; i++) commit(ring[(head + PREROLL - count + i) % PREROLL]);
            count = 0; post_left = POSTROLL; k++;
        }
        if (post_left) { commit((uint16_t)f); post_left--; continue; }
        ring[head] = (uint16_t)f; head = (head + 1u) % PREROLL;   /* 덮어쓰기 = 자동 삭제 */
        if (count < PREROLL) count++;
    }
    printf("전체 %u 프레임(%.0f s), 말소리 %u 프레임(%.0f s)\n", FRAMES, FRAMES / 100.0, speech, speech / 100.0);
    printf("flash에 남은 것: %u 프레임 = %.1f s (%.1f%%)\n", committed, committed / 100.0, 100.0 * committed / FRAMES);
    printf("RAM에 머문 최대 시간: %.1f s (pre-roll 길이)\n", PREROLL / 100.0);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 keyword_gate.c -o keyword_gate && ./keyword_gate
```

```text
전체 6000 프레임(60 s), 말소리 2400 프레임(24 s)
flash에 남은 것: 700 프레임 = 7.0 s (11.7%)
RAM에 머문 최대 시간: 0.5 s (pre-roll 길이)
```

출력에서 볼 것: 60초 중 24초가 말소리였지만 flash에 남은 건 keyword 두 번의 3.5 s × 2 = 7.0 s(11.7%)뿐이다. 나머지 17초의 대화는 **0.5초 이상 존재한 적이 없다.** "자동 삭제"를 별도 작업으로 하지 않고 **링버퍼 덮어쓰기라는 구조로** 보장한 것이 핵심이다. 펌웨어 엔지니어가 프라이버시에 가장 크게 기여할 수 있는 지점이 여기다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <text x="36" y="40" font-size="12" text-anchor="end">VAD</text>
  <rect x="40" y="28" width="40" height="18" fill="#888" opacity="0.6"/>
  <rect x="140" y="28" width="40" height="18" fill="#888" opacity="0.6"/>
  <rect x="240" y="28" width="40" height="18" fill="#888" opacity="0.6"/>
  <rect x="340" y="28" width="40" height="18" fill="#888" opacity="0.6"/>
  <rect x="440" y="28" width="40" height="18" fill="#888" opacity="0.6"/>
  <rect x="540" y="28" width="40" height="18" fill="#888" opacity="0.6"/>
  <text x="36" y="92" font-size="12" text-anchor="end">KWS</text>
  <line x1="160" y1="70" x2="160" y2="100" stroke="#d0564a" stroke-width="2.5"/>
  <text x="160" y="64" font-size="12" text-anchor="middle">12.0 s</text>
  <line x1="450" y1="70" x2="450" y2="100" stroke="#d0564a" stroke-width="2.5"/>
  <text x="450" y="64" font-size="12" text-anchor="middle">41.0 s</text>
  <text x="36" y="146" font-size="12" text-anchor="end">flash</text>
  <rect x="155" y="132" width="5" height="22" fill="#e08a3c"/>
  <rect x="160" y="132" width="30" height="22" fill="#3f9a6b"/>
  <rect x="445" y="132" width="5" height="22" fill="#e08a3c"/>
  <rect x="450" y="132" width="30" height="22" fill="#3f9a6b"/>
  <text x="200" y="148" font-size="12">pre-roll 0.5 s + 명령 3 s</text>
  <text x="490" y="148" font-size="12">총 7.0 s</text>
  <text x="40" y="190" font-size="12">나머지 말소리 17 s: RAM 링버퍼에서 0.5 s 뒤 덮어써짐 → 저장·전송된 적 없음</text>
  <line x1="40" y1="215" x2="640" y2="215" stroke="currentColor"/>
  <line x1="40" y1="215" x2="40" y2="220" stroke="currentColor"/><text x="40" y="236" font-size="12" text-anchor="middle">0</text>
  <line x1="140" y1="215" x2="140" y2="220" stroke="currentColor"/><text x="140" y="236" font-size="12" text-anchor="middle">10</text>
  <line x1="240" y1="215" x2="240" y2="220" stroke="currentColor"/><text x="240" y="236" font-size="12" text-anchor="middle">20</text>
  <line x1="340" y1="215" x2="340" y2="220" stroke="currentColor"/><text x="340" y="236" font-size="12" text-anchor="middle">30</text>
  <line x1="440" y1="215" x2="440" y2="220" stroke="currentColor"/><text x="440" y="236" font-size="12" text-anchor="middle">40</text>
  <line x1="540" y1="215" x2="540" y2="220" stroke="currentColor"/><text x="540" y="236" font-size="12" text-anchor="middle">50</text>
  <line x1="640" y1="215" x2="640" y2="220" stroke="currentColor"/><text x="640" y="236" font-size="12" text-anchor="middle">60</text>
  <text x="340" y="258" font-size="12" text-anchor="middle">시간 (s) — 위 C 예제의 시나리오를 그대로 그림 (1 s = 10 px)</text>
</svg>
```

그림 4 — keyword-gated 보존 타임라인. 회색은 말소리 구간, 빨간 선은 KWS 검출, 주황+초록만 flash에 남는다.

남는 위험과 완화책:
- **false wake**가 곧 의도치 않은 녹음이다. KWS의 false accept를 하루 몇 회로 묶는 것(B5)이 프라이버시 요구사항이 된다. 서버 쪽 second-stage 검증에서 "오작동"으로 판정된 클립은 **즉시 삭제**하고, 학습용 보관은 opt-in 사용자에 한정한다.
- 명령 구간에도 주변인 목소리가 섞인다. 명령 구간 길이를 VAD 끝점(end-of-speech)으로 짧게 자르면 섞이는 양이 줄어든다.
- 사용자가 "지금 무엇이 녹음되나"를 알 수 있는 표시(LED·소리)는 N4·G6 §2.7의 주제다. 가능하면 표시는 마이크 경로 상태와 하드웨어로 묶는다.

### 5.4 내용 redaction — ASR + PII 탐지 (개념)

클립을 보관해야 한다면 전사본에서 PII(이름, 전화번호, 주소, 카드 번호 등)를 찾아 가리고, 필요하면 해당 오디오 구간도 묵음 처리하는 방법이 있다. 구조는 `ASR → 단어별 타임스탬프 → PII 탐지(규칙 + NER 모델) → 텍스트 마스킹 + 오디오 구간 묵음`이다.

한계는 분명하다. ASR 오인식, PII 탐지의 recall이 100%가 아님, 맥락상 민감한 내용("어제 병원에서 들은 진단")은 PII 패턴이 아니어서 걸리지 않는다. 그러므로 redaction은 **다른 통제(동의 범위, 접근 제한, TTL)를 대체하지 않고 겹쳐 쓰는 한 겹**이다.

---

## 6. 차등 프라이버시 — 집계 telemetry를 안전하게

### 6.1 직관

fleet telemetry로 "오늘 false wake가 5회 넘은 기기 수"를 대시보드에 띄운다고 하자. 집계값은 개인 데이터가 아닌 것 같지만, 그룹이 작으면 집계에서 개인이 드러난다. 예: "베타 FW 7.3 사용자 중 해당 기기 수 = 7"이고 내일 한 명이 빠진 뒤 6이 되면, 빠진 사람의 값이 드러난다(differencing attack).

**차등 프라이버시(differential privacy, DP)** 의 약속: "**어느 한 사람이 데이터에 있든 없든, 공개되는 결과의 분포가 거의 같다.**" 그래서 결과를 보고 특정인에 대해 거의 배울 수 없다.

### 6.2 정의

```
메커니즘 M이 ε-DP  ⇔  한 사람만 다른 모든 이웃 데이터셋 D, D′와 모든 결과 집합 S에 대해
                     P[M(D) ∈ S] ≤ e^ε · P[M(D′) ∈ S]
```

말로 하면: **한 사람이 들어오거나 빠져도 어떤 결과가 나올 확률이 최대 e^ε배밖에 안 변한다.** ε이 작을수록 강한 보호, 그 대가로 노이즈가 크다. ε = 1이면 e¹ ≈ 2.72배, ε = 0.1이면 약 1.105배다.

**Laplace mechanism**: 질의 f의 민감도 Δ = (한 사람이 결과를 바꿀 수 있는 최대량)일 때, 결과에 Laplace(0, b = Δ/ε) 노이즈를 더하면 ε-DP다. count 질의는 한 기기가 count를 최대 1 바꾸므로 Δ = 1. Laplace(0, b)의 평균 절대오차는 정확히 b다.

손계산: Δ = 1, ε = 0.5 → b = 2 → 평균 ±2. 진짜 count 1234면 상대오차 2/1234 ≈ 0.16%, 진짜 count 7이면 2/7 ≈ 29%.

### 6.3 코드로 확인 — ε에 따른 오차

```python
# Laplace mechanism: "오늘 false wake가 5회 넘은 기기 수"를 ε-DP로 공개할 때 오차
import numpy as np
rng = np.random.default_rng(0)
true_count, small_group = 1234, 7          # 전체 fleet 집계 vs 특정 펌웨어 빌드처럼 작은 그룹
print(" ε    이론 MAE(1/ε)  실측 MAE   1234의 상대오차   7의 상대오차")
for eps in [0.1, 0.5, 1.0, 2.0, 5.0]:
    noise = rng.laplace(0.0, 1.0 / eps, size=100_000)     # 민감도 Δ=1 (한 기기가 바꿀 수 있는 count는 최대 1)
    mae = np.abs(noise).mean()
    print(f"{eps:4.1f}   {1/eps:10.2f}   {mae:8.2f}     {mae/true_count:10.2%}     {mae/small_group:10.1%}")
# 한 번 실제로 공개되는 값 (반올림·0 미만 clip은 후처리라 DP를 깨지 않는다)
for eps in [0.1, 1.0]:
    print(f"ε={eps}: 공개값 =", [max(0, round(true_count + rng.laplace(0, 1/eps))) for _ in range(5)])
```

```text
 ε    이론 MAE(1/ε)  실측 MAE   1234의 상대오차   7의 상대오차
 0.1        10.00       9.98          0.81%         142.6%
 0.5         2.00       1.99          0.16%          28.5%
 1.0         1.00       1.00          0.08%          14.3%
 2.0         0.50       0.50          0.04%           7.2%
 5.0         0.20       0.20          0.02%           2.9%
ε=0.1: 공개값 = [1242, 1240, 1228, 1232, 1249]
ε=1.0: 공개값 = [1233, 1235, 1235, 1235, 1232]
```

출력에서 볼 것: 실측 MAE가 이론값 1/ε과 일치한다. **같은 노이즈가 큰 집계에선 무시할 만하고(1234 → 1% 미만), 작은 그룹에선 값을 가린다(7 → ε=1에서도 14%).** 이것이 DP가 "큰 fleet 집계"에 잘 맞고 "작은 코호트 디버깅"엔 안 맞는 이유다. 반올림·음수 clip은 노이즈를 더한 뒤의 후처리라서 DP 보장을 깨지 않는다(post-processing 성질).

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <line x1="80" y1="300" x2="600" y2="300" stroke="currentColor"/>
  <line x1="80" y1="300" x2="80" y2="40" stroke="currentColor"/>
  <text x="80" y="318" font-size="12" text-anchor="middle">0.1</text>
  <text x="293.9" y="318" font-size="12" text-anchor="middle">0.5</text>
  <text x="386.1" y="318" font-size="12" text-anchor="middle">1</text>
  <text x="478.2" y="318" font-size="12" text-anchor="middle">2</text>
  <text x="600" y="318" font-size="12" text-anchor="middle">5</text>
  <text x="340" y="336" font-size="12" text-anchor="middle">ε (로그 축) — 오른쪽일수록 보호 약함, 정확도 높음</text>
  <text x="74" y="304" font-size="12" text-anchor="end">0.01%</text>
  <text x="74" y="252" font-size="12" text-anchor="end">0.1%</text>
  <text x="74" y="200" font-size="12" text-anchor="end">1%</text>
  <text x="74" y="148" font-size="12" text-anchor="end">10%</text>
  <text x="74" y="96" font-size="12" text-anchor="end">100%</text>
  <text x="74" y="44" font-size="12" text-anchor="end">1000%</text>
  <line x1="80" y1="144" x2="600" y2="144" stroke="#888" stroke-dasharray="5 4"/>
  <text x="596" y="138" font-size="12" text-anchor="end" fill="#888">상대오차 10%</text>
  <polyline points="80.0,200.7 133.9,209.9 172.1,216.4 226.0,225.6 293.9,237.1 338.7,244.7 386.1,252.7 440.0,261.9 478.2,268.4 532.1,277.6 600.0,289.1" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <polyline points="80.0,83.9 133.9,93.1 172.1,99.6 226.0,108.8 293.9,120.3 338.7,127.9 386.1,135.9 440.0,145.1 478.2,151.6 532.1,160.8 600.0,172.3" fill="none" stroke="#d0564a" stroke-width="2"/>
  <circle cx="80" cy="200.8" r="4" fill="#4a7bd0"/><circle cx="293.9" cy="237.4" r="4" fill="#4a7bd0"/><circle cx="386.1" cy="253.0" r="4" fill="#4a7bd0"/><circle cx="478.2" cy="268.7" r="4" fill="#4a7bd0"/><circle cx="600" cy="289.1" r="4" fill="#4a7bd0"/>
  <circle cx="80" cy="84.0" r="4" fill="#d0564a"/><circle cx="293.9" cy="120.3" r="4" fill="#d0564a"/><circle cx="386.1" cy="135.9" r="4" fill="#d0564a"/><circle cx="478.2" cy="151.4" r="4" fill="#d0564a"/><circle cx="600" cy="172.0" r="4" fill="#d0564a"/>
  <text x="250" y="196" font-size="12" fill="#4a7bd0">true count 1234 (fleet 전체)</text>
  <text x="250" y="86" font-size="12" fill="#d0564a">true count 7 (작은 코호트)</text>
</svg>
```

그림 5 — Laplace mechanism의 평균 상대오차 vs ε (선 = 이론 1/(ε·count), 점 = 예제 6 실측). 같은 ε에서 두 선의 거리는 count 비율(1234/7 ≈ 176배)로 일정하다.

### 6.4 예산(budget)과 합성(composition)

DP의 가장 중요한 운영 규칙: **질의를 여러 번 하면 ε이 쌓인다.** 기본 합성 정리에 따르면 ε₁-DP 질의와 ε₂-DP 질의를 같은 데이터에 하면 전체는 (ε₁ + ε₂)-DP다. 위 예제에서 같은 count를 5번 따로 공개한 것은 사실 ε을 5배 쓴 셈이고, 5개 평균을 내면 노이즈가 줄어 실제로 더 많은 정보가 새어 나간다. 매일 ε = 1로 30일 공개하면 같은 사람의 데이터가 매일 들어가는 한 30일 누적은 ε = 30까지 볼 수 있다(더 정교한 합성 정리로 줄일 수 있지만 개념은 같다).

그래서 운영에서는:
- 대시보드 질의를 미리 정해 두고(ad-hoc 질의 금지), **ε 예산 원장**을 둔다.
- 한 사람이 기여하는 양을 묶는다(contribution bounding — 예: 기기당 하루 1회 보고). 민감도 Δ는 이 묶음에서 나온다.

### 6.5 Local DP — randomized response로 기기에서 노이즈 넣기

위의 Laplace는 **서버가 진짜 값을 모은 뒤** 노이즈를 넣는다(central DP). 서버도 믿지 않으려면 **기기가 보고하기 전에** 노이즈를 넣는다(local DP). 1비트 값에 대한 고전적 방법이 **randomized response**다.

```
기기:   확률 p = e^ε / (1 + e^ε) 로 진실을 보고, 1 − p 로 뒤집어서 보고
서버:   보고 비율 λ 관측.  E[λ] = (2p − 1)·π + (1 − p)
        ⇒ π̂ = (λ − (1 − p)) / (2p − 1)
```

말로 하면: **각 보고는 "아마 진실"일 뿐이라 개인 응답은 부인 가능(plausible deniability)하지만, 많이 모으면 편향을 보정해 전체 비율은 알 수 있다.** ε = 1이면 p = e/(1+e) ≈ 0.731.

손계산: π = 0.18, p = 0.731 → E[λ] = 0.462 × 0.18 + 0.269 = 0.352. 서버가 λ = 0.352를 보면 π̂ = (0.352 − 0.269)/0.462 ≈ 0.18. 오차 표준편차는 √(λ(1−λ)/n)/(2p−1)이라 n = 10,000이면 √(0.352 × 0.648 / 10⁴)/0.462 ≈ 0.0103이다.

```python
# randomized response: 기기가 1비트("오늘 wake word 오작동을 겪었나?")를 노이즈 섞어 보고 → 서버는 비율만 복원
import numpy as np
rng = np.random.default_rng(1)
pi_true, eps = 0.18, 1.0
p = np.exp(eps) / (1 + np.exp(eps))                 # 진실을 말할 확률 (ε=1 → 0.731)
print(f"p(진실) = {p:.3f}")
for n in [100, 1_000, 10_000, 100_000]:
    errs = []
    for _ in range(200):                            # 같은 실험 200번 반복해서 오차 분포를 본다
        truth = rng.random(n) < pi_true
        keep = rng.random(n) < p
        report = np.where(keep, truth, ~truth)      # 1-p 확률로 뒤집어 보고
        lam = report.mean()
        est = (lam - (1 - p)) / (2 * p - 1)          # 편향 보정: E[λ] = (2p-1)π + (1-p)
        errs.append(est - pi_true)
    errs = np.array(errs)
    print(f"n={n:>7,}: 추정 평균 {pi_true + errs.mean():.4f}, 오차 표준편차 {errs.std():.4f}")
```

```text
p(진실) = 0.731
n=    100: 추정 평균 0.1818, 오차 표준편차 0.1048
n=  1,000: 추정 평균 0.1783, 오차 표준편차 0.0354
n= 10,000: 추정 평균 0.1797, 오차 표준편차 0.0088
n=100,000: 추정 평균 0.1799, 오차 표준편차 0.0033
```

출력에서 볼 것: 추정 평균은 항상 0.18 근처(편향 보정이 맞다). 오차 표준편차는 1/√n으로 줄어 n이 100배가 되면 10배 작아진다. n = 10,000의 실측 0.0088은 이론 0.0103과 같은 자릿수다(200회 반복이라 표본 변동이 있다). 노이즈 없는 이상적 조사보다 대략 2배 넓은 오차를 감수하는 대신, **서버는 어떤 기기의 진짜 비트도 확신하지 못한다.**

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <line x1="80" y1="300" x2="600" y2="300" stroke="currentColor"/>
  <line x1="80" y1="300" x2="80" y2="40" stroke="currentColor"/>
  <text x="80" y="318" font-size="12" text-anchor="middle">100</text>
  <text x="253.3" y="318" font-size="12" text-anchor="middle">1,000</text>
  <text x="426.7" y="318" font-size="12" text-anchor="middle">10,000</text>
  <text x="600" y="318" font-size="12" text-anchor="middle">100,000</text>
  <text x="340" y="336" font-size="12" text-anchor="middle">보고한 기기 수 n (로그 축)</text>
  <text x="74" y="304" font-size="12" text-anchor="end">0.001</text>
  <text x="74" y="199" font-size="12" text-anchor="end">0.01</text>
  <text x="74" y="94" font-size="12" text-anchor="end">0.1</text>
  <text x="30" y="170" font-size="12" text-anchor="middle" transform="rotate(-90 30 170)">추정 오차 표준편차</text>
  <polyline points="80.0,88.6 132.2,104.4 201.2,125.3 253.3,141.1 305.5,156.9 374.5,177.7 426.7,193.5 478.8,209.3 547.8,230.2 600.0,246.0" fill="none" stroke="#e08a3c" stroke-width="2"/>
  <polyline points="80.0,133.7 132.2,149.5 201.2,170.4 253.3,186.2 305.5,202.0 374.5,222.8 426.7,238.6 478.8,254.4 547.8,275.3 600.0,291.1" fill="none" stroke="#888" stroke-width="2" stroke-dasharray="6 4"/>
  <circle cx="80" cy="87.9" r="4" fill="#e08a3c"/><circle cx="253.3" cy="137.4" r="4" fill="#e08a3c"/><circle cx="426.7" cy="200.9" r="4" fill="#e08a3c"/><circle cx="600" cy="245.6" r="4" fill="#e08a3c"/>
  <text x="300" y="130" font-size="12" fill="#e08a3c">randomized response, ε = 1 (이론선 + 실측점)</text>
  <text x="140" y="240" font-size="12" fill="#888">노이즈 없는 조사 √(π(1−π)/n)</text>
</svg>
```

그림 6 — randomized response의 비용. 두 선은 평행(둘 다 1/√n)이고 그 간격이 local DP의 대가다. 같은 정확도를 얻으려면 대략 (주황/회색)² ≈ 5배의 기기가 필요하다.

### 6.6 어디에 쓰고 어디에 안 쓰나

| 쓰기 좋은 곳 | 맞지 않는 곳 |
|---|---|
| fleet 전체 비율·히스토그램 (기능 사용률, false wake 비율) | 특정 기기 디버깅 (그건 동의받은 진단 업로드로) |
| 단어·이모지 빈도 같은 대규모 사용 통계 (공개된 사례로 Google의 RAPPOR 연구가 있다) | 작은 코호트(베타 사용자 50명) 집계 |
| 공개 리포트·외부 공유용 집계 | 학습 데이터 원본 (DP 학습은 DP-SGD라는 별도 기법) |

함정: ε을 "1이면 안전, 10이면 위험"처럼 절대 기준으로 말하지 않는다. ε의 의미는 보호 단위(기기·사용자·하루), 합성, 위협 모델에 달려 있다. 면접에서는 "ε 값과 함께 **무엇이 이웃 데이터셋인지(unit of privacy)** 를 명시해야 한다"고 말하면 정확하다.

---

## 7. Federated learning과 온디바이스 학습 — 데이터 대신 업데이트를 보내기

### 7.1 개념

**Federated learning(FL)**: 원본 데이터는 기기에 두고, 각 기기가 로컬에서 모델을 조금 학습한 뒤 **가중치 업데이트(또는 gradient)만** 서버로 보낸다. 서버는 여러 기기의 업데이트를 평균(FedAvg)해서 전역 모델을 갱신한다. 키보드 다음 단어 예측 같은 모바일 사례가 공개 논문으로 알려져 있다.

```
라운드 r:  서버 ──전역 모델 w_r──▶ 기기 k (k = 1..K)
           기기 k: 로컬 데이터로 몇 step 학습 → Δw_k
           서버: w_{r+1} = w_r + ∑ (n_k / n) · Δw_k      (n_k = 기기 k의 샘플 수)
```

말로 하면: **데이터를 모으는 대신 "데이터가 모델을 어느 방향으로 밀었는지"만 모은다.** 온디바이스 개인화(예: 사용자 목소리에 맞춘 KWS threshold 조정, 화자 등록)는 FL의 더 단순한 형태로, 업데이트조차 기기 밖으로 안 나간다. 서버 모델 → 기기 모델 경로는 C5(distillation), 기기·클라우드 분담은 L3/L5와 연결된다.

### 7.2 한계 — FL은 "자동으로 private"가 아니다

- **업데이트도 정보를 흘린다.** gradient에서 학습 입력을 상당 부분 복원하는 공격 연구(예: "Deep Leakage from Gradients", 2019)가 있다. 그래서 실무 FL은 **secure aggregation**(서버가 개별 업데이트가 아닌 합만 볼 수 있게 하는 암호 프로토콜)과 **DP 노이즈**(업데이트 clip + 노이즈)를 함께 쓴다.
- **라벨이 없다.** 웨어러블 센서 데이터는 사용자가 라벨을 달아 주지 않는다. KWS처럼 "사용자가 다시 말함 = 이전 거부가 오답" 같은 약한 신호(H4 weak labeling)를 써야 한다.
- **기기 자원.** 학습은 추론보다 메모리(activation 저장, optimizer 상태)·전력이 훨씬 크다. 웨어러블이라면 충전 중 + Wi-Fi + 유휴일 때만 돌리는 스케줄이 필요하다(N1 전력 예산).
- **디버깅이 어렵다.** 데이터를 볼 수 없으니 "모델이 왜 나빠졌나"를 분석하기 어렵다. 그래서 FL을 쓰더라도 동의받은 소량의 중앙 평가 세트가 보통 필요하다.

결론: FL·온디바이스 학습은 **선택지 중 하나**이고, 웨어러블 1세대 제품이라면 "동의받은 data donation + 강한 최소화"가 현실적인 출발점일 가능성이 높다(추정).

---

## 8. 암호화와 접근 통제

### 8.1 세 군데의 암호화

| 위치 | 위협 | 수단 (개념) |
|---|---|---|
| 기기 저장(at rest) | 기기 분실·분해 후 flash 덤프 | flash/파일 암호화, 키는 SoC 내부(하드웨어 고유 키, secure element, TEE)에서 파생 |
| 전송(in transit) | BLE·Wi-Fi 도청, 중간자 | BLE 페어링 보안 + 앱–서버 TLS 1.3, 기기 인증서로 상호 인증 (H2) |
| 서버 저장(at rest) | 스토리지·백업 유출, 내부자 | 서버측 암호화 + 사용자/데이터셋별 키(envelope encryption), KMS |

대칭 암호의 실무 표준은 AES-GCM 같은 **AEAD**(authenticated encryption with associated data)다. 말로 하면: **한 번에 "남이 못 읽게(기밀성)"와 "누가 바꾸면 알게(무결성)"를 같이 준다.** 핵심 규칙은 같은 키로 nonce를 절대 재사용하지 않는 것이다(GCM에서 nonce 재사용은 치명적이다). 그리고 **암호를 직접 구현하지 않는다** — 검증된 라이브러리(mbedTLS, BoringSSL, 플랫폼 crypto API)와 하드웨어 crypto 엔진을 쓴다.

키 보관(개념, 칩마다 다름): 많은 SoC/MCU가 하드웨어 고유 키(HUK)나 eFuse 키, TrustZone 같은 TEE, 또는 별도 secure element를 제공한다. 공통 아이디어는 **키가 일반 CPU 메모리에 평문으로 나오지 않고**, 암호 연산을 키 저장소 쪽에 "요청"한다는 것이다. 구체 기능은 칩 벤더 문서로 확인해야 한다. Don의 factory provisioning 경험(기기별 키·인증서 주입)이 그대로 이어지는 영역이다.

### 8.2 envelope encryption과 crypto-shredding

서버에서는 키를 계층으로 둔다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="h6d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="250" y="12" width="180" height="40" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/>
  <text x="340" y="37" font-size="13" text-anchor="middle">root key (KMS / HSM)</text>
  <rect x="60" y="92" width="150" height="40" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="135" y="117" font-size="12" text-anchor="middle">data key p_a</text>
  <rect x="265" y="92" width="150" height="40" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="340" y="117" font-size="12" text-anchor="middle">data key p_b</text>
  <rect x="470" y="92" width="150" height="40" rx="6" fill="none" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 4"/>
  <text x="545" y="117" font-size="12" text-anchor="middle">data key p_c — 파기</text>
  <line x1="300" y1="52" x2="150" y2="90" stroke="currentColor" stroke-width="1.3" marker-end="url(#h6d)"/>
  <line x1="340" y1="52" x2="340" y2="90" stroke="currentColor" stroke-width="1.3" marker-end="url(#h6d)"/>
  <line x1="380" y1="52" x2="530" y2="90" stroke="currentColor" stroke-width="1.3" marker-end="url(#h6d)"/>
  <rect x="60" y="170" width="150" height="34" rx="5" fill="none" stroke="currentColor"/>
  <text x="135" y="192" font-size="12" text-anchor="middle">p_a 객체 (암호문)</text>
  <rect x="265" y="170" width="150" height="34" rx="5" fill="none" stroke="currentColor"/>
  <text x="340" y="192" font-size="12" text-anchor="middle">p_b 객체 (암호문)</text>
  <rect x="470" y="170" width="150" height="34" rx="5" fill="none" stroke="#888"/>
  <text x="545" y="192" font-size="12" text-anchor="middle" fill="#888">p_c 객체 + 백업 사본</text>
  <line x1="135" y1="132" x2="135" y2="168" stroke="currentColor" stroke-width="1.3" marker-end="url(#h6d)"/>
  <line x1="340" y1="132" x2="340" y2="168" stroke="currentColor" stroke-width="1.3" marker-end="url(#h6d)"/>
  <line x1="530" y1="140" x2="560" y2="162" stroke="#d0564a" stroke-width="2.5"/>
  <line x1="560" y1="140" x2="530" y2="162" stroke="#d0564a" stroke-width="2.5"/>
  <text x="340" y="236" font-size="12" text-anchor="middle">키 하나를 지우면, 그 키로 암호화된 모든 사본(백업 포함)이 읽을 수 없는 바이트가 된다</text>
</svg>
```

그림 7 — envelope encryption. root key는 data key를 감싸기(wrap)만 하고, 데이터는 data key로 암호화한다. p_c의 data key를 파기하는 것이 crypto-shredding이다 — SSD의 crypto erase와 같은 원리.

이 개념을 코드로 보자. 이 환경에는 `cryptography` 패키지가 없어서, **HMAC-SHA256을 카운터 모드 keystream으로 쓰는 교육용 장난감**으로 "암호화 + 무결성 태그 + 키 파기"의 흐름만 보여 준다. 실제 제품은 검증된 AES-GCM 구현을 쓴다.

```python
# crypto-shredding 개념: 사용자별 data key로 암호화 → 키만 지우면 백업 사본까지 못 읽는다
# 주의: HMAC-SHA256 카운터 모드 keystream은 "개념 설명용 장난감"이다. 실제 제품은 검증된 AES-GCM 라이브러리를 쓴다.
import hashlib, hmac
def keystream(key, nonce, n):
    out = b"".join(hmac.new(key, nonce + i.to_bytes(4, "big"), hashlib.sha256).digest() for i in range(n // 32 + 1))
    return out[:n]
def seal(key, nonce, pt):
    ct = bytes(a ^ b for a, b in zip(pt, keystream(key, nonce, len(pt))))
    tag = hmac.new(key, b"mac" + nonce + ct, hashlib.sha256).digest()[:16]   # 무결성 태그 (GCM의 tag 역할)
    return ct, tag
def open_(key, nonce, ct, tag):
    if not hmac.compare_digest(tag, hmac.new(key, b"mac" + nonce + ct, hashlib.sha256).digest()[:16]):
        raise ValueError("tag 불일치: 변조됐거나 키가 틀림")
    return bytes(a ^ b for a, b in zip(ct, keystream(key, nonce, len(ct))))
key_store = {"p_c": hashlib.sha256(b"demo data key for p_c").digest()}   # 실제: KMS가 감싸서(wrap) 보관
nonce = bytes(12)
ct, tag = seal(key_store["p_c"], nonce, b"transcript: call mom at 6pm")
backup = (ct, tag)                                                         # 30일 보관 백업에 같은 암호문이 복사됨
print("복호화:", open_(key_store["p_c"], nonce, *backup))
print("1비트 변조 검출:", end=" ")
try: open_(key_store["p_c"], nonce, bytes([ct[0] ^ 1]) + ct[1:], tag)
except ValueError as e: print(e)
del key_store["p_c"]                                                       # 삭제 요청 처리 = 키 파기
print("키 파기 후 백업 읽기 가능?", "p_c" in key_store, "| 암호문 앞 8바이트:", backup[0][:8].hex())
```

```text
복호화: b'transcript: call mom at 6pm'
1비트 변조 검출: tag 불일치: 변조됐거나 키가 틀림
키 파기 후 백업 읽기 가능? False | 암호문 앞 8바이트: 7fc12c4f35bc1694
```

출력에서 볼 것: 키가 있으면 복호화되고, 암호문 1비트만 바꿔도 태그 검사에서 걸린다(AEAD의 무결성). 키를 지운 뒤에는 백업에 남은 암호문이 그대로 있어도 읽을 방법이 없다. 백업 테이프를 하나하나 찾아 지우는 대신 **키 하나를 지운다**. 단, 키 자체가 KMS 백업에 남아 있으면 소용없으므로 키 저장소의 보관 정책도 함께 설계해야 한다.

### 8.3 접근 통제와 감사 로그

- **최소 권한(least privilege)**: ML 엔지니어는 가명화된 특징 데이터셋만, 라벨러는 할당된 클립만, raw 접근은 소수 역할 + 승인 절차(시간 제한 권한).
- **감사 로그(audit log)**: 누가 언제 어떤 객체를 읽었는지 append-only로 남기고, 감사 로그 자체는 별도 권한으로 보호한다. "이 사용자 데이터에 누가 접근했나"에 답할 수 있어야 한다.
- **목적 태그 기반 접근**: 데이터셋 manifest에 목적(`kws_train`)을 달고, 그 목적으로 승인된 job만 읽게 한다. §3의 purpose limitation을 접근 통제로 강제하는 방법이다.

---

## 9. 보관과 삭제

### 9.1 데이터 클래스별 TTL

H3에서 object storage의 lifecycle 규칙을 봤다. 여기서는 **정책**을 정한다(아래 값은 예시이지 권고가 아니다).

| 데이터 클래스 | TTL 예 | 근거(예시) |
|---|---|---|
| raw 명령 클립 (학습 opt-in) | 30일 | 특징 추출·라벨링이 끝나면 원본 불필요 |
| 전사본 (redacted) | 90일 | 오류 분석 주기 |
| IMU 세션 특징 | 1년 | 계절·버전 간 drift 비교 (H7) |
| 집계 telemetry | 2년 | 장기 신뢰성 추세, 가명 회전으로 개인 추적 불가 |
| 동의 ledger · tombstone | 법무가 정한 기간 | "동의를 받았다/지웠다"를 증명해야 하므로 오래 남는다 |

### 9.2 삭제 요청과 tombstone

삭제는 "파일 rm"이 아니다. 삭제한 사용자의 데이터가 **다시 들어오지 않게**(기기 큐에 남은 것, 재처리 job) 막아야 하므로 **tombstone**(가명 + 요청 시각만 남는 표식)을 catalog에 남긴다. 펌웨어 비유: FTL에서 trim된 LBA를 매핑 테이블에 "invalid"로 표시하는 것과 같다. 데이터는 GC 때 실제로 지워지지만, 표식이 있어서 다시 읽히지 않는다.

```python
# 데이터 클래스별 TTL + 삭제 요청 tombstone + 데이터셋 재빌드 후 "삭제된 사용자가 정말 없는지" 검증
import pandas as pd
TTL = {"raw_audio": 30, "transcript": 90, "imu": 365, "telemetry": 730}   # 일 단위 (예시 정책값)
catalog = pd.DataFrame([(f"obj{i:02d}", u, c, d) for i, (u, c, d) in enumerate([
    ("p_a", "raw_audio", 10), ("p_a", "imu", 10), ("p_b", "raw_audio", 70), ("p_b", "transcript", 70),
    ("p_c", "imu", 50), ("p_c", "raw_audio", 95), ("p_d", "telemetry", 5), ("p_d", "transcript", 20)])],
    columns=["obj", "user", "cls", "created"])
tombstones = {"p_c": 96}                    # p_c가 96일째 삭제 요청 (가명 + 요청 시각만 남긴다)
today = 100
def sweep(row):
    if row.user in tombstones: return "DELETE: 사용자 삭제 요청"
    if today - row.created > TTL[row.cls]: return f"DELETE: TTL {TTL[row.cls]}일 초과"
    return "KEEP"
catalog["action"] = catalog.apply(sweep, axis=1)
print(catalog.to_string(index=False))
kept = catalog[catalog.action == "KEEP"]
dataset_v8 = {"name": "kws-train-v8", "objects": sorted(kept.obj)}   # 재빌드된 데이터셋 manifest (H5)
leak = set(catalog.set_index("obj").loc[dataset_v8["objects"], "user"]) & tombstones.keys()
print("v8 objects:", dataset_v8["objects"])
print("삭제 사용자 잔존 검사:", "PASS" if not leak else f"FAIL {leak}")
```

```text
  obj user        cls  created             action
obj00  p_a  raw_audio       10 DELETE: TTL 30일 초과
obj01  p_a        imu       10               KEEP
obj02  p_b  raw_audio       70               KEEP
obj03  p_b transcript       70               KEEP
obj04  p_c        imu       50  DELETE: 사용자 삭제 요청
obj05  p_c  raw_audio       95  DELETE: 사용자 삭제 요청
obj06  p_d  telemetry        5               KEEP
obj07  p_d transcript       20               KEEP
v8 objects: ['obj01', 'obj02', 'obj03', 'obj06', 'obj07']
삭제 사용자 잔존 검사: PASS
```

출력에서 볼 것: TTL(30일)을 넘은 raw_audio와 삭제 요청한 p_c의 객체가 모두 DELETE로 분류되고, 재빌드된 데이터셋 manifest(`kws-train-v8`)에 p_c의 객체가 하나도 없음을 **자동 검사**로 확인한다. 이 검사를 데이터셋 빌드 CI의 필수 단계로 둔다(H5 데이터 버전 관리와 연결). 이전 버전 데이터셋(v7)에 p_c가 있었다면 v7을 계속 쓰는 학습 job을 막는 것도 정책에 포함한다.

### 9.3 삭제 검증과 백업

- **derived data까지**: 원본만 지우고 거기서 뽑은 특징·전사본·라벨·캐시를 남기면 삭제가 아니다. catalog에 **lineage**(이 객체가 어느 원본에서 왔는지)가 있어야 따라갈 수 있다(H3).
- **백업**: 백업은 보통 개별 객체를 지울 수 없다. 흔한 접근은 (1) 백업 보관 기간을 짧게 두고 만료로 지워지게 하면서, 복원 시 tombstone을 재적용하거나, (2) 사용자별 키 crypto-shredding(§8.2). 어느 쪽인지, 사용자에게 어떻게 고지할지는 법무와 정한다.
- **증빙**: "언제 요청, 언제 각 단계 완료"를 기록한다(§3.5의 ack). 증빙 기록에는 삭제된 데이터 내용이 아니라 가명과 시각만 담는다.

---

## 10. 사람이 데이터를 볼 때 — 라벨링과 human review

H4의 라벨링은 이 노트에서 가장 민감한 공정이다. 0.2절의 2019년 사례가 바로 이 지점이었다.

체크리스트:
1. **동의 범위**: `human_review` scope가 켜진 사용자 데이터만 라벨링 큐에 들어간다(§3.4 필터를 큐 생성에도 적용).
2. **사전 redaction**: 전사본 PII 마스킹, 가능하면 명령 구간만 잘라서 제공(앞뒤 주변 대화 제거).
3. **최소 노출**: 라벨러에게 사용자 가명조차 보여 줄 필요가 없다. 작업 단위 ID만 준다. 한 라벨러가 한 사용자의 클립을 대량으로 듣지 않게 섞는다.
4. **환경 통제**: 다운로드 금지, 승인된 도구 안에서만 재생, 접근 감사 로그.
5. **민감 내용 신고 경로**: 라벨러가 우연히 매우 사적인 내용(의료, 범죄 피해 등)을 들으면 라벨링 대신 "제외·삭제"로 보내는 버튼을 둔다.
6. **라벨러 계약·교육**: 기밀 유지 의무와 교육(이건 엔지니어 일은 아니지만 존재를 확인한다).

가능하면 사람 검토 자체를 줄인다. 예: wake word 오작동 판정은 서버 쪽 더 큰 KWS 모델로 먼저 자동 분류하고, 모델이 애매하다고 한 클립만 사람에게 보낸다(H4의 active learning이 프라이버시 도구이기도 하다).

---

## 11. 규제 지형 — 엔지니어가 알아 둘 정도 (법률 자문 아님)

> 다시 강조: 아래는 엔지니어 대화용 **높은 수준 요약**이고, 법 개정·지역 차이·해석에 따라 틀릴 수 있다. 제품의 법적 판단은 법무 전문가가 한다. 표의 오른쪽 열은 "엔지니어가 준비해 둘 기술적 능력"이지 "이렇게 하면 합법"이라는 뜻이 아니다.

| 규제 | 범위(대략) | 핵심 개념 | 엔지니어에게 의미하는 것 |
|---|---|---|---|
| GDPR (EU) | EU 내 개인의 개인 데이터 처리 | 처리 원칙(합법성, 목적 제한, 데이터 최소화, 보관 기간 제한 등), 정보주체 권리(열람, 정정, 삭제, 이동 등), 특수 범주 데이터(식별 목적의 생체 데이터, 건강 데이터 포함), data protection by design and by default | 사용자별 데이터 export·삭제 기능, 목적 태그, 보관 기간 자동화, 생체·건강 데이터 별도 취급, 가명 데이터도 개인 데이터로 다룸 |
| CCPA / CPRA (미국 캘리포니아) | 일정 기준 이상 사업자의 캘리포니아 소비자 개인정보 | 알 권리, 삭제 권리, 정정 권리, 판매·공유 opt-out, 민감 개인정보 사용 제한 | "이 사용자에 대해 무엇을 갖고 있나" 조회, 삭제 전파, 제3자 공유 경로 목록화 |
| BIPA (미국 일리노이) | 생체 식별자(voiceprint, 얼굴 기하 등) 수집 | 수집 전 서면 고지와 동의, 보관·파기 일정 공개, 사적 소송권으로 소송이 많았던 것으로 알려짐 | 화자 등록 전 명시적 동의 흐름, voiceprint 서버 저장 회피, 파기 일정 자동화 |
| HIPAA (미국) | 일반적으로 covered entity(의료 제공자, 건강보험 등)와 그 business associate의 건강 정보 | PHI 보호 규칙 | 소비자 웰니스 기기는 보통 직접 적용 대상이 아닐 수 있지만 의료기관과의 계약 관계에 따라 달라진다. 다른 소비자 건강 데이터 규칙(예: FTC 관련 규칙, 일부 주법)이 있을 수 있으니 법무 확인 |
| COPPA (미국) | 13세 미만 아동 대상 온라인 서비스 또는 아동임을 실제로 아는 경우 | 검증 가능한 부모 동의 | 연령 확인 흐름, 아동 계정에 대한 수집 기본 차단 |

기억할 패턴:
- 지역마다 법이 달라도 엔지니어가 만들 **기술적 능력은 거의 같다**: 데이터 인벤토리(무엇이 어디 있나), 목적 태그, 동의 ledger, 사용자 단위 조회·export·삭제, 보관 기간 자동화, 접근 감사. 이것을 처음부터 갖춘 파이프라인은 새 규제가 와도 설정으로 대응할 여지가 크다.
- 다른 미국 주들도 생체·소비자 건강 데이터에 대한 별도 법을 두고 있고 계속 늘고 있다. 출시 지역이 정해지면 법무에 목록을 요청한다.

---

## 12. 임베디드 관점에서 다시 보기

펌웨어·기기 쪽에서 프라이버시는 대부분 **구조적 보장**으로 구현된다. 정책 문서가 아니라 메모리 맵과 상태 머신이다.

| 펌웨어 결정 | 프라이버시 효과 | 비용 |
|---|---|---|
| 오디오 버퍼를 always-on 도메인 SRAM의 짧은 링버퍼로 (§5.3) | raw 오디오가 0.5 s 이상 존재하지 않음 | pre-roll 길이 = KWS 재검증 품질과 trade-off |
| KWS·VAD·화자 검증을 기기에서 (B5) | 대부분의 오디오가 기기를 떠나지 않음 | 모델 크기·전력 예산 (D, N1) |
| 로그 헤더에 data_class·policy_version (§3.6) | 서버 필터가 수집 시점 동의를 검증 가능 | 레코드당 수 바이트 |
| 저장 직전 동의 게이트 | 앱·서버 버그가 있어도 기기에서 1차 차단 | 동의 상태 동기화(BLE) 필요 |
| crash dump에서 오디오·센서 버퍼 영역 마스킹 | 디버그 경로로 새는 것 방지 | 링커 스크립트에 민감 영역 섹션 분리 |
| flash 암호화 + 하드웨어 키 | 분실 기기 덤프 방어 | crypto 엔진 처리량, 부팅 시간 |
| 녹음 표시 LED를 마이크/카메라 전원 경로와 하드웨어로 묶기 (G6 §2.7) | 소프트웨어가 몰래 켤 수 없음 | 회로 설계 단계 결정 |
| 미전송 큐에 TTL, 철회 시 즉시 erase | 오프라인 기기에 오래 쌓인 데이터 방지 | flash wear (H1) |

마지막 항목에서 flash erase는 Don에게 익숙한 함정이 있다: **논리적 삭제(매핑 해제)는 물리적 삭제가 아니다.** NAND/NOR에서 블록이 erase되기 전까지 데이터는 남는다. 그래서 기기 쪽도 "민감 데이터는 암호화해서 쓰고, 삭제 = 키 파기"(SSD crypto erase와 같은 원리)가 깔끔하다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 평문 SHA-256으로 "익명화" | 외부 연구자가 기기 시리얼을 복원 | 작은 ID 공간은 전수 대입 가능 | 환경별 비밀 키 HMAC, 키는 KMS (§4) |
| 동의를 boolean 컬럼 하나로 관리 | 철회 전 수집분 처리 기준이 없음, 감사 질문에 답 못 함 | 시점 정보 손실 | append-only ledger + 샘플에 정책 버전 (§3.3) |
| 제품 동작 동의와 데이터 기부 동의를 묶음 | 학습 데이터의 적법성 분리 불가 | 동의 설계 부재 | scope 분리, 기부는 기본 꺼짐 (§3.1) |
| "특징이니 안전" 가정 | 프레임 mel에서 음성 복원 가능 | 시간 축을 안 버림 | 프레임 특징은 오디오급으로 분류, 필요시 발화 단위 통계 (§5.2) |
| 철회가 원본만 지움 | 파생 특징·라벨·데이터셋에 잔존 | lineage 없음 | catalog lineage + 재빌드 검사 (§9) |
| 키 회전 후 사용자 split 깨짐 | 같은 사람이 train/test 양쪽에 → 성능 과대평가 | 가명이 전부 바뀜 | 키 버전 접두어 + 키 서비스 내부 재매핑, 또는 회전 경계로 split (§4.5) |
| DP 질의를 반복 실행 | ε 예산이 조용히 소진, 평균내기로 복원 | composition 무시 | 질의 사전 등록 + ε 원장 (§6.4) |
| crash dump에 오디오 DMA 버퍼 포함 | 진단 업로드에 대화 조각 | 메모리 전체 덤프 | 민감 섹션 분리·마스킹 (§12) |
| 라벨링 큐에 동의 필터 미적용 | opt-out 사용자 클립을 사람이 들음 | 필터가 학습 경로에만 있음 | 모든 소비 경로(학습, 라벨링, 분석)에 같은 필터 (§10) |
| 백업 무시한 삭제 | 복원 후 삭제 데이터 부활 | 백업은 불변 | 복원 시 tombstone 재적용 또는 crypto-shredding (§9.3) |

---

## 14. 면접에서 이렇게 말한다

**Q.** "How would you design data collection for an always-listening device to respect privacy?"

**A.** 원칙은 "기기에서 끝내고, 남기는 건 최소로, 동의는 데이터에 붙여서"다. 마이크 프레임은 0.5초 pre-roll 링버퍼에만 두고, 기기에서 VAD·KWS가 wake word를 확인했을 때만 그 구간을 처리한다 — 나머지는 덮어써져서 저장된 적이 없다. 학습용 보관은 별도 opt-in 사용자만, 클래스별 TTL과 함께. 로그 레코드에 데이터 클래스와 정책 버전을 넣고, 기기에서 저장 직전 동의를 검사하고, 서버 데이터셋 빌더가 한 번 더 걸러낸다. ID는 HMAC 가명화, 전송은 TLS, 사람 검토는 redaction 후 동의 범위 안에서만.

> "I'd make privacy structural rather than procedural. Audio lives only in a half-second pre-roll ring buffer on the device; only when on-device VAD and keyword spotting fire do we process that window, and everything else is overwritten without ever touching flash or the radio. Retaining clips for training is a separate opt-in with short per-class TTLs. Every log record carries its data class and the consent policy version it was collected under, the firmware checks consent right before persisting, and the dataset builder re-checks it server-side. IDs are pseudonymized with a keyed HMAC, transport is TLS, and human review only happens on redacted clips from users who opted into it."

**Q.** "What happens in the pipeline when a user revokes consent?"

**A.** 철회는 consent ledger에 이벤트로 추가되고, fan-out으로 모든 사본에 전파된다. 기기는 동의 비트를 끄고 미전송 큐를 지우고, ingest는 해당 scope 업로드를 거부하고, catalog는 tombstone을 남기고 원본·파생 데이터를 lineage로 따라가 지운다. 데이터셋 빌더는 다음 빌드부터 제외하고, 빌드 CI가 "삭제된 사용자가 manifest에 없는지" 자동 검사한다. 라벨링 큐에서도 회수한다. 백업은 짧은 보관 기간 + 복원 시 tombstone 재적용 또는 사용자별 키 파기로 처리하고, 이미 학습된 모델은 보통 다음 재학습부터 제외하는 정책 — 정확한 약속은 법무와 정한다. 각 단계가 ack를 보내고 기한 내 미완료면 경보.

> "Revocation is appended to an immutable consent ledger and fanned out as an event, like a cache invalidation with acknowledgements. The device clears its consent bit and erases its upload queue; ingestion rejects new data in that scope; the catalog writes a tombstone and deletes the raw objects plus everything derived from them via lineage; the dataset builder excludes the user from the next build, and CI asserts no tombstoned user appears in any manifest; pending labeling tasks are pulled. Backups expire on a short schedule with tombstones re-applied on restore, or we crypto-shred the user's data key. Already-trained models typically drop the data at the next retrain — that commitment is set with legal. Each stage acks, and missing acks page someone."

**Q.** "Hashing vs pseudonymization with HMAC — why?"

**A.** 해시는 공개 함수라서 입력 공간이 작으면 전수 대입으로 되돌린다. 6자리 시리얼 100만 개는 노트북에서 1초 안에 끝난다. HMAC은 비밀 키가 들어가서 키를 가진 서비스만 가명을 계산할 수 있고, 같은 키 아래 결정적이라 사용자 단위 join은 유지된다. 키는 환경별로 다르게 KMS에 두고, 가명에 키 버전을 붙인다. 다만 가명화는 익명화가 아니다 — 센서 데이터 자체가 지문일 수 있어서 여전히 개인 데이터로 다룬다.

> "A plain hash is a public function, so for a small identifier space you just hash every candidate — a million six-digit serials takes about a second. An HMAC mixes in a secret key, so only the service holding the key can compute pseudonyms, yet it's deterministic, so per-user joins and user-level splits still work. I'd use a separate key per environment, stored in a KMS, and prefix pseudonyms with the key version so rotation is manageable. And I'd be clear that pseudonymized isn't anonymized — gait or voice features can re-identify people on their own."

**Q.** "What's differential privacy and where would you use it?"

**A.** 한 사람이 데이터에 있든 없든 결과 분포가 최대 e^ε배만 달라지게 하는 보장이다. count 질의에 Laplace(1/ε) 노이즈를 더하면 되고, 평균 오차가 1/ε이라 fleet 전체 집계에서는 무시할 만하지만 작은 코호트는 가린다. 서버도 믿지 않으려면 기기에서 randomized response 같은 local DP를 쓴다 — 개인 응답은 부인 가능하지만 비율은 복원된다. 쓰는 곳은 기능 사용률, false wake 비율 같은 대규모 telemetry. 질의마다 ε이 누적되니 질의를 미리 등록하고 예산을 관리한다. 특정 기기 디버깅에는 맞지 않는다.

> "Differential privacy guarantees that adding or removing any one person changes the probability of any output by at most a factor of e to the epsilon. For a count, adding Laplace noise with scale one over epsilon does it; the expected error is one over epsilon, which is negligible for fleet-wide counts in the thousands but swamps a cohort of seven. If we don't want to trust the server, the device can apply local DP like randomized response before reporting. I'd use it for large-scale telemetry — feature usage, false-wake rates — with pre-registered queries and a privacy-budget ledger, since epsilon adds up across releases. It's the wrong tool for debugging an individual device."

**Q.** "Raw audio vs features — what do you store?"

**A.** 기본은 둘 다 최소로. 제품 동작용 오디오는 처리 후 삭제하고, 학습용은 opt-in keyword 클립만 짧은 TTL로. "특징이면 안전"은 틀렸다 — 프레임별 log-mel은 neural vocoder 입력과 같은 표현이라 음성에 준한다. 시간 축을 버린 발화 단위 통계나 집계는 내용 복원이 어렵지만, 화자 embedding은 내용이 없어도 그 자체가 생체 식별자다. 그래서 "어떤 축을 버렸나"로 판단하고, 저장 형태마다 데이터 클래스·TTL을 따로 단다. 용량으로 보면 상시 raw 1.8 GB/일 vs keyword 클립 수 MB/일로 수백 배 차이다.

> "I'd store as little of either as possible. Operational audio is deleted after processing; training audio is opt-in keyword clips with a short TTL. I don't treat features as automatically safe — frame-level log-mels are exactly what neural vocoders turn back into speech, so I classify them like audio. Utterance-level statistics that collapse the time axis are much harder to invert, while speaker embeddings carry no words but are biometric identifiers by design. So I judge a representation by which axes it throws away, and give each one its own data class and TTL. As a sense of scale: continuous raw audio is about 1.8 GB a day versus a few megabytes of keyword clips."

**Q.** "Is federated learning enough to make on-device data private?"

**A.** 원본을 안 올린다는 점에서 큰 진전이지만 충분하지 않다. gradient에서 입력을 복원하는 공격 연구가 있어서 secure aggregation과 DP 노이즈를 함께 써야 하고, 웨어러블 데이터는 라벨이 없고, 학습은 추론보다 메모리·전력이 훨씬 커서 충전 중에만 돌려야 하며, 데이터를 볼 수 없으니 디버깅이 어렵다. 1세대 제품이라면 동의받은 data donation과 강한 최소화가 현실적 출발점이고, FL은 개인화 같은 좁은 문제부터 붙인다.

> "It's a real improvement because raw data stays on the device, but it isn't sufficient on its own. Updates can leak training inputs, so production systems pair it with secure aggregation and clipped, noised updates. On a wearable we also have no labels, training costs far more memory and energy than inference so it only runs while charging, and debugging is hard when you can't see the data. For a first-generation device I'd start with consented data donation plus aggressive minimization, and introduce federated or on-device learning for narrow problems like personalization."

---

## 15. 직접 해보기

1. 기기 MAC 주소의 앞 3바이트(OUI)가 회사 고정값이면, 평문 SHA-256 "익명 MAC"을 전수 대입하는 데 필요한 후보 수는? 초당 200만 해시라면 최악 몇 초인가?
정답: 남은 3바이트 = 2²⁴ ≈ 1,677만 개 → 약 8.4초.

2. Laplace mechanism으로 count를 공개할 때 평균 상대오차를 5% 이하로 유지하고 싶다. ε = 0.5라면 true count가 최소 얼마여야 하나?
정답: MAE = 1/ε = 2 → 2/count ≤ 0.05 → count ≥ 40.

3. randomized response에서 ε = ln 3이면 p는? 서버가 λ = 0.40을 관측했다면 π̂는?
정답: p = 3/4 = 0.75, π̂ = (0.40 − 0.25)/0.5 = 0.30.

4. §3.4 코드에 "목적(purpose)" 축을 추가하라: ledger scopes를 `audio_clip:train,audio_clip:human_review` 형식으로 바꾸고, `decide(s, purpose)`가 목적별로 판정하게 하라. u1이 `train`만 허용했을 때 라벨링 큐 생성에서 s00이 빠지는지 확인하라.
힌트: `f"{s.cls}:{purpose}" in at.scopes.split(",")`.

5. §5.3의 C 코드에 "서버 second-stage가 false wake로 판정하면 해당 클립을 erase"를 흉내 내라: 두 번째 keyword(4100)를 false wake로 보고 committed 카운트를 되돌리는 경로를 만들고, 최종 보존량이 3.5 s가 되는지 확인하라.
힌트: commit 구간 시작 인덱스를 기억해 두고 판정 후 그 구간을 invalid 표시(FTL trim처럼).

6. §5.1처럼 하루 보존량을 계산하라: 하루 wake 50회, 명령 구간 평균 2.5 s, pre-roll 0.5 s, 16 kHz 16-bit. 그중 opt-in 사용자 비율이 10%이고 fleet가 10만 대라면 서버에 하루 쌓이는 클립 용량은?
정답: 50 × 3.0 × 32,000 = 4.8 MB/기기/일 → × 10,000대 = 48 GB/일.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| PII | Personally Identifiable Information | 사람을 직접·간접으로 식별할 수 있는 정보 |
| Data minimization | 데이터 최소화 | 목적에 필요한 최소만 수집·보관 |
| Purpose limitation | 목적 제한 | 밝힌 목적 밖으로 쓰지 않음 |
| Privacy by design / default | 설계·기본값 단계의 프라이버시 | 나중에 덧붙이지 않고, 기본 설정이 가장 보호적 |
| Bystander | 주변인 | 기기 사용자가 아니지만 녹음·촬영되는 사람 |
| Data donation | 데이터 기부 | 제품 동작과 별개로 개발 목적 데이터 제공에 동의 |
| Consent ledger | 동의 원장 | 동의·철회 이벤트를 시각·정책 버전과 함께 쌓는 append-only 로그 |
| Scope | 동의 범위 | 데이터 클래스 × 목적 단위의 허용 항목 |
| Pseudonymization | 가명화 | 추가 정보 없이는 식별 못 하게 식별자를 치환 (되돌릴 수 있음) |
| Anonymization | 익명화 | 합리적 수단으로 재식별 불가 — 센서 데이터에선 어려움 |
| HMAC | Hash-based Message Authentication Code | 비밀 키가 들어간 해시 (RFC 2104) |
| Pepper | 전역 비밀 값 | 모든 입력에 같은 비밀 키를 섞어 전수 대입을 막음 |
| Re-identification | 재식별 | ID가 지워진 데이터에서 사람을 다시 찾아냄 |
| Pre-roll | 사전 버퍼 | 트리거 직전 구간을 담아 두는 짧은 링버퍼 |
| Differential privacy | 차등 프라이버시 | 한 사람의 유무가 결과 분포를 e^ε배 이상 못 바꿈 |
| ε (epsilon) | 프라이버시 손실 파라미터 | 작을수록 강한 보호, 큰 노이즈 |
| Sensitivity Δ | 민감도 | 한 사람이 질의 결과를 바꿀 수 있는 최대량 |
| Composition | 합성 | 여러 DP 질의의 ε이 누적됨 |
| Local DP | 로컬 차등 프라이버시 | 데이터 소유자(기기)가 보고 전에 노이즈를 넣음 |
| Randomized response | 무작위 응답 | 확률 p로 진실, 1−p로 반대를 보고하는 local DP |
| Federated learning | 연합 학습 | 데이터 대신 모델 업데이트를 모아 학습 |
| Secure aggregation | 보안 집계 | 서버가 개별 업데이트가 아닌 합만 보게 하는 프로토콜 |
| AEAD | 인증 암호 | 기밀성과 무결성을 함께 주는 암호 모드 (예: AES-GCM) |
| Envelope encryption | 봉투 암호화 | data key로 데이터를, root key로 data key를 암호화 |
| Crypto-shredding | 암호학적 파기 | 키를 지워 암호문(백업 포함)을 무의미하게 만듦 |
| Tombstone | 삭제 표식 | 삭제된 대상의 가명·시각을 남겨 재유입을 막음 |
| TTL | Time To Live | 데이터 클래스별 자동 삭제 기한 |
| Lineage | 계보 | 파생 데이터가 어떤 원본에서 왔는지의 연결 |
| Audit log | 감사 로그 | 누가 언제 무엇에 접근했는지의 불변 기록 |

---

## 17. 요약 & 체크리스트

always-listening 웨어러블의 데이터 파이프라인은 기본값이 "모두 듣는다"이므로 프라이버시는 **구조로** 만들어야 한다. 데이터를 민감도별로 분류하고(raw 오디오·voiceprint·카메라가 최상위, IMU도 재식별 가능), 기기에서 끝낼 수 있는 일은 기기에서 끝내며, raw 오디오는 짧은 pre-roll 링버퍼에서 덮어써지게 한다. 동의는 append-only ledger로 남기고 정책 버전을 데이터에 붙여, 기기 저장 직전과 데이터셋 빌드 때 두 번 검사한다. 철회는 캐시 무효화처럼 모든 사본에 fan-out되고 ack로 확인한다. 식별자는 평문 해시가 아니라 키가 있는 HMAC으로 가명화하고, 집계 telemetry에는 DP를 쓰되 ε 예산과 작은 코호트의 한계를 안다. 암호화와 접근 통제, TTL, tombstone, crypto-shredding이 그 아래를 받친다. 법은 법무가 판단하고, 엔지니어는 그 판단을 강제하는 기술적 능력(인벤토리, 목적 태그, 사용자 단위 조회·삭제, 보관 자동화, 감사)을 미리 만들어 둔다.

- [ ] 웨어러블의 데이터 9종을 민감도와 기본 처리로 분류하고 그 이유를 말할 수 있다
- [ ] 제품 운영 동의와 data donation 동의를 왜 분리하는지 설명할 수 있다
- [ ] 동의 ledger로 "수집 시점 범위"와 "현재 철회"를 둘 다 판정하는 필터를 짤 수 있다
- [ ] 철회 이벤트가 기기·ingest·catalog·데이터셋·라벨링·백업으로 전파되는 흐름을 그릴 수 있다
- [ ] 평문 해시 ID를 전수 대입으로 복원하는 데 걸리는 시간을 자릿수로 계산할 수 있다
- [ ] HMAC 가명화의 키 관리와 키 회전의 결과(join 단절, 재매핑)를 설명할 수 있다
- [ ] pre-roll 링버퍼 기반 keyword-gated 보존을 C로 구현하고 하루 보존량을 계산할 수 있다
- [ ] 프레임 mel 특징, 발화 통계, 화자 embedding 각각이 무엇을 버리고 무엇을 남기는지 말할 수 있다
- [ ] Laplace mechanism의 오차(1/ε)와 randomized response의 편향 보정 식을 손으로 계산할 수 있다
- [ ] GDPR·CCPA/CPRA·BIPA·HIPAA·COPPA가 엔지니어에게 요구하는 기술적 능력을 법률 판단 없이 요약할 수 있다

## 참고 자료

- Regulation (EU) 2016/679 (GDPR) 원문 — https://eur-lex.europa.eu/eli/reg/2016/679/oj (제5조 원칙, 제9조 특수 범주, 제17조 삭제권, 제25조 data protection by design and by default)
- California Attorney General, "California Consumer Privacy Act (CCPA)" 안내 — https://oag.ca.gov/privacy/ccpa
- Illinois Biometric Information Privacy Act (740 ILCS 14) — 일리노이 주 의회 법령 원문
- U.S. HHS, HIPAA 안내 — https://www.hhs.gov/hipaa
- U.S. FTC, Children's Online Privacy Protection Rule (COPPA) 안내 — ftc.gov
- NIST Privacy Framework — https://www.nist.gov/privacy-framework
- Ann Cavoukian, "Privacy by Design: The 7 Foundational Principles" (2009)
- Cynthia Dwork, Aaron Roth, "The Algorithmic Foundations of Differential Privacy", Foundations and Trends in TCS, 2014
- Úlfar Erlingsson, Vasyl Pihur, Aleksandra Korolova, "RAPPOR: Randomized Aggregatable Privacy-Preserving Ordinal Response", ACM CCS 2014
- S. L. Warner, "Randomized Response: A Survey Technique for Eliminating Evasive Answer Bias", JASA, 1965
- H. Brendan McMahan et al., "Communication-Efficient Learning of Deep Networks from Decentralized Data", AISTATS 2017 (FedAvg)
- Keith Bonawitz et al., "Practical Secure Aggregation for Privacy-Preserving Machine Learning", ACM CCS 2017
- Ligeng Zhu, Zhijian Liu, Song Han, "Deep Leakage from Gradients", NeurIPS 2019
- RFC 2104 (HMAC), RFC 8446 (TLS 1.3), NIST SP 800-38D (GCM)
- D. Griffin, J. Lim, "Signal Estimation from Modified Short-Time Fourier Transform", IEEE TASSP, 1984 (위상 복원)
- 이 시리즈의 관련 노트: B5, G4, G6, H1, H2, H3, H4, H5, H7, H8, C5, L3, L5, N1, N4
