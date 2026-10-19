# P3. 데이터시트 → 드라이버 워크플로 — 읽기, 찾기, 복제, regmap 표, 반복 bring-up (실무 핵심 루틴)

> **이 노트를 다 읽으면**: 데이터시트에서 드라이버에 필요한 줄만 골라 읽을 수 있다 · 새 칩을 받으면 먼저 기존 드라이버를 `grep`/binding/lore로 찾고, 없으면 가장 닮은 드라이버를 골라 복제할 수 있다 · 레지스터 표를 `regmap_config`(접근 표, 리셋값, 페이지, 캐시)로 옮기고 debugfs로 검증할 수 있다 · i2c-stub과 i2c-tools로 칩 없이도 probe를 반복할 수 있다
> **왜 필요한가**: 입사 후 첫 과제가 거의 항상 "이 센서/PMIC 붙여 줘"다. 면접에서도 "새 칩 드라이버를 어떻게 시작하나?"는 bring-up 스토리 질문(P1) 다음으로 자주 나온다.
> **Don 기준 난이도**: 데이터시트 읽기·레지스터 헤더 만들기·에라타 대응은 이미 몸에 있다 / 새로 배울 것은 "먼저 찾고 복제한다"는 Linux식 순서, regmap 표로 칩 행동을 선언하는 법, i2c-stub·debugfs·binding 검증 도구
> **선행 노트**: H2(regmap 기초 — 이 노트는 반복하지 않는다), H5(I2C 서브시스템), I2(IIO), D3(드라이버 매칭), D7(binding YAML), G3(probe 수명), G8(debugfs), M1(dynamic debug), P1(bring-up 순서)

---

## 0. 큰 그림 — 이게 왜 필요한가

bare-metal에서 Don이 새 칩을 붙이는 순서는 대략 이랬을 것이다: 데이터시트를 받는다 → 레지스터 표를 보고 `#define` 헤더를 만든다 → init 시퀀스를 짠다 → 로직 분석기로 버스를 보며 고친다. 전부 **내가 처음부터** 쓴다.

Linux에서는 첫 단계와 둘째 단계 사이에 **"이미 누가 짰나?"**가 끼어든다. mainline 커널에는 IIO 센서 드라이버만 수백 개가 있고, 같은 회사의 형제 칩은 대부분 같은 드라이버에 `compatible` 한 줄과 chip_info 하나를 더하는 것으로 끝난다. 그래서 실무 루틴은 이렇다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <rect x="10" y="30" width="155" height="66" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="52" font-size="13">1. 데이터시트 읽기</text>
  <text x="20" y="70" font-size="12">reg map, reset</text>
  <text x="20" y="86" font-size="12">timing, IRQ, errata</text>
  <rect x="180" y="30" width="155" height="66" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="190" y="52" font-size="13">2. 기존 코드 검색</text>
  <text x="190" y="70" font-size="12">grep compatible</text>
  <text x="190" y="86" font-size="12">bindings, lore</text>
  <rect x="350" y="30" width="155" height="66" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="360" y="52" font-size="13">3. 닮은 코드 복제</text>
  <text x="360" y="70" font-size="12">same bus+subsystem</text>
  <text x="360" y="86" font-size="12">small, recent</text>
  <rect x="520" y="30" width="155" height="66" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="530" y="52" font-size="13">4. regmap 표 작성</text>
  <text x="530" y="70" font-size="12">rd/wr/volatile</text>
  <text x="530" y="86" font-size="12">defaults, ranges</text>
  <rect x="520" y="170" width="155" height="66" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="530" y="192" font-size="13">5. 첫 probe</text>
  <text x="530" y="210" font-size="12">i2c-stub or board</text>
  <text x="530" y="226" font-size="12">CHIP_ID check</text>
  <rect x="350" y="170" width="155" height="66" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="360" y="192" font-size="13">6. 반복 bring-up</text>
  <text x="360" y="210" font-size="12">i2cget, i2cdump</text>
  <text x="360" y="226" font-size="12">debugfs regmap</text>
  <rect x="180" y="170" width="155" height="66" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="190" y="192" font-size="13">7. DT binding</text>
  <text x="190" y="210" font-size="12">YAML, dt-validate</text>
  <text x="190" y="226" font-size="12">dtbs_check</text>
  <rect x="10" y="170" width="155" height="66" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="20" y="192" font-size="13">8. 리뷰 · 반영</text>
  <text x="20" y="210" font-size="12">checkpatch, W=1</text>
  <text x="20" y="226" font-size="12">upstream or BSP</text>
  <line x1="165" y1="63" x2="178" y2="63" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="335" y1="63" x2="348" y2="63" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="505" y1="63" x2="518" y2="63" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="597" y1="96" x2="597" y2="168" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="520" y1="203" x2="507" y2="203" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="350" y1="203" x2="337" y2="203" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="180" y1="203" x2="167" y2="203" stroke="currentColor" marker-end="url(#ah1)"/>
  <path d="M427,236 L427,270 L87,270 L87,98" fill="none" stroke="#d0564a" stroke-dasharray="5,4" marker-end="url(#ah1)"/>
  <text x="140" y="290" font-size="12">값이 이상하면 데이터시트로 돌아간다 (에라타, 타이밍, 리셋값)</text>
  <text x="200" y="135" font-size="12">2에서 찾으면 3~4는 compatible 추가로 끝날 수 있다</text>
</svg>
```
그림 1 — 데이터시트에서 드라이버까지의 실무 루틴. 파란 단계는 책상 작업, 주황은 칩(또는 가짜 칩)과의 반복, 초록은 마무리다. 빨간 점선이 실제로 가장 많이 도는 고리다.

이 노트의 실습 지도:

| 단계 | 실습 | 절 |
|---|---|---|
| 2 기존 드라이버 검색 | 6.18 소스에서 ADXL345를 compatible·ID값·Kconfig로 찾기, 닮은 드라이버 고르기, 서브시스템별 regmap 사용률 측정 | §2 |
| 5 첫 probe (기존 드라이버) | i2c-stub에 LSM6DSO를 흉내 내서 mainline `st_lsm6dsx`를 probe — 틀린 WHO_AM_I로 먼저 실패, 고친 뒤 IIO 값 읽기. ADXL345의 "조용한 -19", BMP280의 "경고만 하고 붙음" 비교 | §4 |
| 3~6 새 드라이버 | 가상 칩 TX31 데이터시트 → regmap 기반 IIO 드라이버를 처음부터 작성, i2c-stub bank로 페이지 레지스터까지 흉내, 에라타·캐시·volatile을 debugfs로 확인 | §5~§7 |
| 7 binding | TX31 YAML binding을 dt-doc-validate / dt-validate로 검증 | §8 |
| 정리 | "데이터시트 줄 → 코드 줄" 체크리스트 | §9 |

실행 환경: Lima VM `elinux`(호스트 커널 `7.0.0-38-generic`, aarch64) 안의 QEMU `virt` 게스트, 커널 **`6.18.55-dom`**(`/var/lab/out/Image-dom`) + `rootfs-dom`(Ubuntu 24.04 + systemd, i2c-tools 포함)을 qcow2 overlay로 부팅했다. QEMU는 TCG(소프트웨어 에뮬레이션)이고 VM을 19명이 같이 쓰는 중이라 dmesg 타임스탬프 간격은 실제 HW 시간이 아니다.

---

## 1. 드라이버 작성자의 데이터시트 읽기

### 1.1 직관 — 데이터시트는 "칩의 행동 명세서"다

데이터시트 200쪽 중 드라이버 작성자가 실제로 코드로 옮기는 부분은 20쪽 남짓이다. 전기적 특성, 패키지 도면, 레이아웃 가이드는 HW 팀 몫이다. 드라이버가 알아야 하는 것은 **"이 칩은 어떻게 행동하나"** 하나다: 어떤 레지스터가 혼자 바뀌는가, 읽으면 무엇이 지워지는가, 전원 후 얼마나 기다려야 하는가, 리셋하면 무엇이 되돌아가는가.

Don이 SSD 펌웨어에서 NAND 데이터시트를 읽던 방식 — "명령 시퀀스, tR/tPROG/tBERS 타이밍, status 비트, 리비전별 차이" — 이 그대로 통한다. 다른 점은 Linux에서는 그 행동을 **코드가 아니라 표(table)로 선언**하는 부분이 많다는 것이다(regmap의 volatile/precious 표, reg_defaults). 그 표를 틀리면 프레임워크가 잘못된 가정으로 버스를 건너뛴다.

### 1.2 읽는 순서와 뽑아낼 것

| 순서 | 데이터시트 절 | 뽑아낼 것 | 코드에서 가는 곳 |
|---|---|---|---|
| 1 | 개요 · 블록도 | 무슨 서브시스템인가 (IIO 가속도? hwmon 온도? input?) | 드라이버 위치(`drivers/iio/...`), 프레임워크 선택(I1) |
| 2 | 디지털 인터페이스 | I2C 주소(핀 스트랩), 최대 속도, auto-increment, SPI 모드, 레지스터 주소/값 폭 | `reg_bits`/`val_bits`, binding의 `reg` enum |
| 3 | 레지스터 맵 표 | 주소, R/W, 리셋값, 비트필드 | `rd_table`/`wr_table`, `reg_defaults`, `REG_FIELD` |
| 4 | 레지스터 설명 본문 | "자동 해제", "읽으면 지워짐", "쓰기 전용", "읽기 순서" | `volatile_table`, `precious_table`, 캐시 우회 |
| 5 | 전원 · 리셋 시퀀스 | t_PU, soft reset 명령과 대기 시간, 레귤레이터 순서 | `fsleep()`, `regulator_bulk_enable`, reset GPIO(H3) |
| 6 | 동작 모드 · 타이밍 | ODR 표, 변환 시간, DRDY 의미 | `regmap_read_poll_timeout`, `sampling_frequency` |
| 7 | 인터럽트 | 핀 극성, 래치/펄스, 무엇이 clear 하는가 | `IRQF_TRIGGER_*`, DT `interrupts`, precious |
| 8 | FIFO | 깊이, 워터마크, 한 레지스터 반복 읽기(no-increment) | `regmap_noinc_read`, IIO buffer |
| 9 | 에라타 / 리비전 노트 | 리비전 ID, 잘못된 리셋값, 금지 시퀀스 | REV_ID 분기, 강제 쓰기 |

Don의 경험으로 하나 덧붙이면: **에라타 문서는 데이터시트와 별도 파일**인 경우가 많고, 벤더 포털 로그인 뒤에 있다. 드라이버 리뷰에서 "왜 이 레지스터를 두 번 쓰나?" 같은 코드는 대부분 에라타에서 왔다. 주석에 에라타 번호를 남기는 습관이 중요하다.

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="70" y="18" font-size="13">데이터시트 절</text>
  <text x="470" y="18" font-size="13">코드 / 표</text>
  <rect x="10" y="30" width="240" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="53" font-size="13">Register map 표 (R/W 열)</text>
  <rect x="10" y="80" width="240" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="103" font-size="13">Reset value 열</text>
  <rect x="10" y="130" width="240" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="153" font-size="13">Timing: t_PU, t_RST, 변환 시간</text>
  <rect x="10" y="180" width="240" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="203" font-size="13">Interrupt: 극성, clear 조건</text>
  <rect x="10" y="230" width="240" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="253" font-size="13">FIFO, burst 읽기 순서</text>
  <rect x="10" y="280" width="240" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="20" y="303" font-size="13">Errata: Rev A 리셋값 오류</text>
  <rect x="400" y="30" width="250" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="53" font-size="12">rd_table, wr_table, max_register</text>
  <rect x="400" y="80" width="250" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="103" font-size="12">reg_defaults (캐시 초기값)</text>
  <rect x="400" y="130" width="250" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="153" font-size="12">fsleep(), read_poll_timeout()</text>
  <rect x="400" y="180" width="250" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="203" font-size="12">IRQF_TRIGGER_*, precious_table</text>
  <rect x="400" y="230" width="250" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="253" font-size="12">volatile_table, bulk/noinc_read</text>
  <rect x="400" y="280" width="250" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="303" font-size="12">REV_ID 분기 + regmap_write_bits</text>
  <line x1="250" y1="48" x2="398" y2="48" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="250" y1="98" x2="398" y2="98" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="250" y1="148" x2="398" y2="148" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="250" y1="198" x2="398" y2="198" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="250" y1="248" x2="398" y2="248" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="250" y1="298" x2="398" y2="298" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="250" y1="248" x2="398" y2="56" stroke="#888" stroke-dasharray="4,3" marker-end="url(#ah2)"/>
</svg>
```
그림 2 — 데이터시트 절이 코드의 어느 부분이 되는가. 점선은 "burst 읽기 순서" 같은 본문 문장이 접근 표(volatile)까지 바꾸는 경우다. 빨간 칸(에라타)이 리뷰에서 가장 많이 질문받는다.

### 1.3 이 노트의 가상 칩 — TX31 데이터시트 발췌

실습 2(§6)에서 처음부터 드라이버를 쓸 대상이다. **가상의 칩**이고 실제 제품이 아니다. 하지만 실제 센서들에서 흔한 함정을 일부러 다 넣었다: 혼자 바뀌는 상태 레지스터, 읽으면 지워지는 인터럽트 소스, 쓰기 전용 명령 레지스터, 페이지 레지스터, burst 읽기 순서, 리비전 에라타.

**표 4-1. TX31 레지스터 맵** (I2C 주소 0x48 = ADDR 핀 GND, 0x49 = VDD. 8비트 주소/8비트 값)

| 주소 | 이름 | R/W | Reset | 설명 |
|---|---|---|---|---|
| 0x00 | CHIP_ID | R | 0xA3 | 고정 ID |
| 0x01 | REV_ID | R | 0x01/0x02 | 0x01 = Rev A, 0x02 = Rev B |
| 0x02 | STATUS | R | 0x00 | bit0 DRDY: 새 변환 완료 시 1, TEMP_H 읽으면 0 · bit1 BUSY |
| 0x03 | CTRL | RW | 0x00 | bit0 EN · bit[3:1] ODR (표 4-3) |
| 0x04 | INT_CFG | RW | 0x00 | bit0 DRDY_IE · bit1 ALERT_IE · bit7 INT_POL (0 = active-low) |
| 0x05 | INT_SRC | R | 0x00 | bit0 DRDY · bit1 ALERT. **읽으면 지워진다 (clear-on-read)** |
| 0x06 | TEMP_L | R | 0x00 | 온도 하위 바이트. **읽는 순간 TEMP_H가 래치된다** |
| 0x07 | TEMP_H | R | 0x00 | 온도 상위 바이트. 16비트 2의 보수, 1 LSB = 1/128 °C |
| 0x10~0x1F | (window) | - | - | PAGE_SEL에 따라 page 0 또는 page 1이 보인다 |
| 0x7E | CMD | W | - | 0xB6 쓰기 = soft reset. **쓰기 전용, 읽으면 0** |
| 0x7F | PAGE_SEL | RW | 0x00 | bit0 = page |

**표 4-2. 페이지 레지스터** (window 0x10부터)

| page | 오프셋 | 이름 | R/W | Reset | 설명 |
|---|---|---|---|---|---|
| 0 | 0x10 | THR_HI | RW | 0x50 | ALERT 상한, 1 °C/LSB (80 °C) |
| 0 | 0x11 | THR_LO | RW | 0x00 | ALERT 하한 |
| 0 | 0x12 | HYST | RW | 0x02 | 히스테리시스 2 °C |
| 1 | 0x10 | CAL_OFF | R | OTP | 공장 보정 오프셋, signed, raw 단위 (1/128 °C) |

**표 4-3. ODR**: ODR=0 → 1 Hz, 1 → 4 Hz, 2 → 16 Hz, 3 → 64 Hz. DRDY는 변환 1회마다 1이 된다.

**6장. 전원 투입 순서**

- 6.1 VDD 인가 후 **t_PU = 2 ms** 동안 I2C 접근 금지.
- 6.2 CMD(0x7E)에 0xB6을 쓰면 soft reset. **t_RST = 1 ms** 뒤 모든 레지스터가 Reset 값으로 돌아간다(PAGE_SEL 포함).
- 6.3 측정 시작: CTRL.EN = 1.
- 4.5 온도를 읽을 때는 TEMP_L, TEMP_H를 **한 번의 연속 읽기(또는 TEMP_L 먼저)** 로 읽는다. TEMP_H만 먼저 읽으면 이전 변환의 상위 바이트가 나온다.

**7장. 인터럽트**: INT 핀은 기본 active-low, level. DRDY_IE=1이면 변환마다 INT가 내려가고, INT_SRC를 읽어야 올라간다.

**에라타 E1 (Rev A)**: INT_CFG의 리셋값이 0x00이 아니라 **0x01**(DRDY_IE=1)이다. soft reset 후에도 마찬가지다. 드라이버는 INT_CFG를 **명시적으로** 써야 한다. Rev B에서 수정됨.

