---
id: BM-M12
time: 90분
updated: 2026-11-11
---
# BM-M12 RTC와 저전력 타임키핑: 알람 웨이크업·백업 도메인

## 학습 목표

- RTCCLK → PREDIV_A → PREDIV_S → ck_spre(1 Hz) 체인을 그리고, 32.768 kHz 에서 127/255 를 고르는 이유(전력 vs 서브초 분해능)를 숫자로 설명할 수 있다.
- 백업 도메인(backup domain) 쓰기의 세 관문 — `PWR_CR.DBP`, `RTC_WPR` 키(0xCA → 0x53), `ISR.INIT`/`INITF` 핸드셰이크 — 중 하나라도 빠지면 쓰기가 **에러 없이 무시**된다는 것을 재현한다.
- 섀도 레지스터(shadow register) 잠금 규칙으로 자정 찢어짐(torn read)을 막고, 섀도가 없을 때(`BYPSHAD=1`, F1식 분할 카운터)는 두 번 읽기로 막는다 — 버스 접근 사이 모든 지점에 RTC 클럭을 끼워 넣어 증명한다.
- BCD 캘린더 ↔ Unix epoch(H. Hinnant `days_from_civil`), 요일, "다음 07:30" 알람과 ALRMAR 마스크를 구현하고, Stop 복귀 후 RSF 재동기화와 알람 ISR 플래그 지우기 함정 3종을 안다.
- 웜 리셋(VBAT 유지)과 콜드 부트(백업 도메인 리셋)를 `INITS` + 백업 레지스터 매직으로 구분하는 부트 정책을 짠다.

## 1. 문제

배터리 센서 노드는 Stop/Standby 로 자다가 RTC 알람으로 07:30 에 깨고, 로그에 타임스탬프를 찍는다. CPU 가 꺼져 있는 동안 시간을 지키는 유일한 블록이 RTC 이고, 펌웨어 면접에서 RTC 질문은 거의 항상 **"가끔 날짜가 하루 틀려요"**, **"`HAL_RTC_GetTime()` 만 부르면 시계가 멈춰요"**, **"리셋하면 시간이 날아가요 / 안 날아가요"**, **"Stop 에서 깨서 읽었더니 자기 전 시각이에요"** 같은 실전 버그로 나온다. SSD 펌웨어에서 power-on hours·전원 차단 타임스탬프를 다뤄 본 경험, RF 모듈 보드에서 32.768 kHz 크리스털이 안 뜨던 경험이 바로 이 레슨의 재료다.

STM32F4 RTC 의 구조 (RM0090 기준 대표값 — RM 확인 권장):

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <rect x="8" y="28" width="520" height="292" rx="10" fill="none" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="18" y="46" fill="currentColor" font-weight="bold" font-size="13">백업 도메인 — VDD 가 꺼져도 VBAT 로 유지 (쓰기는 PWR_CR.DBP=1 일 때만)</text>
  <rect x="20" y="64" width="100" height="48" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="28" y="84" fill="currentColor">LSE 32.768k</text>
  <text x="30" y="102" fill="currentColor">RCC_BDCR</text>
  <rect x="148" y="64" width="110" height="48" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="156" y="84" fill="currentColor">PREDIV_A (7비트)</text>
  <text x="156" y="102" fill="currentColor">÷(A+1) = ÷128</text>
  <rect x="286" y="64" width="110" height="48" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="294" y="84" fill="currentColor">PREDIV_S (15비트)</text>
  <text x="294" y="102" fill="currentColor">÷256 · SSR</text>
  <rect x="420" y="64" width="98" height="48" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="428" y="84" fill="currentColor">캘린더 TR/DR</text>
  <text x="428" y="102" fill="currentColor">BCD, 1초 증가</text>
  <path d="M120 88 L148 88 M258 88 L286 88 M396 88 L420 88" stroke="currentColor"/>
  <text x="252" y="128" fill="currentColor">ck_apre 256 Hz</text>
  <text x="380" y="128" fill="currentColor">ck_spre 1 Hz</text>
  <rect x="420" y="160" width="98" height="52" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="428" y="180" fill="currentColor">알람 A 비교</text>
  <text x="428" y="198" fill="currentColor">ALRMAR MSK1–4</text>
  <path d="M469 112 L469 160" stroke="currentColor"/>
  <rect x="20" y="160" width="378" height="52" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="30" y="180" fill="currentColor">쓰기 관문: ① DBP=1  ② WPR ← 0xCA, 0x53</text>
  <text x="30" y="198" fill="currentColor">③ ISR.INIT=1 → INITF 대기 (TR/DR/PRER 는 이때만 써진다)</text>
  <rect x="20" y="240" width="250" height="48" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="30" y="260" fill="currentColor">RTC_BKP0R..19 (20 × 32비트)</text>
  <text x="30" y="278" fill="currentColor">DBP 만 필요, WPR 대상 아님</text>
  <rect x="290" y="240" width="228" height="48" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="298" y="260" fill="currentColor">ISR.INITS = 연도 ≠ 0</text>
  <text x="298" y="278" fill="currentColor">"한 번이라도 설정됨" 표시</text>
  <rect x="560" y="64" width="170" height="64" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="568" y="84" fill="currentColor">섀도 SSR / TR / DR</text>
  <text x="568" y="102" fill="currentColor">2 RTCCLK 마다 복사 → RSF</text>
  <text x="568" y="120" fill="currentColor">CPU 는 APB 로 이걸 읽는다</text>
  <path d="M518 88 L560 88" stroke="currentColor"/>
  <rect x="560" y="160" width="76" height="52" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="568" y="180" fill="currentColor">ALRAF</text>
  <text x="568" y="198" fill="currentColor">→ EXTI 17</text>
  <rect x="654" y="160" width="76" height="52" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="662" y="180" fill="currentColor">NVIC 41</text>
  <text x="662" y="198" fill="currentColor">Stop 깨움</text>
  <path d="M518 186 L560 186 M636 186 L654 186" stroke="currentColor"/>
  <text x="560" y="250" fill="currentColor">BYPSHAD=1 이면 섀도를</text>
  <text x="560" y="268" fill="currentColor">건너뛰고 카운터를 직접</text>
  <text x="560" y="286" fill="currentColor">읽는다 → 두 번 읽기 필수</text>
