---
id: HWLAB-B01
time: 70분
updated: 2026-11-08
---
# HWLAB-B01 회로도 넷리스트 감사: 레일·풀업·테스트 포인트 (Netlist Audit)

## 학습 목표

- KiCad 넷리스트(`.net`, S-expression)를 **정규식 없이** 스택 파서로 읽고, 부품 값(`4k7`, `R47`, `100n`, `1m` vs `1M`)과 레일 이름(`+3V3`, `VDD_1V8`, `PP1V2_S0`)을 수치로 바꿀 수 있다.
- 저항 한 개를 "반대쪽 핀이 어디 붙었나"로 **풀업 / 풀다운 / 직렬 / DNP** 로 구분하고, I2C·RESET·strap·open-drain 넷이 실제로 유효한 풀 저항을 갖는지 판정한다 (병렬 풀업은 Rp_eff 와 UM10204 Rp_min 으로).
- 단일 노드 넷을 "라벨 오타(FAIL) / 플로팅 입력(WARN) / 의도된 NC(INFO)"로 나눠 경보 피로 없이 보고한다.
- 레일별 테스트 포인트(TP)·디커플링·부하를 뽑아 "TP 없음 → 어디를 프로브할지"까지 적은, 하드웨어 엔지니어가 바로 고칠 수 있는 리포트를 만든다.
- 결함을 심은 rev A 넷리스트에서 5개 결함이 정확히 검출되고 수정판 rev B 는 깨끗함을 테스트로 증명한다.

## 1. 문제

첫 보드에 전원을 넣기 **전에** 회로도에서 "어디를 재고, 무엇이 위험한가"를 뽑는 것이 bring-up 의 첫 작업이다 — Rivian/NVIDIA/Apple 보드·실리콘 검증 면접에서 "새 보드를 받으면 무엇부터 하나?"의 첫 답이 바로 이 레슨이다.

회로도 PDF 는 사람이 보는 그림이고, PCB 툴이 실제로 쓰는 연결 정보는 **넷리스트(netlist)** 다. 넷리스트는 "넷 이름 → 그 넷에 붙은 (부품, 핀) 목록"이므로, 사람이 200페이지 회로도를 눈으로 훑으며 놓치는 것들 — TP 가 없는 레일, DNP 로 빠진 풀업, 라벨 오타로 끊긴 신호 — 을 코드로 빠짐없이 찾을 수 있다. KiCad 의 ERC(Electrical Rules Check)는 "핀이 안 붙었다", "전원 입력을 아무도 안 구동한다" 같은 *전기 규칙* 은 보지만, "I2C 에는 풀업이 있어야 한다", "이 풀업은 DNP 다", "레일마다 TP 가 있어야 측정할 수 있다" 같은 **bring-up 규칙** 은 모른다. 그걸 채우는 게 이 감사 도구다.

이 레슨의 보드는 가상의 **HWB-01** 이다 (`gen_netlist.py` 가 KiCad 8 의 `(export (version "E") ...)` 형식을 흉내 내 생성 — 실제 KiCad export 가 아니다). 5V 입력 → 벅(U2) → `+3V3` → LDO 두 개(U3 → `VDD_1V8`, U4 → `VDD_1V2`), SoC(U1), I2C1(온도 센서 U5, EEPROM U6, 확장 헤더 J2), I2C2(1.8V 전용 IMU U7), SPI 플래시(U8).

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwb01-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">HWB-01 rev A — 전원 트리와 버스 (주황 = 심어 둔 결함 ①–⑤)</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="60" height="36" rx="5" class="f-mut"/>
    <rect x="110" y="40" width="80" height="36" rx="5" class="f-acc"/>
    <rect x="270" y="90" width="70" height="36" rx="5" class="f-acc"/>
    <rect x="270" y="160" width="70" height="36" rx="5" class="f-acc"/>
    <rect x="430" y="40" width="110" height="230" rx="8" class="f-mut"/>
    <rect x="600" y="40" width="72" height="28" rx="4" class="f-acc"/>
    <rect x="600" y="76" width="72" height="28" rx="4" class="f-acc"/>
    <rect x="600" y="112" width="72" height="24" rx="4" class="f-mut"/>
    <rect x="600" y="160" width="72" height="36" rx="4" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="40" y="63">J1 5V</text><text x="150" y="63">U2 buck</text>
    <text x="305" y="113">U3 LDO</text><text x="305" y="183">U4 LDO</text>
    <text x="485" y="150" font-weight="bold">U1 SoC</text>
    <text x="636" y="59">U5 TEMP</text><text x="636" y="95">U6 EEPROM</text><text x="636" y="129">J2 헤더</text>
    <text x="636" y="176">U7 IMU</text><text x="636" y="190" font-size="12">1.8V 전용</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#hwb01-arr)">
    <path d="M70 58 L108 58"/><path d="M190 58 L428 58"/>
    <path d="M250 58 L250 108 L268 108"/><path d="M250 108 L250 178 L268 178"/>
    <path d="M340 108 L428 108"/><path d="M340 178 L428 178"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="74" y="50">+5V</text><text x="76" y="92">TP1</text>
    <text x="200" y="50">+3V3 · TP2</text>
    <text x="346" y="100">VDD_1V8 TP3</text>
    <text x="350" y="170">VDD_1V2</text>
  </g>
  <rect x="345" y="184" width="76" height="22" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="383" y="199" fill="currentColor" font-size="12" text-anchor="middle">① TP 없음</text>
  <g stroke="currentColor">
    <path d="M540 60 L600 54"/><path d="M540 66 L600 90"/><path d="M540 72 L600 124"/>
    <path d="M540 178 L600 178"/>
    <path d="M540 230 L560 230"/><path d="M590 230 L636 230 L636 196"/>
    <path d="M300 245 L430 245"/><path d="M300 222 L430 222"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="545" y="92">I2C1</text><text x="680" y="140" font-size="12">R2‖R15</text>
    <text x="545" y="172">I2C2</text>
    <text x="10" y="226">/NRST: R5 10k↑ · C5 · SW1 · TP4</text>
  </g>
  <rect x="676" y="78" width="80" height="24" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="716" y="95" fill="currentColor" font-size="12" text-anchor="middle">⑤ WP 미연결</text>
  <rect x="540" y="138" width="56" height="20" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="568" y="152" fill="currentColor" font-size="12" text-anchor="middle">② R4 DNP</text>
  <rect x="470" y="282" width="190" height="22" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="565" y="297" fill="currentColor" font-size="12" text-anchor="middle">④ /IMU_INT ≠ /IMU_INT1 (오타)</text>
  <path d="M565 282 L575 236" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="560" y="226" fill="currentColor" font-size="12">✗</text>
  <rect x="10" y="236" width="288" height="22" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="18" y="251" fill="currentColor" font-size="12">③ /BOOT_SEL: TP5 만 있고 풀 저항 없음</text>
  <text x="10" y="322" fill="currentColor" font-size="12">rev B 는 ①–⑤ 를 고친 판: TP6 추가, R4 실장, R7 10k 풀다운, 라벨 통일, WP → GND, R15 제거.</text>
