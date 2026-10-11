# G9. 핫플러그와 udev — 장치가 생기면 누가 /dev 노드를 만들고 드라이버를 올리나

> **이 노트를 다 읽으면**: `device_add()` → `kobject_uevent()` → netlink/uevent_helper → udev·mdev → `/dev` 권한·심볼릭 링크·`modprobe`로 이어지는 핫플러그 사슬을 함수 이름 그대로 그릴 수 있다 · 드라이버에서 `add_uevent_var()`로 uevent 변수를 넣고(bus·class·device_type 세 자리), `MODULE_DEVICE_TABLE`이 어떻게 자동 로딩으로 이어지는지 설명할 수 있다 · udev rule(`==`/`=`/`+=`, `ATTRS{}` 부모 탐색, `SYMLINK+=`, `MODE=`, `RUN+=`)을 쓰고 `udevadm test`로 검증해 적용·제거할 수 있다 · BusyBox mdev(`/etc/mdev.conf`, 데몬·helper 모드)로 같은 일을 하는 임베디드 구성을 만들 수 있다
> **왜 필요한가**: "같은 IMU 두 개가 부팅마다 `/dev` 번호가 바뀐다", "USB GNSS를 꽂았는데 드라이버가 안 올라온다", "앱이 `/dev/xxx`를 열 권한이 없다"는 실무 첫 달 단골이고, 면접에서는 "How does a kernel module get loaded automatically when a device appears?"로 나온다
> **Don 기준 난이도**: "이벤트가 생기면 상위 계층에 메시지를 보낸다"는 펌웨어의 인터럽트 → 메일박스 → 상위 태스크 구조와 같다 / 새로 배울 부분은 그 메시지가 **문자열 KEY=VALUE 목록**이고, 정책(이름·권한·모듈 로드)을 **커널이 아니라 유저 공간 데몬**이 규칙 파일로 정한다는 점
> **선행 노트**: A9(netlink uevent 리스너), D3(modalias·modprobe coldplug), E3(커널 모듈·depmod), G2(디바이스 모델), G4(문자 디바이스) — G8(sysfs)·G10(펌웨어 로딩)과 이웃이다

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어에서 PCIe 링크가 올라오거나 NAND 다이가 응답하지 않는 일이 생기면, 인터럽트 핸들러는 일을 다 하지 않는다. **이벤트를 메일박스(큐)에 적어** 상위 태스크에 넘기고, 정책 판단(재시도할지, 호스트에 알릴지)은 상위 태스크가 한다. "빠르고 단순한 쪽은 사실만 알리고, 느리고 복잡한 쪽이 정책을 정한다"는 분리다.

Linux 핫플러그가 정확히 이 구조다.

- **커널**(사실을 아는 쪽): USB 장치가 꽂히거나, I2C 센서가 probe되거나, 모듈이 로드되거나, 우리가 sysfs에 뭔가 쓰면 driver core가 `struct device`를 만들고 `device_add()`를 부른다. 그 안에서 `kobject_uevent(KOBJ_ADD)`가 **"add@/devices/... + KEY=VALUE 목록"** 메시지를 만든다. 이것이 **uevent**다.
- **배달**: 메시지는 netlink 멀티캐스트(`NETLINK_KOBJECT_UEVENT`, A9에서 직접 들었다)로 방송되고, 설정돼 있으면 `/proc/sys/kernel/hotplug`에 적힌 프로그램(uevent_helper)도 **이벤트마다 실행**된다.
- **유저 공간**(정책을 정하는 쪽): 데스크톱·Yocto의 systemd-udevd, 작은 임베디드의 BusyBox mdev가 메시지를 받아 규칙 파일대로 **권한·소유자·안정된 이름(심볼릭 링크)·모듈 자동 로드(`modprobe $MODALIAS`)·프로그램 실행**을 한다.
- **`/dev` 노드 자체**는 요즘 커널이 직접 만든다(**devtmpfs**). 그래서 udev가 없어도 노드는 생긴다. udev·mdev는 그 위에 정책을 얹는다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 400">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="10" y="30" width="200" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="110.0" y="54.0" font-size="13" text-anchor="middle">device_add()</text><text x="110.0" y="71.0" font-size="13" text-anchor="middle">platform_device_add 등</text><rect x="250" y="30" width="220" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="360.0" y="54.0" font-size="13" text-anchor="middle">kobject_uevent_env()</text><text x="360.0" y="71.0" font-size="13" text-anchor="middle">ACTION·DEVPATH·SEQNUM</text><line x1="210" y1="58" x2="248" y2="58" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/><rect x="10" y="112" width="200" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="110.0" y="133.1" font-size="13" text-anchor="middle">devtmpfs_create_node()</text>
<text x="110.0" y="150.1" font-size="13" text-anchor="middle">/dev 노드 즉시 생성</text><line x1="110" y1="86" x2="110" y2="110" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/><rect x="250" y="112" width="220" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="360.0" y="133.1" font-size="13" text-anchor="middle">netlink 멀티캐스트</text><text x="360.0" y="150.1" font-size="13" text-anchor="middle">KOBJECT_UEVENT group 1</text><rect x="500" y="112" width="170" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="585.0" y="133.1" font-size="13" text-anchor="middle">uevent_helper</text><text x="585.0" y="150.1" font-size="13" text-anchor="middle">(kernel.hotplug)</text><line x1="360" y1="86" x2="360" y2="110" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/><line x1="470" y1="70" x2="560" y2="110" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<line x1="10" y1="182" x2="670" y2="182" stroke="currentColor" stroke-width="1" stroke-dasharray="4 4"/><text x="14.0" y="176.0" font-size="12" text-anchor="start">kernel</text><text x="14.0" y="198.0" font-size="12" text-anchor="start">user</text><rect x="70" y="210" width="240" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="190.0" y="238.1" font-size="13" text-anchor="middle">systemd-udevd / eudev</text><text x="190.0" y="255.1" font-size="13" text-anchor="middle">rules 평가 → group 2 재방송</text><rect x="330" y="210" width="150" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="405.0" y="238.1" font-size="13" text-anchor="middle">mdev -d</text><text x="405.0" y="255.1" font-size="13" text-anchor="middle">(netlink 데몬)</text><rect x="500" y="210" width="170" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="585.0" y="238.1" font-size="13" text-anchor="middle">mdev (helper)</text><text x="585.0" y="255.1" font-size="13" text-anchor="middle">이벤트마다 fork·exec</text><line x1="330" y1="162" x2="220" y2="208" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/><line x1="390" y1="162" x2="400" y2="208" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/><line x1="585" y1="162" x2="585" y2="208" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/><line x1="40" y1="296" x2="640" y2="296" stroke="currentColor" stroke-width="1"/><line x1="190" y1="274" x2="190" y2="296" stroke="currentColor" stroke-width="1"/><line x1="400" y1="274" x2="400" y2="296" stroke="currentColor" stroke-width="1"/><line x1="585" y1="274" x2="585" y2="296" stroke="currentColor" stroke-width="1"/><line x1="89" y1="296" x2="89" y2="318" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<rect x="10" y="320" width="159" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="89.5" y="344.1" font-size="13" text-anchor="middle">권한·소유자</text><text x="89.5" y="361.1" font-size="13" text-anchor="middle">MODE · GROUP</text><line x1="256" y1="296" x2="256" y2="318" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/><rect x="177" y="320" width="159" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="256.5" y="344.1" font-size="13" text-anchor="middle">안정 이름</text><text x="256.5" y="361.1" font-size="13" text-anchor="middle">SYMLINK · by-id</text><line x1="423" y1="296" x2="423" y2="318" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/><rect x="344" y="320" width="159" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="423.5" y="344.1" font-size="13" text-anchor="middle">모듈 자동 로드</text>
<text x="423.5" y="361.1" font-size="13" text-anchor="middle">modprobe $MODALIAS</text><line x1="590" y1="296" x2="590" y2="318" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/><rect x="511" y="320" width="159" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="590.5" y="344.1" font-size="13" text-anchor="middle">프로그램 실행</text><text x="590.5" y="361.1" font-size="13" text-anchor="middle">RUN+= · systemd</text><text x="250.0" y="394.0" font-size="12" text-anchor="start">devtmpfs가 노드를 만들고, 데몬은 그 위에 정책을 얹는다</text>
</svg>
```
그림 1 — 핫플러그 사슬. 커널은 사실(uevent)만 알리고, 노드 생성은 devtmpfs가, 권한·이름·모듈 로드·프로그램 실행 같은 정책은 유저 공간 데몬이 한다.

이 노트에서 쓰는 두 환경:

| 환경 | 커널 | 핫플러그 처리 | 이 노트에서 하는 일 |
|---|---|---|---|
| QEMU 게스트 (공용 랩) | 6.18.55 (BusyBox 1.37 initramfs) | devtmpfs + BusyBox mdev | 직접 만든 모듈 `g9_hot`/`g9_imu`로 장치를 만들고 지우며 uevent·mdev·자동 로드 확인 |
| VM 호스트 (Lima Ubuntu) | `7.0.0-38-generic` (`uname -r`) | systemd-udevd (udevadm 259) | 커널에 이미 있는 모듈(vcan, loop)만 써서 `udevadm monitor/info/test`, 실제 rule 적용·제거 |

> 규칙: 직접 만든 모듈은 **QEMU 게스트에만** 넣는다. 호스트에서는 기존 장치와 기존 모듈만 쓴다.

---

## 1. uevent — 커널이 보내는 "장치 생김/사라짐" 메시지

### 1.1 직관

uevent는 **문자열 메시지 한 통**이다. 첫 줄은 `ACTION@DEVPATH`(예: `add@/devices/platform/g9-imu.0`), 그 뒤는 `\0`으로 구분된 `KEY=VALUE` 목록이다. 펌웨어로 치면 "이벤트 코드 + TLV 페이로드"를 사람이 읽을 수 있는 텍스트로 만든 것이다. 정수 구조체가 아니라 문자열이라서, 커널이 새 키를 추가해도 옛 udev가 깨지지 않는다.

ACTION 종류(`enum kobject_action`, `lib/kobject_uevent.c`의 `kobject_actions[]`):

| ACTION | 언제 | 예 |
|---|---|---|
| `add` | `device_add()`, 모듈 로드(`/module/xxx`) | 센서 probe 후 class 장치 등록 |
| `remove` | `device_del()` | USB 뽑음, `rmmod` |
| `change` | 드라이버가 상태 변화를 알릴 때, sysfs `uevent`에 `change`를 쓸 때 | 디스크 미디어 교체, 배터리 상태 |
| `move` | `device_rename()`·`device_move()` | 네트워크 인터페이스 이름 변경 |
| `online` / `offline` | CPU·메모리 hotplug | `echo 0 > .../cpu1/online` |
| `bind` / `unbind` | 드라이버가 장치에 붙음/떨어짐 (`driver_bound()`, `__device_release_driver()`) | probe 성공 직후 |

`bind`/`unbind`는 4.14에서 추가됐다. 그 전에는 "드라이버가 붙었다"를 유저 공간이 직접 알 방법이 없어서 `add`만 보고 판단했다.

### 1.2 이 노트의 실험 장치 — `g9_hot` + `g9_imu`

진짜 USB 장치를 QEMU에 꽂지 않고도 핫플러그를 마음대로 일으키려고 모듈 두 개를 만들었다.

- `g9_hot.ko`: `/sys/kernel/g9_hot/add`에 `"front SN1001"`을 쓰면 `"g9-imu"`라는 이름의 platform device를 하나 만든다(= 센서가 꽂힘). `remove`에 번호를 쓰면 지운다(= 센서가 빠짐). 장치마다 `device_type.uevent`로 `G9_POSITION`, `G9_SERIAL` 변수를 uevent에 덧붙인다.
- `g9_imu.ko`: `"g9-imu"` 장치를 다루는 **드라이버**. `MODULE_DEVICE_TABLE(platform, ...)`으로 자동 로딩 별칭을 내보내고, probe에서 class `g9`에 char device `g9_imuN`을 만든다(G4). class의 `dev_uevent`로 노드 쪽 이벤트에도 위치를 실어 준다.

#### 예제 1 — `g9_hot.c` (장치를 만들고 지우는 쪽)

무엇을 확인하는 코드인가: platform device를 런타임에 만들고 지우는 법, 그리고 `device_type.uevent`에서 `add_uevent_var()`로 변수를 넣는 법. (길어서 두 조각으로 나눴다. 두 조각을 이어 붙이면 파일 하나다.)

```c
// g9_hot.c (1/2): /sys/kernel/g9_hot/{add,remove}에 쓰면 "g9-imu" platform device를 만들고/지운다
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>

struct g9_info { char pos[16]; char serial[24]; };	/* platform_data */
#define G9_MAX 4
static struct platform_device *devs[G9_MAX];
static DEFINE_MUTEX(g9_lock);
static struct kobject *ctl;

/* device_type.uevent: 이 장치의 모든 uevent에 변수 두 개를 덧붙인다 */
static int g9_type_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct g9_info *in = dev_get_platdata(dev);

	if (add_uevent_var(env, "G9_POSITION=%s", in->pos))
		return -ENOMEM;
	return add_uevent_var(env, "G9_SERIAL=%s", in->serial);
}
static const struct device_type g9_type = { .name = "g9_sensor", .uevent = g9_type_uevent };

static ssize_t add_store(struct kobject *k, struct kobj_attribute *a, const char *buf, size_t n)
{
	struct g9_info in = {};
	struct platform_device *pd;
	int id, ret;

	if (sscanf(buf, "%15s %23s", in.pos, in.serial) != 2)
		return -EINVAL;
	guard(mutex)(&g9_lock);
	for (id = 0; id < G9_MAX && devs[id]; id++)
		;
	if (id == G9_MAX)
		return -ENOSPC;
	pd = platform_device_alloc("g9-imu", id);
	if (!pd)
		return -ENOMEM;
	pd->dev.type = &g9_type;
	ret = platform_device_add_data(pd, &in, sizeof(in));
	if (!ret)
		ret = platform_device_add(pd);	/* → device_add() → KOBJ_ADD uevent */
	if (ret) {
		platform_device_put(pd);
		return ret;
	}
	devs[id] = pd;
	return n;
}
```

```c
// g9_hot.c (2/2)
static ssize_t remove_store(struct kobject *k, struct kobj_attribute *a, const char *buf, size_t n)
{
	unsigned int id;

	if (kstrtouint(buf, 0, &id) || id >= G9_MAX)
		return -EINVAL;
	guard(mutex)(&g9_lock);
	if (!devs[id])
		return -ENOENT;
	platform_device_unregister(devs[id]);	/* → device_del() → KOBJ_REMOVE */
	devs[id] = NULL;
	return n;
}
static struct kobj_attribute add_attr = __ATTR_WO(add);
static struct kobj_attribute remove_attr = __ATTR_WO(remove);
static struct attribute *g9_attrs[] = { &add_attr.attr, &remove_attr.attr, NULL };
static const struct attribute_group g9_grp = { .attrs = g9_attrs };

static int __init g9_hot_init(void)
{
	ctl = kobject_create_and_add("g9_hot", kernel_kobj);
	if (!ctl)
		return -ENOMEM;
	if (sysfs_create_group(ctl, &g9_grp)) {
		kobject_put(ctl);
		return -ENOMEM;
	}
	return 0;
}

static void __exit g9_hot_exit(void)
{
	int i;

	kobject_put(ctl);
	for (i = 0; i < G9_MAX; i++)
		if (devs[i])
			platform_device_unregister(devs[i]);
}
module_init(g9_hot_init);
module_exit(g9_hot_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G9: create/remove g9-imu platform devices on demand");
```

코드에서 볼 것:

- `add_uevent_var(env, fmt, ...)`는 `env->buf`(2048바이트, `UEVENT_BUFFER_SIZE`)에 `KEY=VALUE\0`을 붙이고 `env->envp[]`(최대 64개, `UEVENT_NUM_ENVP`)에 포인터를 단다. 넘치면 `-ENOMEM`을 돌려준다. 그래서 반환값을 그대로 위로 올린다.
- 콜백 시그니처의 `const struct device *`는 6.3 무렵 driver core 전체가 const화되면서 바뀐 것이다. 옛 드라이버 예제(`struct device *dev`)를 그대로 붙이면 6.x에서 경고·에러가 난다.
- `platform_device_add()` 안의 `device_add()`가 uevent를 보낸다. 드라이버 코드에서 `kobject_uevent()`를 직접 부를 일은 거의 없다(`change` 알림 정도).

#### 예제 2 — `g9_imu.c` (드라이버 쪽: 별칭 + class uevent + /dev 노드)

무엇을 확인하는 코드인가: `MODULE_DEVICE_TABLE`로 별칭을 내보내는 법, class의 `dev_uevent`·`devnode` 콜백, probe에서 `device_create()`로 `/dev` 노드를 만드는 법.

```c
// g9_imu.c (1/2): "g9-imu" platform device 드라이버 — MODULE_DEVICE_TABLE + class uevent + /dev/g9_imuN
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/cdev.h>
#include <linux/fs.h>

struct g9_info { char pos[16]; char serial[24]; };
#define G9_MAX 4
static dev_t base;
static struct cdev g9_cdev;
static const struct g9_info *infos[G9_MAX];

static ssize_t g9_read(struct file *f, char __user *u, size_t n, loff_t *off)
{
	const struct g9_info *in = infos[iminor(file_inode(f))];
	char line[48];
	int len;

	if (!in)
		return -ENODEV;
	len = scnprintf(line, sizeof(line), "%s %s\n", in->pos, in->serial);
	return simple_read_from_buffer(u, n, off, line, len);
}
static const struct file_operations g9_fops = { .owner = THIS_MODULE, .read = g9_read };

/* class.dev_uevent: 노드 쪽 uevent에도 부모(platform device)의 위치를 실어 준다 */
static int g9_class_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct g9_info *in = dev_get_platdata(dev->parent);

	return add_uevent_var(env, "G9_POSITION=%s", in ? in->pos : "unknown");
}
static char *g9_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0640;		/* devtmpfs 기본 권한 + uevent의 DEVMODE */
	return NULL;
}
static const struct class g9_class = {
	.name = "g9", .dev_uevent = g9_class_uevent, .devnode = g9_devnode,
};

static int g9_probe(struct platform_device *pd)
{
	struct device *d;

	if (pd->id < 0 || pd->id >= G9_MAX || !dev_get_platdata(&pd->dev))
		return -EINVAL;
	infos[pd->id] = dev_get_platdata(&pd->dev);
	d = device_create(&g9_class, &pd->dev, base + pd->id, NULL, "g9_imu%d", pd->id);
	if (IS_ERR(d))
		return PTR_ERR(d);
	dev_info(&pd->dev, "bound -> /dev/g9_imu%d\n", pd->id);
	return 0;
}
```

```c
// g9_imu.c (2/2)
static void g9_remove(struct platform_device *pd)
{
	device_destroy(&g9_class, base + pd->id);
	infos[pd->id] = NULL;
}

static const struct platform_device_id g9_ids[] = { { "g9-imu" }, { } };
MODULE_DEVICE_TABLE(platform, g9_ids);		/* → alias: platform:g9-imu */

static struct platform_driver g9_drv = {
	.probe = g9_probe, .remove = g9_remove, .id_table = g9_ids,
	.driver = { .name = "g9_imu" },
};

