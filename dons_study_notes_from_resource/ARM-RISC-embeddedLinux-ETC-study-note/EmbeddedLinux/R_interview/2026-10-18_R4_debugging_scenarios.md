# R4. 디버깅 시나리오 — "How would you debug X?" 라운드를 구조로 이긴다

> **이 노트를 다 읽으면**: 9개 단골 시나리오(센서 미검출, 간헐적 데이터 손상, 부팅 멈춤, 카메라 프레임 드롭, CAN 메시지 손실·bus-off, 지연 스파이크, 메모리 증가, OTA 후 부팅 실패, 필드 Oops)를 "명확화 → 진단 트리 → 명령 → HW/SW 가르기 → 원인 → 2분 답변"으로 말할 수 있다 · 각 단계에서 칠 명령을 외워서 쓸 수 있다 · 랩에서 직접 재현한 4개 사례(fw_devlink 대기, systemd emergency, irqsoff 스파이크, kmemleak 누수)의 실제 출력을 근거로 댈 수 있다
> **왜 필요한가**: 시니어 임베디드 면접(Scout AI·Anduril·Neros·Rivian·Verkada)의 "실무 감각" 라운드는 거의 항상 "X가 안 된다, 어떻게 볼래?"다. 정답보다 **순서와 근거**를 본다
> **Don 기준 난이도**: 진단 사고(가설·측정·이분 탐색, scope·LA·JTAG)는 이미 강점 / 새로 익힐 것은 각 층에서 Linux가 주는 관찰 창(debugfs, tracefs, sysfs, journal)의 이름과 출력 모양
> **선행 노트**: M1–M11(디버그·트레이스), N1–N3(실시간·지연), P1·P3·P4(bring-up), Q1·Q2·Q4(카메라·CAN), L1·L4·L7(systemd·OTA·크래시 수집), H9(DMA). 이 노트는 그 노트들의 **요약·연결**이고 깊은 설명은 ID로 넘긴다

---

## 0. 큰 그림 — 면접관이 실제로 채점하는 것

### 0.1 직관 — "고장 난 SSD를 받은 FA 엔지니어"

Don이 SK hynix에서 RMA로 돌아온 SSD를 받았을 때를 떠올리면 된다. 바로 펌웨어를 고치지 않는다. (1) 언제부터, 몇 대가, 어떤 호스트에서 그랬는지 묻고, (2) 재현 조건을 만들고, (3) NAND·컨트롤러·펌웨어·호스트 중 어디 층인지 가르고, (4) 가장 싸고 흔한 원인(로그·SMART 카운터)부터 보고, (5) 펌웨어 버전이나 조건을 반씩 바꿔 경계를 좁히고, (6) 고친 뒤 재현 테스트로 증명한다. Linux 디버깅 면접도 똑같은 뼈대다. **다른 것은 각 층의 "창문" 이름뿐이다** — `devices_deferred`, `/proc/interrupts`, `trace`, `kmemleak`, `journalctl` 같은.

면접관이 채점하는 네 가지:

| 항목 | 좋은 답 | 나쁜 답 |
|---|---|---|
| 명확화 | 범위·빈도·최근 변경을 먼저 묻는다 | 바로 "드라이버를 다시 쓰겠다" |
| 순서 | 싸고 흔한 것부터, 각 단계가 무엇을 배제하는지 말한다 | 도구 이름 나열 |
| 근거 | 명령과 **기대 출력**을 말한다 ("여기서 `-517`이 보이면…") | "로그를 보겠다"로 끝 |
| 경계 | HW/SW 경계를 측정으로 가른다 (scope·LA·GPIO 토글) | "HW 팀에 넘긴다" 또는 "SW 문제일 것" |

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">어떤 시나리오든 같은 뼈대 — 면접관은 이 순서를 듣고 싶어 한다</text>
<rect x="20" y="36" width="410" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="225.0" y="59.2" font-size="12" text-anchor="middle" font-weight="bold">1. 명확화 (질문 3~5개)</text>
<text x="225.0" y="75.2" font-size="12" text-anchor="middle">언제부터 · 몇 대 · 무엇이 바뀌었나 · 얼마나 자주</text>
<line x1="225" y1="90" x2="225" y2="104" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<rect x="20" y="104" width="410" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="225.0" y="127.2" font-size="12" text-anchor="middle" font-weight="bold">2. 재현과 범위</text>
<text x="225.0" y="143.2" font-size="12" text-anchor="middle">항상/가끔 · 한 대/전부 · 특정 온도·부하·보드 리비전</text>
<line x1="225" y1="158" x2="225" y2="172" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<rect x="20" y="172" width="410" height="54" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="225.0" y="195.2" font-size="12" text-anchor="middle" font-weight="bold">3. 층을 나눈다</text>
<text x="225.0" y="211.2" font-size="12" text-anchor="middle">HW(전원·클럭·신호) · 커널(probe·IRQ·DMA) · 유저(앱·설정)</text>
<line x1="225" y1="226" x2="225" y2="240" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<rect x="20" y="240" width="410" height="54" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="225.0" y="263.2" font-size="12" text-anchor="middle" font-weight="bold">4. 싸고 흔한 것부터</text>
<text x="225.0" y="279.2" font-size="12" text-anchor="middle">로그 → 상태 파일 → 트레이스 → 계측기 → 코드 수정</text>
<line x1="225" y1="294" x2="225" y2="308" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<rect x="20" y="308" width="410" height="54" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="225.0" y="331.2" font-size="12" text-anchor="middle" font-weight="bold">5. 이분 탐색</text>
<text x="225.0" y="347.2" font-size="12" text-anchor="middle">버전·설정·HW를 반씩 바꿔 경계를 좁힌다 (git bisect)</text>
<line x1="225" y1="362" x2="225" y2="376" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<rect x="20" y="376" width="410" height="54" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="225.0" y="399.2" font-size="12" text-anchor="middle" font-weight="bold">6. 고치고 증명한다</text>
<text x="225.0" y="415.2" font-size="12" text-anchor="middle">재현 테스트 통과 · 회귀 테스트 추가 · 모니터링</text>
<rect x="460" y="172" width="210" height="112" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="565.0" y="200.2" font-size="12" text-anchor="middle" font-weight="bold">HW냐 SW냐</text>
<text x="565.0" y="216.2" font-size="12" text-anchor="middle">핀에서 본 시각·값과</text>
<text x="565.0" y="232.2" font-size="12" text-anchor="middle">커널이 본 시각·값을</text>
<text x="565.0" y="248.2" font-size="12" text-anchor="middle">같은 시간축에 놓는다</text>
<text x="565.0" y="264.2" font-size="12" text-anchor="middle">(GPIO 토글 + LA, M11)</text>
<line x1="430" y1="228" x2="458" y2="228" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<rect x="460" y="36" width="210" height="112" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="565.0" y="64.2" font-size="12" text-anchor="middle" font-weight="bold">첫 5분 키트</text>
<text x="565.0" y="80.2" font-size="12" text-anchor="middle">dmesg -T · journalctl -b</text>
<text x="565.0" y="96.2" font-size="12" text-anchor="middle">/sys · debugfs</text>
<text x="565.0" y="112.2" font-size="12" text-anchor="middle">/proc/interrupts</text>
<text x="565.0" y="128.2" font-size="12" text-anchor="middle">ip -s · v4l2 seq</text>
<rect x="460" y="308" width="210" height="112" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="565.0" y="336.2" font-size="12" text-anchor="middle" font-weight="bold">막혔을 때</text>
<text x="565.0" y="352.2" font-size="12" text-anchor="middle">가설을 소리 내 말하고</text>
<text x="565.0" y="368.2" font-size="12" text-anchor="middle">"다음 측정이 무엇을</text>
<text x="565.0" y="384.2" font-size="12" text-anchor="middle">배제하나"를 말한다</text>
<text x="565.0" y="400.2" font-size="12" text-anchor="middle"></text>
<line x1="430" y1="364" x2="458" y2="364" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — 모든 시나리오에 쓰는 공통 뼈대. 왼쪽 여섯 칸을 순서대로 말하고, 오른쪽의 "HW냐 SW냐" 칸에서 Don의 계측 경험을 꺼낸다.

### 0.2 답변 템플릿 — 30초와 2분

면접에서 시나리오 질문을 받으면 이렇게 말한다:

1. **30초**: "먼저 X·Y·Z를 묻겠다. 그다음 [층 A]부터 본다 — 가장 흔하고 확인이 싸기 때문이다. 결정적 증거는 [명령/출력]이다." — 여기서 면접관이 끼어들어 방향을 준다. 그게 정상이다.
2. **2분**: 진단 트리를 위에서 아래로 내려가며 "이게 보이면 다음은…"을 3~5단계. 마지막에 실제로 겪은 사례 하나(이 노트의 랩 결과나 Don의 RF/SSD 경험, R6)로 닫는다.
3. **꼬리 질문 대비**: "재현이 안 되면?", "로그가 없으면?", "고쳤는지 어떻게 아나?" — 모든 시나리오에 공통이다(§12).

### 0.3 이 노트의 실습 환경

- VM 호스트: Lima `elinux`, `uname -r` = `7.0.0-38-generic`. 게스트 커널은 랩의 Linux 6.18.55 (`Image`, `Image-sec`, `Image-trace`, `Image-kasan`).
- 새로 재현한 것 4개: §1.6(fw_devlink 대기, `Image` + 커스텀 DT), §3.6(systemd emergency, `Image-sec` + rootfs-sysd overlay), §6.6(irqsoff 스파이크, `Image-trace` + 업스트림 `preemptirq_delay_test`), §7.6(kmemleak, `Image-kasan` + 교육용 누수 모듈).
- **QEMU TCG(에뮬레이션)의 시간 값은 실제 HW 숫자가 아니다.** 지연 수치는 "모양"만 믿는다.
- 나머지 근거는 앞선 노트들이 랩에서 측정한 출력이다. 인용할 때 노트 ID와 절을 붙인다.

---

## 1. 시나리오 1 — "센서가 안 보인다" (I2C 장치 미검출 · probe 안 됨)

### 1.1 면접관의 질문

> "We brought up a new carrier board. The IMU on I2C bus 2 doesn't show up under /sys/bus/iio/devices. How do you debug it?"

### 1.2 먼저 묻는 질문

- 이전 보드 리비전에서는 됐나? 바뀐 것은 DT인가, 커널인가, 보드인가?
- "안 보인다"가 **장치 객체가 없다**인가, **IIO 노드만 없다**인가, **값이 이상하다**인가?
- 같은 버스의 다른 장치는 되나? (어댑터 자체가 살아 있나)
- 드라이버가 built-in인가 모듈인가?

### 1.3 진단 트리 — 위에서 아래로

```svg
<svg viewBox="0 0 680 556" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">"센서가 안 보인다" — 위에서 아래로, 처음 "아니오"가 나오는 칸이 범인의 층</text>
<rect x="10" y="40" width="280" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="150.0" y="64.2" font-size="12" text-anchor="middle">DT 노드가 있고 status = okay?</text>
<text x="150.0" y="80.2" font-size="12" text-anchor="middle">/proc/device-tree/…</text>
<rect x="390" y="40" width="280" height="56" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="530.0" y="72.2" font-size="12" text-anchor="middle">overlay 미적용 · status · 경로 오타</text>
<line x1="290" y1="68" x2="388" y2="68" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="339" y="62" font-size="12" text-anchor="middle">아니오</text>
<line x1="150" y1="96" x2="150" y2="116" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="160" y="110" font-size="12" text-anchor="start">예</text>
<rect x="10" y="116" width="280" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="150.0" y="140.2" font-size="12" text-anchor="middle">버스 아래 장치 객체가 생겼나?</text>
<text x="150.0" y="156.2" font-size="12" text-anchor="middle">/sys/bus/i2c/devices/1-0068</text>
<rect x="390" y="116" width="280" height="56" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="530.0" y="148.2" font-size="12" text-anchor="middle">부모 버스(어댑터) 미동작 · reg 오류</text>
<line x1="290" y1="144" x2="388" y2="144" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="339" y="138" font-size="12" text-anchor="middle">아니오</text>
<line x1="150" y1="172" x2="150" y2="192" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="160" y="186" font-size="12" text-anchor="start">예</text>
<rect x="10" y="192" width="280" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="150.0" y="216.2" font-size="12" text-anchor="middle">driver 링크가 있나?</text>
<text x="150.0" y="232.2" font-size="12" text-anchor="middle">ls -l …/driver</text>
<rect x="390" y="192" width="280" height="56" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="530.0" y="224.2" font-size="12" text-anchor="middle">아래 두 칸으로</text>
<line x1="290" y1="220" x2="388" y2="220" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="339" y="214" font-size="12" text-anchor="middle">아니오</text>
<line x1="150" y1="248" x2="150" y2="268" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="160" y="262" font-size="12" text-anchor="start">예</text>
<rect x="10" y="268" width="280" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="150.0" y="292.2" font-size="12" text-anchor="middle">devices_deferred에 이유가 있나?</text>
<text x="150.0" y="308.2" font-size="12" text-anchor="middle">debugfs devices_deferred</text>
<rect x="390" y="268" width="280" height="56" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="530.0" y="300.2" font-size="12" text-anchor="middle">공급자(clk·reg·gpio) 없음 → 공급자 노드</text>
<line x1="290" y1="296" x2="388" y2="296" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="339" y="290" font-size="12" text-anchor="middle">아니오</text>
<line x1="150" y1="324" x2="150" y2="344" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="160" y="338" font-size="12" text-anchor="start">예</text>
<rect x="10" y="344" width="280" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="150.0" y="368.2" font-size="12" text-anchor="middle">probe 에러가 찍혔나?</text>
<text x="150.0" y="384.2" font-size="12" text-anchor="middle">dmesg · dd.c dyndbg "rejects match"</text>
<rect x="390" y="344" width="280" height="56" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="530.0" y="376.2" font-size="12" text-anchor="middle">compatible 오타 · 모듈 없음 · ID 불일치</text>
<line x1="290" y1="372" x2="388" y2="372" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="339" y="366" font-size="12" text-anchor="middle">아니오</text>
<line x1="150" y1="400" x2="150" y2="420" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="160" y="414" font-size="12" text-anchor="start">예</text>
<rect x="10" y="420" width="280" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="150.0" y="444.2" font-size="12" text-anchor="middle">버스에서 칩이 대답하나?</text>
<text x="150.0" y="460.2" font-size="12" text-anchor="middle">i2cdetect -r · i2ctransfer</text>
<rect x="390" y="420" width="280" height="56" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="530.0" y="452.2" font-size="12" text-anchor="middle">NACK: 전원·리셋 극성·주소 strap · 풀업</text>
<line x1="290" y1="448" x2="388" y2="448" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="339" y="442" font-size="12" text-anchor="middle">아니오</text>
<rect x="10" y="500" width="660" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340.0" y="518.2" font-size="12" text-anchor="middle">칩이 대답하는데 값이 이상하면</text>
<text x="340.0" y="534.2" font-size="12" text-anchor="middle">regmap debugfs 덤프 → 데이터시트 시퀀스 → scope로 SCL·SDA·INT</text>
</svg>
```
그림 2 — 센서 미검출 진단 트리. 각 칸은 "있나?" 질문이고, 처음 "아니오"가 나오는 칸이 원인의 층이다. 순서는 M2의 체인(DT 노드 → 장치 객체 → bind → defer → probe 에러 → 자원)과 같다.

각 칸의 명령:

```sh
# 1) DT 노드: 커널이 받은 최종 DT (overlay 적용 결과 포함)
ls /proc/device-tree/soc/i2c@30a30000/; cat /proc/device-tree/soc/i2c@30a30000/imu@68/status
# 2) 장치 객체와 3) driver 링크
ls -l /sys/bus/i2c/devices/2-0068/ ; readlink /sys/bus/i2c/devices/2-0068/driver
# 4) 왜 기다리나 (dev_err_probe가 남긴 이유)
cat /sys/kernel/debug/devices_deferred
# 5) 조용한 probe 실패 보기: driver core의 dev_dbg 켜고 재-bind
echo "file drivers/base/dd.c +p" > /sys/kernel/debug/dynamic_debug/control
echo 2-0068 > /sys/bus/i2c/drivers/bmi160_i2c/bind; dmesg | tail -3
# 6) 버스에서 대답하나 (드라이버가 안 붙은 주소만, -r = read byte로 탐지)
i2cdetect -y -r 2; i2ctransfer -y 2 w1@0x68 0x00 r1     # WHO_AM_I 같은 ID 레지스터
```

