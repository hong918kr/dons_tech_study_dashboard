# G2. 디바이스 모델 — bus · device · driver · class, kobject/kref, sysfs 트리

> **이 노트를 다 읽으면**: `struct bus_type`/`device`/`device_driver`/`class`가 서로 어떤 포인터로 묶이는지 그림으로 그릴 수 있다 · 직접 만든 버스(`g2bus`)로 `driver_register()`와 `device_add()` 두 입구가 `really_probe()`에서 만나는 것을 ftrace로 보여 줄 수 있다 · kobject 하나 = sysfs 디렉터리 하나라는 규칙으로 `/sys/bus`, `/sys/devices`, `/sys/class`를 읽을 수 있다 · kref와 `release()` 규칙을 어기면 무슨 일이 나는지(WARN, use-after-free, underflow)를 실제 로그로 설명할 수 있다
> **왜 필요한가**: 드라이버 면접의 단골 질문 "When exactly is probe() called?", "Why must every struct device have a release function?"에 답하는 바탕이고, 실무에서는 "모듈을 내렸다 올렸더니 Oops", "센서가 PMIC보다 먼저 probe돼서 실패" 같은 문제를 이 모델로 푼다
> **Don 기준 난이도**: 객체 등록 테이블·참조 카운트·init 순서 의존성은 펌웨어 HAL에서 이미 해 봤다 / 새로 배울 부분은 이 모든 것이 커널 전체에 하나의 공통 모델(kobject)로 통일되어 있고, 그 결과가 sysfs 트리로 그대로 드러난다는 점
> **선행 노트**: D3(드라이버 매칭 — compatible → `platform_match` → probe, `driver_override`), E3(커널 모듈) — G3(platform driver와 생명주기, deferred probe), G8(sysfs 속성), G9(uevent·udev)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어에서 HAL을 짰다고 해 보자. 아마 이런 것들이 있었을 것이다.

- **객체 레지스트리**: "NAND 채널 8개, DMA 엔진 4개, 온도 센서 2개" 같은 장치 객체 배열과, 각 장치 종류를 다루는 드라이버 함수 테이블.
- **바인딩**: 부팅 때 장치 배열을 훑으며 "이 장치 종류면 이 init 함수"를 짝지어 부르는 루프.
- **참조 카운트**: 커맨드가 처리 중인 동안 그 채널 객체가 리셋·해제되지 않도록 잡아 두는 카운터.
- **의존성 순서**: 전원 레일(PMIC)이 올라오기 전에는 NAND를 초기화하지 않는 규칙.
- **디버그 덤프**: 셸 명령으로 "지금 등록된 객체와 상태"를 트리로 출력하는 기능.

Linux **디바이스 모델(driver model)**은 이 다섯 가지를 커널 전체에 하나로 통일한 것이다.

| 펌웨어 HAL | Linux 디바이스 모델 | 이 노트 |
|---|---|---|
| 장치 종류별 버스·컨트롤러 | `struct bus_type` (platform, i2c, spi, usb, pci …) | §1, §2 |
| 장치 객체 | `struct device` (보통 `platform_device`·`i2c_client` 안에 임베드) | §1 |
| 드라이버 함수 테이블 | `struct device_driver` (보통 `platform_driver` 안에 임베드) | §1 |
| "온도 센서들", "UART들" 같은 기능별 묶음 | `struct class` → `/sys/class/<이름>` | §3 |
| 바인딩 루프 | `driver_attach()` / `__device_attach()` → `really_probe()` | §6 |
| 참조 카운터 | `struct kref` (kobject 안), `get_device()`/`put_device()` | §5 |
| 의존성 순서 | device link, fw_devlink, deferred probe | §8 |
| 디버그 덤프 | sysfs (`/sys`) — 객체마다 디렉터리 하나 | §3, §4 |

D3에서는 platform 버스가 compatible 문자열로 장치와 드라이버를 짝짓는 것을 봤다. 이 노트는 한 단계 더 일반화한다. **platform은 여러 버스 중 하나일 뿐**이고, 버스·장치·드라이버·클래스라는 틀은 I2C든 USB든 똑같다. 그걸 확실히 보려고 **우리만의 버스 `g2bus`를 처음부터 만든다**. 공유 예제는 가상의 "sensorkit" IMU 보드 — PMIC 하나(`pmic0`)와 IMU 두 개(`imu0`, `imu1`)다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="210" y="16" width="260" height="66" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="38" text-anchor="middle" font-size="13" font-weight="bold">struct bus_type — g2bus</text>
  <text x="340" y="56" text-anchor="middle" font-size="12">match · uevent · probe · remove</text>
  <text x="340" y="73" text-anchor="middle" font-size="12">장치 목록 · 드라이버 목록 (klist)</text>
  <rect x="20" y="16" width="170" height="66" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="38" text-anchor="middle" font-size="13" font-weight="bold">struct device</text>
  <text x="105" y="56" text-anchor="middle" font-size="12">g2root (루트 장치)</text>
  <text x="105" y="73" text-anchor="middle" font-size="12">/sys/devices/g2root</text>
  <rect x="20" y="160" width="230" height="86" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="135" y="182" text-anchor="middle" font-size="13" font-weight="bold">struct device — imu0</text>
  <text x="135" y="200" text-anchor="middle" font-size="12">(struct g2_device 안에 임베드)</text>
  <text x="135" y="218" text-anchor="middle" font-size="12">bus · parent · driver · class</text>
  <text x="135" y="236" text-anchor="middle" font-size="12">kobj (안에 kref)</text>
  <rect x="430" y="160" width="230" height="86" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="545" y="182" text-anchor="middle" font-size="13" font-weight="bold">struct device_driver</text>
  <text x="545" y="200" text-anchor="middle" font-size="12">sensorkit-imu</text>
  <text x="545" y="218" text-anchor="middle" font-size="12">(struct g2_driver 안에 임베드)</text>
  <text x="545" y="236" text-anchor="middle" font-size="12">bus · owner · p (kobj·목록)</text>
  <rect x="20" y="320" width="200" height="66" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="120" y="342" text-anchor="middle" font-size="13" font-weight="bold">struct class</text>
  <text x="120" y="360" text-anchor="middle" font-size="12">g2class</text>
  <text x="120" y="377" text-anchor="middle" font-size="12">/sys/class/g2class</text>
  <rect x="300" y="320" width="250" height="66" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="425" y="342" text-anchor="middle" font-size="13" font-weight="bold">struct device — sensorkit0</text>
  <text x="425" y="360" text-anchor="middle" font-size="12">class device: class · devt</text>
  <text x="425" y="377" text-anchor="middle" font-size="12">parent = imu0, /dev/sensorkit0</text>
  <line x1="190" y1="160" x2="270" y2="84" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="168" y="118" font-size="12">dev-&gt;bus</text>
  <line x1="500" y1="160" x2="420" y2="84" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="468" y="118" font-size="12">drv-&gt;bus</text>
  <line x1="80" y1="160" x2="80" y2="84" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="26" y="128" font-size="12">parent</text>
  <line x1="250" y1="203" x2="428" y2="203" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah1)"/>
  <text x="270" y="196" font-size="12">dev-&gt;driver (bind 후)</text>
  <line x1="380" y1="320" x2="220" y2="248" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="314" y="276" font-size="12">parent</text>
  <line x1="300" y1="353" x2="222" y2="353" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="232" y="345" font-size="12">class</text>
  <text x="552" y="300" font-size="12">probe()가 만든다</text>
  <line x1="560" y1="286" x2="545" y2="248" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="20" y="412" font-size="12">실선: 항상 있는 포인터 · 점선: bind된 동안만 · 색: 파랑=버스, 초록=버스 장치, 주황=드라이버, 빨강=클래스</text>
</svg>
```
그림 1 — 이 노트에서 만들 객체들의 관계. 장치와 드라이버는 서로를 모른 채 각자 버스에 등록되고, 버스의 `match`가 짝을 지으면 `dev->driver`가 채워진다(점선). 클래스 장치 `sensorkit0`은 "기능" 쪽 얼굴로, 버스 장치 `imu0`을 부모로 갖는다.

| 절 | 내용 | 실습 |
|---|---|---|
| §1 | 네 구조체의 실제 필드 (랩 헤더 grep) | 예제 1 |
| §2 | 우리만의 버스 `g2bus`와 sensorkit 드라이버 만들기 | 예제 2–3 |
| §3 | sysfs 트리 읽기 — `/sys/bus`, `/sys/devices`, `/sys/class`, `/dev` | 예제 4 |
| §4 | kobject · kset · ktype — "kobject 하나 = 디렉터리 하나" | 예제 5–6 |
| §5 | kref와 `release()` 규칙 — 일부러 어겨 보기 | 예제 7–9 |
| §6 | 두 입구: `driver_register()`와 `device_add()` — ftrace로 확인 | 예제 10–11 |
| §7 | 손으로 bind/unbind, `drivers_autoprobe` | 예제 12 |
| §8 | device link, deferred probe 미리보기, fw_devlink | 예제 13–14 |

> 실행 환경: Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안의 공용 랩에서 QEMU `virt`(cortex-a57, 512 MB, `-smp 2`, TCG)로 **디버그 커널 `6.18.55-dbg`**(`/var/lab/out/Image-dbg`, ftrace 포함)를 부팅해 BusyBox initramfs에서 실행한 결과다. 모듈은 `/var/lab/out/linux-build-dbg`에 대고 빌드했다. 커널 소스 인용은 `/var/lab/src/linux-6.18.55`를 grep해서 확인했다. 타임스탬프·주소·SEQNUM·major 번호는 실행마다 다르다.

---

## 1. 네 개의 핵심 구조체

### 1.1 직관 — "명부 세 권과 기능별 색인 한 권"

- `bus_type`은 **명부를 관리하는 사무실**이다. 장치 명부와 드라이버 명부 두 권을 갖고, 새 이름이 들어올 때마다 "짝이 맞나"(`match`)를 검사한다.
- `device`는 **장치 명부의 한 줄**이다. 어느 버스 소속인지(`bus`), 누구 밑에 달렸는지(`parent`), 지금 누가 담당하는지(`driver`)를 적는다.
- `device_driver`는 **드라이버 명부의 한 줄**이다. 어느 버스 소속이고 어떤 모듈이 주인인지.
- `class`는 **기능별 색인**이다. "IMU가 어느 버스에 붙어 있든 사용자는 `/sys/class/…`에서 찾는다." 버스 소속과 무관한 두 번째 분류다.

### 1.2 예제 1 — 랩 커널 헤더에서 구조체 위치와 필드를 직접 확인

무엇을 확인하나: 노트의 구조체 설명이 6.18 소스와 맞는지. 위치부터 찾는다.

```sh
cd /var/lab/src/linux-6.18.55
grep -n "^struct bus_type {\|^struct device_driver {\|^struct class {\|^struct device {\|^struct kobject {\|^struct kset {\|^struct kobj_type {\|^struct kref {" \
  include/linux/device.h include/linux/device/*.h include/linux/kobject.h include/linux/kref.h
```

```text
include/linux/device.h:602:struct device {
include/linux/device/bus.h:81:struct bus_type {
include/linux/device/class.h:50:struct class {
include/linux/device/driver.h:96:struct device_driver {
include/linux/kobject.h:64:struct kobject {
include/linux/kobject.h:116:struct kobj_type {
include/linux/kobject.h:168:struct kset {
include/linux/kref.h:19:struct kref {
```

같은 방법(`sed -n "/^struct bus_type {/,/^};/p"` 등)으로 본문을 뽑아, 이 노트에 필요한 필드만 원문 그대로 남겼다 (나머지는 `...`).

```text
struct bus_type {
	const char		*name;
	int (*match)(struct device *dev, const struct device_driver *drv);
	int (*uevent)(const struct device *dev, struct kobj_uevent_env *env);
	int (*probe)(struct device *dev);
	void (*remove)(struct device *dev);
	...
	bool driver_override;
};
struct device_driver {
	const char		*name;
	const struct bus_type	*bus;
	struct module		*owner;
	bool suppress_bind_attrs;	/* disables bind/unbind via sysfs */
	enum probe_type probe_type;
	const struct of_device_id	*of_match_table;
	int (*probe) (struct device *dev);
	int (*remove) (struct device *dev);
	...
	struct driver_private *p;
};
struct class {
	const char		*name;
	const struct attribute_group	**dev_groups;
	int (*dev_uevent)(const struct device *dev, struct kobj_uevent_env *env);
	char *(*devnode)(const struct device *dev, umode_t *mode);
	void (*dev_release)(struct device *dev);
	...
};
struct device {
	struct kobject kobj;
	struct device		*parent;
	struct device_private	*p;
	const char		*init_name; /* initial name of the device */
	const struct device_type *type;
	const struct bus_type	*bus;	/* type of bus device is on */
	struct device_driver *driver;	/* which driver has allocated this
	void		*driver_data;	/* Driver data, set and get with
	...
	struct dev_links_info	links;
	...
	struct device_node	*of_node; /* associated device tree node */
	struct fwnode_handle	*fwnode; /* firmware device node */
	dev_t			devt;	/* dev_t, creates the sysfs "dev" */
	const struct class	*class;
	const struct attribute_group **groups;	/* optional groups */
	void	(*release)(struct device *dev);
	...
};
```

출력에서 볼 것:

- `struct device`의 **첫 필드가 `struct kobject kobj`**다. 장치의 수명(kref)과 sysfs 디렉터리는 전부 이 kobject가 담당한다(§4, §5).
- `bus_type`에는 장치·드라이버 목록 필드가 **없다**. 목록(klist)과 sysfs kset은 `drivers/base/base.h`의 `struct subsys_private`에 있고, `bus_to_subsys(bus)`로 찾는다. 최근 6.x 커널에서 `bus_type`과 `class`를 `const`로 선언할 수 있게 하려고 내부 상태를 이렇게 밖으로 뺐다. 그래서 우리 `g2bus_type`도 `const`다.
- `device_driver`의 사적 부분도 `struct driver_private *p`(kobject, 장치 klist)로 분리돼 있다. 장치 쪽 사적 부분은 이렇다 (`drivers/base/base.h`):

```text
struct device_private {
	struct klist klist_children;
	struct klist_node knode_parent;
	struct klist_node knode_driver;
	struct klist_node knode_bus;
	struct klist_node knode_class;
	struct list_head deferred_probe;
	const struct device_driver *async_driver;
	char *deferred_probe_reason;
	struct device *device;
	u8 dead:1;
};
```

장치 하나가 동시에 **네 개의 목록**에 걸린다는 뜻이다: 부모의 자식 목록(`knode_parent`), 드라이버의 장치 목록(`knode_driver`), 버스의 장치 목록(`knode_bus`), 클래스의 장치 목록(`knode_class`). 펌웨어로 치면 같은 객체를 "채널별 리스트", "드라이버별 리스트", "기능별 리스트"에 동시에 연결해 둔 intrusive list다. 이 목록들 중 일부가 **참조 카운트를 잡는다**는 점이 §5에서 중요해진다.

### 1.3 "임베드 + container_of" 패턴

`struct device`는 혼자 쓰이는 일이 거의 없다. 버스마다 자기 장치 구조체 안에 임베드한다.

| 버스 | 버스 장치 구조체 | 버스 드라이버 구조체 | 변환 매크로 |
|---|---|---|---|
| platform | `struct platform_device` (안에 `dev`) | `struct platform_driver` (안에 `driver`) | `to_platform_device()` |
| I2C | `struct i2c_client` | `struct i2c_driver` | `to_i2c_client()` |
| SPI | `struct spi_device` | `struct spi_driver` | `to_spi_device()` |
| 이 노트 | `struct g2_device` | `struct g2_driver` | `to_g2_device()` |

driver core는 `struct device *`만 알고, 버스 코드가 `container_of()`로 바깥 구조체를 꺼낸다. Don이 펌웨어에서 `struct nand_ch { struct list_node node; ... }`의 node 포인터에서 채널 구조체를 꺼내던 것과 같은 기법이다.

---

## 2. 우리만의 버스 `g2bus` 만들기

### 2.1 무엇을 만드나

| 모듈 | 역할 | 실제 커널에서 비슷한 것 |
|---|---|---|
| `g2bus.ko` | `bus_type` 등록, 루트 장치 `g2root`, 클래스 `g2class`, 헬퍼 API | `drivers/base/platform.c`, `drivers/i2c/i2c-core-base.c` |
| `g2_board.ko` | 장치 3개(`pmic0`, `imu0`, `imu1`) 등록 | DT populate, 보드 파일 |
| `g2_imu.ko` | kind `imu` 드라이버, probe에서 클래스 장치 `sensorkitN` 생성 | IIO IMU 드라이버 |
| `g2_pmic.ko` | kind `pmic` 드라이버 | 레귤레이터 드라이버 |
| `g2_bad.ko` | refcount 실수 재현용 (§5) | — |
| `g2_kobj.ko` | device 없이 kobject만 쓰기 (§4) | `/sys/kernel/mm/...` 같은 커널 내부 노브 |

매칭 규칙은 D3의 compatible 대신 **kind 문자열 비교** 하나다. 일부러 단순하게 했다 — 매칭 규칙은 버스가 정하는 것이지 driver core가 정하는 게 아니라는 점이 핵심이다.

### 2.2 예제 2 — `g2bus.ko` (헤더 + 소스 3개)

무엇을 확인하나: 버스 하나를 만드는 데 필요한 최소 요소 — 버스 장치·드라이버 구조체, `match`/`uevent`/`probe`/`remove` 콜백, `bus_register()`, 그리고 `release()`를 갖춘 장치 생성 함수.

```c
/* g2bus.h — 우리만의 작은 버스 "g2bus"의 공개 API */
#include <linux/device.h>

