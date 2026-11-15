---
id: ELX-B11
time: 70분
updated: 2026-11-15
---
# ELX-B11 사용자공간 I2C/SPI: i2c-dev와 spidev

## 학습 목표

- `/dev/i2c-N` 의 ioctl 다섯 개(`I2C_SLAVE`, `I2C_SLAVE_FORCE`, `I2C_FUNCS`, `I2C_RDWR`, `I2C_SMBUS`)가 각각 무엇을 하고, 어느 것이 **어댑터의 어떤 능력**을 요구하는지 말할 수 있다.
- `I2C_RDWR` 로 **write-then-read(레지스터 쓰기 → repeated START → 읽기)** 를 한 트랜잭션으로 보내고, 트레이스와 카운터로 "STOP 없이 한 번"이었음을 증명한다.
- i2c-stub이 왜 `I2C_RDWR` 에 `EOPNOTSUPP` 를 돌려주는지 커널 소스로 설명하고, `I2C_FUNCS` 를 보고 SMBus로 물러서는(fallback) 유틸을 짠다. 결과는 BusyBox `i2cget`/`i2cdump` 와 바이트 단위로 대조한다.
- SMBus word 의 바이트 순서, 12-bit 온도값 부호 확장, "STOP 에서 포인터를 잊는 칩" 같은 bring-up 함정을 숫자로 확인한다.
- `struct spi_ioc_transfer` 와 `SPI_IOC_MESSAGE(N)` 를 바르게 채우고, spidev를 실제 보드에서 붙이는 절차를 안다.

## 1. 문제

보드 bring-up 첫날의 질문은 "센서가 살아 있나?"이고, 커널 드라이버가 아직 없을 때 그 답은 `i2cdetect`/`i2cget` 과 작은 C 유틸로 낸다. 면접에서도 "I2C_RDWR 와 SMBus 차이", "repeated START 는 왜", "spidev 는 언제 쓰고 무엇이 위험한가"가 자주 나온다. 펌웨어에서 I2C 컨트롤러 레지스터를 직접 치던 일(COMM-B06, COMM-B07)이 리눅스에서는 **사용자공간 ioctl → i2c-core → 어댑터 드라이버** 로 바뀐다. 이 레슨은 그 경로를 사용자공간 쪽에서 끝까지 따라간다.

> 환경: 모든 출력은 Lima VM `elinux` 안의 **QEMU TCG cortex-a57 -smp 1, 512 MB, Linux 6.18.55-dom** (`/var/lab/out/Image-dom` + `modules-dom` 의 `i2c-stub.ko`, `spidev.ko`) 실측이다. 사용자공간 도구는 랩 initramfs의 **BusyBox 1.37 applet** (`i2cdetect`, `i2cget`, `i2cset`, `i2cdump`, `i2ctransfer`) 을 그대로 썼다(VM의 i2c-tools를 복사하지 않음). 모듈은 게스트 안에서만 올린다. TCG는 실제 CPU가 아니므로 트레이스의 시각은 순서만 의미가 있다.

먼저 해 보고 알게 된 사실 하나가 레슨 구조를 정했다. **i2c-stub은 SMBus 전용 어댑터라서 `I2C_RDWR` 가 동작하지 않는다.**

```text
+ i2ctransfer -y 0 w1@0x48 0x0f r1
i2ctransfer: adapter does not support I2C transfers
```

그래서 게스트에는 어댑터를 두 개 둔다. 둘 다 0x48에 칩이 있다.

| 버스 | 만든 방법 | 어댑터 능력 | 칩 모델 |
|---|---|---|---|
| `i2c-0` STUB | `modprobe i2c-stub chip_addr=0x48` | `.smbus_xfer` 만 (I2C_FUNC_I2C 없음) | 256개 16-bit 셀, 모두 0 |
| `i2c-1` FAKE | `insmod b11_fakei2c.ko` (이 레슨의 **시험 장비**) | `.xfer` (I2C 레벨) + SMBus 에뮬레이션 | 바이트 레지스터 + 내부 포인터, 0x0F = ID 0xB1, 0x00/0x01 = 온도 25.0 °C |

