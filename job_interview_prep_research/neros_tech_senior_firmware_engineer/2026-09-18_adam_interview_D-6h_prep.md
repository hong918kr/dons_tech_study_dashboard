# Adam Kibit 45분 인터뷰 — D-6시간 준비 노트

> **작성**: 2026-09-18 · **인터뷰**: 오늘, 약 6시간 뒤 · 45분 · **상대**: Adam Kibit (Director of Firmware, hiring manager)
> 배경 상세는 `neros_hm_adam_technical_prep_2026-09-18.md`. 이 노트는 **오늘 남은 6시간을 어떻게 쓸지**에 집중한다.
> 신뢰도: `[확인됨]` Devin 콜·공개 프로필·공식 자료 · `[추정]` 추론 · `[영상요약·미검증]` Don이 받은 영상 요약본

---

## 0. 오늘의 핵심 3가지

1. **이야기 3개(A·B·C)를 실제 디테일로 채운다.** 이 인터뷰의 승패가 여기서 갈린다. Director는 코딩보다 "이 사람이 어떻게 일했나"를 판다. [추정]
2. **Adam의 언어로 말한다.** platform, reuse, variants, EOL test, IPC, CI, OTA. 그의 이력이 이 단어들로 채워져 있다. [확인됨]
3. **Devin에게 한 이야기를 그대로 유지한다.** 이직 이유와 Neros에 온 경위가 조금이라도 달라지면 신뢰가 깨진다.

이 인터뷰의 목표는 하나다. **온사이트 초대.** 45분 안에 "이 사람을 현장에서 더 보고 싶다"는 판단을 만들면 된다.

---

## 1. 6시간 타임라인

| 남은 시간 | 할 일 | 산출물 |
|---|---|---|
| **T-6:00 ~ T-5:00** | 스토리 A·B·C 빈칸 채우기 (§5) | 각 스토리 메모 1장 |
| **T-5:00 ~ T-4:15** | 소리 내어 연습: TMAY, Why Neros, Why leave Apple (§3) | 타이머로 90초·60초·45초 |
| **T-4:15 ~ T-3:15** | 코딩·개념 워밍업: ring buffer 맨손 15분 + 퀵파이어 10개 (§6) | 코드 1개, 45초 답변 10개 |
| **T-3:15 ~ T-2:30** | 설계 질문 2개를 종이에 그리기: Board B 변종 구조, MCU↔컴패니언 프로토콜 (§4) | 다이어그램 2장 |
| **T-2:30 ~ T-1:30** | **쉬기.** 식사, 산책. 새 내용 금지 | — |
| **T-1:30 ~ T-0:45** | 치트시트(§8) 1회 통독, 역질문 3개 외우기 (§7) | — |
| **T-0:45 ~ T-0:15** | 환경 세팅: 조용한 장소, 마이크, 물, 메모지, 이력서 PDF 열기 | — |
| **T-0:15** | 스토리 제목 3개만 보고, 호흡. 공부 끝 | — |

- [ ] T-6:00 스토리 A 채우기
- [ ] 스토리 B 채우기
- [ ] 스토리 C 채우기
- [ ] TMAY 90초 × 3회
- [ ] Why leave Apple 45초 × 2회
- [ ] Why Neros 60초 × 2회
- [ ] ring buffer 맨손 구현 (15분)
- [ ] 퀵파이어 10개
- [ ] Board B 변종 구조 그리기
- [ ] MCU↔컴패니언 프로토콜 그리기
- [ ] 휴식 (T-2:30)
- [ ] 치트시트 통독 (T-1:30)
- [ ] 역질문 3개 외우기
- [ ] 마이크·조용한 장소·이력서 확인

> **T-1:30 이후로는 새로운 걸 공부하지 않는다.** 머리에 넣는 것보다 말이 나오는 상태가 더 중요하다.

---

## 2. 상대와 상황 (확인된 것만)

