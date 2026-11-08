---
id: SIV-M04
time: 95분
updated: 2026-11-08
---
# SIV-M04 PCIe 링크 검증 플로우

## 학습 목표

- `lspci -xxx` 덤프를 손과 코드로 읽는다: 0x34 → capability 체인 → PCIe cap(ID 0x10) → **Link Capabilities(+0x0C) / Link Status(+0x12)** → "Gen4 x4 가능, 지금은 Gen1 x4" 를 숫자로 말한다.
- 체인 파서를 **반드시 끝나게** 만든다: 루프 · 헤더 안을 가리키는 포인터 · 하위 2비트 · 잘린 64바이트 덤프 · all-ones(장치 없음) 를 각각 다른 이름으로 보고한다.
- 판정을 **다운그레이드(링크 문제)** 와 **능력 미달(부품/strap 문제)** 로 나눠, 디버그를 엉뚱한 팀에 보내지 않는다.
- 벤더 LTSSM 디버그 레지스터를 가짜 시계로 샘플링해 링크업(Detect → Polling → Configuration → L0 → Recovery 로 속도 올리기)을 감시하고, **샘플링이 놓치는 짧은 Recovery** 를 카운터로 잡는다.
- 루트 포트에서 Retrain Link 를 N 회 반복하는 스트레스를 짜고, 간헐 폭 저하 · 속도 고착 · Recovery 고착 · LBMS 누락(스펙 위반) 을 **iteration 번호와 시그니처**로 보고한다.

## 1. 문제

PCIe 는 first silicon 브링업에서 가장 먼저 "붙었다/안 붙었다" 로 싸우는 인터페이스이고, Rivian HM 스크린에서 실제로 나온 질문도 "lspci 로 무엇을 보고, LTSSM 이 L0 까지 어떻게 가는가" 였다 — 이 레슨은 그 대답을 **코드로 된 검증 플로우**로 만든다.

검증 엔지니어가 받는 보고는 보통 이렇다: "NVMe SSD 가 인식은 되는데 벤치가 느려요." 원인 후보는 많지만 첫 질문은 하나다 — **링크가 기대 속도·폭으로 붙었나?** 그 답은 config space 에 있다. 하지만 표준 config space 는 "지금 상태" 한 장만 보여주고, 링크가 어떻게 올라왔는지(몇 번 재시작했는지, 중간에 Recovery 가 몇 번 났는지)는 벤더 디버그 레지스터를 시간에 따라 봐야 안다. 그리고 "한 번 붙었다" 는 증거가 못 된다 — retrain 을 수백 번 걸어도 매번 같은 속도·폭으로 돌아오는지가 보드/실리콘 검증의 기준이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m04a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">dump_gen4x4_good.txt 의 capability 체인 (256B config space, type 0 헤더)</text>
  <g stroke="currentColor" stroke-width="1.2">
    <rect x="10" y="40" width="130" height="70" rx="6" class="f-mut"/>
    <rect x="170" y="40" width="110" height="70" rx="6" class="f-mut"/>
    <rect x="310" y="40" width="110" height="70" rx="6" class="f-mut"/>
    <rect x="450" y="40" width="130" height="70" rx="6" class="f-acc"/>
    <rect x="610" y="40" width="120" height="70" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="75" y="60">헤더 0x00~0x3F</text>
    <text x="75" y="80" font-size="12" font-family="monospace">06: 10 00 (CapList)</text>
    <text x="75" y="98" font-size="12" font-family="monospace">34: 40</text>
    <text x="225" y="60">0x40 PM</text><text x="225" y="80" font-size="12" font-family="monospace">01 50</text>
    <text x="365" y="60">0x50 MSI</text><text x="365" y="80" font-size="12" font-family="monospace">05 70</text>
    <text x="515" y="60">0x70 PCIe</text><text x="515" y="80" font-size="12" font-family="monospace">10 b0</text>
    <text x="670" y="60">0xB0 MSI-X</text><text x="670" y="80" font-size="12" font-family="monospace">11 00 → 끝</text>
    <text x="225" y="98" font-size="12">[ID, next]</text><text x="365" y="98" font-size="12">[ID, next]</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m04a)">
    <path d="M140 75 L168 75"/><path d="M280 75 L308 75"/><path d="M420 75 L448 75"/><path d="M580 75 L608 75"/>
    <path d="M515 110 L515 140"/>
  </g>
  <rect x="200" y="142" width="530" height="128" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="212" y="162" font-weight="bold">PCI Express Capability (0x70 기준 오프셋)</text>
    <text x="212" y="182" font-family="monospace">+0x02 CapReg   0x0002  ver=2 type[7:4]=0 Endpoint</text>
    <text x="212" y="200" font-family="monospace">+0x0C LinkCap  0x00430844  [3:0]=4 16GT/s  [9:4]=4 x4</text>
    <text x="212" y="218" font-family="monospace">+0x12 LinkSta  0x1044  [3:0]=4 cur speed  [9:4]=4 cur width</text>
    <text x="212" y="236" font-family="monospace">+0x30 LinkCtl2 0x0004  [3:0]=4 Target Link Speed</text>
    <text x="212" y="258">LinkSta 비트: [11] LT  [12] SCC  [13] DLLLA  [14] LBMS(W1C)  [15] LABS(W1C)</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="162">포인터 규칙</text>
    <text x="10" y="182">· 하위 2비트 마스크(&amp;0xFC)</text>
    <text x="10" y="200">· 0 = 체인 끝</text>
    <text x="10" y="218">· &lt; 0x40 은 불법</text>
    <text x="10" y="236">· 방문한 곳 = 루프</text>
    <text x="10" y="254">· len 밖 = 잘린 덤프</text>
  </g>
  <text x="10" y="300" fill="currentColor">dump_cap_loop.txt: 0x50 의 next 가 0x40 → PM ↔ MSI 무한 루프, PCIe cap 에 영영 못 간다.</text>
  <text x="10" y="320" fill="currentColor">dump_gen1x4_downgrade.txt: 0x82 가 41 10 → LinkSta 0x1041 = 2.5 GT/s x4 (cap 은 그대로 Gen4 x4).</text>
