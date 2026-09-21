# Neros — Senior Firmware Engineer, Platform · C 연습 세트

Neros `Senior Firmware Engineer, Platform` (Torrance, $195–273K) 대비 **7토픽 42문제 + 아키텍처 리서치 맵**.
포맷은 `anduril_firmware_engineer/`와 동일하다 — `problems/`에 직접 채워넣고, `solutions/`로 답을 맞춰보고, `notes/`로 개념을 정리한다.

세 층으로 되어 있다.

| 층 | 세트 | 무엇 |
|---|---|---|
| **A. 플랫폼 (JD 직결)** | `01`~`04` | JD 문장을 그대로 문제로 바꾼 것 — logging / telemetry / configuration / IPC·budget |
| **B. 임베디드 기본기** | `05`~`06` | 어느 펌웨어 면접에나 나오는 것 — 레지스터·비트·엔디안·고정소수점 / ISR·틱·스케줄링 |
| **C. 드론 도메인** | `07` | 실제 신호 경로 — IMU 버스트 리드 → 필터 → RC 언팩 → 믹서 → DShot |
| **D. 아키텍처 리서치** | `notes/00_drone_architecture.md` | 코드가 아니라 **개념 지도**. 드론 펌웨어의 서브시스템별 해부 + 리서치 체크리스트 |

> Anduril 세트(`../../anduril_firmware_engineer/`)와 중복을 피했다. 링버퍼·문자열·DSA 기본기는 거기가 더 넓고,
> 여기는 그 위에 얹히는 **플랫폼 레이어 + 드론 도메인**에 집중한다.

---

## 🚀 사용법

```sh
cd neros_tech_senior_firmware_engineer/practice

make list                      # 토픽 목록
make prob N=01_ring_logging    # 연습용 stub — TODO를 채우고 FAIL이 PASS로 바뀌는지 확인
make sol  N=01_ring_logging    # 정답 실행 (전부 PASS 확인)
make test                      # 전체 회귀 (정답 7개 컴파일 + 실행)
make notes                     # 노트 HTML 재생성 + 스터디 대시보드 열기
make clean
```

**연습 루프**: `problems/NN.c`의 `// TODO`를 맨손으로 구현 → `make prob N=NN` → 막히면 `notes/NN.md`의 개념 정리 → 그래도 막히면 `solutions/NN.c` 확인 → **다음 날 다시 맨손으로**.

---

## 🧩 토픽 (7세트 · 42문제 · 123 체크)

### A. 플랫폼 — JD 문장을 그대로 문제로
| N | 토픽 | 문제 (각 6문제) | 체크 | JD 근거 |
|---|---|---|---|---|
| `01_ring_logging` | ISR-safe 로깅 프론트엔드 | SPSC 링(atomics), all-or-nothing bulk write + drop 카운터, 레벨 필터, deferred-format 레코드 encode/decode, drain | 16 | "own the common runtime … **logging**" |
| `02_telemetry_framing` | 텔레메트리 프레이밍 | endian-safe put/get, CRC-16/CCITT, COBS encode/decode, frame_build, **스트리밍 수신 FSM(재동기화·crc_err·overrun)** | 18 | "**telemetry**", bandwidth, 재밍 datalink |
| `03_config_store` | 버전 호환 config 저장소 | TLV put/find, fallback 규칙, v1/v2→v3 마이그레이션 + 미래 버전 관용, A/B 슬롯 선택, 전원 차단 안전 커밋 | 18 | "**configuration**", "keeping them stable as the platform evolves" |
| `04_ipc_budget` | IPC와 리소스 예산 | 블록 풀 allocator, 고정크기 메시지 큐, topic mask 필터, token bucket, 우선순위 대역폭 분배, 링크 저하 시 선택 | 18 | "**IPC**", "**resource trade-offs — compute, memory, bandwidth**" |

### B. 임베디드 기본기 — 어느 펌웨어 면접에나 나오는 것
| N | 토픽 | 문제 | 체크 |
|---|---|---|---|
| `05_embedded_core` | 레지스터·비트·엔디안·고정소수점 | 레지스터 RMW(volatile), 필드 extract/insert + 부호확장, bswap·비정렬 접근, Q15 곱셈/포화·ADC 정수 스케일링, strlcpy·memmove, 구조체 패딩 vs 와이어 포맷 | 18 |
| `06_isr_timing` | ISR 핸드셰이크·틱·스케줄링 | ISR→main 플래그 + missed 카운터, **틱 wraparound 안전 비교**, 32비트에서 64비트 틱 원자 스냅샷, GPIO 디바운스 FSM, 드리프트 없는 주기 스케줄러(backlog skip), 지연 작업 큐(bottom half) | 17 |

