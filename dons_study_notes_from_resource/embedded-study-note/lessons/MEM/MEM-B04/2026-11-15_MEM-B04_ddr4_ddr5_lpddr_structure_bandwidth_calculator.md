---
id: MEM-B04
time: 55분
updated: 2026-11-15
---
# MEM-B04 DDR4 vs DDR5 vs LPDDR4/5 구조 비교와 대역폭 계산

## 학습 목표

- DDR4 · DDR5 · LPDDR4/4X · LPDDR5/5X (+ HBM2E/HBM3 미리보기)를 **독립 채널 폭 × burst length(BL) × 채널 수** 세 숫자로 정리한 비교표를 만들고, 각 칸이 왜 그 값인지 말할 수 있다.
- 피크 대역폭 = 데이터율(MT/s) × 버스 바이트 × 채널 수를 **정수 MB/s 로 정확히** 계산하고(DDR4-3200 64b = 25 600, DDR5-4800 DIMM = 38 400, LPDDR5-6400 x16 = 12 800), 비트/바이트 · MHz/MT/s · ECC 비트 · overflow 함정을 피한다.
- "burst 하나 = 채널 폭 × BL = access granularity" 로 64 B / 128 B 캐시라인에 burst 가 몇 개 필요한지 계산하고, **DDR5 가 채널을 2 × 32b 로 쪼갠 이유**와 "LPDDR x16 BL32 = 64 B" 를 prefetch(8n → 16n)로 설명한다.
- LPDDR5 의 WCK:CK 클럭 구조를 숫자(6400 MT/s → WCK 3.2 GHz, CK 800 MHz)로 말하고, 시스템 구성(데스크톱 · 노트북 · 서버 · HBM 가속기)의 채널 수, 피크, 그리고 Little 법칙으로 "피크를 채우려면 동시에 몇 개의 요청이 날아다녀야 하나"를 계산한다.

## 1. 문제

**왜 중요한가.** "이 SoC 의 메모리 대역폭은 얼마고, 왜 그 숫자죠?" 는 실리콘 검증 · AI 가속기 · SoC bring-up 면접의 단골 첫 질문이다. 답은 데이터시트 표지의 GB/s 가 아니라 **채널 폭 · BL · 채널 수 · 데이터율** 로 유도해야 하고, 그 과정에서 DDR5 서브채널 · LPDDR WCK · HBM pseudo-channel 을 아는지 드러난다.

Don 이 SSD 컨트롤러에서 본 DRAM(매핑 테이블 캐시용 DDR4/LPDDR4)도, Rivian 같은 차량 SoC 의 LPDDR5 도, 새 보드 bring-up 때 메모리 컨트롤러 설정(속도 등급, 채널 수, BL, ECC 유무)을 맞추고 STREAM 같은 측정(MEM-B11)이 **피크의 몇 %** 인지 판단하는 것으로 시작한다. 그 "피크" 를 정확히 계산하는 도구를 이 레슨에서 만든다. 주소가 어느 채널/뱅크로 가는지는 MEM-B01, 명령 타이밍은 MEM-B02, 행 버퍼 정책은 MEM-B03, 리프레시 손실은 MEM-B05 몫이다.

핵심 그림은 하나다: **독립 채널(컨트롤러가 따로 명령을 내리는 단위)의 폭과 개수**가 세대마다 다르다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">독립 채널의 폭과 개수 — 칩/채널 상자 폭 = 데이터 비트 (1 bit = 4 px, HBM 줄만 축척 다름)</text><text x="10" y="58" fill="currentColor" font-size="13">DDR4 ECC DIMM</text><text x="10" y="73" fill="currentColor" font-size="12">채널 1개</text><rect x="150" y="42" width="32" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="166" y="59" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="182" y="42" width="32" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/>
<text x="198" y="59" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="214" y="42" width="32" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="230" y="59" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="246" y="42" width="32" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="262" y="59" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="278" y="42" width="32" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/>
<text x="294" y="59" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="310" y="42" width="32" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="326" y="59" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="342" y="42" width="32" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="358" y="59" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="374" y="42" width="32" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/>
<text x="390" y="59" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="410" y="42" width="32" height="24" class="f-warn" stroke="currentColor" stroke-width="1"/><text x="426" y="59" fill="currentColor" font-size="12" text-anchor="middle">ECC</text><text x="150" y="84" fill="currentColor" font-size="12">DQ[63:0] 64b + CB[7:0] 8b = 72b, CA 버스 1개 · BL8 × 8 B = 64 B / burst</text><text x="10" y="126" fill="currentColor" font-size="13">DDR5 ECC DIMM</text><text x="10" y="141" fill="currentColor" font-size="12">서브채널 2개</text>
<rect x="150" y="110" width="32" height="24" class="f-ok" stroke="currentColor" stroke-width="1"/><text x="166" y="127" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="182" y="110" width="32" height="24" class="f-ok" stroke="currentColor" stroke-width="1"/><text x="198" y="127" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="214" y="110" width="32" height="24" class="f-ok" stroke="currentColor" stroke-width="1"/><text x="230" y="127" fill="currentColor" font-size="12" text-anchor="middle">x8</text>
<rect x="246" y="110" width="32" height="24" class="f-ok" stroke="currentColor" stroke-width="1"/><text x="262" y="127" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="282" y="110" width="32" height="24" class="f-warn" stroke="currentColor" stroke-width="1"/><text x="298" y="127" fill="currentColor" font-size="12" text-anchor="middle">ECC</text><rect x="338" y="110" width="32" height="24" class="f-ok" stroke="currentColor" stroke-width="1"/><text x="354" y="127" fill="currentColor" font-size="12" text-anchor="middle">x8</text>
<rect x="370" y="110" width="32" height="24" class="f-ok" stroke="currentColor" stroke-width="1"/><text x="386" y="127" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="402" y="110" width="32" height="24" class="f-ok" stroke="currentColor" stroke-width="1"/><text x="418" y="127" fill="currentColor" font-size="12" text-anchor="middle">x8</text><rect x="434" y="110" width="32" height="24" class="f-ok" stroke="currentColor" stroke-width="1"/><text x="450" y="127" fill="currentColor" font-size="12" text-anchor="middle">x8</text>
<rect x="470" y="110" width="32" height="24" class="f-warn" stroke="currentColor" stroke-width="1"/><text x="486" y="127" fill="currentColor" font-size="12" text-anchor="middle">ECC</text><text x="232" y="105" fill="currentColor" font-size="12" text-anchor="middle">서브채널 A (CA_A)</text><text x="420" y="105" fill="currentColor" font-size="12" text-anchor="middle">서브채널 B (CA_B)</text><text x="150" y="152" fill="currentColor" font-size="12">2 × (32b + 8b) = 80b, CA 버스 2개 (독립 스케줄) · BL16 × 4 B = 64 B / burst</text>
<text x="10" y="202" fill="currentColor" font-size="13">LPDDR5X 128b</text><text x="10" y="217" fill="currentColor" font-size="12">x16 채널 8개</text><rect x="150" y="186" width="64" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="182" y="203" fill="currentColor" font-size="12" text-anchor="middle">ch0 x16</text><rect x="218" y="186" width="64" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="250" y="203" fill="currentColor" font-size="12" text-anchor="middle">ch1 x16</text>
<rect x="286" y="186" width="64" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="318" y="203" fill="currentColor" font-size="12" text-anchor="middle">ch2 x16</text><rect x="354" y="186" width="64" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="386" y="203" fill="currentColor" font-size="12" text-anchor="middle">ch3 x16</text><rect x="422" y="186" width="64" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="454" y="203" fill="currentColor" font-size="12" text-anchor="middle">ch4 x16</text>
<rect x="490" y="186" width="64" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="522" y="203" fill="currentColor" font-size="12" text-anchor="middle">ch5 x16</text><rect x="558" y="186" width="64" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="590" y="203" fill="currentColor" font-size="12" text-anchor="middle">ch6 x16</text><rect x="626" y="186" width="64" height="24" class="f-acc" stroke="currentColor" stroke-width="1"/><text x="658" y="203" fill="currentColor" font-size="12" text-anchor="middle">ch7 x16</text>
<text x="150" y="228" fill="currentColor" font-size="12">채널마다 CA·WCK 따로 · BL32 × 2 B = 64 B (BL16 = 32 B) · SoC 옆 패키지</text><text x="10" y="278" fill="currentColor" font-size="13">HBM3 stack</text><text x="10" y="293" fill="currentColor" font-size="12">PC 32개</text><rect x="150" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="167" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="184" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/>
<rect x="201" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/><rect x="218" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="235" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="252" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/><rect x="269" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/>
<rect x="286" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="303" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="320" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/><rect x="337" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/><rect x="354" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/>
<rect x="371" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="388" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/><rect x="405" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/><rect x="422" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="439" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/>
<rect x="456" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/><rect x="473" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/><rect x="490" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="507" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="524" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/>
<rect x="541" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/><rect x="558" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="575" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="592" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/><rect x="609" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/>
<rect x="626" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="643" y="262" width="17" height="24" class="f-mut" stroke="currentColor" stroke-width="0.6"/><rect x="660" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/><rect x="677" y="262" width="17" height="24" class="f-ok" stroke="currentColor" stroke-width="0.6"/><text x="150" y="304" fill="currentColor" font-size="12">1024b = 16 ch × 2 PC(pseudo-channel) × 32b (축척 1/8) · BL8 × 4 B = 32 B · 인터포저 (MEM-A03)</text>
<text x="10" y="342" fill="currentColor" font-size="12">공통 규칙: burst 하나 = 채널 폭 × BL = access granularity. 세대가 바뀌며 BL(= prefetch n)이 2배가 되면</text><text x="10" y="360" fill="currentColor" font-size="12">채널 폭을 반으로 줄여 granularity 를 64 B 로 유지했다 (DDR4 64b×8 → DDR5 32b×16, LPDDR 16b×32).</text><text x="10" y="384" fill="currentColor" font-size="12">칩 개수·핀 이름은 x8 RDIMM 의 대표 구성 (이 레슨의 정의 — 실제 모듈은 데이터시트 확인).</text>
</svg>
```

### Part A — 기본 계산기

- `peak_mbps(mts, bus_bits, channels)`: MT/s × (bus_bits / 8) × channels [MB/s, 1 MB = 10^6 B]. 정수로 정확해야 하고, 0 · 8 의 배수가 아닌 폭 · uint64 overflow 는 0 으로 거부한다.
- `burst_bytes(ch_bits, bl)`: burst 하나가 나르는 바이트 = access granularity.
- `bursts_per_line(ch_bits, bl, line)`: 캐시라인 하나에 필요한 burst 수 = ⌈line / granularity⌉. 64 B (x86 · Arm 대부분) 와 128 B (이 맥 M6 의 `hw.cachelinesize`) 둘 다.
- `burst_ps(mts, bl)`: burst 하나의 시간 = BL × 10^6 / MT/s ps (정수 반올림).

### Part B — 세대 비교표와 설정 검증

- 세대 표 `GEN[]` (이 레슨의 정의 — JEDEC 대표값): 채널 폭, 단위당 채널 수, 허용 BL, 속도 범위, ECC side-band 비트. 이 표에서 비교표를 **코드로 출력**한다.
- `cfg_check(cfg)`: 세대 → 속도 범위 → BL → 단위 수 순서로 첫 오류를 돌려준다 (DDR4-3600 XMP 키트 = 범위 밖, DDR5 에 BL8, HBM3 에 BL4 등은 거부).
- `line_efficiency_permille`: 라인 크기와 granularity 가 안 맞으면 얼마나 버리나 (가상의 "64b DDR5 BL16" = 50 %).
- `lp5_clocks(mts, ratio)`: LPDDR5 WCK · CK 주파수 (kHz, 정수로 정확).

### Part C — 시스템 계산기 (어느 메모리를 몇 개?)

- `sys_calc(cfg, line, latency_ns)`: 독립 채널 수, 피크, granularity, 라인당 burst, burst 시간, 그리고 **Little 법칙** in-flight 바이트 = 피크 × 지연 → 동시에 날아다녀야 하는 64 B 요청 수.
- 네 가지 구성(모든 값 대표값): 데스크톱 2 × DDR5-5600 DIMM, 노트북 128-bit LPDDR5X-8533, 서버 8채널 DDR4-3200, 가속기 4-stack HBM3-6400.

## 2. Problem (English)

You are bringing up a new SoC and must sanity-check the memory subsystem numbers in the datasheet and the
measured STREAM result. Write a small, allocation-free calculator.

```c
uint64_t peak_mbps(uint32_t mts, uint32_t bus_bits, uint32_t channels);  /* MB/s (10^6), 0 if invalid/overflow */
uint32_t burst_bytes(uint32_t ch_bits, uint32_t bl);                      /* bytes per burst, 0 if invalid    */
int      bursts_per_line(uint32_t ch_bits, uint32_t bl, uint32_t line);   /* ceil(line/burst), -1 if invalid   */
uint32_t burst_ps(uint32_t mts, uint32_t bl);                             /* rounded picoseconds               */

