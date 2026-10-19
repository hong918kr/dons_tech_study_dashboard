# S4. I2C IMU IIO 드라이버 — 포트폴리오 프로젝트: regmap + 버퍼 + FIFO 타임스탬프 + 하드웨어 없는 테스트 하네스

> **이 노트를 다 읽으면**: upstream 스타일 IIO IMU 드라이버(regmap-i2c, triggered buffer, FIFO watermark, runtime PM, dev_err_probe)를 처음부터 써서 W=1·checkpatch·dt-validate·KUnit을 통과시킬 수 있다 · i2c-stub 대신 "살아 있는" 가짜 칩(I2C adapter + irq_sim + hrtimer)을 만들어 QEMU에서 데이터·인터럽트·진짜 샘플 시각까지 재현할 수 있다 · 지터·드롭·타임스탬프 오차를 직접 만든 도구로 재고 히스토그램으로 보여 줄 수 있다 · 이 프로젝트를 GitHub README와 5분 데모로 설명할 수 있다
> **왜 필요한가**: 로보틱스·국방·차량 Embedded Linux 면접에서 "센서 드라이버 하나를 처음부터 끝까지 써 봤나?"는 거의 반드시 나온다. 말로 하는 답보다 저장소 링크 하나와 실측 그래프가 훨씬 강하다.
> **Don 기준 난이도**: 레지스터 맵·FIFO·인터럽트·타이밍 측정은 SSD/RF bring-up에서 매일 하던 일이다 / 새로 배울 것은 IIO 프레임워크의 계약(trigger·pollfunc·buffer·demux), regmap 분류, devm·PM 수명 관리, 커널 테스트 문화(KUnit, checkpatch, binding YAML)
> **선행 노트**: S1(QEMU 랩), S2(첫 모듈), H2(regmap), H5(I2C), I2(IIO), Q5 4절(FIFO 타임스탬프), P3(데이터시트 → 드라이버), D6(overlay), D7(binding), N6(driver PM)

---

## 0. 큰 그림 — 이게 왜 필요한가

이 노트는 개념 노트가 아니라 **프로젝트 노트**다. 개념은 앞 노트들이 이미 가르쳤다: regmap의 volatile/precious와 noinc는 H2 7·8절, IIO의 trigger·buffer·scan 구조는 I2 4·5절, 타임스탬프 시계는 I2 10절, FIFO 배치 타임스탬프 보간은 Q5 4절, 데이터시트에서 드라이버로 가는 워크플로는 P3, DT overlay는 D6, binding YAML은 D7·P3 8절, 드라이버 PM은 N6. 여기서는 그것들을 **하나의 저장소**로 묶고, 실제로 돌려서 숫자를 낸다.

최종 산출물은 Don의 GitHub에 그대로 올릴 수 있는 저장소 `imu-iio-driver/`다 (작업본: `~/.elinux_scratch/S4/imu-iio-driver/`, git 커밋 5개).

| 산출물 | 내용 | 검증 |
|---|---|---|
| 드라이버 | `s4imu_core.ko` + `s4imu_i2c.ko` (코어/버스 분리, upstream IMU와 같은 구조) | W=1 경고 0, checkpatch --strict 0/0/0 |
| 가짜 칩 | `s4imu_emul.ko`: I2C adapter + irq_sim 인터럽트 컨트롤러 + hrtimer 센서 발진기 + truth 로그 | 드라이버를 수정 없이 probe |
| DT | QEMU overlay(fdtoverlay로 병합), RPi overlay 예시, binding YAML | dt-doc-validate, dt-validate |
| 테스트 | KUnit 7개, QEMU 시나리오 7개, `ci/ci.sh` | CI: ALL PASS |
| 도구 | `imu_jitter.c`(율·지터·드롭·ts 오차·히스토그램), `s4imu_capture.sh`, `hist_svg.py` | 실측 그래프 3장 |
| 문서 | README, 레지스터 맵, 하네스 설계, findings, 실제 HW 포팅 | — |

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="22" font-size="14" text-anchor="middle">imu-iio-driver 프로젝트 구조 — 무엇이 어느 층에 있나</text>
<text x="14" y="52" font-size="13">유저</text>
<rect x="60" y="36" width="190" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="155" y="54" font-size="12" text-anchor="middle">sysfs: in_*_raw, scale</text><text x="155" y="70" font-size="12" text-anchor="middle">sampling_frequency 등</text>
<rect x="262" y="36" width="190" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="357" y="54" font-size="12" text-anchor="middle">/dev/iio:device0 read()</text><text x="357" y="70" font-size="12" text-anchor="middle">scan 24 B (7 x le16 + ts)</text>
<rect x="464" y="36" width="200" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="564" y="54" font-size="12" text-anchor="middle">tools/imu_jitter, libiio</text><text x="564" y="70" font-size="12" text-anchor="middle">(iio_info, iio_readdev)</text>
<line x1="20" y1="92" x2="660" y2="92" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="14" y="122" font-size="13">IIO</text>
<rect x="60" y="104" width="300" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="210" y="122" font-size="12" text-anchor="middle">IIO core: triggered buffer + kfifo,</text><text x="210" y="138" font-size="12" text-anchor="middle">watermark, current_timestamp_clock</text>
<rect x="372" y="104" width="292" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="518" y="122" font-size="12" text-anchor="middle">trigger: s4imu-dev0 (IRQ) 또는</text><text x="518" y="138" font-size="12" text-anchor="middle">s4hrt (iio-trig-hrtimer, IRQ 없을 때)</text>
<text x="14" y="190" font-size="13">드라이버</text>
<rect x="80" y="164" width="400" height="92" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="280" y="182" font-size="13" text-anchor="middle">s4imu_core.ko (src/s4imu_core.c + s4imu_lib.c)</text>
<text x="280" y="200" font-size="12" text-anchor="middle">chan_spec 8개, read/write_raw, avail, mount</text>
<text x="280" y="216" font-size="12" text-anchor="middle">trigger handler: DRDY 1 scan / WTM FIFO drain + ts 추정</text>
<text x="280" y="232" font-size="12" text-anchor="middle">.reenable(잃은 edge 회수), runtime PM, dev_err_probe</text>
<text x="280" y="248" font-size="12" text-anchor="middle">KUnit: s4imu_kunit.ko → s4imu_lib 순수 함수</text>
<rect x="492" y="164" width="172" height="92" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="578" y="190" font-size="13" text-anchor="middle">s4imu_i2c.ko</text><text x="578" y="210" font-size="12" text-anchor="middle">of_match s4lab,s4imu</text><text x="578" y="228" font-size="12" text-anchor="middle">devm_regmap_init_i2c</text><text x="578" y="246" font-size="12" text-anchor="middle">(SPI glue = 확장)</text>
<rect x="80" y="270" width="584" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="372" y="287" font-size="12" text-anchor="middle">regmap: REGCACHE_MAPLE, rd/wr/volatile/precious 표, readable_noinc(FIFO_DATA)</text><text x="372" y="303" font-size="12" text-anchor="middle">→ I2C core: i2c_transfer()  (설정 레지스터 읽기는 캐시에서, 버스 0회)</text>
<line x1="20" y1="322" x2="660" y2="322" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="14" y="352" font-size="13">버스</text>
<rect x="60" y="334" width="290" height="62" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="205" y="352" font-size="12" text-anchor="middle">QEMU: s4imu_emul.ko (테스트 전용)</text><text x="205" y="370" font-size="12" text-anchor="middle">i2c_adapter(.xfer) + 레지스터 파일</text><text x="205" y="388" font-size="12" text-anchor="middle">irq_sim 도메인 = interrupt-controller</text>
<rect x="372" y="334" width="292" height="62" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="518" y="352" font-size="12" text-anchor="middle">실제 보드: SoC I2C 컨트롤러</text><text x="518" y="370" font-size="12" text-anchor="middle">INT 핀 → GPIO irqchip</text><text x="518" y="388" font-size="12" text-anchor="middle">vdd/vddio 레귤레이터 (S3, P4)</text>
<rect x="60" y="410" width="604" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="362" y="428" font-size="12" text-anchor="middle">Device Tree: dts/s4imu-qemu-emul.dtso (fdtoverlay로 virt.dtb에 병합)</text><text x="362" y="444" font-size="12" text-anchor="middle">bindings/iio/imu/s4lab,s4imu.yaml (dt-validate)  ·  dts/s4imu-rpi-i2c1.dtso (가이드)</text>
<line x1="357" y1="80" x2="357" y2="102" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="280" y1="148" x2="280" y2="162" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="578" y1="256" x2="578" y2="268" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="205" y1="310" x2="205" y2="332" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="518" y1="310" x2="518" y2="332" stroke="currentColor" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — 저장소의 층 구조. 위에서부터 유저 도구, IIO core, 드라이버(코어 + I2C glue), regmap, 버스. 맨 아래 버스 층만 QEMU(가짜 칩)와 실제 보드가 다르고 위는 전부 같다.

bare-metal로 비유하면: 드라이버는 Don이 SSD 펌웨어에서 쓰던 "센서/플래시 HAL"이고, IIO core는 그 위의 공통 데이터 수집 프레임워크(링버퍼 + 이벤트 + 타임스탬프)다. 다른 점은 **프레임워크가 정한 계약**을 지켜야 한다는 것 — 콜백이 어떤 컨텍스트에서 불리는지, 누가 메모리를 소유하는지, 언제 전원을 켜도 되는지가 이미 정해져 있다.

### 0.1 왜 실제 칩이 아니라 가상의 칩인가

실제 부품(BMI160, ICM-42688 등)을 흉내 내면 Don이 데이터시트와 다른 값을 "그 칩의 사양"처럼 쓰게 될 위험이 있다. 그래서 **S4IMU라는 가상의 칩**을 정의하고 레지스터 맵을 `docs/register_map.md`에 문서로 고정했다. WHO_AM_I, burst로 읽는 little-endian 데이터 블록, 프레임 단위 FIFO + watermark, clear-on-read 상태 레지스터 같은 **흔한 패턴만** 빌려 왔다. 실제 칩으로 옮길 때 바뀌는 것은 10절에 정리했다.

| 주소 | 이름 | 접근 | 내용 |
|---|---|---|---|
| 0x00 | WHO_AM_I | R | 0x5A |
| 0x02 | STATUS | R, clear-on-read | bit0 DRDY |
| 0x03 | INT_STATUS | R, clear-on-read (precious) | bit0 DRDY, bit1 WTM(래치), bit2 FIFO_OVR |
| 0x04–0x05 | FIFO_CNT | R | 프레임 수, LE16 |
| 0x10–0x1D | ACC xyz, GYR xyz, TEMP | R | s16 LE 7개 = 14 B |
| 0x20 | ODR | RW | 0..6 = 25 … 1600 Hz |
| 0x21 / 0x22 | ACC_RANGE / GYR_RANGE | RW | ±2–16 g / ±250–2000 dps |
| 0x30 / 0x31 | FIFO_CTRL / FIFO_WTM | RW | EN, FLUSH / watermark 1..63 |
| 0x32 | FIFO_DATA | R, no auto-inc (precious) | 읽을 때마다 1 B pop, 프레임 14 B |
| 0x38 | INT_CTRL | RW | DRDY_EN(펄스), WTM_EN(래치), 극성, open-drain |
| 0x3E | PWR_CTRL | RW | 0 suspend, 1 normal (start-up 2 ms) |
| 0x7E | CMD | W | 0xB6 soft reset |

FIFO는 64 프레임이고 가득 차면 **새 프레임을 버린다**(drop-newest). 이 한 줄이 7.4절의 버그 수정에서 결정적이다.

### 0.2 이 노트의 실행 환경

- 빌드: Lima VM `elinux` (호스트 커널 `7.0.0-38-generic`, gcc 15.2.0), 모듈 빌드 트리는 랩의 `/var/lab/out/linux-build-dom`.
- 실행: QEMU `virt` + `Image-dom`(`6.18.55-dom`, IIO·KUnit·irq_sim·regmap 포함), CPU 1개, TCG. 두 가지 게스트를 썼다: BusyBox initramfs(빠름, 대부분의 시나리오)와 `rootfs-dom` qcow2 overlay(Ubuntu + udev + libiio, 9절).
- 소스와 저장소는 Mac 쪽 `~/.elinux_scratch/S4/imu-iio-driver/`, VM에서는 `/var/tmp/S4/repo/`로 rsync해서 빌드했다(VM에서 Mac 홈은 읽기 전용이라 Kbuild 산출물을 쓸 수 없다).

> **TCG 주의**: 이 노트의 모든 시간 수치(IRQ 지연, 지터, 드롭)는 명령어 번역 에뮬레이션 위의 값이고, 같은 VM을 다른 작성자의 QEMU 게스트(S5 PREEMPT_RT 실험 등)와 함께 쓰는 중에 쟀다. **실제 하드웨어 값이 아니다.** 보여 주려는 것은 "어떻게 재는가"와 "모드 사이의 상대적 차이"다.

---

## 1. 준비물과 저장소 구조

### 1.1 준비물

| 항목 | 이 노트에서 쓴 것 | 없으면 |
|---|---|---|
| 커널 빌드 트리 | `/var/lab/out/linux-build-dom` (`source` → `linux-6.18.55`) | S1 노트로 만든다 |
| QEMU + 커널 | `qemu-system-aarch64`, `Image-dom` | S1, 랩 README |
| dtc, fdtoverlay | VM의 `/usr/bin/dtc`, `/usr/bin/fdtoverlay` | `device-tree-compiler` 패키지 |
| dtschema | `/var/tmp/S4/venv` 안에 `pip install dtschema` (2026.9) | 6절 binding 검증만 건너뛴다 |
| pexpect | VM의 python3 (4.9.0) | rootfs 게스트 시나리오(9절)만 영향 |

### 1.2 저장소 구조

```
imu-iio-driver/
  Kbuild  Makefile  LICENSE  README.md  .gitignore
  src/s4imu.h          레지스터 정의, 드라이버 상태 구조체
  src/s4imu_core.c     IIO: 채널, read/write_raw, trigger, FIFO, PM, probe   (696줄)
  src/s4imu_i2c.c      I2C glue: of_match, regmap-i2c                         (55줄)
  src/s4imu_lib.[ch]   순수 함수: ODR/scale 표, 온도 변환, 타임스탬프 추정기
  test/emul/s4imu_emul.c   가짜 칩 (테스트 전용)                              (약 430줄)
  test/kunit/s4imu_kunit.c KUnit 7개
  dts/s4imu-qemu-emul.dtso  dts/s4imu-rpi-i2c1.dtso
  bindings/iio/imu/s4lab,s4imu.yaml
  tools/imu_jitter.c  tools/s4imu_capture.sh  tools/hist_svg.py
  tests/qemu/run.sh  tests/qemu/run_rootfs.py  tests/qemu/guest_*.sh
  ci/ci.sh  .github/workflows/ci.yml (가이드)
  docs/register_map.md test_harness.md findings.md real_hw_port.md img/*.svg
```

모듈을 **코어와 버스 glue로 나눈 이유**: upstream의 `bmi160_core.c` + `bmi160_i2c.c` + `bmi160_spi.c`, `st_lsm6dsx_core.c` + `_i2c.c` + `_spi.c`가 다 이 구조다. SPI 버전을 추가할 때 바뀌는 것은 regmap 초기화 한 줄뿐이다. 코어는 `EXPORT_SYMBOL_NS_GPL(..., "S4IMU")`로 심볼을 내보내고 glue는 `MODULE_IMPORT_NS("S4IMU")`로 받는다(6.13부터 namespace 인자가 문자열 리터럴이다).

### 1.3 Kbuild

```make
# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(src)/src

# the driver: core (+ pure helpers) and bus glue, like upstream IIO IMUs
obj-m += s4imu_core.o
s4imu_core-y := src/s4imu_core.o src/s4imu_lib.o
obj-m += s4imu_i2c.o
s4imu_i2c-y := src/s4imu_i2c.o

# test-only modules
obj-m += s4imu_emul.o
s4imu_emul-y := test/emul/s4imu_emul.o
obj-m += s4imu_kunit.o
s4imu_kunit-y := test/kunit/s4imu_kunit.o
```

`s4imu_lib.o`를 KUnit 모듈에도 링크하고 싶어지지만, 같은 오브젝트를 두 모듈에 넣으면 Kbuild가 경고한다. 그래서 lib는 코어 모듈 안에 두고 KUnit 모듈이 namespace로 import한다. Makefile에는 `w1`(W=1 빌드 후 `warning:`이 하나라도 있으면 실패), `checkpatch`, `dtbo`, `tools`, `check` 타깃을 두었다.

---

## 2. 단계 1 — regmap 설정: 칩의 성격을 표로 적는다

H2 7절에서 배운 분류를 이 칩에 그대로 적용한다. 핵심은 "**어느 레지스터가 칩 스스로 바뀌는가(volatile)**, **어느 레지스터를 읽으면 부작용이 있는가(precious)**, **어느 레지스터가 FIFO 포트인가(noinc)**"다.