`b11_fakei2c.c` 는 문제가 아니라 계측 장비다. 실제 SoC의 I2C 컨트롤러 드라이버처럼 `struct i2c_msg[]` 를 받는 `.xfer()` 를 구현하고, 몇 번 호출됐는지(`xfer_calls`)와 마지막 호출의 메시지 개수(`last_nmsgs`)를 `/sys/module/b11_fakei2c/parameters/` 로 보여 준다. 어댑터(컨트롤러) 쪽 드라이버 작성은 ELN H5 노트 범위이고, 클라이언트 드라이버는 ELX-M08 에서 다룬다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-weight="bold">사용자공간 → i2c-dev → i2c-core → 어댑터 (이 레슨의 두 버스)</text>
<rect x="20" y="30" width="200" height="40" class="f-acc" stroke="currentColor"/>
<text x="30" y="55" fill="currentColor">i2creg (이 레슨의 유틸)</text>
<rect x="240" y="30" width="250" height="40" class="f-mut" stroke="currentColor"/>
<text x="250" y="55" fill="currentColor">BusyBox i2cget / i2cset / i2cdump</text>
<rect x="510" y="30" width="210" height="40" class="f-mut" stroke="currentColor"/>
<text x="520" y="55" fill="currentColor">BusyBox i2ctransfer</text>
<rect x="20" y="95" width="700" height="40" class="f-mut" stroke="currentColor"/>
<text x="30" y="113" fill="currentColor">/dev/i2c-0, /dev/i2c-1  (drivers/i2c/i2c-dev.c, major 89)</text>
<text x="30" y="129" fill="currentColor" font-size="12">I2C_SLAVE(_FORCE) · I2C_FUNCS · I2C_RDWR · I2C_SMBUS · read()/write()</text>
<rect x="20" y="160" width="330" height="44" class="f-acc" stroke="currentColor"/>
<text x="30" y="179" fill="currentColor">i2c_transfer(msgs[], n)</text>
<text x="30" y="196" fill="currentColor" font-size="12">← I2C_RDWR, read(), write()</text>
<rect x="390" y="160" width="330" height="44" class="f-acc" stroke="currentColor"/>
<text x="400" y="179" fill="currentColor">i2c_smbus_xfer()</text>
<text x="400" y="196" fill="currentColor" font-size="12">← I2C_SMBUS (i2cget, i2cdump ...)</text>
<rect x="20" y="255" width="330" height="48" class="f-ok" stroke="currentColor"/>
<text x="30" y="275" fill="currentColor">i2c-1 FAKE: b11_fakei2c</text>
<text x="30" y="293" fill="currentColor" font-size="12">.xfer = I2C 레벨, funcs = I2C | SMBUS_EMUL</text>
<rect x="390" y="255" width="330" height="48" class="f-warn" stroke="currentColor"/>
<text x="400" y="275" fill="currentColor">i2c-0 STUB: i2c-stub</text>
<text x="400" y="293" fill="currentColor" font-size="12">.smbus_xfer 만, funcs = 0x0c7f0000</text>
<line x1="120" y1="70" x2="120" y2="95" stroke="currentColor"/>
<line x1="365" y1="70" x2="365" y2="95" stroke="currentColor"/>
<line x1="615" y1="70" x2="615" y2="95" stroke="currentColor"/>
<line x1="185" y1="135" x2="185" y2="160" stroke="currentColor"/>
<line x1="555" y1="135" x2="555" y2="160" stroke="currentColor"/>
<line x1="185" y1="204" x2="185" y2="255" stroke="currentColor"/>
<line x1="555" y1="204" x2="555" y2="255" stroke="currentColor"/>
<line x1="470" y1="204" x2="300" y2="255" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="330" y="232" fill="currentColor" font-size="12">.smbus_xfer 없음 → msg 2개로 에뮬레이션</text>
<line x1="250" y1="204" x2="430" y2="255" stroke="currentColor" stroke-dasharray="2 4"/>
<text x="210" y="248" fill="currentColor" font-size="12">✕ EOPNOTSUPP</text>
<text x="10" y="328" fill="currentColor" font-size="12">SMBus → I2C 방향은 core가 흉내 낼 수 있지만, I2C(임의 msg 배열) → SMBus 방향은 불가능하다.</text>
</svg>
```

### Part A — I2C_RDWR 로 write-then-read (FAKE 버스)

- A1. `i2c_rdwr_read(fd, addr, reg, buf, len)` — `i2c_msg` 2개(쓰기 1바이트 = reg, 읽기 len)를 **한 번의** `ioctl(I2C_RDWR)` 로. 성공 0, 실패 `-errno`.
- A2. `i2c_rdwr_write(fd, addr, reg, data, len)` — `[reg, d0, d1, ...]` 메시지 1개. `len > 32` 면 `-EINVAL`.
- A3. `smbus_read_i2c_block(fd, reg, buf, len)` — `I2C_SMBUS` ioctl의 "I2C block read". `block[0]` 에 길이를 넣는다. 두 버스 모두에서 동작해야 한다.
- 검증: ID 레지스터 0x0F = 0xB1, `xfer_calls` 가 1 늘고 `last_nmsgs == 2`. 쓴 값은 BusyBox `i2cget` 이 읽고, BusyBox `i2cset` 이 쓴 값은 내 함수가 읽는다.

### Part B — 어댑터 능력에 맞춰 고르기, 실패 읽기

- B1. `i2creg_read(fd, addr, reg, buf, len)` — `I2C_FUNCS` 를 보고 `I2C_FUNC_I2C` 가 있으면 A1, 없으면 `I2C_SLAVE`(force 아님) + I2C block(≤ 32바이트씩) 또는 byte 읽기. `len == 0` 이나 `reg + len > 256` 은 `-EINVAL`.
- STUB에서 A1 이 `-EOPNOTSUPP` 인 것, 없는 주소 0x49 가 FAKE에서 `-ENXIO`, STUB에서 `-ENODEV` 인 것, 커널 한계(메시지 42개, 메시지당 8192바이트)를 실측으로 확인한다.
- 256바이트 전체를 `i2cdump` 와 대조한다. FAKE에서 내 유틸은 **트랜잭션 1번**, `i2cdump` 는 **256번**이다.

### Part C — bring-up 시나리오: 0x48 온도 센서 + SPI 메시지

- C1. `tmp_raw_to_mC(msb, lsb)` — TMP102 형식(12-bit 2의 보수, 왼쪽 정렬, 0.0625 °C/LSB)을 m°C로. FAKE의 레지스터 0x00/0x01 은 이 형식이다(레지스터 맵은 바이트 단위로 단순화했다. 진짜 TMP102는 16-bit 레지스터 포인터 방식).
- 같은 레지스터를 SMBus word로 읽으면 바이트가 뒤집혀 나오는 것을 보인다(`i2cget ... w`).
- `stop_resets_ptr=1` (STOP에서 포인터를 0으로 되돌리는 칩)로 바꾸고 `write()`+`read()` 두 트랜잭션은 틀리고 `I2C_RDWR` 는 맞는 것을 보인다.
- S1. `spi_build_wr_rd(x, tx, txlen, rx, rxlen, speed, bits)` — "명령 보내고 이어서 읽기"를 CS를 내린 채 한 메시지로 만드는 `struct spi_ioc_transfer[2]` 를 채우고 `SPI_IOC_MESSAGE(2)` 를 돌려준다. QEMU virt에는 SPI 컨트롤러가 없으므로 **인코딩만** 검사하고, 보드에서의 절차는 "보드가 있을 때"로 표시한다.

## 2. Problem (English)

> Write `i2creg`, a userspace register utility for Linux i2c-dev, without libi2c.
>
> ```c
> int i2c_rdwr_read(int fd, uint16_t addr, uint8_t reg, uint8_t *buf, uint16_t len);       /* 0 / -errno */
> int i2c_rdwr_write(int fd, uint16_t addr, uint8_t reg, const uint8_t *d, uint16_t len); /* 0 / -errno */
> int smbus_read_i2c_block(int fd, uint8_t reg, uint8_t *buf, uint8_t len);               /* n / -errno */
> int i2creg_read(int fd, uint16_t addr, uint8_t reg, uint8_t *buf, unsigned len);        /* len / -errno */
> int tmp_raw_to_mC(uint8_t msb, uint8_t lsb);
> unsigned long spi_build_wr_rd(struct spi_ioc_transfer x[2], const uint8_t *tx, uint32_t txlen,
>                               uint8_t *rx, uint32_t rxlen, uint32_t speed_hz, uint8_t bits);
> ```
>
> - A register read must be ONE combined transaction: `S addr+W reg Sr addr+R data... P`.
> - `i2creg_read` must work on adapters without `I2C_FUNC_I2C` by falling back to SMBus I2C-block (≤ 32 bytes per call) or byte reads, and must never use `I2C_SLAVE_FORCE`.
> - Return negative errno values from the kernel unchanged (`-EOPNOTSUPP`, `-ENXIO`, `-ENODEV`, `-EBUSY`).
> - `tmp_raw_to_mC`: 12-bit two's complement, left-justified, 0.0625 °C/LSB, no implementation-defined shifts.
>
> Example (guest, FAKE bus = i2c-1):
> ```text
> $ i2creg read 1 0x48 0x0f 2
> i2c-1 0x48 reg 0x0f: b1 73
> ```
>
> Follow-ups: why can't i2c-stub do `I2C_RDWR`? Why is an SMBus word read byte-swapped for a big-endian sensor register? What does `cs_change` do in `spi_ioc_transfer`?

## 3. 시작 코드

파일 여섯 개가 함께 쓰인다. `starter.c`·`solution.c`·`practice.c` 는 게스트에서 도는 사용자 프로그램(`gcc -static`), `b11_fakei2c.c` + `Makefile` 은 시험용 어댑터 모듈(`ccflags-y := -Werror`, `linux-build-dom` 대상), `check.sh` 는 VM 안에서 빌드 → initramfs → QEMU 부팅 → 판정을 한다. 한 번에 약 5~12초(TCG, VM 부하에 따라) 걸린다.

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B11/check.sh starter
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B11/check.sh solution
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B11/check.sh demo
```

`demo` 는 i2cdetect/i2ctransfer 출력과 i2c·smbus 트레이스포인트를 찍는다(실측 전체: `demo_output.txt`). 게스트를 손으로 만지고 싶으면 `check.sh` 의 `test.sh` 대신 `LAB_CMD` 없이 부팅하면 BusyBox 셸이 뜬다.

<!-- code: b11_fakei2c.c -->

<!-- code: check.sh -->

<!-- code: starter.c -->

starter 실측 (전체: `check_starter_output.txt`):

```text
BUSES stub=i2c-0 fake=i2c-1
--- Part A: I2C_RDWR on FAKE (i2c-1), SMBus block on both
PASS: FAKE advertises I2C_FUNC_I2C
FAIL: A1 rdwr_read ID reg 0x0f -> r=-38 0x00 (want 0xb1)
FAIL: A1 one transaction, 2 msgs (write + Sr + read): calls+0 nmsgs=0
…
PASS: B  plain read() on STUB -> errno 95 (Operation not supported)
…
11/38 passed
RESULT: FAIL (starter) log: /tmp/ELX-B11_work/starter.556904/console.log
```