typedef struct { mem_gen gen; uint32_t mts, bl, units; } mem_cfg;       /* units = DIMMs / x16 chans / stacks */
int      cfg_check(const mem_cfg *c);              /* CFG_OK or first error: BAD_GEN, BAD_RATE, BAD_BL, BAD_UNITS */
uint64_t cfg_peak_mbps(const mem_cfg *c);
int      line_efficiency_permille(uint32_t ch_bits, uint32_t bl, uint32_t line);
int      lp5_clocks(uint32_t mts, uint32_t ratio, uint32_t *wck_khz, uint32_t *ck_khz);  /* ratio 4, or 2 if <= 3200 */
int      sys_calc(const mem_cfg *c, uint32_t line, uint32_t latency_ns, sys_report *r);
```

Constraints: integer arithmetic only, exact results in MB/s; reject invalid configs with error codes (no asserts);
never write the report on failure.

Examples:

```text
peak_mbps(3200, 64, 1)  -> 25600     DDR4-3200, one 64-bit channel
peak_mbps(4800, 32, 2)  -> 38400     DDR5-4800 DIMM = two independent 32-bit subchannels
peak_mbps(6400, 16, 1)  -> 12800     LPDDR5-6400, one x16 channel
bursts_per_line(64, 8, 64) -> 1  bursts_per_line(32, 16, 64) -> 1  bursts_per_line(16, 16, 64) -> 2
burst_ps(3200, 8) -> 2500 (= 4 tCK)   lp5_clocks(6400, 4) -> WCK 3 200 000 kHz, CK 800 000 kHz
cfg {DDR4, 3600, BL8, 1}  -> CFG_BAD_RATE (XMP overclock, outside JEDEC bins)
sys_calc({DDR5, 5600, BL16, 2 DIMMs}, 64, 100 ns) -> 4 ch, 89600 MB/s, 1 burst/line, 8960 B = 140 lines in flight
```

Follow-ups: Why did DDR5 split the DIMM into two subchannels? Why BL32 on LPDDR? What is WCK for? You measure
60 GB/s on the 89.6 GB/s desktop — is that bad? How many outstanding misses does one core need to hit peak?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열 군데 (Part A 4, Part B 5, Part C 1). 세대 표 `GEN[]`, 하네스, 표 출력은 주어진다. 스텁은 안전한 틀린 값을 돌려준다 (피크 0, `cfg_check` 는 무엇이든 OK, `sys_calc` 는 −1 이고 보고서를 건드리지 않음). 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:149  peak_mbps(3200, 64, 1) == 25600u
FAIL starter.c:150  peak_mbps(4800, 32, 2) == 38400u
…
FAIL starter.c:220  lp5_clocks(8533, 4, &wck, &ck) == 0 && wck == 4266500u && ck == 1066625u

gen        ch_b ch/u unit    BL     gran B  n/64B  n/128B MT/s max  MB/s/unit  ECC
DDR4         64    1 DIMM    8/4    0/0     0/0    0/0        3200          0    8
…
FAIL starter.c:252  sys_calc(&ddr4, 64, 0, &z) == 0 && z.inflight_bytes == 0 && z.peak_mbps == 25600u
9/59 passed
```

(exit 1. 통과한 9개는 스텁 값이 우연히 맞은 경우 — 예: "잘못된 입력 → 0", lp5_clocks 의 "−1 이어야 하는" 경우.)

## 4. 힌트

<details><summary>힌트 1 — 단위부터 적는다</summary>

MT/s = 핀 하나가 초당 옮기는 전송(transfer) 수 × 10^6. 버스 폭은 **비트**, 대역폭은 **바이트**. 그래서 MT/s × (bits / 8) = MB/s. DDR 은 클럭 한 주기에 두 번 전송하므로 DDR4-3200 의 CK 는 1600 MHz (tCK 0.625 ns) — 클럭 MHz 를 넣으면 절반이 나온다.