```c
/* never cached: changed by the chip itself, or self-clearing */
static const struct regmap_range s4imu_volatile_ranges[] = {
	regmap_reg_range(S4IMU_REG_STATUS, S4IMU_REG_FIFO_CNT_H),
	regmap_reg_range(S4IMU_REG_ACC_X_L, S4IMU_REG_DATA_END),
	regmap_reg_range(S4IMU_REG_FIFO_CTRL, S4IMU_REG_FIFO_CTRL),
	regmap_reg_range(S4IMU_REG_FIFO_DATA, S4IMU_REG_FIFO_DATA),
	regmap_reg_range(S4IMU_REG_CMD, S4IMU_REG_CMD),
};

/* reading has a side effect: never read speculatively (debugfs dump) */
static const struct regmap_range s4imu_precious_ranges[] = {
	regmap_reg_range(S4IMU_REG_INT_STATUS, S4IMU_REG_INT_STATUS),
	regmap_reg_range(S4IMU_REG_FIFO_DATA, S4IMU_REG_FIFO_DATA),
};

static bool s4imu_noinc_reg(struct device *dev, unsigned int reg)
{
	return reg == S4IMU_REG_FIFO_DATA;
}

const struct regmap_config s4imu_regmap_config = {
	.name = "s4imu",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = S4IMU_REG_MAX,
	.rd_table = &s4imu_rd_table,
	.wr_table = &s4imu_wr_table,
	.volatile_table = &s4imu_volatile_table,
	.precious_table = &s4imu_precious_table,
	.readable_noinc_reg = s4imu_noinc_reg,
	.cache_type = REGCACHE_MAPLE,
};
```

(`rd_table`/`wr_table`도 같은 `regmap_reg_range` 표다. 쓰기 전용 CMD는 rd 표에 없다.)

이 표가 주는 효과 세 가지는 3절 smoke 테스트 출력에서 눈으로 확인된다: (1) `in_accel_scale`을 읽을 때 ACC_RANGE를 **캐시에서** 읽으므로 I2C 트랜잭션이 0회다. (2) debugfs의 `registers` 덤프가 **0x03과 0x32를 건너뛴다**(precious라서 덤프가 INT_STATUS를 지워 버리거나 FIFO를 비우는 사고가 안 난다). (3) `regmap_noinc_read(FIFO_DATA, buf, n * 14)`가 한 번의 I2C 읽기로 FIFO를 통째로 가져온다.

FIFO_CTRL이 volatile인 이유도 한 줄 적어 둔다: FLUSH 비트가 self-clearing이라 캐시에 FLUSH=1이 남으면 다음 `update_bits`가 FIFO를 다시 비워 버린다.

**흔한 실패**: INT_STATUS를 volatile에만 넣고 precious에 안 넣으면 `cat registers` 한 번에 래치된 인터럽트가 사라져 스트림이 멈춘다(7.3절과 같은 증상). FIFO_DATA를 noinc로 표시하지 않으면 regmap이 주소를 증가시키며 읽어 0x32, 0x33, 0x34…를 읽는다.

---

## 3. 단계 2 — 채널, sysfs, probe, 그리고 첫 부팅

### 3.1 채널 정의

```c
#define S4IMU_CHAN(_type, _mod, _reg, _si) {				\
	.type = _type,							\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_mod,					\
	.address = _reg,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE),\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
	.scan_index = _si,						\
	.scan_type = {							\
		.sign = 's', .realbits = 16, .storagebits = 16,		\
		.endianness = IIO_LE,					\
	},								\
	.ext_info = s4imu_ext_info,					\
}
```

accel x/y/z, anglvel x/y/z (scan 0–5), temp(raw·scale·offset, scan 6), `IIO_CHAN_SOFT_TIMESTAMP(7)`. ODR은 accel과 gyro가 공유하므로 `shared_by_all` → 파일 이름이 `in_accel_sampling_frequency`가 아니라 그냥 `sampling_frequency`다. scale 값은 `IIO_VAL_INT_PLUS_NANO`로 돌려준다: ±2 g에서 1 LSB = 9.80665 × 2 / 32768 = 0.000598550 m/s², ±2000 dps에서 1 LSB = 2000/32768 × π/180 = 0.001065264 rad/s (MICRO 정밀도면 0.000598로 잘려 0.05 % 오차가 난다). 온도는 `(raw + offset) × scale` = `(raw + 6400) × 3.90625` m°C (6400 = 25 °C × 256).

### 3.2 probe의 순서와 에러 처리

```c
	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(s4imu_supplies),
					     s4imu_supplies);
	if (ret)
		return dev_err_probe(dev, ret, "enable supplies\n");
	fsleep(S4IMU_STARTUP_US);

	ret = iio_read_mount_matrix(dev, &st->orientation);
	if (ret)
		return dev_err_probe(dev, ret, "mount-matrix\n");

	ret = s4imu_chip_init(st);	/* soft reset, WHO_AM_I, defaults, power on */
	if (ret)
		return ret;

	indio_dev->name = name;
	indio_dev->info = &s4imu_info;
	indio_dev->channels = s4imu_channels;
	indio_dev->num_channels = ARRAY_SIZE(s4imu_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      iio_pollfunc_store_time,
					      s4imu_trigger_handler,
					      &s4imu_buffer_ops);
	if (ret)
		return dev_err_probe(dev, ret, "triggered buffer\n");

	if (irq > 0) {
		ret = s4imu_probe_trigger(indio_dev);
		if (ret)
			return ret;
	} else {
		dev_info(dev, "no IRQ: attach a software trigger (iio-trig-hrtimer)\n");
	}

	/* chip is powered (active); drop to suspend after 2 s idle */
	pm_runtime_set_autosuspend_delay(dev, S4IMU_AUTOSUSPEND_MS);
	pm_runtime_use_autosuspend(dev);
	ret = devm_pm_runtime_set_active_enabled(dev);
	if (ret)
		return dev_err_probe(dev, ret, "runtime PM\n");
	pm_runtime_mark_last_busy(dev);
	pm_request_autosuspend(dev);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "register iio device\n");
```

규칙 세 가지:

- **모든 자원이 devm**이다(iio_dev, mutex, regulator, IRQ, trigger, buffer, PM enable, iio 등록, 그리고 `devm_add_action_or_reset(s4imu_power_off)`). remove 함수가 없다. devm 해제는 등록의 역순이므로 "iio 장치 해제 → PM disable → 칩 suspend → regulator off" 순서가 자동으로 맞는다(G3 수명 주기).
- **모든 실패 경로가 `dev_err_probe()`**다. 메시지에 에러 코드 이름이 붙고(`error -ENODEV: ...`), `-EPROBE_DEFER`면 로그 대신 deferred 사유로 기록된다(`/sys/kernel/debug/devices_deferred`).
- `chip_init`에서 soft reset 직후 `regcache_mark_dirty()`를 부른다. 칩이 기본값으로 돌아갔으니 캐시를 믿으면 안 된다는 표시다.

`s4imu_chip_init`은 soft reset → WHO_AM_I 확인 → ODR 100 Hz, ±2 g, ±2000 dps, INT 극성 → PWR normal + 첫 DRDY 대기 순서다. WHO_AM_I가 다르면 `dev_err_probe(dev, -ENODEV, "unexpected WHO_AM_I 0x%02x\n", id)`.

### 3.3 실행 — DT overlay로 probe하고 sysfs 읽기

QEMU의 `-kernel` 부팅에는 bootloader가 없으니, overlay를 **빌드 시점에** base DTB에 병합한다(U-Boot의 `fdt apply`나 RPi 펌웨어의 `dtoverlay=`가 부팅 때 하는 일을 미리 하는 것, D6).

```dts
/dts-v1/;
/plugin/;

#include <dt-bindings/interrupt-controller/irq.h>

&{/} {
	s4emul: i2c-emul {
		compatible = "s4lab,s4imu-i2c-emul";
		#address-cells = <1>;
		#size-cells = <0>;
		interrupt-controller;
		#interrupt-cells = <2>;

		imu@68 {
			compatible = "s4lab,s4imu";
			reg = <0x68>;
			interrupt-parent = <&s4emul>;
			interrupts = <0 IRQ_TYPE_EDGE_RISING>;
			mount-matrix = "1", "0", "0",
				       "0", "1", "0",
				       "0", "0", "1";
		};
	};
};
```

가짜 칩 노드가 **I2C 버스이면서 인터럽트 컨트롤러**다. IMU 노드는 실제 보드와 똑같이 `reg`, `interrupts`, `mount-matrix`를 가진다. 드라이버 입장에서는 실제 보드와 구별할 방법이 없다 — 이것이 이 하네스의 핵심 설계다(4절).

```sh
# VM: 빌드 + overlay 병합 + 게스트 스크립트 실행 (tests/qemu/run.sh가 하는 일)
cd /var/tmp/S4/repo
make KDIR=/var/lab/out/linux-build-dom all -j2
fdtoverlay -i /var/lab/out/virt.dtb -o /var/tmp/S4/virt-s4imu.dtb dts/s4imu-qemu-emul.dtbo
dtc -I dtb -O dts /var/tmp/S4/virt-s4imu.dtb 2>/dev/null | grep -A14 "i2c-emul {"
TMO=300 tests/qemu/run.sh guest_smoke.sh
```

```text
	i2c-emul {
		phandle = <0x8005>;
		#interrupt-cells = <0x02>;
		interrupt-controller;
		#size-cells = <0x00>;
		#address-cells = <0x01>;
		compatible = "s4lab,s4imu-i2c-emul";

		imu@68 {
			mount-matrix = "1", "0", "0", "0", "1", "0", "0", "0", "1";
			interrupts = <0x00 0x01>;
			interrupt-parent = <0x8005>;
			reg = <0x68>;
			compatible = "s4lab,s4imu";
```

`run.sh`는 BusyBox initramfs를 복사해 `.ko`·도구·게스트 스크립트를 넣고, `LAB_CMD=sh~/s4/guest_smoke.sh`로 부팅한다. smoke 테스트 출력(핵심 부분, 실행마다 raw 값은 다르다):

```text
### kernel 6.18.55-dom
s4imu-emul i2c-emul: emulated S4IMU at 0x68 on i2c-0, sim irq 22
s4imu_i2c 0-0068: supply vdd not found, using dummy regulator
s4imu_i2c 0-0068: supply vddio not found, using dummy regulator
### i2c devices
0-0068 i2c-0 
client 0-0068: driver=s4imu_i2c irq= 22: 0 irq_sim 0 Edge s4imu
name = s4imu
sampling_frequency = 100.000000
sampling_frequency_available = 25.000000 50.000000 100.000000 200.000000 400.000000 800.000000 1600.000000
in_accel_scale = 0.000598550
in_accel_scale_available = 0.000598550 0.001197101 0.002394202 0.004788403
in_anglvel_scale = 0.001065264
in_temp_scale = 3.906250
in_temp_offset = 6400
in_mount_matrix = 1, 0, 0; 0, 1, 0; 0, 0, 1
### raw reads (accel z ~ 1 g = 16384 at +-2 g)
in_accel_x_raw = 8180
in_accel_y_raw = -12
in_accel_z_raw = 16400
in_anglvel_z_raw = 1134
in_temp_raw = 1280
### write scale +-8 g and 400 Hz; readback; invalid value
in_accel_scale=0.002394202 sampling_frequency=400.000000 in_accel_z_raw=4102
sh: write error: Invalid argument
write 0.5 rejected (rc=1)
```

출력에서 볼 것:

- `/proc/interrupts`에 `irq_sim 0 Edge s4imu` — DT의 `interrupts`가 가짜 칩의 irq_sim 도메인으로 해석되어 virq 22가 됐다. 버퍼를 켜기 전이라 카운트는 0이다.
- `supply vdd not found, using dummy regulator`는 정상이다. DT에 `vdd-supply`가 없을 때 regulator core가 dummy를 준다. 실제 보드에서는 이 줄이 없어야 한다(10절).
- accel z = 16400 ≈ 1 g × 16384 LSB/g. ±8 g로 바꾸면 4102 ≈ 16384/4. 온도 raw 1280 → (1280 + 6400) × 3.90625 = 30000 m°C = 30 °C(가짜 칩 값).
- 표에 없는 scale(0.5)은 `-EINVAL`로 거절된다.

```text
### regmap debugfs (cache vs volatile)
/sys/kernel/debug/regmap/0-0068-s4imu
s4imu_i2c
00: 5a
01: 01
02: 01
04: 00
05: 00
10: 83
...
20: 02
21: 00
22: 03
38: 10
3e: 01
```

(가운데 `11:`–`16:` 줄은 생략.) 0x03(INT_STATUS)이 목록에 **없다** — precious라서 regmap debugfs가 덤프에서 뺐다. 0x32(FIFO_DATA)도 마찬가지다. 0x38 = 0x10은 INT_CTRL의 active-high 비트만 켜진 상태(버퍼 off), 0x3e = 1은 normal 모드다.

**체크포인트**: 이 단계가 끝나면 `/sys/bus/iio/devices/iio:device0/name`이 `s4imu`이고, `in_accel_z_raw`가 16384 근처이며, `/proc/interrupts`에 `s4imu` 줄이 보여야 한다.

| 증상 | 원인 | 고치는 법 |
|---|---|---|
| i2c 장치가 안 생김 | overlay 미병합, 가짜 칩 모듈 미로드 | `dtc -I dtb -O dts`로 노드 확인, `insmod s4imu_emul.ko` 먼저 |
| probe가 `-EPROBE_DEFER`에서 멈춤 | `interrupt-parent`의 도메인이 아직 없음 | 인터럽트 컨트롤러(여기서는 가짜 칩)를 먼저 로드 |
| `unexpected WHO_AM_I 0x..` | 주소·배선·칩 종류 | `i2ctransfer`로 직접 읽기(3.5절) |
| 부팅 로그가 아예 없음 | 랩의 1 MiB 패딩 `virt.dtb`를 그대로 `-dtb`로 줌 (아래) | `dtc -I dtb -O dtb`로 다시 패킹 |

마지막 줄은 이번에 실제로 겪었다: `Image-dom`(58 MB)에 랩의 `out/virt.dtb`(QEMU dumpdtb 결과, 1 MiB로 패딩됨)를 그대로 주면 earlycon까지 넣어도 출력 없이 멈췄다. 같은 DTB를 `dtc -I dtb -O dtb`로 다시 패킹한 7566 B 파일이나 fdtoverlay 결과(7886 B)는 정상 부팅했다. 원인은 확인하지 않았다(QEMU가 DTB를 놓는 위치와 크기 문제로 추정). `run.sh`는 overlay 없는 시나리오에서 재패킹한 DTB를 쓴다.

### 3.4 runtime PM — 쓰지 않으면 잠든다

```text
### runtime PM
after probe: active delay=2000ms
idle 3 s:   suspended active_ms=2495 suspended_ms=968
after read: active
samples 259
idle again: suspended (emulator samples stop:)
samples 508
samples 508
```

probe 직후 active → 2 s 동안 아무도 안 쓰면 `runtime_suspend` 콜백이 PWR_CTRL=0을 쓰고 suspended. `cat in_accel_z_raw`가 `pm_runtime_resume_and_get()`으로 깨우고(PWR normal + 2 ms start-up + 첫 DRDY 대기), `pm_runtime_put_autosuspend()`로 타이머를 다시 건다. 다시 잠든 뒤 1 s 간격으로 가짜 칩의 `samples` 카운터가 508에서 멈춰 있다 — **칩이 실제로 샘플링을 멈췄다**는 증거다. 버퍼가 켜져 있는 동안은 `preenable`에서 get, `postdisable`에서 put이라 잠들지 않는다. 시스템 슬립은 `SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)`로 같은 콜백을 재사용한다(N6).

이 칩은 suspend 중에도 레지스터가 유지되므로 regcache를 건드리지 않는다. 전원 자체를 끄는 보드라면 H2 9절의 `regcache_cache_only()` + `regcache_mark_dirty()` + `regcache_sync()`가 필요하다(10절 표).

### 3.5 드라이버를 우회해 같은 칩 보기

```text
### raw I2C view of the same chip (i2c-dev, bypassing the driver)
0x5a 0x01
### unload
rmmod ok
```

`i2ctransfer -f -y 0 w1@0x68 0x00 r2` — 포인터 0x00을 쓰고 2바이트 읽기(WHO_AM_I, REV_ID). `-f`는 드라이버가 붙어 있어도 강제로. bring-up 때 "드라이버 문제인가 버스 문제인가"를 가르는 첫 도구다(P3 7절). 마지막으로 `rmmod s4imu_i2c s4imu_core s4imu_emul`이 경고 없이 끝난다 — devm 정리가 맞다는 최소한의 확인이다.

---

## 4. 단계 3 — 테스트 하네스: 하드웨어 없이 "살아 있는" IMU

### 4.1 i2c-stub으로는 왜 안 되나

Q5 2절과 P3 4절은 `i2c-stub`에 레지스터 값을 미리 써 두고 upstream 드라이버를 probe했다. probe와 단위 변환을 확인하기에는 충분하지만, **값이 변하지 않고 인터럽트가 없다**. 버퍼·트리거·FIFO·타임스탬프는 시험할 수 없다. 선택지는 세 가지였다.