static int __init g9_imu_init(void)
{
	int ret = alloc_chrdev_region(&base, 0, G9_MAX, "g9_imu");

	if (ret)
		return ret;
	cdev_init(&g9_cdev, &g9_fops);
	ret = cdev_add(&g9_cdev, base, G9_MAX);
	if (ret)
		goto err_region;
	ret = class_register(&g9_class);
	if (ret)
		goto err_cdev;
	ret = platform_driver_register(&g9_drv);
	if (ret)
		goto err_class;
	return 0;
err_class:
	class_unregister(&g9_class);
err_cdev:
	cdev_del(&g9_cdev);
err_region:
	unregister_chrdev_region(base, G9_MAX);
	return ret;
}
static void __exit g9_imu_exit(void)
{
	platform_driver_unregister(&g9_drv);
	class_unregister(&g9_class);
	cdev_del(&g9_cdev);
	unregister_chrdev_region(base, G9_MAX);
}
module_init(g9_imu_init);
module_exit(g9_imu_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G9: g9-imu driver with module alias + class uevent");
```

빌드(VM, 랩 커널 트리 기준, `Kbuild`는 `obj-m := g9_hot.o g9_imu.o` 한 줄):

```sh
cd /var/tmp/G9/mod && make -C /var/lab/out/linux-build M=$PWD -j2 modules 2>&1 | grep -E "warning|error|CC|LD"
/sbin/modinfo g9_imu.ko
```

```text
  CC [M]  g9_hot.o
  CC [M]  g9_imu.o
  CC [M]  g9_hot.mod.o
  CC [M]  .module-common.o
  CC [M]  g9_imu.mod.o
  LD [M]  g9_hot.ko
  LD [M]  g9_imu.ko
filename:       /var/tmp/G9/mod/g9_imu.ko
description:    G9: g9-imu driver with module alias + class uevent
license:        GPL
alias:          platform:g9-imu
depends:        
name:           g9_imu
vermagic:       6.18.55 SMP preempt mod_unload aarch64
```

출력에서 볼 것: 경고 0개. `alias: platform:g9-imu`는 우리가 쓴 적 없는 줄이다 — `MODULE_DEVICE_TABLE`이 만들었다(§4에서 추적한다).

#### 예제 3 — `g9_listen.c` (모든 uevent 변수를 찍는 netlink 리스너)

A9의 리스너는 몇 개 키만 골라 찍었다. 여기서는 **모든 변수**를 찍고, 수신 시각(CLOCK_MONOTONIC)과 DEVPATH 필터를 붙였다. QEMU 게스트의 BusyBox rootfs에는 libc가 없으므로 VM에서 `gcc -static`으로 빌드해 initramfs에 넣었다.

```c
// g9_listen: NETLINK_KOBJECT_UEVENT group 1을 듣고 모든 변수를 찍는다 (필터 문자열 선택)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#define DIE(m) do { perror(m); exit(1); } while (0)

int main(int argc, char **argv)
{
	const char *filt = argc > 1 ? argv[1] : "";	/* DEVPATH에 이 문자열이 있는 것만 */
	int s = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	if (s < 0) DIE("socket");
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	if (bind(s, (struct sockaddr *)&sa, sizeof sa) < 0) DIE("bind");
	int rcv = 1 << 20;				/* 폭주 때 ENOBUFS 방지 */
	setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof rcv);
	printf("g9_listen: ready\n"); fflush(stdout);
	for (;;) {
		char buf[8192];
		ssize_t n = recv(s, buf, sizeof buf - 1, 0);
		if (n < 0) DIE("recv");
		buf[n] = '\0';
		if (!strstr(buf, filt)) continue;
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		printf("[%5ld.%06ld] %s (%zd bytes)\n", (long)ts.tv_sec, ts.tv_nsec / 1000, buf, n);
		for (char *p = buf + strlen(buf) + 1; p < buf + n; p += strlen(p) + 1)
			printf("    %s\n", p);
		fflush(stdout);
	}
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static g9_listen.c -o g9_listen     # VM에서, 경고 0
```

#### 예제 4 — 장치 두 개를 꽂고 하나를 뽑기 (QEMU 게스트, mdev 없이 devtmpfs만)

무엇을 확인하나: `add` 한 번에 uevent가 몇 개, 어떤 순서로 나오는지, 각 이벤트에 어떤 변수가 실리는지, 그리고 `/dev` 노드가 누구 손으로 생기는지.

게스트 실행 방법(이후 모든 게스트 예제가 같다): 랩 rootfs를 `/var/tmp/G9/rootfs`로 복사해 `/g9/`에 `.ko`·바이너리·스크립트를 넣고 cpio로 묶은 뒤, `LAB_CMD=sh~/g9/t1.sh`로 부팅한다.

```sh
# 호스트(VM)에서
timeout 180 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd /var/tmp/G9/i.gz \
  -append "console=ttyAMA0 quiet printk.time=0 LAB_CMD=sh~/g9/t1.sh"
```

```sh
# /g9/t1.sh (게스트)
cd /g9; insmod g9_hot.ko; insmod g9_imu.ko
./g9_listen g9 > /tmp/ue.log & sleep 1
echo "front SN1001" > /sys/kernel/g9_hot/add
echo "rear SN2002"  > /sys/kernel/g9_hot/add
sleep 1
echo "== /dev (devtmpfs가 만든 노드)"; ls --color=never -l /dev/g9_imu*; cat /dev/g9_imu1
echo "== sysfs uevent 파일 (platform device / class device)"
cat /sys/devices/platform/g9-imu.0/uevent; echo "--"; cat /sys/class/g9/g9_imu0/uevent
echo "== remove 0"; echo 0 > /sys/kernel/g9_hot/remove; sleep 1
echo "== listener log"; cat /tmp/ue.log
```

```text
lab initramfs: 6.18.55 up 2.20s
== /dev (devtmpfs가 만든 노드)
crw-r-----    1 0        0         510,   0 Oct 11 01:37 /dev/g9_imu0
crw-r-----    1 0        0         510,   1 Oct 11 01:37 /dev/g9_imu1
rear SN2002
== sysfs uevent 파일 (platform device / class device)
DEVTYPE=g9_sensor
DRIVER=g9_imu
MODALIAS=platform:g9-imu
G9_POSITION=front
G9_SERIAL=SN1001
--
MAJOR=510
MINOR=0
DEVNAME=g9_imu0
DEVMODE=0640
G9_POSITION=front
== remove 0
== listener log
g9_listen: ready
[    3.527923] add@/devices/platform/g9-imu.0 (186 bytes)
    ACTION=add
    DEVPATH=/devices/platform/g9-imu.0
    SUBSYSTEM=platform
    DEVTYPE=g9_sensor
    MODALIAS=platform:g9-imu
    G9_POSITION=front
    G9_SERIAL=SN1001
    SEQNUM=2206
[    3.548537] add@/devices/platform/g9-imu.0/g9/g9_imu0 (189 bytes)
    ACTION=add
    DEVPATH=/devices/platform/g9-imu.0/g9/g9_imu0
    SUBSYSTEM=g9
    MAJOR=510
    MINOR=0
    DEVNAME=g9_imu0
    DEVMODE=0640
    G9_POSITION=front
    SEQNUM=2207
[    3.548694] bind@/devices/platform/g9-imu.0 (202 bytes)
    ACTION=bind
    DEVPATH=/devices/platform/g9-imu.0
    SUBSYSTEM=platform
    DEVTYPE=g9_sensor
    DRIVER=g9_imu
    MODALIAS=platform:g9-imu
    G9_POSITION=front
    G9_SERIAL=SN1001
    SEQNUM=2208
[    3.548786] add@/devices/platform/g9-imu.1 (185 bytes)
...(g9-imu.1에 대해 같은 add / add(g9_imu1) / bind 세 통, SEQNUM 2209~2211 — 생략)
[    4.827294] remove@/devices/platform/g9-imu.0/g9/g9_imu0 (195 bytes)
    ...(add 때와 같은 변수, ACTION=remove, SEQNUM=2212 — 생략)
[    4.843227] unbind@/devices/platform/g9-imu.0 (167 bytes)
    ACTION=unbind
    DEVPATH=/devices/platform/g9-imu.0
    SUBSYSTEM=platform
    DEVTYPE=g9_sensor
    G9_POSITION=front
    G9_SERIAL=SN1001
    SEQNUM=2213
[    4.843363] remove@/devices/platform/g9-imu.0 (192 bytes)
    ...(MODALIAS 포함 add 때와 같은 변수, SEQNUM=2214 — 생략)
reboot: Power down
```

출력에서 볼 것 (`ls`의 날짜, 타임스탬프, SEQNUM, major 510은 실행마다 다르다):

- **꽂기 한 번 = uevent 세 통**: platform device `add`(#2206) → 드라이버 probe가 만든 class 장치 `add`(#2207) → probe 성공 후 platform device `bind`(#2208). 뽑기는 역순: class `remove` → `unbind` → platform `remove`.
- **`/dev/g9_imu0`은 아무 데몬도 없는데 생겼다.** devtmpfs가 `device_add()` 안에서 직접 만들었다. 권한 `crw-r-----`(0640)은 class의 `devnode` 콜백이 정한 값이고, 같은 값이 uevent의 `DEVMODE=0640`으로도 나갔다. 소유자는 root:root(0:0) — 그룹을 바꾸는 건 유저 공간 몫이다.
- platform device 이벤트에는 `MAJOR`/`MINOR`/`DEVNAME`이 **없다**(노드가 없는 장치). 노드는 class 장치(`g9_imu0`)에 붙는다. udev rule을 쓸 때 "어느 이벤트에 매칭하나"가 여기서 갈린다.
- `unbind` 이벤트에는 `MODALIAS`가 **빠졌다**. `kobject_uevent_env()`가 `KOBJ_UNBIND`일 때 `zap_modalias_env()`로 지운다 — 드라이버가 떨어졌다고 udev가 모듈을 다시 로드하지 않게 하려는 것이다.
- sysfs의 `uevent` 파일을 **읽으면** 같은 변수 목록(ACTION·DEVPATH·SUBSYSTEM·SEQNUM 제외)을 볼 수 있다. 리스너 없이 디버깅할 때 제일 빠른 방법이다.
- class 장치 이벤트에는 `G9_POSITION`만 있고 `G9_SERIAL`은 없다. **각 uevent에는 그 kobject의 콜백이 넣은 변수만 실린다** — 부모의 변수는 자동으로 따라오지 않는다. 그래서 g9_imu의 class 콜백이 부모 platform_data에서 위치를 꺼내 따로 넣었다(§7에서 이 차이가 실제로 드러난다).

---

## 2. uevent 변수 해부 — 누가 어떤 KEY를 넣나

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 392">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="12.0" y="18.0" font-size="12" text-anchor="start">uevent 변수 (실행 출력에서)</text><text x="352.0" y="18.0" font-size="12" text-anchor="start">누가 add_uevent_var()로 넣나 (호출 순서)</text><rect x="10" y="28" width="320" height="24" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="18.0" y="44.2" font-size="12" text-anchor="start">add@/devices/platform/g9-imu.0</text><rect x="350" y="28" width="320" height="24" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="358.0" y="44.2" font-size="12" text-anchor="start">메시지 머리: ACTION@DEVPATH</text><line x1="330" y1="40" x2="350" y2="40" stroke="currentColor" stroke-width="1"/><rect x="10" y="57" width="320" height="24" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="18.0" y="73.2" font-size="12" text-anchor="start">ACTION=add</text>
<rect x="350" y="57" width="320" height="24" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="358.0" y="73.2" font-size="12" text-anchor="start">kobject_uevent_env() 기본 키</text><line x1="330" y1="69" x2="350" y2="69" stroke="currentColor" stroke-width="1"/><rect x="10" y="86" width="320" height="24" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="18.0" y="102.2" font-size="12" text-anchor="start">DEVPATH=/devices/platform/g9-imu.0</text><rect x="350" y="86" width="320" height="24" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="358.0" y="102.2" font-size="12" text-anchor="start">kobject_get_path() — /sys 기준 경로</text><line x1="330" y1="98" x2="350" y2="98" stroke="currentColor" stroke-width="1"/><rect x="10" y="115" width="320" height="24" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="18.0" y="131.2" font-size="12" text-anchor="start">SUBSYSTEM=platform</text>
<rect x="350" y="115" width="320" height="24" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="358.0" y="131.2" font-size="12" text-anchor="start">kset uevent_ops-&gt;name → bus 이름</text><line x1="330" y1="127" x2="350" y2="127" stroke="currentColor" stroke-width="1"/><rect x="10" y="144" width="320" height="24" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="18.0" y="160.2" font-size="12" text-anchor="start">MAJOR= MINOR= DEVNAME= DEVMODE=</text><rect x="350" y="144" width="320" height="24" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="358.0" y="160.2" font-size="12" text-anchor="start">dev_uevent(): devt 있을 때 (class 장치)</text><line x1="330" y1="156" x2="350" y2="156" stroke="currentColor" stroke-width="1"/><rect x="10" y="173" width="320" height="24" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="18.0" y="189.2" font-size="12" text-anchor="start">DEVTYPE=g9_sensor</text>
<rect x="350" y="173" width="320" height="24" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="358.0" y="189.2" font-size="12" text-anchor="start">dev_uevent(): dev-&gt;type-&gt;name</text><line x1="330" y1="185" x2="350" y2="185" stroke="currentColor" stroke-width="1"/><rect x="10" y="202" width="320" height="24" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="18.0" y="218.2" font-size="12" text-anchor="start">DRIVER=g9_imu (bind 뒤)</text><rect x="350" y="202" width="320" height="24" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="358.0" y="218.2" font-size="12" text-anchor="start">dev_uevent(): dev-&gt;driver</text><line x1="330" y1="214" x2="350" y2="214" stroke="currentColor" stroke-width="1"/><rect x="10" y="231" width="320" height="24" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="18.0" y="247.2" font-size="12" text-anchor="start">OF_NAME= OF_COMPATIBLE_0= ...</text>
<rect x="350" y="231" width="320" height="24" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="358.0" y="247.2" font-size="12" text-anchor="start">of_device_uevent(): DT 노드가 있을 때</text><line x1="330" y1="243" x2="350" y2="243" stroke="currentColor" stroke-width="1"/><rect x="10" y="260" width="320" height="24" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="18.0" y="276.2" font-size="12" text-anchor="start">MODALIAS=platform:g9-imu</text><rect x="350" y="260" width="320" height="24" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="358.0" y="276.2" font-size="12" text-anchor="start">bus-&gt;uevent = platform_uevent()</text><line x1="330" y1="272" x2="350" y2="272" stroke="currentColor" stroke-width="1"/><rect x="10" y="289" width="320" height="24" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="18.0" y="305.2" font-size="12" text-anchor="start">G9_POSITION=front (class 장치)</text>
<rect x="350" y="289" width="320" height="24" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="358.0" y="305.2" font-size="12" text-anchor="start">class-&gt;dev_uevent() = g9_class_uevent()</text><line x1="330" y1="301" x2="350" y2="301" stroke="currentColor" stroke-width="1"/><rect x="10" y="318" width="320" height="24" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="18.0" y="334.2" font-size="12" text-anchor="start">G9_POSITION= G9_SERIAL=</text><rect x="350" y="318" width="320" height="24" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="358.0" y="334.2" font-size="12" text-anchor="start">dev-&gt;type-&gt;uevent() = g9_type_uevent()</text><line x1="330" y1="330" x2="350" y2="330" stroke="currentColor" stroke-width="1"/><rect x="10" y="347" width="320" height="24" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="18.0" y="363.2" font-size="12" text-anchor="start">SEQNUM=2206</text>
<rect x="350" y="347" width="320" height="24" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="358.0" y="363.2" font-size="12" text-anchor="start">kobject_uevent_env() 마지막, 전역 증가</text><line x1="330" y1="359" x2="350" y2="359" stroke="currentColor" stroke-width="1"/>
</svg>
```
그림 2 — 예제 4의 실제 변수들과 그 변수를 넣은 함수. 위에서 아래가 `add_uevent_var()` 호출 순서다.

`kobject_uevent_env()`(lib/kobject_uevent.c, v6.18)가 하는 일을 순서대로 적으면:

1. kobject에서 부모로 올라가며 **kset**을 찾는다. `struct device`는 전부 `devices_kset`에 속하고, 그 kset의 `uevent_ops`가 driver core의 `device_uevent_ops`(`dev_uevent_filter`, `dev_uevent_name`, `dev_uevent`)다.
2. `uevent_suppress`가 켜져 있거나 `filter()`가 0을 돌려주면 **조용히 버린다**. `dev_uevent_filter()`는 bus도 class도 없는 장치를 거른다.
3. 버퍼 `struct kobj_uevent_env`(envp 64칸 + buf 2048바이트)를 `kzalloc`하고 기본 키 `ACTION`, `DEVPATH`, `SUBSYSTEM`을 넣는다. SUBSYSTEM은 `dev_uevent_name()` — bus가 있으면 bus 이름, 없으면 class 이름.
4. 호출자가 준 추가 변수(`kobject_uevent_env(kobj, action, envp_ext)`의 `envp_ext`)를 넣는다.
5. kset의 `uevent()` = `dev_uevent()`를 부른다. 이 안에서 **MAJOR/MINOR/DEVNAME/DEVMODE**(devt가 있을 때, `device_get_devnode()`), **DEVTYPE**(`dev->type->name`), **DRIVER**, **OF_NAME/OF_FULLNAME/OF_COMPATIBLE_N/OF_COMPATIBLE_x**(DT 노드가 있을 때, D3 예제 8에서 봤다), 그다음 **`bus->uevent`**(platform이면 `platform_uevent()` → `MODALIAS=platform:<name>`, DT 장치면 `of_device_uevent_modalias()`가 `of:N...T...C...`를 넣는다), **`class->dev_uevent`**, **`dev->type->uevent`** 순으로 부른다.
6. `SEQNUM`을 전역 카운터 `uevent_seqnum`에서 `atomic64_inc_return()`으로 받아 붙인다.
7. `kobject_uevent_net_broadcast()`로 netlink 방송, `CONFIG_UEVENT_HELPER`가 켜져 있고 `uevent_helper[0]`이 비어 있지 않으면 `HOME=/`, `PATH=...`를 더해 helper를 `call_usermodehelper_exec(..., UMH_NO_WAIT)`로 실행한다.

### 2.1 드라이버가 변수를 넣는 세 자리

| 자리 | 콜백 | 언제 쓰나 | 예 |
|---|---|---|---|
| bus | `struct bus_type.uevent` | 버스 공통 키, 특히 **MODALIAS** | `platform_uevent()`, `usb_uevent()`(`PRODUCT=`), `i2c_device_uevent()` |
| class | `struct class.dev_uevent` | 그 class의 모든 장치에 공통 키 | `power_supply` class의 `power_supply_uevent()`(`POWER_SUPPLY_*` 키), 우리 `g9_class_uevent()` |
| device_type | `struct device_type.uevent` | 같은 bus·class 안에서 장치 종류별 키 | `input_dev_type`의 `input_dev_uevent()`(`PRODUCT`/`NAME`/`EV=`/`KEY=` 비트맵), USB `usb_if_device_type`의 `usb_if_uevent()`(`INTERFACE=`, `MODALIAS=usb:v...`), 우리 `g9_sensor` |
| (커널 공통) | `dev_uevent()` 본문 | 모든 장치 | `MAJOR`/`MINOR`/`DEVNAME`/`DEVTYPE`/`DRIVER`/`OF_*` |

`change` 이벤트로 상태를 알리고 싶은 드라이버는 직접 보낸다:

```c
char *envp[] = { "G9_EVENT=CALIB_DONE", NULL };	/* 예: 캘리브레이션 끝 */
kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
```

(위 세 줄은 패턴 설명용이고 이 노트에서 따로 실행하지 않았다.) 전원 공급 장치(`power_supply`)가 배터리 상태가 바뀔 때마다 `KOBJ_CHANGE`를 보내는 것이 대표적이다. 단, uevent는 **비싼 경로**(메모리 할당 + 문자열 포맷 + netlink skb 방송 + 모든 리스너의 규칙 처리, helper가 있으면 프로세스 생성까지)라서 센서 데이터 같은 고빈도 알림에 쓰면 안 된다. 그건 G6의 `poll`이나 IIO 버퍼(I 모듈)로 한다.

### 2.2 변수 이름 규칙과 함정

- 키는 대문자 + 밑줄이 관례다. udev rule의 `ENV{G9_POSITION}`으로 그대로 매칭된다.
- 값에 공백이 있어도 되지만, uevent_helper로 실행되는 셸 스크립트에서 따옴표를 안 쓰면 깨진다.
- 한 이벤트의 변수 총합이 2048바이트를 넘거나 64개를 넘으면 `add_uevent_var()`가 `WARN(1, "add_uevent_var: buffer size too small")`(또는 `too many keys`)을 찍고 `-ENOMEM`을 돌려준다. 긴 문자열(펌웨어 버전 목록 등)을 uevent에 싣지 말고 sysfs 속성(G8)으로 노출하자.

---

## 3. 유저 공간으로 가는 두 길 — netlink와 uevent_helper

### 3.1 netlink 방송 (현재의 주 경로)

A9 예제 11에서 이미 봤다: `socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT)` + `bind(nl_groups = 1)`이면 커널이 보내는 원본 uevent를 받는다. 여기서는 그 위에 얹히는 사실 몇 가지만 정리한다.

| 항목 | 내용 |
|---|---|
| group 1 | 커널 원본 이벤트 (`kobject_uevent_net_broadcast()`). `udevadm monitor --kernel`이 보는 것 |
| group 2 | udevd가 rule 처리를 **끝낸 뒤** 다시 방송하는 이벤트. 헤더가 `"libudev"` 매직으로 시작하는 바이너리 형식이고 udev가 추가한 속성(`DEVLINKS`, `ID_*`, `TAGS`)이 들어 있다. `udevadm monitor --udev`, libudev `udev_monitor_new_from_netlink(udev, "udev")`가 보는 것 |
| 순서 | `SEQNUM`이 커널 전역 단조 증가. 멀티캐스트라 수신 버퍼가 차면 `ENOBUFS`로 이벤트를 잃는다 — 그래서 udevd는 큰 `SO_RCVBUF`를 쓰고, 예제 3도 1 MB로 키웠다 |

앱이 "장치 노드가 **권한까지 준비된 뒤**" 알림을 받아야 한다면 group 1이 아니라 group 2(libudev monitor)를 들어야 한다. group 1 시점에는 udev가 아직 `MODE`/`GROUP`을 적용하지 않았을 수 있다. §8.1의 호스트 실측에서 같은 `add` 이벤트가 KERNEL → UDEV로 약 80 ms 뒤에 다시 나온다.

### 3.2 uevent_helper — `/proc/sys/kernel/hotplug`

옛날(2.4~2.6 초) 방식은 이벤트마다 커널이 **프로그램을 하나 실행**하는 것이었다. 경로는 `/proc/sys/kernel/hotplug`(= sysctl `kernel.hotplug`, = `/sys/kernel/uevent_helper`)에 적는다. 커널 쪽 코드는 §2의 7단계 — `call_usermodehelper_setup()` + `call_usermodehelper_exec(info, UMH_NO_WAIT)`. argv는 `[helper, SUBSYSTEM]`, 환경 변수는 uevent 변수 전체 + `HOME=/` + `PATH=/sbin:/bin:/usr/sbin:/usr/bin`이다.

- 장점: 데몬이 필요 없다. 부팅 아주 초기(initramfs)에도 동작한다. BusyBox `mdev`가 이 방식으로 설계됐다.
- 단점: **이벤트마다 fork+exec**. 부팅 때 수천 개의 이벤트가 쏟아지면 프로세스 폭주가 나고(fork bomb에 가깝다), `UMH_NO_WAIT`라서 **실행 순서가 SEQNUM 순서와 다를 수 있다**(add보다 remove가 먼저 처리되는 레이스). mdev는 이를 `/dev/mdev.seq` 파일로 직렬화한다(§7.3).
- 그래서 `drivers/base/Kconfig`의 `UEVENT_HELPER` 도움말은 "This should not be used today" — 부팅 때 짧은 시간에 이벤트가 몰리면 이벤트당 프로세스 하나가 부하를 만들고, 작은 시스템에서는 부팅 중 OOM까지 난다고 적는다. arm64 defconfig 기반인 랩 커널은 꺼져 있다.

#### 예제 5 — 랩 커널에는 helper가 없다 + sysfs `uevent` 파일에 쓰기

무엇을 확인하나: (1) 공용 랩 커널(arm64 defconfig 기반)에 `CONFIG_UEVENT_HELPER`가 있는지, (2) sysfs의 `uevent` 파일에 쓰면 커널이 이벤트를 **다시 만들어** 보내는지(합성 uevent).

```sh
# /g9/t2.sh (게스트)
cd /g9; insmod g9_hot.ko; insmod g9_imu.ko
echo "front SN1001" > /sys/kernel/g9_hot/add; sleep 1
./g9_listen g9-imu.0 > /tmp/ue.log & sleep 1
echo change > /sys/devices/platform/g9-imu.0/uevent
echo "add 7c1e9d2a-0b5f-4a43-9c1d-2f6e8a1b3c4d CALIB=2 SRC=test" > /sys/devices/platform/g9-imu.0/uevent
sleep 1; cat /tmp/ue.log
echo "== uevent_helper?"; ls /proc/sys/kernel/ | grep -E "hotplug|modprobe"; cat /proc/sys/kernel/modprobe
zcat /proc/config.gz | grep -E "UEVENT_HELPER|DEVTMPFS"
```

```text
lab initramfs: 6.18.55 up 1.01s
g9_listen: ready
[    3.129512] change@/devices/platform/g9-imu.0 (219 bytes)
    ACTION=change
    DEVPATH=/devices/platform/g9-imu.0
    SUBSYSTEM=platform
    SYNTH_UUID=0
    DEVTYPE=g9_sensor
    DRIVER=g9_imu
    MODALIAS=platform:g9-imu
    G9_POSITION=front
    G9_SERIAL=SN1001
    SEQNUM=2209
[    3.136379] add@/devices/platform/g9-imu.0 (285 bytes)
    ACTION=add
    DEVPATH=/devices/platform/g9-imu.0
    SUBSYSTEM=platform
    SYNTH_UUID=7c1e9d2a-0b5f-4a43-9c1d-2f6e8a1b3c4d
    SYNTH_ARG_CALIB=2
    SYNTH_ARG_SRC=test
    DEVTYPE=g9_sensor
    DRIVER=g9_imu
    MODALIAS=platform:g9-imu
    G9_POSITION=front
    G9_SERIAL=SN1001
    SEQNUM=2210
== uevent_helper?
modprobe
/sbin/modprobe
# CONFIG_UEVENT_HELPER is not set
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y
# CONFIG_DEVTMPFS_SAFE is not set
```

출력에서 볼 것:

- `uevent` 파일에 ACTION 이름을 쓰면 `uevent_store()` → `kobject_synth_uevent()`가 **진짜와 똑같은 변수 + `SYNTH_UUID`** 로 이벤트를 다시 보낸다. 장치는 이미 있는데 "add"가 한 번 더 간다. 이것이 **coldplug**(§5)와 `udevadm trigger`의 원리다. UUID와 `KEY=VALUE`를 덧붙이면 `SYNTH_ARG_KEY`로 실려서, 트리거를 누가 일으켰는지 표시할 수 있다(`udevadm trigger`가 자기가 일으킨 이벤트를 알아보는 데 쓰고, `--uuid` 옵션으로 그 UUID를 출력한다).
- `/proc/sys/kernel/`에 `hotplug`가 **없다**(`modprobe`만 있다). 랩 커널은 `CONFIG_UEVENT_HELPER`가 꺼져 있다. 반면 VM 호스트 Ubuntu 커널은 `CONFIG_UEVENT_HELPER=y`, `CONFIG_UEVENT_HELPER_PATH=""`로 기능은 넣되 기본은 빈 값이다(아래 §4.4 출력).
- `/proc/sys/kernel/modprobe`(`/sbin/modprobe`)는 다른 경로다. 커널 **자신이** 모듈이 필요할 때(`request_module()`, 예: 소켓 패밀리·파일시스템·`ip link add type vcan`) 직접 실행하는 프로그램이다. uevent의 `MODALIAS`로 모듈을 올리는 건 **유저 공간 데몬이 하는** 일이고, 둘은 별개다.

#### 예제 6 — helper를 켠 커널로 다시 빌드해서 mdev를 hotplug helper로 쓰기

mdev의 원래 사용법(`echo /sbin/mdev > /proc/sys/kernel/hotplug`)을 보려고, 랩 커널 설정을 `/var/tmp/G9/build`로 복사해 `UEVENT_HELPER`만 켜서 `Image`를 다시 빌드했다(공용 랩은 건드리지 않는다).

```sh
cp -a /var/lab/build/linux /var/tmp/G9/build && rm -f /var/tmp/G9/build/Makefile /var/tmp/G9/build/source
S=/var/lab/src/linux-6.18.55
$S/scripts/config --file /var/tmp/G9/build/.config -e UEVENT_HELPER --set-str UEVENT_HELPER_PATH ""
make -C $S O=/var/tmp/G9/build olddefconfig && grep UEVENT_HELPER /var/tmp/G9/build/.config
make -C $S O=/var/tmp/G9/build -j2 Image
```

```text
CONFIG_UEVENT_HELPER=y
CONFIG_UEVENT_HELPER_PATH=""
...
real	19m6.360s
user	30m29.359s
sys	6m24.255s
-rw-rw-r-- 1 donh donh 42740224 Oct 10 18:52 build/arch/arm64/boot/Image
```

출력에서 볼 것: 옵션 하나만 바꿨는데 `-j2`로 19분이 걸렸다. 빌드 트리를 다른 경로로 복사하면 오브젝트의 명령줄(`.cmd` 파일 속 경로)이 달라져 kbuild가 거의 전부를 다시 컴파일한다. 같은 버전·같은 나머지 설정이라 `vermagic`이 같아서, 랩 트리로 빌드한 `g9_hot.ko`/`g9_imu.ko`를 이 커널에서도 그대로 insmod할 수 있었다. 이 커널로 돌린 결과는 §7.3 예제 10에 있다.

---

## 4. `MODULE_DEVICE_TABLE` → modalias → `modules.alias` → `modprobe`

D3 §9에서 DT 장치(`of:N...T...C...`)로 이 사슬을 이미 따라갔다. 여기서는 **빌드 산출물 쪽**과 **누가 modprobe를 부르나** 쪽을 채운다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 410">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="12" text-anchor="start">빌드 때</text><rect x="10" y="26" width="200" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="110.0" y="47.0" font-size="13" text-anchor="middle">MODULE_DEVICE_TABLE</text><text x="110.0" y="64.0" font-size="13" text-anchor="middle">(platform, g9_ids)</text><rect x="240" y="26" width="190" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="335.0" y="47.0" font-size="13" text-anchor="middle">modpost (file2alias)</text><text x="335.0" y="64.0" font-size="13" text-anchor="middle">→ g9_imu.mod.c</text><rect x="460" y="26" width="210" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="565.0" y="47.0" font-size="13" text-anchor="middle">.modinfo 섹션</text>
<text x="565.0" y="64.0" font-size="13" text-anchor="middle">alias=platform:g9-imu</text><line x1="210" y1="51" x2="238" y2="51" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/><line x1="430" y1="51" x2="458" y2="51" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/><text x="10.0" y="104.0" font-size="12" text-anchor="start">설치 때 (depmod)</text><rect x="250" y="112" width="200" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="350.0" y="133.1" font-size="13" text-anchor="middle">modules.alias</text><text x="350.0" y="150.1" font-size="13" text-anchor="middle">kmod: 원래 문자열</text><rect x="470" y="112" width="200" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="570.0" y="133.1" font-size="13" text-anchor="middle">modules.dep.bb</text><text x="570.0" y="150.1" font-size="13" text-anchor="middle">BusyBox: '-' → '_'</text>
<line x1="565" y1="76" x2="360" y2="110" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/><line x1="565" y1="76" x2="570" y2="110" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/><line x1="10" y1="182" x2="670" y2="182" stroke="currentColor" stroke-width="1" stroke-dasharray="4 4"/><text x="10.0" y="202.0" font-size="12" text-anchor="start">실행 때 (hotplug)</text><rect x="10" y="212" width="200" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="110.0" y="236.1" font-size="13" text-anchor="middle">uevent</text><text x="110.0" y="253.1" font-size="13" text-anchor="middle">MODALIAS=platform:g9-imu</text><rect x="240" y="212" width="200" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340.0" y="236.1" font-size="13" text-anchor="middle">udev 80-drivers.rules</text><text x="340.0" y="253.1" font-size="13" text-anchor="middle">RUN{builtin} kmod load</text>
<rect x="240" y="280" width="200" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340.0" y="301.1" font-size="13" text-anchor="middle">mdev.conf</text><text x="340.0" y="318.1" font-size="13" text-anchor="middle">@modprobe "$MODALIAS"</text><rect x="470" y="212" width="200" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="570.0" y="236.1" font-size="13" text-anchor="middle">modprobe: alias</text><text x="570.0" y="253.1" font-size="13" text-anchor="middle">glob 매칭 → 모듈 이름</text><line x1="210" y1="240" x2="238" y2="240" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/><line x1="210" y1="250" x2="238" y2="300" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/><line x1="440" y1="240" x2="468" y2="240" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/><line x1="440" y1="305" x2="520" y2="270" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/>
<line x1="360" y1="162" x2="520" y2="210" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 4" marker-end="url(#ah3)"/><line x1="570" y1="162" x2="600" y2="210" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 4" marker-end="url(#ah3)"/><rect x="470" y="340" width="200" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="570.0" y="361.1" font-size="13" text-anchor="middle">g9_imu.ko 로드</text><text x="570.0" y="378.1" font-size="13" text-anchor="middle">→ 버스 매칭 → probe()</text><line x1="570" y1="268" x2="570" y2="338" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/><text x="10.0" y="362.0" font-size="12" text-anchor="start">예: alias usb:v0403p6001d*dc*dsc*dp*ic*isc*ip*in* ftdi_sio</text><text x="10.0" y="382.0" font-size="12" text-anchor="start">장치 문자열이 이 glob과 맞으면 ftdi_sio 로드</text>
</svg>
```
그림 3 — 별칭은 빌드 때 `.ko`에 박히고, depmod가 색인으로 모으고, 장치가 생길 때 uevent의 MODALIAS로 그 색인을 찾는다.

### 4.1 빌드 때 — 매크로 하나가 `.ko`에 문자열을 박는다

v6.18의 `include/linux/module.h`:

```c
#define MODULE_DEVICE_TABLE(type, name)					\
static typeof(name) __mod_device_table(type, name)			\
  __attribute__ ((used, alias(__stringify(name))))
```

매크로는 테이블에 `__mod_device_table__kmod_<모듈>__<type>__<이름>`이라는 **별칭 심볼**을 하나 붙일 뿐이다. 그 심볼을 modpost(`scripts/mod/file2alias.c`)가 찾아서 테이블 내용을 읽고, `type`별 규칙으로 alias 문자열을 만들어 `<모듈>.mod.c`에 `MODULE_ALIAS("...")`로 써 넣는다. 그게 컴파일되어 `.modinfo` 섹션의 `alias=` 문자열이 된다.

#### 예제 7 — 심볼에서 `.modinfo`까지 따라가기

```sh
cd /var/tmp/G9/mod
nm g9_imu.o | grep -i device_table
grep -n "MODULE_ALIAS" g9_imu.mod.c
readelf -p .modinfo g9_imu.ko | grep -E "alias|name"
```

```text
0000000000000170 r __mod_device_table__kmod_g9_imu__platform__g9_ids
20:MODULE_ALIAS("platform:g9-imu");
  [    4b]  alias=platform:g9-imu
  [    6a]  name=g9_imu
```

출력에서 볼 것: `.o`에는 심볼만 있고, 문자열 `platform:g9-imu`는 **modpost가 만든 `.mod.c`** 에서 처음 생긴다. 버스마다 문자열 모양이 다르다(file2alias.c의 `do_*_entry()` 함수들):

| 버스 | 테이블 타입 | alias 모양 (예) |
|---|---|---|
| platform | `platform_device_id` | `platform:g9-imu` |
| DT | `of_device_id` | `of:N*T*Cdon,imu` (+ `C*` 변형) |
| I2C | `i2c_device_id` | `i2c:bmi160` |
| SPI | `spi_device_id` | `spi:bmi160` |
| USB | `usb_device_id` | `usb:v0403p6001d*dc*dsc*dp*ic*isc*ip*in*` |
| PCI | `pci_device_id` | `pci:v00008086d00001533sv*sd*bc*sc*i*` |

`MODULE_DEVICE_TABLE`의 `type` 인자가 `platform`이면 file2alias가 platform 규칙을 쓴다. **테이블을 만들어 놓고 이 매크로를 빠뜨리면** `.ko`에 alias가 없다 → insmod로는 잘 동작하지만 자동 로드는 안 된다(D3 §9의 함정).

### 4.2 설치 때 — depmod가 색인을 만든다

kmod `depmod`는 `/lib/modules/<ver>/` 아래 모든 `.ko`의 alias를 모아 `modules.alias`(텍스트)와 `modules.alias.bin`(trie)를 만든다. BusyBox의 modprobe-small 구현은 `modules.dep.bb` 한 파일에 의존성과 alias를 같이 둔다(E3 §11, D3 §9). 이 노트의 게스트 실행 결과:

```text
== modules.dep.bb (g9)
extra/g9_imu.ko platform:g9_imu
```

`platform:g9-imu`가 `platform:g9_imu`로 바뀌어 있다. BusyBox가 색인할 때 `-`를 `_`로 정규화한다(D3에서 `of:N*T*Cdon,imu_v2`로 본 것과 같다). modprobe가 찾을 때 요청 문자열도 같은 방식으로 정규화하므로 매칭은 된다 — 아래 예제 9에서 실제로 자동 로드된다.

### 4.3 실행 때 — 누가 modprobe를 부르나

| 시스템 | 누가 | 어떻게 |
|---|---|---|
| systemd-udevd | `80-drivers.rules` | `ENV{MODALIAS}=="?*", RUN{builtin}+="kmod load"` — 프로세스를 띄우지 않고 udevd 안에서 libkmod로 로드 |
| BusyBox mdev | `/etc/mdev.conf` 한 줄 | `$MODALIAS=.* 0:0 660 @modprobe "$MODALIAS"` — 이벤트마다 셸 + modprobe 실행 |
| 아무 데몬 없음 | 없음 | 자동 로드 안 됨. init 스크립트에서 `modprobe` 목록을 직접 부르거나 드라이버를 built-in(`=y`)으로 |

### 4.4 예제 8 — 호스트(Ubuntu)에서 실제 규칙과 색인 확인

무엇을 확인하나: 데스크톱 udev가 자동 로드에 쓰는 실제 rule 파일, 실제 장치의 modalias가 어느 모듈로 풀리는지, 그리고 USB-시리얼(GNSS 수신기에 흔한 FTDI 칩) 별칭이 glob으로 어떻게 생겼는지. 모두 읽기 전용 명령이다.

```sh
grep -v "^#" /usr/lib/udev/rules.d/80-drivers.rules | grep -v "^$"
cat /sys/bus/virtio/devices/virtio2/modalias; echo
modprobe -R "$(cat /sys/bus/virtio/devices/virtio2/modalias)"
K=/lib/modules/$(uname -r); grep -c . $K/modules.alias
grep -m3 "^alias usb:v0403p6001" $K/modules.alias
modprobe -R "usb:v0403p6001d0600dc00dsc00dp00icFFiscFFipFFin00"
cat /proc/sys/kernel/hotplug; echo "[$(cat /proc/sys/kernel/hotplug)]"
grep UEVENT_HELPER /boot/config-$(uname -r)
```

```text
ACTION!="add", GOTO="drivers_end"
ENV{MODALIAS}=="?*", RUN{builtin}+="kmod load"
SUBSYSTEM=="tifm", ENV{TIFM_CARD_TYPE}=="SD", RUN{builtin}+="kmod load tifm_sd"
SUBSYSTEM=="tifm", ENV{TIFM_CARD_TYPE}=="MS", RUN{builtin}+="kmod load tifm_ms"
SUBSYSTEM=="memstick", RUN{builtin}+="kmod load ms_block mspro_block"
SUBSYSTEM=="i2o", RUN{builtin}+="kmod load i2o_block"
SUBSYSTEM=="module", KERNEL=="parport_pc", RUN{builtin}+="kmod load ppdev"
KERNEL=="mtd*ro", ENV{MTD_FTL}=="smartmedia", RUN{builtin}+="kmod load sm_ftl"
LABEL="drivers_end"
virtio:d00000002v00001AF4
virtio_blk
48661
alias usb:v0403p6001d*dc*dsc*dp*ic*isc*ip*in* ftdi_sio
ftdi_sio

[]
CONFIG_UEVENT_HELPER=y
CONFIG_UEVENT_HELPER_PATH=""
```

출력에서 볼 것:

- 자동 로드의 실체는 **두 번째 줄 하나**다. `ACTION=="add"`이고 MODALIAS가 비어 있지 않으면 kmod로 로드. `RUN{builtin}`은 외부 프로그램이 아니라 udevd 내장 명령이다.
- `modprobe -R`(resolve-alias)은 alias를 모듈 이름으로만 풀어 보고 로드하지 않는다. 디버깅의 첫 명령이다: "이 장치의 modalias는 어느 모듈로 가나?"
- FTDI USB-시리얼의 alias는 vendor `0403` / product `6001`만 고정하고 나머지(bcdDevice, class...)는 `*`. 장치가 보낸 MODALIAS(위에서는 손으로 만든 문자열)와 glob 매칭된다.
- 호스트 커널은 uevent_helper 기능은 있지만 경로가 비어 있다. 즉 udevd(netlink)만 쓴다.

---

## 5. coldplug — 부팅 전에 이미 생긴 장치들

커널은 initramfs나 rootfs가 마운트되기 **전**에 대부분의 장치를 만든다. 그때 보낸 uevent는 들을 사람이 없어 사라졌다. udevd가 뜬 뒤 "이미 있는 장치들의 add 이벤트를 다시 재생"해야 모듈 로드·권한·심볼릭 링크가 적용된다. 이것이 **coldplug**이고, 방법은 예제 5에서 본 sysfs `uevent` 쓰기다.

| 도구 | 하는 일 |
|---|---|
| `udevadm trigger --action=add` | `/sys`의 장치(기본은 devices)를 돌며 각 `uevent` 파일에 `add`를 쓴다. systemd에서는 `systemd-udev-trigger.service`가 부팅마다 실행 |
| `udevadm settle` | 큐에 남은 이벤트가 다 처리될 때까지 기다린다 (`systemd-udev-settle`, 지금은 비권장 — 장치 단위 `.device` 의존으로 대체) |
| `mdev -s` | `/sys/dev` 계층(`/sys/dev/char`, `/sys/dev/block`)을 훑어 `dev` 파일이 있는 장치의 노드를 만든다. 이벤트를 다시 보내지는 **않는다** — 그래서 `$MODALIAS` 규칙은 coldplug 때 돌지 않는다 |
| 손으로 | `for f in /sys/bus/*/devices/*/uevent; do echo add > $f; done` 또는 D3 예제 13의 `modprobe $(cat modalias)` 루프 |

#### 예제 9-0 — `udevadm trigger --dry-run`으로 coldplug 대상만 보기 (호스트)

```sh
sudo udevadm trigger --dry-run --verbose --action=add --subsystem-match=virtio | head
```

```text
/sys/devices/pci0000:00/0000:00:01.0/virtio0
/sys/devices/pci0000:00/0000:00:05.0/virtio1
/sys/devices/pci0000:00/0000:00:06.0/virtio2
/sys/devices/pci0000:00/0000:00:07.0/virtio3
/sys/devices/pci0000:00/0000:00:08.0/virtio4
/sys/devices/pci0000:00/0000:00:09.0/virtio5
/sys/devices/pci0000:00/0000:00:0a.0/virtio6
/sys/devices/pci0000:00/0000:00:0b.0/virtio7
```

출력에서 볼 것: `--dry-run`은 어느 `uevent` 파일에 쓸지만 보여 주고 실제로는 쓰지 않는다. 장비의 부팅이 느릴 때 "coldplug가 몇 개 장치를 재생하나"를 세는 데 쓴다.

> 함정 (mdev 장비): 부팅 스크립트가 `mdev -s`만 하고 끝나면 **built-in이 아닌 드라이버가 자동 로드되지 않는다.** 흔한 해결은 `mdev -s` 뒤에 `find /sys/ -name modalias -exec cat {} + | sort -u | xargs modprobe -a` 같은 루프를 한 번 돌리는 것이다(BusyBox 문서·Buildroot 스크립트에서 자주 보는 패턴, 이 노트에서는 실행하지 않았다 — 같은 원리의 루프는 D3 예제 13에서 실행했다).

---

## 6. devtmpfs와 udev/mdev의 역할 분담

예전(2.6 초)에는 `/dev`가 그냥 tmpfs였고 **udev가 `mknod`로 노드를 만들었다.** 그래서 udev가 늦으면 노드가 없었다. 2.6.32에서 **devtmpfs**가 들어오면서 커널이 노드를 직접 만든다. 지금의 분담:

| 일 | 누가 | 어디서 |
|---|---|---|
| 노드 생성·삭제 (이름 = `DEVNAME`, major:minor) | 커널 devtmpfs | `device_add()` → `devtmpfs_create_node()` (uevent 보내기 **전**) |
| 기본 권한 | 커널 (`devnode` 콜백의 `mode`, 없으면 0600) | class·device_type의 `.devnode` |
| 노드를 하위 디렉터리에 (`/dev/input/event0`, `/dev/bus/usb/001/002`) | 커널 | `devnode`가 이름에 `/`를 넣어 돌려줌 |
| 소유자·그룹·권한 정책 | udev `OWNER`/`GROUP`/`MODE`, mdev `uid:gid mode` | 유저 공간 |
| 안정된 이름 (심볼릭 링크) | udev `SYMLINK+=`, mdev `>path`·스크립트 | 유저 공간 |
| 모듈 로드, 펌웨어(G10), 프로그램 실행 | udev `RUN+=`·`IMPORT`, mdev `@cmd` | 유저 공간 |

예제 4에서 데몬 없이 `/dev/g9_imu0`(0640, root:root)이 생긴 것이 첫 두 줄이다. `CONFIG_DEVTMPFS_MOUNT=y`면 커널이 rootfs를 마운트한 직후 `/dev`에 devtmpfs를 자동 마운트한다(단, initramfs에는 적용되지 않아서 랩 `/init`이 `mount -t devtmpfs devtmpfs /dev`를 직접 한다).

> 함정: devtmpfs 노드 이름은 **커널이 정한다.** udev rule의 `NAME=`은 네트워크 인터페이스에만 효과가 있고, 장치 노드 이름은 바꾸지 못한다(udev 176 이후). 다른 이름이 필요하면 `SYMLINK+=`를 쓴다.

---

## 7. BusyBox mdev — 작은 장비의 udev

### 7.1 언제 mdev를 쓰나

systemd-udevd는 systemd·libkmod·hwdb와 묶여 있고 수 MB다. initramfs 하나로 도는 장비, Buildroot 최소 이미지, 부트 시간이 빠듯한 장비에서는 BusyBox `mdev`(수십 KB, BusyBox 바이너리 안의 applet)를 쓴다. Buildroot의 `/dev management` 메뉴가 정확히 이 선택지다: **정적 device table / devtmpfs only / devtmpfs + mdev / devtmpfs + eudev** (init을 systemd로 고르면 systemd-udevd).

| 모드 | 시작 방법 | 이벤트 처리 |
|---|---|---|
| hotplug helper | `echo /sbin/mdev > /proc/sys/kernel/hotplug` | 커널이 이벤트마다 `mdev`를 실행. `CONFIG_UEVENT_HELPER` 필요 |
| 데몬 (`-d`) | `mdev -d` (BusyBox `FEATURE_MDEV_DAEMON`, 랩의 1.37 빌드에서 켜져 있음) | netlink 소켓 하나로 계속 듣는다. helper 없는 커널에서도 동작 |
| coldplug (`-s`) | `mdev -s` | `/sys/dev`를 훑어 기존 장치 노드만 처리 |

### 7.2 `/etc/mdev.conf` 문법

```text
[-][ENV=regex;]<장치이름 regex>  <uid>:<gid>  <mode>  [=path | >path | !]  [@|$|*<명령>]
$ENVVAR=<regex>                  <uid>:<gid>  <mode>  ...
@<major>,<minor>[-<minor2>]      <uid>:<gid>  <mode>  ...
```

| 필드 | 뜻 |
|---|---|
| 첫 매칭 | **위에서부터 첫 번째로 맞는 줄 하나만** 적용하고 멈춘다(줄 앞에 `-`가 있으면 계속 내려간다). udev와 가장 다른 점 |
| `ENV=regex;` 접두 | uevent 변수 조건을 덧붙인다 (예: `G9_POSITION=front;g9_imu.*`) |
| `$MODALIAS=.*` | 장치 이름 대신 환경 변수로 매칭. 노드 없는 이벤트(platform device add)에도 맞는다 |
| `=path` | 노드를 그 이름/디렉터리로 옮김 (`=sensorkit/`처럼 끝에 `/`면 디렉터리) |
| `>path` | 옮기고 원래 이름에 심볼릭 링크 |
| `!` | 노드를 만들지 않음 |
| `@cmd` / `$cmd` / `*cmd` | 생성 후 / 제거 전 / 둘 다 실행. `system()`이라 `/bin/sh -c`. 환경에는 uevent 변수 전부 + `MDEV`(장치 이름) |
| `%1`, `%2` | 장치 이름 regex의 괄호 그룹 |

이 노트의 게스트에 넣은 설정과 훅 스크립트:

```text
# /etc/mdev.conf (G9) — 위에서부터 첫 매칭 줄만 적용 ('-'로 시작하면 계속 진행)
# <regex> <uid>:<gid> <mode> [>path 이동+심볼릭 링크] [@생성 후/$제거 전/*둘 다 명령]
g9_imu([0-9]+)  0:10  0660  >sensorkit/imu%1  */etc/g9_link.sh
$MODALIAS=.*    0:0   0660  @modprobe "$MODALIAS"
```

```sh
#!/bin/sh
# /etc/g9_link.sh — mdev가 부른다(cwd=/dev): MDEV=옮겨진 노드 이름, ACTION=add/remove, G9_*=커널 uevent 변수
L=/dev/sensorkit-$G9_POSITION
case "$ACTION" in
  add)    ln -sf "$MDEV" "$L" ;;
  remove) rm -f "$L" ;;
