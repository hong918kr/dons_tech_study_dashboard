# G10. 펌웨어 로딩 — request_firmware, /lib/firmware, 비동기 로딩, 실패 대처

> **이 노트를 다 읽으면**: `request_firmware` 계열 API(sync·nowait·direct·nowarn·platform)를 상황에 맞게 고르고 `struct firmware`의 수명을 지킬 수 있다 · 커널이 `/lib/firmware` 아래 어느 경로를 어떤 순서로 찾고, 못 찾으면 어떤 fallback을 타는지 소스 기준으로 말할 수 있다 · 헤더(magic·version·length·CRC32)를 검증하고 chunk 단위로 디바이스에 내려보내는 드라이버를 직접 짤 수 있다 · "built-in 드라이버가 rootfs보다 먼저 펌웨어를 찾는" 부팅 race를 진단하고 해결책 4가지를 고를 수 있다
> **왜 필요한가**: Wi-Fi/BT 모듈, 카메라 ISP, DSP/NPU, 터치 컨트롤러, FPGA는 전원을 켜도 ROM 부트로더만 있어서 리눅스가 펌웨어를 올려 줘야 동작한다. 면접에서는 "Your Wi-Fi driver fails with -2 at boot but works after insmod — why?"가 단골이고, 실무에서는 보드 bring-up 첫 주에 `Direct firmware load for ... failed with error -2`를 반드시 본다
> **Don 기준 난이도**: 펌웨어 이미지 포맷·헤더·CRC·다운로드 프로토콜은 SSD·RF 칩에서 이미 했다 / 새로 배울 부분은 "파일을 누가 어디서 찾아서 커널 버퍼로 주나"(firmware_class), 그 경로 순서, 비동기 콜백의 수명 규칙, 그리고 부팅 순서와 파일시스템의 race
> **선행 노트**: G2(디바이스 모델), G3(platform driver·probe·deferred probe), G8(sysfs 속성) — O2(remoteproc)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 컨트롤러 펌웨어를 할 때를 떠올려 보자. 컨트롤러는 전원이 들어오면 **마스크 ROM 부트로더**가 먼저 돌고, 그 ROM이 NOR flash(또는 NAND의 예약 영역)에서 진짜 펌웨어를 읽어 SRAM/DRAM에 올리고 헤더와 CRC를 확인한 뒤 점프했다. 펌웨어 업데이트는 호스트가 NVMe `Firmware Image Download`로 chunk를 보내고 `Firmware Commit`으로 슬롯에 굽는 방식이었다. **펌웨어는 칩 옆 flash에 산다**.

그런데 Linux SoC에 붙는 많은 주변 칩은 **자기 flash가 없다**. 비용·면적을 아끼려고, 또 펌웨어를 OS 배포판과 같이 업데이트하려고, 칩에는 작은 ROM 부트로더와 SRAM만 넣는다. 그러면 전원을 켤 때마다 **호스트(리눅스 드라이버)가 펌웨어 파일을 읽어서 버스로 내려보내야** 칩이 동작한다.

| 장치 | 펌웨어 예 | 내려보내는 버스 | 실제 드라이버 예 |
|---|---|---|---|
| Wi-Fi/BT 콤보 칩 | `brcmfmac43455-sdio.bin` + 보드별 NVRAM `.txt` | SDIO, PCIe, USB | `brcmfmac`, `ath10k`, `iwlwifi` |
| 카메라 ISP / 센서 | ISP 마이크로코드, 튜닝 테이블 | MIPI 옆 I2C, 또는 SoC 내부 | 벤더 ISP 드라이버 |
| DSP / NPU / 보조 코어 | ELF 이미지 (`.elf`, `.mbn`) | SoC 내부 메모리, DMA | remoteproc (O2) |
| 터치 컨트롤러 | 컨트롤러 펌웨어 + 설정 | I2C | `goodix`, `atmel_mxt_ts` |
| FPGA | 비트스트림 | SPI, SoC 내부 FPGA 매니저 | FPGA manager |
| MCU 보조 프로세서 | 모터·전원 관리 MCU 이미지 | UART, SPI, I2C | 보드 전용 드라이버 |

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="40" width="260" height="290" rx="8" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
  <text x="150" y="28" text-anchor="middle" font-size="13" font-weight="bold">Linux SoC (호스트)</text>
  <rect x="40" y="60" width="220" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="150" y="85" text-anchor="middle" font-size="13">/lib/firmware/vendor/chip.bin</text>
  <rect x="40" y="130" width="220" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="155" text-anchor="middle" font-size="13">request_firmware()</text>
  <rect x="40" y="200" width="220" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="225" text-anchor="middle" font-size="13">드라이버: 헤더·CRC 검증</text>
  <rect x="40" y="270" width="220" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="150" y="295" text-anchor="middle" font-size="13">버스로 chunk 다운로드</text>
  <line x1="150" y1="100" x2="150" y2="128" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="150" y1="170" x2="150" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="150" y1="240" x2="150" y2="268" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="260" y1="290" x2="328" y2="290" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="60" x2="340" y2="320" stroke="currentColor" stroke-width="3"/>
  <text x="530" y="28" text-anchor="middle" font-size="13" font-weight="bold">디바이스 (전원 직후 ROM만 있음)</text>
  <rect x="390" y="40" width="270" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="525" y="63" text-anchor="middle" font-size="13">Wi-Fi/BT 칩 — SDIO/PCIe</text>
  <rect x="390" y="88" width="270" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="525" y="111" text-anchor="middle" font-size="13">카메라 ISP — I2C/내부 버스</text>
  <rect x="390" y="136" width="270" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="525" y="159" text-anchor="middle" font-size="13">DSP / NPU — remoteproc (O2)</text>
  <rect x="390" y="184" width="270" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="525" y="207" text-anchor="middle" font-size="13">터치 컨트롤러 — I2C</text>
  <rect x="390" y="232" width="270" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="525" y="255" text-anchor="middle" font-size="13">FPGA 비트스트림 — SPI</text>
  <rect x="390" y="280" width="270" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="525" y="303" text-anchor="middle" font-size="13">MCU 보조 프로세서 — UART/SPI</text>
  <line x1="340" y1="58" x2="388" y2="58" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="106" x2="388" y2="106" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="154" x2="388" y2="154" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="202" x2="388" y2="202" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="250" x2="388" y2="250" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="298" x2="388" y2="298" stroke="currentColor" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — 펌웨어 파일은 호스트의 파일시스템에 있고, 드라이버가 `request_firmware()`로 받아 검증한 뒤 버스로 칩에 내려보낸다. 칩 쪽 ROM 부트로더가 받는 쪽 프로토콜을 정한다.

역할을 나누면 이렇다.

- **firmware_class (커널 공용 코드)**: "이름 → 바이트 덩어리"만 책임진다. 파일을 어느 디렉터리에서 찾을지, 못 찾으면 어떻게 할지, 같은 파일을 동시에 요청하면 합칠지, suspend 때 캐시할지. 파일 내용은 전혀 해석하지 않는다.
- **드라이버**: 받은 바이트의 **의미**를 책임진다. 헤더 파싱, 버전 확인, CRC, 칩 ROM 부트로더와의 다운로드 프로토콜, 부팅 확인, 버전 보고.

Don의 경험으로 번역하면, firmware_class는 "NVMe `Firmware Image Download` 명령이 호스트 메모리에서 바이트를 가져다 주는 부분"이고, 드라이버는 "컨트롤러 펌웨어 쪽의 이미지 검증·슬롯 기록 로직"이다. 이 노트는 두 부분을 다 다룬다: §1~§3, §5~§8은 firmware_class 쪽, §4·§10은 드라이버 쪽.

> 실행 환경: 커널 모듈 실습은 전부 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안의 공용 랩에서 QEMU `virt`(cortex-a57, 512 MB, TCG, 1 CPU)로 부팅한 **게스트 커널 `6.18.55`**(BusyBox initramfs)에서 실제로 돌린 결과다. 커널 소스 인용은 랩의 `/var/lab/src/linux-6.18.55`에서 확인했다. dmesg 타임스탬프·포인터 값은 실행마다 다르고, TCG 에뮬레이션이라 시간 수치는 실제 보드보다 훨씬 느리다. BusyBox `ls`의 색 제어 문자는 붙여 넣을 때 지웠다.

---

## 1. API 한눈에 — 이름을 주면 바이트를 돌려준다

### 1.1 `struct firmware` — 받는 것은 이것 하나

```c
/* include/linux/firmware.h (6.18) */
struct firmware {
	size_t size;
	const u8 *data;

	/* firmware loader private fields */
	void *priv;
};
```

`data`/`size`가 전부다. `data`는 **읽기 전용**(`const u8 *`)이고, 메모리는 firmware_class가 vmalloc 등으로 잡아 둔 것이다. 드라이버는 필요한 만큼 쓰고(칩에 내려보내거나 자기 버퍼에 복사하고) **반드시 `release_firmware()`로 돌려준다**. 펌웨어가 수 MB라면 이 버퍼를 계속 쥐고 있는 것은 메모리 낭비다.

### 1.2 요청 API 비교

| API | 동작 | 못 찾으면 | 언제 쓰나 |
|---|---|---|---|
| `request_firmware(&fw, name, dev)` | 동기. 찾을 때까지 sleep | 경고 로그 + (조건부) sysfs fallback | 기본형. probe에서 필수 펌웨어 |
| `firmware_request_nowarn(&fw, name, dev)` | 동기 | 경고 로그 없이 에러 반환 | 있으면 쓰고 없어도 되는 선택 파일 |
| `request_firmware_direct(&fw, name, dev)` | 동기, 파일시스템만 | 즉시 에러, fallback 절대 안 탐 | 선택 파일 + udev timeout을 피해야 할 때 (CPU microcode 등) |
| `request_firmware_nowait(mod, uevent, name, dev, gfp, ctx, cb)` | 비동기. workqueue에서 찾고 `cb(fw, ctx)` 호출 | `cb(NULL, ctx)` | probe를 막으면 안 될 때, 큰 펌웨어 |
| `firmware_request_platform(&fw, name, dev)` | 동기, 파일 없으면 플랫폼(EFI 내장) 사본 검색 | 에러 | x86 태블릿 터치스크린처럼 UEFI 안에 펌웨어가 박힌 경우 |
| `request_firmware_into_buf(&fw, name, dev, buf, size)` | 동기, 드라이버가 준 버퍼에 직접 읽기 | 에러 | 미리 잡은 DMA 버퍼에 바로 받고 싶을 때 |
| `release_firmware(fw)` | 버퍼 반납 (`NULL`이면 아무것도 안 함) | — | 받은 뒤 항상 |

공통 규칙:

- **sleep 가능한 문맥**에서만 부른다 (프로세스 문맥, probe, workqueue). 인터럽트 핸들러·spinlock 안에서는 안 된다. 파일을 읽기 때문이다.
- `name`은 `/lib/firmware` 기준 **상대 경로**다. `"brcm/brcmfmac43455-sdio.bin"`처럼 하위 디렉터리를 포함할 수 있지만 `..`는 거부된다.
- `dev`는 로그 접두어(`g10_fw g10_fw: ...`), devres 기반 캐시, sysfs fallback 노드의 부모로 쓰인다. 드라이버 소유 `struct device`를 준다.
- 모듈에는 `MODULE_FIRMWARE("name")`을 적는다. 동작은 바꾸지 않지만 `modinfo`에 `firmware:` 줄이 생겨서, initramfs 생성기(dracut, initramfs-tools)와 배포판 패키저가 "이 모듈은 이 파일이 필요하다"를 알 수 있다 (§7).

### 1.3 반환값으로 상황 읽기

| 반환 | 뜻 | 이 노트에서 재현 |
|---|---|---|
| `0` | 찾음, `fw->data` 유효 | 예제 4 |
| `-ENOENT` (-2) | 모든 경로에서 파일 없음 (fallback 안 탐) | 예제 7, 11 |
| `-ETIMEDOUT` (-110) | sysfs fallback으로 갔는데 아무도 안 채워 줘서 timeout | 예제 10 |
| `-EINVAL` | 이름이 비었거나 `..` 포함 | (소스 확인) |
| `-ENOMEM` | 버퍼 할당 실패 | — |
| 드라이버 쪽 `-EINVAL`/`-EBADMSG`/`-EIO` | 파일은 받았지만 내용이 틀림 — 이건 firmware_class가 아니라 **드라이버가** 낸 에러 | 예제 7 |

---

## 2. 실습 준비 — `g10_fw` 드라이버와 펌웨어 이미지

### 2.1 무엇을 만드나

"칩 ROM 부트로더에 펌웨어를 내려보내는" 드라이버를 흉내 낸다. 실제 칩은 없으니 **vmalloc한 16 MiB 버퍼를 디바이스 SRAM(MMIO 창)이라고 친다**. 드라이버가 하는 일은 실제 Wi-Fi·터치 드라이버와 같은 순서다.

1. `request_firmware()`로 파일을 받는다 (모드에 따라 동기/비동기/direct).
2. 헤더를 검증한다: 크기 ≥ 16, magic `G10F`, 헤더의 `len` == 실제 payload 길이, CRC32 일치.
3. payload를 4 KiB chunk로 "디바이스 RAM"에 쓰고 chunk마다 read-back 비교한다.
4. 성공하면 버전을 sysfs `fw_version`으로 보여 준다 (G8). `fw_status`에는 0 또는 음수 errno.

모듈 파라미터: `fw=`(파일 이름, 기본 `g10_dev.bin`), `mode=`(0 동기, 1 nowait+uevent, 2 nowait 무-uevent → sysfs 수동 로딩, 3 direct), `chunk=`(바이트, 기본 4096). DT 없이 시험하려고 모듈이 `platform_device_register_simple("g10_fw", -1, ...)`로 장치까지 직접 만든다 (G3의 패턴).

### 2.2 이미지 포맷 — 16바이트 헤더

```
offset  size  field       note
0x00    4     magic       0x46303147 = "G10F" (little-endian bytes 47 31 30 46)
0x04    2     ver_major
0x06    2     ver_minor
0x08    4     len         payload bytes (file size - 16)
0x0C    4     crc         CRC32 (IEEE, zlib) of payload
0x10    len   payload
```

Don이 SSD에서 쓰던 이미지 헤더(magic·버전·길이·CRC, 그리고 실제 제품에서는 서명)와 같은 모양이다. 드라이버에서 이 검증을 하는 이유는 §4에서 다룬다.

### 2.3 드라이버 코드 (`g10_fw.c`, 5조각)

한 파일이지만 길어서 다섯 조각으로 나눠 보인다. 조각을 이어 붙이면 그대로 빌드되는 파일이다.

무엇을 확인하는 코드인지 (조각 A): 헤더 구조체, 드라이버 상태, 모듈 파라미터.

```c
// SPDX-License-Identifier: GPL-2.0
/* g10_fw.c - firmware loading demo: header check + chunked "download" */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/crc32.h>
#include <linux/vmalloc.h>
#include <linux/ktime.h>
#include <linux/completion.h>

#define G10_MAGIC   0x46303147u   /* "G10F" little-endian */
#define G10_DEV_RAM (16u << 20)   /* simulated device SRAM: 16 MiB */

struct g10_hdr {                  /* 16 bytes, little-endian */
	__le32 magic;
	__le16 ver_major, ver_minor;
	__le32 len;               /* payload bytes after header */
	__le32 crc;               /* CRC32 (zlib) of payload */
} __packed;

struct g10_priv {
	struct device *dev;
	u8 *dev_ram;              /* pretend MMIO window of the chip */
	char version[16];
	int status;               /* 0 = running fw, <0 = errno */
	struct completion fw_done;
	s64 t_req_us, t_chk_us, t_dl_us;
};

static char *fw = "g10_dev.bin";
module_param(fw, charp, 0444);
static int mode;                  /* 0 sync, 1 nowait, 2 nowait no-uevent, 3 direct */
module_param(mode, int, 0444);
static uint chunk = 4096;
module_param(chunk, uint, 0444);
static struct platform_device *g10_pdev;
```

조각 B: 헤더 검증(`g10_validate`)과 chunk 다운로드(`g10_download`). CRC는 커널 `crc32_le(~0, ...) ^ ~0`으로 계산하는데, 이것이 zlib/Python `zlib.crc32()`와 같은 값이다 (초기값 `0xFFFFFFFF`, 마지막 반전 — Don이 펌웨어에서 쓰던 "CRC-32/ISO-HDLC"와 같다).

```c
static int g10_validate(struct g10_priv *p, const struct firmware *f)
{
	const struct g10_hdr *h = (const void *)f->data;
	u32 len, crc;

	if (f->size < sizeof(*h))
		return dev_err(p->dev, "fw too small: %zu\n", f->size), -EINVAL;
	if (le32_to_cpu(h->magic) != G10_MAGIC)
		return dev_err(p->dev, "bad magic 0x%08x\n", le32_to_cpu(h->magic)), -EINVAL;
	len = le32_to_cpu(h->len);
	if (len != f->size - sizeof(*h) || len > G10_DEV_RAM)
		return dev_err(p->dev, "size mismatch: hdr len %u, file payload %zu\n",
			       len, f->size - sizeof(*h)), -EINVAL;
	crc = crc32_le(~0u, f->data + sizeof(*h), len) ^ ~0u;
	if (crc != le32_to_cpu(h->crc))
		return dev_err(p->dev, "CRC mismatch: hdr 0x%08x calc 0x%08x\n",
			       le32_to_cpu(h->crc), crc), -EBADMSG;
	snprintf(p->version, sizeof(p->version), "%u.%u",
		 le16_to_cpu(h->ver_major), le16_to_cpu(h->ver_minor));
	return 0;
}

/* "download": copy payload into device RAM chunk by chunk, verify each chunk */
static int g10_download(struct g10_priv *p, const u8 *src, u32 len)
{
	u32 off, n, nchunks = 0;

	for (off = 0; off < len; off += n, nchunks++) {
		n = min(chunk, len - off);
		memcpy(p->dev_ram + off, src + off, n);          /* write window */
		if (memcmp(p->dev_ram + off, src + off, n))      /* read back */
			return dev_err(p->dev, "verify failed @0x%x\n", off), -EIO;
	}
	dev_info(p->dev, "downloaded %u bytes in %u chunks of %u\n", len, nchunks, chunk);
	return 0;
}
```