</svg>
```

넷리스트는 이렇게 생겼다 (`hwb01_revA.net` 에서 그대로 발췌):

```text
    (net (code "8") (name "/I2C2_SDA") (class "Default")
      (node (ref "R4") (pin "1") (pintype "passive"))
      (node (ref "U1") (pin "14") (pinfunction "I2C2_SDA") (pintype "bidirectional"))
      (node (ref "U7") (pin "5") (pinfunction "SDA") (pintype "bidirectional")))
    (net (code "9") (name "/IMU_INT") (class "Default")
      (node (ref "U1") (pin "15") (pinfunction "IMU_INT") (pintype "input")))
```

부품 쪽은 `(comp (ref "R4") (value "2k2") (footprint ...) (libsource (lib "Device") (part "R") ...) (property (name "dnp")) ...)`. 넷 이름 규칙: 루트 시트의 로컬 라벨은 `/I2C2_SDA` 처럼 경로가 붙고, 전원 심볼(power symbol)은 `+3V3`, `GND` 같은 전역 이름, 라벨 없는 와이어는 `Net-(U2-SW)` 같은 자동 이름, 아무 데도 안 붙은 핀은 `unconnected-(U1-PA7-Pad17)` 단일 노드 넷이 된다 (자동 이름의 정확한 형식은 KiCad 버전마다 조금 다르다).

rev A 에 심은 결함:

| # | 결함 | 넷리스트에서 보이는 모습 | 첫 보드에서 생기는 일 |
|---|---|---|---|
| ① | `VDD_1V2` 에 TP 없음 | 레일 넷에 `TP*` 노드가 없음 | 코어 전압을 0402 캡 패드에 프로브를 대고 재야 함 → 미끄러져 단락 위험, 측정 반복 불가 |
| ② | I2C2 SDA 풀업 R4 가 DNP | R4 는 연결돼 있지만 `(property (name "dnp"))` | SDA 가 플로팅 → IMU 가 영원히 NACK, "센서 드라이버 버그"로 오인 |
| ③ | `/BOOT_SEL` 스트랩 플로팅 | 넷에 U1 핀과 TP5 뿐 | 리셋마다 부트 모드가 달라짐 (온도·손가락에 따라 "가끔 안 뜬다") |
| ④ | INT 라벨 오타 | `/IMU_INT`, `/IMU_INT1` 각각 노드 1개 | 인터럽트가 영원히 안 옴, 폴링으로 바꾸면 "된다" → 원인 은폐 |
| ⑤ | EEPROM WP 입력 미연결 | `unconnected-(U6-WP-Pad7)` + `input` | 데이터시트에 내부 풀다운이 있으면 무해, 없으면 쓰기 보호가 랜덤 |

### Part A — 넷리스트 파싱과 수치화

- `parse_sexpr(text)` — S-expression → 중첩 리스트. 따옴표 안의 괄호(`"unconnected-(U1-PA7-Pad17)"`)·공백·`\"` 이스케이프를 처리하고, 괄호가 안 맞으면 `ValueError`. KiCad 5(따옴표 없는 `(version D)`)와 KiCad 6+(`"E"`) 모두 같은 결과.
- `parse_value(s)` — `"4k7"`→4700, `"R47"`→0.47, `"0R"`→0, `"1M"`→1e6, `"1m"`→1e-3, `"100n"`→1e-7, `"2u2"`→2.2e-6, `"10uF"`, `"4.7k 1%"`, `"2.2kΩ"`. `"DNP"`·빈 값 → `None`.
- `rail_voltage(name)` — `"+3V3"`→3.3, `"VDD_1V8"`→1.8, `"+3.3V"`→3.3, `"/pwr/PP1V2_S0"`→1.2, `"VBUS"`→`None`.
- (제공) `load_netlist(text)` → `{"comps": {ref: {...,"dnp"}}, "nets": {name: [node]}, "pins": {ref: {pin: net}}}`.

### Part B — 풀 저항 판정과 넷 분류 (순진한 방법이 거짓말하는 곳)

- `find_pulls(nl, net)` — 그 넷에 붙은 2핀 저항(`R\d+`)마다 **다른 쪽 핀의 넷**을 보고 `up`(레일) / `down`(GND) / `series`(다른 신호) / `dnp` 로 분류. DNP 는 풀로 치지 않는다.
- `categories(nl, net)` — 넷 이름과 붙은 핀 이름(`pinfunction`)을 **단어 단위**로 봐서 `i2c`/`reset`/`strap`/`od`(open-drain 출력이 있음). `SPI_SCLK` 는 I2C 가 아니고, 라벨 없는 `Net-(U7-SDA)` 도 핀 이름으로 I2C 로 잡혀야 한다.
- `classify_single(nl, net)` — 노드 1개 넷: 이름 있는 넷 → `FAIL`, `unconnected-*` 의 입력 핀 → `WARN`, 그 외 미연결 핀 → `INFO`.

### Part C — 감사 리포트 (rev A 결함 검출, rev B 통과)

- `audit_rails(nl)` — 레일마다 TP·소스(`power_out`)·디커플링 캡·부하(`power_in` IC). TP 가 없으면 `FAIL` + "가장 큰 디커플링 캡의 레일 쪽 핀을 프로브하라" 제안. GND 에도 TP(프로브 그라운드용)가 있어야 한다.
- (제공) `audit_signals` 판정 규칙: I2C·open-drain·active-low RESET 은 유효 풀업 필수, strap 은 정확히 한 방향, I2C 병렬 풀업은 Rp_eff < Rp_min 이면 FAIL.
- (제공) `audit(nl, waivers)` → `report()` → `exit_code()`: rev A 는 FAIL 5개·WARN 1개·exit 1, rev B 는 exit 0. waiver(예외 승인) 를 주면 그 넷은 `WAIVED`.

## 2. Problem (English)

> **Netlist audit for board bring-up.**
>
> You are given a KiCad netlist (`.net`, S-expression; version "E" with quoted atoms, or legacy version "D" without quotes).
> Using only the Python 3 standard library, write an audit tool that:
>
> 1. Parses the S-expression (quoted strings may contain parentheses, spaces and `\"`), converts component values in
>    RKM/SI notation (`4k7`, `R47`, `100n`, `1m` vs `1M`) to numbers, and extracts the voltage from rail names
>    (`+3V3` → 3.3, `VDD_1V8` → 1.8).
> 2. For every net, classifies each attached 2-pin resistor by the net on its *other* pin: pull-up (power rail),
>    pull-down (ground), series (another signal), or DNP (not populated — must not count).
>    Classifies nets as I2C / reset / strap / open-drain from net names **and** pin names, matching whole words
>    (`SPI_SCLK` is not I2C).
> 3. Reports: rails without a test point (and where to probe instead), I2C/open-drain/active-low reset nets without an
>    effective pull-up, floating or double-pulled straps, I2C pull-ups whose parallel value is below
>    Rp_min = (VDD − VOL)/IOL, and single-node nets (dangling label = FAIL, unconnected input pin = WARN,
>    other unconnected pin = INFO). Exit status 1 if any FAIL that is not waived.
>
> Example (rev A): `FAIL rail VDD_1V2 1.20V no test point (src U4; caps C4,C8; loads U1) -> probe C4.1 or add TP` and
> `FAIL i2c /I2C2_SDA DNP R4 2.2k->VDD_1V8 | needs pull-up, none effective`.