#define G2_MAGIC 0x53454e53		/* "SENS" — UAF 데모에서 확인 */

struct g2_device {			/* platform_device에 해당 */
	u32 magic;
	const char *kind;		/* 매칭 키: "imu", "pmic" */
	struct device dev;		/* 공통 부분은 임베드 */
};
#define to_g2_device(d) container_of_const(d, struct g2_device, dev)

struct g2_driver {			/* platform_driver에 해당 */
	const char *kind;		/* 이 kind의 장치를 다룬다 */
	int  (*probe)(struct g2_device *gd);
	void (*remove)(struct g2_device *gd);
	struct device_driver driver;
};
#define to_g2_driver(d) container_of_const(d, struct g2_driver, driver)

extern const struct bus_type g2bus_type;
extern const struct class g2_class;
extern struct device *g2_root;
struct g2_device *g2_device_create(const char *name, const char *kind);
int __g2_driver_register(struct g2_driver *drv, struct module *owner);
#define g2_driver_register(drv) __g2_driver_register(drv, THIS_MODULE)
void g2_driver_unregister(struct g2_driver *drv);
```

버스 콜백 4개:

```c
// g2bus_core.c — bus_type의 콜백 4개와 class 정의
#include <linux/module.h>
#include "g2bus.h"

static int g2_match(struct device *dev, const struct device_driver *drv)
{
	int ok = !strcmp(to_g2_device(dev)->kind, to_g2_driver(drv)->kind);

	pr_info("g2bus: match(%s, %s) = %d\n", dev_name(dev), drv->name, ok);
	return ok;			/* >0 이면 매칭 */
}

static int g2_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	return add_uevent_var(env, "MODALIAS=g2:%s", to_g2_device(dev)->kind);
}

static int g2_probe(struct device *dev)	/* bus->probe: 타입 변환 후 위임 */
{
	return to_g2_driver(dev->driver)->probe(to_g2_device(dev));
}

static void g2_remove(struct device *dev)
{
	const struct g2_driver *gdrv = to_g2_driver(dev->driver);

	if (gdrv->remove)
		gdrv->remove(to_g2_device(dev));
}

const struct bus_type g2bus_type = {
	.name	= "g2bus",
	.match	= g2_match,
	.uevent	= g2_uevent,
	.probe	= g2_probe,
	.remove	= g2_remove,
};
EXPORT_SYMBOL_GPL(g2bus_type);

const struct class g2_class = { .name = "g2class" };
EXPORT_SYMBOL_GPL(g2_class);
```

장치 생성과 드라이버 등록 헬퍼 — **이 노트에서 가장 중요한 20줄**이다:

```c
// g2bus_api.c — 장치 생성(release 포함)과 드라이버 등록 헬퍼
#include <linux/module.h>
#include <linux/slab.h>
#include "g2bus.h"

static void g2_dev_release(struct device *dev)	/* refcount 0일 때만 불린다 */
{
	pr_info("g2bus: release(%s) -> kfree\n", dev_name(dev));
	kfree(to_g2_device(dev));
}

struct g2_device *g2_device_create(const char *name, const char *kind)
{
	struct g2_device *gd = kzalloc(sizeof(*gd), GFP_KERNEL);
	int ret;

	if (!gd)
		return ERR_PTR(-ENOMEM);
	gd->magic = G2_MAGIC;
	gd->kind = kind;
	device_initialize(&gd->dev);		/* kref = 1 */
	gd->dev.bus = &g2bus_type;
	gd->dev.parent = g2_root;
	gd->dev.release = g2_dev_release;
	dev_set_name(&gd->dev, "%s", name);
	ret = device_add(&gd->dev);		/* sysfs + uevent + 매칭 시도 */
	if (ret) {
		put_device(&gd->dev);		/* kfree 금지: release가 한다 */
		return ERR_PTR(ret);
	}
	return gd;
}
EXPORT_SYMBOL_GPL(g2_device_create);

int __g2_driver_register(struct g2_driver *drv, struct module *owner)
{
	drv->driver.bus = &g2bus_type;
	drv->driver.owner = owner;
	return driver_register(&drv->driver);	/* 매칭 시도는 여기서도 */
}
EXPORT_SYMBOL_GPL(__g2_driver_register);