</svg>
```

이 레슨은 맥에서 돈다. 하드웨어 대신 **RTCCLK 한 사이클 단위로 도는 RTC 모델**(프리스케일러 다운카운터, BCD 캘린더, 섀도 복사와 잠금, WPR 키 상태, INITF·ALRAWF 지연, 알람 비교기, EXTI17 → NVIC-lite)을 BM-B02 mock 축약판 위에 올렸다. 폴링 루프 한 바퀴는 `BUSY_WAIT_STEP()` 으로 RTCCLK 1 사이클을 흘려 보낸다(타깃에서는 no-op). 레이스는 BM-B04 의 "n 번째 버스 접근 직후 끼어들기" 훅으로 **"이 두 읽기 사이에 RTCCLK 2 사이클이 흘렀다"** 를 주입해 재현한다. Stop 슬립은 `sim_stop_until_irq()` 가 초 단위로 빨리 감고(섀도 복사는 멈춤), enable 된 IRQ 라인이 서면 핸들러를 부른다.

### Part A — 캘린더 레지스터와 설정/읽기

- `bcd_ok / bin2bcd / bcd2bin`, `rtc_pack(t, &tr, &dr)` / `rtc_unpack(ss, tr, dr, prediv_s, t)` — 24시간제 TR/DR 비트 배치, 서브초 ms = (PREDIV_S − SS)·1000/(PREDIV_S+1) 내림.
- `rtc_prescaler(f, &a, &s, &ppm)` — (A+1)(S+1) 이 f 에 가장 가깝게(상대오차), 동률이면 A 가 큰 쪽. 0 = 정확, 1 = 근사, -1 = f==0. 4,194,304 조합 전수와 대조.
- `rtc_unlock / rtc_lock / rtc_enter_init / rtc_exit_init / rtc_wait_sync`, 그리고 이것들로 `rtc_set(t)` (요일은 날짜에서 계산) 와 `rtc_get(t)`.

### Part B — 면접관이 찌르는 결함

- DBP 가 안 켜짐(PWR 클럭 꺼짐), WPR 키 순서 틀림, INITF 를 안 기다림, RTCEN 안 켬 → 쓰기 무시 / 상한 안의 타임아웃.
- **자정 찢어짐**: 2026-12-31 23:59:59.996, 다음 RTCCLK 에 해가 바뀌는 위상에서 읽기 함수의 **모든 접근 지점 k** 에 클럭 2 사이클 주입. `BYPSHAD=1` 인데 한 번 읽기, 섀도 모드인데 DR 먼저 읽기 → 찢어짐. 고침: 섀도 순서 SSR→TR→DR, `rtc_get_bypass()` 두 번 읽기.
- 시간만 읽고 DR 을 안 읽음 → 잠금이 남아 시계가 멈춰 보임. 설정 직후 / Stop 복귀 직후 RSF 를 안 기다림 → 묵은 값.
- `rtc_alarm_write(alrmar)` — ALRAE=1 인 채로 ALRMAR 를 쓰면 무시됨. 알람 ISR 함정 3종: `ISR = ~ALRAF` (INIT 비트 덮어씀), EXTI_PR 안 지움(폭주), ALRAF 안 지움(다음 날 못 깸).

### Part C — epoch, 알람, 센서 노드

- `days_from_civil / civil_from_days / weekday_from_days`, `rtc_to_epoch / epoch_to_rtc` (2000–2099), `alarm_daily(h,m,s)`, `alarm_at(t)`, `next_daily_epoch(now,h,m,s)`.
- 시뮬레이션: 매일 07:30 Stop 웨이크 3일, 15분 주기 웨이크가 2028-02-29(윤일) 자정을 넘음, 웜 리셋(시간 유지)과 콜드 부트(기본 시각으로 재설정) — 브링업 엔지니어가 읽는 로그를 찍는다.

## 2. Problem (English)

You are writing the RTC driver for an STM32F4-class MCU (RM0090 register layout; your mock defines the truth). RTCCLK = LSE 32.768 kHz.

```c
int  rtc_prescaler(uint32_t f_rtcclk, uint32_t *prediv_a, uint32_t *prediv_s, int32_t *err_ppm);
int  rtc_set(const rtc_time_t *t);      /* 2000-01-01 .. 2099-12-31, 24h; weekday computed from date */
int  rtc_get(rtc_time_t *t);            /* BYPSHAD=0: consistent date+time+ms */
int  rtc_get_bypass(rtc_time_t *t);     /* BYPSHAD=1: no shadow registers */
int  rtc_alarm_write(uint32_t alrmar);  /* alarm A, interrupt enabled */
void RTC_Alarm_IRQHandler(void);        /* EXTI line 17 → IRQ 41 */
int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d);   /* days since 1970-01-01 */
int  epoch_to_rtc(uint32_t epoch, rtc_time_t *t);  uint32_t alarm_daily(uint8_t h, uint8_t m, uint8_t s);
```

Constraints: every polling loop is bounded (return `RTC_ETIMEOUT`); no malloc; writes to TR/DR/PRER only in init mode; the calendar must never be observed torn (date from one second, time from another).

Examples:
- `rtc_prescaler(32768)` → A=127, S=255, exact. `rtc_prescaler(32000)` → 127/249. `rtc_prescaler(65537)` → 98/661, −15 ppm.
- `rtc_set(2026-11-11 13:45:30)` → TR = `0x00134530`, DR = `0x00267111` (weekday 3 = Wednesday).
- `alarm_daily(7,30,0)` = `0x80073000` (MSK4: any date). 2026-11-11 22:15 → next wake 2026-11-12 07:30:00, 33300 s later.
- Follow-ups: "Why does reading only the time freeze the clock?", "What do you clear in the alarm ISR, in which order?", "How does boot code know the RTC survived a reset?"

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크가 깨져 있으면
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-M12_st && /tmp/BM-M12_st      # 경고 0, FAIL 다수, exit 1
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-M12_so && /tmp/BM-M12_so -v  # -v: rtc_set 접근 로그
```

시작 코드는 컴파일되고, 모든 폴링·슬립 루프에 상한이 있어 멈추지 않는다. 실제 실행 마지막 줄 (starter): `29/575 passed` (exit 1). 프리스케일러 스텁이 `RTC_EINVAL` 을 돌려 `rtc_init` 부터 실패하므로, **A3 부터 채우면** 뒤 그룹들이 하나씩 살아난다.

## 4. 힌트

<details><summary>힌트 1 — BCD 와 비트 배치</summary>

BCD 한 바이트 = 십의 자리 니블 + 일의 자리 니블. TR 의 시는 HT[21:20]+HU[19:16] 라 `(tr >> 16) & 0x3F` 를 `bcd2bin` 하면 된다. 분·초는 7비트(0x7F), 월은 5비트(0x1F, MT 가 1비트), 일은 6비트(0x3F). 요일 WDU[15:13] 는 BCD 가 아닌 1..7.
</details>

<details><summary>힌트 2 — 프리스케일러: A 마다 S 후보는 두 개뿐</summary>

A+1 = a 를 고정하면 오차 |f − a·s|/(a·s) 는 s 가 f/a 에 가까워질수록 줄어든다 → 후보는 ⌊f/a⌋ 와 ⌊f/a⌋+1 (1..32768 로 자름). a 를 128 부터 내려가며 **엄격히 더 좋을 때만** 바꾸면 "동률이면 큰 A" 가 저절로 된다. 분수 비교는 교차곱(64비트)으로.
</details>

<details><summary>힌트 3 — ISR 레지스터에 쓸 때: rc_w0 비트에는 1 을</summary>

ALRAF·RSF 는 0 을 써야 지워지고 1 을 쓰면 그대로(rc_w0, BM-B04). INIT 은 평범한 rw. 그래서 "INIT 만 1" 은 `0xFFFFFFFF`, "INIT 만 0" 은 `~INIT`, "RSF 만 지우기" 는 `~(RSF|INIT)`. `ISR = INIT` 처럼 쓰면 0 이 들어간 다른 플래그(ALRAF)가 지워진다.
</details>

<details><summary>힌트 4 — 섀도 잠금: 누가 잠그고 누가 푸나</summary>

SSR 을 읽으면 TR·DR 이, TR 을 읽으면 DR 이 잠기고, **DR 을 읽어야 풀린다**. SSR 은 아무도 잠그지 않는다. 그러니 순서는 낮은 자리 → 높은 자리: SSR → TR → DR. 마지막에 DR 을 안 읽으면 잠금이 남는다.
</details>

<details><summary>힌트 5 — 섀도가 없을 때</summary>

`BYPSHAD=1` 이면 읽는 순간의 카운터가 그대로 나온다. 세 레지스터를 두 번 연속 읽어 둘 다 같으면 그 사이에 클럭이 안 넘어간 것. 다르면 다시 (상한 3회).
</details>

<details><summary>힌트 6 — 알람 ISR 은 두 군데를 지운다</summary>