| 방법 | 장점 | 단점 |
|---|---|---|
| i2c-stub + 유저 공간 updater + gpio-sim으로 IRQ | 커널 코드 0줄 | updater와 드라이버가 같은 레지스터를 경쟁, clear-on-read·FIFO pop 의미를 못 냄, IRQ 시각이 유저 스케줄링에 묶임 |
| QEMU에 I2C 장치 모델 추가 (C, QEMU 소스) | 진짜 "하드웨어" | QEMU 재빌드(랩 규칙상 무거움), 게스트에서 truth를 보기 어려움 |
| **커널 모듈로 가짜 adapter + irq_sim** (채택) | 레지스터 의미를 정확히 구현, hard-IRQ 시각의 edge, debugfs로 truth 노출, 결정적 고장 주입 | 테스트 코드가 커널 안에 있음(테스트 전용 모듈로 분리) |

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="22" font-size="14" text-anchor="middle">테스트 하네스 — 하드웨어 없이 "살아 있는" IMU 만들기</text>
<rect x="10" y="36" width="660" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="56" font-size="12" text-anchor="middle">Lima VM: tests/qemu/run.sh — make W=1 → initramfs(BusyBox, .ko, 도구)</text>
<text x="340" y="76" font-size="12" text-anchor="middle">→ fdtoverlay → qemu -kernel Image-dom -dtb virt-s4imu.dtb → guest_*.sh 로그</text>
<rect x="10" y="104" width="660" height="304" rx="6" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="24" y="124" font-size="12">QEMU 게스트 (6.18.55-dom, TCG, CPU 1개)</text>
<rect x="30" y="136" width="200" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="130" y="156" font-size="12" text-anchor="middle">imu_jitter</text><text x="130" y="174" font-size="12" text-anchor="middle">Δt 히스토그램, 드롭(seq)</text><text x="130" y="192" font-size="12" text-anchor="middle">ts − truth 오차</text>
<rect x="30" y="236" width="200" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="130" y="258" font-size="12" text-anchor="middle">s4imu 드라이버</text><text x="130" y="276" font-size="12" text-anchor="middle">(테스트 대상, 수정 없음)</text>
<rect x="300" y="136" width="350" height="256" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="475" y="156" font-size="13" text-anchor="middle">s4imu_emul.ko (독립 모델)</text>
<rect x="320" y="170" width="150" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="395" y="192" font-size="12" text-anchor="middle">hrtimer (HARD)</text><text x="395" y="210" font-size="12" text-anchor="middle">= 센서 발진기, ppm</text>
<rect x="485" y="170" width="150" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="560" y="192" font-size="12" text-anchor="middle">레지스터 파일</text><text x="560" y="210" font-size="12" text-anchor="middle">+ FIFO 64 프레임</text>
<rect x="320" y="240" width="150" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="395" y="262" font-size="12" text-anchor="middle">irq_sim 도메인</text><text x="395" y="280" font-size="12" text-anchor="middle">PENDING → virq</text>
<rect x="485" y="240" width="150" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="560" y="262" font-size="12" text-anchor="middle">i2c_algorithm.xfer</text><text x="560" y="280" font-size="12" text-anchor="middle">auto-inc / no-inc</text>
<rect x="320" y="310" width="315" height="68" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="477" y="330" font-size="12" text-anchor="middle">debugfs /sys/kernel/debug/s4imu_emul/</text><text x="477" y="348" font-size="12" text-anchor="middle">stats · truth(seq, 이상적 샘플 시각)</text><text x="477" y="366" font-size="12" text-anchor="middle">inject_stall_ms (IRQ 멈춤 주입)</text>
<line x1="470" y1="198" x2="483" y2="198" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="395" y1="226" x2="395" y2="238" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="320" y1="268" x2="232" y2="268" stroke="currentColor" marker-end="url(#ah3)"/><text x="276" y="262" font-size="12" text-anchor="middle">IRQ</text>
<line x1="232" y1="284" x2="483" y2="284" stroke="currentColor" stroke-dasharray="3 2" marker-end="url(#ah3)"/><text x="276" y="300" font-size="12" text-anchor="middle">I2C</text>
<line x1="130" y1="234" x2="130" y2="208" stroke="currentColor" marker-end="url(#ah3)"/><text x="138" y="224" font-size="12">/dev/iio</text>
<line x1="318" y1="340" x2="150" y2="210" stroke="currentColor" stroke-dasharray="3 2" marker-end="url(#ah3)"/><text x="236" y="330" font-size="12" text-anchor="middle">truth</text>
</svg>
```
그림 2 — 하네스 구조. 드라이버는 `i2c_transfer()`와 IRQ라는, 실제 보드와 똑같은 두 경계로만 가짜 칩과 만난다. truth 로그는 유저 도구가 직접 읽는다.

### 4.2 가짜 칩의 세 부품

**① I2C adapter** — `i2c_algorithm.xfer`(6.18에서 `master_xfer`는 deprecated 별칭) 하나로 레지스터 포인터 모델을 구현한다. 쓰기 메시지의 첫 바이트가 포인터, 나머지는 쓰기 데이터. 읽기는 포인터부터 auto-increment, 단 FIFO_DATA에서는 증가하지 않는다. 주소가 0x68이 아니면 `-ENXIO`(NACK).

```c
static int emul_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct s4emul *e = i2c_get_adapdata(adap);
	unsigned long flags;
	int i, j;

	for (i = 0; i < num; i++)
		if (msgs[i].addr != addr || (msgs[i].flags & I2C_M_TEN))
			return -ENXIO;			/* address NACK */

	spin_lock_irqsave(&e->lock, flags);
	e->n_xfers++;
	for (i = 0; i < num; i++) {
		struct i2c_msg *m = &msgs[i];

		if (m->flags & I2C_M_RD) {
			for (j = 0; j < m->len; j++) {
				m->buf[j] = emul_read(e, e->ptr);
				if (e->ptr != S4IMU_REG_FIFO_DATA)	/* no-inc */
					e->ptr = (e->ptr + 1) & (EMUL_NREGS - 1);
			}
		} else if (m->len) {
			e->ptr = m->buf[0] & (EMUL_NREGS - 1);
			for (j = 1; j < m->len; j++) {
				emul_write(e, e->ptr, m->buf[j]);
				e->ptr = (e->ptr + 1) & (EMUL_NREGS - 1);
			}
		}
	}
	spin_unlock_irqrestore(&e->lock, flags);
	return num;
}
```

`emul_read()`가 레지스터별 부작용을 구현한다: STATUS 읽으면 DRDY 클리어, INT_STATUS 읽으면 전체 클리어 + WTM 래치 해제, FIFO_DATA 읽으면 1바이트 pop(14바이트마다 프레임 하나 감소). 스핀락은 `irqsave` — hrtimer 콜백(hard IRQ 컨텍스트)과 같은 상태를 공유하기 때문이다(F4).

**② irq_sim 인터럽트 컨트롤러** — `devm_irq_domain_create_sim(dev, dev_fwnode(dev), 1)`로 **자기 DT 노드를 fwnode로 하는** irq 도메인을 만든다. 그러면 IMU 노드의 `interrupts = <0 ...>`이 일반 `of_irq_get()` 경로로 이 도메인의 hwirq 0에 매핑된다. irq_sim 도메인에는 `xlate`가 없어서 `irq_domain_translate()`가 `param[0]`을 hwirq로 쓰고 트리거 타입은 무시한다 — 그래서 드라이버는 타입이 NONE이면 rising으로 요청한다. 라인을 올리는 것은 `irq_set_irqchip_state(virq, IRQCHIP_STATE_PENDING, true)`이고, irq_sim이 irq_work로 `generic_handle_irq()`를 불러 드라이버의 hard IRQ 핸들러가 실제 hard IRQ 컨텍스트에서 돈다(H6, F6).

**③ hrtimer = 센서 발진기** — `HRTIMER_MODE_ABS_HARD` 타이머가 ODR 주기로 돈다. 콜백이 늦게 깨어나도 **그 사이 칩이 만들었어야 할 샘플을 모두 만든다**(catch-up 루프). 각 샘플의 "진짜 시각"은 콜백이 깨어난 시각이 아니라 **이상적 발진기 시각** `e->next`다. 실제 센서도 자기 시계로 샘플링하고, CPU가 늦게 알아챌 뿐이니까.

```c
static enum hrtimer_restart emul_tick(struct hrtimer *t)
{
	struct s4emul *e = container_of(t, struct s4emul, timer);
	ktime_t now = ktime_get();
	bool raise = false;
	int burst = 0;

	spin_lock(&e->lock);
	if (!e->running) {
		spin_unlock(&e->lock);
		return HRTIMER_NORESTART;
	}
	/* a late callback still produces every sample the chip would have */
	while (ktime_compare(e->next, now) <= 0 && burst++ < S4IMU_FIFO_DEPTH) {
		raise |= emul_sample(e, e->next);
		e->next = ktime_add_ns(e->next, emul_period_ns(e));
	}
	if (ktime_compare(e->next, now) <= 0)		/* way behind: skip */
		e->next = ktime_add_ns(now, emul_period_ns(e));
	hrtimer_set_expires(t, e->next);
	spin_unlock(&e->lock);

	if (raise && e->virq > 0) {
		irq_set_irqchip_state(e->virq, IRQCHIP_STATE_PENDING, true);
		e->n_irqs++;
	}
	return HRTIMER_RESTART;
}
```

`emul_sample()`는 1 Hz 사인파(accel x, 0.5 g 진폭, `fixp_sin32_rad()`), 1 g + 노이즈(accel z), 0.5 Hz 사인(gyro z, 90 dps)을 range 레지스터에 맞는 LSB로 만들어 데이터 레지스터와 FIFO에 넣고, truth 링(8192개)에 `(seq, 이상적 시각 ns)`를 적는다. 모듈 파라미터:

| 파라미터 | 용도 |
|---|---|
| `clk_ppm` | 발진기 오차(+10000 = 1 % 빠름). 추정기가 실제 주기를 따라가는지 시험 |
| `seq_in_temp` | TEMP 레지스터에 샘플 번호(하위 16비트)를 실음 → 드롭·중복을 정확히 세고 truth와 1:1로 맞춤 |
| `who_am_i` | probe 실패 경로 시험 |
| `standalone` | DT 없이 adapter만 생성 → `new_device` 경로, IRQ 없음 |

debugfs `inject_stall_ms`에 숫자를 쓰면 그 시간 동안 IRQ를 올리지 않는다(버스·CPU 정체 흉내) → FIFO overflow를 결정적으로 만든다(7.4절).

**설계 원칙 하나**: 가짜 칩은 드라이버의 표를 **공유하지 않는다**. ODR 표, 레지스터 의미를 자기 코드로 따로 가진다. 처음에는 `s4imu_odr_period_ns()`를 가져다 썼는데, 그러면 드라이버 표가 틀려도 테스트가 통과한다. 테스트 대상과 테스트 오라클을 분리하는 것은 Don이 SSD 검증에서 "펌웨어가 쓴 로그로 펌웨어를 검증하지 않는다"고 하던 원칙과 같다.

---

## 5. 단계 4 — 버퍼: DRDY 트리거, hrtimer 폴백, FIFO

### 5.1 세 가지 데이터 경로

| 경로 | 언제 | 타임스탬프 | 샘플당 I2C |
|---|---|---|---|
| DRDY 트리거 (`s4imu-dev0`, watermark 1) | IRQ가 DT에 있음 | hard IRQ에서 `iio_pollfunc_store_time` | 1회 (14 B burst) |
| hrtimer 트리거 (`s4hrt`) | IRQ 없음(폴백) | 타이머 시각 | 1회 |
| FIFO (`s4imu-dev0`, watermark > 1) | 고속·저전력 | IRQ 시각 + 추정기로 프레임마다 | 배치당 2회 |

트리거는 `devm_iio_trigger_alloc()` + `devm_request_irq(..., iio_trigger_generic_data_rdy_poll, flags 그리고 IRQF_NO_THREAD, ...)`로 만든다. hard IRQ는 트리거를 poll만 하고, 트리거에 붙은 pollfunc의 top half `iio_pollfunc_store_time`이 **hard IRQ 안에서** 시각을 찍는다. 실제 I2C 읽기는 pollfunc의 스레드(`s4imu_trigger_handler`)에서 한다 — I2C는 잠들 수 있으니 hard IRQ에서 못 한다(F2, F7). 트리거에 `.validate_device = iio_trigger_validate_own_device`를 걸어 다른 장치가 이 트리거를 못 쓰게 한다.

FIFO 모드로 들어가는 스위치는 표준 ABI `buffer/watermark`다. IIO core가 버퍼를 켤 때 `info->hwfifo_set_watermark(indio_dev, watermark)`를 부르고, 드라이버는 값을 저장했다가 트리거의 `set_trigger_state(true)`에서 1이면 DRDY, 2 이상이면 FIFO_WTM 설정 + FLUSH + 래치 해제 + WTM_EN을 쓴다. 이 순서(`preenable` → `update_scan_mode` → `hwfifo_set_watermark` → 버퍼 enable → 트리거 attach(`set_trigger_state`) → `postenable`)는 `drivers/iio/industrialio-buffer.c`의 `iio_enable_buffers()`에서 확인했다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="22" font-size="14" text-anchor="middle">FIFO 모드 데이터 경로 — 타임스탬프가 어디서 찍히고 어떻게 나뉘나</text>
<text x="20" y="60" font-size="12">센서 샘플</text>
<line x1="110" y1="56" x2="660" y2="56" stroke="currentColor" stroke-opacity="0.4"/>
<line x1="130" y1="48" x2="130" y2="64" stroke="#3f9a6b" stroke-width="2"/><line x1="160" y1="48" x2="160" y2="64" stroke="#3f9a6b" stroke-width="2"/><line x1="190" y1="48" x2="190" y2="64" stroke="#3f9a6b" stroke-width="2"/><line x1="220" y1="48" x2="220" y2="64" stroke="#3f9a6b" stroke-width="2"/><line x1="250" y1="48" x2="250" y2="64" stroke="#3f9a6b" stroke-width="2"/><line x1="280" y1="48" x2="280" y2="64" stroke="#3f9a6b" stroke-width="2"/><line x1="310" y1="48" x2="310" y2="64" stroke="#3f9a6b" stroke-width="2"/><line x1="340" y1="48" x2="340" y2="64" stroke="#3f9a6b" stroke-width="2"/><line x1="370" y1="48" x2="370" y2="64" stroke="#3f9a6b" stroke-width="2"/><line x1="400" y1="48" x2="400" y2="64" stroke="#3f9a6b" stroke-width="2"/><line x1="430" y1="48" x2="430" y2="64" stroke="#3f9a6b" stroke-width="2"/>
<text x="130" y="80" font-size="12" text-anchor="middle">t0</text><text x="160" y="80" font-size="12" text-anchor="middle">t1</text><text x="340" y="80" font-size="12" text-anchor="middle">t7 = W번째</text>
<text x="20" y="112" font-size="12">INT 핀</text>
<polyline points="110,116 352,116 352,98 520,98 520,116 660,116" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="436" y="92" font-size="12" text-anchor="middle">WTM 래치 (INT_STATUS 읽을 때까지)</text>
<text x="20" y="150" font-size="12">hard IRQ</text>
<rect x="358" y="136" width="70" height="22" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="393" y="151" font-size="12" text-anchor="middle">irq_ns</text>
<text x="300" y="151" font-size="12" text-anchor="end">지연 L (TCG에서 수백 µs)</text>
<line x1="352" y1="120" x2="380" y2="134" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="20" y="190" font-size="12">IRQ thread</text>
<rect x="436" y="176" width="226" height="22" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="549" y="191" font-size="12" text-anchor="middle">STATUS+CNT 3 B, FIFO n x 14 B</text>
<line x1="428" y1="150" x2="446" y2="174" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="30" y="220" width="620" height="92" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="240" font-size="13" text-anchor="middle">s4imu_ts_batch(irq_ns, n) — Q5 4절 방법 D (정수 연산)</text>
<text x="44" y="262" font-size="12">1) 주기 T̂ ← T̂ + (Δirq/n − T̂)/8  (공칭 ±20 % 밖이면 버림: 드롭·정체 배치)</text>
<text x="44" y="280" font-size="12">2) 앵커 A ← (A + n·T̂) + (irq_ns − 예측)/8  (오차 &gt; max(4T̂, 10 ms)면 A = irq_ns)</text>
<text x="44" y="298" font-size="12">3) A ≤ irq_ns (인과성), 새 첫 프레임 &gt; 이전 앵커 + T̂/2 (단조성) → t_i = A − (n−1−i)·T̂</text>
<rect x="30" y="326" width="300" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="180" y="344" font-size="12" text-anchor="middle">edge를 잃으면 (poll 중 다음 IRQ)</text><text x="180" y="362" font-size="12" text-anchor="middle">.reenable → WTM 래치 확인 → drain</text><text x="180" y="378" font-size="12" text-anchor="middle">시각은 A + n·T̂로 외삽 (no_irq)</text>
<rect x="350" y="326" width="300" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="500" y="344" font-size="12" text-anchor="middle">FIFO overflow (최신 프레임 버림)</text><text x="500" y="362" font-size="12" text-anchor="middle">이번 배치는 직전 체인에 이어 외삽,</text><text x="500" y="378" font-size="12" text-anchor="middle">다음 IRQ에서 새로 앵커</text>
</svg>
```
그림 3 — FIFO 모드의 시간축. 칩은 자기 시계로 샘플링하고, W번째 샘플에서 WTM이 래치되고, CPU는 지연 L 뒤에 hard IRQ에서 시각을 하나 찍는다. 스레드가 n개를 꺼내면 추정기가 n개의 시각을 만든다. 아래 두 상자는 7절에서 찾은 두 가지 예외 경로다.