void g2_driver_unregister(struct g2_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(g2_driver_unregister);
```

모듈 init/exit — 등록은 bus → 루트 장치 → class, 해제는 역순:

```c
// g2bus_main.c — 모듈 init/exit: bus → 루트 device → class 순서로 등록
#include <linux/module.h>
#include "g2bus.h"

struct device *g2_root;			/* /sys/devices/g2root */

static int __init g2bus_init(void)
{
	int ret = bus_register(&g2bus_type);	/* /sys/bus/g2bus */

	if (ret)
		return ret;
	g2_root = root_device_register("g2root");
	if (IS_ERR(g2_root)) {
		bus_unregister(&g2bus_type);
		return PTR_ERR(g2_root);
	}
	ret = class_register(&g2_class);	/* /sys/class/g2class */
	if (ret) {
		root_device_unregister(g2_root);
		bus_unregister(&g2bus_type);
	}
	return ret;
}

static void __exit g2bus_exit(void)
{
	class_unregister(&g2_class);
	root_device_unregister(g2_root);
	bus_unregister(&g2bus_type);
}
module_init(g2bus_init);
module_exit(g2bus_exit);
MODULE_DESCRIPTION("G2 toy bus");
MODULE_LICENSE("GPL");
```

```make
obj-m += g2bus.o g2_board.o g2_imu.o g2_pmic.o g2_bad.o g2_kobj.o
g2bus-y := g2bus_core.o g2bus_api.o g2bus_main.o
```

빌드 (디버그 커널 트리, `W=1`로 추가 경고까지 켜고 경고 줄 수를 센다):

```sh
cd /var/tmp/G2/mod
make -C /var/lab/out/linux-build-dbg M=$PWD clean > /dev/null
make -j2 -C /var/lab/out/linux-build-dbg M=$PWD W=1 modules 2>&1 | grep -ci warning
modinfo g2_board.ko | grep -E "parm|depends|vermagic"
```

```text
0
depends:        g2bus
vermagic:       6.18.55-dbg SMP preempt mod_unload aarch64
parm:           link:bool
```

출력에서 볼 것: 경고 0개. `g2_board.ko`가 `g2bus`에 의존한다는 것(`depends:`)은 modpost가 `EXPORT_SYMBOL_GPL` 사용을 보고 자동으로 적는다. 처음 빌드 때는 `MODULE_DESCRIPTION()`이 없다는 modpost 경고가 4개 나왔고, 매크로를 넣어 없앴다.

세 가지 설계 포인트:

- **`g2_driver`에 `probe`를 따로 두고 `device_driver.probe`는 비워 둔다.** 버스에 `probe`가 있는데 드라이버도 `device_driver.probe`를 채우면 `driver_register()`가 `"Driver '%s' needs updating - please use bus_type methods"` 경고를 찍는다(`drivers/base/driver.c`에서 확인). platform·I2C·SPI가 전부 이 구조다: `bus->probe`(예: `platform_probe`)가 버스 전용 타입으로 바꿔서 `platform_driver.probe`를 부른다.
- **`match`는 양수면 매칭이다.** 6.18의 `__driver_attach()`는 반환값을 `0`(불일치), `-EPROBE_DEFER`(나중에 다시), 다른 음수(에러), 양수(매칭)로 구분한다.
- **`root_device_register()`**는 버스·클래스 없이 `/sys/devices/<이름>`에 장치 하나를 만드는 API다. 우리 장치들의 부모가 되어 `/sys/devices` 트리에 자리를 잡아 준다.

### 2.3 예제 3 — 보드 모듈과 드라이버 두 개

무엇을 확인하나: "장치를 등록하는 쪽"과 "드라이버를 등록하는 쪽"이 서로를 전혀 모르는 별개 모듈이라는 것. 둘 다 `g2bus` API만 안다.

```c
// g2_board.c — "보드 파일": sensorkit 장치 3개를 g2bus에 등록 (DT populate 역할)
#include <linux/module.h>
#include "g2bus.h"

static bool link;			/* 1이면 imu → pmic device link 추가 */
module_param(link, bool, 0444);
static struct g2_device *gd[3];
static const char *const names[] = { "pmic0", "imu0", "imu1" };
static const char *const kinds[] = { "pmic", "imu", "imu" };

static int __init g2_board_init(void)
{
	int i;

	for (i = 0; i < 3; i++) {
		gd[i] = g2_device_create(names[i], kinds[i]);
		if (IS_ERR(gd[i])) {
			while (--i >= 0)
				device_unregister(&gd[i]->dev);
			return -ENODEV;
		}
		pr_info("g2_board: added %s, kref=%u\n", names[i],
			kref_read(&gd[i]->dev.kobj.kref));
	}
	for (i = 1; link && i < 3; i++)	/* consumer=imuN, supplier=pmic0 */
		if (!device_link_add(&gd[i]->dev, &gd[0]->dev, 0))
			pr_warn("g2_board: device_link_add failed\n");
	return 0;
}

static void __exit g2_board_exit(void)
{
	int i;

	for (i = 2; i >= 0; i--)
		device_unregister(&gd[i]->dev);	/* del + put */
}
module_init(g2_board_init);
module_exit(g2_board_exit);
MODULE_DESCRIPTION("G2 board devices");
MODULE_LICENSE("GPL");
```

IMU 드라이버 — probe에서 클래스 장치를 만들어 `/dev/sensorkitN`이 생기게 한다:

```c
// g2_imu.c — kind "imu" 드라이버: probe에서 class device(sensorkitN) 생성
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include "g2bus.h"

static dev_t base;			/* alloc_chrdev_region으로 받은 major */
static int next_minor;

static int imu_probe(struct g2_device *gd)
{
	struct device *cdev;
	int minor = next_minor++;

	pr_info("g2_imu: probe(%s) kref=%u\n", dev_name(&gd->dev),
		kref_read(&gd->dev.kobj.kref));
	cdev = device_create(&g2_class, &gd->dev, MKDEV(MAJOR(base), minor),
			     NULL, "sensorkit%d", minor);
	if (IS_ERR(cdev))
		return PTR_ERR(cdev);
	dev_set_drvdata(&gd->dev, cdev);
	pr_info("g2_imu: %s -> %s, parent kref=%u\n", dev_name(&gd->dev),
		dev_name(cdev), kref_read(&gd->dev.kobj.kref));
	return 0;
}

static void imu_remove(struct g2_device *gd)
{
	struct device *cdev = dev_get_drvdata(&gd->dev);

	pr_info("g2_imu: remove(%s)\n", dev_name(&gd->dev));
	device_destroy(&g2_class, cdev->devt);
}

static struct g2_driver imu_drv = {
	.kind = "imu", .probe = imu_probe, .remove = imu_remove,
	.driver = { .name = "sensorkit-imu" },
};

static int __init g2_imu_init(void)
{
	int ret = alloc_chrdev_region(&base, 0, 4, "sensorkit");

	if (ret)
		return ret;
	ret = g2_driver_register(&imu_drv);
	if (ret)
		unregister_chrdev_region(base, 4);
	return ret;
}

static void __exit g2_imu_exit(void)
{
	g2_driver_unregister(&imu_drv);
	unregister_chrdev_region(base, 4);
}
module_init(g2_imu_init);
module_exit(g2_imu_exit);
MODULE_DESCRIPTION("G2 sensorkit IMU driver");
MODULE_LICENSE("GPL");
```

PMIC 드라이버 `g2_pmic.c`(§8에서 공급자 역할)는 같은 모양이라 핵심만 보인다. init/exit는 `g2_driver_register(&pmic_drv)` / `g2_driver_unregister(&pmic_drv)` 한 줄씩이다.

```c
static int pmic_probe(struct g2_device *gd)
{
	pr_info("g2_pmic: probe(%s) — rails up\n", dev_name(&gd->dev));
	return 0;
}

static struct g2_driver pmic_drv = {
	.kind = "pmic", .probe = pmic_probe,
	.driver = { .name = "sensorkit-pmic" },
};
```

> 단순화: `next_minor`는 계속 증가만 한다. unbind/bind를 반복하면 minor가 4를 넘어 할당 범위를 벗어난다(§7에서 실제로 `sensorkit2`가 나온다). 실제 드라이버는 `ida_alloc()`/`ida_free()`로 번호를 재사용한다 — G4에서 다룬다.

### 2.4 실행 방법

D3과 같은 방식이다. 게스트 스크립트를 initramfs에 `/t.sh`로 넣고 `LAB_CMD`로 실행한다.

빌드는 `/var/tmp/G2/mod`에 소스를 복사해 `make -j2 -C /var/lab/out/linux-build-dbg M=/var/tmp/G2/mod modules`, rootfs는 `/var/lab/build/rootfs`를 `/var/tmp/G2/rootfs`로 복사한 뒤 `.ko`를 넣는다.

```sh
#!/bin/bash
# run.sh <게스트 스크립트> [추가 커널 인자] — DTB 환경 변수가 있으면 -dtb로 넘긴다
W=/var/tmp/G2; S=/Users/donh/.elinux_scratch/G2
cp $S/$1 $W/rootfs/t.sh; chmod +x $W/rootfs/t.sh
(cd $W/rootfs && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout 180 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -smp 2 -nographic \
  -kernel /var/lab/out/Image-dbg -initrd $W/i.gz ${DTB:+-dtb $DTB} \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/t.sh $2" </dev/null 2>&1 | tr -d '\r' | sed -n '/^lab initramfs/,$p'
```

---

## 3. sysfs 트리 읽기 — 같은 장치를 세 방향에서 본다

### 3.1 예제 4 — 모듈 4개를 올리고 `/sys`를 훑기

무엇을 확인하나: `bus_register()`, `device_add()`, `driver_register()`, `device_create()`가 각각 sysfs에 무엇을 만드는지, 그리고 매칭이 언제 일어나는지.

```sh
insmod /g2bus.ko
ls /sys/bus/g2bus /sys/class/g2class; ls /sys/devices | grep g2
insmod /g2_board.ko
insmod /g2_imu.ko
insmod /g2_pmic.ko
dmesg | grep -E "g2"
tree /sys/bus/g2bus
tree /sys/devices/g2root
ls -l /sys/class/g2class
ls -l /dev/sensorkit*
```

먼저 `g2bus.ko`만 올린 직후:

```text
/sys/bus/g2bus:
devices            drivers_autoprobe  uevent
drivers            drivers_probe

/sys/class/g2class:
g2root
```

(`ls` 두 개의 출력이 이어져 있다: `/sys/class/g2class:` 아래는 비어 있고, 마지막 `g2root`는 `ls /sys/devices | grep g2`의 결과다.)

`bus_register()`가 만든 것: `devices/`, `drivers/` 두 kset과 파일 3개. 소스(`drivers/base/bus.c`)에서 확인한 대로 `kset_create_and_add("devices", ...)`, `kset_create_and_add("drivers", ...)`, `bus_create_file(bus, &bus_attr_uevent)`, `add_probe_files(bus)`(→ `drivers_probe`, `drivers_autoprobe`)다.

다음은 보드 → IMU 드라이버 → PMIC 드라이버 순으로 올렸을 때의 dmesg:

```text
[   10.444795] g2bus: loading out-of-tree module taints kernel.
[   10.880418] g2_board: added pmic0, kref=3
[   10.881450] g2_board: added imu0, kref=3
[   10.890272] g2_board: added imu1, kref=3
[   11.034796] g2bus: match(pmic0, sensorkit-imu) = 0
[   11.034914] g2bus: match(imu0, sensorkit-imu) = 1
[   11.035398] g2_imu: probe(imu0) kref=3
[   11.045350] g2_imu: imu0 -> sensorkit0, parent kref=5
[   11.047191] g2bus: match(imu1, sensorkit-imu) = 1
[   11.048507] g2_imu: probe(imu1) kref=3
[   11.052720] g2_imu: imu1 -> sensorkit1, parent kref=5
[   11.132026] g2bus: match(pmic0, sensorkit-pmic) = 1
[   11.132641] g2_pmic: probe(pmic0) — rails up
[   11.133100] g2bus: match(imu0, sensorkit-pmic) = 0
[   11.133135] g2bus: match(imu1, sensorkit-pmic) = 0
```

출력에서 볼 것:

- 보드가 장치를 추가할 때는 **match 로그가 없다**. 아직 버스에 드라이버가 하나도 없어서 `__device_attach()`가 훑을 드라이버 목록이 비어 있다.
- `g2_imu.ko`가 들어오자 `driver_attach()`가 **버스의 모든 장치**(pmic0, imu0, imu1)에 대해 match를 부른다. `g2_pmic.ko`가 들어올 때도 이미 `sensorkit-imu`에 묶인 imu0/imu1까지 match를 부르는 것을 보라 — `__driver_attach()`는 match를 먼저 하고, "이미 드라이버가 있는가"는 그 뒤 `__driver_probe_device()`에서 검사한다(있으면 `-EBUSY`로 건너뜀).
- `kref=3`, `parent kref=5`라는 숫자는 §5에서 하나씩 해부한다.

`/sys/bus/g2bus` — 버스의 시점:

```text
/sys/bus/g2bus
├── devices
│   ├── imu0 -> ../../../devices/g2root/imu0
│   ├── imu1 -> ../../../devices/g2root/imu1
│   └── pmic0 -> ../../../devices/g2root/pmic0
├── drivers
│   ├── sensorkit-imu
│   │   ├── bind
│   │   ├── imu0 -> ../../../../devices/g2root/imu0
│   │   ├── imu1 -> ../../../../devices/g2root/imu1
│   │   ├── module -> ../../../../module/g2_imu
│   │   ├── uevent
│   │   └── unbind
│   └── sensorkit-pmic
│       ├── bind
│       ├── module -> ../../../../module/g2_pmic
│       ├── pmic0 -> ../../../../devices/g2root/pmic0
│       ├── uevent
│       └── unbind
├── drivers_autoprobe
├── drivers_probe
└── uevent

4 directories, 17 files
```

`/sys/devices/g2root` — 하드웨어 계층(부모-자식)의 시점. 각 디렉터리의 `power/` 하위 파일 5개(`autosuspend_delay_ms`, `control`, `runtime_*`)는 반복이라 지웠고, `imu1` 서브트리는 `imu0`과 같아서 한 줄로 줄였다:

```text
/sys/devices/g2root
├── imu0
│   ├── driver -> ../../../bus/g2bus/drivers/sensorkit-imu
│   ├── g2class
│   │   └── sensorkit0
│   │       ├── dev
│   │       ├── device -> ../../../imu0
│   │       ├── power
│   │       ├── subsystem -> ../../../../../class/g2class
│   │       └── uevent
│   ├── power
│   ├── subsystem -> ../../../bus/g2bus
│   └── uevent
├── imu1   (imu0과 같은 모양, sensorkit1 — 생략)
├── module -> ../../module/g2bus
├── pmic0
│   ├── driver -> ../../../bus/g2bus/drivers/sensorkit-pmic
│   ├── power
│   ├── subsystem -> ../../../bus/g2bus
│   └── uevent
├── power
└── uevent
```

`/sys/class/g2class`와 `/dev` — 기능의 시점:

```text
lrwxrwxrwx    1 0        0                0 Oct 11 01:35 sensorkit0 -> ../../devices/g2root/imu0/g2class/sensorkit0
lrwxrwxrwx    1 0        0                0 Oct 11 01:35 sensorkit1 -> ../../devices/g2root/imu1/g2class/sensorkit1
crw-------    1 0        0         510,   0 Oct 11 01:35 /dev/sensorkit0
crw-------    1 0        0         510,   1 Oct 11 01:35 /dev/sensorkit1
```

출력에서 볼 것 — 이 세 장면이 디바이스 모델의 전부다:

1. **진짜 디렉터리는 `/sys/devices` 밑에만 있다.** `/sys/bus/g2bus/devices/*`와 `/sys/class/g2class/*`는 전부 심볼릭 링크다. 장치 하나 = 실제 디렉터리 하나, 나머지 시점은 링크로 만든 색인이다.
2. **`driver` 링크가 있으면 bind된 것이다.** D3에서 본 것과 같다. 반대 방향 링크(`drivers/sensorkit-imu/imu0`)도 있다.
3. **`subsystem` 링크**는 "이 장치가 어느 버스나 클래스 소속인가"를 가리킨다. 버스 장치(`imu0`)는 `bus/g2bus`, 클래스 장치(`sensorkit0`)는 `class/g2class`.
4. 클래스 장치는 **부모 버스 장치 밑의 `g2class/` 중간 디렉터리** 안에 생긴다. 이 중간 디렉터리를 커널은 "glue dir"이라 부른다(`get_device_parent()` → `class_dir_create_and_add()`).
5. 클래스 장치에 `devt`가 있으면 `dev` 파일(`510:0`)이 생기고, devtmpfs가 `/dev/sensorkit0`을 만든다. 실제 char device 동작(`cdev`, `file_operations`)은 G4의 몫이다 — 지금은 노드만 있고 `cdev_add()`를 안 했으므로 열면 실패한다.

### 3.2 진짜 장치로 같은 규칙 확인 — `virt` 머신

무엇을 확인하나: 위 규칙이 우리 장난감 버스만의 것이 아니라는 것. 같은 게스트에서 아무 모듈 없이:

```sh
ls /sys/devices
readlink /sys/class/tty/ttyAMA0
readlink /sys/class/rtc/rtc0
```

```text
breakpoint      kprobe          system          virtual
faux            platform        tracepoint
genpd_provider  software        uprobe
../../devices/platform/9000000.pl011/9000000.pl011:0/9000000.pl011:0.0/tty/ttyAMA0
../../devices/platform/9010000.pl031/rtc/rtc0
```

출력에서 볼 것: PL031 RTC는 `rtc` 클래스 장치 `rtc0`이 AMBA 장치 `9010000.pl031` 밑의 glue dir `rtc/`에 산다 — 우리 `imu0/g2class/sensorkit0`과 똑같은 모양이다. 콘솔 UART `ttyAMA0`은 부모가 세 단계(PL011 AMBA 장치 → serial core가 만든 포트 장치 둘)라서 경로가 길다. 부모 없이 클래스에만 속한 장치는 `/sys/devices/virtual/<class>/` 밑에 생긴다(`get_device_parent()`의 `virtual_device_parent()`).

```svg
<svg viewBox="0 0 680 450" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="165" y="22" text-anchor="middle" font-size="13" font-weight="bold">커널 객체 (kobject를 품은 것)</text>
  <text x="515" y="22" text-anchor="middle" font-size="13" font-weight="bold">sysfs 경로</text>
  <rect x="20" y="34" width="290" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="53" font-size="12">bus_kset (전역 kset)</text>
  <rect x="370" y="34" width="290" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="380" y="53" font-size="12">/sys/bus</text>
  <rect x="20" y="70" width="290" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="89" font-size="12">subsys_private.subsys (kset)</text>
  <rect x="370" y="70" width="290" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="380" y="89" font-size="12">/sys/bus/g2bus</text>
  <rect x="20" y="106" width="290" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="125" font-size="12">devices_kset · drivers_kset</text>
  <rect x="370" y="106" width="290" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="380" y="125" font-size="12">/sys/bus/g2bus/{devices,drivers}</text>
  <rect x="20" y="142" width="290" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="161" font-size="12">driver_private.kobj (sensorkit-imu)</text>
  <rect x="370" y="142" width="290" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="380" y="161" font-size="12">.../drivers/sensorkit-imu</text>
  <rect x="20" y="190" width="290" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="209" font-size="12">g2root-&gt;kobj</text>
  <rect x="370" y="190" width="290" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="380" y="209" font-size="12">/sys/devices/g2root</text>
  <rect x="20" y="226" width="290" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="245" font-size="12">imu0-&gt;dev.kobj (parent = g2root)</text>
  <rect x="370" y="226" width="290" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="380" y="245" font-size="12">/sys/devices/g2root/imu0</text>
  <rect x="20" y="262" width="290" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="30" y="281" font-size="12">glue dir kobject (class_dir)</text>
  <rect x="370" y="262" width="290" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="380" y="281" font-size="12">.../imu0/g2class</text>
  <rect x="20" y="298" width="290" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="30" y="317" font-size="12">sensorkit0-&gt;kobj (class device)</text>
  <rect x="370" y="298" width="290" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="380" y="317" font-size="12">.../imu0/g2class/sensorkit0</text>
  <line x1="310" y1="48" x2="368" y2="48" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="310" y1="84" x2="368" y2="84" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="310" y1="120" x2="368" y2="120" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="310" y1="156" x2="368" y2="156" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="310" y1="204" x2="368" y2="204" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="310" y1="240" x2="368" y2="240" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="310" y1="276" x2="368" y2="276" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="310" y1="312" x2="368" y2="312" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="20" y="356" font-size="13" font-weight="bold">kobject가 아닌 것들</text>
  <rect x="20" y="366" width="290" height="28" rx="4" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="30" y="385" font-size="12">attribute (show/store 함수 쌍)</text>
  <rect x="370" y="366" width="290" height="28" rx="4" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="380" y="385" font-size="12">파일: uevent, dev, bind, unbind …</text>
  <rect x="20" y="402" width="290" height="28" rx="4" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="30" y="421" font-size="12">sysfs_create_link() 호출</text>
  <rect x="370" y="402" width="290" height="28" rx="4" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="380" y="421" font-size="12">링크: driver, subsystem, device …</text>
  <line x1="310" y1="380" x2="368" y2="380" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
  <line x1="310" y1="416" x2="368" y2="416" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
</svg>
```
그림 2 — kobject ↔ sysfs 대응. 실선 상자는 kobject를 품은 커널 객체이고 각각 sysfs **디렉터리** 하나가 된다. 점선 상자의 attribute는 **파일**, `sysfs_create_link()`는 **심볼릭 링크**가 된다. kobject의 `parent` 포인터가 곧 디렉터리의 부모다.

---

## 4. kobject · kset · ktype

### 4.1 정의

랩 헤더(`include/linux/kobject.h`)에서 확인한 6.18 정의:

```text
struct kobject {
	const char		*name;
	struct list_head	entry;
	struct kobject		*parent;
	struct kset		*kset;
	const struct kobj_type	*ktype;
	struct kernfs_node	*sd; /* sysfs directory entry */
	struct kref		kref;

	unsigned int state_initialized:1;
	unsigned int state_in_sysfs:1;
	unsigned int state_add_uevent_sent:1;
	unsigned int state_remove_uevent_sent:1;
	unsigned int uevent_suppress:1;
	...
};
struct kobj_type {
	void (*release)(struct kobject *kobj);
	const struct sysfs_ops *sysfs_ops;
	const struct attribute_group **default_groups;
	...
};
struct kset {
	struct list_head list;
	spinlock_t list_lock;
	struct kobject kobj;
	const struct kset_uevent_ops *uevent_ops;
} __randomize_layout;
```

| 개념 | 한 줄 | 펌웨어 비유 |
|---|---|---|
| kobject | 이름 + 부모 + 참조 카운트 + sysfs 디렉터리. 혼자 쓰지 않고 큰 구조체에 임베드 | 모든 HAL 객체가 상속하는 "base object" 헤더 |
| ktype (`kobj_type`) | 같은 종류 kobject들의 공통 동작: `release`, sysfs 읽기/쓰기 방법, 기본 속성 | C++의 vtable |
| kset | kobject들의 묶음. 자기도 kobject라서 디렉터리가 되고, uevent 필터링을 담당 | 컨테이너 리스트 + 그 리스트 자체의 헤더 |
| kref | `refcount_t` 하나. 0이 되면 `ktype->release` | 펌웨어의 ref count, 단 오버/언더플로를 검사 |

`struct device`의 ktype은 `drivers/base/core.c`의 `device_ktype`이고, 그 `release`가 §5의 `device_release()`다. 드라이버는 kobject를 직접 쓰는 일이 거의 없다. 대신 **`struct device`가 kobject를 감싼 래퍼**라고 생각하면 된다.

`state_*` 비트에서 `state_add_uevent_sent`가 눈에 띈다. 커널은 "add uevent를 보낸 kobject"를 기억했다가, 그 객체를 제거할 때 remove uevent를 자동으로 보낸다.

### 4.2 예제 5 — device 없이 kobject/kset/ktype만으로 디렉터리 만들기

무엇을 확인하나: 디바이스 모델 밑바닥의 세 부품만으로 sysfs 디렉터리·파일을 만들고, `kobject_put()`이 `release()`를 부르는 것.

```c
// g2_kobj.c — device 계층 없이 kobject/kset/ktype만으로 sysfs 디렉터리 만들기
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kobject.h>

struct g2_obj {				/* "센서 채널" 하나 */
	int odr_hz;
	struct kobject kobj;		/* 임베드: 수명은 kobj의 kref가 결정 */
};

static ssize_t odr_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%d\n", container_of(k, struct g2_obj, kobj)->odr_hz);
}
static struct kobj_attribute odr_attr = __ATTR_RO(odr);
static struct attribute *g2_attrs[] = { &odr_attr.attr, NULL };
ATTRIBUTE_GROUPS(g2);
static void g2_obj_release(struct kobject *k)
{
	pr_info("g2_kobj: release(%s)\n", kobject_name(k));
	kfree(container_of(k, struct g2_obj, kobj));
}
static const struct kobj_type g2_ktype = {
	.release = g2_obj_release,
	.sysfs_ops = &kobj_sysfs_ops,	/* kobj_attribute의 show/store로 연결 */
	.default_groups = g2_groups,
};

static struct kset *g2_kset;		/* /sys/kernel/g2_kset */
static struct g2_obj *obj;
static int __init g2_kobj_init(void)
{
	int ret;

	g2_kset = kset_create_and_add("g2_kset", NULL, kernel_kobj);
	if (!g2_kset)
		return -ENOMEM;
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj) {
		kset_unregister(g2_kset);
		return -ENOMEM;
	}
	obj->odr_hz = 400;
	obj->kobj.kset = g2_kset;	/* 부모를 안 주면 kset이 부모가 된다 */
	ret = kobject_init_and_add(&obj->kobj, &g2_ktype, NULL, "imu_accel");
	if (ret) {
		kobject_put(&obj->kobj);	/* 실패해도 put — release가 kfree */
		kset_unregister(g2_kset);
	}
	return ret;
}
static void __exit g2_kobj_exit(void)
{
	kobject_put(&obj->kobj);
	kset_unregister(g2_kset);
}
module_init(g2_kobj_init);
module_exit(g2_kobj_exit);
MODULE_DESCRIPTION("G2 raw kobject demo");
MODULE_LICENSE("GPL");
```

```sh
insmod /g2_kobj.ko
tree /sys/kernel/g2_kset
cat /sys/kernel/g2_kset/imu_accel/odr
rmmod g2_kobj; dmesg | grep g2_kobj; ls /sys/kernel | grep -c g2
```

```text
/sys/kernel/g2_kset
└── imu_accel
    └── odr