조각 C: 검증 → 다운로드를 묶고 시간을 재는 `g10_boot_fw`, 비동기 콜백 `g10_fw_cb`, sysfs 속성 두 개.

```c
static int g10_boot_fw(struct g10_priv *p, const struct firmware *f)
{
	ktime_t t0 = ktime_get();
	int ret = g10_validate(p, f);

	p->t_chk_us = ktime_us_delta(ktime_get(), t0);
	if (ret)
		return ret;
	t0 = ktime_get();
	ret = g10_download(p, f->data + sizeof(struct g10_hdr), f->size - sizeof(struct g10_hdr));
	p->t_dl_us = ktime_us_delta(ktime_get(), t0);
	if (!ret)
		dev_info(p->dev, "fw %s v%s running (req %lld us, crc %lld us, dl %lld us)\n",
			 fw, p->version, p->t_req_us, p->t_chk_us, p->t_dl_us);
	return ret;
}

static void g10_fw_cb(const struct firmware *f, void *ctx)
{
	struct g10_priv *p = ctx;

	dev_info(p->dev, "fw_cb: called, fw=%s\n", f ? "OK" : "NULL");
	p->status = f ? g10_boot_fw(p, f) : -ENOENT;
	release_firmware(f);                 /* NULL is fine */
	complete(&p->fw_done);
}

static ssize_t fw_version_show(struct device *d, struct device_attribute *a, char *buf)
{
	struct g10_priv *p = dev_get_drvdata(d);

	return sysfs_emit(buf, "%s\n", p->status ? "none" : p->version);
}
static DEVICE_ATTR_RO(fw_version);

static ssize_t fw_status_show(struct device *d, struct device_attribute *a, char *buf)
{
	struct g10_priv *p = dev_get_drvdata(d);

	return sysfs_emit(buf, "%d\n", p->status);
}
static DEVICE_ATTR_RO(fw_status);

static struct attribute *g10_attrs[] = {
	&dev_attr_fw_version.attr, &dev_attr_fw_status.attr, NULL };
ATTRIBUTE_GROUPS(g10);
```

조각 D: `probe` — 모드에 따라 동기/비동기 요청.

```c
static int g10_probe(struct platform_device *pdev)
{
	const struct firmware *f;
	struct g10_priv *p;
	ktime_t t0;
	int ret;

	p = devm_kzalloc(&pdev->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->dev = &pdev->dev;
	p->status = -EAGAIN;
	init_completion(&p->fw_done);
	p->dev_ram = vzalloc(G10_DEV_RAM);
	if (!p->dev_ram)
		return -ENOMEM;
	platform_set_drvdata(pdev, p);

	if (mode == 1 || mode == 2) {            /* asynchronous */
		ret = request_firmware_nowait(THIS_MODULE,
				mode == 1 ? FW_ACTION_UEVENT : FW_ACTION_NOUEVENT,
				fw, &pdev->dev, GFP_KERNEL, p, g10_fw_cb);
		dev_info(&pdev->dev, "probe: nowait queued (ret %d), probe returns\n", ret);
		if (ret)
			vfree(p->dev_ram);
		return ret;
	}
	t0 = ktime_get();                        /* synchronous */
	ret = mode == 3 ? request_firmware_direct(&f, fw, &pdev->dev)
			: request_firmware(&f, fw, &pdev->dev);
	p->t_req_us = ktime_us_delta(ktime_get(), t0);
	if (ret) {
		dev_err(&pdev->dev, "request_firmware(%s) failed: %d\n", fw, ret);
		vfree(p->dev_ram);
		return ret;
	}
	dev_info(&pdev->dev, "got %s: %zu bytes\n", fw, f->size);
	p->status = g10_boot_fw(p, f);
	release_firmware(f);                     /* driver copied what it needs */
	complete(&p->fw_done);
	if (p->status)
		vfree(p->dev_ram);
	return p->status;
}
```

조각 E: `remove`, 드라이버 구조체, 모듈 init/exit.

```c
static void g10_remove(struct platform_device *pdev)
{
	struct g10_priv *p = platform_get_drvdata(pdev);

	wait_for_completion(&p->fw_done);        /* async cb must finish first */
	if (!p->status || mode == 1 || mode == 2)
		vfree(p->dev_ram);
}

static struct platform_driver g10_driver = {
	.probe = g10_probe,
	.remove = g10_remove,
	.driver = { .name = "g10_fw", .dev_groups = g10_groups },
};

static int __init g10_init(void)
{
	int ret = platform_driver_register(&g10_driver);

	if (ret)
		return ret;
	g10_pdev = platform_device_register_simple("g10_fw", -1, NULL, 0);
	if (IS_ERR(g10_pdev)) {
		platform_driver_unregister(&g10_driver);
		return PTR_ERR(g10_pdev);
	}
	return 0;
}

static void __exit g10_exit(void)
{
	platform_device_unregister(g10_pdev);
	platform_driver_unregister(&g10_driver);
}
module_init(g10_init);
module_exit(g10_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G10 firmware loading demo");
MODULE_FIRMWARE("g10_dev.bin");
```

읽을 때 볼 점:

- 동기 경로(조각 D 아래쪽)는 `request_firmware` → 검증·다운로드 → **곧바로 `release_firmware`**. 드라이버가 펌웨어 바이트를 계속 들고 있지 않는다. 칩에 이미 내려보냈기 때문이다.
- 비동기 경로는 probe에서 `request_firmware_nowait`만 걸고 **바로 return 0**. 결과는 콜백이 `p->status`에 쓴다. 그래서 `p`의 모든 필드(`dev`, `status`, `fw_done`, `dev_ram`)를 **요청을 걸기 전에** 다 초기화했다 (§5에서 왜 중요한지 실제로 본다).
- `remove`는 `wait_for_completion(&p->fw_done)`으로 콜백이 끝날 때까지 기다린다. 콜백이 도는 중에 `p`가 해제되면 use-after-free다.
- `.dev_groups`로 sysfs 속성을 붙였다 — driver core가 **probe가 성공한 직후** 만들고 unbind 때 지운다 (G8, `really_probe()`). 그래서 probe가 실패하면 `fw_version` 파일 자체가 없다 (예제 7). `return dev_err(...), -EINVAL;`은 "로그 찍고 에러 반환"을 한 줄로 쓴 쉼표 연산자 표현이다.

### 2.4 예제 1 — 빌드

무엇을 확인하나: 랩 커널 빌드 트리로 out-of-tree 모듈을 경고 없이 빌드하고, `MODULE_FIRMWARE`가 `modinfo`에 어떻게 보이는지 본다.

```make
obj-m += g10_fw.o
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/G10/mod && cp /Users/donh/.elinux_scratch/G10/{g10_fw.c,Kbuild} /var/tmp/G10/mod/ \
  && cd /var/tmp/G10/mod && make -j2 -C /var/lab/out/linux-build M=$PWD modules && modinfo g10_fw.ko'
```

(소스는 Mac의 `~/.elinux_scratch/G10/`에 쓰고, VM 안에서는 같은 파일을 절대 경로 `/Users/donh/.elinux_scratch/G10/`로 읽는다. VM 안의 `~`는 VM 사용자 홈이라 다르다.)

```text
make[1]: Entering directory '/var/tmp/G10/mod'
  CC [M]  g10_fw.o
  MODPOST Module.symvers
  CC [M]  g10_fw.mod.o
  CC [M]  .module-common.o
  LD [M]  g10_fw.ko
make[1]: Leaving directory '/var/tmp/G10/mod'
make: Leaving directory '/var/lab/build/linux'
filename:       /var/tmp/G10/mod/g10_fw.ko
firmware:       g10_dev.bin
description:    G10 firmware loading demo
license:        GPL
depends:        
name:           g10_fw
vermagic:       6.18.55 SMP preempt mod_unload aarch64
parm:           fw:charp
parm:           mode:int
parm:           chunk:uint
```

출력에서 볼 것: 경고가 없다. `firmware:       g10_dev.bin` 줄이 `MODULE_FIRMWARE`의 결과다. `vermagic`의 `preempt`는 이 커널이 선점형(`CONFIG_PREEMPT`)이라는 뜻이고, §5에서 콜백 순서에 영향을 준다. (`modinfo` 출력은 `MODULE_FIRMWARE` 줄을 추가해 다시 빌드한 뒤의 것이다. 그 줄 외의 코드는 아래 모든 실행과 같다.)

### 2.5 예제 2 — 펌웨어 이미지 만들기

무엇을 확인하나: 호스트 쪽 "펌웨어 빌드 스크립트"가 헤더를 붙이는 모습. 결함 주입 옵션(`--bad-crc`는 헤더 CRC 비트 하나를 뒤집고, `--truncate N`은 파일을 N바이트에서 자른다)을 넣었다.

```python
#!/usr/bin/env python3
"""g10_mkfw.py OUT MAJOR MINOR PAYLOAD_BYTES [--bad-crc] [--truncate N]"""
import os, struct, sys, zlib

out, major, minor, n = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
payload = bytes((i * 7 + major) & 0xFF for i in range(n))   # deterministic pattern
crc = zlib.crc32(payload)
if "--bad-crc" in sys.argv:
    crc ^= 0x1                                               # flip one bit in header CRC
hdr = struct.pack("<IHHII", 0x46303147, major, minor, len(payload), crc)
blob = hdr + payload
if "--truncate" in sys.argv:
    blob = blob[: int(sys.argv[sys.argv.index("--truncate") + 1])]
with open(out, "wb") as f:
    f.write(blob)
print(f"{os.path.basename(out)}: v{major}.{minor} payload={n} crc=0x{zlib.crc32(payload):08x} file={len(blob)}")
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/G10 && python3 /Users/donh/.elinux_scratch/G10/g10_mkfw.py t.bin 1 2 64 && xxd t.bin | head -3'
```

```text
t.bin: v1.2 payload=64 crc=0x7806812c file=80
00000000: 4731 3046 0100 0200 4000 0000 2c81 0678  G10F....@...,..x
00000010: 0108 0f16 1d24 2b32 3940 474e 555c 636a  .....$+29@GNU\cj
00000020: 7178 7f86 8d94 9ba2 a9b0 b7be c5cc d3da  qx..............
```

출력에서 볼 것: 첫 4바이트 `47 31 30 46`이 ASCII로 `G10F`, 그다음 `0100`(major 1), `0200`(minor 2), `4000 0000`(len 64), `2c81 0678`(CRC `0x7806812c`, little-endian). 16바이트 뒤부터 payload.

### 2.6 실행 틀 — initramfs에 모듈·펌웨어·테스트 스크립트를 넣고 부팅

모든 게스트 실험은 이 스크립트 하나로 돌린다. 시나리오 `sN`마다 `fw_sN.sh`(펌웨어 파일을 rootfs 사본에 만든다)와 `t_sN.sh`(게스트에서 돌 명령)를 준비하고, `LAB_CMD=sh~/t.sh`로 부팅하면 스크립트를 돌고 자동으로 꺼진다.

```sh
#!/bin/bash
# g10_run.sh SCEN "extra kernel args"  -> boots lab kernel with rootfs + g10_fw.ko + firmware files
set -e
S=$1; EXTRA=$2; W=/var/tmp/G10; R=$W/r_$S; MK="python3 /Users/donh/.elinux_scratch/G10/g10_mkfw.py"
rm -rf $R; mkdir -p $R; cp -a /var/lab/build/rootfs/. $R/
cp $W/mod/g10_fw.ko $R/; mkdir -p $R/lib/firmware
cp /Users/donh/.elinux_scratch/G10/t_$S.sh $R/t.sh
[ -f /Users/donh/.elinux_scratch/G10/fw_$S.sh ] && (cd $R && MK="$MK" bash /Users/donh/.elinux_scratch/G10/fw_$S.sh)
(cd $R && find . | cpio -o -H newc --quiet | gzip -1) > $W/i_$S.gz
timeout 240 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd $W/i_$S.gz $QEXTRA \
  -append "console=ttyAMA0 quiet $EXTRA LAB_CMD=sh~/t.sh" > $W/out_$S.txt 2>&1 || true
tr -d '\r' < $W/out_$S.txt
```

포인트: 랩 rootfs(`/var/lab/build/rootfs`)는 **복사본**(`/var/tmp/G10/r_sN`)을 고친다. 랩 원본은 읽기 전용이다. 이 initramfs에는 udev도 mdev hotplug 핸들러도 없다 (`/init`이 `proc`·`sysfs`·`devtmpfs`·`debugfs`만 마운트한다). 그래서 uevent를 보고 펌웨어를 채워 줄 유저스페이스가 **없다** — §6에서 이것이 timeout으로 이어진다.

---

## 3. 검색 경로 — 커널은 어디를 어떤 순서로 찾나

### 3.1 예제 3 — 소스에서 직접 확인

무엇을 확인하나: 검색 경로 표 `fw_path[]`와 `firmware_class.path` 파라미터 정의.

```sh
limactl shell elinux -- bash -lc 'grep -n "fw_path\[\] =" -A7 /var/lab/src/linux-6.18.55/drivers/base/firmware_loader/main.c; grep -n "module_param_string(path" /var/lab/src/linux-6.18.55/drivers/base/firmware_loader/main.c'
```

```text
472:static const char * const fw_path[] = {
473-	fw_path_para,
474-	"/lib/firmware/updates/" UTS_RELEASE,
475-	"/lib/firmware/updates",
476-	"/lib/firmware/" UTS_RELEASE,
477-	"/lib/firmware"
478-};
479-
480-/*
485:module_param_string(path, fw_path_para, sizeof(fw_path_para), 0644);
```

출력에서 볼 것: 순서는 **① `firmware_class.path=`(설정했을 때만) → ② `/lib/firmware/updates/<커널 버전>` → ③ `/lib/firmware/updates` → ④ `/lib/firmware/<커널 버전>` → ⑤ `/lib/firmware`**. `UTS_RELEASE`는 빌드 시점의 `uname -r` 문자열(여기서는 `6.18.55`)이다. 처음 찾은 파일에서 멈춘다. 파라미터 권한이 `0644`라서 부팅 후에도 `/sys/module/firmware_class/parameters/path`에 쓸 수 있다. firmware_class는 보통 built-in이므로 커맨드라인에서는 `firmware_class.path=/opt/fw` 형태로 준다.

`updates/` 디렉터리는 "배포판 패키지가 깐 파일을 건드리지 않고 덮어쓰기"용이다. 예를 들어 센서 키트 현장에서 Wi-Fi 펌웨어 핫픽스를 시험할 때 `/lib/firmware/updates/brcm/...`에 넣으면 원래 파일은 그대로 두고 새 파일이 이긴다.

### 3.2 찾는 과정 전체 — fallback까지

```svg
<svg viewBox="0 0 680 500" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="16" width="380" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="38" text-anchor="middle" font-size="13">request_firmware(&amp;fw, "chip.bin", dev)</text>
  <rect x="20" y="70" width="380" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="92" text-anchor="middle" font-size="13">① 커널 내장? (CONFIG_EXTRA_FIRMWARE)</text>
  <rect x="20" y="124" width="380" height="120" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="144" text-anchor="middle" font-size="13">② 파일시스템 — fw_path[] 순서, 처음 찾은 것</text>
  <text x="40" y="166" font-size="12">0. firmware_class.path= (설정했을 때만)</text>
  <text x="40" y="184" font-size="12">1. /lib/firmware/updates/$(uname -r)</text>
  <text x="40" y="202" font-size="12">2. /lib/firmware/updates</text>
  <text x="40" y="220" font-size="12">3. /lib/firmware/$(uname -r)</text>
  <text x="40" y="238" font-size="12">4. /lib/firmware</text>
  <rect x="20" y="264" width="380" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="286" text-anchor="middle" font-size="13">③ name.zst / name.xz (COMPRESS 켰을 때)</text>
  <rect x="20" y="318" width="380" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="340" text-anchor="middle" font-size="13">④ platform fallback (EFI 내장, _platform만)</text>
  <rect x="20" y="372" width="380" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="210" y="394" text-anchor="middle" font-size="13">⑤ sysfs fallback 조건 맞나? (USER_HELPER)</text>
  <rect x="20" y="440" width="380" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="210" y="465" text-anchor="middle" font-size="13">실패: -ENOENT 또는 -ETIMEDOUT</text>
  <line x1="210" y1="50" x2="210" y2="68" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="210" y1="104" x2="210" y2="122" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="210" y1="244" x2="210" y2="262" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="210" y1="298" x2="210" y2="316" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="210" y1="352" x2="210" y2="370" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="210" y1="406" x2="210" y2="438" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="220" y="426" font-size="12">아니오</text>
  <rect x="450" y="150" width="210" height="64" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="555" y="176" text-anchor="middle" font-size="13">찾음 → assign_fw()</text>
  <text x="555" y="198" text-anchor="middle" font-size="13">struct firmware 반환</text>
  <line x1="400" y1="87" x2="448" y2="160" stroke="#3f9a6b" marker-end="url(#ah2)"/>
  <line x1="400" y1="184" x2="448" y2="184" stroke="#3f9a6b" marker-end="url(#ah2)"/>
  <line x1="400" y1="281" x2="448" y2="205" stroke="#3f9a6b" marker-end="url(#ah2)"/>
  <line x1="400" y1="335" x2="452" y2="214" stroke="#3f9a6b" marker-end="url(#ah2)"/>
  <rect x="450" y="364" width="210" height="64" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="555" y="390" text-anchor="middle" font-size="12">예: /sys/class/firmware/name/</text>
  <text x="555" y="410" text-anchor="middle" font-size="12">유저가 쓰거나 timeout (60 s)</text>
  <line x1="400" y1="389" x2="448" y2="389" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="555" y1="364" x2="555" y2="216" stroke="#3f9a6b" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
</svg>
```
그림 2 — `_request_firmware()`의 실제 순서 (6.18 `drivers/base/firmware_loader/main.c`). 녹색 화살표는 "찾았다"이다. ③④⑤는 전체 읽기 요청에서만 시도하고, `request_firmware_direct()`는 ⑤를 건너뛴다.

①의 "내장"과 같은 이름 요청 합치기, 캐시 확인은 `_request_firmware_prepare()`가, ②③은 `fw_get_filesystem_firmware()`가, ④는 `firmware_fallback_platform()`이, ⑤는 `firmware_fallback_sysfs()`가 한다. §11에서 함수 단위로 다시 본다.