RTC 쪽 ALRAF(rc_w0, INIT 비트는 현재 값 유지)와 EXTI 쪽 PR bit 17(W1C). ALRAF 를 안 지우면 다음 매치 때 0→1 에지가 없어 EXTI 가 다시 안 선다. EXTI_PR 을 안 지우면 NVIC 라인이 계속 high.
</details>

## 5. 풀이

### Part A — BCD 와 TR/DR 레이아웃

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">RTC_TR = 0x00134530 (13:45:30)   ·   RTC_DR = 0x00267111 (2026-11-11 수)   — 비트 23..0</text>
  <text x="50" y="40" fill="currentColor">23</text><text x="610" y="40" fill="currentColor">3</text><text x="708" y="40" fill="currentColor">0</text>
  <rect x="50" y="48" width="28" height="44" class="f-mut" stroke="currentColor"/>
  <rect x="78" y="48" width="28" height="44" class="f-warn" stroke="currentColor"/><text x="81" y="66" fill="currentColor">PM</text><text x="88" y="84" fill="currentColor">0</text>
  <rect x="106" y="48" width="56" height="44" class="f-acc" stroke="currentColor"/><text x="112" y="66" fill="currentColor">HT</text><text x="128" y="84" fill="currentColor">1</text>
  <rect x="162" y="48" width="112" height="44" class="f-acc" stroke="currentColor"/><text x="170" y="66" fill="currentColor">HU [19:16]</text><text x="212" y="84" fill="currentColor">3</text>
  <rect x="274" y="48" width="28" height="44" class="f-mut" stroke="currentColor"/>
  <rect x="302" y="48" width="84" height="44" class="f-ok" stroke="currentColor"/><text x="310" y="66" fill="currentColor">MNT</text><text x="340" y="84" fill="currentColor">4</text>
  <rect x="386" y="48" width="112" height="44" class="f-ok" stroke="currentColor"/><text x="394" y="66" fill="currentColor">MNU [11:8]</text><text x="438" y="84" fill="currentColor">5</text>
  <rect x="498" y="48" width="28" height="44" class="f-mut" stroke="currentColor"/>
  <rect x="526" y="48" width="84" height="44" class="f-acc" stroke="currentColor"/><text x="534" y="66" fill="currentColor">ST</text><text x="564" y="84" fill="currentColor">3</text>
  <rect x="610" y="48" width="112" height="44" class="f-acc" stroke="currentColor"/><text x="618" y="66" fill="currentColor">SU [3:0]</text><text x="662" y="84" fill="currentColor">0</text>
  <rect x="50" y="128" width="112" height="44" class="f-acc" stroke="currentColor"/><text x="58" y="146" fill="currentColor">YT [23:20]</text><text x="102" y="164" fill="currentColor">2</text>
  <rect x="162" y="128" width="112" height="44" class="f-acc" stroke="currentColor"/><text x="170" y="146" fill="currentColor">YU [19:16]</text><text x="212" y="164" fill="currentColor">6</text>
  <rect x="274" y="128" width="84" height="44" class="f-warn" stroke="currentColor"/><text x="282" y="146" fill="currentColor">WDU</text><text x="310" y="164" fill="currentColor">3</text>
  <rect x="358" y="128" width="28" height="44" class="f-ok" stroke="currentColor"/><text x="362" y="146" fill="currentColor">MT</text><text x="368" y="164" fill="currentColor">1</text>
  <rect x="386" y="128" width="112" height="44" class="f-ok" stroke="currentColor"/><text x="394" y="146" fill="currentColor">MU [11:8]</text><text x="438" y="164" fill="currentColor">1</text>
  <rect x="498" y="128" width="56" height="44" class="f-mut" stroke="currentColor"/>
  <rect x="554" y="128" width="56" height="44" class="f-acc" stroke="currentColor"/><text x="562" y="146" fill="currentColor">DT</text><text x="578" y="164" fill="currentColor">1</text>
  <rect x="610" y="128" width="112" height="44" class="f-acc" stroke="currentColor"/><text x="618" y="146" fill="currentColor">DU [3:0]</text><text x="662" y="164" fill="currentColor">1</text>
  <text x="10" y="200" fill="currentColor">회색 = 예약(0). 연도는 2자리(00–99): 세기는 소프트웨어 몫. WDU 는 1=월 … 7=일, 000 금지 — RTC 는 요일을 검사하지 않고</text>
  <text x="10" y="218" fill="currentColor">자정마다 +1 만 한다. 그래서 rtc_set 은 t-&gt;wday 를 믿지 않고 날짜로 계산해 넣는다. 리셋값 DR = 0x00002101 (2000-01-01, WDU=1).</text>
  <text x="10" y="236" fill="currentColor">12시간제(FMT=1)는 PM[22] + 시 01–12 → 연습 1.</text>
</svg>
```

검증: `A2` 가 하루의 86,400 초 전부, 2000–2099 의 36,525 일 전부를 pack → unpack 왕복하고, 하드웨어 달력(모델)의 자정 넘김을 `C3` 가 36,524 번 epoch 산술과 대조한다. 2099-12-31 → 다음 날은 RTC 에서 **연도 00 = "2000-01-01"** 로 읽힌다 — 요일 비트는 자정마다 +1 이라 금(5)인데, 이는 실제 2100-01-01(금)과 맞고 2000-01-01(토)과는 안 맞는다. 2자리 연도 하드웨어의 세기 문제다.

### Part A — 프리스케일러: 왜 127/255 인가

ck_apre = RTCCLK/(A+1), ck_spre = ck_apre/(S+1) = 1 Hz. 32,768 = 2^15 이라 (A+1)(S+1) = 2^15 인 조합이 8 개 있다. 비동기(ripple) 프리스케일러는 싸고, 15비트 동기 카운터(SSR)는 클럭마다 많은 플립플롭이 토글하므로 **A 를 최대로** 잡아 동기 쪽 클럭을 낮추는 것이 RM 권고다 (RM0090 "set the asynchronous prescaler to a high value to minimize consumption" — 확인 권장). 대가는 서브초 분해능. `demo_prescaler_drift.c` 실제 출력:

```text
1) 클럭원별 PREDIV 선택 (A 최대 우선)
   RTCCLK                            Hz     A      S      ppm   ck_apre    SS res
   LSE 32.768 kHz                 32768   127    255        0   256.0 Hz   3906 us
   LSI nominal 32 kHz (F4)        32000   127    249        0   250.0 Hz   4000 us
   LSI 40 kHz (other parts)       40000   124    319        0   320.0 Hz   3125 us
   HSE 8 MHz / RTCPRE 8         1000000   124   7999        0  8000.0 Hz    125 us
   HSE 25 MHz / RTCPRE 25       1000000   124   7999        0  8000.0 Hz    125 us
   HSE 25 MHz / RTCPRE 31        806452    73  10897        0 10898.0 Hz     92 us
   LSI measured 31 337 Hz         31337     0  31336        0 31337.0 Hz     32 us

2) 32.768 kHz 에서 정확한 (A+1)(S+1)=32768 조합: 전력(동기 카운터 클럭) vs 서브초 분해능
     A+1    S+1 ck_apre Hz  SS res (us)
       1  32768      32768        30.52
       2  16384      16384        61.04
       4   8192       8192       122.07
       8   4096       4096       244.14
      16   2048       2048       488.28
      32   1024       1024       976.56
      64    512        512      1953.12
     128    256        256      3906.25

3) rtc_prescaler vs brute force (모든 A,S = 4,194,304 조합)
   160 frequencies, 0 mismatches