이 표만 보고 bare-metal 펌웨어라면 Don은 `tx31_init()`을 10분 안에 짠다. Linux에서는 그 전에 §2를 먼저 한다.

---

## 2. 기존 드라이버부터 찾는다

### 2.1 왜 먼저 찾나

- mainline에 이미 있으면 **유지보수가 공짜**다. 커널을 올릴 때(P5) 내 패치 스택에서 빠진다.
- 같은 회사의 형제 칩은 레지스터 맵을 대부분 공유한다. ID값만 다르고 나머지는 같은 경우가 흔하다 → 기존 드라이버에 `compatible`과 chip_info 하나 추가로 끝난다.
- 리뷰어는 "왜 기존 드라이버를 확장하지 않았나?"를 거의 반드시 묻는다.

### 2.2 무엇으로 찾나 — 검색 키 다섯 개

| 검색 키 | 명령 (git 트리라면 `git grep`) | 찾는 것 |
|---|---|---|
| compatible 문자열 | `git grep -n '"adi,adxl345"'` | DT 매칭 테이블이 있는 드라이버 |
| binding 문서 | `git grep -l adxl345 Documentation/devicetree/bindings` | DT에 써야 할 속성, 메인테이너 |
| 칩 이름 / Kconfig | `git grep -n 'config ADXL345'` | 켜야 할 CONFIG, 의존성 |
| 데이터시트의 ID값·레지스터 이름 | `git grep -niE 'DEVID.*0xe5'` | 이름이 다른 형제 칩 드라이버 |
| 메일링 리스트 (lore) | `lore.kernel.org/all/?q=adxl345` | 아직 머지 안 된 패치 시리즈 |

데이터시트의 ID값으로 역검색하는 것이 의외로 강력하다. 칩 이름은 벤더가 마케팅으로 바꾸지만 실리콘의 WHO_AM_I 값은 형제 칩끼리 같거나 연속이다.

### 2.3 예제 1 — ADXL345가 보드에 있다. 드라이버가 있나?

무엇을 확인하나: 랩 소스(`/var/lab/src/linux-6.18.55`, git 저장소가 아니라서 `grep -rn`으로 같은 검색을 한다)에서 다섯 가지 키로 찾는다.

```sh
#!/bin/sh
# ex1_find.sh — "ADXL345가 보드에 있다. Linux 드라이버가 이미 있나?" 를 소스 트리에서 찾는다
cd /var/lab/src/linux-6.18.55 || exit 1
echo "## 1) compatible 문자열로 (git grep -n 'adi,adxl345' 와 같은 검색)"
grep -rn --include='*.c' '"adi,adxl345"' drivers
echo "## 2) binding 문서"
grep -rl 'adi,adxl345' Documentation/devicetree/bindings
echo "## 3) 칩 이름으로 Kconfig (어떤 CONFIG를 켜야 하나)"
grep -rn -A2 '^config ADXL345' drivers/iio/accel/Kconfig | head -8
echo "## 4) 데이터시트의 ID 값 0xE5 + 레지스터 이름 DEVID 로 역검색"
grep -rniE 'DEVID.*0xe5' drivers include | head
echo "## 5) 같은 칩을 다루는 다른 서브시스템 드라이버가 있나 (input? iio?)"
grep -rli 'adxl34x\|adxl345' drivers --include='*.c' | sort
```

```text
## 1) compatible 문자열로 (git grep -n 'adi,adxl345' 와 같은 검색)
drivers/input/misc/adxl34x-i2c.c:114:	{ .compatible = "adi,adxl345", },
drivers/input/misc/adxl34x-i2c.c:117:	 * compatible values "adi,adxl345" and "adi,adxl346".
drivers/iio/accel/adxl345_i2c.c:53:	{ .compatible = "adi,adxl345", .data = &adxl345_i2c_info },
drivers/iio/accel/adxl345_spi.c:70:	{ .compatible = "adi,adxl345", .data = &adxl345_spi_info },
## 2) binding 문서
Documentation/devicetree/bindings/iio/accel/adi,adxl345.yaml
## 3) 칩 이름으로 Kconfig (어떤 CONFIG를 켜야 하나)
62:config ADXL345
63-	tristate
64-
65:config ADXL345_I2C
66-	tristate "Analog Devices ADXL345 3-Axis Digital Accelerometer I2C Driver"
67-	depends on INPUT_ADXL34X=n
--
79:config ADXL345_SPI
## 4) 데이터시트의 ID 값 0xE5 + 레지스터 이름 DEVID 로 역검색
drivers/iio/accel/adxl345.h:95:#define ADXL345_DEVID			0xE5
drivers/iio/accel/adxl313.h:35:#define ADXL313_DEVID0_ADXL312_314	0xE5
## 5) 같은 칩을 다루는 다른 서브시스템 드라이버가 있나 (input? iio?)
drivers/iio/accel/adxl345_core.c
drivers/iio/accel/adxl345_i2c.c
drivers/iio/accel/adxl345_spi.c
drivers/input/misc/adxl34x-i2c.c
drivers/input/misc/adxl34x-spi.c
drivers/input/misc/adxl34x.c
```

출력에서 볼 것:

- **같은 compatible을 가진 드라이버가 둘**이다 — 옛 input 드라이버(`adxl34x`, 키 이벤트/탭 감지용)와 IIO 드라이버. Kconfig의 `depends on INPUT_ADXL34X=n`이 둘 중 하나만 고르게 강제한다. 로봇 센서 키트라면 샘플 스트림이 필요하니 **IIO 쪽**을 고르고 `CONFIG_INPUT_ADXL34X`를 꺼야 한다. 이런 "같은 칩, 두 서브시스템" 상황은 오래된 칩에서 꽤 흔하다.
- ID값 검색에서 **ADXL313 계열도 0xE5**를 쓴다는 것이 덤으로 나왔다. DEVID 하나만으로 칩을 구분하면 안 된다는 신호이고, 그래서 ADXL313 드라이버는 DEVID0/DEVID1/PARTID를 같이 본다.
- `.data = &adxl345_i2c_info` — 매칭 테이블이 chip_info 포인터를 들고 다닌다. 이 사실이 §4.2에서 실제 문제로 돌아온다.

### 2.4 예제 2 — TX31과 가장 닮은 드라이버 후보

무엇을 확인하나: TX31은 mainline에 없다(가상 칩이니 당연하다). 그러면 "I2C + regmap + IIO 온도 + 페이지 레지스터" 조합에서 복제 기준 드라이버를 찾는다.

```sh
#!/bin/sh
# ex2_similar.sh — TX31(I2C 온도, 페이지 레지스터)과 가장 닮은 기존 드라이버 후보 찾기
cd /var/lab/src/linux-6.18.55 || exit 1
echo "## A) IIO temperature 중 regmap-i2c를 쓰는 드라이버 (줄 수 = 복제 비용)"
for f in $(grep -l 'devm_regmap_init_i2c' drivers/iio/temperature/*.c); do
	printf '%5d %s\n' "$(wc -l < "$f")" "$f"
done | sort -n
echo "## B) regmap 페이지(regmap_range_cfg)를 쓰는 IIO 드라이버"
grep -rl 'struct regmap_range_cfg' drivers/iio | sort
echo "## C) 같은 조합(IIO + regmap-i2c + range_cfg)을 hwmon 에서도"
grep -l 'regmap_range_cfg' $(grep -rl 'devm_regmap_init_i2c' drivers/hwmon) 2>/dev/null
```

```text
## A) IIO temperature 중 regmap-i2c를 쓰는 드라이버 (줄 수 = 복제 비용)
 1094 drivers/iio/temperature/mlx90635.c
 1344 drivers/iio/temperature/mlx90632.c
## B) regmap 페이지(regmap_range_cfg)를 쓰는 IIO 드라이버
drivers/iio/imu/bno055/bno055.c
drivers/iio/imu/inv_icm42600/inv_icm42600_core.c
## C) 같은 조합(IIO + regmap-i2c + range_cfg)을 hwmon 에서도
drivers/hwmon/spd5118.c
```

출력에서 볼 것: 완벽히 닮은 드라이버는 없다. 온도 쪽 후보는 1000줄이 넘는 IR 온도계(EEPROM 보정, 전원 모드가 복잡)라서 통째로 복제하면 지울 코드가 더 많다. 이럴 때는 **두 곳에서 부분을 빌린다**: 구조(IIO `read_raw`, `chan_spec`)는 작은 IIO 드라이버에서, 페이지 처리(`regmap_range_cfg`)는 `inv_icm42600_core.c`나 `spd5118.c`에서. §6의 TX31 드라이버가 그렇게 만들어졌다.

### 2.5 예제 3 — 서브시스템별 regmap 사용률과 "가장 작은 복제 후보"

무엇을 확인하나: `struct i2c_driver`가 있는 파일 중 몇 개가 regmap-i2c를 쓰는지 센다. 복제 기준을 고를 때 "이 서브시스템의 관례가 regmap인가"를 숫자로 보는 것이다.

```sh
#!/bin/sh
# ex3_stats.sh — I2C 드라이버가 regmap을 얼마나 쓰나 (서브시스템별), 작은 복제 후보
cd /var/lab/src/linux-6.18.55 || exit 1
for d in drivers/iio drivers/hwmon drivers/input drivers/mfd drivers/regulator; do
	all=$(grep -rl --include='*.c' 'struct i2c_driver' $d | wc -l)
	rm=$(grep -rl --include='*.c' 'struct i2c_driver' $d | xargs grep -l 'regmap_init_i2c' | wc -l)
	printf '%-18s i2c_driver=%3d  regmap_i2c=%3d\n' "$d" "$all" "$rm"
done
echo "## IIO에서 regmap-i2c + i2c_driver 를 한 파일에 가진 가장 작은 드라이버 5개"
grep -rl --include='*.c' 'struct i2c_driver' drivers/iio | xargs grep -l 'devm_regmap_init_i2c' |
	xargs wc -l | grep -v total | sort -n | head -5
```

```text
drivers/iio        i2c_driver=235  regmap_i2c=101
drivers/hwmon      i2c_driver=186  regmap_i2c= 35
drivers/input      i2c_driver= 93  regmap_i2c= 17
drivers/mfd        i2c_driver=105  regmap_i2c= 81
drivers/regulator  i2c_driver= 71  regmap_i2c= 61
## IIO에서 regmap-i2c + i2c_driver 를 한 파일에 가진 가장 작은 드라이버 5개
     55 drivers/iio/magnetometer/rm3100-i2c.c
     56 drivers/iio/accel/bma400_i2c.c
     57 drivers/iio/imu/bno055/bno055_i2c.c
     60 drivers/iio/accel/fxls8962af-i2c.c
     62 drivers/iio/accel/mma7455_i2c.c
```

```svg
<svg viewBox="0 0 620 250" xmlns="http://www.w3.org/2000/svg">
  <text x="150" y="20" font-size="13">I2C 드라이버 중 regmap-i2c 사용 비율 (6.18.55, 파일 수 기준)</text>
  <line x1="150" y1="35" x2="150" y2="215" stroke="currentColor"/>
  <line x1="150" y1="215" x2="550" y2="215" stroke="currentColor"/>
  <text x="146" y="232" font-size="12">0</text>
  <text x="338" y="232" font-size="12">50%</text>
  <text x="532" y="232" font-size="12">100%</text>
  <line x1="350" y1="211" x2="350" y2="219" stroke="currentColor"/>
  <line x1="550" y1="211" x2="550" y2="219" stroke="currentColor"/>
  <text x="10" y="60" font-size="13">regulator</text>
  <rect x="150" y="45" width="343.7" height="22" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="500" y="61" font-size="12">86% (61/71)</text>
  <text x="10" y="95" font-size="13">mfd (PMIC)</text>
  <rect x="150" y="80" width="308.6" height="22" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="465" y="96" font-size="12">77% (81/105)</text>
  <text x="10" y="130" font-size="13">iio (센서)</text>
  <rect x="150" y="115" width="171.9" height="22" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="329" y="131" font-size="12">43% (101/235)</text>
  <text x="10" y="165" font-size="13">hwmon</text>
  <rect x="150" y="150" width="75.3" height="22" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="232" y="166" font-size="12">19% (35/186)</text>
  <text x="10" y="200" font-size="13">input</text>
  <rect x="150" y="185" width="73.1" height="22" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="230" y="201" font-size="12">18% (17/93)</text>
</svg>
```
그림 3 — 예제 3에서 직접 센 값. PMIC·레귤레이터는 거의 전부 regmap이다(레지스터가 많고 suspend 시 캐시 복원이 필요하다). hwmon·input은 오래된 드라이버가 많아 `i2c_smbus_read_byte_data()`를 직접 부르는 비율이 높다. 새로 쓰는 센서 드라이버는 regmap이 관례다.

출력에서 볼 것: 가장 작은 파일들이 전부 `-i2c.c`로 끝난다. 요즘 IIO 드라이버는 **core + 버스 glue로 쪼개져** 있어서 I2C 파일은 regmap 초기화와 매칭 테이블만 가진 50~60줄짜리다. 복제할 때는 `xxx_core.c`(칩 로직)와 `xxx_i2c.c`(glue)를 같이 본다. SPI 버전이 필요해질 때 core를 그대로 재사용하기 위한 구조다.

### 2.6 lore 검색 — 아직 머지 안 된 드라이버 (가이드, 실행 안 함)

mainline에 없다고 끝이 아니다. 누가 6개월 전에 v3까지 보냈다가 리뷰에서 멈췄을 수 있다. lore.kernel.org는 모든 커널 메일링 리스트의 아카이브이고 public-inbox 검색 문법을 쓴다.

```text
https://lore.kernel.org/all/?q=tx31                       칩 이름으로 전체 리스트 검색
https://lore.kernel.org/linux-iio/?q=s:tx31               linux-iio 리스트, 제목(s:)에 tx31
https://lore.kernel.org/all/?q=dfn:drivers/iio/temperature/   그 경로를 건드린 패치(dfn: = diff 파일명)
```

찾은 시리즈는 `b4 am <message-id>`로 mbox를 받아 `git am`으로 내 트리에 올려 본다. 리뷰 코멘트("이 레지스터는 volatile이어야 한다", "binding에 vdd-supply 빠짐")가 내 드라이버의 무료 리뷰가 된다. 이 노트의 랩에서는 메일·네트워크 활동을 하지 않으므로 실행하지 않았다.

### 2.7 검색 결과별 다음 행동

| 결과 | 다음 행동 | 예 |
|---|---|---|
| 같은 칩 드라이버가 있고 compatible도 있다 | CONFIG 켜고 DT 노드만 쓴다 (P4) | ADXL345 → `CONFIG_ADXL345_I2C`, `adi,adxl345` |
| 형제 칩 드라이버가 있다 (ID만 다름) | chip_info 추가 + compatible 추가 + binding enum 추가 | LSM6DS 계열 `st_lsm6dsx`에 새 WHO_AM_I |
| 같은 칩이 다른 서브시스템에 있다 | 어느 서브시스템이 요구사항에 맞는지 고른다 | input `adxl34x` vs IIO `adxl345` |
| lore에 멈춘 시리즈가 있다 | 받아서 테스트, 리뷰 반영해 이어받기 (P7) | 원작자에게 연락 후 v(n+1) |
| 아무것도 없다 | 가장 닮은 드라이버를 골라 복제 (§3) | TX31 |

---

## 3. 가장 닮은 드라이버를 골라 복제한다

### 3.1 고르는 기준

| 기준 | 이유 |
|---|---|
| 같은 버스 (I2C/SPI) | regmap 초기화, 매칭 테이블 형태가 같다 |
| 같은 서브시스템 (IIO/hwmon) | `read_raw`, sysfs ABI를 그대로 쓴다 |
| 작다 (300~600줄) | 지울 코드가 적다 |
| 최근에 머지됐다 | `devm_*`, `dev_err_probe`, `i2c_get_match_data`, maple 캐시 같은 현재 관례를 따른다. 10년 된 드라이버는 리뷰어가 싫어하는 패턴(수동 해제, `of_match_ptr`)을 갖고 있다 |
| 같은 회사 칩 | 레지스터 스타일(페이지, 명령 레지스터)이 비슷하다 |

### 3.2 복제 후 반드시 바꾸는 것

1. 접두어 전부 (`bma400_` → `tx31_`). `sed`로 바꾸고 나서 `grep -i bma400`이 0줄인지 확인한다.
2. 레지스터 헤더 — 데이터시트 표 4-1을 처음부터 다시 옮긴다. 복제 원본 값을 "비슷하니까" 남기면 안 된다.
3. regmap 접근 표 — **원본 칩의 volatile 표를 그대로 두는 것이 가장 흔한 사고**다(§5.3).
4. probe 순서 — 데이터시트 6장 순서로 다시 쓴다.
5. 매칭 테이블, `MODULE_DESCRIPTION`, 저작권 줄(원본 저작권은 유지하고 "based on" 표기).
6. binding YAML도 같은 방식으로 복제 — 원본 binding을 복사하고 `$id`, title, compatible, 속성을 바꾼다(§8).

