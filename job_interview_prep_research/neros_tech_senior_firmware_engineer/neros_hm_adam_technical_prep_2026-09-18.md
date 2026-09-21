# Neros HM 기술 인터뷰 (Adam Kibit, Director of Firmware) — 준비 노트

> **인터뷰 예정**: 2026-09-18 전후 (Devin 안내: "내일 모레") · 45분 · **상대**: Adam Kibit, Director of Firmware (hiring manager)
> **작성**: 2026-09-16 · 리크루터 콜 결과는 1번, 전체 조사는 `neros_tech_senior_firmware_engineer_context.md`, 코드 연습은 `practice/`
> 신뢰도: `[확인됨]` Devin 콜 또는 공개 프로필 · `[추정]` 추론 · `[Don 제공]` Don이 직접 찾은 정보, 미검증

---

## 0. TL;DR — Adam 인터뷰에서 이기는 법

- **Adam은 드론 사람이 아니라 "자동차 임베디드 플랫폼 아키텍트 출신 엔지니어링 리더"다.** 19년 중 대부분을 Johnson Controls·Visteon·Faraday Future·Canoo에서 **플랫폼 재사용, IPC 프로토콜, OTA/보안 업데이트, BSP+CI 빌드, EOL(양산) 테스트**를 했고, 그다음 스타트업 3곳에서 Head/Director를 했다. Neros엔 **2025-05**에 합류했다. [확인됨]
- 그래서 드론 도메인 지식보다 **① 플랫폼·아키텍처 판단력 ② 양산·테스트 감각 ③ 모호한 환경에서 혼자 굴러가는 시니어리티**를 볼 가능성이 크다. [추정]
- **Don의 최강 카드 3장** (Adam 이력과 1:1 대응):
  1. **새 실리콘 bring-up + 시스템 통합 루트코즈** (Apple · SSD) ↔ Adam의 BSP·플랫폼 통합 경험
  2. **공장 test-node 아키텍처 / 챔버 신뢰성 플랫폼** ↔ Adam의 **EOL Test Application** (JCI, 양산 throughput +20%)
  3. **NVMe telemetry 디버그 기능 + 다른 팀이 쓰는 SDK** ↔ Adam의 **IPC 프로토콜·모듈형 플랫폼 재사용** (JCI·Visteon)
- **팀 구조가 보내는 신호**: FW 12명 중 절반 가까이가 경력 5년 미만. 20년+ 시니어가 2~3명 있다. Director가 원하는 건 "코드도 짜면서 **주니어를 끌어올리고 구조를 잡아줄 사람**"일 가능성이 높다 → Principal 포지셔닝의 근거. [추정]
- **이번 45분의 목표**: 온사이트 초대. 기술 깊이를 보여주되 **Adam의 언어(platform, reuse, variants, EOL, OTA, CI)로 말할 것.**

---

## 1. 리크루터 콜 결과 정리 (2026-09-16, Devin, 30분) [확인됨]

### 1.1 Don이 말한 스토리 — **Adam 앞에서도 똑같이 유지할 것**
1. SK hynix(Solidigm 포함)에서 **7년 3개월**, 데이터센터 SSD 펌웨어 개발만 했다.
2. 다른 영역을 해보고 싶어 **consumer device**를 택해 Apple로 옮겼다.
3. Apple RF 쪽에서 **new silicon을 system에 integration**하는 일을 했고, 이전보다 훨씬 넓은 영역을 다뤘다.
4. 다만 조직이 **너무 세분화**돼 있고 팀 간 R&R과 사내 사정 때문에 **기여할 수 있는 범위에 한계**가 명확했다. 프로그램 속도가 빠른 건 좋았다.
5. 이번 주부터 새롭고 재밌는 스타트업을 찾다가, **1년 반 전 Neros 제3자 리크루팅 회사에서 온 메일**을 발견했다.
6. 회사를 공부해 보니 미션이 흥미롭고, **다양한 칩셋 bring-up 경험 + scalable manufacturing FW**로 기여할 수 있겠다고 판단해 **콜드메일**을 보냈다.