```

마지막 줄 "LSI measured 31 337 Hz" 를 보라. 31,337 은 소수라서 **정확한** 조합은 A=0 하나뿐 → 내 규칙(정확도 우선)은 전력에 가장 나쁜 답을 고른다. LSI 는 어차피 ±수십 % 라(아래 4번 표) 실무에서는 A=127 로 두고 스무스 캘리브레이션(연습 2)이나 주기적 재보정으로 맞추는 편이 낫다 — 규칙은 "무엇을 최적화하나" 를 먼저 정해야 한다는 면접 포인트다. "HSE 25 MHz / RTCPRE 31" 의 806,452 Hz 는 25e6/31 = 806,451.6 을 반올림한 값이다.

```text
4) 클럭 오차 → 하루/한 달 드리프트 (프리스케일러는 명목값에 맞춰 둠)
   case                                  error ppm        s/day     min/30days
   LSE +20 ppm crystal                        20.0         1.73           0.86
   LSE -100 ppm (cold, 2nd-order)           -100.0        -8.64          -4.32
   LSI set 32000, actual 17000           -468750.0    -40500.00      -20250.00
   LSI set 32000, actual 47000            468750.0     40500.00       20250.00
   LSI set 32000, actual 32500             15625.0      1350.00         675.00
```

F4 LSI 범위 17–47 kHz 는 데이터시트 대표값(확인 권장). 그 극단이면 하루에 ±11 시간이 틀린다. **캘린더용 RTC 는 LSE**, LSI 는 IWDG·대략적 웨이크업용이다.

### Part A — `rtc_set`: 세 관문과 INIT 핸드셰이크

`solution -v` 의 실제 접근 로그 (rtc_init 이 이미 DBP 를 켜 둔 상태):

```text
  rtc_set access log (21 entries):
    R PWR_CR     0x00000100
    W PWR_CR     0x00000100
    W RTC_WPR    0x000000CA
    W RTC_WPR    0x00000053
    R RTC_ISR    0x00000001
    W RTC_ISR    0xFFFFFFFF
    R RTC_ISR    0x00000081
    R RTC_ISR    0x00000081
    R RTC_ISR    0x000000C1
    W RTC_PRER   0x000000FF
    W RTC_PRER   0x007F00FF
    W RTC_TR     0x00134530
    W RTC_DR     0x00267111
    R RTC_CR     0x00000000
    W RTC_CR     0x00000000
    W RTC_ISR    0xFFFFFF7F
    R RTC_CR     0x00000000
    W RTC_ISR    0xFFFFFF5F
    R RTC_ISR    0x00000011
    R RTC_ISR    0x00000031
    W RTC_WPR    0x000000FF
```

| 단계 | 접근 | ISR 값 읽기 | 의미 |
|---|---|---|---|
| ① DBP | `PWR_CR \|= DBP` | — | 백업 도메인 쓰기 허용 (F4 는 먼저 `RCC_APB1ENR.PWREN` — 확인 권장) |
| ② WPR | `0xCA`, `0x53` 연속 | — | 사이에 다른 값이 끼면 다시 잠김 |
| ③ INIT | `ISR = 0xFFFFFFFF` | `0x81` → `0x81` → `0xC1` | INIT=1, ALRAWF=1 … 2 RTCCLK 뒤 INITF(0x40) |
| ④ PRER | `0x000000FF` 후 `0x007F00FF` | — | 동기 먼저, 비동기 다음 — 두 번에 나눠 쓴다 (RM 문구, 확인 권장) |
| ⑤ TR/DR | `0x00134530`, `0x00267111` | — | INITF=1 일 때만 받아들여짐. 요일 3 은 드라이버가 계산 |
| ⑥ FMT | `CR &= ~FMT` | — | 24시간제 |
| ⑦ exit | `ISR = ~INIT` (0xFFFFFF7F) | — | 4 RTCCLK 뒤 캘린더가 다시 센다 |
| ⑧ RSF | `ISR = ~(RSF\|INIT)` (0xFFFFFF5F) | `0x11` → `0x31` | INITS(0x10) 켜짐, 섀도 복사 완료(RSF 0x20) |
| ⑨ lock | `WPR = 0xFF` | — | 틀린 키 = 다시 잠금 |

⑧ 을 빼면 `rtc_set` 직후의 `rtc_get` 이 **설정 전 시각**을 돌려준다 (INIT 모드는 RSF 를 지우고 섀도 복사를 멈춘다). `B2` 가 그 시퀀스를 그대로 돌려 `hour == 9` (옛 값) → `rtc_resync_after_wake()` 후 `17:30` 을 확인한다. HAL 의 `HAL_RTC_SetTime` 도 BYPSHAD=0 이면 INIT 해제 뒤 `HAL_RTC_WaitForSynchro()` 를 부른다.

### Part A — `rtc_get` 과 서브초

```c
uint32_t ss = REG_READ(RTC_SSR);   /* TR·DR 잠김 */
uint32_t tr = REG_READ(RTC_TR);
uint32_t dr = REG_READ(RTC_DR);    /* 잠금 해제 */
```

SSR 은 PREDIV_S(255)부터 ck_apre(256 Hz)마다 1씩 내려가는 다운카운터다. ms = (255 − SS)·1000/256 내림 → 분해능 3.906 ms. `A5` 는 1초 경계 직후 위상에서 128 RTCCLK(= ck_apre 한 틱)씩 256 번 전진하며 ms = ⌊k·1000/256⌋ 를 전부 확인하고, 256 번째에 sec=1, ms=0 이 되는 것까지 본다.

### Part B — "에러 없이 무시되는" 쓰기들

`B1` 실측 항목: PWR 클럭이 꺼져 DBP 쓰기가 안 먹으면 WPR 키조차 무시 → INIT 도 무시 → INITF 가 영원히 안 와서 `rtc_set` 은 **64 회 폴링 뒤 `RTC_ETIMEOUT`** (시간은 그대로 2026). WPR 키 시퀀스 5종(모델 정의: 틀린 값 하나면 처음부터) — `{CA,53}` 풀림, `{53,CA}` 잠김, `{CA,00,53}` 잠김, `{CA,53,FF}` 잠김, `{CA,CA,53}` 풀림. INITF 를 안 기다린 쓰기 → `ign_init == 2` (TR, DR 둘 다 버려짐). RTCEN 안 켬 → RTCCLK 이 없어 INITF 불가 → 타임아웃. 실제 칩도 이 경우 버스 에러 없이 조용하다 — 그래서 **드라이버는 반드시 INITF 를 상한 있는 루프로 확인하고 에러를 올려야** 한다.

### Part B — 자정 찢어짐 (torn read)

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">2026-12-31 23:59:59.996 — 읽기 도중 RTCCLK 이 해를 넘긴다 (세로 점선)</text>
  <path d="M400 30 L400 250" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="408" y="42" fill="currentColor">2027-01-01 00:00:00</text>
  <text x="10" y="76" fill="currentColor" font-weight="bold">BYPSHAD=1</text>
  <text x="10" y="92" fill="currentColor">한 번 읽기</text>
  <rect x="180" y="62" width="70" height="30" rx="4" class="f-mut" stroke="currentColor"/><text x="194" y="82" fill="currentColor">SSR=0</text>
  <rect x="270" y="62" width="100" height="30" rx="4" class="f-mut" stroke="currentColor"/><text x="278" y="82" fill="currentColor">TR 23:59:59</text>
  <rect x="430" y="62" width="110" height="30" rx="4" class="f-warn" stroke="currentColor"/><text x="438" y="82" fill="currentColor">DR 2027-01-01</text>
  <text x="560" y="82" fill="currentColor">→ +1일 (k=2)</text>
  <text x="10" y="146" fill="currentColor" font-weight="bold">BYPSHAD=0</text>
  <text x="10" y="162" fill="currentColor">DR 먼저 (틀림)</text>
  <rect x="180" y="132" width="110" height="30" rx="4" class="f-mut" stroke="currentColor"/><text x="188" y="152" fill="currentColor">DR 12-31 해제</text>
  <rect x="430" y="132" width="100" height="30" rx="4" class="f-warn" stroke="currentColor"/><text x="438" y="152" fill="currentColor">TR 00:00:00</text>
  <text x="560" y="152" fill="currentColor">→ −1일 (k=1)</text>
  <text x="10" y="216" fill="currentColor" font-weight="bold">BYPSHAD=0</text>
  <text x="10" y="232" fill="currentColor">SSR→TR→DR</text>
  <rect x="180" y="202" width="90" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="188" y="222" fill="currentColor">SSR 잠금</text>
  <rect x="300" y="202" width="140" height="30" rx="4" class="f-acc" stroke="currentColor"/><text x="306" y="222" fill="currentColor">섀도 TR·DR 동결</text>
  <rect x="460" y="202" width="90" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="468" y="222" fill="currentColor">DR → 해제</text>
  <text x="560" y="222" fill="currentColor">→ 23:59:59.996 일관</text>
</svg>
```