### 3.3 예제 4 — 정상 로딩을 dynamic debug로 들여다보기

무엇을 확인하나: firmware_class의 `dev_dbg` 메시지를 켜서 그림 2의 ② 단계를 실제 로그로 보고, 우리 드라이버가 검증·다운로드까지 마치는지 본다. 시나리오 `s1`의 첫 번째 경우다 (나머지 세 경우는 §4 예제 7).

```sh
$MK lib/firmware/g10_dev.bin 1 2 262144
$MK lib/firmware/g10_badcrc.bin 1 3 262144 --bad-crc
$MK lib/firmware/g10_trunc.bin 1 4 262144 --truncate 100000
```

```sh
echo 'file drivers/base/firmware_loader/main.c +p' > /sys/kernel/debug/dynamic_debug/control
D=/sys/bus/platform/devices/g10_fw
for f in g10_dev.bin g10_missing.bin g10_badcrc.bin g10_trunc.bin; do
  echo "### insmod fw=$f"
  insmod /g10_fw.ko fw=$f; echo "insmod rc=$?"
  echo "driver link: $(ls $D/driver >/dev/null 2>&1 && echo bound || echo unbound)"
  [ -e $D/fw_version ] && echo "fw_version=$(cat $D/fw_version) fw_status=$(cat $D/fw_status)"
  dmesg -c | grep -E 'g10|irmware'
  rmmod g10_fw
done
```

```sh
limactl shell elinux -- bash -lc 'bash /Users/donh/.elinux_scratch/G10/g10_run.sh s1 ""'
```

```text
g10_dev.bin: v1.2 payload=262144 crc=0xb1f78de3 file=262160
g10_badcrc.bin: v1.3 payload=262144 crc=0xb1f78de3 file=262160
g10_trunc.bin: v1.4 payload=262144 crc=0xb1f78de3 file=100000
lab initramfs: 6.18.55 up 1.54s
### insmod fw=g10_dev.bin
insmod rc=0
driver link: bound
fw_version=1.2 fw_status=0
[    0.000000] psci: PSCIv1.1 detected in firmware.
[    1.698779] g10_fw: loading out-of-tree module taints kernel.
[    1.780372] firmware_class: __allocate_fw_priv: fw-g10_dev.bin fw_priv=00000000411a9a3a
[    1.781325] g10_fw g10_fw: loading /lib/firmware/updates/6.18.55/g10_dev.bin failed for no such file or directory.
[    1.781405] g10_fw g10_fw: loading /lib/firmware/updates/g10_dev.bin failed for no such file or directory.
[    1.781434] g10_fw g10_fw: loading /lib/firmware/6.18.55/g10_dev.bin failed for no such file or directory.
[    1.782090] g10_fw g10_fw: Loading firmware from /lib/firmware/g10_dev.bin
[    1.782113] g10_fw g10_fw: direct-loading g10_dev.bin
[    1.782274] firmware_class: fw_set_page_data: fw-g10_dev.bin fw_priv=00000000411a9a3a data=000000007bae7f8a size=262160
[    1.784899] g10_fw g10_fw: Loaded FW: g10_dev.bin, sha256: 9e46bb6a24fd0a898a3f458ad0f6f8fce976830bd9d236ec35140fe41c8d1dbb
[    1.784986] g10_fw g10_fw: got g10_dev.bin: 262160 bytes
[    1.786109] g10_fw g10_fw: downloaded 262144 bytes in 64 chunks of 4096
[    1.786222] g10_fw g10_fw: fw g10_dev.bin v1.2 running (req 4892 us, crc 408 us, dl 746 us)
[    1.786421] firmware_class: __free_fw_priv: fw-g10_dev.bin fw_priv=00000000411a9a3a data=000000007bae7f8a size=262160
```

출력에서 볼 것:

- `loading /lib/firmware/updates/6.18.55/... failed` → `updates/` → `6.18.55/` → 마지막에 `/lib/firmware/g10_dev.bin`에서 `Loading firmware from`. **fw_path[] 순서 그대로** 네 곳을 차례로 두드렸다. `firmware_class.path`를 안 줬으니 0번은 건너뛰었다.
- `Loaded FW: ..., sha256: ...` — 랩 커널은 `CONFIG_FW_LOADER_DEBUG=y`라서 받은 파일의 SHA-256을 로그로 남긴다. "현장 장비에 정말 어떤 파일이 올라갔나"를 사후에 확인할 때 쓸모 있다. 대신 **모든 로딩마다 SHA-256을 계산**하므로 `req` 시간에 포함된다 (§9).
- 드라이버는 `got ... 262160 bytes`(헤더 16 + payload 262144) → 64 chunk 다운로드 → `v1.2 running`. sysfs `fw_version=1.2`.
- 마지막 `__free_fw_priv`가 `release_firmware()`의 흔적이다. 바이트는 칩(여기서는 `dev_ram`)으로 갔고, 커널 버퍼는 바로 반납됐다.
- `[0.000000] psci: PSCIv1.1 detected in firmware.`는 grep의 `irmware`에 걸린 무관한 줄이다 (여기서 firmware는 TF-A다).

### 3.4 예제 5 — 우선순위: 다섯 경로에 다섯 버전을 놓고 하나씩 지우기

무엇을 확인하나: 같은 이름 `g10_dev.bin`을 다섯 경로에 v1~v5로 놓고, 위에서부터 하나씩 지우며 `unbind`/`bind`로 probe를 다시 돌려 어느 파일이 이기는지 본다. 커맨드라인에 `firmware_class.path=/opt/fw`를 준다. 덤으로 `.xz`만 있는 파일도 시험한다 (§3.5).

```sh
mkdir -p lib/firmware/6.18.55 lib/firmware/updates/6.18.55 opt/fw
$MK lib/firmware/g10_dev.bin 1 0 4096
$MK lib/firmware/6.18.55/g10_dev.bin 2 0 4096
$MK lib/firmware/updates/g10_dev.bin 3 0 4096
$MK lib/firmware/updates/6.18.55/g10_dev.bin 4 0 4096
$MK opt/fw/g10_dev.bin 5 0 4096
$MK lib/firmware/g10_cmp.bin 6 0 4096 && xz -k lib/firmware/g10_cmp.bin && rm lib/firmware/g10_cmp.bin
```

```sh
D=/sys/bus/platform/devices/g10_fw; DRV=/sys/bus/platform/drivers/g10_fw
echo "firmware_class.path = '$(cat /sys/module/firmware_class/parameters/path)'"
insmod /g10_fw.ko
for gone in /opt/fw /lib/firmware/updates/6.18.55 /lib/firmware/updates /lib/firmware/6.18.55; do
  echo "version=$(cat $D/fw_version)   <- then remove $gone/g10_dev.bin"
  rm $gone/g10_dev.bin
  echo g10_fw > $DRV/unbind; echo g10_fw > $DRV/bind
done
echo "version=$(cat $D/fw_version)   (only /lib/firmware/g10_dev.bin left)"
echo "--- runtime path change + .xz-only file"
echo /opt/none > /sys/module/firmware_class/parameters/path
cat /sys/module/firmware_class/parameters/path
rmmod g10_fw; ls /lib/firmware/g10_cmp*; insmod /g10_fw.ko fw=g10_cmp.bin
dmesg | grep -E 'g10_cmp|Direct'
```

```sh
limactl shell elinux -- bash -lc 'bash /Users/donh/.elinux_scratch/G10/g10_run.sh s3 firmware_class.path=/opt/fw'
```

```text
g10_dev.bin: v1.0 payload=4096 crc=0x8d1fe65a file=4112
g10_dev.bin: v2.0 payload=4096 crc=0x4c604e4a file=4112
g10_dev.bin: v3.0 payload=4096 crc=0x5e4e1995 file=4112
g10_dev.bin: v4.0 payload=4096 crc=0x68083a3d file=4112
g10_dev.bin: v5.0 payload=4096 crc=0x54e39554 file=4112
g10_cmp.bin: v6.0 payload=4096 crc=0x63811fce file=4112
lab initramfs: 6.18.55 up 0.74s
firmware_class.path = '/opt/fw'
version=5.0   <- then remove /opt/fw/g10_dev.bin
version=4.0   <- then remove /lib/firmware/updates/6.18.55/g10_dev.bin
version=3.0   <- then remove /lib/firmware/updates/g10_dev.bin
version=2.0   <- then remove /lib/firmware/6.18.55/g10_dev.bin
version=1.0   (only /lib/firmware/g10_dev.bin left)
--- runtime path change + .xz-only file
/opt/none

/lib/firmware/g10_cmp.bin.xz
[    1.046994] g10_fw g10_fw: request_firmware(g10_cmp.bin) failed: -2
[    1.048282] g10_fw g10_fw: probe with driver g10_fw failed with error -2
[    1.046517] g10_fw g10_fw: Direct firmware load for g10_cmp.bin failed with error -2
[    1.046994] g10_fw g10_fw: request_firmware(g10_cmp.bin) failed: -2
```

출력에서 볼 것:

- v5(`firmware_class.path`) → v4(`updates/6.18.55`) → v3(`updates`) → v2(`6.18.55`) → v1(`/lib/firmware`). 소스의 `fw_path[]` 순서와 정확히 같다.
- `unbind`/`bind`만으로 probe가 다시 돌아 **새 파일을 다시 읽는다**. firmware_class는 release된 펌웨어를 들고 있지 않으므로 (suspend 캐시 예외, §8) 파일을 바꾸고 rebind하면 바로 반영된다. 현장에서 펌웨어 핫픽스를 재부팅 없이 시험하는 방법이 이것이다 (드라이버가 remove/probe를 깔끔히 지원할 때).
- 실행 중에 `/sys/module/firmware_class/parameters/path`에 `/opt/none`을 쓰니 그대로 바뀌었다 (값 뒤에 빈 줄이 하나 더 찍힌 것은 `echo`가 넣은 `\n`이 저장됐기 때문이다 — 소스가 비교할 때 `\n`을 잘라 내는 이유가 이것이다).
- `.xz`만 있는 `g10_cmp.bin`은 **-2**. 다음 절에서 이유를 본다.

### 3.5 압축 펌웨어 — 켜져 있어야 찾는다

`CONFIG_FW_LOADER_COMPRESS_XZ`/`_ZSTD`가 켜져 있으면, 원래 이름으로 못 찾았을 때 `name.zst`, `name.xz`를 같은 경로 순서로 다시 찾고 커널 안에서 풀어 준다 (그림 2의 ③). linux-firmware 저장소는 설치 시 압축 옵션을 제공하고, 배포판들은 디스크를 아끼려고 `.zst`로 깐다.

예제 6 — 두 커널의 설정 비교. 무엇을 확인하나: 랩 커널과 VM 호스트(Ubuntu) 커널의 펌웨어 로더 설정 차이.

```sh
limactl shell elinux -- bash -lc 'grep -E "FW_LOADER|EXTRA_FIRMWARE|FW_CACHE|FW_UPLOAD" /var/lab/out/linux-build/.config; echo ===; zgrep -E "FW_LOADER_COMPRESS|FW_LOADER_USER_HELPER" /boot/config-$(uname -r)'
```

```text
CONFIG_FW_LOADER=y
CONFIG_FW_LOADER_DEBUG=y
CONFIG_FW_LOADER_PAGED_BUF=y
CONFIG_FW_LOADER_SYSFS=y
CONFIG_EXTRA_FIRMWARE=""
CONFIG_FW_LOADER_USER_HELPER=y
# CONFIG_FW_LOADER_USER_HELPER_FALLBACK is not set
# CONFIG_FW_LOADER_COMPRESS is not set
CONFIG_FW_CACHE=y
CONFIG_FW_UPLOAD=y
===
CONFIG_FW_LOADER_USER_HELPER=y
# CONFIG_FW_LOADER_USER_HELPER_FALLBACK is not set
CONFIG_FW_LOADER_COMPRESS=y
CONFIG_FW_LOADER_COMPRESS_XZ=y
CONFIG_FW_LOADER_COMPRESS_ZSTD=y
```

출력에서 볼 것: 랩 커널(arm64 defconfig 기반)은 `# CONFIG_FW_LOADER_COMPRESS is not set` → `.xz`를 찾지 않으니 예제 5에서 -2가 났다. Ubuntu 커널은 XZ·ZSTD 둘 다 켜져 있다. **Yocto/Buildroot로 만든 커널 설정과 rootfs의 펌웨어 패키징 방식이 어긋나면**(rootfs에는 `.zst`만 깔렸는데 커널은 압축 미지원) 똑같이 -2가 난다. 실무에서 꽤 흔한 함정이다.

나머지 줄의 뜻: `USER_HELPER=y`이지만 `USER_HELPER_FALLBACK`은 꺼짐 → sysfs fallback은 **특정 조건에서만** 탄다 (§6). `FW_CACHE=y` → suspend 캐시 (§8). `EXTRA_FIRMWARE=""` → 커널에 내장된 펌웨어 없음 (§7). `FW_UPLOAD=y` → 반대 방향, 유저가 장치 flash에 펌웨어를 **써 넣는** 프레임워크 (§10).

---

## 4. 받은 바이트를 믿지 마라 — 검증과 실패 처리

### 4.1 왜 드라이버가 또 검증하나

firmware_class는 "그 이름의 파일"을 줄 뿐, 그 파일이 **맞는 칩용인지, 다 복사됐는지, 깨지지 않았는지** 모른다. 현장에서 실제로 생기는 일:

- 패키지 업데이트 중 전원이 나가서 파일이 잘렸다 (truncated).
- 다른 리비전 칩용 파일이 같은 이름으로 깔렸다 (예: 보드 A용 NVRAM이 보드 B에).
- eMMC/SD 비트 오류, 또는 OTA로 받은 파일이 손상됐다.

깨진 펌웨어를 칩에 밀어 넣으면 결과가 나쁘다. 칩 ROM이 CRC를 확인해 주면 다행이고, 아니면 칩이 엉뚱한 코드로 돌다가 버스를 붙잡고 hang된다 — 그러면 증상은 "펌웨어 문제"가 아니라 "SDIO timeout", "I2C NACK"으로 나타나서 원인을 찾기 어렵다. Don이 RF 칩 bring-up에서 버스 장애를 root cause하던 경험을 떠올리면, **다운로드 전에 호스트에서 걸러 내는 것**이 디버깅 시간을 얼마나 줄이는지 알 것이다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="16" text-anchor="middle" font-size="13" font-weight="bold">g10 펌웨어 이미지 (little-endian)</text>
  <text x="30" y="34" font-size="12">0x00</text>
  <text x="130" y="34" font-size="12">0x04</text>
  <text x="180" y="34" font-size="12">0x06</text>
  <text x="230" y="34" font-size="12">0x08</text>
  <text x="330" y="34" font-size="12">0x0C</text>
  <text x="430" y="34" font-size="12">0x10</text>
  <rect x="30" y="40" width="100" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="80" y="63" text-anchor="middle" font-size="13">magic</text>
  <rect x="130" y="40" width="50" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="155" y="63" text-anchor="middle" font-size="13">maj</text>
  <rect x="180" y="40" width="50" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="205" y="63" text-anchor="middle" font-size="13">min</text>
  <rect x="230" y="40" width="100" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="280" y="63" text-anchor="middle" font-size="13">len</text>
  <rect x="330" y="40" width="100" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="380" y="63" text-anchor="middle" font-size="13">crc32</text>
  <rect x="430" y="40" width="220" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="540" y="63" text-anchor="middle" font-size="13">payload (len 바이트)</text>
  <text x="340" y="112" text-anchor="middle" font-size="13" font-weight="bold">드라이버 검증 파이프라인 (g10_validate → g10_download)</text>
  <rect x="15" y="125" width="100" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="65" y="146" text-anchor="middle" font-size="12">① 크기</text>
  <text x="65" y="164" text-anchor="middle" font-size="12">size ≥ 16</text>
  <rect x="125" y="125" width="100" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="175" y="146" text-anchor="middle" font-size="12">② magic</text>
  <text x="175" y="164" text-anchor="middle" font-size="12">== G10F</text>
  <rect x="235" y="125" width="100" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="285" y="146" text-anchor="middle" font-size="12">③ len</text>
  <text x="285" y="164" text-anchor="middle" font-size="12">= size − 16</text>
  <rect x="345" y="125" width="100" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="395" y="146" text-anchor="middle" font-size="12">④ CRC32</text>
  <text x="395" y="164" text-anchor="middle" font-size="12">payload 일치</text>
  <rect x="455" y="125" width="100" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="505" y="146" text-anchor="middle" font-size="12">⑤ 4 KiB 쓰기</text>
  <text x="505" y="164" text-anchor="middle" font-size="12">+ read-back</text>
  <rect x="565" y="125" width="100" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="615" y="146" text-anchor="middle" font-size="12">⑥ sysfs</text>
  <text x="615" y="164" text-anchor="middle" font-size="12">fw_version</text>
  <line x1="115" y1="150" x2="123" y2="150" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="225" y1="150" x2="233" y2="150" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="335" y1="150" x2="343" y2="150" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="445" y1="150" x2="453" y2="150" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="555" y1="150" x2="563" y2="150" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="65" y="200" text-anchor="middle" font-size="12">-EINVAL</text>
  <text x="175" y="200" text-anchor="middle" font-size="12">-EINVAL</text>
  <text x="285" y="200" text-anchor="middle" font-size="12">-EINVAL</text>
  <text x="395" y="200" text-anchor="middle" font-size="12">-EBADMSG</text>
  <text x="505" y="200" text-anchor="middle" font-size="12">-EIO</text>
  <text x="615" y="200" text-anchor="middle" font-size="12">(G8)</text>
  <line x1="15" y1="185" x2="555" y2="185" stroke="#d0564a" stroke-dasharray="4 3"/>
  <text x="340" y="236" text-anchor="middle" font-size="13">하나라도 실패하면 probe가 에러 반환 → 장치는 unbound (예제 7)</text>