(경로·주소는 예시다 — 가이드, 이 블록은 실행 안 함. 같은 명령의 실제 출력은 M2 §1, P3 §4, §1.6에 있다.)

순서의 이유: 1~5는 **보드를 만지지 않고 셸에서 몇 초**에 끝난다. 6은 버스를 건드리고, 그다음(전원·리셋·scope)은 사람이 보드 앞에 가야 한다. 싼 것부터.

### 1.4 HW냐 SW냐 가르기

- `i2cdetect`가 주소에 `--`면 칩이 ACK를 안 한 것이다. 이때부터 SW 문제는 거의 아니다: **전원 레일 → 리셋 극성 → 클럭 → 주소 strap → 풀업** 순서로 의심한다(P4 §8.2). 이 순서는 "칩 ID 읽기 실패"의 증상이 칩이 없을 때와 똑같기 때문에 필요하다.
- **에러 사이 시간 간격**이 첫 감별 단서다. NACK(칩 없음·리셋 중)은 `-ENXIO`/`-EREMOTEIO`가 즉시(1 ms 간격) 오고, 풀업이 없으면 i2c-algo-bit이 SCL 상승을 기다리다 타임아웃해 약 0.83초 간격이 된다 — P4 §8.4의 실측. 부팅이 11초 → 122초로 늘었다.
- scope로 SCL/SDA: 풀업 없음 = 내려간 뒤 안 올라오거나 RC 곡선. 주소 바이트 뒤 9번째 클럭에서 SDA가 High면 NACK. 이 단계가 Don의 홈그라운드다(M11 §9.2: "가끔 -110" → LA로 보니 클럭 스트레칭 3 ms).

### 1.5 실제로 본 원인들 (근거)

| 증상 | 원인 | 근거 |
|---|---|---|
| 장치 객체는 있는데 driver 링크 없음, **에러 0줄** | probe가 `-ENODEV`(-19) — driver core는 이것을 dev_dbg로만 남김. `new_device`로 만든 장치는 fwnode가 없어 `device_get_match_data()`가 NULL | P3 §4.2 (`adxl345_i2c ... rejects match -19`) |
| `failed to power on` + read/write error | 리셋을 `GPIO_ACTIVE_HIGH`로 써서 통신 순간 칩이 리셋 상태 | P4 §8.2 (ftrace `gpio_value`가 전부 반대) |
| 에러 간격 0.83초, 부팅 10배 느려짐 | OD 선에 풀업 없음 | P4 §8.4 |
| `devices_deferred`에 남아 있음 | 공급자(gpio-sim 칩)가 아직 없음 → 생기면 자동 재시도로 bind | M2 §1 (`m2_cons: reset gpio not ready`) |
| WHO_AM_I 불일치로 probe 실패 | 칩 리비전 변경·잘못된 compatible | P3 §4.3 |
| probe 중 Oops | DT 실수(`link-frequencies`에 `/bits/ 64` 누락)가 드라이버 에러 경로의 NULL 역참조를 깨움 | Q1 §8.6 (§9.3에서 다시) |

### 1.6 랩 재현 — 공급자가 없는 센서, 그리고 compatible 오타

무엇을 확인하나: 회로에서 "IMU 전원 스위치와 DRDY가 I2C GPIO expander에 물려 있는데, 그 expander 드라이버가 커널에 없다"와 "compatible 오타"를 DT로 만들어, 각각이 sysfs·debugfs·dmesg에 어떻게 보이는지. `Image`(defconfig, 6.18.55) + 랩 `virt.dts` 끝에 아래 조각을 붙여 `-dtb`로 부팅했다.

```dts
/ {
	r4_gpio: r4-gpio@9100000 {            /* 드라이버가 없는 GPIO expander */
		compatible = "r4,missing-gpio-expander";
		reg = <0x0 0x09100000 0x0 0x1000>;
		gpio-controller;
		#gpio-cells = <2>;
	};
	r4_vdd: r4-imu-vdd {                   /* expander 핀으로 켜는 센서 VDD */
		compatible = "regulator-fixed";
		regulator-name = "r4_imu_vdd";
		regulator-min-microvolt = <1800000>;
		regulator-max-microvolt = <1800000>;
		gpio = <&r4_gpio 1 0>;
		enable-active-high;
	};
	r4-keys {                              /* 같은 expander의 DRDY 입력 */
		compatible = "gpio-keys";
		r4-drdy { label = "r4-imu-drdy"; gpios = <&r4_gpio 3 0>; linux,code = <0x100>; };
	};
	r4-imu@9200000 {                       /* 오타: 숫자 0 대신 대문자 O */
		compatible = "bosch,bmi16O";
		reg = <0x0 0x09200000 0x0 0x1000>;
	};
};
```

게스트 스크립트(BusyBox initramfs, `LAB_CMD=sh~/r4a.sh`):

```sh
echo "== R4A t=$(cut -d' ' -f1 /proc/uptime)s devices_deferred:"; cat /sys/kernel/debug/devices_deferred; echo "[end]"
for d in 9100000.r4-gpio r4-imu-vdd r4-keys 9200000.r4-imu; do
  p=/sys/bus/platform/devices/$d; printf "%-18s " $d
  [ -e $p ] || { echo "NO DEVICE"; continue; }
  [ -e $p/driver ] && echo "driver=$(basename $(readlink $p/driver))" || echo "no driver; modalias=$(cat $p/modalias)"
done
for l in /sys/class/devlink/*r4*; do echo "$(basename $l) status=$(cat $l/status) sync_state_only=$(cat $l/sync_state_only)"; done
sleep 14
echo "== R4A t=$(cut -d' ' -f1 /proc/uptime)s after deferred_probe_timeout:"
dmesg | grep -iE "r4|deferred" | grep -v "Kernel command line"
cat /sys/kernel/debug/devices_deferred; echo "[end]"
```

```text
== R4A t=0.67s devices_deferred:
r4-keys	gpio-keys: failed to get gpio
r4-imu-vdd	platform: supplier 9100000.r4-gpio not ready
[end]
9100000.r4-gpio    no driver; modalias=of:Nr4-gpioT(null)Cr4,missing-gpio-expander
r4-imu-vdd         no driver; modalias=of:Nr4-imu-vddT(null)Cregulator-fixed
r4-keys            no driver; modalias=of:Nr4-keysT(null)Cgpio-keys
9200000.r4-imu     no driver; modalias=of:Nr4-imuT(null)Cbosch,bmi16O
platform:9100000.r4-gpio--platform:r4-imu-vdd status=dormant sync_state_only=0
platform:9100000.r4-gpio--platform:r4-keys status=dormant sync_state_only=1
[   10.794173] platform r4-keys: deferred probe pending: gpio-keys: failed to get gpio
== R4A t=14.78s after deferred_probe_timeout:
[    0.000000] Unknown kernel command line parameters "LAB_CMD=sh~/r4a.sh", will be passed to user space.
[    0.631518]     LAB_CMD=sh~/r4a.sh
[   10.794173] platform r4-keys: deferred probe pending: gpio-keys: failed to get gpio
r4-keys	gpio-keys: failed to get gpio
r4-imu-vdd	reg-fixed-voltage: can't get GPIO
[end]
```

(10.79 s 줄은 `sleep` 중에 콘솔로도 찍혀서 두 번 보인다.) 출력에서 볼 것:

- **두 소비자의 이유 문자열이 다르다.** `r4-imu-vdd`는 `platform: supplier 9100000.r4-gpio not ready` — driver core가 probe를 **시작도 안 시켰다**(`drivers/base/core.c` `device_links_check_suppliers()`의 "supplier %s not ready"). fw_devlink가 DT의 `gpio = <&r4_gpio …>` phandle을 보고 만든 device link 때문이다(`sync_state_only=0`, 진짜 의존성).
- `r4-keys`는 드라이버가 probe를 돌았고 gpiolib이 `-EPROBE_DEFER`를 줬다(`gpio-keys: failed to get gpio`). GPIO가 **자식 노드**(`r4-drdy`)에 있어서 링크가 `sync_state_only=1`(probe 순서를 막지 않는 약한 링크)로 만들어졌기 때문이다. M2 §6에서 "gpio-keys에 devlink가 안 보인다"고 관찰만 했던 현상의 이유가 이것이다.
- 10.79 s = `deferred_probe_timeout`(6.18 기본 10초, `CONFIG_MODULES=y`일 때 — `dd.c` 261행). 이때 커널이 `deferred probe pending: <이유>`를 **경고 레벨**로 찍고(`dd.c` 317행), fw_devlink 링크를 풀어 준다(`fw_devlink_drivers_done()`). 그래서 타임아웃 뒤 `r4-imu-vdd`의 이유가 `reg-fixed-voltage: can't get GPIO`로 바뀌었다 — 이제 probe는 돌지만 GPIO 공급자가 여전히 없다. (같은 설정의 첫 실행에서는 10.78 s에 두 장치 모두 `deferred probe pending` 줄이 나왔다. 어느 줄이 찍히는지는 타이밍에 따라 다르다.)
- **오타 장치(`bosch,bmi16O`)는 어디에도 없다** — deferred 목록에도, dmesg에도. 장치 객체는 있고 modalias는 `Cbosch,bmi16O`인데 그 문자열과 맞는 드라이버가 없을 뿐이다. 매칭 실패는 에러가 아니다. 찾는 법은 modalias와 소스의 매칭 테이블을 대조하는 것:

```sh
grep -rn '"bosch,bmi16' /var/lab/src/linux-6.18.55/drivers/iio
```

```text
/var/lab/src/linux-6.18.55/drivers/iio/imu/bmi160/bmi160_i2c.c:65:	{ .compatible = "bosch,bmi160" },
/var/lab/src/linux-6.18.55/drivers/iio/imu/bmi160/bmi160_spi.c:53:	{ .compatible = "bosch,bmi160" },
```

- `dtc`는 두 실수 모두 경고하지 않았다(같은 dts를 경고와 함께 다시 컴파일해도 r4 노드 경고는 0줄 — virt 원본 노드 경고만). 오타 compatible을 잡는 것은 `dtbs_check`(binding 스키마, D7)다.

### 1.7 말로 하는 답

**30초**: "먼저 이전 리비전에서 됐는지, 바뀐 게 DT·커널·보드 중 무엇인지 묻겠다. 그다음 커널 쪽 체인을 위에서부터 끊어 본다 — DT 노드, 장치 객체, driver 링크, `devices_deferred`, probe 에러. 셸에서 1분이면 끝나니까. 칩이 버스에서 ACK를 안 하면 그때부터 전원·리셋 극성·주소·풀업을 scope로 본다."

> First I'd ask what changed versus the last working board — DT, kernel, or hardware — and whether other devices on the same bus work. Then I walk the kernel chain top-down, because it's cheap: is the DT node there and okay in /proc/device-tree, did the bus create the device object, is there a driver symlink, is it parked in debugfs devices_deferred with a reason, and did probe fail — I'd enable dynamic debug for drivers/base/dd.c because -ENODEV probe failures are only logged at debug level. If the chip doesn't ACK on the bus, it's no longer a software question: I check the rail, reset polarity, clock, address strap and pull-ups, and I put a scope on SCL/SDA. One tell I use: NACKs fail in a millisecond, missing pull-ups fail at timeout spacing, so the gap between error messages already tells me which one it is.

**2분 버전에 넣을 사례**: §1.6의 두 이유 문자열("supplier not ready" vs 드라이버의 `-EPROBE_DEFER`)과 "오타는 아무 데도 안 찍힌다"는 사실, 그리고 P4 §8.2의 리셋 극성(드라이버에서 반전해 고치면 다른 보드에서 깨진다 — DT 한 비트를 고친다).

**꼬리 질문·함정**:

- "What's the difference between -EPROBE_DEFER and -ENODEV?" → defer는 목록에 넣고 다른 bind가 생길 때마다 재시도, -ENODEV/-ENXIO는 "내 장치가 아님"으로 해석해 조용히 포기(G3).
- 함정: `i2cdetect`는 드라이버가 이미 붙은 주소를 `UU`로 표시한다. `UU`인데 IIO가 없다면 버스는 정상이고 드라이버 쪽이다.
- 함정: `i2cdetect` 기본 탐지 방식(quick write)이 일부 칩의 상태를 바꿀 수 있다 — 쓰기 민감한 장치가 있는 버스에서는 `-r`이나 범위 지정.

---

## 2. 시나리오 2 — "가끔 데이터가 깨진다"

### 2.1 면접관의 질문

> "Once every few hours, a sensor frame arrives with garbage in it. No crash, no error in the logs. Walk me through how you'd find it."

### 2.2 먼저 묻는 질문

- "깨진다"는 어떤 모양인가? 몇 바이트, 어느 오프셋, 무작위인가 패턴(0x00·0xFF·이전 프레임 데이터)인가?
- 모든 보드인가, 일부 보드·온도·케이블에서만인가? RAM 4 GiB 넘는 SKU에서만인가?
- 부하(카메라 동시 동작, CPU 100%)와 상관 있나? 최근 바뀐 것은(DMA로 전환, 커널 업그레이드, 캐시 설정)?
- 데이터 경로: 센서 → 버스 → DMA → 링 버퍼 → 유저 복사 중 어디서 검증할 수 있나?