`B3` 실제 출력 (값 = 결과 − 23:59:59.996 을 ms 로, `*` = 찢어짐, k = 몇 번째 접근 직후에 클럭 2 사이클을 넣었나):

```text
  tear sweep: 값 = 결과 − 23:59:59.996 (ms), * = 찢어짐
  BYPSHAD=1, SSR→TR→DR (rtc_get) k0:4 k1:*1000 k2:*86400000 k3:0
  BYPSHAD=0, DR→TR→SSR (bad order) k0:4 k1:*-86399996 k2:*-996 k3:0
  BYPSHAD=0, SSR→TR→DR (rtc_get) k0:4 k1:0 k2:0 k3:0
  BYPSHAD=1, read twice (bypass)     k0:4 k1:4 k2:4 k3:4 k4:4 k5:4 k6:0
```

읽는 법: 일관된 결과는 0(전부 옛 값 23:59:59.996) 또는 +4(전부 새 값 00:00:00.000) 둘뿐이다.

- **같은 `rtc_get` 코드라도 BYPSHAD=1 이면 틀린다.** k=1 (SSR 뒤) → 새 TR·DR + 옛 SSR = 00:00:00.996 (+1 s), k=2 (TR 뒤) → 옛 TR + 새 DR = **2027-01-01 23:59:59 (+1일, 연말이면 +1년처럼 보임)**.
- 섀도 모드에서 DR 을 먼저 읽으면 DR 읽기가 잠금을 풀어 버린 상태에서 복사가 일어나 TR 이 새 값 → **2026-12-31 00:00:00 (−1일)**. k=2 는 SSR 이 TR 보다 늦게 읽혀 −996 ms (서브초만 찢어짐 — SSR 은 아무도 잠그지 않는다).
- 올바른 순서 SSR→TR→DR 은 k 어디에 넣어도 0 이다: SSR 읽기가 TR·DR 섀도를 얼리고, DR 읽기가 푼다.
- 두 번 읽기는 6 접근 중 어디에 넣어도 비교가 어긋나 재시도하고, 결과는 +4(새 값)로 일관된다.

RM0090 의 규칙 원문 요지(확인 권장): BYPSHAD=0 일 때 "SSR 또는 TR 을 읽으면 상위 캘린더 섀도 레지스터가 DR 을 읽을 때까지 잠긴다", BYPSHAD=1 이면 "레지스터를 두 번 읽어 비교해야 한다". 섀도 읽기에는 APB 클럭이 RTCCLK 의 7 배 이상이어야 한다는 조건도 있다.

### Part B — 시간만 읽으면 시계가 멈춘다

`HAL_RTC_GetTime()` 은 SSR → TR 을 읽는다. 날짜를 안 읽으면 TR·DR 잠금이 그대로 남아 다음 `GetTime` 도 같은 값을 돌려준다 — HAL 소스 주석이 "GetTime 뒤에 반드시 GetDate 를 부르라" 고 적어 둔 이유다. `B4` 는 23:59:58 에서 시간만 읽고 3 s 를 흘린 뒤 다시 시간만 읽어 **여전히 23:59:58** 임을 보고, 이어지는 첫 정상 `rtc_get` 도 묵은 값(12-31 23:59:58)을 받은 뒤에야 잠금이 풀리며, 2 사이클 뒤 `2027-01-01 00:00:01 금` 이 되는 것을 확인한다.

### Part B — Stop 에서 깬 직후: RSF

Stop 동안 APB 클럭이 꺼져 섀도 복사가 멈춘다. RSF 는 잠들기 전의 1 이 남아 있어 "동기됐다" 고 거짓말을 한다. `B5`: 22:00:00 에서 10 s Stop → 바로 읽으면 22:00:00 (10 s 뒤처짐), `rtc_resync_after_wake()` (unlock → RSF 지우기 → RSF=1 대기 → lock) 후 22:00:10. RM 은 Stop/Standby 복귀 뒤 RSF 를 지우고 다시 설 때까지 기다리라고 적는다 (확인 권장). 그 대기는 최대 2 RTCCLK ≈ 61 µs.

### Part B — 알람 쓰기와 ISR 의 세 함정

`rtc_alarm_write`: ALRAE=0 → ALRAWF=1 폴링 → ALRMAR → 묵은 ALRAF 지우기 → ALRAE|ALRAIE. `B6` 은 ALRAE=1 인 채로 쓴 ALRMAR 가 `ign_alrm == 1` 로 버려지는 것과, 정상 경로의 로그 순서(CR 쓰기에 ALRAE=0 → ISR 읽기에 ALRAWF=1 → ALRMAR 쓰기)를 확인한다.

| ISR 코드 | 무엇이 일어나나 (`B7` 실측) | 고침 |
|---|---|---|
| `ISR = ~ALRAF;` (WPR 풀어 둔 펌웨어) | INIT 비트에 1 이 써져 초기화 모드 진입 → 이후 5 s 동안 `R.seconds` 증가 0, 캘린더 정지 | `ISR = (~(ALRAF\|INIT) & 0xFFFF) \| (ISR & INIT)` (HAL `__HAL_RTC_ALARM_CLEAR_FLAG` 꼴) |
| ALRAF 만 지우고 EXTI_PR 안 지움 | NVIC 라인이 계속 high → 같은 핸들러 재진입, 상한 50 에서 `g_storm` | `EXTI_PR = 1u << 17` (W1C, 그 비트만) |
| EXTI_PR 만 지우고 ALRAF 안 지움 | 첫날은 깸. 이튿날 비교기는 맞지만(`alarms == 3`) ALRAF 가 이미 1 이라 **0→1 에지가 없어** 이틀(172,800 s) 내내 못 깸 | ALRAF 를 rc_w0 로 지움 |

`ISR = ~ALRAF` 는 WPR 이 잠겨 있으면 INIT 쓰기가 무시돼 "우연히" 무해하다. 그래서 테스트 벤치에서는 멀쩡하고, 누가 편하려고 WPR 을 풀어 둔 빌드에서만 시계가 서는 버그가 된다.

### Part C — epoch 와 요일

```c
y -= (m <= 2) ? 1 : 0;                         /* 3월에 시작하는 해: 윤일이 해의 마지막 날 */
era = (y >= 0 ? y : y - 399) / 400;            /* 400년 = 146,097 일 주기 */
yoe = y - era * 400;                           /* [0, 399] */
doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;   /* 3월=0 기준 날짜, 153/5 가 30·31 패턴 */
doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
return era * 146097 + doe - 719468;            /* 0000-03-01 → 1970-01-01 이동 */
```