</details>

<details><summary>힌트 2 — overflow 는 곱하기 전에</summary>

(uint64)mts × (bits/8) 는 2^32 × 2^29 = 2^61 미만이라 안전하다. 그다음 × channels 가 넘칠 수 있다: `if (per_ch > UINT64_MAX / channels) return 0;` 를 곱하기 **전에** 검사한다. 곱한 뒤 결과를 보고 판단하면 이미 늦다 (부호 없는 정수는 조용히 감긴다).

</details>

<details><summary>힌트 3 — burst 수는 올림 나눗셈</summary>

⌈a / b⌉ = (a + b − 1) / b (a, b > 0). line 이 granularity 보다 작아도 burst 는 1개 (BC4 같은 chop 이 아니면 덜 보낼 수 없다). 효율 = line / (burst 수 × granularity).

</details>

<details><summary>힌트 4 — DDR5 DIMM 은 "채널 2개"</summary>

DDR5 표의 ch_bits 는 32, ch_per_unit 은 2 다. DIMM 피크는 2 × 32b 이므로 64b 채널과 **같은** 식이 나오지만, burst 하나는 32b × 16 = 64 B 로 DDR4 와 같은 크기다. "64b × BL16 = 128 B" 로 계산하면 라인당 burst 가 0.5 개라는 이상한 값이 나온다.

</details>

<details><summary>힌트 5 — LPDDR5 클럭과 Little 법칙</summary>

데이터는 WCK 의 양 에지에 실린다 → WCK [MHz] = MT/s / 2, CK = WCK / 비율. kHz 로 하면 MT/s × 500 이 항상 정수. Little 법칙: 시스템 안의 평균 개수 = 도착률 × 체류 시간. 대역폭(바이트/초) × 지연(초) = 동시에 비행 중인 바이트. MB/s × ns = 10^-3 B 이므로 /1000 하고 올림한다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **세 숫자를 먼저 묻는다**: 독립 채널 하나의 데이터 폭, 데이터율(MT/s), 채널 개수. ECC 비트는 데이터 폭에서 뺀다.
2. **피크 = 곱하기**. 단위를 소리 내어 말한다: "3200 메가 전송/초 × 8 바이트 = 25.6 GB/s".
3. **granularity 를 확인한다**: 채널 폭 × BL. 캐시라인(64 B)과 맞는지 — 이게 세대 설계의 핵심 제약이다.
4. **"피크는 상한일 뿐"** 을 붙인다: 리프레시(MEM-B05), 행 충돌(MEM-B03), 읽기↔쓰기 전환, tFAW(MEM-A01) 때문에 실측은 그보다 낮다. 측정은 MEM-B11.
5. **동시성**: 피크를 채우려면 피크 × 지연만큼 요청이 날아다녀야 한다 (Little). 코어 하나로는 부족한 이유.

### Part A — 피크 대역폭 공식과 단위

이 레슨의 정의 (JESD79-4 / JESD79-5 / JESD209-4 / JESD209-5 / JESD235 / JESD238 대표값 — 실제 부품은 데이터시트 확인):

| 세대 | 독립 채널 폭 | 단위당 채널 | BL (prefetch) | 이 레슨 속도 범위 MT/s | 클럭 | 전압 (대표값) |
|---|---|---|---|---|---|---|
| DDR4 | 64b (+8 ECC) | DIMM 당 1 | BL8 (BC4), 8n | 1600–3200 | CK, DDR → CK = MT/s / 2 | VDD 1.2 V |
| DDR5 | 32b (+8 ECC) 서브채널 | DIMM 당 2 | BL16 (BL32 x4 선택), 16n | 3200–8800 | CK, DDR | VDD 1.1 V, DIMM 위 PMIC |
| LPDDR4/4X | x16 | 채널 1 (다이 하나에 보통 2채널) | BL16/BL32, 16n | 1600–4266 | CK, DDR | VDDQ 1.1 V / 4X 0.6 V |
| LPDDR5/5X | x16 | 채널 1 | BL16/BL32, 16n | 1600–9600 | CK + WCK (2:1 / 4:1) | VDDQ 0.5 V 근처 |
| HBM2E (미리보기) | 64b pseudo-channel | stack 당 16 PC (= 8 ch × 2) | BL4 | 2000–3600 | — | MEM-A03 |
| HBM3 (미리보기) | 32b pseudo-channel | stack 당 32 PC (= 16 ch × 2) | BL8 | 4000–6400 | — | MEM-A03 |

속도 범위는 "이 레슨의 검증 범위" 다: DDR5 는 처음 4800–6400 으로 나와 8800 까지 확장됐고, LPDDR5X 는 8533 이후 더 높은 등급(9600, 10667)이 발표됐다 — 최신 범위는 JEDEC 문서 확인 권장. XMP/EXPO 같은 오버클럭 프로파일은 JEDEC 등급 밖이라 일부러 거부한다 (bring-up 때 "SPD 의 JEDEC 프로파일로 먼저 띄우고, XMP 는 나중에" 가 정석).

`solution.c` 가 이 표에서 출력한 비교표 (실제 출력; `n/64B` = 64 B 라인당 burst 수, BL 이 둘이면 "첫째/둘째"):

```text
gen        ch_b ch/u unit    BL     gran B  n/64B  n/128B MT/s max  MB/s/unit  ECC
DDR4         64    1 DIMM    8/4    64/32   1/2    2/4        3200      25600    8
DDR5         32    2 DIMM    16/32  64/128  1/1    2/1        8800      70400    8
LPDDR4/4X    16    1 x16 ch  16/32  32/64   2/1    4/2        4266       8532    0
LPDDR5/5X    16    1 x16 ch  16/32  32/64   2/1    4/2        9600      19200    0
HBM2E        64   16 stack   4      32      2      4          3600     460800    0
HBM3         32   32 stack   8      32      2      4          6400     819200    0
```

**공식 유도.** 핀 하나는 초당 (MT/s × 10^6) 비트를 옮긴다. 데이터 핀이 W 개면 초당 W × MT/s × 10^6 비트 = (W/8) × MT/s × 10^6 바이트 = (W/8) × MT/s **MB/s**. 독립 채널이 C 개면 C 배. 끝.

**손으로 한 번 (데스크톱 2 × DDR5-5600):**

| 단계 | 값 | 설명 |
|---|---|---|
| 서브채널 하나 | 5600 × 4 B = 22 400 MB/s | 32b = 4 B |
| DIMM 하나 | × 2 = 44 800 MB/s | 서브채널 A, B |
| DIMM 둘 (채널 2개 보드) | × 2 = **89 600 MB/s** | 독립 채널 4개 |
| burst 시간 | 16 / 5600 µs = 2.857 ns | BL16 |
| 64 B 라인 | 32b × 16 = 64 B → burst 1개 | DDR4 와 같은 granularity |

**MHz vs MT/s.** DDR4-3200 의 "3200" 은 MT/s 다. CK 는 1600 MHz (tCK = 2 × 10^6 / 3200 ps = 625 ps, MEM-B02 의 타이밍 표와 같은 값). 데이터시트가 "1600 MHz DDR" 로 쓰기도 하고, 일부 도구는 그 1600 을 그대로 보여 준다. 공식에 1600 을 넣으면 반, "1600 MHz × 2 × 2" 처럼 두 번 곱하면 두 배가 나온다.