---

## 4. 실습 1 — i2c-stub으로 "칩 없이" 기존 드라이버를 probe 한다

### 4.1 i2c-stub이란

i2c-stub은 커널이 제공하는 **가짜 SMBus 어댑터**다. 모듈 인자로 준 주소에 "칩"이 있는 척 하고, 각 칩마다 256개짜리 메모리 배열을 레지스터로 쓴다. 쓰면 저장되고 읽으면 돌려준다. `i2cset`으로 미리 값을 심어 두면, **진짜 드라이버가 그 값을 보고 probe** 한다.

bare-metal 비유: 펌웨어 단위 테스트에서 레지스터 접근 함수를 메모리 배열로 바꿔 치는 것(HAL mock)과 같다. 다만 커널 쪽 코드는 진짜 그대로다.

한계(실험하며 하나씩 확인한다):

- 칩의 **행동**은 흉내 내지 않는다 — 자동 해제 비트, clear-on-read, 변환 완료 플래그가 없다. 그래서 H2·H5 노트는 자기 가짜 어댑터를 따로 썼다.
- 지원하는 트랜잭션은 SMBus quick/byte/byte data/word data/I2C block. **순수 `i2c_transfer`(I2C_FUNC_I2C)는 없다** → regmap-i2c는 SMBus 백엔드를 고른다.
- `bank_reg`/`bank_mask`/`bank_start`/`bank_end` 인자로 페이지 레지스터를 흉내 낼 수 있다(§6에서 쓴다).

준비 — rootfs-dom을 overlay로 부팅하고 9p로 작업 폴더를 공유했다(이후 게스트 명령은 이 셸에서 실행).

```sh
# VM 쪽 (부팅 스크립트 핵심 부분)
qemu-img create -q -f qcow2 -F raw -b /var/lab/out/rootfs-dom.ext4 /var/tmp/P3/p3.qcow2
qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1536M -display none \
  -kernel /var/lab/out/Image-dom -drive file=/var/tmp/P3/p3.qcow2,if=virtio,format=qcow2 \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -virtfs local,path=/var/tmp/P3/share,mount_tag=p3share,security_model=none,id=p3fs \
  -serial unix:/var/tmp/P3/s.sock,server,nowait -monitor none \
  -append "console=ttyAMA0 root=/dev/vda rw rootwait systemd.default_device_timeout_sec=900"
# 게스트 안
mkdir -p /mnt/p3 && mount -t 9p -o trans=virtio p3share /mnt/p3
```

### 4.2 예제 4 — 가짜 칩 만들기, 그리고 ADXL345의 "조용한 -19"

무엇을 확인하나: 0x53에 빈 칩(전부 0)을 만들고, 일부러 ID가 틀린 상태로 mainline `adxl345_i2c`를 붙인다. "Invalid device ID" 에러가 나올 거라고 예상했다.

```sh
modprobe i2c-stub chip_addr=0x53 && i2cdetect -l && dmesg | tail -2
i2cdetect -y 0 0x50 0x57; i2cdump -y 0 0x53 b | head -5
modprobe adxl345_i2c
echo adxl345 0x53 > /sys/bus/i2c/devices/i2c-0/new_device; sleep 1
dmesg | tail -4; ls /sys/bus/i2c/devices/0-0053/; ls /sys/bus/iio/devices/
```

```text
i2c-0	smbus     	SMBus stub driver               	SMBus adapter
[   80.513054] virtio_net virtio0 enp0s1: renamed from eth0
[  128.346961] i2c-stub: Virtual chip at 0x53
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                                                 
...
50: -- -- -- 53 -- -- -- --                         
...
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f    0123456789abcdef
00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    ................
10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    ................
20: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    ................
30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    ................
[   61.415006] audit: type=1400 audit(1792341157.052:105): apparmor="STATUS" operation="profile_load" profile="unconfined" name="wpcom" pid=268 comm="apparmor_parser"
[   80.513054] virtio_net virtio0 enp0s1: renamed from eth0
[  128.346961] i2c-stub: Virtual chip at 0x53
[  136.994966] i2c i2c-0: new_device: Instantiated device adxl345 at 0x53
modalias  name  power  subsystem  uevent
```

(i2cdetect 출력의 빈 행은 `...`로 줄였다. 마지막 `ls /sys/bus/iio/devices/`는 빈 줄을 냈다.) 장치는 생겼는데 `driver` 링크가 없고 IIO 장치도 없고 **에러 메시지도 없다**. driver core의 dynamic debug를 켜고 다시 bind 해 본다(M1).

```sh
echo "file drivers/base/dd.c +p" > /sys/kernel/debug/dynamic_debug/control
echo 0-0053 > /sys/bus/i2c/drivers/adxl345_i2c/bind; dmesg | tail -3
```

```text
-bash: echo: write error: No such device
[  157.346625] i2c 0-0053: bus: 'i2c': __driver_probe_device: matched device with driver adxl345_i2c
[  157.346900] i2c 0-0053: bus: 'i2c': really_probe: probing driver adxl345_i2c with device
[  157.354786] adxl345_i2c 0-0053: probe with driver adxl345_i2c rejects match -19
```

출력에서 볼 것: probe가 `-ENODEV`(-19)를 돌려줬고, driver core는 -ENODEV/-ENXIO를 "이 장치는 내 것이 아니다"로 해석해서 **dev_dbg로만** 남긴다. 원인은 §2.3에서 본 `.data = &adxl345_i2c_info`다. `adxl345_core_probe()`의 앞부분:

```c
	st->info = device_get_match_data(dev);
	if (!st->info)
		return -ENODEV;
```

`device_get_match_data()`는 DT/ACPI 노드(fwnode)에서 매칭 데이터를 찾는다. `new_device`로 만든 장치는 fwnode가 없어서 NULL → 레지스터를 한 번도 읽기 전에 조용히 -19. 실보드에서 DT 노드로 만들면 문제가 없지만, **"sysfs new_device로 빠르게 시험해 보자"가 이 드라이버에는 안 통한다.** 요즘 드라이버는 `i2c_get_match_data()`(fwnode가 없으면 `i2c_device_id`의 `driver_data`로 대체, 6.18 `include/linux/i2c.h`에 있음)를 써서 이 문제를 피한다 — 리뷰에서 자주 나오는 수정이다. 이 실험은 다른 칩으로 넘어간다.

### 4.3 예제 5 — LSM6DSO: 틀린 WHO_AM_I로 먼저 실패

무엇을 확인하나: `st_lsm6dsx_i2c`는 fwnode가 없으면 `i2c_device_id`의 데이터로 대체한다(소스에서 확인). 0x6a에 빈 칩을 만들고 lsm6dso로 붙인다. WHO_AM_I(0x0F)가 0이니 실패해야 한다.

```sh
echo 0x53 > /sys/bus/i2c/devices/i2c-0/delete_device; rmmod adxl345_i2c adxl345_core; rmmod i2c-stub
modprobe i2c-stub chip_addr=0x6a,0x48 bank_reg=0,0x7f bank_mask=0,0x01 bank_start=0,0x10 bank_end=0,0x1f
modprobe st_lsm6dsx_i2c
echo lsm6dso 0x6a > /sys/bus/i2c/devices/i2c-0/new_device; sleep 1; dmesg | tail -4
```

```text
[  190.148791][  T350] st_lsm6dsx_i2c 0-006a: unsupported whoami [00]
[  190.089902] st_lsm6dsx_i2c 0-006a: supply vddio not found, using dummy regulator
[  190.148791] st_lsm6dsx_i2c 0-006a: unsupported whoami [00]
[  190.160647] st_lsm6dsx_i2c 0-006a: probe with driver st_lsm6dsx_i2c rejects match -19
[  190.161020] i2c i2c-0: new_device: Instantiated device lsm6dso at 0x6a
```

출력에서 볼 것: 이번에는 **드라이버가 직접 에러를 찍는다** (`unsupported whoami [00]`). 첫 줄의 `[T350]`이 붙은 것은 콘솔로 바로 나간 같은 메시지다. `supply vddio not found, using dummy regulator`는 DT가 없어서 레귤레이터를 못 찾았다는 뜻인데, 실보드에서 이 줄이 나오면 **DT에 `vddio-supply`가 빠진 것**이다(H3, P4). 실보드 bring-up 첫날 가장 흔한 로그가 바로 이 두 줄이다: "ID가 0x00 또는 0xff" → 전원/리셋/주소 문제, "dummy regulator" → DT 문제.

### 4.4 예제 6 — ID를 고치면 probe 성공, IIO에서 값 읽기

무엇을 확인하나: WHO_AM_I에 LSM6DSO 값 0x6C(드라이버 표 `st_lsm6dsx_sensor_settings`에 있다)를 심고 다시 bind 한다. 그 다음 가속도 출력 레지스터(OUTX_L_A 0x28~0x2D)에 값을 심고 IIO sysfs로 읽는다. i2c-stub의 dynamic debug를 켜서 드라이버가 낸 트랜잭션도 본다.

```sh
i2cset -f -y 0 0x6a 0x0f 0x6c; i2cget -f -y 0 0x6a 0x0f
echo "module i2c_stub +p" > /sys/kernel/debug/dynamic_debug/control
dmesg -C; echo 0-006a > /sys/bus/i2c/drivers/st_lsm6dsx_i2c/bind
dmesg | grep -c i2c-stub; dmesg | grep -v i2c-stub | tail -5; ls /sys/bus/iio/devices/
```

```text
0x6c
0
[  216.835356] i2c i2c-0: i2c block data - addr 0x6a, read  1 bytes at 0x01.
[  216.835380] i2c i2c-0: i2c block data - addr 0x6a, wrote 1 bytes at 0x01.
[  217.034848] st_lsm6dsx_i2c 0-006a: mounting matrix not found: using identity...
[  217.712760] st_lsm6dsx_i2c 0-006a: driver: 'st_lsm6dsx_i2c': driver_bound: bound to device
[  217.714308] st_lsm6dsx_i2c 0-006a: bus: 'i2c': really_probe: bound device to driver st_lsm6dsx_i2c
iio:device0  iio:device1
```

(둘째 줄 `0`은 `grep -c i2c-stub`의 결과다 — i2c-stub의 로그는 장치 이름 `i2c i2c-0:`으로 찍혀서 문자열 "i2c-stub"이 없었다. 그래서 아래 줄들에 트랜잭션 로그가 섞여 나왔다.) 이제 값을 심고 읽는다. X = 0x1000 = 4096, Y = 0xF000 = -4096, Z = 0x4109 = 16649.

```sh
for r in 0x28:0x00 0x29:0x10 0x2a:0x00 0x2b:0xf0 0x2c:0x09 0x2d:0x41; do
	i2cset -f -y 0 0x6a ${r%:*} ${r#*:}; done
cd /sys/bus/iio/devices/iio:device1; dmesg -C
cat in_accel_x_raw in_accel_y_raw in_accel_z_raw in_accel_scale
dmesg | grep "addr 0x6a" | head -12
```

```text
4096
-4096
16649
0.000598205
[  272.104214] i2c i2c-0: i2c block data - addr 0x6a, read  1 bytes at 0x10.
[  272.104584] i2c i2c-0: i2c block data - addr 0x6a, wrote 1 bytes at 0x10.
[  272.355154] i2c i2c-0: i2c block data - addr 0x6a, read  2 bytes at 0x28.
[  272.355291] i2c i2c-0: i2c block data - addr 0x6a, read  1 bytes at 0x10.
[  272.355325] i2c i2c-0: i2c block data - addr 0x6a, wrote 1 bytes at 0x10.
[  272.363000] i2c i2c-0: i2c block data - addr 0x6a, read  1 bytes at 0x10.
[  272.364103] i2c i2c-0: i2c block data - addr 0x6a, wrote 1 bytes at 0x10.
[  272.684512] i2c i2c-0: i2c block data - addr 0x6a, read  2 bytes at 0x2a.
[  272.684602] i2c i2c-0: i2c block data - addr 0x6a, read  1 bytes at 0x10.
[  272.684632] i2c i2c-0: i2c block data - addr 0x6a, wrote 1 bytes at 0x10.
[  272.688540] i2c i2c-0: i2c block data - addr 0x6a, read  1 bytes at 0x10.
[  272.688598] i2c i2c-0: i2c block data - addr 0x6a, wrote 1 bytes at 0x10.
```

출력에서 볼 것:

- 값이 그대로 나온다. Z = 16649 × 0.000598205 m/s² ≈ 9.96 m/s² ≈ 1 g. **진짜 mainline 드라이버가 가짜 칩의 레지스터로 IIO 값을 만들었다.**
- `in_accel_x_raw` 한 번 = CTRL1_XL(0x10) read-modify-write로 센서 켜기 → (ODR 안정 대기) → 0x28에서 2바이트 → 0x10 RMW로 끄기. **oneshot 읽기가 매번 센서를 켰다 끈다**는 드라이버 설계가 트랜잭션 로그로 그대로 보인다. 데이터시트의 "turn-on time" 표가 이 대기 시간이 된다.
- `read 1 bytes at 0x10` 다음에 `wrote` — 이 regmap은 캐시가 없어서 RMW마다 읽는다.
- 트랜잭션 종류가 `i2c block data`다. i2c-stub이 I2C_FUNC_I2C를 안 주니 regmap-i2c가 SMBus I2C block 백엔드를 골랐다(§5.1, 그림 4).

### 4.5 예제 7 — probe 한 번의 버스 트랜잭션, 그리고 i2c-stub이 못 하는 것

무엇을 확인하나: unbind/bind로 probe를 다시 돌리고 트랜잭션 전체를 센다. probe가 끝난 뒤 제어 레지스터 상태를 i2cdump로 본다.

```sh
echo 0-006a > /sys/bus/i2c/drivers/st_lsm6dsx_i2c/unbind; dmesg -C
echo 0-006a > /sys/bus/i2c/drivers/st_lsm6dsx_i2c/bind
dmesg | grep "addr 0x6a" > /tmp/p.log; wc -l < /tmp/p.log
sed "s/^\[.*\] i2c i2c-0: i2c block data - //" /tmp/p.log | head -24
i2cdump -f -y -r 0x0f-0x1f 0 0x6a b
```

```text
80
addr 0x6a, read  1 bytes at 0x01.
addr 0x6a, read  1 bytes at 0x0f.
addr 0x6a, read  2 bytes at 0x3a.
addr 0x6a, read  1 bytes at 0x0a.
addr 0x6a, read  1 bytes at 0x12.
addr 0x6a, read  1 bytes at 0x12.
addr 0x6a, read  1 bytes at 0x12.
addr 0x6a, read  1 bytes at 0x0d.
addr 0x6a, read  1 bytes at 0x56.
addr 0x6a, read  1 bytes at 0x56.
addr 0x6a, read  1 bytes at 0x13.
addr 0x6a, read  1 bytes at 0x01.
addr 0x6a, wrote 1 bytes at 0x01.
addr 0x6a, read  1 bytes at 0x14.
addr 0x6a, read  1 bytes at 0x01.
addr 0x6a, wrote 1 bytes at 0x01.
addr 0x6a, read  1 bytes at 0x19.
addr 0x6a, read  1 bytes at 0x63.
addr 0x6a, read  1 bytes at 0x01.
addr 0x6a, wrote 1 bytes at 0x01.
addr 0x6a, wrote 3 bytes at 0x15.
addr 0x6a, read  1 bytes at 0x01.
addr 0x6a, wrote 1 bytes at 0x01.
addr 0x6a, read  1 bytes at 0x10.
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f    0123456789abcdef
00:                                              6c                   l
10: 00 00 c1 08 03 00 00 00 00 20 00 00 00 00 00 00    ..???.... ......
```

출력에서 볼 것:

- probe 한 번에 트랜잭션 80개. 첫 줄 0x01은 WHO_AM_I보다 **먼저** 하는 "센서 허브 페이지 해제"다(소스 주석: 워치독 리셋 뒤 페이지가 선택된 채 남아 있으면 일반 레지스터가 가려진다). 페이지 레지스터를 가진 칩은 probe 첫 동작으로 페이지를 0으로 되돌려야 한다 — TX31에서도 다시 나온다. 0x01에 대한 `read → wrote` 쌍이 반복되는 것은 페이지 0↔센서 허브 페이지를 오가는 것이다.
- 0x12(CTRL3_C) = **0xc1** = BOOT(bit7) | BDU(bit6) | SW_RESET(bit0). 진짜 칩이라면 SW_RESET과 BOOT는 수 ms 뒤 **스스로 0이 된다**. i2c-stub은 쓴 값을 그대로 들고 있다. 이 드라이버는 리셋 뒤 `msleep(50)`만 하고 비트를 폴링하지 않아서 문제없이 지나갔지만, `regmap_read_poll_timeout()`으로 비트 해제를 기다리는 드라이버였다면 **i2c-stub 위에서는 타임아웃으로 probe가 실패**한다(Documentation/i2c/i2c-stub.rst의 Caveats에도 "polls … could lock it up"이라고 적혀 있다). 그럴 때는 다른 셸에서 `i2cset`으로 비트를 직접 내려 주거나, H2처럼 행동을 흉내 내는 가짜 어댑터를 쓴다.

