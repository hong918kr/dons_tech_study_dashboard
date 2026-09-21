# 🚦 유한 상태 기계 (Finite State Machine, FSM) — Q76~85

> 임베디드 펌웨어의 **제어 흐름 뼈대**. 버튼 디바운스, 프로토콜 파서, 통신 링크,
> 모터/미션 상태, 워치독까지 — "지금 무슨 상태이고, 이 입력에 어떻게 반응하나"를
> 명시적으로 표로 만든 것이 FSM 이다. Anduril 같은 국방/드론 펌웨어 면접에서
> **UART 패킷 파서 FSM** 은 거의 단골. 링 버퍼(이전 토픽)로 바이트를 모으고,
> FSM 으로 프레임을 조립한다.

---

## 1. FSM 란 무엇인가

유한한 **상태(state)** 집합 + **전이(transition)** 규칙 + (선택) **동작(action)**.
매 tick 또는 매 이벤트마다: `현재상태 × 입력 -> 다음상태 (+ 출력/동작)`.

세 가지 구현 스타일:

| 스타일 | 언제 | 장단점 |
|---|---|---|
| **switch/case** | 상태 ≤ 5~6개 | 가장 명료, 디버깅 쉬움. 커지면 case 폭발 |
| **함수 포인터 테이블** | 상태 많음 / 런타임 재구성 | `table[state](evt)` O(1) 디스패치, 확장 쉬움 |
| **상태 전이 표(2D)** | 규칙이 데이터로 표현 | `next[state][evt]`, 매우 조밀하지만 action 얹기 번거로움 |

---

## 2. Mealy vs Moore ⭐ (개념 단골)

| | **Moore** | **Mealy** |
|---|---|---|
| 출력 | `f(상태)` — 상태만의 함수 | `f(상태, 입력)` — 상태+입력 |
| 상태 수 | 보통 더 많음 (출력용 상태 필요) | 더 적음 |
| 타이밍 | 출력이 **레지스터드**(클럭 동기), 1클럭 지연 가능, **글리치 없음** | **조합 출력**, 더 빠름, 입력 글리치가 출력에 샐 수 있음 |
| 예 | "11 검출됨" 전용 상태 S2 를 두고 출력=1 | S1 에서 입력 1 오면 즉시 출력=1 |

> **한 줄 답변**: "Moore 는 출력이 상태만의 함수라 글리치가 없고 예측 가능하지만
> 상태가 더 필요하다. Mealy 는 상태+입력이라 같은 동작을 더 적은 상태로 하고
> 반응이 한 박자 빠르지만, 입력 글리치가 출력에 반영될 수 있다."

이 문제 세트(Q80)는 overlapping **"11" 검출기**를 Moore(3상태)·Mealy(2상태)
양쪽으로 구현해 같은 검출 벡터가 나오는지 비교한다.

---

## 3. 계층적 상태 기계 (HSM) — 개념 (Q81)

상태가 많아지면 **공통 전이**가 중복된다(예: 어느 상태에서든 POWER_OFF → OFF).
HSM 은 상태를 **중첩(superstate/substate)** 시켜, 자식이 처리 못 한 이벤트를
**부모로 버블링**한다. 공통 동작을 부모 한 곳에만 쓰면 되므로 중복 제거.

```
   [ON]  (superstate)  ── EVT_POWER 를 여기서 처리 → OFF
    ├─ IDLE  ── EVT_START → RUN
    └─ RUN   ── EVT_STOP  → IDLE
   [OFF]     ── EVT_POWER → IDLE
```

- RUN 에서 `EVT_POWER` → RUN 은 모름 → **부모 ON 이 처리** → OFF (버블링).
- IDLE 에서 `EVT_START` → 자식이 직접 처리 (leaf).
- UML statechart, QP/QF 프레임워크의 핵심 개념.

---

## 4. 실무 FSM 4대 요소

### (a) entry / exit action (Q82)
상태 **진입 시 entry**, **이탈 시 exit** 액션 자동 호출.
리소스 init/cleanup(타이머 시작/정지, DMA enable/disable)을 전이 로직과 분리 →
상태를 추가해도 대칭적으로 관리, 누락 방지.
```
전이(old→new):  exit(old)  →  state=new  →  entry(new)
자기전이(new==old): 무동작 (또는 정책에 따라 재진입)
```