> ⚠️ 이 스토리가 Adam 인터뷰의 "Why Neros / Why leave Apple" 답의 **원본**이다. 표현을 바꿔도 사실관계는 그대로 두기.
> Apple 이야기는 "R&R 한계"까지만. **조직 불만이나 특정 팀 비판으로 넘어가지 말 것.**

### 1.2 Devin이 알려준 것
| 항목 | 내용 |
|---|---|
| 회사 규모 | 약 **300명**. 공장과 같은 건물이라 **절반 이상이 operator/technician** |
| FW 그룹 | **엔지니어 12명 + Director 1명(Adam)**. SW 그룹은 조금 더 크고, EE/HW 그룹 별도 [일부 추정] |
| 포지션 | "Principal/Senior Embedded Software Engineer". **특정 JD 없음** → 아직 역할이 정의되지 않은 req로 보임 [추정] |
| 다음 단계 | **Adam과 45분 기술 인터뷰** |
| 온사이트 (Adam 통과 시) | ① **Tour 30분** ② **내가 한 일 발표 1시간** (가장 큰 challenge, issue resolving/debugging, bring-up 경험) ③ **1:1 기술 인터뷰 여러 개**: 기본 C/C++, **ring buffer 구현**, **system design** |
| system design 성격 | 드론 개발자가 많지 않아서 **드론 설계가 아니라 generic한 문제** |
| 연봉 | Don이 "base **최소 $200K 이상**" 이라고 답함 |

### 1.3 이 결과가 바꾸는 것
- **JD가 없다 = 레벨과 역할을 Adam이 정한다.** 45분이 사실상 레벨링 인터뷰. Principal로 보일지 Senior로 보일지가 여기서 갈린다.
- 온사이트 1:1 주제가 **ring buffer + generic system design + C/C++** 로 확인됨 → `practice/01`, `04`, `05`, `06`과 Anduril `05_circular_buffer`가 정확히 맞다.
- 온사이트 **1시간 발표**가 있다 → Adam 인터뷰에서 꺼낸 스토리가 발표의 예고편이 돼야 한다. **이번 45분에서 스토리 3개를 시험 발사**하고 반응을 보자.
- 연봉 $200K는 Senior Platform 공고 밴드($195–273K) 하단이다. 이미 말한 숫자는 되돌리지 말고, **레벨이 Principal로 잡히면 그때 밴드 기준으로 다시 이야기**하면 된다.

---

## 2. Adam Kibit 프로필 분석 [확인됨 · LinkedIn 경력 페이지, ZoomInfo]

> 업무 경력만 정리했다. 인터뷰 대비 목적 외로 쓰지 말 것.

| 기간 | 회사 · 직함 | 인터뷰에 연결되는 포인트 |
|---|---|---|
| 2025-05 ~ 현재 | **Neros · Director of Firmware** (El Segundo, on-site) | FW 조직을 약 1년 반 동안 12명으로 키운 사람. 스케일 고통을 직접 겪는 중 |
| 2023-04 ~ 2025-05 | Skyline Mobility · Head of Software & Electronics | SW+전자 겸임 리더. HW/FW 경계에 익숙 |
| 2022-03 ~ 2023-03 | Xeal · Head of Engineering | 스타트업 엔지니어링 총괄, DevOps 포함 |
| 2018-01 ~ 2022-03 | **Canoo · Engineering Director, Digital Product** (Torrance) | EV 스타트업. 차량 SW/전자 리더십 |
| 2017-09 ~ 2017-12 | Aeris · Solutions Engineer | **vehicle-cloud MQTT 인터페이스 스펙**, **보안 멀티 차량 OTA 파이프라인** |
| 2015-12 ~ 2017-09 | **Faraday Future · SW Architect – Infotainment** | 디스플레이 11개 백본, **Phone-as-a-Key 리드(특허 여러 건)**, **BT/BLE/WiFi/LTE 멀티모달 통신 모델** |
| 2014-06 ~ 2015-12 | **Visteon · SW Architect – Telematics** | **모듈형 텔레매틱스 플랫폼 재설계 → 하위~상위 제품 재사용, ~25% 절감**, **PKI 기반 보안 모델** |
| 2005-05 ~ 2014-06 | **Johnson Controls** (9년 2개월): Test Eng → SW Eng → **Tech Lead** | **BSP를 Jenkins CI + 유닛테스트에 통합**, **IPC(프로세서 간 통신) 프로토콜 신규 구현 → 오류 제거, 전송률 +30%**, **메모리 분석 도구 → 대당 HW 원가 절감**, **EOL 테스트 앱 개선 → 양산 throughput +20%**, 자동 테스트 시스템(테스트 시간 −50%), BT A2DP/AVRCP 초기 구현 |
| 학력 | BS Computer Engineering (U. Michigan–Dearborn), MS CS (Georgia Tech) | |