### 2.3 진단 트리 — 깨진 모양이 층을 말해 준다

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">"가끔 데이터가 깨진다" — 깨진 모양이 층을 말해 준다</text>
<rect x="240" y="36" width="200" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340.0" y="54.2" font-size="12" text-anchor="middle">깨진 바이트를 직접 본다</text>
<text x="340.0" y="70.2" font-size="12" text-anchor="middle">hexdump · 기대값과 diff</text>
<rect x="10" y="116" width="160" height="52" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="90.0" y="138.2" font-size="12" text-anchor="middle">64B·4KiB 경계에 맞춰</text>
<text x="90.0" y="154.2" font-size="12" text-anchor="middle">덩어리로 틀림</text>
<rect x="10" y="200" width="160" height="92" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="90.0" y="226.2" font-size="12" text-anchor="middle" font-weight="bold">DMA·캐시</text>
<text x="90.0" y="242.2" font-size="12" text-anchor="middle">dma_sync 누락·방향</text>
<text x="90.0" y="258.2" font-size="12" text-anchor="middle">DMA_API_DEBUG</text>
<text x="90.0" y="274.2" font-size="12" text-anchor="middle">mask 거짓말 (H9 §5.3)</text>
<line x1="90" y1="168" x2="90" y2="198" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<polyline points="340,80 340,96 90,96 90,114" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<rect x="177" y="116" width="160" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="257.0" y="138.2" font-size="12" text-anchor="middle">부하·멀티코어일 때만</text>
<text x="257.0" y="154.2" font-size="12" text-anchor="middle">순서가 섞임</text>
<rect x="177" y="200" width="160" height="92" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="257.0" y="226.2" font-size="12" text-anchor="middle" font-weight="bold">레이스·오버런</text>
<text x="257.0" y="242.2" font-size="12" text-anchor="middle">KCSAN · lockdep</text>
<text x="257.0" y="258.2" font-size="12" text-anchor="middle">링 인덱스·seq 갭</text>
<text x="257.0" y="274.2" font-size="12" text-anchor="middle">KASAN (M8)</text>
<line x1="257" y1="168" x2="257" y2="198" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<polyline points="340,80 340,96 257,96 257,114" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<rect x="344" y="116" width="160" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="424.0" y="138.2" font-size="12" text-anchor="middle">특정 보드·온도·케이블</text>
<text x="424.0" y="154.2" font-size="12" text-anchor="middle">비트 단위로 틀림</text>
<rect x="344" y="200" width="160" height="92" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="424.0" y="226.2" font-size="12" text-anchor="middle" font-weight="bold">신호 무결성</text>
<text x="424.0" y="242.2" font-size="12" text-anchor="middle">CRC·에러 카운터</text>
<text x="424.0" y="258.2" font-size="12" text-anchor="middle">scope: 링잉·상승시간</text>
<text x="424.0" y="274.2" font-size="12" text-anchor="middle">풀업·종단 (P4, Q4)</text>
<line x1="424" y1="168" x2="424" y2="198" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<polyline points="340,80 340,96 424,96 424,114" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<rect x="511" y="116" width="160" height="52" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="591.0" y="138.2" font-size="12" text-anchor="middle">항상 같은 필드가</text>
<text x="591.0" y="154.2" font-size="12" text-anchor="middle">같은 식으로 틀림</text>
<rect x="511" y="200" width="160" height="92" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="591.0" y="226.2" font-size="12" text-anchor="middle" font-weight="bold">해석 버그</text>
<text x="591.0" y="242.2" font-size="12" text-anchor="middle">엔디안·부호 확장</text>
<text x="591.0" y="258.2" font-size="12" text-anchor="middle">스케일·단위</text>
<text x="591.0" y="274.2" font-size="12" text-anchor="middle">DBC·레지스터 맵</text>
<line x1="591" y1="168" x2="591" y2="198" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<polyline points="340,80 340,96 591,96 591,114" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<rect x="10" y="316" width="660" height="96" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340.0" y="344.2" font-size="12" text-anchor="middle" font-weight="bold">공통 첫 수: "언제 깨졌나"를 기록할 장치를 먼저 넣는다</text>
<text x="340.0" y="360.2" font-size="12" text-anchor="middle">시퀀스 번호 + CRC를 데이터에 싣고, 깨진 순간 GPIO 펄스 → LA 트리거</text>
<text x="340.0" y="376.2" font-size="12" text-anchor="middle">깨진 위치(오프셋)·주기·조건이 쌓이면 칸이 정해진다</text>
<text x="340.0" y="392.2" font-size="12" text-anchor="middle">크래시 지점 ≠ 버그 지점: 탐지기(KASAN·KCSAN)로 원인 시점을 당긴다 (M3 §9.4)</text>
</svg>
```
그림 3 — 간헐적 손상은 "깨진 바이트를 직접 보는 것"에서 시작한다. 경계 정렬된 덩어리면 DMA·캐시, 부하 때만이면 레이스·오버런, 특정 보드·환경이면 신호 무결성, 항상 같은 필드면 해석 버그다.

단계와 명령:

1. **증거를 남길 장치부터 넣는다.** 프레임에 시퀀스 번호와 CRC를 싣고(펌웨어나 드라이버), 수신 측에서 검증 실패 시 원본 바이트를 덤프하고 GPIO 펄스를 낸다(M11 §3 디버그 GPIO 패턴). 간헐 버그는 "언제·어디가" 쌓여야 분류된다.
2. **DMA·캐시**: 디버그 커널에서 `CONFIG_DMA_API_DEBUG`로 map/unmap/sync 짝과 방향을 검사한다. H9 §9.2에서 실제로 잡힌 메시지: `device driver syncs DMA memory with different direction [mapped with DMA_TO_DEVICE] [synced with DMA_FROM_DEVICE]`, `device driver failed to check map error`. 비코히어런트 SoC에서 `dma_sync_single_for_cpu()` 누락은 "가끔 이전 프레임 데이터"로 보인다.
3. **DMA mask**: 장치가 28비트밖에 못 내는데 드라이버가 32비트라고 주장하면 커널은 그대로 믿고, 장치는 잘린 주소에 쓴다. **에러도 로그도 없이** 결과만 틀렸다(`dst != src`) — H9 §5.3 실측. "RAM이 큰 SKU에서만"이 단서다.
4. **레이스·오버런**: `KCSAN`(데이터 레이스), `lockdep`, `KASAN`(범위 초과) 디버그 커널로 같은 부하를 돌린다(M8). 링 버퍼면 생산자·소비자 인덱스와 overrun 카운터를 노출해 둔다(F 모듈 링버퍼, Q7 품질 툴).
5. **신호 무결성**: 버스 쪽 에러 카운터가 먼저다 — CAN `ip -details -statistics`의 bus-errors, CSI-2 수신기의 ECC/CRC 카운터(SoC 드라이버 debugfs), PCIe AER. 그다음 scope(링잉, 상승 시간, 접지)와 온도·진동 조건 재현.
6. **해석 버그**: 엔디안, 부호 확장(12비트 ADC 값을 `s16`로), 스케일, DBC 신호 정의. 항상 같은 필드가 같은 방식으로 틀리면 여기다.

### 2.4 HW냐 SW냐 가르기

- **같은 바이트를 두 곳에서 본다.** LA로 SPI MISO를 디코드한 바이트와 드라이버가 받은 바이트를 비교한다. 선 위가 이미 틀렸으면 HW(또는 센서 설정), 선 위는 맞는데 메모리가 틀렸으면 SW(DMA·캐시·레이스). Don이 RF 칩 bring-up에서 RFFE/SPMI 트랜잭션을 LA로 보던 방식 그대로다(M11 §6: 커널 i2c tracepoint와 LA 디코드를 같은 시간축에).
- 깨진 프레임의 **도착 시각**을 GPIO로 표시하고 LA에서 그 순간의 버스를 본다 — 트리거가 "깨짐" 자체이므로 몇 시간짜리 간헐도 잡힌다.
- 온도 챔버·케이블 흔들기로 빈도가 바뀌면 HW 쪽 가설이 강해지고, `isolcpus`나 부하 변경으로 빈도가 바뀌면 SW 쪽(타이밍 의존 레이스)이다.

### 2.5 실제로 본 원인들

| 증상 | 원인 | 근거 |
|---|---|---|
| DMA "성공", IRQ도 옴, 데이터만 틀림 | DMA mask를 장치 능력보다 크게 주장 | H9 §5.3 |
| `DMA-API: ... different direction` | map은 TO_DEVICE, sync는 FROM_DEVICE | H9 §9.2 |
| 크래시 지점과 버그 지점이 다름 | 리스트 손상이 다른 곳에서 터짐 | M3 §9.4 |
| CAN 필터를 걸었는데 엉뚱한 프레임 수신 | 29-bit 프레임이 마스크를 통과(EFF 비트 미지정) | Q4 §4.4 |
| 프레임 seq 갭·delta 들쭉날쭉 | 드롭 또는 타임스탬프를 찍는 IRQ 지연 | Q2 §5.3 |

### 2.6 말로 하는 답

**30초**: "먼저 깨진 바이트를 직접 보겠다 — 오프셋, 크기, 패턴. 캐시 라인이나 페이지 경계에 맞춘 덩어리면 DMA·캐시, 부하 때만이면 레이스, 특정 보드·온도면 신호 무결성이다. 그리고 간헐이니 증거부터 심는다: seq와 CRC, 깨진 순간 GPIO 펄스로 LA 트리거."

> Intermittent corruption is a classification problem first, so I want to see the bad bytes: offset, length, pattern. If the damage is cache-line or page aligned, I suspect DMA ownership — a missing dma_sync_for_cpu on a non-coherent SoC, or a device whose DMA mask is smaller than what the driver claims, which silently truncates addresses and only shows up on SKUs with more RAM. If it only happens under load, I suspect a race or ring-buffer overrun and run the same load on a debug kernel with KCSAN, KASAN, lockdep and DMA_API_DEBUG. If it tracks a board, temperature or cable, it's signal integrity and I go to bus error counters and the scope. Because it's rare, step zero is instrumentation: sequence numbers and a CRC in the frame, and a GPIO pulse on CRC failure so the logic analyzer triggers on the corruption itself. Then I compare the bytes on the wire with the bytes in memory — that's the hardware/software boundary in one capture.

**꼬리 질문·함정**:

- "How do you know it's not the sensor itself?" → 선 위 바이트(LA 디코드)와 메모리 바이트 비교. 선 위가 맞으면 센서 무죄.
- 함정: KASAN·KCSAN 커널은 타이밍을 바꿔서 레이스가 **사라질** 수 있다(M8 비용 실측: 할당 경로 13~16배). 안 나오면 "없다"가 아니라 "이 타이밍에선 안 나온다"다.
- 함정: `volatile`이나 `mb()`를 여기저기 넣어 증상이 사라지면 고친 게 아니다 — 소유권 규칙(H9 §3)을 지키는 코드로 바꾼다.

---

## 3. 시나리오 3 — "부팅 중 멈춘다"

### 3.1 면접관의 질문

> "A unit in the field hangs during boot. Sometimes it's every boot on a new board. Where do you start?"

### 3.2 먼저 묻는 질문

- **콘솔에 마지막으로 찍힌 줄이 무엇인가?** (이 질문 하나가 트리의 가지를 고른다)
- 매번인가 가끔인가? 콜드 부팅만인가(전원·DDR 트레이닝), 웜 리셋도인가?
- 최근 바뀐 것: 부트로더·DT·커널·rootfs·OTA?
- 시리얼 콘솔·JTAG 접근이 되나? 필드 장비면 pstore/ramoops가 있나(L7)?

### 3.3 진단 트리 — 마지막 줄로 단계를 정한다

```svg
<svg viewBox="0 0 680 500" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">"부팅 중 멈춘다" — 콘솔의 마지막 줄이 어느 단계인지 정한다</text>
<rect x="10" y="40" width="230" height="58" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="125.0" y="73.2" font-size="12" text-anchor="middle">아무것도 안 나옴</text>
<rect x="270" y="40" width="400" height="58" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="470.0" y="65.2" font-size="12" text-anchor="middle">전원·클럭·리셋·부트 모드 핀</text>
<text x="470.0" y="81.2" font-size="12" text-anchor="middle">UART 핀먹스 · JTAG attach (P1 §1)</text>
<line x1="240" y1="69" x2="268" y2="69" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<line x1="125" y1="98" x2="125" y2="112" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="10" y="112" width="230" height="58" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125.0" y="145.2" font-size="12" text-anchor="middle">Starting kernel 후 침묵</text>
<rect x="270" y="112" width="400" height="58" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="470.0" y="137.2" font-size="12" text-anchor="middle">earlycon=pl011,0x… 처럼 주소 명시</text>
<text x="470.0" y="153.2" font-size="12" text-anchor="middle">DT memory · JTAG + lx-dmesg (P1 §4.2)</text>
<line x1="240" y1="141" x2="268" y2="141" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<line x1="125" y1="170" x2="125" y2="184" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="10" y="184" width="230" height="58" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125.0" y="217.2" font-size="12" text-anchor="middle">커널 로그가 중간에 멈춤</text>
<rect x="270" y="184" width="400" height="58" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="470.0" y="209.2" font-size="12" text-anchor="middle">initcall_debug의 마지막 initcall</text>
<text x="470.0" y="225.2" font-size="12" text-anchor="middle">타임스탬프 0.000 고정 = 타이머 (P1 §4.1)</text>
<line x1="240" y1="213" x2="268" y2="213" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<line x1="125" y1="242" x2="125" y2="256" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="10" y="256" width="230" height="58" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="125.0" y="289.2" font-size="12" text-anchor="middle">VFS panic / Waiting for root</text>
<rect x="270" y="256" width="400" height="58" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="470.0" y="281.2" font-size="12" text-anchor="middle">root= 오타 · 스토리지 드라이버 =m</text>
<text x="470.0" y="297.2" font-size="12" text-anchor="middle">rootwait 상한 (C6 §8)</text>
<line x1="240" y1="285" x2="268" y2="285" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<line x1="125" y1="314" x2="125" y2="328" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="10" y="328" width="230" height="58" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="125.0" y="361.2" font-size="12" text-anchor="middle">systemd가 start job에서 대기</text>
<rect x="270" y="328" width="400" height="58" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="470.0" y="353.2" font-size="12" text-anchor="middle">"A start job is running" → emergency</text>
<text x="470.0" y="369.2" font-size="12" text-anchor="middle">journalctl -b · rescue (§3.6 실습)</text>
<line x1="240" y1="357" x2="268" y2="357" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<line x1="125" y1="386" x2="125" y2="400" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<rect x="10" y="400" width="230" height="58" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="125.0" y="433.2" font-size="12" text-anchor="middle">셸은 뜨는데 제품이 안 뜸</text>
<rect x="270" y="400" width="400" height="58" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="470.0" y="425.2" font-size="12" text-anchor="middle">systemctl --failed · health check</text>
<text x="470.0" y="441.2" font-size="12" text-anchor="middle">bootcount 롤백 (L1, L4)</text>
<line x1="240" y1="429" x2="268" y2="429" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/>
<text x="10" y="488" font-size="12" text-anchor="start">왼쪽 위 칸일수록 계측기(전원 레일·JTAG)가, 아래 칸일수록 로그·트레이스가 주 도구다.</text>
</svg>
```
그림 4 — 부팅 멈춤은 "어느 단계에서"가 전부다. 위 칸일수록 계측기, 아래 칸일수록 로그·트레이스가 주 도구다. 단계 지도는 C1·P1 §1과 같다.

### 3.4 단계별 명령과 근거

| 마지막 줄 | 먼저 할 것 | 실제로 본 것 (근거) |
|---|---|---|
| 아무것도 없음 | 전원 레일·리셋·부트 모드 핀, UART 핀먹스, JTAG attach | P1 §1.1–1.5 |
| U-Boot 뒤 "Starting kernel" 후 침묵 | `earlycon=pl011,0x9000000`처럼 **드라이버와 주소를 명시**, 안 되면 JTAG + `lx-dmesg` | P1 §4.2: DT memory가 실제보다 큼 → `paging_init()` 중 synchronous external abort. 맨이름 `earlycon`은 ACPI SPCR 커널에서 그보다 늦게 등록돼 한 글자도 안 나왔다 |
| 커널 로그 중간에서 멈춤 | `initcall_debug`로 마지막 initcall, `ftrace_graph_filter=` 부팅 트레이스 | P1 §4.1: 루트 `interrupt-parent` 누락 → `arch_timer: No interrupt available` → 타임스탬프가 끝까지 `0.000000`. gdb로 보니 커널은 살아 있고 유저가 `__clock_gettime`을 반복 |
| `VFS: Unable to mount root fs` / `Waiting for root device` | `root=`·파티션 목록·스토리지 드라이버가 `=m`인지 | C6 §8: `rootwait=3` 상한이 정확히 3초 뒤 panic |
| systemd 단계에서 대기 | `A start job is running for …`, emergency 셸에서 `journalctl -b` | §3.6 (새로 재현) |
| 셸은 뜨는데 제품 서비스 없음 | `systemctl --failed`, `systemd-analyze critical-chain` | L1 §10, M10 §7 |

부팅 중 probe가 오래 걸리는 경우(`msleep` 2초)는 멈춤처럼 보인다 — `initcall_debug`의 `after 2010432 usecs` 같은 줄이 증거다(M10 §5, §12 장면 3).

### 3.5 HW냐 SW냐 가르기

- **JTAG로 PC를 세 번 읽는다.** 같은 곳이면 무한 루프/대기, 예외 벡터면 fault, EL0 주소면 커널은 살아 있다(P1 §4.1에서 `ESR_EL1 = 0x56000000` = SVC, `SPSR = EL0`). 이것은 Don이 SSD 컨트롤러에서 하던 "hang 시 PC 샘플링"과 같다.
- 콜드 부팅에서만 멈추면 전원 시퀀싱·DDR 트레이닝(HW·부트로더) 쪽, 웜 리셋에서도 멈추면 SW 쪽 가설이 강해진다.
- 전원 레일(scope CH1)과 부트 단계 GPIO 펄스(CH2)를 같이 찍으면 "커널 타임스탬프 0초"가 아니라 **전원 기준 절대 시간**으로 어디서 멈췄는지 본다(M11 §9.3).

### 3.6 랩 재현 — systemd가 없는 장치를 기다리다 emergency로

무엇을 확인하나: 커널은 다 떴고 루트 FS도 마운트됐는데 "부팅이 안 끝난다"는 필드 보고의 흔한 원인 — fstab의 데이터 파티션(예: `/data`)이 없거나 라벨이 바뀜. rootfs 이미지를 고치지 않고 커널 커맨드라인의 `systemd.mount-extra=`(systemd 254부터라고 기억 — 확인 필요. 랩 rootfs의 systemd 255에서 동작 확인)로 fstab 한 줄을 흉내 냈다. `Image-sec` + rootfs-sysd(qcow2 overlay), `-smp 2 -m 1G`.

```sh
qemu-img create -f qcow2 -F raw -b /var/lab/out/rootfs-sysd.ext4 rb.qcow2
qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G -nographic -kernel /var/lab/out/Image-sec \
  -drive file=rb.qcow2,if=virtio,format=qcow2 \
  -append "console=ttyAMA0 root=/dev/vda rw rootwait systemd.mount-extra=LABEL=R4DATA:/data/r4:ext4:x-systemd.device-timeout=20s"
