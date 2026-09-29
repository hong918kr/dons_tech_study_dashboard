# 🐍 01 · UART 로그 파서 — 레벨 집계 · 에러 추출 · 테스트 결과 요약

> DUT가 UART로 뱉은 부트·테스트 로그를 정규식으로 파싱해서 레벨별 개수, 에러 목록, 첫 FAIL 시각, 테스트별 최종 결과를 뽑는다. 노이즈 섞인 줄에 죽지 않는 게 핵심. `re` · `Counter` · `dict`.

## 왜 나오나

- JD: "Monitor the test results and ensure the stability of builds before releases", "Strong development skills with a scripting language (e.g. Python) for test automation"
- HIL 테스트 결과의 1차 데이터는 대부분 **DUT 시리얼 로그**다. 로그에서 PASS/FAIL과 에러를 뽑아 CI 리포트로 올리는 코드는 테스트 엔지니어가 가장 먼저 짜는 것
- Python 라이브 코딩 첫 문제로 가장 흔한 유형: 문자열 파싱 + dict/Counter 집계. 15분 안에 깔끔하게 끝내는 게 목표

## 문제

> "Here's a chunk of serial log from our flight controller during a HIL run. Write a function that summarizes it: how many lines per log level, the list of errors with timestamp and module, the timestamp of the first failing test, and the final result of each test. The UART sometimes gives us garbage, so don't crash on bad lines."

로그 한 줄 형식:

```text
[  1.502000] ERROR imu: WHO_AM_I mismatch 0x00
[  2.100000] TEST  imu_selftest FAIL
```

- `[ts]` — 부팅 후 초 (float, 앞에 공백 패딩)
- LEVEL — `DEBUG` / `INFO` / `WARN` / `ERROR` / `TEST`
- ERROR 줄의 나머지 = `module: message`
- TEST 줄의 나머지 = `test_name PASS|FAIL`

`summarize(lines)`가 `counts`, `errors`, `first_fail`, `tests`, `malformed` 키를 가진 dict를 반환한다. 같은 테스트가 재시도로 여러 번 나오면 **마지막 결과**를 쓴다.

## 예시

```python
>>> s = summarize(SAMPLE)
>>> s["errors"]
[(1.502, 'imu', 'WHO_AM_I mismatch 0x00'), (3.5, 'rx', 'failsafe entered')]
>>> s["first_fail"]
2.1
>>> s["tests"]
{'imu_selftest': 'PASS', 'baro_read': 'PASS'}   # imu_selftest는 FAIL 후 재시도 PASS
```

## 엣지 케이스

- 빈 줄 → 무시 (malformed 아님)
- `garbage\x00\xff ...` 같은 UART 노이즈, `[  2.9000`처럼 잘린 줄 → `malformed += 1`, 계속 진행
- TEST 레벨인데 결과 토큰이 없음 → malformed
- ERROR 줄인데 `module:` 형식이 아님 → module `"?"`로 두고 메시지 보존
- 입력이 빈 리스트 → counts 비어 있음, first_fail `None`

## 힌트

1. 줄 하나를 `^\[\s*(ts)\]\s+(LEVEL)\s+(rest)$` 로 먼저 쪼갠다. named group(`(?P<ts>...)`)을 쓰면 `m["ts"]`로 읽혀서 가독성이 좋다
2. 정규식은 모듈 레벨에서 `re.compile` 한 번. 레벨별 세부 파싱(TEST, ERROR)은 두 번째 정규식으로 분리
3. `first_fail`은 `if ... and first_fail is None:` 한 줄. `tests[name] = result`는 덮어쓰기라서 자동으로 "마지막 결과"가 된다

## 풀이 해설

- **2단 파싱**: 공통 헤더(`[ts] LEVEL`)와 레벨별 본문을 분리한다. 로그 포맷이 바뀌면 한 곳만 고치면 된다
- **죽지 않는 파서**: 매치 실패 = `continue`. 실제 HIL에서는 전원 사이클이나 baud 불일치 때 노이즈가 섞이므로, 예외를 던지면 테스트 전체가 무너진다. 대신 `malformed` 개수를 **메트릭으로 남긴다**. 이 값이 갑자기 늘면 그 자체가 신호(케이블, 접지, baud 문제)
- **복잡도**: 줄 수 N에 대해 O(N) 시간. 메모리는 에러 목록과 테스트 수에 비례
- **흔한 실수**
- `float("  1.5")`는 되지만 `[\s*`를 빼먹으면 패딩 때문에 매치 실패
- `line.split()`만으로 파싱하면 메시지 안의 공백·콜론에서 깨짐 → 정규식이나 `split(maxsplit=...)`
- `first_fail`을 매번 덮어써서 "마지막 FAIL"이 되는 실수
- `\x00`이 섞인 bytes를 decode할 때 `errors="replace"`를 안 줘서 UnicodeDecodeError (실제 시리얼에서는 `raw.decode("utf-8", errors="replace")`)

## 말하면서 풀기

- "I'll split the parsing into two layers: a common header regex for timestamp and level, and then a per-level parser. That way a format change only touches one place."
- "Bad lines shouldn't fail the run. I'll count them as malformed and keep going. That count is also a useful health metric for the serial link."
- "For test results I'll just overwrite in a dict, so a retried test naturally reports its last result. If we care about flakiness, I'd keep the full history instead."

## 꼬리 질문

- 로그가 수 GB라면? → 파일을 줄 단위로 스트리밍(`for line in f`), 제너레이터로 파이프라인 구성, 결과만 누적
- 재시도로 PASS가 됐는데 이걸 PASS로 봐도 되나? → 리포트에서는 "passed on retry"를 따로 표시해 **flaky 후보**로 추적해야 한다 (문제 07과 연결)
- 이 결과를 GitLab CI에 어떻게 보여주나? → JUnit XML로 변환해서 `artifacts:reports:junit`
- 타임스탬프가 재부팅으로 0으로 돌아가면? → 단조 증가가 깨지는 지점을 부팅 경계로 보고 세션을 나눈다
- 여러 DUT 로그가 한 스트림에 섞이면? → 줄마다 DUT ID prefix를 붙이거나 포트별로 따로 읽는다

## 파일

- [starter](../starters/01_log_parser.py) · [모범답안](../solutions/01_log_parser.py)
- 채점: `python3 python/run.py 01` (내 풀이) · `python3 python/run.py 01 --sol` (답안)
