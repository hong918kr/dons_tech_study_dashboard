# 🐍 09 · DShot codec — 모터 출력 프레임 encode / decode / 검증

> FC가 ESC로 보내는 DShot 16비트 프레임을 encode·decode하고, logic analyzer 펄스 폭에서 비트를 복원한 뒤, 명령한 throttle과 실제 출력을 비교한다. 비트 연산 · dataclass · 검증 리포트.

## 왜 나오나

- JD Nice-to-have: "Experience with FPV Drone software including **Betaflight**, ExpressLRS, PX4, Ardupilot" + 필수: "embedded communication protocols"
- 드론 HIL 리그의 가장 기본적인 출력 검증은 "FC가 모터에 **무엇을** 보냈나"다. 모터를 실제로 돌리지 않고 신호선만 캡처해서 decode하면 안전하고 자동화하기 좋다.
- 비트 조작(`<<`, `>>`, `^`, `&`)을 Python으로 깔끔하게 하는지 보기에 좋은 문제. Don의 C 배경이 그대로 강점이 된다.

## 먼저 개념: DShot 프레임

| 비트 | 15 … 5 | 4 | 3 … 0 |
|---|---|---|---|
| 내용 | value (11비트) | telemetry request | CRC (4비트) |

- value 0 = motor stop, 1–47 = 특수 명령 (beep, 회전 방향, 설정 저장 등), **48–2047 = throttle (2000단계)** [확인됨: Betaflight DShot 문서, brushlesswhoop "DSHOT – the missing Handbook"]
- CRC: `v = (value << 1) | telemetry`, `crc = (v ^ (v >> 4) ^ (v >> 8)) & 0x0F`
- **bidirectional DShot**: CRC를 반전(`~`)하고 신호 극성도 반전된다. ESC가 이걸 보고 eRPM telemetry를 GCR 인코딩으로 같은 선에 되돌려 보낸다 → Betaflight RPM filter의 입력.
- 속도: DShot150/300/600 = 초당 150k/300k/600k 비트. DShot600은 비트 1.67µs, '1'은 high 1.25µs, '0'은 high 0.625µs (duty 약 75% vs 37.5%).
- 특수 명령은 여러 번 연속으로 보내야 적용되는 것들이 있고, 명령 전송 시 telemetry 비트를 세팅하는 관례가 있다 [추정 — 구현별로 다름, 면접에서 단정하지 말 것].

## 문제

> "The flight controller drives each ESC with DShot. Write `encode(value, telemetry, bidir)` and `decode(frame, bidir)` for the 16-bit frame, including the 4-bit checksum. Then, given logic-analyzer captures of the high-time of each bit, reconstruct the frame. Finally, write a checker that compares captured frames on each motor against the throttle we commanded, and reports CRC errors, out-of-tolerance values, and motors with no signal."

## 예시

```python
encode(1046)                    # 0x82C6 = 0b1000001011000110
decode(0x82C6)                  # DShotFrame(value=1046, telemetry=False, crc=6, crc_ok=True)
decode(0x82C7).crc_ok           # False
throttle_pct_to_dshot(0)        # 48
throttle_pct_to_dshot(100)      # 2047
check_motor_outputs({1: [encode(1000)]}, {1: 1000, 2: 1000})
# ["motor 2: no frames captured"]
```

손 계산 예 (면접에서 화이트보드로 보여 주기 좋음): value 1046, telemetry 0 → v = 2092 = 0x82C → 0x82C ^ 0x82 ^ 0x8 = 0x8A6 → & 0xF = 6 → 프레임 = 0x82C << 4 | 6 = **0x82C6**.

## 엣지 케이스