</svg>
```

**테스트 환경 (주어진 코드).**

| 구성 | 내용 | 실제 현장 대응 |
|---|---|---|
| `dump_gen4x4_good.txt` | 가상의 NVMe SSD 컨트롤러(1234:5a04, class 0108 prog-if 02) Gen4 x4 정상 | `sudo lspci -xxx -s 01:00.0` |
| `dump_gen1x4_downgrade.txt` | 같은 장치, LinkSta = 2.5 GT/s x4, LinkSta2 EQ 비트 0 | "벤치가 느려요" 보드 |
| `dump_cap_loop.txt` | MSI next → 0x40 (체인 루프) | 깨진 config ROM/strap, 잘못 만든 FPGA EP |
| 루트 포트 mock (`g_sim`) | 256B config space(PCIe cap 0x60, Root Port) + 벤더 레지스터 `0x104 LTSSM[5:0]`, `0x108 RCVRY_CNT` | DesignWare 류 컨트롤러의 DBI + 디버그 레지스터 |
| 가짜 시계 | 레지스터 읽기 1회 = 1 µs, `delay_us` 는 시계만 전진, 시작값 `0xFFFFF000` (곧 wrap) | 하드웨어 타이머 (SIV-B09) |
| 스크립트 `seg_t {dur_us, ltssm, speed, width}` | 시간에 따라 LTSSM 상태가 바뀜. RL 쓰기는 Recovery 스크립트를 새로 시작 | 실제 LTSSM |
| `rp_fault_t` | 간헐 폭 저하(확률, 고정 시드 xorshift32) · N 번째부터 Gen3 고착 · N 번째에서 Recovery 고착 · LBMS 미설정 | 레인 불량, EQ 마진 부족, IP 버그 |

덤프는 위 표의 가상 장치로 **합성**한 것이다 (실제 바이트 레이아웃은 PCI/PCIe 스펙 그대로). 데이터 파일을 읽으므로 **레슨 디렉터리에서 실행**한다 (저장소 루트에서 실행해도 `lessons/SIV/SIV-M04/` 경로를 한 번 더 시도한다).

### Part A — 덤프 파싱 · capability 체인 · Link Cap/Status 판정

1. `parse_lspci_dump(text, &cfg)` — `"xx: hh hh ... (16개)"` 데이터 줄만 받아 바이트 배열로. 장치 이름 줄(`01:00.0 Non-Volatile...`)·빈 줄은 건너뛰고, offset 불연속 · 16개 미만 · 잘못된 hex · 뒤 쓰레기는 `-1`. 반환값 = 바이트 수(64/256/4096).
2. `cap_walk(&cfg, &w)` — 0x34 부터 `[ID, next]` 를 따라가며 `(off, id)` 목록. 정상 덤프 → `PM@0x40, MSI@0x50, PCIe@0x70, MSI-X@0xB0`.
3. `decode_link(&cfg, 0x70, &li)` — port type, max/cur speed·width, target speed, LT/DLLLA/LBMS/LABS.
4. `link_verdict(&li, {gen, width})` — `OK / SPEED_DOWN / WIDTH_DOWN / CAP_BELOW / INCONSISTENT / LINK_DOWN / BAD_ENC` (비트 OR). 10개짜리 판정 표 테스트.

### Part B — 방어적 체인 걷기 + LTSSM 링크업 모니터

1. 체인 결함 표: 하위 2비트가 선 포인터(0x43 → 0x40 으로 정상 처리), Status[4]=0, 헤더 안(0x20, 0x3C)을 가리키는 포인터, 자기 자신을 가리키는 cap, **비루트 lspci 의 64바이트 덤프**, all-ones, 최장 48개 체인과 그 끝이 처음으로 돌아가는 루프 — 각각 `status / bad / from / n` 까지 맞아야 한다.
2. `ltssm_monitor(ops, cap, &spec, &r)` — `interval_us` 마다 LTSSM 을 읽어 trace 를 남기고, 목표 속도의 L0 에 도달하면 `observe_us` 동안 안정성을 본다. 결과: `OK / TIMEOUT / BAD_STATE / LINK_DOWN / RETRY / UNSTABLE`.
3. 주입: 정상 Gen4 링크업 · 8 GT/s EQ 실패로 Gen1 에 머묾 · 3번 재시작 후 성공 · 알 수 없는 코드 0x2A · **샘플 사이에 숨은 6 µs Recovery** · 링크업 후 Detect 로 추락.

### Part C — 링크업 → 스냅샷 판정 → Retrain 스트레스 리포트

1. `retrain_once()` — LT==0 확인 → LBMS/LABS W1C 클리어 → Link Control RMW 로 Retrain Link → LT==0 대기(1 s) → `DLL_DOWN > SPEED_DOWN > WIDTH_DOWN > NO_LBMS` 순 판정.
2. `retrain_stress()` — N=200 회, 시그니처별 `count/first/last/예시`, 실패 iteration 목록, Recovery 고착이면 중단. `stress_report()` 가 PERSISTENT / INTERMITTENT 를 붙여 출력.
3. 결함 5종: 정상(200/200) · 간헐 x4→x2 (검출 목록 == 주입 목록, 그리고 **순진한 구현은 0개 검출**) · 57번째부터 Gen3 고착 · 33번째에서 RCVRY_LOCK 고착 · LBMS 미설정.

## 2. Problem (English)

> **PCIe link validation flow.**
>
> 1. Parse an `lspci -xxx` style hex dump (`"%02x: " + 16 bytes` per line, other lines ignored) into a config-space byte array. Walk the capability list starting at offset 0x34 (mask the low 2 bits; stop at 0) and find the PCI Express capability (ID 0x10). Decode Link Capabilities (+0x0C: max speed [3:0], max width [9:4]) and Link Status (+0x12: current speed [3:0], negotiated width [9:4], LT bit 11, DLLLA bit 13, LBMS bit 14). Compare against an expected `{gen, width}` and classify: `SPEED_DOWN`, `WIDTH_DOWN` (below what the device could do), `CAP_BELOW` (the device itself cannot meet the expectation), `INCONSISTENT`, `LINK_DOWN`, `BAD_ENC`.
> 2. The walker must terminate on any input: loops, pointers into the header (< 0x40), truncated dumps (a non-root `lspci` only shows 64 bytes), and absent devices (all-ones). Report *where* the chain broke (`bad` pointer and the `from` location that held it).
> 3. Given bus ops for a root port (`cfg_read16/cfg_write16`, a vendor `LTSSM[5:0]` debug register, a vendor recovery counter, `now_us/delay_us`), write a sampling LTSSM monitor that waits for L0 at the target speed, counts training restarts and speed changes, then observes the link for a window and flags any recovery — including ones too short for the sampler to see.
> 4. Write a retrain stress: on the root port, set Retrain Link N times, wait for Link Training to clear, and verify speed, width, DLLLA and LBMS each time. Report failures by signature with first/last iteration and whether they are persistent or intermittent; abort on a stuck link.
>
> Example report: `WIDTH_DOWN count=4 first=60 last=144 INTERMITTENT e.g. lnksta=0x7024 Gen4 x2 ltssm=L0 rcvry+=1`

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/SIV/SIV-M04          # dump_*.txt 를 읽으므로 레슨 디렉터리에서
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/SIV-M04_st && /tmp/SIV-M04_st
```

실제로 돌린 결과 (앞·뒤만):

```text
-- Part A --
FAIL starter.c:529  load_dump("dump_gen4x4_good.txt", &good) == 256
FAIL starter.c:530  good.b[0] == 0x34 && good.b[1] == 0x12 && good.b[0xFF] == 0x00 && cfg16(&good, 0x82) == 0x1044
...
FAIL starter.c:696  st.pass == 0 && st.sig[RT_NO_LBMS].count == N && st.sig[RT_NO_LBMS].first == 1 && st.sig[RT_NO_LBMS].ex.rcvry_delta == 1 && sig_persistent(&st, RT_NO_LBMS)
RETRAIN_STRESS port=RP0(no-lbms) exp=Gen4 x4 iters=0/200 pass=0 fail=0
4/62 passed
```

(통과하는 4개는 "파서가 `-1` 을 돌려줘야 하는" 테스트라 스텁 반환값과 우연히 맞는다. exit 1.) TODO 는 7개: A1 `parse_lspci_dump`, A2 `cap_walk`, A3 `decode_link`, A4 `link_verdict`, B1 `ltssm_monitor`, C1 `retrain_once`, C2 `retrain_stress`. mock · 스크립트 · 리포트 함수 · `wait_lnksta`(SIV-B09 축약) 는 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — 데이터 줄 판별</summary>

줄 맨 앞의 hex 숫자를 최대 4자리까지 읽는다. 2~3자리이고 바로 뒤가 `": "` 이면 데이터 줄. `01:00.0 ...` 은 `':'` 뒤가 `'0'` 이라서, `0000:01:00.0` 은 4자리라서 자연히 걸러진다. offset 이 지금까지 모은 길이와 다르면 줄이 빠진 것이다.

</details>

<details>
<summary>힌트 2 — 루프 방지는 "방문 표시"</summary>

cap 은 dword 정렬이고 0x40~0xFC 에만 있을 수 있으니 dword 는 최대 48개. `uint64_t seen` 하나에 `1ull << (ptr >> 2)` 로 표시하면 루프를 **정확한 위치**와 함께 잡는다. Linux 처럼 TTL 48 로 세는 방법도 끝나긴 하지만 "어디서 돌았는지" 는 못 알려준다.

</details>

<details>
<summary>힌트 3 — 판정의 기준값은 min(기대, 능력)</summary>