</svg>
```
그림 3 — 위: 16바이트 헤더 + payload. 아래: 드라이버가 칩에 쓰기 전에 거치는 검사. 빨간 점선 아래는 각 단계 실패 시 돌려주는 errno.

검사 순서에도 이유가 있다. **싼 검사부터** 한다 (크기 → magic → 길이 → CRC). 크기를 먼저 보지 않으면 `h->magic`을 읽는 순간 16바이트보다 작은 파일에서 버퍼 밖을 읽는다. 길이를 CRC보다 먼저 보는 것도 같은 이유다 — 헤더의 `len`을 믿고 CRC를 돌리면 잘린 파일에서 out-of-bounds read가 난다. **`fw->size`만이 진짜 길이이고, 헤더 안의 숫자는 전부 공격자/손상 가능 입력**으로 다룬다.

### 4.2 예제 7 — 파일 없음, CRC 불일치, 잘린 파일

무엇을 확인하나: 예제 4와 같은 실행(`s1`)의 나머지 세 경우. firmware_class의 실패(-2)와 드라이버의 검증 실패(-74, -22)가 로그에서 어떻게 다르게 보이는지.

```text
### insmod fw=g10_missing.bin
[    1.998903] g10_fw g10_fw: request_firmware(g10_missing.bin) failed: -2
[    2.001493] g10_fw g10_fw: probe with driver g10_fw failed with error -2
insmod rc=0
driver link: unbound
[    1.925644] firmware_class: fw_name_devm_release: fw_name-g10_dev.bin devm-0000000080db3e7d released
[    1.998016] firmware_class: __allocate_fw_priv: fw-g10_missing.bin fw_priv=000000006fda444d
[    1.998280] g10_fw g10_fw: loading /lib/firmware/updates/6.18.55/g10_missing.bin failed for no such file or directory.
[    1.998372] g10_fw g10_fw: loading /lib/firmware/updates/g10_missing.bin failed for no such file or directory.
[    1.998507] g10_fw g10_fw: loading /lib/firmware/6.18.55/g10_missing.bin failed for no such file or directory.
[    1.998573] g10_fw g10_fw: loading /lib/firmware/g10_missing.bin failed for no such file or directory.
[    1.998627] g10_fw g10_fw: Direct firmware load for g10_missing.bin failed with error -2
[    1.998800] firmware_class: __free_fw_priv: fw-g10_missing.bin fw_priv=000000006fda444d data=0000000000000000 size=0
[    1.998903] g10_fw g10_fw: request_firmware(g10_missing.bin) failed: -2
[    2.001493] g10_fw g10_fw: probe with driver g10_fw failed with error -2
### insmod fw=g10_badcrc.bin
[    2.144810] g10_fw g10_fw: CRC mismatch: hdr 0xb1f78de2 calc 0xb1f78de3
[    2.146433] g10_fw g10_fw: probe with driver g10_fw failed with error -74
insmod rc=0
driver link: unbound
...
[    2.141843] g10_fw g10_fw: Loading firmware from /lib/firmware/g10_badcrc.bin
[    2.141856] g10_fw g10_fw: direct-loading g10_badcrc.bin
[    2.144390] g10_fw g10_fw: got g10_badcrc.bin: 262160 bytes
[    2.144810] g10_fw g10_fw: CRC mismatch: hdr 0xb1f78de2 calc 0xb1f78de3
[    2.144871] firmware_class: __free_fw_priv: fw-g10_badcrc.bin fw_priv=000000006fda444d data=000000007bae7f8a size=262160
[    2.146433] g10_fw g10_fw: probe with driver g10_fw failed with error -74
### insmod fw=g10_trunc.bin
[    2.282549] g10_fw g10_fw: size mismatch: hdr len 262144, file payload 99984
insmod rc=0[    2.284978] g10_fw g10_fw: probe with driver g10_fw failed with error -22

driver link: unbound
...
[    2.282486] g10_fw g10_fw: got g10_trunc.bin: 100000 bytes
[    2.282549] g10_fw g10_fw: size mismatch: hdr len 262144, file payload 99984
[    2.282598] firmware_class: __free_fw_priv: fw-g10_trunc.bin fw_priv=00000000411a9a3a data=000000007bae7f8a size=100000
[    2.284978] g10_fw g10_fw: probe with driver g10_fw failed with error -22
```

(`...`는 예제 4와 같은 `__allocate_fw_priv`·경로 탐색·`fw_set_page_data`·sha256 줄을 생략한 것이다. 맨 위 두 줄씩은 콘솔에 바로 찍힌 에러 레벨 메시지이고, 아래는 `dmesg -c`로 다시 읽은 것이라 같은 줄이 두 번 보인다. `insmod rc=0[ 2.284978]`처럼 섞인 줄은 셸 출력과 커널 콘솔 출력이 한 시리얼 포트에서 섞인 것이다.)

출력에서 볼 것:

- **파일 없음**: 네 경로를 다 두드리고 `Direct firmware load for g10_missing.bin failed with error -2`. 이 한 줄이 Wi-Fi bring-up 첫날 보는 그 메시지다. `-ENOENT` 이후 sysfs fallback은 **타지 않았다** (조건이 안 맞아서, §6) — 그래서 즉시 실패했다.
- **CRC 불일치**: firmware_class 입장에서는 정상 로딩(`Loading firmware from ...`)이다. 드라이버가 `-EBADMSG`(-74)로 거부했다. 헤더 CRC `...e2`와 계산값 `...e3`이 비트 하나 차이 — 결함 주입한 그대로다.
- **잘린 파일**: 100000바이트만 받았는데 헤더는 262144를 주장 → `-EINVAL`(-22). CRC를 돌리기 전에 걸렀다.
- 세 경우 모두 **`insmod rc=0`인데 장치는 `unbound`**다. 모듈 init(드라이버 등록)은 성공했고 probe만 실패했기 때문이다 (G3). "insmod는 됐는데 장치가 안 보인다"의 전형적인 모양이다. 실패 원인은 dmesg에만 있다.
- 실패한 경우에도 `__free_fw_priv`가 찍힌다. 검증 실패 경로에서도 `release_firmware()`를 부르도록 짰기 때문이다 (조각 D: `g10_boot_fw` 결과와 상관없이 `release_firmware(f)`).

### 4.3 서명된 펌웨어 — CRC는 사고를 막고, 서명은 공격을 막는다

CRC32는 **우연한 손상**만 잡는다. 누군가 일부러 바꾼 파일은 CRC를 다시 계산해 넣으면 그만이다. 의도적 변조를 막으려면 서명이 필요하고, 실제 시스템은 보통 이렇게 나눈다.

| 누가 검증하나 | 방법 | 예 |
|---|---|---|
| **디바이스 ROM** (가장 흔함) | 칩 ROM에 벤더 공개키 해시가 박혀 있고, 다운로드된 이미지의 서명을 칩이 확인 | 최신 Wi-Fi 칩, 모뎀, 보안 코어, Don의 SSD 컨트롤러의 secure boot |
| 커널 (LSM/IMA) | `kernel_read_file(..., READING_FIRMWARE)` 경로에 IMA appraisal·LSM 훅이 걸려 파일 서명/해시를 정책으로 검사 | IMA 정책 `appraise func=FIRMWARE_CHECK` (정책 문법은 버전별로 확인) |
| 드라이버 | 드라이버가 헤더 안 서명을 직접 검증 | 드물다 — 키 관리가 커널 쪽에 생기기 때문 |

중요한 사실 하나: **커널 모듈 서명(`CONFIG_MODULE_SIG`)은 펌웨어 파일을 보호하지 않는다**. `.ko`만 검사한다. 면접에서 "secure boot면 펌웨어도 안전한가?"라고 물으면 이 구분을 말하면 된다. firmware_class 코드가 `security_kernel_load_data(LOADING_FIRMWARE, ...)`로 sysfs fallback도 LSM이 막을 수 있게 해 둔 것을 소스에서 확인할 수 있다(`fallback.c`의 `fw_run_sysfs_fallback()`, §6).

---

## 5. 비동기 로딩 — `request_firmware_nowait`

### 5.1 왜 비동기인가

동기 `request_firmware`는 probe 스레드를 파일 읽기·검증·다운로드 시간 동안 붙잡는다. 문제가 되는 경우:

- 펌웨어가 크다 (수 MB DSP 이미지, FPGA 비트스트림). probe가 수백 ms~초를 먹으면 부팅이 그만큼 늦어진다 (같은 스레드에서 다음 드라이버 probe가 기다림).
- 파일이 **아직 없을 수 있다** (rootfs 전, §7). 동기로 기다리면 fallback timeout(기본 60초) 동안 부팅이 멈출 수 있다.
- 디바이스가 펌웨어 없이도 일부 기능(예: 장치 노드 생성, 전원 관리)을 먼저 제공할 수 있다.

`request_firmware_nowait()`는 요청을 workqueue에 넣고 즉시 돌아온다. 결과는 나중에 `cont(fw, context)` 콜백으로 온다. 못 찾으면 `fw == NULL`로 온다. 콜백 안에서 `release_firmware()`를 부르는 것도 드라이버 몫이다.

```c
/* 6.18 시그니처 (include/linux/firmware.h) */
int request_firmware_nowait(struct module *module, bool uevent,
			    const char *name, struct device *device, gfp_t gfp,
			    void *context,
			    void (*cont)(const struct firmware *fw, void *context));
```

`uevent` 인자: `FW_ACTION_UEVENT`(1)이면 보통의 요청, `FW_ACTION_NOUEVENT`(0)이면 "파일이 없으면 uevent 없이 sysfs 노드를 만들고 유저가 직접 채우기를 기다림"(§6). `module`은 콜백이 끝날 때까지 모듈이 언로드되지 않게 참조를 잡는 데 쓴다.

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="30" font-size="13" font-weight="bold">동기 — request_firmware()</text>
  <text x="20" y="80" font-size="13">probe 스레드</text>
  <rect x="150" y="60" width="300" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="300" y="80" text-anchor="middle" font-size="13">파일 읽기 (req)</text>
  <rect x="450" y="60" width="70" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="485" y="80" text-anchor="middle" font-size="13">CRC</text>
  <rect x="520" y="60" width="80" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="80" text-anchor="middle" font-size="13">다운로드</text>
  <line x1="600" y1="50" x2="600" y2="100" stroke="#d0564a" stroke-width="2"/>
  <text x="600" y="118" text-anchor="end" font-size="12">여기서야 probe 반환 → insmod 반환</text>
  <text x="20" y="160" font-size="13" font-weight="bold">비동기 — request_firmware_nowait()</text>
  <text x="20" y="200" font-size="13">probe 스레드</text>
  <rect x="150" y="180" width="60" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="180" y="200" text-anchor="middle" font-size="12">probe</text>
  <line x1="210" y1="170" x2="210" y2="220" stroke="#d0564a" stroke-width="2"/>
  <text x="218" y="200" font-size="12">probe·insmod 반환 (1.057 s / 1.060 s)</text>
  <text x="20" y="260" font-size="13">kworker</text>
  <rect x="260" y="240" width="190" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="355" y="260" text-anchor="middle" font-size="13">파일 읽기</text>
  <rect x="450" y="240" width="70" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="485" y="260" text-anchor="middle" font-size="13">CRC</text>
  <rect x="520" y="240" width="80" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="260" text-anchor="middle" font-size="13">다운로드</text>
  <line x1="180" y1="212" x2="258" y2="248" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <text x="450" y="292" text-anchor="middle" font-size="12">fw_cb 1.125 s</text>
  <text x="600" y="292" text-anchor="end" font-size="12">완료 1.134 s</text>
  <line x1="450" y1="272" x2="450" y2="280" stroke="currentColor"/>
  <line x1="600" y1="272" x2="600" y2="280" stroke="currentColor"/>
</svg>
```
그림 4 — 동기는 probe가 전부를 기다리고, 비동기는 probe가 요청만 걸고 바로 돌아온다. 아래 숫자는 예제 8의 실제 dmesg 시각(4 MiB 펌웨어, 눈금은 비례 아님).

### 5.2 예제 8 — nowait: probe가 먼저 돌아오는가, 그리고 반대 경우

무엇을 확인하나: `mode=1`로 4 MiB 펌웨어를 요청해 probe·insmod가 먼저 반환되고 콜백이 나중에 오는지, `fw_status`가 그 사이 `-EAGAIN`(-11, "아직")인지 본다. 이어서 없는 파일로 같은 실험을 한다. 셸이 `/dev/kmsg`에 표시를 써서 dmesg 안에서 순서를 비교한다.

```sh
D=/sys/bus/platform/devices/g10_fw
echo "g10: shell: before insmod (mode=1, 4 MiB)" > /dev/kmsg
insmod /g10_fw.ko mode=1 fw=g10_big.bin
echo "g10: shell: insmod returned" > /dev/kmsg
echo "right after insmod: fw_status=$(cat $D/fw_status) fw_version=$(cat $D/fw_version)"
sleep 1
echo "1 s later:          fw_status=$(cat $D/fw_status) fw_version=$(cat $D/fw_version)"
rmmod g10_fw
echo "g10: shell: before insmod (mode=1, missing)" > /dev/kmsg
insmod /g10_fw.ko mode=1 fw=g10_nothere.bin
echo "g10: shell: insmod returned" > /dev/kmsg
sleep 1
echo "missing case: driver $(ls $D/driver >/dev/null 2>&1 && echo bound || echo unbound), fw_status=$(cat $D/fw_status) fw_version=$(cat $D/fw_version)"
dmesg | grep -E 'g10' | grep -v taints
```

```sh
limactl shell elinux -- bash -lc 'bash /Users/donh/.elinux_scratch/G10/g10_run.sh s2 ""'
```

```text
g10_big.bin: v2.0 payload=4194304 crc=0x2ae64b61 file=4194320
lab initramfs: 6.18.55 up 0.91s
right after insmod: fw_status=-11 fw_version=none
1 s later:          fw_status=0 fw_version=2.0
missing case: driver bound, fw_status=-2 fw_version=none
[    1.024921] g10: shell: before insmod (mode=1, 4 MiB)
[    1.057487] g10_fw g10_fw: probe: nowait queued (ret 0), probe returns
[    1.060085] g10: shell: insmod returned
[    1.125168] g10_fw g10_fw: fw_cb: called, fw=OK
[    1.134464] g10_fw g10_fw: downloaded 4194304 bytes in 1024 chunks of 4096
[    1.134598] g10_fw g10_fw: fw g10_big.bin v2.0 running (req 0 us, crc 4744 us, dl 4563 us)
[    2.148878] g10: shell: before insmod (mode=1, missing)
[    2.165304] g10_fw g10_fw: Direct firmware load for g10_nothere.bin failed with error -2
[    2.165496] g10_fw g10_fw: fw_cb: called, fw=NULL
[    2.165566] g10_fw g10_fw: probe: nowait queued (ret 0), probe returns
[    2.167869] g10: shell: insmod returned
```

출력에서 볼 것:

- 4 MiB 경우: `probe returns`(1.057) → `insmod returned`(1.060) → `fw_cb`(1.125) → 완료(1.134). 셸은 insmod 직후 `fw_status=-11`(아직 안 끝남), 1초 뒤 `0`과 버전 `2.0`을 읽었다. **probe가 끝났다고 칩이 준비된 것이 아니다.** 유저스페이스(또는 다른 드라이버)가 "준비됨"을 알아야 한다면 상태를 노출해야 한다 — 여기서는 `fw_status`, 실제 드라이버는 네트워크 인터페이스를 콜백에서야 등록하는 식으로 한다 (예: `brcmfmac`은 펌웨어 콜백 이후에 `wlan0`을 만든다).
- `req 0 us`는 비동기 경로에서 요청 시간을 재지 않았기 때문이다 (조각 D의 `t_req_us`는 동기 경로에서만 채운다).
- **없는 파일 경우가 더 중요하다**: `fw_cb: called, fw=NULL`(2.165496)이 `probe: nowait queued ... probe returns`(2.165566)보다 **먼저** 찍혔다. 콜백이 probe 반환 전에 돌았다. 이 커널은 선점형(`preempt`, 예제 1의 vermagic)이고, 파일이 없으면 탐색이 금방 끝나니 `schedule_work()` 직후 kworker가 probe 스레드를 선점했다. CPU가 여럿이면 다른 CPU에서 동시에 도는 것도 흔하다.
- 교훈: `request_firmware_nowait()`를 부르는 순간부터 콜백은 **언제든** 돌 수 있다. 콜백이 쓰는 상태(`p->dev`, `p->fw_done`, `p->dev_ram`)는 **요청 전에** 다 준비해야 한다. "probe 끝에 `platform_set_drvdata()`" 같은 순서는 버그다.
- 없는 파일인데도 장치는 `bound`이고 `fw_status=-2`. 비동기 설계에서는 probe가 이미 0을 돌려줬으므로 driver core 입장에서 성공이다. 펌웨어 실패를 어떻게 처리할지(재시도 타이머, 사용자에게 상태 노출, 또는 `device_release_driver()`로 스스로 unbind)는 드라이버가 정해야 한다.

### 5.3 비동기의 수명 규칙 — remove와 경쟁

- `remove`(또는 모듈 언로드)는 **콜백이 끝날 때까지 기다려야** 한다. 이 노트의 드라이버는 `completion`으로 기다린다. 6.18에는 `request_firmware_nowait_cancel(dev, context, cont)`도 있어서 아직 시작 안 한 요청을 취소할 수 있다 (예제 3과 같은 소스 파일 1306번 줄에 정의, `EXPORT_SYMBOL_GPL` — 비교적 최근에 추가된 API라 오래된 커널에는 없다).
- 콜백은 프로세스 문맥(kworker)에서 돌므로 sleep 가능하다. 그래서 콜백 안에서 I2C/SPI로 칩에 다운로드해도 된다.
- 콜백에서 `fw`를 받으면 다 쓰고 `release_firmware(fw)`. `NULL`이어도 호출해도 된다 (`release_firmware(NULL)`은 no-op).
- `gfp` 인자는 요청 구조체 할당용이다. probe에서는 `GFP_KERNEL`.

---

## 6. sysfs fallback — 유저스페이스가 펌웨어를 넣어 주는 옛 경로

### 6.1 무엇이고 왜 거의 안 쓰나

파일시스템에서 못 찾았을 때, 커널이 `/sys/class/firmware/<name>/` 아래에 `loading`과 `data` 파일을 만들고 (보통 uevent를 보내서) 유저스페이스가 채워 주기를 기다리는 방식이다. 프로토콜은 Don이 아는 "호스트가 칩에 다운로드하는 3단계"와 똑같다.

```sh
echo 1 > /sys/class/firmware/<name>/loading     # 시작
cat image.bin > /sys/class/firmware/<name>/data # 바이트 전송
echo 0 > /sys/class/firmware/<name>/loading     # 완료 (취소는 echo -1)
```