## 3. 시작 코드

<!-- code: starter.py -->

데이터는 생성기로 만든다 (결정적 출력 — 다시 돌려도 바이트 단위로 같다):

<!-- code: gen_netlist.py -->

```bash
cd lessons/HWLAB/HWLAB-B01
python3 -I gen_netlist.py      # hwb01_revA.net (22750 B), hwb01_revB.net (22920 B) 재생성
python3 -I starter.py          # TODO 상태: 4/64 passed, exit 1
```

실제 출력 (일부):

```text
-- 제공 함수 --
PASS  net_base('/sensors/I2C2_SDA') == 'I2C2_SDA'
PASS  is_gnd('GNDA') and not is_gnd('VDD_1V8')
PASS  parallel([4700, 4700]) == 2350
PASS  rp_min(3.3) = 967R, rp_min(1.8) = 720R
-- Part A: parse --
FAIL  parse_sexpr('(a (b "x y") c)')  (NotImplementedError: parse_sexpr)
FAIL  quoted parens: unconnected-(U1-PA7-Pad17)  (NotImplementedError: parse_sexpr)
...
FAIL  revA FAIL set == 5 planted  (TypeError: 'NoneType' object is not iterable)
...
4/64 passed
```

> TODO 7개: `parse_sexpr`, `parse_value`, `rail_voltage` (A) · `find_pulls`, `categories`, `classify_single` (B) · `audit_rails` (C). 순서대로 채우면 테스트가 위에서부터 초록색이 된다.

## 4. 힌트

<details>
<summary>힌트 1 — 파서는 상태 3개짜리 기계다</summary>

기본 상태에서 `(` 는 새 리스트를 현재 리스트에 붙이고 스택에 현재를 push, `)` 는 pop. `"` 를 만나면 **문자열 상태**로 들어가 다음 (이스케이프 안 된) `"` 까지 전부 한 원자다 — 그 안의 `(`, `)` 는 괄호가 아니다. 그 외 문자는 공백·괄호·따옴표가 나올 때까지 한 원자. 끝에서 스택이 비어 있고 최상위가 정확히 하나여야 한다.

</details>

<details>
<summary>힌트 2 — RKM 표기는 "배율 문자가 소수점 자리"</summary>

`4k7` = 4.7 × 10³, `R47` = 0.47, `2u2` = 2.2 × 10⁻⁶. 정규식 두 개: `^(\d*)([pnumRkKMG])(\d+)$` (RKM) 와 `^(\d+(\.\d+)?)([pnumRkKMG]?)$` (일반). `m`(밀리)과 `M`(메가)은 **대소문자를 구분**해야 한다 — 1mΩ 전류 션트를 1MΩ 으로 읽으면 전류 측정 계산이 10⁹ 배 틀린다.

</details>

<details>
<summary>힌트 3 — 풀업 판정은 "반대쪽 핀"</summary>

`nl["pins"][ref]` 가 `{pin: net}` 이다. 지금 보고 있는 핀이 아닌 쪽 넷이 레일이면 풀업, GND 면 풀다운, 다른 신호면 직렬(댐핑) 저항이다. 그리고 `comps[ref]["dnp"]` 를 **먼저** 본다.

</details>

<details>
<summary>힌트 4 — 단어로 쪼개라</summary>

`"SPI_SCLK"` 에 `"SCL"` 이 부분 문자열로 들어 있다. `re.split(r"[^A-Z0-9]+", s.upper())` 로 단어를 만들고 `"SCL" in words` 로 본다. KiCad 오버바 표기 `~{CS}` 는 `~{`, `}` 를 지우고 쪼갠다. 넷 이름이 자동 이름(`Net-(...)`)이어도 붙은 핀 이름(`pinfunction`)이 `SDA` 면 I2C 다.

</details>

## 5. 풀이

### Part A — S-expression 은 정규식으로 읽으면 안 된다

KiCad 6 이후의 넷리스트·회로도(`.kicad_sch`)·PCB(`.kicad_pcb`) 파일은 모두 S-expression 이다. 문법은 단순하다: 리스트 `( ... )`, 원자(atom) — 따옴표 없는 토큰 또는 `"..."` 문자열. 문제는 **문자열 안에 괄호가 들어간다** 는 것이다. `unconnected-(U1-PA7-Pad17)`, `Net-(U2-SW)` 가 그렇다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwb01-sm" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">parse_sexpr 의 상태 기계 (스택 = 열린 리스트들)</text>
  <g stroke="currentColor">
    <rect x="250" y="80" width="150" height="56" rx="26" class="f-acc"/>
    <rect x="30" y="80" width="130" height="56" rx="26" class="f-mut"/>
    <rect x="500" y="80" width="130" height="56" rx="26" class="f-ok"/>
    <rect x="500" y="180" width="130" height="44" rx="20" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="325" y="105" font-weight="bold">기본</text><text x="325" y="123" font-size="12">공백 skip</text>
    <text x="95" y="105" font-weight="bold">ATOM</text><text x="95" y="123" font-size="12">공백·( ) " 전까지</text>
    <text x="565" y="105" font-weight="bold">STRING</text><text x="565" y="123" font-size="12">( ) 도 그냥 글자</text>
    <text x="565" y="207" font-weight="bold">ESC</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#hwb01-sm)">
    <path d="M250 98 L162 98"/><path d="M162 120 L250 120"/>
    <path d="M400 98 L498 98"/><path d="M498 120 L400 120"/>
    <path d="M590 136 L590 178"/><path d="M540 180 L540 138"/>
    <path d="M300 80 C 280 40, 350 40, 340 78"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="172" y="92">그 외 문자</text><text x="172" y="138">끝 → push</text>
    <text x="420" y="92">"</text><text x="420" y="138">" → push</text>
    <text x="596" y="160">\</text><text x="470" y="160">다음 1글자</text>
    <text x="360" y="44">( → 새 리스트 push · ) → pop</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="172">입력: (name "unconnected-(U1-PA7-Pad17)")</text>
    <text x="10" y="194">파서: ["name", "unconnected-(U1-PA7-Pad17)"]</text>
    <text x="10" y="216">정규식 \(name "?([^")]+) → "unconnected-(U1-PA7-Pad17"</text>
  </g>
  <text x="10" y="240" fill="currentColor" font-size="12">정규식은 따옴표 안팎을 구분 못 한다 — 상태(문자열 안인가?)가 필요하다.</text>