PASS인 11개는 TODO와 무관한 커널 사실(어댑터 능력, 커널 한계, spi 구조체 배치)이다.

## 4. 힌트

<details><summary>힌트 1 — I2C_RDWR 의 두 구조체</summary>

`struct i2c_msg { __u16 addr; __u16 flags; __u16 len; __u8 *buf; }` 를 배열로 만들고 `struct i2c_rdwr_ioctl_data { struct i2c_msg *msgs; __u32 nmsgs; }` 로 감싼다. 쓰기 메시지는 `flags = 0`, 읽기는 `I2C_M_RD`. 주소는 **메시지마다** 들어가므로 `I2C_SLAVE` 가 필요 없다. ioctl은 성공 시 처리한 메시지 수를 돌려준다.
</details>

<details><summary>힌트 2 — I2C_SMBUS 의 I2C block read</summary>

`union i2c_smbus_data` 의 `block[0]` 에 원하는 길이(1~32)를 넣고 `size = I2C_SMBUS_I2C_BLOCK_DATA`, `read_write = I2C_SMBUS_READ`. 돌아오면 `block[0]` 이 실제 길이, 데이터는 `block[1]` 부터. `I2C_SMBUS` 는 fd에 묶인 주소를 쓰므로 먼저 `I2C_SLAVE` 를 해야 한다.
</details>

<details><summary>힌트 3 — 어떤 경로를 고를지</summary>

`ioctl(fd, I2C_FUNCS, &unsigned_long)` 로 비트를 읽는다. `I2C_FUNC_I2C` → RDWR, 아니면 `I2C_FUNC_SMBUS_READ_I2C_BLOCK` → 32바이트씩, 아니면 `I2C_FUNC_SMBUS_READ_BYTE_DATA` → 한 바이트씩. 레지스터 번호는 8-bit 라서 `reg + len > 256` 은 미리 막는다.
</details>

<details><summary>힌트 4 — 12-bit 부호 확장</summary>

`raw = (msb << 4) | (lsb >> 4)` 는 0~4095. bit 11이 1이면 음수이므로 4096을 뺀다. `(int16_t)(msb << 8 | lsb) >> 4` 는 음수 오른쪽 시프트가 구현 정의라서 피한다. 0.0625 °C = 62.5 m°C = 125/2.
</details>

<details><summary>힌트 5 — spi_ioc_transfer 를 채울 때</summary>

먼저 **전부 0** 으로(`memset`). 포인터는 `__u64` 필드라서 `(uint64_t)(uintptr_t)p`. 한 메시지 안에서 `cs_change = 0` 이면 다음 transfer 까지 CS를 유지한다. 반환값은 `SPI_IOC_MESSAGE(2)`.
</details>

## 5. 풀이

### 5.1 i2c-dev 가 주는 것: ioctl 지도

`drivers/i2c/i2c-dev.c` 를 읽고 정리했다. "요구 능력"이 없으면 커널이 먼저 거절한다.

| 호출 | 하는 일 | 요구 능력 / 검사 | 이 레슨 실측 |
|---|---|---|---|
| `I2C_SLAVE addr` | fd 에 주소를 묶음 (`read`/`write`/`I2C_SMBUS` 용) | 그 주소에 **커널 드라이버가 붙어 있으면 `-EBUSY`** (`i2cdev_check()`: `dev->driver ? -EBUSY : 0`) | 추가 연습 P3 |
| `I2C_SLAVE_FORCE addr` | 위 검사 없이 묶음 (= i2c-tools `-f`) | 없음 | P3 |
| `I2C_FUNCS` | 어댑터의 `I2C_FUNC_*` 비트 | 없음 | STUB `0x0c7f0000` |
| `I2C_RDWR` | `i2c_msg[]` 를 `i2c_transfer()` 로 | `I2C_FUNC_I2C` 없으면 `-EOPNOTSUPP` (`i2cdev_ioctl_rdwr()`), `nmsgs > 42` · `len > 8192` 면 `-EINVAL` | 모두 확인 |
| `I2C_SMBUS` | SMBus 한 동작 | 크기·방향 검사 후 `i2c_smbus_xfer()` | 두 버스 OK |
| `read()`/`write()` | 메시지 1개짜리 트랜잭션 | `I2C_FUNC_I2C` 필요 (`i2cdev_read()`) | STUB에서 errno 95 |

커널 쪽 두 갈래도 소스로 확인했다.

- `drivers/i2c/i2c-core-base.c` `__i2c_transfer()`: 어댑터에 `.xfer`(옛 이름 `master_xfer`) 가 없으면 `"I2C level transfers not supported"` + `-EOPNOTSUPP`.
- `drivers/i2c/i2c-core-smbus.c` `__i2c_smbus_xfer()`: 어댑터에 `.smbus_xfer` 가 있으면 그것을 부르고, 없으면 `i2c_smbus_xfer_emulated()` 가 SMBus 동작을 `i2c_msg` 1~2개로 바꿔 `.xfer` 로 보낸다.

### 5.2 Part A — 한 트랜잭션 = repeated START

A1 은 메시지 두 개를 한 배열에 넣는 것이 전부다. 핵심은 **배열 안의 메시지 사이에는 STOP 이 아니라 repeated START(Sr)** 가 들어간다는 i2c-core 규칙이다(어댑터 `.xfer()` 가 배열 끝에서만 STOP 을 낸다). FAKE의 카운터로 보면 이렇다.

