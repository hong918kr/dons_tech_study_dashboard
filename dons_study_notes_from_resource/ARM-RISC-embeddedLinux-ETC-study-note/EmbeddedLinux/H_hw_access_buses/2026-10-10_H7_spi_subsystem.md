# H7. SPI 서브시스템 — spi_controller / spi_device / spi_driver, 메시지·트랜스퍼, spi_sync/async, spidev (컨트롤러 쪽까지 직접 만들어 보기)

> **이 노트를 다 읽으면**: SPI 서브시스템의 세 객체(`spi_controller`·`spi_device`·`spi_driver`)와 DT 자식 노드가 어떻게 이어지는지 그릴 수 있다 · `spi_transfer`/`spi_message`/`cs_change`로 "CS 한 번에 무엇을 보내는가"를 정확히 설계하고, `spi_sync`·`spi_async`·`spi_write_then_read`·regmap 중 맞는 것을 고를 수 있다 · `transfer_one` + `set_cs` + 완료 IRQ로 컨트롤러 드라이버를 직접 짜고, 메시지 펌프 kthread와 콜백 문맥을 ftrace로 증명할 수 있다 · spidev를 `driver_override`로 붙여 유저 공간 테스트 툴을 만들고, "스택 버퍼를 SPI에 넘기면 안 되는 이유"를 캐시라인 수준으로 설명할 수 있다
> **왜 필요한가**: 로봇·드론 센서 키트의 IMU·ADC·디스플레이·CAN 컨트롤러(MCP2515)·TPM 대부분이 SPI다. 면접에서는 "SPI 드라이버 구조를 설명해 보세요", "spi_sync와 spi_async 차이는?", "cs_change가 뭐죠?", "왜 스택 버퍼를 쓰면 안 되죠?", "spidev는 언제 쓰나요?"로 나온다
> **Don 기준 난이도**: SPI 프로토콜(mode 0~3, CS 타이밍, 스코프로 보는 파형)은 이미 강하다 / 새로 배울 것은 Linux가 그 파형을 **객체·메시지·큐**로 추상화하는 방식, 그리고 그 추상화가 만드는 함정(CS를 언제 놓는가, 콜백이 어느 문맥에서 도는가, DMA-safe 버퍼)
> **선행 노트**: D2·D3(DT와 compatible 매칭), E3(커널 모듈), F6(인터럽트·threaded IRQ), G2·G3(디바이스 모델·probe 생명주기), G8(sysfs/debugfs), H4(GPIO 디스크립터), H5(I2C — 같은 모양의 다른 버스) — H2(regmap), H9(DMA), I(IIO)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don의 bare-metal 펌웨어에서 SPI IMU를 읽는 코드는 대략 이랬다.

```c
cs_low();                          /* GPIO 또는 SPI 블록의 SS 비트 */
spi_xfer_byte(0x80 | 0x3B);        /* 읽기 명령 + 레지스터 주소 */
for (i = 0; i < 6; i++)
    buf[i] = spi_xfer_byte(0x00);  /* 더미를 보내며 받는다 */
cs_high();
```

한 파일 안에 **칩 프로토콜**(0x80은 읽기 비트, 0x3B는 ACCEL_XOUT_H)과 **컨트롤러 조작**(SS 비트, 데이터 레지스터, 상태 폴링)이 섞여 있다. 보드가 바뀌어 SPI 블록이 바뀌면 IMU 코드도 고쳐야 한다.

Linux는 이것을 정확히 두 층으로 자른다.

- **프로토콜 드라이버** (`spi_driver`, 예: IMU 드라이버) — "이 칩에는 0x80|reg를 보내고 6바이트를 받는다"만 안다. 어느 SoC의 어떤 SPI 블록인지 모른다.
- **컨트롤러 드라이버** (`spi_controller`, 예: i.MX의 `spi-imx`, TI의 `spi-omap2-mcspi`) — "이 SoC의 SPI 블록으로 바이트를 밀고 CS를 내리는 법"만 안다. 어떤 칩이 붙었는지 모른다.
- 둘 사이의 **SPI core**(`drivers/spi/spi.c`)가 메시지 큐, CS 제어 규칙, DMA 매핑, 통계, tracepoint를 맡는다.

QEMU `virt` 머신에는 SPI 컨트롤러가 없다. 그래서 이 노트는 **컨트롤러 드라이버까지 직접 만든다**. `h7_fakectlr`는 진짜 `spi_controller`로 등록되고, 그 뒤에 소프트웨어로 흉내 낸 IMU(CS0)와 12-bit ADC(CS1)가 매달려 있다. 덕분에 프로토콜 드라이버 쪽에서 보내는 모든 것이 컨트롤러에서 어떻게 보이는지 양쪽 끝을 동시에 관찰할 수 있다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" text-anchor="start" font-size="12" font-weight="bold">유저 공간</text>
<rect x="20" y="32" width="200" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="120.0" y="52.5" text-anchor="middle" font-size="13">h7_spidev_adc</text>
<text x="120.0" y="70.5" text-anchor="middle" font-size="12">ioctl(SPI_IOC_MESSAGE(2))</text>
<rect x="250" y="32" width="170" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="335.0" y="52.5" text-anchor="middle" font-size="13">/dev/spidev0.1</text>
<text x="335.0" y="70.5" text-anchor="middle" font-size="12">문자 장치 (major 153)</text>
<line x1="220" y1="57" x2="247" y2="57" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="10" y1="96" x2="670" y2="96" stroke="currentColor" stroke-dasharray="6,4"/>
<text x="20" y="116" text-anchor="start" font-size="12" font-weight="bold">프로토콜(클라이언트) 드라이버 — 칩을 안다</text>
<rect x="20" y="126" width="220" height="54" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="130.0" y="148.6" text-anchor="middle" font-size="13">h7_imu (spi_driver)</text>
<text x="130.0" y="166.6" text-anchor="middle" font-size="12">spi_sync · spi_async · regmap</text>
<rect x="270" y="126" width="170" height="54" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="355.0" y="148.6" text-anchor="middle" font-size="13">spidev (spi_driver)</text>
<text x="355.0" y="166.6" text-anchor="middle" font-size="12">ioctl → spi_sync</text>
<line x1="335" y1="82" x2="335" y2="123" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="470" y="126" width="190" height="54" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-dasharray="5,4"/>
<text x="565.0" y="148.6" text-anchor="middle" font-size="13">IIO · input · MTD …</text>
<text x="565.0" y="166.6" text-anchor="middle" font-size="12">실제 제품의 클라이언트</text>
<text x="20" y="206" text-anchor="start" font-size="12" font-weight="bold">SPI core — 규칙과 줄 세우기</text>
<rect x="20" y="216" width="640" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340.0" y="238.6" text-anchor="middle" font-size="13">SPI core (drivers/spi/spi.c)</text>
<text x="340.0" y="256.6" text-anchor="middle" font-size="12">메시지 큐 · 펌프 kthread "spi0" · CS 제어 · DMA 매핑 · 통계 · tracepoint</text>
<line x1="225" y1="180" x2="225" y2="213" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="355" y1="180" x2="355" y2="213" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="20" y="296" text-anchor="start" font-size="12" font-weight="bold">컨트롤러(호스트) 드라이버 — 선을 안다</text>
<rect x="20" y="306" width="640" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340.0" y="326.6" text-anchor="middle" font-size="13">h7_fakectlr (spi_controller "spi0")</text>
<text x="340.0" y="344.6" text-anchor="middle" font-size="12">setup · set_cs · transfer_one → 완료 IRQ → spi_finalize_current_transfer</text>
<line x1="340" y1="270" x2="340" y2="303" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="10" y1="370" x2="670" y2="370" stroke="currentColor" stroke-dasharray="6,4"/>
<text x="20" y="390" text-anchor="start" font-size="12" font-weight="bold">하드웨어 (흉내)</text>
<text x="400" y="390" text-anchor="start" font-size="12">SCLK · MOSI · MISO 공유</text>
<rect x="20" y="400" width="190" height="54" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="115.0" y="422.6" text-anchor="middle" font-size="13">IMU @ CS0</text>
<text x="115.0" y="440.6" text-anchor="middle" font-size="12">mode 3 · 10 MHz</text>
<rect x="240" y="400" width="190" height="54" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="335.0" y="422.6" text-anchor="middle" font-size="13">12-bit ADC @ CS1</text>
<text x="335.0" y="440.6" text-anchor="middle" font-size="12">mode 0 · 1 MHz · GPIO CS</text>
<rect x="460" y="400" width="200" height="54" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5,4"/>
<text x="560.0" y="422.6" text-anchor="middle" font-size="13">legacy @ CS2</text>
<text x="560.0" y="440.6" text-anchor="middle" font-size="12">"spidev" compatible</text>
<line x1="170" y1="356" x2="170" y2="397" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="335" y1="356" x2="335" y2="397" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="560" y1="356" x2="560" y2="397" stroke="currentColor" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — 이 노트의 실습 구성. 위의 프로토콜 드라이버는 "칩"을, 아래의 컨트롤러 드라이버는 "선"을 안다. 가운데 SPI core가 줄을 세우고 규칙을 지킨다. 실제 제품에서는 맨 아래 칩이 진짜 하드웨어이고, 컨트롤러 드라이버는 SoC 벤더가 이미 써 두었다.

| bare-metal 펌웨어 | Linux SPI 서브시스템 |
|---|---|
| `spi1_init()` + SPI 블록 레지스터 | 컨트롤러 드라이버가 채운 `struct spi_controller` (SoC 벤더 몫) |
| `board.h`의 "IMU = SPI1, CS0, mode 3, 10 MHz" | DT 자식 노드 `imu@0 { reg = <0>; spi-cpol; spi-cpha; spi-max-frequency = ... }` → `struct spi_device` |
| `imu.c`의 init/read 함수 | `struct spi_driver`의 `probe(spi)` + `spi_sync()` 호출 |
| `cs_low(); xfer(); xfer(); cs_high();` | `spi_message` 하나 = CS 한 번 잡기, 그 안의 `spi_transfer` 여러 개 |
| 폴링 루프 또는 DMA 완료 IRQ | `transfer_one()` → 완료 IRQ에서 `spi_finalize_current_transfer()` |
| 태스크가 SPI 드라이버를 독점 | SPI core의 메시지 큐 + 펌프 kthread가 여러 장치의 요청을 줄 세움 |
| 테스트용 디버그 커맨드 | spidev (`/dev/spidevB.C`) + `ioctl(SPI_IOC_MESSAGE(n))` |

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안의 공용 QEMU 랩에서 실제로 돌린 결과다. 게스트는 QEMU `virt`(cortex-a57, `-smp 2`, 512 MB) + 랩의 **디버그 커널 `6.18.55-dbg`**(`/var/lab/out/Image-dbg`: ftrace·lockdep·`DEBUG_ATOMIC_SLEEP` 포함) + BusyBox initramfs. 랩 커널 설정은 `CONFIG_SPI=y`, `CONFIG_SPI_SPIDEV=m`, `CONFIG_REGMAP_SPI=y`이고 `spidev.ko`는 랩 빌드 트리(`/var/lab/build/linux-dbg/drivers/spi/spidev.ko`)의 것을 그대로 썼다. 타임스탬프·PID·포인터 해시는 실행마다 다르다. QEMU는 TCG(에뮬레이션)라 시간 수치는 상대 비교용이다.

### 0.1 이 노트의 실습 지도

| 예제 | 무엇을 하나 | 절 |
|---|---|---|
| 1 | 6.18 소스에서 세 구조체와 접근자 확인 | §1 |
| 2~5 | 칩 모델, 가짜 컨트롤러 `h7_fakectlr`, DT 노드, 빌드·부팅 스크립트 | §3 |
| 6 | 컨트롤러 등록 → 자식 `spi_device` 생성 → 클라이언트 바인딩, "spidev" compatible 거부 | §3.6 |
| 7 | 클라이언트 `h7_imu`: `spi_w8r8`, `spi_write_then_read`, `cs_change` 있음/없음 | §4 |
| 8~9 | `spi_async` 콜백 문맥과 순서, ftrace로 본 sync(호출자) vs async(kthread) | §5 |
| 10 | 같은 IMU를 `devm_regmap_init_spi`로 | §6 |
| 11 | spidev를 `driver_override`로 CS1(ADC)에 붙이고 `SPI_IOC_MESSAGE(2)`, GPIO CS 파형을 trace로 | §7 |
| 12 | 스택 버퍼를 `spi_sync`에 넘기는 버그 — 코어가 그걸 그대로 DMA 매핑한다 | §8 |
| 13 | 처리량: 작은 `spi_sync` N번 vs 묶은 메시지 vs `spi_async` 파이프라인 | §9 |

---

## 1. 세 객체 — `spi_controller`, `spi_device`, `spi_driver`

### 1.1 직관 — I2C(H5)와 같은 3자 구도

H5의 I2C를 기억하면 거의 그대로다.

| 역할 | I2C (H5) | SPI (이 노트) |
|---|---|---|
| 버스를 움직이는 하드웨어 블록 | `struct i2c_adapter` | `struct spi_controller` (옛 이름 `spi_master`) |
| 버스 위의 칩 하나 | `struct i2c_client` (주소 0x68) | `struct spi_device` (칩 셀렉트 번호 0) |
| 칩을 아는 드라이버 | `struct i2c_driver` | `struct spi_driver` |
| 한 번의 거래 | `struct i2c_msg` 배열 + `i2c_transfer` | `struct spi_transfer` 리스트를 담은 `struct spi_message` + `spi_sync` |
| 유저 공간 통로 | `/dev/i2c-N` (i2c-dev) | `/dev/spidevB.C` (spidev) |

차이는 두 가지다. 첫째, SPI에는 **주소가 없다**. 칩은 CS 선으로 고른다. 그래서 DT 자식 노드의 `reg`는 칩 셀렉트 번호다. 둘째, SPI는 **전이중**이다. 보내는 동안 받는다. 그래서 `spi_transfer`는 `tx_buf`와 `rx_buf`를 동시에 가진다.

### 1.2 예제 1 — 6.18 소스에서 구조체 확인

무엇을 확인하나: 랩 커널 소스에서 세 구조체의 핵심 필드와, 6.x에서 바뀐 칩 셀렉트 접근 방식.

```sh
cd /var/lab/src/linux-6.18.55
grep -nE "^\s+(int|void|bool|u8|u16|u32|s16|struct)[^;]*\b(bus_num|num_chipselect|mode_bits|bits_per_word_mask|min_speed_hz|max_speed_hz|transfer_one|transfer_one_message|transfer|setup|set_cs|prepare_transfer_hardware|unprepare_transfer_hardware|cs_gpiods|use_gpio_descriptors|must_async|kworker|rt|can_dma)\b" include/linux/spi/spi.h
grep -n "spi_get_chipselect\|spi_set_chipselect\|spi_get_csgpiod\|devm_spi_alloc_host" include/linux/spi/spi.h
```

```text
186:	u32			max_speed_hz;
188:	bool			rt;
228:	u8			num_chipselect;
566:	s16			bus_num;
572:	u16			num_chipselect;
580:	u32			mode_bits;
586:	u32			bits_per_word_mask;
591:	u32			min_speed_hz;
592:	u32			max_speed_hz;
646:	int			(*setup)(struct spi_device *spi);
678:	int			(*transfer)(struct spi_device *spi,
691:	bool			(*can_dma)(struct spi_controller *ctlr,
705:	struct kthread_worker		*kworker;
715:	bool				rt;
726:	int (*prepare_transfer_hardware)(struct spi_controller *ctlr);
727:	int (*transfer_one_message)(struct spi_controller *ctlr,
729:	int (*unprepare_transfer_hardware)(struct spi_controller *ctlr);
740:	void (*set_cs)(struct spi_device *spi, bool enable);
741:	int (*transfer_one)(struct spi_controller *ctlr, struct spi_device *spi,
742:			    struct spi_transfer *transfer);
758:	struct gpio_desc	**cs_gpiods;
759:	bool			use_gpio_descriptors;
787:	bool			must_async;
1329:	u32 bpw_mask = spi->controller->bits_per_word_mask;
1653:	u32		max_speed_hz;
1663:	u16		bus_num;
291:static inline u8 spi_get_chipselect(const struct spi_device *spi, u8 idx)
296:static inline void spi_set_chipselect(struct spi_device *spi, u8 idx, u8 chipselect)
301:static inline struct gpio_desc *spi_get_csgpiod(const struct spi_device *spi, u8 idx)
316:		if (spi_get_csgpiod(spi, idx))
860:static inline struct spi_controller *devm_spi_alloc_host(struct device *dev,
```

출력에서 볼 것: 186~228행은 `struct spi_device`, 566~787행이 `struct spi_controller`다(1329행 이후는 인라인 함수와 `struct spi_board_info`라 무시). `spi_device` 안에 `u8 chip_select[SPI_DEVICE_CS_CNT_MAX]` **배열**이 있고(아래 예제 1(b)), 그래서 드라이버는 `spi->chip_select`를 직접 읽지 않고 `spi_get_chipselect(spi, 0)`을 쓴다. 이 접근자는 6.3 무렵 들어왔고, 그 뒤 병렬 플래시(한 장치가 CS 여러 개)를 위해 필드가 배열이 되었다(버전 번호는 기억 기반 — 6.18 소스의 모양은 위가 사실이다). 옛날 블로그의 `spi->chip_select`, `spi->cs_gpiod`, `spi_alloc_master()` 코드는 지금 커널에서 컴파일되지 않거나 경고가 난다. 이름도 `master/slave` → `host/target`으로 바뀌었다(`spi_alloc_host`, `devm_spi_alloc_host`). 다만 sysfs 클래스 이름은 호환성 때문에 여전히 `/sys/class/spi_master/`다(예제 6에서 보인다).

예제 1(b) — `struct spi_device` 본체(발췌, 6.18 `include/linux/spi/spi.h`):

```c
struct spi_device {
	struct device		dev;
	struct spi_controller	*controller;
	u32			max_speed_hz;
	u8			bits_per_word;
	bool			rt;
	...
	u32			mode;
	int			irq;
	void			*controller_state;
	void			*controller_data;
	char			modalias[SPI_NAME_SIZE];
	...
	struct spi_delay	word_delay; /* Inter-word delay */
	struct spi_delay	cs_setup;
	struct spi_delay	cs_hold;
	struct spi_delay	cs_inactive;

	u8			chip_select[SPI_DEVICE_CS_CNT_MAX];
	u8			num_chipselect;
	u32			cs_index_mask : SPI_DEVICE_CS_CNT_MAX;
	struct gpio_desc	*cs_gpiod[SPI_DEVICE_CS_CNT_MAX];	/* Chip select gpio desc */
};
```

(`sed -n 160,260p include/linux/spi/spi.h`의 출력에서 주석과 `#define`을 `...`로 줄였다.)

### 1.3 `struct spi_controller` — 컨트롤러 드라이버가 채우는 것

컨트롤러 드라이버의 probe는 결국 "이 구조체를 채우고 `devm_spi_register_controller()`"다. 채우는 필드는 **능력 선언**과 **콜백** 두 종류다.

| 필드 | 뜻 | 이 노트의 값 |
|---|---|---|
| `bus_num` | 버스 번호. 음수면 코어가 DT alias `spiN` 또는 동적 번호를 준다 | -1 (기본) → `spi0` |
| `num_chipselect` | CS 개수. `cs-gpios`가 더 많으면 코어가 늘린다 | 3 |
| `mode_bits` | 이 컨트롤러가 지원하는 `spi->mode` 비트. 없는 비트를 요청하면 `spi_setup`이 거부 | CPOL + CPHA + CS_HIGH |
| `bits_per_word_mask` | 지원 워드 길이. `SPI_BPW_MASK(8)` 식 | 8, 16 |
| `min_speed_hz` / `max_speed_hz` | 클럭 범위. transfer 속도가 넘으면 코어가 깎는다 | 100 kHz / 20 MHz |
| `setup(spi)` | 장치별 설정 저장(모드·속도). 슬립 가능 문맥에서 불림 | 로그만 |
| `set_cs(spi, level)` | 내장 CS 선을 움직인다. **level = 전기적 레벨**(아래 §3.3) | 로그 + IMU 상태기계 리셋 |
| `transfer_one(ctlr, spi, xfer)` | transfer 하나를 실행. 0 = 끝남, 양수 = 진행 중(나중에 `spi_finalize_current_transfer`) | hrtimer로 완료 IRQ 흉내 |
| `transfer_one_message(ctlr, msg)` | 메시지 전체를 직접 처리(CS까지). 안 채우면 코어의 기본 구현이 `transfer_one`을 반복 호출 | 기본 구현 사용 |
| `transfer(spi, msg)` | **옛 방식**: 큐까지 드라이버가 직접. 새 드라이버는 쓰지 않는다 | 사용 안 함 |
| `prepare_transfer_hardware` / `unprepare_...` | 큐가 바빠지면/한가해지면 한 번씩. 클럭·런타임 PM 켜고 끄는 자리 | 로그만 |
| `use_gpio_descriptors` / `cs_gpiods` | DT `cs-gpios`를 코어가 읽어 GPIO로 CS를 대신 움직여 준다 | true (CS1이 GPIO) |
| `can_dma(ctlr, spi, xfer)` | 이 transfer를 DMA로 할지. 참이면 코어가 버퍼를 `dma_map_sg` | 모듈 파라미터 `use_dma` |
| `rt` | 메시지 펌프 kthread를 SCHED_FIFO로 | 모듈 파라미터 `rt` |
| `must_async` | `spi_sync`의 "호출자 문맥에서 바로 전송" 지름길을 끈다 | 기본(false) |

세 가지 전송 콜백은 "드라이버가 얼마나 많이 떠맡나"의 차이다.

| 콜백 | 드라이버가 하는 일 | 코어가 해 주는 일 | 언제 쓰나 |
|---|---|---|---|
| `transfer_one` (권장) | transfer 하나의 데이터 이동 | 큐, 펌프, CS(`set_cs` 호출), `cs_change`, delay, 통계, DMA 매핑, timeout | 거의 모든 새 드라이버 |
| `transfer_one_message` | 메시지 전체(CS 포함) | 큐, 펌프 | 하드웨어가 "메시지 단위"로 CS를 자동 관리할 때 |
| `transfer` (legacy) | 큐잉까지 전부 | 거의 없음 | 아주 오래된 드라이버. 쓰지 말 것 |

### 1.4 `struct spi_device` — 칩 하나의 설정

`spi_device`는 보통 **코어가 DT를 읽어서 만든다**. 드라이버는 probe에서 받기만 한다.

