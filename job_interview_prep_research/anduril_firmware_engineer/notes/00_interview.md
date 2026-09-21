# Anduril 펌웨어/임베디드 SWE — 인터뷰 프로세스 정리

> 최종 갱신: 2026-08-01
> 출처: Blind, Glassdoor, interviewing.io, techinterview.org, Dataford, InterviewQuery 등 지원자 후기 종합.
> **신뢰도 표기**: `[확인됨]` = 복수 출처 일치 / `[추정]` = 단일·2차 출처 또는 일반화.
> 팀·직무·연도별 편차가 큼. **리크루터가 주는 공식 안내를 항상 최우선**으로.

---

## 1. 전체 흐름 (총 3~4주 소요)

```
리크루터 스크린(30분)
   → 기술 폰스크린 / HM 콜(45~90분, C/C++ 라이브 코딩)
      → 온사이트 "페어링 데이"(4~6시간, 라운드 3~4개)
```

가장 흔한 온사이트 구성 **[확인됨]**: 코딩 2 + 시스템/임베디드 설계 1 + 행동 1.
펌웨어 직무는 여기에 **도메인 심화 1~2라운드**가 붙는 경우 있음.

---

## 2. 단계별 상세

### (1) 리크루터 스크린 — 약 30분 `[확인됨]`
- 배경/경력, 지원 동기, **국방(방위산업) 업무에 대한 거부감 여부**.
- **미션 정합성(mission alignment)을 초반부터 강하게 검증** — 국방 일을 실제로 원하는 사람만 통과.
- 급여 기대치, 온사이트 가능 여부(대부분 **Irvine 본사 대면** 필요).

### (2) 기술 폰스크린 / HM 콜 — 45~90분, Zoom + CoderPad `[확인됨]`
- 구성: 인트로/이력 5~10분 → **라이브 코딩 35~40분** → 질문 5~10분.
- 언어: **C++**(임베디드/로보틱스 선호), Flight/Embedded **C** 역할은 C.
- 난이도: **LeetCode Medium**. 다만 "Medium 중 어려운 쪽"으로 흐르기도 함.
- 실제 폰스크린 후기(Blind): 정수 배열 2개에서 각 원소의 **가장 가까운 '센서' 매치**를 찾아 최대 거리를 구하는 문제(도메인 포장된 배열/투포인터). "알고리즘 자체는 안 어렵지만 **속도·스케일업 팔로업이 더 까다롭다**"는 평.
- ⚠️ 회사는 "리트코드 안 낸다"고 말하지만 **실제 문제는 표준 LC 포맷과 유사**하다는 후기 다수.
- 역할에 따라 HM 콜과 병행/대체.

### (3) 온사이트 / 페어링 데이 — 4~6시간 `[확인됨]`
- **① 코딩 1~2라운드 (CoderPad)**
  알고리즘이지만 임베디드/실시간 색채. `동작하는 코드`보다 **시간·공간 트레이드오프, 엣지케이스 추론** 중시. 팔로업("어떻게 더 빠르게/스케일업?")이 까다로움.
- **② 시스템/임베디드 설계 1라운드**
  자율 시스템·분산 센서·실시간 아키텍처. **실패 모드, 대역폭 한계, 노드/링크 단절 시 우아한 저하(graceful degradation)**.
- **③ 도메인 심화 1~2라운드 (펌웨어)**
  과거 프로젝트 설계 결정을 **아주 구체적으로 방어**. 면접관이 표면적 답을 넘어 계속 파고듦.
- **④ 행동/미션 정합성 1라운드**
  국방 업무 동기를 직접 질문. 대화형·FAANG보다 덜 형식적이라는 평.

---

## 3. 질문 스타일 한 줄 요약

**순수 LeetCode ↔ 실무형 임베디드의 하이브리드.**
- 폰스크린: C/C++ 라이브 코딩, LC Medium(가끔 Medium-Hard). 문제 배경이 드론/로봇/센서로 포장(예: 로봇 무리 우선순위 작업 추적 = 힙/PQ).
- 온사이트로 갈수록: 고정 메모리·지연 예산·센서 제약 하 **트레이드오프 추론**.
- 펌웨어 직무 추가: 임베디드 C 정통 주제 + 하드웨어 인지형 설계.
- **인터뷰 중 AI 도구 사용 엄격 금지.**

---

## 4. 빈출 테마

| 테마 | 예시 | 신뢰도 |
|---|---|---|
| 비트 조작 | 32비트 레지스터 비트 set/clear/toggle, 니블↔텍스트 변환 | `[확인됨]` |
| **링 버퍼** | UART 수신용 C 링버퍼, full/empty 구분, ISR-안전(producer=ISR) | `[확인됨]` |
| ISR/volatile/동시성 | volatile 언제·왜, 최소 ISR, 인터럽트-세이프 큐, priority inversion/deadlock | `[확인됨]` |
| **프로토콜 파싱 / FSM** | GPS류(NMEA 유사) 노이즈 스트림 파싱 FSM, 엔디안, 체크섬, 재동기화 | `[확인됨]`(Blind) |
| 실시간 제어 루프 | 고정익 비행 컨트롤러: 제어 100Hz·추진 10Hz·IMU 400Hz·GPS 1Hz 스케줄링, RTOS vs 슈퍼루프 | `[확인됨]`(Blind) |
| 하드웨어 버스 | CAN vs RS422 트레이드오프, CAN 메시징, 경합/저하 환경 에러 처리 | `[확인됨]`(Blind) |
| 표준 알고리즘 | product of array except self, atoi, 링크드리스트, 배열/문자열, 해시맵, 그래프, 정렬, 힙 | `[확인됨]` |
| 시스템/임베디드 아키텍처 | 센서 노드 펌웨어 구조, HW 주변장치 API 설계, 전력 관리 (일반 SWE는 Design TinyURL/Tetris/드론 신호관리) | `[확인됨]` |