### 4.6 예제 8 — 대조: BMP280은 ID가 틀려도 붙는다

무엇을 확인하나: 같은 "빈 칩" 상태로 bmp280을 붙인다.

```sh
modprobe i2c-stub chip_addr=0x76; modprobe bmp280-i2c
echo bmp280 0x76 > /sys/bus/i2c/devices/i2c-0/new_device; sleep 1
dmesg | grep -v "bus:\|driver:"; ls /sys/bus/iio/devices/
cat /sys/bus/iio/devices/iio:device0/name /sys/bus/iio/devices/iio:device0/in_temp_input
```

```text
[  821.575922] bmp280 0-0076: supply vddd not found, using dummy regulator
[  821.576758] bmp280 0-0076: supply vdda not found, using dummy regulator
[  821.582454] bmp280 0-0076: bad chip id: 0x0 is not a known chip id
[  821.614580] i2c i2c-0: new_device: Instantiated device bmp280 at 0x76
iio:device0
bmp280
0.000000000
```

출력에서 볼 것: **경고 한 줄만 찍고 probe 성공**, IIO 장치가 생기고 온도 0 °C를 내놓는다. 최근 IIO 메인테이너 방침은 "DT의 compatible을 믿는다 — 모르는 ID는 경고만"이다. 새 형제 칩이 fallback compatible로 기존 드라이버를 쓸 수 있게 하려는 것이다(bmi160도 같은 방식, `dev_warn(dev, "Chip id not found…")`). 결과적으로 **"probe 성공 = 칩이 있다"가 아니다.** bring-up 체크리스트에 "dmesg에서 chip id 경고 확인"과 "알려진 물리량(실내 온도, 1 g) 확인"을 꼭 넣어야 하는 이유다.

| 드라이버 | ID 불일치 시 | 메시지 |
|---|---|---|
| adxl345 (new_device) | 레지스터 읽기 전 -ENODEV | 없음 (dev_dbg로만 "rejects match -19") |
| st_lsm6dsx | -ENODEV | `unsupported whoami [00]` |
| bmp280, bmi160 | 경고 후 계속 | `bad chip id …` |

---

## 5. regmap — H2 다음 단계: 워크플로에서 실제로 고민하는 것

H2가 regmap API, 캐시 종류, volatile/precious, bulk/noinc, suspend 복원, ftrace를 다 다뤘다. 여기서는 **데이터시트 표를 옮기는 순간 내려야 하는 결정**만 본다.

### 5.1 계층 그림 — 어디서 무엇이 결정되나

```svg
<svg viewBox="0 0 640 400" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <rect x="110" y="10" width="420" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="31" font-size="13">드라이버 (p3_tx31.c)</text>
  <text x="120" y="50" font-size="12">regmap_read, update_bits, field_write, bulk_read</text>
  <rect x="110" y="82" width="420" height="96" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="103" font-size="13">regmap core</text>
  <text x="120" y="122" font-size="12">1) access 표: readable? writeable? volatile? precious?</text>
  <text x="120" y="140" font-size="12">2) range: 0x110 → PAGE_SEL=1 + window 0x10</text>
  <text x="120" y="158" font-size="12">3) cache (maple): hit면 버스 생략, update_bits 생략</text>
  <rect x="110" y="198" width="420" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="120" y="219" font-size="13">regmap-i2c 백엔드 (init 때 한 번 선택)</text>
  <text x="120" y="238" font-size="12">I2C_FUNC_I2C → i2c_transfer · I2C_BLOCK · BYTE_DATA</text>
  <rect x="110" y="270" width="420" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="295" font-size="13">i2c core: i2c_smbus_xfer / i2c_transfer</text>
  <rect x="110" y="330" width="200" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="120" y="352" font-size="13">i2c-stub (이 노트)</text>
  <text x="120" y="371" font-size="12">memory array, banks</text>
  <rect x="330" y="330" width="200" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="352" font-size="13">SoC I2C 컨트롤러</text>
  <text x="340" y="371" font-size="12">real board, scope</text>
  <rect x="540" y="82" width="95" height="96" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="550" y="103" font-size="13">debugfs</text>
  <text x="550" y="122" font-size="12">registers</text>
  <text x="550" y="140" font-size="12">access</text>
  <text x="550" y="158" font-size="12">cache_*</text>
  <rect x="5" y="270" width="95" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="15" y="291" font-size="13">i2c-tools</text>
  <text x="15" y="310" font-size="12">/dev/i2c-0</text>
  <line x1="320" y1="62" x2="320" y2="80" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="320" y1="178" x2="320" y2="196" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="320" y1="250" x2="320" y2="268" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="250" y1="310" x2="210" y2="328" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="390" y1="310" x2="430" y2="328" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="538" y1="130" x2="532" y2="130" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="100" y1="290" x2="108" y2="290" stroke="currentColor" marker-end="url(#ah3)"/>
</svg>
```
그림 4 — regmap 계층. 데이터시트에서 옮긴 표는 전부 core(초록)에 들어간다. 백엔드(주황)는 어댑터 능력에 따라 init 때 고정된다. i2c-tools는 regmap을 **우회**해서 i2c core로 바로 가므로 "캐시가 모르는 HW 값"을 볼 수 있다 — §6의 실험이 이 차이를 이용한다.

### 5.2 결정 1 — 이 레지스터는 캐시해도 되나

질문 하나로 정한다: **"드라이버가 쓰지 않았는데 값이 바뀔 수 있나?"**

| 데이터시트 표현 | 분류 | TX31 |
|---|---|---|
| "status", "flag", "data output", "counter" | volatile | STATUS, TEMP_L/H |
| "self-clearing", "auto-cleared after reset" | volatile (그 비트가 있는 레지스터 전체) | (TX31은 CMD로 분리했다) |
| "cleared on read", "read to acknowledge" | volatile + **precious** | INT_SRC |
| "factory trimmed", "OTP", "read-only ID" | 캐시해도 됨 (한 번 읽고 끝) | CHIP_ID, CAL_OFF |
| "write-only", "reads as 0" | readable에서 제외. **캐시 함정 주의(§5.4)** | CMD |
| 설정 레지스터 | 캐시 + reg_defaults | CTRL, INT_CFG, `THR_*` |

### 5.3 결정 2 — reg_defaults를 믿어도 되나

`reg_defaults`는 "리셋 직후 HW 값 = 이 값"이라는 **약속**이다. 이 약속이 깨지는 경우:

- 부트로더가 칩을 이미 설정했다 (U-Boot이 PMIC 전압을 바꿈) → 커널 시작 시점 HW ≠ 리셋값. 해결: probe에서 soft reset 하거나, `reg_defaults` 대신 `num_reg_defaults_raw`로 HW에서 읽어 채우거나, 캐시 없는 상태로 시작.
- **에라타로 리셋값이 다르다** (TX31 E1). 해결: 해당 레지스터를 무조건 쓴다(`regmap_write` / `regmap_write_bits`).
- 복제 원본 칩의 defaults를 그대로 남겼다. 해결: 표를 다시 옮긴다.

캐시가 HW와 다르면 `regmap_update_bits()`는 "바뀔 게 없다"고 판단해서 **버스에 아무것도 안 낸다**. 버그가 소리 없이 생긴다. §6.6에서 실제로 본다.

### 5.4 결정 3 — 쓰기 전용 레지스터와 regcache_sync

6.18 `drivers/base/regmap/regmap.c`의 `regmap_volatile()`은 첫 줄에서 "readable이 아닌 레지스터는 volatile이 아니다"라고 판단한다. 그래서 readable 표에서 뺀 쓰기 전용 CMD 레지스터는 volatile 표에 넣어도 **캐시에 기록된다**. 그리고 `regcache_reg_needs_sync()`는 기본값이 없는 writeable 레지스터를 동기화 대상으로 본다. 결과: suspend/resume에서 `regcache_sync()`를 부르면 **캐시에 남은 CMD=0xB6(soft reset)이 재생**된다. resume 할 때마다 칩이 리셋되는 버그다.

```c
bool regmap_volatile(struct regmap *map, unsigned int reg)
{
	if (!map->format.format_write && !regmap_readable(map, reg))
		return false;
	...
```

해결책 세 가지: (a) 명령 쓰기를 `regcache_cache_bypass(map, true)`로 감싼다(TX31이 쓰는 방법), (b) CMD를 readable + volatile로 선언한다(읽으면 0이 나오는 칩이라면 무해), (c) `regcache_drop_region()`으로 지운다. 이 노트에서는 suspend 경로까지 실행하지는 않았다 — 위 판단은 6.18 소스를 읽고 내린 것이다.

### 5.5 결정 4 — 페이지(bank) 레지스터는 `ranges`로

```svg
<svg viewBox="0 0 640 280" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="20" y="20" font-size="13">드라이버가 보는 주소 (virtual)</text>
  <text x="400" y="20" font-size="13">버스에 나가는 주소</text>
  <rect x="20" y="35" width="200" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="30" y="60" font-size="12">0x00-0x07, 0x7E, 0x7F</text>
  <rect x="20" y="95" width="200" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="115" font-size="12">0x100-0x10F (page 0)</text>
  <text x="30" y="133" font-size="12">THR_HI, THR_LO, HYST</text>
  <rect x="20" y="165" width="200" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="185" font-size="12">0x110-0x11F (page 1)</text>
  <text x="30" y="203" font-size="12">CAL_OFF</text>
  <rect x="400" y="35" width="220" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="410" y="60" font-size="12">같은 주소 그대로</text>
  <rect x="400" y="100" width="220" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="125" font-size="12">① 0x7F PAGE_SEL ← page</text>
  <rect x="400" y="165" width="220" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="190" font-size="12">② window 0x10 + offset</text>
  <line x1="220" y1="55" x2="398" y2="55" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="220" y1="120" x2="398" y2="120" stroke="#4a7bd0" marker-end="url(#ah4)"/>
  <line x1="220" y1="190" x2="398" y2="120" stroke="#e08a3c" marker-end="url(#ah4)"/>
  <line x1="510" y1="140" x2="510" y2="163" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="20" y="245" font-size="12">regmap_range_cfg: range 0x100-0x11F, selector 0x7F mask 0x01,</text>
  <text x="20" y="263" font-size="12">window_start 0x10, window_len 0x10 → page = (reg - 0x100) / 0x10</text>
</svg>
```
그림 5 — 페이지 레지스터를 regmap `ranges`로 선언하면 드라이버는 가상 주소 하나로 접근하고, core가 PAGE_SEL 쓰기와 window 접근 두 단계로 바꿔 준다. PAGE_SEL 값도 캐시되므로 같은 페이지를 연속으로 쓰면 선택 쓰기가 생략된다.

규칙: 가상 범위(0x100~)는 실제 레지스터 주소와 겹치면 안 된다. window(0x10~0x1F) 자체는 readable 표에서 빼서 드라이버나 debugfs가 페이지 없이 직접 건드리지 못하게 한다. `max_register`는 가상 범위의 끝(0x11F)이다.

bare-metal에서 Don이 했을 `select_page(1); read(0x10); select_page(0);` 래퍼 함수를 표 하나로 대체하는 것이다. 다른 컨텍스트가 끼어들어도 regmap 락 안에서 선택과 접근이 원자적으로 묶인다는 것이 손으로 짠 래퍼보다 나은 점이다.

---

## 6. 실습 2 — TX31 드라이버를 처음부터 쓴다

§1.3 데이터시트를 그대로 옮긴다. 파일은 `p3_tx31_regs.h` + `p3_tx31.c`(여섯 조각, 순서대로 이어 붙이면 한 파일). 빌드 대상은 `linux-build-dom`.

### 6.1 예제 9(a) — 레지스터 헤더: 표 4-1을 그대로

```c
/* p3_tx31_regs.h — 가상의 TX31 온도 센서 레지스터 맵 (데이터시트 표 4-1 그대로) */
#ifndef P3_TX31_REGS_H
#define P3_TX31_REGS_H
#include <linux/bits.h>

#define TX31_CHIP_ID		0x00	/* R   0xA3 */
#define TX31_CHIP_ID_VAL	0xA3
#define TX31_REV_ID		0x01	/* R   0x01=Rev A, 0x02=Rev B */
#define TX31_STATUS		0x02	/* R   bit0 DRDY, bit1 BUSY (HW가 바꿈) */
#define TX31_STATUS_DRDY	BIT(0)
#define TX31_CTRL		0x03	/* RW  0x00: bit0 EN, [3:1] ODR */
#define TX31_CTRL_EN		BIT(0)
#define TX31_INT_CFG		0x04	/* RW  0x00 (Rev A 에라타: 0x01) */
#define TX31_INT_DRDY_IE	BIT(0)
#define TX31_INT_SRC		0x05	/* R   clear-on-read */
#define TX31_TEMP_L		0x06	/* R   TEMP_L 읽으면 TEMP_H 래치 */
#define TX31_TEMP_H		0x07
#define TX31_CMD		0x7E	/* W   0xB6 = soft reset */
#define TX31_CMD_RESET		0xB6
#define TX31_PAGE_SEL		0x7F	/* RW  bit0 = page */
#define TX31_WIN_START		0x10	/* 창: 0x10..0x1F (페이지별) */
#define TX31_WIN_LEN		0x10

/* 가상(virtual) 주소: 0x100 + page*0x10 + (창 offset) */
#define TX31_VIRT_BASE		0x100
#define TX31_THR_HI		0x100	/* page0 0x10 RW 0x50 (80 C) */
#define TX31_THR_LO		0x101	/* page0 0x11 RW 0x00 */
#define TX31_HYST		0x102	/* page0 0x12 RW 0x02 */
#define TX31_CAL_OFF		0x110	/* page1 0x10 R  OTP, signed */
#define TX31_MAX_REG		0x11F

#define TX31_T_PU_US		2000	/* 전원 인가 후 첫 접근까지 */
#define TX31_T_RST_US		1000	/* soft reset 완료 시간 */
#endif
```

### 6.2 예제 9(b) — 접근 표: R/W 열과 "설명" 열을 표로

무엇을 확인하나: 표 4-1의 R/W 열 → `tx31_rd`/`tx31_wr`, 설명 열의 "HW가 바꿈" → `tx31_vol`, "읽으면 지워짐" → `tx31_prec`. CMD(0x7E)는 쓰기 전용이라 readable에 없다. window 0x10~0x1F도 없다(가상 주소로만 접근).

```c
// SPDX-License-Identifier: GPL-2.0
/* p3_tx31.c (1/6) — 데이터시트 표 → regmap 접근 표 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include "p3_tx31_regs.h"

static bool ignore_errata;
module_param(ignore_errata, bool, 0444);
MODULE_PARM_DESC(ignore_errata, "lab only: skip Rev A INT_CFG fix");

static const struct regmap_range tx31_rd[] = {
	regmap_reg_range(TX31_CHIP_ID, TX31_TEMP_H),
	regmap_reg_range(TX31_PAGE_SEL, TX31_PAGE_SEL),
	regmap_reg_range(TX31_THR_HI, TX31_HYST),
	regmap_reg_range(TX31_CAL_OFF, TX31_CAL_OFF),
};
static const struct regmap_range tx31_wr[] = {
	regmap_reg_range(TX31_CTRL, TX31_INT_CFG),
	regmap_reg_range(TX31_CMD, TX31_PAGE_SEL),
	regmap_reg_range(TX31_THR_HI, TX31_HYST),
};
static const struct regmap_range tx31_vol[] = {	/* HW가 바꾸는 것 */
	regmap_reg_range(TX31_STATUS, TX31_STATUS),
	regmap_reg_range(TX31_INT_SRC, TX31_TEMP_H),
};
static const struct regmap_range tx31_prec[] = {	/* 읽으면 상태가 변함 */
	regmap_reg_range(TX31_INT_SRC, TX31_INT_SRC),
};
static const struct regmap_access_table tx31_rd_t = {
	.yes_ranges = tx31_rd, .n_yes_ranges = ARRAY_SIZE(tx31_rd) };
static const struct regmap_access_table tx31_wr_t = {
	.yes_ranges = tx31_wr, .n_yes_ranges = ARRAY_SIZE(tx31_wr) };
static const struct regmap_access_table tx31_vol_t = {
	.yes_ranges = tx31_vol, .n_yes_ranges = ARRAY_SIZE(tx31_vol) };
static const struct regmap_access_table tx31_prec_t = {
	.yes_ranges = tx31_prec, .n_yes_ranges = ARRAY_SIZE(tx31_prec) };
```