### 2.1 Adam이 중요하게 여길 것으로 보이는 것 [추정]
1. **플랫폼 재사용과 변종 관리**: Visteon에서 "가장 싼 모델부터 최상위까지 재사용되는 모듈형 플랫폼"을 만들었다. Neros는 Archer·Archer AI·Bandit으로 제품이 늘고, CTO는 Board B(다른 칩셋 대체 보드)를 말한다. **정확히 같은 문제다.**
2. **프로세서 간 통신(IPC)과 프로토콜 설계**: JCI에서 직접 구현했다. MCU↔컴패니언↔라디오 구조인 드론에서 핵심이다.
3. **OTA·보안 업데이트·PKI**: Aeris·Faraday·Visteon 경력 전체에 걸쳐 있다. 야전 배치 드론에 그대로 적용된다.
4. **빌드/CI 인프라**: BSP를 CI에 올려 실패 빌드를 줄인 사람이다. Platform JD의 Bazel·hermetic build와 같은 결이다.
5. **양산 테스트(EOL)와 원가**: 테스트 엔지니어로 시작해 EOL throughput을 개선했고, 메모리 분석으로 BOM 원가를 줄였다. **주당 1,200대 공장 옆 FW 팀의 Director가 가장 공감할 주제.**
6. **리더십 관점**: Director/Head만 4번 했다. 기술 답변만큼 **"이 사람이 팀에서 어떤 역할을 할지"** 를 본다.

### 2.2 Don과의 교집합 — 이걸 먼저 꺼낼 것
| Adam의 경험 | Don의 대응 경험 | 한 문장 브리지 (영어) |
|---|---|---|
| EOL 테스트 앱, 자동 테스트 시스템 | Apple factory test-node 아키텍처, SK hynix 챔버 신뢰성 테스트 플랫폼 | "I've spent a lot of my career on the manufacturing side of firmware — designing the factory test nodes that catch latent defects before MP." |
| 모듈형 플랫폼 재사용 (Visteon) | 여러 아키텍처(Cortex-R/M, Xtensa)·FPGA→ASIC에 걸친 FW bring-up | "Bringing the same firmware up across different silicon taught me where the platform boundary has to be." |
| IPC 프로토콜 (JCI) | 멀티코어 SSD 컨트롤러의 코어 간 통신, PCIe/I2C/SPMI/RFFE 인터페이스 루트코즈 | "Most of the bugs I've owned lived at an interface between two processors or two teams." |
| BSP + CI 통합 | FPGA 이미지/FW bring-up, 테스트 자동화 SDK | "I built test automation other engineers relied on, so I think about infrastructure as a product." |
| OTA·보안 업데이트 | production FW의 에러 리포팅/핸들링, 텔레메트리 (OTA 직접 경험은 없음) | 없는 경험은 만들지 말고 **질문으로 전환**: "How does the team handle field updates today?" |
| 메모리 분석 → 원가 | FW 성능 튜닝, safety margin sign-off (reliability vs performance/power) | "I've made the call on how much margin we trade for performance and power — that's a cost decision too." |

---

## 3. FW 팀 구성 분석 [Don 제공 · 미검증]

> Don이 LinkedIn에서 찾은 **직함과 경력 연수만** 사용했다. 개인 프로필은 열람하지 않았다.
> ⚠️ 친구 추가·메시지·팔로우 절대 금지 (Don 지시). 필요하면 경력 연수 정도만 참고.

