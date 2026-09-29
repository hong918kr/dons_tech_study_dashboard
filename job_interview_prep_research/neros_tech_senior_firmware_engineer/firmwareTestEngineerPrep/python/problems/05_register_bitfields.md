# 🐍 05 · 레지스터 비트필드 & IMU 읽기 — mask/shift · RMW · signed int16 · struct

> I2C로 붙은 IMU(MPU-6050 계열 레지스터 맵)를 Python 테스트 헬퍼로 다룬다. WHO_AM_I 확인, 다른 비트를 건드리지 않는 read-modify-write, 두 바이트 → signed int16, 6바이트 burst read를 `struct`로 풀어서 g 단위로 스케일.

## 왜 나오나

- JD 필수: "Familiarity with embedded communication protocols — e.g. **I2C, SPI**, UART, Ethernet"
- JD 업무: "Design, develop, and maintain test suites to validate the Neros drone" — 센서 bring-up·보드 테스트의 첫 줄은 항상 "WHO_AM_I가 맞나"
- 비트 연산 + 부호 처리 + endian은 임베디드 테스트 면접의 단골 Python 문제. C 경험자는 쉽지만 **Python 정수에는 오버플로가 없어서** sign extension을 직접 해야 한다는 함정이 있다
- Don 경험: SoC verification에서 "I2C, SPI, DMA ... bring-up", Apple에서 I2C 인터페이스 루트코즈. 레지스터 레벨 이야기로 자연스럽게 이어진다

## 문제

> "We have an IMU on I2C at address 0x68. Write helpers to verify WHO_AM_I, change the accelerometer full-scale range without clobbering the other bits in that register, and read the three accel axes in g. Also write a function that turns two bytes into a signed 16-bit value for either endianness."

레지스터 (MPU-6050/6000 데이터시트):

| 레지스터 | 주소 | 내용 |
|---|---|---|
| WHO_AM_I | `0x75` | `0x68` 이어야 함 |
| ACCEL_CONFIG | `0x1C` | bits[7:5] self-test, **bits[4:3] AFS_SEL** (0=±2g, 1=±4g, 2=±8g, 3=±16g), 나머지 비트 보존 |
| ACCEL_XOUT_H ~ ACCEL_ZOUT_L | `0x3B`~`0x40` | X_H X_L Y_H Y_L Z_H Z_L, **big-endian** signed int16 |

- 감도: ±2g에서 16384 LSB/g, 범위가 두 배가 될 때마다 절반 (±16g → 2048 LSB/g)
- bus는 **smbus2**와 같은 메서드 이름: `read_byte_data(addr, reg)`, `write_byte_data(addr, reg, val)`, `read_i2c_block_data(addr, reg, n)`

```python
from smbus2 import SMBus
with SMBus(1) as bus:                         # /dev/i2c-1
    who = bus.read_byte_data(0x68, 0x75)
    raw = bus.read_i2c_block_data(0x68, 0x3B, 6)
```

구현할 것: `get_field`, `set_field`, `to_int16`, `Imu.check_whoami`, `Imu.set_accel_range`, `Imu.read_accel_g`

## 예시

```python
>>> get_field(0b1111_0111, 3, 2)
2
>>> set_field(0b1110_0111, 3, 2, 0b10)       # self-test 비트와 하위 비트는 그대로
247                                           # 0b1111_0111
>>> to_int16(0xFF, 0xFF), to_int16(0x80, 0x00)
(-1, -32768)
>>> Imu(bus).read_accel_g()                   # raw (-8192, 0, 16384) at ±2g
(-0.5, 0.0, 1.0)
```

## 엣지 케이스

- `set_field`에 width를 넘는 값 (2bit에 4) → `ValueError`. 조용히 잘라 넣으면 옆 필드를 덮는다
- `0x8000` 경계: `0x80 0x00` → -32768, `0x7F 0xFF` → 32767
- 레지스터에 이미 다른 비트가 켜져 있음 → RMW 후에도 유지돼야 함
- 쓰기가 안 먹힘 (write-protect, 전원 문제) → 다시 읽어서 **verify**
- 없는 주소 → 버스가 `OSError` (Linux에서 NACK는 보통 errno 121 Remote I/O error) — 삼키지 말고 올린다
- 범위를 바꾸면 같은 raw 값이라도 g 값이 달라진다 → 스케일은 **레지스터에서 읽은 현재 설정**으로 계산