</svg>
```

`demo_naive.py` 로 실제로 확인한 결과 (rev A, 26개 넷 중 3개가 잘림):

```text
① net names: naive regex vs S-expression parser
   naive found 26 names, parser 26; mangled: ['Net-(U2-SW', 'unconnected-(U1-PA7-Pad17', 'unconnected-(U6-WP-Pad7']
```

개수는 맞는데 이름이 틀린다 — 잘린 이름은 어떤 규칙에도 안 걸려 "문제 없음"으로 조용히 통과하므로 가장 위험한 실패다.

**부품 값 파싱.** 회로도의 값 필드는 사람이 쓴 문자열이다. 실제로 섞여 나오는 표기:

| 표기 | 의미 | 규칙 |
|---|---|---|
| `4k7`, `4K7`, `2u2`, `R47` | 4.7kΩ, 2.2µF, 0.47Ω | RKM 코드(IEC 60062): 배율 문자가 소수점 자리. `R` 은 ×1 |
| `4.7k`, `100n`, `33R`, `0R` | 4.7kΩ, 100nF, 33Ω, 0Ω(점퍼) | 숫자 + 배율 |
| `1m` vs `1M` | 1mΩ 션트 vs 1MΩ | **대소문자 구분** — `K` 만 관용적으로 k 와 같게 |
| `10uF`, `2.2kΩ`, `4.7k 1%` | 단위·공차 꼬리 | 첫 토큰만, 단위(Ω/F) 제거 |
| `DNP`, 빈 값 | 미실장 / 미입력 | `None` (숫자로 우기지 않는다) |

**레일 이름 → 전압.** 회사마다 규칙이 다르지만 대부분 `<정수>V<소수>` 를 넣는다: KiCad 전원 심볼 `+3V3`, `+1V8`, 설계 관례 `VDD_1V8`, `VCC_3V3`, 그리고 `PP3V3_S0` 처럼 전원 상태(power state)까지 넣는 방식도 있다. `VBUS`, `VBAT`, `VSYS` 처럼 숫자가 없는 레일은 전압을 모른다 → `None` 으로 두고 판정에서 빼는 게 정직하다 (5.0 으로 가정하지 않는다). 소수점을 `V` 로 쓰는 이유는 넷 이름에 `.` 을 쓰기 싫어서다 (`3.3V` 도 받는다).

### Part B — 저항 하나의 정체는 "반대쪽"이 결정한다

```svg
<svg viewBox="0 0 740 272" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">find_pulls(nl, "/I2C2_SDA") — 반대쪽 핀의 넷으로 분류</text>
  <path d="M60 130 L700 130" stroke="currentColor" stroke-width="3"/>
  <text x="60" y="152" fill="currentColor" font-family="monospace" font-size="12">/I2C2_SDA (U1.14, U7.5)</text>
  <g stroke="currentColor">
    <path d="M160 130 L160 104"/><rect x="150" y="64" width="20" height="40" class="f-ok"/><path d="M160 64 L160 44"/>
    <path d="M330 130 L330 160"/><rect x="320" y="160" width="20" height="40" class="f-acc"/><path d="M330 200 L330 222"/>
    <path d="M310 222 L350 222"/>
    <path d="M500 130 L500 104"/><rect x="490" y="64" width="20" height="40" class="f-warn" stroke-dasharray="4 3"/>
    <path d="M500 64 L500 44"/><path d="M480 70 L520 98"/><path d="M520 70 L480 98"/>
    <path d="M620 130 L620 160"/><rect x="610" y="160" width="20" height="40" class="f-mut"/><path d="M620 200 L620 222 L700 222"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="130" y="38">VDD_1V8 (레일)</text><text x="178" y="88">up</text>
    <text x="300" y="240">GND</text><text x="348" y="184">down</text>
    <text x="470" y="38">VDD_1V8</text><text x="518" y="88">dnp → 무시</text>
    <text x="640" y="214">/SDA_J (신호)</text><text x="638" y="184">series</text>
  </g>
  <text x="10" y="264" fill="currentColor" font-size="12">rev A 의 R4 는 세 번째 경우: 넷리스트엔 연결, 보드엔 없음. "저항이 붙어 있나?"만 보면 OK 로 나온다.</text>
</svg>
```

`find_pulls` 는 넷 하나에 붙은 저항을 전부 보며 반대쪽 넷으로 분류한다. 핵심 함정 세 가지:

1. **DNP(Do Not Populate)** — 회로도에는 그려 두고 조립에서 빼는 부품. 옵션 풀업, 0Ω 점퍼, 디버그용 직렬 저항을 이렇게 관리한다. 넷리스트의 연결만 보면 "풀업 있음"이다. 이 레슨의 생성기는 KiCad 8 넷리스트가 DNP 속성을 내보내는 형태로 알려진 `(property (name "dnp"))` 를 흉내 냈고, 회사 관례인 값 필드 `DNP`/`DNI`/`DNF` 도 함께 인식한다. **실제 KiCad 버전의 export 에서 DNP 가 어떻게 나오는지는 직접 확인하라** — 확실하지 않은 포맷은 테스트용 작은 회로로 export 해 보는 게 정답이다. 어셈블리 변형(variant)이 있는 회사는 BOM 변형 파일이 진짜 소스다.
2. **직렬 저항** — `R10 33R` 은 `/UART_TX` 와 `/UART_TX_J` 사이에 있다. 양쪽 다 신호이므로 풀이 아니다. "저항이 붙어 있으면 풀업" 으로 짜면 이걸 풀업으로 센다.
3. **병렬 풀업** — rev A 의 `/I2C1_SDA` 에는 R2(보드)와 R15(확장 헤더 옆) 4.7k 가 둘 다 있다. 그 자체는 결함이 아니다 — Rp_eff = 4.7k‖4.7k = 2.35k 를 **수치로** 판정한다. I2C 의 최소 풀업은 출력 Low 전압 규격에서 나온다 (NXP UM10204, Standard/Fast-mode):

   - VDD > 2V: Rp(min) = (VDD − VOL(max)) / IOL = (3.3 − 0.4) / 3mA ≈ **967Ω**
   - VDD ≤ 2V (Fast-mode): VOL = 0.2·VDD @ 2mA → Rp(min) = 0.8·1.8 / 2mA = **720Ω**

   Rp_eff 가 이보다 작으면 가장 약한 디바이스가 SDA 를 VOL 아래로 못 끌어내려 Low 가 Low 로 안 읽힌다. 모듈마다 4.7k 를 달아 다섯 개가 병렬이 되면 940Ω < 967Ω → FAIL. 상한(rise time 과 버스 커패시턴스)은 HWLAB-B03 에서 다룬다. 이 감사 도구는 그 계산의 **입력(Rp_eff, 풀업 레일 전압)** 을 넷리스트에서 정확히 뽑아 주는 역할이다.

**넷 분류는 단어 단위로, 핀 이름까지.** 이름 부분 문자열로 I2C 를 고르면 `/SPI_SCLK` 가 걸린다 (demo ②). SPI 클럭은 push-pull 이라 풀업이 필요 없는데 "I2C 풀업 누락" FAIL 이 나온다 — 거짓 FAIL 이 몇 번 반복되면 팀은 리포트를 안 읽는다.

```text
② I2C by substring 'SCL'/'SDA' in net name
   naive: ['/I2C1_SCL', '/I2C1_SDA', '/I2C2_SCL', '/I2C2_SDA', '/SPI_SCLK']
   words: ['/I2C1_SCL', '/I2C1_SDA', '/I2C2_SCL', '/I2C2_SDA']
   -> naive would FAIL /SPI_SCLK 'missing I2C pull-up' (pulls up=[]) - it is SPI, push-pull