| 구간 | 인원 | 구성 (직함 기준) |
|---|---|---|
| 20년+ | 3 | Senior Embedded SWE (22y), EE/Embedded (25y), Embedded FPGA (연수 불명확, 6y 또는 22y) |
| 8~12년 | 2 | HIL Firmware Engineer (12y), Sr FW (8y) |
| 3~6년 | 5 | Firmware (6y), Firmware developer (4.5y, MIT), Embedded SWE (3.3y), FW Engineer (3y), Firmware (2.3y) |
| 1년 미만~인턴 | 2 | Embedded SWE (<1y), Firmware Intern |

### 3.1 읽히는 신호 [추정]
- **중간 허리(8~15년차)가 얇다.** 20년+ 베테랑과 5년 미만 주니어 사이가 비어 있다 → Don(약 8년, bring-up·양산·디버깅 리드)이 정확히 그 칸에 들어간다.
- **HIL Firmware Engineer**가 있다 → 하드웨어 인 더 루프 테스트 인프라가 존재한다. Don의 테스트 자동화·test-node 경험과 협업 지점.
- **Embedded FPGA 엔지니어**가 FW 팀에 있다 → 자체 라디오/영상 처리가 FPGA에 걸쳐 있을 가능성. Don의 FPGA 단계 SoC bring-up 경험이 대화 소재가 된다.
- **EE/Embedded 겸업 시니어**가 있다 → 보드·FW 경계 문제를 한 사람이 떠안고 있을 수 있다. Don의 "signal/bus/system 레벨 디버깅"이 가치를 갖는 지점.
- 주니어 비중이 높고 JD가 없다 → Adam은 **"스스로 문제를 정의하고, 주니어에게 패턴을 남기는"** 사람을 찾을 가능성이 크다.

---

## 4. 45분 예상 구성 [추정]

```
0–5분    인사 · Adam의 팀/회사 소개 (메모할 것: 제품군, 팀 구조, 지금 가장 아픈 문제)
5–10분   Tell me about yourself  → 90초 버전 (6.1)
10–25분  경력 딥다이브: "가장 어려웠던 bring-up / 디버깅" 1~2개를 깊게 파고듦
25–38분  기술 토론: 플랫폼·아키텍처 질문 또는 임베디드 개념 퀵파이어 (코딩은 가능성 낮음)
38–45분  내 질문 (7번) + 다음 단계
```
- Director 인터뷰는 보통 **라이브 코딩보다 설계·판단·경험 검증** 위주다. 그래도 **화이트보드/공유문서에 ring buffer나 간단한 C를 쓰라고 할 가능성은 열어둘 것** (온사이트 주제와 같으므로).
- **답변 길이 원칙**: 스토리 2분, 개념 질문 45초. Adam이 파고들면 그때 깊게.

---

## 5. 예상 질문과 준비

### 5.1 경력 딥다이브 — 가장 확률 높음
| 질문 | 준비할 스토리 | 반드시 들어갈 요소 |
|---|---|---|
| **"Walk me through the hardest bring-up you've done."** | Solidigm PCIe Gen6 FPGA 단계 FW/SoC bring-up (Cortex R8/R82/M0+, PCIe/NVMe IP) 또는 Apple 새 RF 실리콘 통합 | 첫 전원 인가부터 순서, 무엇이 안 됐고 어떻게 좁혔나, 누구와 협업했나, **무엇을 남겼나(체크리스트·자동화)** |
| **"Tell me about a bug nobody else could crack."** | Apple 인터페이스 레벨 실패 (PCIe/I2C/SPMI/RFFE) 1건 | 증상 → 가설 → 계측(DSO·프로토콜 분석기) → 근본 원인 → 크로스팀 fix → **재발 방지(test-node에 케이스 추가)** |
| **"What did your factory test-node work look like?"** | Apple test-node 아키텍처, scenario/stress 기반 케이스 | **Adam의 EOL 경험과 맞닿는 질문.** 커버리지 vs 테스트 시간(=원가) 트레이드오프, 잠재 결함을 MP 전에 잡은 사례 |
| **"What did you build that other engineers depended on?"** | SK hynix 테스트 플랫폼 SDK / NVMe telemetry 디버그 기능 / production FW error handling scheme | 사용자(다른 팀)가 누구였나, API가 바뀔 때 호환을 어떻게 지켰나 |
| "Why leave Apple so soon?" / "Why Neros?" | 1.1 스토리 그대로 | 6.2 영어 답변 |