**burst 시간 (`burst_ps`).** BL beat 하나 = 1/MT/s µs = 10^6 / MT/s ps. DDR4-3200 BL8 = 8 × 312.5 = 2500 ps = **4 tCK** (MEM-B02 의 "BL8 = 4 tCK of data" 와 같다). DDR5-4800 BL16 = 3333 ps, LPDDR5-6400 BL32 = 5000 ps. 같은 64 B 를 옮기는데 채널이 좁을수록 오래 걸린다:

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">64 B 캐시라인 한 개를 나르는 시간 — 축척 그대로 (1 ns = 100 px, 막대 높이 = 채널 폭 비트 / 2)</text><text x="10" y="62" fill="currentColor" font-size="12">DDR4-3200 64b BL8</text><text x="10" y="77" fill="currentColor" font-size="12">8 beat · 2.5 ns = 4 tCK</text><rect x="170" y="50" width="31.25" height="32" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="201.25" y="50" width="31.25" height="32" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
<rect x="232.5" y="50" width="31.25" height="32" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="263.75" y="50" width="31.25" height="32" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="295" y="50" width="31.25" height="32" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="326.25" y="50" width="31.25" height="32" class="f-acc" stroke="currentColor" stroke-width="0.5"/><rect x="357.5" y="50" width="31.25" height="32" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
<rect x="388.75" y="50" width="31.25" height="32" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="10" y="110.0" fill="currentColor" font-size="12">DDR5-4800 32b BL16</text><text x="10" y="125.0" fill="currentColor" font-size="12">16 beat · 3.33 ns</text><rect x="170" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="190.83" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="211.67" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
<rect x="232.5" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="253.33" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="274.17" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="295" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="315.83" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
<rect x="336.67" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="357.5" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="378.33" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="399.17" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="420" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
<rect x="440.83" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="461.67" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><rect x="482.5" y="98" width="20.83" height="16" class="f-ok" stroke="currentColor" stroke-width="0.5"/><text x="10" y="156.0" fill="currentColor" font-size="12">LPDDR5-6400 x16 BL32</text><text x="10" y="171.0" fill="currentColor" font-size="12">32 beat · 5.0 ns</text><rect x="170" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<rect x="185.62" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="201.25" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="216.88" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="232.5" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="248.12" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<rect x="263.75" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="279.38" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="295" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="310.62" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="326.25" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<rect x="341.88" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="357.5" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="373.12" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="388.75" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="404.38" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<rect x="420" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="435.62" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="451.25" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="466.88" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="482.5" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<rect x="498.12" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="513.75" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="529.38" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="545" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="560.62" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<rect x="576.25" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="591.88" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="607.5" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="623.12" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><rect x="638.75" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<rect x="654.38" y="144" width="15.62" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><text x="10" y="202.0" fill="currentColor" font-size="12">HBM3-6400 PC 32b BL8</text><text x="10" y="217.0" fill="currentColor" font-size="12">8 beat × 2 burst · 2.5 ns</text><rect x="170" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="185.62" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="201.25" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
<rect x="216.88" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="232.5" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="248.12" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="263.75" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="279.38" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
<line x1="295.00" y1="186.0" x2="295.00" y2="210.0" stroke="currentColor" stroke-width="2"/><rect x="295" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="310.62" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="326.25" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="341.88" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
<rect x="357.5" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="373.12" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="388.75" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><rect x="404.38" y="190" width="15.62" height="16" class="f-mut" stroke="currentColor" stroke-width="0.5"/><line x1="170" y1="36" x2="170" y2="228.0" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.6"/>
<text x="170" y="242.0" fill="currentColor" font-size="12" text-anchor="middle">0 ns</text><line x1="270" y1="36" x2="270" y2="228.0" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.6"/><text x="270" y="242.0" fill="currentColor" font-size="12" text-anchor="middle">1 ns</text><line x1="370" y1="36" x2="370" y2="228.0" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.6"/><text x="370" y="242.0" fill="currentColor" font-size="12" text-anchor="middle">2 ns</text><line x1="470" y1="36" x2="470" y2="228.0" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.6"/>
<text x="470" y="242.0" fill="currentColor" font-size="12" text-anchor="middle">3 ns</text><line x1="570" y1="36" x2="570" y2="228.0" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.6"/><text x="570" y="242.0" fill="currentColor" font-size="12" text-anchor="middle">4 ns</text><line x1="670" y1="36" x2="670" y2="228.0" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.6"/><text x="670" y="242.0" fill="currentColor" font-size="12" text-anchor="middle">5 ns</text><text x="10" y="266.0" fill="currentColor" font-size="12">같은 64 B 라도 채널이 좁을수록 오래 걸린다 → 채널을 여러 개 두어 병렬로 채운다 (solution.c burst_ps 값)</text>
</svg>
```

그래서 좁은 채널은 **여러 개** 를 둔다. 데스크톱 DDR5 는 64 B 한 줄을 3.3 ns 동안 한 서브채널로 보내지만, 동시에 다른 서브채널이 다른 줄을 보낸다 — 총량은 같고 병렬성은 두 배.

### Part A — granularity: 왜 세대마다 BL 과 폭이 같이 움직였나

DRAM 셀 배열(core)은 수백 MHz 이상 빨라지지 않는다. 데이터율을 올리는 방법은 **한 번의 column 접근에서 핀당 n 비트를 미리 꺼내(prefetch) 직렬로 내보내는 것** 이다. n 비트를 내보내는 데 n beat 가 걸리므로 BL = n.

| 등급 | prefetch n | 핀당 core column 주기 ≈ MT/s / n | burst (64 B 채널 기준) |
|---|---|---|---|
| DDR4-1600 | 8n | 200 MHz | 64b × 8 = 64 B |
| DDR4-3200 | 8n | 400 MHz | 64b × 8 = 64 B |
| DDR5-6400 | 16n | 400 MHz | 32b × 16 = 64 B |
| LPDDR5-6400 | 16n (BL16) / BL32 = 두 번 | 400 MHz | 16b × 32 = 64 B |

DDR4 에서 3200 을 넘기려면 core 가 400 MHz 보다 빨라져야 한다 → DDR5 는 prefetch 를 16n 으로 두 배. 그런데 64b 채널에 BL16 이면 burst = **128 B**. CPU 가 64 B 라인 하나를 원할 때 128 B 를 옮기면 절반은 버린다 (`line_efficiency_permille(64, 16, 64) = 500`). 해결: 채널 폭을 32b 로 반 → 32b × 16 = 64 B 그대로, 그리고 DIMM 하나에 독립 채널이 2개 → 동시에 2 줄. LPDDR 은 처음부터 x16 이라 BL32 (= 16n 접근 두 번 이어 붙이기) 로 64 B 를 맞춘다. HBM 은 pseudo-channel 32b × BL8 = **32 B** — GPU 의 32 B 섹터(sector)에 맞춘 크기다 (Q7).

DDR4 의 bank group 도 같은 이야기다: 같은 bank group 안에서 연속 column 접근은 core 속도에 묶여 느리고(tCCD_L, 대표값 약 5 ns), 다른 bank group 으로 번갈아 가야 버스 속도(tCCD_S = 4 tCK = BL8 하나)로 이어 붙일 수 있다. 자세한 건 MEM-A01.

### Part B — 설정 검증과 라인 효율

`cfg_check` 는 **순서가 정해진 첫 오류** 를 돌려준다: 세대 → 속도 → BL → 단위 수. 펌웨어에서 "여러 개가 틀렸을 때 무엇을 보고하나" 를 정해 두지 않으면 로그가 매번 달라진다. 테스트한 거부 사례:

| 입력 | 결과 | 이유 |
|---|---|---|
| DDR4-3600 BL8 | BAD_RATE | XMP 오버클럭 등급 — JEDEC 범위 밖 |
| DDR5-3000 | BAD_RATE | 이 레슨의 DDR5 최저 등급 3200 미만 |
| DDR5 BL8 / LPDDR5 BL8 | BAD_BL | 16n prefetch 장치에 BL8 없음 (DDR5 BC8 은 chop 이라 표에서 뺐다) |
| HBM3 BL4, HBM2E BL0 | BAD_BL | HBM3 = BL8, BL 0 은 항상 거부 |
| gen 99, NULL | BAD_GEN | 표 밖 인덱스 → 배열 접근 전에 거부 |
| units 0 / 65 | BAD_UNITS | MAX_UNITS = 64 |

라인 효율 (solution 테스트 값):

| 채널 · BL | 라인 | burst | 효율 |
|---|---|---|---|
| DDR4 64b BL8 | 64 B | 1 | 1000 ‰ |
| 가상의 64b BL16 | 64 B | 1 | **500 ‰** |
| DDR5 32b BL16 | 32 B (GPU 섹터) | 1 | 500 ‰ |
| HBM3 32b BL8 | 32 B | 1 | 1000 ‰ |
| DDR4 64b BL8 | 96 B | 2 | 750 ‰ |

### Part B — LPDDR5 의 WCK

LPDDR5 는 클럭이 둘이다. **CK** 는 명령/주소(CA)용으로 느리게, **WCK**(write clock, 실제로는 읽기/쓰기 데이터 모두에 쓰는 데이터 클럭)는 빠르게 돈다. 데이터는 WCK 의 양 에지에 실린다.

```svg
<svg viewBox="0 0 740 256" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">LPDDR5-6400, WCK:CK = 4:1 — CK 2 주기 = WCK 8 주기 = DQ 16 beat = BL16 (1 ns = 240 px)</text><path d="M110 70 L110.00 40 L260.00 40 L260.00 70 L410.00 70 L410.00 40 L560.00 40 L560.00 70 L710.00 70" fill="none" stroke="currentColor" stroke-width="1.5"/><text x="10" y="54" fill="currentColor" font-size="13">CK</text><text x="10" y="69" fill="currentColor" font-size="12">800 MHz</text><path d="M110 125 L110.00 95 L147.50 95 L147.50 125 L185.00 125 L185.00 95 L222.50 95 L222.50 125 L260.00 125 L260.00 95 L297.50 95 L297.50 125 L335.00 125 L335.00 95 L372.50 95 L372.50 125 L410.00 125 L410.00 95 L447.50 95 L447.50 125 L485.00 125 L485.00 95 L522.50 95 L522.50 125 L560.00 125 L560.00 95 L597.50 95 L597.50 125 L635.00 125 L635.00 95 L672.50 95 L672.50 125 L710.00 125" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="10" y="109" fill="currentColor" font-size="13">WCK</text><text x="10" y="124" fill="currentColor" font-size="12">3200 MHz</text><text x="10" y="164" fill="currentColor" font-size="13">DQ (x16)</text><text x="10" y="179" fill="currentColor" font-size="12">6400 MT/s</text><rect x="110" y="150" width="37.5" height="30" class="f-acc" stroke="currentColor" stroke-width="0.8"/><text x="129" y="170" fill="currentColor" font-size="12" text-anchor="middle">0</text><rect x="147.5" y="150" width="37.5" height="30" class="f-ok" stroke="currentColor" stroke-width="0.8"/>
<text x="166" y="170" fill="currentColor" font-size="12" text-anchor="middle">1</text><rect x="185" y="150" width="37.5" height="30" class="f-acc" stroke="currentColor" stroke-width="0.8"/><text x="204" y="170" fill="currentColor" font-size="12" text-anchor="middle">2</text><rect x="222.5" y="150" width="37.5" height="30" class="f-ok" stroke="currentColor" stroke-width="0.8"/><text x="241" y="170" fill="currentColor" font-size="12" text-anchor="middle">3</text><rect x="260" y="150" width="37.5" height="30" class="f-acc" stroke="currentColor" stroke-width="0.8"/>
<text x="279" y="170" fill="currentColor" font-size="12" text-anchor="middle">4</text><rect x="297.5" y="150" width="37.5" height="30" class="f-ok" stroke="currentColor" stroke-width="0.8"/><text x="316" y="170" fill="currentColor" font-size="12" text-anchor="middle">5</text><rect x="335" y="150" width="37.5" height="30" class="f-acc" stroke="currentColor" stroke-width="0.8"/><text x="354" y="170" fill="currentColor" font-size="12" text-anchor="middle">6</text><rect x="372.5" y="150" width="37.5" height="30" class="f-ok" stroke="currentColor" stroke-width="0.8"/>
<text x="391" y="170" fill="currentColor" font-size="12" text-anchor="middle">7</text><rect x="410" y="150" width="37.5" height="30" class="f-acc" stroke="currentColor" stroke-width="0.8"/><text x="429" y="170" fill="currentColor" font-size="12" text-anchor="middle">8</text><rect x="447.5" y="150" width="37.5" height="30" class="f-ok" stroke="currentColor" stroke-width="0.8"/><text x="466" y="170" fill="currentColor" font-size="12" text-anchor="middle">9</text><rect x="485" y="150" width="37.5" height="30" class="f-acc" stroke="currentColor" stroke-width="0.8"/>
<text x="504" y="170" fill="currentColor" font-size="12" text-anchor="middle">10</text><rect x="522.5" y="150" width="37.5" height="30" class="f-ok" stroke="currentColor" stroke-width="0.8"/><text x="541" y="170" fill="currentColor" font-size="12" text-anchor="middle">11</text><rect x="560" y="150" width="37.5" height="30" class="f-acc" stroke="currentColor" stroke-width="0.8"/><text x="579" y="170" fill="currentColor" font-size="12" text-anchor="middle">12</text><rect x="597.5" y="150" width="37.5" height="30" class="f-ok" stroke="currentColor" stroke-width="0.8"/>
<text x="616" y="170" fill="currentColor" font-size="12" text-anchor="middle">13</text><rect x="635" y="150" width="37.5" height="30" class="f-acc" stroke="currentColor" stroke-width="0.8"/><text x="654" y="170" fill="currentColor" font-size="12" text-anchor="middle">14</text><rect x="672.5" y="150" width="37.5" height="30" class="f-ok" stroke="currentColor" stroke-width="0.8"/><text x="691" y="170" fill="currentColor" font-size="12" text-anchor="middle">15</text><line x1="110.0" y1="34" x2="110.0" y2="190" stroke="currentColor" stroke-dasharray="3 3" stroke-width="0.7"/>
<text x="110.0" y="206" fill="currentColor" font-size="12" text-anchor="middle">0 ns</text><line x1="410.0" y1="34" x2="410.0" y2="190" stroke="currentColor" stroke-dasharray="3 3" stroke-width="0.7"/><text x="410.0" y="206" fill="currentColor" font-size="12" text-anchor="middle">1.25 ns</text><line x1="710.0" y1="34" x2="710.0" y2="190" stroke="currentColor" stroke-dasharray="3 3" stroke-width="0.7"/><text x="710.0" y="206" fill="currentColor" font-size="12" text-anchor="middle">2.5 ns</text><text x="10" y="228" fill="currentColor" font-size="12">명령/주소는 느린 CK 에, 데이터는 WCK 양 에지에 — 빠른 클럭은 데이터가 오갈 때만 돌린다 (전력).</text>
<text x="10" y="244" fill="currentColor" font-size="12">WCK = 6400 / 2 = 3200 MHz, CK = 3200 / 4 = 800 MHz. 비율 2:1 은 ≤ 3200 MT/s 에서 (대표값 — JESD209-5 확인)</text>
</svg>
```

`lp5_clocks` 결과 (테스트 값): 6400 MT/s, 4:1 → WCK 3 200 000 kHz, CK 800 000 kHz. 3200 MT/s, 2:1 → WCK 1 600 000, CK 800 000. LPDDR5X-8533, 4:1 → WCK 4 266 500 kHz, CK 1 066 625 kHz. 6400 에 2:1 은 거부 (CK 가 1.6 GHz 가 되어야 함). 왜 이렇게 하나 (대표 설명, JESD209-5 확인 권장): 빠른 WCK 는 데이터가 오갈 때만 켜고(WCK 동기화 후), 명령 버스는 느린 CK 로 두어 **전력** 을 줄인다. LPDDR4 는 CK 하나 + DQS 스트로브 구조였다. 그 밖에 LPDDR5 는 bank 구조 모드(BG 모드 4 BG × 4 bank / 8B / 16B 모드)와 DVFS 를 지원한다 — 세부는 부품 데이터시트.

### Part C — 시스템 계산기와 Little 법칙

`solution.c` 출력 (실제; 지연 100 ns 는 **가정값** — 실제 idle 지연은 MEM-B10 에서 잰다):

```text
system (lat 100 ns)            ch      MB/s  gran  b/64B bst_ps inflightB  lines64
desktop 2x DDR5-5600 DIMM       4     89600   64B      1   2857      8960      140
laptop 128b LPDDR5X-8533        8    136528   64B      1   3750     13653      214
server 8ch DDR4-3200            8    204800   64B      1   2500     20480      320
accel 4-stack HBM3-6400       128   3276800   32B      2   1250    327680     5120
```

```svg
<svg viewBox="0 0 740 248" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">시스템 피크 대역폭 (solution.c 계산값, MB/s, 가로축 log10)</text><text x="10" y="34" fill="currentColor" font-size="12">막대 끝 = 피크 MB/s · 피크를 채우려면 동시에 날아다녀야 하는 64 B 요청 수 (지연 100 ns 가정, Little 법칙)</text><line x1="230" y1="44" x2="230" y2="222" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.6"/><text x="230" y="238" fill="currentColor" font-size="12" text-anchor="middle">10 GB/s</text><line x1="350" y1="44" x2="350" y2="222" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.6"/>
<text x="350" y="238" fill="currentColor" font-size="12" text-anchor="middle">100 GB/s</text><line x1="470" y1="44" x2="470" y2="222" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.6"/><text x="470" y="238" fill="currentColor" font-size="12" text-anchor="middle">1 TB/s</text><line x1="590" y1="44" x2="590" y2="222" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.6"/><text x="590" y="238" fill="currentColor" font-size="12" text-anchor="middle">10 TB/s</text><text x="10" y="62" fill="currentColor" font-size="12">DDR4-3200 1 DIMM</text>
<rect x="230" y="48" width="48.99" height="18" class="f-mut" stroke="currentColor" stroke-width="0.8"/><text x="285" y="62" fill="currentColor" font-size="12">25,600 · 40 라인</text><text x="10" y="91" fill="currentColor" font-size="12">DDR5-4800 1 DIMM</text><rect x="230" y="77" width="70.12" height="18" class="f-mut" stroke="currentColor" stroke-width="0.8"/><text x="306" y="91" fill="currentColor" font-size="12">38,400 · 60 라인</text><text x="10" y="120" fill="currentColor" font-size="12">desktop 2×DDR5-5600</text>
<rect x="230" y="106" width="114.28" height="18" class="f-ok" stroke="currentColor" stroke-width="0.8"/><text x="350" y="120" fill="currentColor" font-size="12">89,600 · 140 라인</text><text x="10" y="149" fill="currentColor" font-size="12">laptop 128b LPDDR5X-8533</text><rect x="230" y="135" width="136.23" height="18" class="f-ok" stroke="currentColor" stroke-width="0.8"/><text x="372" y="149" fill="currentColor" font-size="12">136,528 · 214 라인</text><text x="10" y="178" fill="currentColor" font-size="12">server 8ch DDR4-3200</text>
<rect x="230" y="164" width="157.36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.8"/><text x="393" y="178" fill="currentColor" font-size="12">204,800 · 320 라인</text><text x="10" y="207" fill="currentColor" font-size="12">accel 4×HBM3-6400</text><rect x="230" y="193" width="301.85" height="18" class="f-warn" stroke="currentColor" stroke-width="0.8"/><text x="538" y="207" fill="currentColor" font-size="12">3,276,800 · 5120 라인</text>
</svg>
```

**Little 법칙.** 평균 동시 개수 L = 도착률 λ × 체류 시간 W. 메모리로 옮기면 "비행 중인 바이트 = 대역폭 × 지연". 89 600 MB/s × 100 ns = 89 600 × 10^6 × 100 × 10^-9 = 8960 B = **140 개의 64 B 요청** 이 항상 미해결(outstanding) 상태여야 피크가 나온다. 코어 하나가 동시에 낼 수 있는 L1 miss 수(MSHR / fill buffer)는 보통 수십 개 수준이라(대표값, 코어마다 다름) **코어 하나로는 피크를 못 채운다** — MEM-B11 에서 STREAM 을 스레드 수별로 재는 이유. HBM 가속기는 5120 개: 수천 개의 스레드(GPU warp)가 지연을 숨기는 구조여야만 쓸 수 있는 대역폭이다.

**노트북 행의 반올림.** LPDDR5X-8533 의 실제 데이터율은 8533.33 MT/s (= 25600/3) 라 정확한 피크는 136 533.33 MB/s 다. 이름 그대로 8533 을 넣으면 136 528 — 이 레슨의 주 계산기는 "등급 이름 × 폭" 을 정수로 정확히 계산하는 것이고, 정확한 유리수 계산은 연습 1 에서 한다.

**이 맥(M6)은?** 실제로 확인한 것 (`system_profiler SPMemoryDataType` · `sysctl`, 실제 출력):

```text
      Memory: 32 GB
      Type: LPDDR5
      Manufacturer: Samsung