esac
echo "$(cut -d' ' -f1 /proc/uptime) $ACTION MDEV=$MDEV pos=$G9_POSITION serial=$G9_SERIAL" >> /tmp/g9_hook.log
```

목표: 센서 위치(커널이 uevent로 알려 준 `G9_POSITION`)로 **안정된 이름** `/dev/sensorkit-front`, `/dev/sensorkit-rear`를 만든다. 몇 번째로 probe됐는지(`g9_imu0`/`g9_imu1`)와 무관하게 앱은 위치 이름만 연다.

#### 예제 9 — `mdev -d` + MODALIAS 자동 로드 + 안정 이름 (QEMU 게스트)

무엇을 확인하나: `g9_imu.ko`를 **insmod하지 않고** `/lib/modules/.../extra`에 두기만 했을 때, 장치 add → uevent의 `MODALIAS=platform:g9-imu` → mdev가 `modprobe` → 드라이버 probe → class 장치 add → mdev가 권한·이동·훅 실행까지 이어지는지.

```sh
# /g9/t3.sh (게스트, 랩 커널 그대로)
cd /g9; K=/lib/modules/$(uname -r); mkdir -p $K/extra; cp g9_imu.ko $K/extra/
depmod; echo "== modules.dep.bb (g9)"; grep -A1 g9 $K/modules.dep.bb
insmod g9_hot.ko
mdev -d; sleep 1; echo "== mdev pid: $(pidof mdev)"
echo "front SN1001" > /sys/kernel/g9_hot/add
echo "rear SN2002"  > /sys/kernel/g9_hot/add; sleep 2
echo "== lsmod"; lsmod
echo "== /dev"; ls --color=never -l /dev/sensorkit-* /dev/g9_imu* /dev/sensorkit/
echo "== read via stable name"; cat /dev/sensorkit-front /dev/sensorkit-rear
echo "== remove front"; echo 0 > /sys/kernel/g9_hot/remove; sleep 1
ls --color=never -l /dev/sensorkit-* /dev/sensorkit/
echo "== hook log"; cat /tmp/g9_hook.log
```

```text
lab initramfs: 6.18.55 up 2.72s
== modules.dep.bb (g9)
extra/g9_imu.ko platform:g9_imu