예전에는 udev가 이 uevent를 받아 자동으로 채웠다. 지금은 커널이 파일시스템을 직접 읽으므로(direct load) 대부분 필요 없고, systemd-udev는 오래전에 이 기능을 뺐다 (기억 기준: 2014년 무렵 — 버전 번호는 확인하지 못했다). 남은 용도는 "파일시스템에 없는 펌웨어를 유저스페이스 프로그램이 만들어서 넣어 줘야 할 때"(예: 보정 데이터를 EEPROM에서 읽어 조합)뿐이다.

**언제 이 경로를 타나** — 소스(`drivers/base/firmware_loader/fallback.c`)를 읽으면:

```c
static bool fw_force_sysfs_fallback(u32 opt_flags)
{
	if (fw_fallback_config.force_sysfs_fallback)
		return true;
	if (!(opt_flags & FW_OPT_USERHELPER))
		return false;
	return true;
}
```

`CONFIG_FW_LOADER_USER_HELPER=y`라도 다음 중 하나여야 탄다.

- `force_sysfs_fallback`가 참: `CONFIG_FW_LOADER_USER_HELPER_FALLBACK=y`로 빌드했거나, 실행 중 `/proc/sys/kernel/firmware_config/force_sysfs_fallback`에 1을 썼을 때 → **모든** 요청이 파일 실패 시 sysfs로 간다.
- 요청에 `FW_OPT_USERHELPER`가 있을 때 → `request_firmware_nowait(..., FW_ACTION_NOUEVENT, ...)`로 요청한 경우.

그리고 `request_firmware_direct()`(`FW_OPT_NOFALLBACK_SYSFS`)와 `ignore_sysfs_fallback=1`은 무조건 막는다. 랩과 Ubuntu 모두 `USER_HELPER=y`, `USER_HELPER_FALLBACK` 꺼짐(예제 6)이라 평소 `request_firmware()`는 예제 7처럼 즉시 -2로 끝났다.

### 6.2 예제 9 — 손으로 펌웨어 넣기 (`FW_ACTION_NOUEVENT`)

무엇을 확인하나: `mode=2`로 파일시스템에 없는 `g10_uh.bin`을 요청하면 sysfs 노드가 생기고, 셸에서 3단계 프로토콜로 넣으면 콜백이 성공으로 불리는지. 펌웨어 원본은 `/lib/firmware`가 아니라 `/root/g10_uh.bin`에 둔다.

```sh
mkdir -p root && $MK root/g10_uh.bin 7 1 65536
```

```sh
D=/sys/bus/platform/devices/g10_fw
echo "sysctl: force_sysfs_fallback=$(cat /proc/sys/kernel/firmware_config/force_sysfs_fallback) ignore_sysfs_fallback=$(cat /proc/sys/kernel/firmware_config/ignore_sysfs_fallback) timeout=$(cat /sys/class/firmware/timeout)"
echo "--- (a) mode=2: request_firmware_nowait(FW_ACTION_NOUEVENT)"
insmod /g10_fw.ko mode=2 fw=g10_uh.bin
sleep 1
ls /sys/class/firmware/
ls /sys/class/firmware/g10_uh.bin/
echo "loading=$(cat /sys/class/firmware/g10_uh.bin/loading) fw_status=$(cat $D/fw_status)"
echo 1 > /sys/class/firmware/g10_uh.bin/loading
cat /root/g10_uh.bin > /sys/class/firmware/g10_uh.bin/data
echo 0 > /sys/class/firmware/g10_uh.bin/loading
sleep 1
echo "after manual load: fw_status=$(cat $D/fw_status) fw_version=$(cat $D/fw_version)"
rmmod g10_fw
echo "--- (b) force_sysfs_fallback=1, timeout=3, sync request for a missing file"
echo 1 > /proc/sys/kernel/firmware_config/force_sysfs_fallback
echo 3 > /sys/class/firmware/timeout
T0=$(cut -d' ' -f1 /proc/uptime); insmod /g10_fw.ko fw=g10_nofile.bin; T1=$(cut -d' ' -f1 /proc/uptime)
echo "insmod took: $T0 -> $T1 s"
dmesg | grep -E 'g10' | grep -v taints
```

```sh
limactl shell elinux -- bash -lc 'bash /Users/donh/.elinux_scratch/G10/g10_run.sh s4 ""'
```

```text
g10_uh.bin: v7.1 payload=65536 crc=0x64581251 file=65552
lab initramfs: 6.18.55 up 0.73s
sysctl: force_sysfs_fallback=0 ignore_sysfs_fallback=0 timeout=60
--- (a) mode=2: request_firmware_nowait(FW_ACTION_NOUEVENT)
g10_uh.bin  timeout
data       device     loading    power      subsystem  uevent
loading=0 fw_status=-11
after manual load: fw_status=0 fw_version=7.1
--- (b) force_sysfs_fallback=1, timeout=3, sync request for a missing file
[    6.150275] g10_fw g10_fw: request_firmware(g10_nofile.bin) failed: -110
[    6.151854] g10_fw g10_fw: probe with driver g10_fw failed with error -110
insmod took: 2.92 -> 6.12 s
[    0.855369] g10_fw g10_fw: Direct firmware load for g10_uh.bin failed with error -2
[    0.855455] g10_fw g10_fw: Falling back to sysfs fallback for: g10_uh.bin
[    0.855914] g10_fw g10_fw: probe: nowait queued (ret 0), probe returns
[    1.914982] g10_fw g10_fw: fw_cb: called, fw=OK
[    1.915382] g10_fw g10_fw: downloaded 65536 bytes in 16 chunks of 4096
[    1.915413] g10_fw g10_fw: fw g10_uh.bin v7.1 running (req 0 us, crc 219 us, dl 135 us)
[    2.982385] g10_fw g10_fw: Direct firmware load for g10_nofile.bin failed with error -2
[    2.982454] g10_fw g10_fw: Falling back to sysfs fallback for: g10_nofile.bin
[    6.150275] g10_fw g10_fw: request_firmware(g10_nofile.bin) failed: -110
[    6.151854] g10_fw g10_fw: probe with driver g10_fw failed with error -110
```

출력에서 볼 것:

- (a) 기본 sysctl은 `force_sysfs_fallback=0`, timeout `60`(초). `Direct firmware load ... failed with error -2` 다음에 `Falling back to sysfs fallback for: g10_uh.bin` — `FW_OPT_USERHELPER` 덕분에 fallback을 탔다. `/sys/class/firmware/g10_uh.bin/`에 `data`, `loading`, `device`(우리 장치로의 링크)가 생겼다.
- 셸이 `loading=1` → `data`에 64 KiB 쓰기 → `loading=0`을 하자 콜백이 `fw=OK`로 불렸고 버전 `7.1`. 커널 입장에서 이 바이트는 파일이 아니라 **유저가 sysfs로 밀어 넣은 버퍼**다. 그래도 드라이버의 CRC 검증은 똑같이 통과해야 한다.
- (b) `force_sysfs_fallback=1`로 바꾸고 timeout을 3초로 줄인 뒤 평범한 동기 `request_firmware()`로 없는 파일을 요청 → 아무도 채우지 않으니 **약 3초 뒤 `-110`(`-ETIMEDOUT`)**. insmod도 그만큼 멈췄다 (`/proc/uptime` 기준 2.92 → 6.12 s; dmesg 시각과 약간 어긋나는 것은 두 시계를 따로 읽었기 때문이다). timeout이 기본값 60이었다면 **probe가 60초 동안 멈췄을 것**이다 — 부팅 중 built-in 드라이버라면 부팅이 1분 멈춘다. "부팅이 가끔 60초씩 멈춘다"는 증상의 고전적 원인이다.

### 6.3 예제 10 — `request_firmware_direct`는 fallback을 절대 안 탄다

무엇을 확인하나: `force_sysfs_fallback=1`인 같은 상황에서 `mode=3`(direct)과 `mode=0`(일반)의 걸리는 시간을 비교한다.

```sh
echo 1 > /proc/sys/kernel/firmware_config/force_sysfs_fallback
echo 3 > /sys/class/firmware/timeout
for m in 3 0; do
  T0=$(cut -d' ' -f1 /proc/uptime); insmod /g10_fw.ko mode=$m fw=g10_opt.bin; T1=$(cut -d' ' -f1 /proc/uptime)
  echo "mode=$m: insmod $T0 -> $T1 s"; rmmod g10_fw
done
dmesg | grep -E 'g10' | grep -v taints
```

```sh
limactl shell elinux -- bash -lc 'bash /Users/donh/.elinux_scratch/G10/g10_run.sh s7 ""' | sed 's/\x1b\[[0-9;]*m//g'
```

```text
lab initramfs: 6.18.55 up 1.16s
[    1.514482] g10_fw g10_fw: request_firmware(g10_opt.bin) failed: -2
[    1.517452] g10_fw g10_fw: probe with driver g10_fw failed with error -2
mode=3: insmod 1.24 -> 1.29 s
[    4.812003] g10_fw g10_fw: request_firmware(g10_opt.bin) failed: -110
[    4.813362] g10_fw g10_fw: probe with driver g10_fw failed with error -110
mode=0: insmod 1.32 -> 4.59 s
[    1.514482] g10_fw g10_fw: request_firmware(g10_opt.bin) failed: -2
[    1.517452] g10_fw g10_fw: probe with driver g10_fw failed with error -2
[    1.591733] g10_fw g10_fw: Direct firmware load for g10_opt.bin failed with error -2
[    1.591846] g10_fw g10_fw: Falling back to sysfs fallback for: g10_opt.bin
[    4.812003] g10_fw g10_fw: request_firmware(g10_opt.bin) failed: -110
[    4.813362] g10_fw g10_fw: probe with driver g10_fw failed with error -110
```

출력에서 볼 것: direct는 약 0.05초 만에 조용히 `-2`(경고 로그도 없다 — `FW_OPT_NO_WARN`), 일반 요청은 fallback으로 가서 3초 기다린 뒤 `-110`. **있을 수도 없을 수도 있는 선택 파일**(보드별 보정 파일, 선택적 패치)은 `request_firmware_direct()` 또는 `firmware_request_nowarn()`으로 요청해야 이런 대기와 로그 오염을 피한다. (`firmware_request_nowarn`은 로그만 끄고 fallback 조건은 `request_firmware`와 같다 — 소스의 `FW_OPT_UEVENT | FW_OPT_NO_WARN`.)

---

## 7. 부팅 타이밍 — built-in 드라이버가 rootfs보다 먼저 펌웨어를 찾을 때

### 7.1 문제

`=y`(built-in)로 빌드한 드라이버의 probe는 커널 init 중 `device_initcall` 단계에서 돈다. 이때 **진짜 rootfs(eMMC의 ext4 등)는 아직 마운트되지 않았다**. `/lib/firmware`가 initramfs에 없다면, 그 시점의 `/lib/firmware`는 비어 있다.

- fallback이 꺼져 있으면: 즉시 `-ENOENT` → probe 실패 → 부팅 후 보면 Wi-Fi가 없다. 그런데 부팅 후 `rmmod`/`modprobe`(또는 unbind/bind)를 하면 멀쩡히 된다. "**insmod로 하면 되는데 부팅 때는 안 된다**"의 정체다.
- fallback이 강제로 켜져 있으면: uevent를 처리할 udev도 아직 없으니 **timeout(기본 60초)까지 부팅이 멈춘다**.

firmware_class는 initramfs에 대해서는 기다려 준다. `fw_get_filesystem_firmware()` 첫머리의 `wait_for_initramfs()`가 비동기로 풀리는 initramfs 압축 해제(`populate_rootfs`)가 끝날 때까지 기다린다. 그래서 **initramfs 안의 펌웨어는 built-in 드라이버도 찾을 수 있다**. 기다려 주지 않는 것은 "나중에 마운트될 진짜 rootfs"다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="120" y="40" width="220" height="64" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="230" y="64" text-anchor="middle" font-size="13">request_firmware()</text>
  <text x="230" y="86" text-anchor="middle" font-size="13">/lib/firmware 비어 있음 → -2</text>
  <rect x="420" y="40" width="230" height="64" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="535" y="64" text-anchor="middle" font-size="13">여기부터 /lib/firmware 보임</text>
  <text x="535" y="86" text-anchor="middle" font-size="13">모듈 probe·nowait는 안전</text>
  <rect x="430" y="140" width="220" height="20" fill="#3f9a6b" fill-opacity="0.12" stroke="none"/>
  <line x1="30" y1="150" x2="655" y2="150" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="60" y1="144" x2="60" y2="156" stroke="currentColor"/>
  <line x1="210" y1="144" x2="210" y2="156" stroke="currentColor"/>
  <line x1="430" y1="144" x2="430" y2="156" stroke="currentColor"/>
  <line x1="580" y1="144" x2="580" y2="156" stroke="currentColor"/>
  <line x1="210" y1="104" x2="210" y2="142" stroke="#d0564a" marker-end="url(#ah5)"/>
  <text x="60" y="176" text-anchor="middle" font-size="12">커널 시작</text>
  <text x="210" y="176" text-anchor="middle" font-size="12">built-in probe</text>
  <text x="210" y="192" text-anchor="middle" font-size="12">(device_initcall)</text>
  <text x="430" y="176" text-anchor="middle" font-size="12">rootfs 마운트</text>
  <text x="430" y="192" text-anchor="middle" font-size="12">(예: mmcblk0p2)</text>
  <text x="580" y="176" text-anchor="middle" font-size="12">init · udev</text>
  <text x="580" y="192" text-anchor="middle" font-size="12">모듈 자동 로드</text>
  <rect x="30" y="222" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="180" y="249" text-anchor="middle" font-size="13">① 펌웨어를 initramfs에 넣기</text>
  <rect x="350" y="222" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="249" text-anchor="middle" font-size="13">② 드라이버를 모듈(=m)로</text>
  <rect x="30" y="280" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="180" y="307" text-anchor="middle" font-size="13">③ CONFIG_EXTRA_FIRMWARE 내장</text>
  <rect x="350" y="280" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="307" text-anchor="middle" font-size="13">④ nowait·재시도·path 지정</text>
  <text x="30" y="214" font-size="13" font-weight="bold">해결책</text>
</svg>
```
그림 5 — built-in 드라이버의 probe는 rootfs 마운트 전에 돈다. 그 시점에 `/lib/firmware`에 파일이 있게 하거나(①③), 요청을 rootfs 이후로 미루거나(②④) 해야 한다.

### 7.2 예제 11 — race 재현: 펌웨어가 "나중에 마운트되는 디스크"에만 있을 때

무엇을 확인하나: built-in 드라이버를 쓰려면 커널을 다시 빌드해야 해서, 같은 상황을 모듈로 재현한다. 펌웨어는 virtio 디스크(ext4 이미지)의 `/lib/firmware`에만 있고 initramfs에는 없다. 디스크를 마운트하기 **전에** 드라이버를 올리면 실패하고, 마운트 후 `firmware_class.path`를 바꾸고 `bind`로 probe를 다시 돌리면 성공하는지 본다.

```sh
limactl shell elinux -- bash -lc 'W=/var/tmp/G10; rm -rf $W/disk && mkdir -p $W/disk/lib/firmware \
  && python3 /Users/donh/.elinux_scratch/G10/g10_mkfw.py $W/disk/lib/firmware/g10_dev.bin 3 1 131072 \
  && rm -f $W/g10_root.img && mkfs.ext4 -q -d $W/disk $W/g10_root.img 16M \
  && QEXTRA="-drive file=$W/g10_root.img,format=raw,if=virtio,readonly=on" bash /Users/donh/.elinux_scratch/G10/g10_run.sh s5 ""'
