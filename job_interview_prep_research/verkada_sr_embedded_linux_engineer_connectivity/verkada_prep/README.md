# Verkada 임베디드 면접 — C 문제 은행 & 스터디 대시보드

**Senior Embedded Linux Engineer, Connectivity (San Mateo)** 기술 세션 대비 문제 은행.
2026-09-15 리크루터(Davis) 안내 메일이 지정한 4개 주제 — thread safety · 동기화(mutex/
condvar/atomic) · 실시간 데이터 처리(double buffering) · 모듈화·테스트 가능한 설계 — 를
드릴 가능한 C 문제로 쪼갠 것이다. 형식은 `anduril_firmware_engineer/` 키트와 동일하다.

> 이 세션은 알고리즘 퍼즐이 아니라 **동시성 자료구조 구현 + 설계 설명**이다.
> 코드가 돌아가는 것만으로는 부족하고 *왜 이 설계인지*를 말로 설명해야 한다.

---

## 🚀 빠르게 시작

```sh
open index.html                     # 대시보드 (오프라인 동작, 진행률 자동 저장)

make list                           # 세트 목록
make prob N=02_condvar_queues       # 연습 stub 컴파일+실행 → 직접 구현 (FAIL→PASS)
make sol  N=02_condvar_queues       # 정답 컴파일+실행 (전부 PASS 확인)
make tsan N=02_condvar_queues       # ThreadSanitizer 로 레이스 검출
make test                           # 전 세트 회귀
make check                          # JSON 스키마 + 컴파일 + 실행 + stub 전체 검증
make dash                           # data/*.json → index.html 재생성
```

**연습 루프:** `problems/NN.c` 의 `// TODO` 를 구현 → `make prob N=NN` → FAIL 이 PASS 로
바뀌는지 확인 → 막히면 대시보드에서 hint 1→2→3 → 그래도 막히면 `solutions/NN.c` 를 본 뒤
**다시 맨손으로**.

---

## 🧩 문제 은행 (8 세트 · 78 문제 · 592 체크)

| # | 세트 | 문항 | 범위 | 한 줄 |
|---|------|-----|------|-------|
| 01 | 🧵 스레드 & 뮤텍스 기초 | 10 | Q1-10 | pthread 스레드 생성/조인부터 mutex 로 불변식을 지키는 법까지, Verkada 인터뷰 1번 주제인 thread sa |
| 02 | 🔐 조건 변수 & 블로킹 큐 | 10 | Q11-20 | mutex + condition variable 로 bounded blocking queue 를 바닥부터 쌓아 올리는 세트다 |
| 03 | ⚛️ Atomic 연산 & Lock-free | 10 | Q21-30 | C11 `<stdatomic |
| 04 | 🎞️ 실시간 버퍼링 (더블/트리플 버퍼) | 8 | Q31-38 | 센서·프레임처럼 '최신값이 곧 정답'인 데이터를 tearing 없이, 생산자를 막지 않고 전달하는 패턴 모음 |
| 05 | 🐧 임베디드 Linux I/O & 이벤트 루프 | 10 | Q39-48 | 게이트웨이 연결 데몬을 이루는 POSIX I/O 부품 10개를 드릴한다 |
| 06 | 🌐 네트워크 · 프로토콜 파싱 | 10 | Q49-58 | IPv4/CIDR 비트 연산, 로그에서 IP 추출, RFC 1071 인터넷 체크섬, hex dump, AT 커맨드 응답 FSM |
| 07 | 🧩 모듈화 · 테스트 가능한 설계 | 10 | Q59-68 | 하드웨어 없이 테스트할 수 있는 임베디드 드라이버를 설계하는 법 |
| 08 | 📊 실전 자료구조 (이벤트·로그 처리) | 10 | Q69-78 | Verkada 에서 실제로 보고된 코딩 문제(LRU, Merge Intervals, Non-overlapping Interva |

모든 정답은 `cc -std=c11 -Wall -Wextra` **경고 0 · 전 테스트 PASS**, 동시성 세트는
**ThreadSanitizer 클린**(의도된 race 데모 제외)으로 검증했다.

---

## 📁 구조

```
verkada_prep/
├── index.html                # ★ 자체완결형 스터디 대시보드
├── dashboard.template.html   #   템플릿 (데이터 주입 전)
├── build_dashboard.py        #   data/*.json → index.html
├── validate.py               #   전체 검증 (make check)
├── Makefile                  #   prob / sol / tsan / test / check / dash
├── problems/   NN_topic.c    # ★ 연습용 stub — 여기에 직접 구현
├── solutions/  NN_topic.c    #   정답 (검증 완료)
├── notes/      NN_topic.md   #   세트별 스터디 노트 (면접에서 말할 문장 포함)
├── data/       NN_topic.json #   대시보드 데이터 (+ profile / roles / interview)
└── SPEC.md                   #   이 은행의 작성 규칙
```

## 📚 노트 읽기

세트별 `notes/*.md` 는 HTML 로도 렌더링되어 있다 — `open ../notes_site/index.html`
(기초 노트 11편 + 이 은행의 복습 노트 8편, 사이드 목차·진행바·읽음 표시).

## 🔗 같이 보는 문서

- `../verkada_concurrency_top10.md` — **빈출 10문제 통합 문서** (문제+해설+시스템 설계+영어 스크립트+3일 플랜)
- `../concurrency_practice/` — 위 10문제의 통짜 구현본 (면접 시뮬레이션용)
- `../verkada_sr_embedded_linux_engineer_connectivity_context.md` — 회사·적합도·인터뷰 프로세스 분석

## ⚠️ 플랫폼

macOS(Apple clang, arm64)에서 컴파일·실행하도록 작성했다. Linux 전용 API
(`epoll`, `timerfd`, `signalfd`, `pthread_condattr_setclock`)는 코드 대신 **노트에서
이론으로** 다룬다 — 면접에서는 말로 설명해야 하기 때문이다.
