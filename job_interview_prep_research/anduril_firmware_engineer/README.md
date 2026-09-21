# Anduril 펌웨어 엔지니어 — 면접 준비 문제 은행 & 스터디 대시보드

**Anduril Firmware / Embedded Engineer interview prep** — 문제 은행(problem bank) + 스터디 노트 + HTML 대시보드.
기존 `Defense_Anduril_FW_prep/c_Coding_Questions_100ea` 100문제를 다듬고, 실제 Anduril 채용공고를 분석해 재구성했다.

> 비공식 학습 자료입니다. 급여·레벨·인터뷰 절차는 추정이 섞여 있으니 리크루터(Stephanie)의 공식 안내를 우선하세요.

---

## 🚀 빠르게 시작

```sh
# 1) 대시보드 열기 — 브라우저에서 index.html 을 그냥 열면 됨 (오프라인 동작, 진행률 자동 저장)
open index.html
#    또는 로컬 서버로:  python3 -m http.server 8765  →  http://localhost:8765/index.html

# 2) 문제 풀기 (기계적 연습 루프)
make list                       # 토픽 목록
make prob N=01_bit_basics       # 연습용 stub 컴파일 + 실행 (직접 채워넣기)
make sol  N=01_bit_basics       # 정답 컴파일 + 실행 (전부 PASS 확인)
make test                       # 모든 정답 회귀 테스트 (전 토픽 컴파일+실행)
```

**연습 루프:** `problems/NN.c` 의 `// TODO` 를 직접 구현 → `make prob N=NN` → FAIL 이 PASS 로 바뀌는지 확인 → 막히면 대시보드에서 hint 1→2→3 → 그래도 막히면 `solutions/NN.c` 참고 후 **다시 맨손으로**.

---

## 📁 구조

```
anduril_firmware_engineer/
├── index.html                # ★ 자체완결형 스터디 대시보드 (더블클릭해서 열기)
├── dashboard.template.html   #   대시보드 템플릿 (데이터 주입 전)
├── build_dashboard.py        #   data/*.json → index.html 재생성 스크립트
├── Makefile                  #   문제 빌드/실행 (prob / sol / test / list / clean)
├── problems/   NN_topic.c    # ★ 연습용 stub — 여기에 직접 구현
├── solutions/  NN_topic.c    #   정답 (컴파일 clean, 전 테스트 PASS)
├── notes/      NN_topic.md   #   토픽별 스터디 노트 (+ 00_roles.md, 00_interview.md)
└── data/       NN_topic.json #   대시보드용 구조화 데이터 (+ roles.json, interview.json)
```

각 `.c` 파일은 자체 `main()` + PASS/FAIL 하네스를 가진 독립 실행 파일이다.
`solutions/` 는 `cc -std=c11 -Wall -Wextra` 로 **경고 0, 전 테스트 PASS** 를 검증했다 (총 496 체크).

---

## 🧩 문제 은행 (17 세트 · 180 문제)

사이드바가 **임베디드**와 **DSA (C)** 두 트랙으로 나뉜다. 모든 정답은 `cc -std=c11 -Wall -Wextra` 로 **경고 0 · 전 테스트 PASS** 검증(총 801 체크).

### 임베디드 (10 세트 · 111)
| # | 토픽 | 문항 | Anduril 연결 |
|---|------|-----|-------------|
| 01 | 비트 조작 기초 | 10 | 레지스터 set/clear/toggle → 디바이스 드라이버 |
| 02 | 비트 조작 심화 | 15 | 엔디안·비트필드·회전 → 프로토콜 바이트 순서 |
| 03 | 문자열 함수 | 15 | 파싱·버퍼 안전성 (atoi, strtok, sprintf) |
| 04 | 메모리 & 포인터 | 15 | `volatile`/`const`·정렬·정적 풀 (HW 레지스터) |
| 05 | 원형 버퍼 (링 버퍼) | 10 | **UART RX ISR↔main SPSC 무락** — 최빈출 |
| 06 | 연결 리스트 (malloc 없이) | 10 | 정적 노드 풀·free list (힙 없는 임베디드) |
| 07 | 유한 상태 기계 (FSM) | 10 | UART/CAN 패킷 파서, 디바운싱 |
| 08 | 데이터 처리 & 프로토콜 | 10 | 체크섬/CRC·고정소수점·필터 → 텔레메트리·제어 |
| 09 | RTOS & 임베디드 개념 | 7 | 워치독·ISR 안전성·우선순위 역전·인터럽트 지연 |
| 10 | 안두릴 실전 & 드론 응용 | 9 | 재현 문제(atoi/reverseBits) + I2C/CRC/IMU/PWM |