(H. Hinnant, *chrono-Compatible Low-Level Date Algorithms*.) 루프·표 없이 O(1) 이고, 3월 시작 해 덕분에 윤일 분기가 사라진다. 요일 = `(z mod 7 + 3) mod 7 + 1` (1970-01-01 = 목). `C1` 이 1970-01-01 부터 2100-12-31 까지 **47,847 일 전부**를 단순 일수 카운터와 대조한다 — 2000-02-29 는 있고(400 의 배수), 2100-02-29 는 없다(100 의 배수). `days_from_civil(2026,11,11) = 20768`, `(2100,3,1) = 47541` 은 파이썬 `date` 로 따로 확인한 값과 같다. epoch 는 `uint32_t` 로 2106 년까지 담기고, RTC 범위는 946684800(2000-01-01) … 4102444799(2099-12-31 23:59:59).

### Part C — 알람 마스크

| ALRMAR | 의미 | `C4` 실측 (구간 / 발생) |
|---|---|---|
| MSK4·3·2·1 | 매초 | 3600 s / 3600 |
| MSK4·3·2 + 초=30 | 매분 :30 | 3600 s / 60 |
| MSK4·3 + 분=15, 초=00 | 매시 15:00 | 1일 / 24 |
| MSK4 + 07:30:00 = `0x80073000` | 매일 07:30 | 2일 / 2 |
| WDSEL + DU=1(월) + 09:00:00 | 매주 월요일 09:00 | 14일 / 2 |
| 날짜 13 + 00:00:00 | 매달 13일 자정 | 20일 / 1 |

`alarm_at(t)` 는 일·시·분·초를 모두 비교하므로 28일 안의 미래에만 쓴다(월은 비교 대상이 아니다). `next_daily_epoch(now, 7,30,0)` 은 "오늘 07:30 이 now 보다 **엄격히** 뒤면 오늘, 아니면 내일" — 정확히 07:30:00 에 부르면 내일이다(알람은 캘린더가 그 초로 **바뀌는 순간** 맞는다).

### Part C — 센서 노드 로그

`solution` 실제 출력:

```text
  [2026-11-12 07:30:00.000 Thu] daily  wake #1  slept  33300 s  isr=1 alarms=1
  [2026-11-13 07:30:00.000 Fri] daily  wake #2  slept  86400 s  isr=2 alarms=2
  [2026-11-14 07:30:00.000 Sat] daily  wake #3  slept  86400 s  isr=3 alarms=3
PASS C5 daily 07:30 wake from Stop (12 checks)
  [2028-02-29 23:35:00.000 Tue] 15min  wake #1  slept    900 s  isr=1 alarms=1
  [2028-02-29 23:50:00.000 Tue] 15min  wake #2  slept    900 s  isr=2 alarms=2
  [2028-03-01 00:05:00.000 Wed] 15min  wake #3  slept    900 s  isr=3 alarms=3
  [2028-03-01 00:20:00.000 Wed] 15min  wake #4  slept    900 s  isr=4 alarms=4
  [2028-03-01 00:35:00.000 Wed] 15min  wake #5  slept    900 s  isr=5 alarms=5
  [2028-03-01 00:50:00.000 Wed] 15min  wake #6  slept    900 s  isr=6 alarms=6
  [2028-03-01 00:50:05.000 Wed] warm reset: INITS=1, BKP0R=0x32F2 → time kept
  [2028-02-29 23:20:00.000 Tue] cold boot : backup domain lost → time set to fallback
PASS C6 15-min wake across leap day + warm/cold boot (16 checks)
599/599 passed
```

- 첫 슬립 33,300 s = 22:15 → 07:30. 깰 때마다 `rtc_resync_after_wake()` 후 읽은 epoch 가 `next_daily_epoch` 예측과 **정확히** 같다. ISR 진입 수 = 웨이크 수(폭주 없음).
- 15분 주기는 매 웨이크마다 `epoch + 900` → `epoch_to_rtc` → `alarm_at` 으로 다시 쓴다. 2028-02-29 23:50 → 03-01 00:05 를 넘긴다.
- 웜 리셋: MCU 만 리셋(DBP=0, EXTI/NVIC 리셋) 후 5 s → 부트가 `INITS=1` 이고 `BKP0R == 0x32F2` 를 보고 **RTC 를 다시 설정하지 않는다** → 시각 연속(+5 s). 매 부트마다 `rtc_set` 을 부르면 INIT 동안 캘린더가 멈추고 서브초 위상이 리셋돼 리셋할 때마다 시계가 조금씩 늦는다.
- 콜드: 백업 도메인 리셋 → INITS=0 → 기본 시각(빌드 시각, GPS/NTP 전까지의 임시값)으로 설정.

### 호스트 모델 ↔ 실제 코드