### 5.2 트리거 핸들러와 FIFO drain

```c
static void s4imu_fifo_drain(struct iio_dev *indio_dev, s64 irq_ns)
{
	struct s4imu_data *st = iio_priv(indio_dev);
	unsigned int n, i;
	u8 hdr[3];		/* INT_STATUS, FIFO_CNT_L, FIFO_CNT_H */
	bool ovr;
	int ret;

	guard(mutex)(&st->lock);
	ret = regmap_bulk_read(st->regmap, S4IMU_REG_INT_STATUS, hdr, sizeof(hdr));
	if (ret)
		return;
	if (!irq_ns && !(hdr[0] & S4IMU_INT_WTM))
		return;				/* nothing was lost */
	ovr = hdr[0] & S4IMU_INT_FIFO_OVR;
	n = min_t(unsigned int, get_unaligned_le16(&hdr[1]), S4IMU_FIFO_DEPTH);
	if (!n) {
		st->short_reads++;
		return;
	}
	ret = regmap_noinc_read(st->regmap, S4IMU_REG_FIFO_DATA, st->fifo_buf,
				n * S4IMU_FRAME_BYTES);
	if (ret)
		return;

	if (ovr)
		st->fifo_overflows++;
	if (irq_ns && !ovr) {
		s4imu_ts_batch(&st->ts, irq_ns, n);
	} else {
		if (!irq_ns)
			st->lost_edges++;
		s4imu_ts_batch_no_irq(&st->ts, n);
	}
	for (i = 0; i < n; i++)
		s4imu_push_frame(indio_dev, &st->fifo_buf[i * S4IMU_FRAME_BYTES],
				 s4imu_ts_frame(&st->ts, n, i));
	if (ovr)
		s4imu_ts_reset(&st->ts, st->ts.nominal_ns);
}
```

레지스터 맵에서 INT_STATUS(0x03)와 FIFO_CNT(0x04–05)를 **붙여 놓은 이유**가 여기서 보인다: 3바이트 burst 하나로 "왜 깨었나 + 몇 개 있나"를 한 번에 읽는다. 실제 칩을 고를 때도 이런 배치를 보면 배치당 I2C 트랜잭션 수를 줄일 수 있다. `fifo_buf`는 `__aligned(IIO_DMA_MINALIGN)` — I2C 컨트롤러가 DMA로 쓸 수 있는 버퍼는 캐시라인 정렬이 필요하다(H9).

`irq_ns == 0`인 호출과 `ovr` 분기는 처음 버전에는 없었다. 7절에서 실측으로 찾은 버그 두 개의 수정이다.

### 5.3 타임스탬프 추정기 (Q5 방법 D의 커널 정수 버전)

Q5 4.2절은 double로 네 방법을 비교했고 "측정 주기 + 앵커 평활(D)"이 간격 오차 1 µs로 가장 좋았다. 커널에는 부동소수점이 없으니 s64 ns와 shift로 옮긴다.

```c
s64 s4imu_ts_batch(struct s4imu_ts *ts, s64 irq_ns, unsigned int n)
{
	s64 meas, pred, err, bound, floor, prev = ts->anchor_ns;

	if (!n)
		return ts->anchor_ns;
	if (!ts->valid) {
		ts->valid = true;
		ts->anchor_ns = irq_ns;
		ts->last_irq_ns = irq_ns;
		return irq_ns;
	}

	/* 1) period: IRQ interval / frames, only if within +-20 % */
	meas = div_s64(irq_ns - ts->last_irq_ns, n + ts->pending);
	ts->pending = 0;
	if (abs(meas - ts->nominal_ns) <= div_s64(ts->nominal_ns, 5))
		ts->period_ns += (meas - ts->period_ns) >> S4IMU_TS_GAIN_SHIFT;

	/* 2) anchor: predict, then pull 1/8 toward the measurement */
	pred = ts->anchor_ns + (s64)n * ts->period_ns;
	err = irq_ns - pred;
	bound = max_t(s64, 4 * ts->period_ns, S4IMU_TS_RESYNC_NS);
	if (abs(err) > bound) {
		ts->anchor_ns = irq_ns;		/* lost frames or long stall */
		ts->resyncs++;
	} else {
		ts->anchor_ns = pred + (err >> S4IMU_TS_GAIN_SHIFT);
	}

	/* 3) causality: a frame cannot be newer than its interrupt */
	if (ts->anchor_ns > irq_ns)
		ts->anchor_ns = irq_ns;
	/* 4) ...but time never runs backwards: oldest new frame > prev + T/2 */
	floor = prev + (s64)(n - 1) * ts->period_ns + ts->period_ns / 2;
	if (ts->anchor_ns < floor)
		ts->anchor_ns = floor;
	ts->last_irq_ns = irq_ns;
	return ts->anchor_ns;
}
```

프레임 i(0 = 가장 오래됨)의 시각은 `anchor − (n − 1 − i) × period`. 32비트 ARM(BeagleBone 등)에서도 빌드되게 64비트 나눗셈은 전부 `div_s64()`다. 4)번 단조성 규칙은 첫 캡처에서 `nonmono=1`이 한 번 나온 뒤 추가했다. upstream `inv_sensors_timestamp.c`가 같은 생각(IRQ 간격 ÷ 샘플 수로 칩 주기 추정, 허용 범위 밖 값은 버림, 앵커를 조금씩만 당김)을 쓴다는 것은 Q5 4.3절에 정리돼 있다.

### 5.4 유저 쪽 — capture 스크립트와 imu_jitter

`tools/s4imu_capture.sh MODE RATE N [WTM]`은 sysfs만으로 버퍼를 준비한다: `current_timestamp_clock`=monotonic(기본값은 realtime — I2 10.1절), `sampling_frequency`, 모든 `scan_elements/*_en`=1, `buffer/length`=1024, 트리거 선택(hrtimer 모드면 configfs로 `s4hrt` 생성), `buffer/watermark`, `buffer/enable`=1, 그리고 `imu_jitter`. `imu_jitter.c`(약 140줄)는 24바이트 scan(7 × le16 + 패딩 2 + s64)을 읽어:

```c
	while (got < want) {
		ssize_t r = read(fd, buf, sizeof(buf));
		if (r < 0) { if (errno == EINTR) continue; perror("read"); return 1; }
		if (r % SCAN) { fprintf(stderr, "short scan (%zd bytes): enable all channels\n", r); return 1; }
		for (ssize_t o = 0; o < r && got < want; o += SCAN, got++) {
			memcpy(&ts[got], buf + o + 16, 8);
			seq[got] = (uint16_t)(buf[o + 12] | buf[o + 13] << 8);
		}
		acc(&slat, (now_ns() - ts[got - 1]) / 1e3);	/* newest sample age */
	}
	close(fd);

	double T = 1e9 / rate;
	for (long i = 1; i < got; i++) {
		double dt = (double)(ts[i] - ts[i - 1]);
		if (dt <= 0) nonmono++;
		if (dt > 1.5 * T) { gaps++; if (!use_seq) drops += lround(dt / T) - 1; }
		if (use_seq) {
			uint16_t d = seq[i] - seq[i - 1];
			if (d == 0) dups++; else drops += d - 1;
		}
		acc(&sdt, (dt - T) / 1e3);
		put(hdt, (dt - T) / 1e3, bin);
	}
```

그 뒤 `-t /sys/kernel/debug/s4imu_emul/truth`를 읽어 seq로 조인하고 `ts − truth`의 통계와 히스토그램을 낸다. 출력 줄의 의미:

| 줄 | 뜻 |
|---|---|
| `drops` / `dups` | seq 번호 차이로 센 잃은 샘플 / 같은 샘플을 두 번 받은 횟수 |
| `gaps` | Δt > 1.5 T 인 구간 수 (seq 없이도 셀 수 있는 근사) |
| `dt-T` | 연속 타임스탬프 간격 − 공칭 주기 (지터) |
| `read_latency` | `read()`가 돌아온 순간 − 그 묶음의 마지막 샘플 시각 |
| `ts-truth` | 드라이버가 붙인 시각 − 가짜 칩의 이상적 샘플 시각 (절대 오차) |
| `H dt/err x n` | 히스토그램 (bin 중심 µs, 개수), 양 끝 bin = 범위 밖 |

**체크포인트**: `s4imu_capture.sh fifo 800 3200 16`이 `drops=0 dups=0 gaps=0 nonmono=0`을 찍어야 한다.

---

## 6. 단계 5 — 정적 검사와 KUnit: "올릴 수 있는 코드"의 기준

### 6.1 W=1, checkpatch, dtc

```sh
cd /var/tmp/S4/repo
make KDIR=/var/lab/out/linux-build-dom clean >/dev/null
make KDIR=/var/lab/out/linux-build-dom w1 -j2 2>&1 | grep -E "CC|LD|warning" | head -20
make KDIR=/var/lab/out/linux-build-dom w1 >/dev/null 2>&1; echo rc=$?
make KDIR=/var/lab/out/linux-build-dom checkpatch 2>&1 | grep -E "^total|ready for" | head -4
```

```text
  CC [M]  src/s4imu_core.o
  CC [M]  src/s4imu_lib.o
  CC [M]  src/s4imu_i2c.o
  CC [M]  test/emul/s4imu_emul.o
  CC [M]  test/kunit/s4imu_kunit.o
  LD [M]  s4imu_core.o
...
  LD [M]  s4imu_kunit.ko
rc=0
total: 0 errors, 0 warnings, 0 checks, 696 lines checked
src/s4imu_core.c has no obvious style problems and is ready for submission.
total: 0 errors, 0 warnings, 0 checks, 55 lines checked
src/s4imu_i2c.c has no obvious style problems and is ready for submission.
```

(나머지 5개 파일도 전부 `0 errors, 0 warnings, 0 checks`.) 실제로 걸렸던 것들: `NSEC_PER_SEC` 미선언(`<linux/time64.h>` 누락), KUnit 안에서 상수식에 `abs()`를 써서 `-Woverflow` 4개(커널 `abs()`는 타입별 `__builtin_choose_expr` 매크로라 상수 s64에서 char 분기가 경고를 냄 → 작은 `abs64()` 헬퍼), checkpatch `--strict`의 "multiple assignments"(`a = b = 0`) 3개와 "blank line after declarations" 1개. `w1` 타깃은 로그에서 `warning:`을 grep해 하나라도 있으면 실패하도록 했다.

### 6.2 binding YAML과 dt-validate

```sh
V=/var/tmp/S4/venv/bin
$V/dt-doc-validate -u bindings bindings/iio/imu/s4lab,s4imu.yaml; echo "doc-validate rc=$?"
$V/dt-mk-schema -j /var/lab/src/linux-6.18.55/Documentation/devicetree/bindings bindings > /var/tmp/S4/schema.json
$V/dt-validate -s /var/tmp/S4/schema.json /var/tmp/S4/work/virt-s4imu.dtb 2>&1 | grep -cE "s4lab|imu@68"
# 일부러 틀린 속성을 넣은 DTB
$V/dt-validate -s /var/tmp/S4/schema.json /var/tmp/S4/bad.dtb 2>&1 | grep -E "s4lab|imu@68" | head -2
```

```text
doc-validate rc=0
0
/var/tmp/S4/bad.dtb: imu@68 (s4lab,s4imu): Unevaluated properties are not allowed ('s4lab,bogus' was unexpected)
	from schema $id: http://devicetree.org/schemas/iio/imu/s4lab,s4imu.yaml
```

merged DTB에서 우리 노드에 대한 경고는 0개다(나머지 경고 10줄은 QEMU가 만든 `pl061@9030000` 같은 노드 이름 규칙 위반으로, 우리와 무관). 스키마가 정말 적용되는지는 **음성 대조**로 확인했다: `s4lab,bogus` 속성을 넣으면 우리 스키마(`$id` 표시)가 거절한다. 처음에는 `$id`와 파일 경로가 안 맞아 "Cannot determine base path from $id"가 났다 — 파일을 `bindings/iio/imu/` 아래로 옮겨 커널 트리와 같은 상대 경로를 만들었다(D7). upstream에 올리려면 `s4lab` vendor prefix를 `vendor-prefixes.yaml`에 등록해야 한다(가상 회사라 하지 않았다).

### 6.3 KUnit — 순수 함수부터 테스트한다

하드웨어를 만지는 코드는 하네스로, **수학은 KUnit으로** 시험한다. 그래서 표·변환·추정기를 `s4imu_lib.c`에 부작용 없는 함수로 모았다. 테스트 7개: ODR 표와 주기(ns), scale × full-scale = 9.80665 m/s²·2000 dps(rad/s) 왕복, 온도 변환의 경계(−32768, −1, 0, 1, 32767), 첫 배치, **수렴**(100 Hz 공칭, 1 % 빠른 시계, W=16, 20–220 µs 의사난수 지연, 300 배치 동안 인과성·단조성 유지, 주기 오차 < 5 µs, 절대 오차 < 0.3 ms), 정체 후 재동기화, 잃은 edge 뒤 외삽.

```c
/* 100 Hz nominal, oscillator 1 % fast, W = 16, IRQ latency 20..220 us */
static void s4imu_test_ts_converge(struct kunit *test)
{
	const s64 nominal = 10000000, real = 9900990;	/* 1e7 / 1.01 */
	struct s4imu_ts ts = { };
	s64 prev = 0, t0 = 5000000000LL, maxerr = 0;
	u32 lcg = 1;
	int k, i;

	s4imu_ts_reset(&ts, nominal);
	for (k = 0; k < 300; k++) {
		s64 last = t0 + (s64)(16 * k + 15) * real;
		s64 irq;

		lcg = lcg * 1103515245 + 12345;
		irq = last + 20000 + (lcg >> 8) % 200000;
		s4imu_ts_batch(&ts, irq, 16);
		KUNIT_EXPECT_LE(test, ts.anchor_ns, irq);	/* causality */
		for (i = 0; i < 16; i++) {
			s64 t = s4imu_ts_frame(&ts, 16, i);

			KUNIT_EXPECT_GT(test, t, prev);		/* monotonic */
			prev = t;
			if (k >= 100)
				maxerr = max(maxerr, abs(t - (t0 + (s64)(16 * k + i) * real)));
		}
	}
	KUNIT_EXPECT_LT(test, abs(ts.period_ns - real), 5000LL);
	KUNIT_EXPECT_LT(test, maxerr, 300000LL);	/* < 0.3 ms vs truth */
	KUNIT_EXPECT_EQ(test, ts.resyncs, 0U);
}
```

out-of-tree KUnit 모듈은 `insmod`하는 순간 suite가 돌고 결과가 KTAP으로 dmesg에 나온다(`CONFIG_KUNIT=y`인 Image-dom).

```sh
TMO=300 tests/qemu/run.sh guest_kunit.sh    # insmod s4imu_core.ko; insmod s4imu_kunit.ko; dmesg
```

```text
KTAP version 1
1..1
    KTAP version 1
    # Subtest: s4imu
    # module: s4imu_kunit
    1..7
    ok 1 s4imu_test_odr
    ok 2 s4imu_test_scale
    ok 3 s4imu_test_temp
    ok 4 s4imu_test_ts_first_batch
    ok 5 s4imu_test_ts_converge
    ok 6 s4imu_test_ts_resync
    ok 7 s4imu_test_ts_no_irq
# s4imu: pass:7 fail:0 skip:0 total:7
# Totals: pass:7 fail:0 skip:0 total:7
ok 1 s4imu
```

(그 앞의 `KTAP version 1 / 1..0`은 부팅 때 built-in suite가 없어서 찍힌 빈 결과다.) 이 형식은 CI가 그대로 파싱한다(P6).

---

## 7. 단계 6 — 실측: 캡처 매트릭스, 그리고 하네스가 찾은 버그 세 개

### 7.1 캡처 매트릭스

```sh
TMO=600 tests/qemu/run.sh guest_capture.sh       # DRDY 50/200/800, FIFO(W=16) 200/800/1600, hrtimer 200/800
```

출력(최종 코드, 핵심 줄만. `read_latency`와 가짜 칩 `stats` 줄은 생략):

```text
### mode=drdy rate=800 trigger=s4imu-dev0 watermark=1
samples=4000 span=5.069s rate=788.96Hz drops=56 dups=0 gaps=37 nonmono=0
dt-T           n=3999 mean=17.5 sd=349.5 min=-888.0 max=14343.6 (us)
ts-truth       n=4000 mean=404.7 sd=174.1 min=-2637.3 max=1190.1 (us)
debugfs: overflows=0 resyncs=0 lost_edges=0 period_est=1250000ns
### mode=fifo rate=800 trigger=s4imu-dev0 watermark=16
samples=3200 span=3.999s rate=800.04Hz drops=0 dups=0 gaps=0 nonmono=0
dt-T           n=3199 mean=-0.1 sd=40.7 min=-636.9 max=178.8 (us)
ts-truth       n=3200 mean=144.5 sd=312.4 min=-740.0 max=619.8 (us)
debugfs: overflows=0 resyncs=0 lost_edges=2 period_est=1247992ns
### mode=fifo rate=1600 trigger=s4imu-dev0 watermark=16
samples=6400 span=3.998s rate=1600.37Hz drops=0 dups=0 gaps=0 nonmono=0
dt-T           n=6399 mean=-0.1 sd=23.0 min=-315.6 max=143.5 (us)
ts-truth       n=6400 mean=52.5 sd=175.2 min=-777.8 max=390.1 (us)
debugfs: overflows=0 resyncs=0 lost_edges=26 period_est=622364ns
### mode=hrtimer rate=800 trigger=s4hrt watermark=1
samples=4000 span=5.151s rate=776.43Hz drops=1242 dups=1121 gaps=56 nonmono=0
dt-T           n=3999 mean=37.9 sd=554.0 min=-865.2 max=22730.6 (us)
ts-truth       n=4000 mean=446.1 sd=500.4 min=-6453.0 max=1181.5 (us)
```