hw.memsize: 34359738368
hw.cachelinesize: 128
machdep.cpu.brand_string: Apple M6
```

버스 폭과 데이터율은 macOS 가 보여 주지 않는다. M 시리즈 기본 칩이 지금까지 128-bit 통합 메모리였다는 기억에 기대면(공식 수치 확인 권장) 128-bit LPDDR5X-8533 가정 시 피크 136.5 GB/s, 64 B 라인 기준 214 개 in-flight. 이 맥의 라인은 128 B 라 x16 BL32 burst **2개** 가 한 줄이다 (`bursts_per_line(16, 32, 128) = 2`). 어느 가정이 맞는지는 MEM-B11 의 실측 대역폭과 비교해서 판단한다.

### 그 밖에 비교표에 들어가는 것 (풀이용 메모, 대표값)

- **핀과 거리.** DDR DIMM 은 메인보드 위 긴 배선 + 슬롯(DIMM 288핀)이라 신호 무결성 때문에 핀당 속도와 DIMM 수(1DPC/2DPC)가 제한되고, DDR5 는 수신 측 등화기(DFE)와 트레이닝(MEM-A04)에 많이 기댄다. LPDDR 은 SoC 바로 옆(PoP 또는 같은 패키지)에 붙어 배선이 짧고 종단을 줄일 수 있어 핀당 속도는 높고 전력은 낮지만, **소켓이 없어 증설 불가**. HBM 은 인터포저 위 1024 핀/stack 으로 핀당 속도가 아니라 **핀 개수** 로 이긴다 (연습 5).
- **에너지.** 비트당 I/O 에너지는 HBM < LPDDR < DDR 순이라는 게 공개 자료의 일관된 경향이다 (수치는 자료마다 달라 여기서는 순서만).
- **ECC.** DDR4 ECC DIMM 은 72b (64 + 8, 12.5 %), DDR5 ECC DIMM 은 서브채널마다 40b (32 + 8) 라 80b (25 %). DDR5 의 **on-die ECC** 는 다이 내부 128 + 8 비트 SEC 로 셀 신뢰도용이고 호스트에 오류를 알리지 않으며 버스 오류는 못 잡는다 → 시스템 ECC 를 대신하지 않는다 (MEM-B07 의 72/64 SECDED, SIV-M14 의 CE/UE).
- **리프레시 · 저전력.** DDR5 는 same-bank refresh 로 리프레시 손실을 줄이고(MEM-B05), LPDDR 은 deep sleep · 부분 어레이 셀프 리프레시 등 저전력 상태가 풍부하다(MEM-M08).

### 복잡도

모든 함수가 O(1), 표는 정적 상수 6행. 테스트 59개 (성질 테스트 2000 회 포함)가 1 ms 안팎으로 끝난다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 59/59 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan
```

