# H5. I2C 서브시스템 — adapter / algorithm / client / driver, i2c_transfer, SMBus, i2c-dev, 버스 복구

> **이 노트를 다 읽으면**: I2C 컨트롤러 드라이버(adapter + algorithm)와 장치 드라이버(client driver)를 구분해서 둘 다 짤 수 있다 · DT 자식 노드가 `i2c_client`가 되고 `probe(client)`가 불리기까지를 설명할 수 있다 · `i2c_transfer`/SMBus helper/i2c-dev 중 무엇을 언제 쓰는지, 각각이 버스에 어떤 파형을 만드는지 ftrace로 확인할 수 있다 · NACK·timeout·arbitration·bus hang의 errno와 recovery(9 클럭) 경로를 코드로 재현할 수 있다
> **왜 필요한가**: 센서 키트 bring-up에서 가장 먼저 붙이는 드라이버가 I2C IMU·EEPROM·PMIC이고, 면접 단골 질문이 "I2C client driver probe를 짜 보라", "버스가 SDA LOW로 멈췄다, 어떻게 하나"다.
> **Don 기준 난이도**: I2C 프로토콜·scope 디버깅·clock stretching·bus hang 복구는 이미 강점 / Linux가 그걸 어떤 객체(adapter·client·driver)와 API로 나누는지, 에러가 어떤 errno로 올라오는지, 소프트웨어 쪽 관측 도구(tracepoint)가 새로 배울 부분
> **선행 노트**: A3(i2c-dev `I2C_RDWR`), A10(i2c-tools, i2c-stub), D(Device Tree), G2·G3(driver model, platform driver probe), G8(sysfs·debugfs)

---

## 0. 큰 그림 — 이게 왜 필요한가

bare-metal에서 Don이 IMU를 읽던 코드는 대략 이랬다: `HAL_I2C_Mem_Read(&hi2c1, 0x68 << 1, 0x3B, 1, buf, 6, 100)`. 이 한 줄 안에 세 가지가 섞여 있다.

1. **어느 컨트롤러로** 보낼지 (`hi2c1` — SoC의 I2C 블록 레지스터를 만지는 코드)
2. **어느 장치에게 무엇을** 보낼지 (주소 0x68, 레지스터 0x3B, 6바이트 — 센서를 아는 코드)
3. 그 사이의 **공통 규칙** (repeated START로 묶기, timeout, 재시도)

Linux는 이 셋을 **완전히 다른 드라이버로 쪼갠다**. 컨트롤러 쪽은 **adapter driver**(= bus controller driver, 예: `i2c-imx`, `i2c-designware`, 이 노트의 `h5_fakebus`), 장치 쪽은 **client driver**(예: `at24`, `bmp280`, 이 노트의 `h5_imu`), 가운데 공통부는 **i2c-core**다. 덕분에 같은 `bmp280` 드라이버가 i.MX8의 I2C에서도, Jetson의 I2C에서도, GPIO bit-bang I2C에서도 한 줄 수정 없이 돈다. 센서 드라이버는 "주소 0x76에 레지스터 0xD0을 읽어 줘"라고만 말하고, 그 요청이 어떤 레지스터 비트로 바뀌는지는 모른다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="220" y="24" font-size="14" text-anchor="middle" font-weight="bold">Linux (이 노트)</text>
<text x="555" y="24" font-size="14" text-anchor="middle" font-weight="bold">bare-metal (Don)</text>
<rect x="20" y="40" width="400" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="220" y="67" font-size="13" text-anchor="middle">유저: i2cget · i2cdetect · u1_rdwr</text>
<rect x="20" y="110" width="190" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="115" y="132" font-size="13" text-anchor="middle">i2c-dev</text>
<text x="115" y="151" font-size="12" text-anchor="middle">/dev/i2c-N + ioctl</text>
<rect x="230" y="110" width="190" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="325" y="132" font-size="13" text-anchor="middle">client driver</text>
<text x="325" y="151" font-size="12" text-anchor="middle">h5_imu · at24 · pca954x</text>
<rect x="20" y="188" width="400" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="220" y="210" font-size="13" text-anchor="middle">i2c-core (drivers/i2c/i2c-core-*.c)</text>
<text x="220" y="232" font-size="12" text-anchor="middle">i2c_transfer · SMBus 에뮬레이션 · bus lock · tracepoint</text>
<rect x="20" y="270" width="400" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="220" y="292" font-size="13" text-anchor="middle">adapter (bus controller) driver — h5_fakebus</text>
<text x="220" y="314" font-size="12" text-anchor="middle">struct i2c_adapter + i2c_algorithm.xfer()</text>
<rect x="20" y="352" width="400" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5 4"/>
<text x="220" y="379" font-size="13" text-anchor="middle">SCL/SDA: IMU@0x68 · EEPROM@0x50 · mux@0x70</text>
<line x1="115" y1="84" x2="115" y2="108" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<line x1="325" y1="84" x2="325" y2="108" stroke="currentColor" stroke-width="1.5" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<line x1="115" y1="162" x2="115" y2="186" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<line x1="325" y1="162" x2="325" y2="186" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<line x1="220" y1="244" x2="220" y2="268" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<line x1="220" y1="326" x2="220" y2="350" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<rect x="450" y="40" width="210" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="555" y="67" font-size="13" text-anchor="middle">앱 · 테스트 코드</text>
<rect x="450" y="110" width="210" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="555" y="141" font-size="13" text-anchor="middle">센서 드라이버 함수</text>
<rect x="450" y="188" width="210" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="555" y="221" font-size="12" text-anchor="middle">HAL_I2C_Mem_Read() 공통부</text>
<rect x="450" y="270" width="210" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="555" y="303" font-size="13" text-anchor="middle">I2C 컨트롤러 레지스터 코드</text>
<rect x="450" y="352" width="210" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5 4"/>
<text x="555" y="379" font-size="13" text-anchor="middle">보드 배선 · scope · LA</text>
<line x1="420" y1="62" x2="450" y2="62" stroke="#888" stroke-dasharray="3 3"/>
<line x1="420" y1="136" x2="450" y2="136" stroke="#888" stroke-dasharray="3 3"/>
<line x1="420" y1="216" x2="450" y2="216" stroke="#888" stroke-dasharray="3 3"/>
<line x1="420" y1="298" x2="450" y2="298" stroke="#888" stroke-dasharray="3 3"/>
<line x1="420" y1="374" x2="450" y2="374" stroke="#888" stroke-dasharray="3 3"/>
</svg>
```
그림 1 — Linux I2C 스택과 Don의 bare-metal 코드 대응. 유저는 i2c-dev로 직접 버스를 쓰거나(실선), client driver가 만든 sysfs·IIO·nvmem 파일을 쓴다(점선). 둘 다 i2c-core를 지나 adapter의 `.xfer()`에서 처음으로 하드웨어를 만난다.

### 0.1 이 노트의 실습 환경 — "가짜 버스를 직접 만든다"

QEMU `virt` 머신에는 I2C 컨트롤러가 없고, 랩 커널은 `CONFIG_I2C_STUB`도 꺼져 있다(A10에서는 VM 호스트 커널의 `i2c-stub`을 썼다). 그래서 이 노트는 **I2C 컨트롤러 드라이버를 직접 하나 쓴다**: `h5_fakebus`. 진짜 컨트롤러 드라이버와 똑같이 `struct i2c_adapter`를 등록하고 `.xfer()`를 구현하는데, 레지스터를 치는 대신 버스 위의 장치들을 메모리 안에서 흉내 낸다. 덤으로 **컨트롤러 쪽 코드**(Don이 bare-metal에서 짜던 쪽)가 Linux에서 어떻게 생겼는지까지 배운다.

| 버스 위 가짜 장치 | 주소 | 흉내 내는 동작 | 붙는 드라이버 |
|---|---|---|---|
| IMU (MPU-6050 비슷) | 0x68 | 128바이트 레지스터 파일, auto-increment, WHO_AM_I(0x75)=0x68, 0x3B~0x40 가속도, 0x80 이상 쓰기는 data NACK | `h5_imu` (이 노트) |
| EEPROM (24c02 비슷) | 0x50 | 256바이트, 8바이트 page write(넘치면 page 앞으로 wrap), 쓰기 후 5 ms 동안 주소 NACK(tWR) | 커널의 `at24` |
| I2C switch PCA9548 | 0x70 | 1바이트 채널 선택 레지스터 | 커널의 `pca954x` |
| 온도센서 2개 | 0x48 (mux 채널 0, 1 뒤) | 같은 주소, 채널마다 다른 값 (25, 30) | 없음 (i2c-dev로 읽음) |

fault injection 노브(debugfs `/sys/kernel/debug/h5_fakebus/`)로 arbitration lost(`-EAGAIN`), SDA 고착, SCL 고착을 만들고, `stats` 파일이 트랜잭션·메시지·바이트·NACK 수와 **400 kHz 기준으로 계산한 버스 시간**을 센다.

실행 환경: Mac의 Lima VM `elinux`(`uname -r` = `7.0.0-38-generic`) 안에서 공용 랩의 **debug 커널** `/var/lab/out/Image-dbg`(`6.18.55-dbg`, ftrace·lockdep 포함)를 QEMU로 부팅한다. 모듈은 `/var/lab/out/linux-build-dbg` 트리에 대해 빌드한다. 유저 프로그램은 VM에서 `gcc -static`으로 빌드해 initramfs에 넣는다(게스트는 BusyBox뿐이라 libc가 없다). BusyBox에 `i2cdetect`·`i2cget`·`i2ctransfer`가 들어 있다.

```sh
# build.sh — 소스 조각을 /var/tmp/H5/mod 로 모아 debug 커널(6.18.55-dbg) 트리에 대해 빌드
set -e
S=/Users/donh/.elinux_scratch/H5; M=/var/tmp/H5/mod
mkdir -p $M && cd $M
cat $S/fbp1.c $S/fbp2.c $S/fbp3.c $S/fbp4.c $S/fbp5.c $S/fbp6.c > h5_fakebus.c
cat $S/imu_a.c $S/imu_b.c $S/imu_c.c > h5_imu.c
cp $S/Kbuild .                      # 내용: obj-m += h5_fakebus.o h5_imu.o
make -C /var/lab/out/linux-build-dbg M=$M -j2 modules 2>&1 | grep -v "^make"
```

```sh
# boot.sh <게스트 스크립트> — debug 커널 + h5.dtb + 모듈 3개(h5_fakebus, h5_imu, at24) + 정적 바이너리
set -e
W=/var/tmp/H5; S=/Users/donh/.elinux_scratch/H5
rm -rf $W/r && mkdir $W/r && cp -a /var/lab/build/rootfs/. $W/r/
cp $W/mod/*.ko /var/lab/build/linux-dbg/drivers/misc/eeprom/at24.ko $W/bin/* $S/$1 $W/r/
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout ${TMO:-180} qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image-dbg -initrd $W/i.gz -dtb $W/h5.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/$1"
```

`at24`는 랩 설정에서 `CONFIG_EEPROM_AT24=m`이라 랩이 이미 빌드해 둔 `at24.ko`(debug 커널용)를 그대로 쓴다. `CONFIG_I2C_CHARDEV=y`, `CONFIG_I2C_MUX_PCA954x=y`, `CONFIG_REGMAP_I2C=y`, `CONFIG_NVMEM_SYSFS=y`는 이미 켜져 있다.

---

## 1. 네 개의 객체 — adapter, algorithm, client, driver

### 1.1 직관 — 누가 무엇을 아는가

| 객체 | 한 문장 | bare-metal 대응 | 이 노트의 예 |
|---|---|---|---|
| `struct i2c_adapter` | **버스 하나**. `/dev/i2c-N`의 N, sysfs의 `i2c-N` | `I2C_HandleTypeDef hi2c1` | `i2c-0` (h5-fakebus), mux 자식 `i2c-1`~`i2c-8` |
| `struct i2c_algorithm` | 그 버스를 **어떻게 구동하나** — `.xfer`, `.functionality` 함수 포인터 | `HAL_I2C_Master_Transmit` 구현부 (레지스터 시퀀스) | `h5_algo` |
| `struct i2c_client` | 버스 위 **장치 하나** = (adapter, 7비트 주소, 이름) | "hi2c1의 0x68" 이라는 조합 | `0-0068`, `0-0050`, `0-0070` |
| `struct i2c_driver` | 특정 종류의 장치를 다루는 **드라이버** — `probe(client)` | `mpu6050_init()`, `mpu6050_read()` 모음 | `h5_imu`, `at24`, `pca954x` |

핵심: **adapter driver는 장치를 모르고, client driver는 컨트롤러를 모른다.** 둘을 잇는 유일한 데이터 구조가 `struct i2c_msg` 배열이다. client driver가 "주소 0x68에 [0x3B]를 쓰고, 이어서 6바이트 읽기"라는 메시지 2개를 만들어 넘기면, adapter의 `.xfer()`가 그것을 START·주소·데이터·repeated START·STOP 파형으로 바꾼다.

sysfs 이름 규칙: client는 `<버스번호>-<주소 4자리 hex>`(예: `0-0068`), adapter는 `i2c-<번호>`. 10비트 주소 client는 주소에 0xa000이 더해져 `0-a150` 꼴이 된다.

### 1.2 6.18 헤더에서 직접 본 정의 (발췌)

아래는 랩 소스 `/var/lab/src/linux-6.18.55/include/linux/i2c.h`에서 잘라 온 것이다(주석 일부 생략).

```c
struct i2c_algorithm {
	union {
		int (*xfer)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
		int (*master_xfer)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
	};
	union {
		int (*xfer_atomic)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
		int (*master_xfer_atomic)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
	};
	int (*smbus_xfer)(struct i2c_adapter *adap, u16 addr, unsigned short flags,
			  char read_write, u8 command, int size, union i2c_smbus_data *data);
	int (*smbus_xfer_atomic)(/* 같은 인자 */);
	u32 (*functionality)(struct i2c_adapter *adap);
	/* CONFIG_I2C_SLAVE: reg_target/unreg_target (옛 이름 reg_slave/unreg_slave) */
};
```

- 같은 헤더의 kernel-doc에 `@master_xfer: deprecated, use @xfer`라고 적혀 있다. **union이라 둘은 같은 메모리**다. 새 코드는 `.xfer`를 쓰고, 옛 드라이버의 `.master_xfer`도 그대로 컴파일된다. (i2c-core 내부 `__i2c_transfer()`는 6.18에서도 아직 `adap->algo->master_xfer(...)`로 부른다 — 같은 포인터라 상관없다.) "master/slave" 용어를 "controller/target"으로 바꾸는 작업의 일부다.
- `.smbus_xfer`가 있으면 SMBus 요청을 그대로 받아 처리하는(SMBus 전용 컨트롤러, 예: PC의 i2c-i801) 것이고, 없으면 core가 SMBus를 `i2c_msg`로 **에뮬레이션**해서 `.xfer`로 보낸다(4장).
- `.xfer_atomic`은 인터럽트가 꺼진 상태(시스템 종료 직전 PMIC에 power-off 명령 등)에서 쓰는 polling 버전이다.

```c
struct i2c_driver {
	/* ... */
	int (*probe)(struct i2c_client *client);     /* 6.3부터 인자 하나 */
	void (*remove)(struct i2c_client *client);   /* 반환값 없음 */
	void (*shutdown)(struct i2c_client *client);
	struct device_driver driver;                 /* .name, .of_match_table, .pm, .dev_groups */
	const struct i2c_device_id *id_table;
	/* ... detect/address_list (옛 방식 자동 탐지) ... */
};
```

- `probe`는 **6.3부터 `const struct i2c_device_id *`인자가 빠지고 하나만 받는다.** 과도기에 쓰던 `.probe_new` 이름은 이후 제거되어 6.18 헤더에는 없다(직접 grep으로 확인). id 테이블 정보가 필요하면 probe 안에서 `i2c_client_get_device_id(client)`나, OF/ACPI/id 테이블을 한 번에 보는 `i2c_get_match_data(client)`를 쓴다(at24가 이렇게 한다).
- `remove`가 `void`를 반환하는 것도 최근 변경이다(옛 코드의 `int remove`는 6.x에서 컴파일 에러). 오래된 블로그 코드를 그대로 붙이면 이 두 곳에서 깨진다.

### 1.3 `struct i2c_msg`와 플래그

```c
struct i2c_msg {
	__u16 addr;    /* 7비트 주소 (오른쪽 정렬, R/W 비트 미포함) */
	__u16 flags;
	__u16 len;     /* 바이트 수 */
	__u8 *buf;
};
```

메시지 하나 = **"(repeated) START + 주소 + R/W + 데이터 len바이트"**. 배열의 메시지 사이는 repeated START(Sr), 배열 끝에만 STOP이 붙는다. 이게 Linux I2C의 가장 중요한 규칙이다.

| 플래그 | 값 | 뜻 | 필요한 functionality |
|---|---|---|---|
| `I2C_M_RD` | 0x0001 | 이 메시지는 읽기 (주소 바이트의 R/W=1) | `I2C_FUNC_I2C` |
| `I2C_M_TEN` | 0x0010 | 10비트 주소 | `I2C_FUNC_10BIT_ADDR` |
| `I2C_M_RECV_LEN` | 0x0400 | 첫 바이트가 "뒤따를 길이" — SMBus block read | `I2C_FUNC_SMBUS_READ_BLOCK_DATA` |
| `I2C_M_NOSTART` | 0x4000 | 앞 메시지에 START·주소 없이 이어 붙임 (버퍼만 나눔) | `I2C_FUNC_NOSTART` |
| `I2C_M_IGNORE_NAK`, `I2C_M_NO_RD_ACK`, `I2C_M_REV_DIR_ADDR`, `I2C_M_STOP` | 0x1000 등 | 규격을 어기는 칩용 "protocol mangling" | `I2C_FUNC_PROTOCOL_MANGLING` |
| `I2C_M_DMA_SAFE` | 0x0200 | buf가 DMA 가능한 메모리임 (커널 내부용) | 없음 |

값은 `include/uapi/linux/i2c.h` 그대로다. `I2C_M_NOSTART`가 무슨 쓸모인지는 4.5절의 at24 트레이스에서 실제로 보게 된다(regmap이 "레지스터 주소 버퍼 + 데이터 버퍼"를 복사 없이 한 write로 보낼 때 쓴다).

### 1.4 예제 1 — adapter 드라이버 `h5_fakebus` 작성과 빌드

무엇을 확인하나: 컨트롤러 드라이버가 해야 하는 일은 결국 `.xfer(adap, msgs, num)` 하나를 정직하게 구현하는 것이다. 성공하면 **처리한 메시지 수(num)**, 실패하면 음수 errno를 돌려준다.

```c
// SPDX-License-Identifier: GPL-2.0
/* h5_fakebus 1/6 — 가짜 target 장치 모델 (IMU, EEPROM, mux, 온도센서) */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/ktime.h>

enum { T_NONE, T_IMU, T_EE, T_MUX, T_TMP0, T_TMP1 };

struct h5_bus {
	struct i2c_adapter adap;
	struct i2c_bus_recovery_info rinfo;
	u8 imu[128], imu_ptr; s16 sample;          /* IMU @0x68: MPU-6050 비슷한 레지스터 파일 */
	u8 ee[256], ee_ptr; ktime_t ee_busy_until; /* EEPROM @0x50: 24c02, 8-byte page */
	u8 mux_ctl, tmp[2][4], tmp_ptr[2];         /* PCA9548 @0x70, 채널0/1 뒤 온도센서 @0x48 */
	u32 inject_eagain, stuck_sda_clocks; bool scl_stuck;   /* fault injection (debugfs) */
	int scl, pulses;
	u64 xfers, msgs, bytes, bits, nacks; u32 bus_hz;      /* 통계 */
	struct dentry *dbg;
};