1 directories, 1 files
400
[    8.952724] g2_kobj: loading out-of-tree module taints kernel.
[    9.095277] g2_kobj: release(imu_accel)
0
```

출력에서 볼 것:

- kset `g2_kset` → 디렉터리, 그 안의 kobject `imu_accel` → 디렉터리, attribute `odr` → 파일. 그림 2의 규칙 그대로다.
- `cat odr`의 경로: VFS `read` → kernfs → `sysfs_ops->show` = `kobj_attr_show()` → 우리 `odr_show()`. `struct device`의 속성(`DEVICE_ATTR`)도 같은 길을 `dev_sysfs_ops`로 간다(G8).
- `rmmod`에서 `kobject_put()` 한 번으로 refcount가 1 → 0이 되어 `g2_obj_release()`가 불리고 디렉터리도 사라졌다(`grep -c` = 0).
- `kobject_init_and_add()`가 **실패해도 `kobject_put()`**으로 정리한다. init 단계에서 이미 kref가 1이 되었고 이름 문자열도 할당됐을 수 있으므로 `kfree()`를 직접 하면 안 된다. `Documentation/core-api/kobject.rst`가 강조하는 규칙이다.

> 실무 조언: 드라이버에서 raw kobject를 쓸 일은 거의 없다. 장치 속성은 `struct device`에 `DEVICE_ATTR`/attribute group으로 붙인다(G8). raw kobject는 `/sys/kernel/mm/transparent_hugepage/` 같은 "장치가 아닌 커널 설정 노브"용이다.

### 4.3 예제 6 — uevent: kobject가 추가될 때 유저 공간이 듣는 것

무엇을 확인하나: `device_add()`가 보내는 uevent의 내용, 그리고 버스 `uevent` 콜백이 무엇을 덧붙이는지. 게스트에 udev가 없으므로 BusyBox `uevent` 애플릿(netlink 수신기)으로 듣는다.

```sh
uevent sh -c 'echo "[$SEQNUM] $ACTION $DEVPATH SUBSYSTEM=$SUBSYSTEM DRIVER=$DRIVER MODALIAS=$MODALIAS DEVNAME=$DEVNAME"' > /tmp/ev.txt &
sleep 1
insmod /g2bus.ko; insmod /g2_board.ko; insmod /g2_imu.ko
sleep 1
grep -v "module/" /tmp/ev.txt
cat /sys/bus/g2bus/devices/imu0/uevent
cat /sys/class/g2class/sensorkit0/uevent
cat /sys/bus/g2bus/drivers/sensorkit-imu/uevent
```

```text
[2212] add /bus/g2bus SUBSYSTEM=bus DRIVER= MODALIAS= DEVNAME=
[2213] add /class/g2class SUBSYSTEM=class DRIVER= MODALIAS= DEVNAME=
[2215] add /devices/g2root/pmic0 SUBSYSTEM=g2bus DRIVER= MODALIAS=g2:pmic DEVNAME=
[2216] add /devices/g2root/imu0 SUBSYSTEM=g2bus DRIVER= MODALIAS=g2:imu DEVNAME=
[2217] add /devices/g2root/imu1 SUBSYSTEM=g2bus DRIVER= MODALIAS=g2:imu DEVNAME=
[2219] add /devices/g2root/imu0/g2class/sensorkit0 SUBSYSTEM=g2class DRIVER= MODALIAS= DEVNAME=sensorkit0
[2220] bind /devices/g2root/imu0 SUBSYSTEM=g2bus DRIVER=sensorkit-imu MODALIAS=g2:imu DEVNAME=
[2221] add /devices/g2root/imu1/g2class/sensorkit1 SUBSYSTEM=g2class DRIVER= MODALIAS= DEVNAME=sensorkit1
[2222] bind /devices/g2root/imu1 SUBSYSTEM=g2bus DRIVER=sensorkit-imu MODALIAS=g2:imu DEVNAME=
[2223] add /bus/g2bus/drivers/sensorkit-imu SUBSYSTEM=drivers DRIVER= MODALIAS= DEVNAME=
DRIVER=sensorkit-imu
MODALIAS=g2:imu
--
MAJOR=510
MINOR=0
DEVNAME=sensorkit0
--
cat: can't open '/sys/bus/g2bus/drivers/sensorkit-imu/uevent': Permission denied
```

(`/module/...` 경로의 모듈 uevent 3개는 `grep -v`로 뺐다 — 그래서 SEQNUM 2211, 2214, 2218이 비어 있다. `--`는 `echo` 구분선이다.)

출력에서 볼 것:

- `MODALIAS=g2:imu`는 우리 `g2_uevent()`가 덧붙인 것이다. 진짜 버스라면 udev가 이 문자열로 `modprobe`를 불러 드라이버를 자동 로드한다(D3 §9, G9). 우리는 `MODULE_ALIAS("g2:imu")`를 안 넣었으므로 자동 로딩은 안 된다.
- **`g2root`의 add 이벤트가 없다.** `drivers/base/core.c`의 `dev_uevent_filter()`가 "버스도 클래스도 없는 장치"의 uevent를 걸러 낸다. 루트 장치는 둘 다 없다.
- 순서를 보라: 클래스 장치 add(2219) → **`bind` 이벤트**(2220) → 다음 장치 → 마지막에 드라이버 add(2223). probe가 성공하면 `driver_bound()`가 `KOBJ_BIND`를 보낸다. 드라이버 디렉터리의 add uevent는 `driver_register()`가 `bus_add_driver()`(= 모든 장치와 매칭·probe) **이후에** 보낸다 — §2.2에서 본 소스 순서 그대로다.
- `sensorkit0`의 `uevent`에 `MAJOR`/`MINOR`/`DEVNAME`이 있다. devtmpfs와 udev는 이 세 줄로 `/dev` 노드를 만든다.
- 드라이버의 `uevent` 파일은 **쓰기 전용**이다(`drivers/base/bus.c`의 `DRIVER_ATTR_WO(uevent)`). 장치의 `uevent`는 읽기·쓰기 둘 다 되고, `echo add > uevent`로 이벤트를 다시 보낼 수 있다 — udev coldplug(`udevadm trigger`)가 이걸 쓴다.

---

## 5. kref와 `release()` 규칙

### 5.1 직관 — "마지막으로 나가는 사람이 불을 끈다"

Don의 펌웨어에서 커맨드 하나가 NAND 채널 객체를 쓰는 동안 그 채널을 리셋하면 안 된다. 그래서 "사용 중 카운터"를 올렸다 내렸다. 그런데 펌웨어에서는 보통 **해제 시점을 한 곳(예: 전원 꺼짐 처리)에서 정한다**.

커널에는 그런 "한 곳"이 없다. `struct device`를 가리키는 포인터는 driver core의 klist, 부모의 자식 목록, sysfs에서 파일을 열어 둔 사용자 프로세스, 다른 드라이버의 device link, 비동기 probe 작업 … 등 여러 곳에 흩어져 있다. 그래서 규칙은 하나다.

> **아무도 "지금 해제하라"고 결정하지 않는다. 참조 카운트가 0이 되는 순간, 마지막 `put`을 한 쪽에서 `release()`가 불린다.**

이 때문에 **`struct device`를 담은 메모리는 반드시 `release()` 안에서만 해제**해야 한다. `device_unregister()` 다음 줄에서 `kfree()`하면, 그 순간에도 누군가 참조를 들고 있을 수 있다.

### 5.2 `device_register()` vs `device_initialize()` + `device_add()`

랩 소스에서 확인한 두 함수:

```text
int device_register(struct device *dev)
{
	device_initialize(dev);
	return device_add(dev);
}
...
void device_unregister(struct device *dev)
{
	pr_debug("device: '%s': %s\n", dev_name(dev), __func__);
	device_del(dev);
	put_device(dev);
}
```

| 함수 | 하는 일 | refcount |
|---|---|---|
| `device_initialize()` | kobject 초기화(ktype = `device_ktype`), 목록·락·devres 초기화 | 1이 된다 |
| `device_add()` | 이름 결정, sysfs 디렉터리·속성·링크, 버스·클래스 목록 추가, uevent, fw_devlink, **매칭 시도** | 목록들이 각자 참조를 잡는다 |
| `device_register()` | 위 둘을 연달아 | — |
| `device_del()` | `device_add()`의 역: sysfs·목록 제거, unbind(`remove()`), remove uevent | 목록들이 잡은 참조를 놓는다 |
| `put_device()` | `kobject_put()` | 1 감소, 0이면 `release()` |
| `device_unregister()` | `device_del()` + `put_device()` | `device_initialize()`의 1을 반납 |

언제 나눠 쓰나: `device_add()` **전에** 필드를 채우거나 실패 처리를 해야 할 때다. 우리 `g2_device_create()`처럼 `device_initialize()` 다음에 `bus`·`parent`·`release`·이름을 채우고 `device_add()`한다. 핵심 규칙 두 개:

- `device_initialize()`를 부른 뒤에는 **실패해도 `kfree()`가 아니라 `put_device()`**로 정리한다. `device_add()`의 커널 주석이 "NOTE: _Never_ directly free @dev after calling this function, even if it returned an error! Always use put_device() to give up your reference instead."라고 적어 둔 그대로다.
- `device_add()`에 성공한 장치는 `device_del()` + `put_device()`(= `device_unregister()`)로 내린다.

### 5.3 `kref=3`, `parent kref=5`의 정체

예제 4에서 본 숫자를 소스로 하나씩 맞춰 보자 (`kref_read()`는 디버그용으로 찍은 것이고, 드라이버 로직에서 이 값을 쓰면 안 된다).

| 시점 | 값 | 참조를 잡은 쪽 (소스 근거) |
|---|---|---|
| `device_initialize()` 직후 | 1 | 등록한 쪽(우리) — `kobject_init()` → `kref_init()` |
| `device_add()` 후 (`added imu0, kref=3`) | 3 | +1 버스 장치 klist: `klist_init(..., klist_devices_get, ...)`가 노드 추가 때 `get_device()` (bus.c)<br>+1 부모의 자식 klist: `klist_children_get()` (core.c) |
| 드라이버 probe 진입 (`probe(imu0) kref=3`) | 3 | 변화 없음 |
| 클래스 장치 생성 후 (`parent kref=5`) | 5 | +1 자식 `device_add()`의 `get_device(dev->parent)` (`device_del()`의 `put_device(parent)`까지 유지)<br>+1 glue dir `g2class` kobject가 부모 kobject를 잡음 (`kobject_add()`) |
| sysfs `bind`로 다시 probe (`kref=4`, §7) | 4 | +1 `bind_store()`의 `bus_find_device_by_name()`이 잡고 끝나면 `put_device()` |
| device link + deferred 재시도 probe (`kref=5`, §8) | 5 | +1 `device_link_add()`의 `get_device(consumer)`<br>+1 `deferred_probe_work_func()`의 `get_device(dev)` |

펌웨어 감각으로는 "카운트가 3이면 누가 3명이지?"를 추적하는 게 귀찮아 보이지만, 핵심은 **각 참조 주체가 자기 몫만 get/put한다**는 것이다. 우리는 우리 몫(1)만 책임지면 된다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <line x1="60" y1="250" x2="660" y2="250" stroke="currentColor"/>
  <line x1="60" y1="250" x2="60" y2="30" stroke="currentColor"/>
  <text x="50" y="254" text-anchor="end" font-size="12">0</text>
  <text x="50" y="214" text-anchor="end" font-size="12">1</text>
  <text x="50" y="174" text-anchor="end" font-size="12">2</text>
  <text x="50" y="134" text-anchor="end" font-size="12">3</text>
  <text x="50" y="94" text-anchor="end" font-size="12">4</text>
  <text x="50" y="54" text-anchor="end" font-size="12">5</text>
  <line x1="56" y1="210" x2="660" y2="210" stroke="#888" stroke-opacity="0.3"/>
  <line x1="56" y1="130" x2="660" y2="130" stroke="#888" stroke-opacity="0.3"/>
  <line x1="56" y1="50" x2="660" y2="50" stroke="#888" stroke-opacity="0.3"/>
  <text x="20" y="22" font-size="12">kref</text>
  <polyline points="90,210 180,210 180,130 300,130 300,50 420,50 420,130 540,130 540,250 640,250" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <circle cx="90" cy="210" r="5" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <circle cx="180" cy="130" r="5" fill="#4a7bd0"/>
  <circle cx="240" cy="130" r="5" fill="#4a7bd0"/>
  <circle cx="300" cy="50" r="5" fill="#4a7bd0"/>
  <circle cx="420" cy="130" r="5" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <circle cx="540" cy="250" r="5" fill="#d0564a"/>
  <text x="90" y="270" text-anchor="middle" font-size="12">initialize</text>
  <text x="180" y="270" text-anchor="middle" font-size="12">device_add</text>
  <text x="240" y="288" text-anchor="middle" font-size="12">probe 진입</text>
  <text x="300" y="270" text-anchor="middle" font-size="12">자식 생성</text>
  <text x="420" y="270" text-anchor="middle" font-size="12">unbind</text>
  <text x="540" y="270" text-anchor="middle" font-size="12">unregister</text>
  <text x="560" y="230" font-size="12">release()</text>
  <text x="560" y="214" font-size="12">→ kfree</text>
  <rect x="430" y="300" width="230" height="46" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="440" y="319" font-size="12">0 이후 접근 = use-after-free</text>
  <text x="440" y="337" font-size="12">0에서 또 put = underflow WARN</text>
  <line x1="540" y1="258" x2="545" y2="298" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="40" y="300" width="370" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="50" y="319" font-size="12">0이 됐는데 release()가 없으면</text>
  <text x="50" y="337" font-size="12">→ WARN "does not have a release() function"</text>
  <text x="200" y="22" font-size="12">● 실측 (kref_read 로그 / release 로그)   ○ 소스로 유도</text>
</svg>
```
그림 4 — `imu0` 하나의 refcount 일생. 채운 점은 예제 4·7의 실제 로그 값, 빈 점은 소스에서 유도한 값이다(`initialize` = 1, unbind로 클래스 장치가 사라지면 그 두 참조가 빠져 3). `device_unregister()`가 `device_del()`로 목록 참조 2개를 놓고 `put_device()`로 마지막 1을 놓는 순간 `release()`가 불린다.

### 5.4 예제 7 — `release()` 없는 device → 커널 WARN

무엇을 확인하나: `release` 콜백 없이 등록한 장치를 내리면 무슨 일이 나는지. 일회용 QEMU 게스트에서만 돌린다.

```c
// g2_bad.c — refcount 실수 재현 (일회용 게스트에서만!)  mode=1: release 없음,
// 2: 너무 이른 put → UAF, 3: get_device로 고친 버전, 4: kref 이중 put
#include <linux/module.h>
#include <linux/slab.h>
#include "g2bus.h"

static int mode;
module_param(mode, int, 0444);

static void g2_bad_mode1(void)
{
	struct device *dev = kzalloc(sizeof(*dev), GFP_KERNEL);

	if (!dev)
		return;
	dev_set_name(dev, "g2_norelease");
	if (device_register(dev))		/* .release 를 안 채웠다 */
		pr_warn("g2_bad: register failed\n");
	device_unregister(dev);			/* kref 0 → WARN, dev는 누수 */
}

static void g2_bad_mode23(bool hold)
{
	struct g2_device *gd = g2_device_create("g2_victim", "none");

	if (IS_ERR(gd))
		return;
	if (hold)
		get_device(&gd->dev);		/* 내가 쓸 동안 살려 둔다 */
	device_unregister(&gd->dev);		/* 등록할 때 받은 참조를 반납 */
	pr_info("g2_bad: after unregister, magic=0x%08x\n", gd->magic);
	if (hold)
		put_device(&gd->dev);		/* 마지막 참조 → 여기서 release */
}

static void g2_kref_release(struct kref *k)
{
	pr_info("g2_bad: kref release\n");
}

static int __init g2_bad_init(void)
{
	struct kref k;

	switch (mode) {
	case 1: g2_bad_mode1(); break;
	case 2: g2_bad_mode23(false); break;
	case 3: g2_bad_mode23(true); break;
	case 4:
		kref_init(&k);
		pr_info("g2_bad: kref=%u\n", kref_read(&k));
		kref_put(&k, g2_kref_release);	/* 1 → 0: release */
		kref_put(&k, g2_kref_release);	/* 0 → ??? 이중 put */
		pr_info("g2_bad: kref=%u\n", kref_read(&k));
		break;
	}
	return -EAGAIN;				/* 일부러 로드 실패 → 바로 언로드 */
}
module_init(g2_bad_init);
MODULE_DESCRIPTION("G2 refcount bugs");
MODULE_LICENSE("GPL");
```

```sh
insmod /g2bus.ko
for m in 1 2 3 4; do
  echo "===== mode=$m"; dmesg -c > /dev/null
  insmod /g2_bad.ko mode=$m
  dmesg -c | head -40
done
```

mode=1 출력 (레지스터 덤프 일부를 `...`로 줄였다):

```text
===== mode=1
insmod: can't insert '/g2_bad.ko': Resource temporarily unavailable
[   12.861565] ------------[ cut here ]------------
[   12.861635] Device 'g2_norelease' does not have a release() function, it is broken and must be fixed. See Documentation/core-api/kobject.rst.
[   12.862486] WARNING: CPU: 1 PID: 75 at drivers/base/core.c:2634 device_release+0x90/0xa8
[   12.863603] Modules linked in: g2_bad(O+) g2bus(O)
[   12.864952] CPU: 1 UID: 0 PID: 75 Comm: insmod Tainted: G           O        6.18.55-dbg #1 PREEMPT 
...
[   12.866174] Call trace:
[   12.866350]  device_release+0x90/0xa8 (P)
[   12.866489]  kobject_put+0xb0/0x230
[   12.866546]  device_unregister+0x34/0x88
[   12.866575]  g2_bad_init+0xe0/0xf80 [g2_bad]
...
```