```

```sh
D=/sys/bus/platform/devices/g10_fw; DRV=/sys/bus/platform/drivers/g10_fw
echo "--- t=$(cut -d' ' -f1 /proc/uptime)s: driver probes, rootfs (/dev/vda) not mounted yet"
ls /lib/firmware/
insmod /g10_fw.ko
echo "bound? $(ls $D/driver >/dev/null 2>&1 && echo yes || echo no)"
echo "--- t=$(cut -d' ' -f1 /proc/uptime)s: 'real rootfs' mounted"
mount -t ext4 -o ro /dev/vda /mnt && ls /mnt/lib/firmware/
echo /mnt/lib/firmware > /sys/module/firmware_class/parameters/path
echo g10_fw > $DRV/bind
echo "bound? $(ls $D/driver >/dev/null 2>&1 && echo yes || echo no), fw_version=$(cat $D/fw_version)"
dmesg | grep -E 'g10' | grep -v taints
```

```text
g10_dev.bin: v3.1 payload=131072 crc=0x25f70869 file=131088
lab initramfs: 6.18.55 up 0.87s
--- t=0.93s: driver probes, rootfs (/dev/vda) not mounted yet
[    1.022351] g10_fw g10_fw: request_firmware(g10_dev.bin) failed: -2
[    1.024248] g10_fw g10_fw: probe with driver g10_fw failed with error -2
bound? no
--- t=0.99s: 'real rootfs' mounted
g10_dev.bin
bound? yes, fw_version=3.1
[    1.022079] g10_fw g10_fw: Direct firmware load for g10_dev.bin failed with error -2
[    1.022351] g10_fw g10_fw: request_firmware(g10_dev.bin) failed: -2
[    1.024248] g10_fw g10_fw: probe with driver g10_fw failed with error -2
[    1.105339] g10_fw g10_fw: got g10_dev.bin: 131088 bytes
[    1.105815] g10_fw g10_fw: downloaded 131072 bytes in 32 chunks of 4096
[    1.105852] g10_fw g10_fw: fw g10_dev.bin v3.1 running (req 5621 us, crc 283 us, dl 154 us)
```

출력에서 볼 것: 첫 probe는 `-2`로 실패하고 장치는 unbound. 디스크를 `/mnt`에 마운트하고 `firmware_class.path=/mnt/lib/firmware`로 바꾼 뒤 `bind` 한 번에 v3.1이 올라왔다. 실제 시스템에서는 rootfs가 `/`에 마운트되므로 path를 바꿀 필요 없이 rebind만 하면 된다. **같은 드라이버, 같은 파일인데 "언제 요청했나"만으로 결과가 갈린다.**

### 7.3 해결책 네 가지 비교

| 방법 | 어떻게 | 장점 | 단점 |
|---|---|---|---|
| ① initramfs에 펌웨어 | dracut/initramfs-tools가 `modinfo`의 `firmware:` 줄(= `MODULE_FIRMWARE`)을 보고 복사. Yocto는 initramfs 이미지 레시피에 `linux-firmware-<칩>` 패키지 추가 | 커널 재빌드 불필요, built-in·모듈 모두 OK | initramfs가 커진다, 펌웨어 업데이트 시 initramfs 재생성 필요 |
| ② 드라이버를 모듈로 | `CONFIG_FOO=m`, rootfs 이후 udev가 modalias로 로드 (G9) | 가장 단순, 배포판 기본 | 부팅 초기에 필요한 장치(예: 루트 저장장치 펌웨어)에는 못 씀 |
| ③ 커널에 내장 | `CONFIG_EXTRA_FIRMWARE="brcm/xxx.bin"`, `CONFIG_EXTRA_FIRMWARE_DIR="/lib/firmware"` → 커널 이미지 안 `.rodata`에 들어가고 `_request_firmware_prepare()`가 먼저 찾음 | 파일시스템 전혀 불필요, 가장 빠름 | 펌웨어 바꾸려면 커널 재빌드. **GPL 커널 이미지에 비GPL 블롭을 합쳐 배포**하는 라이선스 문제 검토 필요 |
| ④ 요청을 늦추기 | `request_firmware_nowait` + 실패 시 재시도(지연 work), 또는 펌웨어가 필요한 시점(예: `ndo_open`, 첫 `open()`)에 요청 | 부팅 지연 없음 | 드라이버가 복잡해짐. "언제 다시 시도하나"를 정해야 함 |

③은 이 노트에서 실행하지 않았다 (랩 커널 `CONFIG_EXTRA_FIRMWARE=""`, 재빌드 필요). Kconfig 도움말(`drivers/base/firmware_loader/Kconfig`)의 설명을 그대로 옮기면: `CONFIG_EXTRA_FIRMWARE="usb8388.bin"`으로 두고 파일을 `/lib/firmware`에 복사해 커널을 빌드하면, "any request_firmware("usb8388.bin") will be satisfied internally inside the kernel without ever looking at your filesystem at runtime."

`-EPROBE_DEFER`로 해결할 수 없을까? deferred probe(G3)는 **다른 드라이버가 bind될 때** 재시도가 걸린다. 파일시스템 마운트는 드라이버 bind 이벤트가 아니므로, 펌웨어 없음을 `-EPROBE_DEFER`로 돌려주면 우연히 다른 드라이버가 늦게 붙을 때만 재시도되고 영영 안 될 수도 있다. late_initcall 시점의 일괄 재시도가 있긴 하지만 rootfs 마운트보다 앞선다. 그래서 펌웨어 없음에 `-EPROBE_DEFER`를 쓰는 것은 권장되지 않는다.

---

## 8. 펌웨어 캐시 — suspend/resume 때 왜 필요한가

resume 콜백에서 칩 전원이 다시 들어오면 펌웨어를 다시 내려보내야 하는 장치가 많다 (Wi-Fi 칩은 suspend 동안 전원이 꺼지는 경우가 흔하다). 그런데 resume 초반에는 저장장치 드라이버가 아직 깨어나지 않았을 수 있어서 `/lib/firmware`를 읽다가 데드락이나 실패가 생길 수 있다.

`CONFIG_FW_CACHE=y`이면 firmware_class가 이것을 대신 해결한다.

1. `request_firmware()`가 성공할 때 `assign_fw()`가 **장치의 devres에 펌웨어 이름을 기록**한다 (`fw_add_devm_name()`). 예제 7의 `fw_name_devm_release: fw_name-g10_dev.bin ... released`가 장치가 unbind될 때 그 기록이 지워지는 흔적이다.
2. suspend 직전 PM notifier가 기록된 이름들의 펌웨어를 **미리 메모리에 읽어 둔다**.
3. resume 중 드라이버가 같은 이름으로 `request_firmware()`를 부르면 파일시스템 대신 캐시에서 받는다.
4. resume 후 10초 지나면 캐시를 비운다.

예제 12 — 소스 확인. 무엇을 확인하나: 위 2·4단계가 PM notifier에 있는지.

```sh
limactl shell elinux -- bash -lc 'sed -n 1615,1641p /var/lab/src/linux-6.18.55/drivers/base/firmware_loader/main.c'
```

```text
static int fw_pm_notify(struct notifier_block *notify_block,
			unsigned long mode, void *unused)
{
	switch (mode) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
	case PM_RESTORE_PREPARE:
		/*
		 * Here, kill pending fallback requests will only kill
		 * non-uevent firmware request to avoid stalling suspend.
		 */
		kill_pending_fw_fallback_reqs(false);
		device_cache_fw_images();
		break;

	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
		/*
		 * In case that system sleep failed and syscore_suspend is
		 * not called.
		 */
		mutex_lock(&fw_lock);
		fw_cache.state = FW_LOADER_NO_CACHE;
		mutex_unlock(&fw_lock);

		device_uncache_fw_images_delay(10 * MSEC_PER_SEC);
		break;
	}