③ pull-up check ignoring DNP on /I2C2_SDA
   naive: pull-up present ['R4'] -> OK
   real : {'up': [], 'down': [], 'series': [], 'dnp': [('R4', 2200.0, 'VDD_1V8')]}
```

반대 방향의 누락도 있다: 설계자가 라벨을 안 달아 넷이 `Net-(U7-SDA)` 같은 자동 이름이면 이름 규칙으로는 못 잡는다. 그래서 `pinfunction`(IC 핀 이름: `SDA`, `I2C2_SDA`, `NRST`, `BOOT0`)도 같은 단어 규칙에 넣는다. open-drain 은 이름이 아니라 **핀 타입**(`open_collector`)으로 잡는다 — `PG`(power-good), `ALERT`, `INT` 같은 출력은 이름이 제각각이지만 핀 타입은 라이브러리가 알려준다 (라이브러리 심볼의 핀 타입이 정확할 때만 — 이것도 리뷰 대상이다).

판정 규칙 (`audit_signals`, 제공):

| 분류 | 필요 조건 | 없으면 | 비고 |
|---|---|---|---|
| `i2c` | 유효 풀업 ≥ 1 | FAIL | Rp_eff < Rp_min → FAIL, 병렬이면 INFO(Rp_eff 표시), 풀다운 있으면 WARN |
| `od` | 유효 풀업 ≥ 1 | FAIL | PG, ALERT 등 open-drain 출력 |
| `reset` (active-low: `NRST`, `_N`, `~{RESET}`) | 풀업 | FAIL | RC(C5)·버튼(SW1)·TP 는 별개 |
| `strap` | 풀업 XOR 풀다운 | 둘 다 없음 FAIL, 둘 다 WARN | 저항 분압으로 아날로그 strap 을 쓰는 칩(PMIC, PHY)도 있어 "둘 다"는 WARN |

**단일 노드 넷 — 하나로 묶지 마라.** 노드가 하나뿐인 넷은 세 종류다. 전부 FAIL 로 찍으면 의도된 NC 핀까지 FAIL 이 되어 진짜 결함(라벨 오타)이 묻힌다 (demo ④).

```text
④ every 1-node net = FAIL ?
   /IMU_INT                     naive FAIL   real FAIL
   /IMU_INT1                    naive FAIL   real FAIL
   unconnected-(U1-PA7-Pad17)   naive FAIL   real INFO
   unconnected-(U6-WP-Pad7)     naive FAIL   real WARN