## 힌트

1. `mask = (1 << width) - 1`. 읽기는 `(v >> shift) & mask`, 쓰기는 `(v & ~(mask << shift)) | (field << shift)`. Python의 `~`는 음수를 만들기 때문에 `& 0xFF`로 8비트로 되돌린다
2. sign extension: `v - 0x10000 if v & 0x8000 else v`. 또는 `int.from_bytes(b, "big", signed=True)`
3. 여러 필드를 한 번에 풀 때는 `struct.unpack(">hhh", bytes(raw))` — `>` big-endian, `h` signed int16

## 풀이 해설

- **RMW는 read → modify → write → verify 네 단계**로 말한다. verify는 테스트 코드라서 특히 중요하다. 테스트 도구가 설정을 잘못 넣고 모르면, 제품 버그로 오진한다
- **Python 정수 함정**: C의 `int16_t` 캐스트 같은 게 없다. `(hi << 8) | lo`는 항상 양수 → 직접 부호를 처리하거나 `signed=True`, `struct`의 `h`를 쓴다
- **왜 struct를 쓰나**: 6바이트 3축을 한 줄로, endian을 포맷에 명시. 필드가 늘어도 포맷 문자열만 바꾸면 된다
- **burst read가 중요한 이유**: X_H를 읽고 X_L을 따로 읽는 사이에 센서가 새 샘플로 갱신하면 상위·하위 바이트가 다른 샘플에서 온다 (tearing). 한 트랜잭션으로 연속 읽기 [일반적인 IMU 동작. 칩마다 데이터 레지스터 잠금 방식이 다름]
- **흔한 실수**
- `value & ~mask << shift` — 연산자 우선순위. `~(mask << shift)`로 괄호
- `read_i2c_block_data`가 list를 돌려주는데 `struct.unpack`에 그대로 넣음 → `bytes(...)`로 변환
- 스케일을 ±2g로 하드코딩

## 말하면서 풀기

- "For a read-modify-write I read the register, clear only the target field with an inverted mask, OR in the new value, write it back, and then read it again to verify. In test tooling, a silent misconfiguration looks exactly like a product bug."
- "Python integers don't overflow, so combining two bytes always gives a positive number. I need to sign-extend explicitly, or use int.from_bytes with signed=True, or struct with the 'h' format."
- "I read all six accel bytes in one burst transaction, so the high and low bytes come from the same sample."
- "I compute the scale factor from the range currently in the register, not from what I think I configured."

## 꼬리 질문

- 같은 테스트를 SPI IMU(ICM-42688 등)에 쓰려면? → bus 인터페이스(`read_reg`, `write_reg`, `burst_read`)를 추상화하고 I2C/SPI 구현을 갈아 끼운다. SPI는 보통 읽기 시 주소 MSB를 1로 세팅 [칩마다 다름]
- IMU 값이 "맞는지"는 어떻게 테스트? → 정지 상태에서 |a| ≈ 1g (허용 오차), 보드를 뒤집으면 z 부호 반전, self-test 비트로 내장 자가진단, 노이즈 표준편차 상한
- I2C가 가끔 NACK 나면? → 재시도 횟수와 에러율을 기록. 반복되면 풀업 저항·버스 속도·clock stretching 문제. 오실로스코프/로직 애널라이저로 확인 (Don: Apple I2C 루트코즈 사례 연결)
- 버스가 SDA low로 멈추면(bus hang)? → SCL을 9번 토글해서 슬레이브가 바이트를 끝내게 하는 bus recovery
- 이걸 HIL에서 쓰면? → FC 보드의 센서를 직접 읽는 대신, FC가 MSP로 보고한 값과 **외부 기준 센서** 값을 비교하는 교차 검증 테스트

## 파일

- [starter](../starters/05_register_bitfields.py) · [모범답안](../solutions/05_register_bitfields.py)
- 채점: `python3 python/run.py 05` (내 풀이) · `python3 python/run.py 05 --sol` (답안)