### 2.1 Adam Kibit [확인됨 · LinkedIn 경력 페이지, 2026-09-16 열람]
| 항목 | 내용 |
|---|---|
| 현재 | **Neros Director of Firmware**, 2025-05 합류 (El Segundo, on-site) |
| 최근 | Skyline Mobility Head of SW & Electronics → Xeal Head of Engineering → Canoo Engineering Director (자동차·EV) |
| 초기 | Aeris(vehicle-cloud, OTA), Faraday Future(인포테인먼트 아키텍트), Visteon(텔레매틱스 아키텍트), **Johnson Controls 9년**(Test Eng → SW Eng → Tech Lead) |
| 이력서에 적힌 성과 | IPC 프로토콜 신규 구현(전송률 +30%), BSP를 Jenkins CI + 유닛테스트에 통합, **EOL 테스트 앱 개선(양산 throughput +20%)**, 자동 테스트 시스템(테스트 시간 −50%), 모듈형 텔레매틱스 플랫폼(재사용, ~25% 절감), PKI 보안 모델, 보안 OTA 파이프라인 |

**읽히는 것** [추정]: 드론 전문가가 아니라 **플랫폼·양산·테스트 쪽 엔지니어링 리더**다. 그래서 드론 지식보다 아키텍처 판단력, 양산 감각, 스스로 굴러가는 시니어리티를 볼 가능성이 크다.

### 2.2 Devin 콜에서 확인된 것 [확인됨 · 2026-09-16]
- 회사 약 **300명**, 절반 이상이 공장 operator/technician
- FW 엔지니어 **12명 + Director**
- 포지션 "Principal/Senior Embedded SWE", **특정 JD 없음**
- 온사이트: Tour 30분 → **발표 1시간** → 1:1 기술 여러 개 (C/C++, **ring buffer**, generic system design)
- Don이 base **$200K 이상** 기대라고 전달

### 2.3 회사 숫자 (필요할 때만 쓸 것)
Archer 주당 약 1,200대 · Series C $250M, $2.5B 밸류(2026-08) · Army PBAS $500M IDIQ · 2028년 연 100만 대 목표 · 자체 라디오·flight computer [확인됨 · 공식 자료]

---

## 3. 45분 흐름과 스크립트

```
0–5분    Adam이 회사·팀 소개        → 메모: 제품군, 팀 구조, 지금 가장 아픈 문제
5–10분   Tell me about yourself     → 90초 (아래 3.1)
10–25분  경력 딥다이브              → 스토리 A/B/C 중 1~2개를 깊게
25–38분  기술 토론                  → 플랫폼·아키텍처, 퀵파이어, 혹은 화이트보드 코드
38–45분  내 질문 + 다음 단계        → §7 역질문
```
- **답변 길이**: 스토리 2분, 개념 45초. Adam이 파고들 때만 깊게 간다.
- Director 인터뷰는 라이브 코딩보다 판단과 경험 검증이 중심이다. 그래도 **ring buffer 정도는 화이트보드에서 쓰라고 할 수 있다.** [추정]

### 3.1 Tell me about yourself (90초)
> I'm an embedded firmware engineer, and most of my career has been at the point where new silicon meets a real product.
>
> I spent about seven years at SK hynix and its Solidigm business writing production firmware for data-center SSDs — bare-metal C on multi-core Cortex-R and M and Xtensa. A lot of that was bring-up: FPGA prototypes before RTL freeze, PCIe and NVMe subsystems, and root cause with the silicon team. I also built infrastructure other engineers relied on — a telemetry-based debug feature our data-center customers used, and a test-automation platform for large-scale reliability testing.
>
> Last December I moved to Apple for consumer-device breadth. I've been integrating new RF silicon into shipping platforms — owning root cause for interface failures like PCIe, I2C, SPMI, RFFE — and designing the factory test nodes that catch latent defects before mass production.
>
> What I'm looking for now is ownership. Apple is highly specialized, and the scope I can contribute to is narrow by design. Neros is where bring-up across changing hardware, and firmware that scales in the factory, are exactly the problems that matter — and that's where I've spent my career.

### 3.2 Why leave Apple / Why Neros (Devin에게 한 이야기와 동일하게)
> I went to Apple on purpose — after seven years of data-center firmware I wanted consumer-device breadth, and I got it. What I also found is that the organization is extremely specialized: roles and boundaries between teams are drawn tightly, so the part of the problem I'm allowed to own is limited, even when I can see how to fix more of it. I love the pace of the programs; I want the scope to match it.
>
> When I started looking at startups this week, I remembered a note from a recruiter about Neros from about a year and a half ago. I read up on the company — the mission, the in-house radios and flight hardware, the factory producing over a thousand units a week — and the overlap with what I do was clear: bringing up new chipsets, and making firmware that holds up at manufacturing scale. So I reached out directly.