`ignore_errata`는 §6.6 실험용 스위치다. 실제 드라이버에는 넣지 않는다.

### 6.3 예제 9(c) — Reset 열 → reg_defaults, 표 4-2 → ranges, 표 4-3 → 필드

```c
/* p3_tx31.c (2/6) — 리셋값, 페이지, regmap_config, 필드 */
static const struct reg_default tx31_defaults[] = {	/* 표 4-1 "Reset" 열 */
	{ TX31_CTRL, 0x00 }, { TX31_INT_CFG, 0x00 }, { TX31_PAGE_SEL, 0x00 },
	{ TX31_THR_HI, 0x50 }, { TX31_THR_LO, 0x00 }, { TX31_HYST, 0x02 },
};

static const struct regmap_range_cfg tx31_pages[] = {
	{
		.name = "tx31-pages",
		.range_min = TX31_VIRT_BASE, .range_max = TX31_MAX_REG,
		.selector_reg = TX31_PAGE_SEL, .selector_mask = 0x01,
		.selector_shift = 0,
		.window_start = TX31_WIN_START, .window_len = TX31_WIN_LEN,
	},
};

static const struct regmap_config tx31_regmap_cfg = {
	.reg_bits = 8, .val_bits = 8,
	.max_register = TX31_MAX_REG,
	.rd_table = &tx31_rd_t, .wr_table = &tx31_wr_t,
	.volatile_table = &tx31_vol_t, .precious_table = &tx31_prec_t,
	.reg_defaults = tx31_defaults,
	.num_reg_defaults = ARRAY_SIZE(tx31_defaults),
	.cache_type = REGCACHE_MAPLE,
	.ranges = tx31_pages, .num_ranges = ARRAY_SIZE(tx31_pages),
};

static const struct reg_field tx31_odr_f = REG_FIELD(TX31_CTRL, 1, 3);
static const int tx31_odr_hz[] = { 1, 4, 16, 64 };	/* 표 4-3 */

struct tx31 {
	struct regmap *map;
	struct regmap_field *odr;
	int cal_off;		/* page1 OTP, raw 단위 (1/128 C) */
};

static const struct iio_chan_spec tx31_chan[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
};
```

포인트: 에라타 E1을 알면서도 `INT_CFG` 기본값을 0x00으로 둔 것은 의도다 — Rev B 기준으로 표를 쓰고 Rev A는 probe에서 덮는다. IIO의 온도 단위는 milli-°C이고 `(raw + offset) × scale`로 계산되므로(I2), OFFSET에 공장 보정값을, SCALE에 1000/128을 준다.

### 6.4 예제 9(d)(e) — 읽기: DRDY 폴링과 burst 읽기 (4.5절), ODR 필드

```c
/* p3_tx31.c (3/6) — IIO read_raw */
static int tx31_read_temp(struct tx31 *t, int *val)
{
	unsigned int st;
	__le16 raw;
	int ret;

	/* 표 4-1 STATUS: DRDY=1 될 때까지 (ODR 1Hz면 최대 1 s) */
	ret = regmap_read_poll_timeout(t->map, TX31_STATUS, st,
				       st & TX31_STATUS_DRDY, 1000, 1100000);
	if (ret)
		return ret;
	/* 4.5절: TEMP_L→TEMP_H 한 번의 burst로 읽어야 래치가 맞다 */
	ret = regmap_bulk_read(t->map, TX31_TEMP_L, &raw, sizeof(raw));
	if (ret)
		return ret;
	*val = sign_extend32(le16_to_cpu(raw), 15);
	return 0;
}

static int tx31_read_raw(struct iio_dev *indio, struct iio_chan_spec const *ch,
			 int *val, int *val2, long mask)
{
	struct tx31 *t = iio_priv(indio);
	unsigned int odr;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = tx31_read_temp(t, val);
		return ret ? ret : IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		*val = t->cal_off;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:	/* 1 LSB = 1/128 C = 1000/128 mC */
		*val = 1000;
		*val2 = 128;
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = regmap_field_read(t->odr, &odr);
		if (ret)
			return ret;
		*val = tx31_odr_hz[odr & 3];
		return IIO_VAL_INT;
	}
	return -EINVAL;
}
```

`regmap_read_poll_timeout(map, reg, val, cond, sleep_us, timeout_us)`의 timeout은 **가장 느린 ODR의 한 주기 + 여유**로 정한다 — 데이터시트 ODR 표가 숫자의 출처다.

```c
/* p3_tx31.c (4/6) — write_raw: sampling_frequency → regmap_field */
static int tx31_write_raw(struct iio_dev *indio, struct iio_chan_spec const *ch,
			  int val, int val2, long mask)
{
	struct tx31 *t = iio_priv(indio);

	if (mask != IIO_CHAN_INFO_SAMP_FREQ)
		return -EINVAL;
	for (int i = 0; i < ARRAY_SIZE(tx31_odr_hz); i++)
		if (tx31_odr_hz[i] == val)
			return regmap_field_write(t->odr, i);
	return -EINVAL;
}

static const struct iio_info tx31_info = {
	.read_raw = tx31_read_raw, .write_raw = tx31_write_raw };
```

### 6.5 예제 9(f)(g) — probe: 6장 순서 그대로, 그리고 매칭 테이블

무엇을 확인하나: 6.1(t_PU) → ID 확인 → 6.2(soft reset, 캐시 우회) → t_RST → REV_ID로 에라타 분기 → 페이지 1 보정값 → 6.3(EN). 각 줄 옆 주석이 데이터시트 절 번호다.

```c
/* p3_tx31.c (5/6) — probe: 데이터시트 6장 "Power-up sequence" 순서 그대로 */
static int tx31_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct iio_dev *indio;
	unsigned int id, rev, cal;
	struct tx31 *t;
	int ret;

	indio = devm_iio_device_alloc(dev, sizeof(*t));
	if (!indio)
		return -ENOMEM;
	t = iio_priv(indio);
	t->map = devm_regmap_init_i2c(client, &tx31_regmap_cfg);
	if (IS_ERR(t->map))
		return dev_err_probe(dev, PTR_ERR(t->map), "regmap init\n");
	t->odr = devm_regmap_field_alloc(dev, t->map, tx31_odr_f);
	if (IS_ERR(t->odr))
		return PTR_ERR(t->odr);

	fsleep(TX31_T_PU_US);					/* t_PU */
	ret = regmap_read(t->map, TX31_CHIP_ID, &id);
	if (ret)
		return dev_err_probe(dev, ret, "no ACK at 0x%02x\n", client->addr);
	if (id != TX31_CHIP_ID_VAL)
		return dev_err_probe(dev, -ENODEV, "CHIP_ID 0x%02x != 0x%02x\n",
				     id, TX31_CHIP_ID_VAL);
	/* 6.2: CMD는 write-only → 캐시에 남으면 regcache_sync가 리셋을 재생 */
	regcache_cache_bypass(t->map, true);
	ret = regmap_write(t->map, TX31_CMD, TX31_CMD_RESET);
	regcache_cache_bypass(t->map, false);
	if (ret)
		return ret;
	fsleep(TX31_T_RST_US);					/* t_RST */

	ret = regmap_read(t->map, TX31_REV_ID, &rev);
	if (ret)
		return ret;
	if (rev == 0x01 && !ignore_errata)	/* 에라타 E1: INT_CFG 리셋값 0x01 */
		ret = regmap_write_bits(t->map, TX31_INT_CFG, TX31_INT_DRDY_IE, 0);
	else
		ret = regmap_update_bits(t->map, TX31_INT_CFG, TX31_INT_DRDY_IE, 0);
	if (ret)
		return ret;
	ret = regmap_read(t->map, TX31_CAL_OFF, &cal);		/* page 1 */
	if (ret)
		return ret;
	t->cal_off = (s8)cal;
	ret = regmap_update_bits(t->map, TX31_CTRL, TX31_CTRL_EN, TX31_CTRL_EN);
	if (ret)
		return ret;

	dev_info(dev, "TX31 rev %u, cal_off %d\n", rev, t->cal_off);
	indio->name = "p3_tx31";
	indio->info = &tx31_info;
	indio->channels = tx31_chan;
	indio->num_channels = ARRAY_SIZE(tx31_chan);
	indio->modes = INDIO_DIRECT_MODE;
	return devm_iio_device_register(dev, indio);
}
```

```c
/* p3_tx31.c (6/6) — 매칭 테이블과 등록 */
static const struct of_device_id tx31_of_match[] = {
	{ .compatible = "p3lab,tx31" },		/* 가상의 vendor prefix */
	{ }
};
MODULE_DEVICE_TABLE(of, tx31_of_match);

static const struct i2c_device_id tx31_id[] = {
	{ "p3_tx31" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tx31_id);

static struct i2c_driver tx31_driver = {
	.driver = { .name = "p3_tx31", .of_match_table = tx31_of_match },
	.probe = tx31_probe,
	.id_table = tx31_id,
};
module_i2c_driver(tx31_driver);
MODULE_DESCRIPTION("P3 lab: fictional TX31 temperature sensor (regmap)");
MODULE_LICENSE("GPL");
```

`regmap_write_bits()`와 `regmap_update_bits()`의 차이가 에라타 대응의 핵심이다. 둘 다 `regmap_update_bits_base()`를 부르지만 write_bits는 `force=true`라서 **값이 같아 보여도 버스에 쓴다**.

### 6.6 예제 10 — 빌드: W=1 경고 0, checkpatch

```make
obj-m += p3_tx31.o
```

```sh
cd /var/tmp/P3/mod && touch p3_tx31.c
make -C /var/lab/out/linux-build-dom M=$PWD W=1 -j2 modules 2>&1 | grep -vE "^make|Entering|Leaving"
/var/lab/src/linux-6.18.55/scripts/checkpatch.pl --no-tree --terse -f p3_tx31.c | tail -3
```

```text
  CC [M]  p3_tx31.o
  MODPOST Module.symvers
  LD [M]  p3_tx31.ko
p3_tx31.c:208: WARNING: It's generally not useful to have the filename in the file
total: 0 errors, 6 warnings, 228 lines checked
```

출력에서 볼 것: `W=1`(추가 경고)에서도 경고 0. checkpatch의 경고 6개는 전부 "주석에 파일명을 쓰지 마라"(노트용으로 조각마다 `p3_tx31.c (n/6)`을 붙였기 때문)라서 upstream 전에 지우면 된다. 리뷰어에게 보내기 전에 W=1 + checkpatch를 돌리는 것은 기본 예의다(P7).

### 6.7 예제 11 — 칩 없음 → ID 실패, 그리고 i2c-stub bank의 함정

무엇을 확인하나: 먼저 빈 칩(전부 0)에 붙여서 ID 실패를 본다. 그 다음 "전원 투입 직후" 값을 심는 스크립트로 Rev A 칩을 만든다.

```sh
insmod /mnt/p3/p3_tx31.ko ignore_errata=1; dmesg -C
echo p3_tx31 0x48 > /sys/bus/i2c/devices/i2c-0/new_device; sleep 1; dmesg
```

```text
[  474.570721][  T350] p3_tx31 0-0048: error -ENODEV: CHIP_ID 0x00 != 0xa3
[  474.570721] p3_tx31 0-0048: error -ENODEV: CHIP_ID 0x00 != 0xa3
[  474.586116] i2c i2c-0: new_device: Instantiated device p3_tx31 at 0x48
```

`dev_err_probe()`는 `error -ENODEV:` 접두어를 자동으로 붙인다. 이제 전원 투입 이미지를 심는다(i2c-tools 패키지의 `i2c-stub-from-dump` 스크립트가 실제 칩의 `i2cdump` 결과로 같은 일을 해 준다 — 실보드에서 덤프를 떠 와 책상에서 드라이버를 돌리는 데 쓴다).

```sh
#!/bin/sh
# tx31_poweron.sh — i2c-stub(0x48)에 "TX31 Rev A 전원 투입 직후" 값을 심는다 (데이터시트 Reset 열)
B=0; A=0x48
w() { i2cset -f -y $B $A "$1" "$2"; }
w 0x00 0xa3      # CHIP_ID
w 0x01 0x01      # REV_ID = Rev A
w 0x02 0x01      # STATUS: DRDY=1 (stub는 변환을 안 하니 고정)
w 0x04 0x01      # INT_CFG: 에라타 E1 — Rev A는 0x01로 깨어난다
w 0x06 0xc0      # TEMP_L  } 0x0CC0 = 3264 → 25.5 C
w 0x07 0x0c      # TEMP_H  }
w 0x10 0x50      # page0 THR_HI = 80 C
w 0x12 0x02      # page0 HYST = 2 C
w 0x7f 0x01      # page 1 선택 → 0x10..0x1f 는 이제 bank 1
w 0x10 0xc0      # page1 CAL_OFF = -64 (= -0.5 C)
w 0x7f 0x00      # page 0 으로 복귀
```

```sh
sh /mnt/p3/tx31_poweron.sh; i2cdump -f -y -r 0x00-0x1f 0 0x48 b
echo "module i2c_stub +p" > /sys/kernel/debug/dynamic_debug/control; dmesg -C
echo 0-0048 > /sys/bus/i2c/drivers/p3_tx31/bind; sleep 1; dmesg | sed "s/i2c i2c-0: //"
```

```text
sh: 0: getcwd() failed: No such file or directory
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f    0123456789abcdef
00: a3 01 01 00 01 00 c0 0c 00 00 00 00 00 00 00 00    ???.?.??........
10: 50 00 02 00 00 00 00 00 00 00 00 00 00 00 00 00    P.?.............
[  490.175411] i2c block data - addr 0x48, read  1 bytes at 0x00.
[  490.175768] i2c block data - addr 0x48, wrote 1 bytes at 0x7e.
[  490.177493] i2c block data - addr 0x48, read  1 bytes at 0x01.
[  490.177867] i2c block data - addr 0x48, wrote 1 bytes at 0x7f.
[  490.177921] i2c block data - addr 0x48, read  1 bytes at 0x10.
[  490.177966] i2c block data - addr 0x48, wrote 1 bytes at 0x03.
[  490.177999] p3_tx31 0-0048: TX31 rev 1, cal_off 80
```

출력에서 볼 것: (첫 줄 getcwd 경고는 셸의 현재 디렉터리가 앞 실험에서 지워진 sysfs 디렉터리였기 때문이고 무해하다.) probe는 성공했는데 **cal_off가 80(=0x50, page 0의 THR_HI)** 이다. 드라이버는 PAGE_SEL에 1을 썼다(`wrote 1 bytes at 0x7f`). 그런데 i2c-stub 소스를 보면 bank 전환은 **SMBus byte data 쓰기**에서만 처리하고(`case I2C_SMBUS_BYTE_DATA` 안의 "Set the bank as needed"), I2C block 쓰기 경로에는 "We ignore banks here"라고 적혀 있다. regmap-i2c가 I2C block 백엔드를 골랐기 때문에 페이지가 안 바뀐 것이다. **에뮬레이터의 한계가 드라이버 버그처럼 보인 경우**다 — 실보드에서도 "로직 분석기 디코더가 틀렸다"는 류의 일이 생긴다. 그래서 숫자가 이상하면 버스 아래층부터 의심한다.

해결: i2c-stub의 `functionality` 인자로 I2C block을 끄면(문서의 예 `0x1f0000` = quick, byte, byte data만) regmap-i2c가 SMBus byte data 백엔드를 고른다.

```sh
echo 0x48 > /sys/bus/i2c/devices/i2c-0/delete_device; echo 0x6a > /sys/bus/i2c/devices/i2c-0/delete_device
rmmod st_lsm6dsx_i2c st_lsm6dsx p3_tx31 i2c-stub
modprobe i2c-stub chip_addr=0x48 bank_reg=0x7f bank_mask=0x01 bank_start=0x10 bank_end=0x1f functionality=0x1f0000
i2cdetect -F 0 | grep -i "block\|byte"
sh /mnt/p3/tx31_poweron.sh; insmod /mnt/p3/p3_tx31.ko ignore_errata=1
echo "module i2c_stub +p" > /sys/kernel/debug/dynamic_debug/control; dmesg -C
echo p3_tx31 0x48 > /sys/bus/i2c/devices/i2c-0/new_device; sleep 1; dmesg | sed "s/i2c i2c-0: //"
```

```text
SMBus Send Byte                  yes
SMBus Receive Byte               yes
SMBus Write Byte                 yes
SMBus Read Byte                  yes
SMBus Block Write                no
SMBus Block Read                 no
SMBus Block Process Call         no
I2C Block Write                  no
I2C Block Read                   no
[  509.129995] smbus byte data - addr 0x48, read  0xa3 at 0x00.
[  509.130369] smbus byte data - addr 0x48, wrote 0xb6 at 0x7e.
[  509.132101] smbus byte data - addr 0x48, read  0x01 at 0x01.
[  509.132280] smbus byte data - addr 0x48, wrote 0x01 at 0x7f.
[  509.132320] switching to bank 1.
[  509.132370] smbus byte data - addr 0x48, read  0xc0 at 0x10.
[  509.132416] smbus byte data - addr 0x48, wrote 0x01 at 0x03.
[  509.132449] p3_tx31 0-0048: TX31 rev 1, cal_off -64
[  509.138024] new_device: Instantiated device p3_tx31 at 0x48
```