- Python int는 비트 수가 무한이다. `~x`는 음수가 된다 → **반드시 `& 0xF` 마스크**. C와 가장 다른 지점.
- value 범위 0–2047 밖 → ValueError. 16비트 밖의 frame → ValueError
- CRC가 틀린 프레임을 decode할 때 예외를 던지지 말고 `crc_ok=False`로 돌려준다 → 검증기가 "몇 개 틀렸나"를 셀 수 있어야 하니까
- 모드 불일치: 일반 DShot 프레임을 bidir로 decode하면 CRC가 전부 틀린다 → 리그 설정 오류를 잡는 신호
- `round(999.5)`는 Python에서 **1000** (banker's rounding, 짝수 쪽). 0.5 경계 값을 테스트할 때 주의
- 펄스 폭 jitter: 기준을 "주기의 50%"로 잡으면 ±0.2µs 정도는 흡수된다

## 힌트

1. encode: `v12 = (value << 1) | int(telemetry)` → `(v12 << 4) | crc`.
2. decode: `v12 = frame >> 4`, `crc = frame & 0xF`, `value = v12 >> 1`, `telemetry = v12 & 1`.
3. 펄스 → 비트: `frame = (frame << 1) | (1 if h > period / 2 else 0)`를 16번. 검증기는 모터 번호 정렬 → 프레임별로 CRC 먼저, 그다음 값 비교.

## 풀이 해설

- **decode 결과를 dataclass(frozen=True)로**: 필드 이름이 있고 불변이라 테스트 비교가 쉽다. `is_command`는 `@property`.
- **왜 4비트 XOR CRC가 1비트 오류를 항상 잡나**: v12의 각 비트는 세 개 니블 중 정확히 하나의 CRC 비트 위치에만 들어간다. 그래서 한 비트가 뒤집히면 CRC 한 비트가 반드시 뒤집힌다. 2비트 오류는 놓칠 수 있다 → 그래서 "CRC 에러율"을 지표로 본다.
- **검증 리포트 설계**: 첫 실패에서 멈추지 말고 **모든 문제를 모아서** 반환한다. HIL 한 번 돌리는 비용이 크니 한 번에 최대한 많은 정보를 남긴다.
- **복잡도**: 프레임당 O(1), 전체 O(캡처 프레임 수).
- **흔한 실수**: `~crc` 뒤 마스크 누락 / telemetry 비트 위치를 CRC 계산에서 빼먹음 / 비트 순서를 LSB 먼저로 처리 / CRC 오류를 예외로 던져서 나머지 검증이 멈춤.

## 말하면서 풀기

- "The frame is eleven bits of value, one telemetry bit, and a four-bit checksum. The checksum is computed over the twelve bits including the telemetry bit."
- "Python integers are unbounded, so after the bitwise NOT for bidirectional mode I have to mask to four bits — that's the main difference from C."
- "Decode never throws on a bad checksum. It returns crc_ok false, so the checker can count error rates instead of stopping at the first bad frame."
- "On the rig I'd capture the motor lines with a logic analyzer, not spin props — it's safe, deterministic, and I can check all four motors on every commit."

## 꼬리 질문

- 모터 출력이 명령값과 조금 다르다면 무조건 버그인가? → 아니다. FC의 mixer, PID, motor idle(dynamic idle), throttle limit 때문에 차이가 난다. 그래서 테스트는 **disarm 상태의 0**, **motor test 모드의 고정값**, 또는 **SITL과 같은 입력을 넣었을 때의 기대값**처럼 결정적인 조건에서 비교한다.
- 캡처 없이 FC가 보내는 값을 알 방법은? → Betaflight MSP로 motor 값 조회(MSP_MOTOR), blackbox 로그 [추정: 정확한 명령 ID는 확인 후 말할 것].
- CRC 에러가 간헐적으로 보이면? → 신호 무결성(케이블 길이, 접지, ESC 노이즈), 캡처 샘플레이트 부족(DShot600이면 최소 수 MHz 이상), 트리거 문제. Don의 오실로스코프·protocol analyzer 경험과 바로 연결된다.
- Don 경험 연결: Apple에서 I2C/SPMI/RFFE 버스 레벨 실패를 DSO와 protocol analyzer로 루트코즈한 경험 → "디코더가 말하는 것과 파형이 말하는 것이 다를 때 파형을 믿는다". (Don: 실제 사례 한 줄 채우기)

## 파일

- [starter](../starters/09_dshot_codec.py) · [모범답안](../solutions/09_dshot_codec.py)
- 채점: `python3 python/run.py 09` (내 풀이) · `python3 python/run.py 09 --sol` (답안)
- 참고: https://betaflight.com/docs/development/API/Dshot · https://brushlesswhoop.com/dshot-and-bidirectional-dshot/