== mdev pid: 70
== lsmod
g9_imu 12288 0 - Live 0xffffd9407c25d000 (O)
g9_hot 12288 0 - Live 0xffffd9407c257000 (O)
== /dev
crw-r-----    1 0        0         510,   0 Oct 11 01:38 /dev/g9_imu0
crw-r-----    1 0        0         510,   1 Oct 11 01:38 /dev/g9_imu1
lrwxrwxrwx    1 0        0               14 Oct 11 01:38 /dev/sensorkit-front -> sensorkit/imu0
lrwxrwxrwx    1 0        0               14 Oct 11 01:38 /dev/sensorkit-rear -> sensorkit/imu1

/dev/sensorkit/:
total 0
crw-rw----    1 0        10        510,   0 Oct 11 01:38 imu0
crw-rw----    1 0        10        510,   1 Oct 11 01:38 imu1
== read via stable name
front SN1001
rear SN2002
== remove front
lrwxrwxrwx    1 0        0               14 Oct 11 01:38 /dev/sensorkit-rear -> sensorkit/imu1

/dev/sensorkit/:
total 0
crw-rw----    1 0        10        510,   1 Oct 11 01:38 imu1
== hook log
7.97 add MDEV=sensorkit/imu0 pos=front serial=
8.03 add MDEV=sensorkit/imu1 pos=rear serial=
9.69 remove MDEV=sensorkit/imu0 pos=front serial=
```

출력에서 볼 것 (주소·시각·major는 실행마다 다르다):

- **`g9_imu`가 lsmod에 있다** — 우리는 insmod하지 않았다. platform device의 add 이벤트가 `$MODALIAS=.*` 줄에 걸려 `modprobe "platform:g9-imu"`가 실행됐고, BusyBox modprobe가 `_` 정규화된 색인에서 찾았다.
- mdev가 `/dev/sensorkit/imu0`을 **새로** 만들고(0660, gid 10), 훅이 위치 이름 링크를 만들었다. `cat /dev/sensorkit-front`가 드라이버의 `read`까지 도달했다.
- 그런데 **devtmpfs가 만든 원래 노드 `/dev/g9_imu0`(0640, gid 0)도 그대로 남아 있다.** `>path`는 "옮기고 원래 이름에 링크"인데, 원래 이름이 이미 devtmpfs 노드로 존재해서 mdev가 링크를 만들지 못했다. devtmpfs + mdev 조합에서 이름 바꾸기 규칙은 **사본을 하나 더 만드는** 셈이고, 원래 노드의 권한은 바뀌지 않는다. 보안 목적이라면 원래 노드 권한도 다뤄야 한다(이름을 바꾸지 말고 권한만 주는 줄을 쓰는 게 단순하다).
- 훅의 `MDEV`는 **옮겨진 이름**(`sensorkit/imu0`)이다. 처음 버전 스크립트는 `MDEV`가 `g9_imu0`이라고 가정했다가 링크가 `sensorkit/imusensorkit/imu0`으로 깨졌다(실제로 겪은 실수, 위는 고친 뒤 결과).
- `serial=`이 **비어 있다.** 노드 이벤트(class 장치)에는 class 콜백이 넣은 `G9_POSITION`만 있고 `G9_SERIAL`은 부모 platform device의 이벤트에만 있었다(예제 4). mdev 스크립트는 그 이벤트의 환경만 본다. udev라면 `ATTRS{}`로 부모를 올라가 볼 수 있지만(§8), uevent 변수는 상속되지 않는다. 필요하면 드라이버가 class 쪽에도 넣거나, 스크립트가 `/sys/$DEVPATH/../../`를 직접 읽어야 한다.

### 7.3 예제 10 — hotplug helper 모드 (`/proc/sys/kernel/hotplug`)

무엇을 확인하나: §3.2에서 다시 빌드한 커널(`UEVENT_HELPER=y`)로, 커널이 이벤트마다 `/sbin/mdev`를 직접 실행하는 원래 방식. `/dev/mdev.seq`로 직렬화하고 `/dev/mdev.log`로 mdev의 판단 과정을 본다.

```sh
# /g9/t4.sh (게스트, -kernel /var/tmp/G9/build/arch/arm64/boot/Image)
cd /g9; K=/lib/modules/$(uname -r); mkdir -p $K/extra; cp g9_imu.ko $K/extra/; depmod
echo "== hotplug before: [$(cat /proc/sys/kernel/hotplug)]"
echo > /dev/mdev.seq                       # SEQNUM 순서대로 직렬화
touch /dev/mdev.log                        # 디버그 로그 켜기
echo /sbin/mdev > /proc/sys/kernel/hotplug
insmod g9_hot.ko
echo "front SN1001" > /sys/kernel/g9_hot/add; sleep 2
echo "== lsmod"; lsmod
echo "== /dev"; ls --color=never -l /dev/sensorkit-front /dev/sensorkit/
cat /dev/sensorkit-front; echo "mdev.seq=$(cat /dev/mdev.seq)"
echo "== hook log"; cat /tmp/g9_hook.log
echo "== /dev/mdev.log"; cat /dev/mdev.log
```

```text
lab initramfs: 6.18.55 up 0.64s
== hotplug before: []
== lsmod
g9_imu 12288 0 - Live 0xffffd2a500c46000 (O)
g9_hot 12288 0 - Live 0xffffd2a500c40000 (O)
== /dev
lrwxrwxrwx    1 0        0               14 Oct 11 01:53 /dev/sensorkit-front -> sensorkit/imu0