전체 8개 구성을 같은 실행에서 모은 표다. 같은 매트릭스를 VM 부하가 낮던 앞선 실행(코드는 5.2절 `s4imu_push_frame`과 overflow 분기 도입 직전이지만, 이 실행에서 overflow는 없었고 전체 채널 캡처 경로는 동일)에서도 돌렸으므로 두 번째 열로 같이 적는다.

| 구성 | drops (최종) | sd(dt−T) 최종 | sd(dt−T) 저부하 실행 | ts−truth 평균 최종 |
|---|---|---|---|---|
| DRDY 50 Hz | 0 | 3547 µs | 278 µs | 4167 µs |
| DRDY 200 Hz | 22 | 1761 µs | 557 µs | 1079 µs |
| DRDY 800 Hz | 56 | 350 µs | 347 µs | 405 µs |
| FIFO W16 200 Hz | 0 | 64 µs | 4.5 µs | 1176 µs |
| FIFO W16 800 Hz | 0 | 41 µs | 7.9 µs | 145 µs |
| FIFO W16 1600 Hz | 0 | 23 µs | 5.0 µs | 53 µs |
| hrtimer 200 Hz | 169 (+165 dups) | 949 µs | 919 µs | 1426 µs |
| hrtimer 800 Hz | 1242 (+1121 dups) | 554 µs | 288 µs | 446 µs |

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><text x="340.0" y="22" font-size="14" text-anchor="middle">모드·ODR별 샘플 간격 지터 sd(Δt − T), 실측 (TCG)</text><line x1="70" y1="250.0" x2="660" y2="250.0" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="254.0" font-size="12" text-anchor="end">1 µs</text><line x1="70" y1="197.5" x2="660" y2="197.5" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="201.5" font-size="12" text-anchor="end">10 µs</text><line x1="70" y1="145.0" x2="660" y2="145.0" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="149.0" font-size="12" text-anchor="end">100 µs</text><line x1="70" y1="92.5" x2="660" y2="92.5" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="96.5" font-size="12" text-anchor="end">1000 µs</text><line x1="70" y1="40.0" x2="660" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="44.0" font-size="12" text-anchor="end">10000 µs</text><rect x="84.8" y="63.6" width="44.2" height="186.4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="106.9" y="58.6" font-size="12" text-anchor="middle">3547</text><text x="106.9" y="266" font-size="12" text-anchor="middle">drdy</text><text x="106.9" y="281" font-size="12" text-anchor="middle">50 Hz</text><text x="106.9" y="296" font-size="12" text-anchor="middle">drop 0</text><rect x="158.5" y="79.6" width="44.2" height="170.4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="180.6" y="74.6" font-size="12" text-anchor="middle">1761</text><text x="180.6" y="266" font-size="12" text-anchor="middle">drdy</text><text x="180.6" y="281" font-size="12" text-anchor="middle">200 Hz</text><text x="180.6" y="296" font-size="12" text-anchor="middle">drop 22</text><rect x="232.2" y="116.5" width="44.2" height="133.5" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="254.4" y="111.5" font-size="12" text-anchor="middle">350</text><text x="254.4" y="266" font-size="12" text-anchor="middle">drdy</text><text x="254.4" y="281" font-size="12" text-anchor="middle">800 Hz</text><text x="254.4" y="296" font-size="12" text-anchor="middle">drop 56</text><rect x="306.0" y="155.4" width="44.2" height="94.6" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="328.1" y="150.4" font-size="12" text-anchor="middle">64</text><text x="328.1" y="266" font-size="12" text-anchor="middle">fifo</text><text x="328.1" y="281" font-size="12" text-anchor="middle">200 Hz</text><text x="328.1" y="296" font-size="12" text-anchor="middle">drop 0</text><rect x="379.8" y="165.5" width="44.2" height="84.5" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="401.9" y="160.5" font-size="12" text-anchor="middle">41</text><text x="401.9" y="266" font-size="12" text-anchor="middle">fifo</text><text x="401.9" y="281" font-size="12" text-anchor="middle">800 Hz</text><text x="401.9" y="296" font-size="12" text-anchor="middle">drop 0</text><rect x="453.5" y="178.5" width="44.2" height="71.5" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="475.6" y="173.5" font-size="12" text-anchor="middle">23</text><text x="475.6" y="266" font-size="12" text-anchor="middle">fifo</text><text x="475.6" y="281" font-size="12" text-anchor="middle">1600 Hz</text><text x="475.6" y="296" font-size="12" text-anchor="middle">drop 0</text><rect x="527.2" y="93.7" width="44.2" height="156.3" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><text x="549.4" y="88.7" font-size="12" text-anchor="middle">949</text><text x="549.4" y="266" font-size="12" text-anchor="middle">hrtimer</text><text x="549.4" y="281" font-size="12" text-anchor="middle">200 Hz</text><text x="549.4" y="296" font-size="12" text-anchor="middle">drop 169</text><rect x="601.0" y="106.0" width="44.2" height="144.0" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><text x="623.1" y="101.0" font-size="12" text-anchor="middle">554</text><text x="623.1" y="266" font-size="12" text-anchor="middle">hrtimer</text><text x="623.1" y="281" font-size="12" text-anchor="middle">800 Hz</text><text x="623.1" y="296" font-size="12" text-anchor="middle">drop 1242</text><line x1="70" y1="250" x2="660" y2="250" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="250" stroke="currentColor"/><text x="340.0" y="322" font-size="12" text-anchor="middle">drop = 시퀀스 번호로 센 잃은 샘플 수 (FIFO는 전부 0)</text></svg>
```
그림 4 — 최종 실행의 모드·ODR별 간격 지터(로그 축). FIFO(초록)가 DRDY(파랑)·hrtimer(주황)보다 한 자릿수 이상 작고, 드롭은 FIFO만 0이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><text x="340.0" y="22" font-size="14" text-anchor="middle">샘플 간격 지터 — DRDY 800 Hz, FIFO wtm16 800 Hz</text><line x1="70" y1="260.0" x2="660" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="264.0" font-size="12" text-anchor="end">1</text><line x1="70" y1="205.0" x2="660" y2="205.0" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="209.0" font-size="12" text-anchor="end">10</text><line x1="70" y1="150.0" x2="660" y2="150.0" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="154.0" font-size="12" text-anchor="end">100</text><line x1="70" y1="95.0" x2="660" y2="95.0" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="99.0" font-size="12" text-anchor="end">1000</text><line x1="70" y1="40.0" x2="660" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="44.0" font-size="12" text-anchor="end">10000</text><rect x="70.0" y="260.0" width="7.0" height="0.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="85.1" y="260.0" width="7.0" height="0.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="100.3" y="260.0" width="7.0" height="0.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="115.4" y="243.4" width="7.0" height="16.6" fill="#4a7bd0" fill-opacity="0.7"/><rect x="130.5" y="243.4" width="7.0" height="16.6" fill="#4a7bd0" fill-opacity="0.7"/><rect x="145.6" y="243.4" width="7.0" height="16.6" fill="#4a7bd0" fill-opacity="0.7"/><rect x="160.8" y="226.9" width="7.0" height="33.1" fill="#4a7bd0" fill-opacity="0.7"/><rect x="175.9" y="217.2" width="7.0" height="42.8" fill="#4a7bd0" fill-opacity="0.7"/><rect x="191.0" y="213.5" width="7.0" height="46.5" fill="#4a7bd0" fill-opacity="0.7"/><rect x="206.2" y="195.3" width="7.0" height="64.7" fill="#4a7bd0" fill-opacity="0.7"/><rect x="221.3" y="164.3" width="7.0" height="95.7" fill="#4a7bd0" fill-opacity="0.7"/><rect x="236.4" y="136.2" width="7.0" height="123.8" fill="#4a7bd0" fill-opacity="0.7"/><rect x="251.5" y="128.1" width="7.0" height="131.9" fill="#4a7bd0" fill-opacity="0.7"/><rect x="266.7" y="141.8" width="7.0" height="118.2" fill="#4a7bd0" fill-opacity="0.7"/><rect x="281.8" y="128.1" width="7.0" height="131.9" fill="#4a7bd0" fill-opacity="0.7"/><rect x="296.9" y="116.1" width="7.0" height="143.9" fill="#4a7bd0" fill-opacity="0.7"/><rect x="312.1" y="123.1" width="7.0" height="136.9" fill="#4a7bd0" fill-opacity="0.7"/><rect x="327.2" y="121.6" width="7.0" height="138.4" fill="#4a7bd0" fill-opacity="0.7"/><rect x="342.3" y="127.1" width="7.0" height="132.9" fill="#4a7bd0" fill-opacity="0.7"/><rect x="357.4" y="133.8" width="7.0" height="126.2" fill="#4a7bd0" fill-opacity="0.7"/><rect x="372.6" y="144.7" width="7.0" height="115.3" fill="#4a7bd0" fill-opacity="0.7"/><rect x="387.7" y="127.1" width="7.0" height="132.9" fill="#4a7bd0" fill-opacity="0.7"/><rect x="402.8" y="119.1" width="7.0" height="140.9" fill="#4a7bd0" fill-opacity="0.7"/><rect x="417.9" y="125.4" width="7.0" height="134.6" fill="#4a7bd0" fill-opacity="0.7"/><rect x="433.1" y="136.1" width="7.0" height="123.9" fill="#4a7bd0" fill-opacity="0.7"/><rect x="448.2" y="135.2" width="7.0" height="124.8" fill="#4a7bd0" fill-opacity="0.7"/><rect x="463.3" y="153.9" width="7.0" height="106.1" fill="#4a7bd0" fill-opacity="0.7"/><rect x="478.5" y="186.2" width="7.0" height="73.8" fill="#4a7bd0" fill-opacity="0.7"/><rect x="493.6" y="195.3" width="7.0" height="64.7" fill="#4a7bd0" fill-opacity="0.7"/><rect x="508.7" y="202.7" width="7.0" height="57.3" fill="#4a7bd0" fill-opacity="0.7"/><rect x="523.8" y="205.0" width="7.0" height="55.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="539.0" y="207.5" width="7.0" height="52.5" fill="#4a7bd0" fill-opacity="0.7"/><rect x="554.1" y="243.4" width="7.0" height="16.6" fill="#4a7bd0" fill-opacity="0.7"/><rect x="569.2" y="243.4" width="7.0" height="16.6" fill="#4a7bd0" fill-opacity="0.7"/><rect x="584.4" y="260.0" width="7.0" height="0.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="599.5" y="243.4" width="7.0" height="16.6" fill="#4a7bd0" fill-opacity="0.7"/><rect x="614.6" y="233.8" width="7.0" height="26.2" fill="#4a7bd0" fill-opacity="0.7"/><rect x="644.9" y="191.0" width="7.0" height="69.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="153.2" y="217.2" width="7.0" height="42.8" fill="#e08a3c" fill-opacity="0.7"/><rect x="168.3" y="260.0" width="7.0" height="0.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="183.5" y="233.8" width="7.0" height="26.2" fill="#e08a3c" fill-opacity="0.7"/><rect x="198.6" y="260.0" width="7.0" height="0.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="213.7" y="260.0" width="7.0" height="0.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="228.8" y="260.0" width="7.0" height="0.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="259.1" y="260.0" width="7.0" height="0.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="274.2" y="260.0" width="7.0" height="0.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="289.4" y="260.0" width="7.0" height="0.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="319.6" y="213.5" width="7.0" height="46.5" fill="#e08a3c" fill-opacity="0.7"/><rect x="334.7" y="200.6" width="7.0" height="59.4" fill="#e08a3c" fill-opacity="0.7"/><rect x="349.9" y="68.3" width="7.0" height="191.7" fill="#e08a3c" fill-opacity="0.7"/><rect x="365.0" y="161.0" width="7.0" height="99.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="380.1" y="182.2" width="7.0" height="77.8" fill="#e08a3c" fill-opacity="0.7"/><rect x="395.3" y="202.7" width="7.0" height="57.3" fill="#e08a3c" fill-opacity="0.7"/><rect x="410.4" y="243.4" width="7.0" height="16.6" fill="#e08a3c" fill-opacity="0.7"/><line x1="70" y1="260" x2="660" y2="260" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="260" stroke="currentColor"/><text x="77.6" y="276" font-size="12" text-anchor="middle">-900</text><text x="153.2" y="276" font-size="12" text-anchor="middle">-650</text><text x="228.8" y="276" font-size="12" text-anchor="middle">-400</text><text x="304.5" y="276" font-size="12" text-anchor="middle">-150</text><text x="380.1" y="276" font-size="12" text-anchor="middle">100</text><text x="455.8" y="276" font-size="12" text-anchor="middle">350</text><text x="531.4" y="276" font-size="12" text-anchor="middle">600</text><text x="607.1" y="276" font-size="12" text-anchor="middle">850</text><text x="365.0" y="294" font-size="12" text-anchor="middle">샘플 간격 − 공칭 주기 (µs, 양 끝 칸 = 범위 밖)</text><text x="16" y="150.0" font-size="12" text-anchor="middle" transform="rotate(-90 16 150.0)">샘플 수 (log)</text><rect x="80" y="312" width="12" height="12" fill="#4a7bd0" fill-opacity="0.7"/><text x="98" y="322" font-size="12">DRDY 800 Hz (sd 350 µs)</text><rect x="380" y="312" width="12" height="12" fill="#e08a3c" fill-opacity="0.7"/><text x="398" y="322" font-size="12">FIFO wtm16 800 Hz (sd 41 µs)</text></svg>
```
그림 5 — 800 Hz에서 샘플 간격 오차 분포(최종 실행, `tools/hist_svg.py`로 로그에서 생성). DRDY는 ±400 µs로 퍼지고 양 끝(범위 밖 = 드롭으로 생긴 큰 간격)에 꼬리가 있다. FIFO는 3200개 중 3062개가 0 bin(±25 µs)에 있다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><text x="340.0" y="22" font-size="14" text-anchor="middle">타임스탬프 오차 — DRDY 800 Hz, FIFO wtm16 800 Hz</text><line x1="70" y1="260.0" x2="660" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="264.0" font-size="12" text-anchor="end">1</text><line x1="70" y1="186.7" x2="660" y2="186.7" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="190.7" font-size="12" text-anchor="end">10</text><line x1="70" y1="113.3" x2="660" y2="113.3" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="117.3" font-size="12" text-anchor="end">100</text><line x1="70" y1="40.0" x2="660" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/><text x="64" y="44.0" font-size="12" text-anchor="end">1000</text><rect x="70.0" y="260.0" width="6.6" height="0.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="142.0" y="260.0" width="6.6" height="0.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="242.7" y="260.0" width="6.6" height="0.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="271.5" y="260.0" width="6.6" height="0.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="300.2" y="260.0" width="6.6" height="0.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="314.6" y="260.0" width="6.6" height="0.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="329.0" y="237.9" width="6.6" height="22.1" fill="#4a7bd0" fill-opacity="0.7"/><rect x="343.4" y="237.9" width="6.6" height="22.1" fill="#4a7bd0" fill-opacity="0.7"/><rect x="372.2" y="176.0" width="6.6" height="84.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="386.6" y="129.6" width="6.6" height="130.4" fill="#4a7bd0" fill-opacity="0.7"/><rect x="401.0" y="100.8" width="6.6" height="159.2" fill="#4a7bd0" fill-opacity="0.7"/><rect x="415.4" y="89.4" width="6.6" height="170.6" fill="#4a7bd0" fill-opacity="0.7"/><rect x="429.8" y="64.3" width="6.6" height="195.7" fill="#4a7bd0" fill-opacity="0.7"/><rect x="444.1" y="60.2" width="6.6" height="199.8" fill="#4a7bd0" fill-opacity="0.7"/><rect x="458.5" y="60.2" width="6.6" height="199.8" fill="#4a7bd0" fill-opacity="0.7"/><rect x="472.9" y="73.0" width="6.6" height="187.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="487.3" y="61.0" width="6.6" height="199.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="501.7" y="88.8" width="6.6" height="171.2" fill="#4a7bd0" fill-opacity="0.7"/><rect x="516.1" y="100.6" width="6.6" height="159.4" fill="#4a7bd0" fill-opacity="0.7"/><rect x="530.5" y="84.3" width="6.6" height="175.7" fill="#4a7bd0" fill-opacity="0.7"/><rect x="544.9" y="75.3" width="6.6" height="184.7" fill="#4a7bd0" fill-opacity="0.7"/><rect x="559.3" y="106.7" width="6.6" height="153.3" fill="#4a7bd0" fill-opacity="0.7"/><rect x="573.7" y="152.8" width="6.6" height="107.2" fill="#4a7bd0" fill-opacity="0.7"/><rect x="588.0" y="163.0" width="6.6" height="97.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="602.4" y="186.7" width="6.6" height="73.3" fill="#4a7bd0" fill-opacity="0.7"/><rect x="616.8" y="208.7" width="6.6" height="51.3" fill="#4a7bd0" fill-opacity="0.7"/><rect x="631.2" y="190.0" width="6.6" height="70.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="645.6" y="176.0" width="6.6" height="84.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="149.1" y="237.9" width="6.6" height="22.1" fill="#e08a3c" fill-opacity="0.7"/><rect x="163.5" y="173.8" width="6.6" height="86.2" fill="#e08a3c" fill-opacity="0.7"/><rect x="177.9" y="173.8" width="6.6" height="86.2" fill="#e08a3c" fill-opacity="0.7"/><rect x="192.3" y="176.0" width="6.6" height="84.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="206.7" y="147.7" width="6.6" height="112.3" fill="#e08a3c" fill-opacity="0.7"/><rect x="221.1" y="140.2" width="6.6" height="119.8" fill="#e08a3c" fill-opacity="0.7"/><rect x="235.5" y="134.8" width="6.6" height="125.2" fill="#e08a3c" fill-opacity="0.7"/><rect x="249.9" y="121.7" width="6.6" height="138.3" fill="#e08a3c" fill-opacity="0.7"/><rect x="264.3" y="116.3" width="6.6" height="143.7" fill="#e08a3c" fill-opacity="0.7"/><rect x="278.7" y="116.0" width="6.6" height="144.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="293.0" y="113.3" width="6.6" height="146.7" fill="#e08a3c" fill-opacity="0.7"/><rect x="307.4" y="117.0" width="6.6" height="143.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="321.8" y="115.0" width="6.6" height="145.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="336.2" y="117.0" width="6.6" height="143.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="350.6" y="113.3" width="6.6" height="146.7" fill="#e08a3c" fill-opacity="0.7"/><rect x="365.0" y="112.7" width="6.6" height="147.3" fill="#e08a3c" fill-opacity="0.7"/><rect x="379.4" y="98.8" width="6.6" height="161.2" fill="#e08a3c" fill-opacity="0.7"/><rect x="393.8" y="110.9" width="6.6" height="149.1" fill="#e08a3c" fill-opacity="0.7"/><rect x="408.2" y="95.7" width="6.6" height="164.3" fill="#e08a3c" fill-opacity="0.7"/><rect x="422.6" y="100.2" width="6.6" height="159.8" fill="#e08a3c" fill-opacity="0.7"/><rect x="437.0" y="92.4" width="6.6" height="167.6" fill="#e08a3c" fill-opacity="0.7"/><rect x="451.3" y="98.2" width="6.6" height="161.8" fill="#e08a3c" fill-opacity="0.7"/><rect x="465.7" y="90.9" width="6.6" height="169.1" fill="#e08a3c" fill-opacity="0.7"/><rect x="480.1" y="64.9" width="6.6" height="195.1" fill="#e08a3c" fill-opacity="0.7"/><rect x="494.5" y="85.1" width="6.6" height="174.9" fill="#e08a3c" fill-opacity="0.7"/><rect x="508.9" y="114.0" width="6.6" height="146.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="523.3" y="90.3" width="6.6" height="169.7" fill="#e08a3c" fill-opacity="0.7"/><rect x="537.7" y="143.3" width="6.6" height="116.7" fill="#e08a3c" fill-opacity="0.7"/><line x1="70" y1="260" x2="660" y2="260" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="260" stroke="currentColor"/><text x="77.2" y="276" font-size="12" text-anchor="middle">-1000</text><text x="149.1" y="276" font-size="12" text-anchor="middle">-750</text><text x="221.1" y="276" font-size="12" text-anchor="middle">-500</text><text x="293.0" y="276" font-size="12" text-anchor="middle">-250</text><text x="365.0" y="276" font-size="12" text-anchor="middle">0</text><text x="437.0" y="276" font-size="12" text-anchor="middle">250</text><text x="508.9" y="276" font-size="12" text-anchor="middle">500</text><text x="580.9" y="276" font-size="12" text-anchor="middle">750</text><text x="652.8" y="276" font-size="12" text-anchor="middle">1000</text><text x="365.0" y="294" font-size="12" text-anchor="middle">타임스탬프 − 진짜 샘플 시각 (µs, 양 끝 칸 = 범위 밖)</text><text x="16" y="150.0" font-size="12" text-anchor="middle" transform="rotate(-90 16 150.0)">샘플 수 (log)</text><rect x="80" y="312" width="12" height="12" fill="#4a7bd0" fill-opacity="0.7"/><text x="98" y="322" font-size="12">DRDY 800 Hz (sd 174 µs)</text><rect x="380" y="312" width="12" height="12" fill="#e08a3c" fill-opacity="0.7"/><text x="398" y="322" font-size="12">FIFO wtm16 800 Hz (sd 312 µs)</text></svg>
```
그림 6 — 같은 실행의 절대 타임스탬프 오차(드라이버 시각 − 진짜 샘플 시각). DRDY는 IRQ 지연만큼 양수 쪽에 몰려 있고, FIFO는 앵커 평활 덕분에 평균이 작다. 음수 쪽 꼬리(추정 시각이 진짜보다 앞섬)는 앵커가 IRQ 지연 평균만큼 늦게 찍힌 구간을 보정하며 생긴 것으로 보인다(이 실행에서 잃은 edge 2회 포함, 원인 분리는 안 했다).

결과에서 읽을 것:

- **FIFO가 지터와 드롭 모두 이긴다.** DRDY는 샘플마다 IRQ → 스레드 → I2C 읽기를 해야 하고, 스레드가 다음 IRQ 전에 끝나지 않으면 그 IRQ가 버려진다(7.3절의 메커니즘) → 드롭. FIFO는 칩이 버퍼링하므로 CPU가 늦어도 샘플이 안 사라지고, 간격은 추정기가 매끈하게 만든다.
- **DRDY의 ts−truth 평균 = IRQ 지연**이다. 50 Hz에서 4 ms라는 큰 값은 최종 실행 당시 VM이 다른 게스트로 바빴던 결과다(저부하 실행에서는 317 µs). 이 숫자는 하드웨어 성능이 아니라 TCG + 공유 VM의 성능이다.
- **hrtimer 폴링은 드롭과 중복을 동시에 만든다.** 트리거 타이머와 칩 발진기가 같은 공칭 주기지만 위상이 독립이라, 한 주기에 두 번 읽으면 같은 샘플(dup), 한 번도 못 읽으면 drop이 된다. 200 Hz에서 `ts−truth`가 0~5 ms(한 주기)에 고르게 퍼지는 것도 같은 이유다 — 타임스탬프는 "읽은 시각"이지 "샘플된 시각"이 아니다. 음수 오차는 트리거 시각을 먼저 찍고 레지스터를 나중에 읽는 사이에 새 샘플이 들어온 경우다. **IRQ 없는 보드에서 hrtimer는 "동작은 하는" 폴백일 뿐**이고, 정확도가 필요하면 IRQ 핀을 배선해야 한다는 근거다.
- **period_est**: 800 Hz에서 1247992 ns(공칭 1250000), 1600 Hz에서 622364 ns(공칭 625000). TCG의 시계 흔들림을 추정기가 그대로 따라간 값이다.

### 7.2 발진기 오차 추적 (clk_ppm=10000)

```sh
TMO=300 tests/qemu/run.sh guest_capture.sh PPM=10000 DRDY_RATES=0 FIFO_RATES=800 HRT_RATES=0
```

(커널 명령줄의 알 수 없는 `KEY=VALUE`는 init의 환경 변수가 된다 — 게스트 스크립트에 값을 넘기는 간단한 방법.)

```text
### emulator clk_ppm=10000
### mode=fifo rate=800 trigger=s4imu-dev0 watermark=16
samples=3200 span=3.959s rate=807.99Hz drops=0 dups=0 gaps=0 nonmono=0
dt-T           n=3199 mean=-12.4 sd=37.6 min=-635.7 max=150.7 (us)
ts-truth       n=3200 mean=177.1 sd=302.5 min=-689.8 max=643.2 (us)
debugfs: overflows=0 resyncs=0 lost_edges=6 period_est=1237129ns
```

칩이 1 % 빠르면 실제 주기는 1250000 / 1.01 = 1237624 ns이고 공칭과의 차이는 −12.4 µs다. `dt-T mean=-12.4`가 정확히 그 값이고, 추정기 `period_est=1237129`는 진짜 주기와 0.04 % 차이다. Q5 4.2의 방법 B(공칭 주기로 채우기)였다면 배치 앞쪽 프레임이 15 × 12.4 = 186 µs씩 틀렸을 것이다.

### 7.3 버그 1 — 잃어버린 edge가 스트림을 멈춘다

첫 캡처 매트릭스는 FIFO 1600 Hz에서 멈췄다.

```text
### mode=fifo rate=800 trigger=s4imu-dev0 watermark=16
samples=3200 span=3.999s rate=799.98Hz drops=0 dups=0 gaps=1 nonmono=0
...
### mode=fifo rate=1600 trigger=s4imu-dev0 watermark=16
qemu-system-aarch64: terminating on signal 15 from pid 2780624 (timeout)
```

원인은 IIO core 코드에 있다(`drivers/iio/industrialio-trigger.c`, 6.18.55):

```c
void iio_trigger_poll(struct iio_trigger *trig)
{
	int i;

	if (!atomic_read(&trig->use_count)) {
		atomic_set(&trig->use_count, CONFIG_IIO_CONSUMERS_PER_TRIGGER);

		for (i = 0; i < CONFIG_IIO_CONSUMERS_PER_TRIGGER; i++) {
			if (trig->subirqs[i].enabled)
				generic_handle_irq(trig->subirq_base + i);
			else
				iio_trigger_notify_done_atomic(trig);
		}
	}
}
```

이전 배치의 스레드가 아직 `iio_trigger_notify_done()`을 안 불렀으면(`use_count != 0`) 새 IRQ는 **조용히 버려진다**. DRDY(펄스) 모드에서는 그 샘플 하나가 드롭될 뿐이지만(7.1의 DRDY drops가 바로 이것), WTM은 **래치**라서: 스레드가 INT_STATUS를 읽어 래치를 푼 뒤 → notify_done 전에 다음 W개가 차서 WTM이 다시 래치되고 IRQ가 올라옴 → 버려짐 → 래치는 INT_STATUS를 읽을 때까지 안 풀림 → 그런데 INT_STATUS는 다음 IRQ의 핸들러만 읽음 → **영원히 멈춤**. 1600 Hz에서 배치 간격(10 ms)이 TCG의 핸들러 시간에 가까워지며 드러났다.

고친 방법: 트리거의 `.reenable` 콜백. `iio_trigger_notify_done()`이 `use_count`를 0으로 만들 때 이것을 동기적으로(스레드 컨텍스트에서) 부른다. 여기서 INT_STATUS의 WTM 비트를 보고 켜져 있으면 바로 drain한다. 그 배치에는 IRQ 시각이 없으니 `s4imu_ts_batch_no_irq()`로 `anchor + n × period` 외삽하고, 다음 진짜 IRQ에서 `pending` 프레임 수까지 합쳐 주기를 잰다.

```c
/* called by iio_trigger_notify_done() when the last consumer is done */
static void s4imu_trig_reenable(struct iio_trigger *trig)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct s4imu_data *st = iio_priv(indio_dev);

	if (READ_ONCE(st->fifo_mode))
		s4imu_fifo_drain(indio_dev, 0);
}
```

수정 후 같은 1600 Hz가 `drops=0`으로 끝나고 debugfs `lost_edges`가 1~26을 보고한다(실행마다 다름). 이 카운터가 0이 아니라는 것 자체가 "이 버그가 실제로 일어나는 조건"이라는 증거다. 실제 칩에서도 같은 일이 생긴다: 래치형 인터럽트 + edge IRQ + 처리 중 무시하는 상위 계층의 조합. upstream FIFO IMU 드라이버(st_lsm6dsx, inv_icm42600)가 IIO trigger 대신 `request_threaded_irq()` + `IRQF_ONESHOT`으로 FIFO를 직접 읽는 이유 중 하나로 볼 수 있다(그들의 설계 의도를 문서로 확인한 것은 아니다).

### 7.4 버그 2 — overflow 뒤의 타임스탬프 20 ms 오차

watermark 48(FIFO 여유 16 프레임 = 800 Hz에서 20 ms)로 돌렸을 때 TCG 정체 한 번에 overflow가 났다:

```text
### mode=fifo rate=800 trigger=s4imu-dev0 watermark=48
samples=3200 span=4.018s rate=796.12Hz drops=16 dups=0 gaps=1 nonmono=0
dt-T           n=3199 mean=6.1 sd=360.7 min=-508.4 max=20387.4 (us)
ts-truth       n=3200 mean=547.5 sd=2786.2 min=-365.7 max=20021.7 (us)
debugfs: overflows=1 resyncs=0 lost_edges=1 period_est=1249965ns
```

드롭 16개는 정상이지만(칩이 버렸다), `ts−truth max = 20 ms`는 드라이버 잘못이다. 그때 코드는 overflow를 보면 추정기를 리셋하고 IRQ 시각으로 다시 앵커했다. 그런데 이 칩은 **최신 프레임을 버린다** — FIFO 안의 64개는 직전 배치 바로 다음 프레임들이고, IRQ 시각은 버려진 16개 뒤의 시각이다. IRQ에 앵커하면 64개 전부가 16 × 1.25 ms = 20 ms 늦게 찍힌다. 수정: overflow 배치는 직전 체인에서 외삽하고, **그 다음** IRQ에서 새로 앵커(5.2절 코드의 `ovr` 분기). drop-oldest 칩이었다면 반대로 IRQ 앵커가 맞다 — 칩의 overflow 정책이 타임스탬프 알고리즘을 결정한다. 데이터시트에서 반드시 확인할 줄이다(P3 1절).

결정적으로 재현하려고 가짜 칩에 `inject_stall_ms`를 넣었다(1 s 뒤 200 ms 동안 IRQ 정지):

```sh
TMO=300 tests/qemu/run.sh guest_capture.sh STALL=200 WTM=16 DRDY_RATES=0 FIFO_RATES=800 HRT_RATES=0
```

```text
### mode=fifo rate=800 trigger=s4imu-dev0 watermark=16
samples=3200 span=4.126s rate=775.35Hz drops=102 dups=0 gaps=1 nonmono=0
dt-T           n=3199 mean=39.7 sd=2261.1 min=-629.1 max=127887.1 (us)
ts-truth       n=3200 mean=157.7 sd=272.9 min=-768.5 max=667.7 (us)
samples 3446 irqs 206 fifo_overflow_frames 102 
debugfs: overflows=1 resyncs=0 lost_edges=8 period_est=1249178ns
```

200 ms 정지 동안 160개가 만들어지고 64개가 FIFO에 남아 102개가 버려졌다(가짜 칩 `fifo_overflow_frames 102`와 도구의 seq 기반 `drops=102`가 **정확히 일치** — 측정 도구와 하네스가 서로를 검증한다). 간격에는 128 ms 구멍이 정직하게 보이고(`dt-T max`), 살아남은 프레임의 절대 오차는 최대 0.67 ms에 머문다.

### 7.5 버그 3 — IIO core demux가 타임스탬프를 엉뚱한 곳에서 복사한다

처음 버전은 "칩이 항상 14바이트 전체를 주니까" `available_scan_masks = { 0x7f, 0 }`을 선언하고 일부 채널만 켠 경우는 IIO core의 demux에 맡겼다(upstream 여러 드라이버가 쓰는 방식). 9절에서 libiio로 `accel_x accel_z timestamp`만 캡처했더니 시각 간격이 4.29 s(= 2^32 ns 근처)로 나왔다. 커널 자체를 의심해 sysfs로 같은 세 채널만 켜고 `/dev/iio:device0`을 직접 덤프했다:

```sh
TMO=200 tests/qemu/run.sh guest_demux.sh
```

```text
enabled: in_accel_x in_accel_z in_timestamp 
index/type: x 0 z 2 ts 7 le:s64/64>>0
00000000  d6 1a 0d 40 00 00 00 00  00 00 a4 02 10 00 00 00  |...@............|
00000010  fc 1b fe 3f 00 00 00 00  00 00 cf 02 11 00 00 00  |...?............|
00000020  c2 1c fe 3f 00 00 00 00  00 00 fb 02 12 00 00 00  |...?............|
00000030  ab 1d f9 3f 00 00 00 00  00 00 25 03 13 00 00 00  |...?......%.....|
```

16바이트 scan의 8–15(타임스탬프 자리)에 `00 00 | a4 02 | 10 00 | 00 00`가 들어 있다 — 전체 scan의 gyro_y(0), gyro_z(0x02a4 = 676), **temp = seq 0x10, 0x11, 0x12, 0x13**(seq_in_temp), 패딩이다. 즉 전체 24바이트 scan의 **offset 8**을 타임스탬프로 복사했다. 진짜 타임스탬프는 offset 16에 있다(`iio_push_to_buffers_with_ts()`가 `scan_bytes / 8 − 1`번째 s64에 넣는다).

`iio_buffer_update_demux()`(industrialio-buffer.c, 6.18.55)를 읽으면 이유가 보인다: 요청 채널을 도는 루프는 마지막 요청 채널(z, index 2)에서 끝나고, 그 뒤 활성(active) 채널 3–6을 `in_loc`에 더하지 않은 채 "Relies on scan_timestamp being last" 블록이 `in_loc = roundup(6, 8) = 8`에서 타임스탬프를 복사한다. 활성 마스크의 꼬리 채널이 꺼져 있고 타임스탬프가 켜진 경우에 생기는 문제다. **최신 mainline에서 고쳐졌는지는 확인하지 않았다** — 업스트림에 보고하기 전에 할 일은 mainline 코드 확인과 `iio_dummy` 같은 in-tree 드라이버로 재현하는 것이다(P7).

드라이버 쪽 해결은 단순하다: `available_scan_masks`를 없애고 활성 채널만 직접 채운다(upstream bmi160이 `iio_for_each_active_channel`로 하는 방식).

```c
static void s4imu_push_frame(struct iio_dev *indio_dev, const u8 *frame, s64 ts)
{
	struct s4imu_data *st = iio_priv(indio_dev);
	unsigned int bit, j = 0;

	iio_for_each_active_channel(indio_dev, bit)
		memcpy(&st->scan.ch[j++], frame + 2 * bit, sizeof(__le16));
	iio_push_to_buffers_with_ts(indio_dev, &st->scan, sizeof(st->scan), ts);
}
```

수정 후 같은 덤프:

```text
00000000  d6 1a 0d 40 00 00 00 00  f0 a5 d3 7d 00 00 00 00  |...@.......}....|
00000010  fc 1b 07 40 00 00 00 00  f0 10 68 7e 00 00 00 00  |...@......h~....|
00000020  c2 1c f1 3f 00 00 00 00  30 c1 01 7f 00 00 00 00  |...?....0.......|
00000030  ab 1d 00 40 00 00 00 00  a0 d8 99 7f 00 00 00 00  |...@............|
```

타임스탬프 0x7dd3a5f0 → 0x7e6810f0, 차이 9728768 ns ≈ 10 ms(100 Hz 기본 ODR). 이 시나리오는 `ci.sh`의 회귀 테스트가 됐다.

**이 절의 교훈(면접용)**: 세 버그 모두 "정상 경로"에서는 안 보였다. 실측 도구가 **시퀀스 번호와 ground truth**를 갖고 있었기 때문에 증상(멈춤, 20 ms 오차, 4.29 s 간격)을 숫자로 잡았고, 원인을 커널 소스 줄까지 따라갔다.

---

## 8. 단계 7 — 폴백과 실패 경로: DT 없이, IRQ 없이, 칩이 틀려도

```sh
NO_OVERLAY=1 TMO=300 tests/qemu/run.sh guest_fallback.sh
```

`standalone=1`이면 가짜 칩이 DT 없이 adapter만 만들고, 클라이언트는 `new_device`로 만든다 — 이 경우 `client->irq = 0`이다(H5: `new_device`는 이름·주소만 받는다).

```text
### adapter i2c-0, no clients yet: i2c-0 
s4imu-emul s4imu-emul: emulated S4IMU at 0x68 on i2c-0, sim irq 22
s4imu_i2c 0-0068: supply vdd not found, using dummy regulator
s4imu_i2c 0-0068: supply vddio not found, using dummy regulator
s4imu_i2c 0-0068: mounting matrix not found: using identity...
s4imu_i2c 0-0068: no IRQ: attach a software trigger (iio-trig-hrtimer)
i2c i2c-0: new_device: Instantiated device s4imu at 0x68
iio:device0 
### device trigger present? none
no device trigger (no IRQ?)
### mode=hrtimer rate=200 trigger=s4hrt watermark=1
samples=1000 span=5.065s rate=197.22Hz drops=220 dups=206 gaps=12 nonmono=0
...
### probe error path 1: wrong WHO_AM_I
s4imu_i2c 0-0068: error -ENODEV: unexpected WHO_AM_I 0x33
bound driver: none
### probe error path 2: nothing at 0x69
s4imu_i2c 0-0069: error -ENXIO: soft reset failed
```

(콘솔과 dmesg가 같은 줄을 두 번 찍은 부분은 하나만 남겼다.) 볼 것: IRQ가 없으면 드라이버는 자기 트리거를 만들지 않고 안내 메시지를 남긴 뒤, `drdy` 요청은 스크립트가 "no device trigger"로 거절하고 hrtimer 트리거로는 정상 캡처된다. 잘못된 ID는 `-ENODEV`, 없는 주소는 첫 쓰기(soft reset)에서 NACK → `-ENXIO`. 두 경우 모두 `dev_err_probe()`가 에러 코드 이름까지 찍고, devm 덕분에 이미 켠 regulator 등은 자동으로 정리된다. 실패한 probe 뒤에 드라이버가 붙어 있지 않다(`bound driver: none`).

---

## 9. 단계 8 — 진짜 유저 공간: udev 자동 로드와 libiio

BusyBox에는 udev도 libiio도 없다. `rootfs-dom`(Ubuntu 24.04, systemd, libiio-utils 0.25) qcow2 overlay로 부팅하고, 산출물은 `mkfs.ext4 -d`로 만든 두 번째 디스크로 넣었다(`tests/qemu/run_rootfs.py`, pexpect). 게스트 셸까지 35 s 걸렸다(`apparmor=0`).

```text
### kernel 6.18.55-dom, libiio-utils	0.25-4build2
i2c:s4imu
of:N*T*Cs4lab,s4imuC*
of:N*T*Cs4lab,s4imu
of:N*T*Cs4lab,s4imu-i2c-emulC*
of:N*T*Cs4lab,s4imu-i2c-emul
of:Ni2c-emulT(null)Cs4lab,s4imu-i2c-emul

