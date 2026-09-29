# 🐍 06 · Telemetry rate check — 메시지 rate · gap · jitter 판정

> HIL 캡처 로그에서 메시지 타입별 rate가 기대값 ±10% 안인지, 끊김(gap)과 jitter가 없는지 판정하는 리포트 함수. defaultdict · sorted · zip · dataclass · two-pointer.

## 왜 나오나

- JD: "Design, develop, and maintain test suites to validate the Neros drone & ground control software", "Monitor the test results and ensure the stability of builds"
- 드론에서 telemetry가 50Hz로 와야 하는데 40Hz로 떨어지거나 0.2초씩 끊기면 GCS 화면이 멈추고, 링크 품질 판단이 틀어진다. **"기능은 되는데 타이밍이 틀린" 회귀**는 HIL에서 rate 체크로만 잡힌다.
- 면접관이 "로그에 timestamp랑 메시지 타입이 있다. rate가 맞는지 검증하는 코드 짜 봐"라고 던지기 딱 좋은 크기다.

## 문제

> "You captured telemetry from the flight controller on a HIL rig. Each sample is a `(timestamp_seconds, msg_type)` tuple. Write `analyze(samples, expected, tolerance=0.10, gap_factor=3.0)` that returns a per-message-type report: measured rate, whether it's within tolerance of the expected rate, any gaps longer than `gap_factor` times the nominal period, and the maximum jitter. Also flag any expected message type that never showed up."

한국어로: 캡처 샘플 리스트를 받아서 타입별로

- `rate_hz` = (개수 − 1) / (마지막 − 처음)
- `rate_ok` = |rate − 기대| ≤ tolerance × 기대 (기대값이 없으면 판정 안 함 = `None`)
- `gaps` = 연속 두 샘플 간격이 `gap_factor × (1/기대 Hz)` 보다 큰 곳의 `(시작 시각, 간격)`
- `jitter_s` = max |간격 − 평균 간격|
- 전체 `passed` = 빠진 타입 없음 AND rate 실패 없음 AND gap 없음

보너스: `max_count_in_window(timestamps, window_s)` — 길이 window 구간에 들어가는 최대 샘플 수 (burst 탐지).

## 예시

```python
samples = [(i * 0.02, "ATTITUDE") for i in range(100)]      # 50 Hz, 2초
rep = analyze(samples, {"ATTITUDE": 50})
rep.passed                         # True
rep.per_type["ATTITUDE"].rate_hz   # 50.0

# 0.78 s ~ 1.00 s 사이 10개가 빠짐
samples = [(i * 0.02, "ATTITUDE") for i in range(100) if not 40 <= i < 50]
rep = analyze(samples, {"ATTITUDE": 50})
rep.per_type["ATTITUDE"].gaps      # [(0.78, 0.22)]
rep.per_type["ATTITUDE"].rate_ok   # False (89 / 1.98 ≈ 44.9 Hz < 45)
```

## 엣지 케이스

- 샘플이 0개인 타입 → `missing`에 넣고 실패
- 샘플 1개 → span 0, rate 0.0 (0으로 나누기 금지)
- 캡처가 여러 스레드/포트에서 합쳐져 **timestamp 순서가 뒤섞여** 올 수 있다 → 타입별로 정렬
- 기대값 목록에 없는 타입(DEBUG 등) → 리포트엔 넣되 판정은 안 한다
- float 경계: 0.3 − 0.1 = 0.19999… 이다. 경계 테스트는 ms 정수로 하거나 tolerance를 둔다

## 힌트

1. `defaultdict(list)`로 `{타입: [timestamps]}`를 만든다. 한 번 순회, O(n).
2. 정렬한 뒤 `zip(ts, ts[1:])`로 연속 간격을 만든다. 여기서 max gap, 평균, jitter, gap 목록이 다 나온다.
3. 타입별 결과는 `@dataclass`로 묶는다. dict 여러 개보다 필드 이름이 명확하고, 실패 메시지에 그대로 찍힌다.

## 풀이 해설

- **구조**: `analyze`는 그룹핑만 하고, 타입 하나를 판정하는 `_analyze_one`으로 쪼갠다. 테스트하기도 쉽고 면접에서 설명하기도 쉽다.
- **rate 정의**: 개수/시간이 아니라 (개수−1)/span 이다. 100개를 0.02초 간격으로 받으면 span은 1.98초라서 100/1.98=50.5가 아니라 99/1.98=50.0이 된다. 이 차이를 말로 설명하면 좋다.
- **gap 기준**: 기대 주기의 3배. 1~2개 빠진 건 무선 링크에서 흔하니 허용하고, 3주기 이상 끊기면 실패. 이 숫자는 요구사항에서 와야 한다고 말할 것.
- **sliding window**: two-pointer로 O(n). `while t[right] - t[left] >= window: left += 1`.
- **복잡도**: 그룹핑 O(n) + 타입별 정렬 O(k log k) → 전체 O(n log n). 캡처가 이미 정렬돼 있으면 O(n).
- **흔한 실수**: 샘플 1개일 때 0으로 나누기 / 전체를 한 번에 정렬하지 않고 타입별 순서를 가정 / `rate_ok`를 기대값이 없는 타입에도 False로 판정 / float `==` 비교.

## 말하면서 풀기

- "First I'll group timestamps by message type with a defaultdict, then analyze each type independently — that keeps the per-type logic small and testable."
- "I define rate as (count minus one) over the time span, because N samples only cover N minus one intervals."
- "The gap threshold is three nominal periods. One or two dropped packets are normal on a radio link; three in a row is a real dropout. In a real project I'd take that number from the requirement."
- "I return a dataclass instead of a bare dict so the failure message in the CI report is self-explanatory."

## 꼬리 질문

- timestamp가 FC 쪽 시계와 호스트 쪽 시계 중 어느 것인가? 호스트에서 찍으면 USB/시리얼 버퍼링 때문에 jitter가 부풀려진다 → **디바이스 timestamp를 메시지에 넣는 게 정답**.
- 샘플이 수백만 개면? → 스트리밍으로 타입별 last_ts, count, max_gap만 유지 (O(타입 수) 메모리).
- 평균 jitter 말고 p99 간격을 보고 싶다면? → `statistics.quantiles` 또는 정렬 후 인덱스.
- 이 체크를 CI에 넣으면 flaky해지지 않나? → tolerance와 gap_factor를 요구사항에서 가져오고, 호스트 부하가 큰 러너에서는 디바이스 timestamp만 쓴다. 실패하면 캡처 파일을 artifact로 남긴다.

## 파일

- [starter](../starters/06_telemetry_rate_check.py) · [모범답안](../solutions/06_telemetry_rate_check.py)
- 채점: `python3 python/run.py 06` (내 풀이) · `python3 python/run.py 06 --sol` (답안)