출력에서 볼 것: 호출 경로가 `device_unregister` → `kobject_put`(카운트 0) → `device_release`다. `drivers/base/core.c:2634`가 바로 이 코드다 (랩 소스에서 확인):

```text
	if (dev->release)
		dev->release(dev);
	else if (dev->type && dev->type->release)
		dev->type->release(dev);
	else if (dev->class && dev->class->dev_release)
		dev->class->dev_release(dev);
	else
		WARN(1, KERN_ERR "Device '%s' does not have a release() function, it is broken and must be fixed. See Documentation/core-api/kobject.rst.\n",
			dev_name(dev));
```

`release`는 **세 군데 중 한 곳**에 있으면 된다: 장치 자신(`dev->release`), 장치 타입(`device_type.release`), 클래스(`class.dev_release`). platform 버스는 `platform_device_alloc()`이 `platform_device_release()`를 넣어 주고, `device_create()`는 `device_create_release()`를 넣어 준다. 그래서 보통 드라이버 작성자는 이 WARN을 볼 일이 없다 — **`struct device`를 직접 할당하는 버스·서브시스템 코드를 쓸 때** 만난다.

"빈 release 함수를 넣으면 되지 않나?" — 경고는 사라지지만 메모리는 누수되고, 정적 변수였다면 모듈이 내려간 뒤 누군가 참조하면 사라진 코드·데이터를 건드린다. 커널 문서가 "빈 release로 경고를 숨기지 말라"고 명시하는 이유다.

### 5.5 예제 8 — 너무 이른 put → use-after-free, 그리고 고친 버전

무엇을 확인하나: `device_unregister()`가 마지막 참조를 놓은 뒤에 포인터를 쓰면 이미 해제된 메모리를 읽는다는 것. 그리고 그게 **디버그 옵션 없이는 안 보인다**는 것.

mode=2를 `slab_debug=P`(SLUB poison: 해제된 객체를 `0x6b`로 채움) 커널 인자로 부팅해서 돌린 결과:

```text
===== mode=2
insmod: can't insert '/g2_bad.ko': Resource temporarily unavailable
[   13.178777] g2bus: release(g2_victim) -> kfree
[   13.178905] g2_bad: after unregister, magic=0x6b6b6b6b
===== mode=3
insmod: can't insert '/g2_bad.ko': Resource temporarily unavailable
[   13.510429] g2_bad: after unregister, magic=0x53454e53
[   13.510505] g2bus: release(g2_victim) -> kfree
```

같은 mode=2를 `slab_debug` **없이** 부팅해서 돌린 결과:

```text
insmod: can't insert '/g2_bad.ko': Resource temporarily unavailable
[    9.472012] g2_bad: after unregister, magic=0x53454e53
```

출력에서 볼 것:

- mode=2: `release` 로그가 **먼저** 찍히고(이미 kfree), 그 다음 읽은 `magic`이 poison 값 `0x6b6b6b6b`다. 명백한 use-after-free.
- 같은 버그를 poison 없이 돌리면 `0x53454e53` — **정상 값처럼 보인다**. 해제된 메모리가 아직 재사용되지 않았을 뿐이다. 실제 보드에서는 이 상태로 몇 주 돌다가 부하가 걸린 날 랜덤 Oops로 터진다. 그래서 refcount 버그는 `slab_debug`, KASAN(`CONFIG_KASAN`, 이 랩 커널에는 꺼져 있다) 같은 도구로 잡는다.
- mode=3(고친 버전): 쓰기 전에 `get_device()`로 내 몫의 참조를 잡았다. `device_unregister()` 후에도 카운트가 1 남아 있어 `magic`이 진짜 값이고, 내가 `put_device()`할 때 비로소 `release`가 불린다(로그 순서가 바뀐 것을 보라).

### 5.6 예제 9 — 0에서 한 번 더 put → `refcount_t: underflow`

무엇을 확인하나: kref 안의 `refcount_t`가 0 이하로 내려가는 것을 잡아 준다는 것 (예제 7과 같은 모듈, mode=4).

```text
[   23.948871] g2_bad: kref=1
[   23.949006] g2_bad: kref release
[   23.949166] refcount_t: underflow; use-after-free.
[   23.951065] Modules linked in: g2_bad(O+) g2bus(O)
[   23.954113]  kref_put.constprop.0.isra.0+0x60/0xed8 [g2_bad]
[   23.954577]  g2_bad_init+0x88/0xf80 [g2_bad]
[   23.956283] g2_bad: kref=3221225472
```

(`dmesg | grep -E "g2_bad|refcount_t"`로 줄만 골랐다. 전체 WARN에는 `WARNING: ... at lib/refcount.c:28 refcount_warn_saturate+0xf4/0x148`와 레지스터 덤프가 있다.)

출력에서 볼 것: 두 번째 `kref_put()`에서 release가 **다시 불리지 않았다** — 이중 kfree는 막혔다. 대신 WARN을 찍고 값을 `3221225472` = `0xC0000000`(`REFCOUNT_SATURATED`)에 고정한다. 포화 상태에서는 더 이상 0에 도달하지 않으므로 메모리는 누수되지만 이중 해제·UAF로 번지지 않는다. 펌웨어의 단순한 `int` 카운터라면 -1이 되고 그다음 `++`로 0이 되어 엉뚱한 시점에 해제됐을 것이다. 다만 실제 `struct device`에서는 해제된 메모리 위의 refcount를 건드리는 경우가 많아(예제 8), 이 검사만으로는 다 못 잡는다.

---

## 6. 두 입구 — `driver_register()`와 `device_add()`가 만나는 곳

### 6.1 경로 (6.18 소스에서 확인)

D3 §5에서 platform 버스로 한 번 봤다. 여기서는 우리 버스로, **ftrace로 실제 호출 스택을 찍어서** 확인한다.

- **드라이버가 나중에 올 때**: `driver_register()` → `bus_add_driver()` → (버스의 `drivers_autoprobe`가 1이면) `driver_attach()` → `bus_for_each_dev(bus, NULL, drv, __driver_attach)` → `__driver_attach()` → `driver_match_device()` → (양수면) `driver_probe_device()` → `__driver_probe_device()` → `really_probe()`.
- **장치가 나중에 올 때**: `device_add()` → `bus_probe_device()` → (autoprobe면) `device_initial_probe()` → `__device_attach()` → `bus_for_each_drv(bus, NULL, &data, __device_attach_driver)` → `__device_attach_driver()` → `driver_match_device()` → `driver_probe_device()` → `__driver_probe_device()` → `really_probe()`.

`driver_match_device()`는 `drivers/base/base.h`의 인라인 함수로 `drv->bus->match(dev, drv)`를 부른다 — 우리 경우 `g2_match()`. `really_probe()`는 다시 `call_driver_probe()`에서 `dev->bus->probe`(= `g2_probe()`)를 부르고, 그것이 `imu_probe()`/`pmic_probe()`로 위임한다.

### 6.2 예제 10 — `really_probe`에 스택 트레이스를 걸어 두 입구를 찍기

무엇을 확인하나: 같은 `really_probe()`가 어떤 경로로 불렸는지. 디버그 커널의 function tracer + `func_stack_trace` 옵션을 쓴다.

```sh
mount -t tracefs none /sys/kernel/tracing
T=/sys/kernel/tracing
insmod /g2bus.ko; insmod /g2_board.ko             # 장치 먼저
echo really_probe > $T/set_ftrace_filter
echo function > $T/current_tracer
echo 1 > $T/options/func_stack_trace
insmod /g2_pmic.ko                                # A: 드라이버가 나중에
grep -v "^#" $T/trace
# (트레이서를 끄고 g2_pmic, g2_board를 내린 뒤)
insmod /g2_pmic.ko; echo > $T/trace
insmod /g2_board.ko                               # B: 장치가 나중에
grep -v "^#" $T/trace | head -40
```

```text
== A: driver comes last (insmod g2_pmic)
          insmod-77      [000] .....     9.263586: really_probe <-__driver_probe_device
          insmod-77      [000] .....     9.264249: <stack trace>
 => really_probe
 => __driver_probe_device
 => driver_probe_device
 => __driver_attach
 => bus_for_each_dev
 => driver_attach
 => bus_add_driver
 => driver_register
 => __g2_driver_register
 => g2_pmic_init
 => do_one_initcall
 => do_init_module
 => load_module
 => __do_sys_init_module
 => __arm64_sys_init_module
 => invoke_syscall.constprop.0
 => el0_svc_common.constprop.0
 => do_el0_svc
 => el0_svc
 => el0t_64_sync_handler
 => el0t_64_sync
== B: device comes last (insmod g2_board after drivers)
          insmod-82      [001] .....    10.257925: really_probe <-__driver_probe_device
          insmod-82      [001] .....    10.257943: <stack trace>
 => really_probe
 => __driver_probe_device
 => driver_probe_device
 => __device_attach_driver
 => bus_for_each_drv
 => __device_attach
 => device_initial_probe
 => bus_probe_device
 => device_add
 => g2_device_create
 => g2_board_init
 ...   (do_one_initcall 아래 10줄은 A와 같다)
```

출력에서 볼 것:

- 위쪽 세 프레임(`really_probe` ← `__driver_probe_device` ← `driver_probe_device`)이 **완전히 같고**, 그 아래만 다르다. A는 `bus_for_each_dev`(장치 목록 순회), B는 `bus_for_each_drv`(드라이버 목록 순회).
- 맨 아래는 둘 다 `el0t_64_sync` → `el0_svc` → `init_module` syscall이다. **`probe()`는 `insmod`를 실행한 프로세스의 문맥에서 동기적으로 불린다**(기본값). 그래서 `insmod`는 probe가 끝날 때까지 안 돌아온다. `probe_type = PROBE_PREFER_ASYNCHRONOUS`를 주면 `__driver_attach()`가 `async_schedule_dev()`로 넘긴다(소스에서 확인, 이 노트에서는 실행 안 함).
- Don의 펌웨어로 치면 `insmod` = "SVC 예외로 커널 진입 → 모듈 로더가 init 함수 호출 → 그 안에서 장치 init까지"가 한 호출 체인 안에서 끝나는 것이다.

```svg
<svg viewBox="0 0 680 520" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="22" text-anchor="middle" font-size="13" font-weight="bold">A. insmod g2_pmic.ko (드라이버가 나중)</text>
  <text x="510" y="22" text-anchor="middle" font-size="13" font-weight="bold">B. insmod g2_board.ko (장치가 나중)</text>
  <rect x="20" y="34" width="300" height="168" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="34" y="56" font-size="12">g2_pmic_init → __g2_driver_register</text>
  <text x="34" y="78" font-size="12">→ driver_register()</text>
  <text x="34" y="100" font-size="12">→ bus_add_driver()</text>
  <text x="34" y="122" font-size="12">→ driver_attach()</text>
  <text x="34" y="144" font-size="12">→ bus_for_each_dev(): 장치마다</text>
  <text x="34" y="166" font-size="12">→ __driver_attach(dev, drv)</text>
  <text x="34" y="188" font-size="12">   이미 driver 있으면 건너뜀</text>
  <rect x="360" y="34" width="300" height="168" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="374" y="56" font-size="12">g2_board_init → g2_device_create</text>
  <text x="374" y="78" font-size="12">→ device_add()</text>
  <text x="374" y="100" font-size="12">→ bus_probe_device()</text>
  <text x="374" y="122" font-size="12">→ device_initial_probe()</text>
  <text x="374" y="144" font-size="12">→ __device_attach()</text>
  <text x="374" y="166" font-size="12">→ bus_for_each_drv(): 드라이버마다</text>
  <text x="374" y="188" font-size="12">→ __device_attach_driver()</text>
  <line x1="170" y1="202" x2="290" y2="234" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="510" y1="202" x2="390" y2="234" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="140" y="236" width="400" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="258" text-anchor="middle" font-size="12">driver_match_device() → bus-&gt;match = g2_match()</text>
  <rect x="140" y="282" width="400" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="304" text-anchor="middle" font-size="12">driver_probe_device() → __driver_probe_device()</text>
  <rect x="140" y="328" width="400" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="350" text-anchor="middle" font-size="12">really_probe(): supplier 확인 · pinctrl · sysfs 링크</text>
  <rect x="140" y="374" width="400" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="396" text-anchor="middle" font-size="12">call_driver_probe() → bus-&gt;probe = g2_probe()</text>
  <rect x="140" y="420" width="400" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="442" text-anchor="middle" font-size="12" font-weight="bold">g2_driver.probe = imu_probe() / pmic_probe()</text>
  <rect x="140" y="466" width="400" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="488" text-anchor="middle" font-size="12">driver_bound(): 목록 추가 · KOBJ_BIND · defer 재시도</text>
  <line x1="340" y1="270" x2="340" y2="280" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="340" y1="316" x2="340" y2="326" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="340" y1="362" x2="340" y2="372" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="340" y1="408" x2="340" y2="418" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="340" y1="454" x2="340" y2="464" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="560" y="258" font-size="12">0이면 다음 후보</text>
</svg>
```
그림 3 — 두 입구가 `driver_match_device()`에서 합류한다. 위 두 상자는 예제 10의 ftrace 스택 그대로이고, 아래 상자들은 예제 11의 function_graph로 확인한 `really_probe()` 내부다. 어느 쪽이 먼저 오든 결과(bind)는 같다 — 그래서 모듈 로드 순서에 의존하지 않는 드라이버를 쓸 수 있다.

### 6.3 예제 11 — function_graph로 `really_probe()` 안을 보기

무엇을 확인하나: `really_probe()`가 드라이버 probe 앞뒤에 무엇을 하는지, 그리고 각 단계의 시간.

```sh
insmod /g2bus.ko; insmod /g2_board.ko
echo really_probe > $T/set_graph_function
echo 2 > $T/max_graph_depth
echo function_graph > $T/current_tracer
insmod /g2_pmic.ko
grep -v "^#" $T/trace
```

```text
 0)               |  really_probe() {
 0) ! 174.240 us  |    device_links_check_suppliers();
 0) ! 534.336 us  |    pinctrl_bind_pins();
 0) ! 410.752 us  |    driver_sysfs_add();
 0) ! 127.040 us  |    g2_probe [g2bus]();
 0)   7.840 us    |    device_add_groups();
 0)   5.712 us    |    pinctrl_init_done();
 0) # 1284.928 us |    driver_bound();
 0) # 3426.992 us |  }
```

한 단계 더 들어가 우리 코드 쪽을 본다 (`g2_probe`를 그래프 시작점으로, 깊이 3, 인터럽트 줄은 `options/funcgraph-irqs`=0으로 끔):

```sh
echo 0 > $T/options/funcgraph-irqs
echo g2_probe > $T/set_graph_function
echo 3 > $T/max_graph_depth
echo function_graph > $T/current_tracer
insmod /g2_imu.ko
```

```text
 0)               |  g2_probe [g2bus]() {
 0)               |    imu_probe [g2_imu]() {
 0) ! 236.080 us  |      _printk();
 0) # 7708.176 us |      device_create();
 0) + 94.576 us   |      _printk();
 0) # 8320.800 us |    }
 0) # 9033.232 us |  }
 0)               |  g2_probe [g2bus]() {
 0)               |    imu_probe [g2_imu]() {
 0) + 79.792 us   |      _printk();
 0) # 4817.104 us |      device_create();
 0) + 81.696 us   |      _printk();
 0) # 5003.680 us |    }
 0) # 5018.464 us |  }
```

출력에서 볼 것:

- `really_probe()`의 직계 자식이 D3 §5에서 소스로 읽은 순서와 같다: 공급자 링크 검사 → pinctrl 기본 상태 적용 → sysfs `driver` 링크 → **버스 probe** → 드라이버 `dev_groups` 속성 추가 → pinctrl "init" 상태 → `driver_bound()`.
- `call_driver_probe()`는 그래프에 안 나온다. 컴파일러가 `really_probe()` 안으로 인라인했다(`set_graph_function`에 넣으면 `Invalid argument`로 거부됐다). ftrace는 인라인된 함수를 볼 수 없다는 점을 기억하자.
- 우리 `probe`에서 가장 비싼 것은 `device_create()`다 — sysfs 디렉터리·파일·링크 생성 + uevent + devtmpfs 노드. TCG 에뮬레이션 + lockdep 켠 디버그 커널이라 절대값은 실보드보다 훨씬 크다. 비율만 보자.

