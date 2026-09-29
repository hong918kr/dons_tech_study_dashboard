# 문제 제작 규격 — "벤더 API 감싸기" 유형 (Verkada 1차 스크리닝 재현)

## 0. 왜 이 유형인가

2026-09-25 Verkada 1차 기술 스크리닝에서 나온 문제의 골격이다.

> **주어진 벤더 API가 블로킹이거나 다루기 불편하다. 이것을 non-blocking · thread-safe API로 감싸라.**
> **Part 1 = 동시성**(전담 스레드 1개 + 공유 상태 publish) → **Part 2 = 자료구조**(히스토리/집계 질의)

Don은 이 유형을 거의 풀지 못했다. 그래서 **같은 골격을 10번 반복**해 손에 익히는 것이 목표다.
원본 문제는 여기에 있다 — 읽고 똑같은 모양으로 만든다:
`../../verkada_sr_embedded_linux_engineer_connectivity/verkada_sep_25_2026_1st_screening_questions/`

## 1. 폴더 구조 (문제 1개 = 디렉토리 1개)

```
NN_slug/
  question_note           문제 지문 (원본 question_note 형식 그대로)
  <vendor>.h              주어지는 벤더 헤더 (수정 금지 대상)
  <api>.h                 내가 구현할 API 선언
  <api>.c                 ★ 연습용 stub — TODO만 있고 컴파일은 됨
  <api>_solution.c        모범답안 (다 풀고 나서 여는 것)
  main.c                  테스트 하네스 + 가짜 벤더 구현 (주어짐)
  main.sh                 빌드/실행:  ./main.sh | ./main.sh sol
```

`main.sh`는 원본과 동일한 인터페이스로:

```sh
#!/bin/bash
set -e
cd "$(dirname "$0")"
mkdir -p build
case "${1:-mine}" in
  mine) SRC=<api>.c ;;
  sol)  SRC=<api>_solution.c ;;
  *) echo "usage: $0 [mine|sol]"; exit 2 ;;
esac
cc -std=c11 -O2 -Wall -Wextra -pthread -o build/a.out main.c "$SRC" -lm
./build/a.out
```

## 2. question_note 형식 (원본과 같은 순서)

```
<제목 한 줄>
연습 문제 — Verkada 1차 스크리닝 유형 (Part 1 동시성 / Part 2 자료구조)

Files in this folder
  ...

------------------------------------------------------------------------------
Background
  <제품 맥락 2~4줄 — Verkada 제품군(게이트웨이/카메라/센서/트레일러/출입)에서 실제로 있을 법한 상황>
  <벤더 API 시그니처와 동작 표: 무엇을 반환하고 얼마나 블로킹하는지, 특이 케이스>

------------------------------------------------------------------------------
Tasks
  Part 1. <동시성 과제 — non-blocking, thread-safe 접근자>
  Part 2. <자료구조 과제 — 히스토리/범위/집계 질의>

------------------------------------------------------------------------------
Expected behaviour
  <하네스가 무엇을 PASS/FAIL로 검사하는지 4~8줄>

------------------------------------------------------------------------------
Things the interviewer is listening for   (코드 짜기 전에 종이에 답해볼 것)
  1. ...   (10개. 원본처럼 동시성 5 + 자료구조 3 + 에러/수명 2 비율)
```

## 3. 하네스(main.c) 필수 요건

1. **가짜 벤더 구현**을 main.c 안에 둔다. 지연은 `usleep`으로 흉내 내되 **전체 실행 10초 이내**.
   원본의 "1초"는 이 연습에서 **50~100ms로 축소**하고, question_note에 그 사실을 적는다.
2. **난수는 직접 만든 xorshift**를 고정 시드로 쓴다. `rand()` 금지 — 플랫폼마다 수열이 달라진다.
3. **정답(ground truth)을 하네스가 따로 기록**해 두고 그것과 비교해 PASS/FAIL을 찍는다.
   하드코딩된 기대값 금지 (원본이 macOS에서 깨진 이유).