```

콘솔(색 코드 제거, 진행 표시줄 반복은 `...`로 줄임):

```text
         Expecting device dev-disk-by\x2dla…vice - /dev/disk/by-label/R4DATA...
...
[    *] Job dev-disk-by\x2dlabel-R4DATA.device/start running (19s / 20s)
[ TIME ] Timed out waiting for device dev-d…device - /dev/disk/by-label/R4DATA.
[DEPEND] Dependency failed for data-r4.mount - /data/r4.
[DEPEND] Dependency failed for local-fs.target - Local File Systems.
[DEPEND] Dependency failed for systemd-fsck…Check on /dev/disk/by-label/R4DATA.
...
[  OK  ] Started emergency.service - Emergency Shell.
[  OK  ] Reached target emergency.target - Emergency Mode.
...
You are in emergency mode. After logging in, type "journalctl -xb" to view
system logs, "systemctl reboot" to reboot, or "exit"
to continue bootup.
Press Enter for maintenance
```

`expect`로 Enter를 보내 maintenance 셸에서 진단했다:

```text
root@labsysd:~# systemctl is-system-running; systemctl list-units --state=failed --no-legend --no-pager
maintenance
root@labsysd:~# cat /run/systemd/generator/data-r4.mount
# Automatically generated by systemd-fstab-generator

[Unit]
Documentation=man:fstab(5) man:systemd-fstab-generator(8)
SourcePath=/proc/cmdline
Before=local-fs.target
Requires=systemd-fsck@dev-disk-by\x2dlabel-R4DATA.service
After=systemd-fsck@dev-disk-by\x2dlabel-R4DATA.service
After=blockdev@dev-disk-by\x2dlabel-R4DATA.target

[Mount]
What=/dev/disk/by-label/R4DATA
Where=/data/r4
Type=ext4
root@labsysd:~# systemctl show -p RequiredBy -p ActiveState -p Result data-r4.mount --no-pager
Result=success
RequiredBy=local-fs.target
ActiveState=inactive
root@labsysd:~# journalctl -b --no-pager --no-hostname -o short-monotonic -p warning | grep -E 'R4DATA|r4' | tail -4
[   23.084446] systemd[1]: dev-disk-by\x2dlabel-R4DATA.device: Job dev-disk-by\x2dlabel-R4DATA.device/start timed out.
[   23.098608] systemd[1]: Timed out waiting for device dev-disk-by\x2dlabel-R4DATA.device - /dev/disk/by-label/R4DATA.
[   23.102251] systemd[1]: Dependency failed for systemd-fsck@dev-disk-by\x2dlabel-R4DATA.service - File System Check on /dev/disk/by-label/R4DATA.
[   23.104202] systemd[1]: Dependency failed for data-r4.mount - /data/r4.
root@labsysd:~# lsblk -f -o NAME,FSTYPE,LABEL,MOUNTPOINT
NAME
    FSTYPE LABEL    M
vda ext4   sysdroot /
```

(`lsblk`의 줄바꿈이 어긋난 것은 시리얼 터미널 폭 때문이다 — 내용은 "디스크는 `vda` 하나, 라벨 `sysdroot`".) 출력에서 볼 것:

- **`systemctl --failed`(여기서는 `--state=failed`)가 비어 있다.** 의존성 실패는 unit을 `failed`로 만들지 않는다 — `data-r4.mount`는 `ActiveState=inactive`, `Result=success`다. "failed 목록이 비었으니 문제없다"는 함정이다. 상태는 `is-system-running` = `maintenance`가 말해 준다.
- 진짜 원인은 장치 job의 **timeout**이고(`device-timeout=20s`, 기본값은 90초 — 필드 장비가 "1분 반 멈췄다가 emergency"로 보이는 이유), unit의 출처는 `SourcePath=`(여기선 `/proc/cmdline`, 실제 장비에선 `/etc/fstab`)가 알려 준다.
- `RequiredBy=local-fs.target` — `nofail`이 없는 fstab 줄은 local-fs.target의 필수 조건이 된다. 데이터 파티션은 `nofail`(+ 앱이 마운트 여부 확인) 또는 `x-systemd.automount`로 두는 것이 제품 설계다(L1, L4 §13.1).
- 장치는 `lsblk`에 라벨 `R4DATA`가 **없다**. 실제 장비라면 여기서 "eMMC 파티션이 안 보이나(커널·DT), 라벨이 바뀌었나(OTA·공장 이미지)"로 갈린다.
- 이 셸은 rootfs-sysd가 root 비밀번호 없이 autologin하게 만든 랩 설정이라 열렸다. 양산 장비는 emergency 셸이 잠겨 있는 경우가 많다 — 그래서 원격 로그·pstore(L7)가 필요하다.

### 3.7 말로 하는 답

**30초**: "콘솔에 마지막으로 찍힌 줄부터 묻겠다. 그게 단계를 정한다 — 아무것도 없으면 전원·클럭·부트 모드, 'Starting kernel' 뒤 침묵이면 earlycon을 주소까지 명시하고 JTAG, 커널 중간이면 initcall_debug, rootfs면 root=와 드라이버, systemd면 start job과 journal."

> The first question is what the last line on the console was, because that picks the stage. Nothing at all: power, clocks, boot-mode straps, UART pinmux — and I attach JTAG. Silence right after 'Starting kernel': I boot with earlycon given explicitly as driver and address, since bare earlycon can register too late, and if it's still silent I halt with JTAG and dump the log buffer with lx-dmesg; I've seen a DT memory node larger than the real DRAM cause a synchronous external abort that never reached the console. Stuck mid-kernel: initcall_debug to find the last initcall. Rootfs: root= versus the partition list, and whether the storage driver is a module. In systemd: 'A start job is running', then journalctl -b from the emergency shell — and note that a dependency failure doesn't show in systemctl --failed. If it hangs only on cold boot, I lean toward power sequencing or DDR training.

**꼬리 질문·함정**:

- "The unit is in the field — no console." → pstore/ramoops로 직전 부팅의 콘솔·panic 저장(L7 §4), bootcount로 자동 롤백(L4), 원격 journal 업로드(L7 §8).
- 함정: `quiet`가 켜진 제품 커맨드라인에서는 커널 로그가 거의 안 나와 "멈춤 위치"를 잘못 읽는다. 디버그 부팅은 `quiet`를 빼고 `ignore_loglevel`.
- 함정: 정식 콘솔은 printk 버퍼를 재생하므로 "앞부분이 다 보이네" 하고 안심하기 쉽다. probe 전에 죽는 버그는 earlycon만 잡는다(P1 §4.3).

---

## 4. 시나리오 4 — "카메라 프레임이 떨어진다"

### 4.1 면접관의 질문

> "Our 30 fps camera pipeline drops frames now and then — maybe 2%. How do you find out where?"

### 4.2 먼저 묻는 질문

- "드롭"을 어떻게 셌나? 앱의 fps인가, V4L2 버퍼의 `sequence` 갭인가? (측정 방법이 틀린 경우가 많다)
- 카메라 몇 대, 해상도·포맷, 버퍼 몇 개? 소비자는 무엇인가(GStreamer, ROS 2, 자체 앱)?
- 드롭이 주기적인가(예: 매 10초 — 로깅·GC), 부하와 같이 오나?
- 부팅 직후부터인가, 온도가 오른 뒤인가?

### 4.3 진단 트리

1. **측정을 바로잡는다**: `v4l2-ctl --stream-mmap=N --verbose`의 `seq` 열로 갭을 센다. 드라이버가 매 프레임 `sequence`를 증가시키므로 **갭 = 드라이버가 알고 있는 손실**이다(Q2 §5.2–5.4, `q2_seqmon`).
2. **버퍼 수를 바꿔 본다** — 생산자 쪽이냐 소비자 쪽이냐를 가르는 가장 싼 실험. Q2 §5.3 실측:

```text
slow consumer(sleep 300ms every 30) bufs=2 frames=150 seq=0..180 gaps=7 lost=31 loss=17.1%
slow consumer(sleep 300ms every 30) bufs=8 frames=150 seq=0..153 gaps=4 lost=4 loss=2.6%
inject 20% bufs=8 frames=150 seq=0..194 gaps=34 lost=45 loss=23.1%
```

(Q2 노트의 vivid 실험 출력 인용.) 버퍼를 늘려 손실이 줄면 **소비자가 가끔 늦다**(우선순위, 복사, 처리 스레드). 늘려도 그대로면 **생산자가 버린다**(센서·CSI-2·ISP·드라이버).
3. **소비자 쪽**: 처리 스레드의 스케줄링 지연(§6의 도구), 메모리 복사(DMABUF zero-copy, Q2 §5.6), GStreamer `fpsdisplaysink`와 "lost frames detected" 경고(Q2 §9.2).
4. **생산자 쪽**: CSI-2 수신기의 에러 카운터(ECC/CRC, FIFO overflow — SoC 드라이버의 debugfs·dmesg), ISP overflow IRQ, DDR 대역폭·IOMMU fault, 센서 자체의 프레임 카운터 레지스터.
5. **파이프라인이 아예 안 뜨면**: `media-ctl -p`로 그래프와 각 pad 포맷을 보고, STREAMON의 `-EPIPE`(Broken pipe)는 링크 양 끝 포맷 불일치다(Q2 §3.2: dyndbg로 원인 확인).

### 4.4 HW냐 SW냐 가르기

- 센서의 frame valid / FSIN·XVS 핀(또는 GMSL 직렬화기의 frame sync)을 LA에 걸고, 드라이버의 frame-end IRQ에 디버그 GPIO 펄스. **센서 펄스 수 = IRQ 펄스 수**인데 seq가 비면 드라이버·버퍼 큐(SW), 센서 펄스는 있는데 IRQ가 없으면 수신기 단계(CSI-2 링크, HW 또는 수신기 설정)다. M11 §4.4의 "핀 개수를 세면 어디서 사라졌는지 보인다"와 같은 방법이다.
- 온도와 같이 오면 열(센서가 frame rate를 낮추는 thermal throttling, SerDes 링크 마진). Q1 §10.3 장면 3, Scout 기술 면접 주제와 연결된다.

### 4.5 실제로 본 원인들

| 증상 | 원인 | 근거 |
|---|---|---|
| 버퍼 2개에서 17% 손실, 8개에서 2.6% | 소비자의 주기적 정지 | Q2 §5.3 |
| 버퍼를 늘려도 23% 그대로 | 생산자(드라이버)가 버림 | Q2 §5.3 (`percentage_of_dropped_buffers` 주입) |
| STREAMON이 `Broken pipe` | pad 포맷 불일치 | Q2 §3.2–3.5 |
| libcamera가 카메라 0개 | udev 없음 / IPA 모듈 없음 | Q2 §8.2–8.3 |
| 센서 probe 중 Oops | `link-frequencies` 셀 크기 실수 | Q1 §8.6 |

### 4.6 말로 하는 답

**30초**: "먼저 드롭을 V4L2 sequence 갭으로 다시 세겠다. 그다음 버퍼 수를 바꿔 본다 — 손실이 줄면 소비자가 늦은 것이고, 그대로면 센서·CSI·드라이버가 버리는 것이다. 그다음 센서 frame 핀과 드라이버 IRQ에 GPIO를 걸어 LA로 펄스 수를 비교한다."

> First I'd re-measure using the V4L2 buffer sequence numbers, because app-level fps counters lie. Then the cheapest discriminating experiment: change the number of capture buffers. In a lab run, a consumer that stalls 300 ms every 30 frames lost 17% with two buffers and 2.6% with eight; drops injected by the driver stayed at 23% regardless. If more buffers help, the consumer is late — scheduling, copies, priority — and I look at the processing thread with tracing and move to DMABUF. If not, the producer is dropping, and I check the CSI-2 receiver's error and overflow counters, ISP interrupts and memory bandwidth. To split hardware and software I put the sensor's frame-sync pin and a debug GPIO in the frame-end IRQ on a logic analyzer and compare pulse counts.

**꼬리 질문·함정**: "Why not just add 16 buffers?" → 메모리(1080p NV12 약 3 MB씩, CMA)와 **지연**을 산다. 제어 루프엔 오래된 프레임이 더 나쁠 수 있다(Q2 §5.3 뒤의 설명). 함정: sequence 비교는 32비트 wrap을 고려해 `(u32)(s - p)`로.

---

## 5. 시나리오 5 — "CAN 메시지가 사라진다 / bus-off"

### 5.1 면접관의 질문

> "The motor controller sometimes doesn't react to our CAN commands. Occasionally the interface goes bus-off. Debug it."

### 5.2 먼저 묻는 질문

- 사라진 것이 **송신**인가 **수신**인가? `write()`는 성공했나?
- 다른 노드(예: PCAN 동글)에서 버스를 보면 프레임이 선 위에 있나?
- bitrate·샘플 포인트는 모든 노드가 같나? 버스 길이·종단은?
- bus-off 후 자동 복구(`restart-ms`) 설정은?

### 5.3 진단 트리

1. **상태와 카운터부터** — 가장 싸다.

```sh
ip -details -statistics link show can0    # state, restart-ms, bus-errors, error-pass, bus-off, TX dropped
candump -t d -e "can0,0~0,#FFFFFFFF"      # 에러 프레임만 (0~0 = 데이터 프레임 0개)
```

(가이드 — 실제 출력은 Q4 §8.4.)
2. **송신 쪽 손실**: `write()`가 `ENOBUFS`면 TX 큐(qlen 10)가 찼다 — 버스 부하나 bus-off(Q4 §9.3). `write()`가 **성공했는데** 안 나갔다면 carrier가 꺼진 상태다 — 아래 사례.
3. **수신 쪽 손실**: 인터페이스 RX 통계는 정상인데 앱만 잃으면 소켓 버퍼(`SO_RCVBUF`, `SO_RXQ_OVFL`), 컨트롤러 FIFO overflow 에러 프레임(`CAN_ERR_CRTL_RX_OVERFLOW`)이면 IRQ/NAPI 지연·SPI 컨트롤러 속도(mcp251xfd), 필터 마스크 실수(Q4 §4.4).
4. **에러 프레임 분류**: ACK 에러만 → 상대 노드 없음·트랜시버 standby, bit/stuff/CRC 간헐 → 종단·샘플 포인트, bus-off 반복 → bitrate 불일치·단락(Q4 §10.4 표).

### 5.4 HW냐 SW냐 가르기

- **전원 끄고 CAN_H–CAN_L 저항**: 약 60 Ω이 정상(120 Ω 두 개). 120 Ω = 종단 하나 빠짐, 40 Ω = 셋(Q4 §10.1).
- 트랜시버 RXD를 LA로 디코드: ACK 슬롯이 recessive면 아무도 ACK 안 함, 비트 폭이 2.000 µs(500 k)에서 벗어나면 클럭·bitrate. 내 노드만 bitrate가 다르면 디코더에 "깨진 프레임 + 에러 플래그"(Q4 §10.3).
- 트랜시버 STB 핀 극성: DT의 `GPIO_ACTIVE_LOW` 실수로 **인터페이스는 up인데 트랜시버는 standby** → ACK 에러 → error-passive → bus-off. 증상이 "가끔 되는 것 같다"로 보고되기 쉽다(P4 §8.3, ftrace `gpio_value`로 확인).

### 5.5 실제로 본 원인 — slcan의 "복구된 척"

Q4 §8.4에서 slcan + MCU로 ACK 에러 → passive → bus-off → MCU 쪽 복구를 재현했다. 핵심 출력(인용):

```text
5: q4sl0: <NO-CARRIER,NOARP,UP> mtu 16 qdisc pfifo_fast state DOWN mode DEFAULT group default qlen 10
    can state ERROR-ACTIVE restart-ms 0 
