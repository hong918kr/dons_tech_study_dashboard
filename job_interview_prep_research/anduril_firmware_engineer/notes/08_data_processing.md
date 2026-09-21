# 08. Data Processing & Protocols (데이터 처리 & 프로토콜) · Q86-95 📡

> 링크 위로 **바이트를 안전하게 주고받는** 드릴. 텔레메트리 무결성(checksum/CRC),
> 프레이밍/파싱, 제어루프 수치처리(fixed-point, moving-average), 대역폭 절약
> (RLE/base64), 바이트 스트림 패턴 탐색(KMP). Anduril 펌웨어 인터뷰의
> "RF/시리얼 링크로 프레임을 받는데 깨진 걸 어떻게 걸러내나?" 유형.

---

## 왜 이 토픽이 인터뷰에 나오나

- 드론/자율체는 **노이즈 있는 RF·시리얼 링크**로 텔레메트리와 명령을 주고받는다.
  비트 하나가 뒤집혀도 잘못된 명령이 실행되면 안 되므로 **checksum/CRC 무결성**이 필수다.
- MCU엔 **FPU가 없거나 느리다**. PID 게인·필터 계수는 **고정소수점(Q 포맷)**으로 돌린다.
  이동평균(SMA)은 센서(IMU/기압계) 노이즈를 죽이는 가장 값싼 필터다.
- 대역폭이 좁으니 **RLE/base64** 같은 인코딩으로 페이로드를 줄이거나 텍스트 채널에 싣는다.
- 프레이밍(start/end 마커, length, CRC)과 **파서**(NMEA GPS 등)는 실무 그 자체다.
  인터뷰어는 여기서 **엔디안, 오버플로, 버퍼 경계, 부분 프레임(partial frame)**을 파고든다.

---

## 핵심 개념 치트시트

| 개념 | 요점 |
|---|---|
| **XOR checksum** | 가장 싼 검사. 비트 오류 검출력 약함(짝수개 오류/자리바뀜 못 잡음). NMEA가 사용. |
| **additive sum8** | 바이트 합의 하위 8비트. XOR보다 낫지만 여전히 약함. wrap-around는 `uint8_t`가 알아서. |
| **CRC-16** | 다항식 나눗셈. **LUT**로 바이트당 1회 조회 → 빠름. 오류 검출력 월등. init/reflect/poly로 변종 다수. |
| **LUT 생성** | `table[i]` = `(i<<8)`를 8번 나눈 잔여(MSB-first). 실무는 **const로 미리 구워** 플래시에 둔다. |
| **framing** | `[start][len][payload][CRC][end]`. 마커·길이·체크섬으로 프레임 경계와 무결성 확인. |
| **Q 포맷** | `값 = raw / 2^q`. 덧셈은 그대로, 곱셈은 `>>q`로 스케일 복원(중간연산 int64). |
| **SMA** | 창 W의 단순이동평균. 지연(lag) ≈ W/2. 스트리밍이면 running sum으로 O(1) 갱신. |
| **RLE** | `[value][count]`. count 8비트면 255에서 분할. 최악의 경우 **2배로 커짐**(무런 데이터). |
| **base64** | 3바이트→4문자(6비트씩). `=` 패딩. 크기 **+33%**. 텍스트 채널 전송용. |
| **KMP** | LPS(prefix-suffix) 테이블로 실패 시 되돌아갈 위치를 미리 계산 → O(n+m). |

---

## 문제별 요점 & 함정

### 86. NMEA 파싱 (nmea_validate / nmea_get_field)
- NMEA-0183: `$GPGGA,123519,4807.038,N,...*47`. 체크섬 = `$`와 `*` 사이 **XOR**, 뒤에 2자리 hex.
- validate: `$`로 시작 → `*` 찾기 → 사이 XOR → 뒤 2 hex와 비교. **널 종료 넘어 읽지 않도록** short-circuit.
- get_field: 콤마로 분리, field 0 = talker+type(`GPGGA`). `*`/CRLF/`\0`에서 종료.
- 함정: 종료자 뒤 hex 두 자리 접근 시 OOB. `hexval('\0')`이 -1을 반환하도록 해 방어.

### 87-88. XOR / sum8 체크섬
- 둘 다 O(n), 상태 1바이트. `NULL` 안전. `uint8_t` 누산이 자연 mod-256.
- 함정: **검출력**을 물어보면 "자리바뀜(transposition)·짝수개 비트오류 취약 → 실무는 CRC" 라고 답.

### 89. CRC-16 (LUT)
- CRC-16/XMODEM(poly `0x1021`, init `0x0000`, MSB-first, 무반사)이 대표적. 표준 검증: `"123456789"`→`0x31C3`.
- LUT 생성:
  ```c
  for (i in 0..255) { crc = i<<8; repeat 8: crc = (crc&0x8000)?(crc<<1)^0x1021:(crc<<1); table[i]=crc; }
  ```
