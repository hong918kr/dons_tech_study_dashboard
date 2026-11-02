# N07D · UART · GPIO · 인터럽트 디버그
<!-- theme: pastel -->
> **묶음**: N07 인터페이스 디버그 · 저속 I/O · **우선**: P1 · **목록**: [N07 인덱스](2026-11-01_N07_interface_debug_scenarios.html) · **이전/다음**: [N07C SPI, QSPI flash 디버그](2026-11-01_N07C_spi_qspi_debug.html) / [N07E DDR 메모리 디버그](2026-11-01_N07E_ddr_memory_debug.html) · **관련**: [BV10 §2.3–2.4, 체인 F](2026-11-01_BV10_board_interface_tests.html) · [BV13 S3 UART 출력 없음](2026-11-01_BV13_board_debug_scenarios.html) · [N06C C3 UART, C4 GPIO, C6 GIC 테스트 플랜](2026-11-01_N06C_plans_peripherals_soc_infra.html) · [deck §7 #4 인터럽트 누락](2026-10-25_rivian_hm_screen_prep.html) · [최종 훑어보기 §5](2026-10-30_final_skim.html) · [N06H 디버그 방법론](2026-11-01_N06H_debug_methodology_triage.html) · [BV04 클럭](2026-11-01_BV04_clocks_osc_pll_refclk.html) · [BV11 계측 장비](2026-11-01_BV11_lab_instruments_measurement.html) · [N07A 공통 틀](2026-11-01_N07A_debug_framework_instruments.html) · **코드**: onsitePrep/n07/d/ ([run.sh](onsitePrep/n07/d/run.sh), [uart_baud_sim.c](onsitePrep/n07/d/uart_baud_sim.c), [divisor_cases.txt](onsitePrep/n07/d/divisor_cases.txt), [run_output.txt](onsitePrep/n07/d/run_output.txt))

## 0. TL;DR

- **이 페이지로 답할 것**: "The console prints garbage", "115200 works but 3 Mbaud fails", "framing errors only when hot", "console dies when Linux starts", "an interrupt is sometimes missed / storms / fires twice / doesn't wake the system". UART 4개(D1–D4) + GPIO/IRQ 4개(D5–D8).
- **외울 것 ①**: UART 깨진 글자 = **먼저 비트 폭을 잰다**. `0x55`('U')를 연속으로 보내면 8N1 프레임이 완전한 0/1 교대 → **scope 주파수 = baud/2** (115200 → 57.6 kHz). 측정/기대 비율이 26/24, 19.2/24 같은 깔끔한 숫자면 **클럭 가정 오류**, 2–4%면 **분주 반올림**.
- **외울 것 ②**: 허용 오차는 **프레임 마지막 샘플(stop, 9.5비트 지점)이 비트 경계를 넘지 않을 조건**에서 나온다. 16× 중앙 샘플·깨끗한 엣지면 TX 빠름 +4.57% / TX 느림 −5.26% (§2.3 계산, 코드로 재현). edge 왜곡·노이즈 여유를 남기려고 **각 쪽 ±2% 이내**가 실무 규칙.
- **외울 것 ③**: 카운터가 계층을 알려 준다 — `fe`/`pe` = 타이밍(baud·신호), `oe` = HW FIFO overrun(IRQ 지연), `bo` = tty 버퍼 overrun(유저 공간이 느림). 인터럽트는 `/proc/interrupts` 증가율 + `/sys/kernel/irq/<n>/type` + 장치 쪽 이벤트 카운터를 **세 개 같이** 본다.
- **IRQ 4대 증상 한 줄**: 누락 = edge 합침·W1C 레이스·clear 순서 · 폭주 = level 소스 미해제·극성 반대·floating 핀 · 두 번 = bounce/느린 엣지 + 히스테리시스 없음 · wake 실패 = wake 미무장·always-on 도메인 아닌 핀·sleep pinctrl.
- **30초 영어 답 (헤드라인 질문 "The console prints garbage — debug it")**: "Consistent garbage is almost always a baud mismatch, so before touching software I measure. I send a stream of 0x55, which makes the line a perfect square wave at half the baud rate, and read the frequency on the scope at the SoC pin. If the ratio to the expected value is a clean clock ratio like 26 over 24, the UART's input clock isn't what the driver assumes — a PLL, mux or device-tree clock problem. If it's off by two to four percent, it's divisor rounding and I check the other side's error too, because the budget is shared. If the bit time is right, I move to frame format, voltage level and polarity. Then I add a baud-accuracy check to the bring-up regression."

## 1. 🔰 1분 설명

기초(프레임·16× oversampling·LSR 비트·GPIO loopback·edge IRQ)는 [BV10 §2.3–2.4](2026-11-01_BV10_board_interface_tests.html)와 [N06C C3/C4/C6 §0](2026-11-01_N06C_plans_peripherals_soc_infra.html)에 있다. 여기선 **디버그에 필요한 그림만**.

- **UART 비유**: 메트로놈 없이 두 사람이 "하나·둘·셋…열"을 각자 센다. 시작 신호(start bit)에서 한 번 맞추고 나면 끝까지 **각자의 박자**로 센다. 박자가 조금 다르면 **열 번째(stop bit)에서 제일 많이 어긋난다** — 그래서 baud 오차는 항상 "프레임 끝"에서 터진다.
- **GPIO 인터럽트 비유**: 초인종(edge) vs 화재경보(level). 초인종은 "눌렸다는 사실"을 한 번 기록하고 끝 — 두 번 연달아 누르면 한 번으로 합쳐질 수 있다(누락). 화재경보는 원인이 사라질 때까지 계속 울린다 — 원인을 안 끄면 **영원히 울린다**(폭주).
- **정확한 정의**: UART = 클럭 없는 비동기 직렬. 수신기는 baud의 16배(또는 8배) 샘플 클럭으로 start 엣지를 찾고, 그 시점부터 **자기 클럭만으로** 비트 중앙을 계산해 샘플한다. GPIO IRQ = 핀 → pad(Schmitt 입력·pull·debounce) → GPIO 컨트롤러(edge 검출 래치 또는 level 통과, status W1C, mask) → 인터럽트 컨트롤러(GIC SPI) → CPU → Linux `irq_desc` → handler(hardirq/threaded).

```text
 UART 경로 (콘솔)
  SoC UART IP                pad / pinmux          보드                       호스트
 ┌───────────────┐ TX  ┌──────────────┐  ┌─────────────────┐  ┌──────────────────┐
 │ baud gen      ├────►│ func=UART?   ├─►│ 직렬 R, 레벨 시프터├─►│ USB-UART 어댑터    │─► 터미널
 │ ref clk ◄─PLL │ RX  │ 1.8V/3.3V 도메인│◄─┤ (1.8↔3.3V)      │◄─┤ (3.3V TTL? RS-232?)│  (baud 설정)
 │ FIFO, LSR     │◄────┤ pull, drive  │  └─────────────────┘  └──────────────────┘
 └───────┬───────┘     └──────────────┘    GND 공통 필수
         └─ IRQ (RX trigger / RX timeout / THRE / line status) ─► GIC

 GPIO 인터럽트 경로
  센서 INT (open-drain)      pad                    GPIO 컨트롤러                    GIC / CPU / Linux
   ──┬──── R_pullup ─ VDD    ┌──────────────┐  ┌──────────────────────────┐   ┌─────────────────────┐
     └──────────────────────►│ Schmitt in   ├─►│ sync ─► debounce ─► edge  ├──►│ SPI (보통 level)      │
                             │ pull up/down │  │ detect latch ─► STATUS   │   │ ack ─► handler ─► EOI │
                             │ IE, 도메인     │  │ (W1C) ─► MASK ─► bank IRQ │   │ /proc/interrupts     │
                             └──────────────┘  └───────────┬──────────────┘   └─────────────────────┘
                                                           └─ wake 요청 ─► PMU (always-on 도메인일 때만)
```

| 용어 | 풀네임 | 쉬운 설명 | 디버그에서 왜 중요 |
|---|---|---|---|
| baud | — | 초당 비트 수 (UART는 심볼=비트) | 1비트 시간 = 1/baud, 115200 → 8.68 µs |
| 8N1 | 8 data, No parity, 1 stop | 프레임 10비트 | 허용 오차 계산의 "10" |
| oversampling (16×/8×) | — | 비트 하나를 16(8)틱으로 나눠 봄 | start 검출 불확실성 = 1틱 → 허용 오차를 깎음 |
| divisor | baud divisor | `f_clk / (osr × baud)`를 정수(또는 분수)로 | 반올림 오차가 baud 오차의 주원인 |
| LSR | Line Status Register (16550) | 수신 에러·FIFO 상태 비트 | DR/OE/PE/FE/BI/THRE/TEMT/RX FIFO error [1] |
| FE | framing error | stop 자리에서 0을 읽음 | baud 불일치·break·노이즈 |
| OE | overrun error | FIFO 꽉 찬 상태에서 새 문자 → 버림 | 타이밍이 아니라 **처리 지연** 문제 |
| RX timeout IRQ | character timeout | trigger 미만 데이터가 4문자 시간 동안 머무름 | 짧은 메시지 latency [1] |
| RTS/CTS | request/clear to send | 수신 측이 "그만" 신호 | 고속 overrun 방지, 데드락 원인도 됨 |
| break | — | 한 프레임 이상 계속 0 | BI + FE 같이 뜸, TX가 low에 붙은 것과 구분 |
| edge / level IRQ | — | 변화 1회 기록 / 상태 유지 동안 계속 | 누락 vs 폭주의 갈림길 |
| W1C | write-1-to-clear | status 비트에 1을 써야 지워짐 | RMW로 지우면 다른 핀 비트까지 지움 → 누락 |
| debounce | — | 짧은 튐 무시 필터 | 두 번 들어옴 해결, 대신 지연·wake 문제 |
| Schmitt trigger | — | 입력 히스테리시스 | 느린 엣지 + 노이즈에서 다중 엣지 방지 |
| wake IRQ | — | suspend에서 깨우는 인터럽트 | `enable_irq_wake`, always-on 도메인 |

## 2. 정상 기준선

### 2.1 8N1 프레임과 수신기 샘플링 (정상)

```text
 'U' = 0x55 = 0b01010101, LSB first   (115200 baud, 1 bit = 8.68 us, 1 frame = 86.8 us)

 idle  start d0 d1 d2 d3 d4 d5 d6 d7 stop  start(next 'U') ...
  1  ┐  0  ┌1 ┐0 ┌1 ┐0 ┌1 ┐0 ┌1 ┐0 ┌ 1  ┐ 0 ┌1 ...
     └────┘  └──┘  └──┘  └──┘  └────────┘    <- 연속 0x55 = 0/1 완전 교대 = baud/2 사각파 (57.6 kHz)
     ^edge
     |<-delta->|  delta = start 엣지 검출 지연 (0 ~ 1틱 = 1/16 비트)
          ^ 8틱 뒤 start 중앙 확인 (false start 거르기)
               ^ 이후 16틱마다 샘플: d0 중앙, d1 중앙, ... stop 중앙(9.5비트 + delta)

 16x 샘플 틱 (start 비트 하나):
  edge→ |.|.|.|.|.|.|.|S|.|.|.|.|.|.|.|.|   S = 중앙 샘플 (3-sample majority면 S-1, S, S+1)
```

- **정상 판정 기준**: 비트 폭 = 1/baud ±(분주 오차), 프레임 사이 idle은 1, stop 자리 1, `fe/pe/oe/brk` 카운터 0.
- 16550의 수신기는 FE가 나면 "그 0이 다음 start bit였다"고 가정하고 재동기한다 — 그래서 baud 오차가 크면 **한 번 틀린 뒤 프레임 경계까지 잃고 연쇄로 깨진다** [1] (§8 S4에서 재현: 15프레임 보냈는데 14프레임 수신).

### 2.2 핵심 타이밍 숫자 (계산값)

| baud | 1비트 | 1문자(10비트) | 0x55 사각파 | 16 B FIFO, trigger 8 → 남은 여유 8문자 | 64 B FIFO, trigger 48 → 여유 16문자 |
|---|---|---|---|---|---|
| 9600 | 104.2 µs | 1.042 ms | 4.8 kHz | 8.3 ms | 16.7 ms |
| 115200 | 8.68 µs | 86.8 µs | 57.6 kHz | 694 µs | 1.39 ms |
| 921600 | 1.085 µs | 10.85 µs | 460.8 kHz | 86.8 µs | 174 µs |
| 3,000,000 | 0.333 µs | 3.33 µs | 1.5 MHz | **26.7 µs** | 53.3 µs |

- "여유" = RX trigger IRQ가 뜬 뒤 FIFO가 넘치기 전까지 ISR/DMA가 반응해야 하는 시간. 3 Mbaud + 16 B FIFO면 **26.7 µs 안에 반응**해야 한다 → 부하 걸린 Linux에서 IRQ 지연이 이걸 넘을 수 있다 [추정: 플랫폼마다 다름, cyclictest로 측정] → DMA 또는 auto RTS/CTS가 필요한 이유.
- 16550 RX timeout = 마지막 문자 수신·마지막 CPU 읽기 후 **4문자 시간** (115200이면 약 347 µs) [1].
- Logic analyzer로 3 Mbaud 디코드: 샘플링은 신호 주파수의 4–10배 → 비트레이트 기준 **최소 12–30 MS/s** 이상 ([N07A](2026-11-01_N07A_debug_framework_instruments.html) 샘플링 규칙).

### 2.3 허용 오차 유도 [계산 — 코드로 검증]

**가정**: 8N1(10비트), 깨끗한 엣지(상승시간·임계 skew·노이즈 0), 수신기는 매 틱 라인을 보고 처음 0을 본 틱을 start로 잡음(검출 지연 δ ∈ [0, 1/osr) 비트), 거기서 osr/2틱 뒤 start 중앙, 이후 osr틱마다 샘플. 비율 r = baud_tx / baud_rx (r > 1: TX가 빠름 = 수신기 입장에서 "내 박자가 느림").

- k번째 비트(start=0 … stop=9)의 샘플 시각을 **TX 비트 단위**로 쓰면 `(k + 0.5 + δ) × r`. 맞게 읽으려면 `k < (k + 0.5 + δ) × r < k + 1`.
- r > 1이면 뒤로 밀리고, k가 클수록 더 밀린다 → **stop(k=9)이 제일 먼저** 깨진다. 최악 δ → 1/16: `r < 10 / (9.5 + 1/16) = 1.04575` → **TX +4.57%**.
- r < 1이면 앞으로 당겨진다. 최악 δ = 0: `r > 9 / 9.5 = 0.94737` → **TX −5.26%**. 비대칭인 이유: 검출 지연 δ는 항상 "늦게" 쪽이라 TX 빠름 쪽 여유를 깎는다.
- 8×면 δ 최대 1/8: TX 빠름 쪽 **+3.90%**로 줄어든다(느림 쪽은 그대로 −5.26%). 즉 8×는 분주 해상도는 좋아지지만 **허용 창은 좁아진다**.
- 3-sample majority(S−1, S, S+1): 깨끗한 엣지에선 **중앙 샘플과 같은 창**(2/3이 틀리려면 중앙이 이미 틀려야 함). 대신 "세 샘플이 다르면 noise flag"를 쓰는 UART라면 flag가 1틱 먼저 뜬다: +3.90% / −4.64%. majority는 **노이즈 내성**이지 **baud 내성**이 아니다.
- **"양쪽 합"의 의미**: r은 두 클럭의 비율이므로 TX 오차와 RX 오차가 **반대 방향일 때 더해진다**. TX −3.12%(25 MHz/16 분주) + 상대 RX +2.5%(RC 발진기 drift) → r = 0.9688/1.025 = 0.945 → −5.26% 창 밖 → FE (§8 S3).
- **실무 여유**: 실제 엣지는 레벨 시프터·케이블·RC 때문에 상승/하강 시간이 있고 임계 전압이 가운데가 아니면 비트 폭이 비대칭(왜곡)이 된다. 3 Mbaud에서 100 ns 엣지는 비트(333 ns)의 30% [계산]. 그래서 이론 ±4.6–5.3% 대신 **각 쪽 ±2%**(비율 최대 약 4%)를 쓴다. N06C의 근사식 (0.5 − 1/16)/9.5 ≈ 4.6%와 같은 결과(비율로 쓰면 4.57%).
- 참고: Maxim/ADI 튜토리얼 AN2141은 동기 오차와 샘플 위치 오차를 더 보수적으로 잡아 더 좁은 예산을 제시한다 [9] [미검증 — 원문 PDF 열람 실패, 검색 요약만 확인].

### 2.4 정상 Linux 출력

```text
[예시 출력]  # cat /proc/tty/driver/serial        (8250 드라이버; 다른 드라이버는 /proc/tty/driver/<driver명>)
serinfo:1.0 driver revision:
0: uart:16550A mmio:0xFE215040 irq:29 tx:18342 rx:1204 RTS|CTS|DTR
1: uart:unknown port:00000000 irq:0
```

- 형식은 `drivers/tty/serial/serial_core.c`의 `uart_line_info()` 기준: `tx:`/`rx:` 다음에 **0이 아닐 때만** `fe:` `pe:` `brk:` `oe:` `bo:`가 붙는다. 그리고 카운터·모뎀 비트는 **CAP_SYS_ADMIN(root)일 때만** 출력된다 [2]. 일반 사용자로 보면 "에러가 없어 보이는" 함정.
- `oe` = HW overrun(FIFO), `bo` = buf_overrun(tty flip buffer가 꽉 참 — 소비자가 느림) [2].

```text
[예시 출력]  # stty -F /dev/ttyS0 -a | head -2
speed 115200 baud; rows 0; columns 0; line = 0;
...  -parenb -parodd cs8 hupcl -cstopb cread clocal crtscts ...

[예시 출력]  # dmesg | grep -i -E 'console|tty'
[    0.000000] earlycon: uart0 at MMIO32 0x00000000fe215040 (options '115200n8')
[    0.000000] printk: bootconsole [uart0] enabled
[    1.104512] fe215040.serial: ttyS0 at MMIO 0xfe215040 (irq = 29, base_baud = 31250000) is a 16550A
[    1.104890] printk: console [ttyS0] enabled
[    1.105011] printk: bootconsole [uart0] disabled
```

- 콘솔 선택은 `console=ttyS0,115200n8` 또는 DT `chosen { stdout-path = "serial0:115200n8"; }`. `console=`가 여러 개면 마지막 것이 `/dev/console` [3]. `earlycon`을 옵션 없이 주면 DT `stdout-path`로 장치를 정한다 [10].

```text
[예시 출력]  # gpioinfo -c gpiochip0 | head -4          (libgpiod v2)
gpiochip0 - 54 lines:
	line   0:	"ID_SDA"        	input
	line  17:	"SENSOR_INT_N"  	input active-low consumer="imu-drdy"
	line  27:	"CAM_PWR_EN"    	output consumer="regulator"

[예시 출력]  # gpiomon -c gpiochip0 -e falling -n 3 22      (pulse generator 1 kHz)
1234.567890123	falling	"BTN_TEST"
1234.568890140	falling	"BTN_TEST"
1234.569890133	falling	"BTN_TEST"         <- 간격 1.000 ms 일정 = 정상

[예시 출력]  # grep -E 'CPU|gpio' /proc/interrupts
           CPU0       CPU1       CPU2       CPU3
 57:       1000          0          0          0  pinctrl-gpio  22 Edge      btn-test
 58:      48210          0          0          0  pinctrl-gpio  17 Level     imu-drdy

[예시 출력]  # cat /sys/kernel/irq/57/{type,actions,hwirq,wakeup,per_cpu_count}
edge
btn-test
22
disabled
1000,0,0,0
```

- `/sys/kernel/irq/<n>/`의 `type`(edge/level), `actions`(핸들러 이름들), `hwirq`, `per_cpu_count`(꺼진 CPU 포함), `wakeup`(enabled/disabled)은 커널 ABI 문서 기준 [5].
- 정상 기준선: **펄스 N개 = gpiomon 이벤트 N개 = `/proc/interrupts` 증가 N = 장치 이벤트 카운터 N**. 이 네 숫자가 하나라도 다르면 어디서 잃었는지가 바로 구간이 된다.

## 3. 관찰 지점 지도

| 무엇 | 어디서 | 도구 | 정상값 | 이상하면 의미 |
|---|---|---|---|---|
| 실제 baud | TX 핀 (SoC 볼 근처) | scope 주파수(0x55 연속) 또는 start bit 폭 | baud/2 사각파, 오차 ≤ 계산 분주 오차 | 비율 오차 → 클럭원, 2–4% → 분주, 0 → 다른 층 |
| 전압·극성 | TX/RX, 어댑터 쪽 | scope 아날로그 | idle high, 도메인 전압(1.8/3.3 V) | idle low → RS-232 레벨/반전, 진폭 부족 → VIH 미달 |
| 엣지 품질 | 수신단 핀 | scope (대역폭 ≥ 0.35/tr) | tr ≪ 비트 폭 (10% 미만) | 느린 엣지 → 고속에서만 FE |
| 라인 상태 | UART LSR 류 (16550: OE/PE/FE/BI, RX FIFO error) | JTAG/devmem, 드라이버 카운터 | 0 | FE=타이밍, OE=처리 지연, BI=라인 low 지속 |
| 드라이버 카운터 | `/proc/tty/driver/*` (root), `TIOCGICOUNT` | cat, 작은 C 프로그램 | fe/pe/oe/bo 없음 | 위 표 + bo=유저 공간 |
| 흐름 제어 | RTS/CTS 핀, `stty -a`의 crtscts | scope, stty | CTS asserted (보낼 수 있음) | CTS 영구 deassert → TX 멈춤(데드락) |
| 콘솔 경로 | `/proc/cmdline`, DT chosen, dmesg | cat, dtc | console=와 stdout-path 일치 | bootconsole disabled 후 침묵 → handover 문제 |
| UART 클럭 | `/sys/kernel/debug/clk/clk_summary` | cat | enable_count ≥ 1, rate = 가정값 | 0 → gate됨(clk_disable_unused), rate 다름 → divisor 오류 |
| pinmux | `/sys/kernel/debug/pinctrl/*/pinmux-pins` | cat | 콘솔 핀 = UART 기능, 올바른 owner | 다른 기능/다른 드라이버가 점유 |
| GPIO 라인 상태 | `gpioinfo`, `gpioget` | libgpiod v2 | 방향·active-low·consumer가 DT와 일치 | consumer 없음 → 드라이버 미바인드 |
| 엣지 이벤트 | `gpiomon -e/-p/-n`, 타임스탬프 | libgpiod v2 [4] | 이벤트 수 = 펄스 수, 간격 일정 | 두 배 → bounce, 적음 → 합침/누락 |
| IRQ 카운트 | `/proc/interrupts`, `/sys/kernel/irq/<n>/per_cpu_count` | watch, diff | 이벤트당 1 증가 | 폭증 → storm, 정지 → mask/disabled |
| IRQ 설정 | `/sys/kernel/irq/<n>/type`, `actions`, `wakeup` | cat | DT 의도와 일치 | type 불일치 → 누락/폭주, wakeup disabled → wake 실패 |
| 핸들러 결과 | ftrace `irq:irq_handler_entry/exit` (ret=handled/unhandled) | trace-cmd, tracefs | handled | unhandled 반복 → "nobody cared" 직전 |
| 컨트롤러 상태 | GPIO raw status vs masked status, GIC pending/active | JTAG/devmem (개념명) | 이벤트 후 status set → 처리 후 clear | status 남음 → level 재진입, raw만 set → mask됨 |
| wake 경로 | `/sys/power/pm_wakeup_irq`, `/sys/kernel/debug/wakeup_sources`, `/sys/power/mem_sleep` | cat | 마지막 wake IRQ = 기대 핀 | 다른 IRQ/없음 → wake 미무장·도메인 문제 |

### 첫 5분 증거 스냅샷

**UART** (깨짐·누락·침묵):

1. 터미널 설정 스크린샷(baud·8N1·흐름 제어) — 사람 실수 배제.
2. scope: SoC TX 핀에서 single-shot falling-edge 트리거 → 토글 여부 · 비트 폭(또는 0x55 연속이면 주파수) · idle 레벨 · 진폭. **SoC 볼 근처와 커넥터 두 곳**.
3. `cat /proc/cmdline` · `dmesg | grep -iE 'console|tty|serial'` (콘솔 handover 확인).
4. `sudo cat /proc/tty/driver/serial` (fe/pe/oe/bo/brk — root로!).
5. `stty -F /dev/ttySx -a` · `sudo cat /sys/kernel/debug/clk/clk_summary | grep -i uart` · `sudo cat /sys/kernel/debug/pinctrl/*/pinmux-pins | grep -i uart`.

**GPIO/IRQ** (누락·폭주·두 번·wake):

1. `grep <name> /proc/interrupts` 두 번 (1초 간격) → 증가율.
2. `cat /sys/kernel/irq/<n>/{type,actions,wakeup}` · DT의 `interrupts = <… IRQ_TYPE_…>`와 대조.
3. `gpioinfo -c <chip>` (consumer, active-low, bias) · scope로 INT 핀 아날로그(레벨·엣지·노이즈).
4. 펄스 발생기로 N개 → `gpiomon -n N` 이벤트 수 · `/proc/interrupts` 증가 · 장치 카운터 대조.
5. wake 문제면 `cat /sys/power/mem_sleep` · `cat /sys/power/pm_wakeup_irq` · `cat /sys/kernel/debug/wakeup_sources`.

## 4. 증상 → 원인 한 장 표

### 4.1 UART

| 증상 | 계층 | 의심 원인 (순위) | 첫 확인 | 확정 방법 |
|---|---|---|---|---|
| 깨진 글자, 매 부팅 같은 모양 | 타이밍 | ① UART 입력 클럭 ≠ 드라이버 가정(PLL·mux·DT) ② 분주 반올림 ③ 터미널 baud | 0x55 연속 → scope 주파수 | 측정/기대 비율 = 클럭 비율이면 클럭원 확정, 클럭 레지스터 readback |
| 깨진 글자, 매번 다른 모양 | 전기 | ① GND 미연결 ② 레벨 불일치(1.8 V → 3.3 V 어댑터 VIH) ③ 노이즈·크로스토크 | 어댑터 쪽 진폭·idle 레벨 | GND 연결/레벨 시프터 교체 후 소멸 |
| 글자마다 bit7만 틀림 / 특정 글자만 | 프로토콜 | ① 7E1 vs 8N1 형식 불일치 ② parity 설정 | LA 디코드로 프레임 비트 수 | 형식 맞추면 소멸 |
| 출력 전혀 없음 | 전기·설정 | ① pinmux ② UART 클럭 gate/리셋 ③ TX/RX 교차 ④ 레벨 시프터 enable | SoC 볼에서 토글 여부 | JTAG로 데이터 레지스터 직접 쓰기 ([BV13 S3](2026-11-01_BV13_board_debug_scenarios.html)) |
| idle이 low, 모든 문자 FE/BI | 전기 | ① RS-232 레벨/반전 신호를 TTL에 연결 ② TX 핀 low 고착(다른 기능) | scope idle 레벨·진폭(±V?) | 어댑터 종류 확인, 핀 기능 readback |
| 첫 글자만 깨짐 | 타이밍·설정 | ① TX 핀이 pinmux 전환 순간 글리치 → 수신기가 가짜 start ② 상대가 늦게 켜짐 ③ 파워온 직후 클럭 미안정 | 첫 프레임 직전 파형 | pinmux를 UART 초기화 뒤가 아니라 전에 / idle 1 확보 후 송신 |
| 고속 baud에서만 FE/PE | 타이밍·전기 | ① 분주 양자화 오차(예: 100 MHz/16 → 3M에서 +4.17%) ② 엣지 느림(레벨 시프터) ③ 8× 모드 | `fe` 증가 + 분주 계산 | 분수 분주/클럭 변경 후 소멸, 엣지 측정 |
| 고속 baud에서만 글자 빠짐 (oe 증가) | 시스템 | ① FIFO overrun = IRQ 지연 > 여유 시간 ② DMA 미사용 ③ RTS/CTS 미사용 | `oe` vs `bo` 카운터 | IRQ 지연 측정(GPIO 토글), DMA/흐름 제어로 소멸 |
| 글자 빠짐 (bo 증가) | 시스템·드라이버 | ① 유저 공간 소비자 느림 ② tty 버퍼 작음 | `bo` 카운터, 소비자 CPU | 소비자 우선순위/읽기 크기 변경 후 소멸 |
| 고온(저온)에서만 FE | 타이밍 | ① RC 발진기 drift + 기존 분주 오차 ② 상대 MCU 클럭 drift ③ 엣지 열화 | chamber에서 비트 폭 vs 온도 | 오차 그래프가 창 경계를 넘는 온도 = FE 시작 온도 |
| 짧은 메시지 응답이 늦음 | 설정·드라이버 | ① RX timeout IRQ 미활성/미구현 ② trigger level 너무 높음 | IRQ 원인별 카운트 | trigger 1로 낮추면 latency 소멸 |
| 송신이 멈춤 (데드락) | 프로토콜·설정 | ① CTS 영구 deassert(상대가 RTS 안 올림, 배선 교차) ② crtscts를 한쪽만 설정 | CTS 핀 레벨, `stty -a` | 흐름 제어 끄면 송신 재개 |
| 부팅 로그 OK, Linux 진입 후 침묵/깨짐 | 설정·드라이버 | ① console=/stdout-path 불일치 ② 드라이버 클럭 가정 오류 ③ clk_disable_unused ④ pinctrl 재설정 ⑤ runtime PM | 침묵 순간의 TX 파형 | `keep_bootcon`, `clk_ignore_unused`로 소멸 여부 |

### 4.2 GPIO · 인터럽트

| 증상 | 계층 | 의심 원인 (순위) | 첫 확인 | 확정 방법 |
|---|---|---|---|---|
| 인터럽트 가끔 누락 | 설정·드라이버 | ① edge 합침(처리 중 재발생) ② W1C를 RMW로 지움 ③ clear 순서(EOI 후 장치 clear) ④ level 장치를 edge로 설정 | 펄스 N vs IRQ 카운트 | 펄스율 스윕 → 누락 시작점 = ISR 처리율? ([deck §7 #4](2026-10-25_rivian_hm_screen_prep.html)) |
| IRQ 폭주, "nobody cared" | 설정·드라이버·전기 | ① level 소스 미해제 ② 극성 반대(level-low를 high로) ③ 공유 IRQ에서 IRQ_NONE ④ floating 입력 ⑤ threaded IRQ인데 ONESHOT 없음 | `/proc/interrupts` 증가율, INT 핀 레벨 | 장치 status clear 후 정지 / 극성 바꾸면 정지 |
| 한 번에 두 번 들어옴 | 전기·설정 | ① 기계식 bounce(ms) ② 느린 엣지 + 노이즈, Schmitt 없음(µs) ③ 반사·링잉(ns) ④ both-edge 설정 | gpiomon 타임스탬프 간격 | 간격의 시간 규모로 원인 분류 → scope로 확인 |
| 이벤트 수가 펄스보다 적음 (고주파에서만) | 시스템 | ① ISR 처리율 초과 → edge 합침(정상 동작) ② debounce 폭 > 펄스 간격 | 펄스율 vs 손실률 | 처리율 이하에서 0 손실이면 사양, 문서화 |
| pinmux 충돌 (핀이 GPIO로 안 움직임) | 설정 | ① 같은 핀을 다른 기능/드라이버가 점유 ② DT pinctrl group 오류 | `pinmux-pins`, gpioinfo consumer | 충돌 드라이버 unbind 후 정상 |
| 출력 레벨 이상 (중간 전압, 느린 상승) | 전기 | ① open-drain 모드 + pull-up 없음 ② drive strength 약함 ③ 전압 도메인 불일치 ④ 외부 부하 | scope 레벨·상승시간 | 모드/drive 변경 후 정상 |
| 입력이 항상 0 또는 1 | 전기·설정 | ① input enable 꺼짐 ② pull 방향 반대 ③ active-low 표기 혼동 | `gpioget -a`(as-is), raw 레벨 scope | 핀 레벨 vs 읽은 값 대조 |
| suspend 중 wake 실패 | 설정·시스템 | ① wake 미무장(enable_irq_wake) ② always-on 도메인 아닌 핀 ③ sleep pinctrl state ④ suspend 중 debounce 클럭 gate ⑤ suspend 진입 중 edge 손실 | `wakeup` 속성, `pm_wakeup_irq` | wake-capable 핀으로 옮기거나 level wake로 바꾸면 해결 |
| resume 후 인터럽트 영원히 안 옴 | 드라이버 | ① resume에서 mask/type 복원 누락 ② suspend 중 발생한 level이 이미 처리돼 edge 재무장 안 됨 | `/sys/kernel/irq/<n>/` + 컨트롤러 레지스터 | suspend 전후 레지스터 덤프 diff |

## 5. 시나리오 드릴

### D1 · 콘솔에 깨진 글자 — 비트 폭부터 잰다

#### 증상

```text
[예시 출력]  minicom 115200 8N1
U-Boot SPL 2024.07 (Oct 28 2026)      <- 이건 다른 보드
¨²6^Ë.BÕì©°·#á                         <- 문제 보드: 매 부팅 똑같은 깨진 패턴
```

같은 바이너리를 golden 보드에 올리면 정상. 깨진 패턴이 **매번 같다** = 랜덤 노이즈가 아니라 **결정적인 타이밍 차이**.

#### 먼저 물어볼 것

- "Is the garbage identical every boot, or does it change?" (결정적 vs 랜덤 → 타이밍 vs 전기)
- "Does it start garbled from the first byte, or after a specific boot stage like a clock switch?"
- "Which clock feeds this UART, and what frequency does the driver or ROM assume?"
- "Has this exact board and adapter ever worked — is there a golden setup to compare?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| UART 입력 클럭 ≠ 가정 (osc 26 MHz인데 24로 계산, PLL bypass, mux 기본값) | 결정적 깨짐의 1순위 | 0x55 연속 → 주파수 측정 | 비율이 26/24 = 1.083, 19.2/24 = 0.8 같은 클럭 비율 |
| 분주 반올림 오차 | 25 MHz/115200처럼 나누어 떨어지지 않음 | divisor 계산(§8) | 비율 0.97–0.98 같은 작은 오차, 상대에 따라 될 때도 |
| 프레임 형식 불일치 (7E1 vs 8N1, 2 stop) | 일부 글자만 맞음 | LA로 프레임 길이 디코드 | bit7 패턴만 틀림 |
| 극성/레벨 (RS-232 반전, 1.8 V TX → 3.3 V 어댑터) | 어댑터 교체 이력 | scope idle 레벨·진폭 | idle low 또는 진폭 < VIH |
| 터미널 설정 실수 | 가장 흔한 사람 실수 | 터미널 설정 확인 | 다른 baud로 바꾸면 읽힘 |

#### 측정 · 계측

- SoC TX 핀 근처에 10× 프로브, 짧은 접지 스프링. FW(또는 JTAG로 UART 데이터 레지스터에 직접)로 `0x55`를 수백 개 연속 송신.
- 연속 0x55는 start(0)–1010 1010–stop(1)–start(0)… 가 **완전 교대**라 scope의 frequency 측정 한 번으로 baud가 나온다: f = baud/2.

```text
 기대 (115200):  ┐_┌‾┐_┌‾┐_┌‾┐_┌‾   period 17.36 us → 57.6 kHz, 1 bit 8.68 us
 측정:           ┐_┌‾┐_┌‾┐_┌‾┐_┌‾   period 16.00 us → 62.5 kHz → baud 125000 → +8.5%
                                 → 26 MHz / (16 × 13) = 125000  ← 드라이버는 24 MHz 가정으로 divisor 13을 씀
```

| 측정/기대 비율 | 가장 그럴듯한 원인 |
|---|---|
| 1.000 ± 0.5% | baud는 정상 → 형식·레벨·극성으로 |
| 0.97–0.98 또는 1.02–1.04 | 분주 반올림 (divisor 표로 확인) |
| 1.083 (26/24), 0.8 (19.2/24), 1.042 (25/24) | 기준 발진기 주파수 가정 오류 |
| 2.0, 0.5 | oversampling 모드(8×/16×) 혼동, 클럭 분주기 ×2 |
| 아주 큰 비율 (예: 40×) | PLL 미설정 → 발진기 직결(bypass) 또는 반대 |

#### root cause · fix

- 대표 원인: SoC 기준 발진기가 26 MHz인데 DT `clock-frequency`/클럭 드라이버가 24 MHz로 계산 → +8.5% → stop보다 훨씬 앞의 **d4–d5에서 이미 비트가 밀림**(§8 S4: 첫 슬립 d4, 첫 "보이는" 오류 d5). 그 뒤 FE → 재동기 실패로 프레임까지 잃는다.
- fix: 클럭 트리 설정(또는 DT) 수정 → 다시 0x55 주파수 측정으로 확인.

#### 회귀로 굳히기

- bring-up 체크리스트 P0: "콘솔 UART 0x55 버스트 → baud 오차 ≤ 1%" (LA 자동 측정 또는 외부 루프백 → SoC 타이머 capture로 엣지 간격 측정).
- 클럭 readback 테스트: `/sys/kernel/debug/clk/clk_summary`의 UART 클럭 rate = DT/통합 스펙 값.

#### 30초 영어 답

"Identical garbage every boot means a deterministic timing mismatch, not noise. I'd send a continuous stream of 0x55, which turns the line into a square wave at half the baud rate, and measure the frequency at the SoC pin. Here I'd expect 57.6 kilohertz; if I see 62.5, that's 8.5 percent fast, which is exactly 26 over 24 — the UART's reference clock is 26 megahertz but the divisor was computed for 24. That points me at the clock tree or device tree, not the UART IP. After fixing it, I keep the 0x55 bit-rate check as a P0 bring-up test."

#### 꼬리 질문

<details>
<summary>Q1. The bit time is exactly right, but the text is still garbled. Next?</summary>

"Then timing is cleared and I move up and down one layer. Down: idle level and amplitude at the adapter — an inverted RS-232-level signal or a 1.8-volt TX into a 3.3-volt adapter can look like garbage with perfect timing. Up: frame format — 7E1 versus 8N1 shows a very specific pattern where only the top bit is wrong. A logic analyzer decode with the frame settings varied answers that in minutes."

한국어 핵심: 비트 폭 정상이면 아래로(전압·극성) 위로(프레임 형식) 한 층씩.

</details>

<details>
<summary>Q2. Why is 0x55 the right test pattern, and why not 0x00 or 0xFF?</summary>

"0x55 with 8N1 gives perfectly alternating bits, so every bit boundary is an edge — easy frequency measurement and any slipped sample changes the value. With 0xFF or 0x00, neighboring bits are equal, so a sample that lands in the wrong bit still reads the right value and the error hides. My simulator shows an 8.5 percent clock error passing cleanly with 0xFF and idle gaps, and failing every frame with 0x55."

한국어 핵심: 0x55 = 모든 경계가 엣지 → 측정 쉽고 슬립이 값으로 드러남. 0xFF/0x00은 숨긴다(§8 S6).

</details>

<details>
<summary>Q3. Only the host-to-SoC direction is garbled; SoC-to-host is fine. What does that tell you?</summary>

"Same clock means the same divisor both ways, so a pure baud error would usually hit both directions. One-direction failure points at that path: the adapter's TX level versus the SoC's input threshold, a missing pull-up on RX, a level shifter direction problem, or the SoC RX pad not configured with input enable. I'd scope RX at the SoC ball, not at the connector."

한국어 핵심: 한 방향만 → 그 방향 경로(레벨·pad input enable·시프터) — SoC 볼에서 측정.

</details>

### D2 · 115200은 되는데 3 Mbaud에서 실패

#### 증상

```text
[예시 출력]  # sudo cat /proc/tty/driver/serial
1: uart:16550A mmio:0xFE201000 irq:31 tx:0 rx:48213977 fe:1291 oe:37 RTS|CTS|DTR
   -> 3 Mbaud 레이더/GNSS 링크, 1시간에 fe 1291, oe 37
```

같은 링크를 115200으로 내리면 오류 0. 속도 손잡이 하나로 고장이 생긴다 → **속도에 의존하는 것**만 후보: 분주 양자화, 엣지 품질, FIFO/IRQ 여유.

#### 먼저 물어볼 것

- "Which counters move — framing and parity, or overrun, or the tty buffer overrun?" (계층 분리의 핵심)
- "What's the UART reference clock, and does the IP have a fractional divider or an 8x mode?"
- "Is RX serviced by interrupts or DMA, and is hardware flow control wired?"
- "Is there a level shifter or long cable between the two ends?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 분주 양자화: 100 MHz/(16×3M) = 2.08 → 2 → +4.17% | 고속일수록 divisor가 작아 반올림 비율 오차 큼 | divisor 계산(§8 Part 1) | `fe` 증가, 상대가 같은 방향 오차면 0 |
| 8× 모드 + 같은 오차 | 8×는 TX-fast 창이 +3.90%로 좁음 | 모드 레지스터, 계산 | 같은 +4.17%인데 8×만 실패(§8 S7) |
| 엣지 느림 (auto-direction 레벨 시프터, 긴 케이블) | 333 ns 비트에서 100 ns 엣지 = 30% 왜곡 | 수신단 scope tr/tf, 듀티 | 1/0 비트 폭 비대칭, `fe`/`pe` |
| FIFO overrun (IRQ 지연 > 26.7 µs) | 3M + 16 B FIFO, trigger 8 | `oe` 카운터, ISR 진입 GPIO 토글 | `oe`만 증가, 부하에서 악화 |
| tty 버퍼 overrun | 유저 공간 소비자 느림 | `bo` 카운터 | `bo`만 증가 |
| 어댑터/상대 장치의 최대 baud 한계 | 저가 USB-UART | 다른 어댑터로 교체 | 교체 후 소멸 |

#### 측정 · 계측

- 손잡이를 하나씩: ① baud만 1M → 2M → 3M 올리며 `fe`/`oe` 카운트 기록 ② 같은 3M에서 DMA on/off ③ 부하(stress-ng) on/off. **fe가 baud만 따라가면 타이밍, oe가 부하를 따라가면 처리 지연**.
- 수신단 핀에서 scope: 대역폭 ≥ 0.35/tr (100 ns 엣지 측정엔 수십 MHz면 충분, 10 ns면 ≥ 35 MHz), 1-비트 폭과 0-비트 폭을 따로 측정해 왜곡(%) 계산.
- IRQ 지연: RX IRQ ISR 첫 줄에서 GPIO 토글 → scope ch1 = RX 마지막 stop 엣지, ch2 = GPIO → 최대값을 segmented memory/persistence로 수집.

#### root cause · fix

- 분주: 클럭을 3M의 16배수로 나누어지는 값(48 MHz)으로 바꾸거나 분수 분주 사용(§8: 100 MHz + 4-bit fraction → +1.01%). 8× 모드는 분해능을 높이지만 창이 좁아지므로 계산으로 선택.
- 처리 지연: DMA(+RX timeout으로 짧은 꼬리 처리) + auto RTS/CTS. 엣지: push-pull 레벨 시프터, 케이블 단축.

#### 회귀로 굳히기

- baud 스윕 테스트: 지원 baud 전부 × 10^7 B × (fe, pe, oe, bo = 0) + 비트 폭 측정값 = 계산값 ±0.1%.
- 3M 연속 DMA 1시간 + CPU/메모리 부하 하에서 oe = 0. 상대 baud를 ±x%로 일부러 틀어 margin 창 측정 ([N06C C3 Q2](2026-11-01_N06C_plans_peripherals_soc_infra.html)).

#### 30초 영어 답

"A failure that appears only when I turn the speed knob narrows it to speed-dependent causes: divisor quantization, edge quality, and FIFO service time. The counters separate them — framing errors are timing, overruns are latency. At 3 megabaud from 100 megahertz with 16x oversampling, the divisor rounds from 2.08 to 2, which is plus 4.2 percent — legal only if the partner is perfect. Overruns mean the ISR couldn't drain a 16-byte FIFO within about 27 microseconds. So I'd fix the clock or use the fractional divider, move RX to DMA with flow control, and add a baud-sweep test with counter checks."

#### 꼬리 질문

<details>
<summary>Q1. Wouldn't 8x oversampling fix the divisor problem?</summary>

"It gives finer divisor resolution, which helps some cases — 25 megahertz at 115200 goes from minus 3.1 percent at 16x to plus 0.5 at 8x. But the start-edge uncertainty doubles to an eighth of a bit, so the TX-fast tolerance drops from about 4.6 to 3.9 percent. For 3 megabaud from 100 megahertz both modes give plus 4.17 percent, and 8x fails while 16x barely passes. So I'd compute both and pick, not assume."

한국어 핵심: 8×는 분해능↑ 창↓ — 계산해서 고른다(§8 Part 1·S7).

</details>

<details>
<summary>Q2. How do you tell a timing problem from a latency problem without a scope?</summary>

"The line-status counters: framing and parity errors come from sampling the wrong bit, which is timing; overrun means a complete, correct character arrived but there was no room, which is latency. And the knobs behave differently — framing errors follow baud rate and temperature, overruns follow CPU load and interrupt latency, and disappear with DMA or flow control."

한국어 핵심: fe/pe = 타이밍(baud·온도 따라감), oe = 지연(부하 따라감, DMA로 사라짐).

</details>

<details>
<summary>Q3. Why does the RX timeout interrupt matter for a high-baud sensor link?</summary>

"With DMA or a high trigger level, the last few bytes of a burst may never reach the threshold. The character timeout — four character times in a 16550 — is what hands those bytes to software. If it's missing or misconfigured, the symptom is not lost data but delayed messages, which looks like a sensor latency problem."

한국어 핵심: trigger 미만 꼬리 바이트는 RX timeout(4문자 시간)이 처리 → 없으면 지연으로 보임.

</details>

### D3 · 고온에서만 framing error

#### 증상

- 상온 10^9 B 0 에러. chamber 105 °C에서 `fe`가 분당 수십 회, −40 °C에선 0. 링크 상대는 차량 내 보조 MCU(내부 RC 발진기 사용) [예시].
- 에러가 **온도에 단조 증가** → 온도에 따라 변하는 양: 발진기 주파수, 엣지 속도, (부하 시) IRQ 지연.

#### 먼저 물어볼 것

- "Which side's clock is a crystal and which is an internal RC oscillator, and what's its specified drift?"
- "At room temperature, what's the computed divisor error on each side?"
- "Is it framing errors or overruns — is the system also throttling when hot?"
- "Both directions, or only one?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| RC 발진기 drift + 기존 분주 오차가 반대 방향으로 합쳐짐 | RC는 온도에 % 단위로 변함 [추정: 부품마다 다름, 데이터시트 확인] | chamber에서 양쪽 비트 폭 vs 온도 | 비율이 −5.26%/+4.57% 경계를 넘는 온도 = fe 시작 온도 |
| 상대 MCU 분주도 오차가 있음 | 양쪽 오차 합 | 상대 쪽 0x55 주파수 측정 | 상온에서도 이미 경계 근처 |
| 엣지 열화 (드라이버 세기, 시프터) | 고온에서 drive 약해짐 [추정] | 수신단 tr/tf vs 온도 | 왜곡 증가, 높은 baud일수록 심함 |
| 고온 throttling → IRQ 지연 | `oe`로 보이면 이쪽 | 카운터 종류 | `fe`가 아니라 `oe` 증가 |
| 수정 발진기 drift | 의심할 만하지만 | 계산 | ±50 ppm = 0.005% → 무시 가능 [계산] |

#### 측정 · 계측

- chamber에 보드를 넣고 양쪽 TX에서 0x55 버스트 주파수를 −40/25/85/105 °C에서 측정 → **오차(%) vs 온도 그래프 두 줄**, 그 차이(비율)를 §2.3 창과 겹쳐 그린다.
- 같은 온도에서 상대 baud를 일부러 ±0.5% 스텝으로 틀어 남은 margin 측정(창의 어느 쪽 가장자리가 닫히는지).

```text
 오차(%)                                     창 경계 (r = baud_tx/baud_rx)
  +2 |                        SoC RX 클럭 (crystal) ────────────── 거의 0
   0 |──────────────────────────────────────────────
  -2 |
  -3 |  MCU TX (div 오차 -3.1%) ──────────────────── 상온
  -5 |                               ╲ MCU TX + RC drift @105C
  -5.26 ┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄╲┄┄┄  ← 여기 넘는 온도부터 FE
       -40      25        85       105 °C
```

#### root cause · fix

- 대표: 상대 MCU가 25 MHz RC 클럭에서 115200을 16× 분주 → 상온 −3.1%, 고온 RC drift로 추가 −2% 이상 → 비율이 −5.26% 창 밖. §8 S3이 같은 상황(TX −3.12%, RX +2.5% → FE 7/16 프레임, **데이터는 맞고 stop만 틀림**)을 재현.
- fix: 상대 쪽 8× 분주 선택(25 MHz/115200 → +0.47%) 또는 crystal 클럭, 공장 trim, 필요하면 baud 자동 보정(상대 0x55 측정 후 divisor 조정).

#### 회귀로 굳히기

- PVT 행 추가: 각 baud의 비트 폭 오차를 3온도에서 측정, **각 쪽 ±2% 이내**를 합격 기준으로 ([N06C C3](2026-11-01_N06C_plans_peripherals_soc_infra.html) PVT 행의 실제 적용).
- 링크 상대가 바뀔 때마다 margin 창 테스트(상대 ±x% 스윕)를 hot에서 실행.

#### 30초 영어 답

"Errors that scale with temperature mean something in the timing budget drifts. A crystal at fifty ppm can't matter — that's 0.005 percent — so I look for an RC oscillator and for an existing divisor error. The budget is a ratio between both ends, so minus 3 percent of divisor error on the MCU plus a couple of percent of RC drift in the opposite direction lands outside the roughly 5 percent window. I'd measure both ends' bit rate across the chamber and overlay it on the window. The fix is a better divisor or clock source, and I'd add a three-temperature baud-accuracy row with a 2-percent-per-side limit."

#### 꼬리 질문

<details>
<summary>Q1. Why does the framing error show up while the data bytes are still correct?</summary>

"Because drift accumulates across the frame and the stop bit is the last and furthest sample. Near the edge of the window only the stop sample crosses into the neighboring bit, so the data is right and only the framing check fails. In my simulation at minus 5.5 percent, seven of sixteen frames had FE with zero byte errors — the FE count is an early warning before data corruption."

한국어 핵심: 누적 오차는 stop에서 최대 → 경계 근처에선 데이터 OK, FE만 → 조기 경보.

</details>

<details>
<summary>Q2. The FE rate is intermittent at a fixed temperature. Why not all-or-nothing?</summary>

"Near the limit, whether the stop sample crosses depends on the start-edge detection delay, which varies per frame from zero to one sample tick depending on phase, and on what follows — back-to-back traffic puts a zero right after the stop bit, while an idle gap keeps the line high. My sweep at plus 5 percent gave framing errors on 890 of 1024 back-to-back frames and zero with a one-bit gap. So I test with back-to-back traffic."

한국어 핵심: 경계 근처 FE는 위상(δ)과 트래픽(연속 vs gap)에 따라 확률적 → 연속 송신으로 시험(§8 S5).

</details>

### D4 · 부팅 로그는 나오다가 Linux 진입 후 콘솔이 사라짐 / 깨짐

#### 증상

```text
[예시 출력]
U-Boot 2024.07 ... Starting kernel ...
[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x410fd083]
[    0.000000] earlycon: uart0 at MMIO32 0x00000000fe215040 (options '115200n8')
 ... (커널 로그 정상) ...
[    1.104890] printk: console [ttyS0] enabled
[    1.105011] printk: bootconsole [uart0] disabled
¶ÿ¿ßÿÿ     <- 여기부터 깨짐   (변형: 아무것도 안 나옴 / 3초쯤 뒤 끊김 / 로그인 프롬프트만 깨짐)
```

- 시스템은 살아 있음(네트워크 ssh 가능) → **하드웨어 경로는 정상, Linux 쪽 설정 handover 문제**.

#### 먼저 물어볼 것

- "Does it die exactly at the bootconsole-to-real-console handover, or a few seconds later?"
- "Silence, or garbage? If garbage, is the bit rate different from the bootloader's?"
- "Is the board otherwise alive — can I reach it over the network or JTAG to read the kernel log?"
- "What changed — device tree, kernel version, clock driver?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 실제 드라이버가 다른 클럭 rate로 divisor 재계산 (DT `clock-frequency`/clocks 오류) | earlycon은 부트로더 divisor를 그대로 씀, handover에서 바뀜 | 깨진 순간 0x55/비트 폭 측정 | handover 후 비트 폭이 다름 (깨짐) |
| `clk_disable_unused`가 UART 클럭을 끔 | 드라이버가 클럭을 claim 안 함 → late init에서 gate | `clk_ignore_unused` 부팅 | 몇 초 뒤 침묵, 파라미터로 소멸 [8][10] |
| console=/stdout-path 불일치 (ttyS0 vs ttyAMA0, 인스턴스 번호) | 커널 로그가 다른 포트로 감 | `/proc/cmdline`, `/proc/consoles` | handover 직후 침묵, 다른 UART에 출력 |
| pinctrl 재설정 (DT `pinctrl-0` 오류, 다른 드라이버가 핀 점유) | probe 시 pad 기능 변경 | `pinmux-pins` | TX 핀이 토글 안 함/low 고착 |
| runtime PM/전원 도메인 off | 콘솔 사용 중 suspend | `/sys/devices/.../power/runtime_status` | idle 시 침묵 |
| 유저 공간 getty baud/tty 불일치 | 커널 로그는 정상인데 프롬프트만 깨짐 | serial-getty 설정 | 로그인 프롬프트만 깨짐 |

#### 측정 · 계측

- **침묵/깨짐 순간의 TX 핀을 scope로**: ① 토글 없음 → 클럭 gate·pinmux·출력 경로 ② 다른 비트 폭으로 토글 → divisor 재계산 ③ low 고착 → 핀이 다른 기능(또는 break).
- `keep_bootcon`으로 부팅: 부트 콘솔을 계속 유지 → handover 구간 로그를 끝까지 볼 수 있다 [10]. `clk_ignore_unused`로 부팅: 증상이 사라지면 클럭 claim 누락 확정 [10].
- 보드가 살아 있으면: `cat /proc/consoles`, `/sys/kernel/debug/clk/clk_summary`(UART 클럭 rate·enable_count), `pinmux-pins`.

#### root cause · fix

- 흔한 조합: DT의 UART 노드가 잘못된 클럭(또는 고정 `clock-frequency`)을 가리켜 드라이버가 다른 divisor를 씀 → handover 순간부터 깨짐. 또는 클럭 드라이버가 UART 클럭을 consumer로 연결하지 않아 `clk_disable_unused`가 꺼버림.
- fix: DT `clocks`를 실제 클럭으로, 드라이버가 `clk_prepare_enable` 하도록. `clk_ignore_unused`는 진단용이지 해결책이 아니다.

#### 회귀로 굳히기

- 보드 팜 부팅 1000회: 콘솔에서 로그인 프롬프트 문자열 일치 + handover 이후 문자 깨짐 0 ([BV13](2026-11-01_BV13_board_debug_scenarios.html)/BV14 farm).
- DT lint: `stdout-path` ↔ `console=` ↔ UART 노드 `clocks` 일치, 부팅 후 clk_summary의 rate = 기대값 assert.

#### 30초 영어 답

"If the bootloader and early console are clean and it breaks at the handover, the hardware path is proven and the problem is what the real driver does differently. The early console reuses the bootloader's divisor; the real driver recomputes it from the device-tree clock, claims pins and manages the clock. So I scope TX at that moment: a different bit time means the wrong clock rate in the device tree; silence a few seconds later suggests the unused-clock cleanup gated it, which booting with clk_ignore_unused confirms. keep_bootcon lets me read the log through that window. Then a boot-loop test in the farm checks the prompt."

#### 꼬리 질문

<details>
<summary>Q1. How do you read the kernel log if the console is dead and there's no network?</summary>

"Several options: keep_bootcon to keep the early console alive, pstore or ramoops to persist the log across a reset, or halt over JTAG and read the printk buffer from memory. On bring-up boards I also like a GPIO or LED toggle at key points as a heartbeat independent of the UART."

한국어 핵심: keep_bootcon, pstore/ramoops, JTAG로 log buffer 읽기, GPIO heartbeat.

</details>

<details>
<summary>Q2. Why would clk_ignore_unused "fix" it, and why is that not the fix?</summary>

"At late init the clock framework gates clocks that are on in hardware but that no Linux driver has enabled. If the UART driver doesn't take a reference on its clock, the console clock gets turned off. clk_ignore_unused skips that cleanup, so the symptom disappears — which proves the diagnosis — but the real fix is wiring the clock in the device tree and having the driver enable it, otherwise power management will bite later."

한국어 핵심: 미사용 클럭 정리가 UART 클럭을 끔 → 파라미터는 진단, 해결은 DT/드라이버 클럭 claim.

</details>

### D5 · 인터럽트가 가끔 누락 (요약 + 링크)

- **증상**: 센서 DRDY(data-ready) 10^6 펄스 중 수백 개가 `/proc/interrupts`에 안 잡힘 → 드라이버가 "sample timeout".
- **3줄 답**: ① edge 설정에서 ISR 처리 중 또 엣지가 오면 하나로 합쳐짐 — 펄스율을 올리며 손실 시작점이 ISR 처리율과 일치하는지 ② status를 RMW로 지우면(W1C) 같은 뱅크의 다른 핀 비트까지 지워 누락 ③ level 장치를 edge로 설정했거나 clear 순서(EOI 먼저, 장치 clear 나중)가 틀려 다음 이벤트를 놓침. 상세: [deck §7 #4](2026-10-25_rivian_hm_screen_prep.html), [N06C C6 버그 2·3](2026-11-01_N06C_plans_peripherals_soc_infra.html), [N06C C4 Q2](2026-11-01_N06C_plans_peripherals_soc_infra.html).
- **여기서 새로 추가**: 손실 위치를 **네 개 카운터의 차이**로 자른다.

```text
 펄스 발생기 N ──► pad ──► GPIO status ──► GIC ──► /proc/interrupts ──► 드라이버 ──► 장치 샘플 카운터
     N             scope       (raw 비트)                 C_irq          C_handler       C_dev
  N == C_irq == C_dev            → 정상
  N >  C_irq, 고주파에서만        → edge 합침 (ISR 처리율 초과) = 사양 문서화 또는 level/FIFO로
  N >  C_irq, 저주파에서도        → W1C RMW 레이스 / mask 구간 / type 불일치
  C_irq == N, C_dev < N          → 드라이버 처리 순서(clear 전 읽기) 또는 threaded 지연
```

- `gpiomon -c gpiochip0 -e rising -n 1000000 22 | wc -l`로 커널 GPIO 경로 카운트, `/sys/kernel/irq/<n>/type`으로 설정 확인 [4][5]. 센서 DRDY처럼 **"데이터가 남아 있는 동안 유지"되는 신호는 level이 기본 선택** (합침 문제 없음, 대신 D6 폭주 주의).

### D6 · IRQ storm — 한 인터럽트가 초당 수십만 번

#### 증상

```text
[예시 출력]
# grep imu /proc/interrupts ; sleep 1 ; grep imu /proc/interrupts
 58:   48210311   0   0   0  pinctrl-gpio  17 Level  imu-drdy
 58:   48612904   0   0   0  pinctrl-gpio  17 Level  imu-drdy      <- 1초에 40만 증가
# dmesg | tail -3
[  812.331201] irq 58: nobody cared (try booting with the "irqpoll" option)
[  812.331540] handlers:
[  812.331552] Disabling IRQ #58
```

- CPU0 hardirq 100%, 시스템 굼뜸. 커널은 최근 100,000번 중 99,900번 이상이 unhandled면 "nobody cared"를 찍고 IRQ를 disable한다 [6] — 그 뒤엔 **폭주가 "누락"으로 바뀌어** 보인다(D5와 헷갈리는 지점).

#### 먼저 물어볼 것

- "Is the line level-triggered, and is it shared with other devices?"
- "Did it start after a driver or device-tree change, or only in a particular device state — sensor unpowered, suspended?"
- "What does the INT pin look like on the scope — stuck asserted, floating, or toggling?"
- "Does the handler report handled or unhandled in the trace?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| level 소스를 장치에서 안 지움 (status read/clear 누락, EOI 후 clear) | level은 원인 해제 전까지 계속 | ISR에서 장치 status 레지스터 로그 | INT 핀이 ISR 후에도 asserted |
| 극성 반대 (active-low 장치를 IRQ_TYPE_LEVEL_HIGH로) | idle 상태가 곧 "asserted" | DT의 `interrupts` 타입 vs 회로도(INT_N) | 장치가 조용해도 폭주, 극성 바꾸면 정지 |
| 핸들러가 IRQ_NONE 반환 (공유 IRQ, 잘못된 장치 확인) | "nobody cared"는 unhandled 누적 | ftrace `irq_handler_exit ret=unhandled` | handled 0, unhandled 폭증 |
| floating 입력 (open-drain INT, pull-up 없음/장치 꺼짐) | 노이즈가 엣지/레벨을 만듦 | scope 핀 레벨, `gpioinfo` bias | 중간 전압·노이즈, pull-up 켜면 정지 |
| threaded IRQ인데 ONESHOT 없이 level | hardirq 후 unmask → thread 실행 전에 재진입 | `request_threaded_irq` 플래그 | 스레드 실행 전 수천 회 재진입 [추정: 커널은 handler=NULL + !ONESHOT 조합을 거부] |

#### 측정 · 계측

- INT 핀 scope (1 ms/div, normal 트리거): **계속 asserted**(장치 미해제/극성) vs **floating 노이즈**(pull 없음) vs **정상 펄스인데 IRQ만 폭주**(clear 순서) 세 가지 모양이 갈린다.

```text
 정상 level:    ‾‾‾‾‾\____ISR읽기____/‾‾‾‾‾‾‾‾‾‾‾      (active-low, 읽으면 deassert)
 미해제:        ‾‾‾‾‾\__________________________     → 계속 asserted → storm
 극성 반대 설정: ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾     → 장치는 idle인데 "high = asserted"로 해석 → storm
 floating:      ~~╱╲~~╱╲╱~~~╲~~╱~~  (중간 전압 + 노이즈)
```

- ftrace: `echo 1 > /sys/kernel/tracing/events/irq/irq_handler_exit/enable` → `ret=unhandled` 비율.
- 장치 쪽: ISR에서 status 읽은 값을 trace_printk로 → 0이면(장치는 "난 아님") 극성/공유/floating.

#### root cause · fix

- 대표: DT에 `IRQ_TYPE_LEVEL_HIGH`, 회로도는 INT_N(active-low) → 장치가 조용할 때 high = "항상 asserted". 또는 ISR이 GIC EOI 후에 장치 status를 지워 매번 1회 이상 재진입.
- fix: 극성 수정, **장치 clear → (필요하면 read-back으로 posted write flush) → EOI/unmask** 순서, pull-up 추가, level + threaded면 ONESHOT.

#### 회귀로 굳히기

- 통합 표 기반 "trigger type 일치" 테스트: 장치 status를 일부러 안 지우고 EOI → level 소스는 재진입, edge 소스는 재진입 없음 ([N06C C6](2026-11-01_N06C_plans_peripherals_soc_infra.html) 배선·type 행).
- 보드 팜 모니터: IRQ별 초당 증가율 임계(예: 기대 최대율의 10배) 초과 시 실패 + dmesg "nobody cared" grep.

#### 30초 영어 답

"An interrupt firing hundreds of thousands of times a second is almost always a level interrupt whose cause isn't being removed. I scope the pin first: if it sits asserted while the device is idle, the polarity in the device tree is probably inverted against an active-low pin; if it's floating, a pull-up is missing; if it pulses normally but the IRQ storms, the driver is clearing the device after EOI. The kernel will eventually say 'nobody cared' and disable the line, which then looks like lost interrupts. I fix the polarity or the clear order, and add a type-check test that leaves the status set and expects re-entry only for level sources."

#### 꼬리 질문

<details>
<summary>Q1. Why does the kernel disable the IRQ, and what does the user see afterwards?</summary>

"The spurious-interrupt detector counts unhandled interrupts; if nearly all of the last hundred thousand were unhandled, it prints 'nobody cared' and disables the line to save the system. After that the device gets no interrupts at all, so the visible symptom flips from a storm to a dead device. That's why I always grep dmesg before treating 'no interrupts' as a hardware problem."

한국어 핵심: 100,000 중 99,900 unhandled → disable → 이후엔 "인터럽트 안 옴"으로 보임 → dmesg 먼저.

</details>

<details>
<summary>Q2. Edge or level for a sensor data-ready line — which would you choose?</summary>

"Level, if the sensor holds it until the data is read: it can't be lost by coalescing, and the risk — a storm if you don't clear — is easy to catch in testing. Edge is fine for true pulses, but then the driver must drain all pending data per interrupt, because two events during one ISR become one."

한국어 핵심: 유지형 DRDY는 level(합침 없음), edge면 ISR마다 전부 비워야.

</details>

### D7 · 버튼/센서 엣지 한 번에 인터럽트가 두 번

#### 증상

```text
[예시 출력]  # gpiomon -c gpiochip0 -e falling 22
5021.118204331	falling	"BTN_TEST"
5021.118215870	falling	"BTN_TEST"        <- 11.5 us 뒤 또 한 번
5022.402771002	falling	"BTN_TEST"
5022.402779964	falling	"BTN_TEST"        <- 9 us
```

- 펄스 발생기로 1000번 넣으면 이벤트 1000–2000 사이로 들쭉날쭉. 드라이버 입장에선 "새 데이터 없음" 에러나 버튼 두 번 눌림.

#### 먼저 물어볼 것

- "What's the time gap between the duplicate events — nanoseconds, microseconds, or milliseconds?" (원인 분류의 핵심)
- "Mechanical contact, or a driven signal? How long is the wire and what's the pull-up?"
- "Is the pad's Schmitt input or a hardware debounce enabled?"
- "Is the IRQ configured for both edges anywhere — DT, driver, or gpiomon itself?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| 기계식 bounce | 버튼·릴레이 | 간격 ms 단위 | 접점 튐 파형(수 ms 동안 여러 번) |
| 느린 엣지 + 노이즈, 히스테리시스 없음 | 약한 pull-up + 긴 선 → RC 수 µs | 간격 µs 단위, scope 엣지 확대 | 임계 전압 근처에서 여러 번 교차 |
| 반사·링잉 | 빠른 엣지 + 긴 trace, 종단 없음 | 간격 ns–수십 ns | 엣지 뒤 링잉이 임계를 다시 넘음 |
| both-edge 설정 / 이벤트 해석 오류 | 설정 실수 | `/sys/kernel/irq/<n>/type`, gpiomon -e | rising+falling이 짝으로 |
| level ack 레이스 | level을 장치 해제 전 ack | 핀이 ack 후에도 asserted | 매번 정확히 1회 추가 |

#### 측정 · 계측

- gpiomon 타임스탬프 간격으로 **scope 시간축을 정한다** — µs면 1 µs/div, 엣지에 트리거해서 **pad에서** 측정(소스 쪽이 아니라).

```text
 깨끗한 엣지 (Schmitt 있음):   ‾‾‾‾‾‾\______________
 느린 엣지 + 노이즈:          ‾‾‾‾╲╱╲╱╲___________     VIL/VIH 사이를 노이즈가 여러 번 가로지름
                                  ^ ^ ^  → 엣지 3번 (히스테리시스가 있으면 1번)
 링잉:                        ‾‾‾‾‾\_/‾\_/‾\________    수십 ns 주기, 첫 언더슈트 뒤 오버슈트가 임계 넘음
```

- 가설 검증 손잡이: `gpiomon -p 1ms`(라인 debounce 요청) [4] — 두 번이 사라지면 bounce/노이즈 확정, debounce 폭을 µs 단위로 줄여 가며 최소 필요 폭 측정. 하드웨어 debounce가 없으면 커널이 소프트웨어 debounce로 대체할 수 있다 [추정: 드라이버/커널 버전에 따라 다름].

#### root cause · fix

- 대표: 센서 보드까지 30 cm 하네스, 10 kΩ pull-up → 상승/하강 수 µs, pad의 Schmitt 입력 비활성 → 노이즈가 임계를 여러 번 교차.
- fix: pad Schmitt/HW debounce 켜기, pull-up 강화(RC 단축), 직렬 종단(링잉), 버튼은 debounce(ms). 드라이버는 status 래치 후 **이벤트가 있었는지 장치에서 확인**하는 구조로.

#### 회귀로 굳히기

- 펄스 발생기 테스트: 깨끗한 펄스 N개 → 이벤트 N개; 일부러 slew를 늦춘 펄스(RC 필터 지그)와 bounce 버스트 → 여전히 N개; debounce 필터 폭 실측 기록 ([N06C C4](2026-11-01_N06C_plans_peripherals_soc_infra.html) debounce 행).

#### 30초 영어 답

"Duplicate edges are usually analog, so the first number I want is the gap between the duplicates — gpiomon's timestamps give it. Milliseconds means mechanical bounce; microseconds means a slow edge with noise crossing the threshold several times, usually with the Schmitt input off; tens of nanoseconds means ringing. That tells me the scope timebase, and I probe at the pad. The fix follows the class: debounce, hysteresis and a stronger pull-up, or termination. And I add a pulse-generator test with deliberately slow and bouncy edges where the count must still equal the pulses."

#### 꼬리 질문

<details>
<summary>Q1. Why not just add a large debounce everywhere?</summary>

"Debounce trades duplicates for latency and can eat real events: a 1-millisecond filter drops any pulse shorter than that and delays every edge, and on some controllers the debounce logic is clocked by something that's gated in suspend, so wake breaks. I size it from the measured bounce, and for fast signals I fix the edge instead."

한국어 핵심: debounce = 지연·짧은 펄스 손실·suspend wake 문제 → 측정 기반 크기, 빠른 신호는 엣지 자체를 고친다.

</details>

<details>
<summary>Q2. The duplicates appear only with the camera running. What does that suggest?</summary>

"Coupling: the camera's high-speed lanes or its power draw are injecting noise into a slow, high-impedance GPIO line. I'd correlate the duplicate timestamps with camera frame activity, look for ground bounce on the scope, and test with a stronger pull-up or the Schmitt input. It's an EMC-style problem showing up at the GPIO."

한국어 핵심: 다른 부하와 상관 → 크로스토크·ground bounce, 고임피던스 라인이 취약.

</details>

### D8 · suspend 중 GPIO wake 실패

#### 증상

```text
[예시 출력]
# cat /sys/power/mem_sleep
s2idle [deep]
# echo mem > /sys/power/state          <- deep 진입
  (버튼 눌러도 안 깸; RTC 알람으로는 깸)
# cat /sys/power/pm_wakeup_irq
45                                    <- RTC IRQ, 기대한 GPIO(57)가 아님
# cat /sys/kernel/irq/57/wakeup
disabled
```

- s2idle에선 GPIO로 깨는데 deep에선 안 깸 / 가끔 깨지만 첫 이벤트를 잃음 같은 변형.

#### 먼저 물어볼 것

- "s2idle or deep — and does it work in one but not the other?" (전원 도메인이 꺼지느냐의 차이)
- "Is this pin wake-capable — routed to an always-on domain or the PMU — per the integration spec?"
- "Edge or level wake, and can the event happen while the system is entering suspend?"
- "Does RTC wake work? That proves the general resume path."

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| wake 미무장 (`enable_irq_wake` 안 함, device wakeup disabled) | `wakeup = disabled` | `/sys/kernel/irq/<n>/wakeup`, `/sys/devices/.../power/wakeup` | 무장하면 해결 |
| 핀이 always-on 도메인 아님 | deep에서 GPIO 컨트롤러 전원 off | 통합 스펙의 wake-capable 핀 목록, DT `wakeup-source` | s2idle OK, deep 실패 |
| sleep pinctrl state가 pull/기능을 바꿈 | DT `pinctrl-names = "default", "sleep"` | sleep state 정의 | suspend 중 핀 레벨이 고정됨(scope) |
| debounce/edge 검출 클럭이 suspend 중 gate | 필터가 클럭 없이 멈춤 | debounce 끄고 시험 | debounce off면 깸 |
| suspend 진입 중 이벤트 → edge 래치가 진입 경로에서 지워짐 | 랜덤하게 실패 | 진입 타이밍에 맞춘 펄스 | 특정 위상에서만 실패 |
| resume 후 이벤트 처리 순서 | 깨긴 하는데 이벤트 손실 | 드라이버 resume 로그 | wake는 되는데 첫 이벤트 없음 |

#### 측정 · 계측

- RTC wake(`rtcwake -m mem -s 10`)로 **resume 경로는 정상**임을 먼저 확인 → GPIO wake 경로만 남김.
- scope 3채널: ① 버튼/INT 핀 ② PMU wake 요청 또는 PMIC 신호(노출돼 있으면) ③ 전원 rail 하나(깨어남 표시). 핀은 움직이는데 ②가 없으면 SoC 내부 wake 라우팅·도메인·무장 문제, ②도 있는데 안 깨면 PMU/펌웨어.
- 레이스: 펄스 발생기를 suspend 시작 명령에 맞춰 0–500 ms 지연 스윕 → 실패가 특정 구간에 몰리는지.

#### root cause · fix

- 대표 ①: 드라이버가 `device_init_wakeup`/`enable_irq_wake`를 안 해서 무장 안 됨. 대표 ②: 보드 설계가 버튼을 일반 GPIO 뱅크(deep에서 off)에 연결 → always-on wake 핀으로 옮기거나 PMU wake 입력 사용.
- 진입 레이스: level wake 또는 "suspend 마지막 단계에서 pending 확인 후 abort" — 커널은 wakeup 이벤트가 pending이면 suspend를 중단하는 구조가 있다 [추정: 드라이버가 wakeup 이벤트를 보고해야 동작].

#### 회귀로 굳히기

- suspend/resume 1000회 × wake 소스별(GPIO·RTC·UART RX) × 진입 중 랜덤 위상 이벤트 → wake 100%, `pm_wakeup_irq` = 기대 IRQ, wake latency p99 기록 ([N06C C4](2026-11-01_N06C_plans_peripherals_soc_infra.html) wake 행, [N06C C6](2026-11-01_N06C_plans_peripherals_soc_infra.html) 코어 off/on 행).

#### 30초 영어 답

"First I prove the resume path with an RTC wake, so only the GPIO wake path is in question. Then software arming: the IRQ's wakeup attribute shows disabled, so the driver never called enable_irq_wake. If arming is right but deep fails while s2idle works, the pin's controller is powered off in deep, and the pin has to be one of the always-on wake-capable pads. I scope the pin and the PMU wake request to split SoC routing from PMU firmware, and sweep the event timing around suspend entry to catch the race. The regression is a thousand suspend cycles per wake source with pm_wakeup_irq checked."

#### 꼬리 질문

<details>
<summary>Q1. How do you know which interrupt actually woke the system?</summary>

"Linux exposes the first wakeup interrupt of the last cycle in /sys/power/pm_wakeup_irq, and the wakeup_sources debugfs file shows per-source event counts. On the hardware side the PMU usually has a wake-reason register. If those disagree with what I triggered, I've found a spurious or misrouted wake."

한국어 핵심: `/sys/power/pm_wakeup_irq` + wakeup_sources + PMU wake 원인 레지스터.

</details>

<details>
<summary>Q2. Why can an edge-triggered wake be lost during suspend entry?</summary>

"Between the driver's suspend callback and the moment the wake logic is armed in the always-on domain, an edge can be latched in the normal GPIO status and then cleared or lost when that domain powers down. A level wake, or checking for pending wake events at the very last step and aborting suspend, closes the window. So I test with events injected at swept delays around suspend entry."

한국어 핵심: 진입 중 edge는 일반 도메인 status에 래치됐다 사라질 수 있음 → level wake 또는 마지막 pending 확인, 위상 스윕 시험.

</details>

## 6. 꼬리 질문 체인

### 체인 A — "The console prints garbage."

<details>
<summary>A-Q. The console prints garbage. Where do you start?</summary>

"I measure the bit rate at the SoC pin with a 0x55 stream before changing anything, because consistent garbage is a timing mismatch most of the time. The ratio between measured and expected tells me whether it's the reference clock or divisor rounding."

한국어 핵심: 0x55 → scope 주파수 → 비율로 클럭원/분주 판별 (D1).

</details>

<details>
<summary>꼬리1. The ratio is 0.969. Is that the problem?</summary>

"Minus 3.1 percent is inside the theoretical window on its own, so with a perfect partner it works — but it uses most of the budget. If the partner is also off a couple of percent in the other direction, or edges are slow, it fails. So I'd check the other side's error and fix the divisor, for example with 8x oversampling or a fractional divider, to get within 2 percent."

한국어 핵심: −3.1%는 단독으론 통과, 상대 오차와 합쳐지면 실패 → 각 쪽 ±2%로 (D3).

</details>

<details>
<summary>꼬리2. Derive the tolerance for me.</summary>

"The receiver syncs on the start edge, up to one sample tick late, then samples each bit at its center with its own clock. The stop bit is sampled at nine and a half bits, so drift accumulates to its maximum there. For a fast transmitter the stop sample must stay before ten transmitter bits: ten over nine-point-five-six is about plus 4.6 percent at 16x. For a slow transmitter it must stay after nine bits: nine over nine-point-five, minus 5.3 percent. Practical designs keep each side within 2 percent for edge distortion."

한국어 핵심: stop 샘플 9.5(+δ)비트가 경계 안 → +4.57%/−5.26% (16×), 실무 ±2%/쪽 (§2.3).

</details>

<details>
<summary>꼬리3. What would you add to the regression so this never reaches bring-up again?</summary>

"A bring-up P0 test that measures every supported baud's bit time and compares it with the computed divisor error, a clock-tree readback against the integration spec, and a margin test that offsets the partner baud and checks the window is where the math says. At the system level, a farm boot loop that verifies the console prompt."

한국어 핵심: baud 전수 측정 + 클럭 readback + margin 창 시험 + farm 부팅 루프.

</details>

### 체인 B — "An interrupt is sometimes missed."

<details>
<summary>B-Q. A GPIO interrupt is sometimes missed. How do you localize it?</summary>

"I count at every stage: pulses from a generator, edges at the pad, the kernel's interrupt count, and the device's own event counter. Where the counts first diverge is where the loss is — pad and controller, interrupt controller, or driver."

한국어 핵심: 단계별 카운트 비교로 손실 구간 특정 (D5).

</details>

<details>
<summary>꼬리1. The kernel count is lower than the pulses only above 50 kHz.</summary>

"Then it's probably coalescing: above the ISR's service rate, two edges during one handler become one pending interrupt, which is correct edge behavior. I'd measure the service rate and either document it as the limit, switch to level or a FIFO with a count, or move the counting into hardware."

한국어 핵심: 고주파에서만 = edge 합침(정상 동작) → 한계 문서화 또는 level/HW 카운터.

</details>

<details>
<summary>꼬리2. You switch to level and now it storms.</summary>

"Level means the line must be deasserted by the handler. A storm means the clear isn't reaching the device before unmask — clear-after-EOI ordering, a posted write that hasn't landed, or inverted polarity. I'd fix the order with a read-back to flush the write, and confirm polarity against the schematic."

한국어 핵심: level 폭주 = clear 순서/posted write/극성 → read-back 후 EOI (D6).

</details>

<details>
<summary>꼬리3. How do you test the fix at the silicon level, not just this board?</summary>

"From the integration table: for each source, trigger it, check it arrives once at the expected interrupt ID, then leave the status set and check level sources re-fire and edge sources don't. Plus a million-pulse count test at rising frequency per bank, with multiple pins in the same bank firing together to catch the write-one-to-clear race."

한국어 핵심: 통합 표 기반 type 일치 + 10^6 펄스 + 같은 뱅크 동시 발생 (N06C C4/C6).

</details>

### 체인 C — "How fast can we run this UART safely?"

<details>
<summary>C-Q. The UART clock is 100 MHz. Can we run 3 megabaud?</summary>

"At 16x the divisor is 100 over 48, about 2.08, which rounds to 2 — plus 4.2 percent. That's inside the 4.6 percent theoretical window only with a perfect partner and clean edges, so I'd call it unsafe. A 4-bit fractional divider gets it to plus 1 percent, or a 48 megahertz clock makes it exact."

한국어 핵심: 2.08→2 = +4.17% → 위험, 분수 분주 +1.01% 또는 48 MHz (§8 Part 1).

</details>

<details>
<summary>꼬리1. What about 8x oversampling?</summary>

"Same plus 4.17 percent here, but the 8x window on the fast side is only about 3.9 percent because start detection is coarser, so it fails outright. 8x helps when it gives a much better divisor, like 25 megahertz at 115200."

한국어 핵심: 8×는 창 +3.90% → 이 경우 오히려 실패 (S7).

</details>

<details>
<summary>꼬리2. Does majority voting give you more tolerance?</summary>

"Not for clean edges: two of three samples can only be wrong if the center one already is, so the window is the same. Majority helps with noise. If the UART flags disagreement between the three samples, that flag shows up about one tick earlier, which is actually a nice early-warning indicator in a margin test."

한국어 핵심: majority는 노이즈 내성, baud 창은 동일; noise flag는 1틱 먼저 (§8 Part 4).

</details>

<details>
<summary>꼬리3. And what else limits 3 megabaud besides the clock?</summary>

"Edges and latency. A 333-nanosecond bit can't afford a 100-nanosecond level-shifter edge, and with a 16-byte FIFO the driver has under 30 microseconds to respond. So I'd measure rise time at the receiver and use DMA with RTS/CTS."

한국어 핵심: 엣지(왜곡)와 FIFO 여유 시간(26.7 µs) → 측정 + DMA·흐름 제어 (D2).

</details>

### 체인 D — "The device doesn't wake from suspend on GPIO."

<details>
<summary>D-Q. Pressing the wake button doesn't resume the system. First steps?</summary>

"Prove the resume path with another wake source like the RTC, then check arming: the IRQ's wakeup attribute and the device's power/wakeup file. Then whether the pin is wake-capable in the suspend state we're using."

한국어 핵심: RTC로 resume 경로 증명 → 무장 → 핀의 wake 가능 여부 (D8).

</details>

<details>
<summary>꼬리1. It works in s2idle but not in deep. Why?</summary>

"In s2idle the SoC stays mostly powered and normal interrupt delivery wakes the CPU. In deep, most domains including that GPIO bank are off, and only always-on wake logic can wake the system. So either the pin isn't routed to that logic or its wake configuration isn't programmed in the always-on domain."

한국어 핵심: deep에선 일반 GPIO 뱅크 전원 off → always-on wake 경로 필요.

</details>

<details>
<summary>꼬리2. Arming and routing are correct, but one in fifty suspends never wakes.</summary>

"A one-in-fifty failure smells like a race at suspend entry. I'd inject the wake event at swept delays around the suspend command and look for a failing window, then check whether the edge was latched in a domain that powered off. Level wake or a last-moment pending check fixes it."

한국어 핵심: 1/50 = 진입 레이스 → 지연 스윕으로 창 찾기 → level wake/pending 확인.

</details>

## 7. Don 경험 연결

- "On every bring-up I've done — Cortex-R8 and R82 prototypes on FPGA at Solidigm, and now new wireless chipsets at Apple — the UART console was the first thing that had to be right, and FPGA prototypes run the UART from a different clock than silicon, so I learned to measure the bit rate instead of trusting the divisor. [숫자 채우기: FPGA UART clock vs silicon clock, e.g. MHz]"
- "At SK hynix I automated chamber reliability tests, so I think about temperature-dependent failures as a budget problem: measure the drifting quantity across temperature and overlay it on the limit. [숫자 채우기: temperature range, units, or test count]"
- "At Apple I root-cause interface failures with a scope and protocol analyzer and build factory test nodes for latent pin-level defects; counting events at every stage — stimulus, pin, interrupt count, driver — is how I localize lost or duplicated events. [숫자 채우기: example issue or defect rate]"

## 8. 실습 / 코드

### 8.1 무엇을 하나

[uart_baud_sim.c](onsitePrep/n07/d/uart_baud_sim.c) (C11, ASan/UBSan) — [run.sh](onsitePrep/n07/d/run.sh)가 빌드·실행하고 [run_output.txt](onsitePrep/n07/d/run_output.txt)를 쓴다.

- **Part 1 divisor 표**: [divisor_cases.txt](onsitePrep/n07/d/divisor_cases.txt)의 (드라이버가 가정한 클럭, 실제 클럭, 목표 baud, osr 16/8, 분수 비트 0/4, 기대 판정) → divisor · 실제 baud · 오차 % · 그 방향의 이론 한계 · 판정(OK ≤ 2% / MARGINAL 이론 창 안 / FAIL) · 비트 폭(측정값/이상값). 판정을 파일의 기대값과 비교해 PASS/FAIL.
- **Part 3 샘플링 시뮬레이터**: 오차 있는 TX가 8N1 프레임을 보내고, 수신기는 매 틱 라인을 보고 start를 찾은 뒤 osr/2틱 뒤 중앙, 이후 osr틱마다 샘플(중앙 1점 또는 3점 majority + noise flag). **oracle**이 각 샘플이 실제로 TX의 몇 번째 비트 구간에 떨어졌는지 계산 → "처음으로 다른 비트 칸을 샘플한 비트(slot slip)"와 "처음으로 값이 틀린 비트(visible)"를 따로 출력. FE, 바이트 오류, 프레임 수.
- **Part 2+4 허용 창**: 오차를 0.05% 스텝으로 키우며 32개 위상 × 16프레임(0x55 연속)이 무오류인 최대 오차를 찾고, §2.3 식과 비교(±0.15% 이내면 PASS).
- 모델 가정: 깨끗한 엣지, 8N1, FE 후 즉시 재탐색(16550의 재동기 설명과 유사 [1]). 엣지 왜곡·노이즈는 모델에 없음 → 실제 창은 더 좁다.

```bash
cd onsitePrep/n07/d && ./run.sh      # macOS: -isysroot MacOSX26.4.sdk, 바이너리는 $TMPDIR
```

### 8.2 실제 출력 (run_output.txt 전체)

```text
### uart_baud_sim.c  (cases: divisor_cases.txt)
== Part 1: divisor / baud error table (8N1, partner assumed exact) ==
case                fclk_drv fclk_real   target osr frac   divisor      actual     err%   limit% verdict  bit_us(meas/ideal)
c24_115k2_x16       24000000  24000000   115200  16    0   13.0000    115384.6   +0.160   +4.575 OK       8.667/8.681
  PASS  c24_115k2_x16 verdict OK (expected OK)
c25_115k2_x16       25000000  25000000   115200  16    0   14.0000    111607.1   -3.119   -5.263 MARGINAL 8.960/8.681
  PASS  c25_115k2_x16 verdict MARGINAL (expected MARGINAL)
c25_115k2_x8        25000000  25000000   115200   8    0   27.0000    115740.7   +0.469   +3.896 OK       8.640/8.681
  PASS  c25_115k2_x8 verdict OK (expected OK)
c100_921k6_x16     100000000 100000000   921600  16    0    7.0000    892857.1   -3.119   -5.263 MARGINAL 1.120/1.085
  PASS  c100_921k6_x16 verdict MARGINAL (expected MARGINAL)
c100_921k6_x16f4   100000000 100000000   921600  16    4    6.8125    917431.2   -0.452   -5.263 OK       1.090/1.085
  PASS  c100_921k6_x16f4 verdict OK (expected OK)
c48_3M_x16          48000000  48000000  3000000  16    0    1.0000   3000000.0   +0.000   +4.575 OK       0.333/0.333
  PASS  c48_3M_x16 verdict OK (expected OK)
c100_3M_x16        100000000 100000000  3000000  16    0    2.0000   3125000.0   +4.167   +4.575 MARGINAL 0.320/0.333
  PASS  c100_3M_x16 verdict MARGINAL (expected MARGINAL)
c100_3M_x8         100000000 100000000  3000000   8    0    4.0000   3125000.0   +4.167   +3.896 FAIL     0.320/0.333
  PASS  c100_3M_x8 verdict FAIL (expected FAIL)
c100_3M_x16f4      100000000 100000000  3000000  16    4    2.0625   3030303.0   +1.010   +4.575 OK       0.330/0.333
  PASS  c100_3M_x16f4 verdict OK (expected OK)
clk_assume24_is26   24000000  26000000   115200  16    0   13.0000    125000.0   +8.507   +4.575 FAIL     8.000/8.681
  PASS  clk_assume24_is26 verdict FAIL (expected FAIL)

== Part 3: sampling simulator (115200 nominal, 16 x 0x55 back-to-back unless noted) ==
S1 good divisor: 24 MHz/16 -> +0.16%
  [S1] e=+0.16% osr=16 center gap=0  frames tx=16 rx=16 byte_err=0 FE=0 noise=0
        first slot slip: frame -1 bit - (min over frames: -)   first VISIBLE wrong bit: frame -1 bit -
  PASS  S1 no errors, no slot slip
S2 25 MHz/16 divisor -> -3.12%, partner exact
  [S2] e=-3.12% osr=16 center gap=0  frames tx=16 rx=16 byte_err=0 FE=0 noise=0
        first slot slip: frame -1 bit - (min over frames: -)   first VISIBLE wrong bit: frame -1 bit -
  PASS  S2 alone it still works (inside -5.26% window)
S3 same TX (-3.12%) but partner RX clock +2.5% fast (RC osc drift)
  [S3] e=-5.49% osr=16 center gap=0  frames tx=16 rx=16 byte_err=0 FE=7 noise=0
        first slot slip: frame 1 bit stop (min over frames: stop)   first VISIBLE wrong bit: frame 1 bit stop
  PASS  S3 combined error breaks the budget: stop bit slips first -> FE
S4 driver assumed 24 MHz, UART clock is 26 MHz -> +8.51% ("console garbage")
  [S4] e=+8.51% osr=16 center gap=0  frames tx=15 rx=14 byte_err=15 FE=9 noise=0
        first slot slip: frame 0 bit d4 (min over frames: start)   first VISIBLE wrong bit: frame 0 bit d5
        sent: 48 65 6C 6C 6F 2C 20 52 69 76 69 61 6E 0D 0A  "Hello, Rivian.."
        recv: A8 B2 36 5E CB 04 42 D5 EC A9 B0 B7 23 E1  "..6^..B.....#."
  PASS  S4 garbage; frame 0 first slips at d4 or d5 (drift > half a bit), then frames are lost
S5 +5.0% just past the +4.57% limit: back-to-back vs 1 idle bit between frames
  [S5] 64 phases: back-to-back FE frames 890/1024 (runs with FE 64/64) ; with 1-bit gap FE=0 byte_err=0
  PASS  S5 near the limit FE depends on traffic: back-to-back fails, idle gap hides it
S6 +8.51% clock error, 2 idle bits between frames: 0xFF hides it, 0x55 exposes it
  [S6-FF] e=+8.51% osr=16 center gap=2  frames tx=16 rx=16 byte_err=0 FE=0 noise=0
        first slot slip: frame 0 bit d4 (min over frames: d4)   first VISIBLE wrong bit: frame -1 bit -
  [S6-55] e=+8.51% osr=16 center gap=2  frames tx=16 rx=16 byte_err=16 FE=0 noise=0
        first slot slip: frame 0 bit d4 (min over frames: d4)   first VISIBLE wrong bit: frame 0 bit d4
  PASS  S6 slot slips under 0xFF are invisible; 0x55 shows them -> test with 0x55/0xAA
S7 8x oversampling, +4.17% (100 MHz/8/div4 for 3 Mbaud, scaled to 115200)
  [S7] errors over 32 phases: 8x=809  16x=0
  PASS  S7 same +4.17% error: 16x survives, 8x (coarser start detection) fails

== Part 2+4: tolerance window, analytic vs simulated (8N1, clean edges) ==
receiver                      an. TX-    sim TX-    an. TX+    sim TX+
16x center                    -5.263%    -5.250%    +4.575%    +4.550%
  PASS  16x center: simulated edges within 0.15% of analytic
16x maj3 (data/FE)            -5.263%    -5.250%    +4.575%    +4.550%
  PASS  16x maj3 (data/FE): simulated edges within 0.15% of analytic
16x maj3 noise-flag-free      -4.636%    -4.600%    +3.896%    +3.850%
  PASS  16x maj3 noise-flag-free: simulated edges within 0.15% of analytic
8x center                     -5.263%    -5.250%    +3.896%    +3.850%
  PASS  8x center: simulated edges within 0.15% of analytic
  combined budget 16x center = 9.84% total span; practical rule: each side within +/-2%

SUMMARY: 21 PASS, 0 FAIL

ALL N07D PROGRAMS PASS
```

### 8.3 출력 읽는 법

- **Part 1 `c24_115k2_x16`**: 24 MHz/(16×115200) = 13.02 → 13, +0.16% — 콘솔 baud로 이상적. 비트 폭 8.667 µs vs 이상 8.681 µs: scope로는 거의 구분 불가.
- **`c25_115k2_x16` vs `c25_115k2_x8`**: 같은 25 MHz인데 16×는 14로 반올림돼 −3.12%(MARGINAL), 8×는 27로 +0.47%(OK). [BV10 §2.3](2026-11-01_BV10_board_interface_tests.html) 표의 −3.12% 행에 대한 **해결책**이 여기.
- **`c100_921k6_x16f4`**: 4-bit 분수 분주로 −3.12% → −0.45%. **`c100_3M_x16` / `_x8` / `_x16f4`**: 3 Mbaud는 정수 분주로 +4.17%, 16×면 창(+4.575%) 안이라 MARGINAL, 8×면 창(+3.896%) 밖이라 FAIL, 분수 분주면 +1.01% OK (D2·체인 C).
- **`clk_assume24_is26`**: 드라이버는 24 MHz로 divisor 13을 계산했지만 실제 26 MHz → 125000 baud, +8.51%. bit_us 8.000/8.681 — **scope에서 보이는 숫자**(D1).
- **S1/S2**: −3.12%는 상대가 정확하면 무오류. **S3**: 같은 TX에 상대 RX +2.5% → 비율 −5.49% → 16프레임 중 7개 FE, **byte_err 0** — 경계 근처에선 stop만 깨진다(D3 꼬리1). 프레임 0은 통과·프레임 1부터 FE = 프레임마다 검출 지연 δ가 달라서.
- **S4**: +8.51%에서 첫 프레임은 **d4에서 첫 slot slip**(식으로 예측한 d4–d5와 일치), 하지만 'H'(0x48)는 d4와 d5가 둘 다 0이라 값으로는 **d5에서 처음 틀림**. 이후 FE → 재동기 실패로 15프레임 중 14개만 수신, "min over frames: start"는 프레임 정렬을 잃은 뒤의 연쇄 효과(첫 프레임만 깨끗한 측정). 수신 문자열이 D1 증상의 "깨진 글자"를 그대로 보여 준다.
- **S5**: +5.0%(창 +4.57% 바로 밖)에서 연속 송신은 1024프레임 중 890 FE, 1비트 idle gap을 두면 0 — **같은 클럭 오차가 트래픽 모양에 따라 보였다 안 보였다** 한다. 시험은 연속 송신으로.
- **S6**: +8.51%로 d4부터 슬립하는데 0xFF + idle gap은 byte_err 0, FE 0 → **완전히 숨는다**. 0x55는 16/16 오류. 테스트 패턴은 0x55/0xAA.
- **S7**: 같은 +4.17%에서 8×는 809 오류, 16×는 0 — §2.3의 "8×는 창이 좁다".
- **Part 4**: 시뮬레이션 창이 식과 0.05% 이내로 일치(시뮬레이션이 약간 안쪽인 건 32위상이 최악 δ에 정확히 닿지 않아서). majority(data/FE)는 중앙 샘플과 **같은 창**, noise flag가 안 뜨는 창은 1틱만큼 좁다(+3.90/−4.64%). "9.84% total span"은 −5.26%~+4.58% 전체 폭이지 "각 쪽 ±4.9%"가 아니다 — 비율 r 기준이라 한쪽에 몰리면 그쪽 한계만 의미 있다.

### 8.4 확장 아이디어 [미검증]

- 엣지 왜곡 모델 추가(상승시간 tr과 임계 전압으로 0/1 비트 폭 비대칭) → 3 Mbaud에서 창이 얼마나 줄어드는지.
- 7E1/8N2/parity 프레임 길이별 창 표, 상대 baud 스윕 결과를 CSV로 내보내 margin 그래프.
- GPIO 엣지 합침 시뮬(펄스율 vs ISR 처리 시간 → 손실률)은 [N06C gic_model.c](onsitePrep/n06/c/gic_model.c)의 latency Monte Carlo와 묶을 수 있음.

## 9. 자주 틀리는 것

1. **"깨진 글자 = 드라이버 버그"로 바로 가기** — 비트 폭 한 번이면 클럭/분주/그 외가 갈린다. 측정 전에 코드부터 보지 않는다.
2. **허용 오차를 "각 쪽 ±5%"로 말하기** — 이론 창은 두 클럭의 **비율**에 대한 것(+4.57%/−5.26%, 16×)이고 양쪽 오차가 반대 방향이면 더해진다. 그래서 각 쪽 ±2%.
3. **"8×로 바꾸면 더 정확해진다"** — 분주 분해능은 좋아지지만 start 검출 불확실성이 1/8비트로 커져 TX-fast 창이 +3.90%로 줄어든다. 계산해서 고른다.
4. **"majority voting이 baud 허용을 늘린다"** — 깨끗한 엣지에선 중앙 1점과 창이 같다. 노이즈 내성이다.
5. **0xFF·0x00·idle이 많은 트래픽으로 baud 시험** — 슬립이 숨는다(S6). 0x55/0xAA 연속 송신.
6. **`/proc/tty/driver/serial`을 일반 사용자로 보고 "에러 없음"** — 카운터는 root(CAP_SYS_ADMIN)에서만, 그리고 0이 아닐 때만 출력된다 [2]. `oe`(HW)와 `bo`(tty 버퍼)도 다른 층.
7. **수정 발진기 drift를 고온 FE 원인으로 의심** — ±50 ppm = 0.005%, 창의 1/1000. RC 발진기·분주 오차·엣지를 본다.
8. **"인터럽트 안 옴"을 HW 문제로 단정** — 그 전에 storm이 있었다면 커널이 "nobody cared" 후 disable했을 수 있다 [6]. dmesg 먼저.
9. **active-low 혼동** — 회로도 INT_N, DT `IRQ_TYPE_LEVEL_HIGH`, gpioinfo `active-low` 표시가 서로 다른 층의 "low"를 말한다. raw 핀 레벨(scope)과 읽은 값을 대조.
10. **debounce를 만능 해결책으로** — 지연·짧은 펄스 손실, suspend 중 debounce 클럭 gate로 wake 실패까지 부를 수 있다. 측정한 bounce 폭으로 크기를 정한다.

## 10. 출처

1. TI, PC16550D UART with FIFOs datasheet (SNLS378C, rev. May 2015) — LSR 비트 정의(OE/PE/FE/BI/THRE/TEMT/LSR7), FIFO timeout 4문자 시간, FE 후 재동기, divisor 식: https://www.ti.com/product/PC16550D (열람: 미러 https://www.cs.unc.edu/~porter/courses/comp630/ref/PC16550D.pdf)
2. Linux kernel, drivers/tty/serial/serial_core.c `uart_line_info()` — /proc/tty/driver 출력 형식(tx/rx, 0이 아닐 때 fe/pe/brk/oe/bo, CAP_SYS_ADMIN): https://github.com/torvalds/linux/blob/master/drivers/tty/serial/serial_core.c
3. Linux kernel, Documentation/admin-guide/serial-console.rst — console= 문법, 여러 개일 때 마지막이 /dev/console: https://www.kernel.org/doc/html/latest/admin-guide/serial-console.html
4. libgpiod v2 man pages — gpiomon(-c, -e, -b, -p, -n, -E, -F 형식 지정자), gpioget(-a, -b, -l): https://man.archlinux.org/man/gpiomon.1.en , https://man.archlinux.org/man/gpioget.1.en
5. Linux kernel, Documentation/ABI/testing/sysfs-kernel-irq — /sys/kernel/irq/<irq>/actions, chip_name, hwirq, name, per_cpu_count, type, wakeup: https://github.com/torvalds/linux/blob/master/Documentation/ABI/testing/sysfs-kernel-irq
6. Linux kernel, kernel/irq/spurious.c — "nobody cared", 100,000 중 99,900 unhandled 시 "Disabling IRQ", irqpoll: https://github.com/torvalds/linux/blob/master/kernel/irq/spurious.c
7. Linux kernel, Documentation/ABI/testing/sysfs-power — /sys/power/pm_wakeup_irq: https://github.com/torvalds/linux/blob/master/Documentation/ABI/testing/sysfs-power
8. Linux kernel, drivers/clk/clk.c — clk_disable_unused, clk_ignore_unused: https://github.com/torvalds/linux/blob/master/drivers/clk/clk.c
9. Maxim Integrated (Analog Devices), Tutorial 2141 "Determining Clock Accuracy Requirements for UART Communications" (검색 요약만 확인, 원문 열람 실패): https://www.analog.com/en/resources/technical-articles/determining-clock-accuracy-requirements-for-uart-communications.html
10. Linux kernel, kernel-parameters — earlycon(옵션 없으면 DT stdout-path), clk_ignore_unused, keep_bootcon, irqpoll: https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html
