# Q7. 센서 데이터 품질 검증 툴 — 레이트·지터·드롭·타임스탬프를 숫자로 재고, 결함을 주입해 검증하고, 헬스 모니터로 지킨다

> **이 노트를 다 읽으면**: 센서 플랫폼에서 "데이터 품질"이 뜻하는 결함 13가지를 이름·검출 방법·원인으로 말할 수 있다 · period 히스토그램, p50/p99/max, rate error(ppm), jitter, 손실(타임스탬프 기준 vs 순번 기준), 지연, Allan deviation을 공식으로 정의하고 고정 메모리로 계산하는 코드를 짤 수 있다 · IIO·V4L2·SocketCAN 어댑터를 가진 재사용 가능한 품질 툴을 만들고, 일부러 결함을 넣어 툴이 정말 잡는지 증명할 수 있다 · systemd `WatchdogSec=`와 히스테리시스 상태 기계로 헬스 모니터를 서비스로 돌리고, 같은 리포트를 CI 회귀 게이트로 쓸 수 있다
> **왜 필요한가**: Scout AI 같은 로보틱스·국방 회사 JD에 "build tools for data quality / sensor validation"이 따로 적혀 있다. 면접에서는 "IMU가 1 kHz라고 하는데 정말 1 kHz인지 어떻게 증명하나", "프레임이 빠지는지 어떻게 아나", "필드에서 센서가 조용히 죽으면 누가 알아채나"를 묻는다.
> **Don 기준 난이도**: 링버퍼·카운터·타임스탬프·워치독·생산 테스트 판정 기준은 SSD 펌웨어에서 매일 하던 일 / 그걸 Linux의 세 가지 서로 다른 ABI(IIO 버퍼, V4L2 `v4l2_buffer`, CAN 소켓 + cmsg)에서 꺼내는 법, 커널이 어디서 조용히 데이터를 버리는지, systemd와 엮는 법이 새로 배울 부분
> **선행 노트**: A4(poll/epoll), A6(signals), I2(IIO — buffered capture, `i2rate` 측정), I5(V4L2 — vb2 큐), I10(CAN 드라이버), L1(systemd — sd_notify·`WatchdogSec=`), M9(유저 공간 디버깅), N3(지연 측정), Q2(카메라 파이프라인), Q3(멀티 센서 동기화 — PTP·PPS), Q4(CAN 실무), Q5(IMU·GNSS·LiDAR 수신), Q6(ROS 2), P6(테스트와 CI)

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어를 양산할 때 Don은 "기능이 동작한다"와 "규격대로 동작한다"를 늘 구분했을 것이다. 읽기 명령이 성공하는 것과, p99.99 latency가 규격 안에 드는 것은 다른 문제다. 그래서 펌웨어 안에 카운터와 히스토그램을 심고, 생산 라인과 CI에서 그 숫자를 판정했다.

로봇·드론·차량의 센서 키트도 똑같다. IMU 드라이버가 `/dev/iio:device0`에서 값을 내주는 것은 "기능"이다. 그런데 센서 퓨전(EKF)이나 SLAM이 정말 필요로 하는 것은 이런 성질이다.

- 샘플이 **명목 주기대로** 온다 (200 Hz라면 5.000 ms마다, 오차 몇백 ppm 이내).
- 샘플이 **빠지지 않는다**. 빠지면 빠졌다는 사실을 안다.
- 타임스탬프가 **단조 증가**하고, **모든 센서가 같은 시계**를 쓴다.
- 값이 **물리적으로 말이 된다** (NaN 없음, 범위 안, 같은 값에 얼어붙지 않음).
- 이 성질이 **필드에서 깨지면 누군가 알아챈다** (헬스 모니터, 경보, 로그).

이걸 사람 눈으로 `candump`를 보며 확인할 수는 없다. 그래서 **데이터 품질 툴**을 만든다. 이 노트는 그 툴을 처음부터 짓는다: 정의 → 공식 → 설계 → 세 가지 실제 입력(IIO, V4L2, SocketCAN) → 결함 주입으로 검증 → systemd 헬스 모니터 → CI 게이트.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" font-size="14" text-anchor="middle">센서 데이터가 지나가는 길과, 품질이 깨지는 지점</text>
  <rect x="10" y="40" width="96" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="58" y="67" font-size="13" text-anchor="middle">Sensor</text>
  <rect x="122" y="40" width="96" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="67" font-size="13" text-anchor="middle">Bus / IRQ</text>
  <rect x="234" y="40" width="96" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="282" y="67" font-size="13" text-anchor="middle">Driver</text>
  <rect x="346" y="40" width="96" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="394" y="67" font-size="12" text-anchor="middle">Kernel buffer</text>
  <rect x="458" y="40" width="96" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="506" y="67" font-size="13" text-anchor="middle">Reader</text>
  <rect x="570" y="40" width="100" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="620" y="67" font-size="13" text-anchor="middle">Fusion / log</text>
  <line x1="106" y1="62" x2="120" y2="62" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="218" y1="62" x2="232" y2="62" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="330" y1="62" x2="344" y2="62" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="442" y1="62" x2="456" y2="62" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="554" y1="62" x2="568" y2="62" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="14" y="110" font-size="12">ODR 오차(ppm)</text>
  <text x="14" y="128" font-size="12">포화·stuck</text>
  <text x="14" y="146" font-size="12">NaN·범위 밖</text>
  <text x="126" y="110" font-size="12">IRQ 지연</text>
  <text x="126" y="128" font-size="12">버스 에러</text>
  <text x="126" y="146" font-size="12">FIFO 넘침</text>
  <text x="238" y="110" font-size="12">시각을 찍는</text>
  <text x="238" y="128" font-size="12">위치·시계</text>
  <text x="238" y="146" font-size="12">순번 없음</text>
  <text x="350" y="110" font-size="12">가득 차면</text>
  <text x="350" y="128" font-size="12">조용히 drop</text>
  <text x="462" y="110" font-size="12">스케줄 지연</text>
  <text x="462" y="128" font-size="12">느린 처리</text>
  <text x="462" y="146" font-size="12">버퍼 부족</text>
  <text x="574" y="110" font-size="12">시계 도메인</text>
  <text x="574" y="128" font-size="12">센서 간 sync</text>
  <text x="574" y="146" font-size="12">순서 뒤바뀜</text>
  <rect x="10" y="176" width="660" height="110" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="198" font-size="13" text-anchor="middle">품질 툴이 보는 신호는 결국 세 가지다</text>
  <text x="24" y="224" font-size="13">1. 타임스탬프 t_k — 주기, 지터, gap, 단조성, 시계 도메인</text>
  <text x="24" y="246" font-size="13">2. 순번 seq_k (있으면) — 손실, 중복, 순서 뒤바뀜, wrap</text>
  <text x="24" y="268" font-size="13">3. 값 v_k 와 도착 시각 a_k — NaN, 범위, stuck, 포화, 지연 (a_k - t_k)</text>
</svg>
```

그림 1 — 센서에서 소비자까지 각 단계에서 생기는 품질 결함. 툴은 단계마다 따로 계측하지 않고, 끝에서 보이는 세 신호(타임스탬프·순번·값)로 거꾸로 추론한다.

bare-metal 비유로 보면, 이 툴은 **SSD 펌웨어의 "latency histogram + error counter + assert"를 Linux 유저 공간 데몬으로 옮긴 것**이다. 다른 점은 데이터가 커널을 거쳐 온다는 것이다. 커널은 버퍼가 가득 차면 대부분 **에러 없이 버린다**(IIO kfifo, V4L2 vb2 큐, 소켓 수신 버퍼 모두). 그래서 툴은 "에러가 안 났다"를 믿지 않고 **타임스탬프와 순번으로 손실을 증명**해야 한다.

### 0.1 이 노트의 실습 환경

- 툴 단위 테스트(2절)는 Lima VM `elinux` 호스트에서 돌렸다: `uname -r` = `7.0.0-38-generic`(aarch64), Python 3.14.4 + numpy 2.5.4(내 디렉터리 `/var/tmp/Q7/venv`에 pip 설치).
- 실제 장치 실습(5~9절)은 공용 랩의 **도메인 커널** `/var/lab/out/Image-dom`(Linux `6.18.55-dom`)과 **도메인 rootfs** `rootfs-dom.ext4`(Ubuntu 24.04 + systemd 255, Python 3.12.3, numpy 1.26.4, stress-ng 0.17.06, can-utils, v4l-utils)를 QEMU `virt`(cortex-a57, `-smp 2`, 1536 MB, **KVM 없는 TCG**)로 부팅해서 했다. rootfs는 복사하지 않고 qcow2 overlay를 썼다.
- 측정 동안 같은 VM에서 다른 노트 작성자 18명이 각자 QEMU를 돌렸다. **모든 µs·ms 절대값은 실제 i.MX8·Jetson 숫자가 아니다.** 믿을 것은 (1) 결함을 넣었을 때 툴이 **무엇을, 몇 개** 잡는가, (2) 깨끗한 경우와 결함 경우의 **상대 차이**다.

---

## 1. "데이터 품질"이란 무엇인가 — 결함 분류표

먼저 이름을 정한다. 면접에서 "데이터 품질"이라고 뭉뚱그리면 약해 보이고, 아래처럼 **결함 하나하나를 검출 신호와 연결**해서 말하면 강해 보인다.

| # | 결함 | 정의 | 검출 신호 | 전형적인 원인 |
|---|---|---|---|---|
| 1 | rate 오차 | 실제 평균 샘플률이 명목값과 다름 | `(N-1)/(t_N - t_1)` vs 명목, ppm | 센서 발진기 오차, ODR 설정 실수, 트리거 주기 변경 |
| 2 | jitter | 샘플 간격 dt가 흔들림 | dt 분포의 sd, `abs(dt - T)`의 p99 | 타임스탬프를 늦게 찍음(스레드), 부하, IRQ 지연 |
| 3 | drop / gap | 샘플이 빠짐 | dt > 1.5T, 또는 seq 차이 > 1 | 버퍼 넘침, 느린 소비자, 버스 에러 |
| 4 | duplicate | 같은 샘플이 두 번 | seq 차이 = 0, dt = 0 | 재전송, 드라이버 버그(같은 FIFO 재읽기) |
| 5 | out-of-order | 순서가 뒤바뀜 | seq 차이 < 0 | 멀티 큐·멀티 스레드 전달, 같은 ID 송신자 둘 |
| 6 | 비단조 타임스탬프 | 시각이 뒤로 감 | dt < 0 | `CLOCK_REALTIME` 사용 중 NTP·`date` 스텝 |
| 7 | 시계 도메인 오류 | 시각이 다른 시계 기준 | 도착 시각과의 차가 비정상(초 단위) | 센서마다 다른 시계, epoch 다름, wrap |
| 8 | latency | 샘플 시각 → 소비 시각 지연 | `a_k - t_k` 분포 | 버퍼 watermark, 배치 읽기, 스케줄 |
| 9 | stuck value | 값이 같은 값에 얼어붙음 | 같은 값 연속 길이 ≥ N | 센서 리셋 후 미초기화, 드라이버가 캐시 값 반환 |
| 10 | saturation | 값이 풀스케일에 붙음 | 값 = min/max 코드 연속 | 범위 설정 잘못(±2 g에 4 g 충격) |
| 11 | NaN / 범위 밖 | 값이 물리적으로 불가능 | `isnan`, 범위 검사 | 변환 버그, 0으로 나눔, 비트 오류 |
| 12 | 센서 간 불일치 | 두 센서가 서로 모순 | 잔차(예: 휠 속도 vs IMU 적분) | 장착 오류, 한쪽 고장, 축 뒤바뀜 |
| 13 | sync 오차 | 센서 사이 시각 차가 큼 | 같은 사건의 두 타임스탬프 차 | 공통 시계 없음(Q3: PTP·PPS), 트리거 배선 |

표에서 볼 것:

- 1~8은 **시간 축** 결함, 9~11은 **값 축** 결함, 12~13은 **센서 사이** 결함이다. 이 노트의 툴은 1~11을 자동으로 잡고, 12~13은 10절에서 방법만 다룬다(동기화 자체는 Q3).
- **손실은 두 가지 방법으로 센다**. 순번(seq)이 있으면 정확하고, 없으면 타임스탬프 gap으로 추정한다. 둘 다 있으면 서로 교차 검증한다(7절에서 CAN으로 해 본다).
- "센서가 순번을 주는가"는 입력마다 다르다. V4L2는 `v4l2_buffer.sequence`가 있고, IIO는 표준 순번 채널이 없고(타임스탬프만), CAN은 프로토콜 설계에 달렸다(보통 payload에 rolling counter를 넣는다 — Q4, O3).

---

## 2. 지표 정의와 공식

### 2.1 직관 — 시간 축 위의 점들

센서 스트림 하나는 결국 `(t_k, seq_k, v_k, a_k)`의 나열이다. `t_k`는 샘플 시각(센서·드라이버가 찍은 것), `a_k`는 내 프로그램이 받은 시각(arrival), `seq_k`는 순번, `v_k`는 값이다. 모든 시간 지표는 **이웃한 두 점의 차 `dt_k = t_k - t_(k-1)`**에서 나온다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="340" y="18" font-size="14" text-anchor="middle">타임스탬프 열에서 보이는 결함 (T = 명목 주기)</text>
  <line x1="20" y1="110" x2="665" y2="110" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="650" y="130" font-size="12">t</text>
  <line x1="40" y1="104" x2="40" y2="116" stroke="#888"/>
  <line x1="110" y1="104" x2="110" y2="116" stroke="#888"/>
  <line x1="180" y1="104" x2="180" y2="116" stroke="#888"/>
  <line x1="250" y1="104" x2="250" y2="116" stroke="#888"/>
  <line x1="320" y1="104" x2="320" y2="116" stroke="#888"/>
  <line x1="390" y1="104" x2="390" y2="116" stroke="#888"/>
  <line x1="460" y1="104" x2="460" y2="116" stroke="#888"/>
  <line x1="530" y1="104" x2="530" y2="116" stroke="#888"/>
  <line x1="600" y1="104" x2="600" y2="116" stroke="#888"/>
  <circle cx="40" cy="110" r="6" fill="#4a7bd0"/>
  <circle cx="114" cy="110" r="6" fill="#4a7bd0"/>
  <circle cx="176" cy="110" r="6" fill="#4a7bd0"/>
  <circle cx="320" cy="110" r="6" fill="#4a7bd0"/>
  <circle cx="390" cy="110" r="6" fill="#4a7bd0"/>
  <circle cx="390" cy="92" r="6" fill="#e08a3c"/>
  <circle cx="530" cy="110" r="6" fill="#4a7bd0"/>
  <circle cx="470" cy="128" r="6" fill="#d0564a"/>
  <text x="40" y="146" font-size="12" text-anchor="middle">seq 0</text>
  <text x="114" y="146" font-size="12" text-anchor="middle">1</text>
  <text x="176" y="146" font-size="12" text-anchor="middle">2</text>
  <text x="320" y="146" font-size="12" text-anchor="middle">5</text>
  <text x="390" y="146" font-size="12" text-anchor="middle">6</text>
  <text x="470" y="150" font-size="12" text-anchor="middle">8 (t 뒤로)</text>
  <text x="540" y="146" font-size="12" text-anchor="middle">7</text>
  <text x="77" y="96" font-size="12" text-anchor="middle">dt=T+e</text>
  <text x="145" y="96" font-size="12" text-anchor="middle">T-e</text>
  <rect x="186" y="62" width="126" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="249" y="80" font-size="12" text-anchor="middle">gap: dt=3T, 손실 2</text>
  <text x="390" y="72" font-size="12" text-anchor="middle">중복 seq 6</text>
  <text x="560" y="96" font-size="12" text-anchor="middle">순서 뒤바뀜</text>
  <line x1="120" y1="186" x2="120" y2="214" stroke="#3f9a6b"/>
  <line x1="120" y1="200" x2="220" y2="200" stroke="#3f9a6b" marker-end="url(#ah2)"/>
  <line x1="220" y1="186" x2="220" y2="214" stroke="#3f9a6b"/>
  <text x="240" y="204" font-size="12">latency = a_k - t_k (읽은 시각 - 샘플 시각)</text>
  <text x="20" y="245" font-size="12">회색 눈금 = 명목 격자 k·T, 파란 점 = 정상 샘플, 주황 = 중복, 빨강 = 비단조·순서 오류</text>
</svg>
```

그림 2 — 한 줄의 타임스탬프에서 jitter(격자에서의 흔들림), gap(빈 격자), 중복, 비단조, 지연을 읽는 법.

### 2.2 정의 — 공식 모음

명목 주기를 `T = 1 / f_nom`이라 하자.

| 지표 | 공식 | 단위·해석 |
|---|---|---|
| 측정 rate | `f_meas = (N - 1) / (t_N - t_1)` | Hz. 끝점 두 개만 쓰므로 jitter에 둔감하다 |
| rate 오차 | `(f_meas / f_nom - 1) × 10^6` | ppm. 크리스털 ±50 ppm, RC 발진기 ±1~2 % 수준 |
| period | `dt_k = t_k - t_(k-1)` | 분포를 히스토그램으로 본다 |
| jitter | `J_k = abs(dt_k - T)` (gap 제외) | p50, p99, p99.9, max로 요약 |
| dt 평균·표준편차 | Welford 누적: `mean`, `M2 / n` | sd는 gap이 섞이면 망가지므로 percentile을 같이 본다 |
| gap | `dt_k > 1.5 T` | 개수 |
| 손실(시각 기준) | gap마다 `round(dt_k / T) - 1`의 합 | 순번이 없을 때의 추정치 |
| 손실(순번 기준) | `d = seq_k - seq_(k-1)`, `d > 1`이면 `d - 1`의 합 | 정확. wrap은 `mod 2^n`으로 접는다 |
| 중복 / 순서 뒤바뀜 | `d = 0` / `d < 0` | 개수 |
| 비단조 | `dt_k < 0` | 개수. 0이 아니면 시계 선택부터 의심 |
| latency | `L_k = a_k - t_k` | 같은 시계 도메인일 때만 의미가 있다 |
| 시계 도메인 오류 | `abs(L_k) > 1 s` | 개수. 다른 시계·epoch를 섞었다는 신호 |
| stuck | 같은 값 연속 길이 ≥ `N_stuck` | 연속 길이 최댓값과 해당 샘플 수 |

### 2.3 왜 평균이 아니라 percentile인가

SSD에서 "평균 latency 80 µs"가 아무 의미 없고 "p99.99 latency 2 ms"가 고객 계약인 것과 똑같다. 센서 퓨전은 **가장 나쁜 간격**에서 깨진다. 예를 들어 200 Hz IMU에서 1초에 한 번 20 ms gap이 생기면 평균 dt는 5.075 ms로 1.5 %만 늘지만, EKF의 예측 단계는 그 순간 4배 긴 적분을 하게 된다. 그래서 리포트에는 **p50(전형), p99(꼬리), max(최악)**을 같이 싣고, gap은 따로 센다.

### 2.4 고정 메모리 — 히스토그램으로 percentile 구하기

모든 dt를 리스트에 쌓고 정렬하면 정확한 percentile이 나오지만, 24시간 1 kHz 스트림이면 8,640만 개다. 장비 위 데몬은 **메모리가 샘플 수와 무관**해야 한다. 그래서 **로그 간격 히스토그램**을 쓴다.

- 경계: 0, 그리고 1 µs ~ 10 s를 decade당 50칸(칸 폭 약 4.7 %). 배열 하나가 353칸(경계 352개 + 넘침 칸) × 8 B = 2,824 B.
- percentile은 누적 개수가 `p × N`을 넘는 칸의 **상단 경계**로 답한다. 오차는 최대 칸 폭(4.7 %)이다.
- dt 히스토그램은 분포 모양(그림 4, 5)에 쓰고, jitter는 `abs(dt - T)`를 **따로** 히스토그램에 넣는다. dt 히스토그램 하나로 jitter p99를 읽으면 10 ms 근처의 칸 폭(0.47 ms)보다 작은 jitter는 안 보이기 때문이다. `abs(dt - T)`의 히스토그램은 50 µs 근처에서 칸 폭이 2.4 µs다.
- 평균·분산은 **Welford(정확히는 Chan의 chunk 병합 공식)**로 누적한다. `sum(x^2) - n·mean^2` 방식은 큰 값에서 자릿수 손실이 생긴다.

이 선택은 펌웨어의 latency bucket(2의 거듭제곱 버킷)과 같은 생각이다. Prometheus의 histogram, HdrHistogram도 같은 원리다.

### 2.5 Allan deviation — "rate가 시간에 따라 흔들리는가"를 한 줄로

rate 오차(ppm)는 "평균적으로 몇 ppm 빠른가"만 말한다. 센서 시계가 **온도에 따라 천천히 떠도는지**, 아니면 **짧은 jitter만 있는지**는 구분하지 못한다. 이걸 보는 도구가 Allan deviation(ADEV)이다. 원래 원자시계·크리스털 안정도 지표이고, IMU 노이즈 분석(각속도 random walk, bias instability)에서도 같은 공식을 쓴다.

타이밍 오차(위상) `x_k = t_k - t_1 - (k-1)·T_fit` (`T_fit`은 최소제곱 직선의 기울기)에 대해, `m`개 샘플 간격 `tau = m·T`의 overlapping ADEV는 다음과 같다.

```text
sigma_y(tau)^2 = 1 / (2 tau^2 (N - 2m)) × sum_{k} ( x_(k+2m) - 2 x_(k+m) + x_k )^2
```

읽는 법: 순수 white jitter(타임스탬프를 찍는 순간의 흔들림)만 있으면 `sigma_y`는 `tau`가 10배 늘 때 10배 줄어든다(기울기 -1). 기울기가 완만해지거나 다시 올라가면 그 시간 척도에서 **주파수 자체가 떠돈다**는 뜻이다(온도 drift, 트리거 재설정). 이 노트의 툴은 로그를 사후 분석할 때만 ADEV를 계산한다(5.2절). 스트리밍으로 하려면 `m`마다 링버퍼가 필요해서 메모리가 커진다.

### 2.6 예제 1 — 정답을 아는 합성 스트림으로 검출기 자체를 검증

툴을 실제 장치에 붙이기 전에 **검출기가 정확한지** 먼저 증명한다. 결함 위치와 개수를 아는 합성 스트림을 만들어 넣고, 툴이 정확히 그 숫자를 내는지 본다. 이 테스트는 CI에서 그대로 단위 테스트가 된다(9절).