- 갱신: `crc = (crc<<8) ^ table[((crc>>8) ^ byte) & 0xFF];`
- 함정: LSB-first(reflected) 변종(MODBUS 등)은 테이블 생성·갱신식이 다르다. **어느 CRC인지 먼저 확정**.

### 90. 바이너리 패킷 파싱
- 여기선 최소판: `start=0xAA`, `end=0xBB`, `len>=2`. 실전은 **length + CRC(#89)**를 더한다.
- 함정: **partial frame**(스트림 중간부터 도착)·마커가 페이로드에 우연히 나타남 → byte stuffing/escape 필요.

### 91. 고정소수점 (Q 포맷)
- `값 = raw / 2^q`. **덧셈**: 같은 Q라 raw끼리 더함(오버플로 방지 int64 + 포화).
- **곱셈**: `(int64)a*b`는 Q(2q) → `>>q`로 복원. 반올림은 `+ 2^(q-1)` 후 시프트.
- 함정: 곱에서 int32끼리 곱하면 오버플로 → **반드시 int64 중간연산**. 음수 산술 시프트는 구현 정의(실무 컴파일러는 산술 시프트).

### 92. 이동평균 (SMA)
- `out[i] = mean(in[i-W+1..i])`, 초반부는 있는 만큼. 지연 ≈ W/2.
- 스트리밍이면 `sum += new - old`로 **O(1)** 갱신(원형버퍼로 old 관리). 여기선 명확성 위해 O(n·W).
- 함정: `size_t` 언더플로(`i-W+1`) → `i+1>=W` 가드. 정수 평균이면 반올림/버림 정책 명시.

### 93. RLE
- `[value][count]` 쌍. count 8비트 → **255에서 분할**. 반환=쓴 바이트 수, 버퍼 부족/홀수 길이면 0.
- 함정: **최악의 경우 팽창**(교대 데이터는 2배). 실무는 escape/RLE 플래그로 팽창 방지.

### 94. base64
- 3바이트(24비트)→4문자(6비트씩), 부족분 `=` 패딩. 크기 +33%. 디코드 표로 역변환.
- 함정: 입력 길이 4의 배수 아님/불량 문자 → 실패 반환. 패딩 개수로 출력 길이 계산.

### 95. 패턴 탐색 (KMP)
- LPS 테이블로 불일치 시 되돌아갈 위치를 미리 계산 → **O(n+m)**, 백트래킹 없음.
- 빈 패턴은 0, 패턴>데이터는 -1. LPS는 **고정 버퍼**(bounded)로 힙 회피, 초과 시 naive fallback.
- 함정: naive는 `0xAAAA...`처럼 반복 접두사에서 O(n·m). KMP의 이득이 여기서 나온다.

---

## 인터뷰 팔로업 (자주 나오는 꼬리질문)

1. **"XOR/sum 체크섬은 뭘 못 잡나?"** → 자리바뀜, 짝수개 비트오류. 그래서 CRC.
2. **"CRC 테이블을 런타임에 만들까 굽어둘까?"** → 플래시에 `const`로 미리 구워 부팅·연산 비용 제거.
3. **"엔디안은?"** → 멀티바이트 필드(길이/CRC)는 프로토콜이 정한 바이트 순서로 직렬화. `htons`류/수동 시프트.
4. **"partial frame/재동기화는?"** → 링버퍼에 모으고 start 마커로 재동기, length로 끝 확인, CRC로 검증.
5. **"float 대신 fixed 왜?"** → FPU 없음/결정성/속도. 단 오버플로·정밀도 관리 부담.
6. **"SMA vs EMA?"** → EMA(지수이동평균)는 상태 1개·곱셈 2번으로 O(1), 버퍼 불필요. SMA는 창 안 모든 샘플 동일가중.
7. **"RLE가 언제 나쁜가?"** → 무런(노이즈) 데이터에서 2배 팽창. 헤더로 raw/compressed 선택.

---

## 빌드 & 실행

```sh
# 연습(직접 채우기)
cc -std=c11 -Wall -Wextra problems/08_data_processing.c -o /tmp/andb_data_processing && /tmp/andb_data_processing
# 정답 확인 (전부 PASS)
cc -std=c11 -Wall -Wextra solutions/08_data_processing.c -o /tmp/andb_data_processing && /tmp/andb_data_processing
# 또는
make prob N=08_data_processing      #  연습
make sol  N=08_data_processing      #  정답
```

> **핵심 한 줄**: "링크는 언젠가 비트를 뒤집는다." 그래서 **체크섬/CRC로 걸러내고,
> 프레이밍으로 경계를 잡고, FPU 없이 fixed-point로 계산하고, 좁은 대역폭엔 인코딩을 쓴다."