| 동작 | `.xfer()` 호출 수 | `last_nmsgs` | 버스 모양 |
|---|---|---|---|
| `i2c_rdwr_read(0x0f, 1)` | +1 | 2 | S 48+W 0F **Sr** 48+R b1 P |
| `i2c_rdwr_write(0x40, 4)` | +1 | 1 | S 48+W 40 de ad be ef P |
| `write(fd,&reg,1)` 다음 `read(fd,&v,1)` | +2 | 1 | S 48+W 10 **P** · S 48+R xx P |
| `i2creg_read(FAKE, 0, 256)` | +1 | 2 | 256바이트 한 번에 |
| `i2cdump -y 1 0x48 b` | **+256** | 2 | 레지스터마다 SMBus read byte |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">버스 위 모양 (FAKE 0x48, 실측 값)</text>
<text x="10" y="45" fill="currentColor" font-size="13">① I2C_RDWR [W 0x0f][R 2]  → xfer 1번, nmsgs=2</text>
<rect x="10" y="55" width="30" height="28" class="f-acc" stroke="currentColor"/><text x="20" y="74" fill="currentColor">S</text>
<rect x="40" y="55" width="70" height="28" class="f-mut" stroke="currentColor"/><text x="48" y="74" fill="currentColor">0x48+W</text>
<rect x="110" y="55" width="50" height="28" class="f-mut" stroke="currentColor"/><text x="120" y="74" fill="currentColor">0x0f</text>
<rect x="160" y="55" width="34" height="28" class="f-warn" stroke="currentColor"/><text x="166" y="74" fill="currentColor">Sr</text>
<rect x="194" y="55" width="70" height="28" class="f-mut" stroke="currentColor"/><text x="202" y="74" fill="currentColor">0x48+R</text>
<rect x="264" y="55" width="44" height="28" class="f-ok" stroke="currentColor"/><text x="275" y="74" fill="currentColor">b1</text>
<rect x="308" y="55" width="44" height="28" class="f-ok" stroke="currentColor"/><text x="319" y="74" fill="currentColor">73</text>
<rect x="352" y="55" width="30" height="28" class="f-acc" stroke="currentColor"/><text x="362" y="74" fill="currentColor">P</text>
<text x="395" y="74" fill="currentColor">포인터 0x0f → 0x10, 자동 증가</text>
<text x="10" y="115" fill="currentColor" font-size="13">② write() + read(), stop_resets_ptr=1  → xfer 2번</text>
<rect x="10" y="125" width="30" height="28" class="f-acc" stroke="currentColor"/><text x="20" y="144" fill="currentColor">S</text>
<rect x="40" y="125" width="70" height="28" class="f-mut" stroke="currentColor"/><text x="48" y="144" fill="currentColor">0x48+W</text>
<rect x="110" y="125" width="50" height="28" class="f-mut" stroke="currentColor"/><text x="120" y="144" fill="currentColor">0x10</text>
<rect x="160" y="125" width="30" height="28" class="f-warn" stroke="currentColor"/><text x="170" y="144" fill="currentColor">P</text>
<rect x="210" y="125" width="30" height="28" class="f-acc" stroke="currentColor"/><text x="220" y="144" fill="currentColor">S</text>
<rect x="240" y="125" width="70" height="28" class="f-mut" stroke="currentColor"/><text x="248" y="144" fill="currentColor">0x48+R</text>
<rect x="310" y="125" width="44" height="28" class="f-warn" stroke="currentColor"/><text x="321" y="144" fill="currentColor">e7</text>
<rect x="354" y="125" width="30" height="28" class="f-acc" stroke="currentColor"/><text x="364" y="144" fill="currentColor">P</text>
<text x="395" y="144" fill="currentColor">STOP 에서 포인터=0 → reg0 값 (틀림)</text>
<text x="10" y="185" fill="currentColor" font-size="13">③ SMBus read word 0x00 (core 에뮬레이션) → data.word = 0x0019</text>
<rect x="10" y="195" width="30" height="28" class="f-acc" stroke="currentColor"/><text x="20" y="214" fill="currentColor">S</text>
<rect x="40" y="195" width="70" height="28" class="f-mut" stroke="currentColor"/><text x="48" y="214" fill="currentColor">0x48+W</text>
<rect x="110" y="195" width="50" height="28" class="f-mut" stroke="currentColor"/><text x="120" y="214" fill="currentColor">0x00</text>
<rect x="160" y="195" width="34" height="28" class="f-warn" stroke="currentColor"/><text x="166" y="214" fill="currentColor">Sr</text>
<rect x="194" y="195" width="70" height="28" class="f-mut" stroke="currentColor"/><text x="202" y="214" fill="currentColor">0x48+R</text>
<rect x="264" y="195" width="44" height="28" class="f-ok" stroke="currentColor"/><text x="275" y="214" fill="currentColor">19</text>
<rect x="308" y="195" width="44" height="28" class="f-ok" stroke="currentColor"/><text x="319" y="214" fill="currentColor">00</text>
<rect x="352" y="195" width="30" height="28" class="f-acc" stroke="currentColor"/><text x="362" y="214" fill="currentColor">P</text>
<text x="395" y="214" fill="currentColor">word = buf[0] | buf[1] &lt;&lt; 8 = 0x0019</text>
<text x="10" y="255" fill="currentColor">센서(빅엔디언) 값은 0x1900 = 0x190 &lt;&lt; 4 = 400 × 0.0625 = 25.0 °C.</text>
<text x="10" y="275" fill="currentColor">SMBus word 는 "낮은 바이트 먼저" 규칙이라 뒤집혀 보인다 (i2cget ... w → 0x0019).</text>
</svg>
```

트레이스포인트(`events/i2c`, `events/smbus`)로 본 실물 (`check.sh demo`, 6.18.55-dom):

```text
== trace: /i2creg read FAKE 0x48 0x0f 2 (I2C_RDWR)
i2c-1 0x48 reg 0x0f: b1 73
i2creg-94 ... i2c_write: i2c-1 #0 a=048 f=0200 l=1 [0f]
i2creg-94 ... i2c_read: i2c-1 #1 a=048 f=0201 l=2
i2creg-94 ... i2c_reply: i2c-1 #1 a=048 f=0201 l=2 [b1-73]
i2creg-94 ... i2c_result: i2c-1 n=2 ret=2
== trace: i2cget -y FAKE 0x48 0x0f (SMBus, emulated by core)
i2cget-97 ... smbus_read: i2c-1 a=048 f=0000 c=f BYTE_DATA
i2cget-97 ... i2c_write: i2c-1 #0 a=048 f=0000 l=1 [0f]
i2cget-97 ... i2c_read: i2c-1 #1 a=048 f=0001 l=1
…
== trace: i2cget -y STUB 0x48 0x10 (SMBus, native smbus_xfer)
i2cget-100 ... smbus_read: i2c-0 a=048 f=0000 c=10 BYTE_DATA
i2cget-100 ... smbus_reply: i2c-0 a=048 f=0000 c=10 BYTE_DATA l=1 [00]
```

읽는 법:

- `#0`, `#1` 은 같은 `i2c_transfer()` 안의 메시지 번호, `n=2 ret=2` 는 두 메시지가 한 번에 처리됐다는 뜻이다.
- `f=0200` 은 `I2C_M_DMA_SAFE`(0x0200). i2c-dev가 사용자 버퍼를 `memdup_user()` 로 커널에 복사한 뒤 붙인다. 사용자 프로그램이 넣은 플래그는 0과 `I2C_M_RD`(0x0001) 뿐이다.
- FAKE에서 `i2cget` 은 `smbus_read` 다음에 `i2c_write`/`i2c_read` 가 나온다. 코어가 SMBus를 I2C 메시지로 **에뮬레이션**한 흔적이다. STUB에서는 `smbus_*` 만 나온다. 버스 메시지라는 개념 자체가 없다.

A3(I2C block read)은 두 버스 모두에서 통한다. FAKE에서는 코어가 `[W reg][R n]` 으로 에뮬레이션하고, STUB에서는 `stub_xfer()` 의 `I2C_SMBUS_I2C_BLOCK_DATA` 분기가 직접 처리한다.

### 5.3 Part B — i2c-stub 가 흉내 내는 것과 못 내는 것

`drivers/i2c/i2c-stub.c` (6.18.55, 418줄)를 읽고 정리했다.

| 항목 | i2c-stub | 실제 칩/컨트롤러 |
|---|---|---|
| 알고리즘 | `.smbus_xfer = stub_xfer` 만, `.xfer` 없음 | 컨트롤러는 `.xfer` |
| 기본 funcs | QUICK · BYTE · BYTE_DATA · WORD_DATA · I2C_BLOCK (= `0x0c7f0000`) | 보통 `I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL` |
| SMBus block data | 기본 꺼짐 (`functionality` 파라미터로 켬), 쓰기 전에 읽으면 `-EOPNOTSUPP` | 칩마다 |
| 레지스터 저장 | `u16 words[256]`. byte 동작은 **하위 바이트만** 씀 | 칩마다 |
| 없는 주소 | `-ENODEV` | 어댑터는 `-ENXIO` (fault-codes.rst: 주소 NACK) |
| 포인터 | `SMBUS_BYTE` 쓰기/읽기용 `pointer` 만 | 진짜 내부 포인터 |
| 뱅크 | `bank_reg/mask/start/end` 파라미터로 흉내 | 칩마다 |
| 타이밍·NACK·clock stretching·버스 hang | 없음 | 있음 (COMM-M01, COMM-M03) |

`words[]` 저장 방식 때문에 STUB에서는 **word 로 쓴 값과 byte/block 으로 읽은 값이 어긋난다**. demo 실측:

```text
+ i2cset -y 0 0x48 0x20 0x1234 w
+ i2cget -y 0 0x48 0x20 w
0x1234
+ i2cget -y 0 0x48 0x20 b
0x34
+ i2cget -y 0 0x48 0x21 b
0x00
+ /i2creg read 0 0x48 0x20 2
i2c-0 0x48 reg 0x20: 34 00
```