- **Apple 이야기는 "R&R 한계"까지만.** 조직이나 특정 팀 비판으로 넘어가지 말 것.
- 꼬리질문 "Solidigm은 왜 떠났나?" → "PCIe Gen6 프로그램은 긴 실리콘 사이클이었고, 저장장치 밖의 시스템 폭이 필요했습니다. Apple에서 그걸 얻었고, 이제는 그걸 다시 펌웨어 오너십으로 가져오고 싶습니다."

### 3.3 "JD가 없는데 뭘 맡고 싶나?" — 레벨링의 핵심 질문
> Where I'd add the most, fastest, is the boundary between hardware and firmware at scale. Three things. First, **bring-up and variants**: when a new board or second-source part comes in, getting firmware running and containing the differences behind a clean interface. Second, **manufacturing and test**: provisioning, functional test modes, and feeding factory failures back into design — with your production rate, that's leverage. Third, **hard cross-team debugging**: the issues that sit between EE, firmware, and the radio or autonomy teams. And I'd keep shipping code while I do it.

### 3.4 모르는 걸 물었을 때
> I haven't done that directly. The closest thing I've done is **[관련 경험]**, and I'd approach it by **[원리 2~3단계]**. I'd want to understand how you do it today before proposing anything.

### 3.5 "방산·미션" 질문이 나오면
본인의 실제 입장을 **짧고 분명하게** 말한다. 망설이거나 얼버무리면 감점이다. 이 부분은 오늘 미리 한 문장으로 정해 두자: `[내 답: ______]`

---

## 4. 예상 질문 Top 12와 답 골격

### 경험 검증 (가장 확률 높음)
| 질문 | 답 골격 |
|---|---|
| **Walk me through your hardest bring-up.** | 스토리 A. 첫 전원부터 순서 → 무엇이 안 됐고 → silicon/board/firmware를 어떻게 가렸나 → 무엇을 남겼나(체크리스트·자동화) |
| **A bug nobody else could crack?** | 스토리 B. 증상 → 가설 → 계측(DSO·프로토콜 분석기) → 근본 원인 → 크로스팀 fix → **재발 방지** |
| **Tell me about your factory test-node work.** | Adam의 EOL 경험과 맞닿는 질문. 커버리지 vs 테스트 시간(=원가), MP 전에 잠재 결함을 잡은 사례 |
| **What did you build others depended on?** | 스토리 C. 사용자가 누구였나, API가 바뀔 때 호환을 어떻게 지켰나 |

### 플랫폼·아키텍처 (Adam의 홈그라운드)
| 질문 | 답 골격 |
|---|---|
| **Board B(같은 기능, 다른 MCU/센서)를 지원해야 한다. FW 구조는?** | HAL 경계를 칩이 아니라 **기능** 단위로 → 보드 설정을 코드가 아닌 **데이터**로 → 보드 ID 런타임 감지 또는 빌드 variant → **두 보드를 같은 CI·HIL에서 매 커밋 검증** → 드라이버 차이는 조건문 대신 ops 테이블 |
| **MCU와 컴패니언 Linux 사이 프로토콜을 설계해 봐.** | 전송 선택(UART/SPI/Ethernet) → 프레이밍(COBS 또는 길이+CRC) → 메시지 ID·버전 → ACK가 필요한 것과 최신값만 중요한 것 구분 → 흐름 제어 → 시간 동기 → 한쪽이 죽었을 때 동작 |
| **필드 수천 대에 펌웨어를 안전하게 업데이트하려면?** | A/B 슬롯 + 이미지 서명 + 부트 검증 + 롤백 + 전원 차단 복구 + 단계 배포. **OTA를 직접 한 적은 없다고 솔직히** 말하고 원리로 답하기 |
| **12명 팀의 빌드/CI는 어떻게?** | 툴체인 고정 → 모든 타깃 매 커밋 빌드 → 유닛테스트 + HIL 스모크 → 바이너리에 커밋 해시 삽입 → 어떤 FW가 어떤 기체에 실렸는지 추적 |
| **양산 라인에서 FW가 할 일은?** | 프로비저닝(시리얼·키·캘리브레이션) → 기능 테스트 모드 → 결과를 MES로 → 테스트 시간이 곧 원가 → 불량 데이터를 설계로 되먹임. **Don의 가장 강한 영역** |

