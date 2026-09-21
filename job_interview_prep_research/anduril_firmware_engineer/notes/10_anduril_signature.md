# 10. Anduril 시그니처 & 드론 응용 (Signature & Drone-Flavored) 🎯

> 실제로 재현된 Anduril 인터뷰 2문항(`my_atoi`, `reverseBits`)을 정석대로 다듬고,
> Anduril TRS(트래킹·무선·센서) 펌웨어 감각의 드론 문제 6개를 붙였다.
> 화이트보드에서 "이거 그대로 나온다" 급의 실전 세트.

---

## 왜 이 토픽이 Anduril 펌웨어 인터뷰의 핵심인가

- `my_atoi` / `reverseBits` 는 **실제 리크루터 스크린에서 재현된 문제**다. 파싱 견고성
  (오버플로/NULL/부호)과 비트 조작(마스크·시프트) 두 축을 한 번에 본다.
- 나머지 6개는 드론 펌웨어의 "매일 쓰는" 패턴: 레지스터 RMW, UART 링버퍼,
  CRC 프레임 검증, IMU 센서 퓨전, 스위치 디바운스, 액추에이터 출력 매핑.
- 공통 관심사: **UB 회피**(시프트폭·부호 오버플로·NULL), **정수 안전**, **volatile/ISR
  공유**, **경계·포화 처리**. 인터뷰어가 진짜 보는 것은 결과가 아니라 이 감각이다.

---

## 문제별 핵심

### Q1. `my_atoi` — 오버플로 클램핑 파서
순서: **공백 스킵 → 부호 1개 → 숫자 변환(첫 비숫자에서 정지) → 오버플로 클램핑**.
```c
if (result > INT_MAX/10 || (result == INT_MAX/10 && digit > INT_MAX%10))
    return (sign == 1) ? INT_MAX : INT_MIN;   // 곱하기 *전에* 선검사
```
- **핵심 트릭**: `result*10 + digit` 를 실제로 하기 전에 오버플로를 판정한다.
  `result > INT_MAX/10` 이거나, 같을 때 마지막 자리 `digit > INT_MAX%10 (=7)`.
- `INT_MIN` 도 이 하나의 검사로 커버된다 — 음수 크기는 `INT_MAX+1` 이지만
  부호를 마지막에 곱하므로 크기 기준으로 클램핑하면 `-2147483648` 이 정확히 나온다.
- **함정**: `isspace(str[i])` 에 음수 `char` 를 넘기면 UB → `isspace((unsigned char)str[i])`.
- **함정**: `"+-2"` 는 부호 뒤 비숫자라 0. NULL/빈문자열/숫자없음 전부 0.

### Q2. `reverseBits` — 기본 루프 O(32)
```c
reversed = (reversed << 1) | (n & 1u);  n >>= 1;   // 32회
```
LSB 를 하나씩 뽑아 `reversed` 위쪽으로 밀어넣는다. `1u` (unsigned) 필수.

### Q3. `reverseBitsOptimized` — 분할정복 O(log n)
"인접 블록 스왑"을 블록 크기 **16→8→4→2→1** 로 반씩 줄이며 5단계.
```c
n = (n >> 16) | (n << 16);                                // 16비트
n = ((n & 0xFF00FF00u)>>8)  | ((n & 0x00FF00FFu)<<8);     // 8비트(바이트)
n = ((n & 0xF0F0F0F0u)>>4)  | ((n & 0x0F0F0F0Fu)<<4);     // 4비트(니블)
n = ((n & 0xCCCCCCCCu)>>2)  | ((n & 0x33333333u)<<2);     // 2비트
n = ((n & 0xAAAAAAAAu)>>1)  | ((n & 0x55555555u)<<1);     // 1비트
```
- **마스크 읽는 법**: `0xAAAA...` = 홀수 위치 비트, `0x5555...` = 짝수 위치 비트.
  각 단계는 좌/우 절반을 골라 서로 반대로 시프트해 자리를 교환한다.
- 루프·분기 없음 → 파이프라인 친화적. 인터뷰 팔로업: "왜 5단계?" → `log2(32)=5`.

### Q4. `reg_set_field` — 레지스터 read-modify-write
```c
new = (reg & ~mask) | ((value << shift) & mask);
```
- 다른 필드를 **건드리지 않고** 한 필드만 교체하는 임베디드 정석. I2C 디바이스
  설정 레지스터, MMIO 주변장치 제어의 기본.
- `& mask` 로 value 의 넘치는 비트를 절삭 → 이웃 필드 오염 방지.
- `shift >= 32` 는 시프트폭 UB → 가드. 실무에선 `volatile` 레지스터를 읽고 쓴다.

### Q5. UART 링버퍼 — SPSC (ISR 생산 / main 소비)
- **UART RX ISR** 가 `put`, **main loop** 가 `get`. head 는 생산자만, tail 은
  소비자만 쓴다 → 단일코어 베어메탈에선 락 없이 안전.