진짜 칩이라면 0x20, 0x21 에 34, 12 가 있어야 한다. STUB은 "SMBus 동작을 따로따로 흉내 내는 저장소"이지 바이트 스트림 칩이 아니다. 그래서 이 레슨은 STUB 대조 테스트를 byte 단위(`i2cset` b, `i2cdump` b)로만 한다.

B1 의 분기와 실측 결과:

| 상황 | `i2creg_read` 경로 | 결과 (실측) |
|---|---|---|
| FAKE, 0x48, 256바이트 | `I2C_RDWR` 1회 | 256, `xfer_calls` +1, `i2cdump` 와 동일 |
| STUB, 0x48, 256바이트 | `I2C_SLAVE` + I2C block 32바이트 × 8 | 256, `i2cdump` 와 동일 |
| FAKE, 0x49 | `I2C_RDWR` | `-6` ENXIO "No such device or address" |
| STUB, 0x49 | `I2C_SLAVE` + block | `-19` ENODEV "No such device" |
| STUB, `i2c_rdwr_read` 직접 | — | `-95` EOPNOTSUPP |
| `nmsgs = 43` | — | `-22` EINVAL (`I2C_RDWR_IOCTL_MAX_MSGS` = 42) |
| 메시지 `len = 8193` | — | `-22` EINVAL |

`i2cdetect -F` 실측도 같은 이야기를 한다.

```text
+ i2cdetect -l
i2c-1	i2c       	b11-fakei2c                     	I2C adapter
i2c-0	smbus     	SMBus stub driver               	SMBus adapter
+ i2cdetect -F 0
I2C                              no
SMBus quick command              yes
…
I2C block read                   yes
+ i2cdetect -F 1
I2C                              yes
…
SMBus block read                 no
```

FAKE의 "SMBus block read no" 는 `I2C_FUNC_SMBUS_EMUL` 에 `READ_BLOCK_DATA` 가 빠져 있기 때문이다(첫 바이트로 길이를 받는 `I2C_M_RECV_LEN` 을 어댑터가 지원해야 함 → `I2C_FUNC_SMBUS_EMUL_ALL`).

### 5.4 Part C — 센서 읽기의 함정 세 개

**(1) 12-bit 부호 확장.** TMP102 형식은 2바이트 `MSB LSB` 에 12-bit 값이 왼쪽 정렬돼 있다.

| msb lsb | raw (12-bit) | 부호 확장 | × 62.5 m°C | 반환 (0쪽 버림) |
|---|---|---|---|---|
| 19 00 | 0x190 | 400 | 25000 | 25000 |
| 7F F0 | 0x7FF | 2047 | 127937.5 | 127937 |
| E7 00 | 0xE70 | −400 | −25000 | −25000 |
| C9 00 | 0xC90 | −880 | −55000 | −55000 |
| FF F0 | 0xFFF | −1 | −62.5 | −62 |
| 00 10 | 0x001 | 1 | 62.5 | 62 |

`raw * 125 / 2` 는 C 정수 나눗셈이라 0 쪽으로 자른다(C11 6.5.5). 내림(floor)을 원하면 따로 처리해야 한다. 테스트 표가 이 규칙을 고정한다.

**(2) SMBus word 는 리틀엔디언.** `i2c-core-smbus.c` 496행 `data->word = msgbuf1[0] | (msgbuf1[1] << 8);`. 빅엔디언 레지스터를 word 로 읽으면 바이트가 뒤집힌다. 실측: `i2cset` 으로 E7 00 을 넣은 뒤 `smbus_read_word_data(0x00)` = `0x00e7`, `i2cget ... w` 도 `0x00e7`, 뒤집으면 `0xe700` = −25.0 °C. 커널 드라이버에는 이 때문에 `i2c_smbus_read_word_swapped()` 가 따로 있다.

**(3) STOP 에서 포인터를 잊는 칩.** 대부분의 센서·EEPROM은 STOP 후에도 포인터를 기억하지만, 데이터시트가 "repeated START 로 읽어라"라고 못박는 칩도 있고, 멀티마스터 버스에서는 STOP 과 다음 START 사이에 다른 마스터가 끼어들 수 있다(COMM-M02). FAKE에 `stop_resets_ptr=1` 을 쓰고 실측했다.

```text
PASS: C  write()+read() = 2 transactions -> got reg0 0xe7, not reg 0x10
PASS: C  rdwr_read (Sr, no STOP) -> 0x73 (want 0x73)
```

### 5.5 SPI — spidev 메시지 인코딩

spidev 에서는 `ioctl(fd, SPI_IOC_MESSAGE(N), struct spi_ioc_transfer[N])` 한 번이 커널의 `spi_sync()` 한 번이다. 한 메시지 동안 CS는 내려가 있고, `cs_change = 1` 인 transfer 뒤에서만 잠깐 올라간다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">struct spi_ioc_transfer (32 바이트, 32/64-bit 공통) 와 SPI_IOC_MESSAGE(2)</text>
<rect x="10" y="30" width="160" height="34" class="f-acc" stroke="currentColor"/><text x="18" y="52" fill="currentColor">0 tx_buf (__u64)</text>
<rect x="170" y="30" width="160" height="34" class="f-acc" stroke="currentColor"/><text x="178" y="52" fill="currentColor">8 rx_buf (__u64)</text>
<rect x="330" y="30" width="90" height="34" class="f-mut" stroke="currentColor"/><text x="336" y="52" fill="currentColor">16 len</text>
<rect x="420" y="30" width="100" height="34" class="f-mut" stroke="currentColor"/><text x="426" y="52" fill="currentColor">20 speed_hz</text>
<rect x="520" y="30" width="70" height="34" class="f-mut" stroke="currentColor"/><text x="524" y="52" fill="currentColor">24 delay</text>
<rect x="590" y="30" width="40" height="34" class="f-mut" stroke="currentColor"/><text x="594" y="52" fill="currentColor">26 b</text>
<rect x="630" y="30" width="44" height="34" class="f-warn" stroke="currentColor"/><text x="634" y="52" fill="currentColor">27 cs</text>
<rect x="674" y="30" width="56" height="34" class="f-mut" stroke="currentColor"/><text x="678" y="52" fill="currentColor">28-31</text>
<text x="10" y="84" fill="currentColor">b = bits_per_word, cs = cs_change, 28-31 = tx_nbits, rx_nbits, word_delay_usecs, pad (0 으로)</text>
<text x="10" y="114" fill="currentColor" font-size="13">SPI_IOC_MESSAGE(2) = 0x40406b00</text>
<rect x="10" y="124" width="70" height="30" class="f-warn" stroke="currentColor"/><text x="16" y="144" fill="currentColor">dir 01</text>
<rect x="80" y="124" width="250" height="30" class="f-acc" stroke="currentColor"/><text x="88" y="144" fill="currentColor">size 14 bit = 0x040 = 2 × 32</text>
<rect x="330" y="124" width="150" height="30" class="f-mut" stroke="currentColor"/><text x="338" y="144" fill="currentColor">type 0x6b 'k'</text>
<rect x="480" y="124" width="120" height="30" class="f-mut" stroke="currentColor"/><text x="488" y="144" fill="currentColor">nr 0x00</text>
<text x="610" y="144" fill="currentColor">(_IOW)</text>
<text x="10" y="184" fill="currentColor" font-size="13">spi_build_wr_rd: 명령 3바이트 → 읽기 4바이트, cs_change = 0</text>
<text x="10" y="210" fill="currentColor">CS</text>
<polyline points="40,200 70,200 70,222 390,222 390,200 420,200" fill="none" stroke="currentColor"/>
<rect x="80" y="230" width="140" height="26" class="f-acc" stroke="currentColor"/><text x="88" y="248" fill="currentColor">x[0] tx 0b 00 10</text>
<rect x="230" y="230" width="150" height="26" class="f-ok" stroke="currentColor"/><text x="238" y="248" fill="currentColor">x[1] rx 4 바이트</text>
<text x="440" y="215" fill="currentColor">CS 는 메시지 전체 동안 low.</text>
<text x="440" y="235" fill="currentColor">x[0].cs_change = 1 이면</text>
<text x="440" y="255" fill="currentColor">두 transfer 사이에서 CS 가 올라감.</text>
</svg>
```

실측으로 확인한 인코딩 사실(게스트의 같은 바이너리, 헤더는 VM `/usr/include/linux/spi/spidev.h`):

- `sizeof(struct spi_ioc_transfer) == 32`, `len@16 speed_hz@20 delay_usecs@24 bits_per_word@26 cs_change@27 pad@31`. 포인터를 `__u64` 에 담아서 32-bit 사용자공간에서도 배치가 같다(ELX-B08 의 compat 문제가 처음부터 없다).
- `SPI_IOC_MESSAGE(511)` 의 크기 필드 = 16352, `SPI_IOC_MESSAGE(512)` = **0**. `SPI_MSGSIZE(N)` 가 14-bit `_IOC_SIZEBITS` 를 넘으면 0을 넣기 때문이다. 크기 0이면 `spidev_get_ioc_message()` 가 `n_ioc = 0` 으로 보고 아무것도 안 한다.
- 커널 검사(`drivers/spi/spidev.c`): type/nr/dir 이 다르면 `-ENOTTY`, 크기가 32의 배수가 아니면 `-EINVAL`, tx/rx 합계가 `bufsiz`(모듈 파라미터, 기본 4096)를 넘으면 `-EMSGSIZE`.

게스트에서 spidev 는 올라가지만 장치 파일은 없다. QEMU `virt` 에 SPI 컨트롤러가 없기 때문이다.

```text
+ grep -E 'i2c|spi' /proc/devices
 89 i2c