4. 검사 항목에 **반드시 포함**:
   - Part 1 정확성 (값이 맞는가, 초기화 직후 동작)
   - Part 2 정확성 + **경계 조건** (미래 시각 / 범위 밖 / 첫 샘플 이전 / 빈 상태)
   - **스레드 안전성 스트레스**: reader 3~4개가 2초간 돌며 "존재한 적 없는 값 / 찢어진 값"이 안 나오는지
   - **getter가 블로킹하지 않음**: 최악 지연을 측정해 벤더 블로킹 시간보다 훨씬 작은지
   - 종료: `deinit` 후 스레드가 정리되는지
5. 마지막 줄은 정확히 `ALL CHECKS PASSED (0 failed)` 또는 `N CHECK(S) FAILED`, 실패 시 exit≠0.
6. `main.c`는 **stub과 solution 양쪽에서 그대로** 쓰인다(수정 금지).

## 4. stub(<api>.c) 요건

- 컴파일·실행되지만 대부분 FAIL이 뜬다. **멈추면 안 된다**(구현 안 된 상태에서 hang 금지).
- 각 함수 위에 `/* TODO: ... */`로 무엇을 해야 하는지 2~4줄. 답을 적지는 않는다.
- 필요한 include와 파일 상단 주석(빌드 방법)은 채워 둔다.

## 5. 모범답안 요건

- `cc -std=c11 -O2 -Wall -Wextra -pthread` **경고 0**, 실행 시 **전부 PASS**.
- 가능하면 `-fsanitize=thread`로도 돌려 경고 0 (의도된 race가 있으면 주석으로 이유 명시).
- 주석은 **왜 이렇게 했는지** 위주로. 면접에서 말할 문장이 그대로 보이게.
- 동시성 선택(mutex / atomic / seqlock / 더블버퍼)과 자료구조 선택의 **근거를 파일 상단 주석에** 3~6줄.

## 6. 플랫폼

- **macOS(Apple clang) + Linux 양쪽에서 빌드·실행**되어야 한다.
- Linux 전용 API 금지: `pthread_condattr_setclock`, `timerfd`, `epoll`, `clock_gettime(CLOCK_MONOTONIC_RAW)` 등.
  타임아웃 대기는 `pthread_cond_timedwait_relative_np`(`__APPLE__`) / 절대시각(`그 외`) 분기.
- 시간 함수는 `gettimeofday` 또는 `clock_gettime(CLOCK_MONOTONIC)`(macOS도 지원) 중 하나로 통일.

## 7. 검증 (제작자가 직접 실행할 것)

```sh
cd NN_slug
./main.sh sol      # 전부 PASS, exit 0
./main.sh          # 빌드·실행되고 FAIL이 뜸 (hang 금지)
cc -std=c11 -O2 -Wall -Wextra -pthread -fsanitize=thread -o /tmp/t main.c <api>_solution.c -lm && /tmp/t
```
세 가지가 모두 통과해야 완료다. 실행 시간은 각각 10초 이내.

## 8. 품질 기준

1. **정확성 최우선.** 컴파일 안 되거나 하네스가 틀린 답을 PASS시키면 없느니만 못하다.
2. 문제는 **10~25분 안에 Part 1을, 다시 15~25분 안에 Part 2를** 풀 수 있는 크기.
3. 시나리오는 Verkada 제품(게이트웨이·카메라·센서·트레일러·출입통제·모뎀)에서 가져오되 억지로 끼워맞추지 않는다.
4. 한국어 지문 금지 — **question_note와 코드 주석은 영어**(실제 면접이 영어이므로). 단 `notes` 성격의 추가 설명이 필요하면 영어로 간결하게.
5. 문제마다 **동시성 기법과 자료구조가 서로 달라야** 한다(배정표 참조).