- power-of-two 크기 + `& (SIZE-1)` 마스킹, `head==tail` = empty, 한 칸 희생.
- **순서 규칙**: 데이터 먼저 쓰고 인덱스 나중에 공개. 멀티코어면 acquire/release
  원자연산 필요(→ 05번 토픽 SPSC 참조), 단일코어면 `volatile` + 순서로 충분.

### Q6. CRC-8 + 프레임 검증 (MAVLink 풍)
```c
crc ^= byte; for(8) crc = (crc & 0x80) ? (crc<<1)^0x07 : (crc<<1);
```
- 프레임: `[START 0xFE][len][payload...][crc]`. crc 는 **START 제외** `{len+payload}`.
- 검증 3종: 시작 바이트, 길이 필드 일관성(`len+3==total`), 후미 CRC 일치.
- 알려진 벡터: `CRC-8/SMBUS("123456789") = 0xF4` — 구현 검산에 쓴다.
- **함정**: `crc << 1` 은 `int` 승격되므로 `(uint8_t)` 캐스트로 잘라야 8비트 유지.

### Q7. IMU 상보 필터 (complementary filter)
```c
angle = alpha*(prev + gyro_rate*dt) + (1 - alpha)*accel_angle;
```
- 자이로 적분: 단기 정확·장기 드리프트. 가속도계: 장기 정확·단기 노이즈.
  둘을 1차 필터로 섞는다. `alpha ≈ 0.98` (자이로에 무게, 저주파는 accel 보정).
- `alpha=1` → 순수 gyro, `alpha=0` → 순수 accel. 칼만필터의 값싼 대체제로
  드론 자세 추정(pitch/roll)에 광범위하게 쓴다.
- 팔로업: "고정소수점으로 바꾸면?" → `alpha` 를 Q15 정수로, 곱셈 후 `>>15`.

### Q8. GPIO 디바운스 — 시프트 레지스터
```c
history = (history<<1) | raw;
if (history==0xFF) state=true; else if (history==0x00) state=false; // else hold
```
- 기계식 스위치 채터(bounce)를 8샘플 히스토리로 제거. 연속 8회 동일해야 확정.
- 카운터/적분기 방식보다 코드가 짧고 노이즈 내성 직관적. 폴링 tick 마다 호출.
- 팔로업: "샘플 주기 vs 바운스 시간?" → 8*Ts 가 바운스 최대 지속(~수 ms)보다 커야.

### Q9. `stick_to_pwm` — 포화 매핑
```c
if (stick<-1000) stick=-1000; if (stick>1000) stick=1000;
return 1500 + stick/2;   // [-1000,1000] -> [1000,2000] us
```
- RC 스틱 → ESC/서보 PWM 펄스폭. 중립 1500us, 범위 밖은 **포화(clamp)**.
- **먼저 클램프 후 스케일** — 순서가 뒤바뀌면 오버플로/래핑 위험.
- 팔로업: "데드밴드는?" → 중립 근처 `|stick|<D` 를 0 으로. "역방향 채널?" → 부호 반전.

---

## 인터뷰 팔로업 (follow-ups)

- **"`my_atoi` 오버플로를 곱셈 후에 검사하면?"** → 이미 UB 발생(signed overflow). 반드시 사전 검사.
- **"`reverseBits` 를 8비트 룩업 테이블로?"** → 256엔트리 테이블 4번 조합, 초고속. 트레이드오프는 메모리.
- **"레지스터 RMW 가 ISR 과 경합하면?"** → read-modify-write 는 원자적이지 않다. BSRR 같은 set/clear 전용 레지스터 또는 임계구역.
- **"링버퍼 full/empty 를 count 로?"** → count 를 양쪽이 쓰면 경합. SPSC 는 인덱스만으로 판별(한 칸 희생).
- **"CRC 대신 체크섬?"** → 체크섬은 비트 교환/버스트 에러 취약. CRC 가 검출력 우수.
- **"상보필터 vs 칼만?"** → 칼만이 최적이지만 튜닝·연산 비쌈. 상보필터는 1곱셈으로 충분히 좋음.

## 흔한 버그 (gotchas)

- `isspace`/`isdigit` 에 음수 char → `(unsigned char)` 캐스트 누락.
- `1 << 31`, `crc << 1` 부호/승격 문제 → `1u`, `(uint8_t)` 캐스트.
- 시프트폭 `>= 32` UB (Q3/Q4).
- 링버퍼 인덱스 공개 순서 뒤집기 → 소비자가 미완성 데이터를 봄.
- PWM 매핑에서 클램프 전 스케일 → 정수 오버플로.
- 디바운스 히스토리 폭과 샘플 주기 미스매치 → 여전히 채터.