| 필드/접근자 | DT 속성 | 뜻 |
|---|---|---|
| `spi_get_chipselect(spi, 0)` | `reg = <N>` | 칩 셀렉트 번호 |
| `max_speed_hz` | `spi-max-frequency` | 이 칩의 최대 클럭. transfer의 `speed_hz`가 0이면 이 값 |
| `mode` 의 `SPI_CPOL` / `SPI_CPHA` | `spi-cpol` / `spi-cpha` | mode 0~3 (§2) |
| `mode` 의 `SPI_CS_HIGH` | `spi-cs-high` | CS active-high 칩 |
| `mode` 의 `SPI_LSB_FIRST` | `spi-lsb-first` | LSB 먼저 |
| `mode` 의 `SPI_3WIRE` | `spi-3wire` | MOSI/MISO 한 선 공유(반이중) |
| `mode` 의 `SPI_TX_DUAL/QUAD`, `SPI_RX_...` | `spi-tx-bus-width`, `spi-rx-bus-width` | 듀얼/쿼드 (주로 플래시, spi-mem) |
| `bits_per_word` | (DT 표준 속성 없음) | 기본 8. 드라이버나 spidev ioctl이 바꾼다 |
| `irq` | `interrupts` | 데이터 레디 핀 같은 칩의 IRQ (F6) |
| `cs_setup` / `cs_hold` / `cs_inactive` | `spi-cs-setup-delay-ns`, `spi-cs-hold-delay-ns`, `spi-cs-inactive-delay-ns` | CS 타이밍 지연 |

### 1.5 `struct spi_driver` — 칩을 아는 드라이버

```c
struct spi_driver {
	const struct spi_device_id *id_table;
	int (*probe)(struct spi_device *spi);
	void (*remove)(struct spi_device *spi);
	void (*shutdown)(struct spi_device *spi);
	struct device_driver driver;
};
```

G3의 platform driver와 모양이 같다(6.x에서 `remove`는 `void` 반환). 매칭은 `spi_match_device()`가 **driver_override → OF compatible → ACPI → `id_table`(modalias) → 드라이버 이름** 순서로 한다. 한 가지 SPI만의 규칙: `of_match_table`에 `"vendor,chip"`이 있으면 `id_table`에도 `"chip"`이 있어야 한다. 없으면 드라이버 등록 때 코어가 `SPI driver %s has no spi_device_id for %s` 경고를 찍는다(6.18 `drivers/spi/spi.c` 515행). 모듈 자동 로딩(uevent의 `MODALIAS=spi:chip`)이 `id_table` 쪽을 쓰기 때문이다.

### 1.6 DT와 객체 그래프

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="15" y="20" width="250" height="300" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="25" y="40" text-anchor="start" font-size="12" font-weight="bold">Device Tree (h7_nodes.dtsi)</text>
<text x="25" y="62" text-anchor="start" font-size="12">h7-spi {</text>
<text x="39" y="79" text-anchor="start" font-size="12">compatible = "h7,fakectlr";</text>
<text x="39" y="96" text-anchor="start" font-size="12">#address-cells = &lt;1&gt;;</text>
<text x="39" y="113" text-anchor="start" font-size="12">#size-cells = &lt;0&gt;;</text>
<text x="39" y="130" text-anchor="start" font-size="12">cs-gpios = &lt;0&gt;, &lt;&amp;gpio 5 LOW&gt;;</text>
<text x="39" y="164" text-anchor="start" font-size="12">imu@0 {</text>
<text x="46" y="181" text-anchor="start" font-size="12">compatible = "h7,imu";</text>
<text x="46" y="198" text-anchor="start" font-size="12">reg = &lt;0&gt;;</text>
<text x="39" y="215" text-anchor="start" font-size="12">spi-max-frequency = &lt;10000000&gt;;</text>
<text x="46" y="232" text-anchor="start" font-size="12">spi-cpol; spi-cpha;</text>
<text x="39" y="249" text-anchor="start" font-size="12">};</text>
<text x="39" y="266" text-anchor="start" font-size="12">adc@1 { reg = &lt;1&gt;; ... };</text>
<text x="39" y="283" text-anchor="start" font-size="12">legacy@2 { ... "spidev" };</text>
<text x="25" y="300" text-anchor="start" font-size="12">};</text>
<rect x="295" y="20" width="170" height="48" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="380.0" y="39.5" text-anchor="middle" font-size="13">platform_device</text>
<text x="380.0" y="57.5" text-anchor="middle" font-size="12">DT 노드 h7-spi</text>
<rect x="495" y="20" width="170" height="48" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="580.0" y="39.5" text-anchor="middle" font-size="13">platform_driver</text>
<text x="580.0" y="57.5" text-anchor="middle" font-size="12">h7_fakectlr probe()</text>
<line x1="465" y1="44" x2="492" y2="44" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="265" y1="44" x2="292" y2="44" stroke="currentColor" stroke-dasharray="5,4" marker-end="url(#ah2)"/>
<rect x="295" y="100" width="370" height="72" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="480.0" y="122.5" text-anchor="middle" font-size="13">struct spi_controller "spi0"</text>
<text x="480.0" y="140.6" text-anchor="middle" font-size="12">bus_num 0 · num_chipselect 3 · bpw 8, 16</text>
<text x="480.0" y="158.6" text-anchor="middle" font-size="12">자식 노드마다 spi_device 생성</text>
<line x1="580" y1="68" x2="580" y2="97" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="590" y="88" text-anchor="start" font-size="12">register</text>
<rect x="295" y="200" width="112" height="64" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="351.0" y="218.6" text-anchor="middle" font-size="13">spi0.0</text>
<text x="351.0" y="236.6" text-anchor="middle" font-size="12">cs 0 · mode 3</text>
<text x="351.0" y="254.6" text-anchor="middle" font-size="12">10 MHz</text>
<rect x="424" y="200" width="112" height="64" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="480.0" y="218.6" text-anchor="middle" font-size="13">spi0.1</text>
<text x="480.0" y="236.6" text-anchor="middle" font-size="12">cs 1 (GPIO)</text>
<text x="480.0" y="254.6" text-anchor="middle" font-size="12">mode 0, 1 MHz</text>
<rect x="553" y="200" width="112" height="64" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="609.0" y="218.6" text-anchor="middle" font-size="13">spi0.2</text>
<text x="609.0" y="236.6" text-anchor="middle" font-size="12">cs 2</text>
<text x="609.0" y="254.6" text-anchor="middle" font-size="12">"spidev" 포함</text>
<line x1="351" y1="172" x2="351" y2="197" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="480" y1="172" x2="480" y2="197" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="609" y1="172" x2="609" y2="197" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="265" y1="140" x2="292" y2="140" stroke="currentColor" stroke-dasharray="5,4" marker-end="url(#ah2)"/>
<rect x="295" y="300" width="112" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="351.0" y="323.6" text-anchor="middle" font-size="13">h7_imu</text>
<text x="351.0" y="341.6" text-anchor="middle" font-size="12">of_match 매칭</text>
<rect x="424" y="300" width="112" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="480.0" y="323.6" text-anchor="middle" font-size="13">spidev</text>
<text x="480.0" y="341.6" text-anchor="middle" font-size="12">override로</text>
<rect x="553" y="300" width="112" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="609.0" y="323.6" text-anchor="middle" font-size="13">spidev probe</text>
<text x="609.0" y="341.6" text-anchor="middle" font-size="12">-EINVAL 거부</text>
<line x1="351" y1="297" x2="351" y2="267" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="480" y1="297" x2="480" y2="267" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="609" y1="297" x2="609" y2="267" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="15" y="396" text-anchor="start" font-size="12">struct spi_driver (위) → probe(struct spi_device *spi)</text>
<text x="15" y="416" text-anchor="start" font-size="12">매칭 순서: driver_override → OF compatible → ACPI → id_table</text>
<text x="15" y="346" text-anchor="start" font-size="12">reg = 칩 셀렉트 번호, spi-cpol/spi-cpha → spi-&gt;mode</text>
<text x="15" y="366" text-anchor="start" font-size="12">spi-max-frequency → spi-&gt;max_speed_hz</text>
</svg>
```
그림 2 — DT 노드 하나(`h7-spi`)가 platform_device가 되고, 컨트롤러 드라이버가 probe에서 `spi_controller`를 등록하면, 코어가 **자식 노드마다 `spi_device`를 하나씩** 만든다(`of_register_spi_devices`). 그다음은 일반 디바이스 모델(G2)의 매칭이다. 이 그림의 세 갈래(정상 바인딩, override 바인딩, 거부)를 §3.6과 §7에서 모두 실행한다.

---

## 2. SPI 모드 — CPOL/CPHA를 Linux 비트로

Don은 이미 스코프로 수없이 본 내용이라 **Linux 쪽 이름**만 맞추면 된다.

```svg
<svg viewBox="0 0 680 500" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="15" y="24" text-anchor="start" font-size="12">CS</text>
<path d="M200,12 H240 V32 H600 V12 H650" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<text x="420" y="26" text-anchor="middle" font-size="12">CS LOW 구간 = 한 프레임</text>
<text x="15" y="72" text-anchor="start" font-size="13" font-weight="bold">mode 0</text>
<text x="15" y="92" text-anchor="start" font-size="12">CPOL 0 · CPHA 0</text>
<text x="15" y="112" text-anchor="start" font-size="12">상승 엣지에서 샘플</text>
<path d="M200,88 H260 V62 H285 V88 H310 V62 H335 V88 H360 V62 H385 V88 H410 V62 H435 V88 H460 H650" fill="none" stroke="currentColor" stroke-width="1.5"/>
<line x1="260" y1="56" x2="260" y2="106" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="260" cy="75" r="3.5" fill="#3f9a6b"/>
<line x1="310" y1="56" x2="310" y2="106" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="310" cy="75" r="3.5" fill="#3f9a6b"/>
<line x1="360" y1="56" x2="360" y2="106" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="360" cy="75" r="3.5" fill="#3f9a6b"/>
<line x1="410" y1="56" x2="410" y2="106" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="410" cy="75" r="3.5" fill="#3f9a6b"/>
<rect x="240" y="106" width="43" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="261.5" y="120" text-anchor="middle" font-size="12">b7</text>
<rect x="285" y="106" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="309.0" y="120" text-anchor="middle" font-size="12">b6</text>
<rect x="335" y="106" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="359.0" y="120" text-anchor="middle" font-size="12">b5</text>
<rect x="385" y="106" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="409.0" y="120" text-anchor="middle" font-size="12">b4</text>
<text x="470" y="120" text-anchor="start" font-size="12">… (b3~b0 계속)</text>
<text x="470" y="80" text-anchor="start" font-size="12">유휴 레벨 = LOW</text>
<text x="15" y="177" text-anchor="start" font-size="13" font-weight="bold">mode 1</text>
<text x="15" y="197" text-anchor="start" font-size="12">CPOL 0 · CPHA 1</text>
<text x="15" y="217" text-anchor="start" font-size="12">하강 엣지에서 샘플</text>
<path d="M200,193 H260 V167 H285 V193 H310 V167 H335 V193 H360 V167 H385 V193 H410 V167 H435 V193 H460 H650" fill="none" stroke="currentColor" stroke-width="1.5"/>
<line x1="285" y1="161" x2="285" y2="211" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="285" cy="180" r="3.5" fill="#3f9a6b"/>
<line x1="335" y1="161" x2="335" y2="211" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="335" cy="180" r="3.5" fill="#3f9a6b"/>
<line x1="385" y1="161" x2="385" y2="211" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="385" cy="180" r="3.5" fill="#3f9a6b"/>
<line x1="435" y1="161" x2="435" y2="211" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="435" cy="180" r="3.5" fill="#3f9a6b"/>
<rect x="260" y="211" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="284.0" y="225" text-anchor="middle" font-size="12">b7</text>
<rect x="310" y="211" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="334.0" y="225" text-anchor="middle" font-size="12">b6</text>
<rect x="360" y="211" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="384.0" y="225" text-anchor="middle" font-size="12">b5</text>
<rect x="410" y="211" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="434.0" y="225" text-anchor="middle" font-size="12">b4</text>
<text x="470" y="225" text-anchor="start" font-size="12">… (b3~b0 계속)</text>
<text x="470" y="185" text-anchor="start" font-size="12">유휴 레벨 = LOW</text>
<text x="15" y="282" text-anchor="start" font-size="13" font-weight="bold">mode 2</text>
<text x="15" y="302" text-anchor="start" font-size="12">CPOL 1 · CPHA 0</text>
<text x="15" y="322" text-anchor="start" font-size="12">하강 엣지에서 샘플</text>
<path d="M200,272 H260 V298 H285 V272 H310 V298 H335 V272 H360 V298 H385 V272 H410 V298 H435 V272 H460 H650" fill="none" stroke="currentColor" stroke-width="1.5"/>
<line x1="260" y1="266" x2="260" y2="316" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="260" cy="285" r="3.5" fill="#3f9a6b"/>
<line x1="310" y1="266" x2="310" y2="316" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="310" cy="285" r="3.5" fill="#3f9a6b"/>
<line x1="360" y1="266" x2="360" y2="316" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="360" cy="285" r="3.5" fill="#3f9a6b"/>
<line x1="410" y1="266" x2="410" y2="316" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="410" cy="285" r="3.5" fill="#3f9a6b"/>
<rect x="240" y="316" width="43" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="261.5" y="330" text-anchor="middle" font-size="12">b7</text>
<rect x="285" y="316" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="309.0" y="330" text-anchor="middle" font-size="12">b6</text>
<rect x="335" y="316" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="359.0" y="330" text-anchor="middle" font-size="12">b5</text>
<rect x="385" y="316" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="409.0" y="330" text-anchor="middle" font-size="12">b4</text>
<text x="470" y="330" text-anchor="start" font-size="12">… (b3~b0 계속)</text>
<text x="470" y="290" text-anchor="start" font-size="12">유휴 레벨 = HIGH</text>
<text x="15" y="387" text-anchor="start" font-size="13" font-weight="bold">mode 3</text>
<text x="15" y="407" text-anchor="start" font-size="12">CPOL 1 · CPHA 1</text>
<text x="15" y="427" text-anchor="start" font-size="12">상승 엣지에서 샘플</text>
<path d="M200,377 H260 V403 H285 V377 H310 V403 H335 V377 H360 V403 H385 V377 H410 V403 H435 V377 H460 H650" fill="none" stroke="currentColor" stroke-width="1.5"/>
<line x1="285" y1="371" x2="285" y2="421" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="285" cy="390" r="3.5" fill="#3f9a6b"/>
<line x1="335" y1="371" x2="335" y2="421" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="335" cy="390" r="3.5" fill="#3f9a6b"/>
<line x1="385" y1="371" x2="385" y2="421" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="385" cy="390" r="3.5" fill="#3f9a6b"/>
<line x1="435" y1="371" x2="435" y2="421" stroke="#3f9a6b" stroke-dasharray="3,3"/>
<circle cx="435" cy="390" r="3.5" fill="#3f9a6b"/>
<rect x="260" y="421" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="284.0" y="435" text-anchor="middle" font-size="12">b7</text>
<rect x="310" y="421" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="334.0" y="435" text-anchor="middle" font-size="12">b6</text>
<rect x="360" y="421" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="384.0" y="435" text-anchor="middle" font-size="12">b5</text>
<rect x="410" y="421" width="48" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="434.0" y="435" text-anchor="middle" font-size="12">b4</text>
<text x="470" y="435" text-anchor="start" font-size="12">… (b3~b0 계속)</text>
<text x="470" y="395" text-anchor="start" font-size="12">유휴 레벨 = HIGH</text>
<circle cx="22" cy="481" r="3.5" fill="#3f9a6b"/>
<text x="32" y="486" text-anchor="start" font-size="12">= 샘플(읽는) 엣지, 주황 칸 = MOSI/MISO 비트가 유지되는 구간 (반대 엣지에서 바뀜)</text>
</svg>
```
그림 3 — mode 0~3. CPOL은 유휴 클럭 레벨, CPHA는 "첫 엣지에서 샘플(0)인가, 두 번째 엣지에서 샘플(1)인가". mode 0과 3은 둘 다 상승 엣지 샘플이라 많은 칩(대부분의 IMU)이 둘 다 받아 준다. 데이터시트의 "SPI mode 0 and 3 supported"가 그 뜻이다.

| mode | CPOL | CPHA | DT 속성 | `spi->mode` | 예 |
|---|---|---|---|---|---|
| 0 | 0 | 0 | (없음) | `SPI_MODE_0` | 많은 ADC, 플래시 |
| 1 | 0 | 1 | `spi-cpha` | `SPI_MODE_1` | 일부 ADC |
| 2 | 1 | 0 | `spi-cpol` | `SPI_MODE_2` | 드묾 |
| 3 | 1 | 1 | `spi-cpol` + `spi-cpha` | `SPI_MODE_3` | IMU(ICM/MPU 계열), 많은 센서 |

uapi 헤더(`include/uapi/linux/spi/spi.h`)의 정의는 이렇다: `SPI_CPHA = BIT(0)`, `SPI_CPOL = BIT(1)`, `SPI_CS_HIGH = BIT(2)`, `SPI_LSB_FIRST = BIT(3)`, `SPI_3WIRE = BIT(4)`, `SPI_LOOP = BIT(5)`, `SPI_NO_CS = BIT(6)` … `SPI_MOSI_IDLE_HIGH = BIT(18)`. 그래서 `spi->mode` 값 `0x3`은 mode 3이다(예제 6 출력의 `mode 0x3`).

함정 하나: **모드가 틀리면 에러가 나지 않는다.** 선 위의 비트가 반 클럭 밀릴 뿐이다. 이 노트의 가짜 컨트롤러는 그걸 흉내 내서, 규격 밖 모드로 읽으면 값을 1비트 밀어서 돌려준다(§7에서 mode 3으로 ADC를 읽으면 채널 번호까지 틀려진다). Don이 스코프로 "WHO_AM_I가 0x71이 아니라 0x38이나 0xE2로 읽힌다 = 1비트 밀림 = CPHA 틀림"을 진단하던 바로 그 장면이다.

---

## 3. 실습 준비 — SPI 컨트롤러를 직접 만든다

### 3.1 설계

| 대상 | CS | 규격 | 프로토콜 |
|---|---|---|---|
| IMU (MPU-9250 비슷) | CS0, 컨트롤러 내장 CS | mode 3, ≤ 10 MHz, 8-bit | 첫 바이트 = `R/W 비트(bit7) + 주소(bit6..0)`, 이후 바이트 = 데이터, 주소 auto-increment. `WHO_AM_I`(0x75) = 0x71, `ACCEL_XOUT_H`(0x3B)부터 6바이트는 CS가 내려갈 때마다 새 샘플 |
| 12-bit ADC | CS1, **GPIO CS** (PL061 5번 핀, active-low) | mode 0, ≤ 1 MHz, **16-bit 워드** | MOSI `[1 start][ch:3][x:12]`, MISO `[0][ch:3][sample:12]` (실제 칩은 보통 다음 프레임에 결과를 주지만 단순화) |
| legacy | CS2 | 500 kHz | DT에 `"spidev"`를 적은 나쁜 예. 받은 바이트를 그대로 돌려준다 |

컨트롤러는 하드웨어 대신 **CPU가 칩 모델을 실행**하고, 선로 시간(`len × 8 / speed_hz`)만큼 뒤에 hrtimer로 "전송 완료 인터럽트"를 낸다. 진짜 컨트롤러 드라이버의 모양(`transfer_one`이 1을 반환 → IRQ 핸들러가 `spi_finalize_current_transfer`)을 그대로 따른다.

### 3.2 예제 2 — 칩 모델 `h7_fc_model.h`

무엇을 확인하나: CS가 내려갈 때 상태기계를 리셋하고, 바이트마다 MOSI를 받아 MISO를 내는 칩의 동작. Don이 FPGA나 칩 모델로 펌웨어를 검증하던 방식과 같다.

```c
/* h7_fc_model.h — 가짜 컨트롤러 뒤에 매달린 "칩" 두 개의 동작 모델 */
#include <linux/types.h>

#define IMU_WHO_AM_I	0x75	/* MPU-9250 계열: 0x71을 돌려준다 */
#define IMU_ACCEL	0x3b	/* ACCEL_XOUT_H .. ACCEL_ZOUT_L (6바이트) */

struct h7_imu {			/* CS0: IMU, SPI mode 3, 최대 10 MHz */
	u8 regs[128];
	u8 addr;		/* 현재 레지스터 포인터 (auto-increment) */
	bool rd;		/* 이번 CS 구간이 읽기인가 */
	unsigned int idx;	/* CS assert 이후 몇 번째 바이트인가 */
	u8 sample;
};

/* CS가 내려갈 때마다: 상태기계 리셋 + 새 가속도 샘플 래치 */
static void imu_cs_assert(struct h7_imu *m)
{
	int i;

	m->idx = 0;
	m->sample++;
	for (i = 0; i < 6; i++)
		m->regs[IMU_ACCEL + i] = (u8)(m->sample + 0x10 * i);
}

/* 1바이트 교환: 첫 바이트 = 명령(bit7 R/W, bit6..0 주소), 이후 = 데이터 */
static u8 imu_xfer_byte(struct h7_imu *m, u8 mosi)
{
	u8 miso = 0x00;		/* 명령 바이트 동안 MISO는 0 */

	if (m->idx == 0) {
		m->rd = mosi & 0x80;
		m->addr = mosi & 0x7f;
	} else if (m->rd) {
		miso = m->regs[m->addr];
		m->addr = (m->addr + 1) & 0x7f;
	} else {
		if (m->addr != IMU_WHO_AM_I)	/* 읽기 전용 */
			m->regs[m->addr] = mosi;
		m->addr = (m->addr + 1) & 0x7f;
	}
	m->idx++;
	return miso;
}

/* CS1: 12-bit ADC, 16-bit 프레임, mode 0, 최대 1 MHz.
 * MOSI = [1 start][ch:3][x:12], MISO = [0][ch:3][sample:12]
 * (실제 칩은 보통 다음 프레임에 결과를 주지만, 여기선 같은 프레임으로 단순화) */