## 7. 자주 하는 실수

**1. 비트와 바이트.** `peak = mts * bus_bits` → DDR4-3200 = 204 800 "MB/s"? 그건 Mb/s 다. 8 로 나눠야 25 600 MB/s.

```c
uint64_t per_ch = (uint64_t)mts * (bus_bits / 8u);   /* 바이트로 바꾼 뒤 곱한다 */
```

**2. DDR5 DIMM 을 64b × BL16 으로 계산.** 피크는 우연히 같지만(64b = 2 × 32b) granularity 가 128 B 로 틀리고, "DIMM 하나 = 채널 하나" 로 세어 스케줄링 병렬성을 절반으로 본다. DDR5 는 `ch_bits = 32, ch_per_unit = 2`.

**3. ECC 비트를 대역폭에 포함.** 72b × 3200 / 8 = 28 800 MB/s 는 쓸 수 있는 대역폭이 아니다. ECC 비트는 같은 시간에 같이 날아가지만 사용자 데이터가 아니다 → 채널 폭은 64b (DDR5 는 32b).

**4. 32비트 정수 overflow.** 4-stack HBM3 를 B/s 로 계산하면 3.28 × 10^12 — `uint32_t` 는 4.29 × 10^9 에서 감긴다. 이 레슨은 MB/s 단위 + `uint64_t` + **곱하기 전** 검사:

```c
if (per_ch > UINT64_MAX / channels) return 0;   /* 곱한 뒤 비교하면 이미 감긴 값 */
```

**5. GB 와 GiB 혼동.** 25 600 MB/s = 25.6 GB/s (10^9) = 23.84 GiB/s (2^30). STREAM 은 10^6 바이트를 MB 로 쓴다. 측정 도구가 GiB/s 로 찍으면 "피크의 93 %" 로 잘못 보인다.

**6. LPDDR 의 "x16 채널" 을 칩/패키지 폭과 혼동.** "128-bit LPDDR5X" 는 독립 x16 채널 **8개** 다. 채널 1개(128b)로 보면 granularity 가 128 × 32 / 8 = 512 B 라는 엉뚱한 값이, 다이 단위(x32)로 보면 채널 수가 절반이 된다.