| 이 레슨 | STM32 HAL / LL / 실제 | 비고 |
|---|---|---|
| `rtc_clock_init` | `HAL_PWR_EnableBkUpAccess` + `HAL_RCCEx_PeriphCLKConfig(RTC=LSE)` | LSE 기동 수백 ms–수 s (데이터시트 tSU(LSE)) |
| `rtc_unlock/lock` | `__HAL_RTC_WRITEPROTECTION_DISABLE/ENABLE` | |
| `rtc_enter_init` | `RTC_EnterInitMode` (`ISR = RTC_INIT_MASK` = 0xFFFFFFFF) | |
| `rtc_wait_sync` | `HAL_RTC_WaitForSynchro` | WPR 풀린 상태에서 호출 |
| `rtc_get` | `HAL_RTC_GetTime` + `HAL_RTC_GetDate` (이 순서, 둘 다) | |
| `RTC_Alarm_IRQHandler` | `HAL_RTC_AlarmIRQHandler` + `__HAL_RTC_ALARM_EXTI_CLEAR_FLAG` | EXTI 17, F4 IRQ 41 |
| `sim_stop_until_irq` | `HAL_PWR_EnterSTOPMode(..., PWR_STOPENTRY_WFI)` | 슬립 진입 레이스 = BM-M11 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-M12_so && /tmp/BM-M12_so          # 599/599 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/BM-M12_as && /tmp/BM-M12_as
cc -std=c11 -Wall -Wextra -Werror -O2 demo_prescaler_drift.c -o /tmp/BM-M12_demo && /tmp/BM-M12_demo
```

ASan/UBSan 빌드도 `599/599 passed`. 실행 시간 0.2–0.3 s (A3 의 전수 대조 10 × 4.2M 조합 포함).

## 7. 자주 하는 실수

1. **GetTime 만 부르고 GetDate 안 부름** → TR·DR 잠금이 남아 시계가 멈춰 보이고, 다음 정상 읽기 한 번은 묵은 값. 고침: SSR → TR → DR 을 한 함수에서 항상 같이.
2. **BYPSHAD=1 (또는 F1식 CNTH/CNTL) 에서 한 번 읽기** → 자정·자리올림에 ±1일 / ±65,536 s. 고침: 두 번 읽어 비교, 또는 hi-lo-hi (연습 4).
3. **`ISR = INIT` 으로 초기화 모드 진입** → 0 이 들어간 ALRAF 가 같이 지워진다. `ISR &= ~INIT` 은 RMW 라 읽기와 쓰기 사이에 HW 가 세운 플래그를 지운다(BM-B04 rc_w0 레이스). 고침: `0xFFFFFFFF` / `~INIT` 처럼 rc_w0 비트에는 1 을 쓴다.
4. **INIT=1 쓰고 INITF 를 안 기다림** → TR/DR 쓰기가 조용히 무시. 고침: 상한 있는 INITF 폴링 + 타임아웃 에러.
5. **매 부트마다 `rtc_set`** → VBAT 가 지켜 준 시각을 덮어쓰거나, 최소한 INIT 동안 캘린더를 멈춰 리셋마다 시계가 늦어진다. 고침: `INITS` + 백업 레지스터 매직으로 "이미 설정됨" 판정.
6. **Stop 복귀 직후 바로 읽기** → 자기 전 시각. 고침: RSF 지우고 재동기화 후 읽기.
7. **알람 ISR 에서 ALRAF 또는 EXTI_PR 중 하나만 지움** → 폭주 또는 다음 날 웨이크 실종. 고침: 둘 다, INIT 보존.
8. **ALRAE=1 인 채로 ALRMAR 수정** → 무시되어 옛 알람이 계속 울린다. 고침: ALRAE=0 → ALRAWF 대기 → 쓰기 → ALRAE=1.

## 8. 면접 Q&A

<details><summary>Q1. RTC 에서 읽은 날짜가 가끔 하루 틀립니다. 원인과 해결은?</summary>

시간과 날짜를 서로 다른 순간에 읽어 자정을 사이에 두고 섞인 것(torn read)이다. 섀도 레지스터가 있는 STM32 RTC(BYPSHAD=0)는 SSR 또는 TR 을 읽으면 상위 레지스터가 DR 을 읽을 때까지 잠기므로 **SSR → TR → DR 순서**로 읽으면 일관된다. DR 을 먼저 읽으면 −1일, 섀도를 끈(BYPSHAD=1) 상태에서 한 번만 읽으면 +1일이 이 레슨의 스윕에서 정확히 재현된다. 섀도가 없으면 세 레지스터를 두 번 읽어 같을 때만 쓴다. 꼬리: "그럼 BYPSHAD=1 은 왜 있나?" → 섀도 동기에 최대 2 RTCCLK(61 µs) 걸리고, Stop 복귀 직후 RSF 대기가 필요 없으며, APB ≥ 7×RTCCLK 조건이 안 될 때 쓴다. 대가는 두 번 읽기.
</details>

<details><summary>Q2. `HAL_RTC_GetTime()` 만 주기적으로 부르는데 시계가 멈춰 있습니다.</summary>

GetTime 이 SSR·TR 을 읽어 TR·DR 섀도를 잠그고, DR 을 읽지 않아 잠금이 영원히 남는다. 그 뒤 GetTime 은 계속 같은 스냅샷을 본다. 해결은 항상 `GetTime` 다음 `GetDate` (HAL 주석에 명시). 꼬리: "날짜가 필요 없으면?" → 그래도 DR 을 읽어 잠금을 푼다. 이 레슨의 `B4` 는 3 s 뒤에도 23:59:58 이 나오고, 첫 정상 읽기 한 번까지 묵은 값임을 보인다.
</details>

<details><summary>Q3. 왜 32.768 kHz 이고, PREDIV_A 는 왜 크게 잡나요?</summary>

32,768 = 2^15 라 15단 2분주로 정확히 1 Hz 가 나오고, 소리굽쇠형 크리스털이 작고 전류가 아주 작다. (A+1)(S+1) = 32768 인 조합 중 A 를 최대(127)로 잡으면 15비트 동기 카운터가 256 Hz 로만 돌아 전력이 줄어든다(RM 권고). 대가는 서브초 분해능 3.906 ms. 타임스탬프에 30 µs 가 필요하면 A=0 (ck_apre 32 kHz) 으로 전력을 내준다. 꼬리: "LSI 32 kHz 로는?" → 127/249 가 정확하지만 LSI 자체가 ±수십 % 라 하루 ±11 시간까지 틀릴 수 있다.
</details>

<details><summary>Q4. 리셋 후에도 시간이 유지되나요? 부트 코드는 어떻게 아나요?</summary>

RTC·LSE·백업 레지스터는 백업 도메인에 있어 VDD 가 꺼져도 VBAT 로 살고, 시스템 리셋에도 리셋되지 않는다(백업 도메인 리셋 = BDRST 나 VBAT 소실). 부트는 `ISR.INITS`(연도 ≠ 0)와 백업 레지스터의 매직 값(예: 0x32F2)을 보고, 둘 다 있으면 **RTC 를 다시 설정하지 않는다**. 매번 설정하면 VBAT 가 지킨 시각을 덮거나, INIT 중 캘린더가 멈춰 리셋마다 시계가 늦는다. 꼬리: "DBP 는?" → 리셋 뒤 DBP=0 이라 읽기는 되지만 쓰기는 다시 DBP 를 켜야 한다.
</details>

<details><summary>Q5. Stop 에서 RTC 알람으로 깼습니다. 바로 시각을 읽으면 무엇이 문제인가요?</summary>

Stop 동안 APB 가 꺼져 섀도 복사가 멈췄고, RSF 는 자기 전 1 이 그대로라 "동기됨" 으로 보인다. 그래서 첫 읽기는 자기 전 시각이다(`B5`: 10 s 뒤처짐). RSF 를 지우고(WPR 해제 필요) 다시 1 이 될 때까지 기다린 뒤 읽는다. 또 Stop 에서 깨면 SYSCLK 가 HSI 로 바뀌어 있어 PLL 을 다시 켜야 한다(BM-M11).
</details>

<details><summary>Q6. 알람 ISR 에서 무엇을, 어떤 순서로 지우나요?</summary>

① RTC 의 ALRAF: rc_w0 라 0 을 쓰되 INIT 비트는 현재 값을 유지(`(~(ALRAF|INIT) & 0xFFFF) | (ISR & INIT)`). `ISR = ~ALRAF` 는 WPR 이 풀려 있으면 INIT=1 로 캘린더를 세운다. ② EXTI_PR bit 17: W1C 로 그 비트만. 주변장치 플래그를 먼저 지우고 EXTI 를 지운다. 하나라도 빠지면 폭주(EXTI) 또는 다음 매치에 에지가 없어 웨이크 실종(ALRAF). 꼬리: "Standby 로 들어가기 전엔?" → ALRAF·WUF 등 웨이크 플래그를 먼저 지워야 즉시 깨지 않는다(RM 확인 권장).
</details>

<details><summary>Q7. 알람과 웨이크업 타이머(WUT)는 언제 무엇을 쓰나요?</summary>

알람은 **달력 시각**(매일 07:30, 매주 월요일, 특정 날짜)에, WUT 는 **주기**(10 ms…36 h)에 쓴다. WUT 는 RTC/16…/2 (분해능 61–488 µs, 최대 32 s) 또는 ck_spre(1 s 단위, 2^16 을 더하는 모드로 약 36 h)에서 센다(연습 3, WUCKSEL 인코딩은 RM 확인 권장). 15분 주기를 알람으로 하면 매번 ALRMAR 를 다시 써야 하지만(이 레슨 C6), WUT 는 한 번 설정으로 끝난다. F4 에서 WUT 는 EXTI 22, 알람은 EXTI 17.
</details>

<details><summary>Q8. RTC 에는 UTC 와 로컬 시각 중 무엇을 넣나요? 2자리 연도와 epoch 는?</summary>

UTC 를 넣고 표시할 때만 시간대·DST 를 적용한다(DST 전환 시 RTC 를 고치면 로그 시각이 겹치거나 빈다). 하드웨어 연도는 00–99 라 세기를 모른다 — 2099-12-31 다음은 "2000-01-01"(요일 비트는 금) 로 보인다(`C3`). epoch 변환은 표·루프 없는 `days_from_civil` 로 하고, `uint32_t` epoch 는 2106 년까지, 부호 있는 32비트 `time_t` 는 2038 년에 넘친다.
</details>

<details><summary>Q9. RTC 정확도를 올리려면?</summary>

LSE 크리스털 부하 커패시턴스를 맞추고(보드 문제가 가장 흔함), 온도 특성(포물선, 저온에서 −100 ppm 대)을 고려하고, 스무스 캘리브레이션 `RTC_CALR` 로 약 −487 … +488 ppm 범위를 0.95 ppm 분해능으로 보정한다(연습 2). 측정은 RTC 의 1 Hz/512 Hz 보정 출력(RTC_CALIB 핀, COSEL — 확인 권장)을 주파수 카운터로 재거나 GPS PPS 와 비교한다. 20 ppm 은 하루 1.73 s, 한 달 52 s 다.
</details>

## 9. 추가 연습

### 연습 1 — 12시간제 (FMT=1)

`tr_pack_12h(h24, m, s)` 를 작성하라. 0 시 → 12 AM, 12 시 → 12 PM, PM 비트는 [22]. 86,400 초 전부 왕복.

<details><summary>답 보기</summary>

`h12 = h24 % 12 == 0 ? 12 : h24 % 12`, `PM = h24 >= 12`. 되돌릴 때 `h24 = h12 % 12 + (PM ? 12 : 0)`. 예: 00:00:00 → `0x00120000`, 12:30:00 → `0x00523000`, 23:59:59 → `0x00515959`. 12시간제에서는 알람 ALRMAR 의 PM 비트[22]도 같이 맞춰야 한다 — 이 레슨 모델은 시 비교에 bits 22..16 을 함께 쓴다.
</details>

### 연습 2 — 스무스 캘리브레이션 고르기

f_cal = f · 2^20 / (2^20 + CALM − 512·CALP) (RM0090 식을 정리한 형태 — 확인 권장). 측정한 오차 x ppm(+ = 빠름)을 가장 잘 상쇄하는 (CALP, CALM) 을 1024 조합 전수로 고르고 남는 오차를 ppb 로.

<details><summary>답 보기</summary>

잔여 = ((10^6 + x)·2^20 − 10^6·D)/D ppm, D = 2^20 + CALM − 512·CALP. 교차곱으로 정확히 비교. 실측: x=+20 → (0, 21), 잔여 −27 ppb; x=−20 → (1, 491); x=+100 → (0, 105); x=+488 → (0, 511); x=−487 → (1, 1); 범위 밖 x=+600 → (0, 511) 포화, 잔여 +112.6 ppm. −480 … +480 ppm 전 구간에서 잔여 < 0.5 ppm. 값은 파이썬 `Fraction` 전수로 따로 계산해 맞췄다.
</details>

### 연습 3 — 웨이크업 타이머 설정

`wut_pick(period_us, 32768, &w)` — RTC/16·/8·/4·/2 중 가장 고운 정확한 분주(없으면 오차 최소, 동률이면 고운 쪽, 틱 1..65536), 32 s 넘으면 ck_spre(정수 초, 65,536 s 넘으면 +2^16 모드).

<details><summary>답 보기</summary>

1 s → RTC/2, WUTR = 16383 (정확). 10 ms → RTC/2, 164 틱 = 10.010 ms (/8, /4 도 같은 오차라 고운 /2 유지). 32 s → RTC/16, 65535. 60 s → ck_spre, 59. 36 h = 129,600 s → +2^16 모드, WUTR = 64063. 45.5 s 는 분주로는 65,536 틱 초과, ck_spre 로는 정수 초가 아니라 −1.
</details>

### 연습 4 — 섀도 없는 분할 카운터 (hi-lo-hi)

STM32F1 RTC 는 32비트 초 카운터를 CNTH/CNTL 두 16비트 레지스터로 보인다(RM0008, 확인 권장). 0x0001FFFF 에서 모든 접근 지점에 +1 을 주입해 naive(hi→lo) 의 찢어짐을 보이고, hi-lo-hi 로 고쳐라.

<details><summary>답 보기</summary>

naive 는 k=1 (hi 읽은 직후 자리올림)에서 `0x00010000` — 65,535 s 과거. hi-lo-hi: `h1, l, h2` 를 읽고 `h1 != h2` 면 `l` 을 다시 읽어 `(h2 << 16) | l`. 모든 k 와 자리올림 근처 20,000 개 무작위 시작값에서 일관. 조건: 두 읽기 사이에 하위 16비트가 한 바퀴(65,536 s) 돌지 않는다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-M12_pr && /tmp/BM-M12_pr      # 28/28 passed
```