static u16 adc_xfer_word(u16 mosi, u16 *seq)
{
	unsigned int ch = (mosi >> 12) & 7;

	if (!(mosi & 0x8000))
		return 0x0000;			/* start bit 없음 → 무응답 */
	return (ch << 12) | ((0x400 + ch * 0x155 + (*seq)++ % 8) & 0xfff);
}
```

### 3.3 예제 3 — 가짜 컨트롤러 `h7_fakectlr.c`

네 조각으로 나눠 보이지만 빌드할 때 하나로 이어 붙인다(`build.sh`).

(A) 모듈 파라미터, private 구조체, `set_cs`, `setup`. **`set_cs`의 두 번째 인자는 "선의 전기적 레벨"이다.** 이름이 `enable`이라 헷갈리는데, 코어(`spi_set_cs()`, 6.18 `drivers/spi/spi.c` 1063행)는 active-low CS를 assert할 때 `set_cs(spi, false)`를 부른다. `SPI_CS_HIGH`가 켜진 장치면 반대로 `true`. 즉 컨트롤러 드라이버는 그 값을 **그대로 핀에 쓰면** 된다.

```c
/* h7_fakectlr.c (A) — QEMU virt에 없는 SPI host 컨트롤러를 소프트웨어로 만든다 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/hrtimer.h>
#include <linux/sched/task_stack.h>
#include "h7_fc_model.h"

static bool irq_mode = true;	/* 1: transfer_one이 1 반환, hrtimer가 "완료 IRQ" 흉내 */
module_param(irq_mode, bool, 0644);
static bool verbose = true;	/* CS·transfer 경계를 dmesg에 찍는다 */
module_param(verbose, bool, 0644);
static bool use_dma;		/* 1: can_dma → 코어가 버퍼를 dma_map_sg 한다 */
module_param(use_dma, bool, 0644);
static bool rt;			/* 1: 메시지 펌프 kthread를 SCHED_FIFO로 */
module_param(rt, bool, 0444);

struct h7_fc {
	struct spi_controller *ctlr;
	struct hrtimer done_timer;	/* "전송 완료 인터럽트" 역할 */
	struct h7_imu imu;
	u16 adc_seq;
	u8 last_lsb;			/* mode 불일치 시 비트 밀림 흉내 */
};

/* 코어가 넘기는 level은 "선의 전기적 레벨"이다 (true = HIGH) */
static void h7_set_cs(struct spi_device *spi, bool level)
{
	struct h7_fc *fc = spi_controller_get_devdata(spi->controller);
	u8 cs = spi_get_chipselect(spi, 0);
	bool asserted = (spi->mode & SPI_CS_HIGH) ? level : !level;

	if (verbose)
		dev_info(&spi->controller->dev, "CS%u line %s (%s)\n", cs,
			 level ? "HIGH" : "LOW", asserted ? "assert" : "deassert");
	if (asserted && cs == 0)
		imu_cs_assert(&fc->imu);
}

static int h7_setup(struct spi_device *spi)
{
	dev_info(&spi->controller->dev, "setup %s: cs%u mode %lu%s %u Hz bpw %u\n",
		 dev_name(&spi->dev), spi_get_chipselect(spi, 0),
		 spi->mode & SPI_MODE_X_MASK,
		 (spi->mode & SPI_CS_HIGH) ? " cs-high" : "",
		 spi->max_speed_hz, spi->bits_per_word);
	return 0;
}
```

(B) 규격 검사(모드·속도·워드 길이가 칩과 안 맞으면 1비트 밀린 값을 돌려준다), 스택 버퍼 검사(§8), `prepare_transfer_hardware`, `can_dma`.

```c
/* h7_fakectlr.c (B) — 스펙 검사·스택 검사·펌프 훅 */
static bool h7_in_spec(struct spi_device *spi, struct spi_transfer *t)
{
	u8 cs = spi_get_chipselect(spi, 0);
	u32 mode = spi->mode & SPI_MODE_X_MASK;
	bool ok = true;

	if (cs == 0)
		ok = mode == SPI_MODE_3 && t->speed_hz <= 10000000 && t->bits_per_word == 8;
	else if (cs == 1)
		ok = mode == SPI_MODE_0 && t->speed_hz <= 1000000 && t->bits_per_word == 16;
	if (!ok)
		dev_warn(&spi->controller->dev, "cs%u: mode %u %u Hz bpw %u out of spec -> garbage\n",
			 cs, mode, t->speed_hz, t->bits_per_word);
	return ok;
}

static void h7_stack_check(struct device *d, const char *what, const void *buf, bool mapped)
{
	if (buf && object_is_on_stack(buf))
		dev_warn(d, "%s is ON STACK (vmalloc=%d virt_addr_valid=%d dma-mapped=%d)\n",
			 what, is_vmalloc_addr(buf), virt_addr_valid(buf), mapped);
}

static int h7_prep_hw(struct spi_controller *ctlr)
{
	if (verbose)
		dev_info(&ctlr->dev, "prepare_transfer_hardware (pump busy)\n");
	return 0;
}

static bool h7_can_dma(struct spi_controller *ctlr, struct spi_device *spi,
		       struct spi_transfer *t)
{
	return use_dma && t->len >= 4;
}
```

(C) `transfer_one` — 이 컨트롤러의 심장. 16-bit 워드면 버퍼를 **CPU 엔디언 `u16` 배열**로 다룬다는 점을 보자. 선 위에서는 MSB 먼저 나가지만, 메모리에서는 리틀 엔디언 `u16`이다(§7의 trace에서 `tx=[00-80]`으로 보인다).

```c
/* h7_fakectlr.c (C) — transfer_one: 칩 모델 실행 + 선로 시간 뒤 완료 */
static int h7_transfer_one(struct spi_controller *ctlr, struct spi_device *spi,
			   struct spi_transfer *t)
{
	struct h7_fc *fc = spi_controller_get_devdata(ctlr);
	u8 cs = spi_get_chipselect(spi, 0);
	bool ok = h7_in_spec(spi, t);
	unsigned int i;

	h7_stack_check(&ctlr->dev, "tx_buf", t->tx_buf, t->tx_sg_mapped);
	h7_stack_check(&ctlr->dev, "rx_buf", t->rx_buf, t->rx_sg_mapped);
	if (cs == 1 && t->bits_per_word == 16) {	/* 16-bit 워드: 버퍼는 CPU 엔디언 u16 */
		const u16 *tx = t->tx_buf;
		u16 *rx = t->rx_buf;

		for (i = 0; i < t->len / 2; i++) {
			u16 v = adc_xfer_word(tx ? tx[i] : 0, &fc->adc_seq);

			if (rx)
				rx[i] = ok ? v : v >> 1;
		}
	} else {
		const u8 *tx = t->tx_buf;
		u8 *rx = t->rx_buf;

		for (i = 0; i < t->len; i++) {
			u8 mosi = tx ? tx[i] : 0, v;

			v = cs == 0 ? imu_xfer_byte(&fc->imu, mosi) : (cs == 2 ? mosi : 0xff);
			if (!ok) {				/* 엣지가 틀리면 1비트 밀린다 */
				u8 lsb = v & 1;

				v = (v >> 1) | (fc->last_lsb << 7);
				fc->last_lsb = lsb;
			}
			if (rx)
				rx[i] = v;
		}
	}
	if (verbose)
		dev_info(&ctlr->dev, "  xfer cs%u len %u%s%s %u Hz bpw %u%s\n", cs, t->len,
			 t->tx_buf ? " tx" : "", t->rx_buf ? " rx" : "", t->speed_hz,
			 t->bits_per_word, t->cs_change ? " cs_change" : "");
	if (!irq_mode)
		return 0;			/* 0 = 이미 끝났음 */
	hrtimer_start(&fc->done_timer,		/* 선로 시간 = len*8 / speed */
		      ns_to_ktime(div_u64((u64)t->len * 8 * NSEC_PER_SEC, t->speed_hz)),
		      HRTIMER_MODE_REL);
	return 1;				/* 1 = 진행 중, 완료는 나중에 통지 */
}
```

(D) 완료 통지와 probe. `transfer_one`이 **1을 반환하면** 코어는 "진행 중"으로 알고 기다린다. hrtimer 콜백(hardirq 문맥)이 `spi_finalize_current_transfer()`를 부르면 코어의 `complete()`가 풀린다. 진짜 드라이버라면 이 자리가 SPI 블록의 "TX FIFO empty / RX done" 인터럽트 핸들러다.

```c
/* h7_fakectlr.c (D) — 완료 통지(가짜 IRQ)와 probe: spi_controller 채워서 등록 */
static enum hrtimer_restart h7_done(struct hrtimer *h)	/* hardirq 문맥 */
{
	struct h7_fc *fc = container_of(h, struct h7_fc, done_timer);

	spi_finalize_current_transfer(fc->ctlr);	/* 진짜 드라이버의 IRQ 핸들러가 하는 일 */
	return HRTIMER_NORESTART;
}

static void h7_cancel(void *data)
{
	hrtimer_cancel(data);
}