### C. 드론 도메인 — 실제 신호 경로
| N | 토픽 | 문제 | 체크 |
|---|---|---|---|
| `07_sensors_actuators` | 센서 → 필터 → RC → 모터 | IMU 레지스터 RMW·버스트 리드, big-endian 샘플 디코드·mdps 변환, 자이로 bias 캘리브레이션, 이동평균·median3·1차 IIR, **CRSF 11비트×16 채널 언팩 + failsafe**, **DShot 프레임 + quad-X 믹서** | 18 |

### D. 아키텍처 리서치 (코드 아님)
- **`notes/00_drone_architecture.md`** — 드론 펌웨어 전체 블록도, 제어 루프 타이밍 예산, 서브시스템 10개 해부
  (FC MCU / IMU / GNSS·광류 / ESC·모터 / 전원 / RC 링크 / 영상 / 컴패니언 autonomy / GCS / 페이로드),
  Betaflight vs PX4 비교, 제어·추정 개념, failsafe·진단, **양산·플랫폼 관점**(Don 강점), 방산·EW 맥락,
  그리고 **우선순위 Top 15 리서치 체크리스트** + 1차 소스 목록.

### 추천 순서
1. **`notes/00_drone_architecture.md` §1~§2** — 블록도와 타이밍 예산부터. 30분이면 대화의 질이 달라진다.
2. **`02_telemetry_framing`** — 출제 확률 1위. COBS+CRC+재동기화 파서는 이 직군의 시그니처.
3. **`07_sensors_actuators`** — 드론 도메인 갭을 가장 빨리 메운다. CRSF·DShot·믹서를 손으로 짜보면 면접 언어가 생긴다.
4. **`01_ring_logging`** — JD 첫 줄이 로깅.
5. **`06_isr_timing`** / **`05_embedded_core`** — 폰스크린 단골. 감이 무뎠다면 여기부터 워밍업.
6. **`04_ipc_budget`** → **`03_config_store`** — 시스템 설계 라운드용.

### 시간 배분 제안 (45분 모의고사식)
- 워밍업 10분: `05` Q1~Q3 (레지스터·비트·엔디안) 또는 `01` Q1~Q2 (SPSC)
- 메인 25분: `02` Q6 (스트리밍 파서 FSM) 또는 `07` Q5~Q6 (CRSF 언팩 + 믹서) — 온사이트 1문제 분량
- 마무리 10분: `04` Q5 (우선순위 예산 분배) 또는 `06` Q2 (틱 wraparound)

---

## 📁 구조

```
practice/
├── Makefile                  # prob / sol / test / list / clean
├── problems/   NN_topic.c    # ★ 연습용 stub — 여기에 직접 구현
├── solutions/  NN_topic.c    #   정답 (경고 0, 전 테스트 PASS 검증)
├── notes/      NN_topic.md   #   개념 정리 + 흔한 함정 + 면접 꼬리질문 (원본)
├── html/                     # ★ notes를 리서치 페이퍼 형식으로 렌더링한 뷰 (생성물 — 직접 수정 금지)
│   ├── index.html            #   스터디 대시보드: 노트 카드, 읽음 표시, 리서치 항목 진행률
│   └── NN_topic.html         #   노트별 페이퍼: 목차 사이드바, 읽기 진행바, 체크박스 저장, 이전/다음
└── build_notes_html.py       #   notes/*.md → html/ 생성기 (의존성 없음)
```

노트를 고쳤으면 `make notes`로 HTML을 다시 만든다. 체크박스·읽음 상태는 브라우저 localStorage에 저장된다.

각 `.c`는 자체 `main()` + `[PASS]/[FAIL]` 하네스를 가진 독립 실행 파일이다. 7개 solution 전부
`cc -std=c11 -Wall -Wextra -O0 -g`로 **경고 0 · 전 테스트 PASS**를 검증했다.

---

## 🔗 같이 볼 것

- `../neros_tech_senior_firmware_engineer_context.md` — 회사·적합도·예상 인터뷰 전체 (4.3 예상 기술 질문 표가 이 문제들의 출처)
- `../neros_phone_screen_prep_2026-09-16.md` — 리크루터 콜용 벼락치기 시트
- `../../anduril_firmware_engineer/` — 링버퍼·비트조작·RTOS·DSA 기본기 (17세트 173문제)
- `../../don-c-prac-master/concurrency_prac/` — 동시성 추가 연습

**코드로 커버되지 않는 갭**: Bazel cross-compile / hermetic build, embedded Linux(Yocto·kernel). 이 둘은 문제풀이가 아니라
**미니 프로젝트 + 개념 정리**로 채워야 한다 — 컨텍스트 파일 3.3 갭 표 참고.