### udev coldplug -> modprobe by modalias
s4imu_i2c 0 
s4imu_core 2 s4imu_i2c
s4imu_emul 0 
```

`.ko`를 `/lib/modules/$(uname -r)/updates/s4imu/`에 넣고 `depmod -a` → `udevadm trigger --action=add`만 했는데, 플랫폼 장치의 modalias `of:Ni2c-emulT(null)Cs4lab,s4imu-i2c-emul`이 `s4imu_emul`의 alias 패턴에 맞아 로드되고 → adapter가 IMU 클라이언트를 만들고 → 그 uevent가 `s4imu_i2c`를 → depmod가 기록한 의존성으로 `s4imu_core`까지 로드했다. `MODULE_DEVICE_TABLE(of, ...)` 한 줄이 이 체인을 만든다(G9). `/etc/modprobe.d/s4imu.conf`의 `options s4imu_emul seq_in_temp=1`도 자동 로드에 적용됐다.

```text
### iio_info (excerpt)
	iio:device0: s4imu (buffer capable)
		9 channels found:
			accel_x:  (input, index: 0, format: le:S16/16>>0)
			3 channel-specific attributes found:
				attr  0: raw value: 7948
				attr  1: scale value: 0.000598550
				attr  2: scale_available value: 0.000598550 0.001197101 0.002394202 0.004788403