`cur < 기대` 만 보면 "Gen3 부품을 Gen4 로 기대한" 테스트 계획 오류도 SPEED_DOWN 이 된다. `want = min(기대, max)` 와 비교해 DOWN 을, `max < 기대` 는 따로 CAP_BELOW 로.

</details>

<details>
<summary>힌트 4 — 모니터의 두 단계</summary>

`up` 플래그 하나로 나눈다. 업 전: DETECT 진입 = 재시작, L0→RECOVERY = 속도 변경, L0 이면 Link Status 를 읽어 속도 확인. 업 후: 시작 시점의 `RCVRY_CNT` 를 기억했다가 창이 끝날 때 `now - c0` (unsigned) 으로 증가분을 본다.

</details>

<details>
<summary>힌트 5 — retrain 성공의 증거</summary>

"LT 가 0 이다" 는 retrain 이 **시작도 안 했을 때**도 참이다. 그래서 (1) RL 쓰기 전에 LBMS 를 지우고 (2) 끝난 뒤 LBMS 가 섰는지 본다. LBMS 는 "RL 로 시작한 retrain 이 끝났다" 를 HW 가 직접 알려주는 비트다.

</details>

## 5. 풀이

### Part A — 덤프에서 Link Status 까지

**① 덤프 형식.** `lspci -x` 는 표준 헤더 64B, `-xxx` 는 256B, `-xxxx` 는 확장 영역 포함 4096B. 각 줄 = `offset: ` + 16바이트(소문자 hex, 리틀 엔디언 레지스터를 바이트 순서대로). 그래서 `82: 44 10` 은 16비트 레지스터 `0x1044` 다. **함정: root 가 아니면 `-xxx` 를 줘도 64바이트만 나온다** (Linux sysfs `config` 파일이 비특권 읽기를 헤더로 제한) — capability 는 0x40 이후라 하나도 안 보인다. 이걸 "cap 없음" 으로 판정하면 오진이다 → Part B 의 `TRUNCATED`.

**② 손으로 읽기 (정상 덤프).**

| offset | 바이트 | 값 | 뜻 |
|---|---|---|---|
| 0x00 | `34 12 04 5a` | 1234:5a04 | Vendor/Device ID. `ff ff` 면 장치 없음(all-ones) |
| 0x06 | `10 00` | Status 0x0010 | bit 4 Capabilities List = 1 → 0x34 가 유효 |
| 0x09 | `02 08 01` | class 01/08/02 | Mass storage / NVM / NVMe |
| 0x34 | `40` | 첫 cap 0x40 | |
| 0x40 | `01 50` | PM, next 0x50 | |
| 0x50 | `05 70` | MSI, next 0x70 | |
| 0x70 | `10 b0 02 00` | PCIe, next 0xB0, CapReg 0x0002 | version 2, type 0 = Endpoint |
| 0x7C | `44 08 43 00` | LinkCap 0x00430844 | [3:0]=4 → 16 GT/s, [9:4]=4 → x4, [11:10]=10b ASPM L1, [17:15]=6 L1 exit 32~64 µs |
| 0x82 | `44 10` | LinkSta 0x1044 | 16 GT/s, x4, bit 12 SCC |
| 0xA0 | `04 00 1e 00` | LinkCtl2 0x0004, LinkSta2 0x001E | target 16 GT/s; 8 GT/s EQ complete + Phase 1/2/3 성공 |

**③ 인코딩.** 속도 필드 값 n 은 엄밀히는 Link Capabilities 2 의 *Supported Link Speeds Vector* 의 비트 위치를 가리키지만, 실무상 1=2.5, 2=5.0, 3=8.0, 4=16.0, 5=32.0, 6=64.0 GT/s (= Gen n) 다. 폭 필드는 레인 수 그대로(1, 2, 4, 8, 12, 16, 32). 링크가 내려가 있을 때 폭은 0 이 보통이고 속도 필드는 의미 없다 → `LINK_DOWN` 을 먼저 거른다.

**④ 판정 표 (VT 테스트 10개, cap 은 Gen4 x4 고정).**

| cur | 기대 | 판정 | 받는 팀 |
|---|---|---|---|
| Gen4 x4 | Gen4 x4 | OK | — |
| Gen4 x4 | Gen5 x4 | CAP_BELOW | 부품/SKU/strap/테스트 계획 — 링크는 정상 |
| Gen3 x4 | Gen5 x4 | CAP_BELOW \| SPEED_DOWN | 둘 다 |
| Gen4 x4 | Gen4 x8 | CAP_BELOW | x8 슬롯에 x4 부품: 정상 |
| Gen4 x2 | Gen4 x4 | WIDTH_DOWN | 보드/레인(물리) |
| Gen1 x2 | Gen4 x4 | SPEED_DOWN \| WIDTH_DOWN | 보드/SI |
| Gen4 x8 | Gen4 x4 | INCONSISTENT | 레지스터/RTL/모델 버그 (협상 폭 > 능력) |
| 폭 0 | | LINK_DOWN | 링크 자체 |
| 속도 7, 폭 3 | | BAD_ENC | 읽기 경로/디코드 확인 |

DOWN 과 CAP_BELOW 를 나누는 이유는 **디버그 담당이 다르기** 때문이다. Gen1 x4 로 붙은 Gen4 장치는 SI·EQ·refclk·리타이머 문제(보드/PHY 팀)지만, Gen4 장치를 Gen5 로 기대한 건 테스트 계획이나 부품 문제다. 실제 기대값은 **min(EP 능력, RP 능력, 슬롯 배선 폭)** 이다 (추가 연습 2). 실제 출력:

```text
PASS 01:00.0 exp=Gen4 x4 cap=Gen4(16.0GT/s) x4 cur=Gen4(16.0GT/s) x4 [OK] lnkcap=0x00430844 lnksta=0x1044
FAIL 01:00.0 exp=Gen4 x4 cap=Gen4(16.0GT/s) x4 cur=Gen1(2.5GT/s) x4 [SPEED_DOWN] lnkcap=0x00430844 lnksta=0x1041
FAIL 01:00.0 exp=Gen4 x4 cap=Gen4(16.0GT/s) x4 cur=Gen4(16.0GT/s) x1 [WIDTH_DOWN] lnkcap=0x00430844 lnksta=0x1014
```

다운그레이드 덤프의 LinkSta2 가 `0x0000` (8 GT/s EQ 미완료) 인 것도 단서다: 링크가 8 GT/s 로 올라가려다 EQ 에서 실패했거나 아예 시도하지 않았다는 뜻 → LTSSM trace(Part B)로 확인할 차례. 실무 대응: `lspci -vv` 의 `LnkCap / LnkSta / LnkCtl2 / LnkSta2` 줄이 이 필드들의 해석이고 (최근 pciutils 는 LnkSta 에 `(ok)`/`(downgraded)` 를 붙여준다), sysfs `current_link_speed / current_link_width / max_link_speed / max_link_width` 도 같은 값이다. 레지스터 하나만 읽을 땐 `setpci -s 01:00.0 CAP_EXP+12.w`.

### Part B — 끝나는 파서, 그리고 시간 축의 링크

**① 체인 결함 표 (CT 테스트 + 최장 체인).**

| 주입 | 결과 | bad / from | 왜 이렇게 보고하나 |
|---|---|---|---|
| 0x34 = 0x43 | OK, 4개 | — | 하위 2비트는 reserved → `& 0xFC`. 마스크 안 하면 0x43 에서 엉뚱한 바이트를 ID 로 읽는다 |
| Status[4] = 0 | NO_CAPLIST | — | 0x34 는 이 비트가 1 일 때만 유효 |
| 0x34 = 0x20 | BAD_PTR | 0x20 / 0x34 | 헤더 안은 cap 일 수 없다 |
| MSI next = 0x3C | BAD_PTR, 2개 | 0x3C / 0x50 | "어느 cap 의 next 가 깨졌나" 가 수리 위치 |
| PCIe next = 0x70 | LOOP, 3개 | 0x70 / 0x70 | 자기 자신 |
| len = 64 | TRUNCATED | 0x40 / 0x34 | **비루트 lspci**. "cap 없음" 이 아니라 "sudo 로 다시" |
| 0x34 = 0 | OK, 0개 | — | cap 이 없는 것도 정상 |
| 전부 0xFF | NOT_PRESENT | — | 장치 없음/링크 다운 상태의 config read |
| 48개 꽉 찬 체인 | OK, 48개 | — | 상한이 정확히 맞는지 (off-by-one) |
| 그 끝 → 0x40 | LOOP, 48개 | 0x40 / 0xFC | 가장 긴 루프 |