무엇을 확인하는 코드인가: 100 Hz, 50 µs 가우시안 jitter인 1만 샘플에 drop 4개(2곳), 300 ms 뒤로 가는 시계 스텝, 중복 1개, stuck 100 샘플, NaN 1개, 범위 밖 1개를 넣고, 256개씩 chunk로 흘려서 집계한다. 마지막에 200만 샘플을 더 넣어도 메모리가 늘지 않는지 본다.

```python
#!/usr/bin/env python3
# test_synth.py — 정답을 아는 합성 스트림으로 검출기 자체를 검증 (CI 단위 테스트 역할)
import numpy as np, resource
import sdq_stats as S

rng = np.random.default_rng(7)
N, T = 10_000, 10_000_000                                  # 100 Hz, 주기 10 ms [ns]
t = np.arange(N, dtype=np.int64) * T + rng.normal(0, 50_000, N).astype(np.int64)  # 50 us 지터
seq = np.arange(N, dtype=np.int64)
v = rng.normal(0, 1, N)
keep = np.ones(N, bool); keep[[1000, 1001, 1002, 5000]] = False   # 4개 drop (gap 2곳)
t, seq, v = t[keep], seq[keep], v[keep]
t[7000:] -= 30 * T                                         # 7000번째에서 시계가 300 ms 뒤로
seq = np.insert(seq, 8000, seq[7999]); t = np.insert(t, 8000, t[7999])   # 중복 1개
v = np.insert(v, 8000, v[7999])
v[9000:9100] = 3.25                                        # 100 샘플 stuck
v[9500] = np.nan; v[9600] = 99                             # NaN 1, 범위 밖 1

st = S.Stream('synth', 100, lo=-10, hi=10, stuck_n=50)
for i in range(0, len(t), 256):                            # 256개씩 chunk로 흘려 넣기
    st.update(t[i:i + 256], None, seq[i:i + 256], v[i:i + 256])
r = st.summary()
for k in ('n', 'gaps', 'lost_by_ts', 'lost_by_seq', 'dups', 'ts_backwards', 'ts_zero_dt',
          'nan', 'out_of_range', 'stuck_samples', 'stuck_run_max', 'dt_p50_us', 'jit_p50_us', 'jit_p99_us'):
    print('%-14s %s' % (k, r[k]))

# 메모리 상한 확인: 2백만 샘플을 더 넣어도 RSS가 늘지 않아야 한다
rss0 = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
big = S.Stream('big', 1000)
for i in range(2000):
    big.update(np.arange(i * 1000, (i + 1) * 1000, dtype=np.int64) * 1_000_000)
print('2M samples: n=%d maxrss before=%d kB after=%d kB, state arrays=%d B'
      % (big.n, rss0, resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
         big.hist.nbytes + big.lhist.nbytes))
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.elinux_scratch/Q7/sdq && /var/tmp/Q7/venv/bin/python -B test_synth.py'
```

```text
n              9997
gaps           2
lost_by_ts     4
lost_by_seq    4
dups           1
ts_backwards   1
ts_zero_dt     1
nan            1
out_of_range   1
stuck_samples  50
stuck_run_max  99
dt_p50_us      10000.0
jit_p50_us     50.1
jit_p99_us     182.0
2M samples: n=2000000 maxrss before=38208 kB after=38208 kB, state arrays=5648 B
```

출력에서 볼 것:

- 넣은 결함이 정확히 나온다: gap 2곳에 손실 4개(시각 기준과 순번 기준이 일치), 중복 1개(`dups`와 `ts_zero_dt` 둘 다 1), 시계 스텝 1회(`ts_backwards=1`), NaN 1, 범위 밖 1.
- stuck 100 샘플은 "연속 같은 값 비교" 99번(`stuck_run_max=99`)이고, 그중 기준 50을 넘긴 뒤의 50개가 `stuck_samples`로 잡힌다. 경보는 "50 샘플째부터" 울린다는 뜻이다.
- jitter: 타임스탬프에 sd 50 µs 잡음을 넣으면 dt의 sd는 `sqrt(2) × 50 = 70.7 µs`이고, `abs(dt - T)`의 중앙값 이론값은 `0.674 × 70.7 = 47.7 µs`, p99는 `2.576 × 70.7 = 182 µs`다. 툴은 50.1과 182.0을 냈다(칸 상단 경계로 답하므로 약간 크게 나온다).
- 200만 샘플을 더 넣어도 `maxrss`가 그대로다. 상태 배열은 히스토그램 몇 KB뿐이다.

함정: 이 테스트에서 시계 스텝 직후의 dt는 -290 ms다. 음수 dt를 jitter 히스토그램에 넣으면 p99가 망가지고, gap 판정(`dt > 1.5T`)에서는 빠져서 "아무 일 없음"처럼 보인다. 그래서 `dt < 0`은 **반드시 별도 카운터**로 센다.

---

## 3. 툴 설계 — `sdq` (sensor data quality)

### 3.1 구조 — 입력은 다르게, 판정은 하나로

센서마다 툴을 따로 만들면 "IMU 툴은 gap을 1.5T로, 카메라 툴은 2T로" 같은 불일치가 생긴다. 그래서 **어댑터(입력별) → 공통 chunk → 하나의 통계 엔진 → 여러 출력**으로 자른다. 어댑터는 장치에서 읽어 numpy 배열 묶음 `{t, arr, seq, v, kdrop}`을 내놓는 일만 한다. 판정 규칙은 엔진 하나에만 있다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="340" y="18" font-size="14" text-anchor="middle">sdq 구조 — 어댑터 · 통계 엔진 · 출력</text>
  <text x="80" y="42" font-size="12" text-anchor="middle">어댑터 (입력별)</text>
  <rect x="10" y="52" width="140" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="80" y="74" font-size="12" text-anchor="middle">IIO buffer read</text>
  <rect x="10" y="94" width="140" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="80" y="116" font-size="12" text-anchor="middle">V4L2 DQBUF</text>
  <rect x="10" y="136" width="140" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="80" y="158" font-size="12" text-anchor="middle">SocketCAN + cmsg</text>
  <rect x="10" y="178" width="140" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="80" y="200" font-size="12" text-anchor="middle">UDP (Q5, 가이드)</text>
  <rect x="10" y="220" width="140" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="80" y="242" font-size="12" text-anchor="middle">ROS 2 topic (Q6)</text>
  <line x1="150" y1="69" x2="214" y2="140" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="150" y1="111" x2="214" y2="145" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="150" y1="153" x2="214" y2="152" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="150" y1="195" x2="214" y2="158" stroke="#888" marker-end="url(#ah3)"/>
  <line x1="150" y1="237" x2="214" y2="163" stroke="#888" marker-end="url(#ah3)"/>
  <rect x="216" y="100" width="210" height="110" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="321" y="122" font-size="13" text-anchor="middle">Stream 엔진 (고정 메모리)</text>
  <text x="228" y="146" font-size="12">카운터: gap, 손실, 중복, 역행</text>
  <text x="228" y="166" font-size="12">dt·jitter·latency 히스토그램</text>
  <text x="228" y="186" font-size="12">Welford 평균·분산, stuck run</text>
  <text x="321" y="96" font-size="12" text-anchor="middle">chunk = t, arr, seq, v, kdrop</text>
  <line x1="426" y1="130" x2="470" y2="74" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="426" y1="150" x2="470" y2="140" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="426" y1="180" x2="470" y2="214" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="472" y="50" width="198" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="571" y="71" font-size="12" text-anchor="middle">리포트: 텍스트 + JSON</text>
  <text x="571" y="89" font-size="12" text-anchor="middle">→ sdq_check (CI 게이트)</text>
  <rect x="472" y="114" width="198" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="571" y="135" font-size="12" text-anchor="middle">RotLog: binary + rotation</text>
  <text x="571" y="153" font-size="12" text-anchor="middle">→ sdq_offline (ADEV)</text>
  <rect x="472" y="180" width="198" height="70" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="571" y="201" font-size="12" text-anchor="middle">q7_health: 1 s 창 판정</text>
  <text x="571" y="219" font-size="12" text-anchor="middle">OK/DEGRADED/FAULT</text>
  <text x="571" y="237" font-size="12" text-anchor="middle">sd_notify·metrics·journal</text>
  <rect x="216" y="270" width="454" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="443" y="290" font-size="12" text-anchor="middle">같은 엔진을 세 곳에서 쓴다: 벤치(sdq.py), 데몬(q7_health),</text>
  <text x="443" y="306" font-size="12" text-anchor="middle">CI 회귀(test_synth + sdq_check) — 판정 규칙이 한 군데에만 있다</text>
</svg>
```

그림 3 — 툴 구조. 회색 어댑터(UDP, ROS 2)는 이 노트에서 돌리지 않고 Q5·Q6에서 만든 수신 코드를 같은 chunk 형식으로 감싸면 된다.

설계 결정과 이유:

| 결정 | 이유 | 대안과 그 문제 |
|---|---|---|
| Python + numpy, chunk 단위 벡터 연산 | 샘플마다 Python 루프를 돌면 1 kHz × 여러 센서에서 CPU를 먹는다. chunk(16~256개)를 한 번에 처리하면 Python 오버헤드가 chunk 수에 비례 | C로 짜면 빠르지만 판정 규칙을 바꾸기 어렵다. 장비 위 상주 데몬이 CPU 예산에 걸리면 엔진만 C로 옮긴다 |
| 공통 chunk 형식 | 어댑터 추가가 쉽고, 단위 테스트(합성 스트림)가 실제 장치와 같은 코드 경로를 탄다 | 센서별 툴: 규칙 불일치 |
| 히스토그램 + 카운터만 상태로 보관 | 메모리 고정, 24시간 상주 가능 | 원시 dt 보관: 메모리 무한 증가 |
| 원시 레코드는 별도 로그(rotation) | 사후 분석(ADEV, 재현)용. 데몬 메모리와 분리 | 로그 없이: 필드 이슈 재현 불가 |
| 도착 시각은 어댑터가 찍는다 | 시계 도메인을 어댑터가 안다(IIO는 고른 시계, V4L2는 MONOTONIC, CAN은 REALTIME) | 엔진이 찍으면 chunk 처리 지연이 latency에 섞인다 |

### 3.2 통계 엔진 `sdq_stats.py`

무엇을 하는 코드인가: 스트림 하나의 상태(카운터, 히스토그램 3개, Welford 누적값, 마지막 샘플)를 들고, chunk가 올 때마다 갱신한다. 이전 chunk의 마지막 시각·순번·값을 들고 있어서 chunk 경계에서도 dt·seq 차이·stuck 길이가 끊기지 않는다.

```python
# sdq_stats.py (1/3) — 스트림 하나의 품질 통계. 메모리는 샘플 수와 무관한 고정 크기
import numpy as np

# 히스토그램 경계 [us]: 0, 1 us ~ 10 s, decade당 50칸(칸 폭 약 4.7%) — 크기 고정
EDGES = np.concatenate(([0.0], np.logspace(0, 7, 351)))

class Stream:
    def __init__(s, name, rate_hz, lo=None, hi=None, stuck_n=50, seq_mod=None):
        s.name, s.rate, s.T = name, rate_hz, 1e6 / rate_hz   # 명목 주기 [us]
        s.lo, s.hi, s.stuck_n, s.seq_mod = lo, hi, stuck_n, seq_mod
        s.hist = np.zeros(len(EDGES) + 1, np.int64)        # dt 분포 (+under/overflow)
        s.lhist = np.zeros(len(EDGES) + 1, np.int64)       # latency 분포
        s.jhist = np.zeros(len(EDGES) + 1, np.int64)       # jitter |dt - T| 분포
        s.n = s.gaps = s.lost_ts = s.lost_seq = s.dups = s.ooo = 0
        s.back = s.zero_dt = s.nan = s.out_range = s.stuck = s.clk_err = 0
        s.kdrop = 0                                        # 커널이 직접 알려 준 drop
        s.n_dt, s.mean, s.m2 = 0, 0.0, 0.0; s.dtmin, s.dtmax = np.inf, 0.0  # dt Welford
        s.t0 = s.t_last = s.seq_last = s.v_last = None
        s.run = s.run_max = 0
        s.lat_max, s.lat_min, s.lat_neg = -np.inf, np.inf, 0

    def _welford(s, x):                                    # Chan의 병합 공식
        n_a, n_b = s.n_dt, len(x)
        if n_b == 0:
            return
        m_b, d = x.mean(), x.mean() - s.mean
        s.m2 += ((x - m_b) ** 2).sum() + d * d * n_a * n_b / (n_a + n_b)
        s.mean += d * n_b / (n_a + n_b); s.n_dt = n_a + n_b
```

```python
    # ---- sdq_stats.py (2/3) — chunk 하나를 받아 카운터와 히스토그램만 갱신한다 ----
    def update(s, t_ns, arr_ns=None, seq=None, v=None):
        t = np.asarray(t_ns, np.int64)
        if len(t) == 0:
            return
        if s.t0 is None:
            s.t0 = int(t[0])
        prev = t if s.t_last is None else np.concatenate(([s.t_last], t))
        dt = np.diff(prev) / 1e3                           # [us]
        s.back += int((dt < 0).sum()); s.zero_dt += int((dt == 0).sum())
        g = dt > 1.5 * s.T
        s.gaps += int(g.sum())
        s.lost_ts += int(np.maximum(np.rint(dt[g] / s.T) - 1, 0).sum())
        pos = dt[dt > 0]
        s.hist += np.bincount(np.searchsorted(EDGES, pos), minlength=len(s.hist))
        ok = pos[~g[dt > 0]]                               # gap은 jitter에서 뺀다
        s.jhist += np.bincount(np.searchsorted(EDGES, np.abs(ok - s.T)), minlength=len(s.jhist))
        if len(pos):
            s.dtmin, s.dtmax = min(s.dtmin, pos.min()), max(s.dtmax, pos.max())
        s._welford(pos)
        s.t_last, s.n = int(t[-1]), s.n + len(t)
        if arr_ns is not None:
            lat = (np.asarray(arr_ns, np.int64) - t) / 1e3
            s.clk_err += int((np.abs(lat) > 1e6).sum())    # 1 s 넘게 어긋남 = 시계 도메인 의심
            ok = lat[np.abs(lat) <= 1e6]
            s.lat_neg += int((ok < 0).sum())               # 도착이 샘플 시각보다 먼저?!
            s.lhist += np.bincount(np.searchsorted(EDGES, ok[ok >= 0]), minlength=len(s.lhist))
            s.lat_max, s.lat_min = max(s.lat_max, lat.max()), min(s.lat_min, lat.min())
        if seq is not None:
            s._seq(np.asarray(seq, np.int64))
        if v is not None:
            s._values(np.asarray(v, np.float64))