### 리더십·레벨
| 질문 | 답 골격 |
|---|---|
| **What does Principal mean to you?** | 코드를 계속 짜면서 **팀이 쓰는 패턴과 인프라를 남기는 사람**. 트레이드오프를 문서로 만들고 주니어가 같은 실수를 두 번 하지 않게 하는 사람 |
| **How do you work with junior engineers?** | 코드리뷰를 가르침의 도구로, 디버깅은 옆에서 같이(가설 세우는 법 보여주기), 런북·체크리스트 남기기 |
| **Disagreement with another team?** | Apple 크로스팀 루트코즈 사례. 데이터로 책임 소재 정리, 비난 대신 재발 방지로 수렴 |

---

## 5. 스토리 A·B·C 채우기 (오늘 가장 중요한 일)

> ⚠️ 아래 질문에는 **Don만 답할 수 있다.** 숫자나 사실을 지어내지 말고, 기억나는 만큼만 적자. 사내 기밀(칩 이름, 내부 수치, 미공개 제품)은 **일반화해서** 쓴다.
> 각 스토리는 **말했을 때 2분**이 되게 만든다.

### 스토리 A — 가장 어려웠던 bring-up (Solidigm, pre-silicon FPGA)
- [ ] **상황**: 어떤 프로젝트였나? 팀은 몇 명이었나? 일정 압박은?
- [ ] **내 역할**: 내가 맡은 정확한 범위는? (멀티코어 부팅? PCIe/NVMe 서브시스템?)
- [ ] **문제**: 무엇이 안 올라왔나? 증상은?
- [ ] **좁혀간 방법**: FW인지 RTL인지 FPGA 타이밍인지 어떻게 가렸나? 어떤 도구를 썼나? (JTAG/Trace32, LA 등)
- [ ] **협업**: 설계팀과 어떻게 일했나?
- [ ] **결과**: 무엇이 해결됐나? 일정에 어떤 영향이 있었나?
- [ ] **남긴 것**: 이후 bring-up 체크리스트나 자동화로 이어졌나?
- **Adam 연결 한 줄**: "That's where I learned to separate what's silicon, what's board, and what's firmware — fast."

### 스토리 B — 남들이 포기한 버그 (Apple, 인터페이스 레벨)
- [ ] **상황**: 어떤 칩·어떤 인터페이스(PCIe/I2C/SPMI/RFFE)였나?
- [ ] **증상**: 간헐적이었나? 어떤 조건에서 나왔나?
- [ ] **소관 불명**: 칩 벤더·보드·FW 중 누구 책임인지 불분명했나? 내가 오너를 맡은 계기는?
- [ ] **계측**: DSO나 프로토콜 분석기로 무엇을 봤나?
- [ ] **근본 원인**: 결국 무엇이었나?
- [ ] **조율**: 어떤 팀을 어떻게 설득했나?
- [ ] **재발 방지**: test-node에 어떤 케이스를 추가했나?
- **Adam 연결 한 줄**: "The fix was one change; the value was making sure the factory would catch the next one."

### 스토리 C — 다른 사람이 쓰는 인프라 (SK hynix)
- [ ] **상황**: 챔버 신뢰성 테스트를 어떻게 돌리고 있었나? 무엇이 불편했나?
- [ ] **내가 만든 것**: SDK·자동화의 범위는? (온도 제어, 스케줄링, UART 시퀀스 자동화)
- [ ] **사용자**: 몇 명·몇 팀이 썼나? 요구는 어떻게 수집했나?
- [ ] **API 변경**: 바뀔 때 사용자에게 어떻게 알리고 호환을 지켰나?
- [ ] **협업**: 외부 벤더·IT·보안·네트워크팀과 무엇을 조율했나?
- [ ] **결과**: 테스트 처리량이나 엔지니어 시간이 어떻게 달라졌나?
- **Adam 연결 한 줄**: "I treated the test platform as a product — other engineers were my customers."