static int h7_probe(struct platform_device *pdev)
{
	struct spi_controller *ctlr;
	struct h7_fc *fc;
	int ret;

	ctlr = devm_spi_alloc_host(&pdev->dev, sizeof(*fc));
	if (!ctlr)
		return -ENOMEM;
	fc = spi_controller_get_devdata(ctlr);
	fc->ctlr = ctlr;
	fc->imu.regs[IMU_WHO_AM_I] = 0x71;
	hrtimer_setup(&fc->done_timer, h7_done, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ret = devm_add_action_or_reset(&pdev->dev, h7_cancel, &fc->done_timer);
	if (ret)
		return ret;

	device_set_node(&ctlr->dev, dev_fwnode(&pdev->dev));	/* 자식 노드 = spi_device */
	ctlr->num_chipselect = 3;
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;	/* LSB_FIRST·3WIRE 미지원 */
	ctlr->bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(16);
	ctlr->min_speed_hz = 100000;
	ctlr->max_speed_hz = 20000000;
	ctlr->use_gpio_descriptors = true;			/* DT cs-gpios 지원 */
	ctlr->rt = rt;
	ctlr->setup = h7_setup;
	ctlr->set_cs = h7_set_cs;
	ctlr->prepare_transfer_hardware = h7_prep_hw;
	ctlr->transfer_one = h7_transfer_one;
	ctlr->can_dma = h7_can_dma;
	return devm_spi_register_controller(&pdev->dev, ctlr);
}

static const struct of_device_id h7_fc_of[] = { { .compatible = "h7,fakectlr" }, { } };
MODULE_DEVICE_TABLE(of, h7_fc_of);
static struct platform_driver h7_fc_drv = {
	.probe = h7_probe,
	.driver = { .name = "h7_fakectlr", .of_match_table = h7_fc_of },
};
module_platform_driver(h7_fc_drv);
MODULE_DESCRIPTION("H7: fake SPI host controller emulating an IMU (CS0) and ADC (CS1)");
MODULE_LICENSE("GPL");
```

### 3.4 예제 4 — DT 노드와 dtb

무엇을 확인하나: `virt.dts` 사본에 컨트롤러 노드와 자식 세 개를 넣고 컴파일한다. PL061 GPIO에 label을 붙여 `cs-gpios`에서 참조한다(G3과 같은 방법).

```dts
/* h7_nodes.dtsi — virt.dts 사본 끝에 #include: 가짜 SPI 컨트롤러 + 자식 장치 3개 */
#include <dt-bindings/gpio/gpio.h>

/ {
	h7_spi: h7-spi {
		compatible = "h7,fakectlr";
		#address-cells = <1>;		/* 자식 reg = 칩 셀렉트 번호 */
		#size-cells = <0>;
		cs-gpios = <0>, <&gpio 5 GPIO_ACTIVE_LOW>;	/* CS0 = 컨트롤러 내장, CS1 = GPIO */

		imu@0 {
			compatible = "h7,imu";
			reg = <0>;
			spi-max-frequency = <10000000>;
			spi-cpol;			/* mode 3 = CPOL 1 + CPHA 1 */
			spi-cpha;
		};

		adc@1 {
			compatible = "h7,adc12";	/* 커널 드라이버 없음 → spidev로 붙일 예정 */
			reg = <1>;
			spi-max-frequency = <1000000>;	/* mode 0 */
		};

		legacy@2 {
			compatible = "rohm,dh2228fv", "spidev";	/* 나쁜 예: "spidev"를 DT에 적음 */
			reg = <2>;
			spi-max-frequency = <500000>;
		};
	};
};
```

```sh
#!/bin/sh
# mkdtb.sh → /var/tmp/H7/h7.dtb
set -e
W=/var/tmp/H7; K=/var/lab/src/linux-6.18.55/include
mkdir -p $W
sed -e 's/^\tpl061@9030000 {/\tgpio: pl061@9030000 {/' /var/lab/out/virt.dts > $W/h7_virt.dts
echo '#include "/Users/donh/.elinux_scratch/H7/h7_nodes.dtsi"' >> $W/h7_virt.dts
cpp -nostdinc -I $K -undef -x assembler-with-cpp $W/h7_virt.dts > $W/h7.pp.dts
dtc -I dts -O dtb -o $W/h7.dtb $W/h7.pp.dts 2>&1 | grep -i "h7-spi\|imu@\|adc@\|legacy@" || true
fdtget -t s $W/h7.dtb /h7-spi/imu@0 compatible
fdtget -p $W/h7.dtb /h7-spi/imu@0
```

```sh
sh /Users/donh/.elinux_scratch/H7/mkdtb.sh
```

```text
h7,imu
compatible
reg
spi-max-frequency
spi-cpol
spi-cpha
```

출력에서 볼 것: 우리 노드에 대한 dtc 경고는 없었다(grep으로 우리 노드 이름이 들어간 경고만 걸렀는데 빈 결과). `fdtget -p`로 `imu@0`의 속성 다섯 개가 dtb에 들어간 것을 확인했다. `cs-gpios = <0>, <&gpio 5 ...>`의 `<0>`은 "CS0은 GPIO가 아니라 컨트롤러 내장 CS"라는 표준 표기다.

### 3.5 예제 5 — 빌드·부팅 스크립트

```sh
#!/bin/sh
# build.sh — 소스를 /var/tmp/H7/mod 로 모아 디버그 랩 커널(6.18.55-dbg)에 대해 빌드
set -e
S=/Users/donh/.elinux_scratch/H7; M=/var/tmp/H7/mod
mkdir -p $M && cd $M
cp $S/h7_fc_model.h $S/Kbuild .
cat $S/h7_fc_a.c $S/h7_fc_b.c $S/h7_fc_c.c $S/h7_fc_d.c > h7_fakectlr.c
cat $S/h7_imu_a.c $S/h7_imu_b.c $S/h7_imu_c.c $S/h7_imu_d.c $S/h7_imu_e.c > h7_imu.c
make -C /var/lab/out/linux-build-dbg M=$M -j2 modules 2>&1 | grep -v "^make"
```

```make
obj-m += h7_fakectlr.o h7_imu.o
```

```sh
#!/bin/sh
# boot.sh <게스트 스크립트> [추가 커널 인자] — 디버그 커널 + h7.dtb + 모듈·spidev.ko·테스트 프로그램
set -e
W=/var/tmp/H7; S=/Users/donh/.elinux_scratch/H7
rm -rf $W/r && mkdir $W/r && cp -a /var/lab/build/rootfs/. $W/r/
cp $W/mod/*.ko /var/lab/build/linux-dbg/drivers/spi/spidev.ko $W/h7_spidev_adc $S/$1 $W/r/
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout ${TMO:-180} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M -nographic \
  -kernel /var/lab/out/Image-dbg -initrd $W/i.gz -dtb $W/h7.dtb \
  -append "console=ttyAMA0 quiet $2 LAB_CMD=sh~/$1"
```

```sh
sh /Users/donh/.elinux_scratch/H7/build.sh
```

```text
  CC [M]  h7_fakectlr.o
  CC [M]  h7_imu.o
  MODPOST Module.symvers
  CC [M]  h7_fakectlr.mod.o
  CC [M]  .module-common.o
  CC [M]  h7_imu.mod.o
  LD [M]  h7_imu.ko
  LD [M]  h7_fakectlr.ko
```

경고 0개다. 디버그 커널(`vermagic 6.18.55-dbg`)에 넣을 모듈이므로 `/var/lab/out/linux-build-dbg`에 대해 빌드했다. 유저 프로그램(§7)은 VM에서 `gcc -std=gnu11 -Wall -Wextra -O2 -static`으로 빌드해 initramfs에 넣었다(경고 0개).

### 3.6 예제 6 — 컨트롤러 등록 → 자식 장치 → 바인딩

무엇을 확인하나: 컨트롤러 모듈 하나를 넣는 순간 DT 자식 노드 세 개가 `spi_device`가 되는가, 각각 어떤 드라이버에 붙는가.

```sh
# t1: 컨트롤러 등록 → 자식 spi_device 생성 → 클라이언트 바인딩
dmesg -n 1; uname -r
insmod /h7_fakectlr.ko
echo "--- after controller"
ls -1 /sys/class/spi_master/ /sys/bus/spi/devices/
for d in /sys/bus/spi/devices/*; do echo "$(basename $d): modalias=$(cat $d/modalias) of_node=$(basename $(readlink $d/of_node))"; done
insmod /spidev.ko; insmod /h7_imu.ko
echo "--- dmesg"
dmesg | grep -E "h7|spi" | sed 's/^\[[ 0-9.]*\] //'
echo "--- bound drivers"
for d in /sys/bus/spi/devices/*; do echo "$(basename $d) -> $(basename "$(readlink $d/driver)")"; done
```

```sh
sh /Users/donh/.elinux_scratch/H7/boot.sh t1.sh
```

```text
6.18.55-dbg
--- after controller
/sys/bus/spi/devices/:
spi0.0
spi0.1
spi0.2

/sys/class/spi_master/:
spi0
spi0.0: modalias=spi:imu of_node=imu@0
spi0.1: modalias=spi:adc12 of_node=adc@1
spi0.2: modalias=spi:dh2228fv of_node=legacy@2
--- dmesg
h7_fakectlr: loading out-of-tree module taints kernel.
spi_master spi0: setup spi0.0: cs0 mode 3 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
spi_master spi0: setup spi0.1: cs1 mode 0 1000000 Hz bpw 8
spi_master spi0: setup spi0.2: cs2 mode 0 500000 Hz bpw 8
spi_master spi0: CS2 line HIGH (deassert)
spidev spi0.2: spidev listed directly in DT is not supported
spidev spi0.2: probe with driver spidev failed with error -22
h7_imu spi0.0: probe: bus 0 cs0 mode 0x3 max 10000000 Hz bpw 8 modalias imu
spi_master spi0: prepare_transfer_hardware (pump busy)
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 1 rx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
spi_master spi0: prepare_transfer_hardware (pump busy)
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 6 rx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
h7_imu spi0.0: WHO_AM_I=0x71 accel=02 12 22 32 42 52
--- bound drivers
spi0.0 -> h7_imu
spi0.1 -> 
spi0.2 -> 
```

(`ls`의 색상 escape 코드만 지웠다.) 출력에서 볼 것:

- 장치 이름은 `spi<bus_num>.<cs>`다. `bus_num = -1`로 두었더니 코어가 0번을 줬다. DT에 `aliases { spi3 = &h7_spi; }`가 있으면 3번이 된다.
- modalias는 compatible에서 **vendor 접두어를 뗀 것**이다(`h7,imu` → `spi:imu`). §1.5의 `id_table` 규칙이 여기서 나온다.
- 코어는 `spi_device`를 등록하면서 바로 `spi_setup()`을 부른다(`setup spi0.0: cs0 mode 3 ...`). 이때 CS를 **deassert 상태로 한 번 강제로** 맞춘다(`CS0 line HIGH`). CS1은 GPIO CS라 우리 `set_cs`가 불리지 않았다 — 코어가 GPIO를 직접 움직인다(§7 trace의 `gpio_value`).
- `spi0.2`에는 `"rohm,dh2228fv", "spidev"`가 적혀 있어 spidev가 매칭되었지만, spidev 스스로 **"DT에 spidev를 직접 적지 말라"**며 `-EINVAL`로 거부했다(§7.1).
- `spi_w8r8()`은 transfer 2개(1바이트 tx, 1바이트 rx)짜리 메시지 하나다. CS는 메시지 전체 동안 한 번 내려갔다 올라간다. 가속도 값 `02 12 ...`의 첫 바이트가 2인 것은 CS가 두 번째로 내려갈 때 래치된 샘플이라서다.

---

## 4. 메시지와 트랜스퍼 — "CS 한 번에 무엇을 보내나"

### 4.1 직관

- **`spi_transfer`** = 버퍼 한 쌍(`tx_buf`, `rx_buf`)과 길이. "CS가 잡혀 있는 동안 이만큼 클럭을 돌린다"의 한 조각. `tx_buf`가 NULL이면 0을 보내고(더미), `rx_buf`가 NULL이면 받은 것을 버린다.
- **`spi_message`** = transfer들의 리스트. **기본적으로 메시지 처음에 CS를 내리고 끝에 올린다.** 메시지 하나가 원자적 단위다 — 같은 버스의 다른 장치 메시지가 중간에 끼어들지 않는다.
- **`cs_change`** = "이 transfer 뒤에서 CS를 평소와 반대로" 하라는 표시. 중간 transfer에 붙이면 거기서 CS를 잠깐 올렸다 다시 내린다(명령 두 개를 한 메시지로). **마지막 transfer**에 붙이면 메시지가 끝나도 CS를 **내린 채로 둔다**(다음 메시지와 이어 붙이는 특수 용도 — 같은 버스를 쓰는 다른 장치를 막으므로 거의 안 쓴다).

```svg
<svg viewBox="0 0 680 485" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="15" y="24" text-anchor="start" font-size="13" font-weight="bold">(A) t[1].cs_change = 1</text>
<text x="15" y="59" text-anchor="start" font-size="12">CS0</text>
<text x="15" y="99" text-anchor="start" font-size="12">SCLK</text>
<text x="15" y="139" text-anchor="start" font-size="12">MOSI</text>
<text x="15" y="179" text-anchor="start" font-size="12">MISO</text>
<path d="M70,42 H130 V66 H250 V42 H285 V66 H560 V42 H660" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<path d="M140,104 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M190,104 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M300,104 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M350,104 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M384,104 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M418,104 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M452,104 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M486,104 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M520,104 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<rect x="140" y="123" width="32" height="22" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="156.0" y="138" text-anchor="middle" font-size="12">F5</text>
<rect x="190" y="163" width="32" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="206.0" y="178" text-anchor="middle" font-size="12">71</text>
<rect x="300" y="123" width="32" height="22" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="316.0" y="138" text-anchor="middle" font-size="12">BB</text>
<rect x="350" y="163" width="32" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="366.0" y="178" text-anchor="middle" font-size="12">02</text>
<rect x="384" y="163" width="32" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="400.0" y="178" text-anchor="middle" font-size="12">12</text>
<rect x="418" y="163" width="32" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="434.0" y="178" text-anchor="middle" font-size="12">22</text>
<rect x="452" y="163" width="32" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="468.0" y="178" text-anchor="middle" font-size="12">32</text>
<rect x="486" y="163" width="32" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="502.0" y="178" text-anchor="middle" font-size="12">42</text>
<rect x="520" y="163" width="32" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="536.0" y="178" text-anchor="middle" font-size="12">52</text>
<path d="M140,196 V202 H172 V196" fill="none" stroke="currentColor"/>
<text x="156.0" y="217" text-anchor="middle" font-size="12">t[0]</text>
<path d="M190,196 V202 H222 V196" fill="none" stroke="currentColor"/>
<text x="206.0" y="217" text-anchor="middle" font-size="12">t[1]</text>
<path d="M300,196 V202 H332 V196" fill="none" stroke="currentColor"/>
<text x="316.0" y="217" text-anchor="middle" font-size="12">t[2]</text>
<path d="M350,196 V202 H552 V196" fill="none" stroke="currentColor"/>
<text x="451.0" y="217" text-anchor="middle" font-size="12">t[3]</text>
<text x="380" y="24" text-anchor="start" font-size="12">t[1] 뒤 CS 토글 → 명령 2개로 해석</text>
<text x="15" y="254" text-anchor="start" font-size="13" font-weight="bold">(B) cs_change = 0 (기본값)</text>
<text x="15" y="289" text-anchor="start" font-size="12">CS0</text>
<text x="15" y="329" text-anchor="start" font-size="12">SCLK</text>
<text x="15" y="369" text-anchor="start" font-size="12">MOSI</text>
<text x="15" y="409" text-anchor="start" font-size="12">MISO</text>
<path d="M70,272 H130 V296 H560 V272 H660" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<path d="M140,334 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M190,334 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M300,334 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M350,334 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M384,334 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M418,334 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M452,334 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M486,334 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<path d="M520,334 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20 h4 v-20 h4 v20" fill="none" stroke="currentColor" stroke-width="1"/>
<rect x="140" y="353" width="32" height="22" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="156.0" y="368" text-anchor="middle" font-size="12">F5</text>
<rect x="190" y="393" width="32" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="206.0" y="408" text-anchor="middle" font-size="12">71</text>
<rect x="300" y="353" width="32" height="22" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="316.0" y="368" text-anchor="middle" font-size="12">BB</text>
<rect x="350" y="393" width="32" height="22" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="366.0" y="408" text-anchor="middle" font-size="12">00</text>
<rect x="384" y="393" width="32" height="22" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="400.0" y="408" text-anchor="middle" font-size="12">00</text>
<rect x="418" y="393" width="32" height="22" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="434.0" y="408" text-anchor="middle" font-size="12">00</text>
<rect x="452" y="393" width="32" height="22" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="468.0" y="408" text-anchor="middle" font-size="12">00</text>
<rect x="486" y="393" width="32" height="22" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="502.0" y="408" text-anchor="middle" font-size="12">00</text>
<rect x="520" y="393" width="32" height="22" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="536.0" y="408" text-anchor="middle" font-size="12">00</text>
<path d="M140,426 V432 H172 V426" fill="none" stroke="currentColor"/>
<text x="156.0" y="447" text-anchor="middle" font-size="12">t[0]</text>
<path d="M190,426 V432 H222 V426" fill="none" stroke="currentColor"/>
<text x="206.0" y="447" text-anchor="middle" font-size="12">t[1]</text>
<path d="M300,426 V432 H332 V426" fill="none" stroke="currentColor"/>
<text x="316.0" y="447" text-anchor="middle" font-size="12">t[2]</text>
<path d="M350,426 V432 H552 V426" fill="none" stroke="currentColor"/>
<text x="451.0" y="447" text-anchor="middle" font-size="12">t[3]</text>
<text x="380" y="254" text-anchor="start" font-size="12">CS 계속 LOW → BB도 데이터 취급</text>
<text x="267" y="34" text-anchor="middle" font-size="12">cs_change</text>
<text x="15" y="474" text-anchor="start" font-size="12">spi_message 하나 = spi_sync 한 번. 기본적으로 메시지 처음부터 끝까지 CS를 잡고 있다.</text>
</svg>
```
그림 4 — 예제 7에서 실제로 보낸 메시지(transfer 4개)의 파형. (A)는 `t[1].cs_change = 1`이라 CS가 한 번 토글되고, IMU는 `BB`를 새 명령으로 해석해 가속도 6바이트를 돌려준다. (B)는 CS가 메시지 내내 LOW라 IMU 입장에서는 "0x75부터 읽기를 계속하는 중"이고, `BB`는 버려지는 더미 바이트, 뒤의 6바이트는 0x77부터의 (빈) 레지스터다.

### 4.2 `struct spi_transfer`의 주요 필드

| 필드 | 뜻 | 기본 |
|---|---|---|
| `tx_buf` / `rx_buf` | 보낼/받을 버퍼. **DMA-safe여야 한다** (§8) | NULL = 더미 송신 / 버림 |
| `len` | 바이트 수 (16-bit 워드면 2의 배수) | — |
| `speed_hz` | 이 transfer만의 클럭. 0이면 `spi->max_speed_hz` | 0 |
| `bits_per_word` | 이 transfer만의 워드 길이. 0이면 `spi->bits_per_word` | 0 |
| `cs_change` | 이 transfer 뒤 CS 동작을 뒤집는다 (§4.1) | 0 |
| `cs_change_delay` | `cs_change`로 CS가 올라가 있는 시간 | 0이면 코어가 10 µs (6.18 `spi.c` 1561행) |
| `delay` | 이 transfer 뒤 다음 transfer(또는 CS 해제) 전 대기 | 0 |
| `word_delay` | 워드 사이 대기 (느린 칩) | 0 |
| `cs_off` | 이 transfer 동안 CS를 아예 올린다 | 0 |
| `tx_nbits` / `rx_nbits` | 1/2/4/8선 (듀얼·쿼드) | 1 |
| `effective_speed_hz` | 컨트롤러가 실제로 낸 속도를 적어 주는 칸 | — |

`speed_hz`와 `bits_per_word`를 0으로 두면 코어가 `__spi_validate()`에서 장치 기본값으로 채운다. 그래서 컨트롤러의 `transfer_one`에서는 항상 0이 아닌 값이 보인다(예제 6 출력의 `10000000 Hz bpw 8`).

### 4.3 예제 7 — 클라이언트 드라이버 `h7_imu` (A): 헬퍼와 `cs_change`

무엇을 확인하나: `spi_w8r8`, `spi_write_then_read`, 그리고 transfer 4개짜리 메시지에서 `cs_change`의 효과.

```c
/* h7_imu.c (A) — IMU 프로토콜 드라이버: 어떤 SPI 컨트롤러 위에서도 같은 코드 */
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/completion.h>
#include <linux/sched.h>

#define REG_WHO_AM_I	0x75
#define REG_ACCEL	0x3b
#define RD(reg)		(0x80 | (reg))		/* 읽기 = 주소 MSB 1 */

struct h7_imu {
	struct spi_device *spi;
	struct regmap *map;
	struct dentry *dbg;
	u8 *tx, *rx;		/* kmalloc 버퍼 → DMA-safe */
};

static int h7_basic(struct h7_imu *im)
{
	struct spi_device *spi = im->spi;
	u8 cmd = RD(REG_ACCEL), acc[6];
	int who, ret;

	who = spi_w8r8(spi, RD(REG_WHO_AM_I));		/* 1B 쓰고 1B 읽기 */
	if (who < 0)
		return who;
	/* 스택 버퍼여도 OK: spi_write_then_read는 내부 DMA-safe 버퍼로 복사한다 */
	ret = spi_write_then_read(spi, &cmd, 1, acc, sizeof(acc));
	if (ret)
		return ret;
	dev_info(&spi->dev, "WHO_AM_I=0x%02x accel=%6ph\n", who, acc);
	return 0;
}

/* 한 메시지에 "레지스터 읽기" 두 번 — 사이에서 CS를 한 번 올려야 한다 */
static int h7_two_reads(struct h7_imu *im, bool cs_change)
{
	struct spi_transfer t[4] = {
		{ .tx_buf = &im->tx[0], .len = 1 },		/* RD(WHO_AM_I) */
		{ .rx_buf = &im->rx[0], .len = 1, .cs_change = cs_change },
		{ .tx_buf = &im->tx[1], .len = 1 },		/* RD(ACCEL) */
		{ .rx_buf = &im->rx[1], .len = 6 },
	};
	struct spi_message m;
	int ret;

	im->tx[0] = RD(REG_WHO_AM_I);
	im->tx[1] = RD(REG_ACCEL);
	memset(im->rx, 0, 8);
	spi_message_init_with_transfers(&m, t, ARRAY_SIZE(t));
	ret = spi_sync(im->spi, &m);
	dev_info(&im->spi->dev, "cs_change=%d ret=%d actual_length=%u who=0x%02x accel=%6ph\n",
		 cs_change, ret, m.actual_length, im->rx[0], &im->rx[1]);
	return ret;
}
```

뒷부분(probe, debugfs 명령 `cmd`)은 §6, §8에서 보인다. 게스트 스크립트 `t2.sh`는 debugfs에 명령을 써서 각 실험을 부른다.

```sh
# t2: cs_change 유무, regmap, 스택 버퍼, spi_async 콜백 문맥
dmesg -n 1; insmod /h7_fakectlr.ko && insmod /h7_imu.ko; dmesg -c >/dev/null
C=/sys/kernel/debug/h7_imu/cmd
for c in "cs 1" "cs 0" "regmap" "async 4"; do
  dmesg -c >/dev/null; echo "### $c"; echo "$c" > $C; dmesg | sed 's/^\[[ 0-9.]*\] //'
done
echo 1 > /sys/module/h7_fakectlr/parameters/use_dma
dmesg -c >/dev/null; echo "### stack (use_dma=1)"; echo stack > $C; dmesg | sed 's/^\[[ 0-9.]*\] //'
```

```sh
sh /Users/donh/.elinux_scratch/H7/boot.sh t2.sh
```

```text
### cs 1
spi_master spi0: prepare_transfer_hardware (pump busy)
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 1 rx 10000000 Hz bpw 8 cs_change
spi_master spi0: CS0 line HIGH (deassert)
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 6 rx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
h7_imu spi0.0: cs_change=1 ret=0 actual_length=9 who=0x71 accel=04 14 24 34 44 54
### cs 0
spi_master spi0: prepare_transfer_hardware (pump busy)
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 1 rx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 6 rx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
h7_imu spi0.0: cs_change=0 ret=0 actual_length=9 who=0x71 accel=00 00 00 00 00 00
```

출력에서 볼 것: 컨트롤러 쪽 로그가 곧 그림 4다. `cs_change=1`이면 transfer 경계에서 `CS0 line HIGH` → `LOW`가 끼고, IMU는 두 번째 명령을 받아 새 샘플(`04 14 ...`)을 준다. `cs_change=0`이면 같은 9바이트를 보냈고 `ret=0`, `actual_length=9`로 **"성공"**했지만 데이터는 전부 0이다. 코어는 프로토콜을 모른다 — CS 경계 설계는 드라이버 몫이다. 이 실수는 에러 코드로 절대 안 잡히고, 로직 분석기나 이런 trace로만 잡힌다.

### 4.4 동기 헬퍼 한눈에

| 함수 | 하는 일 | 버퍼 | 언제 |
|---|---|---|---|
| `spi_sync(spi, msg)` | 메시지 전송, 끝날 때까지 슬립 | 호출자 버퍼 (DMA-safe 필수) | 기본 |
| `spi_sync_transfer(spi, xfers, n)` | transfer 배열로 메시지를 만들어 `spi_sync` | 호출자 | 짧은 고정 시퀀스 |
| `spi_write(spi, buf, len)` / `spi_read(...)` | transfer 1개 | 호출자 | 단방향 블록 |
| `spi_write_then_read(spi, tx, ntx, rx, nrx)` | tx 후 rx, CS 한 번. **내부 버퍼로 복사** | 스택 버퍼도 OK | 레지스터 읽기 (작은 크기) |
| `spi_w8r8(spi, cmd)` | 1바이트 쓰고 1바이트 읽어 반환 | 내부 | 레지스터 1개 |
| `spi_w8r16(spi, cmd)` / `spi_w8r16be` | 1바이트 쓰고 16비트 읽기 (wire 순서 / big-endian→CPU) | 내부 | 16비트 레지스터 |
| `spi_async(spi, msg)` | 큐에 넣고 즉시 반환, 끝나면 `msg->complete(context)` | 호출자 (완료까지 살아 있어야 함) | 스트리밍, atomic 문맥 |

`spi_write_then_read()`가 "스택 버퍼도 OK"인 이유는 소스에 있다(6.18 `drivers/spi/spi.c` 4699~4746행).

```sh
grep -n "#define	SPI_BUFSIZ" drivers/spi/spi.c
grep -n "^int spi_write_then_read" -A 30 drivers/spi/spi.c | grep -n "SPI_BUFSIZ\|kmalloc\|mutex_trylock"
```

```text
4699:#define	SPI_BUFSIZ	max(32, SMP_CACHE_BYTES)
18:4740-	if ((n_tx + n_rx) > SPI_BUFSIZ || !mutex_trylock(&lock)) {
19:4741-		local_buf = kmalloc(max((unsigned)SPI_BUFSIZ, n_tx + n_rx),
```

출력에서 볼 것: 전역 정적 버퍼(`SPI_BUFSIZ`, 캐시라인 크기 이상)를 mutex로 빌려 쓰고, 크거나 누가 쓰는 중이면 `kmalloc`한다. 어느 쪽이든 **사용자의 버퍼에서 memcpy**하므로 호출자 버퍼는 DMA와 무관하다. 대신 복사 비용과 전역 mutex 경쟁이 있어서 큰 블록이나 고속 경로에는 쓰지 않는다.

---

## 5. `spi_sync` vs `spi_async` — 메시지 펌프 속으로

### 5.1 직관

SPI core의 컨트롤러마다 **메시지 큐**와 그것을 비우는 **펌프**(kthread_worker, 이름은 컨트롤러 이름 `spi0`)가 있다. RTOS로 치면 "SPI 드라이버 태스크 + 요청 큐"다.

- `spi_async()`는 큐에 넣고 **바로 반환**한다. 큐 조작은 spinlock(`irqsave`)으로만 하므로 **atomic 문맥(hardirq 포함)에서 불러도 된다**. 완료되면 `msg->complete(msg->context)`가 불린다.
- `spi_sync()`는 끝날 때까지 **슬립**한다. 그래서 process 문맥(또는 threaded IRQ)에서만 부른다. 6.x에는 지름길이 있다: **큐가 비어 있으면** kthread를 깨우지 않고 **호출자 스레드에서 직접** 메시지를 돌린다(`__spi_transfer_message_noqueue`). 문맥 전환 두 번을 아끼는 최적화다.

```svg
<svg viewBox="0 0 680 500" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" text-anchor="start" font-size="13" font-weight="bold">spi_sync — 호출자 스레드 (슬립 가능)</text>
<text x="360" y="22" text-anchor="start" font-size="13" font-weight="bold">spi_async — 즉시 반환 (atomic OK)</text>
<rect x="20" y="34" width="300" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170.0" y="52.5" text-anchor="middle" font-size="13">spi_sync(spi, &amp;msg)</text>
<text x="170.0" y="70.5" text-anchor="middle" font-size="12">statistics: spi_sync++</text>
<rect x="360" y="34" width="300" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="510.0" y="52.5" text-anchor="middle" font-size="13">spi_async(spi, &amp;msg)</text>
<text x="510.0" y="70.5" text-anchor="middle" font-size="12">statistics: spi_async++</text>
<rect x="20" y="104" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170.0" y="124.5" text-anchor="middle" font-size="13">큐가 비었고 must_async가 아니면?</text>
<text x="170.0" y="142.6" text-anchor="middle" font-size="12">예 → fast path (아래)</text>
<rect x="360" y="104" width="300" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="510.0" y="124.5" text-anchor="middle" font-size="13">queue_lock(spinlock) 안에서</text>
<text x="510.0" y="142.6" text-anchor="middle" font-size="12">ctlr-&gt;queue에 넣고 return 0</text>
<rect x="20" y="178" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170.0" y="198.6" text-anchor="middle" font-size="13">__spi_transfer_message_noqueue</text>
<text x="170.0" y="216.6" text-anchor="middle" font-size="12">호출자 스레드가 직접 전송 (immediate++)</text>
<rect x="360" y="178" width="300" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="510.0" y="198.6" text-anchor="middle" font-size="13">kthread "spi0" = 메시지 펌프</text>
<text x="510.0" y="216.6" text-anchor="middle" font-size="12">__spi_pump_messages: 큐에서 하나씩</text>
<line x1="170" y1="80" x2="170" y2="101" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="170" y1="154" x2="170" y2="175" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="510" y1="80" x2="510" y2="101" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="510" y1="154" x2="510" y2="175" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="170" y1="228" x2="170" y2="265" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="510" y1="228" x2="510" y2="265" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="340" y="97" text-anchor="middle" font-size="12">아니오</text>
<path d="M320,129 C345,129 345,203 357,203" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="4,3"/>
<line x1="350" y1="200" x2="357" y2="203" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="20" y="268" width="640" height="58" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340.0" y="292.6" text-anchor="middle" font-size="13">ctlr-&gt;transfer_one_message (기본 구현: spi_transfer_one_message)</text>
<text x="340.0" y="310.6" text-anchor="middle" font-size="12">set_cs(assert) → transfer마다 transfer_one() → 완료 대기 → cs_change 처리 → set_cs(deassert)</text>
<line x1="340" y1="326" x2="340" y2="353" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="20" y="356" width="640" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340.0" y="376.6" text-anchor="middle" font-size="13">컨트롤러 완료 IRQ (이 노트에선 hrtimer) — hardirq 문맥</text>
<text x="340.0" y="394.6" text-anchor="middle" font-size="12">spi_finalize_current_transfer() → complete() → 펌프가 다음 transfer로</text>
<line x1="170" y1="406" x2="170" y2="433" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="510" y1="406" x2="510" y2="433" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="20" y="436" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170.0" y="456.6" text-anchor="middle" font-size="13">spi_finalize_current_message</text>
<text x="170.0" y="474.6" text-anchor="middle" font-size="12">complete() → spi_sync 반환</text>
<rect x="360" y="436" width="300" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="510.0" y="456.6" text-anchor="middle" font-size="13">spi_finalize_current_message</text>
<text x="510.0" y="474.6" text-anchor="middle" font-size="12">msg-&gt;complete(context) 콜백 호출</text>
</svg>
```
그림 5 — 두 경로. 왼쪽 fast path는 호출자 스레드가 펌프 역할까지 한다(통계 `spi_sync_immediate`). 오른쪽은 kthread `spi0`이 메시지를 하나씩 꺼낸다. 두 경로 모두 결국 `transfer_one_message` → `transfer_one` → 완료 IRQ로 같은 길을 지나고, 마지막 `spi_finalize_current_message()`에서 갈린다.

6.18 소스의 그 지름길:

```c
	/* drivers/spi/spi.c __spi_sync(), 4552행 부근 (발췌) */
	if (READ_ONCE(ctlr->queue_empty) && !ctlr->must_async) {
		message->actual_length = 0;
		message->status = -EINPROGRESS;

		trace_spi_message_submit(message);

		SPI_STATISTICS_INCREMENT_FIELD(ctlr->pcpu_statistics, spi_sync_immediate);
		SPI_STATISTICS_INCREMENT_FIELD(spi->pcpu_statistics, spi_sync_immediate);

		__spi_transfer_message_noqueue(ctlr, message);

		return message->status;
	}
	/* 아니면: complete = spi_complete 로 바꿔서 큐에 넣고 wait_for_completion */
```

### 5.2 예제 8 — `spi_async`: 콜백은 어디서, 어떤 순서로

무엇을 확인하나: 메시지 4개를 연달아 `spi_async`로 던지고, 콜백 안에서 `in_task()`, `in_hardirq()`, `irqs_disabled()`, `current->comm`을 찍는다.

```c
/* h7_imu.c (B) — spi_async: 완료 콜백은 어느 문맥에서, 어떤 순서로 불리나 */
struct h7_actx {
	struct spi_message m;
	struct spi_transfer t[2];
	int seq;
};
static atomic_t h7_order, h7_pending;
static DECLARE_COMPLETION(h7_all_done);
static bool h7_cb_log = true;

static void h7_async_done(void *arg)		/* 슬립 금지! */
{
	struct h7_actx *a = arg;
	int ord = atomic_inc_return(&h7_order);

	if (h7_cb_log)
		pr_info("h7_imu: cb seq=%d order=%d status=%d in_task=%d hardirq=%d irqs_off=%d comm=%s\n",
			a->seq, ord, a->m.status, !!in_task(), !!in_hardirq(),
			!!irqs_disabled(), current->comm);
	if (atomic_dec_and_test(&h7_pending))
		complete(&h7_all_done);
}

/* n개 메시지를 spi_async로 연달아 큐잉하고 마지막 완료만 기다린다 */
static int h7_async_burst(struct h7_imu *im, int n, u8 *buf, struct h7_actx *ctx)
{
	int i, ret;

	atomic_set(&h7_order, 0);
	atomic_set(&h7_pending, n);
	reinit_completion(&h7_all_done);
	for (i = 0; i < n; i++) {
		struct h7_actx *a = &ctx[i];

		buf[i * 8] = RD(REG_ACCEL);
		a->seq = i;
		a->t[0] = (struct spi_transfer){ .tx_buf = &buf[i * 8], .len = 1 };
		a->t[1] = (struct spi_transfer){ .rx_buf = &buf[i * 8 + 1], .len = 6 };
		spi_message_init_with_transfers(&a->m, a->t, 2);
		a->m.complete = h7_async_done;
		a->m.context = a;
		ret = spi_async(im->spi, &a->m);	/* 큐에 넣고 바로 반환 */
		if (ret)
			return ret;
	}
	if (!wait_for_completion_timeout(&h7_all_done, msecs_to_jiffies(5000)))
		return -ETIMEDOUT;
	return 0;
}
```

```text
### async 4
spi_master spi0: prepare_transfer_hardware (pump busy)
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 6 rx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
h7_imu: cb seq=0 order=1 status=0 in_task=1 hardirq=0 irqs_off=0 comm=spi0
spi_master spi0: prepare_transfer_hardware (pump busy)
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 6 rx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
h7_imu: cb seq=1 order=2 status=0 in_task=1 hardirq=0 irqs_off=0 comm=spi0
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 6 rx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
h7_imu: cb seq=2 order=3 status=0 in_task=1 hardirq=0 irqs_off=0 comm=spi0
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 6 rx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
h7_imu: cb seq=3 order=4 status=0 in_task=1 hardirq=0 irqs_off=0 comm=spi0
```

출력에서 볼 것:

- 순서: 같은 컨트롤러에 넣은 메시지는 **FIFO 순서로** 처리되고 콜백도 그 순서다(`seq == order - 1`).
- 문맥: 이 컨트롤러(기본 `spi_transfer_one_message` 사용)에서는 콜백이 **펌프 kthread `spi0`의 task 문맥**에서 불렸다. 완료 IRQ(hrtimer, hardirq)는 `complete()`만 하고, 콜백은 깨어난 kthread가 `spi_finalize_current_message()`에서 부른다.
- 그렇다고 "콜백에서 슬립해도 된다"고 결론 내리면 안 된다. **API 계약은 "콜백은 슬립하면 안 된다"**이다. `transfer_one_message`를 직접 구현해 IRQ 핸들러에서 `spi_finalize_current_message()`를 부르는 컨트롤러 드라이버도 있고, 그러면 콜백이 hardirq에서 돈다. 콜백에서는 플래그 세우기, `complete()`, 다음 `spi_async` 큐잉, workqueue로 넘기기 정도만 한다. 특히 콜백에서 `spi_sync()`를 부르면 펌프 kthread가 자기 자신이 끝나기를 기다리는 꼴이 된다.
- 두 번째 메시지 앞에 `prepare_transfer_hardware`가 또 찍힌 것은, 그 사이 큐가 잠깐 비어 펌프가 idle로 갔다는 뜻이다(TCG에서는 제출이 느려서 생긴다).

### 5.3 예제 9 — ftrace로 본 "누가 전송하나"

무엇을 확인하나: SPI tracepoint(`spi:spi_message_submit/start/done`, `spi:spi_transfer_start/stop`, `spi:spi_set_cs`, `spi:spi_controller_busy/idle`)로 sync와 async의 실행 주체를 본다. 컨트롤러 로그는 끄고(`verbose=0`) trace만 본다.

```sh
# t4: spi_sync(호출자 문맥) vs spi_async(펌프 kthread) — ftrace로 누가 무엇을 하나
dmesg -n 1; insmod /h7_fakectlr.ko verbose=0 $FCARGS && insmod /h7_imu.ko; dmesg -c >/dev/null
mount -t tracefs none /sys/kernel/tracing; T=/sys/kernel/tracing
C=/sys/kernel/debug/h7_imu/cmd
echo "### pump thread"; ps -o pid,comm | grep -E "spi0|PID"
grep -E "policy|prio" /proc/$(pidof spi0)/sched
for e in spi_message_submit spi_message_start spi_message_done spi_transfer_start spi_transfer_stop spi_set_cs spi_controller_busy spi_controller_idle; do echo 1 > $T/events/spi/$e/enable; done
echo > $T/trace; echo basic > $C
echo "### trace: basic (spi_w8r8 + spi_write_then_read)"; grep -v "^#" $T/trace
echo > $T/trace; echo "async 2" > $C; sleep 1
echo "### trace: async 2"; grep -v "^#" $T/trace
dmesg -c | sed 's/^\[[ 0-9.]*\] //'
```

```text
### pump thread
PID   COMMAND
   68 spi0
policy                                       :                    0
prio                                         :                  120
### trace: basic (spi_w8r8 + spi_write_then_read)
              sh-65      [000] .....    10.065328: spi_message_submit: spi0.0 000000004372ce45
              sh-65      [000] .....    10.067315: spi_controller_busy: spi0
              sh-65      [000] .....    10.067403: spi_message_start: spi0.0 000000004372ce45
              sh-65      [000] .....    10.067468: spi_set_cs: spi0.0 activate
              sh-65      [000] .....    10.067918: spi_transfer_start: spi0.0 0000000018882a07 len=1 tx=[f5] rx=[]
              sh-65      [000] .....    10.068893: spi_transfer_stop: spi0.0 0000000018882a07 len=1 tx=[f5] rx=[]
              sh-65      [000] .....    10.068949: spi_transfer_start: spi0.0 00000000bba91fbf len=1 tx=[] rx=[02]
              sh-65      [000] .....    10.069258: spi_transfer_stop: spi0.0 00000000bba91fbf len=1 tx=[] rx=[71]
              sh-65      [000] .....    10.069265: spi_set_cs: spi0.0 deactivate
              sh-65      [000] .....    10.069321: spi_message_done: spi0.0 000000004372ce45 len=2/2
              sh-65      [000] .....    10.069407: spi_message_submit: spi0.0 000000004372ce45
              sh-65      [000] .....    10.069416: spi_controller_busy: spi0
              sh-65      [000] .....    10.069419: spi_message_start: spi0.0 000000004372ce45
              sh-65      [000] .....    10.069423: spi_set_cs: spi0.0 activate
              sh-65      [000] .....    10.069429: spi_transfer_start: spi0.0 0000000018882a07 len=1 tx=[bb] rx=[]
              sh-65      [000] .....    10.069907: spi_transfer_stop: spi0.0 0000000018882a07 len=1 tx=[bb] rx=[]
              sh-65      [000] .....    10.069929: spi_transfer_start: spi0.0 00000000bba91fbf len=6 tx=[] rx=[71-12-22-32-42-52]
              sh-65      [000] .....    10.070487: spi_transfer_stop: spi0.0 00000000bba91fbf len=6 tx=[] rx=[04-14-24-34-44-54]
              sh-65      [000] .....    10.070502: spi_set_cs: spi0.0 deactivate
              sh-65      [000] .....    10.070518: spi_message_done: spi0.0 000000004372ce45 len=7/7
### trace: async 2
              sh-65      [000] d..1.    10.171513: spi_message_submit: spi0.0 0000000080398d2f
            spi0-68      [000] .....    10.172982: spi_controller_busy: spi0
            spi0-68      [000] .....    10.172995: spi_message_start: spi0.0 0000000080398d2f
            spi0-68      [000] .....    10.173003: spi_set_cs: spi0.0 activate
            spi0-68      [000] .....    10.173016: spi_transfer_start: spi0.0 000000001cc1d275 len=1 tx=[bb] rx=[]
              sh-65      [000] d..1.    10.177370: spi_message_submit: spi0.0 0000000062997a61
            spi0-68      [000] .....    10.180498: spi_transfer_stop: spi0.0 000000001cc1d275 len=1 tx=[bb] rx=[]
            spi0-68      [000] .....    10.180516: spi_transfer_start: spi0.0 00000000a104ab11 len=6 tx=[] rx=[36-00-00-00-00-00]
            spi0-68      [000] .....    10.182690: spi_transfer_stop: spi0.0 00000000a104ab11 len=6 tx=[] rx=[05-15-25-35-45-55]
            spi0-68      [000] .....    10.182916: spi_set_cs: spi0.0 deactivate
            spi0-68      [000] .....    10.182936: spi_message_done: spi0.0 0000000080398d2f len=7/7
            spi0-68      [000] .....    10.183279: spi_message_start: spi0.0 0000000062997a61
            spi0-68      [000] .....    10.183287: spi_set_cs: spi0.0 activate
            spi0-68      [000] .....    10.183297: spi_transfer_start: spi0.0 000000002e6cf93d len=1 tx=[bb] rx=[]
            spi0-68      [000] .....    10.184427: spi_transfer_stop: spi0.0 000000002e6cf93d len=1 tx=[bb] rx=[]
            spi0-68      [000] .....    10.184434: spi_transfer_start: spi0.0 000000000da8ba93 len=6 tx=[] rx=[2e-e9-05-00-00-ff]
            spi0-68      [000] .....    10.185092: spi_transfer_stop: spi0.0 000000000da8ba93 len=6 tx=[] rx=[06-16-26-36-46-56]
            spi0-68      [000] .....    10.185132: spi_set_cs: spi0.0 deactivate
            spi0-68      [000] .....    10.185143: spi_message_done: spi0.0 0000000062997a61 len=7/7
            spi0-68      [000] .....    10.188729: spi_controller_idle: spi0
h7_imu spi0.0: WHO_AM_I=0x71 accel=04 14 24 34 44 54
h7_imu: cb seq=0 order=1 status=0 in_task=1 hardirq=0 irqs_off=0 comm=spi0
h7_imu: cb seq=1 order=2 status=0 in_task=1 hardirq=0 irqs_off=0 comm=spi0
```

출력에서 볼 것:

- **sync**: 모든 이벤트의 실행 주체가 `sh-65` — debugfs에 `echo`한 셸 프로세스 자신이다. 펌프 kthread(`spi0-68`)는 한 번도 안 나온다. fast path다.
- **async**: `spi_message_submit`만 `sh-65`이고 나머지는 전부 `spi0-68`이다. submit 줄의 플래그 `d..1.`은 "IRQ 꺼짐(d), preempt count 1" — `spi_async`가 spinlock_irqsave 안에서 큐에 넣는다는 증거이자, 그래서 atomic 문맥에서 불러도 된다는 증거다.
- 두 번째 메시지는 첫 메시지가 전송 중일 때 submit되었고(10.177370), 펌프는 첫 메시지를 끝내자마자 idle로 가지 않고 바로 다음을 시작했다(`spi_controller_busy`가 다시 안 찍힘). 큐가 빌 때 비로소 `spi_controller_idle`.
- `spi_transfer_start`의 `rx=[...]`는 **전송 전 버퍼의 쓰레기 값**이다(예: `rx=[36-00-...]`). 의미 있는 값은 `spi_transfer_stop` 쪽이다. `tx=[f5]`는 `RD(0x75)` = 0x80 + 0x75, `tx=[bb]`는 `RD(0x3b)`.
- 펌프 kthread `spi0`의 스케줄링 정책은 기본 `SCHED_NORMAL`(policy 0, prio 120)이다. 컨트롤러가 `ctlr->rt = true`이거나, 클라이언트 드라이버가 `spi->rt = true`로 두고 `spi_setup()`을 부르면 `SCHED_FIFO`가 된다(6.18 `spi.c` 3981행. 예: `mcp251xfd` CAN 드라이버, `cros_ec_spi`)(§9에서 `rt=1`로 `policy 1, prio 49`를 확인).

> 실습 팁: 진짜 보드에서 "SPI가 가끔 느려요"는 이 trace 한 장으로 거의 끝난다. `spi_message_submit` → `spi_message_start` 간격이 크면 큐 대기(다른 장치가 버스를 쥠), `spi_transfer_start` → `stop`이 크면 선로·DMA·IRQ 지연, `spi_set_cs` 간격은 CS 타이밍이다. `trace-cmd record -e spi -e gpio`로 받아 KernelShark로 보면 로직 분석기 화면과 거의 같다.

### 5.4 버스 잠금, `must_async`, 메시지 최적화

- **`spi_bus_lock(ctlr)` / `spi_bus_unlock(ctlr)` + `spi_sync_locked()`** — 여러 메시지 동안 버스를 독점한다. 예: SD 카드(mmc_spi)는 초기화 중 CS를 잡은 채 여러 메시지를 보내야 해서 이걸 쓴다. 잠근 동안 다른 장치의 `spi_sync`는 기다린다. 일반 센서 드라이버는 쓸 일이 거의 없다(이 노트에서 실행하지 않았다 — 6.18 `drivers/spi/spi.c` 4659행 정의만 확인).
- **`ctlr->must_async`** — 컨트롤러가 호출자 문맥 전송을 싫어할 때(예: 특정 CPU에서만 돌아야 하는 경우) fast path를 끈다.
- **`spi_optimize_message()`** (6.9 무렵 추가, 버전은 기억 기반 — 6.18 소스 4329행에 존재) — 같은 메시지를 반복해서 보낼 때 검증·분할·DMA 준비를 한 번만 하게 미리 처리해 둔다. 고속 ADC 드라이버(IIO)에서 쓴다.

---

## 6. regmap-spi — 레지스터 칩이라면 이것부터

### 6.1 직관

§4의 `RD(reg)`, `spi_write_then_read`, auto-increment 같은 "레지스터 맵 칩 프로토콜"은 IMU·PMIC·코덱 대부분이 같다. regmap(H2)은 이걸 설정 구조체 하나로 선언하게 해 준다. 덤으로 캐시, debugfs 덤프, 범위 검사, 필드 접근(`regmap_field`)을 공짜로 얻고, 같은 칩의 I2C 버전과 드라이버 코드를 공유할 수 있다.

### 6.2 예제 10 — 같은 IMU를 regmap으로

무엇을 확인하나: `read_flag_mask = 0x80` 한 줄이 `RD()` 매크로를 대신하는지, 각 regmap 호출이 SPI에서 어떤 메시지가 되는지.

```c
/* h7_imu.c (D) — 스택 버퍼 버그, regmap, debugfs 명령, probe */
static noinline int h7_stack_bug(struct h7_imu *im)
{
	u8 cmd = RD(REG_ACCEL), acc[6];			/* ← 스택! spi_sync에 직접 넘기면 버그 */
	struct spi_transfer t[2] = { { .tx_buf = &cmd, .len = 1 },
				     { .rx_buf = acc, .len = sizeof(acc) } };
	int ret = spi_sync_transfer(im->spi, t, 2);

	dev_info(&im->spi->dev, "stack read ret=%d accel=%6ph\n", ret, acc);
	return ret;
}

static const struct regmap_config h7_rcfg = {
	.reg_bits = 8, .val_bits = 8,
	.read_flag_mask = 0x80,				/* RD(reg)를 regmap이 대신 */
	.max_register = 0x7f,
};

static int h7_regmap_demo(struct h7_imu *im)
{
	unsigned int who, pwr;
	u8 acc[6];
	int ret;

	ret = regmap_read(im->map, REG_WHO_AM_I, &who) ?:
	      regmap_bulk_read(im->map, REG_ACCEL, acc, 6) ?:
	      regmap_write(im->map, 0x6b, 0x01) ?:		/* PWR_MGMT_1 */
	      regmap_read(im->map, 0x6b, &pwr);
	dev_info(&im->spi->dev, "regmap ret=%d who=0x%02x accel=%6ph pwr=0x%02x\n", ret, who, acc, pwr);
	return ret;
}

static ssize_t h7_cmd_write(struct file *f, const char __user *ub, size_t n, loff_t *pos)
{
	struct h7_imu *im = f->private_data;
	char s[24] = {};
	int arg = 0, ret = -EINVAL;

	if (n >= sizeof(s) || copy_from_user(s, ub, n))
		return -EINVAL;
	if (sysfs_streq(s, "basic"))		ret = h7_basic(im);
	else if (sscanf(s, "cs %d", &arg) == 1)	ret = h7_two_reads(im, arg);
	else if (sysfs_streq(s, "stack"))	ret = h7_stack_bug(im);
	else if (sysfs_streq(s, "regmap"))	ret = h7_regmap_demo(im);
	else if (sscanf(s, "bench %d", &arg) == 1 && arg > 0) ret = h7_bench(im, arg);
	else if (sscanf(s, "async %d", &arg) == 1 && arg > 0) {
		struct h7_actx *ctx = kcalloc(arg, sizeof(*ctx), GFP_KERNEL);
		u8 *buf = kmalloc_array(arg, 8, GFP_KERNEL);

		ret = (ctx && buf) ? h7_async_burst(im, arg, buf, ctx) : -ENOMEM;
		kfree(ctx);
		kfree(buf);
	}
	return ret < 0 ? ret : n;
}
```

```text
### regmap
spi_master spi0: prepare_transfer_hardware (pump busy)
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 1 rx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
spi_master spi0: prepare_transfer_hardware (pump busy)
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 6 rx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
spi_master spi0: prepare_transfer_hardware (pump busy)
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 2 tx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
spi_master spi0: prepare_transfer_hardware (pump busy)
spi_master spi0: CS0 line LOW (assert)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0:   xfer cs0 len 1 rx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
h7_imu spi0.0: regmap ret=0 who=0x71 accel=07 17 27 37 47 57 pwr=0x01
```

출력에서 볼 것: 네 개의 regmap 호출이 네 개의 CS 구간이 되었다. `regmap_read` = 1B tx + 1B rx(내부적으로 `spi_write_then_read`), `regmap_bulk_read` = 1B tx + 6B rx(칩의 auto-increment를 믿고 한 번에), `regmap_write` = **2바이트 transfer 하나**(주소 + 값, write 비트는 0), 다시 `regmap_read`로 0x01이 읽혔다. 드라이버 코드에는 0x80이 한 번도 안 나온다. 이 노트에서는 `cache_type`을 지정하지 않아(캐시 없음) 매번 버스로 나갔다. `REGCACHE_MAPLE` 같은 캐시를 켜면 쓰기 전용 설정 레지스터 재읽기는 버스를 타지 않는다(H2).

---

## 7. spidev — 유저 공간에서 SPI 직접 두드리기

### 7.1 왜 DT에 `compatible = "spidev"`를 쓰면 안 되나

spidev는 "어떤 칩에든 붙어 `/dev/spidevB.C`를 만드는 범용 드라이버"다. 그래서 예전에는 DT에 `compatible = "spidev"`라고 적는 일이 흔했다. 하지만 **DT는 하드웨어를 기술하는 것**이고 "spidev"는 하드웨어가 아니라 Linux 구현의 이름이다. 그래서 커널은 이를 막는다. 6.18의 spidev:

```c
/* drivers/spi/spidev.c 707행~ */
/*
 * spidev should never be referenced in DT without a specific compatible string,
 * it is a Linux implementation thing rather than a description of the hardware.
 */
static int spidev_of_check(struct device *dev)
{
	if (device_property_match_string(dev, "compatible", "spidev") < 0)
		return 0;

	dev_err(dev, "spidev listed directly in DT is not supported\n");
	return -EINVAL;
}
```

spidev의 `of_match_table`에는 실제 칩 이름(`"rohm,dh2228fv"`, `"lineartechnology,ltc2488"`, `"semtech,sx1301"` 등 15개)만 있고, 각 항목의 match data가 이 검사 함수다. 예제 6의 `legacy@2`는 `"rohm,dh2228fv", "spidev"`라 매칭은 됐지만 검사에서 걸렸다. `compatible = "spidev"` **하나만** 적으면 아예 아무 드라이버도 매칭되지 않는다(modalias `spi:spidev`가 spidev의 `id_table`에 없다).

올바른 방법은 두 가지다.

1. **DT에는 진짜 칩 이름**(`"ti,ads7950"` 같은)을 쓴다. 커널 드라이버가 있으면 그게 붙는다. 프로토타이핑 단계라 spidev로 쓰고 싶으면 2번.
2. **`driver_override`** — 장치의 sysfs 파일에 `spidev`를 쓰고 bind한다. 매칭 1순위가 override라(§1.5) compatible과 무관하게 spidev가 붙고, spidev의 DT 검사도 match data가 없으니 건너뛴다.

### 7.2 예제 11 — `driver_override`로 ADC(CS1)에 spidev 붙이기

무엇을 확인하나: override → bind → `/dev/spidev0.1` 생성, 유저 프로그램의 `SPI_IOC_MESSAGE(2)`가 컨트롤러에서 어떻게 보이는지(특히 **GPIO CS**), 틀린 모드와 지원 안 하는 모드 비트를 넣으면 무슨 일이 생기는지.

유저 프로그램 — 16-bit 워드, mode 0, 1 MHz로 채널 0과 3을 한 ioctl로 읽는다. `cs_change = 1`로 프레임마다 CS를 토글한다.

```c
/* h7_spidev_adc.c — spidev로 12-bit ADC(CS1) 두 채널을 한 번의 ioctl로 읽는다 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

static void die(const char *s) { perror(s); exit(1); }

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/spidev0.1";
	uint32_t mode = argc > 2 ? strtoul(argv[2], NULL, 0) : SPI_MODE_0;
	uint8_t bpw = 16;
	uint32_t speed = 1000000, m32;
	uint16_t tx[2] = { 0x8000 | (0 << 12), 0x8000 | (3 << 12) };	/* ch0, ch3 */
	uint16_t rx[2] = { 0 };
	struct spi_ioc_transfer x[2];
	int fd = open(dev, O_RDWR);

	if (fd < 0)
		die("open");
	if (ioctl(fd, SPI_IOC_RD_MODE32, &m32) < 0)
		die("RD_MODE32");
	printf("before: mode32=0x%x\n", m32);
	if (ioctl(fd, SPI_IOC_WR_MODE32, &mode) < 0)
		die("WR_MODE32");
	if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bpw) < 0 ||
	    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
		die("cfg");
	memset(x, 0, sizeof(x));
	x[0].tx_buf = (uintptr_t)&tx[0]; x[0].rx_buf = (uintptr_t)&rx[0];
	x[0].len = 2; x[0].cs_change = 1;		/* 프레임마다 CS를 올렸다 내린다 */
	x[1].tx_buf = (uintptr_t)&tx[1]; x[1].rx_buf = (uintptr_t)&rx[1];
	x[1].len = 2;
	int n = ioctl(fd, SPI_IOC_MESSAGE(2), x);	/* 반환값 = 전송한 바이트 수 */
	if (n < 0)
		die("SPI_IOC_MESSAGE");
	for (int i = 0; i < 2; i++)
		printf("mode %u: ch%u -> raw 0x%04x sample %u (%.3f V @3.3V ref)\n",
		       mode, (rx[i] >> 12) & 7, rx[i], rx[i] & 0xfff, (rx[i] & 0xfff) * 3.3 / 4096);
	printf("ioctl returned %d bytes\n", n);
	mode = SPI_MODE_0 | SPI_LSB_FIRST;		/* 컨트롤러가 지원 안 하는 비트 */
	if (ioctl(fd, SPI_IOC_WR_MODE32, &mode) < 0)
		perror("WR_MODE32(SPI_LSB_FIRST)");
	close(fd);
	return 0;
}
```

```sh
# t3: spidev를 driver_override로 CS1(ADC)에 붙이고, 유저 프로그램 + ftrace로 관찰
dmesg -n 1; insmod /h7_fakectlr.ko verbose=0 && insmod /spidev.ko; dmesg -c >/dev/null
D=/sys/bus/spi/devices/spi0.1
echo "### driver_override"
cat $D/driver_override; echo spidev > $D/driver_override
echo spi0.1 > /sys/bus/spi/drivers/spidev/bind
ls -l /dev/spidev*; readlink $D/driver
dmesg -c | sed 's/^\[[ 0-9.]*\] //'
echo "### user program mode 0"
mount -t tracefs none /sys/kernel/tracing; T=/sys/kernel/tracing
echo 1 > $T/events/spi/enable; echo 1 > $T/events/gpio/gpio_value/enable; echo > $T/trace
/h7_spidev_adc /dev/spidev0.1 0
echo 0 > $T/tracing_on
echo "### trace"
grep -v "^#" $T/trace
echo "### user program mode 3 (wrong)"
echo 1 > $T/tracing_on; /h7_spidev_adc /dev/spidev0.1 3
dmesg -c | sed 's/^\[[ 0-9.]*\] //'
echo "### statistics spi0.1"
cd $D/statistics && for f in messages transfers bytes spi_sync spi_sync_immediate spi_async errors; do echo "$f=$(cat $f)"; done
```

```sh
sh /Users/donh/.elinux_scratch/H7/boot.sh t3.sh
```

```text
### driver_override

crw-------    1 0        0         153,   0 Oct 11 03:53 /dev/spidev0.1
../../../../../../bus/spi/drivers/spidev
### user program mode 0
before: mode32=0x0
mode 0: ch0 -> raw 0x0400 sample 1024 (0.825 V @3.3V ref)
mode 0: ch3 -> raw 0x3800 sample 2048 (1.650 V @3.3V ref)
ioctl returned 4 bytes
WR_MODE32(SPI_LSB_FIRST): Invalid argument
### trace
   h7_spidev_adc-77      [000] .....    14.263940: spi_set_cs: spi0.1 deactivate, cs_high
   h7_spidev_adc-77      [000] .....    14.270802: gpio_value: 517 set 1
   h7_spidev_adc-77      [000] .....    14.270964: spi_setup: spi0.1 setup mode 0, cs_high, 8 bits/w, 1000000 Hz max --> 0
   h7_spidev_adc-77      [000] .....    14.271403: spi_set_cs: spi0.1 deactivate, cs_high
   h7_spidev_adc-77      [000] .....    14.271442: gpio_value: 517 set 1
   h7_spidev_adc-77      [000] .....    14.271458: spi_setup: spi0.1 setup mode 0, cs_high, 16 bits/w, 1000000 Hz max --> 0
   h7_spidev_adc-77      [000] .....    14.271657: spi_set_cs: spi0.1 deactivate, cs_high
   h7_spidev_adc-77      [000] .....    14.271672: gpio_value: 517 set 1
   h7_spidev_adc-77      [000] .....    14.271685: spi_setup: spi0.1 setup mode 0, cs_high, 16 bits/w, 1000000 Hz max --> 0
   h7_spidev_adc-77      [000] .....    14.277020: spi_message_submit: spi0.1 00000000c54ef364
   h7_spidev_adc-77      [000] .....    14.277241: spi_controller_busy: spi0
   h7_spidev_adc-77      [000] .....    14.277351: spi_message_start: spi0.1 00000000c54ef364
   h7_spidev_adc-77      [000] .....    14.277531: spi_set_cs: spi0.1 activate, cs_high
   h7_spidev_adc-77      [000] .....    14.277595: gpio_value: 517 set 0
   h7_spidev_adc-77      [000] .....    14.277782: spi_transfer_start: spi0.1 000000007dbcfe5f len=2 tx=[00-80] rx=[00-00]
   h7_spidev_adc-77      [000] .....    14.282089: spi_transfer_stop: spi0.1 000000007dbcfe5f len=2 tx=[00-80] rx=[00-04]
   h7_spidev_adc-77      [000] .....    14.282216: spi_set_cs: spi0.1 deactivate, cs_high
   h7_spidev_adc-77      [000] .....    14.282252: gpio_value: 517 set 1
   h7_spidev_adc-77      [000] .....    14.282390: spi_set_cs: spi0.1 activate, cs_high
   h7_spidev_adc-77      [000] .....    14.282406: gpio_value: 517 set 0
   h7_spidev_adc-77      [000] .....    14.282462: spi_transfer_start: spi0.1 00000000164671f1 len=2 tx=[00-b0] rx=[00-00]
   h7_spidev_adc-77      [000] .....    14.284487: spi_transfer_stop: spi0.1 00000000164671f1 len=2 tx=[00-b0] rx=[00-38]
   h7_spidev_adc-77      [000] .....    14.284545: spi_set_cs: spi0.1 deactivate, cs_high
   h7_spidev_adc-77      [000] .....    14.284565: gpio_value: 517 set 1
   h7_spidev_adc-77      [000] .....    14.285353: spi_message_done: spi0.1 00000000c54ef364 len=4/4
### user program mode 3 (wrong)
before: mode32=0x0
mode 3: ch0 -> raw 0x0201 sample 513 (0.413 V @3.3V ref)
mode 3: ch1 -> raw 0x1c01 sample 3073 (2.476 V @3.3V ref)
ioctl returned 4 bytes
WR_MODE32(SPI_LSB_FIRST): Invalid argument
spi_master spi0: setup spi0.1: cs1 mode 0 cs-high 1000000 Hz bpw 8
spi_master spi0: setup spi0.1: cs1 mode 0 cs-high 1000000 Hz bpw 16
spi_master spi0: setup spi0.1: cs1 mode 0 cs-high 1000000 Hz bpw 16
spidev spi0.1: setup: unsupported mode bits 8
spi_master spi0: setup spi0.1: cs1 mode 3 cs-high 1000000 Hz bpw 16
spi_master spi0: setup spi0.1: cs1 mode 3 cs-high 1000000 Hz bpw 16
spi_master spi0: setup spi0.1: cs1 mode 3 cs-high 1000000 Hz bpw 16
spi_master spi0: cs1: mode 3 1000000 Hz bpw 16 out of spec -> garbage
spi_master spi0: cs1: mode 3 1000000 Hz bpw 16 out of spec -> garbage
spidev spi0.1: setup: unsupported mode bits 8
### statistics spi0.1
messages=2
transfers=4
bytes=8
spi_sync=2
spi_sync_immediate=2
spi_async=0
errors=0
```

(`ls`의 색상 escape 코드만 지웠다.) 출력에서 볼 것:

- `driver_override`는 처음에 빈 값이었고, `spidev`를 쓴 뒤 `bind`하자 `/dev/spidev0.1`(major 153 = `SPIDEV_MAJOR`)이 생겼다. 이름의 `0.1`은 `bus_num.chip_select`다.
- **GPIO CS**: 컨트롤러의 `set_cs`는 불리지 않았고(verbose=0이라 원래 안 찍히지만, CS1은 애초에 코어가 처리), 코어가 `gpio_value: 517 set 0/1`로 PL061 5번 핀(전역 GPIO 번호 517)을 직접 움직였다. 이 tracepoint 두 개(`spi_set_cs` + `gpio_value`)가 스코프의 CS 채널과 같은 정보를 준다. `set 0`이 assert인 것은 DT에서 `GPIO_ACTIVE_LOW`라서다 — 극성은 gpiolib이 처리한다.
- `cs_high` 표시: spidev는 GPIO CS 장치에 대해 내부적으로 `SPI_CS_HIGH`를 켜고(극성을 gpiolib에 맡긴다는 뜻), 유저에게 `SPI_IOC_RD_MODE32`로 보여 줄 때는 지운다(`before: mode32=0x0`). 6.18 `drivers/spi/spidev.c` 383·418행의 `use_gpio_descriptors && spi_get_csgpiod()` 분기다.
- 16-bit 워드의 메모리 배치: `tx=[00-80]`은 `u16 0x8000`(start bit, ch0)의 **리틀 엔디언 바이트**다. 선 위에는 컨트롤러가 MSB부터 내보낸다. 유저 프로그램은 `u16` 배열로 다루면 되고, 바이트 단위로 보낼 때(`bits_per_word = 8`)와 섞으면 바이트 순서 버그가 난다.
- `ioctl` 반환값 4 = 전송한 총 바이트 수. 메시지 1개(transfer 2개)가 통계에 `messages=2`로 나온 것은 프로그램을 두 번(mode 0, mode 3) 돌렸기 때문이다. `spi_sync_immediate=2` — spidev도 `spi_sync`를 부르고, 큐가 비어 있어 **유저 프로세스 `h7_spidev_adc-77`의 문맥에서** 전송되었다(trace의 실행 주체).
- **틀린 모드(3)**: 에러 없이 `ioctl returned 4 bytes`. 값만 틀렸다 — 채널 3을 물었는데 응답 채널 필드가 1이다(1비트 밀림). 진짜 칩에서도 똑같이 "조용히 틀린다".
- **`SPI_LSB_FIRST`(0x8)**: 컨트롤러의 `mode_bits`에 없으므로 `spi_setup()`이 `unsupported mode bits 8`을 찍고 `-EINVAL`. spidev는 이전 모드로 되돌린다. 하드웨어가 못 하는 것을 조용히 무시하지 않고 거부하는 것이 `mode_bits`의 역할이다.

### 7.3 spidev를 쓸 때와 쓰지 말 때

| 쓰면 좋다 | 쓰면 안 된다 |
|---|---|
| bring-up 초기: 칩이 살아 있나, WHO_AM_I가 나오나 | 인터럽트(data-ready)가 필요한 장치 — spidev는 IRQ를 모른다 |
| 공장 테스트·캘리브레이션 툴 | 다른 커널 서브시스템(IIO, input, MTD, net)과 묶여야 하는 장치 |
| 커널 드라이버가 없는 FPGA 레지스터 접근(프로토타입) | 전원·클럭·리셋 시퀀스가 필요한 장치 (H3) |
| 파이썬 `spidev` 모듈로 빠른 실험 | 높은 샘플링 속도(ioctl마다 syscall + 복사, `bufsiz` 4096 기본 제한) |

---

## 8. DMA-safe 버퍼 — 왜 스택 버퍼를 넘기면 안 되나

### 8.1 직관

SPI 컨트롤러는 큰 transfer를 보통 **DMA**로 한다. 그리고 DMA 엔진은 CPU 캐시를 모른다(non-coherent SoC). 그래서 커널은 DMA 전후로 캐시를 clean/invalidate하는데, 그 단위는 **캐시라인**(arm64에서 DMA 정렬 기준 `ARCH_DMA_MINALIGN` = 128B)이다. 버퍼가 다른 변수와 캐시라인을 나눠 쓰면, DMA가 진행되는 동안 CPU가 옆 변수를 건드리는 순간 둘 중 하나가 깨진다. 스택은 그 최악의 장소다: 버퍼 바로 옆에 다른 지역 변수, 저장된 레지스터, 스택 카나리가 있고 CPU가 끊임없이 쓴다.

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" text-anchor="start" font-size="12" font-weight="bold">캐시라인 하나 (arm64 ARCH_DMA_MINALIGN = 128B) — 스택 위</text>
<rect x="20" y="32" width="200" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="120.0" y="50.5" text-anchor="middle" font-size="13">int retry</text>
<text x="120.0" y="68.5" text-anchor="middle" font-size="12">다른 지역 변수</text>
<rect x="220" y="32" width="220" height="46" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="330.0" y="50.5" text-anchor="middle" font-size="13">u8 acc[6]</text>
<text x="330.0" y="68.5" text-anchor="middle" font-size="12">DMA가 쓰는 수신 버퍼</text>
<rect x="440" y="32" width="220" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="550.0" y="50.5" text-anchor="middle" font-size="13">saved regs · canary</text>
<text x="550.0" y="68.5" text-anchor="middle" font-size="12">함수 프레임</text>
<rect x="20" y="110" width="150" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="95.0" y="131.6" text-anchor="middle" font-size="13">① dma_map (RX)</text>
<text x="95.0" y="149.6" text-anchor="middle" font-size="12">arm64: 라인 clean</text>
<line x1="170" y1="136" x2="180" y2="136" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="183" y="110" width="150" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="258.0" y="131.6" text-anchor="middle" font-size="13">② CPU가 retry++</text>
<text x="258.0" y="149.6" text-anchor="middle" font-size="12">라인이 캐시에 dirty</text>
<line x1="333" y1="136" x2="343" y2="136" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="346" y="110" width="150" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="421.0" y="131.6" text-anchor="middle" font-size="13">③ DMA가 RAM의</text>
<text x="421.0" y="149.6" text-anchor="middle" font-size="12">acc[]에 씀</text>
<line x1="496" y1="136" x2="506" y2="136" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="509" y="110" width="150" height="52" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="584.0" y="131.6" text-anchor="middle" font-size="13">④ dirty 라인 evict</text>
<text x="584.0" y="149.6" text-anchor="middle" font-size="12">→ DMA 데이터 덮어씀</text>
<text x="20" y="192" text-anchor="start" font-size="12">④′ unmap 때 arm64는 걸친 라인을 clean+invalidate → 같은 결과 (아키텍처에 따라 retry 쪽이 사라지기도)</text>
<text x="20" y="214" text-anchor="start" font-size="12">non-coherent DMA(캐시 스누핑 없는 SoC)에서만 터진다 — 그래서 x86·QEMU에서는 재현이 안 된다</text>
<text x="20" y="236" text-anchor="start" font-size="12">VMAP_STACK이면 스택은 vmalloc 주소 → virt_addr_valid() 거짓, 페이지 경계도 불연속</text>
<rect x="20" y="256" width="640" height="96" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340.0" y="281.6" text-anchor="middle" font-size="13">해결: DMA 버퍼는 자기 캐시라인을 혼자 쓴다</text>
<text x="340.0" y="299.6" text-anchor="middle" font-size="12">kmalloc/devm_kzalloc 버퍼 (6.5+ arm64는 정렬 안 된 작은 매핑을 swiotlb로 bounce)</text>
<text x="340.0" y="317.6" text-anchor="middle" font-size="12">private 구조체 안이면 u8 buf[N] ____cacheline_aligned (IIO는 __aligned(IIO_DMA_MINALIGN))</text>
<text x="340.0" y="335.6" text-anchor="middle" font-size="12">짧은 읽기는 spi_write_then_read · spi_w8r8 · regmap — 내부 버퍼로 복사</text>
</svg>
```
그림 6 — 스택 위 수신 버퍼와 옆 변수가 캐시라인 하나를 공유할 때. ②에서 dirty가 된 라인이 ③의 DMA 뒤에 RAM으로 써지면 DMA 데이터가 사라진다. Don이 SSD 컨트롤러에서 "DMA 디스크립터 옆에 카운터를 두었다가 가끔 값이 틀어지던" 버그와 같은 종류다.

여기에 Linux만의 이유가 더 있다. arm64 기본 설정은 `CONFIG_VMAP_STACK=y`라 **커널 스택이 vmalloc 영역**에 있다. vmalloc 주소는 물리적으로 연속이 아니고 `virt_addr_valid()`가 거짓이라 `dma_map_single()` 같은 선형 매핑 API의 전제를 깬다. 그래서 DMA API 문서는 "스택과 vmalloc 메모리는 DMA에 쓰지 말라"고 하고, `CONFIG_DMA_API_DEBUG`를 켜면 `device driver maps memory from stack` 경고가 나온다.

### 8.2 예제 12 — 스택 버퍼를 `spi_sync`에 넘기면 코어가 그대로 매핑한다

무엇을 확인하나: 클라이언트의 `h7_stack_bug()`(§6.2 코드 맨 위)가 스택의 `acc[6]`을 `rx_buf`로 넘긴다. 컨트롤러의 `can_dma`를 켜면(`use_dma=1`, 4바이트 이상) 코어가 그 버퍼를 DMA 매핑하는지, 컨트롤러가 받은 주소가 어떤 성질인지 본다.

```text
### stack (use_dma=1)
spi_master spi0: prepare_transfer_hardware (pump busy)
spi_master spi0: CS0 line LOW (assert)
spi_master spi0: tx_buf is ON STACK (vmalloc=1 virt_addr_valid=0 dma-mapped=0)
spi_master spi0:   xfer cs0 len 1 tx 10000000 Hz bpw 8
spi_master spi0: rx_buf is ON STACK (vmalloc=1 virt_addr_valid=0 dma-mapped=1)
spi_master spi0:   xfer cs0 len 6 rx 10000000 Hz bpw 8
spi_master spi0: CS0 line HIGH (deassert)
h7_imu spi0.0: stack read ret=0 accel=0e 1e 2e 3e 4e 5e
```

출력에서 볼 것:

- `vmalloc=1 virt_addr_valid=0` — VMAP_STACK이라 스택 주소는 vmalloc 주소다.
- `rx_buf ... dma-mapped=1` — 6바이트 rx는 `can_dma`가 참이라 **코어가 아무 경고 없이 DMA 매핑했다**(`spi_map_buf_attrs()`는 vmalloc 버퍼를 페이지 단위로 `vmalloc_to_page`해서 scatterlist를 만든다). 1바이트 tx는 `can_dma`가 거짓이라 매핑되지 않았다(`dma-mapped=0`).
- 결과 값은 정상이다. QEMU TCG는 캐시를 흉내 내지 않고, 이 가짜 컨트롤러는 실제로 DMA를 하지 않으니 깨질 수가 없다. **"테스트에서 잘 됐다"가 이 버그의 가장 위험한 특징이다** — 진짜 non-coherent SoC에서 부하가 걸릴 때 가끔만 깨진다. 이 랩 커널은 `CONFIG_DMA_API_DEBUG`가 꺼져 있어 경고 문구를 직접 보지는 못했다(문구는 아래 소스에서 확인).

소스로 확인한 관련 사실(실제 grep 출력):

```sh
cd /var/lab/src/linux-6.18.55
grep -n "define ARCH_DMA_MINALIGN\|define ARCH_KMALLOC_MINALIGN" arch/arm64/include/asm/cache.h
grep -n "maps memory from stack" kernel/dma/debug.c
grep -n "DMA_BOUNCE_UNALIGNED_KMALLOC\|VMAP_STACK=\|DMA_API_DEBUG" /var/lab/out/linux-build-dbg/.config
grep -n "static inline bool dma_kmalloc_needs_bounce" -A 4 include/linux/dma-map-ops.h
```

```text
35:#define ARCH_DMA_MINALIGN	(128)
36:#define ARCH_KMALLOC_MINALIGN	(8)
1090:			err_printk(dev, NULL, "device driver maps memory from stack [addr=%p]\n", addr);
1102:			err_printk(dev, NULL, "device driver maps memory from stack [probable addr=%p]\n", addr);
888:CONFIG_HAVE_ARCH_VMAP_STACK=y
889:CONFIG_VMAP_STACK=y
11421:CONFIG_DMA_BOUNCE_UNALIGNED_KMALLOC=y
11439:# CONFIG_DMA_API_DEBUG is not set
313:static inline bool dma_kmalloc_needs_bounce(struct device *dev, size_t size,
314-					    enum dma_data_direction dir)
315-{
316-	return !dma_kmalloc_safe(dev, dir) && !dma_kmalloc_size_aligned(size);
317-}
```

출력에서 볼 것: arm64는 DMA 정렬 128B인데 kmalloc 최소 정렬은 8B다(6.5 무렵부터 — 버전은 기억 기반). 그 대신 `DMA_BOUNCE_UNALIGNED_KMALLOC`으로, non-coherent 장치에 캐시라인 크기의 배수가 아닌 작은 매핑이 오면 **swiotlb bounce 버퍼로 복사**해서 지킨다. 즉 최신 arm64는 작은 버퍼에 안전망이 있다. 그래도 규칙은 그대로다: 안전망은 아키텍처·IOMMU 구성에 따라 다르고(32-bit ARM, RISC-V 등은 다르다), 스택(vmalloc) 문제는 그대로이며, bounce는 성능 비용이다.

### 8.3 고치는 법 — 세 가지 패턴

```c
/* 1) private 구조체 안에 캐시라인 정렬 버퍼 (가장 흔함, IIO 드라이버 표준) */
struct imu_priv {
	struct spi_device *spi;
	struct mutex lock;              /* buf를 보호 */
	u8 buf[16] ____cacheline_aligned;   /* IIO는 __aligned(IIO_DMA_MINALIGN) */
};

/* 2) 따로 kmalloc / devm_kzalloc (이 노트의 im->tx, im->rx) */
im->rx = devm_kzalloc(&spi->dev, 64, GFP_KERNEL);

/* 3) 짧은 레지스터 접근은 복사형 헬퍼 — 스택 버퍼 허용 */
ret = spi_write_then_read(spi, &cmd, 1, acc, sizeof(acc));
```

(위 블록은 패턴 모음이라 따로 컴파일하지 않았다. 2·3번은 예제 7의 실제 코드다.)

1번에서 `____cacheline_aligned`를 **버퍼 앞에** 붙이는 이유: 버퍼가 새 캐시라인에서 시작하고, 구조체 끝까지 다른 필드가 없으니 뒤도 안전하다. 그래서 DMA 버퍼는 구조체의 **마지막 필드**로 둔다. 같은 `buf`를 동시에 두 경로가 쓰지 않도록 mutex도 필요하다.

---

## 9. 처리량 — 작은 `spi_sync` N번 vs 묶기 vs `spi_async` 파이프라인

### 9.1 예제 13 — 벤치마크

무엇을 확인하나: "IMU 가속도 읽기"(명령 1B + 데이터 6B)를 1000번 하는 네 가지 방법의 읽기당 시간.

- (a) `spi_write_then_read` × N — 복사 + 메시지 하나씩
- (b) `spi_sync_transfer` × N — 복사 없음, 메시지 하나씩
- (c) transfer 2N개를 `cs_change`로 묶은 **메시지 하나** — `spi_sync` 1회
- (d) `spi_async` N개를 연달아 큐잉하고 마지막 완료만 대기

```c
/* h7_imu.c (C) — 처리량: 작은 spi_sync N번 vs 한 메시지로 묶기 vs spi_async 파이프라인 */
static int h7_bench(struct h7_imu *im, int n)
{
	u8 *buf = kmalloc_array(n, 8, GFP_KERNEL);		/* 읽기 n개의 cmd+data */
	struct spi_transfer *t = kvcalloc(2 * n, sizeof(*t), GFP_KERNEL);
	struct h7_actx *ctx = kvcalloc(n, sizeof(*ctx), GFP_KERNEL);
	struct spi_message m;
	u64 t0, ns[4];
	int i, ret = -ENOMEM;

	if (!buf || !t || !ctx)
		goto out;
	ret = 0;
	t0 = ktime_get_ns();			/* (a) spi_write_then_read × n */
	for (i = 0; i < n && !ret; i++)
		ret = spi_write_then_read(im->spi, (u8[]){ RD(REG_ACCEL) }, 1, &buf[1], 6);
	ns[0] = ktime_get_ns() - t0;

	t0 = ktime_get_ns();			/* (b) 미리 만든 2-transfer 메시지로 spi_sync × n */
	for (i = 0; i < n && !ret; i++) {
		buf[0] = RD(REG_ACCEL);
		ret = spi_sync_transfer(im->spi, (struct spi_transfer[]){
			{ .tx_buf = buf, .len = 1 }, { .rx_buf = &buf[1], .len = 6 } }, 2);
	}
	ns[1] = ktime_get_ns() - t0;

	for (i = 0; i < n; i++) {		/* (c) 2n개 transfer를 메시지 하나에 */
		buf[i * 8] = RD(REG_ACCEL);
		t[2 * i] = (struct spi_transfer){ .tx_buf = &buf[i * 8], .len = 1 };
		t[2 * i + 1] = (struct spi_transfer){ .rx_buf = &buf[i * 8 + 1], .len = 6,
						      .cs_change = i < n - 1 };
	}
	spi_message_init_with_transfers(&m, t, 2 * n);
	t0 = ktime_get_ns();
	if (!ret)
		ret = spi_sync(im->spi, &m);
	ns[2] = ktime_get_ns() - t0;

	h7_cb_log = false;			/* (d) spi_async n개 파이프라인 */
	t0 = ktime_get_ns();
	if (!ret)
		ret = h7_async_burst(im, n, buf, ctx);
	ns[3] = ktime_get_ns() - t0;
	h7_cb_log = true;
	dev_info(&im->spi->dev, "bench n=%d ret=%d ns/read: w_then_r=%llu sync=%llu batched=%llu async=%llu\n",
		 n, ret, div_u64(ns[0], n), div_u64(ns[1], n), div_u64(ns[2], n), div_u64(ns[3], n));
out:
	kvfree(ctx);
	kvfree(t);
	kfree(buf);
	return ret;
}
```

컨트롤러는 두 모드로 돌렸다: `irq_mode=0`(transfer가 즉시 끝남 — 소프트웨어 오버헤드만 측정)과 `irq_mode=1`(hrtimer로 완료 IRQ — "선로 시간 + IRQ 지연"이 있는 진짜 컨트롤러 흉내). 그리고 펌프 kthread를 RT(`rt=1`)와 일반(`rt=0`)으로.

```sh
# t5: 처리량 — 작은 spi_sync N번 vs 묶은 메시지 vs spi_async 파이프라인 (irq_mode 0/1)
dmesg -n 1; insmod /h7_fakectlr.ko verbose=0 rt=1 && insmod /h7_imu.ko; dmesg -c >/dev/null
grep -E "^policy|^prio" /proc/$(pidof spi0)/sched
C=/sys/kernel/debug/h7_imu/cmd; P=/sys/module/h7_fakectlr/parameters
for irq in 1 0; do
  echo $irq > $P/irq_mode; echo "### irq_mode=$irq"
  for r in 1 2 3; do echo "bench 1000" > $C; done
  dmesg -c | sed 's/^\[[ 0-9.]*\] //'
done
echo "### statistics spi0.0"
cd /sys/bus/spi/devices/spi0.0/statistics && for f in messages transfers bytes spi_sync spi_sync_immediate spi_async; do echo "$f=$(cat $f)"; done
```

```text
policy                                       :                    1
prio                                         :                   49
### irq_mode=1
h7_imu spi0.0: bench n=1000 ret=0 ns/read: w_then_r=196144 sync=240580 batched=217234 async=285845
h7_imu spi0.0: bench n=1000 ret=0 ns/read: w_then_r=265035 sync=380258 batched=209709 async=259374
h7_imu spi0.0: bench n=1000 ret=0 ns/read: w_then_r=639596 sync=234441 batched=213653 async=273823
### irq_mode=0
h7_imu spi0.0: bench n=1000 ret=0 ns/read: w_then_r=26214 sync=23742 batched=20143 async=183955
h7_imu spi0.0: bench n=1000 ret=0 ns/read: w_then_r=23434 sync=21909 batched=21053 async=186019
h7_imu spi0.0: bench n=1000 ret=0 ns/read: w_then_r=27761 sync=23987 batched=17770 async=206950
### statistics spi0.0
messages=18008
transfers=48004
bytes=168009
spi_sync=12008
spi_sync_immediate=12008
spi_async=6000
```

같은 스크립트에서 `rt=1` → `rt=0`, `irq_mode`는 0만 돌린 `t6.sh`:

```text
policy                                       :                    0
prio                                         :                  120
### irq_mode=0
h7_imu spi0.0: bench n=1000 ret=0 ns/read: w_then_r=33282 sync=34810 batched=19151 async=61998
h7_imu spi0.0: bench n=1000 ret=0 ns/read: w_then_r=24452 sync=26221 batched=25322 async=62464
h7_imu spi0.0: bench n=1000 ret=0 ns/read: w_then_r=26037 sync=25804 batched=24759 async=52487
### statistics spi0.0
messages=9005
transfers=24004
bytes=84009
spi_sync=6005
spi_sync_immediate=6001
spi_async=3004
```

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="15" y="24" text-anchor="start" font-size="12">읽기 1회(명령 1B + 데이터 6B)당 시간, µs — 낮을수록 좋다 (n=1000, 3회 중앙값)</text>
<line x1="70" y1="310.0" x2="665" y2="310.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="314.0" text-anchor="end" font-size="12">0</text>
<line x1="70" y1="266.7" x2="665" y2="266.7" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="270.6666666666667" text-anchor="end" font-size="12">50</text>
<line x1="70" y1="223.3" x2="665" y2="223.3" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="227.33333333333331" text-anchor="end" font-size="12">100</text>
<line x1="70" y1="180.0" x2="665" y2="180.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="184.0" text-anchor="end" font-size="12">150</text>
<line x1="70" y1="136.7" x2="665" y2="136.7" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="140.66666666666666" text-anchor="end" font-size="12">200</text>
<line x1="70" y1="93.3" x2="665" y2="93.3" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="97.33333333333331" text-anchor="end" font-size="12">250</text>
<line x1="70" y1="50.0" x2="665" y2="50.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="64" y="54.0" text-anchor="end" font-size="12">300</text>
<line x1="70" y1="310" x2="665" y2="310" stroke="currentColor"/>
<rect x="92.2" y="287.3" width="34" height="22.7" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="109.16666666666667" y="282.29333333333335" text-anchor="middle" font-size="12">26</text>
<rect x="132.2" y="289.5" width="34" height="20.5" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="149.16666666666669" y="284.46" text-anchor="middle" font-size="12">24</text>
<rect x="172.2" y="292.6" width="34" height="17.4" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="189.16666666666669" y="287.58" text-anchor="middle" font-size="12">20</text>
<rect x="212.2" y="148.8" width="34" height="161.2" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="229.16666666666669" y="143.79999999999998" text-anchor="middle" font-size="12">186</text>
<text x="169.16666666666669" y="330" text-anchor="middle" font-size="12">완료=즉시, 펌프 RT</text>
<rect x="290.5" y="287.5" width="34" height="22.5" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="307.50000000000006" y="282.46666666666664" text-anchor="middle" font-size="12">26</text>
<rect x="330.5" y="287.3" width="34" height="22.7" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="347.50000000000006" y="282.29333333333335" text-anchor="middle" font-size="12">26</text>
<rect x="370.5" y="288.5" width="34" height="21.5" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="387.50000000000006" y="283.50666666666666" text-anchor="middle" font-size="12">25</text>
<rect x="410.5" y="256.3" width="34" height="53.7" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="427.50000000000006" y="251.26666666666665" text-anchor="middle" font-size="12">62</text>
<text x="367.50000000000006" y="330" text-anchor="middle" font-size="12">완료=즉시, 펌프 일반</text>
<rect x="488.8" y="80.3" width="34" height="229.7" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="505.83333333333337" y="75.33333333333331" text-anchor="middle" font-size="12">265</text>
<rect x="528.8" y="101.5" width="34" height="208.5" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="545.8333333333334" y="96.47999999999999" text-anchor="middle" font-size="12">241</text>
<rect x="568.8" y="124.8" width="34" height="185.2" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="585.8333333333334" y="119.79333333333335" text-anchor="middle" font-size="12">214</text>
<rect x="608.8" y="72.7" width="34" height="237.3" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="625.8333333333334" y="67.70666666666665" text-anchor="middle" font-size="12">274</text>
<text x="565.8333333333334" y="330" text-anchor="middle" font-size="12">완료=hrtimer IRQ, 펌프 RT</text>
<rect x="80" y="350" width="12" height="12" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="98" y="360" text-anchor="start" font-size="12">write_then_read</text>
<rect x="231.8461538461538" y="350" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="249.8461538461538" y="360" text-anchor="start" font-size="12">spi_sync</text>
<rect x="335.23076923076917" y="350" width="12" height="12" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="353.23076923076917" y="360" text-anchor="start" font-size="12">묶은 메시지</text>
<rect x="450.1538461538461" y="350" width="12" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="468.1538461538461" y="360" text-anchor="start" font-size="12">spi_async</text>
<text x="80" y="388" text-anchor="start" font-size="12">QEMU TCG(에뮬레이션) 측정 — 절대값보다 상대 크기만 볼 것</text>
</svg>
```
그림 7 — 위 측정의 3회 중앙값(µs/읽기). QEMU TCG라 절대값은 실제 SoC보다 수십 배 크다. 볼 것은 막대 사이의 비율이다.

### 9.2 해석

- **`rt=1`에서 `spi_async`가 가장 느리다(186 µs vs 24 µs).** 펌프 kthread가 SCHED_FIFO라 `spi_async`가 큐에 넣고 kthread를 깨우는 순간 RT 스레드가 제출자를 **선점**한다. 메시지 하나 처리 → 큐 빔 → 잠듦 → 제출자 재개 → 다음 메시지 → 다시 깨움… 파이프라인이 아니라 "메시지마다 문맥 전환 2번"이 된다. `rt=0`이면 제출자가 몇 개를 쌓을 시간이 생겨 62 µs로 줄지만, 그래도 sync보다 느리다.
- **sync가 빠른 이유**는 §5의 fast path다. 통계를 보면 `spi_sync=12008`, `spi_sync_immediate=12008` — sync는 **전부** 호출자 문맥에서 바로 돌았고 kthread를 한 번도 거치지 않았다. `rt=0` 실행에서는 `6005` 중 `6001`만 immediate였다: async 버스트 직후 큐가 아직 비지 않았을 때 들어온 sync 4개가 큐로 돌았다(순서 보장 규칙).
- **묶은 메시지(c)**가 대체로 가장 빠르지만 이득이 크지 않다(20 vs 24 µs). 이 컨트롤러에서는 메시지 오버헤드보다 transfer당 오버헤드(CS 토글, `cs_change` 지연, 통계, tracepoint)가 크기 때문이다. 진짜 하드웨어에서 묶기의 진짜 이득은 **DMA 설정과 IRQ를 한 번으로** 줄이고 **버스 점유를 끊김 없이** 하는 데서 나온다.
- **`irq_mode=1`**에서는 모든 방법이 200 µs대로 수렴한다. transfer마다 hrtimer 완료를 기다리는 시간(TCG의 타이머 해상도·지연)이 지배하기 때문이다. 실제 보드에서도 "transfer 하나 = IRQ 하나"인 컨트롤러라면 작은 transfer를 많이 쓰는 설계가 IRQ 지연에 묶인다. 그래서 진짜 컨트롤러 드라이버들은 작은 transfer를 PIO 폴링으로 처리하고 큰 것만 DMA+IRQ로 한다(`can_dma`의 길이 임계값).
- 결론: **"async = 빠르다"는 신화다.** `spi_async`의 가치는 처리량이 아니라 (1) atomic 문맥에서 제출 가능, (2) 제출자가 다른 일을 하는 동안 전송이 진행되는 **겹치기(overlap)**, (3) 하드웨어 IRQ 핸들러에서 바로 다음 읽기를 거는 저지연 체인이다. 제출자가 바로 기다릴 거라면 `spi_sync`가 낫다.

---

## 10. 커널 안에서는 무슨 일이 일어나나

### 10.1 등록 경로 — 컨트롤러 하나가 장치 세 개를 낳기까지

```text
platform_driver h7_fakectlr .probe(pdev)                 (G3: DT 노드 h7-spi와 매칭)
  devm_spi_alloc_host(dev, sizeof(priv))                 struct spi_controller + priv 한 덩어리
  ... 필드 채우기 (mode_bits, num_chipselect, set_cs, transfer_one ...)
  devm_spi_register_controller()
    spi_register_controller()
      spi_get_gpio_descs()           cs-gpios 읽기, num_chipselect 늘리기, mode_bits에 CS_HIGH 추가
      bus_num 결정 (DT alias 또는 동적)  → device_add(&ctlr->dev)  "spi0" (/sys/class/spi_master/spi0)
      spi_controller_initialize_queue()  kthread_run_worker(0, "spi0")  (rt면 sched_set_fifo)
      of_register_spi_devices()      자식 노드마다:
        of_register_spi_device()
          spi_alloc_device()          struct spi_device
          of_spi_parse_dt()           reg → chip_select, spi-cpol/cpha → mode, spi-max-frequency
          spi_add_device()
            spi_setup()               ctlr->setup(), CS를 deassert로 강제 (spi_set_cs(..., false, true))
            device_add()              → bus 매칭 spi_match_device() → spi_probe() → sdrv->probe(spi)
```

### 10.2 전송 경로 — `spi_sync()` 한 번

```text
spi_sync(spi, msg)                                    (process 문맥, 슬립 가능)
  mutex_lock(&ctlr->bus_lock_mutex)
  __spi_sync()
    __spi_validate()        speed_hz/bits_per_word 기본값 채우기, len 검사, mode_bits 검사
    spi_maybe_optimize_message()
    queue_empty && !must_async ?
      예  → __spi_transfer_message_noqueue()  → __spi_pump_transfer_message() (호출자 문맥)
      아니오 → __spi_queued_transfer() + wait_for_completion()   (kthread "spi0"가 처리)
__spi_pump_transfer_message()
  ctlr->prepare_transfer_hardware()   (busy가 아니었을 때만)
  spi_map_msg()             can_dma면 tx/rx 버퍼 dma_map_sg
  ctlr->transfer_one_message()  = spi_transfer_one_message() (기본)
    spi_set_cs(spi, true)   → ctlr->set_cs(spi, !enable)  또는 gpiod_set_value_cansleep()
    각 xfer:
      trace_spi_transfer_start
      ctlr->transfer_one()  → 0이면 끝 / 양수면 spi_transfer_wait() (timeout 계산)
                               └ 컨트롤러 IRQ: spi_finalize_current_transfer() → complete()
      trace_spi_transfer_stop
      spi_transfer_delay_exec(), cs_change 처리 (CS 토글 또는 마지막이면 유지)
    spi_set_cs(spi, false)  (마지막 cs_change가 없으면)
    spi_finalize_current_message()
      spi_unmap_msg(), 통계, trace_spi_message_done
      msg->complete(msg->context)    sync면 complete(&done), async면 드라이버 콜백
```

함수 위치(6.18 `drivers/spi/spi.c`): `spi_transfer_one_message` 1596행, `spi_finalize_current_transfer` 1716행, `__spi_pump_transfer_message` 1729행, `__spi_pump_messages` 1843행, `spi_finalize_current_message` 2143행, `spi_async` 4460행, `spi_sync` 4610행, `spi_bus_lock` 4659행.

### 10.3 `set_cs`의 레벨 의미 — 소스로 한 번 더

```c
/* drivers/spi/spi.c spi_set_cs(), 1063행~ (발췌) */
	spi->controller->last_cs_mode_high = spi->mode & SPI_CS_HIGH;
	if (spi->controller->last_cs_mode_high)
		enable = !enable;
	...
	if (spi_is_csgpiod(spi)) {
		...	spi_toggle_csgpiod(spi, idx, enable, activate);
	} else if (spi->controller->set_cs) {
		spi->controller->set_cs(spi, !enable);
	}
```

`enable`(= assert)를 받아 CS_HIGH면 뒤집고, 컨트롤러에는 `!enable`을 준다. active-low 장치 assert → `set_cs(spi, false)` = "선을 LOW로". 예제 6의 `CS0 line LOW (assert)`가 그 결과다. 또 `spi_set_cs()` 맨 앞에서 "직전과 같은 CS 상태면 아무것도 안 한다"(`last_cs` 캐시)는 최적화가 있어, 같은 장치에 연속 메시지를 보내도 불필요한 `set_cs` 호출이 줄어든다.

### 10.4 드라이버를 쓸 때 다시 만나는 지점

| 이 노트의 개념 | 다시 만나는 곳 |
|---|---|
| `spi_driver` probe, devm, drvdata | G3 (platform driver와 같은 생명주기) |
| regmap-spi | H2 (캐시, `regmap_field`, I2C/SPI 공용 드라이버) |
| GPIO CS, data-ready IRQ 핀 | H4 (gpiod), F6 (threaded IRQ에서 `spi_sync`) |
| DMA-safe 버퍼, `can_dma`, `dma_map_sg` | H9 (streaming DMA, 캐시 관리, swiotlb) |
| IMU·ADC를 표준 인터페이스로 | I (IIO: `iio_priv` 안 `____cacheline_aligned` 버퍼, triggered buffer) |
| 메시지 펌프 kthread, RT 우선순위 | F(스케줄링·kthread), M(지연 측정) |
| spi-mem (QSPI 플래시) | MTD/플래시 노트 — 일반 SPI 메시지가 아니라 `spi_mem_op` 인터페이스 |

---

## 11. Embedded 실무에서 다시 보기

### 11.1 IMU data-ready 인터럽트 → SPI 읽기

센서 키트의 IMU는 1 kHz로 data-ready 핀을 올린다. hardirq 핸들러에서는 `spi_sync`를 못 부른다(슬립). 정석은 **threaded IRQ**(F6)다.

```c
static irqreturn_t imu_thread_fn(int irq, void *data)   /* 커널 스레드 문맥 — 슬립 OK */
{
	struct imu_priv *p = data;
	int ret;

	mutex_lock(&p->lock);
	ret = spi_write_then_read(p->spi, &p->cmd, 1, p->buf, 14);  /* accel+temp+gyro 한 번에 */
	if (!ret)
		imu_push_sample(p, p->buf);       /* IIO buffer 등 */
	mutex_unlock(&p->lock);
	return IRQ_HANDLED;
}
/* probe: devm_request_threaded_irq(dev, spi->irq, NULL, imu_thread_fn, IRQF_ONESHOT, "imu", p); */
```

(패턴 예시 — 이 노트에서 따로 실행하지 않았다.) 더 낮은 지연이 필요하면 hardirq에서 미리 준비한 메시지로 `spi_async`를 걸고, 콜백에서 타임스탬프와 함께 링버퍼에 넣는다. 그때 콜백은 슬립 금지(§5.2)이고 메시지·버퍼는 완료까지 살아 있어야 한다. 1 kHz 이상에서 지터가 문제면 컨트롤러 펌프를 RT로(드라이버가 `spi->rt = true` 후 `spi_setup()` — CAN 컨트롤러 `mcp251xfd`가 이렇게 한다) 올리는 것도 방법이다. 단 §9에서 본 것처럼 RT 펌프는 처리량을 오히려 깎을 수 있으니 측정하고 정한다.

### 11.2 "WHO_AM_I가 0x00/0xFF만 나와요" — 5분 진단

1. `ls /sys/bus/spi/devices/` — `spiB.C`가 있나? 없으면 DT(컨트롤러 노드 `status`, 자식 `reg`) 또는 컨트롤러 드라이버 probe 실패(`dmesg`, `/sys/kernel/debug/devices_deferred` — G3).
2. `cat /sys/bus/spi/devices/spiB.C/modalias`, `readlink .../driver` — 드라이버가 붙었나.
3. `echo 1 > /sys/kernel/tracing/events/spi/enable` 후 한 번 읽기 — `spi_transfer_stop`의 `rx=`가 0x00이면 MISO가 끊겼거나 칩 전원/리셋(H3), 0xFF면 풀업만 보이는 것(CS가 안 내려감? 다른 CS?), **값이 그럴듯하게 틀리면(0x38, 0xE2) 모드·비트 밀림**.
4. `spi_set_cs` + `gpio_value` 이벤트로 CS가 실제로 움직였는지. GPIO CS인데 `gpio_value`가 없으면 `cs-gpios` 인덱스가 틀렸다.
5. 그래도 모르겠으면 그때 스코프. trace의 transfer 경계와 스코프의 CS 구간을 맞춰 본다.

### 11.3 SPI 디스플레이·대용량 전송

320x240 RGB565 프레임은 150 KB다. 컨트롤러마다 한 transfer 최대 길이가 있어(`ctlr->max_transfer_size` 콜백, `spi_max_transfer_size(spi)`), 넘으면 코어가 자동 분할하거나(`transfers_split_maxsize` 통계) 드라이버가 나눠야 한다. 프레임 버퍼는 `vmalloc`이 아니라 DMA 가능한 메모리(또는 코어의 vmalloc→sg 매핑에 기대되 캐시 정렬 유지)로, transfer는 크게, `cs_change` 없이 한 메시지로 보내야 DMA 한 번에 끝난다.

### 11.4 같은 버스의 장치끼리 싸울 때

IMU(10 MHz, 1 kHz 폴링)와 SPI 플래시(대용량 읽기)가 한 컨트롤러에 있으면, 플래시의 긴 메시지 동안 IMU 메시지는 큐에서 기다린다(메시지는 원자적). 증상은 IMU 샘플 지터. 해법: 플래시 transfer를 잘게 쪼개 메시지 사이에 IMU가 끼어들 틈을 만들거나, 보드 설계에서 지연 민감 장치를 별도 컨트롤러로 분리한다. trace의 `spi_message_submit` → `spi_message_start` 간격이 이 대기 시간이다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 스택 버퍼를 `spi_sync`/`spi_async`에 넘김 | 가끔 데이터 깨짐, 옆 변수 값 변함, 테스트에선 멀쩡 | DMA와 CPU가 캐시라인 공유, VMAP_STACK | kmalloc 버퍼, `____cacheline_aligned`, 또는 `spi_write_then_read` |
| 명령 두 개를 한 메시지에 넣고 `cs_change` 안 붙임 | 두 번째 값이 0x00/엉뚱, 에러는 없음 | CS가 계속 LOW라 칩이 두 번째 명령을 데이터로 봄 | 경계 transfer에 `cs_change = 1`, 또는 메시지 분리 |
| DT 모드 틀림 (`spi-cpha` 누락 등) | 값이 1비트 밀려 그럴듯하게 틀림 | 샘플 엣지 불일치 | 데이터시트 모드 확인, `spi-cpol`/`spi-cpha` |
| DT에 `compatible = "spidev"` | `/dev/spidev*` 안 생김 또는 `spidev listed directly in DT is not supported` | spidev가 DT 직접 기술을 거부 | 진짜 칩 compatible + 커널 드라이버, 또는 `driver_override` |
| `spi_async` 콜백에서 슬립(`mutex_lock`, `spi_sync`, `msleep`) | `BUG: sleeping function called from invalid context` 또는 데드락 | 콜백 문맥은 atomic일 수 있고 펌프 스레드 안일 수 있음 | 콜백은 complete/플래그/workqueue만 |
| `spi_async`에 지역 변수 `spi_message` 넘기고 함수 반환 | 랜덤 크래시, 리스트 손상 | 메시지·transfer·버퍼는 완료 콜백까지 살아 있어야 함 | 힙/private 구조체에 두기 |
| hardirq 핸들러에서 `spi_sync` | sleeping in atomic BUG | `spi_sync`는 슬립 | threaded IRQ 또는 `spi_async` |
| 16-bit ADC를 `bits_per_word = 8`로 읽고 바이트 조립 | 바이트 순서 뒤집힘 | 8-bit는 선 순서 그대로 바이트 배열, 16-bit는 CPU 엔디언 `u16` | 하나로 통일, `be16_to_cpu` 등 명시 |
| `of_match_table`만 있고 `id_table` 없음 | `SPI driver X has no spi_device_id for Y` 경고, 모듈 자동 로딩 실패 | modalias는 `spi:chip` | `spi_device_id`에 `"chip"` 추가 |
| 컨트롤러가 지원 안 하는 모드 비트 요청 | `setup: unsupported mode bits` + `-EINVAL` | `ctlr->mode_bits`에 없음 | 다른 컨트롤러/GPIO 비트뱅잉, 또는 소프트웨어로 비트 반전 |

---

## 13. 면접에서 이렇게 말한다

**Q.** "Walk me through the Linux SPI subsystem. What are the main objects?"

**A.** 세 객체다. SoC의 SPI 블록을 나타내는 `spi_controller`(컨트롤러 드라이버가 `transfer_one`·`set_cs`·`mode_bits`를 채워 등록), 버스 위 칩 하나인 `spi_device`(DT 자식 노드에서 코어가 만듦: `reg`가 CS, `spi-cpol`/`spi-cpha`가 모드, `spi-max-frequency`), 칩을 아는 `spi_driver`(`probe(spi)`). 그 사이 SPI core가 메시지 큐와 펌프 kthread, CS 규칙, DMA 매핑을 맡는다. 프로토콜 드라이버는 어느 SoC인지 모르고, 컨트롤러 드라이버는 어떤 칩인지 모른다.

> "There are three objects. The spi_controller represents the SoC's SPI block; its driver fills in capabilities like mode_bits and bits_per_word_mask plus callbacks like transfer_one and set_cs, and registers it. Each chip on the bus is a spi_device, created by the core from a DT child node — reg is the chip select, spi-cpol and spi-cpha give the mode, spi-max-frequency the clock. A spi_driver binds to the spi_device and talks to the chip in probe and later. In between, the SPI core owns the message queue, the pump kthread, chip-select handling and DMA mapping, so protocol drivers are portable across SoCs."

**꼬리 질문**: "compatible이 `vendor,chip`인데 왜 `id_table`도 필요하죠?" → modalias가 `spi:chip`이라 모듈 자동 로딩과 코어 경고 때문.

**Q.** "What's the difference between spi_transfer and spi_message, and what does cs_change do?"

**A.** transfer는 tx/rx 버퍼 한 쌍과 길이, 메시지는 transfer의 리스트이자 원자적 단위다. 기본으로 메시지 시작에 CS를 내리고 끝에 올린다. 중간 transfer에 `cs_change`를 붙이면 그 뒤에서 CS를 잠깐 올려 칩에게 "새 명령"을 알리고, 마지막 transfer에 붙이면 메시지가 끝나도 CS를 유지한다. 직접 해 보니 `cs_change` 없이 명령 두 개를 한 메시지에 넣어도 `ret=0`으로 성공하지만 데이터는 전부 0이었다 — 코어는 프로토콜을 모르니 CS 경계는 드라이버 책임이다.

> "A transfer is one tx/rx buffer pair with a length; a message is a list of transfers and is atomic on the bus. By default chip select is asserted for the whole message. Setting cs_change on a middle transfer briefly deasserts CS after it, which most chips treat as the end of a command; on the last transfer it keeps CS asserted after the message. I tested this with an emulated IMU: two register reads in one message without cs_change returned success but all-zero data, because the chip saw the second command byte as data. The core can't catch that — CS framing is the driver's job."

**Q.** "spi_sync vs spi_async — when do you use which, and in what context does the completion callback run?"

**A.** `spi_sync`는 슬립하므로 process 문맥·threaded IRQ에서, 대부분의 경우 이것. 큐가 비어 있으면 호출자 스레드에서 바로 전송하는 fast path가 있어서 빠르다. `spi_async`는 spinlock만 잡고 큐잉하므로 atomic 문맥에서도 되고, 완료 콜백은 슬립하면 안 된다. 제 실험에서는 콜백이 펌프 kthread에서 불렸지만, 컨트롤러 드라이버가 IRQ에서 메시지를 끝내면 hardirq에서 불릴 수도 있으니 계약은 "atomic으로 가정"이다. 그리고 async가 처리량상 빠르다는 건 오해다 — RT 펌프에서는 메시지마다 선점이 생겨 sync보다 8배 느렸다.

> "spi_sync sleeps, so it's for process context or threaded IRQ handlers, and it's what you use most of the time. If the queue is empty it even runs the transfer directly in the caller's thread, skipping the pump kthread. spi_async only takes a spinlock to queue the message, so it can be called from atomic context, but the message and buffers must live until the completion callback, and the callback must not sleep. In my trace the callback ran in the spi0 pump kthread, but a controller that finalizes messages from its IRQ handler would call it in hardirq context, so you treat it as atomic. Also, async isn't automatically faster: with an RT pump thread I measured it slower than sync because every submission preempted the submitter."

**Q.** "Why can't you pass a stack buffer to spi_sync?"

**A.** SPI 컨트롤러가 DMA를 쓸 수 있는데, non-coherent SoC에서 DMA 버퍼는 캐시라인을 혼자 써야 한다. 스택 버퍼는 다른 지역 변수와 라인을 공유해서, DMA 도중 CPU가 옆 변수를 쓰면 dirty 라인이 evict되며 DMA 데이터를 덮거나, unmap 때 invalidate가 CPU 쓰기를 날린다. 게다가 VMAP_STACK이면 스택이 vmalloc 주소라 물리 연속도 아니다. 제가 확인해 보니 코어는 스택 버퍼를 아무 경고 없이 DMA 매핑했다. 그래서 kmalloc 버퍼, 구조체 끝의 `____cacheline_aligned` 버퍼, 또는 내부 복사하는 `spi_write_then_read`를 쓴다.

> "Because the controller may DMA into it. On non-coherent SoCs a DMA buffer has to own its cache lines; a stack buffer shares lines with other locals, so a CPU write to a neighbour during the transfer can dirty the line and its eviction overwrites the DMA data, or the invalidate at unmap discards the CPU's write. With VMAP_STACK the stack is also vmalloc memory, not physically contiguous. I checked on 6.18: the SPI core will happily DMA-map a stack buffer without any warning, and it works in QEMU, which is exactly why the bug is nasty. Use kmalloc'd buffers, a ____cacheline_aligned buffer at the end of the private struct, or spi_write_then_read, which copies into its own buffer."

**Q.** "How would you write an SPI controller driver? What must transfer_one do?"

**A.** platform driver probe에서 `devm_spi_alloc_host`로 컨트롤러를 만들고 `mode_bits`, `bits_per_word_mask`, 속도 범위, `num_chipselect`, `use_gpio_descriptors`를 채운 뒤 `set_cs`, `transfer_one`(필요하면 `prepare_transfer_hardware`, `can_dma`)을 등록한다. `transfer_one`은 xfer의 속도·워드 길이로 블록을 설정하고 FIFO/DMA를 시작한 뒤, 끝났으면 0, 진행 중이면 양수를 반환하고 완료 IRQ에서 `spi_finalize_current_transfer()`를 부른다. CS, `cs_change`, 지연, 통계는 코어가 한다. `set_cs`의 인자는 전기적 레벨이라는 점이 함정이다.

> "In the platform probe I allocate it with devm_spi_alloc_host, describe the hardware — mode_bits, bits_per_word_mask, min and max speed, num_chipselect, use_gpio_descriptors for cs-gpios — and provide set_cs and transfer_one, plus prepare_transfer_hardware or can_dma if needed, then devm_spi_register_controller. transfer_one programs clock and word size from the transfer, starts the FIFO or DMA, and returns 0 if it's already done or a positive value if it's in flight; the IRQ handler then calls spi_finalize_current_transfer. The core handles queueing, chip select, cs_change, delays and statistics. One gotcha: set_cs gets the electrical line level, already inverted for active-low."

**Q.** "When is spidev appropriate, and how do you bind it properly?"

**A.** bring-up, 공장 테스트, 프로토타입. IRQ가 없고 다른 서브시스템과 묶을 수 없으니 제품 드라이버는 아니다. DT에 `compatible = "spidev"`는 커널이 거부한다 — DT는 하드웨어를 기술해야 하니까. 진짜 칩 compatible을 쓰고, 커널 드라이버가 없으면 `driver_override`에 `spidev`를 써서 bind한다. 그러면 `/dev/spidevB.C`가 생기고 `SPI_IOC_MESSAGE(n)`으로 transfer 배열을 한 번에 보낸다.

> "For bring-up, factory test tools and quick prototypes — not for product drivers, since spidev has no interrupt support and can't plug into IIO or other subsystems. You can't put compatible "spidev" in the device tree anymore; the kernel rejects it because DT describes hardware and spidev is a Linux implementation detail. You describe the real chip, and if there's no kernel driver yet you write spidev to the device's driver_override in sysfs and bind it. Then /dev/spidevB.C appears and you send arrays of spi_ioc_transfer with SPI_IOC_MESSAGE(n), one chip-select frame per ioctl unless you set cs_change."

**Q.** "An SPI IMU returns plausible but wrong values. How do you debug it?"

**A.** 값이 0x00/0xFF가 아니라 그럴듯하게 틀리면 모드(CPOL/CPHA) 불일치로 1비트 밀린 경우가 많다. 먼저 `/sys/bus/spi/devices`로 장치·드라이버 바인딩을 보고, spi tracepoint로 tx/rx 바이트와 CS 경계를 본다 — 명령 바이트가 맞는지, `cs_change`가 필요한 곳에 있는지. 그다음 DT의 `spi-cpol`/`spi-cpha`와 데이터시트를 대조하고, 마지막에 스코프로 엣지를 확인한다. 실제로 이 노트에서 mode를 틀리게 하자 ADC 응답의 채널 필드가 3이 아니라 1로 나왔다.

> "Plausible-but-wrong usually means a one-bit shift from a CPOL/CPHA mismatch, whereas all-zeros or all-ones points at wiring, power or chip select. I first check the device exists and is bound under /sys/bus/spi/devices, then enable the spi tracepoints to see the exact tx and rx bytes and where CS toggles — that tells me whether the command byte and the CS framing are right. Then I compare spi-cpol and spi-cpha in the DT with the datasheet, and only then put a scope on it to confirm the sampling edge."

---

## 14. 직접 해보기

1. (개념) 같은 버스에 IMU(mode 3)와 ADC(mode 0)가 있다. 컨트롤러는 모드를 언제 바꾸나? 누가 바꾸나?
   정답/힌트: 모드는 `spi_device`별 설정이고, 컨트롤러 드라이버가 `setup()`에서 저장해 두거나 `transfer_one()`/`prepare_message()`에서 그 장치의 `spi->mode`로 레지스터를 다시 쓴다. CS를 내리기 **전에** 클럭 유휴 레벨이 맞아야 하므로, 실제 드라이버는 CS assert 전에 CPOL을 설정한다.
2. (코드) `h7_two_reads()`를 `cs_change` 없이 고치되, 메시지를 두 개로 나누지 말고 IMU의 auto-increment를 이용해 **명령 하나**로 WHO_AM_I(0x75)와 그 뒤 레지스터를 읽어라. 왜 ACCEL(0x3B)과 WHO_AM_I는 한 번에 못 읽나?
   정답/힌트: 0x3B부터 0x75까지 59바이트를 읽으면 되긴 한다(auto-increment). 주소가 멀면 낭비라 보통 메시지(또는 CS 구간)를 나눈다.
3. (코드) `h7_fakectlr`에 `SPI_LSB_FIRST`를 지원하도록 `mode_bits`를 늘리고, `transfer_one`에서 비트 반전(`bitrev8`)으로 흉내 내라. 예제 11의 `WR_MODE32(SPI_LSB_FIRST)`가 성공하는지 확인.
   정답/힌트: `#include <linux/bitrev.h>`, `if (spi->mode & SPI_LSB_FIRST) v = bitrev8(v);`.
4. (관찰) `t4.sh`에서 `echo "async 2"` 대신 `bench 10`을 trace하고, `spi_sync_immediate`가 아닌 sync가 생기는 순간을 trace에서 찾아라(실행 주체가 `spi0`인 sync 메시지).
   정답/힌트: async 버스트 직후의 sync 메시지는 `spi_message_submit`만 `sh`이고 나머지는 `spi0`이다.
5. (코드) `h7_imu`에 data-ready 흉내를 넣어 보라: hrtimer(1 kHz)에서 `spi_async`로 가속도를 읽고, 콜백에서 `ktime_get_ns()`와 함께 링버퍼(kfifo)에 넣은 뒤 debugfs로 내보낸다. 콜백에서 `kfifo_in`이 왜 안전한가?
   정답/힌트: 생산자 하나·소비자 하나인 kfifo는 락 없이 안전하다. 콜백은 슬립하지 않는다.
6. (관찰) `rt=1`로 넣고 `cat /proc/$(pidof spi0)/sched`의 policy를 확인한 뒤, `rt=0` 컨트롤러에서 `h7_imu`의 probe에 `spi->rt = true; spi_setup(spi);`를 넣어 펌프가 RT로 바뀌는지 확인하라.
   정답/힌트: 6.18 `spi_setup()` 3981행 — `spi->rt && !ctlr->rt`이면 `spi_set_thread_rt()`가 펌프를 SCHED_FIFO로 올린다(소스 확인만 했고 이 노트에서 실행하지는 않았다).
7. (개념) spidev의 `bufsiz` 모듈 파라미터 기본값은? 8 KB를 한 번에 읽으려면?
   정답/힌트: 4096(6.18 `spidev.c` 86행). `modprobe spidev bufsiz=8192` 또는 여러 transfer로 나눈다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| `spi_controller` | SPI 호스트(마스터) 컨트롤러 | SoC SPI 블록. 옛 이름 `spi_master`, sysfs 클래스는 아직 `spi_master` |
| `spi_device` | 버스 위 칩 하나 | CS·모드·속도를 가진다. 이름 `spiB.C` |
| `spi_driver` | 프로토콜(클라이언트) 드라이버 | 칩을 안다. `probe(spi)` |
| `spi_transfer` | 버퍼 한 쌍 | tx/rx, len, speed, bpw, cs_change, delay |
| `spi_message` | transfer 리스트 | 원자적 단위, 기본으로 CS 한 구간 |
| `cs_change` | CS 동작 뒤집기 | 중간이면 토글, 마지막이면 유지 |
| CPOL / CPHA | 클럭 극성 / 위상 | mode 0~3, DT `spi-cpol`·`spi-cpha` |
| `mode_bits` | 컨트롤러 지원 모드 비트 | 없는 비트 요청 시 `spi_setup` `-EINVAL` |
| `transfer_one` | transfer 하나 실행 콜백 | 0 = 끝, 양수 = 진행 중 → `spi_finalize_current_transfer` |
| 메시지 펌프 | 큐를 비우는 kthread_worker | 이름 = 컨트롤러 이름(`spi0`), `rt`면 SCHED_FIFO |
| fast path | `spi_sync`의 호출자 문맥 전송 | 큐가 비었을 때. 통계 `spi_sync_immediate` |
| `spi_write_then_read` | 복사형 동기 헬퍼 | 스택 버퍼 OK, 작은 크기용 |
| spidev | 범용 유저 공간 드라이버 | `/dev/spidevB.C`, `SPI_IOC_MESSAGE(n)` |
| `driver_override` | sysfs의 강제 매칭 | 매칭 1순위. spidev를 붙일 때 |
| GPIO CS | GPIO로 만든 칩 셀렉트 | DT `cs-gpios`, 코어가 gpiod로 직접 토글 |
| DMA-safe | DMA에 넘겨도 되는 버퍼 | kmalloc, 캐시라인 단독, 스택·vmalloc 아님 |
| `____cacheline_aligned` | 캐시라인 정렬 속성 | 구조체 안 DMA 버퍼 앞에 |
| `ARCH_DMA_MINALIGN` | DMA 안전 정렬 | arm64 128B |
| VMAP_STACK | vmalloc 커널 스택 | 스택 주소는 `virt_addr_valid()` 거짓 |
| `spi_bus_lock` | 버스 독점 | 여러 메시지를 끼어들기 없이 (mmc_spi) |

---

## 16. 요약 & 체크리스트

Linux SPI는 bare-metal의 "CS 내리고 바이트 밀고 CS 올리기"를 세 층으로 나눈다. 칩을 아는 `spi_driver`는 `spi_transfer`를 엮은 `spi_message`를 `spi_sync`(대부분)나 `spi_async`(atomic·스트리밍)로 넘기고, SPI core는 메시지 큐와 펌프 kthread로 줄을 세우며 CS·`cs_change`·지연·DMA 매핑·통계·tracepoint를 처리하고, 선을 아는 컨트롤러 드라이버는 `transfer_one`과 `set_cs`(전기적 레벨!)만 구현한다. 장치는 DT 자식 노드에서 만들어지고(`reg` = CS, `spi-cpol`/`spi-cpha` = 모드), 유저 공간 실험은 spidev를 `driver_override`로 붙여서 한다(DT에 "spidev" 금지). 버퍼는 DMA-safe여야 하고(스택 금지), 모드가 틀리거나 CS 경계가 틀려도 에러는 나지 않으니 spi tracepoint로 바이트와 CS를 직접 본다. `spi_async`는 빠르기 때문이 아니라 문맥과 겹치기 때문에 쓴다.

- [ ] `spi_controller`·`spi_device`·`spi_driver`와 DT 자식 노드의 관계를 그림으로 그릴 수 있다
- [ ] DT 속성(`reg`, `spi-max-frequency`, `spi-cpol`, `spi-cpha`, `spi-cs-high`, `cs-gpios`)이 어느 필드가 되는지 말할 수 있다
- [ ] mode 0~3을 CPOL/CPHA와 샘플 엣지로 설명하고, 모드 불일치가 "그럴듯하게 틀린 값"을 만드는 이유를 말할 수 있다
- [ ] `spi_transfer`/`spi_message`로 레지스터 읽기를 짜고, `cs_change`가 필요한 곳을 고를 수 있다
- [ ] `spi_sync`의 fast path와 `spi_async`의 펌프 경로를 구분하고, 완료 콜백의 제약을 설명할 수 있다
- [ ] `spi_write_then_read`/`spi_w8r8`/regmap-spi 중 상황에 맞는 것을 고를 수 있다
- [ ] `transfer_one` + `set_cs` + 완료 IRQ 구조의 컨트롤러 드라이버 뼈대를 쓸 수 있다
- [ ] 스택 버퍼가 DMA에 위험한 이유를 캐시라인과 VMAP_STACK으로 설명하고, 세 가지 해법을 쓸 수 있다
- [ ] "spidev" compatible이 거부되는 이유와 `driver_override`로 spidev를 붙이는 명령을 쓸 수 있다
- [ ] `SPI_IOC_MESSAGE(n)`과 `SPI_IOC_WR_MODE32`/`BITS_PER_WORD`/`MAX_SPEED_HZ`로 유저 공간 테스트 툴을 짤 수 있다
- [ ] spi tracepoint와 `/sys/bus/spi/devices/*/statistics`로 SPI 문제를 진단할 수 있다
- [ ] 처리량 측정 결과로 "async가 항상 빠르지는 않다"를 근거와 함께 말할 수 있다

---

## 참고 자료

- docs.kernel.org — "Overview of Linux kernel SPI support": https://docs.kernel.org/spi/spi-summary.html
- docs.kernel.org — "SPI userspace API" (spidev): https://docs.kernel.org/spi/spidev.html
- docs.kernel.org — SPI driver API (kernel-doc): https://docs.kernel.org/driver-api/spi.html
- docs.kernel.org — "Dynamic DMA mapping Guide" (스택·vmalloc 금지, 캐시라인): https://docs.kernel.org/core-api/dma-api-howto.html
- Device Tree 바인딩: `Documentation/devicetree/bindings/spi/spi-controller.yaml`, `spi-peripheral-props.yaml` (커널 소스 트리)
- 소스 (6.18 랩 트리 `/var/lab/src/linux-6.18.55`): `include/linux/spi/spi.h`, `include/uapi/linux/spi/spi.h`, `include/uapi/linux/spi/spidev.h`, `drivers/spi/spi.c`, `drivers/spi/spidev.c`, `include/trace/events/spi.h`, `drivers/base/regmap/regmap-spi.c`, `kernel/dma/debug.c`
- elixir.bootlin.com에서 같은 파일: https://elixir.bootlin.com/linux/v6.18/source/drivers/spi/spi.c
- 예제 컨트롤러 드라이버로 읽기 좋은 것: `drivers/spi/spi-imx.c`(i.MX), `drivers/spi/spi-pl022.c`(ARM PrimeCell), `drivers/spi/spi-gpio.c`(비트뱅잉)
- Bootlin "Linux kernel and driver development" 교육 슬라이드 (SPI·I2C 드라이버 장): https://bootlin.com/training/kernel/