---

## 7. 손으로 bind/unbind, 그리고 `drivers_autoprobe`

D3 §7에서 platform 버스로 한 것을 우리 버스에서 다시 한다. 이 파일들은 **driver core가 모든 버스에 공짜로 만들어 준다** — 우리는 한 줄도 안 썼다.

### 7.1 예제 12 — unbind/bind, 매칭 실패, autoprobe 끄기

무엇을 확인하나: sysfs 쓰기가 `remove()`/`probe()`를 부르는 것, bind도 `match`를 통과해야 한다는 것, 버스 단위로 자동 probe를 끌 수 있다는 것.

```sh
insmod /g2bus.ko; insmod /g2_board.ko; insmod /g2_imu.ko
D=/sys/bus/g2bus/drivers/sensorkit-imu
dmesg -c > /dev/null
echo imu1 > $D/unbind;  dmesg -c | grep g2
ls /sys/bus/g2bus/devices/imu1/; ls /sys/class/g2class
echo imu1 > $D/bind;    dmesg -c | grep g2
readlink /sys/bus/g2bus/devices/imu1/driver
echo pmic0 > $D/bind; echo "exit=$?"; dmesg -c | grep g2
echo 0 > /sys/bus/g2bus/drivers_autoprobe
insmod /g2_pmic.ko
ls /sys/bus/g2bus/devices/pmic0/ | grep -c driver
echo pmic0 > /sys/bus/g2bus/drivers_probe; dmesg -c | grep g2
readlink /sys/bus/g2bus/devices/pmic0/driver
rmmod g2_imu; dmesg -c | grep g2
rmmod g2_pmic; rmmod g2_board; dmesg -c | grep g2
```

(출력의 `== ...` 줄은 스크립트가 단계마다 찍은 `echo` 구분선이다. 위 명령 목록에서는 생략했다. 예제 10·13도 같다.)

```text
== unbind imu1
[    5.836235] g2_imu: remove(imu1)
power      subsystem  uevent
sensorkit0
== bind imu1
[    6.157854] g2bus: match(imu1, sensorkit-imu) = 1
[    6.158415] g2_imu: probe(imu1) kref=4
[    6.162105] g2_imu: imu1 -> sensorkit2, parent kref=6
../../../bus/g2bus/drivers/sensorkit-imu
== bind pmic0 to imu driver (match fails)
sh: write error: No such device
exit=1
[    6.241639] g2bus: match(pmic0, sensorkit-imu) = 0
== drivers_autoprobe=0, then new driver
0
[    6.424982] g2bus: match(pmic0, sensorkit-imu) = 0
[    6.425152] g2bus: match(pmic0, sensorkit-pmic) = 1
[    6.429255] g2_pmic: probe(pmic0) — rails up
../../../bus/g2bus/drivers/sensorkit-pmic
== rmmod g2_imu while bound
[    6.584129] g2_imu: remove(imu1)
[    6.588097] g2_imu: remove(imu0)
== rmmod board
[    6.947721] g2bus: release(imu1) -> kfree
[    6.951141] g2bus: release(imu0) -> kfree
[    6.952202] g2bus: release(pmic0) -> kfree
```

출력에서 볼 것:

- **unbind**: `remove()`가 불리고 `driver` 링크와 클래스 장치(`g2class/`, `sensorkit1`)가 사라졌다. 장치 객체 `imu1` 자체는 그대로다 — unbind는 "드라이버를 떼는 것"이지 장치를 지우는 게 아니다.
- **bind**: 다시 `match`부터 거친다. 이번 probe의 `kref=4`는 `bind_store()`가 이름으로 장치를 찾으며 잡은 임시 참조(+1) 때문이다(§5.3 표). 새 클래스 장치 이름이 `sensorkit2`인 것은 §2.3에서 말한 단순화(minor 재사용 안 함) 때문이다.
- **매칭 안 되는 bind**: `bind_store()`는 `driver_match_device()`가 0이면 `-ENODEV`를 돌려준다. D3의 `driver_override`가 이 검사를 우회하는 장치였다.
- **`drivers_autoprobe=0`**: 드라이버를 올려도 `bus_add_driver()`가 `driver_attach()`를 건너뛰어 match 로그가 없고 `driver` 링크도 없다(`0`). `drivers_probe`에 장치 이름을 쓰면 그 장치에 대해 모든 드라이버와 매칭을 다시 시도한다(pmic0 × imu 드라이버 = 0, × pmic 드라이버 = 1). 실무에서는 "부팅 때 특정 버스의 자동 바인딩을 막고 유저 공간이 순서를 정한다"(예: VFIO로 넘길 PCI 장치) 같은 데 쓴다.
- **드라이버 rmmod**: 묶인 모든 장치의 `remove()`가 불린다(`driver_unregister()` → `driver_detach()`). 장치는 남는다.
- **보드 rmmod**: `device_unregister()` → 마지막 참조가 빠지며 `release()` → `kfree`. 여기까지 와야 메모리가 반환된다.

---

## 8. device link, deferred probe 미리보기, fw_devlink

### 8.1 직관 — "PMIC가 먼저다"

센서 키트 보드에서 IMU는 PMIC의 LDO에서 전원을 받는다. 펌웨어라면 `main()`에 `pmic_init(); imu_init();` 순서로 쓰면 끝이다. Linux에서는 모듈 로드 순서도, probe 순서도 보장되지 않는다. 그래서 driver core에 **"이 장치(consumer)는 저 장치(supplier)가 bind된 뒤에만 probe하라"**는 관계를 등록하는 기능이 있다. 이것이 **device link**다.

- `device_link_add(consumer, supplier, flags)` — 드라이버가 직접 거는 링크. flags에 `DL_FLAG_STATELESS`가 없으면 driver core가 상태를 관리하는 **managed link**가 된다.
- **fw_devlink** — DT의 phandle 참조(`*-supply`, `clocks`, `gpios`, `interrupts` …)를 읽어 커널이 **자동으로** 같은 링크를 만드는 기능. 기본값은 `fw_devlink=rpm`(`drivers/base/core.c`의 `fw_devlink_flags = FW_DEVLINK_FLAGS_RPM`)이고, 커널 인자로 `off`/`permissive`/`on`/`rpm`을 고를 수 있다.

### 8.2 예제 13 — `device_link_add()`로 deferred probe 만들기

무엇을 확인하나: supplier가 bind되지 않은 consumer는 드라이버의 `probe()`까지 가지도 못하고 deferred 목록에 들어간다는 것, supplier가 bind되는 순간 자동으로 재시도된다는 것.

```sh
insmod /g2bus.ko; insmod /g2_board.ko link=1; insmod /g2_imu.ko
dmesg | grep g2_imu
ls /sys/class/devlink | grep g2
L=/sys/class/devlink/g2bus:pmic0--g2bus:imu0
ls $L
for f in status auto_remove_on runtime_pm sync_state_only; do echo "$f: $(cat $L/$f)"; done
ls /sys/devices/g2root/imu0 /sys/devices/g2root/pmic0
cat /sys/kernel/debug/devices_deferred
dmesg -c > /dev/null; insmod /g2_pmic.ko; sleep 1; dmesg -c | grep g2
echo "status: $(cat $L/status)"
cat /sys/kernel/debug/devices_deferred; echo "(deferred list end)"
```

```text
== /sys/class/devlink (g2 only)
g2bus:pmic0--g2bus:imu0
g2bus:pmic0--g2bus:imu1
auto_remove_on   runtime_pm       subsystem        sync_state_only
consumer         status           supplier         uevent
status: dormant
auto_remove_on: never
runtime_pm: 0
sync_state_only: 0
/sys/devices/g2root/imu0:
power                 supplier:g2bus:pmic0
subsystem             uevent

/sys/devices/g2root/pmic0:
consumer:g2bus:imu0  power                uevent
consumer:g2bus:imu1  subsystem
== devices_deferred
imu0	g2bus: supplier pmic0 not ready
imu1	g2bus: supplier pmic0 not ready
== insmod g2_pmic
[   27.062374] g2bus: match(pmic0, sensorkit-pmic) = 1
[   27.064029] g2_pmic: probe(pmic0) — rails up
[   27.067477] g2bus: match(imu0, sensorkit-pmic) = 0
[   27.067566] g2bus: match(imu1, sensorkit-pmic) = 0
[   27.071042] g2bus: match(imu0, sensorkit-imu) = 1
[   27.076205] g2_imu: probe(imu0) kref=5
[   27.088389] g2_imu: imu0 -> sensorkit0, parent kref=7
[   27.091822] g2bus: match(imu1, sensorkit-imu) = 1
[   27.093538] g2_imu: probe(imu1) kref=5
[   27.099040] g2_imu: imu1 -> sensorkit1, parent kref=7
status: active
(deferred list end)
```

출력에서 볼 것:

- `dmesg | grep g2_imu`가 **빈 줄**이다: `g2_imu.ko`를 올렸는데 `imu_probe()`가 한 번도 안 불렸다. `really_probe()`의 첫 단계 `device_links_check_suppliers()`(예제 11의 첫 줄)가 supplier 미준비로 `-EPROBE_DEFER`를 돌려줬기 때문이다. `devices_deferred`가 이유를 문장으로 알려 준다: `supplier pmic0 not ready`.
- 링크는 그 자체로 **장치**다: `/sys/class/devlink/<supplier버스>:<supplier>--<consumer버스>:<consumer>` (이름 형식은 `device_link_add()`의 `dev_set_name(&link->link_dev, "%s:%s--%s:%s", ...)`에서 확인). 양쪽 장치 디렉터리에 `supplier:...`/`consumer:...` 링크가 생긴다.
- `status`가 `dormant`(supplier에 드라이버 없음) → `active`(둘 다 bind됨)로 바뀌었다. PMIC가 bind되자 `driver_bound()`가 deferred 재시도를 트리거해 IMU 둘이 자동으로 probe됐다 — 우리는 아무것도 안 했다.
- 재시도 probe의 `kref=5`: §5.3 표대로 link(+1)와 deferred 작업(+1)이 참조를 잡고 있다.

deferred probe의 실전 처리(`dev_err_probe()`, `-EPROBE_DEFER`를 드라이버가 직접 반환하는 경우, 타임아웃)는 G3에서 다룬다. 여기서 기억할 것: **device link가 있으면 드라이버 probe가 불리기 전에 driver core가 미리 막는다**. 드라이버 안에서 `devm_regulator_get()`이 `-EPROBE_DEFER`를 받는 고전적인 경로보다 한 단계 앞이다.

### 8.3 예제 14 — fw_devlink: DT phandle에서 자동으로 생기는 링크

무엇을 확인하나: 실제 `virt` DT에서 fw_devlink가 무엇을 링크하는지, 그리고 내가 DT에 `*-supply`를 넣으면 무엇이 생기는지.

먼저 QEMU가 만든 기본 DT로 부팅하고, `drivers/base/core.c`의 dev_dbg를 켜서 fw_devlink의 판단을 본다(커널 인자 `dyndbg="file drivers/base/core.c +p"`):

```sh
echo "fwnode links created: $(dmesg | grep -c 'Linked as a fwnode consumer')"
echo "dropped: $(dmesg | grep -c 'Dropping the fwnode link')"
echo "device links created: $(dmesg | grep -c 'Linked as a consumer to')"
dmesg | grep -E "gpio-keys|pl061|apb-pclk|pl011|pl031|pcie|flash" | grep -i -E "link|consumer|supplier" | head -20   # 출력은 앞 8줄만 남겼다
echo "== /sys/class/devlink entries: $(ls /sys/class/devlink | wc -l)"
```

```text
fwnode links created: 42
dropped: 41
device links created: 0
[    0.759682] /gpio-keys/poweroff Linked as a fwnode consumer to /pl061@9030000
[    0.769460] /pl061@9030000 Linked as a fwnode consumer to /apb-pclk
[    0.769711] /pl061@9030000 Linked as a fwnode consumer to /intc@8000000
[    0.772009] platform gpio-keys: Linked as a sync state only consumer to 9030000.pl061
[    0.772113] amba 9030000.pl061: Not linking /intc@8000000 - might never become dev
[    0.772151] /pl061@9030000 Dropping the fwnode link to /intc@8000000
[    0.772234] amba 9030000.pl061: Not linking /apb-pclk - might never become dev
[    0.772267] /pl061@9030000 Dropping the fwnode link to /apb-pclk
...   (pcie, pl031, pl011도 같은 패턴: apb-pclk·intc에 링크했다가 버림)
== /sys/class/devlink entries: 0
```

출력에서 볼 것 — fw_devlink는 **2단계**다:

1. **fwnode link** (DT 노드 ↔ DT 노드): 부팅 초기에 DT를 훑으며 phandle 참조마다 만든다. 42개.
2. **device link** (장치 ↔ 장치): consumer 장치가 `device_add()`될 때(`fw_devlink_link_device()`) fwnode link를 진짜 device link로 바꾼다. 그런데 supplier가 **장치가 되지 않는 노드**면 바꿀 수 없다. GIC(`intc`)는 `IRQCHIP_DECLARE`로 초기화되고 고정 클럭 `apb-pclk`은 `CLK_OF_DECLARE`로 초기화되어 `struct device`가 없다 → "might never become dev" → 링크를 버린다(41개).
3. 유일하게 살아남은 `gpio-keys → pl061`은 `gpios`가 자식 노드(`poweroff`)에 있어서 **sync state only** 링크가 된다. 이런 링크는 probe 순서를 강제하지 않고, consumer가 bind되면 `device_links_driver_bound()`가 지운다(소스에서 `DL_FLAG_SYNC_STATE_ONLY` → `device_link_drop_managed()` 확인). 그래서 결국 `/sys/class/devlink`가 비어 있다.

`virt` 기본 DT만으로는 남는 링크가 없으므로, sensorkit 노드 세 개를 DT에 넣어 본다(D3 예제 1과 같은 방법: `virt.dts` 복사본 끝에 붙이고 `dtc`, `-dtb`로 부팅).

```dts
	g2_vdd: g2-regulator {
		compatible = "regulator-fixed";
		regulator-name = "g2-vdd-3v3";
		regulator-min-microvolt = <3300000>;
		regulator-max-microvolt = <3300000>;
	};
	g2_pmic: g2-pmic@9300000 {
		compatible = "don,g2-pmic";
		reg = <0x0 0x09300000 0x0 0x1000>;
	};
	g2-imu@9301000 {
		compatible = "don,g2-imu";
		reg = <0x0 0x09301000 0x0 0x1000>;
		vdd-supply = <&g2_vdd>;
		vddio-supply = <&g2_pmic>;
	};
```

```sh
for l in /sys/class/devlink/*; do echo "$(basename $l): status=$(cat $l/status) sync_state_only=$(cat $l/sync_state_only)"; done
ls /sys/devices/platform/9301000.g2-imu
echo "waiting_for_supplier=$(cat /sys/devices/platform/9301000.g2-imu/waiting_for_supplier)"
readlink /sys/devices/platform/9301000.g2-imu/supplier:platform:g2-regulator
readlink /sys/bus/platform/devices/g2-regulator/driver
readlink /sys/bus/platform/devices/9300000.g2-pmic/driver || echo "(9300000.g2-pmic: no driver)"
dmesg | grep -E "g2-imu|g2-pmic|g2-regulator" | grep -i link
```

```text
== /sys/class/devlink
platform:9300000.g2-pmic--platform:9301000.g2-imu: status=dormant sync_state_only=0
platform:g2-regulator--platform:9301000.g2-imu: status=available sync_state_only=0
== consumer side: 9301000.g2-imu
driver_override                    supplier:platform:9300000.g2-pmic
modalias                           supplier:platform:g2-regulator
of_node                            uevent
power                              waiting_for_supplier
subsystem
waiting_for_supplier=0
../../virtual/devlink/platform:g2-regulator--platform:9301000.g2-imu
== supplier drivers
../../../bus/platform/drivers/reg-fixed-voltage
(9300000.g2-pmic: no driver)
[    2.482584] /g2-imu@9301000 Linked as a fwnode consumer to /g2-regulator
[    2.482980] /g2-imu@9301000 Linked as a fwnode consumer to /g2-pmic@9300000
[    2.490575] platform 9301000.g2-imu: Linked as a consumer to 9300000.g2-pmic
[    2.490666] /g2-imu@9301000 Dropping the fwnode link to /g2-pmic@9300000
[    2.494035] platform 9301000.g2-imu: Linked as a consumer to g2-regulator
[    2.494059] /g2-imu@9301000 Dropping the fwnode link to /g2-regulator
[   30.336944] platform 9301000.g2-imu: Relaxing link with 9300000.g2-pmic
```