#### STAR 골격 — Don이 실제 디테일로 채울 것
> ⚠️ 아래 `[ ]` 안은 **Don만 아는 실제 내용**으로 채워야 한다. 숫자·이름을 지어내지 말 것.

**스토리 A — 가장 어려운 bring-up (Solidigm, pre-silicon FPGA)**
- **S**: PCIe Gen6 차세대 컨트롤러, RTL freeze 전 FPGA 프로토타입에서 FW와 SoC IP를 동시에 올려야 했음. `[당시 팀 규모, 일정 압박]`
- **T**: `[Don의 담당: 예) 멀티코어 부팅 + PCIe/NVMe 서브시스템 bring-up]`
- **A**: `[증상: 무엇이 안 올라왔나]` → `[가설 좁히기: FW인지 RTL인지 FPGA 타이밍인지 가르는 방법]` → `[사용한 도구: JTAG/Trace32, LA]` → `[설계팀과의 협업 방식]`
- **R**: `[RTL 버그를 freeze 전에 잡았는지, 일정 영향, 이후 bring-up 체크리스트화 여부]`
- **Adam 연결 한 줄**: "That's where I learned to separate what's silicon, what's board, and what's firmware — fast."

**스토리 B — 남들이 포기한 버그 (Apple, 인터페이스 레벨)**
- **S**: 새 RF 칩이 풀 시스템에 들어간 뒤 `[I2C/SPMI/RFFE/PCIe 중 무엇]`에서 `[간헐적 증상]`
- **T**: 칩 벤더·보드·FW 어느 팀 소관인지 불분명한 상태에서 루트코즈 오너
- **A**: `[재현 조건 찾기]` → `[DSO/프로토콜 분석기로 본 것]` → `[근본 원인]` → `[설득·조율한 팀]`
- **R**: `[fix + test-node에 스트레스 케이스 추가로 재발 방지]`
- **Adam 연결 한 줄**: "The fix was one change; the value was making sure the factory would catch the next one."

**스토리 C — 다른 사람이 쓰는 인프라 (SK hynix)**
- **S**: 챔버 기반 대규모 신뢰성 테스트를 엔지니어들이 수동으로 돌리고 있었음
- **T**: 테스트 플랫폼 SDK·자동화 구축
- **A**: `[API 설계: 온도 제어·스케줄링·UART 시퀀스 자동화]` → `[사용자 요구 수집 방식]` → `[외부 벤더·IT·보안팀 협업]`
- **R**: `[몇 명/몇 팀이 썼는지, 테스트 처리량 변화]`
- **Adam 연결 한 줄**: "I treated the test platform as a product — other engineers were my customers."

### 5.2 플랫폼·아키텍처 토론 — Adam의 홈그라운드
| 질문 | 답변 골격 |
|---|---|
| **"같은 기능을 하는 보드인데 MCU/센서가 다른 Board B를 지원해야 한다. FW를 어떻게 구조화하나?"** | HAL 경계를 칩이 아닌 **기능 단위**로 자르기 → 보드 설정을 코드가 아닌 **데이터**(board config)로 → 보드 ID 런타임 감지(strap/EEPROM) 또는 빌드타임 variant → **두 보드를 같은 CI·HIL에서 매 커밋 검증** → 드라이버 차이는 조건문이 아니라 ops 테이블(함수 포인터) |
| "MCU와 Linux 컴패니언(또는 라디오 MCU) 사이 통신 프로토콜을 설계해 봐." | 전송(UART/SPI/Ethernet) 선택 근거 → 프레이밍(COBS/길이+CRC) → 메시지 ID·버전 필드 → ACK/재전송이 필요한 메시지와 최신값만 중요한 메시지 구분 → 흐름 제어·우선순위 → 시간 동기 → 한쪽이 죽었을 때 동작. **`practice/02`, `04`와 동일한 내용** |
| "필드에 있는 수천 대에 펌웨어를 안전하게 업데이트하려면?" | A/B 슬롯 + 부트로더 검증 + 롤백 → 이미지 서명(PKI) → 부분 실패·전원 차단 복구 → 단계적 배포 → 버전·호환성 매트릭스. **OTA 직접 경험은 없다고 솔직히** 말하고 원리로 답하기 (`practice/03` Q5·Q6 패턴) |
| "12명 FW 팀에서 빌드/CI를 어떻게 가져가야 하나?" | 재현 가능한 툴체인 고정 → 모든 타깃 매 커밋 빌드 → 유닛테스트 + HIL 스모크 → 바이너리에 커밋 해시·빌드 정보 삽입 → 어떤 FW가 어떤 기체에 실렸는지 추적(방산 추적성) |
| "양산 라인에서 FW가 해야 할 일은?" | 프로비저닝(시리얼·키·캘리브레이션 값 기록) → 기능 테스트 모드 → 결과를 MES로 → 테스트 시간이 곧 원가라는 관점 → 불량 데이터를 설계로 되먹임. **Don의 가장 강한 영역** |
| "로그/텔레메트리를 한정된 링크로 어떻게 보내나?" | 링 버퍼 + drop 정책 → 포맷 ID 기반 바이너리 로그 → 우선순위·레이트 제한 → 기체 내 저장 vs 실시간 전송 분리 (`practice/01`, `04`) |