/dev/sensorkit/:
total 0
crw-rw----    1 0        10        510,   0 Oct 11 01:53 imu0
front SN1001
mdev.seq=2209
== hook log
0.82 add MDEV=sensorkit/imu0 pos=front serial=
== /dev/mdev.log
mdev[2203]: first seq written
mdev[2202]: 01:53:50.768698 ACTION:add SEQNUM:2202 SUBSYSTEM:module DEVNAME:(null) DEVPATH:/module/g9_hot
mdev[2203]: 01:53:50.764150 ACTION:add SEQNUM:2203 SUBSYSTEM:platform DEVNAME:(null) DEVPATH:/devices/platform/g9-imu.0
mdev[2203]: rule matched, line 4
mdev[2203]: running: modprobe "$MODALIAS"
mdev[2202]: rule matched, line -1
mdev[2202]: 01:53:50.780769 exiting
mdev[2206]: 01:53:50.799220 mdev.seq='2203', need '2206'
mdev[2208]: 01:53:50.803722 mdev.seq='2203', need '2208'
mdev[2204]: 01:53:50.806305 mdev.seq='2203', need '2204'
mdev[2207]: 01:53:50.809093 mdev.seq='2203', need '2207'
mdev[2205]: 01:53:50.812557 mdev.seq='2203', need '2205'
mdev[2204]: 01:53:50.818344 ACTION:add SEQNUM:2204 SUBSYSTEM:class DEVNAME:(null) DEVPATH:/class/g9
mdev[2204]: rule matched, line -1
mdev[2205]: 01:53:50.821326 ACTION:add SEQNUM:2205 SUBSYSTEM:g9 DEVNAME:g9_imu0 DEVPATH:/devices/platform/g9-imu.0/g9/g9_imu0
mdev[2205]: dev 510,0
mdev[2203]: 01:53:50.824498 exiting
mdev[2205]: rule matched, line 3
mdev[2205]: mknod sensorkit/imu0 (510,0) 20660 0:10
mdev[2205]: symlink: g9_imu0
mdev[2205]: running: /etc/g9_link.sh
mdev[2204]: 01:53:50.836500 exiting
mdev[2206]: 01:53:50.857966 ACTION:bind SEQNUM:2206 SUBSYSTEM:platform DEVNAME:(null) DEVPATH:/devices/platform/g9-imu.0
mdev[2206]: rule matched, line 4
mdev[2207]: 01:53:50.860787 ACTION:add SEQNUM:2207 SUBSYSTEM:drivers DEVNAME:(null) DEVPATH:/bus/platform/drivers/g9_imu
mdev[2207]: rule matched, line -1
mdev[2205]: 01:53:50.864627 exiting
mdev[2208]: 01:53:50.865271 ACTION:add SEQNUM:2208 SUBSYSTEM:module DEVNAME:(null) DEVPATH:/module/g9_imu
mdev[2208]: rule matched, line -1
mdev[2207]: 01:53:50.879205 exiting
mdev[2206]: 01:53:50.880903 exiting
mdev[2208]: 01:53:50.876187 exiting
```

출력에서 볼 것 (`mdev[N]`의 N은 PID가 아니라 그 프로세스가 맡은 **SEQNUM**이다 — mdev.c의 `open_mdev_log()`가 이렇게 이름 붙인다):

- `/proc/sys/kernel/hotplug`가 이 커널에는 **있고**(처음 값은 빈 문자열), 거기에 `/sbin/mdev`를 쓰자 이벤트 하나마다 mdev 프로세스가 하나씩 떴다. `insmod g9_hot` 한 줄 + `add` 한 줄이 이벤트 7개(2202~2208), 즉 **프로세스 7개**가 됐다.
- 모듈 로드 자체도 uevent다(`add@/module/g9_hot`, `/module/g9_imu`). `drivers`(`/bus/platform/drivers/g9_imu`), `class`(`/class/g9`) 이벤트도 있다. 대부분 "rule matched, line -1" = 어느 줄에도 안 맞아 기본 처리(노드 없음 → 아무것도 안 함).
- **직렬화**: 2204~2208 프로세스가 거의 동시에 떠서 `mdev.seq='2203', need '2206'`처럼 자기 차례를 기다렸다. 2203이 `modprobe`를 돌리는 동안 나머지가 대기한다. `/dev/mdev.seq`가 없었다면 2205(노드)가 2203(모듈 로드)보다 먼저 끝날 수도 있다.
- 2203(platform add)은 4번 줄(`$MODALIAS=.*`)에 맞아 `modprobe "$MODALIAS"` → g9_imu가 로드되고 → 2205(노드 이벤트)는 3번 줄(`g9_imu([0-9]+)`)에 맞아 `mknod sensorkit/imu0 (510,0) 20660 0:10`(20660 = S_IFCHR | 0660), `symlink: g9_imu0`(devtmpfs 노드가 이미 있어 실제로는 실패, 예제 9와 같은 현상), 훅 실행.
- 2206(**bind**)도 4번 줄에 맞았다. bind 이벤트에도 MODALIAS가 있으므로(예제 4) **modprobe가 한 번 더** 불렸다 — 이미 로드돼 있어 무해하지만 낭비다. udev의 `80-drivers.rules`가 `ACTION!="add"`를 먼저 걸러내는 이유가 이것이다. mdev.conf에서도 `ACTION=add;$MODALIAS=.*`처럼 조건을 붙이면 줄일 수 있다(이 변형은 실행하지 않았다).

---

## 8. udev rules — systemd-udevd에서 정책 쓰기

### 8.1 예제 11 — `udevadm monitor`: 커널 이벤트와 udev 이벤트 (호스트)

무엇을 확인하나: 같은 장치 추가가 KERNEL(group 1)과 UDEV(group 2)로 두 번 보이는 것, 그리고 udev가 무엇을 덧붙이는지. 호스트 커널에 이미 있는 `vcan` 모듈로 가상 CAN 인터페이스를 만들었다 지웠다.

```sh
cd /var/tmp/G9
(sudo timeout 5 udevadm monitor --kernel --udev --property --subsystem-match=net > mon_net.txt 2>&1 &)
sleep 1; sudo ip link add g9vcan0 type vcan; sleep 1; sudo ip link del g9vcan0; sleep 4
cat mon_net.txt
```

```text
monitor will print the received events for:
UDEV - the event which udev sends out after rule processing
KERNEL - the kernel uevent

KERNEL[31289.455275] add      /devices/virtual/net/g9vcan0 (net)
ACTION=add
DEVPATH=/devices/virtual/net/g9vcan0
SUBSYSTEM=net
INTERFACE=g9vcan0
IFINDEX=3
SEQNUM=4506

UDEV  [31289.537090] add      /devices/virtual/net/g9vcan0 (net)
ACTION=add
DEVPATH=/devices/virtual/net/g9vcan0
SUBSYSTEM=net
INTERFACE=g9vcan0
IFINDEX=3
SEQNUM=4506
USEC_INITIALIZED=31289454965
ID_NET_DRIVER=vcan
ID_MM_CANDIDATE=1
ID_NET_LINK_FILE=/usr/lib/systemd/network/99-default.link
ID_NET_NAME=g9vcan0
SYSTEMD_ALIAS=/sys/subsystem/net/devices/g9vcan0
TAGS=:systemd:
CURRENT_TAGS=:systemd:

KERNEL[31290.524544] remove   /devices/virtual/net/g9vcan0 (net)
...(remove의 KERNEL/UDEV 쌍, SEQNUM=4511 — 생략)
```

출력에서 볼 것:

- 같은 SEQNUM 4506이 KERNEL로 한 번, rule 처리 뒤 UDEV로 한 번. 시각 차이 약 82 ms(31289.455 → 31289.537) — 이 VM에서 udevd가 net 장치 한 개의 규칙(`.link` 파일 적용, 이름 정책, hwdb 조회)을 처리한 시간이다.
- UDEV 이벤트에만 있는 키: `ID_NET_DRIVER`, `ID_NET_LINK_FILE`(systemd-networkd `.link` 파일로 이름·MAC 정책), `TAGS=:systemd:`(systemd가 이 장치로 `.device` 유닛을 만든다), `USEC_INITIALIZED`(udev DB에 처음 기록된 시각). 커널 이벤트에는 없다.
- `INTERFACE`, `IFINDEX`는 net class의 `dev_uevent`(`netdev_uevent()`)가 넣은 것이다 — 우리 `g9_class_uevent()`와 같은 자리.

### 8.2 예제 12 — `udevadm info -a`: 부모를 따라 올라가며 속성 보기 (호스트)

무엇을 확인하나: rule에 쓸 수 있는 키 이름과 값을 실제 장치(가상 디스크 `/dev/vda`)에서 뽑기. 출력이 길어 `power/`, `queue/` 같은 일반 속성 줄을 grep으로 걸렀고, 그 뒤에도 남은 긴 줄 몇 개(virtio `features` 비트열, `msi_irqs/*`, `remove`/`rescan`, `devspec`)는 손으로 지웠다.

```sh
udevadm info -a -n /dev/vda | grep -vE "^\s+ATTRS?\{(power|queue|integrity|mq|trace|events|ext_range|alignment_offset|discard_alignment|diskseq|hidden|inflight|range|stat|capability|partscan|ro|removable|uevent|bdi|holders|slaves|subsystem_device|subsystem_vendor|class|config|consistent_dma|d3cold|dma_mask|enable|irq|local_cpu|msi_bus|numa|revision|broken|ari|current_link|max_link|driver_override|link|reset_method|power_state|secondary|subordinate|resource)" | head -70
```

```text
Udevadm info starts with the device specified by the devpath and then
walks up the chain of parent devices. It prints for every device
found, all possible attributes in the udev rules key format.
A rule to match, can be composed by the attributes of the device
and the attributes from one single parent device.

  looking at device '/devices/pci0000:00/0000:00:06.0/virtio2/block/vda':
    KERNEL=="vda"
    SUBSYSTEM=="block"
    DRIVER==""
    ATTR{cache_type}=="write back"
    ATTR{serial}==""
    ATTR{size}=="209715200"

  looking at parent device '/devices/pci0000:00/0000:00:06.0/virtio2':
    KERNELS=="virtio2"
    SUBSYSTEMS=="virtio"
    DRIVERS=="virtio_blk"
    ATTRS{device}=="0x0002"
    ATTRS{vendor}=="0x1af4"

  looking at parent device '/devices/pci0000:00/0000:00:06.0':
    KERNELS=="0000:00:06.0"
    SUBSYSTEMS=="pci"
    DRIVERS=="virtio-pci"
    ATTRS{device}=="0x1042"
    ATTRS{vendor}=="0x1af4"

  looking at parent device '/devices/pci0000:00':
    KERNELS=="pci0000:00"
    SUBSYSTEMS==""
    DRIVERS==""
    ATTRS{available_secure_streams}=="256"
    ATTRS{waiting_for_supplier}=="0"
```

출력에서 볼 것: 이벤트 장치(`vda`)의 키는 `KERNEL`/`SUBSYSTEM`/`DRIVER`/`ATTR{}`(단수), 부모의 키는 `KERNELS`/`SUBSYSTEMS`/`DRIVERS`/`ATTRS{}`(복수형 S). 머리말의 마지막 문장이 핵심이다: **"한 rule은 장치 자신의 속성과 부모 장치 하나의 속성으로 구성할 수 있다."** `SUBSYSTEMS=="virtio"`와 `ATTRS{vendor}=="0x1af4"`를 한 줄에 쓰면 둘 다 virtio2에서 맞아야 한다(pci 부모에도 vendor 0x1af4가 있지만 SUBSYSTEMS가 pci라서 같은 부모가 아니다).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 420">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="18.0" font-size="12" text-anchor="start">이벤트 장치에서 부모 쪽으로 올라간다 (udevadm info -a)</text><rect x="10" y="30" width="300" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160.0" y="52.2" font-size="12" text-anchor="middle">block/vda  (이벤트 장치)</text><text x="160.0" y="68.2" font-size="12" text-anchor="middle">KERNEL · SUBSYSTEM · ATTR{size}</text><rect x="10" y="104" width="300" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="160.0" y="126.2" font-size="12" text-anchor="middle">virtio2</text><text x="160.0" y="142.2" font-size="12" text-anchor="middle">SUBSYSTEMS=virtio · DRIVERS=virtio_blk</text><line x1="160" y1="104" x2="160" y2="82" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/><rect x="10" y="178" width="300" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="160.0" y="200.2" font-size="12" text-anchor="middle">0000:00:06.0</text><text x="160.0" y="216.2" font-size="12" text-anchor="middle">SUBSYSTEMS=pci · ATTRS{vendor}=0x1af4</text><line x1="160" y1="178" x2="160" y2="156" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/><rect x="10" y="252" width="300" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="160.0" y="274.2" font-size="12" text-anchor="middle">pci0000:00</text><text x="160.0" y="290.2" font-size="12" text-anchor="middle">(루트)</text><line x1="160" y1="252" x2="160" y2="230" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/><text x="170.0" y="98.0" font-size="12" text-anchor="start">parent</text><text x="170.0" y="172.0" font-size="12" text-anchor="start">parent</text><text x="170.0" y="246.0" font-size="12" text-anchor="start">parent</text>
<rect x="350" y="30" width="320" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="510.0" y="52.2" font-size="12" text-anchor="middle">KERNEL · SUBSYSTEM · DRIVER · ATTR{}</text><text x="510.0" y="68.2" font-size="12" text-anchor="middle">→ 이벤트 장치 자신만 본다</text><rect x="350" y="104" width="320" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510.0" y="127.2" font-size="12" text-anchor="middle">KERNELS · SUBSYSTEMS · DRIVERS · ATTRS{}</text><text x="510.0" y="143.2" font-size="12" text-anchor="middle">→ 자신 또는 부모 하나에서</text><text x="510.0" y="159.2" font-size="12" text-anchor="middle">한 규칙 줄의 이 키들은 같은 부모에서 맞아야</text><rect x="350" y="196" width="320" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="510.0" y="218.2" font-size="12" text-anchor="middle">ENV{} · TAG · ACTION</text><text x="510.0" y="234.2" font-size="12" text-anchor="middle">→ 이벤트 속성 (부모 무관)</text>
<line x1="310" y1="56" x2="350" y2="56" stroke="currentColor" stroke-width="1"/><line x1="310" y1="130" x2="350" y2="130" stroke="currentColor" stroke-width="1"/><line x1="310" y1="204" x2="350" y2="150" stroke="currentColor" stroke-width="1"/><rect x="10" y="330" width="660" height="76" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="340.0" y="356.2" font-size="12" text-anchor="middle">rules.d 파일은 이름순(/usr/lib, /etc, /run 합쳐서), 줄 단위로 모두 평가</text><text x="340.0" y="372.2" font-size="12" text-anchor="middle">매칭 키(==, !=)가 전부 참인 줄의 대입(=, +=, -=, :=)을 적용 — 첫 매칭에서 멈추지 않는다</text><text x="340.0" y="388.2" font-size="12" text-anchor="middle">RUN은 모은 뒤 맨 끝에 실행 · := 는 이후 변경 금지</text>
</svg>
```
그림 4 — udev rule 평가. 단수 키는 이벤트 장치만, 복수 키는 자신 또는 부모 하나에서 맞아야 한다. 규칙 파일은 이름순으로 모두 평가된다(mdev의 "첫 매칭에서 멈춤"과 반대).

### 8.3 rule 문법 한 장 정리

한 줄 = 쉼표로 구분한 `KEY연산자"값"` 목록. 줄 끝 `\`로 이어 쓸 수 있다. 파일은 `/usr/lib/udev/rules.d`(패키지), `/etc/udev/rules.d`(관리자, 같은 이름이면 이쪽이 이김), `/run/udev/rules.d`(런타임)에서 모아 **파일 이름순**으로 처리한다. 그래서 숫자 접두어가 순서다(`60-persistent-storage.rules`가 `99-*.rules`보다 먼저).

| 연산자 | 종류 | 뜻 |
|---|---|---|
| `==` / `!=` | 매칭 | 같으면 / 다르면 (glob `*`, `?`, `[...]`, 그리고 세로 막대로 구분한 여러 값 중 하나 — 예제 13의 ACTION 줄) |
| `=` | 대입 | 값 설정 (목록이면 덮어씀) |
| `+=` | 대입 | 목록에 추가 (`SYMLINK`, `RUN`, `TAG`) |
| `-=` | 대입 | 목록에서 제거 |
| `:=` | 대입 | 최종 값으로 고정 — 뒤 규칙이 못 바꿈 |

| 키 | 매칭/대입 | 설명 |
|---|---|---|
| `ACTION`, `KERNEL`, `SUBSYSTEM`, `DRIVER`, `DEVPATH` | 매칭 | 이벤트 장치 자신 |
| `ATTR{file}` | 매칭(대입도 가능: sysfs에 씀) | 이벤트 장치의 sysfs 속성. `ATTR{loop/backing_file}`처럼 하위 경로도 됨 |
| `KERNELS`, `SUBSYSTEMS`, `DRIVERS`, `ATTRS{file}` | 매칭 | 자신 또는 **부모 하나** |
| `ENV{key}` | 매칭·대입 | uevent 변수와 udev 속성 (예: `ENV{G9_POSITION}`, `ENV{ID_SERIAL}`) |
| `TAG` | 매칭·대입 | `TAG+="systemd"`면 systemd가 `.device` 유닛 생성, `TAG+="uaccess"`면 로그인 사용자에게 ACL |
| `SYMLINK` | 대입 | `/dev` 아래 심볼릭 링크 이름 (`+=`로) |
| `OWNER`, `GROUP`, `MODE` | 대입 | 노드 소유자·그룹·권한 |
| `NAME` | 대입 | **네트워크 인터페이스 이름만** (노드 이름은 못 바꿈) |
| `RUN{program}`, `RUN{builtin}` | 대입 | 규칙 처리 **끝난 뒤** 실행. 짧은 작업만(udevd가 타임아웃으로 죽인다, 기본 180초). 오래 걸리는 일은 `TAG+="systemd", ENV{SYSTEMD_WANTS}+="x.service"` |
| `PROGRAM`, `IMPORT{program}` | 매칭·대입 | 규칙 **도중** 실행해 결과로 매칭·속성 가져오기 |
| `GOTO`, `LABEL` | 흐름 | 같은 파일 안에서 건너뛰기 |

치환: `%k`(커널 이름), `%n`(번호), `%p`(DEVPATH), `%E{KEY}` 또는 `$env{KEY}`(속성), `%s{attr}` 또는 `$attr{attr}`(sysfs), `%%`.

### 8.4 예제 13 — rule을 쓰고, 문법 검사하고, 설치 없이 `udevadm test`로 미리 돌리기 (호스트)

무엇을 확인하나: loop 장치에 g9 이미지 파일을 물렸을 때만 `/dev/g9/sensorlog` 링크·`adm` 그룹·0664 권한·로그 실행이 적용되는 rule. 설치하기 전에 임시 디렉터리에서 `udevadm verify`와 `udevadm test -D`(`--extra-rules-dir`, 이 VM의 systemd 259 `udevadm test --help`에 있는 옵션)로 검증한다.

```text
# /var/tmp/G9/rules/99-g9-loop.rules
# G9 demo: 백킹 파일 이름이 g9_*.img인 loop 장치에 안정 이름 + 권한 + 로그
ACTION!="add|change", GOTO="g9_end"
SUBSYSTEM!="block", GOTO="g9_end"
KERNEL=="loop[0-9]*", ATTR{loop/backing_file}=="*/g9_*.img", \
  SYMLINK+="g9/sensorlog", MODE="0664", GROUP="adm", ENV{G9_ROLE}="sensorlog", \
  RUN+="/usr/bin/logger -t g9-udev %E{ACTION} %k backing=%s{loop/backing_file}"