출력에서 볼 것 (probe의 버스 트랜잭션 7개를 데이터시트와 맞춰 본다):

| 트랜잭션 | 데이터시트 | 코드 |
|---|---|---|
| read 0xa3 at 0x00 | CHIP_ID | `regmap_read(CHIP_ID)` |
| wrote 0xb6 at 0x7e | 6.2 soft reset | bypass 안의 `regmap_write(CMD)` |
| read 0x01 at 0x01 | REV_ID = Rev A | `regmap_read(REV_ID)` |
| (없음) | 에라타 E1 | `ignore_errata=1` → `update_bits`가 캐시(0x00)를 보고 **생략** |
| wrote 0x01 at 0x7f + bank 1 | 표 4-2 page 1 | regmap range가 자동으로 PAGE_SEL 쓰기 |
| read 0xc0 at 0x10 | CAL_OFF = -64 | `regmap_read(0x110)` |
| wrote 0x01 at 0x03 | 6.3 EN=1 | `update_bits(CTRL)` — 캐시 덕에 read 없이 write만 |

마지막 줄이 regmap 캐시의 이득이고, 넷째 줄(없음)이 같은 캐시의 함정이다. 그리고 probe가 끝난 뒤 **PAGE_SEL은 1인 채로 남는다** — regmap은 페이지를 되돌리지 않는다(필요할 때만 바꾼다). LSM6DSO 드라이버가 probe 첫 동작으로 페이지를 0으로 돌린 이유가 이것이다(§4.5): 이전 부팅에서 페이지가 1로 남은 채 리셋 없이 재부팅하면 일반 레지스터가 가려질 수 있다. TX31은 soft reset이 PAGE_SEL을 0으로 돌리고(6.2), 캐시 기본값도 0이라서 맞아떨어진다.

### 6.8 예제 12 — debugfs regmap: name, range, access

무엇을 확인하나: `/sys/kernel/debug/regmap/<장치>/`의 파일로 내가 선언한 표가 의도대로 들어갔는지 본다(G8, H2 §3.7).

```sh
cd /sys/kernel/debug/regmap/0-0048 && ls && cat name range access
```

```text
access  cache_bypass  cache_dirty  cache_only  name  range  registers  tx31-pages
p3_tx31
0-4
6-7
7f-7f
100-102
110-110
000: y n n n
001: y n n n
002: y n y n
003: y y n n
004: y y n n
005: y n y y
006: y n y n
007: y n y n
07e: n y n n
07f: y y n n
100: y y n n
101: y y n n
102: y y n n
110: y n n n
```

출력에서 볼 것: `access`의 네 열은 readable / writeable / volatile / precious다. **이 표를 데이터시트 표 4-1과 한 줄씩 대조하는 것이 regmap 드라이버 리뷰의 첫 단계**다. 005(INT_SRC)가 `y n y y`(precious) → `range`에서 빠졌다(0-4, 6-7): debugfs 덤프가 읽어서 인터럽트를 지우는 사고를 막는다. 07e(CMD)는 `n y` — 쓰기 전용. 페이지 범위는 별도 파일 `tx31-pages`로 생겼다.

### 6.9 예제 13 — 에라타 버그를 눈으로: 캐시 vs HW

무엇을 확인하나: `ignore_errata=1`로 붙인 상태에서 debugfs `registers`(캐시를 통해 보는 값)와 `i2cget`(regmap을 우회해 HW를 직접 읽음, 그림 4 왼쪽)을 비교한다. 덤프하는 동안 실제로 버스에 나간 읽기도 본다.

```sh
cd /sys/kernel/debug/regmap/0-0048; dmesg -C; cat registers
echo ---bus; dmesg | sed "s/.*i2c i2c-0: //"
echo ---HW; i2cget -f -y 0 0x48 0x04
```

```text
000: a3
001: 01
002: 01
003: 01
004: 00
006: c0
007: 0c
07f: 01
100: 50
101: 00
102: 02
110: c0
---bus
smbus byte data - addr 0x48, read  0x01 at 0x02.
smbus byte data - addr 0x48, read  0xc0 at 0x06.
smbus byte data - addr 0x48, read  0x0c at 0x07.
---HW
0x01
```

출력에서 볼 것:

- **`004: 00`(캐시) vs `0x01`(HW)** — 드라이버는 DRDY 인터럽트를 껐다고 믿지만 칩은 켜져 있다. 실보드라면 INT 핀이 변환마다 내려가고, IRQ 핸들러가 없으니 "spurious IRQ"나 "nobody cared"로 나타난다. **debugfs만 봐서는 절대 못 찾는 버그**다. 캐시 뒤를 보려면 i2cget(또는 `cache_bypass`)이 필요하다.
- 덤프할 때 버스에 나간 것은 volatile 레지스터(STATUS, TEMP_L, TEMP_H) 세 개뿐이다. 나머지 9개는 캐시에서 나왔다.

이제 에라타 수정을 켠다(모듈 인자 없이 다시 로드, 칩도 전원 투입 상태로 되돌림).

```sh
cd /; echo 0x48 > /sys/bus/i2c/devices/i2c-0/delete_device; rmmod p3_tx31
sh /mnt/p3/tx31_poweron.sh; insmod /mnt/p3/p3_tx31.ko; dmesg -C
echo p3_tx31 0x48 > /sys/bus/i2c/devices/i2c-0/new_device; sleep 1; dmesg | sed "s/.*i2c i2c-0: //"
echo ---cache vs HW; grep ^004 /sys/kernel/debug/regmap/0-0048/registers; i2cget -f -y 0 0x48 0x04
```

```text
smbus byte data - addr 0x48, read  0xa3 at 0x00.
smbus byte data - addr 0x48, wrote 0xb6 at 0x7e.
smbus byte data - addr 0x48, read  0x01 at 0x01.
smbus byte data - addr 0x48, wrote 0x00 at 0x04.
smbus byte data - addr 0x48, wrote 0x01 at 0x7f.
switching to bank 1.
smbus byte data - addr 0x48, read  0xc0 at 0x10.
smbus byte data - addr 0x48, wrote 0x01 at 0x03.
[  628.340778] p3_tx31 0-0048: TX31 rev 1, cal_off -64
new_device: Instantiated device p3_tx31 at 0x48
---cache vs HW
004: 00
0x00
```

출력에서 볼 것: `wrote 0x00 at 0x04` 한 줄이 늘었다. `regmap_write_bits()`가 캐시를 무시하고 썼다. 캐시와 HW가 이제 일치한다. **에라타 한 줄 → 코드 한 줄 → 버스 트랜잭션 한 줄**이 정확히 대응한다.

### 6.10 예제 14 — IIO 값과 volatile: 온도가 바뀌면 바로 보인다

```sh
cd /sys/bus/iio/devices/iio:device0
cat name in_temp_raw in_temp_offset in_temp_scale sampling_frequency
echo $(( ( $(cat in_temp_raw) + $(cat in_temp_offset) ) * 78125 / 10000 )) mC
i2cset -f -y 0 0x48 0x06 0x00; i2cset -f -y 0 0x48 0x07 0x0d; cat in_temp_raw
```

```text
p3_tx31
3264
-64
7.812500000
1
25000 mC
3328
```

출력에서 볼 것: (3264 + (-64)) × 7.8125 = 25000 m°C = 25.0 °C. 공장 보정 -0.5 °C가 반영됐다(raw만 보면 25.5 °C). TEMP 레지스터를 바깥에서 0x0D00(=3328)으로 바꾸자 다음 읽기에 바로 보인다 — volatile이라 매번 버스에서 읽기 때문이다. 만약 TEMP를 volatile 표에서 빠뜨렸다면 처음 읽은 3264가 영원히 나온다(H2 §7.2의 버그 1).

### 6.11 예제 15 — 캐시가 HW 리셋을 가린다 (brown-out 시뮬레이션)

무엇을 확인하나: 칩이 전원 글리치로 혼자 리셋됐다고 치고(CTRL을 바깥에서 0으로), 캐시·HW·bypass 값을 비교한 뒤 드라이버가 ODR을 바꾸면 무슨 일이 생기는지 본다.

```sh
D=/sys/kernel/debug/regmap/0-0048; I=/sys/bus/iio/devices/iio:device0
i2cset -f -y 0 0x48 0x03 0x00
echo "[HW] CTRL=$(i2cget -f -y 0 0x48 0x03)"
echo "[cache] $(grep ^003 $D/registers)  dirty=$(cat $D/cache_dirty)"
echo Y > $D/cache_bypass; echo "[bypass] $(grep ^003 $D/registers)"; echo N > $D/cache_bypass
dmesg -C; echo 16 > $I/sampling_frequency; dmesg | sed "s/.*i2c i2c-0: //"
echo "[HW] CTRL=$(i2cget -f -y 0 0x48 0x03)"; cat $I/sampling_frequency
```

```text
[HW] CTRL=0x00
[cache] 003: 01  dirty=N
[bypass] 003: 00
smbus byte data - addr 0x48, wrote 0x05 at 0x03.
[HW] CTRL=0x05
16
```

출력에서 볼 것:

- 칩은 꺼졌는데(0x00) 캐시는 EN=1(0x01)이라고 믿는다. `cache_bypass=Y`로 덤프하면 진실(0x00)이 보인다 — 실보드 bring-up에서 "debugfs 값은 맞는데 칩이 안 움직인다"면 가장 먼저 해 볼 일이다.
- ODR=16 Hz(필드값 2)를 쓰자 regmap_field는 캐시 0x01을 기준으로 `0x01 | (2 << 1) = 0x05`를 **읽지 않고** 썼다. 우연히 EN도 다시 켜졌다. 반대로 칩이 "기본값과 다른" 상태로 리셋됐다면 캐시 기반 RMW가 엉뚱한 비트를 덮었을 것이다.
- 교훈: 칩이 혼자 리셋될 수 있는 시스템(전원 도메인이 따로 꺼지는 센서, 워치독)에서는 리셋을 감지(예: CTRL 재확인, 리셋 인터럽트)했을 때 `regcache_mark_dirty()` + `regcache_sync()`로 캐시를 HW에 다시 밀어 넣어야 한다. 이 경로는 suspend/resume과 똑같다(H2 §9, N6).

### 6.12 예제 16 — 페이지 접근을 버스에서 보기, 그리고 debugfs range 파일의 버그

무엇을 확인하나: `cache_bypass=Y`로 전체를 덤프하면 모든 레지스터가 버스에서 읽힌다. 가상 주소 0x100~0x110이 어떻게 PAGE_SEL 전환으로 바뀌는지 본다. 그 다음 range 파일(`tx31-pages`)을 읽는다.

```sh
D=/sys/kernel/debug/regmap/0-0048; echo Y > $D/cache_bypass; dmesg -C
cat $D/registers > /dev/null; dmesg | sed "s/.*i2c i2c-0: //"; echo N > $D/cache_bypass
echo ---range file; dd if=$D/tx31-pages bs=4k count=1 2>/dev/null
echo ---second read; dd if=$D/tx31-pages bs=16 count=4 2>/dev/null
```

```text
smbus byte data - addr 0x48, read  0xa3 at 0x00.
smbus byte data - addr 0x48, read  0x01 at 0x01.
smbus byte data - addr 0x48, read  0x01 at 0x02.
smbus byte data - addr 0x48, read  0x05 at 0x03.
smbus byte data - addr 0x48, read  0x00 at 0x04.
smbus byte data - addr 0x48, read  0x00 at 0x06.
smbus byte data - addr 0x48, read  0x0d at 0x07.
smbus byte data - addr 0x48, read  0x01 at 0x7f.
smbus byte data - addr 0x48, read  0x01 at 0x7f.
smbus byte data - addr 0x48, wrote 0x00 at 0x7f.
switching to bank 0.
smbus byte data - addr 0x48, read  0x50 at 0x10.
smbus byte data - addr 0x48, read  0x00 at 0x7f.
smbus byte data - addr 0x48, read  0x00 at 0x11.
smbus byte data - addr 0x48, read  0x00 at 0x7f.
smbus byte data - addr 0x48, read  0x02 at 0x12.
smbus byte data - addr 0x48, read  0x00 at 0x7f.
smbus byte data - addr 0x48, wrote 0x01 at 0x7f.
switching to bank 1.
smbus byte data - addr 0x48, read  0xc0 at 0x10.
---range file
000: 50
001: 00
002: 02
010: c0
---second read
000: 50
001: 00
000: 50
001: 00
000: 50
001: 00
000: 50
001: 00
```

출력에서 볼 것:

- 0x100(THR_HI)을 읽으려고 PAGE_SEL을 읽고(현재 1) → 0을 쓰고 → window 0x10을 읽는다. 0x101, 0x102는 PAGE_SEL을 읽어 보고 이미 0이라 **쓰기를 생략**한다(update_bits의 "같으면 안 씀"). 0x110에서 다시 1로 바꾼다. bypass 모드라 PAGE_SEL도 매번 HW에서 읽었다. 평소(캐시 모드)에는 PAGE_SEL 읽기도 캐시에서 나와 버스에는 쓰기만 보인다(§6.7 probe 로그).
- range 파일은 오프셋을 range 시작 기준(000 = 0x100, 010 = 0x110)으로 보여 준다.
- **"second read"가 이상하다**: 16바이트씩 읽으면 매번 처음(000)부터 다시 나온다. 즉 `read()`가 EOF를 영원히 돌려주지 않아서 **`cat tx31-pages`가 끝나지 않고 무한히 출력**한다(처음 실험에서 `cat`이 1.7 MB를 쏟아내서 Ctrl-C로 멈췄다). 6.18.55 `drivers/base/regmap/regmap-debugfs.c`를 보면 `regmap_read_debugfs()`가 `loff_t p = *ppos`로 시작하고, range 파일처럼 `from != 0`이면 `regmap_debugfs_get_dump_start()`가 위치(`*pos`)를 갱신하지 않고 바로 반환한다. 그러면 루프의 `if (p >= *ppos)`가 첫 레지스터부터 참이 되어 매 호출마다 처음부터 다시 쓴다. 메인 `registers` 파일(from = 0)은 오프셋 캐시 경로를 타서 정상이다. 이 노트는 upstream에 이미 수정이 있는지 확인하지 않았다 — 실무라면 lore에서 `regmap-debugfs.c` 패치를 검색해 보고(§2.6), 없으면 재현 절차와 함께 보고할 만한 버그다. 당장은 `dd bs=4k count=1`처럼 한 번만 읽어서 피한다.
- **bring-up 도구도 버그가 있다.** 에뮬레이터(i2c-stub bank), debugfs(range 파일) 둘 다 이 노트에서 실제로 걸렸다.

---

## 7. 반복 bring-up — i2c-tools와 커널 관찰 도구를 섞어 쓰기

| 질문 | 도구 | 이 노트에서 |
|---|---|---|
| 칩이 ACK 하나? | `i2cdetect -y <bus>` (드라이버가 잡은 주소는 `UU`) | §4.2 |
| 어댑터가 어떤 트랜잭션을 지원하나? | `i2cdetect -F <bus>` | §6.7 |
| 칩의 실제 값은? (캐시 우회) | `i2cget -f`, `i2cdump -f -r` | §6.9, §6.11 |
| 드라이버가 버스에 무엇을 냈나? | 어댑터 드라이버의 dynamic debug, i2c tracepoint(`events/i2c`), regmap tracepoint(`events/regmap`) | §4.4, §6.7 (H2 §10, M5) |
| probe가 왜 조용히 실패했나? | `drivers/base/dd.c` dynamic debug, `/sys/kernel/debug/devices_deferred` | §4.2 |
| 드라이버가 믿는 값은? | `/sys/kernel/debug/regmap/<dev>/registers` | §6.9 |
| 표가 의도대로인가? | debugfs `access`, `range` | §6.8 |
| 실보드 덤프로 책상에서 재현 | `i2cdump` → `i2c-stub-from-dump` | §6.7 설명 |

`-f`(force)는 드라이버가 잡고 있는 주소에도 접근하게 한다. 실보드에서 **clear-on-read 레지스터나 FIFO를 i2cget으로 읽으면 드라이버의 데이터를 훔친다**. 관찰이 대상을 바꾸는 것이다. 읽기 전에 그 레지스터가 precious인지 확인한다.

반복 사이클 한 바퀴의 실제 명령은 이렇다 (모듈 교체 → 재바인딩, 재부팅 없음):

```sh
echo 0-0048 > /sys/bus/i2c/drivers/p3_tx31/unbind      # 또는 delete_device
rmmod p3_tx31 && insmod /mnt/p3/p3_tx31.ko               # 새로 빌드한 .ko
sh /mnt/p3/tx31_poweron.sh                               # 칩을 "전원 투입 직후"로
echo p3_tx31 0x48 > /sys/bus/i2c/devices/i2c-0/new_device
```

