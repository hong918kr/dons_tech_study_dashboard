# 📈 C03 센서 슬라이딩 윈도우 — 이동 평균 + K-of-N 알람

> "최근 N개"가 들리면 고정 크기 윈도우(= 덮어쓰는 ring). 함정: 초기 N개 미만 · None/NaN · 매번 sum()을 다시 하면 O(N) · 빠져나가는 값을 카운터에서 빼는 것.

## 1. 면접관 문장

> "We get a battery-voltage reading every 10 ms. Give me a smoothed value over the last N readings, and raise an alarm if at least K of the last N readings were below a threshold. Sometimes the sensor returns garbage."

## 2. 먼저 던질 질문

1. "Before we have N readings, return the average of what we have, or nothing?"
2. "What does garbage look like — None, NaN, out-of-range numbers?" "Skip it, or count it as bad for the alarm?"
3. "Should the alarm latch, or clear as soon as the condition clears?"
4. "Is N large? Do you need O(1) per sample?"

## 3. 손으로 (N=3)

| 입력 | 윈도우 | 합 | 평균 |
|---|---|---|---|
| None | [] | 0 | None (0으로 나누기 방지), invalid=1 |
| 3 | [3] | 3 | 3 |
| 6 | [3,6] | 9 | 4.5 |
| 9 | [3,6,9] | 18 | 6 |
| 12 | [6,9,12] | 27 | 9 (3을 빼고 12를 더함) |

## 4. 엣지 케이스

- [ ] 첫 입력이 None → 나누기 0 없음
- [ ] 워밍업 (N개 미만)
- [ ] NaN: `x < low`는 항상 False → **NaN 검사 먼저**
- [ ] N=1
- [ ] K=N (모두 나빠야 알람)
- [ ] 오래 돌려도 누적 합 오차가 작나 (brute force와 비교)

## 5. 접근

- 이동 평균: `deque` + `total`. 넣을 때 더하고, N 넘으면 `popleft()` 값을 뺀다
- K-of-N: 윈도우에 bool을 넣고 `bad` 개수를 유지. **넣기 전에** 꽉 차 있으면 빠질 값(`win[0]`)이 True인지 보고 빼기
- float 오차가 걱정되면: 정수(mV)로 받거나, 주기적으로 `sum()` 재계산

## 6. 테스트 핵심

```python
a = KofNAlarm(n=3, k=2, low=14.0)
assert [a.add(x) for x in (None, 15, float("nan"), 15, 15)] == [False, False, True, False, False]
```

- brute force 대조: 매 스텝 `sum(hist[-N:]) / len(hist[-N:])`와 비교

## 7. 후속 질문

| 질문 | 답 |
|---|---|
| 알람이 깜빡이면? | 히스테리시스: 켜는 조건 K, 끄는 조건 더 엄격하게 · 또는 최소 유지 시간 |
| 메모리를 더 줄이려면? | 지수 이동 평균(EMA) `y += a*(x - y)` — 윈도우 없음, 대신 "정확히 최근 N개"가 아님 |
| C로? | 고정 배열 + 인덱스, 합은 정수 |
| 중앙값이면? | 정렬된 윈도우(`bisect.insort`) O(N), 또는 힙 두 개 |
| 이걸 펌웨어에서 테스트하려면? | 녹화한 실제 센서 로그를 재생해서 알람 시각 비교 (재생 테스트) |

## 8. 정답 코드

- [c03_sliding_window.py](../code/c03_sliding_window.py) · 연습: `python3 code/run_all.py --practice c03`

## 9. 내 풀이 기록

| 날짜 | 걸린 시간 | 놓친 엣지 | 다음엔 |
|---|---|---|---|
| | | | |