Linux `__pci_find_next_cap_ttl()` 은 TTL 48 로 세고 `pos < 0x40` 이면 멈춘다 — 같은 상한(48 = (256−64)/4)이다. 검증 코드는 한 걸음 더 가서 **어디서** 깨졌는지를 남긴다: 프로덕션 드라이버는 "cap 없음" 으로 조용히 넘어가도 되지만, 검증은 그게 버그 리포트다.

**② LTSSM 은 표준 레지스터로 안 보인다.** config space 에 있는 건 결과(LinkSta 의 속도·폭·LT·DLLLA)뿐이다. 상태 머신의 현재 상태는 컨트롤러의 벤더 디버그 레지스터로 본다 — 이 mock 은 DesignWare 계열 인코딩(Linux `enum dw_pcie_ltssm`: DETECT_QUIET 0x00 … L0 0x11 … RCVRY_EQ3 0x23)을 본떴다. 링크업의 큰 흐름:

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m04b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">LTSSM 링크업 (LINKUP_GOOD 스크립트) 과 샘플링 사각지대</text>
  <g stroke="currentColor" stroke-width="1.2">
    <rect x="10" y="40" width="90" height="44" rx="6" class="f-mut"/>
    <rect x="125" y="40" width="90" height="44" rx="6" class="f-mut"/>
    <rect x="240" y="40" width="110" height="44" rx="6" class="f-mut"/>
    <rect x="375" y="40" width="90" height="44" rx="6" class="f-ok"/>
    <rect x="490" y="40" width="120" height="44" rx="6" class="f-warn"/>
    <rect x="635" y="40" width="95" height="44" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="55" y="58">Detect</text><text x="55" y="74">수신기 감지</text>
    <text x="170" y="58">Polling</text><text x="170" y="74">TS1/TS2, 극성</text>
    <text x="295" y="58">Configuration</text><text x="295" y="74">레인 번호, 폭</text>
    <text x="420" y="58">L0 Gen1</text><text x="420" y="74">2.5 GT/s</text>
    <text x="550" y="58">Recovery</text><text x="550" y="74">Speed + EQ0~3</text>
    <text x="682" y="58">L0 Gen3→Gen4</text><text x="682" y="74">목표 속도</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m04b)">
    <path d="M100 62 L123 62"/><path d="M215 62 L238 62"/><path d="M350 62 L373 62"/><path d="M465 62 L488 62"/><path d="M610 62 L633 62"/>
    <path d="M682 84 C 682 115 550 115 550 88"/>
    <path d="M490 84 C 450 130 120 130 55 88"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="560" y="122">Gen3 → Gen4 로 한 번 더</text>
    <text x="200" y="138">실패 → Detect (재시작 = restarts++)</text>
  </g>
  <line x1="40" y1="200" x2="720" y2="200" stroke="currentColor"/>
  <text x="720" y="220" fill="currentColor" font-size="12" text-anchor="end">시간 →</text>
  <rect x="40" y="185" width="300" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="340" y="185" width="10" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="350" y="185" width="370" height="30" class="f-ok" stroke="currentColor"/>
  <text x="190" y="205" fill="currentColor" text-anchor="middle" font-size="12">L0 Gen4 (500us)</text>
  <text x="530" y="205" fill="currentColor" text-anchor="middle" font-size="12">L0 Gen4</text>
  <text x="345" y="178" fill="currentColor" text-anchor="middle" font-size="12">6us Recovery</text>
  <g stroke="currentColor" stroke-width="2">
    <line x1="80" y1="225" x2="80" y2="240"/><line x1="180" y1="225" x2="180" y2="240"/><line x1="280" y1="225" x2="280" y2="240"/>
    <line x1="380" y1="225" x2="380" y2="240"/><line x1="480" y1="225" x2="480" y2="240"/><line x1="580" y1="225" x2="580" y2="240"/>
  </g>
  <text x="40" y="258" fill="currentColor" font-size="12">샘플(101~102us 간격): 모두 L0 → rcvry_seen = 0. 하지만 벤더 RCVRY_CNT 는 +1 → UNSTABLE.</text>
  <text x="40" y="282" fill="currentColor" font-size="12">폴링 모니터는 샘플 간격보다 짧은 상태를 원리적으로 못 본다 (짧은 Detect 도 마찬가지).</text>
  <text x="40" y="306" fill="currentColor" font-size="12">그래서 HW 카운터 · sticky 비트(LBMS/LABS) · AER correctable 에러 · LTSSM 히스토리 기능을 함께 본다.</text>
</svg>
```

Gen3 이상 링크는 **2.5 GT/s 로 L0 에 먼저 들어간 뒤** Recovery(RcvrLock → Speed → … → Equalization Phase 0~3 → RcvrCfg → Idle)를 거쳐 속도를 올린다. Gen4 는 보통 8 GT/s EQ 를 먼저 하고 16 GT/s EQ 를 한 번 더 한다 (PCIe 5.0 이후에는 EQ 를 건너뛰는 옵션이 생겼다). 그래서 "L0 를 봤다" 는 링크업 완료가 아니다 — **목표 속도의 L0** 여야 한다. mock 시간은 크기 감각만 흉내낸 값이다 (실제 Gen4 EQ 는 ms 단위, Detect.Quiet 12 ms·Polling.Active 24 ms 같은 스펙 타임아웃이 있다).

**③ 모니터 규칙과 그 이유.**

| 규칙 | 이유 |
|---|---|
| `[31:6] ≠ 0` 또는 미정의 코드 → 즉시 BAD_STATE | reserved 비트가 1 이면 읽기 경로(DBI 주소, 클럭 게이트) 이상. 0xFFFFFFFF 도 여기 걸린다 |
| 엄격한 "전이 그래프" 검사는 하지 않음 | 샘플링이 중간 상태를 건너뛰므로 Config→Polling 처럼 보이는 가짜 위반이 나온다. 대신 **DETECT 재진입 = 재시작** 만 센다 |
| 업 = 목표 속도 이상의 L0 | 위 설명. 업 전 `L0 → RECOVERY` 는 속도 변경으로 센다 |
| 업 후 관찰 창 + 카운터 증가분 | 링크가 붙은 직후 떨리는(flapping) 보드를 잡는다. 카운터는 unsigned 뺄셈 (wrap 안전) |
| 우선순위 BAD_STATE > LINK_DOWN > TIMEOUT > RETRY > UNSTABLE > OK | 더 치명적인 것을 먼저 보고 |

실제 출력 (spec: timeout 100 ms, interval 10 µs, 관찰 1 ms, target Gen4, max_restarts 1):

```text
OK ltssm=L0(0x11) dwell=1003us l0=Gen4 x4 target=Gen4 up_at=2210us restarts=0 speed_changes=2 rcvry_seen=0 rcvry_cnt=0 samples=292 elapsed=3213us/100000us
   trace DETECT>POLLING>CONFIG>L0>RECOVERY>L0>RECOVERY>L0