```

```python
    # ---- sdq_stats.py (3/3) — 순번·값 검사와 요약 ----
    def _seq(s, q):
        prev = q if s.seq_last is None else np.concatenate(([s.seq_last], q))
        d = np.diff(prev)
        if s.seq_mod:                                      # wrap: (-mod/2, mod/2]로 접기
            d = (d + s.seq_mod // 2) % s.seq_mod - s.seq_mod // 2
        s.dups += int((d == 0).sum()); s.ooo += int((d < 0).sum())
        s.lost_seq += int((d[d > 1] - 1).sum()); s.seq_last = int(q[-1])

    def _values(s, v):
        s.nan += int(np.isnan(v).sum())
        if s.lo is not None:
            s.out_range += int(((v < s.lo) | (v > s.hi)).sum())
        first = v[0] == s.v_last if s.v_last is not None else False
        eq = np.concatenate(([first], v[1:] == v[:-1]))
        i = np.arange(len(v))
        last_reset = np.maximum.accumulate(np.where(~eq, i, -1))
        run = np.where(last_reset < 0, s.run + i + 1, i - last_reset)
        s.stuck += int((run >= s.stuck_n).sum())
        s.run, s.run_max, s.v_last = int(run[-1]), max(s.run_max, int(run.max())), v[-1]

    def pct(s, p, h=None):                                 # 히스토그램에서 백분위 (칸 상단)
        h = s.hist if h is None else h
        c = np.cumsum(h)
        if c[-1] == 0:
            return float('nan')
        k = int(np.searchsorted(c, p / 100 * c[-1]))
        return round(float(EDGES[min(k, len(EDGES) - 1)]), 1)

    def summary(s):
        span = (s.t_last - s.t0) / 1e9 if s.n > 1 else 0
        rate = (s.n - 1) / span if span > 0 else 0
        sd = (s.m2 / s.n_dt) ** 0.5 if s.n_dt else 0
        return dict(name=s.name, n=s.n, span_s=round(span, 3), rate_hz=round(rate, 3),
                    rate_err_ppm=round((rate / s.rate - 1) * 1e6) if rate else None,
                    dt_mean_us=round(s.mean, 1), dt_sd_us=round(sd, 1),
                    dt_min_us=round(s.dtmin, 1), dt_p50_us=s.pct(50), dt_p99_us=s.pct(99),
                    dt_p999_us=s.pct(99.9), dt_max_us=round(s.dtmax, 1),
                    jit_p50_us=s.pct(50, s.jhist), jit_p99_us=s.pct(99, s.jhist),
                    gaps=s.gaps, lost_by_ts=s.lost_ts, lost_by_seq=s.lost_seq,
                    kernel_drops=s.kdrop, dups=s.dups, out_of_order=s.ooo,
                    ts_backwards=s.back, ts_zero_dt=s.zero_dt, clock_domain_err=s.clk_err,
                    lat_p50_us=s.pct(50, s.lhist), lat_p99_us=s.pct(99, s.lhist),
                    lat_max_us=round(s.lat_max, 1), lat_min_us=round(s.lat_min, 1),
                    lat_negative=s.lat_neg, nan=s.nan, out_of_range=s.out_range,
                    stuck_samples=s.stuck, stuck_run_max=s.run_max)
```

코드에서 볼 것:

- `np.searchsorted(EDGES, x)`가 칸 번호를 주고 `np.bincount(..., minlength=...)`가 그 칸 개수를 센다. 정렬 없이 O(n log 352)다.
- `_welford`는 chunk 전체의 평균·편차제곱합을 기존 누적값과 합친다(Chan 등의 병합 공식). 샘플 하나씩 도는 Welford와 결과가 같다.
- `_seq`의 wrap 처리: `seq_mod = 2^32`(V4L2 `sequence`는 `__u32`)이면 차이를 `(-2^31, 2^31]`로 접는다. 그래서 `4294967295 → 0`은 +1(정상)이고, 큰 음수 점프만 순서 뒤바뀜으로 센다.
- stuck 검사는 "직전과 같은가"의 bool 배열에서 마지막으로 False였던 위치를 `np.maximum.accumulate`로 구해 연속 길이를 벡터로 계산한다.
- latency가 1 s를 넘는 샘플은 히스토그램에 넣지 않고 `clk_err`로만 센다. 시계가 다르면 latency 숫자 자체가 의미가 없기 때문이다. 음수 latency(도착이 샘플 시각보다 먼저)는 따로 `lat_neg`로 센다 — 처음 버전은 음수를 0으로 잘라 히스토그램에 넣었다가 V4L2에서 중요한 사실을 숨겼다(6.2절).

### 3.3 어댑터 `sdq_adapt.py`

#### IIO — scan_elements로 레코드 배치를 계산한다

IIO 버퍼의 한 레코드(scan) 배치는 I2 노트 4절에서 손으로 계산했다: 켜진 채널을 index 순서로 놓고, 각 원소를 **자기 저장 크기의 배수 오프셋**에 정렬하고, 레코드 전체를 가장 큰 원소 크기의 배수로 맞춘다. 어댑터는 이 규칙을 그대로 numpy structured dtype으로 만든다.

```python
# sdq_adapt.py (1/3) — 공통 형식: 어댑터는 chunk dict(t, arr, seq, v)를 yield 한다
import os, re, time, glob, select, socket, struct, fcntl, mmap
import numpy as np

def mono_ns():
    return time.clock_gettime_ns(time.CLOCK_MONOTONIC)

# ---- IIO 버퍼: scan_elements를 읽어 레코드 dtype을 만든다 ----
def iio_dtype(sysdir):
    els = []
    for f in glob.glob(sysdir + '/scan_elements/*_en'):
        if open(f).read().strip() != '1':
            continue
        base = f[:-3]
        idx = int(open(base + '_index').read())
        m = re.match(r'([bl])e:([su])(\d+)/(\d+)>>(\d+)', open(base + '_type').read())
        end, sign, bits, store, shift = m.groups()
        name = os.path.basename(base)
        els.append((idx, name, ('<' if end == 'l' else '>') +
                    ('i' if sign == 's' else 'u') + str(int(store) // 8)))
    fields, off = [], 0
    for idx, name, code in sorted(els):                     # index 순 + 자기 크기로 정렬
        size = int(code[2:])
        off = (off + size - 1) // size * size
        fields.append((name, code, off)); off += size
    rec = (off + 7) // 8 * 8 if any(c.endswith('8') for _, c, _ in fields) else off
    return np.dtype({'names': [f[0] for f in fields], 'formats': [f[1] for f in fields],
                     'offsets': [f[2] for f in fields], 'itemsize': rec})

def iio(devname, vchan='in_voltage0', secs=10, slow_s=0.0):
    sysdir = '/sys/bus/iio/devices/' + devname
    dt = iio_dtype(sysdir)
    fd = os.open('/dev/' + devname, os.O_RDONLY | os.O_NONBLOCK)
    stale = 0
    try:                                                   # 시작 전에 쌓인 scan은 버린다
        while True:
            stale += len(os.read(fd, 65536)) // dt.itemsize
    except BlockingIOError:
        print('# iio: discarded %d stale scans' % stale)
    end = time.monotonic() + secs
    buf = b''
    while time.monotonic() < end:
        select.select([fd], [], [], 0.5)
        try:
            buf += os.read(fd, 4096 // dt.itemsize * dt.itemsize)
        except BlockingIOError:
            continue
        arr = mono_ns()
        k = len(buf) // dt.itemsize
        r = np.frombuffer(buf[:k * dt.itemsize], dt); buf = buf[k * dt.itemsize:]
        ts = r['in_timestamp'].astype(np.int64)
        yield dict(t=ts, arr=np.full(k, arr), v=r[vchan].astype(np.float64))
        if slow_s:
            time.sleep(slow_s)                             # 느린 소비자 흉내
    os.close(fd)
```

볼 것: 장치를 연 직후 **이미 쌓여 있던 scan을 모두 읽어 버린다**(`discarded N stale scans`). 처음 버전에는 이 줄이 없었는데, 버퍼를 켠 뒤 Python이 numpy를 import하는 수 초 동안 kfifo가 가득 차서 첫 chunk가 오래된 샘플 256개 + 수 초짜리 gap으로 시작했다. 그 결과 "깨끗한" 측정이 gap 44개, 손실 834개, `clock_domain_err=256`(오래된 샘플의 latency가 1 s 초과)로 나왔다. **측정 시작 조건(warm-up, flush)도 툴 설계의 일부**다. 도착 시각은 `read()` 직후 한 번 찍어 chunk 전체에 쓴다. 그래서 IIO의 latency는 "샘플 시각 → 그 샘플이 들어 있는 read가 끝난 시각"이다. 버퍼 watermark가 크면 latency가 커지는 것이 그대로 보인다.

#### V4L2 — `v4l2_buffer`를 struct로 직접 다룬다

v4l2-ctl 출력을 파싱하는 대신 ioctl을 직접 부른다. 필요한 것은 `sequence`, `timestamp`, `flags`(ERROR 비트) 세 필드뿐이라 픽셀은 mmap하지 않는다(MMAP 버퍼는 매핑하지 않아도 QBUF/DQBUF가 된다). 구조체 크기와 오프셋은 VM에서 C로 확인했다.

```c
/* v4l2sz.c — arm64에서 struct v4l2_buffer 배치와 ioctl 번호 확인 */
#include <stdio.h>
#include <stddef.h>
#include <linux/videodev2.h>
int main(void){
 printf("buf=%zu ts=%zu seq=%zu mem=%zu m=%zu len=%zu flags=%zu req=%zu\n",sizeof(struct v4l2_buffer),offsetof(struct v4l2_buffer,timestamp),offsetof(struct v4l2_buffer,sequence),offsetof(struct v4l2_buffer,memory),offsetof(struct v4l2_buffer,m),offsetof(struct v4l2_buffer,length),offsetof(struct v4l2_buffer,flags),sizeof(struct v4l2_requestbuffers));
 printf("REQBUFS=%#lx QUERYBUF=%#lx QBUF=%#lx DQBUF=%#lx STREAMON=%#lx\n",(unsigned long)VIDIOC_REQBUFS,(unsigned long)VIDIOC_QUERYBUF,(unsigned long)VIDIOC_QBUF,(unsigned long)VIDIOC_DQBUF,(unsigned long)VIDIOC_STREAMON);}
```

```sh
limactl shell elinux -- bash -lc 'gcc -Wall /Users/donh/.elinux_scratch/Q7/v4l2sz.c -o /tmp/Q7/v && /tmp/Q7/v'
```

```text
buf=88 ts=24 seq=56 mem=60 m=64 len=72 flags=12 req=20
REQBUFS=0xc0145608 QUERYBUF=0xc0585609 QBUF=0xc058560f DQBUF=0xc0585611 STREAMON=0x40045612
```

볼 것: 64비트에서 `struct timeval`이 8바이트 정렬이라 `field`(오프셋 16) 뒤에 4바이트 패딩이 있고 timestamp가 24에서 시작한다. ioctl 번호의 `0x58`이 크기 88이다(`_IOWR('V', 17, struct v4l2_buffer)`). 32비트 ARM에서는 이 숫자가 전부 달라진다 — Python에서 struct를 직접 다룰 때의 함정이다.

```python
# ---- sdq_adapt.py (2/3) — V4L2 capture: MMAP 버퍼, DQBUF의 sequence/timestamp ----
VIDIOC_REQBUFS, VIDIOC_QUERYBUF = 0xc0145608, 0xc0585609     # arm64 값 (C로 확인)
VIDIOC_QBUF, VIDIOC_DQBUF, VIDIOC_STREAMON, VIDIOC_STREAMOFF = \
    0xc058560f, 0xc0585611, 0x40045612, 0x40045613
CAP, MMAP_MEM = 1, 1                                       # V4L2_BUF_TYPE_VIDEO_CAPTURE, MEMORY_MMAP
BUF = struct.Struct('=IIIII4xqq16sII8sII4x4x')             # struct v4l2_buffer, 88 B
assert BUF.size == 88

def _buf(index=0):
    return bytearray(BUF.pack(index, CAP, 0, 0, 0, 0, 0, b'', 0, MMAP_MEM, b'', 0, 0))

def v4l2(dev='/dev/video0', nbufs=4, secs=10, slow_s=0.0, seq_bias=0):
    fd = os.open(dev, os.O_RDWR | os.O_NONBLOCK)
    req = bytearray(struct.pack('=IIII4x', nbufs, CAP, MMAP_MEM, 0))
    fcntl.ioctl(fd, VIDIOC_REQBUFS, req)
    n = struct.unpack_from('=I', req)[0]                   # 드라이버가 준 실제 개수
    for i in range(n):
        fcntl.ioctl(fd, VIDIOC_QBUF, _buf(i))              # mmap 없이도 큐잉 가능
    fcntl.ioctl(fd, VIDIOC_STREAMON, struct.pack('=I', CAP))
    end, out = time.monotonic() + secs, []
    while time.monotonic() < end:
        select.select([fd], [], [], 1.0)
        b = _buf()
        try:
            fcntl.ioctl(fd, VIDIOC_DQBUF, b)
        except BlockingIOError:
            continue
        arr = mono_ns()
        f = BUF.unpack(b)
        idx, flags, sec, usec, seq = f[0], f[3], f[5], f[6], f[8]
        out.append((sec * 1_000_000_000 + usec * 1000, arr, seq + seq_bias, flags & 0x40))
        if slow_s:
            time.sleep(slow_s)                             # 처리가 느린 소비자
        fcntl.ioctl(fd, VIDIOC_QBUF, _buf(idx))
        if len(out) >= 8:
            a = np.array(out, np.int64); out = []
            yield dict(t=a[:, 0], arr=a[:, 1], seq=a[:, 2], err=int((a[:, 3] != 0).sum()))
    fcntl.ioctl(fd, VIDIOC_STREAMOFF, struct.pack('=I', CAP))
    os.close(fd)
    print('# v4l2: driver granted', n, 'buffers')
```

#### SocketCAN — 커널 수신 시각과 커널이 센 drop 수를 같이 받는다

CAN 소켓은 두 가지를 공짜로 준다. `SO_TIMESTAMPNS`를 켜면 **드라이버가 프레임을 받은 시각**(skb 타임스탬프, `CLOCK_REALTIME` 기준)이 cmsg로 오고, `SO_RXQ_OVFL`을 켜면 **이 소켓 수신 큐가 넘쳐서 버린 누적 개수**(`sk_drops`)가 cmsg로 온다. 그래서 CAN에서는 손실을 세 가지 방법(시각 gap, payload 카운터, 커널 drop 카운터)으로 교차 검증할 수 있다.

```python
# ---- sdq_adapt.py (3/3) — SocketCAN: 커널 수신 시각 + 소켓 overflow 카운터 ----
SO_TIMESTAMPNS, SO_RXQ_OVFL = 35, 40

def can(ifname='vcan0', can_id=0x123, secs=10, slow_s=0.0, rcvbuf=0):
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    s.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FILTER, struct.pack('=II', can_id, 0x7ff))
    s.setsockopt(socket.SOL_SOCKET, SO_TIMESTAMPNS, 1)
    s.setsockopt(socket.SOL_SOCKET, SO_RXQ_OVFL, 1)
    if rcvbuf:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
    s.bind((ifname,)); s.settimeout(0.2)
    end, out, ovfl, prev = time.monotonic() + secs, [], 0, 0   # sk_drops는 누적값
    while time.monotonic() < end:
        try:
            frame, anc, _, _ = s.recvmsg(16, 64)
        except socket.timeout:
            yield dict(t=[], tick=True); continue          # 데이터 없음도 health엔 정보
        rt = time.clock_gettime_ns(time.CLOCK_REALTIME)
        ts = None
        for lvl, typ, data in anc:
            if typ == SO_TIMESTAMPNS:
                sec, ns = struct.unpack('=qq', data); ts = sec * 1_000_000_000 + ns
            elif typ == SO_RXQ_OVFL:
                ovfl = struct.unpack('=I', data)[0]
        seq = struct.unpack_from('<Q', frame, 8)[0]        # cangen -D i: payload = 카운터
        out.append((ts, rt, seq, ovfl))
        if slow_s:
            time.sleep(slow_s)
        if len(out) >= 16:
            a = np.array(out, np.int64); out = []
            yield dict(t=a[:, 0], arr=a[:, 1], seq=a[:, 2], kdrop=ovfl - prev)
            prev = ovfl
```

볼 것: `cangen -D i`는 payload 8바이트를 리틀엔디언 카운터로 채운다(6.2절 `candump` 출력에서 확인). 실제 차량·로봇 CAN 메시지에서는 DBC에 정의된 4비트 rolling counter + CRC가 이 역할을 한다(Q4). 그 경우 `seq_mod=16`으로 두면 같은 엔진이 그대로 동작한다. 단, 4비트 카운터는 **16개 이상 연속 손실을 못 본다** — 그래서 시각 gap 검사를 같이 돌린다.

#### UDP·ROS 2 — 여기서는 감싸기만

LiDAR UDP 수신(Q5)은 패킷 헤더의 순번·타임스탬프를 꺼내 `t, seq`로, `SO_TIMESTAMPNS`로 받은 커널 수신 시각을 `arr`로 주면 된다. 소켓 drop은 CAN과 같은 `SO_RXQ_OVFL`이 UDP에서도 동작한다. ROS 2(Q6)는 구독 콜백에서 `msg.header.stamp`를 `t`로, 콜백 진입 시각을 `arr`로, (있으면) 드라이버가 넣은 순번을 `seq`로 준다. 둘 다 이 노트에서는 실행하지 않았다(가이드).

### 3.4 로그 — 고정 크기 레코드 + rotation

헬스 판정에는 히스토그램이면 충분하지만, 필드 이슈를 재현하려면 원시 레코드가 필요하다. 장비 저장장치(eMMC, J 노트)는 작고 수명이 있으므로 **크기 상한 + 오래된 파일 삭제**가 기본이다. 형식은 고정 크기 binary 레코드(32 B: t, arr, seq, v, flags)다. 텍스트 CSV보다 3~5배 작고, numpy로 그대로 다시 읽힌다.

```python
# sdq_log.py — 고정 크기 binary 레코드 + 크기 기반 rotation (최근 keep개 파일만 남김)
import os, struct, glob
import numpy as np

REC = np.dtype([('t', '<i8'), ('arr', '<i8'), ('seq', '<i8'), ('v', '<f4'), ('flags', '<u4')])
MAGIC = b'SDQ1'                                            # 파일 헤더: magic + 이름 길이 + 이름

class RotLog:
    def __init__(s, path, name, max_bytes=256 * 1024, keep=4):
        s.path, s.name, s.max, s.keep = path, name.encode(), max_bytes, keep
        s.idx, s.f = 0, None
        os.makedirs(os.path.dirname(path), exist_ok=True)
        s._open()

    def _open(s):
        if s.f:
            s.f.close()
        s.f = open('%s.%06d' % (s.path, s.idx), 'wb', buffering=64 * 1024)
        s.f.write(MAGIC + struct.pack('<H', len(s.name)) + s.name)
        old = sorted(glob.glob(s.path + '.*'))[:-s.keep]   # 오래된 것부터 삭제
        for o in old:
            os.unlink(o)
        s.idx += 1

    def write(s, c):
        n = len(c['t'])
        if n == 0:
            return
        r = np.zeros(n, REC)
        r['t'] = c['t']
        r['arr'] = c.get('arr', 0)
        r['seq'] = c.get('seq', -1)
        r['v'] = c.get('v', np.nan)
        s.f.write(r.tobytes())
        if s.f.tell() >= s.max:
            s._open()

    def close(s):
        s.f.flush(); s.f.close()

def read_all(path):                                        # 오프라인 분석용: 남은 파일을 순서대로
    out = []
    for fn in sorted(glob.glob(path + '.*')):
        b = open(fn, 'rb').read()
        assert b[:4] == MAGIC
        off = 6 + struct.unpack_from('<H', b, 4)[0]
        out.append(np.frombuffer(b[off:], REC))
    return np.concatenate(out) if out else np.zeros(0, REC)
```

볼 것: 쓰기는 64 KB 사용자 버퍼를 거치므로 샘플마다 `write()` syscall이 나가지 않는다. 대신 전원이 갑자기 끊기면 마지막 64 KB는 잃는다. 이 트레이드오프는 J 노트(fsync, 저널링)와 같은 이야기다. 블랙박스처럼 "사고 직전 N초"가 중요하면 RAM 링버퍼에 두다가 이벤트가 나면 그 링버퍼를 덤프하는 방식(펌웨어의 crash log와 같은 설계)을 쓴다.

#### mcap — 로보틱스에서 쓰는 표준 로그 컨테이너 (가이드, 실행 안 함)

직접 만든 binary 형식은 툴 하나에는 좋지만, 다른 팀(인지·제어)과 나누려면 표준 형식이 낫다. 로보틱스 쪽 사실상 표준은 **MCAP**(Foxglove가 만든 공개 형식)이고, ROS 2 Jazzy에서 `ros2 bag record`의 기본 저장 형식이다(Q6). 구조는 이렇다.

- 파일 = 헤더 + 여러 **chunk**(압축 가능, zstd/lz4) + **summary**(채널·스키마 목록, chunk 인덱스, 통계) + footer.
- 각 메시지는 `channel_id, sequence, log_time, publish_time, data`를 가진다. `log_time`(기록 시각)과 `publish_time`(발행 시각)을 따로 둔다는 점이 이 노트의 `arr`와 `t`에 해당한다.
- summary의 chunk 인덱스 덕분에 큰 파일에서도 시간 구간을 바로 찾아 읽는다. 끝까지 쓰지 못한 파일(전원 차단)도 chunk 단위로 복구된다.
- Python은 `mcap` 패키지(`pip install mcap`)의 `Writer`로 쓸 수 있다. 이 노트 랩에는 설치하지 않았다.

### 3.5 CLI `sdq.py` — 어댑터와 엔진을 잇는 껍데기

무엇을 하는 코드인가: 입력 종류를 고르고, chunk를 엔진과 로그에 흘리고, 끝나면 요약(텍스트), ASCII 히스토그램(0이 아닌 칸만), JSON을 낸다. 툴 자신의 최대 메모리(`maxrss_kb`)도 리포트에 넣는다 — 측정 도구가 장비 자원을 얼마나 먹는지도 품질 정보다.

```python
#!/usr/bin/env python3
# sdq.py — CLI: 어댑터 → Stream 통계 → 로그 → 리포트(텍스트 + ASCII 히스토그램 + JSON)
import sys, json, argparse, resource
import numpy as np
import sdq_adapt as A, sdq_stats as S, sdq_log as L

def ascii_hist(st, width=40):
    h = st.hist[:len(S.EDGES)]
    nz = np.nonzero(h)[0]
    if len(nz) == 0:
        return
    for k in nz:                                           # 0인 칸은 찍지 않는다
        lo, hi = (S.EDGES[k - 1] if k else 0), S.EDGES[k]
        bar = '#' * int(np.ceil(width * h[k] / h.max()))
        print('  %9.0f-%-9.0f us %7d %s' % (lo, hi, h[k], bar))

def main():
    p = argparse.ArgumentParser()
    p.add_argument('src', choices=['iio', 'v4l2', 'can'])
    p.add_argument('--dev'); p.add_argument('--rate', type=float, required=True)
    p.add_argument('--secs', type=float, default=10); p.add_argument('--slow', type=float, default=0)
    p.add_argument('--nbufs', type=int, default=4); p.add_argument('--rcvbuf', type=int, default=0)
    p.add_argument('--lo', type=float); p.add_argument('--hi', type=float)
    p.add_argument('--log'); p.add_argument('--json'); p.add_argument('--hist', action='store_true')
    a = p.parse_args()
    if a.src == 'iio':
        gen, st = A.iio(a.dev, secs=a.secs, slow_s=a.slow), S.Stream(a.dev, a.rate, a.lo, a.hi)
    elif a.src == 'v4l2':
        gen = A.v4l2(a.dev, a.nbufs, a.secs, a.slow)
        st = S.Stream(a.dev, a.rate, seq_mod=1 << 32)
    else:
        gen = A.can(a.dev, secs=a.secs, slow_s=a.slow, rcvbuf=a.rcvbuf)
        st = S.Stream(a.dev, a.rate)                      # 64비트 카운터: wrap 없음
    log, verr = (L.RotLog(a.log, a.dev) if a.log else None), 0
    for c in gen:
        st.update(c['t'], c.get('arr'), c.get('seq'), c.get('v'))
        st.kdrop += c.get('kdrop', 0); verr += c.get('err', 0)
        if log:
            log.write(c)
    if log:
        log.close()
    r = st.summary(); r['buf_flag_error'] = verr
    r['maxrss_kb'] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    for k, v in r.items():
        print('%-18s %s' % (k, v))
    if a.hist:
        ascii_hist(st)
    if a.json:
        r['hist'] = st.hist.tolist(); json.dump(r, open(a.json, 'w'))
    return r

if __name__ == '__main__':
    main()
```

---

## 4. 실습 준비 — rootfs-dom을 overlay로 부팅하고 결과를 디스크 이미지로 꺼낸다

실습은 systemd가 있는 rootfs가 필요하다(9절의 `WatchdogSec=`). 6 GiB rootfs를 복사하지 않고 **qcow2 overlay**(쓰기만 따로 저장)로 부팅한다. 스크립트·툴을 넣고 결과를 꺼낼 때는 **작은 ext4 이미지를 두 번째 디스크**로 붙인다. `mkfs.ext4 -d <디렉터리>`는 root 권한·mount 없이 디렉터리 내용으로 파일시스템을 만들고, `debugfs -R "rdump ..."`는 mount 없이 꺼낸다. VM 호스트의 전역 상태(nbd, loop mount)를 건드리지 않는 방법이다.

```sh
# run.sh <tag> <script>... — share 이미지를 만들고 부팅해서 결과를 /var/tmp/Q7/res/<tag> 로 꺼낸다
set -e
W=/var/tmp/Q7; S=/Users/donh/.elinux_scratch/Q7; tag=$1; shift
mkdir -p $W/stage && rm -rf $W/stage/* && cp -a $S/share/. $W/stage/ && mkdir -p $W/stage/sdq
cp $S/sdq/*.py $W/stage/sdq/
rm -f $W/$tag.img && mkfs.ext4 -q -d $W/stage $W/$tag.img 64M
rm -f $W/$tag.qcow2 && qemu-img create -q -f qcow2 -F raw -b /var/lab/out/rootfs-dom.ext4 $W/$tag.qcow2
expect $S/boot.exp $W/$tag.qcow2 $W/$tag.img "$@" > $W/$tag.console 2>&1 || echo "expect rc=$?"
rm -rf $W/res/$tag && mkdir -p $W/res/$tag
debugfs -R "rdump /out $W/res/$tag" $W/$tag.img 2>/dev/null
ls -la $W/res/$tag/out
```

```text
#!/usr/bin/expect -f
# boot.exp <qcow2> <share.img> <script>... — rootfs-dom 부팅, autologin 셸에서 스크립트 실행
set timeout 900
set q [lindex $argv 0]; set img [lindex $argv 1]
log_user 1
spawn qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 768M -nographic \
  -kernel /var/lab/out/Image-dom \
  -drive file=$q,if=virtio,format=qcow2 -drive file=$img,if=virtio,format=raw \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -append "console=ttyAMA0 root=/dev/vda rw rootwait systemd.default_device_timeout_sec=900"
expect -re {root@[^:]+:~# }
send "uname -r; nproc; mkdir -p /mnt; mount /dev/vdb /mnt && mkdir -p /mnt/out && echo MOUN''TED\r"
expect "MOUNTED"
foreach s [lrange $argv 2 end] {
  expect -re {# $}
  send "sh /mnt/$s.sh > /mnt/out/$s.log 2>&1; echo Q7''DONE-$s\r"
  set timeout 1800
  expect "Q7DONE-$s\r"
}
send "sync; umount /mnt; echo UMOUN''TED; poweroff\r"
expect "UMOUNTED"
set timeout 300
expect eof
```

볼 것:

- `echo MOUN''TED`: 셸은 따옴표를 지우고 `MOUNTED`를 출력하지만, 터미널 에코(입력한 명령줄)에는 `MOUN''TED`로 남는다. 그래서 expect가 **명령줄 에코를 결과로 착각하지 않는다.** 시리얼 콘솔을 expect로 몰 때 자주 밟는 함정이다.
- 실행 한 줄: `limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/Q7/run.sh main run_iio run_v4l2 run_can run_health'`.
- 처음에는 게스트 메모리를 1536 MB로 줬는데, IIO 실습 도중 **VM 호스트의 OOM killer가 QEMU를 죽였다**(`Out of memory: Killed process ... (qemu-system-aar) ... anon-rss:1251804kB`, 19개 QEMU가 12 GB VM을 나눠 쓰는 중). 768 MB로 줄여 다시 돌렸다. 게스트 안의 "데이터 품질" 이전에 **측정 장비 자체의 건강**도 확인해야 한다는 좋은 예다.
- 또 하나 밟은 함정: 처음 설정 스크립트를 `. /mnt/iio_setup.sh 100 256 monotonic`처럼 인자를 주고 source 했는데, Ubuntu의 `sh`(dash)는 `.`에 인자를 넘기지 않는다. `$1`이 빈 문자열이 되어 `echo "" > sampling_frequency`가 `I/O error`로 실패했고, 툴은 **기본값(100 Hz, 버퍼 길이 2, `realtime` 시계)**으로 측정했다. 그 실패한 측정이 무엇을 보여 줬는지는 5.6절에서 쓴다. 지금 스크립트는 함수로 바꿨다.

---

## 5. 어댑터 1 — IIO buffered stream (`iio_dummy` + hrtimer 트리거)

### 5.1 설정

`iio_dummy`는 configfs `mkdir`로 만드는 가상 IIO 장치이고, hrtimer 트리거도 configfs로 만든다(I2 노트 2절). 이 장치의 trigger handler는 **스레드 안에서** `iio_get_time_ns()`를 부르고, 값은 상수 표(`fakedata[]`)에서 가져온다(11절). 그래서 이 실습은 (1) 스레드 스케줄 지연이 타임스탬프 jitter로 나타나는 경우, (2) 값이 상수라서 stuck 검사가 반드시 울리는 경우를 함께 보여 준다.

```sh
# iio_setup.sh — iio_setup <hz> <buflen> <clock>: iio_dummy + hrtimer 트리거를 만들고 버퍼를 켠다
iio_setup() {
C=/sys/kernel/config/iio
modprobe iio_dummy; modprobe iio-trig-hrtimer
[ -d $C/devices/dummy/q7_dummy ] || mkdir $C/devices/dummy/q7_dummy
[ -d $C/triggers/hrtimer/q7_t ] || mkdir $C/triggers/hrtimer/q7_t
for d in /sys/bus/iio/devices/iio:device*; do [ "$(cat $d/name)" = q7_dummy ] && D=$d; done
for d in /sys/bus/iio/devices/trigger*; do [ "$(cat $d/name)" = q7_t ] && T=$d; done
echo 0 > $D/buffer/enable
echo $1 > $T/sampling_frequency
echo q7_t > $D/trigger/current_trigger
echo $3 > $D/current_timestamp_clock
echo 1 > $D/scan_elements/in_voltage0_en
echo 1 > $D/scan_elements/in_timestamp_en
echo $2 > $D/buffer/length
echo 1 > $D/buffer/enable
DEV=${D##*/}
echo "# $DEV trigger=$(cat $D/trigger/current_trigger) hz=$(cat $T/sampling_frequency)" \
     "buflen=$(cat $D/buffer/length) clock=$(cat $D/current_timestamp_clock)"
}
```

```sh
# run_iio.sh — IIO 어댑터: 깨끗한 조건 + 4가지 결함 주입
cd /mnt/sdq; O=/mnt/out; mkdir -p $O
. /mnt/iio_setup.sh
stop() { echo 0 > /sys/bus/iio/devices/$DEV/buffer/enable; }
echo "===== IIO-A clean 100 Hz"
iio_setup 100 256 monotonic
python3 sdq.py iio --dev $DEV --rate 100 --secs 20 --lo 0 --hi 4095 --hist \
    --json $O/iio_clean.json --log /var/q7log/iio_clean; stop
python3 sdq_offline.py /var/q7log/iio_clean
echo "===== IIO-B stress-ng load"
iio_setup 100 256 monotonic
stress-ng --cpu 2 --timeout 22s -q &
python3 sdq.py iio --dev $DEV --rate 100 --secs 20 --hist --json $O/iio_stress.json; stop; wait
echo "===== IIO-C trigger rate changed 100 -> 50 Hz at t=10s"
iio_setup 100 256 monotonic
(sleep 10; echo 50 > $T/sampling_frequency) &
python3 sdq.py iio --dev $DEV --rate 100 --secs 20 --hist --json $O/iio_rate.json; stop; wait
echo "===== IIO-D buffer length 4 + slow reader (0.2 s per read)"
iio_setup 100 4 monotonic
python3 sdq.py iio --dev $DEV --rate 100 --secs 20 --slow 0.2 --hist --json $O/iio_slow.json; stop
echo "===== IIO-E timestamp clock=realtime, date stepped back 5 s at t=8s"
iio_setup 100 256 realtime
(sleep 8; date -s "@$(( $(date +%s) - 5 ))" >/dev/null) &
python3 sdq.py iio --dev $DEV --rate 100 --secs 16 --json $O/iio_clock.json; stop; wait
```

결함 주입 방법을 정리하면 이렇다.

| 실험 | 주입 방법 | 기대하는 검출 |
|---|---|---|
| A | 없음 (100 Hz, 버퍼 256, `monotonic`) | 기준선 |
| B | 게스트 안 `stress-ng --cpu 2` | jitter·latency 증가 |
| C | 측정 도중 트리거 주파수 100 → 50 Hz | rate 오차, dt 분포가 두 봉우리 |
| D | 버퍼 길이 4 + 리더가 read마다 0.2 s 쉼 | kfifo overflow → gap·손실 (커널 카운터 없음) |
| E | 타임스탬프 시계 `realtime` + 측정 도중 `date`로 5 s 뒤로 | 비단조 1회, 시계 도메인 오류 전부 |

### 5.2 A — 기준선과 그 해석

A는 리포트 전체를 싣고, B 이후는 해석에 필요한 키만 골라 싣는다(줄을 고른 것이지 값은 실제 출력 그대로다).

```text
===== IIO-A clean 100 Hz
# iio:device0 trigger=q7_t hz=100.000000 buflen=256 clock=monotonic
# iio: discarded 256 stale scans
name               iio:device0
n                  1956
span_s             19.988
rate_hz            97.809
rate_err_ppm       -21906
dt_mean_us         10224.0
dt_sd_us           3293.9
dt_min_us          392.0
dt_p50_us          10471.3
dt_p99_us          21877.6
dt_p999_us         41686.9
dt_max_us          42685.2
jit_p50_us         1202.3
jit_p99_us         6918.3
gaps               98
lost_by_ts         117
lost_by_seq        0
kernel_drops       0
dups               0
out_of_order       0
ts_backwards       0
ts_zero_dt         0
clock_domain_err   0
lat_p50_us         9120.1
lat_p99_us         575439.9
lat_max_us         763846.8
lat_min_us         285.1
lat_negative       0
nan                0
out_of_range       0
stuck_samples      1906
stuck_run_max      1955
buf_flag_error     0
maxrss_kb          32812
records 1956  mean period 10237.753 us  (rate 97.6777 Hz)
   m   tau_ms    ADEV(fractional)
   1     10.2    3.533e-01
   2     20.5    2.144e-01
   5     51.2    1.159e-01
  10    102.4    7.863e-02
  20    204.8    6.086e-02
  50    511.9    4.501e-02
 100   1023.8    3.708e-02
 200   2047.6    2.543e-02
 500   5118.9    1.311e-02
```

출력에서 볼 것:

- `discarded 256 stale scans`: 3.3절의 flush가 동작했다. 버퍼 길이 256이 꽉 차 있었다는 뜻이다.
- **TCG에서는 "깨끗함"도 깨끗하지 않다.** rate가 97.8 Hz(−21,906 ppm), 1.5주기를 넘는 gap 98개, 시각 기준 손실 117개다. 게스트 hrtimer가 호스트 스케줄(19개 QEMU가 6코어를 나눠 씀)에 밀려 늦게 터지고, 그 사이의 주기는 사라진다. 실제 보드라면 hrtimer 트리거 100 Hz의 jitter는 µs 단위이고 gap은 0이어야 한다.
- `lat_p50 = 9.1 ms`: 버퍼 watermark가 1이라 샘플이 들어오는 즉시 `select()`가 깨지지만, TCG에서 Python 루프 한 바퀴가 수 ms다. `lat_max 764 ms`는 리더가 통째로 밀린 순간이다.
- `stuck_samples 1906`, `stuck_run_max 1955`: `iio_dummy`의 `in_voltage0` 값은 상수 7이다. 실제 센서였다면 이것만으로 FAULT다. 툴이 "모든 것"을 정상 처리하지 않고 이상한 값 패턴을 정확히 잡았다.

dt 히스토그램의 가운데 부분(0인 칸은 툴이 이미 생략했고, 양 끝 꼬리는 여기서 잘랐다):

```text
       7244-7586      us      52 ########
       7586-7943      us      60 #########
       7943-8318      us      67 ##########
       8318-8710      us      87 #############
       8710-9120      us     119 ##################
       9120-9550      us     113 #################
       9550-10000     us     243 ###################################
      10000-10471     us     278 ########################################
      10471-10965     us     151 ######################
      10965-11482     us     130 ###################
      11482-12023     us     107 ################
      12023-12589     us      95 ##############
      12589-13183     us      50 ########
      13183-13804     us      37 ######
      13804-14454     us      41 ######
      14454-15136     us      19 ###
```

10 ms 양옆 칸 두 개에 27 %가 몰려 있지만, 7~15 ms에 넓게 퍼진 어깨가 있다. 이 모양이 "평균은 10.2 ms로 거의 맞는데 p99는 21.9 ms"인 이유다.

#### Allan deviation — 로그로 사후 분석

A 실험은 `--log`로 원시 레코드를 남겼다. 사후 분석 도구는 로그를 읽어 평균 주기를 최소제곱으로 구하고, 2.5절의 공식으로 ADEV를 낸다.

```python
#!/usr/bin/env python3
# sdq_offline.py — 로그 파일로 사후 분석: 주기 추정, 타이밍 오차의 Allan deviation
import sys
import numpy as np
import sdq_log as L

def adev(t_ns, taus):                                      # overlapping ADEV, 입력 = 타임스탬프
    t = (t_ns - t_ns[0]).astype(np.float64)
    k = np.arange(len(t))
    T = np.polyfit(k, t, 1)[0]                             # 최소제곱 평균 주기 [ns]
    x = t - k * T                                          # 위상 오차 x_k [ns]
    out = []
    for m in taus:
        if 2 * m >= len(x):
            break
        d = x[2 * m:] - 2 * x[m:-m] + x[:-2 * m]
        out.append((m, m * T / 1e6, np.sqrt((d ** 2).mean() / 2) / (m * T)))
    return T, out

if __name__ == '__main__':
    r = L.read_all(sys.argv[1])
    T, rows = adev(r['t'], [1, 2, 5, 10, 20, 50, 100, 200, 500])
    print('records %d  mean period %.3f us  (rate %.4f Hz)' % (len(r), T / 1e3, 1e9 / T))
    print('   m   tau_ms    ADEV(fractional)')
    for m, tau, a in rows:
        print('%4d %8.1f    %.3e' % (m, tau, a))
```

```text
records 1956  mean period 10237.753 us  (rate 97.6777 Hz)
   m   tau_ms    ADEV(fractional)
   1     10.2    3.533e-01
   2     20.5    2.144e-01
   5     51.2    1.159e-01
  10    102.4    7.863e-02
  20    204.8    6.086e-02
  50    511.9    4.501e-02
 100   1023.8    3.708e-02
 200   2047.6    2.543e-02
 500   5118.9    1.311e-02
```

출력에서 볼 것: `tau`가 10 ms → 102 ms로 10배 늘 때 ADEV는 0.353 → 0.079(4.5배 감소), 102 ms → 1 s에서는 0.079 → 0.037(2.1배 감소)에 그친다. 순수 white jitter라면 10배씩 줄어야 한다(기울기 −1). 기울기가 −0.65, −0.33으로 완만해진다는 것은 **수백 ms~초 단위로 주기 자체가 떠돈다**는 뜻이다. TCG에서 호스트 경쟁이 몇 초씩 몰려오는 것과 맞다. 실제 보드에서 같은 그래프가 1~100 s 구간에서 평평해지면 온도 drift를 의심한다.

### 5.3 B — 게스트 부하

```text
===== IIO-B stress-ng load
# iio:device0 trigger=q7_t hz=100.000000 buflen=256 clock=monotonic
# iio: discarded 256 stale scans
n                  1973
rate_hz            98.664
rate_err_ppm       -13361
dt_p50_us          10471.3
dt_p99_us          17378.0
dt_max_us          97638.7
jit_p99_us         5495.4
gaps               55
lost_by_ts         69
lat_p50_us         7244.4
lat_p99_us         66069.3
lat_max_us         137267.0
```

볼 것: 게스트 안 부하를 넣었는데 숫자가 A보다 크게 나쁘지 않다(gap 55 vs 98). TCG에서는 **게스트 부하보다 호스트 경쟁이 지배적**이라 두 측정의 차이가 잡음 안에 있다. 이런 비교는 한 번 측정으로 결론 내지 않고 반복해 분포로 봐야 한다 — 실제 HW에서는 B가 A보다 명확히 나빠야 정상이다(N3 노트의 cyclictest 비교와 같은 원리).

### 5.4 C — 트리거 주파수가 바뀌면

```text
===== IIO-C trigger rate changed 100 -> 50 Hz at t=10s
# iio:device0 trigger=q7_t hz=100.000000 buflen=256 clock=monotonic
# iio: discarded 256 stale scans
n                  1076
rate_hz            53.753
rate_err_ppm       -462469
dt_p50_us          19952.6
dt_p99_us          27542.3
gaps               903
lost_by_ts         930
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" font-size="14" text-anchor="middle">IIO hrtimer 100 Hz — 샘플 간격 분포 (실측, TCG)</text>
  <line x1="60" y1="240" x2="660" y2="240" stroke="currentColor"/>
  <line x1="60" y1="40" x2="60" y2="240" stroke="currentColor"/>
  <line x1="60" y1="240.0" x2="660" y2="240.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="244.0" font-size="12" text-anchor="end">0%</text>
  <line x1="60" y1="200.0" x2="660" y2="200.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="204.0" font-size="12" text-anchor="end">4%</text>
  <line x1="60" y1="160.0" x2="660" y2="160.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="164.0" font-size="12" text-anchor="end">8%</text>
  <line x1="60" y1="120.0" x2="660" y2="120.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="124.0" font-size="12" text-anchor="end">12%</text>
  <line x1="60" y1="80.0" x2="660" y2="80.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="84.0" font-size="12" text-anchor="end">16%</text>
  <line x1="60" y1="40.0" x2="660" y2="40.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="44.0" font-size="12" text-anchor="end">20%</text>
  <line x1="60.0" y1="240" x2="60.0" y2="245" stroke="currentColor"/>
  <text x="60.0" y="259" font-size="12" text-anchor="middle">2 ms</text>
  <line x1="230.8" y1="240" x2="230.8" y2="245" stroke="currentColor"/>
  <text x="230.8" y="259" font-size="12" text-anchor="middle">5 ms</text>
  <line x1="360.0" y1="240" x2="360.0" y2="245" stroke="currentColor"/>
  <text x="360.0" y="259" font-size="12" text-anchor="middle">10 ms</text>
  <line x1="489.2" y1="240" x2="489.2" y2="245" stroke="currentColor"/>
  <text x="489.2" y="259" font-size="12" text-anchor="middle">20 ms</text>
  <line x1="660.0" y1="240" x2="660.0" y2="245" stroke="currentColor"/>
  <text x="660.0" y="259" font-size="12" text-anchor="middle">50 ms</text>
  <text x="360" y="282" font-size="12" text-anchor="middle">샘플 간격 dt (로그 축)</text>
  <rect x="68.1" y="239.0" width="8.6" height="1.0" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="102.5" y="239.5" width="8.6" height="0.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="111.1" y="239.5" width="8.6" height="0.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="119.6" y="239.0" width="8.6" height="1.0" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="136.8" y="239.5" width="8.6" height="0.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="145.4" y="239.5" width="8.6" height="0.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="154.0" y="238.5" width="8.6" height="1.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="162.6" y="239.5" width="8.6" height="0.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="171.2" y="238.5" width="8.6" height="1.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="179.7" y="237.4" width="8.6" height="2.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="188.3" y="235.9" width="8.6" height="4.1" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="196.9" y="237.4" width="8.6" height="2.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="205.5" y="238.0" width="8.6" height="2.0" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="214.1" y="235.4" width="8.6" height="4.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="222.7" y="234.4" width="8.6" height="5.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="231.2" y="234.4" width="8.6" height="5.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="239.8" y="234.4" width="8.6" height="5.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="248.4" y="232.8" width="8.6" height="7.2" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="257.0" y="231.8" width="8.6" height="8.2" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="265.6" y="233.9" width="8.6" height="6.1" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="274.2" y="223.1" width="8.6" height="16.9" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="282.7" y="230.3" width="8.6" height="9.7" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="291.3" y="226.7" width="8.6" height="13.3" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="299.9" y="213.4" width="8.6" height="26.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="308.5" y="209.3" width="8.6" height="30.7" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="317.1" y="205.7" width="8.6" height="34.3" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="325.7" y="195.5" width="8.6" height="44.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="334.2" y="179.1" width="8.6" height="60.9" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="342.8" y="182.2" width="8.6" height="57.8" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="351.4" y="115.7" width="8.6" height="124.3" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="360.0" y="97.8" width="8.6" height="142.2" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="368.6" y="162.8" width="8.6" height="77.2" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="377.2" y="173.5" width="8.6" height="66.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="385.8" y="185.3" width="8.6" height="54.7" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="394.3" y="191.4" width="8.6" height="48.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="402.9" y="214.4" width="8.6" height="25.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="411.5" y="221.1" width="8.6" height="18.9" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="420.1" y="219.0" width="8.6" height="21.0" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="428.7" y="230.3" width="8.6" height="9.7" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="437.3" y="228.2" width="8.6" height="11.8" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="445.8" y="232.3" width="8.6" height="7.7" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="454.4" y="235.9" width="8.6" height="4.1" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="463.0" y="232.3" width="8.6" height="7.7" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="471.6" y="236.9" width="8.6" height="3.1" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="480.2" y="236.9" width="8.6" height="3.1" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="488.8" y="238.5" width="8.6" height="1.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="497.3" y="239.5" width="8.6" height="0.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="505.9" y="239.0" width="8.6" height="1.0" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="514.5" y="238.5" width="8.6" height="1.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="531.7" y="238.5" width="8.6" height="1.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="540.3" y="238.0" width="8.6" height="2.0" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="548.8" y="239.5" width="8.6" height="0.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="591.8" y="239.5" width="8.6" height="0.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="600.4" y="239.5" width="8.6" height="0.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="608.9" y="239.0" width="8.6" height="1.0" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="617.5" y="239.5" width="8.6" height="0.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="626.1" y="239.5" width="8.6" height="0.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="222.7" y="239.1" width="8.6" height="0.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="248.4" y="238.1" width="8.6" height="1.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="257.0" y="239.1" width="8.6" height="0.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="265.6" y="238.1" width="8.6" height="1.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="274.2" y="239.1" width="8.6" height="0.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="282.7" y="237.2" width="8.6" height="2.8" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="291.3" y="236.3" width="8.6" height="3.7" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="299.9" y="238.1" width="8.6" height="1.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="308.5" y="238.1" width="8.6" height="1.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="317.1" y="236.3" width="8.6" height="3.7" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="325.7" y="234.4" width="8.6" height="5.6" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="334.2" y="234.4" width="8.6" height="5.6" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="342.8" y="227.0" width="8.6" height="13.0" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="351.4" y="211.2" width="8.6" height="28.8" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="360.0" y="214.9" width="8.6" height="25.1" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="368.6" y="227.0" width="8.6" height="13.0" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="377.2" y="229.8" width="8.6" height="10.2" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="385.8" y="230.7" width="8.6" height="9.3" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="394.3" y="234.4" width="8.6" height="5.6" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="402.9" y="236.3" width="8.6" height="3.7" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="411.5" y="234.4" width="8.6" height="5.6" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="420.1" y="233.5" width="8.6" height="6.5" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="428.7" y="230.7" width="8.6" height="9.3" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="437.3" y="227.9" width="8.6" height="12.1" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="445.8" y="211.2" width="8.6" height="28.8" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="454.4" y="199.1" width="8.6" height="40.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="463.0" y="199.1" width="8.6" height="40.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="471.6" y="148.8" width="8.6" height="91.2" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="480.2" y="54.9" width="8.6" height="185.1" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="488.8" y="44.7" width="8.6" height="195.3" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="497.3" y="141.4" width="8.6" height="98.6" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="505.9" y="177.7" width="8.6" height="62.3" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="514.5" y="194.4" width="8.6" height="45.6" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="523.1" y="225.1" width="8.6" height="14.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="531.7" y="229.8" width="8.6" height="10.2" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="540.3" y="234.4" width="8.6" height="5.6" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="548.8" y="238.1" width="8.6" height="1.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="557.4" y="239.1" width="8.6" height="0.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="566.0" y="239.1" width="8.6" height="0.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="574.6" y="239.1" width="8.6" height="0.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="591.8" y="239.1" width="8.6" height="0.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="617.5" y="239.1" width="8.6" height="0.9" fill="#e08a3c" fill-opacity="0.55"/>
  <rect x="410" y="42" width="12" height="12" fill="#4a7bd0" fill-opacity="0.55"/>
  <text x="428" y="52" font-size="12">A 깨끗함 (100 Hz) (dt 1955개)</text>
  <rect x="410" y="62" width="12" height="12" fill="#e08a3c" fill-opacity="0.55"/>
  <text x="428" y="72" font-size="12">C 도중 50 Hz로 변경 (dt 1075개)</text>
</svg>
```

그림 4 — IIO 100 Hz 기준선(A)과 측정 도중 50 Hz로 바뀐 경우(C)의 dt 분포. 실측값(TCG). C는 10 ms와 20 ms 두 봉우리를 갖는다.

볼 것:

- 툴은 rate 오차 −462,469 ppm과 gap 903개를 낸다. 하지만 **gap 903개는 "손실"이 아니다.** 주기가 바뀌어서 모든 간격이 1.5T를 넘은 것이다. 판정은 "손실"이 아니라 "설정이 바뀌었다"여야 한다. 손실 판정만 보는 툴이었다면 엉뚱한 결론을 냈을 것이다 — 그래서 리포트에 **분포 모양**(그림 4의 두 봉우리)과 rate를 같이 낸다.
- 중앙값이 19.95 ms다. `sleep 10`이 Python 시작(수 초)보다 먼저 흘러가서, 측정 구간 대부분이 50 Hz였다. 측정 스크립트의 타이밍 가정도 TCG에서는 흔들린다.
- 실무에서 이 결함은 "다른 프로세스가 같은 센서의 `sampling_frequency`를 바꿨다", "드라이버가 suspend/resume 뒤 기본 ODR로 돌아갔다"(N6 노트)로 나타난다.

### 5.5 D — 작은 버퍼 + 느린 리더 = 흔적 없는 drop

```text
===== IIO-D buffer length 4 + slow reader (0.2 s per read)
# iio:device0 trigger=q7_t hz=100.000000 buflen=4 clock=monotonic
# iio: discarded 4 stale scans
n                  373
rate_hz            18.961
dt_p50_us          10471.3
dt_p99_us          218776.2
dt_max_us          289012.6
gaps               92
lost_by_ts         1592
kernel_drops       0
lat_p50_us         199526.2
lat_p99_us         288403.2
```

볼 것:

- 20초 동안 373개만 받았다. 100 Hz라면 약 1,960개여야 한다. 리더가 0.2 s마다 읽을 때 kfifo에는 4개만 남아 있고 나머지(약 16개)는 버려진다.
- `kernel_drops 0`: IIO는 overflow를 세어 주지 않는다(11절). 손실 1,592개는 **오로지 타임스탬프 gap**으로 추정한 것이다. 받은 373 + 추정 손실 1,592 = 1,965 ≈ 19.6 s × 100 Hz. 추정이 거의 맞다.
- `lat_p50 = 199.5 ms`: 리더 주기만큼 데이터가 늦게 소비된다. 해결은 버퍼 길이를 "리더 최대 지연 × rate"보다 크게(여기서는 최소 20, 여유를 두면 64 이상) 잡는 것이다.

### 5.6 E — 시계 도메인과 시계 스텝

```text
===== IIO-E timestamp clock=realtime, date stepped back 5 s at t=8s
# iio:device0 trigger=q7_t hz=100.000000 buflen=256 clock=realtime
# iio: discarded 256 stale scans
n                  1581
span_s             10.615
rate_hz            148.852
rate_err_ppm       488524
gaps               28
ts_backwards       1
clock_domain_err   1581
lat_max_us         -1792342132068081.2
lat_min_us         -1792342137508530.8
```

볼 것:

- `clock_domain_err 1581` = 모든 샘플. 타임스탬프는 `CLOCK_REALTIME`(1970 기준), 도착 시각은 `CLOCK_MONOTONIC`(부팅 기준)이라 차이가 약 −1.79 × 10^9 s(56년)다. 툴은 latency 숫자를 버리고(`lat_p50 nan`) "시계가 다르다"고만 말한다. 올바른 동작이다.
- `ts_backwards 1`: `date`로 5초 되돌린 순간이다. 그 직후 dt는 −4.99 s 근처였을 것이고, 그래서 `span_s`가 15.6 s가 아니라 10.6 s로 줄었다.
- 그 결과 `rate_hz 148.9`, `rate_err +488,524 ppm`이라는 **틀린 rate**가 나온다. 끝점 두 개로 계산하는 rate는 시계 스텝 하나에 완전히 망가진다. 그래서 `ts_backwards > 0`이면 rate 판정 자체를 무효로 해야 한다.
- 교훈: IIO의 기본 시계는 `realtime`이다(I2 노트에서도 확인). 로봇에서는 장치를 열기 전에 `current_timestamp_clock`을 `monotonic`(또는 시스템 표준 시계)으로 바꾼다. NTP·timesyncd·GNSS가 시각을 맞추는 순간 `realtime`은 뛴다.

#### 실패한 첫 측정이 보여 준 것

4절에서 말한 dash `.` 함정 때문에 첫 실행은 기본값(버퍼 길이 2, `realtime`)으로 돌았다. 그 리포트의 일부다.

```text
===== IIO-A clean 100 Hz
# iio:device0 trigger=q7_t hz=100.000000 buflen=2 clock=realtime
n                  1665
span_s             36.88
rate_hz            45.119
dt_max_us          16897760.6
gaps               156
lost_by_ts         2044
clock_domain_err   1665
```

20초 측정인데 `span_s`가 36.9 s, `dt_max`가 16.9 s다. 버퍼 길이 2라서 리더가 조금만 늦어도 버리고(gap 156, 손실 2,044), `realtime` 시계가 측정 도중 크게 움직였다(게스트 부팅 직후 시각 동기화로 추정, 확인 안 함). **설정이 기대와 다를 때 툴이 즉시 이상하다고 말해 준 사례**다. 측정 스크립트는 설정을 다시 읽어 찍는 줄(`# iio:device0 ... buflen=2 clock=realtime`)을 반드시 남겨야 한다.

---

## 6. 어댑터 2 — V4L2 capture (`vivid`)

### 6.1 설정과 결함 주입

`vivid`는 커널에 들어 있는 가상 카메라 드라이버다(I5 노트). 쓸모 있는 점은 **오류 주입 컨트롤**이 있다는 것이다: `percentage_of_dropped_buffers`(버퍼를 일부러 안 채움), `wrap_sequence_number`(sequence를 2^32 − 128에서 시작), `wrap_timestamp`(타임스탬프를 64비트·32비트 경계 근처로 옮김). 실제 카메라에서 재현하기 어려운 결함을 버튼 하나로 만든다.

```sh
# run_v4l2.sh — V4L2 어댑터: vivid capture, 깨끗한 조건 + 결함 주입
cd /mnt/sdq; O=/mnt/out; mkdir -p $O
modprobe vivid n_devs=1 node_types=0x1 2>&1
sleep 2
V=$(for d in /sys/class/video4linux/video*; do grep -q "vivid-000-vid-cap" $d/name && echo /dev/${d##*/}; done | head -1)
echo "# capture node: $V"
v4l2-ctl -d $V --set-fmt-video=width=320,height=180,pixelformat=YUYV
v4l2-ctl -d $V --get-fmt-video | grep -E "Width|Pixel"
v4l2-ctl -d $V --set-parm=30 | head -1
v4l2-ctl -d $V --get-parm | grep -i "frames per second"
v4l2-ctl -d $V -l | grep -E "dropped|wrap" 
echo "===== V4L2-A clean, 4 buffers"
python3 sdq.py v4l2 --dev $V --rate 30 --secs 20 --hist --json $O/v4l2_clean.json
echo "===== V4L2-B percentage_of_dropped_buffers=10"
v4l2-ctl -d $V -c percentage_of_dropped_buffers=10
python3 sdq.py v4l2 --dev $V --rate 30 --secs 20 --hist --json $O/v4l2_perc.json
v4l2-ctl -d $V -c percentage_of_dropped_buffers=0
echo "===== V4L2-C 2 buffers + slow consumer 50 ms/frame"
python3 sdq.py v4l2 --dev $V --rate 30 --secs 20 --nbufs 2 --slow 0.05 --hist --json $O/v4l2_slow.json
echo "===== V4L2-D stress-ng load, 4 buffers"
stress-ng --cpu 2 --timeout 22s -q &
python3 sdq.py v4l2 --dev $V --rate 30 --secs 20 --hist --json $O/v4l2_stress.json; wait
echo "===== V4L2-E wrap_sequence_number=1 (seq starts near 2^32)"
v4l2-ctl -d $V -c wrap_sequence_number=1
python3 sdq.py v4l2 --dev $V --rate 30 --secs 10 --json $O/v4l2_seqwrap.json
v4l2-ctl -d $V -c wrap_sequence_number=0
echo "===== V4L2-F wrap_timestamp=2 (32 bit): timestamp far from CLOCK_MONOTONIC"
v4l2-ctl -d $V -c wrap_timestamp=2
python3 sdq.py v4l2 --dev $V --rate 30 --secs 10 --json $O/v4l2_tswrap.json
v4l2-ctl -d $V -c wrap_timestamp=0
```

### 6.2 A — 기준선, 그리고 음수 latency

```text
===== V4L2-A clean, 4 buffers
# v4l2: driver granted 4 buffers
name               /dev/video0
n                  592
span_s             19.9
rate_hz            29.698
rate_err_ppm       -10057
dt_mean_us         33672.0
dt_sd_us           4671.5
dt_min_us          17410.0
dt_p50_us          33113.1
dt_p99_us          50118.7
dt_p999_us         63095.7
dt_max_us          61025.0
jit_p50_us         1995.3
jit_p99_us         15848.9
gaps               5
lost_by_ts         5
lost_by_seq        6
kernel_drops       0
dups               0
out_of_order       0
ts_backwards       0
ts_zero_dt         0
clock_domain_err   0
lat_p50_us         7244.4
lat_p99_us         28840.3
lat_max_us         28794.1
lat_min_us         -29037.5
lat_negative       587
nan                0
out_of_range       0
stuck_samples      0
stuck_run_max      0
buf_flag_error     0
maxrss_kb          32348
```

출력에서 볼 것:

- `Frame rate set to 30.000 fps`: vivid의 320×180 기본값은 5 fps였다(처음 확인한 `--get-parm` 결과). 측정 전에 명목값을 설정하고 **다시 읽어** 확인한다.
- `lost_by_seq 6`, `lost_by_ts 5`: TCG에서 vivid kthread가 늦게 깨서 6프레임을 건너뛰었다. 시각 기준 추정은 5개다 — **jitter에 묻힌 손실 하나를 시각 추정이 놓쳤다**. 순번이 있으면 순번이 정답이다.
- `lat_negative 587`(592개 중), `lat_min_us −29037.5`: 거의 모든 프레임이 **타임스탬프보다 먼저 도착했다**. 원인은 vivid 소스에 있다. 기본 타임스탬프 소스가 EOF(end of frame)라서 `timestamp = 채운 시각 + 0.9 × 프레임 주기`(`cap_frame_eof_offset = f_period × 9 / 10`)다. 33.3 ms × 0.9 = 30.0 ms만큼 미래 시각이 찍히고, 버퍼는 바로 전달되므로 latency가 약 −29 ms가 된다.
- 처음 버전의 툴은 음수 latency를 0으로 잘라 히스토그램에 넣어서 `lat_p50 0.0`(완벽해 보이는 숫자)을 냈다. **"완벽해 보이는 숫자"도 의심해야 한다.** 고친 툴은 음수를 따로 세고, 그 덕에 "이 드라이버의 타임스탬프는 노출 시작이 아니라 노출 끝(모델링된 시각)"이라는 사실이 드러났다. 실제 센서 드라이버도 SOE/EOF 중 무엇을 찍는지 `V4L2_BUF_FLAG_TSTAMP_SRC_SOE/EOF` 플래그로 알려 준다. 센서 퓨전에서 이 차이(수십 ms)는 치명적이다(Q3).
- dt가 33.1 ms 칸에 몰려 있지만 jitter p50이 2 ms다. `CONFIG_HZ=250`(4 ms jiffy)인 커널에서 vivid kthread는 jiffy 단위로 깨어나서, 33.3 ms 주기가 32 ms·36 ms로 양자화된다(11절, 그림 5의 파란 막대가 두 봉우리인 이유).

### 6.3 B·C·D — 드롭 세 가지

```text
===== V4L2-B percentage_of_dropped_buffers=10
# v4l2: driver granted 4 buffers
n                  528
rate_hz            26.702
gaps               60
lost_by_ts         63
lost_by_seq        65
lat_negative       526
===== V4L2-C 2 buffers + slow consumer 50 ms/frame
# v4l2: driver granted 2 buffers
n                  336
rate_hz            17.118
dt_p50_us          66069.3
dt_p99_us          138038.4
gaps               219
lost_by_ts         252
lost_by_seq        252
lat_p50_us         17378.0
lat_p99_us         138038.4
lat_negative       62
===== V4L2-D stress-ng load, 4 buffers
# v4l2: driver granted 4 buffers
n                  568
rate_hz            28.348
dt_max_us          635135.0
lost_by_ts         36
lost_by_seq        33
lat_p99_us         691831.0
lat_max_us         685432.3
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" font-size="14" text-anchor="middle">V4L2 vivid 30 fps — 프레임 간격 분포 (실측, TCG)</text>
  <line x1="60" y1="240" x2="660" y2="240" stroke="currentColor"/>
  <line x1="60" y1="40" x2="60" y2="240" stroke="currentColor"/>
  <line x1="60" y1="240.0" x2="660" y2="240.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="244.0" font-size="12" text-anchor="end">0%</text>
  <line x1="60" y1="200.0" x2="660" y2="200.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="204.0" font-size="12" text-anchor="end">6%</text>
  <line x1="60" y1="160.0" x2="660" y2="160.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="164.0" font-size="12" text-anchor="end">12%</text>
  <line x1="60" y1="120.0" x2="660" y2="120.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="124.0" font-size="12" text-anchor="end">18%</text>
  <line x1="60" y1="80.0" x2="660" y2="80.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="84.0" font-size="12" text-anchor="end">24%</text>
  <line x1="60" y1="40.0" x2="660" y2="40.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="44.0" font-size="12" text-anchor="end">30%</text>
  <line x1="60.0" y1="240" x2="60.0" y2="245" stroke="currentColor"/>
  <text x="60.0" y="259" font-size="12" text-anchor="middle">10 ms</text>
  <line x1="198.8" y1="240" x2="198.8" y2="245" stroke="currentColor"/>
  <text x="198.8" y="259" font-size="12" text-anchor="middle">20 ms</text>
  <line x1="382.3" y1="240" x2="382.3" y2="245" stroke="currentColor"/>
  <text x="382.3" y="259" font-size="12" text-anchor="middle">50 ms</text>
  <line x1="521.2" y1="240" x2="521.2" y2="245" stroke="currentColor"/>
  <text x="521.2" y="259" font-size="12" text-anchor="middle">100 ms</text>
  <line x1="660.0" y1="240" x2="660.0" y2="245" stroke="currentColor"/>
  <text x="676" y="259" font-size="12" text-anchor="end">200 ms</text>
  <text x="360" y="282" font-size="12" text-anchor="middle">샘플 간격 dt (로그 축)</text>
  <rect x="170.7" y="238.9" width="9.2" height="1.1" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="179.9" y="238.9" width="9.2" height="1.1" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="198.4" y="237.7" width="9.2" height="2.3" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="207.6" y="237.7" width="9.2" height="2.3" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="216.8" y="234.4" width="9.2" height="5.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="226.0" y="236.6" width="9.2" height="3.4" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="235.2" y="232.1" width="9.2" height="7.9" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="244.5" y="235.5" width="9.2" height="4.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="253.7" y="228.7" width="9.2" height="11.3" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="262.9" y="220.8" width="9.2" height="19.2" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="272.1" y="219.7" width="9.2" height="20.3" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="281.4" y="164.4" width="9.2" height="75.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="290.6" y="40.3" width="9.2" height="199.7" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="299.8" y="174.6" width="9.2" height="65.4" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="309.0" y="113.7" width="9.2" height="126.3" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="318.3" y="182.5" width="9.2" height="57.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="327.5" y="224.2" width="9.2" height="15.8" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="336.7" y="222.0" width="9.2" height="18.0" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="345.9" y="234.4" width="9.2" height="5.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="355.2" y="232.1" width="9.2" height="7.9" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="364.4" y="235.5" width="9.2" height="4.5" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="373.6" y="234.4" width="9.2" height="5.6" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="392.0" y="237.7" width="9.2" height="2.3" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="401.3" y="238.9" width="9.2" height="1.1" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="410.5" y="238.9" width="9.2" height="1.1" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="419.7" y="238.9" width="9.2" height="1.1" fill="#4a7bd0" fill-opacity="0.55"/>
  <rect x="235.2" y="238.0" width="9.2" height="2.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="253.7" y="236.0" width="9.2" height="4.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="262.9" y="238.0" width="9.2" height="2.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="272.1" y="238.0" width="9.2" height="2.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="281.4" y="216.1" width="9.2" height="23.9" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="290.6" y="186.3" width="9.2" height="53.7" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="299.8" y="216.1" width="9.2" height="23.9" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="309.0" y="184.3" width="9.2" height="55.7" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="318.3" y="212.1" width="9.2" height="27.9" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="327.5" y="218.1" width="9.2" height="21.9" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="336.7" y="228.1" width="9.2" height="11.9" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="345.9" y="238.0" width="9.2" height="2.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="401.3" y="238.0" width="9.2" height="2.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="410.5" y="224.1" width="9.2" height="15.9" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="419.7" y="206.2" width="9.2" height="33.8" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="428.9" y="120.6" width="9.2" height="119.4" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="438.2" y="76.8" width="9.2" height="163.2" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="447.4" y="208.2" width="9.2" height="31.8" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="456.6" y="228.1" width="9.2" height="11.9" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="465.8" y="234.0" width="9.2" height="6.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="475.1" y="236.0" width="9.2" height="4.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="493.5" y="234.0" width="9.2" height="6.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="502.7" y="228.1" width="9.2" height="11.9" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="511.9" y="230.0" width="9.2" height="10.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="521.2" y="234.0" width="9.2" height="6.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="548.8" y="238.0" width="9.2" height="2.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="567.3" y="236.0" width="9.2" height="4.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="576.5" y="238.0" width="9.2" height="2.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="604.2" y="236.0" width="9.2" height="4.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="622.6" y="238.0" width="9.2" height="2.0" fill="#d0564a" fill-opacity="0.55"/>
  <rect x="410" y="42" width="12" height="12" fill="#4a7bd0" fill-opacity="0.55"/>
  <text x="428" y="52" font-size="12">A 깨끗함, 버퍼 4 (dt 591개)</text>
  <rect x="410" y="62" width="12" height="12" fill="#d0564a" fill-opacity="0.55"/>
  <text x="428" y="72" font-size="12">C 버퍼 2 + 50 ms 처리 (dt 335개)</text>
</svg>
```

그림 5 — vivid 30 fps의 프레임 간격 분포. 실측값(TCG). 파란색은 버퍼 4개 기준선, 빨간색은 버퍼 2개 + 프레임마다 50 ms 처리하는 느린 소비자. 빨간 봉우리가 66 ms(= 2주기)로 옮겨 갔다.

볼 것:

- **B(드라이버가 10 % 드롭)**: 528개를 받고 순번 손실 65개. 기준선 손실(약 6)을 빼면 59개 ≈ 593 × 10 %. 주입한 비율과 맞는다. `lat_negative 526` — 드롭이 있어도 latency 성질은 그대로다.
- **C(소비자가 느림)**: 프레임 처리에 50 ms가 걸리고 버퍼가 2개뿐이면, 드라이버가 채울 빈 버퍼가 없는 순간이 생긴다. 252개 손실, dt 중앙값 66 ms(그림 5). latency p50이 17 ms로 커지고 음수 latency가 62개로 줄었다 — 버퍼가 큐에서 오래 기다리기 때문이다. 이 결함은 **버퍼 수를 늘리면 사라지고**, B는 사라지지 않는다. 12.2절의 디버깅 순서가 이 차이를 이용한다.
- **D(부하)**: 순번 손실 33, `dt_max 635 ms`, `lat_max 685 ms`. 부하 순간 kthread와 리더가 모두 밀렸다.

### 6.4 E·F — wrap

```text
===== V4L2-E wrap_sequence_number=1 (seq starts near 2^32)
# v4l2: driver granted 4 buffers
n                  296
lost_by_ts         3
lost_by_seq        3
dups               0
out_of_order       0
```

wrap이 정말 측정 구간 안에서 일어났는지 로그로 확인한다(`run_v4l2b.sh`: 같은 측정을 `--log`로 남기고 wrap 지점을 찾는다).

```sh
# run_v4l2b.sh — sequence wrap을 로그로 남겨 wrap 지점의 실제 seq 값을 본다
cd /mnt/sdq; O=/mnt/out; mkdir -p $O
modprobe vivid n_devs=1 node_types=0x1; sleep 2; V=/dev/video0
v4l2-ctl -d $V --set-fmt-video=width=320,height=180,pixelformat=YUYV; v4l2-ctl -d $V --set-parm=30 >/dev/null
echo "===== V4L2-E2 wrap_sequence_number=1, logged"
v4l2-ctl -d $V -c wrap_sequence_number=1
python3 sdq.py v4l2 --dev $V --rate 30 --secs 10 --log /var/q7log/v4l2_wrap | grep -E "^(n|lost_by_seq|dups|out_of_order) "
python3 - <<'PY'
import numpy as np, sdq_log as L
r = L.read_all('/var/q7log/v4l2_wrap'); s = r['seq']
d = np.diff(s); k = int(np.argmin(d))                    # 가장 큰 음수 점프 = wrap 지점
print('first seq', s[0], '(= 2^32 - %d)' % (2**32 - s[0]))
print('around wrap:', s[k-2:k+3].tolist(), ' raw diff:', d[k-2:k+2].tolist())
PY
```

```text
===== V4L2-E2 wrap_sequence_number=1, logged
n                  296
lost_by_seq        3
dups               0
out_of_order       0
first seq 4294967168 (= 2^32 - 128)
around wrap: [4294967293, 4294967294, 4294967295, 0, 1]  raw diff: [1, 1, -4294967295, 1]
```

볼 것: sequence가 4294967168(= 2^32 − 128)에서 시작해 128프레임(약 4.3 s) 뒤 `4294967295 → 0`으로 넘어갔다. 원시 차이는 −4,294,967,295지만 `seq_mod=2^32`로 접으면 +1이라서 `out_of_order 0`, 손실은 실제 손실(시각 기준과 같은 3개)만 나온다. wrap 처리 없이 뺐다면 "순서 뒤바뀜 1"로 경보가 울렸을 것이다. 24시간 30 fps 카메라는 4.5년 뒤에 wrap하지만, **장비의 카운터는 더 짧은 경우가 많고**(4비트 CAN counter는 16프레임마다), 테스트 없이 wrap을 믿으면 안 된다.

```text
===== V4L2-F wrap_timestamp=2 (32 bit): timestamp far from CLOCK_MONOTONIC
# v4l2: driver granted 4 buffers
n                  288
rate_hz            29.182
lost_by_seq        8
clock_domain_err   288
lat_p50_us         nan
lat_max_us         -2147483168197570.5
lat_min_us         -2147483168283254.8
```

볼 것: `wrap_timestamp=2`는 타임스탬프를 (2^31 − 16) s 근처로 옮긴다. 간격·순번은 정상(rate·손실 지표는 그대로 계산됨)이지만 도착 시각(`CLOCK_MONOTONIC`)과의 차가 약 −2.1 × 10^9 s라서 288개 전부 `clock_domain_err`다. 시간 축 지표(dt)는 **같은 시계 안의 차이**라서 시계 도메인이 달라도 계산되고, latency만 무의미해진다. 그래서 두 지표군을 따로 판정한다.

---

## 7. 어댑터 3 — SocketCAN (`vcan` + `cangen`)

### 7.1 설정과 결함 주입

`vcan`은 가상 CAN 인터페이스이고, `cangen -D i`는 payload를 증가하는 카운터로 채운다(Q4). CAN에서는 손실을 **세 가지 독립된 방법**으로 셀 수 있다: 시각 gap 추정, payload 카운터, 커널 소켓 drop 카운터(`SO_RXQ_OVFL`).

```sh
# run_can.sh — SocketCAN 어댑터: vcan + cangen, 깨끗한 조건 + 결함 주입
cd /mnt/sdq; O=/mnt/out; mkdir -p $O
modprobe vcan; ip link add dev vcan0 type vcan 2>/dev/null; ip link set up vcan0
gen() { cangen vcan0 -I 123 -L 8 -D i -g 10 "$@" & G=$!; }
echo "# cangen -D i payload check:"; timeout 1 candump -n 3 vcan0 & sleep 0.2; cangen vcan0 -I 123 -L 8 -D i -n 3 -g 10; wait
echo "===== CAN-A clean 100 Hz"
gen; python3 sdq.py can --dev vcan0 --rate 100 --secs 20 --hist --json $O/can_clean.json; kill $G
echo "===== CAN-B stress-ng load"
stress-ng --cpu 2 --timeout 22s -q &
gen; python3 sdq.py can --dev vcan0 --rate 100 --secs 20 --hist --json $O/can_stress.json; kill $G; wait
echo "===== CAN-C sender paused 1.5 s (SIGSTOP/SIGCONT) at t=8s"
gen; (sleep 8; kill -STOP $G; sleep 1.5; kill -CONT $G) &
python3 sdq.py can --dev vcan0 --rate 100 --secs 20 --hist --json $O/can_pause.json; kill $G; wait
echo "===== CAN-D small SO_RCVBUF + slow reader (30 ms/frame)"
gen; python3 sdq.py can --dev vcan0 --rate 100 --secs 20 --rcvbuf 1024 --slow 0.03 --json $O/can_slow.json; kill $G
echo "===== CAN-E second sender with the same CAN ID starts at t=8s"
gen; (sleep 8; cangen vcan0 -I 123 -L 8 -D i -g 10 -n 300) &
python3 sdq.py can --dev vcan0 --rate 100 --secs 20 --json $O/can_dupid.json; kill $G; wait
```

### 7.2 결과

```text
# cangen -D i payload check:
  vcan0  123   [8]  00 00 00 00 00 00 00 00
  vcan0  123   [8]  01 00 00 00 00 00 00 00
  vcan0  123   [8]  02 00 00 00 00 00 00 00
```

먼저 `cangen -D i`의 payload가 리틀엔디언 카운터(첫 바이트부터 00, 01, 02)라는 것을 `candump`로 확인했다. 어댑터는 이것을 `<Q`(리틀엔디언 u64)로 읽는다.

```text
===== CAN-A clean 100 Hz
n                  1648
rate_hz            82.65
rate_err_ppm       -173497
dt_p50_us          11481.5
dt_p99_us          20893.0
gaps               108
lost_by_ts         114
lost_by_seq        0
kernel_drops       0
lat_p50_us         1445.4
lat_p99_us         14454.4
===== CAN-B stress-ng load
n                  1552
rate_hz            78.116
dt_max_us          149400.5
lost_by_ts         235
lost_by_seq        0
kernel_drops       0
clock_domain_err   57
lat_p99_us         912010.8
lat_max_us         1494432.1
===== CAN-C sender paused 1.5 s (SIGSTOP/SIGCONT) at t=8s
n                  1680
dt_p50_us          10964.8
dt_max_us          1540462.9
gaps               7
lost_by_ts         160
lost_by_seq        0
===== CAN-D small SO_RCVBUF + slow reader (30 ms/frame)
n                  624
rate_hz            31.368
gaps               619
lost_by_ts         1228
lost_by_seq        1176
kernel_drops       1176
lat_p50_us         91201.1
===== CAN-E second sender with the same CAN ID starts at t=8s
n                  2032
dt_min_us          9.2
lost_by_seq        207663
out_of_order       292
ts_backwards       4
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" font-size="14" text-anchor="middle">CAN 손실을 세 방법으로 센 결과 (실측, 20 s, TCG)</text>
  <line x1="60" y1="250" x2="670" y2="250" stroke="currentColor"/>
  <line x1="60" y1="50" x2="60" y2="250" stroke="currentColor"/>
  <line x1="60" y1="250.0" x2="670" y2="250.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="254.0" font-size="12" text-anchor="end">0</text>
  <line x1="60" y1="200.0" x2="670" y2="200.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="204.0" font-size="12" text-anchor="end">350</text>
  <line x1="60" y1="150.0" x2="670" y2="150.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="154.0" font-size="12" text-anchor="end">700</text>
  <line x1="60" y1="100.0" x2="670" y2="100.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="104.0" font-size="12" text-anchor="end">1050</text>
  <line x1="60" y1="50.0" x2="670" y2="50.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="54" y="54.0" font-size="12" text-anchor="end">1400</text>
  <rect x="82.0" y="233.7" width="30" height="16.3" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="97.0" y="229.7" font-size="12" text-anchor="middle">114</text>
  <rect x="118.0" y="250.0" width="30" height="0.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="133.0" y="246.0" font-size="12" text-anchor="middle">0</text>
  <rect x="154.0" y="250.0" width="30" height="0.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="169.0" y="246.0" font-size="12" text-anchor="middle">0</text>
  <text x="136.2" y="268" font-size="12" text-anchor="middle">A 깨끗함</text>
  <rect x="234.5" y="216.4" width="30" height="33.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="249.5" y="212.4" font-size="12" text-anchor="middle">235</text>
  <rect x="270.5" y="250.0" width="30" height="0.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="285.5" y="246.0" font-size="12" text-anchor="middle">0</text>
  <rect x="306.5" y="250.0" width="30" height="0.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="321.5" y="246.0" font-size="12" text-anchor="middle">0</text>
  <text x="288.8" y="268" font-size="12" text-anchor="middle">B stress-ng</text>
  <rect x="387.0" y="227.1" width="30" height="22.9" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="402.0" y="223.1" font-size="12" text-anchor="middle">160</text>
  <rect x="423.0" y="250.0" width="30" height="0.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="438.0" y="246.0" font-size="12" text-anchor="middle">0</text>
  <rect x="459.0" y="250.0" width="30" height="0.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="474.0" y="246.0" font-size="12" text-anchor="middle">0</text>
  <text x="441.2" y="268" font-size="12" text-anchor="middle">C 송신 1.5 s 정지</text>
  <rect x="539.5" y="74.6" width="30" height="175.4" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="554.5" y="70.6" font-size="12" text-anchor="middle">1228</text>
  <rect x="575.5" y="82.0" width="30" height="168.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="590.5" y="78.0" font-size="12" text-anchor="middle">1176</text>
  <rect x="611.5" y="82.0" width="30" height="168.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="626.5" y="78.0" font-size="12" text-anchor="middle">1176</text>
  <text x="593.8" y="268" font-size="12" text-anchor="middle">D 작은 rcvbuf + 느린 리더</text>
  <rect x="90" y="296" width="12" height="12" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="108" y="307" font-size="12">시각 gap 추정</text>
  <rect x="280" y="296" width="12" height="12" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="298" y="307" font-size="12">payload 카운터</text>
  <rect x="470" y="296" width="12" height="12" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="488" y="307" font-size="12">커널 SO_RXQ_OVFL</text>
</svg>
```

그림 6 — CAN 실험 A~D에서 같은 손실을 세 방법으로 센 결과(실측). D에서 payload 카운터와 커널 drop 카운터가 정확히 1176으로 일치한다. A~C의 시각 기준 "손실"은 실제 손실이 아니다.

볼 것:

- **A(기준선)**: `cangen -g 10`인데 실제 rate는 82.7 Hz, dt 중앙값 11.5 ms다. 순번 손실 0, 커널 drop 0 — **프레임은 하나도 안 버려졌다.** `cangen`의 `-g`는 "보낸 뒤 10 ms 쉬기"(상대 sleep)라서 루프 오버헤드(TCG에서 1~2 ms)가 매 주기 누적된다. 시각 기준 "손실 114"는 **느린 송신자를 손실로 오해한 것**이다. 순번이 있으면 "rate가 낮다(송신 측 문제)"와 "손실이 있다(전달 경로 문제)"가 구분된다. 그래서 8절의 헬스 모니터용 송신기는 절대 마감 시각으로 보내는 `q7_gen.py`로 바꿨다.
- **B(부하)**: 손실은 여전히 0인데 latency p99가 912 ms, max가 1.49 s다. 소켓 수신 버퍼가 충분히 커서 버리지 않고 **쌓아 두었다**. "drop 0"이 "건강함"은 아니다. 또 latency 1 s 초과 57개가 `clock_domain_err`로 분류됐다 — 시계 도메인 판정 문턱(1 s)이 실제 지연과 겹친 **툴의 오분류**다. 시계 도메인은 "차이가 일정한 큰 상수인가"로 보는 편이 맞다(연습 아이디어).
- **C(송신기 1.5 s 정지)**: `dt_max 1540 ms`, 시각 기준 손실 160, 순번 손실 0. 송신기가 멈췄다가 **카운터를 이어서** 보냈으므로 빠진 프레임은 없다. 이것은 "손실"이 아니라 "stall"이다. 헬스 모니터 입장에서는 둘 다 bad 창이지만, 원인 분석에서는 전혀 다르다(센서가 멈춤 vs 경로가 버림).
- **D(작은 수신 버퍼 + 느린 리더)**: `SO_RCVBUF=1024`(커널이 두 배로 잡고 최솟값을 적용)이면 몇 프레임만 큐에 들어간다. 리더가 프레임마다 30 ms를 쓰면 나머지는 `__sock_queue_rcv_skb()`에서 버려진다. **payload 카운터 1176 = 커널 drop 1176**. 커널이 센 숫자와 데이터에서 추론한 숫자가 정확히 일치하므로, 이 손실은 전부 "내 소켓"에서 생겼다고 증명된다. 시각 추정(1228)은 송신자 느림까지 섞여 과대평가다.
- **E(같은 CAN ID 송신자 둘)**: 두 번째 `cangen`이 0부터 카운터를 보내면서 `out_of_order 292`, `lost_by_seq 207663`(의미 없는 큰 수), `dt_min 1.4 µs`(두 프레임이 거의 동시)가 나왔다. 순서가 뒤바뀌기 시작하면 **순번 기반 손실 숫자는 무효**다. 툴은 이 상황을 "ID 충돌·송신자 중복"으로 분류해야 한다. 실제 차량·로봇에서는 같은 ID를 두 노드가 보내는 설정 실수, 게이트웨이 재전송 루프가 이 패턴을 만든다. `ts_backwards 4`는 두 CPU에서 각각 보낸 프레임이 소켓 큐에 들어간 순서와 skb 시각 순서가 어긋난 것으로 보인다(추정, 확인 안 함).

### 7.3 세 어댑터 요약 — 결함별로 무엇이 울렸나

| 결함 주입 | 울린 지표 | 울리지 않은(울리면 안 되는) 지표 |
|---|---|---|
| IIO 트리거 주파수 변경 | rate 오차, dt 분포 두 봉우리, gap | (순번 없음) |
| IIO 버퍼 4 + 느린 리더 | gap, 시각 기준 손실, latency | `kernel_drops`(IIO는 셈 없음) |
| IIO `realtime` + 시계 스텝 | `ts_backwards`, `clock_domain_err`, rate 왜곡 | latency 히스토그램(버림) |
| V4L2 10 % 드롭 | 순번 손실 ≈ 10 % | `buf_flag_error` |
| V4L2 버퍼 2 + 느린 소비자 | 순번 손실, dt 2주기 봉우리, latency | — |
| V4L2 sequence wrap | 없음(정상 처리) | `out_of_order` |
| V4L2 타임스탬프 wrap | `clock_domain_err` 전부 | dt·손실 |
| CAN 송신 정지 | `dt_max`, 시각 gap | 순번 손실 |
| CAN 작은 rcvbuf | 순번 손실 = 커널 drop | — |
| CAN ID 충돌 | `out_of_order`, `ts_backwards` | (순번 손실은 무효) |

결함마다 **서로 다른 지표 조합**이 울린다. 이 표가 곧 진단 규칙이다: 지표 하나만 보고 "손실"이라고 말하지 않는다.

---

## 8. 헬스 모니터 — 장비 위에서 24시간 지키기

### 8.1 벤치 툴과 무엇이 다른가

`sdq.py`는 "20초 재고 리포트"다. 장비 위에서는 질문이 바뀐다: **"지금 이 순간 센서가 건강한가, 그리고 건강하지 않으면 누가 무엇을 하나."** 그래서 헬스 모니터는 세 가지를 더한다.

- **창(window) 판정**: 1초마다 그 1초의 샘플만 보고 good/bad를 낸다. 누적 통계는 "어제 한 번 있었던 일"을 영원히 끌고 다니므로 실시간 판정에 맞지 않다.
- **히스테리시스**: bad 한 번에 바로 FAULT를 내면 부하가 잠깐 튈 때마다 경보가 울린다(alarm flapping). 들어갈 때와 나올 때의 문턱을 다르게 둔다. 펌웨어의 온도 스로틀링(85 °C에서 걸고 80 °C에서 풂)과 같은 생각이다.
- **바깥 세계와의 연결**: systemd watchdog(살아 있음), 상태 문자열(`systemctl status`에 보임), 메트릭 파일(수집기가 가져감), journal 우선순위(경보 규칙이 걸림).

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="340" y="18" font-size="14" text-anchor="middle">헬스 상태 기계 (창 = 1 s, 히스테리시스)</text>
  <rect x="30" y="100" width="130" height="60" rx="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="95" y="126" font-size="14" text-anchor="middle">OK</text>
  <text x="95" y="146" font-size="12" text-anchor="middle">WATCHDOG=1</text>
  <rect x="275" y="100" width="130" height="60" rx="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="126" font-size="14" text-anchor="middle">DEGRADED</text>
  <text x="340" y="146" font-size="12" text-anchor="middle">WATCHDOG=1</text>
  <rect x="520" y="100" width="130" height="60" rx="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="585" y="126" font-size="14" text-anchor="middle">FAULT</text>
  <text x="585" y="146" font-size="12" text-anchor="middle">ping 중단</text>
  <path d="M160,115 L273,115" stroke="currentColor" fill="none" marker-end="url(#ah5)"/>
  <text x="216" y="106" font-size="12" text-anchor="middle">bad 1회</text>
  <path d="M275,148 L162,148" stroke="currentColor" fill="none" marker-end="url(#ah5)"/>
  <text x="216" y="170" font-size="12" text-anchor="middle">good 3회 연속</text>
  <path d="M405,130 L518,130" stroke="currentColor" fill="none" marker-end="url(#ah5)"/>
  <text x="462" y="121" font-size="12" text-anchor="middle">bad 3회 연속</text>
  <path d="M585,100 C585,40 95,40 95,98" stroke="currentColor" fill="none" marker-end="url(#ah5)"/>
  <text x="340" y="52" font-size="12" text-anchor="middle">good 5회 연속 (WatchdogSec보다 짧게 회복하면)</text>
  <path d="M585,160 L585,205" stroke="#d0564a" fill="none" marker-end="url(#ah5)"/>
  <rect x="440" y="208" width="230" height="66" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="555" y="228" font-size="12" text-anchor="middle">WatchdogSec=3s 지나면 systemd가</text>
  <text x="555" y="246" font-size="12" text-anchor="middle">SIGABRT → Restart=on-watchdog</text>
  <text x="555" y="264" font-size="12" text-anchor="middle">3회/60 s 넘으면 failed (경보)</text>
  <text x="20" y="214" font-size="12">bad 창 판정 (하나라도 해당하면 bad):</text>
  <text x="20" y="234" font-size="12">1. 샘플 수 &lt; 0.9 × 명목 rate</text>
  <text x="20" y="252" font-size="12">2. payload 카운터 gap (손실 1개 이상)</text>
  <text x="20" y="270" font-size="12">3. 창 안 최대 dt &gt; 4 T</text>
</svg>
```

그림 7 — 헬스 상태 기계. 들어가는 문턱(bad 1회, 3회 연속)과 나오는 문턱(good 3회, 5회 연속)이 다르다. FAULT에서는 일부러 watchdog ping을 멈춰서 systemd가 복구 동작(재시작)을 하게 한다.

### 8.2 watchdog을 "데이터 건강"에 묶을 것인가

L1 노트에서 본 `WatchdogSec=`는 원래 **프로세스가 멈췄는지(hang)**를 잡는 장치다. 여기서는 한 걸음 더 나가서 **"데이터가 건강할 때만 ping"** 하는 설계를 쓴다. 장단점이 분명하다.

| 설계 | 동작 | 장점 | 단점 |
|---|---|---|---|
| A. 항상 ping (liveness만) | 모니터가 살아 있으면 ping, 데이터 결함은 경보만 | 모니터 재시작이 데이터 문제를 못 고칠 때 무의미한 재시작이 없다 | 복구 동작을 따로 짜야 한다 |
| B. 건강할 때만 ping (이 노트) | FAULT가 WatchdogSec 넘게 지속되면 systemd가 재시작 | 재시작(= 소켓·장치 다시 열기, 드라이버 재바인드를 `ExecStartPre`로)이 표준 경로가 된다. 재시작 횟수가 곧 경보 지표 | 센서 자체가 죽었으면 재시작 루프. 그래서 `StartLimitBurst`로 끊고 `OnFailure=`로 경보 |

실무에서는 보통 B를 **센서 수집 데몬**에, A를 **모니터 자신**에 쓴다. 이 노트는 둘을 한 프로세스로 합쳐서 B를 보여 준다.

### 8.3 코드 `q7_health.py`

무엇을 하는 코드인가: CAN 어댑터를 그대로 쓰고(0.2 s 수신 타임아웃이라 데이터가 끊겨도 루프가 돈다), 1초마다 판정 → 상태 기계 → `sd_notify`(READY, STATUS, WATCHDOG) → Prometheus textfile → journal 로그를 한다. `sd_notify`는 libsystemd 없이 `$NOTIFY_SOCKET`에 데이터그램을 보내는 프로토콜 그대로다(L1 노트 2절).

```python
#!/usr/bin/env python3
# q7_health.py (1/2) — 1초 창마다 판정 → OK/DEGRADED/FAULT (히스테리시스) → sd_notify
import os, sys, socket, time
import numpy as np
import sdq_adapt as A

def sd_notify(msg):                                        # libsystemd 없이 프로토콜 그대로
    path = os.environ.get('NOTIFY_SOCKET')
    if not path:
        return
    if path[0] == '@':
        path = '\0' + path[1:]                             # abstract namespace
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as s:
        s.sendto(msg.encode(), path)

def log(prio, text):                                       # "<N>" 접두어 → journald 우선순위
    print('<%d>%s' % (prio, text), file=sys.stderr, flush=True)

def export(path, m):                                       # Prometheus textfile 형식, 원자적 교체
    tmp = path + '.tmp'
    with open(tmp, 'w') as f:
        for k, v in m.items():
            f.write('q7_%s{stream="vcan0"} %s\n' % (k, v))
    os.replace(tmp, path)

class Health:
    ENTER_FAULT, LEAVE_DEGRADED, LEAVE_FAULT = 3, 3, 5     # 연속 창 개수 (히스테리시스)

    def __init__(s):
        s.state, s.bad_run, s.good_run = 'OK', 0, 0

    def step(s, bad):
        s.bad_run, s.good_run = (s.bad_run + 1, 0) if bad else (0, s.good_run + 1)
        old = s.state
        if s.state == 'OK' and bad:
            s.state = 'DEGRADED'
        elif s.state == 'DEGRADED' and s.bad_run >= s.ENTER_FAULT:
            s.state = 'FAULT'
        elif s.state == 'DEGRADED' and s.good_run >= s.LEAVE_DEGRADED:
            s.state = 'OK'
        elif s.state == 'FAULT' and s.good_run >= s.LEAVE_FAULT:
            s.state = 'OK'
        return old != s.state
```

```python
# ---- q7_health.py (2/2) — 창 판정 규칙과 메인 루프 ----
def judge(ts, seq, rate):                                  # 창 하나 → (bad, 이유)
    n = len(ts)
    if n < 0.9 * rate:
        return True, 'rate %d/s < %d' % (n, 0.9 * rate)
    lost = int(np.clip(np.diff(seq) - 1, 0, None).sum())
    if lost:
        return True, 'lost %d frames (seq gap)' % lost
    dtmax = np.diff(ts).max() / 1e6
    if dtmax > 4e3 / rate:
        return True, 'dt_max %.1f ms' % dtmax
    return False, 'ok'

def main(ifname='vcan0', rate=100.0, metrics='/run/q7/metrics.prom'):
    os.makedirs(os.path.dirname(metrics), exist_ok=True)
    h, ts, seq, win_end, total_bad = Health(), [], [], time.monotonic() + 1, 0
    sd_notify('READY=1\nSTATUS=waiting for first window')
    log(6, 'q7-health start: %s nominal %.0f Hz, pid %d' % (ifname, rate, os.getpid()))
    for c in A.can(ifname, secs=1e9):
        if len(c['t']):
            ts.extend(c['t']); seq.extend(c['seq'])
        if time.monotonic() < win_end:
            continue
        win_end += 1
        bad, why = judge(np.array(ts, np.int64), np.array(seq, np.int64), rate)
        total_bad += bad
        if h.step(bad):
            log(4 if h.state != 'OK' else 5, 'state -> %s (%s)' % (h.state, why))
        export(metrics, dict(window_samples=len(ts), window_bad=int(bad),
                             bad_windows_total=total_bad,
                             state={'OK': 0, 'DEGRADED': 1, 'FAULT': 2}[h.state]))
        sd_notify('STATUS=%s n=%d %s' % (h.state, len(ts), why))
        if h.state != 'FAULT':
            sd_notify('WATCHDOG=1')                        # FAULT면 일부러 ping을 멈춘다
        ts, seq = [], []

if __name__ == '__main__':
    main()
```

볼 것:

- `log(4, ...)`: stderr에 `<4>`를 붙이면 journald가 그 줄을 priority 4(warning)로 저장한다(`SyslogLevelPrefix=yes`가 기본). 경보 규칙은 "q7-health의 warning 이상"으로 걸면 된다.
- `export()`는 임시 파일에 쓰고 `os.replace`로 바꾼다. 수집기(node_exporter textfile collector 같은 것)가 **반쯤 쓴 파일을 읽지 않게** 하는 원자적 교체다.
- FAULT에서는 `STATUS=`는 계속 보내고 `WATCHDOG=1`만 멈춘다. 그래서 죽기 직전까지 `systemctl status`에 이유가 보인다.

### 8.4 가짜 센서와 unit 파일

7.2절에서 본 것처럼 `cangen -g`는 상대 sleep이라 TCG에서 명목 rate를 못 낸다. 헬스 모니터 시험에는 **평균 rate가 밀리지 않는** 송신기가 필요해서, 절대 마감 시각으로 보내는 작은 송신기를 썼다(펌웨어의 "다음 tick = 이전 tick + 주기" 스케줄과 같다).

```python
#!/usr/bin/env python3
# q7_gen.py <hz> — 절대 마감 시각으로 CAN 프레임을 보내는 가짜 센서 (평균 rate가 밀리지 않음)
import sys, socket, struct, time

hz = float(sys.argv[1]) if len(sys.argv) > 1 else 100.0
s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
s.bind(('vcan0',))
period, n = int(1e9 / hz), 0
t_next = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
while True:
    s.send(struct.pack('=IB3xQ', 0x123, 8, n))             # can_frame: id, len, payload=카운터
    n += 1
    t_next += period                                       # 상대 sleep(cangen -g)과 달리 누적 오차 없음
    d = t_next - time.clock_gettime_ns(time.CLOCK_MONOTONIC)
    if d > 0:
        time.sleep(d / 1e9)
```

```text
# /etc/systemd/system/q7-cangen.service
[Unit]
Description=Q7 fake 100 Hz CAN sensor (q7_gen.py on vcan0)

[Service]
ExecStartPre=-/usr/sbin/modprobe vcan
ExecStartPre=-/usr/sbin/ip link add dev vcan0 type vcan
ExecStartPre=/usr/sbin/ip link set up vcan0
ExecStart=/usr/bin/python3 -B /mnt/sdq/q7_gen.py 100
```

```text
# /etc/systemd/system/q7-health.service
[Unit]
Description=Q7 sensor data health monitor (vcan0)
Requires=q7-cangen.service
After=q7-cangen.service
StartLimitIntervalSec=60
StartLimitBurst=3

[Service]
Type=notify
NotifyAccess=main
WorkingDirectory=/mnt/sdq
ExecStart=/usr/bin/python3 -B /mnt/sdq/q7_health.py
WatchdogSec=3s
Restart=on-watchdog
RestartSec=1s
LimitCORE=0
```

볼 것: `Type=notify`라서 systemd는 `READY=1`이 올 때까지 "activating" 상태로 기다린다. TCG에서 numpy import만 수 초가 걸리는데, 그 시간 동안 이 서비스에 의존하는 unit은 시작하지 않는다. `StartLimitIntervalSec=60`·`StartLimitBurst=3`은 60초 안에 세 번 넘게 재시작하면 포기(failed)하라는 뜻이다. 그때 `OnFailure=`로 경보 unit을 걸면 된다(여기서는 생략).

### 8.5 실험 — 센서를 얼려서 watchdog을 터뜨린다

시나리오: 서비스를 시작해 15초 건강하게 돌린 뒤, 송신기를 `SIGSTOP`으로 10초 얼리고(센서가 조용히 멈춘 상황), 풀어 준 뒤 25초 지켜본다.

```sh
# run_health.sh — health monitor를 systemd 서비스로 돌리고 센서를 멈춰 watchdog을 터뜨린다
cp /mnt/q7-cangen.service /mnt/q7-health.service /etc/systemd/system/
systemctl daemon-reload
systemctl start q7-health.service
sleep 15
echo "===== H1 healthy"; systemctl status q7-health --no-pager -n 0 | sed -n '1,6p'
cat /run/q7/metrics.prom
G=$(systemctl show -p MainPID --value q7-cangen)
echo "===== H2 freeze sensor (SIGSTOP q7_gen pid $G) for 10 s"
kill -STOP $G; sleep 10
systemctl show q7-health -p NRestarts -p ActiveState -p SubState -p StatusText
echo "===== H3 resume sensor"
kill -CONT $G; sleep 25
systemctl status q7-health --no-pager -n 0 | sed -n '1,6p'
cat /run/q7/metrics.prom
echo "===== journal"
journalctl -u q7-health -o short-monotonic --no-pager -b | grep -v "^-- "
```

```text
===== H1 healthy
● q7-health.service - Q7 sensor data health monitor (vcan0)
     Loaded: loaded (/etc/systemd/system/q7-health.service; static)
     Active: active (running) since Sun 2026-10-18 16:59:23 UTC; 15s ago
   Main PID: 577 (python3)
     Status: "OK n=96 ok"
      Tasks: 1 (limit: 863)
q7_window_samples{stream="vcan0"} 96
q7_window_bad{stream="vcan0"} 0
q7_bad_windows_total{stream="vcan0"} 0
q7_state{stream="vcan0"} 0
===== H2 freeze sensor (SIGSTOP q7_gen pid 576) for 10 s
StatusText=
NRestarts=1
ActiveState=activating
SubState=start
===== H3 resume sensor
● q7-health.service - Q7 sensor data health monitor (vcan0)
     Loaded: loaded (/etc/systemd/system/q7-health.service; static)
     Active: active (running) since Sun 2026-10-18 16:59:50 UTC; 24s ago
   Main PID: 598 (python3)
     Status: "OK n=112 ok"
      Tasks: 1 (limit: 863)
q7_window_samples{stream="vcan0"} 112
q7_window_bad{stream="vcan0"} 0
q7_bad_windows_total{stream="vcan0"} 0
q7_state{stream="vcan0"} 0
===== journal
[  629.372926] labdom systemd[1]: Starting q7-health.service - Q7 sensor data health monitor (vcan0)...
[  631.563161] labdom python3[577]: q7-health start: vcan0 nominal 100 Hz, pid 577
[  631.586414] labdom systemd[1]: Started q7-health.service - Q7 sensor data health monitor (vcan0).
[  647.644106] labdom python3[577]: state -> DEGRADED (rate 32/s < 90)
[  649.661755] labdom python3[577]: state -> FAULT (rate 0/s < 90)
[  651.679962] labdom systemd[1]: q7-health.service: Watchdog timeout (limit 3s)!
[  651.696459] labdom systemd[1]: q7-health.service: Killing process 577 (python3) with signal SIGABRT.
[  654.731581] labdom systemd[1]: q7-health.service: Main process exited, code=dumped, status=6/ABRT
[  654.763235] labdom systemd[1]: q7-health.service: Failed with result 'watchdog'.
[  654.766625] labdom systemd[1]: q7-health.service: Consumed 3.271s CPU time, 18.6M memory peak, 0B memory swap peak.
[  655.912631] labdom systemd[1]: q7-health.service: Scheduled restart job, restart counter is at 1.
[  655.966187] labdom systemd[1]: Starting q7-health.service - Q7 sensor data health monitor (vcan0)...
[  658.196532] labdom python3[598]: q7-health start: vcan0 nominal 100 Hz, pid 598
[  658.211079] labdom systemd[1]: Started q7-health.service - Q7 sensor data health monitor (vcan0).
```

journal 타임라인을 읽어 보면(괄호 안은 부팅 후 초):

1. 631.6 READY → `Started`. 15초 동안 상태 OK, metrics `q7_state 0`, 창 샘플 96개(명목 100, 문턱 90 이상).
2. 647.6 송신기 정지 직후 창의 샘플이 32개 → **DEGRADED**. 649.7 샘플 0개가 이어지자(bad 3창 연속) → **FAULT**, 이때부터 ping 중단.
3. 651.7 마지막 ping 후 3초 → `Watchdog timeout (limit 3s)!` → SIGABRT. 654.7 `code=dumped, status=6/ABRT`, `Failed with result 'watchdog'`.
4. 655.9 `RestartSec=1s` 뒤 재시작(`restart counter is at 1`), 658.2 새 인스턴스 READY. 그 사이 송신기가 풀려서 새 인스턴스는 바로 OK로 돌았고, H3 시점 `Status: "OK n=112 ok"`, `NRestarts=1`.

볼 것:

- H2 시점의 `ActiveState=activating SubState=start`, `StatusText=`(빈 값): 재시작된 인스턴스가 numpy를 import하는 중이다. 재시작 비용(TCG에서 약 3초)이 곧 **감시 공백**이다. 실제 장비에서는 모니터를 가볍게(C, 또는 import를 최소화) 만들어 공백을 줄인다.
- `LimitCORE=0`을 줬는데도 `code=dumped`로 나왔다. 이 rootfs의 `core_pattern`이 파이프 핸들러(systemd-coredump 등)라서 RLIMIT_CORE와 다르게 처리되는 것으로 보인다(추정, 확인 안 함). 처음 실행(`LimitCORE` 없음)에서는 SIGABRT 후 프로세스가 끝나기까지 9초가 걸렸고, 이번에는 3초였다.
- 그 처음 실행에서는 송신기를 `cangen -g 5`(명목 200 Hz)로 뒀는데, 모니터가 시작하자마자 `state -> DEGRADED (rate 112/s < 180)` → FAULT → watchdog 재시작을 반복했다. 송신기가 TCG에서 200 Hz를 못 낸 것이다. **헬스 모니터는 정확했고, 틀린 것은 시험 장비(가짜 센서)**였다. 그래서 8.4절의 절대 마감 송신기로 바꿨다.
- metrics 파일은 Prometheus textfile 형식이다. 장비에서는 node_exporter의 textfile collector가 이 디렉터리를 읽거나, 자체 수집기가 MQTT 등으로 올린다(가이드).

---

## 9. 데이터 품질 회귀를 CI에 넣기

P6 노트의 테스트 피라미드에 이 툴을 끼우면 두 층이 된다.

- **단위 테스트(모든 커밋)**: 2.6절의 `test_synth.py`처럼 정답을 아는 합성 스트림으로 **검출기 자체**를 검증한다. 판정 규칙을 고친 커밋이 gap 판정을 망가뜨리면 여기서 걸린다. HW가 필요 없다.
- **HIL 테스트(야간·릴리스)**: 실제 보드(또는 이 노트처럼 QEMU)에서 고정 시나리오(idle 60 s, 부하 60 s)로 리포트 JSON을 만들고, **스펙 파일**과 비교해 위반이 있으면 실패로 끝낸다. 커널·드라이버·BSP 업그레이드(P5)에서 "IMU jitter가 조용히 두 배가 됐다"를 잡는 곳이다.

게이트는 단순하다: 키마다 최대 허용값.

```python
#!/usr/bin/env python3
# sdq_check.py — CI 게이트: 리포트 JSON을 스펙(키 → 최대 허용값)과 비교, 위반 시 exit 1
import sys, json

SPEC = {                                                   # 예: 100 Hz IMU 스트림 계약
    'rate_err_ppm': 2000, 'dt_p99_us': 15000, 'dt_max_us': 50000,
    'lost_by_ts': 0, 'ts_backwards': 0, 'clock_domain_err': 0, 'nan': 0,
}

def main(path):
    r, fails = json.load(open(path)), []
    for k, lim in SPEC.items():
        v = abs(r[k]) if isinstance(r[k], (int, float)) else r[k]
        status = 'PASS' if v is not None and v <= lim else 'FAIL'
        print('%-4s %-18s %12s  (limit %s)' % (status, k, r[k], lim))
        if status == 'FAIL':
            fails.append(k)
    print('RESULT:', 'FAIL ' + ','.join(fails) if fails else 'PASS')
    return 1 if fails else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
```

스펙을 지키는 경우를 먼저 확인한다. 깨끗한 100 Hz 합성 스트림(5분, jitter 200 µs, latency 2 ms)의 리포트를 만든다.

```python
#!/usr/bin/env python3
# ci_synth.py out.json — 깨끗한 100 Hz 합성 스트림의 리포트 (게이트가 PASS를 내는지 확인용)
import sys, json
import numpy as np
import sdq_stats as S

rng = np.random.default_rng(1)
k = np.arange(30_000, dtype=np.int64)                      # 100 Hz × 300 s
t = k * 10_000_000 + rng.normal(0, 200_000, len(k)).astype(np.int64)   # 200 us 지터
st = S.Stream('synth-clean', 100)
for i in range(0, len(t), 500):
    st.update(t[i:i + 500], t[i:i + 500] + 2_000_000, k[i:i + 500], rng.normal(0, 1, 500))
json.dump(st.summary(), open(sys.argv[1], 'w'))
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.elinux_scratch/Q7/sdq && /var/tmp/Q7/venv/bin/python -B ci_synth.py /tmp/Q7/synth.json && /var/tmp/Q7/venv/bin/python -B sdq_check.py /tmp/Q7/synth.json; echo "exit=$?"; /var/tmp/Q7/venv/bin/python -B sdq_check.py /var/tmp/Q7/res/final/out/iio_clean.json; echo "exit=$?"'
```

```text
PASS rate_err_ppm                  0  (limit 2000)
PASS dt_p99_us               10964.8  (limit 15000)
PASS dt_max_us               11178.2  (limit 50000)
PASS lost_by_ts                    0  (limit 0)
PASS ts_backwards                  0  (limit 0)
PASS clock_domain_err              0  (limit 0)
PASS nan                           0  (limit 0)
RESULT: PASS
exit=0
FAIL rate_err_ppm             -21906  (limit 2000)
FAIL dt_p99_us               21877.6  (limit 15000)
PASS dt_max_us               42685.2  (limit 50000)
FAIL lost_by_ts                  117  (limit 0)
PASS ts_backwards                  0  (limit 0)
PASS clock_domain_err              0  (limit 0)
PASS nan                           0  (limit 0)
RESULT: FAIL rate_err_ppm,dt_p99_us,lost_by_ts
exit=1
```

출력에서 볼 것:

- 합성 스트림은 PASS, exit 0. TCG에서 잰 IIO 기준선(5.2절)은 rate·p99·손실 세 항목이 FAIL, exit 1. CI 러너는 exit code만 보면 된다.
- 같은 스펙을 TCG 결과에 대면 늘 실패한다. **스펙은 타깃 HW 기준**이고, VM에서 도는 CI는 "툴이 동작하는가"와 "결함을 넣으면 FAIL이 나는가"만 확인한다. VM용으로 스펙을 느슨하게 만들면 실제 회귀를 놓친다.
- 실무에서는 스펙을 센서별 YAML로 빼고(예: `imu_bmi088.yaml: rate_err_ppm 500, dt_p99_us 1100`), 리포트 JSON을 CI 산출물로 보관해 추세 그래프를 그린다. 한 번의 FAIL보다 **p99가 릴리스마다 조금씩 커지는 추세**가 더 중요한 신호인 경우가 많다.
- LAVA·Jenkins·GitHub Actions의 self-hosted runner 등 어디에 붙이든 형태는 같다: 보드 부팅 → `sdq.py ... --json` → `sdq_check.py` → exit code + JSON 업로드. P6 노트의 HIL 구성과 그대로 이어진다(가이드, 이 노트에서 CI 서버는 구성하지 않았다).

---

## 10. 센서 사이의 품질 — 일관성과 sync 오차

지금까지는 스트림 하나만 봤다. 퓨전에서는 **두 센서가 서로 맞는가**가 더 자주 문제다. 툴이 자동으로 잡기 어렵고 도메인 지식이 필요한 영역이라, 방법만 정리한다(실행 안 함, 동기화 구현 자체는 Q3).

| 검사 | 방법 | 판정 예 |
|---|---|---|
| rate 비율 | 두 스트림의 `f_meas` 비가 명목 비와 같은가 | IMU 200 Hz : 카메라 30 Hz = 6.667. 비율이 시간에 따라 변하면 **두 센서가 다른 발진기**를 쓰고 있고, 그 차이(ppm)만큼 시각이 벌어진다 |
| sync 오차 | 같은 물리 사건이 두 센서에 찍힌 시각의 차 | 카메라 strobe(노출 시작) GPIO를 IIO/PPS로 같이 찍어서 `t_cam - t_strobe` 분포를 본다(Q3의 HW trigger) |
| 잔차 일관성 | 한 센서로 다른 센서를 예측한 오차 | 휠 오도메트리 속도 vs IMU 가속 적분, GNSS 속도 vs 휠 속도. 잔차의 이동 RMS가 문턱을 넘으면 한쪽이 의심 |
| 상호상관 지연 | 두 신호의 상호상관 최대 위치 | 로봇을 흔들어 IMU 각속도와 카메라 광류(optical flow)의 상관 최대 지연 = 카메라 타임스탬프 오프셋. Kalibr 같은 캘리브레이션 도구가 하는 일 |
| 시계 도메인 일치 | 모든 스트림이 같은 시계 ID를 쓰는가 | 장치마다 `current_timestamp_clock`(IIO), `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`(V4L2), 소켓 타임스탬프(REALTIME)를 확인하고 하나로 변환 |

마지막 줄이 특히 중요하다. 이 노트의 세 어댑터만 해도 기본 시계가 셋 다 다르다. IIO는 기본 `CLOCK_REALTIME`(설정으로 바꿀 수 있음), V4L2(vivid, 대부분의 드라이버)는 `CLOCK_MONOTONIC`, 소켓 `SO_TIMESTAMPNS`는 `CLOCK_REALTIME`이다. 퓨전 노드에 넘기기 전에 **하나의 시계로 변환하는 계층**이 반드시 있어야 하고, 품질 툴은 그 계층이 맞게 동작하는지(시계 도메인 오류 = 0)를 확인한다. 장치 사이의 공통 시계(PTP의 PHC, PPS)는 Q3에서 다룬다.

---

## 11. 커널 안에서는 무슨 일이 일어나나

툴이 본 숫자를 해석하려면 **커널이 어디서 시각을 찍고, 어디서 데이터를 버리는지** 알아야 한다. 세 어댑터의 경로를 6.18 소스로 따라가 본다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="18" font-size="14" text-anchor="middle">세 경로에서 시각을 찍는 곳(T)과 조용히 버리는 곳(D)</text>
  <text x="10" y="50" font-size="13">IIO</text>
  <rect x="50" y="34" width="120" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="52" font-size="12" text-anchor="middle">hrtimer 만료</text>
  <rect x="180" y="34" width="150" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="52" font-size="12" text-anchor="middle">pollfunc 스레드 (T)</text>
  <rect x="340" y="34" width="170" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="425" y="52" font-size="12" text-anchor="middle">kfifo_in 실패 (D)</text>
  <rect x="520" y="34" width="150" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="595" y="52" font-size="12" text-anchor="middle">read(/dev/iio)</text>
  <text x="50" y="80" font-size="12">dummy는 스레드 안에서 iio_get_time_ns() → 스케줄 지연이 타임스탬프 jitter가 된다</text>
  <text x="50" y="98" font-size="12">kfifo 가득 → iio_store_to_kfifo()가 -EBUSY, 드라이버는 반환값을 무시 → 흔적 없음</text>
  <text x="10" y="140" font-size="13">V4L2</text>
  <rect x="50" y="124" width="120" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="142" font-size="12" text-anchor="middle">vivid kthread</text>
  <rect x="180" y="124" width="150" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="255" y="142" font-size="12" text-anchor="middle">빈 큐면 skip (D)</text>
  <rect x="340" y="124" width="170" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="425" y="142" font-size="12" text-anchor="middle">ktime_get_ns (T)</text>
  <rect x="520" y="124" width="150" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="595" y="142" font-size="12" text-anchor="middle">VIDIOC_DQBUF</text>
  <text x="50" y="170" font-size="12">sequence = 스트림 시작 후 경과 시간 / 프레임 주기 → 버린 프레임만큼 seq가 건너뛴다</text>
  <text x="50" y="188" font-size="12">버퍼가 없거나 perc_dropped에 걸리면 채우지 않고 넘어감 (vivid_thread_vid_cap_tick)</text>
  <text x="10" y="230" font-size="13">CAN</text>
  <rect x="50" y="214" width="120" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="110" y="232" font-size="12" text-anchor="middle">vcan_rx</text>
  <rect x="180" y="214" width="150" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="255" y="232" font-size="12" text-anchor="middle">netif_rx 시각 (T)</text>
  <rect x="340" y="214" width="170" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="425" y="232" font-size="12" text-anchor="middle">rcvbuf 초과 (D, 셈)</text>
  <rect x="520" y="214" width="150" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="595" y="232" font-size="12" text-anchor="middle">recvmsg + cmsg</text>
  <text x="50" y="260" font-size="12">raw_rcv → sock_queue_rcv_skb: sk_rmem_alloc ≥ sk_rcvbuf 이면 sk_drops_inc()</text>
  <text x="50" y="278" font-size="12">SO_RXQ_OVFL을 켜면 그 누적값이 다음 수신 프레임의 cmsg로 온다 (0이 아닐 때만)</text>
  <rect x="10" y="296" width="660" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="315" font-size="12" text-anchor="middle">D 세 곳 중 커널이 "몇 개 버렸다"를 알려 주는 곳은 소켓 하나뿐이다 → 나머지는 순번·시각으로 증명</text>
</svg>
```

그림 8 — IIO·V4L2·SocketCAN에서 타임스탬프를 찍는 지점(T)과 데이터를 버리는 지점(D). 6.18.55 소스 기준.

경로별로 소스 위치를 짚는다.

- **IIO**: `drivers/iio/buffer/kfifo_buf.c`의 `iio_store_to_kfifo()`는 `kfifo_in()`이 1을 못 넣으면 `-EBUSY`를 돌려준다. 이 값은 `iio_push_to_buffer()` → `iio_push_to_buffers()`로 올라가지만 `iio_simple_dummy_trigger_h()`는 반환값을 보지 않는다. 대부분의 업스트림 드라이버도 그렇다. **IIO 버퍼 overflow는 카운터가 없다.** 그래서 IIO 스트림의 손실은 타임스탬프 gap으로만 보인다(I2 노트의 `i2_imu`는 칩 샘플 번호를 채널로 노출해서 이걸 해결했다). 타임스탬프는 `iio_get_time_ns()`가 `current_timestamp_clock`에 따라 `ktime_get_real_ns()`/`ktime_get_ns()`/... 중 하나를 고른다(`industrialio-core.c`).
- **V4L2(vivid)**: `vivid-kthread-cap.c`에서 `cap_seq_count`는 "스트림 시작 후 jiffies 경과 ÷ 프레임 주기"로 계산된다. 스레드가 늦게 깨면 `dropped_bufs`만큼 sequence가 건너뛴다. `perc_dropped_buffers`나 빈 큐(`vid_cap_active` 비어 있음)면 `goto update_mv`로 버퍼를 채우지 않는다. 실제 CSI 수신 드라이버도 "빈 버퍼가 없으면 프레임을 버리고 sequence는 올린다"가 관례라서, **V4L2에서는 sequence gap이 곧 드롭**이다. 타임스탬프는 `vb2_buf.timestamp = ktime_get_ns()`이고 큐의 `timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`(vivid-core.c)이다.
- **SocketCAN**: `vcan_rx()` → `netif_rx()`에서 `net_timestamp_check()`가 skb 시각을 찍는다(누군가 타임스탬프를 요청한 경우). `can_rcv()` → `raw_rcv()` → `sock_queue_rcv_skb_reason()`이고, `__sock_queue_rcv_skb()`가 `sk_rmem_alloc >= sk_rcvbuf`면 `sk_drops_inc()` 후 버린다(`net/core/sock.c`). `sock_recv_drops()`(`net/socket.c`)가 `SOCK_RXQ_OVFL`이 켜져 있고 dropcount가 0이 아닐 때 cmsg를 붙인다.

드라이버를 쓸 때 이 지식이 다시 나오는 곳:

- IIO 드라이버(I2): 타임스탬프를 **hard IRQ 쪽**(`iio_pollfunc_store_time`)에서 찍을지, 스레드에서 찍을지가 jitter를 결정한다. FIFO가 있는 IMU면 샘플 번호나 FIFO 카운트를 채널로 노출하는 것이 품질 툴을 위한 배려다.
- V4L2 드라이버(I5, Q2): `vb2_buffer_done()` 전에 `sequence`를 **버린 프레임까지 세어** 채워야 한다. 에러 프레임은 `VB2_BUF_STATE_ERROR`로 돌려서 유저가 `V4L2_BUF_FLAG_ERROR`를 보게 한다.
- 네트워크·CAN 드라이버(I6, I10): 수신 시각은 가능하면 HW 타임스탬프(`skb_hwtstamps`)로, 드롭은 `netdev` 통계(`rx_dropped`, `rx_over_errors`)로 노출한다. `ip -s link show can0`가 그 숫자다(Q4).

---

## 12. Embedded 실무에서 다시 보기

### 12.1 장면 1 — 새 IMU 보드 bring-up의 "끝" 판정

P1 노트의 bring-up 순서에서 드라이버가 probe되고 `iio_info`에 채널이 보이면 끝난 것처럼 느껴진다. 실제로는 여기서 품질 측정을 한 번 돌려야 "됐다"고 말할 수 있다. 예를 들어 센서 키트의 BMI088을 1 kHz로 설정했다면:

- `sdq.py iio --rate 1000 --secs 600`을 idle에서 한 번, `stress-ng --cpu N`과 카메라 스트리밍을 같이 돌리면서 한 번.
- 판정: rate 오차가 데이터시트 ODR 허용치 안, `lost_by_ts = 0`, jitter p99 < 주기의 10 %, `ts_backwards = 0`.
- 이 결과 JSON을 bring-up 보고서에 붙인다. 숫자가 없는 "잘 됩니다"는 HW 팀과 인지 팀 둘 다 믿지 않는다.

### 12.2 장면 2 — 카메라 드롭이 "가끔" 난다는 버그 리포트

인지 팀이 "SLAM이 가끔 튄다"고 한다. 순서는 이렇다.

1. 카메라 노드 앞단에 V4L2 어댑터를 붙여 sequence gap을 센다. gap이 있으면 드라이버/버퍼 문제, 없으면 그 뒤(ROS 2 전송, Q6)의 문제다.
2. gap이 있으면 `--nbufs`를 바꿔 본다. 버퍼 수를 늘려 사라지면 **소비자가 늦는 것**(6.3절 C와 같은 상황)이고, 그대로면 **드라이버·CSI 수신기 쪽 드롭**(Q2: CSI 에러 카운터, `media-ctl` 링크 상태)이다.
3. 같은 시간대의 CPU 부하·온도(I4: thermal throttling)와 상관을 본다. 헬스 모니터의 metrics가 시계열로 쌓여 있으면 이 단계가 몇 분 만에 끝난다.

### 12.3 장면 3 — CAN 모터 컨트롤러의 rolling counter

로봇 관절 컨트롤러(MCU, O3 노트)가 500 Hz로 상태 프레임을 보낸다. 프로토콜에 4비트 counter + CRC8이 있다. Linux 쪽 수집 데몬은:

- `seq_mod=16`으로 손실·중복을 세고, 16개 이상 연속 손실은 시각 gap으로 잡는다.
- `SO_RXQ_OVFL`로 **Linux 소켓이 버린 것**과 counter로 본 **버스·MCU 쪽 손실**을 구분한다. 소켓 drop이 0인데 counter gap이 있으면 원인은 Linux 바깥(버스 에러, MCU 과부하)이다. `ip -s -d link show can0`의 bus-error·restart 카운터를 같이 본다(Q4).
- 헬스 모니터가 FAULT면 제어 루프에 "센서 신뢰 불가"를 알려 안전 정지로 보낸다. 국방·차량 로봇에서는 이 경로가 기능 안전 요구사항이 된다.

### 12.4 장면 4 — 생산 라인과 필드

생산 테스트(P6)에서는 장비마다 60초 품질 측정을 돌려 JSON을 남기고 `sdq_check`의 스펙으로 합격·불합격을 낸다. 필드에서는 헬스 모니터 metrics를 원격 수집해서 **장비 집단 통계**(어떤 로트의 IMU가 rate 오차가 크다, 어떤 펌웨어 버전 이후 카메라 드롭이 늘었다)를 본다. SSD 양산에서 SMART 로그를 모아 불량 로트를 찾던 것과 같은 일이다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| IIO 버퍼 길이를 기본값(2)으로 둠 | 부하가 조금만 있어도 gap이 수백 개, 손실이 샘플의 절반 | kfifo가 2 scan뿐이라 리더가 한 번만 늦어도 넘침, 흔적 없이 drop | `buffer/length`를 최소 "최대 리더 지연 × rate × 2"로, watermark 설정 |
| IIO 타임스탬프 시계를 안 바꿈 | latency가 이상한 큰 값, 시계 스텝 때 dt 음수 | 기본 `CLOCK_REALTIME`, NTP·timesyncd가 시각을 바꿈 | `current_timestamp_clock`을 `monotonic`으로, 시스템 전체가 같은 시계를 쓰게 |
| 평균 dt만 리포트 | "평균 10.0 ms인데 퓨전이 튄다" | 드문 긴 gap이 평균에 묻힘 | p99, max, gap 개수를 같이 리포트 |
| seq wrap을 처리하지 않음 | wrap 순간 손실 40억 개 또는 순서 뒤바뀜 1개 | `__u32` sequence, 4비트 counter를 그냥 뺌 | 차이를 `mod 2^n`으로 `(-2^(n-1), 2^(n-1)]`에 접기 |
| 손실을 커널 에러로만 판단 | "에러 로그 없음 = 손실 없음"이라고 보고 | kfifo, vb2 큐, 소켓 모두 조용히 버림 | 순번·시각으로 증명, 소켓은 `SO_RXQ_OVFL` |
| 측정 툴 자신이 느림 | 툴을 붙이면 드롭이 생기고 떼면 사라짐 | 샘플마다 Python 루프, print, 동기 write | chunk 벡터 처리, 버퍼링된 로그, 툴 CPU를 리포트에 포함 |
| 히스테리시스 없는 경보 | 부하 순간마다 경보가 울렸다 꺼짐, 아무도 경보를 안 봄 | 창 1개로 상태 전환 | 들어가고 나오는 문턱 분리, 연속 N창 |
| watchdog ping을 별도 타이머 스레드에서 보냄 | 메인 루프가 멈춰도 systemd가 모름 | ping이 실제 작업 경로와 분리됨 | ping은 데이터 처리 루프 안에서, 판정 직후에 |
| 테스트용 VM 숫자를 실제 HW 숫자로 보고 | 리뷰에서 숫자가 재현 안 됨 | TCG, 공유 호스트, 다른 시계원 | 절대값은 타깃 HW에서, VM은 툴 검증·상대 비교용으로만 |

---

## 14. 면접에서 이렇게 말한다

**Q.** "How would you verify that an IMU really delivers 1 kHz data without drops?"

**A.** 버퍼에서 타임스탬프를 오래(수 분~수 시간) 읽어서 세 가지를 본다. 끝점으로 계산한 rate의 ppm 오차, 간격 분포의 p50/p99/max, 그리고 1.5주기를 넘는 gap 개수와 그로 추정한 손실. 센서가 샘플 번호나 FIFO 카운트를 주면 그걸로 손실을 정확히 센다. idle과 부하 상태를 둘 다 측정하고, IIO는 kfifo overflow를 알려 주지 않으니 "에러가 없다"를 증거로 쓰지 않는다.

> I'd stream the buffered timestamps for minutes, not seconds, and report three things: rate error in ppm from the endpoints, the period distribution — p50, p99 and max, not just the mean — and the number of gaps over 1.5 periods with the implied lost samples. If the sensor exposes a sample counter, I use it for exact loss counting. I run it idle and under CPU and I/O load, because IIO's kfifo drops silently when full, so "no errors in dmesg" proves nothing.

**꼬리 질문**: "타임스탬프는 어디서 찍히나?" → hard IRQ에서 `iio_pollfunc_store_time`으로 찍으면 스레드 스케줄 지연이 빠진다. FIFO 배치면 마지막 샘플 시각에서 ODR로 역산한다(I2).

**Q.** "What does 'data quality' mean for a sensor platform? Give concrete metrics."

**A.** 시간 축(rate 오차, jitter, gap·손실, 중복, 순서, 타임스탬프 단조성, 시계 도메인, 지연), 값 축(NaN, 범위, stuck, 포화), 센서 사이(sync 오차, 잔차 일관성)로 나눠 말한다. 각각 검출 공식이 있다고 덧붙인다.

> I split it into three groups. Timing: rate accuracy, jitter, gaps and drops, duplicates, ordering, timestamp monotonicity, clock-domain consistency and latency. Values: NaN, out-of-range, stuck or saturated readings. Cross-sensor: synchronization error and physical consistency, like wheel odometry versus integrated IMU. Each one maps to a concrete detector on the timestamp, sequence or value stream.

**Q.** "How do you compute p99 latency on a device running for weeks without unbounded memory?"

**A.** 로그 간격 히스토그램을 쓴다. 경계를 고정하고 칸 개수만 세면 메모리가 고정되고, percentile은 누적 개수로 칸을 찾아 상단 경계로 답한다. 칸 폭이 오차 한계다. 평균·분산은 Welford로 누적한다.

> A fixed log-spaced histogram — say fifty bins per decade from a microsecond to ten seconds. That's a few kilobytes forever. Percentiles come from the cumulative counts, with the bin width as the error bound, about five percent here. Mean and variance use Welford's online update, which is numerically stable. It's the same idea as HdrHistogram or a Prometheus histogram.

**Q.** "Your camera pipeline drops frames occasionally. How do you find out where?"

**A.** V4L2 `sequence` gap으로 드라이버 단계 드롭인지 먼저 가른다. gap이 있으면 버퍼 수를 늘려 소비자 지연인지 드라이버·수신기 드롭인지 구분하고, 없으면 그 뒤 전송(ROS 2 등)을 본다. 부하·온도와의 상관을 metrics 시계열로 확인한다.

> First, split the pipeline at the V4L2 boundary: dequeued buffers carry a sequence number, and the convention is that dropped frames still advance it. If there are sequence gaps, I vary the number of queued buffers — if more buffers make it go away, the consumer is too slow; if not, it's the driver or CSI receiver. If there are no gaps, the loss is downstream, in the middleware. Then I correlate with CPU load and temperature from the health metrics.

**Q.** "How would you build a health monitor for sensors on a robot, and what happens when it detects a fault?"

**A.** 1초 창 판정 + 히스테리시스 상태 기계(OK/DEGRADED/FAULT), systemd `Type=notify` + `WatchdogSec`, FAULT에서는 ping을 멈춰 재시작 경로를 타게 하고, `StartLimitBurst`로 재시작 루프를 끊고 경보로 넘긴다. metrics와 journal priority로 바깥에 알린다. 제어 쪽에는 센서 신뢰 상태를 넘겨 안전 정지 판단에 쓰게 한다.

> A daemon that judges one-second windows — rate, sequence gaps, max interval — and drives a state machine with hysteresis so it doesn't flap. It runs as a systemd notify service with a watchdog; it only pets the watchdog while data is healthy, so a persistent fault triggers the standard restart path, and a start limit turns a restart loop into a failed unit that raises an alert. It also exports metrics and logs at warning priority, and publishes a sensor-trust flag so the controller can decide on a safe stop.

**Q.** "Two sensors' timestamps disagree by a few milliseconds that slowly grow. What's going on?"

**A.** 서로 다른 발진기로 시각을 찍고 있거나, 서로 다른 시계 도메인을 변환 없이 섞은 것이다. 천천히 커지는 차이는 ppm 차이(주파수 오차)이고, 계단식 점프는 시계 스텝(NTP)이다. 공통 시계(PTP PHC, PPS)에 묶거나 변환 계층을 둔다. Allan deviation으로 어떤 시간 척도에서 떠도는지 본다.

> A slowly growing offset is a frequency difference — the two timestamp sources run on different oscillators, so it grows at the ppm difference. A sudden jump would instead be a clock step, like NTP adjusting CLOCK_REALTIME. The fix is a common timebase — PTP hardware clock or PPS discipline — or an explicit conversion layer, and I'd check stability over time scales with an Allan deviation plot.

**Q.** "How do you keep data-quality from regressing release to release?"

**A.** 품질 리포트를 CI 산출물로 만든다. 합성 스트림 단위 테스트로 검출기 자체를 지키고, HIL 장비에서 고정 시나리오(idle, 부하)로 리포트를 만들어 스펙 파일과 비교해 실패면 빌드를 막는다. 추세는 대시보드에 쌓는다(P6).

> Treat the quality report as a CI artifact. Unit tests with synthetic streams where the right answer is known protect the detectors themselves. On hardware-in-the-loop rigs, fixed scenarios — idle and loaded — produce a JSON report that's checked against a spec file; any violation fails the pipeline. Trends go to a dashboard so slow drifts are visible before they cross a limit.

---

## 15. 직접 해보기

1. **개념**: 200 Hz 스트림에서 첫 샘플과 마지막 샘플 사이가 9.995 s이고, 받은 샘플은 1,992개, gap이 정확히 2번(각 25 ms)이었다. 시각 기준 손실 수와, 툴이 낸 `rate_hz`가 왜 명목보다 낮게 보이는지 계산하라. 정답/힌트: gap 하나에 `round(25/5) - 1 = 4`개, 합 8개. `(1992 - 1) / 9.995 = 199.2 Hz`(−4,000 ppm)처럼 보이지만 `(1992 + 8 - 1) / 9.995 = 200.0 Hz`다. 발진기는 정상이고 손실이 rate를 깎은 것이다 — 이 툴의 `rate_err_ppm`은 손실이 0일 때만 발진기 오차로 읽는다.
2. **코드**: `sdq_stats.Stream`에 **saturation** 검사(값이 `lo` 또는 `hi`와 정확히 같은 샘플이 연속 N개)를 추가하고, `test_synth.py`에 정답을 아는 포화 구간을 넣어 검증하라. 정답/힌트: `_values`에서 `v == s.lo` 또는 `v == s.hi`인 bool 배열에 stuck과 같은 연속 길이 계산을 재사용한다.
3. **코드**: 4비트 rolling counter(`seq_mod=16`)로 20개 연속 손실이 났을 때 `lost_by_seq`가 얼마로 나오는지 합성 테스트로 확인하고, 시각 gap 검사가 이를 어떻게 보완하는지 보여라. 정답/힌트: 순번 차이 21이 mod 16으로 접혀 5가 되므로 손실 4개로 잘못 센다. `lost_by_ts`는 20을 낸다.
4. **관찰**: rootfs-dom 게스트에서 IIO 버퍼 길이를 4, 16, 64, 256으로 바꿔 `--slow 0.2` 조건의 손실률을 재고, "필요 버퍼 길이 = 리더 지연 × rate"가 맞는지 확인하라. 정답/힌트: 0.2 s × 100 Hz = 20 scan. 길이 4에서는 대부분 버리고, 64부터 손실 0이어야 한다(TCG 지연 여유 포함).
5. **관찰**: CAN 실습 D를 `--rcvbuf 0`(기본값)으로 다시 돌려 `kernel_drops`가 어떻게 변하는지 보고, `/proc/sys/net/core/rmem_default`와 연결해 설명하라. 정답/힌트: 기본 수신 버퍼가 크면 20초 동안은 넘치지 않는다. 대신 latency p99가 계속 커진다(큐에 쌓임) — "drop 없음"이 "건강함"은 아니다.
6. **systemd**: `q7-health.service`를 설계 A(항상 ping, 경보만)로 바꾸고, 같은 SIGSTOP 실험에서 `NRestarts`가 0으로 유지되고 journal에 warning만 남는지 확인하라. 정답/힌트: `if h.state != 'FAULT'` 조건을 지우고, FAULT 진입 시 `log(3, ...)`(err)를 남긴다.
7. **설계**: 카메라 30 Hz와 IMU 200 Hz의 sync 오차를 재는 실험을 설계하라. 어떤 HW 신호를 무엇으로 찍고, 어떤 분포를 보고, 합격 기준은 무엇인가. 정답/힌트: 카메라 노출 strobe 출력을 GPIO 입력(또는 PPS 입력)으로 같은 시계에 찍는다(Q3). `t_frame - t_strobe`의 p99 < 1 ms 같은 기준.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| ODR | Output Data Rate | 센서가 샘플을 내는 명목 주파수 |
| ppm | parts per million | 10^-6 단위 상대 오차. 100 ppm = 하루 8.64초 |
| jitter | 지터 | 샘플 간격이 명목 주기에서 벗어나는 정도. 여기서는 `abs(dt - T)` |
| gap | 간격 결함 | 간격이 명목의 1.5배를 넘는 곳. 손실의 흔적 |
| rolling counter | 순환 카운터 | 메시지마다 1씩 늘고 `2^n`에서 0으로 도는 순번 필드 |
| clock domain | 시계 도메인 | 시각이 어떤 시계 기준인가(REALTIME, MONOTONIC, PHC, 센서 내부) |
| percentile (p99) | 백분위수 | 99 %의 샘플이 그 값 이하. 꼬리 성능 지표 |
| Welford | 온라인 분산 알고리즘 | 평균·분산을 한 번 지나가며 안정적으로 누적 |
| Allan deviation | 앨런 편차 | 시간 척도 tau별 주파수 안정도. 발진기·IMU 노이즈 분석 |
| hysteresis | 히스테리시스 | 들어가는 문턱과 나오는 문턱을 다르게 해서 떨림을 막는 것 |
| sd_notify | systemd 알림 프로토콜 | `$NOTIFY_SOCKET`에 `READY=1`, `WATCHDOG=1`, `STATUS=` 전송 |
| `WatchdogSec=` | 서비스 watchdog | 이 시간 안에 ping이 없으면 systemd가 서비스를 abort |
| `SO_RXQ_OVFL` | 소켓 drop 카운터 옵션 | 수신 큐 넘침으로 버린 누적 개수를 cmsg로 받음 |
| `SO_TIMESTAMPNS` | 소켓 수신 시각 옵션 | 커널이 패킷을 받은 시각(ns)을 cmsg로 받음 |
| MCAP | 로그 컨테이너 형식 | chunk + 인덱스 구조, ROS 2 bag 기본 저장 형식 |
| HIL | Hardware-in-the-loop | 실제 HW를 CI 루프에 넣어 시험하는 구성 |
| textfile collector | 메트릭 파일 수집 | 데몬이 파일에 쓴 Prometheus 텍스트 메트릭을 수집기가 가져감 |

---

## 17. 요약 & 체크리스트

센서 데이터 품질은 "값이 나온다"가 아니라 "값이 약속한 시각에, 빠짐없이, 같은 시계로, 말이 되는 값으로 나온다"를 숫자로 증명하는 일이다. 증명의 재료는 타임스탬프·순번·값·도착 시각 네 가지뿐이고, 커널은 대부분의 경로에서 **조용히** 데이터를 버리므로 툴은 순번과 시각으로 손실을 추론해야 한다. 재사용 가능한 툴은 입력별 어댑터와 하나의 고정 메모리 통계 엔진으로 나누고, 정답을 아는 합성 스트림으로 엔진부터 검증한다. 그 다음 실제 장치에 결함을 일부러 넣어(부하, rate 변경, 작은 버퍼, 느린 소비자, 시계 스텝, 중복 송신자) 툴이 **각 결함을 다른 지표로** 잡는지 확인한다. 장비 위에서는 같은 엔진을 1초 창 + 히스테리시스 상태 기계로 돌려 systemd watchdog·metrics·journal로 바깥과 연결하고, CI에서는 같은 리포트를 스펙과 비교하는 게이트로 쓴다.

- [ ] 데이터 품질 결함 13가지를 시간·값·센서 사이로 나눠 검출 신호와 함께 말할 수 있다
- [ ] rate 오차(ppm), jitter, gap, 시각 기준·순번 기준 손실, latency의 공식을 쓸 수 있다
- [ ] 평균 대신 p50/p99/max를 보는 이유를 SSD latency와 연결해 설명할 수 있다
- [ ] 로그 간격 히스토그램으로 고정 메모리 percentile을 계산하는 코드를 짤 수 있다
- [ ] wrap이 있는 순번의 차이를 올바르게 접는 방법을 설명할 수 있다
- [ ] IIO scan_elements에서 레코드 배치를 계산하고 numpy dtype으로 읽을 수 있다
- [ ] V4L2 DQBUF의 sequence·timestamp·ERROR 플래그로 드롭을 셀 수 있다
- [ ] SocketCAN에서 `SO_TIMESTAMPNS`와 `SO_RXQ_OVFL`로 수신 시각과 커널 drop을 받을 수 있다
- [ ] IIO kfifo, vb2 큐, 소켓 수신 버퍼가 각각 어디서 데이터를 버리는지 소스 위치로 말할 수 있다
- [ ] 결함을 주입해 툴을 검증하는 실험(부하, rate 변경, 작은 버퍼, 시계 스텝, 중복 송신자)을 설계할 수 있다
- [ ] 히스테리시스 상태 기계 + `Type=notify` + `WatchdogSec=` 헬스 모니터를 만들고 트립시킬 수 있다
- [ ] 품질 리포트를 CI 게이트로 쓰는 방법과 그 한계(VM 숫자 ≠ HW 숫자)를 설명할 수 있다

---

## 참고 자료

- docs.kernel.org — Industrial I/O: "Buffers", "Triggers", sysfs ABI `Documentation/ABI/testing/sysfs-bus-iio` (`current_timestamp_clock`, `buffer/length`)
- docs.kernel.org — Linux Media Subsystem, V4L2 API: "Buffers" (`struct v4l2_buffer`, `sequence`, `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`, `V4L2_BUF_FLAG_ERROR`), "vivid: Virtual Video Test Driver" (오류 주입 컨트롤)
- docs.kernel.org — Networking: "SocketCAN - Controller Area Network", "Timestamping" (`SO_TIMESTAMPNS`)
- man7.org — `socket(7)` (`SO_RXQ_OVFL`, `SO_RCVBUF`), `cmsg(3)`, `clock_gettime(2)`
- freedesktop.org — `sd_notify(3)`, `systemd.service(5)` (`Type=notify`, `WatchdogSec=`, `Restart=on-watchdog`), `systemd.unit(5)` (`StartLimitBurst=`)
- 커널 소스(6.18.55, elixir.bootlin.com에서 같은 경로): `drivers/iio/buffer/kfifo_buf.c`, `drivers/iio/industrialio-buffer.c`, `drivers/iio/dummy/iio_simple_dummy_buffer.c`, `drivers/iio/trigger/iio-trig-hrtimer.c`, `drivers/media/test-drivers/vivid/vivid-kthread-cap.c`, `drivers/media/test-drivers/vivid/vivid-ctrls.c`, `drivers/net/can/vcan.c`, `net/can/raw.c`, `net/core/sock.c`, `net/socket.c`
- B. P. Welford, "Note on a Method for Calculating Corrected Sums of Squares and Products", Technometrics, 1962 / Chan, Golub, LeVeque의 병렬 분산 병합 공식
- IEEE Std 1139 (주파수·시간 안정도 측정 정의, Allan variance) — 기억 기반, 표준 문서로 확인 필요
- mcap.dev — MCAP 형식 명세와 Python `mcap` 라이브러리
- can-utils (github.com/linux-can/can-utils) — `cangen`, `candump`
- 관련 노트: I2(IIO), I5(V4L2), I10(CAN), L1(systemd), N3(지연 측정), P6(테스트와 CI), Q2~Q6