**7. 속도 등급 이름을 정확한 값으로 착각.** DDR4-2666 의 tCK 는 0.75 ns → 2666.67 MT/s. 이름으로 계산하면 64b 피크가 21 328 (정확히는 21 333.33). 작은 차이지만 "몇 채널이 필요한가" 같은 올림 계산에서 답이 바뀐다 (연습 1: 18 vs 19).

## 8. 면접 Q&A

<details><summary>Q1. DDR5 는 왜 DIMM 을 32비트 서브채널 두 개로 쪼갰나요?</summary>

데이터율을 올리려면 prefetch 를 8n → 16n 으로 늘려야 했고(core 는 ~400 MHz 에서 더 못 빨라짐), 그러면 BL16 이 된다. 64b 채널에 BL16 이면 burst 하나가 128 B 라 64 B 라인을 읽을 때 절반을 버린다. 폭을 32b 로 줄이면 32b × 16 = 64 B 로 granularity 가 유지되고, DIMM 하나에 독립 채널이 둘이 되어 동시에 두 요청을 처리한다 (뱅크도 DDR4 16개 → DDR5 32개, x8 16 Gb 기준). 비용은 명령/주소 버스가 둘이 되고 ECC 오버헤드가 8/64 → 8/32 (12.5 % → 25 %) 로 늘어난 것. 피크 대역폭 공식 자체는 64b 와 같다.

</details>

<details><summary>Q2. DDR5-4800 DIMM 하나의 대역폭은? 실제로 그만큼 나오나요?</summary>

4800 × 4 B × 2 = 38 400 MB/s. 실제는 그보다 낮다: 리프레시가 시간의 몇 %를 먹고(MEM-B05: 16 Gb DDR4 기준 tRFC/tREFI ≈ 7 %), 행 충돌 시 tRP + tRCD 동안 그 뱅크의 데이터가 없고(MEM-B03), 읽기↔쓰기 전환마다 버스가 비고, tFAW 가 ACT 를 제한한다(MEM-A01). 그래서 STREAM 이 피크의 대략 60–80 % 근처면 보통으로 본다는 경험칙이 있다 (플랫폼마다 다름 — 이 맥 실측은 MEM-B11). 측정이 피크의 30 % 면 설정 문제(채널 하나만 살아 있음, 단일 랭크, 느린 속도로 트레이닝됨)를 의심한다.

</details>

<details><summary>Q3. LPDDR 은 왜 x16 채널에 BL32 를 쓰나요? 모바일에 왜 좋죠?</summary>

LPDDR 은 SoC 옆에 붙는 짧은 배선용이라 좁은 채널을 여러 개 두기 쉽다. x16 × BL16 = 32 B 라 64 B 라인을 원하면 BL32 (16n 접근 두 번을 이어 붙임)로 한 번에 보낸다. 좁은 독립 채널이 많으면 (1) 채널마다 따로 저전력 상태로 보낼 수 있고, (2) 동시에 여러 요청을 처리하고, (3) 핀당 전력이 낮다. 대신 채널마다 CA 버스가 있어 컨트롤러가 복잡하고, 소켓이 없어 증설이 안 된다.

</details>

<details><summary>Q4. LPDDR5 의 WCK 가 뭔가요? CK 와 왜 따로 두죠?</summary>

WCK 는 데이터용 고속 클럭이다. 6400 MT/s 면 데이터는 WCK 양 에지에 실리므로 WCK = 3.2 GHz, 명령/주소용 CK 는 4:1 이면 800 MHz. 명령 버스까지 3.2 GHz 로 돌릴 필요가 없고, WCK 는 데이터가 오갈 때만 켜서(동기화 절차 후) 전력을 아낀다. 2:1 비율은 낮은 속도(대표값 ≤ 3200 MT/s)에서. LPDDR4 는 CK + 양방향 DQS 스트로브였다. 정확한 동기화 절차와 비율 규칙은 JESD209-5 확인.

</details>

<details><summary>Q5. DDR5 에 on-die ECC 가 있으니 ECC DIMM 은 필요 없나요?</summary>

아니다. on-die ECC 는 다이 안에서 128 데이터 + 8 패리티로 단일 비트를 조용히 고치는 셀 신뢰도 장치다 — 미세 공정에서 늘어난 셀 약점을 가리려고 넣었다. 버스 위 오류는 못 보고, 호스트에 CE/UE 를 알리지 않으며(EDAC 로그가 안 남음), 2비트 오류는 오히려 잘못 고쳐 3비트로 만들 수도 있다. 서버/차량용 RAS 는 여전히 side-band ECC (DDR5 ECC DIMM 80b, MEM-B07 의 SECDED, Chipkill 은 MEM-A05) 와 CE/UE 보고(SIV-M14, MEM-A06) 가 필요하다.

</details>

<details><summary>Q6. HBM 은 핀당 속도가 DDR5 와 비슷한데 왜 대역폭이 10배 이상이죠?</summary>

핀 개수다. HBM3 stack 하나 = 1024 데이터 핀 × 6.4 Gb/s = 819.2 GB/s. DDR5-6400 DIMM 은 64 데이터 핀 × 6.4 = 51.2 GB/s. 1024 핀은 PCB 로는 불가능하고 실리콘 인터포저 위에서 SoC 바로 옆(수 mm)에 붙이기 때문에 가능하다 — 배선이 짧아 비트당 에너지도 낮다. 대가는 용량(stack 당 최대 수십 GB 수준), 가격, 패키징(CoWoS 류), 열. 세부는 MEM-A03.

</details>

<details><summary>Q7. GPU 는 32 B 섹터를 쓰는데, DDR5 와 HBM 중 어디에 잘 맞나요?</summary>

granularity 를 비교한다. HBM3 pseudo-channel 32b × BL8 = 32 B → 섹터 하나 = burst 하나, 효율 100 %. DDR5 서브채널 32b × BL16 = 64 B → 32 B 하나 원해도 64 B 를 옮겨 50 %. 랜덤 32 B 접근이 많은 워크로드에서는 이 차이가 실효 대역폭을 직접 깎는다. 그래서 GPU 메모리(GDDR, HBM)는 granularity 를 작게 유지하는 쪽으로 설계됐다 (GDDR 세부는 범위 밖).

</details>

<details><summary>Q8. 피크를 채우려면 요청이 몇 개나 동시에 있어야 하죠? 코어 하나로 되나요?</summary>

Little 법칙: 89.6 GB/s × 100 ns(가정) = 8960 B = 64 B 요청 140개. 코어 하나의 미해결 L1 miss 수(MSHR/fill buffer)는 대개 수십 개 수준이라 하드웨어 prefetcher 의 도움 없이 코어 하나가 피크를 채우기 어렵다 → 스레드를 늘려야 대역폭이 올라간다 (MEM-B11 실측, prefetcher 는 MEM-A07). 지연이 길어지면(원격 NUMA, MEM-M09) 같은 대역폭에 더 많은 요청이 필요하다.

</details>

<details><summary>Q9. 새 보드 bring-up 에서 메모리 대역폭이 기대의 절반입니다. 계산기로 무엇을 확인하나요?</summary>

계산기의 각 항을 실제 설정과 맞춘다: (1) 데이터율 — 트레이닝(MEM-A04)이 실패해 낮은 등급으로 내려왔나 (부트 로그/SPD/컨트롤러 레지스터), (2) 채널 수 — 채널 하나가 죽었거나 DIMM 이 한 채널에 몰렸나 (절반이면 이게 1순위), (3) 버스 폭 — x16 장치를 x32 자리에 설정, (4) 인터리빙 — 주소 매핑이 한 채널에만 몰리게 설정됐나 (MEM-B01, MEM-M02). 그 뒤에야 행 충돌 · 리프레시 같은 효율 문제를 본다.

</details>

## 9. 추가 연습

### 연습 1 — 속도 등급 이름은 반올림된 값

DDR4-2666 은 tCK 0.75 ns 라 실제로 2666.67 MT/s 다. JEDEC 등급의 실제 데이터율이 100/3 MT/s 의 배수라는 관찰(이 레슨의 규칙: 2666.67 = 8000/3, 8533.33 = 25600/3, 3200 = 9600/3)을 써서 `grade_exact(nominal) → num/den` 을 짜고, DDR4-2666 64b 의 정확한 피크와, 8채널 DDR5-4800 서버(307 200 MB/s)를 LPDDR5X-8533 x16 채널로 대체하려면 몇 채널이 필요한지 이름 그대로와 정확한 값으로 각각 구하라.