**세 스토리를 채운 뒤 각각을 소리 내어 2분 안에 말해 본다.** 2분을 넘으면 줄이고, 1분이 안 되면 "왜 그렇게 판단했나"를 더한다.

---

## 6. 코딩·개념 워밍업

### 6.1 ring buffer (온사이트 확정 주제, 오늘 한 번은 맨손으로)
```sh
cd ~/workspace/dons_tech_study_dashboard/job_interview_prep_research/anduril_firmware_engineer
make prob N=05_circular_buffer      # 직접 구현 → FAIL이 PASS로
```
말로 설명할 수 있어야 하는 것:
- **소유권 규칙**: 생산자는 `head`만, 소비자는 `tail`만 쓴다. 그래서 락이 필요 없다.
- **순서**: 데이터를 먼저 쓰고 그다음 인덱스를 올린다.
- **full/empty 구분**: count 필드 / 한 칸 희생 / 자유 증가 인덱스.
- **2의 거듭제곱**: `%` 대신 `& (N-1)`로 나눗셈을 없앤다.
- **`volatile`로 충분한가?** → **아니다.** 컴파일러 캐싱만 막을 뿐 원자성과 코어 간 순서는 보장하지 않는다. 멀티코어는 C11 atomics(acquire/release).
- **가득 찼을 때**: drop-new가 안전하다. 생산자가 `tail`을 밀어 "덮어쓰기"를 하면 소비자와 경합해 SPSC가 깨진다.
- 동작하는 참고 구현: `practice/solutions/01_ring_logging.c` Q1, `2026-09-17_neros_low_latency_architecture_whitepaper_verified.md` §3.2

### 6.2 퀵파이어 (각 45초)
| 질문 | 핵심 |
|---|---|
| `volatile`은? | 컴파일러 최적화·캐싱 방지. 원자성·동기화는 보장 안 함 |
| ISR에서 하면 안 되는 것 | 블로킹, malloc, printf, 긴 루프 → 플래그·큐만 넣고 main으로 |
| priority inversion | 낮은 우선순위가 락을 쥐고 중간이 선점 → 우선순위 상속 |
| 워치독 kick 위치 | 타이머 ISR에서 무조건 kick하면 무의미 → 각 태스크 alive 비트가 모였을 때만 |
| I2C 버스가 멈췄다 | SDA가 low로 잡힘 → SCL 9클럭 토글로 복구, 타임아웃 |
| 틱 wraparound | `(now - start) >= timeout`은 안전, `now > deadline`은 깨짐 |
| 구조체를 그대로 무선으로 | 패딩·엔디안·정렬 문제 → 필드 단위 명시적 직렬화 |
| 스택 오버플로 탐지 | 카나리아·워터마크, MPU 가드 영역 |
| 부팅 시퀀스 | 리셋 벡터 → 스택·클럭 → `.data` 복사·`.bss` 0 → 페리페럴 → main |
| DMA와 캐시 | DMA 송신 전 clean, 수신 후 invalidate, 또는 캐시 안 되는 영역 사용 |

코드로 연습: `practice/05_embedded_core`, `practice/06_isr_timing`

---

## 7. 내가 할 질문 (3~4개)

1. **"You've grown the firmware team to twelve in about a year and a half. What's the gap you're hiring for here — and what would make someone a Principal rather than a Senior?"** ← 최우선. 레벨 정의를 직접 듣는다.
2. **"With Archer, Archer AI, and Bandit, how much firmware is shared across products, and how do you handle board variants or second-source parts?"** ← Adam의 플랫폼 재사용 경험과 맞닿는다.
3. **"How does the firmware team work with the factory — provisioning, functional test, getting failure data back?"** ← Don의 강점 영역으로 대화를 끌고 온다.
4. (시간 남으면) "What does the build, CI, and HIL setup look like, and where does it hurt most?"
5. (마지막) "What would a successful first 90 days look like?"

**하지 말 것**: 연봉·복지·원격근무 질문(리크루터 몫), 무기 효과 같은 민감한 질문.

---