TIMEOUT ltssm=L0(0x11) dwell=96348us l0=Gen1 x4 target=Gen4 up_at=0us restarts=0 speed_changes=1 rcvry_seen=0 rcvry_cnt=0 samples=8362 elapsed=100006us/100000us
RETRY ltssm=L0(0x11) dwell=1003us l0=Gen4 x4 target=Gen4 up_at=4377us restarts=3 speed_changes=2 rcvry_seen=0 rcvry_cnt=0 samples=489 elapsed=5380us/100000us
BAD_STATE ltssm=?(0x2A) dwell=88us l0=Gen0 x0 target=Gen4 up_at=0us restarts=0 speed_changes=0 rcvry_seen=0 rcvry_cnt=0 samples=30 elapsed=320us/100000us
UNSTABLE ltssm=L0(0x11) dwell=1012us l0=Gen4 x4 target=Gen4 up_at=2223us restarts=0 speed_changes=0 rcvry_seen=0 rcvry_cnt=1 samples=33 elapsed=3235us/100000us
LINK_DOWN ltssm=DETECT_QUIET(0x00) dwell=0us l0=Gen4 x4 target=Gen4 up_at=2210us restarts=0 speed_changes=2 rcvry_seen=1 rcvry_cnt=0 samples=265 elapsed=2916us/100000us
```

| 줄 | 읽는 법 | 다음 행동 |
|---|---|---|
| TIMEOUT … `L0` dwell 96 ms, Gen1, speed_changes=1 | 링크는 붙었지만 8 GT/s 진입(EQ)에 한 번 실패하고 Gen1 에 주저앉음 — Part A 의 다운그레이드 덤프가 바로 이 상태 | EQ 프리셋/계수, 채널 손실, 리타이머, refclk 지터. Lane Margining 으로 마진 측정 |
| RETRY restarts=3 | 결국 붙었지만 Polling 에서 3번 Detect 로 떨어짐 | 콜드부트 수백 회 반복으로 재현율 측정 — 양산에서 "가끔 SSD 미인식" 이 된다 |
| BAD_STATE 0x2A | 정의되지 않은 코드. `dwell=88us` 는 마지막 정상 상태(POLL_ACTIVE)의 체류 시간 | 디버그 레지스터 주소/비트필드 확인, 다른 경로(JTAG)로 같은 레지스터 읽기 |
| UNSTABLE rcvry_seen=0 rcvry_cnt=1 | 샘플러는 못 봤지만 HW 가 Recovery 1회를 셌다 (거친 샘플링이라 speed_changes 도 0 으로 과소 계수) | AER correctable(Receiver Error, Bad TLP/DLLP, Replay Timer Timeout) 함께 확인 |
| LINK_DOWN | L0 → Recovery 400 µs → Detect | surprise down: 상대 전원/리셋, 커넥터, 심각한 SI |

같은 숨은-Recovery 스크립트를 interval 1 µs 로 돌리면 `rcvry_seen=1` 도 된다 (테스트로 고정). 샘플링을 촘촘히 하면 보이지만 버스를 점유하고, 실칩의 수 µs 짜리 Recovery 는 소프트웨어 폴링으로는 원래 못 본다 — **카운터가 정답**이다.

### Part C — Retrain 스트레스: "매번 같은 링크로 돌아오는가"

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m04c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">retrain_once() — 루트 포트 PCIe cap(0x60) 에서</text>
  <g stroke="currentColor" stroke-width="1.2">
    <rect x="10" y="40" width="130" height="56" rx="6" class="f-mut"/>
    <rect x="160" y="40" width="130" height="56" rx="6" class="f-mut"/>
    <rect x="310" y="40" width="130" height="56" rx="6" class="f-acc"/>
    <rect x="460" y="40" width="130" height="56" rx="6" class="f-mut"/>
    <rect x="610" y="40" width="120" height="56" rx="6" class="f-ok"/>
    <rect x="160" y="150" width="130" height="44" rx="6" class="f-warn"/>
    <rect x="460" y="150" width="130" height="44" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="75" y="60">① LT==0 대기</text><text x="75" y="78">이전 training 끝?</text>
    <text x="225" y="60">② LBMS|LABS</text><text x="225" y="78">W1C 클리어, CNT c0</text>
    <text x="375" y="60">③ LinkCtl RMW</text><text x="375" y="78">|= RL (0x0020)</text>
    <text x="525" y="60">④ LT==0 대기</text><text x="525" y="78">≤ 1 s, 10us 간격</text>
    <text x="670" y="60">⑤ 판정</text><text x="670" y="78">DLL→속도→폭→LBMS</text>
    <text x="225" y="168">PRECOND_TIMEOUT</text><text x="225" y="184">→ 중단</text>
    <text x="525" y="168">TIMEOUT + ltssm</text><text x="525" y="184">→ 중단</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m04c)">
    <path d="M140 68 L158 68"/><path d="M290 68 L308 68"/><path d="M440 68 L458 68"/><path d="M590 68 L608 68"/>
    <path d="M75 96 L160 160"/><path d="M525 96 L525 148"/>
  </g>
  <text x="10" y="226" fill="currentColor" font-size="12">LT(LinkSta[11]) = "Configuration/Recovery 중이거나, RL 에 1 을 썼지만 training 이 아직 시작 안 됨".</text>
  <text x="10" y="246" fill="currentColor" font-size="12">LBMS(LinkSta[14]) = "RL 로 시작한 retrain 이 끝남" 또는 "신뢰성 때문에 HW 가 속도/폭을 바꿈" — sticky, W1C.</text>
  <text x="10" y="266" fill="currentColor" font-size="12">RL · LT · LBMS 는 루트/다운스트림 포트 쪽 비트다 — 엔드포인트에서는 reserved. 그래서 스트레스는 RP 에서 건다.</text>
  <text x="10" y="286" fill="currentColor" font-size="12">Linux pcie_retrain_link() 도 RL 세트 → LT 클리어(또는 DLLLA) 대기, 타임아웃 1 s.</text>
</svg>
```

**① 먼저 정적 판정을 재사용한다.** 링크업 후 `snapshot_cfg()` 로 루트 포트 config 256B 를 16비트 읽기로 떠서 Part A 의 `cap_walk → decode_link → link_verdict` 에 그대로 넣는다 (PCIe cap @0x60, type 4 = Root Port, Gen4 x4, DLLLA=1 → OK). 덤프 파일이든 라이브 레지스터든 **같은 판정 코드**를 쓰는 게 핵심이다 — 랩 PC 의 lspci 와 온칩 펌웨어 테스트가 같은 결론을 낸다.

**② 시그니처와 우선순위.** 한 iteration 에서 여러 개가 동시에 틀려도 시그니처는 하나: `DLL_DOWN > SPEED_DOWN > WIDTH_DOWN > NO_LBMS`. 데이터 링크가 안 살아 있으면 속도·폭은 의미 없고, 속도·폭이 틀린 건 LBMS 누락보다 사용자 영향이 크다. TIMEOUT/PRECOND_TIMEOUT 은 링크가 갇힌 것이라 **즉시 중단**하고 그 순간의 LTSSM 상태를 남긴다 (더 돌리면 같은 TIMEOUT 만 1 s 씩 쌓인다).

**③ 실제 리포트 (N=200).**

```text
RETRAIN_STRESS port=RP0(good) exp=Gen4 x4 iters=200/200 pass=200 fail=0
RETRAIN_STRESS port=RP0(lane-marginal) exp=Gen4 x4 iters=200/200 pass=196 fail=4
  WIDTH_DOWN      count=4   first=60  last=144 INTERMITTENT e.g. lnksta=0x7024 Gen4 x2 ltssm=L0 rcvry+=1 t=52us
   injected: 60 82 124 144
   naive: injected=4 detected=0
RETRAIN_STRESS port=RP0(gen4-fallback) exp=Gen4 x4 iters=200/200 pass=56 fail=144
  SPEED_DOWN      count=144 first=57  last=200 PERSISTENT   e.g. lnksta=0x7043 Gen3 x4 ltssm=L0 rcvry+=1 t=52us
RETRAIN_STRESS port=RP0(stuck-recovery) exp=Gen4 x4 iters=33/200 pass=32 fail=1 ABORTED
  TIMEOUT         count=1   first=33  last=33  PERSISTENT   e.g. lnksta=0x3844 Gen4 x4 ltssm=RCVRY_LOCK rcvry+=1 t=1000008us
RETRAIN_STRESS port=RP0(no-lbms) exp=Gen4 x4 iters=200/200 pass=0 fail=200
  NO_LBMS         count=200 first=1   last=200 PERSISTENT   e.g. lnksta=0x3044 Gen4 x4 ltssm=L0 rcvry+=1 t=52us
```