### 5.3 임베디드 개념 퀵파이어 — 45초 답변
| 질문 | 핵심 답 |
|---|---|
| `volatile`은 무엇이고 무엇을 보장하지 않나? | 컴파일러의 캐싱·최적화를 막을 뿐, **원자성이나 동기화는 보장 안 함** |
| ISR에서 하면 안 되는 것 | 블로킹, malloc, printf, 긴 루프, 부동소수점(컨텍스트 비용) → 플래그/큐만 넣고 main이나 태스크로 넘기기 |
| priority inversion과 해결책 | 낮은 우선순위가 락을 쥐고 중간 우선순위가 선점 → 우선순위 상속/ceiling |
| 링 버퍼에서 full과 empty 구분 | count 필드 / 한 칸 희생 / 자유 증가 인덱스. SPSC는 락 없이 가능한 이유까지 |
| 스택 오버플로 탐지 | 스택 카나리아·워터마크, MPU 가드 영역, RTOS 스택 체크 |
| 워치독 어디서 kick하나 | 타이머 ISR에서 무조건 kick하면 무의미 → 각 태스크 alive 비트가 다 모였을 때만 |
| I2C 버스가 멈췄다 | SDA가 low로 잡힘 → SCL 9클럭 토글로 복구, 타임아웃, 슬레이브 리셋 |
| 틱 카운터 wraparound | 부호 없는 뺄셈 `(now - start) >= timeout` 은 안전, `now > deadline` 은 깨짐 |
| 구조체를 그대로 무선으로 보내면? | 패딩·엔디안·정렬 문제 → 필드 단위 명시적 직렬화 |
| 부팅 시퀀스 | 리셋 벡터 → 스택/클럭 → `.data` 복사·`.bss` 0 → 페리페럴 → main |

→ 전부 `practice/05_embedded_core`, `06_isr_timing`과 Anduril `05_circular_buffer`, `09_rtos_embedded`에 코드로 있다.

### 5.4 리더십·오너십 — Director가 반드시 보는 것
| 질문 | 답변 방향 |
|---|---|
| **"What does Principal mean to you?"** | 코드를 계속 짜면서, **팀 전체가 쓰는 패턴과 인프라를 남기는 사람**. 결정이 필요한 곳에서 트레이드오프를 문서로 만들고, 주니어가 같은 실수를 두 번 하지 않게 만드는 사람 |
| "How do you work with junior engineers?" | 코드리뷰를 가르침 도구로, 디버깅은 옆에서 같이 (가설 세우는 법을 보여주기), 체크리스트·런북 남기기 |
| "Tell me about a disagreement with another team." | Apple 크로스팀 루트코즈 사례: 데이터로 책임 소재 정리, 비난 대신 재발 방지로 수렴 |
| "There's no clear JD. What would you want to own?" | **이게 사실상 핵심 질문.** 6.3의 답변 사용 |
| "How do you handle ambiguity / fast pace?" | Apple 프로그램 속도 경험 + 스타트업에서 원하는 것 (직접 소유) |