153 spi
+ ls /sys/class/spi_master /dev/spidev0.0
ls: /dev/spidev0.0: No such file or directory
/sys/class/spi_master:
```

**보드가 있을 때** (이 VM에서는 실행하지 않음, 절차만):

1. DT 의 SPI 자식 노드에는 **진짜 칩 이름**의 compatible 을 쓴다. `compatible = "spidev"` 를 쓰면 6.18 spidev 는 `"spidev listed directly in DT is not supported"` 를 찍고 probe를 거절한다(`drivers/spi/spidev.c` 716행, ELN H7 노트에서 실측).
2. 커널 드라이버가 없는 칩을 spidev로 쓰려면 `echo spidev > /sys/bus/spi/devices/spi0.1/driver_override` 후 `echo spi0.1 > /sys/bus/spi/drivers/spidev/bind` → `/dev/spidev0.1`.
3. `SPI_IOC_WR_MODE32`, `SPI_IOC_WR_BITS_PER_WORD`, `SPI_IOC_WR_MAX_SPEED_HZ` 로 기본값을 정하고, `spi_build_wr_rd()` 로 만든 배열을 `ioctl(fd, req, x)` 에 넘긴다. 성공하면 전송한 바이트 수가 돌아온다.
4. 로직 분석기로 CS 가 메시지 전체 동안 low인지, `cs_change` 자리에서만 올라가는지 확인한다(COMM-B05 의 CS 타이밍).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B11/check.sh solution
```

실측 (3회 연속 PASS, 각 약 5~12초, 전체: `check_solution_output.txt`):

```text
BUSES stub=i2c-0 fake=i2c-1
PASS: A1 rdwr_read ID reg 0x0f -> r=0 0xb1 (want 0xb1)
PASS: A1 one transaction, 2 msgs (write + Sr + read): calls+1 nmsgs=2
…
PASS: B1 i2creg_read FAKE 256 bytes in ONE transaction (r=256)
PASS: B1 ... == i2cdump FAKE (i2cdump needed 256 transactions)
PASS: B  FAKE addr 0x49 (no ACK) -> -6 (No such device or address)
PASS: B  STUB addr 0x49 -> -19 (No such device)
…
PASS: C  SMBus word = LSB first: 0x00e7, byte-swap -> 0xe700
…
PASS: SPI_IOC_MESSAGE(2) = 0x40406b00: dir W, type 'k', nr 0, size 64
38/38 passed
DMESG_BAD=0
RESULT: PASS (solution)
```

## 7. 자주 하는 실수

1. **`I2C_FUNCS` 를 안 보고 `I2C_RDWR` 부터 쏜다.** SMBus 전용 어댑터(i2c-stub, 많은 PC의 SMBus 컨트롤러)에서 `-EOPNOTSUPP`. → B1 처럼 능력을 보고 고른다. BusyBox `i2ctransfer` 도 같은 이유로 `adapter does not support I2C transfers` 를 낸다.
2. **레지스터 읽기를 `write()` + `read()` 로 한다.** STOP 이 끼어 두 트랜잭션이 된다. 포인터를 잊는 칩이나 멀티마스터에서 틀린 값(실측: 0x73 대신 0xe7). → `I2C_RDWR` 메시지 2개.
3. **SMBus word 로 빅엔디언 레지스터를 읽고 그대로 쓴다.** 25.0 °C 가 `0x0019` 로 보인다. → 바이트 배열로 읽어 `msb << 8 | lsb` 로 조립하거나 word 값을 뒤집는다.
4. **`(int16_t)(msb << 8 | lsb) >> 4` 로 부호 확장.** 범위 밖 값의 정수 변환과 음수 오른쪽 시프트가 구현 정의다(C11 6.3.1.3p3, 6.5.7p5). → `raw & 0x800` 이면 `raw -= 0x1000`.
5. **`-EBUSY` 가 나오면 습관적으로 `-f`(`I2C_SLAVE_FORCE`).** 커널 드라이버가 같은 칩을 쓰는 중이라는 뜻이다. 드라이버의 regmap 캐시와 칩 상태가 어긋나고 트랜잭션이 섞일 수 있다. → 먼저 `/sys/bus/i2c/devices/N-0048/driver` 를 확인하고, 필요하면 unbind 한다.
6. **`spi_ioc_transfer` 를 0으로 초기화하지 않는다.** 스택 쓰레기가 `delay_usecs`, `cs_change`, `tx_nbits` 로 들어가 CS가 엉뚱하게 토글되거나 듀얼/쿼드 전송을 요구하게 된다. 헤더 주석도 "Zero-initialize the structure, including currently unused fields". → `memset` 먼저(S1 테스트는 일부러 0xa5 로 채워 놓고 검사한다).
7. **포인터를 `__u64` 에 바로 대입.** VM의 gcc 15 는 aarch64·armhf 둘 다 `makes integer from pointer without a cast [-Wint-conversion]` **오류**로 멈춘다(실측). `(uint64_t)p` 처럼 바로 캐스트하면 32-bit 에서 포인터 크기가 달라 경고가 난다. → `(uint64_t)(uintptr_t)p`.

## 8. 면접 Q&A

<details><summary>Q1. I2C_RDWR, I2C_SMBUS, read()/write() 는 각각 언제 쓰나?</summary>

`I2C_RDWR` 는 임의의 `i2c_msg` 배열을 한 트랜잭션으로 보낸다. repeated START 가 필요한 레지스터 읽기, 긴 burst, 10-bit 주소 등 SMBus 틀에 안 맞는 것은 이것으로 한다. 단 어댑터에 `I2C_FUNC_I2C` 가 있어야 한다. `I2C_SMBUS` 는 SMBus 동작 하나(byte data, word, I2C block ...)이고 SMBus 전용 어댑터에서도 된다. `read()`/`write()` 는 메시지 1개짜리 트랜잭션이라 "쓰기만" 또는 "포인터 없는 칩에서 읽기만"에 맞는다.