<details><summary>답 보기</summary>

3 × 이름을 100 단위로 반올림 → / 3 (이름과 1 이상 차이 나면 거부). DDR4-2666 피크 = 8000/3 × 8 = **64000/3 = 21 333.33 MB/s** (이름으로는 21 328). 채널 수: 이름 그대로 채널당 17 066 → 17 066 × 18 = 307 188 < 307 200 이라 **19**. 정확히는 채널당 51200/3 → × 18 = 307 200 정확히 → **18**. 올림이 들어가는 계산은 정확한 값으로.

</details>

### 연습 2 — ECC 오버헤드

DDR4 ECC DIMM, DDR5 ECC DIMM, DDR5 on-die ECC 의 패리티 오버헤드를 ‱ (1/10000) 로, DIMM 데이터 버스 폭(ECC 포함)을 구하라.

<details><summary>답 보기</summary>

8/64 = **1250 ‱** (72b DIMM), 8/32 = **2500 ‱** (2 × 40 = 80b DIMM), on-die 8/128 = **625 ‱** (호스트에 안 보임). DDR5 ECC DIMM 은 칩 10개(x8: 서브채널마다 데이터 4 + ECC 1)라 DDR4 ECC(9개)보다 칩이 상대적으로 더 든다.

</details>

### 연습 3 — burst 는 CK 몇 주기?

`burst_ck(bl, ratio)`: ratio 1 = 데이터가 CK 양 에지(DDR4/5), 2·4 = LPDDR5 WCK:CK. 한 CK 주기에 2 × ratio beat 이므로 BL / (2·ratio), 나누어떨어지지 않으면 −1.

<details><summary>답 보기</summary>

DDR4 BL8 = **4 tCK** (MEM-B02 와 일치), DDR5 BL16 = 8 tCK, LPDDR5 BL16 @ 4:1 = **2 CK**, BL32 @ 4:1 = 4 CK, BL16 @ 2:1 = 4 CK. BL4 @ 4:1 = 0.5 CK → −1. 명령은 CK 단위로 나가므로, 4:1 에서는 CK 2 주기마다 BL16 명령을 하나씩 내야 데이터 버스가 꽉 찬다.

</details>

### 연습 4 — 라인 크기 32 / 64 / 128 B × 세대

GPU 섹터(32 B), x86 라인(64 B), 이 맥 M6 라인(128 B)마다 burst 수와 효율을 표로 출력하라.

<details><summary>답 보기</summary>

practice.c 실제 출력:

```text
config            32B n/eff      64B n/eff     128B n/eff
DDR4 BL8          1/ 500‰        1/1000‰        2/1000‰
DDR5 BL16         1/ 500‰        1/1000‰        2/1000‰
LPDDR5 BL16       1/1000‰        2/1000‰        4/1000‰
LPDDR5 BL32       1/ 500‰        1/1000‰        2/1000‰
HBM3 BL8          1/1000‰        2/1000‰        4/1000‰
```

64 B 이상이면 모두 100 % — 낭비는 라인이 granularity 보다 **작을** 때만 생긴다. 128 B 라인은 burst 2개(또는 4개)를 같은 채널에 이어 보내거나 두 채널에 나눠 보낼 수 있다 (어느 쪽인지는 주소 매핑 — MEM-B01).

</details>

### 연습 5 — 1 TB/s 에 데이터 핀이 몇 개?

DDR5-6400 서브채널, LPDDR5X-8533 x16 (정확한 데이터율), HBM3-6400 (stack 단위로만 구매)로 1 000 000 MB/s 를 채우는 데 필요한 채널 수와 데이터 핀 수.

<details><summary>답 보기</summary>

practice.c 실제 출력:

```text
1 TB/s: DDR5-6400 40 subch = 1280 pins (20 DIMM), LPDDR5X-8533 59 ch = 944 pins, HBM3-6400 40 PC -> 2 stacks = 2048 pins
```

DDR5-6400 과 HBM3-6400 은 핀당 속도가 같아 필요한 핀 수(1280)도 같다 — 차이는 **그 핀을 어디에 놓느냐**: DIMM 20개(메인보드 슬롯) vs 인터포저 위 stack 2개(1.25 stack 이면 되지만 stack 단위라 2048 핀). LPDDR5X 는 핀당 속도가 더 높아 핀이 가장 적지만 채널 59개를 SoC 둘레에 붙여야 한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 17/17 passed
```

## 10. 핵심 정리

- 피크 [MB/s] = MT/s × (독립 채널 폭 비트 / 8) × 채널 수. ECC 비트 제외, MT/s ≠ MHz (DDR: CK = MT/s / 2).
- 기준값: DDR4-3200 64b **25 600**, DDR5-4800 DIMM (2 × 32b) **38 400**, LPDDR5-6400 x16 **12 800**, HBM3-6400 stack **819 200** MB/s.
- burst = 채널 폭 × BL = access granularity. DDR4 64b × 8, DDR5 32b × 16, LPDDR x16 × 32 → 모두 **64 B**. HBM3 PC 32b × 8 = 32 B.
- prefetch n = BL. core 는 ~400 MHz 에 묶여 데이터율을 올리려면 n 을 키운다 → BL 이 커진 만큼 채널을 좁혀 64 B 유지 → DDR5 서브채널, 채널 수 2배.
- LPDDR5: 데이터는 WCK 양 에지 (6400 → WCK 3.2 GHz), CA 는 CK (4:1 → 800 MHz). 전력 때문에 클럭을 나눴다.
- 시스템: 2 × DDR5-5600 = 89.6 GB/s (4 ch), 128b LPDDR5X-8533 = 136.5 GB/s (8 ch), 4 × HBM3 = 3.28 TB/s (128 PC).
- Little: 피크 × 지연 = in-flight 바이트 → 100 ns 가정 시 140 / 214 / 5120 개의 64 B 요청. 코어 하나로는 못 채운다.
- 피크는 상한. 실측(MEM-B11)과 비교해 절반이면 채널 수 · 데이터율 · 인터리빙 설정부터 의심.

## 참고자료

- JEDEC JESD79-4 (DDR4 SDRAM), JESD79-5 (DDR5 SDRAM), JESD209-4 (LPDDR4/4X), JESD209-5 (LPDDR5/5X), JESD235 (HBM/HBM2), JESD238 (HBM3) — 이 레슨의 수치는 이 표준들의 **대표값** 이고 표준 문서는 유료라 직접 대조하지 못했다. 속도 범위 · BL 옵션 · WCK 비율 규칙은 표준/데이터시트 확인 권장: <https://www.jedec.org/>
- Micron DDR4 / DDR5 SDRAM 데이터시트와 "Introducing DDR5" 백서 — 서브채널, BL16, on-die ECC, PMIC 설명 (벤더 문서, 버전 확인 권장).
- Samsung / Micron LPDDR5 · LPDDR5X 제품 소개 자료 — WCK, 데이터율 등급 (벤더 문서).
- J. McCalpin, STREAM benchmark — MB = 10^6 바이트 관례: <https://www.cs.virginia.edu/stream/>
- B. Jacob, S. Ng, D. Wang, *Memory Systems: Cache, DRAM, Disk* (Morgan Kaufmann, 2007) — prefetch, burst, 채널 구조의 교과서적 설명.
- J. D. C. Little, "A Proof for the Queuing Formula: L = λW", Operations Research, 1961.
- 관련 레슨: MEM-B01 (주소 → 채널/뱅크 디코더), MEM-B02 (DDR4-3200 타이밍, tCK 625 ps), MEM-B03 (행 버퍼 정책), MEM-B05 (리프레시 손실), MEM-B07 (SECDED 72/64), MEM-B10 (지연 측정), MEM-B11 (STREAM 실측), MEM-M02 (매핑과 뱅크 병렬성), MEM-M08 (저전력 상태), MEM-M09 (NUMA), MEM-A01 (bank group · tFAW), MEM-A03 (HBM), MEM-A04 (트레이닝), MEM-A05 (Chipkill), MEM-A06 (ECC RAS), MEM-A07 (prefetcher), SIV-M14 (ECC 오류 주입), ARCH-B10 (AMAT), AIPERF-B04 · MEM-A08 (Roofline)