### (b) guard condition (Q83)
같은 이벤트라도 **조건(guard)** 에 따라 다른 전이.
```
IDLE + RUN 이벤트:  value > 0 ?  → RUN
                                  → ERROR
```
guard 는 부수효과 없는 **순수 술어**여야 한다(면접 팁: guard 안에서 상태 바꾸지 말 것).

### (c) event queue (Q84)
**ISR 이 이벤트를 큐에 push, main loop 가 pop 해서 FSM 주입.**
ISR 문맥에서 무거운 FSM 로직을 떼어내 → 짧은 ISR + 결정적 처리 + 재진입 안전.
원형 버퍼(이전 토픽)가 그대로 이벤트 큐가 된다. full 이면 push 거부(또는 오버런 정책).

### (d) timeout event (Q85)
ACTIVE 상태에서 일정 tick 동안 활동이 없으면 TIMEOUT 으로 → **무응답 감지/복구**.
워치독, 링크 유실(link loss), 핸드셰이크 타임아웃의 전형. `event=1`(활동)이면
타이머 리셋, `0`(무활동)이면 증가시켜 limit 도달 시 전이.

---

## 5. UART 패킷 파서 FSM ⭐⭐ (Q78 — 이 토픽의 핵심)

바이트 스트림에서 프레임을 뽑아내는 **바이트 단위 FSM**. 드론 텔레메트리/명령
링크의 심장. 프레임:

```
[0xAA sync][ len (1..MAX) ][ data × len ][ checksum = XOR(data) ]
```

```
 IDLE  ──(byte==0xAA)──▶ LEN
 LEN   ──(1..MAX)──────▶ DATA        (그 외 길이는 폐기 → IDLE)
 DATA  ──(idx==len)────▶ CKSUM       (각 바이트 저장 + 러닝 XOR)
 CKSUM ──(match)───────▶ IDLE + 패킷 완료(good++)
       ──(mismatch)────▶ IDLE + bad++
```

**설계 포인트 (면접에서 파고드는 곳):**

- **재동기화(resync)**: IDLE 에서 sync 바이트 아니면 그냥 버린다 → 스트림 중간
  진입/노이즈에도 다음 프레임을 잡는다.
- **길이 검증**: `len` 을 그대로 믿지 말 것. `1..MAX` 범위 밖이면 폐기해야
  **버퍼 오버플로**를 막는다 (`data[idx++]` 가 배열을 넘지 않도록).
- **checksum/CRC**: 예제는 XOR(간단). 실무는 CRC-16/CRC-32. 불일치 시 프레임
  폐기 + 통계 카운트(bad_packets).
- **back-to-back / partial**: 한 바이트씩 들어오는(부분 수신) 상황에서도 상태가
  보존돼야 하고, 패킷이 끝나면 곧장 다음 sync 를 받을 수 있어야 한다.
- **payload 소비**: 완료 시 `out_data/out_len` 에 복사해 호출자가 안전하게
  꺼내가도록. 다음 패킷이 조립 버퍼를 덮어써도 무방.

> 확장 질문: "escape/byte-stuffing(SLIP, HDLC)은?", "가변 길이 필드/타입-길이-값
> (TLV)은?", "CRC 를 스트리밍으로 갱신하려면?", "sync 가 payload 에 나타나면?"
> → 길이 프리픽스 방식은 payload 안의 0xAA 를 신경 안 써도 된다(길이만큼만 읽으므로).

---

## 6. 버튼 디바운싱 FSM (Q76)

기계식 접점은 누를/뗄 때 수 ms 채터링. **N개 연속 동일 샘플**로 확정하는 4-상태 FSM:

```
 RELEASED ──high──▶ BOUNCE_PRESS ──(N tick 연속 high)──▶ PRESSED
 PRESSED  ──low ──▶ BOUNCE_RELEASE ──(N tick 연속 low)──▶ RELEASED
 BOUNCE_* 중 반대 입력 → 글리치로 판단, 원래 상태 복귀 (카운트 취소)
```