static int h5_target(struct h5_bus *b, u16 addr)   /* 주소 phase: 누가 ACK 하나 */
{
	switch (addr) {
	case 0x68: return T_IMU;
	case 0x70: return T_MUX;
	case 0x50: return ktime_before(ktime_get(), b->ee_busy_until) ? T_NONE : T_EE;
	case 0x48: return (b->mux_ctl & 1) ? T_TMP0 : (b->mux_ctl & 2) ? T_TMP1 : T_NONE;
	}
	return T_NONE;
}
```

`h5_target()`이 "주소 바이트를 보냈을 때 누가 ACK하나"다. EEPROM은 쓰기 사이클(tWR) 동안 주소에 NACK하고, 0x48 온도센서는 mux 채널이 열려 있을 때만 버스에 보인다 — 진짜 하드웨어와 같다.

```c
/* h5_fakebus 2/6 — target 한 바이트 쓰기/읽기 */
/* write 바이트 하나. first = 이 write 의 첫 바이트(레지스터 포인터 / word address) */
static int h5_wr(struct h5_bus *b, int t, u8 v, bool first, int *nwr)
{
	int i = t - T_TMP0;

	if (t == T_MUX) { b->mux_ctl = v; return 0; }
	if (t == T_IMU) {
		if (first) { b->imu_ptr = v; return 0; }
		if (b->imu_ptr >= 0x80) return -EREMOTEIO;      /* 없는 레지스터 → data NACK */
		if (b->imu_ptr != 0x75) b->imu[b->imu_ptr] = v; /* WHO_AM_I 는 읽기 전용 */
		b->imu_ptr++;
		return 0;
	}
	if (t == T_EE) {
		if (first) { b->ee_ptr = v; return 0; }
		b->ee[b->ee_ptr] = v;                 /* page(8B) 안에서만 증가 → 넘치면 page 앞으로 wrap */
		b->ee_ptr = (b->ee_ptr & ~7) | ((b->ee_ptr + 1) & 7);
		(*nwr)++;
		return 0;
	}
	if (first) b->tmp_ptr[i] = v & 3; else b->tmp[i][b->tmp_ptr[i]] = v;
	return 0;
}
static u8 h5_rd(struct h5_bus *b, int t)
{
	int i = t - T_TMP0, k;
	u8 v;

	if (t == T_MUX) return b->mux_ctl;
	if (t == T_EE) return b->ee[b->ee_ptr++];          /* u8 이라 256 에서 0 으로 wrap */
	if (t == T_IMU) {
		if (b->imu_ptr == 0x3B) {                   /* ACCEL_XOUT_H 부터 읽으면 새 샘플 latch */
			s16 s[3] = { b->sample, (s16)-b->sample, 16384 };
			b->sample++;
			for (k = 0; k < 3; k++) {
				b->imu[0x3B + 2 * k] = (u16)s[k] >> 8;
				b->imu[0x3C + 2 * k] = s[k] & 0xff;
			}
		}
		v = b->imu[b->imu_ptr];
		b->imu_ptr = (b->imu_ptr + 1) & 0x7f;           /* auto-increment */
		return v;
	}
	v = b->tmp[i][b->tmp_ptr[i]];
	b->tmp_ptr[i] = (b->tmp_ptr[i] + 1) & 3;
	return v;
}
```

레지스터형 I2C 장치의 공통 모델이 여기 다 있다: **write의 첫 바이트 = 내부 포인터**, 이후 바이트는 포인터 위치에 쓰고 증가, read는 포인터 위치부터 읽고 증가. EEPROM의 page wrap(8바이트 page 경계를 넘으면 같은 page 앞으로 돌아감)은 데이터시트의 그 함정 그대로다.

```c
/* h5_fakebus 3/6 — i2c_msg 배열을 "버스 위 파형"으로 실행 */
static int h5_run(struct h5_bus *b, struct i2c_msg *msgs, int num)
{
	int i, j, t = T_NONE, ret = num, nwr = 0;
	bool first = true;

	for (i = 0; i < num; i++) {
		struct i2c_msg *m = &msgs[i];

		if (m->flags & I2C_M_TEN) { ret = -EAFNOSUPPORT; goto out; }
		if (i == 0 || !(m->flags & I2C_M_NOSTART)) {   /* (repeated) START + 주소 + ACK */
			b->bits += 10;
			t = h5_target(b, m->addr);
			if (t == T_NONE) { b->nacks++; ret = -ENXIO; goto out; }
			first = true;
		}
		b->msgs++;
		if (m->flags & I2C_M_RD) {
			j = 0;
			if (m->flags & I2C_M_RECV_LEN) {        /* SMBus block read: 첫 바이트 = 길이 */
				m->buf[0] = (t == T_IMU) ? 6 : 1;
				m->len += m->buf[0];
				j = 1;
			}
			for (; j < m->len; j++) m->buf[j] = h5_rd(b, t);
		} else {
			for (j = 0; j < m->len; j++, first = false) {
				ret = h5_wr(b, t, m->buf[j], first, &nwr);
				if (ret) { b->nacks++; goto out; }
			}
			ret = num;
		}
		b->bytes += m->len;
		b->bits += 9 * m->len;
	}
out:
	b->bits += 1;                                         /* STOP */
	if (nwr) b->ee_busy_until = ktime_add_us(ktime_get(), 5000);  /* tWR = 5 ms 동안 NACK */
	return ret;
}
```

진짜 컨트롤러 드라이버의 `.xfer`도 뼈대가 똑같다: 메시지마다 (NOSTART가 아니면) START/Sr + 주소를 내보내고 ACK를 확인 → 바이트를 내보내거나 받아 오고 → 끝에 STOP. 차이는 "레지스터에 쓰고 완료 인터럽트를 `wait_for_completion_timeout()`으로 기다린다"는 부분뿐이다. `I2C_M_RECV_LEN` 처리 규칙(첫 바이트를 받아 `msg->len`에 더한다)은 실제 컨트롤러 드라이버들이 하는 방식 그대로다. 버스 시간 계산은 바이트당 9비트(8+ACK), START·주소 묶음 10비트, STOP 1비트로 단순화했다.

```c
/* h5_fakebus 4/6 — i2c_algorithm: .xfer / .functionality (+ fault injection) */
static int h5_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct h5_bus *b = i2c_get_adapdata(adap);
	int ret;

	b->xfers++;
	if (b->inject_eagain) {                 /* 다른 master 에게 arbitration 을 졌다 */
		b->inject_eagain--;
		return -EAGAIN;                     /* core 가 adap->retries 번 다시 부른다 */
	}
	if (b->stuck_sda_clocks || b->scl_stuck) {   /* 버스가 안 풀려 START 를 못 건다 */
		msleep(jiffies_to_msecs(adap->timeout)); /* 실제 HW 라면 bus-free 를 기다리다 timeout */
		dev_warn(&adap->dev, "bus busy (SDA=%d SCL=%d) -> i2c_recover_bus()\n",
			 b->stuck_sda_clocks == 0, !b->scl_stuck);
		ret = i2c_recover_bus(adap);
		dev_warn(&adap->dev, "recovery %s after %d SCL pulses (ret=%d)\n",
			 ret ? "FAILED" : "ok", b->pulses, ret);
		if (ret)
			return -ETIMEDOUT;
	}
	return h5_run(b, msgs, num);
}

static u32 h5_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_NOSTART | I2C_FUNC_SMBUS_EMUL |
	       I2C_FUNC_SMBUS_READ_BLOCK_DATA;
}

static const struct i2c_algorithm h5_algo = {
	.xfer = h5_xfer,                 /* 6.18: master_xfer 는 deprecated 별칭 (union) */
	.functionality = h5_func,
};
```

`.functionality`는 "이 버스가 무엇을 할 수 있나"를 비트마스크로 알린다. `I2C_FUNC_I2C`는 "임의의 `i2c_msg` 조합 가능", `I2C_FUNC_SMBUS_EMUL`은 "SMBus 명령 대부분을 core가 에뮬레이션해 줄 수 있음"이다(raw I2C가 되면 SMBus는 공짜로 따라온다). client driver는 probe에서 `i2c_check_functionality()`로 이 비트를 확인한다.

```c
/* h5_fakebus 5/6 — bus recovery 콜백 + debugfs 통계 */
static int h5_get_scl(struct i2c_adapter *a)
{
	struct h5_bus *b = i2c_get_adapdata(a);
	return b->scl_stuck ? 0 : b->scl;
}
static void h5_set_scl(struct i2c_adapter *a, int v)
{
	struct h5_bus *b = i2c_get_adapdata(a);
	if (v && !b->scl) {                         /* rising edge 하나 = 클럭 하나 */
		b->pulses++;
		if (b->stuck_sda_clocks)
			b->stuck_sda_clocks--;      /* target 이 밀다 만 비트를 하나씩 내보낸다 */
	}
	b->scl = v;
}
static int h5_get_sda(struct i2c_adapter *a)
{
	return ((struct h5_bus *)i2c_get_adapdata(a))->stuck_sda_clocks == 0;
}
static void h5_prep(struct i2c_adapter *a)
{
	((struct h5_bus *)i2c_get_adapdata(a))->pulses = 0;
	dev_info(&a->dev, "recovery: pins -> GPIO mode, SDA=%d\n", h5_get_sda(a));
}
static void h5_unprep(struct i2c_adapter *a)
{
	dev_info(&a->dev, "recovery: pins -> I2C mode, SDA=%d\n", h5_get_sda(a));
}
static int h5_stats_show(struct seq_file *s, void *p)
{
	struct h5_bus *b = s->private;
	seq_printf(s, "xfers=%llu msgs=%llu bytes=%llu nacks=%llu bus_bits=%llu bus_us=%llu\n",
		   b->xfers, b->msgs, b->bytes, b->nacks, b->bits,
		   div_u64(b->bits * 1000000ULL, b->bus_hz));
	return 0;
}
static int h5_stats_open(struct inode *i, struct file *f)
{
	return single_open(f, h5_stats_show, i->i_private);
}
static ssize_t h5_stats_clear(struct file *f, const char __user *u, size_t n, loff_t *o)
{
	struct h5_bus *b = ((struct seq_file *)f->private_data)->private;
	b->xfers = b->msgs = b->bytes = b->nacks = b->bits = 0;
	return n;
}
static const struct file_operations h5_stats_fops = {
	.owner = THIS_MODULE, .open = h5_stats_open, .read = seq_read,
	.write = h5_stats_clear, .llseek = seq_lseek, .release = single_release,
};
```

recovery 콜백은 7장에서 자세히 본다. 여기서는 "SCL을 GPIO처럼 올리고 내리는 함수, SDA 레벨을 읽는 함수를 core에 주면, 9 클럭 알고리즘은 core(`i2c_generic_scl_recovery`)가 돌린다"는 구조만 기억하자.

```c
/* h5_fakebus 6/6 — probe: adapter 등록, platform driver */
static int h5_probe(struct platform_device *pdev)
{
	struct h5_bus *b = devm_kzalloc(&pdev->dev, sizeof(*b), GFP_KERNEL);
	struct i2c_timings t;
	int ret;

	if (!b)
		return -ENOMEM;
	b->imu[0x75] = 0x68; b->imu[0x6B] = 0x40;            /* WHO_AM_I, PWR_MGMT_1=SLEEP */
	memset(b->ee, 0xff, sizeof(b->ee)); memcpy(b->ee, "H5-EEPROM-24C02", 15);
	b->tmp[0][0] = 25; b->tmp[1][0] = 30;                /* 채널별 온도 (°C) */
	b->scl = 1;
	i2c_parse_fw_timings(&pdev->dev, &t, true);          /* DT clock-frequency */
	b->bus_hz = t.bus_freq_hz;
	b->rinfo = (struct i2c_bus_recovery_info){ .recover_bus = i2c_generic_scl_recovery,
		.get_scl = h5_get_scl, .set_scl = h5_set_scl, .get_sda = h5_get_sda,
		.prepare_recovery = h5_prep, .unprepare_recovery = h5_unprep };
	strscpy(b->adap.name, "h5-fakebus", sizeof(b->adap.name));
	b->adap.owner = THIS_MODULE;
	b->adap.algo = &h5_algo;
	b->adap.retries = 3;
	b->adap.timeout = msecs_to_jiffies(50);
	b->adap.bus_recovery_info = &b->rinfo;
	b->adap.dev.parent = &pdev->dev;
	b->adap.dev.of_node = pdev->dev.of_node;   /* ← 이 줄이 DT 자식 노드 → i2c_client 생성의 열쇠 */
	i2c_set_adapdata(&b->adap, b);
	platform_set_drvdata(pdev, b);
	b->dbg = debugfs_create_dir("h5_fakebus", NULL);
	debugfs_create_u32("inject_eagain", 0600, b->dbg, &b->inject_eagain);
	debugfs_create_u32("stuck_sda_clocks", 0600, b->dbg, &b->stuck_sda_clocks);
	debugfs_create_bool("scl_stuck", 0600, b->dbg, &b->scl_stuck);
	debugfs_create_file("stats", 0600, b->dbg, b, &h5_stats_fops);
	ret = devm_i2c_add_adapter(&pdev->dev, &b->adap);
	if (ret) { debugfs_remove_recursive(b->dbg); return ret; }
	dev_info(&pdev->dev, "i2c-%d registered, %u Hz\n", b->adap.nr, b->bus_hz);
	return 0;
}
static void h5_remove(struct platform_device *pdev)
{
	debugfs_remove_recursive(((struct h5_bus *)platform_get_drvdata(pdev))->dbg);
}

static const struct of_device_id h5_of_match[] = {
	{ .compatible = "h5,fakebus" },
	{ }
};
MODULE_DEVICE_TABLE(of, h5_of_match);

static struct platform_driver h5_fakebus_driver = {
	.probe = h5_probe,
	.remove = h5_remove,
	.driver = { .name = "h5_fakebus", .of_match_table = h5_of_match },
};
module_platform_driver(h5_fakebus_driver);
MODULE_DESCRIPTION("H5: emulated I2C bus controller with fake targets");
MODULE_LICENSE("GPL");
```

진짜 SoC 컨트롤러 드라이버의 probe와 비교하면, 빠진 것은 `devm_platform_ioremap_resource()`(H1), `devm_clk_get_enabled()`(H3), `platform_get_irq()` + `devm_request_irq()`(F)뿐이고 나머지 adapter 필드 채우기는 똑같다. `adap.nr`를 지정하지 않았으므로(0) 번호는 동적으로 받는다(`i2c_add_adapter`). 보드에서 번호를 고정하고 싶으면 DT `aliases { i2c3 = &i2c3; }`를 두면 core가 그 번호를 쓴다.

빌드 결과:

```sh
sh /Users/donh/.elinux_scratch/H5/build.sh
```

```text
  CC [M]  h5_fakebus.o
  CC [M]  h5_imu.o
  MODPOST Module.symvers
  CC [M]  h5_fakebus.mod.o
  CC [M]  .module-common.o
  CC [M]  h5_imu.mod.o
  LD [M]  h5_fakebus.ko
  LD [M]  h5_imu.ko
```

출력에서 볼 것: 경고 0개. `.module-common.o`는 6.x 최근 Kbuild가 모듈 공통 부분을 따로 빌드하는 것이다.

---

## 2. client는 어떻게 생기나 — DT, new_device, board info, ACPI

### 2.1 직관 — "버스가 등록되는 순간, 그 버스에 매달린 장치 목록을 펼친다"

I2C는 PCIe·USB와 달리 **장치를 스스로 찾지(enumerate) 못한다.** 주소를 찔러 보는 건 위험하고(6.4절), ACK만으로는 무슨 칩인지 모른다. 그래서 "이 버스의 0x68에는 h5,imu가 있다"는 정보를 **바깥에서** 줘야 한다. 그 방법이 네 가지다.

| 방법 | 언제 | 커널 함수 | 이 노트 |
|---|---|---|---|
| DT 자식 노드 | ARM 보드의 표준 | adapter 등록 시 `of_i2c_register_devices(adap)` | 2.2 실행 |
| sysfs `new_device` | 실험·bring-up, DT 수정 전 | `i2c_new_client_device()` | 2.3 실행 |
| 코드에서 직접 | 다른 드라이버가 보조 칩을 만들 때 (예: at24의 SPD 온도센서, MFD) | `i2c_new_client_device(adap, &info)` | 2.4 발췌 |
| board file / ACPI | 옛 ARM board file(`i2c_register_board_info()`) / x86 ACPI `I2cSerialBusV2` | `i2c_scan_static_board_info()` / `i2c_acpi_register_devices()` | 설명만 |

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="40" width="210" height="180" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5 4"/>
<text x="115" y="60" font-size="13" text-anchor="middle">Device Tree (h5.dtb)</text>
<text x="22" y="86" font-size="12">i2c-h5 {</text>
<text x="34" y="106" font-size="12">compatible = h5,fakebus</text>
<text x="34" y="126" font-size="12">imu@68: h5,imu</text>
<text x="34" y="146" font-size="12">eeprom@50: atmel,24c02</text>
<text x="34" y="166" font-size="12">i2c-mux@70: nxp,pca9548</text>
<text x="48" y="186" font-size="12">i2c@0 { }  i2c@1 { }</text>
<text x="22" y="206" font-size="12">}</text>
<rect x="245" y="40" width="200" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="345" y="58" font-size="13" text-anchor="middle">platform_device</text>
<text x="345" y="76" font-size="12" text-anchor="middle">i2c-h5</text>
<rect x="480" y="40" width="190" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="575" y="58" font-size="13" text-anchor="middle">h5_fakebus</text>
<text x="575" y="76" font-size="12" text-anchor="middle">platform_driver</text>
<line x1="478" y1="62" x2="447" y2="62" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<text x="462" y="100" font-size="12" text-anchor="middle">probe</text>
<rect x="245" y="130" width="200" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="345" y="150" font-size="13" text-anchor="middle">i2c_adapter  i2c-0</text>
<text x="345" y="169" font-size="12" text-anchor="middle">algo = &amp;h5_algo</text>
<line x1="575" y1="84" x2="447" y2="152" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<text x="590" y="126" font-size="12" text-anchor="middle">devm_i2c_add_adapter()</text>
<line x1="220" y1="62" x2="243" y2="62" stroke="#888" stroke-width="1.5" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
<line x1="220" y1="140" x2="243" y2="150" stroke="#888" stroke-width="1.5" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
<line x1="345" y1="180" x2="345" y2="248" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<text x="352" y="208" font-size="12">of_i2c_register_devices()</text>
<text x="345" y="242" font-size="12" text-anchor="middle">i2c_client (DT 자식마다 1개)</text>
<rect x="245" y="250" width="200" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="345" y="275" font-size="12" text-anchor="middle">0-0068  name=imu</text>
<rect x="245" y="305" width="200" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="345" y="330" font-size="12" text-anchor="middle">0-0050  name=24c02</text>
<rect x="245" y="360" width="200" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="345" y="385" font-size="12" text-anchor="middle">0-0070  name=pca9548</text>
<text x="575" y="242" font-size="12" text-anchor="middle">i2c_driver (compatible 매칭)</text>
<rect x="480" y="250" width="190" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="575" y="275" font-size="12" text-anchor="middle">h5_imu</text>
<rect x="480" y="305" width="190" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="575" y="330" font-size="12" text-anchor="middle">at24</text>
<rect x="480" y="360" width="190" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="575" y="385" font-size="12" text-anchor="middle">pca954x</text>
<line x1="447" y1="270" x2="478" y2="270" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<line x1="447" y1="325" x2="478" y2="325" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<line x1="447" y1="380" x2="478" y2="380" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<rect x="480" y="415" width="190" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="575" y="440" font-size="12" text-anchor="middle">i2c-1 … i2c-8 (mux 자식)</text>
<line x1="575" y1="400" x2="575" y2="413" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<text x="20" y="260" font-size="12">점선: of_node 연결</text>
<text x="20" y="280" font-size="12">platform_device ← 루트 노드</text>
<text x="20" y="300" font-size="12">adapter.dev.of_node ← i2c-h5</text>
</svg>
```
그림 2 — 객체 그래프. DT의 `i2c-h5` 노드가 platform_device가 되고, 그 드라이버(h5_fakebus)가 adapter를 등록하면서 `adap.dev.of_node`를 넘기면, core가 그 노드의 자식들을 훑어 client를 만든다. client마다 compatible로 i2c_driver를 찾아 probe한다. pca954x는 자기 아래에 adapter를 다시 8개 만든다.

### 2.2 예제 2 — DT 자식 노드로 client 만들기

무엇을 확인하나: 컨트롤러 노드 아래 자식 노드의 `reg`가 주소, `compatible`이 드라이버 매칭 키다. 랩의 `virt.dts`를 복사하고 끝에 아래 노드를 덧붙여 `dtc`로 `h5.dtb`를 만들었다(`virt.dts`에 원래 있던 clocks/gpios 경고 5개 외에 새 경고 없음).

```dts
/ {
	h5_i2c: i2c-h5 {
		compatible = "h5,fakebus";
		#address-cells = <1>;
		#size-cells = <0>;
		clock-frequency = <400000>;

		imu@68 {
			compatible = "h5,imu";
			reg = <0x68>;
		};

		eeprom@50 {
			compatible = "atmel,24c02";
			reg = <0x50>;
			pagesize = <8>;
		};

		i2c-mux@70 {
			compatible = "nxp,pca9548";
			reg = <0x70>;
			#address-cells = <1>;
			#size-cells = <0>;

			i2c@0 {
				reg = <0>;
				#address-cells = <1>;
				#size-cells = <0>;
			};
			i2c@1 {
				reg = <1>;
				#address-cells = <1>;
				#size-cells = <0>;
			};
		};
	};
};
```

- I2C 버스 노드는 반드시 `#address-cells = <1>; #size-cells = <0>;` — 자식의 `reg`가 "주소 하나, 크기 없음"이라는 뜻이다(D2).
- `clock-frequency`는 버스 속도. adapter 드라이버가 `i2c_parse_fw_timings()`로 읽는다. 진짜 보드라면 이 노드에 `reg`(MMIO), `interrupts`, `clocks`, `pinctrl-0`도 있고 노드 이름은 `i2c@30a20000`처럼 주소가 붙는다. 이 가짜 컨트롤러는 MMIO가 없어 `i2c-h5`로 했다.
- `pagesize = <8>`은 at24 바인딩 속성이다. 빠지면 at24는 안전하게 1바이트씩 쓴다(at24.c 주석: "This is slow, but we can't know all eeproms").

