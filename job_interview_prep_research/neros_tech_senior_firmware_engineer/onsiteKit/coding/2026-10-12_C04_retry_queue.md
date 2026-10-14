# ⏱️ C04 명령 · ACK · timeout · retry (8비트 id)

> 테스트 호스트가 FC에 명령을 보내고 ACK를 기다리는 구조. 함정: 대기 목록 포화 · 늦게 온 ACK · 중복 ACK · **id가 255 다음 0으로 돌 때 아직 기다리는 id 재사용** · 시간은 주입받아야 테스트 가능.

## 1. 면접관 문장

> "The test host sends commands to the flight controller over a lossy link. Each command gets an ACK with the same id. If there's no ACK in 50 ms, resend, up to 3 tries total, then report it failed. Ids are one byte."

## 2. 먼저 던질 질문

1. "How many commands can be outstanding at once? What if a new one comes when we're at the limit — reject, or queue?"
2. "Is 3 tries total, or 3 retries after the first?" (경계값 혼동 방지)
3. "What should happen with an ACK for something we already gave up on?"
4. "Can I pass the current time in? It makes this testable without sleeping."

## 3. 손으로 (timeout 50 ms, 3 tries)

| 시각 ms | 사건 | inflight | 송신 수 |
|---|---|---|---|
| 1000 | submit B | {B: try1, 마감 1050} | 1 |
| 1049 | tick | 그대로 | 1 |
| 1050 | tick → 재전송 | {B: try2, 1100} | 2 |
| 1100 | tick → 재전송 | {B: try3, 1150} | 3 |
| 1150 | tick → 포기 | {} → failed=[B] | 3 |
| 1200 | ACK B | 없음 → stale_acks += 1 | |

## 4. 엣지 케이스

- [ ] 정상 ACK → 제거
- [ ] 같은 ACK 두 번 → 두 번째는 False
- [ ] 마감 직전/정확히 마감 (경계 포함 여부 결정)
- [ ] 정확히 max_tries 번 보내고 포기 (4번째 송신 없음)
- [ ] 포기 후 늦은 ACK
- [ ] 대기 한도 도달 → submit None
- [ ] 순서 바뀐 ACK
- [ ] id wrap: 254, 255, 0 … 아직 기다리는 id 건너뛰기

## 5. 접근

- `inflight: dict id -> [payload, tries, deadline]`
- `submit`: 한도 검사 → 다음 id (기다리는 중이면 건너뜀) → 저장 → 송신
- `on_ack`: 있으면 지우고 True, 없으면 카운트 + False
- `tick(now)`: 마감 지난 것마다 → 횟수 다 썼으면 failed로, 아니면 재송신 + 새 마감
- **시간은 정수 ms**: float 초를 쓰면 `1.10 + 0.05 = 1.1500000000000001`이라 정확히 1.15의 tick에서 놓친다 (이 키트 첫 버전이 실제로 걸림 — 면접에서 말하면 좋은 이야기)

## 6. 테스트 핵심

```python
wire = []
s = CommandSender(lambda i, p: wire.append((i, p)), timeout=50, max_tries=3, max_inflight=2)
b = s.submit(b"B", now=1000)
assert s.tick(1049) == [] and len(wire) == 1
assert s.tick(1050) == [] and len(wire) == 2
assert s.tick(1100) == [] and len(wire) == 3
assert s.tick(1150) == [b] and len(wire) == 3     # no 4th send
```

- 송신 함수를 가짜(list에 append)로 주입 → 무엇이 언제 몇 번 나갔는지 검사 가능

## 7. 후속 질문

| 질문 | 답 |
|---|---|
| inflight가 크면 tick이 O(n)? | 마감 순 `heapq` (지운 것은 lazy 삭제) |
| 재전송 간격을 늘리려면? | 지수 백오프 + 지터 |
| 늦은 ACK가 **재사용된 id**의 새 명령을 완료시키면? | id 공간 확대(16비트), 또는 ACK에 명령 종류/해시 포함, 또는 포기한 id를 일정 시간 격리 |
| FC 쪽에서 같은 명령을 두 번 받으면? | 멱등(idempotent) 처리: 최근 처리한 id를 기억했다가 다시 ACK만 |
| 실제 시간으로? | `now = int(time.monotonic() * 1000)` — `time.time()`은 시계 조정에 흔들림 |

## 8. 정답 코드

- [c04_retry_queue.py](../code/c04_retry_queue.py) · 연습: `python3 code/run_all.py --practice c04`

## 9. 내 풀이 기록

| 날짜 | 걸린 시간 | 놓친 엣지 | 다음엔 |
|---|---|---|---|
| | | | |
