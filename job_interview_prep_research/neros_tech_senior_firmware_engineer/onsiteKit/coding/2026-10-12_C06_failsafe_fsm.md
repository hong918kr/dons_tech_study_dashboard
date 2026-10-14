# 🛡️ C06 arm / failsafe 상태 머신

> 드론 회사에서 나오기 좋은 상태 머신. 함정: 시간 경계(정확히 100 ms) · 복구 깜빡임 방지(연속 N 프레임) · 무장 조건 · **전이 로그를 남겨서 테스트 가능하게**.

## 1. 면접관 문장

> "The drone must only arm when throttle is low and the radio link is alive. If we stop hearing the radio for 100 ms while armed, go to a safe mode. Coming back should not flicker. Model it so I can test it."

## 2. 먼저 던질 질문 / 가정을 말하기

1. "From failsafe, do we go back to armed when the link returns, or do we always disarm?" → 이 풀이: 연속 5 프레임이면 ARMED, failsafe가 3초 넘으면 DISARMED (착륙 가정)
2. "Is exactly 100 ms a timeout?" → 이 풀이: `now - last_rx >= 100 ms`면 timeout
3. "Can I get time as an argument?" → 테스트 가능성
4. "Should a refused arm say why?" → 실제 시스템은 이유를 기록/비프

## 3. 상태 · 전이 표

| 현재 | 사건 | 조건 | 다음 | 로그 |
|---|---|---|---|---|
| DISARMED | arm 요청 | 스로틀 낮음 + link OK | ARMED | arm |
| DISARMED | arm 요청 | 아니면 | DISARMED | (거절) |
| ARMED | tick | link 끊김 ≥ 100 ms | FAILSAFE | rx timeout |
| ARMED | disarm 요청 | — | DISARMED | disarm |
| FAILSAFE | RX 프레임 | 연속 5개 (프레임 사이 공백 < 100 ms) | ARMED | link recovered |
| FAILSAFE | tick | failsafe 3초 이상 | DISARMED | failsafe landed |
| FAILSAFE | disarm 요청 | — | DISARMED | disarm |

> 화이트보드에서 **표를 먼저 그리고** 코드를 쓰면 면접관과 가정을 맞추기 쉽다.

## 4. 엣지 케이스

- [ ] link 한 번도 없이 arm → 거절
- [ ] 스로틀 높으면 거절
- [ ] 99 ms는 유지, 100 ms에 failsafe
- [ ] 복구 중 4개는 아직 failsafe, 5번째에 ARMED
- [ ] 복구 중 공백이 생기면 연속 횟수 다시 1부터
- [ ] failsafe 장시간 → DISARMED
- [ ] 전이 로그 순서

## 5. 접근

- 상태는 문자열 상수, 시간은 인자, 전이는 `_go(now, new, why)` 하나로만 → 로그가 자동으로 남는다
- `on_rx_frame`, `request_arm`, `request_disarm`, `tick` 네 개의 입력만 있다고 생각

## 6. 테스트 핵심

```python
m = FlightMode()
m.on_rx_frame(0.0); m.request_arm(0.01, throttle=0.0)
m.tick(0.099); assert m.state == ARMED
m.tick(0.100); assert m.state == FAILSAFE
```

- 테스트 직군 포인트: **로그(전이 기록)를 assert 한다** — 실제 HIL에서도 상태 전이 로그가 아티팩트가 된다

## 7. 후속 질문

| 질문 | 답 |
|---|---|
| 상태가 많아지면? | 전이 표를 dict `{(state, event): handler}`로 → 빠진 조합을 표에서 바로 봄 |
| ISR에서 RX 시각을 넣으면? | 시각 하나만 기록(원자적), 판단은 tick에서 |
| 이 로직을 어떻게 시스템에서 검증? | HIL에서 RX 끊기 · 노이즈 프레임 · 짧은 끊김 반복 → 모터 출력 시각 측정 |
| 시간 경계를 float로 하면? | 0.1 근처 부동소수 오차 → 정수 ms 권장 ([C04](2026-10-12_C04_retry_queue.md) 참조) |

## 8. 정답 코드

- [c06_failsafe_fsm.py](../code/c06_failsafe_fsm.py) · 연습: `python3 code/run_all.py --practice c06`

## 9. 내 풀이 기록

| 날짜 | 걸린 시간 | 놓친 엣지 | 다음엔 |
|---|---|---|---|
| | | | |