게스트 스크립트 `t1.sh`(앞부분):

```sh
echo "### uname"; uname -r
echo "### insmod h5_fakebus"; insmod /h5_fakebus.ko; dmesg | grep -E "h5|i2c|pca954|at24" | tail -10
echo "### /sys/bus/i2c/devices"; ls -1 /sys/bus/i2c/devices/ | tr "\n" " "; echo
for d in /sys/bus/i2c/devices/0-*; do echo "$d name=$(cat $d/name) modalias=$(cat $d/modalias) driver=$(basename $(readlink $d/driver) 2>/dev/null)"; done
echo "### adapters"; for a in /sys/bus/i2c/devices/i2c-*; do echo "$(basename $a): $(cat $a/name)"; done
echo "### insmod h5_imu, at24"; insmod /h5_imu.ko; insmod /at24.ko; dmesg | tail -4
for d in /sys/bus/i2c/devices/0-*; do echo "$(basename $d) driver=$(basename $(readlink $d/driver) 2>/dev/null)"; done
echo "### of_node of client 0-0068"; ls -l /sys/bus/i2c/devices/0-0068/of_node | sed 's/.*-> //'
```

```sh
sh /Users/donh/.elinux_scratch/H5/boot.sh t1.sh
```

```text
lab initramfs: 6.18.55-dbg up 6.34s
### uname
6.18.55-dbg
### insmod h5_fakebus
[    6.778042] i2c i2c-0: Added multiplexed i2c bus 1
[    6.782012] i2c i2c-0: Added multiplexed i2c bus 2
[    6.785482] i2c i2c-0: Added multiplexed i2c bus 3
[    6.788593] i2c i2c-0: Added multiplexed i2c bus 4
[    6.792152] i2c i2c-0: Added multiplexed i2c bus 5
[    6.796292] i2c i2c-0: Added multiplexed i2c bus 6
[    6.799692] i2c i2c-0: Added multiplexed i2c bus 7
[    6.802871] i2c i2c-0: Added multiplexed i2c bus 8
[    6.803026] pca954x 0-0070: registered 8 multiplexed busses for I2C switch pca9548
[    6.804218] h5_fakebus i2c-h5: i2c-0 registered, 400000 Hz
### /sys/bus/i2c/devices
0-0050 0-0068 0-0070 i2c-0 i2c-1 i2c-2 i2c-3 i2c-4 i2c-5 i2c-6 i2c-7 i2c-8 
/sys/bus/i2c/devices/0-0050 name=24c02 modalias=of:NeepromT(null)Catmel,24c02 driver=
/sys/bus/i2c/devices/0-0068 name=imu modalias=of:NimuT(null)Ch5,imu driver=
/sys/bus/i2c/devices/0-0070 name=pca9548 modalias=of:Ni2c-muxT(null)Cnxp,pca9548 driver=pca954x
### adapters
i2c-0: h5-fakebus
i2c-1: i2c-0-mux (chan_id 0)
i2c-2: i2c-0-mux (chan_id 1)
i2c-3: i2c-0-mux (chan_id 2)
i2c-4: i2c-0-mux (chan_id 3)
i2c-5: i2c-0-mux (chan_id 4)
i2c-6: i2c-0-mux (chan_id 5)
i2c-7: i2c-0-mux (chan_id 6)
i2c-8: i2c-0-mux (chan_id 7)
### insmod h5_imu, at24
[    6.804218] h5_fakebus i2c-h5: i2c-0 registered, 400000 Hz
[    7.945678] h5_imu 0-0068: probed: WHO_AM_I=0x68 on h5-fakebus (addr 0x68, name 'imu')
[    7.985966] at24 0-0050: supply vcc not found, using dummy regulator
[    7.992215] at24 0-0050: 256 byte 24c02 EEPROM, writable, 8 bytes/write
0-0050 driver=at24
0-0068 driver=h5_imu
0-0070 driver=pca954x
### of_node of client 0-0068
../../../../../firmware/devicetree/base/i2c-h5/imu@68
```

출력에서 볼 것:

- **순서**: pca954x의 "Added multiplexed i2c bus" 로그가 h5_fakebus의 "registered" 로그보다 **먼저** 찍혔다. `devm_i2c_add_adapter()` 안에서 core가 `of_i2c_register_devices()`로 client를 만들고, 그중 pca954x는 커널에 built-in(`=y`)이라 그 자리에서 probe까지 끝났기 때문이다. 우리 `dev_info`는 add_adapter가 **반환한 뒤**에 찍힌다.
- client는 드라이버가 없어도 **먼저 생긴다**(`0-0068 driver=` 비어 있음). 나중에 `insmod h5_imu.ko`를 하면 driver core가 기존 client와 매칭해 probe한다(G2의 "device와 driver는 누가 먼저 와도 된다").
- `name=imu`: DT client의 이름은 compatible `"h5,imu"`에서 vendor 접두사를 뗀 것(`of_alias_from_compatible()`)이다. modalias `of:NimuT(null)Ch5,imu`가 udev/modprobe가 자동 로딩에 쓰는 문자열이다.
- at24의 "supply vcc not found, using dummy regulator"는 DT에 `vcc-supply`가 없어서 나오는 정보성 메시지다(H3). "8 bytes/write"가 `pagesize`를 반영한 결과다.

### 2.3 예제 3 — sysfs `new_device` / `delete_device`

무엇을 확인하나: DT를 고치지 않고 셸에서 client를 만들고 지운다. `echo <이름> <주소> > new_device`의 이름은 드라이버의 **`id_table`** 이름과 매칭된다. 존재하지 않는 0x69에 만들어 probe 실패를 관찰한다.

```sh
echo h5-imu 0x69 > /sys/bus/i2c/devices/i2c-0/new_device; dmesg | tail -2
ls /sys/bus/i2c/devices/ | tr '\n' ' '; echo; cat /sys/bus/i2c/devices/0-0069/name; ls /sys/bus/i2c/devices/0-0069/ | tr '\n' ' '; echo
echo 0x69 > /sys/bus/i2c/devices/i2c-0/delete_device; dmesg | tail -1
```

```text
[    8.421876] h5_imu 0-0069: error -ENXIO: WHO_AM_I read failed
[    8.421876] h5_imu 0-0069: error -ENXIO: WHO_AM_I read failed
[    8.423224] i2c i2c-0: new_device: Instantiated device h5-imu at 0x69
0-0050 0-0068 0-0069 0-0070 i2c-0 i2c-1 i2c-2 i2c-3 i2c-4 i2c-5 i2c-6 i2c-7 i2c-8 
h5-imu
modalias name power subsystem uevent 
[    8.627125] i2c i2c-0: delete_device: Deleting device h5-imu at 0x69
```

출력에서 볼 것:

- 첫 줄은 KERN_ERR라 `quiet`(loglevel 4)에서도 **콘솔에 바로** 찍힌 것이고, 둘째 줄은 같은 메시지를 `dmesg`가 다시 보여 준 것이다.
- probe가 `-ENXIO`로 실패해도 **client(`0-0069`)는 남는다**. `driver` 링크가 없을 뿐이다. "장치 노드는 있는데 드라이버가 안 붙었다"는 이 상태가 bring-up에서 가장 흔한 그림이다(2.5).
- 드라이버 코어는 probe의 `-ENXIO`/`-ENODEV`를 "이 장치는 내 것이 아님"으로 보고 경고를 찍지 않는다. 그래서 진단 메시지는 **드라이버가 직접** `dev_err_probe()`로 남겨야 한다.
- `delete_device`는 `new_device`로 만든 client만 지울 수 있다(DT로 생긴 것은 안 된다).

### 2.4 코드에서 client 만들기 — `i2c_new_client_device()` (발췌, 이 노트에서 단독 실행 안 함)

다른 드라이버가 같은 버스의 보조 칩을 만들 때 쓴다. `new_device` sysfs가 내부에서 부르는 것도 이 함수다(2.3에서 간접 실행).

```c
struct i2c_board_info info = { I2C_BOARD_INFO("h5-imu", 0x69) };   /* type, addr */
struct i2c_client *c = i2c_new_client_device(adap, &info);
if (IS_ERR(c))
	return PTR_ERR(c);          /* NULL 이 아니라 ERR_PTR 을 돌려준다 (옛 i2c_new_device 는 NULL) */
/* ... */
i2c_unregister_device(c);
```

같은 구조체 `i2c_board_info`를 배열로 만들어 부팅 초기에 `i2c_register_board_info(busnum, info, n)`로 등록하던 것이 DT 이전 ARM board file 방식이다. 이 함수는 built-in 코드에서만 쓸 수 있고(adapter가 등록되기 **전에** 불려야 함), 새 보드에서는 쓰지 않는다. x86/ACPI 플랫폼에서는 ACPI 테이블의 `I2cSerialBusV2` 리소스가 같은 역할을 한다(`i2c_acpi_register_devices()`).

### 2.5 매칭 순서 — OF → ACPI → id_table

`drivers/i2c/i2c-core-base.c`의 `i2c_device_match()`는 이 순서로 본다(6.18 소스 확인):

1. `i2c_of_match_device(drv->of_match_table, client)` — DT compatible
2. `acpi_driver_match_device()` — ACPI
3. `i2c_match_id(driver->id_table, client)` — client 이름 대 `id_table` 이름

그래서 client driver는 보통 **둘 다** 둔다: `of_match_table`(DT 보드용)과 `id_table`(new_device·board_info·`i2c_new_client_device`용). `id_table`이 없으면 2.3처럼 셸에서 instantiate한 장치에 붙지 않는다. 또 `MODULE_DEVICE_TABLE(of, ...)`와 `MODULE_DEVICE_TABLE(i2c, ...)`가 있어야 모듈이 modalias로 **자동 로딩**된다.

---

## 3. client driver 쓰기 — `h5_imu`

### 3.1 probe의 정석

I2C 센서 드라이버의 probe는 G3의 platform probe와 같은 "bring-up 순서"를 따른다. 차이는 MMIO 대신 **버스 트랜잭션으로 ID를 확인**한다는 것.

1. `i2c_check_functionality()` — 이 버스가 내가 쓸 전송 방식을 지원하나
2. (H3) 전원 레귤레이터 enable, reset GPIO 해제, 대기
3. **ID 레지스터 읽기** — 응답이 없으면 `-ENXIO`, 다른 칩이면 `-ENODEV`
4. 초기 설정 (sleep 해제 등)
5. 상위 프레임워크 등록 (IIO, input, hwmon … — 이 노트는 sysfs 속성으로 대신)

```c
// SPDX-License-Identifier: GPL-2.0
/* h5_imu 1/3 — I2C client driver: SMBus helper / i2c_transfer / bus lock 세 방식으로 읽기 */
#include <linux/module.h>
#include <linux/i2c.h>

#define REG_ACCEL 0x3B
#define REG_PWR   0x6B
#define REG_WHO   0x75

static int h5_imu_regs(struct i2c_client *c, u8 reg, u8 *buf, u16 len)
{
	struct i2c_msg m[2] = {
		{ .addr = c->addr, .flags = 0,        .len = 1,   .buf = &reg },
		{ .addr = c->addr, .flags = I2C_M_RD, .len = len, .buf = buf },
	};                                       /* 두 msg 사이 = repeated START */
	int ret = i2c_transfer(c->adapter, m, 2);
	return ret == 2 ? 0 : ret < 0 ? ret : -EIO;
}

static ssize_t accel_show(struct device *dev, struct device_attribute *a, char *out)
{
	struct i2c_client *c = to_i2c_client(dev);
	u8 b[6];
	int ret = h5_imu_regs(c, REG_ACCEL, b, 6);
	if (ret)
		return ret;
	return sysfs_emit(out, "%d %d %d\n", (s16)(b[0] << 8 | b[1]),
			  (s16)(b[2] << 8 | b[3]), (s16)(b[4] << 8 | b[5]));
}
static DEVICE_ATTR_RO(accel);
```

`h5_imu_regs()`가 이 노트에서 가장 중요한 10줄이다. `i2c_transfer()`의 반환값은 **처리된 메시지 수**라서 성공은 `2`다. `0`이나 `1`(일부만 처리)이 올 수도 있으니 `== 2`로 확인하고, 음수는 그대로 올린다. 이 습관이 없으면 "ret이 양수라 성공인 줄 알았다"는 버그가 생긴다. 주의: `buf`는 스택 변수여도 `i2c_transfer()`에는 괜찮지만, DMA를 쓰는 컨트롤러 드라이버는 내부에서 바운스 버퍼를 쓴다(`I2C_M_DMA_SAFE`가 없는 메시지는 DMA-safe가 아니라고 가정 — `i2c_get_dma_safe_msg_buf()`).

```c
/* h5_imu 2/3 — 바이트 단위 SMBus 읽기, bus lock 으로 묶은 두 트랜잭션 */
static ssize_t accel_bytewise_show(struct device *dev, struct device_attribute *a, char *out)
{
	struct i2c_client *c = to_i2c_client(dev);
	int i, v[6];
	for (i = 0; i < 6; i++) {               /* 레지스터마다 별도 트랜잭션: 샘플이 찢어질 수 있다 */
		v[i] = i2c_smbus_read_byte_data(c, REG_ACCEL + i);
		if (v[i] < 0)
			return v[i];
	}
	return sysfs_emit(out, "%d %d %d\n", (s16)(v[0] << 8 | v[1]),
			  (s16)(v[2] << 8 | v[3]), (s16)(v[4] << 8 | v[5]));
}
static DEVICE_ATTR_RO(accel_bytewise);

static ssize_t burst_show(struct device *dev, struct device_attribute *a, char *out)
{
	struct i2c_client *c = to_i2c_client(dev);
	u8 reg = REG_WHO, who = 0, pwr = 0, r2 = REG_PWR;
	struct i2c_msg m1[2] = { { c->addr, 0, 1, &reg }, { c->addr, I2C_M_RD, 1, &who } };
	struct i2c_msg m2[2] = { { c->addr, 0, 1, &r2 }, { c->addr, I2C_M_RD, 1, &pwr } };
	int r1, rr2;

	i2c_lock_bus(c->adapter, I2C_LOCK_SEGMENT);   /* 두 트랜잭션 사이에 남이 못 끼게 */
	r1 = __i2c_transfer(c->adapter, m1, 2);       /* lock 을 이미 잡았으니 __ 버전 */
	rr2 = __i2c_transfer(c->adapter, m2, 2);
	i2c_unlock_bus(c->adapter, I2C_LOCK_SEGMENT);
	return sysfs_emit(out, "who=0x%02x pwr=0x%02x (ret %d %d)\n", who, pwr, r1, rr2);
}
static DEVICE_ATTR_RO(burst);

static struct attribute *h5_imu_attrs[] = {
	&dev_attr_accel.attr, &dev_attr_accel_bytewise.attr, &dev_attr_burst.attr, NULL
};
ATTRIBUTE_GROUPS(h5_imu);
```

```c
/* h5_imu 3/3 — probe/remove, 매칭 테이블 */
static int h5_imu_probe(struct i2c_client *client)       /* 6.3+ : 인자 하나 */
{
	struct device *dev = &client->dev;
	int who, ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_I2C | I2C_FUNC_SMBUS_BYTE_DATA))
		return -EOPNOTSUPP;
	who = i2c_smbus_read_byte_data(client, REG_WHO);    /* 1) 살아 있나? */
	if (who < 0)
		return dev_err_probe(dev, who, "WHO_AM_I read failed\n");
	if (who != 0x68)
		return dev_err_probe(dev, -ENODEV, "unexpected WHO_AM_I 0x%02x\n", who);
	ret = i2c_smbus_write_byte_data(client, REG_PWR, 0x00); /* 2) sleep 해제 */
	if (ret)
		return ret;
	dev_info(dev, "probed: WHO_AM_I=0x%02x on %s (addr 0x%02x, name '%s')\n",
		 who, client->adapter->name, client->addr, client->name);
	return 0;
}

static void h5_imu_remove(struct i2c_client *client)
{
	i2c_smbus_write_byte_data(client, REG_PWR, 0x40);   /* 다시 sleep */
	dev_info(&client->dev, "removed\n");
}

static const struct i2c_device_id h5_imu_id[] = {   /* new_device / board_info 이름 매칭 */
	{ "h5-imu" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, h5_imu_id);

static const struct of_device_id h5_imu_of[] = {     /* DT compatible 매칭 */
	{ .compatible = "h5,imu" },
	{ }
};
MODULE_DEVICE_TABLE(of, h5_imu_of);

static struct i2c_driver h5_imu_driver = {
	.driver = { .name = "h5_imu", .of_match_table = h5_imu_of,
		    .dev_groups = h5_imu_groups },
	.probe = h5_imu_probe,
	.remove = h5_imu_remove,
	.id_table = h5_imu_id,
};
module_i2c_driver(h5_imu_driver);
MODULE_DESCRIPTION("H5: example I2C client driver for the fake IMU");
MODULE_LICENSE("GPL");
```

- `i2c_smbus_read_byte_data()`는 **값(0~255) 또는 음수 errno**를 돌려준다. 그래서 반환형이 `s32`이고 `u8`에 바로 받으면 에러를 놓친다.
- `.dev_groups`는 probe 성공 후 core가 sysfs 속성을 자동으로 만들고 remove 때 지운다(G8). `device_create_file()`을 probe에서 부르면 uevent 경쟁이 생긴다.
- `module_i2c_driver()`는 `i2c_add_driver()`/`i2c_del_driver()`를 부르는 init/exit를 만들어 주는 매크로다.
- 실제 센서 드라이버는 이 위에 **regmap**(H2)을 얹어 `regmap_bulk_read()`를 쓰는 게 표준이다. at24도 그렇다(4.5절 트레이스).

### 3.2 예제 4 — sysfs 속성 읽기, unbind/bind

```sh
cat /sys/bus/i2c/devices/0-0068/accel /sys/bus/i2c/devices/0-0068/accel /sys/bus/i2c/devices/0-0068/burst
```

```text
0 0 16384
1 -1 16384
who=0x68 pwr=0x00 (ret 2 2)
```

출력에서 볼 것: 읽을 때마다 가짜 샘플 번호가 1씩 늘어난다(x=n, y=-n, z=16384 = 1 g @ ±2 g 범위). `burst`의 `pwr=0x00`은 probe가 sleep을 해제한 결과다.

`t6.sh`로 sysfs 위치와 unbind/bind를 본다:

```sh
cd /sys/bus/i2c/devices; for x in i2c-0 0-0068 0-0050 i2c-2; do echo "$x -> $(readlink $x | sed 's#.*/devices/#/sys/devices/#')"; done
echo "### uevent 0-0068"; cat 0-0068/uevent
echo "### /dev/i2c-*"; ls -l /dev/i2c-0 /dev/i2c-2 | cut -c1-11,35-
echo 0-0068 > /sys/bus/i2c/drivers/h5_imu/unbind; dmesg | tail -1 | cut -c16-
i2cget -y 0 0x68 0x6b
echo 0-0068 > /sys/bus/i2c/drivers/h5_imu/bind; dmesg | tail -1 | cut -c16-
i2cget -y -f 0 0x68 0x6b
```

```text
i2c-0 -> /sys/devices/platform/i2c-h5/i2c-0
0-0068 -> /sys/devices/platform/i2c-h5/i2c-0/0-0068
0-0050 -> /sys/devices/platform/i2c-h5/i2c-0/0-0050
i2c-2 -> /sys/devices/platform/i2c-h5/i2c-0/i2c-2
### uevent 0-0068
DRIVER=h5_imu
OF_NAME=imu
OF_FULLNAME=/i2c-h5/imu@68
OF_COMPATIBLE_0=h5,imu
OF_COMPATIBLE_N=1
MODALIAS=of:NimuT(null)Ch5,imu
### /dev/i2c-*
crw-------   89,   0 Oct 11 03:58 /dev/i2c-0
crw-------   89,   2 Oct 11 03:58 /dev/i2c-2
h5_imu 0-0068: removed
0x40
h5_imu 0-0068: probed: WHO_AM_I=0x68 on h5-fakebus (addr 0x68, name 'imu')
0x00
```

출력에서 볼 것:

- 장치 트리(sysfs)가 **물리 토폴로지 그대로**다: platform_device → adapter(i2c-0) → client(0-0068), 그리고 mux 자식 adapter `i2c-2`도 `i2c-0` 아래에 있다. 보드에서 "이 센서가 어느 컨트롤러에 붙었나"는 `readlink`로 바로 안다.
- `/dev/i2c-N`은 char major 89, minor = 버스 번호다(A3).
- unbind하면 remove가 PWR_MGMT_1에 0x40(sleep)을 써서, 드라이버 없이 읽은 값이 `0x40`이다. bind하면 probe가 다시 0x00으로 깨운다. 드라이버가 붙은 뒤에는 `i2cget`에 `-f`(force)가 필요하다(6.2절).

---

## 4. 전송 API — `i2c_transfer`, `i2c_master_send/recv`, SMBus helper