LABEL="g9_end"
```

```sh
udevadm verify /var/tmp/G9/rules/99-g9-loop.rules; echo rc=$?
cd /var/tmp/G9 && truncate -s 16M g9_sensorlog.img
L=$(sudo losetup -f --show /var/tmp/G9/g9_sensorlog.img); echo LOOP=$L
cat /sys/block/${L#/dev/}/loop/backing_file; ls -l $L
sudo udevadm test -a change -D /var/tmp/G9/rules /sys/block/${L#/dev/} 2>&1 | tail -40
```

(아래 출력에서 `ID_LOOP_BACKING_*` 몇 줄, `ID_PROCESSING`, `UDEV_DATABASE_VERSION`, `Inotify watch` 항목은 지면 때문에 지웠다.)

```text
1 udev rules files have been checked.
  Success: 1
  Fail:    0
rc=0
LOOP=/dev/loop0
/var/tmp/G9/g9_sensorlog.img
brw-rw---- 1 root disk 7, 0 Oct 10 09:57 /dev/loop0
Device type:
  disk
Device node:
  /dev/loop0 (block 7:0)
Device node owner group:
  adm (gid=4)
Device node permission:
  0664
Device node symlinks: (priority=0)
  /dev/disk/by-diskseq/13
  /dev/disk/by-loop-inode/253:1-3983919
  /dev/disk/by-loop-ref/\x2fvar\x2ftmp\x2fG9\x2fg9_sensorlog.img
  /dev/g9/sensorlog
Tags:
  systemd
Properties:
  ACTION=change
  CURRENT_TAGS=:systemd:
  DEVLINKS=/dev/disk/by-diskseq/13 /dev/disk/by-loop-inode/253:1-3983919 /dev/disk/by-loop-ref/\x2fvar\x2ftmp\x2fG9\x2fg9_sensorlog.img /dev/g9/sensorlog
  DEVNAME=/dev/loop0
  DEVPATH=/devices/virtual/block/loop0
  DEVTYPE=disk
  DISKSEQ=13
  G9_ROLE=sensorlog
  ID_BLOCK_SUBSYSTEM=loop
  ID_LOOP_BACKING_FILENAME=/var/tmp/G9/g9_sensorlog.img
  MAJOR=7
  MINOR=0
  SUBSYSTEM=block
  TAGS=:systemd:
  USEC_INITIALIZED=1701053
Queued commands:
  RUN{program} : /usr/bin/logger -t g9-udev change loop0 backing=/var/tmp/G9/g9_sensorlog.img
```

`-v`를 붙이면 줄 단위 판정이 나온다(우리 파일 부분만 grep):

```sh
sudo udevadm test -v -a change -D /var/tmp/G9/rules /sys/block/loop0 2>&1 | grep -n "99-g9" | head -20
```

```text
323:Reading rules file: /var/tmp/G9/rules/99-g9-loop.rules
1205:loop0: /var/tmp/G9/rules/99-g9-loop.rules:2 ACTION!="add|change": String "change" matches one of "add", "change": FAIL
1206:loop0: /var/tmp/G9/rules/99-g9-loop.rules:3 SUBSYSTEM!="block": String "block" matches "block": FAIL
1207:loop0: /var/tmp/G9/rules/99-g9-loop.rules:4 KERNEL=="loop[0-9]*": String "loop0" matches "loop[0-9]*": PASS
1208:loop0: /var/tmp/G9/rules/99-g9-loop.rules:4 ATTR{loop/backing_file}=="*/g9_*.img": String "/var/tmp/G9/g9_sensorlog.img" matches "*/g9_*.img": PASS
1209:loop0: /var/tmp/G9/rules/99-g9-loop.rules:4 GROUP="adm": Set group ID: 4
1210:loop0: /var/tmp/G9/rules/99-g9-loop.rules:4 MODE="0664": Set mode: 0664
1211:loop0: /var/tmp/G9/rules/99-g9-loop.rules:4 ENV{G9_ROLE}="sensorlog": Set property "G9_ROLE=sensorlog".
1212:loop0: /var/tmp/G9/rules/99-g9-loop.rules:4 SYMLINK+="g9/sensorlog": Added device node symlink "g9/sensorlog".
1213:loop0: /var/tmp/G9/rules/99-g9-loop.rules:4 RUN+="/usr/bin/logger -t g9-udev %E{ACTION} %k backing=%s{loop/backing_file}": Format substitution: "/usr/bin/logger -t g9-udev %E{ACTION} %k backing=%s{loop/backing_file}" -> "/usr/bin/logger -t g9-udev change loop0 backing=/var/tmp/G9/g9_sensorlog.img"
1214:loop0: /var/tmp/G9/rules/99-g9-loop.rules:4 RUN+="/usr/bin/logger -t g9-udev %E{ACTION} %k backing=%s{loop/backing_file}": Set command: /usr/bin/logger -t g9-udev change loop0 backing=/var/tmp/G9/g9_sensorlog.img
```

출력에서 볼 것:

- `udevadm test`는 **dry-run**이다. 결과(그룹 adm, 0664, 링크, 실행할 명령)를 계산해 보여 주지만 노드를 바꾸지도, RUN을 실행하지도 않는다("Queued commands"). 직전 `ls -l`의 `/dev/loop0`은 여전히 `root disk 0660`이다.
- `!=` 줄의 "FAIL"은 "건너뛰기 조건이 거짓" = 계속 진행한다는 뜻이다. 헷갈리기 쉽다.
- 우리 링크 외에도 `/dev/disk/by-loop-ref/...` 같은 링크가 이미 있다 — 배포판의 `60-persistent-storage.rules`가 만든 **안정 이름**이다. 우리 rule은 그 위에 하나를 더 얹는다.
- `ATTR{loop/backing_file}`처럼 하위 디렉터리 속성도 매칭된다. loop 장치는 `losetup` 시점에 `change` 이벤트를 보내므로(백킹 파일이 그때 생긴다) `ACTION`에 change를 포함했다.

### 8.5 예제 14 — 실제로 설치하고, 적용을 확인하고, 지우기 (호스트)

```sh
L=$(cat /var/tmp/G9/loopdev)       # /dev/loop0
ls -l /dev/g9; ls -l $L
sudo install -m 644 /var/tmp/G9/rules/99-g9-loop.rules /etc/udev/rules.d/99-g9-loop.rules
sudo udevadm control --reload
sudo udevadm trigger --action=change --settle $L
ls -l $L /dev/g9/
udevadm info -q property -n /dev/g9/sensorlog | grep -E "G9_ROLE|DEVNAME"
journalctl -t g9-udev --no-pager -o short-monotonic | tail -3
echo "== detach + reattach (real hotplug, no trigger)"
sudo losetup -d $L; sleep 1; ls -l /dev/g9
L2=$(sudo losetup -f --show /var/tmp/G9/g9_sensorlog.img); sudo udevadm settle
ls -l $L2 /dev/g9/; journalctl -t g9-udev --no-pager -o short-monotonic | tail -3
```

```text
ls: cannot access '/dev/g9': No such file or directory
brw-rw---- 1 root disk 7, 0 Oct 10 18:39 /dev/loop0
brw-rw-r-- 1 root adm 7, 0 Oct 10 18:39 /dev/loop0

/dev/g9/:
total 0
lrwxrwxrwx 1 root root 8 Oct 10 18:39 sensorlog -> ../loop0
DEVNAME=/dev/loop0
G9_ROLE=sensorlog
[31325.193711] lima-elinux g9-udev[807206]: change loop0 backing=/var/tmp/G9/g9_sensorlog.img
== detach + reattach (real hotplug, no trigger)
ls: cannot access '/dev/g9': No such file or directory
brw-rw-r-- 1 root adm 7, 0 Oct 10 18:39 /dev/loop0

/dev/g9/:
total 0
lrwxrwxrwx 1 root root 8 Oct 10 18:39 sensorlog -> ../loop0
[31325.193711] lima-elinux g9-udev[807206]: change loop0 backing=/var/tmp/G9/g9_sensorlog.img
[31326.263075] lima-elinux g9-udev[807266]: change loop0 backing=/var/tmp/G9/g9_sensorlog.img
[31326.274079] lima-elinux g9-udev[807269]: change loop0 backing=/var/tmp/G9/g9_sensorlog.img
```

(원래 출력은 `set -x` 추적 줄이 섞여 있어 그 줄들만 지웠다.)

출력에서 볼 것:

- rule 파일을 넣는 것만으로는 아무 일도 없다. `udevadm control --reload`(규칙 다시 읽기) + **이벤트**(여기서는 `trigger --action=change`)가 있어야 적용된다. 이미 붙어 있는 장치에 새 rule을 적용하는 표준 절차다.
- 적용 후 `root adm 0664`, `/dev/g9/sensorlog -> ../loop0`, udev DB에 `G9_ROLE=sensorlog`, journal에 logger 줄.
- detach하면 링크가 사라지고, 다시 attach하면 trigger 없이 실제 핫플러그 이벤트로 다시 생긴다. 다시 붙일 때 logger가 **두 번** 실행됐다. rule 없이 커널 이벤트만 따로 보면 이유가 보인다:

```sh
(sudo timeout 4 udevadm monitor --kernel --subsystem-match=block > mon_loop.txt 2>&1 &); sleep 1
L=$(sudo losetup -f --show /var/tmp/G9/g9_sensorlog.img); sleep 1; sudo losetup -d $L; sleep 3; cat mon_loop.txt
```

```text
monitor will print the received events for:
KERNEL - the kernel uevent

KERNEL[32044.253658] change   /devices/virtual/block/loop0 (block)
KERNEL[32044.259914] change   /devices/virtual/block/loop0 (block)
KERNEL[32045.270520] change   /devices/virtual/block/loop0 (block)
KERNEL[32045.270569] change   /devices/virtual/block/loop0 (block)
```

attach 한 번에 커널이 `change`를 **두 번**(6 ms 간격), detach에도 두 번 보낸다. rule이 `change`에 반응하므로 RUN도 두 번 돈다. RUN 스크립트는 **같은 이벤트가 여러 번 와도 안전하게**(idempotent) 써야 한다.

#### 예제 15 — systemd device 유닛 확인 후 rule 제거 (호스트)

```sh
L=$(sudo losetup -f --show /var/tmp/G9/g9_sensorlog.img); sudo udevadm settle
systemctl list-units --type=device --all --no-pager --plain | grep -E "g9|^UNIT"
systemctl show -p SysFSPath,Following dev-g9-sensorlog.device
echo "== remove rule"
sudo rm -f /etc/udev/rules.d/99-g9-loop.rules; sudo udevadm control --reload
sudo udevadm trigger --action=change --settle $L; ls -l $L; ls /dev/g9
sudo losetup -d $L; ls -l /dev/loop0; ls /etc/udev/rules.d/
```

```text
UNIT                                                                                                                   LOAD   ACTIVE   SUB     DESCRIPTION
dev-disk-by\x2dloop\x2dref-\x5cx2fvar\x5cx2ftmp\x5cx2fG9\x5cx2fg9_sensorlog.img.device                                 loaded active   plugged /dev/disk/by-loop-ref/\x2fvar\x2ftmp\x2fG9\x2fg9_sensorlog.img
dev-g9-sensorlog.device                                                                                                loaded active   plugged /dev/g9/sensorlog
Following=sys-devices-virtual-block-loop0.device
SysFSPath=/sys/devices/virtual/block/loop0
== remove rule
brw-rw-r-- 1 root adm 7, 0 Oct 10 18:40 /dev/loop0
ls: cannot access '/dev/g9': No such file or directory
brw-rw---- 1 root disk 7, 0 Oct 10 18:40 /dev/loop0
```

(마지막 `ls /etc/udev/rules.d/`는 빈 디렉터리라 출력이 없다.)

출력에서 볼 것:

- `TAGS=:systemd:`인 장치는 systemd에 `.device` 유닛으로 나타나고, **udev가 만든 심볼릭 링크마다** 유닛 이름이 생긴다(`dev-g9-sensorlog.device`). 그래서 서비스 파일에 `BindsTo=dev-g9-sensorlog.device` + `After=`를 쓰면 "이 센서가 있을 때만 도는 서비스"가 된다. 센서 키트에서 카메라 데몬을 카메라 장치에 묶는 표준 방법이다.
- rule을 지우고 change를 다시 트리거하자 **링크는 사라졌지만 권한(`root adm 0664`)은 그대로 남았다.** 실제로 관찰된 동작이다 — 지운 rule이 바꿔 놓은 권한을 기본 rule이 그 change 이벤트에서 되돌리지 않았다. 장치를 떼자(`losetup -d`) 그때 `root disk 0660`으로 돌아왔다. 교훈: rule을 지운 뒤 "깨끗한 상태"는 장치를 다시 붙이거나 재부팅해서 확인한다. (왜 change에서 되돌리지 않았는지 udevd 내부 원인은 이 노트에서 추적하지 않았다.)

### 8.6 여러 개의 똑같은 센서·카메라에 안정된 이름 주기

같은 IMU 두 개, 같은 USB 카메라 두 개는 probe 순서에 따라 `/dev/video0`/`video2`가 부팅마다 바뀔 수 있다. 앱이 `video0`을 하드코딩하면 앞·뒤 카메라가 뒤바뀐다. 해결은 **바뀌지 않는 속성으로 링크**를 거는 것이다.

| 무엇으로 | rule 예 (이 노트에서 실행하지 않은 예시) | 언제 |
|---|---|---|
| 일련번호 | `SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", ATTRS{serial}=="FT9ABC12", SYMLINK+="gnss0"` | USB 장치에 serial이 있을 때 |
| 물리 포트 경로 | `SUBSYSTEM=="video4linux", ENV{ID_PATH}=="platform-xhci-hcd.1-usb-0:1:1.0", SYMLINK+="cam-front"` | serial이 없거나 같을 때. `path_id` builtin이 `ID_PATH`를 만든다 (`/dev/v4l/by-path/`가 이것) |
| DT 노드 | `SUBSYSTEM=="iio", KERNELS=="2-0068", ATTRS{name}=="bmi160", SYMLINK+="sensorkit-front"` | SoC 내장·I2C/SPI 센서. 버스 번호+주소는 DT가 고정하므로 안정적. |
| 드라이버가 알려 준 값 | `ENV{G9_POSITION}=="front", SYMLINK+="sensorkit-front"` | 우리 g9처럼 드라이버가 uevent로 위치를 알려 줄 때 |

배포판이 이미 주는 안정 이름도 먼저 확인하자: `/dev/disk/by-id`, `/dev/disk/by-path`, `/dev/serial/by-id`, `/dev/serial/by-path`, `/dev/v4l/by-id`, `/dev/v4l/by-path`, `/dev/input/by-path`. 이것들은 `60-persistent-*.rules`가 만든다.

---

## 9. 얼마나 걸리나 — 장치 추가부터 /dev 경로가 보일 때까지

#### 예제 16 — `g9_lat.c`: 추가 → 경로 출현 지연 측정

무엇을 확인하는 코드인가: `/sys/kernel/g9_hot/add`에 쓰기 직전부터 지정한 `/dev` 경로가 `lstat`으로 보일 때까지 시간을 N번 재서 분포를 낸다. 같은 프로그램으로 devtmpfs 노드(`/dev/g9_imu0`), mdev가 만든 링크(`/dev/sensorkit-front`)를 비교한다.

```c
// g9_lat: "장치 추가 → /dev 경로가 보일 때까지" 지연을 N번 잰다 (devtmpfs / mdev 비교용)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#define DIE(m) do { perror(m); exit(1); } while (0)

static double now_us(void)
{
	struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
static void put(const char *path, const char *s)
{
	int fd = open(path, O_WRONLY);
	if (fd < 0 || write(fd, s, strlen(s)) < 0) DIE(path);
	close(fd);
}
static double wait_for(const char *path, int want)	/* want=1: 생길 때까지, 0: 없어질 때까지 */
{
	double t0 = now_us();
	struct stat st;
	while ((lstat(path, &st) == 0) != want) {
		if (now_us() - t0 > 5e6) { fprintf(stderr, "timeout %s\n", path); exit(1); }
		usleep(50);
	}
	return now_us() - t0;
}
static int cmp(const void *a, const void *b) { double x = *(double *)a, y = *(double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s <dev path> <N>\n", argv[0]); return 1; }
	int n = atoi(argv[2]);
	double v[256];
	if (n < 1 || n > 256) return 1;
	for (int i = 0; i < n; i++) {
		double t0 = now_us();
		put("/sys/kernel/g9_hot/add", "front SN1001");
		wait_for(argv[1], 1);
		v[i] = now_us() - t0;
		put("/sys/kernel/g9_hot/remove", "0");
		wait_for(argv[1], 0);
	}
	qsort(v, n, sizeof v[0], cmp);
	printf("%s n=%d min=%.0f median=%.0f p90=%.0f max=%.0f us\n",
	       argv[1], n, v[0], v[n / 2], v[n * 9 / 10], v[n - 1]);
	return 0;
}
```

QEMU 게스트(위에서 다시 빌드한 helper 지원 커널, `-m 512M`, CPU 1개, TCG)에서 세 가지 경로를 40번씩 쟀다. `g9_imu`는 미리 insmod해서 모듈 로드 시간은 빠져 있다.

```sh
# /g9/t5.sh
cd /g9; insmod g9_hot.ko; insmod g9_imu.ko
./g9_lat /dev/g9_imu0 40
mdev -d; sleep 1; ./g9_lat /dev/sensorkit-front 40; killall mdev; sleep 1
echo /sbin/mdev > /proc/sys/kernel/hotplug; ./g9_lat /dev/sensorkit-front 40
```

```text
lab initramfs: 6.18.55 up 0.64s
/dev/g9_imu0 n=40 min=984 median=1001 p90=1191 max=2517 us
/dev/sensorkit-front n=40 min=53630 median=65495 p90=75444 max=96469 us
/dev/sensorkit-front n=40 min=49095 median=57426 p90=74364 max=85445 us
reboot: Power down
lab initramfs: 6.18.55 up 0.65s
/dev/g9_imu0 n=40 min=989 median=1067 p90=1309 max=2578 us
/dev/sensorkit-front n=40 min=58813 median=72521 p90=83646 max=97286 us
/dev/sensorkit-front n=40 min=39801 median=57972 p90=65944 max=76583 us
```

두 줄씩 세 묶음이 각각 devtmpfs / `mdev -d` / mdev helper다(두 번 부팅해서 두 번 쟀다).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 260">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10.0" y="20.0" font-size="12" text-anchor="start">추가 → 경로 출현 지연 (ms, 40회, 막대=중앙값, 선=p90) — QEMU TCG 게스트</text><line x1="230.0" y1="34" x2="230.0" y2="196" stroke="#888" stroke-width="1" stroke-dasharray="4 4"/><text x="230.0" y="214.0" font-size="12" text-anchor="middle">0</text><line x1="312.5" y1="34" x2="312.5" y2="196" stroke="#888" stroke-width="1" stroke-dasharray="4 4"/><text x="312.5" y="214.0" font-size="12" text-anchor="middle">20</text><line x1="395.0" y1="34" x2="395.0" y2="196" stroke="#888" stroke-width="1" stroke-dasharray="4 4"/><text x="395.0" y="214.0" font-size="12" text-anchor="middle">40</text><line x1="477.5" y1="34" x2="477.5" y2="196" stroke="#888" stroke-width="1" stroke-dasharray="4 4"/><text x="477.5" y="214.0" font-size="12" text-anchor="middle">60</text>
<line x1="560.0" y1="34" x2="560.0" y2="196" stroke="#888" stroke-width="1" stroke-dasharray="4 4"/><text x="560.0" y="214.0" font-size="12" text-anchor="middle">80</text><line x1="230" y1="196" x2="560" y2="196" stroke="currentColor" stroke-width="1"/><text x="222.0" y="71.0" font-size="12" text-anchor="end">devtmpfs 노드 (/dev/g9_imu0)</text><rect x="230" y="50" width="4.1" height="30" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><line x1="234.129125" y1="65" x2="234.912875" y2="65" stroke="currentColor" stroke-width="1.5"/><line x1="234.912875" y1="57" x2="234.912875" y2="73" stroke="currentColor" stroke-width="1.5"/><text x="242.9" y="70.0" font-size="12" text-anchor="start">1.0 / 1.2</text><text x="222.0" y="119.0" font-size="12" text-anchor="end">mdev -d 데몬 + 훅 링크</text><rect x="230" y="98" width="270.2" height="30" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<line x1="500.166875" y1="113" x2="541.2065" y2="113" stroke="currentColor" stroke-width="1.5"/><line x1="541.2065" y1="105" x2="541.2065" y2="121" stroke="currentColor" stroke-width="1.5"/><text x="549.2" y="118.0" font-size="12" text-anchor="start">65.5 / 75.4</text><text x="222.0" y="167.0" font-size="12" text-anchor="end">mdev helper + 훅 링크</text><rect x="230" y="146" width="236.9" height="30" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/><line x1="466.88225" y1="161" x2="536.7515000000001" y2="161" stroke="currentColor" stroke-width="1.5"/><line x1="536.7515000000001" y1="153" x2="536.7515000000001" y2="169" stroke="currentColor" stroke-width="1.5"/><text x="544.8" y="166.0" font-size="12" text-anchor="start">57.4 / 74.4</text><text x="395.0" y="234.0" font-size="12" text-anchor="middle">ms</text><text x="10.0" y="252.0" font-size="12" text-anchor="start">호스트 udev 참고: loop 링크 출현 중앙값 8.5 ms (네이티브 VM, 다른 환경이라 같은 축에 두지 않음)</text>
</svg>
```
그림 5 — 첫 번째 실행의 중앙값(막대)과 p90(선). devtmpfs 노드는 `write()`가 돌아올 때 이미 있고(약 1 ms = sysfs write + probe + `device_create` 전체), mdev가 만드는 링크는 수십 ms 뒤에 나타난다.

출력에서 볼 것:

- **devtmpfs 노드는 사실상 동기**다. `devtmpfs_create_node()`가 `device_add()` 안에서 kdevtmpfs 스레드의 완료를 기다리므로, `echo > add`가 돌아오면 노드가 이미 있다. 1 ms는 에뮬레이션 CPU에서 probe 경로 전체를 포함한 시간이다.
- mdev 경로는 50~70 ms. 대부분은 **프로세스 생성과 셸 실행**(mdev가 `system()`으로 `/bin/sh -c /etc/g9_link.sh`를 띄우고, 그 셸이 `ln`·`cut`을 또 fork)이다. TCG 에뮬레이션에서 fork·exec이 특히 비싸서 실제 Cortex-A 보드보다 크게 나온다 — **절대값이 아니라 "노드 vs 정책 적용"의 차이**를 보라.
- 이 측정에서는 helper가 데몬보다 약간 빨랐다. 데몬은 한 프로세스가 이벤트를 **차례로** 처리하지만, helper는(이 측정에서는 `/dev/mdev.seq`를 만들지 않아) 이벤트마다 별도 프로세스가 **병렬로** 뜨므로 노드 이벤트가 앞 이벤트를 기다리지 않는다. 대신 순서 보장이 없다. 다른 노트 작성자들의 빌드로 VM 부하가 높을 때 미리 돌려 본 실행에서는 `mdev -d` 중앙값이 245 ms까지 늘었다 — 이런 수치는 부하에 민감하다.
- 비교용으로, VM 호스트(네이티브 aarch64, systemd-udevd)에서 예제 14의 rule을 둔 채 `losetup` 직후부터 `/dev/g9/sensorlog` 링크가 보일 때까지 20번 잰 결과는 다음과 같다(Python 스크립트, `losetup` 실행 시간은 제외).

```text
udev link after losetup returns: n=20 min=2600 median=8473 p90=18883 max=476017 us
losetup itself: median=3364 us
```

max 476 ms 같은 꼬리는 udevd가 다른 이벤트(다른 작업자들의 활동)를 처리 중이었을 때다. 장비에서 "카메라를 꽂고 앱이 열 때까지"의 지연 예산을 잡을 때는 **중앙값이 아니라 꼬리**를 봐야 한다. 앱은 고정 `sleep` 대신 libudev monitor나 systemd `.device` 의존으로 "준비됨"을 기다린다.

---

## 10. 커널 안에서는 무슨 일이 일어나나

`echo "front SN1001" > /sys/kernel/g9_hot/add` 한 줄이 커널 안에서 어떻게 퍼지는지, 예제 4의 SEQNUM과 함께 따라가 보자.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 470">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="60.0" y="20.0" font-size="12" text-anchor="middle">user</text><line x1="60" y1="28" x2="60" y2="460" stroke="#888" stroke-width="1" stroke-dasharray="4 4"/><text x="210.0" y="20.0" font-size="12" text-anchor="middle">g9_hot / platform</text><line x1="210" y1="28" x2="210" y2="460" stroke="#888" stroke-width="1" stroke-dasharray="4 4"/><text x="370.0" y="20.0" font-size="12" text-anchor="middle">driver core</text><line x1="370" y1="28" x2="370" y2="460" stroke="#888" stroke-width="1" stroke-dasharray="4 4"/><text x="510.0" y="20.0" font-size="12" text-anchor="middle">g9_imu</text><line x1="510" y1="28" x2="510" y2="460" stroke="#888" stroke-width="1" stroke-dasharray="4 4"/><text x="615.0" y="20.0" font-size="12" text-anchor="middle">devtmpfs · uevent</text>
<line x1="615" y1="28" x2="615" y2="460" stroke="#888" stroke-width="1" stroke-dasharray="4 4"/><line x1="60" y1="52" x2="210" y2="52" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><text x="135.0" y="47.0" font-size="12" text-anchor="middle">echo &gt; add</text><line x1="210" y1="84" x2="370" y2="84" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><text x="290.0" y="79.0" font-size="12" text-anchor="middle">platform_device_add()</text><rect x="300" y="98" width="140" height="26" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="370.0" y="115.2" font-size="12" text-anchor="middle">device_add()</text><line x1="370" y1="148" x2="615" y2="148" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><text x="492.5" y="143.0" font-size="12" text-anchor="middle">kobject_uevent(ADD) #2206</text><text x="615.0" y="168.0" font-size="12" text-anchor="end">MODALIAS · G9_*</text>
<rect x="290" y="180" width="160" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="370.0" y="197.2" font-size="12" text-anchor="middle">bus_probe_device()</text><line x1="370" y1="232" x2="510" y2="232" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><text x="440.0" y="227.0" font-size="12" text-anchor="middle">really_probe()</text><rect x="450" y="244" width="120" height="26" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510.0" y="261.2" font-size="12" text-anchor="middle">g9_probe()</text><line x1="510" y1="296" x2="370" y2="296" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><text x="440.0" y="291.0" font-size="12" text-anchor="middle">device_create()</text><rect x="300" y="308" width="140" height="26" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="370.0" y="325.2" font-size="12" text-anchor="middle">device_add()</text>
<line x1="370" y1="354" x2="615" y2="354" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><text x="492.5" y="349.0" font-size="12" text-anchor="middle">devtmpfs_create_node()</text><line x1="370" y1="390" x2="615" y2="390" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><text x="492.5" y="385.0" font-size="12" text-anchor="middle">kobject_uevent(ADD) #2207</text><line x1="370" y1="426" x2="615" y2="426" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><text x="492.5" y="421.0" font-size="12" text-anchor="middle">driver_bound(): BIND #2208</text><text x="10.0" y="456.0" font-size="12" text-anchor="start">같은 device_add() 안에서 노드 생성이 uevent #2207보다 먼저</text>
</svg>
```
그림 6 — 장치 하나 추가가 만드는 커널 호출 사슬과 uevent 세 통. devtmpfs 노드는 class 장치의 `device_add()` 안에서, uevent를 보내기 **전에** 생긴다.

| 단계 | 함수 (v6.18) | 파일 | 결과 |
|---|---|---|---|
| sysfs write | `kernfs_fop_write_iter()` → `sysfs_kf_write()` → `kobj_attr_store()` → `add_store()` | fs/kernfs/file.c, fs/sysfs/file.c, lib/kobject.c | 우리 코드 |
| 장치 등록 | `platform_device_add()` → `device_add()` | drivers/base/platform.c, core.c | `/sys/devices/platform/g9-imu.0` |
| uevent #1 | `kobject_uevent(KOBJ_ADD)` → `kobject_uevent_env()` → `dev_uevent()` → `platform_uevent()`, `g9_type_uevent()` | lib/kobject_uevent.c | add, MODALIAS |
| 매칭 | `bus_probe_device()` → `__device_attach()` → `platform_match()` (id_table) → `really_probe()` → `g9_probe()` | drivers/base/bus.c, dd.c | D3 참고 |
| 노드 | `device_create()` → `device_add()` → `devtmpfs_create_node()` → (kdevtmpfs 스레드가 `vfs_mknod`) | drivers/base/devtmpfs.c | `/dev/g9_imu0` 0640 |
| uevent #2 | 같은 `device_add()` 안의 `kobject_uevent(KOBJ_ADD)` → `g9_class_uevent()` | | add, DEVNAME |
| uevent #3 | `driver_bound()` → `kobject_uevent(KOBJ_BIND)` | drivers/base/dd.c | bind, DRIVER |
| 배달 | `kobject_uevent_net_broadcast()` → `netlink_broadcast()` / (helper면) `call_usermodehelper_exec(UMH_NO_WAIT)` | lib/kobject_uevent.c, kernel/umh.c | 유저 공간 |
| 모듈 로드 (유저가 시작) | `modprobe` → `finit_module()` → `load_module()` → `do_init_module()` | kernel/module/main.c | E3 참고. 로드 자체도 `add@/module/g9_imu` uevent를 낸다 |

드라이버를 쓸 때 다시 만나는 지점:

- **G2/G3**: `bus_type.uevent`, `device_type`, `driver_bound()`는 디바이스 모델 노트의 그 구조체들이다. 버스 드라이버를 새로 쓰면(예: 사내 FPGA 버스) **`.uevent`에서 MODALIAS를 만들고, file2alias가 이해하는 테이블 타입**이 있어야 자동 로드가 된다 — 없으면 `MODULE_ALIAS("g9bus:...")`를 손으로 쓴다.
- **G4/G7**: `device_create()`(또는 `misc_register()`)가 devtmpfs 노드와 uevent를 동시에 만든다. 노드 권한을 커널에서 정하려면 class의 `devnode`, 정책으로 정하려면 udev rule.
- **G10**: `request_firmware()`의 유저 헬퍼 fallback(`CONFIG_FW_LOADER_USER_HELPER`)도 uevent(`SUBSYSTEM=firmware`, `FIRMWARE=...`)로 유저 공간을 부른다. mdev의 `FEATURE_MDEV_LOAD_FIRMWARE`가 그걸 처리한다.
- **I(IIO)·V4L2**: 서브시스템이 만든 장치(`iio:device0`, `video0`)도 같은 경로로 uevent를 낸다. 안정 이름 rule은 그 이벤트에 건다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트의 `/dev` 설계 — 위치 이름으로 열기

센서 키트(앞·뒤 IMU 두 개, USB GNSS, 카메라 두 대, CAN 두 개)의 앱이 `/dev/iio:device0`, `/dev/ttyUSB0`, `/dev/video0`, `can0`을 하드코딩하면, 부팅마다 probe 순서가 바뀌거나 USB 포트를 바꿔 꽂을 때 앞·뒤가 뒤바뀐다. 실무 표준은 **udev rule 하나로 역할 이름을 만들고 앱은 그 이름만 연다**:

```text
# /etc/udev/rules.d/70-sensorkit.rules (예시 — 실제 장비 값으로 바꿔야 함, 이 노트에서 실행하지 않음)
SUBSYSTEM=="iio", KERNELS=="2-0068", SYMLINK+="sensorkit/imu-front", GROUP="sensors", MODE="0660"
SUBSYSTEM=="iio", KERNELS=="2-0069", SYMLINK+="sensorkit/imu-rear",  GROUP="sensors", MODE="0660"
SUBSYSTEM=="tty", ATTRS{idVendor}=="1546", ATTRS{idProduct}=="01a8", SYMLINK+="sensorkit/gnss", \
  TAG+="systemd", ENV{SYSTEMD_WANTS}+="gpsd.service"
SUBSYSTEM=="video4linux", ENV{ID_PATH}=="platform-*-usb-0:1:1.0", ATTR{index}=="0", SYMLINK+="sensorkit/cam-front"
SUBSYSTEM=="net", ACTION=="add", KERNELS=="30a00000.can", NAME="can_motor"
```

마지막 줄처럼 네트워크 인터페이스는 `NAME=`으로 진짜 이름을 바꿀 수 있다(systemd 시스템이면 `.link` 파일이 더 권장된다 — 예제 11의 `ID_NET_LINK_FILE`이 그 흔적). 1546:01a8은 u-blox GNSS 수신기의 USB ID로 알려진 값이지만, 실제 장비에서는 `udevadm info -a`로 반드시 확인한다.

### 11.2 mdev 장비의 부팅 스크립트

Buildroot/BusyBox 장비의 `/etc/init.d/rcS`에서 흔히 보는 순서(이 노트의 예제 9·10을 이어 붙인 것):

```sh
mount -t devtmpfs devtmpfs /dev          # (initramfs라면 직접)
echo > /dev/mdev.seq
mdev -d                                  # helper 없는 커널: 데몬. helper 커널이면 echo /sbin/mdev > /proc/sys/kernel/hotplug
mdev -s                                  # coldplug: 기존 노드에 mdev.conf 적용
find /sys/devices -name modalias -exec cat {} + | sort -u | xargs -n1 modprobe -q 2>/dev/null   # coldplug 모듈 로드
```

(위 블록은 패턴 정리이고 이 순서 그대로 한 번에 실행해 보지는 않았다. 각 줄의 동작은 예제 9, 10, D3 예제 13에서 확인했다.)

### 11.3 부팅 시간 — "udev settle 3초"

데스크톱 배포판 기반 이미지를 장비에 올리면 `systemd-analyze blame`에서 `systemd-udev-trigger`·`systemd-udev-settle`이 수 초를 먹는 경우가 많다. 대응:

- 장비에 필요한 드라이버는 **built-in(`=y`)** 으로 넣어 coldplug 모듈 로드를 줄인다.
- `systemd-udev-settle.service`에 의존하는 유닛을 찾아 장치 단위 의존(`BindsTo=dev-xxx.device`)으로 바꾼다.
- 정말 작은 장비면 udev 대신 devtmpfs + mdev, 또는 devtmpfs only + 고정 권한(`devnode` 콜백)으로 간다. 예제 4처럼 **커널이 이미 0640으로 노드를 만들어 주면** 데몬 없이도 충분한 경우가 있다.

### 11.4 "드라이버가 자동으로 안 올라와요" 진단 순서

```sh
cat /sys/bus/usb/devices/1-1:1.0/modalias          # 1) 장치가 내놓은 문자열
modprobe -R "$(cat /sys/bus/usb/devices/1-1:1.0/modalias)"   # 2) 어느 모듈로 풀리나 (없으면 alias 없음 → MODULE_DEVICE_TABLE/ depmod 확인)
modinfo -F alias mydrv                              # 3) 모듈 쪽 alias
udevadm monitor --kernel --udev --property          # 4) 꽂을 때 MODALIAS가 실제로 오나, UDEV까지 가나
udevadm test /sys/bus/usb/devices/1-1:1.0           # 5) 80-drivers.rules가 kmod load를 큐에 넣나
```

(경로 `1-1:1.0`은 예시다. 각 명령의 동작은 예제 8·11·13에서 실제 장치로 확인했다.) Yocto에서 흔한 원인은 `.ko`는 이미지에 들어갔는데 **`depmod`가 이미지 빌드 때 안 돌아서** `modules.alias`가 비어 있는 경우, 그리고 커스텀 커널 모듈 레시피가 `MODULE_DEVICE_TABLE`을 빠뜨린 경우다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `MODULE_DEVICE_TABLE` 누락 | insmod하면 잘 되는데 제품에서 드라이버가 안 올라옴 | `.ko`에 alias가 없음 | 매크로 추가, `modinfo -F alias`로 확인 |
| 이미지에 `depmod` 안 돌림 | `modprobe -R <modalias>`가 아무것도 안 찍음 | `modules.alias`/`modules.dep.bb`가 낡음 | 빌드 후 `depmod -b <rootfs> <ver>` (BusyBox면 부팅 때 `depmod`) |
| mdev가 `-s`만 실행 | 부팅 후 꽂은 장치는 노드가 없거나, coldplug 장치 모듈이 안 올라옴 | hotplug 경로(helper/`-d`)가 없음, `-s`는 modalias를 안 봄 | `mdev -d` 또는 helper 설정 + modalias 루프 |
| uevent_helper를 상시 사용 | 부팅 중 프로세스 폭주·OOM, 가끔 노드 누락 | 이벤트당 fork, `UMH_NO_WAIT` 순서 뒤섞임 | `mdev -d`/udevd로, 최소한 `/dev/mdev.seq` |
| rule에 `NAME="mydev"` (char/block) | 무시됨 | 노드 이름은 devtmpfs(커널)가 정함 | `SYMLINK+=` 사용 |
| `ATTRS{}` 두 개를 서로 다른 부모 속성으로 | rule이 절대 안 맞음 | 한 줄의 복수 키는 **같은 부모 하나**에서 맞아야 | `udevadm info -a`에서 한 블록 안의 키만 조합 |
| `RUN+=`에서 긴 작업·데몬 실행 | 이벤트 처리 지연, 프로그램이 죽음 | RUN은 이벤트 처리 일부, 타임아웃에 걸리면 죽임 | `TAG+="systemd", ENV{SYSTEMD_WANTS}+=` 로 서비스 시작 |
| rule 파일만 고치고 끝 | 변화 없음 | 규칙은 다음 이벤트부터 적용 | `udevadm control --reload` + `udevadm trigger` |
| 앱이 group 1(커널) 이벤트를 보고 바로 open | 가끔 `EACCES`/`ENOENT` | udev가 아직 권한·링크를 안 붙임 | libudev monitor(group 2) 또는 systemd `.device` 의존 |
| mdev 훅에서 부모 uevent 변수를 기대 | 변수가 빈 문자열 (예제 9의 `serial=`) | uevent 변수는 kobject별, 상속 안 됨 | 드라이버가 class 쪽에도 넣기, 또는 `/sys/$DEVPATH/..` 읽기 |

---

## 13. 면접에서 이렇게 말한다

**Q.** What happens, end to end, when you plug in a USB device on a Linux system?

**A.** USB 코어가 enumerate해서 `usb_device`/`usb_interface`를 만들고 `device_add()`를 부른다. 그 안에서 devtmpfs가 노드를 만들고, `kobject_uevent(KOBJ_ADD)`가 `ACTION`, `DEVPATH`, `SUBSYSTEM`, `MODALIAS=usb:v...p...` 같은 변수를 담은 uevent를 netlink로 방송한다. udevd가 받아 rule을 돌린다 — `80-drivers.rules`가 MODALIAS로 kmod를 불러 `modules.alias`에서 드라이버를 찾아 로드하고, 드라이버가 probe되면 tty·video 같은 class 장치가 생기며 또 uevent가 온다. 그 이벤트에서 udev가 권한·그룹·`/dev/serial/by-id` 같은 링크를 만들고, systemd 태그가 있으면 `.device` 유닛을 활성화한다.

> The USB core enumerates the device and registers it with device_add(). devtmpfs creates the node, and kobject_uevent sends an add event over netlink with ACTION, DEVPATH, SUBSYSTEM and a MODALIAS string built from the vendor, product and interface IDs. udevd runs its rules: the drivers rule calls kmod with the MODALIAS, which looks it up in modules.alias and loads the matching driver. When the driver probes, it registers a class device like a tty, which generates another uevent, and on that one udev applies ownership, permissions and persistent symlinks such as /dev/serial/by-id, and tags it for systemd so a device unit becomes active.

**꼬리 질문**: "Where does modules.alias come from?" → `MODULE_DEVICE_TABLE` → modpost(file2alias)가 `.mod.c`에 `MODULE_ALIAS` → `.modinfo` → depmod.

**Q.** How does a kernel module get loaded automatically when its device appears?

**A.** 드라이버의 매칭 테이블에 `MODULE_DEVICE_TABLE`을 붙이면 빌드 때 modpost가 테이블을 alias 문자열로 바꿔 `.ko`의 `.modinfo`에 넣는다. depmod가 모든 alias를 `modules.alias`로 모은다. 장치가 생기면 버스의 `uevent` 콜백이 같은 형식의 `MODALIAS`를 만들어 보내고, udev나 mdev가 `modprobe $MODALIAS`를 실행해 glob 매칭으로 모듈을 찾는다. 커널이 `request_module()`로 직접 modprobe를 부르는 경로는 별개다.

> The driver annotates its ID table with MODULE_DEVICE_TABLE. At build time modpost turns each entry into an alias string and stores it in the module's modinfo, and depmod collects all aliases into modules.alias. When a device appears, the bus's uevent callback emits a MODALIAS in the same format, and udev or mdev runs modprobe with it; modprobe glob-matches it against the alias index and loads the module. That's separate from request_module, where the kernel itself execs modprobe, for example for a socket family or filesystem.

**Q.** What's the difference between devtmpfs and udev? Do I need udev?

**A.** devtmpfs는 커널이 `device_add()` 안에서 직접 노드를 만드는 파일시스템이다. 이름과 기본 권한은 커널(드라이버의 `devnode`)이 정한다. udev는 그 위에 정책 — 소유자·그룹·권한, 안정된 심볼릭 링크, 모듈 로드, 프로그램 실행 — 을 얹는다. 작은 장비는 devtmpfs만, 또는 devtmpfs + mdev로 충분하다. 핫플러그 장치·모듈 자동 로드·권한 정책이 필요하면 udev나 mdev가 있어야 한다.

> devtmpfs is the kernel creating device nodes itself, synchronously inside device_add, with the name and default mode coming from the driver. udev adds policy on top: ownership and permissions, persistent symlinks, module autoloading and running programs. A small fixed-function device can run on devtmpfs alone, or devtmpfs plus BusyBox mdev. If you have hotplug devices or need autoloading and permission policy, you want udev or mdev.

**Q.** We have two identical IMUs (or cameras). How do you make sure the application always opens the right one?

**A.** 커널이 붙이는 번호(`iio:device0`, `video0`)는 probe 순서에 따라 바뀔 수 있으니 쓰지 않는다. 바뀌지 않는 속성 — I2C 버스 번호와 주소(DT가 고정), USB 물리 포트 경로(`ID_PATH`), 일련번호 — 로 udev rule을 써서 `/dev/sensorkit/imu-front` 같은 역할 이름 링크를 만들고, 앱은 그것만 연다. `udevadm info -a`로 키를 고르고 `udevadm test`로 검증한다. 드라이버를 우리가 쓴다면 위치 정보를 uevent 변수로 내보내는 것도 방법이다.

> I never rely on kernel enumeration numbers like video0 because probe order can change. I write udev rules that match stable attributes — the I2C bus and address fixed by the device tree, the USB physical port path via ID_PATH, or a serial number — and create role-based symlinks like /dev/sensorkit/imu-front. The application only opens those. I pick the keys with udevadm info -a and verify the rule with udevadm test before deploying it.

**꼬리 질문**: "Why can't you combine ATTRS from two different parents?" → udev는 한 규칙 줄의 복수 키들을 부모 하나에서 같이 맞아야 하는 것으로 평가한다.

**Q.** How would you add custom information to a device's uevent from a driver?

**A.** `add_uevent_var()`를 세 군데 중 하나에서 부른다: 버스 공통이면 `bus_type.uevent`, class 공통이면 `class.dev_uevent`, 특정 장치 종류면 `device_type.uevent`. driver core의 `dev_uevent()`가 이 순서로 부른다. 상태 변화를 알릴 때는 `kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp)`로 change 이벤트를 보낸다. 다만 uevent는 버퍼 2 KB·64개 제한이 있고 비싼 경로라 고빈도 데이터 통지에는 쓰지 않는다.

> I'd call add_uevent_var from the right hook: the bus_type uevent callback for bus-wide keys like MODALIAS, the class dev_uevent for keys common to a class, or a device_type uevent for one kind of device. dev_uevent in the driver core calls them in that order. For state changes I'd send a change event with kobject_uevent_env and an extra envp array. I keep it small — the environment is limited to 2 KB and 64 keys — and I don't use uevents for high-rate notifications; that's what poll or IIO buffers are for.

**Q.** What is coldplug and how does it work?

**A.** 커널은 유저 공간이 뜨기 전에 대부분의 장치를 만들기 때문에 그때 uevent는 아무도 못 들었다. udevd가 뜬 뒤 `udevadm trigger`가 `/sys`의 모든 장치의 `uevent` 파일에 `add`를 써서 커널이 같은 이벤트를 다시 보내게 하고, 그래서 모듈 로드·권한·링크가 적용된다. mdev는 `mdev -s`로 `/sys/dev`를 훑지만 이벤트를 재생하지는 않아서 모듈 로드는 따로 해야 한다.

> Most devices are created before userspace starts, so their uevents had no listener. After udevd starts, udevadm trigger writes "add" into every device's uevent file in sysfs, and the kernel re-emits a synthetic event with the same variables, so rules and module loading get applied. BusyBox mdev -s only scans /sys/dev to create nodes; it doesn't replay events, so module autoloading needs a separate modalias loop.

**Q.** What is /proc/sys/kernel/hotplug and why isn't it used anymore?

**A.** uevent_helper 경로다. 설정되면 커널이 이벤트마다 그 프로그램을 `call_usermodehelper`로 실행한다. 데몬이 필요 없어 초기 initramfs나 mdev에 쓰였지만, 이벤트마다 fork·exec라 부팅 때 부하·OOM 위험이 있고 비동기 실행이라 순서가 뒤섞일 수 있다. 지금은 netlink를 듣는 데몬(udevd, `mdev -d`)이 표준이고 많은 커널이 `CONFIG_UEVENT_HELPER`를 끈다.

> It's the uevent helper path. If set, the kernel execs that program for every uevent through call_usermodehelper, with the event variables as its environment. It needs no daemon, so it was used in early initramfs and by mdev, but it forks a process per event, which can overload or even OOM a small system at boot, and since it's asynchronous the events can be processed out of order. Today a daemon listening on the netlink socket — udevd or mdev -d — is the norm, and many kernels disable CONFIG_UEVENT_HELPER.

---

## 14. 직접 해보기

1. (개념) 예제 4에서 `echo "front SN1001" > add` 한 번에 uevent가 세 통 나왔다. 각각의 ACTION·SUBSYSTEM과, 그 이벤트를 만든 커널 함수를 순서대로 적어라.
정답/힌트: platform `add`(`platform_device_add` → `device_add`), g9 class `add`(probe 안의 `device_create` → `device_add`), platform `bind`(`driver_bound`). 그림 6.

2. (코드) `g9_imu`의 class uevent 콜백에 `G9_SERIAL`도 넣고 예제 9를 다시 돌려 hook log의 `serial=`이 채워지는지 확인하라.
정답/힌트: `g9_class_uevent()`에서 `add_uevent_var(env, "G9_SERIAL=%s", in->serial)`를 하나 더. mdev 훅은 노드 이벤트의 환경만 본다.

3. (코드) `g9_imu.c`에서 `MODULE_DEVICE_TABLE` 줄을 지우고 다시 빌드해 `modinfo -F alias g9_imu.ko`와 예제 9의 lsmod 결과를 비교하라.
정답/힌트: alias가 비고, mdev의 `modprobe "platform:g9-imu"`가 실패해 `g9_imu`가 올라오지 않는다. `/dev/sensorkit/`도 생기지 않는다.

4. (관찰) 호스트에서 `udevadm monitor --kernel --udev --property`를 켜 두고 `sudo losetup -f --show <파일>`을 실행해, KERNEL 이벤트와 UDEV 이벤트의 키 차이를 표로 정리하라. 끝나면 `losetup -d`.
정답/힌트: UDEV 쪽에만 `DEVLINKS`, `ID_LOOP_*`, `TAGS`, `USEC_INITIALIZED`. 예제 13의 Properties와 비교.

5. (rule) 예제 13의 rule을 `ATTR{loop/backing_file}` 대신 `ENV{ID_LOOP_BACKING_FILENAME}`으로 매칭하도록 바꾸고 `udevadm test -v -D`로 확인하라. 어느 쪽이 더 이른 시점에도 동작하나?
정답/힌트: `ID_LOOP_*`는 앞선 rule(`60-persistent-storage` 계열)이 만든 속성이라 우리 파일이 그보다 **뒤 번호**여야 보인다. 99번이면 된다. sysfs 속성은 순서와 무관.

6. (mdev) `/etc/mdev.conf`의 두 줄 순서를 바꿔(`$MODALIAS=.*`를 먼저) 예제 9를 돌리면 어떻게 되나? 예상하고 실행해 보라.
정답/힌트: mdev는 첫 매칭에서 멈춘다. class 장치 이벤트에는 MODALIAS가 없으므로 g9 줄까지 내려가 결과는 같을 것으로 예상되지만, MODALIAS가 있는 장치 이벤트(platform `bind` 등)에서 modprobe가 다시 불린다. 실행해서 `ps`/hook log로 확인.

7. (측정) 예제 16을 `-smp 2`로 다시 돌려 mdev helper와 `mdev -d`의 차이가 어떻게 바뀌는지 그래프로 그려라.
정답/힌트: 이 VM은 TCG 에뮬레이션이라 fork·exec 비용이 실제 하드웨어보다 훨씬 크다. 절대값보다 비율을 보라.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| uevent | 유저 이벤트 | 장치 추가·제거·변경 때 커널이 보내는 `ACTION@DEVPATH` + `KEY=VALUE` 메시지 |
| `kobject_uevent_env()` | uevent 생성 함수 | 변수 버퍼를 만들고 kset/bus/class/type 콜백을 불러 netlink·helper로 보냄 |
| `add_uevent_var()` | 변수 추가 | `env`에 `KEY=VALUE` 하나를 붙임 (2048 B, 64개 한도) |
| SEQNUM | 이벤트 번호 | 커널 전역 단조 증가 카운터, 순서 판단용 |
| MODALIAS | 모듈 별칭 키 | 장치가 내보내는 "어떤 드라이버가 필요한가" 문자열 |
| `MODULE_DEVICE_TABLE` | 테이블 내보내기 매크로 | 매칭 테이블을 alias 문자열로 `.ko`에 남기게 하는 표시 |
| file2alias | alias 생성기 | modpost의 일부, 버스별 테이블을 alias 문자열로 변환 |
| `modules.alias` | alias 색인 | depmod가 만든 "alias glob → 모듈" 표 (BusyBox는 `modules.dep.bb`) |
| netlink uevent | 배달 경로 | `NETLINK_KOBJECT_UEVENT` 멀티캐스트, group 1=커널, group 2=udev |
| uevent_helper | 헬퍼 프로그램 | `/proc/sys/kernel/hotplug`, 이벤트마다 커널이 실행 |
| devtmpfs | 커널 관리 /dev | `device_add()`에서 커널이 노드를 직접 만드는 파일시스템 |
| udev / systemd-udevd | 장치 관리자 | rule로 권한·링크·모듈 로드·실행을 정하는 데몬 |
| mdev | BusyBox 장치 관리자 | `/etc/mdev.conf` 기반, helper 또는 `-d` 데몬 |
| coldplug | 콜드플러그 | 부팅 전 생긴 장치의 이벤트를 `uevent` 파일 쓰기로 재생 |
| `udevadm test` | 규칙 dry-run | 이벤트를 시뮬레이션해 적용될 결과만 출력 |
| `ATTRS{}` | 부모 속성 매칭 | 자신 또는 부모 하나의 sysfs 속성과 비교 |
| `.device` 유닛 | systemd 장치 유닛 | `TAG+="systemd"` 장치마다 생기는 유닛, 서비스 의존에 사용 |

---

## 16. 요약 & 체크리스트

장치가 생기면 driver core의 `device_add()`가 (노드가 있으면) devtmpfs로 `/dev` 노드를 먼저 만들고, `kobject_uevent(KOBJ_ADD)`로 `ACTION`/`DEVPATH`/`SUBSYSTEM`에 `dev_uevent()`·bus·class·device_type 콜백이 `add_uevent_var()`로 붙인 변수(MAJOR/MINOR/DEVNAME, DRIVER, `OF_*`, MODALIAS, 드라이버 고유 키)와 `SEQNUM`을 담은 uevent를 만든다. 이 메시지는 netlink 멀티캐스트로 방송되고, 설정돼 있으면 uevent_helper(`/proc/sys/kernel/hotplug`, 이벤트마다 fork)로도 전달된다. 유저 공간의 udevd 또는 mdev가 이를 받아 rule대로 권한·소유자·안정 이름 링크를 만들고, `MODALIAS`로 `modprobe`를 불러 `MODULE_DEVICE_TABLE` → file2alias → `.modinfo` → depmod 색인으로 이어진 사슬을 통해 드라이버를 자동 로드한다. 부팅 전 생긴 장치는 sysfs `uevent` 파일 쓰기(coldplug)로 이벤트를 재생한다. udev rule은 파일 이름순·줄 단위로 모두 평가하고 복수 키는 부모 하나에서 맞아야 하며, mdev.conf는 첫 매칭에서 멈춘다. 이 노트의 실측으로는 devtmpfs 노드가 데몬보다 훨씬 먼저 생기고, 이벤트마다 셸을 띄우는 mdev는 그 위에 수십 ms(TCG 에뮬레이션, 부하가 높으면 수백 ms)를 더한다.

- [ ] 장치 추가 한 번이 만드는 uevent들(add / class add / bind)과 그 순서를 설명할 수 있다
- [ ] `kobject_uevent_env()`가 변수를 넣는 순서와 bus·class·device_type 콜백의 자리를 말할 수 있다
- [ ] 드라이버에서 `add_uevent_var()`로 변수를 넣는 코드를 쓸 수 있다
- [ ] netlink(group 1/2)와 uevent_helper의 차이, helper의 단점을 설명할 수 있다
- [ ] `MODULE_DEVICE_TABLE`에서 `modprobe $MODALIAS`까지의 사슬을 빌드·설치·실행 단계로 나눠 설명할 수 있다
- [ ] coldplug가 왜 필요하고 `udevadm trigger`/`mdev -s`가 무엇을 하는지 설명할 수 있다
- [ ] devtmpfs와 udev의 역할 분담(노드 vs 정책)을 설명할 수 있다
- [ ] udev rule을 `==`/`=`/`+=`/`:=`와 `ATTRS{}`로 쓰고 `udevadm verify`·`udevadm test`로 검증할 수 있다
- [ ] rule을 설치·reload·trigger로 적용하고 깨끗하게 제거할 수 있다
- [ ] `/etc/mdev.conf`로 권한·이동·스크립트 실행·modalias 로드를 설정할 수 있다
- [ ] 같은 센서 여러 개에 안정된 이름을 주는 방법 세 가지를 말할 수 있다
- [ ] "드라이버가 자동으로 안 올라온다"를 modalias → `modprobe -R` → monitor → test 순으로 진단할 수 있다

---

## 참고 자료

- docs.kernel.org — driver model: https://docs.kernel.org/driver-api/driver-model/index.html
- docs.kernel.org — "Everything you never wanted to know about kobjects, ksets, and ktypes" (uevent 절 포함): https://docs.kernel.org/core-api/kobject.html
- man7.org — `udev(7)`: https://man7.org/linux/man-pages/man7/udev.7.html
- man7.org — `udevadm(8)`: https://man7.org/linux/man-pages/man8/udevadm.8.html
- man7.org — `systemd-udevd.service(8)`: https://man7.org/linux/man-pages/man8/systemd-udevd.service.8.html
- man7.org — `systemd.device(5)`: https://man7.org/linux/man-pages/man5/systemd.device.5.html
- man7.org — `netlink(7)`: https://man7.org/linux/man-pages/man7/netlink.7.html
- man7.org — `modprobe(8)`, `depmod(8)`: https://man7.org/linux/man-pages/man8/modprobe.8.html, https://man7.org/linux/man-pages/man8/depmod.8.html
- BusyBox 소스 `docs/mdev.txt`, `examples/mdev_fat.conf`, `util-linux/mdev.c` (랩의 `/var/lab/src/busybox-1.37.0`)
- 커널 소스 (v6.18): `lib/kobject_uevent.c`(`kobject_uevent_env`, `add_uevent_var`, `kobject_synth_uevent`, `zap_modalias_env`), `drivers/base/core.c`(`device_add`, `dev_uevent`, `uevent_store`), `drivers/base/devtmpfs.c`, `drivers/base/dd.c`(`driver_bound`), `drivers/base/platform.c`(`platform_uevent`), `drivers/base/Kconfig`(`UEVENT_HELPER`), `scripts/mod/file2alias.c`, `scripts/mod/modpost.c`, `include/linux/module.h` — https://elixir.bootlin.com/linux/v6.18/source
- systemd 소스의 기본 규칙: `rules.d/80-drivers.rules`, `rules.d/60-persistent-storage.rules` — https://github.com/systemd/systemd/tree/main/rules.d
- Bootlin "Embedded Linux system development" 슬라이드 (devtmpfs·mdev·udev 절): https://bootlin.com/training/embedded-linux/
- 이 저장소의 선행 노트: A9 §11.3(uevent 리스너), D3 §9(modalias·coldplug), E3 §11(depmod·modprobe)