send rc=0
    TX:  bytes packets errors dropped carrier collsns           
             3       3      1       1       0       0 
```

상태는 ERROR-ACTIVE로 돌아왔는데 carrier는 꺼진 채라 `cansend`는 **rc=0인데 프레임은 버려졌다**(TX dropped 1). `slcan_bump_state()`가 상태만 바꾸고 `netif_carrier_on()`을 안 부르기 때문이고, `ip link set q4sl0 type can restart-ms 100`은 `Device doesn't support restart from Bus Off.`로 거절됐다(slcan에 `do_set_mode` 없음). "보냈는데 모터가 안 움직인다"의 정확한 모습이다. 수정은 앱이 상태 변화를 감시해 `ip link down/up`으로 다시 열거나, bus-off 복구를 지원하는 어댑터를 쓰는 것(Q4 §12.2 감시 코드).

### 5.6 말로 하는 답

**30초**: "송신인지 수신인지부터 가르겠다. `ip -details -statistics`로 상태·에러 카운터·TX dropped, `candump -e`로 에러 프레임 종류. ACK 에러면 상대·트랜시버, 간헐 CRC면 종단·샘플 포인트, bus-off 반복이면 bitrate. 그리고 전원 끄고 60옴을 잰다."

> I'd first separate transmit from receive loss. ip -details -statistics gives me the controller state, error-passive and bus-off counts, restart-ms and TX drops; candump with an error-only filter tells me which error class. Only ACK errors means nobody is acknowledging — other node off, or our transceiver in standby, which I've seen caused by an inverted STB polarity in the device tree. Intermittent CRC or stuff errors on a long harness point to termination or sample-point mismatch, so I measure 60 ohms with power off and decode RXD on a logic analyzer. Repeated bus-off is usually a bitrate mismatch. And I check recovery behavior: on one adapter the controller came back to error-active but the netdev carrier stayed off, so send() returned success while frames were dropped — the app has to watch state and reopen.

**꼬리 질문·함정**: "How does bus-off recovery work?" → TEC ≥ 256이면 bus-off, 노드는 recessive 11비트 × 128회를 본 뒤 복귀; Linux는 `restart-ms`로 `can_restart()`를 예약하거나 수동 `ip link set can0 type can restart`(I10, Q4 그림 6). 함정: `restart-ms`를 짧게 두면 bitrate 불일치 노드가 버스를 계속 오염시킨다 — 복구 정책은 시스템 설계 문제(R5).

---

## 6. 시나리오 6 — "지연이 가끔 튄다"

### 6.1 면접관의 질문

> "Our 1 kHz control loop runs fine, but a few times an hour a cycle is 3 ms late. How do you find the cause?"

### 6.2 먼저 묻는 질문

- 커널은 PREEMPT_RT인가? 루프는 SCHED_FIFO·`mlockall`·CPU 고정인가(N2)?
- "3 ms 늦다"를 어떻게 쟀나 — 루프 자신의 시계인가, 외부 GPIO+scope인가?
- 스파이크가 주기적인가(로그 로테이션, 크론, 열 관리 데몬), 특정 동작과 같이 오나(카메라 시작, 네트워크 부하, 플래시 쓰기)?
- 하이퍼바이저·TrustZone 펌웨어 호출이 있나(SCMI·PSCI, DVFS)?

### 6.3 진단 트리

1. **측정을 먼저 표준화**: `cyclictest`(백분위·max, 충분히 긴 시간 — N3 §5: max는 측정 시간에 따라 계속 커진다)와 부하(`stress-ng`, 실제 제품 부하).
2. **IRQ 단계냐 thread 단계냐**: `rtla timerlat top -a <us>` — 임계값에서 멈추고 auto-analysis가 "IRQ latency"(누가 IRQ를 껐나)와 "Blocking thread"(누가 CPU를 쥐었나)로 나눠 준다(N3 §7.4: IRQ off 10 ms 스파이크를 `n3_burn` 스택으로 지목).
3. **IRQ off·preempt off 구간 범인 찾기**: `irqsoff`/`preemptoff` tracer가 최대 구간의 시작·끝 함수와 스택을 저장한다 — §6.6에서 새로 재현.
4. **스케줄링**: `sched_switch`·`sched_wakeup` 이벤트로 "그 순간 같은 CPU에서 누가 돌았나"(M5), `perf sched latency`(M6).
5. **OS 밖**: auto-analysis가 범인을 못 짚으면 `rtla hwnoise`/`hwlat`로 OS가 못 보는 정지(EL3 펌웨어, SMI류, DVFS)를 확인한다(N3 §8, §13.3).

### 6.4 HW냐 SW냐 가르기

**소프트웨어 자체 측정은 자기가 멈춘 시간을 못 잴 수 있다**(N3 §13.4). 최종 확인은 GPIO: 루프 시작에 핀을 올리고 끝에 내려 scope로 주기와 지터를 본다(M11 §4). IMU DRDY 핀 → hardirq 핀 → thread 핀을 단계별로 찍으면 "DRDY→HARD는 항상 8 µs, HARD→THREAD가 가끔 1.9 ms"처럼 어느 구간이 튀는지 숫자로 나온다(M11 §9.1). §6.6의 결과가 이 원칙을 랩에서 보여 준다.

### 6.5 실제로 본 원인들

| 증상 | 원인 | 근거 |
|---|---|---|
| IRQ latency 9879 µs | 드라이버 코드가 IRQ를 끄고 busy wait | N3 §7.4 (`n3_burn`) |
| Thread latency 20 ms 중 74%가 한 kthread | preempt off 구간 | N3 §7.4 |
| 격리 CPU에 가끔 50 µs | 남은 IRQ affinity·타이머·RCU | N2 §6, §13.2 |
| HARD→THREAD 가끔 1.9 ms | 더 높은 FIFO 우선순위 스레드(카메라) | M11 §9.1 |

### 6.6 랩 재현 — 유저 루프가 본 스파이크와 irqsoff tracer가 본 범인

무엇을 확인하나: (1) 1 ms 주기 루프가 lateness를 스스로 기록하고, (2) 업스트림 테스트 모듈 `kernel/trace/preemptirq_delay_test.c`(지연 tracer 검증용, 수정 없이 out-of-tree 빌드)로 "IRQ를 4000 µs 끄는 드라이버"를 흉내 내고, (3) `irqsoff` tracer가 범인을 이름으로 짚는지. `Image-trace`, `-smp 1`(같은 CPU에서 충돌하게).

루프 프로그램(`gcc -std=gnu11 -Wall -Wextra -O2 -static`, 경고 0):

```c
/* r4_lat: 1 ms periodic loop (absolute deadlines), reports lateness spikes */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static long long ns(const struct timespec *t)
{
	return (long long)t->tv_sec * 1000000000LL + t->tv_nsec;
}

int main(int argc, char **argv)
{
	int loops = argc > 1 ? atoi(argv[1]) : 5000;
	long long thr_us = argc > 2 ? atoll(argv[2]) : 1000;
	struct timespec next, now;
	long long max = 0, sum = 0;
	int over = 0;

	if (clock_gettime(CLOCK_MONOTONIC, &next)) { perror("clock_gettime"); exit(1); }
	for (int i = 0; i < loops; i++) {
		next.tv_nsec += 1000000;
		if (next.tv_nsec >= 1000000000) { next.tv_nsec -= 1000000000; next.tv_sec++; }
		if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL)) {
			perror("clock_nanosleep"); exit(1);
		}
		clock_gettime(CLOCK_MONOTONIC, &now);
		long long late = (ns(&now) - ns(&next)) / 1000;
		sum += late;
		if (late > max) max = late;
		if (late > thr_us && over++ < 5)
			printf("spike #%d at mono=%lld.%06lld s: late %lld us\n", over,
			       ns(&now) / 1000000000LL, (ns(&now) % 1000000000LL) / 1000, late);
	}
	printf("loops=%d avg=%lld us max=%lld us over%lldus=%d\n",
	       loops, sum / loops, max, thr_us, over);
	return 0;
}
```

게스트 스크립트:

```sh
mount -t tracefs none /sys/kernel/tracing; cd /sys/kernel/tracing
echo mono > trace_clock; echo 0 > options/function-trace; echo irqsoff > current_tracer
sleep 3; echo "== baseline (no culprit), 3000 loops"; echo 0 > tracing_max_latency
/r4_lat 3000 1000; echo "irqsoff max = $(cat tracing_max_latency) us"
echo "== with culprit: upstream preemptirq_delay_test, IRQs off 4000 us, retriggered"
insmod /preemptirq_delay_test.ko test_mode=irq delay=4000 burst_size=1
echo 0 > tracing_max_latency
/r4_lat 3000 1000 &
for i in 1 2 3 4; do sleep 0.5; echo 1 > /sys/kernel/preemptirq_delay_test/trigger; done
wait
echo "irqsoff max = $(cat tracing_max_latency) us"; grep -v "^#$" trace | head -40
```

(`options/function-trace`를 끄면 irqsoff tracer가 구간 안의 모든 함수 대신 시작·끝·스택만 남긴다 — TCG에서 함수 트레이스는 너무 무겁다.) 두 번째 실행 결과:

```text
== baseline (no culprit), 3000 loops
spike #1 at mono=5.345328 s: late 1036 us
spike #2 at mono=5.352235 s: late 6944 us
spike #3 at mono=5.353157 s: late 6866 us
spike #4 at mono=5.353906 s: late 6615 us
spike #5 at mono=5.354654 s: late 6363 us
loops=3000 avg=626 us max=24435 us over1000us=61
irqsoff max = 715 us
== with culprit: upstream preemptirq_delay_test, IRQs off 4000 us, retriggered
spike #1 at mono=8.804845 s: late 3382 us
spike #2 at mono=8.806893 s: late 4429 us
spike #3 at mono=8.807767 s: late 4304 us
spike #4 at mono=8.811734 s: late 7270 us
spike #5 at mono=8.813099 s: late 7635 us
loops=3000 avg=470 us max=11888 us over1000us=73
irqsoff max = 4014 us
# tracer: irqsoff
# irqsoff latency trace v1.1.5 on 6.18.55-trace
# --------------------------------------------------------------------
# latency: 4014 us, #4/4, CPU#0 | (M:PREEMPT VP:0, KP:0, SP:0 HP:0 #P:1)
#    -----------------
#    | task: irq_test-77 (uid:0 nice:0 policy:0 rt_prio:0)
#    -----------------
#  => started at: execute_preemptirqtest
#  => ended at:   preemptirqtest_0
...
```

(헤더의 열 설명 10줄은 `...`로 생략.) 첫 번째 실행의 trace 본문은 이랬다(같은 설정, 4010 µs):

```text
irq_test-78        0d....    1us#: trace_hardirqs_off <-execute_preemptirqtest
irq_test-78        0d.... 4009us : execute_preemptirqtest <-preemptirqtest_0
irq_test-78        0d.... 4016us+: tracer_hardirqs_on <-preemptirqtest_0
irq_test-78        0d.... 4031us : <stack trace>
 => preemptirqtest_0
 => preemptirq_delay_run
 => kthread
 => ret_from_fork
```

세 번 실행한 요약(run 1은 스크립트의 앞 버전 — 시작 `sleep 3` 없이, trigger 2회를 1초 간격으로 — 이고 run 2·3이 위 스크립트다):