### 4.1 레지스터 읽기 = `i2c_msg` 2개 = repeated START 하나

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<rect x="30" y="16" width="14" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="50" y="27" font-size="12">master(컨트롤러)가 SDA 구동</text>
<rect x="250" y="16" width="14" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="270" y="27" font-size="12">target(센서)이 SDA 구동</text>
<rect x="30" y="44" width="36" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="48" y="66" font-size="13" text-anchor="middle">S</text>
<rect x="66" y="44" width="100" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="116" y="66" font-size="13" text-anchor="middle">0x68+W</text>
<rect x="166" y="44" width="32" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="182" y="66" font-size="13" text-anchor="middle">A</text>
<rect x="198" y="44" width="72" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="234" y="66" font-size="13" text-anchor="middle">0x75</text>
<rect x="270" y="44" width="32" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="286" y="66" font-size="13" text-anchor="middle">A</text>
<rect x="302" y="44" width="40" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="322" y="66" font-size="13" text-anchor="middle">Sr</text>
<rect x="342" y="44" width="100" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="392" y="66" font-size="13" text-anchor="middle">0x68+R</text>
<rect x="442" y="44" width="32" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="458" y="66" font-size="13" text-anchor="middle">A</text>
<rect x="474" y="44" width="84" height="34" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="516" y="66" font-size="13" text-anchor="middle">data</text>
<rect x="558" y="44" width="50" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="583" y="66" font-size="13" text-anchor="middle">NACK</text>
<rect x="608" y="44" width="36" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="626" y="66" font-size="13" text-anchor="middle">P</text>
<path d="M32,84 L32,92 L300,92 L300,84" fill="none" stroke="currentColor"/>
<text x="166" y="110" font-size="12" text-anchor="middle">msgs[0]  .flags = 0</text>
<text x="166" y="126" font-size="12" text-anchor="middle">len=1  buf={0x75}</text>
<path d="M304,84 L304,92 L642,92 L642,84" fill="none" stroke="currentColor"/>
<text x="473" y="110" font-size="12" text-anchor="middle">msgs[1]  .flags = I2C_M_RD</text>
<text x="473" y="126" font-size="12" text-anchor="middle">len=1  buf={data}</text>
<text x="4" y="185" font-size="12">SDA</text><text x="4" y="237" font-size="12">SCL</text>
<path d="M30,170 L48.0,170 L52.0,192 L66,192" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M66,170 L70,192 L162,192 L166,170" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M66,192 L70,170 L162,170 L166,192" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M166,192 L198,192" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M198,170 L202,192 L266,192 L270,170" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M198,192 L202,170 L266,170 L270,192" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M270,192 L302,192" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M302,192 L308,170 L326.0,170 L330.0,192 L342,192" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M342,170 L346,192 L438,192 L442,170" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M342,192 L346,170 L438,170 L442,192" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M442,192 L474,192" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M474,170 L478,192 L554,192 L558,170" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M474,192 L478,170 L554,170 L558,192" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M558,192 L562,170 L608,170" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M608,192 L629.6,192 L633.6,170 L644,170" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<path d="M30,222 L58.8,222 L61.8,244 L66,244" fill="none" stroke="currentColor" stroke-width="1.2"/>
<path d="M66,244 L69.1,244 L69.1,222 L75.4,222 L75.4,244 L81.6,244 L81.6,222 L87.9,222 L87.9,244 L94.1,244 L94.1,222 L100.4,222 L100.4,244 L106.6,244 L106.6,222 L112.9,222 L112.9,244 L119.1,244 L119.1,222 L125.4,222 L125.4,244 L131.6,244 L131.6,222 L137.9,222 L137.9,244 L144.1,244 L144.1,222 L150.4,222 L150.4,244 L156.6,244 L156.6,222 L162.9,222 L162.9,244 L166,244" fill="none" stroke="currentColor" stroke-width="1.2"/>
<path d="M166,244 L174.0,244 L174.0,222 L190.0,222 L190.0,244 L198,244" fill="none" stroke="currentColor" stroke-width="1.2"/>
<path d="M198,244 L200.2,244 L200.2,222 L204.8,222 L204.8,244 L209.2,244 L209.2,222 L213.8,222 L213.8,244 L218.2,244 L218.2,222 L222.8,222 L222.8,244 L227.2,244 L227.2,222 L231.8,222 L231.8,244 L236.2,244 L236.2,222 L240.8,222 L240.8,244 L245.2,244 L245.2,222 L249.8,222 L249.8,244 L254.2,244 L254.2,222 L258.8,222 L258.8,244 L263.2,244 L263.2,222 L267.8,222 L267.8,244 L270,244" fill="none" stroke="currentColor" stroke-width="1.2"/>
<path d="M270,244 L278.0,244 L278.0,222 L294.0,222 L294.0,244 L302,244" fill="none" stroke="currentColor" stroke-width="1.2"/>
<path d="M302,244 L314.0,244 L314.0,222 L336.0,222 L336.0,244 L342,244" fill="none" stroke="currentColor" stroke-width="1.2"/>
<path d="M342,244 L345.1,244 L345.1,222 L351.4,222 L351.4,244 L357.6,244 L357.6,222 L363.9,222 L363.9,244 L370.1,244 L370.1,222 L376.4,222 L376.4,244 L382.6,244 L382.6,222 L388.9,222 L388.9,244 L395.1,244 L395.1,222 L401.4,222 L401.4,244 L407.6,244 L407.6,222 L413.9,222 L413.9,244 L420.1,244 L420.1,222 L426.4,222 L426.4,244 L432.6,244 L432.6,222 L438.9,222 L438.9,244 L442,244" fill="none" stroke="currentColor" stroke-width="1.2"/>
<path d="M442,244 L450.0,244 L450.0,222 L466.0,222 L466.0,244 L474,244" fill="none" stroke="currentColor" stroke-width="1.2"/>
<path d="M474,244 L476.6,244 L476.6,222 L481.9,222 L481.9,244 L487.1,244 L487.1,222 L492.4,222 L492.4,244 L497.6,244 L497.6,222 L502.9,222 L502.9,244 L508.1,244 L508.1,222 L513.4,222 L513.4,244 L518.6,244 L518.6,222 L523.9,222 L523.9,244 L529.1,244 L529.1,222 L534.4,222 L534.4,244 L539.6,244 L539.6,222 L544.9,222 L544.9,244 L550.1,244 L550.1,222 L555.4,222 L555.4,244 L558,244" fill="none" stroke="currentColor" stroke-width="1.2"/>
<path d="M558,244 L570.5,244 L570.5,222 L595.5,222 L595.5,244 L608,244" fill="none" stroke="currentColor" stroke-width="1.2"/>
<path d="M608,244 L617.0,244 L617.0,222 L644,222" fill="none" stroke="currentColor" stroke-width="1.2"/>
<line x1="48" y1="252" x2="48" y2="262" stroke="#d0564a"/>
<text x="48" y="276" font-size="12" text-anchor="middle">S</text>
<line x1="322" y1="252" x2="322" y2="262" stroke="#d0564a"/>
<text x="322" y="276" font-size="12" text-anchor="middle">Sr</text>
<line x1="626" y1="252" x2="626" y2="262" stroke="#d0564a"/>
<text x="626" y="276" font-size="12" text-anchor="middle">P</text>
<text x="30" y="306" font-size="12">S/Sr: SCL HIGH 동안 SDA 하강 · P: SCL HIGH 동안 SDA 상승 — Sr 은 STOP 없이 방향을 바꾼다</text>
<text x="30" y="330" font-size="12">write() 와 read() 를 따로 부르면 그 사이에 P 와 새 S 가 들어간다 (다른 master 가 끼어들 틈)</text>
</svg>
```
그림 3 — 레지스터 읽기(WHO_AM_I) 한 번이 `i2c_msg` 두 개로 표현되는 방식. msgs[0]은 write(레지스터 번호 1바이트), msgs[1]은 read. 둘 사이에 core는 STOP이 아니라 **repeated START(Sr)**를 넣는다. 마지막 읽기 바이트에 master가 NACK을 주는 것은 컨트롤러 드라이버의 몫이다.

Don에게는 익숙한 파형이다. Linux에서 새로 기억할 것은 "**배열 하나 = STOP 하나**"라는 규칙과, 그 배열을 만드는 API가 여럿이라는 점이다.

| API | 만드는 메시지 | 버스 파형 | 반환값 (성공) |
|---|---|---|---|
| `i2c_transfer(adap, msgs, n)` | 임의 n개 | S … Sr … Sr … P | n (처리한 msg 수) |
| `i2c_master_send(client, buf, len)` | write 1개 | S A+W d… P | len (바이트 수) |
| `i2c_master_recv(client, buf, len)` | read 1개 | S A+R d… P | len |
| `i2c_smbus_read_byte_data(client, reg)` | [W reg][R 1] (에뮬레이션 시) | S A+W reg Sr A+R d P | 값 0~255 |
| `i2c_smbus_write_byte_data(client, reg, v)` | [W reg,v] | S A+W reg v P | 0 |
| `i2c_smbus_read_word_data(client, reg)` | [W reg][R 2] | … Sr A+R lo hi P | 값 (**LSB 먼저** 조립) |
| `i2c_smbus_read_i2c_block_data(client, reg, len, buf)` | [W reg][R len], len ≤ 32 | … Sr A+R d1..dlen P | 읽은 바이트 수 |
| `i2c_smbus_read_block_data(client, reg, buf)` | [W reg][R RECV_LEN] | … Sr A+R N d1..dN P | N |

- **`i2c_master_send()` 다음에 `i2c_master_recv()`**를 부르면 중간에 STOP이 들어간다. 대부분의 센서는 그래도 동작하지만(내부 포인터가 STOP에서 리셋되지 않는 한), 버스에 다른 master가 있으면 사이에 끼어들 수 있고, 일부 칩은 STOP에서 포인터를 리셋한다. 레지스터 읽기는 `i2c_transfer()` 2-msg나 SMBus helper를 쓴다.
- **SMBus word는 little-endian**이다. 빅엔디언 레지스터(대부분의 IMU)는 `i2c_smbus_read_word_swapped()`를 쓰거나 바이트 두 개를 직접 조립한다. u1_rdwr 출력(6.1)에서 "LSB first"를 볼 수 있다.
- `i2c_smbus_read_i2c_block_data()`의 "i2c block"은 SMBus 규격의 진짜 block read가 아니라 **Linux 확장**이다(길이 바이트 없이 정해진 len만큼 읽음). 진짜 SMBus block read(장치가 첫 바이트로 길이를 알려 줌)는 `i2c_smbus_read_block_data()`이고, 이걸 지원하지 않는 컨트롤러가 많다.

### 4.2 예제 5 — ftrace i2c/smbus tracepoint로 "버스 위 메시지"를 보기

Don이 로직 분석기로 보던 것을 **소프트웨어에서** 보는 방법이다. i2c-core에는 tracepoint가 두 묶음 있다.

- `i2c:i2c_write`, `i2c:i2c_read`(요청), `i2c:i2c_reply`(읽은 데이터), `i2c:i2c_result`(반환값) — `__i2c_transfer()` 안
- `smbus:smbus_write`, `smbus:smbus_read`, `smbus:smbus_reply`, `smbus:smbus_result` — `__i2c_smbus_xfer()` 안

랩 debug 커널(`CONFIG_FTRACE=y`)에서 tracefs를 마운트하고 켠다. `t3.sh`:

```sh
insmod /h5_fakebus.ko; insmod /h5_imu.ko
dmesg -n 1
T=/sys/kernel/tracing; mount -t tracefs none $T
ls $T/events/i2c $T/events/smbus | tr '\n' ' '; echo
echo 1 > $T/events/i2c/enable; echo 1 > $T/events/smbus/enable
cap() { echo > $T/trace; eval "$1" > /dev/null; echo "### $1"; grep -v "^#" $T/trace | sed 's/^ *//; s/  */ /g' | cut -d' ' -f1,4-; }
cap "cat /sys/bus/i2c/devices/0-0068/accel"
cap "cat /sys/bus/i2c/devices/0-0068/accel_bytewise"
cap "i2cget -y -f 0 0x68 0x75"
cap "i2cdetect -y -a 0 0x4f 0x51"
cap "i2cget -y 2 0x48 0x00"
cap "insmod /at24.ko"
cap "printf ABCDEFGHIJ | dd of=/sys/bus/nvmem/devices/0-00501/nvmem bs=10 seek=1 2>/dev/null"
```

`cut`으로 CPU 번호·irq 플래그 열을 잘라 "태스크-PID 시각: 이벤트: 내용"만 남겼다. 시각은 실행마다 다르다. 먼저 **raw `i2c_transfer`**(accel) 대 **SMBus 바이트 6번**(accel_bytewise):

```text
/sys/kernel/tracing/events/i2c: enable filter i2c_read i2c_reply i2c_result i2c_write  /sys/kernel/tracing/events/smbus: enable filter smbus_read smbus_reply smbus_result smbus_write 
### cat /sys/bus/i2c/devices/0-0068/accel
cat-71 6.383762: i2c_write: i2c-0 #0 a=068 f=0000 l=1 [3b]
cat-71 6.384297: i2c_read: i2c-0 #1 a=068 f=0001 l=6
cat-71 6.384450: i2c_reply: i2c-0 #1 a=068 f=0001 l=6 [00-00-00-00-40-00]
cat-71 6.384519: i2c_result: i2c-0 n=2 ret=2
### cat /sys/bus/i2c/devices/0-0068/accel_bytewise
cat-75 6.530103: smbus_read: i2c-0 a=068 f=0000 c=3b BYTE_DATA
cat-75 6.530173: i2c_write: i2c-0 #0 a=068 f=0000 l=1 [3b]
cat-75 6.530179: i2c_read: i2c-0 #1 a=068 f=0001 l=1
cat-75 6.530189: i2c_reply: i2c-0 #1 a=068 f=0001 l=1 [00]
cat-75 6.530192: i2c_result: i2c-0 n=2 ret=2
cat-75 6.530252: smbus_reply: i2c-0 a=068 f=0000 c=3b BYTE_DATA l=1 [00]
cat-75 6.530364: smbus_result: i2c-0 a=068 f=0000 c=3b BYTE_DATA rd res=0
cat-75 6.530472: smbus_read: i2c-0 a=068 f=0000 c=3c BYTE_DATA
cat-75 6.530476: i2c_write: i2c-0 #0 a=068 f=0000 l=1 [3c]
cat-75 6.530478: i2c_read: i2c-0 #1 a=068 f=0001 l=1
cat-75 6.530482: i2c_reply: i2c-0 #1 a=068 f=0001 l=1 [01]
cat-75 6.530483: i2c_result: i2c-0 n=2 ret=2
cat-75 6.530485: smbus_reply: i2c-0 a=068 f=0000 c=3c BYTE_DATA l=1 [01]
cat-75 6.530487: smbus_result: i2c-0 a=068 f=0000 c=3c BYTE_DATA rd res=0
...  (c=3d, 3e, 3f 도 같은 7줄 패턴 — 생략)
cat-75 6.530594: smbus_read: i2c-0 a=068 f=0000 c=40 BYTE_DATA
cat-75 6.530596: i2c_write: i2c-0 #0 a=068 f=0000 l=1 [40]
cat-75 6.530597: i2c_read: i2c-0 #1 a=068 f=0001 l=1
cat-75 6.530600: i2c_reply: i2c-0 #1 a=068 f=0001 l=1 [00]
cat-75 6.530603: i2c_result: i2c-0 n=2 ret=2
cat-75 6.530605: smbus_reply: i2c-0 a=068 f=0000 c=40 BYTE_DATA l=1 [00]
cat-75 6.530606: smbus_result: i2c-0 a=068 f=0000 c=40 BYTE_DATA rd res=0
```

출력에서 볼 것:

- 읽는 법: `#0`/`#1`은 배열 안 메시지 인덱스, `a=068` 주소, `f=0001`은 `I2C_M_RD`, `l=` 길이, `[..]` 데이터. `i2c_result: n=2 ret=2`가 "msg 2개 요청, 2개 처리".
- raw 방식은 **트랜잭션 1개**(write 3b + read 6바이트). SMBus 방식은 같은 6바이트에 **트랜잭션 6개**다. 각 `smbus_read`가 core 안에서 `[W reg][R 1]` 두 메시지로 **에뮬레이션**된 것이 그대로 보인다(이 adapter에는 `.smbus_xfer`가 없으니까).
- 진짜 센서에서 바이트 단위 읽기는 **샘플 찢어짐(tearing)** 위험이 있다: 0x3B를 읽은 뒤 0x3C를 읽기 전에 센서가 새 샘플로 갱신하면 상위·하위 바이트가 서로 다른 샘플에서 온다. 그래서 IMU 데이터는 한 트랜잭션 burst로 읽는다(많은 센서가 burst 읽기 동안 출력 레지스터를 고정한다 — 칩마다 데이터시트 확인).

### 4.3 SMBus 에뮬레이션 — core가 SMBus를 `i2c_msg`로 바꾸는 표