꼬리질문: I2C_RDWR 는 왜 `I2C_SLAVE` 가 필요 없나? → 주소가 메시지마다 들어 있다. 그 대신 **주소 사용 중 검사(EBUSY)도 없다**(추가 연습 P3 에서 드라이버가 붙은 주소에 RDWR 이 그대로 나가는 것을 실측).
</details>

<details><summary>Q2. repeated START 는 왜 필요한가? STOP 후 다시 START 하면 안 되나?</summary>

(1) 칩에 따라 STOP 에서 레지스터 포인터를 초기화하거나 변환을 다시 시작한다. (2) 멀티마스터 버스에서는 STOP 뒤에 다른 마스터가 버스를 잡아 포인터를 바꿀 수 있다. (3) SMBus 의 read byte/word data 가 원래 Sr 로 정의돼 있다. 이 레슨에서는 `stop_resets_ptr=1` 칩으로 2-트랜잭션이 0xe7(틀림), RDWR 이 0x73(맞음)인 것을 봤다.

꼬리질문: 컨트롤러가 임의의 Sr 을 못 하면? → 어댑터가 `struct i2c_adapter_quirks` 로 제약을 선언한다. 6.18 코어의 `i2c_check_for_quirks()` 는 메시지 수(`max_num_msgs`), "쓰기 다음 읽기만" 조합(`I2C_AQ_COMB_WRITE_THEN_READ`), 길이 제한, 길이 0 금지를 검사해 맞지 않는 요청을 `-EOPNOTSUPP` 로 막는다. `I2C_AQ_NO_REP_START` 플래그도 있지만(USB 브리지 `i2c-cp2615.c` 등이 선언) 코어가 직접 검사하지는 않는다 — 소스 grep 으로 확인, 실행은 하지 않음. 그런 어댑터에서 Sr 이 꼭 필요한 칩은 "쓰기 다음 읽기" 조합 하나로만 다룬다.
</details>

<details><summary>Q3. i2c-stub 에서 I2C_RDWR 가 안 되는 이유를 소스로 설명하면?</summary>

i2c-stub 의 `i2c_algorithm` 은 `.smbus_xfer` 와 `.functionality` 만 채우고 `.xfer` 는 없다. 기능 비트에도 `I2C_FUNC_I2C` 가 없다(`0x0c7f0000`). i2c-dev 의 `i2cdev_ioctl_rdwr()` 가 맨 처음 `I2C_FUNC_I2C` 를 검사해 `-EOPNOTSUPP` 를 돌려주고, 그 검사를 지나더라도 `__i2c_transfer()` 가 `.xfer` 가 없으면 `-EOPNOTSUPP` 다.

꼬리질문: 반대로 `.xfer` 만 있는 어댑터에 SMBus 요청은 왜 되나? → `i2c_smbus_xfer_emulated()` 가 SMBus 동작을 `i2c_msg` 1~2개로 바꿔 준다. FAKE 트레이스에서 `smbus_read` 다음 `i2c_write`/`i2c_read` 가 보인 것이 그 증거다. 임의 메시지 배열을 SMBus 동작으로 바꾸는 일반 방법은 없으므로 반대 방향 에뮬레이션은 없다.
</details>

<details><summary>Q4. i2cget 이 "Device or resource busy" 를 냈다. 무슨 뜻이고 어떻게 하나?</summary>

그 주소에 커널 드라이버가 바인딩돼 있다(`i2cdev_check()`: `dev->driver ? -EBUSY : 0`). `i2cdetect` 에서는 `UU` 로 보인다. 읽기만 확인하려면 `-f`(`I2C_SLAVE_FORCE`)로 가능하지만, 드라이버의 캐시·상태 머신과 충돌할 수 있다. 쓰기는 드라이버를 unbind 하고 하는 것이 원칙이다. 이 레슨의 P3 에서 `echo dummy 0x48 > new_device` 로 i2c-core 의 `dummy` 드라이버를 붙여 EBUSY, force, `UU` 를 모두 재현했다.

꼬리질문: unbind 는? → `echo 0-0048 > /sys/bus/i2c/drivers/<드라이버>/unbind`.
</details>

<details><summary>Q5. 같은 "칩 없음"인데 어떤 버스는 ENXIO, 어떤 버스는 ENODEV 를 돌려준다. 왜?</summary>

`Documentation/i2c/fault-codes.rst` 는 주소 단계 NACK 에 `ENXIO` 를 쓰라고 한다(FAKE가 그렇게 함: `-6`). i2c-stub 은 칩 목록에 없는 주소에 `-ENODEV` 를 돌려준다(`-19`, 소스 그대로). 실제 컨트롤러 드라이버도 NACK 에 `-ENXIO`, `-EREMOTEIO`, `-EIO` 등을 섞어 쓴다. 그래서 사용자공간 유틸은 errno 하나에 의존하지 말고 "음수면 실패, 메시지는 strerror"로 다루는 것이 안전하다. 중재 패배는 `EAGAIN`, 시간 초과는 `ETIMEDOUT` 으로 문서에 정의돼 있다.

꼬리질문: bring-up 에서 0x48 이 `--` 로 보이면 무엇부터? → 전원·리셋 핀, 풀업, 주소 핀(A0) 배선, 7-bit/8-bit 주소 혼동(0x90 은 0x48 의 8-bit 표기), 버스 hang(COMM-M03).
</details>

<details><summary>Q6. 사용자공간 드라이버(i2c-dev/spidev)의 한계는? 그래도 언제 쓰나?</summary>

한계: 인터럽트를 직접 받지 못한다(GPIO uAPI 이벤트로 우회 — ELX-B10), 매 트랜잭션이 syscall 이라 지연과 지터가 크다, 전원관리·suspend 연동이 없다, hwmon/IIO/input 같은 커널 프레임워크에 못 붙는다, 루트 권한이나 장치 파일 권한이 필요하다, 같은 칩을 커널 드라이버와 같이 쓰면 충돌한다, 여러 트랜잭션에 걸친 원자성은 프로세스 사이에서 보장되지 않는다.

쓰는 곳: bring-up 첫 확인, 공장 테스트·캘리브레이션 도구, 데이터시트 검증, 커널 드라이버를 쓰기 전 프로토타입. 제품에서 계속 쓸 장치는 ELX-M08 처럼 `i2c_driver` + regmap 으로 옮긴다.
</details>

<details><summary>Q7. SPI_IOC_MESSAGE(N) 의 숫자는 어떻게 만들어지고, cs_change 는 무엇을 하나?</summary>

`_IOW('k', 0, char[N × 32])`. `SPI_IOC_MESSAGE(2)` = `0x40406b00` (dir=write, size=64, type='k', nr=0). spidev 는 크기를 32로 나눠 transfer 개수를 얻는다. 크기 필드가 14-bit 라 N=512 부터는 0이 된다. `cs_change = 1` 은 그 transfer 가 끝난 뒤 CS 를 잠깐 올렸다가 다음 transfer 에서 다시 내린다는 뜻이다. 마지막 transfer 에 붙이면 의미가 반대로, 메시지가 끝나도 CS 를 유지할 수 있다(`include/linux/spi/spi.h` 의 `struct spi_transfer` 설명 (i)/(ii)). 0 이면 메시지 내내 CS 가 low 다.

꼬리질문: 왜 DT 에 `compatible = "spidev"` 를 쓰면 안 되나? → DT 는 하드웨어를 기술하는데 spidev 는 리눅스 구현 이름이다. 6.18 spidev 는 그 경우 probe 를 거절한다. 진짜 칩 이름을 쓰고 `driver_override` 로 붙인다.
</details>