---

## 6. 영어 답변 스크립트

### 6.1 Tell me about yourself (90초, Adam 버전)
> I'm an embedded firmware engineer, and most of my career has been at the point where new silicon meets a real product.
>
> I spent a little over seven years at SK hynix and its Solidigm business writing production firmware for data-center SSDs — bare-metal C on multi-core Cortex-R and M and Xtensa. A lot of that was bring-up: FPGA prototypes before RTL freeze, PCIe and NVMe subsystems, and working with the silicon team on root cause. I also built infrastructure other engineers relied on — a telemetry-based debug feature our data-center customers used, and a test-automation platform for large-scale reliability testing.
>
> Last December I moved to Apple to get consumer-device breadth. I've been integrating new RF silicon into shipping platforms — owning root cause for interface failures like PCIe, I2C, SPMI, RFFE — and designing the factory test nodes that catch latent defects before mass production.
>
> What I'm looking for now is ownership. Apple is highly specialized, and the scope I can contribute to is narrow by design. Neros is at the stage where bring-up across changing hardware and firmware that scales in the factory are exactly the problems that matter — and that's where I've spent my career.

### 6.2 Why leave Apple / Why Neros (리크루터에게 한 말과 일관되게)
> I went to Apple on purpose — after seven years of data-center firmware I wanted consumer-device breadth, and I got it: system integration across a much wider surface than I'd seen before. What I also found is that the organization is extremely specialized. Roles and boundaries between teams are drawn tightly, so the part of the problem I'm allowed to own is limited, even when I can see how to fix more of it. I love the pace of the programs; I want the scope to match it.
>
> When I started looking at startups this week, I remembered a note from a recruiter about Neros from about a year and a half ago. I read up on the company — the mission, the in-house radios and flight hardware, the factory producing over a thousand units a week — and the overlap with what I do was obvious: bringing up new chipsets, and making firmware that holds up at manufacturing scale. So I reached out directly.

### 6.3 "JD가 없는데 뭘 맡고 싶나?" — 레벨링 질문 대비
> Where I'd add the most, fastest, is the boundary between hardware and firmware at scale. Three things:
> First, **bring-up and variants** — when a new board or a second-source part comes in, getting firmware running and the differences contained behind a clean interface, so the rest of the team isn't chasing hardware changes.
> Second, **manufacturing and test** — the firmware side of the factory: provisioning, functional test modes, and making sure field and factory failures feed back into design. With the production rate you're running, that's leverage.
> Third, **hard cross-team debugging** — the issues that sit between EE, firmware, and the radio or autonomy teams.
> And I'd expect to keep shipping code while I do it — I'm not looking for a pure management role.

### 6.4 모르는 걸 물었을 때 (드론·OTA·Bazel·Linux)
> I haven't done that directly. The closest thing I've done is **[관련 경험]**, and the way I'd approach it is **[원리 기반 접근 2~3단계]**. I'd want to understand how you do it today before proposing anything.

---

## 7. Adam에게 할 질문 (5개 중 3~4개)

1. **"You've grown the firmware team to twelve in about a year and a half. What's the gap you're hiring for with this role — and what would make someone a Principal here rather than a Senior?"** ← 레벨 정의를 직접 듣는 질문. 최우선
2. **"With Archer, Archer AI, and Bandit, how much firmware is shared across products today, and how do you handle board variants or second-source parts?"** ← Adam의 Visteon 경험과 CTO가 말한 Board B를 동시에 건드림
3. **"How does the firmware team work with the factory — provisioning, functional test, getting failure data back?"** ← Don의 강점 영역으로 대화를 끌고 옴
4. "What does the build, CI, and HIL setup look like, and where does it hurt most?" ← HIL 엔지니어가 있다는 사실과 연결
5. "What would a successful first 90 days look like for this person?"

**하지 말 것**: 연봉·복지·원격근무 질문(리크루터 몫), 드론 무기 효과 같은 민감한 질문.

---

## 8. 온사이트 미리보기 — 지금부터 준비 시작