SMBus(System Management Bus)는 I2C 위에 "명령 바이트 + 정해진 모양"만 허용하도록 좁힌 규격이다(PC 메인보드의 온도·팬·SPD). Linux의 `i2c_smbus_*()` helper는 adapter가 `.smbus_xfer`를 주면 그대로 넘기고, 없으면 `i2c_smbus_xfer_emulated()`(drivers/i2c/i2c-core-smbus.c)가 아래 표대로 메시지를 만든다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<text x="102" y="30" font-size="13" text-anchor="middle" font-weight="bold">SMBus 명령</text>
<text x="345" y="30" font-size="13" text-anchor="middle" font-weight="bold">버스 위 파형</text>
<text x="582" y="30" font-size="13" text-anchor="middle" font-weight="bold">에뮬레이션 i2c_msg[]</text>
<rect x="10" y="44" width="185" height="38" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="20" y="60" font-size="12">quick write</text>
<text x="20" y="76" font-size="12">(i2cdetect 기본)</text>
<rect x="205" y="44" width="280" height="38" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="215" y="68" font-size="12">S A+W P</text>
<rect x="495" y="44" width="175" height="38" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="505" y="68" font-size="12">[W len=0]</text>
<rect x="10" y="86" width="185" height="38" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="20" y="102" font-size="12">receive byte</text>
<text x="20" y="118" font-size="12">read_byte()</text>
<rect x="205" y="86" width="280" height="38" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="215" y="110" font-size="12">S A+R d P</text>
<rect x="495" y="86" width="175" height="38" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="505" y="110" font-size="12">[R 1]</text>
<rect x="10" y="128" width="185" height="38" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="144" font-size="12">read byte data</text>
<text x="20" y="160" font-size="12">read_byte_data()</text>
<rect x="205" y="128" width="280" height="38" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="215" y="152" font-size="12">S A+W c Sr A+R d P</text>
<rect x="495" y="128" width="175" height="38" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="505" y="152" font-size="12">[W c][R 1]</text>
<rect x="10" y="170" width="185" height="38" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="20" y="186" font-size="12">write byte data</text>
<text x="20" y="202" font-size="12">write_byte_data()</text>
<rect x="205" y="170" width="280" height="38" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="215" y="194" font-size="12">S A+W c d P</text>
<rect x="495" y="170" width="175" height="38" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="505" y="194" font-size="12">[W c,d]</text>
<rect x="10" y="212" width="185" height="38" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="228" font-size="12">read word data</text>
<text x="20" y="244" font-size="12">read_word_data()</text>
<rect x="205" y="212" width="280" height="38" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="215" y="236" font-size="12">S A+W c Sr A+R lo hi P</text>
<rect x="495" y="212" width="175" height="38" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="505" y="236" font-size="12">[W c][R 2]</text>
<rect x="10" y="254" width="185" height="38" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="20" y="270" font-size="12">block read (SMBus)</text>
<text x="20" y="286" font-size="12">read_block_data()</text>
<rect x="205" y="254" width="280" height="38" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="215" y="278" font-size="12">S A+W c Sr A+R N d1..dN P</text>
<rect x="495" y="254" width="175" height="38" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="505" y="278" font-size="12">[W c][R RECV_LEN]</text>
<rect x="10" y="296" width="185" height="38" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="20" y="312" font-size="12">I2C block (Linux 확장)</text>
<text x="20" y="328" font-size="12">read_i2c_block_data()</text>
<rect x="205" y="296" width="280" height="38" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="215" y="320" font-size="12">S A+W c Sr A+R d1..dL P   (L ≤ 32)</text>
<rect x="495" y="296" width="175" height="38" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="505" y="320" font-size="12">[W c][R L]</text>
</svg>
```
그림 4 — SMBus 명령과 I2C 메시지의 대응(A = 7비트 주소, c = command/레지스터 번호). 함수 이름은 `i2c_smbus_` 접두사를 생략했다. adapter에 `.smbus_xfer`가 없으면 core가 오른쪽 열의 메시지를 만들어 `.xfer`로 보낸다. 반대로 SMBus 전용 컨트롤러(`.xfer` 없음)에서는 `i2c_transfer()`가 `-EOPNOTSUPP`이다.

트레이스에서 확인: `i2cget -y -f 0 0x68 0x75`(BusyBox i2cget은 SMBus read byte data)도 정확히 같은 모양이다.

```text
### i2cget -y -f 0 0x68 0x75
i2cget-79 6.814640: smbus_read: i2c-0 a=068 f=0000 c=75 BYTE_DATA
i2cget-79 6.814660: i2c_write: i2c-0 #0 a=068 f=0000 l=1 [75]
i2cget-79 6.814665: i2c_read: i2c-0 #1 a=068 f=0001 l=1
i2cget-79 6.814674: i2c_reply: i2c-0 #1 a=068 f=0001 l=1 [68]
i2cget-79 6.814677: i2c_result: i2c-0 n=2 ret=2
i2cget-79 6.814683: smbus_reply: i2c-0 a=068 f=0000 c=75 BYTE_DATA l=1 [68]
i2cget-79 6.814688: smbus_result: i2c-0 a=068 f=0000 c=75 BYTE_DATA rd res=0
```

### 4.4 functionality 비트 — "이 버스로 무엇을 할 수 있나"

`I2C_FUNC_*` 비트는 adapter가 `.functionality()`로 알리고, client driver(`i2c_check_functionality()`)와 유저(`ioctl(fd, I2C_FUNCS, &f)`)가 읽는다. h5_fakebus가 돌려준 값은 6.1절 출력의 `FUNCS=0x0fff0019`다. 풀어 보면:

| 비트 | 값 | 이유 |
|---|---|---|
| `I2C_FUNC_I2C` | 0x00000001 | 임의 메시지 조합 가능 |
| `I2C_FUNC_SMBUS_PEC` | 0x00000008 | `I2C_FUNC_SMBUS_EMUL`에 포함 (PEC 바이트도 core가 계산) |
| `I2C_FUNC_NOSTART` | 0x00000010 | `I2C_M_NOSTART` 지원 |
| `I2C_FUNC_SMBUS_QUICK` … `WRITE_I2C_BLOCK` | 0x0fff0000 중 대부분 | `I2C_FUNC_SMBUS_EMUL` (raw I2C 위 에뮬레이션) |
| `I2C_FUNC_SMBUS_READ_BLOCK_DATA` | 0x01000000 | `I2C_M_RECV_LEN`을 직접 구현했기 때문에 추가 |
| `I2C_FUNC_10BIT_ADDR` | 0x00000002 | **없음** → 10비트 요청은 거절 (6.3절) |

block read를 EMUL에 넣지 않는 이유: 길이를 장치가 정하기 때문에 컨트롤러가 "첫 바이트를 받은 뒤 남은 길이를 바꿔 가며" 받을 수 있어야 하는데, 많은 하드웨어 컨트롤러가 이걸 못 한다. 그래서 할 수 있는 드라이버만 직접 비트를 켠다.

컨트롤러 하드웨어 제약(예: "읽기 최대 255바이트", "메시지 2개까지만, 첫 번째는 write")은 functionality가 아니라 `adap->quirks`(`struct i2c_adapter_quirks`: `max_num_msgs`, `max_read_len`, `I2C_AQ_COMB_WRITE_FIRST` …)로 알린다. core가 `__i2c_transfer()` 앞에서 검사하고 위반하면 `-EOPNOTSUPP`를 돌려준다.

### 4.5 `I2C_M_NOSTART`와 at24의 ack polling — 트레이스로 보기

at24를 로드하면 probe가 "1바이트 테스트 읽기"를 하고(at24.c: "Perform a one-byte test read to verify that the chip is functional"), nvmem 파일에 쓰면 page 단위로 쪼개 쓴다.

```text
### insmod /at24.ko
insmod-91 7.258930: i2c_write: i2c-0 #0 a=050 f=0000 l=1 [00]
insmod-91 7.258945: i2c_read: i2c-0 #1 a=050 f=0001 l=1
insmod-91 7.258999: i2c_reply: i2c-0 #1 a=050 f=0001 l=1 [48]
insmod-91 7.259004: i2c_result: i2c-0 n=2 ret=2
### printf ABCDEFGHIJ | dd of=/sys/bus/nvmem/devices/0-00501/nvmem bs=10 seek=1 2>/dev/null
dd-96 7.412728: i2c_write: i2c-0 #0 a=050 f=0000 l=1 [0a]
dd-96 7.412743: i2c_write: i2c-0 #1 a=050 f=4000 l=6 [41-42-43-44-45-46]
dd-96 7.412825: i2c_result: i2c-0 n=2 ret=2
dd-96 7.413075: i2c_write: i2c-0 #0 a=050 f=0000 l=1 [10]
dd-96 7.413078: i2c_write: i2c-0 #1 a=050 f=4000 l=4 [47-48-49-4a]
dd-96 7.413093: i2c_result: i2c-0 n=2 ret=-6
dd-96 7.415098: i2c_write: i2c-0 #0 a=050 f=0000 l=1 [10]
dd-96 7.415106: i2c_write: i2c-0 #1 a=050 f=4000 l=4 [47-48-49-4a]
dd-96 7.415116: i2c_result: i2c-0 n=2 ret=-6
dd-96 7.417380: i2c_write: i2c-0 #0 a=050 f=0000 l=1 [10]
dd-96 7.417386: i2c_write: i2c-0 #1 a=050 f=4000 l=4 [47-48-49-4a]
dd-96 7.417396: i2c_result: i2c-0 n=2 ret=-6
dd-96 7.419129: i2c_write: i2c-0 #0 a=050 f=0000 l=1 [10]
dd-96 7.419133: i2c_write: i2c-0 #1 a=050 f=4000 l=4 [47-48-49-4a]
dd-96 7.419148: i2c_result: i2c-0 n=2 ret=2
```

출력에서 볼 것:

- `f=4000` = `I2C_M_NOSTART`. at24는 regmap-i2c를 쓰고, regmap-i2c의 `regmap_i2c_gather_write()`는 adapter가 `I2C_FUNC_NOSTART`를 지원하면 "레지스터 주소 버퍼"와 "데이터 버퍼"를 **복사 없이** 두 메시지로 보내되 두 번째에 NOSTART를 붙인다. 버스 위에서는 `S 50+W 0a 41 42 … 46 P` 한 덩어리다. (지원하지 않으면 두 버퍼를 합쳐 메시지 하나로 보낸다.)
- **page 경계 분할**: 오프셋 0x0a부터 10바이트를 썼는데, at24가 `pagesize=8`에 맞춰 0x0a~0x0f(6바이트)와 0x10~0x13(4바이트)으로 나눴다(`at24_adjust_write_count()`: "Never roll over backwards, to the start of this page").
- **ack polling**: 첫 page를 쓴 직후 EEPROM이 내부 쓰기(tWR 5 ms) 중이라 주소에 NACK → `ret=-6`(`-ENXIO`). at24는 `usleep_range(1000, 1500)` 후 재시도를 `write_timeout`(기본 25 ms)까지 반복한다. 시각을 보면 7.4128 → 7.4191로 약 6.3 ms 뒤에 성공했다. 진짜 EEPROM 데이터시트의 "acknowledge polling" 그대로다.
- probe의 테스트 읽기 `[48]`은 EEPROM 0번지의 'H'다.

---

## 5. 잠금 — adapter bus lock, `i2c_lock_bus`, mux

### 5.1 버스 하나 = 한 번에 트랜잭션 하나

bare-metal RTOS에서는 I2C 버스마다 mutex를 하나 두고 "트랜잭션 시작 전에 잡고 STOP 후에 놓는다"를 직접 짰을 것이다. Linux에서는 core가 대신 한다. `i2c_transfer()`는 내부에서 `__i2c_lock_bus_helper()`로 adapter의 `bus_lock`(`struct rt_mutex`)을 잡고 `__i2c_transfer()`를 부른 뒤 놓는다. 그래서:

- adapter 드라이버의 `.xfer`는 **같은 adapter에 대해 동시에 두 번 불리지 않는다.** h5_fakebus가 자체 mutex 없이 `h5_bus` 상태를 만질 수 있는 이유다(다른 경로 — debugfs 쓰기 — 와의 경쟁은 이 실습에서 무시했다).
- client driver도 보통 잠금을 신경 쓰지 않는다. **트랜잭션 하나 안**(배열 하나)은 원자적이다.
- rt_mutex인 이유: 우선순위 상속. 낮은 우선순위 스레드가 버스를 잡고 있을 때 RT 스레드가 기다리면 그 스레드의 우선순위를 올려 준다(N 모듈).

**여러 트랜잭션을 묶어야 할 때**(예: "설정 레지스터 쓰기 → 다른 드라이버가 끼어들면 안 되는 상태에서 → 데이터 읽기", 또는 칩이 STOP을 꼭 요구해서 배열 하나로 못 묶을 때) `i2c_lock_bus()` + `__i2c_transfer()`(밑줄 두 개 = 이미 잠금을 가진 버전) + `i2c_unlock_bus()`를 쓴다. 3장의 `burst_show()`가 그 예이고, 실행 결과 `who=0x68 pwr=0x00 (ret 2 2)`를 3.2절에서 봤다. 잠금을 잡은 채 `i2c_transfer()`(밑줄 없는 버전)를 부르면 **자기 자신과 데드락**이다 — rt_mutex는 재귀 잠금이 안 된다.

`I2C_LOCK_SEGMENT` vs `I2C_LOCK_ROOT_ADAPTER`: mux 뒤 버스에서 의미가 갈린다. SEGMENT는 "이 가지만", ROOT_ADAPTER는 "맨 위 물리 버스 전체"를 잠근다. mux가 없는 일반 버스에서는 둘이 같다.

### 5.2 예제 6 — PCA9548 mux: 같은 주소 두 개를 한 버스에

로봇 센서 키트에서 **같은 IMU 두 개**(주소 고정 0x68)나 같은 온도센서 여러 개를 붙일 때 I2C switch/mux(PCA9548 등)를 쓴다. Linux는 mux의 각 채널을 **독립 adapter**(`i2c-1`~`i2c-8`)로 보여 준다. client driver는 자기가 mux 뒤에 있는지조차 모른다.

```sh
i2cget -y 1 0x48 0x00; i2cget -y 2 0x48 0x00
/u3_detect 1 | sed -n '1p;5,6p'
i2cget -y -f 0 0x70
```

```text
### mux: same address 0x48 behind channel 0 and 1
0x19
0x1e
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: -- -- -- -- -- -- -- -- 48 -- -- -- -- -- -- --
### mux control reg seen from root bus
0x01
```

출력에서 볼 것: 같은 주소 0x48인데 `i2c-1`(채널 0)에서는 25(0x19), `i2c-2`(채널 1)에서는 30(0x1e)이 읽혔다. 마지막으로 채널 0을 쓴 `u3_detect 1`의 흔적이 mux 레지스터 `0x01`로 남아 있다(pca954x 기본 idle 동작은 "선택한 채널을 그대로 둔다" — sysfs `idle_state`로 바꿀 수 있다).

트레이스는 **중첩**을 보여 준다(4.2의 t3.sh):

```text
### i2cget -y 2 0x48 0x00
i2cget-87 7.104257: smbus_read: i2c-2 a=048 f=0000 c=0 BYTE_DATA
i2cget-87 7.104275: i2c_write: i2c-2 #0 a=048 f=0000 l=1 [00]
i2cget-87 7.104280: i2c_read: i2c-2 #1 a=048 f=0001 l=1
i2cget-87 7.104347: smbus_write: i2c-0 a=070 f=0000 c=2 BYTE l=0 []
i2cget-87 7.104353: i2c_write: i2c-0 #0 a=070 f=0000 l=1 [02]
i2cget-87 7.104358: i2c_result: i2c-0 n=1 ret=1
i2cget-87 7.104366: smbus_result: i2c-0 a=070 f=0000 c=2 BYTE wr res=0
i2cget-87 7.104398: i2c_write: i2c-0 #0 a=048 f=0000 l=1 [00]
i2cget-87 7.104400: i2c_read: i2c-0 #1 a=048 f=0001 l=1
i2cget-87 7.104444: i2c_reply: i2c-0 #1 a=048 f=0001 l=1 [1e]
i2cget-87 7.104447: i2c_result: i2c-0 n=2 ret=2
i2cget-87 7.104499: i2c_reply: i2c-2 #1 a=048 f=0001 l=1 [1e]
i2cget-87 7.104500: i2c_result: i2c-2 n=2 ret=2
i2cget-87 7.104503: smbus_reply: i2c-2 a=048 f=0000 c=0 BYTE_DATA l=1 [1e]
i2cget-87 7.104507: smbus_result: i2c-2 a=048 f=0000 c=0 BYTE_DATA rd res=0
```

출력에서 볼 것: 유저는 `i2c-2`에 요청했다 → mux core가 부모 `i2c-0`에서 먼저 **채널 선택**(0x70에 `0x02` = 채널 1 비트, SMBus send byte)을 하고 → 같은 메시지를 부모 버스로 보낸다 → 결과가 `i2c-2`로 돌아온다. pca954x는 `i2c_mux_alloc(..., 0, ...)`로 **parent-locked** mux라서, 선택 + 전송 동안 부모 adapter 전체를 잠근다(그래서 `pca954x_reg_write()`는 잠금 없는 `__i2c_smbus_xfer()`를 쓴다). 반대로 `I2C_MUX_LOCKED`(mux-locked)는 mux 자체가 별도 잠금을 쓰는 방식으로, mux를 GPIO 확장기처럼 다른 버스로 제어할 때 쓴다.

주의: mux 뒤 버스에 부모와 **같은 주소**의 client를 만들려 하면 core가 막는다(`i2c_check_mux_parents()` → `-EBUSY`). 채널이 열리면 부모에서도 그 주소가 보이기 때문이다. `u3_detect 1`의 출력에서 0x70 줄은 잘렸지만, 부모의 client 0x70(pca9548)이 자식 버스에서 `UU`로 보이는 이유도 이 검사다.

---

## 6. i2c-dev — 유저스페이스에서 직접 버스 쓰기

### 6.1 예제 7 — `I2C_RDWR`, `I2C_SMBUS`, `I2C_SLAVE` vs `I2C_SLAVE_FORCE`

A3에서 `/dev/i2c-N` ioctl의 기본을 봤다. 여기서는 functionality 확인, 드라이버가 잡은 주소에 대한 `EBUSY`, SMBus word/block을 더 본다.

```c
/* u1_rdwr: /dev/i2c-0 로 IMU 읽기 — I2C_RDWR combined, I2C_SMBUS, SLAVE vs SLAVE_FORCE */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int rdwr(int fd, __u16 addr, __u8 reg, __u8 *rx, __u16 n)
{
	struct i2c_msg m[2] = {
		{ .addr = addr, .flags = 0,        .len = 1, .buf = &reg },
		{ .addr = addr, .flags = I2C_M_RD, .len = n, .buf = rx },
	};
	struct i2c_rdwr_ioctl_data x = { .msgs = m, .nmsgs = 2 };
	return ioctl(fd, I2C_RDWR, &x);          /* 성공 시 처리한 msg 개수 */
}

static int smbus(int fd, char rw, __u8 cmd, int size, union i2c_smbus_data *d)
{
	struct i2c_smbus_ioctl_data a = { .read_write = rw, .command = cmd,
					  .size = (__u32)size, .data = d };
	return ioctl(fd, I2C_SMBUS, &a);
}

int main(void)
{
	int fd = open("/dev/i2c-0", O_RDWR), r;
	unsigned long f;
	__u8 b[6];
	union i2c_smbus_data d;

	if (fd < 0 || ioctl(fd, I2C_FUNCS, &f) < 0) { perror("i2c-0"); return 1; }
	printf("FUNCS=0x%08lx I2C=%d NOSTART=%d 10BIT=%d SMBUS_QUICK=%d BLOCK_READ=%d\n", f,
	       !!(f & I2C_FUNC_I2C), !!(f & I2C_FUNC_NOSTART), !!(f & I2C_FUNC_10BIT_ADDR),
	       !!(f & I2C_FUNC_SMBUS_QUICK), !!(f & I2C_FUNC_SMBUS_READ_BLOCK_DATA));
	r = rdwr(fd, 0x68, 0x75, b, 1);
	printf("I2C_RDWR  [W 75][R 1]  ret=%d WHO_AM_I=0x%02x\n", r, b[0]);
	r = rdwr(fd, 0x68, 0x3B, b, 6);
	printf("I2C_RDWR  [W 3B][R 6]  ret=%d accel=%d,%d,%d\n", r, (short)(b[0] << 8 | b[1]),
	       (short)(b[2] << 8 | b[3]), (short)(b[4] << 8 | b[5]));
	if (ioctl(fd, I2C_SLAVE, 0x68) < 0)      /* h5_imu 가 잡고 있으면 EBUSY */
		printf("I2C_SLAVE 0x68 -> %s\n", strerrorname_np(errno));
	if (ioctl(fd, I2C_SLAVE_FORCE, 0x68) < 0) { perror("FORCE"); return 1; }
	r = smbus(fd, I2C_SMBUS_READ, 0x75, I2C_SMBUS_BYTE_DATA, &d);
	printf("SMBUS read_byte_data(0x75) ret=%d -> 0x%02x\n", r, d.byte);
	r = smbus(fd, I2C_SMBUS_READ, 0x6B, I2C_SMBUS_WORD_DATA, &d);
	printf("SMBUS read_word_data(0x6B) ret=%d -> 0x%04x (LSB first)\n", r, d.word);
	r = smbus(fd, I2C_SMBUS_READ, 0x3B, I2C_SMBUS_BLOCK_DATA, &d);
	printf("SMBUS block_data(0x3B)     ret=%d -> len=%d first=%02x %02x\n", r,
	       d.block[0], d.block[1], d.block[2]);
	close(fd);
	return 0;
}
```

VM에서 `gcc -std=gnu11 -Wall -Wextra -O2 -static`으로 빌드했다(4개 프로그램 모두 경고 0개). `t2.sh`는 h5_fakebus, h5_imu, at24를 로드한 뒤 실행한다.

```text
### u1_rdwr
FUNCS=0x0fff0019 I2C=1 NOSTART=1 10BIT=0 SMBUS_QUICK=1 BLOCK_READ=1
I2C_RDWR  [W 75][R 1]  ret=2 WHO_AM_I=0x68
I2C_RDWR  [W 3B][R 6]  ret=2 accel=0,0,16384
I2C_SLAVE 0x68 -> EBUSY
SMBUS read_byte_data(0x75) ret=0 -> 0x68
SMBUS read_word_data(0x6B) ret=0 -> 0x0000 (LSB first)
SMBUS block_data(0x3B)     ret=0 -> len=6 first=00 01
```

출력에서 볼 것:

- `I2C_RDWR`은 주소를 **메시지마다** 지정하므로 `I2C_SLAVE`가 필요 없고, 드라이버가 잡은 주소에도 그냥 나간다(검사하지 않는다 — 위험하다는 뜻).
- `I2C_SMBUS`·`read()`/`write()`는 fd에 묶인 주소를 쓰는데, `I2C_SLAVE`로 정할 때 **커널 드라이버가 이미 그 주소를 쓰고 있으면 `EBUSY`**(i2c-dev.c `i2cdev_check()`: `dev->driver ? -EBUSY : 0`). `I2C_SLAVE_FORCE`는 이 안전장치를 끈다. i2c-tools의 `-f` 옵션이 이것이다.
- block read(`I2C_SMBUS_BLOCK_DATA`)는 장치가 첫 바이트로 준 길이(6)만큼 받았다. h5_fakebus가 `I2C_M_RECV_LEN`을 구현했기에 가능하다.
- `I2C_RDWR`은 한 번에 메시지 최대 `I2C_RDWR_IOCTL_MAX_MSGS`(42)개, 메시지당 8192바이트까지(i2c-dev.c 확인).

### 6.2 i2c-dev를 쓰면 안 되는 이유 — 커널 드라이버가 나은 점

| 관점 | i2c-dev(유저 툴) | 커널 client driver |
|---|---|---|
| 인터럽트(DRDY 핀) | 받을 수 없음 → polling | `request_threaded_irq()` (F) |
| 동시 접근 | 같은 칩에 커널 드라이버가 있으면 서로 상태를 망침 (`-f`가 필요한 이유) | 드라이버가 유일한 주인 |
| 전원·클럭·reset 시퀀스 | 유저가 따로 GPIO·regulator를 만져야 함 | probe에서 H3·H4 API로 |
| suspend/resume, runtime PM | 없음 | `dev_pm_ops` |
| 상위 인터페이스 | 프로그램마다 제각각 | IIO·input·hwmon·nvmem 표준 ABI → 다른 앱이 그대로 사용 |
| 권한 | `/dev/i2c-N` 쓰기 권한 = 그 버스 전체(PMIC 포함!) 권한 | 장치별 파일만 노출 |
| 언제 쓰나 | bring-up 초기 "칩이 살아 있나", 공장 테스트, 드라이버 없는 단순 칩 | 제품 |

특히 마지막 "권한" 항목: 같은 버스에 PMIC가 있는 보드에서 `/dev/i2c-N`을 일반 앱에 열어 주면, 앱 버그 하나로 전압 레일을 끌 수 있다. 제품 이미지에서는 i2c-dev를 빼거나 udev로 root 전용으로 둔다.

### 6.3 예제 8 — 에러 errno 모음, `I2C_M_NOSTART`, EEPROM page wrap

```c
/* u2_errors: NACK 종류별 errno, I2C_M_NOSTART 로 EEPROM page write, 10-bit 요청 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int fd;
static void xfer(const char *what, struct i2c_msg *m, int n)
{
	struct i2c_rdwr_ioctl_data x = { .msgs = m, .nmsgs = (__u32)n };
	int r = ioctl(fd, I2C_RDWR, &x);
	printf("%-40s ret=%d %s\n", what, r, r < 0 ? strerrorname_np(errno) : "");
}

int main(void)
{
	__u8 reg = 0x75, rx[16], bad[2] = { 0x90, 0x01 }, wa = 0x20;
	__u8 data[11] = "0123456789";
	if ((fd = open("/dev/i2c-0", O_RDWR)) < 0) { perror("open"); return 1; }

	struct i2c_msg nack[2] = { { 0x51, 0, 1, &reg }, { 0x51, I2C_M_RD, 1, rx } };
	xfer("addr 0x51 (nobody) [W][R]", nack, 2);
	struct i2c_msg dnack[1] = { { 0x68, 0, 2, bad } };
	xfer("IMU write reg 0x90 (data NACK)", dnack, 1);
	struct i2c_msg ten[1] = { { 0x150, I2C_M_TEN, 1, &reg } };
	xfer("10-bit addr 0x150", ten, 1);

	/* EEPROM: word address 와 data 를 서로 다른 버퍼에서, START 없이 이어 붙인다 */
	struct i2c_msg pw[2] = { { 0x50, 0, 1, &wa }, { 0x50, I2C_M_NOSTART, 10, data } };
	xfer("EEPROM [W 20][NOSTART 10 bytes]", pw, 2);
	struct i2c_msg rd[2] = { { 0x50, 0, 1, &wa }, { 0x50, I2C_M_RD, 8, rx } };
	xfer("EEPROM read right after write", rd, 2);   /* tWR 중 → NACK */
	usleep(6000);
	xfer("EEPROM read after 6 ms", rd, 2);
	printf("0x20..0x27 = %.8s   (page wrap: '89' landed at 0x20)\n", (char *)rx);
	close(fd);
	return 0;
}
```

```text
### u2_errors
addr 0x51 (nobody) [W][R]                ret=-1 ENXIO
IMU write reg 0x90 (data NACK)           ret=-1 EREMOTEIO
10-bit addr 0x150                        ret=-1 EAFNOSUPPORT
EEPROM [W 20][NOSTART 10 bytes]          ret=2 
EEPROM read right after write            ret=-1 ENXIO
EEPROM read after 6 ms                   ret=2 
0x20..0x27 = 89234567   (page wrap: '89' landed at 0x20)
```

출력에서 볼 것:

- **주소 NACK = `ENXIO`**, **데이터 NACK = `EREMOTEIO`** — 이 adapter의 규칙이다. 커널 문서 `Documentation/i2c/fault-codes.rst`는 "주소 phase에 ACK가 없으면 `ENXIO`"를 권장하지만, 실제 컨트롤러 드라이버는 제각각이다: 6.18 `drivers/i2c/busses/`에서 `EREMOTEIO`를 쓰는 파일이 16개(designware, tegra, omap, bcm2835 …), `ENXIO`를 쓰는 파일이 76개였다(grep 집계). 일부는 주소 NACK도 `EREMOTEIO`로, 일부는 둘 다 `EIO`로 올린다. **client driver는 "NACK 종류"를 errno로 구분하려 하지 말고 음수면 실패로 다뤄야** 이식성이 있다.
- 10비트 주소는 functionality에 없으므로 adapter가 `EAFNOSUPPORT`로 거절했다(fault-codes.rst의 권장 코드). i2c-dev의 `I2C_TENBIT` ioctl을 썼다면 그 전에 functionality 검사로 막혔을 것이다.
- `I2C_M_NOSTART`로 이어 붙인 10바이트 쓰기가 성공했지만, **0x20 page(8바이트)를 넘은 '8', '9'가 page 앞(0x20, 0x21)을 덮었다** → `89234567`. 24c02 계열의 고전 함정이다. at24가 page 경계에서 쪼개 쓰는 이유(4.5)가 바로 이것이다.
- 쓰기 직후 읽기는 tWR 동안 `ENXIO`, 6 ms 뒤에는 성공. 유저 툴로 EEPROM을 쓸 때도 ack polling이 필요하다.

### 6.4 예제 9 — mini-i2cdetect와 "찔러 보기"의 위험

i2cdetect는 주소 0x03~0x77을 하나씩 "찔러서" ACK가 오면 장치가 있다고 본다. 문제는 **찌르는 행위 자체가 버스 트랜잭션**이라는 것이다. i2c-tools의 기본(auto) 규칙을 그대로 구현해 보자.

```c
/* u3_detect: mini-i2cdetect — i2c-tools 와 같은 규칙으로 0x03..0x77 을 찔러 본다 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int probe(int fd, int use_read)
{
	union i2c_smbus_data d;
	struct i2c_smbus_ioctl_data a = { .data = &d };
	if (use_read) {              /* SMBus "receive byte": START addr+R data STOP */
		a.read_write = I2C_SMBUS_READ; a.size = I2C_SMBUS_BYTE;
	} else {                     /* SMBus "quick write": START addr+W STOP (데이터 0바이트) */
		a.read_write = I2C_SMBUS_WRITE; a.size = I2C_SMBUS_QUICK;
	}
	return ioctl(fd, I2C_SMBUS, &a);
}