출력에서 볼 것:

- `vdd-supply`, `vddio-supply` 두 phandle이 그대로 device link 두 개가 됐다. 여기서 "Dropping the fwnode link"는 버린 게 아니라 **device link로 변환을 끝낸 fwnode link를 지운 것**이다.
- 레귤레이터는 `reg-fixed-voltage` 드라이버에 bind되어 링크가 `available`, 드라이버가 없는 PMIC 쪽은 `dormant`다. IMU에도 드라이버가 없어서 probe 시도 자체가 없으므로 deferred 목록에는 안 나온다. 만약 `don,g2-imu` 드라이버가 있었다면 예제 13처럼 PMIC를 기다리며 defer됐을 것이다(이 조합은 실행하지 않았다).
- 30초쯤 "Relaxing link"가 찍혔다. 드라이버가 끝내 안 나타나는 supplier 때문에 영원히 막히지 않도록, deferred probe 타임아웃(`drivers/base/dd.c`의 `driver_deferred_probe_timeout` 기본 10초, 드라이버가 등록될 때마다 `deferred_probe_extend_timeout()`으로 연장)이 지나면 `fw_devlink_drivers_done()`이 그런 링크를 "느슨하게" 만든다. 링크 자체는 남아 있지만(`dormant`) 더 이상 probe를 막지 않는다.
- 링크 장치의 실제 위치는 `/sys/devices/virtual/devlink/` — 부모 없는 클래스 장치이므로 §3.2의 규칙대로 `virtual` 밑이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="200" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="44" text-anchor="middle" font-size="12">DT: vdd-supply =</text>
  <text x="120" y="64" text-anchor="middle" font-size="12">&lt;&amp;g2_vdd&gt;</text>
  <rect x="240" y="20" width="200" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="44" text-anchor="middle" font-size="12">fwnode link</text>
  <text x="340" y="64" text-anchor="middle" font-size="12">(노드 ↔ 노드, 부팅 초기)</text>
  <rect x="460" y="20" width="200" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="44" text-anchor="middle" font-size="12">device link</text>
  <text x="560" y="64" text-anchor="middle" font-size="12">(consumer device_add 때)</text>
  <line x1="220" y1="50" x2="238" y2="50" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="440" y1="50" x2="458" y2="50" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="240" y="110" width="200" height="60" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="134" text-anchor="middle" font-size="12">supplier가 device가</text>
  <text x="340" y="154" text-anchor="middle" font-size="12">안 됨 (GIC, 고정 클럭)</text>
  <line x1="340" y1="80" x2="340" y2="108" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="350" y="98" font-size="12">버림</text>
  <rect x="460" y="110" width="200" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="560" y="132" text-anchor="middle" font-size="12">dormant: 공급자 미bind</text>
  <rect x="460" y="156" width="200" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="560" y="178" text-anchor="middle" font-size="12">available: 공급자 bind</text>
  <rect x="460" y="202" width="200" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="224" text-anchor="middle" font-size="12">active: 소비자도 bind</text>
  <line x1="560" y1="80" x2="560" y2="108" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="560" y1="144" x2="560" y2="154" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="560" y1="190" x2="560" y2="200" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="20" y="200" width="400" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="30" y="222" font-size="12">소비자 probe 시 공급자가 available 전이면</text>
  <text x="30" y="242" font-size="12">really_probe → -EPROBE_DEFER (driver probe 안 불림)</text>
  <line x1="458" y1="127" x2="422" y2="210" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
  <text x="20" y="290" font-size="12">타임아웃(기본 10초 + 연장) 뒤에도 공급자 드라이버가 없으면 "Relaxing link"</text>
  <text x="20" y="310" font-size="12">→ 링크는 남지만 probe를 더 이상 막지 않는다</text>