| 실행 | 유저 루프 max, 범인 없음 | 유저 루프 max, 범인 있음 | irqsoff max, 범인 없음 | irqsoff max, 범인 있음 |
|---|---|---|---|---|
| run 1 | 27452 µs | 4593 µs | 631 µs | 4010 µs |
| run 2 | 24435 µs | 11888 µs | 715 µs | 4014 µs |
| run 3 | 4622 µs | 21877 µs | 1727 µs | 4006 µs |

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">같은 스파이크, 두 관찰자 — QEMU TCG 3회 (µs, 막대 위 숫자는 실측)</text>
<text x="200" y="48" font-size="12" text-anchor="middle" font-weight="bold">유저 루프 최대 지연 (r4_lat)</text>
<line x1="60" y1="260" x2="350" y2="260" stroke="currentColor"/>
<line x1="60" y1="60" x2="60" y2="260" stroke="currentColor"/>
<line x1="56" y1="260.0" x2="60" y2="260.0" stroke="currentColor"/>
<text x="54" y="264.0" font-size="12" text-anchor="end">0</text>
<line x1="56" y1="193.3" x2="60" y2="193.3" stroke="currentColor"/>
<text x="54" y="197.33333333333331" font-size="12" text-anchor="end">10000</text>
<line x1="56" y1="126.7" x2="60" y2="126.7" stroke="currentColor"/>
<text x="54" y="130.66666666666666" font-size="12" text-anchor="end">20000</text>
<line x1="56" y1="60.0" x2="60" y2="60.0" stroke="currentColor"/>
<text x="54" y="64.0" font-size="12" text-anchor="end">30000</text>
<rect x="80" y="77.0" width="30" height="183.0" fill="#888" fill-opacity="0.6"/>
<text x="95" y="72.98666666666668" font-size="12" text-anchor="middle">27452</text>
<rect x="114" y="229.4" width="30" height="30.6" fill="#d0564a" fill-opacity="0.6"/>
<text x="129" y="225.38" font-size="12" text-anchor="middle">4593</text>
<text x="112" y="276" font-size="12" text-anchor="middle">run 1</text>
<rect x="170" y="97.1" width="30" height="162.9" fill="#888" fill-opacity="0.6"/>
<text x="185" y="93.1" font-size="12" text-anchor="middle">24435</text>
<rect x="204" y="180.7" width="30" height="79.3" fill="#d0564a" fill-opacity="0.6"/>
<text x="219" y="176.74666666666667" font-size="12" text-anchor="middle">11888</text>
<text x="202" y="276" font-size="12" text-anchor="middle">run 2</text>
<rect x="260" y="229.2" width="30" height="30.8" fill="#888" fill-opacity="0.6"/>
<text x="275" y="225.18666666666667" font-size="12" text-anchor="middle">4622</text>
<rect x="294" y="114.2" width="30" height="145.8" fill="#d0564a" fill-opacity="0.6"/>
<text x="309" y="110.15333333333334" font-size="12" text-anchor="middle">21877</text>
<text x="292" y="276" font-size="12" text-anchor="middle">run 3</text>
<text x="530" y="48" font-size="12" text-anchor="middle" font-weight="bold">irqsoff tracer 최대 (커널)</text>
<line x1="390" y1="260" x2="680" y2="260" stroke="currentColor"/>
<line x1="390" y1="60" x2="390" y2="260" stroke="currentColor"/>
<line x1="386" y1="260.0" x2="390" y2="260.0" stroke="currentColor"/>
<text x="384" y="264.0" font-size="12" text-anchor="end">0</text>
<line x1="386" y1="220.0" x2="390" y2="220.0" stroke="currentColor"/>
<text x="384" y="224.0" font-size="12" text-anchor="end">1000</text>
<line x1="386" y1="180.0" x2="390" y2="180.0" stroke="currentColor"/>
<text x="384" y="184.0" font-size="12" text-anchor="end">2000</text>
<line x1="386" y1="140.0" x2="390" y2="140.0" stroke="currentColor"/>
<text x="384" y="144.0" font-size="12" text-anchor="end">3000</text>
<line x1="386" y1="100.0" x2="390" y2="100.0" stroke="currentColor"/>
<text x="384" y="104.0" font-size="12" text-anchor="end">4000</text>
<line x1="386" y1="60.0" x2="390" y2="60.0" stroke="currentColor"/>
<text x="384" y="64.0" font-size="12" text-anchor="end">5000</text>
<rect x="410" y="234.8" width="30" height="25.2" fill="#888" fill-opacity="0.6"/>
<text x="425" y="230.76" font-size="12" text-anchor="middle">631</text>
<rect x="444" y="99.6" width="30" height="160.4" fill="#d0564a" fill-opacity="0.6"/>
<text x="459" y="95.6" font-size="12" text-anchor="middle">4010</text>
<text x="442" y="276" font-size="12" text-anchor="middle">run 1</text>
<rect x="500" y="231.4" width="30" height="28.6" fill="#888" fill-opacity="0.6"/>
<text x="515" y="227.4" font-size="12" text-anchor="middle">715</text>
<rect x="534" y="99.4" width="30" height="160.6" fill="#d0564a" fill-opacity="0.6"/>
<text x="549" y="95.44" font-size="12" text-anchor="middle">4014</text>
<text x="532" y="276" font-size="12" text-anchor="middle">run 2</text>
<rect x="590" y="190.9" width="30" height="69.1" fill="#888" fill-opacity="0.6"/>
<text x="605" y="186.92000000000002" font-size="12" text-anchor="middle">1727</text>
<rect x="624" y="99.8" width="30" height="160.2" fill="#d0564a" fill-opacity="0.6"/>
<text x="639" y="95.75999999999999" font-size="12" text-anchor="middle">4006</text>
<text x="622" y="276" font-size="12" text-anchor="middle">run 3</text>
<rect x="180" y="296" width="14" height="10" fill="#888" fill-opacity="0.6"/>
<text x="200" y="306" font-size="12" text-anchor="start">범인 없음</text>
<rect x="300" y="296" width="14" height="10" fill="#d0564a" fill-opacity="0.6"/>
<text x="320" y="306" font-size="12" text-anchor="start">IRQ off 4000 µs 주입</text>
</svg>
```
그림 5 — 같은 실험을 두 관찰자로 본 값. 유저 루프의 max는 범인이 없을 때가 더 큰 경우도 있다(TCG에서 VM 호스트가 QEMU 스레드를 재우는 시간이 섞인다). irqsoff tracer는 매번 4006~4014 µs와 함수 이름을 준다.

출력에서 볼 것:

- **유저 루프 숫자만으로는 범인 유무조차 못 가린다.** 범인이 없을 때 max 24 ms, 있을 때 12 ms(run 2). TCG의 vCPU가 호스트에서 선점되는 시간은 게스트 커널이 볼 수 없는 "OS 밖 정지"다 — 실보드의 EL3 펌웨어·DVFS 정지와 같은 종류(N3 §8). 이 숫자들은 실제 HW 지연이 아니다.
- **irqsoff tracer는 안정적이다**: 주입한 4000 µs + 진입·복귀 약 10 µs, 시작 함수 `execute_preemptirqtest`, 끝 `preemptirqtest_0`, 스택 `preemptirq_delay_run → kthread`. 실제 제품이라면 이 자리에 `xxx_spi_xfer`나 `spin_lock_irqsave`를 잡은 드라이버 함수가 나온다.
- `trace_clock`을 `mono`로 바꿔 두면 trace 시각과 `CLOCK_MONOTONIC`(앱 로그)을 같은 축에 놓을 수 있다(M11 §5.1).
- `tracing_max_latency`는 **최대값 하나만** 보관한다. 여러 범인이 섞이면 가장 긴 것만 남으므로, 하나 고친 뒤 다시 돌린다. 분포가 필요하면 `rtla timerlat hist`(N3 §7.3).

### 6.7 말로 하는 답

**30초**: "먼저 측정을 표준화하겠다 — cyclictest나 timerlat을 실제 부하와 함께 길게. 그다음 `rtla timerlat -a`로 스파이크 순간에 멈춰서 IRQ 단계인지 thread 단계인지 가른다. IRQ 단계면 irqsoff tracer가 IRQ를 끈 함수를 스택과 함께 준다. 그래도 범인이 없으면 hwnoise로 OS 밖 정지를 보고, 최종 확인은 GPIO와 scope다."

> I'd standardize the measurement first — cyclictest or timerlat for long enough, under the real load, because max latency keeps growing with test duration. Then rtla timerlat with auto-analysis to stop at the first spike above threshold and split it into IRQ latency versus thread latency. If it's the IRQ stage, someone had interrupts off, and the irqsoff tracer records the longest window with its start and end functions and a stack — in my lab it pinned a 4 ms IRQs-off section to the exact function every run, while the application's own lateness numbers were too noisy to even tell whether the culprit was present. If it's the thread stage, I look at sched_switch on that CPU for a higher-priority task or a preempt-off section. If the kernel sees nothing, I check hwlat or hwnoise for time stolen outside the OS, like firmware calls, and I confirm with a GPIO toggle on a scope because a software clock can't measure its own stalls.

**꼬리 질문·함정**: "Would PREEMPT_RT fix it?" → RT는 선점 불가 구간을 줄이지만 드라이버의 `local_irq_save` + busy wait는 RT에서도 그대로 IRQ를 막는다(raw spinlock·IRQ off는 RT에서도 IRQ off). 함정: 디버그 tracer 자체가 오버헤드를 더한다 — 지연 값 비교는 같은 커널 설정끼리.

---

## 7. 시나리오 7 — "메모리가 계속 는다"

### 7.1 면접관의 질문

> "Devices in the field reboot every few days with OOM. Memory usage grows slowly. How do you find the leak?"

### 7.2 먼저 묻는 질문

- 커지는 것이 **어느 프로세스의 RSS**인가, 아니면 프로세스 합과 무관하게 **커널(Slab·vmalloc)**인가?
- 성장이 시간에 비례하나, 이벤트 수(재연결, 카메라 재시작, 핫플러그)에 비례하나?
- OOM killer 로그(`dmesg`의 `Out of memory: Killed process …`와 메모리 덤프)가 남아 있나?
- 최근 바뀐 앱·드라이버·커널은?

### 7.3 진단 트리

1. **유저냐 커널이냐**: `/proc/meminfo`의 `MemAvailable`, `Slab`, `SUnreclaim`, `VmallocUsed`와 각 프로세스의 `VmRSS`/`RssAnon`을 주기적으로 기록해 diff한다. 프로세스 합이 그대로인데 `SUnreclaim`이 크면 커널이다(M2 §5.6).
2. **유저 쪽**: 해당 프로세스의 `/proc/PID/smaps_rollup`·`[heap]` 크기 추이, 현장 스냅숏 `gcore`(M9 §8), 개발 환경에서 valgrind(`definitely lost`)·ASan/LeakSanitizer·massif(M9 §7).
3. **커널 쪽**: `/proc/slabinfo` 주기 diff로 **커지는 캐시 이름**을 찾는다. 드라이버 전용 캐시면 범인이 바로 보이고, `kmalloc-N`이면 주인을 모른다(M2 §5.5: `kmalloc-512`가 1000개씩 늘고 rmmod 후에도 그대로).
4. **`kmalloc-N` 주인 찾기**: 디버그 커널에서 kmemleak(§7.6) 또는 `slab_debug=U,kmalloc-512` + `/sys/kernel/slab/kmalloc-512/alloc_calls`.
5. **이벤트 비례 테스트**: 의심 동작(재연결, `rmmod/insmod`, 스트림 시작·정지)을 1000번 반복하며 카운터를 본다 — 몇 주짜리 누수를 몇 분으로 압축한다.

### 7.4 HW냐 SW냐

누수는 거의 항상 SW다. 다만 "메모리가 준다"가 사실 **CMA·reserved memory 단편화**(카메라 버퍼를 연속 할당 못 함, F10)이거나 GPU/NPU 드라이버가 들고 있는 DMA 버퍼(dma-buf, `/sys/kernel/debug/dma_buf/bufinfo`)일 수 있다 — 이것도 "누수처럼 보이는 정상"을 가르는 단계다.

### 7.5 실제로 본 원인들

| 증상 | 원인 | 근거 |
|---|---|---|
| `kmalloc-512` 1000개씩 증가, rmmod 후에도 남음 | 모듈이 kmalloc을 해제 안 함 | M2 §5.5 |
| kmemleak 백트레이스에 심볼 없음 | 이미 언로드된 모듈 안의 할당 — rmmod 전에 스캔 | M8 §5.2 |
| valgrind `16 bytes ... definitely lost` | 리스트 head를 잃음 | M9 예제 19 |
| RSS가 이벤트마다 계단식 증가 | 재연결 때 버퍼 재할당·미해제 | M9 §8 |

### 7.6 랩 재현 — 에러 경로의 kfree 누락을 kmemleak으로

무엇을 확인하나: 실무에서 가장 흔한 커널 누수 모양 — **정상 경로는 해제하는데 에러 경로가 해제를 잊는다**. 아래는 **교육용 예제**다(메모리만 새고 다른 부작용은 없다). 모듈 파라미터에 쓰면 요청 하나를 처리하고, 홀수 id는 "잘못된 요청"으로 에러 경로를 탄다.

```c
// SPDX-License-Identifier: GPL-2.0
/* R4 teaching example ONLY: a "request handler" whose error path forgets kfree().
 * Every write of an odd id to /sys/module/r4_leak/parameters/req leaks one object. */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>

struct r4_req {
	u32 id;
	u8 payload[188];
};

static int r4_handle_req(u32 id)
{
	struct r4_req *r = kzalloc(sizeof(*r), GFP_KERNEL);

	if (!r)
		return -ENOMEM;
	r->id = id;
	if (id & 1)		/* "odd id = invalid" -> error path */
		return -EINVAL;	/* BUG: r is never freed here */
	kfree(r);
	return 0;
}

static int r4_req_set(const char *val, const struct kernel_param *kp)
{
	u32 id;
	int ret = kstrtou32(val, 0, &id);

	if (ret)
		return ret;
	ret = r4_handle_req(id);
	pr_info("r4_leak: req %u -> %d\n", id, ret);
	return 0;
}

static const struct kernel_param_ops r4_req_ops = { .set = r4_req_set };
module_param_cb(req, &r4_req_ops, NULL, 0200);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("R4 teaching example: leak on error path");
```

(빌드 `make -C /var/lab/out/linux-build-kasan M=$PWD modules`, 경고 0.) 게스트(`Image-kasan`, BusyBox initramfs):

```sh
K=/sys/kernel/debug/kmemleak
echo clear > $K
insmod /r4_leak.ko
grep -E "^kmalloc-(192|256) " /proc/slabinfo | cut -c1-60
for i in 0 1 2 3 4 5 6 7 8 9; do echo $i > /sys/module/r4_leak/parameters/req; done
dmesg | grep "r4_leak: req" | tail -3
grep -E "^kmalloc-(192|256) " /proc/slabinfo | cut -c1-60
sleep 7; echo scan > $K; sleep 2; echo scan > $K
echo "== unreferenced: $(grep -c 'unreferenced object' $K)"
cat $K | head -24
```

```text
kmalloc-256         1545   1554    768   21    4 : tunables 
kmalloc-192          638    672    384   21    2 : tunables 
[   17.625696][   T77] r4_leak: req 7 -> -22
[   17.628846][   T77] r4_leak: req 8 -> 0
[   17.632044][   T77] r4_leak: req 9 -> -22
kmalloc-256         1544   1554    768   21    4 : tunables 
kmalloc-192          655    672    384   21    2 : tunables 
== unreferenced: 5
unreferenced object 0xffff00000ed4eac0 (size 192):
  comm "sh", pid 77, jiffies 4294896522
  hex dump (first 32 bytes):
    01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
  backtrace (crc 596c8d81):
    kmemleak_alloc+0xb8/0xd0
    __kmalloc_cache_noprof+0x574/0x7e0
    r4_req_set+0xfc/0x1c0 [r4_leak]
    param_attr_store+0x138/0x278
    module_attr_store+0x48/0x80
    sysfs_kf_write+0xcc/0x140
    kernfs_fop_write_iter+0x2b0/0x480
    vfs_write+0x4e0/0xc80
    ksys_write+0xf0/0x1e0
    __arm64_sys_write+0x70/0xc0
    invoke_syscall.constprop.0+0x64/0x290
    el0_svc_common.constprop.0+0xa8/0x250
    do_el0_svc+0x44/0x68
    el0_svc+0x50/0x218
    el0t_64_sync_handler+0xa0/0xe8
    el0t_64_sync+0x198/0x1a0
unreferenced object 0xffff00000ed4e940 (size 192):
  comm "sh", pid 77, jiffies 4294896523