int main(int argc, char **argv)
{
	char dev[32];
	snprintf(dev, sizeof(dev), "/dev/i2c-%s", argc > 1 ? argv[1] : "0");
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
	for (int row = 0; row < 0x80; row += 16) {
		printf("%02x:", row);
		for (int a = row; a < row + 16; a++) {
			if (a < 0x03 || a > 0x77) { printf("   "); continue; }
			if (ioctl(fd, I2C_SLAVE, a) < 0) {      /* 커널 드라이버가 쓰는 중 */
				printf(errno == EBUSY ? " UU" : " ??");
				continue;
			}
			/* i2c-tools 기본(auto) 모드: EEPROM 대역과 0x30-0x37 은 read, 나머지는 quick write */
			int rd = (a >= 0x30 && a <= 0x37) || (a >= 0x50 && a <= 0x5f);
			printf(probe(fd, rd) < 0 ? " --" : " %02x", a);
		}
		printf("\n");
	}
	close(fd);
	return 0;
}
```

```text
### u3_detect 0
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: UU -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- UU -- -- -- -- -- -- --
70: UU -- -- -- -- -- -- --                        
### busybox i2cdetect -y 0
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: UU -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- UU -- -- -- -- -- -- -- 
70: UU -- -- -- -- -- -- --                         
```

출력에서 볼 것: 우리 구현과 BusyBox i2cdetect의 결과가 같다. 세 주소 모두 드라이버(at24, h5_imu, pca954x)가 붙어 있어서 **찔러 보지도 않고** `UU`로 표시된다(`I2C_SLAVE`가 `EBUSY`). 0x48 온도센서는 mux 채널이 닫혀 있어 부모 버스에서 안 보인다. (A10에서 본 i2c-stub 결과와 같은 형식이다.)

드라이버가 **없는** 주소를 찌를 때 실제로 무엇이 나가는지가 핵심이다. at24를 로드하기 전에 0x4f~0x51만 스캔한 트레이스:

```text
### i2cdetect -y -a 0 0x4f 0x51
i2cdetect-83 6.964064: smbus_write: i2c-0 a=04f f=0000 c=0 QUICK l=0 []
i2cdetect-83 6.964176: i2c_write: i2c-0 #0 a=04f f=0000 l=0 []
i2cdetect-83 6.964203: i2c_result: i2c-0 n=1 ret=-6
i2cdetect-83 6.964219: smbus_result: i2c-0 a=04f f=0000 c=0 QUICK wr res=-6
i2cdetect-83 6.964633: smbus_read: i2c-0 a=050 f=0000 c=0 BYTE
i2cdetect-83 6.964648: i2c_read: i2c-0 #0 a=050 f=0001 l=1
i2cdetect-83 6.964743: i2c_reply: i2c-0 #0 a=050 f=0001 l=1 [48]
i2cdetect-83 6.964747: i2c_result: i2c-0 n=1 ret=1
i2cdetect-83 6.964755: smbus_reply: i2c-0 a=050 f=0000 c=0 BYTE l=1 [48]
i2cdetect-83 6.964761: smbus_result: i2c-0 a=050 f=0000 c=0 BYTE rd res=0
i2cdetect-83 6.965162: smbus_read: i2c-0 a=051 f=0000 c=0 BYTE
i2cdetect-83 6.965169: i2c_read: i2c-0 #0 a=051 f=0001 l=1
i2cdetect-83 6.965172: i2c_result: i2c-0 n=1 ret=-6
i2cdetect-83 6.965174: smbus_result: i2c-0 a=051 f=0000 c=0 BYTE rd res=-6
```

출력에서 볼 것:

- 0x4f는 **quick write**(`l=0` write — `S 4F+W P`), 0x50·0x51은 EEPROM 대역이라 **receive byte**(1바이트 read)로 찔렀다. 빈 주소는 `ret=-6`(`-ENXIO`).
- 0x50에서 실제로 **1바이트를 읽었다**(`[48]` = 'H'). EEPROM의 "current address read"라 **내부 주소 포인터가 1 증가**했다. 무해해 보이지만 "읽으면 지워지는" 상태 레지스터(인터럽트 status, FIFO)를 가진 칩이라면 i2cdetect 한 번으로 이벤트를 잃는다.
- 반대로 quick write는 데이터가 없지만 **R/W 비트 = 0인 주소 바이트** 자체를 명령으로 해석하는 칩이 있다. i2cdetect 매뉴얼은 `-q`(quick write)가 "Atmel AT24RF08 EEPROM을 망가뜨리는 것으로 알려져 있다", `-r`(read byte)가 "쓰기 전용 칩(특히 0x69의 클럭 칩)에서 SMBus를 잠그는 것으로 알려져 있다"고 경고한다. auto 모드가 대역별로 방식을 섞는 이유다(기억 기반 인용 — 정확한 문구는 `man i2cdetect`로 확인).
- 그래서 실무 규칙: **양산 보드에서 i2cdetect는 마지막 수단**이고, 쓰더라도 범위를 좁혀(`first last` 인자) 필요한 주소만, 드라이버가 붙은 버스에서는 결과를 믿지 말고(`UU`) DT·sysfs를 먼저 본다.

---

## 7. 에러 코드와 버스 복구

### 7.1 errno 지도

| errno | 값 | 누가 언제 | 이 노트에서 재현 |
|---|---|---|---|
| `ENXIO` | -6 | adapter: 주소 phase NACK (장치 없음, 전원 꺼짐, 주소 핀 잘못, EEPROM 쓰기 중) / probe: "내 장치 아님" | 6.3, 6.4, 4.5 |
| `EREMOTEIO` | -121 | adapter: 데이터 phase NACK (드라이버에 따라 주소 NACK에도) | 6.3 |
| `EAGAIN` | -11 | adapter: arbitration lost (multi-master). core가 `adap->retries`번 재시도. atomic 문맥에서 버스가 이미 사용 중일 때도 | 7.2 |
| `ETIMEDOUT` | -110 | adapter: 완료가 안 옴 — clock stretching 과다, 버스 고착 | 7.3 |
| `EBUSY` | -16 | SMBus adapter: 버스가 너무 오래 busy / recovery 실패 / i2c-dev: 주소를 드라이버가 사용 중 | 7.3, 6.1 |
| `EOPNOTSUPP` | -95 | 지원하지 않는 전송 (functionality에 없음, quirks 위반) | 4.4 |
| `EAFNOSUPPORT` | -97 | 10비트 주소 미지원 | 6.3 |
| `EPROTO` | -71 | SMBus block 길이가 1~32 밖 등 규격 위반 | — |
| `EBADMSG` | -74 | SMBus PEC(CRC) 불일치 | — |
| `ESHUTDOWN` | -108 | suspend된 adapter로 전송 | — |

출처: `Documentation/i2c/fault-codes.rst`(6.18). `EREMOTEIO`는 이 문서 목록에 없지만 많은 컨트롤러 드라이버가 쓴다(6.3절 집계).

### 7.2 예제 10 — arbitration lost와 자동 재시도

`__i2c_transfer()`의 재시도 루프(6.18 소스 발췌):

```c
	/* Retry automatically on arbitration loss */
	orig_jiffies = jiffies;
	for (ret = 0, try = 0; try <= adap->retries; try++) {
		if (i2c_in_atomic_xfer_mode() && adap->algo->master_xfer_atomic)
			ret = adap->algo->master_xfer_atomic(adap, msgs, num);
		else
			ret = adap->algo->master_xfer(adap, msgs, num);

		if (ret != -EAGAIN)
			break;
		if (time_after(jiffies, orig_jiffies + adap->timeout))
			break;
	}
```

**`-EAGAIN`만** 재시도한다. NACK(`-ENXIO`)이나 timeout은 core가 재시도하지 않는다 — 그건 client driver가 판단할 일이다(at24의 ack polling처럼). `t4.sh`:

```sh
insmod /h5_fakebus.ko; dmesg -n 1
D=/sys/kernel/debug/h5_fakebus; T=/sys/kernel/tracing; mount -t tracefs none $T
echo 1 > $T/events/i2c/i2c_result/enable
st() { cat $D/stats; echo 0 > $D/stats; }
echo 0 > $D/stats
echo "### arbitration lost x2 (retries=3)"; echo 2 > $D/inject_eagain
i2cget -y 0 0x68 0x75; st
echo "### arbitration lost x10"; echo 10 > $D/inject_eagain
i2cget -y 0 0x68 0x75; echo "rc=$?"; st; echo 0 > $D/inject_eagain
```

```text
### arbitration lost x2 (retries=3)
0x68
xfers=3 msgs=2 bytes=2 nacks=0 bus_bits=39 bus_us=97
### arbitration lost x10
i2cget: read failed: Resource temporarily unavailable
rc=1
xfers=4 msgs=0 bytes=0 nacks=0 bus_bits=0 bus_us=0
```

출력에서 볼 것: 2번 지면 `.xfer`가 3번 불리고(xfers=3) 세 번째에 성공한다 — 호출자는 실패를 모른다. 10번 지면 `retries=3`이라 **총 4번**(1 + 3) 시도 후 `-EAGAIN`이 유저까지 올라온다("Resource temporarily unavailable"). `adap->retries`는 adapter 드라이버가 정하고, i2c-dev의 `I2C_RETRIES` ioctl로 바꿀 수도 있다.

### 7.3 예제 11 — 버스 고착과 `i2c_recover_bus()`: 9 클럭

Don이 scope로 여러 번 본 장면이다: target이 바이트를 내보내던 중 master가 리셋(또는 글리치)되어, target은 여전히 "다음 클럭에 다음 비트를 내보내려고" **SDA를 LOW로 잡고** 있다. master는 SDA가 LOW라 START를 못 건다. 해법은 SCL을 최대 9번 토글해서 target이 남은 비트를 다 밀어내고 NACK을 보게 한 뒤 STOP을 만드는 것.

Linux에서는 이 알고리즘이 core에 있다: `i2c_generic_scl_recovery()`(drivers/i2c/i2c-core-base.c, 6.18 발췌·요약):

```c
#define RECOVERY_NDELAY		5000
#define RECOVERY_CLK_CNT	9