| 리포트 | 해석 | 디버그 엔지니어의 다음 행동 |
|---|---|---|
| WIDTH_DOWN ×4 INTERMITTENT (60, 82, 124, 144) | 간헐적으로 한 쌍의 레인이 training 에서 빠짐. 0x7024 = LBMS+DLLLA+SCC, x2 | 레인별 마진(Lane Margining at Receiver), 커넥터/AC 커플링 캡/납땜, 온도 의존성. 실패 iteration 이 랜덤이면 SI, 특정 시점 이후면 열 |
| SPEED_DOWN 57~200 PERSISTENT | 어느 순간부터 Gen4 를 못 하고 Gen3 에 고착 | 그 시점에 무엇이 바뀌었나(온도 상승, 전압 droop). 16 GT/s EQ 결과 레지스터, 프리셋 고정 후 재시도 |
| TIMEOUT @33, RCVRY_LOCK, LT=1 | retrain 중 Recovery.RcvrLock 에서 못 나옴 (0x3844 = LT+DLLLA+SCC) | 상대편(EP) 상태 동시 덤프, PHY CDR lock, refclk. 실칩에선 보통 24 ms 뒤 Detect 로 떨어지므로 "고착" 자체가 LTSSM 버그 후보 |
| NO_LBMS ×200 | retrain 은 매번 성공(rcvry+=1)했는데 LBMS 가 안 섬 | **IP 의 스펙 위반** — 보드 문제 아님. LBMS 에 기대는 SW(대역폭 알림 드라이버)가 오동작. RTL 팀에 errata 후보로 |

**④ 결함 주입이 "물리는지" 확인.** 간헐 폭 저하 테스트는 두 가지를 CHECK 한다: (1) 검출 목록이 mock 이 기록한 주입 목록 `60 82 124 144` 와 **정확히 같다** (빠짐도 오탐도 없음), (2) 같은 결함·같은 시드에서 `retrain_naive()` — RL 을 쓰고 곧바로 LinkSta 를 읽는 흔한 구현 — 은 **0개** 를 잡는다. RL 직후의 LinkSta 는 retrain **이전** 링크의 속도·폭이기 때문이다. "순진한 구현이 놓친다" 가 테스트로 고정돼 있어야 나중에 누가 대기 코드를 지워도 회귀가 잡힌다.

**⑤ 실행 시간.** 가짜 시계 덕분에 62개 테스트(100 ms 링크 타임아웃, 1 s retrain 타임아웃, retrain 1,000회 이상 포함)가 `0.005 total` 에 끝났다 (`time /tmp/SIV-M04_so`, Apple Silicon, Apple clang 21). 실보드에서 retrain 1회는 Gen4 EQ 포함 수~수십 ms 라 200회면 수 초 — 야간 회귀에선 수천~수만 회 돌린다 (몇 회면 충분한가는 추가 연습 3).