## 8. 치트시트 (인터뷰 직전 1분용)

**나는 누구**: 새 실리콘이 제품을 만나는 지점의 펌웨어 엔지니어. SSD FW 약 7년 → Apple RF 통합 9개월.

**강점 3장**
1. 새 실리콘 bring-up과 시스템 통합 루트코즈
2. 공장 test-node·챔버 테스트 인프라 (Adam의 EOL 경험과 맞닿음)
3. telemetry 디버그 기능·SDK — 다른 엔지니어가 쓰는 것을 만들었다

**Adam의 언어**: platform · reuse · variants · EOL test · IPC · CI · OTA

**말투**: 답은 60~90초, 스토리는 2분. 모르면 "직접 해보진 않았지만…"으로 시작해서 원리로 답한다.

**마무리 질문**: "레벨을 가르는 기준이 뭔가요?"

---

## 9. 반드시 지킬 것 — 일관성과 정직

### 9.1 이미 Devin에게 한 이야기
- SK hynix에서 **7년 3개월**, 데이터센터 펌웨어만 / Apple로 간 이유는 consumer device 경험 / R&R 한계 / 1년 반 전 recruiting 메일을 찾아 콜드메일을 보냄 / base **$200K 이상**
- ⚠️ **기간 표기 주의**: 레쥬메상 Oct 2018 ~ Nov 2025는 약 **7년 1개월**이다. Devin에게 "7년 3개월"이라고 했다면, Adam에게는 **"about seven years"** 로 말해서 어긋나지 않게 하자.

### 9.2 없는 경험을 만들지 말 것
| 주제 | 실제 | 이렇게 말한다 |
|---|---|---|
| Board A/B 변종 관리 | 직접 경험 명시 없음. 멀티 아키텍처 bring-up은 있음 | "직접 second-source 보드를 맡은 적은 없지만, 여러 아키텍처와 FPGA→ASIC에서 경계를 어디에 그어야 하는지 배웠습니다" |
| OTA / 보안 업데이트 | 직접 경험 없음 | 원리(A/B, 서명, 롤백)로 답하고 "어떻게 하고 있나요?"로 되묻기 |
| Bazel, embedded Linux, 드론 flight stack | 없음 | 솔직히 없다고 말하고, 가장 가까운 경험과 학습 계획 |
| 기능안전(ISO 26262) | Adam의 이력에 명시 없음 | **Adam에 대해 단정하지 말 것** |

### 9.3 인용할 때 단서를 붙일 것
- Board A/B, Bandit 속도, "Super IC" 표현은 영상 요약본에서 나온 것 [영상요약·미검증] → **"제가 본 CTO 인터뷰에서는…"** 이라고만.
- 연봉: 이 인터뷰에서는 **먼저 꺼내지 않는다.** 물으면 "Devin과 이야기했고, 레벨이 정해지면 다시 맞추겠다"

### 9.4 사내 기밀
Apple·SK hynix의 **칩 이름, 내부 수치, 미공개 제품은 일반화**해서 말한다. 발표 자료(온사이트)에도 넣지 않는다.

---

## 10. 인터뷰 중·직후

**중간에 메모할 것**: Adam이 강조한 단어, 반응이 좋았던 스토리, 레벨 힌트, 온사이트 일정, 그가 "아픈 문제"라고 말한 것

**끝나고 5분 안에**
- [ ] 받은 질문을 그대로 적기
- [ ] 반응이 좋았던 스토리, 막혔던 부분 표시
- [ ] 레벨·팀·다음 단계 메모
- [ ] 이 폴더에서 `claude`를 열어 말하기: "Adam 인터뷰 했어. 받은 질문은 ..., 다음 단계는 ..."

---

## 11. 관련 자료

- 배경·Adam 이력 상세: `neros_hm_adam_technical_prep_2026-09-18.md`
- 회사·적합도·진행 로그: `neros_tech_senior_firmware_engineer_context.md`
- 코드 연습: `practice/` (`make prob N=...`), 아키텍처 대시보드 `practice/html/index.html`
- 아키텍처 리서치 검증판: `2026-09-17_neros_low_latency_architecture_whitepaper_verified.md` (영어) / `_verified_kor.md` (한국어)
