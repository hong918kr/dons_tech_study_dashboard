# 🔌 03 · 시리얼 센서 드라이버를 하드웨어 없이 테스트 — unittest.mock · 재시도 · 백오프

> 보드가 없는 CI에서 드라이버 코드를 어떻게 테스트하나? 포트를 `Mock`으로 바꾸고, 응답 '대본'을 `side_effect`로 주고, 어떤 바이트가 선로에 나갔는지 `call_args_list`로 검사한다. 피검 드라이버에는 재시도 · 지수 백오프 · 에러 분류 · 범위 검사 · `with` 정리가 들어 있고, 채점기가 mutant 9개로 테스트를 검사한다.

## 왜 나오나

- 리크루터 주제 **pytest** + Neros 테스트 팀의 실무 그 자체: HIL 프레임워크의 가장 아래층은 "DUT와 대화하는 드라이버"이고, **그 드라이버도 테스트 대상**이다 [추정]
- Full Stack 인터뷰어는 외부 API·DB를 mock 하는 데 익숙하다 → "시리얼 포트 = 외부 의존성"으로 프레이밍하면 같은 언어로 대화할 수 있다
- 이미 FTE 준비에서 드라이버를 **구현**하는 문제를 풀었다 → [FTE 문제 04 시리얼 DUT 드라이버](../../../firmwareTestEngineerPrep/site/problems/04_serial_dut_driver.html). 이번엔 **테스트하는 쪽**

## 문제

> "This driver talks to a temperature sensor over UART. We don't have hardware in CI. Write unit tests that would catch protocol mistakes, retry bugs, and resource leaks."

- 피검 코드: [lib/sensor_driver.py](../lib/sensor_driver.py) — docstring에 프로토콜이 있다
- `b"TEMP?\n"` → `b"TEMP=23.50\r\n"` / `b"ERR 3\r\n"`(재시도 안 함) / `b""`(타임아웃 → 재시도, 백오프 0.1 → 0.2 → …)
- `set_rate(hz)`: 1/10/50/100만 허용. 잘못된 값은 **선로에 아무것도 쓰지 않고** `ValueError`
- `sleep`은 생성자로 주입 → 테스트는 0초에 끝나고, 백오프 값도 검사 가능

## 예시 — Mock 기본 패턴

```python
from unittest.mock import Mock, call

port = Mock(spec=["write", "readline", "reset_input_buffer", "close"])
port.readline.side_effect = [b"", b"TEMP=21.0\r\n"]      # 1번째 호출은 타임아웃, 2번째는 응답
drv = SensorDriver(port, sleep=Mock())
assert drv.read_temperature() == 21.0
assert port.write.call_args_list == [call(b"TEMP?\n")] * 2
```

## 엣지 케이스

- 경계값 -40.0 / 125.0은 통과, 125.1 / -40.5는 에러 (범위 검사 누락 mutant)
- 음수 파싱 `TEMP=-12.25` (부호를 버리는 정규식 mutant)
- 타임아웃 2번 후 성공 → write 3번, sleep(0.1), sleep(0.2) (재시도 없음 · 고정 백오프 mutant)
- `ERR`는 재시도하지 않는다 → write 1번, sleep 0번
- 정확한 바이트 `b"TEMP?\n"` (`\r\n`을 보내는 mutant)
- 재시도마다 **쓰기 전에** 입력 버퍼 비우기 — 순서 검증 (flush 누락 mutant)
- `set_rate(5)` → write 호출 0번 (검증 전에 써 버리는 mutant)
- `with` 블록에서 예외가 나도 `close()` (정리 누락 mutant)

## 힌트

1. `Mock(spec=[...])` — 오타 난 메서드 이름(`port.wirte`)을 부르면 AttributeError가 나서 테스트 자체의 버그를 잡는다. 실제 클래스가 있으면 `create_autospec(serial.Serial)`
2. `side_effect` 리스트 = 호출마다 다음 값, 예외 인스턴스를 넣으면 그 호출에서 raise
3. 순서 검증: `manager = Mock(); manager.attach_mock(port.write, "write"); ...` 후 `manager.mock_calls`
4. 실수 비교는 `pytest.approx(0.1)` (부동소수 누적 오차)

## 풀이 해설

- **무엇을 mock 하나**: 경계(포트, 시계)만. 드라이버 내부 메서드(`_transact`)를 mock 하면 구현에 묶인 테스트가 된다 → 리팩터링마다 깨짐
- **의존성 주입 vs `monkeypatch`/`patch`**: 생성자로 받으면 패치가 필요 없다. 레거시 코드라 못 바꾸면 `monkeypatch.setattr(sd.time, "sleep", fake)` 또는 `with patch("lib.sensor_driver.time.sleep")` — **쓰는 쪽 모듈 경로**를 패치해야 한다는 함정이 단골 질문
- **에러 분류가 재시도 정책을 정한다**: 타임아웃(일시적) → 재시도, 센서가 보고한 에러(영구적) → 즉시 실패. 테스트로 이 정책을 **문서화**하는 셈
- **mock 테스트의 한계**를 먼저 말하기: "These prove the driver follows the protocol as I understand it. They can't prove the sensor actually behaves that way — so I'd keep one HIL smoke test against a real sensor, or a loopback, to validate the mock."

## 말하면서 풀기

- "The port is the boundary, so that's what I mock. I'm not mocking anything inside the driver — I want to be free to refactor it."
- "I'll script the responses with side_effect: timeout, timeout, then a good reading — and assert exactly what bytes went out and how long it backed off."
- "Retries should only happen for transient failures. A sensor-reported error isn't transient, so I assert there's exactly one write."

## 꼬리 질문

- "`patch` vs dependency injection?" → DI가 기본, `patch`는 바꿀 수 없는 코드에. 패치 경로는 '정의된 곳'이 아니라 '조회되는 곳'
- "How do you keep the mock honest?" → 실제 장치에서 녹취한 응답을 fixture 파일로 저장해 대본으로 재생(record/replay), 계약 테스트 1개는 HIL에서
- "pyserial `readline` blocks forever?" → `Serial(timeout=…)`을 반드시 설정, 아니면 블로킹. 드라이버 생성자에서 강제
- "Thread-safety?" → 포트 하나에 여러 스레드가 transact 하면 응답이 섞인다 → 드라이버에 `Lock`, 또는 포트당 워커 스레드 + 큐 ([문제 05](05_blocking_ring.md))

## 파일

- [starter](../starters/03_mock_serial_driver.py) · [모범답안](../solutions/03_mock_serial_driver.py) · 피검 코드 [lib/sensor_driver.py](../lib/sensor_driver.py)
- 채점: `python3 python/run.py 03` · `python3 python/run.py 03 --sol`
- 개념 복습: [FTE N04 10절 unittest.mock](../../../firmwareTestEngineerPrep/site/notes/2026-09-28_N04_pytest_framework.html) · [FTE 문제 08 retry/wait](../../../firmwareTestEngineerPrep/site/problems/08_retry_and_wait.html)