```

출력에서 볼 것:

- **정확히 5개**(id 1·3·5·7·9), 크기 192(`sizeof(struct r4_req)` = 4 + 188). hex dump 첫 바이트 `01` = 첫 누수 객체의 `id`가 1이다 — 내용으로 어느 요청이 샜는지까지 보인다.
- `/proc/slabinfo`는 같은 시간에 `kmalloc-192`가 638 → 655(+17)로 늘었다. 5개 누수 외에 다른 할당도 섞였기 때문이다(그리고 KASAN 커널은 객체 크기 열이 384로, 메타데이터 때문에 원래 크기의 두 배다). **slabinfo는 "어느 캐시가" 커지는지, kmemleak은 "누가" 할당했는지**를 말한다.
- 백트레이스에 `r4_handle_req`가 없고 `r4_req_set+0xfc`다 — 컴파일러가 인라인했다(M3 §4.4의 인라인 맹점). `faddr2line r4_leak.ko r4_req_set+0xfc/0x1c0`로 소스 줄을 확인한다.
- `sleep 7`은 kmemleak이 **5초보다 어린 객체는 보고하지 않기** 때문이다(`mm/kmemleak.c`의 최소 나이). 스캔은 두 번 해서 오탐(우연히 그 주소 값을 가진 데이터)을 줄인다(M8 §5).
- 수정: 에러 경로에 `kfree(r)` 또는 6.x 스타일 `struct r4_req *r __free(kfree) = kzalloc(...)`와 `return_ptr`/`no_free_ptr` 패턴. 회귀 방지는 CI 디버그 커널에서 kmemleak "새 항목 0개" 게이트(M8 §11).

### 7.7 말로 하는 답

**30초**: "먼저 유저냐 커널이냐를 가르겠다 — meminfo의 SUnreclaim과 프로세스 RSS를 같이 주기 기록. 프로세스면 RSS 추이와 gcore, 개발 환경에서 ASan/valgrind. 커널이면 slabinfo diff로 커지는 캐시를 찾고, kmalloc-N이면 디버그 커널의 kmemleak으로 할당 스택을 본다. 그리고 의심 이벤트를 천 번 반복해 며칠을 몇 분으로 줄인다."

> First, user or kernel: I log MemAvailable, Slab, SUnreclaim and per-process RSS periodically and diff them. If a process grows, I look at its heap and anon RSS trend, take a gcore in the field, and reproduce under ASan or valgrind. If the kernel grows, /proc/slabinfo diffs tell me which cache — a driver-specific cache names the culprit immediately; a generic kmalloc-N cache needs kmemleak or slab_debug alloc_calls on a debug kernel. In a lab run, kmemleak reported exactly the five objects leaked by an error path that forgot kfree, with the allocation backtrace and even the request id in the hex dump. To make it fast, I find the event the growth is proportional to — reconnects, stream restarts, hotplug — and loop it a thousand times.

**꼬리 질문·함정**: "Can you run kmemleak in production?" → 메모리·CPU 비용이 커서 보통 아니다(M8 비용 실측: Slab 5.8배). 필드에서는 slabinfo·meminfo 텔레메트리, 재현은 디버그 커널로. 함정: 페이지 캐시 증가(`Cached`)는 누수가 아니다 — `MemAvailable`을 본다.

---

## 8. 시나리오 8 — "OTA 후 부팅이 안 된다"

### 8.1 면접관의 질문

> "After an OTA rollout, 2% of units come back on the old version, and a few are bricked. What do you look at?"

### 8.2 먼저 묻는 질문

- 업데이트 방식은? A/B 슬롯 + bootcount인가, single-copy + recovery인가(L4 §2)?
- "롤백"된 장치와 "벽돌"이 된 장치를 구분할 수 있나? 벽돌은 부트로더·env까지 망가진 것인가?
- 롤백된 장치의 공통점: HW 리비전, 카메라 모듈, 저장장치 벤더, 부팅 시간이 health 타임아웃 근처인가?
- 업데이트 중 전원 차단이 있었나?

### 8.3 진단 트리

1. **롤백된 장치의 증거부터**(살아 있으니 가장 싸다): `fw_printenv boot_slot upgrade_available bootcount` + 슬롯 상태, `rauc status --detailed`(설치 시각·번들 버전·해시) — L4 §16.2 순서.
2. **실패한 시험 부팅의 로그**: journal이 영구 저장이면 `journalctl --list-boots`와 `journalctl -b -1`, panic이면 pstore/ramoops(L7 §4–5).
3. **실패 종류 분류**: 커널 panic(`Attempted to kill init!` → `panic=5` → bootcount 증가), systemd는 떴지만 health check 실패, 타임아웃(느린 eMMC에서 udev coldplug가 90초를 넘음 — L4 §16.2에서 실제로 겪음).
4. **벽돌**: 슬롯 밖 요소 — 부트로더 업데이트, env 두 벌이 다 깨짐(L4 §4.7), anti-rollback 카운터, 데이터 파티션 마이그레이션(L4 §13).

### 8.4 HW냐 SW냐

- 특정 저장장치 벤더·리비전에만 몰리면 HW 상호작용(eMMC 쓰기 지연, 전원 차단 내성) 쪽. 시리얼 콘솔로 U-Boot의 bootcount 줄을 직접 보면 "부트로더는 슬롯을 맞게 골랐나"가 바로 갈린다.
- 업데이트 중 전원 차단은 L4 §8.1처럼 **비활성 슬롯 쓰기 중 kill -9**로 재현할 수 있다 — A/B면 활성 슬롯이 무사해야 한다.

### 8.5 실제로 본 것 — bootcount 롤백 (L4 §7.1 인용)

```text
L4: boot_slot=A part=1 bootcount=3 upgrade_available=1
L4 v3-broken: init exits on purpose
[    1.316937] Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000100
[    1.321702] Rebooting in 5 seconds..
...
Warning: Bootlimit (3) exceeded. Using altbootcmd.
...
L4: bootlimit exceeded on slot A -> rollback
...
L4: boot_slot=B part=2 bootcount=0 upgrade_available=0
```

롤백 뒤 `rauc status`는 A를 `bad`로 기록했다 — 서버에 "이 번들은 이 장비에서 부팅 실패"를 보고할 정보가 남는다. 면접 포인트: **롤백은 성공이 아니라 증거 수집의 시작**이다.

### 8.6 말로 하는 답

**30초**: "롤백된 장치부터 보겠다 — 살아 있고 증거가 있다. 부트로더 env의 슬롯 상태와 bootcount, RAUC 같은 업데이터의 설치 기록, 그리고 이전 부팅의 journal이나 pstore. 실패를 panic·health check·타임아웃으로 나누고 HW 리비전별로 묶는다. 벽돌은 슬롯 밖 요소 — 부트로더, env, anti-rollback을 의심한다."

> The rolled-back units are the best evidence because they're alive. I read the bootloader environment — which slot was marked bad, bootcount, upgrade_available — and the updater's record of what was installed, then the failed trial boot's logs from a persistent journal or pstore. I classify the failures: kernel panic, health check failure, or a timeout; a slow eMMC pushing udev coldplug past a timeout is a real case. Then I group by hardware revision and storage vendor. Bricked units mean something outside the A/B slots broke — a bootloader update, both env copies, or an anti-rollback counter — so I'd get one on a bench with a serial console. The fix side is process: staged rollout with automatic halt on rollback rate.

**꼬리 질문·함정**: "Who decides the boot was successful?" → 부트로더는 "시도"만 세고, 성공 판정은 유저 공간 health check가 `upgrade_available=0`·`bootcount=0`으로 commit한다(L4 §3.2). 함정: commit을 너무 일찍(커널이 뜨자마자) 하면 앱이 죽는 업데이트를 못 잡는다.

---

## 9. 시나리오 9 — "필드에서 커널 Oops/panic이 난다"

### 9.1 면접관의 질문

> "We get a kernel panic in the field about once a week across a fleet of 500 units. Here's a log snippet. How do you approach it?"

### 9.2 먼저 묻는 질문

- 로그가 있나? 전체 Oops(레지스터, Call trace, `Code:`)인가 마지막 몇 줄인가? 같은 빌드의 `vmlinux`(디버그 정보)가 보관돼 있나(L7 §9 build-id)?
- 매번 같은 스택인가? 특정 HW 리비전·온도·가동 시간과 상관 있나?
- 첫 Oops인가, 앞에 WARN이나 다른 Oops가 있었나? (첫 Oops 뒤의 커널은 믿을 수 없다 — M3 §8.2)
- tainted 플래그(`O` = out-of-tree 모듈, `P` = 독점)가 있나?

### 9.3 진단 트리

1. **증거 체계**: `panic_on_oops=1` + `panic=N`(자동 재부팅), pstore/ramoops(웜 리셋 후 다음 부팅에서 `/sys/fs/pstore`), systemd-pstore로 디스크 보관, 여유가 있으면 kdump(L7 §2–6).
2. **첫 줄 읽기**: `Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000` + `ESR`(EC 0x25 = data abort, M3 §3)로 "무슨 종류의 죽음인가".
3. **pc를 소스 줄로**: `scripts/decode_stacktrace.sh vmlinux < oops.txt` 또는 `scripts/faddr2line vmlinux func+0x100/0x218`. **주의**: `.text.unlikely`나 인라인 때문에 `decode_stacktrace.sh`가 틀린 줄을 줄 수 있다 — M3 §6.3에서 실제로 틀렸고 `objdump -dr`·gdb로 확인했다.
4. **주소 패턴**: `0x0`(NULL), `0xdead…100/122`(list_del 뒤 사용), `0x6b6b6b6b`(slab poison, 해제 후 사용) — M3 §9.1 표.
5. **크래시 지점 ≠ 버그 지점**이면(메모리 손상), 디버그 커널(KASAN·KFENCE)로 원인 시점을 당긴다. 양산에는 KFENCE(샘플링, 비용 작음)를 켜 둘 수 있다(M8).

### 9.4 HW냐 SW냐

- 같은 스택이 반복되면 SW 버그 가능성이 높다. **매번 다른 스택**, 무작위 주소, `synchronous external abort`, 특정 보드에만 → DDR(트레이닝·전압·온도), 전원 drop, ECC 에러 로그(EDAC), 클럭을 의심한다. memtester·stress-ng로 그 보드만 돌려 본다(P1 §5).
- `synchronous external abort`는 "존재하지 않는 물리 주소를 건드림 = 버스 decode error"다(P1 §4.2) — Don이 SoC에서 보던 버스 에러와 같은 신호. 클럭 게이팅된 블록의 레지스터를 읽어도 비슷한 abort가 난다.

### 9.5 실제로 본 것 — DT 실수가 드라이버 에러 경로를 깨움 (Q1 §8.6 인용)

```text
[    7.495516][   T72] OF: size of link-frequencies in node /q1-i2c/cam@14/port/endpoint is not a multiple of 8
[    7.495739][   T72] imx219 0-0014: no link frequencies in firmware
[    7.496346][   T72] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
...
[    7.510981][   T72] pc : imx219_check_hwcfg.constprop.0+0x100/0x218 [imx219]
...
[    7.517910][   T72]  imx219_probe+0x78/0x718 [imx219]
```

읽는 순서: **Oops 앞 두 줄이 원인을 이미 말한다**(`link-frequencies`를 `/bits/ 64` 없이 32비트로 씀). 그 실패를 보고하는 에러 경로가 할당 안 된 배열의 0번을 읽어 NULL 역참조가 났다 — "에러를 찍는 코드가 크래시를 일으키는" 고전적 버그이고, 에러 경로는 덜 테스트된다는 교훈이다(6.18.55에서 재현, 최신 mainline 수정 여부는 Q1 노트에서도 확인 필요로 남김).

### 9.6 말로 하는 답

**30초**: "먼저 증거 체계를 확인하겠다 — panic_on_oops, pstore, 빌드별 vmlinux 보관. 그다음 Oops의 첫 줄과 ESR로 죽음의 종류를, faddr2line으로 소스 줄을 찾는다. 같은 스택이 반복되면 SW 버그고 Oops 앞의 경고 줄을 본다. 매번 스택이 다르면 메모리 손상이나 HW라서 KASAN 커널 재현이나 DDR·전원을 본다."

> First I make sure we're collecting the right evidence: panic_on_oops with auto-reboot, pstore/ramoops so the next boot can upload the log, and the matching vmlinux with debug info kept per build-id. Then I read the Oops top-down: the first line and the ESR tell me what kind of fault it was, faddr2line maps pc to a source line — and I double check with objdump because inlining and .text.unlikely can make decode_stacktrace point at the wrong line. I always read the lines before the Oops; in one case the DT parser had already printed the real cause and the driver's error path crashed while reporting it. If every crash has the same stack, it's a software bug I can usually reproduce. If the stacks are random, it's memory corruption or hardware: I run a KASAN build or keep KFENCE on in production, and check DDR, power rails and EDAC on the affected boards.

**꼬리 질문·함정**: "Oops vs panic?" → Oops는 커널이 예외를 보고하고 그 태스크를 죽인 뒤 계속 갈 수도 있는 상태, panic은 멈춤(또는 `panic=N` 재부팅). 제품은 `panic_on_oops=1`로 "오염된 커널로 계속 달리기"를 막는다(M3 §8.5). 함정: tainted 커널(`O` 플래그)이면 업스트림은 리포트를 받지 않는다 — 벤더 모듈 없이 재현을 먼저 시도한다.

---

## 10. 커널 안에서는 무슨 일이 일어나나 — 각 "창문"은 어디서 열리나

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">관찰 지점 지도 — 증상은 위층에서 보이고, 원인은 아래층에 있는 경우가 많다</text>
<rect x="10" y="40" width="140" height="64" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="80.0" y="76.5" font-size="13" text-anchor="middle" font-weight="bold">유저 공간</text>
<rect x="160" y="40" width="445" height="64" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="382.5" y="76.2" font-size="12" text-anchor="middle">strace · perf · valgrind/ASan · journalctl · coredumpctl</text>
<rect x="10" y="120" width="140" height="64" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="80.0" y="156.6" font-size="13" text-anchor="middle" font-weight="bold">커널 서비스</text>
<rect x="160" y="120" width="445" height="64" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="382.5" y="156.2" font-size="12" text-anchor="middle">dmesg · dyndbg · ftrace · kmemleak · devices_deferred</text>
<rect x="10" y="200" width="140" height="64" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="80.0" y="236.6" font-size="13" text-anchor="middle" font-weight="bold">드라이버 · 버스</text>
<rect x="160" y="200" width="445" height="64" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="382.5" y="236.2" font-size="12" text-anchor="middle">i2c tracepoint · /proc/interrupts · ip -s can · v4l2 seq</text>
<rect x="10" y="280" width="140" height="64" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="80.0" y="316.6" font-size="13" text-anchor="middle" font-weight="bold">하드웨어</text>
<rect x="160" y="280" width="445" height="64" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="382.5" y="316.2" font-size="12" text-anchor="middle">scope · 로직 분석기 · JTAG · 전류 프로브 · 열화상</text>
<line x1="628" y1="50" x2="628" y2="340" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<text x="638" y="200" font-size="12" text-anchor="start">원인</text>
<text x="340" y="380" font-size="12" text-anchor="middle">HW/SW 경계 확인 = 아래 두 층의 같은 사건을 같은 시계로 (M11 §5)</text>
</svg>
```
그림 6 — 관찰 지점 지도. 증상은 위층(앱이 "느리다", "없다")에서 보고되고, 원인은 아래층(드라이버·HW)에 있는 경우가 많다. 경계를 넘는 확인은 아래 두 층의 같은 사건을 같은 시계로 보는 것이다.

이 노트에서 쓴 창문이 커널의 어디서 열리는지:

| 창문 | 커널 쪽 출처 | 이 노트에서 본 것 |
|---|---|---|
| `devices_deferred` | `drivers/base/dd.c` `deferred_devs_show()` — `deferred_probe_pending_list` + `dev_err_probe()`가 채운 이유 | §1.6 두 종류의 이유 문자열 |
| "supplier … not ready" | `drivers/base/core.c` `device_links_check_suppliers()` — fw_devlink가 DT phandle로 만든 링크 | §1.6 `sync_state_only=0` 링크만 probe를 막음 |
| `deferred probe pending:` | `dd.c` `deferred_probe_timeout_work_func()` — 기본 10초, 이후 fw_devlink 완화 | §1.6 10.79 s |
| irqsoff tracer | `kernel/trace/trace_irqsoff.c` — `local_irq_disable/enable`의 tracepoint 훅에서 구간 길이 계산, 최대값 저장 | §6.6 4006~4014 µs |
| timerlat/osnoise | `kernel/trace/trace_osnoise.c` — 주기 타이머 IRQ와 per-CPU 스레드가 직접 지연을 잰다 | N3 §7 |
| kmemleak | `mm/kmemleak.c` — 모든 할당을 기록, 루트(데이터·bss·스택)에서 도달 못 하는 객체 보고 | §7.6 |
| `can state BUS-OFF` | `drivers/net/can/dev/dev.c` `can_change_state()`·`can_bus_off()` → `netif_carrier_off()` | Q4 §8.4 |
| V4L2 `sequence` | 드라이버가 `vb2_buffer_done()` 전에 채움 — 갭 = 드라이버가 아는 손실 | Q2 §5 |
| systemd job timeout | 유저 공간(systemd)이 udev의 장치 이벤트를 기다림 — 커널은 장치를 못 만들었을 뿐 | §3.6 |

드라이버를 쓸 때 다시 만나는 지점: **`dev_err_probe()`를 써야 `devices_deferred`에 이유가 남는다**(G3, M2), IRQ를 끄는 구간은 irqsoff tracer에 그대로 찍힌다(F6–F8), 에러 경로의 해제는 `devm_`·`__free()`로 구조적으로 막는다(G4), 버퍼 sequence와 timestamp는 드라이버의 책임이다(I5).

---

## 11. Embedded 실무에서 다시 보기

### 11.1 "첫 5분" 수집 스크립트 — 보고를 받으면 먼저 떠 둔다

면접에서 "로그를 보겠다"를 구체적으로 말하는 방법이다. 장비에 넣어 두는 진단 번들(가이드 — 이 조합 그대로는 실행 안 함, 각 명령은 앞선 노트들에서 실행해 봤다):

```sh
#!/bin/sh
# triage.sh - one-shot evidence bundle (field or bench)
o=/data/triage-$(date +%s); mkdir -p $o
uname -a > $o/uname; cat /proc/cmdline > $o/cmdline; uptime > $o/uptime
dmesg -T > $o/dmesg 2>&1; journalctl -b -1 -o short-monotonic > $o/journal-prev 2>&1
journalctl -b -p warning > $o/journal-warn 2>&1
cat /sys/kernel/debug/devices_deferred > $o/deferred 2>&1
cat /proc/interrupts > $o/irq1; sleep 1; cat /proc/interrupts > $o/irq2   # rate = diff
cat /proc/meminfo /proc/slabinfo > $o/mem 2>&1
systemctl list-units --state=failed --no-pager > $o/failed; systemctl is-system-running > $o/state
ip -details -statistics link > $o/net 2>&1
ls /sys/fs/pstore > $o/pstore 2>&1
tar czf $o.tgz -C /data $(basename $o) && echo $o.tgz
```