---

## 5. 준비 팁

1. **임베디드 정통 + LC 알고리즘을 "둘 다"** — 한쪽만 하면 탈락 사유.
2. C/C++ **라이브 코딩** 손에 익히기(컴파일·엣지케이스까지 빠르게).
3. `동작`에서 멈추지 말고 **복잡도·엣지케이스·제약 하 트레이드오프를 소리 내어** 설명.
4. **폰스크린 준비 최소 2주** 확보(후기). 며칠 벼락치기는 부족.
5. **미션/국방 동기 구체화** — "멋진 기술이라서"는 약함. 국가안보 정합성 / 레거시 방산의 느린 속도에 대한 불만 / 군 관련 개인·가족 인연 등 구체적 서사가 고득점.
6. **과거 프로젝트 설계 결정 방어** — 왜 이 버스? 왜 이 스케줄링? 왜 이 메모리 레이아웃?
7. 담당 **제품/역할 사전 조사**(Lattice, Ghost, Bolt, Fury 등과 자기 역할의 관계).
8. **실패 모드 사고**를 설계에 반영(노드/링크 단절 시 graceful degradation).
9. **AI 도구 절대 금지**.
10. 대부분 **Irvine 본사 대면** — 이동·일정까지 계획.

---

## 6. 행동 면접 예시

- 왜 방위산업/국방에서 일하려 하는가? *(미션 정합성 — 가장 중요, 구체적 답 필수)*
- 가장 도전적이었던 기술 프로젝트, 규모와 기술적 난제는?
- 설계 결정에서 이해관계자와 충돌 시 해결 방법은?
- 이 특정 역할·팀에 왜 적합한가?
- 촉박한 마감/스트레스 상황 대응과 우선순위 결정은?
- 다분야(HW/시스템/SW) 팀과의 협업 경험은?
- 끝까지 오너십을 갖고 책임진 사례는?

---

## 7. 레드 플래그 (탈락 신호)

- 국방 동기가 모호함('멋진 기술', '돈') → 리크루터 단계부터 감점.
- 하드웨어·C 기본만 하고 **알고리즘/자료구조 소홀** → 코딩 라운드 대표 실패 패턴.
- 과거 프로젝트 설계를 **표면적으로만** 답 → 도메인 심화에서 걸림.
- 인터뷰 중 **AI 사용** → 즉시 탈락.
- **폰스크린 며칠 벼락치기** → 부족. 감 잃었으면 일정 미루라는 조언.
- 문화가 **강도 높고 요구 많음(intense/demanding)** — 스트레스 내성·오너십을 봄. 워라밸 기대와 미스매치 가능.
- "리트코드 안 낸다" 안내를 **곧이곧대로 믿고** 알고리즘 준비 스킵.

---

## 8. 참고 링크 (출처)

- Blind — [firmware engineer interview process](https://www.teamblind.com/post/anduril-firmware-engineer-interview-process-rke0bj0g)
- Blind — [Technical Phone Screen](https://www.teamblind.com/post/anduril-interview-technical-phone-screen-wnlmgfpm)
- Blind — [C++ interviews](https://www.teamblind.com/post/anduril-c-interviews-okqaq6go)
- Blind — [Anduril Software Interview (센서 배열 폰스크린 후기)](https://www.teamblind.com/post/Anduril-Software-Interview-SUOhG3nC)
- Blind — [worst phone screen experience](https://www.teamblind.com/post/had-my-worst-phone-screen-experience-with-anduril-oswm1hmp)
- [interviewing.io — Anduril Interview Process & Questions](https://interviewing.io/anduril-interview-questions)
- [techinterview.org — Anduril Interview Guide 2026](https://www.techinterview.org/companies/anduril-interview-guide/)
- [Dataford — Anduril Embedded Engineer Guide](https://dataford.io/interview-guides/anduril/embedded-engineer)
- [InterviewQuery — Anduril SWE Guide](https://www.interviewquery.com/interview-guides/andurilindustries-software-engineer)
- [Snubber — Anduril Interview Questions 2026](https://snubber.ai/blog/anduril-interview-questions)
- [Glassdoor — Anduril Firmware Engineer Questions](https://www.glassdoor.com/Interview/Anduril-Firmware-Engineer-Interview-Questions-EI_IE3546800.0,7_KO8,25.htm)
- [Anduril Careers — Firmware Engineer, Embedded Systems (JD)](https://job-boards.greenhouse.io/andurilindustries/jobs/5052359007)