### DSA in C (7 세트 · 62) — 코딩 라운드(LeetCode-medium) 대비
> 기존 은행이 약했던 **그래프/트리/힙/DP/해시맵** 갭을 메움. Anduril 코딩 라운드는 C/C++로 LC-medium을 냄.

| # | 토픽 | 문항 | 포함 |
|---|------|-----|-----|
| 11 | 배열 & 해싱 | 10 | two-sum, product-except-self\*, top-k\*, Kadane, sliding window |
| 12 | 스택 & 큐 | 8 | 괄호, min-stack, 단조 스택/덱, RPN, decode-string |
| 13 | 트리 & BST | 9 | 순회(재귀/반복), 레벨오더 BFS, BST 삽입/검증, LCA |
| 14 | 힙 & 우선순위 큐 | 8 | heapify, k번째, **로봇 스웜 스케줄러\***, 스트림 중앙값 |
| 15 | 그래프 | 9 | BFS/DFS, num-islands, 위상정렬, union-find, Dijkstra |
| 16 | 동적 계획법 | 9 | 계단, coin change, LIS, 0/1 배낭, edit distance, LCS |
| 17 | 정렬 & 탐색 | 9 | 이진탐색 변형, merge/quick, quickselect, **2배열 최근접\*** |

`*` = 인터뷰 리서치에서 **실제 보고된** 안두릴 출제 유형.

### ⏱️ 45분 모의고사 (Timed Mock)
대시보드 **⏱️ 45분 모의고사** 탭 = 실제 폰스크린 시뮬레이션. easy→medium→hard **3문제 escalating 세트 8개** + 45분 카운트다운 타이머(시작/일시정지/리셋, 5분 남으면 경고색). 각 세트는 임베디드+DSA를 섞고 실제 출제 유형을 포함한다. **목표: 45분 안에 3번까지 완주** → 다음 라운드 진출 감각 훈련. 세트는 `data/mocks.json` 에서 편집.

### 🎯 타겟 기업 (Target Companies)
**🎯 타겟 기업** 탭 = Don 프로필에 맞는 **pre-IPO 하드웨어/펌웨어 기업 26곳**(5개 티어: ① AI 실리콘·인터커넥트 ② 방산 ③ 로보틱스/휴머노이드 ④ 우주·항공전자 ⑤ AI 랩). 각 카드에 밸류에이션·IPO 단계 배지·**Don 적합도(★)**·주의점. **2026-08 웹 리서치 기준**(밸류/IPO 시점은 빠르게 변하고 회사 공식 발표가 우선 — 투자 권유 아님, 취업 타겟팅용). 편집: `data/companies.json`.

---

## 🎯 역할 요약 (자세한 내용은 대시보드 → 역할 & 제품 / 인터뷰 프로세스)

- **사업부:** Air/counter-UAS (Ghost·Bolt·Anvil·Altius), 해양 AUV (Ghost Shark), Air Dominance CCA (Fury), Strike (Barracuda), Sensing/EW (Sentry·Pulsar), Lattice 플랫폼.
- **공통 요구:** 뛰어난 **C** + 언어 표준 이해, 베어메탈 & RTOS, MCU+센서 통합, I2C/SPI/CAN/UART/1553/ARINC 버스 브링업, JTAG/오실로스코프/로직분석기 디버깅, U.S. Secret clearance 취득 자격, ITAR(US Person).
- **인터뷰:** 리크루터 스크린 → C/C++ 폰스크린(LC Medium 수준) → 온사이트(코딩 2 + 임베디드/시스템 설계 1 + 행동 1). **LeetCode + 임베디드 둘 다** 준비 필수. 국방 미션 정합성(동기)을 강하게 봄.

---

## 🔧 대시보드 재생성

`data/*.json` 을 수정했다면:

```sh
python3 build_dashboard.py      # → index.html 다시 생성
```

대시보드는 외부 리소스·폰트·네트워크 없이 완전 자체완결형(오프라인)이며, 진행률은 브라우저 `localStorage` 에 저장된다.

---

## 📌 출처

원본 100문제: `../don-c-prac-master/0ESE_prep/Defense_Anduril_FW_prep/c_Coding_Questions_100ea/`
재현 문제: `../don-c-prac-master/interview_repro/anduril_atoi.c`, `anduril_reverseBit.c`
역할/인터뷰 근거: `data/roles.json`, `data/interview.json` (공고·Blind·Glassdoor 등, 각 항목에 [확인됨]/[추정] 표기).