## 10. 핵심 정리

- ck_spre = RTCCLK / ((PREDIV_A+1)(PREDIV_S+1)); 32.768 kHz → 127/255, A 최대 = 저전력, 대가는 SS 분해능 3.9 ms. 캘린더는 LSE, LSI 는 ±수십 %.
- 쓰기 세 관문: DBP → WPR 0xCA, 0x53 → INIT 후 INITF 대기. 빠지면 에러 없이 무시 → 상한 있는 폴링과 타임아웃.
- ISR 레지스터: rc_w0 플래그엔 1 을 쓴다 — 진입 `0xFFFFFFFF`, 해제 `~INIT`, RSF 지우기 `~(RSF|INIT)`, ALRAF 지우기는 INIT 보존.
- 읽기 순서 SSR → TR → DR (DR 이 잠금 해제). 시간만 읽으면 시계가 멈춘다. BYPSHAD=1 이면 두 번 읽기.
- 설정 직후·Stop 복귀 직후 RSF 재동기화. 섀도는 잠들기 전 값을 들고 있다.
- 알람: ALRAE=0 → ALRAWF → ALRMAR → ALRAE. ISR 은 ALRAF(rc_w0) + EXTI_PR 17(W1C) 둘 다.
- 부트: INITS + 백업 레지스터 매직이면 RTC 를 건드리지 않는다. RTC 에는 UTC, 2자리 연도의 세기는 소프트웨어, epoch 는 `days_from_civil`.

## 참고자료

- STMicroelectronics, *RM0090 Reference manual — STM32F405/415, 407/417, 427/437, 429/439*, Real-time clock (RTC) 장 (섀도 레지스터·BYPSHAD·INIT·알람·스무스 캘리브레이션), PWR 장 (DBP), RCC 장 (BDCR, 백업 도메인 리셋) — 이 레슨의 주소·비트는 여기서 확인할 것.
- STMicroelectronics, *AN3371 — Using the hardware real-time clock (RTC) in STM32 F0, F2, F3, F4 and L1 series MCUs* (프리스케일러 표, 캘리브레이션, 저전력 모드에서의 RTC).
- STMicroelectronics, STM32CubeF4 `stm32f4xx_hal_rtc.c` (`RTC_EnterInitMode`, `HAL_RTC_WaitForSynchro`, `HAL_RTC_GetTime` 의 "GetDate 를 함께 부르라" 주석, `__HAL_RTC_ALARM_CLEAR_FLAG`).
- STMicroelectronics, *STM32F405xx/407xx datasheet* — LSI 주파수 범위, LSE 기동 시간 tSU(LSE).
- Howard Hinnant, *chrono-Compatible Low-Level Date Algorithms* — http://howardhinnant.github.io/date_algorithms.html (`days_from_civil`, `civil_from_days`).
- 관련 레슨: BM-B02 (mock 프레임워크), BM-B04 (rc_w0 / W1C / 끼어들기 훅), BM-B13 (SysTick, wrap-safe 시간), BM-M11 (저전력 모드, 슬립 진입 레이스), C-M14 (ISR ↔ main 공유 데이터). 다음 단계: BM-M10 (워치독 + 리셋 원인), ARM-M10 (PRIMASK/BASEPRI 임계 구역), TEST-B09 (태스크 체크인).