```

| 단일 노드 넷 | 판정 | 이유 |
|---|---|---|
| 이름 있는 넷 (`/IMU_INT`) | FAIL | 라벨을 붙였는데 아무도 같은 라벨이 없음 = 오타 또는 시트 간 연결 누락 |
| `unconnected-*` + `input` 핀 (최근 KiCad 는 `input+no_connect` 로 내보냄 → `split("+")[0]`) | WARN | CMOS 입력이 떠 있으면 중간 전압에서 입력단 양쪽 트랜지스터가 반쯤 켜져 전류가 늘고, 노이즈로 토글한다. 내부 풀이 있는지 데이터시트 확인 (24C02 계열 EEPROM 은 WP 에 내부 풀다운이 있는 제품이 많지만 제품마다 확인) |
| `unconnected-*` + 그 외 (`bidirectional` 등) | INFO | MCU GPIO 는 리셋 후 보통 입력이므로 펌웨어가 출력/아날로그로 설정하거나 내부 풀을 켜야 한다 — 펌웨어 체크리스트 항목 |

원인 → 증상 → 그걸 드러내는 측정 (첫 보드에서 이 감사를 안 했을 때):

| 원인 | 증상 | 드러내는 측정 |
|---|---|---|
| I2C 풀업 DNP/누락 | 모든 트랜잭션 NACK, 또는 SDA 가 0.5~1V 근처에서 떠 있음 | 스코프로 SDA/SCL 유휴 레벨 (풀업 있으면 VDD), LA 디코드에서 주소 바이트 후 NACK |
| strap 플로팅 | 부트 모드가 리셋마다 다름, 손 대면 바뀜 | 리셋 해제 엣지를 트리거로 strap 핀 레벨 캡처, 부트 모드 상태 레지스터 읽기 |
| 라벨 오타로 INT 끊김 | 인터럽트 미발생, 폴링하면 동작 | IMU INT1 핀(센서 쪽)에서 펄스 확인 + SoC 핀에서는 없음 → 두 지점 비교 |
| TP 없는 레일 | 측정이 느리고 위험, 결과가 사람마다 다름 | — (측정 자체가 어려워짐; 다음 rev 에 TP 추가) |
| 플로팅 입력 | 대기 전류 증가, 간헐 오동작 | 전류계로 보드 대기 전류, 의심 핀에 손가락/프로브 대면 전류 변화 |

### Part C — 리포트: 하드웨어 엔지니어가 읽고 바로 고칠 수 있게

`audit_rails` 는 레일마다 TP·소스·캡·부하를 모은다. TP 가 없는 레일에는 "대신 어디를 재라"를 붙인다 — 가장 큰 디커플링 캡의 레일 쪽 패드. 다만 0402 패드에 프로브 팁을 세우는 건 미끄러지기 쉽고 옆 GND 패드와 단락시킬 수 있어, 리포트의 진짜 조치는 "다음 rev 에 TP 추가"다. TP 는 레귤레이터 출력이 아니라 **부하(SoC) 가까이** 두는 게 좋다 — 레일 경로의 IR drop 까지 포함한 실제 전압을 봐야 하니까. GND TP 는 프로브 그라운드 스프링/짧은 리드를 꽂을 곳이다 (긴 그라운드 리드가 만드는 링잉은 HWLAB-B05).

rev A 실제 리포트:

```text
$ python3 -I solution.py --audit hwb01_revA.net; echo "exit=$?"
== HWB-01 bring-up board rev A - netlist audit (43 comps, 26 nets) ==
FAIL   rail           VDD_1V2                     1.20V no test point (src U4; caps C4,C8; loads U1) -> probe C4.1 or add TP
FAIL   strap          /BOOT_SEL                   no pull | floating strap: boot mode undefined at reset
FAIL   i2c            /I2C2_SDA                   DNP R4 2.2k->VDD_1V8 | needs pull-up, none effective
FAIL   1-node         /IMU_INT                    only 1 node U1.15(IMU_INT): dangling label/wire (typo or missing connection)
FAIL   1-node         /IMU_INT1                   only 1 node U7.6(INT1): dangling label/wire (typo or missing connection)
WARN   1-node         unconnected-(U6-WP-Pad7)    floating input U6.7(WP): CMOS input must not float - tie per datasheet
INFO   i2c            /I2C1_SDA                   up R2 4.7k->+3V3; up R15 4.7k->+3V3 | 2 pull-ups in parallel -> Rp_eff 2.35k (check rise time, HWLAB-B03)
INFO   1-node         unconnected-(U1-PA7-Pad17)  unconnected U1.17(PA7) [bidirectional]: OK if intended (FW: set output/analog or pull)
OK     rail           +3V3                        3.30V tp=TP2 (src -; caps C2,C6,C7,C9,C10,C12; loads U1,U3,U4,U5,U6,U8)
OK     rail           +5V                         5.00V tp=TP1 (src -; caps C1; loads U2)
OK     rail           GND                         ground tp=TP7,TP8
OK     rail           VDD_1V8                     1.80V tp=TP3 (src U3; caps C3,C11; loads U1,U7)
OK     strap          /BOOT0                      down R6 10k->GND
OK     i2c            /I2C1_SCL                   up R1 4.7k->+3V3
OK     i2c            /I2C2_SCL                   up R3 2.2k->VDD_1V8
OK     reset          /NRST                       up R5 10k->+3V3
OK     od             /PG_3V3                     up R9 100k->+3V3
OK     od             /TEMP_ALERT                 up R8 10k->+3V3
summary: 5 FAIL, 1 WARN, 0 WAIVED, 2 INFO, 10 OK -> exit 1
exit=1
```

읽는 법:

- `+3V3` 의 `src -` — 벅(U2)의 출력은 `SW` 핀(`output`)에서 인덕터 L1 을 거쳐 나오고, `+3V3` 에는 `FB` 입력만 붙는다. 그래서 `power_out` 핀이 없다. LDO(U3, U4)는 `VOUT` 이 `power_out` 이라 소스가 보인다. 소스 칸은 판정이 아니라 정보다 — 하지만 레일에 소스가 전혀 없고 이름도 모르는 레일이면 ERC 의 "power input not driven" 경고와 같은 신호다.
- `/I2C1_SDA` INFO — 2.35k 는 3.3V 에서 Rp_min 967Ω 보다 크므로 FAIL 이 아니다. 하지만 SCL 은 4.7k 인데 SDA 만 2.35k 라는 **비대칭** 은 "누군가 확장 헤더용 풀업을 SDA 에만 달았다"는 냄새다. 리뷰어가 보고 판단할 수 있게 수치를 보여 주는 게 목적이다.

**waiver(예외 승인).** SoC 데이터시트에 "BOOT_SEL 은 리셋 중 내부 풀다운 활성"이라고 적혀 있으면 외부 풀다운이 필요 없을 수 있다 (내부 풀은 수십 kΩ 급이라 노이즈에 약하니 "리셋 중 활성" 근거가 핵심). 그때는 도구를 고치지 말고 근거와 함께 waiver 를 남긴다 — 리포트에 `WAIVED` 로 남아 다음 리뷰어가 근거를 볼 수 있다:

```text
$ python3 -I solution.py --audit hwb01_revA.net --waive "/BOOT_SEL=SoC internal pull-down (DS 7.3)" | grep -E "BOOT_SEL|summary"
WAIVED strap          /BOOT_SEL                   no pull | floating strap: boot mode undefined at reset [waived: SoC internal pull-down (DS 7.3)]
summary: 4 FAIL, 1 WARN, 1 WAIVED, 2 INFO, 10 OK -> exit 1
```

수정판 rev B:

```text
$ python3 -I solution.py --audit hwb01_revB.net | tail -3
OK     od             /PG_3V3                     up R9 100k->+3V3
OK     od             /TEMP_ALERT                 up R8 10k->+3V3
summary: 0 FAIL, 0 WARN, 0 WAIVED, 1 INFO, 14 OK -> exit 0
$ python3 -I solution.py --audit hwb01_revB.net > /dev/null; echo "exit=$?"
exit=0
```

**장비/도구가 있을 때 (이 맥에는 KiCad 가 없어 실행하지 않음):** KiCad 7 이후의 `kicad-cli` 로 회로도에서 넷리스트를 뽑아 같은 도구에 넣는다.

```bash
kicad-cli sch export netlist --format kicadsexpr --output board.net board.kicad_sch   # KiCad 7+
kicad-cli sch erc --output board_erc.rpt board.kicad_sch                               # KiCad 8+ (ERC 리포트)
python3 -I solution.py --audit board.net
```

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py                          # 64/64 passed, exit 0
python3 -I solution.py --audit hwb01_revA.net   # FAIL 5, WARN 1 → exit 1
python3 -I demo_naive.py                        # 순진한 방법 4가지가 어떻게 틀리는지
```

<!-- code: demo_naive.py -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `re.findall(r'\(name "?([^")]+)', text)` 로 넷 이름 추출 | 따옴표 안의 `)` 에서 잘림: `unconnected-(U1-PA7-Pad17` | 상태(문자열 안/밖)를 가진 스택 파서 |
| "넷에 저항이 붙어 있으면 풀업" | 직렬 저항(R10), 풀다운, DNP 를 풀업으로 셈 | 반대쪽 핀 넷이 레일인지 + `dnp` 먼저 확인 |
| DNP 를 무시 | rev A 의 R4: 회로도엔 있고 보드엔 없음 → 결함을 놓침 | `property dnp` / 값 `DNP` 를 `dnp` 목록으로 |
| `"SCL" in name` | `SPI_SCLK` 가 I2C 로 분류 → 거짓 FAIL | 단어 분리 후 `"SCL" in words` |
| 넷 이름만으로 분류 | 라벨 없는 `Net-(U7-SDA)` 를 놓침 | `pinfunction` 도 같은 규칙에 |
| `float("1m".replace("m","e-3"))` 처럼 대소문자 무시 | 1mΩ 션트가 1MΩ(또는 반대) | 배율 표를 대소문자 구분, `K` 만 별칭 |
| `VBUS` 를 5.0V 로 가정 | 근거 없는 수치가 Rp_min 같은 판정에 들어감 | 전압 모르면 `None` 으로 두고 그 판정은 건너뜀 |
| 단일 노드 넷 전부 FAIL | 의도된 NC 까지 FAIL → 경보 피로, 진짜 결함이 묻힘 | 이름 있음 FAIL / 미연결 입력 WARN / 나머지 INFO |
| 병렬 풀업 = 무조건 FAIL (또는 무시) | 2.35k 는 합법, 940Ω 은 위반 — 판정은 숫자로 | Rp_eff vs Rp_min(VDD) |
| 데이터시트 근거로 예외를 도구 코드에 하드코딩 | 다음 보드에서 같은 예외가 숨은 채 재사용 | `--waive NET=근거` → 리포트에 `WAIVED` 로 남김 |

## 8. 면접 Q&A