`/proc/interrupts` 두 번 찍기는 P4 §8.3의 IRQ 폭주(1초 만에 286회 vs 정상 1회)를 잡는 가장 싼 방법이다.

### 11.2 bring-up 일지에 남길 것

- 정상 상태 스냅숏(첫 부팅의 `dmesg`, `/proc/interrupts`, `devices_deferred`, `regulator_summary`)을 저장해 두면 이후 커널·DT 변경 때마다 diff로 회귀를 잡는다(M2 §11).
- 버그마다 "증상 → 가설 → 실행한 측정 → 배제한 것 → 원인 → 수정 → 회귀 테스트"를 한 단락으로. 이것이 면접의 STAR 답변 원료가 된다(R6, P1 §9).

### 11.3 Don의 경험을 이 틀에 올리기

- **RF 칩 bring-up의 버스 장애 root cause**(PCIe/I2C/SPI/SPMI/RFFE) → 시나리오 1·2의 "HW냐 SW냐" 칸. LA로 트랜잭션을 보고 드라이버 로그와 시간축을 맞춘 경험을 그대로 말한다.
- **SSD 펌웨어의 간헐 데이터 손상·성능 스파이크** → 시나리오 2·6. "증거부터 심는다(이벤트 로그·카운터), 분포를 본다, 최대값은 측정 시간에 따라 커진다"는 이미 아는 원칙이다.
- **양산 RMA 분석** → 시나리오 8·9의 "필드 증거 체계". Linux 용어로는 pstore·bootcount·build-id다. 구체적인 문장 다듬기는 R6.

---

## 12. 흔한 실수와 증상 (면접 답변의 실수)

| 실수 | 면접관에게 보이는 증상 | 왜 나쁜가 | 고치는 법 |
|---|---|---|---|
| 질문 없이 바로 해결책 | "드라이버를 다시 쓰겠다" | 범위를 모르고 비싼 일부터 함 | 명확화 질문 3개로 시작 |
| 도구 나열 | "ftrace, perf, eBPF를 쓰겠다" | 무엇을 보려는지 없음 | 도구마다 "이게 보이면 다음은"을 붙임 |
| 가설 하나에 고착 | 반증 증거가 나와도 같은 방향 | 간헐 버그에서 시간 낭비 | 각 단계에서 "무엇을 배제했나"를 말함 |
| HW로 떠넘김 | "HW 팀에 물어보겠다" | 경계를 측정으로 못 가름 | scope·LA·GPIO 토글로 증명 |
| 측정 자체를 안 의심 | 앱 fps, 유저 루프 max만 믿음 | §6.6처럼 관찰자가 틀릴 수 있음 | 두 번째 관찰자(커널 trace, GPIO) |
| "failed 목록이 비었으니 정상" | systemd emergency 원인을 못 찾음 | 의존성 실패는 failed가 아님(§3.6) | `is-system-running`, journal |
| 디버그 커널에서 안 나오면 "없다" | 레이스 버그를 놓침 | 탐지기가 타이밍을 바꿈 | 조건을 바꿔 반복, 카운터 계측 |
| 고친 뒤 증명 없음 | "이제 안 난다" | 간헐 버그는 우연히 안 날 수 있음 | 재현 테스트 + 기존 빈도 대비 통계 |

---

## 13. 면접에서 이렇게 말한다 — 시나리오를 가로지르는 메타 질문

**Q.** "How do you approach a bug you've never seen before?"

**A.** 같은 뼈대를 쓴다: 명확화 → 재현과 범위 → 층 나누기 → 싸고 흔한 것부터 → 이분 탐색 → 고치고 증명. 각 단계가 무엇을 배제하는지 말한다.

> I use the same skeleton every time. Clarify scope and what changed, get a reproduction or at least a rate, split the problem into layers — hardware, kernel, user space — and test the cheapest and most likely hypotheses first, saying what each measurement rules out. When I'm stuck, I bisect: versions, configs, or hardware swaps. And I'm not done until I have a reproduction that fails before the fix and passes after, plus a regression test.

**꼬리 질문**: "Give me an example." → R6의 STAR 스토리 하나(RF 버스 장애 또는 SSD 간헐 손상).

**Q.** "How do you decide whether it's hardware or software?"

**A.** 같은 사건을 HW 쪽과 SW 쪽에서 같은 시계로 본다. 선 위 바이트와 메모리 바이트, 센서 펄스 수와 IRQ 수, GPIO 토글과 ftrace 시각.

> I look at the same event from both sides with the same clock. Bytes on the wire from a logic analyzer versus bytes in memory; sensor pulses versus interrupt counts; a GPIO toggled in the driver versus the trace timestamp. Where the two views first disagree is the boundary. I don't hand it over to the hardware team until I can show them a capture.

**Q.** "The bug happens once a week and you can't reproduce it. What now?"

**A.** 증거를 심고 기다리는 것과, 빈도를 올리는 것을 동시에 한다. seq·CRC·카운터·pstore로 다음 발생 때 필요한 정보를 남기고, 의심 조건(부하·온도·이벤트 반복)을 키워 주 1회를 시간 1회로 만든다.

> Two tracks in parallel. Instrument so the next occurrence carries its own evidence — counters, sequence numbers, pstore, a GPIO pulse that triggers a logic analyzer, a tracer armed to stop on threshold like rtla timerlat with auto-analysis. And raise the rate: amplify the suspected condition — load, temperature, the event it correlates with — so once a week becomes once an hour.

**Q.** "There are no logs at all from the field unit."

**A.** 그 자체가 첫 번째 버그다. pstore/ramoops, `panic_on_oops` + `panic=N`, 영구 journal, 원격 업로드, bootcount 기록을 설계에 넣는다(L7). 당장은 같은 유닛을 벤치로 가져와 콘솔·JTAG.

> Then missing evidence is the first bug to fix. I'd add pstore/ramoops so a panic survives a warm reset, panic_on_oops with auto-reboot, a persistent journal on a data partition, a rate-limited remote upload, and bootcount state from the bootloader. For the unit at hand, I'd bring it to a bench with a serial console and JTAG.

**Q.** "How do you know you actually fixed an intermittent bug?"

**A.** 원래 빈도를 숫자로 알고 있어야 한다. 수정 전 N시간에 k번이었다면, 수정 후 같은 조건으로 그보다 충분히 긴 시간 0번(통계적으로 의미 있게), 그리고 원인을 설명하는 메커니즘이 있어야 한다.

> I need the baseline rate. If it failed k times in N hours before, I run the same stress for several times N afterwards with zero failures, and I need a mechanism that explains the bug — ideally a targeted test that triggers it deterministically before the fix. "It hasn't happened since" isn't proof.

**Q.** "What's in your Linux debugging toolkit, in order?"

**A.** dmesg·journal → sysfs/debugfs 상태(devices_deferred, /proc/interrupts, regulator_summary) → 버스 도구(i2c-tools, candump, v4l2-ctl, media-ctl) → 트레이스(ftrace 이벤트, irqsoff, rtla, perf) → 디버그 커널 탐지기(KASAN, kmemleak, lockdep, DMA_API_DEBUG) → JTAG·gdb → 계측기(scope, LA). 비용 순서다.

> In order of cost: dmesg and the journal; state files — debugfs devices_deferred, /proc/interrupts, regulator and clock summaries; bus tools like i2c-tools, candump, v4l2-ctl and media-ctl; tracing — ftrace events, the latency tracers, rtla, perf; debug kernels with KASAN, kmemleak, lockdep and DMA API debugging; JTAG with gdb; and the scope and logic analyzer, which I reach for earlier than most software people because that's where I come from.

---

## 14. 직접 해보기

1. (개념) §1.6에서 `r4-keys`의 링크가 `sync_state_only=1`이 된 이유를 한 문장으로 설명하라. 정답/힌트: GPIO phandle이 장치 노드가 아니라 자식 노드(`r4-drdy`)에 있고, 자식 노드의 링크는 부모 장치로 옮겨질 때 probe 순서를 막지 않는 약한 링크가 된다(`drivers/base/core.c` `__fw_devlink_link_to_suppliers()` 부근).
2. (실습) §1.6의 DT에서 `r4-drdy`의 `gpios`를 `r4-keys` 노드 자체로 옮기면(gpio-keys 바인딩에는 맞지 않지만) `devices_deferred`의 이유가 어떻게 바뀌는지 예측하고, 가능하면 다른 소비자(`gpio-leds`가 있는 `Image-sub` 등)로 확인하라. 정답/힌트: 직접 링크면 "supplier … not ready".
3. (실습) §6.6을 `test_mode=preempt`로 바꾸고 `preemptoff` tracer로 같은 실험을 하라. `irqsoff`에는 무엇이 보이나? 정답/힌트: preempt off 구간에서는 IRQ가 들어오므로 irqsoff max는 작게 남고 preemptoff가 4000 µs를 잡는다.
4. (실습) §7.6의 모듈을 고치고(`kfree` 또는 `__free(kfree)`), 같은 스크립트로 `unreferenced: 0`을 확인하라. 정답/힌트: 수정 전·후 같은 스크립트, `echo clear` 먼저.
5. (말하기) 시나리오 하나를 골라 30초 답을 녹음하고, "명확화 질문 → 첫 층 → 결정적 증거"가 다 들어 있는지 체크하라. 정답/힌트: 30초 안에 명령 이름 하나와 기대 출력 하나가 들어가야 한다.
6. (관찰) §3.6의 부팅을 `nofail`을 추가해(`...:ext4:nofail,x-systemd.device-timeout=20s`) 다시 하면 무엇이 달라지나? 정답/힌트: local-fs.target이 실패하지 않아 정상 부팅하지만 `/data/r4`는 마운트되지 않는다 — 앱이 확인해야 한다(가이드, 실행 안 함).
7. (설계) 시나리오 5에서 "write는 성공했는데 프레임이 버려지는" 문제를 앱 수준에서 감지하는 방법 두 가지를 적어라. 정답/힌트: `CAN_ERR_BUSOFF`·상태 에러 프레임 구독, TX echo(`IFF_ECHO`/loopback 수신)로 실제 송신 확인 또는 상대의 응답 타임아웃(Q4 §12.2).

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| devices_deferred | debugfs 파일 | probe가 미뤄진 장치와 이유 문자열 |
| fw_devlink | firmware device link | DT phandle로 공급자-소비자 링크를 자동 생성 |
| sync_state_only | 약한 device link | probe 순서를 막지 않고 공급자의 sync_state만 늦춤 |
| deferred_probe_timeout | 부팅 인자, 기본 10초 | 지나면 대기 장치를 경고하고 fw_devlink를 완화 |
| earlycon | 초기 콘솔 | 정식 UART 드라이버 전에 쓰는 콘솔, 주소 명시가 안전 |
| emergency.target | systemd 비상 모드 | 필수 마운트 실패 등에서 진입하는 최소 셸 |
| irqsoff tracer | ftrace 지연 tracer | IRQ가 꺼진 가장 긴 구간과 시작·끝 함수 기록 |
| timerlat | ftrace/rtla 측정기 | 주기 타이머의 IRQ·thread 단계 지연을 직접 측정 |
| kmemleak | 커널 누수 탐지기 | 루트에서 도달 못 하는 할당 객체 보고 |
| DMA_API_DEBUG | 커널 디버그 옵션 | DMA map/sync/unmap 짝과 방향 검사 |
| bus-off | CAN 오류 상태 | TEC ≥ 256, 노드가 버스에서 빠짐 |
| bootcount | 부트로더 카운터 | 시험 부팅 횟수, 한도 초과 시 롤백 |
| pstore/ramoops | 영속 로그 | 예약 RAM에 panic·콘솔을 남겨 다음 부팅에서 읽음 |
| ESR | Exception Syndrome Register | arm64 예외의 종류(EC)와 세부 원인 |

---

## 16. 요약 & 체크리스트

디버깅 면접은 정답 맞히기가 아니라 **순서와 근거를 말하는 시험**이다. 모든 시나리오는 같은 뼈대 — 명확화, 재현, 층 나누기, 싸고 흔한 것부터, 이분 탐색, 증명 — 위에 각 층의 Linux "창문"을 얹은 것이다. 센서는 DT → 장치 → bind → defer → probe → 버스 순서로 끊고(오타 compatible은 아무 데도 안 찍힌다), 간헐 손상은 깨진 모양으로 층을 고르고, 부팅 멈춤은 마지막 줄로 단계를 고른다. 지연 스파이크는 유저 숫자가 아니라 커널 tracer와 GPIO가 범인을 말하고(§6.6), 누수는 slabinfo가 "어느 캐시", kmemleak이 "누가"를 말한다(§7.6). Don의 강점인 계측은 모든 시나리오의 "HW냐 SW냐" 칸에서 같은 시계로 두 관찰을 맞추는 형태로 꺼낸다.

- [ ] 공통 6단계 뼈대를 30초 안에 말할 수 있다
- [ ] 센서 미검출 체인 6칸과 각 칸의 명령을 외워서 쓸 수 있다
- [ ] "supplier not ready"와 드라이버의 `-EPROBE_DEFER` 이유 문자열의 차이를 설명할 수 있다
- [ ] 간헐 손상을 깨진 모양 네 가지로 분류하고 각 도구를 댈 수 있다
- [ ] 부팅 멈춤을 콘솔 마지막 줄로 6단계 중 하나에 놓을 수 있다
- [ ] systemd 의존성 실패가 `--failed`에 안 나오는 이유를 설명할 수 있다
- [ ] 버퍼 수 실험으로 카메라 드롭의 생산자/소비자를 가를 수 있다
- [ ] CAN bus-off·ACK 에러·종단 문제를 `ip -s`, `candump -e`, 저항 측정으로 가를 수 있다
- [ ] irqsoff tracer와 rtla timerlat으로 스파이크 범인을 짚는 순서를 말할 수 있다
- [ ] slabinfo diff와 kmemleak으로 커널 누수의 캐시와 할당자를 찾을 수 있다
- [ ] OTA 롤백 장치에서 env·업데이터 기록·이전 부팅 로그 순으로 증거를 모을 수 있다
- [ ] Oops 첫 줄·ESR·faddr2line과 "크래시 지점 ≠ 버그 지점"을 설명할 수 있다

## 참고 자료

- docs.kernel.org — Device links (`Documentation/driver-api/device_link.rst`), kernel-parameters의 `fw_devlink`, `deferred_probe_timeout`, `earlycon`, `initcall_debug`: https://docs.kernel.org/admin-guide/kernel-parameters.html
- docs.kernel.org — ftrace (irqsoff·preemptoff tracer): https://docs.kernel.org/trace/ftrace.html , timerlat/osnoise tracer, RTLA: https://docs.kernel.org/tools/rtla/index.html
- docs.kernel.org — Kernel Memory Leak Detector: https://docs.kernel.org/dev-tools/kmemleak.html
- docs.kernel.org — DMA API (`Documentation/core-api/dma-api.rst`, DMA_API_DEBUG 절)
- docs.kernel.org — SocketCAN: https://docs.kernel.org/networking/can.html
- docs.kernel.org — V4L2 버퍼(`sequence`, timestamp): userspace-api/media/v4l/buffer
- systemd man 페이지: systemd.mount(5)(`x-systemd.device-timeout`, `nofail`), systemd-fstab-generator(8)(`systemd.mount-extra=`), systemd.special(7)(`emergency.target`) — https://www.freedesktop.org/software/systemd/man/
- 소스(랩 6.18.55): `drivers/base/dd.c`(deferred probe, timeout), `drivers/base/core.c`(`device_links_check_suppliers`), `kernel/trace/preemptirq_delay_test.c`, `kernel/trace/trace_irqsoff.c`, `mm/kmemleak.c`, `drivers/net/can/dev/dev.c`
- 이 사이트의 원 노트: M1–M11, N2–N3, P1·P3·P4, Q1·Q2·Q4, L1·L4·L7, H9, C6 (각 절 번호는 본문에 표시)