**실무 연결.** Linux 에서 루트 포트 retrain 은 `setpci -s <RP> CAP_EXP+10.w=0020:0020` (값:마스크) 로 걸 수 있고, 커널의 `pcie_retrain_link()` 가 같은 절차(RL → LT 클리어 대기, 1 s)를 쓴다. 일부 스위치는 RL 이 저절로 안 지워지는 errata 가 있어 커널에 quirk(`quirk_enable_clear_retrain_link`)가 있다 — 검증은 이런 errata 를 **찾는** 쪽이다. 속도를 바꿔 재시도하려면 Link Control 2 의 Target Link Speed 를 쓰고 retrain 한다. Pre-silicon 에서는 PCIe VIP(UVM)가 LTSSM 전이와 타임아웃을 체크하고, post-silicon 에서는 이 레슨 같은 C 테스트가 JTAG/온칩 펌웨어/호스트 리눅스에서 돈다 (SIV-B13). 프로토콜 분석기는 TS1/TS2 순서를 직접 보여주지만 비싸고 몇 대 없다 — 그래서 레지스터 기반 플로우가 1차 스크리닝이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cd lessons/SIV/SIV-M04
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/SIV-M04_so && /tmp/SIV-M04_so
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/SIV-M04_san && /tmp/SIV-M04_san
```

실제 결과: 위 Part A/B/C 출력 + `62/62 passed`, exit 0. ASan/UBSan 빌드도 `62/62 passed`, 진단 메시지 없음. (`/tmp` 에서 실행하면 덤프 파일을 못 찾아 `30/62 passed` — cwd 가 중요하다.)

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `ptr = b[0x34]` 를 마스크 없이 사용 | 하위 2비트는 reserved. 1 이 서 있으면 엉뚱한 바이트를 ID 로 읽음 | `ptr = b[0x34] & 0xFC`, next 도 동일 |
| `while (ptr) ptr = b[ptr+1];` | 루프면 무한 대기, 0x3C 같은 포인터면 헤더를 cap 으로 해석 | 방문 비트맵 + `ptr < 0x40` 검사 + `ptr+2 > len` 검사 |
| 64바이트 덤프에서 "PCIe cap 없음" | 비루트 lspci 는 헤더만 준다 | TRUNCATED 로 따로 보고 ("sudo 로 다시") |
| `cur < 기대` 만으로 FAIL | Gen3 부품을 Gen4 로 기대한 계획 오류도 "링크 불량" 이 됨 | `min(기대, 능력)` 과 비교 + CAP_BELOW 분리 |
| 엔드포인트에서 RL/LT/LBMS 를 읽고 씀 | EP(업스트림 포트)에서는 reserved — 항상 0 | 루트/다운스트림 포트 쪽 PCIe cap 에서 |
| RL 쓰고 바로 LinkSta 판정 | retrain 이전 값을 봄 (naive: 4개 중 0개 검출) | LT==0 대기 + LBMS 로 "끝났다" 를 확인 |
| LBMS 를 미리 안 지움 | 이전 retrain 의 sticky 비트가 남아 있어 증거가 안 됨 | RL 전에 `LinkSta = LBMS\|LABS` (W1C) |
| LinkSta 를 RMW 로 쓰기 | 읽은 값에 LBMS=1 이 있으면 원치 않게 지움 / 다른 W1C 비트 영향 | W1C 레지스터는 지울 비트만 써서 (C-M02) |
| 첫 L0 를 "링크업" 으로 간주 | Gen1 L0 다음에 속도 변경이 남아 있음 | 목표 속도 이상의 L0 |
| LTSSM 샘플 trace 로 전이 그래프를 엄격 검사 | 샘플링이 상태를 건너뛰어 가짜 위반 | 재시작(DETECT 재진입)만 세고, 정밀 전이는 HW 히스토리/분석기로 |
| 샘플로 Recovery 0회 → "안정" | 수 µs Recovery 는 폴링으로 안 보임 | 벤더 카운터 증가분, AER correctable 에러 |
| retrain 타임아웃 후에도 계속 반복 | 같은 TIMEOUT 이 1 s 씩 누적, 로그 오염 | 즉시 중단 + 그 순간 LTSSM/LinkSta 기록 |

## 8. 면접 Q&A

<details>
<summary>Q1. lspci -xxx 덤프만 주어졌을 때 링크 속도·폭을 어떻게 찾나요?</summary>

0x06 Status bit 4 로 capability list 존재 확인 → 0x34 의 포인터(`& 0xFC`) → 각 cap 의 [ID, next] 를 따라 ID 0x10 을 찾고 → 그 위치 +0x0C 가 Link Capabilities(최대), +0x12 가 Link Status(현재). 예: PCIe cap 이 0x70 이면 0x82 의 `44 10` → 0x1044 → [3:0]=4 = 16 GT/s, [9:4]=4 = x4. 꼬리질문 "root 가 아니면?" → 64바이트만 보여서 cap 을 못 찾는다. "lspci -vv 의 어느 줄?" → `LnkCap:` / `LnkSta:` / `LnkCtl2:` (Target Link Speed).

</details>

<details>
<summary>Q2. Gen4 x4 를 기대했는데 Gen1 x4 로 붙었습니다. 무엇을 의심하고 어떤 순서로 보나요?</summary>

먼저 **기대가 맞는지**: EP 와 RP 의 LinkCap, 루트 포트 LinkCtl2 Target Link Speed(펌웨어/BIOS 가 낮춰놨나), 슬롯 배선 폭. 다음 **어디서 멈췄나**: Gen1 L0 이후 Recovery.Speed/EQ 에서 실패하고 돌아왔는지 LTSSM 히스토리·LinkSta2(8 GT/s EQ complete/Phase 성공 비트)로 확인. 원인 후보는 EQ 실패(채널 손실, 프리셋), refclk 품질, 리타이머 설정, 한쪽의 errata. 실험: Target Speed 를 Gen3 로 놓고 retrain → 되면 16 GT/s 특유 문제, 안 되면 8 GT/s EQ 부터. 꼬리질문 "x4 가 x2 로 붙으면?" → 속도 문제가 아니라 **레인 물리** 문제(레인 단선, AC 캡, 커넥터). 극성 반전은 training 이 자동 처리하고 레인 역순(reversal)도 지원하는 구현이 많으므로 "배선이 뒤집혔다" 보다는 한 레인이 죽은 쪽을 먼저 본다.

</details>

<details>
<summary>Q3. LTSSM 주요 상태와 링크업 순서를 설명하고, 소프트웨어가 그 상태를 어떻게 보는지 말해보세요.</summary>

Detect(상대 수신기 감지) → Polling(비트/심볼 락, TS1/TS2 교환, 극성) → Configuration(링크·레인 번호, 폭 결정) → L0(정상). Recovery 는 재훈련·속도 변경·EQ 를 담당하고, L0s/L1 은 ASPM 저전력, L2 는 주 전원 off, 그 외 Disabled/Loopback/Hot Reset. Gen3+ 는 2.5 GT/s 로 L0 에 들어간 뒤 Recovery 로 속도를 올린다. 꼬리질문 "표준 레지스터로 LTSSM 상태를 읽을 수 있나?" → 아니오. 표준은 결과(LinkSta 속도·폭, LT, DLLLA)만 주고, 상태 값은 컨트롤러 벤더 디버그 레지스터(DesignWare 의 LTSSM 필드 등)로 읽는다. 그래서 "어느 상태에서 멈췄나" 는 IP 데이터북을 알아야 답할 수 있다.

</details>

<details>
<summary>Q4. 소프트웨어에서 retrain 을 걸고 성공을 판정하는 절차는? 왜 엔드포인트가 아니라 루트 포트인가요?</summary>

루트(또는 스위치 다운스트림) 포트의 Link Control bit 5 Retrain Link 에 1 (RMW). 걸기 전 LT==0 확인, LBMS/LABS 를 W1C 로 지우고, 건 뒤 LT==0 을 타임아웃(리눅스는 1 s)으로 기다린 다음 LinkSta 의 속도·폭·DLLLA·LBMS 를 확인한다. RL, LT, LBMS 는 다운스트림 포트에만 의미가 있고 엔드포인트(업스트림 포트)에서는 reserved 다. 꼬리질문 "LT 가 0 인 걸로 끝났다고 믿어도 되나?" → RL 직후 LT 가 늦게 서는 구현이면 시작도 안 한 retrain 을 끝났다고 오판한다(추가 연습 4). 그래서 LBMS 같은 "끝났다" 증거를 함께 본다.

</details>

<details>
<summary>Q5. LBMS 와 LABS 의 차이는? 검증에서 어떻게 쓰나요?</summary>

둘 다 Link Status 의 sticky W1C 비트(14, 15)이고 Link Bandwidth Notification Capability 가 있는 다운스트림 포트에서 쓴다. LBMS = RL 로 시작한 retrain 이 끝났거나, **신뢰성 문제 때문에** HW 가 속도/폭을 바꿈. LABS = 신뢰성 외의 이유로 HW 가 **자율적으로** 속도/폭을 바꿈. 검증에선 (1) retrain 완료의 증거, (2) 스트레스 중 아무도 모르게 링크가 바뀌었는지 탐지(테스트 시작 시 지우고 끝에 확인), (3) 비트 자체가 스펙대로 서는지 IP 검증(Part C NO_LBMS). 꼬리질문 "그냥 Recovery 가 한 번 났을 때(속도·폭 변화 없음)도 LBMS 가 서나?" → 아니다. 그런 Recovery 는 표준 비트로 안 남으니 벤더 카운터나 AER correctable 에러로 본다.

</details>

<details>
<summary>Q6. LTSSM 을 폴링으로 감시하면 무엇을 놓치나요? 대안은?</summary>

샘플 간격보다 짧은 상태 — 수 µs Recovery, 짧은 Detect 재진입 — 를 원리적으로 못 본다(Part B: 101 µs 간격에서 6 µs Recovery 를 0회로 봄, 속도 변경도 0회로 과소 계수). 촘촘히 폴링하면 버스를 점유하고 그래도 실칩 속도엔 못 미친다. 대안: 벤더 Recovery/전이 카운터의 **증가분**, LTSSM 히스토리 버퍼(지원하는 IP), AER correctable 에러(Receiver Error, Bad TLP, Bad DLLP, Replay Timer Timeout, REPLAY_NUM Rollover), 프로토콜 분석기. 꼬리질문 "그럼 trace 로 전이 그래프 위반을 검사할 수 있나?" → 샘플 trace 로는 가짜 위반이 나온다. 단조 진행/Detect 재진입 같은 "건너뛰어도 성립하는" 성질만 검사한다.

</details>

<details>
<summary>Q7. capability 체인 파서에서 무한 루프를 어떻게 막고, 확장 capability 는 무엇이 다른가요?</summary>

표준 cap 은 0x40~0xFC 의 dword 정렬 위치라 최대 48개 — Linux 는 TTL 48 로 세고 `pos < 0x40` 이면 멈춘다. 검증 코드는 방문 비트맵으로 루프 **위치**까지 보고한다. 확장 cap 은 0x100 부터, 32비트 헤더 [15:0] ID, [19:16] version, [31:20] next. 헤더가 0 이면 없음, all-ones 면 확장 영역 접근 불가(ECAM 없는 접근, 장치 없음). Linux 는 TTL (4096−256)/8 = 480 을 쓴다. 꼬리질문 "확장 cap 에 링크 관련으로 무엇이 있나?" → Secondary PCIe(0x0019, 레인별 EQ 제어), Physical Layer 16.0 GT/s(0x0026), Lane Margining at Receiver(0x0027), AER(0x0001).

</details>

<details>
<summary>Q8. retrain 200회가 전부 통과했습니다. 실패율이 0 이라고 말해도 되나요?</summary>

아니다. 0/N 에서 95% 신뢰 상한은 `(1−p)^N = 0.05` 를 푼 p ≈ 3/N (rule of three): N=200 이면 약 1.49%, N=1000 이면 0.30% (추가 연습 3 실행값). "0.1% 미만" 을 95% 로 주장하려면 무실패 2,995회, 99% 면 4,603회가 필요하다. 그래서 야간 회귀에서 수천 회를 돌리고, 온도·전압 코너(SIV-M08)마다 따로 센다. BER 신뢰구간도 같은 식이다(SIV-M06). 꼬리질문 "실패가 1개 나오면?" → 그 iteration 의 조건(온도, 시간, 직전 이벤트)을 붙잡고 재현율을 측정 — 간헐 실패는 숫자 하나보다 **패턴(INTERMITTENT/PERSISTENT, 시작 시점)** 이 원인을 말해준다.

</details>

## 9. 추가 연습

### 연습 1 — 확장 capability 걷기

4096B config 에서 0x100 부터 확장 cap 을 따라가 ID 로 찾는 `ext_find()` 를 써라. AER(0x0001)@0x100 → Secondary PCIe(0x0019)@0x148 → Physical Layer 16.0 GT/s(0x0026)@0x170 → 끝. 루프, 0x100 미만 포인터, all-ones 헤더, 256B 덤프를 처리.

<details>
<summary>답 보기</summary>

헤더 `h`: ID = `h & 0xFFFF`, next = `(h >> 20) & 0xFFC` (하위 2비트 reserved). 방문 표시는 dword 단위 1024칸. `next == 0` 이면 NOT_FOUND, `next < 0x100` 이면 BAD_PTR, 이미 방문이면 LOOP(위치 보고). 시작 헤더가 0 이나 0xFFFFFFFF 면 NO_EXT, len < 4096 이면 확장 영역이 덤프에 없으니 NO_EXT. 테스트: 0x0026 은 3번째(0x170)에서 찾고, 0x170 의 next 를 0x148 로 바꾸면 LOOP@0x148.

</details>

### 연습 2 — 기대 링크 계산

`expected_link(ep_cap, rp_cap, slot_width)` = 속도는 min(EP, RP), 폭은 min(EP, RP, 슬롯 배선).

<details>
<summary>답 보기</summary>

예: EP Gen4 x4 + RP Gen5 x16 + 슬롯 x8 → Gen4 x4. EP Gen4 x16 + RP Gen4 x16 + x16 커넥터에 x8 만 배선 → Gen4 x8 (이걸 x16 으로 기대하면 매일 WIDTH_DOWN 오탐). 테스트 계획의 "기대값" 은 이 계산 결과여야 하고, 슬롯 배선 폭은 보드 회로도에서 와야 한다. (실제 협상은 양쪽이 지원하는 폭의 공통집합에서 고르므로 x12 같은 드문 폭은 min 만으로 부족할 수 있다.)

</details>

### 연습 3 — 무실패 N 회의 의미 (rule of three)

0/N 의 95% 신뢰 상한 p 를 이분 탐색으로 구하고 (libm 없이), "p < 0.1%" 를 95%/99% 로 주장하는 데 필요한 최소 N 을 구하라.

<details>
<summary>답 보기</summary>

`(1−p)^N = 1−CL` → p = 1 − (1−CL)^(1/N) ≈ −ln(1−CL)/N. 95% 면 −ln 0.05 = 2.996 이라 ≈ 3/N. 필요한 N 은 `(1−p)^N ≤ 1−CL` 이 처음 성립하는 N = ⌈ln(1−CL)/ln(1−p)⌉. 실행 결과: `p_upper(200)=1.4867% p_upper(1000)=0.2991% n_needed(0.1%,95%)=2995 (99%: 4603)`. 근사식 3/N = 1.5% 와 거의 같다.

</details>

### 연습 4 — "RL 직후 LT=1" 스펙 준수 검사

LT 가 RL 쓰기 후 5 µs 동안 안 서는 결함 mock 을 만들고, (a) 순진한 "LT==0 대기" 가 첫 읽기에 완료로 착각하는 것과 (b) RL 직후 첫 읽기의 LT 를 검사하는 체크가 이걸 잡는 것을 보여라.

<details>
<summary>답 보기</summary>

스펙의 LT 정의에 "RL 에 1 을 썼지만 training 이 아직 시작 안 됨" 이 포함돼 있으므로 RL 직후 첫 읽기에서 LT=1 이어야 한다. `check_lt_immediate()` = RL 쓰기 → 한 번 읽기 → LT 확인. 실행 결과 `P4 naive_wait polls: good=40 lt-late=1` — 정상 칩에선 40회 폴링 후 완료, 결함 칩에선 1회 만에 "완료"(실제로는 시작 전). 본문 Part C 가 LBMS 까지 확인하는 이유이기도 하다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/SIV-M04_pr && /tmp/SIV-M04_pr
```