</svg>
```
그림 5 — fw_devlink의 두 단계와 링크 상태. 예제 13(직접 `device_link_add`)과 예제 14(DT에서 자동)는 오른쪽 열의 같은 상태 기계를 쓴다.

---

## 9. 커널 안에서는 무슨 일이 일어나나

### 9.1 등록 API → 커널 동작 지도

| 호출 | 핵심 내부 동작 (6.18, drivers/base/) | sysfs 결과 |
|---|---|---|
| `bus_register(bus)` | `subsys_private` 할당, `bus_kset` 밑에 kset, `devices`/`drivers` kset, klist 2개, probe 파일 | `/sys/bus/<bus>/{devices,drivers,drivers_probe,drivers_autoprobe,uevent}` |
| `class_register(cls)` | `subsys_private`(class용), `class_kset` 밑에 kset | `/sys/class/<cls>/` |
| `root_device_register(name)` | 버스·클래스 없는 device 등록 + module 링크 | `/sys/devices/<name>/` |
| `device_initialize(dev)` | `kobject_init(&dev->kobj, &device_ktype)`, 목록·락·devres 초기화 | 없음 (refcount = 1) |
| `device_add(dev)` | 이름 결정 → `get_device_parent()`(glue dir/virtual) → `kobject_add()` → `uevent` 속성 → 클래스 링크 → 속성 → `bus_add_device()` → `dpm_sysfs_add()` → `dev` 속성·devtmpfs(devt 있으면) → `KOBJ_ADD` → `fw_devlink_link_device()` → `bus_probe_device()` → 부모·클래스 klist | 디렉터리 + 링크 + `/dev` 노드 |
| `driver_register(drv)` | 중복 이름 검사 → `bus_add_driver()`(kobject, klist, `driver_attach()`, module 링크, `uevent`/`bind`/`unbind`) → groups → `KOBJ_ADD` | `/sys/bus/<bus>/drivers/<name>/` |
| `device_link_add(c, s, f)` | 링크 객체 + `link_dev`(devlink 클래스 장치), 양쪽 `get_device()` | `/sys/class/devlink/...`, `supplier:`/`consumer:` 링크 |
| `device_del(dev)` | unbind(`remove`) → 링크·속성 제거 → `KOBJ_REMOVE` → `kobject_del()` → glue dir 정리 → `put_device(parent)` | 디렉터리 사라짐 |
| `put_device(dev)` | `kobject_put()` → 0이면 `device_release()` → `devres_release_all()` → `release()` | — |

### 9.2 sysfs 쓰기 → 함수

| 동작 | 커널 함수 |
|---|---|
| `echo dev > drivers/X/bind` | `bind_store()` → `bus_find_device_by_name()`(+1 ref) → `driver_match_device()` → `device_driver_attach()` |
| `echo dev > drivers/X/unbind` | `unbind_store()` → `device_driver_detach()` → `bus->remove` |
| `echo 0 > drivers_autoprobe` | `subsys_private.drivers_autoprobe = 0` → `bus_add_driver()`/`bus_probe_device()`가 attach 생략 |
| `echo dev > drivers_probe` | `drivers_probe_store()` → `bus_rescan_devices_helper()` → `device_attach()` |
| `echo add > devices/D/uevent` | `uevent_store()` → `kobject_synth_uevent()` |

### 9.3 드라이버를 쓸 때 다시 만나는 지점

- **G3 platform driver**: `platform_driver_register()`는 우리 `g2_driver_register()`와 똑같은 래퍼다. `devm_*` 자원은 `device_release()`/unbind 시 `devres_release_all()`이 푼다 — §9.1 표의 마지막 줄. deferred probe와 `dev_err_probe()`도 G3.
- **G4 문자 디바이스**: 우리 `/dev/sensorkitN`에 `cdev_add()`와 `file_operations`를 붙이면 진짜 char device가 된다. `ida`로 minor 관리.
- **G8 sysfs 속성**: `struct device`에 `dev_groups`/`DEVICE_ATTR_RW`로 파일 붙이기 — 예제 5의 kobj_attribute와 같은 길(`dev_sysfs_ops`).
- **G9 uevent·udev**: `MODALIAS`와 `MODULE_DEVICE_TABLE`이 만나 자동 로딩, `/dev` 노드 이름·권한.
- **H5/H7 I2C/SPI**: `i2c_bus_type`/`spi_bus_type`이 우리 `g2bus_type`과 같은 구조다. `i2c_driver.probe` ← `i2c_device_probe()` ← `bus->probe`.

---

## 10. Embedded 실무에서 다시 보기

### 10.1 장면 1 — "모듈을 내렸다 올렸더니 Oops"

센서 키트의 IMU 드라이버를 개발 중에 `rmmod`/`insmod`를 반복한다. 세 번째쯤에 Oops가 난다. 원인 후보는 거의 항상 refcount다.

- 드라이버가 `struct device`를 **정적 변수**로 두고 `device_register()`했다 → 모듈이 내려간 뒤에도 sysfs 파일을 연 프로세스가 참조를 들고 있으면 `release()`가 사라진 모듈 코드를 가리킨다.
- `device_unregister()` 뒤에 `kfree(priv)`를 바로 했다(예제 8의 패턴).
- `class_find_device()`, `bus_find_device()`, `of_find_device_by_node()`처럼 **참조를 올려서 돌려주는 find 계열 함수** 결과를 `put_device()` 안 했다 → 누수(모듈이 안 내려가거나 메모리 증가).

진단: 개발 보드 커널에 `CONFIG_DEBUG_KOBJECT_RELEASE`(release를 일부러 늦춰 UAF를 드러냄), `CONFIG_KASAN`, 또는 최소한 `slab_debug=P`를 켠다. 이 랩의 커널에는 앞의 두 개가 꺼져 있어 예제 8에서는 `slab_debug`를 썼다.

### 10.2 장면 2 — IMU가 PMIC보다 먼저 probe돼서 실패

증상: 부팅 로그에 IMU 드라이버의 "chip id read failed (-5)". 재부팅하면 가끔 된다.

- 원인: IMU의 전원 레일을 주는 PMIC 레귤레이터가 아직 안 올라왔는데 IMU probe가 I2C를 읽었다. DT에 `vdd-supply`가 **빠져 있으면** fw_devlink도 의존성을 모른다.
- 고치는 법: DT에 `vdd-supply = <&pmic_ldo3>;`를 넣고(fw_devlink가 링크 생성 → 예제 13·14처럼 probe 자체가 대기), 드라이버는 `devm_regulator_get_enable()`로 레일을 켠다. `devices_deferred`와 `/sys/class/devlink/*/status`로 확인.

### 10.3 장면 3 — 부팅 시간 최적화

fw_devlink 덕에 "공급자 준비 전 probe → defer → 재시도" 왕복이 줄어든다. 그래도 부팅이 느리면 `initcall_debug`와 예제 10의 ftrace(`really_probe` 스택)로 어떤 probe가 오래 걸리는지 본다. 오래 걸리는 드라이버(펌웨어 로드하는 Wi-Fi·카메라)는 `probe_type = PROBE_PREFER_ASYNCHRONOUS`로 `insmod`/부팅 스레드를 막지 않게 할 수 있다.

### 10.4 장면 4 — 커스텀 FPGA 버스

센서 키트에 FPGA가 있고 그 안에 IP 블록이 여러 개(IMU 전처리기, 타임스탬프 유닛, 모터 PWM) 있다. FPGA 레지스터 맵의 ID 테이블을 읽어 블록을 **열거**할 수 있다면, 예제 2의 `g2bus`처럼 **자기 버스 타입을 만드는 것**이 정석이다: FPGA 드라이버가 probe에서 ID 테이블을 읽어 `g2_device_create()`처럼 자식 장치를 만들고, 블록별 드라이버가 ID로 매칭한다. 커널의 `drivers/fpga/dfl.c`(Intel FPGA Device Feature List, `dfl_bus_type`)가 실제 사례다. 블록이 고정이면 그냥 DT에 platform 장치로 적는 쪽이 간단하다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `release` 없이 `struct device` 등록 | 내릴 때 WARN "does not have a release() function" | `device_release()`가 세 곳 다 비어 있음 | `dev->release`/`type->release`/`class->dev_release` 중 하나에서 메모리 해제 |
| 빈 `release()`로 WARN을 숨김 | 메모리 누수, 정적 객체면 rmmod 뒤 Oops | 해제 책임을 회피 | 동적 할당 + `release()`에서 `kfree` |
| `device_unregister()` 직후 `kfree()` | 가끔 랜덤 Oops, slab poison에서 `0x6b6b...` | 다른 참조가 남아 있을 수 있음 | 해제는 `release()`에서만. 계속 써야 하면 미리 `get_device()` |
| `device_add()` 실패 시 `kfree()` | 이중 해제·이름 문자열 누수 | `device_initialize()`로 이미 kref·이름이 생김 | 항상 `put_device()` |
| `bus_find_device()`/`class_find_device()` 결과 put 누락 | 장치가 영원히 안 사라짐, rmmod 불가 | find 계열은 참조를 올려서 반환 | 다 쓰면 `put_device()` |
| 버스 `probe`와 `device_driver.probe` 둘 다 채움 | "needs updating - please use bus_type methods" 경고 | driver core가 bus 메서드를 우선 | 버스 전용 드라이버 구조체에 probe를 두고 `bus->probe`가 위임 |
| `match`에서 0/1 대신 음수 에러를 돌림 | 매칭 실패로 조용히 넘어가거나 defer | 6.18은 음수를 에러/`-EPROBE_DEFER`로 해석 | 일치 1, 불일치 0, 기다릴 때만 `-EPROBE_DEFER` |
| DT에 `*-supply`/`clocks` 누락 | 부팅마다 probe 순서가 달라 간헐 실패 | fw_devlink가 의존성을 모름 | DT에 phandle을 정확히 적고 `/sys/class/devlink` 확인 |
| 클래스 장치를 부모 없이 생성 | `/sys/devices/virtual/<class>/`에 생겨 하드웨어 위치를 잃음 | `device_create()`의 parent에 NULL | 버스 장치를 parent로 넘김 |
| unbind 후 리소스 미해제 | 다시 bind 시 `-EBUSY`, 번호 소진(`sensorkit2`…) | `remove()`가 probe의 역을 안 함 | `devm_*` 사용, 번호는 `ida` |

---

## 12. 면접에서 이렇게 말한다

**Q.** Explain the Linux device model: what are bus, device, driver and class?

**A.** 버스(`bus_type`)는 장치 목록과 드라이버 목록을 갖고 둘을 짝짓는 `match`·`probe` 규칙을 정한다. 장치(`struct device`)는 하드웨어 인스턴스 하나로 부모·버스·현재 드라이버를 가리키고, 드라이버(`device_driver`)는 어떤 장치를 다룰 수 있는지 밝힌다. 클래스는 버스와 무관하게 "rtc", "input" 같은 기능으로 장치를 묶어 유저 공간 인터페이스를 준다. 모두 kobject 위에 만들어져 있어서 객체 하나가 sysfs 디렉터리 하나로 보인다. 진짜 디렉터리는 `/sys/devices`에 있고 `/sys/bus`, `/sys/class`는 링크다.

> A bus type owns two lists, devices and drivers, and defines how they match and probe. A struct device is one hardware instance with a parent, a bus and, once bound, a driver. A device_driver declares what it can handle. A class groups devices by function — rtc, input, net — independent of the bus, and gives user space a stable interface. Everything is built on kobjects, so each object is one sysfs directory: the real directories live under /sys/devices, and /sys/bus and /sys/class are symlink views.

**꼬리 질문**: "platform_device는 어디에 맞나?" → platform은 여러 bus_type 중 하나이고, `platform_device`/`platform_driver`가 `struct device`/`device_driver`를 임베드한 버스 전용 래퍼다.

**Q.** When exactly is a driver's probe() called?

**A.** 장치와 드라이버 중 나중에 등록되는 쪽에서 불린다. 드라이버가 나중이면 `driver_register()` → `bus_add_driver()` → `driver_attach()`가 버스의 모든 장치에 대해, 장치가 나중이면 `device_add()` → `bus_probe_device()` → `__device_attach()`가 모든 드라이버에 대해 `bus->match`를 부른다. 일치하면 `really_probe()`가 공급자 링크를 확인하고 pinctrl·DMA·sysfs를 준비한 뒤 `bus->probe`, 그리고 드라이버의 probe를 부른다. 기본은 insmod나 부팅 스레드 문맥에서 동기 실행이다. 그 밖에 sysfs `bind`, deferred probe 재시도, `drivers_probe`도 같은 길로 들어온다. 저는 직접 만든 버스에서 ftrace 스택으로 두 경로를 확인했다.

> Whenever the second half of a pair shows up. If the driver registers last, driver_register goes through bus_add_driver and driver_attach and tries the bus match against every device. If the device is added last, device_add calls bus_probe_device, which tries every driver. On a match, really_probe checks device-link suppliers, applies pinctrl, sets up DMA and sysfs links, and calls the bus probe, which calls the driver's probe. By default it runs synchronously in the context of insmod or the boot thread. A sysfs bind, a deferred-probe retry or drivers_probe enter the same path. I traced both entry points with ftrace on a toy bus I wrote.

**Q.** Why must every struct device have a release() function, and what goes wrong without one?

**A.** `struct device`의 수명은 등록한 코드가 아니라 kobject 안의 kref가 정한다. 버스 목록, 부모의 자식 목록, sysfs 파일을 연 프로세스, device link 등 여러 곳이 참조를 잡기 때문에, 마지막 `put_device()`가 언제일지 아무도 모른다. 그래서 메모리 해제는 카운트가 0일 때 불리는 `release()`에서만 해야 한다. release가 없으면 `device_release()`가 "does not have a release() function" WARN을 찍고 메모리는 누수된다. `device_unregister()` 직후 `kfree`하면 use-after-free가 된다 — slab poison으로 실제로 `0x6b6b6b6b`를 읽는 걸 확인했다.

> The lifetime of a struct device is controlled by the kref in its kobject, not by the code that registered it. The bus list, the parent's child list, an open sysfs file or a device link can all hold references, so nobody knows which put_device will be the last one. Memory must therefore be freed only in release, which runs when the count hits zero. Without release, device_release prints the "does not have a release() function" warning and the memory leaks. Freeing right after device_unregister is a use-after-free — I reproduced it with slab poisoning and read back 0x6b6b6b6b.

**꼬리 질문**: "device_register와 device_initialize + device_add 차이?" → 같은 일을 두 단계로 나눈 것. 사이에 필드를 채우거나 에러 처리를 할 수 있고, `device_initialize()` 뒤에는 실패해도 반드시 `put_device()`로 정리한다.

**Q.** What is a kobject, kset and ktype?

**A.** kobject는 이름·부모·kref·sysfs 디렉터리를 가진 기본 객체로, 큰 구조체에 임베드해 `container_of`로 꺼낸다. ktype은 같은 종류 kobject들의 `release`와 sysfs 읽기·쓰기 방법, 기본 속성을 정한 vtable이다. kset은 kobject들의 묶음이고 자기도 kobject라 디렉터리가 되며 uevent를 필터링한다. `/sys/bus/<bus>/devices`가 kset의 예다. 드라이버 작성자는 보통 `struct device`를 통해 간접적으로만 쓴다.

> A kobject is the base object: name, parent, a kref and a sysfs directory. It is embedded in a larger structure and recovered with container_of. The ktype is like a vtable for a family of kobjects — the release function, the sysfs show/store dispatch, and default attributes. A kset is a collection of kobjects; it is a kobject itself, so it is a directory, and it filters uevents. `/sys/bus/<bus>/devices` is a kset. Driver writers mostly use them indirectly through struct device.

**Q.** What are device links and fw_devlink?

**A.** device link는 consumer 장치와 supplier 장치의 의존성을 driver core에 등록한 것이다. managed link면 supplier가 bind되기 전에는 consumer의 `really_probe()`가 드라이버 probe 전에 `-EPROBE_DEFER`로 막고, supplier가 bind되면 자동 재시도한다. 언바인드 순서와 runtime PM에도 쓰인다. fw_devlink는 DT의 `*-supply`, `clocks`, `gpios`, `interrupts` 같은 phandle을 파싱해 이 링크를 자동으로 만든다. supplier가 GIC처럼 struct device가 안 되는 노드면 링크를 버리고, 드라이버가 끝내 안 오는 supplier는 타임아웃 뒤 relax한다. `/sys/class/devlink`와 `devices_deferred`로 상태를 본다.

> A device link registers a dependency between a consumer and a supplier device. With a managed link, really_probe defers the consumer before its driver's probe is even called until the supplier is bound, and retries automatically afterwards; it also orders unbind and runtime PM. fw_devlink creates these links automatically by parsing DT phandles such as supply, clocks, gpios and interrupts. If the supplier node never becomes a device — the GIC, for example — the link is dropped, and links to suppliers whose driver never shows up are relaxed after the deferred-probe timeout. I check them under /sys/class/devlink and in devices_deferred.

**Q.** How would you add support for a new kind of bus, say an FPGA that exposes enumerable IP blocks?

**A.** `bus_type`을 하나 정의한다: ID 비교 `match`, `MODALIAS`를 넣는 `uevent`, 버스 전용 드라이버 구조체로 위임하는 `probe`/`remove`. 버스 장치·드라이버 구조체에 `struct device`/`device_driver`를 임베드하고, 장치 생성 헬퍼에서 반드시 `release()`를 채운다. FPGA 컨트롤러 드라이버가 probe에서 ID 테이블을 읽어 자식 장치를 그 버스에 등록하고, 블록별 드라이버는 ID 테이블로 매칭한다. 커널의 DFL 버스가 실제 예다. 고정 구성이면 DT platform 장치가 더 간단하다.

> Define a bus_type: a match that compares IDs, a uevent that emits a MODALIAS, and probe and remove that dispatch to a bus-specific driver struct. Embed struct device and struct device_driver in bus-specific wrappers, and always set a release function in the device-creation helper. The FPGA controller driver reads the ID table in its probe and registers child devices on that bus; per-block drivers match on the ID. The kernel's DFL bus for FPGAs is a real example. If the layout is fixed, plain DT platform devices are simpler.

---

## 13. 직접 해보기

1. **개념**: 예제 4에서 `g2_pmic.ko`를 `g2_board.ko`보다 먼저 올리면 dmesg의 match 줄 순서와 개수가 어떻게 바뀌나? — 정답/힌트: 드라이버 등록 때는 장치가 없어 match 0줄, 이후 장치가 하나 추가될 때마다 그 장치 × 등록된 드라이버 수만큼 match가 찍힌다(예제 10의 경로 B).
2. **코드**: `g2_imu.c`의 `next_minor`를 `DEFINE_IDA` + `ida_alloc_max()`/`ida_free()`로 바꿔 unbind/bind를 반복해도 `sensorkit0`/`sensorkit1`이 재사용되게 하라. — 정답/힌트: minor를 drvdata에 같이 저장하거나 `MINOR(cdev->devt)`로 꺼내 `remove()`에서 `ida_free()`.
3. **코드**: 버스에 `dev_groups`로 `kind` 속성(읽기 전용)을 추가해 `cat /sys/bus/g2bus/devices/imu0/kind`가 `imu`를 출력하게 하라. — 정답/힌트: `static ssize_t kind_show(struct device *dev, struct device_attribute *attr, char *buf)` + `DEVICE_ATTR_RO(kind)` + `ATTRIBUTE_GROUPS(g2_dev)` + `.dev_groups = g2_dev_groups` (G8).
4. **관찰**: 예제 13에서 `g2_pmic.ko`를 올려 IMU가 bind된 상태에서 `echo pmic0 > /sys/bus/g2bus/drivers/sensorkit-pmic/unbind` 하면 IMU는 어떻게 되나? — 정답/힌트: managed link의 supplier가 unbind되면 consumer가 먼저 unbind된다(`device_links_unbind_consumers()`). 직접 돌려 보면 dmesg에 `g2_imu: remove(imu0)`, `remove(imu1)`가 찍히고 링크 `status`는 `dormant`로 돌아간다. IMU는 deferred 목록에도 들어가지 않는다(다시 probe를 시도할 계기가 없으므로) — PMIC를 다시 bind하면 어떻게 되는지까지 확인해 보라.
5. **ftrace**: 예제 10을 `really_probe` 대신 `device_release`에 걸고 `rmmod g2_board`를 하라. 스택에서 `device_unregister` → `kobject_put`이 보이는가? — 정답/힌트: 예제 7의 WARN call trace와 같은 모양이 정상 경로에서도 나온다.
6. **개념**: 예제 6에서 `g2root`의 add uevent가 없던 이유를 소스 함수 이름과 함께 설명하라. 루트 장치에 클래스를 주면 어떻게 될까? — 정답/힌트: `dev_uevent_filter()`가 bus도 class도 없는 장치를 거른다. 클래스를 주면 이벤트가 나가지만 `/sys/class/...` 링크도 생긴다.
7. **DT**: 예제 14의 DT에서 `vddio-supply`를 지우고 다시 부팅해 `/sys/class/devlink`에 몇 개가 남는지 예상하고 확인하라. — 정답/힌트: 레귤레이터 링크 1개(`available`).

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| device model | 디바이스 모델 | 버스·장치·드라이버·클래스를 kobject로 통일한 커널 객체 체계 |
| `bus_type` | 버스 타입 | 장치·드라이버 목록과 match/probe 규칙을 가진 객체 (platform, i2c, g2bus) |
| `struct device` | 장치 | 하드웨어 인스턴스 하나. 버스 전용 구조체 안에 임베드 |
| `device_driver` | 드라이버 | 장치를 다룰 수 있다고 밝힌 객체. 버스 전용 드라이버 안에 임베드 |
| class | 클래스 | 기능별 장치 묶음, `/sys/class/<이름>`, 보통 `/dev` 노드를 만든다 |
| class device | 클래스 장치 | 클래스에 속한 `struct device`. 보통 버스 장치를 부모로 갖는다 |
| glue dir | 접착 디렉터리 | 부모 장치 밑에 클래스 이름으로 생기는 중간 디렉터리 (`imu0/g2class/`) |
| kobject | 커널 객체 | 이름·부모·kref·sysfs 디렉터리를 가진 기본 객체 |
| ktype | kobject 타입 | `release`·sysfs 연산·기본 속성을 정한 vtable |
| kset | kobject 집합 | kobject들의 묶음이자 디렉터리, uevent 필터 |
| kref | 참조 카운트 | `refcount_t` 래퍼. 0이면 release, 언더플로·오버플로 시 WARN 후 포화 |
| `release()` | 해제 콜백 | refcount가 0일 때 불리는, 메모리를 해제하는 유일한 장소 |
| `get_device`/`put_device` | 참조 획득/반납 | kobject kref를 1 올리고 내림 |
| bind / unbind | 결합 / 분리 | 장치에 드라이버를 붙여 probe / 떼어 remove |
| `drivers_autoprobe` | 자동 probe 스위치 | 버스 단위로 등록 시 자동 매칭을 켜고 끔 |
| uevent | 유저 이벤트 | kobject 추가·제거·bind 등을 netlink로 유저 공간에 알림 |
| device link | 장치 링크 | consumer → supplier 의존성. probe·unbind·runtime PM 순서 보장 |
| fw_devlink | 펌웨어 장치 링크 | DT phandle에서 device link를 자동 생성하는 기능 |
| fwnode link | 펌웨어 노드 링크 | 장치가 생기기 전 DT 노드 사이의 임시 의존성 기록 |
| deferred probe | 지연 probe | `-EPROBE_DEFER` 시 목록에 넣고 다른 드라이버가 bind될 때 재시도 |
| `slab_debug=P` | slab poison | 해제된 slab 객체를 `0x6b`로 채워 UAF를 드러내는 커널 인자 |

---

## 15. 요약 & 체크리스트

Linux 디바이스 모델은 펌웨어 HAL의 객체 레지스트리 + 바인딩 루프 + 참조 카운트 + 의존성 순서를 커널 전체에 하나로 통일한 것이다. `bus_type`이 장치 목록과 드라이버 목록을 갖고, 둘 중 나중에 등록되는 쪽(`driver_register()` → `driver_attach()` 또는 `device_add()` → `bus_probe_device()`)이 `bus->match`를 부르며, 일치하면 `really_probe()`가 공급자 링크·pinctrl·sysfs를 준비하고 `bus->probe` → 드라이버 probe를 부른다. 모든 객체는 kobject를 품고 있어서 객체 하나 = sysfs 디렉터리 하나이고, 진짜 디렉터리는 `/sys/devices`의 부모-자식 트리에, `/sys/bus`와 `/sys/class`는 링크 색인으로 있다. 클래스 장치는 버스 장치 밑 glue dir에 생기고 `devt`가 있으면 `/dev` 노드가 된다. 수명은 kref가 정하므로 `struct device` 메모리는 `release()`에서만 해제하고, `device_initialize()` 뒤에는 실패해도 `put_device()`로 정리한다. 이를 어기면 WARN, use-after-free, refcount underflow가 난다. 장치 간 의존성은 device link로 표현되고, fw_devlink가 DT phandle에서 이를 자동으로 만들어 supplier가 준비되기 전에는 consumer probe를 막는다.

- [ ] `bus_type`·`device`·`device_driver`·`class`의 핵심 필드와 서로 가리키는 포인터를 그릴 수 있다
- [ ] 버스 전용 구조체에 `struct device`를 임베드하고 `container_of`로 꺼내는 패턴을 설명할 수 있다
- [ ] `match`/`uevent`/`probe`/`remove`를 가진 최소 버스를 경고 없이 짤 수 있다
- [ ] `/sys/devices`, `/sys/bus`, `/sys/class`의 관계와 `driver`/`subsystem`/`device` 링크의 의미를 말할 수 있다
- [ ] kobject·kset·ktype의 역할과 "kobject 하나 = 디렉터리 하나" 규칙을 설명할 수 있다
- [ ] `device_register()`와 `device_initialize()` + `device_add()`의 차이, 실패 시 `put_device()` 규칙을 말할 수 있다
- [ ] `release()`가 없을 때·너무 일찍 put할 때·두 번 put할 때 각각의 증상을 설명할 수 있다
- [ ] `driver_register()`와 `device_add()` 두 입구가 `really_probe()`로 합류하는 경로를 함수 이름으로 말할 수 있다
- [ ] ftrace(`func_stack_trace`, `function_graph`)로 probe 경로와 시간을 찍을 수 있다
- [ ] sysfs `bind`/`unbind`/`drivers_autoprobe`/`drivers_probe`로 바인딩을 손으로 조작할 수 있다
- [ ] uevent에 무엇이 실리는지(`ACTION`, `DEVPATH`, `SUBSYSTEM`, `MODALIAS`, `MAJOR`/`MINOR`)와 누가 쓰는지 말할 수 있다
- [ ] device link와 fw_devlink의 동작, `/sys/class/devlink`의 `status` 값, "might never become dev"의 의미를 설명할 수 있다

---

## 참고 자료

- docs.kernel.org — Driver Model 개요 / Bus Types / Device Drivers / Driver Binding: https://docs.kernel.org/driver-api/driver-model/index.html
- docs.kernel.org — Everything you never wanted to know about kobjects, ksets, and ktypes: https://docs.kernel.org/core-api/kobject.html
- docs.kernel.org — Adding reference counters (krefs) to kernel objects: https://docs.kernel.org/core-api/kref.html
- docs.kernel.org — Device links: https://docs.kernel.org/driver-api/device_link.html
- docs.kernel.org — Kernel parameters (`fw_devlink=`, `fw_devlink.strict=`, `deferred_probe_timeout=`, `slab_debug=`): https://docs.kernel.org/admin-guide/kernel-parameters.html
- docs.kernel.org — ftrace: https://docs.kernel.org/trace/ftrace.html
- 커널 소스 (이 노트에서 인용한 위치, v6.18): `include/linux/device.h`, `include/linux/device/{bus,driver,class}.h`, `include/linux/kobject.h`, `include/linux/kref.h`, `drivers/base/core.c`(`device_initialize`, `device_add`, `device_del`, `device_release`, `get_device_parent`, `dev_uevent_filter`, `device_link_add`, fw_devlink), `drivers/base/bus.c`(`bus_register`, `bus_add_driver`, `bus_probe_device`, `bind_store`), `drivers/base/dd.c`(`driver_attach`, `__driver_attach`, `__device_attach`, `really_probe`, deferred probe), `drivers/base/driver.c`(`driver_register`), `drivers/base/class.c`, `drivers/base/base.h`(`subsys_private`, `driver_private`, `device_private`), `lib/kobject.c`, `lib/refcount.c`, `drivers/fpga/dfl.c` — https://elixir.bootlin.com/linux/v6.18/source
- Bootlin, "Linux kernel and driver development training" 슬라이드 (Device model 장): https://bootlin.com/training/kernel/
- Corbet, Rubini, Kroah-Hartman, "Linux Device Drivers, 3rd ed." 14장 The Linux Device Model — 개념은 유효하지만 API는 2.6 시절이라 함수 이름·시그니처가 많이 바뀌었다(예: `class_device`는 없어졌다)