이 노트의 실험은 전부 이 사이클로 돌렸고 게스트는 한 번만 부팅했다. 실보드에서 i2c-stub 자리에 진짜 칩이 들어가고, `tx31_poweron.sh` 자리에 "전원 레일 껐다 켜기(레귤레이터 sysfs나 GPIO)"가 들어간다.

---

## 8. DT binding YAML — 드라이버와 같이 쓴다

binding은 "이 compatible이면 DT에 이 속성들을 써라"라는 계약(ABI)이다. 문법과 검증 도구는 D7이 자세히 다뤘다. 여기서는 TX31 binding을 데이터시트에서 바로 뽑는다: 주소 enum은 ADDR 핀(0x48/0x49), `interrupts`는 7장, `vdd-supply`는 전원 핀.

### 8.1 예제 17 — binding 작성과 meta-schema 검사

```text
# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
%YAML 1.2
---
$id: http://devicetree.org/schemas/iio/temperature/p3lab,tx31.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: P3 Lab TX31 digital temperature sensor (fictional)

maintainers:
  - Don <don@example.com>

description:
  Fictional I2C temperature sensor used in the P3 study note. 16-bit
  temperature (1/128 C per LSB), paged threshold/calibration registers,
  active-low DRDY/ALERT interrupt output.

properties:
  compatible:
    const: p3lab,tx31

  reg:
    enum: [0x48, 0x49]

  interrupts:
    maxItems: 1

  vdd-supply: true

required:
  - compatible
  - reg
  - vdd-supply

additionalProperties: false

examples:
  - |
    #include <dt-bindings/interrupt-controller/irq.h>
    i2c {
        #address-cells = <1>;
        #size-cells = <0>;

        temperature-sensor@48 {
            compatible = "p3lab,tx31";
            reg = <0x48>;
            vdd-supply = <&vdd_3v3>;
            interrupt-parent = <&gpio1>;
            interrupts = <12 IRQ_TYPE_LEVEL_LOW>;
        };
    };
```

dtschema를 내 venv(`/var/tmp/P3/venv`, pip로 dtschema 2026.9 · yamllint 1.38.0)에 설치하고, 랩 소스는 읽기 전용이라 binding을 내 폴더에 두고 검사했다. 처음에는 파일을 `dt/` 바로 아래에 뒀다.

```sh
. /var/tmp/P3/venv/bin/activate; B=/var/lab/src/linux-6.18.55/Documentation/devicetree/bindings
yamllint -c $B/.yamllint dt/p3lab,tx31.yaml && echo yamllint-ok
dt-doc-validate -u $B dt/p3lab,tx31.yaml && echo doc-validate-ok
```

```text
yamllint-ok
/var/tmp/P3/dt/p3lab,tx31.yaml: $id: Cannot determine base path from $id, relative path/filename doesn't match actual path or filename
	 $id: http://devicetree.org/schemas/iio/temperature/p3lab,tx31.yaml
	file: /var/tmp/P3/dt/p3lab,tx31.yaml
doc-validate-ok
```

출력에서 볼 것: `$id`의 경로(`iio/temperature/p3lab,tx31.yaml`)는 **실제 파일 위치와 같아야** 한다. 트리 안에서는 `Documentation/devicetree/bindings/iio/temperature/`에 두면 맞는다. 파일을 `dt/iio/temperature/`로 옮기자 경고 없이 `doc-validate-ok`만 나왔다.

### 8.2 예제 18 — 예제 DT를 스키마로 검증: 통과와 실패

무엇을 확인하나: 커널 binding 전체 + 내 binding으로 processed schema를 만들고(TCG VM에서 1분 15초), binding의 examples를 dtb로 컴파일해 `dt-validate`로 검사한다. 그 다음 일부러 틀린 DT(주소 0x4a, 전원 이름 오타)를 검사한다.

```sh
cd /var/tmp/P3/dt && mkdir -p iio/temperature && mv p3lab,tx31.yaml iio/temperature/
dt-mk-schema -j $B dt/ > processed.json
dt-extract-example iio/temperature/p3lab,tx31.yaml > ex.dts
cpp -nostdinc -I /var/lab/src/linux-6.18.55/include -undef -x assembler-with-cpp ex.dts -o ex.pp.dts
dtc -@ -I dts -O dtb -o ex.dtb ex.pp.dts; dt-validate -s processed.json ex.dtb && echo validate-ok
sed -e "s/reg = <0x48>;/reg = <0x4a>;/" -e "s/vdd-supply = <&vdd_3v3>;/vcc-supply = <\&vdd_3v3>;/" ex.pp.dts > bad.dts
dtc -@ -q -I dts -O dtb -o bad.dtb bad.dts && dt-validate -s processed.json bad.dtb
```

```text
ex.dts:12.15-38.7: Warning (avoid_unnecessary_addr_size): /example-0: unnecessary #address-cells/#size-cells without "ranges", "dma-ranges" or child "reg" property
validate-ok
bad.dtb: temperature-sensor@48 (p3lab,tx31): 'vcc-supply' does not match any of the regexes: '^pinctrl-[0-9]+$'
	from schema $id: http://devicetree.org/schemas/iio/temperature/p3lab,tx31.yaml
bad.dtb: temperature-sensor@48 (p3lab,tx31): reg:0:0: 74 is not one of [72, 73]
	from schema $id: http://devicetree.org/schemas/iio/temperature/p3lab,tx31.yaml
bad.dtb: temperature-sensor@48 (p3lab,tx31): 'vdd-supply' is a required property
	from schema $id: http://devicetree.org/schemas/iio/temperature/p3lab,tx31.yaml
```

출력에서 볼 것: 스키마가 **회로도 실수(0x4a 주소)와 오타(vcc-supply)를 부팅 전에** 잡는다. dtc 경고는 `dt-extract-example`이 만든 래퍼 노드 때문이라 무시해도 된다(커널의 `make dt_binding_check`는 이 경고를 끈다). 실제 트리에서는 `make dt_binding_check DT_SCHEMA_FILES=iio/temperature/p3lab,tx31.yaml`과 보드 DT에 대한 `make dtbs_check`로 같은 일을 한다(D7). 그리고 `p3lab` 같은 새 vendor prefix는 `vendor-prefixes.yaml`에 등록해야 checkpatch와 스키마 검사를 통과한다 — 이 가상 칩은 등록하지 않았다.

binding을 쓰면서 드라이버도 다시 보게 된다: binding에 `vdd-supply`를 required로 넣었으니 드라이버도 `devm_regulator_get_enable(dev, "vdd")` 후 t_PU를 기다려야 맞다. TX31 드라이버는 i2c-stub 실습이라 레귤레이터를 생략했다 — 실제 드라이버라면 리뷰에서 바로 지적받는다(§12 연습문제 3).

---

## 9. 체크리스트 — "데이터시트 줄 → 코드 줄"

드라이버를 리뷰하거나 쓸 때 이 표를 위에서부터 채운다. 오른쪽 열이 비어 있으면 빠뜨린 것이다.

| 데이터시트 줄 (TX31 예) | 코드 줄 | 확인 방법 |
|---|---|---|
| "I2C address 0x48/0x49 (ADDR pin)" | binding `reg: enum: [0x48, 0x49]` | dt-validate |
| "8-bit register address, 8-bit data" | `.reg_bits = 8, .val_bits = 8` | debugfs `registers` 폭 |
| 표 4-1 마지막 주소 (가상 포함) | `.max_register = TX31_MAX_REG` | debugfs `range` |
| R/W 열 | `rd_table`, `wr_table` | debugfs `access` 1·2열 |
| "set by HW", "data output" | `volatile_table` | `access` 3열, 바깥에서 값 바꾼 뒤 재읽기 |
| "cleared on read" | `precious_table` (+ volatile) | `access` 4열, `range`에서 빠짐 |
| "write-only command" | readable 제외 + 캐시 우회 쓰기 | sync 경로 리뷰 |
| Reset 열 | `reg_defaults` | 리셋 직후 `i2cdump`와 `registers` 비교 |
| 페이지/뱅크 | `regmap_range_cfg` | bypass 덤프 + 버스 로그 |
| 비트필드 (ODR[3:1]) | `REG_FIELD(reg, lsb, msb)` | 필드 쓰기 후 i2cget |
| "t_PU = 2 ms" | regulator enable 뒤 `fsleep(2000)` | 스코프 (VDD → 첫 SCL) |
| "soft reset, t_RST = 1 ms" | CMD 쓰기 + `fsleep(1000)` | 리셋 직후 ID 읽기 |
| "CHIP_ID = 0xA3" | ID 비교 → `-ENODEV` 또는 경고 | 틀린 ID로 probe |
| "DRDY set when conversion done" | `regmap_read_poll_timeout` (timeout = 최저 ODR 주기 + 여유) | 최저 ODR에서 읽기 |
| "read TEMP_L first / burst" | `regmap_bulk_read(TEMP_L, 2)` | 버스 로그 순서 |
| "FIFO_DATA: reading pops" | `regmap_noinc_read`, precious | H2 §8 |
| "INT active-low, level" | DT `IRQ_TYPE_LEVEL_LOW`, `IRQF_ONESHOT` | `/proc/interrupts` 카운트 |
| "INT cleared by reading INT_SRC" | 핸들러에서 INT_SRC 읽기 | IRQ storm 여부 |
| "LSB = 1/128 °C" | IIO `SCALE = 1000/128` | 알려진 온도로 확인 |
| "factory calibration (OTP)" | probe에서 한 번 읽어 OFFSET | `in_temp_offset` |
| 에라타 "Rev A reset value wrong" | REV_ID 분기 + `regmap_write_bits` | 캐시 vs `i2cget -f` |
| "VDD supply" | `vdd-supply` required + regulator API | "dummy regulator" 로그 없음 |

---

## 10. 커널 안에서는 무슨 일이 일어나나

### 10.1 `new_device` 한 줄에서 probe까지

```text
echo p3_tx31 0x48 > /sys/bus/i2c/devices/i2c-0/new_device
 -> new_device_store()                         drivers/i2c/i2c-core-base.c
    -> i2c_new_client_device(adap, &info)      (fwnode 없음 - DT 노드가 아님)
       -> device_add() -> bus_probe_device()
          -> __driver_attach / i2c_device_match()   이름 "p3_tx31"이 id_table과 일치
             -> really_probe() -> i2c_device_probe() -> tx31_probe(client)
                반환값 -ENODEV/-ENXIO  => dev_dbg "rejects match"만 남기고 조용히 포기
                반환값 -EPROBE_DEFER   => devices_deferred 목록, 나중에 재시도
                그 밖의 에러           => "probe ... failed with error"
```

DT로 만든 장치는 `of_i2c_register_device()`가 같은 `i2c_new_client_device()`를 부르되 fwnode를 붙인다. 그래서 `device_get_match_data()`가 `.data`를 찾는다(§4.2의 차이). D3의 매칭 순서(of → acpi → id_table)가 여기서 실제 동작 차이를 만든다.

### 10.2 `regmap_read(map, 0x110)` 한 번의 경로

```text
regmap_read(map, 0x110, &val)
 -> map->lock()                                 (기본 mutex; fast_io면 spinlock)
 -> _regmap_read()
    -> regmap_readable(0x110)?                  rd_table -> yes
    -> !regmap_volatile && cache: regcache_read  hit이면 여기서 끝 (버스 없음)
    -> _regmap_range_lookup(0x110)              tx31-pages 범위
       -> _regmap_select_page(): page = (0x110-0x100)/0x10 = 1
          -> _regmap_update_bits(PAGE_SEL, 0x01, 1)   캐시상 이미 1이면 쓰기 생략
       -> reg = window_start + (0x110-0x100)%0x10 = 0x10
    -> map->reg_read() = regmap_smbus_byte_reg_read()  (init 때 고른 백엔드)
       -> i2c_smbus_read_byte_data(client, 0x10) -> i2c_smbus_xfer -> adapter
    -> regcache_write(0x110, val)               non-volatile이면 캐시에 저장
 -> map->unlock()
```

H2 §13이 캐시 쪽 경로를 더 자세히 그렸다. 드라이버 작성자가 기억할 것: **페이지 선택과 window 접근이 같은 regmap 락 안에서 일어난다** — IRQ 스레드와 sysfs가 동시에 다른 페이지를 건드려도 섞이지 않는다. 대신 regmap 바깥(i2c-tools, 다른 드라이버, 펌웨어)이 PAGE_SEL을 바꾸면 캐시된 페이지 값이 틀어진다.

### 10.3 드라이버를 쓸 때 다시 만나는 곳

- I2(IIO): `read_raw`의 `IIO_VAL_FRACTIONAL`, `chan_spec`의 `info_mask_*`.
- H6: regmap-irq — 인터럽트 상태/마스크 레지스터를 표로 선언해 irqchip을 만드는 것도 같은 "데이터시트 표 → 선언" 방식이다.
- N6: suspend/resume의 `regcache_cache_only` + `regcache_sync` — §5.4의 쓰기 전용 레지스터 함정이 터지는 곳.
- P6: `regmap-kunit`(랩 커널에 `REGMAP_KUNIT=m`)은 regmap core 자체의 테스트다. 드라이버 테스트로는 i2c-stub 기반 스크립트를 CI에 넣을 수 있다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트 — IMU 리비전이 바뀌었다

로봇 센서 키트의 IMU가 양산 중에 새 리비전으로 바뀌었고, 일부 보드에서만 "가속도 Z축이 가끔 튄다"는 리포트가 왔다. 루틴: (1) 벤더 포털에서 새 리비전 에라타를 받는다, (2) 문제 보드와 정상 보드에서 `i2cdump -f`를 떠서 diff, (3) 리셋값이 다른 레지스터를 찾으면 `reg_defaults`/에라타 분기 확인, (4) 문제 보드 덤프를 `i2c-stub-from-dump`로 책상에 재현, (5) REV_ID 분기 패치. §6.9가 정확히 이 패턴이다.

### 11.2 PMIC — "부트로더가 이미 만졌다"

i.MX8이나 AM62 보드에서 U-Boot이 PMIC(PCA9450, TPS65219 같은, 둘 다 이 트리의 `drivers/regulator`에 regmap 드라이버가 있다)의 버크 전압을 DDR용으로 바꿔 놓는다. 커널 드라이버가 `reg_defaults`를 데이터시트 리셋값으로 채우면 캐시 ≠ HW로 시작한다. 그래서 PMIC 드라이버는 대부분 기본값 표 없이 시작하거나 HW에서 읽어 채운다. 그림 3에서 regulator/mfd가 regmap 사용률 80%대인 것도, 이런 상태 관리를 프레임워크에 맡기기 위해서다.

### 11.3 벤더 BSP 드라이버 vs mainline

벤더 BSP(P2)에 같은 칩 드라이버가 있는데 mainline에도 있다면? 벤더 쪽은 보통 오래된 커널 API, 자체 sysfs ABI, 데이터시트에 없는 "비밀 튜닝 레지스터"를 갖고 있다. 실무 판단: mainline 드라이버를 기본으로 하고, 벤더 드라이버에서 **에라타 대응과 튜닝 시퀀스만** 데이터시트 줄과 대조해 가져온다. 이때 §9 체크리스트가 "벤더 코드의 이 줄은 데이터시트 어디서 왔나"를 묻는 도구가 된다.

### 11.4 Don의 RF 칩 경험과 연결

