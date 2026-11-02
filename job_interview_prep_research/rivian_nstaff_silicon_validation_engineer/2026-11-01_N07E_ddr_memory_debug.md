# N07E · DDR / LPDDR 메모리 디버그 — 실패 패턴 읽기
<!-- theme: pastel -->

> **묶음**: N07 인터페이스 디버그 시나리오 · **우선**: P0 ★ · **목록**: [N07 인덱스](2026-11-01_N07_interface_debug_scenarios.html) · **이전/다음**: [N07D UART, GPIO, 인터럽트](2026-11-01_N07D_uart_gpio_irq_debug.html) / [N07F Ethernet](2026-11-01_N07F_ethernet_debug.html) · **관련**: [BV09 DDR training, memtest, ECC](2026-11-01_BV09_ddr_bringup_training_memtest.html) · [BV13 S6 training 실패, S7 고온](2026-11-01_BV13_board_debug_scenarios.html) · [N06D D2 DDR 테스트 플랜](2026-11-01_N06D_plans_dma_memory.html) · [V1 March 코딩 드릴](hmScreenPrep/problems/V1_march_memtest/index.html) · [BV12 margining, shmoo](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · [LP08 DMA, 캐시 일관성](2026-10-31_LP08_dma_iommu_coherency.html) · [N06H 디버그 방법론](2026-11-01_N06H_debug_methodology_triage.html) · **코드**: [onsitePrep/n07/e/ddr_fail_decode.c](onsitePrep/n07/e/ddr_fail_decode.c) · [run_output.txt](onsitePrep/n07/e/run_output.txt) · **작성**: 2026-11-01

## 0. TL;DR

- 이 페이지로 답할 것: **"Memtest reports random bit errors. Where's the fault?"**, "bit 13만 실패한다", "byte lane 하나만 간헐", "상위 2 GB가 하위 2 GB의 복사본", "고온에서만 CE", "같은 주소 CE 수천 번", "training 통과인데 주 1회 패닉". BV09가 **테스트를 어떻게 짜고 training을 어떻게 읽나**라면, 여기는 **실패 기록(주소 · 기대값 · 실제값)을 보고 고장 위치를 추론**하는 기술.
- **외울 것 ① XOR 한 줄**: `xor = expected ^ actual` → 켜진 비트 = 틀린 비트. **logical bit → 물리 DQ로 접기** (`dq = bit % 버스폭`, 64-bit word가 x32 버스면 bit 13과 bit 45는 **같은 DQ13**) → **byte lane = dq / 8 = DQS 그룹**. 방향(0→1만 / 1→0만 / 섞임)이 stuck-at vs 타이밍을 가른다.
- **외울 것 ② 모양 → 계층**: **한 DQ · 한 방향 · 모든 주소** = 핀(전기) · **한 lane 안 여러 비트 · 양방향** = 그 lane의 DQS/VREF/ODT 마진(타이밍) · **xor가 2의 거듭제곱 주소 차이** = 주소 비트 aliasing(설정 또는 주소선) · **한 주소 한 비트 반복** = 셀 하드 고장 · **여러 lane에 흩어짐 + 온도·시간 의존** = refresh/retention 또는 전역 마진 · **캐시라인/버퍼 경계 단위** = SW(캐시·DMA).
- **외울 것 ③ 손잡이 4개**: **다시 읽기**(값이 변하면 read 경로, 고정이면 저장된 값이 틀림 = write/셀) · **주파수 한 단계 낮추기**(사라지면 마진) · **refresh 2×**(사라지면 retention) · **캐시 off / non-cacheable 버퍼**(사라지면 SW 일관성 의심).
- 실패 해석 전 **반드시 고정할 가정 2개**: 컨트롤러 **주소 맵**(어느 주소 비트가 channel/rank/bank/row/col인지 — controller의 address-map 레지스터) + 보드 **DQ swizzle 표**(logical bit ↔ DRAM 볼). 이 페이지의 예시 맵은 **명시된 mock 가정**이다.
- ECC 쪽 증거는 Linux **EDAC**: `mc*/ce_count` · `ue_count` · `dimm*/dimm_ce_count`, `rasdaemon`의 `ras-mc-ctl --summary/--errors` [1][5]. CE는 "괜찮다"가 아니라 **추세와 위치**를 봐야 하는 신호.
- 코드: **DDR 실패 로그 디코더(C)** — mock 로그 6개(깨끗한 대조군 + 결함 주입 5종)를 읽어 DQ 히스토그램 · lane · 방향 · 주소 aliasing · bank/row/col · 온도 의존을 출력하고 기대 진단과 비교 → **6/6 PASS** (§8).
- **30초 영어 답 (headline)**: *"Random-looking bit errors usually aren't random once you XOR expected against actual. I fold every failing bit onto the physical DQ pin using the bus width and the board swizzle, then look at three things: how many DQs and byte lanes are involved, whether the flips go one direction or both, and whether they track address, temperature, or time. One DQ stuck one way on every address is a pin or solder problem; several bits inside one byte lane flipping both ways is margin on that lane's strobe group; a power-of-two address difference is aliasing; and errors spread across lanes that grow with temperature and dwell time point at refresh. Then I turn one knob — re-read, drop frequency, double refresh, or disable caches — to confirm the layer before I touch the board."*

## 1. 🔰 1분 설명

> 비유: **아파트 단지 우편 배달**. 주소(동 · 층 · 호)는 **bank · row · column**, 편지 내용은 **데이터**, 배달원 8명이 한 조(**byte lane**)로 움직이고 조마다 **조장(DQS 스트로브)** 이 "지금 받아!"라고 박자를 준다. 편지가 틀리게 도착했을 때 **보낸 편지와 받은 편지를 겹쳐 형광펜으로 다른 글자만 칠하는 것**이 XOR 분석. 형광펜이 **늘 같은 배달원 자리**에만 있으면 그 배달원(핀)이 문제, **한 조 전체에 흩어져** 있으면 조장 박자(타이밍), **다른 집 편지가 통째로** 와 있으면 주소 체계(aliasing), **오래 묵힌 편지만** 글씨가 바랬으면 보관(refresh) 문제.

- 정확한 정의: **실패 패턴 분석** = 메모리 테스트·ECC·커널 로그가 남긴 `(주소, 기대값, 실제값, 조건)` 기록을 ① XOR로 틀린 비트를 뽑고 ② 버스폭·swizzle로 **물리 DQ/byte lane**에 매핑하고 ③ 주소 맵으로 **channel/rank/bank/row/col**에 매핑하고 ④ 온도·시간·패턴·부하 조건과 상관을 봐서 **고장이 전기 · 타이밍 · 설정 · 셀 · SW 중 어디에 있는지** 고정하는 과정.
- DRAM 구조(controller ↔ PHY ↔ DRAM, training, 주소 계층)는 [BV09 §1](2026-11-01_BV09_ddr_bringup_training_memtest.html)에 그림이 있으니 여기선 **"실패 비트가 어디서 왔나"** 경로만 그린다.

```text
 CPU / DMA 64-bit word  (offset 0x...48, expected 0x..., actual 0x...)
        │  bit 63 ........................ 32 │ 31 ........................ 0
        │        beat 1 (두 번째 전송)          │        beat 0 (첫 전송)            ← x32 버스면 word 하나 = 2 beat
        ▼
 Memory controller  ──(주소 맵: offset → ch / rank / bank / row / col)──┐   ECC 인코더 (inline 또는 sideband)
        │                                                              │
        ▼  DFI                                                         ▼
 DDR PHY   byte lane 0      byte lane 1      byte lane 2      byte lane 3
           DQ0-7 + DQS0     DQ8-15 + DQS1    DQ16-23 + DQS2   DQ24-31 + DQS3     ← lane마다 독립 training(지연·VREF)
           (+DM/DBI0)       (+DM/DBI1)       (+DM/DBI2)       (+DM/DBI3)
        │  PCB / 패키지 배선 — 보드 swizzle: lane 안 비트 순서·lane 순서가 DRAM 볼과 다를 수 있음
        ▼
 DRAM die(s)   CA 버스(명령·주소) + CK  ──▶ bank × row × column 셀 배열 (커패시터 → refresh 필요)

  logical bit b  →  dq = b mod 32  →  lane = dq / 8   (예: b45 → DQ13 → lane 1 · b59 → DQ27 → lane 3)
```

| 용어 | 풀네임 | 쉬운 설명 | 디버그에서 왜 중요 |
|---|---|---|---|
| XOR 마스크 | expected XOR actual | 틀린 비트만 1인 값 | 모든 해석의 출발점 — 비트 수·위치·방향 |
| DQ | Data (Q) line | 데이터 핀 1개 | 한 DQ에만 몰리면 핀/배선/솔더 |
| DQS | Data Strobe | lane마다 있는 "지금 샘플" 박자 신호 | 한 lane 전체가 흔들리면 DQS 그룹 마진 |
| byte lane | DQS group | DQ 8개 + DQS + DM/DBI 한 묶음 | training·마진·배선이 lane 단위 |
| beat / burst | burst length (BL) | 한 번 명령에 연속 전송하는 박자 수 (LPDDR4 BL16) | 64-bit word의 위·아래 절반이 다른 beat → 같은 DQ가 두 번 나타남 |
| swizzle | DQ bit/byte swap | 보드 배선 편의로 비트·바이트 순서를 바꾼 것 | logical bit 번호 ≠ DRAM 볼 번호, 물리 위치를 말하려면 표 필요 |
| DM / DBI | Data Mask / Data Bus Inversion | 바이트 쓰기 마스크 / 0 많은 바이트를 뒤집어 보내는 기능 | DBI 핀 고장 = **바이트 8비트가 통째로 반전**, DM 고장 = 이전 값이 남음 |
| stuck-at | stuck-at-0 / -1 | 비트가 한 값에 고정 | 방향이 한쪽뿐 → 전기적 고정 |
| aliasing | address aliasing | 서로 다른 두 주소가 같은 셀 | 주소 비트 하나가 디코드 안 됨 |
| 주소 맵 | address map | offset 비트 → ch/rank/bank/row/col 배정 | bank/row 상관을 보려면 필수, controller 레지스터로 정해짐 |
| CE / UE | Corrected / Uncorrected Error | ECC가 고친 오류 / 못 고친 오류 | CE 위치·추세 = 조기 경보, UE = 데이터 손실 |
| retention | data retention | 셀 전하가 새기 전 버티는 시간 | 고온·긴 대기에서만 1→0 (true cell 기준) |
| row hammer | RowHammer | 한 row를 반복 활성화하면 옆 row 비트가 뒤집힘 | 특정 row 인접에만 몰린 오류 [9] |

## 2. 정상 기준선

### 2.1 정상이면 보이는 것 (증거 4종)

| 증거 | 정상 모양 | 근거/주의 |
|---|---|---|
| training 결과 | 모든 단계 PASS, lane별 eye 폭이 비슷하고 가이드라인 이상, 보드 간 분포 좁음 | 읽는 법은 [BV09 §3.2 · §3.4](2026-11-01_BV09_ddr_bringup_training_memtest.html) — 여기선 반복 안 함 |
| bare-metal 테스트 | data bus → address bus → March C-(배경 여러 개) 0 오류, 캐시 off | 순서 어기면 오진 ([BV09 §6.1](2026-11-01_BV09_ddr_bringup_training_memtest.html) 실측) |
| OS 레벨 테스트 | `memtester`가 모든 하위 테스트 `ok`, mlock 성공 | 실패면 `FAILURE: 0x… != 0x… at offset 0x….` 형식으로 찍힌다 [7] |
| ECC 카운터 | `ce_count` 0 또는 매우 낮고 **증가하지 않음**, `ue_count` 0 | 시험 구간마다 `reset_counters`, 절대값보다 **증가율** [1][2] |

### 2.2 정상 vs 이상 로그 한 쌍

`[예시 출력]` — memtester 정상 / 실패 (실패 줄 형식은 memtester 소스 `compare_regions`·`test_stuck_address`의 printf 형식 [7]):

```text
$ sudo memtester 1G 1                      # 정상
  Stuck Address       : ok
  Random Value        : ok
  Compare XOR         : ok
  ...
$ sudo memtester 1G 1                      # 실패
  Stuck Address       : ok
  Random Value        : FAILURE: 0x9531985d5d9dc9f8 != 0x9531b85d5d9de9f8 at offset 0x892f9028.
  Compare XOR         : FAILURE: 0x1600a35a099950d8 != 0x1600a35a099970d8 at offset 0x81e74ef0.
```

- 실패 줄을 손으로 XOR: `0x9531985d5d9dc9f8 ^ 0x9531b85d5d9de9f8 = 0x0000200000002000` → **bit 45와 bit 13** = x32 버스에서 **둘 다 DQ13** (beat 1과 beat 0). 이 두 줄이 §8 M1 mock 로그의 첫 두 레코드다.
- memtester 주의: 기본은 **가상 주소의 offset** → 물리 주소·bank/row로 바꾸려면 `-p physaddr` 모드나 `/proc/self/pagemap` 변환이 필요 (BV09 출처 19). bare-metal 테스트가 진단에 더 정확한 이유.

### 2.3 정상 숫자 (해석에 쓰는 것만)

| 항목 | 값 | 근거 |
|---|---|---|
| DDR4 refresh 간격 tREFI | 7.8 µs (0–85 °C), 확장 온도 3.9 µs (2×) | BV09 §2.5 · 출처 15 [확인됨 — Intel 안내], JEDEC 원문 미열람 |
| LPDDR4 refresh window | 32 ms, MR4가 온도에 따라 0.5×–4× 배율 신호 | BV09 출처 15 [확인됨] |
| SECDED | 64 data + 8 check, 1비트 정정 · 2비트 검출 | BV09 §2.4 [확인됨] |
| RowHammer 임계 (2014 DDR3 실측) | 한 row 약 139K회 접근으로 인접 row 오류, 시험 모듈 129개 중 110개 | [9] [확인됨 — 논문 요약]; 최신 공정은 더 낮음 [추정] |
| x32 버스 + 64-bit word | word 하나 = 2 beat, logical bit b와 b+32는 같은 DQ | 버스폭 정의에서 계산 |

### 2.4 정상 EDAC 스냅샷

`[예시 출력]` — ECC 켠 보드에서 기대하는 모양 (파일 이름은 kernel RAS 문서·ABI 기준 [1][2]; VM 실측은 BV09 §3.5에 있음 — VM엔 mc0이 없다):

```text
$ grep . /sys/devices/system/edac/mc/mc0/{ce_count,ce_noinfo_count,ue_count,ue_noinfo_count,seconds_since_reset}
/sys/devices/system/edac/mc/mc0/ce_count:0
/sys/devices/system/edac/mc/mc0/ce_noinfo_count:0
/sys/devices/system/edac/mc/mc0/ue_count:0
/sys/devices/system/edac/mc/mc0/ue_noinfo_count:0
/sys/devices/system/edac/mc/mc0/seconds_since_reset:86400
$ ras-mc-ctl --error-count          # sysfs 기준 CE/UE 개수 [5]
$ ras-mc-ctl --summary              # rasdaemon DB에 쌓인 오류 요약 [5]
No Memory errors.                   # [예시 출력] — 버전마다 문구 다름 [추정]
```

- `*_noinfo_count`가 오르면 **위치 정보 없는 오류** — 위치 분석이 불가능하니 드라이버/펌웨어 보고 경로부터 의심.

## 3. 관찰 지점 지도

| 무엇 | 어디서 | 도구 | 정상값 | 이상하면 의미 |
|---|---|---|---|---|
| 실패 레코드 (주소 · 기대 · 실제) | bare-metal 테스트 로그(UART), memtester 출력 | 디코더(§8), 수작업 XOR | 없음 | XOR 모양으로 계층 결정 (§3.3) |
| per-lane / per-bit training 결과 | PHY training 결과 레지스터 덤프 | 부트 FW 로그, JTAG | lane 간 비슷, 가이드 이상 | 실패 lane과 eye 좁은 lane이 같으면 확정 증거 |
| ECC CE/UE 총계 | `/sys/devices/system/edac/mc/mc*/ce_count`, `ue_count` | `grep`, `ras-mc-ctl --error-count` | 0, 증가 없음 | 증가율이 온도·부하와 상관 → 마진, 한 곳 반복 → 셀 |
| DIMM/rank별 CE | `mc*/dimm*/dimm_ce_count` (또는 `rank*`) [1] | sysfs | 0 | 한 rank/DIMM만 → 그 소자·CS |
| CE 위치 · 비트 | controller CE 주소/syndrome 레지스터 → 커널 로그 | `dmesg`, `ras-mc-ctl --errors` | 없음 | 예: Synopsys 드라이버는 `Row · Bank · BankGroup · Bit Position · Data`를 찍는다 [3] |
| 위치 정보 없는 오류 | `mc*/ce_noinfo_count`, `ue_noinfo_count` | sysfs | 0 | 보고 경로 문제 → 위치 분석 불가 |
| 주소 맵 | controller address-map 레지스터 (예: Synopsys DDRC ADDRMAPx — Linux `synopsys_edac`가 poison 설정용으로 읽음 [3]) | JTAG/devmem 덤프, 벤더 툴 | 설계 문서와 일치 | 밀도·rank·interleave 설정 오류 = aliasing |
| DRAM 실제 밀도·폭 | LPDDR: mode register read(MR8 류 기본 구성) / DDR4 DIMM: SPD | 부트 FW 로그 | 설정값과 같음 | 다르면 aliasing·용량 오인 [추정 — MR 번호는 JEDEC 원문 확인] |
| DRAM 온도 상태 | LPDDR4 MR4 (refresh 배율) | 부트 FW / 주기 polling 로그 | 온도 따라 변함 | 고온인데 1× 그대로 → derating 미동작 |
| 레일 | VDD2 / VDDQ (DRAM 근처 decoupling) | scope (AC 결합, droop 트리거) | spec 안, ripple 작음 | 부하 시 droop와 오류 시각 일치 → PDN |
| 온도 | SoC·DRAM 온도 센서 + 열전대 | 로그 | 챔버 + 자기 발열 | 오류율 vs 접합 온도 곡선 |
| 페이지 격리 | `/sys/devices/system/memory/soft_offline_page` [4] | rasdaemon `PAGE_CE_ACTION` [6] | 사용 안 함 | 같은 페이지 반복 CE면 운영 대책 |

### 3.1 첫 5분 증거 스냅샷 (순서대로)

1. **조건 기록**: 보드 SN · DRAM 부품/lot · DDR 속도 · 온도 · 전압 · FW hash · 테스트 이름과 데이터 배경.
2. **실패 레코드 20개 이상 확보** (첫 실패에서 멈추는 테스트면 "계속 진행" 모드로) — 하나로는 패턴이 안 보인다.
3. **XOR → DQ fold → lane → 방향** 표 (손이든 §8 디코더든).
4. **같은 주소 다시 읽기 3회** — 값이 흔들리나(read 경로) 고정인가(저장값).
5. EDAC: `grep . /sys/devices/system/edac/mc/mc*/{ce_count,ue_count,ce_noinfo_count}` + `dmesg | grep -i edac` + `ras-mc-ctl --summary`.
6. training 결과 덤프에서 **실패 lane의 eye 폭**과 다른 lane 비교.
7. 주소 맵 레지스터 덤프 + 설정한 DRAM 밀도·rank 수 vs 실장 부품 마킹.

### 3.2 이 페이지의 주소 맵 가정 (mock)

```text
 [예시 주소 맵 — mock, 실제는 controller address-map 레지스터 + interleave 설정]
 4 GB, x32 버스, BL16 → burst 1회 = 32 bit × 16 = 64 B

 offset bit:  31 ............ 19 | 18  17  16 | 15 ............. 6 | 5 ...... 0
              row (13 bit)       | bank (3)   | column/burst (10)  | byte in burst (beat = [5:2], lane = [1:0])
```

- 실제 SoC는 **channel/rank 비트와 bank 비트를 XOR 해시**하거나 낮은 비트로 interleave 하는 경우가 많다 [추정] — 그래서 "bank 3만 실패"라고 말하려면 **반드시 그 보드의 맵을 먼저 확인**하고, 면접에서도 "assuming a linear row-bank-column map"처럼 가정을 말한다.

### 3.3 실패 로그 해석 절차 — XOR 7단계 ★

1. **xor = expected ^ actual**. popcount가 1이면 단일 비트, 8개가 한 바이트에 몰려 있으면 바이트 통째(DBI/DM/칩 없음), 거의 전부면 주소 문제 또는 버스 전체.
2. **fold**: `dq = bit mod 버스폭` → `lane = dq / 8`. logical bit 13과 45가 둘 다 나오면 같은 핀이라는 강한 증거; **13만 나오고 45는 절대 안 나오면 핀이 아니다**(beat 하나만 → 데이터 경로 로직·캐시·테스트 패턴 쪽).
3. **방향**: 0→1만 / 1→0만 = stuck(전기) 또는 retention(1→0, true cell 기준), 섞임 = 샘플 타이밍.
4. **주소 상관**: address-as-data(각 주소에 자기 주소를 쓴 테스트)면 `expected ^ actual` 자체가 **주소 차이** → 2의 거듭제곱이면 그 주소 비트 aliasing. 일반 패턴이면 실패 주소들의 공통 비트/차이 비트를 본다.
5. **bank/row/col 디코드**(명시한 맵으로): 한 row 주변(±1, ±2)만 → row hammer/약한 row, 한 bank만 → bank 로직·tRC 계열 타이밍, 한 column 비트 패턴 → column 디코더, 전부 흩어짐 → 핀·lane·전역.
6. **조건 상관**: 온도(임계 온도 "벽"), 쓰고 읽기까지 시간(age), 시각(버스트로 몰리나), 데이터 패턴(모든 DQ 동시 토글에서만 → SSN/PDN), 부하(다른 master 동시), 전원 상태 전환(self-refresh/DFS 직후).
7. **가설 하나 → 손잡이 하나**로 확인 (다시 읽기 · 주파수 · refresh 2× · VDDQ ± · 캐시 off · 다른 보드/DRAM swap) — 그 다음에야 보드 재작업.

## 4. 증상 → 원인 한 장 표

| 증상 (실패 패턴) | 계층 | 의심 원인 (순위) | 첫 확인 | 확정 방법 |
|---|---|---|---|---|
| 한 DQ 비트가 모든 주소에서 항상 1 (또는 0) | 전기 | ① 그 DQ open/short(솔더) ② DRAM/SoC IO 손상 ③ 종단 때문에 open이 한 값으로 읽힘 | fold 후 DQ 1개 · 방향 1개 확인 | 저속에서도 실패, X-ray/boundary scan, DRAM swap |
| logical bit 하나만, 같은 DQ의 다른 beat 비트는 정상 | 설정·드라이버 / 시스템 | ① 데이터 경로 로직(ECC 디코더·버스 브리지) ② 캐시/테스트 버그 ③ burst 순서 | 버스폭으로 fold해 보기 | DMA vs CPU 경로 비교, 캐시 off |
| 두 DQ가 항상 같은 값으로 틀림 | 전기 | 인접 DQ short | 두 비트를 다르게 쓰는 배경(0x55/0xAA) | data bus walking-1s, 저속 재현 |
| 한 byte lane 안 여러 비트, 양방향, 간헐 | 타이밍 | ① 그 lane DQS 마진(read gate/eye) ② lane VREF ③ lane ODT/drive ④ 그 lane 솔더 marginal | training 결과에서 그 lane eye 폭 | 주파수↓ 사라짐, lane VREF/지연 sweep |
| 한 바이트 8비트가 통째로 반전 (xor 바이트 = 0xFF) | 전기 / 설정 | ① DBI 핀 고장 또는 DBI 설정 불일치 ② 그 바이트 칩 없음/미응답 | DBI on/off 비교 | DBI off로 사라지면 DBI 경로 |
| 한 바이트가 "이전 값" 그대로 (stale) | 전기 / 설정 | DM(쓰기 마스크) 핀 고정 · 마스크 설정 | actual 바이트 = 직전 배경 값인지 | 마스크 쓰기 테스트, DM 프로브 |
| 주소 X와 X + 2^k가 같은 데이터 | 설정 / 전기 | ① 밀도·row/col 비트 설정 불일치 ② NoC/주소 창 크기 설정 ③ 주소선(DDR4 A핀) open | address-as-data, xor = 2^k | 맵 레지스터 vs 부품, 맵 바꾸면 alias 비트 이동 |
| 상위 절반 = 하위 절반 복사본 | 설정 | 최상위 row/rank 비트 미디코드 · 용량 설정 2배 | 최상위 비트 alias 확인 | MR/SPD로 실제 밀도 확인 |
| 한 rank(CS)만 실패 | 전기 / 설정 | CS/CKE/ODT 핀 · rank 타이밍 · rank별 training 값 | 실패 주소의 rank 비트 | rank별 training 결과, rank swap |
| 한 channel만 | 전기 / 타이밍 | 그 채널 CK/CA · 채널 레일 · 채널 training | 실패 주소의 channel 비트 | 채널별 margin, 채널 interleave off |
| 특정 row 인접에만, 접근 많은 row 옆 | 셀 / 시스템 | ① RowHammer ② 약한 row ③ 완화 기능(TRR/RFM 류) 미동작 [추정] | 실패 row ±1, ±2와 hot row 관계 | hammer 패턴 재현, refresh 2×로 감소 |
| 고온에서만, 1→0 위주, 오래 묵힌 데이터 | 셀 / 설정 | ① refresh derating 미적용(MR4 무시) ② 약한 retention 셀 ③ 고온 마진 | 온도별 실패 수 · age 상관 | refresh 2× 강제 → 사라짐 |
| 고온에서만, 양방향, 특정 lane | 타이밍 | 상온 training 값 drift, 주기 보정 off | 고온 부팅 vs 상온 부팅 후 가열 | 주기적 ZQ/retraining on |
| self-refresh exit 후만 | 설정·드라이버 | ① exit 시퀀스/타이밍 ② PHY 상태(training 값) 복원 누락 ③ suspend 중 레일 off ④ 캐시 flush 누락(SW) | 손상 단위(비트 vs 캐시라인 vs 페이지) | suspend 루프 + 페이지별 CRC |
| DFS 주파수 전환 후만 | 설정·드라이버 | FSP별 training 값/MR 세트 불일치, 전환 중 트래픽 | 어느 주파수 쌍에서 | 각 주파수 고정 시 정상인지 |
| CPU 단독 OK, DMA/NPU/ISP 동시 부하에서 실패 | 시스템 / 전기 | ① PDN droop·SSN ② 온도 상승 ③ DMA 캐시 일관성(SW) ④ interconnect 버그 | 오류 단위 + 레일 droop 시각 | 합성 부하 재현, non-cacheable 버퍼, VDDQ +3% |
| 캐시 켜면 사라짐 / 생김 | 설정·드라이버 / 시스템 | 생김: 일관성·flush 누락, 캐시라인 단위 / 사라짐: 테스트가 캐시만 때림 | 오류 크기가 64 B 배수인가 | 캐시 maintenance 추가, 캐시 off 재시험 |
| 같은 주소 같은 비트 CE 반복 | 셀 | 하드 셀 고장(ECC가 가려 줌) · 드물게 EDAC 중복 보고 | `ras-mc-ctl --errors` 위치 | 그 주소에 반대 값 쓰고 읽기, page offline |
| CE가 여러 주소·lane에 퍼짐, 시간 따라 증가 | 타이밍 / 전기 | 전역 마진 · 온도 · 전원 | CE 증가율 vs 온도/부하 | 주파수↓·VDDQ↑로 감소 |
| UE 후 커널 패닉 | 시스템 | 같은 word 2비트(셀 + 마진), 누적 CE 방치, scrub off | 직전 CE 이력 같은 위치? | `edac_mc_panic_on_ue` 정책, scrub rate 확인 |
| training 통과인데 Linux가 부하에서 랜덤 crash | 시스템 | ① 마진(패턴·온도) ② 주소 맵/interleave 설정 ③ ECC off로 무증상 손상 ④ SW 버그 | panic PC가 흩어졌나 한 곳인가 | 주파수↓로 MTBF 변화, ECC on |
| warm reset 후만 | 설정·드라이버 | 리셋 시 DRAM 상태 미초기화, training 재사용 값 부정확 | cold vs warm 비교 | warm reset 루프 + 매번 training 결과 기록 |

## 5. 시나리오 드릴

### E1 · memtest가 bit 13만 실패

#### 증상

```text
[예시 출력]  bare-metal random-value 테스트, 4 GB, 25 °C, 실패 계속 진행 모드
FAIL off=0x0892f9028 exp=0x9531985d5d9dc9f8 act=0x9531b85d5d9de9f8 xor=0x0000200000002000
FAIL off=0x081e74ef0 exp=0x1600a35a099950d8 act=0x1600a35a099970d8 xor=0x0000000000002000
FAIL off=0x06f036758 exp=0x3d9c172411e20b8f act=0x3d9c372411e22b8f xor=0x0000200000002000
... (모든 bank, 모든 row에서)
```

- 팀은 "bit 13 fails"라고 보고하지만 XOR을 보면 **bit 13과 bit 45**가 섞여 나오고, 항상 **0→1**.

#### 먼저 물어볼 것

- *"What's the data bus width per channel, and is this a 64-bit CPU word test?"* (fold 계산용)
- *"Does the bit ever flip 1-to-0, or only 0-to-1?"*
- *"Is it every address, or one region, rank or channel?"*
- *"Same failure on other boards with the same DRAM part? And at the lowest speed bin?"*

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① DQ13 핀 open/short (BGA 솔더 · 배선) | 한 DQ · 한 방향 · 전 주소 = 전기적 고정의 교과서 모양 | 저속 training/테스트, X-ray, boundary scan 연결 테스트 | 최저 속도에서도 동일, 다른 보드는 정상 |
| ② DRAM 또는 SoC 쪽 IO 손상(ESD) | 같은 모양 | DRAM swap(재볼링) 또는 SoC swap | 부품을 따라감 |
| ③ training이 DQ13을 극단 지연값으로 "통과"시킴 | per-bit deskew가 범위 끝 | per-bit deskew 덤프 | DQ13 지연값만 탭 범위 끝, eye 0에 가까움 |
| ④ 핀이 아님 (데이터 경로 로직·테스트) | 만약 bit 45가 한 번도 안 나온다면 | fold 결과 확인, DMA 경로로 같은 테스트 | 13만 실패 + 45 정상 → 핀 아님 |

#### 측정 · 계측

- **fold 먼저**: x32 버스 → DQ13 = lane 1 (DQ8–15, DQS1). bit 45도 실패 = 두 beat 모두 같은 핀 → ①–③.
- **방향 해석**: 0→1만 = 수신단에서 이 선이 늘 High로 보임. 종단이 VDDQ 쪽인 인터페이스(DDR4 POD)에서는 **open 핀이 1로 읽히기 쉽고**, VSSQ 쪽 종단(LPDDR4/4X LVSTL)에서는 0으로 읽히기 쉽다 [추정 — 종단 방식에서 추론, JEDEC 원문 미열람]. 즉 방향이 **open인지 short인지**, 어느 레일에 붙었는지의 힌트.
- write 문제인지 read 문제인지: training 결과의 **DQ13 write deskew vs read deskew** 비교, 또는 DRAM의 read-only 경로(MPC/MR 읽기 패턴, 지원 시)로 확인 [추정].
- 물리 위치: **swizzle 표로 DQ13 → DRAM 볼 번호**를 찾고, 패키지 밖 배선이면 그 볼 근처 via에 프로브 (PoP/패키지 내 메모리면 불가 → boundary scan · X-ray).

```text
 정상 DQ13 (read, 수신단)          DQ13 open + 상단 종단 (bad)
   ┌─┐ ┌───┐   ┌─┐                 ─────────────────────────   ← 늘 High 근처, 토글 없음
 ──┘ └─┘   └───┘ └──                (주변 DQ crosstalk로 작은 bump만 보임)
```

#### root cause · fix

- 예: DRAM 쪽 DQ13 볼 **head-in-pillow 솔더 open** (X-ray/단면). Fix = 그 보드 리워크, 공정 측은 리플로우 프로파일·패드 설계 피드백. 여러 보드에서 같은 볼이면 **패드/via 설계**.

#### 회귀로 굳히기

- 부팅마다 **channel × lane별 data bus walking-1s/0s**(0.2N, 수 µs) + 실패 시 **"ch/lane/DQ 번호"로 바로 출력** (logical bit가 아니라). 생산 테스트에도 같은 항목 → 솔더 open을 출하 전에 잡음.

#### 30초 영어 답

*"First I'd XOR expected and actual and fold the failing bits onto physical DQs using the bus width. Here bit 13 and bit 45 both fail, and on a 32-bit bus those are the same pin, DQ13 in byte lane 1, in two different beats — so it's one physical line, not a logic bug. It only goes 0-to-1 and it's on every address, which says it's stuck rather than marginal. I'd confirm it fails at the lowest speed too, check the per-bit deskew from training, then use the swizzle table to find the ball and look at it with X-ray or boundary scan. And I'd add a per-lane walking-ones test at boot that reports the DQ number directly."*

#### 꼬리 질문

**Q1. What if only bit 13 fails and bit 45 never does?**

<details><summary>답</summary>

*"Then it's not the pin, because the same DQ carries bit 45 in the next beat. I'd look at anything that sees the 64-bit word as a whole: an ECC decoder or a bus bridge bit, a cache bug, or the test itself. Running the same pattern through a DMA engine instead of the CPU separates the data path from the DRAM."*
- 한국어 핵심: **같은 DQ의 다른 beat가 멀쩡하면 핀이 아니다** → 데이터 경로·로직.

</details>

**Q2. Could a wrong swizzle setting cause this?**

<details><summary>답</summary>

*"Not usually as a data error — bit swizzle within a byte is applied the same way on writes and reads, so it's transparent to normal data. It matters for training feedback, mode-register reads, DBI and ECC lane mapping, and it matters to me for locating the physical ball. A wrong swizzle more often shows up as a training failure on one lane."* [추정]
- 한국어 핵심: swizzle은 보통 데이터엔 투명, **training·MR 읽기·위치 찾기**에서 문제.

</details>

**Q3. Why did training pass if the pin is stuck?**

<details><summary>답</summary>

*"Depending on the PHY, training may report per-lane results and a single dead bit can still let the lane pass with a skewed or default value, or the error is in a step that only checks one feedback DQ. That's why I always run a data-bus test after training — training finding a working point is not proof every line is connected."* [추정 — PHY마다 다름]
- 한국어 핵심: training PASS ≠ 모든 선 연결, **training 뒤 bus test 필수**.

</details>

### E2 · byte lane 2 전체 간헐

#### 증상

- 1시간 랜덤 copy 스트레스, 45 °C: 실패 32건이 **DQ16–23(lane 2)** 에만, 8개 비트 모두, **0→1과 1→0 섞임**, 시간상 **4번의 뭉치(버스트)** 로 몰림 (§8 M2).

```text
[예시 출력]  decoder 요약
   folded DQ   : DQ16:8(+3/-5) DQ17:8(+5/-3) DQ18:9(+2/-7) ... DQ23:8(+5/-3)
   byte lanes  : L0:0 L1:0 L2:56 L3:0
   time        : span=2048s  bursts(gap>60s)=4
```

#### 먼저 물어볼 것

- *"Is it the same lane on every board, or only this board?"* (설계 vs 조립)
- *"If you re-read a failing address, does the value change?"* (read 경로 vs 저장값)
- *"What else is happening when the bursts occur — temperature steps, DFS, other masters, periodic calibration?"*
- *"What did training report for lane 2's read and write eye compared with the other lanes?"*

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① lane 2 read 마진 (DQS2 gate/eye drift) | lane 하나, 양방향, 간헐 = 그 strobe 그룹 타이밍 | 다시 읽기 3회, read eye 덤프 | 다시 읽으면 값이 바뀜(정상으로 돌아오기도) |
| ② lane 2 write 마진 | 같은 모양이지만 저장값이 틀림 | 다시 읽기, write eye/VREF | 다시 읽어도 같은 틀린 값 |
| ③ lane 2 VREF/ODT 설정 | lane마다 설정 가능 | lane VREF sweep, ODT 조합 | 특정 값에서 사라짐 |
| ④ 주기적 이벤트(ZQ cal·retraining·DFS)와 충돌 | 버스트가 주기적 | 이벤트 로그와 시각 비교 | 버스트 시각 = 이벤트 시각 |
| ⑤ lane 2 솔더 marginal (한 보드) | 보드 하나만이면 | 보드 비교, 국부 가열/냉각 | 가열/냉각에 민감 |

#### 측정 · 계측

- **다시 읽기 실험**이 가장 싸다: 실패 주소를 즉시 3번 다시 읽어 `act1/act2/act3` 기록 → 값이 변하면 **read**(DQS gate/eye), 고정이면 **write 또는 셀** → write eye/VREF.
- **DBI/DM 배제**: xor가 바이트 `0xFF`로 통째면 DBI, actual 바이트가 이전 배경 값이면 DM — 이 로그는 1–3비트씩이라 둘 다 아님.
- **margin 지도**: lane 2의 read/write 지연 × VREF 2D sweep을 다른 lane과 비교 ([BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)). 다른 보드 3–5장도 → 모든 보드 lane 2가 좁으면 **배선(길이 · crosstalk · 레퍼런스 플레인)**.
- **버스트 원인**: 실패 시각을 온도 로그·DFS 로그·ZQ/retraining 이벤트·다른 master 부하와 한 그래프에.

#### root cause · fix

- 예: 모든 보드에서 lane 2 read eye가 다른 lane의 60% [예시] + 버스트가 주기적 ZQ cal 직후 → **DQS2 경로 crosstalk + 보정 후 지연값 미세 이동**. 단기: lane 2 read VREF/ODT 최적화, 주기적 DQS drift tracking 활성화(PHY 지원 시). 장기: 레이아웃 팀에 lane 2 배선 검토 요청(번호·eye 데이터 첨부).

#### 회귀로 굳히기

- 매 부팅 **lane별 eye 폭을 CSV로** 남기고 최소값 임계 미달 알람 ([BV14](2026-11-01_BV14_board_validation_plan_automation.html) 팜 패턴). 스트레스 테스트 실패 레코드는 **lane 히스토그램을 자동 첨부**.

#### 30초 영어 답

*"Every failing bit folds into DQ16 to 23, so this is byte lane 2 and its strobe group, not random. Flips go both directions, which says sampling margin, not a stuck line. The cheapest split is to re-read the failing address: if the value changes it's the read path — DQS gate or read eye — and if it's stably wrong it was written wrong or stored wrong. Then I compare lane 2's training margins against the other lanes and across several boards to separate layout from assembly, and I line up the error bursts against temperature, DFS and calibration events."*

#### 꼬리 질문

**Q1. Errors on lane 2 come in bursts every ~10 minutes. What do you suspect?**

<details><summary>답</summary>

*"Something periodic: ZQ calibration, a retraining or tracking update, a thermal control loop stepping, a DFS transition, or another master's periodic job. I timestamp both the errors and those events and see which one lines up; then I disable that one event to confirm."*
- 한국어 핵심: **주기적 버스트 = 주기적 이벤트**와 시각 맞추기.

</details>

**Q2. How would you show the fix actually added margin rather than hiding the symptom?**

<details><summary>답</summary>

*"I'd measure the lane 2 eye before and after with the same sweep, and run the stress at a stressed corner — higher temperature, lower VDDQ, or one speed bin higher — where it used to fail quickly. Zero failures at a corner that used to fail in minutes is much stronger than zero failures at nominal."*
- 한국어 핵심: **eye 폭 전후 비교 + 스트레스 corner에서 재시험**.

</details>

### E3 · 4 GB 중 상위 절반이 하위 절반의 복사본 ★

#### 증상

- Linux가 4 GB를 보고하고 부팅도 되지만 메모리를 많이 쓰면 랜덤 crash. bare-metal **address-as-data**(각 주소에 자기 offset을 씀) 테스트: 상위 2 GB의 모든 읽기가 **offset − 2 GB** 값을 돌려줌 (§8 M3).

```text
[예시 출력]
FAIL off=0x0c3a91f40 exp=0x00000000c3a91f40 act=0x0000000043a91f40   xor=0x80000000
FAIL off=0x0f2201a88 exp=0x00000000f2201a88 act=0x0000000072201a88   xor=0x80000000
... 16/16 records: xor = 0x80000000
```

#### 먼저 물어볼 것

- *"What density and rank count is the controller configured for, and what's actually populated?"*
- *"What address bit is 2 GB in this controller's map — row, rank, or channel?"*
- *"Does the SoC interconnect's DRAM window match 4 GB?"*
- *"Did training pass on all ranks and channels?"*

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① 용량/row 비트 설정이 실물보다 큼 (예: 8 Gb 부품에 16 Gb 설정) | 최상위 row 비트를 DRAM이 무시 → 정확히 절반 경계 aliasing | 부품 마킹, LPDDR MR 읽기(밀도)/DDR4 SPD, 설정값 | 실물 밀도 = 설정의 절반 |
| ② 주소 맵에서 2 GB 비트가 rank 비트인데 rank 1이 없음/CS가 묶임 | rank 비트 미디코드도 같은 모양 | 맵 레지스터, CS1 연결 | 2 GB 비트 = rank 비트 |
| ③ SoC interconnect/주소 창 크기 설정(2 GB 창에서 wrap) | DRAM 밖에서도 생김 | NoC 주소 창 레지스터, 다른 master로 접근 | 창 크기 2 GB |
| ④ 물리 주소선 open (DDR4 A핀) | 고전적 원인 | address bus test 단계별 결과 | 그 A핀에 해당하는 비트만 |

#### 측정 · 계측

- **address-as-data의 장점**: actual이 곧 "어느 주소의 데이터를 읽었나"이므로 `xor = 0x80000000` 하나로 **alias 비트 = 31**이 바로 나온다. 일반 패턴 테스트(March)는 "틀림"만 알려 주고 어디서 왔는지 모른다.
- **방향**: 상위 읽기가 하위 값을 돌려줌 = **나중에 쓴 쪽이 이김**. 이 테스트는 쓰기를 내림차순으로 했으므로 하위가 나중 → "bit 31이 디코드 안 됨"과 일치. 오름차순이면 하위 읽기가 실패했을 것 — **어느 쪽이 실패하느냐는 쓰기 순서에 달렸다**는 점을 말할 수 있으면 좋다.
- **설정 vs 배선 가르기**: 주소 맵에서 row 비트 배치를 바꿔(가능하면) 다시 돌린다 → **alias가 offset 비트를 따라 이동하면 DRAM 쪽 row 비트(밀도/핀)**, 그대로 31에 남으면 **interconnect 창**.
- LPDDR4는 주소가 **6개 CA 핀에 여러 사이클로 다중화**되어 있어서 CA 핀 하나 open은 "한 주소 비트 alias"가 아니라 **명령 자체가 깨져 CA training 실패**로 나오는 경우가 많다 [추정 — CA 다중화 구조에서 추론]. 그래서 LPDDR에서 깨끗한 단일 비트 alias는 **설정(밀도·맵·창)** 쪽 1순위.

#### root cause · fix

- 예: BOM 대체 부품으로 DRAM이 16 Gb → 8 Gb/채널로 바뀌었는데 DDR 설정은 이전 부품 기준 → 컨트롤러는 4 GB로 알고 최상위 row 비트를 내보내지만 DRAM은 무시. Fix: 설정 재생성(벤더 툴) + 부트 FW가 **MR(또는 SPD)로 읽은 밀도와 설정을 비교해 불일치면 정지**.

#### 회귀로 굳히기

- 부팅 시 **address-as-data 샘플 테스트**(2의 거듭제곱 offset마다 1 word, 수십 접근)와 **밀도 일치 검사**. BOM 변경 시 DDR 설정 리뷰를 ECO 체크리스트에 ([BV13 S12](2026-11-01_BV13_board_debug_scenarios.html) 패턴).

#### 30초 영어 답

*"That's aliasing on the 2 GB address bit. With an address-as-data test the returned value tells me which address I actually read, and every failure differs by exactly 0x80000000, so bit 31 isn't being decoded. On LPDDR a clean single-bit alias is almost always configuration: the controller thinks the part is twice as dense as it is, the rank bit points at a rank that doesn't exist, or the interconnect window is 2 GB. I'd compare the configured density with the mode-register or SPD value and the address-map registers, and move the row bits in the map to see whether the alias follows the DRAM. The fix gets a boot-time density check so it can't ship again."*

#### 꼬리 질문

**Q1. Why didn't training catch this?**

<details><summary>답</summary>

*"Training aligns timing on the data and command buses; it doesn't check that every address is unique. A missing top row bit is perfectly good timing. Only an address-uniqueness test — power-of-two offsets or address-as-data — finds it."*
- 한국어 핵심: training은 **타이밍**, aliasing은 **주소 고유성 테스트**만 잡는다.

</details>

**Q2. Why does Linux boot fine and then crash randomly?**

<details><summary>답</summary>

*"The kernel and early allocations sit in the lower half, so everything works until something allocates in the upper half; those writes silently overwrite lower-half memory that belongs to someone else. The crash location is random because the victim is whatever lived at offset minus 2 GB."*
- 한국어 핵심: 상위 할당이 **하위의 남의 메모리를 덮어씀** → 랜덤 crash.

</details>

**Q3. Your address-bus test reported 'A0 stuck' instead. Do you trust it?**

<details><summary>답</summary>

*"Not until the data bus test passes. A data-line fault or a missing byte lane can make an address test report low address lines, which we reproduced in BV09. I check the XOR shape: a full-word difference is real aliasing, a few bits is a data problem."*
- 한국어 핵심: **data bus 먼저** — xor가 일부 비트면 데이터 문제 ([BV09 §6.1](2026-11-01_BV09_ddr_bringup_training_memtest.html)).

</details>

### E4 · 105 °C에서만 ECC CE 증가

#### 증상

- 챔버 스윕 25 → 105 °C, ECC on. `ce_count` 증가율: 85 °C까지 0, **95 °C 3건, 105 °C 21건**. CE 비트가 **모든 lane에 흩어짐**, 대부분 **1→0**, bit-fade(쓰고 128–512 ms 대기 후 읽기)에서 나옴 (§8 M4).

```text
[예시 출력]
   fails/temp  : 25C:0 65C:0 85C:0 95C:3 105C:21
   byte lanes  : L0:10 L1:5 L2:3 L3:6
   direction   : 0->1=4  1->0=20
   age>=64ms   : 24/24 records
```

#### 먼저 물어볼 것

- *"Is that chamber temperature or DRAM junction temperature?"*
- *"What refresh rate is applied at that temperature — is MR4 being polled and acted on?"*
- *"Do errors depend on how long the data sat before it was read?"*
- *"Did the board boot at hot, or boot cold and ramp?"*

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① 고온 refresh 배율 미적용 (MR4 무시, derating 실패) | 고온 · 1→0 · age 의존 · lane 무관 = retention | refresh 2× 강제 | CE 0으로 떨어짐 |
| ② 약한 retention 셀 (부품 tail) | 같은 모양, 특정 주소 반복 | CE 주소 반복성, 부품 swap | 같은 주소 반복, 부품 따라감 |
| ③ 고온 타이밍 마진 | 고온 실패의 흔한 원인 | age 0 테스트, lane 분포 | age 무관, 양방향, lane 편중 (이 로그와 다름) |
| ④ 고온 레일/VREF drift | 고온에서 레귤레이터 drift | 레일 측정 | 전압이 하한 근처 |

#### 측정 · 계측

- **retention vs 타이밍 가르는 2축**: ① **age**(쓰고 읽기까지 시간)를 0, 64, 256, 1024 ms로 바꿔 실패율 — retention이면 age와 함께 증가, 타이밍이면 무관 ② **방향과 lane** — retention은 lane 무관 + 한 방향 우세, 타이밍은 lane 편중 + 양방향.
- 1→0 우세는 **true cell(1 = 충전)** 기준; 반대 극성(anti cell) 셀은 0→1로 보이므로 소수의 0→1은 정상적인 섞임 [추정].
- **MR4 로그**: 온도 상승 중 MR4가 가리키는 refresh 배율과 controller가 실제 적용한 tREFI를 같이 찍는다. **85 °C 이상에서 부팅**한 경우도 따로 (i.MX8MM ERR050805 류: 고온 시작 시 자동 derating 실패 → 고정 2× refresh 우회 [BV09 출처 15]).
- **접합 온도**: DRAM 자체 온도 센서/MR4 값 vs 챔버 — 자기 발열로 10 °C 이상 차이 날 수 있음 [추정].

#### root cause · fix

- 예: refresh 배율 갱신이 SW 몫인 플랫폼에서 polling 태스크가 비활성 → 105 °C에서도 1× refresh. Fix: MR4 polling + tREFI 조정 활성화(또는 고온 등급 제품은 고정 2×), 고온 부팅 경로 별도 처리.

#### 회귀로 굳히기

- 챔버 회귀에 **"고온 부팅" + "상온 부팅 후 가열"** 두 경로, **bit-fade age sweep**, 온도 단계마다 `ce_count` 증가율과 적용 tREFI를 같이 기록 ([N06D D2](2026-11-01_N06D_plans_dma_memory.html)의 MR4/derating 행).

#### 30초 영어 답

*"Errors that appear only above 95 degrees, spread across all lanes, mostly 1-to-0, and only on data that sat for a while look like retention, not timing. The fastest confirmation is to force double refresh: if the CEs disappear, the refresh rate isn't being derated with temperature — typically MR4 isn't polled or the controller's automatic derating failed, which also has a known boot-at-hot erratum on some SoCs. If they don't disappear and they cluster on one lane, I'd switch to the timing-margin path and retrain at temperature. Either way I'd log DRAM junction temperature, MR4 and the applied refresh interval together."*

#### 꼬리 질문

**Q1. Doubling refresh fixed it. Is that the fix?**

<details><summary>답</summary>

*"It's evidence, and it might be the fix, but I want the right mechanism: temperature-based derating working, not a blanket 2x that costs bandwidth and power at room temperature. So I'd make MR4-driven derating work, verify it across the ramp and when booting hot, and keep a fixed 2x only if the product grade requires it."*
- 한국어 핵심: 2× refresh는 **증거**, 진짜 fix는 **온도 기반 derating 동작**.

</details>

**Q2. How do you separate a weak cell from a refresh problem?**

<details><summary>답</summary>

*"A refresh problem hits many random addresses and scales with temperature for every part; a weak cell repeats at the same address and follows the DRAM part when you swap it. I'd look at address repeatability across runs and boards."*
- 한국어 핵심: **랜덤 다수 주소 = refresh**, **같은 주소 반복·부품 따라감 = 약한 셀**.

</details>

### E5 · suspend/resume 후 데이터 손상

#### 증상

- suspend-to-RAM 루프 500회 중 7회 resume 후 앱 crash 또는 파일 checksum 불일치. 커널은 대부분 살아 있음.

#### 먼저 물어볼 것

- *"What's the corruption granularity — single bits, 64-byte cache lines, or whole pages?"*
- *"Which rails and clocks are off in suspend, and is the PHY power-gated?"*
- *"Are caches cleaned before entering self-refresh, and by whom?"*
- *"Does resume restore training values or retrain, and at which frequency set point?"*

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① 캐시 clean 누락 (SW) | dirty line이 DRAM에 안 써진 채 전원 off | 손상 단위 | **64 B 단위 stale 데이터** (최근에 쓴 내용이 이전 값) |
| ② PHY 상태(training 값) 복원 누락/불완전 | PHY 전원 gating 후 기본값 | resume 직후 bus test, lane 분포 | **lane 단위** 양방향 오류, resume 직후부터 |
| ③ self-refresh 진입/탈출 순서·타이밍 | CKE/클럭 정지 타이밍 | 시퀀스 로그·scope | 랜덤 비트, 전체 영역 |
| ④ suspend 중 DRAM 레일 drop / refresh 공백 | PMIC 시퀀스 | VDD2/VDDQ를 suspend 동안 기록 | 전체 영역 retention 오류(1→0) |
| ⑤ resume 시 FSP/주파수 불일치 | DFS 상태 복원 | 주파수 고정 실험 | 특정 주파수에서 suspend한 경우만 |

#### 측정 · 계측

- **손상 단위가 계층을 말해 준다**: 비트 → 전기/retention, lane → PHY 복원, 64 B 캐시라인 → SW 캐시, 4 KB 페이지 → MMU/페이지 테이블·IOMMU.
- 테스트 설계: suspend 전 **non-cacheable 영역**(또는 clean 후)에 페이지별 패턴 + CRC 기록 → resume 후 비교 → 실패 페이지마다 XOR 패턴 저장. 같은 실험을 **cacheable 영역에 캐시 clean 없이** 돌려 차이를 본다.
- scope: suspend 진입 순간 CKE와 VDDQ (긴 시간 캡처, segmented memory), resume 순간 클럭 재시작 순서.

#### root cause · fix

- 예: 손상이 항상 64 B 정렬 + stale 값 → suspend 경로에서 마지막 L2/L3 clean이 DRAM self-refresh 진입 **뒤**에 일어남(순서 버그). Fix: clean → barrier → self-refresh 진입 순서 고정 + 진입 직전 검증 hook.

#### 회귀로 굳히기

- **suspend/resume 1000회 루프 + 페이지 CRC 비교** (N06D D2 self-refresh 행), 실패 시 XOR와 손상 단위를 자동 분류.

#### 30초 영어 답

*"The first thing I want is the corruption granularity, because it points at the layer. Single random bits suggest retention during suspend — rails or refresh — while errors confined to a byte lane suggest the PHY didn't restore its training state, and 64-byte stale chunks almost always mean a cache line wasn't cleaned before the DRAM went into self-refresh. I'd build a test that writes per-page CRCs to non-cacheable memory, cycles suspend a thousand times, and classifies each failure, while scoping CKE and VDDQ across entry and exit."*

#### 꼬리 질문

**Q1. Corruption is 64-byte aligned. Why are you so sure it's software?**

<details><summary>답</summary>

*"DRAM and PHY faults don't know about cache lines; they show up as bits, lanes, or rows. Exactly 64-byte chunks holding older data is the signature of a dirty line that never got written back. I'd still confirm by cleaning caches explicitly before suspend and seeing it disappear."*
- 한국어 핵심: **캐시라인 단위 stale = write-back 누락**, 명시적 clean으로 확인.

</details>

**Q2. It's lane-shaped instead. What next?**

<details><summary>답</summary>

*"Then I'd dump the PHY delay and VREF registers before suspend and right after resume and diff them. If they differ, the restore path is incomplete; if they match but errors appear, the DRAM or PHY drifted while off and resume needs a retrain or a tracking update."*
- 한국어 핵심: **suspend 전/후 PHY 레지스터 diff**.

</details>

### E6 · NPU + 카메라 동시 부하에서만 비트 에러

#### 증상

- CPU memtester 단독 8시간 0 오류. NPU 추론 루프 + 카메라 4대 ISP 스트림 + memtester 동시: 1시간에 수 건, ECC on이면 CE가 **여러 lane**에서 증가. ISP 프레임 drop도 가끔.

#### 먼저 물어볼 것

- *"Are these data errors detected by ECC or the memory test, or are we seeing frame drops and underflows — different problems?"*
- *"Does the error rate track VDDQ or VDD2 droop, or temperature rise from the NPU?"*
- *"Are the corrupted areas the DMA buffers themselves, at buffer or cache-line granularity?"*
- *"Does lowering DDR frequency one bin make it go away?"*

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① PDN droop / 동시 스위칭 노이즈 | 최대 대역폭에서 모든 DQ 동시 토글 → VDDQ 노이즈 | 레일 scope, droop 트리거, VDDQ +3% | 오류 시각 = droop, 전압↑로 감소 |
| ② 온도 상승 | NPU 발열 | DRAM 온도 로그 | 오류율이 온도 따라감 (E4 경로로) |
| ③ DMA 캐시 일관성 (SW) | 부하 = DMA 많음 | 손상 단위·위치, non-cacheable 버퍼 | 버퍼 경계 · 64 B 단위, 버퍼에만 |
| ④ 데이터 패턴 의존 마진 | NPU/영상 데이터가 특정 패턴 | 패턴별 재현(0x00↔0xFF 교대, PRBS) | 특정 패턴에서만 |
| ⑤ QoS/대역폭 (데이터 오류 아님) | 프레임 drop | ISP underflow 카운터 | **데이터는 정상**, drop만 |

#### 측정 · 계측

- **먼저 증상 둘을 분리**: 데이터 비트 오류(ECC/테스트가 봄)와 프레임 drop(QoS/underflow)은 다른 문제. drop만이면 [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html)·QoS 쪽.
- **합성 부하로 재현**: NPU/ISP 대신 DMA 엔진 여러 개로 최대 대역폭 copy + CPU memtester → 재현되면 "부하"가 원인이지 NPU 기능이 아님.
- **레일**: DRAM 근처 VDDQ/VDD2에 짧은 접지 프로브, AC 결합, **droop 크기 트리거(window/runt)** + 오류 발생 시 GPIO 토글을 두 번째 채널로 → 시각 상관. ([N07A](2026-11-01_N07A_debug_framework_instruments.html) scope 요령)
- **패턴**: 모든 DQ 동시 토글(0x00000000 ↔ 0xFFFFFFFF 교대)과 DBI on/off 비교 — DBI는 동시 토글 수를 줄여 SSN을 낮추는 기능이라 DBI on에서 사라지면 PDN/SSN 증거 [추정].

#### root cause · fix

- 예: 최대 대역폭 시 VDDQ에 수십 mV droop [예시], 오류 시각과 일치, DBI on + VDDQ 목표 +2% [예시]에서 0 오류 → 단기 DBI 활성화·레귤레이터 튜닝, 장기 decoupling/PDN 리뷰(BV12).

#### 회귀로 굳히기

- 검증 플랜에 **"전 master 동시 최대 부하 + memtest + ECC 카운터"** soak 행(N06D D2 동시성·QoS 행), 레일 droop 측정값을 보드별로 기록.

#### 30초 영어 답

*"First I'd separate data errors from frame drops — drops are a bandwidth or QoS problem, bit errors are integrity. For the bit errors, the fact that CPU-only stress is clean but concurrent load fails points at what load changes: supply noise from every DQ toggling at full bandwidth, temperature, or DMA coherency in software. I'd reproduce with synthetic DMA traffic, scope VDDQ at the DRAM with a droop trigger correlated to an error GPIO, and check whether errors land on DMA buffers at cache-line granularity. Raising VDDQ slightly or enabling DBI making it disappear points at power integrity; non-cacheable buffers making it disappear points at software."*

#### 꼬리 질문

**Q1. How do you trigger the scope on something that happens twice an hour?**

<details><summary>답</summary>

*"I make the error detector toggle a GPIO and trigger on that, with a long pre-trigger buffer so I see the rail before the error, or use segmented memory to store many short captures around each event. Running it overnight gives me a dozen events to compare."*
- 한국어 핵심: **오류 → GPIO → 트리거**, pre-trigger + segmented memory.

</details>

**Q2. The errors are only inside the NPU's output buffers.**

<details><summary>답</summary>

*"Then I'd suspect the producer-consumer handoff before the DRAM: cache maintenance around the NPU's DMA, or an IOMMU mapping. I'd use non-cacheable buffers or add explicit clean and invalidate, and check whether the corrupted region aligns to cache lines or buffer boundaries."*
- 한국어 핵심: 버퍼 안에만 → **DMA 일관성** ([LP08](2026-10-31_LP08_dma_iommu_coherency.html)).

</details>

### E7 · 같은 주소 CE가 하루 수천 번

#### 증상

- 필드 시험 차량 한 대: `ce_count`가 하루 약 4,500 증가, `ras-mc-ctl --errors`에 **같은 주소 · 같은 비트**만 반복. 커널 로그는 예를 들어 Synopsys 드라이버 형식이면 `DDR ECC error type:CE Row … Bank … Bit Position: … Data: 0x…` [3] (§8 M5).

```text
[예시 출력]  decoder 요약 (EDAC CE 레코드를 (주소, 저장값, raw 읽기)로 재구성)
   folded DQ   : DQ27:40(+0/-40)
   decode(mock): distinct rows=1 banks=1  first: off=0x03a5f1c48 bank=7 row=0x74b col=0x71
   time        : span=762s  rate=189.1/h
```

#### 먼저 물어볼 것

- *"Is it exactly one address and one bit, and does it repeat on reads only or also with patrol scrub?"*
- *"Does the rate follow how often that address is accessed, or the scrub interval?"*
- *"Any CEs on neighbouring rows, or near a heavily accessed row?"*
- *"Has this address ever produced a UE?"*

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① 하드 셀 고장 (stuck-at, 1→0) | 한 주소 · 한 비트 · 한 방향 · 온도 무관 | 반대 값을 쓰면 CE 없음, 같은 값이면 매번 CE | stuck 방향과 일치 |
| ② RowHammer 피해자 | 접근 많은 row 옆 | 인접 row 접근 빈도, 인접 row CE | 인접 row ±1에 CE 분포 |
| ③ EDAC/드라이버 중복 보고 | 상태 레지스터 clear 누락 | 접근 없이도 증가하나 | 접근 0인데 카운트 증가 |
| ④ 핀/lane 문제 | DQ 고장도 CE를 만든다 | 다른 주소 CE 유무 | 이 경우 여러 주소에 퍼짐 (아님) |

#### 측정 · 계측

- **rate 해석**: CE는 그 주소를 **읽을 때마다**(또는 scrub이 지나갈 때마다) 보고된다 → 하루 4,500회 = 고장 정도가 아니라 **접근 빈도**. 셀 하나가 고장 난 것이지 "점점 나빠지는" 증거는 아님.
- **stuck 방향 확인**: 그 주소에 bit 59 = 0을 쓰면 CE 0, 1을 쓰면 매번 CE → stuck-at-0. (패닉 위험 없는 정비 모드에서)
- **위험도**: 같은 64-bit word에 두 번째 비트(마진·우주선)가 겹치면 **UE** → 운영 정책 필요.
- Linux 대책: `soft_offline_page`에 그 물리 주소를 쓰면 커널이 내용을 옮기고 그 페이지를 다시 안 씀 [4]; rasdaemon은 `PAGE_CE_THRESHOLD` / `PAGE_CE_REFRESH_CYCLE` / `PAGE_CE_ACTION=soft` 로 자동화 [6].

#### root cause · fix

- 예: DRAM 한 셀 stuck-at-0 → 기능은 ECC가 지킴. 조치: 페이지 격리 + 차량 단위 기록, 같은 lot에서 반복되면 공급사 FA(failure analysis). **"CE니까 무시"가 아니라 "고장 하나를 ECC가 가리고 있다"** 로 보고.

#### 회귀로 굳히기

- 보드 팜·필드 텔레메트리에서 **CE를 주소 단위로 집계**해 "같은 주소 N회 이상" 규칙으로 하드 고장 분류(분산 CE와 구분), 페이지 격리 경로를 시스템 테스트에 포함(`CONFIG_EDAC_DEBUG` 주입으로 검증 — BV09 §3.5).

#### 30초 영어 답

*"Thousands of CEs at one address and one bit isn't margin, it's one hard cell that ECC is masking, and the count mostly reflects how often that address gets read or scrubbed. I'd confirm it's stuck by writing both values there, check neighbouring rows to rule out a hammer victim, and make sure the driver isn't re-reporting a stale status. Operationally I'd offline that page — the kernel has a soft-offline interface and rasdaemon can do it on a threshold — because a second bit in the same word becomes an uncorrectable error. And I'd track CEs per address across the fleet so hard faults are separated from spread-out margin CEs."*

#### 꼬리 질문

**Q1. When is a CE rate acceptable?**

<details><summary>답</summary>

*"There's no single number; it depends on the product's safety case. What I'd define is a policy: CEs at one location above a threshold within a window mean a hard fault and get the page offlined; CEs spread across addresses that grow with temperature or load mean margin and get escalated as a validation bug. Zero CEs in qualification at nominal is the expectation for a new design."*
- 한국어 핵심: **위치 집중 = 하드 고장 → 격리**, **분산 + 조건 상관 = 마진 → 버그**.

</details>

**Q2. How would you validate that the CE reporting path itself works?**

<details><summary>답</summary>

*"Inject a known single-bit error at a known address — through the controller's poison feature, which Linux exposes on some drivers when EDAC debug is enabled — then read that address and check the counter increments by one, the reported row, bank and bit match what I injected, and a double-bit injection raises a UE. That proves address decoding of the report, not just the counter."*
- 한국어 핵심: **주입 → 카운터 +1 + 위치 일치 + 2비트 UE** ([BV09 §2.4](2026-11-01_BV09_ddr_bringup_training_memtest.html)).

</details>

### E8 · training은 통과, 1주일에 한 번 커널 패닉 ★

#### 증상

- 보드 팜 20장 중 몇 장이 **주 1회꼴** 커널 패닉. panic 위치가 매번 다름(slab, page cache, 네트워크 스택…). ECC는 꺼져 있음(LPDDR inline ECC 대역폭 때문에 개발 빌드에서 off). training·부팅 시 memtest 모두 PASS.

#### 먼저 물어볼 것

- *"Are panic PCs and corrupted structures random, or always the same code path?"*
- *"Is ECC available? Can we turn it on for the investigation even at a bandwidth cost?"*
- *"Which boards, what temperature and workload when it happens — any correlation in the farm data?"*
- *"Do we have pstore or kdump so each panic leaves the faulting address and memory contents?"*

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① DDR 마진 (패턴·온도·부하 의존) | 랜덤 panic 위치, 일부 보드만 | 마진 손잡이로 가속(주파수↑ 또는 VDDQ↓) | MTBF가 손잡이에 반응 |
| ② SW 메모리 손상 (use-after-free, DMA 일관성) | 랜덤처럼 보일 수 있음 | KASAN/slub_debug 빌드, 손상 단위 | 특정 구조체·버퍼 경계 |
| ③ refresh/온도 이벤트 | 온도 상승 시점 | 팜 온도 로그와 시각 | panic 시각 = 고온 구간 |
| ④ 주소 맵/interleave 설정 일부 영역 | 큰 메모리 사용 시 | address-as-data 전체 | 특정 영역 aliasing |
| ⑤ DRAM 밖 원인 (전원, CPU 마진) | 시스템 전체 | CPU 전압·주파수 손잡이 | DDR 손잡이엔 무반응 |

#### 측정 · 계측

- **증거를 만든다**: ECC를 켜면 무증상 손상이 **위치 있는 CE/UE**로 바뀐다 — 비용이 들어도 조사 기간에는 on. pstore/kdump로 매 panic의 손상 주소·내용 확보 → 손상 값 XOR 패턴(한 비트? 한 lane? 구조체 크기?).
- **가속 시험 (Staff 시그널)**: 주 1회는 통계적으로 다루기 너무 느림. 0회 관측으로 "고쳤다"를 95% 신뢰로 말하려면 **평균 고장 간격의 약 3배**를 깨끗하게 돌려야 한다(포아송, rule of three) → 3주. 대신 **마진을 일부러 줄여**(DDR 한 bin 위, VDDQ −3%, 고온) 고장률을 시간당 단위로 올리고, 그 조건에서 원인을 찾고 fix 효과를 비교한다. 반대로 **마진을 늘려**(한 bin 아래) MTBF가 길어지면 DDR 원인 증거.
- **HW vs SW**: 비트/lane 상관 + 온도 상관 = HW, 구조체·버퍼 경계 + 코드 경로 상관 = SW (BV09 Chain 6). 같은 보드에서 커널 디버그 옵션(KASAN 등)으로도 재현되면 SW 쪽 가중.
- 팜 데이터: 보드 × 온도 × 부하 × FW별 panic 수 — **특정 보드에 몰리면 HW 마진**, 모든 보드 균등이면 SW 또는 설계.

#### root cause · fix

- 예: ECC on 후 CE가 **lane 1 비트들에, 오후 고온 구간에만** → 고온 lane 1 write 마진. 가속 조건(VDDQ −3%, 95 °C)에서 시간당 수 건 재현 → write VREF 재최적화 + 주기적 retraining on → 같은 가속 조건에서 0건/48시간.

#### 회귀로 굳히기

- 팜 nightly에 **마진 감소 corner 스트레스**(stressapptest 류 + ECC 카운터 + 온도), 결과를 lane·보드·온도로 집계. 개발 빌드도 **ECC on을 기본**으로(또는 일정 비율 보드).

#### 30초 영어 답

*"A random kernel panic once a week with no ECC is a statistics problem first, so I'd create evidence and acceleration. Evidence: turn ECC on for the investigation and capture pstore or kdump, so silent corruption becomes errors with addresses and bit positions I can XOR and fold onto lanes. Acceleration: reduce margin on purpose — one speed bin up, lower VDDQ, higher temperature — so the failure happens per hour instead of per week; if the rate responds to DDR margin it's hardware, and if it doesn't, I look harder at software with KASAN-style tools. Proving a fix at the natural rate would need about three clean weeks, which is why the stressed corner is where I compare before and after."*

#### 꼬리 질문

**Q1. Why three weeks?**

<details><summary>답</summary>

*"With a Poisson failure process, seeing zero events over time t has probability exp of minus t over the mean interval. To claim with 95 percent confidence that the rate is lower, I need exp of minus t over MTBF below 0.05, which means t is about three MTBFs — the rule of three."*
- 한국어 핵심: **e^(−t/MTBF) < 0.05 → t ≈ 3 × MTBF**.

</details>

**Q2. Reducing margin makes it fail more. Couldn't that just create a different failure?**

<details><summary>답</summary>

*"Yes, so I check the signature matches: same lanes, same bit directions, same kind of corrupted structures as the natural failures. If stressing produces a different signature I'm debugging the wrong thing."*
- 한국어 핵심: 가속 시험은 **시그니처가 같을 때만** 유효.

</details>

**Q3. ECC can't be on in production. Then what?**

<details><summary>답</summary>

*"Then validation has to prove margin without it: characterize every lane across PVT with a guard band, keep ECC-on boards in the farm as sentinels, and make the safety case explicit about which memory regions need ECC — on automotive parts the safety-critical regions usually do."* [추정 — 제품 안전 요구에 따라 다름]
- 한국어 핵심: ECC 없으면 **PVT 마진 + guard band + sentinel 보드**.

</details>

## 6. 꼬리 질문 체인

### Chain 1 — "Memtest reports random bit errors. Where's the fault?"

<details><summary>Q → 꼬리1 → 꼬리2 → 꼬리3</summary>

**Q. Memtest reports random bit errors. Where's the fault?**
*"I don't accept 'random' until I've XORed expected against actual and folded the bits onto physical DQs and byte lanes. Then the shape tells me the layer: one DQ one direction is a pin, one lane both directions is that strobe group's margin, a power-of-two address difference is aliasing, spread across lanes with temperature or dwell dependence is refresh. I also decode the addresses with the controller's map to see bank and row clustering."*
- 한국어 핵심: **XOR → fold → lane/방향 → 주소 맵 → 조건**.

**꼬리1. All the errors are on one byte lane.**
*"Then it's that lane's DQS group. I re-read to split read from write, compare that lane's training margins to the others and across boards, and sweep its VREF and delay. Same lane on every board is layout; one board is assembly."*
- 한국어 핵심: **다시 읽기 + lane margin + 보드 비교**.

**꼬리2. It only happens after the board has warmed up.**
*"Then either the trained delays drift with temperature and tracking or retraining isn't enabled, or the lane's margin simply shrinks at hot. Booting hot versus booting cold and heating separates them: if booting hot passes, it's drift."*
- 한국어 핵심: **고온 부팅 vs 상온 부팅 후 가열**.

**꼬리3. How do you prove the fix?**
*"Same sweep before and after to show the eye grew, plus stress at a corner that used to fail fast — hotter, lower VDDQ — with enough runs for the rule of three, and a regression that logs per-lane margins every boot."*
- 한국어 핵심: **eye 전후 + 스트레스 corner + 매 부팅 margin 기록**.

</details>

### Chain 2 — "ECC is on and you see CEs. Is that OK?"

<details><summary>Q → 꼬리1 → 꼬리2 → 꼬리3</summary>

**Q. ECC is on and you see correctable errors. Is that OK?**
*"Functionally the data is fine, but a CE is evidence, not noise. I look at where and how fast: one address repeating is a hard cell, spread-out CEs that grow with temperature or load are margin. On a new design in qualification I'd expect zero at nominal conditions."*
- 한국어 핵심: CE = **증거**, 위치·추세로 분류.

**꼬리1. Where do you read them on Linux?**
*"EDAC sysfs: per controller ce_count and ue_count, per DIMM or rank dimm_ce_count, the noinfo counters, and seconds_since_reset; rasdaemon stores each event with location so ras-mc-ctl can summarize or list them."*
- 한국어 핵심: `mc*/ce_count`, `dimm*/dimm_ce_count`, `ras-mc-ctl --summary/--errors` [1][5].

**꼬리2. A UE just happened. What now?**
*"Data is lost, so the system policy decides — panic, kill the owner, or poison the page. For debug, I check whether that location had CEs before: a UE on top of a known hard cell means we failed to offline in time; a UE out of nowhere means margin collapse or a multi-bit fault like a lane or pin."*
- 한국어 핵심: **직전 CE 이력**으로 UE 원인 분류.

**꼬리3. How do you validate the reporting itself?**
*"Inject single- and double-bit errors at known addresses through the controller's poison feature, and check counts, reported row, bank and bit, interrupts, and that a double-bit gives a UE. Without that, a silent reporting path looks exactly like a healthy memory."*
- 한국어 핵심: **주입으로 보고 경로 검증**, 아니면 "조용함"을 믿을 수 없다.

</details>

### Chain 3 — "How do you tell a DDR problem from a software corruption bug?"

<details><summary>Q → 꼬리1 → 꼬리2</summary>

**Q. How do you tell a DDR problem from software memory corruption?**
*"By the shape and by what it responds to. DDR errors cluster by bit, lane or row and respond to frequency, voltage and temperature. Software corruption clusters by data structure, buffer boundary or cache line and responds to code changes, cache settings and debug allocators."*
- 한국어 핵심: **비트·lane·row + 마진 손잡이 = HW**, **구조체·버퍼·캐시라인 + 코드 = SW**.

**꼬리1. It disappears with caches disabled.**
*"That's a hint, not a verdict. It can mean a coherency bug, because DMA and CPU now agree, but caches off also changes the traffic pattern and lowers bandwidth, which can hide a marginal DDR. I'd make only the DMA buffers non-cacheable and keep the rest cached to isolate it."*
- 한국어 핵심: 캐시 off는 **두 가지를 동시에** 바꾼다 → 버퍼만 non-cacheable로.

**꼬리2. It disappears at a lower DDR frequency.**
*"That's stronger evidence for hardware margin, especially if the signature stays the same lanes. I'd then go the other way — one bin up or lower VDDQ — and expect the rate to rise. Moving in both directions with a consistent signature is what convinces me."*
- 한국어 핵심: **양방향으로 손잡이를 움직여** 반응 확인.

</details>

### Chain 4 — "Training passed. Why didn't it catch this?"

<details><summary>Q → 꼬리1 → 꼬리2</summary>

**Q. Training passed. Why didn't it catch the problem?**
*"Training answers one question: where's a working timing point for each lane right now. It doesn't check that every address is unique, that every cell holds data, that refresh is right at hot, or that the margin survives load. Those are memory-test and characterization questions."*
- 한국어 핵심: training = **타이밍 한 지점**, 나머지는 테스트·특성화.

**꼬리1. So what's your minimum test after training?**
*"Data bus walking ones per lane, address bus power-of-two plus address-as-data, then March C- with several backgrounds, caches off — in that order, because running them out of order misdiagnoses data faults as address faults."*
- 한국어 핵심: **data bus → address bus → March(배경 여러 개)**, 순서 고정.

**꼬리2. And for the problems that only show up later?**
*"Bit-fade with dwell time for retention, a temperature sweep including booting hot, concurrent-master stress with ECC counters, suspend and DFS loops, and per-lane margin logging so drift shows up as a trend before it's a failure."*
- 한국어 핵심: **bit-fade · 고온 부팅 · 동시 부하 · suspend/DFS 루프 · margin 추세**.

</details>

## 7. Don 경험 연결

- *"At Solidigm I verified SRAM and DRAM paths on our FPGA prototype as part of SoC verification before RTL freeze, so I've been on the side that has to turn a failing test into a location — which data bit, which address bit, which path."* `[숫자 채우기: DRAM 경로에서 잡은 버그 1건 — 증상 · XOR 모양 · 원인]`
- *"At SK hynix I automated chamber reliability testing for NAND and PCIe interfaces, so correlating failures with temperature and voltage and separating one weak unit from a design-wide margin problem is how I'm used to reading data."* `[숫자 채우기: 온도 범위, 샘플 수, 찾은 패턴 1개]`
- *"In my current role I do root cause on PCIe, I2C and RFFE with scopes and protocol analyzers; the habit I'd bring here is the same — pin the layer with the cheapest experiment first, then get the waveform."* `[숫자 채우기: 사례 1개]`
- 정직하게: *"I haven't owned a DDR PHY training flow myself; where I'm strong is reading the failure data and designing the experiment that splits the hypotheses."*

## 8. 실습 / 코드

### 8.1 무엇을 하나

- 코드: [onsitePrep/n07/e/ddr_fail_decode.c](onsitePrep/n07/e/ddr_fail_decode.c) (C11, 의존성 없음) · mock 로그 생성기 [gen_mocks.py](onsitePrep/n07/e/gen_mocks.py) (한 번 실행해 [logs/](onsitePrep/n07/e/logs/) 에 저장, 디코더는 결함을 모름) · [run.sh](onsitePrep/n07/e/run.sh) · [run_output.txt](onsitePrep/n07/e/run_output.txt).
- [V1](hmScreenPrep/problems/V1_march_memtest/index.html)·[BV09 §6.1](2026-11-01_BV09_ddr_bringup_training_memtest.html)이 **테스트를 돌려 고장을 찾는** 코드라면, 이건 **이미 나온 실패 기록을 해석**하는 코드.
- 입력 레코드: `<offset> <expected64> <actual64> [temp=C] [age=ms] [t=s] [src=…]`, 헤더 `#bus 32`, `#pattern addr`(address-as-data), `#tested_at <온도들>`, `#expect <진단>`.
- 처리: XOR → logical bit 히스토그램 → `dq = bit % bus` fold → lane → 방향 / address-as-data면 주소 XOR로 alias 비트 / mock 맵(§3.2)으로 bank·row·col / 온도별 실패 수(시험한 온도 0건 포함) · age · 시간 버스트 → 규칙 순서대로 진단 1줄 → `#expect`와 비교(PASS/FAIL).
- 규칙 순서 (구체적인 것 먼저): ① 레코드 0 → CLEAN ② address-as-data + xor 일정 + 1비트 → ADDR_ALIAS ③ DQ 1개 + 주소 1개 → SINGLE_CELL ④ DQ 1개 + 한 방향 → DQ_STUCK (양방향이면 DQ_MARGIN) ⑤ 온도 의존 + 1→0 80% 이상 + 모두 오래 묵힘 → TEMP_RETENTION ⑥ lane 1개 → LANE_MARGIN ⑦ 온도 의존 → TEMP_MARGIN ⑧ 나머지 → MULTI_LANE_MARGIN.

### 8.2 실행

```bash
cd onsitePrep/n07/e
./run.sh          # cc -isysroot …MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined → $TMPDIR, 6개 로그 실행, run_output.txt 저장
# mock 다시 만들 때만: python3 -I gen_mocks.py
```

### 8.3 실제 출력 (2026-11-01, macOS, ASan+UBSan, 경고 0)

```text
== M0 clean control (no failures)
   records=0  bus=x32 (4 byte lanes)  pattern=data
   diagnosis   : CLEAN
   expect      : CLEAN  -> PASS

== M1 memtest says 'bit 13' fails (scenario E1)
   records=24  bus=x32 (4 byte lanes)  pattern=data
   logical bits: b13:19 b45:15
   folded DQ   : DQ13:34(+34/-0)
   byte lanes  : L0:0 L1:34 L2:0 L3:0
   direction   : 0->1=34  1->0=0
   decode(mock): distinct rows=24 banks=7  first: off=0x0892f9028 bank=7 row=0x1125 col=0x240
   fails/temp  : 25C:24
   diagnosis   : DQ_STUCK dq=13 lane=1 dir=0to1
   expect      : DQ_STUCK dq=13 lane=1 dir=0to1  -> PASS

== M2 byte lane 2 intermittent (scenario E2)
   records=32  bus=x32 (4 byte lanes)  pattern=data
   logical bits: b16:5 b17:4 b18:6 b19:4 b20:3 b21:7 b22:1 b23:4 b48:3 b49:4 b50:3 b51:1 b52:1 b53:4 b54:2 b55:4
   folded DQ   : DQ16:8(+3/-5) DQ17:8(+5/-3) DQ18:9(+2/-7) DQ19:5(+1/-4) DQ20:4(+0/-4) DQ21:11(+7/-4) DQ22:3(+1/-2) DQ23:8(+5/-3)
   byte lanes  : L0:0 L1:0 L2:56 L3:0
   direction   : 0->1=24  1->0=32
   decode(mock): distinct rows=32 banks=8  first: off=0x0b4d66a38 bank=6 row=0x169a col=0x1a8
   fails/temp  : 45C:32
   time        : span=2048s  bursts(gap>60s)=4  rate=56.2/h
   diagnosis   : LANE_MARGIN lane=2 dqs=2 ndq=8
   expect      : LANE_MARGIN lane=2  -> PASS

== M3 upper half is a copy of lower half (scenario E3)
   records=16  bus=x32 (4 byte lanes)  pattern=address-as-data
   addr-xor (expected addr ^ returned addr) = 0x080000000  same for all records: yes
   -> offset bit 31 (mock map field: row) is not decoded; 16/16 reads at bit31=1 returned bit31=0 data
   diagnosis   : ADDR_ALIAS bit=31 field=row dir=hi_reads_lo
   expect      : ADDR_ALIAS bit=31 dir=hi_reads_lo  -> PASS

== M4 errors only at hot after long dwell (scenario E4)
   records=24  bus=x32 (4 byte lanes)  pattern=data
   logical bits: b3:1 b4:1 b6:1 b10:1 b14:1 b17:2 b20:1 b26:1 b29:2 b32:1 b34:1 b35:2 b36:3 b40:1 b42:1 b45:1 b56:1 b60:1 b63:1
   folded DQ   : DQ0:1(+0/-1) DQ2:1(+0/-1) DQ3:3(+1/-2) DQ4:4(+1/-3) DQ6:1(+0/-1) DQ8:1(+0/-1) DQ10:2(+0/-2) DQ13:1(+0/-1) DQ14:1(+0/-1) DQ17:2(+1/-1) DQ20:1(+0/-1) DQ24:1(+0/-1) DQ26:1(+1/-0) DQ28:1(+0/-1) DQ29:2(+0/-2) DQ31:1(+0/-1)
   byte lanes  : L0:10 L1:5 L2:3 L3:6
   direction   : 0->1=4  1->0=20
   decode(mock): distinct rows=24 banks=8  first: off=0x002f4b340 bank=4 row=0x5e col=0x2cd
   fails/temp  : 25C:0 65C:0 85C:0 95C:3 105C:21
   age>=64ms   : 24/24 records
   diagnosis   : TEMP_RETENTION hot_from=95 dir=mostly_1to0 lanes=4
   expect      : TEMP_RETENTION hot_from=95  -> PASS

== M5 same-address CE storm (scenario E7)
   records=40  bus=x32 (4 byte lanes)  pattern=data
   logical bits: b59:40
   folded DQ   : DQ27:40(+0/-40)
   byte lanes  : L0:0 L1:0 L2:0 L3:40
   direction   : 0->1=0  1->0=40
   decode(mock): distinct rows=1 banks=1  first: off=0x03a5f1c48 bank=7 row=0x74b col=0x71
   fails/temp  : 40C:15 55C:14 70C:11
   time        : span=762s  bursts(gap>60s)=1  rate=189.1/h
   diagnosis   : SINGLE_CELL addr=0x03a5f1c48 dq=27 lane=3 dir=1to0 count=40
   expect      : SINGLE_CELL addr=0x03a5f1c48 dq=27  -> PASS

summary: 6/6 PASS
```

- 음성 검사(실행함): M1 로그의 `#expect`를 `dq=14`로 바꿔 돌리면 `-> FAIL`, `summary: 0/1 PASS`, 종료 코드 1 → 비교기가 실제로 틀린 기대를 잡는다.

### 8.4 출력 읽는 법

| 케이스 | 핵심 줄 | 읽는 법 |
|---|---|---|
| M0 | `CLEAN` | 대조군 — 디코더가 "실패 없음"을 고장으로 오진하지 않는지 |
| M1 | `b13:19 b45:15` → `DQ13:34(+34/-0)` | logical 비트 두 개가 **한 핀**으로 접힘(x32 버스, 2 beat). `+34/-0` = 전부 0→1 → stuck. 24개 row · 7개 bank에 퍼짐 → 주소 무관 = 핀. "bit 13" 보고를 "lane 1 DQ13 stuck-high"로 바꿔 말하는 것이 요점 |
| M2 | `L2:56`, `+3/-5 …`, `bursts=4` | lane 2 밖은 0 → **DQS2 그룹**. 모든 DQ가 양방향 → 타이밍. 4개 뭉치 → 주기적 이벤트·온도와 시각 대조할 차례 (E2) |
| M3 | `addr-xor = 0x080000000 same for all: yes` | address-as-data라 xor가 곧 **주소 차이** → bit 31 alias. `16/16 … returned bit31=0 data` = 상위 읽기가 하위 데이터 (내림차순 쓰기에서 하위가 마지막) |
| M4 | `25C:0 … 95C:3 105C:21`, `1->0=20`, `age>=64ms 24/24`, 4 lane | **시험한 온도의 0건까지** 찍어야 "고온 의존"이라 말할 수 있다. lane 무관 + 1→0 + 오래 묵힌 데이터 → retention/refresh (E4). 0→1 4건은 anti-cell·노이즈 몫 [추정] |
| M5 | `rows=1 banks=1`, `DQ27:40(+0/-40)`, `rate=189.1/h` | 한 주소 · 한 비트 · 한 방향 → 하드 셀. 189/h ≈ 하루 4,500 = **접근 빈도** (E7). 온도 3단계에 고르게 → 온도 무관 |

- 한계 `[미검증]`: 실제 보드 로그로 돌려 본 적 없음 — 실제는 **주소 맵이 해시/interleave**일 수 있고, swizzle 표를 적용해야 DQ 번호가 DRAM 볼과 맞는다. RowHammer(인접 row 군집)·DBI(바이트 0xFF)·DM(stale) 규칙은 표(§4)에만 있고 코드에는 아직 없음.

## 9. 자주 틀리는 것

1. **"random bit errors"를 그대로 받아들임** — XOR · fold · lane · 방향을 하기 전에는 random이 아니다. 대부분 한 lane, 한 핀, 한 주소 비트로 줄어든다.
2. **logical bit 번호 = 핀 번호라고 말함** — 64-bit word는 버스폭에 따라 여러 beat; x32면 bit b와 b+32가 같은 DQ. 게다가 보드 swizzle. "bit 13"은 "DQ13, lane 1, swizzle 적용 전"으로 바꿔 말한다.
3. **주소 맵 가정 없이 "bank 3 문제"** — 맵이 해시/interleave면 bank 번호가 전혀 다르다. 맵 레지스터부터.
4. **실패한 온도만 보고 "고온 의존"** — 시험했는데 0건인 온도도 같이 있어야 의존성이다 (디코더가 `25C:0`을 찍는 이유).
5. **CE는 정정됐으니 OK** — 한 주소 반복 = 하드 고장(UE 예비군), 분산 증가 = 마진 버그. 둘 다 보고 대상.
6. **CE rate를 고장 심각도로 읽음** — 같은 셀이라도 접근·scrub 빈도에 따라 수십~수천. 위치 수와 확산이 심각도.
7. **캐시 off로 사라졌으니 SW 버그** — 캐시 off는 트래픽 패턴·대역폭도 바꾼다. 버퍼만 non-cacheable로 분리.
8. **training PASS = 메모리 OK** — training은 타이밍 한 점. aliasing·셀·refresh·부하 마진은 테스트와 특성화로만.
9. **주 1회 고장을 자연 발생률로 "고쳤다" 확인** — 95% 신뢰엔 MTBF의 약 3배 무고장 필요. 마진을 줄여 가속하고 **같은 시그니처**인지 확인.
10. **address test 결과를 data bus test 전에 믿음** — 데이터선 고장이 "A0 stuck"으로 보인다 (BV09 실측). xor가 일부 비트면 데이터 쪽.

## 10. 출처

1. Linux kernel docs, "Reliability, Availability and Serviceability (RAS)" — EDAC sysfs (`mcX/ce_count`, `ce_noinfo_count`, `ue_count`, `ue_noinfo_count`, `reset_counters`, `seconds_since_reset`, `dimmX/dimm_ce_count`, `dimm_label`, `dimm_location`), `edac_mc_log_ce`, `edac_mc_panic_on_ue`, CE/UE 정의, csrow/channel/rank — https://docs.kernel.org/admin-guide/RAS/main.html
2. Linux `Documentation/ABI/testing/sysfs-devices-edac` (BV09 출처 14로 확인, `sdram_scrub_rate` 포함) — https://raw.githubusercontent.com/torvalds/linux/master/Documentation/ABI/testing/sysfs-devices-edac
3. Linux `drivers/edac/synopsys_edac.c` — CE 메시지 형식("DDR ECC error type:%s Row %d Bank %d BankGroup Number %d Block Number %d Bit Position: %d Data: 0x%08x"), ADDRMAP 레지스터를 poison 설정용으로 읽음(`setup_address_map`, `CONFIG_EDAC_DEBUG`) — https://raw.githubusercontent.com/torvalds/linux/master/drivers/edac/synopsys_edac.c
4. Linux `Documentation/ABI/testing/sysfs-memory-page-offline` — `soft_offline_page` / `hard_offline_page`, 물리 주소 hex 입력 — https://raw.githubusercontent.com/torvalds/linux/master/Documentation/ABI/testing/sysfs-memory-page-offline
5. ras-mc-ctl(8) man page (rasdaemon) — `--summary`, `--errors`, `--error-count`, `--layout` — https://manpages.ubuntu.com/manpages/noble/man8/ras-mc-ctl.8.html
6. rasdaemon page isolation 패치 (`PAGE_CE_THRESHOLD`, `PAGE_CE_REFRESH_CYCLE`, `PAGE_CE_ACTION` off/account/soft/hard/soft-then-hard) — https://patchwork.kernel.org/patch/11578087/
7. memtester `tests.c` — `FAILURE: 0x%08lx != 0x%08lx at offset 0x%08lx.`, `FAILURE: possible bad address line at offset …` — https://raw.githubusercontent.com/jnavila/memtester/master/tests.c
8. Michael Barr, "Software-Based Memory Testing" (data bus → address bus → device 순서, BV09 출처 2) — https://www.esacademy.com/en/library/technical-articles-and-documents/miscellaneous/software-based-memory-testing.html
9. Kim et al., "Flipping Bits in Memory Without Accessing Them" (ISCA 2014) — 약 139K 접근으로 인접 row 오류, 129개 모듈 중 110개 (검색 요약으로 확인) — https://www.cfallin.org/pubs/isca2014_bitflips.pdf · https://arxiv.org/pdf/1603.00747
10. Refresh·온도(DDR4 tREFI 7.8/3.9 µs, LPDDR4 32 ms · MR4, i.MX8MM ERR050805) — BV09 출처 15 묶음 — [BV09 §2.5](2026-11-01_BV09_ddr_bringup_training_memtest.html)