...
			timestamp:  (input, index: 7, format: le:S64/64>>0)
			mount:  (input, WARN:iio_channel_get_type()=UNKNOWN)
			1 channel-specific attributes found:
### iio_attr
16388
400.000000
```

libiio 0.25가 `in_mount_matrix`를 "mount"라는 채널로 오인해 경고를 낸다(채널 9개로 셈) — libiio 쪽 해석 문제이고 커널 ABI는 표준 이름이다. `iio_attr -c s4imu accel_z raw`, `iio_attr -d s4imu sampling_frequency 400`이 동작한다.

```text
### iio_readdev: device trigger, 3 of 7 channels
sample rate not set : Permission denied (13)
WARNING: High-speed mode not enabled
-rw-r--r-- 1 root root 128 Oct 19 01:51 /tmp/s.bin
after iio_readdev: buffer/length=16 buffer/watermark=4
accel_x=  8112 accel_z= 16378 ts=51639217424 dt_us=0.0
accel_x=  8130 accel_z= 16390 ts=51641717424 dt_us=2500.0
accel_x=  8147 accel_z= 16397 ts=51644217424 dt_us=2500.0
accel_x=  8160 accel_z= 16376 ts=51646717424 dt_us=2500.0
accel_x=  8172 accel_z= 16381 ts=51648664352 dt_us=1946.9
accel_x=  8180 accel_z= 16388 ts=51651145280 dt_us=2480.9
accel_x=  8187 accel_z= 16368 ts=51653626208 dt_us=2480.9
accel_x=  8190 accel_z= 16382 ts=51656107136 dt_us=2480.9
```

(hexdump 4줄은 생략. 디코드는 게스트의 python3 `struct.unpack_from("<hh4xq", ...)`.) 두 가지가 보인다:

- `iio_readdev -t s4imu-dev0 -s 8 -b 4`가 **buffer/watermark를 4로 써서** 드라이버가 FIFO 모드로 들어갔다. 4개씩 정확히 2500.0 µs 간격(첫 배치는 공칭 주기로 back-fill), 다음 배치는 측정 주기 2480.9 µs로 바뀐다 — 추정기가 일하는 모습이 그대로 보인다. "sample rate not set: Permission denied"는 libiio가 트리거의 `sampling_frequency`를 쓰려다 실패한 것이다(우리 트리거엔 그 속성이 없다). Q5 3.4절이 경고한 "도구가 측정 대상을 바꾼다"의 또 다른 사례다.
- 7.5절 수정 전에는 같은 명령이 `dt_us=4295622.7`처럼 나왔다(core demux 문제). 수정 후 정상.

같은 이미지에서 `imu_jitter`(FIFO W16, 400 Hz): `drops=0 dups=0 gaps=0 nonmono=0`, `dt-T sd=36.5 µs`.

---

## 10. 실제 하드웨어로 옮기기 (가이드 — 실행 안 함)

QEMU에서 증명한 것은 드라이버 **논리**다. 실제 보드(S3의 Raspberry Pi 5 / BeaglePlay)로 옮길 때 바뀌는 것:

| 항목 | 이 저장소 (QEMU) | 실제 보드 |
|---|---|---|
| 칩 | 가상의 S4IMU | BMI160/BMI270, ICM-42688-P, LSM6DSO 등: 데이터시트로 레지스터 맵을 새로 쓰거나 upstream 드라이버(bmi160, inv_icm42600, st_lsm6dsx)를 쓴다 (P3 2절) |
| 버스 | 가짜 adapter, 전송 길이 무제한 | SoC I2C: `i2c_adapter_quirks`의 최대 길이, 400 kHz 필요 여부(1.6 kHz × 14 B ≈ 22 kB/s) |
| DT 적용 | 빌드 때 fdtoverlay | RPi: `config.txt`의 `dtoverlay=`, BeaglePlay: U-Boot overlay (S3, D6) |
| IRQ | irq_sim, edge, 극성 없음 | GPIO irqchip: `interrupts` 플래그와 INT_CTRL 극성 비트를 일치, 핀 pull 설정(H4), level이면 threaded + ONESHOT |
| 전원 | dummy regulator | `vdd-supply`/`vddio-supply`를 회로도대로 (P4), 데이터시트 power-up 지연 |
| PM | suspend 중 레지스터 유지 | 전원을 끊는다면 regcache cache_only/mark_dirty/sync (H2 9절) |
| 시간 | TCG: IRQ 지연 수백 µs | 수십 µs 예상 — `imu_jitter`로 다시 재서 비교 |

RPi 예시 overlay `dts/s4imu-rpi-i2c1.dtso`(I2C1 = GPIO2/3, INT = GPIO17)는 dtc로 컴파일만 확인했다. `&i2c1`, `&gpio`, `&vdd_3v3_reg` 라벨이 실제 보드 DT에 있는지 먼저 확인해야 한다. 벤치 bring-up 순서: `i2cdetect -y 1`(ACK) → `i2ctransfer`로 WHO_AM_I → 스코프로 INT 핀을 보며 DRDY 켜기 → 드라이버 로드 → `/proc/interrupts` 증가 → `imu_jitter`. Don이 RF bring-up에서 하던 "버스 → 레지스터 → 인터럽트 → 데이터" 순서와 같다.

---

## 11. CI — 한 번에 다 돌리기

```sh
cd /var/tmp/S4/repo && time DTSCHEMA=/var/tmp/S4/venv/bin ci/ci.sh 2>&1 | tail -20
```

```text
build W=1 (0 warnings)            PASS
checkpatch --strict               PASS
dtc overlays (-@)                 PASS
tools -Wall -Wextra               PASS
dt-doc-validate binding           PASS
dt-validate merged DTB            PASS
KUnit (QEMU)                      PASS
smoke: probe/sysfs/PM             PASS
demux: subset + timestamp         PASS
capture: no drops in FIFO         PASS
fallback: new_device+hrtimer      PASS
CI: ALL PASS