<details>
<summary>Q1. 새 보드를 받았습니다. 전원 넣기 전에 회로도·넷리스트에서 무엇을 확인하나요?</summary>

전원 트리(입력 → 레귤레이터 → 레일)와 각 레일의 목표 전압·허용 오차·TP 위치, 레일 간 단락 체크 지점(HWLAB-B09), 리셋·부트 strap 의 풀 저항과 값, I2C/open-drain 풀업과 그 레일 전압, DNP 부품 목록, 디버그 접근(JTAG/SWD/UART)의 연결. 이 레슨처럼 넷리스트에서 자동으로 뽑아 "측정 계획표"(연습 4)로 만들고, 프로브 그라운드를 꽂을 GND TP 까지 확인한다.
꼬리질문: "TP 가 없는 레일은 어떻게 재나?" → 디커플링 캡 패드나 인덕터 패드를 프로브하되 미끄러짐·단락 위험이 있으니 납땜한 와이어/핀을 쓰고, 다음 rev 에 TP 를 넣도록 리포트한다. 리플 측정이면 패드에 납땜한 짧은 동축이나 스프링 그라운드가 필요하다 (HWLAB-B08).

</details>

<details>
<summary>Q2. I2C 풀업이 빠지면 어떤 증상이 보이고, 스코프로 무엇을 보면 확인되나요?</summary>

I2C 는 open-drain 이라 아무도 High 를 구동하지 않는다. 풀업이 없으면 SDA/SCL 이 떠서 유휴 레벨이 VDD 가 아니고(수백 mV ~ 불확정), 마스터가 START 를 못 만들거나 모든 주소에 NACK. 스코프로 유휴 레벨이 VDD 인지, 상승 에지가 RC 곡선인지(풀업이 있으면 τ = Rp·Cbus 의 지수 곡선, 하강은 빠름) 본다.
꼬리질문: "MCU 내부 풀업을 켜면 되지 않나?" → 내부 풀업은 보통 수십 kΩ(예: 30~50kΩ 급)이라 100pF 버스에서 상승 시간이 수 µs 로 늘어 400kHz(tr ≤ 300ns) 를 못 맞춘다. 100kHz 에서 짧은 버스면 동작할 수도 있지만 마진이 없다 (계산은 HWLAB-B03).

</details>

<details>
<summary>Q3. 부트 strap 핀이 플로팅이면 무슨 일이 생기고, strap 값은 어떻게 검증하나요?</summary>

SoC 는 리셋 해제 순간(또는 POR 중 특정 시점)에 strap 핀 레벨을 래치해 부트 소스·전압 모드 등을 정한다. 플로팅이면 그 순간의 누설·커플링에 따라 값이 바뀌어 "가끔 부팅 안 됨", "손 대면 됨" 같은 간헐 문제가 된다. 검증: 리셋 해제 에지로 트리거해 strap 핀 레벨을 스코프로 캡처(래치 시점에 VIH/VIL 을 넉넉히 넘는가), 부트 후 SoC 의 부트 모드 상태 레지스터를 읽어 기대값과 비교.
꼬리질문: "strap 핀이 평소엔 GPIO 로도 쓰이면?" → strap 저항이 그 GPIO 를 구동하는 외부 회로와 싸우지 않게 값(보통 수 kΩ~10kΩ 대)을 고르고, 외부 회로가 리셋 중에 그 핀을 구동하지 않는지(전원 시퀀스) 확인한다.

</details>

<details>
<summary>Q4. 1.8V 전용 센서의 I2C 를 3.3V 로 풀업하면 어떻게 되나요? 넷리스트로 어떻게 잡나요?</summary>

센서 입력 핀의 ESD 다이오드(핀 → VDD) 를 통해 3.3V 가 센서의 1.8V 레일로 흘러 들어간다(back-powering). 절대 최대 정격 위반, 센서가 꺼져 있어야 할 때 반쯤 켜짐, 1.8V 레일 전압 상승. 넷리스트에서는 풀업 레일 전압과 그 넷에 붙은 IC 들의 전원 레일 전압을 비교하면 잡힌다 (연습 1). 레벨 정의·노이즈 마진·역전원 분석은 HWLAB-B02.
꼬리질문: "U1 처럼 3.3V 와 1.8V 를 다 가진 SoC 는?" → IC 단위 최대 전원으로는 못 잡는다. 핀별 IO 도메인(어느 VDDIO 뱅크인지)이 필요하고, 그건 데이터시트의 핀-도메인 표에서 와야 한다.

</details>

<details>
<summary>Q5. KiCad ERC 를 통과했는데 왜 따로 감사 스크립트가 필요한가요?</summary>

ERC 는 전기 규칙 검사다: 출력끼리 충돌, 미연결 핀, 전원 입력 미구동, 라벨 하나만 있는 넷 등. 하지만 "I2C 에 풀업이 있어야 한다", "그 풀업은 DNP 다", "레일마다 TP 가 있어야 한다", "Rp_eff 가 Rp_min 보다 크다" 는 설계 의도·bring-up 규칙이라 ERC 가 모른다. 또 ERC 경고는 설계 중에 무시/억제되는 경우가 많아, 결과를 bring-up 관점에서 분류(FAIL/WARN/INFO)하고 waiver 로 근거를 남기는 흐름이 따로 필요하다.
꼬리질문: "이걸 CI 에 넣는다면?" → 회로도가 바뀔 때마다 `kicad-cli` 로 넷리스트 export → 감사 → 이전 rev 리포트와 diff, 새 FAIL 이 생기면 리뷰 차단.

</details>

<details>
<summary>Q6. 레일 TP 는 어디에 두는 게 좋고, GND TP 는 왜 따로 필요한가요?</summary>

레일 TP 는 부하(SoC 전원 핀) 근처 — 레귤레이터 출력에서 재면 PCB 경로의 IR drop 과 부하 쪽 과도 응답을 못 본다. 리모트 센스가 있는 레귤레이터면 센스 지점 근처. GND TP 는 각 레일 TP 가까이에 두어 프로브 그라운드를 짧게(스프링) 연결할 수 있게 한다. 15cm 그라운드 리드는 수십~수백 MHz 에서 링잉을 만들어 리플·오버슈트를 과장한다 (HWLAB-B05, B08).
꼬리질문: "TP 가 너무 많으면?" → 고속 신호에 TP 를 달면 스텁이 생겨 신호 품질이 나빠진다. 고속 차동 신호는 TP 대신 커넥터/인터포저나 링크 자체의 진단(eye/margining)으로 본다.

</details>

## 9. 추가 연습

### 연습 1 — 풀업 레일 vs IC 전원 도메인 (역전원 위험)

`domain_mismatch(nl)`: 풀업이 있는 넷마다, 풀업 레일 전압이 그 넷에 붙은 IC 의 가장 높은 전원 레일 전압보다 높으면 `(net, ic, rail, v_pull, v_ic_max)`. rev B 는 `[]`, R3 을 `+3V3` 으로 옮긴 변형은 `[("/I2C2_SCL", "U7", "+3V3", 3.3, 1.8)]`.