int i2c_generic_scl_recovery(struct i2c_adapter *adap)
{
	/* prepare_recovery() / pinctrl "gpio" state 로 핀을 GPIO 로 전환 */
	bri->set_scl(adap, scl);              /* SCL HIGH 에서 시작 */
	/* ... set_sda 가 있으면 SDA 가 반 클럭 늦게 SCL 을 따라가게 해서 매번 STOP 을 만든다 */
	while (i++ < RECOVERY_CLK_CNT * 2) {
		if (scl && !bri->get_scl(adap)) {   /* 내가 HIGH 로 놓았는데 LOW? */
			dev_err(&adap->dev, "SCL is stuck low, exit recovery\n");
			ret = -EBUSY; break;
		}
		scl = !scl;
		bri->set_scl(adap, scl);          /* 5 us / 2.5 us 지연 → 약 100 kHz */
		if (scl) {
			ret = i2c_generic_bus_free(adap);  /* get_bus_free() 또는 get_sda() */
			if (ret == 0) break;               /* SDA 풀렸다 → 끝 */
		}
	}
	/* unprepare_recovery() / pinctrl "default" 로 복귀 */
	return ret;
}
```

adapter 드라이버가 할 일은 `struct i2c_bus_recovery_info`를 채우는 것뿐이다. 실제 SoC에서는 보통 두 방법 중 하나다.

1. **GPIO 방식**: DT에 `scl-gpios`, `sda-gpios`와 pinctrl state `"gpio"`(또는 `"recovery"`)를 두면, core의 `i2c_gpio_init_recovery()`가 GPIO를 얻어 `get_scl`/`set_scl`/`get_sda`를 자동으로 채운다(`scl`은 `GPIOD_OUT_HIGH_OPEN_DRAIN`으로 요청). 드라이버는 `adap->bus_recovery_info = &bri;`만 하면 된다.
2. **컨트롤러 내장 방식**: 일부 IP(예: designware 일부 버전, i.MX의 LPI2C 등)는 "SCL 9펄스 생성" 레지스터 비트가 있어 `recover_bus`에 자기 함수를 넣는다.

h5_fakebus는 1번 구조를 흉내 냈다: `set_scl`의 rising edge마다 "target이 비트 하나를 내보냄"(`stuck_sda_clocks--`), 0이 되면 `get_sda`가 1. recovery는 **adapter 드라이버가 부른다** — core가 자동으로 부르지 않는다. 보통 `.xfer`에서 "bus busy timeout" 또는 "arbitration lost인데 다른 master가 없음"을 감지했을 때 `i2c_recover_bus(adap)`를 부르고 재시도한다. 계속(`t4.sh`):

```sh
echo "### SDA stuck, target needs 5 clocks"; echo 5 > $D/stuck_sda_clocks
i2cget -y 0 0x68 0x75; dmesg | tail -4 | cut -c16-
echo "### SDA stuck, target needs 12 clocks"; echo 12 > $D/stuck_sda_clocks
i2cget -y 0 0x68 0x75; echo "rc=$?"; dmesg | tail -4 | cut -c16-; echo "left=$(cat $D/stuck_sda_clocks)"
echo "### next transfer"; i2cget -y 0 0x68 0x75; dmesg | tail -4 | cut -c16-
echo "### SCL stuck low (clock stretching forever)"; echo 1 > $D/scl_stuck
i2cget -y 0 0x68 0x75; echo "rc=$?"; dmesg | tail -4 | cut -c16-; echo 0 > $D/scl_stuck
echo "### i2c_result events"; grep -v "^#" $T/trace | sed 's/^ *//; s/  */ /g' | cut -d' ' -f1,4-
```

```text
### SDA stuck, target needs 5 clocks
0x68
i2c i2c-0: bus busy (SDA=0 SCL=1) -> i2c_recover_bus()
i2c i2c-0: recovery: pins -> GPIO mode, SDA=0
i2c i2c-0: recovery: pins -> I2C mode, SDA=1
i2c i2c-0: recovery ok after 5 SCL pulses (ret=0)
### SDA stuck, target needs 12 clocks
i2cget: read failed: Connection timed out
rc=1
i2c i2c-0: bus busy (SDA=0 SCL=1) -> i2c_recover_bus()
i2c i2c-0: recovery: pins -> GPIO mode, SDA=0
i2c i2c-0: recovery: pins -> I2C mode, SDA=0
i2c i2c-0: recovery FAILED after 9 SCL pulses (ret=-16)
left=3
### next transfer
0x68
i2c i2c-0: bus busy (SDA=0 SCL=1) -> i2c_recover_bus()
i2c i2c-0: recovery: pins -> GPIO mode, SDA=0
i2c i2c-0: recovery: pins -> I2C mode, SDA=1
i2c i2c-0: recovery ok after 3 SCL pulses (ret=0)
### SCL stuck low (clock stretching forever)
i2cget: read failed: Connection timed out
rc=1
i2c i2c-0: recovery: pins -> GPIO mode, SDA=1
i2c i2c-0: SCL is stuck low, exit recovery
i2c i2c-0: recovery: pins -> I2C mode, SDA=1
i2c i2c-0: recovery FAILED after 0 SCL pulses (ret=-16)
### i2c_result events
i2cget-69 6.141149: i2c_result: i2c-0 n=2 ret=2
i2cget-71 6.201468: i2c_result: i2c-0 n=2 ret=-11
i2cget-73 6.331790: i2c_result: i2c-0 n=2 ret=2
i2cget-77 6.510805: i2c_result: i2c-0 n=2 ret=-110
i2cget-82 6.694661: i2c_result: i2c-0 n=2 ret=2
i2cget-86 6.863750: i2c_result: i2c-0 n=2 ret=-110
```

출력에서 볼 것:

- **5 클럭**이면 core 루프가 5번째 rising edge 뒤 `get_sda()==1`을 보고 멈췄다. 9번 다 치지 않는다 — 풀리는 즉시 멈춘다.
- **12 클럭**이 필요한 (비정상) 경우 core는 9번까지만 치고 `-EBUSY`(-16)로 포기한다. h5_fakebus는 이를 `-ETIMEDOUT`(-110)으로 바꿔 올렸다. 남은 3 클럭은 **다음 전송**의 recovery에서 풀렸다. 실제 보드에서 "가끔 한 번 실패하고 다음엔 된다"는 증상이 이렇게 생길 수 있다.
- **SCL 고착**(target이 clock stretching을 끝내지 않음, SCL이 GND에 쇼트)은 클럭 복구로 못 푼다. core가 첫 HIGH 확인에서 "SCL is stuck low"로 바로 나간다 → target 전원 리셋(regulator off/on, reset GPIO)이 답이다.
- 마지막 블록은 같은 실행의 `i2c:i2c_result` 이벤트다. 유저가 본 에러(`-11`, `-110`)가 tracepoint에도 그대로 남는다. 현장에서 "가끔 센서 값이 안 온다"는 보고가 있으면 `i2c_result`만 켜 두고 `ret`이 음수인 줄을 찾는 것이 첫 단계다(필터 예: `echo 'ret < 0' > events/i2c/i2c_result/filter`).
- `adap->timeout`은 50 ms로 설정했고 HZ=250이라 실제로는 13 jiffies(52 ms)를 기다린다.

```svg
<svg viewBox="0 0 680 480" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="190" y="12" width="300" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="37" font-size="13" text-anchor="middle">i2c_transfer() 가 음수를 돌려줬다</text>
<line x1="300" y1="52" x2="87" y2="88" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<line x1="320" y1="52" x2="252" y2="88" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<line x1="360" y1="52" x2="417" y2="88" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<line x1="380" y1="52" x2="582" y2="88" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="10" y="90" width="155" height="88" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="87" y="110" font-size="12" text-anchor="middle">-ENXIO (-6)</text>
<text x="87" y="128" font-size="12" text-anchor="middle">주소 NACK</text>
<text x="87" y="146" font-size="12" text-anchor="middle">장치 없음 · 전원 꺼짐</text>
<text x="87" y="164" font-size="12" text-anchor="middle">EEPROM 쓰기 중</text>
<rect x="175" y="90" width="155" height="88" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="252" y="110" font-size="12" text-anchor="middle">-EREMOTEIO (-121)</text>
<text x="252" y="128" font-size="12" text-anchor="middle">data NACK</text>
<text x="252" y="146" font-size="12" text-anchor="middle">없는 레지스터</text>
<text x="252" y="164" font-size="12" text-anchor="middle">쓰기 금지 영역</text>
<rect x="340" y="90" width="155" height="88" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="417" y="110" font-size="12" text-anchor="middle">-EAGAIN (-11)</text>
<text x="417" y="128" font-size="12" text-anchor="middle">arbitration lost</text>
<text x="417" y="146" font-size="12" text-anchor="middle">multi-master 경합</text>
<text x="417" y="164" font-size="12" text-anchor="middle">core 가 자동 재시도</text>
<rect x="505" y="90" width="165" height="88" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="587" y="110" font-size="12" text-anchor="middle">-ETIMEDOUT (-110)</text>
<text x="587" y="128" font-size="12" text-anchor="middle">버스가 안 풀림</text>
<text x="587" y="146" font-size="12" text-anchor="middle">SDA 또는 SCL 이</text>
<text x="587" y="164" font-size="12" text-anchor="middle">LOW 에 고착</text>
<line x1="87" y1="178" x2="87" y2="198" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<line x1="252" y1="178" x2="252" y2="198" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<line x1="417" y1="178" x2="417" y2="198" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<line x1="587" y1="178" x2="587" y2="198" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="10" y="200" width="155" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="87" y="222" font-size="12" text-anchor="middle">주소 · 전원 · 리셋 확인</text>
<text x="87" y="240" font-size="12" text-anchor="middle">at24 는 ack polling</text>
<rect x="175" y="200" width="155" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="252" y="222" font-size="12" text-anchor="middle">레지스터 맵 확인</text>
<text x="252" y="240" font-size="12" text-anchor="middle">errno 는 드라이버마다</text>
<rect x="340" y="200" width="155" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="417" y="222" font-size="12" text-anchor="middle">adap-&gt;retries 만큼</text>
<text x="417" y="240" font-size="12" text-anchor="middle">그래도 실패면 상위로</text>
<rect x="505" y="200" width="165" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="587" y="227" font-size="13" text-anchor="middle">SCL 이 LOW 인가?</text>
<line x1="560" y1="244" x2="380" y2="288" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<line x1="600" y1="244" x2="580" y2="288" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="270" y="290" width="200" height="72" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="370" y="310" font-size="12" text-anchor="middle">예 — SCL 이 LOW</text>
<text x="370" y="328" font-size="12" text-anchor="middle">clock stretching 고착</text>
<text x="370" y="346" font-size="12" text-anchor="middle">→ target 전원 · 리셋 GPIO</text>
<rect x="480" y="290" width="190" height="72" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="575" y="310" font-size="12" text-anchor="middle">아니오 — SDA 만 LOW</text>
<text x="575" y="328" font-size="12" text-anchor="middle">i2c_recover_bus()</text>
<text x="575" y="346" font-size="12" text-anchor="middle">최대 9 클럭 + STOP</text>
<line x1="575" y1="362" x2="575" y2="398" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="450" y="400" width="220" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="560" y="422" font-size="12" text-anchor="middle">SDA 풀림 → 전송 재시도</text>
<text x="560" y="442" font-size="12" text-anchor="middle">안 풀림 → -EBUSY, 전원 사이클</text>
<rect x="10" y="290" width="240" height="90" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5 4"/>
<text x="130" y="312" font-size="12" text-anchor="middle">관찰 도구</text>
<text x="22" y="332" font-size="12">i2c:i2c_result 의 ret 값</text>
<text x="22" y="350" font-size="12">dmesg 의 recovery 로그</text>
<text x="22" y="368" font-size="12">scope: SCL · SDA 정지 레벨</text>
</svg>
```
그림 5 — 에러·복구 판단 흐름. errno로 대분류하고, timeout이면 "어느 선이 LOW에 붙었나"로 갈린다. SDA만 LOW면 클럭으로 풀 수 있고(core의 `i2c_generic_scl_recovery`), SCL이 LOW면 target이 잡고 있는 것이라 전원·리셋으로만 푼다.

---

## 8. at24 EEPROM과 처리량 — 바이트 단위 vs 블록

### 8.1 예제 12 — 256바이트를 네 가지 방식으로 읽기

무엇을 확인하나: 같은 데이터를 읽어도 **트랜잭션 수**가 버스 시간과 CPU 시간을 좌우한다. EEPROM 256바이트를 (1) SMBus read_byte_data 256번 (2) SMBus I2C block 32바이트 8번 (3) `I2C_RDWR` 한 번 (4) at24 nvmem 파일 `pread()` 로 각각 20회 읽고, h5_fakebus의 `stats`(트랜잭션·메시지·계산된 버스 시간)와 벽시계 시간을 잰다.

```c
/* u4_bench: EEPROM 256 B 를 네 가지 방식으로 읽고 트랜잭션 수·버스 시간·벽시계 시간 비교 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define ST "/sys/kernel/debug/h5_fakebus/stats"
static int fd;
static unsigned char buf[256];

static void stats(char *out, int clear)
{
	int s = open(ST, O_RDWR);
	if (clear) { if (write(s, "0", 1) < 0) perror("clear"); }
	else { ssize_t n = read(s, out, 199); out[n > 0 ? n - 1 : 0] = 0; }
	close(s);
}
static int smbus(__u8 cmd, int size, union i2c_smbus_data *d)
{
	struct i2c_smbus_ioctl_data a = { I2C_SMBUS_READ, cmd, (__u32)size, d };
	return ioctl(fd, I2C_SMBUS, &a);
}
static int mode;
static const char *nvpath;
static void run(void)
{
	union i2c_smbus_data d;
	__u8 off = 0;
	struct i2c_msg m[2] = { { 0x50, 0, 1, &off }, { 0x50, I2C_M_RD, 256, buf } };
	struct i2c_rdwr_ioctl_data x = { m, 2 };
	if (mode == 0)                                   /* 1 바이트씩 SMBus read_byte_data */
		for (int i = 0; i < 256; i++) { smbus((__u8)i, I2C_SMBUS_BYTE_DATA, &d); buf[i] = d.byte; }
	else if (mode == 1)                              /* 32 바이트씩 SMBus I2C block */
		for (int i = 0; i < 256; i += 32) {
			d.block[0] = 32; smbus((__u8)i, I2C_SMBUS_I2C_BLOCK_DATA, &d);
			memcpy(buf + i, d.block + 1, 32);
		}
	else if (mode == 2) ioctl(fd, I2C_RDWR, &x);     /* 한 번에 256 바이트 */
	else {                                           /* at24 드라이버의 nvmem 파일 */
		int e = open(nvpath, O_RDONLY);
		if (e < 0 || pread(e, buf, 256, 0) != 256) perror("nvmem");
		close(e);
	}
}
```

```c
/* u4_bench (계속) — main: 방식마다 통계 초기화 → 20회 반복 → 통계·시간 출력 */
int main(int argc, char **argv)
{
	const char *name[] = { "smbus byte x256", "smbus i2c-block 32 x8",
			       "I2C_RDWR 256 x1", "at24 nvmem pread 256" };
	char st[200];
	nvpath = argc > 1 ? argv[1] : "/sys/bus/nvmem/devices/0-00500/nvmem";
	fd = open("/dev/i2c-0", O_RDWR);
	if (fd < 0 || ioctl(fd, I2C_SLAVE_FORCE, 0x50) < 0) { perror("i2c-0"); return 1; }
	for (mode = 0; mode < 4; mode++) {
		struct timespec a, b;
		stats(st, 1);
		clock_gettime(CLOCK_MONOTONIC, &a);
		for (int k = 0; k < 20; k++) run();               /* 20 회 반복 */
		clock_gettime(CLOCK_MONOTONIC, &b);
		stats(st, 0);
		long us = (b.tv_sec - a.tv_sec) * 1000000L + (b.tv_nsec - a.tv_nsec) / 1000;
		printf("%-22s wall/iter=%6ld us  [%.15s] %s\n", name[mode], us / 20, buf, st);
	}
	return 0;
}
```

(위 두 블록이 한 파일이다.) 게스트 스크립트 `t5.sh` — nvmem 장치 이름은 `NVMEM_DEVID_AUTO` 때문에 `0-00501`처럼 번호가 붙으므로 sysfs에서 찾는다:

```sh
insmod /h5_fakebus.ko; insmod /at24.ko; dmesg -n 1
NV=$(ls -d /sys/bus/i2c/devices/0-0050/0-005*)/nvmem; echo "nvmem: $NV"
/u4_bench $NV
```

```text
nvmem: /sys/bus/i2c/devices/0-0050/0-00501/nvmem
smbus byte x256        wall/iter= 22771 us  [H5-EEPROM-24C02] xfers=5120 msgs=10240 bytes=10240 nacks=0 bus_bits=199680 bus_us=499200
smbus i2c-block 32 x8  wall/iter=   830 us  [H5-EEPROM-24C02] xfers=160 msgs=320 bytes=5280 nacks=0 bus_bits=50880 bus_us=127200
I2C_RDWR 256 x1        wall/iter=   127 us  [H5-EEPROM-24C02] xfers=20 msgs=40 bytes=5140 nacks=0 bus_bits=46680 bus_us=116700
at24 nvmem pread 256   wall/iter=  1713 us  [H5-EEPROM-24C02] xfers=40 msgs=80 bytes=5160 nacks=0 bus_bits=47280 bus_us=118200
```

출력에서 볼 것: `stats`는 20회 합계다. 1회로 나누면:

| 방식 | 트랜잭션 / 256 B | 메시지 | 버스 시간 @400 kHz (계산) | QEMU 벽시계 (측정) |
|---|---|---|---|---|
| SMBus byte data × 256 | 256 | 512 | 24.96 ms | 22.8 ms |
| SMBus I2C block 32 × 8 | 8 | 16 | 6.36 ms | 0.83 ms |
| `I2C_RDWR` 256 × 1 | 1 | 2 | 5.84 ms | 0.13 ms |
| at24 nvmem `pread` | 2 | 4 | 5.91 ms | 1.71 ms |

```svg
<svg viewBox="0 0 680 345" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="22" font-size="14" text-anchor="middle" font-weight="bold">EEPROM 256 B 읽기 1회 — 방식별 비교 (h5_fakebus, 400 kHz)</text>
<rect x="250" y="34" width="14" height="10" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="270" y="44" font-size="12">버스 시간 (계산, ms)</text>
<rect x="420" y="34" width="14" height="10" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="440" y="44" font-size="12">QEMU 벽시계 (측정, ms)</text>
<text x="240" y="78" font-size="13" text-anchor="end">smbus byte x256</text>
<text x="240" y="96" font-size="12" text-anchor="end">트랜잭션 256개</text>
<rect x="250" y="64" width="349.4" height="16" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="605.4" y="77" font-size="12">24.96</text>
<rect x="250" y="84" width="318.8" height="16" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="574.8" y="97" font-size="12">22.771</text>
<text x="240" y="138" font-size="13" text-anchor="end">smbus i2c-block 32 x8</text>
<text x="240" y="156" font-size="12" text-anchor="end">트랜잭션 8개</text>
<rect x="250" y="124" width="89.0" height="16" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="345.0" y="137" font-size="12">6.36</text>
<rect x="250" y="144" width="11.6" height="16" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="267.6" y="157" font-size="12">0.830</text>
<text x="240" y="198" font-size="13" text-anchor="end">I2C_RDWR 256 x1</text>
<text x="240" y="216" font-size="12" text-anchor="end">트랜잭션 1개</text>
<rect x="250" y="184" width="81.7" height="16" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="337.7" y="197" font-size="12">5.83</text>
<rect x="250" y="204" width="1.8" height="16" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="257.8" y="217" font-size="12">0.127</text>
<text x="240" y="258" font-size="13" text-anchor="end">at24 nvmem pread</text>
<text x="240" y="276" font-size="12" text-anchor="end">트랜잭션 2개</text>
<rect x="250" y="244" width="82.7" height="16" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="338.7" y="257" font-size="12">5.91</text>
<rect x="250" y="264" width="24.0" height="16" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="280.0" y="277" font-size="12">1.713</text>
<line x1="250" y1="56" x2="250" y2="298" stroke="currentColor"/>
<line x1="250" y1="298" x2="600.0" y2="298" stroke="currentColor"/>
<line x1="250.0" y1="298" x2="250.0" y2="303" stroke="currentColor"/><text x="250.0" y="316" font-size="12" text-anchor="middle">0</text>
<line x1="320.0" y1="298" x2="320.0" y2="303" stroke="currentColor"/><text x="320.0" y="316" font-size="12" text-anchor="middle">5</text>
<line x1="390.0" y1="298" x2="390.0" y2="303" stroke="currentColor"/><text x="390.0" y="316" font-size="12" text-anchor="middle">10</text>
<line x1="460.0" y1="298" x2="460.0" y2="303" stroke="currentColor"/><text x="460.0" y="316" font-size="12" text-anchor="middle">15</text>
<line x1="530.0" y1="298" x2="530.0" y2="303" stroke="currentColor"/><text x="530.0" y="316" font-size="12" text-anchor="middle">20</text>
<line x1="600.0" y1="298" x2="600.0" y2="303" stroke="currentColor"/><text x="600.0" y="316" font-size="12" text-anchor="middle">25</text>
<text x="425.0" y="334" font-size="12" text-anchor="middle">ms (선형 축)</text>
</svg>
```
그림 6 — EEPROM 256바이트 읽기 1회의 버스 시간(파랑, h5_fakebus가 400 kHz로 계산)과 QEMU 안 벽시계 시간(주황, 측정). 버스 시간은 결정적이고, 벽시계 값은 실행마다 수십 % 흔들린다(주석만 다른 이전 빌드로 두 번 더 돌렸을 때 byte 방식이 31~34 ms, at24가 2.5~3.2 ms였다).

해석:

- **버스 시간**: 바이트 단위는 바이트 하나마다 "S + 주소W + reg + Sr + 주소R + data + P"(약 39비트)를 내므로 실제 데이터의 4배 넘는 오버헤드다. 블록 읽기는 데이터 비트가 대부분이라 6 ms 근처로 수렴한다. 400 kHz에서 256바이트의 순수 데이터 비트(256 × 9 = 2304비트)만 5.76 ms이니, 블록 방식은 이론 한계에 가깝다.
- **벽시계 시간**: 가짜 버스라 버스 대기가 없으므로 측정값은 **소프트웨어 오버헤드**(syscall, i2c-dev, core, 에뮬레이션)다. 트랜잭션 수에 거의 비례한다: 256번 → 22.8 ms(1번당 약 89 us — TCG 에뮬레이션 + lockdep 등 debug 커널 비용 포함), 1번 → 0.13 ms.
- **at24가 `I2C_RDWR`보다 느린 이유**(추정): at24는 `io_limit` 128바이트로 두 번에 나눠 읽고(트랜잭션 2개), nvmem → regmap → pm_runtime 경로를 지나며, debug 커널의 lockdep이 그 경로의 잠금마다 비용을 더한다. 실제 하드웨어에서는 버스 시간(5.9 ms)이 지배적이라 이 차이는 묻힌다.
- 실무 결론: 센서 드라이버는 **한 번에 burst로**(regmap `regmap_bulk_read()` / `i2c_smbus_read_i2c_block_data()` / 2-msg `i2c_transfer`) 읽고, 1 kHz IMU를 바이트 단위로 읽는 코드는 400 kHz 버스를 혼자 다 쓴다.

---

## 9. 커널 안에서는 무슨 일이 일어나나

`i2c_smbus_read_byte_data(client, 0x75)` 한 줄이 `.xfer`까지 가는 길(6.18 기준, 함수 이름은 소스에서 확인):

```
i2c_smbus_read_byte_data(client, cmd)                      drivers/i2c/i2c-core-smbus.c
  i2c_smbus_xfer(adap, addr, flags, READ, cmd, BYTE_DATA, &data)
    __i2c_lock_bus_helper(adap)          -> rt_mutex bus_lock (or trylock in atomic)
    __i2c_smbus_xfer(...)
      trace_smbus_read / smbus_write     -> events/smbus/
      if (adap->algo->smbus_xfer)        -> SMBus-only controller (e.g. i801)
      else i2c_smbus_xfer_emulated()     -> build msg[0]=[W cmd], msg[1]=[R 1]
        __i2c_transfer(adap, msg, 2)                       drivers/i2c/i2c-core-base.c
          i2c_check_for_quirks()         -> -EOPNOTSUPP if adap->quirks violated
          trace_i2c_write / i2c_read     -> events/i2c/
          for (try = 0; try <= adap->retries; try++)
            adap->algo->master_xfer(...) == .xfer  -> h5_xfer() / imx_i2c_xfer() ...
            break unless -EAGAIN
          trace_i2c_reply / i2c_result
      trace_smbus_reply / smbus_result
    i2c_unlock_bus(adap)