```

출력에서 볼 것: `PM_SUSPEND_PREPARE`에서 `device_cache_fw_images()`, `PM_POST_SUSPEND`에서 `device_uncache_fw_images_delay(10 * MSEC_PER_SEC)`. 실제 suspend/resume은 이 랩에서 실행하지 않았다 (QEMU `virt` + BusyBox에서 깨우기 경로를 따로 꾸며야 함). 드라이버 쪽 교훈은 하나다: **resume에서도 `request_firmware()`를 그냥 불러도 된다**(소스 주석: "The function can be called safely inside device's suspend and resume callback"). 단 probe 때 한 번 성공적으로 받아 둔 이름이어야 캐시 대상이 된다. 이름을 resume 때 처음 바꿔 부르면 캐시에 없다.

---

## 9. 크기와 시간 — 펌웨어가 크면 얼마나 걸리나

### 9.1 예제 13 — 64 KiB ~ 8 MiB 측정

무엇을 확인하나: 동기 모드로 크기별 펌웨어를 3번씩 올리고 `req`(파일→커널 버퍼, sha256 디버그 포함), `crc`(CRC32 검증), `dl`(4 KiB chunk 복사 + read-back) 시간을 잰다.

```sh
for s in 64k:65536 256k:262144 1m:1048576 4m:4194304 8m:8388608; do $MK lib/firmware/g10_${s%%:*}.bin 1 0 ${s##*:}; done
```

```sh
for f in g10_64k g10_256k g10_1m g10_4m g10_8m; do for r in 1 2 3; do
  insmod /g10_fw.ko fw=$f.bin; rmmod g10_fw
done; done
dmesg | grep running
```

```sh
limactl shell elinux -- bash -lc 'bash /Users/donh/.elinux_scratch/G10/g10_run.sh s6 ""'
```

```text
g10_64k.bin: v1.0 payload=65536 crc=0x0b0afef1 file=65552
g10_256k.bin: v1.0 payload=262144 crc=0xb1f78de3 file=262160
g10_1m.bin: v1.0 payload=1048576 crc=0x334cc221 file=1048592
g10_4m.bin: v1.0 payload=4194304 crc=0x33225a3e file=4194320
g10_8m.bin: v1.0 payload=8388608 crc=0x22b60ec6 file=8388624
lab initramfs: 6.18.55 up 0.83s
[    0.000000] arch_timer: cp15 timer running at 62.50MHz (virt).
[    0.957791] g10_fw g10_fw: fw g10_64k.bin v1.0 running (req 2258 us, crc 224 us, dl 147 us)
[    1.002758] g10_fw g10_fw: fw g10_64k.bin v1.0 running (req 851 us, crc 116 us, dl 108 us)
[    1.037841] g10_fw g10_fw: fw g10_64k.bin v1.0 running (req 1191 us, crc 121 us, dl 121 us)
[    1.075822] g10_fw g10_fw: fw g10_256k.bin v1.0 running (req 2523 us, crc 305 us, dl 229 us)
[    1.293574] g10_fw g10_fw: fw g10_256k.bin v1.0 running (req 52500 us, crc 2689 us, dl 801 us)
[    1.389346] g10_fw g10_fw: fw g10_256k.bin v1.0 running (req 3610 us, crc 455 us, dl 239 us)
[    1.613407] g10_fw g10_fw: fw g10_1m.bin v1.0 running (req 34114 us, crc 1414 us, dl 661 us)
[    1.814966] g10_fw g10_fw: fw g10_1m.bin v1.0 running (req 10031 us, crc 1071 us, dl 717 us)
[    1.888197] g10_fw g10_fw: fw g10_1m.bin v1.0 running (req 9759 us, crc 1054 us, dl 640 us)
[    1.975522] g10_fw g10_fw: fw g10_4m.bin v1.0 running (req 37179 us, crc 6258 us, dl 2449 us)
[    2.058919] g10_fw g10_fw: fw g10_4m.bin v1.0 running (req 36254 us, crc 4762 us, dl 2559 us)
[    2.164343] g10_fw g10_fw: fw g10_4m.bin v1.0 running (req 50225 us, crc 4329 us, dl 2522 us)
[    2.306133] g10_fw g10_fw: fw g10_8m.bin v1.0 running (req 82982 us, crc 8406 us, dl 4849 us)
[    2.578382] g10_fw g10_fw: fw g10_8m.bin v1.0 running (req 168368 us, crc 29009 us, dl 5481 us)
[    2.746873] g10_fw g10_fw: fw g10_8m.bin v1.0 running (req 87285 us, crc 10474 us, dl 4851 us)
```

(`arch_timer` 줄은 grep `running`에 걸린 무관한 줄이다.) 측정값은 TCG 에뮬레이션 + 같은 VM에서 다른 작업이 동시에 돌던 상태라 흔들림이 크다 (256 KiB의 52.5 ms, 8 MiB의 168 ms 같은 튀는 값). 그래서 3회의 **중앙값**으로 그린다.

| 크기 | req 중앙값 | crc 중앙값 | dl 중앙값 | req 처리량 |
|---|---|---|---|---|
| 64 KiB | 1.19 ms | 0.12 ms | 0.12 ms | 약 55 MB/s |
| 256 KiB | 3.61 ms | 0.46 ms | 0.24 ms | 약 73 MB/s |
| 1 MiB | 10.0 ms | 1.07 ms | 0.66 ms | 약 105 MB/s |
| 4 MiB | 37.2 ms | 4.76 ms | 2.52 ms | 약 113 MB/s |
| 8 MiB | 87.3 ms | 10.5 ms | 4.85 ms | 약 96 MB/s |

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <line x1="80" y1="330" x2="600" y2="330" stroke="currentColor"/>
  <line x1="80" y1="330" x2="80" y2="50" stroke="currentColor"/>
  <line x1="80" y1="236.7" x2="600" y2="236.7" stroke="#888" stroke-opacity="0.3"/>
  <line x1="80" y1="143.3" x2="600" y2="143.3" stroke="#888" stroke-opacity="0.3"/>
  <line x1="80" y1="50.0" x2="600" y2="50.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="74" y="334.0" text-anchor="end" font-size="12">100 us</text>
  <text x="74" y="240.7" text-anchor="end" font-size="12">1 ms</text>
  <text x="74" y="147.3" text-anchor="end" font-size="12">10 ms</text>
  <text x="74" y="54.0" text-anchor="end" font-size="12">100 ms</text>
  <line x1="80.0" y1="330" x2="80.0" y2="335" stroke="currentColor"/>
  <line x1="228.6" y1="330" x2="228.6" y2="335" stroke="currentColor"/>
  <line x1="377.1" y1="330" x2="377.1" y2="335" stroke="currentColor"/>
  <line x1="525.7" y1="330" x2="525.7" y2="335" stroke="currentColor"/>
  <line x1="600.0" y1="330" x2="600.0" y2="335" stroke="currentColor"/>
  <text x="80.0" y="350" text-anchor="middle" font-size="12">64K</text>
  <text x="228.6" y="350" text-anchor="middle" font-size="12">256K</text>
  <text x="377.1" y="350" text-anchor="middle" font-size="12">1M</text>
  <text x="525.7" y="350" text-anchor="middle" font-size="12">4M</text>
  <text x="600.0" y="350" text-anchor="middle" font-size="12">8M</text>
  <text x="340" y="374" text-anchor="middle" font-size="13">펌웨어 payload 크기 (로그 축)</text>
  <text x="22" y="190" text-anchor="middle" font-size="13" transform="rotate(-90 22 190)">시간 (로그 축, 3회 중앙값)</text>
  <polyline points="80.0,229.6 228.6,184.6 377.1,143.2 525.7,90.1 600.0,55.5" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <circle cx="80.0" cy="229.6" r="3.5" fill="#4a7bd0"/>
  <circle cx="228.6" cy="184.6" r="3.5" fill="#4a7bd0"/>
  <circle cx="377.1" cy="143.2" r="3.5" fill="#4a7bd0"/>
  <circle cx="525.7" cy="90.1" r="3.5" fill="#4a7bd0"/>
  <circle cx="600.0" cy="55.5" r="3.5" fill="#4a7bd0"/>
  <polyline points="80.0,322.3 228.6,268.6 377.1,233.9 525.7,173.4 600.0,141.5" fill="none" stroke="#e08a3c" stroke-width="2"/>
  <circle cx="80.0" cy="322.3" r="3.5" fill="#e08a3c"/>
  <circle cx="228.6" cy="268.6" r="3.5" fill="#e08a3c"/>
  <circle cx="377.1" cy="233.9" r="3.5" fill="#e08a3c"/>
  <circle cx="525.7" cy="173.4" r="3.5" fill="#e08a3c"/>
  <circle cx="600.0" cy="141.5" r="3.5" fill="#e08a3c"/>
  <polyline points="80.0,322.3 228.6,294.7 377.1,253.4 525.7,199.2 600.0,172.7" fill="none" stroke="#3f9a6b" stroke-width="2"/>
  <circle cx="80.0" cy="322.3" r="3.5" fill="#3f9a6b"/>
  <circle cx="228.6" cy="294.7" r="3.5" fill="#3f9a6b"/>
  <circle cx="377.1" cy="253.4" r="3.5" fill="#3f9a6b"/>
  <circle cx="525.7" cy="199.2" r="3.5" fill="#3f9a6b"/>
  <circle cx="600.0" cy="172.7" r="3.5" fill="#3f9a6b"/>
  <text x="608" y="59.5" font-size="12">87.3 ms</text>
  <text x="608" y="145.5" font-size="12">10.5 ms</text>
  <text x="608" y="176.7" font-size="12">4.9 ms</text>
  <line x1="100" y1="22" x2="120" y2="22" stroke="#4a7bd0" stroke-width="2"/>
  <text x="126" y="26" font-size="12">request_firmware</text>
  <line x1="265" y1="22" x2="285" y2="22" stroke="#e08a3c" stroke-width="2"/>
  <text x="291" y="26" font-size="12">CRC32 검증</text>
  <line x1="384" y1="22" x2="404" y2="22" stroke="#3f9a6b" stroke-width="2"/>
  <text x="410" y="26" font-size="12">4 KiB chunk 다운로드 + read-back</text>
</svg>
```
그림 6 — 크기별 시간 (양축 로그, 예제 13의 3회 중앙값). 세 선 모두 기울기가 거의 1, 즉 **크기에 비례**한다. 64 KiB에서 CRC와 다운로드 점은 같은 값(121 us)이라 겹쳐 있다.

읽는 법:

- 모두 크기에 선형이다. 고정 비용(경로 4개 탐색, 버퍼 할당)은 64 KiB에서도 1 ms 안쪽이라 큰 펌웨어에서는 무시된다.
- `req`가 가장 크다. 여기에는 파일 읽기(initramfs = 메모리 속 파일 → vmalloc 버퍼 복사)와 함께 `CONFIG_FW_LOADER_DEBUG`의 **SHA-256 계산**이 들어 있다. 같은 바이트를 한 번 훑는 CRC32(10.5 ms)보다 8배 느린 것은 SHA-256이 CRC보다 훨씬 비싼 연산이기 때문이다. 이 둘을 분리해 재지는 않았다 (분리하려면 `FW_LOADER_DEBUG`를 끈 커널을 따로 빌드해야 한다). 양산 커널에서는 이 옵션을 끄는 것이 보통이다.
- 실제 보드에서는 이 숫자들이 훨씬 작지만(네이티브 CPU), **진짜 병목은 대개 버스**다. 예를 들어 400 kHz I2C의 이론 대역폭은 약 40 KB/s 수준이라 256 KiB 터치 펌웨어도 수 초가 걸린다. 25 MHz SPI면 약 3 MB/s. 그래서 큰 펌웨어를 느린 버스로 보내는 드라이버일수록 `nowait`(§5)가 필요하다.

---

## 10. 칩에 내려보내기, 버전 보고, 그리고 linux-firmware

### 10.1 다운로드 패턴 세 가지

`fw->data`를 받은 뒤의 일은 칩 ROM 부트로더의 프로토콜이 정한다. 크게 셋이다.

| 패턴 | 어떻게 | 예 | Don 경험과 연결 |
|---|---|---|---|
| MMIO 창 | 칩 SRAM이 SoC 주소 공간에 매핑됨 → `memcpy_toio()`로 쓰고 "boot" 레지스터 write | SoC 내부 DSP, PCIe Wi-Fi(BAR 창) | SSD 컨트롤러 내부 코어 간 이미지 로딩 |
| 버스 chunk 전송 | I2C/SPI/UART로 "주소+길이+데이터+체크섬" 패킷을 반복, 매 패킷 ACK/상태 폴링 | 터치 컨트롤러, MCU, BT(HCI vendor command) | NVMe Firmware Image Download, RF 칩 SPI 다운로드 |
| DMA | `dma_alloc_coherent()` 버퍼에 복사 → 칩 DMA 엔진에 주소·길이 전달 → 완료 인터럽트 | PCIe NIC/Wi-Fi, GPU, NPU | SSD의 호스트 메모리 DMA |

버스 chunk 전송의 전형적인 모양 (실행하지 않은 스케치 — 레지스터 이름과 패킷 형식은 가상의 칩 기준이다):

```c
/* 가상의 I2C 터치 칩: [cmd=0xA0][addr 4B][len 2B][data...][sum 1B] → status 레지스터 폴링 */
static int tp_download(struct i2c_client *c, const u8 *img, size_t len)
{
	u8 pkt[1 + 4 + 2 + 128 + 1];
	size_t off;

	for (off = 0; off < len; off += 128) {
		size_t n = min_t(size_t, 128, len - off);
		u8 sum = 0;
		int i, ret, st;

		pkt[0] = 0xA0;
		put_unaligned_le32(off, &pkt[1]);
		put_unaligned_le16(n, &pkt[5]);
		memcpy(&pkt[7], img + off, n);
		for (i = 0; i < 7 + n; i++)
			sum += pkt[i];
		pkt[7 + n] = sum;
		ret = i2c_master_send(c, pkt, 8 + n);
		if (ret < 0)
			return ret;
		ret = read_poll_timeout(i2c_smbus_read_byte_data, st, st != 0x01,
					200, 50000, false, c, 0xF0);  /* 0x01 = busy */
		if (ret || st != 0x00)
			return ret ? ret : -EIO;               /* 0x00 = ok */
	}
	return 0;
}
```

이 노트의 `g10_download()`의 "chunk 쓰기 + read-back"이 이 루프의 축소판이다. 실무에서 신경 쓸 것: chunk 크기는 칩 버퍼와 버스 최대 전송 크기(I2C 어댑터 제한)에 맞춘다, chunk마다 timeout을 둔다, 실패하면 칩을 리셋 GPIO로 ROM 모드에 되돌린 뒤 처음부터 다시 한다, 다운로드 후 칩이 보고하는 버전·CRC를 읽어 호스트 값과 비교한다.

### 10.2 버전 보고 — sysfs와 dmesg

현장에서 "이 장비의 Wi-Fi 펌웨어 버전이 뭐냐"에 답할 수 있어야 한다. 드라이버가 할 일:

- probe/콜백에서 한 줄 `dev_info()`: 이름·버전·(가능하면) 칩이 보고한 빌드 날짜. 예제 4의 `fw g10_dev.bin v1.2 running`.
- sysfs 읽기 전용 속성 (`fw_version`, G8). 이 노트의 드라이버는 `.dev_groups`로 붙였다. 네트워크 드라이버는 `ethtool -i`의 `firmware-version`처럼 서브시스템별 표준 자리가 있으면 그것을 쓴다.
- 버전 비교 정책: 헤더 버전이 드라이버가 아는 최소 버전보다 낮으면 경고하거나 거부한다. 칩 API가 펌웨어 버전마다 다르면 드라이버가 버전별로 분기해야 한다 (`iwlwifi`가 파일 이름에 API 번호를 넣고 여러 번호를 차례로 시도하는 것이 이 문제 때문이다).

### 10.3 반대 방향 — `firmware_upload` (장치 flash에 굽기)

지금까지는 "매 부팅마다 RAM에 올리는" 펌웨어였다. 칩에 자기 flash가 있고 그 flash를 **업데이트**하고 싶다면 방향이 반대다: 유저가 파일을 커널에 주고, 드라이버가 칩 flash에 굽는다. Don의 NVMe `Firmware Download/Commit`과 같은 일이다. 커널에는 이를 위한 공용 프레임워크 `firmware_upload_register()`(`CONFIG_FW_UPLOAD`, 랩 커널은 `=y`)가 있어 `/sys/class/firmware/<name>/` 아래 `loading`·`data`·`status`·`error`·`cancel` 등으로 진행 상황을 보여 준다. 이 노트에서는 실행하지 않았다. 자세한 인터페이스는 `Documentation/driver-api/firmware/fw_upload.rst`.

### 10.4 linux-firmware 저장소와 라이선스

배포판의 `/lib/firmware`는 대부분 **linux-firmware** 저장소(git.kernel.org의 `firmware/linux-firmware.git`)에서 온다. 특징:

- 커널 소스와 **별도 저장소**다. 블롭 대부분이 GPL이 아니라 벤더의 "재배포 허용" 라이선스이기 때문이다. 각 파일의 출처·버전·라이선스는 저장소 최상위 **`WHENCE`** 파일에 드라이버별로 적혀 있고, 라이선스 원문은 `LICENCE.<vendor>` / `LICENSE.<vendor>` 파일로 들어 있다.
- 받아들이는 조건은 "재배포 가능"이다. 소스 공개는 요구하지 않는다.
- 배포판은 이를 패키지(`linux-firmware`)로 깔고, Yocto는 `linux-firmware` 레시피를 칩별 작은 패키지(`linux-firmware-bcm43455` 같은 형태)로 쪼개서 필요한 것만 이미지에 넣게 한다. Buildroot도 칩별 선택 옵션이 있다. 임베디드에서는 **필요한 파일만** 넣어 rootfs를 줄인다.
- 제품에 넣을 때 확인할 것: 해당 블롭의 라이선스가 상업 재배포를 허용하는지, 보드별 NVRAM/보정 파일은 linux-firmware에 없고 **모듈 벤더에게 따로 받아야** 한다는 점 (Wi-Fi 모듈의 `.txt` NVRAM이 대표적).

예제 14 — 실제 배포판의 `/lib/firmware` 보기. 무엇을 확인하나: VM 호스트(Ubuntu 클라우드 이미지)의 `/lib/firmware` 구성.

```sh
limactl shell elinux -- bash -lc 'ls -la /lib/firmware/; du -sh /lib/firmware; find /lib/firmware/7.0.0-38-generic -type f | sed "s/.*\.//" | sort | uniq -c | sort -rn | head -5'
```

```text
total 96
drwxr-xr-x  4 root root  4096 Oct 10 09:58 .
drwxr-xr-x 99 root root  4096 Oct 10 13:34 ..
drwxr-xr-x  3 root root  4096 Sep 27 09:28 7.0.0-34-generic
drwxr-xr-x  3 root root  4096 Oct  6 07:26 7.0.0-38-generic
-rw-r--r--  1 root root  6380 Aug 17 10:26 regulatory.db
-rw-r--r--  1 root root  1085 Aug 17 10:26 regulatory.db.p7s
-rw-r--r--  1 root root 67152 Dec  6  2025 skl_hda_dsp_generic-tplg.bin
209M	/lib/firmware
   1637 dtb
    100 dtbo
```

출력에서 볼 것:

- 이 VM은 클라우드 이미지라 `linux-firmware` 패키지가 **깔려 있지 않다** (보통 데스크톱 Ubuntu에는 수백 MB의 칩별 디렉터리가 있다). 실제 Wi-Fi가 없는 가상 머신이라 필요 없기 때문이다.
- `7.0.0-38-generic/` — fw_path[]의 `/lib/firmware/$(uname -r)` 자리가 실제로 쓰이는 예다. Ubuntu는 여기에 **커널 버전별 DTB/DTBO**를 둔다 (1637개 `.dtb`). DT 오버레이를 `request_firmware()`로 읽는 드라이버가 있으므로(D6) 펌웨어 경로에 DTB가 있는 것이 이상한 일이 아니다.
- `regulatory.db` + `regulatory.db.p7s` — 무선 규제 데이터베이스(wireless-regdb)와 그 **PKCS#7 서명**. cfg80211이 `request_firmware()`로 둘 다 읽고 커널에 내장된 키로 서명을 검증한다 (§4.3의 "커널이 검증하는" 드문 예). 칩 펌웨어는 아니지만 같은 API를 쓴다.

---

## 11. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="16" width="360" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="38" text-anchor="middle" font-size="13">request_firmware() / nowait → work</text>
  <rect x="20" y="62" width="360" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="84" text-anchor="middle" font-size="13">_request_firmware_prepare()</text>
  <rect x="20" y="108" width="360" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="130" text-anchor="middle" font-size="13">prepare_kernel_cred(&amp;init_task)</text>
  <rect x="20" y="154" width="360" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="176" text-anchor="middle" font-size="13">fw_get_filesystem_firmware()</text>
  <rect x="20" y="200" width="360" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="222" text-anchor="middle" font-size="13">kernel_read_file_from_path_initns()</text>
  <rect x="20" y="246" width="360" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="200" y="268" text-anchor="middle" font-size="13">firmware_fallback_platform / _sysfs()</text>
  <rect x="20" y="292" width="360" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="200" y="314" text-anchor="middle" font-size="13">assign_fw() → fw_add_devm_name()</text>
  <rect x="20" y="338" width="360" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="200" y="360" text-anchor="middle" font-size="13">*firmware_p = fw {size, data}</text>
  <line x1="200" y1="50" x2="200" y2="60" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="200" y1="96" x2="200" y2="106" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="200" y1="142" x2="200" y2="152" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="200" y1="188" x2="200" y2="198" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="200" y1="234" x2="200" y2="244" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="200" y1="280" x2="200" y2="290" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="200" y1="326" x2="200" y2="336" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="395" y="38" font-size="12">nowait도 kworker에서 같은 경로</text>
  <text x="395" y="84" font-size="12">내장 fw, 같은 이름 요청 합치기</text>
  <text x="395" y="130" font-size="12">커널 자격으로 파일 읽기</text>
  <text x="395" y="176" font-size="12">wait_for_initramfs() + fw_path[]</text>
  <text x="395" y="222" font-size="12">init의 mount ns 기준, 버퍼 할당</text>
  <text x="395" y="268" font-size="12">파일 실패 시에만 (§6)</text>
  <text x="395" y="314" font-size="12">suspend 캐시용 이름 기록 (§8)</text>
  <text x="395" y="360" font-size="12">드라이버가 release_firmware()</text>
</svg>
```
그림 7 — `request_firmware()` 내부 호출 경로 (6.18 `drivers/base/firmware_loader/main.c`, `fallback.c`). 오른쪽은 각 단계가 하는 일.

단계별로 보면:

- **`_request_firmware_prepare()`**: `CONFIG_EXTRA_FIRMWARE`로 내장된 펌웨어가 있으면 바로 돌려준다(`firmware_request_builtin_buf()`). 아니면 `struct fw_priv`를 찾거나 만든다 — 이름이 같은 요청이 동시에 진행 중이면 **같은 `fw_priv`를 공유**해서 파일을 한 번만 읽는다(예제 4의 `__allocate_fw_priv` 줄). 여러 개의 같은 칩(예: 카메라 4개)이 동시에 probe할 때 유용하다.
- **`prepare_kernel_cred(&init_task)` + `override_creds()`**: 요청한 스레드가 어떤 유저 프로세스(예: `insmod`를 부른 셸)의 문맥이어도 **커널 자격으로** 파일을 연다. 소스 주석: "we may have been called by a driver when serving an unrelated request from userland".
- **`fw_get_filesystem_firmware()`**: `wait_for_initramfs()`로 initramfs 해제를 기다린 뒤 `fw_path[]`를 돈다(§3). 실패 로그 `loading ... failed for no such file or directory`는 `dev_dbg`라 평소에 안 보이고, 최종 실패만 `dev_warn`(`Direct firmware load for ... failed with error -2`)으로 보인다.
- **`kernel_read_file_from_path_initns()`**: **init 프로세스의 마운트 네임스페이스** 기준으로 경로를 연다. 컨테이너 안에서 드라이버를 로드해도 호스트의 `/lib/firmware`를 읽는다는 뜻이다. `READING_FIRMWARE` id로 LSM/IMA 훅이 걸린다 (§4.3).
- **fallback**: platform(EFI 내장 사본, `firmware_request_platform`만), sysfs(§6).
- **`assign_fw()`**: 상태를 done으로 바꾸고, FW_CACHE면 장치 devres에 이름을 기록한다(§8). 이후 `fw->data`/`fw->size`가 채워져 드라이버에 돌아간다.
- **`release_firmware()`**: `fw_priv` 참조를 내려 마지막이면 버퍼를 해제한다(예제 4의 `__free_fw_priv`).

드라이버를 쓸 때 다시 만나는 지점:

- probe에서의 요청과 실패 처리 → G3 (probe/remove, devm, deferred probe)
- 버전·상태 노출 → G8 (`DEVICE_ATTR_RO`, `.dev_groups`)
- `MODULE_FIRMWARE`, 자동 모듈 로드와 initramfs → G9
- 보조 코어에 ELF 이미지를 올리고 부팅·종료 관리 → O2 (remoteproc: `rproc_boot()`가 내부에서 `request_firmware()`로 이미지를 읽고 ELF 세그먼트를 장치 메모리에 복사한다)
- DT 오버레이를 펌웨어 경로에서 읽는 경우 → D6

---

## 12. Embedded 실무에서 다시 보기

### 12.1 센서 키트의 Wi-Fi 모듈이 부팅 때만 안 뜬다

i.MX8 보드에 SDIO Wi-Fi 모듈(Broadcom/Infineon 계열, `brcmfmac`)을 붙였다. 증상: 부팅 후 `wlan0`이 없고 dmesg에

```text
brcmfmac mmc1:0001:1: Direct firmware load for brcm/brcmfmac43455-sdio.xxx,board.bin failed with error -2
```

(예시 형태 — 실제 파일 이름은 칩과 DT의 보드 compatible에 따라 다르다. 이 블록은 실행 출력이 아니다.) 진단 순서:

1. `ls /lib/firmware/brcm/`에 파일이 있나? 있다면 이름이 **정확히** 같은가 (`brcmfmac`은 보드별 이름을 먼저 시도하고 일반 이름으로 fallback하므로 경고가 한두 줄 나는 것은 정상일 수 있다 — 마지막에 성공했는지 본다).
2. 커널 설정: 드라이버가 `=y`인가 `=m`인가? `=y`이고 파일이 rootfs에만 있다면 §7의 race다. `zcat /proc/config.gz` 또는 `/boot/config-*`로 확인.
3. 파일이 `.zst`로만 깔렸는데 커널에 `FW_LOADER_COMPRESS_ZSTD`가 없는가? (§3.5)
4. 보드별 NVRAM `.txt`가 있는가? 이것은 linux-firmware에 없고 모듈 벤더에게 받는다.
5. 해결 후 확인: `echo <dev> > /sys/bus/sdio/drivers/brcmfmac/unbind` 후 `bind`, 또는 `modprobe -r brcmfmac && modprobe brcmfmac`.

### 12.2 보조 코어(Cortex-M4/M7, DSP)에 이미지 올리기

i.MX8M의 Cortex-M 코어나 TI AM62의 R5F/DSP에 실시간 모터 제어 펌웨어를 올리는 일은 remoteproc(O2)가 맡는다. DT에 `firmware-name = "imx8mp_m7_tcm.elf";` 같은 속성을 두거나 sysfs로 이름을 바꾼 뒤:

```sh
echo imx8mp_m7_tcm.elf > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
```

(실행하지 않은 예 — 보드와 파일 이름은 가정이다.) 안에서는 `request_firmware()`로 ELF를 읽고 세그먼트를 TCM/DDR에 복사한 뒤 코어 리셋을 푼다. 이 노트의 g10 드라이버와 같은 구조에 "ELF 파싱"이 추가된 것이다. 펌웨어를 바꿔 다시 올리는 개발 루프가 `/lib/firmware` 교체 + stop/start라서, §3의 경로 규칙을 알아야 "왜 새 파일이 안 먹지?"를 피한다.

### 12.3 카메라 ISP / NPU 펌웨어 크기와 부팅 시간

NPU 런타임 펌웨어나 ISP 튜닝 블롭은 수 MB다. 동기 요청을 built-in 드라이버 probe에서 하면 부팅 시간이 그만큼 늘고, 부팅 시간 예산(예: 카메라 첫 프레임까지 N초)이 있는 제품에서는 문제가 된다. 흔한 해법: probe에서는 장치만 등록하고, 펌웨어는 `nowait`로 받거나 **첫 `open()`**(또는 V4L2 `s_stream`) 시점에 받는다. 측정은 §9처럼 드라이버 안에서 `ktime`으로 구간을 나눠 찍고, 부팅 전체는 `initcall_debug` 커맨드라인으로 probe별 시간을 본다.

### 12.4 터치/MCU 펌웨어 현장 업데이트

칩 flash에 펌웨어가 있는 터치 컨트롤러·MCU는 "부팅마다 다운로드"가 아니라 "**버전이 다르면 업데이트**"다. 드라이버 probe에서 칩이 보고한 버전을 읽고, `/lib/firmware`의 파일 헤더 버전과 비교해서 다를 때만 굽는다(또는 sysfs `update_fw` 같은 속성 쓰기로 사람이 트리거). 굽는 도중 전원이 나가도 벽돌이 되지 않도록 **A/B 슬롯 또는 부트로더 복구 모드**를 칩 쪽에서 지원하는지가 핵심이고, 이것은 Don이 SSD 펌웨어 슬롯·커밋 액션에서 이미 다룬 문제와 같다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| built-in 드라이버가 probe에서 동기 요청, 파일은 rootfs에만 | 부팅 때만 `Direct firmware load ... error -2`, 나중에 modprobe하면 됨 | rootfs 마운트 전 요청 (§7) | initramfs에 파일 포함, `=m`, `EXTRA_FIRMWARE`, 또는 nowait/지연 요청 |
| sysfs fallback이 켜진 커널에서 선택 파일을 `request_firmware`로 요청 | 부팅이 60초 멈추고 `-110` | 아무도 채우지 않는 fallback timeout (예제 9b, 10) | `request_firmware_direct` / `firmware_request_nowarn` |
| `release_firmware()` 누락 | 펌웨어 크기만큼 메모리 누수, 반복 rebind 시 증가 | `fw_priv` 참조가 안 내려감 | 성공·실패 모든 경로에서 release, 필요한 데이터는 복사 |
| `fw->data` 포인터를 저장해 두고 release 후 사용 | 간헐적 크래시, 쓰레기 데이터 | 버퍼가 이미 해제됨 | 필요하면 `kmemdup`/`vmalloc` 복사본을 드라이버가 소유 |
| 헤더의 `len`을 믿고 CRC/복사 | 잘린 파일에서 out-of-bounds read, 칩 hang | `fw->size` 확인 누락 (§4.1) | 크기 → magic → 길이 → CRC 순으로 검사 |
| nowait 요청 뒤에 콜백이 쓰는 상태를 초기화 | 드물게 NULL 역참조, 부팅마다 다른 증상 | 콜백이 probe 반환 전에 돌 수 있음 (예제 8) | 요청 전에 모든 상태 초기화 |
| remove에서 진행 중인 nowait 콜백을 기다리지 않음 | rmmod 직후 크래시 | 해제된 `priv`를 콜백이 사용 | completion 대기 또는 `request_firmware_nowait_cancel` |
| rootfs에는 `.zst`, 커널은 압축 미지원 | 파일이 분명 있는데 `-2` | `FW_LOADER_COMPRESS_*` 꺼짐 (예제 5·6) | 커널 설정 켜거나 압축 안 된 파일 설치 |
| 새 펌웨어를 `/lib/firmware`에 넣었는데 옛 버전이 로드됨 | 버전 그대로 | `updates/`나 `$(uname -r)` 경로, `firmware_class.path`에 옛 파일이 먼저 있음 (예제 5) | 우선순위 경로 확인, 예제 4처럼 dyndbg로 실제 경로 확인 |
| `MODULE_FIRMWARE` 누락 | 배포판/initramfs에 파일이 안 들어감 | 생성기가 필요한 파일을 모름 | 요청하는 모든 이름을 `MODULE_FIRMWARE`로 선언 |

---

## 14. 면접에서 이렇게 말한다

**Q.** "Why do some devices need the host to load firmware at runtime, and how does a Linux driver do it?"

**A.** 비용과 업데이트 때문에 칩에 flash 없이 ROM 부트로더와 SRAM만 두는 경우가 많다. 그러면 전원이 들어올 때마다 호스트가 이미지를 내려보내야 한다. 드라이버는 `request_firmware(&fw, "vendor/chip.bin", dev)`로 파일을 받는데, 커널의 firmware_class가 `/lib/firmware` 경로들을 찾아 `struct firmware {size, data}`로 돌려준다. 드라이버는 헤더·CRC를 검증하고 칩 프로토콜대로 버스로 내려보낸 뒤 `release_firmware()`한다.

> Many peripherals—Wi-Fi chips, DSPs, touch controllers—ship with only a boot ROM and SRAM, so the host must download the firmware on every power-up. In Linux the driver calls request_firmware with a relative name; the firmware loader searches /lib/firmware and returns a struct firmware with data and size. The driver validates the image—size, magic, version, CRC—pushes it to the chip over MMIO, SPI/I2C or DMA following the boot ROM's protocol, then calls release_firmware.

**꼬리 질문**: "Where exactly does it look?" → `firmware_class.path` → `/lib/firmware/updates/$(uname -r)` → `/lib/firmware/updates` → `/lib/firmware/$(uname -r)` → `/lib/firmware`, 처음 찾은 것.

**Q.** "Your Wi-Fi driver fails with error -2 during boot, but works if you reload the module later. What's going on?"

**A.** 드라이버가 built-in이라 probe가 rootfs 마운트 전에 돌았고, 그때 `/lib/firmware`에 파일이 없어서 `-ENOENT`가 났을 가능성이 가장 크다. 나중에 다시 로드하면 rootfs가 있으니 된다. 해결은 펌웨어를 initramfs에 넣거나, 드라이버를 모듈로 빌드하거나, `CONFIG_EXTRA_FIRMWARE`로 커널에 넣거나, 요청을 비동기·지연으로 바꾸는 것이다. 확인은 커널 설정과 dmesg의 시각으로 한다.

> Most likely the driver is built in, so probe runs before the real root filesystem is mounted and /lib/firmware is still empty—hence ENOENT. Reloading later works because the rootfs is there. Fixes: put the firmware into the initramfs (MODULE_FIRMWARE lets the initramfs generator find it), build the driver as a module, embed it with CONFIG_EXTRA_FIRMWARE, or defer the request with request_firmware_nowait or until first open. I'd confirm with the kernel config and the dmesg timestamp versus the root mount.

**Q.** "When would you use request_firmware_nowait instead of request_firmware?"

**A.** 펌웨어가 크거나 느린 버스로 보내야 해서 probe를 오래 붙잡을 때, 또는 파일이 늦게 생길 수 있을 때다. probe는 요청만 걸고 바로 반환하고, kworker가 파일을 찾아 콜백을 부른다. 주의할 점은 콜백이 probe 반환 전에도 돌 수 있어서 상태를 먼저 초기화해야 하고, remove에서 콜백 완료를 기다려야 하며, 실패하면 콜백에 `NULL`이 온다는 것이다. 실제로 선점형 커널에서 콜백이 probe 로그보다 먼저 찍히는 것을 확인했다.

> When the image is large or goes over a slow bus, so a synchronous request would stall probe and boot, or when the file may appear later. Probe just queues the request and returns; a workqueue thread looks up the file and invokes the callback with the firmware or NULL. The pitfalls are that the callback can run before probe even returns, so all state must be initialized first; remove must wait for or cancel the pending callback; and the device is already bound, so the driver must expose a 'firmware not ready' state.

**Q.** "What is the firmware fallback mechanism, and why can it hang boot for 60 seconds?"

**A.** 파일을 직접 못 찾으면 `/sys/class/firmware/<name>/loading`, `data`를 만들어 유저스페이스가 채우길 기다리는 옛 방식이다. `CONFIG_FW_LOADER_USER_HELPER_FALLBACK`이나 `force_sysfs_fallback` sysctl이 켜져 있으면 모든 요청이 이 경로로 가고, 부팅 초기에는 채워 줄 udev가 없으니 기본 60초 timeout 후 `-ETIMEDOUT`이 난다. 선택적 펌웨어는 `request_firmware_direct()`로 요청해서 fallback을 피한다. 랩에서 timeout을 3초로 줄여 `-110`과 3초 정지를 재현했다.

> If the direct filesystem lookup fails and the sysfs fallback is enabled—either the kernel was built with the user-helper fallback or the force_sysfs_fallback sysctl is set, or the driver used nowait without uevent—the kernel creates loading and data files under /sys/class/firmware and waits for user space to write the image. Early in boot nobody services that, so the request blocks until the timeout, 60 seconds by default, and returns ETIMEDOUT. For optional firmware use request_firmware_direct, which never falls back.

**Q.** "How do you make sure a corrupted or wrong firmware file doesn't brick or hang the device?"

**A.** 호스트에서 먼저 거른다: `fw->size`로 최소 크기, magic, 헤더 길이와 실제 길이 일치, CRC, 칩 ID·버전 호환성을 순서대로 본다. 헤더 숫자는 신뢰하지 않는다. 다운로드는 chunk마다 ACK·timeout을 두고, 실패하면 리셋 GPIO로 칩을 ROM 모드로 돌려 재시도한다. 악의적 변조까지 막으려면 CRC가 아니라 서명이 필요하고, 보통 칩 ROM이 검증한다. 커널 모듈 서명은 펌웨어 파일을 보호하지 않는다.

> Validate on the host before touching the chip: check the real size from struct firmware, the magic, that the header length matches the file, the CRC, and chip ID and version compatibility—never trust header fields blindly. Download in chunks with per-chunk acknowledgement and timeouts, and on failure reset the chip back into ROM mode and retry. CRC only catches accidental corruption; against tampering you need signatures, usually verified by the chip's boot ROM, since module signing doesn't cover firmware files.

**Q.** "What happens to firmware across suspend and resume?"

**A.** 칩 전원이 꺼졌다 켜지면 resume에서 펌웨어를 다시 올려야 한다. 그런데 resume 초반에는 저장장치가 아직 안 깨어났을 수 있다. `CONFIG_FW_CACHE`가 있으면 firmware_class가 성공적으로 로드된 이름을 장치별로 기억해 두었다가 suspend 직전에 메모리에 미리 읽어 두고, resume 중 `request_firmware()`는 캐시에서 받는다. 그래서 드라이버는 resume 콜백에서 그냥 `request_firmware()`를 다시 부르면 된다.

> Devices that lose power need the firmware re-downloaded on resume, but storage may not be available yet at that point. With FW_CACHE, the firmware loader remembers which names each device loaded successfully, pre-loads them into memory on the PM suspend-prepare notifier, and serves request_firmware from that cache during resume; the cache is dropped a few seconds afterwards. So a driver can simply call request_firmware again in its resume path.

**Q.** "How do you ship firmware in an embedded Linux product?"

**A.** linux-firmware 저장소에서 필요한 블롭만 Yocto/Buildroot 패키지로 rootfs에 넣고, `WHENCE`와 라이선스 파일로 재배포 조건을 확인한다. 보드별 NVRAM·보정 파일은 모듈 벤더에게 따로 받는다. 부팅 초기에 필요한 것은 initramfs에 넣는다. 드라이버는 `MODULE_FIRMWARE`로 이름을 선언하고, 버전을 dmesg와 sysfs로 보고해서 현장에서 확인할 수 있게 한다.

> I pull only the needed blobs from linux-firmware through the Yocto or Buildroot package, check the WHENCE entry and license for redistribution terms, and get board-specific NVRAM or calibration files from the module vendor. Anything needed before the rootfs mounts goes into the initramfs. Drivers declare their files with MODULE_FIRMWARE and report the running firmware version in dmesg and sysfs so field units can be audited.

---

## 15. 직접 해보기

1. (개념) `request_firmware()`, `firmware_request_nowarn()`, `request_firmware_direct()`의 차이를 "경고 로그"와 "sysfs fallback" 두 축으로 표를 만들어라.
정답/힌트: request_firmware = 경고 O·fallback 조건부, nowarn = 경고 X·fallback 조건부, direct = 경고 X·fallback 절대 X (소스의 `FW_OPT_*` 플래그 조합).

2. (코드) `g10_fw`에 `chip_id` 필드를 헤더에 추가하고(헤더 20바이트), 모듈 파라미터로 받은 칩 ID와 다르면 `-ENODEV`로 거부하라. `g10_mkfw.py`도 고쳐서 맞는 ID / 틀린 ID 두 파일로 시험하라.
정답/힌트: 헤더 크기를 상수로 빼고 모든 `sizeof(*h)` 계산을 확인. 버전 호환성 검사와 같은 위치(magic 다음).

3. (코드) `mode=1`(nowait)에서 펌웨어가 없을 때 2초 간격으로 최대 5번 다시 요청하는 재시도 로직을 `delayed_work`로 추가하라. 시험: 부팅 후 3초 뒤에 셸에서 파일을 `/lib/firmware`에 복사하면 자동으로 올라와야 한다.
정답/힌트: 콜백에서 `fw == NULL`이면 `schedule_delayed_work()`; remove에서 `cancel_delayed_work_sync()` + 진행 중 요청 대기.

4. (관찰) 예제 4처럼 dynamic debug를 켜고 `firmware_class.path=/opt/fw`를 준 채 없는 파일을 요청해 보라. 몇 개의 경로를 두드리는가?
정답/힌트: 5개 — path 파라미터가 설정되면 0번 항목이 맨 앞에 추가된다.

5. (관찰) `force_sysfs_fallback=1` 상태에서 `request_firmware()`가 기다리는 동안 다른 셸에서 `/sys/class/firmware/`를 보고, `echo -1 > .../loading`으로 취소하면 드라이버가 어떤 에러를 받는지 확인하라. (QEMU 게스트에서 셸을 두 개 쓰려면 insmod를 `&`로 백그라운드에 보낸다.)
정답/힌트: 취소하면 timeout을 기다리지 않고 즉시 실패한다. 반환 코드는 직접 확인해서 §1.3 표에 추가하라.

6. (측정) `chunk=` 파라미터를 64, 512, 4096, 65536으로 바꿔 8 MiB 다운로드 시간을 재고 그래프로 그려라. chunk가 작을 때 느려지는 이유를 실제 버스(I2C 트랜잭션 오버헤드)와 연결해 설명하라.
정답/힌트: chunk당 고정 비용(함수 호출, memcmp 시작, 실제 버스에서는 START/주소/ACK)이 chunk 개수만큼 곱해진다.

7. (조사) 랩 커널 소스에서 `request_firmware_nowait`를 쓰는 드라이버 하나(예: `drivers/remoteproc/remoteproc_core.c`의 auto boot)를 읽고, 콜백이 실패(`NULL`)를 어떻게 처리하는지 정리하라.
정답/힌트: `grep -n request_firmware_nowait -A5 drivers/remoteproc/remoteproc_core.c`로 시작해 콜백 함수를 따라간다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| firmware_class / firmware loader | 커널의 펌웨어 로딩 공용 코드 | `drivers/base/firmware_loader/`, 이름 → 바이트 덩어리 |
| `struct firmware` | 요청 결과 | `size`, `data`(읽기 전용), 내부용 `priv` |
| direct load | 커널이 파일시스템에서 직접 읽기 | 현재의 기본 경로, `kernel_read_file` 기반 |
| `fw_path[]` | 검색 경로 표 | path 파라미터 → updates/버전 → updates → 버전 → /lib/firmware |
| `firmware_class.path` | 사용자 지정 최우선 경로 | 커맨드라인 또는 `/sys/module/firmware_class/parameters/path` |
| sysfs fallback (user helper) | 유저스페이스가 채워 주는 옛 경로 | `/sys/class/firmware/<name>/loading`, `data`, timeout 60 s |
| `FW_ACTION_NOUEVENT` | nowait의 uevent 끔 | 파일 없으면 sysfs 노드를 만들고 수동 로딩을 기다림 |
| `CONFIG_EXTRA_FIRMWARE` | 커널 내장 펌웨어 | 커널 이미지에 블롭을 넣어 파일시스템 없이 제공 |
| FW_CACHE | suspend 대비 캐시 | 로드된 이름을 기억, suspend 직전 미리 읽음 |
| `MODULE_FIRMWARE` | 모듈이 필요로 하는 파일 선언 | `modinfo`의 `firmware:` 줄, initramfs 생성기가 사용 |
| linux-firmware | 펌웨어 블롭 저장소 | 커널과 별도, 재배포 허용 라이선스, `WHENCE` 파일 |
| `WHENCE` | linux-firmware의 출처 목록 | 파일별 드라이버·버전·라이선스 |
| firmware_upload | 장치 flash 업데이트 프레임워크 | 유저 → 커널 → 칩 flash, `CONFIG_FW_UPLOAD` |
| remoteproc | 보조 코어 관리 프레임워크 | ELF 펌웨어를 올리고 코어를 시작·정지 (O2) |
| boot ROM | 칩 내부 마스크 ROM 코드 | 다운로드 프로토콜과 (있다면) 서명 검증 담당 |

---

## 17. 요약 & 체크리스트

전원만으로 동작하지 않는 칩(Wi-Fi, ISP, DSP/NPU, 터치, FPGA, MCU)은 호스트가 펌웨어를 내려보내야 한다. 드라이버는 `request_firmware()` 계열로 이름을 주고 `struct firmware {size, data}`를 받는다. 커널은 `firmware_class.path` → `/lib/firmware/updates/$(uname -r)` → `/lib/firmware/updates` → `/lib/firmware/$(uname -r)` → `/lib/firmware` 순서로 찾고(예제 5에서 v5→v1로 확인), 설정에 따라 압축 파일·플랫폼 사본·sysfs fallback을 시도한다. firmware_class는 내용을 모르므로 드라이버가 크기·magic·길이·CRC를 순서대로 검증한 뒤 chunk로 내려보내고 즉시 `release_firmware()`한다. 큰 펌웨어나 늦게 생기는 파일에는 `request_firmware_nowait()`를 쓰되, 콜백이 probe 반환 전에도 돌 수 있다는 점(예제 8)과 remove 경쟁을 처리해야 한다. built-in 드라이버의 rootfs 이전 요청은 `-ENOENT` 또는 fallback timeout(예제 9·10의 `-110`)으로 이어지고, initramfs·모듈화·내장·지연 요청 중 하나로 해결한다. suspend/resume은 FW_CACHE가 해결하고, 제품에는 linux-firmware에서 필요한 블롭만 라이선스를 확인해 넣는다.

- [ ] 펌웨어를 호스트가 올리는 장치 종류와 그 이유(비용·업데이트)를 설명할 수 있다
- [ ] `request_firmware` / `firmware_request_nowarn` / `request_firmware_direct` / `request_firmware_nowait` / `firmware_request_platform`의 차이를 말할 수 있다
- [ ] 검색 경로 다섯 개를 순서대로 말하고 `firmware_class.path`를 부팅 시·실행 중 바꿀 수 있다
- [ ] dynamic debug로 실제로 어느 경로에서 파일을 읽었는지 확인할 수 있다
- [ ] 헤더(magic·version·length·CRC32) 검증 순서와 그 이유(out-of-bounds 방지)를 설명할 수 있다
- [ ] 검증 + chunk 다운로드 + sysfs 버전 보고를 하는 드라이버를 코드로 짤 수 있다
- [ ] nowait 콜백의 순서·수명 규칙(요청 전 초기화, remove 대기, NULL 처리)을 지킬 수 있다
- [ ] sysfs fallback이 언제 타는지, 왜 60초 정지가 생기는지, 손으로 펌웨어를 넣는 3단계를 설명할 수 있다
- [ ] built-in 드라이버의 부팅 race를 진단하고 해결책 네 가지를 비교할 수 있다
- [ ] FW_CACHE가 suspend/resume에서 하는 일을 설명할 수 있다
- [ ] 압축 펌웨어와 커널 설정 불일치로 생기는 `-2`를 알아볼 수 있다
- [ ] linux-firmware·`WHENCE`·`MODULE_FIRMWARE`를 이용해 제품 rootfs에 펌웨어를 넣는 방법을 설명할 수 있다

---

## 참고 자료

- Linux kernel documentation — Firmware guide: https://docs.kernel.org/driver-api/firmware/index.html (특히 `fw_search_path`, `fallback-mechanisms`, `firmware_cache`, `built-in-fw`, `request_firmware`, `fw_upload` 장. 랩 소스의 `Documentation/driver-api/firmware/`에 같은 rst 파일들이 있다)
- 커널 소스: `drivers/base/firmware_loader/main.c`, `fallback.c`, `fallback_table.c`, `include/linux/firmware.h` — https://elixir.bootlin.com/linux/v6.18/source/drivers/base/firmware_loader
- linux-firmware 저장소와 `WHENCE`: https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git
- remoteproc 문서 (O2와 연결): https://docs.kernel.org/staging/remoteproc.html
- Bootlin, "Linux kernel and driver development training" 슬라이드 (디바이스 모델·드라이버 장): https://bootlin.com/training/kernel/
- 선행·후속 노트: G3(platform driver), G8(sysfs), G9(udev·`MODULE_DEVICE_TABLE`), D6(DT overlay), O2(remoteproc)