| 온사이트 항목 | 지금 할 것 | 자료 |
|---|---|---|
| **발표 1시간** (challenge, debugging, bring-up) | 스토리 A·B·C를 발표 골격으로. Adam 인터뷰에서 **어느 스토리에 반응했는지 메모** → 그걸 메인으로 | 이 노트 5.1 |
| C/C++ 기본 | 포인터·비트·메모리·`const`/`volatile`/`static`, C++는 RAII·가상함수 비용·템플릿 기초 | `practice/05`, Anduril `01`~`04` |
| **ring buffer 구현** | count 기반 → power-of-2 mask → SPSC lock-free까지 **맨손으로 15분 안에** | Anduril `05_circular_buffer`, `practice/01` Q1~Q2 |
| **generic system design** | 로거/텔레메트리 시스템, 메시지 큐, 펌웨어 업데이트 시스템, 센서 데이터 파이프라인, 레이트 리미터 | `practice/04`, `02`, `03`, `notes/00_drone_architecture.md` §7 |
| Tour 30분 | 공장 라인에서 FW가 개입하는 지점(프로비저닝·테스트 스테이션)을 **눈으로 찾고 질문하기** | — |

**발표 초안 골격 (1시간 = 발표 35~40분 + Q&A)**
1. 나는 누구인가 (3분): 데이터센터 SSD FW → consumer RF 통합, 공통점 = 새 실리콘이 제품을 만나는 지점
2. 사례 1 — Bring-up (10분): pre-silicon FPGA에서 PCIe/NVMe 서브시스템
3. 사례 2 — 크로스팀 디버깅 (10분): Apple 인터페이스 레벨 실패, 계측 → 근본 원인 → 재발 방지
4. 사례 3 — 스케일을 위한 인프라 (8분): test-node / 테스트 플랫폼 SDK / telemetry
5. 배운 원칙 (4분): 경계를 먼저 정의하라, 공장이 다음 버그를 잡게 하라, 인프라는 제품이다
6. Neros에서 이걸 어떻게 쓸 것인가 (3분)
> ⚠️ Apple·SK hynix 사내 기밀(칩 이름, 내부 수치, 미공개 제품)은 **일반화해서** 말할 것. 발표 자료에도 넣지 말 것.

---

## 9. 체크리스트 (인터뷰 전날까지)

- [ ] **스토리 A·B·C의 `[ ]` 칸을 실제 디테일로 채우기** (가장 중요 — 이게 없으면 딥다이브에서 무너짐)
- [ ] 각 스토리를 **2분 버전**으로 소리 내어 말해보기 (타이머)
- [ ] 6.1 TMAY, 6.2 Why leave Apple, 6.3 JD 없는 역할 질문 — 소리 내어 각 2회
- [ ] 5.2 플랫폼 질문 중 **Board B 변종 구조**와 **MCU↔컴패니언 프로토콜** 두 개를 화이트보드에 그려보기
- [ ] 5.3 퀵파이어 10문항을 45초씩 답해보기
- [ ] ring buffer 맨손 구현 1회 (`make prob N=05_circular_buffer` in Anduril 또는 `practice/01`)
- [ ] 7번 질문 중 1·2·3번 외우기
- [ ] Adam 이력 핵심 5개 기억: 자동차 플랫폼 아키텍트 · IPC 프로토콜 · OTA/PKI · BSP+CI · EOL 테스트
- [ ] 사내 기밀 경계 정리: 말해도 되는 것 / 일반화할 것 / 말하면 안 되는 것
- [ ] 인터뷰 직후 메모: 받은 질문, Adam이 반응한 스토리, 레벨 힌트, 온사이트 일정

---

## 10. 출처

1. LinkedIn — Adam Kibit 경력 상세 페이지 (2026-09-16 열람, 보기만 함)
2. https://www.zoominfo.com/p/Adam-Kibit/2447509283 — Neros 재직, 학력 (2026-09-16)
3. https://patents.justia.com/assignee/faraday-future-inc?page=6 — Faraday Future 특허 (Phone-as-a-Key 관련 언급) (2026-09-16)
4. Devin(리크루터) 콜, 2026-09-16 — 팀 규모, 인터뷰 프로세스, 포지션 표기
5. Don이 LinkedIn에서 정리한 FW 팀 명단 (직함·경력 연수만 사용)