real	1m2.085s
```

(이 실행 뒤 `clean` 단계를 맨 앞에 추가했다 — 이미 빌드된 트리에서는 W=1 단계가 아무것도 컴파일하지 않아 경고 0이 "공짜로" 나오기 때문이다.) GitHub Actions에는 랩 VM이 없으므로 `.github/workflows/ci.yml`은 배포판 헤더로 W=1 빌드·dtbo·도구만 하는 **가이드**로 남겼다(실행 안 함). QEMU 단계까지 하려면 self-hosted runner나 커널 빌드 캐시가 필요하다(P6).

---

## 12. 커널 안에서는 무슨 일이 일어나나

샘플 하나가 칩에서 유저 버퍼까지 가는 경로를 함수 이름으로 따라가면:

1. 가짜 칩 `emul_tick()`(hrtimer hard IRQ) → `emul_sample()` → FIFO push, WTM 래치 → `irq_set_irqchip_state(PENDING)` → `irq_work_queue()`.
2. irq_work → `irq_sim_handle_irq()` → `generic_handle_domain_irq()` → 드라이버가 `devm_request_irq`로 건 `iio_trigger_generic_data_rdy_poll()` → `iio_trigger_poll()`(use_count 검사!) → 트리거의 sub-IRQ → pollfunc top half `iio_pollfunc_store_time()`이 `pf->timestamp = iio_get_time_ns()`.
3. pollfunc 스레드(IRQ 스레드, SCHED_FIFO) → `s4imu_trigger_handler()` → `s4imu_fifo_drain()` → `regmap_bulk_read()`/`regmap_noinc_read()` → `regmap_i2c` → `i2c_transfer()` → `emul_xfer()`.
4. `s4imu_push_frame()` → `iio_push_to_buffers_with_ts()` → (demux가 있으면 `iio_demux()`) → `iio_push_to_buffer()` → kfifo 저장 → watermark 이상이면 `wake_up_interruptible_poll()`.
5. `iio_trigger_notify_done()` → `use_count` 0 → `.reenable`(`s4imu_trig_reenable`).
6. 유저 `read(/dev/iio:device0)` → `iio_buffer_read()`가 watermark만큼 찰 때까지 대기 → kfifo에서 복사.

드라이버 작성 때 다시 만나는 지점: 2번의 hard IRQ에서는 잠들 수 없고(I2C 불가), 3번 스레드에서만 버스를 만진다(F2·F6·F7). 4번의 scan 레이아웃(정렬, 타임스탬프 위치)은 I2 4절. 5번 reenable은 IIO 고유의 "트리거 재무장" 훅. runtime PM은 1번 앞 단계(`preenable` → `pm_runtime_resume_and_get` → `s4imu_runtime_resume` → PWR normal)에서 칩을 깨운다(N6).

---

## 13. Embedded 실무에서 다시 보기

- **로봇 IMU 노드**: ROS 2 `sensor_msgs/Imu`를 내는 데몬은 `imu_jitter`의 read 루프와 같은 구조다. `header.stamp`에는 반드시 scan 안의 타임스탬프(monotonic)를 넣고, `read()` 시각을 쓰지 않는다(Q5 4.3, Q6). 드롭은 시퀀스나 간격으로 감시해 health 토픽으로 낸다(Q7).
- **IRQ 핀이 없는 하네스 보드**: 기구 제약으로 INT 핀을 안 뽑은 보드가 실제로 있다. 7.1의 hrtimer 결과(drop + dup, 0~1 주기 오차)는 "INT 핀 하나 배선하는 비용 vs 데이터 품질" 논의의 정량 근거가 된다.
- **FIFO watermark 고르기**: 크면 IRQ·전력이 줄지만 지연(read_latency)이 늘고 overflow 여유가 준다. 7.4의 W48 사례처럼 "watermark + 최악 지연 × ODR < depth"를 검사한다. 실제 칩이 `hwfifo_watermark_max`를 알려 주면 그 ABI로 노출한다.
- **양산 회귀 테스트**: 가짜 칩 하네스는 하드웨어 랩 없이 CI에서 도는 드라이버 회귀 테스트다. 실제 칩용 드라이버라도 같은 방식으로 레지스터 모델을 만들어 두면, 커널 업그레이드(P5) 때 1분 만에 확인할 수 있다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| INT_STATUS를 precious로 안 둠 | debugfs 덤프 뒤 스트림 멈춤 | 덤프가 래치를 지움 | precious_table에 넣기 (H2 7절) |
| FIFO_DATA를 noinc로 안 둠 | FIFO 데이터가 뒤죽박죽 | regmap이 주소를 증가시키며 읽음 | `readable_noinc_reg` + `regmap_noinc_read` |
| 래치형 WTM + IIO trigger를 그대로 씀 | 고속에서 가끔 영원히 멈춤 | `iio_trigger_poll`이 처리 중 IRQ를 버림 | `.reenable`에서 상태 확인 후 drain (7.3) |
| overflow 때 IRQ 시각으로 재앵커 | 배치 전체가 수~수십 ms 틀림 | drop-newest 정책 무시 | 칩 정책에 맞춰 외삽 (7.4) |
| `available_scan_masks` + 일부 채널 + 타임스탬프 | 타임스탬프가 쓰레기 | 6.18.55 demux의 in_loc (7.5) | 드라이버가 활성 채널만 직접 채움 |
| 타임스탬프 시계 기본값(realtime) 사용 | NTP/PTP 보정 때 시각 점프 | `current_timestamp_clock` 미설정 | monotonic으로 (I2 10절) |
| scale을 MICRO로 반환 | 가속도 0.05 % 오차 | 0.000598550의 하위 자릿수 손실 | `IIO_VAL_INT_PLUS_NANO` + `write_raw_get_fmt` |
| hard IRQ에서 I2C 읽기 | "sleeping function called from invalid context" | I2C는 잠든다 | 시각만 hard IRQ, 읽기는 스레드 |
| 32비트에서 s64 `/` | 링크 에러 `__aeabi_ldivmod` | 64비트 나눗셈 | `div_s64()` |
| 테스트 모델이 드라이버 표를 공유 | 표가 틀려도 테스트 통과 | 오라클이 피검체에 의존 | 가짜 칩에 독립 표 (4.2) |

---

## 15. 면접에서 이렇게 말한다 — 그리고 GitHub에 이렇게 올린다

### 15.1 Q&A

**Q.** Walk me through a sensor driver you wrote.

**A.** I2C IMU를 IIO 드라이버로 처음부터 썼다. regmap으로 칩 성격(volatile/precious/noinc)을 표로 적고, 채널·scale·ODR을 sysfs로, DRDY IRQ 트리거와 FIFO watermark 모드로 버퍼를 냈다. 하드웨어가 없어서 I2C adapter + irq_sim + hrtimer로 가짜 칩을 커널 모듈로 만들고 진짜 샘플 시각을 debugfs로 내보내 지터·드롭·타임스탬프 오차를 쟀다. FIFO가 간격 지터를 DRDY 대비 한 자릿수 이상 줄였다.

> I wrote an IIO driver for an I2C 6-axis IMU from scratch: regmap with volatile, precious and no-increment registers, standard channels with scale and sampling_frequency, a data-ready trigger and a FIFO watermark mode with per-sample timestamp estimation, runtime PM, devm and dev_err_probe throughout. Since I had no hardware, I wrote an in-kernel chip model — an I2C adapter that's also an irq_sim interrupt controller, clocked by an hrtimer — that exposes ground-truth sample times. With that I measured jitter, drops and absolute timestamp error, and found three real bugs.

**꼬리 질문**: "그 세 버그가 뭐였나?" → 7.3–7.5를 30초씩.

**Q.** How do you timestamp samples that come out of a hardware FIFO?

**A.** IRQ 시각 하나가 배치의 마지막 샘플에 대응한다. 주기는 IRQ 간격 ÷ 프레임 수로 추정하고(±20 % 밖은 버림), 앵커는 예측값을 1/8씩만 측정값으로 당긴다. 인과성(IRQ보다 미래 금지)과 단조성 규칙을 두고, 잃은 edge·overflow 때는 외삽한다. 1 % 빠른 발진기에서 추정 주기가 진짜와 0.04 % 차이였다.

> One interrupt timestamp per batch belongs to the newest frame. I estimate the real period from interrupt interval over frame count, rejecting outliers, and move the anchor only an eighth of the way toward each new measurement, like a simple PLL. Then I enforce causality and monotonicity and extrapolate across lost edges or overflow. With a 1 % fast oscillator the estimate landed within 0.04 % of the true period.

**Q.** Your IRQ handler — what runs in hard IRQ context and what doesn't?

**A.** hard IRQ에서는 트리거 poll과 타임스탬프만(`iio_pollfunc_store_time`). I2C는 잠들 수 있어서 pollfunc 스레드에서 읽는다. 처리 중에 다음 IRQ가 오면 IIO core가 버리는데, 래치형 인터럽트에서는 그게 영구 정지가 되므로 `.reenable`에서 상태를 다시 본다.

> Only the timestamp and the trigger poll run in hard IRQ context; bus I/O happens in the poll function's thread because I2C can sleep. IIO drops a trigger that fires while the previous one is still being handled, and with a latched watermark interrupt that can stall the stream forever — so the trigger's reenable callback re-checks the status register and drains if needed.

**Q.** How did you test without hardware, and why not i2c-stub?

**A.** i2c-stub은 값이 안 변하고 IRQ가 없다. 그래서 레지스터 의미(clear-on-read, FIFO pop)를 구현한 adapter, 자기 DT 노드를 fwnode로 하는 irq_sim 도메인, 이상적 발진기 시각을 기록하는 hrtimer를 가진 테스트 모듈을 만들었다. 드라이버는 수정 없이 DT로 probe한다. 정적 검사(W=1, checkpatch, dt-validate)와 KUnit, QEMU 시나리오를 `ci.sh` 하나로 돌린다.

> i2c-stub only stores bytes — no changing data, no interrupt. My test module is an I2C adapter that implements the register semantics, plus an irq_sim domain registered on its own DT node so the IMU's interrupts property resolves normally, plus an hrtimer acting as the sensor oscillator that logs ideal sample times. The driver probes from DT unmodified. One script runs W=1, checkpatch, dt-validate, KUnit and the QEMU scenarios.

**Q.** What changes when you move this to a real board?

**A.** 레지스터 맵(데이터시트), DT overlay 적용 방식, IRQ 극성·level/edge와 핀 pull, 실제 레귤레이터와 power-up 지연, 전원 차단 시 regcache 동기화, 버스 전송 길이 제한. 그리고 지터를 다시 잰다 — TCG 숫자는 의미가 없다.

> The register map from the datasheet, how the overlay gets applied, interrupt polarity and edge versus level plus pin bias, real supplies and power-up timing, regcache sync if the rail is cut in suspend, and the controller's transfer limits. Then re-measure — emulator timings aren't hardware numbers.

**Q.** What's runtime PM doing in your driver?

**A.** 2 s autosuspend. sysfs 읽기는 resume_and_get/put_autosuspend로 감싸고, 버퍼는 preenable/postdisable에서 참조를 잡는다. resume에서 PWR normal + start-up + 첫 DRDY 대기. 가짜 칩 샘플 카운터가 멈추는 것으로 실제 suspend를 확인했다.

> Two-second autosuspend: direct reads take a reference with resume_and_get and drop it with put_autosuspend, the buffer holds one from preenable to postdisable, and resume waits for start-up plus the first data-ready. I verified it by watching the emulator's sample counter stop while suspended.

**Q.** Did you find anything in the kernel itself?

**A.** 6.18.55의 IIO demux가 활성 마스크의 꼬리 채널이 꺼져 있을 때 타임스탬프를 잘못된 offset에서 복사하는 것을 hexdump로 확인하고 코드 줄을 짚었다. 드라이버는 활성 채널만 직접 채우도록 바꿨고, mainline 확인과 in-tree 재현이 업스트림 보고 전 다음 단계다.

> Yes — with available_scan_masks covering all channels and only a prefix plus the timestamp enabled, the 6.18.55 demux copies the timestamp from the wrong offset because it doesn't advance past the trailing active channels. I showed it with a raw hexdump and pointed at the line. I worked around it in the driver; next step before reporting is reproducing on mainline with an in-tree driver.

### 15.2 GitHub README 구성 (저장소의 README.md)

1. 한 줄 소개 + "가상의 칩, 학습용"이라는 명시 + 아키텍처 그림(그림 1)
2. Features 목록 (regmap 분류, 채널, 트리거·FIFO, 타임스탬프, PM, devm, binding)
3. **Results 표**(7.1 표의 800/1600 Hz 줄) + 히스토그램 1장 + "QEMU TCG 수치"라는 경고
4. Findings 링크(버그 세 개) — 면접관이 가장 오래 보는 부분
5. Layout, Build & test(`make check`, `ci/ci.sh`), 수동 세션 3줄
6. Real hardware port 링크, License

### 15.3 5분 데모 스크립트

1. (30 s) README 그림 1로 층 구조 설명.
2. (60 s) `tests/qemu/run.sh guest_smoke.sh` — probe 로그, `/proc/interrupts`, raw/scale, runtime PM 전환.
3. (90 s) `guest_capture.sh DRDY_RATES=800 FIFO_RATES=800 HRT_RATES=800` — 세 줄의 drops/sd를 나란히 보여 주고 그림 5.
4. (60 s) `STALL=200` 실행 — 가짜 칩 overflow 102 = 도구 drops 102, ts 오차는 0.7 ms 이내.
5. (60 s) findings.md의 demux hexdump 전/후.

보여 줄 숫자: FIFO 800 Hz drops 0 / sd 41 µs vs DRDY drops 56 / sd 350 µs, 1 % 시계 오차에서 추정 주기 오차 0.04 %, KUnit 7/7, checkpatch 0/0/0, W=1 0 경고. "TCG라 절대값보다 비율을 보라"는 말을 먼저 한다.

---

## 16. 확장 과제와 직접 해보기

1. **SPI glue 추가**: `s4imu_spi.c`에서 `devm_regmap_init_spi()`와 읽기 비트(주소 MSB=1 같은 칩 규칙은 `read_flag_mask`)만 다르게. 가짜 칩은 SPI 컨트롤러로 만들어야 한다. 힌트: `spi_controller`의 `transfer_one`.
2. **HW 타임스탬프 칩 흉내**: 가짜 칩 FIFO 프레임에 24비트 칩 시간 태그를 넣고, 드라이버가 st_lsm6dsx처럼 `ts_gain`으로 변환. IRQ 지연 지터가 사라지는지 `ts-truth`로 확인. 힌트: Q5 4.3.
3. **upstream 방식으로 FIFO 재구성**: IIO trigger 대신 `devm_request_threaded_irq(..., IRQF_ONESHOT)` + `devm_iio_kfifo_buffer_setup()` + `hwfifo_flush_to_buffer`. 7.3 버그가 구조적으로 사라지는지 비교. 정답/힌트: `drivers/iio/imu/inv_icm42600/`.
4. **demux 문제를 in-tree로 재현**: `iio_dummy`(Image-sub/dom에 모듈 있음)에 `available_scan_masks`를 주는 작은 패치로 같은 hexdump를 만들 수 있나? mainline `industrialio-buffer.c`와 비교. 힌트: P7의 패치 제출 흐름.
5. **level IRQ 지원**: irq_sim은 edge만 받는다(`irq_sim_set_type`). gpio-sim 라인(`/sys/bus/gpio/...`)으로 바꾸고 드라이버를 threaded + ONESHOT으로. 힌트: H4, H6.
6. **ftrace로 IRQ → 유저 지연 쪼개기**: `irq_handler_entry`, `sched_wakeup`, regmap 트레이스포인트로 7.1의 405 µs가 어디서 쓰이는지 나눈다. 정답/힌트: S5, H2 10절.
7. **실제 보드**: S3 보드에 BMI160 브레이크아웃을 달고 upstream `bmi160` 드라이버로 같은 `imu_jitter`를 돌려 TCG 숫자와 비교. 10절 표가 체크리스트다.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| IIO | Industrial I/O | ADC·IMU 같은 센서용 커널 프레임워크 (I2) |
| scan | 버퍼 한 샘플 | 활성 채널 + 정렬 패딩 + 타임스탬프 |
| trigger | 샘플링 시점 신호 | DRDY IRQ, hrtimer, sysfs 등 |
| pollfunc | 트리거의 소비자 | top half(시각) + 스레드(읽기) |
| watermark | 깨우는 기준 개수 | `buffer/watermark`, `hwfifo_set_watermark`로 칩에 전달 |
| demux | 채널 재배열 | core가 활성 마스크에서 요청 채널만 골라 복사 |
| regmap | 레지스터 추상화 | 캐시, 분류, bulk/noinc (H2) |
| volatile | 칩이 바꾸는 레지스터 | 캐시하지 않음 |
| precious | 읽으면 부작용 | 덤프·추측 읽기 금지 |
| noinc | 주소 고정 연속 읽기 | FIFO 포트 |
| irq_sim | 가상 IRQ 도메인 | 코드로 PENDING을 세워 IRQ 발생 |
| fdtoverlay | DTB + DTBO 병합 도구 | 부트로더 overlay를 빌드 때 수행 |
| KUnit | 커널 단위 테스트 | 모듈 로드 시 KTAP 결과 |
| KTAP | 테스트 결과 형식 | `ok N name`, CI 파싱용 |
| runtime PM | 장치 단위 절전 | usage count + autosuspend (N6) |
| dev_err_probe | probe 에러 헬퍼 | 에러 이름 출력, defer 사유 기록 |
| ground truth | 진짜 값 | 가짜 칩의 이상적 샘플 시각 |
| drop-newest | overflow 정책 | 가득 차면 새 프레임을 버림 |

---

## 18. 요약 & 체크리스트

가상의 S4IMU 레지스터 맵을 문서로 고정하고, upstream 구조(코어 + I2C glue)의 IIO 드라이버를 썼다: regmap 분류(volatile·precious·noinc + maple 캐시), 표준 채널과 NANO scale, DRDY 트리거·hrtimer 폴백·FIFO watermark 세 경로, 정수 타임스탬프 추정기, runtime PM, devm, dev_err_probe. i2c-stub 대신 I2C adapter + irq_sim + hrtimer 가짜 칩을 모듈로 만들어 DT로 수정 없이 probe시키고, 시퀀스 번호와 진짜 샘플 시각을 debugfs로 내보내 지터·드롭·절대 오차를 쟀다. FIFO 모드가 간격 지터를 DRDY 대비 한 자릿수 이상 줄이고 드롭 0을 냈으며, 하네스가 잃은 edge로 인한 영구 정지, overflow 뒤 20 ms 앵커 오차, 6.18.55 IIO demux의 타임스탬프 offset 문제를 찾아 각각 수정했다. W=1·checkpatch·dt-validate·KUnit 7개·QEMU 시나리오가 `ci.sh` 한 번에 통과한다. 모든 시간 수치는 TCG 값이다.

- [ ] regmap에서 volatile/precious/noinc를 이 칩의 레지스터로 예를 들어 설명할 수 있다
- [ ] IIO 채널 정의(separate/shared_by_type/shared_by_all, scan_type)를 화이트보드에 쓸 수 있다
- [ ] DRDY 트리거의 hard IRQ / 스레드 분업과 그 이유를 말할 수 있다
- [ ] `buffer/watermark`가 `hwfifo_set_watermark`로 칩까지 가는 순서를 설명할 수 있다
- [ ] FIFO 배치 타임스탬프 추정기(주기, 앵커, 인과성, 단조성)를 코드로 쓸 수 있다
- [ ] `iio_trigger_poll`이 IRQ를 버리는 조건과 래치형 인터럽트에서의 결과를 설명할 수 있다
- [ ] overflow 정책(drop-newest/oldest)이 타임스탬프 처리에 주는 영향을 말할 수 있다
- [ ] irq_sim + I2C adapter로 가짜 칩을 만드는 이유와 구조를 설명할 수 있다
- [ ] runtime PM의 get/put 위치와 autosuspend 동작을 sysfs로 보여 줄 수 있다
- [ ] probe 실패 경로를 dev_err_probe + devm으로 정리하는 법을 안다
- [ ] binding YAML을 dt-doc-validate/dt-validate로 검증하고 음성 대조를 할 수 있다
- [ ] 이 저장소를 5분 데모로 보여 주고 숫자 세 개를 말할 수 있다

## 참고 자료

- docs.kernel.org — Industrial I/O: Core elements, Buffers, Triggers (`Documentation/driver-api/iio/`)
- docs.kernel.org — KUnit (`Documentation/dev-tools/kunit/`), Writing DT bindings in YAML (`Documentation/devicetree/bindings/writing-schema.rst`)
- docs.kernel.org — Runtime Power Management Framework for I/O Devices (`Documentation/power/runtime_pm.rst`)
- `Documentation/ABI/testing/sysfs-bus-iio` — `buffer/watermark`, `hwfifo_*`, `current_timestamp_clock`, `mount_matrix`
- 커널 소스(6.18.55, elixir.bootlin.com에서 같은 경로): `drivers/iio/industrialio-buffer.c`(`iio_enable_buffers`, `iio_buffer_update_demux`), `drivers/iio/industrialio-trigger.c`(`iio_trigger_poll`, `iio_trigger_notify_done`), `kernel/irq/irq_sim.c`, `kernel/irq/irqdomain.c`(`irq_domain_translate`), `drivers/iio/imu/bmi160/bmi160_core.c`, `drivers/iio/common/inv_sensors/inv_sensors_timestamp.c`, `drivers/iio/imu/st_lsm6dsx/`, `drivers/base/regmap/`
- libiio 문서 (Analog Devices, `iio_info`/`iio_attr`/`iio_readdev`, 이 랩은 0.25)
- 관련 노트: S1, S2, S3, S5, H2, H4, H5, H6, H9, I2, Q5, Q6, Q7, P3, P4, P5, P6, P7, D6, D7, F2, F4, F6, F7, G3, G9, N6