<details>
<summary>답 보기</summary>

IC 의 전원 = 그 IC 핀 중 레일 넷에 붙은 것들의 전압 최대값. U7 은 `VDD_1V8` 뿐이라 1.8V, 3.3V 풀업은 ESD 다이오드로 역전원된다. U1 은 3.3V 전원도 있어서 이 단순 규칙으로는 안 걸린다 — 실제로는 I2C2 핀이 VDDIO2(1.8V) 뱅크에 있으므로 위험하다. 핀별 도메인 표가 필요한 이유이고, 정식 분석은 HWLAB-B02.

</details>

### 연습 2 — rev A → rev B 넷리스트 diff

`diff_netlists(a, b)`: 부품 추가/삭제·값·DNP 변화, 넷 추가/삭제, 넷의 노드 증감. 실제 결과:

```text
DNP R4 yes -> no
+COMP R7 10k
-COMP R15 4k7
+COMP TP6 TP_1V2
NET +3V3 -R15.2
NET /BOOT_SEL +R7.1
NET /I2C1_SDA -R15.1
NET /IMU_INT +U7.6
-NET /IMU_INT1
NET GND +R7.2 +U6.7
NET VDD_1V2 +TP6.1
-NET unconnected-(U6-WP-Pad7)
```

<details>
<summary>답 보기</summary>

부품은 ref, 넷은 이름으로 매칭하고 노드는 `"ref.pin"` 집합 차이로 본다. 이 목록이 곧 "rev A 감사 결과가 rev B 에서 고쳐졌나"의 증거다: ①→`+COMP TP6`, ②→`DNP R4 yes -> no`, ③→`+COMP R7`, ④→`-NET /IMU_INT1`, ⑤→`GND +U6.7`. 한계: 넷 이름이 바뀌면(라벨 수정) 삭제+추가로 보인다 — 노드 집합 유사도로 이름 변경을 추정하는 게 다음 단계.

</details>

### 연습 3 — 라벨 오타 후보 짝 찾기

`split_net_candidates(nl)`: `unconnected-*` 를 뺀 단일 노드 넷들 중 이름(경로 제외)의 편집 거리(Levenshtein)가 2 이하인 쌍. rev A → `[("/IMU_INT", "/IMU_INT1")]`, rev B → `[]`.

<details>
<summary>답 보기</summary>

동적 계획법 한 줄씩(`prev`/`cur`)으로 편집 거리를 구한다. 단일 노드 넷 두 개가 이름이 거의 같으면 "원래 하나였어야 할 넷"일 확률이 높다 — 리포트에 "similar: /IMU_INT1" 힌트를 붙이면 고치는 사람이 바로 찾는다. 거리 임계값을 너무 크게 하면 `/SPI_CS` 와 `/SPI_CK` 같은 정상 쌍이 걸린다 (단일 노드인 경우만 보니 위험은 작다).

</details>

### 연습 4 — 첫 전원 인가 측정 순서표

`bringup_plan(nl)`: CSV. 1행 GND TP(프로브 그라운드), 그다음 레일을 전압 내림차순(상류 → 하류)으로 `TP, 기대 범위(±5%)`, TP 없으면 감사의 프로브 제안, 마지막에 active-low 리셋의 "해제 후 ≥ 0.7·VDD". rev A 실제 결과:

```text
step,what,where,expect
1,GND reference,TP7,0 V (ground spring here)
2,+5V,TP1,4.750..5.250 V
3,+3V3,TP2,3.135..3.465 V
4,VDD_1V8,TP3,1.710..1.890 V
5,VDD_1V2,C4.1 (no TP),1.140..1.260 V
6,/NRST,TP4,high >= 0.7*3.30 = 2.31 V after release
```

<details>
<summary>답 보기</summary>

±5% 와 0.7·VDD 는 자리 표시용 기본값이다 — 실제 기대 범위는 레귤레이터 출력 정확도(데이터시트)와 SoC 의 레일 허용 범위 중 좁은 쪽, 리셋 High 는 SoC 의 NRST VIH 규격에서 가져와야 한다. 상류부터 재는 이유: `+3V3` 이 없으면 하류 LDO 출력도 없으므로, 첫 실패 지점이 원인 위치다. 실제 bring-up 에서는 이 표를 PyVISA 로 DMM/전원 공급기를 제어하는 스크립트의 입력으로 쓴다 (전원 인가·전류 제한·inrush 는 HWLAB-B09).

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py      # 11/11 passed, exit 0
```

## 10. 핵심 정리

- 넷리스트 = 연결의 진실, BOM(변형) = 실장의 진실. 감사는 둘을 합쳐서 한다 (DNP!).
- KiCad `.net` 은 S-expression: 문자열 안에 괄호가 있으므로 정규식 말고 상태 + 스택 파서.
- 값: RKM(`4k7`, `R47`, `2u2`), `m`≠`M`. 레일: `<n>V<m>` → 전압, 모르면 `None`.
- 저항의 정체는 반대쪽 넷: 레일=풀업, GND=풀다운, 신호=직렬, DNP=없음.
- 분류는 단어 단위 + 핀 이름(`pinfunction`) + 핀 타입(`open_collector`). `SCLK` ≠ `SCL`.
- I2C 병렬 풀업은 숫자로: Rp_min = (VDD−0.4V)/3mA ≈ 967Ω @3.3V (UM10204), 상한은 rise time (HWLAB-B03).
- 단일 노드 넷: 이름 있음 FAIL / 미연결 입력 WARN / 기타 INFO — 경보 피로를 막아야 리포트가 읽힌다.
- 레일마다 부하 근처 TP + 가까운 GND TP. 없으면 "어디를 대신 재라"와 "다음 rev 에 추가"를 리포트에.

## 참고자료

- [KiCad 문서 — Schematic Editor: netlist export, ERC](https://docs.kicad.org/) · [kicad-cli 레퍼런스](https://docs.kicad.org/8.0/en/cli/cli.html)
- [KiCad 개발자 문서 — S-expression 파일 포맷](https://dev-docs.kicad.org/en/file-formats/)
- [NXP UM10204 I2C-bus specification and user manual](https://www.nxp.com/docs/en/user-guide/UM10204.pdf) — §7.1 풀업 저항 크기 (Rp(min)/Rp(max)), VOL/IOL 표
- IEC 60062 — 저항·커패시터 값 표기 (RKM 코드)
- The Art of Electronics, 3rd ed. (Horowitz, Hill) — CMOS 입력 플로팅, open-drain 출력
- High-Speed Digital Design (Howard Johnson, Martin Graham) — 프로브 그라운드와 측정 지점
- [PyVISA documentation](https://pyvisa.readthedocs.io/) — 측정 순서표를 계측기 제어로 잇기
- 관련 레슨: HWLAB-B02 (로직 레벨·역전원), HWLAB-B03 (I2C 풀업 창·rise time), HWLAB-B05 (프로브 그라운드 링잉), HWLAB-B08 (레일 리플), HWLAB-B09 (첫 전원 인가)