- `pressed`(bool) 만 확정된 출력 → 상위 로직은 채터링을 못 본다.
- 대안: **시프트 레지스터/카운터 적분기** (raw 를 `history<<=1|bit`, 특정 패턴이면 확정).
- 인터뷰 팁: "몇 tick? 샘플 주기?" → 접점 안정화 시간(예 5~20ms) / 폴링 주기로 산정.

---

## 7. 신호등 FSM (Q79) — 타이머 기반 실시간 제어

각 상태가 **자기 지속시간(tick)** 을 가지고 `RED→GREEN→YELLOW→RED` 순환.
```
tl_tick(): if (--timer <= 0) { 다음 상태로 전이; timer = 다음상태_지속시간; }
```
확장: 보행자 버튼(비동기 이벤트 삽입), 야간 점멸(모드 전환) → HSM 으로 승격하기 좋은 예.

---

## 8. 인터뷰 팔로업 (follow-ups)

- **"switch vs 함수 포인터 테이블, 언제?"** → 상태 적고 명료함 우선이면 switch,
  상태 많거나 런타임 재구성/플러그인이면 테이블.
- **"Mealy 를 Moore 로 바꾸면?"** → 출력 조합을 상태로 흡수(상태 수 증가), 출력 1클럭 지연.
- **"UART 파서에서 길이 검증 왜 필수?"** → 신뢰 못 할 `len` 으로 `data[]` 오버플로 → 원격 취약점.
- **"ISR 에서 FSM 을 직접 돌리면?"** → ISR 길어지고 재진입/우선순위 문제 → event queue 로 디커플.
- **"상태 폭발(state explosion) 대응?"** → HSM(계층화), 직교 영역(orthogonal region), 상태 변수 분리.
- **"FSM 테스트 전략?"** → 입력 시퀀스 → 상태/출력 벡터를 골든값과 비교(이 하네스가 그 방식).

---

## 9. 흔한 버그 체크리스트 (gotchas)

- [ ] UART: `len` 미검증 → `data[idx++]` 배열 오버플로.
- [ ] UART: 완료 후 IDLE 복귀 안 해서 다음 프레임 못 잡음(재동기화 실패).
- [ ] 디바운스: BOUNCE_* 에서 반대 입력 시 카운트 리셋/취소 누락 → 글리치 통과.
- [ ] entry/exit: 자기전이(new==old)에서 exit/entry 를 또 호출(정책 미정) → 리소스 이중 해제.
- [ ] guard: guard 안에서 부수효과(상태 변경) → 재현 불가 버그.
- [ ] event queue: full/empty 판정 오류, ISR↔main `volatile`/원자성 누락.
- [ ] timeout: 활동(event=1) 시 타이머 리셋 누락 → 정상인데 타임아웃.
- [ ] 상태 enum 에 `default` 없어 미정의 상태에서 침묵 → 방어적 `default` 로 안전상태 복귀.

---

## 10. Anduril / 드론 펌웨어 맥락

- **통신 링크 파서**: MAVLink 유사 텔레메트리/명령 프레이밍을 바이트 FSM 으로 파싱.
  링 버퍼(RX ISR) → FSM(main) → 명령 디스패치. 경합/저하 환경에서 **재동기화**와
  **CRC 폐기** 가 곧 링크 견고성.
- **미션/모드 상태 관리**: DISARMED→ARMED→TAKEOFF→MISSION→RTL→LAND 를 HSM 으로.
  FAILSAFE 는 어느 상태에서든 부모가 가로채는 전형적 버블링.
- **워치독/타임아웃**: 링크 유실·센서 무응답 시 TIMEOUT → 안전 상태(hover/RTL) 복귀.
- **디바운스/GPIO 이벤트**: 물리 스위치, 세이프티 인터록 확정.
- **테스트 가능성**: FSM 은 입력→출력이 결정적이라 단위 테스트와 HIL 에 잘 맞는다 —
  Anduril 이 중시하는 "검증 가능한 펌웨어" 와 직결.