<details><summary>Q8. i2cdump 로 레지스터 전체를 보는 것과 burst read 의 차이는 bring-up 에서 왜 중요한가?</summary>

`i2cdump ... b` 는 레지스터마다 SMBus 트랜잭션을 하나씩 만든다(FAKE 실측 256번). 다바이트 측정값(가속도 X_H/X_L, 온도 MSB/LSB)을 이렇게 읽으면 바이트 사이에 칩이 값을 갱신해 **상위·하위가 다른 샘플**에서 올 수 있다(tearing). 많은 센서는 burst read 동안 출력 레지스터를 잠근다(shadow/latch). 그래서 다바이트 값은 한 트랜잭션(RDWR 이나 I2C block)으로 읽는다. 또 `i2cdump` 는 읽기만으로 상태가 바뀌는 레지스터(인터럽트 상태 clear-on-read, FIFO pop)를 건드리므로 실물 보드에서는 범위를 좁혀 쓴다.
</details>

## 9. 추가 연습

<details><summary>P1. i2creg_write + i2c_update_bits — 두 버스 모두에서 RMW</summary>

`I2C_FUNC_I2C` 면 `i2c_rdwr_write` 를 32바이트씩, 아니면 `I2C_SMBUS_I2C_BLOCK_DATA` 쓰기(32바이트씩) 또는 byte 쓰기. `i2c_update_bits` 는 읽고 `(old & ~mask) | (val & mask)` 를 만들어 **값이 바뀔 때만** 쓴다. 실측: 40바이트 쓰기가 STUB에서는 SMBus block 32+8, FAKE에서는 RDWR 32+8. 바뀌지 않으면 FAKE `xfer_calls` 가 1(읽기만) 늘어난다. 커널의 `regmap_update_bits()` 와 같은 생각이다(ELX-M08).
</details>

<details><summary>P2. i2c_scan — i2cdetect 와 같은 결과 만들기</summary>

0x03~0x77 을 돌며 `I2C_SLAVE`(force 아님) → `-EBUSY` 면 "UU"(2), 아니면 SMBus quick write(`I2C_SMBUS_QUICK`, data NULL) 가 성공하면 1. FAKE에서 quick write 는 길이 0 메시지(주소 단계만)로 에뮬레이션된다. 두 버스 모두 `i2cdetect -y` 출력을 파싱한 표와 128칸이 같아야 한다. 주의: 실제 보드에서 quick write 는 일부 칩(쓰기 보호 EEPROM 등)의 상태를 바꿀 수 있어 i2cdetect 는 0x30–0x37, 0x50–0x5F 에서 read byte 를 쓴다.
</details>

<details><summary>P3. 드라이버가 붙은 주소: EBUSY, -f, UU, 그리고 RDWR 의 무방비</summary>

`echo dummy 0x48 > /sys/bus/i2c/devices/i2c-N/new_device` 로 i2c-core 의 `dummy` 드라이버를 붙인다. 그러면 `I2C_SLAVE` → `-EBUSY`, `i2creg_read`(STUB, force 없음) → `-EBUSY`, `i2c_bind_addr(..., allow_force=1)` → 1(강제) 후 읽기 성공, 스캔은 0x48 에 `UU`. 같은 상태에서 FAKE의 `I2C_RDWR` 는 **아무 검사 없이** 0xb1 을 읽는다. 끝나면 `delete_device` 에 `0x48` 을 써서 지운다.
</details>

<details><summary>P4. SPI 메시지 두 가지 더 — 명령 2개 + CS 토글, 전이중</summary>

`spi_build_two_cmds`: x[0] = cmd1(예: WREN 0x06, `cs_change = 1` 로 끝나면 CS 올림), x[1] = cmd2(예: READ 0x03 + 주소), x[2] = rx. `SPI_IOC_MESSAGE(3)`, 크기 96. SPI flash 의 "WREN 은 독립된 CS 프레임이어야 한다" 규칙을 한 ioctl 로 지키는 방법이다. `spi_build_fd`: transfer 하나에 tx_buf, rx_buf 를 둘 다 넣으면 같은 클럭에 보내고 받는다(SPI 는 본래 전이중). `speed_hz = 0` 은 "장치 기본값"이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B11/check.sh practice
```

실측: `55/55 passed`, `DMESG_BAD=0`, `RESULT: PASS (practice)` (본문 38개 + 연습 17개).

## 10. 핵심 정리

- `/dev/i2c-N`: `I2C_FUNCS` 로 능력을 먼저 보고, `I2C_RDWR`(I2C_FUNC_I2C 필요) 또는 `I2C_SMBUS` 를 고른다.
- 레지스터 읽기 = `i2c_msg` 2개 = 한 트랜잭션 = `S W reg Sr R data P`. `write()`+`read()` 는 STOP 이 끼는 두 트랜잭션이다.
- i2c-stub 은 `.smbus_xfer` 만 있는 SMBus 어댑터 → `I2C_RDWR` 는 `-EOPNOTSUPP`, 없는 주소는 `-ENODEV`, 16-bit 셀 저장이라 word 와 byte 보기가 다르다.
- SMBus → I2C 는 코어가 에뮬레이션(`i2c_smbus_xfer_emulated`), 반대는 없다.
- SMBus word 는 LSB 먼저 → 빅엔디언 센서 값은 뒤집힌다. 12-bit 부호 확장은 `raw -= 0x1000` 으로.
- `I2C_SLAVE` 의 `-EBUSY` = 커널 드라이버가 사용 중. `-f` 는 위험을 알고 쓴다. RDWR 은 그 검사조차 없다.
- spidev: `struct spi_ioc_transfer` 는 32바이트, 먼저 0으로, 포인터는 `(uint64_t)(uintptr_t)`, `SPI_IOC_MESSAGE(N)` 크기는 `N × 32`(N ≥ 512 면 0), `cs_change` 로 CS 프레임을 나눈다.
- DT 에 `"spidev"` 를 쓰지 말고 진짜 칩 이름 + `driver_override`.

## 참고자료

- Linux 6.18.55 소스 (`/var/lab/src/linux-6.18.55`): `drivers/i2c/i2c-dev.c`, `drivers/i2c/i2c-core-base.c` (`__i2c_transfer`), `drivers/i2c/i2c-core-smbus.c` (`i2c_smbus_xfer_emulated`), `drivers/i2c/i2c-stub.c`, `drivers/spi/spidev.c`, `include/uapi/linux/i2c.h`, `include/uapi/linux/i2c-dev.h`, `include/uapi/linux/spi/spidev.h`
- `Documentation/i2c/dev-interface.rst`, `Documentation/i2c/fault-codes.rst`, `Documentation/i2c/i2c-stub.rst`, `Documentation/spi/spidev.rst` — https://docs.kernel.org/i2c/dev-interface.html , https://docs.kernel.org/spi/spidev.html
- NXP UM10204 I2C-bus specification (START/Sr/STOP, ACK/NACK), SMBus 3.x specification (word = low byte first)
- TI TMP102 datasheet (온도 레지스터 형식: 12-bit, 0.0625 °C/LSB)
- 이 사이트: COMM-B06 (I2C 비트 수준), COMM-B07 (repeated START 센서 읽기), COMM-B05 (SPI 명령/CS 타이밍), COMM-M02 (멀티마스터 중재), COMM-M03 (버스 복구), ELX-B08 (ioctl 번호와 compat), ELX-B10 (GPIO uAPI), ELX-M08 (I2C 클라이언트 드라이버 + regmap)
- 랩 노트: ELN `H_hw_access_buses/2026-10-10_H5_i2c_subsystem.md` (i2c-dev, 어댑터 작성), `2026-10-10_H7_spi_subsystem.md` (spidev driver_override 실측)