```

드라이버를 쓸 때 다시 만나는 지점:

- **adapter 드라이버**(SoC bring-up, P 모듈): `.xfer`에서 레지스터 시퀀스 + `wait_for_completion_timeout()`(F), IRQ 핸들러(F), DMA 길이 임계값(H9), `bus_recovery_info`(H4의 GPIO·pinctrl), `adap->quirks`, runtime PM(N).
- **client 드라이버**(센서 드라이버, Q 모듈): probe = `i2c_check_functionality` → regulator/reset(H3, H4) → ID 확인 → regmap(H2) → IIO/input/hwmon 등록(I). 인터럽트 핀은 DT `interrupts`가 있으면 core가 `client->irq`에 넣어 준다(`i2c_device_probe()`가 `fwnode_irq_get()`으로 채움).
- **mux**: 보드에 같은 주소 칩이 여럿이면 DT에 i2c-mux 노드, 드라이버는 그대로.
- 관측: `events/i2c`, `events/smbus`(M 모듈의 ftrace), `/sys/bus/i2c/devices/*/driver` 링크, `dmesg`의 `dev_err_probe` 메시지, `/sys/kernel/debug/devices_deferred`(G3).

---

## 10. Embedded 실무에서 다시 보기

### 10.1 bring-up 첫날: "DT에 넣었는데 센서가 안 보인다"

센서 키트 보드(i.MX8 + IMU@0x68 + 기압센서@0x76)에서 순서대로 본다. 이 노트의 명령이 그대로 쓰인다.

```sh
ls /sys/bus/i2c/devices/                       # 1) adapter(i2c-N)가 있나? 없으면 컨트롤러 드라이버 문제 (clock, pinctrl, status="okay")
cat /sys/bus/i2c/devices/i2c-*/name            # 2) 어느 N이 어느 컨트롤러인가
ls -l /sys/bus/i2c/devices/2-0068/driver       # 3) client는 있는데 driver 링크가 없나? → probe 실패/미로딩
dmesg | grep -i -e 2-0068 -e i2c               # 4) dev_err_probe 메시지, -ENXIO(주소 NACK) 여부
cat /sys/kernel/debug/devices_deferred         # 5) -EPROBE_DEFER로 regulator·GPIO를 기다리는 중인가
```

`-ENXIO`면 Don의 영역으로 넘어간다: 전원 레일, reset 핀, 주소 선택 핀(AD0), 풀업, scope로 ACK 확인. 이 순서가 "소프트웨어 문제인지 하드웨어 문제인지"를 5분 안에 가른다.

### 10.2 현장에서 "가끔 IMU 값이 멈춘다"

로봇이 진동하는 환경에서 커넥터 접촉 불량으로 바이트 중간에 글리치가 생기면 7.3절의 SDA 고착이 실제로 일어난다. 대응 3단:

1. 컨트롤러 노드에 recovery 자원을 준다 — `scl-gpios`/`sda-gpios` + pinctrl `"gpio"` state (드라이버가 `bus_recovery_info`를 지원해야 함).
2. client driver는 `-ETIMEDOUT`/`-EREMOTEIO` 를 받으면 즉시 포기하지 말고 제한된 재시도 + 센서 재초기화(soft reset 레지스터) 경로를 둔다.
3. 현장 로그용으로 `i2c:i2c_result` 이벤트에 `ret < 0` 필터를 걸어 trace buffer를 상시 켜 두거나, 드라이버에 에러 카운터(sysfs/debugfs)를 둔다.

### 10.3 같은 주소 IMU 두 개

좌우 바퀴 모듈에 같은 IMU(주소 고정)를 하나씩 → PCA9548을 DT에 넣고 각 채널 아래에 `imu@68`을 둔다(5.2). 드라이버는 그대로 두 번 probe되고 `i2c-3/3-0068`, `i2c-4/4-0068`처럼 다른 경로에 나타난다. 대역폭은 부모 버스 하나를 나눠 쓴다는 점(8장 계산)을 잊지 말 것.

### 10.4 EEPROM에 보드 정보(시리얼, MAC) 저장

공장 라인에서 at24 + nvmem을 쓰면 유저는 `/sys/bus/nvmem/devices/<이름>/nvmem` 파일을 `pread`/`pwrite` 하기만 하면 된다. page 분할·ack polling은 at24가 한다(4.5). DT에 `nvmem-cells`로 MAC 셀을 정의하면 이더넷 드라이버가 커널 안에서 바로 MAC을 가져간다(J·P 모듈).

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `i2c_transfer()` 반환값을 `if (ret)` 로 검사 | 성공(2)을 에러로 처리, 또는 부분 전송을 성공으로 처리 | 반환값은 처리한 msg 수 | `ret == num` 확인, 음수는 그대로 반환 |
| `i2c_smbus_read_byte_data()` 결과를 `u8`에 바로 저장 | 에러가 0xFA 같은 "값"으로 보임 | 반환형 s32, 음수 = errno | `int`로 받고 `< 0` 검사 |
| write + read를 `i2c_master_send`/`recv`로 따로 | 가끔 다른 레지스터 값, multi-master에서 깨짐 | 중간 STOP | 2-msg `i2c_transfer` 또는 SMBus helper |
| SMBus word로 빅엔디언 레지스터 읽기 | 값의 바이트가 뒤바뀜 | SMBus word는 LSB 먼저 | `i2c_smbus_read_word_swapped()` 또는 수동 조립 |
| `id_table` 누락 | DT 보드에선 되는데 `new_device`로 만든 장치엔 안 붙음 | 매칭 경로가 다름 | `of_match_table`과 `id_table` 둘 다 |
| 6.3 이전 예제의 `probe(client, id)` · `int remove` 복사 | 컴파일 에러 | probe 인자 하나, remove는 void | 현재 시그니처 사용, `i2c_get_match_data()` |
| `i2c_lock_bus()` 잡고 `i2c_transfer()` 호출 | 프로세스가 영원히 멈춤 (lockdep 경고) | 같은 rt_mutex 재귀 | 잠금 안에서는 `__i2c_transfer()` |
| EEPROM에 page 경계를 넘겨 한 번에 쓰기 | 앞쪽 데이터가 덮임 (6.3의 `89234567`) | page 안에서 주소 wrap | at24 사용 또는 page 단위 분할 + ack polling |
| 양산 보드에서 i2cdetect 전체 스캔 | 센서 상태 레지스터 클리어, 일부 칩 오동작 | 찌르기 = 실제 트랜잭션 | 범위 지정, DT·sysfs 먼저 |
| recovery 없는 컨트롤러 설정 | 한 번 SDA 고착되면 재부팅 전까지 버스 사망 | `bus_recovery_info` 없음 | `scl-gpios`·pinctrl gpio state 추가 |

---

## 12. 면접에서 이렇게 말한다

**Q.** Explain the Linux I2C subsystem: adapter, algorithm, client, driver.

**A.** adapter는 버스 하나(`i2c-N`), algorithm은 그 버스를 구동하는 함수 표(`.xfer`, `.functionality`)로 컨트롤러 드라이버가 제공한다. client는 (버스, 주소, 이름)으로 정해진 장치 하나, driver는 그 장치 종류를 다루는 `i2c_driver`로 `probe(client)`를 가진다. 둘은 i2c-core를 사이에 두고 `i2c_msg` 배열로만 대화한다.

> The adapter is one physical bus, and the controller driver registers it with an i2c_algorithm — mainly the xfer callback that turns an array of i2c_msg into START, address, data, repeated START and STOP on the wire, plus a functionality mask. A client is one device on that bus — adapter, 7-bit address, name — usually created from a DT child node of the controller. The i2c_driver matches clients by of_match_table or id_table and gets probe(client). The core in the middle does locking, SMBus emulation, retries and tracing, so a sensor driver works unchanged on any controller.

**꼬리 질문**: master_xfer와 xfer의 차이? → 6.18에서 union으로 같은 포인터, master_xfer는 deprecated 이름.

**Q.** Write the probe function of an I2C client driver for an IMU.

**A.** functionality 확인 → (전원·리셋) → ID 레지스터 읽기 → 다르면 `-ENODEV`, NACK면 에러 전달 → 초기화 → 프레임워크 등록. 3.1절 `h5_imu_probe()`를 그대로 화이트보드에 쓴다.

> I check i2c_check_functionality for the transfers I need, enable the regulator and release reset, then read WHO_AM_I with i2c_smbus_read_byte_data. A negative value is returned through dev_err_probe; a wrong ID returns -ENODEV. Then I wake the chip, set up regmap, request the threaded IRQ from client->irq, and register with IIO. Everything is devm-managed so remove stays small. Since 6.3 probe takes only the client; I'd use i2c_get_match_data for per-variant data.

**Q.** How do you read a register with a repeated start?

**A.** 메시지 두 개: write(레지스터 번호) + read(`I2C_M_RD`)를 한 `i2c_transfer()`로. 배열 사이는 Sr, 끝에만 P. 유저에서는 `I2C_RDWR` ioctl.

> Two i2c_msg in one i2c_transfer call: a one-byte write with the register address, then a read with I2C_M_RD. The core guarantees a repeated START between messages and a single STOP at the end, and the bus lock is held for the whole array. i2c_smbus_read_byte_data does exactly this when the adapter has no native SMBus. From user space it's the I2C_RDWR ioctl.

**Q.** The bus is hung — SDA is stuck low. What do you do?

**A.** 원인은 대개 target이 바이트 중간에 master를 잃은 것. SCL을 최대 9번 토글해 target이 비트를 다 내보내게 하고 STOP을 만든다. Linux에선 `bus_recovery_info` + `i2c_recover_bus()` — core의 `i2c_generic_scl_recovery`가 9 클럭을 친다. SCL이 LOW면 클럭으로 안 되고 target 전원·리셋.

> First I check which line is low. If only SDA is low, a target is stuck mid-byte, so I clock SCL up to nine times until SDA releases, then generate a STOP. In Linux the controller driver provides i2c_bus_recovery_info — usually scl-gpios, sda-gpios and a gpio pinctrl state — and calls i2c_recover_bus on timeout; the core's generic recovery does the nine pulses and checks SDA after each. If SCL itself is held low, it's a stretching or shorted target, and only a power cycle or reset line helps. I'd confirm on a scope and log i2c_result errors via tracepoints.

**Q.** What do -ENXIO, -EREMOTEIO, -EAGAIN and -ETIMEDOUT mean from i2c_transfer?

**A.** ENXIO = 주소 NACK(권장 코드), EREMOTEIO = 데이터 NACK에 흔히 쓰임(드라이버마다 다름), EAGAIN = arbitration lost로 core가 `adap->retries`만큼 재시도, ETIMEDOUT = 완료 안 됨(stretching·고착).

> ENXIO is the recommended code for an address NACK — nobody there, or an EEPROM busy writing. EREMOTEIO is commonly used for a data NACK, but conventions vary across controller drivers, so client drivers should treat any negative as failure. EAGAIN means lost arbitration; the core retries up to adap->retries. ETIMEDOUT means the transfer never completed — stuck bus or excessive clock stretching — which is where bus recovery comes in.

**Q.** SMBus vs I2C in Linux — when does it matter?

**A.** SMBus는 I2C의 제한된 부분집합. `i2c_smbus_*` helper는 adapter가 native SMBus면 그대로, 아니면 core가 `i2c_msg`로 에뮬레이션. 반대로 SMBus 전용 컨트롤러에선 임의 `i2c_transfer`가 안 되므로 드라이버는 functionality를 확인해 경로를 고른다.

> SMBus is a restricted protocol on top of I2C. The i2c_smbus helpers go to the adapter's smbus_xfer if it has one, otherwise the core emulates them with one or two i2c_msg. The reverse isn't true: an SMBus-only controller can't do arbitrary transfers, so a portable driver checks functionality bits — or uses regmap, which picks I2C or SMBus I2C-block accordingly.

**Q.** Why prefer a kernel driver over i2c-dev from user space?

**A.** 인터럽트, 전원·리셋, PM, 다른 드라이버와의 소유권, 표준 ABI(IIO 등), 그리고 버스 전체 권한 문제. i2c-dev는 bring-up과 테스트용.

> i2c-dev can't take the data-ready interrupt, can't sequence regulators or handle suspend, and it fights with any kernel driver bound to the same address — that's why I2C_SLAVE returns EBUSY. It also grants access to the whole bus, PMIC included. A kernel driver exposes a standard interface like IIO that every application can use. I use i2c-dev only for bring-up and factory tests.

**Q.** How would you debug I2C without a logic analyzer?

**A.** i2c/smbus tracepoint. 메시지·주소·플래그·데이터·반환값이 다 찍힌다. `i2c_result`만 켜고 `ret < 0` 필터.

> I enable the i2c and smbus tracepoints in tracefs. i2c_write and i2c_read show each message with address, flags and data, i2c_reply shows what came back, and i2c_result shows the return code. It's basically a protocol analyzer at the message level. For field issues I keep only i2c_result with a ret < 0 filter.

---

## 13. 직접 해보기

1. (개념) `i2c_transfer()`에 메시지 3개 `[W reg][R 2][W reg2]`를 넘기면 버스 파형은? STOP은 몇 개인가? 정답/힌트: S … Sr … Sr … P — STOP은 끝에 1개, 메시지 사이는 모두 repeated START.
2. (코드) h5_fakebus에 `adap->quirks`를 추가해 `max_read_len = 32`로 제한하고, u4_bench의 `I2C_RDWR 256` 방식이 어떤 errno로 실패하는지 확인하라. 정답/힌트: `static const struct i2c_adapter_quirks q = { .max_read_len = 32 };` → `-EOPNOTSUPP`. at24는 `io_limit`과 quirks를 보고 스스로 나눠 읽는지도 관찰.
3. (코드) h5_imu를 regmap으로 바꿔라: `devm_regmap_init_i2c()` + `regmap_bulk_read(map, 0x3B, buf, 6)`. 트레이스가 3장의 raw `i2c_transfer`와 같은지 비교. 정답/힌트: 같은 [W 3b][R 6] 2-msg가 나온다(H2 미리보기).
4. (관찰) `echo 'ret < 0' > /sys/kernel/tracing/events/i2c/i2c_result/filter` 후 7.3절 시나리오를 돌려 실패만 걸러지는지 확인. 정답/힌트: `-11`, `-110` 줄만 남는다.
5. (코드) i2c-gpio(`CONFIG_I2C_GPIO=m`)를 gpio-sim 위에 올려 bit-bang 버스를 만들고 `i2cdetect`를 돌려 보라. 무엇이 ACK하는가? 정답/힌트: target이 없으니 전부 `--`; gpio-sim은 출력값을 그대로 다시 읽으므로 SDA를 끌어내릴 장치가 없다. "SCL 확인" 경고가 나올 수 있다(실행해 보지 않음 — 결과는 직접 확인).
6. (개념) mux 뒤 `i2c-2`의 0x48에 client를 만들려는데 부모 `i2c-0`에도 0x48 client가 있으면? 정답/힌트: `i2c_check_mux_parents()`가 `-EBUSY` — 채널이 열리면 두 장치가 같은 버스에 보이기 때문.
7. (코드) h5_fakebus의 `.xfer`는 recovery가 실패하면 바로 `-ETIMEDOUT`을 돌려준다. 실패 시 `i2c_recover_bus()`를 최대 2번 부르도록 바꾸고 12 클럭 시나리오(7.3)를 다시 돌려라. 정답/힌트: 두 번째 recovery에서 남은 3 클럭이 풀려 첫 `i2cget`이 성공한다. 단 실제 HW에서 무한 재시도는 금물 — 횟수 제한과 로그를 남길 것.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| adapter | `struct i2c_adapter` | 버스 하나. `i2c-N`, `/dev/i2c-N` |
| algorithm | `struct i2c_algorithm` | adapter의 전송 함수 표 — `.xfer`, `.smbus_xfer`, `.functionality` |
| client | `struct i2c_client` | 버스 위 장치 하나 (adapter, 주소, 이름). sysfs `N-00AA` |
| i2c_driver | client driver | `probe(client)`·`remove`·매칭 테이블을 가진 장치 드라이버 |
| `i2c_msg` | 메시지 | (repeated) START + 주소 + 데이터. 배열 끝에만 STOP |
| repeated START (Sr) | 재시작 | STOP 없이 방향을 바꿔 다시 START — 레지스터 읽기의 핵심 |
| SMBus | System Management Bus | I2C의 제한된 부분집합 (command + 정해진 모양) |
| SMBus emulation | 에뮬레이션 | `.smbus_xfer` 없는 adapter에서 core가 SMBus를 `i2c_msg`로 변환 |
| functionality | `I2C_FUNC_*` | adapter가 지원하는 전송 종류 비트마스크 |
| quirks | `i2c_adapter_quirks` | 길이·메시지 수 같은 하드웨어 제약 |
| i2c-dev | `/dev/i2c-N` | 유저스페이스 직접 접근 (`I2C_RDWR`, `I2C_SMBUS`, `I2C_SLAVE`) |
| bus lock | `adap->bus_lock` | 트랜잭션 하나를 원자적으로 만드는 rt_mutex |
| mux | i2c-mux | 채널마다 자식 adapter를 만들어 같은 주소 장치를 분리 |
| bus recovery | `i2c_bus_recovery_info` | SCL 최대 9 클럭 + STOP으로 SDA 고착 해제 |
| arbitration lost | 중재 패배 | multi-master에서 다른 master에게 짐 → `-EAGAIN` |
| ack polling | 응답 폴링 | EEPROM 쓰기 사이클 중 NACK이 ACK로 바뀔 때까지 재시도 |
| at24 | EEPROM 드라이버 | 24cxx EEPROM을 nvmem 파일로 노출 |
| tracepoint | `events/i2c`, `events/smbus` | 메시지 단위로 버스 트래픽을 기록하는 ftrace 이벤트 |

---

## 15. 요약 & 체크리스트

Linux I2C는 Don의 bare-metal I2C 코드를 셋으로 쪼갠다: 컨트롤러를 아는 **adapter driver**(`i2c_adapter` + `i2c_algorithm.xfer`), 장치를 아는 **client driver**(`i2c_driver.probe(client)`), 그 사이에서 잠금·SMBus 에뮬레이션·재시도·트레이스를 맡는 **i2c-core**. 둘은 `i2c_msg` 배열로만 대화하고, 배열 하나가 STOP 하나다. client는 컨트롤러 DT 노드의 자식에서 `of_i2c_register_devices()`로 생기거나 `new_device`·`i2c_new_client_device()`로 만들어지며, OF → ACPI → id_table 순으로 드라이버와 매칭된다. 이 노트는 가짜 컨트롤러 `h5_fakebus`를 직접 써서 IMU·EEPROM·mux를 흉내 냈고, tracepoint로 raw 전송과 SMBus 에뮬레이션의 차이, at24의 NOSTART 쓰기와 ack polling, mux 채널 선택 중첩을 봤다. 에러는 `-ENXIO`(주소 NACK) · `-EREMOTEIO`(데이터 NACK, 드라이버마다 다름) · `-EAGAIN`(arbitration, core 재시도) · `-ETIMEDOUT`(고착)이고, SDA 고착은 `i2c_recover_bus()`의 9 클럭으로 풀리지만 SCL 고착은 전원·리셋만이 답이다. 처리량은 트랜잭션 수가 좌우한다 — 256바이트를 바이트 단위로 읽으면 버스 시간 25 ms, 한 번에 읽으면 5.8 ms.

- [ ] adapter·algorithm·client·driver 네 객체를 bare-metal 코드에 대응시켜 설명할 수 있다
- [ ] `.xfer`가 메시지 배열을 START/Sr/STOP 파형으로 바꾸는 컨트롤러 드라이버 뼈대를 짤 수 있다
- [ ] DT 자식 노드 → `of_i2c_register_devices()` → client → probe 흐름과 `adap.dev.of_node`의 역할을 말할 수 있다
- [ ] 6.18 시그니처로 I2C client driver(probe·remove·`of_match_table`·`id_table`)를 처음부터 쓸 수 있다
- [ ] repeated START 레지스터 읽기를 `i2c_transfer` 2-msg, SMBus helper, `I2C_RDWR`로 각각 쓸 수 있다
- [ ] SMBus 명령이 어떤 `i2c_msg`로 에뮬레이션되는지, word가 LSB 먼저인 이유를 설명할 수 있다
- [ ] `I2C_FUNC_*`와 quirks의 차이, `I2C_M_NOSTART`·`I2C_M_RECV_LEN`의 용도를 말할 수 있다
- [ ] `i2c_lock_bus` + `__i2c_transfer`가 필요한 경우와 데드락 함정을 설명할 수 있다
- [ ] i2cdetect가 quick write와 read byte를 쓰는 규칙과 그 위험을 설명할 수 있다
- [ ] errno별 원인과 대응, `i2c_generic_scl_recovery`의 9 클럭 알고리즘을 설명할 수 있다
- [ ] i2c/smbus tracepoint로 버스 트래픽과 실패를 관측할 수 있다
- [ ] 트랜잭션 수로 I2C 처리량을 계산하고 burst 읽기를 선택할 수 있다

---

## 참고 자료

- 커널 문서 (docs.kernel.org): [I2C/SMBus Subsystem](https://docs.kernel.org/i2c/index.html) — 특히 "Implementing I2C device drivers"(writing-clients), "Instantiating I2C devices", "I2C/SMBus Functionality", "I2C and SMBus fault codes", "The SMBus Protocol", "I2C muxes", "Implementing I2C device drivers in userspace"(dev-interface)
- 랩 소스 `/var/lab/src/linux-6.18.55/`: `include/linux/i2c.h`, `include/uapi/linux/i2c.h`, `include/uapi/linux/i2c-dev.h`, `drivers/i2c/i2c-core-base.c`(`__i2c_transfer`, `i2c_generic_scl_recovery`, `i2c_device_match`), `drivers/i2c/i2c-core-smbus.c`(`i2c_smbus_xfer_emulated`), `drivers/i2c/i2c-core-of.c`, `drivers/i2c/i2c-dev.c`, `drivers/i2c/muxes/i2c-mux-pca954x.c`, `drivers/misc/eeprom/at24.c`, `drivers/base/regmap/regmap-i2c.c`, `include/trace/events/i2c.h`, `include/trace/events/smbus.h`, `Documentation/i2c/fault-codes.rst`
- 같은 파일을 브라우저로: [elixir.bootlin.com/linux/v6.18/source/drivers/i2c](https://elixir.bootlin.com/linux/v6.18/source/drivers/i2c)
- i2c-tools 매뉴얼: `man i2cdetect`, `man i2ctransfer`
- NXP UM10204 "I2C-bus specification and user manual" — "Bus clear" 절 (9 클럭)
- Bootlin "Linux kernel and driver development" 교육 슬라이드 — I2C 드라이버 장
- 이 사이트의 관련 노트: A3(i2c-dev ioctl), A10(i2c-tools, i2c-stub), D(Device Tree), G2·G3(driver model, probe), H2(regmap), H3(regulator·reset), H4(GPIO·pinctrl), M(ftrace)