Apple에서 RF 칩셋 통합을 하며 Don이 하던 "레지스터 덤프 비교로 root cause" 작업은 Linux에서는 `i2cdump -f`(HW) vs debugfs `registers`(드라이버의 믿음) 비교로 바뀐다. 이 둘이 다르다는 사실 자체가 단서다 — §6.9, §6.11이 그 두 가지 대표 원인(잘못된 기본값, 드라이버 모르게 일어난 리셋)이다. 면접에서 이 대비를 말하면 "bare-metal 디버깅을 Linux 도구로 옮길 줄 안다"는 신호가 된다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 기존 드라이버를 안 찾고 새로 씀 | 리뷰에서 "extend X instead" 거절 | compatible/ID값 검색 생략 | §2의 다섯 키로 먼저 검색 |
| 같은 칩의 input/IIO 드라이버 둘 다 켬 | Kconfig가 막거나 엉뚱한 쪽이 bind | `depends on INPUT_ADXL34X=n` 같은 배타 관계 | 요구사항에 맞는 서브시스템 하나만 |
| new_device로 시험했는데 조용히 안 붙음 | driver 링크 없음, 에러 없음 | `device_get_match_data()`가 fwnode 필요 → -ENODEV | dd.c dynamic debug로 확인, DT로 만들거나 `i2c_get_match_data()` |
| 상태/데이터 레지스터를 volatile에서 뺌 | 값이 처음 읽은 값에서 안 바뀜 | 캐시 hit | `access` 3열 확인, volatile 추가 |
| reg_defaults가 HW와 다름 (에라타, 부트로더) | update_bits가 버스에 안 씀, 기능이 안 꺼짐 | 캐시상 이미 같은 값 | `regmap_write`/`write_bits`, 또는 HW에서 기본값 읽기 |
| 쓰기 전용 명령 레지스터를 캐시 | resume 할 때마다 칩 리셋 | regcache_sync가 CMD 재생 | bypass로 쓰기, 또는 readable+volatile |
| 칩이 혼자 리셋됐는데 캐시 유지 | debugfs는 정상, 칩은 꺼져 있음 | 캐시가 HW 상태를 가림 | 리셋 감지 시 mark_dirty + sync, bypass로 진단 |
| clear-on-read를 i2cget/debugfs로 읽음 | 인터럽트가 사라짐 | 관찰이 상태를 바꿈 | precious 선언, 실보드에서 -f 읽기 주의 |
| ID 경고를 무시 (warn-only 드라이버) | IIO 값이 0 또는 엉터리 | 칩 부재/다른 칩인데 probe 성공 | dmesg 경고 확인 + 알려진 물리량 검증 |
| 에뮬레이터 결과를 그대로 믿음 | 페이지 값이 섞임 | i2c-stub bank가 byte data에서만 동작 | `i2cdetect -F`로 백엔드 확인, `functionality` 조정 |

---

## 13. 면접에서 이렇게 말한다

**Q.** You get a new sensor on the board. Walk me through how you bring up its driver.

**A.** 데이터시트에서 인터페이스·레지스터 맵·전원 시퀀스·에라타를 먼저 읽고, 바로 코딩하지 않고 compatible·칩 이름·WHO_AM_I 값으로 mainline과 lore를 검색한다. 형제 칩 드라이버가 있으면 chip_info와 compatible만 추가하고, 없으면 같은 버스·같은 서브시스템의 작고 최근 드라이버를 복제한다. 레지스터 맵은 regmap 표로 옮기고, i2cdetect로 ACK, 첫 probe의 ID, debugfs와 i2cget 비교, 알려진 물리량 확인 순으로 반복한다. 마지막에 binding YAML과 dtbs_check.

> I read the datasheet for the interface, register map, power-up timing and errata, then I search before I write: compatible strings, the part name and the WHO_AM_I value in the tree, and lore for unmerged series. If a sibling chip is supported I extend that driver with a chip_info and a compatible. Otherwise I clone the smallest recent driver on the same bus and subsystem, translate the register table into a regmap config, and iterate: ACK with i2cdetect, chip ID in probe, compare the regmap debugfs dump with raw i2cget reads, and check a known physical value. Then the YAML binding and dtbs_check.

**꼬리 질문**: "검색해서 아무것도 없으면 어떤 드라이버를 복제하나?" → §3.1 기준표.

**Q.** How do you decide which registers are volatile in a regmap config?

**A.** "드라이버가 안 썼는데 값이 바뀔 수 있나"로 정한다. 상태·데이터·카운터·자동 해제 비트가 있는 레지스터는 volatile, 읽으면 지워지는 것은 precious까지. ID·OTP 보정값은 캐시해도 된다. 그리고 debugfs `access` 파일로 표가 의도대로 들어갔는지 확인한다.

> The rule is: can the value change without the driver writing it? Status, data, counters and anything with self-clearing bits are volatile; clear-on-read registers are also precious so debugfs never reads them. IDs and OTP calibration are cacheable. I verify the tables with the regmap debugfs access file.

**Q.** What can go wrong with regmap caching during bring-up?

**A.** 캐시와 HW가 어긋나는 세 가지: 리셋값이 데이터시트와 다를 때(에라타, 부트로더가 만짐) update_bits가 쓰기를 생략하고, 칩이 혼자 리셋되면 debugfs가 옛 값을 보여 주고, 쓰기 전용 명령 레지스터가 캐시에 남으면 regcache_sync가 재생한다. 진단은 debugfs 덤프와 `i2cget -f`를 비교하거나 cache_bypass를 켠다.

> Three ways the cache diverges from hardware: wrong reset defaults — errata or the bootloader touched the chip — so update_bits skips the write; the chip resets behind the driver's back so debugfs shows stale values; and a write-only command register that gets cached is replayed by regcache_sync. I diagnose it by diffing the debugfs dump against raw i2cget reads or by enabling cache_bypass, and I fix it with forced writes or a mark_dirty plus sync.

**꼬리 질문**: "update_bits와 write_bits의 차이는?" → force 플래그: write_bits는 값이 같아도 쓴다.

**Q.** How do you handle a chip with paged registers?

**A.** regmap `ranges`(regmap_range_cfg)로 가상 주소 범위, 페이지 선택 레지스터, window를 선언한다. 드라이버는 가상 주소만 쓰고 core가 선택과 접근을 같은 락 안에서 한다. 선택 레지스터도 캐시되니 같은 페이지 연속 접근은 쓰기가 생략된다. probe 초반에 페이지를 알려진 값으로 되돌리는 것도 잊지 않는다.

> I describe it with a regmap_range_cfg: a virtual range, the selector register and mask, and the data window. The driver uses virtual addresses only; the core selects the page and accesses the window under the same lock, and skips the selector write if the page is already selected. In probe I make sure the page is in a known state, because a warm reboot can leave it selected.

**Q.** How do you develop a driver before the hardware arrives?

**A.** i2c-stub으로 가짜 칩을 만들고 i2cset으로 전원 투입 상태를 심은 다음 new_device로 붙인다. 실보드 덤프가 있으면 i2c-stub-from-dump로 재현한다. 한계 — 자동 해제·clear-on-read 같은 행동은 흉내 내지 못하고 bank는 byte data에서만 동작한다 — 를 알고 써야 하고, 행동이 중요하면 작은 가짜 어댑터 모듈을 직접 쓴다.

> I fake the chip with i2c-stub, preload a power-on register image with i2cset or i2c-stub-from-dump, and bind the driver through new_device. I know its limits: it doesn't model self-clearing or clear-on-read behaviour, and its bank emulation only works for byte-data transfers, so when behaviour matters I write a small fake adapter.

**Q.** A probe silently doesn't bind. How do you debug it?

**A.** driver 링크가 없고 에러도 없으면 probe가 -ENODEV/-ENXIO를 돌려줬을 가능성이 크다. `drivers/base/dd.c` dynamic debug로 "rejects match"를 보고, devices_deferred로 defer 여부를 확인한다. 흔한 원인은 fwnode 없이 만든 장치에서 match data를 못 찾는 경우, 레귤레이터나 GPIO 대기다.

> No driver link and no error usually means probe returned -ENODEV or -ENXIO, which the core logs only at debug level. I enable dynamic debug for drivers/base/dd.c to see "rejects match", check devices_deferred for probe deferral, and look at match data — a device created without a firmware node has no of_device_id data.

**Q.** How do you turn an erratum into code?

**A.** 리비전 ID로 분기하고, 에라타 번호를 주석에 남기고, regmap 캐시가 그 쓰기를 생략하지 않게 강제 쓰기를 쓴다. 그리고 캐시 vs HW 비교로 그 한 줄이 버스에 나갔는지 확인한다.

> Branch on the revision ID, cite the erratum number in a comment, and use a forced write so the regmap cache can't optimise it away. Then I confirm on the bus — or by comparing the cached value with a raw read — that the write actually happened.

---

## 14. 직접 해보기

1. (개념) TX31의 `STATUS`를 volatile 표에서 빼면 `in_temp_raw` 읽기는 어떻게 되나? DRDY 폴링과 TEMP 값 각각에 대해 답하라.
   정답/힌트: STATUS가 캐시되면 처음 읽은 DRDY 값이 고정 — 처음이 1이면 폴링은 즉시 통과(문제 숨김), 0이면 매번 1.1 s 타임아웃. TEMP는 volatile로 남아 있으니 값은 바뀐다.
2. (코드) 이 노트의 i2c-stub 실습을 재현하고, `tx31_poweron.sh`에서 THR_HI를 0x60으로 심은 뒤 debugfs `registers`의 `100:` 값과 `i2cget`(페이지 0 선택 후 0x10) 값을 비교하라. 어긋나는 이유는?
   정답/힌트: reg_defaults가 0x50이라 캐시는 HW를 안 읽고 0x50을 보여 준다. "칩이 데이터시트 리셋값과 다르게 깨어남" = §5.3.
3. (코드) TX31 드라이버에 `vdd` 레귤레이터를 추가하라(`devm_regulator_get_enable`, 그 뒤 `fsleep(TX31_T_PU_US)`). i2c-stub에서는 dummy regulator 로그가 나오는지 확인하라.
   정답/힌트: `supply vdd not found, using dummy regulator`. 실보드에서 이 줄이 보이면 DT 누락(P4).
4. (코드) INT_SRC를 읽는 threaded IRQ 핸들러를 추가하고 DT 없이 시험할 방법을 생각해 보라.
   정답/힌트: gpio-sim(H4) 라인을 IRQ로 쓰는 DT overlay가 필요하다. i2c-stub 단독으로는 IRQ를 줄 수 없다.
5. (관찰) `events/regmap/regmap_reg_read`·`regmap_reg_write` tracepoint로 §6.12 덤프를 다시 보고, 캐시 hit(`regcache_*`)와 버스 접근을 구분하라.
   정답/힌트: H2 §10의 방법을 그대로 쓴다. bypass 끄고/켜고 비교.
6. (조사) 6.18 `regmap-debugfs.c`의 range 파일 반복 버그를 최소 수정으로 고치는 패치를 구상하라(실제로 보내지는 말 것).
   정답/힌트: subrange일 때 `p`를 0에서 시작하게 하거나, `regmap_debugfs_get_dump_start()`가 base 경로에서도 `*pos`를 설정하게 한다. 수정 전 lore에서 기존 패치 검색.
7. (검색) 랩 소스에서 `regmap_write_bits` 사용처 3곳을 찾아 각자 왜 강제 쓰기가 필요한지(주석이나 데이터시트 근거) 설명하라.
   정답/힌트: `grep -rn regmap_write_bits drivers | head` — 대개 "write 1 to clear" 상태 비트나 트리거 비트다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| datasheet | 데이터시트 | 칩의 인터페이스·레지스터·타이밍·전기 특성 명세 |
| errata | 에라타 | 실리콘 리비전별 알려진 결함과 회피 방법 문서 |
| compatible | 호환 문자열 | DT 노드와 드라이버를 잇는 `"vendor,part"` 키 |
| binding | 바인딩 | compatible별 DT 속성 계약, YAML 스키마로 검증 |
| lore | lore.kernel.org | 커널 메일링 리스트 아카이브 + 검색 |
| chip_info | 칩 정보 구조체 | 형제 칩 차이(ID, 스케일, 채널)를 담아 match data로 넘기는 관례 |
| i2c-stub | 가짜 SMBus 어댑터 | 메모리 배열을 레지스터로 쓰는 테스트 모듈 |
| new_device | sysfs 장치 생성 | `echo name addr > .../new_device`로 DT 없이 i2c client 생성 |
| regmap_config | regmap 설정 | 주소/값 폭, 접근 표, 캐시, 페이지를 선언 |
| volatile | 휘발성 레지스터 | HW가 바꿀 수 있어 캐시하지 않는 레지스터 |
| precious | 소중한 레지스터 | 읽기 자체가 부작용이라 드라이버 외에는 읽으면 안 됨 |
| reg_defaults | 리셋값 표 | 캐시의 초기값 = "리셋 직후 HW" 약속 |
| regmap_range_cfg | 페이지 설정 | 가상 주소 → 선택 레지스터 + window 변환 |
| regmap_field | 비트필드 핸들 | `REG_FIELD(reg, lsb, msb)`를 이름으로 읽고 쓰기 |
| write_bits | 강제 RMW | 값이 같아도 버스에 쓰는 update_bits |
| cache_bypass | 캐시 우회 | debugfs/API로 캐시를 건너뛰고 HW 직접 접근 |
| regcache_sync | 캐시 복원 | 캐시 값을 HW에 다시 쓰기 (resume, 리셋 후) |
| dt-validate | DT 검증기 | dtb를 processed schema로 검사 (dtschema 패키지) |
| fwnode | 펌웨어 노드 | DT/ACPI 노드의 공통 추상, match data의 출처 |

---

## 16. 요약 & 체크리스트

새 칩 드라이버의 실무 루틴은 "읽기 → 찾기 → 복제 → 표로 선언 → 칩과 반복 → binding"이다. 데이터시트에서는 레지스터의 **행동**(누가 값을 바꾸나, 읽으면 무엇이 변하나, 언제 접근해도 되나, 리비전별로 무엇이 다른가)을 뽑고, 코딩 전에 compatible·칩 이름·ID값·binding·lore로 기존 드라이버를 찾는다. 없으면 같은 버스·서브시스템의 작고 최근 드라이버를 복제한다. regmap 표는 데이터시트 표의 번역이고, 표를 틀리면 캐시가 소리 없이 버스를 생략한다 — 이 노트에서 에라타 리셋값(`update_bits` 생략), 바깥 리셋(캐시가 가림), 쓰기 전용 명령(sync 재생)을 실제로 보거나 소스로 확인했다. i2c-stub과 i2c-tools로 칩 없이 반복할 수 있지만 도구도 한계와 버그가 있다(bank는 byte data만, debugfs range 파일 무한 반복). 최종 확인은 언제나 debugfs(드라이버의 믿음)와 `i2cget -f`(HW의 진실)의 비교, 그리고 알려진 물리량이다.

- [ ] 데이터시트에서 드라이버에 필요한 절 9가지를 말하고, 각각이 코드 어디로 가는지 설명할 수 있다
- [ ] compatible·Kconfig·ID값·binding·lore 다섯 키로 기존 드라이버를 찾을 수 있다
- [ ] "같은 칩, 두 서브시스템" 상황(adxl34x vs adxl345)에서 고르는 기준을 말할 수 있다
- [ ] 복제 기준 드라이버를 고르는 기준과 복제 후 반드시 바꿀 것을 말할 수 있다
- [ ] i2c-stub에 값을 심고 new_device로 기존 드라이버를 probe 할 수 있다
- [ ] 조용히 bind 안 되는 probe를 dd.c dynamic debug로 진단할 수 있다
- [ ] 레지스터 표를 rd/wr/volatile/precious 표, reg_defaults, ranges, REG_FIELD로 옮길 수 있다
- [ ] update_bits와 write_bits의 차이, 그리고 에라타 대응에 왜 write_bits가 필요한지 설명할 수 있다
- [ ] 쓰기 전용 레지스터가 regcache_sync에서 재생되는 이유와 회피법을 설명할 수 있다
- [ ] debugfs `access`/`range`/`registers`와 `cache_bypass`로 표와 캐시를 검증할 수 있다
- [ ] binding YAML을 쓰고 dt-doc-validate / dt-validate로 검증할 수 있다
- [ ] "데이터시트 줄 → 코드 줄" 체크리스트로 드라이버를 리뷰할 수 있다

## 참고 자료

- docs.kernel.org — I2C: "Linux I2C and SMBus stub" (`Documentation/i2c/i2c-stub.rst`), "Implementing I2C device drivers", "How to instantiate I2C devices" (new_device 포함)
- docs.kernel.org — Devicetree: "Writing Devicetree Bindings in json-schema" (`Documentation/devicetree/bindings/writing-schema.rst`), "Writing DT bindings" (`writing-bindings.rst`)
- 소스 (6.18.55, elixir.bootlin.com에서 같은 경로): `drivers/base/regmap/regmap.c`, `regcache.c`, `regmap-debugfs.c`, `regmap-i2c.c`, `include/linux/regmap.h`, `drivers/i2c/i2c-stub.c`, `drivers/iio/accel/adxl345_core.c`, `drivers/iio/imu/st_lsm6dsx/st_lsm6dsx_core.c`, `drivers/iio/pressure/bmp280-core.c`, `drivers/base/dd.c`
- lore.kernel.org 검색 문법 도움말: `https://lore.kernel.org/all/_/text/help/`
- dtschema: `https://github.com/devicetree-org/dt-schema`
- i2c-tools (i2cdetect/i2cget/i2cset/i2cdump, i2c-stub-from-dump): `https://git.kernel.org/pub/scm/utils/i2c-tools/i2c-tools.git`
- Bootlin, "Linux kernel and driver development" 교육 슬라이드 (I2C, regmap, DT 장)
- 이 사이트의 관련 노트: H2(regmap), H5(I2C), H3(regulator), H4(GPIO), H6(regmap-irq), I2(IIO), D3(매칭), D7(binding 검증), G8(debugfs), M1(dynamic debug), M5(ftrace), N6(driver PM), P1(bring-up 순서), P2(BSP), P4(회로도 → DT), P6(테스트), P7(upstream)