실제 출력:

```text
P3 p_upper(200)=1.4867% p_upper(1000)=0.2991% n_needed(0.1%,95%)=2995 (99%: 4603)
P4 naive_wait polls: good=40 lt-late=1
17/17 passed
```

## 10. 핵심 정리

- 링크 확인 경로: Status[4] → `0x34 & 0xFC` → [ID, next] 체인 → ID 0x10 → **+0x0C LinkCap(최대), +0x12 LinkSta(현재)**, [3:0] 속도(1~6 = 2.5~64 GT/s), [9:4] 폭(레인 수).
- 체인 파서는 반드시 끝나야 한다: 방문 비트맵(최대 48), `< 0x40` 거부, 잘린 덤프(비루트 lspci = 64B) 와 all-ones(장치 없음) 를 따로 보고, **bad / from** 위치까지.
- 판정은 `min(기대, 능력)` 과 비교: DOWN = 링크 문제(보드/PHY), CAP_BELOW = 부품/계획 문제. 기대값 = min(EP, RP, 슬롯 배선).
- LTSSM 은 표준 레지스터에 없다(벤더 디버그). 링크업 = **목표 속도의 L0** (Gen1 L0 → Recovery 로 속도/EQ). 재시작 = DETECT 재진입.
- 폴링은 짧은 Recovery 를 못 본다 → 벤더 카운터 증가분 · LBMS/LABS · AER correctable 로 보완.
- Retrain 은 루트 포트에서: LT==0 → LBMS/LABS W1C → RL(RMW) → LT==0 대기(1 s) → DLLLA·속도·폭·LBMS. RL 직후 값은 retrain 이전 값.
- 스트레스 리포트 = 시그니처별 count · first · last · PERSISTENT/INTERMITTENT · 예시 lnksta/ltssm, 고착이면 즉시 중단. 0/N 은 실패율 0 이 아니라 ≈ 3/N 상한.

## 참고자료

- PCI Express Base Specification — 7.5.3 PCI Express Capability Structure (Link Capabilities / Control / Status / Control 2 / Status 2), 4.2.6 LTSSM, 6.6 Link Reset 과 config 요청 전 대기 시간 (절 번호는 판에 따라 다름)
- PCI Local Bus Specification 3.0 — Capabilities List (0x34, Status bit 4)
- [Linux `include/uapi/linux/pci_regs.h` — PCI_EXP_LNKCAP / LNKSTA / LNKCTL2 비트 정의](https://github.com/torvalds/linux/blob/master/include/uapi/linux/pci_regs.h)
- [Linux `drivers/pci/pci.c` — __pci_find_next_cap_ttl, pci_find_next_ext_capability, pcie_retrain_link](https://github.com/torvalds/linux/blob/master/drivers/pci/pci.c)
- [Linux `drivers/pci/controller/dwc/pcie-designware.h` — enum dw_pcie_ltssm](https://github.com/torvalds/linux/blob/master/drivers/pci/controller/dwc/pcie-designware.h)
- [Linux `drivers/pci/quirks.c` — quirk_enable_clear_retrain_link](https://github.com/torvalds/linux/blob/master/drivers/pci/quirks.c)
- [pciutils — lspci(8), setpci(8)](https://mj.ucw.cz/sw/pciutils/)
- Linux sysfs ABI `Documentation/ABI/testing/sysfs-bus-pci` — current_link_speed / current_link_width / max_link_speed / max_link_width
- 관련 레슨: SIV-B09 (poll-with-timeout, 가짜 시계), SIV-B13 (pre/post silicon 재사용), SIV-B14 (리포트), SIV-M06 (BER 신뢰구간), SIV-M08 (PVT 코너), C-M02 (W1C), PCIE 트랙
